#include "online_menu.h"
#include "../network/network_manager.h"
#include "../constants.h"
#include "../ui/ui_widgets.h"
#include "../ui/ui_theme.h"
#include "../ui/weapon_skin_preview.h"
#include <cmath>
#include <string>

OnlineMenuScreen::OnlineMenuScreen() :
    startGame(false),
    goToLobby(false),
    activeInput(0),
    joinAddress(""),
    username("Player"),
    currentSkin(1),
    currentGunSkin(0),
    isConnecting(false),
    connectError(""),
    background(nullptr),
    preview(nullptr),
    skinPreview(nullptr),
    fadeIn(0.0f),
    blurStrength(0.75f),
    hoverHost(0.0f),
    hoverJoin(0.0f),
    hoverGlobalBack(0.0f),
    hoverTabFighter(1.0f),
    hoverTabLoadout(0.0f),
    caretBlink(0.0f),
    currentLeftTab(0) {

    menuFont = LoadFontEx("assets/fonts/BruceForeverRegular-X3jd2.ttf", 60, nullptr, 0);
    if (menuFont.texture.id == 0) menuFont = GetFontDefault();

    if (NetworkManager::GetInstance().localSkinIndex >= 1 &&
        NetworkManager::GetInstance().localSkinIndex <= 4) {
        currentSkin = NetworkManager::GetInstance().localSkinIndex;
    }
    if (!NetworkManager::GetInstance().localUsername.empty()) {
        username = NetworkManager::GetInstance().localUsername;
    }
    // Initialize gun skin from network manager
    currentGunSkin = NetworkManager::GetInstance().localWeaponSkin;

    background = new MenuBackground(currentSkin, MenuBackground::BackdropStyle::CINEMATIC_BEACH);
    preview = new CharacterPreview();
    skinPreview = new WeaponSkinPreview(currentGunSkin);

    // Layout: left side for character/gun tabs, right side for setup
    shell = { 70.0f, 70.0f, 1160.0f, 540.0f };
    
    // Left side:
    stageArea = { 110.0f, 195.0f, 520.0f, 255.0f }; // character stage
    selectorArea = { 110.0f, 465.0f, 520.0f, 100.0f }; // character selector
    // Offset gunPreviewArea vertically by 28.0f to match CharacterPreview's inner stageBox padding
    gunPreviewArea = { stageArea.x, stageArea.y + 28.0f, stageArea.width, stageArea.height - 28.0f };
    gunSelectorArea = selectorArea;

    // Right side:
    float rightX = 720.0f;
    float rightWidth = 420.0f;
    nameField = { rightX, 230.0f, rightWidth, 48.0f };
    hostBtn = { rightX, 350.0f, rightWidth, 64.0f };
    joinBtn = { rightX, 424.0f, rightWidth, 64.0f };
}

OnlineMenuScreen::~OnlineMenuScreen() {
    delete preview;
    delete background;
    delete skinPreview;
    if (menuFont.texture.id != GetFontDefault().texture.id) {
        UnloadFont(menuFont);
    }
}

