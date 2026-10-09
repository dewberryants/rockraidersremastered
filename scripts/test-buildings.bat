@echo off
rem Building test: Level18 starts with a Tool Store, Power Station, Teleport Pad, Support Station,
rem two Mining Lasers and a Geological Center. Added: an Upgrade Station west of the Power
rem Station, power paths linking it and the Tool Store to the Power Station, a Small Digger,
rem a Hover Scout and a Small Transport Truck, and three Rock Raiders trained as Pilot, Sailor
rem and Driver. Storage starts with 10 crystals and 50 ore.
setlocal
set RR_DEBUG_STOCK=10,50
set RR_DEBUG_SKILLS=7
set RR_DEBUG_PATH=20,31;20,30;20,29;20,28
set RR_DEBUG_SPAWN=Upgrade,19,30;SmallDigger,22,29;Hoverboard,23,30;SmallTruck,21,30;Pilot,21,29;Pilot,22,30;Pilot,23,29
cd /d "%~dp0..\build\debug"
RockRaiders.exe --startlevel Level18 --debug %*
endlocal
