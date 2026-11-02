/* spotter_api.h - "draw through Spotter": a plain C API exported by Spotter.asi (SnowRunner minimap / HUD mod by
   SnowPothos) so another .asi / .dll mod can draw every frame through Spotter's DXGI Present hook instead of a
   ReShade add-on. Public domain / CC0: copy this header into your mod as is.

   - Plain C, __cdecl, exported undecorated from Spotter.asi. Resolve with GetProcAddress (never link an import lib):
       HMODULE s = GetModuleHandleW(L"Spotter.asi");
       SpotterApiVersionFn ver = (SpotterApiVersionFn)GetProcAddress(s, "SpotterApiVersion");
   - No Dear ImGui context is shared. Your mod keeps its own ImGui (or any renderer) and draws with the D3D11 objects
     handed to your callback.
   - Stable ABI: names, signatures and the meaning of fields never change within a major SPOTTER_API_VERSION. New
     fields are only ever appended to SpotterPresentInfo; check info->size before reading a field newer than v1.
   - Threads: Register / Unregister / RequestInput / InputOwner may be called from any thread. Your callback runs on
     the game's render thread (inside IDXGISwapChain::Present, before the frame is shown).

   Compiles as C or C++ without d3d11.h: the COM pointers are void*. Cast them to the named type:
     swapChain      IDXGISwapChain*           (the game's swap chain)
     device         ID3D11Device*             (the game's device)
     context        ID3D11DeviceContext*      (its immediate context)
     backbufferRtv  ID3D11RenderTargetView*   (back buffer 0, a non-sRGB (UNORM) view: same colours as ReShade's
                                               non-sRGB back-buffer target / ImGui's defaults)
     backBuffer     ID3D11Texture2D*          (back buffer 0 itself)
     window         HWND                      (the game window)
   All of them are BORROWED and valid only during the callback: do not Release them and do not keep them for a later
   frame (AddRef + keep = the game cannot resize its swap chain). Spotter makes a fresh view every frame, so a
   window / resolution change (ResizeBuffers) needs nothing from you. */
#ifndef SPOTTER_API_H
#define SPOTTER_API_H

#include <stdint.h>

#define SPOTTER_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(SPOTTER_CDECL)
#define SPOTTER_CDECL __cdecl
#elif !defined(SPOTTER_CDECL)
#define SPOTTER_CDECL
#endif

/* flags in SpotterPresentInfo */
#define SPOTTER_PRESENT_AFTER_SPOTTER_UI 0x1u   /* this call is after Spotter's own UI (order >= 0) */
#define SPOTTER_PRESENT_SPOTTER_UI_ON    0x2u   /* Spotter's own UI is drawn this frame (it may be off / not up yet) */

typedef struct SpotterPresentInfo {
    uint32_t size;          /* sizeof(SpotterPresentInfo) as filled by Spotter; read newer fields only if size covers them */
    uint32_t apiVersion;    /* SPOTTER_API_VERSION of the running Spotter.asi */
    void* swapChain;        /* IDXGISwapChain* */
    void* device;           /* ID3D11Device* */
    void* context;          /* ID3D11DeviceContext* (immediate) */
    void* backbufferRtv;    /* ID3D11RenderTargetView* on back buffer 0 (may be NULL in a rare failed frame: skip drawing then) */
    uint32_t width;         /* back buffer size in pixels */
    uint32_t height;
    uint64_t frame;         /* Spotter's frame counter, +1 per presented frame */
    /* --- v1 extras --- */
    void* backBuffer;       /* ID3D11Texture2D* back buffer 0 */
    void* window;           /* HWND of the game window */
    uint32_t rtvFormat;     /* DXGI_FORMAT of backbufferRtv */
    uint32_t flags;         /* SPOTTER_PRESENT_* */
} SpotterPresentInfo;

/* Called once per presented frame. Spotter saves the whole D3D11 pipeline state before the call, hands you a cleared
   (default) state, and puts the game's state back afterwards: set what you need (ImGui_ImplDX11 does), restore nothing.
   A callback that crashes (access violation etc.) is switched off for the rest of the session, logged once in
   Spotter.log with its module name; the other callbacks and the game keep running. */
typedef void (SPOTTER_CDECL *SpotterPresentFn)(const SpotterPresentInfo* info, void* user);

/* SpotterApiVersion: always works (even when Spotter cannot draw). Returns SPOTTER_API_VERSION. */
typedef uint32_t (SPOTTER_CDECL *SpotterApiVersionFn)(void);

