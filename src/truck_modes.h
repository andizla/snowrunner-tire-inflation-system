// truck_modes.h: each truck's pressure mode, kept by the truck kind's name (PerTruck, since 1.3.1) in
// TirePressure.trucks.ini next to the exe. One section per kind as the game's files name it ("western_star_6900xd";
// "zetros/176593" for a truck from a mod) with Mode=Low, Reduced, Normal or Increased. Two trucks of one kind share
// it. tire_pressure.cpp reads the kind from the game (TruckKindName) and calls these.
#pragma once
#include <windows.h>
#include <stdio.h>
#include <string>
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

// The mode saved for kind; false when none is saved or the word is no mode's name.
inline bool TruckModeLoad(const std::wstring &file, const char *kind, int &mode)
{
    wchar_t buf[32] = {};
    GetPrivateProfileStringW(TruckKindWide(kind).c_str(), L"Mode", L"", buf, 32, file.c_str());
    for (int m = 0; m < kModeCount; m++)
        if (buf[0] && !_wcsicmp(buf, kTruckModeNames[m])) { mode = m; return true; }
    return false;
}

// The mode for kind, into its section. A missing file is made first with a note at its head on what it is.
inline void TruckModeSave(const std::wstring &file, const char *kind, int mode)
{
    if (mode < 0 || mode >= kModeCount) return;
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        FILE *f = nullptr;
        if (_wfopen_s(&f, file.c_str(), L"w, ccs=UTF-16LE") == 0 && f)
        {
            fwprintf(f, L"; Each truck's pressure mode by the truck kind's name. It is written when a mode is chosen and put back when\n"
                        L"; that truck is driven again. PerTruck=0 in TirePressure.ini turns this off.\n");
            fclose(f);
        }
    }
    WritePrivateProfileStringW(TruckKindWide(kind).c_str(), L"Mode", kTruckModeNames[mode], file.c_str());
}
