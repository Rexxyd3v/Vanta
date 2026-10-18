#include "MapRegistry.h"
#include "scenes/map_scene.h"
#include <raylib.h>
#include <iostream>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

MapRegistry& MapRegistry::GetInstance() {
    static MapRegistry instance;
    return instance;
}

MapRegistry::~MapRegistry() {
    UnloadAll();
}

// ---------------------------------------------------------------------------
// Helper: if the scene has no spawn points (no registered builder, or the
// builder returned none), auto-generate a 3x3 grid that scales with the map.
// This makes any new .tmx folder in assets/Maps/ work out-of-the-box.
// ---------------------------------------------------------------------------
// Tiled point objects keep custom spawn positions editable alongside the map.
static void LoadTmxSpawnPoints(MapData& data) {
    if (!data.tmxMap) return;
    for (uint32_t i=0;i<data.tmxMap->layersLength;++i) {
        const auto& layer=data.tmxMap->layers[i];
        if (layer.type!=LAYER_TYPE_OBJECT_GROUP || !layer.name || std::strcmp(layer.name,"spawns")!=0) continue;
        std::vector<MapSpawnPoint> points;
        const auto& group=layer.exact.objectGroup;
        for (uint32_t j=0;j<group.objectsLength;++j) {
            const auto& object=group.objects[j];
            if (object.type==OBJECT_TYPE_POINT && object.x>=0 && object.y>=0 &&
                object.x<data.pixelWidth && object.y<data.pixelHeight)
                points.push_back({{(float)object.x,(float)object.y}});
        }
        if (!points.empty()) data.scene.spawnPoints=std::move(points);
    }
}

static void AutoFillSpawnPoints(MapData& mapData) {
    if (!mapData.scene.spawnPoints.empty()) return; // already provided

    // Inset 10 % from each edge so spawns are never against the boundary.
    const float insetX = mapData.pixelWidth  * 0.10f;
    const float insetY = mapData.pixelHeight * 0.10f;
    const float spanX  = mapData.pixelWidth  - insetX * 2.0f;
    const float spanY  = mapData.pixelHeight - insetY * 2.0f;

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            MapSpawnPoint sp;
            sp.position = {
                insetX + spanX * (col / 2.0f),
                insetY + spanY * (row / 2.0f)
            };
            mapData.scene.spawnPoints.push_back(sp);
        }
    }
    TraceLog(LOG_INFO, "MapRegistry: Auto-generated 9 spawn points for map '%s'",
             mapData.name.c_str());
}

