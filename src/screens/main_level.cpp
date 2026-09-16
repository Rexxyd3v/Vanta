#include "main_level.h"
#include "../constants.h"
#include "../ui/ui_theme.h"
#include "../utils/texture_manager.h"
#include "../network/network_manager.h"
#include "../network/packets.h"
#include "../ui/ui_widgets.h"
#include "../map_loader/scenes/map_scene.h"
#include <iostream>
#include <math.h>
#include <algorithm>
#include <cstring>
#include "../voice/proximity_voice_chat.h"
extern ProximityVoiceChat proximityVoiceChat;


// Simple clamp helper (raylib Clamp may not be available without raymath)
static inline float fclamp(float val, float lo, float hi) {
    return val < lo ? lo : (val > hi ? hi : val);
}

static float netSendTimer = 0.0f;
static const float NET_SEND_RATE = 1.0f / 30.0f; // Send 30 updates per second
static const float GAMEPLAY_CAMERA_ZOOM = 0.5f;

GameplayScreen::GameplayScreen(GameMode mode)
    : currentMapData(nullptr),
      currentTmxMap(nullptr),
      worldWidth(DEFAULT_WORLD_WIDTH),
      worldHeight(DEFAULT_WORLD_HEIGHT),
      currentMode(mode),
      player(nullptr),
      player2(nullptr),
      spawnTimer(0.0f),
      netSendTimer(0.0f),
      worldTime(0.0f),
      wallsObjectGroup{} {
    MapRegistry& mapRegistry = MapRegistry::GetInstance();
    currentMapData = mapRegistry.GetMap(NetworkManager::GetInstance().selectedMapName);
    if (!currentMapData) {
        currentMapData = mapRegistry.GetMapByIndex(0);
    }

    std::string bgPath = "assets/Maps/Beach/";
    if (currentMapData) {
        currentTmxMap = currentMapData->tmxMap;
        worldWidth = (float)currentMapData->pixelWidth;
        worldHeight = (float)currentMapData->pixelHeight;
        mapFolderPath = currentMapData->folderPath;
        bgPath = mapFolderPath;
        NetworkManager::GetInstance().selectedMapName = currentMapData->name;
    }

    bgTex1   = TextureManager::GetTexture(bgPath + "background.png");
    bgTex2   = TextureManager::GetTexture(bgPath + "background.png");
    bgDetail = TextureManager::GetTexture(bgPath + "background.png");
    // Load all 9 health bar animation frames
    for (int i = 0; i < 9; i++) {
        healthBarFrames[i] = TextureManager::GetTexture(
            "assets/HealthBar/" + std::to_string(i + 1) + "_no_bg.png");
    }

    // Load all 6 dash bar animation frames
    for (int i = 0; i < 6; i++) {
        dashBarFrames[i] = TextureManager::GetTexture(
            "assets/DashBar/dash" + std::to_string(i + 1) + ".png");
    }

    // Load the character head portrait for the HUD circle (use the player's selected skin)
    {
        int skinIdx = NetworkManager::GetInstance().localSkinIndex;
        if (skinIdx < 1 || skinIdx > 4) skinIdx = 1;
        std::string headPath = "assets/Head_display/char" + std::to_string(skinIdx) + ".png";
        headPortrait = TextureManager::GetTexture(headPath);
        // Fallback to assets/Head_display/char1.png if not found
        if (headPortrait.id == 0) {
            headPortrait = TextureManager::GetTexture("assets/Head_display/char1.png");
        }
    }

    // Player starts near center of world, using selected skin
    {
        std::string charPath = "assets/Free 2D Animated Vector Game Character Sprites/Free 2D Animated Vector Game Character Sprites/Full body animated characters/";
        int skinIdx = NetworkManager::GetInstance().localSkinIndex;
        if (skinIdx < 1 || skinIdx > 4) skinIdx = 1;
        std::string skinPath = charPath + "Char " + std::to_string(skinIdx) + "/with hands/";
        player = new Player({worldWidth / 2.0f - 50.0f, worldHeight / 2.0f}, 0, skinPath, NetworkManager::GetInstance().localWeaponSkin);
        player->SetName(NetworkManager::GetInstance().localUsername.empty() ? "You" : NetworkManager::GetInstance().localUsername);
    }
    // In online mode player2 is a remote player, so we don't spawn them locally
    // player2 is unused now - all opponents come via network as RemotePlayer
    player2 = nullptr;

    // No AI companions in Online PvP mode
    // No AI companions needed - it's PvP!

    // Find all collision object groups from the TMX map
    allObjectGroups.clear();
    if (currentTmxMap) {
        for (uint32_t i = 0; i < currentTmxMap->layersLength; i++) {
            TmxLayer& layer = currentTmxMap->layers[i];
            if (layer.type == LAYER_TYPE_OBJECT_GROUP) {
                allObjectGroups.push_back(layer.exact.objectGroup);
                if (strcmp(layer.name, "collision") == 0) {
                    wallsObjectGroup = layer.exact.objectGroup;
                }
            }
        }
    }





    // Camera2D
    camera.target   = player->GetPosition();
    camera.offset   = {VIRTUAL_WIDTH / 2.0f, VIRTUAL_HEIGHT / 2.0f};
    camera.rotation = 0.0f;
    camera.zoom     = GAMEPLAY_CAMERA_ZOOM;
}

GameplayScreen::~GameplayScreen() {
    delete player;
    if (player2) delete player2;
    for (auto r : remotePlayers) delete r;
    // healthBarFrames textures are managed by TextureManager
}

Character* GameplayScreen::GetNearestEnemy(Vector2 pos) {
    // In PvP mode, nearest "enemy" is actually a remote player
    Character* nearest = nullptr;
    float minDist = 999999.0f;
    for (auto r : remotePlayers) {
        if (r->IsDead()) continue;
        float dx = r->GetPosition().x - pos.x;
        float dy = r->GetPosition().y - pos.y;
        float dist = dx*dx + dy*dy;
        if (dist < minDist) {
            minDist = dist;
            nearest = r;
        }
    }
    return nearest;
}

// AABB minimum-overlap push-out. Separates `c` from obstacle rectangle `ob`
// along whichever axis has the smallest penetration depth (SAT / MTV).
//
// Platform semantics: when the character's feet are above the top of the
// obstacle (i.e. they walked onto it from above) and the horizontal overlap
// is small, prefer to land on top of the obstacle rather than push out
// sideways. This makes tree trunks / rocks feel like walkable platforms
// while still blocking from the sides and below.
static void PushOut(Character* c, const Rectangle& ob) {
    Rectangle cb = c->GetCollisionBounds();
    if (!CheckCollisionRecs(cb, ob)) return;

    float oL = (cb.x + cb.width)  - ob.x;           // penetration pushing left
    float oR = (ob.x + ob.width)  - cb.x;           // penetration pushing right
    float oT = (cb.y + cb.height) - ob.y;           // penetration pushing up
    float oB = (ob.y + ob.height) - cb.y;           // penetration pushing down

    Vector2 pos = c->GetPosition();

    // Are the character's feet already sitting at/above the top of the
    // obstacle? (i.e. the bottom of cb is at or just above ob.y).
    // In that case the character is "standing on" the obstacle — push up
    // so they land on top, instead of bouncing off horizontally.
    const float PLATFORM_SNAP = 2.0f; // px of tolerance for the snap
    bool standingOnTop = (cb.y + cb.height - ob.y) <= PLATFORM_SNAP
                       && oT < oL && oT < oR
                       && oT <= oB;

    if (standingOnTop) {
        pos.y -= oT;
    } else if (std::min(oL, oR) < std::min(oT, oB)) {
        // Separate on X (less penetration horizontally)
        pos.x += (oL < oR) ? -oL : oR;
    } else {
        // Separate on Y
        pos.y += (oT < oB) ? -oT : oB;
    }
    c->SetPosition(pos);
}

// Forward decl: closest-edge push-out (defined below SATPushOut).
// Used by ResolveWorldCollision when SAT returns a tiny / wrong-axis MTV
// for a small AABB hitting a much larger irregular polygon.
static void PolygonClosestEdgePushOut(Character* c,
                                      const std::vector<Vector2>& polyVerts);

// TMX object positions are authoritative — no extra offset applied.
// (Previously a +128px hack was used to align multi-tile tree trunks, but it
//  shifted every collision object in the map, not just trunks.)
static const float TMX_COLLISION_Y_OFFSET = 0.0f;

// Extract Tiled polygon vertices in world space with optional Y offset.
// Note: raytmx stores OBJECT_TYPE_POLYGON with points[0] = centroid,
// points[1..N] = outer vertices, and points[N+1] = duplicate of points[1].
// We skip index 0 (centroid) and index N+1 (duplicate) to get the exact boundary.
static std::vector<Vector2> GetWorldPolygonVertices(const TmxObject& obj, float offsetY = TMX_COLLISION_Y_OFFSET) {
    std::vector<Vector2> verts;
    if (!obj.points || obj.pointsLength < 3) return verts;

    if (obj.type == OBJECT_TYPE_POLYGON) {
        uint32_t endIdx = (obj.pointsLength > 2) ? (obj.pointsLength - 1) : obj.pointsLength;
        for (uint32_t i = 1; i < endIdx; i++) {
            verts.push_back({
                (float)obj.x + obj.points[i].x,
                (float)obj.y + offsetY + obj.points[i].y
            });
        }
    } else {
        for (uint32_t i = 0; i < obj.pointsLength; i++) {
            verts.push_back({
                (float)obj.x + obj.points[i].x,
                (float)obj.y + offsetY + obj.points[i].y
            });
        }
    }
    return verts;
}



// Project a set of vertices onto an axis; returns the min/max scalar values.
static void ProjectVertices(const std::vector<Vector2>& verts, const Vector2& axis, float& outMin, float& outMax) {
    outMin =  INFINITY;
    outMax = -INFINITY;
    for (const auto& v : verts) {
        float d = v.x * axis.x + v.y * axis.y;
        if (d < outMin) outMin = d;
        if (d > outMax) outMax = d;
    }
}

// Closest point on the segment p1-p2 to point p.
static Vector2 ClosestPointOnSegment(Vector2 p, Vector2 a, Vector2 b) {
    Vector2 ab = { b.x - a.x, b.y - a.y };
    float len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 < 0.0001f) return a;
    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2;
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    return { a.x + ab.x * t, a.y + ab.y * t };
}

