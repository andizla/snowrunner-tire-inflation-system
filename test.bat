@echo off
rem Runs the offline tests build.bat made: the loader on its own, the loader with a second copy of itself as its
rem chain file, the loader behind another ASI loader that had the .asi first, and the mod's own checks (ini round
rem trip, flattening and gear numbers, the write breakpoint probe) and the overlay's finding of the swap chains
rem (overlay_test.exe). With SR_IMAGE set to a copy of the game exe's
rem image from a running game, the mod's search through the game's code is checked on it too.
cd /d "%~dp0"
set failed=0
out\test\loader_test.exe || set failed=1
out\test\chain\loader_test.exe || set failed=1
out\test\twice\loader_test.exe already || set failed=1
echo the game's log> out\test\helper\AsiLoader.log
out\test\helper\crash_reporter.exe helper || set failed=1
out\test\probe_test.exe || set failed=1
out\test\overlay\overlay_test.exe || set failed=1
if %failed%==1 (echo TESTS FAILED & exit /b 1)
echo TESTS OK
