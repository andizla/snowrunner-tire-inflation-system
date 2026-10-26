// overlay_test.cpp: the offline test of Next Gen's overlay (src\ng_overlay.cpp), how it finds the swap chains. build.bat
// makes out\test\overlay\overlay_test.exe from this file and, with OVERLAY_TEST_MOD, a stand-in mod of the shared
// overlay files and a tiny client, copied as ng_mod_a.dll and ng_mod_b.dll: two Next Gen mods in one game. The program
// makes its DXGI factory, device and swap chains through its imports as SnowRunner does (CreateDXGIFactory1,
// D3D11CreateDevice), on the WARP software device in a hidden window, and checks that:
// - both mods draw on a swap chain made by a factory from CreateDXGIFactory1 (CreateSwapChain), and on one made by the
//   factory of a device made without an adapter (CreateSwapChainForHwnd, flip model), once for each Present and for
//   each Present1;
// - such swap chains have a method table of the mods' own, and every factory the same one, made once;
// - DirectX's own method tables are as they were before the mods came in (the Steam overlay hooks what they hold at
//   each swap chain made, and with the overlay's hook in them the two called each other until the stack ran out), and a
//   swap chain made without the program's imports (D3D11CreateDeviceAndSwapChain) gets no overlay;
// - DirectInput, where a table cannot be copied (DirectInput checks its objects' tables): the mods' reads are in
//   dinput8.dll's device table after the first frame, and a keyboard and a mouse made after it read through them;
// - a stand-in for the Steam overlay's hook, put in DirectX's factory table before the mods come in, as Steam's sits in
//   DXGI's code under every other hook: at each new swap chain it notes what the swap chain's Present slot holds, as
//   Steam does before it hooks that function with one pointer to call on to for all. It may only ever see DirectX's own
//   Present: a second function there is what looped with Steam (the tester's crash of 2026-10-22);
// - the two mods writing slots on one read-only page at once, as at the game's start: every write lands, none faults,
//   and the page stays read-only;
// - the start's notice for each way ReShade can be missing (no dxgi.dll, a ReShade that took no add-on, another
//   program's dxgi.dll), and none with ReShade working;
// - the Graphics tab: with OVERLAY_TEST_GRAPHICS this file is also a stand-in for Next Gen Graphics' NgGraphicsGetApi
//   (ng_graphics_standin.dll); handed to both mods, one claims the tab, and its page reads the switches.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <stdio.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#if defined(OVERLAY_TEST_GRAPHICS)

// A stand-in for Next Gen Graphics' hid.dll: three switches and two status lines through NgGraphicsGetApi
// (ng_graphics.h), counting how often the Graphics tab reads its switches.
#include "ng_graphics.h"

static volatile LONG g_on[3] = { 1, 0, 1 };
static volatile LONG g_gets = 0;
static const char *Name(int i)
{
    static const char *const n[] = { "Contact shadows", "AO at half size", "Reflections" };
    return i >= 0 && i < 3 ? n[i] : nullptr;
}
static const char *Help(int i) { return i == 0 ? "A stand-in's tip" : nullptr; }
static int Get(int i)
{
    InterlockedIncrement(&g_gets);
    return i >= 0 && i < 3 ? (int)g_on[i] : 0;
}
static void Set(int i, int on)
{
    if (i >= 0 && i < 3) InterlockedExchange(&g_on[i], on);
}
static int StatusCount() { return 2; }
static const char *Status(int i) { return i == 0 ? "Stand-in build" : i == 1 ? "ReShade: no" : nullptr; }
static const NgGraphicsApi g_api = { 1, 3, &Name, &Help, &Get, &Set, &StatusCount, &Status };

extern "C" __declspec(dllexport) const NgGraphicsApi *NgGraphicsGetApi(int version) { return version == 1 ? &g_api : nullptr; }
extern "C" __declspec(dllexport) LONG TestGraphicsGets() { return g_gets; }

#elif defined(OVERLAY_TEST_MOD)

