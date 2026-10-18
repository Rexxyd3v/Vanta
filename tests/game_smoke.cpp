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
#include "ui/loadout_storage.h"
#include "rules/grenade_rules.h"
#include "audio/combat_audio.h"
#include "ui/weapon_hands.h"
#include "ui/weapon_reload.h"
#include "ui/match_presentation.h"
#include <filesystem>
#include "ui/ui_theme.h"
#include "map_loader/map_effects.h"
#include <iostream>
#include <cmath>
#include <cstdlib>

struct VoiceTestAccess {
    static bool ExerciseAudioPath() {
        ProximityVoiceChat voice;
        voice.setTransmitEnabled(true);
        bool ok=!voice.isLocalSpeaking(); // A failed/unopened microphone must not claim to transmit.
        int error=0;
        voice.m_encoder=opus_encoder_create(48000,1,OPUS_APPLICATION_VOIP,&error);
        if(!voice.m_encoder || error!=OPUS_OK) return false;
        voice.m_initialized.store(true);
        ma_device device{}; device.pUserData=&voice;
        std::vector<int16_t> input(960);
        for(int i=0;i<960;++i) input[i]=(int16_t)(std::sin(i*2*PI*440/48000)*8000);
        voice.captureCallback(&device,nullptr,input.data(),960);
        ok&=voice.m_outgoing.empty(); // No key held: no audio packets.
        voice.setTransmitEnabled(true);
        voice.captureCallback(&device,nullptr,input.data(),960);
        ok&=voice.m_outgoing.size()==1;
        auto packet=voice.m_outgoing.front();
        ok&=packet.frameSize>0 && packet.frameSize<=MAX_OPUS_FRAME_SIZE;
        voice.processVoicePacket(&packet,11);
        size_t samples=voice.m_senders[11]->writePos;
        voice.processVoicePacket(&packet,11);
        ok&=voice.m_senders[11]->writePos==samples; // Duplicate packets never repeat speech.
        voice.setOutputVolume(.5f);
        std::vector<float> output(960);
        voice.playbackCallback(&device,output.data(),nullptr,960);
        float energy=0;for(float sample:output) energy+=sample*sample;
        ok&=energy>.01f && voice.isUserSpeaking(11);
        for(int i=0;i<15;++i) voice.captureCallback(&device,nullptr,input.data(),960);
        ok&=voice.m_outgoing.size()==10; // Bounded backlog.
        voice.captureCallback(&device,nullptr,input.data(),24);
        voice.setTransmitEnabled(false);
        ok&=voice.m_outgoing.empty() && voice.m_captureBuf.empty();
        voice.captureCallback(&device,nullptr,input.data(),960);
        ok&=voice.m_outgoing.empty(); // Releasing PTT stops all new audio.
        voice.setTransmitEnabled(true);
        voice.captureCallback(&device,nullptr,nullptr,960);
        ok&=voice.m_outgoing.empty();
        voice.setTransmitEnabled(false);
        voice.m_initialized.store(false); // Codec-only fixture owns no physical devices.
        opus_encoder_destroy(voice.m_encoder); voice.m_encoder=nullptr;
        for(auto& entry:voice.m_senders) { opus_decoder_destroy(entry.second->decoder);delete entry.second; }
        voice.m_senders.clear();
        return ok;
    }
};
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
struct LobbyTestAccess {
    static float Height(const LobbyScreen& lobby) { return lobby.previewBounds.height; }
    static void Scroll(LobbyScreen& lobby,float amount) { lobby.ScrollSetup(amount); }
    static float Offset(const LobbyScreen& lobby) { return lobby.setupScroll; }
    static float Maximum(const LobbyScreen& lobby) { return lobby.SetupScrollMax(); }
    static bool HiddenChip(const LobbyScreen& lobby) {
        Rectangle local{0,24,268,34},r=lobby.SetupBounds(local);
        return lobby.SetupHit(local,{r.x+10,r.y+2});
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
    static bool CanBag(const GameplayScreen& game) { return game.CanChangeBag(); }
    static bool PickBag(GameplayScreen& game,int bag) { return game.SelectBag(bag); }
    static int ActiveBag(const GameplayScreen& game) { return game.activeBag; }
    static Vector2 BagHome(const GameplayScreen& game) { return game.bagSpawnPosition; }
    static void BagMenu(GameplayScreen& game) { game.bagMenuOpen=true; }
    static void Combat(GameplayScreen& game) { game.UpdateEquipmentCombat(); }
    static void RemoteFlash(GameplayScreen& game,Vector2 target) { game.grenades.push_back({target,target,0,2,false,99,2}); }
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
    static void RestartMatch(GameplayScreen& game){game.RestartMatch();}
    static void LeaveResults(GameplayScreen& game){game.LeaveResults();}
    static void NoteDamage(GameplayScreen& g,Character* a,Character* v,float amount,int weapon,uint32_t shot=0){g.NoteDamage(a,v,amount,weapon,0,shot);}
    static void Spectate(GameplayScreen& g){g.UpdateSpectator(.1f);}
    static Character* Watched(GameplayScreen& g){return g.spectateTarget;}
    static int Choices(GameplayScreen& g){return (int)g.SpectatorCandidates().size();}
    static int RecapEntries(GameplayScreen& g){return (int)g.lifeDamage.size();}
    static float RecapDamage(GameplayScreen& g){float total=0;for(auto& hit:g.lifeDamage)total+=hit.damage;return total;}
    static Character* Mvp(GameplayScreen& g){return g.RankedActors().front();}
    static bool Receipt(GameplayScreen& g,const PacketDamageConfirm& p){return g.AcceptDamageConfirm(p);}
    static void ResultAge(GameplayScreen& g,float age){g.resultsAge=age;}
    static std::vector<Character*> Winners(GameplayScreen& g){return g.VictoryActors();}
    static void Winner(GameplayScreen& g,int team){g.matchWinner=team;}
    static bool BotHazards(GameplayScreen& g){auto* bot=g.bots.front().get();Vector2 p=bot->GetPosition();
        g.bonusBlasts.push_back({p,1,5});g.PrepareBotTactics(bot);bot->UpdateAI(.02f,{p.x+500,p.y});bool fire=bot->IsEvading();g.bonusBlasts.clear();
        g.grenades.push_back({p,p,.2f,0,true});g.PrepareBotTactics(bot);bot->UpdateAI(.02f,{p.x+500,p.y});bool grenade=bot->IsEvading();g.grenades.clear();return fire && grenade;
    }
    static void MedalKill(GameplayScreen& g,uint64_t killer,uint64_t victim,uint64_t local){g.NoteMedalKill(killer,victim,local,true);}
    static int Medals(GameplayScreen& g){return (int)g.medalQueue.size();}
    static Medals::Kind MedalKind(GameplayScreen& g){return g.medalQueue.front().kind;}
    static void MedalTick(GameplayScreen& g,float dt){g.UpdateMedals(dt);}
    static void DrawMedal(GameplayScreen& g){g.DrawMedals();}

    static void Finish(GameplayScreen& g){g.matchEndTimer=6;g.offlineResult="GLOBAL RISK WINS";}
    static void Capture(GameplayScreen& g,Character* c){g.CreditCapture(c);}
    static bool BulletWall(GameplayScreen& g){
        auto groups=g.allObjectGroups;g.allObjectGroups.clear();auto* p=g.player;p->ResetHealth(100);p->SwitchWeapon(0);Vector2 hand=p->GetHandPosition();
        p->Shoot({hand.x+1000,hand.y});auto bullet=p->GetProjectiles().back();Vector2 from=bullet->GetPosition();
        TmxObject wall{};wall.type=OBJECT_TYPE_RECTANGLE;wall.x=from.x+25;wall.y=from.y-30;wall.width=5;wall.height=60;
        TmxObjectGroup group{};group.objects=&wall;group.objectsLength=1;g.allObjectGroups.push_back(group);bullet->Update(.1f);g.UpdateBulletWorld();
        bool hit=!bullet->IsActive() && !g.impacts.empty();g.allObjectGroups=std::move(groups);return hit;
    }
    static void FlagPlan(GameplayScreen& g){g.PlanFlagBots();}
    static int FlagRoles(GameplayScreen& g,int team,int role){int count=0;for(const auto& entry:g.flagBotPlans)if(!entry.first->IsDead() && entry.first->GetTeamID()==team && (int)entry.second.role==role)++count;return count;}
    static bool SpreadPlans(GameplayScreen& g,int team){std::vector<Vector2> destinations;for(const auto& entry:g.flagBotPlans)if(entry.first->GetTeamID()==team && !entry.first->IsDead())destinations.push_back(entry.second.destination);
        for(size_t i=0;i<destinations.size();++i)for(size_t j=i+1;j<destinations.size();++j)if(std::hypot(destinations[i].x-destinations[j].x,destinations[i].y-destinations[j].y)<110)return false;return true;}
    static void AssignCarrier(GameplayScreen& g,int team,Character* actor){g.flags[team].carrier=actor;g.flags[team].dropped=false;g.flags[team].respawnTimer=0;}
    static bool FlagDetour(GameplayScreen& g){
        auto groups=g.allObjectGroups;g.allObjectGroups.clear();g.flagWalkGrid.clear();g.flagGridLinks.clear();
        TmxObject wall{};wall.type=OBJECT_TYPE_RECTANGLE;wall.x=1400;wall.y=400;wall.width=96;wall.height=900;
        TmxObjectGroup group{};group.objects=&wall;group.objectsLength=1;g.allObjectGroups.push_back(group);
        auto* bot=g.bots.front().get();bot->SetPosition({1000,800});bot->ResetHealth(100);g.flagBotPlans[bot].destination={1850,800};
        bool safe=true;float longest=0;for(int i=0;i<400;++i){g.PrepareBotTactics(bot);Vector2 before=bot->GetPosition();bot->MoveToObjective(.1f,g.FlagBotWaypoint(bot,.1f));bot->Update(.1f);g.ResolveWorldMovement(bot,before);
            Vector2 now=bot->GetPosition();longest=std::max(longest,std::hypot(now.x-before.x,now.y-before.y));safe&=!CheckCollisionRecs(bot->GetCollisionBounds(),{1400,400,96,900});
            if(std::hypot(now.x-1850,now.y-800)<60)break;
        }
        bool arrived=std::hypot(bot->GetPosition().x-1850,bot->GetPosition().y-800)<60;
        g.allObjectGroups=std::move(groups);g.flagWalkGrid.clear();g.flagGridLinks.clear();g.flagBotPlans.clear();return safe && arrived && longest<30;
    }
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
    static bool RocketWall(GameplayScreen& g,Character* p) {
        auto groups=g.allObjectGroups;g.allObjectGroups.clear();g.bonusProjectiles.clear();g.bonusBlasts.clear();
        Vector2 origin=p->GetPosition();TmxObject wall{};wall.type=OBJECT_TYPE_RECTANGLE;wall.x=origin.x+100;wall.y=origin.y+30;wall.width=20;wall.height=140;
        TmxObjectGroup group{};group.objects=&wall;group.objectsLength=1;g.allObjectGroups.push_back(group);
        g.bonusProjectiles.push_back({{origin.x+40,origin.y+80},{760,0},0,2,0,true,p});g.UpdateBonuses(.1f);
        bool stopped=g.bonusProjectiles.empty() && !g.bonusBlasts.empty() && g.bonusBlasts.back().position.x<=origin.x+108;
        g.allObjectGroups=std::move(groups);g.bonusBlasts.clear();return stopped;
    }
    static void AwaitBonus(GameplayScreen& g,int index){g.awaitingBonusNode=index;}
    static size_t BonusCount(const GameplayScreen& g){return g.bonusNodes.size();}
    static bool NoFfaBase(const GameplayScreen& g){return !g.HasTeamBases() && !g.HasDeathmatchBases();}
    static Vector2 BonusPoint(GameplayScreen& g,int index){return g.bonusNodes[index].position;}
    static float BonusCooldown(GameplayScreen& g,int index){return g.bonusNodes[index].cooldown;}
    static uint32_t BonusSerial(GameplayScreen& g,int index){return g.bonusNodes[index].serial;}
    static void ReadyBonus(GameplayScreen& g,int index){g.bonusNodes[index].cooldown=0;}
    static void Claim(GameplayScreen& g,int index,Character* c,uint32_t id=0){g.ClaimBonus(index,c,id);}
    static void BonusTime(GameplayScreen& g,float dt){g.UpdateBonuses(dt);}
    static void BonusBlast(GameplayScreen& g,int kind,Vector2 p,Character* owner){g.bonusBlasts.push_back({p,0,kind,0,true,owner});}
    static void ClearBonusEffects(GameplayScreen& g){g.bonusProjectiles.clear();g.bonusBlasts.clear();}
    static bool ThrowSpecial(GameplayScreen& g,Character* c,Vector2 target){return g.ThrowBonus(c,target,0);}
    static int SpecialProjectiles(const GameplayScreen& g){return (int)g.bonusProjectiles.size();}
    static void Grace(GameplayScreen& g,float amount){g.spawnGrace=amount;}
    static void BonusPacket(GameplayScreen& g,const void* bytes,size_t size){g.HandleBonusPacket(bytes,size);}
    static bool ClearArena(GameplayScreen& game,Vector2 point) {return game.ArenaClear(point,0);}
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
    check(VoiceTestAccess::ExerciseAudioPath(),"PTT gates capture, encodes/decodes audible PCM, drops duplicates and clears queued speech on release");
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
        auto savedPrefs=Settings::Get();auto savedSetup=Offline::Get();int previousSkin=NetworkManager::GetInstance().localWeaponSkin;
        Settings::Get()=Settings::Preferences{};Offline::Get().mode=Offline::Mode::Training;Offline::Get().bots=3;
        auto working=std::filesystem::current_path();auto fixture=working/"bin/bag-save-test";std::filesystem::create_directories(fixture);
        {
            GameplayScreen game(GameMode::PRACTICE);auto* actor=static_cast<Player*>(game.GetLocalPlayer());
            check(actor->GetWeaponIndex()==0 && actor->GetBag().secondary==2,"bag one equips its primary and pistol sidearm");
            actor->SelectEquipment(1);check(actor->GetWeaponIndex()==2,"secondary slot equips only the assigned sidearm");
            actor->SelectEquipment(2);check(actor->GetMeleeVariant()==0 && actor->GetEquipmentSlot()==2,"slot three equips the generated combat knife");
            auto* enemy=GameplayEffectsTestAccess::Bot(game,0);actor->SetPosition({2600,1600});enemy->SetPosition({2675,1600});enemy->ResetHealth(100);
            for(int i=1;i<3;++i) GameplayEffectsTestAccess::Bot(game,i)->SetPosition({3500+i*200.0f,2200});
            actor->SetAimTarget(enemy->GetPosition());
            check(actor->Shoot(enemy->GetPosition()),"equipped melee can swing");GameplayEffectsTestAccess::Combat(game);
            check(enemy->GetHealth()==45 && actor->GetProjectiles().empty(),"knife deals short-range damage without firing a bullet");
            check(!actor->Shoot(enemy->GetPosition()),"knife swing respects melee cooldown");
            GameplayEffectsTestAccess::Focus(game,actor->GetPosition(),.8f);game.Draw(target);capture("bin/bag-melee-gameplay-preview.png");
            actor->UpdateWithoutInput(.51f);enemy->SetPosition({2525,1600});actor->Shoot({2800,1600});GameplayEffectsTestAccess::Combat(game);
            check(enemy->GetHealth()==45,"melee swing cannot hit behind the attacker");
            actor->UpdateWithoutInput(.51f);enemy->SetPosition({3000,1600});actor->Shoot({3100,1600});GameplayEffectsTestAccess::Combat(game);
            check(enemy->GetHealth()==45,"melee swing cannot hit outside its short range");
            actor->UpdateWithoutInput(.51f);enemy->SetPosition({2675,1600});check(actor->Shoot(enemy->GetPosition()),"knife can swing again after recovery");GameplayEffectsTestAccess::Combat(game);
            check(enemy->IsDead() && GameplayEffectsTestAccess::FeedWeapon(game)==Bags::MeleeDamageId,"melee kill feed records a knife rather than a poison icon");
            actor->SelectEquipment(0);actor->ResetHealth(100);actor->Shoot({3200,1600});int ammo=actor->GetAmmo();
            actor->SetAimTarget({3200,1600});check(game.ThrowGrenade(0),"bag permits its equipped frag grenade");
            float cooldown=game.GetGrenadeCooldown(0);auto home=GameplayEffectsTestAccess::BagHome(game);actor->SetPosition(home);
            std::filesystem::current_path(fixture);
            check(GameplayEffectsTestAccess::CanBag(game) && GameplayEffectsTestAccess::PickBag(game,1),"bag change succeeds at spawn/base");
            check(actor->GetWeaponIndex()==3 && actor->GetBag().secondary==7,"bag two equips its assigned rifle and machine pistol");
            check(game.GetGrenadeCooldown(0)==cooldown,"changing bags does not refill grenade cooldown");
            check(GameplayEffectsTestAccess::PickBag(game,0) && actor->GetAmmo()==ammo,"changing back preserves spent primary ammo");
            actor->SetPosition({home.x+900,home.y+600});
            check(!GameplayEffectsTestAccess::CanBag(game) && !GameplayEffectsTestAccess::PickBag(game,2) && GameplayEffectsTestAccess::ActiveBag(game)==0,"bag changes are blocked away from the base");
            actor->TakeDamage(10000);check(GameplayEffectsTestAccess::PickBag(game,2) && actor->IsDead(),"dead player can select the next bag without being healed");
            std::filesystem::current_path(working);
            GameplayEffectsTestAccess::Respawn(game);check(actor->GetWeaponIndex()==6 && actor->GetMeleeVariant()<0,"respawn applies selected bag three primary");
            GameplayEffectsTestAccess::BagMenu(game);game.Draw(target);capture("bin/in-game-bags-preview.png");
        }
        {
            GameplayScreen game(GameMode::PRACTICE);auto* actor=static_cast<Player*>(game.GetLocalPlayer());
            actor->SetPosition({2600,1600});actor->SetAimTarget({2750,1600});
            auto* closeBot=GameplayEffectsTestAccess::Bot(game,0);closeBot->ResetHealth(100);closeBot->SetPosition({2750,1600});
            auto* distantBot=GameplayEffectsTestAccess::Bot(game,1);distantBot->SetPosition({3600,1600});
            check(game.ThrowGrenade(2) && game.GetGrenadeCooldown(2)==15,"equipped flashbang throws and starts its cooldown");
            GameplayEffectsTestAccess::Step(game,.81f);
            check(actor->GetFlashBlind()>0 && closeBot->GetFlashBlind()>0 && distantBot->GetFlashBlind()==0,"flashbang blinds nearby thrower and bot within its range");
            check(actor->GetHealth()==100 && closeBot->GetHealth()==100 && !closeBot->Shoot(actor->GetPosition()),"flashbang deals no damage and disrupts blinded bots");
            game.Draw(target);capture("bin/flashbang-blind-preview.png");
            actor->UpdateWithoutInput(3);check(actor->GetFlashBlind()==0,"flashbang vision recovers over time");
            GameplayEffectsTestAccess::Reset(game);GameplayEffectsTestAccess::RemoteFlash(game,actor->GetPosition());
            GameplayEffectsTestAccess::Step(game,.81f);check(actor->GetFlashBlind()>0 && actor->GetHealth()==100,"replicated flashbang blinds local receiver without damage");
        }
        std::filesystem::current_path(fixture);
        auto& prefs=Settings::Get();prefs.activeBag=2;prefs.bags[2].primary=5;prefs.bags[2].secondary=7;
        prefs.bags[2].primarySkin=2;prefs.bags[2].secondarySkin=1;prefs.bags[2].melee=1;prefs.bags[2].meleeSkin=3;
        prefs.bags[2].grenade=false;prefs.bags[2].smoke=false;prefs.bags[2].flash=true;
        check(Settings::Save(),"save configured bags and equipment");prefs=Settings::Preferences{};Settings::Load();
        check(prefs.activeBag==2 && prefs.bags[2].primary==5 && prefs.bags[2].secondary==7 && prefs.bags[2].primarySkin==2 && prefs.bags[2].secondarySkin==1 && prefs.bags[2].melee==1 && prefs.bags[2].meleeSkin==3 && !prefs.bags[2].smoke && !prefs.bags[2].grenade && prefs.bags[2].flash,"all bag slots, per-bag skins and utility choices survive save/load");
        LoadoutStorage storage;storage.Open();storage.SelectSlot(0);storage.EquipGun(2);
        check(prefs.bags[2].primary==5,"storage refuses a pistol in the primary slot");
        storage.EquipGun(3,2);check(prefs.bags[2].primary==3 && prefs.bags[2].primarySkin==2 && prefs.bags[0].primary==0,"storage equips one bag without changing other bags");
        std::filesystem::current_path(working);
        for(int bag=0;bag<3;++bag) {
            prefs.activeBag=bag;storage.Open();storage.SelectSlot(0);
            BeginTextureMode(target);storage.Draw(GetFontDefault());EndTextureMode();capture(TextFormat("bin/storage-bag%d-preview.png",bag+1));
        }
        storage.SelectSlot(2);BeginTextureMode(target);storage.Draw(GetFontDefault());EndTextureMode();capture("bin/storage-melee-preview.png");
        storage.SelectSlot(5);BeginTextureMode(target);storage.Draw(GetFontDefault());EndTextureMode();capture("bin/storage-flashbang-preview.png");
        Settings::Get()=savedPrefs;Offline::Get()=savedSetup;NetworkManager::GetInstance().localWeaponSkin=previousSkin;
    }
    {
        auto& registry=MapRegistry::GetInstance();
        auto names=registry.GetMapNames();
        check(names.size()==3 && std::find(names.begin(),names.end(),"Forest")==names.end() && std::find(names.begin(),names.end(),"Forest2")==names.end(),
              "new arenas replace both forests in the shared online/offline map selector");
        auto& network=NetworkManager::GetInstance();auto selected=network.selectedMapName;
        auto setup=Offline::Get();
        for(const char* name:{"AbandonedFoundry","DesertOutpost"}) {
            auto* map=registry.GetMap(name);
            check(map && map->tmxMap && map->pixelWidth==4800 && map->pixelHeight==3200 && map->scene.spawnPoints.size()==16,
                  "new arena loads editable TMX art and sixteen authored spawns");
            network.selectedMapName=name;Offline::Get().mode=Offline::Mode::Deathmatch;
            GameplayScreen game(GameMode::PRACTICE);auto* actor=game.GetLocalPlayer();
            for(auto spawn:map->scene.spawnPoints) {
                actor->SetPosition(spawn.position);GameplayEffectsTestAccess::Resolve(game,actor);
                auto p=actor->GetPosition();check(std::hypot(p.x-spawn.position.x,p.y-spawn.position.y)<.1f,"new arena spawn clears all cover and wall collision");
            }
            for(int team=1;team<=2;++team) {
                auto p=GameplayEffectsTestAccess::Base(game,team);
                check(std::abs(p.x-map->pixelWidth*(team==1?.16f:.84f))<1,"flag base remains in its open delivery lane");
            }
            int probes=0;
            for(uint32_t i=0;i<map->tmxMap->layersLength;++i) {
                const auto& layer=map->tmxMap->layers[i];
                if(layer.type!=LAYER_TYPE_OBJECT_GROUP || !layer.name || std::string(layer.name)!="collision") continue;
                const auto& objects=layer.exact.objectGroup;
                for(uint32_t j=0;j<objects.objectsLength;++j) {
                    const auto& o=objects.objects[j];
                    if(o.x<320 || o.x+o.width>4400) continue;
                    Rectangle obstacle{(float)o.x,(float)o.y,(float)o.width,(float)o.height};
                    for(Vector2 direction:{Vector2{1,0},Vector2{-1,0},Vector2{0,1},Vector2{0,-1}}) for(float jump:{0.0f,160.0f}) {
                        Vector2 feetCenter{obstacle.x+obstacle.width*.5f,obstacle.y+obstacle.height*.5f};
                        if(direction.x) feetCenter.x=direction.x>0?obstacle.x-40:obstacle.x+obstacle.width+40;
                        else feetCenter.y=direction.y>0?obstacle.y-40:obstacle.y+obstacle.height+40;
                        Vector2 start{feetCenter.x,feetCenter.y-actor->GetFeetOffset()+3};
                        actor->SetPosition(start);actor->SetJumpHeight(0);float depth=actor->GetDepthY();
                        actor->SetJumpHeight(jump);check(actor->GetDepthY()==depth,"jumping keeps character depth at the ground projection");
                        actor->SetPosition({start.x+direction.x*240,start.y+direction.y*240});GameplayEffectsTestAccess::Move(game,actor,start);
                        auto feet=actor->GetCollisionBounds();
                        check(!CheckCollisionRecs(feet,obstacle),"walking and jumping from every side cannot enter prop/wall collision");
                        if(direction.x>0) check(feet.x+feet.width<=obstacle.x+.1f,"left contact cannot tunnel across cover");
                        if(direction.x<0) check(feet.x>=obstacle.x+obstacle.width-.1f,"right contact cannot tunnel across cover");
                        if(direction.y>0) check(feet.y+feet.height<=obstacle.y+.1f,"north contact cannot tunnel across cover");
                        if(direction.y<0) check(feet.y>=obstacle.y+obstacle.height-.1f,"south contact cannot tunnel across cover");
                    }
                    ++probes;
                }
            }
            check(probes>=20,"new arena collision check covers walls and all props");actor->SetJumpHeight(0);
            Camera2D camera{{650,340},{2400,1600},0,.20f};Rectangle view{0,0,4800,3200};
            BeginTextureMode(target);ClearBackground({10,16,22,255});BeginMode2D(camera);
            DrawMapWithMaterials(map->tmxMap,&camera,&view);DrawMapEffects(map->tmxMap,2,view);EndMode2D();EndTextureMode();
            capture(TextFormat("bin/%s-map-preview.png",name));
            for(uint32_t i=0;i<map->tmxMap->layersLength;++i) {
                const auto& layer=map->tmxMap->layers[i];
                if(layer.type!=LAYER_TYPE_OBJECT_GROUP || !layer.classString || std::string(layer.classString)!="decor") continue;
                for(uint32_t j=0;j<layer.exact.objectGroup.objectsLength;++j) {
                    const auto& prop=layer.exact.objectGroup.objects[j];
                    check(prop.propertiesLength>0,"every new arena prop authors its true ground edge");
                    float ground=MapObjectGroundDepth(prop);
                    bool aligned=false;
                    for(uint32_t k=0;k<map->tmxMap->layersLength;++k) {
                        const auto& contacts=map->tmxMap->layers[k];
                        if(contacts.type!=LAYER_TYPE_OBJECT_GROUP || !contacts.name || std::string(contacts.name)!="collision") continue;
                        for(uint32_t q=0;q<contacts.exact.objectGroup.objectsLength;++q) {
                            const auto& foot=contacts.exact.objectGroup.objects[q];
                            aligned|=foot.name && std::string(foot.name)=="Cover foot" && std::abs(foot.y+foot.height-ground)<.01;
                        }
                    }
                    check(aligned,"prop depth matches collision ground edge rather than transparent texture padding");
                    if(std::string(name)=="AbandonedFoundry" && prop.name && std::string(prop.name)=="Cover 2" && j==8) {
                        float center=1870;
                        for(int side:{-1,1}) {
                            actor->SetPosition({center,ground+(side>0?8:-110)-actor->GetFeetOffset()});
                            actor->SetJumpHeight(0);GameplayEffectsTestAccess::Resolve(game,actor);
                            check(side>0?actor->GetDepthY()>ground:actor->GetDepthY()<ground,"rack front/back contact sorts on the correct side");
                            GameplayEffectsTestAccess::Focus(game,{center,ground-65},1.2f);
                            game.Draw(target);capture(side>0?"bin/pipe-rack-front-contact.png":"bin/pipe-rack-back-contact.png");
                        }
                    }
                }
            }
            actor->SetPosition({2400,1472});GameplayEffectsTestAccess::Focus(game,{2400,1150},.60f);GameplayEffectsTestAccess::Time(game,2);
            game.Draw(target);capture(TextFormat("bin/%s-gameplay-preview.png",name));
        }
        check(registry.GetMap("Forest")==registry.GetMap("AbandonedFoundry") && registry.GetMap("Forest2")==registry.GetMap("DesertOutpost"),"old map references migrate to their replacement arenas");
        network.selectedMapName=selected;Offline::Get()=setup;
    }
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
        check(GameplayEffectsTestAccess::FeedWeapon(arena)==WeaponCount-1,"switching guns never changes a recorded kill icon");
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
                auto texture=LoadWeaponSlotTexture(weapon,skin);auto geometry=GetWeaponGeometry(texture,weapon);
                float weaponScale=.15f*.45f*geometry.scale;auto hold=GetWeaponHold(texture,weapon,pose,weaponScale,actor.GetFaceDirection());
                check(std::hypot(hold.trigger.x-pose.hand.x,hold.trigger.y-pose.hand.y)<.001f,"trigger hand stays attached to the gun pivot for every facing and angle");
                if(weapon!=2){Vector2 delta{hold.support.x-hold.trigger.x,hold.support.y-hold.trigger.y};
                    check(std::abs(delta.x*pose.direction.x+delta.y*pose.direction.y-(geometry.support.x-geometry.pivot.x)*weaponScale)<.01f,"support hand follows the foregrip along the actual weapon axis");}
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
            if(slot>=5) {
                Image art=LoadImageFromTexture(actor.Texture(slot));
                check(GetImageColor(art,0,0).a==0,"new gun sprite preserves transparent background");
                UnloadImage(art);
            }
        }
        BeginTextureMode(target); ClearBackground({10,18,27,255});
        for(int slot=0;slot<WeaponCount;++slot) for(int skin=0;skin<WeaponSkinCount;++skin) {
            Rectangle card{slot*(1300.0f/WeaponCount),skin*158.0f+35,1300.0f/WeaponCount,153};
            DrawText(WeaponShortName(slot),(int)card.x+12,(int)card.y,16,UiTheme::AccentGold());
            Texture2D texture=LoadWeaponSlotTexture(slot,skin); auto geometry=GetWeaponGeometry(texture,slot);
            check(texture.id && geometry.bounds.width>10 && geometry.bounds.height>10,"all gun skins have visible sprites");
            float fit=std::min((card.width-24)/geometry.bounds.width,85/geometry.bounds.height);
            Rectangle destination{card.x+card.width/2,card.y+66,geometry.bounds.width*fit,geometry.bounds.height*fit};
            DrawWeaponSkinTexture(texture,geometry.bounds,destination,{destination.width/2,destination.height/2},0,slot,skin);
            DrawText(WeaponSkinLabel(slot,skin),(int)card.x+12,(int)card.y+118,10,LIGHTGRAY);
        }
        EndTextureMode(); capture("bin/arsenal-skins-preview.png");
        Image rendered=LoadImageFromTexture(target.texture); ImageFlipVertical(&rendered);
        for(int slot=0;slot<WeaponCount;++slot) for(int skin=0;skin<WeaponSkinCount;++skin) {
            int visible=0;
            for(int y=skin*158+63;y<skin*158+145;++y) for(int x=(int)(slot*(1300.0f/WeaponCount))+10;x<(int)((slot+1)*(1300.0f/WeaponCount))-10;++x) {
                Color color=GetImageColor(rendered,x,y);
                if(color.r>35 || color.g>40 || color.b>50) ++visible;
            }
            check(visible>60,"every skin actually draws a visible weapon in the gallery");
        }
        UnloadImage(rendered);
        for(int weapon : {3,4,5,6,7}) {
            actor.ResetHealth(100); actor.SwitchWeapon(weapon);
            check(actor.GetAmmo()==Weapons[weapon].maxAmmo,"new gun starts with its own magazine size");
            auto count=actor.GetProjectiles().size();
            check(actor.Shoot({1000,150}),"new weapon fires");
            check(actor.GetProjectiles().size()-count==(weapon==6?7:1),"double barrel fires seven pellets while other guns fire one bullet");
            check(actor.GetProjectiles().back()->GetWeaponIndex()==weapon,"projectile remembers the fired gun for kill attribution");
            check(actor.GetAmmo()==Weapons[weapon].maxAmmo-1 && actor.GetProjectiles().back()->GetDamage()==Weapons[weapon].damage,"new gun consumes ammo and uses configured projectile damage");
            check(!actor.Shoot({1000,150}),"new gun respects fire cooldown");
            actor.TriggerReload(); actor.UpdateWithoutInput(Weapons[weapon].reloadTime+0.1f);
            check(!actor.IsReloading() && actor.GetAmmo()==Weapons[weapon].maxAmmo,"new gun finishes its own reload duration");
            RemotePlayer remote({900,120},CharacterPreview::BodyPath(1),preview.GetPackedSkins());
            remote.ApplyNetworkUpdate({900,120},0,weapon,-1,100,0,0,preview.GetPackedSkins());
            check(remote.GetWeaponIndex()==weapon,"online player can equip new weapon slots");
            remote.ShootInDirection({-1,0});
            check(remote.GetProjectiles().size()==(weapon==6?7:1),"online visuals preserve new gun projectile counts");
        }
        for(int weapon=0;weapon<WeaponCount;++weapon) {
            actor.ResetHealth(100); actor.SwitchWeapon(weapon); actor.SetEquipment();
            const int capacity=Weapons[weapon].maxAmmo, reserve=Weapons[weapon].reserveAmmo;
            check(actor.GetReserveAmmo()==reserve,"each gun starts with its configured finite reserve");
            actor.SetShootCooldown(0); actor.Shoot({1000,150});
            actor.TriggerReload(); actor.UpdateSkills(Weapons[weapon].reloadTime*.5f);
            actor.SwitchWeapon((weapon+1)%WeaponCount); actor.SwitchWeapon(weapon);
            check(!actor.IsReloading() && actor.GetAmmo()==capacity-1 && actor.GetReserveAmmo()==reserve,
                  "switching weapons cancels reload without creating or consuming rounds");
            actor.SetEquipment(-1,2); actor.TriggerReload(); actor.UpdateSkills(Weapons[weapon].reloadTime+.01f);
            check(actor.GetAmmo()==capacity && actor.GetReserveAmmo()==reserve-1,
                  "partial reload transfers only missing rounds from that gun's reserve");
            int shots=1;
            while(actor.GetAmmo()+actor.GetReserveAmmo()>0) {
                if(actor.GetAmmo()==0) {
                    int before=actor.GetReserveAmmo(); actor.TriggerReload();
                    actor.UpdateSkills(Weapons[weapon].reloadTime+.01f);
                    check(actor.GetAmmo()==std::min(capacity,before),"last reload uses only available reserve rounds");
                }
                actor.SetShootCooldown(0);
                check(actor.Shoot({1000,150}),"remaining ammunition can be fired");
                actor.GetProjectiles().clear(); ++shots;
            }
            actor.TriggerReload(); actor.UpdateSkills(10); actor.SetShootCooldown(0);
            check(shots==capacity+reserve && !actor.IsReloading() && !actor.Shoot({1000,150}),
                  "every gun exhausts its entire finite ammo supply and cannot reload or fire for free");
            actor.SetEquipment(-1,3); actor.SwitchWeapon((weapon+1)%WeaponCount); actor.SwitchWeapon(weapon);
            check(actor.GetAmmo()==0 && actor.GetReserveAmmo()==0,"equipment and gun changes preserve exhausted ammunition");
            actor.ResetHealth(100);
            check(actor.GetAmmo()==capacity && actor.GetReserveAmmo()==reserve,"respawn restores magazine and reserve");
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
        BeginTextureMode(target); ClearBackground({10,18,27,255});
        for(int column=0;column<3;++column) for(int skin=0;skin<WeaponSkinCount;++skin) {
            int slot=column+5;float x=column*430.0f,y=skin*162.0f;
            DrawText(WeaponName(slot),(int)x+20,(int)y+8,18,UiTheme::AccentGold());
            auto texture=LoadWeaponSlotTexture(slot,skin);auto bounds=GetWeaponGeometry(texture,slot).bounds;
            float fit=std::min(380/bounds.width,95/bounds.height);
            DrawWeaponSkinTexture(texture,bounds,{x+215,y+85,bounds.width*fit,bounds.height*fit},{bounds.width*fit/2,bounds.height*fit/2},0,slot,skin);
            DrawText(WeaponSkinLabel(slot,skin),(int)x+20,(int)y+140,14,LIGHTGRAY);
        }
        EndTextureMode();capture("bin/three-new-guns-skins-preview.png");
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
        nm.UpdateLobbySettings(OnlineGameMode::TEAM_DEATHMATCH,300,0,8);
        LobbyScreen lobby;
        check(LobbyTestAccess::Height(lobby)>=240,"online lobby uses the enlarged map preview");
        LobbyTestAccess::Scroll(lobby,10000);
        check(LobbyTestAccess::Offset(lobby)==LobbyTestAccess::Maximum(lobby) && !LobbyTestAccess::HiddenChip(lobby),"online settings scroll clamps and clips hidden controls");
        lobby.Draw(target); capture("bin/lobby-scrolled-preview.png");
        LobbyTestAccess::Scroll(lobby,-10000);
        check(LobbyTestAccess::Offset(lobby)==0,"online settings return to the top");
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
            GameplayEffectsTestAccess::Flow(game,30);check(!game.ShouldReturnToLobby(),"match report stays open until the player chooses the next action");
            GameplayEffectsTestAccess::LeaveResults(game);check(game.ShouldReturnToLobby(),"results button returns offline match to setup");
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
            GameplayScreen game(GameMode::PRACTICE);
            auto* bot=GameplayEffectsTestAccess::Bot(game,0);
            bot->TakeDamage(10000); bot->ResetDeathTimer();
            GameplayEffectsTestAccess::Practice(game,7.9f);
            check(bot->IsDead(),"offline flag bot cannot respawn before eight seconds");
            GameplayEffectsTestAccess::Practice(game,.11f);
            check(!bot->IsDead(),"offline flag bot respawns after eight seconds");
        }
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
            bot->TakeDamage(10000); GameplayEffectsTestAccess::Practice(game,8.1f);
            check(!bot->IsDead() && std::hypot(bot->GetPosition().x-botBase.x,bot->GetPosition().y-botBase.y)<111,"flag bots respawn at their own visible base");
            game.GetLocalPlayer()->SetPosition(botBase); GameplayEffectsTestAccess::Respawn(game);
            check(std::hypot(game.GetLocalPlayer()->GetPosition().x-target.x,game.GetLocalPlayer()->GetPosition().y-target.y)<111,"player respawn belongs to own team base");
        }
        Offline::Get().bots=8;
        {
            GameplayScreen game(GameMode::PRACTICE);auto* p=game.GetLocalPlayer();Vector2 home=GameplayEffectsTestAccess::Base(game,1);
            p->SetPosition({home.x,home.y-450});GameplayEffectsTestAccess::FlagPlan(game);
            check(GameplayEffectsTestAccess::FlagRoles(game,1,0)==1 && GameplayEffectsTestAccess::FlagRoles(game,1,2)==1 && GameplayEffectsTestAccess::FlagRoles(game,1,4)==2,"flag squads split into one runner, one defender and separate flankers");
            check(GameplayEffectsTestAccess::SpreadPlans(game,1),"flag squad objectives are separated instead of sharing a single target");
            GameplayEffectsTestAccess::AssignCarrier(game,1,p);auto* hostile=GameplayEffectsTestAccess::Bot(game,0);hostile->SetPosition({home.x+350,home.y});GameplayEffectsTestAccess::AssignCarrier(game,2,hostile);
            GameplayEffectsTestAccess::FlagPlan(game);
            check(GameplayEffectsTestAccess::FlagRoles(game,1,3)==1 && GameplayEffectsTestAccess::FlagRoles(game,1,2)==1 && GameplayEffectsTestAccess::FlagRoles(game,1,1)==2,"player carrier gets spread escorts while one teammate intercepts danger and another guards home");
            GameplayEffectsTestAccess::ResetFlag(game,1);GameplayEffectsTestAccess::ResetFlag(game,2);GameplayEffectsTestAccess::FlagPlan(game);
            BotEnemy* oldRunner=nullptr;for(int i=0;i<8;++i){auto* b=GameplayEffectsTestAccess::Bot(game,i);if(b->GetTeamID()==1 && !oldRunner){oldRunner=b;}}
            // Removing any teammate must leave exactly one living runner, never a whole squad chase.
            oldRunner->TakeDamage(10000);GameplayEffectsTestAccess::FlagPlan(game);
            check(GameplayEffectsTestAccess::FlagRoles(game,1,0)==1,"a dead teammate does not break the one-runner assignment");
            check(GameplayEffectsTestAccess::FlagDetour(game),"flag runner walks around a blocking wall without clipping or teleporting");
        }
        {
            bool audio=Character::IsCombatAudioEnabled();Character::SetCombatAudioEnabled(false);
            auto savedMap=Offline::Get().map;auto savedNetMap=NetworkManager::GetInstance().selectedMapName;
            for(const char* map:{"MagmaRift","DesertOutpost","AbandonedFoundry"}){
                Offline::Get().map=map;NetworkManager::GetInstance().selectedMapName=map;
                GameplayScreen game(GameMode::PRACTICE);game.GetLocalPlayer()->SetGodMode(true);game.GetLocalPlayer()->SetPosition({game.GetWorldWidth()/2,220});
                for(int i=0;i<8;++i)GameplayEffectsTestAccess::Bot(game,i)->SetGodMode(true);
                for(int frame=0;frame<900 && GameplayEffectsTestAccess::Score(game,1)+GameplayEffectsTestAccess::Score(game,2)==0;++frame){GameplayEffectsTestAccess::Practice(game,.1f);GameplayEffectsTestAccess::Flags(game,.1f);}
                check(GameplayEffectsTestAccess::Score(game,1)+GameplayEffectsTestAccess::Score(game,2)>0,(std::string("flag bots complete a real delivery using map routes on ")+map).c_str());
            }
            Offline::Get().map=savedMap;NetworkManager::GetInstance().selectedMapName=savedNetMap;Character::SetCombatAudioEnabled(audio);
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
            Vector2 before=player->GetPosition();GameplayEffectsTestAccess::Respawn(game);
            Vector2 spawn=player->GetPosition();
            check(GameplayEffectsTestAccess::ClearArena(game,spawn) && std::hypot(spawn.x-before.x,spawn.y-before.y)>100,"FFA respawns at a different clear random location");
            auto* bot=GameplayEffectsTestAccess::Bot(game,0);before=bot->GetPosition();bot->TakeDamage(10000);GameplayEffectsTestAccess::Practice(game,4.1f);
            spawn=bot->GetPosition();check(!bot->IsDead() && GameplayEffectsTestAccess::ClearArena(game,spawn),"FFA bot respawns safely away from solid cover");
            GameplayEffectsTestAccess::Focus(game,player->GetPosition());game.Draw(target);capture("bin/ffa-random-spawn-preview.png");
        }
        Offline::Get()=saved;
    }
    {
        BeginTextureMode(target);ClearBackground({24,32,40,255});
        DrawText("SPECIAL PICKUPS / REDESIGNED HARDWARE",35,24,24,UiTheme::AccentGold());
        for(int kind=0;kind<6;++kind){float x=35+(kind%3)*425,y=85+(kind/3)*160;
            DrawBonusArt(kind,{x,y,370,118});DrawText(Bonus::Name(kind),(int)x+30,(int)y+120,16,Bonus::Tint(kind));}
        DrawText("DIRECTIONAL FLAMES / FROST BURST / BURNING GROUND",35,428,18,LIGHTGRAY);
        DrawBonusFlame({60,550},{1,-.1f},290,35,.35f);
        DrawBonusBurst(3,{600,565},.45f,true);DrawBonusIce({600,540},1.9f);
        DrawBonusBurst(5,{1060,570},1.3f,true);
        EndTextureMode();capture("bin/special-pickups-effects-preview.png");
    }
    {
        Medals::Tracker tracker;
        check(tracker.Kill(1,2,1,0).empty(),"first kill has no multikill medal");
        auto medals=tracker.Kill(1,3,1,3.9f);check(medals.size()==1 && medals[0]==Medals::DoubleKill,"second kill within four seconds earns double kill");
        medals=tracker.Kill(1,4,1,4.1f);check(medals.size()==1 && medals[0]==Medals::TripleKill,"third chained kill earns triple kill");
        medals=tracker.Kill(2,1,2,5);check(medals.size()==2 && medals[0]==Medals::Revenge && medals[1]==Medals::Shutdown,"revenge and shutdown use the victim's actual last killer and living streak");
        check(tracker.Kill(2,5,2,10).empty(),"expired multikill windows do not award medals");
        tracker.Reset();tracker.Kill(2,1,1,0);tracker.Kill(1,1,1,1);
        medals=tracker.Kill(1,2,1,2);check(medals.size()==1 && medals[0]==Medals::Revenge,"revenge survives a respawn or environmental death");
        tracker.Reset();check(tracker.Kill(1,1,1,0).empty() && tracker.Kill(1,2,1,1,false).empty(),"suicides and team kills cannot earn medals");
        BotEnemy bot({500,500},CharacterPreview::BodyPath(1));bot.SetDifficulty(2);bot.SwitchWeapon(0);
        while(bot.GetAmmo()+bot.GetReserveAmmo()>0){if(bot.GetAmmo()==0){bot.TriggerReload();bot.UpdateSkills(10);}bot.SetShootCooldown(0);bot.Shoot({1400,500});bot.GetProjectiles().clear();}
        bot.ManageAmmo();check(bot.GetWeaponIndex()==2 && bot.GetAmmo()==Weapons[2].maxAmmo,"bots switch to pistol only when primary magazine and reserve are exhausted");
        bot.ResetHealth(100);bot.ManageAmmo();check(bot.GetWeaponIndex()==0,"respawn restores the bot's original primary weapon");
        auto clear=[](Vector2 p){return p.x>100 && p.x<2000 && p.y>100 && p.y<2000;};
        bot.SetPosition({500,500});bot.SetTacticalContext({{{520,500},230}},clear,[](Vector2,Vector2){return true;});
        bot.UpdateAI(.1f,{950,500});
        check(bot.IsEvading() && bot.GetPosition().x<500 && bot.GetProjectiles().empty(),"bots move away from imminent grenade danger and stop firing while escaping");
        bot.ResetHealth(100);bot.SetPosition({500,500});bot.SetShootCooldown(0);bot.Shoot({950,500});bot.TriggerReload();
        bot.SetTacticalContext({},clear,[](Vector2,Vector2 end){return end.y>470;});
        bot.UpdateAI(.1f,{950,500});check(bot.IsUsingCover() && bot.GetPosition().y<500,"reloading bots choose reachable cover that blocks enemy sight");
        bot.ApplySlow(2,0);Vector2 frozen=bot.GetPosition();bot.UpdateAI(.2f,{950,500});
        check(bot.GetPosition().x==frozen.x && bot.GetPosition().y==frozen.y,"tactical AI respects full freeze immobilization");
        auto saved=Offline::Get();Offline::Get().mode=Offline::Mode::FreeForAll;Offline::Get().bots=3;
        {GameplayScreen game(GameMode::PRACTICE);
            GameplayEffectsTestAccess::MedalKill(game,1,2,1);GameplayEffectsTestAccess::MedalKill(game,1,3,1);
            GameplayEffectsTestAccess::MedalKill(game,1,4,1);
            check(GameplayEffectsTestAccess::Medals(game)==1 && GameplayEffectsTestAccess::MedalKind(game)==Medals::TripleKill,"triple kill upgrades an active double kill without duplicate toast");
            GameplayEffectsTestAccess::MedalTick(game,.4f);BeginTextureMode(target);ClearBackground({12,24,30,255});GameplayEffectsTestAccess::DrawMedal(game);EndTextureMode();capture("bin/combat-medal-preview.png");
            GameplayEffectsTestAccess::MedalTick(game,2.5f);check(GameplayEffectsTestAccess::Medals(game)==0,"medal presentation expires without covering the HUD permanently");
            auto* champion=GameplayEffectsTestAccess::Bot(game,0);champion->AddKill();champion->AddKill();
            game.GetLocalPlayer()->ConfirmDamage(20000);
            check(GameplayEffectsTestAccess::Winners(game).front()==champion,"FFA podium follows winning kills rather than MVP damage rating");
            GameplayEffectsTestAccess::Finish(game);GameplayEffectsTestAccess::ResultAge(game,5);game.Draw(target);capture("bin/victory-ffa-preview.png");
            GameplayEffectsTestAccess::RestartMatch(game);check(GameplayEffectsTestAccess::Medals(game)==0,"rematch clears medal presentation");
            check(GameplayEffectsTestAccess::BotHazards(game),"actual grenade and incendiary zones reach offline bot tactical decisions");
            GameplayEffectsTestAccess::Kill(game,game.GetLocalPlayer(),GameplayEffectsTestAccess::Bot(game,0));
            GameplayEffectsTestAccess::Kill(game,game.GetLocalPlayer(),GameplayEffectsTestAccess::Bot(game,1));
            check(GameplayEffectsTestAccess::Medals(game)==1 && GameplayEffectsTestAccess::MedalKind(game)==Medals::DoubleKill,"actual offline kill events award local combat medals");
        }Offline::Get()=saved;
    }
    {
        auto left=CombatAudio::Mix({-300,0},{0,0},1000),right=CombatAudio::Mix({300,0},{0,0},1000);
        check(left.pan<0 && right.pan>0 && std::abs(left.volume-right.volume)<.0001f,"combat sounds pan left/right at equal distance");
        check(CombatAudio::Mix({50,0},{0,0},1000).volume>right.volume && CombatAudio::Mix({1100,0},{0,0},1000).volume==0,"distant combat sounds attenuate and stop outside hearing range");
        Player actor({300,300});actor.SwitchWeapon(3);Vector2 hand=actor.GetHandPosition();
        actor.Shoot({hand.x+1000,hand.y});float recoil=actor.GetRecoil();actor.SetShootCooldown(0);actor.Shoot({hand.x+1000,hand.y});
        check(actor.GetRecoil()>recoil && actor.GetCasingCount()==2,"successive rifle shots build recoil and eject individual casings");
        check(actor.GetLastShotDirection().y<0,"recoil changes the next actual bullet direction");
        uint32_t shot=actor.GetProjectiles().back()->GetShotID();actor.ConfirmDamage(20,shot);actor.ConfirmDamage(10,shot);
        check(actor.GetCombatStats().shots==2 && actor.GetCombatStats().hits==1 && actor.GetCombatStats().damage==30 && actor.GetCombatStats().Accuracy()==50,"accuracy counts a shot once even when pellets hit multiple times");
        actor.UpdateSkills(2);check(actor.GetRecoil()==0 && actor.GetCasingCount()==0,"recoil recovers and casing particles expire");
        actor.ResetHealth(100);check(actor.GetCombatStats().damage==30,"respawning preserves match statistics");actor.ResetMatchStats();check(actor.GetCombatStats().shots==0 && actor.GetCombatStats().damage==0,"rematch resets combat statistics");
        auto saved=Offline::Get();Offline::Get().mode=Offline::Mode::Elimination;Offline::Get().bots=3;Offline::Get().playerTeam=1;
        {
            GameplayScreen game(GameMode::PRACTICE);auto* p=game.GetLocalPlayer();auto* enemy=GameplayEffectsTestAccess::Bot(game,0);auto* ally=GameplayEffectsTestAccess::Bot(game,1);
            check(GameplayEffectsTestAccess::BulletWall(game),"fast bullets sweep through thin wall geometry and create impact particles");
            p->TakeDamage(100);GameplayEffectsTestAccess::NoteDamage(game,enemy,p,70,3);GameplayEffectsTestAccess::NoteDamage(game,enemy,p,30,3);
            GameplayEffectsTestAccess::Kill(game,enemy,p);GameplayEffectsTestAccess::Spectate(game);
            check(GameplayEffectsTestAccess::Watched(game)==ally && GameplayEffectsTestAccess::Choices(game)==1,"dead team players can spectate only surviving teammates");
            check(GameplayEffectsTestAccess::RecapEntries(game)==1 && GameplayEffectsTestAccess::RecapDamage(game)==100,"death recap aggregates damage and hits by attacker and weapon");
            GameplayEffectsTestAccess::Focus(game,ally->GetPosition());game.Draw(target);capture("bin/death-recap-spectating-preview.png");
            GameplayEffectsTestAccess::Capture(game,ally);ally->ConfirmDamage(450);GameplayEffectsTestAccess::Finish(game);
            check(GameplayEffectsTestAccess::Mvp(game)==ally,"MVP rewards objective captures and damage as well as kills");
            check(!MatchPresentation::Ready(0) && !MatchPresentation::Ready(7.4f) && MatchPresentation::Ready(7.8f),"results controls stay locked during the end-of-match presentation");
            GameplayEffectsTestAccess::ResultAge(game,.35f);game.Draw(target);capture("bin/match-end-settle-preview.png");
            GameplayEffectsTestAccess::ResultAge(game,1.8f);game.Draw(target);capture("bin/match-report-loading-preview.png");
            GameplayEffectsTestAccess::Winner(game,1);
            auto winners=GameplayEffectsTestAccess::Winners(game);
            check(winners.size()==2 && winners.front()==ally && winners.back()==p,"victory lineup includes only winning teammates, including fallen players");
            Vector2 beforePose=p->GetPosition();int beforeAmmo=p->GetAmmo();
            GameplayEffectsTestAccess::ResultAge(game,5);game.Draw(target);capture("bin/victory-squad-preview.png");
            check(p->IsDead() && p->GetAmmo()==beforeAmmo && p->GetPosition().x==beforePose.x && p->GetPosition().y==beforePose.y,"victory poses preserve actual life, ammo and world position");
            GameplayEffectsTestAccess::ResultAge(game,8.2f);game.Draw(target);capture("bin/match-results-preview.png");GameplayEffectsTestAccess::RestartMatch(game);
            check(!GameplayEffectsTestAccess::MatchOver(game) && !p->IsDead() && p->GetDeaths()==0 && ally->GetCombatStats().captures==0 && GameplayEffectsTestAccess::RecapEntries(game)==0,"offline rematch restores fighters, clears results and resets match statistics");
        }
        Offline::Get().mode=Offline::Mode::FreeForAll;
        {GameplayScreen game(GameMode::PRACTICE);auto* p=game.GetLocalPlayer();p->TakeDamage(100);GameplayEffectsTestAccess::Spectate(game);check(GameplayEffectsTestAccess::Choices(game)==3,"FFA spectators can watch every living opponent");}
        auto& nm=NetworkManager::GetInstance();check(nm.HostRoom(17779),"host online combat report test");nm.currentGameMode=OnlineGameMode::TEAM_DEATHMATCH;nm.SetPlayerTeam(nm.GetLocalPlayerID(),1);nm.UpsertPlayer(9,"Opponent",1,0,2);
        {
            GameplayScreen game(GameMode::ONLINE);auto* p=game.GetLocalPlayer();p->SetShootCooldown(0);Vector2 hand=p->GetHandPosition();p->Shoot({hand.x+1000,hand.y});
            PacketDamageConfirm confirm{};confirm.header={PacketType::DAMAGE_CONFIRM,9};confirm.recipient=nm.GetLocalPlayerID();confirm.serial=1;confirm.shotID=1;confirm.damage=24;
            check(GameplayEffectsTestAccess::Receipt(game,confirm) && !GameplayEffectsTestAccess::Receipt(game,confirm) && p->GetCombatStats().damage==24,"online damage confirmations credit actual damage exactly once");
            confirm.serial=2;confirm.shotID=500;check(!GameplayEffectsTestAccess::Receipt(game,confirm),"online accuracy rejects confirmations for shots never fired");
            nm.UpsertPlayer(10,"Second Opponent",2,0,2);
            GameplayEffectsTestAccess::OnlineKill(game,nm.GetLocalPlayerID(),9,3);
            GameplayEffectsTestAccess::OnlineKill(game,nm.GetLocalPlayerID(),9,3);
            check(GameplayEffectsTestAccess::Medals(game)==0,"duplicate online kill packets cannot award a false double kill");
            GameplayEffectsTestAccess::OnlineKill(game,nm.GetLocalPlayerID(),10,3);
            check(GameplayEffectsTestAccess::Medals(game)==1 && GameplayEffectsTestAccess::MedalKind(game)==Medals::DoubleKill,"confirmed online kills award local medals across separate victims");
            GameplayEffectsTestAccess::Finish(game);GameplayEffectsTestAccess::RestartMatch(game);
            check(!GameplayEffectsTestAccess::MatchOver(game) && p->GetCombatStats().damage==0 && nm.teamScores[1]==0,"host rematch resets online scores and combat report");
            confirm.serial=3;confirm.shotID=0;check(!GameplayEffectsTestAccess::Receipt(game,confirm),"delayed confirmations from the previous match cannot affect a rematch");
        }
        nm.Shutdown();nm.Initialize();Offline::Get()=saved;
    }
    {
        Player actor({0,0});Character::SetCombatAudioEnabled(false);
        BeginTextureMode(target);ClearBackground({64,79,88,255});
        DrawText("WEAPON GRIP / LIVE CHARACTER RENDERER",28,16,22,UiTheme::AccentGold());
        Camera2D view{};view.offset={0,0};view.target={0,0};view.zoom=1.45f;BeginMode2D(view);
        for(int weapon=0;weapon<WeaponCount;++weapon)for(int row=0;row<3;++row){
            actor.ResetHealth(100);actor.SwitchWeapon(weapon);actor.SetPosition({55+weapon*112.0f,36+row*144.0f});int facing=row==1?-1:1;actor.SetFaceDirection(facing);
            Vector2 hand=actor.GetHandPosition();actor.SetAimTarget({hand.x+facing*600,hand.y+(row==2?-260:0)});actor.Draw();
        }
        EndMode2D();EndTextureMode();capture("bin/weapon-grip-preview.png");
        BeginTextureMode(target);ClearBackground({64,79,88,255});DrawText("POWER WEAPONS / MELEE / THROWABLE GRIPS",28,16,22,UiTheme::AccentGold());BeginMode2D(view);
        for(int item=0;item<6;++item)for(int row=0;row<2;++row){actor.ResetHealth(100);actor.SetPosition({70+item*145.0f,45+row*190.0f});actor.SetFaceDirection(row==0?1:-1);
            actor.SetEquipment();if(item<3)actor.GiveBonus(item);else if(item==3)actor.SetEquipment(0,0);else actor.SetEquipment(-1,0,item-4);
            Vector2 hand=actor.GetHandPosition();actor.SetAimTarget({hand.x+(row==0?600:-600),hand.y+(row==0?-120:100)});actor.Draw();}
        EndMode2D();EndTextureMode();capture("bin/equipment-grip-preview.png");Character::SetCombatAudioEnabled(true);
    }
    {
        bool oldEffects=Settings::Get().effects;Settings::Get().effects=true;Character::SetCombatAudioEnabled(false);
        Player actor({180,130});actor.UpdateSkills(.016f);Rectangle footprint=actor.GetCollisionBounds();
        actor.SetPosition({190,130});actor.UpdateSkills(.0667f);check(actor.GetBodyLean()>0 && actor.GetBodyLean()<=4.5f,"running adds bounded visual lean in the travel direction");
        auto feet=actor.GetCollisionBounds();check(std::abs(feet.x-footprint.x-10)<.001f && feet.y==footprint.y,"body lean never changes the ground collision footprint");
        actor.UpdateSkills(1);check(std::abs(actor.GetBodyLean())<.01f,"body smoothly returns upright when movement stops");
        float grounded=actor.GetJumpShadowRadius();actor.SetJumpHeight(85);actor.UpdateSkills(.016f);
        check(actor.GetJumpShadowRadius()<grounded,"jump shadow shrinks with height while remaining on the ground");
        actor.SetJumpHeight(0);actor.UpdateSkills(.016f);check(actor.GetLandingParticleCount()==10,"landing emits one bounded dust burst");
        actor.UpdateSkills(.016f);check(actor.GetLandingParticleCount()==10,"standing on the floor does not repeatedly emit landing dust");
        actor.UpdateSkills(1);check(actor.GetLandingParticleCount()==0,"landing particles expire");
        Settings::Get().effects=false;actor.SetJumpHeight(80);actor.UpdateSkills(.016f);actor.SetJumpHeight(0);actor.UpdateSkills(.016f);
        check(actor.GetLandingParticleCount()==0,"effects toggle suppresses landing particles");Settings::Get().effects=true;
        check(WeaponReload::Type(1)==WeaponReload::Style::Tube && WeaponReload::Type(6)==WeaponReload::Style::BreakOpen && WeaponReload::Type(3)==WeaponReload::Style::Magazine,"shotgun, double barrel and rifle use distinct reload mechanisms");
        check(WeaponReload::MagazineTravel(.32f)>15 && WeaponReload::MagazineTravel(.96f)==0 && WeaponReload::BreakAngle(.5f)>25 && WeaponReload::BreakAngle(1)==0,"magazine and break-action components leave the weapon then return to their closed positions");
        BeginTextureMode(target);ClearBackground({64,79,88,255});DrawText("ANIMATED RELOADS / REMOVE - LOAD - CHAMBER",25,16,20,UiTheme::AccentGold());
        Camera2D view{{0,0},{0,0},0,1.4f};BeginMode2D(view);
        for(int weapon=0;weapon<WeaponCount;++weapon)for(int row=0;row<3;++row){actor.ResetHealth(100);actor.SwitchWeapon(weapon);actor.SetPosition({60+weapon*113.0f,38+row*147.0f});actor.SetFaceDirection(1);
            Vector2 hand=actor.GetHandPosition();actor.Shoot({hand.x+600,hand.y});actor.TriggerReload();actor.UpdateSkills(Weapons[weapon].reloadTime*(row==0?.2f:row==1?.4f:.65f));actor.Draw();}
        EndMode2D();EndTextureMode();capture("bin/animated-reloads-preview.png");
        actor.ResetHealth(100);actor.SwitchWeapon(3);Vector2 hand=actor.GetHandPosition();actor.Shoot({hand.x+600,hand.y});actor.TriggerReload();int reserve=actor.GetReserveAmmo();actor.UpdateSkills(Weapons[3].reloadTime*.5f);
        check(actor.GetAmmo()==Weapons[3].maxAmmo-1 && actor.GetReserveAmmo()==reserve,"visual magazine movement does not transfer ammunition early");actor.SwitchWeapon(2);actor.SwitchWeapon(3);check(!actor.IsReloading() && actor.GetReserveAmmo()==reserve,"canceling a reload preserves magazine and reserve counts");
        RemotePlayer remote({450,180},CharacterPreview::BodyPath(2));remote.SwitchWeapon(3);remote.SetEquipment();remote.SetRemoteReload(true,.4f);
        check(remote.IsReloading() && std::abs(remote.GetReloadProgress()-.4f)<.001f,"online reload state starts at the replicated progress");
        int remoteAmmo=remote.GetAmmo();remote.Update(.1f);check(remote.GetReloadProgress()>.4f && remote.GetAmmo()==remoteAmmo,"online reload animation interpolates without inventing local ammo transfers");remote.SetEquipment();remote.SetRemoteReload(false,1);check(!remote.IsReloading(),"online reload cancellation clears the visual action");
        BeginTextureMode(target);ClearBackground({64,79,88,255});DrawText("MOVEMENT / LEAN - JUMP SHADOW - LANDING DUST",25,16,20,UiTheme::AccentGold());BeginMode2D(view);
        actor.ResetHealth(100);actor.SetPosition({130,190});actor.UpdateSkills(.016f);actor.SetPosition({145,190});actor.UpdateSkills(.1f);actor.Draw();
        actor.ResetHealth(100);actor.SetPosition({440,260});actor.SetJumpHeight(80);actor.UpdateSkills(.016f);actor.Draw();
        actor.ResetHealth(100);actor.SetPosition({740,190});actor.SetSurfaceStyle(1);actor.SetJumpHeight(100);actor.UpdateSkills(.016f);actor.SetJumpHeight(0);actor.UpdateSkills(.016f);actor.UpdateSkills(.08f);actor.Draw();
        EndMode2D();EndTextureMode();capture("bin/movement-polish-preview.png");
        for(const char* name:{"MagmaRift","AbandonedFoundry","DesertOutpost"}){
            auto* map=MapRegistry::GetInstance().GetMap(name);Vector2 focus=std::string(name)=="MagmaRift"?Vector2{1900,800}:std::string(name)=="AbandonedFoundry"?Vector2{2400,2180}:Vector2{2400,1450};
            Camera2D camera{{650,340},focus,0,1};Rectangle viewport{focus.x-650,focus.y-340,1300,680};
            auto render=[&](float time){BeginTextureMode(target);ClearBackground({15,21,27,255});BeginMode2D(camera);DrawMapWithMaterials(map->tmxMap,&camera,&viewport);DrawMapEffects(map->tmxMap,time,viewport);EndMode2D();EndTextureMode();Image result=LoadImageFromTexture(target.texture);return result;};
            Image a=render(.2f),b=render(2.7f);int changed=0;for(int y=30;y<650;y+=6)for(int x=30;x<1270;x+=6){Color c=GetImageColor(a,x,y),d=GetImageColor(b,x,y);if(std::abs((int)c.r-d.r)+std::abs((int)c.g-d.g)+std::abs((int)c.b-d.b)>4)++changed;}
            check(changed>15,"each map has visibly animated authored atmosphere");UnloadImage(a);UnloadImage(b);capture(TextFormat("bin/%s-atmosphere-preview.png",name));
            Settings::Get().effects=false;a=render(.2f);b=render(2.7f);changed=0;for(int y=20;y<660;y+=12)for(int x=20;x<1280;x+=12){Color c=GetImageColor(a,x,y),d=GetImageColor(b,x,y);if(c.r!=d.r || c.g!=d.g || c.b!=d.b)++changed;}check(changed==0,"effects-off keeps every map's terrain stable");UnloadImage(a);UnloadImage(b);Settings::Get().effects=true;
        }
        Character::SetCombatAudioEnabled(true);Settings::Get().effects=oldEffects;
    }
    // FFA power-weapon flow: mode isolation, safe distributed spawns, ownership, ammo and effects.
    {
        auto setup=Offline::Get();auto& nm=NetworkManager::GetInstance();auto map=nm.selectedMapName;
        Offline::Get().bots=1;Offline::Get().mode=Offline::Mode::FreeForAll;
        for(const char* arena:{"MagmaRift","AbandonedFoundry","DesertOutpost"}) {
            nm.selectedMapName=arena;Offline::Get().map=arena;
            GameplayScreen game(GameMode::PRACTICE);auto* p=game.GetLocalPlayer();auto* bot=GameplayEffectsTestAccess::Bot(game,0);
            check(GameplayEffectsTestAccess::BonusCount(game)==6 && GameplayEffectsTestAccess::NoFfaBase(game),"FFA alone has six special pickups and no outpost bases");
            for(int i=0;i<6;++i){Vector2 node=GameplayEffectsTestAccess::BonusPoint(game,i);check(GameplayEffectsTestAccess::ClearArena(game,node),"pickup stands on traversable ground");
                for(int j=0;j<i;++j){Vector2 other=GameplayEffectsTestAccess::BonusPoint(game,j);check(std::hypot(node.x-other.x,node.y-other.y)>500,"pickup locations remain distributed across the arena");}}
            for(int i=0;i<10;++i){GameplayEffectsTestAccess::Respawn(game);Vector2 pos=p->GetPosition();check(GameplayEffectsTestAccess::ClearArena(game,pos),"repeated FFA respawns stay outside map collision");
                Vector2 enemy=bot->GetPosition();check(std::hypot(pos.x-enemy.x,pos.y-enemy.y)>600,"random respawn avoids the live opponent");}
            GameplayEffectsTestAccess::Grace(game,0);check(!GameplayEffectsTestAccess::CanBag(game),"FFA cannot change bags by returning to the old spawn location");
            GameplayEffectsTestAccess::Respawn(game);check(GameplayEffectsTestAccess::CanBag(game),"FFA bag change is available immediately after respawn");
            for(int kind=0;kind<6;++kind){GameplayEffectsTestAccess::ReadyBonus(game,kind);Vector2 node=GameplayEffectsTestAccess::BonusPoint(game,kind);
                p->SetPosition({node.x+300,node.y});GameplayEffectsTestAccess::Claim(game,kind,p);check(GameplayEffectsTestAccess::BonusSerial(game,kind)==0,"distant player cannot consume pickup");
                p->SetPosition(node);GameplayEffectsTestAccess::Claim(game,kind,p);check(GameplayEffectsTestAccess::BonusSerial(game,kind)==1 && GameplayEffectsTestAccess::BonusCooldown(game,kind)>20,"claim consumes one pickup and starts its respawn timer");
                GameplayEffectsTestAccess::Claim(game,kind,bot,1);check(GameplayEffectsTestAccess::BonusSerial(game,kind)==1,"second claimant cannot duplicate an unavailable pickup");
                if(kind<3){check(p->BonusEquipped() && p->GetBonusAmmo()==Bonus::Ammo(kind),"power gun is equipped with bounded pickup ammo");
                    int normal=p->GetAmmo();p->SetShootCooldown(0);check(p->Shoot({node.x+500,node.y+80}),"special gun fires");
                    check(p->GetBonusAmmo()==Bonus::Ammo(kind)-1 && p->GetAmmo()==normal && p->GetProjectiles().empty(),"special shot consumes its own ammunition without spawning an ordinary bullet");
                    check(p->ConsumeBonusShot()==kind,"special shot event preserves its weapon kind");}
                else check(p->GetBonusThrow()==kind && p->GetBonusCharges()==2,"special throw pickup gives two charges");
            }
            p->GiveBonus(3);Vector2 point=GameplayEffectsTestAccess::BonusPoint(game,3);p->SetPosition(point);bot->SetPosition({point.x+80,point.y});
            check(GameplayEffectsTestAccess::ThrowSpecial(game,p,{point.x+180,point.y}),"collected freeze bomb can be thrown");
            check(p->GetBonusCharges()==1 && GameplayEffectsTestAccess::SpecialProjectiles(game)==1,"throw spends one finite charge and creates a traveling projectile");
            GameplayEffectsTestAccess::ClearBonusEffects(game);GameplayEffectsTestAccess::BonusBlast(game,3,point,p);GameplayEffectsTestAccess::BonusTime(game,.01f);
            check(p->IsFrozen() && bot->IsFrozen() && p->GetSlowFactor()==0 && !p->ActivateDash({1,0}),"freeze fully immobilizes thrower and enemy and blocks dash escape");
            Vector2 frozenPlayer=p->GetPosition(),frozenBot=bot->GetPosition();p->SetJumpHeight(30);
            p->UpdateSkills(.1f);p->UpdatePhysics(.1f);bot->UpdateAI(.1f,{point.x+800,point.y});bot->MoveToObjective(.1f,{point.x+800,point.y});
            check(p->GetPosition().x==frozenPlayer.x && p->GetPosition().y==frozenPlayer.y && p->GetJumpHeight()==30 &&
                  bot->GetPosition().x==frozenBot.x && bot->GetPosition().y==frozenBot.y,
                  "frozen player cannot drift vertically and frozen bots cannot walk or pursue objectives");
            p->SetJumpHeight(0);
            if(std::string(arena)=="MagmaRift") {GameplayEffectsTestAccess::Focus(game,point,1.3f);game.Draw(target);capture("bin/special-freeze-preview.png");}
            p->UpdateSkills(3);bot->Update(3);check(p->GetSlowFactor()==1 && bot->GetSlowFactor()==1,"freeze wears off for both fighters");
            GameplayEffectsTestAccess::ClearBonusEffects(game);p->ResetHealth(100);bot->ResetHealth(100);
            p->ActivateDash({1,0});GameplayEffectsTestAccess::BonusBlast(game,4,point,p);GameplayEffectsTestAccess::BonusTime(game,.01f);
            check(p->GetDashTimer()==0 && p->GetSlowFactor()>.4f && p->GetSlowFactor()<1 && bot->GetFlashBlind()>0,"shock interrupts dash, slows and briefly disorients opponents");
            GameplayEffectsTestAccess::ClearBonusEffects(game);p->ResetHealth(100);bot->ResetHealth(100);
            GameplayEffectsTestAccess::BonusBlast(game,2,point,p);GameplayEffectsTestAccess::BonusTime(game,.01f);
            check(p->GetHealth()==30 && bot->GetHealth()==30,"bazooka blast damages multiple targets, including the shooter");
            GameplayEffectsTestAccess::ClearBonusEffects(game);p->ResetHealth(100);bot->ResetHealth(100);
            GameplayEffectsTestAccess::BonusBlast(game,5,point,p);GameplayEffectsTestAccess::BonusTime(game,.5f);check(p->GetHealth()==92 && bot->GetHealth()==92,"incendiary area applies timed damage including to its thrower");
            if(std::string(arena)=="MagmaRift") {GameplayEffectsTestAccess::Focus(game,point,1.3f);game.Draw(target);capture("bin/special-fire-preview.png");}
            p->ResetHealth(100);bot->ResetHealth(100);check(GameplayEffectsTestAccess::RocketWall(game,p),"rocket sweeps stop at wall collision and explode instead of tunneling through it");
            p->GiveBonus(2);p->GiveBonus(4);GameplayEffectsTestAccess::Respawn(game);check(p->GetBonusAmmo()==0 && p->GetBonusCharges()==0,"death/respawn clears power weapons and special throws");
            GameplayEffectsTestAccess::ClearBonusEffects(game);p->SetPosition(GameplayEffectsTestAccess::BonusPoint(game,1));p->GiveBonus(1);
            for(int i=0;i<6;++i)GameplayEffectsTestAccess::ReadyBonus(game,i);
            GameplayEffectsTestAccess::Focus(game,{game.GetWorldWidth()*.5f,game.GetWorldHeight()*.5f},.43f);game.Draw(target);capture(TextFormat("bin/ffa-special-%s.png",arena));
        }
        nm.selectedMapName=map;
        for(auto mode:{Offline::Mode::Training,Offline::Mode::TeamDeathmatch,Offline::Mode::Elimination,Offline::Mode::Deathmatch}) {
            Offline::Get().mode=mode;GameplayScreen game(GameMode::PRACTICE);
            check(GameplayEffectsTestAccess::BonusCount(game)==0,"training, TDM, elimination and flag never spawn special pickups");
            auto* p=game.GetLocalPlayer();p->GiveBonus(3);check(!GameplayEffectsTestAccess::ThrowSpecial(game,p,p->GetPosition()),"special throws are disabled outside FFA");
        }
        check(nm.HostRoom(17778),"host bonus-claim validation test");nm.currentGameMode=OnlineGameMode::FREE_FOR_ALL;nm.UpsertPlayer(9,"Claimant",1,0,0);
        {
            GameplayScreen game(GameMode::ONLINE);check(GameplayEffectsTestAccess::BonusCount(game)==6,"online FFA also spawns special pickups");
            auto* remote=GameplayEffectsTestAccess::Remote(game,9);
            GameplayEffectsTestAccess::ReadyBonus(game,0);remote->SetPosition(GameplayEffectsTestAccess::BonusPoint(game,0));
            PacketBonusClaim request{};request.header={PacketType::BONUS_CLAIM,9};request.node=0;
            GameplayEffectsTestAccess::BonusPacket(game,&request,sizeof(request));
            check(remote->GetBonusGun()==0 && GameplayEffectsTestAccess::BonusSerial(game,0)==1,"online host grants a valid nearby claim exactly once");
            GameplayEffectsTestAccess::BonusPacket(game,&request,sizeof(request));check(GameplayEffectsTestAccess::BonusSerial(game,0)==1,"duplicate online requests cannot duplicate the pickup");
        }
        for(auto mode:{OnlineGameMode::ELIMINATION,OnlineGameMode::TEAM_DEATHMATCH,OnlineGameMode::DEATHMATCH}){nm.currentGameMode=mode;GameplayScreen game(GameMode::ONLINE);check(GameplayEffectsTestAccess::BonusCount(game)==0,"other online modes have no special pickups");}
        nm.Disconnect();nm.currentGameMode=OnlineGameMode::FREE_FOR_ALL;
        {
            GameplayScreen client(GameMode::ONLINE);auto* p=client.GetLocalPlayer();PacketBonusState state{};state.header={PacketType::BONUS_STATE,0};
            for(int i=0;i<6;++i)state.nodes[i]={GameplayEffectsTestAccess::BonusPoint(client,i),12,0,UINT32_MAX,i};
            state.nodes[0].serial=1;state.nodes[0].owner=nm.GetLocalPlayerID();
            GameplayEffectsTestAccess::BonusPacket(client,&state,sizeof(state));check(p->GetBonusAmmo()==0,"late grant cannot resurrect an old-life pickup without an outstanding claim");
            GameplayEffectsTestAccess::AwaitBonus(client,0);state.nodes[0].serial=2;
            GameplayEffectsTestAccess::BonusPacket(client,&state,sizeof(state));check(p->GetBonusAmmo()==60,"client receives a host-confirmed pickup grant");
            p->SetShootCooldown(0);Vector2 aim=p->GetPosition();aim.x+=500;p->Shoot(aim);p->ConsumeBonusShot();
            GameplayEffectsTestAccess::BonusPacket(client,&state,sizeof(state));check(p->GetBonusAmmo()==59,"repeated pickup state does not refill spent ammo");
            for(int shot=0;shot<59;++shot){p->SetShootCooldown(0);p->Shoot(aim);p->ConsumeBonusShot();}
            GameplayEffectsTestAccess::BonusTime(client,.01f);check(p->GetBonusGun()==-1 && p->GetWeaponIndex()==Settings::Get().bags[Settings::Get().activeBag].primary,"empty special gun returns automatically to the primary bag slot");
        }
        Offline::Get()=setup;nm.selectedMapName=map;
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
            auto* nearBot=GameplayEffectsTestAccess::Bot(effects,0);
            auto* farBot=GameplayEffectsTestAccess::Bot(effects,1);
            nearBot->ResetHealth(100); farBot->ResetHealth(100);
            nearBot->SetPosition({origin.x+210,origin.y}); farBot->SetPosition({origin.x+240,origin.y});
            victim->SetAimTarget(origin);
            check(effects.ThrowGrenade(0),"throw expanded blast grenade");
            GameplayEffectsTestAccess::Step(effects,.81f);
            check(nearBot->GetHealth()==60 && farBot->GetHealth()==100,"larger blast damages at 210 units and stops before 240");
            nearBot->ResetHealth(100);farBot->ResetHealth(100);
            nearBot->SetPosition({origin.x+1000,origin.y});farBot->SetPosition({origin.x+1400,origin.y});
            GameplayEffectsTestAccess::Reset(effects);
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
            GameplayEffectsTestAccess::Step(effects,0.24f);
            effects.Draw(target); capture("bin/gray-explosion-preview.png");
            GameplayEffectsTestAccess::Step(effects,0.66f);
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
