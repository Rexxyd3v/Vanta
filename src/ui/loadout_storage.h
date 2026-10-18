#pragma once
#include <raylib.h>
class LoadoutStorage {
public:
    void Update(float dt);
    void Draw(Font font);
    bool WantsClose() const { return close; }
    void Open();
    void SelectBag(int bag);
    void SelectSlot(int slot) { selectedSlot=slot; }
    void EquipGun(int gun,int skin=-1);
private:
    int selectedBag=0,selectedSlot=0;
    bool close=false;
    float clock=0;
    void Save();
};
