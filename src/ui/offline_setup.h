#pragma once
#include <algorithm>
#include <string>

namespace Offline {
enum class Mode { Training, FreeForAll, TeamDeathmatch, Elimination, Deathmatch };
struct Setup {
    Mode mode = Mode::Training;
    int bots = 3;
    int difficulty = 1;
    int playerTeam = 1;
    int killLimit = 20;
    int roundLimit = 5;
    int timeLimit = 300;
    std::string map="MagmaRift";
};
inline Setup& Get() { static Setup setup; return setup; }
inline bool TeamMode(Mode mode) { return mode==Mode::TeamDeathmatch || mode==Mode::Elimination || mode==Mode::Deathmatch; }
inline bool RoundMode(Mode mode) { return mode==Mode::Elimination; }
inline const char* Name(Mode mode) {
    switch(mode) {
        case Mode::Training: return "TRAINING";
        case Mode::FreeForAll: return "FREE FOR ALL";
        case Mode::TeamDeathmatch: return "TEAM DEATHMATCH";
        case Mode::Elimination: return "ELIMINATION";
        case Mode::Deathmatch: return "DEATHMATCH / FLAG";
    }
    return "TRAINING";
}
inline const char* Description(Mode mode) {
    switch(mode) {
        case Mode::Training: return "Warm up freely. Unlimited respawns and no score limit.";
        case Mode::FreeForAll: return "Random arena spawns. Fight for special pickups and reach the kill target.";
        case Mode::TeamDeathmatch: return "Fight with your squad. Team kills count toward victory.";
        case Mode::Elimination: return "One life per round. Eliminate the opposing team.";
        case Mode::Deathmatch: return "Carry your flag to the enemy base. Respawn at home; captures win.";
    }
    return "";
}
inline const char* Difficulty(int value) { return value==0 ? "EASY" : value==1 ? "MEDIUM" : "DIFFICULT"; }
inline void Normalize(Setup& s) {
    s.bots=std::clamp(s.bots,s.mode==Mode::Training ? 0 : 1,15);
    s.difficulty=std::clamp(s.difficulty,0,2); s.playerTeam=s.playerTeam==2 ? 2 : 1;
    s.killLimit=std::clamp(s.killLimit,5,100); s.roundLimit=std::clamp(s.roundLimit,1,14);
    s.timeLimit=std::clamp(s.timeLimit,60,1800);
}
inline int BotTeam(int index,int playerTeam) { return index%2==0 ? 3-playerTeam : playerTeam; }
}
