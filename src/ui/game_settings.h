#pragma once
#include <raylib.h>
#include "../entities/weapon_catalog.h"
#include "../entities/bag_catalog.h"

// Shared input/preferences; audio callback volume is copied into an atomic separately.
namespace Settings {
enum Action { Up, Down, Left, Right, Jump, Dash, Shield, Reload, Talk, Grenade, Smoke, Interact, Flash, ActionCount };
struct Preferences {
    float master = 0.8f, music = 0.6f, sfx = 0.8f, voice = 0.8f;
    std::array<int,WeaponCount> weaponSkins{};
    std::array<Bags::Loadout,Bags::Count> bags=Bags::Defaults();
    int activeBag=0;
    bool effects = true, showFps = false;
    int difficulty = 1;
    int keys[ActionCount] = {KEY_W, KEY_S, KEY_A, KEY_D, KEY_SPACE, KEY_LEFT_SHIFT, KEY_E, KEY_R, KEY_C, KEY_G, KEY_Q, KEY_F, KEY_T};
};
Preferences& Get();
void Load();
bool Save();
void Open();
bool IsOpen();
bool BlocksInput();
void Update();
void Draw();
bool DownKey(Action action);
bool Pressed(Action action);
const char* KeyLabel(Action action);
}
