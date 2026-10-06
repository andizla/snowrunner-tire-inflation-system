// panel_preview.cpp: draws the pressure panel without the game, into PNG files, with the mod's own drawing code
// (src\panel_draw.h). Dear ImGui is compiled in (its full sources, see test\build_preview.bat) and renders on a
// Direct3D 11 WARP device: no game, display or graphics card needed.
//   panel_preview.exe <out dir> <font dir> [reference.png or -] [<picture> <x> <y> <scale> <mode> <name>]...
// Each group of six after the reference draws the panel on a picture, for the mod pages: <name>.png is a 1920 x 1080
// frame filled by the picture (cut to 16:9 around its middle), with the panel's top left corner at x, y, at <scale>
// times its size in the game, at mode 0 to 3 (Low, Reduced, Normal, Increased), as the pad shows it.
// Writes, in <out dir>:
//   compare.png   the reference screenshot of Expeditions' panel (when given) next to ours at its scale, Normal
//   states.png    pad Low / Reduced / Normal / Increased and keyboard Reduced at the 1080p size
//   placed.png    the panel at its place on a 1080p screen
//   warning.png   the tire damage warning
//   fallback.png  Reduced with the ImGui font standing in (no game font files)
//   settings.png  the Tire Inflation System tab of ReShade's overlay (settings_page.h) with a too grippy modded tire, at
//                 ReShade FontScale 2
#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <stdio.h>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "panel_draw.h"
#include "settings_page.h"

volatile LONG g_pageCapturing = 0; // panel.cpp's, which is not part of this program

static ID3D11Device *g_dev = nullptr;
static ID3D11DeviceContext *g_ctx = nullptr;
static IWICImagingFactory *g_wic = nullptr;

template <typename T> static void Release(T *&p) { if (p) { p->Release(); p = nullptr; } }

static ID3D11ShaderResourceView *MakeTexture(const uint8_t *rgba, int w, int h)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA sd = { rgba, (UINT)w * 4, 0 };
    ID3D11Texture2D *tex = nullptr;
    ID3D11ShaderResourceView *srv = nullptr;
    if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &tex))) g_dev->CreateShaderResourceView(tex, nullptr, &srv);
    Release(tex);
    return srv;
}

static bool LoadImageRgba(const wchar_t *path, std::vector<uint8_t> &out, UINT &w, UINT &h)
{
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *conv = nullptr;
    bool ok = SUCCEEDED(g_wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(g_wic->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(conv->GetSize(&w, &h));
    if (ok)
    {
        out.resize((size_t)w * h * 4);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)out.size(), out.data()));
    }
    Release(conv);
    Release(frame);
    Release(dec);
    return ok;
}

static bool SavePng(const wchar_t *path, const uint8_t *bgra, UINT w, UINT h)
{
    IWICStream *st = nullptr;
    IWICBitmapEncoder *enc = nullptr;
    IWICBitmapFrameEncode *fr = nullptr;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    bool ok = SUCCEEDED(g_wic->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(path, GENERIC_WRITE)) &&
              SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(w, h)) &&
              SUCCEEDED(fr->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) &&
              SUCCEEDED(fr->WritePixels(h, w * 4, w * h * 4, (BYTE *)bgra)) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
    Release(fr);
    Release(enc);
    Release(st);
    return ok;
}

// ImGui frames of w x h: draw() fills the draw lists, the last frame goes to path as PNG (earlier ones let windows
// that size themselves settle).
template <typename F> static bool RenderPng(const wchar_t *path, int w, int h, F draw, int frames = 1)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt = nullptr, *staging = nullptr;
    ID3D11RenderTargetView *rtv = nullptr;
    bool ok = SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &rt)) && SUCCEEDED(g_dev->CreateRenderTargetView(rt, nullptr, &rtv));
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ok = ok && SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &staging));
    if (ok)
    {
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DeltaTime = 1.0f / 60.0f;
        for (int i = 0; i < frames; i++)
        {
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            draw(ImGui::GetForegroundDrawList(), ImGui::GetBackgroundDrawList());
            ImGui::Render();
        }
        const float grey[4] = { 0.16f, 0.17f, 0.16f, 1.0f };
        g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
        g_ctx->ClearRenderTargetView(rtv, grey);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_ctx->CopyResource(staging, rt);
        D3D11_MAPPED_SUBRESOURCE map = {};
        ok = SUCCEEDED(g_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map));
        if (ok)
        {
            std::vector<uint8_t> bgra((size_t)w * h * 4);
            for (int y = 0; y < h; y++)
            {
                const uint8_t *s = (const uint8_t *)map.pData + (size_t)y * map.RowPitch;
                uint8_t *d = bgra.data() + (size_t)y * w * 4;
                for (int x = 0; x < w; x++, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; }
            }
            g_ctx->Unmap(staging, 0);
            ok = SavePng(path, bgra.data(), (UINT)w, (UINT)h);
        }
    }
    Release(staging);
    Release(rtv);
    Release(rt);
    wprintf(L"%s %s\n", ok ? L"wrote" : L"FAILED", path);
    return ok;
}

