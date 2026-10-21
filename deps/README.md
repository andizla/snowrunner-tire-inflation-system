# Third-party code the mod is built with

`imgui` holds the sources of Dear ImGui 1.92.1 (https://github.com/ocornut/imgui), MIT, see `imgui\LICENSE.txt`. They are compiled into the mod: Next Gen's own overlay (`src\ng_overlay.cpp`) draws with them on the game's DirectX 11 device, through `src\ng_dx11.cpp`, Dear ImGui's DirectX 11 renderer with one change for pictures in linear light (HDR). `imconfig.h` turns Dear ImGui's checks into no-ops, so a failed check cannot end the game, and leaves out the demo windows and the Windows input method calls; those lines are marked [NEXT GEN].

`reshade\include` holds the add-on API headers of ReShade 6.8.0 (add-on API version 20), BSD-3-Clause, see `reshade\LICENSE.md`. The mod needs no ReShade. Where ReShade is installed, `src\ng_reshade.cpp` registers with it only to be handed the game's swap chain at its first present, as a DirectX probe of the mod's own would run into ReShade setting up its hooks at start.

`stb` holds `imstb_truetype.h` (stb_truetype 1.26) and `imstb_rectpack.h` (stb_rect_pack 1.01) as Dear ImGui ships them (warning fixes only, marked [DEAR IMGUI]), public domain or MIT, see the end of each file. Dear ImGui builds its font atlas with them, and the mod bakes the glyph atlas of its panel with them from the game's own font files, which are read from the game folder at run time and never shipped.

`ng_overlay.cpp`, `ng_overlay.h`, `ng_dx11.cpp`, `ng_dx11.h`, `ng_reshade.cpp` and `ng_reshade.h` in `src` are the same files in every Next Gen mod (Next Gen Gameplay, Next Gen Tire Inflation): keep them alike, as the mods in one game share the NEXT GEN menu.
