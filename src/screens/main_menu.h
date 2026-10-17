#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include <raylib.h>
#include "iscreen.h"
#include "../constants.h"
#include "menu_background.h"
#include <memory>

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
    Rectangle cardOffline;

    float hoverOnline;
    float hoverOffline = 0;

    int keyboardNavIndex; // -1 = mouse, 1 = Online

    Music mainMenuMusic;
    Sound choiceSound;
    std::unique_ptr<MenuBackground> background;
    float fadeInTimer;
    float uiRise;

};

#endif // MAIN_MENU_H
