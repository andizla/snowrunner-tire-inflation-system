// mud_tune.h: the mud of the game on three factors (BRIEF_mud_setting.md, part A), and what of it reads no game
// memory, so probe_test checks it offline. tire_pressure.cpp does the reads and the writes (MudPass, in the truck
// update on the game's thread).
//
// The game has one mud volume with four floats of its own: + 0x28 SubstanceFriction, + 0x2C the drag (LinResistance),
// + 0x30 the depth at which the lift is full (FullPushDepth), + 0x34 the lift (PushResistance). Four globals hold the
// game's figures and the game copies them into the volume at each map load. The mod writes the volume's drag, depth and
// lift as global x a factor. It reads the globals every time and never writes them, so nothing is a multiple of a
// multiple. The friction is read for the check and never written. Water has a volume and globals of its own, which the
// mod never touches.
//
// The places come from one function of the game, found by what its code looks like: it calls a function that returns
// the address of a holder, loads the four globals and stores them at volume + 0x28. [holder] is 0 with no map, else
// [[holder] + 0x18] is the volume, whose type at + 0x3C is 0x68.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include "build_find.h"

// The volume's floats in its own order, from + 0x28.
enum { kMudFriction, kMudDrag, kMudDepth, kMudLift, kMudFloats };
static const uint32_t kMudFloatsAt = 0x28, kMudTypeAt = 0x3C, kMudType = 0x68, kMudVolumeAt = 0x18;
static const float kMudDepthLeast = 0.05f; // m: the full depth is a divisor in the game, so what is written is held at least here

// The settings: a factor of the game's own figure each, in the order the tuning tool shows them. The texts are the
// player's: no apostrophe in them.
enum { kMudtLift, kMudtDrag, kMudtDepth, kMudTuneCount };
static const int kMudTuneFloat[kMudTuneCount] = { kMudLift, kMudDrag, kMudDepth };
struct MudTuneInfo { const char *key, *name, *tip; float least, most, def, step; int decimals; };
static const MudTuneInfo kMudTune[kMudTuneCount] = {
    { "MudLift", "Mud lift", "How hard mud holds wheels and bodies up. More means less sinking, up to the cap of the game.", 0.25f, 5.0f, 1.0f, 0.05f, 2 },
    { "MudDrag", "Mud drag", "How hard mud slows what moves through it, mostly at walking speed. Past 2.5 m/s the game caps it.", 0.25f, 4.0f, 1.0f, 0.05f, 2 },
    { "MudDepth", "Mud full depth", "The depth at which the lift is full. Smaller means firm sooner.", 0.25f, 2.0f, 1.0f, 0.05f, 2 },
};
static const char kMudGroup[] = "Mud";
static const char kMudSwitchName[] = "Mud tuning";
static const char kMudSwitchTip[] = "Scales the mud of the game by the three factors below. Off leaves the mud as the game has it.";
static const char kMudWhyOff[] = "Mud tuning is off.";
enum { kMudLineState, kMudLineLift, kMudLineDrag, kMudLineDepth, kMudLineSink, kMudLineCount };
struct MudLineInfo { const char *name, *tip; };
static const MudLineInfo kMudLines[kMudLineCount] = {
    { "Mud state", "What the mod does with the mud now." },
    { "Lift in force", "The lift of the mud now, beside the figure of the game." },
    { "Drag in force", "The drag of the mud now, beside the figure of the game." },
    { "Full depth in force", "The depth at which the lift is full now, beside the figure of the game." },
    { "Deepest wheel in mud", "How deep the deepest wheel of the driven truck sits." },
};

// ---- the mud grip by the weight a wheel carries (BRIEF_drive_1114_findings.md, part 181, part 1; the Reviewer's
// addendum of 2026-11-14 11:00; review\research_grip_by_weight.md) ----
// The game's mud traction is a force of a fixed size a wheel, with no load in it, so a heavy truck has less of it for
// each kilogram than a light one. The figure of the tuning tool, 0 to 100, gives a wheel's mud factor one more part:
// 1 + (clamp(M / 700, 1, 3) - 1) x the share, M the kilograms a wheel carries as the game holds them (wheel + 0x1B4,
// read only, never written). 1 where M is no number or not above 0, and exactly 1 at a share of 0. It is a part of the
// one factor the mod writes, never a write of its own.
static const char kMudWeightKey[] = "MudGripByWeight", kMudWeightName[] = "Mud grip by weight",
                  kMudWeightTip[] = "More mud grip for a truck that is heavy for its wheels. 0 is today. 100 gives up to three times at 2100 kg a wheel.";
