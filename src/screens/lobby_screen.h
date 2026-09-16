#ifndef LOBBY_SCREEN_H
#define LOBBY_SCREEN_H

#include "iscreen.h"
#include "menu_background.h"
#include "../ui/character_preview.h"
#include "../network/network_manager.h"
#include <string>
#include <raylib.h>

class LobbyScreen : public IScreen {
public:
    LobbyScreen();
    ~LobbyScreen() override;

    bool Update(float deltaTime) override;
    void Draw(RenderTexture2D target) override;
    bool ShouldStartGame() const { return startGame; }

private:
    bool startGame;
    bool isHost;
    std::string playerName;
    std::string lanAddress;
    int playerSkin;

    Font menuFont;
    Sound choiceSound;
    MenuBackground* background;
    CharacterPreview* preview;

    float fadeIn;
    float blurStrength;
    float hoverReady;
    float hoverStart;
    float hoverCopy;
    float hoverGlobalBack;
    float optionScroll;
    float optionScrollMax;

    Rectangle shell;
    Rectangle rosterPanel;
    Rectangle readyBtn;
    Rectangle startBtn;
    Rectangle copyBtn;

    // ---- Map cycle selector ----
    Rectangle prevMapBtn;
    Rectangle nextMapBtn;
    Rectangle mapDisplay;
    Rectangle mapPreview;
    float hoverPrevMap;
    float hoverNextMap;
    RenderTexture2D cachedPreview;
    std::string cachedPreviewName;

    // ---- Mode cycle selector (< MODE >) ----
    Rectangle prevModeBtn;  // left arrow
    Rectangle nextModeBtn;  // right arrow
    Rectangle modeDisplay;  // center label area
    float hoverPrevMode;
    float hoverNextMode;

    // ---- Chip rows for win/time/kill settings ----
    Rectangle chipWin[3];
    Rectangle chipGoal[2]; // KILLS or TIME
    Rectangle chipLimit[3]; // The limit values (40/60/100 or 5m/10m/15m)
    float    hoverChipWin[3];
    float    hoverChipGoal[2];
    float    hoverChipLimit[3];

    // ---- Per-player team-switch pills (host-only for non-self rows) ----
    static constexpr int MAX_LOBBY_PLAYERS = 16;
    Rectangle switchGR[MAX_LOBBY_PLAYERS];
    Rectangle switchBL[MAX_LOBBY_PLAYERS];
    float    hoverSwitchGR[MAX_LOBBY_PLAYERS];
    float    hoverSwitchBL[MAX_LOBBY_PLAYERS];

    void DrawRoster();
    void DrawModeSelector(); // replaces DrawModeCards
    void DrawChipRows();
    void DrawMapCards(); // wrapper retained for layout compatibility
};

#endif // LOBBY_SCREEN_H
