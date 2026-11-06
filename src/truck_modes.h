// truck_modes.h: each truck's pressure mode (PerTruck, since 1.3.1) in TirePressure.trucks.ini next to the exe.
// [<kind>] holds the mode of every truck of a kind, by the kind's name as the game's files name it ("western_star_6900xd";
// "zetros/176593" for a truck from a mod). Since 1.3.4 [<kind>:<id>] holds one owned truck's, by the game's own id for
// it ("{8E42BF0A-F7C6-40f9-B0F2-225DE74CE031}"), with Own=1 while it is in force (Own=0: kept, not in use; a section
// without Own is in force). The mode in force: the truck's own, else the kind's, else the start mode (the highest
// mode on offer). tire_pressure.cpp reads the kind and the id from the game (TruckKindName, TruckIdRead) and calls these.
// The id, its shape and the take-over of a truck the game made anew under another id are read and done as Next Gen
// Gameplay does (its src\truck.h lines 375 to 405, TruckIdentity, TruckIdText and TruckKey, and src\gauges.cpp's
// TruckSuspTakeOver, TruckSuspSeen, TruckSuspPrune, SuspTrack and SuspAgainTakeOver, at its commit 88e1ba7), so both
// mods name a truck alike. Tire Inflation runs without Gameplay: this is its own copy, not a call into it.
#pragma once
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include "panel.h"

static const wchar_t *const kTruckModesFile = L"TirePressure.trucks.ini";
static const wchar_t *const kTruckModeNames[kModeCount] = { L"Low", L"Reduced", L"Normal", L"Increased" }; // PressureMode

// A kind's name made fit for a section of the ini: what would end the name there, or does not print, becomes '_'
// (as Next Gen Gameplay's TruckKey does for its truck scales).
inline void TruckKindClean(char *name)
{
    for (char *c = name; *c; c++)
        if ((unsigned char)*c <= 0x20 || (unsigned char)*c > 0x7E || *c == '[' || *c == ']') *c = '_';
}

inline std::wstring TruckKindWide(const char *kind)
{
    std::wstring w;
    while (*kind) w += (wchar_t)(unsigned char)*kind++;
    return w;
}

