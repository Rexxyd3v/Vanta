#include "character.h"
#include "../utils/texture_manager.h"
#include <math.h>
#include <cmath>
#include <algorithm>
#include "../ui/game_settings.h"
#include <iostream>
#include "../ui/weapon_skin_catalog.h"

// Static combat audio flag — disabled during menu background simulation
bool Character::combatAudioEnabled = true;
// Static debug-draw flag — when true, every Character::DrawDebugCollision()
// call renders the foot AABB and circle in colored outlines so you can see
// exactly where the collision shape is in the world.
bool Character::debugDrawCollision = false;

static void PlayWeaponSound(int index) {
    static Sound sounds[WeaponCount] = { {0}, {0}, {0} };
    static bool loaded[WeaponCount] = { false, false, false };

    // Respect the global combat audio flag (muted during menu background)
    if (!Character::IsCombatAudioEnabled()) return;
    
    std::string paths[WeaponCount] = {
        "assets/sounds/weapon1.mp3", // Weapon 1 (SMG)
        "assets/sounds/weapon3.mp3", // Weapon 2 (Shotgun)
        "assets/sounds/weapon2.mp3", // Pistol
        "assets/sounds/weapon1.mp3", // Assault rifle
        "assets/sounds/weapon2.mp3" // Sniper
    };
    
    if (index >= 0 && index < WeaponCount) {
        if (!loaded[index]) {
            Wave wave = LoadWave(paths[index].c_str());
            if (IsWaveValid(wave)) {
                // Convert wave format to 16-bit mono for easy silence detection
                WaveFormat(&wave, wave.sampleRate, 16, 1);
                short* samples = (short*)wave.data;
                unsigned int firstActiveFrame = 0;
                int threshold = 150; // silence threshold
                for (unsigned int i = 0; i < wave.frameCount; ++i) {
                    short val = samples[i];
                    if ((val < 0 ? -val : val) > threshold) {
                        firstActiveFrame = i;
                        break;
                    }
                }
                if (firstActiveFrame > 0) {
                    WaveCrop(&wave, firstActiveFrame, wave.frameCount - 1);
                }
                sounds[index] = LoadSoundFromWave(wave);
                UnloadWave(wave);
            } else {
                sounds[index] = LoadSound(paths[index].c_str());
            }
            loaded[index] = true;
        }
        if (IsSoundValid(sounds[index])) {
            SetSoundVolume(sounds[index], Settings::Get().sfx);
            PlaySound(sounds[index]);
        }
    }
}

Character::Character(Vector2 startPosition, const std::string& assetPath, float scale, int weaponSkin)
    : position(startPosition), faceDirection(1), currentState(CharState::IDLE),
      scale(scale), health(100.0f), speed(150.0f), shootCooldown(0.15f), currentShootCooldown(0.0f),
      jumpHeight(0.0f), jumpVelocity(0.0f), baseHeight(0.0f), floorHeight(0.0f), feetOffset(10.0f), weaponRotation(0.0f),
      justShot(false), isMonster(false), kills(0), deaths(0), muzzleFlashTimer(0.0f),
      dashTimer(0.0f), dashCooldown(0.0f), dashDirection{0.0f, 0.0f}, shieldTimer(0.0f), shieldCooldown(0.0f),
      reloadTimer(0.0f), lastHitTimer(0.0f), healthDisplay(100.0f), godMode(false), teamID(0) {

    for (int i = 0; i < WeaponCount; ++i) {
        ammo[i] = Weapons[i].maxAmmo;
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
    if (baseDir.find("Enemy 3") != std::string::npos) {
        animations[CharState::IDLE]        = new Animation(baseDir + "fly_",      6, 0.10f, true);
        animations[CharState::WALK]        = new Animation(baseDir + "fly_",      6, 0.08f, true);
        animations[CharState::JUMP_START]  = new Animation(baseDir + "fly_",      6, 0.10f, false);
        animations[CharState::JUMP_END]    = new Animation(baseDir + "fly_",      6, 0.10f, false);
        animations[CharState::FALL]        = new Animation(baseDir + "fly_",      6, 0.10f, true);
        animations[CharState::HIT]         = new Animation(baseDir + "fly_",      6, 0.10f, false);
        animations[CharState::DEATH]       = new Animation(baseDir + "fly_",      6, 0.10f, false);
    } else {
        animations[CharState::IDLE]        = new Animation(baseDir + "idle_",      6, 0.10f, true);
        animations[CharState::WALK]        = new Animation(baseDir + "walk_",      8, 0.08f, true);
        animations[CharState::JUMP_START]  = new Animation(baseDir + "jumpStart_", 2, 0.10f, false);
        animations[CharState::JUMP_END]    = new Animation(baseDir + "jumpEnd_",   3, 0.10f, false);
        animations[CharState::FALL]        = new Animation(baseDir + "fall_",      5, 0.10f, true);
        animations[CharState::HIT]         = new Animation(baseDir + "hit_",       3, 0.10f, false);
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
        currentWeaponIndex = index;
        shootCooldown = Weapons[index].fireRate;
        reloadTimer = 0.0f; // cancel ongoing reload
    }
}

// World-space position of the character's weapon hand
Vector2 Character::GetHandPosition() const {
    float draw_y = position.y - jumpHeight;
    // Sprite is 2048x2048. Hand anchor at approx (1170, 1580) in the right-facing sprite.
    float hand_x = position.x + (1170.0f - 1024.0f) * scale * faceDirection;
    float hand_y = draw_y      + (1580.0f - 1024.0f) * scale;
    return { hand_x, hand_y };
}

WeaponPose Character::GetWeaponPose(Vector2 target) const {
    Texture2D texture=weaponTextures.empty()?Texture2D{}:weaponTextures[std::clamp(currentWeaponIndex,0,(int)weaponTextures.size()-1)];
    auto geometry=GetWeaponGeometry(texture,currentWeaponIndex);
    return AimWeapon(GetHandPosition(),target,faceDirection,scale*0.45f*geometry.scale,geometry);
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
    if (IsDead()) return false;
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

            float bSpeed = Weapons[currentWeaponIndex].bulletSpeed;
            float bRange = Weapons[currentWeaponIndex].bulletRange;
            float bDamage = Weapons[currentWeaponIndex].damage;

            if (isMonster) {
                projectiles.push_back(std::make_shared<Projectile>(projPos, dir, bSpeed, bRange, bDamage, RED, MAROON));
            } else {
                projectiles.push_back(std::make_shared<Projectile>(projPos, dir, bSpeed, bRange, bDamage));
            }
            justShot = true;
            projectiles.back()->SetWeaponSource(currentWeaponIndex,weaponSkinId);
            muzzleFlashTimer = 0.08f; // Show muzzle flash
            PlayWeaponSound(currentWeaponIndex); // play gun sound
            return true;
        }
    }
    return false;
}

