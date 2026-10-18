#include "combat_audio.h"
#include "../entities/character.h"
#include "../ui/game_settings.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
namespace CombatAudio {
namespace {
struct Bank { Sound base{};std::array<Sound,6> voices{};int next=0; };
std::array<Bank,WeaponCount> guns;
std::array<Bank,CueCount> cues;
Vector2 listener{};
void Voices(Bank& bank) {if(IsSoundValid(bank.base))for(auto& s:bank.voices)s=LoadSoundAlias(bank.base);}
void Synthesize(Bank& bank,Cue cue) {
    constexpr int rate=22050;
    float duration=cue==Medal?.48f:cue==Blast?.8f:cue==Kill?.3f:cue==Warning?.32f:.18f;
    std::vector<short> pcm((int)(rate*duration));uint32_t seed=371+cue*97;
    for(size_t i=0;i<pcm.size();++i){float t=i/(float)rate;seed=1664525*seed+1013904223;
        float noise=((seed>>8)/8388607.5f-1),v=0;
        switch(cue){
        case Step: v=(noise*.42f+std::sin(t*2*PI*105)*.55f)*std::exp(-t*38);break;
        case Land: v=(noise*.25f+std::sin(t*2*PI*(80-t*170))*.7f)*std::exp(-t*23);break;
        case Reload:case Ready: v=noise*(std::exp(-t*80)+.65f*std::exp(-std::abs(t-.07f)*150))*.65f;break;
        case Throw: v=noise*std::sin(PI*t/duration)*.25f;break;
        case Warning: v=std::sin(t*2*PI*1200)*std::exp(-std::fmod(t,.16f)*55)*.28f;break;
        case Blast: v=(noise*.8f+std::sin(t*2*PI*(60-t*30))*.4f)*std::exp(-t*6);break;
        case Hit: v=(noise*.2f+std::sin(t*2*PI*1550)*.4f)*std::exp(-t*70);break;
        case Kill: v=(std::sin(t*2*PI*880)+std::sin(t*2*PI*1320))*.25f*std::exp(-t*12);break;
        case Medal: {float note=t<.12f?660:t<.24f?880:1320;float envelope=std::exp(-std::fmod(t,.12f)*16)*std::min(1.0f,t*100)*(1-t/duration);v=std::sin(t*2*PI*note)*envelope*.36f;break;}
        case Flame: v=noise*.45f*std::sin(PI*t/duration);break;
        case Freeze: v=(std::sin(t*2*PI*1750)*.25f+noise*.35f)*std::exp(-t*19);break;
        case Shock: v=noise*std::sin(t*2*PI*120)*.45f*std::exp(-t*17);break;
        default:break;
        }
        pcm[i]=(short)(std::clamp(v,-1.0f,1.0f)*22000);
    }
    Wave wave{(unsigned int)pcm.size(),rate,16,1,pcm.data()};bank.base=LoadSoundFromWave(wave);Voices(bank);
}
void Emit(Bank& bank,SpatialMix mix,float pitch=1) {
    if(!IsSoundValid(bank.base))return;auto voice=bank.voices[bank.next++%bank.voices.size()];
    if(!IsSoundValid(voice))return;
    SetSoundVolume(voice,std::clamp(mix.volume*Settings::Get().sfx,0.0f,1.0f));SetSoundPan(voice,mix.pan);SetSoundPitch(voice,pitch);PlaySound(voice);
}
bool Audible(){return Character::IsCombatAudioEnabled() && IsAudioDeviceReady() && Settings::Get().sfx>0;}
}
SpatialMix Mix(Vector2 source,Vector2 ear,float range) {
    float dx=source.x-ear.x,dy=source.y-ear.y,distance=std::hypot(dx,dy);
    float falloff=std::clamp(1-distance/std::max(1.0f,range),0.0f,1.0f);
    return {falloff*falloff,std::clamp(dx/600.0f,-.9f,.9f)};
}
void Listen(Vector2 position){listener=position;}
void Weapon(int index,Vector2 source) {
    if(!Audible() || index<0 || index>=WeaponCount)return;auto mix=Mix(source,listener,1800);if(mix.volume<.003f)return;
    auto& bank=guns[index];
    if(!IsSoundValid(bank.base)){
        const char* paths[]={"weapon1.mp3","weapon3.mp3","weapon2.mp3","weapon1.mp3","weapon2.mp3","weapon1.mp3","weapon3.mp3","weapon1.mp3"};
        std::string path=std::string("assets/sounds/")+paths[index];Wave wave=LoadWave(path.c_str());
        if(IsWaveValid(wave)){
            WaveFormat(&wave,wave.sampleRate,16,1);auto* samples=(short*)wave.data;unsigned int start=0;
            while(start+1<wave.frameCount && std::abs((int)samples[start])<150)++start;
            if(start>0 && start+1<wave.frameCount)WaveCrop(&wave,start,wave.frameCount);
            bank.base=LoadSoundFromWave(wave);UnloadWave(wave);Voices(bank);
        }
    }
    const float pitch[]={1.1f,.88f,1.08f,.88f,.68f,.76f,.78f,1.2f};Emit(bank,mix,pitch[index]);
}
void Play(Cue cue,Vector2 source,float strength) {
    if(!Audible() || cue<0 || cue>=CueCount)return;
    float range=cue==Step?620:cue==Reload || cue==Ready?650:cue==Blast?1900:1000;
    auto mix=cue==Medal?SpatialMix{1,0}:Mix(source,listener,range);mix.volume*=strength;if(mix.volume<.003f)return;
    auto& bank=cues[cue];if(!IsSoundValid(bank.base))Synthesize(bank,cue);Emit(bank,mix);
}
void Shutdown(){
    auto clear=[](Bank& bank){for(auto& voice:bank.voices)if(IsSoundValid(voice))UnloadSoundAlias(voice);if(IsSoundValid(bank.base))UnloadSound(bank.base);bank={};};
    for(auto& bank:guns)clear(bank);for(auto& bank:cues)clear(bank);
}
}
