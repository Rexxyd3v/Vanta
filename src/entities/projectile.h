#ifndef PROJECTILE_H
#define PROJECTILE_H

#include <raylib.h>

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

private:
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
