// ng_overlay.cpp: Next Gen's own overlay (ng_overlay.h): the swap chain probe and Present hook, Dear ImGui on the game's
// DirectX 11 device, the input the menu takes from the game while it is open, and the NEXT GEN menu with its theme.
// The same file is in every Next Gen mod; keep the copies alike (kNgSharedVersion says which block layout they share).
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ng_dx11.h"
#include "ng_overlay.h"
#include "ng_reshade.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxguid.lib")

static NgClient g_client = {};
#define NG_LOG(...) do { if (g_client.log) g_client.log(__VA_ARGS__); } while (0)

// ---- the block the Next Gen mods in one game share ----

static const LONG kNgSharedVersion = 1;
// The menu's key when NextGen.ini has none: Delete. Compact keyboards (75 percent) have it, where End is often missing;
// the game, ReShade (Home, and Page Up for its screenshots here), the Spotter minimap mod (F4 to F9, Insert, Home) and
// Steam (F12) leave it free. The user's choice, 2026-10-21.
static const int kNgKeyDefault = VK_DELETE;
static const int kNgSlots = 8;
struct NgShared
{
    volatile LONG version;   // kNgSharedVersion once set up; -1 while the first mod sets it up
    volatile LONG open;      // 1 while the menu shows
    volatile LONG chosen;    // the slot whose tab shows, -1 for none yet
    volatile LONG toggledAt; // GetTickCount() of the last opening or closing, so one key press does it once for all mods
    volatile LONG key;       // the menu's virtual key
    volatile LONG inputSlot; // 1 + the slot of the mod that took the game's input calls (one does it for all)
    volatile LONG wheelTurn; // the mouse wheel's turns while the menu was open, in WHEEL_DELTA parts, since the start
    volatile LONG charCount; // characters typed while the menu was open, since the start
    volatile LONG chars[32]; // the last 32 of them, at charCount % 32
    volatile LONG cursorX, cursorY; // where the cursor was when the menu opened: the game sees it there until it closes
    volatile LONG placeTurn; // counts the window's moves and new sizes
    float x, y, w, h;        // the window's place and size, 0 before it was first drawn
    volatile LONG64 table;   // the swap chain's method table, once a mod found it (the others need no probe of their own)
    struct Slot
    {
        volatile LONG used, ready;
        LONG order;
        char tab[40];
    } slots[kNgSlots];
};
static NgShared g_private = {};
static NgShared *g_shared = &g_private;
static int g_slot = -1;

static void SharedJoin()
{
    wchar_t name[64];
    swprintf_s(name, L"Local\\NextGen.Menu.%lu", GetCurrentProcessId());
    const HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(NgShared), name);
    NgShared *s = map ? (NgShared *)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NgShared)) : nullptr;
    if (s)
    {
        // a new block is all zeros: the first mod sets it up, the others wait the moment that takes
        if (InterlockedCompareExchange(&s->version, -1, 0) == 0)
        {
            s->chosen = -1;
            s->key = kNgKeyDefault;
            InterlockedExchange(&s->version, kNgSharedVersion);
        }
        for (int i = 0; i < 500 && s->version == -1; i++) Sleep(1);
        if (s->version == kNgSharedVersion) g_shared = s;
        else NG_LOG(L"overlay: another Next Gen mod shares the menu another way (block version %ld): this mod's tab is in a menu of its own", s->version);
    }
    if (g_shared == &g_private)
    {
        g_private.version = kNgSharedVersion;
        g_private.chosen = -1;
        g_private.key = kNgKeyDefault;
    }
    NgShared *sh = g_shared;
    for (int i = 0; i < kNgSlots; i++)
        if (InterlockedCompareExchange(&sh->slots[i].used, 1, 0) == 0)
        {
            sh->slots[i].order = g_client.order;
            strncpy_s(sh->slots[i].tab, g_client.tab ? g_client.tab : "?", _TRUNCATE);
            InterlockedExchange(&sh->slots[i].ready, 1);
            g_slot = i;
            break;
        }
}

bool NgMenuOpen() { return g_shared->open != 0; }

// Keys the menu cannot have: the mouse's buttons, the modifiers, the Windows keys and Esc (which keeps the old key when
// a new one is being picked, and pauses the game).
static bool KeyNotForMenu(int vk)
{
    return vk < 8 || (vk >= VK_SHIFT && vk <= VK_MENU) || (vk >= VK_LSHIFT && vk <= VK_RMENU) || vk == VK_LWIN || vk == VK_RWIN || vk == VK_ESCAPE || vk > 254;
}

// A key's name as the keyboard's layout has it, such as "F2" or "End".
static void KeyName(int vk, char *out, size_t size)
{
    UINT scan = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
    // the navigation keys and the arrows are extended keys: without the flag they are named as the number pad's
    switch (vk)
    {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
        scan |= 0x100;
    }
    if (!scan || !GetKeyNameTextA((LONG)(scan << 16), out, (int)size)) snprintf(out, size, "Key %d", vk);
}

// NextGen.ini, next to the game's exe: one file for every Next Gen mod in the game, [Menu] Key= a virtual key code.
static bool IniPath(wchar_t *path, size_t size)
{
    const DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)size);
    wchar_t *slash = n && n < size ? wcsrchr(path, L'\\') : nullptr;
    if (!slash || (size_t)(slash + 1 - path) + 12 >= size) return false;
    wcscpy_s(slash + 1, size - (size_t)(slash + 1 - path), L"NextGen.ini");
    return true;
}

static void KeySave(int vk)
{
    wchar_t path[MAX_PATH + 16];
    if (!IniPath(path, MAX_PATH + 16)) return;
    // a new file gets its help lines first (two mods starting at once: one of them makes it)
    const HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE)
    {
        static const char help[] = "; NEXT GEN: the in-game menu of the Next Gen mods (Next Gen Gameplay, Next Gen Tire Inflation).\r\n"
                                   "; Key: the virtual key code that opens and closes it (46 = Delete, 34 = Page Down, 33 = Page Up, 36 = Home, 113 = F2).\r\n"
                                   "; The menu sets it too: click the key's name at its top right, then press the new key.\r\n";
        DWORD wrote = 0;
        WriteFile(f, help, sizeof(help) - 1, &wrote, nullptr);
        CloseHandle(f);
    }
    wchar_t text[16];
    swprintf_s(text, L"%d", vk);
    WritePrivateProfileStringW(L"Menu", L"Key", text, path);
}

