// build_find.h: finds, in an image of SnowRunner.exe, the few places the mod needs that lie elsewhere in every build
// of the game, by what the game's code looks like around them, and checks that the game's objects are laid out as the
// mod expects. The mod runs it on the running game's own image at its start; the offline test runs it on a copy of an
// image in a file. A place has to be found exactly once and every check has to hold, or the mod stands down rather than
// write to places it only guesses; only the game-thread path may be missing (BuildFindAll says which places that is).
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

struct Build
{
    uint32_t stamp, image;      // the exe's link time and its size in memory
    uint64_t control;           // RVA of the truck control global
    uint64_t wheelVtable;       // RVA of the vtable of a wheel's parameter object (combine::TRUCK_WHEEL_PARAMS)
    uint64_t cylinderVtable;    // RVA of the vtable of the wheel's collision shape (hkpCylinderShape)
    uint64_t damageUpdate;      // RVA of the game's damage update
    uint64_t foregroundSlot;    // RVA of the exe's import slot of GetForegroundWindow
    uint64_t truckUpdateReturn; // RVA the truck update's call through that slot returns to
    uint64_t firstArgument;     // from the place of that call's return address on the stack up to the truck update's
                                // saved first argument
};

// An image as the loader lays it out (offset = RVA). base: what the pointers stored in it are relative to.
struct BuildImage
{
    const uint8_t *p;
    size_t n;
    uint64_t base;
    uint16_t u16(size_t o) const { uint16_t v = 0; if (o + 2 <= n) memcpy(&v, p + o, 2); return v; }
    uint32_t u32(size_t o) const { uint32_t v = 0; if (o + 4 <= n) memcpy(&v, p + o, 4); return v; }
    uint64_t u64(size_t o) const { uint64_t v = 0; if (o + 8 <= n) memcpy(&v, p + o, 8); return v; }
    int32_t i32(size_t o) const { return (int32_t)u32(o); }
    size_t header() const { return u32(0x3C); }
    size_t optional() const { return header() + 24; }
    uint32_t directory(int i, bool size = false) const { return u32(optional() + 112 + i * 8 + (size ? 4 : 0)); }
};

// "48 8b ?? e8" as bytes, ?? = any byte (-1).
static std::vector<int> BuildPattern(const char *text)
{
    std::vector<int> out;
    for (const char *c = text; *c;)
    {
        if (*c == ' ') { c++; continue; }
        if (c[0] == '?') out.push_back(-1);
        else
        {
            auto digit = [](char x) { return x >= '0' && x <= '9' ? x - '0' : (x | 32) - 'a' + 10; };
            out.push_back(digit(c[0]) * 16 + digit(c[1]));
        }
        c += 2;
    }
    return out;
}

// The places in the image's code sections, between from and to, where the bytes are the pattern's: at most `most`.
// The pattern has to begin with a fixed byte.
static std::vector<uint32_t> BuildFind(const BuildImage &im, const char *text, uint32_t from = 0, uint32_t to = 0xFFFFFFFFu, size_t most = 4)
{
    const std::vector<int> pat = BuildPattern(text);
    std::vector<uint32_t> hits;
    const size_t sections = im.u16(im.header() + 6), first = im.optional() + im.u16(im.header() + 20);
    for (size_t s = 0; s < sections && hits.size() < most; s++)
    {
        const size_t h = first + s * 40;
        if (!(im.u32(h + 36) & 0x20000000)) continue; // not code
        size_t begin = im.u32(h + 12), end = begin + im.u32(h + 8);
        if (begin < from) begin = from;
        if (end > to) end = to;
        if (end > im.n) end = im.n;
        if (end < begin + pat.size()) continue;
        end -= pat.size();
        for (size_t o = begin; o <= end && hits.size() < most; o++)
        {
            const uint8_t *next = (const uint8_t *)memchr(im.p + o, pat[0], end - o + 1);
            if (!next) break;
            o = (size_t)(next - im.p);
            size_t k = 1;
            while (k < pat.size() && (pat[k] < 0 || im.p[o + k] == pat[k])) k++;
            if (k == pat.size()) hits.push_back((uint32_t)o);
        }
    }
    return hits;
}

