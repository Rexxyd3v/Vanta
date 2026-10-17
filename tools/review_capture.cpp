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

ProximityVoiceChat proximityVoiceChat;
bool proximityVoiceInitialized=false;
bool (*getLocalPlayerPosCallback)(Vector2&)=nullptr;
constexpr int W=1280,H=672,FPS=30,FRAMES=1050;
constexpr float PLAYBACK_SPEED=1.3f;
void hr(HRESULT value) { if (FAILED(value)) { std::cerr<<"Media export HRESULT 0x"<<std::hex<<value<<std::endl; throw std::runtime_error("Media export failed"); } }
template<class T> void release(T*& p) { if(p) { p->Release(); p=nullptr; } }
struct Mp4 {
    IMFSinkWriter* writer=nullptr; DWORD video=0,audio=0; Wave music{};
    struct Voice { int weapon; long long start; float gain; };
    Wave shots[3]{}; std::vector<Voice> voices;
    void shot(int weapon,int frame,float gain) { if(frame>=0) voices.push_back({weapon,(long long)frame*(44100/FPS),gain}); }
    Mp4(const wchar_t* path) {
        hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); hr(MFStartup(MF_VERSION));
        IMFAttributes* attributes=nullptr; hr(MFCreateAttributes(&attributes,2));
        hr(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE));
        hr(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE));
        hr(MFCreateSinkWriterFromURL(path,nullptr,attributes,&writer)); release(attributes);
        IMFMediaType *output=nullptr,*input=nullptr;
        hr(MFCreateMediaType(&output)); hr(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
        hr(output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264)); hr(output->SetUINT32(MF_MT_AVG_BITRATE,6500000));
        hr(output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        hr(MFSetAttributeSize(output,MF_MT_FRAME_SIZE,W,H)); hr(MFSetAttributeRatio(output,MF_MT_FRAME_RATE,FPS,1));
        hr(MFSetAttributeRatio(output,MF_MT_PIXEL_ASPECT_RATIO,1,1)); hr(writer->AddStream(output,&video)); release(output);
        hr(MFCreateMediaType(&input)); hr(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
        hr(input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32)); hr(input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        hr(MFSetAttributeSize(input,MF_MT_FRAME_SIZE,W,H)); hr(MFSetAttributeRatio(input,MF_MT_FRAME_RATE,FPS,1));
        hr(MFSetAttributeRatio(input,MF_MT_PIXEL_ASPECT_RATIO,1,1)); hr(input->SetUINT32(MF_MT_DEFAULT_STRIDE,W*4));
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

// Drives the local fighter through the same movement, ammo, health, and cooldown rules.
// This is an offline AI-controlled match, with no immunity or forced captures.
class ReviewPlayer : public Player {
public:
    using Player::Player;
    void drive(float dt,Vector2 enemy,float distance,float time,bool threat) {
        if(IsDead()) { SetState(CharState::DEATH); UpdateWithoutInput(dt); return; }
        float dx=enemy.x-position.x,dy=enemy.y-position.y,len=std::max(1.0f,std::hypot(dx,dy));
        Vector2 forward{dx/len,dy/len};
        float side=std::sin(time*1.4f)>0?1.0f:-1.0f;
        float approach=distance>500?1.0f:distance<230?-0.65f:0.1f;
        Vector2 direction{forward.x*approach-forward.y*side*0.75f,forward.y*approach+forward.x*side*0.75f};
        float length=std::max(1.0f,std::hypot(direction.x,direction.y)); direction.x/=length; direction.y/=length;
        if(dashTimer<=0) { position.x+=direction.x*speed*dt; position.y+=direction.y*speed*dt; }
        if((threat || distance>700) && dashCooldown<=0) ActivateDash(direction);
        if(health<50 && distance<700) ActivateShield();
        if(threat && jumpHeight<=floorHeight+1 && jumpVelocity<=0) jumpVelocity=420;
        SetState(jumpHeight>floorHeight+1?(jumpVelocity>0?CharState::JUMP_START:CharState::FALL):CharState::WALK);
        int weapon=distance<230?1:distance>800?2:0;
        if(!IsReloading()) SwitchWeapon(weapon);
        SetAimTarget(enemy); SetFaceDirection(dx>=0?1:-1);
        if(distance<1100) Shoot(enemy);
        if(GetAmmo()==0 || (GetAmmo()<4 && distance>650)) TriggerReload();
        UpdateWithoutInput(dt);
    }
};
struct GameplayEffectsTestAccess {
    static ReviewPlayer* prepare(GameplayScreen& g) {
        Vector2 position=g.player->GetPosition(); int team=g.player->GetTeamID(); delete g.player;
        auto* actor=new ReviewPlayer(position,0,CharacterPreview::BodyPath(1),0);
        actor->SetTeamID(team); actor->SetName("@rjltyd"); g.player=actor;
        g.cinematicCapture=true;
        return actor;
    }
    static bool frameCamera(GameplayScreen& g,float time) {
        // Follow the closest live duel, allowing natural action to choose the shot.
        static Character *first=nullptr,*second=nullptr;
        static Vector2 center=g.player->GetPosition(); static float zoom=0.78f,nextCut=0;
        std::vector<Character*> fighters{g.player}; for(auto& b:g.bots) fighters.push_back(b.get());
        bool cut=false;
        if(!first || !second || first->IsDead() || second->IsDead() || time>=nextCut) {
            float best=1e9f;
            for(size_t i=0;i<fighters.size();++i) for(size_t j=i+1;j<fighters.size();++j) {
                if(fighters[i]->IsDead() || fighters[j]->IsDead()) continue;
                Vector2 a=fighters[i]->GetPosition(),b=fighters[j]->GetPosition();
                float distance=std::hypot(a.x-b.x,a.y-b.y);
                if(distance<best) { best=distance; first=fighters[i]; second=fighters[j]; }
            }
            nextCut=time+2.5f; cut=true;
        }
        if(!first || !second || first->IsDead() || second->IsDead()) return false;
        if(first && second) {
            Vector2 a=first->GetPosition(),b=second->GetPosition();
            Vector2 target{(a.x+b.x)/2,(a.y+b.y)/2+25};
            if(std::hypot(a.x-b.x,a.y-b.y)>1000) return false;
            float desired=std::clamp(std::min(1020.0f/(std::abs(a.x-b.x)+260),450.0f/(std::abs(a.y-b.y)+200)),0.48f,1.08f);
            if(cut && std::hypot(target.x-center.x,target.y-center.y)>450) { center=target; zoom=desired; }
            center.x+=(target.x-center.x)*0.13f; center.y+=(target.y-center.y)*0.13f;
            zoom+=(desired-zoom)*0.08f;
        }
        g.camera.target=center; g.camera.offset={650,340}; g.camera.zoom=zoom;
        float halfW=650/zoom,halfH=340/zoom;
        g.camera.target.x=std::clamp(g.camera.target.x,halfW,g.worldWidth-halfW);
        g.camera.target.y=std::clamp(g.camera.target.y,halfH,g.worldHeight-halfH);
        if(g.explosionShake>0) { g.camera.offset.x+=std::sin(time*93)*g.explosionShake*0.45f; g.camera.offset.y+=std::cos(time*117)*g.explosionShake*0.45f; }
        return true;
    }
    static void step(GameplayScreen& g,ReviewPlayer& player,float dt,float time,Mp4& video,int frame) {
        Character* target=g.NearestOfflineEnemy(&player); Vector2 aim=target?target->GetPosition():Vector2{g.worldWidth/2,g.worldHeight/2};
        float distance=std::hypot(aim.x-player.GetPosition().x,aim.y-player.GetPosition().y);
        bool threat=false;
        for(auto& bot:g.bots) if(g.OfflineEnemies(&player,bot.get())) for(auto& bullet:bot->GetProjectiles()) {
            Vector2 q=bullet->GetPosition(),p=player.GetPosition();
            if(bullet->IsActive() && std::hypot(q.x-p.x,q.y-p.y)<160) threat=true;
        }
        player.drive(dt,aim,distance,time,threat);
        Vector2 cursor=GetWorldToScreen2D(aim,g.camera);
        SetMouseOffset(0,0); Vector2 mouse=GetMousePosition(); SetMouseOffset((int)(cursor.x-mouse.x),(int)(cursor.y-mouse.y));
        if(!player.IsDead() && target && distance>330 && distance<600 && std::fmod(time,12.0f)<dt) g.ThrowGrenade(0);
        if(!player.IsDead() && target && player.GetHealth()<40 && distance<500 && std::fmod(time,16.0f)<dt) g.ThrowGrenade(1);
        g.Update(dt);
        player.SetAimTarget(aim);
        if(player.DidShoot()) video.shot(player.GetWeaponIndex(),frame,0.55f);
        for(auto& bot:g.bots) if(bot->DidShoot()) {
            float d=std::hypot(bot->GetPosition().x-player.GetPosition().x,bot->GetPosition().y-player.GetPosition().y);
            video.shot(bot->GetWeaponIndex(),frame,0.32f*std::clamp(1-d/1400,0.0f,1.0f));
        }
    }
};

void extract(const wchar_t* path) {
    hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); hr(MFStartup(MF_VERSION));
    IMFAttributes* attributes=nullptr; hr(MFCreateAttributes(&attributes,1));
    hr(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE));
    IMFSourceReader* reader=nullptr; hr(MFCreateSourceReaderFromURL(path,attributes,&reader)); release(attributes);
    IMFMediaType* type=nullptr; hr(MFCreateMediaType(&type)); hr(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)); hr(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32));
    hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type)); release(type);
    for(int i=0;i<1500;++i) {
        IMFSample* s=nullptr; DWORD flags=0; hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,nullptr,&s));
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM) break;
        if(s && (i==0 || i==300 || i==750 || i==1140)) {
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
            ExportImage(image,TextFormat("bin/review/riot-decoded-%04d.png",i)); UnloadImage(image);
            hr(b->Unlock()); release(b);
        }
        release(s);
        if(i>=1140) break;
    }
    release(reader); MFShutdown(); CoUninitialize();
}
int main(int argc,char** argv) {
    try {
        hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        SetTraceLogLevel(LOG_WARNING); SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(VIRTUAL_WIDTH,VIRTUAL_HEIGHT,"Vanta review export"); InitAudioDevice(); SetMasterVolume(0); SetExitKey(KEY_NULL); SetRandomSeed(20261017);
        if(argc>1 && std::string(argv[1])=="--extract") { extract(L"bin/review/vanta-ffa-riot-cinematic.mp4"); return 0; }
        bool probe=argc>1 && std::string(argv[1])=="--probe";
        MapRegistry::GetInstance().LoadAllMaps(); NetworkManager::GetInstance().Initialize();
        auto canvas=LoadRenderTexture(W,H);
        auto gameFrame=LoadRenderTexture(VIRTUAL_WIDTH,VIRTUAL_HEIGHT);
        Mp4 video(L"bin/review/vanta-ffa-riot-cinematic.mp4");
        NetworkManager::GetInstance().localUsername="@rjltyd";
        auto& setup=Offline::Get(); setup.mode=Offline::Mode::FreeForAll; setup.bots=3;
        setup.difficulty=2; setup.playerTeam=1; setup.killLimit=100; setup.timeLimit=1800;
        auto maps=MapRegistry::GetInstance().GetMapNames();
        setup.map=MapRegistry::GetInstance().HasMap("Forest2")?"Forest2":maps.front();
        NetworkManager::GetInstance().selectedMapName=setup.map;
        auto game=std::make_unique<GameplayScreen>(GameMode::PRACTICE);
        auto* player=GameplayEffectsTestAccess::prepare(*game);
        // Let the four solo fighters meet naturally after leaving separate outposts.
        float time=0; constexpr float dt=1.0f/90;
        for(int i=0;i<2700;++i) {
            GameplayEffectsTestAccess::step(*game,*player,dt,time,video,-1); time+=dt;
            if(i>720 && !player->IsDead() && player->GetHealth()>70) break;
        }
        int count=probe?3:FRAMES;
        for(int frame=0;frame<count;++frame) {
            constexpr int steps=4; float stepTime=PLAYBACK_SPEED/(FPS*steps);
            for(int step=0;step<steps;++step) { GameplayEffectsTestAccess::step(*game,*player,stepTime,time,video,frame); time+=stepTime; }
            int skipped=0;
            while(!GameplayEffectsTestAccess::frameCamera(*game,time) && skipped<1800) {
                GameplayEffectsTestAccess::step(*game,*player,stepTime,time,video,-1); time+=stepTime; ++skipped;
            }
            if(skipped>=1800) throw std::runtime_error("No active duel for cinematic capture");
            game->Draw(gameFrame);
            BeginTextureMode(canvas); ClearBackground(BLACK);
            DrawTexturePro(gameFrame.texture,{0,0,(float)VIRTUAL_WIDTH,-(float)VIRTUAL_HEIGHT},{0,0,(float)W,(float)H},{0,0},0,WHITE);
            EndTextureMode();
            // The footage fills the frame, with the game's original HUD and camera.
            video.frame(canvas,frame,count);
            if(frame%150==0) { Image image=LoadImageFromTexture(canvas.texture); ImageFlipVertical(&image); ExportImage(image,TextFormat("bin/review/riot-%04d.png",frame)); UnloadImage(image); std::cout<<"Exported "<<frame<<" / "<<count<<std::endl; }
            BeginDrawing(); ClearBackground(BLACK); EndDrawing();
        }
        video.finish(); game.reset();
        std::cout<<"MP4 export complete"<<std::endl;
    } catch(const std::exception& error) { std::cerr<<error.what()<<std::endl; return 1; }
    return 0;
}