static int KeyLoad()
{
    wchar_t path[MAX_PATH + 16];
    if (!IniPath(path, MAX_PATH + 16)) return kNgKeyDefault;
    const int vk = (int)GetPrivateProfileIntW(L"Menu", L"Key", 0, path);
    if (vk > 0 && !KeyNotForMenu(vk)) return vk;
    if (vk == 0) KeySave(kNgKeyDefault); // the first start: the file with its help lines and the default
    return kNgKeyDefault;
}

void NgMenuKeySet(int vk)
{
    if (vk <= 0 || KeyNotForMenu(vk)) return;
    InterlockedExchange(&g_shared->key, vk);
    KeySave(vk);
}

// ---- the game's input while the menu is open ----
// The game reads the cursor and keys through user32 (GetCursorPos, SetCursorPos, GetAsyncKeyState, GetKeyState; its
// imports, as the build of 2026-10-20 has them) and the window's messages. While the menu is open the mod that took
// them gives the game a still cursor and no keys, lets the cursor move freely, and keeps the clicks, keys and wheel
// for the menu. Key and button releases always reach the game, so nothing it saw pressed stays down for it.

typedef BOOL(WINAPI *SetCursorPosFn)(int, int);
typedef BOOL(WINAPI *GetCursorPosFn)(POINT *);
typedef SHORT(WINAPI *KeyStateFn)(int);
static SetCursorPosFn g_gameSetCursorPos = nullptr;
static GetCursorPosFn g_gameGetCursorPos = nullptr;
static KeyStateFn g_gameGetAsyncKeyState = nullptr, g_gameGetKeyState = nullptr;
static WNDPROC g_gameWndProc = nullptr;
static bool g_wndUnicode = true;

static BOOL WINAPI HookSetCursorPos(int x, int y)
{
    if (g_shared->open) return TRUE;
    return g_gameSetCursorPos(x, y);
}

static BOOL WINAPI HookGetCursorPos(POINT *p)
{
    if (g_shared->open && p)
    {
        p->x = g_shared->cursorX;
        p->y = g_shared->cursorY;
        return TRUE;
    }
    return g_gameGetCursorPos(p);
}

static SHORT WINAPI HookGetAsyncKeyState(int vk)
{
    if (g_shared->open) return 0;
    return g_gameGetAsyncKeyState(vk);
}

static SHORT WINAPI HookGetKeyState(int vk)
{
    if (g_shared->open) return 0;
    return g_gameGetKeyState(vk);
}

static LRESULT CALLBACK HookWndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NgShared *sh = g_shared;
    if (sh->open)
    {
        switch (msg)
        {
        case WM_MOUSEWHEEL:
            InterlockedAdd(&sh->wheelTurn, GET_WHEEL_DELTA_WPARAM(wp));
            return 0;
        case WM_CHAR:
        {
            const LONG n = InterlockedIncrement(&sh->charCount);
            InterlockedExchange(&sh->chars[(n - 1) & 31], (LONG)wp);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEHWHEEL:
            return 0;
        }
    }
    return g_wndUnicode ? CallWindowProcW(g_gameWndProc, wnd, msg, wp, lp) : CallWindowProcA(g_gameWndProc, wnd, msg, wp, lp);
}

// The slot of an import of a module, by the function's name; nullptr when it has none.
static void **ImportSlot(HMODULE mod, const char *dll, const char *fn)
{
    BYTE *base = (BYTE *)mod;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++)
    {
        if (_stricmp((const char *)(base + d->Name), dll) != 0 || !d->OriginalFirstThunk || !d->FirstThunk) continue;
        const IMAGE_THUNK_DATA64 *names = (const IMAGE_THUNK_DATA64 *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *slots = (IMAGE_THUNK_DATA64 *)(base + d->FirstThunk);
        for (int i = 0; names[i].u1.AddressOfData; i++)
        {
            if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
            const IMAGE_IMPORT_BY_NAME *by = (const IMAGE_IMPORT_BY_NAME *)(base + names[i].u1.AddressOfData);
            if (strcmp((const char *)by->Name, fn) == 0) return (void **)&slots[i].u1.Function;
        }
    }
    return nullptr;
}

// Puts hook in a pointer slot (a method table's or an import's) and gives back what was there, which the hook calls on to.
static bool SlotSwap(void **slot, void *hook, void **was)
{
    DWORD old = 0;
    if (!slot || !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return false;
    for (;;)
    {
        void *cur = *(void *volatile *)slot;
        *was = cur;
        if (InterlockedCompareExchangePointer(slot, hook, cur) == cur) break;
    }
    VirtualProtect(slot, sizeof(void *), old, &old);
    return true;
}

// The game reads its mouse and keys through DirectInput as well, so with only the above the mouse would still turn the
// camera and the keys still drive while the menu is open. GetDeviceState and GetDeviceData are hooked in the method
// tables of Windows' own dinput8.dll (under any proxy of it, such as dimerge's, which calls on to it) and, for mice and
// keyboards only, give the game a still mouse and no keys while the menu is open. Releases in the buffered data still
// reach it, so a key held as the menu opened is not held for the game ever after. Wheels and pads are left alone.
typedef HRESULT(WINAPI *DiCreateFn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT(STDMETHODCALLTYPE *DiStateFn)(IDirectInputDevice8W *, DWORD, LPVOID);
typedef HRESULT(STDMETHODCALLTYPE *DiDataFn)(IDirectInputDevice8W *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
static DiStateFn g_diState[2] = {};
static DiDataFn g_diData[2] = {};

// 1 a mouse, 2 a keyboard, 0 anything else; asked of each device once.
static int DiKind(IDirectInputDevice8W *dev)
{
    static void *volatile seen[32] = {};
    static volatile LONG kinds[32] = {}, next = 0;
    for (int i = 0; i < 32; i++)
        if (seen[i] == dev) return kinds[i];
    DIDEVCAPS caps = {};
    caps.dwSize = sizeof(caps);
    int kind = 0;
    if (SUCCEEDED(dev->GetCapabilities(&caps)))
    {
        const BYTE type = GET_DIDEVICE_TYPE(caps.dwDevType);
        kind = type == DI8DEVTYPE_MOUSE ? 1 : type == DI8DEVTYPE_KEYBOARD ? 2 : 0;
    }
    const LONG i = (InterlockedIncrement(&next) - 1) & 31;
    InterlockedExchange(&kinds[i], kind);
    seen[i] = dev;
    return kind;
}

template <int K> static HRESULT STDMETHODCALLTYPE HookDiState(IDirectInputDevice8W *dev, DWORD size, LPVOID data)
{
    const HRESULT hr = g_diState[K](dev, size, data);
    if (SUCCEEDED(hr) && g_shared->open && data && DiKind(dev)) memset(data, 0, size);
    return hr;
}

template <int K> static HRESULT STDMETHODCALLTYPE HookDiData(IDirectInputDevice8W *dev, DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags)
{
    const HRESULT hr = g_diData[K](dev, size, data, count, flags);
    if (FAILED(hr) || !g_shared->open || !data || !count || size < sizeof(DIDEVICEOBJECTDATA_DX3)) return hr;
    const int kind = DiKind(dev);
    if (!kind) return hr;
    DWORD kept = 0;
    for (DWORD i = 0; i < *count; i++)
    {
        BYTE *e = (BYTE *)data + (size_t)i * size;
        const DIDEVICEOBJECTDATA_DX3 *d = (const DIDEVICEOBJECTDATA_DX3 *)e;
        const bool button = kind == 2 || (d->dwOfs >= DIMOFS_BUTTON0 && d->dwOfs <= DIMOFS_BUTTON7);
        if (!button || (d->dwData & 0x80)) continue; // a move or a press: not for the game while the menu is open
        if (kept != i) memmove((BYTE *)data + (size_t)kept * size, e, size);
        kept++;
    }
    *count = kept;
    return hr;
}

static int DiTake()
{
    wchar_t path[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n > MAX_PATH - 16) return 0;
    wcscat_s(path, L"\\dinput8.dll");
    const HMODULE m = LoadLibraryW(path);
    const DiCreateFn create = m ? (DiCreateFn)GetProcAddress(m, "DirectInput8Create") : nullptr;
    if (!create) return 0;
    int hooked = 0;
    void **first = nullptr;
    for (int k = 0; k < 2; k++)
    {
        IDirectInput8W *di = nullptr;
        if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, k ? IID_IDirectInput8A : IID_IDirectInput8W, (void **)&di, nullptr)) || !di) continue;
        IDirectInputDevice8W *dev = nullptr;
        if (SUCCEEDED(di->CreateDevice(GUID_SysMouse, &dev, nullptr)) && dev)
        {
            void **table = *(void ***)dev;
            if (table != first)
            {
                first = first ? first : table;
                hooked += SlotSwap(&table[9], k ? (void *)&HookDiState<1> : (void *)&HookDiState<0>, (void **)&g_diState[k]);
                hooked += SlotSwap(&table[10], k ? (void *)&HookDiData<1> : (void *)&HookDiData<0>, (void **)&g_diData[k]);
            }
            dev->Release();
        }
        di->Release();
    }
    return hooked;
}

