// panel.cpp: the Tire Inflation System panel and the gamepad side of TirePressure.asi.
// The panel is drawn from ReShade's reshade_overlay add-on event, which ReShade runs on every frame once an add-on
// listens to it. ReShade owns the swap chain and Dear ImGui and hands add-ons the ImGui functions through a table, so
// only headers are compiled in (deps\reshade, deps\imgui; their versions must match the installed ReShade). Without
// ReShade the mod still works, with F3 cycling the modes directly.
// The drawing is in panel_draw.h, shared with the offline preview (test\panel_preview.cpp): Expeditions' panel, its
// text in the game's font from a glyph atlas that this file makes a texture of on ReShade's device. The overlay also
// gets a Tire Inflation System tab (settings_page.h) that edits TirePressure.ini in the game. The name is the one
// Expeditions gives the system in its own menus, so players know it.
#include <windows.h>
#include <xinput.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#define ImTextureID ImU64 // ReShade expects 64-bit texture handles
#include <imgui.h>
#include <reshade.hpp>
#include "panel.h"
#include "panel_draw.h"
#include "settings_page.h"

// ReShade lists the add-on under these.
extern "C" __declspec(dllexport) const char *NAME = "Tire Inflation System";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Expeditions' tire inflation system for SnowRunner: the pressure panel of TirePressure.asi.";

PanelView g_view = { 0, kNormal, kNormal, -1, 0, kModeCount };
PanelMode g_panelModes[kModeCount] = { { "LOW PRESSURE", 1, 1, 1, 1 }, { "REDUCED PRESSURE", 1, 1, 1, 1 }, { "NORMAL PRESSURE", 1, 1, 1, 1 },
                                       { "INCREASED PRESSURE", 1, 1, 1, 1 } };
float g_panelScale = 1.0f;
char g_panelKeyName[16] = "F3";
int g_panelConfirmMs = 2000;
volatile LONG g_pageCapturing = 0; // GetTickCount() of the last page frame that waited for pad buttons, 0 = none
volatile LONG g_modOff = 0;

// The game's font as a texture on ReShade's device: baked for the screen's scale the first time the panel shows,
// again when the scale changes, freed with the device.
static PanelFonts g_fonts;
static bool g_fontFiles = false;
static float g_fontFailedAt = 0.0f; // the scale a bake failed at: not tried again until the scale changes
static reshade::api::device *g_fontDevice = nullptr;
static reshade::api::resource g_fontTex = {};
static reshade::api::resource_view g_fontView = {};

static void FreeFontTexture()
{
    if (g_fontDevice)
    {
        if (g_fontView.handle) g_fontDevice->destroy_resource_view(g_fontView);
        if (g_fontTex.handle) g_fontDevice->destroy_resource(g_fontTex);
    }
    g_fontDevice = nullptr;
    g_fontTex = {};
    g_fontView = {};
    g_fonts.tex = 0;
}

static void UpdateFontTexture(reshade::api::effect_runtime *runtime, float u)
{
    using namespace reshade::api;
    device *dev = runtime->get_device();
    if (!g_fontFiles || (g_fonts.tex && g_fontDevice == dev && fabsf(g_fonts.u - u) < 0.001f)) return;
    if (!g_fonts.tex && g_fontFailedAt > 0.0f && fabsf(g_fontFailedAt - u) < 0.001f) return;
    FreeFontTexture();
    if (PanelFontsBake(g_fonts, u))
    {
        const subresource_data data = { g_fonts.rgba, (uint32_t)g_fonts.w * 4, (uint32_t)g_fonts.w * (uint32_t)g_fonts.h * 4 };
        const resource_desc desc((uint32_t)g_fonts.w, (uint32_t)g_fonts.h, 1, 1, format::r8g8b8a8_unorm, 1, memory_heap::default_, resource_usage::shader_resource);
        if (dev->create_resource(desc, &data, resource_usage::shader_resource, &g_fontTex))
        {
            if (dev->create_resource_view(g_fontTex, resource_usage::shader_resource, resource_view_desc(format::r8g8b8a8_unorm), &g_fontView))
            {
                g_fontDevice = dev;
                g_fonts.tex = g_fontView.handle;
            }
            else
            {
                dev->destroy_resource(g_fontTex);
                g_fontTex = {};
            }
        }
        free(g_fonts.rgba);
        g_fonts.rgba = nullptr;
    }
    if (g_fonts.tex)
    {
        g_fontFailedAt = 0.0f;
        PanelLog(L"panel: game font baked at %.3f px per reference px (%dx%d atlas)", u, g_fonts.w, g_fonts.h);
    }
    else
    {
        // not again every frame: ReShade's font stands in until the panel's size changes (a very large panel does not
        // fit the atlas; a smaller one will again)
        g_fontFailedAt = u;
        PanelLog(L"panel: the game font did not become a texture at %.3f px per reference px, ReShade's font stands in", u);
    }
}

