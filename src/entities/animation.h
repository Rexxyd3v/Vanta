#ifndef ANIMATION_H
#define ANIMATION_H

#include <raylib.h>
#include <vector>
#include <string>

class Animation {
public:
    Animation(const std::string& pathPrefix, int frameCount, float frameDuration, bool loop);
    ~Animation();

    void Update(float deltaTime);
    void Draw(Vector2 position, int faceDirection, float scale = 1.0f, float rotation = 0.0f, Color tint = WHITE);
    void Reset();
    bool IsFinished() const;
    int FrameWidth() const;
    int FrameHeight() const;
    float GroundFootOffset(float scale) const;
    bool HasFrames() const { return !frames.empty() && frames[0].id != 0; }

private:
    std::vector<Texture2D> frames;
    float frameDuration;
    float elapsedTime;
    int currentFrame;
    bool loop;
    bool finished;
};

#endif // ANIMATION_H
