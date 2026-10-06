// panel_draw.h: draws the Tire Inflation System panel. Shared by the mod (panel.cpp, where Dear ImGui comes through
// ReShade's function table) and the offline preview (test\panel_preview.cpp, Dear ImGui compiled in), so the preview
// shows the mod's pixels. Include after imgui.h, with IMGUI_DEFINE_MATH_OPERATORS.
// Geometry and colours are measured from a screenshot of Expeditions' panel, 800 x 1174 px there; u converts those
// reference pixels to the screen's.
// Text is drawn in the game's own font (TT Lakes Compressed, read from the game folder) from a glyph atlas baked here
// with stb_truetype (deps\stb). The includer turns PanelFonts::rgba into a texture and sets PanelFonts::tex; until
// then, or without the font files, the current ImGui font stands in.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "panel.h"

#pragma warning(push, 0)
#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include "imstb_rectpack.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#pragma warning(pop)

// Size of the reference panel in reference pixels (an Expeditions screenshot taken at 2160p).
static const float kPanelRefW = 800.0f, kPanelRefH = 1174.0f;
// Where Expeditions puts it: the popup (gfx popup_lib "popUpTireInflation") is 400 wide on the HUD's 1920 x 1080 stage
// and hangs from the popup layer's "popupAttachTire" point at 205, 180; its title bar starts 55.7 above that point.
static const float kPanelStageW = 1920.0f, kPanelStageH = 1080.0f, kPanelStageWidth = 400.0f, kPanelStageX = 205.0f, kPanelStageY = 124.3f;

enum PanelStyle { kTitle, kMode, kHeading, kLabel, kValue, kGrade, kButton, kKey, kStyleCount };
// Cap height in reference pixels and the weight (0 Medium, 1 DemiBold) of each kind of text.
static const struct { float cap; int bold; } kPanelStyles[kStyleCount] = {
    { 37.0f, 1 }, { 26.0f, 1 }, { 27.0f, 0 }, { 26.0f, 0 }, { 27.0f, 0 }, { 26.0f, 1 }, { 28.0f, 1 }, { 21.0f, 1 } };

struct PanelFonts
{
    unsigned char *file[2] = {};                   // the Medium and DemiBold .ttf files (stb_truetype reads them in place)
    stbtt_fontinfo info[2] = {};
    float capUnits[2] = {};                        // height of 'H' in font units
    short kern[2][95][95] = {};                    // kerning of printable ASCII pairs, font units
    stbtt_packedchar glyphs[kStyleCount][95] = {};
    float unit[kStyleCount] = {};                  // font units to pixels, per style
    unsigned char *rgba = nullptr;                 // the baked atlas (white, coverage in alpha) until it is a texture
    int w = 0, h = 0;
    float u = 0.0f;                                // the scale the atlas was baked for
    ImTextureID tex = 0;                           // the atlas texture, set by the includer
};

static unsigned char *PanelReadFile(const wchar_t *path)
{
    FILE *f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return nullptr;
    unsigned char *data = nullptr;
    if (fseek(f, 0, SEEK_END) == 0)
    {
        const long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = (unsigned char *)malloc((size_t)n)) != nullptr && fread(data, 1, (size_t)n, f) != (size_t)n)
        {
            free(data);
            data = nullptr;
        }
    }
    fclose(f);
    return data;
}

// Reads the two font files from dir (the game's folder). False = no game font; the ImGui font stands in.
static bool PanelFontsLoad(PanelFonts &f, const wchar_t *dir)
{
    static const wchar_t *names[2] = { L"TTLakesCompressed-Medium.ttf", L"TTLakesCompressed-DemiBold.ttf" };
    for (int i = 0; i < 2; i++)
    {
        // (a path of any length: a fixed buffer too small for it would end the game in the CRT's handler)
        f.file[i] = PanelReadFile((std::wstring(dir) + L"\\" + names[i]).c_str());
        // a file that is no font gives no offset; stb_truetype would read far outside it with -1
        const int offset = f.file[i] ? stbtt_GetFontOffsetForIndex(f.file[i], 0) : -1;
        int x0, y0, x1, y1;
        if (offset < 0 || !stbtt_InitFont(&f.info[i], f.file[i], offset) || !stbtt_GetCodepointBox(&f.info[i], 'H', &x0, &y0, &x1, &y1) || y1 <= 0)
            return false;
        f.capUnits[i] = (float)y1;
        for (int a = 0; a < 95; a++)
            for (int b = 0; b < 95; b++) f.kern[i][a][b] = (short)stbtt_GetCodepointKernAdvance(&f.info[i], 32 + a, 32 + b);
    }
    return true;
}

