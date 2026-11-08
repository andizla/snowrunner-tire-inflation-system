// truck_id_test.cpp: the shared truck id (src\truck_id.h, BRIEF_truck_id_part2.md), the same file in Next Gen Gameplay
// and Next Gen Tire Inflation, built by each one's build.bat. Made-up ids and vehicles, ini files beside the exe in
// out\test (truck_id_test_*.ini), made anew at each run. Each repo's own test keeps its own sections and words.
#include "truck_id.h"
#include <string>

static int g_failed = 0, g_ok = 0;
static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    ok ? g_ok++ : g_failed++;
}

static const char *const kA1 = "{AAAAAAAA-0000-0000-0000-000000000001}", *const kA2 = "{AAAAAAAA-0000-0000-0000-000000000002}",
                  *const kA3 = "{AAAAAAAA-0000-0000-0000-000000000003}", *const kB1 = "{BBBBBBBB-0000-0000-0000-000000000001}",
                  *const kNew = "{CCCCCCCC-0000-0000-0000-000000000001}";

static std::wstring g_dir;
static std::wstring Fresh(const wchar_t *name)
{
    const std::wstring f = g_dir + name;
    DeleteFileW(f.c_str());
    return f;
}
static std::wstring Get(const std::wstring &file, const std::wstring &sec, const wchar_t *key)
{
    wchar_t buf[256] = {};
    GetPrivateProfileStringW(sec.c_str(), key, L"", buf, 256, file.c_str());
    return buf;
}
static int Sections(const std::wstring &file)
{
    std::vector<wchar_t> names(65536, L'\0');
    if (!GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), file.c_str())) return 0;
    int n = 0;
    for (const wchar_t *s = names.data(); *s; s += wcslen(s) + 1) n++;
    return n;
}

