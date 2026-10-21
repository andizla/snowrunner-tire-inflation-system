// ng_reshade.h: where ReShade is installed, Next Gen's overlay takes the game's swap chain from ReShade's add-on
// events instead of making a DirectX probe (ng_reshade.cpp says why).
#pragma once
#include <windows.h>
#include <dxgi.h>

// Registers the mod with ReShade and calls found once, on the render thread, with the game's own swap chain (the
// native one under ReShade's) at its first present. false: no ReShade with add-on support is loaded.
bool NgReShadeWatch(HMODULE self, void (*found)(IDXGISwapChain *));
// ReShade's module where one is in the game (it may not have taken the add-on), else nullptr.
HMODULE NgReShadeModule();
