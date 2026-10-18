#include "main_level.h"
#include "../network/network_manager.h"
#include "../audio/combat_audio.h"
#include "../ui/game_settings.h"
#include "../ui/ui_widgets.h"
#include "../ui/match_presentation.h"
#include <algorithm>
#include <cmath>
namespace {
float Cross(Vector2 a,Vector2 b){return a.x*b.y-a.y*b.x;}
bool SegmentHit(Vector2 a,Vector2 delta,Vector2 c,Vector2 d,float& t,Vector2& normal){
    Vector2 edge{d.x-c.x,d.y-c.y},offset{c.x-a.x,c.y-a.y};float divisor=Cross(delta,edge);
    if(std::abs(divisor)<.00001f)return false;
    float hit=Cross(offset,edge)/divisor,u=Cross(offset,delta)/divisor;
    if(hit<0 || hit>t || u<0 || u>1)return false;
    t=hit;float length=std::hypot(edge.x,edge.y);normal={-edge.y/length,edge.x/length};
    if(normal.x*delta.x+normal.y*delta.y>0){normal.x=-normal.x;normal.y=-normal.y;}return true;
}
}
void GameplayScreen::NoteDamage(Character* attacker,Character* victim,float amount,int weapon,int skin,uint32_t shot){
    if(!victim || !std::isfinite(amount) || amount<=0)return;
    auto& nm=NetworkManager::GetInstance();
    if(attacker && attacker!=victim){
        if(currentMode==GameMode::PRACTICE || attacker==player){attacker->ConfirmDamage(amount,shot);if(attacker==player)RegisterHit(victim->GetPosition(),amount);}
        else if(victim==player){
            auto* remote=dynamic_cast<RemotePlayer*>(attacker);
            if(remote){PacketDamageConfirm p{};p.header={PacketType::DAMAGE_CONFIRM,nm.GetLocalPlayerID()};p.recipient=remote->peerID;p.serial=++damageSerial;p.shotID=shot;p.epoch=combatEpoch;p.damage=amount;p.position=victim->GetPosition();nm.SendPacket(&p,sizeof(p),true);}
        }
    }
    if(victim!=player)return;
    damageFlash=.5f;damageSource=attacker?attacker->GetPosition():victim->GetPosition();
    std::string name=attacker==player?"YOURSELF":attacker?attacker->GetName():"ENVIRONMENT";
    auto found=std::find_if(lifeDamage.begin(),lifeDamage.end(),[&](const RecapHit& hit){return hit.attacker==name && hit.weapon==weapon && hit.skin==skin;});
    if(found!=lifeDamage.end()){found->damage+=amount;++found->hits;}
    else{if(lifeDamage.size()>=32)lifeDamage.erase(lifeDamage.begin());lifeDamage.push_back({name,weapon,skin,1,amount});}
}
void GameplayScreen::BeginDeath(Character* killer,int weapon,int skin){
    deathKiller=killer==player?"YOURSELF":killer?killer->GetName():"ENVIRONMENT";deathWeapon=weapon;deathSkin=skin;
    spectateTarget=nullptr;
}
std::vector<Character*> GameplayScreen::SpectatorCandidates()const{
    std::vector<Character*> choices;auto include=[&](Character* c){if(c && !c->IsDead() && (!HasTeamBases() || c->GetTeamID()==player->GetTeamID()))choices.push_back(c);};
    for(const auto& b:bots)include(b.get());for(auto* r:remotePlayers)include(r);return choices;
}
void GameplayScreen::UpdateSpectator(float){
    if(!player->IsDead() || matchEndTimer>0){spectateTarget=nullptr;return;}
    auto choices=SpectatorCandidates();if(choices.empty()){spectateTarget=nullptr;return;}
    auto found=std::find(choices.begin(),choices.end(),spectateTarget);
    int index=found==choices.end()?0:(int)(found-choices.begin());
    if(!Settings::BlocksInput() && !isPaused && IsWindowFocused()){
        if(IsKeyPressed(KEY_RIGHT))index=(index+1)%choices.size();
        if(IsKeyPressed(KEY_LEFT))index=(index+(int)choices.size()-1)%choices.size();
    }
    spectateTarget=choices[index];
}
void GameplayScreen::UpdateBulletWorld(){
    std::vector<Character*> actors{player};if(player2)actors.push_back(player2);for(auto& b:bots)actors.push_back(b.get());for(auto* r:remotePlayers)actors.push_back(r);
    for(auto* actor:actors)for(auto& bullet:actor->GetProjectiles()){
        if(!bullet->IsActive())continue;Vector2 a=bullet->GetPreviousPosition(),b=bullet->GetPosition(),delta{b.x-a.x,b.y-a.y};
        if(std::hypot(delta.x,delta.y)<.001f)continue;
        float t=1;Vector2 normal{};bool hit=false;
        for(const auto& group:allObjectGroups)for(uint32_t j=0;j<group.objectsLength;++j){const auto& o=group.objects[j];
            if((o.type==OBJECT_TYPE_POLYGON || o.type==OBJECT_TYPE_POLYLINE) && o.pointsLength>2){
                for(uint32_t k=o.type==OBJECT_TYPE_POLYGON?1:0;k+1<o.pointsLength;++k){auto x=o.points[k],y=o.points[k+1];
                    hit=SegmentHit(a,delta,{(float)o.x+x.x,(float)o.y+x.y},{(float)o.x+y.x,(float)o.y+y.y},t,normal)||hit;}
            }else{Rectangle r=o.type==OBJECT_TYPE_RECTANGLE?Rectangle{(float)o.x,(float)o.y,(float)o.width,(float)o.height}:o.aabb;
                Vector2 points[]={{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}};
                for(int edge=0;edge<4;++edge)hit=SegmentHit(a,delta,points[edge],points[(edge+1)%4],t,normal)||hit;
            }
        }
        if(hit){bullet->Deactivate();if(impacts.size()>=128)impacts.erase(impacts.begin());impacts.push_back({{a.x+delta.x*t,a.y+delta.y*t},normal,0,mapFolderPath.find("Foundry")!=std::string::npos?1:0});}
    }
}
void GameplayScreen::DrawCombatEffects(){
    if(!Settings::Get().effects)return;
    for(const auto& hit:impacts){float fade=std::clamp(1-hit.age/.35f,0.0f,1.0f);Color color=hit.material==2?Color{236,237,218,255}:hit.material==1?Color{255,209,113,255}:Color{180,167,145,255};
        Vector2 side{-hit.normal.y,hit.normal.x};
        for(int i=0;i<7;++i){float lane=(i-3)/3.0f,speed=50+(i*31%80);Vector2 v{hit.normal.x*speed+side.x*lane*70,hit.normal.y*speed+side.y*lane*70};
            Vector2 p{hit.position.x+v.x*hit.age,hit.position.y+v.y*hit.age+65*hit.age*hit.age};
            DrawLineEx(p,{p.x-v.x*.025f,p.y-v.y*.025f},hit.material==1?1.5f:2.5f,Fade(color,fade));
        }
        if(hit.age<.07f)DrawCircleGradient(hit.position,12,Fade(color,fade*.55f),BLANK);
    }
}
std::vector<Character*> GameplayScreen::RankedActors()const{
    auto& nm=NetworkManager::GetInstance();std::vector<Character*> actors{player};for(auto& b:bots)actors.push_back(b.get());for(auto* r:remotePlayers)actors.push_back(r);
    auto kills=[&](Character* a){return a==player && currentMode==GameMode::ONLINE?nm.localKills:a->GetKills();};
    auto deaths=[&](Character* a){return a==player && currentMode==GameMode::ONLINE?nm.localDeaths:a->GetDeaths();};
    std::stable_sort(actors.begin(),actors.end(),[&](Character* a,Character* b){return a->GetCombatStats().Rating(kills(a),deaths(a))>b->GetCombatStats().Rating(kills(b),deaths(b));});return actors;
}
void GameplayScreen::CreditCapture(Character* actor){
    if(!actor)return;actor->AddCapture();auto& nm=NetworkManager::GetInstance();
    if(currentMode==GameMode::ONLINE && nm.IsHost()){
        PacketCaptureCredit p{};p.header={PacketType::CAPTURE_CREDIT,nm.GetLocalPlayerID()};p.actor=actor==player?nm.GetLocalPlayerID():static_cast<RemotePlayer*>(actor)->peerID;p.total=actor->GetCombatStats().captures;nm.SendPacket(&p,sizeof(p),true);
    }
}
void GameplayScreen::LeaveResults(){
    auto& nm=NetworkManager::GetInstance();
    if(currentMode==GameMode::ONLINE){if(nm.IsHost()){PacketHeader p{PacketType::RETURN_LOBBY,nm.GetLocalPlayerID()};nm.SendPacket(&p,sizeof(p),true);}else nm.Disconnect();}
    returnToLobby=true;
}
void GameplayScreen::RestartMatch(bool broadcast){
    auto& nm=NetworkManager::GetInstance();if(currentMode==GameMode::ONLINE && broadcast && !nm.IsHost())return;
    if(currentMode==GameMode::ONLINE && broadcast){PacketHeader p{PacketType::MATCH_RESTART,nm.GetLocalPlayerID()};nm.SendPacket(&p,sizeof(p),true);}
    flagBotPlans.clear();flagRunners[1]=flagRunners[2]=nullptr;
    medalTracker.Reset();medalQueue.clear();medalClock=0;
    ++combatEpoch;
    nm.ResetMatch();nm.localKills=nm.localDeaths=0;if(currentMode==GameMode::ONLINE)nm.matchActive=true;
    elapsed=roundElapsed=deadFor=stateTimer=matchEndTimer=roundBannerTimer=0;matchWinner=-1;currentRoundNumber=0;
    offlineRoundOver=isSpectating=returnToLobby=isPaused=bagMenuOpen=false;roundInProgress=false;offlineResult.clear();
    offlineScores[0]=offlineScores[1]=offlineScores[2]=0;eliminated.clear();lifeDamage.clear();deathKiller.clear();spectateTarget=nullptr;
    ResetGrenades();wasReloading.clear();flagInteractRequests.clear();flagAnnouncement.clear();flagAnnouncementTimer=flagStateTimer=0;
    damageReceipts.clear();resultsAge=0;resultsScroll=0;impacts.clear();damageNumbers.clear();killFeed.clear();hitMarker=killConfirm=damageFlash=0;
    // Keep damageSerial monotonic so delayed acknowledgements cannot match a later rematch.
    player->ResetMatchStats();RespawnLocal();
    for(auto& bot:bots){bot->ResetMatchStats();bot->ResetHealth(100);bot->ResetDeathTimer();bot->SetPosition(bot->GetTeamID()?GetTeamSpawnPoint(bot->GetTeamID()):RandomArenaSpawn(bot.get()));ResolveWorldCollision(bot.get());}
    for(auto* remote:remotePlayers){remote->ResetMatchStats();remote->ResetHealth(100);remote->Teleport(remote->GetTeamID()?GetTeamSpawnPoint(remote->GetTeamID()):RandomArenaSpawn(remote));}
    ResetFlag(1);ResetFlag(2);InitializeBonuses();
    if(currentMode==GameMode::PRACTICE && Offline::RoundMode(offlineSetup.mode))StartOfflineRound();
    else if(currentMode==GameMode::ONLINE && nm.IsHost() && nm.currentGameMode==OnlineGameMode::ELIMINATION)BroadcastRoundStart(1);
    camera.target=player->GetPosition();
}
void GameplayScreen::UpdateResultsControls(){
    if(matchEndTimer<=0 || !MatchPresentation::Ready(resultsAge) || Settings::BlocksInput() || isPaused || !IsWindowFocused())return;
    resultsScroll=std::clamp(resultsScroll-(int)GetMouseWheelMove(),0,std::max(0,(int)RankedActors().size()-8));
    auto mouse=Ui::RemapMouseToVirtual();bool click=IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    bool authority=currentMode==GameMode::PRACTICE || NetworkManager::GetInstance().IsHost();
    if(authority && (IsKeyPressed(KEY_R) || (click && CheckCollisionPointRec(mouse,{70,610,390,48}))))RestartMatch();
    else if((IsKeyPressed(KEY_ENTER) || (click && CheckCollisionPointRec(mouse,{830,610,400,48}))))LeaveResults();
}

bool GameplayScreen::AcceptDamageConfirm(const PacketDamageConfirm& p){
    auto& nm=NetworkManager::GetInstance();uint64_t key=((uint64_t)p.header.playerID<<32)|p.serial;
    if(p.epoch!=combatEpoch || p.recipient!=nm.GetLocalPlayerID() || p.header.playerID==nm.GetLocalPlayerID() || !nm.FindPlayer(p.header.playerID) ||
       p.serial==0 || !std::isfinite(p.damage) || p.damage<=0 || p.damage>90 || p.shotID>(uint32_t)player->GetCombatStats().shots ||
       !std::isfinite(p.position.x) || !std::isfinite(p.position.y) || !damageReceipts.insert(key).second)return false;
    player->ConfirmDamage(p.damage,p.shotID);RegisterHit(p.position,p.damage);return true;
}
