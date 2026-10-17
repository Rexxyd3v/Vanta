#ifndef PLAYER_H
#define PLAYER_H

#include "character.h"

class Player : public Character {
public:
    // playerIndex: 0 = WASD + mouse,  1 = Arrow keys + auto-aim
    Player(Vector2 startPosition, int playerIndex = 0, const std::string& skinPath = "", int weaponSkin = 0);
    ~Player() override;

    void Update(float deltaTime) override;
    void UpdateWithoutInput(float deltaTime);
    int  GetPlayerIndex() const { return playerIndex; }

private:
    int playerIndex;
};

#endif // PLAYER_H
