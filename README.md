# GORPH

**Galactic Organic Robot Phalanx**

A native fixed-screen shooter in early-80s style, written in ANSI C89 with
SDL2 - presented in the look of a C64, complete with boot screen, raster
colors, multicolor sprites and demoscene effects.

The Gorphian phalanx attacks in four waves. Survive all four and the
campaign starts over - faster and more merciless.

## Missions

1. **Astro Battles** - alien formation behind shields
2. **Laser Attack** - gun squads with dive-bombing fighters
3. **Space Warp** - spiraling objects in a star tunnel
4. **Flag Ship** - the flagship, dismantled piece by piece

## Building

Requires SDL2 (`sdl2-config` in the path):

```
make
./gorph
```

## Controls

| Key              | Action              |
|------------------|---------------------|
| Arrows / WASD    | Move                |
| Space            | Fire                |
| F11              | Fullscreen          |
| Esc              | Quit                |

A gamepad is detected automatically.

Debug/cheat keys are off on a normal start. `./gorph -cheat` enables them:
1-4 mission 1-4, 5 restart title intro, 6 flagship kill, 8 level-8 outro,
9 restart boot screen, N next mission including rank, 0 in the credits: jump
close to the end.

## Features

- C64 boot screen with typewriter prompt, then a seamless zoom into the game
- Intro with starfield rotation, logo fly-in, speech and an orbiting robot
  with motion blur
- Attract mode: after a while a demo plays itself (Astro Battles and Space
  Warp in alternation)
- Lip-synced typewriter outros with speech samples
- Software VIC with 16 sprites, screen shake, TV glitch transitions
- Synthesizer sound effects plus sampled speech
- Amiga-demo credits (key C on the title screen): starfield morph, sine
  scroller with copper fill and floor mirror, lens flares, an embedded
  ProTracker MOD player, an orbiting demon and a finale in which a dropped
  rock shatters the mirror and a sponge arm wipes the screen clean

## Technology

Game logic, renderer (text and bitmap multicolor modes) and sound synthesis
are implemented from scratch; all assets are embedded in the source. No
foreign program code included.

## Tools

`tools/wiperec.c` records the sponge path for the credits finale:

```
make wiperec
./wiperec tools/wipe_bg.bmp
```

Hold the left mouse button to wipe, `R` restarts, `D` toggles the leftover
markers, `F` toggles fullscreen, `S` saves to `src/wipepath.h` and exits.
Rebuild afterwards and the game plays back the recorded path.
