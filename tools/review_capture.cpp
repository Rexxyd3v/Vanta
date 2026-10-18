#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
// Media Foundation needs these data types even when Windows drawing/UI APIs are excluded.
typedef struct tagMSG MSG,*PMSG,*LPMSG;
typedef struct tagBITMAPINFOHEADER {
    DWORD biSize; LONG biWidth,biHeight; WORD biPlanes,biBitCount;
    DWORD biCompression,biSizeImage; LONG biXPelsPerMeter,biYPelsPerMeter; DWORD biClrUsed,biClrImportant;
} BITMAPINFOHEADER;
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <raylib.h>
#include "screens/main_level.h"
#include "screens/intro_screen.h"
#include "network/network_manager.h"
#include "map_loader/MapRegistry.h"
#include "voice/proximity_voice_chat.h"
#include "ui/character_preview.h"
#include "ui/ui_widgets.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <queue>
#include <fstream>
#include <filesystem>

ProximityVoiceChat proximityVoiceChat;
bool proximityVoiceInitialized=false;
bool (*getLocalPlayerPosCallback)(Vector2&)=nullptr;
constexpr int W=1920,H=1080,FPS=30,FRAMES=2400;
constexpr float PLAYBACK_SPEED=1.12f;
void hr(HRESULT value) { if (FAILED(value)) { std::cerr<<"Media export HRESULT 0x"<<std::hex<<value<<std::endl; throw std::runtime_error("Media export failed"); } }
template<class T> void release(T*& p) { if(p) { p->Release(); p=nullptr; } }
struct Mp4 {
    IMFSinkWriter* writer=nullptr; DWORD video=0,audio=0; Wave music{};
    struct Voice { int weapon; long long start; float gain; };
    Wave shots[6]{}; std::vector<Voice> voices;
    void shot(int weapon,int frame,float gain) { if(frame>=0) voices.push_back({weapon==1 || weapon==6?1:weapon==2 || weapon==4?2:0,(long long)frame*(44100/FPS),gain}); }
    void effect(int kind,int frame,float gain) {if(frame>=0)voices.push_back({kind,(long long)frame*(44100/FPS),gain});}
    Mp4(const wchar_t* path,int width=W,int height=H,int bitrate=12000000) {
        hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); hr(MFStartup(MF_VERSION));
        IMFAttributes* attributes=nullptr; hr(MFCreateAttributes(&attributes,2));
        hr(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE));
        hr(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE));
        hr(MFCreateSinkWriterFromURL(path,nullptr,attributes,&writer)); release(attributes);
        IMFMediaType *output=nullptr,*input=nullptr;
        hr(MFCreateMediaType(&output)); hr(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
        hr(output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264)); hr(output->SetUINT32(MF_MT_AVG_BITRATE,bitrate));
        hr(output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        hr(MFSetAttributeSize(output,MF_MT_FRAME_SIZE,width,height)); hr(MFSetAttributeRatio(output,MF_MT_FRAME_RATE,FPS,1));
        hr(MFSetAttributeRatio(output,MF_MT_PIXEL_ASPECT_RATIO,1,1)); hr(writer->AddStream(output,&video)); release(output);
        hr(MFCreateMediaType(&input)); hr(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
        hr(input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32)); hr(input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        hr(MFSetAttributeSize(input,MF_MT_FRAME_SIZE,width,height)); hr(MFSetAttributeRatio(input,MF_MT_FRAME_RATE,FPS,1));
        hr(MFSetAttributeRatio(input,MF_MT_PIXEL_ASPECT_RATIO,1,1)); hr(input->SetUINT32(MF_MT_DEFAULT_STRIDE,width*4));
        hr(writer->SetInputMediaType(video,input,nullptr)); release(input);
        hr(MFCreateMediaType(&output)); hr(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));
        hr(output->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_AAC)); hr(output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2));
        hr(output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,44100)); hr(output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));
        hr(output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,16000)); hr(writer->AddStream(output,&audio)); release(output);
        hr(MFCreateMediaType(&input)); hr(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));
        hr(input->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM)); hr(input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2));
        hr(input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,44100)); hr(input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));
        hr(input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4)); hr(input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,176400));
        hr(writer->SetInputMediaType(audio,input,nullptr)); release(input); hr(writer->BeginWriting());
        music=LoadWave("assets/sounds/MAINSOUNDTRACK.wav"); WaveFormat(&music,44100,16,2);
        if (!music.data || !music.frameCount) throw std::runtime_error("Missing soundtrack");
        const char* paths[]={"assets/sounds/weapon1.mp3","assets/sounds/weapon3.mp3","assets/sounds/weapon2.mp3"};
        for(int i=0;i<3;++i) {
            shots[i]=LoadWave(paths[i]); WaveFormat(&shots[i],44100,16,2);
            auto* pcm=static_cast<short*>(shots[i].data); unsigned int first=0;
            while(first<shots[i].frameCount && std::abs(pcm[first*2])<150 && std::abs(pcm[first*2+1])<150) ++first;
            if(first>0 && first+1<shots[i].frameCount) WaveCrop(&shots[i],first,shots[i].frameCount-1);
        }
        for(int k=3;k<6;++k) {
            Wave& w=shots[k];w.frameCount=k==3?44100:17640;w.sampleRate=44100;w.sampleSize=16;w.channels=2;
            w.data=MemAlloc(w.frameCount*4);auto* pcm=(short*)w.data;unsigned rng=417+k;
            float low=0;
            for(unsigned n=0;n<w.frameCount;++n) {
                float t=n/44100.0f;rng=rng*1664525u+1013904223u;float noise=((rng>>8)/8388608.0f-1);
                low+=.07f*(noise-low);
                float v=k==3?(low*1.9f+std::sin(t*6.283185f*(65-25*t))*.24f)*std::exp(-t*5):k==4?noise*.14f*std::exp(-t*13)+std::sin(t*6.283185f*1900)*.045f*std::exp(-t*7):low*.4f*std::sin(t/.4f*3.14159f);
                pcm[n*2]=pcm[n*2+1]=(short)(std::clamp(v,-1.0f,1.0f)*26000);
            }
        }
    }
    void sample(DWORD stream,const void* bytes,DWORD size,LONGLONG time,LONGLONG duration) {
        IMFMediaBuffer* buffer=nullptr; IMFSample* sample=nullptr;
        hr(MFCreateMemoryBuffer(size,&buffer)); BYTE* data=nullptr; hr(buffer->Lock(&data,nullptr,nullptr));
        std::memcpy(data,bytes,size); hr(buffer->Unlock()); hr(buffer->SetCurrentLength(size));
        hr(MFCreateSample(&sample)); hr(sample->AddBuffer(buffer)); hr(sample->SetSampleTime(time)); hr(sample->SetSampleDuration(duration));
        hr(writer->WriteSample(stream,sample)); release(sample); release(buffer);
    }
    void frame(RenderTexture2D canvas,int index,int count) {
        Image pixels=LoadImageFromTexture(canvas.texture); ImageFormat(&pixels,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        // The encoder expects top-down RGB32; normalize the render texture exactly once.
        ImageFlipVertical(&pixels);
        auto* rgba=static_cast<unsigned char*>(pixels.data);
        for (int p=0;p<W*H;++p) std::swap(rgba[p*4],rgba[p*4+2]);
        LONGLONG start=(LONGLONG)index*10000000/FPS,end=(LONGLONG)(index+1)*10000000/FPS;
        sample(video,rgba,W*H*4,start,end-start); UnloadImage(pixels);
        constexpr int block=44100/FPS; std::vector<short> pcm(block*2);
        auto* source=static_cast<short*>(music.data);
        for(int s=0;s<block;++s) {
            float seconds=(index*block+s)/44100.0f;
            float gain=0.16f*std::min({1.0f,seconds/1.2f,(count/(float)FPS-seconds)/1.5f});
            int sourceFrame=(index*block+s)%music.frameCount;
            for(int channel=0;channel<2;++channel) {
                float value=source[sourceFrame*2+channel]*gain;
                long long now=(long long)index*block+s;
                for(const auto& voice:voices) {
                    long long offset=now-voice.start;
                    if(offset>=0 && offset<shots[voice.weapon].frameCount)
                        value+=static_cast<short*>(shots[voice.weapon].data)[offset*2+channel]*voice.gain;
                }
                pcm[s*2+channel]=(short)std::clamp(value,-32000.0f,32000.0f);
            }
        }
        sample(audio,pcm.data(),(DWORD)pcm.size()*2,start,end-start);
        voices.erase(std::remove_if(voices.begin(),voices.end(),[&](const Voice& voice){return (long long)(index+1)*block-voice.start>=shots[voice.weapon].frameCount;}),voices.end());
    }
    void finish() { hr(writer->Finalize()); }
    ~Mp4() { for(auto& shot:shots) UnloadWave(shot); UnloadWave(music); release(writer); MFShutdown(); CoUninitialize(); }
};

