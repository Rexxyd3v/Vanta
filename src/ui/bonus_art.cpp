#include "../entities/bonus_catalog.h"
#include <cmath>
namespace {
float Noise(int i){return std::fmod(std::abs(std::sin(i*127.1f+311.7f)*43758.5453f),1.0f);}
Vector2 At(Vector2 p,float angle,float distance,float flatten=1){return {p.x+std::cos(angle)*distance,p.y+std::sin(angle)*distance*flatten};}
void Shard(Vector2 p,float angle,float length,float width,float alpha) {
    Vector2 tip=At(p,angle,length),left=At(p,angle+PI*.5f,width),right=At(p,angle-PI*.5f,width);
    DrawTriangle(p,left,tip,Fade({57,137,183,255},alpha));DrawTriangle(p,tip,right,Fade({185,242,255,255},alpha));
    DrawLineEx(left,tip,1.2f,Fade({227,253,255,255},alpha));
}
}
void DrawBonusArt(int kind,Rectangle area,float rotation,float alpha,bool mirror) {
    Color accent=Fade(Bonus::Tint(kind),alpha),light=Fade({203,214,218,255},alpha),white=Fade({246,246,224,255},alpha);
    Color dark=Fade({22,29,35,255},alpha),metal=Fade({69,82,89,255},alpha),steel=Fade({111,128,135,255},alpha);
    float unit=std::min(area.width/104,area.height/60);Vector2 center{area.x+area.width*.5f,area.y+area.height*.5f};
    auto p=[&](float x,float y){if(mirror)x=-x;float a=rotation*DEG2RAD;return Vector2{center.x+(x*std::cos(a)-y*std::sin(a))*unit,center.y+(x*std::sin(a)+y*std::cos(a))*unit};};
    auto line=[&](float x,float y,float xx,float yy,float width,Color c){DrawLineEx(p(x,y),p(xx,yy),width*unit,c);};
    auto disk=[&](float x,float y,float r,Color c){DrawCircleV(p(x,y),r*unit,c);};
    auto panel=[&](float x,float y,float w,float h,Color c){line(x,y+h*.5f,x+w,y+h*.5f,h,c);};
    if(kind==0) { // Pressure tank, ribbed hose, insulated nozzle and pilot burner.
        panel(-42,-9,23,24,dark);panel(-40,-7,19,19,metal);panel(-39,-7,17,6,accent);
        panel(-36,-14,10,5,dark);panel(-32,-17,3,5,steel);
        line(-39,16,-24,22,5,dark);line(-24,22,0,18,5,dark);line(0,18,10,3,5,dark);
        line(-36,15,-23,19,2,steel);line(-23,19,-2,15,2,steel);
        panel(-17,-8,44,16,dark);panel(-14,-7,31,10,light);panel(-12,-7,28,3,white);
        line(-8,7,-12,23,8,dark);line(-11,11,-13,22,3,metal);
        panel(12,-5,35,10,metal);panel(38,-8,9,16,dark);panel(40,-7,5,14,steel);
        for(int i=0;i<5;++i)line(17+i*4,-4,17+i*4,4,2,dark);
        line(23,9,46,9,3,dark);disk(48,8,2,accent);disk(-28,0,4,dark);disk(-28,0,2,accent);
        line(-10,-3,8,-3,2,accent);
    }else if(kind==1) { // Short rifled barrel and a six-round revolving cylinder.
        panel(-47,-5,22,10,dark);line(-45,-5,-45,12,5,metal);line(-43,8,-29,-3,3,steel);
        panel(-28,-10,48,18,dark);panel(-26,-9,38,7,light);panel(-24,-9,33,2,white);
        disk(0,6,13,dark);disk(0,6,10,metal);disk(0,6,5,steel);
        for(int i=0;i<6;++i){float a=i*PI/3;disk(std::cos(a)*7,6+std::sin(a)*7,2,accent);}
        panel(14,-5,33,12,dark);panel(19,-4,24,5,steel);panel(43,-7,5,15,metal);
        line(-21,5,-25,24,8,dark);line(-24,12,-26,22,3,metal);
        line(-13,-13,25,-13,3,dark);line(22,-18,22,-13,3,accent);
        for(int i=0;i<3;++i)line(27+i*5,-3,27+i*5,5,2,dark);
    }else if(kind==2) { // Long launch tube, flared rear and visible rocket warhead.
        panel(-47,-10,83,19,dark);panel(-43,-8,75,14,Fade({104,118,76,255},alpha));
        panel(-43,-8,75,4,Fade({156,168,116,255},alpha));panel(-48,-13,7,25,metal);
        panel(-23,-11,5,21,dark);panel(19,-11,5,21,dark);panel(31,-6,14,11,steel);
        DrawTriangle(p(49,-1),p(36,-11),p(36,9),accent);line(37,-6,44,-2,2,white);
        line(-7,8,-12,25,7,dark);line(9,8,6,23,6,dark);line(-10,12,-13,24,2,steel);
        line(-12,-14,8,-14,3,dark);line(2,-21,2,-13,3,light);line(2,-21,11,-21,2,accent);
        line(-34,10,-20,16,2,steel);
    }else {
        // Distinct grenade casings with an inset payload, metal cap and lever.
        DrawPoly(p(0,4),6,22*unit,rotation+30,dark);DrawPoly(p(0,4),6,18*unit,rotation+30,metal);
        DrawPoly(p(0,4),6,14*unit,rotation+30,accent);line(-11,-9,-11,14,3,light);line(10,-8,10,14,3,dark);
        panel(-9,-21,18,7,dark);panel(-6,-21,11,3,steel);line(5,-23,17,-16,4,light);line(17,-16,18,0,3,steel);
        DrawCircleLines((int)p(-11,-23).x,(int)p(-11,-23).y,5*unit,light);
        if(kind==3)for(int i=0;i<3;++i){float a=i*PI/3;line(-std::cos(a)*10,4-std::sin(a)*10,std::cos(a)*10,4+std::sin(a)*10,2,white);}
        if(kind==4){line(4,-7,-5,5,4,white);line(-5,5,5,3,3,white);line(5,3,-3,15,3,white);}
        if(kind==5){panel(-8,0,16,13,dark);DrawTriangle(p(-6,12),p(6,12),p(2,-5),white);DrawTriangle(p(-2,12),p(4,12),p(1,3),accent);}
        disk(-6,-12,2,white);disk(6,20,2,dark);
    }
}
// Curled, tapering ribbons give flames a direction and a bright core instead of glowing balls.
void DrawBonusFlame(Vector2 origin,Vector2 direction,float length,float width,float phase,float alpha) {
    float d=std::hypot(direction.x,direction.y);if(d<.001f)return;direction.x/=d;direction.y/=d;
    Vector2 side{-direction.y,direction.x};
    const Color colors[]={{191,49,23,255},{250,92,20,255},{255,177,42,255},{255,239,164,255}};
    for(int layer=0;layer<4;++layer){Vector2 ribbon[24];float scale=1-layer*.21f;
        for(int i=0;i<12;++i){float t=i/11.0f;float curl=std::sin(t*7-phase*12)*width*.22f*t+std::sin(t*13+phase*7)*width*.12f*t;
            float w=width*scale*std::pow(1-t,.65f)*(.15f+.85f*std::sin(std::min(1.0f,t*4)*PI*.5f))*(.78f+.17f*std::sin(t*18-phase*16));
            Vector2 c{origin.x+direction.x*length*t+side.x*curl,origin.y+direction.y*length*t+side.y*curl};
            ribbon[i*2]={c.x-side.x*w,c.y-side.y*w};ribbon[i*2+1]={c.x+side.x*w,c.y+side.y*w};}
        DrawTriangleStrip(ribbon,24,Fade(colors[layer],alpha*(layer==0?.55f:.86f)));
    }
    for(int i=0;i<3;++i){float t=std::fmod(phase*1.8f+i*.31f,1.0f);Vector2 ember{origin.x+direction.x*length*t+side.x*std::sin(i*9+phase*5)*width,origin.y+direction.y*length*t+side.y*std::sin(i*9+phase*5)*width};
        DrawLineEx(ember,{ember.x-direction.x*5,ember.y-direction.y*5},1.5f,Fade({255,218,117,255},alpha*(1-t)));}
}
void DrawBonusIce(Vector2 center,float remaining) {
    float fade=std::min(1.0f,remaining*5);
    DrawEllipse((int)center.x,(int)(center.y+55),43,14,Fade({67,182,224,255},fade*.25f));
    Vector2 shell[]={{center.x-37,center.y+42},{center.x-40,center.y-27},{center.x-22,center.y-52},{center.x+15,center.y-55},{center.x+38,center.y-29},{center.x+37,center.y+43},{center.x+11,center.y+57},{center.x-18,center.y+54}};
    for(int i=0;i<8;++i){Vector2 a=shell[i],b=shell[(i+1)%8];DrawTriangle(center,b,a,Fade(i%2?Color{96,198,232,255}:Color{187,239,250,255},fade*(i%2?.18f:.29f)));DrawLineEx(a,b,1.5f,Fade({195,248,255,255},fade*.85f));}
    DrawLineEx(shell[2],{center.x+4,center.y-5},1.6f,Fade(WHITE,fade*.8f));
    DrawLineEx({center.x+4,center.y-5},shell[5],1.4f,Fade(WHITE,fade*.75f));
    DrawLineEx({center.x+4,center.y-5},{center.x-10,center.y+18},1.2f,Fade(WHITE,fade*.7f));
    DrawLineEx({center.x-10,center.y+18},shell[7],1.2f,Fade(WHITE,fade*.6f));
    for(int i=0;i<7;++i){Vector2 p{center.x-36+i*12,center.y+46+Noise(i)*8};Shard(p,-PI*.5f+(Noise(i+11)-.5f)*.8f,12+Noise(i+18)*17,3+Noise(i+19)*3,fade*.85f);}
}
void DrawBonusBurst(int kind,Vector2 center,float age,bool detailed) {
    if(kind==3){float grow=1-std::exp(-age*12),fade=std::clamp((1.15f-age)*1.8f,0.0f,1.0f);
        DrawCircleGradient(center,280*grow,Fade({116,218,245,255},fade*.16f),BLANK);
        for(int i=0;i<(detailed?32:16);++i){float a=i*2.39996f,r=(40+Noise(i+8)*220)*grow;Vector2 p=At(center,a,r);
            DrawLineEx(At(center,a,r*.78f),p,1.3f,Fade({171,236,253,255},fade*.3f));
            Vector2 branch=At(p,a+.6f,13+Noise(i+7)*18);DrawLineEx(p,branch,1,Fade(WHITE,fade*.6f));
            Shard(p,a-PI*.5f,(9+Noise(i+14)*27)*grow,(3+Noise(i+44)*5)*grow,fade);
            if(age<.35f)DrawLineEx(p,At(p,a,18),2,Fade(WHITE,fade*(1-age/.35f)));
        }
        if(age<.18f)DrawCircleGradient(center,65+age*200,Fade({226,251,255,255},(1-age/.18f)*.7f),BLANK);
    }else if(kind==5){float fade=std::clamp((5-age)*2,0.0f,1.0f),grow=std::min(1.0f,age*5);
        for(int i=0;i<17;++i){Vector2 p=At(center,i*2.39996f,std::sqrt((i+.5f)/17)*140*grow,.8f);DrawCircleGradient(p,35+Noise(i+7)*20,Fade({36,25,23,255},fade*.26f),BLANK);}
        DrawCircleGradient(center,180*grow,Fade({255,104,24,255},fade*.2f),BLANK);
        int count=detailed?35:17;
        // Golden-angle positions fill the area without a visible grid; stable roots, moving tips.
        for(int i=0;i<count;++i){float r=std::sqrt((i+.5f)/count)*157*grow,a=i*2.39996f;Vector2 root=At(center,a,r,.8f);
            float phase=age+i*.37f,height=(28+Noise(i+29)*48)*(.8f+.2f*std::sin(age*13+i))*grow;
            if(detailed && i%3==0){Vector2 smoke{root.x+std::sin(phase*2)*13,root.y-height-14-std::fmod(phase*.5f,1.0f)*32};DrawCircleGradient(smoke,20,Fade({64,60,59,255},fade*.24f),BLANK);}
            DrawBonusFlame(root,{.18f*std::sin(phase*3),-1},height,8+Noise(i+10)*8,phase,fade*.9f);
        }
    }
}
