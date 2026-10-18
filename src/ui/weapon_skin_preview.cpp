#include "weapon_skin_preview.h"
#include "../entities/weapon_geometry.h"
#include "../network/network_manager.h"
#include "game_settings.h"
#include "ui_theme.h"
#include <cmath>

WeaponSkinPreview::WeaponSkinPreview(int selection) { SetSkin(selection); }
void WeaponSkinPreview::SetSkin(int selection) {
    for(int slot=0;slot<WeaponCount;++slot) selectedSkins[slot]=WeaponSkinSelection(selection,slot);
}
void WeaponSkinPreview::SelectSkin(int slot,int skin,bool persist) {
    if(slot<0 || slot>=WeaponCount || skin<0 || skin>=WeaponSkinCount) return;
    selectedSkins[slot]=skin;
    Settings::Get().weaponSkins=selectedSkins;
    NetworkManager::GetInstance().localWeaponSkin=GetPackedSkins();
    if(persist) Settings::Save();
}
void WeaponSkinPreview::Update(float dt) { rotation+=dt; }
static void DrawWeaponPreview(int slot,int selection,Rectangle area,float rotation) {
    Texture2D texture=LoadWeaponSlotTexture(slot,selection);
    if(!texture.id) return;
    Rectangle source=GetWeaponGeometry(texture,slot).bounds;
    if(source.width<=0 || source.height<=0) return;
    float scale=std::min(area.width/source.width,area.height/source.height);
    Rectangle destination{area.x+area.width/2,area.y+area.height/2,source.width*scale,source.height*scale};
    DrawWeaponSkinTexture(texture,source,destination,{destination.width/2,destination.height/2},rotation,slot,selection);
}
void WeaponSkinPreview::DrawPreview(Rectangle area,int,Font font) {
    Color gold=UiTheme::AccentGold();
    DrawRectangleGradientV((int)area.x,(int)area.y,(int)area.width,(int)area.height,{10,18,27,100},{10,18,27,220});
    DrawLineEx({area.x+24,area.y+area.height-36},{area.x+area.width-24,area.y+area.height-36},1,Fade(gold,0.22f));
    Rectangle art{area.x+62,area.y+35,area.width-124,area.height-90};
    DrawWeaponPreview(selectedWeaponSlot,GetPackedSkins(),art,std::sin(rotation)*1.8f);
    const char* name=WeaponName(selectedWeaponSlot); float size=17;
    Vector2 measured=MeasureTextEx(font,name,size,1);
    DrawTextEx(font,name,{area.x+(area.width-measured.x)/2,area.y+area.height-26},size,1,gold);
    for(int direction : {-1,1}) {
        Rectangle hit{direction<0?area.x+10:area.x+area.width-46,area.y+area.height/2-20,36,40};
        bool hovered=CheckCollisionPointRec(Ui::RemapMouseToVirtual(),hit);
        Color color=hovered?gold:UiTheme::TextPrimary();
        float x=hit.x+18,y=hit.y+20;
        DrawLineEx({x-direction*4,y-7},{x+direction*4,y},2,color);
        DrawLineEx({x+direction*4,y},{x-direction*4,y+7},2,color);
        if(!Settings::BlocksInput() && hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) selectedWeaponSlot=(selectedWeaponSlot+direction+WeaponCount)%WeaponCount;
    }
}
int WeaponSkinPreview::DrawSelector(Rectangle area,Font font,int& selectedSkin,Vector2 mouse) {
    const float gap=9,width=(area.width-gap*(WeaponSkinCount-1))/WeaponSkinCount;
    int clicked=-1;
    for(int skin=0;skin<WeaponSkinCount;++skin) {
        Rectangle card{area.x+skin*(width+gap),area.y,width,area.height};
        bool selected=selectedSkins[selectedWeaponSlot]==skin,hover=CheckCollisionPointRec(mouse,card);
        DrawRectangleRounded(card,0.15f,6,Fade(UiTheme::AccentGold(),selected?0.15f:hover?0.09f:0.035f));
        if(selected) DrawLineEx({card.x+12,card.y+card.height},{card.x+width-12,card.y+card.height},2,UiTheme::AccentGold());
        auto skins=selectedSkins; skins[selectedWeaponSlot]=skin;
        DrawWeaponPreview(selectedWeaponSlot,PackWeaponSkins(skins),{card.x+12,card.y+10,width-24,card.height-40},0);
        const char* label=WeaponSkinLabel(selectedWeaponSlot,skin);
        float size=std::min(10.0f,(width-12)/MeasureTextEx(font,label,10,1).x*10);
        Vector2 measured=MeasureTextEx(font,label,size,1);
        DrawTextEx(font,label,{card.x+(width-measured.x)/2,card.y+card.height-24},size,1,selected?UiTheme::AccentGold():UiTheme::TextMuted());
        if(!Settings::BlocksInput() && hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { SelectSkin(selectedWeaponSlot,skin); selectedSkin=skin; clicked=skin; }
    }
    return clicked;
}
