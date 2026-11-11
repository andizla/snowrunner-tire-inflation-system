// The ground probe's own reckoning (ini ProbeGround=1, BRIEF_ground_probe.md): what the game keeps of the ground under
// each wheel, logged while the truck moves. Read only: nothing here or in its reader writes to the game. This file
// reads no game memory, so probe_test checks it offline: which wheels get a line (GpPick) and the line's text (GpLine).
// tire_pressure.cpp does the reads in the truck update (GroundProbeTruckUpdate) and the line on the mod's thread
// (GroundProbePass).
#pragma once
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "rolling_drag.h"
#include "text_cut.h"

const int kGpWheelsMost = 16;
const uint32_t kGpPassMs = 100;  // the truck update reads at most this often: 10 a second
const uint32_t kGpLineMs = 1000; // a wheel with nothing moved still gets a line this often
const float kGpSpeedMin = 0.5f;  // m/s: nothing is read while the truck is slower

// The wheel's floats in the order of the line. step: how far one has to move from the wheel's last line for a new one;
// 0 = not compared. + 0x194 is FLT_MAX with no reading, and + 0x190 drifts on its own after water, so neither starts a
// line (review M1). + 0x178 and + 0x17C are metres of sink, where a centimetre is already much.
enum { kGpFloats = 10, kGpInts = 2, kGpBytes = 9 };
static const struct { uint16_t at; float step; } kGpFloat[kGpFloats] = {
    { 0x128, 0.01f }, { 0x178, 0.005f }, { 0x17C, 0.005f }, { 0x190, 0.0f }, { 0x194, 0.0f },
    { 0x1A8, 0.01f }, { 0x1AC, 0.01f },  { 0x1B0, 0.01f },  { 0x1BC, 0.01f }, { 0x1C4, 0.01f },
};
enum { kGpContactShare, kGpSinkA, kGpSinkB, kGp190, kGp194, kGp1A8, kGp1AC, kGp1B0, kGp1BC, kGpIceBlend };
static const uint16_t kGpIntAt[kGpInts] = { 0x1B8, 0x1D0 };
// the bytes: [0] + 0x1A4 (a ground body under the wheel), [1] + 0x1C0 (ice), [2] to [8] + 0x1C8 to + 0x1CE in one read
enum { kGpByteBody, kGpByteIce, kGpByteFlags };
// ok: a bit for each read that came back. Floats 0 to 9, ints 10 and 11, then the three byte reads.
enum : uint32_t { kGpOkBody = 1u << 12, kGpOkIce = 1u << 13, kGpOkFlags = 1u << 14 };

// One wheel as read in one pass. A read that failed leaves its value 0 and its ok bit clear.
struct GpWheel
{
    int index;   // its place in the vehicle's list of wheel models
    uint32_t ok;
    float f[kGpFloats];
    int32_t i[kGpInts];
    uint8_t b[kGpBytes];
};

// Whether a float moved by more than step. Two that are no number did not move; one that became one did.
inline bool GpFloatMoved(float now, float last, float step)
{
    const bool nanNow = now != now, nanLast = last != last;
    if (nanNow || nanLast) return nanNow != nanLast;
    return fabsf(now - last) > step;
}

// Whether a wheel moved since its last line: a compared float by more than its step, an int or a byte by any change,
// or a read that failed where it came back before (and the other way).
inline bool GpMoved(const GpWheel &now, const GpWheel &last)
{
    if (now.index != last.index || now.ok != last.ok) return true;
    for (int k = 0; k < kGpFloats; k++)
        if (kGpFloat[k].step > 0.0f && (now.ok & (1u << k)) && GpFloatMoved(now.f[k], last.f[k], kGpFloat[k].step)) return true;
    for (int k = 0; k < kGpInts; k++)
        if (now.i[k] != last.i[k]) return true;
    return memcmp(now.b, last.b, sizeof now.b) != 0;
}

// What the probe remembers between passes: each wheel as its last line had it, and when that was. One truck at a time:
// another vehicle or another count of wheels starts it anew.
struct GpState
{
    uint64_t vehicle;
    int count;
    uint64_t startAt; // the first pass of this vehicle, ms: the line's t counts from it
    bool has[kGpWheelsMost];
    uint64_t at[kGpWheelsMost];
    GpWheel last[kGpWheelsMost];
};

// Which wheels of a pass get written: one never written for this truck, one that moved since its last line, one whose
// last line is a second old. write: a flag per wheel. Returns how many. Those written become the wheels' last lines.
inline int GpPick(GpState &s, uint64_t vehicle, const GpWheel *now, int count, uint64_t at, bool *write)
{
    if (count > kGpWheelsMost) count = kGpWheelsMost;
    if (count < 0) count = 0;
    if (s.vehicle != vehicle || s.count != count)
    {
        s = GpState{};
        s.vehicle = vehicle;
        s.count = count;
        s.startAt = at;
    }
    int n = 0;
    for (int k = 0; k < count; k++)
    {
        write[k] = !s.has[k] || at - s.at[k] >= kGpLineMs || GpMoved(now[k], s.last[k]);
        if (!write[k]) continue;
        s.has[k] = true;
        s.at[k] = at;
        s.last[k] = now[k];
        n++;
    }
    return n;
}

