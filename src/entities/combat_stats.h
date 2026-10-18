#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
struct CombatStats {
    int shots=0,hits=0,captures=0;
    float damage=0;
    std::unordered_set<uint32_t> hitShots;
    void Confirm(float amount,uint32_t shot=0){
        if(!std::isfinite(amount) || amount<=0)return;
        damage+=amount;
        if(shot>0 && shot<=(uint32_t)shots && hitShots.insert(shot).second)++hits;
    }
    float Accuracy()const{return shots>0?100.0f*hits/shots:0;}
    float Rating(int kills,int deaths)const{return captures*500+kills*100+damage*.1f-deaths*10;}
};
struct WeaponFeel {float kick,maximum,recovery,push;};
inline constexpr WeaponFeel GunFeel[]={{.65f,3.6f,6,3},{3.2f,5,10,7},{1.8f,3.2f,8,4},{1.0f,4.2f,6,4},{4.0f,5,7,9},{1.4f,5,6,5},{4.2f,6,11,9},{.45f,3.8f,5,2.5f}};