// Used by RemotePlayer: spawn a projectile using a pre-computed direction (no cooldown check)
void Character::ShootInDirection(Vector2 dir) {
    if (IsDead()) return;
    float length=std::sqrt(dir.x*dir.x+dir.y*dir.y);
    if (!std::isfinite(length) || length<0.001f) return;
    dir={dir.x/length,dir.y/length};
    faceDirection=dir.x<0?-1:1;
    WeaponPose pose=GetWeaponPoseInDirection(dir);
    Vector2 projPos{pose.muzzle.x+dir.x*2,pose.muzzle.y+dir.y*2};
    lastShotDirection=dir;
    aimTarget={pose.muzzle.x+dir.x*1000,pose.muzzle.y+dir.y*1000};

    float bSpeed = Weapons[currentWeaponIndex].bulletSpeed;
    float bRange = Weapons[currentWeaponIndex].bulletRange;
    float bDamage = Weapons[currentWeaponIndex].damage;

    projectiles.push_back(std::make_shared<Projectile>(projPos, dir, bSpeed, bRange, bDamage));
    projectiles.back()->SetWeaponSource(currentWeaponIndex,weaponSkinId);
    muzzleFlashTimer = 0.08f; // Show muzzle flash
    PlayWeaponSound(currentWeaponIndex); // play gun sound for remote players
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
    drawShieldOrbit(false);
    animations[currentState]->Draw({ position.x, draw_y }, faceDirection, scale, 0.0f, tint);
    // Update muzzle flash timer moved to Update()

    if (!isMonster && !IsDead()) {
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

            WeaponPose pose=GetWeaponPose(aimTarget);
            float rotation=pose.rotation;

            Rectangle srcRec = { 0.0f, 0.0f,
                                  (float)currentTex.width  * faceDirection,
                                  (float)currentTex.height };
            Rectangle dstRec = { hand.x, hand.y,
                                  (float)currentTex.width  * weaponScale,
                                  (float)currentTex.height * weaponScale };
            Vector2 origin = pose.origin;
            DrawWeaponSkinTexture(currentTex,srcRec,dstRec,origin,rotation,currentWeaponIndex,weaponSkinId,alpha);

            if (muzzleFlashTimer > 0.0f) {
                Vector2 start=pose.muzzle, dir=pose.direction, normal{-dir.y,dir.x};
                float pulse=std::clamp(muzzleFlashTimer/0.08f,0.0f,1.0f);
                float length=(currentWeaponIndex==1?42.0f:currentWeaponIndex==2?25.0f:32.0f)*(0.65f+0.35f*pulse);
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

    drawShieldOrbit(true);

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
    if (!IsDead() && dashCooldown <= 0.0f && dashTimer <= 0.0f) {
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
            position.x += dashDirection.x * speed * 3.0f * travelTime;
            position.y += dashDirection.y * speed * 3.0f * travelTime;
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
            ammo[currentWeaponIndex] = Weapons[currentWeaponIndex].maxAmmo;
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

int Character::GetMaxAmmo() const {
    return Weapons[currentWeaponIndex].maxAmmo;
}

bool Character::IsReloading() const {
    return reloadTimer > 0.0f;
}

void Character::RefillAmmo() {
    for (int i = 0; i < WeaponCount; ++i) ammo[i] = Weapons[i].maxAmmo;
}

float Character::GetReloadProgress() const {
    return IsReloading() ? 1.0f - reloadTimer / Weapons[currentWeaponIndex].reloadTime : 1.0f;
}

void Character::TriggerReload() {
    if (reloadTimer <= 0.0f && ammo[currentWeaponIndex] < Weapons[currentWeaponIndex].maxAmmo) {
        reloadTimer = Weapons[currentWeaponIndex].reloadTime;
    }
}
