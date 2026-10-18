#include "weapon_reload.h"
#include "weapon_skin_catalog.h"
#include <algorithm>
#include <cmath>
namespace WeaponReload {
namespace {
float Ramp(float a,float b,float p){float t=std::clamp((p-a)/(b-a),0.0f,1.0f);return t*t*(3-2*t);}
Vector2 Mix(Vector2 a,Vector2 b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
Vector2 World(Vector2 point,const WeaponGeometry& g,const WeaponPose& pose,float scale,int facing){Vector2 n{-pose.direction.y,pose.direction.x};float x=(point.x-g.pivot.x)*scale,y=(point.y-g.pivot.y)*scale*facing;return {pose.hand.x+pose.direction.x*x+n.x*y,pose.hand.y+pose.direction.y*x+n.y*y};}
Vector2 Offset(const WeaponPose& pose,int facing,float x,float y){Vector2 n{-pose.direction.y,pose.direction.x};return {pose.direction.x*x+n.x*y*facing,pose.direction.y*x+n.y*y*facing};}
Vector2 Rotate(Vector2 p,Vector2 hinge,float angle){float a=angle*DEG2RAD,x=p.x-hinge.x,y=p.y-hinge.y;return {hinge.x+x*std::cos(a)-y*std::sin(a),hinge.y+x*std::sin(a)+y*std::cos(a)};}
Rectangle Component(const WeaponGeometry& g,int slot){
    const Rectangle parts[]={{.52f,.48f,.14f,.52f},{.64f,.35f,.21f,.40f},{.06f,.83f,.21f,.17f},{.47f,.44f,.18f,.56f},{.35f,.57f,.13f,.43f},{.49f,.43f,.18f,.57f},{.48f,0,.52f,1},{.37f,.55f,.13f,.45f}};
    Rectangle r=parts[std::clamp(slot,0,7)];return {g.bounds.x+r.x*g.bounds.width,g.bounds.y+r.y*g.bounds.height,r.width*g.bounds.width,r.height*g.bounds.height};
}
Vector2 Center(Rectangle r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
float PumpStroke(float pump){return pump>0?std::sin(std::clamp(1-pump/.32f,0.0f,1.0f)*PI):0;}
Vector2 Feed(const WeaponPose& pose,int facing,float p){float phase=std::fmod(std::clamp((p-.15f)/.7f,0.0f,.999f)*3,1.0f);Vector2 delta=Offset(pose,facing,10,7+(1-Ramp(.15f,.8f,phase))*19);return {pose.hand.x+delta.x,pose.hand.y+delta.y};}
void Shell(Vector2 center,const WeaponPose& pose,float alpha,float length=8){
    Vector2 a{center.x-pose.direction.x*length*.5f,center.y-pose.direction.y*length*.5f},b{center.x+pose.direction.x*length*.5f,center.y+pose.direction.y*length*.5f};
    DrawLineEx(a,b,4,Fade({32,30,28,255},alpha));DrawLineEx(a,b,2.8f,Fade({170,58,42,255},alpha));DrawLineEx(b,{b.x-pose.direction.x*2,b.y-pose.direction.y*2},3,Fade({229,188,92,255},alpha));
}
}
Style Type(int slot){return slot==1?Style::Tube:slot==6?Style::BreakOpen:Style::Magazine;}
float MagazineTravel(float p){if(p<.45f)return 24*Ramp(.12f,.34f,p);if(p<.52f)return 27;return 27*(1-Ramp(.52f,.83f,p));}
float BreakAngle(float p){return 28*Ramp(.08f,.28f,p)*(1-Ramp(.78f,.96f,p));}
Vector2 Support(Texture2D texture,int slot,const WeaponPose& pose,float scale,int facing,float p,float pump,Vector2 rest){
    auto g=GetWeaponGeometry(texture,slot);Vector2 sliding=Offset(pose,facing,-7*PumpStroke(pump),0);rest.x+=sliding.x;rest.y+=sliding.y;
    if(p<0)return rest;
    if(Type(slot)==Style::Tube){return Mix(rest,Feed(pose,facing,p),Ramp(.05f,.18f,p)*(1-Ramp(.82f,.98f,p)));}
    Rectangle part=Component(g,slot);Vector2 point=World(Center(part),g,pose,scale,facing);
    if(Type(slot)==Style::BreakOpen){Vector2 hinge=World({part.x,part.y+part.height*.6f},g,pose,scale,facing);Vector2 hand=Rotate(rest,hinge,BreakAngle(p)*facing);
        Vector2 feedOffset=Offset(pose,facing,5,13);Vector2 feed{hinge.x+feedOffset.x,hinge.y+feedOffset.y};
        return Mix(hand,feed,Ramp(.35f,.48f,p)*(1-Ramp(.69f,.8f,p)));}
    Vector2 delta=Offset(pose,facing,-MagazineTravel(p)*.15f,MagazineTravel(p));point.x+=delta.x;point.y+=delta.y;
    float reach=Ramp(.03f,.15f,p)*(1-Ramp(.92f,1,p));
    if(p>.84f){Vector2 bolt=Offset(pose,facing,8,-3);point={pose.hand.x+bolt.x,pose.hand.y+bolt.y};}
    return Mix(rest,point,reach);
}
void Draw(Texture2D texture,int slot,int skin,const WeaponPose& pose,float scale,int facing,float p,float pump,float alpha){
    auto g=GetWeaponGeometry(texture,slot);auto style=Type(slot);Rectangle part=Component(g,slot);bool animate=p>=0 || (slot==1 && pump>0);
    Vector2 hinge=World({part.x,part.y+part.height*.6f},g,pose,scale,facing);
    auto piece=[&](Rectangle source,Vector2 delta,float rotation,float opacity){
        if(source.width<=0 || source.height<=0 || opacity<=0)return;
        Vector2 center=World(Center(source),g,pose,scale,facing);if(rotation!=0)center=Rotate(center,hinge,rotation);center.x+=delta.x;center.y+=delta.y;
        Rectangle dst{center.x,center.y,source.width*scale,source.height*scale};source.width*=facing;
        DrawWeaponSkinTexture(texture,source,dst,{dst.width*.5f,dst.height*.5f},pose.rotation+rotation,slot,skin,alpha*opacity);
    };
    bool cut=animate && ((style==Style::Magazine && p>.12f && p<.84f) || style==Style::BreakOpen || (style==Style::Tube && pump>0));
    if(cut){
        piece({0,0,(float)texture.width,part.y},{0,0},0,1);
        piece({0,part.y+part.height,(float)texture.width,texture.height-part.y-part.height},{0,0},0,1);
        piece({0,part.y,part.x,part.height},{0,0},0,1);
        piece({part.x+part.width,part.y,texture.width-part.x-part.width,part.height},{0,0},0,1);
        float opacity=1,rotation=0;Vector2 delta{};
        if(style==Style::Magazine){float distance=MagazineTravel(p);delta=Offset(pose,facing,-distance*.15f,distance);opacity=p<.45f?1-Ramp(.34f,.45f,p):Ramp(.5f,.55f,p);}
        if(style==Style::BreakOpen)rotation=BreakAngle(p)*facing;
        if(style==Style::Tube)delta=Offset(pose,facing,-7*PumpStroke(pump),0);
        if(slot==2 && opacity>0){Vector2 plate=World(Center(part),g,pose,scale,facing);plate.x+=delta.x;plate.y+=delta.y;
            Vector2 up=Offset(pose,facing,2,-12);DrawLineEx({plate.x+up.x,plate.y+up.y},plate,5,Fade({40,47,53,255},alpha*opacity));
            DrawLineEx({plate.x+up.x+pose.direction.x*1.5f,plate.y+up.y+pose.direction.y*1.5f},{plate.x+pose.direction.x*1.5f,plate.y+pose.direction.y*1.5f},1,Fade({128,140,147,255},alpha*opacity));}
        piece(part,delta,rotation,opacity);
    }else piece({0,0,(float)texture.width,(float)texture.height},{0,0},0,1);
    if(p<0)return;
    if(style==Style::Tube && p>.15f && p<.85f)Shell(Feed(pose,facing,p),pose,alpha);
    if(style==Style::BreakOpen){
        if(p>.24f && p<.44f){float t=(p-.24f)/.20f;for(int i=0;i<2;++i){Vector2 d=Offset(pose,facing,-t*18,5+i*5+t*t*22);Shell({hinge.x+d.x,hinge.y+d.y},pose,alpha*(1-t),7);}}
        if(p>.48f && p<.79f){float t=Ramp(.48f,.77f,p);for(int i=0;i<2;++i){Vector2 d=Offset(pose,facing,4,(1-t)*21+i*4);Shell({hinge.x+d.x,hinge.y+d.y},pose,alpha,7);}}
    }
    if(style==Style::Magazine && p>.85f && p<.96f){Vector2 a=Offset(pose,facing,8,-4),b=Offset(pose,facing,12-4*std::sin((p-.85f)/.11f*PI),-4);DrawLineEx({pose.hand.x+a.x,pose.hand.y+a.y},{pose.hand.x+b.x,pose.hand.y+b.y},2.5f,Fade({185,197,203,255},alpha));}
}
}
