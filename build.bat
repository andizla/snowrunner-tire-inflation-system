@echo off
rem Builds out\version.dll (forwards version.dll, loads the .asi files next to the exe) and its offline test in
rem out\test (loader_test.exe, probe.asi, a copy of version.dll; the same again in out\test\chain with a second copy
rem of the proxy as version_chain.dll, and in out\test\twice with a stand-in for another mod's ASI loader as
rem version_chain.dll), the mod out\TirePressure.asi and its tests. x64, static CRT, Visual Studio 2022 Community.
rem test.bat runs the tests.
rem   build.bat [serial]
rem The jobs are the lines of tools\build.jobs. tools\run_jobs.py (python, the standard library) runs them side by side,
rem as many as the machine has cores less one (set JOBS=n for another count), and prints their output in the list's
rem order. "build.bat serial" runs the same lines one at a time. Every job is run each time: nothing is skipped by a
rem file's date. Each job's output is also in out\log\build, with the time each took in times.txt.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
set MODE=
if /i "%~1"=="serial" set MODE=--serial
if not exist out mkdir out
if not exist out\obj mkdir out\obj
if not exist out\test mkdir out\test
if not exist out\test\chain mkdir out\test\chain
if not exist out\test\twice mkdir out\test\twice
if not exist out\test\helper mkdir out\test\helper
if not exist out\test\proton mkdir out\test\proton
if not exist out\test\proton_helper mkdir out\test\proton_helper
if not exist out\test\overlay mkdir out\test\overlay
rem the loader test loads every .asi in its folder: only this build's probe.asi may be there
del /q out\test\*.asi out\test\chain\*.asi out\test\twice\*.asi out\test\proton\*.asi out\test\proton_helper\*.asi 2>nul
set OVERLAY_OBJ=out\obj\panel.obj out\obj\ng_overlay.obj out\obj\ng_dx11.obj out\obj\imgui.obj out\obj\imgui_draw.obj out\obj\imgui_tables.obj out\obj\imgui_widgets.obj out\obj\ng_reshade.obj
python -I tools\run_jobs.py tools\build.jobs --log out\log\build --stop %MODE%
if errorlevel 1 goto fail
echo BUILD OK: out\version.dll, out\TirePressure.asi, out\test\loader_test.exe
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
