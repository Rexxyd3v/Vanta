#include "main_level.h"
#include "../network/network_manager.h"
#include "../ui/game_settings.h"
#include "../ui/ui_widgets.h"
#include "../ui/ui_theme.h"
#include "../ui/weapon_skin_catalog.h"
#include "../ui/equipment_art.h"
#include "../entities/bag_catalog.h"
#include "../rules/match_rules.h"
#include "../ui/match_presentation.h"
#include <algorithm>
#include <cmath>
namespace {
const Color Ink{7,15,23,255},White{239,244,246,255},Muted{151,177,190,255},Gold{242,200,117,255};
std::string Fit(std::string value,int size,int width){for(auto& c:value)if((unsigned char)c<32)c=' ';if(MeasureText(value.c_str(),size)<=width)return value;while(!value.empty() && MeasureText((value+"...").c_str(),size)>width)value.pop_back();return value+"...";}
const char* DamageName(int weapon){if(weapon<WeaponCount)return WeaponName(weapon);if(weapon>=Bonus::FirstDamage)return Bonus::Name(weapon-Bonus::FirstDamage);return weapon==Bags::MeleeDamageId?"MELEE":weapon==WeaponCount+1?"POISON SMOKE":"FRAG GRENADE";}
void Art(int weapon,int skin,Rectangle area){
    if(weapon>=0 && weapon<WeaponCount){auto texture=LoadWeaponSlotTexture(weapon,skin);auto bounds=GetWeaponGeometry(texture,weapon).bounds;if(texture.id && bounds.width>0 && bounds.height>0){float fit=std::min(area.width/bounds.width,area.height/bounds.height);Rectangle dst{area.x+area.width*.5f,area.y+area.height*.5f,bounds.width*fit,bounds.height*fit};DrawWeaponSkinTexture(texture,bounds,dst,{dst.width*.5f,dst.height*.5f},0,weapon,skin);}}
    else if(weapon>=Bonus::FirstDamage)DrawBonusArt(weapon-Bonus::FirstDamage,area);
    else if(weapon==Bags::MeleeDamageId)DrawEquipmentArt(true,std::clamp(skin/4,0,1),skin%4,area);
    else DrawEquipmentArt(false,weapon==WeaponCount+1?1:0,0,area);
}
void Right(const char* text,int x,int y,int size,Color color){DrawText(text,x-MeasureText(text,size),y,size,color);}
}
void GameplayScreen::DrawDeathRecap(){
    DrawRectangleGradientH(30,255,520,279,Fade(Ink,.95f),Fade(Ink,.65f));
    DrawLineEx({50,272},{110,272},3,{255,119,100,255});
    DrawText("ELIMINATED BY",50,288,11,Muted);
    std::string killer=Fit(deathKiller.empty()?"UNKNOWN":deathKiller,26,325);DrawText(killer.c_str(),50,308,26,White);
    Art(deathWeapon,deathSkin,{389,284,132,51});DrawText(DamageName(deathWeapon),50,344,12,Gold);
    auto hits=lifeDamage;std::stable_sort(hits.begin(),hits.end(),[](const RecapHit& a,const RecapHit& b){return a.damage>b.damage;});
    float total=0;int count=0;for(const auto& h:hits){total+=h.damage;count+=h.hits;}
    DrawText(TextFormat("THIS LIFE / %.0f DAMAGE RECEIVED / %d HITS",total,count),50,373,11,Muted);
    for(size_t i=0;i<std::min<size_t>(3,hits.size());++i){const auto& h=hits[i];int y=400+(int)i*31;
        DrawText(Fit(h.attacker,13,175).c_str(),50,y,13,White);Art(h.weapon,h.skin,{239.0f,(float)y-3,83,24});
        Right(TextFormat("%.0f DMG / %d HIT%s",h.damage,h.hits,h.hits==1?"":"S"),520,y,12,Gold);
    }
    if(hits.empty())DrawText("No damage breakdown was recorded.",50,403,12,Muted);
    bool elimination=currentMode==GameMode::PRACTICE?Offline::RoundMode(offlineSetup.mode):NetworkManager::GetInstance().currentGameMode==OnlineGameMode::ELIMINATION;
    bool automatic=currentMode==GameMode::ONLINE || offlineSetup.mode!=Offline::Mode::Training;
    const char* respawn=elimination?"ONE LIFE / WAIT FOR THE NEXT ROUND":automatic?TextFormat("RESPAWN IN %.1f",std::max(0.0f,Rules::RespawnSeconds(IsFlagDeathmatch())-deadFor)):TextFormat("ENTER / RESPAWN AVAILABLE IN %.1f",std::max(0.0f,Rules::RespawnSeconds(false)-deadFor));
    DrawText(respawn,50,510,11,Muted);
    if(spectateTarget){const char* label=TextFormat("SPECTATING / %s",Fit(spectateTarget->GetName(),16,310).c_str());DrawText(label,650-MeasureText(label,16)/2,153,16,Gold);DrawText("LEFT / RIGHT  SWITCH PLAYER",650-MeasureText("LEFT / RIGHT  SWITCH PLAYER",10)/2,177,10,Muted);}
    else {const char* label=HasTeamBases()?"NO LIVING TEAMMATES TO SPECTATE":"NO LIVING PLAYERS TO SPECTATE";DrawText(label,650-MeasureText(label,12)/2,154,12,Muted);}
}
void GameplayScreen::DrawMatchResults(){
    auto& nm=NetworkManager::GetInstance();auto actors=RankedActors();
    auto kills=[&](Character* a){return a==player && currentMode==GameMode::ONLINE?nm.localKills:a->GetKills();};
    auto deaths=[&](Character* a){return a==player && currentMode==GameMode::ONLINE?nm.localDeaths:a->GetDeaths();};
    if(resultsAge>=MatchPresentation::SquadSeconds && resultsAge<MatchPresentation::RevealSeconds){DrawVictorySquad();return;}
    if(resultsAge<MatchPresentation::SquadSeconds){
        float age=resultsAge,progress=MatchPresentation::Progress(age);
        float settle=std::clamp(age/.65f,0.0f,1.0f),wash=std::clamp((age-.4f)/1.2f,0.0f,1.0f);
        DrawRectangle(0,0,VIRTUAL_WIDTH,VIRTUAL_HEIGHT,Fade(Ink,.24f+.65f*wash));
        float bars=22*settle;DrawRectangle(0,0,VIRTUAL_WIDTH,(int)bars,Fade(Ink,.95f));DrawRectangle(0,VIRTUAL_HEIGHT-(int)bars,VIRTUAL_WIDTH,(int)bars,Fade(Ink,.95f));
        const char* eyebrow="OPERATION ENDED";DrawText(eyebrow,650-MeasureText(eyebrow,12)/2,215,12,Gold);
        std::string outcome=currentMode==GameMode::PRACTICE?offlineResult:nm.currentGameMode==OnlineGameMode::FREE_FOR_ALL?
            (matchWinner<0?"DRAW":nm.FindPlayer((uint32_t)matchWinner)?nm.FindPlayer((uint32_t)matchWinner)->username+" WINS":"MATCH ENDED"):
            (matchWinner<0?"DRAW":std::string(matchWinner==1?"GLOBAL RISK":"BLACK LIST")+" WINS");
        outcome=Fit(outcome.empty()?"BATTLE CONCLUDED":outcome,32,1050);
        int titleY=253+(int)((1-settle)*15);DrawText(outcome.c_str(),650-MeasureText(outcome.c_str(),32)/2,titleY,32,White);
        DrawLineEx({590,310},{710,310},2,Fade(Gold,settle));
        if(age>.55f){
            const char* stage=progress<.3f?"CONFIRMING FINAL SCORE":progress<.72f?"TALLYING COMBAT DATA":"PREPARING MATCH REPORT";
            DrawText(stage,650-MeasureText(stage,12)/2,374,12,Muted);
            DrawLineEx({450,411},{850,411},2,Fade(Muted,.23f));
            float eased=progress*progress*(3-2*progress);DrawLineEx({450,411},{450+400*eased,411},2,Gold);
            for(int i=0;i<3;++i){float pulse=.25f+.65f*(.5f+.5f*std::sin(age*7-i*1.6f));DrawCircleV({638+i*12.0f,443},2,Fade(Gold,pulse));}
        }
        return;
    }
    DrawRectangle(0,0,VIRTUAL_WIDTH,VIRTUAL_HEIGHT,Fade(Ink,.96f));
    DrawRectangleGradientH(0,0,1300,680,Fade({31,57,68,255},.65f),BLANK);
    DrawText("VANTA / AFTER ACTION REPORT",70,34,12,Gold);
    DrawText("MATCH COMPLETE",70,61,35,White);
    std::string result=currentMode==GameMode::PRACTICE?offlineResult:nm.currentGameMode==OnlineGameMode::FREE_FOR_ALL?
        (matchWinner<0?"DRAW":nm.FindPlayer((uint32_t)matchWinner)?nm.FindPlayer((uint32_t)matchWinner)->username+" WINS":"MATCH OVER"):
        (matchWinner<0?"DRAW":std::string(matchWinner==1?"GLOBAL RISK":"BLACK LIST")+" WINS");
    Right(Fit(result,18,510).c_str(),1230,74,18,Gold);
    DrawLineEx({70,116},{1230,116},1,Fade(Muted,.3f));
    Character* mvp=actors.front();const auto& best=mvp->GetCombatStats();
    DrawText("M V P",70,139,13,Gold);DrawText(Fit(mvp->GetName(),28,400).c_str(),70,162,28,White);
    DrawText(TextFormat("%d KILLS / %.0f DAMAGE / %d CAPTURES",kills(mvp),best.damage,best.captures),70,202,11,Muted);
    const auto& mine=player->GetCombatStats();
    const char* labels[]={"YOUR K / D","ACCURACY","DAMAGE DEALT","FLAG CAPTURES"};
    for(int i=0;i<4;++i){int x=565+i*172;DrawText(labels[i],x,145,10,Muted);
        const char* value=i==0?TextFormat("%d / %d",kills(player),deaths(player)):i==1?TextFormat("%.0f%%",mine.Accuracy()):i==2?TextFormat("%.0f",mine.damage):TextFormat("%d",mine.captures);
        DrawText(value,x,170,25,i==1?Color{112,223,241,255}:White);
    }
    DrawText("MATCH LEADERBOARD",70,249,12,Gold);
    DrawText("RANK / PLAYER",70,281,11,Muted);Right("KILLS",630,281,11,Muted);Right("DEATHS",750,281,11,Muted);Right("ACCURACY",910,281,11,Muted);Right("DAMAGE",1080,281,11,Muted);Right("CAPTURES",1230,281,11,Muted);
    resultsScroll=std::clamp(resultsScroll,0,std::max(0,(int)actors.size()-8));
    for(int row=0;row<8 && row+resultsScroll<(int)actors.size();++row){int rank=row+resultsScroll;auto* actor=actors[rank];const auto& stats=actor->GetCombatStats();int y=314+row*32;
        if(actor==player)DrawRectangleGradientH(60,y-6,1180,30,Fade(Gold,.15f),Fade(Gold,.02f));
        Color color=actor==player?Gold:White;DrawText(TextFormat("%02d",rank+1),70,y,13,Muted);DrawText(Fit(actor->GetName()+(actor==player?" / YOU":""),14,385).c_str(),117,y,14,color);
        Right(TextFormat("%d",kills(actor)),630,y,14,color);Right(TextFormat("%d",deaths(actor)),750,y,14,Muted);Right(TextFormat("%.0f%%",stats.Accuracy()),910,y,14,Muted);Right(TextFormat("%.0f",stats.damage),1080,y,14,color);Right(TextFormat("%d",stats.captures),1230,y,14,color);
    }
    if(actors.size()>8)Right(TextFormat("SCROLL / %d-%d OF %d",resultsScroll+1,std::min(resultsScroll+8,(int)actors.size()),(int)actors.size()),1230,584,10,Muted);
    bool authority=currentMode==GameMode::PRACTICE || nm.IsHost();Vector2 mouse=Ui::RemapMouseToVirtual();
    auto button=[&](Rectangle rect,const char* label){bool hover=CheckCollisionPointRec(mouse,rect);DrawRectangleRec(rect,Fade(Gold,hover?.24f:.09f));DrawLineEx({rect.x,rect.y+rect.height},{rect.x+rect.width,rect.y+rect.height},2,Fade(Gold,hover?1:.45f));DrawText(label,(int)rect.x+20,(int)rect.y+16,15,authority?Gold:Muted);};
    button({70,610,390,48},authority?"REMATCH / R":"WAITING FOR THE HOST");
    button({830,610,400,48},currentMode==GameMode::PRACTICE?"MATCH SETUP / ENTER":authority?"RETURN TO LOBBY / ENTER":"LEAVE MATCH / ENTER");
    float reveal=std::clamp((resultsAge-MatchPresentation::RevealSeconds)/.35f,0.0f,1.0f);
    if(reveal<1)DrawRectangle(0,0,VIRTUAL_WIDTH,VIRTUAL_HEIGHT,Fade(Ink,1-reveal));
}

