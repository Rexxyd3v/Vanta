#ifndef LOBBY_SCREEN_H
#define LOBBY_SCREEN_H
#include "iscreen.h"
#include "menu_background.h"
#include "../ui/character_preview.h"
#include "../ui/match_setup_ui.h"
#include "../network/network_manager.h"
#include <memory>
#include <string>

class LobbyScreen : public IScreen {
public:
    LobbyScreen();
    ~LobbyScreen() override;
    bool Update(float dt) override;
    void Draw(RenderTexture2D target) override;
    bool ShouldStartGame() const { return startGame; }
private:
    bool startGame=false, isHost=false;
    Font font{};
    std::unique_ptr<MenuBackground> background;
    std::unique_ptr<CharacterPreview> portraits;
    MapPreview mapPreview;
    std::string lanAddress;
    float fade=0, hoverReady=0, hoverStart=0, hoverCopy=0, hoverBack=0;
    float hoverMode[4]{}, hoverMap[2]{};
    int rosterScroll[2]{};
    Rectangle modeRows[4]{};
    Rectangle ready{646,486,550,44}, start{646,540,550,54}, copy{646,636,550,28}, back{76,644,170,26};
    Rectangle switchGR[16]{}, switchBL[16]{};
    bool Click(Rectangle r) const;
    void DrawRoster();
    void RosterLayout();
    void CycleMap(int step);
};
#endif
