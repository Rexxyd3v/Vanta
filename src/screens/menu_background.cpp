#include "../map_loader/map_effects.h"
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
    // Main menu leaves the left side for the title and places the live fight on the right.
    camera.offset   = { VIRTUAL_WIDTH * (style==BackdropStyle::LIVE_GAMEPLAY?0.72f:0.5f), VIRTUAL_HEIGHT / 2.0f };
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

Vector2 MenuBackground::WorldSize() const {
    return currentMapData?Vector2{(float)currentMapData->pixelWidth,(float)currentMapData->pixelHeight}:
                          Vector2{(float)WORLD_WIDTH,(float)WORLD_HEIGHT};
}
void MenuBackground::InitWorld() {
    auto& registry=MapRegistry::GetInstance();
    currentMapData=registry.GetMap(NetworkManager::GetInstance().selectedMapName);
    if(!currentMapData) currentMapData=registry.GetMap("MagmaRift");
    if(!currentMapData) currentMapData=registry.GetMapByIndex(0);
    bgTex={};
    if(currentMapData && !currentMapData->tmxMap)
        bgTex=TextureManager::GetTexture(currentMapData->folderPath+currentMapData->scene.bgTexture);
    Vector2 size=WorldSize();
    fightFocus=camera.target={size.x*.48f,size.y*.49f};
    cameraZoom=cameraZoomTarget=camera.zoom=.52f;
}
void MenuBackground::ResolveDemoCollision(Character* actor) {
    if(!actor) return;
    if(currentMapData && currentMapData->tmxMap) for(int pass=0;pass<4;++pass) {
        const auto* map=currentMapData->tmxMap;
        for(uint32_t i=0;i<map->layersLength;++i) {
            const auto& layer=map->layers[i];
            if(layer.type!=LAYER_TYPE_OBJECT_GROUP || !layer.name || std::string(layer.name)!="collision") continue;
            const auto& group=layer.exact.objectGroup;
            for(uint32_t j=0;j<group.objectsLength;++j) {
                const auto& object=group.objects[j];
                Rectangle obstacle{(float)object.x,(float)object.y,(float)object.width,(float)object.height};
                if(object.type==OBJECT_TYPE_POLYGON) obstacle=object.aabb;
                if(object.type!=OBJECT_TYPE_RECTANGLE && object.type!=OBJECT_TYPE_POLYGON) continue;
                Rectangle feet=actor->GetCollisionBounds();
                if(CheckCollisionRecs(feet,obstacle)) ResolveCollisionPush(actor,feet,obstacle);
            }
        }
    }
    Vector2 size=WorldSize(),p=actor->GetPosition();
    p.x=fclamp(p.x,120,size.x-120);p.y=fclamp(p.y,120,size.y-actor->GetFeetOffset()-120);
    actor->SetPosition(p);
}
void MenuBackground::PlaceSpaced(BotEnemy* actor,Vector2 desired) {
    Vector2 best=desired;float bestGap=-1;
    for(int attempt=0;attempt<30;++attempt) {
        float angle=attempt*2.399963f,radius=attempt?90.0f+attempt*12:0;
        actor->SetPosition({desired.x+std::cos(angle)*radius,desired.y+std::sin(angle)*radius});
        ResolveDemoCollision(actor);
        float gap=1e9f;
        for(auto* other:bots) if(other!=actor && !other->IsDead()) {
            Vector2 p=actor->GetPosition(),q=other->GetPosition();
            gap=std::min(gap,std::hypot(p.x-q.x,p.y-q.y));
        }
        if(gap>bestGap) { bestGap=gap;best=actor->GetPosition(); }
        if(gap>=380) break;
    }
    actor->SetPosition(best);
}
void MenuBackground::InitEntities(int localSkinIndex) {
    (void)localSkinIndex;
    const std::string path="assets/Free 2D Animated Vector Game Character Sprites/Free 2D Animated Vector Game Character Sprites/Full body animated characters/";
    const Vector2 positions[]={{.33f,.40f},{.47f,.38f},{.64f,.41f},{.34f,.59f},{.48f,.59f},{.66f,.56f}};
    const int weapons[]={0,3,1,2,3,4};
    Vector2 size=WorldSize();
    for(int i=0;i<6;++i) {
        auto* fighter=new BotEnemy({positions[i].x*size.x,positions[i].y*size.y},path+"Char "+std::to_string(i%4+1)+"/with hands/");
        fighter->SetName("Fighter "+std::to_string(i+1));
        fighter->SetDifficulty(2);fighter->SwitchWeapon(weapons[i]);
        PlaceSpaced(fighter,fighter->GetPosition());bots.push_back(fighter);
        agents.push_back({fighter->GetPosition(),fighter->GetPosition(),.15f*i,.12f*i,3.0f+i*.8f});
    }
}

