#include "map_effects.h"
#include "../ui/game_settings.h"
#include <cmath>
#include <cstring>
#include <map>
#include <algorithm>

float MapObjectGroundDepth(const TmxObject& object) {
 for(uint32_t i=0;i<object.propertiesLength;++i) {
  const auto& property=object.properties[i];
  if(property.name && !std::strcmp(property.name,"ground_y")) return property.floatValue;
 }
 return (float)object.y-28;
}

static bool ClassIs(const TmxLayer& layer,const char* name) {
 return layer.classString && !std::strcmp(layer.classString,name);
}
static bool IsArchitecture(const TmxLayer& layer) {
 return ClassIs(layer,"architecture_foundry") || ClassIs(layer,"architecture_desert");
}
static bool IsMarkings(const TmxLayer& layer) {
 return ClassIs(layer,"markings_foundry") || ClassIs(layer,"markings_desert");
}
// Walls are built from the same TMX rectangles as their collision, with restrained
// bevels and masonry rather than baked scenery that suggests false paths.
static void DrawArenaStructure(const TmxMap* map,const TmxLayer& layer,const Rectangle* viewport) {
 if(layer.type!=LAYER_TYPE_OBJECT_GROUP) return;
 bool steel=ClassIs(layer,"architecture_foundry"),markings=IsMarkings(layer);
 Texture2D wall{};Rectangle strip{};
 if(!markings) for(uint32_t i=0;i<map->tilesetsLength;++i) {
  const auto& set=map->tilesets[i];
  if(set.name && !std::strcmp(set.name,"Arena wall") && set.firstGid<map->gidsToTilesLength) {
   wall=map->gidsToTiles[set.firstGid].texture;break;
  }
 }
 if(wall.id) {
  static std::map<unsigned int,Rectangle> borders;
  auto found=borders.find(wall.id);
  if(found==borders.end()) {
   Image art=LoadImageFromTexture(wall);Rectangle ink=GetImageAlphaBorder(art,.15f);UnloadImage(art);
   found=borders.emplace(wall.id,ink).first;SetTextureFilter(wall,TEXTURE_FILTER_BILINEAR);
  }
  strip=found->second;
 }
 const auto& group=layer.exact.objectGroup;
 for(uint32_t i=0;i<group.objectsLength;++i) {
  const auto& o=group.objects[i];
  Rectangle r{(float)o.x,(float)o.y,(float)o.width,(float)o.height};
  if(viewport && !CheckCollisionRecs(r,*viewport)) continue;
  if(markings) {
   bool industrial=ClassIs(layer,"markings_foundry"),horizontal=r.width>r.height;
   if(industrial) {
    // Keep only the thin service-lane edge paint; no dark rectangular floor overlay.
    float span=horizontal?r.width:r.height;
    for(float t=12;t<span-24;t+=112) {
     if(horizontal) {
      DrawRectangle((int)(r.x+t),(int)r.y+12,54,5,{142,117,65,155});
      DrawRectangle((int)(r.x+t),(int)(r.y+r.height)-17,54,5,{142,117,65,155});
     } else {
      DrawRectangle((int)r.x+12,(int)(r.y+t),5,54,{142,117,65,130});
      DrawRectangle((int)(r.x+r.width)-17,(int)(r.y+t),5,54,{142,117,65,130});
     }
    }
   } else {
    // Dust is drawn as moving wisps, never as a filled rectangular lane.
    for(int j=0;j<22;++j) {
     float t=j/22.0f;
     Vector2 p{r.x+r.width*t,r.y+r.height*(.3f+.3f*std::sin(j*2.4f))};
     DrawLineEx(p,{p.x+32,p.y-3},2,Fade(Color{245,214,158,255},.2f));
    }
   }
   continue;
  }
  if(wall.id && strip.width>0) {
   bool horizontal=r.width>=r.height;float span=horizontal?r.width:r.height,thickness=horizontal?r.height:r.width;
   int pieces=std::max(1,(int)std::ceil(span/(thickness*5)));
   float length=span/pieces;
   for(int n=0;n<pieces;++n) {
    Vector2 center=horizontal?Vector2{r.x+(n+.5f)*length,r.y+r.height*.5f}:Vector2{r.x+r.width*.5f,r.y+(n+.5f)*length};
    DrawTexturePro(wall,strip,{center.x,center.y,length+1,thickness},{(length+1)*.5f,thickness*.5f},horizontal?0.0f:90.0f,WHITE);
   }
   continue;
  }
  Color edge=steel?Color{22,29,36,255}:Color{103,65,40,255};
  Color face=steel?Color{49,60,69,255}:Color{166,114,68,255};
  Color top=steel?Color{80,96,107,255}:Color{207,158,98,255};
  Color light=steel?Color{122,141,150,255}:Color{239,194,126,255};
  DrawRectangleRec(r,edge);
  Rectangle inset{r.x+5,r.y+5,std::max(0.0f,r.width-10),std::max(0.0f,r.height-10)};
  DrawRectangleRec(inset,face);
  DrawRectangleRec({r.x+8,r.y+7,std::max(0.0f,r.width-16),std::max(0.0f,r.height-24)},top);
  DrawLineEx({r.x+8,r.y+8},{r.x+r.width-8,r.y+8},3,light);
  if(steel) {
   bool horizontal=r.width>r.height;float span=horizontal?r.width:r.height;
   for(float t=32;t<span-16;t+=96) {
    Vector2 p=horizontal?Vector2{r.x+t,r.y+16}:Vector2{r.x+16,r.y+t};
    DrawCircleV(p,4,edge);DrawCircleV({p.x-1,p.y-1},2,light);
    if(horizontal) DrawLineEx({p.x+18,r.y+10},{p.x+18,r.y+r.height-12},2,Fade(edge,.7f));
    else DrawLineEx({r.x+10,p.y+18},{r.x+r.width-10,p.y+18},2,Fade(edge,.7f));
   }
  } else {
   for(float y=r.y+35;y<r.y+r.height-12;y+=38) {
    DrawLineEx({r.x+9,y},{r.x+r.width-9,y},2,Fade(edge,.65f));
    for(float x=r.x+35+((int)((y-r.y)/38)%2)*35;x<r.x+r.width-12;x+=80)
     DrawLineEx({x,y-25},{x,y},2,Fade(edge,.55f));
   }
  }
 }
}