// Rasterises every text style at scale u into f.rgba (the smallest square atlas it fits). The old texture, if any,
// is the includer's to free first.
static bool PanelFontsBake(PanelFonts &f, float u)
{
    free(f.rgba);
    f.rgba = nullptr;
    f.tex = 0;
    if (!f.file[0] || !f.file[1]) return false;
    for (int size = 256; size <= 4096; size *= 2)
    {
        unsigned char *alpha = (unsigned char *)calloc((size_t)size * size, 1);
        if (!alpha) return false;
        stbtt_pack_context pc;
        bool ok = stbtt_PackBegin(&pc, alpha, size, size, 0, 1, nullptr) != 0;
        if (ok)
        {
            stbtt_PackSetOversampling(&pc, 2, 1);
            for (int i = 0; i < kStyleCount && ok; i++)
            {
                const int font = kPanelStyles[i].bold;
                // the pixel height that gives this cap height
                const float px = kPanelStyles[i].cap * u / (f.capUnits[font] * stbtt_ScaleForPixelHeight(&f.info[font], 1.0f));
                f.unit[i] = stbtt_ScaleForPixelHeight(&f.info[font], px);
                ok = stbtt_PackFontRange(&pc, f.file[font], 0, px, 32, 95, f.glyphs[i]) != 0;
            }
            stbtt_PackEnd(&pc);
        }
        if (ok && (f.rgba = (unsigned char *)malloc((size_t)size * size * 4)) != nullptr)
        {
            for (size_t p = 0, n = (size_t)size * size; p < n; p++)
            {
                f.rgba[p * 4 + 0] = f.rgba[p * 4 + 1] = f.rgba[p * 4 + 2] = 255;
                f.rgba[p * 4 + 3] = alpha[p];
            }
            f.w = f.h = size;
            f.u = u;
        }
        free(alpha);
        if (f.rgba) return true;
    }
    return false;
}

static ImU32 PanelCol(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)(fminf(fmaxf(a, 0.0f), 1.0f) * 255.0f + 0.5f)); }

static int PanelGlyph(unsigned char c) { return c >= 32 && c < 127 ? c - 32 : '?' - 32; }

static float PanelTextWidth(const PanelFonts &f, int style, const char *s, float u)
{
    if (!f.tex)
    {
        const float size = kPanelStyles[style].cap * u * 1.45f;
        return ImGui::CalcTextSize(s).x * (size / ImGui::GetFontSize());
    }
    const int font = kPanelStyles[style].bold;
    float w = 0.0f;
    int prev = -1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    {
        const int g = PanelGlyph(*p);
        if (prev >= 0) w += f.kern[font][prev][g] * f.unit[style];
        w += f.glyphs[style][g].xadvance;
        prev = g;
    }
    return w;
}