static void Rules()
{
    using namespace truckid;
    const Who a1 = WhoOf("kind_a", kA1), a2 = WhoOf("kind_a", kA2), b1 = WhoOf("kind_b", kB1), none = {};
    char gone[40];
    auto never = [](const Who &, uint64_t) { return false; };
    // a recovery: none between, back within 20 s on another vehicle, the old one gone
    {
        Tracker t = {};
        const bool away = !TrackWith(t, a1, none, 1000, gone, 0x1000, 0, 0, nullptr, never);
        bool byVehicle = true;
        const bool due = TrackWith(t, none, a2, 6000, gone, 0, 0x2000, 0, &byVehicle, never) && !strcmp(gone, kA1) && !byVehicle;
        Check(away && due, "rules: back under a new id within 20 s, the old truck gone: a take-over");
    }
    // another truck driven between, too late, the same id again, no none between
    {
        Tracker t = {};
        TrackWith(t, a1, none, 0, gone, 0x1000, 0, 0, nullptr, never);
        TrackWith(t, none, b1, 2000, gone, 0, 0x3000, 0, nullptr, never);
        TrackWith(t, b1, none, 3000, gone, 0x3000, 0, 0, nullptr, never);
        const bool viaB = !TrackWith(t, none, a2, 4000, gone, 0, 0x2000, 0, nullptr, never);
        Tracker u = {};
        TrackWith(u, a1, none, 0, gone, 0x1000, 0, 0, nullptr, never);
        const bool late = !TrackWith(u, none, a2, 25000, gone, 0, 0x2000, 0, nullptr, never);
        Tracker w = {};
        TrackWith(w, a1, none, 0, gone, 0x1000, 0, 0, nullptr, never);
        const bool same = !TrackWith(w, none, a1, 3000, gone, 0, 0x1000, 0, nullptr, never);
        Check(viaB && late && same, "rules: a truck between, over 20 s on another vehicle, or the same id: none");
    }
    // the same vehicle however long the gap, with no load between; a load voids it
    {
        Tracker t = {};
        TrackWith(t, a1, none, 0, gone, 0x1000, 0, 4, nullptr, never);
        bool byVehicle = false;
        const bool long1 = TrackWith(t, none, a2, 600000, gone, 0, 0x1000, 4, &byVehicle, never) && byVehicle;
        Tracker u = {};
        TrackWith(u, a1, none, 0, gone, 0x1000, 0, 4, nullptr, never);
        const bool loaded = !TrackWith(u, none, a2, 600000, gone, 0, 0x1000, 5, nullptr, never);
        Check(long1 && loaded, "rules: the same vehicle after 600 s with no load between: a take-over by the vehicle; a load between: none");
    }
    // another vehicle within 20 s: still there, gone; the same vehicle never asks; no vehicle at all
    {
        int asked = 0;
        auto ask = [&](bool thereNow, uint64_t goneOn, uint64_t on) {
            Tracker t = {};
            TrackWith(t, a1, none, 1000, gone, goneOn, 0, 0, nullptr, never);
            return TrackWith(t, none, a2, 6000, gone, 0, on, 0, nullptr, [&](const Who &g, uint64_t gv) {
                asked++;
                return thereNow && !strcmp(g.id, kA1) && gv == goneOn;
            });
        };
        const bool thereNo = !ask(true, 0x1000, 0x2000) && asked == 1;
        const bool goneYes = ask(false, 0x1000, 0x2000) && asked == 2;
        const bool sameNoAsk = ask(true, 0x1000, 0x1000) && asked == 2;
        const bool noVehicle = ask(false, 0, 0) && asked == 2;
        Check(thereNo && goneYes && sameNoAsk && noVehicle,
              "rules: another vehicle within 20 s: still there is none, gone is a take-over; the same vehicle is not asked; no vehicle at all within 20 s: a take-over");
    }
    Check(AgainTakeOver(kA1, 0x1000, kA2, 0x1000) && !AgainTakeOver(kA1, 0x1000, kA2, 0x2000) && !AgainTakeOver(kA1, 0, kA2, 0) && !AgainTakeOver(kA1, 0x1000, kA1, 0x1000),
          "rules: a new id while driven is a take-over only on the old id's vehicle");
    // the watch: an id not read on a pass keeps the last one; a change while driven on the same vehicle
    {
        Watch w = {};
        WatchStepWith(w, a1, 0x1000, 0, 0, never);
        Who noId = WhoOf("kind_a", "");
        const Change kept = WatchStepWith(w, noId, 0x1000, 50, 0, never);
        const Change again = WatchStepWith(w, a2, 0x1000, 100, 0, never);
        Check(!kept.changed && again.changed && again.takeOver && again.whileDriven && !strcmp(again.oldId, kA1), "watch: an id not read keeps the last; a new id on the same vehicle while driven is a take-over");
    }
}

