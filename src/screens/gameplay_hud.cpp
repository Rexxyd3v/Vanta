#include "main_level.h"
#include "../network/network_manager.h"
#include "../ui/game_settings.h"
#include "../ui/ui_theme.h"
#include "../ui/ui_widgets.h"
#include "../rules/match_rules.h"
#include "../rules/flag_rules.h"
#include "../ui/weapon_skin_catalog.h"
#include <algorithm>
#include <cmath>

void GameplayScreen::DrawHud() {
    auto& nm = NetworkManager::GetInstance();
    const bool practice = currentMode == GameMode::PRACTICE;
    const bool elimination = practice ? Offline::RoundMode(offlineSetup.mode) : nm.currentGameMode == OnlineGameMode::ELIMINATION;
    const bool flagRace = IsFlagDeathmatch();
    const bool offlineTeams = practice && Offline::TeamMode(offlineSetup.mode);
    const Color gold{244, 204, 128, 255}, cyan{112, 223, 241, 255}, green{163, 245, 87, 255};
    const Color white{240, 245, 249, 255}, muted{176, 194, 207, 255};
    const Color ink{6, 15, 24, 255};
    // Edge fades keep floating graphics readable on bright maps without panel outlines.
    DrawRectangleGradientV(0, 0, VIRTUAL_WIDTH, 154, Fade(ink, 0.83f), Fade(ink, 0));
    DrawRectangleGradientV(0, 504, VIRTUAL_WIDTH, 176, Fade(ink, 0), Fade(ink, 0.92f));
    auto text = [&](const char* value, int x, int y, int size, Color color) {
        DrawText(value, x+1, y+2, size, Fade(ink, 0.85f));
        DrawText(value, x, y, size, color);
    };
    auto centered = [&](const char* value, int x, int y, int size, Color color) {
        text(value, x-MeasureText(value,size)/2, y, size, color);
    };
    auto right = [&](const char* value, int x, int y, int size, Color color) {
        text(value, x-MeasureText(value,size), y, size, color);
    };

    text("V A N T A", 38, 28, 16, gold);
    text(player->GetName().substr(0,20).c_str(), 38, 54, 20, white);
    text(TextFormat("%d KILLS   /   %d DEATHS", practice ? player->GetKills() : nm.localKills,
        practice ? player->GetDeaths() : nm.localDeaths), 38, 83, 12, muted);

    // A floating clock with score wings is shared by every game mode.
    const char* mode = practice ? Offline::Name(offlineSetup.mode) : elimination ? "E L I M I N A T I O N" :
        flagRace ? "DEATHMATCH / FLAG" : nm.currentGameMode == OnlineGameMode::FREE_FOR_ALL ? "F R E E  F O R  A L L" : "T E A M  D E A T H M A T C H";
    centered(mode, 650, 22, 12, gold);
    int seconds = (int)std::max(0.0f, elimination ? Rules::RoundSeconds-roundElapsed :
        practice && offlineSetup.mode!=Offline::Mode::Training ? offlineSetup.timeLimit-elapsed :
        !practice && nm.timeLimit>0 ? nm.timeLimit-elapsed : elapsed);
    centered(TextFormat("%02d:%02d",seconds/60,seconds%60),650,43,36,elimination && seconds<20 ? ORANGE : white);
    if (offlineTeams || (!practice && nm.currentGameMode != OnlineGameMode::FREE_FOR_ALL)) {
        right(TextFormat("%02d",practice?offlineScores[1]:nm.teamScores[1]), 558, 45, 32, UiTheme::TeamBlue());
        text(TextFormat("%02d",practice?offlineScores[2]:nm.teamScores[2]), 742, 45, 32, UiTheme::TeamRed());
        right("GLOBAL RISK", 558, 82, 10, muted);
        text("BLACK LIST", 742, 82, 10, muted);
        DrawLineEx({575,48},{575,77},1,Fade(white,0.25f));
        DrawLineEx({725,48},{725,77},1,Fade(white,0.25f));
    }
    const char* objective = flagRace ? TextFormat("FIRST TO %d CAPTURES / DELIVER YOUR FLAG TO ENEMY BASE",practice?offlineSetup.roundLimit:nm.roundLimit) :
        practice ? offlineSetup.mode==Offline::Mode::Training ? "AIM. MOVE. SURVIVE." :
        elimination ? TextFormat("ROUND %d / FIRST TO %d",currentRoundNumber,offlineSetup.roundLimit) : TextFormat("FIRST TO %d KILLS",offlineSetup.killLimit) :
        elimination ? "ONE LIFE. MAKE IT COUNT." : nm.killLimit>0 ? TextFormat("FIRST TO %d KILLS",nm.killLimit) : "MOST KILLS WINS";
    centered(objective,650,100,12,muted);
    DrawKillFeed();

    if (flagRace && matchEndTimer<=0) {
        for (int team=1;team<=2;++team) {
            const auto& flag=flags[team];
            std::string status=std::string(team==1?"GR FLAG / ":"BL FLAG / ")+
                (flag.respawnTimer>0?"RESETTING":flag.carrier?flag.carrier->GetName():flag.dropped?"DROPPED / RECOVER OR INTERCEPT":"AT HOME BASE");
            centered(status.c_str(),650,126+(team-1)*19,11,team==1?UiTheme::TeamBlue():UiTheme::TeamRed());
        }
        int team=player->GetTeamID();
        if (flags[team].carrier==player) centered(TextFormat("CARRYING YOUR FLAG / ATTACK ENEMY BASE / [%s] DROP",Settings::KeyLabel(Settings::Interact)),650,175,12,gold);
        else if (!player->IsDead() && flags[team].respawnTimer<=0) {
            Character* carrier=flags[team].carrier;
            Vector2 target=carrier?carrier->GetPosition():flags[team].position;
            float distance=std::hypot(target.x-player->GetPosition().x,target.y-player->GetPosition().y);
            const char* hint=carrier?(distance<=FlagRules::HandoffRadius?
                TextFormat("[%s] TAKE FLAG FROM TEAMMATE",Settings::KeyLabel(Settings::Interact)):
                "YOUR TEAMMATE HAS THE FLAG / GET CLOSE TO TAKE IT"):
                "PICK UP YOUR TEAM FLAG / WALK INTO ITS GROUND RING";
            centered(hint,650,175,12,gold);
        }
        if (flagAnnouncementTimer>0) centered(flagAnnouncement.c_str(),650,211,20,flagAnnouncementTeam==1?UiTheme::TeamBlue():UiTheme::TeamRed());
    }
    if (HasTeamBases()) for (int team=1;team<=2;++team) {
        Vector2 location=GetWorldToScreen2D(GroundPoint(teamBasePositions[team]),camera);
        if (location.x>=40 && location.x<=1260 && location.y>=180 && location.y<=480) continue;
        Vector2 marker{std::clamp(location.x,76.0f,1224.0f),std::clamp(location.y,190.0f,480.0f)};
        Color color=team==1?UiTheme::TeamBlue():UiTheme::TeamRed();
        DrawPolyLines(marker,4,12,0,color);
        centered(team==player->GetTeamID()?"YOUR BASE":"ENEMY BASE",(int)marker.x,(int)marker.y+18,10,color);
    }
    if (practice && offlineRoundOver && matchEndTimer<=0) {
        DrawRectangleGradientV(0,250,VIRTUAL_WIDTH,90,Fade(ink,0),Fade(ink,0.65f));
        DrawRectangleGradientV(0,340,VIRTUAL_WIDTH,90,Fade(ink,0.65f),Fade(ink,0));
        centered(lastRoundWinnerID==1?"GLOBAL RISK WINS":lastRoundWinnerID==2?"BLACK LIST WINS":"ROUND DRAW",650,292,28,gold);
        centered(offlineResult.c_str(),650,335,16,white);
        centered(TextFormat("GR %d  /  %d BL   -   NEXT ROUND IN %.0f",offlineScores[1],offlineScores[2],std::ceil(roundBannerTimer)),650,364,14,muted);
    }

    // Vitals sit on a single angled rail rather than a framed player card.
    float hp=std::clamp(player->GetHealth()/100.0f,0.0f,1.0f);
    Color healthColor=hp>0.3f ? Color{111,234,181,255} : Color{255,104,99,255};
    Vector2 portrait{73,605};
    DrawCircleGradient(portrait,43,Fade(ink,0.75f),Fade(ink,0));
    DrawRing(portrait,35,37,0,360,64,Fade(white,0.18f));
    if (hp>0) DrawRing(portrait,35,37,135,135+270*hp,64,healthColor);
    if (headPortrait.id) DrawTexturePro(headPortrait,{0,0,(float)headPortrait.width,(float)headPortrait.height},
        {45,577,56,56},{0,0},0,WHITE);
    text(TextFormat("%03d",(int)std::max(0.0f,player->GetHealth())),124,578,40,white);
    text("HP",212,601,12,healthColor);
    for (int i=0;i<10;++i) {
        float x=126+i*20.0f, fill=std::clamp(hp*10-i,0.0f,1.0f);
        DrawLineEx({x,632},{x+15,628},5,Fade(white,0.16f));
        if (fill>0) DrawLineEx({x,632},{x+15*fill,632-4*fill},5,healthColor);
    }
    text(hp<=0.3f && !player->IsDead() ? "LOW HEALTH" : practice && offlineSetup.mode==Offline::Mode::Training ? "TRAINING OPERATOR" :
        player->GetTeamID()==1 ? "GLOBAL RISK" : player->GetTeamID()==2 ? "BLACK LIST" : "SOLO OPERATOR",126,650,10,healthColor);

    if (poisonExposure>0 && !player->IsDead()) {
        text("POISONED / LEAVE THE SMOKE",126,552,12,green);
        if (Settings::Get().effects) {
            DrawRectangleGradientH(0,0,70,VIRTUAL_HEIGHT,Fade(green,poisonExposure*0.15f),Fade(green,0));
            DrawRectangleGradientH(VIRTUAL_WIDTH-70,0,70,VIRTUAL_HEIGHT,Fade(green,0),Fade(green,poisonExposure*0.15f));
        }
    }

    // Airy circular skill controls; recharge is an arc, with seconds inside it.
    auto skill=[&](int x,const char* label,Settings::Action action,float cooldown,float duration,bool active,Color color,int icon) {
        Vector2 center{(float)x,596};
        const bool ready=cooldown<=0 && !player->IsDead();
        Color tint=player->IsDead() ? muted : ready || active ? color : Fade(color,0.65f);
        float progress=std::clamp(1-cooldown/duration,0.0f,1.0f);
        DrawCircleGradient(center,34,Fade(ink,0.62f),Fade(ink,0));
        DrawRing(center,26,28,0,360,64,Fade(white,0.18f));
        if (progress>0 && !player->IsDead()) DrawRing(center,26,28,-90,-90+360*progress,64,tint);
        if (active) DrawRing(center,31,32,0,360,64,Fade(color,0.5f));
        centered(Settings::KeyLabel(action),x,547,12,white);
        if (!ready && !active) {
            centered(player->IsDead() ? "-" : TextFormat("%.1f",cooldown),x,588,18,white);
        } else if (icon==0) {
            for (int i=0;i<2;++i) {
                float bx=center.x-13+i*12;
                DrawLineEx({bx,center.y-9},{bx+9,center.y},2.5f,tint);
                DrawLineEx({bx+9,center.y},{bx,center.y+9},2.5f,tint);
            }
        } else if (icon==1) {
            Vector2 points[]={{center.x-10,center.y-10},{center.x,center.y-14},{center.x+10,center.y-10},
                {center.x+8,center.y+5},{center.x,center.y+13},{center.x-8,center.y+5},{center.x-10,center.y-10}};
            DrawLineStrip(points,7,tint);
            DrawLineEx({center.x,center.y-7},{center.x,center.y+6},2,tint);
        } else {
            DrawCircleLines(x,596,9,tint);
            DrawLineEx({center.x-5,center.y-13},{center.x+4,center.y-13},3,tint);
            DrawLineEx({center.x+4,center.y-13},{center.x+8,center.y-6},2,tint);
            if (icon==3) { DrawCircleLines(x+10,580,4,tint); DrawCircleLines(x+5,573,3,Fade(tint,0.6f)); }
            else { DrawLineEx({center.x-4,center.y-4},{center.x+4,center.y+4},2,tint); }
        }
        centered(label,x,635,12,active ? color : muted);
        if (active) centered("ACTIVE",x,652,10,color);
        else if (ready) DrawCircle(x,656,2,color);
    };
    skill(506,"DASH",Settings::Dash,player->GetDashCooldown(),2,player->GetDashTimer()>0,cyan,0);
    skill(602,"SHIELD",Settings::Shield,player->GetShieldCooldown(),6,player->GetShieldTimer()>0,{155,177,255,255},1);
    skill(698,"GRENADE",Settings::Grenade,grenadeCooldown[0],8,false,green,2);
    skill(794,"SMOKE",Settings::Smoke,grenadeCooldown[1],12,false,green,3);

    // Ammo uses typography and a thin progress arc, without stacked weapon boxes.

    int weapon=std::clamp(player->GetWeaponIndex(),0,WeaponCount-1);
    right(WeaponName(weapon),1260,552,18,gold);
    right(TextFormat("%02d",player->GetAmmo()),1186,579,52,player->GetAmmo()<=3 ? ORANGE : white);
    text(TextFormat("/ %02d",player->GetMaxAmmo()),1202,605,20,muted);
    float ammoProgress=player->IsReloading() ? player->GetReloadProgress() : (float)player->GetAmmo()/player->GetMaxAmmo();
    DrawLineEx({1090,639},{1260,639},2,Fade(white,0.15f));
    DrawLineEx({1090,639},{1090+170*std::clamp(ammoProgress,0.0f,1.0f),639},2,player->IsReloading() ? cyan : gold);
    right(player->IsReloading() ? TextFormat("RELOADING %02d%%",(int)(100*player->GetReloadProgress())) :
        TextFormat("[%s] RELOAD",Settings::KeyLabel(Settings::Reload)),1260,650,10,player->IsReloading() ? cyan : muted);
    for (int i=0;i<WeaponCount;++i) {
        int x=902+i*72;
        text(TextFormat("%d %s",i+1,WeaponShortName(i)),x,521,9,i==weapon ? gold : Fade(muted,0.65f));
        if (i==weapon) DrawLineEx({(float)x,539},{(float)x+20,539},2,gold);
    }
    centered("ESC  MENU     F10  SETTINGS     TAB  SCORES",650,672,8,Fade(muted,0.7f));

    if (!player->IsDead() && !isPaused && !Settings::BlocksInput()) {
        Vector2 m=Ui::RemapMouseToVirtual();
        if (player->IsReloading()) {
            float progress=std::clamp(player->GetReloadProgress(),0.0f,1.0f);
            DrawRing(m,23,26,0,360,48,Fade(ink,0.65f));
            if (progress>0) DrawRing(m,23,26,-90,-90+360*progress,64,cyan);
            centered(TextFormat("RELOAD %02d%%",(int)(100*progress)),
                (int)std::clamp(m.x,72.0f,1228.0f),(int)std::clamp(m.y+34,8.0f,650.0f),12,cyan);
        } else if (player->GetAmmo()<=3) centered(TextFormat("[%s] LOW AMMO",Settings::KeyLabel(Settings::Reload)),
            (int)std::clamp(m.x,80.0f,1220.0f),(int)std::clamp(m.y+30,8.0f,650.0f),14,ORANGE);
    }
    if (player->IsDead() && matchEndTimer<=0 && !offlineRoundOver) {
        DrawRectangleGradientV(0,258,VIRTUAL_WIDTH,80,Fade(ink,0),Fade(ink,0.55f));
        DrawRectangleGradientV(0,338,VIRTUAL_WIDTH,80,Fade(ink,0.55f),Fade(ink,0));
        centered("ELIMINATED",650,300,36,gold);
        bool automatic=!practice || offlineSetup.mode!=Offline::Mode::Training;
        const char* hint=elimination ? "Waiting for the next round" : automatic ? TextFormat("Respawning at your base in %.0f",std::ceil(std::max(0.0f,Rules::RespawnDelay-deadFor))) : deadFor<Rules::RespawnDelay ?
            TextFormat("Respawn available in %.0f",std::ceil(Rules::RespawnDelay-deadFor)) : "Press ENTER to respawn";
        centered(hint,650,352,18,white);
    }
    if (damageFlash>0 && Settings::Get().effects) {
        DrawRectangleLinesEx({3,3,VIRTUAL_WIDTH-6.0f,VIRTUAL_HEIGHT-6.0f},6,Fade(RED,damageFlash*0.6f));
        Vector2 p=player->GetPosition(); float angle=std::atan2(damageSource.y-p.y,damageSource.x-p.x);
        DrawCircleV({VIRTUAL_WIDTH/2.0f+std::cos(angle)*90,VIRTUAL_HEIGHT/2.0f+std::sin(angle)*90},7,Fade(RED,damageFlash*2));
    }
}