static void BuildMiss(std::wstring &report, const wchar_t *what, size_t found)
{
    if (!report.empty()) report += L"; ";
    report += what;
    report += found ? L" (found more than once)" : L" (not found)";
}

// The vtable of a class, by the name its type information carries (".?AVhkpCylinderShape@@"): the type descriptor
// holds the name, the object locator of the class's first vtable points at the descriptor and at itself, and the
// vtable begins right after the pointer to that locator.
static uint64_t BuildVtable(const BuildImage &im, const char *name, const wchar_t *what, std::wstring &report)
{
    const size_t len = strlen(name) + 1;
    std::vector<uint64_t> found;
    for (size_t s = 16; s + len <= im.n; s++)
    {
        const uint8_t *next = (const uint8_t *)memchr(im.p + s, name[0], im.n - len - s + 1);
        if (!next) break;
        s = (size_t)(next - im.p);
        if (memcmp(next, name, len)) continue;
        const uint32_t type = (uint32_t)(s - 16);
        for (size_t o = 0; o + 24 <= im.n; o += 4)
        {
            uint32_t at[6];
            memcpy(at, im.p + o, sizeof at);
            if (at[3] != type || at[0] != 1 || at[1] != 0 || at[5] != o) continue;
            const uint64_t locator = im.base + o;
            for (size_t v = 0; v + 16 <= im.n; v += 8)
                if (!memcmp(im.p + v, &locator, 8)) found.push_back(v + 8);
        }
    }
    if (found.size() != 1) BuildMiss(report, what, found.size());
    return found.size() == 1 ? found[0] : 0;
}

// The exe's import slot of a function.
static uint32_t BuildImportSlot(const BuildImage &im, const char *dll, const char *function)
{
    for (size_t d = im.directory(1); d && d + 20 <= im.n && im.u32(d + 12); d += 20)
    {
        const size_t name = im.u32(d + 12);
        if (name >= im.n || _stricmp((const char *)im.p + name, dll)) continue;
        const size_t names = im.u32(d) ? im.u32(d) : im.u32(d + 16), slots = im.u32(d + 16);
        for (size_t k = 0; names + k * 8 + 8 <= im.n && im.u64(names + k * 8); k++)
        {
            const uint64_t v = im.u64(names + k * 8);
            if ((v >> 63) || v + 2 >= im.n) continue; // by ordinal
            if (!strcmp((const char *)im.p + v + 2, function)) return (uint32_t)(slots + k * 8);
        }
    }
    return 0;
}

// The function a code address lies in, with what its unwind data says about its stack: registers pushed and bytes
// of locals. False when there is none, or its unwind data is chained to another's (then this does not tell).
static bool BuildFunction(const BuildImage &im, uint32_t rva, uint32_t &begin, uint32_t &end, uint32_t &pushes, uint32_t &locals)
{
    const size_t table = im.directory(3), size = im.directory(3, true);
    for (size_t o = table; o + 12 <= table + size && o + 12 <= im.n; o += 12)
    {
        begin = im.u32(o);
        end = im.u32(o + 4);
        if (!(begin <= rva && rva < end)) continue;
        const size_t u = im.u32(o + 8);
        if (u + 4 > im.n || ((im.p[u] >> 3) & 4)) return false;
        const int codes = im.p[u + 2];
        pushes = locals = 0;
        for (int i = 0; i < codes; i++)
        {
            const size_t c = u + 4 + i * 2;
            if (c + 6 > im.n) return false;
            const int op = im.p[c + 1] & 15, info = im.p[c + 1] >> 4;
            if (op == 0) pushes++;                                                             // push of a register
            else if (op == 1) { locals += info ? im.u32(c + 2) : im.u16(c + 2) * 8; i += info ? 2 : 1; } // large allocation
            else if (op == 2) locals += info * 8 + 8;                                          // small allocation
            else if (op == 4 || op == 8) i += 1;                                               // a register saved
            else if (op == 5 || op == 9) i += 2;
        }
        return true;
    }
    return false;
}

