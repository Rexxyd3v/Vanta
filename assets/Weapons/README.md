# Weapon sprites

AssaultRifle/default.png and Sniper/default.png were created with the built-in image_gen tool using the existing shotgun sprite as a style reference. Both retain transparent backgrounds. The original generated files remain in the Codex generated-images folder.

Default art stays unpainted. Skin colors and patterns are applied by the shared weapon renderer, preserving each sprite's silhouette and alpha. All five weapons use the same hand-pivot, aiming, muzzle, projectile, and reload pipeline.


## Replacement models

All five guns now use assets/Weapons/<weapon>/default.png. SMG/default.png, Shotgun/default.png, and Pistol/default.png replace the original models. Every model has Default plus three runtime skin materials; the same model and alpha are used across variants to preserve grip and muzzle alignment. Skins are selected independently and saved in vanta-settings.cfg.

Generated with the built-in image_gen tool. AR and sniper sprites were the style references for the following prompts.

### SMG

Use case: stylized-concept. Asset type: one transparent side-view game weapon sprite for VANTA. Reference images are STYLE REFERENCES: match their clean readable angular white/light gray receiver panels, dark charcoal outlines and hardware, medium-gray shadow planes, restrained detail. Strict orthographic side view facing RIGHT with perfectly horizontal barrel, flat cartoon game art, no perspective or tilt. Only ONE weapon, large centered horizontal silhouette with transparent padding, actual transparent alpha background. No text, brands, hands, character, scenery, floor shadow, glow, or realistic texture. All visible pieces must be attached to the gun, no detached particles or debris. Muzzle at the far right edge. Create a compact tactical SUBMACHINE GUN: short vented handguard, straight compact barrel, short folding stock extending left, a small straight box magazine and angled pistol grip under rear receiver. Distinctly shorter and more compact than the assault rifle reference, while belonging to exactly the same art family.

### Shotgun

Use case: stylized-concept. Asset type: one transparent side-view game weapon sprite for VANTA. Reference images are STYLE REFERENCES: match their clean readable angular white/light gray receiver panels, dark charcoal outlines and hardware, medium-gray shadow planes, restrained detail. Strict orthographic side view facing RIGHT with perfectly horizontal barrel, flat cartoon game art, no perspective or tilt. Only ONE weapon, large centered horizontal silhouette with transparent padding, actual transparent alpha background. No text, brands, hands, character, scenery, floor shadow, glow, or realistic texture. All visible pieces must be attached to the gun, no detached particles or debris. Muzzle at the far right edge. Create a tactical PUMP SHOTGUN: long heavy barrel and parallel magazine tube, ribbed pump fore-end beneath the front barrel, angular shoulder stock to the left, simple pistol grip under rear receiver, small front bead sight. No scope or curved assault rifle magazine. Match the art family and panel treatment of the rifle reference.

### Pistol

Use case: stylized-concept. Asset type: one transparent side-view game weapon sprite for VANTA. Reference images are STYLE REFERENCES: match their clean readable angular white/light gray receiver panels, dark charcoal outlines and hardware, medium-gray shadow planes, restrained detail. Strict orthographic side view facing RIGHT with perfectly horizontal barrel, flat cartoon game art, no perspective or tilt. Only ONE weapon, large centered horizontal silhouette with transparent padding, actual transparent alpha background. No text, brands, hands, character, scenery, floor shadow, glow, or realistic texture. All visible pieces must be attached to the gun, no detached particles or debris. Muzzle at the far right edge. Create a compact tactical SEMIAUTOMATIC PISTOL: blocky light gray slide, short dark barrel pointing right, dark angled grip below left half, clear trigger guard and tiny sights. Very recognizable handgun silhouette, compact proportions. No stock, long barrel, extended magazine, scope, or attachments. Match the art family and panel treatment of the rifle reference.


## Generation prompts

### Assault rifle

Use case: stylized-concept. Asset type: a single transparent game weapon sprite for the existing VANTA 2D cartoon shooter. Reference image is STYLE reference only: simple flat white/light gray body with medium gray shadow shapes and black grip cutouts, clean flat vector-like cartoon art. Create ONE default ASSAULT RIFLE, clearly distinct from an SMG: angular shoulder stock on left, pistol grip beneath rear-center, curved magazine, rectangular receiver, vented handguard, straight barrel facing RIGHT. Strict perfect side view, zero perspective or tilt. Long horizontal silhouette centered on canvas with generous transparent margin. Use large simple readable shapes, 3 flat grayscale tones, NO gradients, realism, fine texture, glow, hands, character, background, text, logo, shadow outside weapon, or other objects. A compact stylized rifle suited to Mini Militia-like proportions. Actual transparent alpha background. Keep top barrel horizontal and muzzle at rightmost visible edge.

### Sniper rifle

Use case: stylized-concept. Asset type: a single transparent game weapon sprite for the existing VANTA 2D cartoon shooter. Reference image is STYLE reference only: simple flat white/light gray body with medium gray shadow shapes and black grip cutouts, clean flat vector-like cartoon art. Create ONE default SNIPER RIFLE: long slim barrel facing RIGHT, simple large readable scope centered above receiver, angular stock on left, pistol grip and small magazine below, heavy muzzle at far right. Strict perfect side view, zero perspective or tilt. Long horizontal silhouette centered on canvas with generous transparent margin. Use large simple readable shapes, 3 flat grayscale tones, NO gradients, realism, fine texture, glow, hands, character, background, text, logo, shadow outside weapon, or other objects. Stylized proportions consistent with the reference cartoon weapon. Actual transparent alpha background. Muzzle must be the rightmost visible edge.

## Added AK, double barrel and machine pistol

AKRifle/default.png, DoubleBarrel/default.png and MachinePistol/default.png were generated with imagegen using AssaultRifle/default.png as a style reference. Each has transparent alpha and an unpainted grayscale default. Three independent runtime skin materials per gun reuse the existing shader, loadout, save and network skin format.

Slots 6/7/8 select the AK rifle (30 rounds, 30 damage), double barrel (2 shells, seven 11-damage pellets per shell), and machine pistol (24 rounds, 9 damage). All use the existing reload, muzzle alignment, tracer and kill-feed systems. Double-barrel pellet spread is identical for local and remote shot rendering.

AK skins: Urban Camo, Ember, Frostbite. Double-barrel skins: Walnut, Rust Iron, Gold Rush. Machine-pistol skins: Neon Volt, Crimson, Ice Tech.
