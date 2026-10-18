#ifndef GAMEPLAY_SCREEN_H
#define GAMEPLAY_SCREEN_H

#include <raylib.h>
#include "iscreen.h"
#include "../entities/player.h"
#include "../entities/remote_player.h"
#include "../entities/bot_enemy.h"
#include "../entities/combat_medals.h"
#include <vector>
#include <memory>
#include <functional>
#include "../constants.h"
#include "../ui/offline_setup.h"
#include "../map_loader/MapRegistry.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

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

struct PacketFlagState;
struct PacketDamageConfirm;
class GameplayScreen : public IScreen {
public:
    explicit GameplayScreen(GameMode mode);
    ~GameplayScreen() override;

    bool Update(float deltaTime) override;
    void Draw(RenderTexture2D target) override;

    GameMode GetMode() const { return currentMode; }
    bool ThrowGrenade(int kind);
    float GetGrenadeCooldown(int kind) const { return kind >= 0 && kind < 3 ? grenadeCooldown[kind] : 0; }

    // Used by voice/proximity system in main.cpp
    Character* GetLocalPlayer() const { return player; }
    
    // Get current world dimensions (from loaded TMX map or defaults)
    float GetWorldWidth() const { return worldWidth; }
    float GetWorldHeight() const { return worldHeight; }
    bool ShouldReturnToLobby() const { return returnToLobby; }

private:
    friend struct GameplayEffectsTestAccess;
    std::vector<std::unique_ptr<BotEnemy>> bots;
    std::unordered_set<uint32_t> eliminated;
    bool returnToLobby = false;
    float elapsed = 0, stateTimer = 0, roundElapsed = 0, deadFor = 0;
    float hitMarker = 0, damageFlash = 0;
    Vector2 damageSource{};
    int matchWinner = -1;
    struct DamageNumber { Vector2 position; float amount; float life; };
    std::vector<DamageNumber> damageNumbers;
    void RegisterHit(Vector2 position, float damage);
    void ProcessKill(uint32_t killer, uint32_t victim,int weapon=0,int skin=0);
    struct KillFeedEntry {
        std::string killer,victim;
        int weapon=0,skin=0,killerTeam=0,victimTeam=0;
        bool localKill=false,localDeath=false;
        float remaining=7.0f;
    };
    std::vector<KillFeedEntry> killFeed;
    void AddKillFeed(const std::string& killer,const std::string& victim,int weapon,int skin,
                     int killerTeam,int victimTeam,bool localKill,bool localDeath);
    void UpdateKillFeed(float dt);
    void DrawKillFeed();
    struct RecapHit {std::string attacker;int weapon=0,skin=0,hits=0;float damage=0;};
    std::vector<RecapHit> lifeDamage;
    std::string deathKiller;int deathWeapon=0,deathSkin=0;
    Character* spectateTarget=nullptr;
    float killConfirm=0,resultsAge=0;int resultsScroll=0;
    uint32_t combatEpoch=0;
    uint32_t damageSerial=0;std::unordered_set<uint64_t> damageReceipts;
    struct Impact {Vector2 position,normal;float age;int material;};
    std::vector<Impact> impacts;
    std::unordered_map<Character*,bool> wasReloading;
    void NoteDamage(Character* attacker,Character* victim,float damage,int weapon,int skin=0,uint32_t shot=0);
    bool AcceptDamageConfirm(const PacketDamageConfirm& packet);
    void BeginDeath(Character* killer,int weapon,int skin);
    std::vector<Character*> SpectatorCandidates()const;
    void UpdateSpectator(float dt);
    void UpdateBulletWorld();
    void DrawCombatEffects();
    void DrawDeathRecap();
    void DrawMatchResults();
    void UpdateResultsControls();
    void RestartMatch(bool broadcast=true);
    void LeaveResults();
    void CreditCapture(Character* actor);
    std::vector<Character*> RankedActors()const;
    std::vector<Character*> VictoryActors()const;
    void DrawVictorySquad();
    void PrepareBotTactics(BotEnemy* bot);
    bool BotSight(Vector2 a,Vector2 b)const;
    enum class FlagBotRole {Runner,Escort,Defender,Interceptor,Flanker};
    struct FlagBotPlan {FlagBotRole role=FlagBotRole::Flanker;Vector2 destination{},routeTarget{};std::vector<Vector2> route;float repath=0;};
    std::unordered_map<BotEnemy*,FlagBotPlan> flagBotPlans;
    BotEnemy* flagRunners[3]{};
    std::vector<unsigned char> flagWalkGrid,flagGridLinks;
    int flagGridWidth=0,flagGridHeight=0;
    void PlanFlagBots();
    Vector2 FlagBotWaypoint(BotEnemy* bot,float dt);
    bool FlagRouteClear(Vector2 a,Vector2 b)const;
    Vector2 FlagBotSpacing(BotEnemy* bot)const;
    Medals::Tracker medalTracker;
    struct MedalToast{Medals::Kind kind;float age=0;};
    std::vector<MedalToast> medalQueue;
    float medalClock=0;
    void NoteMedalKill(uint64_t killer,uint64_t victim,uint64_t local,bool enemies);
    void UpdateMedals(float dt);
    void DrawMedals();
    Offline::Setup offlineSetup;
    int offlineScores[3] = {0,0,0};
    bool offlineRoundOver = false;
    struct FlagState {
        Vector2 position{};
        Character* carrier=nullptr;
        Character* previousCarrier=nullptr;
        bool dropped=false;
        float returnTimer=0, respawnTimer=0, pickupLock=0;
    };
    FlagState flags[3];
    float flagStateTimer=0;
    uint32_t flagAnnouncementSerial=0;
    std::unordered_set<uint32_t> flagInteractRequests;
    float flagAnnouncementTimer=0;
    std::string flagAnnouncement;
    int flagAnnouncementTeam=0;
    std::string offlineResult;
    void InitializeOffline();
    void StartOfflineRound();
    void EndOfflineRound(int winner, const char* reason);
    void UpdateOfflineFlow(float dt);
    void UpdateFlags(float dt, bool dropFlag, Character* actor=nullptr);
    bool IsFlagDeathmatch() const;
    void UpdateOnlineFlags(float dt);
    PacketFlagState MakeFlagState() const;
    void ApplyFlagState(const PacketFlagState& packet);
    void ResetFlag(int team, float delay=0);
    void InitializeBases();
    Vector2 SafeSpawn(Vector2 desired);
    Vector2 GetDeathmatchSpawnPoint(uint32_t identity) const;
    Vector2 GroundPoint(Vector2 anchor) const;
    bool HasTeamBases() const;
    bool HasDeathmatchBases() const;
    void DrawBases();
    void DrawFlags();
    void DrawWavingFlag(Vector2 feet,int team,float size,float phase,int direction=1,float poleHeight=136) const;
    void AnnounceFlag(const std::string& text,int team);
    void RecordOfflineKill(Character* killer, Character* victim,int weapon=-1,int skin=0);
    bool OfflineEnemies(const Character* a, const Character* b) const;
    Character* NearestOfflineEnemy(Character* source) const;
    void UpdatePractice(float dt);
    void UpdateMatchFlow(float dt);
    void DrawHud();
    struct GrenadeEffect { Vector2 start, target; float age; int kind; bool local; uint32_t ownerID = 0; int ownerTeam = 0; };
    std::vector<GrenadeEffect> grenades;
    float grenadeCooldown[3] = {0, 0, 0};
    float explosionShake = 0, poisonExposure = 0;
    void UpdateGrenades(float dt, bool allowInput);
    void DrawGrenades();
    void DrawGrenadeScars();
    void ApplyPoisonDamage(const GrenadeEffect& effect, Character* victim);
    void ResetGrenades() { grenades.clear(); grenadeCooldown[0] = grenadeCooldown[1] = grenadeCooldown[2] = 0; explosionShake = poisonExposure = 0; }
    int activeBag=0;
    bool bagMenuOpen=false,bagInputConsumed=false;
    Vector2 bagSpawnPosition{};
    float bagMessageTimer=0;
    bool CanChangeBag() const;
    bool SelectBag(int index);
    void ApplyActiveBag(bool respawn=false);
    void UpdateBagControls(float dt);
    void UpdateEquipmentCombat();
    void DrawBagHud();
    void RespawnLocal();
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
    Vector2 teamBasePositions[3]{};
    std::vector<Vector2> teamSpawnPoints[3];
    Vector2 deathmatchBases[4]{};
    Vector2 deathmatchSpawns[16]{};

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
    void  ResolveWorldMovement(Character* c, Vector2 previous);
    void  ClampCharacterToWorld(Character* c);
    void  CheckCollisions();
    Character* GetNearestEnemy(Vector2 pos);
    Vector2 GetFarSpawnPoint();
    int GetCharacterTeam(const Character* c) const;
    bool cinematicCapture = false;
    bool IsFreeForAll() const;
    Vector2 RandomArenaSpawn(Character* actor=nullptr);
    bool ArenaClear(Vector2 anchor,float margin=0) const;
    struct BonusNode {Vector2 position{};float cooldown=0;int kind=0;uint32_t serial=0,owner=UINT32_MAX;};
    std::vector<BonusNode> bonusNodes;
    struct BonusProjectile {Vector2 position{},velocity{};float age=0;int kind=0;uint32_t owner=0;bool local=true;Character* actor=nullptr;uint32_t shot=0;};
    struct BonusBlast {Vector2 position{};float age=0;int kind=0;uint32_t owner=0;bool local=true;Character* actor=nullptr;uint32_t shot=0;};
    std::vector<BonusProjectile> bonusProjectiles;
    std::vector<BonusBlast> bonusBlasts;
    float bonusClock=0,bonusNetTimer=0,bonusMessageTime=0,spawnGrace=0;
    std::string bonusMessage;
    int awaitingBonusNode=-1;
    void InitializeBonuses();void UpdateBonuses(float dt);void DrawBonuses();void DrawBonusHud();
    void SpawnBonusShot(Character* actor,int kind,Vector2 direction,uint32_t owner,bool local);
    bool ThrowBonus(Character* actor,Vector2 target,uint32_t owner,bool local=true);
    void ClaimBonus(int index,Character* actor,uint32_t owner);
    void SendBonusState();void HandleBonusPacket(const void* bytes,size_t size);
    void ApplyBonusHit(Character* shooter,Character* victim,float damage,int kind,uint32_t owner,uint32_t shot=0);

};

#endif