void extract(const wchar_t* path,const char* pattern="bin/review/highlight-decoded-%04d.png") {
    hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); hr(MFStartup(MF_VERSION));
    IMFAttributes* attributes=nullptr; hr(MFCreateAttributes(&attributes,1));
    hr(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE));
    IMFSourceReader* reader=nullptr; hr(MFCreateSourceReaderFromURL(path,attributes,&reader)); release(attributes);
    IMFMediaType* type=nullptr; hr(MFCreateMediaType(&type)); hr(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)); hr(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32));
    hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type)); release(type);
    for(int i=0;i<FRAMES;++i) {
        IMFSample* s=nullptr; DWORD flags=0; hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,nullptr,&s));
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM) break;
        if(s && (i==45 || i==120 || i==225 || i==690 || i==1200 || i==1530 || i==1890)) {
            IMFMediaBuffer* b=nullptr; hr(s->ConvertToContiguousBuffer(&b)); BYTE* data=nullptr; DWORD length=0; hr(b->Lock(&data,nullptr,&length));
            IMFMediaType* decoded=nullptr; hr(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&decoded));
            UINT32 width=0,height=0,pitch=0; hr(MFGetAttributeSize(decoded,MF_MT_FRAME_SIZE,&width,&height));
            if(FAILED(decoded->GetUINT32(MF_MT_DEFAULT_STRIDE,&pitch))) pitch=width*4;
            LONG stride=(LONG)pitch; std::cout<<"Decoded "<<width<<"x"<<height<<" stride "<<stride<<" bytes "<<length<<std::endl; release(decoded);
            Image image=GenImageColor(width,height,BLACK); auto* out=static_cast<unsigned char*>(image.data);
            for(UINT32 y=0;y<height;++y) for(UINT32 x=0;x<width;++x) {
                const BYTE* pixel=data+(stride<0?(height-1-y)*(-stride):y*stride)+x*4;
                int p=y*width+x; out[p*4]=pixel[2]; out[p*4+1]=pixel[1]; out[p*4+2]=pixel[0]; out[p*4+3]=255;
            }
            ExportImage(image,TextFormat(pattern,i)); UnloadImage(image);
            hr(b->Unlock()); release(b);
        }
        release(s);
        
    }
    release(reader); MFShutdown(); CoUninitialize();
}