static void InputTake(HWND wnd)
{
    if (InterlockedCompareExchange(&g_shared->inputSlot, g_slot + 1, 0) != 0) return; // another Next Gen mod has done it
    const HMODULE exe = GetModuleHandleW(nullptr);
    int n = 0;
    n += SlotSwap(ImportSlot(exe, "USER32.dll", "SetCursorPos"), (void *)&HookSetCursorPos, (void **)&g_gameSetCursorPos);
    n += SlotSwap(ImportSlot(exe, "USER32.dll", "GetCursorPos"), (void *)&HookGetCursorPos, (void **)&g_gameGetCursorPos);
    n += SlotSwap(ImportSlot(exe, "USER32.dll", "GetAsyncKeyState"), (void *)&HookGetAsyncKeyState, (void **)&g_gameGetAsyncKeyState);
    n += SlotSwap(ImportSlot(exe, "USER32.dll", "GetKeyState"), (void *)&HookGetKeyState, (void **)&g_gameGetKeyState);
    g_wndUnicode = IsWindowUnicode(wnd) != FALSE;
    g_gameWndProc = (WNDPROC)(g_wndUnicode ? SetWindowLongPtrW(wnd, GWLP_WNDPROC, (LONG_PTR)&HookWndProc)
                                           : SetWindowLongPtrA(wnd, GWLP_WNDPROC, (LONG_PTR)&HookWndProc));
    if (!g_gameWndProc)
    {
        NG_LOG(L"overlay: the game window's messages could not be taken (error %lu): with the menu open the game still gets clicks and keys", GetLastError());
        g_gameWndProc = (WNDPROC)(g_wndUnicode ? DefWindowProcW : DefWindowProcA);
    }
    const int di = DiTake();
    NG_LOG(L"overlay: the menu takes the game's input while open (%d of 4 user32 imports of the game, %s window messages, %d of 4 DirectInput reads)", n,
           g_wndUnicode ? L"Unicode" : L"ANSI", di);
}

// ---- the theme ----

static ImFont *g_fontText = nullptr, *g_fontBold = nullptr;
static const float kNgTextPx = 21.0f;   // body text at 1080p
static const float kNgTitlePx = 34.0f;  // NEXT GEN in the header
static const float kNgTabPx = 23.0f;    // the tabs' names

static ImVec4 Rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

