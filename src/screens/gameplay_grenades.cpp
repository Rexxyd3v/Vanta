#include "main_level.h"
#include "../network/network_manager.h"
#include "../ui/game_settings.h"
#include "../rules/match_rules.h"
#include "../rules/grenade_rules.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr Color PoisonGreen{154, 248, 67, 255};
float Distance(Vector2 a, Vector2 b) {
    float dx=a.x-b.x, dy=a.y-b.y; return std::sqrt(dx*dx+dy*dy);
}
Vector2 Offset(Vector2 p, float angle, float radius, float flatten=1) {
    return {p.x+std::cos(angle)*radius, p.y+std::sin(angle)*radius*flatten};
}
float Hash(int i) { return std::fmod(std::abs(std::sin(i*127.1f+311.7f)*43758.5453f),1.0f); }
}

bool GameplayScreen::ThrowGrenade(int kind) {
    if (kind < 0 || kind > 1 || player->IsDead() || isSpectating || matchEndTimer > 0 ||
        isPaused || Settings::BlocksInput() || grenadeCooldown[kind] > 0 || grenades.size() >= 32) return false;
    auto& nm = NetworkManager::GetInstance();
    Vector2 start = player->GetPosition(), target = player->GetAimTarget();
    float dx = target.x-start.x, dy = target.y-start.y, length = std::sqrt(dx*dx+dy*dy);
    if (length > 650) { target.x = start.x+dx/length*650; target.y = start.y+dy/length*650; }
    target.x = std::clamp(target.x, 0.0f, worldWidth); target.y = std::clamp(target.y, 0.0f, worldHeight);
    grenades.push_back({start, target, 0, kind, true, nm.GetLocalPlayerID(), player->GetTeamID()});
    grenadeCooldown[kind] = kind == 0 ? 8.0f : 12.0f;
    if (currentMode == GameMode::ONLINE) {
        PacketPlayerGrenade p{}; p.header = {PacketType::PLAYER_GRENADE, nm.GetLocalPlayerID()};
        p.start = start; p.target = target; p.kind = (uint8_t)kind; nm.SendPacket(&p, sizeof(p), true);
    }
    return true;
}

void GameplayScreen::ApplyPoisonDamage(const GrenadeEffect& g, Character* victim) {
    if (victim->IsDead()) return;
    auto& nm = NetworkManager::GetInstance();
    bool localVictim = victim == player;
    if (currentMode == GameMode::ONLINE && !GrenadeRules::PoisonAffects(g.ownerID==nm.GetLocalPlayerID(),
        nm.currentGameMode!=OnlineGameMode::FREE_FOR_ALL,g.ownerTeam,victim->GetTeamID())) return;
    if (currentMode == GameMode::PRACTICE && victim != player && !OfflineEnemies(player,victim)) return;
    float before = victim->GetHealth();
    victim->TakeDamage(GrenadeRules::PoisonDamage);
    if (victim->GetHealth() >= before) return;
    if (localVictim) { poisonExposure = 0.8f; damageSource = g.target; }
    else if (g.local) RegisterHit(victim->GetPosition(),before-victim->GetHealth());
    if (!victim->IsDead()) return;
    if (currentMode == GameMode::PRACTICE) {
        RecordOfflineKill(player,victim);
    } else if (localVictim) {
        // Each receiver applies its own exposure exactly once; the death event shares attribution.
        ProcessKill(g.ownerID,nm.GetLocalPlayerID());
        PacketPlayerKilled p{}; p.header={PacketType::PLAYER_KILLED,nm.GetLocalPlayerID()};
        p.killerPlayerID=g.ownerID; p.victimPlayerID=nm.GetLocalPlayerID();
        nm.SendPacket(&p,sizeof(p),true);
        if (nm.currentGameMode==OnlineGameMode::ELIMINATION) isSpectating=true;
    }
}

