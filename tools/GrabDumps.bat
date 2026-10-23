@echo off
setlocal EnableExtensions
rem GrabDumps.bat: puts what a crash report for the Next Gen mods needs into one zip beside this file.
rem It goes in the game's Bin folder (next to SnowRunner.exe). Run it after a crash, before starting the game again:
rem the game's crash dump is overwritten by the next crash, and the logs by the next start.
rem Paths are never used inside parentheses or unquoted: "Program Files (x86)" would end a block early, a "&" split a line.

cd /d "%~dp0"
tasklist /FI "IMAGENAME eq SnowRunner.exe" 2>nul | find /I "SnowRunner.exe" >nul
if not errorlevel 1 goto running

for /f "usebackq delims=" %%t in (`powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH-mm-ss"`) do set "STAMP=%%t"
if not defined STAMP set "STAMP=latest"
set "T=%TEMP%\NextGen_dumps_%STAMP%"
set "ZIP=%~dp0NextGen_dumps_%STAMP%.zip"
if exist "%T%" rd /s /q "%T%"
md "%T%"

rem the game's crash dump
if exist "%LOCALAPPDATA%\Saber\CrashDump.dmp" copy /y "%LOCALAPPDATA%\Saber\CrashDump.dmp" "%T%\" >nul

rem the mods' logs and settings, the loader's log, ReShade's and the manual transmission mod's logs
for %%f in (AsiLoader.log Gauges.log TirePressure.log ReShade.log SMT_LOG.txt Gauges.ini TirePressure.ini NextGen.ini) do if exist "%%f" copy /y "%%f" "%T%\" >nul

rem Gauges keeps each earlier start's log in Gauges\logs: the newest 3
set /a N=0
if exist "Gauges\logs\*.log" md "%T%\Gauges logs"
for /f "delims=" %%f in ('dir /b /a-d /o-d "Gauges\logs\*.log" 2^>nul') do call :older "%%f"

rem what is in the game folder, and when the crash dump was written
> "%T%\files.txt" echo Game folder:
cd >> "%T%\files.txt"
>> "%T%\files.txt" echo.
dir /a-d /o:n *.dll *.asi *.addon *.addon64 *.ini *.off >> "%T%\files.txt" 2>nul
>> "%T%\files.txt" echo.
>> "%T%\files.txt" echo The game's crash dump:
dir /a-d "%LOCALAPPDATA%\Saber\*.dmp" >> "%T%\files.txt" 2>&1

powershell -NoProfile -Command "Compress-Archive -Path (Join-Path $env:T '*') -DestinationPath $env:ZIP -Force"
if exist "%ZIP%" goto done
rem the game folder could not be written: the desktop instead
set "ZIP=%USERPROFILE%\Desktop\NextGen_dumps_%STAMP%.zip"
powershell -NoProfile -Command "Compress-Archive -Path (Join-Path $env:T '*') -DestinationPath $env:ZIP -Force"
if exist "%ZIP%" goto done
echo.
echo The zip could not be written. The files are in:
echo "%T%"
pause
exit /b 1

:done
rd /s /q "%T%"
echo.
echo Saved:
echo "%ZIP%"
echo.
echo Send that zip.
pause
exit /b 0

:running
echo SnowRunner is still running. Close it first, then run this again.
pause
exit /b 1

:older
if %N% geq 3 goto :eof
copy /y "Gauges\logs\%~1" "%T%\Gauges logs\" >nul
set /a N+=1
goto :eof
