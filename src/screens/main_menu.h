#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include <raylib.h>
#include "iscreen.h"
#include "menu_background.h"
#include "../constants.h"

class MainMenuScreen : public IScreen {
public:
    MainMenuScreen();
    ~MainMenuScreen();

    bool Update(float deltaTime) override;
    void Draw(RenderTexture2D target) override;

    GameMode GetSelectedMode() const { return selectedMode; }

private:
    Font menuFont;
    GameMode selectedMode;

    Rectangle cardOnline;

    float hoverOnline;

    int keyboardNavIndex; // -1 = mouse, 1 = Online

    Music mainMenuMusic;
    Sound choiceSound;

    MenuBackground* background;

    float fadeInTimer;
    float blurStrength;
    float uiRise;
    float glitchTimer;       // total elapsed time for glitch timing
    float glitchIntensity;   // 0..1, spikes randomly
    float glitchCooldown;    // countdown until next glitch burst
};

#endif // MAIN_MENU_H
