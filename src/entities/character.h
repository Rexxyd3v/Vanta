#ifndef CHARACTER_H
#define CHARACTER_H

#include <raylib.h>
#include "animation.h"
#include "projectile.h"
#include "weapon_geometry.h"
#include "weapon_catalog.h"
#include "bonus_catalog.h"
#include "combat_stats.h"
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
    void DrawVictoryPose(Vector2 center,float size,int facing);

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
        bonusGun=-1;bonusAmmo=0;bonusEquipped=false;bonusPending=-1;bonusLastShot=-1;bonusThrow=-1;bonusCharges=0;statusSlowTimer=0;slowFactor=1;
        recoil=bodyLean=pumpTimer=airPeak=0;casings.clear();landingDust.clear();stepDistance=0;motionTracked=airborne=false;landingAge=1;remoteReload=false;
        health = amount;
        healthDisplay = amount;
        currentShootCooldown = 0.0f;
        reloadTimer = 0.0f;
        dashTimer = dashCooldown = shieldTimer = shieldCooldown = 0.0f;
        dashEchoes.clear(); dashEchoClock = shieldAge = shieldHitFlash = 0.0f;
        floorHeight = 0.0f;
        meleeSwing=meleeCooldown=flashBlindTimer=0;meleeHitPending=false;justShot=false;
        projectiles.clear();
        RefillAmmo();
        jumpHeight = 0.0f;
        jumpVelocity = 0.0f;
        SetState(CharState::IDLE);
        if (animations.count(CharState::IDLE)) animations[CharState::IDLE]->Reset();
    }
    void GiveBonus(int kind) { if(!Bonus::Valid(kind))return;if(kind<3){bonusGun=kind;bonusAmmo=Bonus::Ammo(kind);EquipBonus();}else{bonusThrow=kind;bonusCharges=Bonus::Ammo(kind);} }
    void EquipBonus() { if(bonusGun>=0 && bonusAmmo>0){bonusEquipped=true;meleeVariant=-1;utilityItem=-1;reloadTimer=0;} }
    int GetBonusGun() const {return bonusGun;} int GetBonusAmmo() const{return bonusAmmo;}
    bool BonusEquipped() const{return bonusEquipped && bonusAmmo>0;}
    int GetBonusThrow() const{return bonusThrow;} int GetBonusCharges() const{return bonusCharges;}
    bool UseBonusThrow(){if(bonusCharges<=0)return false;--bonusCharges;return true;}
    int ConsumeBonusShot(){int kind=bonusPending;bonusPending=-1;return kind;}
    int LastBonusShot() const{return bonusLastShot;}
    void SetRemoteBonus(int kind,int rounds,bool equipped){bonusGun=std::clamp(kind,-1,2);bonusAmmo=std::clamp(rounds,0,60);bonusEquipped=equipped && bonusGun>=0 && bonusAmmo>0;}
    void ClearBonusGun(){bonusGun=-1;bonusAmmo=0;bonusEquipped=false;}
    void ApplySlow(float duration,float amount){if(duration<=0)return;slowFactor=std::min(GetSlowFactor(),std::clamp(amount,0.0f,1.0f));statusSlowTimer=std::max(statusSlowTimer,duration);dashTimer=0;}
    float GetSlowFactor()const{return statusSlowTimer>0?slowFactor:1.0f;}
    float GetSlowTime()const{return statusSlowTimer;}
    void SetRemoteStatus(float time,float factor){statusSlowTimer=std::clamp(time,0.0f,3.0f);slowFactor=std::clamp(factor,0.0f,1.0f);}
    bool IsFrozen()const{return statusSlowTimer>0 && slowFactor==0;}
    void SetEquipment(int melee=-1,int skin=0,int utility=-1) { bonusEquipped=false;reloadTimer=0;meleeVariant=std::clamp(melee,-1,1);meleeSkin=std::clamp(skin,0,3);utilityItem=std::clamp(utility,-1,2); }
    int GetMeleeVariant() const { return meleeVariant; }
    int GetMeleeSkin() const { return meleeSkin; }
    int GetUtilityItem() const { return utilityItem; }
    bool ConsumeMeleeHit() { bool hit=meleeHitPending;meleeHitPending=false;return hit; }
    void ApplyFlash(float seconds) { flashBlindTimer=std::max(flashBlindTimer,seconds); }
    float GetFlashBlind() const { return flashBlindTimer; }
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
    const CombatStats& GetCombatStats()const{return combatStats;}
    void ConfirmDamage(float damage,uint32_t shot=0){combatStats.Confirm(damage,shot);}
    void AddCapture(){++combatStats.captures;}
    void SetCaptures(int total){combatStats.captures=std::clamp(total,0,10000);}
    void SetRemoteStats(int shots,int hits,float damage){combatStats.shots=std::clamp(shots,0,1000000);combatStats.hits=std::clamp(hits,0,combatStats.shots);combatStats.damage=std::clamp(damage,0.0f,10000000.0f);}
    void ResetMatchStats(){kills=deaths=0;combatStats={};}
    float GetRecoil()const{return recoil;}
    float GetBodyLean()const{return bodyLean;}
    int GetLandingParticleCount()const{return (int)landingDust.size();}
    float GetJumpShadowRadius()const{return 22*(scale/.15f)*(1-.38f*std::clamp((jumpHeight-floorHeight)/160.0f,0.0f,1.0f));}
    void SetSurfaceStyle(int style){surfaceStyle=style;}
    void SetRemoteReload(bool active,float progress){remoteReload=true;reloadTimer=active?Weapons[currentWeaponIndex].reloadTime*(1-std::clamp(progress,0.0f,.999f)):0;}
    int GetCasingCount()const{return (int)casings.size();}
    float GetShootCooldown() const { return currentShootCooldown; }
    void SetShootCooldown(float val) { currentShootCooldown = val; }

    // Skills
    float GetDashCooldown() const { return dashCooldown; }
    float GetDashTimer() const { return dashTimer; }
    float GetShieldTimer() const { return shieldTimer; }
    float GetShieldCooldown() const { return shieldCooldown; }
    bool ActivateDash(Vector2 direction);
    bool ActivateShield();
    void UpdateSkills(float deltaTime, bool moveDash = true);

    // Weapons and Reloading
    int GetAmmo() const;
    int GetReserveAmmo() const;
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
        return position.y+feetOffset;
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
    void SpawnWeaponProjectiles(Vector2 muzzle,Vector2 direction);
    void ShootInDirection(Vector2 dir); // for remote players: uses normalized direction directly

    // Getters for networking
    int       GetWeaponIndex()  const { return currentWeaponIndex; }
    int GetWeaponSkin() const { return weaponSkinId; }
    CharState GetCurrentState() const { return currentState; }
    bool      DidShoot() { bool ret = justShot; justShot = false; return ret; }
    Vector2   GetHandPosition() const;
    WeaponPose GetBonusWeaponPose(Vector2 target)const;
    WeaponPose GetBonusWeaponPoseInDirection(Vector2 direction)const;
    WeaponPose GetWeaponPose(Vector2 target) const;
    WeaponPose GetWeaponPoseInDirection(Vector2 direction) const;
    Vector2 GetLastShotDirection() const { return lastShotDirection; }

