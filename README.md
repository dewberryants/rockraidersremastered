# Rock Raiders Redux

A from-scratch C++17 / SDL2 / OpenGL engine that runs Rock Raiders (PC, 1999) using the data from
your own copy of the game. Game behaviour is reproduced from a reverse-engineering analysis of the
original executable.

No game data or original code is part of this repository: the engine reads `LegoRR0.wad`,
`LegoRR1.wad` and `Data\` from an official installation at run time.