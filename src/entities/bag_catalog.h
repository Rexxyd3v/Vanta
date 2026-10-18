#pragma once
#include "weapon_catalog.h"
#include <array>
namespace Bags {
constexpr int Count=3;
constexpr int MeleeDamageId=WeaponCount+2;
struct Loadout {
    int primary=0,secondary=2,primarySkin=0,secondarySkin=0,melee=0,meleeSkin=0;
    bool grenade=true,smoke=true,flash=true;
};
inline bool IsSecondary(int gun) { return gun==2 || gun==7; }
inline bool IsPrimary(int gun) { return gun>=0 && gun<WeaponCount && !IsSecondary(gun); }
inline void Normalize(Loadout& bag) {
    if(!IsPrimary(bag.primary)) bag.primary=0;
    if(!IsSecondary(bag.secondary)) bag.secondary=2;
    bag.primarySkin=std::clamp(bag.primarySkin,0,3);bag.secondarySkin=std::clamp(bag.secondarySkin,0,3);
    bag.melee=std::clamp(bag.melee,0,1);bag.meleeSkin=std::clamp(bag.meleeSkin,0,3);
}
inline std::array<Loadout,Count> Defaults() {
    std::array<Loadout,Count> bags{};bags[1].primary=3;bags[1].secondary=7;bags[2].primary=6;bags[2].melee=1;return bags;
}
inline int PackedSkins(const Loadout& bag,const std::array<int,WeaponCount>& storage) {
    auto skins=storage;skins[bag.primary]=bag.primarySkin;skins[bag.secondary]=bag.secondarySkin;return PackWeaponSkins(skins);
}
inline const char* MeleeName(int variant) { return variant==1?"KUKRI":"COMBAT KNIFE"; }
inline float MeleeRange(int variant) { return variant==1?130.0f:110.0f; }
inline float MeleeDamage(int variant) { return variant==1?70.0f:55.0f; }
inline float MeleeCooldown(int variant) { return variant==1?.75f:.5f; }
inline const char* UtilityName(int kind) { return kind==0?"GRENADE":kind==1?"POISON SMOKE":"FLASHBANG"; }
inline bool UtilityEquipped(const Loadout& bag,int kind) { return kind==0?bag.grenade:kind==1?bag.smoke:kind==2?bag.flash:false; }
}
