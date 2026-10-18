#include "main_level.h"
#include "../audio/combat_audio.h"
#include "../ui/game_settings.h"
#include <algorithm>
#include <cmath>

void GameplayScreen::NoteMedalKill(uint64_t killer,uint64_t victim,uint64_t local,bool enemies){
    if(matchEndTimer>0)return;
    for(auto kind:medalTracker.Kill(killer,victim,local,medalClock,enemies)){
        if(kind==Medals::TripleKill){auto doubleKill=std::find_if(medalQueue.begin(),medalQueue.end(),[](const MedalToast& t){return t.kind==Medals::DoubleKill;});
            if(doubleKill!=medalQueue.end()){doubleKill->kind=kind;doubleKill->age=0;CombatAudio::Play(CombatAudio::Medal,player->GetPosition(),.65f);continue;}}
        if(medalQueue.size()>=6)medalQueue.erase(medalQueue.begin()+1);
        bool first=medalQueue.empty();medalQueue.push_back({kind,0});
        if(first)CombatAudio::Play(CombatAudio::Medal,player->GetPosition(),.65f);
    }
}
void GameplayScreen::UpdateMedals(float dt){
    if(matchEndTimer>0)return;
    medalClock+=dt;
    if(medalQueue.empty())return;
    medalQueue.front().age+=dt;
    if(medalQueue.front().age>=2.3f){medalQueue.erase(medalQueue.begin());
        if(!medalQueue.empty())CombatAudio::Play(CombatAudio::Medal,player->GetPosition(),.65f);}
}
void GameplayScreen::DrawMedals(){
    if(medalQueue.empty())return;const auto& toast=medalQueue.front();float age=toast.age;
    float alpha=std::min(std::clamp(age/.16f,0.0f,1.0f),std::clamp((2.3f-age)/.35f,0.0f,1.0f));
    float settle=Settings::Get().effects?1-std::pow(1-std::clamp(age/.3f,0.0f,1.0f),3):1;
    float x=650,y=205+(1-settle)*-16;Color gold{242,200,117,255},white{241,245,240,255};
    if(Settings::Get().effects){DrawCircleGradient({x,y},70,Fade(gold,alpha*.13f),BLANK);
        if(age<.5f)DrawRing({x,y},36+age*55,37+age*55,0,360,48,Fade(gold,(1-age/.5f)*.6f));}
    // An open metal crest, wings and distinct internal marks instead of a HUD box.
    Vector2 crest[]={{x,y-28},{x+25,y-13},{x+21,y+16},{x,y+30},{x-21,y+16},{x-25,y-13}};
    for(int i=0;i<6;++i)DrawLineEx(crest[i],crest[(i+1)%6],2,Fade(gold,alpha));
    for(int side:{-1,1})for(int i=0;i<3;++i){float start=x+side*(34+i*7.0f);DrawLineEx({start,y+12-i*8.0f},{start+side*(24-i*5.0f),y+5-i*8.0f},2,Fade(gold,alpha*(1-i*.2f)));}
    if(toast.kind==Medals::DoubleKill || toast.kind==Medals::TripleKill){int n=toast.kind==Medals::DoubleKill?2:3;
        for(int i=0;i<n;++i)DrawLineEx({x+(i-(n-1)*.5f)*9-3,y+10},{x+(i-(n-1)*.5f)*9+3,y-10},3,Fade(white,alpha));
    }else if(toast.kind==Medals::Revenge){DrawRing({x,y},10,12,45,315,24,Fade(white,alpha));DrawTriangle({x+11,y-11},{x+3,y-12},{x+11,y-3},Fade(white,alpha));}
    else{DrawLineEx({x-9,y-10},{x+9,y+10},3,Fade(white,alpha));DrawLineEx({x+9,y-10},{x-9,y+10},3,Fade(white,alpha));}
    const char* title=Medals::Name(toast.kind);DrawText(title,(int)x-MeasureText(title,22)/2,(int)y+42,22,Fade(gold,alpha));
    const char* detail=Medals::Detail(toast.kind);DrawText(detail,(int)x-MeasureText(detail,9)/2,(int)y+71,9,Fade(white,alpha*.8f));
}