// SAT collision between an AABB (rect) and a convex polygon.
// Returns true if overlapping, and fills 'mtv' with the minimum translation
// vector that pushes the rectangle out of the polygon.
static bool SATPushOut(const Rectangle& rect, const std::vector<Vector2>& polyVerts, Vector2& mtv) {
    // Four corners of the rectangle
    Vector2 rectCorners[4] = {
        { rect.x,                 rect.y },
        { rect.x + rect.width,    rect.y },
        { rect.x + rect.width,    rect.y + rect.height },
        { rect.x,                 rect.y + rect.height }
    };

    // Build list of axes to test: the two rect axes + one per polygon edge normal
    std::vector<Vector2> axes;
    axes.reserve(2 + polyVerts.size());

    // Rect axes (world X and Y)
    axes.push_back({ 1.0f, 0.0f });
    axes.push_back({ 0.0f, 1.0f });

    // Polygon edge normals
    for (size_t i = 0; i < polyVerts.size(); i++) {
        size_t j = (i + 1) % polyVerts.size();
        Vector2 edge = { polyVerts[j].x - polyVerts[i].x,
                         polyVerts[j].y - polyVerts[i].y };
        Vector2 n = { -edge.y, edge.x }; // perpendicular
        float len = sqrtf(n.x * n.x + n.y * n.y);
        if (len > 0.0001f) {
            n.x /= len;
            n.y /= len;
            axes.push_back(n);
        }
    }

    float minOverlap = INFINITY;
    Vector2 minAxis   = { 0.0f, 0.0f };

    // Test every axis
    for (const auto& axis : axes) {
        float minR, maxR, minP, maxP;
        ProjectVertices(std::vector<Vector2>(rectCorners, rectCorners + 4), axis, minR, maxR);
        ProjectVertices(polyVerts, axis, minP, maxP);

        if (maxR < minP || maxP < minR) return false; // gap ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ no collision

        float overlap = std::min(maxR, maxP) - std::max(minR, minP);
        if (overlap < minOverlap) {
            minOverlap = overlap;
            minAxis    = axis;
        }
    }

    // Determine direction: the MTV must push the rect away from the polygon.
    // Use the closest point on the polygon perimeter to the rect center.
    Vector2 rectCenter = { rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f };
    Vector2 closestPt = polyVerts[0];
    float minDstSq = INFINITY;
    for (size_t i = 0; i < polyVerts.size(); i++) {
        size_t j = (i + 1) % polyVerts.size();
        Vector2 pt = ClosestPointOnSegment(rectCenter, polyVerts[i], polyVerts[j]);
        float dx = rectCenter.x - pt.x;
        float dy = rectCenter.y - pt.y;
        float dstSq = dx*dx + dy*dy;
        if (dstSq < minDstSq) {
            minDstSq = dstSq;
            closestPt = pt;
        }
    }

    Vector2 dir = { rectCenter.x - closestPt.x, rectCenter.y - closestPt.y };
    if (dir.x * dir.x + dir.y * dir.y < 0.0001f) {
        dir = minAxis;
    }

    if (dir.x * minAxis.x + dir.y * minAxis.y < 0.0f) {
        minAxis.x = -minAxis.x;
        minAxis.y = -minAxis.y;
    }

    mtv.x = minAxis.x * minOverlap;
    mtv.y = minAxis.y * minOverlap;
    return true;
}

void GameplayScreen::ResolveWorldCollision(Character* c) {
    if (!c) return;

    for (int pass = 0; pass < 4; ++pass) {
        Rectangle cb = c->GetCollisionBounds();

        // Process all object groups (object layers) from TMX
        for (const auto& group : allObjectGroups) {
            for (uint32_t i = 0; i < group.objectsLength; i++) {
                const TmxObject& obj = group.objects[i];

                if (obj.type == OBJECT_TYPE_RECTANGLE) {
                    Rectangle objRect;
                    objRect.x = (float)obj.x;
                    objRect.y = (float)obj.y + TMX_COLLISION_Y_OFFSET;
                    objRect.width = (float)obj.width;
                    objRect.height = (float)obj.height;
                    if (CheckCollisionRecs(cb, objRect)) {
                        PushOut(c, objRect);
                        cb = c->GetCollisionBounds(); // refresh after push
                    }
                } else if (obj.type == OBJECT_TYPE_POLYGON || obj.type == OBJECT_TYPE_POLYLINE) {
                    std::vector<Vector2> worldVerts = GetWorldPolygonVertices(obj);
                    if (worldVerts.size() < 3) {
                        // Fallback: use AABB
                        if (CheckCollisionRecs(cb, obj.aabb)) {
                            PushOut(c, obj.aabb);
                            cb = c->GetCollisionBounds();
                        }
                        continue;
                    }

                    Vector2 mtv;
                    if (SATPushOut(cb, worldVerts, mtv)) {
                        Vector2 pos = c->GetPosition();
                        pos.x += mtv.x;
                        pos.y += mtv.y;
                        c->SetPosition(pos);
                        cb = c->GetCollisionBounds(); // refresh after push
                    }
                }
            }
        }
    }
}


void GameplayScreen::ClampCharacterToWorld(Character* c) {
    if (!c) return;

    Vector2 pos = c->GetPosition();
    Rectangle bounds = c->GetCollisionBounds();
    float left = pos.x - bounds.x;
    float top = pos.y - bounds.y;
    float right = (bounds.x + bounds.width) - pos.x;
    float bottom = (bounds.y + bounds.height) - pos.y;

    pos.x = fclamp(pos.x, left, worldWidth - right);
    pos.y = fclamp(pos.y, top, worldHeight - bottom);
    c->SetPosition(pos);
}



static const float HIT_HALF_W   = 22.0f;   // half-width of visible body in world units
static const float HIT_TOP_OFF  =  0.0f;   // body top relative to sprite center
static const float HIT_BOT_OFF  = 60.0f;   // body bottom relative to sprite center (feet below)

// Projectile radius (small circle around the bullet)
static const float PROJ_RADIUS  = 3.0f;

// Returns true if projectile at `pp` hits the AABB for a character whose
// visual center is at `cc` (already adjusted for jumpHeight).
static inline bool ProjectileHitsAABB(Vector2 pp, Vector2 cc) {
    float left   = cc.x - HIT_HALF_W;
    float right  = cc.x + HIT_HALF_W;
    float top    = cc.y + HIT_TOP_OFF;
    float bottom = cc.y + HIT_BOT_OFF;

    // Find closest point on the AABB to the projectile (sweep with small bullet radius)
    float cx = (pp.x < left) ? left : (pp.x > right ? right : pp.x);
    float cy = (pp.y < top)  ? top  : (pp.y > bottom ? bottom : pp.y);

    float dx = pp.x - cx;
    float dy = pp.y - cy;
    return (dx*dx + dy*dy) <= PROJ_RADIUS * PROJ_RADIUS;
}

void GameplayScreen::CheckCollisions() {
    // Player 1 projectiles hitting remote players (online PvP)
    std::vector<std::shared_ptr<Projectile>>& playerProjs = player->GetProjectiles();
    int onlineMode = (int)NetworkManager::GetInstance().currentGameMode;
    int localTeam  = NetworkManager::GetInstance().localTeamID;
    for (int i = 0; i < (int)playerProjs.size(); i++) {
        if (!playerProjs[i]->IsActive()) continue;
        Vector2 projPos = playerProjs[i]->GetPosition();
        for (int j = 0; j < (int)remotePlayers.size(); j++) {
            RemotePlayer* t = remotePlayers[j];
            if (t->IsDead()) continue;
            Vector2 cc = { t->GetPosition().x, t->GetPosition().y - t->GetJumpHeight() };

            // Friendly-fire prevention in TDM: same non-zero teamID = no damage.
            int victimTeam = t->GetTeamID();
            if (onlineMode == (int)OnlineGameMode::TEAM_DEATHMATCH
                && localTeam != 0 && victimTeam != 0 && localTeam == victimTeam) {
                playerProjs[i]->Deactivate();
                break;
            }

            if (ProjectileHitsAABB(projPos, cc)) {
                PacketPlayerDamage dmgPkt;
                dmgPkt.header.type    = PacketType::PLAYER_DAMAGE;
                dmgPkt.header.playerID = NetworkManager::GetInstance().GetLocalPlayerID();
                dmgPkt.targetPlayerID = t->peerID;
                dmgPkt.damageAmount   = playerProjs[i]->GetDamage();
                NetworkManager::GetInstance().SendPacket(&dmgPkt, sizeof(dmgPkt), true);
                t->TakeDamage(playerProjs[i]->GetDamage());
                playerProjs[i]->Deactivate();
                break;
            }
        }
    }

    // --- ONLINE NETWORKING ---
}