// The shape of the game's id of a truck: 38 characters, braces around hex digits in groups of 8, 4, 4, 4 and 12.
// Gameplay's TruckIdText (src\truck.h at 88e1ba7), as it is.
inline bool TruckIdText(const char *text, size_t length)
{
    if (length != 38 || text[0] != '{' || text[37] != '}') return false;
    for (size_t i = 1; i < 37; i++)
    {
        const char c = text[i];
        const bool dash = i == 9 || i == 14 || i == 19 || i == 24;
        if (dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// The game's id of the truck at vehicle, as Gameplay's TruckKey reads it (src\truck.h at 88e1ba7): the text in braces
// behind the pointer at vehicle + 0x48, 38 characters and its end. "" where it is not read or has not the shape of one.
// peek reads size bytes at an address of the game's into out, false where it cannot.
template <class Peek> inline void TruckIdOf(Peek peek, uint64_t vehicle, char (&id)[40])
{
    id[0] = 0;
    uint64_t chars = 0;
    char text[39];
    if (peek(vehicle + 0x48, &chars, sizeof chars) && chars >= 0x10000 && peek(chars, text, sizeof text) && !text[38] && TruckIdText(text, 38)) memcpy(id, text, sizeof text);
}

// The kind's name of the truck at vehicle as the game's files name it: a text at model + 0x588, the model at
// vehicle + 0x60. A text is 0x20 bytes: its characters while it has room for fewer than 16, else a pointer to them;
// the length at + 0x10 and the room at + 0x18 (Next Gen Gameplay's TruckText reads it so). False where it is not read,
// empty or 95 long or more.
template <class Peek> inline bool TruckKindOf(Peek peek, uint64_t vehicle, char (&out)[96])
{
    out[0] = 0;
    uint64_t model = 0, chars = 0, length = 0, room = 0;
    uint8_t text[0x20];
    if (!peek(vehicle + 0x60, &model, sizeof model) || !peek(model + 0x588, text, sizeof text)) return false;
    memcpy(&chars, text, 8);
    memcpy(&length, text + 0x10, 8);
    memcpy(&room, text + 0x18, 8);
    if (!length || length >= 95 || room < length) return false;
    if (room < 16) memcpy(out, text, (size_t)length);
    else if (chars < 0x10000 || !peek(chars, out, (size_t)length)) return false;
    out[length] = 0;
    TruckKindClean(out);
    return out[0] != 0;
}

// A vehicle's chassis body (model + 0x230, the model at vehicle + 0x60), the body's vtable, its inverse mass
// (+ 0x22C) and its world: hkpWorldObject's m_world at + 0x10 in Havok's layout (not confirmed in this game yet; the
// once-a-truck line gives the chassis's and the first wheel body's). False where a read fails.
template <class Peek> inline bool TruckChassisWorld(Peek peek, uint64_t vehicle, uint64_t &body, uint64_t &vtable, float &inverseMass, uint64_t &world)
{
    uint64_t model = 0;
    body = vtable = world = 0;
    inverseMass = 0.0f;
    return peek(vehicle + 0x60, &model, sizeof model) && peek(model + 0x230, &body, sizeof body) && peek(body, &vtable, sizeof vtable) &&
           peek(body + 0x22C, &inverseMass, sizeof inverseMass) && peek(body + 0x10, &world, sizeof world);
}

// The take-over's check on another vehicle within 20 s (BRIEF_takeover_other_truck.md and the Reviewer's addendum,
// as Next Gen Gameplay's TruckStillThere): whether the truck that went is still in the game. A switch to another
// truck of the kind through the map goes through "none driven" as a rebuild does, and a freed vehicle block can still
// read its old id. So the old truck counts as there only when all hold: its vehicle reads the old id (vehicle + 0x48)
// and the old kind; its chassis body has the new truck's chassis's vtable and the inverse mass of 10 kg to 1000 t;
// and that body's world is not 0 and is the new chassis's. A destroyed truck's bodies leave the world before they are
// freed; a truck left through the map stays in it. Anything else (a read fails, another id or kind, another world, or
// none) is gone.
struct TruckThere
{
    char id[40];                              // what the old vehicle reads now ("" not read)
    char kind[96];
    uint64_t body, world, newBody, newWorld;  // the old and the new chassis body and their worlds (0 not read)
    bool there;
};
template <class Peek> inline bool TruckStillThere(Peek peek, uint64_t oldVehicle, const char *oldKind, const char *oldId, uint64_t newVehicle, TruckThere &o)
{
    o = TruckThere{};
    TruckIdOf(peek, oldVehicle, o.id);
    TruckKindOf(peek, oldVehicle, o.kind);
    uint64_t vtable = 0, newVtable = 0;
    float inverse = 0.0f, newInverse = 0.0f;
    const bool body = TruckChassisWorld(peek, oldVehicle, o.body, vtable, inverse, o.world);
    const bool newBody = TruckChassisWorld(peek, newVehicle, o.newBody, newVtable, newInverse, o.newWorld);
    const float mass = inverse > 0.0f ? 1.0f / inverse : 0.0f;
    o.there = oldId[0] && !strcmp(o.id, oldId) && oldKind[0] && !strcmp(o.kind, oldKind) && body && newBody && vtable && vtable == newVtable && o.body != o.newBody &&
              isfinite(mass) && mass >= 10.0f && mass <= 1.0e6f && o.world && o.world == o.newWorld;
    return o.there;
}

// The first wheel's body of the truck at vehicle and its world, for the once-a-truck line: the wheel models from
// [vehicle + 0x200], the wheel at wheel model + 0x2C8, its body at wheel + 0x18, m_world at body + 0x10.
template <class Peek> inline bool TruckWheelWorld(Peek peek, uint64_t vehicle, uint64_t &body, uint64_t &world)
{
    uint64_t begin = 0, wheelModel = 0, wheel = 0;
    body = world = 0;
    return peek(vehicle + 0x200, &begin, sizeof begin) && peek(begin, &wheelModel, sizeof wheelModel) && peek(wheelModel + 0x2C8, &wheel, sizeof wheel) &&
           peek(wheel + 0x18, &body, sizeof body) && peek(body + 0x10, &world, sizeof world);
}

// Which truck the mode goes by: its kind's name and the game's id for it ("" = not read). kind "" = none driven.
struct TruckWho2 { char kind[96]; char id[40]; };
inline bool TruckWhoSame(const TruckWho2 &a, const TruckWho2 &b) { return !strcmp(a.kind, b.kind) && !strcmp(a.id, b.id); }

// A truck's section: the kind's, or with an id the owned truck's.
inline std::wstring TruckSection(const char *kind, const char *id)
{
    return id && id[0] ? TruckKindWide(kind) + L":" + TruckKindWide(id) : TruckKindWide(kind);
}

// The mode a section holds; false when none is saved there or the word is no mode's name.
inline bool TruckSectionMode(const std::wstring &file, const std::wstring &section, int &mode)
{
    wchar_t buf[32] = {};
    GetPrivateProfileStringW(section.c_str(), L"Mode", L"", buf, 32, file.c_str());
    for (int m = 0; m < kModeCount; m++)
        if (buf[0] && !_wcsicmp(buf, kTruckModeNames[m])) { mode = m; return true; }
    return false;
}

// The mode saved for kind (the kind's section alone); false when none is saved or the word is no mode's name.
inline bool TruckModeLoad(const std::wstring &file, const char *kind, int &mode) { return TruckSectionMode(file, TruckSection(kind, ""), mode); }

// Where the mode in force comes from: the start mode (no section for the truck), the kind's section, the truck's own.
enum { kTruckLevelStart, kTruckLevelKind, kTruckLevelOwn };
struct TruckModeFound
{
    int mode;     // the mode in force; unset at kTruckLevelStart
    int level;    // kTruckLevel...
    bool ownKept; // the truck's own section is there with Own=0 (its mode kept, not in use)
    int keptMode; // that section's mode
};

// The mode for the truck kind/id: its own section where it is in force, else the kind's, else none (the start mode).
// id "": the kind's alone.
inline TruckModeFound TruckModeFind(const std::wstring &file, const char *kind, const char *id)
{
    TruckModeFound f = { kNormal, kTruckLevelStart, false, kNormal };
    if (id && id[0])
    {
        const std::wstring own = TruckSection(kind, id);
        int m = kNormal;
        if (TruckSectionMode(file, own, m))
        {
            if (GetPrivateProfileIntW(own.c_str(), L"Own", 1, file.c_str())) { f.mode = m; f.level = kTruckLevelOwn; return f; }
            f.ownKept = true;
            f.keptMode = m;
        }
    }
    int m = kNormal;
    if (TruckSectionMode(file, TruckSection(kind, ""), m)) { f.mode = m; f.level = kTruckLevelKind; }
    return f;
}

// A missing file is made first with a note at its head on what it is.
inline void TruckModesFileMake(const std::wstring &file)
{
    if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    FILE *f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"w, ccs=UTF-16LE") == 0 && f)
    {
        fwprintf(f, L"; Each truck's pressure mode: [kind] for every truck of a kind, [kind:{id}] for one owned truck by the game's id\n"
                    L"; for it (Own=1 in force, Own=0 kept). It is written when a mode is chosen and put back when that truck is\n"
                    L"; driven again. TakenBy and TakenAt mark a truck the game made anew under another id. PerTruck=0 in\n"
                    L"; TirePressure.ini turns this off.\n");
        fclose(f);
    }
}

// The mode for kind, into the kind's section. Key by key through the profile API.
inline void TruckModeSave(const std::wstring &file, const char *kind, int mode)
{
    if (mode < 0 || mode >= kModeCount) return;
    TruckModesFileMake(file);
    WritePrivateProfileStringW(TruckSection(kind, "").c_str(), L"Mode", kTruckModeNames[mode], file.c_str());
}

// The mode for one owned truck, into its own section with Own (1 = in force, 0 = kept).
inline void TruckModeSaveOwn(const std::wstring &file, const char *kind, const char *id, int mode, bool own)
{
    if (mode < 0 || mode >= kModeCount || !id || !id[0]) return;
    TruckModesFileMake(file);
    const std::wstring sec = TruckSection(kind, id);
    WritePrivateProfileStringW(sec.c_str(), L"Own", own ? L"1" : L"0", file.c_str());
    WritePrivateProfileStringW(sec.c_str(), L"Mode", kTruckModeNames[mode], file.c_str());
}

// A mode chosen in the panel for the truck kind/id: it goes into the truck's own section (Own=1), unless This truck only
// is unticked for it (ownKept: its own section holds Own=0), or its id is not read; then into the kind's. So the kind's
// section is only what a truck no one has changed reads (the lead's call on the review's F1). The level now in force.
inline int TruckModeChosen(const std::wstring &file, const char *kind, const char *id, bool ownKept, int mode)
{
    if (ownKept || !id || !id[0])
    {
        TruckModeSave(file, kind, mode);
        return kTruckLevelKind;
    }
    TruckModeSaveOwn(file, kind, id, mode, true);
    return kTruckLevelOwn;
}

inline int TruckModeInForce(const TruckModeFound &f, int modeCount);

// The tick "This truck only" for the truck kind/id in mode (f as TruckModeFind found it). It shows unticked only where
// the truck's own section holds Own=0. On: that section is in force again with its mode (Gameplay's This truck only
// brings a kept set back). Off: the truck's own section holds Own=0, written with the mode in force where it had none,
// and the kind's mode is in force. A truck at its own level whose kind has no section lifts its mode up into the kind's
// first, as Gameplay's Every <kind> does, so nothing moves (the review's N3). Nothing for a truck whose id is not read.
// True when the mode in force changes: newMode is then the one now in force. modeCount: the modes in use (3 or 4).
inline bool TruckOnlySet(const std::wstring &file, const char *kind, const char *id, const TruckModeFound &f, int mode, int modeCount, bool on, int &newMode)
{
    newMode = mode;
    if (!kind[0] || !id || !id[0]) return false;
    if (on)
    {
        if (!f.ownKept) return false;
        TruckModeSaveOwn(file, kind, id, f.keptMode, true);
        newMode = f.keptMode < modeCount ? f.keptMode : kNormal;
        return newMode != mode;
    }
    if (f.ownKept) return false;
    if (f.level == kTruckLevelOwn)
    {
        int kindMode = kNormal;
        if (!TruckSectionMode(file, TruckSection(kind, ""), kindMode)) TruckModeSave(file, kind, mode); // the kind has none: the truck's goes up
        WritePrivateProfileStringW(TruckSection(kind, id).c_str(), L"Own", L"0", file.c_str());
    }
    else TruckModeSaveOwn(file, kind, id, mode, false);
    newMode = TruckModeInForce(TruckModeFind(file, kind, id), modeCount);
    return newMode != mode;
}

// The take-over (Gameplay's TruckSuspTakeOver at 88e1ba7): the game made the driven truck anew under another id. The
// old id's section is copied to the new id when the new one has none of its own (one it has means another truck of
// the kind, which keeps its mode). The old section stays, marked TakenBy=<new id> and TakenAt=<when, in 100 ns since
// 1601>; the mark goes when the old id is driven again (TruckModeSeen), and only the newest kTruckTakenKept marked
// sections of the file are kept (TruckModePrune); an unmarked section is never dropped. Key by key through the
// profile API. True when a mode was taken over.
static const int kTruckTakenKept = 10;
inline int TruckModePrune(const std::wstring &file);
inline bool TruckModeTakeOver(const std::wstring &file, const char *kind, const char *oldId, const char *newId, unsigned long long at = 0)
{
    if (!kind[0] || !oldId[0] || !newId[0] || !strcmp(oldId, newId)) return false;
    const std::wstring from = TruckSection(kind, oldId), to = TruckSection(kind, newId);
    std::vector<wchar_t> keys(4096, L'\0'), mine(64, L'\0');
    if (!GetPrivateProfileSectionW(from.c_str(), keys.data(), (DWORD)keys.size(), file.c_str())) return false;
    if (GetPrivateProfileSectionW(to.c_str(), mine.data(), (DWORD)mine.size(), file.c_str())) return false;
    bool any = false;
    for (const wchar_t *k = keys.data(); *k; k += wcslen(k) + 1)
    {
        const wchar_t *eq = wcschr(k, L'=');
        if (!eq) continue;
        const std::wstring name(k, eq - k);
        if (!_wcsicmp(name.c_str(), L"TakenBy") || !_wcsicmp(name.c_str(), L"TakenAt")) continue; // the new section is the truck's own, unmarked
        any = WritePrivateProfileStringW(to.c_str(), name.c_str(), eq + 1, file.c_str()) || any;
    }
    if (!any) return false;
    if (!at)
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        at = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    }
    wchar_t when[32];
    _snwprintf_s(when, _TRUNCATE, L"%llu", at);
    WritePrivateProfileStringW(from.c_str(), L"TakenBy", TruckKindWide(newId).c_str(), file.c_str());
    WritePrivateProfileStringW(from.c_str(), L"TakenAt", when, file.c_str());
    TruckModePrune(file);
    return true;
}

