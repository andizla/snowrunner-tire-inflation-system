// Rolling drag (ini RollingDrag, BRIEF_rolling_drag.md part 2): what it reckons that reads no game memory, so
// probe_test checks it offline. tire_pressure.cpp (RollingDragFrame, in the truck update) does the reads and writes.
//
// The game drags a wheel through its Havok body's angular damping (body + 0x216, a half float): the wheel's spin falls
// by c·ω, a torque of I·c·ω, which the tire's grip passes to the truck. Part 1's coasts showed it acts on the truck as
// I·c·ω. A rolling resistance is a force Crr·N, a torque Crr·N·r. So each grounded, unbroken wheel gets
//
//   c = c_stock + Crr · factor · N · r / (I · max(|ω|, 2 m/s / r))
//
// with ω the body's own spin along its axle (not the truck's speed over r, so a spinning wheel gets no more torque
// than a rolling one), I the body's inertia about the axle, r the radius the wheel rolls on (its collision cylinder's,
// RdRollRadius, which this mod makes smaller with the pressure) and c_stock the game's own figure for
// the wheel (wheel + 0xAC, never the body's word read back). Above the floor the torque is Crr·N·r; below it the torque
// falls with the spin to nothing standing, so the damping never pushes a wheel. N is the wheel's share of the weight:
// the truck's Havok mass (the chassis's and every wheel's, the mass the physics moves) times g over the wheels on the
// ground. Cargo and a trailer are not in it.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>

// Crr by the ground the game's check left in the wheel (+0x1C8 paved, +0x1C9 gravel, +0x1CA sand, +0x1CD hard, that is
// rock) and its ice flag (+0x1C0). In this order where several are set: ice, paved, sand, gravel, rock, and dirt for
// the rest, which takes a wheel with no flag set too (mud sets none, so mud gets dirt's: the mod cannot tell them
// apart yet). Ice is 0, as the game's own stays. The numbers are this mod's, from published truck tire figures
// (0.006 to 0.01 on asphalt at the rated pressure).
enum RdGround { kRdIce, kRdPaved, kRdSand, kRdGravel, kRdRock, kRdDirt, kRdGroundCount };
static const float kRdCrr[kRdGroundCount] = { 0.0f, 0.010f, 0.060f, 0.020f, 0.015f, 0.020f };
static const char *const kRdGroundNames[kRdGroundCount] = { "ice", "paved", "sand", "gravel", "rock", "dirt" };
static const float kRdFloor = 2.0f;    // m/s: the spin is held at least at this ground speed's worth
static const float kRdMost = 20.0f;    // the most damping written: part 1 measured up to 8, and c·dt passes 1 at 60 a second
static const float kRdChange = 0.02f;  // a new c is written when it moves by more than this share of the last one
static const float kRdG = 9.81f;

// flags: wheel + 0x1C8 .. + 0x1CD; ice: wheel + 0x1C0
inline RdGround RdGroundOf(const uint8_t flags[6], uint8_t ice)
{
    if (ice) return kRdIce;
    if (flags[0]) return kRdPaved;
    if (flags[2]) return kRdSand;
    if (flags[1]) return kRdGravel;
    if (flags[5]) return kRdRock;
    return kRdDirt;
}

// The pressure's factor on Crr, (p / p_Normal)^-0.5, from the mod's pressure share as it moves (Low 0.45 gives x1.49,
// Reduced 0.65 x1.24, Normal x1, Increased 1.25 x0.89).
inline float RdPressureFactor(float pressure) { return pressure > 0.05f ? 1.0f / sqrtf(pressure) : 1.0f / sqrtf(0.05f); }

