# Roadmap

Where the reimplementation stands and what comes next, in order. Each milestone lists what the
original does (the functions to read first) and what "done" means when you play it.

## Where we are

Playable: Level 1 start to finish, and the early missions as far as their systems go.

- Levels load from the original data: terrain, textures, lighting, fog, objects, hidden caverns.
- Rock Raiders drill, clear rubble, collect and store crystals and ore, fetch tools, reinforce
  walls, train skills, use dynamite and drop what they carry. Tasks follow the level's
  priorities, and the priority panel can reorder or switch them off.
- Building placement, construction sites with deliveries, power paths, the power grid.
- Vehicles (milestone 1, play-tested): teleporting in, getting in and out, driving, digging,
  clearing rubble, hauling loads, ground following, engine sounds, upgrade parts drawn.
- Effects: rock falls, explosions, scene sound cues, activity and put-down sounds.
- Interface: icon menus that slide, HUD panels, radar, crystal side bar, objective window,
  block highlights, level scripts (NERPs) with their messages.
- Testing aids: cheat mode (`[debug] cheats=1` or `--cheats`) and the `RR_DEBUG_*` hooks
  (`RR_DEBUG_SKILLS=bits` gives every raider skills; `RR_DEBUG_SPAWN="Type,x,y[,dir];..."` places
  objects, queues collect jobs for ore and crystals and stands buildings on their footprint;
  `RR_DEBUG_PATH="x,y;..."` lays power paths; `RR_DEBUG_UPGRADE="tick,type"` sends the first
  vehicle and the first raider on foot for an upgrade).

**Next: milestone 2, building functions.** Step 1 (building upgrades) is play-tested; step 2
(Upgrade Station: vehicle and Rock Raider upgrades) is built and waits for the play-test. Then
the further building functions (step 3), starting with the Ore Refinery.

## How we work

Each step is built and committed, then played and checked by the user. Fixes follow the
report. Behaviour comes from the decompiled game; where we deliberately differ, the code says so.

- **Keep this file current.** The user clears the context often: after each step, record what
  was done, what is open and what comes next here, and commit it with the work.