// Text with its capitals centred on at.y, centred on at.x (align 0) or starting / ending there (align -1 / 1).
static void PanelText(ImDrawList *dl, const PanelFonts &f, int style, ImVec2 at, ImU32 col, const char *s, int align, float u)
{
    const float w = PanelTextWidth(f, style, s, u);
    float x = at.x - (align == 0 ? w * 0.5f : align > 0 ? w : 0.0f);
    if (!f.tex)
    {
        // the ImGui font: no bold, so a second pass a little to the right stands in for it
        const float size = kPanelStyles[style].cap * u * 1.45f;
        const ImVec2 p(x, at.y - size * 0.5f);
        dl->AddText(ImGui::GetFont(), size, p, col, s);
        if (kPanelStyles[style].bold) dl->AddText(ImGui::GetFont(), size, ImVec2(p.x + size * 0.035f, p.y), col, s);
        return;
    }
    const int font = kPanelStyles[style].bold;
    const float base = floorf(at.y + kPanelStyles[style].cap * u * 0.5f + 0.5f);
    const float iw = 1.0f / f.w, ih = 1.0f / f.h;
    dl->PushTexture(ImTextureRef(f.tex));
    int prev = -1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
    {
        const int g = PanelGlyph(*p);
        const stbtt_packedchar &q = f.glyphs[style][g];
        if (prev >= 0) x += f.kern[font][prev][g] * f.unit[style];
        if (q.x1 > q.x0)
        {
            dl->PrimReserve(6, 4);
            dl->PrimRectUV(ImVec2(x + q.xoff, base + q.yoff), ImVec2(x + q.xoff2, base + q.yoff2), ImVec2(q.x0 * iw, q.y0 * ih), ImVec2(q.x1 * iw, q.y1 * ih), col);
        }
        x += q.xadvance;
        prev = g;
    }
    dl->PopTexture();
}

static const float kPanelPi = 3.14159265f;

static ImVec2 PanelPolar(ImVec2 c, float r, float a) { return ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r); }

// Upper half disc (screen angles grow clockwise, y points down: the upper half runs from pi to 2 pi).
static void PanelHalfDisc(ImDrawList *dl, ImVec2 c, float r, ImU32 col)
{
    ImVec2 pts[97];
    for (int i = 0; i <= 96; i++) pts[i] = PanelPolar(c, r, kPanelPi + kPanelPi * i / 96.0f);
    dl->AddConvexPolyFilled(pts, 97, col);
}

// Gauge angle of a pressure position: 0 = Low on the left, 15 degrees above level, count - 1 = the highest mode in
// use on the right; Expeditions' three modes put Reduced at the top.
static float PanelGaugeAngle(float pos, int count) { return (195.0f + 150.0f * pos / (float)(count - 1)) * kPanelPi / 180.0f; }

// A drop: a round end of radius r at o narrowing to a blunt point of radius rt at tip, as one convex outline (a
// circle plus a triangle would show a seam where their smoothed edges meet): the arc round the back of the big
// circle, then the arc round the front of the small one, joined where the sides touch them.
static void PanelDrop(ImDrawList *dl, ImVec2 o, ImVec2 tip, float r, float rt, ImU32 col)
{
    const ImVec2 d = tip - o;
    const float len = sqrtf(d.x * d.x + d.y * d.y);
    if (len <= r - rt) { dl->AddCircleFilled(o, r, col, 24); return; }
    const ImVec2 ax = d * (1.0f / len), side(-ax.y, ax.x);
    const float touch = acosf((r - rt) / len); // angle from the axis to where a side touches either circle
    ImVec2 pts[34];
    int n = 0;
    for (int i = 0; i <= 24; i++)
    {
        const float t = touch + (2.0f * kPanelPi - 2.0f * touch) * i / 24.0f;
        pts[n++] = o + (ax * cosf(t) + side * sinf(t)) * r;
    }
    for (int i = 0; i <= 8; i++)
    {
        const float t = -touch + 2.0f * touch * i / 8.0f;
        pts[n++] = tip + (ax * cosf(t) + side * sinf(t)) * rt;
    }
    dl->AddConvexPolyFilled(pts, n, col);
}

