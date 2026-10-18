#pragma once
#include <algorithm>
#include <array>

inline constexpr int WeaponCount=5;
inline constexpr int WeaponSkinCount=4;
inline constexpr int IndividualWeaponSkins=0x10000;
struct WeaponStats { int maxAmmo; float reloadTime,fireRate,bulletSpeed,bulletRange,damage; };
inline constexpr std::array<WeaponStats,WeaponCount> Weapons{{
    {30,1.5f,0.15f,1000,1200,12},
    {6,2.0f,0.8f,700,900,40},
    {12,1.2f,0.4f,750,1800,20},
    {30,1.9f,0.22f,1300,1700,24},
    {5,2.7f,1.1f,1800,2400,90}
}};
inline const char* WeaponName(int slot) {
    static const char* names[]={"SMG","SHOTGUN","PISTOL","ASSAULT RIFLE","SNIPER RIFLE"};
    return names[std::clamp(slot,0,WeaponCount-1)];
}
inline const char* WeaponShortName(int slot) {
    static const char* names[]={"SMG","SHOTGUN","PISTOL","AR","SNIPER"};
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
