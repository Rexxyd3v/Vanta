#include "character.h"
#include "../utils/texture_manager.h"
#include <math.h>
#include <cmath>
#include <algorithm>
#include "../ui/game_settings.h"
#include <iostream>
#include "../ui/weapon_skin_catalog.h"
#include "../ui/equipment_art.h"
#include "bag_catalog.h"
#include "../audio/combat_audio.h"
#include "../ui/weapon_hands.h"
#include "../ui/weapon_reload.h"

// Static combat audio flag — disabled during menu background simulation
bool Character::combatAudioEnabled = true;
// Static debug-draw flag — when true, every Character::DrawDebugCollision()
// call renders the foot AABB and circle in colored outlines so you can see
// exactly where the collision shape is in the world.
bool Character::debugDrawCollision = false;


Character::Character(Vector2 startPosition, const std::string& assetPath, float scale, int weaponSkin)
    : position(startPosition), faceDirection(1), currentState(CharState::IDLE),
      scale(scale), health(100.0f), speed(150.0f), shootCooldown(0.15f), currentShootCooldown(0.0f),
      jumpHeight(0.0f), jumpVelocity(0.0f), baseHeight(0.0f), floorHeight(0.0f), feetOffset(10.0f), weaponRotation(0.0f),
      justShot(false), isMonster(false), kills(0), deaths(0), muzzleFlashTimer(0.0f),
      dashTimer(0.0f), dashCooldown(0.0f), dashDirection{0.0f, 0.0f}, shieldTimer(0.0f), shieldCooldown(0.0f),
      reloadTimer(0.0f), lastHitTimer(0.0f), healthDisplay(100.0f), godMode(false), teamID(0) {

    for (int i = 0; i < WeaponCount; ++i) {
        ammo[i] = Weapons[i].maxAmmo;
        reserveAmmo[i] = Weapons[i].reserveAmmo;
    }

    if (assetPath.find("Enemies/") != std::string::npos) {
        isMonster = true;
    }

    aimTarget = { startPosition.x + 100.0f, startPosition.y };
    velocity  = { 0.0f, 0.0f };
    LoadAnimations(assetPath);
    if(animations.count(CharState::IDLE)) feetOffset=animations[CharState::IDLE]->GroundFootOffset(scale);

    currentWeaponIndex = 0;
    weaponSkinId = weaponSkin;
    currentWeaponSkin = weaponSkin;

    // Every weapon uses its own default model and independently selected material.
    weaponTextures.clear();

    for(int slot=0;slot<WeaponCount;++slot) weaponTextures.push_back(LoadWeaponSlotTexture(slot,weaponSkinId));
}

Character::~Character() {
    for (auto& pair : animations) delete pair.second;
}

void Character::LoadAnimations(const std::string& baseDir) {
    std::string activeDir=baseDir;
    auto fixedHands=activeDir.find("/with hands/");
    if(!isMonster && fixedHands!=std::string::npos)activeDir.replace(fixedHands,12,"/no hands/");
    if (baseDir.find("Enemy 3") != std::string::npos) {
        animations[CharState::IDLE]        = new Animation(activeDir + "fly_",      6, 0.10f, true);
        animations[CharState::WALK]        = new Animation(activeDir + "fly_",      6, 0.08f, true);
        animations[CharState::JUMP_START]  = new Animation(activeDir + "fly_",      6, 0.10f, false);
        animations[CharState::JUMP_END]    = new Animation(activeDir + "fly_",      6, 0.10f, false);
        animations[CharState::FALL]        = new Animation(activeDir + "fly_",      6, 0.10f, true);
        animations[CharState::HIT]         = new Animation(activeDir + "fly_",      6, 0.10f, false);
        animations[CharState::DEATH]       = new Animation(activeDir + "fly_",      6, 0.10f, false);
    } else {
        animations[CharState::IDLE]        = new Animation(activeDir + "idle_",      6, 0.10f, true);
        animations[CharState::WALK]        = new Animation(activeDir + "walk_",      8, 0.08f, true);
        animations[CharState::JUMP_START]  = new Animation(activeDir + "jumpStart_", 2, 0.10f, false);
        animations[CharState::JUMP_END]    = new Animation(activeDir + "jumpEnd_",   3, 0.10f, false);
        animations[CharState::FALL]        = new Animation(activeDir + "fall_",      5, 0.10f, true);
        animations[CharState::HIT]         = new Animation(activeDir + "hit_",       3, 0.10f, false);
        animations[CharState::DEATH]       = new Animation(baseDir + "death_",    10, 0.10f, false);
    }
}

void Character::SetState(CharState newState) {
    if (currentState != newState) {
        currentState = newState;
        animations[currentState]->Reset();
    }
}

void Character::SetWeaponSkin(int weaponSkin) {
    // Clear existing weapon textures from local array
    weaponTextures.clear();

    weaponSkinId = weaponSkin;
    currentWeaponSkin = weaponSkin;

    for(int slot=0;slot<WeaponCount;++slot) weaponTextures.push_back(LoadWeaponSlotTexture(slot,weaponSkinId));

    // Reset to first weapon if the current weapon index is out of bounds
    if (currentWeaponIndex < 0 || currentWeaponIndex >= static_cast<int>(weaponTextures.size())) {
        currentWeaponIndex = 0;
    }
}

void Character::TakeDamage(float amount) {
    if (IsDead() || !std::isfinite(amount) || amount <= 0.0f) return;
    if (shieldTimer > 0.0f) { shieldHitFlash = 0.22f; return; }
    health -= amount;
    lastHitTimer = 3.0f; // show health bar for 3 seconds when hit
    if (health <= 0) {
        // God mode (used by the main-menu background): clamp to 1 HP instead of
        // dying so the simulated battle can run forever. The hit feedback above
        // still triggers so the player visually flinches and the HP bar drops.
        if (godMode) {
            health = 1.0f;
        } else {
            health = 0;
            dashTimer = shieldTimer = 0.0f; dashEchoes.clear();
            SetState(CharState::DEATH);
        }
    }
}