- **Test setups are batch files** in `scripts\` (the user runs Windows and cannot set environment
  variables on a command line). Example: `scripts\test-vehicles.bat`. `.bat` files are kept
  with CRLF line endings (`.gitattributes`).
- **The user play-tests.** Build, commit, then hand over a short checklist. Short headless runs
  of our own are fine for data and sanity checks (`--screenshot <png> --ticks N`,
  `--debugcam "tilt,zoom,x,y"`, `--input "tick:click;..."`; the level briefing needs clicks to
  dismiss). The log is `build/debug/RockRaiders.log`; `--debug` adds debug lines.
- **Build:** `. tools/env.sh` then `cmake --build --preset debug` (clang, Ninja).
- **Reading the original:** decompiled functions are in `analysis/raw/decomp/<addr>_<name>.c`
  (one file per function, reference names from OpenLRR), extracted game data in
  `analysis/raw/data/` (Lego.cfg, .ae activity files, .lws scenes, level maps), strings in
  `analysis/raw/strings.json`. Object fields appear as raw offsets: `param_1[0xdb]` is the
  driver / driven vehicle, `[0xf8]`..`[0xfb]` flags1..4 (our `flags3` is `[0xfa]`), `[0xfc]`
  abilities, `[0xc2]`.. the carried objects with their count in `[0xc9]`, `[0xbf]` the route
  target object.
- **Level coordinates:** `Dugg_*.map` / `Surf_*.map` (header `MAP `, u32 width/height at offset
  8, u16 cells from offset 16). RR_DEBUG_SPAWN takes 0-based block coordinates.

## 1. Vehicles and drivers (done; upgrades in milestone 2 step 2)

All play-tested. Test: `scripts\test-vehicles.bat` (Level02: Small Truck, Hover Scout, Small
Digger and Bulldozer, ore and crystals to collect, every raider trained).

1. **Getting in and out.** Get In (menu) creates a FindDriver task (priority GetIn) any raider
   with the skill takes; clicking an unmanned vehicle with a trained raider selected sends that
   raider. Skill: NeedsPilot or land+water: Pilot, land: Driver, water: Sailor. The raider
   boards on arrival (`ObjectList::Board`), sits on `DriverNullName` (or the vehicle origin),
   plays `<vehicle activity><vehicle type>` (`Activity_` + type while routing) and is hidden for
   `InvisibleDriver`. Get Out puts it at the deposit null (`ObjectList::GetOut`). Object list
   `driving` boards and trains the raider.
2. **Driving.** Driven vehicles route like units (`Lego_GetCrossTerrainType` blocks CanBeDriven
   vehicles only without a driver). The original never plays StartUp or ShutDown. EngineSound
   loops on every vehicle and building from arrival until removal (`LegoObject_FUN_00438720`);
   one buffer per sample, so the latest object with that sound takes it. A goto click deselects
   the units, as in the original (see the remaster option in milestone 8).
3. **Vehicle jobs.** Driven vehicles take tasks (`AITask_FUN_00404ef0`). Drill-null vehicles dig
   and stop when the drill tip (found in any animation, `NullPoint(..., anyActivity)`) is in the
   wall; the Small Digger's hard rock time really is 180 s. CanClearRubble vehicles clear all
   layers in one pass. Carriers (carry null + CrossLand) hold min(MaxCarry, CarryNullFrames)
   objects of one type on carry null CarryStart + slot in `GameObject::cargo`, and deliver when
   full or after standing 75 units (`LegoObject_VehicleMaxCarryChecksTime_FUN_00439c80`).
   Units drop loads (`ObjectList::DropCarried`) via the Unload items and before direct orders.
   Upgrade parts are looked up with `ObjectRegistry::FindUpgradePart` and play the vehicle's
   activity.
4. **Ground following.** `ObjectList::UpdateVehicleTilt` (`Vehicle_SetPosition`): two triangle
   normals from the floor under the first four wheel nulls, smoothed (0.4), give the tilt;
   wheels are moved onto the floor keeping their rest height (the original's exact offset is
   lost in the decompile) and each turns by its own movement, backwards when reversing.
5. **Vehicle upgrades:** built with the Upgrade Station (milestone 2 step 2). Original notes:
   engine, drill, scan and carry bits (`GameObject::upgradeLevel`, the .ae `Upgrades::LevelABCD`
   blocks, bit order [carry][scan][speed][drill]); stats have one column per level (Lego.cfg
   `Levels 16`; `o.level` picks the column - check how the original maps upgrade bits to it).
   Read `LegoObject_CompleteVehicleUpgrade`, `Vehicle_SetUpgradeLevel`, `Vehicle_CanUpgradeType`,
   `LegoObject_TryUpgrade_FUN_00448f50`, `AITask_DoUpgrade`, and task type 0x15 in
   `AITask_FUN_00404ef0` (the vehicle drives to the station's deposit null).

Still open (later milestones): water vehicles (docks, GetInAtLand/GetOutAtLand, the cats
carrying on water), several selected raiders answering one get-in click, TakeCarryingDrivers,
vehicles carrying barriers, the get-in mouse pointer, reversing and turning (milestone 7).

## 2. Building functions (next)

Test: `scripts\test-buildings.bat` (Level18: Tool Store, Power Station, Teleport Pad, Support
Station, two Mining Lasers, Geological Center; added an Upgrade Station at 19,30 with power
paths to it and the Tool Store, a Small Digger, Hover Scout and Small Truck, three trained
raiders; 10 crystals and 50 ore). Level18 has no raiders of its own and its Power Station
reaches nothing without the added paths.

1. **Upgrading buildings** (play-tested). The Upgrade item of a building's
   menu (`LegoObject_GetBuildingUpgradeCost`, `LegoObject_UpgradeBuilding`): enabled while the
   building has another level and storage holds `Main::BuildingUpgradeCostOre` ore (5), or else
   `BuildingUpgradeCostStuds` studs (1). The upgrade is immediate: no raider and no upgrade time
   (`UpgradeTime` in the stats is for vehicles). The level goes up (stats column), the
   `UpgradeEffect` sparks play (misc effect 9, at most ten at once) and the power grid is
   recomputed. The disabled tooltip reads "Ore needed to upgrade: n" (the original draws n ore
   icons after the text). Tooltips are placed as in `ToolTip_Draw`: 32 below the pointer (above it
   at the bottom), menu icon tooltips right-aligned to end 32 right of the pointer, all kept
   inside the right edge. Building upgrade parts are drawn for the building's level
   (`Upgrade_SetUpgradeLevel` takes the parts whose level equals the object level, so level 1 is
   `Level0001`); only the Mining Laser has them (Turret, DTurret at level 1). The third value of
   a part is a weapon name, not a parent: the Mining Laser turret and the Large Digger's laser
   were skipped before and are drawn now. Not done: the help window text
   (`HelpWindow_RecallDependencies`), the turret's fire and pivot nulls (milestone 3).
2. **Upgrade Station** (built, waiting for the play-test). `AITask_DoUpgrade` makes an Upgrade
   task (0x15) only the ordering unit takes; buildings must be active (`LegoObject_IsActive`:
   standing and powered, flags3 0x20000000).
   - *Rock Raiders* (UpgradeMan, item 0x40): enabled with another level to go, empty hands and
     an active tool store. The raider goes to the nearest one (`Level_GetBuildingAtPosition`
     is a nearest-tool-store search), stands beside it like training (`LegoObject_TryTrain`
     with action 0xf) and plays Activity_Train (100 repeats) for UpgradeTime (seconds x 25)
     times the tool store's FunctionCoef; then its level goes up (more tools on the belt). No
     cost.
   - *Vehicles* (UpgradeVehicle 0x42 opens the submenu; Engine 0x43 / Drill 0x44 / Scan 0x45 /
     Carry 0x46 are upgrade types 1 / 0 / 2 / 3). Enabled with an active Upgrade Station and
     `LegoObject_HasEnoughOreToUpgrade`: the model has parts for a level with the type's bit
     (`Vehicle_CanUpgradeType`), it does not have it yet, and storage holds the type's
     `UpgradeCostOre` (stats column = type) or a fifth of it in studs. The vehicle drives to the
     block of the nearest free station's deposit null (`LegoObject_TryUpgrade`; facing bits from
     block deltas, which ends up facing the station). On arrival both play Activity_Upgrade;
     the station animates at its FunctionCoef (0.8, 1.6 at level 1). When the station's
     animation ends the cost is paid (ore, or else `UpgradeCostStuds`; nothing if neither is
     there) and the bit is set: `upgradeLevel` is the vehicle's stats level and picks the
     drawn parts. Some upgrades are free (Small Digger drill). Differences: the game also
     waits for the vehicle's own animation to loop; "a unit has been upgraded" text and the
     help window are not shown yet.
3. **Further building functions:**
   - **Ore Refinery:** turns ore into refined ore (studs). The side bar's stud display, and
     `CostRefinedOre` for buildings that ask for it.
   - **Support Station:** oxygen supply (`OxygenCoef`) and raiders eating to restore energy
     (GoFeed, Eat).
   - **Geological Center:** scanning reveals the map around it (ScanRange) and on the radar.
   - **Mining Laser:** turret firing at monsters (`Weapon_*`).
   - **Docks, Super Teleport:** water and large vehicles (with milestone 1).
   - **Power-off bubbles** over buildings without power, and the Power On/Off menu item.
   - **Teleporting buildings down** (`LegoObject_TeleportDownBuilding`). Construction leftovers:
     flattening the ground, barriers around sites, height tolerance.

Notes from milestone 1 that matter here: buildings already loop their EngineSound hum; the
Bulldozer has no EnterToolStore, so it delivers only to ProcessOre / ProcessCrystal buildings
(Refinery, Power Station) once the carry upgrade lets it carry.

## 3. Rock monsters and combat

- Rock, Lava and Ice monsters emerge from walls (`LegoObject_TryGenerateRMonster`). They wander,
  eat crystals, attack buildings and raiders, throw boulders, and are woken by noise and dynamite.
- Health, damage and death for every unit (dynamite already deals damage).
  Health bars (`ShowHealthBar`).
- Weapons: laser, pusher and freezer guns (GetTool items), the Mining Laser, laser shots,
  boulder hits.
- Raiders run from monsters and ticking dynamite properly (`LegoObject_TryRunAway`, RunPanic).
- Slugs and slug holes. Electric fences.

## 4. Hazards

- Lava erosion spreading over time (`Level_UpdateErosion`), with cooled lava.
- Landslides and cave-ins (`Fallin_*`), which reinforced walls prevent.
- Oxygen running out in levels that use it: the air meter, and losing the level.
- Water and lake blocks (wading depth, boats).

## 5. Interface and feedback

- Info messages and the info dock (seam found, cavern discovered, trained, under attack…),
  and the text window messages.
- Proper tooltip boxes, the message panel's text window and speech, the advisor.
- Camera controls panel (rotate, zoom, cycle buildings), first and second person views.
- Encyclopedia, Information panel.
- The rest of the interface items, drag-select, the original's right-click handling.
- The remaining level script functions (`NerpsFunctions.h`), message timing, camera requests.

## 6. Level flow

- Rewards screen after a level, score and medals.
- Mission completion unlocking the next missions, save games (slots, completion flags), and
  the options and load/save menus.
- Level end conditions: crystal goals, time limits, losing all raiders or buildings.

## 7. Fidelity and polish

- Level of detail (medium and high poly meshes by distance), camera shake, smoke, sparkles.
- Sound: 3D ranges, speech samples, the remaining sound triggers.
- Movement details: turning on the spot, reversing, slow-downs, straightening curves near walls.
- Drilling details: drill null, single-width tunnels, the inner-corner nudge.

## 8. Remaster options (opt-in, after the game is complete)

Higher resolutions and widescreen interface, faster game speeds, quality-of-life options,
and mod and override folders. Defaults always stay faithful to the original.

Quality-of-life options asked for so far:

- **Keep selection after a move order.** The original deselects units (raiders and vehicles)
  after a goto click, because `Lego_HandleWorld` calls `Interface_BackToMain`. As an option,
  the selection and its menu stay so a vehicle can be driven around with repeated clicks.

## Parked issues

- Rock falls when breaking into a cave (the outside-corner animation) don't look right.
- Interrupting a task when a unit is selected: two flag states in `Message_SelectObject` that
  exempt units are not identified yet.
- Raiders running from dynamite just walk away (no RunPanic, no proper escape route).
- Units with zero health do not die yet.

## Suggested order

1 → 2 → 3, since vehicles and buildings feed the later missions and monsters depend on both.
Then 4 and 6, so whole missions can be won or lost, then 5 and 7 in parallel as missions
need them, and 8 last.
