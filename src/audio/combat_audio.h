#pragma once
#include <raylib.h>
namespace CombatAudio {
enum Cue { Step, Land, Reload, Ready, Throw, Warning, Blast, Hit, Kill, Flame, Freeze, Shock, Medal, CueCount };
struct SpatialMix {float volume,pan;};
SpatialMix Mix(Vector2 source,Vector2 listener,float range);
void Listen(Vector2 position);
void Weapon(int index,Vector2 source);
void Play(Cue cue,Vector2 source,float strength=1);
void Shutdown();
}
