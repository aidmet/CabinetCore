# CabinetCore

CabinetCore is a no-code arcade maker. You paint stages, place actors, tune behavior, then build a folder that contains `game.dat` and `cabinet.elf`.

It covers the Arcade Game Studio set — platformers, run-and-gun, vertical and horizontal shooters, and mazes — and adds direct painting, layers, undo, a pixel editor, in-editor playtest, gamepad play, CRT scanlines, and high scores.

## Build

SDL2 is already used from the system library. Headers live in `third_party/`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

That produces:

- `build/cabinetcore` — the editor
- `build/cabinet.elf` — the game runtime

`cabinet.elf` is the Linux launcher that sits next to the game data. Run it with `./cabinet.elf`.

## Make a game

```bash
./build/cabinetcore
```

The demo "Neon Cabinet" opens immediately. It has a run-and-gun rooftop, a vertical shooter with a boss, and a pellet maze.

- Left drag paints the collision layer. Right click picks a tile.
- Tools: Select, Paint, Erase, Fill, Rect, Stamp. Keys 1–6.
- Backdrop and foreground are separate layers. Backdrop scrolls slower in game.
- Stamp places the actor selected under Actors.
- F5 playtests. F6 builds `dist/game.dat` and `dist/cabinet.elf` next to the project.
- Ctrl+S saves a `.ccproj`. It is the same package format as `game.dat`.

Play the build:

```bash
./build/cabinetcore --export-demo dist
./dist/cabinet.elf
```

If `game.dat` sits beside `cabinet.elf`, the runtime loads it with no arguments.

## Play controls

| Cabinet control | What it does |
| --- | --- |
| Joystick | Move, and pick high-score letters |
| Button 1 | Jump, confirm, restart while paused |
| Button 2 | Shoot, return to the title while paused |
| Start | Begin, continue, and pause |

On a keyboard the joystick is the arrows or WASD, button 1 is Z or Space, button 2 is X or Ctrl, and Start is Enter. A gamepad uses the stick, A, B, and Start.

In a shooter, the ship flies in eight directions and fires upward (vertical) or forward (horizontal). Hold a direction while shooting in a platform stage to aim.

## What a stage can do

Physics is per stage: platform, run-and-gun, vertical shooter, horizontal shooter, maze, or top-down.

Actors are the Arcade Game Studio set: hero, gun, enemy, item, check (exit), boss, plus decoration. Behaviors include patrol, chase, turret, sine, dive, hopper, bounce, fly, maze ghost, and boss. Tiles can be solid, one-way, ladder, hazard, water, ice, or destructible.

A stage ends when the player touches an exit, reaches the end of an autoscroller, collects every pellet, or defeats the boss. Intermission stages show a message until Start.

High scores are written to `scores.dat` next to `game.dat`.