bool GameplayScreen::Update(float deltaTime) {
    worldTime += deltaTime;

    if (IsKeyPressed(KEY_ESCAPE)) {
        isPaused = !isPaused;
    }

    if (isPaused) {
        // Handle pause menu UI updates
        Vector2 mouse = Ui::RemapMouseToVirtual();
        Rectangle resumeBtn = { VIRTUAL_WIDTH / 2.0f - 100.0f, VIRTUAL_HEIGHT / 2.0f - 30.0f, 200.0f, 40.0f };
        Rectangle quitBtn   = { VIRTUAL_WIDTH / 2.0f - 100.0f, VIRTUAL_HEIGHT / 2.0f + 30.0f, 200.0f, 40.0f };

        bool hResume = CheckCollisionPointRec(mouse, resumeBtn);
        bool hQuit   = CheckCollisionPointRec(mouse, quitBtn);

        hoverResume = Ui::Approach(hoverResume, hResume ? 1.0f : 0.0f, 8.0f, deltaTime);
        hoverQuit   = Ui::Approach(hoverQuit,   hQuit   ? 1.0f : 0.0f, 8.0f, deltaTime);

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (hResume) isPaused = false;
            else if (hQuit) {
                if (currentMode == GameMode::ONLINE) {
                    NetworkManager::GetInstance().Disconnect();
                }
                return false;
            }
        }
    }

    // F8 toggles the on-screen collision-shape debug overlay
    if (IsKeyPressed(KEY_F8)) {
        Character::SetDebugDrawCollision(!Character::IsDebugDrawCollision());
    }

    // Convert mouse screen coords -> world coords using Camera2D
    Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), camera);

    // Set player aim target in world coordinates
    player->SetAimTarget(mouseWorld);

    // If paused, we still do collision resolution to avoid falling through the floor
    // but skip player input processing so they don't move/shoot.
    if (!isPaused) {
        if (!isSpectating) {
            player->Update(deltaTime);
        }
    }
    
    // Always resolve collision to keep physics stable
    ResolveWorldCollision(player);
    ClampCharacterToWorld(player);


    if (player2) {
        // Simple auto-aim for player 2
        Character* nearestEnemy = GetNearestEnemy(player2->GetPosition());
        if (nearestEnemy) {
            player2->SetAimTarget(nearestEnemy->GetPosition());
        }
        player2->Update(deltaTime);
        ResolveWorldCollision(player2);
        ClampCharacterToWorld(player2);

    }

    // --- ONLINE NETWORKING ---

    // --- ONLINE NETWORKING ---
    if (currentMode == GameMode::ONLINE) {
        // Update remote players' animations
        for (auto r : remotePlayers) {
            r->Update(deltaTime);
        }

        netSendTimer += deltaTime;
        if (netSendTimer >= NET_SEND_RATE) {
            netSendTimer = 0.0f;

            PacketPlayerUpdate pkt;
            pkt.header.type   = PacketType::PLAYER_UPDATE;
            pkt.header.playerID = NetworkManager::GetInstance().GetLocalPlayerID();
            pkt.position      = player->GetPosition();
            pkt.state         = (int)player->GetCurrentState();
            pkt.currentWeaponIndex = player->GetWeaponIndex();
            pkt.faceDirection = player->GetFaceDirection();
            pkt.health        = player->GetHealth();
            pkt.jumpHeight    = player->GetJumpHeight();
            pkt.jumpVelocity  = player->GetJumpVelocity();
            std::strncpy(pkt.username, NetworkManager::GetInstance().localUsername.c_str(), sizeof(pkt.username) - 1);
            pkt.username[sizeof(pkt.username) - 1] = '\0';
            pkt.charSkin      = NetworkManager::GetInstance().localSkinIndex;
            pkt.weaponSkin    = NetworkManager::GetInstance().localWeaponSkin;

            NetworkManager::GetInstance().SendPacket(&pkt, sizeof(pkt), false);
        }

        if (player->DidShoot()) {
            Vector2 aim = player->GetAimTarget();
            Vector2 hand = player->GetHandPosition();
            float dx = aim.x - hand.x;
            float dy = aim.y - hand.y;
            float len = sqrtf(dx*dx + dy*dy);

            PacketPlayerShoot shootPkt;
            shootPkt.header.type   = PacketType::PLAYER_SHOOT;
            shootPkt.header.playerID = NetworkManager::GetInstance().GetLocalPlayerID();
            shootPkt.aimDir        = (len > 1.0f) ? Vector2{dx/len, dy/len} : Vector2{1.0f, 0.0f};
            NetworkManager::GetInstance().SendPacket(&shootPkt, sizeof(shootPkt), true);
        }

        // Online respawn is still controlled by the existing network flow.
        if (player->IsDead() && IsKeyPressed(KEY_R)) {
            int onlineMode2 = (int)NetworkManager::GetInstance().currentGameMode;
            int tid2 = (onlineMode2 == (int)OnlineGameMode::FREE_FOR_ALL) ? 0
                                                              : NetworkManager::GetInstance().localTeamID;
            player->ResetHealth(100.0f);
            Vector2 spawnPos = (tid2 == 0)
                ? Vector2{ worldWidth / 2.0f + GetRandomValue(-200, 200),
                           worldHeight / 2.0f + GetRandomValue(-200, 200) }
                : GetTeamSpawnPoint(tid2);
            player->SetPosition(spawnPos);

            PacketPlayerRespawn resPkt;
            resPkt.header.type = PacketType::PLAYER_RESPAWN;
            resPkt.header.playerID = NetworkManager::GetInstance().GetLocalPlayerID();
            resPkt.spawnPosition = spawnPos;
            resPkt.charSkin = NetworkManager::GetInstance().localSkinIndex;
            resPkt.weaponSkin = NetworkManager::GetInstance().localWeaponSkin;
            NetworkManager::GetInstance().SendPacket(&resPkt, sizeof(resPkt), true);
        }

        // Poll incoming events (positions/shoots from others)
        PollNetworkEvents(deltaTime);
    }

    CheckCollisions();

    // ---- Elimination round flow (host-only) ----
    if (currentMode == GameMode::ONLINE
        && NetworkManager::GetInstance().currentGameMode == OnlineGameMode::ELIMINATION
        && NetworkManager::GetInstance().IsHost()) {

        if (roundInProgress) {
            int aliveGR = CountAliveOnTeam(1);
            int aliveBL = CountAliveOnTeam(2);
            if (aliveGR == 0 || aliveBL == 0) {
                int winner = (aliveGR > 0) ? 1 : (aliveBL > 0) ? 2 : 0;
                if (winner == 1)      NetworkManager::GetInstance().teamScores[1]++;
                else if (winner == 2) NetworkManager::GetInstance().teamScores[2]++;
                BroadcastRoundEnd(winner,
                                  NetworkManager::GetInstance().teamScores[1],
                                  NetworkManager::GetInstance().teamScores[2]);
            }
        } else if (roundBannerTimer <= 0.0f) {
            // Banner expired -> start next round or end match.
            if (NetworkManager::GetInstance().teamScores[1] >= NetworkManager::GetInstance().roundLimit
                || NetworkManager::GetInstance().teamScores[2] >= NetworkManager::GetInstance().roundLimit) {
                NetworkManager::GetInstance().roundPhase = NetworkManager::RoundPhase::MATCH_OVER;
                matchEndTimer = 5.0f;
            } else {
                BroadcastRoundStart(currentRoundNumber + 1);
            }
        }
    }

    // Tick banner / match-end timers.
    if (roundBannerTimer > 0.0f) roundBannerTimer -= deltaTime;
    if (matchEndTimer > 0.0f) {
        matchEndTimer -= deltaTime;
        if (matchEndTimer <= 0.0f
            && NetworkManager::GetInstance().IsHost()
            && NetworkManager::GetInstance().currentGameMode == OnlineGameMode::ELIMINATION) {
            // Pop back to lobby on match end.
            NetworkManager::GetInstance().roundPhase = NetworkManager::RoundPhase::IDLE;
            NetworkManager::GetInstance().ResetMatch();
            return false;
        }
    }

    Vector2 playerPos = player->GetPosition();
    camera.target.x += (playerPos.x - camera.target.x) * 5.0f * deltaTime;
    camera.target.y += (playerPos.y - camera.target.y) * 5.0f * deltaTime;

    camera.zoom = GAMEPLAY_CAMERA_ZOOM;
    float halfVW = VIRTUAL_WIDTH  / (2.0f * camera.zoom);
    float halfVH = VIRTUAL_HEIGHT / (2.0f * camera.zoom);
    if (worldWidth <= halfVW * 2.0f) camera.target.x = worldWidth * 0.5f;
    else camera.target.x = fclamp(camera.target.x, halfVW,  worldWidth  - halfVW);
    if (worldHeight <= halfVH * 2.0f) camera.target.y = worldHeight * 0.5f;
    else camera.target.y = fclamp(camera.target.y, halfVH,  worldHeight - halfVH);

    return true;
}




