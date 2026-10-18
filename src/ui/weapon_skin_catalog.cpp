#include "weapon_skin_catalog.h"
#include "../entities/weapon_geometry.h"
#include <algorithm>
#include <rlgl.h>

namespace {

const char* Folders[]={"SMG","Shotgun","Pistol","AssaultRifle","Sniper"};
std::string DefaultFile(int slot) {
    return std::string("assets/Weapons/")+Folders[slot]+"/default.png";
}
struct Palette { Color main,detail; };
Palette Colors(int slot,int skin) {
    static const Palette colors[WeaponCount][3]={
        {{{80,225,232,255},{224,67,206,255}},{{242,91,36,255},{255,207,83,255}},{{74,86,103,255},{165,185,200,255}}},
        {{{156,89,44,255},{222,168,94,255}},{{214,166,64,255},{255,228,137,255}},{{57,68,81,255},{130,149,167,255}}},
        {{{179,199,215,255},{68,87,110,255}},{{53,218,231,255},{184,75,224,255}},{{55,64,77,255},{138,155,174,255}}},
        {{{67,111,69,255},{166,177,99,255}},{{192,48,57,255},{52,224,229,255}},{{169,211,229,255},{59,90,119,255}}},
        {{{183,148,94,255},{98,84,61,255}},{{44,65,80,255},{100,206,190,255}},{{99,79,191,255},{71,225,201,255}}}
    };
    return colors[std::clamp(slot,0,WeaponCount-1)][std::clamp(skin-1,0,2)];
}
}
std::string GetWeaponTexturePath(int slot,int selection) {
    (void)selection;
    return DefaultFile(std::clamp(slot,0,WeaponCount-1));
}
Texture2D LoadWeaponSlotTexture(int slot,int selection) { return TextureManager::GetTexture(GetWeaponTexturePath(slot,selection)); }
std::string GetWeaponSlotSkinPath(int slot,int selection) { std::string file=GetWeaponTexturePath(slot,selection); return file.substr(0,file.find_last_of('/')+1); }
const char* WeaponSkinLabel(int slot,int skin) {
    static const char* labels[WeaponCount][WeaponSkinCount]={
        {"DEFAULT","NEON CAMO","MAGMA","CARBON FIBER"},
        {"DEFAULT","CLASSIC WOOD","GOLDEN OAK","CARBON FIBER"},
        {"DEFAULT","SILVER ALLOY","NEON CYBER","CARBON FIBER"},
        {"DEFAULT","JUNGLE CAMO","CRIMSON CIRCUIT","ARCTIC STEEL"},
        {"DEFAULT","DESERT CAMO","NIGHT OPS","AURORA"}
    };
    return labels[std::clamp(slot,0,WeaponCount-1)][std::clamp(skin,0,WeaponSkinCount-1)];
}
Color GetWeaponSlotSkinTint(int slot,int selection) {
    int skin=WeaponSkinSelection(selection,slot); return skin==0?WHITE:Colors(slot,skin).main;
}
void DrawWeaponSkinTexture(Texture2D texture,Rectangle source,Rectangle destination,Vector2 origin,float rotation,int slot,int selection,float alpha) {
    int skin=WeaponSkinSelection(selection,slot);
    if(skin==0) { DrawTexturePro(texture,source,destination,origin,rotation,Fade(WHITE,alpha)); return; }
    static Shader shader{}; static bool attempted=false;
    if(!attempted) {
        const char* fragment=R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 inkBounds;
uniform vec2 imageSize;
uniform vec3 paint;
uniform vec3 accent;
uniform int style;
uniform int weaponSlot;
out vec4 finalColor;
void main() {
    vec4 pixel=texture(texture0,fragTexCoord);
    vec2 uv=(fragTexCoord*imageSize-inkBounds.xy)/inkBounds.zw;
    float shade=max(pixel.r,max(pixel.g,pixel.b));
    vec2 cell=floor(uv*vec2(14.0,7.0));
    float noise=fract(sin(dot(cell,vec2(12.9898,78.233)))*43758.5453);
    float pattern=style==1?step(0.58,noise):style==2?step(0.84,fract(uv.x*8.0+uv.y*2.5)):step(0.5,mod(cell.x+cell.y,2.0))*0.48;
    if(weaponSlot==1 && style==1) pattern=0.35+0.25*sin(uv.y*100.0+sin(uv.x*18.0)*1.6);
    if(weaponSlot==2 && style==1) pattern=step(0.94,fract(uv.y*40.0))*0.35;
    vec3 material=mix(paint,accent,pattern)*(0.50+shade*0.50);
    if(weaponSlot==0 && style==2) {
        float veins=abs(sin(uv.x*34.0+sin(uv.y*13.0)*2.0)*sin(uv.y*29.0+sin(uv.x*17.0)*1.8));
        material=mix(vec3(0.10,0.055,0.03),mix(paint,accent,0.65),1.0-smoothstep(0.10,0.26,veins))*(0.6+shade*0.4);
    }
    pixel.rgb=shade<0.18?pixel.rgb:material;
    finalColor=pixel*fragColor*colDiffuse;
})GLSL";
        shader=LoadShaderFromMemory(nullptr,fragment); attempted=true;
    }
    if(!shader.id || shader.id==rlGetShaderIdDefault()) { DrawTexturePro(texture,source,destination,origin,rotation,Fade(GetWeaponSlotSkinTint(slot,selection),alpha)); return; }
    Palette palette=Colors(slot,skin); auto bounds=GetWeaponGeometry(texture,slot).bounds;
    float mainColor[]={palette.main.r/255.0f,palette.main.g/255.0f,palette.main.b/255.0f};
    float detail[]={palette.detail.r/255.0f,palette.detail.g/255.0f,palette.detail.b/255.0f};
    float size[]={(float)texture.width,(float)texture.height}; float ink[]={bounds.x,bounds.y,std::max(bounds.width,1.0f),std::max(bounds.height,1.0f)};
    SetShaderValue(shader,GetShaderLocation(shader,"paint"),mainColor,SHADER_UNIFORM_VEC3);
    SetShaderValue(shader,GetShaderLocation(shader,"accent"),detail,SHADER_UNIFORM_VEC3);
    SetShaderValue(shader,GetShaderLocation(shader,"imageSize"),size,SHADER_UNIFORM_VEC2);
    SetShaderValue(shader,GetShaderLocation(shader,"inkBounds"),ink,SHADER_UNIFORM_VEC4);
    SetShaderValue(shader,GetShaderLocation(shader,"style"),&skin,SHADER_UNIFORM_INT);
    SetShaderValue(shader,GetShaderLocation(shader,"weaponSlot"),&slot,SHADER_UNIFORM_INT);
    BeginShaderMode(shader); DrawTexturePro(texture,source,destination,origin,rotation,Fade(WHITE,alpha)); EndShaderMode();
}
const char* WeaponSkinPath(WeaponSkinId) { return "assets/Weapons/SMG/"; }
Color GetWeaponSkinTint(int skin) { return GetWeaponSlotSkinTint(0,skin); }
std::vector<Ui::GalleryItem> GetWeaponSkinGalleryItems() {
    std::vector<Ui::GalleryItem> items;
    for(int skin=0;skin<WeaponSkinCount;++skin) items.push_back({LoadWeaponSlotTexture(0,skin),WeaponSkinLabel(0,skin),false});
    return items;
}
