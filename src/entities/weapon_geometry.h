#pragma once
#include <raylib.h>

struct WeaponGeometry { Vector2 pivot; Vector2 muzzle; float width; };
struct WeaponPose { Vector2 hand, muzzle, direction, origin; float rotation; };
WeaponGeometry GetWeaponGeometry(Texture2D texture);
WeaponPose PoseWeapon(Vector2 hand, Vector2 direction, int facing, float scale, const WeaponGeometry& geometry);
WeaponPose AimWeapon(Vector2 hand, Vector2 target, int facing, float scale, const WeaponGeometry& geometry);
