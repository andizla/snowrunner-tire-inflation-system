// ng_overlay.h: Next Gen's own overlay. A mod draws what it shows over the game (a gauge cluster, a panel) and its tab
// of the NEXT GEN menu with Dear ImGui, compiled into the mod, on the game's own DirectX 11 device. No ReShade is
// needed: the overlay takes the game's imports of CreateDXGIFactory1 and D3D11CreateDevice and gives every swap chain
// the game makes a method table of its own with the overlay's Present, which calls on to what the table held (DirectX,
// the Steam overlay's hook on it, ReShade). DirectX's own tables stay as they are (ng_overlay.cpp says why). With
// ReShade installed the swap chains come from ReShade's presents instead (ng_reshade.cpp).
// The same two files (ng_overlay.h, ng_overlay.cpp) are in every Next Gen mod (Next Gen Gameplay, Next Gen Tire
// Inflation). The mods in one game share the menu through a block of named shared memory: one window with a tab per
// mod, opened and closed with one key (Delete unless NextGen.ini next to the game's exe says otherwise; the menu sets it
// too), drawn by the mod whose tab is chosen.
#pragma once
#include <windows.h>
#include "imgui.h"

struct NgClient
{
    const char *tab;                      // the mod's tab in the NEXT GEN menu, such as "Gameplay"
    const char *version;                  // shown at the top of the tab, such as "0.16.0"
    int order;                            // where its tab goes among the others, lowest first
    void (*log)(const wchar_t *fmt, ...); // the mod's log
    void (*frame)();                      // every frame: what the mod always draws, into ImGui's background or foreground draw list
    void (*page)();                       // the tab's body, on the frames it shows
    void (*deviceGone)();                 // the game's device is going: free every texture NgTextureMake made
};

// Starts the overlay: takes the game's imports (or ReShade's events) that bring its swap chains, so call it before the
// game makes its DirectX device. false: it cannot start (in the log).
bool NgOverlayStart(const NgClient &client);
// A texture of w x h pixels, 8-bit RGBA, on the game's device, for ImDrawList::PushTexture; 0 when none was made.
// Only from frame() or page().
ImTextureID NgTextureMake(const void *rgba, int w, int h);
void NgTextureFree(ImTextureID tex);
// Whether the NEXT GEN menu is open, on any tab.
bool NgMenuOpen();
// The menu's key, a virtual key code; 0 leaves it. Saved in NextGen.ini, which every Next Gen mod reads at start.
void NgMenuKeySet(int vk);
// The theme's colours, for a mod's own drawing: the logo's orange (top, middle, bottom of its gradient) and the greys.
static const ImU32 kNgOrangeTop = IM_COL32(255, 203, 57, 255), kNgOrange = IM_COL32(254, 158, 32, 255), kNgOrangeLow = IM_COL32(236, 103, 19, 255);
static const ImU32 kNgGreyDeep = IM_COL32(20, 20, 22, 255), kNgGrey = IM_COL32(42, 42, 46, 255), kNgGreyLight = IM_COL32(74, 74, 80, 255);
