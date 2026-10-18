#include "game_settings.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "../constants.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

namespace Settings {
static Preferences prefs;
static bool opened = false;
static bool blocked = false;
static int binding = -1;
static std::string status;
Preferences& Get() { return prefs; }
bool IsOpen() { return opened; }
bool BlocksInput() { return opened || blocked; }
void Open() { opened = true; binding = -1; status.clear(); }
static bool ValidKey(int k) { return (k >= KEY_APOSTROPHE && k <= KEY_GRAVE) || (k >= KEY_SPACE && k <= KEY_SPACE) || (k >= KEY_INSERT && k <= KEY_END) || (k >= KEY_CAPS_LOCK && k <= KEY_PAUSE) || (k >= KEY_KP_0 && k <= KEY_KP_EQUAL) || (k >= KEY_LEFT_SHIFT && k <= KEY_KB_MENU); }
void Load() {
    std::ifstream in("vanta-settings.cfg");
    bool savedBags=false;
    std::string key; float value;
    while (in >> key >> value) {
        if (!std::isfinite(value)) continue;
        if (key == "master") prefs.master = std::clamp(value, 0.0f, 1.0f);
        else if (key == "music") prefs.music = std::clamp(value, 0.0f, 1.0f);
        else if (key == "sfx") prefs.sfx = std::clamp(value, 0.0f, 1.0f);
        else if (key == "voice") prefs.voice = std::clamp(value, 0.0f, 1.0f);
        else if (key == "effects") prefs.effects = value != 0;
        else if (key == "fps") prefs.showFps = value != 0;
        else if (key == "difficulty") prefs.difficulty = (int)std::clamp(value, 0.0f, 2.0f);
        else if(key=="activeBag") prefs.activeBag=(int)std::clamp(value,0.0f,2.0f);
        else if(key.rfind("bag",0)==0) {
            savedBags=true;
            for(int i=0;i<Bags::Count;++i) {
                std::string prefix="bag"+std::to_string(i);auto& bag=prefs.bags[i];
                if(key==prefix+"Primary") bag.primary=(int)std::clamp(value,-1.0f,100.0f);
                if(key==prefix+"Secondary") bag.secondary=(int)std::clamp(value,-1.0f,100.0f);
                if(key==prefix+"PrimarySkin") bag.primarySkin=(int)std::clamp(value,0.0f,3.0f);
                if(key==prefix+"SecondarySkin") bag.secondarySkin=(int)std::clamp(value,0.0f,3.0f);
                if(key==prefix+"Melee") bag.melee=(int)std::clamp(value,0.0f,1.0f);
                if(key==prefix+"MeleeSkin") bag.meleeSkin=(int)std::clamp(value,0.0f,3.0f);
                if(key==prefix+"Grenade") bag.grenade=value!=0;
                if(key==prefix+"Smoke") bag.smoke=value!=0;
                if(key==prefix+"Flash") bag.flash=value!=0;
                Bags::Normalize(bag);
            }
        }
        else if(key.rfind("weaponSkin",0)==0) {
            for(int i=0;i<WeaponCount;++i) if(key=="weaponSkin"+std::to_string(i)) prefs.weaponSkins[i]=(int)std::clamp(value,0.0f,(float)(WeaponSkinCount-1));
        }
        else for (int i = 0; i < ActionCount; ++i) if (key == "key" + std::to_string(i) && value >= 0 && value <= 512 && ValidKey((int)value)) prefs.keys[i] = (int)value;
    }
    if(!savedBags) for(auto& bag:prefs.bags) { bag.primarySkin=prefs.weaponSkins[bag.primary];bag.secondarySkin=prefs.weaponSkins[bag.secondary]; }
}
bool Save() {
    std::ofstream out("vanta-settings.cfg");
    out << "master " << prefs.master << "\nmusic " << prefs.music << "\nsfx " << prefs.sfx << "\nvoice " << prefs.voice
        << "\neffects " << prefs.effects << "\nfps " << prefs.showFps << "\ndifficulty " << prefs.difficulty << '\n';
    for (int i = 0; i < ActionCount; ++i) out << "key" << i << ' ' << prefs.keys[i] << '\n';
    for(int i=0;i<WeaponCount;++i) out << "weaponSkin" << i << ' ' << prefs.weaponSkins[i] << '\n';
    out << "activeBag " << prefs.activeBag << '\n';
    for(int i=0;i<Bags::Count;++i) {
        const auto& b=prefs.bags[i];std::string p="bag"+std::to_string(i);
        out<<p<<"Primary "<<b.primary<<'\n'<<p<<"Secondary "<<b.secondary<<'\n'
           <<p<<"PrimarySkin "<<b.primarySkin<<'\n'<<p<<"SecondarySkin "<<b.secondarySkin<<'\n'
           <<p<<"Melee "<<b.melee<<'\n'<<p<<"MeleeSkin "<<b.meleeSkin<<'\n'
           <<p<<"Grenade "<<b.grenade<<'\n'<<p<<"Smoke "<<b.smoke<<'\n'<<p<<"Flash "<<b.flash<<'\n';
    }
    out.flush(); return out.good();
}
bool DownKey(Action a) { return !BlocksInput() && IsKeyDown(prefs.keys[a]); }
bool Pressed(Action a) { return !BlocksInput() && IsKeyPressed(prefs.keys[a]); }
const char* KeyLabel(Action a) {
    int k = prefs.keys[a];
    if (k == KEY_SPACE) return "SPACE";
    if (k == KEY_LEFT_SHIFT || k == KEY_RIGHT_SHIFT) return "SHIFT";
    if (k == KEY_LEFT_CONTROL || k == KEY_RIGHT_CONTROL) return "CTRL";
    if (k == KEY_LEFT_ALT || k == KEY_RIGHT_ALT) return "ALT";
    if (k >= 32 && k <= 96) return TextFormat("%c", k);
    return TextFormat("KEY %d", k);
}
static bool Click(Rectangle r) { return CheckCollisionPointRec(Ui::RemapMouseToVirtual(), r) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON); }
void Update() {
    blocked = opened;
    if (!opened) return;
    if (binding >= 0) {
        int k = GetKeyPressed();
        if (k == KEY_ESCAPE) binding = -1;
        else if (ValidKey(k) && k != KEY_ONE && k != KEY_TWO && k != KEY_THREE && k != KEY_FOUR && k != KEY_FIVE) {
            int old = prefs.keys[binding];
            for (int i = 0; i < ActionCount; ++i) if (prefs.keys[i] == k) prefs.keys[i] = old;
            prefs.keys[binding] = k; binding = -1;
        } else if (k) status = "That key is reserved. Choose a letter, modifier or space.";
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE) || Click({890, 94, 80, 32})) {
        if (Save()) opened = false; else status = "Could not save settings. Check folder write access.";
        return;
    }
    float* volumes[] = {&prefs.master, &prefs.music, &prefs.sfx, &prefs.voice};
    for (int i = 0; i < 4; ++i) {
        Rectangle r{350, 190.0f + i * 60, 250, 20};
        Vector2 m = Ui::RemapMouseToVirtual();
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(m, {r.x, r.y - 10, r.width, 40})) *volumes[i] = std::clamp((m.x - r.x) / r.width, 0.0f, 1.0f);
    }
    if (Click({350, 435, 250, 34})) prefs.effects = !prefs.effects;
    if (Click({350, 480, 250, 34})) prefs.showFps = !prefs.showFps;
    if (Click({350, 525, 250, 34})) prefs.difficulty = (prefs.difficulty + 1) % 3;
    for (int i = 0; i < ActionCount; ++i) if (Click({680, 161.0f + i * 27, 270, 25})) binding = i;
    if (Click({680, 548, 270, 27})) { prefs = Preferences{}; status = "Defaults restored. Close to save."; }
}
void Draw() {
    if (!opened) return;
    DrawRectangle(0, 0, VIRTUAL_WIDTH, VIRTUAL_HEIGHT, Fade(BLACK, 0.78f));
    DrawRectangleRec({320, 80, 660, 530}, {12, 18, 29, 255});
    DrawRectangle(320, 80, 660, 3, UiTheme::AccentGold());
    DrawText("SETTINGS", 350, 103, 26, RAYWHITE);
    DrawText("CLOSE", 894, 104, 16, UiTheme::AccentGold());
    DrawText("AUDIO & DISPLAY", 350, 150, 16, UiTheme::AccentGold());
    DrawText("CONTROLS  /  CLICK TO REBIND", 680, 137, 14, UiTheme::AccentGold());
    const char* labels[] = {"MASTER", "MUSIC", "SOUND EFFECTS", "VOICE"};
    float values[] = {prefs.master, prefs.music, prefs.sfx, prefs.voice};
    for (int i = 0; i < 4; ++i) {
        int y = 175 + i * 60;
        DrawText(labels[i], 350, y - 7, 14, LIGHTGRAY);
        DrawText(TextFormat("%d%%", (int)(values[i] * 100)), 555, y - 7, 14, RAYWHITE);
        DrawRectangle(350, y + 15, 250, 10, {38, 48, 65, 255});
        DrawRectangle(350, y + 15, (int)(250 * values[i]), 10, UiTheme::AccentGold());
    }
    DrawText(prefs.effects ? "CAMERA EFFECTS   ON" : "CAMERA EFFECTS   OFF", 350, 444, 16, RAYWHITE);
    DrawText(prefs.showFps ? "FPS COUNTER      ON" : "FPS COUNTER      OFF", 350, 489, 16, RAYWHITE);
    const char* difficulty[] = {"EASY", "MEDIUM", "DIFFICULT"};
    DrawText(TextFormat("PRACTICE: %s", difficulty[prefs.difficulty]), 350, 534, 16, RAYWHITE);
    const char* actions[] = {"Move up", "Move down", "Move left", "Move right", "Jump", "Dash", "Shield", "Reload", "Push to talk", "Grenade", "Smoke grenade", "Interact / pickup", "Flashbang", "Special throw"};
    for (int i = 0; i < ActionCount; ++i) {
        int y = 161 + i * 27;
        DrawRectangle(680, y, 270, 25, binding == i ? Color{70, 55, 25, 255} : Color{24, 33, 47, 255});
        DrawText(actions[i], 690, y + 6, 16, LIGHTGRAY);
        DrawText(binding == i ? "..." : KeyLabel((Action)i), 867, y + 7, 14, UiTheme::AccentGold());
    }
    DrawText("RESTORE DEFAULTS", 717, 556, 14, LIGHTGRAY);
    DrawText(status.empty() ? "F10 SETTINGS   /   ESC SAVE & CLOSE" : status.c_str(), 350, 586, 12, LIGHTGRAY);
}
}
