#pragma once
#include <algorithm>
namespace MatchPresentation {
inline constexpr float SquadSeconds=3.2f,RevealSeconds=7.4f,InputSeconds=7.8f;
inline float Progress(float age){return std::clamp((age-.65f)/(SquadSeconds-.65f),0.0f,1.0f);}
inline bool Ready(float age){return age>=InputSeconds;}
}
