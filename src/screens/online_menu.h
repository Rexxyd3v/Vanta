#ifndef ONLINE_MENU_H
#define ONLINE_MENU_H

#include "iscreen.h"
#include "menu_background.h"
#include "../ui/character_preview.h"
#include "../ui/weapon_skin_preview.h"
#include "../ui/loadout_storage.h"
#include <string>
#include <raylib.h>


class OnlineMenuScreen : public IScreen {
public:
    OnlineMenuScreen();
    ~OnlineMenuScreen() override;

    bool Update(float deltaTime) override;
    void Draw(RenderTexture2D target) override;

    bool ShouldStartGame() const { return startGame; }
    bool ShouldGoToLobby() const { return goToLobby; }

private:
    LoadoutStorage storage;
    bool storageOpen=false;
    bool startGame;
    bool goToLobby;
    int activeInput; // 0 = none, 1 = username, 2 = join address
    std::string joinAddress;
    std::string username;
    bool isConnecting;

    std::string connectError;

    Font menuFont;
    MenuBackground* background;
    CharacterPreview* preview;
    WeaponSkinPreview* skinPreview;

    float fadeIn;
    float blurStrength;
    float hoverHost;
    float hoverJoin;
    float hoverGlobalBack;
    float hoverTabFighter;
    float hoverTabLoadout;
    float caretBlink;

    int currentSkin; // character skin
    int currentGunSkin; // gun skin
    int currentLeftTab; // 0 = FIGHTER, 1 = LOADOUT

    Rectangle nameField;
    Rectangle hostBtn;
    Rectangle joinBtn;
    Rectangle shell;
    Rectangle stageArea;
    Rectangle selectorArea;
    Rectangle gunPreviewArea;
    Rectangle gunSelectorArea;
};

#endif // ONLINE_MENU_H
