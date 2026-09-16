#include "menu_background.h"
#include "../constants.h"
#include "../utils/texture_manager.h"
#include "../network/network_manager.h"
#include "../entities/projectile.h"
#include "../entities/character.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <climits>

static inline float fclamp(float val, float lo, float hi) {
    return val < lo ? lo : (val > hi ? hi : val);
}



// --- Hit / collision helpers (mirrors main_level.cpp) ------------------------

static const float HIT_HALF_W   = 22.0f;
static const float HIT_TOP_OFF  =  0.0f;
static const float HIT_BOT_OFF  = 60.0f;
static const float PROJ_RADIUS  =  3.0f;

static inline bool ProjectileHitsAABB(Vector2 pp, Vector2 cc) {
    float left   = cc.x - HIT_HALF_W;
    float right  = cc.x + HIT_HALF_W;
    float top    = cc.y + HIT_TOP_OFF;
    float bottom = cc.y + HIT_BOT_OFF;
    float cx = (pp.x < left) ? left : (pp.x > right ? right : pp.x);
    float cy = (pp.y < top)  ? top  : (pp.y > bottom ? bottom : pp.y);
    float dx = pp.x - cx;
    float dy = pp.y - cy;
    return (dx*dx + dy*dy) <= PROJ_RADIUS * PROJ_RADIUS;
}

static inline void ResolveCollisionPush(Character* c, Rectangle cb, Rectangle ob) {
    float overlapLeft   = cb.x + cb.width  - ob.x;
    float overlapRight  = (ob.x + ob.width) - cb.x;
    float overlapTop    = cb.y + cb.height - ob.y;
    float overlapBottom = (ob.y + ob.height) - cb.y;
    bool separateX = (std::min(overlapLeft, overlapRight) < std::min(overlapTop, overlapBottom));
    if (separateX) {
        if (overlapLeft < overlapRight) {
            c->SetPosition({ c->GetPosition().x - overlapLeft, c->GetPosition().y });
        } else {
            c->SetPosition({ c->GetPosition().x + overlapRight, c->GetPosition().y });
        }
    } else {
        if (overlapTop < overlapBottom) {
            c->SetPosition({ c->GetPosition().x, c->GetPosition().y - overlapTop });
        } else {
            c->SetPosition({ c->GetPosition().x, c->GetPosition().y + overlapBottom });
        }
    }
}

// --- MenuBackground ---------------------------------------------------------

