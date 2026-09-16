#include "ui_widgets.h"
#include "ui_theme.h"
#include "../constants.h"
#include <cmath>
#include <cstring>

namespace Ui {

void DrawVignette(float strength) {
    if (strength <= 0.0f) return;
    const int rings = 8;
    for (int i = 0; i < rings; ++i) {
        float t = (float)(i + 1) / (float)rings;
        unsigned char a = (unsigned char)(strength * 28.0f * t);
        int inset = i * 18;
        DrawRectangleLinesEx(
            { (float)inset, (float)inset,
              (float)(VIRTUAL_WIDTH - inset * 2),
              (float)(VIRTUAL_HEIGHT - inset * 2) },
            18.0f, { 0, 0, 0, a });
    }
    DrawRectangle(0, 0, VIRTUAL_WIDTH, 90, { 0, 0, 0, (unsigned char)(40 * strength) });
    DrawRectangle(0, VIRTUAL_HEIGHT - 80, VIRTUAL_WIDTH, 80, { 0, 0, 0, (unsigned char)(55 * strength) });
}

void DrawGlassPanel(Rectangle bounds, float alpha) {
    Color bg = UiTheme::PanelBg();
    bg.a = (unsigned char)(bg.a * alpha);
    Color border = UiTheme::PanelBorder();
    border.a = (unsigned char)(border.a * alpha);
    Color inner = UiTheme::PanelInner();
    inner.a = (unsigned char)(inner.a * alpha);

    DrawRectangleRec(bounds, bg);
    DrawRectangleLinesEx(bounds, 1.5f, border);
    DrawRectangle(
        (int)(bounds.x + 1), (int)(bounds.y + 1),
        (int)(bounds.width - 2), 2, inner);
}

bool DrawMenuButton(Rectangle bounds, const Font& font, const char* label,
                    float hoverT, bool primary, bool enabled) {
    Vector2 mouse = RemapMouseToVirtual();
    bool hovered = enabled && CheckCollisionPointRec(mouse, bounds);

    float scale = 1.0f + hoverT * 0.03f;
    float cx = bounds.x + bounds.width * 0.5f;
    float cy = bounds.y + bounds.height * 0.5f;
    Rectangle draw = {
        cx - bounds.width * scale * 0.5f,
        cy - bounds.height * scale * 0.5f,
        bounds.width * scale,
        bounds.height * scale
    };

    Color fill = enabled
        ? Color{
            (unsigned char)(UiTheme::ButtonIdle().r + (UiTheme::ButtonHover().r - UiTheme::ButtonIdle().r) * hoverT),
            (unsigned char)(UiTheme::ButtonIdle().g + (UiTheme::ButtonHover().g - UiTheme::ButtonIdle().g) * hoverT),
            (unsigned char)(UiTheme::ButtonIdle().b + (UiTheme::ButtonHover().b - UiTheme::ButtonIdle().b) * hoverT),
            (unsigned char)(UiTheme::ButtonIdle().a + (UiTheme::ButtonHover().a - UiTheme::ButtonIdle().a) * hoverT)
          }
        : Color{ 40, 40, 48, 140 };

    DrawRectangleRec(draw, fill);

    Color border = primary ? UiTheme::AccentGold() : UiTheme::PanelBorder();
    if (!enabled) border = Fade(border, 0.35f);
    else border = Fade(border, 0.45f + hoverT * 0.55f);
    DrawRectangleLinesEx(draw, 2.0f, border);

    if (enabled && (primary || hoverT > 0.01f)) {
        float railW = 4.0f + hoverT * 2.0f;
        DrawRectangle((int)draw.x, (int)draw.y, (int)railW, (int)draw.height,
                      Fade(UiTheme::AccentGold(), 0.35f + hoverT * 0.65f));
    }

    float fontSize = UiTheme::FontButton;
    Vector2 sz = MeasureTextEx(font, label, fontSize, 1.0f);
    
    // Scale down text if it's too wide for the button
    float maxTextWidth = draw.width - 32.0f; // 16px padding on each side
    if (sz.x > maxTextWidth) {
        fontSize *= maxTextWidth / sz.x;
        sz = MeasureTextEx(font, label, fontSize, 1.0f);
    }

    Color textCol = enabled ? UiTheme::TextPrimary() : Fade(UiTheme::TextMuted(), 0.5f);
    DrawTextEx(font, label,
               { draw.x + draw.width * 0.5f - sz.x * 0.5f,
                 draw.y + draw.height * 0.5f - sz.y * 0.5f },
               fontSize, 1.0f, textCol);

    return hovered;
}

void DrawTacticalFrame(float alpha, const Font* font) {
    if (alpha <= 0.0f) return;
    Color bracketCol = Fade(UiTheme::AccentGold(), alpha * 0.45f);
    Color lineCol    = Fade(UiTheme::PanelBorder(), alpha * 0.35f);
    Color textCol    = Fade(UiTheme::TextMuted(), alpha * 0.7f);

    float pad = 24.0f;
    float len = 32.0f;
    float thickness = 2.0f;

    // Top-Left corner bracket
    DrawLineEx({ pad, pad }, { pad + len, pad }, thickness, bracketCol);
    DrawLineEx({ pad, pad }, { pad, pad + len }, thickness, bracketCol);
    DrawLineEx({ pad - 4.0f, pad }, { pad + 4.0f, pad }, 1.0f, bracketCol);
    DrawLineEx({ pad, pad - 4.0f }, { pad, pad + 4.0f }, 1.0f, bracketCol);

    // Top-Right corner bracket
    DrawLineEx({ (float)VIRTUAL_WIDTH - pad - len, pad }, { (float)VIRTUAL_WIDTH - pad, pad }, thickness, bracketCol);
    DrawLineEx({ (float)VIRTUAL_WIDTH - pad, pad }, { (float)VIRTUAL_WIDTH - pad, pad + len }, thickness, bracketCol);

    // Bottom-Left corner bracket
    DrawLineEx({ pad, (float)VIRTUAL_HEIGHT - pad }, { pad + len, (float)VIRTUAL_HEIGHT - pad }, thickness, bracketCol);
    DrawLineEx({ pad, (float)VIRTUAL_HEIGHT - pad - len }, { pad, (float)VIRTUAL_HEIGHT - pad }, thickness, bracketCol);

    // Bottom-Right corner bracket
    DrawLineEx({ (float)VIRTUAL_WIDTH - pad - len, (float)VIRTUAL_HEIGHT - pad }, { (float)VIRTUAL_WIDTH - pad, (float)VIRTUAL_HEIGHT - pad }, thickness, bracketCol);
    DrawLineEx({ (float)VIRTUAL_WIDTH - pad, (float)VIRTUAL_HEIGHT - pad - len }, { (float)VIRTUAL_WIDTH - pad, (float)VIRTUAL_HEIGHT - pad }, thickness, bracketCol);

    // Top Header Status Line
    DrawLineEx({ 60.0f, 44.0f }, { (float)VIRTUAL_WIDTH - 60.0f, 44.0f }, 1.0f, lineCol);

    if (font && font->texture.id != 0) {
        // Status labels removed.
    }
}

void DrawBadgeTag(Vector2 pos, const char* text, Color bg, Color textCol, const Font& font, float fontSize) {
    if (!text || !text[0]) return;
    Vector2 sz = MeasureTextEx(font, text, fontSize, 1.0f);
    Rectangle bgRec = { pos.x, pos.y, sz.x + 14.0f, sz.y + 6.0f };
    DrawRectangleRec(bgRec, bg);
    DrawRectangleLinesEx(bgRec, 1.0f, Fade(textCol, 0.45f));
    DrawTextEx(font, text, { pos.x + 7.0f, pos.y + 3.0f }, fontSize, 1.0f, textCol);
}

bool DrawModeCard(Rectangle bounds, const Font& font,
                  const char* title, const char* subtitle, float hoverT,
                  const char* badgeTag, int iconType, Color accentColor) {
    Vector2 mouse = RemapMouseToVirtual();
    bool hovered = CheckCollisionPointRec(mouse, bounds);

    float lift = hoverT * 6.0f;
    Rectangle draw = { bounds.x, bounds.y - lift, bounds.width, bounds.height };

    // Outer glow halo when hovered
    if (hoverT > 0.01f) {
        Rectangle glowRec = { draw.x - 5.0f, draw.y - 5.0f, draw.width + 10.0f, draw.height + 10.0f };
        DrawRectangleRec(glowRec, Fade(accentColor, hoverT * 0.12f));
        DrawRectangleLinesEx(glowRec, 1.5f, Fade(accentColor, hoverT * 0.35f));
    }

    // Glass panel base
    DrawGlassPanel(draw, 0.92f + hoverT * 0.08f);

    // Left accent rail
    float railW = 6.0f + hoverT * 3.0f;
    Color railColor = Fade(accentColor, 0.55f + hoverT * 0.45f);
    DrawRectangle((int)draw.x, (int)draw.y, (int)railW, (int)draw.height, railColor);

    // Border
    Color borderColor = Fade(accentColor, 0.3f + hoverT * 0.7f);
    DrawRectangleLinesEx(draw, 2.0f, borderColor);

    // Top-right corner cut accent
    DrawLineEx({ draw.x + draw.width - 22.0f, draw.y },
               { draw.x + draw.width, draw.y + 22.0f },
               2.0f, borderColor);

    // Reserve a right lane for metadata and the icon.
    float contentLeft = draw.x + 30.0f;
    float contentRight = draw.x + draw.width - 150.0f;
    float laneX = draw.x + draw.width - 116.0f;
    DrawLineEx({ laneX, draw.y + 22.0f }, { laneX, draw.y + draw.height - 22.0f },
               1.0f, Fade(accentColor, 0.2f + hoverT * 0.25f));

    // Tactical Badge Tag on top right
    if (badgeTag && badgeTag[0]) {
        Vector2 bsz = MeasureTextEx(font, badgeTag, 11.0f, 1.0f);
        float badgeX = draw.x + draw.width - bsz.x - 24.0f;
        float badgeY = draw.y + 13.0f;
        Color badgeBg = Fade(accentColor, 0.18f + hoverT * 0.18f);
        DrawBadgeTag({ badgeX, badgeY }, badgeTag, badgeBg, accentColor, font, 11.0f);
    }

        // Small index marker gives the two choices a visual rhythm without competing
        // with the title and keeps the content column clearly separated from the icon.
        const char* indexText = iconType == 1 ? "01" : "02";
        DrawTextEx(font, indexText, { contentLeft, draw.y + draw.height - 28.0f },
                   11.0f, 1.0f, Fade(accentColor, 0.72f + hoverT * 0.28f));

    // Procedural Icon on the right side of the card
    if (iconType > 0) {
        float iconCx = draw.x + draw.width - 58.0f;
        float iconCy = draw.y + draw.height * 0.5f + 4.0f;
        Color iconCol = Fade(accentColor, 0.5f + hoverT * 0.5f);

        if (iconType == 1) { // Crosshair icon for Offline Arena
            float r1 = 18.0f;
            DrawCircleLines((int)iconCx, (int)iconCy, r1, iconCol);
            DrawCircleLines((int)iconCx, (int)iconCy, r1 * 0.45f, Fade(iconCol, 0.75f));
            DrawCircle((int)iconCx, (int)iconCy, 3.0f, iconCol);
            // Crosshair tick marks
            DrawLineEx({ iconCx - r1 - 6.0f, iconCy }, { iconCx - r1 + 3.0f, iconCy }, 2.0f, iconCol);
            DrawLineEx({ iconCx + r1 - 3.0f, iconCy }, { iconCx + r1 + 6.0f, iconCy }, 2.0f, iconCol);
            DrawLineEx({ iconCx, iconCy - r1 - 6.0f }, { iconCx, iconCy - r1 + 3.0f }, 2.0f, iconCol);
            DrawLineEx({ iconCx, iconCy + r1 - 3.0f }, { iconCx, iconCy + r1 + 6.0f }, 2.0f, iconCol);
        } else if (iconType == 2) { // Network / Signal icon for Online Lobby
            float r1 = 16.0f;
            DrawCircle((int)iconCx, (int)iconCy, 4.0f, iconCol);
            DrawCircleLines((int)iconCx, (int)iconCy, 10.0f, Fade(iconCol, 0.85f));
            DrawCircleLines((int)iconCx, (int)iconCy, 18.0f, Fade(iconCol, 0.55f));
            // Network node arms
            DrawLineEx({ iconCx - 14.0f, iconCy - 10.0f }, { iconCx, iconCy }, 1.5f, Fade(iconCol, 0.65f));
            DrawLineEx({ iconCx + 14.0f, iconCy - 10.0f }, { iconCx, iconCy }, 1.5f, Fade(iconCol, 0.65f));
            DrawLineEx({ iconCx, iconCy + 16.0f }, { iconCx, iconCy }, 1.5f, Fade(iconCol, 0.65f));
            DrawCircle((int)(iconCx - 14.0f), (int)(iconCy - 10.0f), 2.5f, iconCol);
            DrawCircle((int)(iconCx + 14.0f), (int)(iconCy - 10.0f), 2.5f, iconCol);
            DrawCircle((int)iconCx, (int)(iconCy + 16.0f), 2.5f, iconCol);
        }
    }

    // Title
    float titleSize = 30.0f;
    const float contentWidth = contentRight - contentLeft;
    Vector2 tsz = MeasureTextEx(font, title, titleSize, 1.0f);
    if (tsz.x > contentWidth) {
        titleSize *= contentWidth / tsz.x;
        tsz = MeasureTextEx(font, title, titleSize, 1.0f);
    }
    // Subtle drop shadow for title
    DrawTextEx(font, title,
               { contentLeft + 1.0f, draw.y + 23.0f },
               titleSize, 1.0f, Fade(BLACK, 0.65f));
    DrawTextEx(font, title,
               { contentLeft, draw.y + 22.0f },
               titleSize, 1.0f, UiTheme::TextPrimary());

    // Subtitle
    float subSize = 15.0f;
    Vector2 subtitleSize = MeasureTextEx(font, subtitle, subSize, 1.0f);
    if (subtitleSize.x > contentWidth) {
        subSize *= contentWidth / subtitleSize.x;
    }
    DrawTextEx(font, subtitle,
               { contentLeft, draw.y + 22.0f + tsz.y + 8.0f },
               subSize, 1.0f, UiTheme::TextMuted());

    // Bottom accent edge glow on hover
    if (hoverT > 0.01f) {
        DrawRectangle((int)draw.x, (int)(draw.y + draw.height - 2.0f),
                      (int)draw.width, 2, Fade(accentColor, hoverT * 0.85f));
    }

    return hovered;
}

void DrawTextField(Rectangle bounds, const Font& font, const char* text,
                   bool focused, float caretBlink) {
    Color fill = focused ? Color{ 245, 240, 230, 245 } : Color{ 220, 215, 205, 220 };
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, focused ? 2.5f : 1.5f,
                         focused ? UiTheme::AccentGold() : UiTheme::PanelBorder());

