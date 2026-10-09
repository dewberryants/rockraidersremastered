# Roadmap

Where the reimplementation stands and what comes next, in order. Each milestone lists what the
original does (the functions to read first) and what "done" means when you play it.

## Where we are

Playable: Level 1 start to finish, and the early missions as far as their systems go.

- Levels load from the original data: terrain, textures, lighting, fog, objects, hidden caverns.
- Rock Raiders drill, clear rubble, collect and store crystals and ore, fetch tools, reinforce
  walls, train skills and use dynamite. Tasks follow the level's priorities, and the priority
  panel can reorder or switch them off.
- Building placement, construction sites with deliveries, power paths, the power grid.
- Teleporting in Rock Raiders and vehicles. Vehicles are drawn (wheels, default parts) but
  nobody can drive them yet.
- Effects: rock falls, explosions, scene sound cues, activity and put-down sounds.
- Interface: icon menus that slide, HUD panels, radar, crystal side bar, objective window,
  block highlights, level scripts (NERPs) with their messages.
- Testing aids: cheat mode (`[debug] cheats=1` or `--cheats`) and the `RR_DEBUG_*` hooks
  (`RR_DEBUG_SKILLS=bits` gives every raider skills; `RR_DEBUG_SPAWN="Type,x,y;..."` places objects).

## How we work

