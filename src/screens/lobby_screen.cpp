#include "lobby_screen.h"
#include "../ui/ui_widgets.h"
#include "../ui/ui_theme.h"
#include "../ui/game_settings.h"
#include "../map_loader/MapRegistry.h"
#include <algorithm>
#include <cstring>

LobbyScreen::LobbyScreen() {
    auto& nm=NetworkManager::GetInstance(); isHost=nm.IsHost();
    font=LoadFontEx("assets/fonts/BruceForeverRegular-X3jd2.ttf",60,nullptr,0);
    if (!font.texture.id) font=GetFontDefault();
    background=std::make_unique<MenuBackground>(std::clamp(nm.localSkinIndex,1,4),MenuBackground::BackdropStyle::CINEMATIC_BEACH);
    portraits=std::make_unique<CharacterPreview>();
    if (isHost) lanAddress=nm.GetLocalIPAddress()+":"+std::to_string(DEFAULT_GAME_PORT);
    for (int i=0;i<4;++i) modeRows[i]={76,156.0f+i*60,476,54};
}
LobbyScreen::~LobbyScreen() { if (font.texture.id!=GetFontDefault().texture.id) UnloadFont(font); }
bool LobbyScreen::Click(Rectangle r) const { return r.width>0 && r.height>0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(Ui::RemapMouseToVirtual(),r); }
float LobbyScreen::SetupContentHeight() const {
    auto mode=NetworkManager::GetInstance().currentGameMode;
    return mode==OnlineGameMode::ELIMINATION || mode==OnlineGameMode::DEATHMATCH?110.0f:142.0f;
}
float LobbyScreen::SetupScrollMax() const { return std::max(0.0f,SetupContentHeight()-setupViewport.height); }
void LobbyScreen::ScrollSetup(float amount) { setupScroll=std::clamp(setupScroll+amount,0.0f,SetupScrollMax()); }
Rectangle LobbyScreen::SetupBounds(Rectangle r) const { return {setupViewport.x+r.x,setupViewport.y+r.y-setupScroll,r.width,r.height}; }
Rectangle LobbyScreen::ScrollThumb() const {
    float height=std::max(28.0f,setupViewport.height*setupViewport.height/SetupContentHeight());
    return {setupViewport.x+setupViewport.width+11,setupViewport.y+(setupViewport.height-height)*(SetupScrollMax()>0?setupScroll/SetupScrollMax():0),4,height};
}
bool LobbyScreen::SetupHit(Rectangle r,Vector2 mouse) const {
    return CheckCollisionPointRec(mouse,setupViewport) && CheckCollisionPointRec(mouse,SetupBounds(r));
}
bool LobbyScreen::ClickSetup(Rectangle r) const { return IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && SetupHit(r,Ui::RemapMouseToVirtual()); }
void LobbyScreen::CycleMap(int step) {
    auto& nm=NetworkManager::GetInstance(); auto names=MapRegistry::GetInstance().GetMapNames(); if (names.empty()) return;
    auto it=std::find(names.begin(),names.end(),nm.selectedMapName); int index=it==names.end()?0:(int)(it-names.begin());
    nm.selectedMapName=names[(index+step+(int)names.size())%(int)names.size()];
    PacketMapChanged packet{}; packet.header={PacketType::MAP_CHANGED,nm.GetLocalPlayerID()};
    std::strncpy(packet.mapName,nm.selectedMapName.c_str(),sizeof(packet.mapName)-1); nm.SendPacket(&packet,sizeof(packet),true);
}
void LobbyScreen::RosterLayout() {
    auto& nm=NetworkManager::GetInstance(); bool teams=nm.currentGameMode!=OnlineGameMode::FREE_FOR_ALL;
    const auto& players=nm.GetPlayers(); int count[2]{}, rank[2]{};
    for (int i=0;i<16;++i) switchGR[i]=switchBL[i]={0,0,0,0};
    for (const auto& p:players) if (!p.username.empty() && p.username!="Unknown") ++count[teams && p.teamID==2?1:0];
    for (int col=0;col<2;++col) rosterScroll[col]=std::clamp(rosterScroll[col],0,std::max(0,count[col]-8));
    int visible=0;
    for (int i=0;i<(int)players.size() && i<16;++i) {
        const auto& p=players[i]; if (p.username.empty() || p.username=="Unknown") continue;
        int col=teams?(p.teamID==2?1:0):visible%2;
        int row=teams?rank[col]++-rosterScroll[col]:visible/2; ++visible;
        if (!teams || row<0 || row>=8 || (!isHost && p.peerID!=nm.GetLocalPlayerID())) continue;
        float x=76+col*248.0f,y=458+row*22.0f;
        switchGR[i]={x+182,y,24,20}; switchBL[i]={x+210,y,24,20};
    }
}
bool LobbyScreen::Update(float dt) {
    auto& nm=NetworkManager::GetInstance();
    fade=Ui::Approach(fade,1,2,dt); background->Update(dt,fade); portraits->Update(dt); nm.Update();
    if (!nm.IsConnected()) return false;
    for (const auto& event:nm.GetIncomingEvents()) {
        if (event.data.size()>=sizeof(PacketGameStart) && event.data[0]==(uint8_t)PacketType::GAME_START) {
            PacketGameStart packet{}; std::memcpy(&packet,event.data.data(),sizeof(packet)); packet.mapName[sizeof(packet.mapName)-1]='\0';
            nm.selectedMapName=packet.mapName; startGame=true; return false;
        }
        if (event.data.size()>=sizeof(PacketMapChanged) && event.data[0]==(uint8_t)PacketType::MAP_CHANGED) {
            PacketMapChanged packet{}; std::memcpy(&packet,event.data.data(),sizeof(packet)); packet.mapName[sizeof(packet.mapName)-1]='\0'; nm.selectedMapName=packet.mapName;
        }
    }
    if (Settings::BlocksInput()) return true;
    if(storageOpen) { storage.Update(dt);if(storage.WantsClose()) storageOpen=false;return true; }
    if(IsKeyPressed(KEY_L) || Click({310,644,242,26})) { storage.Open();storageOpen=true;return true; }
    Vector2 mouse=Ui::RemapMouseToVirtual();
    if (IsKeyPressed(KEY_ESCAPE) || Click(back)) { nm.Disconnect(); return false; }
    bool canStart=nm.CanStartGame();
    hoverReady=Ui::Approach(hoverReady,CheckCollisionPointRec(mouse,ready)?1.0f:0.0f,8,dt);
    hoverStart=Ui::Approach(hoverStart,canStart && CheckCollisionPointRec(mouse,start)?1.0f:0.0f,8,dt);
    hoverCopy=Ui::Approach(hoverCopy,CheckCollisionPointRec(mouse,copy)?1.0f:0.0f,8,dt);
    hoverBack=Ui::Approach(hoverBack,CheckCollisionPointRec(mouse,back)?1.0f:0.0f,8,dt);
    if (IsKeyPressed(KEY_R) || Click(ready)) nm.SetPlayerReady(!nm.IsLocalPlayerReady());
    if (isHost && (IsKeyPressed(KEY_C) || Click(copy)) && !lanAddress.empty()) SetClipboardText(lanAddress.c_str());
    if (isHost && canStart && (IsKeyPressed(KEY_SPACE) || Click(start)) && nm.StartGame()) { startGame=true; return false; }
    for (int i=0;i<4;++i) {
        hoverMode[i]=Ui::Approach(hoverMode[i],isHost && CheckCollisionPointRec(mouse,modeRows[i])?1.0f:0.0f,8,dt);
        if (isHost && Click(modeRows[i]) && i!=(int)nm.currentGameMode) {
            nm.UpdateLobbySettings((OnlineGameMode)i,nm.timeLimit,nm.killLimit,nm.roundLimit); rosterScroll[0]=rosterScroll[1]=0; setupScroll=0; draggingScroll=false;
        }
    }
    for (int i=0;i<2;++i) {
        Rectangle hit{i==0?previewBounds.x:previewBounds.x+previewBounds.width-38,previewBounds.y+previewBounds.height/2-21,38,42};
        hoverMap[i]=Ui::Approach(hoverMap[i],isHost && CheckCollisionPointRec(mouse,hit)?1.0f:0.0f,8,dt);
        if (isHost && Click(hit)) CycleMap(i==0?-1:1);
    }
    ScrollSetup(0);
    Rectangle scrollArea{setupViewport.x,setupViewport.y,setupViewport.width+24,setupViewport.height};
    if(CheckCollisionPointRec(mouse,scrollArea)) {
        ScrollSetup(-GetMouseWheelMove()*46);
        if(IsKeyPressed(KEY_HOME)) setupScroll=0;
        if(IsKeyPressed(KEY_END)) setupScroll=SetupScrollMax();
    }
    if(IsKeyPressed(KEY_PAGE_DOWN)) ScrollSetup(setupViewport.height*.8f);
    if(IsKeyPressed(KEY_PAGE_UP)) ScrollSetup(-setupViewport.height*.8f);
    Rectangle thumb=ScrollThumb(),track{thumb.x-6,setupViewport.y,16,setupViewport.height};
    if(SetupScrollMax()>0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mouse,track)) {
        draggingScroll=true;scrollGrab=CheckCollisionPointRec(mouse,thumb)?mouse.y-thumb.y:thumb.height*.5f;
    }
    if(!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) draggingScroll=false;
    if(draggingScroll) {
        float travel=setupViewport.height-thumb.height;
        setupScroll=travel>0?std::clamp((mouse.y-setupViewport.y-scrollGrab)/travel,0.0f,1.0f)*SetupScrollMax():0;
    }
    if (isHost) {
        bool elimination=nm.currentGameMode==OnlineGameMode::ELIMINATION;
        bool flag=nm.currentGameMode==OnlineGameMode::DEATHMATCH;
        if (!elimination && !flag) {
            if (ClickSetup({0,24,268,34}) && nm.timeLimit>0) nm.UpdateLobbySettings(nm.currentGameMode,0,40,nm.roundLimit);
            if (ClickSetup({282,24,268,34}) && nm.timeLimit<=0) nm.UpdateLobbySettings(nm.currentGameMode,300,0,nm.roundLimit);
        }
        const int rounds[]={6,8,14},captures[]={3,5,8},kills[]={40,60,100},times[]={300,600,900};
        for (int i=0;i<3;++i) if (ClickSetup({i*186.0f,(elimination||flag)?24.0f:98.0f,174,34})) {
            if (flag) nm.UpdateLobbySettings(nm.currentGameMode,300,0,captures[i]);
            else if (elimination) nm.UpdateLobbySettings(nm.currentGameMode,nm.timeLimit,nm.killLimit,rounds[i]);
            else if (nm.timeLimit>0) nm.UpdateLobbySettings(nm.currentGameMode,times[i],0,nm.roundLimit);
            else nm.UpdateLobbySettings(nm.currentGameMode,0,kills[i],nm.roundLimit);
        }
    }
    if (CheckCollisionPointRec(mouse,{76,436,476,198}) && nm.currentGameMode!=OnlineGameMode::FREE_FOR_ALL) {
        int col=mouse.x<324?0:1; rosterScroll[col]-=(int)GetMouseWheelMove();
    }
    RosterLayout();
    const auto& players=nm.GetPlayers();
    for (int i=0;i<(int)players.size() && i<16;++i) {
        int team=Click(switchGR[i])?1:Click(switchBL[i])?2:0; if (!team) continue;
        if (isHost) nm.SetPlayerTeam(players[i].peerID,team);
        else if (players[i].peerID==nm.GetLocalPlayerID()) nm.RequestTeamChange(team);
    }
    return true;
}
void LobbyScreen::DrawRoster() {
    auto& nm=NetworkManager::GetInstance(); const auto& players=nm.GetPlayers();
    bool teams=nm.currentGameMode!=OnlineGameMode::FREE_FOR_ALL;
    Color muted{144,164,182,255};
    DrawTextEx(font,TextFormat("SQUAD / %d PLAYERS",nm.GetTotalPlayerCount()),{76,412},13,1,UiTheme::AccentGold());
    DrawTextEx(font,teams?"GLOBAL RISK":"FREE FOR ALL",{76,438},10,1,teams?UiTheme::TeamBlue():muted);
    DrawTextEx(font,teams?"BLACK LIST":"CONNECTED PLAYERS",{324,438},10,1,teams?UiTheme::TeamRed():muted);
    int rank[2]{},drawn[2]{},visible=0;
    RosterLayout();
    for (int i=0;i<(int)players.size() && i<16;++i) {
        const auto& p=players[i]; if (p.username.empty() || p.username=="Unknown") continue;
        int col=teams?(p.teamID==2?1:0):visible%2,row=teams?rank[col]++-rosterScroll[col]:visible/2; ++visible;
        if (row<0 || row>=8) continue;
        float x=76+col*248.0f,y=458+row*22.0f; ++drawn[col];
        if (p.peerID==nm.GetLocalPlayerID()) DrawRectangleRounded({x-2,y,238,20},0.4f,6,Fade(UiTheme::AccentGold(),0.07f));
        portraits->DrawHead(p.charSkin,{x,y+1,18,18});
        std::string name=p.username; if (p.peerID==nm.GetLocalPlayerID()) name+=" / YOU";
        float maxWidth=teams?112:160;
        while (!name.empty() && MeasureTextEx(font,name.c_str(),10,1).x>maxWidth) name.pop_back();
        DrawTextEx(font,name.c_str(),{x+24,y+5},10,1,p.peerID==nm.GetLocalPlayerID()?UiTheme::TextPrimary():muted);
        DrawCircle((int)x+(teams?158:217),(int)y+10,3,p.isReady?UiTheme::ReadyGreen():Fade(muted,0.5f));
        if (teams && (isHost || p.peerID==nm.GetLocalPlayerID())) {
            Color colors[]={UiTheme::TeamBlue(),UiTheme::TeamRed()};
            for (int t=0;t<2;++t) {
                Rectangle r=t==0?switchGR[i]:switchBL[i];
                if (p.teamID==t+1) DrawLineEx({r.x,r.y+r.height},{r.x+r.width,r.y+r.height},1,colors[t]);
                DrawTextEx(font,t==0?"GR":"BL",{r.x+2,r.y+6},9,1,p.teamID==t+1?colors[t]:muted);
            }
        }
    }
    for (int col=0;col<2;++col) for (int row=drawn[col];row<4;++row) {
        float x=76+col*248.0f,y=458+row*22.0f;
        DrawCircleLines((int)x+8,(int)y+9,5,Fade(muted,0.25f));
        DrawTextEx(font,"OPEN SLOT",{x+24,y+5},9,1,Fade(muted,0.45f));
    }
}
void LobbyScreen::Draw(RenderTexture2D target) {
    auto& nm=NetworkManager::GetInstance();
    mapPreview.Prepare(nm.selectedMapName,(int)previewBounds.width,(int)previewBounds.height); background->Draw(target,fade,0.45f);
    BeginTextureMode(target); SetupUi::Backdrop();
    SetupUi::Header(font,"MULTIPLAYER OPERATIONS","ONLINE LOBBY",isHost?"Host controls match settings. Assemble your squad.":"Connected. Choose a team and ready up.");
    const char* modes[]={"ELIMINATION","FREE FOR ALL","TEAM DEATHMATCH","DEATHMATCH / FLAG"};
    const char* descriptions[]={"One life per round. No respawns.","Random spawns. Contest special pickups.","Team kills win. Respawn at your home base.","Deliver your flag. Fight, respawn, and capture."};
    for (int i=0;i<4;++i) SetupUi::ModeChoice(font,modeRows[i],modes[i],descriptions[i],(int)nm.currentGameMode==i,hoverMode[i],isHost);
    DrawRoster();
    Rectangle mapBounds=previewBounds; mapPreview.Draw(mapBounds,font); SetupUi::MapArrows(mapBounds,isHost,hoverMap[0],hoverMap[1]);
    Color gold=UiTheme::AccentGold(),muted{144,164,182,255};
    DrawCircle(652,411,4,nm.AllPlayersReady()?UiTheme::ReadyGreen():muted);
    DrawTextEx(font,TextFormat("READY %d / %d",nm.GetReadyPlayerCount(),nm.GetTotalPlayerCount()),{666,404},14,1,nm.AllPlayersReady()?UiTheme::ReadyGreen():muted);
    DrawTextEx(font,isHost?"HOST SETTINGS":"HOST CONTROLS SETTINGS",{987,408},9,1,muted);
    bool elimination=nm.currentGameMode==OnlineGameMode::ELIMINATION;
    bool flag=nm.currentGameMode==OnlineGameMode::DEATHMATCH;
    BeginScissorMode((int)setupViewport.x,(int)setupViewport.y,(int)setupViewport.width,(int)setupViewport.height);
    auto label=[&](const char* text,float y,int size) { Rectangle r=SetupBounds({0,y,0,0});DrawTextEx(font,text,{r.x,r.y},size,1,muted); };
    auto chip=[&](Rectangle local,const char* label,bool selected) {
        bool hover=isHost && SetupHit(local,Ui::RemapMouseToVirtual());
        Rectangle r=SetupBounds(local);
        DrawRectangleRounded(r,0.5f,10,Fade(gold,selected?0.15f:hover?0.07f:0.025f));
        Vector2 size=MeasureTextEx(font,label,14,1); DrawTextEx(font,label,{r.x+(r.width-size.x)/2,r.y+9},14,1,selected?gold:muted);
        if (selected) DrawLineEx({r.x+24,r.y+r.height},{r.x+r.width-24,r.y+r.height},2,gold);
    };
    label(flag?"CAPTURES TO WIN":elimination?"ROUNDS TO WIN":"VICTORY CONDITION",0,12);
    if (flag) {
        const int captures[]={3,5,8};
        for (int i=0;i<3;++i) chip({i*186.0f,24,174,34},TextFormat("%d",captures[i]),nm.roundLimit==captures[i]);
        label("5 minutes / 8s base respawn / F drop or take flag",82,10);
    } else if (elimination) {
        const int rounds[]={6,8,14};
        for (int i=0;i<3;++i) chip({i*186.0f,24,174,34},TextFormat("%d",rounds[i]),nm.roundLimit==rounds[i]);
        label("No respawns until the next round.",82,11);
    } else {
        chip({0,24,268,34},"KILLS",nm.timeLimit<=0); chip({282,24,268,34},"TIME",nm.timeLimit>0);
        label(nm.timeLimit>0?"MATCH DURATION":"KILLS TO WIN",76,12);
        const int kills[]={40,60,100},times[]={300,600,900};
        for (int i=0;i<3;++i) chip({i*186.0f,98,174,34},nm.timeLimit>0?TextFormat("%d MIN",times[i]/60):TextFormat("%d",kills[i]),nm.timeLimit>0?nm.timeLimit==times[i]:nm.killLimit==kills[i]);
    }
    EndScissorMode();
    if(SetupScrollMax()>0) {
        Rectangle thumb=ScrollThumb();
        DrawRectangle((int)thumb.x,(int)setupViewport.y,4,(int)setupViewport.height,Fade(muted,.15f));
        DrawRectangleRec(thumb,Fade(gold,.7f));
    }
    SetupUi::Action(font,ready,nm.IsLocalPlayerReady()?"CANCEL READY / R":"READY UP / R",hoverReady,false);
    if (isHost) {
        SetupUi::Action(font,start,"START MATCH / SPACE",hoverStart,true,nm.CanStartGame());
        DrawTextEx(font,lanAddress.c_str(),{646,656},12,1,UiTheme::SkyAccent());
        SetupUi::Action(font,copy,"COPY ROOM ADDRESS / C",hoverCopy,false,!lanAddress.empty());
    } else DrawTextEx(font,nm.IsLocalPlayerReady()?"Waiting for host to deploy...":"Press R when you are ready.",{646,610},14,1,muted);
    DrawTextEx(font,"BACK / ESC",{back.x+hoverBack*4,back.y+5},14,1,hoverBack>0?gold:muted);
    DrawTextEx(font,nm.StartRequirement().c_str(),{76,632},9,1,gold);
    DrawTextEx(font,"BAGS / STORAGE / L",{320,650},12,1,gold);
    if(storageOpen) storage.Draw(font);
    EndTextureMode();
}