static void StyleSet(float s)
{
    ImGuiStyle &st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(18, 14);
    st.FramePadding = ImVec2(9, 5);
    st.ItemSpacing = ImVec2(10, 8);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.IndentSpacing = 20;
    st.ScrollbarSize = 13;
    st.GrabMinSize = 14;
    st.WindowRounding = 10;
    st.ChildRounding = 6;
    st.FrameRounding = 5;
    st.PopupRounding = 6;
    st.ScrollbarRounding = 8;
    st.GrabRounding = 4;
    st.TabRounding = 5;
    st.WindowBorderSize = 1;
    st.FrameBorderSize = 0;
    st.PopupBorderSize = 1;
    st.SeparatorTextBorderSize = 2;
    st.SeparatorTextPadding = ImVec2(14, 4);
    st.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    st.ScaleAllSizes(s);
    st.FontSizeBase = kNgTextPx;
    st._NextFrameFontSizeBase = kNgTextPx;
    st.FontScaleMain = s;
    // dark grey, with the logo's orange where a thing is set, grabbed or chosen
    ImVec4 *c = st.Colors;
    const ImVec4 orange = Rgb(254, 158, 32), orangeTop = Rgb(255, 203, 57), orangeLow = Rgb(236, 103, 19);
    c[ImGuiCol_Text] = Rgb(236, 236, 236);
    c[ImGuiCol_TextDisabled] = Rgb(142, 142, 147);
    c[ImGuiCol_WindowBg] = Rgb(20, 20, 22, 0.97f);
    c[ImGuiCol_ChildBg] = Rgb(0, 0, 0, 0.0f);
    c[ImGuiCol_PopupBg] = Rgb(26, 26, 29, 0.98f);
    c[ImGuiCol_Border] = Rgb(58, 58, 63, 0.9f);
    c[ImGuiCol_BorderShadow] = Rgb(0, 0, 0, 0.0f);
    c[ImGuiCol_FrameBg] = Rgb(42, 42, 46);
    c[ImGuiCol_FrameBgHovered] = Rgb(54, 54, 59);
    c[ImGuiCol_FrameBgActive] = Rgb(66, 66, 72);
    c[ImGuiCol_TitleBg] = Rgb(20, 20, 22);
    c[ImGuiCol_TitleBgActive] = Rgb(28, 28, 31);
    c[ImGuiCol_TitleBgCollapsed] = Rgb(20, 20, 22, 0.8f);
    c[ImGuiCol_MenuBarBg] = Rgb(28, 28, 31);
    c[ImGuiCol_ScrollbarBg] = Rgb(14, 14, 16, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = Rgb(74, 74, 80);
    c[ImGuiCol_ScrollbarGrabHovered] = Rgb(90, 90, 96);
    c[ImGuiCol_ScrollbarGrabActive] = orange;
    c[ImGuiCol_CheckMark] = orange;
    c[ImGuiCol_SliderGrab] = orange;
    c[ImGuiCol_SliderGrabActive] = orangeTop;
    c[ImGuiCol_Button] = Rgb(46, 46, 51);
    c[ImGuiCol_ButtonHovered] = Rgb(60, 60, 66);
    c[ImGuiCol_ButtonActive] = orangeLow;
    c[ImGuiCol_Header] = Rgb(46, 46, 51);
    c[ImGuiCol_HeaderHovered] = Rgb(60, 60, 66);
    c[ImGuiCol_HeaderActive] = orangeLow;
    c[ImGuiCol_Separator] = Rgb(254, 158, 32, 0.55f);
    c[ImGuiCol_SeparatorHovered] = orange;
    c[ImGuiCol_SeparatorActive] = orangeTop;
    c[ImGuiCol_ResizeGrip] = Rgb(254, 158, 32, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = Rgb(254, 158, 32, 0.6f);
    c[ImGuiCol_ResizeGripActive] = Rgb(254, 158, 32, 0.9f);
    c[ImGuiCol_InputTextCursor] = orangeTop;
    c[ImGuiCol_Tab] = Rgb(32, 32, 35);
    c[ImGuiCol_TabHovered] = Rgb(54, 54, 59);
    c[ImGuiCol_TabSelected] = Rgb(42, 42, 46);
    c[ImGuiCol_TabSelectedOverline] = orange;
    c[ImGuiCol_TabDimmed] = Rgb(32, 32, 35);
    c[ImGuiCol_TabDimmedSelected] = Rgb(42, 42, 46);
    c[ImGuiCol_TabDimmedSelectedOverline] = Rgb(254, 158, 32, 0.5f);
    c[ImGuiCol_PlotLines] = orange;
    c[ImGuiCol_PlotLinesHovered] = orangeTop;
    c[ImGuiCol_PlotHistogram] = orange;
    c[ImGuiCol_PlotHistogramHovered] = orangeTop;
    c[ImGuiCol_TableHeaderBg] = Rgb(35, 35, 39);
    c[ImGuiCol_TableBorderStrong] = Rgb(58, 58, 63);
    c[ImGuiCol_TableBorderLight] = Rgb(44, 44, 48);
    c[ImGuiCol_TableRowBg] = Rgb(0, 0, 0, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = Rgb(255, 255, 255, 0.03f);
    c[ImGuiCol_TextLink] = orange;
    c[ImGuiCol_TextSelectedBg] = Rgb(254, 158, 32, 0.35f);
    c[ImGuiCol_TreeLines] = Rgb(58, 58, 63);
    c[ImGuiCol_DragDropTarget] = orangeTop;
    c[ImGuiCol_NavCursor] = orange;
    c[ImGuiCol_NavWindowingHighlight] = Rgb(255, 255, 255, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = Rgb(0, 0, 0, 0.3f);
    c[ImGuiCol_ModalWindowDimBg] = Rgb(0, 0, 0, 0.45f);
}

static void FontsAdd()
{
    ImGuiIO &io = ImGui::GetIO();
    // the game's own font, TT Lakes Compressed, next to its exe (never shipped with a mod)
    wchar_t dir[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t *slash = n && n < MAX_PATH ? wcsrchr(dir, L'\\') : nullptr;
    bool game = false;
    if (slash)
    {
        *slash = 0;
        const wchar_t *names[2] = { L"TTLakesCompressed-Medium.ttf", L"TTLakesCompressed-DemiBold.ttf" };
        ImFont **fonts[2] = { &g_fontText, &g_fontBold };
        for (int i = 0; i < 2; i++)
        {
            wchar_t path[MAX_PATH + 64];
            swprintf_s(path, L"%s\\%s", dir, names[i]);
            char path8[(MAX_PATH + 64) * 3];
            if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES || !WideCharToMultiByte(CP_UTF8, 0, path, -1, path8, sizeof(path8), nullptr, nullptr)) continue;
            *fonts[i] = io.Fonts->AddFontFromFileTTF(path8, kNgTextPx);
            game = game || *fonts[i];
        }
    }
    if (!g_fontText) g_fontText = g_fontBold;
    if (!g_fontText) g_fontText = io.Fonts->AddFontDefault();
    if (!g_fontBold) g_fontBold = g_fontText;
    io.FontDefault = g_fontText;
    NG_LOG(game ? L"overlay: the menu is in the game's font (TT Lakes Compressed)" : L"overlay: the game's font was not found, Dear ImGui's own stands in");
}

// ---- drawing helpers for the header ----

static ImU32 Mix(ImU32 a, ImU32 b, float t)
{
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

// The logo's orange from top to foot over the vertices added since start: #FFCB39, #FE9E20 halfway, #EC6713.
static void Gradient(ImDrawList *dl, int start)
{
    float top = FLT_MAX, foot = -FLT_MAX;
    for (int i = start; i < dl->VtxBuffer.Size; i++)
    {
        top = fminf(top, dl->VtxBuffer[i].pos.y);
        foot = fmaxf(foot, dl->VtxBuffer[i].pos.y);
    }
    const float span = fmaxf(foot - top, 1.0f);
    for (int i = start; i < dl->VtxBuffer.Size; i++)
    {
        ImDrawVert &v = dl->VtxBuffer[i];
        const float t = (v.pos.y - top) / span;
        const ImU32 c = t < 0.5f ? Mix(kNgOrangeTop, kNgOrange, t * 2.0f) : Mix(kNgOrange, kNgOrangeLow, t * 2.0f - 1.0f);
        v.col = (c & ~IM_COL32_A_MASK) | (v.col & IM_COL32_A_MASK);
    }
}

// The logo's five-pointed star.
static void Star(ImDrawList *dl, ImVec2 at, float r)
{
    ImVec2 p[10];
    for (int k = 0; k < 10; k++)
    {
        const float a = -1.5707963f + k * 0.6283185f, rr = (k & 1) ? r * 0.42f : r;
        p[k] = ImVec2(at.x + cosf(a) * rr, at.y + sinf(a) * rr);
    }
    const int start = dl->VtxBuffer.Size;
    dl->AddConcavePolyFilled(p, 10, IM_COL32_WHITE);
    Gradient(dl, start);
}

// ---- the menu ----

static LONG g_placeSeen = -1;
static bool g_keyCapture = false;  // the menu waits for a new key
static bool g_captureWas[256] = {}; // the keys that were down, so only a new press counts

static void MenuClose()
{
    NgShared *sh = g_shared;
    if (InterlockedExchange(&sh->open, 0))
    {
        InterlockedExchange(&sh->toggledAt, (LONG)GetTickCount());
        SetCursorPos(sh->cursorX, sh->cursorY); // where the game left it: it sees no jump
    }
}

static void MenuOpen()
{
    NgShared *sh = g_shared;
    POINT p = {};
    GetCursorPos(&p);
    InterlockedExchange(&sh->cursorX, p.x);
    InterlockedExchange(&sh->cursorY, p.y);
    InterlockedExchange(&sh->open, 1);
}

static void MenuDraw(float s)
{
    NgShared *sh = g_shared;
    const ImGuiIO &io = ImGui::GetIO();
    if (sh->placeTurn != g_placeSeen)
    {
        g_placeSeen = sh->placeTurn;
        if (sh->w > 0.0f && sh->h > 0.0f)
        {
            ImGui::SetNextWindowPos(ImVec2(sh->x, sh->y));
            ImGui::SetNextWindowSize(ImVec2(sh->w, sh->h));
        }
        else
        {
            const ImVec2 size(fminf(900.0f * s, io.DisplaySize.x * 0.9f), fminf(700.0f * s, io.DisplaySize.y * 0.8f)); // clear of a cluster at the bottom
            ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - size.x) * 0.5f, (io.DisplaySize.y - size.y) * 0.42f));
            ImGui::SetNextWindowSize(size);
        }
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(560.0f * s, 340.0f * s), io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool shown = ImGui::Begin("NEXT GEN##ngmenu", nullptr,
                                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (shown)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        const float pad = 18.0f * s, headH = 66.0f * s, round = ImGui::GetStyle().WindowRounding;
        // the header: a darker grey band, the logo's star and NEXT GEN in its orange, a thin orange line below
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + headH), IM_COL32(28, 28, 31, 255), round, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + headH - 2.0f * s), ImVec2(p.x + size.x, p.y + headH), kNgOrangeTop, kNgOrangeLow, kNgOrangeLow, kNgOrangeTop);
        float x = p.x + pad;
        Star(dl, ImVec2(x + 13.0f * s, p.y + headH * 0.5f - 1.0f * s), 13.0f * s);
        x += 34.0f * s;
        const float titlePx = kNgTitlePx * s;
        const ImVec2 title = g_fontBold->CalcTextSizeA(titlePx, FLT_MAX, 0.0f, "NEXT GEN");
        const int start = dl->VtxBuffer.Size;
        dl->AddText(g_fontBold, titlePx, ImVec2(x, p.y + (headH - title.y) * 0.5f), IM_COL32_WHITE, "NEXT GEN");
        Gradient(dl, start);
        x += title.x + 30.0f * s;
        // the tabs: one per Next Gen mod in the game, in their order; the chosen one white over an orange bar
        int order[kNgSlots], count = 0;
        for (int i = 0; i < kNgSlots; i++)
            if (sh->slots[i].ready) order[count++] = i;
        for (int a = 1; a < count; a++)
            for (int b = a; b > 0 && sh->slots[order[b]].order < sh->slots[order[b - 1]].order; b--)
            {
                const int t = order[b];
                order[b] = order[b - 1];
                order[b - 1] = t;
            }
        const float tabPx = kNgTabPx * s;
        for (int k = 0; k < count; k++)
        {
            const int i = order[k];
            const char *name = sh->slots[i].tab;
            const ImVec2 t = g_fontBold->CalcTextSizeA(tabPx, FLT_MAX, 0.0f, name);
            const ImVec2 at(x, p.y + 6.0f * s), box(t.x + 24.0f * s, headH - 8.0f * s);
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##tab", box)) InterlockedExchange(&sh->chosen, i);
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool chosen = i == g_slot;
            if (hovered && !chosen) dl->AddRectFilled(at, ImVec2(at.x + box.x, at.y + box.y), IM_COL32(255, 255, 255, 16), 5.0f * s);
            const ImU32 col = chosen ? IM_COL32(244, 244, 244, 255) : hovered ? IM_COL32(220, 220, 220, 255) : IM_COL32(150, 150, 155, 255);
            dl->AddText(g_fontBold, tabPx, ImVec2(at.x + 12.0f * s, p.y + (headH - t.y) * 0.5f), col, name);
            if (chosen)
                dl->AddRectFilledMultiColor(ImVec2(at.x + 8.0f * s, p.y + headH - 7.0f * s), ImVec2(at.x + box.x - 8.0f * s, p.y + headH - 3.0f * s), kNgOrangeTop, kNgOrangeLow,
                                            kNgOrangeLow, kNgOrangeTop);
            x += box.x + 6.0f * s;
        }
        // at the right: the close button, and before it the shown mod's version and the key
        const float closeSize = 30.0f * s;
        const ImVec2 closeAt(p.x + size.x - pad - closeSize, p.y + (headH - closeSize) * 0.5f);
        ImGui::SetCursorScreenPos(closeAt);
        if (ImGui::InvisibleButton("##close", ImVec2(closeSize, closeSize))) MenuClose();
        const bool closeHot = ImGui::IsItemHovered();
        if (closeHot) dl->AddRectFilled(closeAt, ImVec2(closeAt.x + closeSize, closeAt.y + closeSize), IM_COL32(236, 103, 19, 200), 6.0f * s);
        const float m = closeSize * 0.32f;
        const ImVec2 cc(closeAt.x + closeSize * 0.5f, closeAt.y + closeSize * 0.5f);
        const ImU32 xcol = closeHot ? IM_COL32_WHITE : IM_COL32(170, 170, 175, 255);
        dl->AddLine(ImVec2(cc.x - m, cc.y - m), ImVec2(cc.x + m, cc.y + m), xcol, 2.0f * s);
        dl->AddLine(ImVec2(cc.x - m, cc.y + m), ImVec2(cc.x + m, cc.y - m), xcol, 2.0f * s);
        // before it, the menu's key, a button that takes a new one (the next key pressed), and left of that the version
        char name[32], keyText[64];
        KeyName(sh->key, name, sizeof(name));
        if (g_keyCapture) snprintf(keyText, sizeof(keyText), "Press a key (Esc keeps %s)", name);
        else snprintf(keyText, sizeof(keyText), "%s opens and closes", name);
        const float notePx = 17.0f * s;
        const ImVec2 kt = g_fontText->CalcTextSizeA(notePx, FLT_MAX, 0.0f, keyText);
        const ImVec2 keyBox(kt.x + 18.0f * s, closeSize);
        const ImVec2 keyAt(closeAt.x - 10.0f * s - keyBox.x, closeAt.y);
        if (keyAt.x > x)
        {
            ImGui::SetCursorScreenPos(keyAt);
            if (ImGui::InvisibleButton("##key", keyBox))
            {
                g_keyCapture = !g_keyCapture;
                for (int vk = 0; vk < 256; vk++) g_captureWas[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0; // keys already down do not count
            }
            const bool hot = ImGui::IsItemHovered();
            const ImVec2 keyEnd(keyAt.x + keyBox.x, keyAt.y + keyBox.y);
            if (g_keyCapture) dl->AddRectFilled(keyAt, keyEnd, IM_COL32(236, 103, 19, 210), 6.0f * s);
            else if (hot) dl->AddRectFilled(keyAt, keyEnd, IM_COL32(255, 255, 255, 24), 6.0f * s);
            else dl->AddRect(keyAt, keyEnd, IM_COL32(255, 255, 255, 40), 6.0f * s, 0, 1.0f * s);
            dl->AddText(g_fontText, notePx, ImVec2(keyAt.x + 9.0f * s, keyAt.y + (keyBox.y - kt.y) * 0.5f), g_keyCapture || hot ? IM_COL32_WHITE : IM_COL32(190, 190, 195, 255), keyText);
            if (hot && !g_keyCapture) ImGui::SetTooltip("Click, then press the key that opens and closes this menu. It is kept in NextGen.ini.");
            char ver[64];
            snprintf(ver, sizeof(ver), "%s %s", g_client.tab ? g_client.tab : "", g_client.version ? g_client.version : "");
            const ImVec2 vt = g_fontText->CalcTextSizeA(notePx, FLT_MAX, 0.0f, ver);
            if (keyAt.x - 14.0f * s - vt.x > x) dl->AddText(g_fontText, notePx, ImVec2(keyAt.x - 14.0f * s - vt.x, p.y + (headH - vt.y) * 0.5f), IM_COL32(142, 142, 147, 255), ver);
        }
        // the chosen mod's page
        ImGui::SetCursorScreenPos(ImVec2(p.x + pad, p.y + headH + 12.0f * s));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * s, 4.0f * s));
        const bool body = ImGui::BeginChild("##page", ImVec2(size.x - 2.0f * pad + 6.0f * s, size.y - headH - 12.0f * s - pad), ImGuiChildFlags_None, ImGuiWindowFlags_None);
        ImGui::PopStyleVar();
        if (body && g_client.page) g_client.page();
        ImGui::EndChild();
        // where the window is now, for the other mods' tabs
        const ImVec2 np = ImGui::GetWindowPos(), ns = ImGui::GetWindowSize();
        if (np.x != sh->x || np.y != sh->y || ns.x != sh->w || ns.y != sh->h)
        {
            sh->x = np.x;
            sh->y = np.y;
            sh->w = ns.x;
            sh->h = ns.y;
            g_placeSeen = InterlockedIncrement(&sh->placeTurn);
        }
    }
    ImGui::End();
}

