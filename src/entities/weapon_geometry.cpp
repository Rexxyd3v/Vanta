#include "weapon_geometry.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

WeaponGeometry GetWeaponGeometry(Texture2D texture,int slot) {
    static std::unordered_map<unsigned int,WeaponGeometry> cache;
    auto found=cache.find(texture.id); if (found!=cache.end()) return found->second;
    float width=texture.width>0?(float)texture.width:2048, height=texture.height>0?(float)texture.height:2048;
    WeaponGeometry geometry{{width*960/2048,height*1580/2048},{width*1700/2048,height*1425/2048},width};
    // Find the end of the visible barrel, rather than the edge of its padded canvas.
    if (texture.id) {
        Image image=LoadImageFromTexture(texture);
        Color* pixels=image.data?LoadImageColors(image):nullptr;
        if (pixels) {
            int right=-1;
            for (int x=image.width-1;x>=0 && right<0;--x) for (int y=0;y<image.height;++y)
                if (pixels[y*image.width+x].a>64) { right=x; break; }
            if (right>=0) {
                int left=right,top=image.height-1,bottom=0;
                for(int y=0;y<image.height;++y) for(int x=0;x<=right;++x) if(pixels[y*image.width+x].a>64) {
                    left=std::min(left,x); top=std::min(top,y); bottom=std::max(bottom,y);
                }
                geometry.bounds={(float)left,(float)top,(float)(right-left+1),(float)(bottom-top+1)};
                const float visibleWidth[]={1000,1250,640,1100,1450};
                const Vector2 grips[]={{0.34f,0.70f},{0.28f,0.75f},{0.22f,0.73f},{0.33f,0.65f},{0.27f,0.80f}};
                int weapon=std::clamp(slot,0,4);
                geometry.pivot={left+geometry.bounds.width*grips[weapon].x,top+geometry.bounds.height*grips[weapon].y};
                geometry.scale=visibleWidth[weapon]/geometry.bounds.width;
                float sum=0,count=0;
                for (int x=std::max(0,right-3);x<=right;++x) for (int y=0;y<image.height;++y)
                    if (pixels[y*image.width+x].a>64) { sum+=y; ++count; }
                if (count>0) geometry.muzzle={(float)right+1,sum/count};
            }
            UnloadImageColors(pixels);
        }
        UnloadImage(image);
        cache[texture.id]=geometry;
    }
    return geometry;
}
WeaponPose PoseWeapon(Vector2 hand,Vector2 direction,int facing,float scale,const WeaponGeometry& g) {
    float length=std::sqrt(direction.x*direction.x+direction.y*direction.y);
    if (!std::isfinite(length) || length<0.0001f) direction={(float)facing,0};
    else { direction.x/=length; direction.y/=length; }
    facing=facing<0?-1:1;
    float forward=(g.muzzle.x-g.pivot.x)*scale, lateral=(g.muzzle.y-g.pivot.y)*scale*facing;
    Vector2 muzzle{hand.x+direction.x*forward-direction.y*lateral,hand.y+direction.y*forward+direction.x*lateral};
    return {hand,muzzle,direction,{(facing==1?g.pivot.x:g.width-g.pivot.x)*scale,g.pivot.y*scale},
        std::atan2(direction.y,direction.x)*RAD2DEG-(facing==1?0:180)};
}
WeaponPose AimWeapon(Vector2 hand,Vector2 target,int facing,float scale,const WeaponGeometry& g) {
    float dx=target.x-hand.x,dy=target.y-hand.y,length=std::sqrt(dx*dx+dy*dy);
    float lateral=(g.muzzle.y-g.pivot.y)*scale*(facing<0?-1:1);
    float angle=length>0.001f?std::atan2(dy,dx)-std::asin(std::clamp(lateral/length,-0.95f,0.95f)):(facing==1?0:PI);
    return PoseWeapon(hand,{std::cos(angle),std::sin(angle)},facing,scale,g);
}
