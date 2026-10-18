#include "main_level.h"
#include "../ui/game_settings.h"
#include "../ui/ui_widgets.h"
#include "../ui/ui_theme.h"
#include "../ui/weapon_skin_catalog.h"
#include "../ui/equipment_art.h"
#include "../entities/bag_catalog.h"
#include "../network/network_manager.h"
#include <cmath>

void GameplayScreen::ApplyActiveBag(bool respawn) {
    auto& prefs=Settings::Get();activeBag=std::clamp(prefs.activeBag,0,2);
    auto& bag=prefs.bags[activeBag];Bags::Normalize(bag);
    int skins=Bags::PackedSkins(bag,prefs.weaponSkins);
    player->ConfigureBag(bag,skins);NetworkManager::GetInstance().localWeaponSkin=skins;
    if(respawn) { bagSpawnPosition=player->GetPosition();bagMenuOpen=false;bagMessageTimer=0; }
}
bool GameplayScreen::CanChangeBag() const {
    if(matchEndTimer>0) return false;
    if(player->IsDead()) return true;
    if(IsFreeForAll()) return spawnGrace>0;
    Vector2 base=bagSpawnPosition;
    if(player->GetTeamID()>0 && HasTeamBases()) base=teamBasePositions[player->GetTeamID()];
    else if(HasDeathmatchBases()) {
        base=GetDeathmatchSpawnPoint(currentMode==GameMode::ONLINE?NetworkManager::GetInstance().GetLocalPlayerID():0);
    }
    Vector2 p=player->GetPosition();return std::hypot(p.x-base.x,p.y-base.y)<=180;
}
bool GameplayScreen::SelectBag(int index) {
    if(index<0 || index>=3 || !CanChangeBag()) { bagMessageTimer=2.5f;return false; }
    bagInputConsumed=true;bagMessageTimer=0;Settings::Get().activeBag=index;ApplyActiveBag();Settings::Save();bagMenuOpen=false;return true;
}
void GameplayScreen::UpdateBagControls(float dt) {
    bagMessageTimer=std::max(0.0f,bagMessageTimer-dt);
    bagInputConsumed=bagMenuOpen;
    if(isPaused || Settings::BlocksInput() || !IsWindowFocused()) return;
    if(IsKeyPressed(KEY_B)) { bagMenuOpen=!bagMenuOpen;bagInputConsumed=true; }
    if(IsKeyPressed(KEY_F1)) SelectBag(0);
    if(IsKeyPressed(KEY_F2)) SelectBag(1);
    if(IsKeyPressed(KEY_F3)) SelectBag(2);
    if(bagMenuOpen && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        Vector2 mouse=Ui::RemapMouseToVirtual();
        for(int i=0;i<3;++i) if(CheckCollisionPointRec(mouse,{365+i*194.0f,360,182,108})) SelectBag(i);
    }
}
void GameplayScreen::UpdateEquipmentCombat() {
    if(player->ConsumeMeleeHit() && !player->IsDead() && matchEndTimer<=0) {
        Character* closest=nullptr;float nearest=Bags::MeleeRange(player->GetMeleeVariant());
        Vector2 from=player->GetPosition(),aim=player->GetLastShotDirection();
        auto consider=[&](Character* target) {
            if(target->IsDead() || (currentMode==GameMode::PRACTICE?!OfflineEnemies(player,target):
               (player->GetTeamID()!=0 && player->GetTeamID()==target->GetTeamID()))) return;
            Vector2 p=target->GetPosition();float dx=p.x-from.x,dy=p.y-from.y,distance=std::hypot(dx,dy);
            if(distance<nearest && (distance<1 || (dx*aim.x+dy*aim.y)/distance>.3f)) { nearest=distance;closest=target; }
        };
        if(currentMode==GameMode::PRACTICE) for(auto& bot:bots) consider(bot.get());
        else for(auto* remote:remotePlayers) consider(remote);
        if(closest) {
            float damage=Bags::MeleeDamage(player->GetMeleeVariant());
            if(currentMode==GameMode::PRACTICE) {
                float before=closest->GetHealth();closest->TakeDamage(damage);
                if(before>closest->GetHealth())NoteDamage(player,closest,before-closest->GetHealth(),Bags::MeleeDamageId,player->GetMeleeVariant()*4+player->GetMeleeSkin());
                if(closest->IsDead()) RecordOfflineKill(player,closest,Bags::MeleeDamageId,player->GetMeleeVariant()*4+player->GetMeleeSkin());
            } else {
                auto* remote=static_cast<RemotePlayer*>(closest);
                PacketPlayerDamage packet{};packet.header={PacketType::PLAYER_DAMAGE,NetworkManager::GetInstance().GetLocalPlayerID()};
                packet.targetPlayerID=remote->peerID;packet.damageAmount=damage;packet.weaponIndex=Bags::MeleeDamageId;
                packet.weaponSkin=player->GetMeleeVariant()*4+player->GetMeleeSkin();
                NetworkManager::GetInstance().SendPacket(&packet,sizeof(packet),true);
            }
        }
    }
    if(!bagMenuOpen && !bagInputConsumed && !isPaused && !Settings::BlocksInput() && IsWindowFocused() && player->GetUtilityItem()>=0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if(ThrowGrenade(player->GetUtilityItem())) player->SelectEquipment(0);
    }
}
void GameplayScreen::DrawBagHud() {
    const auto& bag=player->GetBag();Color gold=UiTheme::AccentGold(),muted{163,184,201,255};
    DrawText(TextFormat("BAG %d / B",activeBag+1),36,184,12,gold);
    if(CanChangeBag()) DrawText("BAG CHANGE AVAILABLE",36,202,9,muted);
    const char* labels[]={WeaponShortName(bag.primary),WeaponShortName(bag.secondary),"MELEE","FRAG","SMOKE","FLASH"};
    for(int i=0;i<6;++i) {
        if(i>=3 && !Bags::UtilityEquipped(bag,i-3)) continue;
        int x=710+i*90;bool selected=!player->BonusEquipped() && player->GetEquipmentSlot()==i;
        DrawText(TextFormat("%d %s",i+1,labels[i]),x,521,9,selected?gold:Fade(muted,.7f));
        if(selected) DrawLineEx({(float)x,539},{x+24.0f,539},2,gold);
    }
    if(bagMessageTimer>0) DrawText("CHANGE BAGS AT YOUR BASE OR AFTER RESPAWN",420,475,14,gold);
    if(!bagMenuOpen) return;
    DrawRectangleRounded({345,310,610,183},.12f,8,{9,17,27,242});
    DrawText(CanChangeBag()?"CHOOSE BAG / F1 - F3 / B CLOSE":"BAGS / RETURN TO BASE TO CHANGE",365,330,16,gold);
    for(int i=0;i<3;++i) {
        const auto& item=Settings::Get().bags[i];Rectangle r{365+i*194.0f,360,182,108};
        DrawRectangleRounded(r,.1f,8,Fade(gold,activeBag==i?.17f:.055f));
        DrawText(TextFormat("BAG %d",i+1),(int)r.x+12,(int)r.y+10,17,gold);
        DrawText(WeaponShortName(item.primary),(int)r.x+12,(int)r.y+43,14,WHITE);
        DrawText(WeaponShortName(item.secondary),(int)r.x+12,(int)r.y+65,11,muted);
        DrawText(Bags::MeleeName(item.melee),(int)r.x+12,(int)r.y+87,9,muted);
    }
}
