@echo off
rem Support Station test: Level18 with a Support Station east of the base on the power paths,
rem three hungry Rock Raiders (energy 30: they go to eat after about 20 seconds) and the air
rem supply starting at 5 percent with a fast OxygenRate, so the warning and heartbeat show at once
rem and the powered station then fills the meter in about a minute.
setlocal
set RR_DEBUG_STOCK=10,0,0
set RR_DEBUG_ENERGY=30
set RR_DEBUG_OXYGEN=5,200
set RR_DEBUG_PATH=20,31;20,30;20,29;20,28;22,30;23,30;24,30;25,30
set RR_DEBUG_SPAWN=Barracks,26,30;Pilot,21,29;Pilot,22,29;Pilot,23,29
cd /d "%~dp0..\build\debug"
RockRaiders.exe --startlevel Level18 --debug %*
endlocal
