// ng_graphics.h: what Next Gen Graphics (hid.dll beside the game's exe) offers the NEXT GEN menu, agreed between the
// Next Gen mods' overlay and Next Gen Graphics on 2026-10-23. Next Gen Graphics has no overlay of its own: it exports
//
//     extern "C" const NgGraphicsApi *NgGraphicsGetApi(int version);
//
// which gives the table below for a version it serves (from 1) and nullptr for one it does not. The overlay finds the
// export by the full path of hid.dll beside the exe (the game loads Windows' own hid.dll too), and one Next Gen mod in
// the game draws the menu's Graphics tab from it. The strings are ASCII (the menu's font is the game's) and last as long
// as the process; get and set are plain atomic flags, called on the game's present thread while the tab shows, and a
// set takes effect at the next draw that reads it, as the DLL's own keys (F4 to F11) do.
#pragma once

static const int kNgGraphicsVersion = 1;

struct NgGraphicsApi
{
    int version;                   // the version this table is, kNgGraphicsVersion or later
    int count;                     // the switches, 0 to 64
    const char *(*name)(int i);    // a switch's name in the tab
    const char *(*help)(int i);    // its tip, or nullptr
    int (*get)(int i);             // 1 on, 0 off
    void (*set)(int i, int on);    // switches it, as its key would
    int (*statusCount)();          // the status lines below the switches
    const char *(*status)(int i);  // one of them, or nullptr
};

typedef const NgGraphicsApi *(*NgGraphicsGetApiFn)(int version);
