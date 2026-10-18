#include "bot_enemy.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

// Static demo-mode flag (false by default — only MenuBackground flips it on)
bool BotEnemy::demoMode = false;
void BotEnemy::SetDemoMode(bool enabled) { demoMode = enabled; }

// ---- Gameplay constants (normal bots) ----
const float BOT_SPEED          = 100.0f;
const float BOT_SHOOT_COOLDOWN = 1.5f;
const float BOT_AGGRO_RANGE    = 800.0f;
const float BOT_MOVE_RANGE     = 500.0f;

// ---- Spectator / demo constants (menu background bots) ----
const float DEMO_SPEED          = 95.0f;   // slow and cinematic
const float DEMO_SHOOT_COOLDOWN = 1.1f;    // controlled fire rate
const float DEMO_AGGRO_RANGE    = 950.0f;
const float DEMO_MOVE_RANGE     = 80.0f;   // stay close for action

BotEnemy::BotEnemy(Vector2 startPosition, const std::string& assetPath)
    : Character(startPosition, assetPath, 0.15f), stateTimer(0.0f), isMoving(false), deathTimer(0.0f),
      strafeDir(1.0f), strafeTimer(0.0f) {
    this->speed = BOT_SPEED;
    this->shootCooldown = BOT_SHOOT_COOLDOWN;
    this->health = 100.0f;
    this->currentWeaponIndex = GetRandomValue(0, WeaponCount-1); // Random weapon for variety
    this->targetPosition = startPosition;
    maneuverClock=GetRandomValue(40,90)/10.0f;
    strafeDir=GetRandomValue(0,1)?1.0f:-1.0f;
    this->respawnDelay = (float)GetRandomValue(25, 40) / 10.0f; // 2.5-4.0s
}

void BotEnemy::SetDifficulty(int level) {
    difficulty=std::clamp(level,0,2);
    const float speeds[]={85,110,145}, cooldowns[]={1.9f,1.25f,0.75f};
    speed=speeds[difficulty]; shootCooldown=cooldowns[difficulty];
}
void BotEnemy::MoveToObjective(float dt,Vector2 target,Vector2 spacing) {
    if(GetFlashBlind()>0 || IsFrozen()) { velocity={};SetState(CharState::IDLE);return; }
    if (IsDead()) return;
    ManageAmmo();
    if(walkable && TacticalMove(dt,target,true))return;
    Vector2 delta{target.x-position.x,target.y-position.y};
    float distance=std::sqrt(delta.x*delta.x+delta.y*delta.y);
    SetAimTarget(target); faceDirection=delta.x>=0 ? 1 : -1;
    if (distance>35) {
        Vector2 direction{delta.x/distance+spacing.x,delta.y/distance+spacing.y};
        float length=std::hypot(direction.x,direction.y);
        if(length>.01f){direction.x/=length;direction.y/=length;}
        Vector2 next{position.x+direction.x*speed*GetSlowFactor()*dt,position.y+direction.y*speed*GetSlowFactor()*dt};
        if(!walkable || walkable(next))position=next;
        else{next={position.x+delta.x/distance*speed*GetSlowFactor()*dt,position.y+delta.y/distance*speed*GetSlowFactor()*dt};if(!walkable || walkable(next))position=next;}
        SetState(CharState::WALK);
    } else SetState(CharState::IDLE);
}

BotEnemy::~BotEnemy() {
}

