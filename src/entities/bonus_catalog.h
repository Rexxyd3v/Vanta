#pragma once
#include "weapon_catalog.h"
#include "weapon_geometry.h"
#include <raylib.h>
namespace Bonus {
constexpr int Count=6, FirstDamage=WeaponCount+3, LastDamage=FirstDamage+Count-1;
inline bool Valid(int kind) { return kind>=0 && kind<Count; }
inline const char* Name(int kind) { static const char* names[]={"FLAMETHROWER","GRENADE LAUNCHER","BAZOOKA","FREEZE BOMB","SHOCK GRENADE","INCENDIARY"};return Valid(kind)?names[kind]:"BONUS"; }
inline Color Tint(int kind) { static const Color colors[]={{255,147,74,255},{248,201,98,255},{231,114,103,255},{122,224,255,255},{187,151,255,255},{255,107,65,255}};return Valid(kind)?colors[kind]:WHITE; }
inline int Ammo(int kind) { return kind==0?60:kind==1?4:kind==2?3:2; }
inline WeaponGeometry Geometry(int kind){
    const Vector2 grips[]={{41,46},{28,46},{42,47}};
    const Vector2 supports[]={{79,35},{73,38},{72,39}};
    int index=std::clamp(kind,0,2);WeaponGeometry g{grips[index],{100,30},104};g.support=supports[index];return g;
}
inline float HeldScale(float characterScale){return (44.0f/60)*(characterScale/.15f);}
inline float Cooldown(int kind) { return kind==0?.085f:kind==1?.9f:1.2f; }
}
// Code-native silhouettes use the same angular light receiver/dark hardware art direction as the arsenal.
void DrawBonusArt(int kind,Rectangle area,float rotation=0,float alpha=1,bool mirror=false);

void DrawBonusFlame(Vector2 origin,Vector2 direction,float length,float width,float phase,float alpha=1);
void DrawBonusIce(Vector2 center,float remaining);
void DrawBonusBurst(int kind,Vector2 center,float age,bool detailed=true);
