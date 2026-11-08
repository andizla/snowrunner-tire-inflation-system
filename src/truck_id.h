// truck_id.h: who a truck is, for what the mods keep per owned truck (BRIEF_truck_id_part2.md). The same file in Next
// Gen Gameplay and Next Gen Tire Inflation, as crt_line.h is: each mod keeps its own storage (its file, its section
// names and what a section holds) and calls these. It includes only Windows, the C runtime and the C++ library.
//
// - The id: the game's own text in braces for one truck, behind the pointer at vehicle + 0x48
//   ("{8E42BF0A-F7C6-40f9-B0F2-225DE74CE031}"). The kind: the text at model + 0x588, the model at vehicle + 0x60.
// - The watch: the driven truck from pass to pass, and the truck that went with none driven since (Tracker).
// - The take-over, three rules: the same kind back under a new id within 20 s with no truck between (a recovery builds
//   a new vehicle object); the same vehicle object under a new id however long the gap, with no load between (a garage
//   visit took 21 s); another vehicle within 20 s only when the old truck is gone (StillThere). While driven, a new id
//   is a take-over only on the old id's vehicle (AgainTakeOver).
// - The marks: the old section is copied, never moved, and marked TakenBy=<new id> and TakenAt=<when>. Driving the old
//   id again clears the mark (MarkClear). Only the newest kTakenKept marked sections are kept (Prune), and a section a
//   warehouse match marked (How=warehouse) is never pruned. An unmarked section is never dropped.
// - Seen: [Seen] lists every driven id, <id>=<when>, written once a start at the id's first drive; the newest kSeenKept
//   are kept. A truck is new to the mod only where its id is not listed.
// - The warehouse match: a truck put in the warehouse loses its id, and taken out it gets a new one. A driven truck
//   new to the mod, with no section and no take-over, looks for a waiting truck of its kind: unmarked, with something of
//   its own, its parts recorded and the same as the new truck's, and not driven this start (or driven this start, with
//   no load since and its vehicle gone). Counted by id: exactly one takes over, marked How=warehouse. A truck bought new
//   with the same parts as one waiting is matched the same way: it gets a copy, and the old keeps its own.
// "When" is a count of 100 ns since 1601 (FILETIME), for TakenAt and Seen alike: a plain date cannot say "before this
// start" when the last start was the same day.
#pragma once
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace truckid
{
static const int kKindMax = 96;                 // a kind's name and its end
static const ULONGLONG kTakeOverMs = 20000;     // the take-over's window: the same kind back under a new id within 20 s
static const int kTakenKept = 10;               // the newest marked sections kept by Prune
static const int kSeenKept = 200;               // the newest ids kept in [Seen]
static const wchar_t *const kSeenSection = L"Seen";

// ---- the id and the kind, read through peek(address, out, size) ----

// The shape of the game's id: 38 characters, braces around hex digits in groups of 8, 4, 4, 4 and 12.
inline bool IdText(const char *text, size_t length)
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

// A kind's name made fit for a section of an ini: what would end the name there, or does not print, becomes '_'.
inline void KindClean(char *name)
{
    for (char *c = name; *c; c++)
        if ((unsigned char)*c <= 0x20 || (unsigned char)*c > 0x7E || *c == '[' || *c == ']') *c = '_';
}

template <class Peek> inline bool Read(Peek peek, uint64_t at, void *out, size_t size) { return at >= 0x10000 && peek(at, out, size); }

// The id of the truck at vehicle: "" where it is not read or has not the shape of one.
template <class Peek> inline void IdOf(Peek peek, uint64_t vehicle, char (&id)[40])
{
    id[0] = 0;
    uint64_t chars = 0;
    char text[39];
    if (Read(peek, vehicle + 0x48, &chars, sizeof chars) && Read(peek, chars, text, sizeof text) && !text[38] && IdText(text, 38)) memcpy(id, text, sizeof text);
}

// The kind's name of the truck at vehicle. A text is 0x20 bytes: its characters while it has room for fewer than 16,
// else a pointer to them; the length at + 0x10 and the room at + 0x18. False where it is not read, empty or 95 long or
// more.
template <class Peek> inline bool KindOf(Peek peek, uint64_t vehicle, char (&out)[kKindMax])
{
    out[0] = 0;
    uint64_t model = 0, chars = 0, length = 0, room = 0;
    uint8_t text[0x20];
    if (!Read(peek, vehicle + 0x60, &model, sizeof model) || !Read(peek, model + 0x588, text, sizeof text)) return false;
    memcpy(&chars, text, 8);
    memcpy(&length, text + 0x10, 8);
    memcpy(&room, text + 0x18, 8);
    if (!length || length >= kKindMax - 1 || room < length) return false;
    if (room < 16) memcpy(out, text, (size_t)length);
    else if (!Read(peek, chars, out, (size_t)length)) return false;
    out[length] = 0;
    KindClean(out);
    return out[0] != 0;
}

// A vehicle's chassis body (model + 0x230), the body's vtable, its inverse mass (+ 0x22C) and its world: hkpWorldObject's
// m_world at + 0x10 in Havok's layout. False where a read fails.
template <class Peek> inline bool ChassisWorld(Peek peek, uint64_t vehicle, uint64_t &body, uint64_t &vtable, float &inverseMass, uint64_t &world)
{
    uint64_t model = 0;
    body = vtable = world = 0;
    inverseMass = 0.0f;
    return Read(peek, vehicle + 0x60, &model, sizeof model) && Read(peek, model + 0x230, &body, sizeof body) && Read(peek, body, &vtable, sizeof vtable) &&
           Read(peek, body + 0x22C, &inverseMass, sizeof inverseMass) && Read(peek, body + 0x10, &world, sizeof world);
}

// The first wheel's body of the truck at vehicle and its world, for the once-a-truck line: the wheel models from
// [vehicle + 0x200], the wheel at wheel model + 0x2C8, its body at wheel + 0x18, m_world at body + 0x10.
template <class Peek> inline bool WheelWorld(Peek peek, uint64_t vehicle, uint64_t &body, uint64_t &world)
{
    uint64_t begin = 0, wheelModel = 0, wheel = 0;
    body = world = 0;
    return Read(peek, vehicle + 0x200, &begin, sizeof begin) && Read(peek, begin, &wheelModel, sizeof wheelModel) && Read(peek, wheelModel + 0x2C8, &wheel, sizeof wheel) &&
           Read(peek, wheel + 0x18, &body, sizeof body) && Read(peek, body + 0x10, &world, sizeof world);
}

// The take-over's check on another vehicle within 20 s (BRIEF_takeover_other_truck.md): whether the truck that went is
// still in the game. A switch to another truck of the kind through the map goes through "none driven" as a rebuild
// does, and a freed vehicle block can still read its old id. So the old truck counts as there only when all hold: its
// vehicle reads the old id and the old kind; its chassis body has the new truck's chassis's vtable and the inverse mass
// of 10 kg to 1000 t; and that body's world is not 0 and is the new chassis's. A destroyed truck's bodies leave the
// world before they are freed; a truck left through the map stays in it. Anything else is gone.
struct There
{
    char id[40];                              // what the old vehicle reads now ("" not read)
    char kind[kKindMax];
    uint64_t body, world, newBody, newWorld;  // the old and the new chassis body and their worlds (0 not read)
    bool there;
};
template <class Peek> inline bool StillThere(Peek peek, uint64_t oldVehicle, const char *oldKind, const char *oldId, uint64_t newVehicle, There &o)
{
    o = There{};
    IdOf(peek, oldVehicle, o.id);
    KindOf(peek, oldVehicle, o.kind);
    uint64_t vtable = 0, newVtable = 0;
    float inverse = 0.0f, newInverse = 0.0f;
    const bool body = ChassisWorld(peek, oldVehicle, o.body, vtable, inverse, o.world);
    const bool newBody = ChassisWorld(peek, newVehicle, o.newBody, newVtable, newInverse, o.newWorld);
    const float mass = inverse > 0.0f ? 1.0f / inverse : 0.0f;
    o.there = oldId[0] && !strcmp(o.id, oldId) && oldKind[0] && !strcmp(o.kind, oldKind) && body && newBody && vtable && vtable == newVtable && o.body != o.newBody &&
              isfinite(mass) && mass >= 10.0f && mass <= 1.0e6f && o.world && o.world == o.newWorld;
    return o.there;
}

// ---- the watch ----

// Which truck: its kind's name and its id ("" = not read). kind "" = none driven. unnamed: the kind is not the name
// (Gameplay's build key), which never matches out of the warehouse and is never "there".
struct Who
{
    char kind[kKindMax];
    char id[40];
    bool unnamed;
};
inline bool Same(const Who &a, const Who &b) { return !strcmp(a.kind, b.kind) && !strcmp(a.id, b.id); }
inline Who WhoOf(const char *kind, const char *id, bool named = true)
{
    Who w = {};
    strncpy_s(w.kind, kind ? kind : "", _TRUNCATE);
    strncpy_s(w.id, id ? id : "", _TRUNCATE);
    w.unnamed = !named;
    return w;
}

// The same kind under a new id while driven (no "none" between) is a take-over only on the vehicle the old id was
// taken with; a new id on another vehicle is another truck of the kind.
inline bool AgainTakeOver(const char *oldId, uint64_t oldVehicle, const char *newId, uint64_t newVehicle)
{
    return oldId[0] && newId[0] && strcmp(oldId, newId) && oldVehicle && oldVehicle == newVehicle;
}

// The truck that went last with none driven since, when, the vehicle object its id was last read on, and how many
// loading screens had begun then. At each change of the driven truck's kind (or to none), TrackWith says whether the
// truck now driven takes over the one that went: the same kind under another id, no other truck driven between (any
// truck driven clears gone), and either within 20 s or on the same vehicle object with no load between, however long
// the gap. loads: the loading screens begun so far (0 always where a mod reads none). oldId gets the id that went;
// byVehicle, where given, says the same-vehicle rule took it (over 20 s). On another vehicle within 20 s,
// there(gone, goneVehicle) asks whether the old truck is still in the game (StillThere).
struct Tracker
{
    Who gone;
    ULONGLONG goneAt;
    uint64_t goneVehicle;
    unsigned goneLoads;
};
template <class Th>
inline bool TrackWith(Tracker &t, const Who &was, const Who &now, ULONGLONG at, char (&oldId)[40], uint64_t wasVehicle, uint64_t nowVehicle, unsigned loads,
                      bool *byVehicle, Th there)
{
    oldId[0] = 0;
    if (byVehicle) *byVehicle = false;
    if (was.kind[0] && !now.kind[0])
    {
        t.gone = was;
        t.goneAt = at;
        t.goneVehicle = wasVehicle;
        t.goneLoads = loads;
        return false;
    }
    if (!now.kind[0]) return false;
    const bool kind = t.gone.kind[0] && !strcmp(t.gone.kind, now.kind) && t.gone.id[0] && now.id[0] && strcmp(t.gone.id, now.id);
    const bool soon = at - t.goneAt <= kTakeOverMs;
    const bool sameVehicle = t.goneVehicle && t.goneVehicle == nowVehicle && t.goneLoads == loads;
    const bool otherVehicle = t.goneVehicle != nowVehicle;
    const bool due = kind && (sameVehicle || (soon && !(otherVehicle && there(t.gone, t.goneVehicle))));
    if (due) strncpy_s(oldId, t.gone.id, _TRUNCATE);
    if (due && byVehicle) *byVehicle = !soon;
    t = Tracker{};
    return due;
}

// The driven truck from pass to pass: the one read last (kind "" = none driven) and its vehicle, and the take-over's watch.
struct Watch
{
    Who seen;
    uint64_t seenVehicle;
    Tracker track;
};
// What one pass's change of the driven truck comes to.
struct Change
{
    bool changed;      // another truck than on the last pass (another kind, or another id), or none
    bool takeOver;     // the game made the truck that went anew under now's id: now takes oldId's set over
    bool whileDriven;  // that take-over is of a new id on the same vehicle while driven
    bool byVehicle;    // a take-over back after more than 20 s, on the same vehicle object
    char oldId[40];
    ULONGLONG goneAt;  // when the truck that went was last read (for the log)
};
// One pass: now is the truck read on vehicle (kind "" when none is driven). An id not read on this pass leaves the last
// one of the same vehicle and kind. The same kind under another id with no pass of none between is a take-over on the
// same vehicle only; else the tracker decides (TrackWith).
template <class Th> inline Change WatchStepWith(Watch &w, Who now, uint64_t vehicle, ULONGLONG at, unsigned loads, Th there)
{
    Change c = {};
    if (now.kind[0] && !now.id[0] && vehicle == w.seenVehicle && !strcmp(w.seen.kind, now.kind)) strncpy_s(now.id, w.seen.id, _TRUNCATE);
    if (Same(now, w.seen)) return c;
    c.changed = true;
    c.goneAt = w.track.goneAt;
    if (w.seen.kind[0] && now.kind[0] && !strcmp(w.seen.kind, now.kind))
    {
        c.whileDriven = c.takeOver = AgainTakeOver(w.seen.id, w.seenVehicle, now.id, vehicle);
        if (c.takeOver) strncpy_s(c.oldId, w.seen.id, _TRUNCATE);
    }
    else c.takeOver = TrackWith(w.track, w.seen, now, at, c.oldId, w.seenVehicle, vehicle, loads, &c.byVehicle, there);
    w.seen = now;
    w.seenVehicle = now.kind[0] ? vehicle : 0;
    return c;
}

// ---- the files: marks, the prune, Seen, the parts and the warehouse match ----

inline unsigned long long Now()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

inline std::wstring Wide(const char *s)
{
    std::wstring w;
    while (s && *s) w += (wchar_t)(unsigned char)*s++;
    return w;
}

// A when as a local date and time, "2026-11-08 20:31"; "?" for 0.
inline void DateText(unsigned long long when, wchar_t *out, size_t size)
{
    FILETIME ft = { (DWORD)when, (DWORD)(when >> 32) }, local;
    SYSTEMTIME st;
    if (!when || !FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st))
    {
        _snwprintf_s(out, size, _TRUNCATE, L"?");
        return;
    }
    _snwprintf_s(out, size, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

// An id's first 8 hex digits, after the brace ("8E42BF0A"); the id as it is where it is shorter.
inline std::string Short(const char *id)
{
    if (!id || strlen(id) < 9 || id[0] != '{') return id ? std::string(id) : std::string();
    return std::string(id + 1, 8);
}

inline bool KeyIs(const std::wstring &name, const wchar_t *what) { return !_wcsicmp(name.c_str(), what); }
inline bool MarkKey(const std::wstring &name) { return KeyIs(name, L"TakenBy") || KeyIs(name, L"TakenAt") || KeyIs(name, L"How"); }

// A section's keys as name and value; empty where there is no such section.
inline std::vector<std::pair<std::wstring, std::wstring>> Keys(const std::wstring &file, const std::wstring &section)
{
    std::vector<std::pair<std::wstring, std::wstring>> out;
    std::vector<wchar_t> buf(32768, L'\0');
    if (!GetPrivateProfileSectionW(section.c_str(), buf.data(), (DWORD)buf.size(), file.c_str())) return out;
    for (const wchar_t *k = buf.data(); *k; k += wcslen(k) + 1)
    {
        const wchar_t *eq = wcschr(k, L'=');
        if (eq) out.push_back({ std::wstring(k, eq - k), std::wstring(eq + 1) });
    }
    return out;
}
inline bool SectionThere(const std::wstring &file, const std::wstring &section)
{
    wchar_t buf[64] = {};
    return GetPrivateProfileSectionW(section.c_str(), buf, 64, file.c_str()) > 0;
}

// The marked sections whose names begin with prefix and hold a ':' beyond the newest kTakenKept go out of the file. A
// section a warehouse match marked (How=warehouse) is passed over: a match is a guess, and its truck may come back.
// The count dropped.
inline int Prune(const std::wstring &file, const std::wstring &prefix)
{
    std::vector<wchar_t> names(65536, L'\0');
    if (!GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), file.c_str())) return 0;
    std::vector<std::pair<unsigned long long, std::wstring>> marked;
    for (const wchar_t *n = names.data(); *n; n += wcslen(n) + 1)
    {
        if (_wcsnicmp(n, prefix.c_str(), prefix.size()) || !wcschr(n, L':')) continue;
        wchar_t at[32] = {}, how[16] = {};
        if (!GetPrivateProfileStringW(n, L"TakenAt", L"", at, 32, file.c_str())) continue;
        GetPrivateProfileStringW(n, L"How", L"", how, 16, file.c_str());
        if (!_wcsicmp(how, L"warehouse")) continue;
        marked.push_back({ _wcstoui64(at, nullptr, 10), n });
    }
    if ((int)marked.size() <= kTakenKept) return 0;
    std::sort(marked.begin(), marked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    int dropped = 0;
    for (size_t i = kTakenKept; i < marked.size(); i++, dropped++) WritePrivateProfileStringW(marked[i].second.c_str(), nullptr, nullptr, file.c_str());
    return dropped;
}

// The take-over of a section: from is copied to to when to has none of its own (one it has means another truck of the
// kind, which keeps its set) and from holds more than marks. from stays, marked TakenBy=<newId> and TakenAt=<at, now
// where 0>, and How=<how> where how is given ("warehouse" for a match). The new section is the truck's own, unmarked.
// Then Prune(file, prunePrefix). True when a section was taken over.
inline bool TakeOver(const std::wstring &file, const std::wstring &from, const std::wstring &to, const char *newId, unsigned long long at, const wchar_t *how,
                     const std::wstring &prunePrefix)
{
    if (from == to || !newId || !newId[0]) return false;
    if (SectionThere(file, to)) return false;
    bool any = false;
    for (const auto &k : Keys(file, from)) any = any || !MarkKey(k.first);
    if (!any) return false;
    std::vector<wchar_t> buf(32768, L'\0');
    if (!GetPrivateProfileSectionW(from.c_str(), buf.data(), (DWORD)buf.size(), file.c_str())) return false;
    if (!WritePrivateProfileSectionW(to.c_str(), buf.data(), file.c_str())) return false;
    WritePrivateProfileStringW(to.c_str(), L"TakenBy", nullptr, file.c_str());
    WritePrivateProfileStringW(to.c_str(), L"TakenAt", nullptr, file.c_str());
    WritePrivateProfileStringW(to.c_str(), L"How", nullptr, file.c_str());
    if (!at) at = Now();
    wchar_t when[32];
    _snwprintf_s(when, _TRUNCATE, L"%llu", at);
    WritePrivateProfileStringW(from.c_str(), L"TakenBy", Wide(newId).c_str(), file.c_str());
    WritePrivateProfileStringW(from.c_str(), L"TakenAt", when, file.c_str());
    WritePrivateProfileStringW(from.c_str(), L"How", how && how[0] ? how : nullptr, file.c_str());
    Prune(file, prunePrefix);
    return true;
}

// An id driven again: its section's take-over mark goes (it is a truck in use, not a left-over).
inline void MarkClear(const std::wstring &file, const std::wstring &section)
{
    wchar_t by[48] = {};
    if (!GetPrivateProfileStringW(section.c_str(), L"TakenBy", L"", by, 48, file.c_str())) return;
    WritePrivateProfileStringW(section.c_str(), L"TakenBy", nullptr, file.c_str());
    WritePrivateProfileStringW(section.c_str(), L"TakenAt", nullptr, file.c_str());
    WritePrivateProfileStringW(section.c_str(), L"How", nullptr, file.c_str());
}

// An id's line in [Seen]: true and when where it is listed.
inline bool SeenGet(const std::wstring &file, const char *id, unsigned long long &when)
{
    when = 0;
    if (!id || !id[0]) return false;
    wchar_t at[32] = {};
    if (!GetPrivateProfileStringW(kSeenSection, Wide(id).c_str(), L"", at, 32, file.c_str())) return false;
    when = _wcstoui64(at, nullptr, 10);
    return true;
}

// An id listed in [Seen] at when; the newest kSeenKept are kept. [Seen] makes no truck's section.
inline void SeenPut(const std::wstring &file, const char *id, unsigned long long when)
{
    if (!id || !id[0]) return;
    wchar_t at[32];
    _snwprintf_s(at, _TRUNCATE, L"%llu", when);
    WritePrivateProfileStringW(kSeenSection, Wide(id).c_str(), at, file.c_str());
    auto all = Keys(file, kSeenSection);
    if ((int)all.size() <= kSeenKept) return;
    std::sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return _wcstoui64(a.second.c_str(), nullptr, 10) > _wcstoui64(b.second.c_str(), nullptr, 10); });
    for (size_t i = kSeenKept; i < all.size(); i++) WritePrivateProfileStringW(kSeenSection, all[i].first.c_str(), nullptr, file.c_str());
}