void GameplayScreen::DrawKillFeed() {
    auto shortened=[](std::string value,int width) {
        for(auto& c:value) if((unsigned char)c<32) c=' ';
        if(MeasureText(value.c_str(),14)<=width) return value;
        while(!value.empty() && MeasureText((value+"...").c_str(),14)>width) value.pop_back();
        return value+"...";
    };
    for(size_t i=0;i<killFeed.size();++i) {
        const auto& entry=killFeed[i];
        float opacity=std::clamp(entry.remaining/1.2f,0.0f,1.0f);
        float enter=std::clamp((7-entry.remaining)/.18f,0.0f,1.0f);
        opacity*=enter;
        int shift=(int)((1-enter)*10),y=35+(int)i*32;
        Color killer=entry.localKill?Color{244,204,128,255}:entry.killerTeam==1?UiTheme::TeamBlue():entry.killerTeam==2?UiTheme::TeamRed():Color{238,244,248,255};
        Color victim=entry.localDeath?Color{255,115,111,255}:entry.victimTeam==1?UiTheme::TeamBlue():entry.victimTeam==2?UiTheme::TeamRed():Color{196,209,219,255};
        DrawRectangleGradientH(950+shift,y-4,314,26,Fade(Color{6,15,24,255},opacity*.08f),Fade(Color{6,15,24,255},opacity*.58f));
        auto label=[&](const std::string& value,int x,Color color) {
            DrawText(value.c_str(),x+shift+1,y+2,14,Fade(BLACK,opacity*.85f));
            DrawText(value.c_str(),x+shift,y+1,14,Fade(color,opacity));
        };
        std::string attacker=shortened(entry.killer,110),target=shortened(entry.victim,108);
        label(attacker,1072-MeasureText(attacker.c_str(),14),killer);
        label(target,1152,victim);
        if(entry.weapon<WeaponCount) {
            Texture2D texture=LoadWeaponSlotTexture(entry.weapon,entry.skin);
            if(texture.id) {
                Rectangle source=GetWeaponGeometry(texture,entry.weapon).bounds;
                if(source.width>0 && source.height>0) {
                    float fit=std::min(62/source.width,21/source.height);
                    Rectangle destination{1112.0f+shift,y+10.0f,source.width*fit,source.height*fit};
                    DrawWeaponSkinTexture(texture,source,destination,{destination.width/2,destination.height/2},0,entry.weapon,entry.skin,opacity);
                }
            }
        } else {
            Color color=entry.weapon==WeaponCount+1?Color{154,248,67,255}:Color{233,192,107,255};
            Vector2 center{1112.0f+shift,y+11.0f};
            DrawCircleV(center,6,Fade(color,opacity*.28f));
            DrawCircleLinesV(center,6,Fade(color,opacity));
            DrawLineEx({center.x-2,center.y-7},{center.x+1,center.y-10},2,Fade(color,opacity));
            DrawLineEx({center.x+1,center.y-10},{center.x+6,center.y-8},1,Fade(color,opacity));
            if(entry.weapon==WeaponCount+1) {
                DrawCircleV({center.x-12,center.y+1},2,Fade(color,opacity*.55f));
                DrawCircleV({center.x+12,center.y-3},3,Fade(color,opacity*.35f));
            }
        }
        if(entry.localKill || entry.localDeath) DrawLineEx({1265.0f+shift,y+1.0f},{1265.0f+shift,y+17.0f},2,Fade(entry.localDeath?victim:killer,opacity));
    }
}