#include "ng_overlay.h"

static char g_tab[16] = "?";
static volatile LONG *g_frames = nullptr;

static void TestLog(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    wprintf(L"  [%S] ", g_tab);
    vwprintf(fmt, ap);
    wprintf(L"\n");
    va_end(ap);
    fflush(stdout);
}

static void TestFrame() { InterlockedIncrement(g_frames); }
static void TestPage() {}
static void TestDeviceGone() {}

extern "C" __declspec(dllexport) bool TestSlotSwap(void **slot, void *value)
{
    void *was = nullptr;
    return NgSlotSwap(slot, value, &was);
}

extern "C" __declspec(dllexport) int TestReShade(bool registered, HMODULE reshade, const wchar_t *dxgi, char *note, int size)
{
    const int r = reshade || dxgi || registered ? NgReShadeCheck(registered, reshade, dxgi) : -1;
    strncpy_s(note, size, NgReShadeNote(), _TRUNCATE);
    return r;
}

extern "C" __declspec(dllexport) bool TestGraphics(const wchar_t *dll) { return NgGraphicsFrom(LoadLibraryW(dll)); }
extern "C" __declspec(dllexport) int TestTabCount(const char *tab) { return NgMenuTabCount(tab); }
extern "C" __declspec(dllexport) bool TestShowTab(const char *tab) { return NgMenuShowTab(tab); }
extern "C" __declspec(dllexport) int TestKeyFromIni() { return NgMenuKeyFromIni(); }
extern "C" __declspec(dllexport) int TestTableCopy() { return NgTableCopySelfCheck(); }

extern "C" __declspec(dllexport) bool TestStart(const char *tab, int order, volatile LONG *frames)
{
    strncpy_s(g_tab, tab, _TRUNCATE);
    g_frames = frames;
    NgClient c = {};
    c.tab = g_tab;
    c.version = "test";
    c.order = order;
    c.log = &TestLog;
    c.frame = &TestFrame;
    c.page = &TestPage;
    c.deviceGone = &TestDeviceGone;
    return NgOverlayStart(c);
}

#else

typedef bool (*StartFn)(const char *tab, int order, volatile LONG *frames);
typedef bool (*SwapFn)(void **slot, void *value);
static int g_failed = 0;

// The Steam overlay's way, in short: at every swap chain made, a look at what its Present slot holds (Steam then hooks
// that function, keeping one pointer to call on to). These note each function seen there.
typedef HRESULT(STDMETHODCALLTYPE *CreateSwapChainFn)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
typedef HRESULT(STDMETHODCALLTYPE *CreateSwapChainHwndFn)(IDXGIFactory2 *, IUnknown *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
                                                         const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **);
static CreateSwapChainFn g_steamCreate = nullptr;
static CreateSwapChainHwndFn g_steamCreateHwnd = nullptr;
static void *g_steamSaw[8] = {};
static int g_steamSawCount = 0, g_steamLooks = 0;

static void SteamLook(IUnknown *sc)
{
    void *present = (*(void ***)sc)[8];
    g_steamLooks++;
    for (int i = 0; i < g_steamSawCount; i++)
        if (g_steamSaw[i] == present) return;
    if (g_steamSawCount < 8) g_steamSaw[g_steamSawCount++] = present;
}

static HRESULT STDMETHODCALLTYPE SteamCreate(IDXGIFactory *f, IUnknown *dev, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **out)
{
    const HRESULT hr = g_steamCreate(f, dev, desc, out);
    if (SUCCEEDED(hr) && out && *out) SteamLook(*out);
    return hr;
}

static HRESULT STDMETHODCALLTYPE SteamCreateHwnd(IDXGIFactory2 *f, IUnknown *dev, HWND wnd, const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                 const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *full, IDXGIOutput *output, IDXGISwapChain1 **out)
{
    const HRESULT hr = g_steamCreateHwnd(f, dev, wnd, desc, full, output, out);
    if (SUCCEEDED(hr) && out && *out) SteamLook(*out);
    return hr;
}