void GameplayScreen::UpdateGrenades(float dt, bool allowInput) {
    auto& nm = NetworkManager::GetInstance();
    explosionShake=std::max(0.0f,explosionShake-dt*26);
    poisonExposure=std::max(0.0f,poisonExposure-dt*2);
    for (int i = 0; i < 2; ++i) {
        grenadeCooldown[i] = std::max(0.0f, grenadeCooldown[i] - dt);
        if (allowInput && Settings::Pressed(i == 0 ? Settings::Grenade : Settings::Smoke)) ThrowGrenade(i);
    }
    for (auto& g : grenades) {
        float before = g.age; g.age += dt;
        if (g.kind==1) {
            if (matchEndTimer>0 || (currentMode==GameMode::ONLINE && isSpectating) || (currentMode==GameMode::PRACTICE && offlineRoundOver)) continue;
            float active=g.age-GrenadeRules::Fuse;
            if (active>=0 && active<GrenadeRules::SmokeDuration && !player->IsDead() &&
                Distance(player->GetPosition(),g.target)<=GrenadeRules::SmokeRadius(active) &&
                (currentMode!=GameMode::ONLINE || GrenadeRules::PoisonAffects(g.ownerID==nm.GetLocalPlayerID(),
                nm.currentGameMode!=OnlineGameMode::FREE_FOR_ALL,g.ownerTeam,player->GetTeamID()))) poisonExposure=0.8f;
            // Fixed exposure ticks are independent of frame rate and stop immediately outside the cloud.
            for (int tick=GrenadeRules::PoisonTicks(before)+1;tick<=GrenadeRules::PoisonTicks(g.age);++tick) {
                float radius=GrenadeRules::SmokeRadius(tick*GrenadeRules::PoisonInterval);
                if (Distance(player->GetPosition(),g.target)<=radius) ApplyPoisonDamage(g,player);
                if (currentMode==GameMode::PRACTICE) for (auto& bot : bots)
                    if (Distance(bot->GetPosition(),g.target)<=radius) ApplyPoisonDamage(g,bot.get());
            }
            continue;
        }
        if (before>=GrenadeRules::Fuse || g.age<GrenadeRules::Fuse) continue;
        // Remote explosions also shake the local camera, with a smooth proximity falloff.
        explosionShake=std::max(explosionShake,GrenadeRules::ShakeStrength(Distance(player->GetPosition(),g.target)));
        if (!g.local || matchEndTimer>0 || (currentMode==GameMode::PRACTICE && offlineRoundOver)) continue;
        auto damage = [&](Character* victim, uint32_t id) {
            if (victim->IsDead() || Distance(victim->GetPosition(),g.target)>GrenadeRules::BlastRadius ||
                (currentMode==GameMode::PRACTICE && !OfflineEnemies(player,victim))) return;
            if (currentMode == GameMode::ONLINE) {
                if (Rules::FriendlyFire(nm.currentGameMode != OnlineGameMode::FREE_FOR_ALL, g.ownerTeam, victim->GetTeamID())) return;
                PacketPlayerDamage d{}; d.header = {PacketType::PLAYER_DAMAGE, g.ownerID};
                d.targetPlayerID = id; d.damageAmount = 40; nm.SendPacket(&d, sizeof(d), true);
                RegisterHit(victim->GetPosition(), 40);
            } else {
                float hp = victim->GetHealth(); victim->TakeDamage(40);
                if (victim->GetHealth() < hp) RegisterHit(victim->GetPosition(), hp-victim->GetHealth());
                if (victim->IsDead()) RecordOfflineKill(player,victim);
            }
        };
        if (currentMode == GameMode::PRACTICE) for (auto& bot : bots) damage(bot.get(), 0);
        else for (auto* remote : remotePlayers) damage(remote, remote->peerID);
    }
    grenades.erase(std::remove_if(grenades.begin(), grenades.end(), [](const GrenadeEffect& g) {
        return g.age > GrenadeRules::Fuse+(g.kind==0 ? GrenadeRules::BlastLifetime : GrenadeRules::SmokeDuration);
    }), grenades.end());
}

void GameplayScreen::DrawGrenadeScars() {
    for (const auto& g : grenades) {
        float age=g.age-GrenadeRules::Fuse;
        if (g.kind!=0 || age<0) continue;
        float opacity=std::clamp((GrenadeRules::BlastLifetime-age)/1.2f,0.0f,1.0f);
        float spread=std::min(1.0f,age/0.14f);
        DrawEllipse((int)g.target.x,(int)g.target.y,100*spread,65*spread,Fade(Color{24,30,19,255},opacity*0.55f));
        for (int branch=0;branch<9;++branch) {
            float angle=branch*6.28318f/9+Hash(branch)*0.35f;
            Vector2 last=g.target;
            for (int segment=1;segment<=4;++segment) {
                float r=segment*(22+Hash(branch+23)*13)*spread;
                Vector2 next=Offset(g.target,angle+(Hash(branch*5+segment)-0.5f)*0.38f,r,0.7f);
                DrawLineEx(last,next,5-segment*0.7f,Fade(Color{14,22,16,255},opacity));
                if (age<0.65f) DrawLineEx({last.x,last.y-1},{next.x,next.y-1},1.2f,Fade(PoisonGreen,opacity*(1-age/0.65f)));
                if (segment==2 || segment==3) {
                    Vector2 fork=Offset(next,angle+(branch%2 ? 0.7f : -0.7f),24*spread,0.7f);
                    DrawLineEx(next,fork,1.5f,Fade(Color{14,22,16,255},opacity));
                }
                last=next;
            }
        }
    }
}

