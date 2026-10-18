#pragma once
#include <string>
#include <vector>
#include "../entities/weapon_catalog.h"
#include "../utils/texture_manager.h"
#include "ui_widgets.h"

enum class WeaponSkinId { Default=0, Neon=1, Magma=2, Carbon=3, COUNT=4 };
std::string GetWeaponTexturePath(int slot,int selection);
Texture2D LoadWeaponSlotTexture(int slot,int selection);
const char* WeaponSkinLabel(int slot,int skin);
void DrawWeaponSkinTexture(Texture2D texture,Rectangle source,Rectangle destination,Vector2 origin,float rotation,int slot,int selection,float alpha=1);
// Shared texture-based loadout helpers.
std::string GetWeaponSlotSkinPath(int slot,int selection);
Color GetWeaponSlotSkinTint(int slot,int selection);
const char* WeaponSkinPath(WeaponSkinId id);
Color GetWeaponSkinTint(int skinId);
std::vector<Ui::GalleryItem> GetWeaponSkinGalleryItems();