float Character::GetHealth() const { return health; }

void Character::SwitchWeapon(int index) {
    if (index >= 0 && index < (int)weaponTextures.size()) {
        bonusEquipped=false;
        currentWeaponIndex = index;recoil=0;
        meleeVariant=-1;utilityItem=-1;
        shootCooldown = Weapons[index].fireRate;
        reloadTimer = 0.0f; // cancel ongoing reload
    }
}

// World-space position of the character's weapon hand
Vector2 Character::GetHandPosition() const {
    float draw_y = position.y - jumpHeight;
    // Sprite is 2048x2048. Hand anchor at approx (1170, 1580) in the right-facing sprite.
    float x=(1170.0f-1024.0f)*scale*faceDirection,y=(1580.0f-1024.0f)*scale-feetOffset;
    float angle=bodyLean*DEG2RAD;
    return {position.x+x*std::cos(angle)-y*std::sin(angle),draw_y+feetOffset+x*std::sin(angle)+y*std::cos(angle)};
}

WeaponPose Character::GetBonusWeaponPose(Vector2 target)const{
    return AimWeapon(GetHandPosition(),target,faceDirection,Bonus::HeldScale(scale),Bonus::Geometry(bonusGun));
}
WeaponPose Character::GetBonusWeaponPoseInDirection(Vector2 direction)const{
    return PoseWeapon(GetHandPosition(),direction,faceDirection,Bonus::HeldScale(scale),Bonus::Geometry(bonusGun));
}
WeaponPose Character::GetWeaponPose(Vector2 target) const {
    Texture2D texture=weaponTextures.empty()?Texture2D{}:weaponTextures[std::clamp(currentWeaponIndex,0,(int)weaponTextures.size()-1)];
    auto geometry=GetWeaponGeometry(texture,currentWeaponIndex);
    WeaponPose pose=AimWeapon(GetHandPosition(),target,faceDirection,scale*0.45f*geometry.scale,geometry);
    float a=-recoil*faceDirection*DEG2RAD;Vector2 dir=pose.direction;
    Vector2 kicked{dir.x*std::cos(a)-dir.y*std::sin(a),dir.x*std::sin(a)+dir.y*std::cos(a)};
    return PoseWeapon(GetHandPosition(),kicked,faceDirection,scale*0.45f*geometry.scale,geometry);
}
WeaponPose Character::GetWeaponPoseInDirection(Vector2 direction) const {
    Texture2D texture=weaponTextures.empty()?Texture2D{}:weaponTextures[std::clamp(currentWeaponIndex,0,(int)weaponTextures.size()-1)];
    auto geometry=GetWeaponGeometry(texture,currentWeaponIndex);
    return PoseWeapon(GetHandPosition(),direction,faceDirection,scale*0.45f*geometry.scale,geometry);
}

// ---------------------------------------------------------------------------
// Foot-anchored collision shape.
//
// Animation::Draw renders the current frame centered on (position.x,
// position.y - jumpHeight) with origin = (tex.width*scale/2,
// tex.height*scale/2). The visible sprite therefore spans:
//     x: [position.x - sw/2, position.x + sw/2]
//     y: [draw_y       - sh/2, draw_y       + sh/2]
// where sw = tex.width*scale, sh = tex.height*scale.
//
// We want a *small* collision shape that hugs the feet — not a body-sized
// box. The full body of the character (head, arms, gun sticking out) is
// well over 100 px wide at the default scale; if the AABB covers all of
// that the character sticks to walls from far away and feels "magnetic".
//
// The box is sized in absolute world units (not as a fraction of the
// sprite) so it stays the same size regardless of how big the sprite is
// drawn. It is anchored to the *bottom* of the visible sprite (the feet),
// with a small height covering just the boots/lower legs.
// ---------------------------------------------------------------------------

// Half-width of the foot box (world units). 4 means an 8 px wide AABB
// straddling the sprite center — narrow enough to let the character walk
// close to walls without the visible body snagging, but wide enough to
// prevent ghosting though thin collision objects.
static const float FOOT_HALF_W = 4.0f;
// Height of the foot box (world units). 6 means a thin 6 px tall sliver
// right at the feet — covers just the boot/sole, nothing more.
static const float FOOT_HEIGHT = 6.0f;

Vector2 Character::GetCollisionSize() const {
    // Box is intentionally small and constant — it does not scale with
    // the sprite, so a big character still has the same foot footprint
    // as a small one. Callers can scale with `scale` if they need a
    // proportional footprint.
    float cw = FOOT_HALF_W * 2.0f;
    float ch = FOOT_HEIGHT;
    return { cw, ch };
}

Rectangle Character::GetCollisionBounds() const {
    Vector2 size=GetCollisionSize();
    // Horizontal movement uses the ground projection. Jump animation cannot move this footprint.
    return {position.x-size.x*0.5f,position.y+feetOffset-size.y,size.x,size.y};
}

Character::Circle Character::GetFeetCircle() const {
    Vector2 size=GetCollisionSize();
    float radius=std::min(size.x,size.y)*0.5f;
    return {{position.x,position.y+feetOffset-radius},radius};
}

