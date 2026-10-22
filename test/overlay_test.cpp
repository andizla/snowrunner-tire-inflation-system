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
//   dinput8.dll's device table after the first frame, and a keyboard and a mouse made after it read through them.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <stdio.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#ifdef OVERLAY_TEST_MOD

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
static int g_failed = 0;

static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok    " : "FAILED", what);
    fflush(stdout);
    if (!ok) g_failed++;
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
    Check(startA("A", 1, &framesA), "mod A's overlay starts");
    Check(startB("B", 2, &framesB), "mod B's overlay starts");

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

    printf(g_failed ? "OVERLAY TEST FAILED (%d)\n" : "OVERLAY TEST OK\n", g_failed);
    return g_failed ? 1 : 0;
}

#endif