static void OnDestroyDevice(reshade::api::device *device)
{
    if (device == g_fontDevice) FreeFontTexture();
}

static void OnOverlay(reshade::api::effect_runtime *runtime)
{
    static float alpha = 0.0f, needle = 0.0f, warn = 0.0f;
    if (g_modOff) return;
    g_overlayThread = (LONG)GetCurrentThreadId();
    ImGuiIO &io = ImGui::GetIO();
    const float u = PanelUnit(io.DisplaySize.x, io.DisplaySize.y, g_panelScale);
    // baked on the first frames (the game's loading screen) rather than when the panel first opens, so that never waits
    if (io.DisplaySize.y >= 240.0f) UpdateFontTexture(runtime, u);
    const float dt = fminf(fmaxf(io.DeltaTime, 0.0f), 0.1f);
    const bool open = g_view.open != 0;
    alpha = fminf(fmaxf(alpha + (open ? dt : -dt) / 0.15f, 0.0f), 1.0f);
    warn = fminf(fmaxf(warn + (g_view.warn ? dt : -dt) / 0.25f, 0.0f), 1.0f);
    const ImVec2 at = PanelOrigin(io.DisplaySize.x, io.DisplaySize.y);
    // the tire damage warning shows while the panel is closed
    if (warn * (1.0f - alpha) > 0.0f)
        PanelWarning(ImGui::GetForegroundDrawList(), g_fonts, PanelWarnOrigin(io.DisplaySize.x, io.DisplaySize.y), u, warn * (1.0f - alpha), (int)g_view.wear);
    const int sel = (int)g_view.selected, cur = (int)g_view.current;
    if (alpha <= 0.0f) { needle = (float)cur; return; }
    needle += ((float)sel - needle) * fminf(dt * 12.0f, 1.0f);
    PanelDraw(ImGui::GetForegroundDrawList(), g_fonts, at, u, alpha, needle, g_view, g_panelModes, g_panelKeyName, g_panelConfirmMs);
}

// The Tire Inflation System tab: the page edits a copy of the settings in force and hands it back when something changed.
static void OnSettingsPage(reshade::api::effect_runtime *)
{
    if (g_modOff)
    {
        ImGui::TextWrapped("The mod cannot work on this version of the game and is not running. TirePressure.log in the game folder says what it did not find.");
        return;
    }
    static PageState st;
    TpSettings s;
    TpLive live;
    SettingsGet(s);
    LiveGet(live);
    // the pad is read on every frame of the page: the frame a binding's button is pressed on has to know that the
    // pad's A, which pressed it, is still down
    const int act = SettingsPage(s, live, st, PadButtons());
    if (act & kPageChanged) SettingsPut(s);
    if (act & kPageReload) SettingsReload();
}

