#include "screens/main_menu.h"
#include "screens/lobby_screen.h"
#include "screens/offline_menu.h"
#include "screens/main_level.h"
#include "ui/game_settings.h"
#include "map_loader/MapRegistry.h"
#include "utils/texture_manager.h"
#include "voice/proximity_voice_chat.h"
#include "network/network_manager.h"
#include "ui/weapon_skin_catalog.h"
#include "ui/weapon_skin_preview.h"
#include "ui/ui_theme.h"
#include "map_loader/map_effects.h"
#include <iostream>
#include <cmath>
#include <cstdlib>

struct MenuBackgroundTestAccess {
    static Camera2D Camera(const MenuBackground& b) { return b.camera; }
    static int Subject(const MenuBackground& b) { return b.cameraSubject; }
    static float Shake(const MenuBackground& b) { return b.blastShake; }
    static bool ShakeMovesOnlyRenderCamera(const MenuBackground& b) {
        Camera2D render=b.RenderCamera();
        return std::hypot(render.target.x-b.camera.target.x,render.target.y-b.camera.target.y)>.1f;
    }
    static int Fighters(const MenuBackground& b) { return (int)b.bots.size(); }
    static bool PlayersOnly(const MenuBackground& b) {
        for(auto* actor:b.bots) if(actor->IsMonster()) return false;
        return true;
    }
    static int Throws(const MenuBackground& b) { return b.ordnanceSequence; }
    static float MinimumGap(const MenuBackground& b) {
        float gap=1e9f;
        for(size_t i=0;i<b.bots.size();++i) for(size_t j=i+1;j<b.bots.size();++j) {
            if(b.bots[i]->IsDead() || b.bots[j]->IsDead()) continue;
            Vector2 a=b.bots[i]->GetPosition(),c=b.bots[j]->GetPosition();
            gap=std::min(gap,std::hypot(a.x-c.x,a.y-c.y));
        }
        return gap;
    }
    static void Frame(MenuBackground& background,MapData* map,float offsetX) {
        background.currentMapData=map;
        background.blastShake=0;
        background.camera={{offsetX,340},{600+offsetX,740},0,1};
    }
};
struct OfflineMenuTestAccess {
    static void Scroll(OfflineMenuScreen& menu,float amount) { menu.ScrollSetup(amount); }
    static float Offset(const OfflineMenuScreen& menu) { return menu.setupScroll; }
    static float Maximum(const OfflineMenuScreen& menu) { return menu.SetupScrollMax(); }
    static Rectangle Preview(const OfflineMenuScreen& menu) { return menu.previewBounds; }
    static bool HiddenBotControl(const OfflineMenuScreen& menu) {
        Rectangle local{0,20,44,32};
        Rectangle screen=menu.SetupBounds(local);
        return menu.SetupHit(local,{screen.x+22,screen.y+16});
    }
};
struct SkinTestPlayer : Player {
    using Player::Player;
    Texture2D Texture(int slot) const { return weaponTextures[slot]; }
};
struct GameplayEffectsTestAccess {
    static size_t FeedCount(const GameplayScreen& game) { return game.killFeed.size(); }
    static int FeedWeapon(const GameplayScreen& game) { return game.killFeed.front().weapon; }
    static std::string FeedKiller(const GameplayScreen& game) { return game.killFeed.front().killer; }
    static void FeedTime(GameplayScreen& game,float dt) { game.UpdateKillFeed(dt); }
    static void OnlineKill(GameplayScreen& game,uint32_t killer,uint32_t victim,int weapon) { game.ProcessKill(killer,victim,weapon,0); }

