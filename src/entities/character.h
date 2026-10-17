#ifndef CHARACTER_H
#define CHARACTER_H

#include <raylib.h>
#include "animation.h"
#include "projectile.h"
#include "weapon_geometry.h"
#include <map>
#include <string>
#include <vector>
#include <memory>

enum class CharState {
    IDLE,
    WALK,
    JUMP_START,
    JUMP_END,
    FALL,
    HIT,
    DEATH
};

class Character {
public:
    Character(Vector2 startPosition, const std::string& assetPath, float scale, int weaponSkin = 0);
    virtual ~Character();

    virtual void Update(float deltaTime) = 0;
    virtual void Draw();
    virtual void DrawUI(); // Renders overhead HUD elements (health bar, IGN) after world objects

    // Debug visualization of the foot-anchored collision shape (AABB + circle).
    // Toggled globally with SetDebugDrawCollision(); safe to call from any
    // screen that wants to see why a character is or isn't colliding.
    virtual void DrawDebugCollision() const;
    static void SetDebugDrawCollision(bool enabled) { debugDrawCollision = enabled; }
    static bool IsDebugDrawCollision()                { return debugDrawCollision; }

    Vector2 GetPosition() const   { return position; }
    void    SetPosition(Vector2 pos) { position = pos; }
    int     GetFaceDirection() const { return faceDirection; }
    void    SetFaceDirection(int dir) { faceDirection = dir; }
    void    SetAimTarget(Vector2 target) { aimTarget = target; }
    Vector2 GetAimTarget() const { return aimTarget; }
    void    ResetHealth(float amount) {
        health = amount;
        healthDisplay = amount;
        currentShootCooldown = 0.0f;
        reloadTimer = 0.0f;
        dashTimer = dashCooldown = shieldTimer = shieldCooldown = 0.0f;
        floorHeight = 0.0f;
        projectiles.clear();
        RefillAmmo();
        jumpHeight = 0.0f;
        jumpVelocity = 0.0f;
        SetState(CharState::IDLE);
        if (animations.count(CharState::IDLE)) animations[CharState::IDLE]->Reset();
    }
    bool    IsDead() const { return health <= 0; }
    bool    IsMonster() const { return isMonster; }
    bool    IsAnimationFinished() const;
    void    TakeDamage(float amount);
    float   GetHealth() const;

    // God mode: clamps health to a minimum of 1.0 so the character can never die.
    // Used by the main menu's background simulation where the player is rendered
    // in the middle of an active battle but must not actually die. Visual hit
    // feedback (lastHitTimer, etc.) still triggers normally.
    void    SetGodMode(bool enabled) { godMode = enabled; }
    bool    IsGodMode() const { return godMode; }

    // Mute weapon SFX (menu background combat should be silent).
    static void SetCombatAudioEnabled(bool enabled) { combatAudioEnabled = enabled; }
    static bool IsCombatAudioEnabled() { return combatAudioEnabled; }

    // Team affiliation (online modes only). 0 = None / FFA, 1 = GR, 2 = BL.
    int  GetTeamID() const { return teamID; }
    void SetTeamID(int id) { teamID = id; }

    // Stats
    std::string GetName() const { return name; }
    void SetName(const std::string& n) { name = n; }
    int GetKills() const { return kills; }
    int GetDeaths() const { return deaths; }
    void AddKill() { kills++; }
    void AddDeath() { deaths++; }
    float GetShootCooldown() const { return currentShootCooldown; }
    void SetShootCooldown(float val) { currentShootCooldown = val; }

    // Skills
    float GetDashCooldown() const { return dashCooldown; }
    float GetDashTimer() const { return dashTimer; }
    float GetShieldTimer() const { return shieldTimer; }
    float GetShieldCooldown() const { return shieldCooldown; }
    bool ActivateDash(Vector2 direction);
    bool ActivateShield();
    void UpdateSkills(float deltaTime);

    // Weapons and Reloading
    int GetAmmo() const;
    int GetMaxAmmo() const;
    bool IsReloading() const;
    void TriggerReload();
    void RefillAmmo();
    float GetReloadProgress() const;

    // Platform / jump system
    float GetJumpHeight()    const { return jumpHeight; }
    void  SetJumpHeight(float h)   { jumpHeight = h; }
    float GetJumpVelocity()  const { return jumpVelocity; }
    void  SetJumpVelocity(float v) { jumpVelocity = v; }
    float GetBaseHeight()    const { return baseHeight; }
    void  SetBaseHeight(float h)   { baseHeight = h; }

    // Depth-sort key: the world Y of the character's feet. Higher Y = drawn
    // in front; lower Y = drawn behind.
    // Uses the actual visual bottom edge of the sprite (position.y - jumpHeight + spriteHeight/2)
    // so the sorted order matches what the player actually sees.
    virtual float GetDepthY() const {
        auto it = animations.find(currentState);
        if (it != animations.end() && it->second && it->second->HasFrames()) {
            float draw_y = position.y - jumpHeight;
            float sh = (float)it->second->FrameHeight() * scale;
            return draw_y + sh * 0.5f; // actual bottom edge of sprite = feet
        }
        return position.y - jumpHeight + feetOffset; // fallback
    }