/* SpotterPresentAvailable: 0 = Spotter will NOT call present callbacks (its Present hook failed, a second copy of
   Spotter, shutting down): use your own fallback (ReShade). 1 = set up, waiting for the game's first frame (the
   hook is installed once the game window is up; callbacks start then). 2 = running (frames are being drawn).
   Treat any non-zero value as "use Spotter". */
typedef int32_t (SPOTTER_CDECL *SpotterPresentAvailableFn)(void);

/* SpotterRegisterPresent: order < 0 = drawn BEFORE Spotter's own UI (under the minimap / F7 menu), order >= 0 =
   AFTER it (on top). Lower order first; equal order = registration order. May be called before the game has a
   swap chain: the callback starts with the first frame. Returns a non-zero handle, or 0 (fn NULL, too many (64),
   shutting down). One module may register several callbacks. */
typedef uint64_t (SPOTTER_CDECL *SpotterRegisterPresentFn)(SpotterPresentFn fn, void* user, int32_t order);

/* SpotterUnregisterPresent: removes it; safe from inside any callback (takes effect at once: it is not called again,
   also not later in the same frame) and from your DllMain(DLL_PROCESS_DETACH) / shutdown. When called from another
   thread while the render thread is inside THIS callback, waits for that call to end (at most 250 ms; never waits
   on the render thread itself). Unknown / 0 handles are ignored. Releases its input claim. */
typedef void (SPOTTER_CDECL *SpotterUnregisterPresentFn)(uint64_t handle);

/* Input owner, for all mods to agree who handles the mouse / keyboard right now:
   0 = the game, 1 = Spotter's UI (F7 menu, Edit Mode, big map, Spotter main menu overlay),
   2 = a registered mod (some mod called SpotterRequestInput with a non-zero mask). */
#define SPOTTER_INPUT_GAME    0
#define SPOTTER_INPUT_SPOTTER 1
#define SPOTTER_INPUT_MOD     2
typedef int32_t (SPOTTER_CDECL *SpotterInputOwnerFn)(void);

/* SpotterRequestInput: claim (mask != 0) or release (0) input for a registered handle, e.g. while your window is open.
   mask: SPOTTER_WANT_MOUSE | SPOTTER_WANT_KEYS. While any mod holds a claim (and Spotter's own UI is closed):
     blocked from the GAME: mouse (raw input, DirectInput mouse, cursor re-centring SetCursorPos / ClipCursor)
       with SPOTTER_WANT_MOUSE; DirectInput keyboard and raw-input keyboard with SPOTTER_WANT_KEYS
       (exactly Spotter's own menu blocking);
     Spotter's hotkeys (F7, big map, minimap toggle, Edit Mode Ctrl+F7, pad combos) do nothing;
     window messages (WM_MOUSE*, WM_KEY*, WM_CHAR) are NOT eaten by Spotter: they go on down the window chain, so
       your own WndProc hook (before or after Spotter's) still gets them; XInput pads are not blocked.
   Spotter's UI wins: while it is open SpotterInputOwner() is 1 and your claim waits (kept, applied when it closes).
   A claim is dropped when the handle is unregistered or its callback crashed. Returns the input owner after the
   call (0/1/2), or -1 for an unknown handle. */
#define SPOTTER_WANT_MOUSE 0x1
#define SPOTTER_WANT_KEYS  0x2
typedef int32_t (SPOTTER_CDECL *SpotterRequestInputFn)(uint64_t handle, int32_t wantsMouseOrKeys);

#if defined(SPOTTER_API_EXPORTS)   /* Spotter.asi itself */
#define SPOTTER_API_DECL __declspec(dllexport)
#else
#define SPOTTER_API_DECL __declspec(dllimport)   /* prefer GetProcAddress; declared for completeness */
#endif

#if defined(SPOTTER_API_EXPORTS) || defined(SPOTTER_API_DECLARE_IMPORTS)
SPOTTER_API_DECL uint32_t SPOTTER_CDECL SpotterApiVersion(void);
SPOTTER_API_DECL int32_t  SPOTTER_CDECL SpotterPresentAvailable(void);
SPOTTER_API_DECL uint64_t SPOTTER_CDECL SpotterRegisterPresent(SpotterPresentFn fn, void* user, int32_t order);
SPOTTER_API_DECL void     SPOTTER_CDECL SpotterUnregisterPresent(uint64_t handle);
SPOTTER_API_DECL int32_t  SPOTTER_CDECL SpotterInputOwner(void);
SPOTTER_API_DECL int32_t  SPOTTER_CDECL SpotterRequestInput(uint64_t handle, int32_t wantsMouseOrKeys);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SPOTTER_API_H */
