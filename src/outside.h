// outside.h: the outside temperature and what it does to a tire. A gauge cluster mod by the same author works the
// temperature out by map and hour and publishes it for other mods in a block of shared memory,
// "Local\SnowRunnerGauges.State.<process id>", about 20 times a second; the layout below is its version 1. Without
// it, or with a block that stands still for kOutsideStaleMs, AmbientC from the ini is the outside temperature, and
// the block is looked for again every kOutsideLookMs. The block is copied while its sequence is even and unchanged,
// as the writer makes it odd while it writes. The writer's file is never touched: the mapping lives on while either
// side holds it, so there is never a pointer into a module that went away.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static const uint32_t kOutsideMagic = 0x47554147u; // "GAUG"
static const uint32_t kOutsideVersion = 1;
static const unsigned long long kOutsideLookMs = 5000, kOutsideStaleMs = 2000;

// Its flags, as the gauge mod sets them.
enum : uint32_t
{
    kOutsideVehicle = 1u << 0,  // a truck is driven; vehicle names it
    kOutsideByMap = 1u << 1,    // the temperature is the map's, by map and hour, not a setting
    kOutsideEngineOn = 1u << 2, // the truck's engine runs
};

// The block as the gauge mod lays it out, version 1. A later version only adds to its end.
struct OutsideBlock
{
    uint32_t magic, version, size;
    volatile long sequence; // odd while the writer writes
    uint64_t tick;          // GetTickCount64 at the write
    uint64_t vehicle;       // the truck driven, 0 none
    float ambientC;         // the outside temperature in force, degrees C
    float coolantC;         // the engine's coolant temperature as the gauge mod models it
    float rpm;              // the engine speed as the gauge mod models it
    float boostBar;         // the boost pressure as it models it
    uint32_t flags;
    uint32_t reserved[3];
};
static_assert(sizeof(OutsideBlock) == 64 && offsetof(OutsideBlock, tick) == 16 && offsetof(OutsideBlock, ambientC) == 32 && offsetof(OutsideBlock, flags) == 48,
              "the gauge mod's layout, version 1");

// A consistent copy of the block, or false while the writer is in the middle of a write or the block is not one of
// its.
inline bool OutsideCopy(const volatile OutsideBlock *from, OutsideBlock &to)
{
    for (int tries = 0; tries < 8; tries++)
    {
        const long before = from->sequence;
        if (before & 1) { Sleep(0); continue; }
        memcpy(&to, (const void *)from, sizeof to);
        MemoryBarrier();
        if (from->sequence != before) continue;
        return to.magic == kOutsideMagic && to.version >= 1 && to.size >= sizeof(OutsideBlock);
    }
    return false;
}

// The mapping, held open once it is found.
struct OutsideReader
{
    HANDLE mapping;
    const volatile OutsideBlock *block;
    unsigned long long lookAt; // when it is looked for next
};

// Where the outside temperature in force comes from: the ini, another mod's block, or that block with the map's own
// temperature (kOutsideByMap). The tab words its line and the slider's tip by this (panel.h). fresh: OutsideRead's
// answer; flags: the flags of the block that answer came from.
enum { kOutsideFromIni = 0, kOutsideFromMod = 1, kOutsideFromMap = 2 };
inline int OutsideSource(bool fresh, uint32_t flags)
{
    return !fresh ? kOutsideFromIni : flags & kOutsideByMap ? kOutsideFromMap : kOutsideFromMod;
}

// The outside temperature from the block, when there is one and it is fresh: true with c, and with flags the flags
// of the same copy of the block. The mapping is opened the first time it is there (looked for every kOutsideLookMs
// until then) and held open after.
inline bool OutsideRead(OutsideReader &r, unsigned long long now, float &c, uint32_t *flags = nullptr)
{
    if (!r.block && now >= r.lookAt)
    {
        r.lookAt = now + kOutsideLookMs;
        wchar_t name[64];
        swprintf(name, 64, L"Local\\SnowRunnerGauges.State.%lu", GetCurrentProcessId());
        r.mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (r.mapping)
        {
            r.block = (const volatile OutsideBlock *)MapViewOfFile(r.mapping, FILE_MAP_READ, 0, 0, sizeof(OutsideBlock));
            if (!r.block)
            {
                CloseHandle(r.mapping);
                r.mapping = nullptr;
            }
        }
    }
    OutsideBlock b;
    // (a block written a moment after `now` was taken is fresh, so the difference is signed)
    if (!r.block || !OutsideCopy(r.block, b) || (long long)(now - b.tick) > (long long)kOutsideStaleMs || !(b.ambientC > -90.0f && b.ambientC < 90.0f)) return false;
    c = b.ambientC;
    if (flags) *flags = b.flags;
    return true;
}

inline void OutsideClose(OutsideReader &r)
{
    if (r.block) UnmapViewOfFile((const void *)r.block);
    if (r.mapping) CloseHandle(r.mapping);
    r = OutsideReader{};
}

// ---- what the temperature does to a tire ----
// The modes' numbers are for kTempRef. The air in a tire follows the gas law on its absolute pressure: Normal is taken
// as 35 psi over the air's 14.7 (kTempAtm, as a share of Normal's), so at 20 below Normal holds 83 percent of its
// pressure and Low about 76 percent of its own. Rubber hardens in the cold and grips less, most on hard ground
// (asphalt, rock: a fifth less at 20 below), less on soft ground; warm rubber grips a little more on hard ground.
// A soft tire's sidewalls heat as they flex, so in the heat the wear starts from lower speeds, in the cold from higher.
static const float kTempRef = 15.0f, kTempAtm = 14.7f / 35.0f;
struct TempEffect { float pressure, hard, soft, speed; }; // factors: the air's absolute pressure, grip on hard and on soft ground, the wear speeds

inline TempEffect TempEffectAt(float c)
{
    TempEffect e;
    const float below = fmaxf(0.0f, kTempRef - c), above = fmaxf(0.0f, c - kTempRef);
    e.pressure = (c + 273.15f) / (kTempRef + 273.15f);
    e.hard = fmaxf(0.7f, fminf(1.08f, 1.0f - 0.006f * below + 0.003f * above));
    e.soft = fmaxf(0.85f, 1.0f - 0.003f * below);
    e.speed = fmaxf(0.75f, fminf(1.25f, 1.0f + 0.006f * (kTempRef - c)));
    return e;
}

// The tires' pressure (a share of Normal's) as the air's temperature leaves it.
inline float TempPressure(float pressure, const TempEffect &e) { return (pressure + kTempAtm) * e.pressure - kTempAtm; }
