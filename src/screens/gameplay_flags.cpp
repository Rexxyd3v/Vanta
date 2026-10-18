#include "main_level.h"
#include "../network/network_manager.h"
#include "../ui/ui_theme.h"
#include "../ui/game_settings.h"
#include "../rules/flag_rules.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
float Range(Vector2 a,Vector2 b) { float dx=a.x-b.x,dy=a.y-b.y; return std::sqrt(dx*dx+dy*dy); }
Color TeamColor(int team) { return team==1?UiTheme::TeamBlue():UiTheme::TeamRed(); }
}
bool GameplayScreen::IsFlagDeathmatch() const {
    return currentMode==GameMode::PRACTICE?offlineSetup.mode==Offline::Mode::Deathmatch:
        NetworkManager::GetInstance().currentGameMode==OnlineGameMode::DEATHMATCH;
}
Vector2 GameplayScreen::GroundPoint(Vector2 anchor) const {
    return {anchor.x,anchor.y+player->GetFeetOffset()};
}
bool GameplayScreen::HasTeamBases() const {
    return currentMode==GameMode::PRACTICE?Offline::TeamMode(offlineSetup.mode):NetworkManager::GetInstance().currentGameMode!=OnlineGameMode::FREE_FOR_ALL;
}
bool GameplayScreen::HasDeathmatchBases() const {
    return false; // FFA has random arena spawns, never bases.
}
Vector2 GameplayScreen::SafeSpawn(Vector2 desired) {
    Vector2 saved=player->GetPosition(),best=desired; float closest=1e10f;
    for (int ring=0;ring<5;++ring) for (int side=0;side<(ring==0?1:12);++side) {
        float angle=side*2*PI/12;
        Vector2 candidate{desired.x+std::cos(angle)*ring*36,desired.y+std::sin(angle)*ring*36};
        player->SetPosition(candidate); ResolveWorldCollision(player); ClampCharacterToWorld(player);
        Vector2 resolved=player->GetPosition(); float displacement=Range(candidate,resolved),distance=Range(desired,resolved);
        if (distance<closest) { closest=distance; best=resolved; }
        if (displacement<0.5f) { player->SetPosition(saved); return resolved; }
    }
    player->SetPosition(saved); return best;
}
void GameplayScreen::InitializeBases() {
    for (int team=1;team<=2;++team) {
        teamBasePositions[team]=SafeSpawn({worldWidth*(team==1?0.16f:0.84f),worldHeight*0.46f});
        teamSpawnPoints[team].clear();
        for (int i=0;i<8;++i) {
            float angle=i*2*PI/8; Vector2 center=teamBasePositions[team];
            Vector2 spawn=SafeSpawn({center.x+std::cos(angle)*52,center.y+std::sin(angle)*34});
            // Every spawn belongs to the visible base footprint; fall back to the clear center.
            if (Range(spawn,center)>110) spawn=center;
            teamSpawnPoints[team].push_back(spawn);
        }
    }
    for (int i=0;i<4;++i) deathmatchBases[i]=SafeSpawn({worldWidth*(i%2?0.80f:0.20f),worldHeight*(i<2?0.24f:0.68f)});
    for (int i=0;i<16;++i) {
        Vector2 base=deathmatchBases[i%4]; float angle=(i/4)*PI*0.5f;
        Vector2 spawn=SafeSpawn({base.x+std::cos(angle)*26,base.y+std::sin(angle)*20});
        deathmatchSpawns[i]=Range(spawn,base)<100?spawn:base;
    }
}
Vector2 GameplayScreen::GetDeathmatchSpawnPoint(uint32_t identity) const { return deathmatchSpawns[identity%16]; }
void GameplayScreen::AnnounceFlag(const std::string& text,int team) {
    flagAnnouncement=text; flagAnnouncementTeam=team; flagAnnouncementTimer=2.8f;
    ++flagAnnouncementSerial;
}
void GameplayScreen::ResetFlag(int team,float delay) {
    flags[team]=FlagState{}; flags[team].position=teamBasePositions[team]; flags[team].respawnTimer=delay;
}
void GameplayScreen::UpdateFlags(float dt,bool dropFlag,Character* actor) {
    if (!actor) actor=player;
    flagAnnouncementTimer=std::max(0.0f,flagAnnouncementTimer-dt);
    std::vector<Character*> fighters{player};
    for (auto& bot:bots) fighters.push_back(bot.get());
    for (auto* remote:remotePlayers) fighters.push_back(remote);
    for (int team=1;team<=2;++team) {
        auto& flag=flags[team];
        flag.pickupLock=std::max(0.0f,flag.pickupLock-dt);
        if (flag.respawnTimer>0) { flag.respawnTimer=std::max(0.0f,flag.respawnTimer-dt); continue; }
        // Interaction also lets the player take the objective from a nearby bot.
        // Only the carrier at the start of the frame can drop it with this press.
        if (dropFlag && !actor->IsDead() && actor->GetTeamID()==team &&
            flag.carrier && flag.carrier!=actor && !flag.carrier->IsDead() &&
            Range(actor->GetPosition(),flag.carrier->GetPosition())<=FlagRules::HandoffRadius) {
            flag.carrier=actor; flag.position=actor->GetPosition();
            AnnounceFlag(actor->GetName()+" TOOK THE FLAG",team);
            dropFlag=false;
        }
        if (flag.carrier) {
            Character* carrier=flag.carrier; flag.position=carrier->GetPosition();
            if (carrier->IsDead() || (carrier==actor && dropFlag)) {
                flag.carrier=nullptr; flag.dropped=true; flag.returnTimer=FlagRules::ReturnDelay;
                flag.previousCarrier=carrier; flag.pickupLock=FlagRules::DropPickupLock;
                AnnounceFlag(std::string(team==1?"GR":"BL")+" FLAG DROPPED",team);
            } else if (Range(flag.position,teamBasePositions[3-team])<=FlagRules::CaptureRadius) {
                if (currentMode==GameMode::PRACTICE) ++offlineScores[team];
                else ++NetworkManager::GetInstance().teamScores[team];
                CreditCapture(carrier);
                ResetFlag(team,FlagRules::ScoreResetDelay);
                AnnounceFlag(std::string(team==1?"GLOBAL RISK":"BLACK LIST")+" CAPTURED!",team);
                continue;
            }
        }
        if (flag.carrier) continue;
        if (flag.dropped) {
            flag.returnTimer-=dt;
            if (flag.returnTimer<=0) { ResetFlag(team); AnnounceFlag(std::string(team==1?"GR":"BL")+" FLAG RETURNED",team); continue; }
        }
        Character* closest=nullptr; float distance=FlagRules::PickupRadius;
        // A bot standing on the pedestal must not beat a nearby human to the flag.
        if (!player->IsDead() && player->GetTeamID()==team &&
            !(player==flag.previousCarrier && flag.pickupLock>0) &&
            Range(player->GetPosition(),flag.position)<=FlagRules::PickupRadius) closest=player;
        for (Character* fighter:fighters) {
            if (closest==player) break;
            if (fighter->IsDead() || (fighter==flag.previousCarrier && flag.pickupLock>0)) continue;
            if (fighter->GetTeamID()!=team && !flag.dropped) continue;
            float d=Range(fighter->GetPosition(),flag.position);
            if (d<distance) { distance=d; closest=fighter; }
        }
        if (!closest) continue;
        if (closest->GetTeamID()!=team) { ResetFlag(team); AnnounceFlag(std::string(team==1?"GR":"BL")+" FLAG INTERCEPTED / RETURNED",3-team); }
        else {
            flag.carrier=closest; flag.dropped=false; flag.returnTimer=0;
            AnnounceFlag(closest->GetName()+" CARRIES THE "+(team==1?"GR":"BL")+" FLAG",team);
        }
    }
}
PacketFlagState GameplayScreen::MakeFlagState() const {
    auto& nm=NetworkManager::GetInstance(); PacketFlagState packet{};
    packet.header={PacketType::FLAG_STATE,nm.GetLocalPlayerID()};
    for (int team=1;team<=2;++team) {
        const auto& flag=flags[team]; auto& wire=packet.flags[team-1];
        wire.position=flag.position; wire.carrierID=UINT32_MAX;
        if (flag.carrier==player) wire.carrierID=nm.GetLocalPlayerID();
        else for (auto* remote:remotePlayers) if (flag.carrier==remote) wire.carrierID=remote->peerID;
        wire.dropped=flag.dropped; wire.returnTimer=flag.returnTimer; wire.respawnTimer=flag.respawnTimer;
    }
    packet.grScore=nm.teamScores[1]; packet.blScore=nm.teamScores[2];
    packet.announcementSerial=flagAnnouncementSerial; packet.announcementTeam=flagAnnouncementTeam;
    std::strncpy(packet.announcement,flagAnnouncement.c_str(),sizeof(packet.announcement)-1);
    return packet;
}
void GameplayScreen::ApplyFlagState(const PacketFlagState& packet) {
    auto& nm=NetworkManager::GetInstance();
    for (int team=1;team<=2;++team) {
        const auto& wire=packet.flags[team-1]; auto& flag=flags[team];
        flag.position=wire.position; flag.dropped=wire.dropped;
        flag.returnTimer=wire.returnTimer; flag.respawnTimer=wire.respawnTimer;
        flag.carrier=nullptr;
        if (wire.carrierID!=UINT32_MAX) {
            if (wire.carrierID==nm.GetLocalPlayerID()) flag.carrier=player;
            else flag.carrier=FindByPeerID(wire.carrierID);
        }
    }
    nm.teamScores[1]=packet.grScore; nm.teamScores[2]=packet.blScore;
    if (packet.announcementSerial!=flagAnnouncementSerial) {
        flagAnnouncementSerial=packet.announcementSerial; flagAnnouncementTeam=packet.announcementTeam;
        flagAnnouncement.assign(packet.announcement,strnlen(packet.announcement,sizeof(packet.announcement)));
        flagAnnouncementTimer=2.8f;
    }
}
void GameplayScreen::UpdateOnlineFlags(float dt) {
    if (!IsFlagDeathmatch()) return;
    auto& nm=NetworkManager::GetInstance();
    bool interact=Settings::Pressed(Settings::Interact);
    if (!nm.IsHost()) {
        flagAnnouncementTimer=std::max(0.0f,flagAnnouncementTimer-dt);
        if (interact) { PacketHeader request{PacketType::FLAG_INTERACT,nm.GetLocalPlayerID()}; nm.SendPacket(&request,sizeof(request),true); }
        return;
    }
    for (uint32_t id:flagInteractRequests) if (auto* remote=FindByPeerID(id)) UpdateFlags(0,true,remote);
    flagInteractRequests.clear();
    UpdateFlags(dt,interact);
    flagStateTimer-=dt;
    if (flagStateTimer<=0) { auto packet=MakeFlagState(); nm.SendPacket(&packet,sizeof(packet),true); flagStateTimer=0.1f; }
}
void GameplayScreen::DrawWavingFlag(Vector2 feet,int team,float size,float phase,int direction,float poleHeight) const {
    Color color=TeamColor(team); float height=poleHeight*size,width=76*size;
    Vector2 top{feet.x,feet.y-height};
    DrawLineEx({feet.x+2*size,feet.y},{top.x+2*size,top.y},5*size,{10,18,28,255});
    DrawLineEx(feet,top,3*size,{218,229,232,255});
    DrawCircleV({top.x,top.y-3*size},4*size,UiTheme::AccentGold());
    // Cloth is a strip of animated quads, with a notched free edge.
    for (int segment=0;segment<10;++segment) {
        float x0=segment/10.0f,x1=(segment+1)/10.0f;
        auto edge=[&](float x,bool bottom) {
            float wave=std::sin(worldTime*4.8f+phase-x*4)*10*size*x;
            float notch=bottom && x>0.8f?(x-0.8f)*48*size:0;
            return Vector2{top.x+direction*x*width,top.y+12*size+wave+(bottom?47*size-notch:0)};
        };
        Vector2 vertices[]={edge(x0,false),edge(x0,true),edge(x1,false),edge(x1,true)};
        if (direction<0) { std::swap(vertices[0],vertices[1]); std::swap(vertices[2],vertices[3]); }
        float shade=0.75f+0.25f*std::sin(worldTime*4.8f+phase-x0*4);
        Color cloth{(unsigned char)(color.r*shade),(unsigned char)(color.g*shade),(unsigned char)(color.b*shade),255};
        DrawTriangleStrip(vertices,4,cloth);
        DrawLineEx(edge(x0,false),edge(x1,false),1,Fade(WHITE,0.4f));
    }
    Vector2 badge{top.x+direction*27*size,top.y+35*size+std::sin(worldTime*4.8f+phase-1.3f)*3*size};
    DrawPoly(badge,4,10*size,0,Fade(WHITE,0.95f));
    DrawPoly(badge,4,5*size,0,color);
}
void GameplayScreen::DrawBases() {
    if (!HasTeamBases() && !HasDeathmatchBases()) return;
    int count=HasTeamBases()?2:4;
    for (int i=0;i<count;++i) {
        int team=HasTeamBases()?i+1:0;
        Vector2 p=GroundPoint(team?teamBasePositions[team]:deathmatchBases[i]);
        Color accent=team?TeamColor(team):UiTheme::AccentGold();
        float width=team?164.0f:100.0f,height=team?102.0f:65.0f;
        DrawEllipse((int)p.x,(int)p.y+13,width+15,height+10,Fade(BLACK,0.3f));
        Vector2 outline[9];
        for (int v=0;v<9;++v) { float a=PI/8-v*PI/4; outline[v]={p.x+std::cos(a)*width,p.y+std::sin(a)*height}; }
        DrawTriangleFan(outline,8,{24,36,48,255});
        DrawLineStrip(outline,9,accent);
        for (int stripe=0;stripe<5;++stripe) {
            float x=p.x+(stripe-2)*width*0.3f;
            DrawLineEx({x-15,p.y+height*0.65f},{x+10,p.y+height*0.65f},4,Fade(accent,0.8f));
        }
        DrawEllipseLines((int)p.x,(int)p.y,width*0.48f,height*0.48f,Fade(accent,0.6f));
        DrawEllipseLines((int)p.x,(int)p.y,width*0.28f,height*0.28f,Fade(accent,0.25f+0.12f*std::sin(worldTime*2)));
        // Raised back wall, supply modules, and an antenna make the outpost a world structure.
        Vector2 left{p.x-width*0.70f,p.y-height*0.65f},right{p.x+width*0.70f,p.y-height*0.65f};
        DrawRectangle((int)left.x,(int)left.y-42,(int)(right.x-left.x),42,{19,30,42,255});
        DrawRectangle((int)left.x,(int)left.y-42,(int)(right.x-left.x),5,accent);
        DrawLineEx({left.x,left.y-42},{left.x-15,left.y-31},3,accent);
        DrawLineEx({right.x,right.y-42},{right.x+15,right.y-31},3,accent);
        for (int side : {-1,1}) {
            Rectangle crate{p.x+side*width*0.75f-17,p.y-22,34,28};
            DrawRectangleRec(crate,{39,53,63,255}); DrawRectangleLinesEx(crate,2,Fade(accent,0.7f));
            DrawLineEx({crate.x+6,crate.y+7},{crate.x+28,crate.y+7},3,accent);
        }
        Vector2 antenna{right.x-8,right.y-42};
        DrawLineEx(antenna,{antenna.x,antenna.y-56},3,{190,204,215,255});
        DrawCircleV({antenna.x,antenna.y-59},4,accent);
        const char* title=team==1?"GLOBAL RISK":team==2?"BLACK LIST":TextFormat("OUTPOST %02d",i+1);
        int size=team?18:12;
        DrawText(title,(int)p.x-MeasureText(title,size)/2,(int)left.y-32,size,accent);
        if (team && IsFlagDeathmatch() &&
            flagAnnouncementTimer>0 && team==3-flagAnnouncementTeam && flagAnnouncement.find("CAPTURED")!=std::string::npos) {
            float age=2.8f-flagAnnouncementTimer;
            for (int ring=0;ring<3;++ring) {
                float phase=std::fmod(age*0.65f+ring/3.0f,1.0f);
                DrawEllipseLines((int)p.x,(int)p.y,35+phase*150,18+phase*80,Fade(TeamColor(flagAnnouncementTeam),1-phase));
            }
            for (int spark=0;spark<16;++spark) {
                float angle=spark*2*PI/16,travel=std::fmod(age*80+spark*11.0f,160.0f);
                Vector2 light{p.x+std::cos(angle)*travel,p.y+std::sin(angle)*travel*0.45f-age*18};
                DrawPoly(light,4,3,45,Fade(UiTheme::AccentGold(),1-travel/180));
            }
        }
        const char* caption=team && IsFlagDeathmatch()?"FLAG BASE / RESPAWN":"RESPAWN BASE";
        DrawText(caption,(int)p.x-MeasureText(caption,12)/2,(int)(p.y+height+20),12,accent);
    }
}
void GameplayScreen::DrawFlags() {
    for (int team=1;team<=2;++team) {
        const auto& flag=flags[team];
        if (flag.respawnTimer>0) {
            Vector2 p=GroundPoint(teamBasePositions[team]);
            DrawText(TextFormat("FLAG IN %.0f",std::ceil(flag.respawnTimer)),(int)p.x-45,(int)p.y-80,16,TeamColor(team));
            continue;
        }
        Vector2 anchor=flag.carrier?flag.carrier->GetPosition():flag.position;
        Vector2 feet=GroundPoint(anchor);
        if (flag.carrier) {
            feet.x-=flag.carrier->GetFaceDirection()*48;
            feet.y-=flag.carrier->GetJumpHeight()+5;
            DrawEllipse((int)GroundPoint(anchor).x,(int)GroundPoint(anchor).y,38,16,Fade(TeamColor(team),0.23f));
            for (int trail=1;trail<5;++trail) DrawCircleV({feet.x-flag.carrier->GetFaceDirection()*trail*12.0f,feet.y-45+std::sin(worldTime*6-trail)*5},3,Fade(TeamColor(team),0.5f-trail*0.09f));
        } else {
            DrawEllipseLines((int)feet.x,(int)feet.y,FlagRules::PickupRadius,FlagRules::PickupRadius*0.45f,Fade(TeamColor(team),0.7f));
            const char* hint=team==player->GetTeamID()?"YOUR FLAG / WALK INTO RING":"ENEMY FLAG / DEFEND YOUR BASE";
            DrawText(hint,(int)feet.x-MeasureText(hint,14)/2,(int)feet.y+40,14,TeamColor(team));
        }
        DrawWavingFlag(feet,team,flag.carrier?0.8f:1,team*1.5f,flag.carrier?-flag.carrier->GetFaceDirection():1,flag.carrier?220.0f:136.0f);
        if (flag.dropped) DrawText(TextFormat("RETURN %.0f",std::ceil(flag.returnTimer)),(int)feet.x-35,(int)feet.y+20,14,TeamColor(team));
    }
}
