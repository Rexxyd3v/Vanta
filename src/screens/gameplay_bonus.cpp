#include "main_level.h"
#include "../network/network_manager.h"
#include "../ui/game_settings.h"
#include "../rules/match_rules.h"
#include "../ui/explosion_effect.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include "../audio/combat_audio.h"
namespace {float Dist(Vector2 a,Vector2 b){return std::hypot(a.x-b.x,a.y-b.y);} }
bool GameplayScreen::IsFreeForAll() const {
    return currentMode==GameMode::PRACTICE?offlineSetup.mode==Offline::Mode::FreeForAll:NetworkManager::GetInstance().currentGameMode==OnlineGameMode::FREE_FOR_ALL;
}
bool GameplayScreen::ArenaClear(Vector2 anchor,float margin) const {
    if(anchor.x<160 || anchor.x>worldWidth-160 || anchor.y<160 || anchor.y>worldHeight-160)return false;
    Rectangle bounds{anchor.x-8-margin,anchor.y+player->GetFeetOffset()-6-margin,16+margin*2,8+margin*2};
    for(const auto& group:allObjectGroups)for(uint32_t i=0;i<group.objectsLength;++i){const auto& o=group.objects[i];
        Rectangle r=o.type==OBJECT_TYPE_RECTANGLE?Rectangle{(float)o.x,(float)o.y,(float)o.width,(float)o.height}:o.aabb;
        if(CheckCollisionRecs(bounds,r))return false;
    }return true;
}
Vector2 GameplayScreen::RandomArenaSpawn(Character* actor) {
    std::vector<Character*> fighters{player};for(auto& b:bots)fighters.push_back(b.get());for(auto* r:remotePlayers)fighters.push_back(r);
    std::vector<Vector2> choices;Vector2 best{};float score=-1;
    for(int attempt=0;attempt<256;++attempt) {
        Vector2 p{(float)GetRandomValue(220,(int)worldWidth-220),(float)GetRandomValue(220,(int)worldHeight-220)};
        if(!ArenaClear(p,26))continue;
        float nearest=10000;
        for(auto* c:fighters)if(c && c!=actor && !c->IsDead())nearest=std::min(nearest,Dist(p,c->GetPosition()));
        for(auto& b:bonusBlasts)if(b.kind==5 && b.age<5)nearest=std::min(nearest,Dist(p,b.position)-220);
        if(actor && Dist(p,actor->GetPosition())<360)continue;
        if(nearest>score){score=nearest;best=p;}
        if(nearest>650)choices.push_back(p);
    }
    if(!choices.empty())return choices[GetRandomValue(0,(int)choices.size()-1)];
    if(score>=0)return best;
    return SafeSpawn(GetFarSpawnPoint());
}
void GameplayScreen::InitializeBonuses() {
    bonusNodes.clear();bonusProjectiles.clear();bonusBlasts.clear();bonusClock=bonusNetTimer=0;
    if(!IsFreeForAll())return;
    const Vector2 locations[]={{.24f,.32f},{.5f,.28f},{.76f,.32f},{.24f,.70f},{.5f,.72f},{.76f,.70f}};
    for(int i=0;i<Bonus::Count;++i) {
        Vector2 desired{worldWidth*locations[i].x,worldHeight*locations[i].y},best=SafeSpawn(desired);float distance=1e20f;
        for(float y=240;y<worldHeight-240;y+=64)for(float x=240;x<worldWidth-240;x+=64) {
            Vector2 p{x,y};if(!ArenaClear(p,60))continue;
            bool separated=true;for(auto& n:bonusNodes)if(Dist(n.position,p)<540)separated=false;
            float d=Dist(p,desired);if(separated && d<distance){distance=d;best=p;}
        }
        bonusNodes.push_back({best,3+i*.65f,i,0,UINT32_MAX});
    }
}
void GameplayScreen::SendBonusState() {
    auto& nm=NetworkManager::GetInstance();if(currentMode!=GameMode::ONLINE || !nm.IsHost() || bonusNodes.size()!=6)return;
    PacketBonusState packet{};packet.header={PacketType::BONUS_STATE,nm.GetLocalPlayerID()};
    for(int i=0;i<6;++i){auto& n=bonusNodes[i];packet.nodes[i]={n.position,n.cooldown,n.serial,n.owner,n.kind};}
    nm.SendPacket(&packet,sizeof(packet),true);
}
void GameplayScreen::ClaimBonus(int index,Character* actor,uint32_t owner) {
    if(!IsFreeForAll() || !actor || actor->IsDead() || matchEndTimer>0 || index<0 || index>=(int)bonusNodes.size())return;
    auto& node=bonusNodes[index];if(node.cooldown>0 || Dist(actor->GetPosition(),node.position)>100)return;
    actor->GiveBonus(node.kind);node.cooldown=node.kind<3?28:22;++node.serial;node.owner=owner;
    if(actor==player){bonusMessage=std::string(Bonus::Name(node.kind))+" ACQUIRED";bonusMessageTime=2.5f;}
    SendBonusState();
}
void GameplayScreen::HandleBonusPacket(const void* bytes,size_t size) {
    if(!IsFreeForAll() || matchEndTimer>0)return;auto& nm=NetworkManager::GetInstance();const auto* header=(const PacketHeader*)bytes;
    if(header->type==PacketType::BONUS_CLAIM && nm.IsHost() && size==sizeof(PacketBonusClaim)) {
        const auto* packet=(const PacketBonusClaim*)bytes;ClaimBonus(packet->node,FindByPeerID(header->playerID),header->playerID);
    }else if(header->type==PacketType::BONUS_STATE && !nm.IsHost() && size==sizeof(PacketBonusState) && header->playerID==0 && bonusNodes.size()==6) {
        const auto* packet=(const PacketBonusState*)bytes;
        for(int i=0;i<6;++i){const auto& wire=packet->nodes[i];auto& node=bonusNodes[i];
            if(!Bonus::Valid(wire.kind) || !std::isfinite(wire.cooldown) || wire.cooldown<0 || wire.cooldown>30 || !std::isfinite(wire.position.x) || !std::isfinite(wire.position.y) || !ArenaClear(wire.position,0))continue;
            if(wire.serial<node.serial)continue;
            if(wire.serial>node.serial && wire.owner==nm.GetLocalPlayerID() && awaitingBonusNode==i && !player->IsDead()) {
                awaitingBonusNode=-1;player->GiveBonus(wire.kind);bonusMessage=std::string(Bonus::Name(wire.kind))+" ACQUIRED";bonusMessageTime=2.5f;
            }
            node={wire.position,wire.cooldown,wire.kind,wire.serial,wire.owner};
        }
    }
}
void GameplayScreen::SpawnBonusShot(Character* actor,int kind,Vector2 direction,uint32_t owner,bool local) {
    if(!IsFreeForAll() || !actor || kind<0 || kind>2 || actor->IsDead())return;
    float d=std::hypot(direction.x,direction.y);if(!std::isfinite(d) || d<.001f)return;direction.x/=d;direction.y/=d;
    Vector2 hand=actor->GetBonusWeaponPoseInDirection(direction).muzzle;float speed=kind==0?540:kind==1?510:760;
    Vector2 position{hand.x+direction.x*2,hand.y+direction.y*2};
    bonusProjectiles.push_back({position,{direction.x*speed,direction.y*speed},0,kind,owner,local,actor,(uint32_t)actor->GetCombatStats().shots});
}
bool GameplayScreen::ThrowBonus(Character* actor,Vector2 target,uint32_t owner,bool local) {
    if(!IsFreeForAll() || !actor || actor->IsDead() || !Bonus::Valid(actor->GetBonusThrow()) || actor->GetBonusCharges()<=0)return false;
    Vector2 start=actor->GetPosition();float d=Dist(start,target);if(!std::isfinite(d))return false;
    actor->UseBonusThrow();CombatAudio::Play(CombatAudio::Throw,start,.65f);
    if(d>650){target.x=start.x+(target.x-start.x)*650/d;target.y=start.y+(target.y-start.y)*650/d;}
    target.x=std::clamp(target.x,0.0f,worldWidth);target.y=std::clamp(target.y,0.0f,worldHeight);
    int kind=actor->GetBonusThrow();bonusProjectiles.push_back({start,{(target.x-start.x)/.8f,(target.y-start.y)/.8f},0,kind,owner,local,actor});
    if(currentMode==GameMode::ONLINE && local){PacketPlayerGrenade packet{};packet.header={PacketType::PLAYER_GRENADE,owner};packet.start=start;packet.target=target;packet.kind=kind;NetworkManager::GetInstance().SendPacket(&packet,sizeof(packet),true);}
    return true;
}
void GameplayScreen::ApplyBonusHit(Character* shooter,Character* victim,float damage,int kind,uint32_t owner,uint32_t shot) {
    if(!victim || victim->IsDead() || (shooter && shooter!=victim && shooter->GetTeamID()!=0 && shooter->GetTeamID()==victim->GetTeamID()))return;
    if(currentMode==GameMode::ONLINE && victim!=player) {
        auto* remote=dynamic_cast<RemotePlayer*>(victim);if(!remote)return;
        PacketPlayerDamage packet{};packet.header={PacketType::PLAYER_DAMAGE,owner};packet.targetPlayerID=remote->peerID;packet.damageAmount=damage;packet.weaponIndex=Bonus::FirstDamage+kind;packet.shotID=shot;
        NetworkManager::GetInstance().SendPacket(&packet,sizeof(packet),true);return;
    }
    float before=victim->GetHealth();victim->TakeDamage(damage);if(victim->GetHealth()<before)NoteDamage(shooter,victim,before-victim->GetHealth(),Bonus::FirstDamage+kind,0,shot);
    if(victim->IsDead()) {
        if(currentMode==GameMode::PRACTICE)RecordOfflineKill(shooter?shooter:player,victim,Bonus::FirstDamage+kind);
        else {auto& nm=NetworkManager::GetInstance();ProcessKill(owner,nm.GetLocalPlayerID(),Bonus::FirstDamage+kind);PacketPlayerKilled packet{};packet.header={PacketType::PLAYER_KILLED,nm.GetLocalPlayerID()};packet.killerPlayerID=owner;packet.victimPlayerID=nm.GetLocalPlayerID();packet.weaponIndex=Bonus::FirstDamage+kind;nm.SendPacket(&packet,sizeof(packet),true);}
    }
}
void GameplayScreen::UpdateBonuses(float dt) {
    if(matchEndTimer>0 || !IsFreeForAll() || (currentMode==GameMode::PRACTICE && (isPaused || Settings::BlocksInput())))return;
    if(player->IsDead())awaitingBonusNode=-1;
    bonusClock+=dt;bonusMessageTime=std::max(0.0f,bonusMessageTime-dt);spawnGrace=std::max(0.0f,spawnGrace-dt);
    auto& nm=NetworkManager::GetInstance();bool authority=currentMode==GameMode::PRACTICE || nm.IsHost();uint32_t me=nm.GetLocalPlayerID();
    for(auto& n:bonusNodes)n.cooldown=std::max(0.0f,n.cooldown-dt);
    if(authority){bonusNetTimer-=dt;if(bonusNetTimer<=0){SendBonusState();bonusNetTimer=.5f;}}
    if(!isPaused && !Settings::BlocksInput() && !bagMenuOpen && !bagInputConsumed && IsWindowFocused() && !player->IsDead()) {
        if(Settings::Pressed(Settings::Interact))for(int i=0;i<(int)bonusNodes.size();++i)if(bonusNodes[i].cooldown<=0 && Dist(player->GetPosition(),bonusNodes[i].position)<100) {
            if(authority)ClaimBonus(i,player,me);else{awaitingBonusNode=i;PacketBonusClaim p{};p.header={PacketType::BONUS_CLAIM,me};p.node=i;nm.SendPacket(&p,sizeof(p),true);}break;
        }
        if(Settings::Pressed(Settings::SpecialThrow))ThrowBonus(player,player->GetAimTarget(),me);
    }
    int localShot=player->ConsumeBonusShot();if(localShot>=0)SpawnBonusShot(player,localShot,player->GetLastShotDirection(),me,true);
    if(player->GetBonusGun()>=0 && player->GetBonusAmmo()<=0){player->ClearBonusGun();player->SelectEquipment(0);}
    if(currentMode==GameMode::PRACTICE)for(size_t i=0;i<bots.size();++i) {
        auto* bot=bots[i].get();uint32_t id=(uint32_t)i+1;
        if(!bot->IsDead()) {
            for(int n=0;n<(int)bonusNodes.size();++n)if(bonusNodes[n].cooldown<=0 && Dist(bot->GetPosition(),bonusNodes[n].position)<85)ClaimBonus(n,bot,id);
            Character* target=NearestOfflineEnemy(bot);
            if(target && bot->GetBonusCharges()>0 && Dist(target->GetPosition(),bot->GetPosition())<650 && std::fmod(bonusClock+i*.7f,8.0f)<dt)ThrowBonus(bot,target->GetPosition(),id);
            if(bot->GetBonusAmmo()>0)bot->EquipBonus();
        }
        int kind=bot->ConsumeBonusShot();if(kind>=0)SpawnBonusShot(bot,kind,bot->GetLastShotDirection(),id,true);
        if(bot->GetBonusGun()>=0 && bot->GetBonusAmmo()<=0)bot->ClearBonusGun();
    }
    std::vector<Character*> fighters{player};for(auto& b:bots)fighters.push_back(b.get());for(auto* r:remotePlayers)fighters.push_back(r);
    for(auto& p:bonusProjectiles) {
        Vector2 old=p.position;p.age+=dt;p.position.x+=p.velocity.x*dt;p.position.y+=p.velocity.y*dt;
        bool impact=false;float duration=p.kind==0?.55f:p.kind==1?1.0f:p.kind==2?1.8f:.8f;
        if(p.kind<3) {
            int steps=std::max(1,(int)std::ceil(Dist(old,p.position)/8));
            for(int step=1;step<=steps && !impact;++step) {
                Vector2 point{old.x+(p.position.x-old.x)*step/steps,old.y+(p.position.y-old.y)*step/steps};
                // Projectiles use actual wall rectangles; their flight height does not move wall collision.
                for(const auto& group:allObjectGroups)for(uint32_t j=0;j<group.objectsLength;++j){auto& o=group.objects[j];Rectangle wall=o.type==OBJECT_TYPE_RECTANGLE?Rectangle{(float)o.x,(float)o.y,(float)o.width,(float)o.height}:o.aabb;if(CheckCollisionPointRec(point,wall)){impact=true;break;}}
                if(impact){p.position=point;break;}
                for(auto* victim:fighters)if(victim!=p.actor && !victim->IsDead()) {
                    Vector2 pos=victim->GetPosition();pos.y-=victim->GetJumpHeight();
                    if(CheckCollisionCircleRec(point,p.kind==0?16:7,{pos.x-22,pos.y,44,65})) {
                        impact=true;p.position=point;if(p.kind==0 && p.local)ApplyBonusHit(p.actor,victim,5,p.kind,p.owner,p.shot);break;
                    }
                }
            }
        }
        if(impact || p.age>=duration) {
            if(p.kind!=0) {
                CombatAudio::Play(p.kind==3?CombatAudio::Freeze:p.kind==4?CombatAudio::Shock:CombatAudio::Blast,p.position,p.kind==5?.6f:1);
                bonusBlasts.push_back({p.position,0,p.kind,p.owner,p.local,p.actor,p.shot});
                if(p.kind<3)explosionShake=std::max(explosionShake,16*std::clamp(1-Dist(player->GetPosition(),p.position)/900,0.0f,1.0f));
            }
            p.age=100;
        }
    }
    bonusProjectiles.erase(std::remove_if(bonusProjectiles.begin(),bonusProjectiles.end(),[](const BonusProjectile& p){return p.age>=100;}),bonusProjectiles.end());
    for(auto& blast:bonusBlasts) {
        float before=blast.age;blast.age+=dt;
        bool first=before==0;int tick=blast.kind==5?((int)(std::min(blast.age,5.0f)/.5f)-(int)(std::min(before,5.0f)/.5f)):0;
        if(!first && !tick)continue;
        float radius=blast.kind==1?175:blast.kind==2?230:blast.kind==3?280:blast.kind==4?245:175;
        for(auto* victim:fighters)if(!victim->IsDead() && Dist(victim->GetPosition(),blast.position)<radius) {
            if(blast.kind==3 || blast.kind==4) {
                if(currentMode==GameMode::PRACTICE || victim==player){victim->ApplySlow(blast.kind==3?2.5f:1.8f,blast.kind==3?0.0f:.55f);if(blast.kind==4)victim->ApplyFlash(.35f);}
            }
            if(blast.local && blast.kind!=3 && (blast.kind!=5 || tick)) {
                float damage=blast.kind==1?48:blast.kind==2?70:blast.kind==4?12:8*tick;
                ApplyBonusHit(blast.actor,victim,damage,blast.kind,blast.owner,blast.shot);
            }
        }
    }
    bonusBlasts.erase(std::remove_if(bonusBlasts.begin(),bonusBlasts.end(),[](const BonusBlast& b){return b.age>(b.kind==5?5.0f:b.kind<3?1.2f:1.0f);}),bonusBlasts.end());
}
void GameplayScreen::DrawBonuses() {
    if(!IsFreeForAll())return;
    for(auto& n:bonusNodes) {
        Vector2 p=GroundPoint(n.position);Color color=Bonus::Tint(n.kind);bool ready=n.cooldown<=0;
        DrawEllipse((int)p.x,(int)p.y,53,22,Fade(BLACK,.3f));
        DrawEllipseLines((int)p.x,(int)p.y,47,18,Fade(color,ready?.8f:.2f));
        if(ready){float bob=std::sin(bonusClock*2.4f+n.kind)*5;DrawBonusArt(n.kind,{p.x-57,p.y-77+bob,114,64});
            DrawCircleGradient({p.x,p.y-23},42,Fade(color,.12f),BLANK);
        }else{DrawRing({p.x,p.y-12},12,14,-90,-90+360*(1-n.cooldown/(n.kind<3?28:22)),32,Fade(color,.5f));DrawText(TextFormat("%ds",(int)std::ceil(n.cooldown)),(int)p.x-12,(int)p.y-18,12,Fade(color,.8f));}
        if(ready && matchEndTimer<=0 && Dist(player->GetPosition(),n.position)<170 && !player->IsDead()) {
            const char* text=TextFormat("[%s] %s",Settings::KeyLabel(Settings::Interact),Bonus::Name(n.kind));int w=MeasureText(text,13);
            DrawText(text,(int)p.x-w/2,(int)p.y-95,13,color);
        }
    }
    for(auto& p:bonusProjectiles) {
        Vector2 point=p.position;float d=std::hypot(p.velocity.x,p.velocity.y),angle=std::atan2(p.velocity.y,p.velocity.x)*RAD2DEG;
        Vector2 axis{p.velocity.x/d,p.velocity.y/d};
        if(p.kind==0){
            float fade=std::min(1.0f,(.6f-p.age)*5);
            Vector2 root{point.x-axis.x*(35+p.age*50),point.y-axis.y*(35+p.age*50)};
            DrawBonusFlame(root,axis,45+p.age*85,8+p.age*27,bonusClock+p.age,fade);
        }else if(p.kind>=3){
            point.y-=std::sin(std::min(p.age/.8f,1.0f)*PI)*110;
            DrawEllipse((int)p.position.x,(int)p.position.y,11,5,Fade(BLACK,.24f));
            DrawLineEx({point.x-axis.x*17,point.y-axis.y*17},point,2,Fade(Bonus::Tint(p.kind),.6f));
            DrawBonusArt(p.kind,{point.x-18,point.y-18,36,36},p.age*280);
        }else if(p.kind==1){
            point.y-=std::sin(std::min(p.age,1.0f)*PI)*110;
            DrawEllipse((int)p.position.x,(int)p.position.y,10,5,Fade(BLACK,.25f));
            DrawRectanglePro({point.x,point.y,25,13},{12.5f,6.5f},angle,{26,33,38,255});
            DrawRectanglePro({point.x,point.y,17,9},{8.5f,4.5f},angle,{186,161,87,255});
            DrawRectanglePro({point.x+axis.x*8,point.y+axis.y*8,7,9},{3.5f,4.5f},angle,{241,205,113,255});
            for(int i=1;i<5;++i)DrawCircleGradient({point.x-axis.x*i*10,point.y-axis.y*i*10},4+i,Fade({152,148,138,255},.13f),BLANK);
        }else{
            Vector2 tail{point.x-axis.x*17,point.y-axis.y*17};
            for(int i=1;i<7;++i)DrawCircleGradient({tail.x-axis.x*i*14,tail.y-axis.y*i*14},5+i*1.7f,Fade({119,119,116,255},.15f*(1-i/7.0f)),BLANK);
            DrawBonusFlame(tail,{-axis.x,-axis.y},36,5,bonusClock*2,.9f);
            Vector2 normal{-axis.y,axis.x};
            DrawLineEx({point.x-axis.x*16,point.y-axis.y*16},point,10,{28,36,42,255});
            DrawLineEx({point.x-axis.x*13-normal.x*2,point.y-axis.y*13-normal.y*2},point,6,{179,196,185,255});
            DrawTriangle({point.x+axis.x*13,point.y+axis.y*13},{point.x-normal.x*7,point.y-normal.y*7},{point.x+normal.x*7,point.y+normal.y*7},{211,116,76,255});
            DrawLineEx({tail.x+normal.x*8,tail.y+normal.y*8},{tail.x+axis.x*8,tail.y+axis.y*8},3,{157,168,157,255});
            DrawLineEx({tail.x-normal.x*8,tail.y-normal.y*8},{tail.x+axis.x*8,tail.y+axis.y*8},3,{157,168,157,255});
        }
    }

    for(auto& b:bonusBlasts) {
        if(b.kind<3){ExplosionFx::Scorch(b.position,b.age,1.2f);ExplosionFx::Draw(b.position,b.age,Settings::Get().effects);continue;}
        if(b.kind==3 || b.kind==5){DrawBonusBurst(b.kind,GroundPoint(b.position),b.age,Settings::Get().effects);continue;}
        float fade=std::clamp((1-b.age)*2,0.0f,1.0f);Color color=Bonus::Tint(4);
        DrawCircleGradient(b.position,245,Fade(color,.12f*fade),BLANK);
        float ring=245*std::min(1.0f,b.age*5);DrawRing(b.position,ring,ring+2,0,360,80,Fade(color,.65f*fade));
        for(int i=0;i<(Settings::Get().effects?14:7);++i){float angle=i*2.39996f;Vector2 last=b.position;
            for(int j=1;j<=5;++j){float r=j*44.0f,a=angle+std::sin(i*7+j*11+(int)(b.age*18))*.17f;
                Vector2 p{b.position.x+std::cos(a)*r,b.position.y+std::sin(a)*r*.8f};
                DrawLineEx(last,p,5,Fade(color,.2f*fade));DrawLineEx(last,p,1.4f,Fade({229,224,255,255},fade));last=p;
            }
        }
    }
}
void GameplayScreen::DrawBonusHud() {
    if(!IsFreeForAll())return;
    for(const auto& node:bonusNodes)if(node.cooldown<=0 && !player->IsDead() && matchEndTimer<=0 && Dist(node.position,player->GetPosition())<130){
        const char* prompt=TextFormat("[%s] PICK UP %s",Settings::KeyLabel(Settings::Interact),Bonus::Name(node.kind));
        DrawText(prompt,650-MeasureText(prompt,14)/2,463,14,Bonus::Tint(node.kind));break;
    }
    if(bonusMessageTime>0){const char* text=bonusMessage.c_str();DrawText(text,650-MeasureText(text,18)/2,224,18,{248,211,133,255});}
    if(player->GetBonusAmmo()>0){DrawText(TextFormat("[7] %s / %d",Bonus::Name(player->GetBonusGun()),player->GetBonusAmmo()),38,239,12,Bonus::Tint(player->GetBonusGun()));}
    if(player->GetBonusCharges()>0){DrawBonusArt(player->GetBonusThrow(),{34,260,38,28});DrawText(TextFormat("[%s] %s / %d",Settings::KeyLabel(Settings::SpecialThrow),Bonus::Name(player->GetBonusThrow()),player->GetBonusCharges()),78,267,12,Bonus::Tint(player->GetBonusThrow()));}
    if(player->GetSlowTime()>0)DrawText(TextFormat("%s / %.1fs",player->GetSlowFactor()<.4f?"FROZEN":"SHOCKED",player->GetSlowTime()),38,309,14,Bonus::Tint(player->GetSlowFactor()<.4f?3:4));
}
