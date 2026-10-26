// panel.cpp: the Tire Inflation System panel and the gamepad side of TirePressure.asi (Next Gen Tire Inflation).
// The panel is drawn on every frame by Next Gen's own overlay (ng_overlay.cpp: Dear ImGui compiled into the mod, on the
// game's DirectX 11 device; no ReShade needed, and ReShade where installed is left alone). Should the overlay not
// start, the mod still works, with F3 cycling the modes directly.
// The drawing is in panel_draw.h, shared with the offline preview (test\panel_preview.cpp): Expeditions' panel, its
// text in the game's font from a glyph atlas that this file makes a texture of on the game's device. The overlay also
// gets the Tire Inflation tab of the NEXT GEN menu (settings_page.h), which edits TirePressure.ini in the game; Next Gen
// Gameplay, where installed, has its tab in the same menu. The panel keeps the name Expeditions gives the system in
// its own menus, so players know it.
#include <windows.h>
#include <vector>
#include <xinput.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "ng_overlay.h"
#include "panel.h"
#include "panel_draw.h"
#include "settings_page.h"

PanelView g_view = { 0, kNormal, kNormal, -1, 0, kModeCount };
PanelMode g_panelModes[kModeCount] = { { "LOW PRESSURE", 1, 1, 1, 1 }, { "REDUCED PRESSURE", 1, 1, 1, 1 }, { "NORMAL PRESSURE", 1, 1, 1, 1 },
                                       { "INCREASED PRESSURE", 1, 1, 1, 1 } };
float g_panelScale = 1.0f;
char g_panelKeyName[16] = "F3";
int g_panelConfirmMs = 2000;
volatile LONG g_pageCapturing = 0; // GetTickCount() of the last page frame that waited for pad buttons, 0 = none
volatile LONG g_modOff = 0;

// The game's font as a texture on the game's device: baked for the screen's scale the first time the panel shows,
// again when the scale changes, freed with the device.
static PanelFonts g_fonts;
static bool g_fontFiles = false;
static float g_fontFailedAt = 0.0f; // the scale a bake failed at: not tried again until the scale changes

static void FreeFontTexture()
{
    NgTextureFree(g_fonts.tex);
    g_fonts.tex = 0;
}

static void UpdateFontTexture(float u)
{
    if (!g_fontFiles || (g_fonts.tex && fabsf(g_fonts.u - u) < 0.001f)) return;
    if (!g_fonts.tex && g_fontFailedAt > 0.0f && fabsf(g_fontFailedAt - u) < 0.001f) return;
    FreeFontTexture();
    if (PanelFontsBake(g_fonts, u))
    {
        g_fonts.tex = NgTextureMake(g_fonts.rgba, g_fonts.w, g_fonts.h);
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
        // not again every frame: Dear ImGui's font stands in until the panel's size changes (a very large panel does
        // not fit the atlas; a smaller one will again)
        g_fontFailedAt = u;
        PanelLog(L"panel: the game font did not become a texture at %.3f px per reference px, Dear ImGui's font stands in", u);
    }
}

static void OnFrame()
{
    static float alpha = 0.0f, needle = 0.0f, warn = 0.0f, hold = 0.0f;
    if (g_modOff) return;
    g_overlayThread = (LONG)GetCurrentThreadId();
    ImGuiIO &io = ImGui::GetIO();
    const float u = PanelUnit(io.DisplaySize.x, io.DisplaySize.y, g_panelScale);
    // baked on the first frames (the game's loading screen) rather than when the panel first opens, so that never waits
    if (io.DisplaySize.y >= 240.0f) UpdateFontTexture(u);
    const float dt = fminf(fmaxf(io.DeltaTime, 0.0f), 0.1f);
    const bool open = g_view.open != 0;
    alpha = fminf(fmaxf(alpha + (open ? dt : -dt) / 0.15f, 0.0f), 1.0f);
    warn = fminf(fmaxf(warn + (g_view.warn ? dt : -dt) / 0.25f, 0.0f), 1.0f);
    hold = fminf(fmaxf(hold + (g_view.hold ? dt : -dt) / 0.25f, 0.0f), 1.0f);
    const ImVec2 at = PanelOrigin(io.DisplaySize.x, io.DisplaySize.y);
    // the tire damage warning shows while the panel is closed, and below it the hint that a change waits for the
    // truck to stand still
    if (warn * (1.0f - alpha) > 0.0f)
        PanelWarning(ImGui::GetForegroundDrawList(), g_fonts, PanelWarnOrigin(io.DisplaySize.x, io.DisplaySize.y), u, warn * (1.0f - alpha), (int)g_view.wear);
    if (hold * (1.0f - alpha) > 0.0f)
    {
        ImVec2 holdAt = PanelWarnOrigin(io.DisplaySize.x, io.DisplaySize.y);
        if (warn > 0.0f) holdAt.y += (kPanelWarnH + 16.0f) * u;
        PanelHold(ImGui::GetForegroundDrawList(), g_fonts, holdAt, u, hold * (1.0f - alpha));
    }
    const int sel = (int)g_view.selected, cur = (int)g_view.current;
    if (alpha <= 0.0f) { needle = (float)cur; return; }
    needle += ((float)sel - needle) * fminf(dt * 12.0f, 1.0f);
    PanelDraw(ImGui::GetForegroundDrawList(), g_fonts, at, u, alpha, needle, g_view, g_panelModes, g_panelKeyName, g_panelConfirmMs);
}

