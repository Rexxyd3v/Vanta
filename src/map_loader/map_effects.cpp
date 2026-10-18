#include "map_effects.h"
#include "../ui/game_settings.h"
#include <cmath>
#include <cstring>
#include <map>

bool IsMapGroundLayer(const TmxLayer& layer) {
 return layer.classString && (!std::strcmp(layer.classString,"ground") || !std::strcmp(layer.classString,"ground_blend"));
}
void DrawMapLayerWithMaterial(const TmxMap* map,const Camera2D* camera,const Rectangle* viewport,const TmxLayer* layer) {
 if(!layer->visible) return;
 bool mirror=layer->classString && !std::strcmp(layer->classString,"ground_blend");
 static Shader material{};
 static bool attempted=false;
 if(mirror && !attempted) {
  attempted=true;
  material=LoadShaderFromMemory(R"GLSL(#version 330
in vec3 vertexPosition;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 groundPosition;
out vec4 tint;
void main(){groundPosition=vertexPosition.xy;tint=vertexColor;gl_Position=mvp*vec4(vertexPosition,1.0);}
)GLSL",R"GLSL(#version 330
in vec2 groundPosition;
in vec4 tint;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main(){
 vec2 uv=fract(groundPosition/1536.0);
 // Blend shifted texture samples at repeat boundaries without reflecting the stone shapes.
 vec2 weight=smoothstep(vec2(0.0),vec2(.14),min(uv,1.0-uv));
 vec4 original=texture(texture0,uv);
 vec4 shiftX=texture(texture0,fract(uv+vec2(.5,0)));
 vec4 shiftY=texture(texture0,fract(uv+vec2(0,.5)));
 vec4 shiftXY=texture(texture0,fract(uv+vec2(.5,.5)));
 vec4 color=mix(mix(shiftXY,shiftY,weight.x),mix(shiftX,original,weight.x),weight.y);
 finalColor=color*tint*colDiffuse;
}
)GLSL");
 }
 if(mirror && material.id) BeginShaderMode(material);
 DrawTMXLayers(map,camera,viewport,layer,1,0,0,WHITE);
 if(mirror && material.id) EndShaderMode();
}
void DrawMapWithMaterials(const TmxMap* map,const Camera2D* camera,const Rectangle* viewport) {
 if(!map) return;
 for(uint32_t i=0;i<map->layersLength;++i) DrawMapLayerWithMaterial(map,camera,viewport,&map->layers[i]);
}

// Draw the bank after the lava surface so the irregular stone silhouettes cover every cut edge.
static void DrawRockShore(const TmxMap* map,float time,Rectangle viewport) {
 Texture2D stones{};
 for(uint32_t i=0;i<map->tilesetsLength;++i) {
  const auto& set=map->tilesets[i];
  if(set.name && !std::strcmp(set.name,"Basalt rim") && set.firstGid<map->gidsToTilesLength) {
   stones=map->gidsToTiles[set.firstGid].texture; break;
  }
 }
 if(!stones.id) return;
 static std::map<unsigned int,Rectangle> bounds;
 auto found=bounds.find(stones.id);
 if(found==bounds.end()) {
  Image image=LoadImageFromTexture(stones);
  Rectangle alpha=GetImageAlphaBorder(image,.12f); UnloadImage(image);
  found=bounds.emplace(stones.id,alpha).first;
  SetTextureFilter(stones,TEXTURE_FILTER_BILINEAR);
 }
 Rectangle strip=found->second;
 for(uint32_t i=0;i<map->layersLength;++i) {
  const auto& layer=map->layers[i];
  if(!layer.visible || layer.type!=LAYER_TYPE_OBJECT_GROUP || !layer.classString || std::strcmp(layer.classString,"shore")) continue;
  const auto& group=layer.exact.objectGroup;
  for(uint32_t j=0;j<group.objectsLength;++j) {
   const auto& o=group.objects[j];
   Rectangle area{(float)o.x-48,(float)o.y-48,(float)o.width+96,(float)o.height+96};
   if(!CheckCollisionRecs(area,viewport)) continue;
   bool horizontal=o.width>o.height;
   bool north=o.name && o.name[0]=='N',west=o.name && o.name[0]=='W';
   Vector2 center=horizontal?Vector2{(float)o.x+32,(float)o.y+(north?2.0f:8.0f)}:
                             Vector2{(float)o.x+(west?2.0f:8.0f),(float)o.y+32};
   int variation=((int)o.x/64*7+(int)o.y/64*11)%6;
   Rectangle source{strip.x+variation*strip.width/6,strip.y,strip.width/6,strip.height};
   if(Settings::Get().effects) {
    float pulse=.10f+.025f*std::sin(time*1.4f+o.x*.01f+o.y*.01f);
    DrawCircleGradient(center,52,Fade(ORANGE,pulse),Fade(ORANGE,0));
   }
   // Overlapping silhouettes close the stair-step corners; the ground footprint stays in the XML.
   DrawTexturePro(stones,source,{center.x,center.y,76,60},{38,30},horizontal?0.0f:90.0f,WHITE);
  }
 }
}

