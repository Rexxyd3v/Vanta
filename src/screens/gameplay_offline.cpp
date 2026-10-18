#include "main_level.h"
#include "../ui/game_settings.h"
#include "../ui/ui_theme.h"
#include "../ui/character_preview.h"
#include "../rules/match_rules.h"
#include <algorithm>
#include <cmath>
#include "../audio/combat_audio.h"

namespace {
float Separation(Vector2 a,Vector2 b) { float x=a.x-b.x,y=a.y-b.y; return std::sqrt(x*x+y*y); }
}

bool GameplayScreen::OfflineEnemies(const Character* a,const Character* b) const {
    return a!=b && (!Offline::TeamMode(offlineSetup.mode) || a->GetTeamID()!=b->GetTeamID());
}
Character* GameplayScreen::NearestOfflineEnemy(Character* source) const {
    Character* nearest=nullptr; float distance=1e10f;
    auto consider=[&](Character* candidate) {
        if (candidate->IsDead() || !OfflineEnemies(source,candidate)) return;
        float d=Separation(source->GetPosition(),candidate->GetPosition());
        if (d<distance) { nearest=candidate; distance=d; }
    };
    consider(player); for (auto& bot:bots) consider(bot.get()); return nearest;
}
void GameplayScreen::InitializeOffline() {
    offlineSetup=Offline::Get(); Offline::Normalize(offlineSetup);
    player->SetTeamID(Offline::TeamMode(offlineSetup.mode)?offlineSetup.playerTeam:0);
    if (player->GetTeamID()) player->SetPosition(GetTeamSpawnPoint(player->GetTeamID()));
    for (int i=0;i<offlineSetup.bots;++i) {
        int team=Offline::TeamMode(offlineSetup.mode)?Offline::BotTeam(i,offlineSetup.playerTeam):0;
        auto bot=std::make_unique<BotEnemy>(team?GetTeamSpawnPoint(team):offlineSetup.mode==Offline::Mode::FreeForAll?RandomArenaSpawn():GetFarSpawnPoint(),CharacterPreview::BodyPath(i%4+1));
        bot->SetTeamID(team); bot->SetDifficulty(offlineSetup.difficulty);
        bot->SetName(std::string(team==1?"GR ":team==2?"BL ":"")+"BOT "+std::to_string(i+1));
        ResolveWorldCollision(bot.get()); ClampCharacterToWorld(bot.get());
        bots.push_back(std::move(bot));
    }
    if (offlineSetup.mode==Offline::Mode::FreeForAll) player->SetPosition(RandomArenaSpawn(player));
    ResetFlag(1); ResetFlag(2);
    if (Offline::RoundMode(offlineSetup.mode)) StartOfflineRound();
}
void GameplayScreen::StartOfflineRound() {
    ++currentRoundNumber; roundElapsed=0; roundBannerTimer=0; offlineRoundOver=false; isSpectating=false;
    ResetGrenades();
    player->ResetHealth(100); player->SetPosition(GetTeamSpawnPoint(player->GetTeamID()));
    ResolveWorldCollision(player); ClampCharacterToWorld(player);
    for (auto& bot:bots) {
        bot->ResetHealth(100); bot->ResetDeathTimer(); bot->SetPosition(GetTeamSpawnPoint(bot->GetTeamID()));
        ResolveWorldCollision(bot.get()); ClampCharacterToWorld(bot.get());
    }
    ApplyActiveBag(true);
    deadFor=0;lifeDamage.clear();deathKiller.clear();spectateTarget=nullptr;
}
void GameplayScreen::RecordOfflineKill(Character* killer,Character* victim,int weapon,int skin) {
    if(weapon<0) { weapon=killer->GetWeaponIndex(); skin=killer->GetWeaponSkin(); }
    AddKillFeed(killer->GetName(),victim->GetName(),weapon,skin,killer->GetTeamID(),victim->GetTeamID(),killer==player,victim==player);
    if(victim==player)BeginDeath(killer,weapon,skin);
    if(killer==player && killer!=victim){killConfirm=.65f;CombatAudio::Play(CombatAudio::Kill,player->GetPosition(),.7f);}
    NoteMedalKill((uint64_t)(uintptr_t)killer,(uint64_t)(uintptr_t)victim,(uint64_t)(uintptr_t)player,OfflineEnemies(killer,victim));
    victim->AddDeath();
    if (killer!=victim) {
        killer->AddKill();
        if (offlineSetup.mode==Offline::Mode::TeamDeathmatch && OfflineEnemies(killer,victim)) ++offlineScores[killer->GetTeamID()];
    }
}
void GameplayScreen::EndOfflineRound(int winner,const char* reason) {
    if (offlineRoundOver || matchEndTimer>0) return;
    if (winner>0) ++offlineScores[winner];
    lastRoundWinnerID=winner; offlineResult=reason;
    offlineRoundOver=true; roundBannerTimer=3; isSpectating=true;
    if (winner>0 && offlineScores[winner]>=offlineSetup.roundLimit) {
        matchWinner=winner; matchEndTimer=6; roundBannerTimer=0;
        offlineResult=std::string(winner==1?"GLOBAL RISK":"BLACK LIST")+" wins / "+reason;
    }
}
void GameplayScreen::UpdatePractice(float dt) {
    const bool fighting=matchEndTimer<=0 && !offlineRoundOver;
    if(fighting && IsFlagDeathmatch())PlanFlagBots();
    for (size_t botIndex=0;botIndex<bots.size();++botIndex) {
        auto& bot=bots[botIndex];
        Vector2 previousBotPosition=bot->GetPosition();
        if (fighting && !bot->IsDead()) {
            PrepareBotTactics(bot.get());
            Character* enemy=NearestOfflineEnemy(bot.get());
            bool objective=false; Vector2 destination{};
            if (offlineSetup.mode==Offline::Mode::Deathmatch) {
                destination=FlagBotWaypoint(bot.get(),dt);
                objective=true;
                const auto& plan=flagBotPlans[bot.get()];
                Character* carrier=flags[3-bot->GetTeamID()].carrier;
                if(plan.role==FlagBotRole::Interceptor && carrier && !carrier->IsDead())enemy=carrier;
                if(flags[bot->GetTeamID()].carrier==bot.get() && enemy && Separation(enemy->GetPosition(),bot->GetPosition())<450 && bot->GetHealth()<70)bot->ActivateShield();
            }
            if(IsFreeForAll() && !objective) {
                float best=800;Vector2 from=bot->GetPosition();
                for(auto& node:bonusNodes)if(node.cooldown<=0 && (node.kind<3?bot->GetBonusAmmo()<=0:bot->GetBonusCharges()<=0)) {
                    float distance=Separation(from,node.position);if(distance>=best || (enemy && Separation(from,enemy->GetPosition())<260))continue;
                    bool clear=true;int steps=std::max(1,(int)(distance/24));
                    for(int k=1;k<=steps;++k)if(!ArenaClear({from.x+(node.position.x-from.x)*k/steps,from.y+(node.position.y-from.y)*k/steps},12)){clear=false;break;}
                    if(clear){destination=node.position;objective=true;best=distance;}
                }
            }
            if(IsFreeForAll() && enemy && bot->BonusEquipped() && bot->GetBonusGun()==0 && Separation(bot->GetPosition(),enemy->GetPosition())>210) {objective=true;destination=enemy->GetPosition();}
            if (objective) {
                bot->MoveToObjective(dt,destination,IsFlagDeathmatch()?FlagBotSpacing(bot.get()):Vector2{});
                if (!bot->IsEvading() && enemy && BotSight(bot->GetHandPosition(),enemy->GetPosition()) && Separation(enemy->GetPosition(),bot->GetPosition())<(bot->BonusEquipped() && bot->GetBonusGun()==0?310:650)) bot->Shoot(enemy->GetPosition());
            }
            else if (enemy) bot->UpdateAI(dt,enemy->GetPosition());
        }
        bot->Update(dt); ResolveWorldMovement(bot.get(),previousBotPosition); ClampCharacterToWorld(bot.get());
        if (fighting && !Offline::RoundMode(offlineSetup.mode) && bot->ShouldRespawn(IsFlagDeathmatch()?Rules::FlagRespawnDelay:0)) {
            bot->ResetHealth(100); bot->ResetDeathTimer();
            bot->SetPosition(bot->GetTeamID()?GetTeamSpawnPoint(bot->GetTeamID()):offlineSetup.mode==Offline::Mode::FreeForAll?RandomArenaSpawn(bot.get()):GetFarSpawnPoint());
            ResolveWorldCollision(bot.get()); ClampCharacterToWorld(bot.get());
        }
    }
    UpdateBulletWorld();
    if (!fighting) return;
    std::vector<Character*> fighters{player}; for (auto& bot:bots) fighters.push_back(bot.get());
    for (Character* shooter:fighters) for (auto& bullet:shooter->GetProjectiles()) {
        if (!bullet->IsActive()) continue;
        for (Character* victim:fighters) {
            if (victim->IsDead() || !OfflineEnemies(shooter,victim)) continue;
            Vector2 p=victim->GetPosition(); p.y-=victim->GetJumpHeight();
            if (!CheckCollisionCircleRec(bullet->GetPosition(),3,{p.x-22,p.y,44,60})) continue;
            const float multiplier[]={0.65f,1.0f,1.25f};
            float before=victim->GetHealth(); victim->TakeDamage(bullet->GetDamage()*(shooter==player?1:multiplier[offlineSetup.difficulty]));
            bullet->Deactivate();
            if (victim->GetHealth()<before) {
                NoteDamage(shooter,victim,before-victim->GetHealth(),bullet->GetWeaponIndex(),bullet->GetWeaponSkin(),bullet->GetShotID());
            }
            if (victim->IsDead()) RecordOfflineKill(shooter,victim,bullet->GetWeaponIndex(),bullet->GetWeaponSkin());
            break;
        }
    }
}
void GameplayScreen::UpdateOfflineFlow(float dt) {
    if (matchEndTimer>0) {
        return;
    }
    if (offlineRoundOver) {
        roundBannerTimer=std::max(0.0f,roundBannerTimer-dt);
        if (roundBannerTimer<=0) StartOfflineRound();
        return;
    }
    elapsed+=dt; roundElapsed+=dt;
    if (offlineSetup.mode==Offline::Mode::Training) return;
    if (Offline::RoundMode(offlineSetup.mode)) {
        int alive[3]={0,0,0};
        if (!player->IsDead()) ++alive[player->GetTeamID()];
        for (auto& bot:bots) if (!bot->IsDead()) ++alive[bot->GetTeamID()];
        if (!alive[1]) EndOfflineRound(alive[2]?2:0,alive[2]?"Global Risk eliminated":"Both squads eliminated");
        else if (!alive[2]) EndOfflineRound(1,"Black List eliminated");
        else if (roundElapsed>=Rules::RoundSeconds) EndOfflineRound(alive[1]>alive[2]?1:alive[2]>alive[1]?2:0,"Round time expired");
        return;
    }
    if (offlineSetup.mode==Offline::Mode::Deathmatch) {
        UpdateFlags(dt,Settings::Pressed(Settings::Interact));
        if (std::max(offlineScores[1],offlineScores[2])>=offlineSetup.roundLimit || elapsed>=offlineSetup.timeLimit) {
            matchEndTimer=6; isSpectating=true;
            matchWinner=offlineScores[1]>offlineScores[2]?1:offlineScores[2]>offlineScores[1]?2:-1;
            offlineResult=matchWinner<0?"DEATHMATCH DRAW":std::string(matchWinner==1?"GLOBAL RISK":"BLACK LIST")+" wins deathmatch";
        }
        return;
    }
    int top=player->GetKills(); Character* leader=player; bool tied=false;
    for (auto& bot:bots) {
        if (bot->GetKills()>top) { top=bot->GetKills(); leader=bot.get(); tied=false; }
        else if (bot->GetKills()==top) tied=true;
    }
    if (offlineSetup.mode==Offline::Mode::TeamDeathmatch) top=std::max(offlineScores[1],offlineScores[2]);
    if (Rules::LimitReached(offlineSetup.timeLimit,offlineSetup.killLimit,elapsed,top)) {
        matchEndTimer=6; isSpectating=true;
        if (offlineSetup.mode==Offline::Mode::TeamDeathmatch) {
            matchWinner=offlineScores[1]>offlineScores[2]?1:offlineScores[2]>offlineScores[1]?2:-1;
            offlineResult=matchWinner<0?"Draw":std::string(matchWinner==1?"GLOBAL RISK":"BLACK LIST")+" wins";
        } else offlineResult=tied?"Draw":leader->GetName()+" wins";
    }
}