MenuBackground::MenuBackground(int localSkinIndex, BackdropStyle style) {
    worldTime = 0.0f;

    // Broadcast camera initial state
    fightFocus     = { WORLD_WIDTH / 2.0f, WORLD_HEIGHT / 2.0f };
    broadcastTimer = 0.0f;
    cameraZoom     = 1.0f;
    cameraZoomTarget = 1.0f;
    cameraZoomTimer  = 0.0f;

    // Enable professional demo-mode AI for all bots (menu-only, reverted in destructor)
    BotEnemy::SetDemoMode(true);
    Character::SetCombatAudioEnabled(false); // Silence gun SFX on menu

    // Parallax option not present in this build; keep disabled.
    (void)style;
    useParallaxBackdrop = false;


    // Off-screen render targets
    sceneRT = LoadRenderTexture(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
    SetTextureFilter(sceneRT.texture, TEXTURE_FILTER_BILINEAR);

    blurRT1 = LoadRenderTexture(VIRTUAL_WIDTH / 4, VIRTUAL_HEIGHT / 4);
    SetTextureFilter(blurRT1.texture, TEXTURE_FILTER_BILINEAR);

    blurRT2 = LoadRenderTexture(VIRTUAL_WIDTH / 8, VIRTUAL_HEIGHT / 8);
    SetTextureFilter(blurRT2.texture, TEXTURE_FILTER_BILINEAR);

    // Camera starts at world center
    camera.target   = { WORLD_WIDTH / 2.0f, WORLD_HEIGHT / 2.0f };
    camera.offset   = { VIRTUAL_WIDTH / 2.0f, VIRTUAL_HEIGHT / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom     = 1.0f;

    InitWorld();
    InitEntities(localSkinIndex);
}

MenuBackground::~MenuBackground() {
    // Restore normal gameplay AI before we go
    BotEnemy::SetDemoMode(false);
    Character::SetCombatAudioEnabled(true); // Restore weapon SFX for gameplay
    for (auto b : bots) delete b;
    UnloadRenderTexture(sceneRT);
    UnloadRenderTexture(blurRT1);
    UnloadRenderTexture(blurRT2);
}

void MenuBackground::InitWorld() {
    auto mapNames = MapRegistry::GetInstance().GetMapNames();
    std::string mapName = "Forest";
    if (!mapNames.empty()) {
        int randomIndex = GetRandomValue(0, (int)mapNames.size() - 1);
        mapName = mapNames[randomIndex];
    }
    currentMapData = MapRegistry::GetInstance().GetMap(mapName);

    std::string basePath = currentMapData ? currentMapData->folderPath : "assets/Maps/Forest/";
    std::string bgName   = (currentMapData && !currentMapData->scene.bgTexture.empty()) ? currentMapData->scene.bgTexture : "background.png";

    bgTex = TextureManager::GetTexture(basePath + bgName);
}

void MenuBackground::InitEntities(int localSkinIndex) {
    (void)localSkinIndex; // Pure spectator: no local player skin needed

    std::string charPath =
        "assets/Free 2D Animated Vector Game Character Sprites/"
        "Free 2D Animated Vector Game Character Sprites/"
        "Full body animated characters/";

    // 6 ranged bots + 4 monsters in tight clusters so the camera always
    // sees active combat.  Clusters are placed across the world so the
    // broadcast camera has interesting areas to cut between.
    const char* rangedSkins[4] = {
        "Char 1/with hands/", "Char 2/with hands/",
        "Char 3/with hands/", "Char 4/with hands/"
    };
    const char* monsterSkins[4] = {
        "Enemies/Enemy 1/", "Enemies/Enemy 2/",
        "Enemies/Enemy 3/", "Enemies/Enemy 4/"
    };

    // Three combat clusters spread across the map
    struct ClusterDef { Vector2 center; float radius; };
    const ClusterDef clusters[] = {
        { { 900.0f,   800.0f }, 280.0f },   // left cluster
        { { 2000.0f, 1000.0f }, 300.0f },   // centre cluster
        { { 3200.0f,  700.0f }, 280.0f },   // right cluster
    };

    // Assign 2 ranged bots to each cluster (indices 0-1, 2-3, 4-5)
    for (int i = 0; i < 6; i++) {
        const ClusterDef& cl = clusters[i / 2];
        float angle = (float)(i % 2) * 3.14159f + GetRandomValue(0, 31) * 0.1f;
        Vector2 pos = {
            cl.center.x + cosf(angle) * cl.radius,
            cl.center.y + sinf(angle) * cl.radius
        };
        BotEnemy* b = new BotEnemy(pos, charPath + rangedSkins[i % 4]);
        b->SetName(rangedSkins[i % 4]);
        bots.push_back(b);
    }
    // One monster per cluster (3 clusters → 3 monsters) + an extra monster
    for (int i = 0; i < 4; i++) {
        const ClusterDef& cl = clusters[i % 3];
        float angle = (float)i * 1.57f;
        Vector2 pos = {
            cl.center.x + cosf(angle) * cl.radius * 0.6f,
            cl.center.y + sinf(angle) * cl.radius * 0.6f
        };
        BotEnemy* m = new BotEnemy(pos, charPath + monsterSkins[i]);
        m->SetName(monsterSkins[i]);
        bots.push_back(m);
    }
}

// Returns the nearest living bot that b can target.
// In spectator mode there is no player, so bots only target each other.
Character* MenuBackground::GetNearestTargetForBot(BotEnemy* b) {
    Character* nearest = nullptr;
    float minDist = 1e9f;

    for (auto other : bots) {
        if (other == b || other->IsDead()) continue;
        if (b->IsMonster() && other->IsMonster()) continue; // monsters don't target monsters
        float dx = other->GetPosition().x - b->GetPosition().x;
        float dy = other->GetPosition().y - b->GetPosition().y;
        float dist = dx*dx + dy*dy;
        if (dist < minDist) {
            minDist = dist;
            nearest = other;
        }
    }
    return nearest;
}

void MenuBackground::TickWorld(float deltaTime) {
    // --- Bots: AI + Update ---
    for (auto b : bots) {
        Character* target = GetNearestTargetForBot(b);
        if (target) {
            b->UpdateAI(deltaTime, target->GetPosition());
        } else {
            // No live target: wander toward a random world position
            Vector2 wander = {
                (float)GetRandomValue(300, (int)WORLD_WIDTH  - 300),
                (float)GetRandomValue(300, (int)WORLD_HEIGHT - 300)
            };
            b->UpdateAI(deltaTime, wander);
        }
        b->Update(deltaTime);

        // Respawn dead bots quickly (2s) so the spectator always sees action
        if (b->ShouldRespawn()) {
            // Respawn near an existing living bot for immediate combat, but try to avoid spawning too close to the camera
            Vector2 spawn = {
                (float)GetRandomValue(200, (int)WORLD_WIDTH  - 200),
                (float)GetRandomValue(200, (int)WORLD_HEIGHT - 200)
            };
            bool foundGoodSpawn = false;
            for (auto other : bots) {
                if (!other->IsDead() && other != b) {
                    // Try up to 5 times to find a spawn point near this bot that is far from camera
                    for (int attempt = 0; attempt < 5; attempt++) {
                        float angle = (float)GetRandomValue(0, 628) / 100.0f;
                        Vector2 candidate = {
                            other->GetPosition().x + cosf(angle) * 350.0f,
                            other->GetPosition().y + sinf(angle) * 350.0f
                        };
                        candidate.x = fclamp(candidate.x, 150.0f, WORLD_WIDTH  - 150.0f);
                        candidate.y = fclamp(candidate.y, 150.0f, WORLD_HEIGHT - 150.0f);
                        // Check distance to camera target (at least 400 units away)
                        float dx = candidate.x - camera.target.x;
                        float dy = candidate.y - camera.target.y;
                        float distSq = dx*dx + dy*dy;
                        if (distSq >= 400.0f * 400.0f) {
                            spawn = candidate;
                            foundGoodSpawn = true;
                            break; // break out of attempt loop
                        }
                    }
                    if (foundGoodSpawn) {
                        break; // break out of other loop
                    }
                    // If we didn't find a good spot for this bot, try the next bot
                }
            }
            // If we found a good spawn, we use it; otherwise, we keep the initial random spawn.
            b->ResetHealth(100.0f);
            b->GetProjectiles().clear();
            b->SetPosition(spawn);
            b->ResetDeathTimer();
            // Start fade-in effect for respawning bots in demo mode
            b->SetSpawnFadeTimer(0.4f);
        }
    }

    // --- Projectile collisions (bot-only; no player target) ---
    for (auto shooter : bots) {
        if (shooter->IsMonster()) continue;
        for (auto& p : shooter->GetProjectiles()) {
            if (!p->IsActive()) continue;
            Vector2 pp = p->GetPosition();
            for (auto t : bots) {
                if (t == shooter || t->IsDead()) continue;
                Vector2 cc = { t->GetPosition().x, t->GetPosition().y - t->GetJumpHeight() };
                if (ProjectileHitsAABB(pp, cc)) {
                    t->TakeDamage(p->GetDamage());
                    p->Deactivate();
                    break;
                }
            }
        }
    }

    // --- Monster melee: contact damage on cooldown ---
    for (auto m : bots) {
        if (!m->IsMonster()) continue;
        float minDist = 50.0f;
        Character* nearestTarget = nullptr;
        for (auto other : bots) {
            if (other == m || other->IsDead() || other->IsMonster()) continue;
            float dx = other->GetPosition().x - m->GetPosition().x;
            float dy = other->GetPosition().y - m->GetPosition().y;
            float d = sqrtf(dx*dx + dy*dy);
            if (d < minDist) { minDist = d; nearestTarget = other; }
        }
        if (nearestTarget && m->GetShootCooldown() <= 0.0f) {
            nearestTarget->TakeDamage(10.0f);
            m->SetShootCooldown(0.8f);
        }
    }

    // --- Apply horizontal velocity + resolve world collisions ---
    // UpdateAI sets velocity but does NOT move position (for demo mode).
    // We apply the movement here, then immediately push characters out of walls.
    auto resolveWorld = [&](Character* c) {
        if (!c) return;
        Rectangle cb = c->GetCollisionBounds();
        if (currentMapData && currentMapData->tmxMap) {
            for (uint32_t i = 0; i < currentMapData->tmxMap->layersLength; i++) {
                TmxLayer& layer = currentMapData->tmxMap->layers[i];
                if (layer.type == LAYER_TYPE_OBJECT_GROUP) {
                    for (uint32_t j = 0; j < layer.exact.objectGroup.objectsLength; j++) {
                        const TmxObject& obj = layer.exact.objectGroup.objects[j];
                        if (obj.type == OBJECT_TYPE_RECTANGLE) {
                            Rectangle objRect = { (float)obj.x, (float)obj.y - 32.0f, (float)obj.width, (float)obj.height };
                            if (CheckCollisionRecs(cb, objRect)) {
                                ResolveCollisionPush(c, cb, objRect);
                                cb = c->GetCollisionBounds();
                            }
                        }
                    }
                }
            }
        }
        // Clamp inside world bounds
        Vector2 p = c->GetPosition();
        p.x = fclamp(p.x, 80.0f, (float)WORLD_WIDTH  - 80.0f);
        p.y = fclamp(p.y, 80.0f, (float)WORLD_HEIGHT - 80.0f);
        c->SetPosition(p);
    };
    for (auto b : bots) resolveWorld(b);

    // --- Broadcast camera: smoothly follow the hottest fight ---
    // Re-evaluate which fight to spectate every broadcastTimer seconds.
    broadcastTimer -= deltaTime;
    if (broadcastTimer <= 0.0f) {
        // Find the pair of living bots with the smallest distance
        // (they are actively in each other's faces — most interesting fight).
        float   bestDist = 1e9f;
        Vector2 bestMid  = fightFocus;
        int     numLive  = 0;
        for (int i = 0; i < (int)bots.size(); i++) {
            if (bots[i]->IsDead()) continue;
            numLive++;
            for (int j = i + 1; j < (int)bots.size(); j++) {
                if (bots[j]->IsDead()) continue;
                float dx = bots[j]->GetPosition().x - bots[i]->GetPosition().x;
                float dy = bots[j]->GetPosition().y - bots[i]->GetPosition().y;
                float d  = dx*dx + dy*dy;
                if (d < bestDist) {
                    bestDist = d;
                    bestMid  = {
                        (bots[i]->GetPosition().x + bots[j]->GetPosition().x) * 0.5f,
                        (bots[i]->GetPosition().y + bots[j]->GetPosition().y) * 0.5f
                    };
                }
            }
        }
        if (numLive > 0) fightFocus = bestMid;
        // Stay on the same fight for 2-4 seconds then re-evaluate
        broadcastTimer = (float)GetRandomValue(20, 40) / 10.0f;

        // Randomize a new cinematic zoom target: occasionally zoom in closer
        cameraZoomTarget = (GetRandomValue(0, 1) == 0) ? 1.05f : 0.92f;
        cameraZoomTimer  = broadcastTimer;
    }

    // Smooth camera pan toward the fight focus (slow lerp = cinematic feel)
    camera.target.x += (fightFocus.x - camera.target.x) * 1.2f * deltaTime;
    camera.target.y += (fightFocus.y - camera.target.y) * 1.2f * deltaTime;

    // Clamp so the camera never shows outside the world
    float halfVW = VIRTUAL_WIDTH  / (2.0f * camera.zoom);
    float halfVH = VIRTUAL_HEIGHT / (2.0f * camera.zoom);
    camera.target.x = fclamp(camera.target.x, halfVW, (float)WORLD_WIDTH  - halfVW);
    camera.target.y = fclamp(camera.target.y, halfVH, (float)WORLD_HEIGHT - halfVH);

    // Smooth zoom
    cameraZoom += (cameraZoomTarget - cameraZoom) * 0.8f * deltaTime;
    camera.zoom = cameraZoom;
}

void MenuBackground::Update(float deltaTime, float fadeInAlpha) {
    (void)fadeInAlpha; // reserved for future hooks
    worldTime += deltaTime;

    if (!useParallaxBackdrop) {
        TickWorld(deltaTime);
    }
}


void MenuBackground::DrawScene() {
    BeginTextureMode(sceneRT);

    if (useParallaxBackdrop) {
        // --- Menu backdrop: dusk gradient + slow parallax tile scrolling ---
        for (int strip = 0; strip < WORLD_HEIGHT; strip += 80) {
            float t = (float)strip / (float)WORLD_HEIGHT;
            unsigned char r = (unsigned char)(25 + t * 25);      // deeper pink
            unsigned char g = (unsigned char)(10 + t * 12);
            unsigned char b = (unsigned char)(45 + t * 35);
            DrawRectangle(0, strip, WORLD_WIDTH, 80, { r, g, b, 255 });
        }

        // soft sun glow (screen-space inside worldRT)
        float sunX = WORLD_WIDTH * 0.65f + sinf(worldTime * 0.10f) * 80.0f;
        float sunY = WORLD_HEIGHT * 0.25f;
        DrawCircle((int)sunX, (int)sunY, 220, { 255, 180, 80, 35 });
        DrawCircle((int)sunX, (int)sunY, 160, { 255, 210, 120, 25 });

        // Camera drift (no follow needed)
        camera.target = { WORLD_WIDTH / 2.0f, WORLD_HEIGHT / 2.0f };
        camera.offset = { VIRTUAL_WIDTH / 2.0f, VIRTUAL_HEIGHT / 2.0f };
        BeginMode2D(camera);

        if (bgTex.id != 0 && bgTex.width > 0) {
            // layer speeds
            float offFar  = worldTime * 12.0f;
            float offNear = worldTime * 28.0f;

            int tileW = bgTex.width;
            int tileH = bgTex.height;

            // Far layer tint
            Color farTint = { 180, 120, 200, 160 };
            int cols = (int)(WORLD_WIDTH / tileW) + 3;
            int rows = (int)(WORLD_HEIGHT / tileH) + 3;
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < cols; ++x) {
                    float dx = x * tileW + fmodf(offFar, (float)tileW);
                    DrawTexture(bgTex, (int)dx, y * tileH, farTint);
                }
            }

            // Near layer (slightly higher contrast)
            Color nearTint = { 255, 190, 120, 190 };
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < cols; ++x) {
                    float dx = x * tileW + fmodf(offNear, (float)tileW);
                    DrawTexture(bgTex, (int)dx, y * tileH, nearTint);
                }
            }
        }
        EndMode2D();

        // Vignette overlay
        for (int i = 0; i < 6; ++i) {
            float a = 35.0f + i * 10.0f;
            DrawRectangle(-i * 25, -i * 25, WORLD_WIDTH + i * 50, WORLD_HEIGHT + i * 50,
                          { 0, 0, 0, (unsigned char)a });
        }

        EndTextureMode();
        return;
    }

    // --- Original live gameplay simulation ---
    // Sky: vertical gradient
    for (int strip = 0; strip < WORLD_HEIGHT; strip += 80) {
        float t = (float)strip / (float)WORLD_HEIGHT;
        unsigned char r = (unsigned char)(15 + t * 10);
        unsigned char g = (unsigned char)(15 + t * 8);
        unsigned char b = (unsigned char)(35 + t * 15);
        DrawRectangle(0, strip, WORLD_WIDTH, 80, {r, g, b, 255});
    }

    BeginMode2D(camera);

    // Render map background (TMX or tiled image)
    if (currentMapData && currentMapData->tmxMap) {
        float viewW = VIRTUAL_WIDTH / camera.zoom;
        float viewH = VIRTUAL_HEIGHT / camera.zoom;
        Rectangle viewport = { camera.target.x - viewW * 0.5f,
                               camera.target.y - viewH * 0.5f,
                               viewW,
                               viewH };
        DrawTMX(currentMapData->tmxMap, &camera, &viewport, 0, 0, WHITE);
    } else if (bgTex.id != 0 && bgTex.width > 0) {
        int cols = (int)(WORLD_WIDTH  / bgTex.width)  + 2;
        int rows = (int)(WORLD_HEIGHT / bgTex.height) + 2;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                DrawTexture(bgTex, x * bgTex.width, y * bgTex.height, WHITE);
            }
        }
    }

    // Depth-sorted world objects (trees, rocks, bots, projectiles)
    struct RenderItem {
        float depthY;
        std::function<void()> draw;
    };
    std::vector<RenderItem> items;


    for (auto* b : bots) {
        RenderItem it;
        it.depthY = b->GetDepthY();
        it.draw = [b]() { b->Draw(); };
        items.push_back(it);
    }
    // Projectiles
    for (auto* b : bots) {
        for (auto& p : b->GetProjectiles()) {
            RenderItem it;
            it.depthY = p->GetPosition().y;
            it.draw = [&p]() { p->Draw(); };
            items.push_back(it);
        }
    }

    std::sort(items.begin(), items.end(), [](const RenderItem& a, const RenderItem& b) {
        return a.depthY < b.depthY;
    });
    for (auto& it : items) it.draw();

    EndMode2D();
    EndTextureMode();
}