// ---------------------------------------------------------------------------
// Debug draw — visualizes the foot AABB and circle so the collision shape
// can be inspected in-game. Toggle with Character::SetDebugDrawCollision().
//
// Layout:
//   * Yellow outline rectangle = the AABB from GetCollisionBounds().
//   * Green filled circle       = the circle from GetFeetCircle().
//   * Magenta dot               = the sprite center (position.x, draw_y).
//   * Red horizontal line       = the visible sprite's feet line.
//
// The line + dot are there so you can confirm the box really IS at the
// feet (not floating in the body) and that the box matches the visible
// sprite's footprint.
// ---------------------------------------------------------------------------
void Character::DrawDebugCollision() const {
    if (!debugDrawCollision) return;

    Rectangle box = GetCollisionBounds();
    Circle    foot = GetFeetCircle();
    float     draw_y = position.y - jumpHeight;

    // AABB outline (yellow, slightly thick for visibility)
    DrawRectangleLines((int)box.x, (int)box.y,
                       (int)box.width, (int)box.height, YELLOW);

    // Inner feet circle (green outline + faint green fill)
    DrawCircleV(foot.center, foot.radius, Fade(GREEN, 0.20f));
    DrawCircleLines((int)foot.center.x, (int)foot.center.y,
                    foot.radius, GREEN);

    // Sprite center crosshair (magenta)
    DrawLine((int)position.x - 6, (int)draw_y,
             (int)position.x + 6, (int)draw_y, MAGENTA);
    DrawLine((int)position.x, (int)draw_y - 6,
             (int)position.x, (int)draw_y + 6, MAGENTA);

    // Feet line (red horizontal across the visible bottom of the sprite)
    auto it = animations.find(currentState);
    if (it != animations.end() && it->second && it->second->HasFrames()) {
        float sh = (float)it->second->FrameHeight() * scale;
        float feetY = draw_y + sh * 0.5f;
        DrawLine((int)(position.x - sh * 0.5f), (int)feetY,
                 (int)(position.x + sh * 0.5f), (int)feetY,
                 Fade(RED, 0.6f));
    }
}

bool Character::Shoot(Vector2 targetPos) {
    if (IsDead() || utilityItem>=0) return false;
    if(BonusEquipped()) {
        if(currentShootCooldown>0 || GetFlashBlind()>0)return false;
        Vector2 hand=GetHandPosition();float dx=targetPos.x-hand.x,dy=targetPos.y-hand.y,d=std::hypot(dx,dy);if(d<1)return false;
        lastShotDirection=GetBonusWeaponPose(targetPos).direction;aimTarget=targetPos;bonusPending=bonusLastShot=bonusGun;--bonusAmmo;
        ++combatStats.shots;CombatAudio::Play(bonusGun==0?CombatAudio::Flame:CombatAudio::Throw,position);
        currentShootCooldown=Bonus::Cooldown(bonusGun);muzzleFlashTimer=.08f;justShot=true;return true;
    }
    bonusLastShot=-1;
    if(meleeVariant>=0) {
        if(meleeCooldown>0) return false;
        float dx=targetPos.x-position.x,dy=targetPos.y-position.y,length=std::hypot(dx,dy);
        if(length<1) return false;
        lastShotDirection={dx/length,dy/length};aimTarget=targetPos;
        meleeCooldown=Bags::MeleeCooldown(meleeVariant);meleeSwing=.28f;meleeHitPending=true;justShot=true;
        return true;
    }
    if (reloadTimer > 0.0f) return false; // cannot shoot while reloading
    if (ammo[currentWeaponIndex] <= 0) {
        TriggerReload(); // automatically reload if empty
        return false;
    }

    if (currentShootCooldown <= 0.0f) {
        currentShootCooldown = shootCooldown;
        ammo[currentWeaponIndex]--;

        Vector2 hand = GetHandPosition();
        float dx = targetPos.x - hand.x;
        float dy = targetPos.y - hand.y;
        float len = sqrtf(dx*dx + dy*dy);

        if (len > 1.0f) {
            WeaponPose pose = GetWeaponPose(targetPos);
            Vector2 dir = pose.direction;
            Vector2 projPos = {pose.muzzle.x+dir.x*2,pose.muzzle.y+dir.y*2};
            lastShotDirection = dir;
            aimTarget = targetPos;

            ++combatStats.shots;
            ShotFeedback(dir);
            SpawnWeaponProjectiles(projPos,dir);
            justShot = true;
            muzzleFlashTimer = 0.08f; // Show muzzle flash
            CombatAudio::Weapon(currentWeaponIndex,position); // play gun sound
            return true;
        }
    }
    return false;
}

void Character::SpawnWeaponProjectiles(Vector2 muzzle,Vector2 direction) {
    const auto& stats=Weapons[currentWeaponIndex];
    const float spread[]={-.105f,-.070f,-.035f,.035f,.070f,.105f,0};
    int count=currentWeaponIndex==6?7:1;
    for(int i=0;i<count;++i) {
        float angle=count==1?0:spread[i];
        Vector2 ray{direction.x*std::cos(angle)-direction.y*std::sin(angle),
                    direction.x*std::sin(angle)+direction.y*std::cos(angle)};
        projectiles.push_back(std::make_shared<Projectile>(muzzle,ray,stats.bulletSpeed,stats.bulletRange,stats.damage,
            isMonster?RED:YELLOW,isMonster?MAROON:ORANGE));
        projectiles.back()->SetWeaponSource(currentWeaponIndex,weaponSkinId);
        projectiles.back()->SetShotID((uint32_t)combatStats.shots);
    }
}

