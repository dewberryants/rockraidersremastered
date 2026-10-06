# Portable toolchain

Everything here is self-contained; nothing is installed system-wide or added to the system PATH.
Only this README and the scripts are tracked in git — the tool folders are re-downloadable.

| Folder | Version | Source |
| --- | --- | --- |
| `jdk21/` | Temurin JDK 21.0.12.1 | adoptium.net (SHA-256 verified) |
| `maven/` | Apache Maven 3.9.16, local repo → `tools/m2/` (set in `maven/conf/settings.xml`) | apache.org (SHA-512 verified) |
| `ghidra/` | Ghidra 12.1.3 (`ghidra_12.1.3_PUBLIC_20260817.zip`), settings/cache/temp → `tools/ghidra-home/` (set in `ghidra/support/launch.properties`) | NSA GitHub release (SHA-256 verified) |
| `ghidra-mcp/` | bethington/ghidra-mcp 7.0.0, Python venv in `ghidra-mcp/.venv` | GitHub |
| `ghidra-mcp-dist/` | Built `GhidraMCP-7.0.0.zip` (GUI extension) and `GhidraMCPHeadless.jar` | built locally |
| `uv/`, `python/` | uv 0.12.23, CPython 3.12.15 (uv-managed) | astral-sh GitHub (SHA-256 verified) |
| `llvm-mingw/` | llvm-mingw 20260922 (clang 23, UCRT, x86_64) | mstorsjo GitHub |
| `cmake/`, `ninja/` | CMake 4.4.4, Ninja 1.13.2 | Kitware / ninja-build GitHub |

## Use

```powershell
. .\tools\env.ps1          # PowerShell
```

```bash
. tools/env.sh             # Git Bash
```

## Ghidra MCP server

Start the headless server (serves `127.0.0.1:8089`, file access limited to this project folder):

```powershell
powershell -ExecutionPolicy Bypass -File tools\start-ghidra-mcp-headless.ps1 -Project E:\Users\LtLary\Desktop\WTF\analysis\ghidra\LegoRR -Program LegoRR.exe
```

The project-level `.mcp.json` registers the stdio bridge (`bridge-mcp-ghidra`) with Claude Code.
The Ghidra project lives in `analysis/ghidra/LegoRR` (git-ignored).

To use the Ghidra GUI instead: run `tools\ghidra\ghidraRun.bat`, install `tools\ghidra-mcp-dist\GhidraMCP-7.0.0.zip`
via File > Install Extensions, and enable the plugin in the CodeBrowser (File > Configure). Only one of GUI/headless
can have the project open at a time.
