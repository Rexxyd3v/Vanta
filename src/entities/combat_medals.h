#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Medals {
enum Kind { DoubleKill, TripleKill, Revenge, Shutdown };
inline const char* Name(Kind k){return k==DoubleKill?"DOUBLE KILL":k==TripleKill?"TRIPLE KILL":k==Revenge?"REVENGE":"SHUTDOWN";}
inline const char* Detail(Kind k){return k==DoubleKill?"TWO ELIMINATIONS / FOUR SECONDS":k==TripleKill?"THREE ELIMINATIONS / RAPID CHAIN":k==Revenge?"YOUR LAST KILLER / ELIMINATED":"ENEMY KILL STREAK / ENDED";}
struct Life {int streak=0,chain=0;float lastKill=-100;uint64_t killer=0;bool hasKiller=false;};
class Tracker {
    std::unordered_map<uint64_t,Life> actors;
public:
    void Reset(){actors.clear();}
    std::vector<Kind> Kill(uint64_t killer,uint64_t victim,uint64_t local,float now,bool enemies=true){
        auto& v=actors[victim];int stopped=v.streak;v.streak=v.chain=0;v.lastKill=-100;
        if(killer==victim || !enemies)return {};
        v.killer=killer;v.hasKiller=true;
        auto& k=actors[killer];bool revenge=k.hasKiller && k.killer==victim;
        if(revenge)k.hasKiller=false;
        k.chain=now-k.lastKill<=4? k.chain+1:1;k.lastKill=now;++k.streak;
        if(killer!=local)return {};
        std::vector<Kind> result;
        if(k.chain==2)result.push_back(DoubleKill);else if(k.chain==3)result.push_back(TripleKill);
        if(revenge)result.push_back(Revenge);
        if(stopped>=3)result.push_back(Shutdown);
        return result;
    }
};
}