// Used by RemotePlayer: spawn a projectile using a pre-computed direction (no cooldown check)
void Character::ShootInDirection(Vector2 dir) {
    if (IsDead()) return;
    if(meleeVariant>=0) {
        meleeSwing=.28f;lastShotDirection=dir;aimTarget={position.x+dir.x*1000,position.y+dir.y*1000};return;
    }
    float length=std::sqrt(dir.x*dir.x+dir.y*dir.y);
    if (!std::isfinite(length) || length<0.001f) return;
    dir={dir.x/length,dir.y/length};
    faceDirection=dir.x<0?-1:1;
    WeaponPose pose=GetWeaponPoseInDirection(dir);
    Vector2 projPos{pose.muzzle.x+dir.x*2,pose.muzzle.y+dir.y*2};
    lastShotDirection=dir;
    aimTarget={pose.muzzle.x+dir.x*1000,pose.muzzle.y+dir.y*1000};

    ShotFeedback(dir);recoil=0; // Remote direction already includes the shooter's recoil.
    SpawnWeaponProjectiles(projPos,dir);
    muzzleFlashTimer = 0.08f; // Show muzzle flash
    CombatAudio::Weapon(currentWeaponIndex,position); // play gun sound for remote players
}

void Character::Draw() {
    float draw_y = position.y - jumpHeight;
    float alpha = GetDrawAlpha();
    Color tint = WHITE;
    tint.a = (unsigned char)(255.0f * alpha);
    const Color energy{88, 231, 255, 255};
    if (Settings::Get().effects && !IsDead()) {
        for (const auto& echo : dashEchoes) {
            float strength = echo.life / 0.20f;
            auto it = animations.find(echo.state);
            if (it != animations.end())
                it->second->Draw({echo.position.x, echo.position.y-echo.height}, echo.facing, scale, 0,
                                 Fade(energy, strength*0.32f*alpha));
            Vector2 center{echo.position.x, echo.position.y-echo.height+44};
            Vector2 normal{-dashDirection.y, dashDirection.x};
            for (int lane=-1; lane<=1; ++lane) {
                Vector2 tip{center.x+normal.x*lane*28, center.y+normal.y*lane*28};
                DrawLineEx({tip.x-dashDirection.x*30,tip.y-dashDirection.y*30},tip,
                           lane==0?2.5f:1.3f, Fade(energy,strength*0.65f*alpha));
            }
        }
    }
    // Split each orbit at its depth plane so energy travels behind and in front of the body.
    auto drawShieldOrbit = [&](bool front) {
        if (shieldTimer <= 0 || IsDead()) return;
        const bool effects=Settings::Get().effects;
        Vector2 center{position.x,draw_y+44};
        float unit=scale/0.15f;
        float opening=std::clamp(shieldAge/0.16f,0.0f,1.0f);
        float radius=77*unit*(0.65f+0.35f*opening);
        float fade=std::min(1.0f,shieldTimer/0.22f)*alpha;
        float impact=shieldHitFlash/0.22f;
        for (int orbit=0;orbit<2;++orbit) {
            float tilt=(orbit==0 ? -42.0f : 48.0f)*DEG2RAD;
            float phase=shieldAge*(orbit==0 ? 390.0f : -335.0f)+orbit*180;
            Color color=orbit==0 ? Color{74,232,255,255} : Color{142,153,255,255};
            auto point=[&](float degrees,float expansion=1.0f) {
                float a=degrees*DEG2RAD;
                float x=cosf(a)*radius*expansion, y=sinf(a)*radius*0.43f*expansion;
                return Vector2{center.x+x*cosf(tilt)-y*sinf(tilt),
                               center.y+x*sinf(tilt)+y*cosf(tilt)};
            };
            // Three separated comet arcs on each orbit; luminous heads keep the rotation readable.
            for (int arc=0;arc<3;++arc) {
                float head=phase+arc*120;
                for (int step=0;step<22;++step) {
                    float a=head-(22-step)*3.3f;
                    if ((sinf((a+1.65f)*DEG2RAD)>=0)!=front) continue;
                    float strength=(step+1)/22.0f;
                    float opacity=(0.12f+0.73f*strength)*fade*(front?1.0f:0.38f);
                    Vector2 from=point(a),to=point(a+3.3f);
                    if (effects) DrawLineEx(from,to,(8+impact*5)*unit,
                                           Fade(color,opacity*0.12f));
                    DrawLineEx(from,to,(1+strength*2.8f+impact*2)*unit,Fade(color,opacity));
                    if (effects && step>12) DrawLineEx(from,to,1.1f*unit,
                                                       Fade(WHITE,strength*0.65f*opacity));
                }
                if ((sinf(head*DEG2RAD)>=0)==front) {
                    Vector2 core=point(head);
                    if (effects) DrawCircleGradient(core,10*unit,Fade(color,fade*0.5f),Fade(color,0));
                    DrawCircleV(core,2.3f*unit,Fade(WHITE,fade*(front?1.0f:0.4f)));
                }
                if (effects) for (int spark=0;spark<3;++spark) {
                    float a=head-15-spark*23;
                    if ((sinf(a*DEG2RAD)>=0)!=front) continue;
                    Vector2 pos=point(a,1.07f+0.035f*spark);
                    DrawCircleV(pos,(1.5f-spark*0.3f)*unit,
                                Fade(color,fade*(0.65f-spark*0.16f)*(front?1.0f:0.35f)));
                }
            }
            // Energy ripples race outward on activation and whenever the shield absorbs a hit.
            float wave=impact>0?1-impact:std::clamp(shieldAge/0.35f,0.0f,1.0f);
            if (effects && wave<1) for(int step=0;step<72;++step) {
                float a=step*5.0f;
                if ((sinf((a+2.5f)*DEG2RAD)>=0)!=front) continue;
                DrawLineEx(point(a,1+wave*0.30f),point(a+5,1+wave*0.30f),1.5f*unit,
                           Fade(WHITE,(1-wave)*0.45f*fade));
            }
        }
    };
    DrawGroundPresentation();
    drawShieldOrbit(false);
    animations[currentState]->Draw({ position.x, draw_y }, faceDirection, scale, IsDead()?0:bodyLean, tint,feetOffset);
    // Update muzzle flash timer moved to Update()

    if (!isMonster && !IsDead() && meleeVariant<0 && utilityItem<0 && !BonusEquipped()) {
        // Always try to draw a weapon. If the selected weapon skin texture failed to load,
        // fallback to the default skin so the gun is never invisible.
        Texture2D currentTex = {0};
        bool haveGun = false;

        if (!weaponTextures.empty() && currentWeaponIndex >= 0 && currentWeaponIndex < (int)weaponTextures.size()) {
            currentTex = weaponTextures[currentWeaponIndex];
            haveGun = (currentTex.id != 0);
        }

        if (!haveGun) {
            currentTex=LoadWeaponSlotTexture(currentWeaponIndex,0);
            haveGun = (currentTex.id != 0);
        }

        if (haveGun && currentTex.id != 0) {
            float weaponScale = scale * 0.45f * GetWeaponGeometry(currentTex,currentWeaponIndex).scale;
            Vector2 hand = GetHandPosition();

            WeaponPose pose=GetWeaponPoseInDirection(lastShotDirection);
            if(muzzleFlashTimer<=0)pose=GetWeaponPose(aimTarget);
            auto hold=GetWeaponHold(currentTex,currentWeaponIndex,pose,weaponScale,faceDirection,IsReloading()?GetReloadProgress():-1,pumpTimer);
            DrawWeaponHands(hold,13*scale/.15f,alpha,false);
            WeaponReload::Draw(currentTex,currentWeaponIndex,weaponSkinId,pose,weaponScale,faceDirection,IsReloading()?GetReloadProgress():-1,pumpTimer,alpha);
            DrawWeaponHands(hold,13*scale/.15f,alpha,true);

            if (muzzleFlashTimer > 0.0f) {
                Vector2 start=pose.muzzle, dir=pose.direction, normal{-dir.y,dir.x};
                float pulse=std::clamp(muzzleFlashTimer/0.08f,0.0f,1.0f);
                float length=((currentWeaponIndex==1 || currentWeaponIndex==6)?42.0f:currentWeaponIndex==2?25.0f:32.0f)*(0.65f+0.35f*pulse);
                auto point=[&](float along,float across) { return Vector2{start.x+dir.x*along+normal.x*across,start.y+dir.y*along+normal.y*across}; };
                if (Settings::Get().effects) DrawCircleGradient(point(5,0),18,Fade(ORANGE,0.35f*alpha*pulse),Fade(ORANGE,0));
                // Flame starts at the barrel opening and points along the exact shot axis.
                DrawLineEx(point(0,0),point(length*0.68f,0),8*alpha,Fade(ORANGE,alpha));
                DrawLineEx(point(1,0),point(length,0),3.5f,Fade(Color{255,240,166,255},alpha));
                DrawLineEx(point(2,0),point(length*0.6f,0),2,Fade(WHITE,alpha));
                DrawLineEx(point(6,0),point(length*0.36f,9*pulse),3,Fade(GOLD,alpha*pulse));
                DrawLineEx(point(6,0),point(length*0.36f,-9*pulse),3,Fade(GOLD,alpha*pulse));
            }
        }
    }

    if(!isMonster && !IsDead() && (meleeVariant>=0 || utilityItem>=0)){
        Vector2 hand=GetHandPosition();float angle=std::atan2(aimTarget.y-hand.y,aimTarget.x-hand.x)*RAD2DEG;
        Vector2 contact=hand;if(utilityItem>=0)contact.y+=8;
        DrawEquipmentHand({position.x-faceDirection*20*scale/.15f,draw_y+90*scale/.15f},90,12*scale/.15f,alpha,false);
        if(meleeVariant>=0 && meleeSwing>0)angle+=(1-meleeSwing/.28f)*130-65;
        DrawEquipmentHand(contact,angle,13*scale/.15f,alpha,false);
        DrawHeldEquipment(hand,aimTarget,faceDirection,meleeVariant,meleeSkin,utilityItem,meleeSwing/.28f);
        DrawEquipmentHand(contact,angle,13*scale/.15f,alpha,true);
    }
    if(!IsDead() && BonusEquipped()) {
        WeaponPose pose=GetBonusWeaponPose(aimTarget);float unit=scale/.15f,fit=Bonus::HeldScale(scale);auto g=Bonus::Geometry(bonusGun);
        Vector2 normal{-pose.direction.y,pose.direction.x};
        Vector2 center{pose.hand.x+pose.direction.x*(52-g.pivot.x)*fit+normal.x*(30-g.pivot.y)*fit*faceDirection,
                       pose.hand.y+pose.direction.y*(52-g.pivot.x)*fit+normal.y*(30-g.pivot.y)*fit*faceDirection};
        auto hold=GetBonusHold(bonusGun,pose,fit,faceDirection);DrawWeaponHands(hold,13*unit,alpha,false);
        DrawBonusArt(bonusGun,{center.x-42*unit,center.y-22*unit,84*unit,44*unit},pose.rotation,alpha,faceDirection<0);
        DrawWeaponHands(hold,13*unit,alpha,true);
        if(muzzleFlashTimer>0)DrawCircleGradient(pose.muzzle,15,Fade(ORANGE,muzzleFlashTimer*7),BLANK);
    }
    if(!IsDead() && IsFrozen())DrawBonusIce({position.x,draw_y+55},statusSlowTimer);
    else if(!IsDead() && GetSlowTime()>0){Vector2 p{position.x,draw_y+40};DrawEllipseLines((int)p.x,(int)p.y,50,64,Fade(Bonus::Tint(4),.8f));}
    drawShieldOrbit(true);
    if(Settings::Get().effects)for(const auto& casing:casings){
        float fade=std::min(1.0f,(1.6f-casing.age)*3);DrawRectanglePro({casing.position.x,casing.position.y,5,2.5f},{2.5f,1.25f},casing.spin+casing.age*540,Fade({213,178,98,255},fade));
    }

    for (auto& proj : projectiles) {
        proj->Draw();
    }

    // Debug visualization of the collision shape — no-op unless the
    // global flag is set via Character::SetDebugDrawCollision(true).
    DrawDebugCollision();
}


