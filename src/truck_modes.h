// truck_modes.h: each truck's pressure mode (PerTruck, since 1.3.1) in TirePressure.trucks.ini next to the exe.
// [<kind>] holds the mode of every truck of a kind, by the kind's name as the game's files name it ("western_star_6900xd";
// "zetros/176593" for a truck from a mod). Since 1.3.4 [<kind>:<id>] holds one owned truck's, by the game's own id for
// it ("{8E42BF0A-F7C6-40f9-B0F2-225DE74CE031}"), with Own=1 while it is in force (Own=0: kept, not in use; a section
// without Own is in force). The mode in force: the truck's own, else the kind's, else the start mode (the highest
// mode on offer). tire_pressure.cpp reads the kind and the id from the game (TruckKindName, TruckIdRead) and calls these.
// The id, its shape, the take-over of a truck the game made anew under another id, [Seen] and the warehouse match are
// truck_id.h's, the same file as Next Gen Gameplay's (BRIEF_truck_id_part2.md), so both mods name and follow a truck
// alike. Tire Inflation runs without Gameplay: the file is compiled into each mod, not a call into the other.
#pragma once
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include "panel.h"
#include "truck_id.h"

static const wchar_t *const kTruckModesFile = L"TirePressure.trucks.ini";
static const wchar_t *const kTruckModeNames[kModeCount] = { L"Low", L"Reduced", L"Normal", L"Increased" }; // PressureMode

// The id, the kind, the take-over check and the watch are truck_id.h's, the same file as Next Gen Gameplay's
// (BRIEF_truck_id_part2.md): these names are Tire Inflation's for them, as before.
inline void TruckKindClean(char *name) { truckid::KindClean(name); }
inline std::wstring TruckKindWide(const char *kind) { return truckid::Wide(kind); }
inline bool TruckIdText(const char *text, size_t length) { return truckid::IdText(text, length); }
template <class Peek> inline void TruckIdOf(Peek peek, uint64_t vehicle, char (&id)[40]) { truckid::IdOf(peek, vehicle, id); }
template <class Peek> inline bool TruckKindOf(Peek peek, uint64_t vehicle, char (&out)[96]) { return truckid::KindOf(peek, vehicle, out); }
template <class Peek> inline bool TruckChassisWorld(Peek peek, uint64_t vehicle, uint64_t &body, uint64_t &vtable, float &inverseMass, uint64_t &world)
{
    return truckid::ChassisWorld(peek, vehicle, body, vtable, inverseMass, world);
}
typedef truckid::There TruckThere;
template <class Peek> inline bool TruckStillThere(Peek peek, uint64_t oldVehicle, const char *oldKind, const char *oldId, uint64_t newVehicle, TruckThere &o)
{
    return truckid::StillThere(peek, oldVehicle, oldKind, oldId, newVehicle, o);
}
template <class Peek> inline bool TruckWheelWorld(Peek peek, uint64_t vehicle, uint64_t &body, uint64_t &world) { return truckid::WheelWorld(peek, vehicle, body, world); }

// Which truck the mode goes by: its kind's name and the game's id for it ("" = not read). kind "" = none driven.
typedef truckid::Who TruckWho2;
inline bool TruckWhoSame(const TruckWho2 &a, const TruckWho2 &b) { return truckid::Same(a, b); }

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

// A missing file is made first with this note at its head on what it is. The making is truck_id.h's (FileMake, behind
// its Put), so the file's first write gets it whichever it is: [Seen] at the first drive comes before any mode.
static const wchar_t kTruckModesNote[] =
    L"; Each truck's pressure mode: [kind] for every truck of a kind, [kind:{id}] for one owned truck by the game's id\n"
    L"; for it (Own=1 in force, Own=0 kept). It is written when a mode is chosen and put back when that truck is\n"
    L"; driven again. TakenBy and TakenAt mark a truck the game made anew under another id (How=warehouse: matched\n"
    L"; out of the warehouse by Tire=). [Seen] lists every truck driven. PerTruck=0 in TirePressure.ini turns this off.\n";
inline const bool kTruckModesNoteSet = (truckid::FileNote() = kTruckModesNote, true);

// The mode for kind, into the kind's section. Key by key through the profile API.
inline void TruckModeSave(const std::wstring &file, const char *kind, int mode)
{
    if (mode < 0 || mode >= kModeCount) return;
    truckid::Put(file, TruckSection(kind, ""), L"Mode", kTruckModeNames[mode]);
}

