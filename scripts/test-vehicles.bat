@echo off
rem Vehicle test: Level02 with a Small Truck, a Hover Scout, a Small Digger and a Bulldozer
rem beside the Tool Store, six ore and two crystals to collect, and every Rock Raider trained
rem as Pilot, Sailor and Driver.
setlocal
set RR_DEBUG_SKILLS=7
set RR_DEBUG_SPAWN=SmallTruck,7,9;Hoverboard,6,9;SmallDigger,7,10;BullDozer,5,9;Ore,10,10;Ore,10,10;Ore,10,10;Ore,10,10;Ore,9,10;Ore,9,10;PowerCrystal,6,10;PowerCrystal,6,10
cd /d "%~dp0..\build\debug"
RockRaiders.exe --startlevel Level02 --debug %*
endlocal
