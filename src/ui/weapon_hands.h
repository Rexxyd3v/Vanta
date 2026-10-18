#pragma once
#include "../entities/weapon_geometry.h"
struct WeaponHold {Vector2 trigger,support;float angle;bool twoHands;};
WeaponHold GetWeaponHold(Texture2D texture,int slot,const WeaponPose& pose,float scale,int facing,float reload=-1,float pump=0);
WeaponHold GetBonusHold(int kind,const WeaponPose& pose,float scale,int facing);
void DrawWeaponHands(const WeaponHold& hold,float size,float alpha,bool fingers);
void DrawEquipmentHand(Vector2 position,float angle,float size,float alpha,bool fingers);
