#include "lobby_screen.h"
#include "../constants.h"
#include "../ui/ui_widgets.h"
#include "../ui/ui_theme.h"
#include "../ui/map_catalog.h"
#include "../map_loader/MapRegistry.h"
#include "../map_loader/raytmx.h"
#include <cstring>
#include <algorithm>

LobbyScreen::LobbyScreen() :
    startGame(false),
    isHost(NetworkManager::GetInstance().IsHost()),
    playerName(NetworkManager::GetInstance().localUsername),
    playerSkin(NetworkManager::GetInstance().localSkinIndex),
    background(nullptr),
    preview(nullptr),
    fadeIn(0.0f),
    blurStrength(0.75f),
    hoverReady(0.0f),
    hoverStart(0.0f),
    hoverCopy(0.0f),
    hoverGlobalBack(0.0f),
    optionScroll(0.0f),
    optionScrollMax(0.0f) {

    menuFont = LoadFontEx("assets/fonts/BruceForeverRegular-X3jd2.ttf", 60, nullptr, 0);
    if (menuFont.texture.id == 0) menuFont = GetFontDefault();
    choiceSound = LoadSound("assets/sounds/choice.wav");

    if (isHost) {
        lanAddress = NetworkManager::GetInstance().GetLocalIPAddress() + ":" +
                     std::to_string(DEFAULT_GAME_PORT);
    }

    background = new MenuBackground(playerSkin, MenuBackground::BackdropStyle::CINEMATIC_BEACH);
    preview = new CharacterPreview();

    shell       = { 30.0f,  40.0f, 1240.0f, 620.0f };
    rosterPanel = { 60.0f, 120.0f,  630.0f, 510.0f };

    // Right-column layout.
    const float rightX  = 752.0f;
    const float rightW  = 474.0f;
    const float cardGap = 10.0f;
    const float arrowW  = 42.0f;

    // ── MAP section ──────────────────────────────
    const float mapRowY = 136.0f;
    const float mapRowH = 32.0f;
    prevMapBtn = { rightX,                     mapRowY, arrowW,               mapRowH };
    mapDisplay = { rightX + arrowW + 4.0f,     mapRowY, rightW - arrowW*2-8, mapRowH };
    nextMapBtn = { rightX + rightW - arrowW,   mapRowY, arrowW,               mapRowH };
    mapPreview = { rightX,                     172.0f,  rightW,               180.0f  };
    hoverPrevMap = 0.0f;
    hoverNextMap = 0.0f;

    // ── MODE section ──────────────────────────────
    const float modeRowY = 384.0f;
    const float modeRowH = 34.0f;
    prevModeBtn = { rightX,                     modeRowY, arrowW,               modeRowH };
    modeDisplay = { rightX + arrowW + 4.0f,     modeRowY, rightW - arrowW*2-8, modeRowH };
    nextModeBtn = { rightX + rightW - arrowW,   modeRowY, arrowW,               modeRowH };
    hoverPrevMode = 0.0f;
    hoverNextMode = 0.0f;

    // ── CHIP rows ────────────────────────────────
    const float chipH  = 28.0f;
    const float chipW3 = (rightW - cardGap * 2.0f) / 3.0f;
    const float chipW2 = (rightW - cardGap) / 2.0f;
    const float chipY0 = 448.0f;
    const float chipY1 = 508.0f;
    
    for (int i = 0; i < 3; ++i) {
        chipWin[i]   = { rightX + i * (chipW3 + cardGap), chipY0, chipW3, chipH };
        chipLimit[i] = { rightX + i * (chipW3 + cardGap), chipY1, chipW3, chipH };
        hoverChipWin[i]   = 0.0f;
        hoverChipLimit[i] = 0.0f;
    }
    for (int i = 0; i < 2; ++i) {
        chipGoal[i]      = { rightX + i * (chipW2 + cardGap), chipY0, chipW2, chipH };
        hoverChipGoal[i] = 0.0f;
    }

    // ── Action buttons ───────────────────────────
    readyBtn = { rightX, 558.0f, rightW, 40.0f };
    startBtn = { rightX, 606.0f, rightW, 40.0f };
    copyBtn  = { rightX, 668.0f, rightW, 32.0f };
    optionScrollMax = 80.0f;

    for (int i = 0; i < MAX_LOBBY_PLAYERS; ++i) {
        switchGR[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        switchBL[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        hoverSwitchGR[i] = 0.0f;
        hoverSwitchBL[i] = 0.0f;
    }

    cachedPreview = LoadRenderTexture((int)mapPreview.width, (int)mapPreview.height);
    cachedPreviewName = "";
}

LobbyScreen::~LobbyScreen() {
    UnloadSound(choiceSound);
    UnloadRenderTexture(cachedPreview);
    delete preview;
    delete background;
    if (menuFont.texture.id != GetFontDefault().texture.id) {
        UnloadFont(menuFont);
    }
}


bool LobbyScreen::Update(float deltaTime) {
    fadeIn = Ui::Approach(fadeIn, 1.0f, 2.0f, deltaTime);
    blurStrength = Ui::Approach(blurStrength, 0.45f, 0.4f, deltaTime);

    if (background) background->Update(deltaTime, fadeIn);
    if (preview) preview->Update(deltaTime);

    NetworkManager::GetInstance().Update();

    std::string currentMap = NetworkManager::GetInstance().selectedMapName;
    MapData* md = MapRegistry::GetInstance().GetMap(currentMap);
    if (md && md->pixelWidth > 0) {
        float desiredHeight = mapPreview.width * ((float)md->pixelHeight / (float)md->pixelWidth);
        if (desiredHeight < 64.0f) desiredHeight = 64.0f;
        // Cap max height so it doesn't take over the entire scroll view infinitely
        if (desiredHeight > 400.0f) desiredHeight = 400.0f; 
        
        if (std::abs(mapPreview.height - desiredHeight) > 0.5f) {
            float diff = desiredHeight - mapPreview.height;
            mapPreview.height = desiredHeight;
            
            UnloadRenderTexture(cachedPreview);
            cachedPreview = LoadRenderTexture((int)mapPreview.width, (int)mapPreview.height);
            cachedPreviewName = ""; // Force redraw
            
            modeDisplay.y += diff;
            prevModeBtn.y += diff;
            nextModeBtn.y += diff;
            for (int i=0; i<3; ++i) {
                chipWin[i].y += diff;
                chipLimit[i].y += diff;
            }
            for (int i=0; i<2; ++i) chipGoal[i].y += diff;
            
            readyBtn.y += diff;
            startBtn.y += diff;
            copyBtn.y += diff;
            
            optionScrollMax += diff;
            if (optionScrollMax < 0.0f) optionScrollMax = 0.0f;
        }
    }
    bool canStart = isHost && NetworkManager::GetInstance().AllPlayersReady();

    if (IsKeyPressed(KEY_ESCAPE)) {
        NetworkManager::GetInstance().Disconnect();
        return false;
    }

    // Keep the back button in the top margin, outside the lobby shell.
    Rectangle globalBackBtn = { 28.0f, 4.0f, 168.0f, 32.0f };
    bool hBack = CheckCollisionPointRec(Ui::RemapMouseToVirtual(), globalBackBtn);
    hoverGlobalBack = Ui::Approach(hoverGlobalBack, hBack ? 1.0f : 0.0f, 8.0f, deltaTime);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hBack) {
        PlaySound(choiceSound);
        NetworkManager::GetInstance().Disconnect();
        return false;
    }


    if (isHost && IsKeyPressed(KEY_C) && !lanAddress.empty()) {
        SetClipboardText(lanAddress.c_str());
    }

    if (IsKeyPressed(KEY_R)) {
        bool currentReady = NetworkManager::GetInstance().IsLocalPlayerReady();
        NetworkManager::GetInstance().SetPlayerReady(!currentReady);
    }

    if (IsKeyPressed(KEY_SPACE) && isHost && canStart) {
        NetworkManager::GetInstance().StartGame();
        startGame = true;
        return false;
    }

    Vector2 mouse = Ui::RemapMouseToVirtual();
    float viewportY = shell.y + 78.0f;
    Rectangle optionViewport = { 736.0f, viewportY, 506.0f, (shell.y + shell.height - 12.0f) - viewportY };
    if (CheckCollisionPointRec(mouse, optionViewport)) {
        optionScroll -= GetMouseWheelMove() * 24.0f;
        if (optionScroll < 0.0f) optionScroll = 0.0f;
        if (optionScroll > optionScrollMax) optionScroll = optionScrollMax;
    }
    Vector2 optionMouse = mouse;
    optionMouse.y += optionScroll;
    bool hReady = CheckCollisionPointRec(optionMouse, readyBtn);
    bool hStart = canStart && CheckCollisionPointRec(optionMouse, startBtn);
    bool hCopy  = isHost && CheckCollisionPointRec(optionMouse, copyBtn);

    hoverReady = Ui::Approach(hoverReady, hReady ? 1.0f : 0.0f, 8.0f, deltaTime);
    hoverStart = Ui::Approach(hoverStart, hStart ? 1.0f : 0.0f, 8.0f, deltaTime);
    hoverCopy  = Ui::Approach(hoverCopy,  hCopy  ? 1.0f : 0.0f, 8.0f, deltaTime);

    // Map cycle selector hover animations
    bool hPrevMap = isHost && CheckCollisionPointRec(optionMouse, prevMapBtn);
    bool hNextMap = isHost && CheckCollisionPointRec(optionMouse, nextMapBtn);
    hoverPrevMap = Ui::Approach(hoverPrevMap, hPrevMap ? 1.0f : 0.0f, 8.0f, deltaTime);
    hoverNextMap = Ui::Approach(hoverNextMap, hNextMap ? 1.0f : 0.0f, 8.0f, deltaTime);

    // Mode cycle selector hover animations
    bool hPrev = isHost && CheckCollisionPointRec(optionMouse, prevModeBtn);
    bool hNext = isHost && CheckCollisionPointRec(optionMouse, nextModeBtn);
    hoverPrevMode = Ui::Approach(hoverPrevMode, hPrev ? 1.0f : 0.0f, 8.0f, deltaTime);
    hoverNextMode = Ui::Approach(hoverNextMode, hNext ? 1.0f : 0.0f, 8.0f, deltaTime);

    // Chip hover animations
    for (int i = 0; i < 3; ++i) {
        bool hovW = isHost && CheckCollisionPointRec(optionMouse, chipWin[i]);
        bool hovL = isHost && CheckCollisionPointRec(optionMouse, chipLimit[i]);
        hoverChipWin[i]   = Ui::Approach(hoverChipWin[i],   hovW ? 1.0f : 0.0f, 8.0f, deltaTime);
        hoverChipLimit[i] = Ui::Approach(hoverChipLimit[i], hovL ? 1.0f : 0.0f, 8.0f, deltaTime);
    }
    for (int i = 0; i < 2; ++i) {
        bool hovG = isHost && CheckCollisionPointRec(optionMouse, chipGoal[i]);
        hoverChipGoal[i] = Ui::Approach(hoverChipGoal[i], hovG ? 1.0f : 0.0f, 8.0f, deltaTime);
    }
    // Per-player team-switch pill hovers (only for rows the user can actually click).
    const auto& players = NetworkManager::GetInstance().GetPlayers();
    uint32_t myPid = NetworkManager::GetInstance().GetLocalPlayerID();
    for (int i = 0; i < (int)players.size() && i < MAX_LOBBY_PLAYERS; ++i) {
        if (players[i].username.empty() || players[i].username == "Unknown") continue;
        bool canSwitch = isHost || (players[i].peerID == myPid);
        bool hGR = canSwitch && CheckCollisionPointRec(mouse, switchGR[i]);
        bool hBL = canSwitch && CheckCollisionPointRec(mouse, switchBL[i]);
        hoverSwitchGR[i] = Ui::Approach(hoverSwitchGR[i], hGR ? 1.0f : 0.0f, 8.0f, deltaTime);
        hoverSwitchBL[i] = Ui::Approach(hoverSwitchBL[i], hBL ? 1.0f : 0.0f, 8.0f, deltaTime);
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        bool playedSound = false;
        if (hReady) {
            bool currentReady = NetworkManager::GetInstance().IsLocalPlayerReady();
            NetworkManager::GetInstance().SetPlayerReady(!currentReady);
            if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
        } else if (hStart) {
            NetworkManager::GetInstance().StartGame();
            startGame = true;
            if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
            return false;
        } else if (hCopy && !lanAddress.empty()) {
            SetClipboardText(lanAddress.c_str());
            if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
        }

        if (isHost) {
            // Map cycle selector clicks
            auto mapNames = MapRegistry::GetInstance().GetMapNames();
            if (!mapNames.empty()) {
                auto& nm = NetworkManager::GetInstance();
                int curIdx = 0;
                for (int i = 0; i < (int)mapNames.size(); ++i) {
                    if (mapNames[i] == nm.selectedMapName) {
                        curIdx = i;
                        break;
                    }
                }
                
                int newIdx = curIdx;
                if (CheckCollisionPointRec(optionMouse, prevMapBtn)) {
                    newIdx = (curIdx + mapNames.size() - 1) % mapNames.size();
                } else if (CheckCollisionPointRec(optionMouse, nextMapBtn)) {
                    newIdx = (curIdx + 1) % mapNames.size();
                }
                
                if (newIdx != curIdx) {
                    nm.selectedMapName = mapNames[newIdx];
                    if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                    PacketMapChanged pkt{};
                    pkt.header.type = PacketType::MAP_CHANGED;
                    pkt.header.playerID = nm.GetLocalPlayerID();
                    std::strncpy(pkt.mapName, mapNames[newIdx].c_str(), sizeof(pkt.mapName) - 1);
                    pkt.mapName[sizeof(pkt.mapName) - 1] = '\0';
                    nm.SendPacket(&pkt, sizeof(pkt), true);
                }
            }

            // Mode cycle selector clicks: < and > cycle through modes
            auto& nm = NetworkManager::GetInstance();
            int curMode = (int)nm.currentGameMode;
            if (CheckCollisionPointRec(optionMouse, prevModeBtn)) {
                int newMode = (curMode + 2) % 3; // go backwards
                if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                nm.UpdateLobbySettings((OnlineGameMode)newMode, nm.timeLimit, nm.killLimit, nm.roundLimit);
            } else if (CheckCollisionPointRec(optionMouse, nextModeBtn)) {
                int newMode = (curMode + 1) % 3; // go forwards
                if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                nm.UpdateLobbySettings((OnlineGameMode)newMode, nm.timeLimit, nm.killLimit, nm.roundLimit);
            }
            // Chip clicks — only the row matching the current mode is visible (others are
            // not drawn, but be defensive in case of overlap during transition).
            if (nm.currentGameMode == OnlineGameMode::ELIMINATION) {
                const int rounds[3] = { 6, 8, 14 };
                for (int i = 0; i < 3; ++i) {
                    if (CheckCollisionPointRec(optionMouse, chipWin[i]) && nm.roundLimit != rounds[i]) {
                        if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                        nm.UpdateLobbySettings(nm.currentGameMode, nm.timeLimit, nm.killLimit, rounds[i]);
                        break;
                    }
                }
            } else {
                bool isTimeGoal = (nm.timeLimit > 0);

                // Goal Type selection (first row: chipGoal)
                if (CheckCollisionPointRec(optionMouse, chipGoal[0]) && isTimeGoal) {
                    if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                    nm.UpdateLobbySettings(nm.currentGameMode, 0, 40, nm.roundLimit);
                } else if (CheckCollisionPointRec(optionMouse, chipGoal[1]) && !isTimeGoal) {
                    if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                    nm.UpdateLobbySettings(nm.currentGameMode, 300, 0, nm.roundLimit);
                }

                // Limit selection (second row: chipLimit)
                if (isTimeGoal) {
                    const int times[3] = { 300, 600, 900 };   // 5m / 10m / 15m in seconds
                    for (int i = 0; i < 3; ++i) {
                        if (CheckCollisionPointRec(optionMouse, chipLimit[i]) && nm.timeLimit != times[i]) {
                            if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                            nm.UpdateLobbySettings(nm.currentGameMode, times[i], 0, nm.roundLimit);
                            break;
                        }
                    }
                } else {
                    const int kills[3] = { 40, 60, 100 };
                    for (int i = 0; i < 3; ++i) {
                        if (CheckCollisionPointRec(optionMouse, chipLimit[i]) && nm.killLimit != kills[i]) {
                            if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                            nm.UpdateLobbySettings(nm.currentGameMode, 0, kills[i], nm.roundLimit);
                            break;
                        }
                    }
                }
            }

            // Team-switch pills (host can move anyone).
            const auto& players = nm.GetPlayers();
            uint32_t myPid = nm.GetLocalPlayerID();
            for (int i = 0; i < (int)players.size() && i < MAX_LOBBY_PLAYERS; ++i) {
                if (players[i].username.empty() || players[i].username == "Unknown") continue;
                if (CheckCollisionPointRec(mouse, switchGR[i])) {
                    if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                    nm.SetPlayerTeam(players[i].peerID, 1);
                } else if (CheckCollisionPointRec(mouse, switchBL[i])) {
                    if (!playedSound) { PlaySound(choiceSound); playedSound = true; }
                    nm.SetPlayerTeam(players[i].peerID, 2);
                }
            }
            (void)myPid; // silence unused warning if all players valid
        } else {
            // Non-host: clients may only move themselves. Find the local player index
            // by matching peerID. We mirror the host pill layout so the same rectangles
            // are reused for hover/draw.
            auto& nm = NetworkManager::GetInstance();
            const auto& players = nm.GetPlayers();
            uint32_t myPid = nm.GetLocalPlayerID();
            int localIdx = -1;
            for (int i = 0; i < (int)players.size() && i < MAX_LOBBY_PLAYERS; ++i) {
                if (players[i].peerID == myPid) { localIdx = i; break; }
            }
            if (localIdx >= 0) {
                if (CheckCollisionPointRec(mouse, switchGR[localIdx])) {
                    nm.RequestTeamChange(1);
                } else if (CheckCollisionPointRec(mouse, switchBL[localIdx])) {
                    nm.RequestTeamChange(2);
                }
            }
        }
    }

    auto events = NetworkManager::GetInstance().GetIncomingEvents();
    for (const auto& event : events) {
        if (event.data.size() >= sizeof(PacketType)) {
            PacketType packetType = static_cast<PacketType>(event.data[0]);
            if (packetType == PacketType::GAME_START && event.data.size() >= sizeof(PacketGameStart)) {
                PacketGameStart pkt;
                std::memcpy(&pkt, event.data.data(), sizeof(PacketGameStart));
                NetworkManager::GetInstance().selectedMapName = pkt.mapName;
                startGame = true;
                return false;
            } else if (packetType == PacketType::MAP_CHANGED && event.data.size() >= sizeof(PacketMapChanged)) {
                PacketMapChanged pkt;
                std::memcpy(&pkt, event.data.data(), sizeof(PacketMapChanged));
                NetworkManager::GetInstance().selectedMapName = pkt.mapName;
            }
            // LOBBY_SETTINGS is already applied inside NetworkManager::Update(); the
            // next frame's redraw will pick up the new values.
        }
    }

    return true;
}

void LobbyScreen::DrawRoster() {
    DrawRectangleRec(rosterPanel, Fade(UiTheme::PanelBg(), 0.72f));
    DrawRectangleLinesEx(rosterPanel, 1.5f, Fade(UiTheme::PanelBorder(), 0.85f));
    DrawRectangle((int)rosterPanel.x, (int)rosterPanel.y, 5, (int)rosterPanel.height,
                  Fade(UiTheme::SkyAccent(), 0.75f));
    DrawLineEx({ rosterPanel.x + 18.0f, rosterPanel.y + 43.0f },
               { rosterPanel.x + rosterPanel.width - 18.0f, rosterPanel.y + 43.0f },
               1.0f, Fade(UiTheme::PanelBorder(), 0.55f));

    const auto& players = NetworkManager::GetInstance().GetPlayers();
    OnlineGameMode mode = NetworkManager::GetInstance().currentGameMode;
    bool teamMode = (mode == OnlineGameMode::ELIMINATION || mode == OnlineGameMode::TEAM_DEATHMATCH);

    if (!teamMode) {
        // ---- Single-column FFA roster (legacy layout) ----
        Ui::DrawSectionLabel(menuFont, "PLAYERS", { rosterPanel.x + 20, rosterPanel.y + 16 }, 20.0f);
        float y = rosterPanel.y + 56.0f;
        float rowH = 72.0f;
        int drawn = 0;
        for (const auto& player : players) {
            if (player.username.empty() || player.username == "Unknown") continue;
            bool isLocal = (player.peerID == NetworkManager::GetInstance().GetLocalPlayerID());
            bool isHostPlayer = (player.peerID == 0);

            Rectangle row = { rosterPanel.x + 16, y, rosterPanel.width - 32, rowH - 10 };
            DrawRectangleRec(row, Fade(UiTheme::ButtonIdle(), isLocal ? 0.95f : 0.7f));
            DrawRectangleLinesEx(row, 1.0f, Fade(isLocal ? UiTheme::SkyAccent() : UiTheme::PanelBorder(), 0.5f));
            if (isLocal) {
                DrawRectangle((int)row.x, (int)row.y, 4, (int)row.height, UiTheme::SkyAccent());
            }

            Rectangle head = { row.x + 12, row.y + 10, 48, 48 };
            if (preview) preview->DrawHead(player.charSkin, head);
            DrawRectangleLinesEx(head, 1.5f, Fade(UiTheme::AccentGold(), 0.4f));

            Color nameCol = isLocal ? UiTheme::SkyAccent() : (isHostPlayer ? UiTheme::AccentGold() : UiTheme::TextPrimary());
            DrawTextEx(menuFont, player.username.c_str(),
                       { row.x + 76, row.y + 14 }, 20.0f, 1.0f, nameCol);

            std::string meta = std::string("Char ") + std::to_string(player.charSkin);
            if (isHostPlayer) meta += "  -  HOST";
            if (isLocal) meta += "  -  YOU";
            DrawTextEx(menuFont, meta.c_str(),
                       { row.x + 76, row.y + 40 }, 12.0f, 1.0f, UiTheme::TextMuted());

            const char* readyLabel = player.isReady ? "READY" : "WAIT";
            Color pill = player.isReady ? UiTheme::ReadyGreen() : Fade(UiTheme::TextMuted(), 0.55f);
            Vector2 psz = MeasureTextEx(menuFont, readyLabel, 14.0f, 1.0f);
            Rectangle pillR = { row.x + row.width - psz.x - 28, row.y + row.height * 0.5f - 14, psz.x + 20, 28 };
            DrawRectangleRec(pillR, Fade(pill, 0.25f));
            DrawRectangleLinesEx(pillR, 1.5f, pill);
            DrawTextEx(menuFont, readyLabel,
                       { pillR.x + 10, pillR.y + 6 }, 14.0f, 1.0f, pill);

            y += rowH;
            drawn++;
            if (y + rowH > rosterPanel.y + rosterPanel.height) break;
        }
        if (drawn == 0) {
            DrawTextEx(menuFont, "Waiting for players...",
                       { rosterPanel.x + 24, rosterPanel.y + 80 }, 18.0f, 1.0f, UiTheme::TextMuted());
        }
        // Clear any stale switch rectangles from a previous team-mode visit.
        for (int i = 0; i < MAX_LOBBY_PLAYERS; ++i) {
            switchGR[i] = switchBL[i] = { 0, 0, 0, 0 };
        }
        return;
    }

    // ---- Two-column team roster (Elimination / TDM) ----
    Ui::DrawSectionLabel(menuFont, "TEAMS", { rosterPanel.x + 20, rosterPanel.y + 16 }, 20.0f);

    const float colGap   = 12.0f;
    const float colW     = (rosterPanel.width - 32.0f - colGap) * 0.5f;
    const float colLeftX = rosterPanel.x + 16.0f;
    const float colRightX = colLeftX + colW + colGap;
    const float colTopY  = rosterPanel.y + 50.0f;
    const float rowH     = 60.0f;

    // Column headers
    DrawTextEx(menuFont, "GLOBAL RISK",
               { colLeftX + 8.0f, colTopY }, 14.0f, 1.0f, UiTheme::TeamBlue());
    DrawTextEx(menuFont, "BLACK LIST",
               { colRightX + 8.0f, colTopY }, 14.0f, 1.0f, UiTheme::TeamRed());
    // Underline each header
    DrawLineEx({ colLeftX,  colTopY + 22.0f }, { colLeftX + colW,  colTopY + 22.0f }, 2.0f, Fade(UiTheme::TeamBlue(), 0.8f));
    DrawLineEx({ colRightX, colTopY + 22.0f }, { colRightX + colW, colTopY + 22.0f }, 2.0f, Fade(UiTheme::TeamRed(), 0.8f));

    float yLeft  = colTopY + 30.0f;
    float yRight = colTopY + 30.0f;
    int drawnGR = 0, drawnBL = 0;

    uint32_t myPid = NetworkManager::GetInstance().GetLocalPlayerID();
    int rowIdx = 0;
    for (int p = 0; p < (int)players.size() && rowIdx < MAX_LOBBY_PLAYERS; ++p) {
        const auto& player = players[p];
        if (player.username.empty() || player.username == "Unknown") continue;

        bool isGR = (player.teamID == 1);
        bool isBL = (player.teamID == 2);
        bool isLocal    = (player.peerID == myPid);
        bool isHostPlayer = (player.peerID == 0);

        // Render unassigned players under GR by default so they don't disappear.
        bool leftColumn  = isGR || (!isBL);
        float colX = leftColumn ? colLeftX : colRightX;
        float& yRef = leftColumn ? yLeft : yRight;
        float yTop = yRef;
        float yMax = rosterPanel.y + rosterPanel.height - 4.0f;
        if (yTop + rowH > yMax) break; // out of room in this column

        Rectangle row = { colX + 4.0f, yTop, colW - 8.0f, rowH - 6.0f };
        Color borderCol = isGR ? UiTheme::TeamBlue()
                               : (isBL ? UiTheme::TeamRed() : UiTheme::PanelBorder());
        DrawRectangleRec(row, Fade(UiTheme::ButtonIdle(), isLocal ? 0.95f : 0.7f));
        DrawRectangleLinesEx(row, 1.0f, Fade(isLocal ? UiTheme::SkyAccent() : borderCol, 0.5f));

        Rectangle head = { row.x + 6, row.y + 6, 40, 40 };
        if (preview) preview->DrawHead(player.charSkin, head);
        DrawRectangleLinesEx(head, 1.5f, Fade(UiTheme::AccentGold(), 0.4f));

        Color nameCol = isLocal ? UiTheme::SkyAccent()
                                : (isHostPlayer ? UiTheme::AccentGold() : UiTheme::TextPrimary());
        // Truncate long names so they fit the narrower team column.
        std::string shown = player.username;
        if ((int)shown.size() > 10) shown = shown.substr(0, 10) + "..";
        DrawTextEx(menuFont, shown.c_str(),
                   { row.x + 52, row.y + 8 }, 16.0f, 1.0f, nameCol);
        std::string meta = std::string("C") + std::to_string(player.charSkin);
        if (isHostPlayer) meta += " HOST";
        if (isLocal) meta += " YOU";
        DrawTextEx(menuFont, meta.c_str(),
                   { row.x + 52, row.y + 30 }, 11.0f, 1.0f, UiTheme::TextMuted());

        // Ready pill
        const char* readyLabel = player.isReady ? "READY" : "WAIT";
        Color pill = player.isReady ? UiTheme::ReadyGreen() : Fade(UiTheme::TextMuted(), 0.55f);
        Vector2 psz = MeasureTextEx(menuFont, readyLabel, 11.0f, 1.0f);
        Rectangle pillR = { row.x + row.width - psz.x - 14, row.y + row.height * 0.5f - 11, psz.x + 12, 22 };
        DrawRectangleRec(pillR, Fade(pill, 0.25f));
        DrawRectangleLinesEx(pillR, 1.2f, pill);
        DrawTextEx(menuFont, readyLabel,
                   { pillR.x + 6, pillR.y + 4 }, 11.0f, 1.0f, pill);

        // Team-switch pills (host: anyone; client: only own row).
        bool canMove = isHost || isLocal;
        float pillW = 24.0f, pillH = 18.0f;
        float pillY = row.y + row.height - pillH - 4.0f;
        Rectangle grBtn = { row.x + row.width - 64.0f, pillY, pillW, pillH };
        Rectangle blBtn = { row.x + row.width - 36.0f, pillY, pillW, pillH };

        Color grIdle = Fade(UiTheme::TeamBlue(), 0.30f);
        Color blIdle = Fade(UiTheme::TeamRed(),  0.30f);
        Color grBorder = canMove ? UiTheme::TeamBlue() : Fade(UiTheme::TeamBlue(), 0.20f);
        Color blBorder = canMove ? UiTheme::TeamRed()  : Fade(UiTheme::TeamRed(),  0.20f);

        DrawRectangleRec(grBtn, Fade(grIdle, canMove ? 1.0f : 0.4f));
        DrawRectangleLinesEx(grBtn, 1.2f, Fade(grBorder, 0.6f + hoverSwitchGR[rowIdx] * 0.4f));
        DrawTextEx(menuFont, "GR", { grBtn.x + 5, grBtn.y + 3 }, 11.0f, 1.0f,
                   canMove ? UiTheme::TeamBlue() : Fade(UiTheme::TeamBlue(), 0.4f));
        DrawRectangleRec(blBtn, Fade(blIdle, canMove ? 1.0f : 0.4f));
        DrawRectangleLinesEx(blBtn, 1.2f, Fade(blBorder, 0.6f + hoverSwitchBL[rowIdx] * 0.4f));
        DrawTextEx(menuFont, "BL", { blBtn.x + 6, blBtn.y + 3 }, 11.0f, 1.0f,
                   canMove ? UiTheme::TeamRed() : Fade(UiTheme::TeamRed(), 0.4f));

        switchGR[rowIdx] = canMove ? grBtn : Rectangle{0,0,0,0};
        switchBL[rowIdx] = canMove ? blBtn : Rectangle{0,0,0,0};

        if (leftColumn) { yLeft += rowH; drawnGR++; }
        else            { yRight += rowH; drawnBL++; }
        rowIdx++;
    }
    // Hide any leftover pill rectangles from previous frames.
    for (int i = rowIdx; i < MAX_LOBBY_PLAYERS; ++i) {
        switchGR[i] = switchBL[i] = { 0, 0, 0, 0 };
    }
    if (drawnGR + drawnBL == 0) {
        DrawTextEx(menuFont, "Waiting for players...",
                   { rosterPanel.x + 24, rosterPanel.y + 80 }, 18.0f, 1.0f, UiTheme::TextMuted());
    }
}

void LobbyScreen::DrawModeSelector() {
    Ui::DrawSectionLabel(menuFont, "MODE", { 758.0f, modeDisplay.y - 20.0f }, 13.0f);

    const char* modeNames[3] = { "ELIMINATION", "FREE FOR ALL", "TEAM DM" };
    int activeMode = (int)NetworkManager::GetInstance().currentGameMode;
    const char* modeName = modeNames[activeMode];

    // Left arrow button [ < ]
    Color prevFill = Fade(UiTheme::ButtonIdle(), 1.0f);
    if (hoverPrevMode > 0.01f) prevFill = Fade(UiTheme::ButtonHover(), hoverPrevMode);
    DrawRectangleRec(prevModeBtn, prevFill);
    DrawRectangleLinesEx(prevModeBtn, 1.5f, Fade(UiTheme::PanelBorder(), 0.6f + hoverPrevMode * 0.4f));
        Color prevArrowColor = isHost ? UiTheme::TextPrimary() : UiTheme::TextMuted();
        float prevArrowX = prevModeBtn.x + prevModeBtn.width * 0.5f;
        float prevArrowY = prevModeBtn.y + prevModeBtn.height * 0.5f;
        DrawLineEx({ prevArrowX + 4.0f, prevArrowY - 6.0f }, { prevArrowX - 3.0f, prevArrowY }, 2.0f, prevArrowColor);
        DrawLineEx({ prevArrowX - 3.0f, prevArrowY }, { prevArrowX + 4.0f, prevArrowY + 6.0f }, 2.0f, prevArrowColor);

    // Center display
    DrawRectangleRec(modeDisplay, Fade(UiTheme::AccentGold(), 0.15f));
    DrawRectangleLinesEx(modeDisplay, 2.0f, Fade(UiTheme::AccentGold(), 0.8f));
    // Gold left accent bar
    DrawRectangle((int)modeDisplay.x, (int)modeDisplay.y, 4, (int)modeDisplay.height, UiTheme::AccentGold());
    Vector2 nameSz = MeasureTextEx(menuFont, modeName, 16.0f, 1.0f);
    DrawTextEx(menuFont, modeName,
        { modeDisplay.x + modeDisplay.width * 0.5f - nameSz.x * 0.5f,
          modeDisplay.y + modeDisplay.height * 0.5f - nameSz.y * 0.5f },
        16.0f, 1.0f, UiTheme::AccentGold());

    // Right arrow button [ > ]
    Color nextFill = Fade(UiTheme::ButtonIdle(), 1.0f);
    if (hoverNextMode > 0.01f) nextFill = Fade(UiTheme::ButtonHover(), hoverNextMode);
    DrawRectangleRec(nextModeBtn, nextFill);
    DrawRectangleLinesEx(nextModeBtn, 1.5f, Fade(UiTheme::PanelBorder(), 0.6f + hoverNextMode * 0.4f));
        Color nextArrowColor = isHost ? UiTheme::TextPrimary() : UiTheme::TextMuted();
        float nextArrowX = nextModeBtn.x + nextModeBtn.width * 0.5f;
        float nextArrowY = nextModeBtn.y + nextModeBtn.height * 0.5f;
        DrawLineEx({ nextArrowX - 4.0f, nextArrowY - 6.0f }, { nextArrowX + 3.0f, nextArrowY }, 2.0f, nextArrowColor);
        DrawLineEx({ nextArrowX + 3.0f, nextArrowY }, { nextArrowX - 4.0f, nextArrowY + 6.0f }, 2.0f, nextArrowColor);
}

void LobbyScreen::DrawChipRows() {
    if (!isHost) return; // clients see values but cannot click
    OnlineGameMode mode = NetworkManager::GetInstance().currentGameMode;
    if (mode == OnlineGameMode::ELIMINATION) {
        Ui::DrawSectionLabel(menuFont, "WIN LIMIT (ROUNDS)",
                             { 758.0f, chipWin[0].y - 20.0f }, 13.0f);
        const char* labels[3] = { "6", "8", "14" };
        int active = (NetworkManager::GetInstance().roundLimit == 8) ? 1
                   : (NetworkManager::GetInstance().roundLimit == 14) ? 2 : 0;
        for (int i = 0; i < 3; ++i) {
            Rectangle b = chipWin[i];
            bool selected = (i == active);
            Color fill = selected
                ? Fade(UiTheme::AccentGold(), 0.30f + hoverChipWin[i] * 0.20f)
                : Color{
                    (unsigned char)(UiTheme::ButtonIdle().r + (UiTheme::ButtonHover().r - UiTheme::ButtonIdle().r) * hoverChipWin[i]),
                    (unsigned char)(UiTheme::ButtonIdle().g + (UiTheme::ButtonHover().g - UiTheme::ButtonIdle().g) * hoverChipWin[i]),
                    (unsigned char)(UiTheme::ButtonIdle().b + (UiTheme::ButtonHover().b - UiTheme::ButtonIdle().b) * hoverChipWin[i]),
                    (unsigned char)(UiTheme::ButtonIdle().a + (UiTheme::ButtonHover().a - UiTheme::ButtonIdle().a) * hoverChipWin[i])
                  };
            DrawRectangleRec(b, fill);
            Color border = selected ? UiTheme::AccentGold() : UiTheme::PanelBorder();
            float thick = selected ? 2.0f : 1.0f;
            DrawRectangleLinesEx(b, thick, Fade(border, 0.4f + hoverChipWin[i] * 0.6f));
            Vector2 sz = MeasureTextEx(menuFont, labels[i], 16.0f, 1.0f);
            DrawTextEx(menuFont, labels[i],
                       { b.x + b.width * 0.5f - sz.x * 0.5f,
                         b.y + b.height * 0.5f - sz.y * 0.5f },
                       16.0f, 1.0f, selected ? UiTheme::AccentGold() : UiTheme::TextPrimary());
        }
    } else {
        // FFA / TDM
        bool isTimeGoal = (NetworkManager::GetInstance().timeLimit > 0);

        Ui::DrawSectionLabel(menuFont, "GOAL TYPE",
                             { 758.0f, chipGoal[0].y - 20.0f }, 13.0f);
        const char* goalLabels[2] = { "KILLS", "TIME" };
        int activeGoal = isTimeGoal ? 1 : 0;
        for (int i = 0; i < 2; ++i) {
            Rectangle b = chipGoal[i];
            bool selected = (i == activeGoal);
            Color fill = selected
                ? Fade(UiTheme::AccentGold(), 0.30f + hoverChipGoal[i] * 0.20f)
                : Color{
                    (unsigned char)(UiTheme::ButtonIdle().r + (UiTheme::ButtonHover().r - UiTheme::ButtonIdle().r) * hoverChipGoal[i]),
                    (unsigned char)(UiTheme::ButtonIdle().g + (UiTheme::ButtonHover().g - UiTheme::ButtonIdle().g) * hoverChipGoal[i]),
                    (unsigned char)(UiTheme::ButtonIdle().b + (UiTheme::ButtonHover().b - UiTheme::ButtonIdle().b) * hoverChipGoal[i]),
                    (unsigned char)(UiTheme::ButtonIdle().a + (UiTheme::ButtonHover().a - UiTheme::ButtonIdle().a) * hoverChipGoal[i])
                  };
            DrawRectangleRec(b, fill);
            Color border = selected ? UiTheme::AccentGold() : UiTheme::PanelBorder();
            float thick = selected ? 2.0f : 1.0f;
            DrawRectangleLinesEx(b, thick, Fade(border, 0.4f + hoverChipGoal[i] * 0.6f));
            Vector2 sz = MeasureTextEx(menuFont, goalLabels[i], 16.0f, 1.0f);
            DrawTextEx(menuFont, goalLabels[i],
                       { b.x + b.width * 0.5f - sz.x * 0.5f,
                         b.y + b.height * 0.5f - sz.y * 0.5f },
                       16.0f, 1.0f, selected ? UiTheme::AccentGold() : UiTheme::TextPrimary());
        }

        if (isTimeGoal) {
            Ui::DrawSectionLabel(menuFont, "TIME LIMIT",
                                 { 758.0f, chipLimit[0].y - 20.0f }, 13.0f);
            const char* labels[3] = { "5m", "10m", "15m" };
            int cur = NetworkManager::GetInstance().timeLimit;
            int active = (cur == 300) ? 0 : (cur == 600) ? 1 : 2;
            for (int i = 0; i < 3; ++i) {
                Rectangle b = chipLimit[i];
                bool selected = (i == active);
                Color fill = selected
                    ? Fade(UiTheme::AccentGold(), 0.30f + hoverChipLimit[i] * 0.20f)
                    : Color{
                        (unsigned char)(UiTheme::ButtonIdle().r + (UiTheme::ButtonHover().r - UiTheme::ButtonIdle().r) * hoverChipLimit[i]),
                        (unsigned char)(UiTheme::ButtonIdle().g + (UiTheme::ButtonHover().g - UiTheme::ButtonIdle().g) * hoverChipLimit[i]),
                        (unsigned char)(UiTheme::ButtonIdle().b + (UiTheme::ButtonHover().b - UiTheme::ButtonIdle().b) * hoverChipLimit[i]),
                        (unsigned char)(UiTheme::ButtonIdle().a + (UiTheme::ButtonHover().a - UiTheme::ButtonIdle().a) * hoverChipLimit[i])
                      };
                DrawRectangleRec(b, fill);
                Color border = selected ? UiTheme::AccentGold() : UiTheme::PanelBorder();
                float thick = selected ? 2.0f : 1.0f;
                DrawRectangleLinesEx(b, thick, Fade(border, 0.4f + hoverChipLimit[i] * 0.6f));
                Vector2 sz = MeasureTextEx(menuFont, labels[i], 16.0f, 1.0f);
                DrawTextEx(menuFont, labels[i],
                           { b.x + b.width * 0.5f - sz.x * 0.5f,
                             b.y + b.height * 0.5f - sz.y * 0.5f },
                           16.0f, 1.0f, selected ? UiTheme::AccentGold() : UiTheme::TextPrimary());
            }
        } else {
            Ui::DrawSectionLabel(menuFont, "KILL LIMIT",
                                 { 758.0f, chipLimit[0].y - 20.0f }, 13.0f);
            const char* klabels[3] = { "40", "60", "100" };
            int curK = NetworkManager::GetInstance().killLimit;
            int activeK = (curK == 40) ? 0 : (curK == 60) ? 1 : 2;
            for (int i = 0; i < 3; ++i) {
                Rectangle b = chipLimit[i];
                bool selected = (i == activeK);
                Color fill = selected
                    ? Fade(UiTheme::AccentGold(), 0.30f + hoverChipLimit[i] * 0.20f)
                    : Color{
                        (unsigned char)(UiTheme::ButtonIdle().r + (UiTheme::ButtonHover().r - UiTheme::ButtonIdle().r) * hoverChipLimit[i]),
                        (unsigned char)(UiTheme::ButtonIdle().g + (UiTheme::ButtonHover().g - UiTheme::ButtonIdle().g) * hoverChipLimit[i]),
                        (unsigned char)(UiTheme::ButtonIdle().b + (UiTheme::ButtonHover().b - UiTheme::ButtonIdle().b) * hoverChipLimit[i]),
                        (unsigned char)(UiTheme::ButtonIdle().a + (UiTheme::ButtonHover().a - UiTheme::ButtonIdle().a) * hoverChipLimit[i])
                      };
                DrawRectangleRec(b, fill);
                Color border = selected ? UiTheme::AccentGold() : UiTheme::PanelBorder();
                float thick = selected ? 2.0f : 1.0f;
                DrawRectangleLinesEx(b, thick, Fade(border, 0.4f + hoverChipLimit[i] * 0.6f));
                Vector2 sz = MeasureTextEx(menuFont, klabels[i], 16.0f, 1.0f);
                DrawTextEx(menuFont, klabels[i],
                           { b.x + b.width * 0.5f - sz.x * 0.5f,
                             b.y + b.height * 0.5f - sz.y * 0.5f },
                           16.0f, 1.0f, selected ? UiTheme::AccentGold() : UiTheme::TextPrimary());
            }
        }
    }
}

void LobbyScreen::DrawMapCards() { /* map drawing moved inline in Draw() */ }

void LobbyScreen::Draw(RenderTexture2D target) {
    std::string currentMap = NetworkManager::GetInstance().selectedMapName;
    MapData* previewMd = MapRegistry::GetInstance().GetMap(currentMap);
    if (previewMd && previewMd->tmxMap) {
        // Update cache if map changed
        if (cachedPreviewName != currentMap) {
            BeginTextureMode(cachedPreview);
            ClearBackground(Color{ 18, 20, 28, 255 });

            float scaleX = mapPreview.width  / (float)previewMd->pixelWidth;
            float scaleY = mapPreview.height / (float)previewMd->pixelHeight;
            // Fit the map within the preview area without cropping.
            float scale  = scaleX < scaleY ? scaleX : scaleY;

            TraceLog(LOG_INFO, "LobbyScreen: Updating map preview for %s (Size: %dx%d, Scale: %f)",
                     currentMap.c_str(), previewMd->pixelWidth, previewMd->pixelHeight, scale);

            Camera2D miniCam = {};
            miniCam.zoom     = scale;
            miniCam.target   = { previewMd->pixelWidth * 0.5f, previewMd->pixelHeight * 0.5f };
            miniCam.offset   = { mapPreview.width * 0.5f, mapPreview.height * 0.5f };
            miniCam.rotation = 0.0f;

            BeginMode2D(miniCam);
            // Draw the actual TMX map. Use explicit map bounds because
            // RayTMX otherwise derives culling from the window size,
            // which is unrelated to this small render texture.
            Rectangle mapViewport = {
                -10000.0f, -10000.0f,
                20000.0f, 20000.0f
            };
            DrawTMX(previewMd->tmxMap, &miniCam, &mapViewport, 0, 0, WHITE);
            EndMode2D();

            EndTextureMode();
            cachedPreviewName = currentMap;
        }
    }

    if (background) {
        background->Draw(target, fadeIn, blurStrength);
    } else {
        BeginTextureMode(target);
        ClearBackground(BLACK);
        EndTextureMode();
    }

    BeginTextureMode(target);
    Ui::DrawVignette(0.65f);
    Ui::DrawGlassPanel(shell, 0.95f);

    // Gold top accent bar on the shell
    DrawRectangle((int)shell.x, (int)shell.y, (int)shell.width, 3,
                  Fade(UiTheme::AccentGold(), 0.75f));

    // Give the room controls their own visual rail over the animated backdrop.
    Rectangle roomRail = { 736.0f, shell.y + 12.0f, 506.0f, shell.height - 24.0f };
    DrawRectangleRec(roomRail, Fade(UiTheme::PanelBg(), 0.48f));
    DrawRectangleLinesEx(roomRail, 1.0f, Fade(UiTheme::PanelBorder(), 0.50f));
    // Gold rule below ROOM header
    DrawLineEx({ 736.0f, shell.y + 76.0f },
               { 1242.0f, shell.y + 76.0f },
               1.5f, Fade(UiTheme::AccentGold(), 0.42f));

    // LOBBY title with gold underline
    Ui::DrawSectionLabel(menuFont, "LOBBY", { shell.x + 32, shell.y + 16 }, 30.0f);
    float lobbyTitleW = MeasureTextEx(menuFont, "LOBBY", 30.0f, 1.0f).x;
    DrawRectangle((int)(shell.x + 32), (int)(shell.y + 52), (int)lobbyTitleW, 3,
                  Fade(UiTheme::AccentGold(), 0.85f));
    const char* role = isHost ? "You are the Host" : "You are a Player";
    DrawTextEx(menuFont, role,
               { shell.x + 32, shell.y + 60 }, 13.0f, 1.0f,
               isHost ? UiTheme::AccentGold() : UiTheme::TextMuted());

    DrawRoster();

    // Right actions — ROOM header
    Ui::DrawSectionLabel(menuFont, "ROOM", { 758, shell.y + 16 }, 22.0f);

    int readyCount = NetworkManager::GetInstance().GetReadyPlayerCount();
    int totalCount = NetworkManager::GetInstance().GetTotalPlayerCount();
    bool allReady = NetworkManager::GetInstance().AllPlayersReady();
    std::string status = "Ready  " + std::to_string(readyCount) + " / " + std::to_string(totalCount);
    // Colored status dot
    DrawCircle((int)(758 + 5), (int)(shell.y + 58 + 7), 5.0f,
               allReady ? UiTheme::ReadyGreen() : Fade(UiTheme::TextMuted(), 0.6f));
    DrawTextEx(menuFont, status.c_str(), { 758 + 16, shell.y + 52 }, 14.0f, 1.0f,
               allReady ? UiTheme::ReadyGreen() : UiTheme::TextMuted());

    float viewportY = shell.y + 78.0f;
    Rectangle optionViewport = { 736.0f, viewportY, 506.0f, (shell.y + shell.height - 12.0f) - viewportY };
    BeginScissorMode((int)optionViewport.x, (int)optionViewport.y,
                     (int)optionViewport.width, (int)optionViewport.height);
    Camera2D optionCamera = {};
    optionCamera.target = { 0.0f, optionScroll };
    optionCamera.offset = { 0.0f, 0.0f };
    optionCamera.zoom = 1.0f;
    BeginMode2D(optionCamera);

    // Map selection drawing
    Ui::DrawSectionLabel(menuFont, "MAP", { 758, mapDisplay.y - 20.0f }, 13.0f);
    {
        auto mapNames = MapRegistry::GetInstance().GetMapNames();
        std::string currentMap = NetworkManager::GetInstance().selectedMapName;
        
        // Prev button
        Color prevFill = Fade(UiTheme::ButtonIdle(), 1.0f);
        if (hoverPrevMap > 0.01f) prevFill = Fade(UiTheme::ButtonHover(), hoverPrevMap);
        DrawRectangleRec(prevMapBtn, prevFill);
        DrawRectangleLinesEx(prevMapBtn, 1.5f, Fade(UiTheme::PanelBorder(), 0.6f + hoverPrevMap * 0.4f));
                Color prevArrowColor = isHost ? UiTheme::TextPrimary() : UiTheme::TextMuted();
                float prevArrowX = prevMapBtn.x + prevMapBtn.width * 0.5f;
                float prevArrowY = prevMapBtn.y + prevMapBtn.height * 0.5f;
                DrawLineEx({ prevArrowX + 4.0f, prevArrowY - 6.0f }, { prevArrowX - 3.0f, prevArrowY }, 2.0f, prevArrowColor);
                DrawLineEx({ prevArrowX - 3.0f, prevArrowY }, { prevArrowX + 4.0f, prevArrowY + 6.0f }, 2.0f, prevArrowColor);

        // Center display
        DrawRectangleRec(mapDisplay, Fade(UiTheme::ButtonIdle(), 0.15f));
        DrawRectangleLinesEx(mapDisplay, 2.0f, Fade(UiTheme::PanelBorder(), 0.8f));
        Vector2 nameSz = MeasureTextEx(menuFont, currentMap.c_str(), 14.0f, 1.0f);
        DrawTextEx(menuFont, currentMap.c_str(),
            { mapDisplay.x + mapDisplay.width * 0.5f - nameSz.x * 0.5f,
              mapDisplay.y + mapDisplay.height * 0.5f - nameSz.y * 0.5f },
            14.0f, 1.0f, UiTheme::TextPrimary());

        // Next button
        Color nextFill = Fade(UiTheme::ButtonIdle(), 1.0f);
        if (hoverNextMap > 0.01f) nextFill = Fade(UiTheme::ButtonHover(), hoverNextMap);
        DrawRectangleRec(nextMapBtn, nextFill);
        DrawRectangleLinesEx(nextMapBtn, 1.5f, Fade(UiTheme::PanelBorder(), 0.6f + hoverNextMap * 0.4f));
                Color nextArrowColor = isHost ? UiTheme::TextPrimary() : UiTheme::TextMuted();
                float nextArrowX = nextMapBtn.x + nextMapBtn.width * 0.5f;
                float nextArrowY = nextMapBtn.y + nextMapBtn.height * 0.5f;
                DrawLineEx({ nextArrowX - 4.0f, nextArrowY - 6.0f }, { nextArrowX + 3.0f, nextArrowY }, 2.0f, nextArrowColor);
                DrawLineEx({ nextArrowX + 3.0f, nextArrowY }, { nextArrowX - 4.0f, nextArrowY + 6.0f }, 2.0f, nextArrowColor);

        // Big Map Preview
        DrawRectangleRec(mapPreview, Color{ 10, 14, 20, 235 });
        
        MapData* md = MapRegistry::GetInstance().GetMap(currentMap);
        if (md && md->tmxMap) {
            // Draw the cached texture (flip Y because OpenGL render textures are inverted)
            Rectangle sourceRec = { 0.0f, 0.0f, (float)cachedPreview.texture.width, -(float)cachedPreview.texture.height };
            DrawTextureRec(cachedPreview.texture, sourceRec, { mapPreview.x, mapPreview.y }, WHITE);
        } else {
            DrawTextEx(menuFont, "No preview",
                       { mapPreview.x + 140, mapPreview.y + 40 }, 12.0f, 1.0f, UiTheme::TextMuted());
        }
        
        DrawRectangleLinesEx(mapPreview, 1.5f, Fade(UiTheme::SkyAccent(), 0.75f));
    }
    // Dynamic Separator between map preview bottom and mode section
    DrawLineEx({ 752.0f, mapPreview.y + mapPreview.height + 8.0f }, { 1226.0f, mapPreview.y + mapPreview.height + 8.0f },
               1.0f, Fade(UiTheme::PanelBorder(), 0.40f));

    DrawModeSelector();
    DrawChipRows();

    // Dynamic Separator between chip rows and action buttons
    DrawLineEx({ 752.0f, chipLimit[0].y + chipLimit[0].height + 10.0f }, { 1226.0f, chipLimit[0].y + chipLimit[0].height + 10.0f },
               1.0f, Fade(UiTheme::PanelBorder(), 0.40f));

    bool localReady = NetworkManager::GetInstance().IsLocalPlayerReady();
    bool canStart = isHost && NetworkManager::GetInstance().AllPlayersReady();

    Ui::DrawMenuButton(readyBtn, menuFont,
                       localReady ? "UNREADY  (R)" : "READY  (R)",
                       hoverReady, !localReady, true);

    if (isHost) {
        Ui::DrawMenuButton(startBtn, menuFont, "START MATCH  (SPACE)",
                           hoverStart, true, canStart);

        DrawTextEx(menuFont, "SHARE", { 758, copyBtn.y - 18 }, 12.0f, 1.0f, UiTheme::TextMuted());
        if (!lanAddress.empty()) {
            Vector2 addressSize = MeasureTextEx(menuFont, lanAddress.c_str(), 11.0f, 1.0f);
            DrawTextEx(menuFont, lanAddress.c_str(),
                       { copyBtn.x + copyBtn.width - addressSize.x, copyBtn.y - 18 },
                       11.0f, 1.0f, UiTheme::SkyAccent());
        }
        Ui::DrawMenuButton(copyBtn, menuFont, "COPY ADDRESS  (C)",
                           hoverCopy, false, !lanAddress.empty());
    } else {
        DrawTextEx(menuFont, localReady ? "Waiting for host to start..." : "Press Ready when you're set",
                   { 758, readyBtn.y + readyBtn.height + 12 }, 14.0f, 1.0f, UiTheme::TextMuted());
    }

    EndMode2D();
    EndScissorMode();

    if (optionScrollMax > 0.0f) {
        float trackX = optionViewport.x + optionViewport.width - 7.0f;
        float trackY = optionViewport.y + 8.0f;
        float trackH = optionViewport.height - 16.0f;
        float thumbH = trackH * optionViewport.height /
                       (optionViewport.height + optionScrollMax);
        float thumbY = trackY + (trackH - thumbH) * optionScroll / optionScrollMax;
        DrawRectangle((int)trackX, (int)trackY, 3, (int)trackH,
                      Fade(UiTheme::PanelBorder(), 0.35f));
        DrawRectangle((int)trackX, (int)thumbY, 3, (int)thumbH,
                      Fade(UiTheme::SkyAccent(), 0.75f));
    }

    // Keep the back button in the top margin, outside the lobby shell.
    Rectangle globalBackBtn = { 28.0f, 4.0f, 168.0f, 32.0f };
    Ui::DrawMenuButton(globalBackBtn, menuFont, "BACK", hoverGlobalBack, false, true);

    EndTextureMode();
}

