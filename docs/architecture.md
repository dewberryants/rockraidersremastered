# LegoRR.exe architecture

Findings from the Ghidra analysis of `Rock Raiders\LegoRR.exe` (v1.0, linked 1999-09-27, MSVC 5.0,
x86-32, not packed). Function names come from the community reference (OpenLRR), mapped onto this build
and checked against our own decompilation; see [analysis.md](analysis.md) for how.

## Two layers

| Layer | What it is | Main modules (function count) |
| --- | --- | --- |
| **Gods98** engine | LEGO Media's in-house DirectX 6 engine (source path `C:\Dev\SourceSafe\gods98\dx6\...`). Platform, files, config, 2D/3D, sound, input. | Container 110, Mesh 59, Sound3D 42, File 37, Image 27, Lws 25, Viewport 22, DirectDraw 21, Main 20, Draw 18, Maths 18, Flic 17, Config 16, Wad 14, RNC 14, Font 13, TextWindow 13, Animation 9, Movie 5, Input 6, Mem 7, Registry 3 |
| **LegoRR** game | Game logic on top of Gods98. | NERPFunc 285, LegoObject 271, Front 141, Lego 119, AITask 105, Level 101, Interface 99, Map3D 56, Panel 52, StatsObject 49, Weapon 47, Info 38, Reward 33, Construction 32, Flocks 29, Vehicle 29, ElectricFence 27, Effect 27, Camera 27, Message 26, NERPsRuntime 26, SFX 25, Building 24, Creature 20, HelpWindow 19, NERPs 18, Advisor 17, Bubble 16, Objective 15, Priorities 14, RadarMap 14, Stats 14, Water 13, Smoke 12, Teleporter 12, … |

3,005 functions in total after analysis; 2,745 carry reference names, the rest are CRT/DirectX helpers or
not yet named.

## Start-up sequence (`Main_WinMain`)

1. Single-instance mutex `"Lego Rock Raiders Mutex"`.
2. Program name = executable name without extension (`LegoRR`); command line = registry
   `HKLM\SOFTWARE\LEGO Media\Games\Rock Raiders\StandardParameters` + actual arguments.
3. `Error_Initialise`, `Mem_Initialise`.
4. `File_Initialise`: data root `<cwd>\Data`; mounts `LegoRR0.wad` … `LegoRR9.wad`; locates the CD
   (a drive with `Data\cd.key`).
5. `Config_Initialise`, `Input_InitKeysAndDI`, window creation, DirectDraw, `Sound_Initialise`,
   `Init_Initialise` (display mode selection), `Animation_Initialise`, `Draw_Initialise`.
6. `Gods_Go("LegoRR")` registers the game's three callbacks: `Lego_Initialise`, `Lego_MainLoop`,
   and `Lego_Shutdown_Full` / `Lego_Shutdown_Quick`.
7. Main loop (below), then the shutdown callback.

`Lego_Initialise` loads `Lego.cfg` and runs ~100 sub-initialisers (fonts, text windows, panels,
interface, pointers, object types, sounds, reward screen, …) before showing the front end.

## Main loop and time units

Every iteration: pump window messages → read keyboard → read mouse → call the game's main-loop
callback with `elapsed` → finish 3D → flip.

`elapsed` is in **game units of 1/25 s** (Lego.cfg: "Times are specified as 25.0 units per second"):

- normally `elapsed = milliseconds_since_last_frame * 0.025`, clamped to at most **3.0**;
- a "fixed frame" mode flag forces `0.8333333` (= 1/30 s);
- a pause flag forces `0`;
- `Main_SetState` can override with a fixed value.

`Main_LoopUpdate(clear)` is the same pump/flip used by blocking screens (loading, front end).

World units: **40 units per map block** (also from the Lego.cfg header).

## File system (`File_*`, `Wad_*`)

See [formats/wad.md](formats/wad.md).

- Names are resolved relative to `<cwd>\Data` (leading `\` stripped, `..` resolved); paths that would
  leave the data root are rejected.
- Reads: if the name is in any mounted WAD, the WAD copy is used; WADs are searched from the highest
  number down, so `LegoRR1.wad` overrides `LegoRR0.wad`. Otherwise the loose file under `Data\` is used,
  then `<CD>:\Data\<name>`.
- Writes always go to the loose `Data\` file.
- All name comparisons are case-insensitive.

## Configuration (`Config_*`)

See [formats/cfg.md](formats/cfg.md). Everything in the game is driven by `Lego.cfg` lookups such as
`Lego*::Main::LoadScreen`.