void Character::DrawUI() {
    float draw_y = position.y - jumpHeight;
    float alpha = GetDrawAlpha();

    // Overhead hit indicator health bar (with container border)
    {
        const float barWidth = 60.0f;
        const float barHeight = 6.0f;
        // Positioning: IGN is drawn later at nameY = draw_y - nameYOffset.
        // Keep health bar just BELOW the IGN pill (not too high so it won't cover the head).
        float nameYOffset = 800.0f * scale;
        const int nameFontSize = 16;
        const int namePadding  = 4;
        const float pillHeight = (float)nameFontSize + (float)(2 * namePadding);

        // Start the bar right after the pill bottom, with a smaller gap so it doesn't go up into the head.
        // Add a scale-aware extra offset so it stays under the head and doesn't overlap with the character sprite.
        float barY = (draw_y - nameYOffset) + pillHeight + (30.0f * scale);
        Vector2 barPos = { position.x - barWidth / 2.0f, barY };
        // Background
        Color bg = {60, 60, 60, (unsigned char)(200.0f * alpha)};
        DrawRectangle((int)barPos.x, (int)barPos.y, (int)barWidth, (int)barHeight, bg);
        // Health fill (smoothed)
        float healthFill = barWidth * (healthDisplay / 100.0f);
        if (healthFill > 0.0f) {
            Color healthColor = isMonster ? RED : GREEN;
            healthColor.a = (unsigned char)(healthColor.a * alpha);
            DrawRectangle((int)barPos.x, (int)barPos.y, (int)healthFill, (int)barHeight, healthColor);
        }
        // Outline
        Color outline = {200, 200, 200, (unsigned char)(230.0f * alpha)};
        DrawRectangleLines((int)barPos.x, (int)barPos.y, (int)barWidth, (int)barHeight, outline);
    }

    // ---- Player name (IGN) floating above the head ----
    if (!name.empty()) {
        const int nameFontSize = 16;
        const int namePadding  = 4;
        int textWidth  = MeasureText(name.c_str(), nameFontSize);
        int textHeight = nameFontSize;
        // Anchor: above the head sprite. draw_y is the visual center of the character.
        // Animations are ~120-140px tall, so place the text well above.
        // Move label closer to the character's head. Make it scale-aware so it stays aligned across character sizes.
        float nameYOffset = 800.0f * scale;
        float nameY = draw_y - nameYOffset;
        float nameX = position.x - textWidth / 2.0f;
        // Background pill for readability against any ground.
        // Team-tinted (subtle) so players can tell teams apart at a glance.
        Color bg = {0, 0, 0, (unsigned char)(160.0f * alpha)};
        if (teamID == 1)      bg = { 30,  60, 120, (unsigned char)(180.0f * alpha) };
        else if (teamID == 2) bg = { 120, 30,  30, (unsigned char)(180.0f * alpha) };
        DrawRectangle((int)nameX - namePadding, (int)nameY - namePadding,
                      textWidth + namePadding * 2, textHeight + namePadding * 2,
                      bg);
        // Name text color:
        //   monsters           -> red
        //   GR (teamID == 1)   -> blue
        //   BL (teamID == 2)   -> red
        //   otherwise          -> white
        Color nameColor = RAYWHITE;
        if (isMonster)            nameColor = Color{ 255, 100, 100, 255 };
        else if (teamID == 1)     nameColor = Color{ 180, 220, 255, 255 };
        else if (teamID == 2)     nameColor = Color{ 255, 180, 180, 255 };
        nameColor.a = (unsigned char)(nameColor.a * alpha);
        DrawText(name.c_str(), (int)nameX, (int)nameY, nameFontSize, nameColor);
    }
}

