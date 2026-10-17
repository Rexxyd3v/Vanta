#pragma once
#include <algorithm>
#include <vector>

// Pure rule decisions, shared by gameplay and regression checks.
namespace Rules {
constexpr float RespawnDelay = 3.0f;
constexpr float RoundSeconds = 120.0f;
inline bool FriendlyFire(bool teamMode, int shooter, int victim) {
    return teamMode && shooter != 0 && shooter == victim;
}
inline bool CanRespawn(bool elimination, bool matchOver, float deadFor) {
    return !elimination && !matchOver && deadFor >= RespawnDelay;
}
inline bool TeamsValid(const std::vector<int>& teams, bool teamMode) {
    if (teams.size() < 2) return false;
    if (!teamMode) return true;
    int a = 0, b = 0;
    for (int team : teams) { if (team == 1) ++a; else if (team == 2) ++b; else return false; }
    return a > 0 && b > 0 && std::abs(a - b) <= 1;
}
inline bool LimitReached(int timeLimit, int killLimit, float elapsed, int topScore) {
    return (timeLimit > 0 && elapsed >= timeLimit) || (killLimit > 0 && topScore >= killLimit);
}
}
