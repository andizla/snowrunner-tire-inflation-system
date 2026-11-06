// Stiff base (StiffBase=1; BRIEF_tire_stiff_and_per_truck.md part 1, BRIEF_drive_2052_fixes.md F2). Each tire type has
// a softness preset (WheelSoftness: Stiff RadiusOffset 0.02, Soft 0.03, ScoutSoft 0.1, Average 0.025), and the mod's
// modes add their offset on top, so a soft tire at Low went flatter than a Stiff one. The game holds a wheel's
// RadiusOffset at wheel+0xB4 times the wheel's Scale (the drive of 2026-11-06 20:52: one tire at two sizes, each held at
// the file's figure times the radius; a Soft 0.03 on a wheel of 0.580 is held 0.0174), and the stock cylinder is the
// radius less it. With StiffBase on the
// cylinder the modes start from is the radius less at most 0.02 x Scale, so a tire softer than Stiff sits as a Stiff
// one does; one already stiffer keeps its own. Off, the game's own offset again.
// +0x120 is not SoftForceScale (the drive of 2026-11-06 20:52): it read 1e12, -8e32, 0 and -1.9e33 on the wheels of
// one tire, never its file's figure. Its place is searched for, read only (SoftTargets, SoftForceSearch in
// tire_pressure.cpp), and logged once per truck and tire.
// The tire is known by its mesh's text at wheel model+0x060 -> +0x010 (the game's 0x20-byte text), which tells a tire
// change; the tire's Name from its file is not held there.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

static const float kStiffRadiusOffset = 0.02f;

// The RadiusOffset the base keeps, in the units the game holds it: at most Stiff's 0.02 times the Scale. held: wheel+0xB4.
// scale: wheel+0x98, the radius, with the tire file's Radius taken as 1 (every vanilla TruckWheels file that sets a
// Radius sets 1; a mod tire of another Radius is held a little off, never past its radius). on: StiffBase.
// A held offset within kStiffSlack of the cap is kept as it is, so a Stiff tire (0.02 x 0.42 = 0.0084, the CK1500's) is
// never written an ulp off (the Reviewer, point 13).
static const float kStiffSlack = 0.0005f;
static float StiffKeptOffset(float held, float scale, bool on)
{
    if (!on || !(scale > 0.0f)) return held;
    const float most = kStiffRadiusOffset * scale;
    return held > most + kStiffSlack ? most : held;
}

// A text worth logging: 3 to 80 characters of letters, digits, space and _ - / . : ' ( ) &, at least two letters,
// ended by a 0 within n.
static bool PlainText(const uint8_t *p, size_t n, std::string &out)
{
    out.clear();
    int letters = 0;
    for (size_t i = 0; i < n && i <= 80; i++)
    {
        const uint8_t c = p[i];
        if (!c) { if (out.size() >= 3 && letters >= 2) return true; out.clear(); return false; }
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!(letter || (c >= '0' && c <= '9') || strchr(" _-/.:'()&", c))) { out.clear(); return false; }
        letters += letter;
        out.push_back((char)c);
    }
    out.clear();
    return false;
}

// The game's text (0x20 bytes, as at model+0x588): its characters inline while the room is under 16, else a pointer
// to them; the length at +0x10, the room at +0x18. True with inlineChars set when it holds its characters itself;
// else pointer is where they are (read by the caller).
static bool GameTextLayout(const uint8_t *t, std::string &inlineChars, uint64_t &pointer, uint64_t &length)
{
    uint64_t room = 0;
    memcpy(&pointer, t, 8);
    memcpy(&length, t + 0x10, 8);
    memcpy(&room, t + 0x18, 8);
    inlineChars.clear();
    if (!length || length > 80 || room < length || room > 4096) return false;
    if (room < 16)
    {
        uint8_t buf[17] = {};
        memcpy(buf, t, (size_t)length);
        pointer = 0;
        return PlainText(buf, 17, inlineChars) && inlineChars.size() == length;
    }
    return pointer >= 0x10000;
}

// A key for a text (FNV-1a), 0 for none: what the wheel's record keeps of its tire's mesh.
static uint64_t TextKey(const std::string &t)
{
    if (t.empty()) return 0;
    uint64_t h = 1469598103934665603ULL;
    for (const char c : t) h = (h ^ (uint8_t)c) * 1099511628211ULL;
    return h ? h : 1;
}

// ---- the SoftForceScale search, read only ----
// The figures a wheel's SoftForceScale may be held as: each preset's (Soft 0.12, ScoutSoft 0.20,
// Average 0.24, Stiff 0.32), as it is and times the wheel's Scale, and the reciprocals of both in case the game holds
// the inverse. A float within 0.5 % of one is a hit.
struct SoftTarget { float value; char what[24]; };
static const float kSoftScales[4] = { 0.12f, 0.20f, 0.24f, 0.32f };
static const int kSoftTargets = 16;
static int SoftTargets(float scale, SoftTarget out[kSoftTargets])
{
    int n = 0;
    for (const float f : kSoftScales)
    {
        out[n].value = f;
        snprintf(out[n++].what, sizeof out[0].what, "%.2f", f);
        out[n].value = 1.0f / f;
        snprintf(out[n++].what, sizeof out[0].what, "1/%.2f", f);
        if (!(scale > 0.0f)) continue;
        out[n].value = f * scale;
        snprintf(out[n++].what, sizeof out[0].what, "%.2f x Scale", f);
        out[n].value = 1.0f / (f * scale);
        snprintf(out[n++].what, sizeof out[0].what, "1/(%.2f x Scale)", f);
    }
    return n;
}
// The target v hits, or -1.
static int SoftTargetHit(float v, const SoftTarget *t, int n)
{
    if (!(v > 0.0f) || v > 100.0f) return -1;
    for (int i = 0; i < n; i++)
        if (fabsf(v - t[i].value) <= t[i].value * 0.005f) return i;
    return -1;
}

// A hit: its way from the wheel ("wheel+0x120", "wheel+0x018 -> +0x040", "wheel+0x018 -> +0x040 -> +0x0A8"), what it
// hit and the float there.
struct SoftHit { std::string path; std::string what; float value; };

// The ways found on every wheel (each wheel's hits in its own list), in the first wheel's order.
static std::vector<std::string> SoftHitsOnEvery(const std::vector<std::vector<SoftHit>> &perWheel)
{
    std::vector<std::string> every;
    if (perWheel.empty()) return every;
    for (const SoftHit &h : perWheel[0])
    {
        bool all = true;
        for (size_t w = 1; w < perWheel.size() && all; w++)
        {
            bool found = false;
            for (const SoftHit &o : perWheel[w]) if (o.path == h.path) { found = true; break; }
            all = found;
        }
        bool twice = false;
        for (const std::string &e : every) if (e == h.path) twice = true;
        if (all && !twice) every.push_back(h.path);
    }
    return every;
}
