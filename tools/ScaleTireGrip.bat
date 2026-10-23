@echo off
rem Drag a folder of SnowRunner tire XML files (a tire mod's folder, or files taken out of the game's paks) onto this
rem file. It lists what ScaleTireGrip.ps1 would change, asks, then scales ground and mud grip by 0.55; asphalt stays.
rem From a command line:  ScaleTireGrip.bat "<folder>" [factor]   or   ScaleTireGrip.bat "<folder>" undo
setlocal
set "PS=powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0ScaleTireGrip.ps1""
set "FOLDER=%~1"
set "FACTOR=0.55"
if "%FOLDER%"=="" goto usage
if /i "%~2"=="undo" goto undo
if not "%~2"=="" set "FACTOR=%~2"
%PS% -Path "%FOLDER%" -Factor %FACTOR% -DryRun
if errorlevel 1 goto end
echo.
choice /c YN /m "Write these changes"
if errorlevel 2 goto end
%PS% -Path "%FOLDER%" -Factor %FACTOR%
goto end
:undo
%PS% -Path "%FOLDER%" -Undo
goto end
:usage
echo Drag a folder with the tire XML files onto ScaleTireGrip.bat.
:end
echo.
pause