bool OnlineMenuScreen::Update(float deltaTime) {
    fadeIn = Ui::Approach(fadeIn, 1.0f, 2.0f, deltaTime);
    blurStrength = Ui::Approach(blurStrength, 0.45f, 0.4f, deltaTime);
    caretBlink += deltaTime * 2.0f;
    if (caretBlink > 1.0f) caretBlink -= 1.0f;

    if (background) background->Update(deltaTime, fadeIn);
    if (preview) preview->Update(deltaTime);
    if (skinPreview) skinPreview->Update(deltaTime);

    if (isConnecting) {
        auto state = NetworkManager::GetInstance().PollJoinRoom();
        if (state == NetworkManager::JoinState::CONNECTED) {
            isConnecting = false;
            goToLobby = true;
            return false;
        } else if (state == NetworkManager::JoinState::FAILED) {
            isConnecting = false;
            connectError = "Connection failed. Check the address and try again.";
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            NetworkManager::GetInstance().Disconnect();
            isConnecting = false;
            connectError = "Connection cancelled.";
        }
        return true;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (activeInput != 0) {
            activeInput = 0;
        } else {
            return false;
        }
    }

    Vector2 mouse = Ui::RemapMouseToVirtual();

    // Tabs on left side (Sleek text-based tabs)
    float fontSize = 16.0f;
    Vector2 szFighter = MeasureTextEx(menuFont, "FIGHTER", fontSize, 1.0f);
    Vector2 szLoadout = MeasureTextEx(menuFont, "LOADOUT", fontSize, 1.0f);

    Rectangle tabFighter = { 110.0f, 145.0f, szFighter.x + 20.0f, 30.0f };
    Rectangle tabLoadout = { 110.0f + szFighter.x + 40.0f, 145.0f, szLoadout.x + 20.0f, 30.0f };
    
    bool hTabFighter = CheckCollisionPointRec(mouse, tabFighter);
    bool hTabLoadout = CheckCollisionPointRec(mouse, tabLoadout);
    
    hoverTabFighter = Ui::Approach(hoverTabFighter, (hTabFighter || currentLeftTab == 0) ? 1.0f : 0.0f, 8.0f, deltaTime);
    hoverTabLoadout = Ui::Approach(hoverTabLoadout, (hTabLoadout || currentLeftTab == 1) ? 1.0f : 0.0f, 8.0f, deltaTime);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (hTabFighter) {
            currentLeftTab = 0;
        }
        if (hTabLoadout) {
            currentLeftTab = 1;
        }
    }

    if (currentLeftTab == 0) {
        // Character select
        const float gap = 12.0f;
        float cardW = (selectorArea.width - gap * 3) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            Rectangle card = {
                selectorArea.x + i * (cardW + gap),
                selectorArea.y,
                cardW,
                selectorArea.height
            };
            if (CheckCollisionPointRec(mouse, card) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                currentSkin = i + 1;
                NetworkManager::GetInstance().localSkinIndex = currentSkin;
                connectError = "";
            }
        }
    } else {
        // Gun skin selection is handled in Draw method via WeaponSkinPreview::DrawSelector
    }

    if (activeInput != 0) {
        std::string* activeStr = nullptr;
        if (activeInput == 1) activeStr = &username;
        else if (activeInput == 2) activeStr = &joinAddress;

        if (activeStr) {
            int key = GetCharPressed();
            while (key > 0) {
                if ((key >= 32) && (key <= 125) && (activeStr->length() < 64)) {
                    *activeStr += (char)key;
                }
                key = GetCharPressed();
            }

            if (IsKeyPressed(KEY_BACKSPACE) && !activeStr->empty()) {
                activeStr->pop_back();
            }

            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
                const char* clipboardText = GetClipboardText();
                if (clipboardText) {
                    std::string clip(clipboardText);
                    for (char c : clip) {
                        if ((c >= 32) && (c <= 125) && (activeStr->length() < 64)) {
                            *activeStr += c;
                        }
                    }
                }
            }

            if (IsKeyPressed(KEY_ENTER)) {
                if (activeInput == 2) {
                    connectError = "";
                    NetworkManager::GetInstance().localUsername = username;
                    NetworkManager::GetInstance().localSkinIndex = currentSkin;
                    if (NetworkManager::GetInstance().BeginJoinRoom(joinAddress)) {
                        isConnecting = true;
                    } else {
                        connectError = "Invalid address.";
                    }
                    activeInput = 0;
                } else if (activeInput == 1) {
                    activeInput = 0;
                }
            }
        }

        // Still allow BACK while editing
        Rectangle globalBackBtn = { 28.0f, VIRTUAL_HEIGHT - 60.0f, 150.0f, 42.0f };
        bool hBack = CheckCollisionPointRec(mouse, globalBackBtn);
        hoverGlobalBack = Ui::Approach(hoverGlobalBack, hBack ? 1.0f : 0.0f, 8.0f, deltaTime);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hBack) {
            activeInput = 0;
        }
        return true;
    }

    bool hHost = CheckCollisionPointRec(mouse, hostBtn);
    bool hJoin = CheckCollisionPointRec(mouse, joinBtn);
    Rectangle globalBackBtn = { 28.0f, VIRTUAL_HEIGHT - 60.0f, 150.0f, 42.0f };
    bool hBack = CheckCollisionPointRec(mouse, globalBackBtn);

    hoverHost = Ui::Approach(hoverHost, hHost ? 1.0f : 0.0f, 8.0f, deltaTime);
    hoverJoin = Ui::Approach(hoverJoin, hJoin ? 1.0f : 0.0f, 8.0f, deltaTime);
    hoverGlobalBack = Ui::Approach(hoverGlobalBack, hBack ? 1.0f : 0.0f, 8.0f, deltaTime);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        connectError = "";
        if (hBack) {
            return false;
        } else if (CheckCollisionPointRec(mouse, nameField)) {
            activeInput = 1;
        } else if (hHost) {
            NetworkManager::GetInstance().localUsername = username;
            NetworkManager::GetInstance().localSkinIndex = currentSkin;
            if (NetworkManager::GetInstance().HostRoom(DEFAULT_GAME_PORT)) {
                goToLobby = true;
                return false;
            }
            connectError = "Failed to host room.";
        } else if (hJoin) {
            activeInput = 2;
        }
    }

    return true;
}

