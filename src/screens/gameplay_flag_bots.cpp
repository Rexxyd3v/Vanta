#include "main_level.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <limits>

namespace {
constexpr float Cell=72;
float Distance(Vector2 a,Vector2 b){return std::hypot(a.x-b.x,a.y-b.y);}
}
void GameplayScreen::PlanFlagBots(){
    // Assign once from the frame's team snapshot; followers never all claim the flag.
    for(int team=1;team<=2;++team){
        int opponent=3-team;const auto& own=flags[team];const auto& hostile=flags[opponent];
        Vector2 home=teamBasePositions[team],goal=teamBasePositions[opponent];float forward=team==1?1.0f:-1.0f;
        std::vector<BotEnemy*> available;
        for(auto& b:bots)if(!b->IsDead() && b->GetTeamID()==team)available.push_back(b.get());
        int total=(int)available.size();
        auto assign=[&](BotEnemy* bot,FlagBotRole role,Vector2 destination){
            auto& plan=flagBotPlans[bot];if(plan.role!=role){plan.route.clear();plan.repath=0;}
            plan.role=role;plan.destination={std::clamp(destination.x,170.0f,worldWidth-170),std::clamp(destination.y,170.0f,worldHeight-170)};
            available.erase(std::remove(available.begin(),available.end(),bot),available.end());
        };
        auto closest=[&](Vector2 target)->BotEnemy*{BotEnemy* best=nullptr;float distance=1e20f;
            for(auto* bot:available){float d=Distance(bot->GetPosition(),target);if(d<distance){distance=d;best=bot;}}return best;};
        BotEnemy* runner=nullptr;
        for(auto* bot:available)if(own.carrier==bot){runner=bot;break;}
        if(runner)assign(runner,FlagBotRole::Runner,goal);
        // With a tiny squad, a flag at the home gate takes priority over fetching ours.
        bool emergency=hostile.carrier && !hostile.carrier->IsDead() && Distance(hostile.carrier->GetPosition(),home)<650;
        if(emergency && !available.empty()){
            auto* interceptor=closest(hostile.carrier->GetPosition());
            assign(interceptor,FlagBotRole::Interceptor,hostile.carrier->GetPosition());
        }
        if(!own.carrier && own.respawnTimer<=0 && !available.empty()){
            auto found=std::find(available.begin(),available.end(),flagRunners[team]);
            runner=found!=available.end()?*found:closest(own.position);
            assign(runner,FlagBotRole::Runner,own.position);
        }
        flagRunners[team]=runner;
        if(!emergency && (hostile.carrier || hostile.dropped) && !available.empty()){
            Vector2 target=hostile.carrier?hostile.carrier->GetPosition():hostile.position;
            if(hostile.carrier){Vector2 delta{home.x-target.x,home.y-target.y};float d=std::max(1.0f,std::hypot(delta.x,delta.y));target.x+=delta.x/d*100;target.y+=delta.y/d*100;}
            assign(closest(target),FlagBotRole::Interceptor,target);
        }
        if(total>=3 && available.size()>=2){
            auto* defender=closest(home);Vector2 station{home.x+forward*180,home.y+(team==1?-190.0f:190.0f)};
            Character* intruder=nullptr;float nearest=650;
            auto watch=[&](Character* enemy){if(!enemy || enemy->IsDead() || enemy->GetTeamID()==team)return;
                float d=Distance(enemy->GetPosition(),home);if(d<nearest){nearest=d;intruder=enemy;}};
            watch(player);for(auto& b:bots)watch(b.get());
            if(intruder){Vector2 p=intruder->GetPosition(),delta{home.x-p.x,home.y-p.y};float d=std::max(1.0f,std::hypot(delta.x,delta.y));station={p.x+delta.x/d*180,p.y+delta.y/d*180};}
            assign(defender,FlagBotRole::Defender,station);
        }
        auto remaining=available;
        for(size_t i=0;i<remaining.size();++i){auto* bot=remaining[i];float side=i%2?-1.0f:1.0f;float rank=(float)(i/2);
            if(own.carrier && !own.carrier->IsDead()){
                // Screen both sides of the route, rather than standing on the carrier.
                Vector2 p=own.carrier->GetPosition();float lead=i<2?170.0f:-140.0f;
                assign(bot,FlagBotRole::Escort,{p.x+forward*lead,p.y+side*(175+rank*95)});
            }else{
                float progress=(bot->GetPosition().x-home.x)*forward;
                float span=std::abs(goal.x-home.x);float x=progress>span*.38f?goal.x-forward*(280+rank*130):home.x+forward*span*.52f;
                assign(bot,FlagBotRole::Flanker,{x,worldHeight*(side>0?.72f:.27f)+rank*65});
            }
        }
    }
}
bool GameplayScreen::FlagRouteClear(Vector2 a,Vector2 b)const{
    if(!ArenaClear(b,4))return false;
    // Sweep the entire ground footprint. Sampling can miss a shallow corner
    // and approve a shortcut that the character's next movement cannot take.
    for(const auto& group:allObjectGroups)for(uint32_t i=0;i<group.objectsLength;++i){const auto& o=group.objects[i];
        Rectangle r=o.type==OBJECT_TYPE_RECTANGLE?Rectangle{(float)o.x,(float)o.y,(float)o.width,(float)o.height}:o.aabb;
        r={r.x-12,r.y-player->GetFeetOffset()-6,r.width+24,r.height+16};
        if(CheckCollisionPointRec(a,r)){if(CheckCollisionPointRec(b,r))return false;continue;}
        Vector2 corners[]={{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}},hit{};
        for(int k=0;k<4;++k)if(CheckCollisionLines(a,b,corners[k],corners[(k+1)%4],&hit))return false;
    }
    return true;
}
Vector2 GameplayScreen::FlagBotWaypoint(BotEnemy* bot,float dt){
    auto& plan=flagBotPlans[bot];Vector2 from=bot->GetPosition(),target=plan.destination;
    // Share a grid in ground-foot coordinates, independent of sprite padding.
    float feetDelta=bot->GetFeetOffset()-player->GetFeetOffset();from.y+=feetDelta;target.y+=feetDelta;
    auto body=[&](Vector2 p){p.y-=feetDelta;return p;};
    plan.repath-=dt;
    if(FlagRouteClear(from,target)){plan.route.clear();return body(target);}
    if(flagWalkGrid.empty()){
        flagGridWidth=(int)std::ceil(worldWidth/Cell);flagGridHeight=(int)std::ceil(worldHeight/Cell);
        flagWalkGrid.resize(flagGridWidth*flagGridHeight);
        for(int y=0;y<flagGridHeight;++y)for(int x=0;x<flagGridWidth;++x)
            flagWalkGrid[y*flagGridWidth+x]=ArenaClear({(x+.5f)*Cell,(y+.5f)*Cell},4);
        flagGridLinks.resize(flagWalkGrid.size());
        for(int y=0;y<flagGridHeight;++y)for(int x=0;x<flagGridWidth;++x){int index=y*flagGridWidth+x,bit=0;if(!flagWalkGrid[index])continue;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx && !dy)continue;int mask=1<<bit++;int nx=x+dx,ny=y+dy;
                if(nx<0 || ny<0 || nx>=flagGridWidth || ny>=flagGridHeight || !flagWalkGrid[ny*flagGridWidth+nx])continue;
                if(dx && dy && (!flagWalkGrid[y*flagGridWidth+nx] || !flagWalkGrid[ny*flagGridWidth+x]))continue;
                if(FlagRouteClear({(x+.5f)*Cell,(y+.5f)*Cell},{(nx+.5f)*Cell,(ny+.5f)*Cell}))flagGridLinks[index]|=mask;
            }
        }
    }
    auto point=[&](int index){return Vector2{(index%flagGridWidth+.5f)*Cell,(index/flagGridWidth+.5f)*Cell};};
    if((plan.repath<=0 && plan.route.empty()) || Distance(target,plan.routeTarget)>120){
        plan.repath=1.0f;plan.routeTarget=target;plan.route.clear();
        auto nearest=[&](Vector2 p,bool connect){int best=-1;float distance=1e20f;
            for(int i=0;i<(int)flagWalkGrid.size();++i)if(flagWalkGrid[i]){float d=Distance(p,point(i));
                if(d<distance && (!connect || (d<320 && FlagRouteClear(p,point(i))))){distance=d;best=i;}}
            return best;};
        int start=nearest(from,true),finish=nearest(target,false);
        if(start>=0 && finish>=0){
            std::vector<float> cost(flagWalkGrid.size(),std::numeric_limits<float>::infinity());std::vector<int> parent(flagWalkGrid.size(),-1);
            using Entry=std::pair<float,int>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
            cost[start]=0;open.push({Distance(point(start),point(finish)),start});
            while(!open.empty()){
                auto [priority,index]=open.top();open.pop();if(index==finish)break;
                if(priority>cost[index]+Distance(point(index),point(finish))+.1f)continue;
                int x=index%flagGridWidth,y=index/flagGridWidth,bit=0;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx && !dy)continue;
                    int mask=1<<bit++;if(!(flagGridLinks[index]&mask))continue;
                    int next=(y+dy)*flagGridWidth+x+dx;
                    float nextCost=cost[index]+Cell*(dx && dy?1.414214f:1);
                    if(nextCost>=cost[next])continue;
                    cost[next]=nextCost;parent[next]=index;open.push({nextCost+Distance(point(next),point(finish)),next});
                }
            }
            if(start==finish || parent[finish]>=0){for(int i=finish;i>=0;i=parent[i]){plan.route.push_back(point(i));if(i==start)break;}std::reverse(plan.route.begin(),plan.route.end());}
        }
    }
    while(!plan.route.empty() && Distance(from,plan.route.front())<40)plan.route.erase(plan.route.begin());
    if(!plan.route.empty()){
        // Smooth only across walkable segments; players still traverse every distance.
        size_t furthest=0;for(size_t i=1;i<plan.route.size();++i){if(!FlagRouteClear(from,plan.route[i]))break;furthest=i;}
        if(furthest>0)plan.route.erase(plan.route.begin(),plan.route.begin()+furthest);
        return body(plan.route.front());
    }
    return body(from);
}
Vector2 GameplayScreen::FlagBotSpacing(BotEnemy* bot)const{
    Vector2 push{},p=bot->GetPosition();
    auto avoid=[&](Character* ally,size_t index){if(ally==bot || ally->IsDead() || ally->GetTeamID()!=bot->GetTeamID())return;
        Vector2 a=ally->GetPosition();float dx=p.x-a.x,dy=p.y-a.y,d=std::hypot(dx,dy);
        if(d<100){if(d<1){float angle=(index+1)*2.39996f;dx=std::cos(angle);dy=std::sin(angle);d=1;}
            float strength=(100-d)/100;push.x+=dx/d*strength;push.y+=dy/d*strength;}};
    avoid(player,0);for(size_t i=0;i<bots.size();++i)avoid(bots[i].get(),i+1);
    float length=std::hypot(push.x,push.y);if(length>1){push.x/=length;push.y/=length;}return push;
}
