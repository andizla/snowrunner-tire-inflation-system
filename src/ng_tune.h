// ng_tune.h: the block a Next Gen mod publishes its tuning figures in, and the tuning tool reads and answers
// (BRIEF_rolling_drag_sliders.md, the Reviewer's addendum 3). The same file in every repo that uses it.
//
// One block a mod: named shared memory Local\NextGen.Tune.<process id>.<n>, n from 0 to 7. A mod makes the first n
// that is not there yet; the tool tries all eight. No pointer is in the block: names and texts are bytes in it. The
// tool never writes a mod's memory or ini: it writes a wanted value into the block and the mod takes it at its next
// pass, holds it to the range and puts it in force by the road its own page uses.
//
// The layout is only ever added to at the end, of the head and of an entry. Nothing moves and nothing changes its
// meaning; a field added later must read "as before" at zero. A reader steps through the entries by the sizes the
// block gives, never by its own, and reads of each no more than both sides have. kNgTuneLayout rises only for a change
// that breaks this, and a block of another layout is left alone by the tool.
//
// Who writes what, with no lock:
//   the mod's own thread alone writes the head and, of an entry, everything but wanted and wantedCount. It writes
//   under the sequence: odd, write, even. No hook and no thread of the game's touches the block.
//   the tool's drawing thread alone writes wanted, then adds 1 to wantedCount.
//   the mod, each pass, for each entry whose wantedCount differs from the count it took: reads the count, reads
//   wanted, reads the count again. The same: taken. Not the same: next pass.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static const uint32_t kNgTuneMagic = 0x5554474E; // "NGTU"
static const uint32_t kNgTuneLayout = 1;
static const int kNgTuneBlocks = 8, kNgTuneMost = 64;
static const uint64_t kNgTuneStaleMs = 2000; // a mod's tick older than this: the tool greys its group and writes nothing
enum { kNgTuneSlider = 1, kNgTuneLine = 2, kNgTuneSwitch = 3, kNgTuneButton = 4 };
static const uint32_t kNgTuneDisabled = 1; // flags, bit 0: the entry takes nothing; its text is the reason

#pragma pack(push, 4)
struct NgTuneHead
{
    uint32_t magic;         // kNgTuneMagic
    uint32_t layout;        // kNgTuneLayout
    uint32_t headSize;      // this head as the mod has it: the entries start there
    uint32_t entrySize;     // one entry as the mod has it
    uint32_t count;         // entries, kNgTuneMost at the most
    volatile LONG sequence; // odd while the mod writes
    uint64_t tick;          // GetTickCount64 at the mod's last pass
    int32_t order;          // where the mod's group goes among the others, lowest first (its tab's order)
    char name[40];          // the mod's name, for the small heading
    uint32_t spare;
};
struct NgTuneEntry
{
    // what the mod writes once
    uint32_t kind;   // kNgTuneSlider, kNgTuneLine, kNgTuneSwitch, kNgTuneButton
    char name[48];
    char group[32];
    char tip[128];
    float least, most, def, step;
    int32_t decimals; // shown after the point
    // what the mod writes each pass
    float value;      // in force
    char text[32];    // a line that is a word (the ground), or the reason an entry is disabled
    uint32_t flags;   // kNgTuneDisabled
    // what the tool writes
    float wanted;
    volatile LONG wantedCount; // a button: each rise is one press
    // what the mod writes back
    volatile LONG takenCount;
    uint32_t spare;
};
#pragma pack(pop)
// The reviewed layout named an entry of 256 bytes; the fields it lists come to 284, so an entry is 288 (hand-back).
static_assert(sizeof(NgTuneHead) == 80 && offsetof(NgTuneHead, sequence) == 20 && offsetof(NgTuneHead, tick) == 24 && offsetof(NgTuneHead, order) == 32 &&
                  offsetof(NgTuneHead, name) == 36,
              "the tuning block's head is fixed: add at its end only");
