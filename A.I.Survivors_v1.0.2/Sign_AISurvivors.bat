@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM ================================================================
REM  Signs @AISurvivors\addons\AISurvivors.pbo with DayZ Tools' DS Utils.
REM  Needed for any server with verifySignatures = 2 (your Purgatory
REM  servers). The local diag test (Test_AISurvivors_Diag.bat) does not need it.
REM
REM  First run creates your key pair in "_keys" next to this file.
REM  KEEP AISurvivors.biprivatekey PRIVATE. Never upload it.
REM  Re-run this file every time the PBO is rebuilt.
REM ================================================================

set "DSU=C:\Program Files (x86)\Steam\steamapps\common\DayZ Tools\Bin\DsUtils"
set "DZSERVER=C:\Program Files (x86)\Steam\steamapps\common\DayZServer"
set "KEYNAME=AISurvivors"
set "KEYDIR=%~dp0_keys"
set "PBO=%~dp0@AISurvivors\addons\AISurvivors.pbo"

if not exist "%DSU%\DSSignFile.exe" (
    echo [ERROR] DayZ Tools DS Utils not found in "%DSU%"
    goto :FAIL
)
if not exist "%PBO%" (
    echo [ERROR] "%PBO%" not found.
    goto :FAIL
)

if not exist "%KEYDIR%" mkdir "%KEYDIR%"
if not exist "%KEYDIR%\%KEYNAME%.biprivatekey" (
    echo Creating key pair %KEYNAME%...
    pushd "%KEYDIR%"
    "%DSU%\DSCreateKey.exe" %KEYNAME%
    popd
)
if not exist "%KEYDIR%\%KEYNAME%.bikey" (
    echo [ERROR] Key creation failed.
    goto :FAIL
)

del /q "%~dp0@AISurvivors\addons\*.bisign" 2>nul
"%DSU%\DSSignFile.exe" "%KEYDIR%\%KEYNAME%.biprivatekey" "%PBO%"
if not exist "%PBO%.%KEYNAME%.bisign" (
    echo [ERROR] Signing failed - no .bisign was created.
    goto :FAIL
)

if not exist "%~dp0@AISurvivors\keys" mkdir "%~dp0@AISurvivors\keys"
copy /y "%KEYDIR%\%KEYNAME%.bikey" "%~dp0@AISurvivors\keys\" >nul

REM Install the public key on the local DayZ Server (harmless if it fails)
if exist "%DZSERVER%\keys" copy /y "%KEYDIR%\%KEYNAME%.bikey" "%DZSERVER%\keys\" >nul

echo.
echo [OK] Signed: %PBO%.%KEYNAME%.bisign
echo [OK] Public key: @AISurvivors\keys\%KEYNAME%.bikey
echo      Put %KEYNAME%.bikey in the "keys" folder of every server that runs the mod.
pause
exit /b 0

:FAIL
echo.
echo [STOPPED] Fix the error above and run this file again.
pause
exit /b 1