bool IsMapGroundLayer(const TmxLayer& layer) {
 return ClassIs(layer,"ground") || ClassIs(layer,"ground_blend") || IsArchitecture(layer) || IsMarkings(layer);
}
void DrawMapLayerWithMaterial(const TmxMap* map,const Camera2D* camera,const Rectangle* viewport,const TmxLayer* layer) {
 if(!layer->visible) return;
 if(IsArchitecture(*layer) || IsMarkings(*layer)) { DrawArenaStructure(map,*layer,viewport);return; }
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
 // Discrete, map-authored ambience keeps the ground readable during combat.
 for(uint32_t i=0;i<map->layersLength;++i) {
  const auto& layer=map->layers[i];
  bool foundry=ClassIs(layer,"foundry_fx"),desert=ClassIs(layer,"desert_fx");
  if(!layer.visible || layer.type!=LAYER_TYPE_OBJECT_GROUP || (!foundry && !desert)) continue;
  const auto& group=layer.exact.objectGroup;
  for(uint32_t j=0;j<group.objectsLength;++j) {
   const auto& o=group.objects[j];Vector2 origin{(float)o.x,(float)o.y};
   Rectangle area{origin.x-320,origin.y-320,640,640};
   if(!CheckCollisionRecs(area,viewport)) continue;
   if(foundry) {
    float pulse=.18f+.035f*std::sin(time*2.1f+j);
    DrawCircleGradient(origin,105,Fade(Color{255,151,49,255},pulse),Fade(ORANGE,0));
    for(int n=0;n<6;++n) {
     float phase=std::fmod(time*.13f+n/6.0f+j*.17f,1.0f);
     Vector2 p{origin.x+std::sin(phase*5+j)*24,origin.y-35-phase*160};
     DrawCircleGradient(p,18+phase*45,Fade(Color{169,180,183,255},std::sin(phase*PI)*.22f),Fade(WHITE,0));
    }
    for(int n=0;n<4;++n) {
     float phase=std::fmod(time*.45f+n*.25f+j*.21f,1.0f);
     Vector2 p{origin.x+std::sin(n*3.1f)*phase*45,origin.y-phase*95};
     DrawLineEx({p.x,p.y+5},p,1.5f,Fade(Color{255,195,112,255},std::sin(phase*PI)*.65f));
    }
   } else {
    for(int n=0;n<8;++n) {
     float phase=std::fmod(time*.075f+n*.125f+j*.18f,1.0f);
     float fade=std::sin(phase*PI);
     Vector2 p{origin.x-260+phase*520,origin.y+std::sin(n*2.4f+phase*3)*100};
     DrawEllipse((int)p.x,(int)p.y,40+phase*35,6+phase*5,Fade(Color{246,213,158,255},fade*.08f));
     DrawLineEx(p,{p.x+10,p.y-2},1,Fade(Color{250,223,172,255},fade*.35f));
    }
   }
  }
 }
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