void OnlineMenuScreen::Draw(RenderTexture2D target) {
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

    Ui::DrawSectionLabel(menuFont, "ONLINE", { shell.x + 36, shell.y + 22 }, 28.0f);
    DrawTextEx(menuFont, "Choose your fighter, then host or join",
               { shell.x + 36, shell.y + 56 }, 14.0f, 1.0f, UiTheme::TextMuted());

    // Connecting overlay
    if (isConnecting) {
        DrawRectangleRec(shell, UiTheme::DimOverlay());
        Ui::DrawCenteredText(menuFont, "CONNECTING", VIRTUAL_HEIGHT * 0.42f, 36.0f, UiTheme::AccentGold());
        float spin = (float)GetTime() * 4.0f;
        Vector2 c = { VIRTUAL_WIDTH * 0.5f, VIRTUAL_HEIGHT * 0.55f };
        for (int i = 0; i < 8; ++i) {
            float a = spin + i * (PI * 0.25f);
            float r = 28.0f;
            DrawCircle(
                (int)(c.x + cosf(a) * r),
                (int)(c.y + sinf(a) * r),
                4.0f, Fade(UiTheme::AccentGold(), 0.3f + (i / 8.0f) * 0.7f));
        }
        Ui::DrawCenteredText(menuFont, "ESC to cancel", VIRTUAL_HEIGHT * 0.62f, 16.0f, UiTheme::TextMuted());
        EndTextureMode();
        return;
    }

    // Left Tabs Drawing (Sleek, tab-bar style with thin underline)
    float tabFontSize = 16.0f;
    Vector2 szFighter = MeasureTextEx(menuFont, "FIGHTER", tabFontSize, 1.0f);
    Vector2 szLoadout = MeasureTextEx(menuFont, "LOADOUT", tabFontSize, 1.0f);

    Vector2 posFighter = { 110.0f, 150.0f };
    Vector2 posLoadout = { 110.0f + szFighter.x + 40.0f, 150.0f };
    float lineY = 175.0f;

    // Draw bottom border line for the whole tab bar
    DrawLineEx({ 110.0f, lineY }, { 630.0f, lineY }, 1.0f, Fade(UiTheme::PanelBorder(), 0.35f));

    // Interpolate colors based on active and hover states
    Color colFighter = UiTheme::TextMuted();
    if (currentLeftTab == 0) {
        colFighter = UiTheme::AccentGold();
    } else {
        colFighter.r = (unsigned char)(UiTheme::TextMuted().r + (UiTheme::TextPrimary().r - UiTheme::TextMuted().r) * hoverTabFighter);
        colFighter.g = (unsigned char)(UiTheme::TextMuted().g + (UiTheme::TextPrimary().g - UiTheme::TextMuted().g) * hoverTabFighter);
        colFighter.b = (unsigned char)(UiTheme::TextMuted().b + (UiTheme::TextPrimary().b - UiTheme::TextMuted().b) * hoverTabFighter);
    }

    Color colLoadout = UiTheme::TextMuted();
    if (currentLeftTab == 1) {
        colLoadout = UiTheme::AccentGold();
    } else {
        colLoadout.r = (unsigned char)(UiTheme::TextMuted().r + (UiTheme::TextPrimary().r - UiTheme::TextMuted().r) * hoverTabLoadout);
        colLoadout.g = (unsigned char)(UiTheme::TextMuted().g + (UiTheme::TextPrimary().g - UiTheme::TextMuted().g) * hoverTabLoadout);
        colLoadout.b = (unsigned char)(UiTheme::TextMuted().b + (UiTheme::TextPrimary().b - UiTheme::TextMuted().b) * hoverTabLoadout);
    }

    // Draw Tab Labels
    DrawTextEx(menuFont, "FIGHTER", posFighter, tabFontSize, 1.0f, colFighter);
    DrawTextEx(menuFont, "LOADOUT", posLoadout, tabFontSize, 1.0f, colLoadout);

    // Draw Tab Underlines
    if (currentLeftTab == 0) {
        DrawRectangleRec({ posFighter.x - 2.0f, lineY - 2.0f, szFighter.x + 4.0f, 3.0f }, UiTheme::AccentGold());
    } else if (hoverTabFighter > 0.01f) {
        DrawRectangleRec({ posFighter.x - 2.0f, lineY - 1.0f, szFighter.x + 4.0f, 2.0f }, Fade(UiTheme::TextPrimary(), hoverTabFighter * 0.4f));
    }

    if (currentLeftTab == 1) {
        DrawRectangleRec({ posLoadout.x - 2.0f, lineY - 2.0f, szLoadout.x + 4.0f, 3.0f }, UiTheme::AccentGold());
    } else if (hoverTabLoadout > 0.01f) {
        DrawRectangleRec({ posLoadout.x - 2.0f, lineY - 1.0f, szLoadout.x + 4.0f, 2.0f }, Fade(UiTheme::TextPrimary(), hoverTabLoadout * 0.4f));
    }

    if (currentLeftTab == 0) {
        // Left: character stage + selector
        if (preview) {
            preview->DrawStage(stageArea, currentSkin);
            Vector2 mouse = Ui::RemapMouseToVirtual();
            preview->DrawSelector(selectorArea, menuFont, currentSkin, mouse);
        }

        std::string charLabel = "CHAR " + std::to_string(currentSkin);
        Vector2 charTextSize = MeasureTextEx(menuFont, charLabel.c_str(), 14.0f, 1.0f);
        DrawTextEx(menuFont, charLabel.c_str(),
                   { stageArea.x + stageArea.width - charTextSize.x, stageArea.y + 6.0f },
                   14.0f, 1.0f, UiTheme::AccentGold());
    } else {
        // Left: gun skin preview and selector
        if (skinPreview) {
            skinPreview->DrawPreview(gunPreviewArea, currentGunSkin, menuFont);
            Vector2 mouse = Ui::RemapMouseToVirtual();
            
            // Get active weapon slot and current skin for that slot
            int slot = skinPreview->GetSelectedWeaponSlot();
            int currentWeaponSkin = skinPreview->GetWeaponSkin(slot);

            int clicked = skinPreview->DrawSelector(gunSelectorArea, menuFont, currentWeaponSkin, mouse);
            if (clicked != -1) {
                // Save selected skin for this weapon slot to NetworkManager (for local player info sync)
                NetworkManager::GetInstance().localWeaponSkin = skinPreview->GetPackedSkins();
                connectError = "";
            }

            // Draw Weapon Skin Label showing active weapon + active skin
            std::string skinLabel=std::string(WeaponShortName(slot))+" / "+WeaponSkinLabel(slot,currentWeaponSkin);

            Vector2 skinTextSize = MeasureTextEx(menuFont, skinLabel.c_str(), 14.0f, 1.0f);
            DrawTextEx(menuFont, skinLabel.c_str(),
                       { stageArea.x + stageArea.width - skinTextSize.x, stageArea.y + 6.0f },
                       14.0f, 1.0f, UiTheme::AccentGold());
        }
    }

    // Right panel content
    if (activeInput == 2) {
        Ui::DrawSectionLabel(menuFont, "JOIN ROOM", { 720, 160 }, 22.0f);
        DrawTextEx(menuFont, "Server address (LAN or playit.gg)",
                   { 720, 195 }, 14.0f, 1.0f, UiTheme::TextMuted());

        Rectangle addrField = { 720, 230, 420, 48 };
        Ui::DrawTextField(addrField, menuFont, joinAddress.c_str(), true, caretBlink);

        DrawTextEx(menuFont, "Examples: 192.168.1.5   or   abc.gl.at.ply.gg:54321",
                   { 720, 295 }, 12.0f, 1.0f, UiTheme::TextMuted());
        DrawTextEx(menuFont, "ENTER connect   -   ESC cancel",
                   { 720, 330 }, 14.0f, 1.0f, UiTheme::TextMuted());
    } else {
        Ui::DrawSectionLabel(menuFont, "YOUR SETUP", { 720, 160 }, 22.0f);

        DrawTextEx(menuFont, "NAME", { 720, 195 }, 12.0f, 1.0f, UiTheme::TextMuted());
        Ui::DrawTextField(nameField, menuFont, username.c_str(), activeInput == 1, caretBlink);

        Ui::DrawMenuButton(hostBtn, menuFont, "CREATE ROOM", hoverHost, true, true);
        Ui::DrawMenuButton(joinBtn, menuFont, "JOIN ROOM", hoverJoin, false, true);

        DrawTextEx(menuFont, "Host: open UDP 7777 (playit.gg tunnel OK)",
                   { 720, 505 }, 12.0f, 1.0f, UiTheme::TextMuted());
        DrawTextEx(menuFont, "Join: paste host:port or LAN IP",
                   { 720, 525 }, 12.0f, 1.0f, UiTheme::TextMuted());
    }

    Rectangle globalBackBtn = { 28.0f, VIRTUAL_HEIGHT - 60.0f, 150.0f, 42.0f };
    Ui::DrawMenuButton(globalBackBtn, menuFont, "BACK", hoverGlobalBack, false, true);

    Ui::DrawStatusBanner(menuFont, connectError.c_str(), true);

    EndTextureMode();
}
