#pragma once
#include "ui_widgets.h"
#include "weapon_skin_catalog.h"
#include <array>
class WeaponSkinPreview {
public:
    WeaponSkinPreview(int selection=0);
    void SetSkin(int selection);
    void Update(float dt);
    void DrawPreview(Rectangle area,int selection,Font font);
    int DrawSelector(Rectangle area,Font font,int& skin,Vector2 mouse);
    int GetSelectedWeaponSlot() const { return selectedWeaponSlot; }
    void SetSelectedWeaponSlot(int slot) { selectedWeaponSlot=std::clamp(slot,0,WeaponCount-1); }
    int GetWeaponSkin(int slot) const { return selectedSkins[slot]; }
    int GetPackedSkins() const { return PackWeaponSkins(selectedSkins); }
    void SelectSkin(int slot,int skin,bool persist=true);
private:
    int selectedWeaponSlot=0;
    std::array<int,WeaponCount> selectedSkins{};
    float rotation=0;
};
