#include "main_menu.h"
#include "../ui/game_settings.h"
#include "../ui/ui_widgets.h"
#include "../network/network_manager.h"
#include <cmath>
#include <algorithm>

MainMenuScreen::MainMenuScreen() {
    menuFont = LoadFontEx("assets/fonts/BruceForeverRegular-X3jd2.ttf", 80, nullptr, 0);
    if (menuFont.texture.id == 0) menuFont = GetFontDefault();

    selectedMode     = GameMode::NONE;
    hoverOnline      = 0.0f;
    uiRise           = 0.0f;
    keyboardNavIndex = -1;
    background=std::make_unique<MenuBackground>(std::clamp(NetworkManager::GetInstance().localSkinIndex,1,4),MenuBackground::BackdropStyle::LIVE_GAMEPLAY);

    cardOnline = {80, 412, 550, 130};
    cardOffline = {716, 440, 504, 102};

    fadeInTimer  = -0.6f;
    uiRise       =  0.0f;

    mainMenuMusic = LoadMusicStream("assets/sounds/MAINSOUNDTRACK.wav");
    SetMusicVolume(mainMenuMusic, 0.0f);
    PlayMusicStream(mainMenuMusic);

    choiceSound = LoadSound("assets/sounds/choice.wav");
}

MainMenuScreen::~MainMenuScreen() {
    if (menuFont.texture.id != GetFontDefault().texture.id) UnloadFont(menuFont);
    UnloadSound(choiceSound);
    UnloadMusicStream(mainMenuMusic);
}

bool MainMenuScreen::Update(float deltaTime) {
    UpdateMusicStream(mainMenuMusic);
    if (IsKeyPressed(KEY_ESCAPE)) { selectedMode = GameMode::NONE; return false; }
    if (IsKeyPressed(KEY_P)) { selectedMode = GameMode::PRACTICE; return false; }
    Vector2 navMouse = Ui::RemapMouseToVirtual();
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (CheckCollisionPointRec(navMouse, {80, 570, 190, 40})) { Settings::Open(); return true; }
    }

    fadeInTimer += deltaTime / 1.6f;
    if (fadeInTimer > 1.0f) fadeInTimer = 1.0f;

    float visibleAlpha = (fadeInTimer < 0.0f) ? 0.0f : fadeInTimer;
    SetMusicVolume(mainMenuMusic, visibleAlpha * Settings::Get().music);

    uiRise       = Ui::Approach(uiRise,       1.0f,  2.2f,  deltaTime);
    background->Update(deltaTime,visibleAlpha);

    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) ||
        IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
        keyboardNavIndex = keyboardNavIndex == 1 ? 0 : 1;

    Vector2 mouseDelta = GetMouseDelta();
    if (fabsf(mouseDelta.x) > 1.0f || fabsf(mouseDelta.y) > 1.0f)
        keyboardNavIndex = -1;

    Vector2 mouse = Ui::RemapMouseToVirtual();
    bool hOn = CheckCollisionPointRec(mouse, cardOnline) || (keyboardNavIndex == 1);
    bool hOff = CheckCollisionPointRec(mouse, cardOffline) || (keyboardNavIndex == 0);
    hoverOffline = Ui::Approach(hoverOffline, hOff ? 1.0f : 0.0f, 10.0f, deltaTime);
    hoverOnline = Ui::Approach(hoverOnline, hOn ? 1.0f : 0.0f, 10.0f, deltaTime);

    bool confirm = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) ||
                   (IsKeyPressed(KEY_ENTER) || (keyboardNavIndex >= 0 && IsKeyPressed(KEY_SPACE)));
    if (confirm && hOff) { selectedMode = GameMode::PRACTICE; PlaySound(choiceSound); return false; }
    if (confirm && (hOn || IsKeyPressed(KEY_ENTER))) {
        selectedMode = GameMode::ONLINE;
        PlaySound(choiceSound);
        return false;
    }

    return true;
}

