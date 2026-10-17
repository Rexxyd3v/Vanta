#include "intro_screen.h"
#include "../utils/render_utils.h"
#include "../constants.h"

FirstScreen::FirstScreen() {
    gamefont = LoadFontEx("assets/fonts/BruceForeverRegular-X3jd2.ttf", 260, nullptr, 0);
    positionRx = 520.0f;
    positionRy = 150.0f;

    slideSpeed = 7.0f;
    slideLimit = 340.0f;
    fadeAlpha = 0.0f;
    fadeSpeed = 6.0f;

    isFading = true;
    isWaiting = false;
    isSliding = false;
    showGamestudio = false;
    isFadingOut = false;
    waitBeforeFadeOut = false;

    waitTime = 1.0f;
    fadeOutWaitTime = 2.0f;
    elapsedTime = 0.0f;
    fadeOutElapsedTime = 0.0f;

    // Second screen animation
    secondFont = LoadFontEx("assets/fonts/arial.ttf", 100, nullptr, 0);
    secondState = SecondScreenState::SECOND_SCREEN_EXPANDING;
    secondTopAndLeft = 50.0f;
    secondBottomAndRight = 50.0f;
    secondTextVisible = false;
    secondWaitTimer = 0.0f;
    secondFadeAlpha = 0.0f;
}

FirstScreen::~FirstScreen() {
    if (gamefont.texture.id != GetFontDefault().texture.id) {
        UnloadFont(gamefont);
    }
    if (secondFont.texture.id != GetFontDefault().texture.id) {
        UnloadFont(secondFont);
    }
}

bool FirstScreen::Update(float deltaTime) {
    // First screen: fade in R
    if (isFading) {
        fadeAlpha += fadeSpeed * (deltaTime * 60.0f);
        if (fadeAlpha >= 255.0f) {
            fadeAlpha = 255.0f;
            isFading = false;
            isWaiting = true;
        }
    }
    // Wait after fade in
    else if (isWaiting) {
        elapsedTime += deltaTime;
        if (elapsedTime >= waitTime) {
            isWaiting = false;
            isSliding = true;
        }
    }
    // Slide R to left
    else if (isSliding) {
        if (positionRx > slideLimit) {
            positionRx -= slideSpeed * (deltaTime * 60.0f);
        } else {
            isSliding = false;
            showGamestudio = true;
            waitBeforeFadeOut = false; // We'll use a different flag for second screen
        }
    }
    // Show Gamestudio text and line
    else if (showGamestudio) {
        // Just show, wait for fadeOutWaitTime
        fadeOutElapsedTime += deltaTime;
        if (fadeOutElapsedTime >= fadeOutWaitTime) {
            fadeOutElapsedTime = 0.0f;
            showGamestudio = false;
            // Start second screen animation
            secondState = SecondScreenState::SECOND_SCREEN_EXPANDING;
        }
    }
    // Second screen animation
    else {
        switch (secondState) {
            case SecondScreenState::SECOND_SCREEN_EXPANDING: {
                if (secondTopAndLeft < 300.0f) {
                    secondTopAndLeft += 100.0f * GetFrameTime();
                }
                if (secondBottomAndRight < 300.0f) {
                    secondBottomAndRight += 100.0f * GetFrameTime();
                }
                if (secondTopAndLeft >= 300.0f && secondBottomAndRight >= 300.0f) {
                    secondTextVisible = true;
                    secondState = SecondScreenState::SECOND_SCREEN_TEXT_WAIT;
                }
                break;
            }
            case SecondScreenState::SECOND_SCREEN_TEXT_WAIT: {
                secondWaitTimer += deltaTime;
                if (secondWaitTimer >= 2.0f) {
                    secondState = SecondScreenState::SECOND_SCREEN_FADING_OUT;
                }
                break;
            }
            case SecondScreenState::SECOND_SCREEN_FADING_OUT: {
                // Fade out: increase alpha from 0 to 255 over time
                // Original: fadeOpacity += 100.0f * GetFrameTime() / 255.0f; where fadeOpacity 0-1
                // We'll convert: secondFadeAlpha increase per second such that 255 reached in ~2.55 seconds? Let's match original.
                // Original fadeOpacity increment per second: 100.0 / 255.0 per frame? Actually per second: 100.0/255.0 per frame * 60? Let's just replicate.
                // We'll do: secondFadeAlpha += 100.0f * GetFrameTime(); // because original adds 100/255 per frame, and 1/255 of 255 is 1? Wait.
                // Let's compute: original fadeOpacity (0-1) increases by (100 * dt) / 255.
                // To convert to alpha 0-255: alpha = fadeOpacity * 255.
                // So delta_alpha = (100 * dt / 255) * 255 = 100 * dt.
                // Therefore, secondFadeAlpha += 100.0f * GetFrameTime();
                secondFadeAlpha += 100.0f * GetFrameTime();
                if (secondFadeAlpha >= 255.0f) {
                    secondFadeAlpha = 255.0f;
                    secondState = SecondScreenState::SECOND_SCREEN_DONE;
                }
                break;
            }
            case SecondScreenState::SECOND_SCREEN_DONE: {
                // Animation complete, transition to next screen
                return false;
            }
        }
    }

    return true;
}

void FirstScreen::Draw(RenderTexture2D target) {
    BeginTextureMode(target);

    if (isFading || isWaiting || isSliding || showGamestudio) {
        // First screen: black background
        ClearBackground(BLACK);

        // Draw first screen elements
        Color drawColor = {255, 255, 255, (unsigned char)fadeAlpha};
        Vector2 rPos = {positionRx, positionRy};
        DrawTextEx(gamefont, "R", rPos, 260.0f, 2.0f, drawColor);
        if (showGamestudio) {
            DrawLine(560, 310, 970, 310, drawColor);
            Vector2 studioPos = {560.0f, 260.0f};
            DrawTextEx(gamefont, "Gamestudio.", studioPos, 50.0f, 2.0f, drawColor);
        }
    }
    else {
        // Second screen: white background
        ClearBackground(RAYWHITE);

        // Draw second screen
        // Draw expanding lines
        DrawRectangle(500, 150, (int)secondTopAndLeft, 20, BLACK);
        DrawRectangle(500, 150, 20, (int)secondTopAndLeft, BLACK);
        DrawRectangle(800 - (int)secondBottomAndRight, 450, (int)secondBottomAndRight, 20, BLACK);
        DrawRectangle(780, 450 - (int)secondBottomAndRight, 20, (int)secondBottomAndRight, BLACK);
        DrawTextEx(secondFont, "www.raylib.com", (Vector2){1145, 655}, 20, 2, BLACK);
        DrawTextEx(secondFont, "@rjltyd", (Vector2){10, 655}, 20, 2, BLACK);
        if (secondTextVisible) {
            DrawText("Powered by:", 505, 115, 25, BLACK);
            DrawText("raylib", 620, 390, 50, BLACK);
            DrawTriangle((Vector2){770, 180}, (Vector2){650, 180}, (Vector2){770, 300}, RED);
        }
        // Draw fade out black rectangle if fading out or done
        if (secondState == SecondScreenState::SECOND_SCREEN_FADING_OUT || secondState == SecondScreenState::SECOND_SCREEN_DONE) {
            DrawRectangle(0, 0, VIRTUAL_WIDTH, VIRTUAL_HEIGHT, Fade(BLACK, secondFadeAlpha / 255.0f));
        }
    }

    EndTextureMode();
}