static const float kMudWeightPer = 700.0f;                    // kg a wheel: at or under this the part is 1
static const float kMudWeightMost = 3.0f;                     // the part at 100, at 2100 kg a wheel and over
static const float kMudWeightSane[2] = { 50.0f, 20000.0f };   // kg a wheel: outside it the figure read is no wheel's load
static const float kMudWeightSame = 0.005f;                   // a truck's wheels read the same figure to within this share
inline int MudWeightHeld(float v) { return !isfinite(v) || v <= 0.0f ? 0 : v >= 100.0f ? 100 : (int)floorf(v + 0.5f); }
inline float MudWeightFactor(float m, float share)
{
    if (!isfinite(m) || !(m > 0.0f) || !isfinite(share) || !(share > 0.0f)) return 1.0f;
    const float ratio = m / kMudWeightPer, held = ratio < 1.0f ? 1.0f : ratio > kMudWeightMost ? kMudWeightMost : ratio;
    return 1.0f + (held - 1.0f) * (share > 1.0f ? 1.0f : share);
}
// The part for a truck from its wheels' figures as read (n of them, read false where one was not read): each inside
// the sane range and all the same, else 1 with the reason. mass: the figure it went by, 0 none.
enum { kMwOk, kMwOff, kMwNoRead, kMwRange, kMwDiffer };
inline float MudWeightOfWheels(const float *m, int n, bool read, int percent, int *why = nullptr, float *mass = nullptr)
{
    if (mass) *mass = 0.0f;
    auto none = [&](int w) {
        if (why) *why = w;
        return 1.0f;
    };
    if (percent <= 0) return none(kMwOff);
    if (!read || n <= 0 || !m) return none(kMwNoRead);
    for (int i = 0; i < n; i++)
        if (!isfinite(m[i]) || m[i] < kMudWeightSane[0] || m[i] > kMudWeightSane[1]) return none(kMwRange);
    for (int i = 1; i < n; i++)
        if (fabsf(m[i] - m[0]) > kMudWeightSame * m[0]) return none(kMwDiffer);
    if (why) *why = kMwOk;
    if (mass) *mass = m[0];
    return MudWeightFactor(m[0], (percent > 100 ? 100 : percent) / 100.0f);
}
// The part into the mode's mud factor: at a part of exactly 1 the factor is not touched, so at 0 it is today's to the last bit.
inline float MudWeightInto(float factor, float part) { return part == 1.0f ? factor : factor * part; }

// A factor held: no number is 1, then the range, then rounded to the two places it is shown and saved with.
inline float MudHeld(int i, float v)
{
    const MudTuneInfo &t = kMudTune[i];
    if (!isfinite(v)) return t.def;
    v = (float)(floor((double)v * 100.0 + 0.5) / 100.0);
    return v < t.least ? t.least : v > t.most ? t.most : v;
}

// ---- the places ----
// The function whole, from its first byte to its store at volume + 0x28: the call, the four loads (friction, drag, full
// depth, lift, by the shuffles that follow them to the one store) and the walk from the holder. RVAs.
struct MudPlaces { uint64_t function, holder, global[kMudFloats]; };
static const char kMudPattern[] = "48 83 EC 38 E8 ?? ?? ?? ?? F3 0F 10 1D ?? ?? ?? ?? BA 30 00 00 00 F3 0F 10 05 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? 48 8B 08 "
                                  "F3 0F 10 15 ?? ?? ?? ?? 48 85 C9 0F C6 DB E1 F3 0F 10 D8 0F C6 DB C6 48 8D 41 18 F3 0F 10 D9 48 0F 44 C2 0F C6 DB 27 "
                                  "F3 0F 10 DA 0F C6 DB 39 48 8B 08 0F 11 59 28";