void GameplayScreen::DrawGrenades() {
    const bool effects=Settings::Get().effects;
    for (const auto& g : grenades) {
        float t = std::min(g.age / GrenadeRules::Fuse, 1.0f);
        Vector2 p{g.start.x+(g.target.x-g.start.x)*t, g.start.y+(g.target.y-g.start.y)*t};
        if (t < 1) {
            DrawEllipse((int)p.x, (int)p.y+12, 13, 6, Fade(BLACK, 0.3f));
            p.y -= std::sin(t*3.14159f)*110;
            DrawCircleV(p, 10, {31, 70, 30, 255});
            DrawCircleLines((int)p.x, (int)p.y, 10, PoisonGreen);
            DrawRectangle((int)p.x-5, (int)p.y-14, 10, 5, PoisonGreen);
            DrawLineEx({p.x-4,p.y-3}, {p.x+4,p.y+3}, 3, PoisonGreen);
            DrawCircleLines((int)g.target.x, (int)g.target.y, 18, Fade(PoisonGreen, 0.45f));
        } else if (g.kind == 0) {
            float age=g.age-GrenadeRules::Fuse;
            if (age>1.45f) continue;
            float fade=std::clamp(1-age/1.45f,0.0f,1.0f);
            // Dark pressure cloud, a bright white-hot core, and a fast expanding shockwave.
            DrawCircleGradient(p,65+age*100,Fade(Color{33,41,27,255},fade*0.72f),Fade(BLACK,0));
            if (age<0.48f) {
                float flash=1-age/0.48f;
                DrawCircleGradient(p,50+90*(1-flash),Fade(PoisonGreen,flash*0.95f),Fade(PoisonGreen,0));
                DrawCircleGradient(p,45*flash,Fade(Color{249,255,203,255},flash),Fade(PoisonGreen,0));
            }
            if (effects && age<0.55f) {
                float radius=25+age*430;
                DrawRing(p,radius-4,radius,0,360,96,Fade(Color{220,255,167,255},1-age/0.55f));
            }
            int count=effects ? 34 : 12;
            for (int i=0;i<count;++i) {
                float angle=Hash(i+9)*6.28318f,speed=100+Hash(i+45)*280;
                float drag=1-std::exp(-age*2.8f);
                Vector2 ground=Offset(p,angle,speed*drag,0.7f), shard=ground;
                float lift=std::max(0.0f,(90+Hash(i+3)*130)*age-170*age*age);
                shard.y-=lift;
                DrawEllipse((int)ground.x,(int)ground.y,5*fade,2*fade,Fade(BLACK,fade*0.35f));
                Color color=i%3 ? Color{62,70,48,255} : PoisonGreen;
                if (age<0.65f && effects) DrawLineEx(Offset(shard,angle+3.14159f,16*fade),shard,2,Fade(PoisonGreen,fade));
                DrawPoly(shard,3,4+Hash(i+10)*5,i*37+age*300,Fade(color,fade));
            }
            if (effects) for (int i=0;i<8;++i) {
                Vector2 puff=Offset(p,i*2.39996f,40+age*90,0.6f); puff.y-=age*55;
                DrawCircleGradient(puff,24+age*35,Fade(Color{63,77,47,255},fade*0.38f),Fade(BLACK,0));
            }
        } else {
            float age=g.age-GrenadeRules::Fuse,radius=GrenadeRules::SmokeRadius(age);
            float fade=std::min(1.0f,age*4)*std::min(1.0f,GrenadeRules::SmokeDuration-age);
            DrawCircleGradient(p,radius,Fade(Color{35,76,20,255},fade*0.9f),Fade(PoisonGreen,0));
            // Expanding, rolling lobes and outward-moving wisps make the gas visibly spread.
            int count=effects ? 24 : 12;
            for (int i=0;i<count;++i) {
                float angle=i*2.39996f+std::sin(age*0.45f+i)*0.14f;
                float band=0.18f+0.54f*Hash(i+19);
                float drift=std::fmod(age*0.17f+Hash(i+7),1.0f);
                Vector2 puff=Offset(p,angle,radius*(band+0.12f*std::sin(age+i)));
                puff.y-=8*std::sin(age*0.7f+i);
                float size=radius*(0.22f+0.16f*Hash(i+61))*(0.9f+0.1f*std::sin(age*1.1f+i));
                Color color=i%2 ? Color{84,160,35,255} : Color{145,210,56,255};
                DrawCircleGradient(puff,size,Fade(color,fade*0.72f),Fade(color,0));
                if (effects && i%2==0) {
                    Vector2 wisp=Offset(p,angle,radius*drift);
                    wisp.y-=drift*24;
                    DrawCircleGradient(wisp,12+18*drift,Fade(PoisonGreen,fade*(1-drift)*0.35f),Fade(PoisonGreen,0));
                }
            }
            for (int i=0;i<10;++i) {
                float angle=i*2.39996f+age*0.18f;
                Vector2 mote=Offset(p,angle,radius*(0.45f+Hash(i+5)*0.4f));
                mote.y-=std::fmod(age*17+i*11,35.0f);
                DrawCircleV(mote,1.8f,Fade(PoisonGreen,fade*0.55f));
            }
        }
    }
}
