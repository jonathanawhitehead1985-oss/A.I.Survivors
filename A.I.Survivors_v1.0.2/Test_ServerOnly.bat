@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM ================================================================
REM  A.I. SURVIVORS - SERVER-ONLY TEST (DayZDiag server, Nasdara, no game client)
REM
REM  Uses the mods you already have from the Steam Workshop, so nothing
REM  has to be copied into the DayZServer folder. Signatures are not
REM  checked in this test, so the PBO does not need to be signed.
REM  Everything it creates goes into the "_TestServer" folder next to
REM  this file. Delete that folder to start fresh.
REM ================================================================

set "DAYZ=C:\Program Files (x86)\Steam\steamapps\common\DayZ"
set "DZSERVER=C:\Program Files (x86)\Steam\steamapps\common\DayZServer"
set "WS=%DAYZ%\!Workshop"
set "HERE=%~dp0"
set "TEST=%~dp0_TestServer"
set "MAP=dayzOffline.nasdara"
set "MISSION=%TEST%\%MAP%"
set "PORT=2302"

REM Load order: CF -> Dabs Framework -> Expansion Core -> Expansion AI -> A.I. Survivors
set "MODS=%WS%\@CF;%WS%\@Dabs Framework;%WS%\@DayZ-Expansion-Core;%WS%\@DayZ-Expansion-AI;%HERE%@AISurvivors"

REM Stop any previous test session (it locks the PBO and the port)
taskkill /im DayZDiag_x64.exe /f >nul 2>&1
timeout /t 2 /nobreak >nul

if not exist "%DAYZ%\DayZDiag_x64.exe" (
    echo [ERROR] DayZDiag_x64.exe not found in "%DAYZ%"
    goto :FAIL
)

call :NEED "%WS%\@CF" "Community Framework"
if errorlevel 1 goto :FAIL
call :NEED "%WS%\@Dabs Framework" "Dabs Framework"
if errorlevel 1 goto :FAIL
call :NEED "%WS%\@DayZ-Expansion-Core" "DayZ-Expansion-Core"
if errorlevel 1 goto :FAIL
call :NEED "%WS%\@DayZ-Expansion-AI" "DayZ-Expansion-AI"
if errorlevel 1 goto :FAIL
if not exist "%HERE%@AISurvivors\addons\AISurvivors.pbo" (
    echo [ERROR] "%HERE%@AISurvivors\addons\AISurvivors.pbo" is missing.
    goto :FAIL
)

if not exist "%TEST%" mkdir "%TEST%"
if not exist "%TEST%\Profiles" mkdir "%TEST%\Profiles"
if not exist "%TEST%\ClientProfile" mkdir "%TEST%\ClientProfile"

REM ---- Mission: copy the vanilla Nasdara mission once ----
if not exist "%MISSION%\init.c" (
    if not exist "%DZSERVER%\mpmissions\%MAP%\init.c" (
        echo [ERROR] "%DZSERVER%\mpmissions\%MAP%" not found. Update DayZ Server in Steam.
        goto :FAIL
    )
    echo Copying the %MAP% mission...
    robocopy "%DZSERVER%\mpmissions\%MAP%" "%MISSION%" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 (
        echo [ERROR] Could not copy the mission.
        goto :FAIL
    )
)

REM ---- Server config for local diag testing ----
if not exist "%TEST%\serverDZ.cfg" (
    > "%TEST%\serverDZ.cfg" (
        echo hostname = "AI Survivors Local Test";
        echo password = "";
        echo passwordAdmin = "";
        echo enableWhitelist = 0;
        echo maxPlayers = 4;
        echo verifySignatures = 0;
        echo forceSameBuild = 0;
        echo allowFilePatching = 1;
        echo disableVoN = 1;
        echo shardId = "aisv01";
        echo disable3rdPerson = 0;
        echo disableCrosshair = 0;
        echo serverTime = "2026/7/1/10/00";
        echo serverTimeAcceleration = 1;
        echo serverTimePersistent = 0;
        echo guaranteedUpdates = 1;
        echo instanceId = 1;
        echo storageAutoFix = 1;
        echo class Missions
        echo {
        echo     class DayZ
        echo     {
        echo         template = "%MAP%";
        echo     };
        echo };
    )
)

echo ================================================================
echo  A.I. SURVIVORS - LOCAL TEST on %MAP%
echo  Mods:   %MODS%
echo  Logs:   %TEST%\Profiles   (server script_*.log)
echo  Mod settings appear after the first boot in:
echo          %TEST%\Profiles\AISurvivors\AISurvivorsSettings.json
echo ================================================================

start "AI Survivors - DIAG SERVER" "%DAYZ%\DayZDiag_x64.exe" -server "-config=%TEST%\serverDZ.cfg" "-profiles=%TEST%\Profiles" "-mission=%MISSION%" "-mod=%MODS%" -filePatching -port=%PORT% -doLogs -adminLog -freezeCheck

echo Server started in its own window. It runs without a game client;
echo tribes still spawn and build (they only avoid spawning near players).
echo Watch the newest script_*.log in "%TEST%\Profiles" for [AISurvivors] lines.
echo To stop it: close the server window, or run this file again.
pause
exit /b 0

:NEED
if exist "%~1\" exit /b 0
echo [ERROR] Missing Workshop mod: %~2
echo         Subscribe to "%~2" on the Steam Workshop, start the DayZ launcher once
echo         so it downloads, then run this file again.
exit /b 1

:FAIL
echo.
echo [STOPPED] Fix the error above and run this file again.
pause
exit /b 1
