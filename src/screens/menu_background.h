#ifndef MENU_BACKGROUND_H
#define MENU_BACKGROUND_H

#include <raylib.h>
#include <vector>
#include "../entities/bot_enemy.h"
#include "../map_loader/MapRegistry.h"
#include "../map_loader/scenes/map_scene.h"

// MenuBackground
//
// Backdrop for menu screens using loaded TMX maps from MapRegistry.
class MenuBackground {
public:
    enum class BackdropStyle {
        LIVE_GAMEPLAY   = 0,
        CINEMATIC_BEACH = 1
    };

    MenuBackground(int localSkinIndex, BackdropStyle style = BackdropStyle::CINEMATIC_BEACH);

    ~MenuBackground();

    void Update(float deltaTime, float fadeInAlpha);

    // Draw onto `target` with alpha and blurStrength (0 = sharp, 1 = heavy blur).
    void Draw(RenderTexture2D target, float alpha, float blurStrength);

private:
    friend struct MenuBackgroundTestAccess;
    bool useCinematic = true;
    bool useParallaxBackdrop = false;

    std::vector<BotEnemy*> bots;

    Texture2D bgTex;

    Camera2D camera;

    RenderTexture2D sceneRT;
    RenderTexture2D blurRT1;
    RenderTexture2D blurRT2;

    MapData* currentMapData = nullptr;

    float worldTime;

    Vector2 fightFocus;
    float   broadcastTimer;
    float   cameraZoom;
    float   cameraZoomTarget;
    float   cameraZoomTimer;

    // Cinematic drift
    Vector2 cinematicBase;
    float   driftPhase;

    void InitWorld();
    void InitEntities(int localSkinIndex);
    void TickWorld(float deltaTime);
    void TickCinematicCamera(float deltaTime);
    void DrawScene();
    Character* GetNearestTargetForBot(BotEnemy* b);
};

#endif // MENU_BACKGROUND_H