bool PanelInit(HMODULE self)
{
    if (!reshade::register_addon(self)) return false;
    // the game's font files sit next to the exe
    wchar_t dir[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t *slash = n && n < MAX_PATH ? wcsrchr(dir, L'\\') : nullptr;
    if (slash) *slash = 0;
    g_fontFiles = slash && PanelFontsLoad(g_fonts, dir);
    PanelLog(g_fontFiles ? L"panel: game font read (TT Lakes Compressed)" : L"panel: game font not found, ReShade's font stands in");
    reshade::register_event<reshade::addon_event::reshade_overlay>(&OnOverlay);
    reshade::register_event<reshade::addon_event::destroy_device>(&OnDestroyDevice);
    reshade::register_overlay("Tire Inflation System", &OnSettingsPage);
    return true;
}

// ---- gamepad ----

typedef DWORD(WINAPI *XInputGetStateFn)(DWORD, XINPUT_STATE *);
static XInputGetStateFn g_realGetState = nullptr;
static volatile LONG g_openMask = 0, g_stepMask[2] = {}, g_panelMask = 0;
static volatile LONG g_padLive = 0; // 1 while a truck is driven: only then are the panel's buttons taken from the game

volatile LONG g_padThread = 0, g_padThreadChanges = 0, g_overlayThread = 0; // which of the game's threads read the pad and present

static DWORD WINAPI PadHook(DWORD index, XINPUT_STATE *state)
{
    // panel buttons still down after the panel closed, hidden until released; per pad, as the game asks for each in
    // turn and another pad's state must not let them through
    static volatile LONG held[XUSER_MAX_COUNT] = {};
    const LONG me = (LONG)GetCurrentThreadId();
    if (g_padThread != me) { InterlockedExchange(&g_padThread, me); InterlockedIncrement(&g_padThreadChanges); }
    const DWORD r = g_realGetState(index, state);
    if (r == ERROR_SUCCESS && state && index < XUSER_MAX_COUNT)
    {
        WORD &b = state->Gamepad.wButtons;
        if (!g_padLive) { InterlockedExchange(&held[index], 0); return r; }
        const LONG still = g_view.open ? g_panelMask : (held[index] & b);
        InterlockedExchange(&held[index], still);
        WORD hide = (WORD)still;
        for (const LONG m : { g_openMask, g_stepMask[0], g_stepMask[1] })
            if (m && (b & m) == m) hide |= (WORD)m;
        b &= (WORD)~hide;
    }
    return r;
}

void PadLive(bool live) { InterlockedExchange(&g_padLive, live ? 1 : 0); }

void PadMasks(uint32_t open, uint32_t lower, uint32_t raise, uint32_t confirm, uint32_t cancel)
{
    // a step binding of one button only acts while the panel is open, so the game keeps that button otherwise
    g_openMask = (LONG)open;
    g_stepMask[0] = PadChord(lower) ? (LONG)lower : 0;
    g_stepMask[1] = PadChord(raise) ? (LONG)raise : 0;
    g_panelMask = (LONG)(open | lower | raise | confirm | cancel);
}

int PadInit(uint64_t exeBase)
{
    HMODULE x = GetModuleHandleW(L"xinput9_1_0.dll");
    const FARPROC real = x ? GetProcAddress(x, "XInputGetState") : nullptr;
    // the game's XInput DLL once the game has loaded it; until then XInput 1.4 serves the settings page
    if (real) g_realGetState = (XInputGetStateFn)real;
    else if (!g_realGetState)
    {
        HMODULE own = LoadLibraryW(L"xinput1_4.dll");
        g_realGetState = own ? (XInputGetStateFn)GetProcAddress(own, "XInputGetState") : nullptr;
    }
    if (!real || !exeBase) return 0;
    // the game loads xinput9_1_0.dll by name and keeps XInputGetState in more than one pointer (this build: its input
    // system's at RVA 0x2af0e80 and a lazily filled one at 0x2ab7bc0): every copy in the image's non-code sections
    // goes to the filter
    int n = 0;
    const BYTE *image = (const BYTE *)exeBase;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(image + ((const IMAGE_DOS_HEADER *)image)->e_lfanew);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        uint64_t *p = (uint64_t *)(image + sec->VirtualAddress), *end = p + sec->Misc.VirtualSize / 8;
        for (; p < end; p++)
        {
            if (*p != (uint64_t)real) continue;
            DWORD old = 0;
            if (!VirtualProtect(p, 8, PAGE_READWRITE, &old)) continue;
            InterlockedExchange64((volatile LONG64 *)p, (LONG64)&PadHook);
            VirtualProtect(p, 8, old, &old);
            n++;
        }
    }
    return n;
}

// The buttons of every connected pad together: the filter above takes the panel's buttons from each pad the game
// reads, so each of them has to be able to work the panel. A slot with no pad is slow to ask, so the empty ones are
// only asked again every two seconds.
uint32_t PadButtons()
{
    static volatile LONG connected = 0xF, askedAt = 0;
    if (!g_realGetState) return 0;
    const DWORD now = GetTickCount();
    const bool all = now - (DWORD)askedAt > 2000;
    if (all) InterlockedExchange(&askedAt, (LONG)now);
    const LONG known = connected;
    LONG answered = 0;
    uint32_t buttons = 0;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++)
    {
        if (!all && !(known & (1L << i))) continue;
        XINPUT_STATE st = {};
        if (g_realGetState(i, &st) != ERROR_SUCCESS) continue;
        answered |= 1L << i;
        buttons |= st.Gamepad.wButtons;
    }
    if (all) InterlockedExchange(&connected, answered);
    return buttons;
}

