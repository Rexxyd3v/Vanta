#ifndef LOBBY_SCREEN_H
#define LOBBY_SCREEN_H
#include "iscreen.h"
#include "menu_background.h"
#include "../ui/character_preview.h"
#include "../ui/match_setup_ui.h"
#include "../ui/loadout_storage.h"
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
    friend struct LobbyTestAccess;
    bool startGame=false, isHost=false,storageOpen=false;
    LoadoutStorage storage;
    Font font{};
    std::unique_ptr<MenuBackground> background;
    std::unique_ptr<CharacterPreview> portraits;
    MapPreview mapPreview;
    std::string lanAddress;
    float fade=0, hoverReady=0, hoverStart=0, hoverCopy=0, hoverBack=0;
    float hoverMode[4]{}, hoverMap[2]{};
    int rosterScroll[2]{};
    Rectangle modeRows[4]{};
    Rectangle ready{646,542,550,40}, start{646,590,550,50}, copy{928,648,268,28}, back{76,644,170,26};
    Rectangle switchGR[16]{}, switchBL[16]{};
    Rectangle previewBounds{646,146,550,246}, setupViewport{646,432,550,96};
    float setupScroll=0, scrollGrab=0;
    bool draggingScroll=false;
    float SetupContentHeight() const;
    float SetupScrollMax() const;
    void ScrollSetup(float amount);
    Rectangle SetupBounds(Rectangle local) const;
    Rectangle ScrollThumb() const;
    bool SetupHit(Rectangle local,Vector2 mouse) const;
    bool ClickSetup(Rectangle local) const;
    bool Click(Rectangle r) const;
    void DrawRoster();
    void RosterLayout();
    void CycleMap(int step);
};
#endif
