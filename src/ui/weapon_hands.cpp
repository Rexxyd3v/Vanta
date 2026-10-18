#include "weapon_hands.h"
#include "weapon_reload.h"
#include "../entities/bonus_catalog.h"
#include "../utils/texture_manager.h"
#include <algorithm>
#include <cmath>
namespace {
Vector2 Contact(const WeaponGeometry& g,Vector2 point,const WeaponPose& pose,float scale,int facing){
    Vector2 normal{-pose.direction.y,pose.direction.x};float x=(point.x-g.pivot.x)*scale,y=(point.y-g.pivot.y)*scale*facing;
    return {pose.hand.x+pose.direction.x*x+normal.x*y,pose.hand.y+pose.direction.y*x+normal.y*y};
}
struct HandSprite {Texture2D texture{};Rectangle ink{};};
HandSprite Hand(){
    static HandSprite sprite;
    if(!sprite.texture.id){sprite.texture=TextureManager::GetTexture("assets/Free 2D Animated Vector Game Character Sprites/Free 2D Animated Vector Game Character Sprites/Animated body parts/Right hands/handR1/idle_0.png");
        if(sprite.texture.id){Image image=LoadImageFromTexture(sprite.texture);sprite.ink=GetImageAlphaBorder(image,.1f);UnloadImage(image);}}
    return sprite;
}
}
WeaponHold GetWeaponHold(Texture2D texture,int slot,const WeaponPose& pose,float scale,int facing,float reload,float pump){
    const auto g=GetWeaponGeometry(texture,slot);Vector2 support=Contact(g,g.support,pose,scale,facing);
    if(slot==2){Vector2 n{-pose.direction.y,pose.direction.x};support={pose.hand.x-pose.direction.x*2+n.x*3,pose.hand.y-pose.direction.y*2+n.y*3};}
    support=WeaponReload::Support(texture,slot,pose,scale,facing,reload,pump,support);
    return {pose.hand,support,std::atan2(pose.direction.y,pose.direction.x)*RAD2DEG,true};
}
WeaponHold GetBonusHold(int kind,const WeaponPose& pose,float scale,int facing){
    auto g=Bonus::Geometry(kind);return {pose.hand,Contact(g,g.support,pose,scale,facing),std::atan2(pose.direction.y,pose.direction.x)*RAD2DEG,true};
}
void DrawEquipmentHand(Vector2 position,float angle,float size,float alpha,bool fingers){
    float a=angle*DEG2RAD;Vector2 axis{std::cos(a),std::sin(a)},normal{-axis.y,axis.x};
    if(!fingers){auto hand=Hand();if(hand.texture.id && hand.ink.height>0){float width=size*hand.ink.width/hand.ink.height;DrawTexturePro(hand.texture,hand.ink,{position.x,position.y,width,size},{width*.5f,size*.5f},angle-90,Fade(WHITE,alpha));}
        return;
    }
    // Fingers cross the grip after the weapon is drawn; the palm stays behind it.
    for(int i=0;i<2;++i){float offset=(i-.5f)*size*.22f;Vector2 p{position.x+normal.x*offset,position.y+normal.y*offset};
        DrawLineEx({p.x-axis.x*size*.21f,p.y-axis.y*size*.21f},{p.x+axis.x*size*.23f,p.y+axis.y*size*.23f},size*.19f,Fade({12,16,21,255},alpha));
        DrawLineEx({p.x-axis.x*size*.16f-normal.x*.6f,p.y-axis.y*size*.16f-normal.y*.6f},{p.x+axis.x*size*.12f-normal.x*.6f,p.y+axis.y*size*.12f-normal.y*.6f},.7f,Fade({83,91,98,255},alpha*.75f));
    }
}
void DrawWeaponHands(const WeaponHold& hold,float size,float alpha,bool fingers){
    if(hold.twoHands)DrawEquipmentHand(hold.support,hold.angle,size*.9f,alpha,fingers);
    DrawEquipmentHand(hold.trigger,hold.angle,size,alpha,fingers);
}