// The global a small accessor hands out, once the code at `function` is "lea rax, [rip + x]; ret"; else 0.
static uint64_t BuildAccessorTarget(const BuildImage &im, uint64_t function)
{
    if (function + 8 > im.n || im.p[function] != 0x48 || im.p[function + 1] != 0x8d || im.p[function + 2] != 0x05 || im.p[function + 7] != 0xc3) return 0;
    return function + 7 + im.i32((size_t)function + 3);
}

// The control global, three ways, the first that stands once (only > 0: that way alone, for the offline test):
// (1) the second of three small "return the address of a global" calls in a row, inside the truck update where that
// is known (from, to), else anywhere in the code; (2) the same with the stack frame offsets left open, as another
// build of the same source keeps the registers but not always the frame (the Game Pass build of 2026-10, stamp
// 6a607d39, had neither the truck update's call nor this pattern); (3) the small function that hands out the static
// object control is a field of, 0x18 into it, by the code around that function: the tail of the one before it, the
// function itself and the start of the one after, as the gauge mod's harvest of the Steam build has it.
static uint64_t BuildControl(const BuildImage &im, uint32_t from, uint32_t to, std::wstring &report, int only = 0)
{
    static const char *const kThreeCalls[2] = {
        "e8 ?? ?? ?? ?? 4c 8b 20 4c 89 65 c8 e8 ?? ?? ?? ?? 48 8b 30 48 89 75 a0 e8 ?? ?? ?? ?? 48 89 45 18 4c 8b 00 80 be d8 00 00 00 00",
        "e8 ?? ?? ?? ?? 4c 8b 20 4c 89 65 ?? e8 ?? ?? ?? ?? 48 8b 30 48 89 75 ?? e8 ?? ?? ?? ?? 48 89 45 ?? 4c 8b 00 80 be d8 00 00 00 00" };
    size_t most = 0;
    for (int way = 1; way <= 2; way++)
    {
        if (only && only != way) continue;
        const std::vector<uint32_t> hits = BuildFind(im, kThreeCalls[way - 1], from, to);
        most = hits.size() > most ? hits.size() : most;
        if (hits.size() != 1) continue;
        const uint64_t control = BuildAccessorTarget(im, (uint64_t)hits[0] + 17 + im.i32(hits[0] + 13));
        if (control) return control;
    }
    if (!only || only == 3)
    {
        // the accessor is 9 bytes into the window, after the ret and the padding of the function before it
        const std::vector<uint32_t> hits = BuildFind(im, "c3 cc cc cc cc cc cc cc cc 48 8d 05 ?? ?? ?? ?? c3 cc cc cc cc cc cc cc cc 40 53 48 83 ec 60 48");
        most = hits.size() > most ? hits.size() : most;
        if (hits.size() == 1)
        {
            const uint64_t object = BuildAccessorTarget(im, (uint64_t)hits[0] + 9);
            if (object) return object + 0x18;
        }
    }
    BuildMiss(report, L"the control global", most > 1 ? most : 0);
    return 0;
}

