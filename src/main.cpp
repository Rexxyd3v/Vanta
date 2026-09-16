#define RAYGUI_IMPLEMENTATION
#include <raylib.h>
#include "constants.h"
#include "utils/render_utils.h"
#include "screens/iscreen.h"
#include "screens/intro_screen.h"
#include "screens/main_level.h"
#include "screens/main_menu.h"
#include "screens/online_menu.h"
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
    if (!mapNames.empty()) {
        NetworkManager::GetInstance().selectedMapName = mapNames.front();
    }

    NetworkManager::GetInstance().Initialize();

    RenderTexture2D target = LoadRenderTexture(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);

    IScreen* currentScreen = new FirstScreen();
    bool isFirstScreenActive = true;
    bool isMainMenuActive = false;
    bool isOnlineMenuActive = false;
    bool isLobbyActive = false;
    bool isGameplayActive = false;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F11)) {
            ToggleFullscreen();
        }

        // Handle push-to-talk with C key
        if (IsKeyDown(KEY_C)) {
            proximityVoiceChat.setTransmitEnabled(true);
        } else {
            proximityVoiceChat.setTransmitEnabled(false);
        }

        float deltaTime = GetFrameTime();

        // Update logic
        if (currentScreen) {
            if (!currentScreen->Update(deltaTime)) {
                if (isFirstScreenActive) {
                    delete currentScreen;
                    currentScreen = new MainMenuScreen();
                    isFirstScreenActive = false;
                    isMainMenuActive = true;
                } else if (isMainMenuActive) {
                    MainMenuScreen* menu = static_cast<MainMenuScreen*>(currentScreen);
                    GameMode mode = menu->GetSelectedMode();
                    delete currentScreen;
                    currentScreen = new OnlineMenuScreen();
                    isOnlineMenuActive = true;
                    isMainMenuActive = false;
                } else if (isOnlineMenuActive) {
                    OnlineMenuScreen* menu = static_cast<OnlineMenuScreen*>(currentScreen);
                    if (menu->ShouldStartGame()) {
                        delete currentScreen;
                        currentScreen = new GameplayScreen(GameMode::ONLINE);
                        isOnlineMenuActive = false;
                        isGameplayActive = true;
                    } else if (menu->ShouldGoToLobby()) {
                        // Go to lobby screen
                        delete currentScreen;
                        currentScreen = new LobbyScreen();
                        isOnlineMenuActive = false;
                        isLobbyActive = true;
                    } else {
                        // User backed out to main menu
                        delete currentScreen;
                        currentScreen = new MainMenuScreen();
                        isOnlineMenuActive = false;
                        isMainMenuActive = true;
                    }
                } else if (isLobbyActive) {
                    LobbyScreen* lobby = static_cast<LobbyScreen*>(currentScreen);
                    if (lobby->ShouldStartGame()) {
                        delete currentScreen;
                        currentScreen = new GameplayScreen(GameMode::ONLINE);
                        isLobbyActive = false;
                        isGameplayActive = true;
                    } else {
                        delete currentScreen;
                        currentScreen = new MainMenuScreen();
                        isLobbyActive = false;
                        isMainMenuActive = true;
                    }
                } else if (isGameplayActive) {
                    delete currentScreen;
                    currentScreen = new MainMenuScreen();
                    isGameplayActive = false;
                    isMainMenuActive = true;
                } else {
                    delete currentScreen;
                    currentScreen = nullptr;
                    break;
                }
            }
        }

        // Update voice chat system
        if (proximityVoiceInitialized && currentScreen) {
            // Try to get local player from current screen if it's a gameplay screen
            GameplayScreen* gameplayScreen = dynamic_cast<GameplayScreen*>(currentScreen);
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

        BeginDrawing();
        ClearBackground(BLACK);
        DrawScaledToScreen(target);
        EndDrawing();
    }

    if (currentScreen != nullptr) {
        delete currentScreen;
    }

    UnloadRenderTexture(target);
    // TextureManager cleanup
    TextureManager::UnloadAll();
    NetworkManager::GetInstance().Shutdown();

    CloseAudioDevice(); // Close audio device
    CloseWindow();
    return 0;
}
