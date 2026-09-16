#include "main_menu.h"
#include "../network/network_manager.h"
#include "../ui/ui_widgets.h"
#include "../ui/ui_theme.h"
#include <cmath>
#include <cstdlib>

// Simple seeded noise helpers — no external deps
static float RandFloat(int seed) {
    unsigned int n = (unsigned int)(seed * 1664525u + 1013904223u);
    n ^= n >> 16;
    return (float)(n & 0xFFFF) / 65535.0f;
}

MainMenuScreen::MainMenuScreen() {
    menuFont = LoadFontEx("assets/fonts/BruceForeverRegular-X3jd2.ttf", 80, nullptr, 0);
    if (menuFont.texture.id == 0) menuFont = GetFontDefault();

    selectedMode     = GameMode::NONE;
    hoverOnline      = 0.0f;
    uiRise           = 0.0f;
    keyboardNavIndex = -1;
    glitchTimer      = 0.0f;
    glitchIntensity  = 0.0f;
    glitchCooldown   = 1.5f; // first glitch after 1.5s

    float cardWidth  = 380.0f;
    float cardHeight = 52.0f;
    cardOnline = {
        ((float)VIRTUAL_WIDTH  - cardWidth)  * 0.5f,
        ((float)VIRTUAL_HEIGHT - cardHeight) * 0.5f + 80.0f,
        cardWidth, cardHeight
    };

    int skinIdx = NetworkManager::GetInstance().localSkinIndex;
    if (skinIdx < 1 || skinIdx > 4) skinIdx = 1;
    background = new MenuBackground(skinIdx, MenuBackground::BackdropStyle::CINEMATIC_BEACH);

    fadeInTimer  = -0.6f;
    blurStrength =  1.0f;
    uiRise       =  0.0f;

    mainMenuMusic = LoadMusicStream("assets/sounds/MAINSOUNDTRACK.wav");
    SetMusicVolume(mainMenuMusic, 0.0f);
    PlayMusicStream(mainMenuMusic);

    choiceSound = LoadSound("assets/sounds/choice.wav");
}

MainMenuScreen::~MainMenuScreen() {
    if (background) { delete background; background = nullptr; }
    if (menuFont.texture.id != GetFontDefault().texture.id) UnloadFont(menuFont);
    UnloadSound(choiceSound);
    UnloadMusicStream(mainMenuMusic);
}

bool MainMenuScreen::Update(float deltaTime) {
    UpdateMusicStream(mainMenuMusic);

    fadeInTimer += deltaTime / 1.6f;
    if (fadeInTimer > 1.0f) fadeInTimer = 1.0f;

    float visibleAlpha = (fadeInTimer < 0.0f) ? 0.0f : fadeInTimer;
    SetMusicVolume(mainMenuMusic, visibleAlpha * 0.85f);

    blurStrength = Ui::Approach(blurStrength, 0.38f, 0.30f, deltaTime);
    uiRise       = Ui::Approach(uiRise,       1.0f,  2.2f,  deltaTime);
    glitchTimer += deltaTime;

    // Glitch burst logic — fires occasionally, decays fast
    glitchCooldown -= deltaTime;
    if (glitchCooldown <= 0.0f) {
        glitchIntensity = 0.7f + RandFloat((int)(glitchTimer * 1000.0f)) * 0.3f;
        // next burst in 2–5 seconds
        glitchCooldown  = 2.0f + RandFloat((int)(glitchTimer * 997.0f)) * 3.0f;
    }
    // Decay intensity quickly
    glitchIntensity = Ui::Approach(glitchIntensity, 0.0f, 5.5f, deltaTime);

    if (background) background->Update(deltaTime, fadeInTimer);

    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) ||
        IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
        keyboardNavIndex = 1;

    Vector2 mouseDelta = GetMouseDelta();
    if (fabsf(mouseDelta.x) > 1.0f || fabsf(mouseDelta.y) > 1.0f)
        keyboardNavIndex = -1;

    Vector2 mouse = Ui::RemapMouseToVirtual();
    bool hOn = CheckCollisionPointRec(mouse, cardOnline) || (keyboardNavIndex == 1);
    hoverOnline = Ui::Approach(hoverOnline, hOn ? 1.0f : 0.0f, 10.0f, deltaTime);

    bool confirm = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) ||
                   (keyboardNavIndex >= 0 && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)));
    if (confirm && hOn) {
        selectedMode = GameMode::ONLINE;
        PlaySound(choiceSound);
        return false;
    }

    return true;
}

