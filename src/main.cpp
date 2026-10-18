#define RAYGUI_IMPLEMENTATION
#include <raylib.h>
#include "constants.h"
#include "ui/game_settings.h"
#include <memory>
#include <algorithm>
#include "utils/render_utils.h"
#include "screens/iscreen.h"
#include "screens/intro_screen.h"
#include "screens/main_level.h"
#include "screens/main_menu.h"
#include "screens/online_menu.h"
#include "screens/offline_menu.h"
#include "screens/lobby_screen.h"
#include "network/network_manager.h"
#include "map_loader/MapRegistry.h"
#include "utils/texture_manager.h"
#include "voice/proximity_voice_chat.h"

// Global voice chat instance (external linkage so network_manager.cpp can extern-link them)
ProximityVoiceChat proximityVoiceChat;
bool proximityVoiceInitialized = false;

// Callback used by network_manager.cpp to get the local player world position for proximity checks.
// Set to a valid function pointer while GameplayScreen is active, nullptr otherwise.
bool (*getLocalPlayerPosCallback)(Vector2& outPos) = nullptr;

// Pointer to active GameplayScreen used by getLocalPlayerPosCallback
static GameplayScreen* s_activeGameplay = nullptr;

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING); // Suppress INFO logs (texture loading spam = slow startup)
    InitWindow(VIRTUAL_WIDTH, VIRTUAL_HEIGHT, "VANTA");

    // Set the app/window icon (shown in title bar, taskbar, and alt-tab switcher).
    // Must be called after InitWindow() since it needs an active window/GL context.
    Image iconImage = LoadImage("assets/icon.png");
    if (iconImage.data != nullptr) {
        // Windows requires uncompressed 32-bit RGBA for window icons
        ImageFormat(&iconImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcon(iconImage);
        UnloadImage(iconImage); // SetWindowIcon copies the data internally, safe to unload
    } else {
        TraceLog(LOG_WARNING, "Failed to load window icon: assets/icon.png");
    }

    InitAudioDevice(); // Initialize audio device
    SetTargetFPS(60);
    SetExitKey(KEY_NULL); // Prevent Raylib from auto-closing the window on ESC

    // Initialize voice chat system
    proximityVoiceInitialized = proximityVoiceChat.initialize();
    if (!proximityVoiceInitialized) {
        TraceLog(LOG_WARNING, "Failed to initialize voice chat system");
    }

    MapRegistry::GetInstance().LoadAllMaps();
    auto mapNames = MapRegistry::GetInstance().GetMapNames();
    if (!MapRegistry::GetInstance().GetMap(NetworkManager::GetInstance().selectedMapName) && !mapNames.empty()) {
        NetworkManager::GetInstance().selectedMapName = mapNames.front();
    }

    NetworkManager::GetInstance().Initialize();

    RenderTexture2D target = LoadRenderTexture(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);

    Settings::Load();
    NetworkManager::GetInstance().localWeaponSkin=PackWeaponSkins(Settings::Get().weaponSkins);
    enum class Screen { Intro, Menu, Online, Offline, Lobby, Gameplay };
    Screen screen = Screen::Intro;
    std::unique_ptr<IScreen> currentScreen = std::make_unique<FirstScreen>();
    bool exitRequested = false;
    auto changeScreen = [&](Screen next, GameMode mode = GameMode::ONLINE) {
        s_activeGameplay = nullptr;
        getLocalPlayerPosCallback = nullptr;
        currentScreen.reset(); // Destroy menu background before enabling gameplay audio/AI.
        screen = next;
        switch (next) {
            case Screen::Intro: currentScreen = std::make_unique<FirstScreen>(); break;
            case Screen::Menu: currentScreen = std::make_unique<MainMenuScreen>(); break;
            case Screen::Offline: currentScreen = std::make_unique<OfflineMenuScreen>(); break;
            case Screen::Online: currentScreen = std::make_unique<OnlineMenuScreen>(); break;
            case Screen::Lobby: currentScreen = std::make_unique<LobbyScreen>(); break;
            case Screen::Gameplay: currentScreen = std::make_unique<GameplayScreen>(mode); break;
        }
    };
    while (!WindowShouldClose() && !exitRequested) {
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();
        bool overlayThisFrame = Settings::IsOpen();
        if (IsKeyPressed(KEY_F10) && !overlayThisFrame) Settings::Open();
        overlayThisFrame = overlayThisFrame || Settings::IsOpen();
        Settings::Update();
        SetMasterVolume(Settings::Get().master);
        proximityVoiceChat.setOutputVolume(Settings::Get().master * Settings::Get().voice);
        proximityVoiceChat.setTransmitEnabled(NetworkManager::GetInstance().IsConnected() &&
            !overlayThisFrame && Settings::DownKey(Settings::Talk) && IsWindowFocused());
        float deltaTime = std::min(GetFrameTime(), 0.1f);
        // Online screens keep polling while settings are open; their input is suppressed.
        if (currentScreen && (!overlayThisFrame || screen == Screen::Gameplay)) {
            if (!currentScreen->Update(deltaTime)) {
                switch (screen) {
                    case Screen::Intro: changeScreen(Screen::Menu); break;
                    case Screen::Menu: {
                        auto mode = static_cast<MainMenuScreen*>(currentScreen.get())->GetSelectedMode();
                        if (mode == GameMode::NONE) exitRequested = true;
                        else if (mode == GameMode::PRACTICE) changeScreen(Screen::Offline);
                        else changeScreen(Screen::Online);
                        break;
                    }
                    case Screen::Offline:
                        if (static_cast<OfflineMenuScreen*>(currentScreen.get())->ShouldStartGame()) changeScreen(Screen::Gameplay, GameMode::PRACTICE);
                        else changeScreen(Screen::Menu);
                        break;
                    case Screen::Online: {
                        auto* menu = static_cast<OnlineMenuScreen*>(currentScreen.get());
                        if (menu->ShouldStartGame()) changeScreen(Screen::Gameplay);
                        else if (menu->ShouldGoToLobby()) changeScreen(Screen::Lobby);
                        else changeScreen(Screen::Menu);
                        break;
                    }
                    case Screen::Lobby:
                        if (static_cast<LobbyScreen*>(currentScreen.get())->ShouldStartGame()) changeScreen(Screen::Gameplay);
                        else changeScreen(Screen::Menu);
                        break;
                    case Screen::Gameplay:
                        if (static_cast<GameplayScreen*>(currentScreen.get())->GetMode() == GameMode::PRACTICE) changeScreen(Screen::Offline);
                        else if (static_cast<GameplayScreen*>(currentScreen.get())->ShouldReturnToLobby() && NetworkManager::GetInstance().IsConnected()) {
                            NetworkManager::GetInstance().PrepareLobby();
                            changeScreen(Screen::Lobby);
                        } else changeScreen(Screen::Menu);
                        break;
                }
            }
        } else if (overlayThisFrame && NetworkManager::GetInstance().IsConnected()) {
            // Screen events must not be consumed by a second poll before it resumes.
            // Lobby/online screens are updated below with input disabled by Settings::IsOpen.
            if (screen == Screen::Lobby && !currentScreen->Update(deltaTime)) {
                if (static_cast<LobbyScreen*>(currentScreen.get())->ShouldStartGame()) changeScreen(Screen::Gameplay);
                else changeScreen(Screen::Menu);
            }
        }

        // Update voice chat system
        if (proximityVoiceInitialized && currentScreen) {
            // Try to get local player from current screen if it's a gameplay screen
            GameplayScreen* gameplayScreen = dynamic_cast<GameplayScreen*>(currentScreen.get());
            if (gameplayScreen) {
                // Track active gameplay screen so the callback can query it
                s_activeGameplay = gameplayScreen;
                getLocalPlayerPosCallback = [](Vector2& outPos) -> bool {
                    if (s_activeGameplay && s_activeGameplay->GetLocalPlayer()) {
                        outPos = s_activeGameplay->GetLocalPlayer()->GetPosition();
                        return true;
                    }
                    return false;
                };
                proximityVoiceChat.update(deltaTime, gameplayScreen->GetLocalPlayer());
            } else {
                s_activeGameplay = nullptr;
                getLocalPlayerPosCallback = nullptr; // Not in gameplay — disable proximity
                proximityVoiceChat.update(deltaTime, nullptr); // Update without proximity check
            }
        }

        // Draw logic
        if (currentScreen) {
            currentScreen->Draw(target);
        }

        BeginTextureMode(target);
        Settings::Draw();
        if (Settings::Get().showFps) DrawFPS(12, VIRTUAL_HEIGHT - 24);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        DrawScaledToScreen(target);
        EndDrawing();
    }

    s_activeGameplay = nullptr;
    getLocalPlayerPosCallback = nullptr;
    currentScreen.reset();
    Settings::Save();
    proximityVoiceChat.shutdown();
    UnloadRenderTexture(target);
    // TextureManager cleanup
    TextureManager::UnloadAll();
    NetworkManager::GetInstance().Shutdown();

    CloseAudioDevice(); // Close audio device
    CloseWindow();
    return 0;
}