    const float pad = 12.0f;
    float fontSize = 20.0f;
    DrawTextEx(font, text,
               { bounds.x + pad, bounds.y + bounds.height * 0.5f - fontSize * 0.45f },
               fontSize, 1.0f, UiTheme::TextDark());

    if (focused && caretBlink > 0.5f) {
        Vector2 sz = MeasureTextEx(font, text, fontSize, 1.0f);
        float cx = bounds.x + pad + sz.x + 2.0f;
        DrawRectangle((int)cx, (int)(bounds.y + 8), 2, (int)(bounds.height - 16), UiTheme::TextDark());
    }
}

void DrawSectionLabel(const Font& font, const char* text, Vector2 pos, float size) {
    DrawTextEx(font, text, pos, size, 1.0f, UiTheme::AccentGold());
}

void DrawStatusBanner(const Font& font, const char* text, bool isError) {
    if (!text || !text[0]) return;
    Rectangle bar = { 80, (float)VIRTUAL_HEIGHT - 70, (float)VIRTUAL_WIDTH - 160, 48 };
    Color bg = isError ? Fade(UiTheme::Danger(), 0.88f) : Fade(UiTheme::AccentWarm(), 0.88f);
    DrawRectangleRec(bar, bg);
    DrawRectangleLinesEx(bar, 1.5f, Fade(WHITE, 0.35f));
    Vector2 sz = MeasureTextEx(font, text, 18.0f, 1.0f);
    DrawTextEx(font, text,
               { bar.x + bar.width * 0.5f - sz.x * 0.5f,
                 bar.y + bar.height * 0.5f - sz.y * 0.5f },
               18.0f, 1.0f, WHITE);
}

void DrawBackHint(const Font& font, const char* text) {
    DrawTextEx(font, text, { 28, (float)VIRTUAL_HEIGHT - 36 }, 14.0f, 1.0f, UiTheme::TextMuted());
}

void DrawCenteredText(const Font& font, const char* text, float y, float size, Color color) {
    Vector2 sz = MeasureTextEx(font, text, size, 1.0f);
    DrawTextEx(font, text, { VIRTUAL_WIDTH * 0.5f - sz.x * 0.5f, y }, size, 1.0f, color);
}

float Approach(float current, float target, float speed, float dt) {
    float diff = target - current;
    float step = speed * dt;
    if (diff > step) return current + step;
    if (diff < -step) return current - step;
    return target;
}

Vector2 RemapMouseToVirtual() {
    Vector2 mouse = GetMousePosition();
    float scaleX = (float)VIRTUAL_WIDTH / (float)GetScreenWidth();
    float scaleY = (float)VIRTUAL_HEIGHT / (float)GetScreenHeight();
    return { mouse.x * scaleX, mouse.y * scaleY };
}

} // namespace Ui
