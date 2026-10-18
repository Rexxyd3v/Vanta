#include "equipment_art.h"
#include "weapon_skin_catalog.h"
#include "../utils/texture_manager.h"
#include <cmath>
#include <algorithm>
namespace {
struct Sprite {Texture2D texture{};Rectangle ink{};};
Sprite GetSprite(bool melee,int item) {
    static Sprite blades[2]{},utilities[3]{};
    item=std::clamp(item,0,melee?1:2);Sprite& sprite=melee?blades[item]:utilities[item];
    if(!sprite.texture.id) {
        const char* file=melee?(item==0?"assets/Equipment/combat-knife.png":"assets/Equipment/kukri.png"):"assets/Equipment/utilities.png";
        sprite.texture=TextureManager::GetTexture(file);
        if(sprite.texture.id) {
            Image image=LoadImageFromTexture(sprite.texture);
            Rectangle cell=melee?Rectangle{0,0,(float)image.width,(float)image.height}:Rectangle{item*image.width/3.0f,0,image.width/3.0f,(float)image.height};
            Image part=ImageFromImage(image,cell);sprite.ink=GetImageAlphaBorder(part,.1f);
            sprite.ink.x+=cell.x;sprite.ink.y+=cell.y;UnloadImage(part);UnloadImage(image);
        }
    }
    return sprite;
}
}
void DrawEquipmentArt(bool melee,int item,int skin,Rectangle area,float rotation,float alpha) {
    auto sprite=GetSprite(melee,item);if(!sprite.texture.id || sprite.ink.width<1) return;
    float fit=std::min(area.width/sprite.ink.width,area.height/sprite.ink.height);
    Rectangle dest{area.x+area.width/2,area.y+area.height/2,sprite.ink.width*fit,sprite.ink.height*fit};
    Vector2 origin{dest.width/2,dest.height/2};
    if(melee) DrawWeaponSkinTexture(sprite.texture,sprite.ink,dest,origin,rotation,2,skin,alpha);
    else DrawTexturePro(sprite.texture,sprite.ink,dest,origin,rotation,Fade(WHITE,alpha));
}
void DrawHeldEquipment(Vector2 hand,Vector2 target,int facing,int melee,int skin,int utility,float swing) {
    float angle=std::atan2(target.y-hand.y,target.x-hand.x)*RAD2DEG;
    bool blade=melee>=0;auto sprite=GetSprite(blade,blade?melee:utility);if(!sprite.texture.id) return;
    if(!blade) { DrawEquipmentArt(false,utility,0,{hand.x-12,hand.y-17,24,34},angle*.08f);return; }
    float length=melee==1?76.0f:64.0f,scale=length/sprite.ink.width;
    Rectangle src=sprite.ink;Vector2 origin{length*.23f,src.height*scale*.5f};
    if(facing<0) {src.width=-src.width;origin.x=length-origin.x;angle-=180;}
    if(swing>0) angle+=(1-swing)*130-65;
    DrawWeaponSkinTexture(sprite.texture,src,{hand.x,hand.y,length,sprite.ink.height*scale},origin,angle,2,skin);
    if(swing>0) {
        float aim=std::atan2(target.y-hand.y,target.x-hand.x)*RAD2DEG;
        DrawRing(hand,62,65,aim-50,aim+50,24,Fade(Color{213,242,255,255},std::sin(swing*PI)*.7f));
    }
}
