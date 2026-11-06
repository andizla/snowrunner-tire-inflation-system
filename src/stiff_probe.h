// Stiff base, part 1's read-only step (BRIEF_tire_stiff_and_per_truck.md): each wheel's softness pair as the game
// holds it, RadiusOffset at wheel+0xB4 (the stock cylinder is +0x94 less it) and SoftForceScale at wheel+0x120, logged
// once per truck and again when a wheel's pair changes (a tire change, a rebuild). Nothing is written. The game's
// WheelSoftness presets (initial_templates\trucks.xml) name a pair where it matches one. The tire's name has no known
// place yet, so the first wheel's object and model are searched once per truck for texts, and every text found is
// logged with the way to it: the drive's log shows where the name is.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

struct SoftPreset { const char *name; float radiusOffset, softForceScale; };
static const SoftPreset kSoftPresets[] = {
    { "Stiff", 0.02f, 0.32f }, { "Soft", 0.03f, 0.12f }, { "ScoutSoft", 0.1f, 0.2f }, { "Average or ScoutDefault", 0.025f, 0.24f },
};

// The preset a pair matches, else nullptr. scale: what the offset may have been multiplied by (the wheel's
// CompatibleWheels Scale, if the game scales it): a match at 1 or at that scale is named, and which one.
static const char *SoftPresetName(float radiusOffset, float softForceScale, float scale, bool &scaled)
{
    scaled = false;
    for (const SoftPreset &p : kSoftPresets)
    {
        if (fabsf(softForceScale - p.softForceScale) > 0.0005f) continue;
        if (fabsf(radiusOffset - p.radiusOffset) <= 0.0005f) return p.name;
        if (scale > 0.0f && fabsf(radiusOffset - p.radiusOffset * scale) <= 0.0005f) { scaled = true; return p.name; }
    }
    return nullptr;
}

// One wheel's pair as read.
struct SoftPair { uint64_t wheel = 0; float radiusOffset = 0.0f, softForceScale = 0.0f, radius = 0.0f; };

// Whether the pairs read now differ from those logged last: another wheel object, another count, or a value moved.
static bool SoftPairsChanged(const std::vector<SoftPair> &was, const std::vector<SoftPair> &now)
{
    if (was.size() != now.size()) return true;
    for (size_t i = 0; i < now.size(); i++)
        if (was[i].wheel != now[i].wheel || fabsf(was[i].radiusOffset - now[i].radiusOffset) > 0.0001f ||
            fabsf(was[i].softForceScale - now[i].softForceScale) > 0.0001f)
            return true;
    return false;
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