// One arm of the d-pad glyph along dir from the centre c: a point at the centre, rounded at the outer end.
static void PanelDPadArm(ImDrawList *dl, ImVec2 c, ImVec2 dir, float u, bool lit, float a)
{
    const ImVec2 side(-dir.y, dir.x);
    const float hw = 11.5f * u, tip = 3.5f * u, shoulder = 14.5f * u, outer = 32.0f * u, round = 4.0f * u;
    ImVec2 pts[12];
    int n = 0;
    pts[n++] = c + dir * tip;
    pts[n++] = c + dir * shoulder + side * hw;
    for (int i = 0; i <= 3; i++)
    {
        const float t = kPanelPi * 0.5f * i / 3.0f; // the outer corners, rounded
        pts[n++] = c + dir * (outer - round + sinf(t) * round) + side * (hw - round + cosf(t) * round);
    }
    for (int i = 0; i <= 3; i++)
    {
        const float t = kPanelPi * 0.5f * i / 3.0f;
        pts[n++] = c + dir * (outer - round + cosf(t) * round) - side * (hw - round + sinf(t) * round);
    }
    pts[n++] = c + dir * shoulder - side * hw;
    if (lit)
    {
        dl->AddConvexPolyFilled(pts, n, PanelCol(216, 216, 216, a));
        return;
    }
    dl->AddConvexPolyFilled(pts, n, PanelCol(24, 24, 24, a));
    dl->AddPolyline(pts, n, PanelCol(165, 165, 165, a), ImDrawFlags_Closed, 2.5f * u);
}

// The d-pad glyph with one arm lit (-1 left, 1 right); the whole glyph dims when that way is not open.
static void PanelDPad(ImDrawList *dl, ImVec2 c, float u, int lit, bool open, float a)
{
    const float g = open ? a : a * 0.21f;
    PanelDPadArm(dl, c, ImVec2(0.0f, -1.0f), u, false, g);
    PanelDPadArm(dl, c, ImVec2(0.0f, 1.0f), u, false, g);
    PanelDPadArm(dl, c, ImVec2(-1.0f, 0.0f), u, lit < 0, g);
    PanelDPadArm(dl, c, ImVec2(1.0f, 0.0f), u, lit > 0, g);
}

// A keyboard key with its name, centred on c.
static void PanelKeyCap(ImDrawList *dl, const PanelFonts &f, ImVec2 c, float u, const char *name, float a)
{
    const float w = fmaxf(50.0f * u, PanelTextWidth(f, kKey, name, u) + 22.0f * u), h = 44.0f * u;
    dl->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), PanelCol(216, 216, 216, a), 6.0f * u);
    PanelText(dl, f, kKey, c, PanelCol(30, 30, 30, a), name, 0, u);
}

static const char *PanelRating(float m)
{
    if (m < 0.95f) return "Poor";
    if (m < 1.05f) return "Average";
    if (m < 1.2f) return "Good";
    if (m < 1.45f) return "Great";
    return "Excellent";
}

// Fuel efficiency against the Normal mode's 7 of 10 segments.
static int PanelFuelSegments(float fuel) { const int n = (int)floorf(7.0f / fmaxf(fuel, 0.1f) + 0.5f); return n < 1 ? 1 : n > 10 ? 10 : n; }
static const char *PanelFuelGrade(int n)
{
    static const char *grades[11] = { "F", "E", "D", "D+", "C", "C+", "B", "B+", "A-", "A", "A+" };
    return grades[n < 0 ? 0 : n > 10 ? 10 : n];
}

// The HUD stage's scale on a screen this size: the 1920 x 1080 stage fitted in.
static float PanelStageScale(float w, float h) { return fminf(w / kPanelStageW, h / kPanelStageH); }

// Screen pixels per reference pixel on a screen this size with the ini's UIScale.
static float PanelUnit(float w, float h, float uiScale) { return PanelStageScale(w, h) * uiScale * (kPanelStageWidth / kPanelRefW); }

// The panel's top left corner on the screen, where Expeditions has it (left of the truck). The stage is centred top to
// bottom; on a wider screen the panel keeps its distance from the left edge. UIScale grows the panel from this corner.
static ImVec2 PanelOrigin(float w, float h)
{
    const float s = PanelStageScale(w, h);
    return ImVec2(s * kPanelStageX, (h - kPanelStageH * s) * 0.5f + s * kPanelStageY);
}