// Bakes the font atlas for u and makes it the texture the drawing uses.
static ID3D11ShaderResourceView *g_fontSrv = nullptr;
static bool UseFonts(PanelFonts &f, float u)
{
    Release(g_fontSrv);
    if (!PanelFontsBake(f, u)) return false;
    g_fontSrv = MakeTexture(f.rgba, f.w, f.h);
    free(f.rgba);
    f.rgba = nullptr;
    f.tex = (ImTextureID)(intptr_t)g_fontSrv;
    printf("font atlas %dx%d for u %.3f\n", f.w, f.h, u);
    return g_fontSrv != nullptr;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3)
    {
        fwprintf(stderr, L"usage: panel_preview <out dir> <font dir> [reference.png]\n");
        return 2;
    }
    const wchar_t *out = argv[1], *fontDir = argv[2], *refPath = argc > 3 && wcscmp(argv[3], L"-") ? argv[3] : nullptr;
    CreateDirectoryW(out, nullptr);
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic))))
    {
        fwprintf(stderr, L"no WIC\n");
        return 1;
    }
    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &g_dev, &fl, &g_ctx)))
    {
        fwprintf(stderr, L"no D3D11 WARP device\n");
        return 1;
    }
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault(); // ReShade's font when its ini names none: the stand-in without the game's font
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    static PanelFonts fonts;
    if (!PanelFontsLoad(fonts, fontDir)) wprintf(L"game font not found in %s: the ImGui font stands in\n", fontDir);

    PanelMode modes[kModeCount] = { { "LOW PRESSURE", 3.5f, 0.85f, 1.3f, 1.5f }, { "REDUCED PRESSURE", 3.0f, 0.95f, 1.15f, 1.25f },
                                    { "NORMAL PRESSURE", 1.0f, 1.0f, 1.0f, 1.0f }, { "INCREASED PRESSURE", 0.9f, 1.3f, 0.85f, 0.85f } };
    auto view = [](int sel, int pad, int confirmMs, int count) {
        PanelView v = {};
        v.open = 1;
        v.selected = sel;
        v.current = kNormal;
        v.confirmMs = confirmMs;
        v.viaPad = pad;
        v.count = count;
        return v;
    };
    wchar_t path[MAX_PATH];

    // the reference next to ours, both panels at the same place in their halves (the reference's panel starts at 43, 24)
    std::vector<uint8_t> ref;
    UINT rw = 0, rh = 0;
    ID3D11ShaderResourceView *refSrv = nullptr;
    if (refPath && LoadImageRgba(refPath, ref, rw, rh)) refSrv = MakeTexture(ref.data(), (int)rw, (int)rh);
    if (refSrv)
    {
        UseFonts(fonts, 1.0f);
        const PanelView v = view(kNormal, 1, -1, 3); // the three modes of the reference
        swprintf_s(path, L"%s\\compare.png", out);
        RenderPng(path, (int)rw * 2 + 20, (int)rh, [&](ImDrawList *fg, ImDrawList *bg) {
            bg->AddImage((ImTextureID)(intptr_t)refSrv, ImVec2(0, 0), ImVec2((float)rw, (float)rh));
            bg->AddRectFilled(ImVec2((float)rw + 20.0f, 0), ImVec2((float)rw * 2 + 20.0f, (float)rh), IM_COL32(6, 6, 6, 255));
            PanelDraw(fg, fonts, ImVec2((float)rw + 20.0f + 43.0f, 24.0f), 1.0f, 1.0f, (float)kNormal, v, modes, "F3", 2000);
        });
    }

    // the states at the 1080p size
    const float u = PanelUnit(1920.0f, 1080.0f, 1.0f);
    UseFonts(fonts, u);
    const float pw = kPanelRefW * u, ph = kPanelRefH * u, gap = 24.0f;
    const PanelView states[5] = { view(kLow, 1, 600, 4), view(kReduced, 1, 1300, 4), view(kNormal, 1, 2000, 4), view(kIncreased, 1, 1300, 4),
                                  view(kReduced, 0, 1300, 4) };
    swprintf_s(path, L"%s\\states.png", out);
    RenderPng(path, (int)(5 * pw + 6 * gap), (int)(ph + 2 * gap), [&](ImDrawList *fg, ImDrawList *) {
        for (int i = 0; i < 5; i++)
            PanelDraw(fg, fonts, ImVec2(gap + i * (pw + gap), gap), u, 1.0f, (float)states[i].selected, states[i], modes, "F3", 2000);
    });

    // at its place on a 1080p screen (Expeditions' place), with the old centred place outlined
    swprintf_s(path, L"%s\\placed.png", out);
    RenderPng(path, 1920, 1080, [&](ImDrawList *fg, ImDrawList *bg) {
        bg->AddRectFilled(ImVec2(0, 0), ImVec2(1920, 1080), IM_COL32(74, 80, 70, 255));
        const float uo = 420.0f / kPanelRefW, ow = kPanelRefW * uo, oh = kPanelRefH * uo;
        bg->AddRect(ImVec2((1920 - ow) * 0.5f, (1080 - oh) * 0.5f), ImVec2((1920 + ow) * 0.5f, (1080 + oh) * 0.5f), IM_COL32(255, 255, 255, 110), 0.0f, 0, 2.0f);
        PanelDraw(fg, fonts, PanelOrigin(1920.0f, 1080.0f), u, 1.0f, (float)kReduced, states[1], modes, "F3", 2000);
    });

    // the tire damage warning
    swprintf_s(path, L"%s\\warning.png", out);
    RenderPng(path, (int)(pw + 2 * gap), (int)(kPanelWarnH * u + 2 * gap), [&](ImDrawList *fg, ImDrawList *bg) {
        bg->AddRectFilled(ImVec2(0, 0), ImVec2(pw + 2 * gap, kPanelWarnH * u + 2 * gap), IM_COL32(74, 80, 70, 255));
        PanelWarning(fg, fonts, ImVec2(gap, gap), u, 1.0f, 54);
    });

    // without the game's font
    ImTextureID keep = fonts.tex;
    fonts.tex = 0;
    swprintf_s(path, L"%s\\fallback.png", out);
    RenderPng(path, (int)(pw + 2 * gap), (int)(ph + 2 * gap), [&](ImDrawList *fg, ImDrawList *) {
        PanelDraw(fg, fonts, ImVec2(gap, gap), u, 1.0f, (float)kReduced, states[1], modes, "F3", 2000);
    });
    fonts.tex = keep;

    // the panel on pictures
    for (int i = 4; i + 5 < argc; i += 6)
    {
        std::vector<uint8_t> pic;
        UINT w = 0, h = 0;
        ID3D11ShaderResourceView *srv = LoadImageRgba(argv[i], pic, w, h) ? MakeTexture(pic.data(), (int)w, (int)h) : nullptr;
        if (!srv) { wprintf(L"FAILED to read %s\n", argv[i]); continue; }
        const float x = (float)_wtof(argv[i + 1]), y = (float)_wtof(argv[i + 2]), us = u * (float)_wtof(argv[i + 3]);
        const int mode = min(max(_wtoi(argv[i + 4]), 0), kModeCount - 1);
        UseFonts(fonts, us);
        // the part of the picture that fills a 16:9 frame, as texture coordinates around its middle
        const float cut = min((float)w / 1920.0f, (float)h / 1080.0f), cw = 960.0f * cut / (float)w, ch = 540.0f * cut / (float)h;
        const PanelView v = view(mode, 1, 1300, 4);
        swprintf_s(path, L"%s\\%s.png", out, argv[i + 5]);
        RenderPng(path, 1920, 1080, [&](ImDrawList *fg, ImDrawList *bg) {
            bg->AddImage((ImTextureID)(intptr_t)srv, ImVec2(0, 0), ImVec2(1920, 1080), ImVec2(0.5f - cw, 0.5f - ch), ImVec2(0.5f + cw, 0.5f + ch));
            PanelDraw(fg, fonts, ImVec2(x, y), us, 1.0f, (float)mode, v, modes, "F3", 2000);
        });
        Release(srv);
    }

    // the settings page, as ReShade's overlay shows it with FontScale=2 (ReShade's own style differs)
    {
        TpSettings s;
        SettingsDefaults(s);
        s.vanillaBalance = true;
        TpLive live = {};
        live.wheels = 4;
        live.ground = 3.7f; // a modded truck's tire: better than every vanilla tire in all three
        live.asphalt = 3.5f;
        live.mud = 3.6f;
        live.balance = VanillaBalance(live.ground, live.asphalt, live.mud, s.balanceStrength);
        live.room = 0.067f; // a 0.47 m tire with 0.019 m of its own
        PageState st;
        ImGui::GetStyle().FontScaleMain = 2.0f;
        ImGui::GetStyle().ScaleAllSizes(2.0f);
        swprintf_s(path, L"%s\\settings.png", out);
        RenderPng(path, 1100, 1900, [&](ImDrawList *, ImDrawList *) {
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowSize(ImVec2(1080, 0));
            ImGui::Begin("Tire Inflation System", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            SettingsPage(s, live, st, 0);
            ImGui::End();
        }, 3);

        // The pad binding window, run as the page runs it: "Open the panel" pressed with the pad's A (still down for
        // two frames), then LB and d-pad down held and let go one after the other. padbind.png shows the window
        // while both are held. Then a capture nobody answers gives up and keeps the old binding.
        const uint32_t A = XINPUT_GAMEPAD_A, LB = XINPUT_GAMEPAD_LEFT_SHOULDER, DN = XINPUT_GAMEPAD_DPAD_DOWN;
        const uint32_t script[10] = { A, A, 0, 0, LB, LB | DN, LB | DN, DN, 0, 0 };
        int frame = 0, act = 0;
        auto page = [&](ImDrawList *, ImDrawList *) {
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowSize(ImVec2(1080, 0));
            ImGui::Begin("Tire Inflation System", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            act |= SettingsPage(s, live, st, script[frame < 10 ? frame : 9]);
            ImGui::End();
            frame++;
        };
        strcpy_s(s.pad[0], "DPadDown");
        st.capture = 1;
        st.peak = 0;
        st.armed = false;
        st.began = GetTickCount64();
        swprintf_s(path, L"%s\\padbind.png", out);
        RenderPng(path, 1100, 1900, page, 7);
        const bool held = st.capture == 1 && st.peak == (LB | DN) && !strcmp(s.pad[0], "DPadDown");
        swprintf_s(path, L"%s\\padbind_done.png", out);
        RenderPng(path, 1100, 1900, page, 3);
        const bool bound = st.capture == -1 && !strcmp(s.pad[0], "LB+DPadDown") && (act & kPageChanged);
        printf("%s pad binding: A let go first, then LB+DPadDown read and kept (%s)\n", held && bound ? "ok  " : "FAIL", s.pad[0]);
        st.capture = 2;
        st.armed = false;
        st.began = GetTickCount64() - kPageCaptureMs - 1000;
        frame = 9;
        act = 0;
        RenderPng(path, 1100, 1900, page, 2);
        const bool gaveUp = st.capture == -1 && !strcmp(s.pad[1], "LB+DPadLeft") && !(act & kPageChanged);
        printf("%s pad binding: left alone it gives up and keeps %s\n", gaveUp ? "ok  " : "FAIL", s.pad[1]);

        // The frame a capture begins on, which the page run above does not have (it sets the capture by hand): the
        // pad's A pressed the binding's button and is still down. First with the pad read on that frame, then with
        // it read as nothing there, as the page did until 2026-10-04 (it then took that A for the binding).
        auto capture = [&](const uint32_t *pad, int frames) {
            PageState c;
            c.capture = 1;
            c.began = GetTickCount64();
            uint32_t got = 0;
            bool over = false;
            for (int i = 0; i < frames && !over; i++) over = PageCaptureStep(c, pad[i], i == 0, false, c.began + (unsigned long long)i * 16, got);
            return over ? got : 0xFFFFFFFFu;
        };
        const uint32_t withA[8] = { A, A, 0, LB, LB | DN, DN, 0, 0 }, blind[6] = { 0, A, A, 0, LB, 0 };
        const uint32_t first = capture(withA, 8), second = capture(blind, 6);
        printf("%s pad binding: the A that begins a capture is not bound (%s, then %s)\n", first == (LB | DN) && second == LB ? "ok  " : "FAIL",
               first == (LB | DN) ? "LB+DPadDown" : "wrong", second == LB ? "LB" : "wrong");
    }

    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    Release(refSrv);
    Release(g_fontSrv);
    Release(g_ctx);
    Release(g_dev);
    Release(g_wic);
    CoUninitialize();
    return 0;
}