std::vector<Character*> GameplayScreen::VictoryActors()const{
    auto ranked=RankedActors();auto& nm=NetworkManager::GetInstance();
    if(HasTeamBases()){
        ranked.erase(std::remove_if(ranked.begin(),ranked.end(),[&](Character* a){return matchWinner<1 || a->GetTeamID()!=matchWinner;}),ranked.end());
    }else{
        auto kills=[&](Character* a){return a==player && currentMode==GameMode::ONLINE?nm.localKills:a->GetKills();};
        // FFA is won by kills; MVP rating only resolves the order of tied scores.
        std::stable_sort(ranked.begin(),ranked.end(),[&](Character* a,Character* b){return kills(a)>kills(b);});
    }
    if(ranked.size()>3)ranked.resize(3);return ranked;
}
void GameplayScreen::DrawVictorySquad(){
    auto squad=VictoryActors();float age=resultsAge-MatchPresentation::SquadSeconds;
    bool teams=HasTeamBases();auto& nm=NetworkManager::GetInstance();
    auto killsOf=[&](Character* a){return a==player && currentMode==GameMode::ONLINE?nm.localKills:a->GetKills();};
    bool tied=!teams && (squad.size()>1 && killsOf(squad[0])==killsOf(squad[1]));
    DrawRectangle(0,0,1300,680,Ink);
    DrawRectangleGradientV(0,60,1300,565,Color{22,42,48,255},Ink);
    Color accent=matchWinner==2?Color{242,134,115,255}:Gold;
    DrawText("VANTA / THE FINAL LINEUP",70,36,12,Muted);
    const char* title=teams?(matchWinner<1?"HONORS EVEN":"VICTORIOUS SQUAD"):"TOP OPERATORS";
    DrawText(title,650-MeasureText(title,32)/2,83,32,White);
    const char* caption=teams?(matchWinner<1?"DRAW / NO WINNING SQUAD":matchWinner==1?"GLOBAL RISK":"BLACK LIST"):"FREE FOR ALL / FINAL STANDINGS";
    DrawText(caption,650-MeasureText(caption,12)/2,130,12,accent);
    DrawLineEx({590,162},{710,162},2,accent);
    if(squad.empty()){
        DrawText("BOTH SIDES HELD THEIR GROUND",650-MeasureText("BOTH SIDES HELD THEIR GROUND",20)/2,330,20,Muted);
    }else{
        // Supporting players arrive first; the highest ranked winner stands forward.
        const int order[]={2,1,0};
        for(int slot:order){if(slot>=(int)squad.size())continue;auto* actor=squad[slot];
            float fade=std::clamp((age-slot*.16f)/.55f,0.0f,1.0f);if(fade<=0)continue;
            float entrance=1-std::pow(1-fade,3);float x=slot==0?650.0f:slot==1?340.0f:960.0f;
            float y=466+(1-entrance)*35;
            float size=slot==0?.31f:.255f;
            if(Settings::Get().effects){DrawCircleGradient({x,365},slot==0?210:150,Fade(accent,slot==0?.12f:.055f),BLANK);
                for(int i=0;i<10;++i){float h=std::fmod(age*19+i*37.0f,240.0f);DrawCircleV({x-110+i*24.0f,440-h},1.1f,Fade(accent,.18f*(1-h/240)));}}
            DrawEllipse((int)x,466,slot==0?116:95,13,Fade(BLACK,.6f));
            DrawLineEx({x-70,470},{x+70,470},2,Fade(accent,slot==0?.8f:.35f));
            // Render the real character and skin, even if they died at the last instant.
            actor->DrawVictoryPose({x,y},size,slot==2?-1:1);
            const char* role=slot==0?(teams?"SQUAD MVP":tied?"01 / JOINT LEADER":"01 / CHAMPION"):slot==1?"02 / OPERATOR":"03 / OPERATOR";
            DrawText(role,(int)x-MeasureText(role,11)/2,493,11,slot==0?accent:Muted);
            std::string name=Fit(actor->GetName(),slot==0?25:19,260);DrawText(name.c_str(),(int)x-MeasureText(name.c_str(),slot==0?25:19)/2,516,slot==0?25:19,White);
            int kills=actor==player && currentMode==GameMode::ONLINE?nm.localKills:actor->GetKills();
            const char* stat=TextFormat("%d KILLS / %.0f DMG",kills,actor->GetCombatStats().damage);
            DrawText(stat,(int)x-MeasureText(stat,11)/2,553,11,Muted);
        }
    }
    DrawText("MATCH REPORT UP NEXT",650-MeasureText("MATCH REPORT UP NEXT",10)/2,623,10,Muted);
    float progress=std::clamp(age/(MatchPresentation::RevealSeconds-MatchPresentation::SquadSeconds),0.0f,1.0f);
    DrawLineEx({510,651},{790,651},1,Fade(Muted,.25f));DrawLineEx({510,651},{510+280*progress,651},2,accent);
    float fadeIn=std::clamp(age/.3f,0.0f,1.0f),fadeOut=std::clamp((resultsAge-(MatchPresentation::RevealSeconds-.25f))/.25f,0.0f,1.0f);
    float curtain=std::max(1-fadeIn,fadeOut);if(curtain>0)DrawRectangle(0,0,1300,680,Fade(Ink,curtain));
}
