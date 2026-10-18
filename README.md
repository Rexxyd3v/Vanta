<div align="center">

# VANTA

**Aim. Move. Survive.**

A 2D arena shooter by **R Gamestudio** — built with C++17, raylib, and ENet.

[Download v1.0.0](https://github.com/Rexxyd3v/Vanta/releases/download/v1.0.0/VANTA-1.0.0-Setup.exe) · [Gameplay](#gameplay) · [Game modes](#game-modes) · [Controls](#controls) · [Build & run](#build--run)

</div>

## Download & play

**[Download VANTA 1.0.0 for Windows](https://github.com/Rexxyd3v/Vanta/releases/download/v1.0.0/VANTA-1.0.0-Setup.exe)**

Run `VANTA-1.0.0-Setup.exe`, install, and launch **VANTA** from the Start menu. The single installer includes the game, assets, maps, and runtime DLLs. No ZIP extraction or development tools are needed. A desktop shortcut is optional, and the default installation does not require administrator access.

Requires **64-bit Windows 10/11** and **OpenGL 3.3-capable graphics**. See the [release page](https://github.com/Rexxyd3v/Vanta/releases/tag/v1.0.0) for checksums and release notes.

## Gameplay

<!-- GAMEPLAY_VIDEO_START -->
[![VANTA riot gameplay — open the highlight video](docs/media/gameplay-preview.png)](docs/media/vanta-riot-highlights.mp4)

[Watch / download the gameplay highlights](docs/media/vanta-riot-highlights.mp4)
<!-- GAMEPLAY_VIDEO_END -->

A 66-second highlight featuring the game's original **R Gamestudio → raylib** intro, followed by a 12-fighter offline free-for-all across all three maps. Includes walking, jumps, dashes, grenade blasts, poison smoke, flashbangs, changing camera focus, and zoom transitions. Captured with the native game renderer at 1.12× playback speed, with music and combat audio.

<!-- For GitHub's inline video player, upload docs/media/vanta-riot-highlights.mp4 through GitHub's Markdown editor, then replace the block above with the generated https://github.com/user-attachments/assets/... URL on its own line. See docs/README_VIDEO.md. -->

## Features

- **Offline and online combat:** practice against configurable bots or host and join multiplayer rooms.
- **Movement and defense:** directional dash trails, jumps, and an orbiting shield.
- **Eight guns with skins:** SMG, shotgun, pistol, assault rifle, sniper rifle, AK-style rifle, double-barrel shotgun, and machine pistol. Every gun has its default finish and three alternate skins.
- **Three saved bags:** equip a primary, secondary, melee weapon, frag grenade, poison smoke, and flashbang. Change bags at a base or after respawning.
- **Combat effects:** gray explosive smoke, light flashes, ground cracks, proximity screen shake, and spreading green poison smoke that can damage its thrower.
- **Match HUD:** health, ammo, reload progress, ability cooldowns, equipment slots, and a weapon-icon kill feed.
- **Proximity voice chat:** hold the push-to-talk key during online play.

## Game modes

| Mode | Objective | Respawns |
| --- | --- | --- |
| Training | Warm up without a score limit | Unlimited |
| Free for All | Reach the individual kill target | Random safe arena spawns |
| Team Deathmatch | Reach the team kill target | Team-base respawns |
| Elimination | Eliminate the opposing team | One life per round |
| Deathmatch / Flag | Carry your team's flag to the enemy base; captures win | Team-base respawns |

Offline setup supports **up to 15 bots**, with **Easy**, **Medium**, and **Difficult** AI. Training can also run without bots.

### FFA special pickups (development build)

The published v1.0.0 installer predates these changes; they are available in the current source build. Free for All uses randomized clear respawn positions, without bases. **Only offline and online FFA** spawn special pickups; Training, TDM, Elimination, and Flag do not.

Contest six pickup locations across every map: **flamethrower, grenade launcher, bazooka, freeze bomb, shock grenade, and incendiary bomb**. Press the interact key (default **F**) near a ready pickup. Guns have limited ammunition and return after 28 seconds; throwable supplies return after 22 seconds. Pickups are lost on death and cannot be added to saved bags.

Use **7** to re-equip a collected special gun, **1–6** to return to bag equipment, and **V** to throw a collected special bomb. Freeze locks movement, jumping, and dashing for 2.5 seconds with a visible ice shell, shock interrupts dashes and briefly disrupts vision, and incendiary bombs leave damaging fire. Explosive and area weapons can also affect their thrower. FFA bag changes are available briefly after respawning, instead of at a base. Online pickup claims and respawn timers are owned by the host; all players need the revised game build.

## Maps

| Arena | Setting |
| --- | --- |
| **Magma Rift** | Basalt cover, stone-edged lava pools, molten effects, and drifting embers. The default arena. |
| **Abandoned Foundry** | Industrial lanes, steel barricades, pipe racks, generators, steam, and sparks. |
| **Desert Outpost** | Sandstone terrain, rock cover, sandbags, vehicles, and windblown dust. |

Maps use editable **Tiled TMX/XML** files in [`assets/Maps`](assets/Maps), with separate visual, collision, spawn, and effect layers.

## Controls

These are the default bindings. Movement, abilities, interaction, and push-to-talk can be rebound in **Settings / F10**.

| Action | Key / input |
| --- | --- |
| Move | `W A S D` or arrow keys |
| Aim / fire | Mouse / left mouse button |
| Jump | `Space` |
| Dash | `Left Shift` |
| Shield | `E` |
| Reload | `R` |
| Primary / secondary / melee | `1` / `2` / `3` |
| Select frag / poison smoke / flashbang | `4` / `5` / `6` |
| Throw frag / poison smoke / flashbang | `G` / `Q` / `T` |
| Flag interaction | `F` |
| Push to talk | Hold `C` |
| Open bag selection | `B` |
| Choose Bag 1 / 2 / 3 | `F1` / `F2` / `F3` |
| Open storage from the setup menus / lobby | `L` |
| Scoreboard | Hold `Tab` |
| Settings | `F10` |
| Pause / back | `Esc` |

Bag changes are restricted to bases and respawns. Changing a bag does not refill ammunition or reset grenade cooldowns. Loadouts, skins, audio settings, and bindings are saved locally in `vanta-settings.cfg`.

## Build & run

The current build targets **64-bit Windows**. Use the **MSYS2 UCRT64** terminal with GCC, CMake, Ninja, raylib, ENet, Opus, and miniaudio installed.

```sh
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-raylib \
  mingw-w64-ucrt-x86_64-enet \
  mingw-w64-ucrt-x86_64-opus \
  mingw-w64-ucrt-x86_64-miniaudio
```

From the repository root:

```sh
cmake -S . -B bin/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build bin/build -j 4
./bin/build/Vanta.exe
```

Run the executable **from the repository root** so the relative `assets/` paths resolve. If distributing a build, keep the `assets/` directory and required runtime DLLs alongside the executable.

### Offline

Choose **Play Offline**, select a mode and map, set the bot count and difficulty, configure your bags, and deploy.

### Online

One player hosts a room from **Play Online**. Other players join using the host's address. The host chooses the map and mode; players ready up before the match starts. For a local network game, use the LAN address shown in the lobby. Internet connections require the host's game port to be reachable; there is no automatic public matchmaking service.

### Tests

```sh
ctest --test-dir bin/build --output-on-failure
```

The test targets cover match rules and game smoke checks, including loadouts, equipment, map collision, and combat behavior.

## Project layout

```text
assets/       Maps, characters, weapons, equipment, fonts, and audio
src/          Game screens, entities, rendering, networking, voice, and UI
tests/        Match-rule tests and game smoke checks
tools/        Map authoring scripts and native gameplay video capture
docs/media/   README gameplay video and preview
```

## Credits

Created by **R Gamestudio / [@rjltyd](https://github.com/Rexxyd3v)**.

Built with [raylib](https://www.raylib.com/), [ENet](http://enet.bespin.org/), [Opus](https://opus-codec.org/), [miniaudio](https://miniaud.io/), and [raytmx](src/map_loader/raytmx.h). Maps use the [Tiled](https://www.mapeditor.org/) format.

### Finite ammunition (development build)

All standard guns use finite magazines and reserve ammunition in offline and online matches. Reloading transfers only the missing rounds from reserve; switching weapons, skins, or bags does not refill them. Respawning restores the starting supply. The HUD shows magazine / reserve and warns when the reserve or the entire supply is empty. FFA special pickups retain their own limited ammunition.

| Gun | Magazine | Reserve |
| --- | ---: | ---: |
| SMG | 30 | 180 |
| Shotgun | 6 | 40 |
| Pistol | 12 | 96 |
| Assault rifle | 30 | 150 |
| Sniper rifle | 5 | 30 |
| AK rifle | 30 | 150 |
| Double barrel | 2 | 30 |
| Machine pistol | 24 | 168 |

### Combat feedback and match reports (development build)

Weapons have distinct recoil and recovery, ejected brass, and swept wall impacts with stone dust or metal sparks. Hits and kills have separate visual and sound confirmations. Combat audio uses stereo positioning and distance falloff for gunfire, footsteps, landings, reloads, throws and explosions; the SFX slider controls these sounds.

When eliminated, the death recap shows the killer, weapon, and damage received that life. Use **Left / Right** to spectate living teammates in team modes or surviving opponents in FFA. Normal respawn rules still apply.

After a 3.2-second outcome and score-tally transition, the match report fades in, stays open and shows the MVP, kills, deaths, shot accuracy, actual damage dealt, and flag captures. Accuracy counts a shotgun blast only once even when several pellets hit. Online hit feedback and damage statistics use victim confirmations, so blocked damage does not earn credit. Use the mouse wheel for larger leaderboards. **R** starts a rematch offline or as the online host; **Enter** returns to setup/lobby. Online clients can leave with **Enter** or wait for the host's next match. Rematches refill ammunition, clear temporary effects, and reset match stats while preserving the selected map, mode and loadout.

Player weapons use separate hands anchored to their trigger grips and foregrips, including mirrored aim directions and special weapons. The support hand moves toward the magazine during reloads. Melee weapons and throwables use their own holding poses.

### Movement, reloads and atmosphere (development build)

Players lean smoothly into horizontal movement while their collision footprint stays fixed. Jump shadows shrink with height, and landings kick up short dust bursts matched to the arena. These effects also follow online players.

Reloads visibly remove and insert magazines on SMGs, pistols and rifles. Shotguns feed shells and cycle their pump; the double barrel opens its breech to eject and load shells. Hand contacts follow the moving components, and online reload progress is synchronized. Ammo still transfers only when the reload finishes.

Magma pools bubble and pop, foundry vents emit drifting steam and intermittent sparks, and desert gusts carry curved dust wisps and sand particles. Atmosphere uses map-authored emitters and respects the Effects setting.


### Bot tactics, victory lineup and combat medals (development build)

- Offline bots choose reachable cover when reloading or under pressure, peek back out, check lines of sight before firing, and steer away from incoming grenades, poison clouds and incendiary fire. Medium and difficult bots can dash out of danger. An exhausted primary switches to the pistol; respawning restores the original primary.
- After the end-of-match score tally, the winning squad poses with its actual characters, weapons and skins before the report appears. FFA shows the top three by kills, with the champion in front. Team draws do not present a false winning squad.
- Offline and online combat award animated, audible double-kill, triple-kill, revenge and shutdown medals. Rapid kills chain within four seconds of the previous kill; shutdown ends a living streak of at least three. Suicides and team kills never earn medals, and rematches clear the tracker.


Offline Flag bots coordinate a single runner, spaced escorts, home defense and interception instead of converging on one objective. Roles adapt to the carrier, dropped flags, approaching enemies and available teammates. Cached map routes guide them around obstacles, with navigation aligned to each character's ground footprint and local spacing between allies.
