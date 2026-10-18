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
    loadout=std::make_unique<LoadoutStorage>();
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
float OfflineMenuScreen::SetupContentHeight() const {
    return Offline::Get().mode==Offline::Mode::Training?210.0f:330.0f;
}
float OfflineMenuScreen::SetupScrollMax() const {
    return std::max(0.0f,SetupContentHeight()-setupViewport.height);
}
void OfflineMenuScreen::ScrollSetup(float delta) {
    setupScroll=std::clamp(setupScroll+delta,0.0f,SetupScrollMax());
}
Rectangle OfflineMenuScreen::SetupBounds(Rectangle local) const {
    return {setupViewport.x+local.x,setupViewport.y+local.y-setupScroll,local.width,local.height};
}
Rectangle OfflineMenuScreen::ScrollThumb() const {
    float height=std::max(28.0f,setupViewport.height*setupViewport.height/SetupContentHeight());
    float travel=setupViewport.height-height;
    return {setupViewport.x+setupViewport.width+11,setupViewport.y+(SetupScrollMax()>0?travel*setupScroll/SetupScrollMax():0),4,height};
}
bool OfflineMenuScreen::SetupHit(Rectangle local,Vector2 mouse) const {
    return CheckCollisionPointRec(mouse,setupViewport) && CheckCollisionPointRec(mouse,SetupBounds(local));
}
bool OfflineMenuScreen::ClickSetup(Rectangle local) const {
    return IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && SetupHit(local,Ui::RemapMouseToVirtual());
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
    loadout->Update(dt);
    if(loadoutOpen) {
        if(loadout->WantsClose()) loadoutOpen=false;
        return true;
    }
    if(IsKeyPressed(KEY_L) || Click({76,552,476,42})) { loadoutOpen=true;loadout->Open(); return true; }
    auto& s=Offline::Get(); Vector2 mouse=Ui::RemapMouseToVirtual();
    auto previousMode=s.mode;
    hoverStart=Ui::Approach(hoverStart,CheckCollisionPointRec(mouse,start)?1.0f:0.0f,8,dt);
    hoverBack=Ui::Approach(hoverBack,CheckCollisionPointRec(mouse,back)?1.0f:0.0f,8,dt);
    if (IsKeyPressed(KEY_ESCAPE) || Click(back)) return false;
    for (int i=0;i<5;++i) {
        hoverMode[i]=Ui::Approach(hoverMode[i],CheckCollisionPointRec(mouse,modeRows[i])?1.0f:0.0f,8,dt);
        if (Click(modeRows[i])) s.mode=(Offline::Mode)i;
    }
    if (IsKeyPressed(KEY_UP)) s.mode=(Offline::Mode)(((int)s.mode+4)%5);
    if (IsKeyPressed(KEY_DOWN)) s.mode=(Offline::Mode)(((int)s.mode+1)%5);
    if(s.mode!=previousMode) { setupScroll=0; draggingScroll=false; }
    ScrollSetup(0);
    Rectangle previousMap{previewBounds.x,previewBounds.y+previewBounds.height/2-21,38,42};
    Rectangle nextMap{previewBounds.x+previewBounds.width-38,previousMap.y,38,42};
    if(Click(previousMap)) CycleMap(-1);
    if(Click(nextMap)) CycleMap(1);
    Rectangle scrollArea{setupViewport.x,setupViewport.y,setupViewport.width+24,setupViewport.height};
    if(CheckCollisionPointRec(mouse,scrollArea)) {
        ScrollSetup(-GetMouseWheelMove()*46);
        if(IsKeyPressed(KEY_HOME)) setupScroll=0;
        if(IsKeyPressed(KEY_END)) setupScroll=SetupScrollMax();
    }
    if(IsKeyPressed(KEY_PAGE_DOWN)) ScrollSetup(setupViewport.height*.8f);
    if(IsKeyPressed(KEY_PAGE_UP)) ScrollSetup(-setupViewport.height*.8f);
    Rectangle thumb=ScrollThumb();
    Rectangle track{thumb.x-6,setupViewport.y,16,setupViewport.height};
    if(SetupScrollMax()>0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mouse,track)) {
        draggingScroll=true;
        scrollGrab=CheckCollisionPointRec(mouse,thumb)?mouse.y-thumb.y:thumb.height*.5f;
    }
    if(!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) draggingScroll=false;
    if(draggingScroll) {
        float travel=setupViewport.height-thumb.height;
        setupScroll=std::clamp((mouse.y-setupViewport.y-scrollGrab)/travel*SetupScrollMax(),0.0f,SetupScrollMax());
    }
    if(!draggingScroll) {
        if(ClickSetup({0,20,44,32})) --s.bots;
        if(ClickSetup({506,20,44,32})) ++s.bots;
        for(int i=0;i<3;++i) if(ClickSetup({i*186.0f,99,174,32})) s.difficulty=i;
        if(Offline::TeamMode(s.mode)) {
            if(ClickSetup({0,198,268,32})) s.playerTeam=1;
            if(ClickSetup({282,198,268,32})) s.playerTeam=2;
        }
        const int kills[]={10,20,40},rounds[]={3,5,8};
        if(s.mode!=Offline::Mode::Training) for(int i=0;i<3;++i) if(ClickSetup({i*186.0f,278,174,34})) {
            if(Offline::RoundMode(s.mode) || s.mode==Offline::Mode::Deathmatch) s.roundLimit=rounds[i]; else s.killLimit=kills[i];
        }
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
    mapPreview.Prepare(Offline::Get().map,(int)previewBounds.width,(int)previewBounds.height);
    ScrollSetup(0);
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
    mapPreview.Draw(previewBounds,font);
    Vector2 mouse=Ui::RemapMouseToVirtual();
    Rectangle previousMap{previewBounds.x,previewBounds.y+previewBounds.height/2-21,38,42};
    Rectangle nextMap{previewBounds.x+previewBounds.width-38,previousMap.y,38,42};
    SetupUi::MapArrows(previewBounds,true,CheckCollisionPointRec(mouse,previousMap)?1.0f:0.0f,CheckCollisionPointRec(mouse,nextMap)?1.0f:0.0f);
    auto settingLabel=[&](const char* value,float x,float y,float size,Color color) {
        label(value,{setupViewport.x+x,setupViewport.y+y-setupScroll},size,color);
    };
    BeginScissorMode((int)setupViewport.x,(int)setupViewport.y,(int)setupViewport.width,(int)setupViewport.height);
    settingLabel("BOT COUNT",0,0,12,muted);
    Ui::DrawMenuButton(SetupBounds({0,20,44,32}),font,"-",0);
    Ui::DrawMenuButton(SetupBounds({506,20,44,32}),font,"+",0);
    settingLabel(TextFormat("%02d",s.bots),238,14,32,white);
    settingLabel(TextFormat("%d fighters total / up to 15 bots",s.bots+1),108,60,10,muted);
    settingLabel("AI DIFFICULTY",0,82,12,muted);
    for(int i=0;i<3;++i) {
        Rectangle local{i*186.0f,99,174,32},r=SetupBounds(local);
        bool selected=s.difficulty==i,hovered=SetupHit(local,mouse);
        DrawRectangleRounded(r,.5f,10,Fade(gold,selected?.15f:hovered?.09f:.035f));
        Vector2 size=MeasureTextEx(font,Offline::Difficulty(i),14,1);
        label(Offline::Difficulty(i),{r.x+(r.width-size.x)/2,r.y+8},14,selected?gold:muted);
        if(selected) DrawLineEx({r.x+24,r.y+r.height},{r.x+r.width-24,r.y+r.height},2,gold);
    }
    settingLabel(s.difficulty==0?"Slower reactions, forgiving aim, lighter damage.":s.difficulty==1?"Balanced reactions, movement, and damage.":"Fast reactions, accurate fire, aggressive movement.",0,142,10,muted);
    if(Offline::TeamMode(s.mode)) {
        settingLabel("YOUR SIDE",0,178,10,muted);
        const char* teams[]={"GLOBAL RISK / BLUE","BLACK LIST / RED"};
        for(int i=0;i<2;++i) {
            Rectangle r=SetupBounds({i*282.0f,198,268,32});
            Color color=i==0?UiTheme::TeamBlue():UiTheme::TeamRed();
            DrawRectangleRounded(r,.45f,8,Fade(color,s.playerTeam==i+1?.10f:SetupHit({i*282.0f,198,268,32},mouse)?.06f:.02f));
            label(teams[i],{r.x+8,r.y+9},12,s.playerTeam==i+1?color:muted);
            if(s.playerTeam==i+1) DrawLineEx({r.x+8,r.y+r.height},{r.x+r.width-18,r.y+r.height},2,color);
        }
    } else settingLabel(s.mode==Offline::Mode::Training?"UNLIMITED RESPAWNS / NO SCORE LIMIT":"SOLO / EVERY BOT IS AN OPPONENT",0,181,12,muted);
    if(s.mode!=Offline::Mode::Training) {
        settingLabel(s.mode==Offline::Mode::Deathmatch?"CAPTURES TO WIN / 5 MINUTE LIMIT":Offline::RoundMode(s.mode)?"ROUNDS TO WIN":"KILLS TO WIN / 5 MINUTE LIMIT",0,258,10,muted);
        const int kills[]={10,20,40},rounds[]={3,5,8};
        for(int i=0;i<3;++i) {
            int value=(Offline::RoundMode(s.mode)||s.mode==Offline::Mode::Deathmatch)?rounds[i]:kills[i];
            bool selected=value==((Offline::RoundMode(s.mode)||s.mode==Offline::Mode::Deathmatch)?s.roundLimit:s.killLimit);
            Rectangle r=SetupBounds({i*186.0f,278,174,34});
            DrawRectangleRounded(r,.45f,8,Fade(gold,selected?.1f:.025f));
            settingLabel(TextFormat("%d",value),74+i*186.0f,286,17,selected?gold:muted);
            if(selected) DrawLineEx({r.x+24,r.y+r.height},{r.x+r.width-24,r.y+r.height},2,gold);
        }
    }
    if(setupScroll>0) DrawRectangleGradientV((int)setupViewport.x,(int)setupViewport.y,(int)setupViewport.width,9,{8,15,23,215},{8,15,23,0});
    if(setupScroll<SetupScrollMax()) DrawRectangleGradientV((int)setupViewport.x,(int)(setupViewport.y+setupViewport.height-9),(int)setupViewport.width,9,{8,15,23,0},{8,15,23,215});
    EndScissorMode();
    if(SetupScrollMax()>0) {
        Rectangle thumb=ScrollThumb();
        DrawRectangleRec({thumb.x+1,setupViewport.y,2,setupViewport.height},Fade(muted,.18f));
        DrawRectangleRounded(thumb,1,6,Fade(gold,draggingScroll?.95f:.62f));
    }
    SetupUi::Action(font,start,"DEPLOY / ENTER",hoverStart);
    label("BACK / ESC",{back.x+hoverBack*4,back.y+8},16,hoverBack>0?gold:muted);
    label(s.mode==Offline::Mode::Deathmatch?TextFormat("Touch your flag / deliver to enemy base / %s drops flag",Settings::KeyLabel(Settings::Interact)):"Choose a mode, set your bots, then deploy.",{646,654},10,muted);
    DrawTextEx(font,"BAGS / STORAGE / L",{84,566},14,1,gold);
    if(loadoutOpen) loadout->Draw(font);
    EndTextureMode();
}
