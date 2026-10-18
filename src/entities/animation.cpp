#include "animation.h"
#include <map>
#include "../utils/texture_manager.h"

Animation::Animation(const std::string& pathPrefix, int frameCount, float frameDuration, bool loop) 
    : frameDuration(frameDuration), loop(loop), elapsedTime(0.0f), currentFrame(0), finished(false) {
    for (int i = 0; i < frameCount; ++i) {
        std::string path = pathPrefix + std::to_string(i) + ".png";
        Texture2D tex = TextureManager::GetTexture(path);
        frames.push_back(tex);
    }
}

Animation::~Animation() {
    
}

void Animation::Update(float deltaTime) {
    if (finished || frames.empty()) return;

    elapsedTime += deltaTime;
    if (elapsedTime >= frameDuration) {
        elapsedTime -= frameDuration;
        currentFrame++;
        
        if (currentFrame >= frames.size()) {
            if (loop) {
                currentFrame = 0;
            } else {
                currentFrame = (int)frames.size() - 1;
                finished = true;
            }
        }
    }
}

void Animation::Draw(Vector2 position, int faceDirection, float scale, float rotation, Color tint) {
    if (frames.empty()) return;

    Texture2D tex = frames[currentFrame];
    Rectangle sourceRec = { 0.0f, 0.0f, (float)tex.width * faceDirection, (float)tex.height };
    Rectangle destRec = { position.x, position.y, (float)tex.width * scale, (float)tex.height * scale };
    Vector2 origin = { (float)tex.width * scale / 2.0f, (float)tex.height * scale / 2.0f };

    DrawTexturePro(tex, sourceRec, destRec, origin, rotation, tint);
}

void Animation::Reset() {
    currentFrame = 0;
    elapsedTime = 0.0f;
    finished = false;
}

bool Animation::IsFinished() const {
    return finished;
}

int Animation::FrameWidth() const {
    if (frames.empty()) return 0;
    return frames[0].width;
}

int Animation::FrameHeight() const {
    if (frames.empty()) return 0;
    return frames[0].height;
}

float Animation::GroundFootOffset(float scale) const {
    if (!HasFrames()) return 0;
    // Cache the visible foot row; transparent sprite padding is not physical terrain.
    static std::map<unsigned int,int> rows;
    const auto& texture=frames.front();
    auto found=rows.find(texture.id);
    if(found==rows.end()) {
        Image image=LoadImageFromTexture(texture);
        Color* pixels=LoadImageColors(image);
        int bottom=image.height/2;
        if(pixels) for(int y=image.height-1;y>=0;--y) {
            int solid=0;
            for(int x=0;x<image.width;x+=2) if(pixels[y*image.width+x].a>40) ++solid;
            if(solid>=4) { bottom=y+1;break; }
        }
        if(pixels) UnloadImageColors(pixels);
        UnloadImage(image);found=rows.emplace(texture.id,bottom).first;
    }
    return (found->second-texture.height*0.5f)*scale;
}