// An id driven again: its section's take-over mark goes (it is a truck in use, not a left-over).
inline void TruckModeSeen(const std::wstring &file, const char *kind, const char *id)
{
    if (!kind[0] || !id[0]) return;
    const std::wstring sec = TruckSection(kind, id);
    wchar_t by[48] = {};
    if (!GetPrivateProfileStringW(sec.c_str(), L"TakenBy", L"", by, 48, file.c_str())) return;
    WritePrivateProfileStringW(sec.c_str(), L"TakenBy", nullptr, file.c_str());
    WritePrivateProfileStringW(sec.c_str(), L"TakenAt", nullptr, file.c_str());
}

// The marked sections beyond the newest kTruckTakenKept go out of the file (one count for the whole file, as
// Gameplay's per file). The count dropped.
inline int TruckModePrune(const std::wstring &file)
{
    std::vector<wchar_t> names(65536, L'\0');
    if (!GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), file.c_str())) return 0;
    std::vector<std::pair<unsigned long long, std::wstring>> marked;
    for (const wchar_t *n = names.data(); *n; n += wcslen(n) + 1)
    {
        if (!wcschr(n, L':')) continue;
        wchar_t at[32] = {};
        if (!GetPrivateProfileStringW(n, L"TakenAt", L"", at, 32, file.c_str())) continue;
        marked.push_back({ _wcstoui64(at, nullptr, 10), n });
    }
    if ((int)marked.size() <= kTruckTakenKept) return 0;
    std::sort(marked.begin(), marked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    int dropped = 0;
    for (size_t i = kTruckTakenKept; i < marked.size(); i++, dropped++) WritePrivateProfileStringW(marked[i].second.c_str(), nullptr, nullptr, file.c_str());
    return dropped;
}

