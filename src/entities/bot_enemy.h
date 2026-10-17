#ifndef BOT_ENEMY_H
#define BOT_ENEMY_H

#include "character.h"

class BotEnemy : public Character {
public:
    BotEnemy(Vector2 startPosition, const std::string& assetPath);
    ~BotEnemy() override;

    // AI logic injected via Update
    void Update(float deltaTime) override;

    // Provide a reference to the target to chase/shoot at
    void UpdateAI(float deltaTime, Vector2 targetPosition);

    void SetDifficulty(int level);
    void MoveToObjective(float dt, Vector2 target);
    bool ShouldRespawn() const;
    void ResetDeathTimer();

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
