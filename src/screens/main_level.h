#ifndef GAMEPLAY_SCREEN_H
#define GAMEPLAY_SCREEN_H

#include <raylib.h>
#include "iscreen.h"
#include "../entities/player.h"
#include "../entities/remote_player.h"
#include "../entities/bot_enemy.h"
#include <vector>
#include <memory>
#include <functional>
#include "../constants.h"
#include "../map_loader/MapRegistry.h"
#include <string>
#include <unordered_map>

// Default world dimensions (fallback if no map loaded)
#define DEFAULT_WORLD_WIDTH  3900
#define DEFAULT_WORLD_HEIGHT 2040

// A renderable item in the depth-sorted list. Stores its feet-Y (so the
// std::sort comparator is trivial) and a draw callback. The screen pushes
// Player, RemotePlayer, and BotEnemy instances into the list and sorts once per frame.
struct RenderItem {
    float depthY;
    std::function<void()> draw;
};

class GameplayScreen : public IScreen {
public:
    explicit GameplayScreen(GameMode mode);
    ~GameplayScreen() override;

    bool Update(float deltaTime) override;
    void Draw(RenderTexture2D target) override;

    // Used by voice/proximity system in main.cpp
    Character* GetLocalPlayer() const { return player; }
    
    // Get current world dimensions (from loaded TMX map or defaults)
    float GetWorldWidth() const { return worldWidth; }
    float GetWorldHeight() const { return worldHeight; }

private:
    // Map data
    MapData* currentMapData;
    TmxMap* currentTmxMap;
    float worldWidth;
    float worldHeight;
    std::string mapFolderPath;
    TmxObjectGroup wallsObjectGroup; // Object group from TMX for collision
    std::vector<TmxObjectGroup> allObjectGroups; // All object layers from TMX


    // Entities
    GameMode currentMode;
    Player* player;
    Player* player2;
    std::vector<RemotePlayer*> remotePlayers; // Online opponents

    // Elimination / round-flow state (online host-authoritative, mirrored on clients).
    bool  isSpectating        = false;
    bool  roundInProgress     = false;
    float roundBannerTimer    = 0.0f;
    float matchEndTimer       = 0.0f;
    int   currentRoundNumber  = 0;
    int   lastRoundWinnerID   = 0;
    Vector2 flagPosition{};
    Vector2 teamBasePositions[3]{};
    Character* flagCarrier = nullptr;
    int flagTeam = 0;

    // Pause menu state
    bool isPaused = false;
    float hoverResume = 0.0f;
    float hoverQuit = 0.0f;

    Camera2D camera;

    Texture2D bgTex1;
    Texture2D bgTex2;
    Texture2D bgDetail; // sand/detail layer
    Texture2D healthBarFrames[9];  // Animated health bar: index 0=frame1 (lowest), index 8=frame9 (full)
    Texture2D dashBarFrames[6];    // Animated dash bar:   index 0=dash1.png (full), index 5=dash6.png (empty)
    Texture2D headPortrait;        // Character head portrait shown in HUD circle
    float worldTime;    // accumulates delta for animations
    float lastDashCooldown = 0.0f; // tracks transition 0 -> 2.0 to detect a fresh dash

    float spawnTimer;
    float netSendTimer;

    // Networking helpers
    void PollNetworkEvents(float deltaTime);
    RemotePlayer* FindOrCreateRemotePlayer(uint32_t playerID, int charSkin, int weaponSkin = 0);
    RemotePlayer* FindByPeerID(uint32_t playerID);

    void RemoveRemotePlayer(uint32_t playerID);

    // Mode-specific helpers
    Vector2 GetTeamSpawnPoint(int teamID) const;
    int     CountAliveOnTeam(int teamID) const;
    void    BroadcastRoundStart(int roundNumber);
    void    BroadcastRoundEnd(int winningTeamID, int grScore, int blScore);

    // Physics & collision helpers
    void  ResolveWorldCollision(Character* c);
    void  ClampCharacterToWorld(Character* c);
    void  CheckCollisions();
    Character* GetNearestEnemy(Vector2 pos);
    Vector2 GetFarSpawnPoint();
    int GetCharacterTeam(const Character* c) const;
    Character* FindFlagCarrier() const;
};

#endif