// ---- the frame ----

typedef HRESULT(STDMETHODCALLTYPE *PresentFn)(IDXGISwapChain *, UINT, UINT);
static PresentFn g_present = nullptr;
static volatile LONG g_broken = 0;
static HWND g_probeWnd = nullptr, g_wnd = nullptr;
static IDXGISwapChain *g_sc = nullptr; // the swap chain drawn on (not held: only compared)
static ULONGLONG g_scSeen = 0;
static ID3D11Device *g_dev = nullptr;
static ID3D11DeviceContext *g_ctx = nullptr;
static bool g_context = false, g_keyWas = false;
static float g_styleScale = 0.0f;
static LARGE_INTEGER g_rate = {}, g_last = {};
static LONG g_wheelSeen = 0, g_charSeen = 0;
static bool g_keysDown[ImGuiKey_NamedKey_END] = {};

ImTextureID NgTextureMake(const void *rgba, int w, int h)
{
    if (!g_dev || !rgba || w <= 0 || h <= 0) return 0;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = (UINT)w;
    d.Height = (UINT)h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data = { rgba, (UINT)w * 4, 0 };
    ID3D11Texture2D *t = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&d, &data, &t)) || !t) return 0;
    ID3D11ShaderResourceView *v = nullptr;
    const HRESULT hr = g_dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return SUCCEEDED(hr) && v ? (ImTextureID)(intptr_t)v : 0;
}

