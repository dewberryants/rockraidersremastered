# Rock Raiders re-implementation

A from-scratch C++17 / SDL2 / OpenGL engine that runs Rock Raiders (PC, 1999) using the data from
your own copy of the game. Game behaviour is reproduced from a reverse-engineering analysis of the
original executable.

No game data or original code is part of this repository: the engine reads `LegoRR0.wad`,
`LegoRR1.wad` and `Data\` from your installation at run time.

## Build

```powershell
. .\tools\env.ps1
cmake --preset debug
cmake --build --preset debug
.\build\debug\RockRaiders.exe --data "E:\path\to\Rock Raiders"
```

The toolchain is portable and lives in `tools/` (see [tools/README.md](tools/README.md)).

Options: `--data <dir>` game folder (auto-detected if it is next to the exe or a `Rock Raiders`
subfolder), `--fullscreen`, `--debug` (verbose log), `--screenshot <file.png> --frames N`
(render N frames, save, exit).

## Documentation

- [docs/architecture.md](docs/architecture.md): how the original executable is put together
- [docs/analysis.md](docs/analysis.md): how the binary was analysed and named
- [docs/formats/](docs/formats/): file formats

## Status

| Area | State |
| --- | --- |
| WAD / Data file system | done |
| Lego.cfg parser and lookup | done |
| BMP images, 2D renderer | done |
| Fonts, text windows, front end menus | next |
| Sound, music, movies | planned |
| 3D: LightWave/X models, terrain, camera | planned |
| Levels, objects, AI tasks, NERPs scripts, interface | planned |