void GameplayScreen::Draw(RenderTexture2D target) {


    BeginTextureMode(target);
    // Sky: vertical gradient from deep space blue at top to dark midnight at bottom
    ClearBackground({15, 15, 35, 255});
    // Draw sky gradient strips (above world, in screen space before Mode2D)
    // We'll do this in world space during BeginMode2D

    BeginMode2D(camera);

    if (currentTmxMap) {
        float viewW = VIRTUAL_WIDTH / camera.zoom;
        float viewH = VIRTUAL_HEIGHT / camera.zoom;
        Rectangle viewport = { camera.target.x - viewW * 0.5f,
                               camera.target.y - viewH * 0.5f,
                               viewW,
                               viewH };

        // -----------------------------------------------------------------
        // UNIVERSAL DEPTH-SORT RENDERING
        //
        // The first visible tile layer (TMX layer 0) = ground, always drawn
        // first. Every subsequent tile layer is depth-sorted per tile-row
        // alongside characters using painter's algorithm:
        //
        //   depthY = (tileRow + 1) * tileHeight   (world-Y of tile bottom)
        //
        // Works for any map ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â trees, rocks, buildings ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â no naming convention
        // or Tiled properties required.
        // -----------------------------------------------------------------

        // --- Step 1: draw the ground layer (first visible tile layer) ---
        const TmxLayer* groundLayer = nullptr;
        for (uint32_t i = 0; i < currentTmxMap->layersLength; i++) {
            const TmxLayer& layer = currentTmxMap->layers[i];
            if (!layer.visible) continue;
            if (layer.type == LAYER_TYPE_OBJECT_GROUP) continue;
            if (layer.type == LAYER_TYPE_TILE_LAYER) {
                groundLayer = &layer;
                break;
            }
        }
        if (groundLayer) {
            DrawTMXLayers(currentTmxMap, &camera, &viewport, groundLayer, 1, 0, 0, WHITE);
        }

        // --- Step 2: collect depth-sorted renderables (characters + tiles) ---
        std::vector<RenderItem> renderables;


        // Characters
        {
            RenderItem it;
            it.depthY = player->GetDepthY();
            it.draw   = [this]() { player->Draw(); };
            renderables.push_back(it);
        }
        if (player2) {
            RenderItem it;
            it.depthY = player2->GetDepthY();
            it.draw   = [this]() { if (player2) player2->Draw(); };
            renderables.push_back(it);
        }
        for (auto* rp : remotePlayers) {
            RenderItem it;
            it.depthY = rp->GetDepthY();
            it.draw   = [rp]() { rp->Draw(); };
            renderables.push_back(it);
        }

        // Foreground tile layers: every tile layer after the first (ground)
        bool pastGround = false;
        for (uint32_t li = 0; li < currentTmxMap->layersLength; li++) {
            const TmxLayer& layer = currentTmxMap->layers[li];
            if (!layer.visible) continue;
            if (layer.type == LAYER_TYPE_OBJECT_GROUP) continue;
            if (layer.type != LAYER_TYPE_TILE_LAYER) continue;

            if (!pastGround) {
                pastGround = true; // this is the ground layer ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â already drawn
                continue;
            }

            // -----------------------------------------------------------------
            // COLUMN-BASED DEPTH SORT
            //
            // Problem with per-row depthY: the shadow tiles that spread BELOW
            // the trunk have a higher depthY than the trunk, so they draw AFTER
            // the player even when the player is standing south of the trunk
            // (where they should be in front of the whole tree).
            //
            // Fix: for every column, find the TOPMOST non-empty tile (= the
            // "trunk base"). All tiles in that column share the same depthY =
            // bottom of that topmost tile row. This makes the entire tree
            // (trunk + shadow) sort as one object at the trunk's depth.
            //
            // Character south of trunk.bottomY Ã¢â€ â€™ in front of tree Ã¢Å“â€œ
            // Character north of trunk.bottomY Ã¢â€ â€™ behind tree Ã¢Å“â€œ
            // Shadow never independently occludes the character Ã¢Å“â€œ
            // -----------------------------------------------------------------

            // (Removed pass 1 precomputation, using dynamic lookdown instead)
            const TmxTileLayer& tl = layer.exact.tileLayer;
            const uint32_t mapW    = currentTmxMap->width;
            const uint32_t mapH    = currentTmxMap->height;
            const float    tileW   = (float)currentTmxMap->tileWidth;
            const float    tileH   = (float)currentTmxMap->tileHeight;

            // Pass 2: add visible tiles as RenderItems using column-based depthY.
            for (uint32_t ty = 0; ty < mapH; ty++) {
                float rowWorldY = (float)ty * tileH;
                if (rowWorldY + tileH + 500.0f < viewport.y) continue;
                if (rowWorldY - 500.0f > viewport.y + viewport.height) break;

                for (uint32_t tx = 0; tx < mapW; tx++) {
                    float colWorldX = (float)tx * tileW;
                    if (colWorldX + tileW + 500.0f < viewport.x) continue;
                    if (colWorldX - 500.0f > viewport.x + viewport.width) break;

                    uint32_t gid = tl.tiles[ty * mapW + tx];
                    if (gid == 0) continue;
                    if (gid >= currentTmxMap->gidsToTilesLength) continue;

                    const TmxTile& tile = currentTmxMap->gidsToTiles[gid];
                    if (tile.gid == 0) continue;

                    int top = (int)ty;
                    for (int sty = (int)ty; sty >= 0; sty--) {
                        if (tl.tiles[sty * mapW + tx] != 0) {
                            top = sty;
                        } else {
                            break;
                        }
                    }

                    // Also find the bottommost tile in this column block
                    int bottom = (int)ty;
                    for (int sty = (int)ty; sty < (int)mapH; sty++) {
                        if (tl.tiles[sty * mapW + tx] != 0) {
                            bottom = sty;
                        } else {
                            break;
                        }
                    }

                    // Depth threshold — adjust the fraction (0.0=top, 1.0=bottom) to control
                    // how far down the tree the player must be before going BEHIND it.
                    float colDepthY = ((float)top + (float)(bottom - top) * 0.75f + 1.0f) * tileH;

                    float dw = (tile.sourceRect.width > 0.0f) ? tile.sourceRect.width : (float)currentTmxMap->tileWidth;
                    float dh = (tile.sourceRect.height > 0.0f) ? tile.sourceRect.height : (float)currentTmxMap->tileHeight;
                    float wx = colWorldX + tile.offset.x;
                    float wy = rowWorldY  + tile.offset.y + (tileH - dh);
                    Rectangle src = tile.sourceRect;
                    Texture2D tex  = tile.texture;


                    RenderItem it;
                    it.depthY = colDepthY;
                    it.draw   = [tex, src, wx, wy, dw, dh]() {
                        Rectangle dst = { wx, wy, dw, dh };
                        DrawTexturePro(tex, src, dst, {0, 0}, 0.0f, WHITE);
                    };
                    renderables.push_back(it);
                }
            }
        }

        // --- Step 3: sort all renderables by depthY and draw ---
        std::sort(renderables.begin(), renderables.end(),
                  [](const RenderItem& a, const RenderItem& b) {
                      return a.depthY < b.depthY;
                  });
        for (auto& it : renderables) it.draw();

    } else {
        // No TMX map Ã¢â‚¬â€ legacy texture-based background
        for (int strip = 0; strip < (int)worldHeight; strip += 80) {
            float t = (float)strip / worldHeight;
            unsigned char r = (unsigned char)(15 + t * 10);
            unsigned char g = (unsigned char)(15 + t * 8);
            unsigned char b = (unsigned char)(35 + t * 15);
            DrawRectangle(0, strip, (int)worldWidth, 80, {r, g, b, 255});
        }
        if (bgTex1.id != 0 && bgTex1.width > 0) {
            int cols = (int)(worldWidth / bgTex1.width) + 2;
            int rows = (int)(worldHeight / bgTex1.height) + 2;
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < cols; ++x)
                    DrawTexture(bgTex1, x * bgTex1.width, y * bgTex1.height, WHITE);
        }
        // Depth-sort characters (no tile objects in fallback mode)
        std::vector<RenderItem> renderables;
        {
            RenderItem it; it.depthY = player->GetDepthY();
            it.draw = [this]() { player->Draw(); }; renderables.push_back(it);
        }
        if (player2) {
            RenderItem it; it.depthY = player2->GetDepthY();
            it.draw = [this]() { if (player2) player2->Draw(); }; renderables.push_back(it);
        }
        for (auto* rp : remotePlayers) {
            RenderItem it; it.depthY = rp->GetDepthY();
            it.draw = [rp]() { rp->Draw(); }; renderables.push_back(it);
        }
        std::sort(renderables.begin(), renderables.end(),
            [](const RenderItem& a, const RenderItem& b){ return a.depthY < b.depthY; });
        for (auto& it : renderables) it.draw();
    } // end if(currentTmxMap)/else

    // Draw Character UIs (health bars, names) on top of all depth-sorted world objects
    player->DrawUI();
    if (player2) player2->DrawUI();
    for (auto* rp : remotePlayers) rp->DrawUI();

    // ---- Voice speaking indicator (green dot above head) ----
    auto drawSpeakDot = [](Vector2 worldPos, float jumpOffset, bool speaking) {
        if (!speaking) return;
        Vector2 p = worldPos;
        p.y -= jumpOffset;
        DrawCircleV(p, 7.0f, GREEN);
    };

    // Local player
    drawSpeakDot(player->GetPosition(), player->GetJumpHeight() + 10.0f, proximityVoiceChat.isLocalSpeaking());

    // Draw TMX collision objects when F8 debug mode is active
    if (Character::IsDebugDrawCollision()) {
        for (const auto& group : allObjectGroups) {
            for (uint32_t i = 0; i < group.objectsLength; i++) {
                const TmxObject& obj = group.objects[i];
                if (obj.type == OBJECT_TYPE_RECTANGLE) {
                    Rectangle r = { (float)obj.x, (float)obj.y + TMX_COLLISION_Y_OFFSET, (float)obj.width, (float)obj.height };
                    DrawRectangleLinesEx(r, 2.0f, GREEN);
                } else if (obj.type == OBJECT_TYPE_POLYGON || obj.type == OBJECT_TYPE_POLYLINE) {
                    std::vector<Vector2> verts = GetWorldPolygonVertices(obj);
                    for (size_t j = 0; j < verts.size(); j++) {
                        Vector2 p1 = verts[j];
                        Vector2 p2 = verts[(j + 1) % verts.size()];
                        DrawLineEx(p1, p2, 2.0f, GREEN);
                    }
                }
            }
        }
    }

    EndMode2D();




    // HUD (drawn in screen space, not world space)

    // HUD: Animated Health bar (9-frame sprite animation) and Ammo
    const int barWidth = 500;
    const int barHeight = 100;
    int startX = 13;
    int startY = 0;

    // No clipping/shaping ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â draw it as-is so you can position it freely.
    if (headPortrait.id != 0 && headPortrait.width > 0) {
        int headSize = 85;                            // base size; tweak as needed
        int headX    = 30;                                // default x; adjust below
        int headY    = 5;                                // default y; adjust below
        Rectangle src = { 0.0f, 0.0f, (float)headPortrait.width, (float)headPortrait.height };
        Rectangle dst = { (float)headX, (float)headY, (float)headSize, (float)headSize };
        DrawTexturePro(headPortrait, src, dst, {0.0f, 0.0f}, 0.0f, WHITE);
    }

    // Pick frame based on health percentage (0.0 - 1.0)
    // Frame 1 (index 0) = full health, Frame 9 (index 8) = lowest health (reversed)
    {
        float healthPct = player->GetHealth() / 100.0f;
        if (healthPct < 0.0f) healthPct = 0.0f;
        if (healthPct > 1.0f) healthPct = 1.0f;
        int frameIndex = (int)((1.0f - healthPct) * 8.0f + 0.5f); // 0..8 (reversed: full HP = frame 1, low HP = frame 9)
        if (frameIndex < 0) frameIndex = 0;
        if (frameIndex > 8) frameIndex = 8;

        Texture2D& tex = healthBarFrames[frameIndex];
        if (tex.id != 0) {
            // Auto-detect the non-transparent content bounds (each frame may have different padding)
            // Lazy-init per frame, cache the source rect
            static Rectangle contentSrc[9] = {0};
            static bool contentSrcReady[9] = {false};
            int fi = frameIndex;
            if (!contentSrcReady[fi]) {
                // Load the image, find alpha bounds, then upload as GPU texture so we can free the CPU image
                Image img = LoadImage(
                    ("assets/HealthBar/" + std::to_string(fi + 1) + "_no_bg.png").c_str());
                if (img.data != nullptr) {
                    int minX = img.width, minY = img.height, maxX = -1, maxY = -1;
                    Color* px = LoadImageColors(img);
                    int n = img.width * img.height;
                    for (int k = 0; k < n; k++) {
                        if (px[k].a > 8) {
                            int x = k % img.width;
                            int y = k / img.width;
                            if (x < minX) minX = x;
                            if (y < minY) minY = y;
                            if (x > maxX) maxX = x;
                            if (y > maxY) maxY = y;
                        }
                    }
                    UnloadImageColors(px);
                    if (maxX >= 0) {
                        contentSrc[fi] = {
                            (float)minX, (float)minY,
                            (float)(maxX - minX + 1), (float)(maxY - minY + 1)
                        };
                    } else {
                        contentSrc[fi] = { 0, 0, (float)img.width, (float)img.height };
                    }
                    // Keep the original GPU texture handle; just cache the source rect for DrawTexturePro.
                    // This avoids runtime artifacts caused by UnloadTexture/LoadTexture during gameplay.
                    UnloadImage(img);
                    contentSrcReady[fi] = true;
                }
            }
            // Use the cached source rect (cropped bounds)
            Rectangle src = {
                contentSrc[fi].x,
                contentSrc[fi].y,
                contentSrc[fi].width,
                contentSrc[fi].height
            };
            Rectangle dst = { (float)startX, (float)startY, (float)barWidth, (float)barHeight };
            DrawTexturePro(tex, src, dst, {0.0f, 0.0f}, 0.0f, WHITE);
        }
    }

    // ---- Top-center team score banner (TDM / Elimination) ----
    if (currentMode == GameMode::ONLINE
        && (NetworkManager::GetInstance().currentGameMode == OnlineGameMode::TEAM_DEATHMATCH
         || NetworkManager::GetInstance().currentGameMode == OnlineGameMode::ELIMINATION)) {
        Font bannerFont = GetFontDefault();
        int grScore = NetworkManager::GetInstance().teamScores[1];
        int blScore = NetworkManager::GetInstance().teamScores[2];
        std::string banner = "GR  " + std::to_string(grScore)
                            + "      :      " + std::to_string(blScore) + "  BL";
        Vector2 bsz = MeasureTextEx(bannerFont, banner.c_str(), 22.0f, 1.0f);
        float bx = VIRTUAL_WIDTH * 0.5f - bsz.x * 0.5f;
        float by = 8.0f;
        Rectangle bg = { bx - 14.0f, by - 4.0f, bsz.x + 28.0f, bsz.y + 10.0f };
        DrawRectangleRec(bg, Fade(BLACK, 0.65f));
        DrawRectangleLinesEx(bg, 1.5f, Fade(UiTheme::PanelBorder(), 0.6f));
        std::string leftStr  = "GR  "  + std::to_string(grScore);
        std::string rightStr = std::to_string(blScore) + "  BL";
        Vector2 lsz = MeasureTextEx(bannerFont, leftStr.c_str(), 22.0f, 1.0f);
        Vector2 rsz = MeasureTextEx(bannerFont, rightStr.c_str(), 22.0f, 1.0f);
        DrawTextEx(bannerFont, leftStr.c_str(),  { bx,                          by + 2.0f }, 22.0f, 1.0f, UiTheme::TeamBlue());
        DrawTextEx(bannerFont, ":",             { bx + lsz.x + 6.0f,          by + 2.0f }, 22.0f, 1.0f, RAYWHITE);
        DrawTextEx(bannerFont, rightStr.c_str(),{ bx + bsz.x - rsz.x,         by + 2.0f }, 22.0f, 1.0f, UiTheme::TeamRed());
    }

    // ---- Dash bar (6-frame animation) ----
    // dash1.png = FULL/READY, dash6.png = EMPTY (just dashed)
    // During the active dash (dashTimer > 0): animate forward dash1 -> dash2 -> ... -> dash6.
    // During recharge (dashTimer == 0, dashCooldown > 0): animate backward dash6 -> dash5 -> ... -> dash1.
    // When ready (both 0): show dash1.png.
    {
        float dashTimer    = player->GetDashTimer();
        float dashCooldown = player->GetDashCooldown();
        const float DASH_ACTIVE_DURATION = 0.15f; // matches Character::ActivateDash
        const float DASH_MAX_COOLDOWN    = 2.0f;  // matches Character::ActivateDash
        int frameIndex; // 0..5  ->  dash1.png..dash6.png

        if (dashTimer > 0.0f) {
            // Active dash: animate forward dash1 (index 0) -> dash6 (index 5)
            // dashTimer goes 0.15 -> 0, so 1 - timer/duration goes 0 -> 1
            float activePct = 1.0f - (dashTimer / DASH_ACTIVE_DURATION);
            if (activePct < 0.0f) activePct = 0.0f;
            if (activePct > 1.0f) activePct = 1.0f;
            frameIndex = (int)(activePct * 5.0f + 0.5f); // 0 (dash1) .. 5 (dash6)
        } else if (dashCooldown > 0.0f) {
            // Recharge: animate backward dash6 (index 5) -> dash1 (index 0)
            // dashCooldown goes 2.0 -> 0, so cooldown/duration goes 1 -> 0
            float rechargePct = dashCooldown / DASH_MAX_COOLDOWN;
            if (rechargePct > 1.0f) rechargePct = 1.0f;
            if (rechargePct < 0.0f) rechargePct = 0.0f;
            frameIndex = (int)(rechargePct * 5.0f + 0.5f); // 5 (dash6) .. 0 (dash1)
        } else {
            // Ready -> show full frame (dash1)
            frameIndex = 0;
        }

        if (frameIndex < 0) frameIndex = 0;
        if (frameIndex > 5) frameIndex = 5;

        Texture2D& dtex = dashBarFrames[frameIndex];
        if (dtex.id != 0) {
            // Lazy-init per frame, cache the source rect (mirrors health bar logic)
            static Rectangle dashContentSrc[6] = {0};
            static bool dashContentSrcReady[6] = {false};
            int fi = frameIndex;
            if (!dashContentSrcReady[fi]) {
                Image img = LoadImage(
                    ("assets/DashBar/dash" + std::to_string(fi + 1) + ".png").c_str());
                if (img.data != nullptr) {
                    int minX = img.width, minY = img.height, maxX = -1, maxY = -1;
                    Color* px = LoadImageColors(img);
                    int n = img.width * img.height;
                    for (int k = 0; k < n; k++) {
                        if (px[k].a > 8) {
                            int x = k % img.width;
                            int y = k / img.width;
                            if (x < minX) minX = x;
                            if (y < minY) minY = y;
                            if (x > maxX) maxX = x;
                            if (y > maxY) maxY = y;
                        }
                    }
                    UnloadImageColors(px);
                    if (maxX >= 0) {
                        dashContentSrc[fi] = {
                            (float)minX, (float)minY,
                            (float)(maxX - minX + 1), (float)(maxY - minY + 1)
                        };
                    } else {
                        dashContentSrc[fi] = { 0, 0, (float)img.width, (float)img.height };
                    }
                    // Keep the original GPU texture handle; just cache the source rect for DrawTexturePro.
                    // This avoids runtime artifacts caused by UnloadTexture/LoadTexture during gameplay.
                    UnloadImage(img);
                    dashContentSrcReady[fi] = true;
                }
            }
            // Dash bar sits directly below the health bar (health bar is 100px tall at y=0)
            int dashStartX = 122;
            int dashStartY = 63 + 5; // 5px gap below the health bar
            const int dashBarWidth  = 320;
            const int dashBarHeight = 10;  // slimmer than the health bar so it reads as a secondary indicator
            // Use cached cropped bounds
            Rectangle dSrc = {
                dashContentSrc[fi].x,
                dashContentSrc[fi].y,
                dashContentSrc[fi].width,
                dashContentSrc[fi].height
            };
            Rectangle dDst = { (float)dashStartX, (float)dashStartY,
                               (float)dashBarWidth, (float)dashBarHeight };
            DrawTexturePro(dtex, dSrc, dDst, {0.0f, 0.0f}, 0.0f, WHITE);
        }
    }

    if (player->IsDead() && (!player2 || player2->IsDead())) {
        DrawText("YOU'RE DEAD", VIRTUAL_WIDTH/2 - 120, VIRTUAL_HEIGHT/2 - 20, 48, RED);
        DrawText("Press R to Respawn", VIRTUAL_WIDTH/2 - 90, VIRTUAL_HEIGHT/2 + 40, 20, RAYWHITE);
    }

    // ---- Spectator banner (Elimination only) ----
    if (isSpectating && NetworkManager::GetInstance().currentGameMode == OnlineGameMode::ELIMINATION) {
        Font specFont = GetFontDefault();
        const char* spec = "SPECTATING - WAITING FOR NEXT ROUND";
        Vector2 sz = MeasureTextEx(specFont, spec, 18.0f, 1.0f);
        DrawTextEx(specFont, spec,
                   { VIRTUAL_WIDTH * 0.5f - sz.x * 0.5f, VIRTUAL_HEIGHT - 60.0f },
                   18.0f, 1.0f, UiTheme::TextMuted());
    }

    // ---- Round-over banner ----
    if (roundBannerTimer > 0.0f
        && NetworkManager::GetInstance().currentGameMode == OnlineGameMode::ELIMINATION) {
        Font bannerFont = GetFontDefault();
        const char* winnerLabel =
            (lastRoundWinnerID == 1) ? "GLOBAL RISK WINS THE ROUND"
            : (lastRoundWinnerID == 2) ? "BLACK LIST WINS THE ROUND"
            :                            "ROUND DRAW";
        Color winColor = (lastRoundWinnerID == 1) ? UiTheme::TeamBlue()
                        : (lastRoundWinnerID == 2) ? UiTheme::TeamRed()
                                                   : RAYWHITE;
        Rectangle bar = { 80, VIRTUAL_HEIGHT * 0.5f - 40,
                          (float)(VIRTUAL_WIDTH - 160), 80 };
        DrawRectangleRec(bar, Fade(BLACK, 0.85f));
        DrawRectangleLinesEx(bar, 2.5f, Fade(winColor, 0.9f));
        Vector2 sz = MeasureTextEx(bannerFont, winnerLabel, 28.0f, 1.0f);
        DrawTextEx(bannerFont, winnerLabel,
                   { bar.x + bar.width * 0.5f - sz.x * 0.5f,
                     bar.y + 12.0f },
                   28.0f, 1.0f, winColor);
        std::string scoreStr = "Score  GR " +
            std::to_string(NetworkManager::GetInstance().teamScores[1]) +
            "  :  " +
            std::to_string(NetworkManager::GetInstance().teamScores[2]) + "  BL";
        Vector2 ssz = MeasureTextEx(bannerFont, scoreStr.c_str(), 14.0f, 1.0f);
        DrawTextEx(bannerFont, scoreStr.c_str(),
                   { bar.x + bar.width * 0.5f - ssz.x * 0.5f,
                     bar.y + 50.0f },
                   14.0f, 1.0f, RAYWHITE);
    }

    // ---- Match-end banner ----
    if (matchEndTimer > 0.0f) {
        Font matchFont = GetFontDefault();
        const char* matchLabel = "MATCH OVER";
        Rectangle bar = { 80, VIRTUAL_HEIGHT * 0.5f - 60,
                          (float)(VIRTUAL_WIDTH - 160), 120 };
        DrawRectangleRec(bar, Fade(BLACK, 0.90f));
        DrawRectangleLinesEx(bar, 3.0f, UiTheme::AccentGold());
        Vector2 sz = MeasureTextEx(matchFont, matchLabel, 36.0f, 1.0f);
        DrawTextEx(matchFont, matchLabel,
                   { bar.x + bar.width * 0.5f - sz.x * 0.5f,
                     bar.y + 18.0f },
                   36.0f, 1.0f, UiTheme::AccentGold());
        int finalGR = NetworkManager::GetInstance().teamScores[1];
        int finalBL = NetworkManager::GetInstance().teamScores[2];
        std::string finalStr = "Final  GR " + std::to_string(finalGR)
                              + "  :  " + std::to_string(finalBL) + "  BL";
        Vector2 fsz = MeasureTextEx(matchFont, finalStr.c_str(), 18.0f, 1.0f);
        DrawTextEx(matchFont, finalStr.c_str(),
                   { bar.x + bar.width * 0.5f - fsz.x * 0.5f,
                     bar.y + 70.0f },
                   18.0f, 1.0f, RAYWHITE);
        std::string hint = "Returning to lobby...";
        Vector2 hsz = MeasureTextEx(matchFont, hint.c_str(), 12.0f, 1.0f);
        DrawTextEx(matchFont, hint.c_str(),
                   { bar.x + bar.width * 0.5f - hsz.x * 0.5f,
                     bar.y + bar.height - 22.0f },
                   12.0f, 1.0f, UiTheme::TextMuted());
    }

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Scoreboard (TAB) ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    if (IsKeyDown(KEY_TAB)) {
        Font sbFont = GetFontDefault();

        // Collect entries
        struct SBEntry { std::string name; int kills; int deaths; bool isLocal; bool isMonster; };
        std::vector<SBEntry> board;

        if (currentMode == GameMode::ONLINE) {
            board.push_back({
                NetworkManager::GetInstance().localUsername.empty() ? "You" : NetworkManager::GetInstance().localUsername,
                NetworkManager::GetInstance().localKills,
                NetworkManager::GetInstance().localDeaths,
                true, false
            });
            for (auto rp : remotePlayers) {
                board.push_back({ rp->username.empty() ? "Player" : rp->username, rp->GetKills(), rp->GetDeaths(), false, false });
            }
        } else {
            board.push_back({ player->GetName().empty() ? "You" : player->GetName(), player->GetKills(), player->GetDeaths(), true, false });
            if (player2) {
                board.push_back({ player2->GetName().empty() ? "Bot" : player2->GetName(), player2->GetKills(), player2->GetDeaths(), false, player2->IsMonster() });
            }
        }

        // Sort by kills descending
        std::sort(board.begin(), board.end(), [](const SBEntry& a, const SBEntry& b) {
            return a.kills > b.kills;
        });

        // Layout
        const int rowH    = 32;
        const int headerH = 54;
        const int footerH = 16;
        int panelW = 560;
        int panelH = headerH + (int)board.size() * rowH + footerH + 16;
        int panelX = VIRTUAL_WIDTH  / 2 - panelW / 2;
        int panelY = VIRTUAL_HEIGHT / 2 - panelH / 2;

        // Outer glass panel
        DrawRectangle(panelX, panelY, panelW, panelH, { 8, 10, 22, 230 });
        DrawRectangleLines(panelX, panelY, panelW, panelH, { 200, 160, 60, 160 });
        DrawRectangleLines(panelX + 1, panelY + 1, panelW - 2, panelH - 2, { 200, 160, 60, 60 });

        // Gold accent top bar
        DrawRectangle(panelX, panelY, panelW, 3, { 200, 160, 60, 255 });

        // Title
        const char* title = (currentMode == GameMode::ONLINE) ? "SCOREBOARD" : "LEADERBOARD";
        Vector2 titleSz = MeasureTextEx(sbFont, title, 22.0f, 1.5f);
        DrawTextEx(sbFont, title,
            { panelX + panelW * 0.5f - titleSz.x * 0.5f, (float)panelY + 10.0f },
            22.0f, 1.5f, { 220, 180, 60, 255 });

        // Column headers
        int hy = panelY + headerH - 18;
        DrawTextEx(sbFont, "PLAYER",  { (float)panelX + 60,           (float)hy }, 11.0f, 1.0f, { 120, 140, 180, 200 });
        DrawTextEx(sbFont, "KILLS",   { (float)panelX + panelW - 160, (float)hy }, 11.0f, 1.0f, { 120, 140, 180, 200 });
        DrawTextEx(sbFont, "DEATHS",  { (float)panelX + panelW -  72, (float)hy }, 11.0f, 1.0f, { 120, 140, 180, 200 });
        DrawLine(panelX + 10, panelY + headerH - 2, panelX + panelW - 10, panelY + headerH - 2, { 200, 160, 60, 80 });

        // Rows
        for (int i = 0; i < (int)board.size(); i++) {
            int ry = panelY + headerH + i * rowH;

            // Row highlight for local player
            if (board[i].isLocal) {
                DrawRectangle(panelX + 4, ry + 2, panelW - 8, rowH - 4, { 40, 60, 30, 80 });
            } else if (i % 2 == 0) {
                DrawRectangle(panelX + 4, ry + 2, panelW - 8, rowH - 4, { 255, 255, 255, 8 });
            }

            // Rank medal
            const char* rank = (i == 0) ? "#1" : (i == 1) ? "#2" : (i == 2) ? "#3" : TextFormat("#%d", i + 1);
            Color rankColor = (i == 0) ? Color{ 255, 200, 50, 255 } :
                              (i == 1) ? Color{ 180, 200, 220, 255 } :
                              (i == 2) ? Color{ 200, 130, 80, 255 } :
                                         Color{ 120, 130, 150, 200 };
            DrawTextEx(sbFont, rank, { (float)panelX + 12, (float)ry + 8 }, 14.0f, 1.0f, rankColor);

            // Name + tag
            Color nameColor = board[i].isLocal    ? Color{ 100, 230, 100, 255 } :
                              board[i].isMonster  ? Color{ 255, 100,  80, 255 } :
                                                   Color{ 210, 215, 230, 240 };
            const char* tag = board[i].isLocal   ? " [YOU]"    :
                              board[i].isMonster ? " [MONSTER]" : " [BOT]";
            std::string displayName = board[i].name;
            DrawTextEx(sbFont, displayName.c_str(), { (float)panelX + 60, (float)ry + 8 }, 14.0f, 1.0f, nameColor);

            // Kills
            std::string killStr = std::to_string(board[i].kills);
            Vector2 ksz = MeasureTextEx(sbFont, killStr.c_str(), 14.0f, 1.0f);
            DrawTextEx(sbFont, killStr.c_str(),
                { (float)panelX + panelW - 160 + 20 - ksz.x * 0.5f, (float)ry + 8 },
                14.0f, 1.0f, { 100, 220, 120, 255 });

            // Deaths
            std::string deathStr = std::to_string(board[i].deaths);
            Vector2 dsz = MeasureTextEx(sbFont, deathStr.c_str(), 14.0f, 1.0f);
            DrawTextEx(sbFont, deathStr.c_str(),
                { (float)panelX + panelW - 72 + 20 - dsz.x * 0.5f, (float)ry + 8 },
                14.0f, 1.0f, { 220, 100, 100, 255 });

            // Row separator
            if (i < (int)board.size() - 1)
                DrawLine(panelX + 10, ry + rowH, panelX + panelW - 10, ry + rowH, { 255, 255, 255, 18 });
        }

        // Footer hint
        const char* hint = "Hold [TAB] to view";
        Vector2 hsz = MeasureTextEx(sbFont, hint, 10.0f, 1.0f);
        DrawTextEx(sbFont, hint,
            { panelX + panelW * 0.5f - hsz.x * 0.5f,
              (float)(panelY + headerH + (int)board.size() * rowH + 6) },
            10.0f, 1.0f, { 120, 130, 150, 160 });
    }

    // Draw Match Timer if Time Goal is active
    if (NetworkManager::GetInstance().timeLimit > 0) {
        int timeLimit = NetworkManager::GetInstance().timeLimit;
        int timeRemaining = timeLimit - (int)worldTime;
        if (timeRemaining < 0) timeRemaining = 0;
        
        int m = timeRemaining / 60;
        int s = timeRemaining % 60;
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", m, s);
        
        Font timeFont = GetFontDefault();
        Vector2 tsz = MeasureTextEx(timeFont, timeStr, 24.0f, 1.0f);
        
        Rectangle tBg = { (float)VIRTUAL_WIDTH / 2.0f - tsz.x / 2.0f - 16.0f, 10.0f, tsz.x + 32.0f, 36.0f };
        DrawRectangleRec(tBg, Fade(BLACK, 0.75f));
        DrawRectangleLinesEx(tBg, 2.0f, Fade(UiTheme::AccentGold(), 0.8f));
        DrawTextEx(timeFont, timeStr, { (float)VIRTUAL_WIDTH / 2.0f - tsz.x / 2.0f, 16.0f }, 24.0f, 1.0f, RAYWHITE);
    }

    // Draw Pause Menu
    if (isPaused) {
        DrawRectangle(0, 0, VIRTUAL_WIDTH, VIRTUAL_HEIGHT, Fade(BLACK, 0.7f));
        
        Font pauseFont = GetFontDefault();
        const char* pTitle = "PAUSED";
        Vector2 ptSz = MeasureTextEx(pauseFont, pTitle, 40.0f, 2.0f);
        DrawTextEx(pauseFont, pTitle, { (float)VIRTUAL_WIDTH / 2.0f - ptSz.x / 2.0f, (float)VIRTUAL_HEIGHT / 2.0f - 100.0f }, 40.0f, 2.0f, UiTheme::AccentGold());
        
        Rectangle resumeBtn = { (float)VIRTUAL_WIDTH / 2.0f - 100.0f, (float)VIRTUAL_HEIGHT / 2.0f - 30.0f, 200.0f, 40.0f };
        Rectangle quitBtn   = { (float)VIRTUAL_WIDTH / 2.0f - 100.0f, (float)VIRTUAL_HEIGHT / 2.0f + 30.0f, 200.0f, 40.0f };
        
        // Resume
        Color rFill = Fade(UiTheme::ButtonIdle(), 1.0f);
        if (hoverResume > 0.0f) rFill = Fade(UiTheme::ButtonHover(), 1.0f);
        DrawRectangleRec(resumeBtn, rFill);
        DrawRectangleLinesEx(resumeBtn, 2.0f, UiTheme::PanelBorder());
        DrawText("RESUME", (int)(resumeBtn.x + resumeBtn.width/2 - MeasureText("RESUME", 20)/2), (int)(resumeBtn.y + 10), 20, RAYWHITE);
        
        // Quit
        Color qFill = Fade(UiTheme::ButtonIdle(), 1.0f);
        if (hoverQuit > 0.0f) qFill = Fade(UiTheme::ButtonHover(), 1.0f);
        DrawRectangleRec(quitBtn, qFill);
        DrawRectangleLinesEx(quitBtn, 2.0f, UiTheme::PanelBorder());
        DrawText("QUIT TO MENU", (int)(quitBtn.x + quitBtn.width/2 - MeasureText("QUIT TO MENU", 20)/2), (int)(quitBtn.y + 10), 20, RAYWHITE);
    }

    // Crosshair (custom cursor) - draw at mouse position in screen space
    {
        Texture2D crosshairTex = TextureManager::GetTexture(
            "assets/Free 2D Animated Vector Game Character Sprites/"
            "Free 2D Animated Vector Game Character Sprites/Extras/crosshair.png");
        if (crosshairTex.id != 0) {
            Vector2 mousePos = GetMousePosition();
            float scaleX2 = (float)VIRTUAL_WIDTH  / (float)GetScreenWidth();
            float scaleY2 = (float)VIRTUAL_HEIGHT / (float)GetScreenHeight();
            Vector2 mouseVirt = { mousePos.x * scaleX2, mousePos.y * scaleY2 };
            float cScale = 0.5f;
            float cW = crosshairTex.width  * cScale;
            float cH = crosshairTex.height * cScale;
            Rectangle cSrc = { 0, 0, (float)crosshairTex.width, (float)crosshairTex.height };
            Rectangle cDst = { mouseVirt.x, mouseVirt.y, cW, cH };
            Vector2 cOrigin = { cW / 2.0f, cH / 2.0f };
            DrawTexturePro(crosshairTex, cSrc, cDst, cOrigin, 0.0f, WHITE);
        } else {
            // Fallback: draw a simple crosshair with lines
            Vector2 mp = GetMousePosition();
            float scaleX2 = (float)VIRTUAL_WIDTH  / (float)GetScreenWidth();
            float scaleY2 = (float)VIRTUAL_HEIGHT / (float)GetScreenHeight();
            Vector2 mv = { mp.x * scaleX2, mp.y * scaleY2 };
            DrawLine((int)mv.x - 10, (int)mv.y, (int)mv.x + 10, (int)mv.y, WHITE);
            DrawLine((int)mv.x, (int)mv.y - 10, (int)mv.x, (int)mv.y + 10, WHITE);
        }
    }

    EndTextureMode();
}