// The tire damage warning's top left corner. When a truck takes damage the game shows its truck card (picture, name,
// three ratings) in the top left corner of the screen for a few seconds: its text starts 52 from the left on the
// stage and its last line ends at about 191. The warning starts below that, in line with the card's text.
static const float kPanelWarnStageX = 52.0f, kPanelWarnStageY = 205.0f;
static ImVec2 PanelWarnOrigin(float w, float h)
{
    const float s = PanelStageScale(w, h);
    return ImVec2(s * kPanelWarnStageX, (h - kPanelStageH * s) * 0.5f + s * kPanelWarnStageY);
}

// The panel with its top left corner at `at`. u: screen pixels per reference pixel. a: opacity. needle: gauge
// position, a PressureMode (the caller eases it towards the selection). keyName: the keyboard key, shown when the
// keyboard opened the panel. confirmMs: the time the choice takes to confirm itself (v.confirmMs counts it down).
static void PanelDraw(ImDrawList *dl, const PanelFonts &f, ImVec2 at, float u, float a, float needle, const PanelView &v,
                      const PanelMode modes[kModeCount], const char *keyName, int confirmMs)
{
    const int count = v.count >= 4 ? 4 : 3;
    const int sel = v.selected < 0 ? 0 : v.selected >= count ? count - 1 : (int)v.selected;
    const bool pad = v.viaPad != 0;
    const PanelMode &m = modes[sel], &n = modes[kNormal];
    const float x0 = floorf(at.x), y0 = floorf(at.y);
    const float remain = confirmMs > 0 && v.confirmMs > 0 ? fminf((float)v.confirmMs / confirmMs, 1.0f) : 0.0f;
    auto P = [&](float x, float y) { return ImVec2(x0 + x * u, y0 + y * u); };
    const float cx = kPanelRefW * 0.5f;

    // body and title bar
    dl->AddRectFilled(P(0, 0), P(kPanelRefW, kPanelRefH), PanelCol(5, 5, 5, 0.94f * a), 3.0f * u);
    dl->AddRectFilled(P(0, 0), P(kPanelRefW, 112), PanelCol(68, 82, 82, a), 3.0f * u, ImDrawFlags_RoundCornersTop);
    PanelText(dl, f, kTitle, P(cx, 60), PanelCol(255, 255, 255, a), "Tire Inflation System", 0, u);

    // gauge: light rim and base, a mid band with the marks, a dark face
    const ImVec2 c = P(cx, 444);
    PanelHalfDisc(dl, c, 266.0f * u, PanelCol(216, 216, 216, a));
    PanelHalfDisc(dl, c, 250.0f * u, PanelCol(39, 39, 39, a));
    PanelHalfDisc(dl, c, 205.0f * u, PanelCol(19, 19, 19, a));
    dl->AddRectFilled(ImVec2(c.x - 266.0f * u, c.y - 4.5f * u), ImVec2(c.x + 266.0f * u, c.y + 11.5f * u), PanelCol(216, 216, 216, a));
    for (int i = 0; i <= (count - 1) * 5; i++)
    {
        // a drop per mode, four dots between two modes
        const float ang = PanelGaugeAngle(i * 0.2f, count);
        if (i % 5) dl->AddCircleFilled(PanelPolar(c, 229.0f * u, ang), 4.5f * u, PanelCol(100, 100, 100, a), 16);
        else PanelDrop(dl, PanelPolar(c, 237.5f * u, ang), PanelPolar(c, 222.5f * u, ang), 7.8f * u, 2.5f * u, PanelCol(220, 220, 220, a));
    }
    {
        const float ang = PanelGaugeAngle(needle, count);
        const ImVec2 dir(cosf(ang), sinf(ang)), side(-dir.y, dir.x);
        // a rounded rod: about 18 px across by the hub and 8 near the tip, a bright core that fades out towards the
        // tip, darker sides (three tapered layers, measured across the reference's needle)
        auto layer = [&](float halfHub, float halfEnd, float len, int grey) {
            const ImVec2 end = c + dir * (len * u);
            dl->AddQuadFilled(c + side * (halfHub * u), end + side * (halfEnd * u), end - side * (halfEnd * u), c - side * (halfHub * u), PanelCol(grey, grey, grey, a));
        };
        layer(9.0f, 3.5f, 198.0f, 135);
        dl->AddCircleFilled(c + dir * (198.0f * u), 3.5f * u, PanelCol(135, 135, 135, a), 16);
        layer(6.5f, 2.0f, 188.0f, 178);
        layer(4.0f, 0.6f, 165.0f, 218);
        dl->AddCircleFilled(c, 24.0f * u, PanelCol(105, 105, 105, a), 32);
        dl->AddCircleFilled(c, 13.0f * u, PanelCol(217, 217, 217, a), 24);
    }

    // the mode between the d-pad glyphs (the pad lowers left, raises right); the keyboard's key steps it
    if (pad)
    {
        PanelDPad(dl, P(77.5f, 549.5f), u, -1, sel > kLow, a);
        PanelDPad(dl, P(722.5f, 549.5f), u, 1, sel < count - 1, a);
    }
    else PanelKeyCap(dl, f, P(77.5f, 549.5f), u, keyName, a);
    PanelText(dl, f, kMode, P(cx, 546.5f), PanelCol(255, 255, 255, a), m.title, 0, u);

    // fuel efficiency: 10 slanted segments (Normal = 7, the last two of those yellow) and the grade
    PanelText(dl, f, kHeading, P(cx, 657), PanelCol(250, 250, 250, a), "Fuel efficiency", 0, u);
    {
        const int segs = PanelFuelSegments(m.fuel), normal = PanelFuelSegments(n.fuel);
        const float top = 711.5f, bottom = 727.5f, slant = 11.0f, wid = 20.5f, pitch = 24.0f, left = 273.5f;
        for (int i = 0; i < 10; i++)
        {
            const float l = left + i * pitch;
            const ImU32 col = i >= segs ? PanelCol(63, 63, 63, a) : (i < normal - 2 || segs < normal) ? PanelCol(255, 255, 255, a) : PanelCol(205, 208, 112, a);
            dl->AddQuadFilled(P(l + slant, top), P(l + slant + wid, top), P(l + wid, bottom), P(l, bottom), col);
        }
        PanelText(dl, f, kGrade, P(531.0f, 722.5f), PanelCol(233, 230, 163, a), PanelFuelGrade(segs), -1, u);
    }

    // traction ratings against Normal, underlined where better
    PanelText(dl, f, kHeading, P(cx, 811), PanelCol(250, 250, 250, a), "Tire traction", 0, u);
    {
        const struct { const char *label; float v, base; } cols[3] = { { "Ground:", m.ground, n.ground }, { "Asphalt:", m.asphalt, n.asphalt }, { "Mud:", m.mud, n.mud } };
        for (int i = 0; i < 3; i++)
        {
            const float x = cx + (i - 1) * 260.0f;
            PanelText(dl, f, kLabel, P(x, 881.5f), PanelCol(142, 150, 152, a), cols[i].label, 0, u);
            const float rel = cols[i].v / fmaxf(cols[i].base, 0.01f);
            const char *word = PanelRating(rel);
            PanelText(dl, f, kValue, P(x, 941.0f), rel < 0.95f ? PanelCol(235, 120, 100, a) : PanelCol(255, 255, 255, a), word, 0, u);
            if (cols[i].v > cols[i].base * 1.04f)
            {
                const float hw = PanelTextWidth(f, kValue, word, u) * 0.5f / u + 37.0f;
                dl->AddRectFilled(P(x - hw, 969.5f), P(x + hw, 978.5f), PanelCol(225, 58, 70, a));
            }
        }
    }

    // confirm: the pad's A, or for the keyboard a clock face that empties as the choice confirms itself
    dl->AddRectFilled(P(0, 1016.5f), P(kPanelRefW, 1019.5f), PanelCol(33, 33, 33, a));
    {
        const float textW = PanelTextWidth(f, kButton, "Confirm", u) / u;
        const float bw = 19.0f + 63.0f + 23.0f + textW + 27.0f, bl = floorf(cx - bw * 0.5f);
        dl->AddRectFilled(P(bl, 1050), P(bl + bw, 1145), PanelCol(20, 20, 20, a), 3.0f * u);
        const ImVec2 ic = P(bl + 19.0f + 31.5f, 1097.5f);
        if (pad)
        {
            dl->AddCircleFilled(ic, 31.5f * u, PanelCol(216, 216, 216, a), 40);
            PanelText(dl, f, kButton, ic, PanelCol(40, 40, 40, a), "A", 0, u);
            if (remain > 0.0f) dl->AddRectFilled(P(bl, 1145.0f), P(bl + bw * remain, 1150.0f), PanelCol(225, 58, 70, a));
        }
        else
        {
            dl->AddCircle(ic, 28.0f * u, PanelCol(216, 216, 216, a), 40, 4.0f * u);
            if (remain > 0.0f)
            {
                const float frac = remain;
                ImVec2 pts[34];
                pts[0] = ic;
                for (int i = 0; i <= 32; i++) pts[1 + i] = PanelPolar(ic, 20.0f * u, -kPanelPi * 0.5f + 2.0f * kPanelPi * frac * i / 32.0f);
                if (frac > 0.5f)
                {
                    // AddConvexPolyFilled needs a convex shape: more than half a pie goes as two halves
                    dl->AddConvexPolyFilled(pts, 18, PanelCol(216, 216, 216, a));
                    pts[16] = ic;
                    dl->AddConvexPolyFilled(pts + 16, 18, PanelCol(216, 216, 216, a));
                }
                else dl->AddConvexPolyFilled(pts, 34, PanelCol(216, 216, 216, a));
            }
        }
        PanelText(dl, f, kButton, P(bl + 19.0f + 63.0f + 23.0f, 1096.5f), PanelCol(241, 241, 241, a), "Confirm", -1, u);
    }
}

