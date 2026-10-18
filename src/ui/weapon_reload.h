#pragma once
#include "../entities/weapon_geometry.h"
namespace WeaponReload {
enum class Style { Magazine, Tube, BreakOpen };
Style Type(int slot);
float MagazineTravel(float progress);
float BreakAngle(float progress);
Vector2 Support(Texture2D texture,int slot,const WeaponPose& pose,float scale,int facing,float progress,float pump,Vector2 rest);
void Draw(Texture2D texture,int slot,int skin,const WeaponPose& pose,float scale,int facing,float progress,float pump,float alpha=1);
}
