#pragma once
#include "raytmx.h"
// Effects are authored as lava rectangles and vent/ember points in the TMX.
void DrawMapEffects(const TmxMap* map, float time, Rectangle viewport);

bool IsMapGroundLayer(const TmxLayer& layer);
void DrawMapLayerWithMaterial(const TmxMap* map,const Camera2D* camera,const Rectangle* viewport,const TmxLayer* layer);
void DrawMapWithMaterials(const TmxMap* map,const Camera2D* camera,const Rectangle* viewport);
