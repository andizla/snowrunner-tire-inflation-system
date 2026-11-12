@echo off
rem Runs the offline tests build.bat made: the loader on its own, the loader with a second copy of itself as its
rem chain file, the loader behind another ASI loader that had the .asi first, the loader with a System32 version.dll
rem that lacks an export (as Wine's), and the mod's own checks (ini round
rem trip, flattening and gear numbers, the write breakpoint probe) and the overlay's finding of the swap chains
rem (overlay_test.exe). With SR_IMAGE set to a copy of the game exe's
rem image from a running game, the mod's search through the game's code is checked on it too.
rem   test.bat [serial]
rem The tests are the lines of tools\test.jobs. tools\run_jobs.py runs those that share nothing side by side and the
rem others in a row, and prints their output in the list's order; every test runs, also after one failed, and one that
rem does not end in 300 s is killed and fails. "test.bat serial" runs the same lines one at a time. Each test's output
rem is also in out\test\log, with the time each took in times.txt.
cd /d "%~dp0"
set MODE=
if /i "%~1"=="serial" set MODE=--serial
python -I tools\run_jobs.py tools\test.jobs --log out\test\log --timeout 300 %MODE%
if errorlevel 1 goto failed
echo TESTS OK
exit /b 0
:failed
echo TESTS FAILED
exit /b 1