static_assert(sizeof(NgTuneEntry) == 288 && offsetof(NgTuneEntry, name) == 4 && offsetof(NgTuneEntry, group) == 52 && offsetof(NgTuneEntry, tip) == 84 &&
                  offsetof(NgTuneEntry, least) == 212 && offsetof(NgTuneEntry, decimals) == 228 && offsetof(NgTuneEntry, value) == 232 && offsetof(NgTuneEntry, text) == 236 &&
                  offsetof(NgTuneEntry, flags) == 268 && offsetof(NgTuneEntry, wanted) == 272 && offsetof(NgTuneEntry, wantedCount) == 276 &&
                  offsetof(NgTuneEntry, takenCount) == 280,
              "the tuning block's entry is fixed: add at its end only");
static const size_t kNgTuneHeadLeast = 80, kNgTuneEntryLeast = 288; // the sizes of layout 1's first build: a block with less is not one
static const size_t kNgTuneBlockMost = sizeof(NgTuneHead) + (size_t)kNgTuneMost * sizeof(NgTuneEntry);

inline void NgTuneName(char *out, size_t size, DWORD pid, int n) { snprintf(out, size, "Local\\NextGen.Tune.%lu.%d", (unsigned long)pid, n); }
// A text into a field of the block: cut at the field, the rest zeros (a full field has no zero at its end).
inline void NgTuneText(char *field, size_t size, const char *text)
{
    memset(field, 0, size);
    if (text) memcpy(field, text, strnlen(text, size));
}
// A value held to an entry's range and step, then rounded to the decimals shown, so a figure of 0.010 is the float
// nearest 0.010 whatever the steps summed to (a default then comes back as the default, to the bit). False: no number,
// nothing to take.
inline bool NgTuneHold(uint32_t kind, float least, float most, float step, int decimals, float wanted, float &held)
{
    if (!isfinite(wanted)) return false;
    if (kind == kNgTuneSwitch)
    {
        held = wanted >= 0.5f ? 1.0f : 0.0f;
        return true;
    }
    if (!isfinite(least) || !isfinite(most) || most < least) return false;
    float v = wanted < least ? least : wanted > most ? most : wanted;
    if (isfinite(step) && step > 0.0f)
    {
        v = least + step * floorf((v - least) / step + 0.5f);
    }
    if (decimals >= 0 && decimals <= 6)
    {
        double scale = 1.0;
        for (int i = 0; i < decimals; i++) scale *= 10.0;
        v = (float)(floor((double)v * scale + 0.5) / scale);
    }
    if (v > most) v = most;
    if (v < least) v = least;
    held = v;
    return true;
}