// The same kind under a new id while driven (no "none" between) is a take-over only on the vehicle the old id was
// taken with; a new id on another vehicle is another truck of the kind (Gameplay's SuspAgainTakeOver at 88e1ba7).
inline bool TruckAgainTakeOver(const char *oldId, uint64_t oldVehicle, const char *newId, uint64_t newVehicle)
{
    return oldId[0] && newId[0] && strcmp(oldId, newId) && oldVehicle && oldVehicle == newVehicle;
}

static const ULONGLONG kTruckTakeOverMs = 20000; // the take-over's window: the same kind back under a new id within 20 s

// The take-over's watch (Gameplay's SuspTrack): the truck that went last with none driven since, when, and the vehicle
// object its id was last read on. At each change of the driven truck (or to none), TruckTrack says whether the truck
// now driven takes over the one that went: the same kind under another id, no other truck driven between (any truck
// driven clears gone), and either within 20 s (a recovery builds a new vehicle) or on the same vehicle object however
// long the gap (a garage visit with an upgrade took 21 s, the drive of 2026-11-06 22:37). This mod reads no loading
// screen, so a load between does not void the same-vehicle rule as it does in Gameplay. wasVehicle and nowVehicle:
// the vehicle each id was read on. oldId gets the id that went; byVehicle, where given, says the same-vehicle rule
// took it (over 20 s). On another vehicle within 20 s, there(gone, goneVehicle) asks whether the old truck is still in
// the game (TruckStillThere): a switch to another truck of the kind through the map, which is no take-over
// (BRIEF_takeover_other_truck.md).
struct TruckTracker { TruckWho2 gone; ULONGLONG goneAt; uint64_t goneVehicle; };
template <class There>
inline bool TruckTrackWith(TruckTracker &t, const TruckWho2 &was, const TruckWho2 &now, ULONGLONG at, char (&oldId)[40], uint64_t wasVehicle, uint64_t nowVehicle,
                           bool *byVehicle, There there)
{
    oldId[0] = 0;
    if (byVehicle) *byVehicle = false;
    if (was.kind[0] && !now.kind[0])
    {
        t.gone = was;
        t.goneAt = at;
        t.goneVehicle = wasVehicle;
        return false;
    }
    if (!now.kind[0]) return false;
    const bool kind = t.gone.kind[0] && !strcmp(t.gone.kind, now.kind) && t.gone.id[0] && now.id[0] && strcmp(t.gone.id, now.id);
    const bool soon = at - t.goneAt <= kTruckTakeOverMs;
    const bool sameVehicle = t.goneVehicle && t.goneVehicle == nowVehicle;
    const bool due = kind && (sameVehicle || (soon && !there(t.gone, t.goneVehicle)));
    if (due) strncpy_s(oldId, t.gone.id, _TRUNCATE);
    if (due && byVehicle) *byVehicle = !soon;
    t = TruckTracker{};
    return due;
}
// As TruckTrackWith with the old truck never there (the tests of the rules before the check).
inline bool TruckTrack(TruckTracker &t, const TruckWho2 &was, const TruckWho2 &now, ULONGLONG at, char (&oldId)[40], uint64_t wasVehicle = 0,
                       uint64_t nowVehicle = 0, bool *byVehicle = nullptr)
{
    return TruckTrackWith(t, was, now, at, oldId, wasVehicle, nowVehicle, byVehicle, [](const TruckWho2 &, uint64_t) { return false; });
}