// Draw the title text with RGB-split glitch slices
static void DrawGlitchText(const Font& font, const char* text,
                            Vector2 pos, float fontSize, float spacing,
                            Color baseColor, float intensity, float seed) {
    if (intensity < 0.01f) {
        DrawTextEx(font, text, pos, fontSize, spacing, baseColor);
        return;
    }

    // Red channel offset
    float rx = (RandFloat((int)(seed * 3.1f)) - 0.5f) * 14.0f * intensity;
    float ry = (RandFloat((int)(seed * 7.3f)) - 0.5f) *  2.0f * intensity;
    DrawTextEx(font, text, { pos.x + rx, pos.y + ry },
               fontSize, spacing, Fade({ 255, 30, 30, 180 }, intensity * 0.8f));

    // Cyan/blue channel offset (opposite direction)
    float cx = (RandFloat((int)(seed * 5.7f)) - 0.5f) * 12.0f * intensity;
    float cy = (RandFloat((int)(seed * 2.9f)) - 0.5f) *  2.0f * intensity;
    DrawTextEx(font, text, { pos.x - cx, pos.y - cy },
               fontSize, spacing, Fade({ 30, 220, 255, 160 }, intensity * 0.7f));

    // Main text on top — slightly jittered
    float jx = (RandFloat((int)(seed * 11.0f)) - 0.5f) * 3.0f * intensity;
    DrawTextEx(font, text, { pos.x + jx, pos.y }, fontSize, spacing, baseColor);

    // Horizontal slice glitch — one or two random rows displaced
    if (intensity > 0.4f) {
        float sliceY  = pos.y + RandFloat((int)(seed * 4.1f)) * fontSize;
        float sliceH  = 8.0f + RandFloat((int)(seed * 9.2f)) * 12.0f;
        float sliceOX = (RandFloat((int)(seed * 6.6f)) - 0.5f) * 22.0f * intensity;

        BeginScissorMode((int)(pos.x + sliceOX - 4), (int)sliceY,
                         (int)(MeasureTextEx(font, text, fontSize, spacing).x + 8), (int)sliceH);
        DrawTextEx(font, text, { pos.x + sliceOX, pos.y },
                   fontSize, spacing, Fade(UiTheme::AccentGold(), intensity * 0.9f));
        EndScissorMode();
    }
}

