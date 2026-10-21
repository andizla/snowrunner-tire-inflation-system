// ng_reshade.cpp: where ReShade is installed, Next Gen's overlay gets the game's swap chain from ReShade instead of
// making a DirectX probe of its own. A probe's swap chain, even on the WARP device ReShade skips, goes through ReShade's
// hooks, and made on another thread while the game starts it ran into ReShade setting up its own: the game quit a few
// seconds later, at the next hook ReShade put in, its DirectInput one (2026-10-21, ReShade 6.8; the same with the
// Spotter minimap mod's probe). Through ReShade's add-on events nothing is made: the first present hands over the
// game's own swap chain, the native one under ReShade's, whose method table the overlay hooks as it would the probe's,
// so the overlay draws after ReShade and its add-ons (Luma's HDR among them) on the picture that goes to the screen.
// A file of its own because reshade.hpp brings in ReShade's Dear ImGui function table, for another ImGui version.
#include <windows.h>
#include <dxgi.h>
#include <reshade.hpp>
#include "ng_reshade.h"

static void (*g_found)(IDXGISwapChain *) = nullptr;
static volatile LONG g_handed = 0;

static void OnPresent(reshade::api::command_queue *, reshade::api::swapchain *sc, const reshade::api::rect *, const reshade::api::rect *, uint32_t, const reshade::api::rect *)
{
    if (g_handed || !sc || sc->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    if (InterlockedExchange(&g_handed, 1)) return;
    g_found((IDXGISwapChain *)sc->get_native());
}

bool NgReShadeWatch(HMODULE self, void (*found)(IDXGISwapChain *))
{
    g_found = found;
    if (!reshade::register_addon(self)) return false;
    reshade::register_event<reshade::addon_event::present>(&OnPresent);
    return true;
}
