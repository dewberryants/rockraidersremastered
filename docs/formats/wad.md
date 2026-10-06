# WAD archives (`LegoRR0.wad`, `LegoRR1.wad`)

Little-endian. Verified against both retail archives and `Wad_Load`.

| Field | Type | Notes |
| --- | --- | --- |
| magic | `char[4]` | `"WWAD"` |
| count | `u32` | number of entries |
| names | `count` × NUL-terminated string | lookup path relative to `Data\`, e.g. `Levels\GameLevels\Level01\01.txt` |
| sourcePaths | `count` × NUL-terminated string | original path on the developers' machine (`\\ROBW\C\Dev\SourceSafe\Lego\...`); unused at run time |
| entries | `count` × 16 bytes | `u32 flags`, `u32 size`, `u32 packedSize`, `u32 offset` (from file start) |

- Every retail entry has `flags = 1` (stored, `size == packedSize`). The executable also contains an
  RNC decompressor (`RNC_Uncompress`), but no shipped entry uses it.
- `LegoRR0.wad`: 5,631 entries (models, textures, sounds, levels). `LegoRR1.wad`: 214 entries
  (language-specific: `Lego.cfg`, text, voice samples, level briefings).
- Up to ten archives (`<program>0.wad` … `<program>9.wad`) are mounted; higher numbers win on lookup.

Content by type (both archives): 2,523 `.bmp`, 1,132 `.lwo`, 750 `.lws`, 387 `.wav`, 240 `.map`,
220 `.blx`, 164 `.x`, 60 `.uv`, 56 `.ae`, 44 `.ol`, 43 `.txt`, 37 `.ptl`, 34 `.npl`, 34 `.nrn`,
27 `.nrm`, 21 `.ifo`, 18 `.flh`, 9 `.blk`, plus `Lego.cfg`.

Implementation: [`src/fs/Wad.cpp`](../../src/fs/Wad.cpp), [`src/fs/FileSystem.cpp`](../../src/fs/FileSystem.cpp).