void NgTextureFree(ImTextureID tex)
{
    if (tex) ((ID3D11ShaderResourceView *)(intptr_t)tex)->Release();
}

static void DeviceSet(ID3D11Device *dev)
{
    if (g_dev)
    {
        if (g_client.deviceGone) g_client.deviceGone();
        ImGui_ImplDX11_Shutdown();
        g_ctx->Release();
        g_dev->Release();
        g_ctx = nullptr;
        NG_LOG(L"overlay: the game made a new DirectX device");
    }
    g_dev = dev;
    dev->GetImmediateContext(&g_ctx);
    if (!g_context)
    {
        g_context = true;
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.BackendPlatformName = "next_gen_overlay";
        FontsAdd();
        QueryPerformanceFrequency(&g_rate);
        QueryPerformanceCounter(&g_last);
    }
    ImGui_ImplDX11_Init(g_dev, g_ctx);
}

// A virtual key as Dear ImGui names it; ImGuiKey_None for the ones the menu does not read (the mouse buttons among them).
// Every key reaches the menu while it is open: a tab that sets a key binding reads the next one pressed.
static ImGuiKey KeyOf(int vk)
{
    if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
    if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
    if (vk >= VK_F1 && vk <= VK_F24) return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (ImGuiKey)(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0));
    switch (vk)
    {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_OEM_7: return ImGuiKey_Apostrophe;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_OEM_MINUS: return ImGuiKey_Minus;
    case VK_OEM_PERIOD: return ImGuiKey_Period;
    case VK_OEM_2: return ImGuiKey_Slash;
    case VK_OEM_1: return ImGuiKey_Semicolon;
    case VK_OEM_PLUS: return ImGuiKey_Equal;
    case VK_OEM_4: return ImGuiKey_LeftBracket;
    case VK_OEM_5: return ImGuiKey_Backslash;
    case VK_OEM_6: return ImGuiKey_RightBracket;
    case VK_OEM_3: return ImGuiKey_GraveAccent;
    case VK_CAPITAL: return ImGuiKey_CapsLock;
    case VK_SCROLL: return ImGuiKey_ScrollLock;
    case VK_NUMLOCK: return ImGuiKey_NumLock;
    case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
    case VK_PAUSE: return ImGuiKey_Pause;
    case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE: return ImGuiKey_KeypadDivide;
    case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case VK_ADD: return ImGuiKey_KeypadAdd;
    case VK_LSHIFT: return ImGuiKey_LeftShift;
    case VK_LCONTROL: return ImGuiKey_LeftCtrl;
    case VK_LMENU: return ImGuiKey_LeftAlt;
    case VK_LWIN: return ImGuiKey_LeftSuper;
    case VK_RSHIFT: return ImGuiKey_RightShift;
    case VK_RCONTROL: return ImGuiKey_RightCtrl;
    case VK_RMENU: return ImGuiKey_RightAlt;
    case VK_RWIN: return ImGuiKey_RightSuper;
    case VK_APPS: return ImGuiKey_Menu;
    }
    return ImGuiKey_None;
}