// --- NETWORKING FUNCTIONS ---

const int DEFAULT_PLAYER_SKIN = 1;

// Find a RemotePlayer by authoritative playerID, or create one if it's new.
// Always keyed by the host-assigned playerID ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â never by ENet incomingPeerID.
RemotePlayer* GameplayScreen::FindOrCreateRemotePlayer(uint32_t playerID, int charSkin, int weaponSkin) {
    for (int i = 0; i < (int)remotePlayers.size(); i++) {
        if (remotePlayers[i]->peerID == playerID) {
            // Update the weapon skin in case it changed (e.g., during gameplay).
            remotePlayers[i]->SetRemoteWeaponSkin(weaponSkin);
            // Note: character skin is not updated during gameplay to avoid reload cost.
            return remotePlayers[i];
        }
    }
    // New player joined
    std::string charPath = "assets/Free 2D Animated Vector Game Character Sprites/Free 2D Animated Vector Game Character Sprites/Full body animated characters/";
    // Ensure skin is 1-4
    if (charSkin < 1 || charSkin > 4) charSkin = 1;

    RemotePlayer* rp = new RemotePlayer(
        {worldWidth / 2.0f + 100.0f, worldHeight / 2.0f},
        charPath + "Char " + std::to_string(charSkin) + "/with hands/",
        weaponSkin
    );
    rp->peerID = playerID;
    remotePlayers.push_back(rp);
    return rp;
}

