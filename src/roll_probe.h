// The rolling drag probe's own reckoning (ini ProbeRoll=1, BRIEF_rolling_drag.md part 1): what it needs that reads
// no game memory, so probe_test checks it offline. tire_pressure.cpp (RollProbePass, RollFrameWrite) does the reads
// and the writes.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "text_cut.h"

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
// The two closest to each other are across it. -1 where one is no inverse (zero, negative or not a number), and where
// the largest and the smallest lie within 2 % (a sphere's inertia, as Havok setups often give a wheel: no axle shows).
inline int RollAxle(const float inv[3])
{
    float in[3];
    for (int i = 0; i < 3; i++)
    {
        if (!(inv[i] > 0.0f && inv[i] < 1.0e30f)) return -1;
        in[i] = 1.0f / inv[i];
    }
    const float most = fmaxf(in[0], fmaxf(in[1], in[2])), least = fminf(in[0], fminf(in[1], in[2]));
    if (most - least <= 0.02f * most) return -1;
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

// The coast-down a step counts (BRIEF_roll_probe_n_only.md): in N, with no pedal, no brake and no handbrake, from
// 7 m/s or more (25 km/h) down to 1 m/s. The ground does not refuse a coast: + 0x1CE (flag 1CE) is on over paved road
// too, 99 % of the passes of the 23:51 drive, so its meaning is not known; the line keeps each wheel's shares to read
// the ground by. Every run from 7 m/s or more down to
// 1 m/s gives one result, counted or not, with the reason when not; only a counted one moves the step on.
static const float kRollFrom = 7.0f;        // m/s a coast starts from (25.2 km/h)
static const float kRollTo = 1.0f;          // m/s it is measured to: the last crawl to the stand is the game's rest friction
static const float kRollNoPedal = 0.02f;    // |throttle| under this is no pedal and no brake (below 0 is the pedal)
static const float kRollG = 9.81f;
static const int kRollWheelsMost = 16;

// A wheel's ground as the game's ground check left it (wheel + 0x1C8 paved, + 0x1C9 gravel, + 0x1CA sand, + 0x1CE flag 1CE,
// meaning not known).
struct RollGround { uint8_t paved, gravel, sand, soil; };
// One pass as RollProbePass reads it. gear: the game's engaged gear (action + 0x70), 0 is N; throttle: action + 0x44.
struct RollPass
{
    float speed, height, dt;
    bool gearKnown;
    int gear;
    float throttle;
    bool handbrake;
    int wheels;
    RollGround ground[kRollWheelsMost];
    int onceHeld, onceWritten; // the once step's word still held, of the wheels it went into (0 of 0 in the other steps)
};

enum RollRule { kRuleOk, kRuleGearUnknown, kRuleGear, kRulePedal, kRuleBrake, kRuleHandbrake };
inline RollRule RollRuleOf(const RollPass &p)
{
    if (!p.gearKnown) return kRuleGearUnknown;
    if (p.gear != 0) return kRuleGear;
    if (p.throttle <= -kRollNoPedal) return kRulePedal;
    if (p.throttle >= kRollNoPedal) return kRuleBrake;
    if (p.handbrake) return kRuleHandbrake;
    return kRuleOk;
}
inline void RollRuleText(RollRule r, int gear, char *out, size_t size)
{
    switch (r)
    {
    case kRuleGearUnknown: strncpy_s(out, size, "the gear not read", _TRUNCATE); break;
    case kRuleGear:
        if (gear < 0) strncpy_s(out, size, "in reverse", _TRUNCATE);
        else _snprintf_s(out, size, _TRUNCATE, "in gear %d", gear);
        break;
    case kRulePedal: strncpy_s(out, size, "the pedal", _TRUNCATE); break;
    case kRuleBrake: strncpy_s(out, size, "the brake", _TRUNCATE); break;
    case kRuleHandbrake: strncpy_s(out, size, "the handbrake", _TRUNCATE); break;
    default: out[0] = 0;
    }
}

// What one coast came to. numbers: it ran in N from 7 m/s or more for at least one pass, so v0 .. flat hold.
struct RollCoastResult
{
    int step;           // the step it ran in; the steps' count or more after the run
    bool numbers, counted;
    char reason[64];    // why it did not count, "" when it did
    float v0, v1, t, d, h0, h1;
    float decel, slope, flat; // (v0 - v1) / t; g (h0 - h1) / d; (v0² - v1²) / (2d) + g (h0 - h1) / d
    float soil;               // the share of the coast with flag 1CE on under any wheel
    int wheels;
    float share[4][kRollWheelsMost]; // each wheel's share with paved, gravel, sand and flag 1CE on
    bool once;
    int onceHeld, onceWritten;       // at the coast's end, in the once step
};

// The run of the coasts. phase: 0 waiting for 7 m/s, 1 from 7 m/s with no coast yet, 2 coasting.
struct RollRun
{
    int step = 0;
    int phase = 0;
    char why[64] = "";  // phase 1: why no coast began (the rule last broken at 1 m/s or more, or the start under 7)
    float on[4][kRollWheelsMost] = {}, any = 0.0f;
    float lastSpeed = 0.0f, lastHeight = 0.0f; // the coast's last pass with every rule held (review N1, N2)
    RollCoastResult c = {};
};

inline void RollCoastBegin(RollRun &r, const RollPass &p)
{
    r.phase = 2;
    r.c = RollCoastResult{};
    r.c.step = r.step;
    r.c.numbers = true;
    r.c.v0 = r.c.v1 = p.speed;
    r.c.h0 = r.c.h1 = p.height;
    r.c.wheels = p.wheels < kRollWheelsMost ? p.wheels : kRollWheelsMost;
    memset(r.on, 0, sizeof r.on);
    r.any = 0.0f;
    r.lastSpeed = p.speed;
    r.lastHeight = p.height;
}

// The coast's numbers from what it gathered.
inline void RollCoastEnd(RollRun &r, const RollPass &p)
{
    RollCoastResult &c = r.c;
    c.v1 = p.speed;
    c.h1 = p.height;
    c.decel = c.t > 0.0f ? (c.v0 - c.v1) / c.t : 0.0f;
    c.slope = c.d > 0.0f ? kRollG * (c.h0 - c.h1) / c.d : 0.0f;
    c.flat = c.d > 0.0f ? (c.v0 * c.v0 - c.v1 * c.v1) / (2.0f * c.d) + c.slope : 0.0f;
    for (int k = 0; k < 4; k++)
        for (int w = 0; w < c.wheels; w++) c.share[k][w] = c.t > 0.0f ? r.on[k][w] / c.t : 0.0f;
    c.soil = c.t > 0.0f ? r.any / c.t : 0.0f;
    c.once = p.onceWritten > 0;
    c.onceHeld = p.onceHeld;
    c.onceWritten = p.onceWritten;
}

// One pass: true when a coast's result is ready (in out), whether it counted or not. It is ready as the coast ends: at
// the first pass under 1 m/s, or at a broken rule. A counted one in a step moves the run to its next step at once, with
// no stand (the lead's pick on review F1), so the next damping is written from then on. A run from 7 m/s with no coast
// gives its line once it is under 1 m/s. After the last step coasts are still measured (their step is the count), and
// move nothing.
inline bool RollRunPass(RollRun &r, int steps, const RollPass &p, RollCoastResult &out)
{
    const RollRule rule = RollRuleOf(p);
    if (r.phase == 0 && p.speed >= kRollFrom)
    {
        r.phase = 1;
        strncpy_s(r.why, "", _TRUNCATE);
    }
    if (r.phase == 1)
    {
        if (rule == kRuleOk && p.speed >= kRollFrom)
        {
            RollCoastBegin(r, p); // its time and distance start from this pass
            return false;
        }
        else if (p.speed >= kRollTo)
        {
            if (rule != kRuleOk) RollRuleText(rule, p.gear, r.why, sizeof r.why);
            else strncpy_s(r.why, "from under 25 km/h", _TRUNCATE);
        }
        else
        {
            out = RollCoastResult{};
            out.step = r.step;
            strncpy_s(out.reason, r.why[0] ? r.why : "from under 25 km/h", _TRUNCATE);
            r.phase = 0;
            return true;
        }
    }
    if (r.phase != 2) return false;
    if (rule != kRuleOk)
    {
        // the coast ends where it last held: the pass that broke a rule is not part of it
        RollRuleText(rule, p.gear, r.c.reason, sizeof r.c.reason);
        RollPass last = p;
        last.speed = r.lastSpeed;
        last.height = r.lastHeight;
        RollCoastEnd(r, last);
    }
    else
    {
        r.c.t += p.dt;
        r.c.d += 0.5f * (r.lastSpeed + p.speed) * p.dt; // the trapezoid between the last pass and this one
        r.lastSpeed = p.speed;
        r.lastHeight = p.height;
        bool any = false;
        for (int w = 0; w < r.c.wheels; w++)
        {
            const RollGround &g = p.ground[w];
            const uint8_t flags[4] = { g.paved, g.gravel, g.sand, g.soil };
            for (int k = 0; k < 4; k++)
                if (flags[k]) r.on[k][w] += p.dt;
            any = any || g.soil;
        }
        if (any) r.any += p.dt;
        if (p.speed >= kRollTo) return false;
        RollCoastEnd(r, p);
    }
    RollCoastResult &c = r.c;
    c.counted = c.reason[0] == 0 && c.step < steps;
    out = c;
    r.phase = 0;
    if (c.counted) r.step++;
    return true;
}

// The step's name: "step 3" or "after" (the coasts after the run).
inline void RollStepName(int step, int steps, char *out, size_t size)
{
    if (step >= steps) strncpy_s(out, size, "after", _TRUNCATE);
    else _snprintf_s(out, size, _TRUNCATE, "step %d", step + 1);
}

// The coast's line for the log, after "probe roll: ".
inline void RollCoastText(const RollCoastResult &c, int steps, char *out, size_t size)
{
    char name[16];
    RollStepName(c.step, steps, name, sizeof name);
    if (!c.numbers)
    {
        _snprintf_s(out, size, _TRUNCATE, "%s, the coast did not count: %s", name, c.reason);
        return;
    }
    int n = FmtCut(out, size, "%s, coast in N %.2f to %.2f m/s in %.2f s over %.1f m, height %+.2f m, decel by time %.3f m/s2, slope part %+.3f m/s2, flat decel by distance %.3f m/s2; "
                      "flag 1CE under a wheel %.2f of it; by wheel paved/gravel/sand/1CE", name, c.v0, c.v1, c.t, c.d, c.h1 - c.h0, c.decel, c.slope, c.flat, c.soil);
    for (int w = 0; w < c.wheels && n > 0 && n + 40 < (int)size; w++)
        n += FmtCut(out + n, size - n, " w%d %.2f/%.2f/%.2f/%.2f", w, c.share[0][w], c.share[1][w], c.share[2][w], c.share[3][w]);
    if (c.once && n > 0 && n + 60 < (int)size) n += FmtCut(out + n, size - n, "; the once word held in %d of %d wheels at its end", c.onceHeld, c.onceWritten);
    if (n > 0 && n + 2 < (int)size)
    {
        if (c.counted) _snprintf_s(out + n, size - n, _TRUNCATE, "; counted");
        else if (c.step >= steps) _snprintf_s(out + n, size - n, _TRUNCATE, "; after the run%s%s", c.reason[0] ? ", " : "", c.reason);
        else _snprintf_s(out + n, size - n, _TRUNCATE, "; not counted: %s", c.reason);
    }
}

// ---- the probe box (drawn by panel.cpp while ProbeRoll=1) ----
enum RollBoxColor { kBoxWhite, kBoxGrey, kBoxGreen, kBoxRed };
struct RollBoxSeg { char text[40]; uint8_t color; };
struct RollBoxLine { char text[120]; uint8_t color; int segs; RollBoxSeg seg[4]; };
static const int kRollBoxLines = 14, kRollBoxCoasts = 6;
struct RollBox { int lines; RollBoxLine line[kRollBoxLines]; };

// What the box shows: the step, this pass's rules and ground, the last coasts (newest first) and the counted ones'
// flat decel by step, for "x1.08 of stock".
struct RollBoxState
{
    int step, steps;
    RollStepDef def;
    bool coasting;          // a coast runs now: "from 25 km/h" says whether it began at 7 m/s or more
    float coastFrom;        // its v0
    RollPass now;
    int coasts;             // results kept, at most kRollBoxCoasts, newest at [0]
    RollCoastResult last[kRollBoxCoasts];
    bool stockCounted;
    float stockFlat;        // the stock step's counted flat decel
};

inline void RollBoxAdd(RollBoxState &s, const RollCoastResult &c)
{
    for (int i = kRollBoxCoasts - 1; i > 0; i--) s.last[i] = s.last[i - 1];
    s.last[0] = c;
    if (s.coasts < kRollBoxCoasts) s.coasts++;
    if (c.counted && c.step == 0)
    {
        s.stockCounted = true;
        s.stockFlat = c.flat;
    }
}

inline RollBoxLine &RollBoxNext(RollBox &b, uint8_t color)
{
    RollBoxLine &l = b.line[b.lines < kRollBoxLines ? b.lines++ : kRollBoxLines - 1];
    l = RollBoxLine{};
    l.color = color;
    return l;
}

inline void RollBoxText(const RollBoxState &s, RollBox &b)
{
    b.lines = 0;
    RollBoxLine &title = RollBoxNext(b, kBoxWhite);
    if (s.step >= s.steps) strncpy_s(title.text, "Done: the log has the results. ProbeRoll=0 to stop.", _TRUNCATE);
    else if (s.def.how == kRollStock) _snprintf_s(title.text, _TRUNCATE, "Step %d of %d: the stock damping", s.step + 1, s.steps);
    else _snprintf_s(title.text, _TRUNCATE, "Step %d of %d: damping %g, %s", s.step + 1, s.steps, s.def.damping, s.def.how == kRollOnce ? "once" : "every frame");
    // the rules as the coast checks them
    RollBoxLine &rules = RollBoxNext(b, kBoxWhite);
    const RollRule rule = RollRuleOf(s.now);
    const bool from = s.coasting ? s.coastFrom >= kRollFrom : s.now.speed >= kRollFrom;
    const struct { const char *text; bool ok; } r[4] = { { "N", rule != kRuleGear && rule != kRuleGearUnknown },
                                                         { "no pedal", s.now.throttle > -kRollNoPedal },
                                                         { "no brake", s.now.throttle < kRollNoPedal && !s.now.handbrake },
                                                         { "from 25 km/h", from } };
    rules.segs = 4;
    for (int i = 0; i < 4; i++)
    {
        strncpy_s(rules.seg[i].text, r[i].text, _TRUNCATE);
        rules.seg[i].color = (uint8_t)(r[i].ok ? kBoxGreen : kBoxRed);
    }
    // the ground under the first wheel, flag 1CE in white (it keeps no coast from counting)
    RollBoxLine &ground = RollBoxNext(b, kBoxWhite);
    const RollGround g = s.now.wheels > 0 ? s.now.ground[0] : RollGround{};
    ground.segs = 2;
    strncpy_s(ground.seg[0].text, "Ground:", _TRUNCATE);
    ground.seg[0].color = kBoxWhite;
    _snprintf_s(ground.seg[1].text, _TRUNCATE, "flag 1CE %u", g.soil ? 1u : 0u);
    ground.seg[1].color = kBoxWhite;
    if (g.paved || g.gravel || g.sand)
    {
        ground.segs = 3;
        _snprintf_s(ground.seg[2].text, _TRUNCATE, "%s%s%s", g.paved ? "paved " : "", g.gravel ? "gravel " : "", g.sand ? "sand " : "");
        ground.seg[2].text[strlen(ground.seg[2].text) - 1] = 0;
        ground.seg[2].color = kBoxWhite;
    }
    RollBoxLine &speed = RollBoxNext(b, kBoxWhite);
    _snprintf_s(speed.text, _TRUNCATE, "Speed %.0f km/h", s.now.speed * 3.6f);
    RollBoxLine &head = RollBoxNext(b, kBoxGrey);
    strncpy_s(head.text, s.coasts ? "Coasts, the newest first:" : "No coast yet: shift to N from 25 km/h, no pedal, no brake", _TRUNCATE);
    for (int i = 0; i < s.coasts; i++)
    {
        const RollCoastResult &c = s.last[i];
        char name[16];
        RollStepName(c.step, s.steps, name, sizeof name);
        name[0] = (char)(name[0] - 'a' + 'A');
        RollBoxLine &l = RollBoxNext(b, (uint8_t)(c.counted ? kBoxGreen : kBoxGrey));
        if (!c.numbers) _snprintf_s(l.text, _TRUNCATE, "%s: not counted: %s", name, c.reason);
        else
        {
            char end[64];
            if (c.counted && c.step == 0) strncpy_s(end, "counted (stock)", _TRUNCATE);
            else if (c.counted && s.stockCounted && s.stockFlat > 0.0f) _snprintf_s(end, _TRUNCATE, "counted, x%.2f of stock", c.flat / s.stockFlat);
            else if (c.counted) strncpy_s(end, "counted, no stock coast yet", _TRUNCATE);
            else if (c.step >= s.steps) strncpy_s(end, c.reason[0] ? c.reason : "after the run", _TRUNCATE);
            else _snprintf_s(end, _TRUNCATE, "not counted: %s", c.reason);
            _snprintf_s(l.text, _TRUNCATE, "%s: %.0f to %.0f km/h, flat %.2f m/s2, flag 1CE %.2f, %s", name, c.v0 * 3.6f, c.v1 * 3.6f, c.flat, c.soil, end);
        }
    }
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

// The weight search behind flag 1CE (+0x1CE): a float of the wheel that could be a blend weight lies in 0..1 (a NaN does not).
inline bool RollUnit(float v) { return v >= 0.0f && v <= 1.0f; }

// At a flip of flag 1CE (+0x1CE): the floats that are candidates for the weight, between the pass before and now. A
// candidate crossed 0.5 (in 0..1 both times, one under 0.5 and one at or over it) or moved by more than 0.05 within
// 0..1. Zeros, denormals and other still values are left out, wherever they lie. Their indexes into out, the count.
inline int RollCandidates(const float *before, const float *now, int count, int *out)
{
    int n = 0;
    for (int i = 0; i < count; i++)
    {
        if (!RollUnit(before[i]) || !RollUnit(now[i])) continue;
        const bool crossed = (before[i] < 0.5f) != (now[i] < 0.5f);
        if (crossed || fabsf(now[i] - before[i]) > 0.05f) out[n++] = i;
    }
    return n;
}