// ---- the mod's side ----
// One figure as a mod lists it. The texts are ASCII and are copied into the block.
struct NgTuneItem
{
    int kind;
    const char *name, *group, *tip;
    float least, most, def, step;
    int decimals;
};
// The block's bytes set up in memory handed in (a mapping, or a test's buffer of kNgTuneBlockMost bytes).
inline bool NgTuneSetUp(uint8_t *block, size_t size, const char *modName, int order, const NgTuneItem *items, int count)
{
    if (!block || count < 0 || count > kNgTuneMost || size < sizeof(NgTuneHead) + (size_t)count * sizeof(NgTuneEntry)) return false;
    memset(block, 0, sizeof(NgTuneHead) + (size_t)count * sizeof(NgTuneEntry));
    NgTuneHead *h = (NgTuneHead *)block;
    h->sequence = 1; // being written
    h->layout = kNgTuneLayout;
    h->headSize = sizeof(NgTuneHead);
    h->entrySize = sizeof(NgTuneEntry);
    h->count = (uint32_t)count;
    h->order = order;
    NgTuneText(h->name, sizeof h->name, modName);
    for (int i = 0; i < count; i++)
    {
        NgTuneEntry *e = (NgTuneEntry *)(block + sizeof(NgTuneHead)) + i;
        e->kind = (uint32_t)items[i].kind;
        NgTuneText(e->name, sizeof e->name, items[i].name);
        NgTuneText(e->group, sizeof e->group, items[i].group);
        NgTuneText(e->tip, sizeof e->tip, items[i].tip);
        e->least = items[i].least, e->most = items[i].most, e->def = items[i].def, e->step = items[i].step;
        e->decimals = items[i].decimals;
        e->value = items[i].def;
    }
    h->magic = kNgTuneMagic; // last: a reader takes no block without it
    InterlockedExchange(&h->sequence, 2);
    return true;
}
inline NgTuneEntry *NgTuneAt(uint8_t *block, int i) { return (NgTuneEntry *)(block + sizeof(NgTuneHead)) + i; }
// A pass of the mod's thread: Begin, a Value for every entry that changed, End with the pass's tick.
inline void NgTuneBegin(uint8_t *block) { InterlockedIncrement(&((NgTuneHead *)block)->sequence); }
inline void NgTuneValue(uint8_t *block, int i, float value, const char *text = nullptr, bool disabled = false)
{
    NgTuneEntry *e = NgTuneAt(block, i);
    e->value = value;
    NgTuneText(e->text, sizeof e->text, text);
    e->flags = disabled ? (e->flags | kNgTuneDisabled) : (e->flags & ~kNgTuneDisabled);
}
inline void NgTuneEnd(uint8_t *block, uint64_t tick)
{
    NgTuneHead *h = (NgTuneHead *)block;
    h->tick = tick;
    InterlockedIncrement(&h->sequence);
}
// What the tool asked of entry i since the mod last looked. 0: nothing, or not whole yet (next pass). Else the number
// of new askings, with held the value to put in force (a slider or a switch, held to the range and the step). A wanted
// value that is no number, and anything asked of a disabled entry or of a line, is dropped: -1, and it is not asked
// again. For a button the return is the presses and held is not set.
// NgTuneTaken is the decision on the three reads (the count, wanted, the count again), apart for the tests.
inline int NgTuneTaken(NgTuneEntry *e, LONG count, float wanted, LONG again, float &held)
{
    if (again != count) return 0; // the tool wrote between the two reads: next pass
    const int rises = (int)(count - e->takenCount);
    e->takenCount = count;
    if ((e->flags & kNgTuneDisabled) || e->kind == kNgTuneLine) return -1;
    if (e->kind == kNgTuneButton) return rises > 0 ? rises : -1;
    if (e->kind != kNgTuneSlider && e->kind != kNgTuneSwitch) return -1;
    return NgTuneHold(e->kind, e->least, e->most, e->step, e->decimals, wanted, held) ? (rises > 0 ? rises : 1) : -1;
}
inline int NgTuneTake(uint8_t *block, int i, float &held)
{
    NgTuneEntry *e = NgTuneAt(block, i);
    const LONG count = e->wantedCount;
    if (count == e->takenCount) return 0;
    const float wanted = e->wanted;
    return NgTuneTaken(e, count, wanted, e->wantedCount, held);
}
// The mod's mapping: the first n of this process that is not there yet. block stays null where none could be made
// (eight are taken, or the system refused): the mod then goes on as if there were no tool.
struct NgTuneMod
{
    HANDLE map;
    uint8_t *block;
    int n, count;
};
inline bool NgTuneOpen(NgTuneMod &m, const char *modName, int order, const NgTuneItem *items, int count)
{
    m = NgTuneMod{ nullptr, nullptr, -1, 0 };
    if (count < 0 || count > kNgTuneMost) return false;
    for (int n = 0; n < kNgTuneBlocks; n++)
    {
        char name[64];
        NgTuneName(name, sizeof name, GetCurrentProcessId(), n);
        SetLastError(0);
        const HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)kNgTuneBlockMost, name);
        if (!map) continue;
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(map);
            continue;
        }
        uint8_t *block = (uint8_t *)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!block || !NgTuneSetUp(block, kNgTuneBlockMost, modName, order, items, count))
        {
            if (block) UnmapViewOfFile(block);
            CloseHandle(map);
            return false;
        }
        m = NgTuneMod{ map, block, n, count };
        return true;
    }
    return false;
}