void MenuBackground::Draw(RenderTexture2D target, float alpha, float blurStrength) {
    // 1) Render the world into sceneRT
    DrawScene();

    // 2) Downsample the scene into blurRT1 (1/4) and blurRT2 (1/8).
    //    This is the standard raylib "no-shader" blur pattern: bilinear
    //    filtering on the small textures gives a soft, dreamy look when
    //    they're upscaled back to full resolution.
    BeginTextureMode(blurRT1);
    ClearBackground({0, 0, 0, 0});
    DrawTexturePro(
        sceneRT.texture,
        { 0, 0, (float)VIRTUAL_WIDTH, -(float)VIRTUAL_HEIGHT }, // flip Y: render-texture convention
        { 0, 0, (float)VIRTUAL_WIDTH / 4, (float)VIRTUAL_HEIGHT / 4 },
        { 0, 0 }, 0.0f, WHITE
    );
    EndTextureMode();

    BeginTextureMode(blurRT2);
    ClearBackground({0, 0, 0, 0});
    DrawTexturePro(
        sceneRT.texture,
        { 0, 0, (float)VIRTUAL_WIDTH, -(float)VIRTUAL_HEIGHT },
        { 0, 0, (float)VIRTUAL_WIDTH / 8, (float)VIRTUAL_HEIGHT / 8 },
        { 0, 0 }, 0.0f, WHITE
    );
    EndTextureMode();

    // 3) Composite onto the main target.
    //    Layered approach: original scene (faded by alpha), then heavy blur
    //    on top blended by blurStrength. This means at blurStrength=0 we
    //    see the sharp scene at full alpha; at blurStrength=1 the heavy
    //    blur dominates and the sharp layer is mostly hidden.
    BeginTextureMode(target);
    ClearBackground({0, 0, 0, 0});

    unsigned char sceneA = (unsigned char)(alpha * 255.0f);
    unsigned char blurA  = (unsigned char)(alpha * 255.0f * blurStrength);

    // Sharp scene (no blur) — full alpha
    DrawTexturePro(
        sceneRT.texture,
        { 0, 0, (float)VIRTUAL_WIDTH, -(float)VIRTUAL_HEIGHT },
        { 0, 0, (float)VIRTUAL_WIDTH, (float)VIRTUAL_HEIGHT },
        { 0, 0 }, 0.0f, { 255, 255, 255, (unsigned char)(sceneA * (1.0f - blurStrength)) }
    );

    // Mid blur (1/4) — additive feel
    DrawTexturePro(
        blurRT1.texture,
        { 0, 0, (float)VIRTUAL_WIDTH / 4, -(float)VIRTUAL_HEIGHT / 4 },
        { 0, 0, (float)VIRTUAL_WIDTH, (float)VIRTUAL_HEIGHT },
        { 0, 0 }, 0.0f, { 255, 255, 255, (unsigned char)(blurA * 0.6f) }
    );

    // Heavy blur (1/8) — dreamy glow
    DrawTexturePro(
        blurRT2.texture,
        { 0, 0, (float)VIRTUAL_WIDTH / 8, -(float)VIRTUAL_HEIGHT / 8 },
        { 0, 0, (float)VIRTUAL_WIDTH, (float)VIRTUAL_HEIGHT },
        { 0, 0 }, 0.0f, { 255, 255, 255, (unsigned char)blurA }
    );

    EndTextureMode();
}
