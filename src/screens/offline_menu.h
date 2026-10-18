#pragma once
#include "iscreen.h"
#include "menu_background.h"
#include "../ui/offline_setup.h"
#include "../ui/match_setup_ui.h"
#include <memory>
#include "../ui/weapon_skin_preview.h"

class OfflineMenuScreen : public IScreen {
public:
    OfflineMenuScreen();
    ~OfflineMenuScreen() override;
    bool Update(float dt) override;
    void Draw(RenderTexture2D target) override;
    bool ShouldStartGame() const { return startGame; }
private:
    friend struct OfflineMenuTestAccess;
    bool startGame=false,loadoutOpen=false;
    std::unique_ptr<WeaponSkinPreview> loadout;
    Font font{};
    MapPreview mapPreview;
    std::unique_ptr<MenuBackground> background;
    float fade=0, hoverStart=0, hoverBack=0;
    float hoverMode[5]{};
    Rectangle modeRows[5]{};
    Rectangle start{646,590,550,50}, back{76,611,170,32};
    Rectangle previewBounds{646,146,550,246}, setupViewport{646,410,550,164};
    float setupScroll=0,scrollGrab=0;
    bool draggingScroll=false;
    float SetupContentHeight() const;
    float SetupScrollMax() const;
    void ScrollSetup(float delta);
    Rectangle SetupBounds(Rectangle local) const;
    Rectangle ScrollThumb() const;
    bool SetupHit(Rectangle local,Vector2 mouse) const;
    bool ClickSetup(Rectangle local) const;
    bool Click(Rectangle r) const;
    void CycleMap(int step);
};