void MainMenuScreen::Draw(RenderTexture2D target) {
    const float alpha=std::max(0.0f,fadeInTimer)*uiRise;
    const float rise=(1-uiRise)*22;
    const Color ink{10,16,23,255}, white{244,239,225,255}, gold{227,181,91,255}, muted{177,190,199,255};
    background->Draw(target,1,0.16f);
    BeginTextureMode(target);
    // Keep the moving arena visible on the right; shade only where menu text needs it.
    DrawRectangle(0,0,VIRTUAL_WIDTH,VIRTUAL_HEIGHT,Fade(ink,0.13f));
    DrawRectangleGradientH(0,0,900,VIRTUAL_HEIGHT,Fade(ink,0.94f),Fade(ink,0));
    DrawRectangleGradientV(0,0,VIRTUAL_WIDTH,140,Fade(ink,0.65f),Fade(ink,0));
    DrawRectangleGradientV(0,330,VIRTUAL_WIDTH,350,Fade(ink,0),Fade(ink,0.98f));
    auto body=[&](const char* text,float x,float y,int size,Color color) {
        DrawText(text,(int)x+1,(int)(y+rise)+1,size,Fade(ink,alpha*0.75f));
        DrawText(text,(int)x,(int)(y+rise),size,Fade(color,alpha));
    };
    auto brand=[&](const char* text,Vector2 p,float size,float spacing,Color color) {
        p.y+=rise;
        DrawTextEx(menuFont,text,{p.x+3,p.y+4},size,spacing,Fade(ink,alpha*0.7f));
        DrawTextEx(menuFont,text,p,size,spacing,Fade(color,alpha));
    };
    DrawLineEx({81,45},{90,62},3,Fade(gold,alpha));
    DrawLineEx({90,62},{99,45},3,Fade(gold,alpha));
    body("VANTA / ARENA COMBAT",117,48,12,muted);
    body("@rjltyd",1130,48,14,gold);
    body("AIM. MOVE. SURVIVE.",84,133,12,gold);
    brand("VANTA",{73,157},150,5,white);
    DrawLineEx({84,311+rise},{154,311+rise},3,Fade(gold,alpha));
    body("MAKE EVERY SHOT COUNT.",174,305,15,muted);
    for (int i=0;i<2;++i) {
        Rectangle r=i==0?cardOnline:cardOffline;
        float hover=i==0?hoverOnline:hoverOffline;
        Color accent=i==0?gold:white;
        float y=r.y+rise;
        DrawRectangleGradientH((int)r.x,(int)y,(int)r.width,(int)r.height,Fade(accent,hover*0.055f*alpha),Fade(accent,0));
        body(i==0?"01 / MULTIPLAYER":"02 / LOCAL PLAY",r.x+4,r.y+3,11,muted);
        brand(i==0?"PLAY ONLINE":"PLAY OFFLINE",{r.x+hover*8,r.y+29},i==0?34.0f:27.0f,1,hover>0.05f?gold:white);
        body(i==0?"HOST A ROOM. BRING YOUR SQUAD.":"YOUR BOTS. YOUR RULES.",r.x+4,r.y+78,12,muted);
        DrawLineEx({r.x,y+r.height},{r.x+r.width,y+r.height},1,Fade(white,0.15f*alpha));
        DrawLineEx({r.x,y+r.height},{r.x+66+hover*(r.width-66),y+r.height},2,Fade(accent,alpha));
        float x=r.x+r.width-24+hover*5,arrowY=y+48;
        DrawLineEx({x-17,arrowY},{x,arrowY},2,Fade(accent,alpha));
        DrawLineEx({x-7,arrowY-7},{x,arrowY},2,Fade(accent,alpha));
        DrawLineEx({x,arrowY},{x-7,arrowY+7},2,Fade(accent,alpha));
    }
    bool settingsHover=CheckCollisionPointRec(Ui::RemapMouseToVirtual(),{80,570,190,40});
    body("SETTINGS / F10",84,584,13,settingsHover?gold:muted);
    DrawLineEx({80,634},{1220,634},1,Fade(white,0.1f*alpha));
    body("ESC  EXIT",84,651,10,muted);
    body("ARROWS  NAVIGATE    ENTER  SELECT    P  OFFLINE",775,651,10,muted);
    DrawRectangle(0,0,VIRTUAL_WIDTH,VIRTUAL_HEIGHT,Fade(ink,1-alpha));
    EndTextureMode();
}
