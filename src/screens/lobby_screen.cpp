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
            nm.UpdateLobbySettings((OnlineGameMode)i,nm.timeLimit,nm.killLimit,nm.roundLimit); rosterScroll[0]=rosterScroll[1]=0;
        }
    }
    for (int i=0;i<2;++i) {
        Rectangle hit{i==0?646.0f:1158.0f,188,38,42};
        hoverMap[i]=Ui::Approach(hoverMap[i],isHost && CheckCollisionPointRec(mouse,hit)?1.0f:0.0f,8,dt);
        if (isHost && Click(hit)) CycleMap(i==0?-1:1);
    }
    if (isHost) {
        bool elimination=nm.currentGameMode==OnlineGameMode::ELIMINATION;
        bool flag=nm.currentGameMode==OnlineGameMode::DEATHMATCH;
        if (!elimination && !flag) {
            if (Click({646,354,268,34}) && nm.timeLimit>0) nm.UpdateLobbySettings(nm.currentGameMode,0,40,nm.roundLimit);
            if (Click({928,354,268,34}) && nm.timeLimit<=0) nm.UpdateLobbySettings(nm.currentGameMode,300,0,nm.roundLimit);
        }
        const int rounds[]={6,8,14},captures[]={3,5,8},kills[]={40,60,100},times[]={300,600,900};
        for (int i=0;i<3;++i) if (Click({646+i*186.0f,(elimination||flag)?354.0f:428.0f,174,34})) {
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
    mapPreview.Prepare(nm.selectedMapName,550,126); background->Draw(target,fade,0.45f);
    BeginTextureMode(target); SetupUi::Backdrop();
    SetupUi::Header(font,"MULTIPLAYER OPERATIONS","ONLINE LOBBY",isHost?"Host controls match settings. Assemble your squad.":"Connected. Choose a team and ready up.");
    const char* modes[]={"ELIMINATION","FREE FOR ALL","TEAM DEATHMATCH","DEATHMATCH / FLAG"};
    const char* descriptions[]={"One life per round. No respawns.","Solo combat. Respawn at assigned outposts.","Team kills win. Respawn at your home base.","Deliver your flag. Fight, respawn, and capture."};
    for (int i=0;i<4;++i) SetupUi::ModeChoice(font,modeRows[i],modes[i],descriptions[i],(int)nm.currentGameMode==i,hoverMode[i],isHost);
    DrawRoster();
    Rectangle mapBounds{646,146,550,126}; mapPreview.Draw(mapBounds,font); SetupUi::MapArrows(mapBounds,isHost,hoverMap[0],hoverMap[1]);
    Color gold=UiTheme::AccentGold(),muted{144,164,182,255};
    DrawCircle(652,298,4,nm.AllPlayersReady()?UiTheme::ReadyGreen():muted);
    DrawTextEx(font,TextFormat("READY %d / %d",nm.GetReadyPlayerCount(),nm.GetTotalPlayerCount()),{666,291},14,1,nm.AllPlayersReady()?UiTheme::ReadyGreen():muted);
    DrawTextEx(font,isHost?"HOST SETTINGS":"HOST CONTROLS SETTINGS",{987,295},9,1,muted);
    bool elimination=nm.currentGameMode==OnlineGameMode::ELIMINATION;
    bool flag=nm.currentGameMode==OnlineGameMode::DEATHMATCH;
    auto chip=[&](Rectangle r,const char* label,bool selected) {
        bool hover=isHost && CheckCollisionPointRec(Ui::RemapMouseToVirtual(),r);
        DrawRectangleRounded(r,0.5f,10,Fade(gold,selected?0.15f:hover?0.07f:0.025f));
        Vector2 size=MeasureTextEx(font,label,14,1); DrawTextEx(font,label,{r.x+(r.width-size.x)/2,r.y+9},14,1,selected?gold:muted);
        if (selected) DrawLineEx({r.x+24,r.y+r.height},{r.x+r.width-24,r.y+r.height},2,gold);
    };
    DrawTextEx(font,flag?"CAPTURES TO WIN":elimination?"ROUNDS TO WIN":"VICTORY CONDITION",{646,330},12,1,muted);
    if (flag) {
        const int captures[]={3,5,8};
        for (int i=0;i<3;++i) chip({646+i*186.0f,354,174,34},TextFormat("%d",captures[i]),nm.roundLimit==captures[i]);
        DrawTextEx(font,"5 minutes / respawn at your base / F drop or take flag",{646,412},10,1,muted);
    } else if (elimination) {
        const int rounds[]={6,8,14};
        for (int i=0;i<3;++i) chip({646+i*186.0f,354,174,34},TextFormat("%d",rounds[i]),nm.roundLimit==rounds[i]);
        DrawTextEx(font,"No respawns until the next round.",{646,412},11,1,muted);
    } else {
        chip({646,354,268,34},"KILLS",nm.timeLimit<=0); chip({928,354,268,34},"TIME",nm.timeLimit>0);
        DrawTextEx(font,nm.timeLimit>0?"MATCH DURATION":"KILLS TO WIN",{646,406},12,1,muted);
        const int kills[]={40,60,100},times[]={300,600,900};
        for (int i=0;i<3;++i) chip({646+i*186.0f,428,174,34},nm.timeLimit>0?TextFormat("%d MIN",times[i]/60):TextFormat("%d",kills[i]),nm.timeLimit>0?nm.timeLimit==times[i]:nm.killLimit==kills[i]);
    }
    SetupUi::Action(font,ready,nm.IsLocalPlayerReady()?"CANCEL READY / R":"READY UP / R",hoverReady,false);
    if (isHost) {
        SetupUi::Action(font,start,"START MATCH / SPACE",hoverStart,true,nm.CanStartGame());
        DrawTextEx(font,lanAddress.c_str(),{646,610},12,1,UiTheme::SkyAccent());
        SetupUi::Action(font,copy,"COPY ROOM ADDRESS / C",hoverCopy,false,!lanAddress.empty());
    } else DrawTextEx(font,nm.IsLocalPlayerReady()?"Waiting for host to deploy...":"Press R when you are ready.",{646,560},14,1,muted);
    DrawTextEx(font,"BACK / ESC",{back.x+hoverBack*4,back.y+5},14,1,hoverBack>0?gold:muted);
    DrawTextEx(font,nm.StartRequirement().c_str(),{76,632},9,1,gold);
    EndTextureMode();
}