// ---- the figures on sliders (BRIEF_rolling_drag_sliders.md) ----
// The constants above are the defaults of settings: one strength for every ground, a Crr a ground with mud and snow
// apart, how much the pressure counts, and the band of the wheel's sink over which mud and snow are blended in. Each
// has its ini key, its range and its texts here; the mod publishes them for the tuning tool (ng_tune.h) and has no
// slider for them on its own page. Ice stays 0 and has none. The texts are the player's: no apostrophe in them.
enum RdTune { kRdtStrength, kRdtPaved, kRdtRock, kRdtGravel, kRdtDirt, kRdtMud, kRdtSnow, kRdtSand, kRdtPressure, kRdtMudFrom, kRdtMudFull, kRdTuneCount };
struct RdTuneInfo { const char *key, *name, *tip; float least, most, def, step; int decimals; };
static const RdTuneInfo kRdTune[kRdTuneCount] = {
    { "RollingDragStrength", "Strength", "Scales the rolling resistance on every ground, in percent. 100 is the default and 0 is none.", 0.0f, 300.0f, 100.0f, 1.0f, 0 },
    { "CrrPaved", "Paved", "Rolling resistance on paved roads.", 0.0f, 0.1f, 0.010f, 0.001f, 3 },
    { "CrrRock", "Rock", "Rolling resistance on rock.", 0.0f, 0.1f, 0.015f, 0.001f, 3 },
    { "CrrGravel", "Gravel", "Rolling resistance on gravel.", 0.0f, 0.1f, 0.020f, 0.001f, 3 },
    { "CrrDirt", "Dirt", "Rolling resistance on dirt, and on painted mud where the wheel sits shallow.", 0.0f, 0.1f, 0.020f, 0.001f, 3 },
    { "CrrMud", "Mud", "Rolling resistance in mud, on top of the drag the mud itself gives. Blended in from Dirt by how deep the wheel sits.", 0.0f, 0.1f, 0.020f, 0.001f, 3 },
    { "CrrSnow", "Snow", "Rolling resistance in snow. Blended in by how deep the wheel sits, over the Mud from and Mud full band.", 0.0f, 0.1f, 0.020f, 0.001f, 3 },
    { "CrrSand", "Sand", "Rolling resistance on sand.", 0.0f, 0.1f, 0.060f, 0.001f, 3 },
    { "RollingDragPressure", "Pressure effect", "How much airing down raises the drag, in percent. 0 is none and 100 is the default curve.", 0.0f, 200.0f, 100.0f, 1.0f, 0 },
    { "MudFromCm", "Mud from", "Up to this depth in cm a wheel counts as on firm ground. Snow uses the same band.", 0.0f, 20.0f, 5.0f, 1.0f, 0 },
    { "MudFullCm", "Mud full", "From this depth in cm a wheel is fully in mud or snow. Held at least 5 cm over Mud from.", 10.0f, 60.0f, 40.0f, 1.0f, 0 },
};
static const float kRdBandLeast = 5.0f; // cm: Mud full is at least this far over Mud from (the blend divides by the band)

inline void RdTuneDefaults(float v[kRdTuneCount])
{
    for (int i = 0; i < kRdTuneCount; i++) v[i] = kRdTune[i].def;
}
// A figure held: no number is the default, then the range, then rounded to the decimals it is shown and saved with
// (so 0.010 is the float nearest 0.010 and a default comes back as the default, to the bit).
inline float RdTuneHeld(int i, float v)
{
    const RdTuneInfo &t = kRdTune[i];
    if (!isfinite(v)) return t.def;
    double scale = 1.0;
    for (int d = 0; d < t.decimals; d++) scale *= 10.0;
    v = (float)(floor((double)v * scale + 0.5) / scale);
    return v < t.least ? t.least : v > t.most ? t.most : v;
}
// The band held: Mud full at least kRdBandLeast over Mud from. The one that was not moved gives way (moved: kRdtMudFrom
// or kRdtMudFull; anything else, an ini's pair, moves Mud full). Both stay in their ranges by the ranges themselves.
inline void RdBandHold(float v[kRdTuneCount], int moved)
{
    if (v[kRdtMudFull] >= v[kRdtMudFrom] + kRdBandLeast) return;
    if (moved == kRdtMudFull) v[kRdtMudFrom] = v[kRdtMudFull] - kRdBandLeast;
    else v[kRdtMudFull] = v[kRdtMudFrom] + kRdBandLeast;
}
// Whether every figure is the default.
inline bool RdTuneIsDefault(const float v[kRdTuneCount])
{
    for (int i = 0; i < kRdTuneCount; i++)
        if (v[i] != kRdTune[i].def) return false;
    return true;
}

