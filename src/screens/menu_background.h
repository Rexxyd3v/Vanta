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
    struct DemoAgent { Vector2 home{},destination{}; float retarget=0,fire=0,skill=3; };
    struct DemoGrenade { Vector2 start{},target{}; float age=0; int kind=0; bool exploded=false; };
    std::vector<DemoAgent> agents;
    std::vector<DemoGrenade> ordnance;
    float ordnanceTimer=.3f;
    int ordnanceSequence=0,shotSequence=0;
    float blastShake=0;
    int cameraSubject=-1, previousCameraSubject=-1;
    float shotElapsed=0, shotDuration=4.5f;
    float shotZoomStart=.52f, shotZoomEnd=.68f;
    float shotRollStart=-1.2f, shotRollEnd=1.0f;
    Vector2 shotPanStart{},shotPanEnd{};
    Camera2D RenderCamera() const;
    Vector2 WorldSize() const;
    void ResolveDemoCollision(Character* actor);
    void PlaceSpaced(BotEnemy* actor,Vector2 desired);
    void DrawOrdnance() const;

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
