// The rolling drag probe's own reckoning (ini ProbeRoll=1, BRIEF_rolling_drag.md part 1): what it needs that reads
// no game memory, so probe_test checks it offline. tire_pressure.cpp (RollProbePass, RollFrameWrite) does the reads
// and the writes.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

// A Havok half float back to a float: its 16 bits are a float's upper half. The mod writes them through HavokHalf,
// which rounds up by 1.003906 first, so a value read back lies at most 0.4 % over the one meant.
inline float HalfToFloat(uint16_t half)
{
    const uint32_t bits = (uint32_t)half << 16;
    float f = 0.0f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

// The axle among a body's three axes, from the inverse inertia Havok keeps for each (body+0x220): the axis whose
// inertia stands apart from the other two, as a wheel is a cylinder (½mr² about its axle, the two across it alike).
// The two closest to each other are across it. -1 where one is no inverse (zero, negative or not a number).
inline int RollAxle(const float inv[3])
{
    float in[3];
    for (int i = 0; i < 3; i++)
    {
        if (!(inv[i] > 0.0f && inv[i] < 1.0e30f)) return -1;
        in[i] = 1.0f / inv[i];
    }
    const float d01 = fabsf(in[0] - in[1]), d02 = fabsf(in[0] - in[2]), d12 = fabsf(in[1] - in[2]);
    return d01 <= d02 && d01 <= d12 ? 2 : d02 <= d12 ? 1 : 0;
}

// The coast-down's steps: 0 the stock damping, 1 the first value written once (the probe counts each second whether
// it still holds, so a game that puts it back shows), then each value written every frame by the truck update.
enum RollHow { kRollStock, kRollOnce, kRollEveryFrame };
struct RollStepDef { float damping; RollHow how; };
static const int kRollMostValues = 8;
inline int RollStepCount(int values) { return values > 0 ? values + 2 : 1; }
inline RollStepDef RollStepAt(const float *values, int count, int step)
{
    if (count <= 0 || step <= 0 || step >= RollStepCount(count)) return { 0.0f, kRollStock };
    if (step == 1) return { values[0], kRollOnce };
    return { values[step - 2], kRollEveryFrame };
}

// One pass of the run: true when it moves on to its next step. It does once the truck has gone faster than 5 m/s in
// this step and has stood (under 0.2 m/s) for 3 s since: one coast-down to a stop each. After the last step it stays.
struct RollRun { int step = 0; float stillFor = 0.0f, topSpeed = 0.0f; };
inline bool RollRunPass(RollRun &r, int steps, float speed, float dt)
{
    if (r.step >= steps) return false;
    if (speed > r.topSpeed) r.topSpeed = speed;
    r.stillFor = speed < 0.2f ? r.stillFor + dt : 0.0f;
    if (r.topSpeed <= 5.0f || r.stillFor < 3.0f) return false;
    r.step++;
    r.stillFor = r.topSpeed = 0.0f;
    return true;
}

// ini ProbeRollDamping: the dampings to coast at, "0.5,2,8" (each over 0 and up to 100, at most 8). The count read;
// 0 where the text holds none, and the reading stops at the first that is not one.
inline int RollParseValues(const wchar_t *text, float (&out)[kRollMostValues])
{
    int n = 0;
    const wchar_t *p = text;
    while (p && *p && n < kRollMostValues)
    {
        while (*p == L' ' || *p == L',') p++;
        if (!*p) break;
        wchar_t *end = nullptr;
        const float v = wcstof(p, &end);
        if (end == p || !(v > 0.0f && v <= 100.0f)) break;
        out[n++] = v;
        p = end;
    }
    return n;
}

// The mud layer's weight search: a float of the wheel that could be a blend weight lies in 0..1 (a NaN does not).
inline bool RollUnit(float v) { return v >= 0.0f && v <= 1.0f; }
