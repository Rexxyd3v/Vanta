#ifndef PLAYER_H
#define PLAYER_H

#include "character.h"
#include "bag_catalog.h"

class Player : public Character {
public:
    // playerIndex: 0 = WASD + mouse,  1 = Arrow keys + auto-aim
    Player(Vector2 startPosition, int playerIndex = 0, const std::string& skinPath = "", int weaponSkin = 0);
    ~Player() override;

    void Update(float deltaTime) override;
    void UpdateWithoutInput(float deltaTime);
    void ConfigureBag(const Bags::Loadout& bag,int packedSkins);
    void SelectEquipment(int slot);
    int GetEquipmentSlot() const { return equipmentSlot; }
    const Bags::Loadout& GetBag() const { return bagLoadout; }
    int  GetPlayerIndex() const { return playerIndex; }

private:
    int playerIndex;
    bool bagEnabled=false;
    int equipmentSlot=0;
    Bags::Loadout bagLoadout;
};

#endif // PLAYER_H
