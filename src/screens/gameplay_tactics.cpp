#include "main_level.h"
#include "../rules/grenade_rules.h"
#include <cmath>
#include <algorithm>

bool GameplayScreen::BotSight(Vector2 a,Vector2 b)const{
    auto crosses=[&](Vector2 c,Vector2 d){Vector2 hit{};return CheckCollisionLines(a,b,c,d,&hit);};
    for(const auto& group:allObjectGroups)for(uint32_t i=0;i<group.objectsLength;++i){const auto& o=group.objects[i];
        if((o.type==OBJECT_TYPE_POLYGON || o.type==OBJECT_TYPE_POLYLINE) && o.pointsLength>2){
            for(uint32_t k=o.type==OBJECT_TYPE_POLYGON?1:0;k+1<o.pointsLength;++k)
                if(crosses({(float)o.x+o.points[k].x,(float)o.y+o.points[k].y},{(float)o.x+o.points[k+1].x,(float)o.y+o.points[k+1].y}))return false;
        }else{Rectangle r=o.type==OBJECT_TYPE_RECTANGLE?Rectangle{(float)o.x,(float)o.y,(float)o.width,(float)o.height}:o.aabb;
            if(CheckCollisionPointRec(a,r) || CheckCollisionPointRec(b,r))return false;
            Vector2 corners[]={{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}};
            for(int k=0;k<4;++k)if(crosses(corners[k],corners[(k+1)%4]))return false;
        }
    }return true;
}
void GameplayScreen::PrepareBotTactics(BotEnemy* bot){
    std::vector<BotEnemy::Hazard> zones;
    for(const auto& g:grenades){
        if(g.kind==0 && g.age<GrenadeRules::Fuse)zones.push_back({g.target,GrenadeRules::BlastRadius});
        if(g.kind==1 && g.age<GrenadeRules::Fuse+GrenadeRules::SmokeDuration &&
           GrenadeRules::PoisonAffects(false,HasTeamBases(),g.ownerTeam,bot->GetTeamID()))
            zones.push_back({g.target,GrenadeRules::SmokeRadius(std::max(0.0f,g.age-GrenadeRules::Fuse)+.65f)});
    }
    for(const auto& b:bonusBlasts)if(b.kind==5 && b.age<5)zones.push_back({b.position,175});
    for(const auto& b:bonusProjectiles)if(b.kind==1 || b.kind==2){
        Vector2 projected{b.position.x+b.velocity.x*.2f,b.position.y+b.velocity.y*.2f};
        if(std::hypot(projected.x-bot->GetPosition().x,projected.y-bot->GetPosition().y)<500)
            zones.push_back({projected,b.kind==1?175.0f:230.0f});
    }
    bot->SetTacticalContext(std::move(zones),[this,bot](Vector2 p){p.y+=bot->GetFeetOffset()-player->GetFeetOffset();return ArenaClear(p,3);},
                           [this](Vector2 a,Vector2 b){return BotSight(a,b);});
}