// The driven truck from pass to pass: the one read last (kind "" = none driven) and its vehicle, and the take-over's watch.
struct TruckWatch { TruckWho2 seen; uint64_t seenVehicle; TruckTracker track; };
// What one pass's change of the driven truck comes to.
struct TruckChange
{
    bool changed;      // another truck than on the last pass (another kind, or another id), or none
    bool takeOver;     // the game made the truck that went anew under now's id: now takes oldId's mode over
    bool whileDriven;  // that take-over is of a new id on the same vehicle while driven (else: back within 20 s or on the same vehicle)
    bool byVehicle;    // a take-over back after more than 20 s, on the same vehicle object
    char oldId[40];
    ULONGLONG goneAt;  // when the truck that went was last read (for the log)
};

// One pass: now is the truck read on vehicle (kind "" when none is driven). An id not read on this pass leaves the last
// one of the same vehicle and kind (as Gameplay's TruckWhoStep). The same kind under another id with no pass of none
// between is a take-over on the same vehicle only, a first read of the id none (Gameplay's SuspAgainTakeOver); else
// the watch decides (TruckTrackWith, there as there).
template <class There> inline TruckChange TruckWatchStepWith(TruckWatch &w, TruckWho2 now, uint64_t vehicle, ULONGLONG at, There there)
{
    TruckChange c = {};
    if (now.kind[0] && !now.id[0] && vehicle == w.seenVehicle && !strcmp(w.seen.kind, now.kind)) strncpy_s(now.id, w.seen.id, _TRUNCATE);
    if (TruckWhoSame(now, w.seen)) return c;
    c.changed = true;
    c.goneAt = w.track.goneAt;
    if (w.seen.kind[0] && now.kind[0] && !strcmp(w.seen.kind, now.kind))
    {
        c.whileDriven = c.takeOver = TruckAgainTakeOver(w.seen.id, w.seenVehicle, now.id, vehicle);
        if (c.takeOver) strncpy_s(c.oldId, w.seen.id, _TRUNCATE);
    }
    else c.takeOver = TruckTrackWith(w.track, w.seen, now, at, c.oldId, w.seenVehicle, vehicle, &c.byVehicle, there);
    w.seen = now;
    w.seenVehicle = now.kind[0] ? vehicle : 0;
    return c;
}
inline TruckChange TruckWatchStep(TruckWatch &w, TruckWho2 now, uint64_t vehicle, ULONGLONG at)
{
    return TruckWatchStepWith(w, now, vehicle, at, [](const TruckWho2 &, uint64_t) { return false; });
}