// The Tire Inflation tab of the NEXT GEN menu: the page edits a copy of the settings in force and hands it back when
// something changed.
static void OnPage()
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

bool PanelInit(HMODULE)
{
    // the game's font files sit next to the exe
    wchar_t dir[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t *slash = n && n < MAX_PATH ? wcsrchr(dir, L'\\') : nullptr;
    if (slash) *slash = 0;
    g_fontFiles = slash && PanelFontsLoad(g_fonts, dir);
    PanelLog(g_fontFiles ? L"panel: game font read (TT Lakes Compressed)" : L"panel: game font not found, Dear ImGui's font stands in");
    NgClient client = {};
    client.tab = "Tire Inflation";
    client.version = TP_VERSION;
    client.order = 2;
    client.log = PanelLog;
    client.frame = OnFrame;
    client.page = OnPage;
    client.deviceGone = FreeFontTexture;
    return NgOverlayStart(client);
}

// ---- gamepad ----

typedef DWORD(WINAPI *XInputGetStateFn)(DWORD, XINPUT_STATE *);
static XInputGetStateFn g_realGetState = nullptr;
static volatile LONG g_openMask = 0, g_panelMask = 0;
static volatile LONG g_padLive = 0; // 1 while a truck is driven: only then are the panel's buttons taken from the game
static uint8_t g_padMap[2][256];      // each trigger's value through its curve: 0 the right trigger (throttle), 1 the left (brake and reverse)
static volatile LONG g_padCurved = 0; // 1 while a curve is not 1, so the filter maps the triggers

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
        if (g_padCurved)
        {
            state->Gamepad.bRightTrigger = g_padMap[0][state->Gamepad.bRightTrigger];
            state->Gamepad.bLeftTrigger = g_padMap[1][state->Gamepad.bLeftTrigger];
        }
        const LONG still = g_view.open ? g_panelMask : (held[index] & b);
        InterlockedExchange(&held[index], still);
        WORD hide = (WORD)still;
        const LONG m = g_openMask;
        if (m && (b & m) == m) hide |= (WORD)m;
        b &= (WORD)~hide;
    }
    return r;
}

void PadLive(bool live) { InterlockedExchange(&g_padLive, live ? 1 : 0); }

// The curve: the trigger's share raised to the power, so a light pull gives less and the full pull stays full (at 2,
// half the trigger gives a quarter). A table per trigger, built here on the settings thread; the filter only reads
// it, and reads none while both curves are 1. A pad's trigger pressed a tenth of the way was a lot of throttle for
// SnowRunner's trucks (asked for by the author, 2026-10-23).
void PadCurves(float throttle, float brake)
{
    const float p[2] = { throttle, brake };
    bool any = false;
    for (int t = 0; t < 2; t++)
    {
        const float e = p[t] < 1.0f ? 1.0f : p[t] > 3.0f ? 3.0f : p[t];
        for (int v = 0; v < 256; v++) g_padMap[t][v] = e == 1.0f ? (uint8_t)v : (uint8_t)(int)(255.0f * powf((float)v / 255.0f, e) + 0.5f);
        any = any || e != 1.0f;
    }
    InterlockedExchange(&g_padCurved, any ? 1 : 0);
}

uint8_t PadCurved(uint8_t value, int trigger) { return g_padMap[trigger & 1][value]; }

void PadMasks(uint32_t open, uint32_t lower, uint32_t raise, uint32_t confirm, uint32_t cancel)
{
    // the step bindings only act while the panel is open, so the game keeps their buttons otherwise
    g_openMask = (LONG)open;
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
    // the game loads xinput9_1_0.dll by name and keeps XInputGetState in more than one pointer (the Steam build
    // 6a607c05: its input system's at RVA 0x2af0e80 and a lazily filled one at 0x2ab7bc0): every copy in the image's
    // non-code sections goes to the filter. The sections are read through ReadProcessMemory, as the mod reads the
    // game's memory elsewhere: a page that cannot be read is passed over, not a fault on the mod's thread. Each copy
    // is taken only while it still holds the real function, through the lock the Next Gen mods take around their
    // slot writes.
    int n = 0;
    const size_t kChunk = 8192; // 64 KB at a time
    std::vector<uint64_t> chunk(kChunk);
    const BYTE *image = (const BYTE *)exeBase;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(image + ((const IMAGE_DOS_HEADER *)image)->e_lfanew);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        const uint64_t start = exeBase + sec->VirtualAddress;
        const size_t words = sec->Misc.VirtualSize / 8;
        for (size_t at = 0; at < words; at += kChunk)
        {
            const size_t count = words - at < kChunk ? words - at : kChunk;
            const uint64_t from = start + at * 8;
            if (!ReadProcessMemory(GetCurrentProcess(), (const void *)from, chunk.data(), count * 8, nullptr))
                for (size_t w = 0; w < count; w += 512) // a page of it cannot be read: page by page, those as nothing found
                {
                    const size_t c = count - w < 512 ? count - w : 512;
                    if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(from + w * 8), chunk.data() + w, c * 8, nullptr)) memset(chunk.data() + w, 0, c * 8);
                }
            for (size_t w = 0; w < count; w++)
            {
                void *was = nullptr;
                if (chunk[w] == (uint64_t)real && NgSlotSwap((void **)(from + w * 8), (void *)&PadHook, &was, (void *)real)) n++;
            }
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