static void TableWrite(void **slot, void *value)
{
    DWORD old = 0;
    VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
    *slot = value;
    VirtualProtect(slot, sizeof(void *), old, &old);
}

// One mod's slot writes, many at once with the other's.
struct SwapRun
{
    SwapFn swap;
    void **slot;
    int done;
};

static DWORD WINAPI SwapThread(void *p)
{
    SwapRun *r = (SwapRun *)p;
    for (int i = 1; i <= 20000; i++) r->done += r->swap(r->slot, (void *)(intptr_t)i) ? 1 : 0;
    return 0;
}

static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok    " : "FAILED", what);
    fflush(stdout);
    if (!ok) g_failed++;
}

static void WriteAll(const wchar_t *path, const void *data, DWORD n)
{
    const HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD put = 0;
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, data, n, &put, nullptr);
    CloseHandle(f);
}

// What the start makes of ReShade in each case, and what the menu's notice then says: the start of the two mods found no
// ReShade and no dxgi.dll beside this exe; the other cases are read from files made for them.
static void TestReShadeCases(HMODULE a, HMODULE b)
{
    typedef int (*RsFn)(bool, HMODULE, const wchar_t *, char *, int);
    const RsFn rsA = a ? (RsFn)GetProcAddress(a, "TestReShade") : nullptr, rsB = b ? (RsFn)GetProcAddress(b, "TestReShade") : nullptr;
    char note[1024] = "";
    Check(rsA && rsA(false, nullptr, nullptr, note, sizeof(note)) == -1 && strncmp(note, "ReShade is not in the game.", 27) == 0,
          "no ReShade and no dxgi.dll beside the exe: the start's notice says ReShade is not in the game");
    if (!rsB) return;
    wchar_t tmp[MAX_PATH], none[MAX_PATH], rs[MAX_PATH], edge[MAX_PATH], other[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wcscat_s(tmp, L"ng_rs_test\\");
    CreateDirectoryW(tmp, nullptr);
    swprintf_s(none, L"%snone\\dxgi.dll", tmp);
    swprintf_s(rs, L"%srs.dll", tmp);
    swprintf_s(edge, L"%sedge.dll", tmp);
    swprintf_s(other, L"%sother.dll", tmp);
    static char bytes[70000];
    static const char mark[] = "crosire's ReShade";
    memset(bytes, 'x', sizeof(bytes));
    memcpy(bytes + 100, mark, sizeof(mark) - 1);
    WriteAll(rs, bytes, 1000);
    memset(bytes, 'x', sizeof(bytes));
    memcpy(bytes + 65536 - 8, mark, sizeof(mark) - 1); // across the end of the reader's first 64 KB
    WriteAll(edge, bytes, sizeof(bytes));
    memset(bytes, 'x', sizeof(bytes));
    WriteAll(other, bytes, sizeof(bytes));
    Check(rsB(false, nullptr, none, note, sizeof(note)) == 2 && strstr(note, "Copy dxgi.dll and ReShade.ini from the mod's zip"),
          "no dxgi.dll: ReShade is missing, and the notice says what to copy");
    Check(rsB(false, nullptr, rs, note, sizeof(note)) == 1 && strstr(note, "takes no add-ons"), "a dxgi.dll that is ReShade but took no add-on: the notice says so");
    Check(rsB(false, nullptr, edge, note, sizeof(note)) == 1, "ReShade's name across the end of the reader's 64 KB is still found");
    Check(rsB(false, nullptr, other, note, sizeof(note)) == 3 && strstr(note, "another program's dxgi.dll"), "another program's dxgi.dll: the notice says so");
    Check(rsB(false, GetModuleHandleW(L"kernel32.dll"), nullptr, note, sizeof(note)) == 1, "a ReShade module that did not take the add-on: no add-ons");
    Check(rsB(true, nullptr, nullptr, note, sizeof(note)) == 0 && !note[0], "ReShade took the add-on: no notice");
    DeleteFileW(rs);
    DeleteFileW(edge);
    DeleteFileW(other);
    RemoveDirectoryW(tmp);
}

static int Presents(IDXGISwapChain *sc, int n);

// The Graphics tab: both mods are handed a stand-in for Next Gen Graphics; one of them claims the tab, so the menu has
// one, and opened on it the page reads the stand-in's switches.
static void TestGraphicsTab(HMODULE a, HMODULE b, IDXGISwapChain *sc, const wchar_t *dir)
{
    typedef bool (*TakeFn)(const wchar_t *);
    typedef int (*CountFn)(const char *);
    typedef bool (*ShowFn)(const char *);
    typedef LONG (*GetsFn)();
    const TakeFn takeA = (TakeFn)GetProcAddress(a, "TestGraphics"), takeB = (TakeFn)GetProcAddress(b, "TestGraphics");
    const CountFn count = (CountFn)GetProcAddress(a, "TestTabCount");
    const ShowFn show = (ShowFn)GetProcAddress(a, "TestShowTab");
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sng_graphics_standin.dll", dir);
    const HMODULE standin = LoadLibraryW(path);
    const GetsFn gets = standin ? (GetsFn)GetProcAddress(standin, "TestGraphicsGets") : nullptr;
    Check(takeA && takeB && count && show && gets && takeA(path) && takeB(path), "both mods take the stand-in's NgGraphicsGetApi");
    if (!takeA || !takeB || !count || !show || !gets) return;
    Presents(sc, 3);
    Check(count("Graphics") == 1, "one Graphics tab in the menu, though both mods could draw it");
    Check(show("Graphics"), "the menu opens on the Graphics tab");
    const LONG before = gets();
    Presents(sc, 3);
    char line[128];
    sprintf_s(line, "the Graphics page reads the stand-in's switches (%ld reads in 3 frames)", gets() - before);
    Check(gets() - before >= 3, line);
    show(nullptr);
}

static HWND Window(const wchar_t *title)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ng_overlay_test";
        RegisterClassW(&wc);
        registered = true;
    }
    return CreateWindowExW(0, L"ng_overlay_test", title, WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

static DXGI_SWAP_CHAIN_DESC Desc(HWND wnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 320;
    sd.BufferDesc.Height = 240;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    return sd;
}

// The factory of a device, through its adapter; nullptr when there is none.
static IDXGIFactory2 *FactoryOf(ID3D11Device *dev)
{
    IDXGIDevice *dd = nullptr;
    IDXGIAdapter *ad = nullptr;
    IDXGIFactory2 *f = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dd)) && dd && SUCCEEDED(dd->GetAdapter(&ad)) && ad)
        ad->GetParent(__uuidof(IDXGIFactory2), (void **)&f);
    if (ad) ad->Release();
    if (dd) dd->Release();
    return f;
}