// Fills b from the image. report: what was not found, or found more than once (empty when all is well). True when
// the places the mod cannot do without are there: the two classes, the control global and the six checks of the
// game's objects. The game-thread path (the damage update, the truck update's call through the import slot and the
// way to its first argument) may be missing on a build of the game: its fields then stay zero, report names what was
// not found, and the mod deals tire damage from its own thread instead.
static bool BuildFindAll(const BuildImage &im, Build &b, std::wstring &report)
{
    b = Build{};
    report.clear();
    if (im.n < 0x1000 || im.u16(0) != 0x5A4D || im.header() + 0x200 > im.n) { report = L"not an exe image"; return false; }
    b.stamp = im.u32(im.header() + 8);
    b.image = im.u32(im.optional() + 56);
    auto one = [&](const wchar_t *what, const std::vector<uint32_t> &hits) -> uint32_t {
        if (hits.size() != 1) BuildMiss(report, what, hits.size());
        return hits.size() == 1 ? hits[0] : 0;
    };

    b.wheelVtable = BuildVtable(im, ".?AUTRUCK_WHEEL_PARAMS@combine@@", L"the wheel parameter class", report);
    b.cylinderVtable = BuildVtable(im, ".?AVhkpCylinderShape@@", L"the cylinder shape class", report);

    // The game's objects as the mod reads and writes them: each of these is game code that uses the same positions.
    // Found anywhere is enough: a second copy of the same instructions says the same about the positions.
    static const struct { const wchar_t *what; const char *pattern; } kLayout[] = {
        { L"the steering speed at vehicle+0x69C", "f3 0f 10 96 9c 06 00 00 f3 41 0f 5f d1" },
        { L"the fuel use at vehicle+0xE38 and +0xE3C", "f3 0f 10 83 3c 0e 00 00 f3 0f 58 83 38 0e 00 00 f3 0f 59 f0" },
        { L"the gear speeds at +0x58 by the gear at +0x74", "48 63 4b 74 44 0f 29 44 24 50" },
        { L"the ground grip list and the paved flag at wheel+0x1C8", "f3 42 0f 10 0c b0 80 bf c8 01 00 00 00 0f 84" },
        { L"the asphalt grip at wheel+0xA4", "f3 0f 59 8f a4 00 00 00 f3 0f 59 15" },
        { L"the wheel's radius and its offset at +0x94 and +0xB4", "f3 0f 10 9b 94 00 00 00 f3 0f 5c 9b b4 00 00 00" },
    };
    bool layout = true;
    for (const auto &check : kLayout)
        if (BuildFind(im, check.pattern, 0, 0xFFFFFFFFu, 1).empty()) { BuildMiss(report, check.what, 0); layout = false; }

    // the damage update, by its first instructions (they hold the vehicle's +0x150)
    b.damageUpdate = one(L"the damage update", BuildFind(im,
        "48 8b c4 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 83 ec 50 48 c7 40 98 fe ff ff ff 48 89 58 18 0f 29 70 b8 0f 29 78 a8 4c 8b e9 "
        "48 8d b9 50 01 00 00 48 8b cf e8"));

    // the truck update asks Windows which window is in front: the call through the import slot, in a function that
    // begins by putting its first argument into its home slot
    uint32_t begin = 0, end = 0; // the truck update's bounds, once known
    b.foregroundSlot = BuildImportSlot(im, "USER32.dll", "GetForegroundWindow");
    if (!b.foregroundSlot) BuildMiss(report, L"the import of GetForegroundWindow", 0);
    const uint32_t site = one(L"the truck update's call of GetForegroundWindow",
                              BuildFind(im, "40 b7 01 40 88 7c 24 50 44 0f b6 ff ff 15 ?? ?? ?? ?? 48 8b d8 e8 ?? ?? ?? ?? 48 3b c3 74"));
    if (site && b.foregroundSlot)
    {
        const uint32_t call = site + 12;
        uint32_t pushes = 0, locals = 0;
        static const uint8_t kHead[33] = { 0x48, 0x8b, 0xc4, 0xf3, 0x0f, 0x11, 0x58, 0x20, 0xf3, 0x0f, 0x11, 0x50, 0x18, 0x48, 0x89, 0x50, 0x10, 0x48, 0x89, 0x48, 0x08,
                                           0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
        if ((uint64_t)call + 6 + im.i32(call + 2) != b.foregroundSlot) BuildMiss(report, L"the truck update's call through the import slot", 0);
        else if (!BuildFunction(im, call, begin, end, pushes, locals) || begin + sizeof kHead > im.n || memcmp(im.p + begin, kHead, sizeof kHead))
        {
            BuildMiss(report, L"the truck update as the mod knows it", 0);
            begin = end = 0;
        }
        else
        {
            b.truckUpdateReturn = call + 6;
            // its locals, its pushes, its own return address, then the home slot of its first argument
            b.firstArgument = (uint64_t)locals + pushes * 8 + 16;
        }
    }
    // the game-thread path is all or nothing: without any part of it the mod deals the damage on its own thread
    if (!b.damageUpdate || !b.foregroundSlot || !b.truckUpdateReturn) b.damageUpdate = b.foregroundSlot = b.truckUpdateReturn = b.firstArgument = 0;

    // the control global, three ways (BuildControl), inside the truck update where that is known
    b.control = BuildControl(im, begin, end ? end : 0xFFFFFFFFu, report);

    return layout && b.control && b.wheelVtable && b.cylinderVtable;
}