// The mode for one owned truck, into its own section with Own (1 = in force, 0 = kept).
inline void TruckModeSaveOwn(const std::wstring &file, const char *kind, const char *id, int mode, bool own)
{
    if (mode < 0 || mode >= kModeCount || !id || !id[0]) return;
    const std::wstring sec = TruckSection(kind, id);
    truckid::Put(file, sec, L"Own", own ? L"1" : L"0");
    truckid::Put(file, sec, L"Mode", kTruckModeNames[mode]);
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

// The look every 2 s at the driven truck's tire (truck_id.h parts). written holds what was last seen: the tire, then
// "|s" while the truck has its own section, as Gauges' look does. A change of either writes Tire= into that section,
// so a section made while driving (a mode chosen, TruckModeChosen) gets it too (the review's M1). True when written.
inline std::string TruckTireSeen(const std::wstring &file, const char *kind, const char *id, const std::string &tire)
{
    if (tire.empty()) return std::string();
    const bool there = id && id[0] && truckid::SectionThere(file, TruckSection(kind, id));
    return there ? tire + "|s" : tire;
}
inline bool TruckTireLook(const std::wstring &file, const char *kind, const char *id, const std::string &tire, std::string &written)
{
    const std::string seen = TruckTireSeen(file, kind, id, tire);
    if (seen.empty() || seen == written) return false;
    written = seen;
    if (seen.size() < 2 || seen.compare(seen.size() - 2, 2, "|s")) return false; // no section of its own yet
    truckid::Parts parts = { { L"Tire", truckid::Wide(tire.c_str()) } };
    return truckid::PartsPut(file, TruckSection(kind, id), parts);
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

// The take-over (truck_id.h TakeOver): the game made the driven truck anew under another id. The old id's section is
// copied to the new id when the new one has none of its own (one it has means another truck of the kind, which keeps
// its mode). The old section stays, marked TakenBy and TakenAt; the mark goes when the old id is driven again
// (TruckMarkClear), and only the newest kTruckTakenKept marked sections of the file are kept (TruckModePrune), never one
// a warehouse match marked. True when a mode was taken over.
static const int kTruckTakenKept = truckid::kTakenKept;
inline int TruckModePrune(const std::wstring &file) { return truckid::Prune(file, L""); }
inline bool TruckModeTakeOver(const std::wstring &file, const char *kind, const char *oldId, const char *newId, unsigned long long at = 0)
{
    if (!kind[0] || !oldId[0] || !newId[0] || !strcmp(oldId, newId)) return false;
    return truckid::TakeOver(file, TruckSection(kind, oldId), TruckSection(kind, newId), newId, at, nullptr, L"");
}

// An id driven again: its section's take-over mark goes (it is a truck in use, not a left-over).
inline void TruckMarkClear(const std::wstring &file, const char *kind, const char *id)
{
    if (kind[0] && id[0]) truckid::MarkClear(file, TruckSection(kind, id));
}

// The same kind under a new id while driven is a take-over only on the vehicle the old id was taken with.
inline bool TruckAgainTakeOver(const char *oldId, uint64_t oldVehicle, const char *newId, uint64_t newVehicle)
{
    return truckid::AgainTakeOver(oldId, oldVehicle, newId, newVehicle);
}

static const ULONGLONG kTruckTakeOverMs = truckid::kTakeOverMs;

// The take-over's watch (truck_id.h TrackWith). This mod reads no loading screen, so its loads are always 0 and a load
// between does not void the same-vehicle rule as it does in Gameplay.
typedef truckid::Tracker TruckTracker;
template <class There>
inline bool TruckTrackWith(TruckTracker &t, const TruckWho2 &was, const TruckWho2 &now, ULONGLONG at, char (&oldId)[40], uint64_t wasVehicle, uint64_t nowVehicle,
                           bool *byVehicle, There there)
{
    return truckid::TrackWith(t, was, now, at, oldId, wasVehicle, nowVehicle, 0, byVehicle, there);
}
// As TruckTrackWith with the old truck never there (the tests of the rules before the check).
inline bool TruckTrack(TruckTracker &t, const TruckWho2 &was, const TruckWho2 &now, ULONGLONG at, char (&oldId)[40], uint64_t wasVehicle = 0,
                       uint64_t nowVehicle = 0, bool *byVehicle = nullptr)
{
    return TruckTrackWith(t, was, now, at, oldId, wasVehicle, nowVehicle, byVehicle, [](const TruckWho2 &, uint64_t) { return false; });
}

// The driven truck from pass to pass (truck_id.h WatchStepWith).
typedef truckid::Watch TruckWatch;
typedef truckid::Change TruckChange;
template <class There> inline TruckChange TruckWatchStepWith(TruckWatch &w, TruckWho2 now, uint64_t vehicle, ULONGLONG at, There there)
{
    return truckid::WatchStepWith(w, now, vehicle, at, 0, there);
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
//
// Then, for a truck new to the mod (its id has no section, is no take-over and is not in [Seen]), the warehouse match
// (truck_id.h MatchFind): exactly one waiting truck of the kind with the same tire (Tire=, the first wheel's tire mesh)
// gives its mode, copied and marked How=warehouse. Then the id's [Seen] line, once a start, and its Tire= where it has
// a section. parts: Tire= now ("" not read: no match, nothing written); start: this start's when; drivenGone(id): an id
// driven this start counts as waiting (this mod reads no loading screen, so it passes none). note: what came of it for
// the drive's line.
struct TruckIdNote
{
    int line;                  // truckid::kLine...
    unsigned long long lastSeen; // the id's [Seen] line before this start (0 none)
    truckid::Match match;      // the match, where one was looked for (looked)
    bool looked;
    bool matched;
};
template <class Gone>
inline bool TruckModeOnChangeWith(const std::wstring &file, bool perTruck, TruckWho2 &cur, const TruckWho2 &now, const TruckChange &c, TruckModeFound &f, bool &taken,
                                  bool &firstShared, const std::string &tire, unsigned long long start, Gone drivenGone, TruckIdNote &note)
{
    taken = firstShared = false;
    note = TruckIdNote{};
    if (!c.changed || !now.kind[0]) return false;
    if (!perTruck)
    {
        firstShared = !cur.kind[0];
        cur = now;
        return false;
    }
    if (TruckWhoSame(now, cur)) return false;
    taken = c.takeOver && TruckModeTakeOver(file, now.kind, c.oldId, now.id);
    TruckMarkClear(file, now.kind, now.id);
    cur = now;
    f = TruckModeFind(file, cur.kind, cur.id);
    unsigned long long seen = 0;
    const bool listed = truckid::SeenGet(file, cur.id, seen);
    note.lastSeen = listed && seen < start ? seen : 0;
    truckid::Parts parts;
    if (!tire.empty()) parts.push_back({ L"Tire", truckid::Wide(tire.c_str()) });
    if (cur.id[0] && !taken && !c.takeOver && f.level != kTruckLevelOwn && !f.ownKept && !listed)
    {
        const std::vector<std::wstring> prefixes = { TruckKindWide(cur.kind) + L":" };
        note.looked = true;
        note.match = truckid::MatchFind(file, prefixes, cur.id, parts, true, start, drivenGone);
        if (note.match.why == truckid::kMatchOne && truckid::MatchTake(file, prefixes, { L"" }, note.match.id, cur.id) > 0)
        {
            note.matched = true;
            f = TruckModeFind(file, cur.kind, cur.id);
        }
    }
    if (cur.id[0] && (!listed || seen < start)) truckid::SeenPut(file, cur.id, truckid::Now());
    if (cur.id[0]) truckid::PartsPut(file, TruckSection(cur.kind, cur.id), parts);
    note.line = note.matched ? truckid::kLineMatched : taken ? truckid::kLineTaken : f.level == kTruckLevelOwn ? truckid::kLineOwn : truckid::kLineKind;
    return true;
}
inline bool TruckModeOnChange(const std::wstring &file, bool perTruck, TruckWho2 &cur, const TruckWho2 &now, const TruckChange &c, TruckModeFound &f, bool &taken,
                              bool &firstShared)
{
    TruckIdNote note;
    return TruckModeOnChangeWith(file, perTruck, cur, now, c, f, taken, firstShared, std::string(), 0, [](const char *) { return false; }, note);
}

// The drive's one line for note (truck_id.h DriveLine), into out.
inline void TruckIdLine(wchar_t *out, size_t size, const TruckWho2 &cur, const TruckChange &c, bool taken, const TruckIdNote &note, const TruckModeFound &f)
{
    const wchar_t *rule = c.whileDriven ? L"a new id on the same vehicle while driven" : c.byVehicle ? L"the same vehicle after more than 20 s" : L"back within 20 s";
    const wchar_t *why = !cur.id[0] ? L"its id not read" : f.ownKept ? L"This truck only unticked" : note.looked ? truckid::MatchWords(note.match.why) : L"";
    truckid::DriveLine(out, size, cur.kind, cur.id[0] ? cur.id : "", note.line, note.lastSeen, note.matched ? note.match.id : taken ? c.oldId : "", rule, why);
}