static void Marks()
{
    using namespace truckid;
    const std::wstring f = Fresh(L"truck_id_test_marks.ini");
    WritePrivateProfileStringW(L"S:{AAAAAAAA-0000-0000-0000-000000000001}", L"Mode", L"Low", f.c_str());
    const bool took = TakeOver(f, L"S:{AAAAAAAA-0000-0000-0000-000000000001}", L"S:{AAAAAAAA-0000-0000-0000-000000000002}", kA2, 123, nullptr, L"S:");
    const bool copied = Get(f, L"S:{AAAAAAAA-0000-0000-0000-000000000002}", L"Mode") == L"Low" && Get(f, L"S:{AAAAAAAA-0000-0000-0000-000000000002}", L"TakenBy").empty();
    const bool marked = Get(f, L"S:{AAAAAAAA-0000-0000-0000-000000000001}", L"TakenBy") == Wide(kA2) && Get(f, L"S:{AAAAAAAA-0000-0000-0000-000000000001}", L"TakenAt") == L"123" &&
                        Get(f, L"S:{AAAAAAAA-0000-0000-0000-000000000001}", L"Mode") == L"Low";
    WritePrivateProfileStringW(L"S:{AAAAAAAA-0000-0000-0000-000000000003}", L"Mode", L"Normal", f.c_str());
    const bool refused = !TakeOver(f, L"S:{AAAAAAAA-0000-0000-0000-000000000002}", L"S:{AAAAAAAA-0000-0000-0000-000000000003}", kA3, 0, nullptr, L"S:") &&
                         Get(f, L"S:{AAAAAAAA-0000-0000-0000-000000000003}", L"Mode") == L"Normal";
    MarkClear(f, L"S:{AAAAAAAA-0000-0000-0000-000000000001}");
    const bool cleared = Get(f, L"S:{AAAAAAAA-0000-0000-0000-000000000001}", L"TakenBy").empty() && Get(f, L"S:{AAAAAAAA-0000-0000-0000-000000000001}", L"Mode") == L"Low";
    Check(took && copied && marked && refused && cleared, "marks: copied, never moved, the old marked; one with a section of its own refused; driven again the mark goes");
    // the prune: the newest 10 marked kept; a warehouse mark is never pruned, past 10 newer
    const std::wstring p = Fresh(L"truck_id_test_prune.ini");
    WritePrivateProfileStringW(L"S:{W}", L"Mode", L"Low", p.c_str());
    WritePrivateProfileStringW(L"S:{W}", L"TakenBy", L"{X}", p.c_str());
    WritePrivateProfileStringW(L"S:{W}", L"TakenAt", L"1", p.c_str());
    WritePrivateProfileStringW(L"S:{W}", L"How", L"warehouse", p.c_str());
    WritePrivateProfileStringW(L"S:{U}", L"Mode", L"Low", p.c_str());
    for (int i = 0; i < 12; i++)
    {
        wchar_t sec[32], at[16];
        _snwprintf_s(sec, _TRUNCATE, L"S:{M%d}", i);
        _snwprintf_s(at, _TRUNCATE, L"%d", 100 + i);
        WritePrivateProfileStringW(sec, L"Mode", L"Low", p.c_str());
        WritePrivateProfileStringW(sec, L"TakenBy", L"{X}", p.c_str());
        WritePrivateProfileStringW(sec, L"TakenAt", at, p.c_str());
    }
    const int dropped = Prune(p, L"S:");
    Check(dropped == 2 && SectionThere(p, L"S:{W}") && SectionThere(p, L"S:{U}") && !SectionThere(p, L"S:{M0}") && !SectionThere(p, L"S:{M1}") && SectionThere(p, L"S:{M2}"),
          "prune: the 10 newest marked kept, the 2 oldest dropped; the warehouse mark kept past 12 newer; an unmarked section never dropped");
}

static void Seen()
{
    using namespace truckid;
    const std::wstring f = Fresh(L"truck_id_test_seen.ini");
    unsigned long long when = 0;
    const bool none = !SeenGet(f, kA1, when) && when == 0;
    SeenPut(f, kA1, 500);
    const bool listed = SeenGet(f, kA1, when) && when == 500 && Sections(f) == 1;
    // once a start: the caller writes it only where it is older than the start
    const unsigned long long start = 400;
    const bool before = SeenGet(f, kA1, when) && !(when < start);
    for (int i = 0; i < 205; i++)
    {
        char id[40];
        _snprintf_s(id, _TRUNCATE, "{DDDDDDDD-0000-0000-0000-%012d}", i);
        SeenPut(f, id, 1000 + i);
    }
    const bool newest = Keys(f, kSeenSection).size() == 200 && !SeenGet(f, kA1, when) && !SeenGet(f, "{DDDDDDDD-0000-0000-0000-000000000004}", when) &&
                        SeenGet(f, "{DDDDDDDD-0000-0000-0000-000000000005}", when);
    Check(none && listed && before && newest && Sections(f) == 1,
          "seen: not listed, then listed at its when; driven this start reads as not before it; the newest 200 kept; [Seen] makes no truck's section");
    wchar_t date[32];
    DateText(0, date, 32);
    const bool zero = !wcscmp(date, L"?");
    DateText(Now(), date, 32);
    Check(zero && wcslen(date) == 16 && date[4] == L'-' && date[13] == L':' && Short(kA1) == "AAAAAAAA" && Short("x") == "x", "seen: a when as a local date; an id's first 8");
}