// A truck's parts, as each mod records them (Gameplay: Engine= and Gearbox=; Tire Inflation: Tire=).
typedef std::vector<std::pair<std::wstring, std::wstring>> Parts;

// The parts written into section where it exists (never a section made for them). True when one changed.
inline bool PartsPut(const std::wstring &file, const std::wstring &section, const Parts &parts)
{
    if (parts.empty() || !SectionThere(file, section)) return false;
    bool changed = false;
    for (const auto &p : parts)
    {
        wchar_t was[256] = {};
        GetPrivateProfileStringW(section.c_str(), p.first.c_str(), L"", was, 256, file.c_str());
        if (p.second == was) continue;
        WritePrivateProfileStringW(section.c_str(), p.first.c_str(), p.second.c_str(), file.c_str());
        changed = true;
    }
    return changed;
}

// How a warehouse match came out.
enum { kMatchOne, kMatchNone, kMatchTwo, kMatchNoParts, kMatchUnnamed };
inline const wchar_t *MatchWords(int why)
{
    return why == kMatchOne     ? L"matched out of the warehouse by its parts"
           : why == kMatchTwo   ? L"two or more waiting trucks of the kind have its parts, so none is taken"
           : why == kMatchNoParts ? L"its parts are not read, so no waiting truck is matched"
           : why == kMatchUnnamed ? L"its kind's name is not read, so no waiting truck is matched"
                                  : L"no waiting truck of the kind has its parts";
}
struct Match
{
    int why;           // kMatch...
    int candidates;    // the waiting ids with its parts
    char id[40];       // the one matched (kMatchOne)
    int waiting;       // the waiting ids of the kind, whatever their parts
};