// A float for the line: "?" for a failed read, "nan" and "inf" for what is no number (never left to the format),
// "none" for FLT_MAX where the game means no reading. sink: metres to four places, else four digits.
inline int GpNum(wchar_t *out, size_t size, bool ok, float v, bool sink = false, bool noneAtMost = false)
{
    if (!ok) return FmtCut(out, size, L"?");
    if (v != v) return FmtCut(out, size, L"nan");
    if (noneAtMost && v == FLT_MAX) return FmtCut(out, size, L"none");
    if (fabsf(v) > FLT_MAX) return FmtCut(out, size, v > 0.0f ? L"inf" : L"-inf");
    return FmtCut(out, size, sink ? L"%.4f" : L"%.4g", (double)v);
}

// The columns of a wheel, for the line at the start. + 0x1CB is not reset by the game's check, so it can be stale.
static const wchar_t *const kGpStart =
    L"ground probe: ProbeGround=1, read only: nothing is written to the game. At most 10 lines a second while the truck moves over 0.5 m/s; a wheel is in a "
    L"line when one of its figures moved since its last line (+178 and +17C by 0.005, the other floats by 0.01, +190 and +194 never, an int or a byte by any "
    L"change) and once a second. A line: the truck's m/s, t in seconds since this truck's first line, then each wheel as "
    L"index: ground Crr | +1A4 +128 | +178 +17C | +190 +194 | +1A8 +1AC +1B0 +1BC | +1C4 +1C0 | +1B8 +1D0 | +1C8 to +1CE. "
    L"The ground is rolling drag's pick and Crr its table's times the pressure's factor (what the drag would use where the line says drag off). "
    L"? is a failed read, none is FLT_MAX at +194. +1CB can be stale: the game's check does not reset it";

// One wheel's text: "index: ground Crr | +1A4 +128 | +178 +17C | +190 +194 | +1A8 +1AC +1B0 +1BC | +1C4 +1C0 | +1B8 +1D0 | flags".
// factor: the pressure's factor on Crr. Returns the characters written.
inline int GpWheelText(wchar_t *out, size_t size, const GpWheel &w, float factor)
{
    wchar_t f[kGpFloats][24], crr[24], ints[kGpInts][16], body[8], ice[8], flags[40];
    for (int k = 0; k < kGpFloats; k++) GpNum(f[k], 24, (w.ok & (1u << k)) != 0, w.f[k], k == kGpSinkA || k == kGpSinkB, k == kGp194);
    for (int k = 0; k < kGpInts; k++)
    {
        if (w.ok & (1u << (kGpFloats + k))) FmtCut(ints[k], 16, L"%d", w.i[k]);
        else FmtCut(ints[k], 16, L"?");
    }
    if (w.ok & kGpOkBody) FmtCut(body, 8, L"%u", (unsigned)w.b[kGpByteBody]);
    else FmtCut(body, 8, L"?");
    if (w.ok & kGpOkIce) FmtCut(ice, 8, L"%u", (unsigned)w.b[kGpByteIce]);
    else FmtCut(ice, 8, L"?");
    if (w.ok & kGpOkFlags)
    {
        int used = 0;
        for (int k = 0; k < 7; k++) used += FmtCut(flags + used, 40 - used, k ? L".%u" : L"%u", (unsigned)w.b[kGpByteFlags + k]);
    }
    else FmtCut(flags, 40, L"?");
    // the ground as rolling drag picks it: the six flags from + 0x1C8 and the ice byte
    const bool groundOk = (w.ok & kGpOkIce) && (w.ok & kGpOkFlags);
    const RdGround ground = groundOk ? RdGroundOf(w.b + kGpByteFlags, w.b[kGpByteIce]) : kRdDirt;
    GpNum(crr, 24, groundOk, kRdCrr[ground] * factor, true);
    return FmtCut(out, size, L"%d: %hs %s | %s %s | %s %s | %s %s | %s %s %s %s | %s %s | %s %s | %s", w.index, groundOk ? kRdGroundNames[ground] : "?", crr, body,
                  f[kGpContactShare], f[kGpSinkA], f[kGpSinkB], f[kGp190], f[kGp194], f[kGp1A8], f[kGp1AC], f[kGp1B0], f[kGp1BC], f[kGpIceBlend], ice, ints[0], ints[1],
                  flags);
}

// One pass's line: the wheels to write side by side, into a fixed buffer. seconds: since this truck's first pass.
// dragOn: whether rolling drag writes now. lost: passes the truck update read that no line took. A line too long for
// the buffer lands cut. Returns the characters written.
inline int GpLine(wchar_t *out, size_t size, float speed, float seconds, bool dragOn, float factor, const GpWheel *now, const bool *write, int count, int lost)
{
    wchar_t v[24];
    GpNum(v, 24, true, speed, true);
    int used = FmtCut(out, size, L"ground probe: %s m/s, t %.1f s%s", v, (double)seconds, dragOn ? L"" : L", drag off");
    if (lost > 0) used += FmtCut(out + used, size - used, L", %d passes not written", lost);
    for (int k = 0; k < count && k < kGpWheelsMost; k++)
    {
        if (!write[k]) continue;
        used += FmtCut(out + used, size - used, L" || ");
        used += GpWheelText(out + used, size - used, now[k], factor);
    }
    return used;
}