// Returns the nearest living bot that b can target.
// In spectator mode there is no player, so bots only target each other.
Character* MenuBackground::GetNearestTargetForBot(BotEnemy* b) {
    Character* nearest = nullptr;
    float minDist = 1e9f;

    for (auto other : bots) {
        if (other == b || other->IsDead()) continue;
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
    float dt=std::clamp(deltaTime,0.0f,.05f);
    for(size_t i=0;i<bots.size();++i) {
        auto* fighter=bots[i];auto& state=agents[i];
        Vector2 previous=fighter->GetPosition();
        if(!fighter->IsDead()) {
            auto* target=GetNearestTargetForBot(fighter);
            state.retarget-=dt;state.fire-=dt;state.skill-=dt;
            if(state.retarget<=0) {
                float phase=worldTime*.37f+i*1.7f;
                state.destination={state.home.x+std::sin(phase)*210,state.home.y+std::cos(phase*.83f)*145};
                state.retarget=1.8f+(i%3)*.5f;
            }
            Vector2 destination=state.destination;
            for(const auto& grenade:ordnance) if(grenade.kind==1 && grenade.age>.9f && grenade.age<6.9f) {
                Vector2 p=fighter->GetPosition();float dx=p.x-grenade.target.x,dy=p.y-grenade.target.y,d=std::hypot(dx,dy);
                if(d<240 && d>1) { destination.x+=dx/d*220;destination.y+=dy/d*220; }
            }
            fighter->MoveToObjective(dt,destination);
            if(target) {
                Vector2 aim=target->GetPosition();aim.y+=45-target->GetJumpHeight();
                fighter->SetAimTarget(aim);fighter->SetFaceDirection(aim.x>=fighter->GetPosition().x?1:-1);
                if(fighter->GetAmmo()==0 && !fighter->IsReloading()) fighter->TriggerReload();
                float range=std::hypot(aim.x-fighter->GetPosition().x,aim.y-fighter->GetPosition().y);
                if(state.fire<=0 && range<1250) {
                    aim.x+=std::sin(worldTime*1.7f+i)*13;
                    if(fighter->Shoot(aim)) state.fire=std::max(.20f,Weapons[fighter->GetWeaponIndex()].fireRate)+.06f*(i%3);
                }
                if(state.skill<=0) {
                    if(fighter->GetHealth()<45) fighter->ActivateShield();
                    else {
                        Vector2 difference{aim.x-fighter->GetPosition().x,aim.y-fighter->GetPosition().y};
                        fighter->ActivateDash({-difference.y*(i%2?1.0f:-1.0f),difference.x*(i%2?1.0f:-1.0f)});
                    }
                    state.skill=5.5f+i*.3f;
                }
            }
        }
        fighter->Update(dt);
        Vector2 end=fighter->GetPosition(),delta{end.x-previous.x,end.y-previous.y};
        int steps=std::max(1,(int)std::ceil(std::hypot(delta.x,delta.y)/6));
        fighter->SetPosition(previous);
        for(int step=0;step<steps;++step) {
            Vector2 p=fighter->GetPosition();fighter->SetPosition({p.x+delta.x/steps,p.y+delta.y/steps});ResolveDemoCollision(fighter);
        }
        if(fighter->ShouldRespawn()) {
            fighter->ResetHealth(100);PlaceSpaced(fighter,state.home);fighter->ResetDeathTimer();fighter->SetSpawnFadeTimer(.7f);
        }
    }
    // Maintain room between fighters instead of allowing close-range piles around one target.
    for(int pass=0;pass<2;++pass) for(size_t i=0;i<bots.size();++i) for(size_t j=i+1;j<bots.size();++j) {
        auto* a=bots[i];auto* b=bots[j];if(a->IsDead() || b->IsDead()) continue;
        Vector2 p=a->GetPosition(),q=b->GetPosition();float dx=q.x-p.x,dy=q.y-p.y,d=std::hypot(dx,dy);
        if(d<280) {
            if(d<.01f){dx=1;dy=0;d=1;}
            float push=(280-d)*.5f;
            a->SetPosition({p.x-dx/d*push,p.y-dy/d*push});b->SetPosition({q.x+dx/d*push,q.y+dy/d*push});
            ResolveDemoCollision(a);ResolveDemoCollision(b);
        }
    }
    for(auto* shooter:bots) for(auto& bullet:shooter->GetProjectiles()) {
        if(!bullet->IsActive()) continue;
        for(auto* victim:bots) {
            if(victim==shooter || victim->IsDead()) continue;
            Vector2 p=victim->GetPosition();p.y-=victim->GetJumpHeight();
            if(ProjectileHitsAABB(bullet->GetPosition(),p)) { victim->TakeDamage(bullet->GetDamage());bullet->Deactivate();break; }
        }
    }
    ordnanceTimer-=dt;
    if(ordnanceTimer<=0 && ordnance.size()<8) {
        BotEnemy* shooter=nullptr;
        for(size_t step=0;step<bots.size();++step) {
            auto* candidate=bots[(ordnanceSequence+step)%bots.size()];
            Vector2 p=candidate->GetPosition();
            if(!candidate->IsDead() && std::hypot(p.x-camera.target.x,p.y-camera.target.y)<1150) { shooter=candidate;break; }
        }
        if(shooter) if(auto* target=GetNearestTargetForBot(shooter)) {
            Vector2 p=target->GetPosition();p.y+=target->GetFeetOffset();
            p.x+=std::sin(worldTime)*95;p.y+=std::cos(worldTime)*65;
            ordnance.push_back({shooter->GetHandPosition(),p,0,ordnanceSequence%3==0?1:0,false});
            ++ordnanceSequence;
        }
        ordnanceTimer=2.4f;
    }
    blastShake=std::max(0.0f,blastShake-dt*1.65f);
    for(auto& grenade:ordnance) {
        grenade.age+=dt;
        if(grenade.kind==0 && grenade.age>=.9f && !grenade.exploded) {
            grenade.exploded=true;
            Vector2 screen=GetWorldToScreen2D(grenade.target,camera);
            float distance=std::hypot(screen.x-VIRTUAL_WIDTH*.6f,screen.y-VIRTUAL_HEIGHT*.5f);
            blastShake=std::max(blastShake,std::clamp(1.15f-distance/1400,.25f,1.0f));
            for(auto* victim:bots) {
                Vector2 p=victim->GetPosition();p.y+=victim->GetFeetOffset();
                if(!victim->IsDead() && std::hypot(p.x-grenade.target.x,p.y-grenade.target.y)<190) victim->TakeDamage(35);
            }
        }
    }
    ordnance.erase(std::remove_if(ordnance.begin(),ordnance.end(),[](const DemoGrenade& g){return g.age>(g.kind==1?7.5f:3.0f);}),ordnance.end());
    TickCinematicCamera(dt);
}

void MenuBackground::TickCinematicCamera(float dt) {
    shotElapsed+=dt;
    if(cameraSubject<0 || shotElapsed>=shotDuration || bots[cameraSubject]->IsDead()) {
        previousCameraSubject=cameraSubject;
        int start=(shotSequence*2+shotSequence/3)%(int)bots.size();
        for(size_t i=0;i<bots.size();++i) {
            int candidate=(start+i)%bots.size();
            if(!bots[candidate]->IsDead() && candidate!=previousCameraSubject) { cameraSubject=candidate;break; }
        }
        if(cameraSubject<0) cameraSubject=0;
        int style=shotSequence++%4;
        shotElapsed=0;
        shotDuration=style==0?5.2f:style==1?4.3f:style==2?4.8f:4.1f;
        // Establishing pullback, fighter push-in, travelling duel, then reverse pullout.
        const float from[]={.68f,1.02f,.82f,1.30f},to[]={.48f,1.42f,1.03f,.68f};
        shotZoomStart=from[style];shotZoomEnd=to[style];
        shotRollStart=style%2?1.5f:-1.6f;shotRollEnd=-shotRollStart*.7f;
        shotPanStart=style%2?Vector2{-100,55}:Vector2{110,-65};
        shotPanEnd={-shotPanStart.x*.7f,-shotPanStart.y*.7f};
    }
    float t=std::clamp(shotElapsed/shotDuration,0.0f,1.0f);
    float ease=t*t*(3-2*t);
    Vector2 subject=bots[cameraSubject]->GetPosition();subject.y+=35;
    int style=(shotSequence-1)%4;
    if(style==0 || style==2) {
        if(auto* opponent=GetNearestTargetForBot(bots[cameraSubject])) {
            Vector2 other=opponent->GetPosition();
            subject.x=subject.x*.6f+other.x*.4f;
            subject.y=subject.y*.6f+(other.y+35)*.4f;
        }
    }
    // Frame a live grenade in the travelling shot, then follow its blast.
    if(style==2 && !ordnance.empty()) {
        const auto& grenade=ordnance.back();
        if(grenade.age<1.6f) {
            subject.x=subject.x*.45f+grenade.target.x*.55f;
            subject.y=subject.y*.45f+grenade.target.y*.55f;
        }
    }
    fightFocus={subject.x+shotPanStart.x+(shotPanEnd.x-shotPanStart.x)*ease,
                subject.y+shotPanStart.y+(shotPanEnd.y-shotPanStart.y)*ease};
    float follow=1-std::exp(-dt*2.2f);
    camera.target.x+=(fightFocus.x-camera.target.x)*follow;
    camera.target.y+=(fightFocus.y-camera.target.y)*follow;
    cameraZoomTarget=shotZoomStart+(shotZoomEnd-shotZoomStart)*ease;
    cameraZoom+=(cameraZoomTarget-cameraZoom)*(1-std::exp(-dt*3.0f));
    camera.zoom=cameraZoom;
    float roll=shotRollStart+(shotRollEnd-shotRollStart)*ease;
    camera.rotation+=(roll-camera.rotation)*(1-std::exp(-dt*2.0f));
    Vector2 size=WorldSize();
    const float margin=55;
    camera.target.x=fclamp(camera.target.x,camera.offset.x/camera.zoom+margin,size.x-(VIRTUAL_WIDTH-camera.offset.x)/camera.zoom-margin);
    camera.target.y=fclamp(camera.target.y,camera.offset.y/camera.zoom+margin,size.y-(VIRTUAL_HEIGHT-camera.offset.y)/camera.zoom-margin);
}

Camera2D MenuBackground::RenderCamera() const {
    Camera2D result=camera;
    float shake=blastShake*blastShake;
    result.target.x+=(std::sin(worldTime*91)+std::sin(worldTime*137)*.4f)*shake*17/camera.zoom;
    result.target.y+=(std::cos(worldTime*77)+std::sin(worldTime*113)*.4f)*shake*12/camera.zoom;
    result.rotation+=std::sin(worldTime*83)*shake*.65f;
    return result;
}

void MenuBackground::Update(float deltaTime, float fadeInAlpha) {
    (void)fadeInAlpha; // reserved for future hooks
    worldTime += deltaTime;

    if (!useParallaxBackdrop) {
        TickWorld(deltaTime);
    }
}


void MenuBackground::DrawOrdnance() const {
    for(const auto& grenade:ordnance) {
        Vector2 center=grenade.target;
        if(grenade.age<.9f) {
            float t=grenade.age/.9f;
            Vector2 p{grenade.start.x+(center.x-grenade.start.x)*t,
                      grenade.start.y+(center.y-grenade.start.y)*t-std::sin(t*PI)*95};
            DrawEllipse((int)(grenade.start.x+(center.x-grenade.start.x)*t),
                        (int)(grenade.start.y+(center.y-grenade.start.y)*t),10,4,Fade(BLACK,.25f));
            DrawCircleV(p,7,{44,73,38,255});
            DrawCircleLines((int)p.x,(int)p.y,7,{174,221,82,255});
            DrawCircleV({p.x+3,p.y-7},2,{255,218,115,255});
            continue;
        }
        float age=grenade.age-.9f;
        if(grenade.kind==1) {
            float envelope=std::min(1.0f,age/.7f)*std::min(1.0f,(6.6f-age)/1.4f);
            float spread=55+130*(1-std::exp(-age*.8f));
            for(int i=0;i<16;++i) {
                float angle=i*2.399963f+age*.065f;
                float distance=spread*(.22f+.055f*(i%12));
                Vector2 puff{center.x+std::cos(angle)*distance+age*7,
                             center.y+std::sin(angle)*distance*.65f-age*5};
                float radius=62+age*5+(i%4)*9+std::sin(worldTime*1.2f+i)*7;
                DrawCircleGradient(puff,radius,
                                   Fade({81,126,56,255},envelope*.21f),Fade({45,70,40,255},0));
            }
            for(int i=0;i<9;++i) {
                float angle=i*2.4f+age*.3f;
                Vector2 mote{center.x+std::cos(angle)*spread*.65f+age*7,
                             center.y+std::sin(angle)*spread*.4f-age*12};
                DrawCircleV(mote,1.7f,Fade({169,218,87,255},envelope*.45f));
            }
        } else {
            float fade=std::max(0.0f,1-age/2.1f);
            DrawCircleGradient(center,115,Fade({35,23,17,255},fade*.65f),Fade(BLACK,0));
            for(int i=0;i<9;++i) {
                float angle=i*.698f;
                Vector2 start{center.x+std::cos(angle)*18,center.y+std::sin(angle)*12};
                Vector2 bend{center.x+std::cos(angle+.12f)*55,center.y+std::sin(angle+.12f)*38};
                Vector2 end{center.x+std::cos(angle-.08f)*(90+i%3*16),center.y+std::sin(angle-.08f)*(62+i%3*10)};
                DrawLineEx(start,bend,2,Fade({255,123,35,255},fade*.55f));
                DrawLineEx(bend,end,1.5f,Fade({54,29,17,255},fade));
            }
            if(age<.75f) {
                float burst=1-age/.75f;
                DrawCircleGradient(center,80+age*180,
                                   Fade({255,156,39,255},burst*.8f),Fade({255,64,15,255},0));
                DrawRing(center,30+age*250,34+age*250,0,360,64,Fade({255,197,110,255},burst*.65f));
                for(int i=0;i<20;++i) {
                    float angle=i*2.399963f,speed=150+(i%5)*42;
                    Vector2 p{center.x+std::cos(angle)*age*speed,center.y+std::sin(angle)*age*speed*.72f-age*55};
                    Vector2 tail{p.x-std::cos(angle)*burst*20,p.y-std::sin(angle)*burst*14};
                    DrawLineEx(tail,p,2+burst,Fade({255,201,108,255},burst));
                }
                if(age<.15f) DrawCircleGradient(center,70,
                    Fade({255,246,207,255},1-age/.15f),Fade({255,180,55,255},0));
            }
        }
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

    Camera2D steadyCamera=camera;
    camera=RenderCamera();

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
        float viewW = VIRTUAL_WIDTH / camera.zoom+140;
        float viewH = VIRTUAL_HEIGHT / camera.zoom+140;
        Rectangle viewport = { camera.target.x - camera.offset.x/camera.zoom-70,
                               camera.target.y - camera.offset.y/camera.zoom-70,
                               viewW,
                               viewH };
        DrawMapWithMaterials(currentMapData->tmxMap,&camera,&viewport);
        DrawMapEffects(currentMapData->tmxMap,worldTime,viewport);
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
    std::sort(items.begin(), items.end(), [](const RenderItem& a, const RenderItem& b) {
        return a.depthY < b.depthY;
    });
    for (auto& it : items) it.draw();
    DrawOrdnance();

    EndMode2D();
    camera=steadyCamera;
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