static void InputFeed(bool menu, bool front, float scaleX, float scaleY)
{
    ImGuiIO &io = ImGui::GetIO();
    NgShared *sh = g_shared;
    const LONG wheel = sh->wheelTurn, chars = sh->charCount;
    const LONG wheelNew = wheel - g_wheelSeen;
    g_wheelSeen = wheel;
    if (!menu || !front)
    {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        for (int b = 0; b < 3; b++) io.AddMouseButtonEvent(b, false);
        for (int k = 0; k < ImGuiKey_NamedKey_END; k++)
            if (g_keysDown[k])
            {
                g_keysDown[k] = false;
                io.AddKeyEvent((ImGuiKey)k, false);
            }
        g_charSeen = chars;
        return;
    }
    POINT pt = {};
    GetCursorPos(&pt);
    ScreenToClient(g_wnd, &pt);
    io.AddMousePosEvent(pt.x * scaleX, pt.y * scaleY);
    const bool swap = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    io.AddMouseButtonEvent(0, (GetAsyncKeyState(swap ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (GetAsyncKeyState(swap ? VK_LBUTTON : VK_RBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
    if (wheelNew) io.AddMouseWheelEvent(0.0f, wheelNew / (float)WHEEL_DELTA);
    for (; g_charSeen < chars; g_charSeen++) io.AddInputCharacterUTF16((ImWchar16)sh->chars[g_charSeen & 31]);
    for (int vk = 8; vk < 0xE0; vk++)
    {
        // the menu's own key opens and closes it and is nothing else to Dear ImGui (as End, it would light the
        // keyboard's cursor around the page)
        const ImGuiKey key = vk == sh->key ? ImGuiKey_None : KeyOf(vk);
        if (key == ImGuiKey_None) continue;
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down != g_keysDown[key])
        {
            g_keysDown[key] = down;
            io.AddKeyEvent(key, down);
        }
    }
    io.AddKeyEvent(ImGuiMod_Ctrl, (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
}

static void Frame(IDXGISwapChain *sc)
{
    const ULONGLONG now = GetTickCount64();
    if (sc != g_sc)
    {
        // another swap chain: the overlay keeps to the one it draws on, unless that one is gone (it has not presented
        // for 3 s) or the new one shows in the same window (the game made its swap chain anew)
        DXGI_SWAP_CHAIN_DESC sd = {};
        if (FAILED(sc->GetDesc(&sd)) || !sd.OutputWindow || sd.OutputWindow == g_probeWnd) return;
        if (g_sc && now - g_scSeen < 3000 && sd.OutputWindow != g_wnd) return;
        const bool first = !g_sc;
        g_sc = sc;
        g_wnd = sd.OutputWindow;
        NG_LOG(L"overlay: drawing on the game's swap chain (%u x %u, format %d, window %p)%s", sd.BufferDesc.Width, sd.BufferDesc.Height, (int)sd.BufferDesc.Format, sd.OutputWindow,
               first ? L"" : L", a new one");
        if (first) InputTake(g_wnd);
    }
    g_scSeen = now;
    ID3D11Device *dev = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void **)&dev)) || !dev) return;
    if (dev != g_dev) DeviceSet(dev);
    else dev->Release();
    ID3D11Texture2D *bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)) || !bb) return;
    D3D11_TEXTURE2D_DESC td = {};
    bb->GetDesc(&td);
    // a picture in linear light (the 16-bit float swap chain of HDR, scRGB) takes the menu at a paper white of 203 nits
    const bool linear = td.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    const bool srgb = td.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    ImGui_ImplDX11_SetLinearScale(linear ? 203.0f / 80.0f : srgb ? 1.0f : 0.0f);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)td.Width, (float)td.Height);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    io.DeltaTime = fmaxf((float)((double)(t.QuadPart - g_last.QuadPart) / (double)g_rate.QuadPart), 0.0001f);
    g_last = t;
    const float s = fmaxf(td.Height / 1080.0f, 0.5f);
    if (fabsf(s - g_styleScale) > 0.001f)
    {
        g_styleScale = s;
        StyleSet(s);
    }
    // the menu's key: opens and closes it for every Next Gen mod at once
    NgShared *sh = g_shared;
    const bool front = GetForegroundWindow() == g_wnd;
    // a new key for it: the next key pressed while the menu waits for one; Esc keeps the old one
    if (g_keyCapture && (!sh->open || sh->chosen != g_slot || !front)) g_keyCapture = false;
    for (int vk = 8; g_keyCapture && vk < 255; vk++)
    {
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0, pressed = down && !g_captureWas[vk];
        g_captureWas[vk] = down;
        if (!pressed || (KeyNotForMenu(vk) && vk != VK_ESCAPE)) continue;
        g_keyCapture = false;
        if (vk == VK_ESCAPE) break;
        InterlockedExchange(&sh->toggledAt, (LONG)GetTickCount()); // the press that sets it does not also close the menu, here or in another mod
        InterlockedExchange(&sh->key, vk);
        g_keyWas = true;
        KeySave(vk);
        char name[32];
        KeyName(vk, name, sizeof(name));
        NG_LOG(L"overlay: the menu's key is now %S (%d), kept in NextGen.ini", name, vk);
    }
    const bool keyDown = front && (GetAsyncKeyState(sh->key ? sh->key : kNgKeyDefault) & 0x8000) != 0;
    if (keyDown && !g_keyWas)
    {
        const LONG at = (LONG)GetTickCount(), was = sh->toggledAt;
        if ((DWORD)(at - was) > 300 && InterlockedCompareExchange(&sh->toggledAt, at, was) == was)
        {
            if (sh->open) MenuClose();
            else MenuOpen();
        }
    }
    g_keyWas = keyDown;
    // the tab shown: the chosen one, or the first in order when none is chosen yet
    const LONG chosen = sh->chosen;
    if (sh->open && (chosen < 0 || chosen >= kNgSlots || !sh->slots[chosen].ready))
    {
        int first = -1;
        for (int i = 0; i < kNgSlots; i++)
            if (sh->slots[i].ready && (first < 0 || sh->slots[i].order < sh->slots[first].order)) first = i;
        InterlockedCompareExchange(&sh->chosen, first, chosen);
    }
    const bool menu = sh->open && sh->chosen == g_slot;
    RECT rc = {};
    GetClientRect(g_wnd, &rc);
    const float sx = rc.right > 0 ? td.Width / (float)rc.right : 1.0f, sy = rc.bottom > 0 ? td.Height / (float)rc.bottom : 1.0f;
    InputFeed(menu, front, sx, sy);
    CURSORINFO ci = { sizeof(ci) };
    io.MouseDrawCursor = menu && front && GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING);
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    if (g_client.frame) g_client.frame();
    if (menu) MenuDraw(s);
    ImGui::Render();
    ImDrawData *dd = ImGui::GetDrawData();
    if (dd && dd->CmdListsCount > 0)
    {
        D3D11_RENDER_TARGET_VIEW_DESC rd = {};
        rd.Format = td.Format;
        rd.ViewDimension = td.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        ID3D11RenderTargetView *rtv = nullptr;
        if (SUCCEEDED(g_dev->CreateRenderTargetView(bb, &rd, &rtv)) && rtv)
        {
            // the game's render targets back as they were after the overlay's (ImGui's renderer restores the rest)
            ID3D11RenderTargetView *rtvWas[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            ID3D11DepthStencilView *dsvWas = nullptr;
            g_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvWas, &dsvWas);
            g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(dd);
            g_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvWas, dsvWas);
            for (ID3D11RenderTargetView *r : rtvWas)
                if (r) r->Release();
            if (dsvWas) dsvWas->Release();
            rtv->Release();
        }
    }
    bb->Release();
}