// Remove a RemotePlayer by authoritative playerID (e.g., on disconnect).
void GameplayScreen::RemoveRemotePlayer(uint32_t playerID) {
    for (auto it = remotePlayers.begin(); it != remotePlayers.end(); ++it) {
        if ((*it)->peerID == playerID) {
            delete *it;
            remotePlayers.erase(it);
            return;
        }
    }
}

// Called each frame to process packets from the network
void GameplayScreen::PollNetworkEvents(float deltaTime) {
    NetworkManager::GetInstance().Update();
    std::vector<NetworkManager::NetworkEvent> events = NetworkManager::GetInstance().GetIncomingEvents();

    uint32_t myPlayerID = NetworkManager::GetInstance().GetLocalPlayerID();

    for (int i = 0; i < (int)events.size(); i++) {
        NetworkManager::NetworkEvent& ev = events[i];
        if (ev.data.size() < sizeof(PacketHeader)) continue;

        PacketHeader header;
        std::memcpy(&header, ev.data.data(), sizeof(PacketHeader));

        // Don't process self state updates sent back by the server.
        // Gameplay events (kills/damage) must always be processed.
        if (!NetworkManager::GetInstance().IsWaitingForAssignment() &&
            !NetworkManager::GetInstance().IsHost() &&
            header.playerID == myPlayerID &&
            (header.type == PacketType::PLAYER_UPDATE ||
             header.type == PacketType::PLAYER_SHOOT)) {
            continue;
        }



        if (header.type == PacketType::PLAYER_UPDATE && ev.data.size() >= sizeof(PacketPlayerUpdate)) {
            PacketPlayerUpdate pkt;
            std::memcpy(&pkt, ev.data.data(), sizeof(PacketPlayerUpdate));

            RemotePlayer* rp = FindOrCreateRemotePlayer(pkt.header.playerID, pkt.charSkin, pkt.weaponSkin);
            rp->username = std::string(pkt.username);
            // Keep the inherited Character::name in sync so the head-label draw code uses the latest username.
            rp->SetName(rp->username);
            // Clamp to valid weapon indices [0..2].
            // When multiple players join, stale/uninitialized values can cause remote
            // weapons to render with the wrong texture/hand pivot.
            int clampedWeaponIndex = pkt.currentWeaponIndex;
            if (clampedWeaponIndex < 0) clampedWeaponIndex = 0;
            if (clampedWeaponIndex > 2) clampedWeaponIndex = 2;

            rp->ApplyNetworkUpdate(pkt.position, pkt.state, clampedWeaponIndex, pkt.faceDirection, pkt.health, pkt.jumpHeight, pkt.jumpVelocity, pkt.weaponSkin);

        } else if (header.type == PacketType::PLAYER_SHOOT && ev.data.size() >= sizeof(PacketPlayerShoot)) {
            PacketPlayerShoot pkt;
            std::memcpy(&pkt, ev.data.data(), sizeof(PacketPlayerShoot));

            RemotePlayer* rp = FindOrCreateRemotePlayer(pkt.header.playerID, DEFAULT_PLAYER_SKIN, 0);
            rp->SetFaceDirection(pkt.aimDir.x < 0 ? -1 : 1);
            rp->lastAimDir = pkt.aimDir; // store aim direction for rendering
            rp->ShootInDirection(pkt.aimDir);

        } else if (header.type == PacketType::PLAYER_DAMAGE && ev.data.size() >= sizeof(PacketPlayerDamage)) {
            PacketPlayerDamage pkt;
            std::memcpy(&pkt, ev.data.data(), sizeof(PacketPlayerDamage));

            // Host-side validation: drop same-team damage in TDM (defense-in-depth
            // against a tampered client that bypassed the client-side filter).
            if (NetworkManager::GetInstance().IsHost()
                && NetworkManager::GetInstance().currentGameMode == OnlineGameMode::TEAM_DEATHMATCH) {
                const auto* sInfo = NetworkManager::GetInstance().FindPlayer(pkt.header.playerID);
                const auto* vInfo = NetworkManager::GetInstance().FindPlayer(pkt.targetPlayerID);
                int sTeam = sInfo ? sInfo->teamID : 0;
                int vTeam = vInfo ? vInfo->teamID : 0;
                if (sTeam != 0 && vTeam != 0 && sTeam == vTeam) {
                    continue; // ignore friendly-fire attempt
                }
            }

            // Is it us who was damaged?
            if (pkt.targetPlayerID == myPlayerID) {
                player->TakeDamage(pkt.damageAmount);
                if (player->IsDead()) {
                    NetworkManager::GetInstance().localDeaths++;
                    // Notify killer
                    PacketPlayerKilled killPkt;
                    killPkt.header.type = PacketType::PLAYER_KILLED;
                    killPkt.header.playerID = myPlayerID;
                    killPkt.killerPlayerID = pkt.header.playerID;
                    killPkt.victimPlayerID = pkt.targetPlayerID;
                    NetworkManager::GetInstance().SendPacket(&killPkt, sizeof(killPkt), true);

                    // Elimination: enter spectator state.
                    if (NetworkManager::GetInstance().currentGameMode == OnlineGameMode::ELIMINATION) {
                        isSpectating = true;
                        player->SetGodMode(true);
                    }
                }
            }
        } else if (header.type == PacketType::PLAYER_KILLED && ev.data.size() >= sizeof(PacketPlayerKilled)) {
            PacketPlayerKilled pkt;
            std::memcpy(&pkt, ev.data.data(), sizeof(PacketPlayerKilled));

            // Ensure kills/deaths objects exist consistently on every client.
            if (pkt.killerPlayerID == myPlayerID) {
                NetworkManager::GetInstance().localKills++;
            } else {
                RemotePlayer* killerRp = FindOrCreateRemotePlayer(pkt.killerPlayerID, DEFAULT_PLAYER_SKIN, 0);
                killerRp->AddKill();
            }

            if (pkt.victimPlayerID == myPlayerID) {
                NetworkManager::GetInstance().localDeaths++;
            } else {
                RemotePlayer* victimRp = FindOrCreateRemotePlayer(pkt.victimPlayerID, DEFAULT_PLAYER_SKIN, 0);
                victimRp->AddDeath();
            }

            // Host-only: aggregate team scores for TDM (FFA leaves teamScores untouched,
            // Elimination increments on round-end instead of on each kill).
            if (NetworkManager::GetInstance().IsHost()
                && NetworkManager::GetInstance().currentGameMode == OnlineGameMode::TEAM_DEATHMATCH) {
                const auto* k = NetworkManager::GetInstance().FindPlayer(pkt.killerPlayerID);
                const auto* v = NetworkManager::GetInstance().FindPlayer(pkt.victimPlayerID);
                if (k && v && k->teamID != 0 && v->teamID != 0 && k->teamID != v->teamID) {
                    NetworkManager::GetInstance().teamScores[k->teamID]++;
                }
            }
        } else if (header.type == PacketType::PLAYER_RESPAWN && ev.data.size() >= sizeof(PacketPlayerRespawn)) {
            PacketPlayerRespawn pkt;
            std::memcpy(&pkt, ev.data.data(), sizeof(PacketPlayerRespawn));
            RemotePlayer* rp = FindOrCreateRemotePlayer(pkt.header.playerID, pkt.charSkin, pkt.weaponSkin);
            rp->ResetHealth(100.0f);
            // Honor team spawn zones when in a team mode.
            int onlineMode2 = (int)NetworkManager::GetInstance().currentGameMode;
            int remoteTeam = rp->GetTeamID();
            if (onlineMode2 != (int)OnlineGameMode::FREE_FOR_ALL && remoteTeam != 0) {
                rp->SetPosition(GetTeamSpawnPoint(remoteTeam));
            } else {
                rp->SetPosition(pkt.spawnPosition);
            }
            rp->SetRemoteWeaponSkin(pkt.weaponSkin);
        } else if (header.type == PacketType::PLAYER_TEAM_CHANGE && ev.data.size() >= sizeof(PacketPlayerTeamChange)) {
            PacketPlayerTeamChange pkt;
            std::memcpy(&pkt, ev.data.data(), sizeof(PacketPlayerTeamChange));
            int newTeam = pkt.teamID;
            if (pkt.header.playerID == myPlayerID) {
                // Local player's team change already applied in NetworkManager; update
                // the local Character and (in team modes) reposition to the new team's half.
                player->SetTeamID(newTeam);
                if (NetworkManager::GetInstance().currentGameMode != OnlineGameMode::FREE_FOR_ALL && newTeam != 0) {
                    player->SetPosition(GetTeamSpawnPoint(newTeam));
                }
            } else {
                if (RemotePlayer* rp = FindByPeerID(pkt.header.playerID)) {
                    rp->SetTeamID(newTeam);
                }
            }
        } else if (header.type == PacketType::ROUND_START && ev.data.size() >= sizeof(PacketRoundStart)) {
            PacketRoundStart pkt;
            std::memcpy(&pkt, ev.data.data(), sizeof(PacketRoundStart));
            currentRoundNumber = pkt.roundNumber;
            roundInProgress    = true;
            roundBannerTimer   = 0.0f;
            isSpectating       = false;
            player->SetGodMode(false);
            player->ResetHealth(100.0f);
            int onlineMode2 = (int)NetworkManager::GetInstance().currentGameMode;
            int tid = (onlineMode2 == (int)OnlineGameMode::FREE_FOR_ALL) ? 0
                                                              : NetworkManager::GetInstance().localTeamID;
            player->SetPosition((tid == 0)
                ? Vector2{ worldWidth / 2.0f + GetRandomValue(-200, 200),
                           worldHeight / 2.0f + GetRandomValue(-200, 200) }
                : GetTeamSpawnPoint(tid));
        } else if (header.type == PacketType::ROUND_END && ev.data.size() >= sizeof(PacketRoundEnd)) {
            PacketRoundEnd pkt;
            std::memcpy(&pkt, ev.data.data(), sizeof(PacketRoundEnd));
            roundInProgress  = false;
            roundBannerTimer = 3.0f;
            lastRoundWinnerID = pkt.winningTeamID;
            // Force spectator off here too — host will either start next round or end match.
            isSpectating = true;
            player->SetGodMode(true);
        } else if (header.type == PacketType::PLAYER_DISCONNECT && ev.data.size() >= sizeof(PacketPlayerDisconnectHeader)) {
            PacketPlayerDisconnectHeader pkt;
            std::memcpy(&pkt, ev.data.data(), sizeof(PacketPlayerDisconnectHeader));
            RemoveRemotePlayer(pkt.header.playerID);
        }
    }
}