// The warehouse match for newId, whose kind's sections are named prefix + <id> for each of prefixes (Gameplay:
// "Suspension:" and "Engine:" in the kind's file; Tire Inflation: "<kind>:" in its one file). A candidate is an id other
// than newId with: no section marked; something of its own in a section (a key other than the marks and the parts);
// every section of it with the parts recorded and the same as parts; and waiting: its [Seen] line before start, or no
// line, or driven this start with drivenGone(id) true (no load since its truck was last driven, and its vehicle gone).
// Counted by id. It takes nothing over: Take does, on kMatchOne.
template <class Gone> inline Match MatchFind(const std::wstring &file, const std::vector<std::wstring> &prefixes, const char *newId, const Parts &parts, bool named,
                                             unsigned long long start, Gone drivenGone)
{
    Match m = {};
    m.why = kMatchNone;
    if (!named) { m.why = kMatchUnnamed; return m; }
    if (parts.empty()) { m.why = kMatchNoParts; return m; }
    for (const auto &p : parts)
        if (p.second.empty()) { m.why = kMatchNoParts; return m; }
    std::vector<wchar_t> names(65536, L'\0');
    if (!GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), file.c_str())) return m;
    // the ids of the kind's sections, each once
    std::vector<std::wstring> ids;
    for (const wchar_t *n = names.data(); *n; n += wcslen(n) + 1)
        for (const auto &pre : prefixes)
        {
            if (wcslen(n) <= pre.size() || _wcsnicmp(n, pre.c_str(), pre.size())) continue;
            const std::wstring id = n + pre.size();
            if (id.find(L':') != std::wstring::npos) continue;
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        }
    const std::wstring mine = Wide(newId);
    for (const auto &id : ids)
    {
        if (!_wcsicmp(id.c_str(), mine.c_str())) continue;
        bool marked = false, own = false, partsAll = true, partsSame = true, any = false;
        for (const auto &pre : prefixes)
        {
            const auto keys = Keys(file, pre + id);
            if (keys.empty()) continue;
            any = true;
            for (const auto &k : keys)
            {
                if (KeyIs(k.first, L"TakenBy")) marked = true;
                bool part = false;
                for (const auto &p : parts) part = part || KeyIs(k.first, p.first.c_str());
                own = own || (!MarkKey(k.first) && !part);
            }
            for (const auto &p : parts)
            {
                const auto it = std::find_if(keys.begin(), keys.end(), [&](const auto &k) { return KeyIs(k.first, p.first.c_str()); });
                if (it == keys.end()) partsAll = false;
                else if (it->second != p.second) partsSame = false;
            }
        }
        if (!any || marked || !own) continue;
        char idText[40] = {};
        WideCharToMultiByte(CP_ACP, 0, id.c_str(), -1, idText, sizeof idText, nullptr, nullptr);
        unsigned long long seen = 0;
        const bool listed = SeenGet(file, idText, seen);
        const bool waiting = !listed || seen < start || drivenGone(idText);
        if (!waiting) continue;
        m.waiting++;
        if (!partsAll || !partsSame) continue;
        if (++m.candidates == 1) strncpy_s(m.id, idText, _TRUNCATE);
    }
    m.why = m.candidates == 1 ? kMatchOne : m.candidates > 1 ? kMatchTwo : kMatchNone;
    if (m.why != kMatchOne) m.id[0] = 0;
    return m;
}