// The warning while the truck is too fast for its tire pressure, at PanelWarnOrigin (the panel is closed then): the
// panel's width, its body and its red. wear: the most worn tire's damage in percent of what it takes, as a number
// and a bar along the bottom (the game's own wheel icon only shows a tire once it is flat); below 0 = not known.
static const float kPanelWarnH = 148.0f;
static void PanelWarning(ImDrawList *dl, const PanelFonts &f, ImVec2 at, float u, float a, int wear)
{
    const float x0 = floorf(at.x), y0 = floorf(at.y);
    auto P = [&](float x, float y) { return ImVec2(x0 + x * u, y0 + y * u); };
    dl->AddRectFilled(P(0, 0), P(kPanelRefW, kPanelWarnH), PanelCol(5, 5, 5, 0.94f * a), 3.0f * u);
    dl->AddRectFilled(P(0, 0), P(12, kPanelWarnH), PanelCol(225, 58, 70, a), 3.0f * u, ImDrawFlags_RoundCornersLeft);
    PanelText(dl, f, kMode, P(44, 50), PanelCol(255, 255, 255, a), "TIRE DAMAGE", -1, u);
    PanelText(dl, f, kLabel, P(44, 101), PanelCol(142, 150, 152, a), "Slow down or raise the pressure", -1, u);
    if (wear >= 0)
    {
        char text[8];
        const int shown = wear > 100 ? 100 : wear;
        snprintf(text, sizeof text, "%d%%", shown);
        PanelText(dl, f, kMode, P(kPanelRefW - 44, 50), PanelCol(225, 58, 70, a), text, 1, u);
        dl->AddRectFilled(P(12, kPanelWarnH - 9), P(kPanelRefW, kPanelWarnH), PanelCol(33, 33, 33, a), 3.0f * u, ImDrawFlags_RoundCornersBottomRight);
        dl->AddRectFilled(P(12, kPanelWarnH - 9), P(12 + (kPanelRefW - 12) * (float)shown / 100.0f, kPanelWarnH), PanelCol(225, 58, 70, a));
    }
}