// What the truck update reckons with, finished on the mod's thread: each ground's Crr with the strength in it (ice 0
// at every strength), mud's and snow's the same, and the band in metres. No percent and no power on the game's thread.
// The truck update takes one copy at a frame's start.
struct RdTable { float crr[kRdGroundCount], mud, snow, from, full; };
inline RdTable RdTableMake(const float v[kRdTuneCount])
{
    const float k = v[kRdtStrength] / 100.0f;
    RdTable t = {};
    t.crr[kRdIce] = 0.0f;
    t.crr[kRdPaved] = v[kRdtPaved] * k;
    t.crr[kRdSand] = v[kRdtSand] * k;
    t.crr[kRdGravel] = v[kRdtGravel] * k;
    t.crr[kRdRock] = v[kRdtRock] * k;
    t.crr[kRdDirt] = v[kRdtDirt] * k;
    t.mud = v[kRdtMud] * k;
    t.snow = v[kRdtSnow] * k;
    t.from = v[kRdtMudFrom] / 100.0f;
    t.full = v[kRdtMudFull] / 100.0f;
    return t;
}
inline RdTable RdTableDefault()
{
    float v[kRdTuneCount];
    RdTuneDefaults(v);
    return RdTableMake(v);
}

// The pressure's factor with the effect in it (1 is the curve above, to the bit; 0 is 1 at every pressure; 2 is 1 / p).
// The exponent is scaled, so Normal stays 1 at every effect. The pressure's floor of 0.05 stays inside.
inline float RdPressureFactorWith(float pressure, float effect)
{
    if (effect == 1.0f) return RdPressureFactor(pressure);
    if (!(effect > 0.0f)) return 1.0f;
    return powf(pressure > 0.05f ? pressure : 0.05f, -0.5f * effect);
}

// The share of mud or snow in a wheel's figure by its sink (wheel + 0x178, metres): 0 up to from, 1 from full on, a
// straight line between. A sink that is no number or under 0 is 0. Nothing divides by the sink.
inline float RdShare(float sink, float from, float full)
{
    if (!isfinite(sink) || !(sink > from)) return 0.0f;
    if (sink >= full || !(full > from)) return 1.0f;
    return (sink - from) / (full - from);
}

// A wheel's ground and figure from one read of the wheel (the flags + 0x1C8 .. + 0x1CD, the ice flag + 0x1C0, the
// ground's material + 0x1D0, the sink + 0x178). Ice and paved first and with no blend; then the ground as RdGroundOf
// picks it; then one blend, formed as the ground's figure + share x (the other's - the ground's), so equal figures
// give the ground's to the bit at any sink:
//   snow where + 0x1CC is set (the game resets it every check), or + 0x1CB is set with a material read in the same
//   check (+ 0x1D0 not -1: + 0x1CB is not reset, so without one it is an old byte) and a sink over the band's start;
//   else mud where the ground is dirt.
enum { kRdBlendNone, kRdBlendMud, kRdBlendSnow };
struct RdPick { RdGround ground; int blend; float share, crr; };
inline RdPick RdPickOf(const RdTable &t, const uint8_t flags[6], uint8_t ice, int32_t material, bool sinkOk, float sink)
{
    RdPick p = { RdGroundOf(flags, ice), kRdBlendNone, 0.0f, 0.0f };
    const float base = t.crr[p.ground];
    p.crr = base;
    if (p.ground == kRdIce || p.ground == kRdPaved) return p;
    const float share = sinkOk ? RdShare(sink, t.from, t.full) : 0.0f;
    const bool snow = flags[4] || (flags[3] && material != -1 && share > 0.0f);
    if (!snow && p.ground != kRdDirt) return p;
    const float other = snow ? t.snow : t.mud;
    p.blend = snow ? kRdBlendSnow : kRdBlendMud;
    p.share = share;
    p.crr = share >= 1.0f ? other : base + share * (other - base);
    return p;
}

// The radius the wheel rolls on: its collision cylinder's, which the mod makes smaller than the wheel's own figure
// (wheel + 0x94) with the pressure. With the wheel's own the force at the ground came out larger than Crr·N by the
// one over the other, 13 to 18 percent at Low. The wheel's own figure where there is no cylinder, it is no number or
// not above 0, or it lies outside kRdCylLo to kRdCylHi of the wheel's own (the gauge mod's rule for its cut).
static const float kRdCylLo = 0.5f, kRdCylHi = 1.25f;
inline float RdRollRadius(float wheelRadius, bool haveCylinder, float cylinder)
{
    if (!haveCylinder || !(cylinder > 0.0f) || !(cylinder >= kRdCylLo * wheelRadius && cylinder <= kRdCylHi * wheelRadius)) return wheelRadius;
    return cylinder;
}