// All or nothing: the pattern once, the call to a function that is "lea rax, [holder]; ret", and the holder and the
// four globals inside the image, each global another address. Else false with why, and out is not to be used.
inline bool MudPlacesFind(const BuildImage &im, MudPlaces &out, std::wstring &why)
{
    out = MudPlaces{};
    const std::vector<uint32_t> hits = BuildFind(im, kMudPattern);
    if (hits.size() != 1)
    {
        why = hits.empty() ? L"the copy the game makes of the mud figures was not found in this build" : L"the copy the game makes of the mud figures was found more than once in this build";
        return false;
    }
    const uint64_t at = hits[0];
    const uint64_t getter = at + 9 + (int64_t)im.i32((size_t)at + 5);
    static const uint8_t lea[3] = { 0x48, 0x8D, 0x05 };
    if (getter + 8 > im.n || memcmp(im.p + getter, lea, 3) || im.p[getter + 7] != 0xC3)
    {
        why = L"the function that copy calls is not the one that gives the holder, as the mod knows it";
        return false;
    }
    out.function = at;
    out.holder = getter + 7 + (int64_t)im.i32((size_t)getter + 3);
    static const uint32_t loads[kMudFloats] = { 0x09, 0x16, 0x1E, 0x29 }; // each "F3 0F 10 xx disp32": friction, drag, full depth, lift
    for (int k = 0; k < kMudFloats; k++) out.global[k] = at + loads[k] + 8 + (int64_t)im.i32((size_t)at + loads[k] + 4);
    bool ok = out.holder + 8 <= im.n;
    for (int k = 0; k < kMudFloats; k++)
    {
        ok = ok && out.global[k] + 4 <= im.n && !(out.global[k] & 3);
        for (int j = 0; j < k; j++) ok = ok && out.global[j] != out.global[k];
    }
    if (!ok) why = L"the holder or one of the four mud figures lies outside the image of the game, or two are the same";
    return ok;
}

// ---- the judging of one pass ----
// want: the switch and the three factors in force (the mod's thread sets them; the game's thread reads them).
struct MudWant { bool on; float factor[kMudTuneCount]; };
// What a pass found. kMudIdle: the switch off and nothing of the mod's in the volume, so nothing was read.
// kMudNoMap: the holder or its volume is 0, or could not be read: not there yet, look again. kMudNotMud: a volume of
// another type. kMudNotStock: its figures are not the game's before the mod's first write. kMudForeign: a figure is
// neither the mod's nor the game's, so somebody else writes the volume. The last three refuse that volume: nothing more
// is written to it, and a new volume pointer is judged anew.
enum MudState { kMudIdle, kMudNoMap, kMudStock, kMudWrites, kMudBack, kMudNotMud, kMudNotStock, kMudForeign };
static const char *const kMudStateNames[] = { "off", "no map yet", "on, at the game figures", "in force", "put back", "refused, see the log", "refused, see the log", "refused, see the log" };
// The mod's record of the volume it works on: the game's thread's only.
struct MudRecord { uint64_t volume; bool refused, active, have[kMudFloats]; uint32_t wrote[kMudFloats]; MudState state; };
struct MudPlan { MudState state; bool write[kMudFloats]; float value[kMudFloats]; };

inline uint32_t MudBits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
inline float MudFloat(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }

// volume and global: the four floats as bits, read in this pass. A figure in the volume has to be the game's (its
// global) or the one the mod wrote last, compared as bits. Then each of the three is global x factor, or the global
// with the switch off. The full depth is held at least kMudDepthLeast, and a product that is no number or not above 0
// is not written. A figure already there is not written, so with every factor at 1 nothing is.
inline MudPlan MudJudge(const MudRecord &rec, const uint32_t volume[kMudFloats], const uint32_t global[kMudFloats], const MudWant &want)
{
    MudPlan p = {};
    bool wroteAny = false, same = true;
    for (int k = 0; k < kMudFloats; k++) wroteAny = wroteAny || rec.have[k];
    for (int k = 0; k < kMudFloats; k++) same = same && (volume[k] == global[k] || (k != kMudFriction && rec.have[k] && volume[k] == rec.wrote[k]));
    if (!same)
    {
        p.state = wroteAny ? kMudForeign : kMudNotStock;
        return p;
    }
    bool any = false;
    for (int t = 0; t < kMudTuneCount; t++)
    {
        const int k = kMudTuneFloat[t];
        const float stock = MudFloat(global[k]);
        float v = stock;
        if (want.on)
        {
            v = stock * want.factor[t];
            if (!isfinite(v) || !(v > 0.0f)) continue; // nothing written for this one
            if (k == kMudDepth && v < kMudDepthLeast) v = kMudDepthLeast;
        }
        p.value[k] = v;
        p.write[k] = MudBits(v) != volume[k];
        any = any || MudBits(v) != global[k];
    }
    p.state = !want.on ? kMudBack : any ? kMudWrites : kMudStock;
    return p;
}
