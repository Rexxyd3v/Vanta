# DesertOutpost

Original modular 4800 x 3200 arena, matching MagmaRift's faceted game-art direction.
Floor and four isolated props were generated with imagegen using the MagmaRift floor as a style reference.
Layout, modular wall strips, floor markings and effects are authored separately. Nothing is painted over a single map image.

Edit map.tmx in Tiled; rerun tools/create_outpost_maps.py to regenerate the authored layout.
The collision layer shares exact wall rectangles and alpha-fitted prop feet with the visuals. Each cover sprite carries ground_y so transparent atlas padding never affects depth ordering.
Sixteen checked spawn points and a continuous center delivery lane support FFA, TDM, elimination and flag mode.
Effect emitters use class desert_fx and respect the Effects setting.