void DrawMapEffects(const TmxMap* map,float time,Rectangle viewport) {
 if(!map) return;
 if(!Settings::Get().effects) { DrawRockShore(map,time,viewport); return; }
 static Shader flow{};
 static int clock=-1;
 static bool attempted=false;
 if (!attempted) {
  attempted=true;
  const char* vertex=R"GLSL(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 mapPosition;
out vec4 tint;
void main(){ mapPosition=vertexPosition.xy; tint=vertexColor; gl_Position=mvp*vec4(vertexPosition,1.0); }
)GLSL";
  const char* fragment=R"GLSL(#version 330
in vec2 mapPosition;
in vec4 tint;
uniform float mapTime;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
void main(){
 vec2 p=mapPosition/1300.0+vec2(mapTime*.014,-mapTime*.010);
 p+=vec2(sin(p.y*6.0+mapTime*.42),sin(p.x*5.4-mapTime*.35))*.012;
 vec2 uv=1.0-abs(fract(p*.5)*2.0-1.0);
 vec4 lava=texture(texture0,clamp(uv,vec2(.001),vec2(.999)));
 float pulse=.98+.035*sin(mapTime*1.25+mapPosition.x*.003-mapPosition.y*.002);
 finalColor=vec4(lava.rgb*pulse,lava.a)*tint*colDiffuse;
}
)GLSL";
  flow=LoadShaderFromMemory(vertex,fragment);
  clock=GetShaderLocation(flow,"mapTime");
 }
 Texture2D lavaTexture{};
 for(uint32_t i=0;i<map->layersLength;++i) {
  const auto& layer=map->layers[i];
  if(layer.type==LAYER_TYPE_TILE_LAYER && layer.name && !std::strcmp(layer.name,"Magma bed")) {
   uint32_t gid=layer.exact.tileLayer.tiles[0];
   if(gid<map->gidsToTilesLength) lavaTexture=map->gidsToTiles[gid].texture;
   break;
  }
 }
 if (flow.id && clock>=0 && lavaTexture.id) {
  SetShaderValue(flow,clock,&time,SHADER_UNIFORM_FLOAT);
  BeginShaderMode(flow);
  for(uint32_t i=0;i<map->layersLength;++i) {
   const auto& layer=map->layers[i];
   if (!layer.visible || layer.type!=LAYER_TYPE_OBJECT_GROUP || !layer.classString || std::strcmp(layer.classString,"lava")) continue;
   const auto& group=layer.exact.objectGroup;
   for(uint32_t j=0;j<group.objectsLength;++j) {
    const auto& o=group.objects[j];
    if(o.type!=OBJECT_TYPE_RECTANGLE) continue;
    Rectangle r{(float)o.x,(float)o.y,(float)o.width,(float)o.height};
    if(CheckCollisionRecs(viewport,r)) DrawTexturePro(lavaTexture,{0,0,(float)lavaTexture.width,(float)lavaTexture.height},r,{0,0},0,WHITE);
   }
  }
  EndShaderMode();
 }
 DrawRockShore(map,time,viewport);
 for(uint32_t i=0;i<map->layersLength;++i) {
  const auto& layer=map->layers[i];
  if(!layer.visible || layer.type!=LAYER_TYPE_OBJECT_GROUP || !layer.classString) continue;
  bool smoke=std::strcmp(layer.classString,"vents")==0;
  if(!smoke && std::strcmp(layer.classString,"embers")!=0) continue;
  const auto& group=layer.exact.objectGroup;
  for(uint32_t j=0;j<group.objectsLength;++j) {
   const auto& emitter=group.objects[j];
   if(emitter.type!=OBJECT_TYPE_POINT) continue;
   Rectangle area{(float)emitter.x-120,(float)emitter.y-300,240,340};
   if(!CheckCollisionRecs(viewport,area)) continue;
   for(int spark=0;spark<(smoke?5:7);++spark) {
    float phase=std::fmod(time*(smoke?.19f:.45f)+j*.173f+spark*(smoke?.2f:1.0f/7),1.0f);
    float glow=std::sin(phase*PI);
    Vector2 point{(float)emitter.x+std::sin(phase*5+j)* (smoke?35:22),
                  (float)emitter.y-phase*(smoke?250:160)};
    if(smoke) {
     DrawCircleGradient(point,20+phase*62,Fade(Color{76,73,77,255},glow*.28f),Fade(Color{42,39,44,255},0));
    } else {
     DrawCircleGradient(point,9,Fade(ORANGE,glow*.26f),Fade(ORANGE,0));
     DrawLineEx({point.x-2,point.y+4},point,2,Fade(Color{255,188,77,255},glow*.9f));
    }
   }
  }
 }
}