protected:
    int bonusGun=-1,bonusAmmo=0,bonusPending=-1,bonusLastShot=-1,bonusThrow=-1,bonusCharges=0;
    bool bonusEquipped=false;
    float statusSlowTimer=0,slowFactor=1;
    CombatStats combatStats;
    float recoil=0,stepDistance=0;
    float bodyLean=0,pumpTimer=0,landingAge=1,airPeak=0;
    bool motionTracked=false,airborne=false,remoteReload=false;
    Vector2 motionPosition{};int surfaceStyle=0;
    struct LandingDust{Vector2 position,velocity;float age,lifetime,radius;};
    std::vector<LandingDust> landingDust;
    void UpdateMovementPresentation(float dt);
    void DrawGroundPresentation();
    Vector2 soundPosition{};bool soundTracked=false;float soundJump=0;
    struct Casing{Vector2 position,velocity;float age,spin;};
    std::vector<Casing> casings;
    void ShotFeedback(Vector2 direction);
    int meleeVariant=-1,meleeSkin=0,utilityItem=-1;
    float meleeSwing=0,meleeCooldown=0,flashBlindTimer=0;
    bool meleeHitPending=false;
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
    struct DashEcho { Vector2 position; float height, life; int facing; CharState state; };
    std::vector<DashEcho> dashEchoes;
    float dashEchoClock = 0.0f;
    float shieldAge = 0.0f;
    float shieldHitFlash = 0.0f;
    int        ammo[WeaponCount];
    int        reserveAmmo[WeaponCount];
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
