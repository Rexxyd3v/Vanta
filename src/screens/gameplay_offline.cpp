#include "main_level.h"
#include "../ui/game_settings.h"
#include "../ui/ui_theme.h"
#include "../ui/character_preview.h"
#include "../rules/match_rules.h"
#include <algorithm>
#include <cmath>

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
        auto bot=std::make_unique<BotEnemy>(team?GetTeamSpawnPoint(team):offlineSetup.mode==Offline::Mode::FreeForAll?GetDeathmatchSpawnPoint(i+1):GetFarSpawnPoint(),CharacterPreview::BodyPath(i%4+1));
        bot->SetTeamID(team); bot->SetDifficulty(offlineSetup.difficulty);
        bot->SetName(std::string(team==1?"GR ":team==2?"BL ":"")+"BOT "+std::to_string(i+1));
        ResolveWorldCollision(bot.get()); ClampCharacterToWorld(bot.get());
        bots.push_back(std::move(bot));
    }
    if (offlineSetup.mode==Offline::Mode::FreeForAll) player->SetPosition(GetDeathmatchSpawnPoint(0));
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
    deadFor=0;
}
void GameplayScreen::RecordOfflineKill(Character* killer,Character* victim,int weapon,int skin) {
    if(weapon<0) { weapon=killer->GetWeaponIndex(); skin=killer->GetWeaponSkin(); }
    AddKillFeed(killer->GetName(),victim->GetName(),weapon,skin,killer->GetTeamID(),victim->GetTeamID(),killer==player,victim==player);
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
    for (size_t botIndex=0;botIndex<bots.size();++botIndex) {
        auto& bot=bots[botIndex];
        Vector2 previousBotPosition=bot->GetPosition();
        if (fighting && !bot->IsDead()) {
            Character* enemy=NearestOfflineEnemy(bot.get());
            bool objective=false; Vector2 destination{};
            if (offlineSetup.mode==Offline::Mode::Deathmatch) {
                int team=bot->GetTeamID(),opponent=3-team;
                const auto& own=flags[team]; const auto& other=flags[opponent];
                if (own.carrier==bot.get()) destination=teamBasePositions[opponent];
                else if (other.carrier && Separation(other.carrier->GetPosition(),teamBasePositions[team])<850) destination=other.carrier->GetPosition();
                else if (other.dropped && Separation(other.position,bot->GetPosition())<350) destination=other.position;
                else if (own.carrier) {
                    Vector2 p=own.carrier->GetPosition(); destination={p.x+(team==1?-75.0f:75.0f),p.y+(bot->GetName().back()%2?65.0f:-65.0f)};
                } else destination=own.position;
                objective=true;
            }
            if (objective) {
                bot->MoveToObjective(dt,destination);
                if (enemy && Separation(enemy->GetPosition(),bot->GetPosition())<650) bot->Shoot(enemy->GetPosition());
            }
            else if (enemy) bot->UpdateAI(dt,enemy->GetPosition());
        }
        bot->Update(dt); ResolveWorldMovement(bot.get(),previousBotPosition); ClampCharacterToWorld(bot.get());
        if (fighting && !Offline::RoundMode(offlineSetup.mode) && bot->ShouldRespawn()) {
            bot->ResetHealth(100); bot->ResetDeathTimer();
            bot->SetPosition(bot->GetTeamID()?GetTeamSpawnPoint(bot->GetTeamID()):offlineSetup.mode==Offline::Mode::FreeForAll?GetDeathmatchSpawnPoint((uint32_t)botIndex+1):GetFarSpawnPoint());
            ResolveWorldCollision(bot.get()); ClampCharacterToWorld(bot.get());
        }
    }
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
                if (shooter==player) RegisterHit(p,before-victim->GetHealth());
                if (victim==player) { damageFlash=0.5f; damageSource=shooter->GetPosition(); }
            }
            if (victim->IsDead()) RecordOfflineKill(shooter,victim,bullet->GetWeaponIndex(),bullet->GetWeaponSkin());
            break;
        }
    }
}
void GameplayScreen::UpdateOfflineFlow(float dt) {
    if (matchEndTimer>0) {
        matchEndTimer=std::max(0.001f,matchEndTimer-dt);
        if (matchEndTimer<=0.001f) returnToLobby=true;
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
