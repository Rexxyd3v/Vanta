#pragma once
#include "iscreen.h"
#include "menu_background.h"
#include "../ui/offline_setup.h"
#include "../ui/match_setup_ui.h"
#include <memory>

class OfflineMenuScreen : public IScreen {
public:
    OfflineMenuScreen();
    ~OfflineMenuScreen() override;
    bool Update(float dt) override;
    void Draw(RenderTexture2D target) override;
    bool ShouldStartGame() const { return startGame; }
private:
    bool startGame=false;
    Font font{};
    MapPreview mapPreview;
    std::unique_ptr<MenuBackground> background;
    float fade=0, hoverStart=0, hoverBack=0;
    float hoverMode[5]{};
    Rectangle modeRows[5]{};
    Rectangle start{646,590,550,50}, back{76,611,170,32};
    bool Click(Rectangle r) const;
    void CycleMap(int step);
};