Vector2 GameplayScreen::GetFarSpawnPoint() {
    std::vector<Vector2> mapSpawns;
    if (currentMapData && !currentMapData->scene.spawnPoints.empty()) {
        for (const auto& sp : currentMapData->scene.spawnPoints) {
            mapSpawns.push_back(sp.position);
        }
    } else {
        mapSpawns = {
            { 300.0f, 300.0f },
            { 1800.0f, 300.0f },
            { 3500.0f, 300.0f },
            { 300.0f, 1000.0f },
            { 1800.0f, 1000.0f },
            { 3500.0f, 1000.0f },
            { 300.0f, 1700.0f },
            { 1800.0f, 1700.0f },
            { 3500.0f, 1700.0f }
        };
    }
    int numSpawns = (int)mapSpawns.size();

    std::vector<Character*> activeChars;
    if (!player->IsDead()) activeChars.push_back(player);

    if (activeChars.empty()) {
        return mapSpawns[GetRandomValue(0, numSpawns - 1)];
    }

    Vector2 bestSpawn = mapSpawns[0];
    float maxMinDistSq = -1.0f;

    for (int i = 0; i < numSpawns; i++) {
        float minDistSq = 99999999.0f;
        for (auto c : activeChars) {
            float dx = c->GetPosition().x - mapSpawns[i].x;
            float dy = c->GetPosition().y - mapSpawns[i].y;
            float distSq = dx*dx + dy*dy;
            if (distSq < minDistSq) {
                minDistSq = distSq;
            }
        }
        if (minDistSq > maxMinDistSq) {
            maxMinDistSq = minDistSq;
            bestSpawn = mapSpawns[i];
        }
    }
    return bestSpawn;
}