namespace {
float length(Vector2 p) { return std::hypot(p.x,p.y); }
Vector2 sub(Vector2 a,Vector2 b) { return {a.x-b.x,a.y-b.y}; }
Vector2 norm(Vector2 p) { float d=length(p); return d>0.001f?Vector2{p.x/d,p.y/d}:Vector2{}; }
float ease(float t) { t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t); }
}
class FilmBot : public BotEnemy {
public:
    FilmBot(Vector2 p,int index):BotEnemy(p,CharacterPreview::BodyPath(index%4+1)) {
        speed=160; SetWeaponSkin(0x10000|((index%4)*0x5555)); SwitchWeapon(index%WeaponCount);
    }
    void move(Vector2 direction,float dt) {
        if(GetFlashBlind()>0 || IsDead()) {SetState(IsDead()?CharState::DEATH:CharState::IDLE);return;}
        if(dashTimer<=0) {position.x+=direction.x*speed*dt;position.y+=direction.y*speed*dt;}
        SetState(jumpHeight>1?(jumpVelocity>0?CharState::JUMP_START:CharState::FALL):length(direction)>.05f?CharState::WALK:CharState::IDLE);
    }
};
struct FilmActor {
    FilmBot* body; Vector2 spawn; float deadTime=0,repath=0,nextJump=0,nextDash=0,nextBurst=0;
    std::vector<Vector2> path;
};
struct FilmScene {
    std::unique_ptr<GameplayScreen> game;
    std::vector<FilmActor> actors;
    std::vector<Rectangle> walls;
    std::vector<bool> walk;
    Vector2 cameraCenter{2400,1600}; float zoom=.55f,time=0,nextThrow=1.2f;
    int throwCount=0,focus=0,shots=0,jumps=0,throws[3]{},kills=0,respawns=0;
    float nextFocus=0,shake=0,minSeparation=10000;
    static constexpr int NX=75,NY=50; static constexpr float Cell=64;
    bool clear(Vector2 p,float radius=16) const {
        if(p.x<300 || p.x>4500 || p.y<320 || p.y>2860) return false;
        for(auto r:walls) if(CheckCollisionRecs({p.x-radius,p.y-radius,radius*2,radius*2},r)) return false;
        return true;
    }
    bool visible(Vector2 a,Vector2 b) const {
        float d=length(sub(b,a));int steps=std::max(1,(int)(d/24));
        for(int i=0;i<=steps;++i) if(!clear({a.x+(b.x-a.x)*i/steps,a.y+(b.y-a.y)*i/steps},12)) return false;
        return true;
    }
    Vector2 safe(Vector2 p) const {
        if(clear(p,38))return p;
        for(int r=64;r<1400;r+=64)for(int j=0;j<24;++j) {
            Vector2 q{p.x+r*std::cos(j*6.283185f/24),p.y+r*std::sin(j*6.283185f/24)};
            if(clear(q,38))return q;
        }
        throw std::runtime_error("No safe film route");
    }
    int cell(Vector2 p) const {return std::clamp((int)(p.y/Cell),0,NY-1)*NX+std::clamp((int)(p.x/Cell),0,NX-1);}
    Vector2 point(int c) const {return {(c%NX+.5f)*Cell,(c/NX+.5f)*Cell};}
    std::vector<Vector2> route(Vector2 start,Vector2 goal) const {
        if(visible(start,goal))return {goal};
        int a=cell(start),b=cell(goal); if(!walk[b])b=cell(safe(goal));
        std::vector<int> parent(NX*NY,-1);std::queue<int> q;q.push(a);parent[a]=a;
        while(!q.empty() && parent[b]<0) {
            int c=q.front();q.pop();int x=c%NX,y=c/NX;
            const int dx[]={1,-1,0,0},dy[]={0,0,1,-1};
            for(int k=0;k<4;++k) {
                int xx=x+dx[k],yy=y+dy[k];if(xx<0 || xx>=NX || yy<0 || yy>=NY)continue;
                int n=yy*NX+xx;if(parent[n]<0 && walk[n]) {parent[n]=c;q.push(n);}
            }
        }
        if(parent[b]<0)return {};
        std::vector<Vector2> out;for(int c=b;c!=a;c=parent[c])out.push_back(point(c));
        std::reverse(out.begin(),out.end());out.push_back(goal);return out;
    }
};
struct GameplayEffectsTestAccess {
    static void prepare(FilmScene& s,const char* map) {
        auto& setup=Offline::Get();setup.mode=Offline::Mode::FreeForAll;setup.bots=0;setup.map=map;
        setup.killLimit=1000;setup.timeLimit=1800;setup.difficulty=2;
        NetworkManager::GetInstance().selectedMapName=map;
        s.game=std::make_unique<GameplayScreen>(GameMode::PRACTICE);auto& g=*s.game;g.cinematicCapture=true;
        // The normal local player stays outside this spectator-only offline fight.
        g.player->TakeDamage(1000);g.player->SetPosition({-10000,-10000});
        for(const auto& group:g.allObjectGroups)for(unsigned i=0;i<group.objectsLength;++i) {
            const auto& o=group.objects[i];
            if(o.type==OBJECT_TYPE_RECTANGLE)s.walls.push_back({(float)o.x,(float)o.y,(float)o.width,(float)o.height});
            else s.walls.push_back(o.aabb);
        }
        s.walk.resize(FilmScene::NX*FilmScene::NY);
        for(int i=0;i<(int)s.walk.size();++i)s.walk[i]=s.clear(s.point(i),26);
        constexpr int count=12;
        for(int i=0;i<count;++i) {
            float angle=i*6.283185f/count;
            Vector2 p=s.safe({2400+std::cos(angle)*1050,1600+std::sin(angle)*850});
            // Initial spawn placement is outside the recorded simulation, never a movement shortcut.
            for(const auto& a:s.actors)if(length(sub(a.spawn,p))<270)p=s.safe({p.x+180,p.y+200});
            auto actor=std::make_unique<FilmBot>(p,i);
            actor->SetName(i==0?"@rjltyd":"RIOT "+std::to_string(i+1));actor->SetTeamID(0);
            FilmBot* ptr=actor.get();g.bots.push_back(std::move(actor));
            s.actors.push_back({ptr,p,0,i*.04f,1.0f+i*.32f,3.0f+i*.43f,i*.06f,{}});
        }
        g.camera.offset={650,340};g.camera.target=s.cameraCenter;g.camera.zoom=s.zoom;
    }
    static void step(FilmScene& s,float dt,Mp4* audio,int frame) {
        auto& g=*s.game;s.time+=dt;g.worldTime=s.time;s.shake=std::max(0.0f,s.shake-dt*25);
        for(size_t i=0;i<s.actors.size();++i) {
            auto& a=s.actors[i];auto* c=a.body;Vector2 previous=c->GetPosition();
            if(c->IsDead()) {
                a.deadTime+=dt;c->Update(dt);
                // Standard timed outpost respawn; no mid-fight repositioning or skipped frames.
                if(a.deadTime>4.2f) {c->ResetHealth(100);c->ResetDeathTimer();c->SetPosition(a.spawn);a.deadTime=0;a.path.clear();++s.respawns;}
                continue;
            }
            Character* enemy=nullptr;float closest=1e9f;
            for(auto& other:s.actors)if(other.body!=c && !other.body->IsDead()) {
                float d=length(sub(other.body->GetPosition(),previous));
                if(d<closest) {closest=d;enemy=other.body;}
                s.minSeparation=std::min(s.minSeparation,d);
            }
            if(!enemy) {c->Update(dt);continue;}
            Vector2 target=enemy->GetPosition();target.y+=24-enemy->GetJumpHeight();
            Vector2 forward=norm(sub(target,previous));float side=std::sin(s.time*.55f+i*1.7f)>0?1:-1;
            float approach=closest>720?.7f:closest<390?-.85f:0;
            Vector2 direction{forward.x*approach-forward.y*side*.65f,forward.y*approach+forward.x*side*.65f};
            for(auto& other:s.actors)if(other.body!=c && !other.body->IsDead()) {
                Vector2 away=sub(previous,other.body->GetPosition());float d=length(away);
                if(d<330 && d>1) {direction.x+=away.x/d*(330-d)/70;direction.y+=away.y/d*(330-d)/70;}
            }
            a.repath-=dt;
            if(a.repath<=0) {
                a.repath=.55f;Vector2 desired=s.safe({previous.x+direction.x*260,previous.y+direction.y*260});
                a.path=s.route(previous,desired);
            }
            while(!a.path.empty() && length(sub(a.path.front(),previous))<35)a.path.erase(a.path.begin());
            Vector2 motion=a.path.empty()?Vector2{}:norm(sub(a.path.front(),previous));
            c->move(motion,dt);c->SetAimTarget(target);c->SetFaceDirection(target.x>=previous.x?1:-1);
            if(c->GetFlashBlind()<=0) {
                if(s.time>a.nextJump && closest<950 && c->GetJumpHeight()<1) {c->SetJumpVelocity(420);a.nextJump=s.time+3.4f+i*.11f;++s.jumps;}
                if(s.time>a.nextDash && closest<550) {c->ActivateDash(motion);a.nextDash=s.time+5.8f+i*.13f;}
                if(c->GetHealth()<45 && closest<900)c->ActivateShield();
                if(c->GetAmmo()==0)c->TriggerReload();
                // Controlled bursts leave visible movement/reload windows between engagements.
                if(closest<1450 && s.visible(previous,enemy->GetPosition()) && std::fmod(s.time+i*.3f,1.65f)<.70f) c->Shoot(target);
            }
            c->Update(dt);g.ResolveWorldMovement(c,previous);g.ClampCharacterToWorld(c);
            if(c->DidShoot()) {
                ++s.shots;float d=length(sub(c->GetPosition(),s.cameraCenter));
                if(audio && d<1600)audio->shot(c->GetWeaponIndex(),frame,.23f*std::clamp(1-d/1900,.15f,1.0f));
            }
        }
        // Bullet damage and hit tests use the same body boxes and weapon damage as the game.
        for(auto& a:s.actors)for(auto& bullet:a.body->GetProjectiles())if(bullet->IsActive()) {
            for(auto& b:s.actors)if(b.body!=a.body && !b.body->IsDead()) {
                Vector2 p=b.body->GetPosition();p.y-=b.body->GetJumpHeight();
                if(CheckCollisionCircleRec(bullet->GetPosition(),3,{p.x-22,p.y,44,60})) {
                    b.body->TakeDamage(bullet->GetDamage());bullet->Deactivate();
                    if(b.body->IsDead()) {g.RecordOfflineKill(a.body,b.body,bullet->GetWeaponIndex(),bullet->GetWeaponSkin());++s.kills;}
                    break;
                }
            }
        }
        if(s.time>=s.nextThrow) {
            FilmActor* thrower=nullptr;float best=1e9f;
            for(auto& a:s.actors)if(!a.body->IsDead() && a.body->GetFlashBlind()<=0) {
                float score=length(sub(a.body->GetPosition(),s.cameraCenter))+((int)(&a-&s.actors[0])==s.focus?0:250);
                if(score<best){best=score;thrower=&a;}
            }
            if(thrower) {
                auto* c=thrower->body;Character* target=nullptr;float nearest=1e9;
                for(auto& a:s.actors)if(a.body!=c && !a.body->IsDead()) {float d=length(sub(a.body->GetPosition(),c->GetPosition()));if(d<nearest){nearest=d;target=a.body;}}
                if(target) {
                    Vector2 start=c->GetHandPosition(),landing=target->GetPosition();Vector2 d=sub(landing,start);float dist=length(d);
                    if(dist>650){landing.x=start.x+d.x/dist*650;landing.y=start.y+d.y/dist*650;}
                    int kind=s.throwCount%3;
                    g.grenades.push_back({start,landing,0,kind,false,(uint32_t)(thrower-&s.actors[0]+1),0});
                    ++s.throws[kind];++s.throwCount;
                    if(audio)audio->effect(5,frame,.35f);
                    s.focus=(int)(thrower-&s.actors[0]);s.nextFocus=s.time+3.0f;
                }
            }
            s.nextThrow=s.time+2.3f;
        }
        for(auto& grenade:g.grenades)if(grenade.kind==0 && grenade.age<.8f && grenade.age+dt>=.8f) {
            if(audio)audio->effect(3,frame,.85f);
            s.shake=std::max(s.shake,12.0f*std::clamp(1-length(sub(grenade.target,s.cameraCenter))/1600,.1f,1.0f));
            Character* owner=grenade.ownerID>0?s.actors[grenade.ownerID-1].body:nullptr;
            for(auto& a:s.actors)if(!a.body->IsDead() && length(sub(a.body->GetPosition(),grenade.target))<=230) {
                a.body->TakeDamage(40);if(a.body->IsDead() && owner){g.RecordOfflineKill(owner,a.body,WeaponCount);++s.kills;}
            }
        }
        if(audio)for(auto& grenade:g.grenades)if(grenade.kind==2 && grenade.age<.8f && grenade.age+dt>=.8f)audio->effect(4,frame,.7f);
        g.UpdateGrenades(dt,false);
        g.UpdateKillFeed(dt);
    }
    static void camera(FilmScene& s,float dt) {
        if(s.time>=s.nextFocus || s.actors[s.focus].body->IsDead()) {
            int next=s.focus;float best=-1;
            for(size_t i=0;i<s.actors.size();++i)if(!s.actors[i].body->IsDead()) {
                float activity=0;for(auto& a:s.actors)if(a.body!=s.actors[i].body && !a.body->IsDead()) {
                    float d=length(sub(a.body->GetPosition(),s.actors[i].body->GetPosition()));
                    if(d>250 && d<1000)activity+=1;
                }
                activity+=.7f*std::sin(i*2.7f+s.time*.6f);if((int)i==s.focus)activity-=2;
                if(activity>best){best=activity;next=(int)i;}
            }
            s.focus=next;s.nextFocus=s.time+4.4f;
        }
        Vector2 p=s.actors[s.focus].body->GetPosition();Character* enemy=nullptr;float closest=1e9;
        for(auto& a:s.actors)if(a.body!=s.actors[s.focus].body && !a.body->IsDead()) {
            float d=length(sub(a.body->GetPosition(),p));if(d<closest){closest=d;enemy=a.body;}
        }
        Vector2 target=p;
        if(enemy){Vector2 q=enemy->GetPosition();target={(p.x+q.x)*.5f,(p.y+q.y)*.5f};}
        float shot=std::fmod(s.time,12.0f);
        float desired=shot<4?.52f+ease(shot/4)*.37f:shot<8?.89f+ease((shot-4)/4)*.23f:1.12f-ease((shot-8)/4)*.60f;
        if(enemy)desired=std::min(desired,std::min(1100.0f/(std::abs(p.x-enemy->GetPosition().x)+330),560.0f/(std::abs(p.y-enemy->GetPosition().y)+260)));
        desired=std::clamp(desired,.48f,1.12f);
        s.zoom+=(desired-s.zoom)*(1-std::exp(-dt*1.8f));
        s.cameraCenter.x+=(target.x-s.cameraCenter.x)*(1-std::exp(-dt*1.0f));
        s.cameraCenter.y+=(target.y-s.cameraCenter.y)*(1-std::exp(-dt*1.0f));
        float hw=650/s.zoom+40,hh=340/s.zoom+40;
        s.cameraCenter.x=std::clamp(s.cameraCenter.x,hw,4800-hw);s.cameraCenter.y=std::clamp(s.cameraCenter.y,hh,3200-hh);
        auto& camera=s.game->camera;camera.target=s.cameraCenter;camera.zoom=s.zoom;camera.rotation=std::sin(s.time*.23f)*.7f;
        camera.offset={650+std::sin(s.time*103)*s.shake,340+std::cos(s.time*119)*s.shake};
    }
};
void centered(Font font,const char* text,float y,float size,Color color,float spacing=2) {
    Vector2 m=MeasureTextEx(font,text,size,spacing);DrawTextEx(font,text,{(W-m.x)/2,y},size,spacing,color);
}
void webVideo() {
    std::filesystem::create_directories("docs/media");
    IMFAttributes* attributes=nullptr;hr(MFCreateAttributes(&attributes,1));
    hr(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE));
    IMFSourceReader* reader=nullptr;hr(MFCreateSourceReaderFromURL(L"bin/review/vanta-riot-highlights.mp4",attributes,&reader));release(attributes);
    hr(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE));
    IMFMediaType* type=nullptr;hr(MFCreateMediaType(&type));hr(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
    hr(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32));
    hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type));release(type);
    hr(MFCreateMediaType(&type));hr(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));hr(type->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM));
    hr(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2));hr(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,44100));hr(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));
    hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,type));release(type);
    hr(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE));hr(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE));
    Mp4 output(L"docs/media/vanta-riot-highlights.mp4",1280,720,850000);
    int frames=0,ended=0;
    while(ended<2) {
        DWORD stream=0,flags=0;LONGLONG timestamp=0;IMFSample* sample=nullptr;
        HRESULT read=reader->ReadSample(MF_SOURCE_READER_ANY_STREAM,0,&stream,&flags,&timestamp,&sample);
        if(FAILED(read)) {std::cout<<"Reader ended at frame "<<frames<<", ended streams "<<ended<<std::endl;if(frames==1991)break;hr(read);}
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM) {std::cout<<"End stream "<<stream<<" at "<<frames<<" frames"<<std::endl;reader->SetStreamSelection(stream,FALSE);++ended;release(sample);continue;}
        if(!sample)continue;
        hr(reader->GetCurrentMediaType(stream,&type));GUID major{};hr(type->GetGUID(MF_MT_MAJOR_TYPE,&major));UINT32 width=0,height=0,pitch=0;if(major==MFMediaType_Video){hr(MFGetAttributeSize(type,MF_MT_FRAME_SIZE,&width,&height));if(FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE,&pitch)))pitch=width*4;}release(type);
        IMFMediaBuffer* buffer=nullptr;hr(sample->ConvertToContiguousBuffer(&buffer));BYTE* data=nullptr;DWORD size=0;hr(buffer->Lock(&data,nullptr,&size));
        LONGLONG duration=0;if(FAILED(sample->GetSampleDuration(&duration)))duration=major==MFMediaType_Video?10000000/FPS:(LONGLONG)size*10000000/(44100*4);
        if(major==MFMediaType_Video) {
            Image image=GenImageColor(width,std::min(height,1080u),BLACK);LONG stride=(LONG)pitch;
            for(int row=0;row<image.height;++row)std::memcpy((BYTE*)image.data+row*width*4,data+(stride<0?(height-1-row)*(-stride):row*stride),width*4);
            for(int pixel=0;pixel<image.width*image.height;++pixel)((BYTE*)image.data)[pixel*4+3]=255;
            ImageResize(&image,1280,720);output.sample(output.video,image.data,1280*720*4,timestamp,duration);UnloadImage(image);
            if(++frames%300==0)std::cout<<"Compressed "<<frames<<" frames"<<std::endl;}
        else output.sample(output.audio,data,size,timestamp,duration);
        hr(buffer->Unlock());release(buffer);release(sample);
    }
    output.finish();release(reader);std::cout<<"GitHub video ready: "<<frames<<" frames"<<std::endl;
}
int main(int argc,char** argv) {
    try {
        hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(VIRTUAL_WIDTH,VIRTUAL_HEIGHT,"Vanta highlight capture");
        InitAudioDevice();SetMasterVolume(0);SetExitKey(KEY_NULL);SetRandomSeed(20261018);Character::SetCombatAudioEnabled(false);
        std::filesystem::create_directories("bin/review");
        if(argc>1 && std::string(argv[1])=="--extract") {extract(L"bin/review/vanta-riot-highlights.mp4");return 0;}
        if(argc>1 && std::string(argv[1])=="--web-video") {hr(MFStartup(MF_VERSION));webVideo();return 0;}
        if(argc>1 && std::string(argv[1])=="--extract-web") {extract(L"docs/media/vanta-riot-highlights.mp4","bin/review/github-decoded-%04d.png");return 0;}
        bool probe=argc>1 && std::string(argv[1])=="--probe";
        MapRegistry::GetInstance().LoadAllMaps();NetworkManager::GetInstance().Initialize();
        FilmScene scene[3];const char* maps[]={"MagmaRift","AbandonedFoundry","DesertOutpost"};
        const char* names[]={"MAGMA RIFT","ABANDONED FOUNDRY","DESERT OUTPOST"};
        for(int i=0;i<3;++i)GameplayEffectsTestAccess::prepare(scene[i],maps[i]);
        Font font=LoadFontEx("assets/fonts/BruceForeverRegular-X3jd2.ttf",180,nullptr,0);SetTextureFilter(font.texture,TEXTURE_FILTER_BILINEAR);
        auto canvas=LoadRenderTexture(W,H),gameFrame=LoadRenderTexture(VIRTUAL_WIDTH,VIRTUAL_HEIGHT),previousFrame=LoadRenderTexture(VIRTUAL_WIDTH,VIRTUAL_HEIGHT);
        Mp4 video(L"bin/review/vanta-riot-highlights.mp4");
        // Record the actual complete game intro, followed by 18 continuous seconds per arena.
        int introFrames=0;
        { FirstScreen timing; do {++introFrames;}while(timing.Update(1.0f/FPS) && introFrames<900); }
        FirstScreen originalIntro;
        float introDuration=introFrames/(float)FPS;
        int totalFrames=introFrames+55*FPS;
        int count=probe?introFrames+12*FPS:totalFrames;
        std::cout<<"Original game intro: "<<introFrames<<" frames, video "<<count<<" frames"<<std::endl;
        int lastMap=-1;
        for(int frame=0;frame<count;++frame) {
            float t=frame/(float)FPS;float gameplayTime=t-introDuration;int map=std::clamp((int)(gameplayTime/18),0,2);float local=gameplayTime-map*18;
            if(gameplayTime>=0 && gameplayTime<54) {
                if(lastMap!=map && lastMap>=0) {
                    BeginTextureMode(previousFrame);ClearBackground(BLACK);
                    DrawTexturePro(gameFrame.texture,{0,0,(float)VIRTUAL_WIDTH,-(float)VIRTUAL_HEIGHT},{0,0,(float)VIRTUAL_WIDTH,(float)VIRTUAL_HEIGHT},{0,0},0,WHITE);EndTextureMode();
                }
                lastMap=map;
                constexpr int substeps=4;float dt=PLAYBACK_SPEED/(FPS*substeps);
                for(int step=0;step<substeps;++step)GameplayEffectsTestAccess::step(scene[map],dt,&video,frame);
                GameplayEffectsTestAccess::camera(scene[map],1.0f/FPS);scene[map].game->Draw(gameFrame);
            }
            if(frame<introFrames) { originalIntro.Update(1.0f/FPS);originalIntro.Draw(gameFrame); }
            BeginTextureMode(canvas);ClearBackground(BLACK);
            if(frame<introFrames) {
                DrawTexturePro(gameFrame.texture,{0,0,(float)VIRTUAL_WIDTH,-(float)VIRTUAL_HEIGHT},{0,38,(float)W,1004},{0,0},0,WHITE);
            }
            else if(gameplayTime<54) {
                Rectangle source{0,0,(float)VIRTUAL_WIDTH,-(float)VIRTUAL_HEIGHT},dest{0,38,(float)W,1004};
                DrawTexturePro(gameFrame.texture,source,dest,{0,0},0,WHITE);
                if(map>0 && local<.65f) {
                    // A brief exposure dissolve connects maps, without skipping any gameplay simulation.
                    DrawTexturePro(previousFrame.texture,source,dest,{0,0},0,Fade(WHITE,1-ease(local/.65f)));
                    DrawRectangleRec(dest,Fade(Color{245,214,166,255},std::sin(local/.65f*3.14159f)*.13f));
                }
                float title=std::min(ease(local/.6f),ease((3.5f-local)/.7f));
                if(title>0) {
                    DrawRectangleGradientV(0,770,W,272,BLANK,Fade(BLACK,.55f*title));
                    DrawTextEx(font,"VANTA / RIOT",{94,849},20,2,Fade(Color{235,195,99,255},title));
                    DrawTextEx(font,names[map],{90,890},49,2,Fade(RAYWHITE,title));
                    DrawTextEx(font,"12 FIGHTERS  /  FREE FOR ALL",{94,957},16,2,Fade(RAYWHITE,title*.65f));
                }
                if(gameplayTime<.5f)DrawRectangle(0,0,W,H,Fade(BLACK,1-ease(gameplayTime/.5f)));
                if(gameplayTime>53)DrawRectangle(0,0,W,H,Fade(BLACK,ease(gameplayTime-53)));
            } else {centered(font,"VANTA",420,112,Fade(RAYWHITE,1-ease(gameplayTime-54)));}
            EndTextureMode();
            video.frame(canvas,frame,count);
            if(frame%150==0 || frame==45 || frame==120) {
                Image image=LoadImageFromTexture(canvas.texture);ImageFlipVertical(&image);
                ExportImage(image,TextFormat("bin/review/highlight-%04d.png",frame));UnloadImage(image);
                std::cout<<"Exported "<<frame<<" / "<<count<<std::endl;
            }
            BeginDrawing();ClearBackground(BLACK);EndDrawing();
        }
        video.finish();
        std::ofstream report("bin/review/highlight-capture-report.txt");
        report<<"Native VANTA renderer, offline FFA AI. 1920x1080, 30 fps, 1.12x playback.\nNo simulation skips. Camera only: smooth zoom, pan, roll, impact shake.\n";
        for(int i=0;i<3;++i)report<<maps[i]<<": simulated="<<scene[i].time<<" shots="<<scene[i].shots<<" jumps="<<scene[i].jumps<<" frag="<<scene[i].throws[0]<<" smoke="<<scene[i].throws[1]<<" flash="<<scene[i].throws[2]<<" kills="<<scene[i].kills<<" timed respawns="<<scene[i].respawns<<" minimum spacing="<<scene[i].minSeparation<<"\n";
        std::cout<<"Highlight MP4 complete"<<std::endl;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
    return 0;
}
