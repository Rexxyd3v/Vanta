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
#include "ui/ui_theme.h"
#include <iostream>
#include <cmath>
#include <cstdlib>

struct MenuBackgroundTestAccess {
    static void Frame(MenuBackground& background,MapData* map,float offsetX) {
        background.currentMapData=map;
        background.camera={{offsetX,340},{600+offsetX,740},0,1};
    }
};
struct GameplayEffectsTestAccess {
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
    auto target = LoadRenderTexture(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
    auto capture = [&](const char* path) {
        Image img = LoadImageFromTexture(target.texture); ImageFlipVertical(&img);
        ExportImage(img, path); UnloadImage(img);
        BeginDrawing(); ClearBackground(BLACK); EndDrawing();
    };
    {
        MainMenuScreen menu;
        for (int i = 0; i < 180; ++i) menu.Update(1.0f/60);
        menu.Draw(target); capture("bin/menu-preview.png");
    }
    {
        MenuBackground background(1,MenuBackground::BackdropStyle::LIVE_GAMEPLAY);
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
            for (int weapon=0;weapon<3;++weapon) for (float angle:angles) {
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
        OfflineMenuScreen menu;
        for (int i=0;i<180;++i) menu.Update(1.0f/60);
        Offline::Get().mode=Offline::Mode::TeamDeathmatch; Offline::Get().bots=7; Offline::Get().difficulty=2;
        menu.Draw(target); capture("bin/offline-setup-preview.png");
        Offline::Get().mode=Offline::Mode::Deathmatch;
        menu.Draw(target); capture("bin/offline-flag-setup-preview.png");
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
