@echo off
rem Vehicle test: Level02 with a Small Truck, a Hover Scout, a Small Digger and a Bulldozer
rem beside the Tool Store, and every Rock Raider trained as Pilot, Sailor and Driver.
setlocal
set RR_DEBUG_SKILLS=7
set RR_DEBUG_SPAWN=SmallTruck,7,9;Hoverboard,6,9;SmallDigger,7,10;BullDozer,5,9
cd /d "%~dp0..\build\debug"
RockRaiders.exe --startlevel Level02 --debug %*
endlocal
