@echo off
rem Ore Refinery test: Level18 with an Ore Refinery east of the base, power paths linking the
rem Tool Store, the refinery and the Power Station, three Rock Raiders and twelve pieces of ore
rem to collect. Storage starts with 10 crystals, no ore and no studs.
setlocal
set RR_DEBUG_STOCK=10,0,0
set RR_DEBUG_PATH=20,31;20,30;20,29;20,28;22,30;23,30;24,30;25,30
set RR_DEBUG_SPAWN=OreRefinery,26,30;Pilot,21,29;Pilot,22,29;Pilot,23,29;Ore,22,28;Ore,22,28;Ore,23,28;Ore,23,28;Ore,21,28;Ore,21,28;Ore,22,28;Ore,22,28;Ore,23,28;Ore,23,28;Ore,21,28;Ore,21,28
cd /d "%~dp0..\build\debug"
RockRaiders.exe --startlevel Level18 --debug %*
endlocal