    // ------------------------------------------------------------------
    // Collision shape (foot-anchored, small).
    //
    // The old hardcoded 36x40 box was too small relative to the visible
    // sprite (a Char1 drawn at 0.08 scale is ~164 px wide) and sat in
    // the *middle* of the body, not at the feet. That made characters
    // feel like they hit walls from far away.
    //
    // The new shape is a tiny box (12x6 by default) anchored to the
    // *bottom* of the visible sprite — the character's feet. It is
    // intentionally small so the player can walk through tight gaps
    // (between rocks, through narrow TMX walls) without the visible
    // arms/gun "magnetically" sticking to nearby walls.
    // ------------------------------------------------------------------

    // Width and height (in world units) of the foot-anchored collision box.
    Vector2 GetCollisionSize() const;

    // World-space AABB of the foot-anchored collision box. The bottom of
    // the box is the bottom of the visible sprite (the feet); the box
    // is centered horizontally on the sprite.
    Rectangle GetCollisionBounds() const;

    // Small circle at the feet, for code that wants a smooth shape.
    struct Circle { Vector2 center; float radius; };
    Circle GetFeetCircle() const;

    // Alpha multiplier for drawing (0.0f to 1.0f). Override in subclasses for effects like fade-in.
    virtual float GetDrawAlpha() const { return 1.0f; }

    // Vertical offset from the sprite center to the feet. Slightly positive
    // so the depth key matches the bottom of the silhouette, which is what
    // RPG Maker-style engines use to decide overlap.
    float GetFeetOffset() const { return feetOffset; }
    void  SetFeetOffset(float f) { feetOffset = f; }

    // floorHeight: 0 = flat ground, >0 = standing on an elevated platform
    float GetFloorHeight() const { return floorHeight; }
    void  SetFloorHeight(float h){ floorHeight = h; }

    void UpdatePhysics(float deltaTime); // uses internal floorHeight

    std::vector<std::shared_ptr<Projectile>>& GetProjectiles() { return projectiles; }
    void SwitchWeapon(int index);
    bool Shoot(Vector2 targetPos); // expose for remote player
    void ShootInDirection(Vector2 dir); // for remote players: uses normalized direction directly

    // Getters for networking
    int       GetWeaponIndex()  const { return currentWeaponIndex; }
    CharState GetCurrentState() const { return currentState; }
    bool      DidShoot() { bool ret = justShot; justShot = false; return ret; }
    Vector2   GetHandPosition() const;
    WeaponPose GetWeaponPose(Vector2 target) const;
    WeaponPose GetWeaponPoseInDirection(Vector2 direction) const;
    Vector2 GetLastShotDirection() const { return lastShotDirection; }

protected:
    Vector2    position;
    Vector2    velocity;
    int        faceDirection; // 1 = right, -1 = left
    CharState  currentState;
    bool       justShot;
    bool       isMonster;
    std::string name;
    int        kills;
    int        deaths;
    float      muzzleFlashTimer;
    Vector2 lastShotDirection{1,0};
    float      dashTimer;
    float      dashCooldown;
    Vector2    dashDirection;
    float      shieldTimer;
    float      shieldCooldown;
    int        ammo[3];
    float      reloadTimer;
    float      lastHitTimer;
    float      scale;
    float      health;
    float      healthDisplay; // for smooth health bar
    bool       godMode;       // when true, health cannot drop below 1.0 (no death)
    float      speed;
    float      shootCooldown;
    float      currentShootCooldown;

    float   jumpHeight;    // visual offset upward when jumping / on platform
    float   jumpVelocity;  // current vertical velocity
    float   baseHeight;    // (legacy, kept for compatibility)
    float   floorHeight;   // current floor level (0 = ground, rock.visualHeight = platform)
    float   feetOffset;    // vertical offset from sprite center to the feet, used for depth sort
    Vector2 aimTarget;
    float   weaponRotation;
    int     teamID;        // 0 = None / FFA, 1 = GR, 2 = BL (synced via PacketPlayerTeamChange)

    std::map<CharState, Animation*>          animations;
    std::vector<std::shared_ptr<Projectile>> projectiles;
    std::vector<Texture2D>                   weaponTextures;
    int                                      currentWeaponIndex;
    int                                      weaponSkinId;
    int                                      currentWeaponSkin;

    static bool combatAudioEnabled;
    static bool debugDrawCollision;

    void    LoadAnimations(const std::string& baseDir);
    void    SetState(CharState newState);
    void    SetWeaponSkin(int weaponSkin);
};

#endif // CHARACTER_H
