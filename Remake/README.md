# Championship Boxing remake engine (C + SDL3)

A cross-platform engine that plays Sierra's *Championship Boxing* (1984, IBM PC
CGA) using **your own copy of the original game files**. Everything is read at
startup from your game files, and those files are never written to.

Two releases of the game are supported:

* **SierraCh** (`KO.EXE`, `*.OLY`, `*.PIX`, `*.SPT`, one `.DAT` per boxer).
  This is the preferred one: it is complete, and has the factory roster.
* **The 1987 booter conversion** (`BOXING.OVR`, `BOXING.PI3`, `BOXING.BR`…).

How the original works is documented in `../docs/ANALYSIS.md` (files and
graphics) and `../docs/GAMEPLAY.md` (game logic).

## Building

You need CMake 3.16+, a C11 compiler and SDL 3.2+.

```sh
# macOS
brew install sdl3 cmake
# Debian / Ubuntu (24.10+)
sudo apt install libsdl3-dev cmake
# Windows: vcpkg install sdl3, or let CMake fetch it (below)

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

No SDL3 package? Add `-DCB_FETCH_SDL=ON` and CMake downloads and builds it.

## Running

```sh
./build/cbremake --game-dir ../Original_IBM_CGA
```

The game folder is found from, in order: `--game-dir`, the `CB_GAME_DIR`
environment variable, the current folder, or an `Original_IBM_CGA` folder next
to the current folder or the program. Each of those may be a game folder itself
or hold release folders under any name (`SierraCh/`, `ArchiveOrg/`,
`1987_Crack/`…). Releases are recognised by their files (`KO.EXE` and
`RING.PIX` for SierraCh, `BOXING.PIC` for the 1987 conversion), and SierraCh
is preferred. File names may be upper or lower case.

Other options:

| Option | What it does |
|---|---|
| `--scale N` | Window size multiplier (default 3). The window is resizable; F11 or Alt+Enter toggles full screen |
| `--selftest` | No window: decodes every file, prints what it found, and runs a computer-vs-computer fight |
| `--shots DIR` | Walks through every screen with scripted keys and saves a BMP of each to `DIR` |

### Missing files

Copies differ. If a boxer file is missing (the 1987 conversion lacks
`BOXING.WR`, the White boxer's sprites for the left side of the ring), the
engine uses another character's file for that side and says so at the start of
the fight. SierraCh has the real art (`WBXR0.SPT`); copying it into the 1987
folder as `BOXING.WR` also works. Missing
screens show black, and the error screen lists what could not be found if the
essentials (the arena, a boxer and the roster) are absent.

## Controls

Menus work like the original: type the number of your choice (and Enter for
numbers like boxer slots).

* **Title**: 1 Gym, 2 Fight, 3 Options, 4 Exit.
* **Tale of the Tape**: 1–12 change a boxer's age, weight, reach, height,
  fights or losses for this fight, 13 the number of rounds, 14 the round
  length, 15 the mode, 16 players and side, 17 new boxers, 18 back to the
  title, 19 begin the fight.
* **Corner** between rounds: in Strategy, Q W E A S D X C as listed on screen
  (Space skips). In Arcade, before every round: O shows the keys, any other
  key fights.
* **Arcade fighting**, when a boxer is human-controlled (mode Arcade, and
  players 1 with a side, or 2). The original's own keys are F1–F10 for the
  left boxer and the keypad for the right one; the remake has those plus
  letters for the left boxer:

| Action | Left boxer | Original left key | Right boxer |
|---|---|---|---|
| Jab | Q | F3 | Keypad 7 |
| Head punch | W | F4 | Keypad 8 / Up |
| Uppercut | E | F6 | Keypad 9 |
| Body punch | S | F5 | Keypad 5 |
| Duck | Z | F9 or F10 | Keypad 2 / Down |
| Cover up | X | F8 | Keypad 1 |
| Guard | C | F7 | Keypad 3 |
| Step forward | D | F2 | Keypad 4 / Left |
| Step back | A | F1 | Keypad 6 / Right |

  On a Mac the F keys may need the Fn key. The original's right-boxer keypad
  layout is remappable in its corner screen; the remake's is fixed.

Esc leaves the current screen.

## What is in place

* Every graphics format decoded in C: RLE full screens, opaque, head and
  masked sprites (`src/cga.c`).
* The boxers are drawn by a port of the original's own draw routine
  (`src/anim.c`): its sprite tables and its 27 animation sequences per side
  are read from your `BOXING.OVR` and fight overlay at startup, and the frames
  are drawn into a copy of CGA video memory exactly as the original does,
  including how it repaints the ring behind a boxer.
* The ring-girl intro the first time you choose Fight (when Options item 5,
  the intro, is on): the credits, the girl walking the ring with her sign and
  the intro tune, all read from your files. Any key skips it.
* The window and Dock icon is the intro frame (the girl with the SIERRA
  BOXING sign), drawn at startup from your own `GIRL.SPT`; nothing of the
  original is stored in the remake.
* Title, Gym (roster and each boxer's statistics, read-only), Options,
  boxer selection, the Tale of the Tape with the original's odds and edits,
  the Arcade corner prompt, the introductions (touching gloves in round 1,
  walking out in the last round), the fight with names and running scores on
  the scoreboard, the end-of-round statistics with the judges' cards turned
  over one by one, corner advice, and the end of the fight as the original shows it (the
  winner announced in the ring with his glove raised and flowers thrown, the
  end-of-fight statistics and the Another fight / Rematch / Quit or Start
  over questions). Simulation mode (Tale of the Tape item 15) runs the whole
  fight with no corners.
* Sound (`src/audio.c`): the bell (ring, PCjr chord or beep, by the Sound and
  Bell options), the count, punches landing, the glove tap, a man hitting the
  canvas, the stoppage buzzer and the wrong-key beep, synthesised the way the
  original drives the PC speaker.
* The game rules, ported from the original code (see `src/fight.h` and
  `docs/GAMEPLAY.md` section 7): ratings from the Gym answers and the
  weight and reach adjustments, the round loop itself (the computer's choices,
  when a punch lands, blocks, staggers, hurt, knock-downs, damage, injuries
  and stoppages, the count and getting up, the clock and the bell), corner
  advice, energy between rounds, three-judge scoring (ten-point must or
  rounds), and even the original's random number generator, which reads
  bytes of the fight overlay from your files. The round is driven by the
  original's own animation sequences, as the original was.
* Options from your `BOXING.OV1` are used as the defaults. Changes last for
  the session only.

## Still to do

* The Arcade fighting style (corner key R) and the punch-sequence variants it
  selects; everyone fights as a Slugger, the default.
* The speed: the original has no timer. The remake runs 12.75 frames a
  second (both boxers together), an estimate for a 4.77 MHz PC that also
  makes a round last its nominal length. DOSBox at its default speed runs the
  original about 2.3 times faster.
* Gym editing (create / change / eliminate boxers) saved to the remake's own
  save folder, never to the original files.
* The Arcade options behind the corner's O (key remapping, style, handicap),
  the PCjr crowd roar after a fight, and crowd and referee sprites from
  `FG3`.

## Source layout

| File | Purpose |
|---|---|
| `src/cga.c` | CGA screen and sprite decoders, software framebuffer |
| `src/assets.c` | Finds the game folder, maps SierraCh names to the 1987 ones, loads screens, sprites, roster and options |
| `src/boxer.c` | Boxer records and derived ratings |
| `src/anim.c` | Boxer animation: the original's tables, sequences and draw routine |
| `src/fight.c` | Round simulation, corners, judges |
| `src/screens.c` | Screen flow, text layer, boxer drawing |
| `src/audio.c` | PC speaker / PCjr sound effects through SDL3 audio |
| `src/main.c` | SDL3 window, input, rendering, self-test and screenshot modes |
