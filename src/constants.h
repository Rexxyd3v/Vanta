#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cstdint>

#define VIRTUAL_WIDTH 1300
#define VIRTUAL_HEIGHT 680
#define DEFAULT_GAME_PORT 7777

// World is 3x the virtual screen (must be visible to all screens that simulate world space)
#define WORLD_WIDTH  3900
#define WORLD_HEIGHT 2040

enum class GameMode {
    NONE,
    PRACTICE,
    ONLINE
};

// Team ID used by team-based online modes (Elimination / TDM).
// 0 = unassigned / FFA, 1 = Global Risk (Blue), 2 = Black List (Red).
enum class TeamID : uint8_t {
    NONE       = 0,
    GLOBAL_RISK = 1,
    BLACKLIST   = 2
};

#endif // CONSTANTS_H
