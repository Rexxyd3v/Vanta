#pragma once
#include <raylib.h>
#include <algorithm>
#include <cmath>

// Shared cartoon blast for live combat and the menu's spectator battle.
namespace ExplosionFx {
inline float Noise(int seed) { return std::fmod(std::abs(std::sin(seed*127.1f+311.7f)*43758.5453f),1.0f); }
inline Vector2 Radial(Vector2 p,float angle,float radius,float flatten=1) {
    return {p.x+std::cos(angle)*radius,p.y+std::sin(angle)*radius*flatten};
}
inline void Scorch(Vector2 p,float age,float lifetime=4) {
    if(age<0 || age>lifetime) return;
    float fade=std::clamp((lifetime-age)/1.2f,0.0f,1.0f),spread=std::min(1.0f,age/.12f);
    DrawEllipse((int)p.x,(int)p.y,128*spread,83*spread,Fade({23,25,29,255},fade*.5f));
    for(int branch=0;branch<9;++branch) {
        float angle=branch*2*PI/9+Noise(branch)*.35f;
        Vector2 last=p;
        for(int segment=1;segment<=4;++segment) {
            Vector2 next=Radial(p,angle+(Noise(branch*5+segment)-.5f)*.38f,segment*(28+Noise(branch+23)*17)*spread,.7f);
            DrawLineEx(last,next,5-segment*.7f,Fade({18,20,24,255},fade));
            DrawLineEx({last.x,last.y+2},{next.x,next.y+2},1,Fade({155,158,163,255},fade*.35f));
            if(segment==2) DrawLineEx(next,Radial(next,angle+.7f,26*spread,.7f),1.5f,Fade({18,20,24,255},fade));
            last=next;
        }
    }
}
inline void Draw(Vector2 p,float age,bool detailed=true) {
    if(age<0 || age>1.8f) return;
    float fade=std::clamp((1.8f-age)/.9f,0.0f,1.0f);
    float expansion=1-std::exp(-age*7);
    // Rolling charcoal lobes, shaded silver on the side facing the initial flash.
    int lobes=detailed?14:8;
    for(int i=0;i<lobes;++i) {
        float angle=i*2.399963f,band=.30f+.62f*Noise(i+11);
        Vector2 cloud=Radial(p,angle,(25+expansion*138)*band,.68f);
        cloud.y-=age*(25+Noise(i+16)*35);
        float radius=(25+Noise(i+30)*29)*( .45f+expansion*.75f)+age*8;
        DrawCircleGradient(cloud,radius*1.25f,Fade({43,47,53,255},fade*.45f),Fade({63,67,75,255},0));
        DrawCircleGradient(cloud,radius,Fade({54,58,64,255},fade*.85f),Fade({54,58,64,255},0));
        Vector2 lit{cloud.x-radius*.08f,cloud.y-radius*.12f};
        DrawCircleGradient(lit,radius*.96f,Fade({108,112,119,255},fade*.82f),Fade({91,95,102,255},0));
        DrawCircleGradient({lit.x-radius*.1f,lit.y-radius*.12f},radius*.88f,
            Fade({157,161,168,255},fade*.55f),Fade({100,105,114,255},0));
    }
    if(age<.48f) {
        float pressure=1-age/.48f,radius=30+age*470;
        DrawRing(p,radius-2-pressure*4,radius,0,360,80,Fade({225,231,239,255},pressure*.8f));
    }
    // Short white-hot flash and warm light, followed by clean fading sparks.
    BeginBlendMode(BLEND_ADDITIVE);
    if(age<.28f) {
        float light=1-age/.28f;
        DrawCircleGradient(p,90+expansion*170,Fade({255,220,169,255},light*.6f),Fade(WHITE,0));
        DrawCircleGradient(p,75*light+15,Fade({255,249,231,255},light),Fade({255,221,175,255},0));
        if(age<.12f) {
            float flash=1-age/.12f;
            DrawCircleV(p,40*flash+8,Fade({255,253,242,255},flash));
            DrawLineEx({p.x-110*flash,p.y},{p.x+110*flash,p.y},3*flash,Fade(WHITE,flash*.8f));
        }
    }
    int sparks=detailed?26:10;
    float sparkFade=std::max(0.0f,1-age/.8f);
    for(int i=0;i<sparks;++i) {
        float angle=Noise(i+70)*2*PI,distance=(150+Noise(i+90)*310)*(1-std::exp(-age*2.8f));
        Vector2 tip=Radial(p,angle,distance,.75f);tip.y-=std::max(0.0f,age*70-age*age*85);
        DrawLineEx(Radial(tip,angle+PI,8+sparkFade*18,.75f),tip,1.5f+sparkFade,Fade({255,230,186,255},sparkFade));
        DrawCircleV(tip,1.5f,Fade(WHITE,sparkFade));
    }
    EndBlendMode();
    for(int i=0;i<(detailed?18:7);++i) {
        float angle=Noise(i+100)*2*PI;
        Vector2 ground=Radial(p,angle,(100+Noise(i+120)*240)*(1-std::exp(-age*2)),.7f);
        Vector2 shard=ground;shard.y-=std::max(0.0f,(100+Noise(i+140)*100)*age-160*age*age);
        DrawEllipse((int)ground.x,(int)ground.y,4*fade,2*fade,Fade(BLACK,fade*.3f));
        DrawPoly(shard,3,3+Noise(i+160)*5,i*37+age*250,Fade({43,46,52,255},fade));
        DrawLineEx(shard,{shard.x+3,shard.y-2},1,Fade({171,176,184,255},fade));
    }
}
}