bool Character::IsAnimationFinished() const {
    auto it = animations.find(currentState);
    return (it != animations.end()) ? it->second->IsFinished() : true;
}

void Character::UpdatePhysics(float deltaTime) {
    if(IsFrozen())return;
    // Apply vertical physics using floorHeight as the landing level
    if (jumpHeight > floorHeight || jumpVelocity > 0.0f) {
        jumpVelocity -= 950.0f * deltaTime; // gravity
        jumpHeight   += jumpVelocity * deltaTime;
        if (jumpHeight <= floorHeight) {
            jumpHeight   = floorHeight;
            jumpVelocity = 0.0f;
        }
    } else if (jumpHeight < floorHeight) {
        // Floor was raised (landed on platform) — snap up
        jumpHeight   = floorHeight;
        jumpVelocity = 0.0f;
    }
}

bool Character::ActivateDash(Vector2 direction) {
    if (!IsDead() && GetSlowFactor()>.4f && dashCooldown <= 0.0f && dashTimer <= 0.0f) {
        dashTimer = 0.22f;
        dashEchoClock = 0.0f;
        dashEchoes.clear();
        dashCooldown = 2.0f;
        float len = sqrtf(direction.x * direction.x + direction.y * direction.y);
        if (len > 0.01f) {
            dashDirection = { direction.x / len, direction.y / len };
        } else {
            dashDirection = { (float)faceDirection, 0.0f };
        }
        return true;
    }
    return false;
}

