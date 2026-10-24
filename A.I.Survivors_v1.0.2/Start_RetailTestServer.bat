@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM ================================================================
REM  PURGATORY - A.I. SURVIVORS TEST on the retail DayZServer
REM  (fixed version of Purgatory_AISurvivors_Test_Start.bat)
REM
REM  What changed:
REM   - adds @Dabs Framework (Expansion Core will not load without it)
REM   - loads the mods straight from your Steam Workshop folder, so
REM     nothing has to be copied into the DayZServer folder
REM   - copies every mod's .bikey into DayZServer\keys (verifySignatures = 2)
REM   - loads @AISurvivors from this folder
REM
REM  Run Sign_AISurvivors.bat first, because retail servers need a signed PBO.
REM  Players joining need the same mods: CF, Dabs Framework,
REM  DayZ-Expansion-Core, DayZ-Expansion-AI and @AISurvivors.
REM ================================================================

set "SERVER_DIR=C:\Program Files (x86)\Steam\steamapps\common\DayZServer"
set "WS=C:\Program Files (x86)\Steam\steamapps\common\DayZ\!Workshop"
set "HERE=%~dp0"
set "SERVER_CONFIG=serverDZ.cfg"
set "SERVER_PORT=2302"
set "PROFILE_DIR=PurgatoryTestProfiles"

REM Mission: Nasdara. Change to dayzOffline.chernarusplus etc. to test another map.
set "MISSION=mpmissions\dayzOffline.nasdara"

set "MODS=%WS%\@CF;%WS%\@Dabs Framework;%WS%\@DayZ-Expansion-Core;%WS%\@DayZ-Expansion-AI;%HERE%@AISurvivors"

if not exist "%SERVER_DIR%\DayZServer_x64.exe" (
    echo [ERROR] "%SERVER_DIR%\DayZServer_x64.exe" not found.
    goto :FAIL
)
if not exist "%HERE%@AISurvivors\addons\AISurvivors.pbo.AISurvivors.bisign" (
    echo [ERROR] AISurvivors.pbo is not signed. Run Sign_AISurvivors.bat first.
    goto :FAIL
)

call :KEYS "%WS%\@CF" "Community Framework"
if errorlevel 1 goto :FAIL
call :KEYS "%WS%\@Dabs Framework" "Dabs Framework"
if errorlevel 1 goto :FAIL
call :KEYS "%WS%\@DayZ-Expansion-Core" "DayZ-Expansion-Core"
if errorlevel 1 goto :FAIL
call :KEYS "%WS%\@DayZ-Expansion-AI" "DayZ-Expansion-AI"
if errorlevel 1 goto :FAIL
call :KEYS "%HERE%@AISurvivors" "A.I. Survivors"
if errorlevel 1 goto :FAIL

cd /d "%SERVER_DIR%"
if not exist "%PROFILE_DIR%" mkdir "%PROFILE_DIR%"
title Purgatory - AI Survivors TEST

echo ================================================================
echo  Mods:    %MODS%
echo  Mission: %MISSION%
echo  Logs:    %SERVER_DIR%\%PROFILE_DIR%
echo ================================================================

"%SERVER_DIR%\DayZServer_x64.exe" "-config=%SERVER_CONFIG%" "-port=%SERVER_PORT%" "-profiles=%PROFILE_DIR%" "-mission=%MISSION%" "-mod=%MODS%" -cpuCount=2 -doLogs -adminLog -netLog -freezeCheck
echo.
echo [INFO] Server exited. Logs: "%SERVER_DIR%\%PROFILE_DIR%"
pause
exit /b 0

:KEYS
if exist "%~1\" goto :KEYS_FOUND
echo [ERROR] Missing mod: %~2
echo         Expected at: "%~1"
exit /b 1
:KEYS_FOUND
if exist "%~1\keys\*.bikey" copy /y "%~1\keys\*.bikey" "%SERVER_DIR%\keys\" >nul
exit /b 0

:FAIL
echo.
echo [STOPPED] Fix the error above and run this file again.
pause
exit /b 1