static void Matching()
{
    using namespace truckid;
    const std::vector<std::wstring> pre = { L"Suspension:", L"Engine:" };
    const Parts parts = { { L"Engine", L"eng_a" }, { L"Gearbox", L"box_a" } };
    auto never = [](const char *) { return false; };
    auto put = [](const std::wstring &f, const wchar_t *sec, const wchar_t *key, const wchar_t *v) { WritePrivateProfileStringW(sec, key, v, f.c_str()); };
    const unsigned long long start = 1000;
    // one waiting truck with both sections and the same parts: one candidate, taken, marked How=warehouse
    {
        const std::wstring f = Fresh(L"truck_id_test_match1.ini");
        for (const wchar_t *sec : { L"Suspension:{AAAAAAAA-0000-0000-0000-000000000001}", L"Engine:{AAAAAAAA-0000-0000-0000-000000000001}" })
            put(f, sec, L"Own", L"1"), put(f, sec, L"Engine", L"eng_a"), put(f, sec, L"Gearbox", L"box_a");
        put(f, L"Suspension:{AAAAAAAA-0000-0000-0000-000000000001}", L"SpringScale", L"1.5");
        SeenPut(f, kA1, 500);
        const Match m = MatchFind(f, pre, kNew, parts, true, start, never);
        const int taken = m.why == kMatchOne ? MatchTake(f, pre, pre, m.id, kNew) : 0;
        const std::wstring to = L"Suspension:" + Wide(kNew), from = L"Suspension:" + Wide(kA1);
        Check(m.why == kMatchOne && m.candidates == 1 && !strcmp(m.id, kA1) && taken == 2 && Get(f, to, L"SpringScale") == L"1.5" && Get(f, to, L"How").empty() &&
                  Get(f, from, L"How") == L"warehouse" && Get(f, from, L"SpringScale") == L"1.5" && Get(f, L"Engine:" + Wide(kA1), L"TakenBy") == Wide(kNew),
              "match: one waiting truck with both sections counts once, its parts the same: both copied, the old kept and marked How=warehouse");
        for (int i = 0; i < 12; i++)
        {
            wchar_t sec[48], at[16];
            _snwprintf_s(sec, _TRUNCATE, L"Suspension:{M%d}", i);
            _snwprintf_s(at, _TRUNCATE, L"%llu", Now() + i);
            put(f, sec, L"Own", L"1"), put(f, sec, L"TakenBy", L"{X}"), put(f, sec, L"TakenAt", at);
        }
        Prune(f, L"Suspension:");
        Check(SectionThere(f, from) && Get(f, from, L"How") == L"warehouse", "match: its mark is kept past 12 newer marks");
        const Match again = MatchFind(f, pre, kA3, parts, true, start, never);
        Check(strcmp(again.id, kA1) != 0, "match: a marked truck is no candidate");
    }
    // two waiting trucks: none taken; parts that differ; no parts recorded (an old file, the build's first start)
    {
        const std::wstring f = Fresh(L"truck_id_test_match2.ini");
        for (const char *id : { kA1, kA2 })
        {
            const std::wstring sec = L"Suspension:" + Wide(id);
            put(f, sec.c_str(), L"Own", L"1"), put(f, sec.c_str(), L"Engine", L"eng_a"), put(f, sec.c_str(), L"Gearbox", L"box_a");
        }
        const Match two = MatchFind(f, pre, kNew, parts, true, start, never);
        put(f, (L"Suspension:" + Wide(kA2)).c_str(), L"Engine", L"eng_b");
        const Match other = MatchFind(f, pre, kNew, { { L"Engine", L"eng_c" }, { L"Gearbox", L"box_a" } }, true, start, never);
        const Match oneLeft = MatchFind(f, pre, kNew, parts, true, start, never);
        Check(two.why == kMatchTwo && two.candidates == 2 && !two.id[0] && other.why == kMatchNone && oneLeft.why == kMatchOne && !strcmp(oneLeft.id, kA1),
              "match: two waiting trucks with its parts: none taken; parts that differ: none; one left with its parts: that one");
        const std::wstring g = Fresh(L"truck_id_test_match3.ini");
        put(g, (L"Suspension:" + Wide(kA1)).c_str(), L"Own", L"1");
        put(g, (L"Suspension:" + Wide(kA1)).c_str(), L"SpringScale", L"1.5");
        const Match old = MatchFind(g, pre, kNew, parts, true, start, never);
        Check(old.why == kMatchNone && old.waiting == 1, "match: a section with no parts recorded (an old file, the build's first start) never matches");
        Check(MatchFind(f, pre, kNew, {}, true, start, never).why == kMatchNoParts && MatchFind(f, pre, kNew, parts, false, start, never).why == kMatchUnnamed &&
                  MatchFind(f, pre, kNew, { { L"Engine", L"" }, { L"Gearbox", L"box_a" } }, true, start, never).why == kMatchNoParts,
              "match: no parts read, or a kind not read by name: never");
    }
    // driven this start: waiting only with no load since and its vehicle gone (drivenGone); a section with only parts and marks
    {
        const std::wstring f = Fresh(L"truck_id_test_match4.ini");
        const std::wstring sec = L"Suspension:" + Wide(kA1);
        put(f, sec.c_str(), L"Own", L"1"), put(f, sec.c_str(), L"Engine", L"eng_a"), put(f, sec.c_str(), L"Gearbox", L"box_a");
        SeenPut(f, kA1, 2000); // driven this start (after start 1000)
        const Match load = MatchFind(f, pre, kNew, parts, true, start, never);
        const Match gone = MatchFind(f, pre, kNew, parts, true, start, [](const char *id) { return !strcmp(id, kA1); });
        Check(load.why == kMatchNone && gone.why == kMatchOne, "match: driven this start, a load since or its vehicle there: none; no load and gone (stored and taken out): matched");
        const std::wstring g = Fresh(L"truck_id_test_match5.ini");
        const std::wstring bare = L"Suspension:" + Wide(kA1);
        put(g, bare.c_str(), L"Engine", L"eng_a"), put(g, bare.c_str(), L"Gearbox", L"box_a");
        Check(MatchFind(g, pre, kNew, parts, true, start, never).why == kMatchNone, "match: a section with nothing of its own beyond its parts: none");
    }
    // the parts go only into a section that exists
    {
        const std::wstring f = Fresh(L"truck_id_test_parts.ini");
        const bool noSection = !PartsPut(f, L"Suspension:{A}", parts) && Sections(f) == 0;
        put(f, L"Suspension:{A}", L"Own", L"1");
        const bool written = PartsPut(f, L"Suspension:{A}", parts) && Get(f, L"Suspension:{A}", L"Engine") == L"eng_a" && !PartsPut(f, L"Suspension:{A}", parts);
        Check(noSection && written, "parts: never a section made for them; written where one is, once while they stay the same");
    }
}