// The mode in force for what f found: the start mode is the highest on offer (Increased where it is on, else Normal),
// and a saved Increased with Increased off goes Normal. modeCount: the modes in use (3 or 4).
inline int TruckModeInForce(const TruckModeFound &f, int modeCount)
{
    const int m = f.level == kTruckLevelStart ? modeCount - 1 : f.mode;
    return m >= modeCount || m < 0 ? kNormal : m;
}

// A change of the driven truck to now (c from TruckWatchStep) while the mode in force goes by cur. With PerTruck on and
// now another truck than cur: the take-over where it is due (taken is true when a mode was taken over), the take-over
// mark of now's id goes, cur becomes now and f what TruckModeFind found for it; true then. PerTruck off: one mode for
// all, cur follows the truck and nothing is read or written; false. firstShared: PerTruck off and this is the first
// truck of the game start (cur was none), which then starts in the start mode, the highest on offer (the review's N1).
inline bool TruckModeOnChange(const std::wstring &file, bool perTruck, TruckWho2 &cur, const TruckWho2 &now, const TruckChange &c, TruckModeFound &f, bool &taken,
                              bool &firstShared)
{
    taken = firstShared = false;
    if (!c.changed || !now.kind[0]) return false;
    if (!perTruck)
    {
        firstShared = !cur.kind[0];
        cur = now;
        return false;
    }
    if (TruckWhoSame(now, cur)) return false;
    taken = c.takeOver && TruckModeTakeOver(file, now.kind, c.oldId, now.id);
    TruckModeSeen(file, now.kind, now.id);
    cur = now;
    f = TruckModeFind(file, cur.kind, cur.id);
    return true;
}
