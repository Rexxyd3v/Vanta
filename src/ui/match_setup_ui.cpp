#include "../map_loader/map_effects.h"
#include "match_setup_ui.h"
#include "ui_theme.h"
#include "../constants.h"
#include "../map_loader/MapRegistry.h"
#include <algorithm>

MapPreview::~MapPreview() { if (target.id) UnloadRenderTexture(target); }
void MapPreview::Prepare(const std::string& name,int width,int height) {
    if (target.id && mapName==name && target.texture.width==width && target.texture.height==height) return;
    if (target.id) UnloadRenderTexture(target);
    target=LoadRenderTexture(width,height); mapName=name;
    MapData* map=MapRegistry::GetInstance().GetMap(name);
    available=map && map->tmxMap && map->pixelWidth>0 && map->pixelHeight>0;
    BeginTextureMode(target); ClearBackground({16,28,37,255});
    if (available) {
        Camera2D camera{};
        camera.zoom=std::max((float)width/map->pixelWidth,(float)height/map->pixelHeight);
        camera.target={map->pixelWidth*0.5f,map->pixelHeight*0.5f}; camera.offset={width*0.5f,height*0.5f};
        Rectangle viewport{0,0,(float)map->pixelWidth,(float)map->pixelHeight};
        BeginMode2D(camera); DrawMapWithMaterials(map->tmxMap,&camera,&viewport); DrawMapEffects(map->tmxMap,0,viewport); EndMode2D();
    }
    EndTextureMode();
}
void MapPreview::Draw(Rectangle r,const Font& font) const {
    if (target.id) DrawTexturePro(target.texture,{0,0,(float)target.texture.width,-(float)target.texture.height},r,{0,0},0,WHITE);
    DrawRectangleGradientV((int)r.x,(int)(r.y+r.height-64),(int)r.width,64,{5,12,20,0},{5,12,20,240});
    DrawTextEx(font,mapName.c_str(),{r.x+18,r.y+r.height-29},18,1,UiTheme::TextPrimary());
    DrawLineEx({r.x,r.y+r.height},{r.x+r.width,r.y+r.height},1,Fade(UiTheme::AccentGold(),0.5f));
    if (!available) DrawText("PREVIEW UNAVAILABLE",(int)r.x+20,(int)r.y+25,14,UiTheme::TextMuted());
}
namespace SetupUi {
void Backdrop() { DrawRectangleGradientH(0,0,VIRTUAL_WIDTH,VIRTUAL_HEIGHT,{7,14,23,246},{7,14,23,213}); }
void Header(const Font& font,const char* eyebrow,const char* title,const char* subtitle) {
    DrawTextEx(font,eyebrow,{76,48},14,1,UiTheme::AccentGold());
    DrawTextEx(font,title,{76,78},36,1,UiTheme::TextPrimary());
    DrawTextEx(font,subtitle,{76,128},13,1,{144,164,182,255});
    DrawLineEx({598,148},{598,628},1,Fade(UiTheme::TextMuted(),0.17f));
    DrawTextEx(font,"MATCH SETUP",{646,86},21,1,UiTheme::AccentGold());
    DrawTextEx(font,"MAP",{646,123},11,1,{144,164,182,255});
}
void ModeChoice(const Font& font,Rectangle r,const char* title,const char* description,bool selected,float hover,bool enabled) {
    Color gold=UiTheme::AccentGold(),muted{144,164,182,255};
    if (selected || hover>0) DrawRectangleRounded(r,0.22f,10,Fade(gold,selected?0.12f:hover*0.06f));
    DrawCircle((int)r.x+18,(int)r.y+26,selected?5:3,selected?gold:Fade(muted,0.4f));
    DrawTextEx(font,title,{r.x+40+hover*4,r.y+10},19,1,selected?gold:enabled?UiTheme::TextPrimary():muted);
    DrawTextEx(font,description,{r.x+40,r.y+39},11,1,muted);
    if (selected) DrawLineEx({r.x+40,r.y+r.height},{r.x+r.width-22,r.y+r.height},1,Fade(gold,0.4f));
}
void Action(const Font& font,Rectangle r,const char* text,float hover,bool primary,bool enabled) {
    Color accent=primary?UiTheme::AccentGold():UiTheme::SkyAccent();
    DrawRectangleRounded(r,0.6f,12,enabled?Fade(accent,(primary?0.13f:0.04f)+hover*0.15f):Color{25,31,39,160});
    float size=primary?23.0f:16.0f; Vector2 measured=MeasureTextEx(font,text,size,1);
    if (measured.x>r.width-36) { size*=(r.width-36)/measured.x; measured=MeasureTextEx(font,text,size,1); }
    DrawTextEx(font,text,{r.x+(r.width-measured.x)*0.5f+hover*3,r.y+(r.height-measured.y)*0.5f},size,1,
        enabled?primary?accent:UiTheme::TextPrimary():Fade(UiTheme::TextMuted(),0.4f));
}
void MapArrows(Rectangle r,bool enabled,float previousHover,float nextHover) {
    for (int direction=0;direction<2;++direction) {
        float x=direction==0?r.x+19:r.x+r.width-19,y=r.y+r.height*0.5f,sign=direction==0?-1.0f:1.0f;
        float hover=direction==0?previousHover:nextHover;
        Color color=enabled?UiTheme::TextPrimary():UiTheme::TextMuted();
        DrawCircleV({x,y},16,Color{7,14,23,(unsigned char)(170+hover*60)});
        DrawLineEx({x-sign*3,y-6},{x+sign*3,y},2,color);
        DrawLineEx({x+sign*3,y},{x-sign*3,y+6},2,color);
    }
}
}