static int Presents(IDXGISwapChain *sc, int n)
{
    int ok = 0;
    for (int i = 0; i < n; i++)
        if (SUCCEEDED(sc->Present(0, 0))) ok++;
    return ok;
}

int main()
{
    char line[256];
    HWND one = Window(L"one"), other = Window(L"other");
    Check(one && other, "two hidden windows");
    if (!one || !other) return 1;

    // DirectX's own method tables before the mods come in, from a swap chain made by D3D11CreateDeviceAndSwapChain (no
    // mod takes that one) and the factory of its device
    void **scTable = nullptr, *scPresent = nullptr, **fTable = nullptr, *fCreate = nullptr, *fCreateHwnd = nullptr;
    {
        DXGI_SWAP_CHAIN_DESC sd = Desc(other);
        IDXGISwapChain *sc = nullptr;
        ID3D11Device *dev = nullptr;
        ID3D11DeviceContext *ctx = nullptr;
        const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx);
        IDXGIFactory2 *f = SUCCEEDED(hr) ? FactoryOf(dev) : nullptr;
        Check(SUCCEEDED(hr) && f, "a swap chain on WARP before the mods, and its device's factory");
        if (!f) return 1;
        scTable = *(void ***)sc;
        scPresent = scTable[8];
        fTable = *(void ***)f;
        // the Steam stand-in under every other hook, as Steam's detour in DXGI's code is
        g_steamCreate = (CreateSwapChainFn)fTable[10];
        g_steamCreateHwnd = (CreateSwapChainHwndFn)fTable[15];
        TableWrite(&fTable[10], (void *)&SteamCreate);
        TableWrite(&fTable[15], (void *)&SteamCreateHwnd);
        fCreate = fTable[10];
        fCreateHwnd = fTable[15];
        f->Release();
        sc->Release();
        ctx->Release();
        dev->Release();
    }

    // DirectInput's own method tables before the mods come in, through dinput8.dll's export found by name (no mod takes
    // that way)
    typedef HRESULT(WINAPI * DiCreateFn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
    const HMODULE di8 = LoadLibraryW(L"dinput8.dll");
    const DiCreateFn diByName = di8 ? (DiCreateFn)GetProcAddress(di8, "DirectInput8Create") : nullptr;
    void **devTable = nullptr, *devState = nullptr, *devData = nullptr;
    {
        IDirectInput8W *di = nullptr;
        IDirectInputDevice8W *kb = nullptr;
        Check(diByName && SUCCEEDED(diByName(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void **)&di, nullptr)) && di &&
                  SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &kb, nullptr)) && kb,
              "a DirectInput keyboard before the mods");
        if (!kb) return 1;
        devTable = *(void ***)kb;
        devState = devTable[9];
        devData = devTable[10];
        kb->Release();
        di->Release();
    }

    // two Next Gen mods
    wchar_t dir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    *(wcsrchr(dir, L'\\') + 1) = 0;
    wchar_t pathA[MAX_PATH], pathB[MAX_PATH];
    swprintf_s(pathA, L"%sng_mod_a.dll", dir);
    swprintf_s(pathB, L"%sng_mod_b.dll", dir);
    const HMODULE a = LoadLibraryW(pathA), b = LoadLibraryW(pathB);
    const StartFn startA = a ? (StartFn)GetProcAddress(a, "TestStart") : nullptr, startB = b ? (StartFn)GetProcAddress(b, "TestStart") : nullptr;
    Check(startA && startB && a != b, "the two stand-in mods load, each its own copy");
    if (!startA || !startB) return 1;
    static volatile LONG framesA = 0, framesB = 0;
    // a NextGen.ini beside the exe that still holds a test build's default key, never picked in the menu: each start
    // moves it to F2 once. Mod A meets the first default (Delete), mod B the second (Page Down, moved there from Delete)
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%sNextGen.ini", dir);
    DeleteFileW(ini);
    WritePrivateProfileStringW(L"Menu", L"Key", L"46", ini);
    Check(startA("A", 1, &framesA), "mod A's overlay starts");
    Check(GetPrivateProfileIntW(L"Menu", L"Key", 0, ini) == VK_F2 && GetPrivateProfileIntW(L"Menu", L"KeyMoved", 0, ini) == 2,
          "NextGen.ini's first default key, Delete, moves to F2 once");
    WritePrivateProfileStringW(L"Menu", L"Key", L"34", ini);
    WritePrivateProfileStringW(L"Menu", L"KeyMoved", L"1", ini);
    Check(startB("B", 2, &framesB), "mod B's overlay starts");
    Check(GetPrivateProfileIntW(L"Menu", L"Key", 0, ini) == VK_F2 && GetPrivateProfileIntW(L"Menu", L"KeyMoved", 0, ini) == 2,
          "NextGen.ini's second default key, Page Down, moves to F2 once");
    // a fresh NextGen.ini is made with KeyMoved=2, so a Delete or Page Down set in it by hand afterwards stays, and a key
    // picked in the menu is never moved (review 5, C1)
    typedef int (*KeyFn)();
    // a method table another program put at the very start of its memory (two players' games, 2026-10-25) is copied
    // from there with the hooks in their slots, one with a readable word before it as before, one too short is refused
    const KeyFn tableA = (KeyFn)GetProcAddress(a, "TestTableCopy");
    const int tableFailed = tableA ? tableA() : -1;
    if (tableFailed) printf("  table copy check failed: bits %d\n", tableFailed);
    Check(tableFailed == 0, "a method table at the start of its memory is copied from there, one too short for its slots is refused");
    const KeyFn keyA = (KeyFn)GetProcAddress(a, "TestKeyFromIni");
    DeleteFileW(ini);
    const int fresh = keyA ? keyA() : 0;
    const bool freshFile = fresh == VK_F2 && GetPrivateProfileIntW(L"Menu", L"Key", 0, ini) == VK_F2 && GetPrivateProfileIntW(L"Menu", L"KeyMoved", 0, ini) == 2;
    WritePrivateProfileStringW(L"Menu", L"Key", L"46", ini);
    const int byHand = keyA ? keyA() : 0;
    WritePrivateProfileStringW(L"Menu", L"Key", L"34", ini);
    const int byHand2 = keyA ? keyA() : 0;
    Check(freshFile && byHand == VK_DELETE && byHand2 == VK_NEXT && GetPrivateProfileIntW(L"Menu", L"Key", 0, ini) == VK_NEXT,
          "a fresh NextGen.ini is made with KeyMoved=2, and a Delete or Page Down set in it by hand stays");
    WritePrivateProfileStringW(L"Menu", L"KeyMoved", nullptr, ini);
    WritePrivateProfileStringW(L"Menu", L"Picked", L"1", ini);
    Check(keyA && keyA() == VK_NEXT && GetPrivateProfileIntW(L"Menu", L"Key", 0, ini) == VK_NEXT, "a key picked in the menu (Picked=1) is never moved");
    WritePrivateProfileStringW(L"Menu", L"Picked", nullptr, ini);
    WritePrivateProfileStringW(L"Menu", L"Key", L"113", ini);
    WritePrivateProfileStringW(L"Menu", L"KeyMoved", L"2", ini);
    TestReShadeCases(a, b);

    // the game's way: a factory from CreateDXGIFactory1, a device on one of its adapters, the swap chain from the factory
    IDXGIFactory1 *f = nullptr;
    Check(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&f)) && f, "CreateDXGIFactory1 through the program's import");
    if (!f) return 1;
    void **ourFactoryTable = *(void ***)f;
    Check(ourFactoryTable != fTable, "the factory has a method table of the mods' own");
    IDXGIFactory4 *f4 = nullptr;
    IDXGIAdapter *warp = nullptr;
    if (SUCCEEDED(f->QueryInterface(__uuidof(IDXGIFactory4), (void **)&f4)) && f4) f4->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void **)&warp);
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    Check(warp && SUCCEEDED(D3D11CreateDevice(warp, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)) && dev,
          "D3D11CreateDevice on the factory's WARP adapter, through the program's import");
    if (!dev) return 1;
    DXGI_SWAP_CHAIN_DESC sd = Desc(one);
    IDXGISwapChain *sc = nullptr;
    Check(SUCCEEDED(f->CreateSwapChain(dev, &sd, &sc)) && sc, "CreateSwapChain on that factory");
    if (!sc) return 1;
    Check(*(void ***)sc != scTable && (*(void ***)sc)[8] != scPresent, "the swap chain has a method table of the mods' own");
    Check(Presents(sc, 10) == 10, "10 presents");
    sprintf_s(line, "both mods drew on it once for each present (A %ld frames, B %ld)", framesA, framesB);
    Check(framesA == 10 && framesB == 10, line);
    TestGraphicsTab(a, b, sc, dir);
    IDXGIFactory1 *again = nullptr;
    Check(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&again)) && again && *(void ***)again == ourFactoryTable,
          "a second factory gets the same method table, made once");
    if (again) again->Release();
    sc->Release();
    if (ctx) ctx->Release();
    dev->Release();
    if (warp) warp->Release();
    if (f4) f4->Release();
    f->Release();

    // a device made without an adapter: its own factory, through the device's adapter, makes a flip model swap chain
    ID3D11Device *dev2 = nullptr;
    ID3D11DeviceContext *ctx2 = nullptr;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev2, nullptr, &ctx2)) && dev2,
          "D3D11CreateDevice without an adapter");
    if (!dev2) return 1;
    IDXGIFactory2 *f2 = FactoryOf(dev2);
    Check(f2 && *(void ***)f2 != fTable, "the device's factory has a method table of the mods' own");
    if (!f2) return 1;
    DXGI_SWAP_CHAIN_DESC1 d1 = {};
    d1.Width = 320;
    d1.Height = 240;
    d1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d1.SampleDesc.Count = 1;
    d1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d1.BufferCount = 2;
    d1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *sc1 = nullptr;
    Check(SUCCEEDED(f2->CreateSwapChainForHwnd(dev2, one, &d1, nullptr, nullptr, &sc1)) && sc1, "CreateSwapChainForHwnd on it, in the same window");
    if (!sc1) return 1;
    const LONG wasA = framesA, wasB = framesB;
    Check(Presents(sc1, 10) == 10, "10 presents");
    sprintf_s(line, "both mods drew on the new swap chain once for each present (A %ld frames, B %ld)", framesA - wasA, framesB - wasB);
    Check(framesA - wasA == 10 && framesB - wasB == 10, line);
    const LONG was1A = framesA, was1B = framesB;
    DXGI_PRESENT_PARAMETERS pp = {};
    int presented1 = 0;
    for (int i = 0; i < 10; i++)
        if (SUCCEEDED(sc1->Present1(0, 0, &pp))) presented1++;
    Check(presented1 == 10, "10 presents by Present1");
    sprintf_s(line, "both mods drew once for each Present1 too (A %ld frames, B %ld)", framesA - was1A, framesB - was1B);
    Check(framesA - was1A == 10 && framesB - was1B == 10, line);
    sc1->Release();
    f2->Release();
    ctx2->Release();
    dev2->Release();

    // the game's mouse and keys: the mods' reads are in DirectInput's own device table since the first frame, and a
    // keyboard and a mouse made now (as a pad or a mouse plugged in later) read through them
    {
        Check(devTable[9] != devState && devTable[10] != devData, "the mods' DirectInput reads are in dinput8.dll's device table");
        IDirectInput8W *di = nullptr;
        Check(SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void **)&di, nullptr)) && di,
              "DirectInput8Create through the program's import");
        if (!di) return 1;
        IDirectInputDevice8W *kb = nullptr, *mouse = nullptr;
        const HRESULT hrKb = di->CreateDevice(GUID_SysKeyboard, &kb, nullptr), hrMouse = di->CreateDevice(GUID_SysMouse, &mouse, nullptr);
        sprintf_s(line, "a keyboard and a mouse from it (%08lx, %08lx)", (unsigned long)hrKb, (unsigned long)hrMouse);
        Check(SUCCEEDED(hrKb) && kb && SUCCEEDED(hrMouse) && mouse, line);
        if (!kb || !mouse) return 1;
        BYTE keys[256] = {};
        DIMOUSESTATE ms = {};
        DIDEVICEOBJECTDATA data[8] = {};
        DWORD count = 8;
        const bool ready = SUCCEEDED(kb->SetDataFormat(&c_dfDIKeyboard)) && SUCCEEDED(kb->SetCooperativeLevel(other, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)) &&
                           SUCCEEDED(mouse->SetDataFormat(&c_dfDIMouse)) && SUCCEEDED(mouse->SetCooperativeLevel(other, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE));
        DIPROPDWORD buffer = { { sizeof(DIPROPDWORD), sizeof(DIPROPHEADER), 0, DIPH_DEVICE }, 16 };
        const bool acquired = ready && SUCCEEDED(kb->SetProperty(DIPROP_BUFFERSIZE, &buffer.diph)) && SUCCEEDED(kb->Acquire()) && SUCCEEDED(mouse->Acquire());
        Check(acquired && SUCCEEDED(kb->GetDeviceState(sizeof keys, keys)) && SUCCEEDED(mouse->GetDeviceState(sizeof ms, &ms)) &&
                  SUCCEEDED(kb->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), data, &count, 0)),
              "the keyboard's and the mouse's reads go through the mods' hooks");
        mouse->Release();
        kb->Release();
        di->Release();
    }

    // DirectX's own tables are as they were, and a swap chain made without the program's imports gets no overlay
    {
        DXGI_SWAP_CHAIN_DESC sd3 = Desc(other);
        IDXGISwapChain *sc3 = nullptr;
        ID3D11Device *dev3 = nullptr;
        ID3D11DeviceContext *ctx3 = nullptr;
        const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd3, &sc3, &dev3, nullptr, &ctx3);
        IDXGIFactory2 *f3 = SUCCEEDED(hr) ? FactoryOf(dev3) : nullptr;
        Check(f3 != nullptr, "a swap chain by D3D11CreateDeviceAndSwapChain after the mods, and its device's factory");
        if (!f3) return 1;
        Check(*(void ***)sc3 == scTable && scTable[8] == scPresent, "DirectX's own swap chain method table is as it was, its Present slot too");
        Check(*(void ***)f3 == fTable && fTable[10] == fCreate && fTable[15] == fCreateHwnd, "DirectX's own factory method table is as it was");
        const LONG beforeA = framesA, beforeB = framesB;
        Presents(sc3, 5);
        Check(framesA == beforeA && framesB == beforeB, "that swap chain gets no overlay");
        f3->Release();
        sc3->Release();
        ctx3->Release();
        dev3->Release();
    }

    // what the Steam stand-in saw in the Present slots of the swap chains made since the mods came in
    sprintf_s(line, "the Steam stand-in saw only DirectX's own Present in every new swap chain (%d looks, %d function%s)", g_steamLooks, g_steamSawCount,
              g_steamSawCount == 1 ? "" : "s");
    Check(g_steamLooks >= 3 && g_steamSawCount == 1 && g_steamSaw[0] == scPresent, line);

    // the two mods writing slots on one read-only page at once
    {
        const SwapFn swapA = (SwapFn)GetProcAddress(a, "TestSlotSwap"), swapB = (SwapFn)GetProcAddress(b, "TestSlotSwap");
        void **page = (void **)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        DWORD old = 0;
        const bool made = swapA && swapB && page && VirtualProtect(page, 4096, PAGE_READONLY, &old);
        SwapRun ra = { swapA, page, 0 }, rb = { swapB, page ? page + 1 : nullptr, 0 };
        HANDLE th[2] = { made ? CreateThread(nullptr, 0, SwapThread, &ra, 0, nullptr) : nullptr, made ? CreateThread(nullptr, 0, SwapThread, &rb, 0, nullptr) : nullptr };
        if (th[0] && th[1]) WaitForMultipleObjects(2, th, TRUE, 60000);
        MEMORY_BASIC_INFORMATION mi = {};
        const bool readOnly = page && VirtualQuery(page, &mi, sizeof(mi)) == sizeof(mi) && mi.Protect == PAGE_READONLY;
        sprintf_s(line, "the two mods wrote slots on one read-only page at once: %d and %d of 20000 writes, the page %s", ra.done, rb.done,
                  readOnly ? "read-only again" : "NOT read-only");
        Check(th[0] && th[1] && ra.done == 20000 && rb.done == 20000 && page[0] == (void *)20000 && page[1] == (void *)20000 && readOnly, line);
        for (HANDLE h : th)
            if (h) CloseHandle(h);
    }

    printf(g_failed ? "OVERLAY TEST FAILED (%d)\n" : "OVERLAY TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}

#endif
