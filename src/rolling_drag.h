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
// than a rolling one), I the body's inertia about the axle, r the wheel's radius and c_stock the game's own figure for
// the wheel (wheel + 0xAC, never the body's word read back). Above the floor the torque is Crr·N·r; below it the torque
// falls with the spin to nothing standing, so the damping never pushes a wheel. N is the wheel's share of the weight:
// the truck's Havok mass (the chassis's and every wheel's, the mass the physics moves) times g over the wheels on the
// ground. Cargo and a trailer are not in it.
#pragma once
#include <math.h>
#include <stdint.h>

// Crr by the ground the game's check left in the wheel (+0x1C8 paved, +0x1C9 gravel, +0x1CA sand, +0x1CD hard, that is
// rock) and its ice flag (+0x1C0). In this order where several are set: ice, paved, sand, gravel, rock, and dirt for
// the rest, which takes a wheel with no flag set too (mud sets none, so mud gets dirt's: the mod cannot tell them
// apart yet). Ice is 0, as the game's own stays. The numbers are this mod's, from published truck tire figures
// (0.006 to 0.01 on asphalt at the rated pressure).
enum RdGround { kRdIce, kRdPaved, kRdSand, kRdGravel, kRdRock, kRdDirt, kRdGroundCount };
static const float kRdCrr[kRdGroundCount] = { 0.0f, 0.010f, 0.060f, 0.020f, 0.015f, 0.030f };
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

// The mod's own record of a wheel: the wheel it was written for, the word written last and the c it stood for.
struct RdRecord { uint64_t wheel; uint16_t word; float c; };

// Whether a wheel's c goes in now: when the mod has not written it, the body holds another word than the one the mod
// wrote last (the game wrote it: compared as a word, never taken as a stock), or c moved by more than 2 % since.
inline bool RdWrite(const RdRecord *rec, uint64_t wheel, uint16_t bodyWord, float c)
{
    if (!rec || rec->wheel != wheel || rec->word != bodyWord) return true;
    return fabsf(c - rec->c) > kRdChange * fabsf(rec->c);
}
