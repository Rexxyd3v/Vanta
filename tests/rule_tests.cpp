#include "../src/rules/match_rules.h"
#include "../src/rules/grenade_rules.h"
#include "../src/ui/offline_setup.h"
#include "../src/entities/weapon_catalog.h"
#include "../src/entities/bag_catalog.h"
#include <cstdlib>
#include <iostream>
static void check(bool ok, const char* label) { if (!ok) { std::cerr << label << '\n'; std::exit(1); } }
int main() {
    auto bags=Bags::Defaults();
    for(auto& bag:bags) check(Bags::IsPrimary(bag.primary) && Bags::IsSecondary(bag.secondary),"every default bag has correctly categorized weapons");
    bags[0].primary=2;bags[0].secondary=4;bags[0].melee=99;bags[0].primarySkin=8;Bags::Normalize(bags[0]);
    check(bags[0].primary==0 && bags[0].secondary==2 && bags[0].melee==1 && bags[0].primarySkin==3,"invalid bag equipment normalizes to legal storage slots");
    check(GrenadeRules::FlashDuration(100)>GrenadeRules::FlashDuration(400) && GrenadeRules::FlashDuration(600)==0,"flashbang strength falls off with distance");
    check(GrenadeRules::FlashDuration(100,true)>GrenadeRules::FlashDuration(100,false),"looking away reduces flash duration");
    std::array<int,WeaponCount> choices{1,2,3,0,2,3,2,1};
    int encoded=PackWeaponSkins(choices);
    for(int i=0;i<WeaponCount;++i) check(WeaponSkinSelection(encoded,i)==choices[i],"mixed per-gun selections survive encoding");
    for(int i=0;i<WeaponCount;++i) check(WeaponSkinSelection(2,i)==2,"legacy shared skin ID remains compatible");
    choices[3]=3; int changed=PackWeaponSkins(choices);
    check(WeaponSkinSelection(changed,0)==1 && WeaponSkinSelection(changed,3)==3 && WeaponSkinSelection(changed,4)==2,"changing rifle skin leaves other guns unchanged");
    check(Rules::FriendlyFire(true, 1, 1), "teammate damage blocked");
    check(!Rules::FriendlyFire(false, 1, 1), "FFA permits combat");
    check(!Rules::FriendlyFire(true, 1, 2), "enemies take damage");
    check(!Rules::CanRespawn(true, false, 20), "no elimination respawn");
    check(!Rules::CanRespawn(false, true, 20), "no post-match respawn");
    check(!Rules::CanRespawn(false, false, 2.9f), "respawn delay enforced");
    check(Rules::CanRespawn(false, false, 3), "respawn unlocks at delay");
    check(!Rules::CanRespawn(false,false,7.99f,true),"flag respawn stays locked for eight seconds");
    check(Rules::CanRespawn(false,false,8.0f,true),"flag respawn unlocks at eight seconds");
    check(!Rules::CanRespawn(true,false,20,true),"flag delay never overrides elimination");
    check(!Rules::TeamsValid({1}, true), "solo online match rejected");
    check(!Rules::TeamsValid({1,1}, true), "empty opposing team rejected");
    check(!Rules::TeamsValid({1,1,1,2}, true), "unbalanced teams rejected");
    check(!Rules::TeamsValid({1,0,2}, true), "unassigned team rejected");
    check(Rules::TeamsValid({1,1,2}, true), "odd balanced roster accepted");
    check(Rules::TeamsValid({0,0}, false), "FFA needs no teams");
    check(!Rules::LimitReached(300,0,299.9f,100), "time match ignores kills");
    check(Rules::LimitReached(300,0,300,0), "timer expires at limit");
    check(Rules::LimitReached(0,40,1,40), "kill target ends match");
    check(!Rules::LimitReached(0,40,900,39), "kill match ignores elapsed time");
    check(GrenadeRules::PoisonTicks(0.8f)==0, "no poison damage during fuse");
    check(GrenadeRules::PoisonTicks(1.3f)==1, "first poison tick after half second");
    check(GrenadeRules::PoisonTicks(2.3f)==3, "three exposure ticks in 1.5 seconds");
    check(GrenadeRules::PoisonTicks(100)==16, "poison stops after eight seconds");
    int ticks=0;
    for (int i=0;i<528;++i) ticks+=GrenadeRules::PoisonTicks((i+1)/60.0f)-GrenadeRules::PoisonTicks(i/60.0f);
    check(ticks==16, "tick count independent of frame rate");
    check(GrenadeRules::SmokeRadius(0)<GrenadeRules::SmokeRadius(1), "smoke expands");
    check(GrenadeRules::SmokeRadius(3)==GrenadeRules::SmokeRadius(8), "smoke growth capped");
    check(GrenadeRules::PoisonAffects(true,true,1,1), "thrower takes own poison in team modes");
    check(!GrenadeRules::PoisonAffects(false,true,1,1), "teammates retain friendly fire protection");
    check(GrenadeRules::PoisonAffects(false,true,1,2), "enemy smoke damages player");
    check(GrenadeRules::PoisonAffects(false,false,1,1), "FFA poison damages any player");
    check(GrenadeRules::ShakeStrength(0)>GrenadeRules::ShakeStrength(400), "nearby explosion shakes harder");
    check(GrenadeRules::ShakeStrength(850)==0 && GrenadeRules::ShakeStrength(1200)==0, "distant explosion does not shake");
    Offline::Setup offline; offline.mode=Offline::Mode::TeamDeathmatch; offline.bots=-5; offline.difficulty=9;
    Offline::Normalize(offline); check(offline.bots==1 && offline.difficulty==2,"offline team setup has opponents and valid difficulty");
    offline.mode=Offline::Mode::Training; offline.bots=0; Offline::Normalize(offline); check(offline.bots==0,"training allows zero bots");
    check(Offline::BotTeam(0,1)==2 && Offline::BotTeam(1,1)==1,"bots balance squads including player");
    check(!Offline::RoundMode(Offline::Mode::Deathmatch) && Offline::RoundMode(Offline::Mode::Elimination),"flag mode permits respawns while elimination uses rounds");
    std::cout << "34 match, grenade, and offline setup checks passed\n";
}
