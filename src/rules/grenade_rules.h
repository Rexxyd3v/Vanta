#pragma once
#include <algorithm>
#include <cmath>

namespace GrenadeRules {
constexpr float Fuse = 0.8f;
constexpr float SmokeDuration = 8.0f;
constexpr float PoisonInterval = 0.5f;
constexpr float PoisonDamage = 6.0f;
constexpr float BlastRadius = 230.0f;
constexpr float BlastLifetime = 4.0f;
inline float FlashDuration(float distance,bool facing=true) {
    return std::clamp(1.0f-distance/600.0f,0.0f,1.0f)*(facing?2.6f:1.15f);
}
inline float SmokeRadius(float activeAge) {
    return 55.0f + 205.0f * std::clamp(activeAge / 2.5f, 0.0f, 1.0f);
}
inline int PoisonTicks(float age) {
    float active = std::clamp(age - Fuse, 0.0f, SmokeDuration);
    return (int)std::floor((active + 0.00001f) / PoisonInterval);
}
inline float ShakeStrength(float distance) {
    float proximity = std::clamp(1.0f - distance / 850.0f, 0.0f, 1.0f);
    return 14.0f * proximity * proximity;
}
inline bool PoisonAffects(bool ownSmoke, bool teamMode, int ownerTeam, int victimTeam) {
    return ownSmoke || !teamMode || ownerTeam == 0 || ownerTeam != victimTeam;
}
}
