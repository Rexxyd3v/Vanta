#ifndef BOT_ENEMY_H
#define BOT_ENEMY_H

#include "character.h"
#include <functional>

class BotEnemy : public Character {
public:
    BotEnemy(Vector2 startPosition, const std::string& assetPath);
    ~BotEnemy() override;

    // AI logic injected via Update
    void Update(float deltaTime) override;

    // Provide a reference to the target to chase/shoot at
    void UpdateAI(float deltaTime, Vector2 targetPosition);

    bool Shoot(Vector2 target) { return GetFlashBlind()>0?false:Character::Shoot(target); }
    void SetDifficulty(int level);
    void MoveToObjective(float dt, Vector2 target,Vector2 spacing={});
    bool ShouldRespawn(float minimumDelay=0) const;
    void ResetDeathTimer();
    struct Hazard {Vector2 position;float radius;};
    void SetTacticalContext(std::vector<Hazard> danger,std::function<bool(Vector2)> clear,
                           std::function<bool(Vector2,Vector2)> sight){hazards=std::move(danger);walkable=std::move(clear);visible=std::move(sight);}
    void ManageAmmo();
    bool IsEvading()const{return evading;}
    bool IsUsingCover()const{return usingCover;}

    // Alpha for drawing (used for fade-in effect in demo mode)
    float GetDrawAlpha() const override;

    // Demo/spectator mode: when true, the bot uses more aggressive AI constants
    // (higher speed, faster shooting, more dashes/jumps) for the main menu
    // live-gameplay background. Does NOT affect actual gameplay bots.
    static void SetDemoMode(bool enabled);
    static bool demoMode;
// Set the spawn fade-in timer (in seconds). Used by MenuBackground to start fade-in on respawn.
void SetSpawnFadeTimer(float t) { spawnFadeTimer = t; }

private:
    std::vector<Hazard> hazards;
    std::function<bool(Vector2)> walkable;
    std::function<bool(Vector2,Vector2)> visible;
    bool evading=false,usingCover=false;
    float maneuverClock=0;float coverClock=0;float tacticalTimer=0;Vector2 tacticalGoal{};
    int preferredWeapon=-1;
    bool TacticalMove(float dt,Vector2 target,bool objective);
    bool RouteClear(Vector2 target)const;
    int difficulty = 1;
    float spawnFadeTimer{0.0f};
    float respawnDelay{0.0f};    // Time to wait before respawning (randomized per bot)
    float stateTimer;
    Vector2 targetPosition; // Where to move
    bool isMoving;
    float deathTimer;
    // Strafe state for demo mode
    float strafeDir;   // -1 or 1, randomized after each shot
    float strafeTimer; // how long to strafe in current direction
};

#endif // BOT_ENEMY_H