// Predefined spawn points covering the 3900x2040 world map
static const Vector2 SPAWN_POINTS[] = {
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
static const int NUM_SPAWN_POINTS = sizeof(SPAWN_POINTS) / sizeof(SPAWN_POINTS[0]);

void BotEnemy::Update(float deltaTime) {
    // Standard updates like physics, animations, and projectiles
    if (IsDead()) {
        SetState(CharState::DEATH);
        deathTimer += deltaTime;
    } else {
        deathTimer = 0.0f;
    }

    UpdateSkills(deltaTime);
    UpdatePhysics(deltaTime);

    // Update cooldowns
    if (currentShootCooldown > 0.0f) {
        currentShootCooldown -= deltaTime;
    }

    // Update active projectiles
    for (auto& p : projectiles) {
        p->Update(deltaTime);
    }

    projectiles.erase(std::remove_if(projectiles.begin(),projectiles.end(),[](const std::shared_ptr<Projectile>& p) { return !p->IsActive(); }),projectiles.end());

    // Update current animation
    if (animations.count(currentState)) {
        animations[currentState]->Update(deltaTime);
    }

    // Update fade-in timer for demo mode
    if (spawnFadeTimer > 0.0f) {
        spawnFadeTimer -= deltaTime;
        if (spawnFadeTimer < 0.0f) {
            spawnFadeTimer = 0.0f;
        }
    }
}

void BotEnemy::UpdateAI(float deltaTime, Vector2 playerPosition) {
    if(GetFlashBlind()>0 || IsFrozen()) { velocity={};SetState(CharState::IDLE);return; }
    if (IsDead()) return;
    ManageAmmo();
    if(!demoMode && !isMonster && walkable){TacticalMove(deltaTime,playerPosition,false);return;}

    // Distance to target
    float dx = playerPosition.x - position.x;
    float dy = playerPosition.y - position.y;
    float distance = sqrtf(dx*dx + dy*dy);

    // Aim at target
    SetAimTarget(playerPosition);

    // Face the target
    faceDirection = (playerPosition.x > position.x) ? 1 : -1;

    if (isMonster) {
        // --- MONSTER MELEE AI: always chase aggressively, no shooting ---
        // Monsters are not allowed to use shield or dash — those skills are reserved for players and ranged bots.
        float monsterSpeed = (demoMode ? DEMO_SPEED : speed) * 1.8f; // faster than bots

        if (distance > 45.0f) {
            velocity.x = (dx / distance) * monsterSpeed;
            velocity.y = (dy / distance) * monsterSpeed;
            SetState(CharState::WALK);
        } else {
            velocity.x = 0;
            velocity.y = 0;
            SetState(CharState::IDLE);
        }
        position.x += velocity.x * GetSlowFactor() * deltaTime;
        position.y += velocity.y * GetSlowFactor() * deltaTime;
        // Melee damage is handled in main_level.cpp (CheckCollisions / contact damage)
    } else if (demoMode) {
        // ---- DEMO / SPECTATOR MODE — professional-looking ranged AI ----
        // Always active, always repositioning. Looks like a skilled player:
        // strafes while shooting, dashes aggressively, jumps frequently.

        // --- State timer for repositioning ---
        stateTimer -= deltaTime;
        if (stateTimer <= 0.0f) {
            stateTimer = (float)GetRandomValue(3, 9) / 10.0f; // 0.3-0.9s (snappy decisions)

            if (distance > DEMO_MOVE_RANGE) {
                // Close-in with slight random offset so they don't stack perfectly
                isMoving = true;
                float offX = (float)GetRandomValue(-120, 120);
                float offY = (float)GetRandomValue(-120, 120);
                targetPosition = { playerPosition.x + offX, playerPosition.y + offY };
            } else {
                // Already in range — hold position or strafe
                isMoving = GetRandomValue(0, 1) == 1; // 50% chance to stay and shoot
            }
        }

        // --- Strafe timer: lateral movement while at short range ---
        strafeTimer -= deltaTime;
        if (strafeTimer <= 0.0f) {
            // Pick a new strafe direction every 0.4-0.9s
            strafeDir = (GetRandomValue(0, 1) == 0) ? 1.0f : -1.0f;
            strafeTimer = (float)GetRandomValue(4, 9) / 10.0f;
        }

        // Perpendicular (strafe) direction
        float perpX = 0.0f, perpY = 0.0f;
        if (distance > 0.01f) {
            perpX = -(dy / distance) * strafeDir;
            perpY =  (dx / distance) * strafeDir;
        }

        if (isMoving) {
            float tDx = targetPosition.x - position.x;
            float tDy = targetPosition.y - position.y;
            float tDist = sqrtf(tDx*tDx + tDy*tDy);

            if (tDist > 10.0f) {
                // Blend approach + strafe for fluid movement
                float approachX = (tDx / tDist) * DEMO_SPEED * 0.7f;
                float approachY = (tDy / tDist) * DEMO_SPEED * 0.7f;
                float strafeX   = perpX * DEMO_SPEED * 0.5f;
                float strafeY   = perpY * DEMO_SPEED * 0.5f;
                velocity.x = approachX + strafeX;
                velocity.y = approachY + strafeY;
                SetState(CharState::WALK);
            } else {
                // Arrived — strafe only
                velocity.x = perpX * DEMO_SPEED * 0.55f;
                velocity.y = perpY * DEMO_SPEED * 0.55f;
                SetState(velocity.x != 0.0f || velocity.y != 0.0f ? CharState::WALK : CharState::IDLE);
            }
        } else {
            // Not closing — pure strafe
            velocity.x = perpX * DEMO_SPEED * 0.45f;
            velocity.y = perpY * DEMO_SPEED * 0.45f;
            SetState(CharState::WALK);
        }

        // --- Jump occasionally (not every second) ---
        bool isOnGround = (jumpHeight <= floorHeight + 1.0f && jumpVelocity <= 0.0f);
        bool startedJump = false;
        if (isOnGround && !IsFrozen() && GetRandomValue(0, 200) == 0) { // ~1/200 chance per frame — rare
            jumpVelocity = 420.0f;
            startedJump = true;
        }

        // --- Dash only when far away ---
        if (distance > 350.0f && GetRandomValue(0, 120) == 0) { // rare dash
            ActivateDash({ dx, dy });
        }
        // --- Shield when low health ---
        if (health < 45.0f && GetRandomValue(0, 60) == 0) {
            ActivateShield();
        }

        // NOTE: Do NOT directly add velocity*deltaTime here.
        // UpdatePhysics() (called from Update()) will apply velocity to position,
        // and then menu_background.cpp runs resolveWorld() AFTER that to push
        // the character out of walls — this makes collision actually work.

        // --- Shoot aggressively if in range ---
        if (distance < DEMO_AGGRO_RANGE && currentShootCooldown <= 0.0f) {
            Shoot(playerPosition);
            // Randomize cooldown slightly so bursts don't all fire at once
            currentShootCooldown = DEMO_SHOOT_COOLDOWN + ((float)GetRandomValue(0, 8) / 20.0f);
            // Change strafe direction after each shot (evasive maneuver)
            strafeDir = -strafeDir;
            strafeTimer = (float)GetRandomValue(3, 7) / 10.0f;
        }

        // ---- Jump/fall state overrides ----
        if (!isOnGround && jumpVelocity > 0.0f) {
            if (startedJump || currentState != CharState::JUMP_START)
                SetState(CharState::JUMP_START);
        } else if (!isOnGround && jumpVelocity <= 0.0f) {
            SetState(CharState::FALL);
        } else if (isOnGround && (currentState == CharState::JUMP_START || currentState == CharState::FALL)) {
            SetState(CharState::JUMP_END);
        }

    } else {
        // --- BOT RANGED AI (normal gameplay) ---
        stateTimer -= deltaTime;
        if (stateTimer <= 0.0f) {
            stateTimer = (float)GetRandomValue(10, 25) / 10.0f;

            if (distance > BOT_MOVE_RANGE) {
                isMoving = true;
                float randomOffsetX = (float)GetRandomValue(-200, 200);
                float randomOffsetY = (float)GetRandomValue(-200, 200);
                targetPosition = {playerPosition.x + randomOffsetX, playerPosition.y + randomOffsetY};
            } else {
                isMoving = false;
            }
        }

        bool wantsToWalk = false;
        if (isMoving) {
            float tDx = targetPosition.x - position.x;
            float tDy = targetPosition.y - position.y;
            float tDist = sqrtf(tDx*tDx + tDy*tDy);

            if (tDist > 10.0f) {
                velocity.x = (tDx / tDist) * speed;
                velocity.y = (tDy / tDist) * speed;
                wantsToWalk = true;
            } else {
                isMoving = false;
                velocity.x = 0;
                velocity.y = 0;
            }
        } else {
            velocity.x = 0;
            velocity.y = 0;
        }

        // Random jump while moving
        bool isOnGround = (jumpHeight <= floorHeight + 1.0f && jumpVelocity <= 0.0f);
        bool startedJump = false;
        if (isMoving && isOnGround && !IsFrozen() && GetRandomValue(0, 200) == 0) {
            jumpVelocity = 420.0f;
            startedJump = true;
        }

        // Random dash towards target
        if (isMoving && distance > 300.0f && GetRandomValue(1, 120) == 1) {
            ActivateDash({dx, dy});
        }
        // Shield if low health
        if (health < 40.0f && GetRandomValue(1, 100) == 1) {
            ActivateShield();
        }

        if (dashTimer <= 0.0f) {
            position.x += velocity.x * GetSlowFactor() * deltaTime;
            position.y += velocity.y * GetSlowFactor() * deltaTime;
        }

        // Shoot if in aggro range
        if (distance < (difficulty==0 ? 650.0f : difficulty==1 ? 900.0f : 1150.0f) && currentShootCooldown <= 0.0f) {
            const int spread[]={90,38,8};
            Shoot({playerPosition.x+GetRandomValue(-spread[difficulty],spread[difficulty]), playerPosition.y+GetRandomValue(-spread[difficulty],spread[difficulty])});
            currentShootCooldown = std::max(shootCooldown,Weapons[currentWeaponIndex].fireRate) + ((float)GetRandomValue(0, 10) / 30.0f);
        }

        // ---- JUMP / FALL state picking (override walk/idle) ----
        if (!isOnGround) {
            if (jumpVelocity > 0.0f) {
                if (startedJump || currentState != CharState::JUMP_START) {
                    SetState(CharState::JUMP_START);
                }
            } else {
                SetState(CharState::FALL);
            }
        } else {
            bool jumpEndDone = (currentState == CharState::JUMP_END &&
                                animations.count(CharState::JUMP_END) &&
                                animations[CharState::JUMP_END]->IsFinished());

            if (currentState == CharState::JUMP_START || currentState == CharState::FALL) {
                SetState(CharState::JUMP_END);
            } else if (currentState == CharState::JUMP_END && !jumpEndDone) {
                // Still playing JUMP_END; let it finish before transitioning
            } else {
                if (wantsToWalk) SetState(CharState::WALK);
                else             SetState(CharState::IDLE);
            }
        }
    }
}

bool BotEnemy::ShouldRespawn(float minimumDelay) const {
    return IsDead() && deathTimer >= std::max(respawnDelay,minimumDelay);
}

void BotEnemy::ResetDeathTimer() {
    deathTimer = 0.0f;
    isMoving = false;velocity={};tacticalTimer=0;evading=usingCover=false;
    if(preferredWeapon>=0){SwitchWeapon(preferredWeapon);preferredWeapon=-1;}
    this->respawnDelay = (float)GetRandomValue(25, 40) / 10.0f; // 2.5-4.0s
}


float BotEnemy::GetDrawAlpha() const {
    // Only apply fade-in effect in demo mode (menu background)
    if (demoMode && spawnFadeTimer > 0.0f) {
        // Fade in over 0.4 seconds
        const float fadeDuration = 0.4f;
        return spawnFadeTimer / fadeDuration; // 0.0 to 1.0
    }
    return 1.0f;
}

// Tactical decisions use the same authored collision and hazard zones as gameplay.
void BotEnemy::ManageAmmo(){
    if(IsDead() || BonusEquipped() || isMonster)return;
    if(preferredWeapon>=0 && ammo[preferredWeapon]+reserveAmmo[preferredWeapon]>0){SwitchWeapon(preferredWeapon);preferredWeapon=-1;}
    if(GetAmmo()+GetReserveAmmo()==0 && currentWeaponIndex!=2){preferredWeapon=currentWeaponIndex;SwitchWeapon(2);}
    if(GetAmmo()==0 && GetReserveAmmo()>0 && !IsReloading())TriggerReload();
}
bool BotEnemy::RouteClear(Vector2 goal)const{
    if(!walkable)return true;
    float d=std::hypot(goal.x-position.x,goal.y-position.y);int steps=std::max(1,(int)std::ceil(d/18));
    for(int i=1;i<=steps;++i){Vector2 p{position.x+(goal.x-position.x)*i/steps,position.y+(goal.y-position.y)*i/steps};
        if(!walkable(p))return false;
        for(const auto& h:hazards){float start=std::hypot(position.x-h.position.x,position.y-h.position.y),next=std::hypot(p.x-h.position.x,p.y-h.position.y);
            if(next<h.radius+12 && next<start-1)return false;}
    }
    return true;
}
bool BotEnemy::TacticalMove(float dt,Vector2 target,bool objective){
    bool wasEvading=evading;evading=false;usingCover=false;coverClock+=dt;
    float danger=0;
    for(const auto& h:hazards){float d=std::hypot(position.x-h.position.x,position.y-h.position.y);
        if(d<h.radius+90){evading=true;danger+=(h.radius+90-d)/std::max(1.0f,h.radius);}}
    if(objective && !evading)return false;
    SetAimTarget(target);faceDirection=target.x>=position.x?1:-1;
    maneuverClock-=dt;
    bool seekCover=!objective && (IsReloading() || (health<48 && std::fmod(coverClock,3.8f)<2.4f));
    tacticalTimer-=dt;
    if(tacticalTimer<=0 || evading!=wasEvading){
        tacticalTimer=evading?.12f:.32f+(2-difficulty)*.15f;
        float best=-1e20f;Vector2 chosen=position;
        float enemyDistance=std::hypot(target.x-position.x,target.y-position.y);
        for(int ring=0;ring<3;++ring)for(int i=0;i<16;++i){float a=i*2*PI/16,r=ring==0?80.0f:ring==1?160.0f:260.0f;
            Vector2 p{position.x+std::cos(a)*r,position.y+std::sin(a)*r};if(!RouteClear(p))continue;
            float score=0;bool exposed=visible?visible(target,{p.x,p.y+35}):true;
            if(evading){for(const auto& h:hazards)score+=std::min(450.0f,std::hypot(p.x-h.position.x,p.y-h.position.y)-h.radius)*4;score-=r*.12f;}
            else if(seekCover){score=exposed?-800:800;score-=r*.6f;}
            else{float ideal=(currentWeaponIndex==1 || currentWeaponIndex==6)?210:currentWeaponIndex==4?620:380;
                score=-std::abs(std::hypot(p.x-target.x,p.y-target.y)-ideal)-r*.2f;
                if(exposed)score+=160;
                // Alternating flanks distribute bots instead of stacking at the target.
                score+=std::sin(a)*strafeDir*32;
                if(enemyDistance>1000)score-=std::hypot(p.x-target.x,p.y-target.y)*.4f;}
            if(score>best){best=score;chosen=p;}
        }
        if(seekCover && visible && !visible(target,{position.x,position.y+35}))chosen=position;
        tacticalGoal=chosen;
    }
    usingCover=seekCover && visible && !visible(target,{tacticalGoal.x,tacticalGoal.y+35});
    float dx=tacticalGoal.x-position.x,dy=tacticalGoal.y-position.y,d=std::hypot(dx,dy);
    velocity={};
    if(d>8){float movement=speed*(evading?1.2f:1);velocity={dx/d*movement,dy/d*movement};
        Vector2 next{position.x+velocity.x*dt*GetSlowFactor(),position.y+velocity.y*dt*GetSlowFactor()};
        if(walkable(next) && dashTimer<=0)position=next;else tacticalTimer=0;
        SetState(CharState::WALK);
        if(evading && difficulty>0 && danger>.45f && RouteClear({position.x+dx/d*180,position.y+dy/d*180}))ActivateDash({dx,dy});
    }else SetState(CharState::IDLE);
    if(seekCover || evading)ActivateShield();
    if(!evading && !seekCover && d>30 && maneuverClock<=0 && jumpHeight<=floorHeight+1){
        jumpVelocity=420;maneuverClock=GetRandomValue(55,100)/10.0f;
    }
    if(jumpHeight>floorHeight+1 || jumpVelocity>0)SetState(jumpVelocity>0?CharState::JUMP_START:CharState::FALL);
    float distance=std::hypot(target.x-position.x,target.y-position.y);
    if(!objective && !evading && !IsReloading() && distance<(difficulty==0?650:difficulty==1?900:1150) &&
       currentShootCooldown<=0 && (!visible || visible(GetHandPosition(),target))){
        const int spread[]={90,38,8};
        const float cadence[]={1.9f,1.25f,.75f};
        if(Shoot({target.x+GetRandomValue(-spread[difficulty],spread[difficulty]),target.y+GetRandomValue(-spread[difficulty],spread[difficulty])}))
            currentShootCooldown=std::max(cadence[difficulty],Weapons[currentWeaponIndex].fireRate)+GetRandomValue(0,10)/30.0f;
    }
    return true;
}