// The wheel's damping, c above, held at kRdMost (capped, where given, says it was). Where any of N, r or I is not a
// usable number the stock.
inline float RdDamping(float stock, float crr, float factor, float n, float r, float inertia, float spin, bool *capped = nullptr)
{
    if (capped) *capped = false;
    if (!(n > 0.0f && n < 1.0e7f) || !(r > 0.03f && r < 5.0f) || !(inertia > 0.0f && inertia < 1.0e5f) || !(crr > 0.0f)) return stock;
    const float omega = fmaxf(fabsf(spin), kRdFloor / r);
    const float c = stock + crr * factor * n * r / (inertia * omega);
    if (c < kRdMost) return c;
    if (capped) *capped = true;
    return kRdMost;
}

// The tuning tool's group, why its sliders are greyed, and the lines to read under them (the player's texts).
static const char kRdTuneGroup[] = "Rolling drag";
static const char kRdWhyOff[] = "Rolling drag is off.";
static const char kRdWhyMode[] = "Fixed while a hard mode is on.";
enum { kRdLineGround, kRdLineCrr, kRdLineSlowing, kRdLineSink, kRdLineDepth, kRdLineCount };
struct RdLineInfo { const char *name, *tip; int decimals; };
static const RdLineInfo kRdLines[kRdLineCount] = {
    { "Ground", "The ground most wheels stand on as the mod reads it, with the share of mud or snow.", 0 },
    { "Crr in force", "The rolling resistance in force after strength and pressure, the mean over the wheels on the ground.", 3 },
    { "Slowing", "What the drag as written slows the truck by. The truck alone, with no cargo and no trailer.", 2 },
    { "Deepest wheel", "How deep the deepest wheel sits, and its share of mud or snow.", 0 },
    { "Layer depth", "The depth of the layer under the deepest wheel as the game gives it. Hard ground reads hard.", 2 },
};

// ---- the live read for the tuning tool ----
// Made from what a frame wrote, never from the formula: c is the damping the wheel's body holds (the mod's record), so
// a figure the cap or the 2 percent rule held back is not shown as given. The slowing is the sum over the wheels on the
// ground of (c - stock) x I x |spin| / r, a force at the ground, over the truck's mass (no cargo, no trailer in it).
struct RdLiveWheel { bool grounded, sinkOk, hard, capped; RdGround ground; int blend; float share, crr, c, stock, inertia, spin, r, sink, depth; };
struct RdLive { bool any, capped, slow; float crr, slowing, sinkCm, sharePct, depth; char ground[32], slowingText[32], sinkText[32], depthText[32]; };
inline float RdSlowing(const RdLiveWheel *w, int count, float mass)
{
    float force = 0.0f;
    for (int i = 0; i < count; i++)
        if (w[i].grounded && w[i].c > w[i].stock && w[i].r > 0.03f) force += (w[i].c - w[i].stock) * w[i].inertia * fabsf(w[i].spin) / w[i].r;
    return mass > 0.0f ? force / mass : 0.0f;
}
// factor: the pressure's, in force; speed: the truck's, m/s. The ground is the one most wheels on the ground read, with
// the mean share of those wheels; the Crr the mean over the wheels on the ground; the sink, its share and the layer
// depth (wheel + 0x1BC, 4.0 and "hard" where the hard flag is set) are the deepest wheel's.
inline void RdLiveMake(const RdLiveWheel *w, int count, float factor, float mass, float speed, RdLive &out)
{
    out = RdLive{};
    int votes[3 * kRdGroundCount] = {}, grounded = 0, best = -1, deepest = -1;
    float shares[3 * kRdGroundCount] = {}, crr = 0.0f;
    for (int i = 0; i < count; i++)
    {
        if (!w[i].grounded) continue;
        const int key = (w[i].blend == kRdBlendSnow ? 2 : w[i].blend == kRdBlendMud && w[i].share > 0.0f ? 1 : 0) * kRdGroundCount + w[i].ground;
        votes[key]++;
        shares[key] += w[i].share;
        if (best < 0 || votes[key] > votes[best]) best = key;
        crr += w[i].crr * factor;
        grounded++;
        out.capped = out.capped || w[i].capped;
        if (w[i].sinkOk && isfinite(w[i].sink) && (deepest < 0 || w[i].sink > w[deepest].sink)) deepest = i;
    }
    snprintf(out.ground, sizeof out.ground, "off the ground");
    snprintf(out.slowingText, sizeof out.slowingText, "no read");
    snprintf(out.sinkText, sizeof out.sinkText, "no read");
    snprintf(out.depthText, sizeof out.depthText, "no read");
    if (!grounded) return;
    out.any = true;
    out.crr = crr / (float)grounded;
    const RdGround ground = (RdGround)(best % kRdGroundCount);
    const int percent = (int)(shares[best] / (float)votes[best] * 100.0f + 0.5f);
    if (best / kRdGroundCount == 2) snprintf(out.ground, sizeof out.ground, "snow on %s, %d percent", kRdGroundNames[ground], percent);
    else if (best / kRdGroundCount == 1) snprintf(out.ground, sizeof out.ground, "mud, %d percent", percent);
    else snprintf(out.ground, sizeof out.ground, "%s", kRdGroundNames[ground]);
    out.slowing = RdSlowing(w, count, mass);
    out.slow = fabsf(speed) < kRdFloor;
    snprintf(out.slowingText, sizeof out.slowingText, "%.2f m/s2%s%s", out.slowing, out.capped ? ", capped" : "", out.slow ? ", under 2 m/s" : "");
    if (deepest < 0) return;
    out.sinkCm = w[deepest].sink * 100.0f;
    out.sharePct = w[deepest].share * 100.0f;
    snprintf(out.sinkText, sizeof out.sinkText, "%.0f cm, %.0f percent", out.sinkCm, out.sharePct);
    out.depth = w[deepest].depth;
    if (w[deepest].hard) snprintf(out.depthText, sizeof out.depthText, "hard");
    else snprintf(out.depthText, sizeof out.depthText, "%.2f m", out.depth);
}