// A fault in the overlay ends the overlay, never the game.
static void FrameGuarded(IDXGISwapChain *sc)
{
    __try
    {
        Frame(sc);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        InterlockedExchange(&g_broken, 1);
        NG_LOG(L"overlay: a fault while drawing (exception %08lx): the overlay stops, the game goes on", GetExceptionCode());
    }
}

static HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    if (!g_broken && !(flags & DXGI_PRESENT_TEST)) FrameGuarded(sc);
    return g_present(sc, sync, flags);
}

// ---- the start ----

// The swap chain's method table, from a swap chain made for the purpose on the WARP software device in a hidden
// window and released at once. A hardware device here would make ReShade, where it is installed, load its add-ons onto it.
static void **ProbeTable()
{
    const HWND wnd = CreateWindowExW(0, L"STATIC", L"Next Gen overlay probe", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
    if (!wnd)
    {
        NG_LOG(L"overlay: no window for the DirectX probe (error %lu): nothing is drawn", GetLastError());
        return nullptr;
    }
    g_probeWnd = wnd;
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 64;
    sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    IDXGISwapChain *sc = nullptr;
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx);
    void **table = SUCCEEDED(hr) && sc ? *(void ***)sc : nullptr;
    if (sc) sc->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    DestroyWindow(wnd);
    if (!table) NG_LOG(L"overlay: the DirectX probe on the WARP device failed (%08lx): nothing is drawn", (unsigned long)hr);
    return table;
}

// The game's swap chain as ReShade hands it over (ng_reshade.cpp): its method table hooked as a probe's would be.
static void HookFrom(IDXGISwapChain *sc)
{
    void **table = sc ? *(void ***)sc : nullptr;
    if (!table || !SlotSwap(&table[8], (void *)&HookPresent, (void **)&g_present))
    {
        NG_LOG(L"overlay: the swap chain ReShade handed over could not be hooked (error %lu): nothing is drawn", GetLastError());
        return;
    }
    NG_LOG(L"overlay: Present hooked (the game's swap chain from ReShade's first present), tab slot %d", g_slot);
}

static DWORD WINAPI StartThread(void *)
{
    // with ReShade installed no probe is made: the game's swap chain comes from ReShade's first present
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&StartThread, &self);
    if (self && NgReShadeWatch(self, &HookFrom))
    {
        NG_LOG(L"overlay: ReShade is installed: the overlay hooks the game's own swap chain at its first present, with no DirectX probe of its own");
        return 0;
    }
    void **table = (void **)g_shared->table;
    const bool found = table != nullptr;
    if (!table)
    {
        table = ProbeTable();
        if (!table) return 0;
        InterlockedExchange64(&g_shared->table, (LONG64)table);
    }
    if (!SlotSwap(&table[8], (void *)&HookPresent, (void **)&g_present))
    {
        NG_LOG(L"overlay: the swap chain's Present could not be hooked (error %lu): nothing is drawn", GetLastError());
        return 0;
    }
    HMODULE owner = nullptr;
    wchar_t ownerName[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)g_present, &owner))
    {
        GetModuleFileNameW(owner, ownerName, MAX_PATH);
        const wchar_t *slash = wcsrchr(ownerName, L'\\');
        if (slash) memmove(ownerName, slash + 1, (wcslen(slash + 1) + 1) * sizeof(wchar_t));
    }
    NG_LOG(L"overlay: Present hooked (%s; it calls on to %s), tab slot %d", found ? L"the method table another Next Gen mod found" : L"found with a WARP probe", ownerName,
           g_slot);
    return 0;
}

bool NgOverlayStart(const NgClient &client)
{
    g_client = client;
    SharedJoin();
    InterlockedExchange(&g_shared->key, KeyLoad());
    if (g_slot < 0)
    {
        NG_LOG(L"overlay: the menu has no room for another tab (%d mods): nothing is drawn", kNgSlots);
        return false;
    }
    const HANDLE t = CreateThread(nullptr, 0, StartThread, nullptr, 0, nullptr);
    if (!t)
    {
        NG_LOG(L"overlay: no thread for the start (error %lu): nothing is drawn", GetLastError());
        return false;
    }
    CloseHandle(t);
    return true;
}
