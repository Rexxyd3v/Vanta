#include "remote_player.h"
#include <cmath>
#include <algorithm>

static const float CHAR_SCALE = 0.15f;


static const std::string CHAR_PATH = "assets/Free 2D Animated Vector Game Character Sprites/Free 2D Animated Vector Game Character Sprites/Full body animated characters/";

RemotePlayer::RemotePlayer(Vector2 startPos, const std::string& assetPath, int weaponSkin)
    : Character(startPos, assetPath, CHAR_SCALE, weaponSkin), peerID(0), username("Player"), lastAimDir({0.0f, 0.0f}) {

    // Keep the inherited Character::name in sync so the head-label draw code
    // (which reads Character::name) works for online players too.
    Character::SetName(username);
}

RemotePlayer::~RemotePlayer() {
}

void RemotePlayer::ApplyNetworkUpdate(Vector2 pos, int state, int weaponIndex, int faceDir, float hp, float jumpH, float jumpV, int weaponSkin, float dash, float shield) {
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(hp)) return;
    float dx = pos.x - position.x, dy = pos.y - position.y;
    if (!hasNetworkTarget || dx*dx + dy*dy > 400.0f*400.0f) position = pos;
    networkTarget = pos;
    hasNetworkTarget = true;

    if (std::isfinite(dash) && std::isfinite(shield)) {
        if (shield > 0 && shieldTimer <= 0) shieldAge = 0;
        dashTimer = hp>0 ? std::clamp(dash,0.0f,0.22f) : 0;
        shieldTimer = hp>0 ? std::clamp(shield,0.0f,2.0f) : 0;
        float distance=std::hypot(dx,dy);
        if (dashTimer>0 && distance>0.01f) dashDirection={dx/distance,dy/distance};
        if (hp<=0) dashEchoes.clear();
    }

    // Set animation state directly
    CharState newState = (state >= 0 && state <= (int)CharState::DEATH) ? (CharState)state : CharState::IDLE;
    if (newState != currentState) {
        SetState(newState);
    }

    // Keep the remote player's weapon skin in sync with the networked selection.
    SetRemoteWeaponSkin(weaponSkin);

    // Set weapon
    if (weaponIndex >= 0 && weaponIndex < (int)weaponTextures.size()) {
        currentWeaponIndex = weaponIndex;
    }

    faceDirection = faceDir;
    health = hp;
    jumpHeight = jumpH;
    jumpVelocity = jumpV;
    // Aim direction is updated from shoot packets; aiming for rendering is handled in Update()
    // aimTarget = { position.x + faceDir * 200.0f, position.y };
}

void RemotePlayer::Update(float deltaTime) {
    if (hasNetworkTarget) {
        float blend = 1.0f - std::exp(-20.0f * deltaTime);
        position.x += (networkTarget.x - position.x) * blend;
        position.y += (networkTarget.y - position.y) * blend;
    }
    UpdateSkills(deltaTime, false);

    // Only update the animation timer, no keyboard input
    if (animations.count(currentState)) {
        animations[currentState]->Update(deltaTime);
    }
    // Update projectiles
    for (int i = 0; i < (int)projectiles.size(); ) {
        projectiles[i]->Update(deltaTime);
        if (!projectiles[i]->IsActive()) {
            projectiles.erase(projectiles.begin() + i);
        } else {
            i++;
        }
    }
    // Aim direction for rendering: use last known aim direction from shoot packets
    // If no aim data yet, fallback to facing direction.
    Vector2 aimDir = lastAimDir;
    if (aimDir.x == 0.0f && aimDir.y == 0.0f) {
        aimDir = { (float)faceDirection, 0.0f };
    }
    // Aim target is a point in front of the character (used for weapon rotation)
    WeaponPose pose=GetWeaponPoseInDirection(aimDir);
    aimTarget = {pose.muzzle.x+pose.direction.x*1000,pose.muzzle.y+pose.direction.y*1000};
}
