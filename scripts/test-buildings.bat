@echo off
rem Building test: Level18 starts with a Tool Store, Power Station, Teleport Pad, Support Station,
rem two Mining Lasers and a Geological Center. Storage starts with 10 crystals and 30 ore,
rem enough for six building upgrades at 5 ore each.
setlocal
set RR_DEBUG_STOCK=10,30
cd /d "%~dp0..\build\debug"
RockRaiders.exe --startlevel Level18 --debug %*
endlocal
