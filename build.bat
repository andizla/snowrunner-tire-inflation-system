@echo off
rem Builds out\version.dll (forwards version.dll, loads the .asi files next to the exe) and its offline test in
rem out\test (loader_test.exe, probe.asi, a copy of version.dll; the same again in out\test\chain with a second copy
rem of the proxy as version_chain.dll, and in out\test\twice with a stand-in for another mod's ASI loader as
rem version_chain.dll). x64, static CRT, Visual Studio 2022 Community. test.bat runs the tests.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\obj mkdir out\obj
if not exist out\test mkdir out\test
if not exist out\test\chain mkdir out\test\chain
if not exist out\test\twice mkdir out\test\twice
rem the loader test loads every .asi in its folder: only this build's probe.asi may be there
del /q out\test\*.asi out\test\chain\*.asi out\test\twice\*.asi 2>nul
ml64 /nologo /c /Fo out\obj\version_stubs.obj src\version_stubs.asm || goto fail
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /Fo:out\obj\loader.obj src\loader.cpp || goto fail
link /nologo /DLL /DEF:src\version.def /OUT:out\version.dll /IMPLIB:out\obj\version_proxy.lib out\obj\version_stubs.obj out\obj\loader.obj || goto fail
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /Fo:out\obj\ /Fe:out\test\loader_test.exe test\loader_test.cpp /link version.lib || goto fail
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /LD /D LOADER_TEST_ASI /Fo:out\obj\probe.obj /Fe:out\test\probe.asi test\loader_test.cpp || goto fail
copy /y out\version.dll out\test\version.dll >nul || goto fail
copy /y out\version.dll out\test\chain\version.dll >nul || goto fail
copy /y out\version.dll out\test\chain\version_chain.dll >nul || goto fail
copy /y out\test\loader_test.exe out\test\chain\loader_test.exe >nul || goto fail
copy /y out\test\probe.asi out\test\chain\probe.asi >nul || goto fail
rem another mod's ASI loader as the chain file: it loads probe.asi first, and the proxy has to leave that alone
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /LD /D LOADER_TEST_CHAIN /Fo:out\obj\chain_loader.obj /Fe:out\test\twice\version_chain.dll test\loader_test.cpp || goto fail
copy /y out\version.dll out\test\twice\version.dll >nul || goto fail
copy /y out\test\loader_test.exe out\test\twice\loader_test.exe >nul || goto fail
copy /y out\test\probe.asi out\test\twice\probe.asi >nul || goto fail
rem the game's crash reporter loads version.dll too: the test under its name wants no .asi in it and the game's log kept
if not exist out\test\helper mkdir out\test\helper
copy /y out\version.dll out\test\helper\version.dll >nul || goto fail
copy /y out\test\loader_test.exe out\test\helper\crash_reporter.exe >nul || goto fail
copy /y out\test\probe.asi out\test\helper\probe.asi >nul || goto fail
rem the mod itself, and the read-only tools for the running game
rem Dear ImGui (deps\imgui) and its DirectX 11 renderer (src\ng_dx11.cpp) compiled in; ng_overlay.cpp is Next Gen's overlay
rem (the Present hook, the NEXT GEN menu), ng_reshade.cpp takes the swap chain from ReShade where it is installed;
rem panel.cpp draws the pressure panel and the Tire Inflation tab with it, its text in the game's font (deps\stb)
cl /nologo /c /O2 /W3 /EHsc /std:c++17 /utf-8 /MT /I deps\imgui /I deps\stb /Fo:out\obj\ deps\imgui\imgui.cpp deps\imgui\imgui_draw.cpp deps\imgui\imgui_tables.cpp deps\imgui\imgui_widgets.cpp src\ng_dx11.cpp || goto fail
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I deps\imgui /I deps\stb /Fo:out\obj\ src\ng_overlay.cpp src\panel.cpp || goto fail
cl /nologo /c /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I deps\reshade\include /Fo:out\obj\ng_reshade.obj src\ng_reshade.cpp || goto fail
set OVERLAY_OBJ=out\obj\panel.obj out\obj\ng_overlay.obj out\obj\ng_dx11.obj out\obj\imgui.obj out\obj\imgui_draw.obj out\obj\imgui_tables.obj out\obj\imgui_widgets.obj out\obj\ng_reshade.obj
rem the sounds the mod's file carries (src\sounds.rc: assets\air_in.wav)
rc /nologo /fo out\obj\sounds.res src\sounds.rc || goto fail
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /LD /I deps\imgui /Fo:out\obj\tire_pressure.obj /Fe:out\TirePressure.asi src\tire_pressure.cpp %OVERLAY_OBJ% out\obj\sounds.res /link user32.lib ole32.lib d3d11.lib d3dcompiler.lib || goto fail
cl /nologo /O2 /W4 /EHsc /MT /Fo:out\obj\ /Fe:out\wheelscan.exe tools\wheelscan.cpp || goto fail
cl /nologo /O2 /W4 /EHsc /MT /Fo:out\obj\ /Fe:out\ptrscan.exe tools\ptrscan.cpp || goto fail
rem the mod's write breakpoint probe as a program of its own (TP_PROBE_TEST adds a main to the same source)
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /I deps\imgui /D TP_PROBE_TEST /Fo:out\obj\probe_test.obj /Fe:out\test\probe_test.exe src\tire_pressure.cpp %OVERLAY_OBJ% out\obj\sounds.res /link user32.lib ole32.lib d3d11.lib d3dcompiler.lib || goto fail
rem the overlay's own test (test\overlay_test.cpp): two copies of a stand-in mod made of the shared overlay files, in a
rem program that makes its swap chains through its imports as the game does
if not exist out\test\overlay mkdir out\test\overlay
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /LD /D OVERLAY_TEST_MOD /I deps\imgui /I src /Fo:out\obj\overlay_mod.obj /Fe:out\test\overlay\ng_mod_a.dll test\overlay_test.cpp out\obj\ng_overlay.obj out\obj\ng_dx11.obj out\obj\imgui.obj out\obj\imgui_draw.obj out\obj\imgui_tables.obj out\obj\imgui_widgets.obj out\obj\ng_reshade.obj /link user32.lib ole32.lib d3d11.lib d3dcompiler.lib dxgi.lib || goto fail
copy /y out\test\overlay\ng_mod_a.dll out\test\overlay\ng_mod_b.dll >nul || goto fail
cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 /MT /Fo:out\obj\overlay_test.obj /Fe:out\test\overlay\overlay_test.exe test\overlay_test.cpp /link user32.lib d3d11.lib dxgi.lib dinput8.lib dxguid.lib || goto fail
echo BUILD OK: out\version.dll, out\TirePressure.asi, out\test\loader_test.exe
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
