# Magma Rift

Modular Tiled XML map: 75 x 50 cells at 64 world units. Open map.tmx in Tiled. It appears in online and offline map selection.

Basalt land, two lava pools, 16 spawn points and ten cover sprites are authored in tools/create_magma_rift_map.py. Lava masks and collision rectangles come from the same occupancy grid. Movement is swept through obstacles; the collision footprint uses visible sprite feet and stays grounded during jumps.

Basalt floor and cover sprites are new assets. See ART_NOTES.md, LAVA_ART.md and SHORE_ART.md for provenance and final prompts. No older game artwork is referenced by this map.

Layer classes: ground and ground_blend render terrain; decor sorts cover sprites; shore renders ledges and reflected heat; lava provides animated masks; embers and vents produce rising particles. Only a collision layer or class participates in world collision. The effects setting controls map animation while leaving terrain and collision unchanged.

Run python tools/create_magma_rift_map.py to regenerate the XML. This overwrites manual Tiled edits. Lava flow, shore glow, sparks and smoke are implemented in src/map_loader/map_effects.cpp.

Floor and lava materials use continuous blended sampling to avoid atlas cut seams. The flowing lava uses lava-flow.png; no procedural crack-web texture is used.

Basalt-rim sprites form the banks around the lava pools and outer lava boundary. Their placement comes from the same exposed grid edges as their short stone-bank collision footprints.
