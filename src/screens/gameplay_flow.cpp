#include "main_level.h"
#include "../network/network_manager.h"
#include "../rules/match_rules.h"
#include "../ui/game_settings.h"
#include <algorithm>
#include <cmath>

void GameplayScreen::RegisterHit(Vector2 p, float damage) {
    hitMarker = 0.18f;
    if (damageNumbers.size() >= 32) damageNumbers.erase(damageNumbers.begin());
    damageNumbers.push_back({p, damage, 0.8f});
}

void GameplayScreen::RespawnLocal() {
    auto& nm = NetworkManager::GetInstance();
    player->SetGodMode(false);
    player->ResetHealth(100);
    grenadeCooldown[0] = grenadeCooldown[1] = 0;
    poisonExposure = 0;
    player->SetPosition(player->GetTeamID() ? GetTeamSpawnPoint(player->GetTeamID()) : currentMode==GameMode::PRACTICE && offlineSetup.mode==Offline::Mode::Training ? GetFarSpawnPoint() : GetDeathmatchSpawnPoint(nm.GetLocalPlayerID()));
    ResolveWorldCollision(player); ClampCharacterToWorld(player);
    deadFor = 0;
    eliminated.erase(nm.GetLocalPlayerID());
    if (currentMode == GameMode::ONLINE) {
        PacketPlayerRespawn p{};
        p.header = {PacketType::PLAYER_RESPAWN, nm.GetLocalPlayerID()};
        p.spawnPosition = player->GetPosition(); p.charSkin = nm.localSkinIndex; p.weaponSkin = nm.localWeaponSkin;
        nm.SendPacket(&p, sizeof(p), true);
    }
}

void GameplayScreen::ProcessKill(uint32_t killer, uint32_t victim) {
    auto& nm = NetworkManager::GetInstance();
    if (matchEndTimer > 0 || !nm.FindPlayer(killer) || !nm.FindPlayer(victim)) return;
    if (!eliminated.insert(victim).second) return;
    if (killer != victim) {
        if (killer == nm.GetLocalPlayerID()) nm.localKills++;
        else if (auto* rp = FindByPeerID(killer)) rp->AddKill();
    }
    if (victim == nm.GetLocalPlayerID()) nm.localDeaths++;
    else if (auto* rp = FindByPeerID(victim)) { rp->AddDeath(); rp->SetGodMode(false); rp->TakeDamage(10000); }
    if (killer != victim && nm.IsHost() && nm.currentGameMode == OnlineGameMode::TEAM_DEATHMATCH) {
        const auto* k = nm.FindPlayer(killer); const auto* v = nm.FindPlayer(victim);
        if (k->teamID && k->teamID != v->teamID) nm.teamScores[k->teamID]++;
    }
}

void GameplayScreen::UpdateMatchFlow(float dt) {
    if (currentMode != GameMode::ONLINE) return;
    auto& nm = NetworkManager::GetInstance();
    if (matchEndTimer > 0) {
        matchEndTimer = std::max(0.001f, matchEndTimer - dt);
        if (matchEndTimer <= 0.001f && nm.IsHost()) {
            PacketHeader p{PacketType::RETURN_LOBBY, 0}; nm.SendPacket(&p, sizeof(p), true);
            returnToLobby = true;
        }
        return;
    }
    elapsed += dt; roundElapsed += dt;
    roundBannerTimer = std::max(0.0f, roundBannerTimer - dt);
    UpdateOnlineFlags(dt);
    if (!nm.IsHost()) return;
    bool finished = false;
    if (nm.currentGameMode == OnlineGameMode::ELIMINATION) {
        if (roundInProgress) {
            int a = CountAliveOnTeam(1), b = CountAliveOnTeam(2);
            // A grace period lets clients receive ROUND_START and publish their reset health.
            if ((roundElapsed > 1 && (!a || !b)) || roundElapsed >= Rules::RoundSeconds) {
                int winner = a > b ? 1 : b > a ? 2 : 0;
                if (a && b && roundElapsed < Rules::RoundSeconds) winner = 0;
                if (winner) nm.teamScores[winner]++;
                BroadcastRoundEnd(winner, nm.teamScores[1], nm.teamScores[2]);
            }
        } else if (roundBannerTimer <= 0) {
            finished = nm.teamScores[1] >= nm.roundLimit || nm.teamScores[2] >= nm.roundLimit;
            if (!finished) BroadcastRoundStart(currentRoundNumber + 1);
        }
    } else if (nm.currentGameMode==OnlineGameMode::DEATHMATCH) {
        finished=std::max(nm.teamScores[1],nm.teamScores[2])>=nm.roundLimit || (nm.timeLimit>0 && elapsed>=nm.timeLimit);
    } else {
        int topScore = nm.currentGameMode == OnlineGameMode::TEAM_DEATHMATCH
            ? std::max(nm.teamScores[1], nm.teamScores[2]) : nm.localKills;
        if (nm.currentGameMode == OnlineGameMode::FREE_FOR_ALL) for (auto* rp : remotePlayers) topScore = std::max(topScore, rp->GetKills());
        finished = Rules::LimitReached(nm.timeLimit, nm.killLimit, elapsed, topScore);
    }
    // A team with no connected players forfeits; FFA ends when only one participant remains.
    if (nm.currentGameMode == OnlineGameMode::FREE_FOR_ALL) finished = finished || nm.GetPlayers().size() < 2;
    else {
        int a = 0, b = 0;
        for (const auto& p : nm.GetPlayers()) { if (p.teamID == 1) ++a; if (p.teamID == 2) ++b; }
        if (!a || !b) { finished = true; matchWinner = a ? 1 : b ? 2 : -1; }
    }
    if (finished) {
        if (matchWinner < 0 && nm.currentGameMode != OnlineGameMode::FREE_FOR_ALL)
            matchWinner = nm.teamScores[1] > nm.teamScores[2] ? 1 : nm.teamScores[2] > nm.teamScores[1] ? 2 : -1;
        if (nm.currentGameMode == OnlineGameMode::FREE_FOR_ALL) {
            int best = nm.localKills; matchWinner = (int)nm.GetLocalPlayerID(); bool tied = false;
            for (auto* rp : remotePlayers) {
                if (rp->GetKills() > best) { best = rp->GetKills(); matchWinner = (int)rp->peerID; tied = false; }
                else if (rp->GetKills() == best) tied = true;
            }
            if (tied) matchWinner = -1;
        }
        matchEndTimer = 6; isSpectating = true; roundBannerTimer = 0;
        nm.roundPhase = NetworkManager::RoundPhase::MATCH_OVER;
    }
    stateTimer += dt;
    if (finished || stateTimer >= 0.5f) {
        stateTimer = 0;
        PacketMatchState p{}; p.header = {PacketType::MATCH_STATE, 0};
        p.elapsed = elapsed; p.grScore = nm.teamScores[1]; p.blScore = nm.teamScores[2]; p.finished = finished; p.winner = matchWinner;
        nm.SendPacket(&p, sizeof(p), true);
    }
}