int GameplayScreen::GetCharacterTeam(const Character* c) const {
    return c ? c->GetTeamID() : 0;
}

Character* GameplayScreen::FindFlagCarrier() const {
    if (flagCarrier && !flagCarrier->IsDead()) return flagCarrier;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Mode-specific helpers (team spawns, alive counts, round broadcasts)
// ---------------------------------------------------------------------------

Vector2 GameplayScreen::GetTeamSpawnPoint(int teamID) const {
    // GR (1) -> left third of the map; BL (2) -> right third.
    float xMin = (teamID == 1) ? 50.0f              : worldWidth * 0.66f;
    float xMax = (teamID == 1) ? worldWidth * 0.33f : worldWidth - 50.0f;
    float yMin = worldHeight * 0.10f;
    float yMax = worldHeight * 0.90f;

    // Prefer a real map spawn point that falls inside the team's x-range.
    if (currentMapData && !currentMapData->scene.spawnPoints.empty()) {
        Vector2 best{0,0};
        bool found = false;
        int bestTie = -1;
        for (const auto& sp : currentMapData->scene.spawnPoints) {
            if (sp.position.x < xMin || sp.position.x > xMax) continue;
            int tie = GetRandomValue(0, 1000);
            if (!found || tie > bestTie) {
                bestTie = tie;
                best = sp.position;
                found = true;
            }
        }
        if (found) return best;
    }

    return { (float)GetRandomValue((int)xMin, (int)xMax),
             (float)GetRandomValue((int)yMin, (int)yMax) };
}

int GameplayScreen::CountAliveOnTeam(int teamID) const {
    int count = 0;
    // Local player counts as alive unless they're truly dead (health <= 0).
    // Spectators in Elimination still count: god mode clamps health to 1, so
    // IsDead() returns false; that's correct — their team is NOT wiped.
    if (!player->IsDead() && player->GetTeamID() == teamID) {
        count++;
    }
    for (auto* rp : remotePlayers) {
        if (!rp->IsDead() && rp->GetTeamID() == teamID) {
            count++;
        }
    }
    return count;
}

void GameplayScreen::BroadcastRoundStart(int roundNumber) {
    PacketRoundStart pkt{};
    pkt.header.type     = PacketType::ROUND_START;
    pkt.header.playerID = 0; // from host
    pkt.roundNumber     = roundNumber;
    NetworkManager::GetInstance().SendPacket(&pkt, sizeof(pkt), true);
    currentRoundNumber = roundNumber;
    roundInProgress    = true;
    roundBannerTimer   = 0.0f;
    isSpectating       = false;
    player->SetGodMode(false);
    player->ResetHealth(100.0f);
    // Reposition everyone into their team halves.
    for (auto* rp : remotePlayers) {
        int tid = rp->GetTeamID();
        if (tid != 0) rp->SetPosition(GetTeamSpawnPoint(tid));
    }
    int localTeam = NetworkManager::GetInstance().localTeamID;
    if (localTeam != 0) player->SetPosition(GetTeamSpawnPoint(localTeam));
}

void GameplayScreen::BroadcastRoundEnd(int winningTeamID, int grScore, int blScore) {
    PacketRoundEnd pkt{};
    pkt.header.type     = PacketType::ROUND_END;
    pkt.header.playerID = 0;
    pkt.winningTeamID   = winningTeamID;
    pkt.grScore         = grScore;
    pkt.blScore         = blScore;
    NetworkManager::GetInstance().SendPacket(&pkt, sizeof(pkt), true);
    roundInProgress  = false;
    roundBannerTimer = 3.0f;
    lastRoundWinnerID = winningTeamID;
    isSpectating = true;
    player->SetGodMode(true);
}

RemotePlayer* GameplayScreen::FindByPeerID(uint32_t playerID) {
    for (auto* rp : remotePlayers) {
        if (rp->peerID == playerID) return rp;
    }
    return nullptr;
}