// Whether the drag acts on a truck at all, by what does not change with the ground (a jump or a truck on its side
// must not read as a truck the drag cannot reckon): the mass read, and at least one wheel that is read, not broken
// and has its axle told from its inertia, in contact or not. told: the wheels with an axle told; reckoned: those of
// them read and not broken. Where it does not act the wheels get no drag, so the fuel must not be left to it.
enum RdWhy { kRdActs, kRdNoMass, kRdNoAxle, kRdNoWheel };
inline RdWhy RdActsWhy(bool massRead, int told, int reckoned)
{
    return !massRead ? kRdNoMass : reckoned > 0 ? kRdActs : told > 0 ? kRdNoWheel : kRdNoAxle;
}

// The fuel's side of that answer, kept on the mod's thread. The truck update leaves the answer with its vehicle; the
// mod's thread takes it only for the vehicle it serves (have). No answer yet for a vehicle: the drag is taken to act,
// so a good truck never pays the fuel factor and the drag at the start. No frame for a while: the last answer for
// that vehicle holds. A truck the drag does not act on gets one line, once a session, and one more, once, if it
// changes over later (the mass read late). Only such trucks are kept, the last kRdFuelTrucks of them.
enum RdSay { kRdSayNothing, kRdSayNot, kRdSayActs };
struct RdFuelTruck { uint64_t vehicle; bool acts, notSaid, actsSaid; };
static const int kRdFuelTrucks = 32;
struct RdFuelState { RdFuelTruck truck[kRdFuelTrucks]; int count, next; };
inline bool RdFuelTake(RdFuelState &s, uint64_t vehicle, bool have, bool acts, RdSay *say = nullptr)
{
    if (say) *say = kRdSayNothing;
    RdFuelTruck *t = nullptr;
    for (int i = 0; i < s.count; i++)
        if (s.truck[i].vehicle == vehicle) t = &s.truck[i];
    if (!have) return t ? t->acts : true;
    if (!t)
    {
        if (acts) return true; // a good truck with nothing said needs no record
        t = &s.truck[s.count < kRdFuelTrucks ? s.count++ : s.next++ % kRdFuelTrucks];
        *t = { vehicle, true, false, false };
    }
    t->acts = acts;
    if (!acts && !t->notSaid)
    {
        t->notSaid = true;
        if (say) *say = kRdSayNot;
    }
    else if (acts && t->notSaid && !t->actsSaid)
    {
        t->actsSaid = true;
        if (say) *say = kRdSayActs;
    }
    return acts;
}

// The mod's own record of a wheel: the wheel it was written for, the word written last and the c it stood for.
struct RdRecord { uint64_t wheel; uint16_t word; float c; };

// Whether a wheel's c goes in now: when the mod has not written it, the body holds another word than the one the mod
// wrote last (the game wrote it: compared as a word, never taken as a stock), or c moved by more than 2 % since.
inline bool RdWrite(const RdRecord *rec, uint64_t wheel, uint16_t bodyWord, float c)
{
    if (!rec || rec->wheel != wheel || rec->word != bodyWord) return true;
    return fabsf(c - rec->c) > kRdChange * fabsf(rec->c);
}