bool Character::ActivateShield() {
    if (!IsDead() && shieldCooldown <= 0.0f && shieldTimer <= 0.0f) {
        shieldTimer = 2.0f;
        shieldAge = shieldHitFlash = 0.0f;
        shieldCooldown = 6.0f;
        return true;
    }
    return false;
}

void Character::UpdateSkills(float deltaTime, bool moveDash) {
    if(std::isfinite(deltaTime) && deltaTime>0){
        UpdateMovementPresentation(deltaTime);
        pumpTimer=std::max(0.0f,pumpTimer-deltaTime);
        recoil=std::max(0.0f,recoil-GunFeel[currentWeaponIndex].recovery*deltaTime);
        if(soundTracked && !IsDead()){
            float travel=std::hypot(position.x-soundPosition.x,position.y-soundPosition.y);
            if(travel<100 && jumpHeight<=floorHeight+2 && !IsFrozen())stepDistance+=travel;
            if(stepDistance>=52){stepDistance=std::fmod(stepDistance,52.0f);CombatAudio::Play(CombatAudio::Step,position,.5f);}

        }
        soundTracked=true;soundPosition=position;soundJump=jumpHeight;
        for(auto& c:casings){c.age+=deltaTime;if(c.age<.42f){c.position.x+=c.velocity.x*deltaTime;c.position.y+=c.velocity.y*deltaTime;c.velocity.y+=400*deltaTime;}}
        casings.erase(std::remove_if(casings.begin(),casings.end(),[](const Casing& c){return c.age>1.6f;}),casings.end());
    }
    statusSlowTimer=std::max(0.0f,statusSlowTimer-deltaTime);
    meleeSwing=std::max(0.0f,meleeSwing-deltaTime);meleeCooldown=std::max(0.0f,meleeCooldown-deltaTime);
    flashBlindTimer=std::max(0.0f,flashBlindTimer-deltaTime);
    if (!std::isfinite(deltaTime) || deltaTime <= 0) return;
    muzzleFlashTimer = std::max(0.0f,muzzleFlashTimer-deltaTime);
    for (auto& echo : dashEchoes) echo.life -= deltaTime;
    dashEchoes.erase(std::remove_if(dashEchoes.begin(),dashEchoes.end(),
        [](const DashEcho& echo) { return echo.life <= 0; }),dashEchoes.end());
    if (dashTimer > 0.0f && !IsDead()) {
        float travelTime=std::min(deltaTime,dashTimer);
        dashEchoClock -= deltaTime;
        if (dashEchoClock <= 0 && Settings::Get().effects) {
            dashEchoes.push_back({position,jumpHeight,0.20f,faceDirection,currentState});
            if (dashEchoes.size()>8) dashEchoes.erase(dashEchoes.begin());
            dashEchoClock=0.025f;
        }
        if (moveDash) {
            position.x += dashDirection.x * speed * GetSlowFactor() * 3.0f * travelTime;
            position.y += dashDirection.y * speed * GetSlowFactor() * 3.0f * travelTime;
        }
    }
    dashTimer = std::max(0.0f,dashTimer-deltaTime);
    dashCooldown = std::max(0.0f,dashCooldown-deltaTime);
    if (shieldTimer>0) shieldAge += deltaTime;
    shieldHitFlash = std::max(0.0f,shieldHitFlash-deltaTime);
    shieldTimer = std::max(0.0f,shieldTimer-deltaTime);
    shieldCooldown = std::max(0.0f,shieldCooldown-deltaTime);

    // Reloading timer tick
    if (reloadTimer > 0.0f) {
        reloadTimer -= deltaTime;
        if (reloadTimer <= 0.0f) {
            if(!remoteReload){const int rounds=std::min(Weapons[currentWeaponIndex].maxAmmo-ammo[currentWeaponIndex],reserveAmmo[currentWeaponIndex]);
                ammo[currentWeaponIndex]+=rounds;reserveAmmo[currentWeaponIndex]-=rounds;CombatAudio::Play(CombatAudio::Ready,position,.6f);}
            if(currentWeaponIndex==1)pumpTimer=.32f;
        }
    }

    // Smooth health display for overhead bar
    if (healthDisplay != health) {
        float lerpSpeed = 5.0f; // higher = faster catch-up
        healthDisplay += (health - healthDisplay) * deltaTime * lerpSpeed;
        // Clamp
        if (healthDisplay < 0.0f) healthDisplay = 0.0f;
        if (healthDisplay > 100.0f) healthDisplay = 100.0f;
    }

    // Overhead hit indicator timer tick
    if (lastHitTimer > 0.0f) lastHitTimer -= deltaTime;
}

int Character::GetAmmo() const {
    return ammo[currentWeaponIndex];
}

int Character::GetReserveAmmo() const {
    return reserveAmmo[currentWeaponIndex];
}

int Character::GetMaxAmmo() const {
    return Weapons[currentWeaponIndex].maxAmmo;
}

bool Character::IsReloading() const {
    return reloadTimer > 0.0f;
}

void Character::RefillAmmo() {
    for (int i = 0; i < WeaponCount; ++i) {
        ammo[i] = Weapons[i].maxAmmo;
        reserveAmmo[i] = Weapons[i].reserveAmmo;
    }
}

float Character::GetReloadProgress() const {
    return IsReloading() ? 1.0f - reloadTimer / Weapons[currentWeaponIndex].reloadTime : 1.0f;
}

void Character::TriggerReload() {
    if (!bonusEquipped && meleeVariant < 0 && utilityItem < 0 && reloadTimer <= 0.0f && reserveAmmo[currentWeaponIndex] > 0 && ammo[currentWeaponIndex] < Weapons[currentWeaponIndex].maxAmmo) {
        remoteReload=false;
        reloadTimer = Weapons[currentWeaponIndex].reloadTime;
        CombatAudio::Play(CombatAudio::Reload,position,.65f);
    }
}

