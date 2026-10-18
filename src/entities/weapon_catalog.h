#pragma once
#include <algorithm>
#include <array>

inline constexpr int WeaponCount=8;
inline constexpr int WeaponSkinCount=4;
inline constexpr int IndividualWeaponSkins=0x10000;
static_assert(WeaponCount*2<=16,"Packed per-gun skins must not overlap their format marker");
struct WeaponStats { int maxAmmo; float reloadTime,fireRate,bulletSpeed,bulletRange,damage; };
inline constexpr std::array<WeaponStats,WeaponCount> Weapons{{
    {30,1.5f,0.15f,1000,1200,12},
    {6,2.0f,0.8f,700,900,40},
    {12,1.2f,0.4f,750,1800,20},
    {30,1.9f,0.22f,1300,1700,24},
    {5,2.7f,1.1f,1800,2400,90},
    {30,2.2f,.28f,1250,1800,30}, // AK: heavier automatic rifle
    {2,2.3f,.65f,900,850,11}, // Double barrel: seven pellets per shell
    {24,1.35f,.095f,1100,1050,9} // Machine pistol: rapid close-range fire
}};
inline const char* WeaponName(int slot) {
    static const char* names[]={"SMG","SHOTGUN","PISTOL","ASSAULT RIFLE","SNIPER RIFLE","AK RIFLE","DOUBLE BARREL","MACHINE PISTOL"};
    return names[std::clamp(slot,0,WeaponCount-1)];
}
inline const char* WeaponShortName(int slot) {
    static const char* names[]={"SMG","SHOTGUN","PISTOL","AR","SNIPER","AK","DB","MP"};
    return names[std::clamp(slot,0,WeaponCount-1)];
}
inline int WeaponSkinSelection(int encoded,int slot) {
    if(slot<0 || slot>=WeaponCount) return 0;
    return (encoded&IndividualWeaponSkins)?(encoded>>(slot*2))&3:std::clamp(encoded,0,WeaponSkinCount-1);
}
inline int PackWeaponSkins(const std::array<int,WeaponCount>& skins) {
    int encoded=IndividualWeaponSkins;
    for(int i=0;i<WeaponCount;++i) encoded|=std::clamp(skins[i],0,WeaponSkinCount-1)<<(i*2);
    return encoded;
}
