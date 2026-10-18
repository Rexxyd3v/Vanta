#include "loadout_storage.h"
#include "game_settings.h"
#include "weapon_skin_catalog.h"
#include "equipment_art.h"
#include "ui_theme.h"
#include "../entities/weapon_geometry.h"
#include "../network/network_manager.h"
#include <cmath>
namespace {
void Gun(int weapon,int skin,Rectangle area) {
    auto texture=LoadWeaponSlotTexture(weapon,skin);if(!texture.id) return;
    auto source=GetWeaponGeometry(texture,weapon).bounds;
    float fit=std::min(area.width/source.width,area.height/source.height);
    Rectangle dest{area.x+area.width/2,area.y+area.height/2,source.width*fit,source.height*fit};
    DrawWeaponSkinTexture(texture,source,dest,{dest.width/2,dest.height/2},0,weapon,skin);
}
bool Hit(Rectangle r) { return !Settings::BlocksInput() && CheckCollisionPointRec(Ui::RemapMouseToVirtual(),r); }
bool Click(Rectangle r) { return Hit(r) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON); }
}
void LoadoutStorage::Open() { close=false;selectedBag=Settings::Get().activeBag; }
void LoadoutStorage::Update(float dt) {
    clock+=dt;if(IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_L) || Click({1120,36,130,38})) close=true;
}
void LoadoutStorage::Save() {
    auto& prefs=Settings::Get();prefs.activeBag=selectedBag;
    for(auto& bag:prefs.bags) Bags::Normalize(bag);
    NetworkManager::GetInstance().localWeaponSkin=Bags::PackedSkins(prefs.bags[selectedBag],prefs.weaponSkins);
    Settings::Save();
}
void LoadoutStorage::SelectBag(int bag) { selectedBag=std::clamp(bag,0,2);Save(); }
void LoadoutStorage::EquipGun(int gun,int skin) {
    auto& bag=Settings::Get().bags[selectedBag];
    if(selectedSlot==0 && Bags::IsPrimary(gun)) { bag.primary=gun;bag.primarySkin=skin<0?Settings::Get().weaponSkins[gun]:skin; }
    else if(selectedSlot==1 && Bags::IsSecondary(gun)) { bag.secondary=gun;bag.secondarySkin=skin<0?Settings::Get().weaponSkins[gun]:skin; }
    else return;
    Save();
}
void LoadoutStorage::Draw(Font font) {
    const Color ink{9,16,24,255},white{238,242,241,255},muted{142,164,182,255},gold=UiTheme::AccentGold();
    DrawRectangle(0,0,1300,680,ink);
    DrawRectangleGradientH(0,0,1300,680,Fade(Color{32,47,59,255},.45f),Fade(ink,0));
    auto text=[&](const char* value,float x,float y,float size,Color color) { DrawTextEx(font,value,{x,y},size,1,color); };
    text("ARMORY / PERSONAL STORAGE",70,30,12,gold);text("BUILD YOUR LOADOUT",68,52,31,white);
    text("CLOSE / ESC",1130,48,12,Hit({1120,36,130,38})?gold:muted);
    auto& bag=Settings::Get().bags[selectedBag];
    for(int i=0;i<3;++i) {
        Rectangle r{70+i*196.0f,106,180,42};bool active=i==selectedBag;
        if(active || Hit(r)) DrawRectangleRounded(r,.2f,8,Fade(gold,active?.16f:.07f));
        text(TextFormat("BAG %d",i+1),r.x+20,r.y+12,18,active?gold:muted);
        if(active) DrawLineEx({r.x+12,r.y+r.height},{r.x+r.width-12,r.y+r.height},2,gold);
        if(Click(r)) SelectBag(i);
    }
    text("Equipped for next deployment",720,122,12,muted);
    DrawLineEx({490,178},{490,600},1,Fade(muted,.2f));
    const char* slots[]={"PRIMARY","SECONDARY","MELEE"};
    for(int i=0;i<3;++i) {
        Rectangle r{70,180+i*106.0f,390,94};
        bool active=selectedSlot==i;
        DrawRectangleRounded(r,.15f,8,Fade(gold,active?.12f:Hit(r)?.07f:.025f));
        if(active) DrawRectangle((int)r.x,(int)r.y,3,(int)r.height,gold);
        text(slots[i],r.x+18,r.y+12,10,muted);
        const char* name=i==0?WeaponName(bag.primary):i==1?WeaponName(bag.secondary):Bags::MeleeName(bag.melee);
        text(name,r.x+18,r.y+67,13,active?gold:white);
        if(i<2) Gun(i==0?bag.primary:bag.secondary,i==0?bag.primarySkin:bag.secondarySkin,{r.x+150,r.y+10,218,66});
        else DrawEquipmentArt(true,bag.melee,bag.meleeSkin,{r.x+160,r.y+14,200,56});
        if(Click(r)) selectedSlot=i;
    }
    for(int i=0;i<3;++i) {
        Rectangle r{70+i*132.0f,504,124,101};bool enabled=Bags::UtilityEquipped(bag,i),active=selectedSlot==i+3;
        DrawRectangleRounded(r,.15f,8,Fade(gold,active?.13f:Hit(r)?.08f:.025f));
        DrawEquipmentArt(false,i,0,{r.x+34,r.y+8,56,56},0,enabled?1:.3f);
        text(i==1?"POISON SMOKE":Bags::UtilityName(i),r.x+8,r.y+70,10,active?gold:muted);
        text(enabled?"EQUIPPED":"EMPTY",r.x+27,r.y+87,10,enabled?UiTheme::ReadyGreen():muted);
        if(Click(r)) selectedSlot=i+3;
    }
    text("STORAGE",530,180,20,gold);
    text(selectedSlot==0?"PRIMARY WEAPONS":selectedSlot==1?"SIDEARMS":selectedSlot==2?"MELEE WEAPONS":"TACTICAL EQUIPMENT",530,213,11,muted);
    if(selectedSlot<2) {
        int row=0;
        for(int gun=0;gun<WeaponCount;++gun) {
            if(selectedSlot==0?!Bags::IsPrimary(gun):!Bags::IsSecondary(gun)) continue;
            Rectangle r{530+(row%2)*350.0f,240+(row/2)*82.0f,334,74};++row;
            int equipped=selectedSlot==0?bag.primary:bag.secondary;
            DrawRectangleRounded(r,.12f,8,Fade(gold,equipped==gun?.12f:Hit(r)?.08f:.02f));
            text(WeaponName(gun),r.x+12,r.y+10,12,equipped==gun?gold:white);
            Gun(gun,equipped==gun?(selectedSlot==0?bag.primarySkin:bag.secondarySkin):Settings::Get().weaponSkins[gun],{r.x+120,r.y+10,200,51});
            text(equipped==gun?"EQUIPPED":"OWNED / CLICK TO EQUIP",r.x+12,r.y+55,10,muted);
            if(Click(r)) EquipGun(gun);
        }
        int gun=selectedSlot==0?bag.primary:bag.secondary,skin=selectedSlot==0?bag.primarySkin:bag.secondarySkin;
        text("WEAPON FINISH",530,505,10,muted);
        for(int i=0;i<4;++i) {
            Rectangle r{530+i*175.0f,528,162,77};
            DrawRectangleRounded(r,.15f,8,Fade(gold,skin==i?.14f:Hit(r)?.08f:.025f));
            Gun(gun,i,{r.x+8,r.y+5,r.width-16,43});
            text(WeaponSkinLabel(gun,i),r.x+8,r.y+60,10,skin==i?gold:muted);
            if(Click(r)) { Settings::Get().weaponSkins[gun]=i;EquipGun(gun,i); }
        }
    } else if(selectedSlot==2) {
        for(int i=0;i<2;++i) {
            Rectangle r{530+i*350.0f,240,334,215};
            DrawRectangleRounded(r,.1f,8,Fade(gold,bag.melee==i?.13f:Hit(r)?.08f:.025f));
            DrawEquipmentArt(true,i,bag.meleeSkin,{r.x+20,r.y+35,294,105});
            text(Bags::MeleeName(i),r.x+20,r.y+164,16,bag.melee==i?gold:white);
            text(TextFormat("%.0f DAMAGE / %.0f REACH",Bags::MeleeDamage(i),Bags::MeleeRange(i)),r.x+20,r.y+192,10,muted);
            if(Click(r)) { bag.melee=i;Save(); }
        }
        const char* labels[]={"DEFAULT","SILVER","NEON EDGE","CARBON"};
        for(int i=0;i<4;++i) {
            Rectangle r{530+i*175.0f,528,162,77};
            DrawRectangleRounded(r,.1f,8,Fade(gold,bag.meleeSkin==i?.14f:.03f));
            DrawEquipmentArt(true,bag.melee,i,{r.x+10,r.y+5,142,43});text(labels[i],r.x+12,r.y+60,9,muted);
            if(Click(r)) { bag.meleeSkin=i;Save(); }
        }
    } else {
        int kind=selectedSlot-3;bool enabled=Bags::UtilityEquipped(bag,kind);
        DrawEquipmentArt(false,kind,0,{650,250,380,220});
        text(Bags::UtilityName(kind),560,480,22,white);
        text(kind==0?"Blast damage, pressure wave and debris.":kind==1?"Spreading poison cloud. Your own gas can hurt you.":"Blinds nearby players and disrupts bots. No blast damage.",560,520,11,muted);
        Rectangle action{560,555,600,44};
        DrawRectangleRounded(action,.4f,8,Fade(gold,Hit(action)?.25f:.14f));
        text(enabled?"REMOVE FROM THIS BAG":"EQUIP IN THIS BAG",action.x+28,action.y+13,15,gold);
        if(Click(action)) { if(kind==0) bag.grenade=!enabled;else if(kind==1) bag.smoke=!enabled;else bag.flash=!enabled;Save(); }
    }
    DrawLineEx({70,629},{1230,629},1,Fade(muted,.2f));
    text("All owned equipment is stored here. Bag changes in a match are limited to your base or respawn.",70,648,10,muted);
}