Test setups for the user are batch files in `scripts\` (the user runs Windows and cannot set
environment variables on a command line).

Each step is built and committed, then played and checked by the user. Fixes follow the
report. Behaviour comes from the decompiled game; where we deliberately differ, the code says so.

## 1. Vehicles and drivers (in progress)

What is done: teleporting vehicles in, their models, the unmanned vehicle menu, getting in and
out, driving, vehicle jobs (all play-tested). Next up: ground following (built), then vehicle upgrades.

Next:

1. **Getting in and out** (done). Get In (menu) creates a FindDriver task
   (priority GetIn) any raider with the skill takes; clicking an unmanned vehicle with a trained
   raider selected sends that raider. Skill: NeedsPilot or land+water → Pilot, land → Driver,
   water → Sailor. The raider walks onto the vehicle's position and boards at once
   (`ObjectList::Board`); it sits on `DriverNullName` (or the vehicle origin) and plays
   `<vehicle activity><vehicle type>` (`Activity_` + type while routing), hidden for
   `InvisibleDriver`. Get Out puts it at the deposit null if the ground takes it
   (`ObjectList::GetOut`). Object list `driving` boards and trains the raider.
   Test setup: `scripts	est-vehicles.bat` (Level02, three vehicles, every raider trained). Still open: water vehicles (docks, GetInAtLand/GetOutAtLand), several selected
   raiders answering a click, dropping a carried load first, TakeCarryingDrivers, the get-in
   pointer.
2. **Driving** (done). Driven vehicles route like units
   (`Lego_GetCrossTerrainType` only blocks CanBeDriven vehicles without a driver) with their own
   RouteSpeed and terrain rules, and play Activity_Route. The original never plays StartUp or
   ShutDown (no such activity in the exe). EngineSound loops on every vehicle and building from
   arrival until removal, driven or not (`LegoObject_FUN_00438720`); one buffer per sample, so the
   latest object with that sound takes it. Reverse (route flag 2) is left for milestone 7.
   Test: `scripts	est-vehicles.bat`. A goto click deselects the units, as in the original
   (`Interface_BackToMain` posts a clear-selection); see the remaster option in milestone 8.
3. **Vehicle jobs.** Split in two:
   - **3a (built, awaiting play-test):** driven vehicles join the task system
     (`AITask_FUN_00404ef0`: CanBeDriven needs a driver; no tools needed). Vehicles with a
     `DrillNullName` dig (flag 0x2 of `LegoObject_Create`: Small/Large Digger, Walker) with their
     own drill times and sounds, and stop when the drill tip is in the wall. The tip only exists
     in the drill animation; like `Vehicle_FindNull`, `NullPoint(..., anyActivity)` looks in the
     other animations. CanClearRubble vehicles (Bulldozer) clear rubble. Orders from the menu go
     to a selected vehicle only if it can do them. Test: `scripts	est-vehicles.bat`.
     First play-test fixes: upgrade parts were never drawn (the registry hid UpgradeTypes from
     name lookups; now `FindUpgradePart`) and play the vehicle's activity; missing DrillSound /
     DrillFadeSound fall back to SFX_Drill / SFX_DrillFade; a vehicle clears all rubble layers in
     one pass. The Small Digger's hard-rock time really is 180 s (Lego.cfg).
   - **3b (built, awaiting play-test):** carriers collect and deposit several items. A vehicle
     collects if its model has `CarryNullName` and it crosses land (flag 0x40); it holds
     min(`MaxCarry`, `CarryNullFrames`) objects of one type on carry null `CarryStart` + slot, takes
     plain ore and charged crystals only, and only if a building takes them (tool stores for
     `EnterToolStore` vehicles, otherwise a ProcessOre/ProcessCrystal building). It keeps
     collecting until full or it has stood still for 75 units, then delivers the whole load at
     once (`LegoObject_ProccessCarriedObjects`). Only the Small Truck collects without upgrades
     (Bulldozer MaxCarry is 0 until the carry upgrade). Vehicles keep their load in
     `GameObject::cargo`; raiders still use `carried`. Test: `scripts	est-vehicles.bat` (spawns
     ore and crystals with collect jobs).
     Play-tested: vehicles fine. Follow-up (play-tested): units drop their load
     (`ObjectList::DropCarried`, `LegoObject_DropCarriedObject`) through the UnLoadMinifigure /
     UnLoadVehicle menu items and before direct dig, reinforce, dynamite, train, get-tool and
     get-in orders (`AITask_UnkInitRouting_FUN_00402530` with dropCarried; clearing keeps the
     load). Dropped ore and crystals are collected again, and requested again if they were on
     the way to a construction site.
     Open: the cats carrying on water, other objects (barriers), drivers carried in
     (`TakeCarryingDrivers`).
4. **Ground following** (built, awaiting play-test). `ObjectList::UpdateVehicleTilt` follows
   `Vehicle_SetPosition`: the floor heights under the first four wheel nulls of the playing
   animation give two triangle normals; their sum, smoothed with the last tick's (weight 0.4), is
   the vehicle's up and the heading is flattened onto it. Wheels are then moved onto the floor
   (keeping the rest height of their null; the original's exact offset is lost in the
   decompile). Drivers take the vehicle's tilt. All vehicles have wheel nulls (hover vehicles use
   NULL_OBJECT wheels), so all tilt. Reported before: the Bulldozer sank into sloped floor when its
   clearing animation backs it up.
5. **Vehicle upgrades.** The upgrade menu at an Upgrade Station: engine, drill, scan and carry
   bits, their parts and stats. This needs the Upgrade Station (milestone 2).

Done when: you can beam down a Small Truck, a Hover Scout and a Small Digger, get a trained
raider in, drive each one, and have the digger drill and the truck haul ore to the Tool Store.

## 2. Building functions

Buildings that are only built and powered today:

- **Upgrading buildings:** the Upgrade menu item, ore cost, upgrade time, the model's upgrade
  parts, and higher-level stats. Read `LegoObject_UpgradeBuilding`.
- **Ore Refinery:** turns ore into refined ore (studs). The side bar's stud display, and
  `CostRefinedOre` for buildings that ask for it.
- **Support Station:** oxygen supply (`OxygenCoef`) and raiders eating to restore energy
  (GoFeed, Eat).
- **Upgrade Station:** vehicle upgrades and Rock Raider upgrades (UpgradeMan).
- **Geological Center:** scanning reveals the map around it (ScanRange) and on the radar.
- **Mining Laser:** turret firing at monsters (`Weapon_*`).
- **Docks, Super Teleport:** water and large vehicles (with milestone 1).
- **Power-off bubbles** over buildings without power, and the Power On/Off menu item.
- **Teleporting buildings down** (`LegoObject_TeleportDownBuilding`). Construction leftovers:
  flattening the ground, barriers around sites, height tolerance.

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