void Character::ShotFeedback(Vector2 direction){
    if(currentWeaponIndex==1)pumpTimer=.32f;
    const auto& feel=GunFeel[currentWeaponIndex];recoil=std::min(feel.maximum,recoil+feel.kick);
    if(Settings::Get().effects && currentWeaponIndex!=6){Vector2 hand=GetHandPosition();Vector2 side{-direction.y,direction.x};
        if(casings.size()>=24)casings.erase(casings.begin());
        casings.push_back({{hand.x+direction.x*9,hand.y+direction.y*9},{side.x*75+direction.x*20,-95+side.y*35},0,(float)GetRandomValue(0,180)});
    }
}

void Character::UpdateMovementPresentation(float dt){
    float leanTarget=0;
    if(motionTracked && !IsDead() && !IsFrozen()){
        float dx=position.x-motionPosition.x,dy=position.y-motionPosition.y;
        if(std::hypot(dx,dy)<100)leanTarget=std::clamp(dx/dt/150.0f,-1.0f,1.0f)*(dashTimer>0?7:4.5f);
    }
    bodyLean+=(leanTarget-bodyLean)*(1-std::exp(-dt*13));motionPosition=position;motionTracked=true;
    landingAge+=dt;
    for(auto& p:landingDust){p.age+=dt;p.position.x+=p.velocity.x*dt;p.position.y+=p.velocity.y*dt;p.velocity.x*=std::exp(-dt*4);p.velocity.y*=std::exp(-dt*4);}
    landingDust.erase(std::remove_if(landingDust.begin(),landingDust.end(),[](const LandingDust& p){return p.age>=p.lifetime;}),landingDust.end());
    float height=std::max(0.0f,jumpHeight-floorHeight);
    if(!IsDead() && height>1.5f){airborne=true;airPeak=std::max(airPeak,height);}
    else if(airborne && !IsFrozen()){
        airborne=false;landingAge=0;CombatAudio::Play(CombatAudio::Land,position,.4f+std::min(airPeak/120,.45f));
        if(Settings::Get().effects && airPeak>8){
            float strength=std::clamp(airPeak/70,.5f,1.5f),unit=scale/.15f;
            for(int i=0;i<10;++i){float a=i*2.39996f;float speed=(22+(i*19)%37)*strength;
                landingDust.push_back({{position.x+std::cos(a)*5*unit,position.y+feetOffset-floorHeight+std::sin(a)*3*unit},{std::cos(a)*speed*unit,std::sin(a)*speed*.32f*unit},0,.42f+(i%3)*.1f,(5.0f+(i%4)*1.2f)*unit});}
        }airPeak=0;
    }
}
void Character::DrawGroundPresentation(){
    if(isMonster || IsDead())return;
    Vector2 feet{position.x,position.y+feetOffset-floorHeight};float height=std::max(0.0f,jumpHeight-floorHeight);
    float opacity=(.30f-.13f*std::min(1.0f,height/150))*GetDrawAlpha();float radius=GetJumpShadowRadius();
    DrawEllipse((int)feet.x,(int)feet.y,radius,radius*.30f,Fade(BLACK,opacity));
    DrawEllipse((int)feet.x,(int)feet.y,radius*.65f,radius*.19f,Fade(BLACK,opacity*.4f));
    if(!Settings::Get().effects)return;
    Color dust=surfaceStyle==1?Color{225,191,140,255}:surfaceStyle==2?Color{149,159,164,255}:Color{148,143,138,255};
    for(const auto& p:landingDust){float fade=1-p.age/p.lifetime;float size=p.radius*(1+p.age*1.8f);
        DrawCircleGradient(p.position,size,Fade(dust,fade*.55f),BLANK);
        if(p.age<.2f)DrawLineEx(p.position,{p.position.x-p.velocity.x*.025f,p.position.y-p.velocity.y*.025f},1.5f,Fade(dust,fade*.65f));}
    if(landingAge<.24f){float spread=landingAge/.24f;DrawEllipseLines((int)feet.x,(int)feet.y,17+spread*25,5+spread*8,Fade(dust,(1-spread)*.28f));}
}

// Presentation-only pose: render original idle art and the equipped gun without
// touching life state, animations, ammo, collision, or network snapshots.
void Character::DrawVictoryPose(Vector2 center,float size,int facing){
    auto idle=animations.find(CharState::IDLE);if(idle==animations.end())return;
    Vector2 savedPosition=position;float savedScale=scale,savedJump=jumpHeight,savedLean=bodyLean;
    int savedFacing=faceDirection;
    center.y-=idle->second->GroundFootOffset(size);
    position=center;scale=size;jumpHeight=0;bodyLean=0;faceDirection=facing;
    idle->second->Draw(center,facing,size,0,WHITE,feetOffset);
    Texture2D tex=LoadWeaponSlotTexture(currentWeaponIndex,weaponSkinId);
    if(tex.id){float fit=size*.45f*GetWeaponGeometry(tex,currentWeaponIndex).scale;
        auto pose=GetWeaponPoseInDirection({facing*.97f,-.24f});
        auto hold=GetWeaponHold(tex,currentWeaponIndex,pose,fit,facing);
        DrawWeaponHands(hold,13*size/.15f,1,false);
        WeaponReload::Draw(tex,currentWeaponIndex,weaponSkinId,pose,fit,facing,-1,0,1);
        DrawWeaponHands(hold,13*size/.15f,1,true);
    }
    position=savedPosition;scale=savedScale;jumpHeight=savedJump;bodyLean=savedLean;faceDirection=savedFacing;
}
