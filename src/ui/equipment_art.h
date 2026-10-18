#pragma once
#include <raylib.h>
void DrawEquipmentArt(bool melee,int item,int skin,Rectangle area,float rotation=0,float alpha=1);
void DrawHeldEquipment(Vector2 hand,Vector2 target,int facing,int melee,int skin,int utility,float swing);