// The match's take-over: each section of oldId copied to newId and marked How=warehouse (never pruned). prunePrefix
// gives each prefix the prefix Prune goes by (the same, or "" for the whole file). The count of sections taken.
inline int MatchTake(const std::wstring &file, const std::vector<std::wstring> &prefixes, const std::vector<std::wstring> &prunePrefixes, const char *oldId,
                     const char *newId, unsigned long long at = 0)
{
    int taken = 0;
    for (size_t i = 0; i < prefixes.size(); i++)
        taken += TakeOver(file, prefixes[i] + Wide(oldId), prefixes[i] + Wide(newId), newId, at, L"warehouse", i < prunePrefixes.size() ? prunePrefixes[i] : prefixes[i]) ? 1 : 0;
    return taken;
}

// ---- the one log line a drive, the same words in both mods ----
enum { kLineOwn, kLineKind, kLineTaken, kLineMatched };
// "truck: <kind>, id <first 8>, ..." into out. lastSeen: the id's [Seen] line before this start (0 none); fromId: the
// id taken over or matched; rule: the take-over's rule in words; why: for kLineKind, why the kind's (a MatchWords text
// or another), "" none.
inline void DriveLine(wchar_t *out, size_t size, const char *kind, const char *id, int what, unsigned long long lastSeen, const char *fromId, const wchar_t *rule,
                      const wchar_t *why)
{
    wchar_t date[32];
    DateText(lastSeen, date, 32);
    const std::wstring k = Wide(kind), me = Wide(Short(id).c_str()), from = Wide(Short(fromId).c_str());
    if (what == kLineOwn)
        _snwprintf_s(out, size, _TRUNCATE, L"truck: %s, id %s, its own settings, %s%s", k.c_str(), me.c_str(), lastSeen ? L"last driven " : L"not listed as driven before",
                     lastSeen ? date : L"");
    else if (what == kLineTaken)
        _snwprintf_s(out, size, _TRUNCATE, L"truck: %s, id %s, taken over from %s (%s)", k.c_str(), me.c_str(), from.c_str(), rule && rule[0] ? rule : L"a take-over");
    else if (what == kLineMatched)
        _snwprintf_s(out, size, _TRUNCATE, L"truck: %s, id %s, matched out of the warehouse by its parts: the settings of %s (a truck bought new with the same parts is "
                                            L"matched the same way; the other keeps its own)",
                     k.c_str(), me.c_str(), from.c_str());
    else
        _snwprintf_s(out, size, _TRUNCATE, L"truck: %s, id %s, on the kind's settings%s%s%s", k.c_str(), me.c_str(), why && why[0] ? L" (" : L"", why ? why : L"",
                     why && why[0] ? L")" : L"");
}
} // namespace truckid