void MainMenuScreen::Draw(RenderTexture2D target) {
    float visibleAlpha = (fadeInTimer < 0.0f) ? 0.0f : fadeInTimer;

    if (background) {
        background->Draw(target, visibleAlpha, blurStrength);
    } else {
        BeginTextureMode(target);
        ClearBackground(BLACK);
        EndTextureMode();
    }

    BeginTextureMode(target);

    // Fade from black
    DrawRectangle(0, 0, VIRTUAL_WIDTH, VIRTUAL_HEIGHT,
                  Fade(BLACK, 1.0f - visibleAlpha));

    // Soft vignette — darkens edges, no hard line
    Ui::DrawVignette(0.70f);

    // Gentle bottom darkening — smooth, no seam
    for (int i = 0; i < 200; i++) {
        float t = (float)i / 200.0f;
        t = t * t; // ease-in so bottom is dark, top fades smoothly
        DrawRectangle(0, VIRTUAL_HEIGHT - i, VIRTUAL_WIDTH, 1,
                      Fade(BLACK, visibleAlpha * t * 0.80f));
    }

    float risePx     = (1.0f - uiRise) * 30.0f;
    float brandAlpha = visibleAlpha * uiRise;

    // ── TITLE ────────────────────────────────────────────────────────────
    const char* titleText = "VANTA";
    float titleFontSize   = 128.0f;
    float titleSpacing    = 6.0f;
    Vector2 titleSize = MeasureTextEx(menuFont, titleText, titleFontSize, titleSpacing);
    float titleX = ((float)VIRTUAL_WIDTH  - titleSize.x) * 0.5f;
    float titleY = 100.0f + risePx;

    // Hard retro drop shadow
    DrawTextEx(menuFont, titleText,
               { titleX + 6.0f, titleY + 6.0f },
               titleFontSize, titleSpacing, Fade(BLACK, brandAlpha * 0.90f));

    // Glitch effect on title
    DrawGlitchText(menuFont, titleText, { titleX, titleY },
                   titleFontSize, titleSpacing,
                   Fade(UiTheme::TextPrimary(), brandAlpha),
                   glitchIntensity * brandAlpha,
                   glitchTimer);

    // Thick gold underline bars
    float barY = titleY + titleSize.y + 8.0f;
    // Glitch: bar also shifts slightly
    float barJitter = glitchIntensity * brandAlpha * (RandFloat((int)(glitchTimer * 13.0f)) - 0.5f) * 10.0f;
    DrawRectangle((int)(titleX + barJitter), (int)barY, (int)titleSize.x, 6,
                  Fade(UiTheme::AccentGold(), brandAlpha));
    DrawRectangle((int)titleX, (int)(barY + 10.0f), (int)titleSize.x, 2,
                  Fade(UiTheme::AccentGold(), brandAlpha * 0.30f));

    // ── PLAY ONLINE BUTTON ───────────────────────────────────────────────
    Rectangle btn = cardOnline;
    btn.y += risePx;

    bool hovered = hoverOnline > 0.05f;

    // Dark box
    Color btnBg = hovered ? Color{ 232, 188, 96, 28 } : Color{ 6, 8, 14, 190 };
    DrawRectangleRec(btn, Fade(btnBg, brandAlpha));

    // Left accent stripe
    float railW = 4.0f + hoverOnline * 4.0f;
    DrawRectangle((int)btn.x, (int)btn.y, (int)railW, (int)btn.height,
                  Fade(UiTheme::AccentGold(), brandAlpha * (0.55f + hoverOnline * 0.45f)));

    // Border
    DrawRectangleLinesEx(btn, 2.0f,
                         Fade(UiTheme::AccentGold(), brandAlpha * (0.22f + hoverOnline * 0.68f)));

    // Chevron ">" cursor
    float chevX = btn.x + 22.0f;
    float chevY = btn.y + btn.height * 0.5f;
    float ca    = brandAlpha * (0.45f + hoverOnline * 0.55f);
    DrawLineEx({ chevX,        chevY - 9.0f }, { chevX + 9.0f, chevY        }, 2.5f, Fade(UiTheme::AccentGold(), ca));
    DrawLineEx({ chevX + 9.0f, chevY        }, { chevX,        chevY + 9.0f }, 2.5f, Fade(UiTheme::AccentGold(), ca));

    // Label
    const char* btnLabel = "PLAY ONLINE";
    float btnFontSize    = 26.0f;
    float btnSpacing     = 2.5f;
    Vector2 labelSz = MeasureTextEx(menuFont, btnLabel, btnFontSize, btnSpacing);
    float labelX = btn.x + (btn.width - labelSz.x) * 0.5f + 10.0f;
    float labelY = btn.y + (btn.height - labelSz.y) * 0.5f;

    DrawTextEx(menuFont, btnLabel,
               { labelX + 2.0f, labelY + 2.0f },
               btnFontSize, btnSpacing, Fade(BLACK, brandAlpha * 0.75f));
    Color labelCol = hovered ? UiTheme::AccentGold() : UiTheme::TextPrimary();
    DrawTextEx(menuFont, btnLabel,
               { labelX, labelY },
               btnFontSize, btnSpacing, Fade(labelCol, brandAlpha));

    // ── ESC hint — tiny, bottom-left ─────────────────────────────────────
    DrawTextEx(menuFont, "ESC  EXIT",
               { 36.0f, (float)VIRTUAL_HEIGHT - 32.0f },
               12.0f, 1.5f, Fade(UiTheme::TextMuted(), brandAlpha * 0.50f));

    EndTextureMode();
}
