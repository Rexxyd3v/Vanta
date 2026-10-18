#ifndef PROJECTILE_H
#define PROJECTILE_H

#include <raylib.h>
#include <cstdint>

class Projectile {
public:
    Projectile(Vector2 startPosition, Vector2 direction, float speed, float maxDistance, float damage, Color fillColor = YELLOW, Color lineColor = ORANGE);
    ~Projectile();

    void    Update(float deltaTime);
    void    Draw();
    bool    IsActive() const;
    void    Deactivate();   // Force-deactivate on hit
    Vector2 GetPosition() const;
    float   GetDamage() const { return damage; }
    void SetWeaponSource(int slot,int skin) { weaponIndex=slot; weaponSkin=skin; }
    int GetWeaponIndex() const { return weaponIndex; }
    int GetWeaponSkin() const { return weaponSkin; }
    void SetShotID(uint32_t id){shotID=id;}
    uint32_t GetShotID()const{return shotID;}
    Vector2 GetPreviousPosition()const{return previousPosition;}

private:
    uint32_t shotID=0;
    Vector2 previousPosition{};
    int weaponIndex=0,weaponSkin=0;
    Vector2 position;
    Vector2 direction;
    float   speed;
    bool    active;
    float   maxDistance;
    float   distanceTraveled;
    float   damage;
    Color   fillColor;
    Color   lineColor;
};

#endif // PROJECTILE_H
