# Level maps and terrain

## `.map` files

| Field | Type | Notes |
| --- | --- | --- |
| magic | `char[4]` | `"MAP "` |
| size | `u32` | bytes following this field (8 + width × height × 2) |
| width, height | `u32`, `u32` | grid size in blocks (points for the surface map) |
| values | `u16[width × height]` | row-major |

Per level (keys in the level's `Lego.cfg` block):

| Key | Meaning of values |
| --- | --- |
| `SurfaceMap` (`High_*.map`) | terrain point heights; world Z = −value × `RoughLevel` |
| `PredugMap` (`Dugg_*.map`) | 1 = open cavern, 2 = hidden cavern, 3 = open with slug hole, 4 = hidden with slug hole |
| `TerrainMap` (`Surf_*.map`) | 1 solid rock, 2 hard rock, 3 loose rock, 4 dirt, 5 → loaded as 4, 6 lava, 8 ore seam, 9 water (lake), 10 energy crystal seam, 11 recharge seam |
| `PathMap` (`Path_*.map`) | on open ground: 1 = rubble, 2 = power path |
| `CryOreMap`, `ErodeMap`, `EmergeMap`, `AIMap`, `FallinMap`, `BlockPointersMap` | resources, lava erosion, monster emergence, AI hints, cave-ins, tutorial block ids |

Only interior blocks (1 .. size − 2) are read from the predug map; the outer ring is always rock.

## Terrain mesh (Map3D)

* A W × H point grid gives (W−1) × (H−1) quads; quad (x, y) is the visible top of block (x, y)
  with corners (x, y), (x+1, y), (x+1, y+1), (x, y+1).
* World: x = px × BlockSize − (W−1) × BlockSize / 2, y = (H−1) × BlockSize / 2 − py × BlockSize.
  +Z points down.
* Every point starts at its surface-map height. Digging a block lowers each of its four corner
  points that is still raised by `DigDepth` (ore/crystal seams: four stages of DigDepth / 4).
* A quad is split along the diagonal with the smaller height difference.
* The number of lowered corners decides the shape: 4 floor, 2 straight wall (or diagonal "V" wall
  when the raised corners are opposite), 1 outer corner, 3 inner corner, 0 solid rock top.

## Surface textures

A texture id is two hex digits that name the file: `<texturebasename>XY.bmp` (e.g. `Rock35.bmp`).

| Id | Surface |
| --- | --- |
| `00` | clean floor (predug caverns) |
| `10`–`13` | floor with 3 .. 0 rubble layers |
| `01`–`05` | straight wall: soil, dirt, loose, hard, solid |
| `21`–`25` | reinforced wall |
| `20` / `40` / `67` | crystal / ore / recharge seam |
| `31`–`35` | outer corner |
| `51`–`55` | inner corner |
| `77` | diagonal wall |
| `70` | solid rock top |
| `30` | slug hole |
| `45` | water, `46` lava, `06/16/26/36` erosion stages, `47` cooled lava |
| `61`–`65`, `71`–`75` | power path pieces (unpowered / powered): cross, straight, corner, T, end |
| `66` / `76` | building foundation (powered / unpowered) |

Texture coordinates are rotated by the block direction; lava and water quads get slowly drifting
UVs.

Implementation: [`src/game/world/Level.cpp`](../../src/game/world/Level.cpp),
[`src/game/world/Map3D.cpp`](../../src/game/world/Map3D.cpp).