    static void Step(GameplayScreen& game,float dt) { game.UpdateGrenades(dt,false); }
    static void Reset(GameplayScreen& game) { game.ResetGrenades(); }
    static float Shake(const GameplayScreen& game) { return game.explosionShake; }
    static void RemoteSmoke(GameplayScreen& game,Vector2 target,int team) {
        game.grenades.push_back({target,target,0,1,false,99,team});
    }
    static int BotCount(const GameplayScreen& game) { return (int)game.bots.size(); }
    static BotEnemy* Bot(GameplayScreen& game,int index) { return game.bots[index].get(); }
    static int Score(const GameplayScreen& game,int team) { return game.offlineScores[team]; }
    static bool RoundOver(const GameplayScreen& game) { return game.offlineRoundOver; }
    static bool MatchOver(const GameplayScreen& game) { return game.matchEndTimer>0; }
    static void Flow(GameplayScreen& game,float dt) { game.UpdateOfflineFlow(dt); }
    static void Practice(GameplayScreen& game,float dt) { game.UpdatePractice(dt); }
    static void Kill(GameplayScreen& game,Character* killer,Character* victim) { victim->TakeDamage(10000); game.RecordOfflineKill(killer,victim); }
    static bool Enemy(GameplayScreen& game,Character* a,Character* b) { return game.OfflineEnemies(a,b); }
    static void Flags(GameplayScreen& game,float dt,bool drop=false) { game.UpdateFlags(dt,drop); }
    static void OnlineFlow(GameplayScreen& game,float dt) { game.UpdateMatchFlow(dt); }
    static void FlagAction(GameplayScreen& game,uint32_t id) { game.flagInteractRequests.insert(id); }
    static RemotePlayer* Remote(GameplayScreen& game,uint32_t id) { return game.FindByPeerID(id); }
    static PacketFlagState Snapshot(GameplayScreen& game) { return game.MakeFlagState(); }
    static void Apply(GameplayScreen& game,const PacketFlagState& packet) { game.ApplyFlagState(packet); }
    static Vector2 Base(GameplayScreen& game,int team) { return game.teamBasePositions[team]; }
    static Character* Carrier(GameplayScreen& game,int team) { return game.flags[team].carrier; }
    static bool Dropped(GameplayScreen& game,int team) { return game.flags[team].dropped; }
    static Vector2 FlagPosition(GameplayScreen& game,int team) { return game.flags[team].position; }
    static void ResetFlag(GameplayScreen& game,int team) { game.ResetFlag(team); }
    static float FlagDelay(GameplayScreen& game,int team) { return game.flags[team].respawnTimer; }
    static Vector2 DmSpawn(GameplayScreen& game,int identity) { return game.GetDeathmatchSpawnPoint(identity); }
    static void Respawn(GameplayScreen& game) { game.RespawnLocal(); }
    static void Resolve(GameplayScreen& game,Character* actor) { game.ResolveWorldCollision(actor); }
    static void Move(GameplayScreen& game,Character* actor,Vector2 previous) { game.ResolveWorldMovement(actor,previous); }
    static void Time(GameplayScreen& game,float t) { game.worldTime=t; }
    static void Focus(GameplayScreen& game,Vector2 center,float zoom=0.85f) { game.camera.target=center; game.camera.zoom=zoom; }
    static void Restart(GameplayScreen& game) { game.StartOfflineRound(); }
    static void RemoteBlast(GameplayScreen& game,Vector2 target) {
        game.grenades.push_back({target,target,0,0,false,99,2});
    }
};
static void check(bool ok,const char* label) { if (!ok) { std::cerr << label << "\n"; std::exit(20); } }
ProximityVoiceChat proximityVoiceChat;
bool proximityVoiceInitialized = false;
bool (*getLocalPlayerPosCallback)(Vector2&) = nullptr;
int main() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(VIRTUAL_WIDTH, VIRTUAL_HEIGHT, "Vanta smoke test");
    InitAudioDevice(); SetExitKey(KEY_NULL);
    NetworkManager::GetInstance().Initialize();
    MapRegistry::GetInstance().LoadAllMaps();
    check(NetworkManager::GetInstance().selectedMapName=="MagmaRift" && Offline::Setup{}.map=="MagmaRift",
          "online and offline default to the lava map");
    auto target = LoadRenderTexture(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
    auto capture = [&](const char* path) {
        Image img = LoadImageFromTexture(target.texture); ImageFlipVertical(&img);
        ExportImage(img, path); UnloadImage(img);
        BeginDrawing(); ClearBackground(BLACK); EndDrawing();
    };
    {
        auto* map=MapRegistry::GetInstance().GetMap("MagmaRift");
        check(map && map->tmxMap && map->pixelWidth==4800 && map->pixelHeight==3200,"new TMX arena is discovered and loads at its authored size");
        check(map->scene.spawnPoints.size()==16,"TMX point objects provide all authored spawn positions");
        auto& nm=NetworkManager::GetInstance(); auto previous=nm.selectedMapName;
        nm.selectedMapName="MagmaRift";
        BeginTextureMode(target); ClearBackground({20,29,25,255});
        Camera2D camera{{VIRTUAL_WIDTH*0.5f,VIRTUAL_HEIGHT*0.5f},{2400,1600},0,0.19f};
        Rectangle viewport{0,0,4800,3200};
        BeginMode2D(camera); DrawMapWithMaterials(map->tmxMap,&camera,&viewport); EndMode2D();
        EndTextureMode(); capture("bin/magma-rift-map-preview.png");
        GameplayScreen arena(GameMode::PRACTICE);
        GameplayEffectsTestAccess::Focus(arena,{2400,1600},0.32f);
        arena.Draw(target); capture("bin/magma-rift-gameplay-preview.png");
        check(arena.GetLocalPlayer() && !arena.GetLocalPlayer()->IsDead(),"new map starts a playable offline match");
        auto* actor=arena.GetLocalPlayer();
        for (auto spawn:map->scene.spawnPoints) {
            actor->SetPosition(spawn.position); GameplayEffectsTestAccess::Resolve(arena,actor);
            Vector2 actual=actor->GetPosition();
            check(std::hypot(actual.x-spawn.position.x,actual.y-spawn.position.y)<0.1f,"authored magma spawn is clear of lava and obstacle collision");
        }
        const TmxLayer* floor=nullptr;
        for(uint32_t i=0;i<map->tmxMap->layersLength;++i) {
            const auto& layer=map->tmxMap->layers[i];
            if(layer.name && std::string(layer.name)=="Basalt platforms") floor=&layer;
        }
        check(floor,"magma map has editable modular floor tiles");
        int edges=0;
        for(int y=1;y<49;++y) for(int x=1;x<74;++x) {
            if(!floor->exact.tileLayer.tiles[y*75+x]) continue;
            for(auto direction : {Vector2{1,0},Vector2{-1,0},Vector2{0,1},Vector2{0,-1}}) {
                int nx=x+(int)direction.x,ny=y+(int)direction.y;
                if(floor->exact.tileLayer.tiles[ny*75+nx]) continue;
                Vector2 start{x*64+32.0f,y*64+32.0f-actor->GetFeetOffset()+3};
                for(float height : {0.0f,180.0f}) {
                    actor->ResetHealth(100); actor->SetPosition(start); actor->SetJumpHeight(height);
                    Rectangle ground=actor->GetCollisionBounds();
                    actor->SetJumpHeight(0);
                    check(actor->GetCollisionBounds().y==ground.y,"jumping never moves the ground collision footprint");
                    actor->SetJumpHeight(height);
                    actor->SetPosition({start.x+direction.x*120,start.y+direction.y*120});
                    GameplayEffectsTestAccess::Move(arena,actor,start);
                    Rectangle feet=actor->GetCollisionBounds();
                    int cellX=(int)((feet.x+feet.width/2)/64),cellY=(int)((feet.y+feet.height/2)/64);
                    check(floor->exact.tileLayer.tiles[cellY*75+cellX]!=0,"swept walk and jump remain on visible land at every lava edge");
                }
                ++edges;
            }
        }
        check(edges>200,"collision regression exercises the whole terrain perimeter and both pools");
        actor->ResetHealth(100);
        Vector2 dashStart{21*64+64.0f,20*64+66.0f-actor->GetFeetOffset()};
        actor->SetPosition(dashStart); actor->ActivateDash({0,1}); actor->UpdateSkills(0.22f);
        GameplayEffectsTestAccess::Move(arena,actor,dashStart);
        Rectangle stopped=actor->GetCollisionBounds();
        check(stopped.y+stopped.height<=20*64+88.1f,"long dash frame cannot tunnel through a thin rock footprint");
        bool previousEffects=Settings::Get().effects;
        Settings::Get().effects=true;
        auto surface=[&](float time) {
            BeginTextureMode(target); ClearBackground(BLACK);
            BeginMode2D(camera); DrawMapWithMaterials(map->tmxMap,&camera,&viewport);
            DrawMapEffects(map->tmxMap,time,viewport); EndMode2D(); EndTextureMode();
            Image result=LoadImageFromTexture(target.texture); ImageFlipVertical(&result); return result;
        };
        Image flowA=surface(0),flowB=surface(1.4f);
        int changed=0;
        for(int y=70;y<610;y+=4) for(int x=220;x<1080;x+=4) {
            Color a=GetImageColor(flowA,x,y),b=GetImageColor(flowB,x,y);
            if(std::abs((int)a.r-b.r)+std::abs((int)a.g-b.g)+std::abs((int)a.b-b.b)>15) ++changed;
        }
        check(changed>300,"lava flow, vents and ember effects visibly animate");
        UnloadImage(flowA); UnloadImage(flowB);
        Settings::Get().effects=false;
        flowA=surface(0); flowB=surface(1.4f); changed=0;
        for(int y=20;y<650;y+=8) for(int x=180;x<1120;x+=8) {
            Color a=GetImageColor(flowA,x,y),b=GetImageColor(flowB,x,y);
            if(a.r!=b.r || a.g!=b.g || a.b!=b.b) ++changed;
        }
        check(changed==0,"effects setting disables map animation without changing terrain");
        UnloadImage(flowA); UnloadImage(flowB);
        Settings::Get().effects=previousEffects;
        actor->ResetHealth(100); actor->SetPosition({2350,1550});
        GameplayEffectsTestAccess::Focus(arena,{2400,1600},0.43f);
        GameplayEffectsTestAccess::Time(arena,1.4f);
        arena.Draw(target); capture("bin/magma-rift-gameplay-preview.png");
        actor->SetName("rexxyqt");
        auto* targetBot=GameplayEffectsTestAccess::Bot(arena,0);
        for(int weapon=0;weapon<WeaponCount;++weapon) {
            targetBot->ResetHealth(100); targetBot->SetName("bot"+std::to_string(weapon+1));
            actor->SwitchWeapon(weapon);
            GameplayEffectsTestAccess::Kill(arena,actor,targetBot);
            check(GameplayEffectsTestAccess::FeedWeapon(arena)==weapon,"offline kill feed records the killing gun");
        }
        check(GameplayEffectsTestAccess::FeedCount(arena)==5 && GameplayEffectsTestAccess::FeedKiller(arena)=="rexxyqt","offline feed shows killer and keeps the latest five kills");
        actor->SwitchWeapon(2);
        check(GameplayEffectsTestAccess::FeedWeapon(arena)==4,"switching guns never changes a recorded kill icon");
        GameplayEffectsTestAccess::FeedTime(arena,.25f);
        arena.Draw(target); capture("bin/kill-feed-preview.png");
        for(int i=0;i<3;++i) { targetBot->ResetHealth(100); GameplayEffectsTestAccess::Kill(arena,actor,targetBot); }
        check(GameplayEffectsTestAccess::FeedCount(arena)==5,"rapid kills keep the HUD feed bounded");
        GameplayEffectsTestAccess::FeedTime(arena,8);
        check(GameplayEffectsTestAccess::FeedCount(arena)==0,"old kill messages expire");
        nm.selectedMapName=previous;
    }
    {
        MainMenuScreen menu;
        for (int i = 0; i < 240; ++i) menu.Update(1.0f/60);
        menu.Draw(target); capture("bin/menu-preview.png");
    }
    {
        MenuBackground background(1,MenuBackground::BackdropStyle::LIVE_GAMEPLAY);
        check(MenuBackgroundTestAccess::Fighters(background)==6 && MenuBackgroundTestAccess::PlayersOnly(background),
              "menu riot contains six player fighters and no monsters");
        for(int i=0;i<240;++i) background.Update(1.0f/60,1);
        background.Draw(target,1,0); capture("bin/menu-riot-preview.png");
        float minimumZoom=10,maximumZoom=0,maximumRoll=0;
        int subjectMask=0;bool visibleShake=false;
        for(int i=0;i<1200;++i) {
            background.Update(1.0f/60,1);
            if(i==300 || i==600) {
                background.Draw(target,1,0);
                capture(i==300?"bin/menu-cinematic-closeup.png":"bin/menu-cinematic-tracking.png");
            }
            auto camera=MenuBackgroundTestAccess::Camera(background);
            minimumZoom=std::min(minimumZoom,camera.zoom); maximumZoom=std::max(maximumZoom,camera.zoom);
            maximumRoll=std::max(maximumRoll,std::abs(camera.rotation));
            subjectMask|=1<<MenuBackgroundTestAccess::Subject(background);
            if(MenuBackgroundTestAccess::Shake(background)>.5f)
                visibleShake|=MenuBackgroundTestAccess::ShakeMovesOnlyRenderCamera(background);
            check(MenuBackgroundTestAccess::MinimumGap(background)>150,"menu fighters remain spread out during combat");
        }
        check(maximumZoom-minimumZoom>.55f && maximumRoll>.6f,"cinematic camera visibly zooms and changes its angle");
        check((subjectMask&(subjectMask-1))!=0,"cinematic shots change their fighter focus");
        check(visibleShake,"grenade explosions visibly shake the cinematic render camera");
        check(MenuBackgroundTestAccess::Throws(background)>=4,"menu riot repeatedly throws smoke and grenades");
        for (const auto& name:MapRegistry::GetInstance().GetMapNames()) {
            auto* map=MapRegistry::GetInstance().GetMap(name);
            MenuBackgroundTestAccess::Frame(background,map,VIRTUAL_WIDTH*0.5f);
            background.Draw(target,1,0);
            Image reference=LoadImageFromTexture(target.texture);
            MenuBackgroundTestAccess::Frame(background,map,VIRTUAL_WIDTH*0.72f);
            background.Draw(target,1,0);
            Image shifted=LoadImageFromTexture(target.texture);
            int differences=0;
            for (int y=8;y<VIRTUAL_HEIGHT;y+=16) for (int x=8;x<256;x+=16) {
                Color a=GetImageColor(reference,x,y),b=GetImageColor(shifted,x,y);
                if (a.r!=b.r || a.g!=b.g || a.b!=b.b) ++differences;
            }
            check(differences==0,"offset menu camera renders the same left map edge as equivalent centered framing");
            UnloadImage(reference); UnloadImage(shifted);
        }
    }
    {
        Character::SetCombatAudioEnabled(false);
        const float angles[]={-1.1f,-0.4f,0,0.4f,1.1f,2.1f,3.14159f,4.2f};
        for (int skin=0;skin<4;++skin) {
            Player actor({200,150},0,"",skin);
            for (int weapon=0;weapon<WeaponCount;++weapon) for (float angle:angles) {
                actor.ResetHealth(100); actor.SwitchWeapon(weapon);
                actor.SetFaceDirection(std::cos(angle)>=0?1:-1);
                Vector2 hand=actor.GetHandPosition(),targetPos{hand.x+std::cos(angle)*650,hand.y+std::sin(angle)*650};
                actor.SetAimTarget(targetPos); WeaponPose pose=actor.GetWeaponPose(targetPos);
                Vector2 delta{targetPos.x-pose.muzzle.x,targetPos.y-pose.muzzle.y};
                check(std::abs(delta.x*pose.direction.y-delta.y*pose.direction.x)<0.02f,"barrel axis points exactly at aim target for skin, angle, and facing");
                check(actor.Shoot(targetPos),"aligned shot fires");
                Vector2 bullet=actor.GetProjectiles().back()->GetPosition();
                check(std::abs(bullet.x-pose.muzzle.x-pose.direction.x*2)<0.01f && std::abs(bullet.y-pose.muzzle.y-pose.direction.y*2)<0.01f,"projectile spawns at the same barrel point as flash");
            }
            BeginTextureMode(target); ClearBackground({15,24,36,255});
            DrawText(TextFormat("BARREL / FLASH / TRACER  -  SKIN %d",skin),36,22,22,UiTheme::AccentGold());
            const char* labels[]={"SMG","SHOTGUN","PISTOL"};
            for (int row=0;row<2;++row) for (int weapon=0;weapon<3;++weapon) {
                actor.ResetHealth(100); actor.SwitchWeapon(weapon); actor.SetPosition({200+weapon*410.0f,140+row*300.0f});
                int face=row==0?1:-1; actor.SetFaceDirection(face);
                Vector2 hand=actor.GetHandPosition(); float angle=weapon==0?0:weapon==1?-0.45f:0.45f;
                Vector2 targetPos{hand.x+face*std::cos(angle)*650,hand.y+std::sin(angle)*650};
                actor.SetAimTarget(targetPos); actor.Shoot(targetPos);
                for (auto& projectile:actor.GetProjectiles()) projectile->Update(0.08f);
                actor.Draw(); DrawText(labels[weapon],44+weapon*410,68+row*300,18,LIGHTGRAY);
            }
            EndTextureMode(); capture(TextFormat("bin/weapon-alignment-skin%d-preview.png",skin));
            actor.ResetHealth(100); actor.SetFaceDirection(1); actor.ShootInDirection({3,-4});
            WeaponPose remotePose=actor.GetWeaponPose(actor.GetAimTarget());
            check(std::abs(remotePose.direction.x-0.6f)<0.001f && std::abs(remotePose.direction.y+0.8f)<0.001f,"network shot axis matches rendered barrel");
        }
        Character::SetCombatAudioEnabled(true);
    }
    {
        auto saved=Settings::Get(); int previous=NetworkManager::GetInstance().localWeaponSkin;
        WeaponSkinPreview preview;
        preview.SelectSkin(0,1,false); preview.SelectSkin(1,2,false); preview.SelectSkin(3,3,false);
        check(preview.GetWeaponSkin(0)==1 && preview.GetWeaponSkin(1)==2 && preview.GetWeaponSkin(2)==0 && preview.GetWeaponSkin(3)==3,"selecting one gun never overwrites another gun's skin or default");
        SkinTestPlayer actor({300,120},0,"",preview.GetPackedSkins());
        for(int slot=0;slot<WeaponCount;++slot) {
            check(actor.Texture(slot).id==LoadWeaponSlotTexture(slot,preview.GetPackedSkins()).id,"player uses each independently selected texture");
            check(LoadWeaponSlotTexture(slot,0).id!=0,"every gun has a valid default texture");
        }
        BeginTextureMode(target); ClearBackground({10,18,27,255});
        for(int slot=0;slot<WeaponCount;++slot) for(int skin=0;skin<WeaponSkinCount;++skin) {
            Rectangle card{slot*260.0f,skin*158.0f+35,260,153};
            DrawText(WeaponShortName(slot),(int)card.x+12,(int)card.y,16,UiTheme::AccentGold());
            Texture2D texture=LoadWeaponSlotTexture(slot,skin); auto geometry=GetWeaponGeometry(texture,slot);
            check(texture.id && geometry.bounds.width>10 && geometry.bounds.height>10,"all gun skins have visible sprites");
            float fit=std::min(225/geometry.bounds.width,85/geometry.bounds.height);
            Rectangle destination{card.x+130,card.y+66,geometry.bounds.width*fit,geometry.bounds.height*fit};
            DrawWeaponSkinTexture(texture,geometry.bounds,destination,{destination.width/2,destination.height/2},0,slot,skin);
            DrawText(WeaponSkinLabel(slot,skin),(int)card.x+12,(int)card.y+118,12,LIGHTGRAY);
        }
        EndTextureMode(); capture("bin/arsenal-skins-preview.png");
        Image rendered=LoadImageFromTexture(target.texture); ImageFlipVertical(&rendered);
        for(int slot=0;slot<WeaponCount;++slot) for(int skin=0;skin<WeaponSkinCount;++skin) {
            int visible=0;
            for(int y=skin*158+63;y<skin*158+145;++y) for(int x=slot*260+10;x<slot*260+250;++x) {
                Color color=GetImageColor(rendered,x,y);
                if(color.r>35 || color.g>40 || color.b>50) ++visible;
            }
            check(visible>60,"every skin actually draws a visible weapon in the gallery");
        }
        UnloadImage(rendered);
        for(int weapon : {3,4}) {
            actor.ResetHealth(100); actor.SwitchWeapon(weapon);
            check(actor.GetAmmo()==Weapons[weapon].maxAmmo,"new gun starts with its own magazine size");
            check(actor.Shoot({1000,150}),"new weapon fires");
            check(actor.GetProjectiles().back()->GetWeaponIndex()==weapon,"projectile remembers the fired gun for kill attribution");
            check(actor.GetAmmo()==Weapons[weapon].maxAmmo-1 && actor.GetProjectiles().back()->GetDamage()==Weapons[weapon].damage,"new gun consumes ammo and uses configured projectile damage");
            check(!actor.Shoot({1000,150}),"new gun respects fire cooldown");
            actor.TriggerReload(); actor.UpdateWithoutInput(Weapons[weapon].reloadTime+0.1f);
            check(!actor.IsReloading() && actor.GetAmmo()==Weapons[weapon].maxAmmo,"new gun finishes its own reload duration");
            RemotePlayer remote({900,120},CharacterPreview::BodyPath(1),preview.GetPackedSkins());
            remote.ApplyNetworkUpdate({900,120},0,weapon,-1,100,0,0,preview.GetPackedSkins());
            check(remote.GetWeaponIndex()==weapon,"online player can equip new weapon slots");
        }
        BeginTextureMode(target); ClearBackground({10,18,27,255});
        DrawText("ASSAULT RIFLE / SNIPER  -  GRIP, FLASH AND TRACER",36,24,22,UiTheme::AccentGold());
        for(int row=0;row<2;++row) for(int column=0;column<2;++column) {
            actor.ResetHealth(100); actor.SwitchWeapon(3+column); actor.SetPosition({310+column*650.0f,110+row*300.0f});
            actor.SetFaceDirection(row==0?1:-1); Vector2 hand=actor.GetHandPosition();
            Vector2 aim{hand.x+(row==0?600:-600),hand.y+(column==0?-130:100)};
            actor.SetAimTarget(aim); actor.Shoot(aim); for(auto& bullet:actor.GetProjectiles()) bullet->Update(0.05f); actor.Draw();
        }
        EndTextureMode(); capture("bin/new-weapons-alignment-preview.png");
        for(int slot=0;slot<WeaponCount;++slot) {
            preview.SetSelectedWeaponSlot(slot);
            BeginTextureMode(target); ClearBackground({10,18,27,255});
            preview.DrawPreview({180,90,940,330},preview.GetPackedSkins(),GetFontDefault());
            int skin=preview.GetWeaponSkin(slot); preview.DrawSelector({180,455,940,150},GetFontDefault(),skin,{-100,-100});
            EndTextureMode(); capture(TextFormat("bin/loadout-weapon%d-preview.png",slot));
        }
        Settings::Get()=saved; NetworkManager::GetInstance().localWeaponSkin=previous;
    }
    {
        SkinTestPlayer runner({250,200},0);
        check(runner.ActivateDash({3,4}),"dash starts with normalized direction");
        check(!runner.ActivateDash({1,0}),"cannot chain dash during cooldown");
        runner.UpdateSkills(1.0f);
        Vector2 end=runner.GetPosition();
        check(std::abs(end.x-309.4f)<0.01f && std::abs(end.y-279.2f)<0.01f,
              "longer dash travels exactly 99 units even on a long frame");
        check(runner.GetDashTimer()==0,"dash timer never goes negative");
        runner.ResetHealth(100); runner.SetPosition({250,200}); runner.ActivateDash({3,4});
        for(int i=0;i<60;++i) runner.UpdateSkills(1.0f/60);
        check(std::hypot(runner.GetPosition().x-end.x,runner.GetPosition().y-end.y)<0.01f,
              "dash distance stays identical across frame rates");
        runner.ResetHealth(100); check(runner.ActivateShield(),"shield activates");
        runner.TakeDamage(30); check(runner.GetHealth()==100,"active shield absorbs damage");
        check(!runner.ActivateShield(),"shield cannot stack");
        runner.UpdateSkills(2.01f); runner.TakeDamage(30);
        check(runner.GetHealth()==70 && !runner.ActivateShield(),"expired shield stops protection and keeps cooldown");
        runner.UpdateSkills(4); check(runner.ActivateShield(),"shield becomes ready after cooldown");
        runner.ResetHealth(100); runner.TakeDamage(100);
        check(!runner.ActivateDash({1,0}) && !runner.ActivateShield(),"dead players cannot activate abilities");
        runner.ResetHealth(100);
        check(runner.GetShieldTimer()==0 && runner.GetDashCooldown()==0,"respawn clears ability states");
        RemotePlayer peer({900,200},CharacterPreview::BodyPath(1));
        peer.ApplyNetworkUpdate({900,200},0,0,1,100,0,0,0,0.22f,2.0f);
        peer.Update(0.06f);
        check(peer.GetShieldTimer()>0 && peer.GetDashTimer()>0,"online peer displays synchronized abilities");
        check(peer.GetPosition().x==900 && peer.GetPosition().y==200,"remote dash never adds movement to replicated position");
        peer.ApplyNetworkUpdate({900,200},0,0,1,0,0,0,0,0.22f,2);
        check(peer.GetDashTimer()==0 && peer.GetShieldTimer()==0,"remote death clears ability visuals");
        bool previousEffects=Settings::Get().effects; Settings::Get().effects=true;
        runner.SetPosition({290,220}); runner.ActivateDash({1,0});
        for(int i=0;i<10;++i) runner.UpdateSkills(1.0f/60);
        SkinTestPlayer shield({870,220},0);
        shield.ActivateShield(); shield.UpdateSkills(0.16f); shield.TakeDamage(20);
        BeginTextureMode(target); ClearBackground({10,18,27,255});
        DrawText("DASH / AFTERIMAGES",190,120,22,UiTheme::AccentGold());
        DrawText("SHIELD / IMPACT",750,120,22,UiTheme::AccentGold());
        runner.SetAimTarget({1000,runner.GetHandPosition().y});
        shield.SetAimTarget({1400,shield.GetHandPosition().y});
        runner.Draw(); shield.Draw();
        EndTextureMode(); capture("bin/dash-shield-preview.png");
        BeginTextureMode(target); ClearBackground({10,18,27,255});
        DrawText("ORBIT SHIELD / ENERGY IN MOTION",38,36,24,UiTheme::AccentGold());
        for(int frame=0;frame<3;++frame) {
            shield.ResetHealth(100); shield.SetPosition({230+frame*420.0f,250});
            shield.ActivateShield(); shield.UpdateSkills(0.20f+frame*0.37f);
            shield.SetAimTarget({shield.GetPosition().x+500,shield.GetHandPosition().y});
            shield.Draw();
        }
        EndTextureMode(); capture("bin/shield-orbit-preview.png");
        Settings::Get().effects=previousEffects;
    }
    {
        auto& nm = NetworkManager::GetInstance();
        nm.localUsername = "Player"; nm.localSkinIndex = 1;
        if (!nm.HostRoom(17777)) return 10;
        nm.SetPlayerReady(true);
        LobbyScreen lobby;
        for (int i=0; i<180; ++i) lobby.Update(1.0f/60);
        lobby.Draw(target); capture("bin/lobby-ffa-preview.png");
        nm.UpdateLobbySettings(OnlineGameMode::ELIMINATION,0,0,8);
        nm.RequestTeamChange(1);
        lobby.Draw(target); capture("bin/lobby-preview.png");
        for (int i=1;i<16;++i) nm.UpsertPlayer(i,"Player "+std::to_string(i),i%4+1,0,i%2+1);
        lobby.Draw(target); capture("bin/lobby-full-roster-preview.png");
        for (int i=1;i<16;++i) nm.RemovePlayer(i);
        auto maps=MapRegistry::GetInstance().GetMapNames();
        if (maps.size()>1) { nm.selectedMapName=maps[1]; lobby.Draw(target); capture("bin/lobby-second-map-preview.png"); }
        nm.Disconnect();
    }
    {
        auto& nm=NetworkManager::GetInstance();
        check(nm.HostRoom(17777),"host online flag deathmatch");
        nm.UpdateLobbySettings(OnlineGameMode::DEATHMATCH,300,0,3);
        check(nm.currentGameMode==OnlineGameMode::DEATHMATCH,"online lobby accepts deathmatch flag mode");
        nm.SetPlayerTeam(nm.GetLocalPlayerID(),1); nm.UpsertPlayer(1,"Flag runner",2,0,2);
        nm.UpsertPlayer(2,"Teammate",1,0,1);
        nm.teamScores[1]=nm.teamScores[2]=0; nm.roundLimit=3;
        {
            LobbyScreen lobby; for (int i=0;i<90;++i) lobby.Update(1.0f/60);
            lobby.Draw(target); capture("bin/lobby-dm-flag-preview.png");
            GameplayScreen game(GameMode::ONLINE); auto* player=game.GetLocalPlayer();
            auto* runner=GameplayEffectsTestAccess::Remote(game,1); auto* ally=GameplayEffectsTestAccess::Remote(game,2);
            Vector2 home=GameplayEffectsTestAccess::Base(game,1),away=GameplayEffectsTestAccess::Base(game,2);
            player->SetPosition(home); runner->SetPosition(away); ally->SetPosition({home.x,home.y+400});
            GameplayEffectsTestAccess::OnlineFlow(game,0.1f);
            check(GameplayEffectsTestAccess::Carrier(game,1)==player && GameplayEffectsTestAccess::Carrier(game,2)==runner,"host updates local and remote flag pickups");
            auto packet=GameplayEffectsTestAccess::Snapshot(game);
            check(packet.flags[0].carrierID==nm.GetLocalPlayerID() && packet.flags[1].carrierID==1,"snapshot carries authoritative host zero and remote IDs");
            GameplayEffectsTestAccess::ResetFlag(game,1); GameplayEffectsTestAccess::ResetFlag(game,2);
            GameplayEffectsTestAccess::Apply(game,packet);
            check(GameplayEffectsTestAccess::Carrier(game,1)==player && GameplayEffectsTestAccess::Carrier(game,2)==runner,"replicated state resolves both flag carriers");
            runner->SetPosition(home); GameplayEffectsTestAccess::OnlineFlow(game,0.1f);
            check(nm.teamScores[2]==1,"remote player delivery scores on host");
            GameplayEffectsTestAccess::ResetFlag(game,2); runner->SetPosition(away);
            player->SetPosition({home.x,home.y+400}); ally->SetPosition(home);
            GameplayEffectsTestAccess::ResetFlag(game,1); GameplayEffectsTestAccess::OnlineFlow(game,0.1f);
            player->SetPosition({home.x+800,home.y}); ally->SetPosition({home.x+400,home.y});
            GameplayEffectsTestAccess::FlagAction(game,2); GameplayEffectsTestAccess::OnlineFlow(game,0.1f);
            check(GameplayEffectsTestAccess::Dropped(game,1),"remote carrier interaction drops flag on host");
            player->SetPosition(home); ally->SetPosition({home.x,home.y+400});
            GameplayEffectsTestAccess::ResetFlag(game,1); GameplayEffectsTestAccess::OnlineFlow(game,0.1f);
            player->SetPosition(away); GameplayEffectsTestAccess::OnlineFlow(game,0.1f);
            check(nm.teamScores[1]==1,"local player delivery scores in online deathmatch");
            auto empty=GameplayEffectsTestAccess::Snapshot(game);
            check(empty.flags[0].carrierID==UINT32_MAX,"reset snapshot distinguishes missing carrier from host ID zero");
            GameplayEffectsTestAccess::Focus(game,away); game.Draw(target); capture("bin/online-dm-flag-preview.png");
            nm.teamScores[1]=3; GameplayEffectsTestAccess::OnlineFlow(game,0.1f);
            check(GameplayEffectsTestAccess::MatchOver(game),"capture target ends online deathmatch");
        }
        nm.Disconnect();
    }
    {
        Offline::Setup saved=Offline::Get();
        Offline::Get().map="MagmaRift";
        OfflineMenuScreen menu;
        for (int i=0;i<180;++i) menu.Update(1.0f/60);
        check(OfflineMenuTestAccess::Preview(menu).height>=240,"offline map preview uses the taller layout");
        Offline::Get().mode=Offline::Mode::Training; Offline::Get().bots=4; Offline::Get().difficulty=1;
        menu.Draw(target); capture("bin/offline-training-setup-preview.png");
        Offline::Get().mode=Offline::Mode::TeamDeathmatch; Offline::Get().bots=7; Offline::Get().difficulty=2;
        menu.Draw(target); capture("bin/offline-setup-preview.png");
        OfflineMenuTestAccess::Scroll(menu,10000);
        check(OfflineMenuTestAccess::Offset(menu)==OfflineMenuTestAccess::Maximum(menu),"setup scrolling clamps at the last visible setting");
        check(!OfflineMenuTestAccess::HiddenBotControl(menu),"clipped controls cannot be activated through the map preview");
        menu.Draw(target); capture("bin/offline-setup-scrolled-preview.png");
        Offline::Get().mode=Offline::Mode::Deathmatch;
        menu.Draw(target); capture("bin/offline-flag-setup-preview.png");
        OfflineMenuTestAccess::Scroll(menu,-10000);
        check(OfflineMenuTestAccess::Offset(menu)==0,"setup scrolling clamps at the top");
        Offline::Get()=saved;
    }
    {
        auto saved=Offline::Get();
        Offline::Get().mode=Offline::Mode::Training; Offline::Get().bots=0;
        { GameplayScreen game(GameMode::PRACTICE); check(GameplayEffectsTestAccess::BotCount(game)==0,"zero bot training supported"); }
        Offline::Get().mode=Offline::Mode::FreeForAll; Offline::Get().bots=15; Offline::Get().killLimit=5;
        {
            GameplayScreen game(GameMode::PRACTICE);
            check(GameplayEffectsTestAccess::BotCount(game)==15,"configured maximum bot count spawned");
            check(GameplayEffectsTestAccess::Enemy(game,game.GetLocalPlayer(),GameplayEffectsTestAccess::Bot(game,0)),"FFA bots are opponents");
            for (int i=0;i<5;++i) game.GetLocalPlayer()->AddKill();
            GameplayEffectsTestAccess::Flow(game,0.1f);
            check(GameplayEffectsTestAccess::MatchOver(game),"FFA kill target ends offline match");
            GameplayEffectsTestAccess::Flow(game,6.1f); check(game.ShouldReturnToLobby(),"offline match returns to setup");
        }
        Offline::Get().mode=Offline::Mode::TeamDeathmatch; Offline::Get().bots=3; Offline::Get().playerTeam=1;
        {
            GameplayScreen game(GameMode::PRACTICE); auto* player=game.GetLocalPlayer();
            auto* enemy=GameplayEffectsTestAccess::Bot(game,0); auto* ally=GameplayEffectsTestAccess::Bot(game,1);
            check(enemy->GetTeamID()==2 && ally->GetTeamID()==1,"offline bots assigned to opposing and allied squads");
            check(!GameplayEffectsTestAccess::Enemy(game,player,ally),"offline friendly fire blocked");
            for (int i=0;i<5;++i) { enemy->ResetHealth(100); GameplayEffectsTestAccess::Kill(game,ally,enemy); }
            check(GameplayEffectsTestAccess::Score(game,1)==5,"allied bot kills count toward TDM team score");
            GameplayEffectsTestAccess::Flow(game,0.1f); check(GameplayEffectsTestAccess::MatchOver(game),"TDM target ends match");
            game.Draw(target); capture("bin/offline-tdm-preview.png");
        }
        Offline::Get().mode=Offline::Mode::FreeForAll;
        for (int level=0;level<3;++level) {
            Offline::Get().difficulty=level;
            GameplayScreen game(GameMode::PRACTICE);
            auto* bot=GameplayEffectsTestAccess::Bot(game,0); Vector2 origin=bot->GetPosition();
            bot->MoveToObjective(0.1f,{origin.x+500,origin.y});
            const float expected[]={8.5f,11.0f,14.5f};
            check(std::abs(bot->GetPosition().x-origin.x-expected[level])<0.05f,"AI movement reflects selected difficulty");
            game.GetLocalPlayer()->SetGodMode(true);
            for (int frame=0;frame<180;++frame) check(game.Update(1.0f/60),"offline match simulates at selected difficulty");
        }
        Offline::Get().difficulty=1;
        Offline::Get().mode=Offline::Mode::Elimination; Offline::Get().roundLimit=3;
        {
            GameplayScreen game(GameMode::PRACTICE); auto* player=game.GetLocalPlayer();
            for (int i : {0,2}) GameplayEffectsTestAccess::Kill(game,player,GameplayEffectsTestAccess::Bot(game,i));
            GameplayEffectsTestAccess::Flow(game,0.1f);
            check(GameplayEffectsTestAccess::RoundOver(game) && GameplayEffectsTestAccess::Score(game,1)==1,"elimination awards round for squad wipe");
            GameplayEffectsTestAccess::Practice(game,4);
            check(GameplayEffectsTestAccess::Bot(game,0)->IsDead(),"eliminated bots do not respawn during a round");
            game.Draw(target); capture("bin/offline-elimination-preview.png");
            GameplayEffectsTestAccess::Flow(game,3.1f);
            check(!GameplayEffectsTestAccess::RoundOver(game) && !GameplayEffectsTestAccess::Bot(game,0)->IsDead(),"next round resets players and bots");
        }
        Offline::Get().mode=Offline::Mode::Deathmatch;
        Offline::Get().roundLimit=3;
        {
            GameplayScreen game(GameMode::PRACTICE); auto* player=game.GetLocalPlayer();
            Vector2 home=GameplayEffectsTestAccess::Base(game,1),enemyBase=GameplayEffectsTestAccess::Base(game,2);
            Vector2 midfield{(home.x+enemyBase.x)/2,home.y-250};
            auto* teammate=GameplayEffectsTestAccess::Bot(game,1);
            teammate->SetPosition(home); player->SetPosition({home.x+75,home.y});
            GameplayEffectsTestAccess::Flags(game,0.1f);
            check(GameplayEffectsTestAccess::Carrier(game,1)==player,"nearby player gets pickup priority over bot on flag pedestal");
            GameplayEffectsTestAccess::ResetFlag(game,1); player->SetPosition(midfield);
            GameplayEffectsTestAccess::Flags(game,0.1f);
            check(GameplayEffectsTestAccess::Carrier(game,1)==teammate,"teammate can collect flag when player is away");
            player->SetPosition({home.x+120,home.y}); GameplayEffectsTestAccess::Flags(game,0.1f,true);
            check(GameplayEffectsTestAccess::Carrier(game,1)==player && !GameplayEffectsTestAccess::Dropped(game,1),"interact takes flag from teammate without dropping it on same press");
            GameplayEffectsTestAccess::ResetFlag(game,1);
            for (int i=0;i<3;++i) GameplayEffectsTestAccess::Bot(game,i)->SetPosition({midfield.x,midfield.y+500});
            player->SetPosition(home); GameplayEffectsTestAccess::Flags(game,0.1f);
            check(GameplayEffectsTestAccess::Carrier(game,1)==player,"player picks up own team flag at home base");
            GameplayEffectsTestAccess::Flags(game,0.1f);
            check(GameplayEffectsTestAccess::Score(game,1)==0,"own home base does not score delivery");
            GameplayEffectsTestAccess::Focus(game,home); game.Draw(target); capture("bin/flag-base-preview.png");
            player->SetPosition(midfield); GameplayEffectsTestAccess::Focus(game,midfield);
            game.Draw(target); capture("bin/flag-carrier-preview.png");
            player->SetPosition(enemyBase); GameplayEffectsTestAccess::Flags(game,0.1f);
            check(GameplayEffectsTestAccess::Score(game,1)==1 && GameplayEffectsTestAccess::FlagDelay(game,1)>0,"enemy base delivery scores once and resets flag");
            GameplayEffectsTestAccess::Flags(game,0.2f); check(GameplayEffectsTestAccess::Score(game,1)==1,"flag cannot score repeatedly while resetting");
            GameplayEffectsTestAccess::ResetFlag(game,1); player->SetPosition(home); GameplayEffectsTestAccess::Flags(game,0.1f);
            player->SetPosition(midfield); GameplayEffectsTestAccess::Flags(game,0.1f,true);
            check(GameplayEffectsTestAccess::Dropped(game,1) && !GameplayEffectsTestAccess::Carrier(game,1),"player can drop flag for teammate");
            auto* ally=GameplayEffectsTestAccess::Bot(game,1); ally->SetPosition(midfield);
            GameplayEffectsTestAccess::Flags(game,0.1f); check(GameplayEffectsTestAccess::Carrier(game,1)==ally,"teammate recovers dropped flag");
            player->SetPosition({midfield.x,midfield.y+500});
            ally->TakeDamage(10000); GameplayEffectsTestAccess::Flags(game,0.1f);
            check(GameplayEffectsTestAccess::Dropped(game,1),"flag drops when carrier dies");
            auto* defender=GameplayEffectsTestAccess::Bot(game,0); defender->SetPosition(midfield);
            GameplayEffectsTestAccess::Flags(game,0.1f);
            check(!GameplayEffectsTestAccess::Dropped(game,1) && !GameplayEffectsTestAccess::Carrier(game,1),"enemy intercept returns dropped flag home");
            check(std::abs(GameplayEffectsTestAccess::FlagPosition(game,1).x-home.x)<0.1f,"returned flag is on home pedestal");
            player->SetPosition(midfield); defender->SetPosition(home); GameplayEffectsTestAccess::Flags(game,0.1f);
            check(!GameplayEffectsTestAccess::Carrier(game,1),"enemy cannot steal a home flag in delivery mode");
            defender->SetPosition({midfield.x,midfield.y+500}); player->SetPosition(home); GameplayEffectsTestAccess::Flags(game,0.1f);
            player->SetPosition(midfield); GameplayEffectsTestAccess::Flags(game,0.1f,true); GameplayEffectsTestAccess::Flags(game,15.1f);
            check(!GameplayEffectsTestAccess::Dropped(game,1) && std::abs(GameplayEffectsTestAccess::FlagPosition(game,1).x-home.x)<0.1f,"abandoned flag automatically returns");
            for (int capture=0;capture<2;++capture) {
                GameplayEffectsTestAccess::ResetFlag(game,1); player->SetPosition(home); GameplayEffectsTestAccess::Flags(game,0.1f);
                player->SetPosition(enemyBase); GameplayEffectsTestAccess::Flags(game,0.1f);
            }
            GameplayEffectsTestAccess::Flow(game,0.1f); check(GameplayEffectsTestAccess::MatchOver(game),"capture target ends flag race");
        }
        {
            Offline::Get().bots=1;
            GameplayScreen game(GameMode::PRACTICE); auto* bot=GameplayEffectsTestAccess::Bot(game,0);
            Vector2 botBase=GameplayEffectsTestAccess::Base(game,2),target=GameplayEffectsTestAccess::Base(game,1);
            game.GetLocalPlayer()->SetPosition({target.x,target.y-400}); bot->SetPosition(botBase);
            GameplayEffectsTestAccess::Flags(game,0.1f); check(GameplayEffectsTestAccess::Carrier(game,2)==bot,"bot collects its team flag");
            float before=std::abs(bot->GetPosition().x-target.x);
            GameplayEffectsTestAccess::Practice(game,0.2f);
            check(std::abs(bot->GetPosition().x-target.x)<before,"bot carrier advances toward enemy base");
            bot->SetPosition(target); GameplayEffectsTestAccess::Flags(game,0.1f);
            check(GameplayEffectsTestAccess::Score(game,2)==1,"bot delivers flag into enemy base");
            bot->TakeDamage(10000); GameplayEffectsTestAccess::Practice(game,4.1f);
            check(!bot->IsDead() && std::hypot(bot->GetPosition().x-botBase.x,bot->GetPosition().y-botBase.y)<111,"flag bots respawn at their own visible base");
            game.GetLocalPlayer()->SetPosition(botBase); GameplayEffectsTestAccess::Respawn(game);
            check(std::hypot(game.GetLocalPlayer()->GetPosition().x-target.x,game.GetLocalPlayer()->GetPosition().y-target.y)<111,"player respawn belongs to own team base");
        }
        Offline::Get().mode=Offline::Mode::TeamDeathmatch; Offline::Get().bots=3;
        {
            GameplayScreen game(GameMode::PRACTICE); auto* player=game.GetLocalPlayer();
            Vector2 base=GameplayEffectsTestAccess::Base(game,1);
            player->SetPosition(GameplayEffectsTestAccess::Base(game,2)); GameplayEffectsTestAccess::Respawn(game);
            check(std::hypot(player->GetPosition().x-base.x,player->GetPosition().y-base.y)<111,"TDM player respawns at home base");
            GameplayEffectsTestAccess::Focus(game,base); game.Draw(target); capture("bin/tdm-base-preview.png");
        }
        Offline::Get().mode=Offline::Mode::FreeForAll;
        {
            GameplayScreen game(GameMode::PRACTICE); auto* player=game.GetLocalPlayer();
            player->SetPosition({900,200}); GameplayEffectsTestAccess::Respawn(game);
            Vector2 spawn=GameplayEffectsTestAccess::DmSpawn(game,0);
            check(std::hypot(player->GetPosition().x-spawn.x,player->GetPosition().y-spawn.y)<1,"DM player uses assigned respawn outpost");
            auto* bot=GameplayEffectsTestAccess::Bot(game,0); bot->TakeDamage(10000); GameplayEffectsTestAccess::Practice(game,4.1f);
            spawn=GameplayEffectsTestAccess::DmSpawn(game,1);
            check(!bot->IsDead() && std::hypot(bot->GetPosition().x-spawn.x,bot->GetPosition().y-spawn.y)<1,"DM bot respawns at assigned outpost");
            GameplayEffectsTestAccess::Focus(game,player->GetPosition()); game.Draw(target); capture("bin/dm-outpost-preview.png");
        }
        Offline::Get()=saved;
    }
    {
        GameplayScreen practice(GameMode::PRACTICE);
        for (int i = 0; i < 600; ++i) { if (!practice.Update(1.0f/60)) return 2; }
        practice.Draw(target); capture("bin/gameplay-preview.png");
        auto* p = practice.GetLocalPlayer();
        p->ResetHealth(100); p->SetGodMode(true);
        p->Shoot({p->GetPosition().x+300, p->GetPosition().y}); p->TriggerReload();
        if (!p->IsReloading()) return 11;
        if (!p->ActivateDash({1,0}) || !p->ActivateShield()) return 12;
        practice.Draw(target); capture("bin/reload-preview.png");
        Vector2 position = p->GetPosition(); p->SetAimTarget({position.x+240, position.y});
        if (!practice.ThrowGrenade(0) || !practice.ThrowGrenade(1)) return 13;
        if (practice.ThrowGrenade(0) || practice.ThrowGrenade(1)) return 14;
        if (practice.GetGrenadeCooldown(0) != 8 || practice.GetGrenadeCooldown(1) != 12) return 15;
        for (int i=0; i<60; ++i) practice.Update(1.0f/60);
        practice.Draw(target); capture("bin/grenade-preview.png");
        for (int i=0; i<720; ++i) practice.Update(1.0f/60);
        if (practice.GetGrenadeCooldown(0)>0 || practice.GetGrenadeCooldown(1)>0) return 16;
        p->TriggerReload(); p->ResetHealth(100);
        if (p->GetAmmo() != p->GetMaxAmmo() || p->IsReloading()) return 3;
        {
            GameplayScreen effects(GameMode::PRACTICE);
            auto* victim=effects.GetLocalPlayer();
            Vector2 origin=victim->GetPosition();
            victim->SetAimTarget(origin);
            check(effects.ThrowGrenade(1),"throw poison smoke");
            GameplayEffectsTestAccess::Step(effects,0.8f);
            check(victim->GetHealth()==100,"smoke fuse does not damage thrower");
            GameplayEffectsTestAccess::Step(effects,0.5f);
            check(victim->GetHealth()==94,"own smoke damages thrower");
            victim->SetPosition({origin.x+1000,origin.y});
            GameplayEffectsTestAccess::Step(effects,0.5f);
            check(victim->GetHealth()==94,"leaving smoke stops poison damage");
            victim->SetPosition(origin);
            GameplayEffectsTestAccess::Step(effects,0.5f);
            check(victim->GetHealth()==88,"reentering smoke resumes damage");
            effects.Draw(target); capture("bin/poison-spread-preview.png");
            GameplayEffectsTestAccess::Step(effects,6.6f);
            check(victim->GetHealth()==10,"all remaining exposure ticks applied with large delta");
            GameplayEffectsTestAccess::Step(effects,2);
            check(victim->GetHealth()==10,"expired smoke stops damaging");
            GameplayEffectsTestAccess::Reset(effects);
            victim->ResetHealth(6);
            check(effects.ThrowGrenade(1),"throw lethal self smoke");
            GameplayEffectsTestAccess::Step(effects,1.3f);
            check(victim->IsDead() && victim->GetDeaths()==1 && victim->GetKills()==0,"self poison death gives no kill");
            GameplayEffectsTestAccess::Reset(effects);
            victim->ResetHealth(100);
            GameplayEffectsTestAccess::RemoteSmoke(effects,origin,2);
            GameplayEffectsTestAccess::Step(effects,1.3f);
            check(victim->GetHealth()==94,"replicated enemy smoke damages local player once");
            GameplayEffectsTestAccess::Reset(effects);
            GameplayEffectsTestAccess::RemoteBlast(effects,{origin.x+1000,origin.y});
            GameplayEffectsTestAccess::Step(effects,0.81f);
            check(GameplayEffectsTestAccess::Shake(effects)==0,"distant replicated blast causes no shake");
            GameplayEffectsTestAccess::Reset(effects);
            GameplayEffectsTestAccess::RemoteBlast(effects,{origin.x+150,origin.y});
            GameplayEffectsTestAccess::Step(effects,0.91f);
            check(GameplayEffectsTestAccess::Shake(effects)>0,"near replicated blast shakes camera");
            effects.Draw(target); capture("bin/explosion-preview.png");
            GameplayEffectsTestAccess::Step(effects,0.9f);
            effects.Draw(target); capture("bin/explosion-cracks-preview.png");
            check(GameplayEffectsTestAccess::Shake(effects)==0,"camera shake decays");
        }
        {
            auto& nm=NetworkManager::GetInstance();
            check(nm.HostRoom(17777),"host online poison regression");
            nm.currentGameMode=OnlineGameMode::TEAM_DEATHMATCH;
            nm.SetPlayerTeam(nm.GetLocalPlayerID(),1); nm.UpsertPlayer(1,"Opponent",2,0,2);
            GameplayScreen online(GameMode::ONLINE);
            auto* victim=online.GetLocalPlayer(); victim->SetTeamID(1);
            Vector2 homeBase=GameplayEffectsTestAccess::Base(online,1);
            check(std::hypot(victim->GetPosition().x-homeBase.x,victim->GetPosition().y-homeBase.y)<111,"online TDM begins inside visible team base");
            {
                GameplayScreen peerView(GameMode::ONLINE);
                Vector2 same=GameplayEffectsTestAccess::Base(peerView,1);
                check(std::hypot(homeBase.x-same.x,homeBase.y-same.y)<0.01f,"base coordinates deterministic across game instances");
            }
            victim->SetPosition(GameplayEffectsTestAccess::Base(online,2)); GameplayEffectsTestAccess::Respawn(online);
            check(std::hypot(victim->GetPosition().x-homeBase.x,victim->GetPosition().y-homeBase.y)<111,"online TDM respawn uses own base");
            Vector2 origin=victim->GetPosition(); victim->SetAimTarget(origin);
            victim->ResetHealth(6);
            check(online.ThrowGrenade(1),"throw online self smoke");
            GameplayEffectsTestAccess::Step(online,1.3f);
            check(victim->IsDead() && nm.localDeaths==1 && nm.localKills==0 && nm.teamScores[1]==0,"online suicide records death without kill or team score");
            GameplayEffectsTestAccess::Reset(online); victim->ResetHealth(100);
            GameplayEffectsTestAccess::RemoteSmoke(online,origin,1);
            GameplayEffectsTestAccess::Step(online,1.3f);
            check(victim->GetHealth()==100,"friendly smoke keeps team protection");
            GameplayEffectsTestAccess::Reset(online);
            GameplayEffectsTestAccess::RemoteSmoke(online,origin,2);
            GameplayEffectsTestAccess::Step(online,1.3f);
            check(victim->GetHealth()==94,"online enemy smoke damages local player");
            nm.Disconnect();
        }
        Settings::Open();
        BeginTextureMode(target); Settings::Draw(); EndTextureMode(); capture("bin/settings-preview.png");
    }
    {
        auto& nm=NetworkManager::GetInstance();
        check(nm.HostRoom(17777),"host online kill feed regression");
        nm.currentGameMode=OnlineGameMode::FREE_FOR_ALL;
        nm.UpsertPlayer(1,"bot1",1,0,0);
        GameplayScreen game(GameMode::ONLINE);
        GameplayEffectsTestAccess::OnlineKill(game,nm.GetLocalPlayerID(),1,3);
        GameplayEffectsTestAccess::OnlineKill(game,nm.GetLocalPlayerID(),1,3);
        check(GameplayEffectsTestAccess::FeedCount(game)==1 && GameplayEffectsTestAccess::FeedWeapon(game)==3,"online kill event records one feed entry despite duplicate packets");
        GameplayEffectsTestAccess::FeedTime(game,.3f);
        game.Draw(target); capture("bin/online-kill-feed-preview.png");
        nm.Disconnect();
    }
    // Render the same HUD through each online-mode branch.
    {
        auto& nm = NetworkManager::GetInstance();
        if (!nm.HostRoom(17777)) return 17;
        const OnlineGameMode modes[] = {OnlineGameMode::ELIMINATION, OnlineGameMode::FREE_FOR_ALL, OnlineGameMode::TEAM_DEATHMATCH};
        const char* paths[] = {"bin/online-elimination-preview.png", "bin/online-ffa-preview.png", "bin/online-tdm-preview.png"};
        for (int i=0;i<3;++i) {
            nm.currentGameMode=modes[i];
            GameplayScreen online(GameMode::ONLINE);
            online.Draw(target); capture(paths[i]);
        }
        nm.Disconnect();
    }
    UnloadRenderTexture(target); TextureManager::UnloadAll();
    NetworkManager::GetInstance().Shutdown(); CloseAudioDevice(); CloseWindow();
    std::cout << "Lobby, gameplay, reload, dash, shield, grenade cooldowns, smoke, respawn reset, and settings rendered successfully\n";
}
