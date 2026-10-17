#pragma once
#include <raylib.h>
#include <string>

class MapPreview {
public:
    ~MapPreview();
    void Prepare(const std::string& map,int width,int height);
    void Draw(Rectangle bounds,const Font& font) const;
private:
    RenderTexture2D target{};
    std::string mapName;
    bool available=false;
};
namespace SetupUi {
void Backdrop();
void Header(const Font& font,const char* eyebrow,const char* title,const char* subtitle);
void ModeChoice(const Font& font,Rectangle bounds,const char* title,const char* description,bool selected,float hover,bool enabled=true);
void Action(const Font& font,Rectangle bounds,const char* text,float hover,bool primary=true,bool enabled=true);
void MapArrows(Rectangle bounds,bool enabled,float previousHover,float nextHover);
}