void MapRegistry::LoadAllMaps(const char* mapsRoot) {
    UnloadAll();

#ifdef _WIN32
    // Scan for subdirectories under mapsRoot
    std::string searchPath = std::string(mapsRoot) + "*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        TraceLog(LOG_WARNING, "MapRegistry: Could not find maps directory '%s'", mapsRoot);
        return;
    }

    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            const char* dirName = findData.cFileName;
            // Skip "." and ".."
            if (strcmp(dirName, ".") == 0 || strcmp(dirName, "..") == 0)
                continue;

            std::string folderPath = std::string(mapsRoot) + dirName + "/";
            
            // Look for a .tmx file in this folder
            std::string tmxSearch = folderPath + "*.tmx";
            WIN32_FIND_DATAA tmxData;
            HANDLE hTmx = FindFirstFileA(tmxSearch.c_str(), &tmxData);
            if (hTmx == INVALID_HANDLE_VALUE) {
                continue; // No TMX file in this folder, skip
            }

            std::string tmxFile = tmxData.cFileName;
            std::string tmxPath = folderPath + tmxFile;
            FindClose(hTmx);

            // Load the TMX file
            TmxMap* tmxMap = LoadTMX(tmxPath.c_str());
            if (!tmxMap) {
                TraceLog(LOG_WARNING, "MapRegistry: Failed to load TMX '%s'", tmxPath.c_str());
                continue;
            }

            MapData mapData;
            mapData.name       = dirName;
            mapData.folderPath = folderPath;
            mapData.tmxPath    = tmxPath;
            mapData.tmxMap     = tmxMap;
            mapData.pixelWidth  = (int)(tmxMap->width  * tmxMap->tileWidth);
            mapData.pixelHeight = (int)(tmxMap->height * tmxMap->tileHeight);

            // Build the scene (spawn points, etc.) from a registered builder, or
            // fall through to the auto-generator below.
            mapData.scene = MapSceneRegistry::GetInstance().Build(dirName);

            // Auto-fill spawn points for maps with no registered scene builder.
            LoadTmxSpawnPoints(mapData);
            AutoFillSpawnPoints(mapData);

            TraceLog(LOG_INFO, "MapRegistry: Loaded map '%s' (%dx%d px, %dx%d tiles)",
                     dirName, mapData.pixelWidth, mapData.pixelHeight,
                     tmxMap->width, tmxMap->height);

            maps[dirName] = std::move(mapData);
            mapOrder.push_back(dirName);
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
#else
    // Linux/macOS implementation using dirent.h
    DIR* dir = opendir(mapsRoot);
    if (!dir) {
        TraceLog(LOG_WARNING, "MapRegistry: Could not open maps directory '%s'", mapsRoot);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            const char* dirName = entry->d_name;
            if (strcmp(dirName, ".") == 0 || strcmp(dirName, "..") == 0)
                continue;

            std::string folderPath = std::string(mapsRoot) + dirName + "/";
            
            // Look for a .tmx file
            DIR* tmxDir = opendir(folderPath.c_str());
            if (!tmxDir) continue;
            
            struct dirent* tmxEntry;
            std::string tmxFile;
            while ((tmxEntry = readdir(tmxDir)) != nullptr) {
                std::string name = tmxEntry->d_name;
                if (name.size() > 4 && name.substr(name.size() - 4) == ".tmx" &&
                    tmxEntry->d_type == DT_REG) {
                    tmxFile = name;
                    break;
                }
            }
            closedir(tmxDir);
            
            if (tmxFile.empty()) continue;

            std::string tmxPath = folderPath + tmxFile;
            TmxMap* tmxMap = LoadTMX(tmxPath.c_str());
            if (!tmxMap) {
                TraceLog(LOG_WARNING, "MapRegistry: Failed to load TMX '%s'", tmxPath.c_str());
                continue;
            }

            MapData mapData;
            mapData.name       = dirName;
            mapData.folderPath = folderPath;
            mapData.tmxPath    = tmxPath;
            mapData.tmxMap     = tmxMap;
            mapData.pixelWidth  = (int)(tmxMap->width  * tmxMap->tileWidth);
            mapData.pixelHeight = (int)(tmxMap->height * tmxMap->tileHeight);

            // Build the scene from a registered builder, or auto-generate below.
            mapData.scene = MapSceneRegistry::GetInstance().Build(dirName);

            // Auto-fill spawn points for maps with no registered scene builder.
            LoadTmxSpawnPoints(mapData);
            AutoFillSpawnPoints(mapData);

            TraceLog(LOG_INFO, "MapRegistry: Loaded map '%s' (%dx%d px, %dx%d tiles)",
                     dirName, mapData.pixelWidth, mapData.pixelHeight,
                     tmxMap->width, tmxMap->height);

            maps[dirName] = std::move(mapData);
            mapOrder.push_back(dirName);
        }
    }
    closedir(dir);
#endif

    if (maps.empty()) {
        TraceLog(LOG_WARNING, "MapRegistry: No maps found in '%s'", mapsRoot);
    }
}

MapData* MapRegistry::GetMap(const std::string& name) {
    auto it = maps.find(name);
    if (it != maps.end())
        return &it->second;
    return nullptr;
}

MapData* MapRegistry::GetMapByIndex(int index) {
    if (index >= 0 && index < (int)mapOrder.size()) {
        return GetMap(mapOrder[index]);
    }
    return nullptr;
}

int MapRegistry::GetMapCount() const {
    return (int)maps.size();
}

std::vector<std::string> MapRegistry::GetMapNames() const {
    return mapOrder;
}

bool MapRegistry::HasMap(const std::string& name) const {
    return maps.find(name) != maps.end();
}

void MapRegistry::UnloadAll() {
    maps.clear();
    mapOrder.clear();
}
