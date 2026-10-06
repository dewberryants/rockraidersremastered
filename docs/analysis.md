# How the binary was analysed

## Pipeline

1. `LegoRR.exe` imported into a headless Ghidra 12.1.3 project (`analysis/ghidra/LegoRR`) through the
   GhidraMCP server; auto-analysis found 2,564 functions.
2. `analysis/scripts/export_all.py` exports every function's decompiled C plus strings and imports to
   `analysis/raw/` (git-ignored: it is derived from the original binary).
3. Function names were transferred from the community reference **OpenLRR** (no licence, so it is used
   only as a source of facts: addresses, names and declarations; none of its code is copied).
4. `analysis/scripts/dc.py` decompiles functions on demand by name, address or regex for the module being
   re-implemented.

## Mapping reference names onto this build

OpenLRR's addresses belong to a slightly different build of the same source. Function order is
identical, but a few functions changed size, so addresses drift by a **piecewise-constant delta**:

| Reference address range | Delta to our build |
| --- | --- |
| `0x401000`–`0x40136f` | `0x0` |
| `0x4013e0`–`0x4014ff` | `+0x90` |
| `0x401690`–`0x402000` | `+0xC0` |
| `0x402150`–`0x435E46` | `+0xD0` |
| `0x436A53`–`0x436D2D` | `-0x222` |
| `0x436EE0`–end of `.text` | `-0x2C0` |

`map_ref_symbols.py` finds those runs by matching sequences of three consecutive function-size gaps
between the two address lists, then maps every reference function and checks that it lands on a
function start or on bytes that look like one (after `int3`/`nop` padding or a `ret`). Results:

- 2,745 of 2,747 `.text` reference functions mapped; 2,306 hit functions Ghidra already had, and 440 were
  callbacks only reachable through function pointers, which auto-analysis had missed. These were created.
- Spot checks: `AITask_IsCollectAndSameTarget` decompiles to "type == collect && target matches";
  `Gods_Go` registers `Lego_Initialise` / `Lego_MainLoop` / `Lego_Shutdown_*`, matching the call sites.
- Globals (`.data`) use a different shift and are still to be mapped.

## Re-running

```bash
. tools/env.sh
powershell -File tools/start-ghidra-mcp-headless.ps1 -Project analysis/ghidra/LegoRR -Program LegoRR.exe
tools/ghidra-mcp/.venv/Scripts/python analysis/scripts/dc.py --list "^Config_"
tools/ghidra-mcp/.venv/Scripts/python analysis/scripts/dc.py Config_Load
```
