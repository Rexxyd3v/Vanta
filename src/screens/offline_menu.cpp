#include "offline_menu.h"
#include "../ui/ui_widgets.h"
#include "../ui/ui_theme.h"
#include "../ui/game_settings.h"
#include "../network/network_manager.h"
#include "../map_loader/MapRegistry.h"
#include <algorithm>

OfflineMenuScreen::OfflineMenuScreen() {
    font=LoadFontEx("assets/fonts/BruceForeverRegular-X3jd2.ttf",60,nullptr,0);
    if (!font.texture.id) font=GetFontDefault();
    auto& setup=Offline::Get(); Offline::Normalize(setup);
    auto names=MapRegistry::GetInstance().GetMapNames();
    if (std::find(names.begin(),names.end(),setup.map)==names.end() && !names.empty()) setup.map=names.front();
    background=std::make_unique<MenuBackground>(std::clamp(NetworkManager::GetInstance().localSkinIndex,1,4),MenuBackground::BackdropStyle::CINEMATIC_BEACH);
    for (int i=0;i<5;++i) modeRows[i]={76,170.0f+i*76,476,64};
}
OfflineMenuScreen::~OfflineMenuScreen() {
    if (font.texture.id!=GetFontDefault().texture.id) UnloadFont(font);
}
bool OfflineMenuScreen::Click(Rectangle r) const {
    return IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(Ui::RemapMouseToVirtual(),r);
}
void OfflineMenuScreen::CycleMap(int step) {
    auto names=MapRegistry::GetInstance().GetMapNames(); if (names.empty()) return;
    auto it=std::find(names.begin(),names.end(),Offline::Get().map);
    int index=it==names.end() ? 0 : (int)(it-names.begin());
    Offline::Get().map=names[(index+step+(int)names.size())%(int)names.size()];
}
bool OfflineMenuScreen::Update(float dt) {
    fade=Ui::Approach(fade,1,2,dt); background->Update(dt,fade);
    if (Settings::BlocksInput()) return true;
    auto& s=Offline::Get(); Vector2 mouse=Ui::RemapMouseToVirtual();
    hoverStart=Ui::Approach(hoverStart,CheckCollisionPointRec(mouse,start)?1.0f:0.0f,8,dt);
    hoverBack=Ui::Approach(hoverBack,CheckCollisionPointRec(mouse,back)?1.0f:0.0f,8,dt);
    if (IsKeyPressed(KEY_ESCAPE) || Click(back)) return false;
    for (int i=0;i<5;++i) {
        hoverMode[i]=Ui::Approach(hoverMode[i],CheckCollisionPointRec(mouse,modeRows[i])?1.0f:0.0f,8,dt);
        if (Click(modeRows[i])) s.mode=(Offline::Mode)i;
    }
    if (IsKeyPressed(KEY_UP)) s.mode=(Offline::Mode)(((int)s.mode+4)%5);
    if (IsKeyPressed(KEY_DOWN)) s.mode=(Offline::Mode)(((int)s.mode+1)%5);
    if (Click({646,188,38,42})) CycleMap(-1);
    if (Click({1158,188,38,42})) CycleMap(1);
    if (Click({646,314,44,32})) --s.bots;
    if (Click({1152,314,44,32})) ++s.bots;
    for (int i=0;i<3;++i) if (Click({646.0f+i*186,400,174,34})) s.difficulty=i;
    if (Offline::TeamMode(s.mode)) {
        if (Click({646,472,268,32})) s.playerTeam=1;
        if (Click({928,472,268,32})) s.playerTeam=2;
    }
    const int kills[]={10,20,40}, rounds[]={3,5,8};
    if (s.mode!=Offline::Mode::Training) for (int i=0;i<3;++i) if (Click({646.0f+i*186,542,174,32})) {
        if (Offline::RoundMode(s.mode) || s.mode==Offline::Mode::Deathmatch) s.roundLimit=rounds[i]; else s.killLimit=kills[i];
    }
    Offline::Normalize(s);
    if (IsKeyPressed(KEY_ENTER) || Click(start)) {
        NetworkManager::GetInstance().selectedMapName=s.map;
        Settings::Get().difficulty=s.difficulty;
        startGame=true; return false;
    }
    return true;
}
void OfflineMenuScreen::Draw(RenderTexture2D target) {
    mapPreview.Prepare(Offline::Get().map,550,126);
    background->Draw(target,fade,0.45f);
    BeginTextureMode(target);
    SetupUi::Backdrop();
    auto& s=Offline::Get(); Color gold=UiTheme::AccentGold(), white=UiTheme::TextPrimary(), muted{144,164,182,255};
    auto label=[&](const char* value,Vector2 p,float size,Color color) { DrawTextEx(font,value,p,size,1,color); };
    SetupUi::Header(font,"LOCAL OPERATIONS","PLAY OFFLINE","Your rules. Your squad. No connection needed.");
    const char* details[]={"Unlimited warm-up","Solo combat / respawn outposts","Team kills win / base respawns","One life per round / no respawns","Flag captures win / base respawns"};
    for (int i=0;i<5;++i) {
        Rectangle r=modeRows[i]; bool selected=(int)s.mode==i;
        SetupUi::ModeChoice(font,r,Offline::Name((Offline::Mode)i),details[i],selected,hoverMode[i]);
    }
    Rectangle previewBounds{646,146,550,126};
    mapPreview.Draw(previewBounds,font);
    Vector2 mouse=Ui::RemapMouseToVirtual();
    SetupUi::MapArrows(previewBounds,true,CheckCollisionPointRec(mouse,{646,188,38,42})?1.0f:0.0f,CheckCollisionPointRec(mouse,{1158,188,38,42})?1.0f:0.0f);
    label("BOT COUNT",{646,292},12,muted);
    Ui::DrawMenuButton({646,314,44,32},font,"-",0);
    Ui::DrawMenuButton({1152,314,44,32},font,"+",0);
    label(TextFormat("%02d",s.bots),{884,308},32,white);
    label(TextFormat("%d fighters total / up to 15 bots",s.bots+1),{754,356},10,muted);
    label("AI DIFFICULTY",{646,381},12,muted);
    for (int i=0;i<3;++i) {
        Rectangle r{646.0f+i*186,400,174,34}; bool selected=s.difficulty==i;
        DrawRectangleRounded(r,0.5f,10,Fade(gold,selected?0.15f:0.035f));
        Vector2 sz=MeasureTextEx(font,Offline::Difficulty(i),14,1);
        label(Offline::Difficulty(i),{r.x+(r.width-sz.x)/2,r.y+9},14,selected?gold:muted);
        if (selected) DrawLineEx({r.x+24,r.y+r.height},{r.x+r.width-24,r.y+r.height},2,gold);
    }
    label(s.difficulty==0?"Slower reactions, forgiving aim, lighter damage.":s.difficulty==1?"Balanced reactions, movement, and damage.":"Fast reactions, accurate fire, aggressive movement.",{646,446},10,muted);
    if (Offline::TeamMode(s.mode)) {
        label("YOUR SIDE",{646,466},10,muted);
        const char* teams[]={"GLOBAL RISK / BLUE","BLACK LIST / RED"};
        for (int i=0;i<2;++i) {
            Color color=i==0?UiTheme::TeamBlue():UiTheme::TeamRed(); float x=i==0?646.0f:928.0f;
            label(teams[i],{x+8,489},12,s.playerTeam==i+1?color:muted);
            if (s.playerTeam==i+1) DrawLineEx({x+8,510},{x+250,510},2,color);
        }
    } else label(s.mode==Offline::Mode::Training?"UNLIMITED RESPAWNS / NO SCORE LIMIT":"SOLO / EVERY BOT IS AN OPPONENT",{646,489},12,muted);
    if (s.mode!=Offline::Mode::Training) {
        label(s.mode==Offline::Mode::Deathmatch?"CAPTURES TO WIN / 5 MINUTE LIMIT":Offline::RoundMode(s.mode)?"ROUNDS TO WIN":"KILLS TO WIN / 5 MINUTE LIMIT",{646,525},10,muted);
        const int kills[]={10,20,40},rounds[]={3,5,8};
        for (int i=0;i<3;++i) {
            int value=(Offline::RoundMode(s.mode)||s.mode==Offline::Mode::Deathmatch)?rounds[i]:kills[i];
            bool selected=value==((Offline::RoundMode(s.mode)||s.mode==Offline::Mode::Deathmatch)?s.roundLimit:s.killLimit);
            label(TextFormat("%d",value),{720.0f+i*186,552},17,selected?gold:muted);
            if (selected) DrawLineEx({670.0f+i*186,575},{796.0f+i*186,575},2,gold);
        }
    }
    SetupUi::Action(font,start,"DEPLOY / ENTER",hoverStart);
    label("BACK / ESC",{back.x+hoverBack*4,back.y+8},16,hoverBack>0?gold:muted);
    label(s.mode==Offline::Mode::Deathmatch?TextFormat("Touch your flag / deliver to enemy base / %s drops flag",Settings::KeyLabel(Settings::Interact)):"Choose a mode, set your bots, then deploy.",{646,660},10,muted);
    EndTextureMode();
}