// ---- the tool's side ----
// A block as the tool takes it: a copy of what both sides know, every text with a zero at its end.
struct NgTuneSeenEntry
{
    int index; // the entry's place in the block, for NgTuneWant
    uint32_t kind, flags;
    char name[49], group[33], tip[129], text[33];
    float least, most, def, step, value;
    int decimals;
};
struct NgTuneSeen
{
    bool ok;
    char why[96]; // why there is no group, for the tool's log
    char mod[41];
    int order, count;
    uint64_t tick;
    size_t headSize, entrySize;
    NgTuneSeenEntry e[kNgTuneMost];
};
inline void NgTuneCopyText(char *out, const char *field, size_t size)
{
    memcpy(out, field, size);
    out[size] = 0;
}
// mapped: how many bytes of the block can be read. False with why: no group for this block. An entry of a kind the
// tool does not know is left out and the others show.
inline bool NgTuneRead(const uint8_t *block, size_t mapped, NgTuneSeen &out)
{
    out.ok = false, out.count = 0, out.why[0] = 0;
    const auto no = [&out](const char *why) {
        snprintf(out.why, sizeof out.why, "%s", why);
        return false;
    };
    if (!block || mapped < kNgTuneHeadLeast) return no("the block is shorter than a head");
    const NgTuneHead *live = (const NgTuneHead *)block;
    if (live->magic != kNgTuneMagic) return no("it is not a tuning block (or its mod has not set it up yet)");
    if (live->layout != kNgTuneLayout) return no("it has another layout than this tool knows");
    const size_t headSize = live->headSize, entrySize = live->entrySize, count = live->count;
    if (headSize < kNgTuneHeadLeast || (headSize & 7)) return no("its head's size makes no sense");
    if (entrySize < kNgTuneEntryLeast || (entrySize & 7)) return no("its entry size makes no sense");
    if (count > (size_t)kNgTuneMost) return no("it names more entries than a block holds");
    if (headSize > mapped || count * entrySize > mapped - headSize) return no("it is smaller than its sizes say");
    // a copy taken while the sequence is even and did not move: two tries, else the last copy
    static const size_t kKnown = sizeof(NgTuneEntry);
    NgTuneHead head = {};
    for (int look = 0; look < 2; look++)
    {
        const LONG before = live->sequence;
        memcpy(&head, block, sizeof head);
        out.count = 0;
        for (size_t i = 0; i < count; i++)
        {
            NgTuneEntry e;
            memcpy(&e, block + headSize + i * entrySize, kKnown);
            if (e.kind != kNgTuneSlider && e.kind != kNgTuneLine && e.kind != kNgTuneSwitch && e.kind != kNgTuneButton) continue;
            NgTuneSeenEntry &s = out.e[out.count++];
            s.index = (int)i, s.kind = e.kind, s.flags = e.flags;
            NgTuneCopyText(s.name, e.name, sizeof e.name), NgTuneCopyText(s.group, e.group, sizeof e.group);
            NgTuneCopyText(s.tip, e.tip, sizeof e.tip), NgTuneCopyText(s.text, e.text, sizeof e.text);
            s.least = e.least, s.most = e.most, s.def = e.def, s.step = e.step, s.value = e.value;
            s.decimals = e.decimals < 0 ? 0 : e.decimals > 6 ? 6 : e.decimals;
        }
        if (!(before & 1) && live->sequence == before) break;
    }
    NgTuneCopyText(out.mod, head.name, sizeof head.name);
    out.order = head.order, out.tick = head.tick, out.headSize = headSize, out.entrySize = entrySize;
    out.ok = true;
    return true;
}
inline bool NgTuneStale(uint64_t tick, uint64_t now) { return now > tick && now - tick > kNgTuneStaleMs; }
// The tool asks: a value for a slider or a switch, held to the entry's range first; press for a button. Nothing is
// written into a stale block, a disabled entry or a line, or where the value is no number. True: asked.
inline bool NgTuneWant(uint8_t *block, const NgTuneSeen &seen, const NgTuneSeenEntry &entry, float value, uint64_t now)
{
    if (!seen.ok || NgTuneStale(seen.tick, now) || (entry.flags & kNgTuneDisabled) || entry.kind == kNgTuneLine) return false;
    NgTuneEntry *e = (NgTuneEntry *)(block + seen.headSize + (size_t)entry.index * seen.entrySize);
    if (entry.kind != kNgTuneButton)
    {
        float held = 0.0f;
        if (!NgTuneHold(entry.kind, entry.least, entry.most, entry.step, entry.decimals, value, held)) return false;
        e->wanted = held;
    }
    InterlockedIncrement(&e->wantedCount);
    return true;
}
