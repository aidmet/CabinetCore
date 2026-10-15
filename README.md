# CabinetCore

CabinetCore is a no-code arcade maker for Linux. You paint stages, draw sprites, place actors, and tune how they behave. When the game is ready, the editor writes a folder you can play: `game.dat` and `cabinet.elf`.

The demo, Neon Cabinet, opens the first time you start the editor. It has a run-and-gun rooftop, a vertical shooter with a boss, and a pellet maze. You can keep editing that demo or start a new game from the File menu.

## What you can make

Each stage picks its own physics:

- Platform
- Run-and-gun
- Vertical shooter
- Horizontal shooter
- Maze
- Top-down

Actors are the hero, enemies, guns, items, exits, bosses, and decorations. An actor can patrol, chase, hop, fly, bounce, dive, turret, or act as a maze ghost or a boss. You can also stack blocks on an actor: move, jump, shoot, wait, branch, play a sound, add score, change stage, and more.

Tiles can be solid, one-way, ladder, hazard, water, ice, destructible, or crumbling. A stage can scroll with the hero, stay locked, or autoscroll. It ends when the player touches an exit, reaches the end of an autoscroller, collects every pellet, or defeats the boss. An intermission stage shows a message until Start.

The picture is three layers. Backdrop scrolls slower than the action. Collision is what the player stands on. Foreground draws in front.

Sounds can be the built-in jump, shoot, and coin tones, a new tone you design, or a WAV you import. Sprites and tiles are drawn in the pixel painter. The Game page sets the title, lives, health, view size, CRT scanlines, screen shake, and free play.

Free play is on by default, so Start begins a game immediately. Turn it off for a coin-op cabinet. The 5 key inserts a credit, and Start spends that credit.

High scores are saved in `scores.dat` next to `game.dat`.

## Run CabinetCore

CabinetCore needs SDL2 to open a window. The setup script and the launcher both check for it.

### First time

From the project folder:

```bash
sh setup.sh
```

`setup.sh` checks for CMake, a C++ compiler, and SDL2. If something is missing, it prints one install command and stops. On Debian or Ubuntu that command looks like:

```bash
sudo apt-get install cmake g++ libsdl2-2.0-0
```

Run the command it prints, then run `sh setup.sh` again. The script builds the project and opens the editor.

The build writes:

- `build/cabinetcore` — the editor
- `build/cabinet.elf` — the player that runs a finished game
- `build/cabinet-plugin` — the tool that scaffolds a plugin

SDL2 itself is not included. The headers used to compile live in `third_party/`. The shared library comes from the system.

### When the project is already built

```bash
sh cabinetcore.sh
```

The launcher checks for SDL2 again. If it is missing, it prints the install command and does not open the editor. If it is there, the editor starts. Use this when you built with `setup.sh` or with CMake yourself.

You can also build without the setup script, then start the launcher:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sh cabinetcore.sh
```

## Use the editor

The window is four panels. The left panel is Stages, Tiles, Actors, Sprites, Sounds, Build, and Game. The middle is the stage. The right panel is the inspector for the stage, the selected tile, or the selected actor. The bottom panel is the pixel painter when a sprite or tile is selected.

Drag the edges between the panels to resize them. View, Save layout keeps those sizes for the next time the editor opens. View, Reset layout puts them back.

Paint with the left mouse button. Right click picks a tile. The tools are Select, Paint, Erase, Fill, Rect, and Stamp, on keys 1 through 6. Stamp places the actor selected under Actors. Choose Backdrop, Collision, or Foreground before you paint. Ctrl and the mouse wheel zoom the stage from 1 to 6. Ctrl+Z undoes and Ctrl+Y redoes.

| Action | How |
| --- | --- |
| New game | File, New, then a physics type or the full demo |
| Open | Ctrl+O, and choose a `.ccproj` |
| Save | Ctrl+S. The editor reopens that project next time |
| Playtest | F5. Button 2 (X or Ctrl) returns to the title |
| Build | F6. Writes `dist/game.dat` and `dist/cabinet.elf` next to the project |
| Hitboxes | F3, during a playtest |

A `.ccproj` file is the same package as `game.dat`. Saving keeps an editable project. Building copies the player beside the game data so that folder can be played on its own.

Play the demo as a finished game without opening the editor:

```bash
sh cabinetcore.sh --export-demo dist
./dist/cabinet.elf
```

If `game.dat` sits beside `cabinet.elf`, the player loads it with no arguments. The player needs SDL2 as well.

## Play controls

| Cabinet control | What it does |
| --- | --- |
| Joystick | Move, and pick high-score letters |
| Button 1 | Jump, confirm, restart while paused |
| Button 2 | Shoot, return to the title while paused |
| Start | Begin, continue, and pause |

On a keyboard the joystick is the arrows or WASD, button 1 is Z or Space, button 2 is X or Ctrl, and Start is Enter. A gamepad uses the stick, A, B, and Start.

In a shooter, the ship flies in eight directions and fires upward (vertical) or forward (horizontal). Hold a direction while shooting in a platform stage to aim.

## Plugins

A plugin is a small C++ add-on packed as a `.ccplugin`. There are three kinds:

- A **block** shows up in an actor's block list and runs during the game.
- A **tool** is armed from the Build page, then used by clicking the stage.
- A **generator** builds or changes the current stage from the Build page.

Create one with:

```bash
./build/cabinet-plugin
```

It asks for a name, a type (`block`, `tool`, or `generator`), an author, a short summary, and the label shown in the editor. It writes a folder with an SDK and `src/plugin.cpp`. Edit that file, then run `./build` inside the folder. That produces `Name.ccplugin`.

In the editor, choose File, Load plugin, or drop the `.ccplugin` onto the window. Loaded plugins are remembered. A built game copies them into `dist/plugins/`, and `cabinet.elf` loads that folder when it starts.

## License

CabinetCore is released under the MIT license. See `LICENSE`. Dear ImGui, in `third_party/imgui`, keeps its own MIT license.
