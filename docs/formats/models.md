# Models, animation and textures

## Object types and activity files

`Lego.cfg` maps object type names to folders: `MiniFigureTypes`, `VehicleTypes` (some vehicles have two
parts, e.g. `Vehicles\WalkerLegs,Vehicles\WalkerBody`), `BuildingTypes`, `RockMonsterTypes`,
`UpgradeTypes`. Each folder holds an activity file `<folder>\<leaf>.ae` in config format:

| Key | Meaning |
| --- | --- |
| `Scale` | model scale |
| `CameraNullName`, `DrillNullName`, `CarryNullName`, ... | names of null objects used as attachment points |
| `MediumPoly`, `HighPoly`, `FPPoly` | mesh replacements per level of detail / first-person camera |
| `Activities { Activity_Stand Stand ... }` | game activity id → block name (`!` = optional) |
| `<Block> { FILE VLPstand  LWSFILE TRUE  TRANSCOEF 0.0  LOOPING  TRIGGER  SAMPLE }` | the animation scene and its parameters |

## LightWave scenes (`.lws`, LWSC version 1)

Used for every animated model. Relevant keywords: `FirstFrame`, `LastFrame`, `FramesPerSecond` (25),
`LoadObject <path>` (only the file name is used: searched in the scene's folder, then
`Lego*::Main::SharedObjects`), `AddNullObject <name>`, `ObjectMotion` (9 channels: x y z, heading
pitch bank in degrees, scale x y z; each key followed by `frame linear tension continuity bias`),
`ParentObject <1-based index>` (parents may appear *after* their children), `PivotPoint`,
`ObjDissolve` (value or envelope), `UnseenByCamera`.

* Keys are interpolated **linearly**; spline parameters are ignored. Looping animations wrap with
  `fmod(time, LastFrame)`, others clamp at the last frame.
* Node transform: rotation = bank·pitch·heading (LightWave HPB) with scale, rotating about the pivot.
* Game objects face their heading: model +Z along (sin h, cos h, 0), model +Y up (world −Z).

## LightWave objects (`.lwo`, LWOB)

Standard big-endian LWOB (`PNTS`, `SRFS`, `POLS`, `SURF`). How the game uses surfaces:

* One mesh group per surface; triangles and quads (quads split 0-1-2 / 0-2-3).
* Normals are smoothed per surface (sum of the polygon normals sharing a point).
* Colour = `COLR`/256, replaced by white when textured unless `FLAG` bit 0x80; diffuse = colour ×
  `DIFF`, emissive = colour × `LUMI`, alpha = 1 − `TRAN`; `FLAG` 0x200 additive, 0x100 double sided;
  no pixel blending (`TFLG` 0x20 clear) → point sampling.
* Planar texture coordinates: the axis flag in `TFLG` (1 = X, 2 = Y, 4 = Z) drops that axis;
  u, v = (coordinate − `TCTR`) / `TSIZ` + 0.5.
* `TIMG ... (sequence)` = animated texture: numbered frames, up to 100.

### `.uv` files

Optional per object. Lines: version, surface count *S*, *S* surface names, *S* texture paths
(override `TIMG`), polygon count, then per polygon `index cornerCount` followed by `u v w` per
corner. The game stores `v` negated. A trailing projection section is ignored.

## Textures

* A texture file named `A<nnn>_...` treats palette entry *nnn* as transparent.
* Lookup: the mesh's folder, then `Lego*::Main::SharedTextures`.
* 3D texture surfaces keep BMP rows in file order (bottom row first), i.e. texture v = 0 is the
  bottom of the image. Together with the negated `.uv` values this makes UV atlases line up.

Implementation: [`src/gfx/model/`](../../src/gfx/model/).
