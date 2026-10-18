
#include "projectile.h"
#include <math.h>
#include <cmath>
#include <algorithm>

Projectile::Projectile(Vector2 startPosition, Vector2 direction, float speed, float maxDistance, float damage, Color fillColor, Color lineColor)
    : position(startPosition), direction(direction), speed(speed),
      active(true), maxDistance(maxDistance), distanceTraveled(0.0f),
      damage(damage), fillColor(fillColor), lineColor(lineColor) {
    previousPosition=startPosition;
    float length=sqrtf(direction.x*direction.x+direction.y*direction.y);
    if (std::isfinite(length) && length>0.001f) {
        this->direction={direction.x/length,direction.y/length};
    } else active=false;
}

Projectile::~Projectile() {}

void Projectile::Update(float deltaTime) {
    if (!active) return;

    previousPosition=position;
    float stepX = speed * direction.x * deltaTime;
    float stepY = speed * direction.y * deltaTime;

    position.x += stepX;
    position.y += stepY;

    distanceTraveled += sqrtf(stepX*stepX + stepY*stepY);

    if (distanceTraveled >= maxDistance) {
        active = false;
    }
}

void Projectile::Draw() {
    if (!active) return;
    float length=std::min(std::clamp(speed*0.028f,22.0f,34.0f),distanceTraveled+6.0f);
    Vector2 tail{position.x-direction.x*length,position.y-direction.y*length};
    Vector2 core{position.x-direction.x*length*0.66f,position.y-direction.y*length*0.66f};
    DrawLineEx(tail,position,6,Fade(lineColor,0.22f));
    DrawLineEx(tail,position,3.5f,fillColor);
    DrawLineEx(core,position,1.3f,{255,250,218,255});
}

void Projectile::Deactivate() {
    active = false;
}

bool Projectile::IsActive() const {
    return active;
}

Vector2 Projectile::GetPosition() const {
    return position;
}