static void Lines()
{
    using namespace truckid;
    wchar_t t[512];
    DriveLine(t, 512, "kind_a", kA1, kLineOwn, 0, "", L"", L"");
    const bool own = !wcscmp(t, L"truck: kind_a, id AAAAAAAA, its own settings, not listed as driven before");
    DriveLine(t, 512, "kind_a", kA1, kLineOwn, Now(), "", L"", L"");
    const bool ownSeen = wcsstr(t, L"its own settings, last driven 20") != nullptr;
    DriveLine(t, 512, "kind_a", kA2, kLineTaken, 0, kA1, L"back within 20 s", L"");
    const bool taken = !wcscmp(t, L"truck: kind_a, id AAAAAAAA, taken over from AAAAAAAA (back within 20 s)");
    DriveLine(t, 512, "kind_a", kNew, kLineMatched, 0, kA1, L"", L"");
    const bool matched = wcsstr(t, L"truck: kind_a, id CCCCCCCC, matched out of the warehouse by its parts: the settings of AAAAAAAA") == t;
    DriveLine(t, 512, "kind_a", kNew, kLineKind, 0, "", L"", MatchWords(kMatchNone));
    const bool kind = !wcscmp(t, L"truck: kind_a, id CCCCCCCC, on the kind's settings (no waiting truck of the kind has its parts)");
    Check(own && ownSeen && taken && matched && kind, "the drive's line: its own (last driven or not listed), taken over, matched, the kind's with why");
}

int wmain()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_dir = self;
    g_dir = g_dir.substr(0, g_dir.rfind(L'\\') + 1);
    Rules();
    Marks();
    Seen();
    Matching();
    Lines();
    printf("truck id: %d ok, %d failed\n", g_ok, g_failed);
    return g_failed ? 1 : 0;
}
