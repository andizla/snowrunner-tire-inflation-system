// TirePressure.asi for SnowRunner: Expeditions' tire inflation system. The truck being driven gets pressure modes
// (Low, Reduced, Normal, Increased), picked on a panel or with a key. A mode multiplies the tire's grip by the ground
// under each wheel, shrinks the wheel's collision cylinder (the game's own soft tire rendering then draws the
// flattening), scales fuel use and steering speed, and soft tires driven too fast take damage.
//
// Where the values live (found on the running game 2026-10-02; the RVAs are the Steam build's, kBuilds has each
// build's own):
//   truck control global at RVA 0x2a8eb78; the driven vehicle at control+8
//   vehicle+0x200 / +0x208 -> begin / end of its array of wheel model pointers; wheel model+0x2C8 -> wheel object
//   wheel object (vtable RVA 0x2258918): +0x18 Havok body, +0x94 radius, +0xA0 SubstanceFriction, +0xA4 BodyFrictionAsphalt
//   body+0xCC BodyFriction (the Havok material; the game rewrites it per surface while driving, so it is not touched)
//   body+0x20 -> cylinder shape (vtable RVA 0x23c2c08), shape+0x28 radius
// A respawn or a tire change rebuilds the wheel objects with stock values, so the wheels are looked up again twice a
// second and the mode is applied again to any wheel that does not carry it.
//
// Ini TirePressure.ini next to the exe (written with the defaults on the first start), log TirePressure.log.
#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "panel.h"
#include "vanilla_balance.h"
#include "marker.h"
#include "build_find.h"
#include "sound.h"

// Where the few places lie that are elsewhere in every build of the game's exe (build_find.h has what each is). Run
// looks for them in the running exe at the start and stands down when it does not find every one.
static Build g_buildFound = {};
static const Build *const g_build = &g_buildFound;

// body: the grip on plain ground (dirt, and what is none of the others); gravel, sand, rock: the same where the ground
// is that.
struct Mode { const wchar_t *name; float body, asphalt, substance, radiusOffset, fuel, steering, gravel, sand, rock; };
// The modes by PressureMode (panel.h): Normal is the tire's own values; the others come from the ini (SettingsDefaults
// has their defaults and where they come from).
static Mode g_modes[kModeCount] = { { L"Low", 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
                                    { L"Reduced", 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
                                    { L"Normal", 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
                                    { L"Increased", 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } };
static int g_modeCount = 4; // 3 without Increased (ini Increased=0): the modes run kLow .. g_modeCount - 1
// What a wheel or truck the game just built carries: its own values. The ini's base grip (BaseGround, BaseAsphalt,
// BaseMud) goes into all the modes above, so Normal is not always this.
static const Mode kStockMode = { L"stock", 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
// Tire damage at speed, by PressureMode (Expeditions' TickDamageParams): faster than minVel (m/s) for `seconds`, every
// wheel on the ground takes minDamage, rising to maxDamage at maxVel, and again every `seconds`. maxDamage 0 = none.
struct Tick { float minVel, maxVel, seconds; int minDamage, maxDamage; };
static Tick g_ticks[kModeCount] = {};
static bool g_tireDamage = true; // ini TireDamage
static float g_asphaltFloor = 0.0f; // ini AsphaltFloor: paved grip every tire gets at least, 0 = off
static int g_key = VK_F3;
static int g_stepKeys[4] = { VK_LEFT, VK_RIGHT, VK_RETURN, VK_BACK }; // ini KeyLower, KeyRaise, KeyConfirm, KeyCancel: the keyboard while the panel is open
// the panel (panel.cpp): UI=0 turns it off (F3 then cycles the modes directly, as without ReShade)
static bool g_uiWanted = true, g_ui = false, g_padOk = false, g_reshadeOk = false;
static int g_confirmMs = 15000; // the panel's selection confirms itself after this long without input, 0 = never
static uint32_t g_padOpen = 0, g_padLower = 0, g_padRaise = 0, g_padConfirm = 0, g_padCancel = 0;
static HMODULE g_self = nullptr;
static bool g_beep = true;
static bool g_balanceOn = false;      // ini VanillaBalance (vanilla_balance.h)
static float g_balanceStrength = 1.0f;

// The ini's settings in force (panel.h TpSettings). The settings page in ReShade's overlay reads them and hands back
// edits (SettingsPut, render thread); this thread applies those on its next pass and saves the ini a second later.
static SRWLOCK g_setLock = SRWLOCK_INIT;
static TpSettings g_set;
static TpLive g_live = {};
static volatile LONG g_setDirty = 0, g_reloadAsked = 0;

static float g_seconds = kSecondsDefault; // a mode change deflates or inflates over this time
static float g_soundVolume = 0.5f;        // ini SoundVolume as 0..1: the air and compressor sounds of a change (sound.h)
static int g_probeFriction = 0, g_probeWriters = 0; // diagnostics, see ProbeHandler
static int g_probeFuel = 0;                         // diagnostics, see FuelSnapshot
static int g_probeDamage = -1, g_probeDamageBack = 0; // diagnostics: ini ProbeDamage=N sets every wheel's damage to N once, ProbeDamageBack=s to none s seconds later
static int g_probeDrive = 0;                        // diagnostics: ini ProbeDrive=1 logs speed, handbrake and ground every second
static int g_probeByHand = 0;                       // diagnostics: ini ProbeByHand=1 deals tire damage without the game's thread
static long long g_watchA = -1, g_watchB = -1, g_watchC = -1;

// cylinder: the collision cylinder's stock radius; writtenCylinder: the radius written last; room: how many metres the
// game can draw this tire flatter than it comes (FlattenLimit less the tire's own offset); radius: the tire's radius.
struct Stock { float body, asphalt, substance, cylinder; Mode written; float writtenCylinder, room, radius; };
static std::unordered_map<uint64_t, Stock> g_wheels; // wheel object -> its stock values and the factors written to it
static float g_deepest = 0.0f; // the largest AdditionalRadiusOffset among the modes
static bool g_rollingRadius = true; // ini RollingRadius (RollFactor)
static std::wstring g_dir;
static uint64_t g_base = 0;
static int g_mode = kNormal;
static Mode g_now = g_modes[kNormal]; // the factors in force, moving towards g_modes[g_mode]

// The log is rewritten at each game start. This thread and ReShade's render thread (PanelLog) both write it: one at a
// time, as the file is opened for one writer and a second open would fail and lose its line.
static SRWLOCK g_logLock = SRWLOCK_INIT;
static bool g_logStarted = false; // true once this start's first line is in (a second copy of the mod sets it to add to the first copy's log)
static void LogV(const wchar_t *fmt, va_list ap)
{
    AcquireSRWLockExclusive(&g_logLock);
    FILE *f = nullptr;
    if (_wfopen_s(&f, (g_dir + L"TirePressure.log").c_str(), g_logStarted ? L"a, ccs=UTF-8" : L"w, ccs=UTF-8") == 0 && f)
    {
        g_logStarted = true;
        SYSTEMTIME t;
        GetLocalTime(&t);
        fwprintf(f, L"%02u:%02u:%02u ", t.wHour, t.wMinute, t.wSecond);
        vfwprintf(f, fmt, ap);
        fputwc(L'\n', f);
        fclose(f);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

static void Log(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    LogV(fmt, ap);
    va_end(ap);
}

void PanelLog(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    LogV(fmt, ap);
    va_end(ap);
}

// Game memory is read and written through these, so an object freed under us is an error return, not a fault.
template <typename T> static bool Read(uint64_t addr, T &out) { return addr >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), (void *)addr, &out, sizeof out, nullptr); }
template <typename T> static bool WriteAs(uint64_t addr, const T &v) { return addr >= 0x10000 && WriteProcessMemory(GetCurrentProcess(), (void *)addr, &v, sizeof v, nullptr) != 0; }
static bool Write(uint64_t addr, float v) { return WriteAs(addr, v); }

// A number from the ini; def for a missing key and for anything that is not a finite number ("nan" would pass every
// range check after it, the min and max macros let it through, and end up in the game's memory).
static float IniFloat(const wchar_t *section, const std::wstring &key, float def, const std::wstring &ini)
{
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(section, key.c_str(), L"", buf, 64, ini.c_str());
    if (!buf[0]) return def;
    const float v = (float)_wtof(buf);
    return isfinite(v) ? v : def;
}

static const wchar_t *const kModeSections[3] = { L"Reduced", L"Low", L"Increased" }; // TpSettings::mode[i]
static const wchar_t *const kBaseKeys[3] = { L"BaseGround", L"BaseAsphalt", L"BaseMud" };

static std::wstring Wide(const char *s) { std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

// Every key the settings page edits, into the ini (WritePrivateProfileString keeps the comments and other keys).
static void WriteIni(const TpSettings &s)
{
    const std::wstring ini = g_dir + L"TirePressure.ini";
    // "%g" values go in with the fewest digits that read back as the same float (20 km/h is 5.555556 m/s)
    auto put = [&](const wchar_t *section, const std::wstring &key, const wchar_t *fmt, double v) {
        wchar_t b[32];
        swprintf_s(b, fmt, v);
        for (int digits = 7; digits <= 9 && !wcscmp(fmt, L"%g") && (float)_wtof(b) != (float)v; digits++) swprintf_s(b, L"%.*g", digits, v);
        WritePrivateProfileStringW(section, key.c_str(), b, ini.c_str());
    };
    const wchar_t *T = L"TirePressure";
    put(T, L"Key", L"%.0f", s.key);
    for (int i = 0; i < 4; i++) put(T, Wide(kPanelKeyNames[i]), L"%.0f", s.panelKeys[i]);
    put(T, L"Beep", L"%.0f", s.beep ? 1 : 0);
    put(T, L"Seconds", L"%g", s.seconds);
    put(T, L"SoundVolume", L"%g", s.soundVolume);
    put(T, L"IniVersion", L"%.0f", kIniVersion);
    put(T, L"UI", L"%.0f", s.ui ? 1 : 0);
    put(T, L"UIScale", L"%g", s.uiScale);
    put(T, L"ConfirmSeconds", L"%g", s.confirmSeconds);
    for (int i = 0; i < 5; i++) WritePrivateProfileStringW(T, Wide(kPadKeys[i]).c_str(), Wide(s.pad[i]).c_str(), ini.c_str());
    for (int i = 0; i < 3; i++) put(T, kBaseKeys[i], L"%g", s.base[i]);
    put(T, L"VanillaBalance", L"%.0f", s.vanillaBalance ? 1 : 0);
    put(T, L"VanillaBalanceStrength", L"%g", s.balanceStrength);
    put(T, L"AsphaltFloor", L"%g", s.asphaltFloor);
    put(T, L"Increased", L"%.0f", s.increased ? 1 : 0);
    put(T, L"TireDamage", L"%.0f", s.tireDamage ? 1 : 0);
    put(T, L"RollingRadius", L"%.0f", s.rollingRadius ? 1 : 0);
    for (int m = 0; m < 3; m++)
        for (int k = 0; k < kModeKeyCount; k++) put(kModeSections[m], Wide(kModeKeys[k]), L"%g", s.mode[m][k]);
}

// The ini into s (defaults for missing keys); a missing ini is written with the defaults and how to use them.
static void ReadIni(TpSettings &s)
{
    SettingsDefaults(s);
    const std::wstring ini = g_dir + L"TirePressure.ini";
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        FILE *f = nullptr;
        if (_wfopen_s(&f, ini.c_str(), L"w, ccs=UTF-16LE") == 0 && f)
        {
            fwprintf(f, L"; TirePressure: tire pressure for the truck being driven: Low, Reduced, Normal and (Increased=1) Increased.\n"
                        L"; The Tire Inflation System tab in ReShade's overlay edits everything here in the game.\n"
                        L"; KeyLower, KeyRaise, KeyConfirm and KeyCancel work the panel while it is open (37 and 39 = the left and\n"
                        L"; right arrow, 13 = Enter, 8 = Backspace, 0 = none).\n"
                        L"; Key is a virtual key code (114 = F3). Beep=1 beeps once for Normal, twice for Reduced, three times for Low,\n"
                        L"; and high for Increased. Seconds is how long the tires take to deflate or inflate to the new mode.\n"
                        L"; SoundVolume (0 to 100, 0 = none): air let out while the tires deflate, air going in while they fill,\n"
                        L"; and the compressor, which fills its tank again after every few fillings and lets its air go as it stops.\n"
                        L"; AirOutSound, AirInSound, CompressorSound and CompressorStopSound pick those four sounds: nothing = the\n"
                        L"; mod's own, none = without it, or the name of a WAV file in this folder, or of a sample in the game's\n"
                        L"; shared_sound.pak ([sound]\\actors\\actor_lamp_generator_loop.pcm). The first three play round and\n"
                        L"; round while they last; a WAV file with a loop marked in it plays up to the loop once.\n"
                        L"; UI=1 shows the Tire Inflation System panel (needs ReShade with add-on support): the key opens it and\n"
                        L"; steps the pressure down, the choice confirms itself after ConfirmSeconds. Pad buttons (XInput): DPadUp,\n"
                        L"; DPadDown, DPadLeft, DPadRight, A, B, X, Y, LB, RB, LS, RS, Start, Back, joined with + (LB+DPadDown), or None.\n"
                        L"; BaseGround, BaseAsphalt and BaseMud scale the grip of every mode, Normal too.\n"
                        L"; VanillaBalance=1 brings tires that grip better than every vanilla tire on ground, asphalt and mud at once\n"
                        L"; down to the best vanilla tire of their kind; vanilla tires stay as they are. VanillaBalanceStrength 0..1.\n"
                        L"; AsphaltFloor: paved grip every tire gets at least, before the modes (0 = off).\n"
                        L"; Per mode: multipliers for grip on dirt (OnModelFriction), gravel (GravelFriction), sand (SandFriction),\n"
                        L"; rock (RockFriction), paved ground (BodyFrictionAsphalt) and in mud (SubstanceFriction),\n"
                        L"; AdditionalRadiusOffset in metres off the tire's radius, and multipliers for fuel use while moving\n"
                        L"; (FuelConsumptionModifier) and steering speed (SteeringSpeedModifier). The game only draws a tire so\n"
                        L"; flat: on a tire with less room than the deepest mode asks for, every mode's offset shrinks alike.\n"
                        L"; RollingRadius=1: a flattened tire is a smaller wheel in the game and would lose about three times\n"
                        L"; the speed a real tire loses; the gears make up the difference. 0 = the smaller wheel as it is.\n"
                        L"; TireDamage=1: faster than MinVel (metres per second; 10 = 36 km/h) the tires wear: every DamageTick\n"
                        L"; seconds each wheel on the ground takes MinDamage, rising to MaxDamage at MaxVel. A wheel takes about\n"
                        L"; 50 before the tire is flat. MaxDamage=0 or TireDamage=0: none.\n");
            fclose(f);
            WriteIni(s);
            for (const SoundVoiceKind &v : kSoundVoices) WritePrivateProfileStringW(L"TirePressure", v.key, L"", ini.c_str());
        }
        return;
    }
    const wchar_t *T = L"TirePressure";
    s.key = GetPrivateProfileIntW(T, L"Key", s.key, ini.c_str());
    for (int i = 0; i < 4; i++) s.panelKeys[i] = min(255, max(0, (int)GetPrivateProfileIntW(T, Wide(kPanelKeyNames[i]).c_str(), s.panelKeys[i], ini.c_str())));
    s.beep = GetPrivateProfileIntW(T, L"Beep", 1, ini.c_str()) != 0;
    s.seconds = min(60.0f, max(0.0f, IniFloat(T, L"Seconds", s.seconds, ini)));
    // An ini from before 1.1.0 holds the 3 seconds every ini was written with then. The change now takes longer, with
    // sounds made for that, so those 3 seconds become the new time, once: the file then says so and carries the version.
    if ((int)GetPrivateProfileIntW(T, L"IniVersion", 1, ini.c_str()) < kIniVersion)
    {
        if (s.seconds == kSecondsBefore)
        {
            s.seconds = kSecondsDefault;
            Log(L"settings: an ini from before 1.1.0 with a pressure change of %g s: it takes %g s now", kSecondsBefore, kSecondsDefault);
        }
        wchar_t b[32];
        swprintf_s(b, L"%g", s.seconds);
        WritePrivateProfileStringW(T, L"Seconds", b, ini.c_str());
        swprintf_s(b, L"%d", kIniVersion);
        WritePrivateProfileStringW(T, L"IniVersion", b, ini.c_str());
    }
    s.soundVolume = min(100.0f, max(0.0f, IniFloat(T, L"SoundVolume", s.soundVolume, ini)));
    s.ui = GetPrivateProfileIntW(T, L"UI", 1, ini.c_str()) != 0;
    s.uiScale = min(3.0f, max(0.3f, IniFloat(T, L"UIScale", s.uiScale, ini)));
    s.confirmSeconds = min(30.0f, max(0.0f, IniFloat(T, L"ConfirmSeconds", s.confirmSeconds, ini)));
    for (int i = 0; i < 5; i++)
    {
        wchar_t buf[64] = {};
        GetPrivateProfileStringW(T, Wide(kPadKeys[i]).c_str(), Wide(s.pad[i]).c_str(), buf, 64, ini.c_str());
        size_t n = 0;
        for (const wchar_t *p = buf; *p && n + 1 < sizeof s.pad[i]; p++) s.pad[i][n++] = *p < 128 ? (char)*p : '?';
        s.pad[i][n] = 0;
    }
    for (int i = 0; i < 3; i++) s.base[i] = min(5.0f, max(0.1f, IniFloat(T, kBaseKeys[i], s.base[i], ini)));
    s.vanillaBalance = GetPrivateProfileIntW(T, L"VanillaBalance", 0, ini.c_str()) != 0;
    s.balanceStrength = min(1.0f, max(0.0f, IniFloat(T, L"VanillaBalanceStrength", s.balanceStrength, ini)));
    s.asphaltFloor = min(10.0f, max(0.0f, IniFloat(T, L"AsphaltFloor", s.asphaltFloor, ini)));
    s.increased = GetPrivateProfileIntW(T, L"Increased", s.increased ? 1 : 0, ini.c_str()) != 0;
    s.tireDamage = GetPrivateProfileIntW(T, L"TireDamage", s.tireDamage ? 1 : 0, ini.c_str()) != 0;
    s.rollingRadius = GetPrivateProfileIntW(T, L"RollingRadius", s.rollingRadius ? 1 : 0, ini.c_str()) != 0;
    for (int m = 0; m < 3; m++)
        for (int k = 0; k < kModeKeyCount; k++)
            s.mode[m][k] = min(kModeRange[k].hi, max(kModeRange[k].lo, IniFloat(kModeSections[m], Wide(kModeKeys[k]), s.mode[m][k], ini)));
}

// Puts settings in force: the key, the panel, the pad bindings, the modes with the base grip in them.
static void ApplySettings(const TpSettings &s, bool log)
{
    g_modeCount = s.increased ? 4 : 3;
    g_view.count = g_modeCount;
    g_asphaltFloor = s.asphaltFloor;
    g_key = s.key;
    g_beep = s.beep;
    g_seconds = s.seconds;
    g_soundVolume = s.soundVolume / 100.0f;
    g_uiWanted = s.ui;
    g_ui = g_uiWanted && g_reshadeOk;
    g_panelScale = s.uiScale;
    g_confirmMs = (int)(s.confirmSeconds * 1000.0f + 0.5f);
    g_panelConfirmMs = g_confirmMs;
    uint32_t *pads[5] = { &g_padOpen, &g_padLower, &g_padRaise, &g_padConfirm, &g_padCancel };
    for (int i = 0; i < 5; i++) *pads[i] = PadParse(s.pad[i]);
    if (g_ui) PadMasks(g_padOpen, g_padLower, g_padRaise, g_padConfirm, g_padCancel);
    else PadMasks(0, 0, 0, 0, 0);
    KeyName(g_key, g_panelKeys.open, sizeof g_panelKeys.open);
    // the keyboard's keys for the open panel, and their names on it (none: no name, the panel then shows none)
    char *const names[3] = { g_panelKeys.lower, g_panelKeys.raise, g_panelKeys.confirm };
    for (int i = 0; i < 4; i++)
    {
        g_stepKeys[i] = s.panelKeys[i];
        if (i < 3 && s.panelKeys[i]) KeyName(s.panelKeys[i], names[i], sizeof g_panelKeys.lower);
        else if (i < 3) names[i][0] = 0;
    }
    g_balanceOn = s.vanillaBalance;
    g_balanceStrength = s.balanceStrength;
    g_tireDamage = s.tireDamage;
    g_rollingRadius = s.rollingRadius;
    Mode &normal = g_modes[kNormal];
    normal.body = normal.asphalt = normal.substance = normal.fuel = normal.steering = normal.gravel = normal.sand = normal.rock = 1.0f;
    normal.radiusOffset = 0.0f;
    g_ticks[kNormal] = Tick{};
    for (int m = 0; m < 3; m++)
    {
        Mode &d = g_modes[kSettingsModes[m]];
        const float *k = s.mode[m];
        d.body = k[kModeGround];
        d.asphalt = k[kModeAsphalt];
        d.substance = k[kModeMud];
        d.radiusOffset = k[kModeOffset];
        d.fuel = k[kModeFuel];
        d.steering = k[kModeSteering];
        d.gravel = k[kModeGravel];
        d.sand = k[kModeSand];
        d.rock = k[kModeRock];
        Tick &t = g_ticks[kSettingsModes[m]];
        t = Tick{ k[kModeMinVel], k[kModeMaxVel], k[kModeDamageTick], (int)k[kModeMinDamage], (int)k[kModeMaxDamage] };
        if (log)
        {
            Log(L"%s: dirt x%g, gravel x%g, sand x%g, rock x%g, asphalt x%g, mud x%g, radius offset %g, fuel x%g, steering speed x%g", d.name, d.body,
                d.gravel, d.sand, d.rock, d.asphalt, d.substance, d.radiusOffset, d.fuel, d.steering);
            if (s.tireDamage && t.maxDamage > 0 && t.minVel > 0.0f)
                Log(L"%s: tire damage %d to %d every %g s from %.0f to %.0f km/h", d.name, t.minDamage, t.maxDamage, t.seconds, t.minVel * 3.6f, t.maxVel * 3.6f);
        }
    }
    g_deepest = 0.0f; // among the modes in use: Increased switched off keeps its numbers but does not count
    for (int m = 0; m < g_modeCount; m++) g_deepest = max(g_deepest, g_modes[m].radiusOffset);
    // base grip: some trucks (modded ones mostly) grip too much as they come; these scale every mode's grip, Normal too
    for (Mode &d : g_modes)
    {
        d.body *= s.base[0];
        d.gravel *= s.base[0];
        d.sand *= s.base[0];
        d.rock *= s.base[0];
        d.asphalt *= s.base[1];
        d.substance *= s.base[2];
    }
    for (int m = 0; m < kModeCount; m++)
    {
        g_panelModes[m].ground = g_modes[m].body;
        g_panelModes[m].asphalt = g_modes[m].asphalt;
        g_panelModes[m].mud = g_modes[m].substance;
        g_panelModes[m].fuel = g_modes[m].fuel;
    }
    if (log && (s.base[0] != 1.0f || s.base[1] != 1.0f || s.base[2] != 1.0f))
        Log(L"base grip for every mode: ground x%g, asphalt x%g, mud x%g", s.base[0], s.base[1], s.base[2]);
    if (log && s.vanillaBalance) Log(L"vanilla balance on, strength %g", s.balanceStrength);
    if (log && s.asphaltFloor > 0.0f) Log(L"asphalt floor %g", s.asphaltFloor);
    if (log) Log(L"rolling radius: %s", s.rollingRadius ? L"the gears make up for the flattened wheels, but for what a real tire loses" : L"off, the smaller wheel as it is");
    if (log) Log(L"modes: Low, Reduced, Normal%s", s.increased ? L", Increased" : L"");
}

void SettingsGet(TpSettings &out)
{
    AcquireSRWLockShared(&g_setLock);
    out = g_set;
    ReleaseSRWLockShared(&g_setLock);
}

void SettingsPut(const TpSettings &in)
{
    AcquireSRWLockExclusive(&g_setLock);
    g_set = in;
    ReleaseSRWLockExclusive(&g_setLock);
    InterlockedExchange(&g_setDirty, 1);
}

void SettingsReload() { InterlockedExchange(&g_reloadAsked, 1); }

void LiveGet(TpLive &out)
{
    AcquireSRWLockShared(&g_setLock);
    out = g_live;
    ReleaseSRWLockShared(&g_setLock);
}

// Which sounds the system has (sound.h): the ini's AirOutSound, AirInSound, CompressorSound and CompressorStopSound,
// by hand only.
static void LoadSoundNames()
{
    const std::wstring ini = g_dir + L"TirePressure.ini";
    std::wstring names[kVoiceCount];
    for (int k = 0; k < kVoiceCount; k++)
    {
        wchar_t name[MAX_PATH] = {};
        GetPrivateProfileStringW(L"TirePressure", kSoundVoices[k].key, L"", name, MAX_PATH, ini.c_str());
        names[k] = name;
    }
    SoundNames(names);
}

static void LoadIni()
{
    TpSettings s;
    ReadIni(s);
    AcquireSRWLockExclusive(&g_setLock);
    g_set = s;
    ReleaseSRWLockExclusive(&g_setLock);
    ApplySettings(s, true);
    LoadSoundNames();
    // diagnostics, by hand only (not on the settings page)
    const std::wstring ini = g_dir + L"TirePressure.ini";
    g_probeFriction = GetPrivateProfileIntW(L"TirePressure", L"ProbeFriction", 0, ini.c_str());
    g_probeWriters = min(3600, max(0, (int)GetPrivateProfileIntW(L"TirePressure", L"ProbeWriters", 0, ini.c_str())));
    g_probeFuel = GetPrivateProfileIntW(L"TirePressure", L"ProbeFuel", 0, ini.c_str());
    g_probeDamage = (int)GetPrivateProfileIntW(L"TirePressure", L"ProbeDamage", -1, ini.c_str());
    g_probeDamageBack = max(0, (int)GetPrivateProfileIntW(L"TirePressure", L"ProbeDamageBack", 0, ini.c_str()));
    if (g_probeDamage >= 0) Log(L"probe: ProbeDamage=%d (every wheel's damage is set to it 10 s after a truck is found), ProbeDamageBack=%d s", g_probeDamage, g_probeDamageBack);
    g_probeDrive = GetPrivateProfileIntW(L"TirePressure", L"ProbeDrive", 0, ini.c_str());
    g_probeByHand = GetPrivateProfileIntW(L"TirePressure", L"ProbeByHand", 0, ini.c_str());
    wchar_t watch[64] = {};
    GetPrivateProfileStringW(L"TirePressure", L"ProbeWatch", L"", watch, 64, ini.c_str());
    const int parts = swscanf_s(watch, L"%llx,%llx,%llx", &g_watchA, &g_watchB, &g_watchC);
    if (parts < 1) g_watchA = -1;
    if (parts < 2) g_watchB = -1;
    if (parts < 3) g_watchC = -1;
    if (g_probeFriction || g_probeWriters || g_probeFuel)
        Log(L"probe: ProbeFriction=%d, ProbeWriters=%d s, ProbeFuel=%d, ProbeWatch=%s", g_probeFriction, g_probeWriters, g_probeFuel, watch[0] ? watch : L"(the wheel's friction)");
}

// model: the wheel model that owns the wheel object; index: its place in the vehicle's list of wheel models.
struct WheelRef { uint64_t wheel, body, shape, model; int index; };

// The driven truck's wheel objects, each checked by its vtable and its cylinder shape's vtable. of: the vehicle they
// were read from (0 = none): one read gives both, so a truck change in between cannot pair one truck's wheels with
// the other truck.
static std::vector<WheelRef> CurrentWheels(uint64_t *of = nullptr)
{
    std::vector<WheelRef> out;
    uint64_t control = 0, vehicle = 0, arr = 0, end = 0;
    if (of) *of = 0;
    if (!Read(g_base + g_build->control, control) || !Read(control + 8, vehicle) || !Read(vehicle + 0x200, arr) || !Read(vehicle + 0x208, end)) return out;
    if (of) *of = vehicle;
    const uint64_t count = end > arr && end - arr <= 64 * 8 ? (end - arr) / 8 : 0;
    for (uint64_t i = 0; i < count; i++)
    {
        uint64_t vt = 0;
        WheelRef w = {};
        w.index = (int)i;
        if (!Read(arr + i * 8, w.model) || !Read(w.model + 0x2C8, w.wheel) || !Read(w.wheel, vt) || vt != g_base + g_build->wheelVtable) continue;
        if (!Read(w.wheel + 0x18, w.body) || !Read(w.body + 0x20, w.shape) || !Read(w.shape, vt) || vt != g_base + g_build->cylinderVtable) continue;
        out.push_back(w);
    }
    return out;
}

// The driven truck's vehicle object.
static uint64_t CurrentVehicle()
{
    uint64_t control = 0, vehicle = 0;
    return Read(g_base + g_build->control, control) && Read(control + 8, vehicle) ? vehicle : 0;
}

// Whether a wheel found earlier in the pass is still that wheel: asked just before writing to it, as a successful
// read only says the memory is there, and the game may have rebuilt the wheel or its shape in the meantime.
static bool StillThere(const WheelRef &w)
{
    uint64_t vt = 0, wheel = 0, body = 0, shape = 0;
    return Read(w.model + 0x2C8, wheel) && wheel == w.wheel && Read(w.wheel, vt) && vt == g_base + g_build->wheelVtable && Read(w.wheel + 0x18, body) &&
           body == w.body && Read(w.body + 0x20, shape) && shape == w.shape && Read(w.shape, vt) && vt == g_base + g_build->cylinderVtable;
}

static bool IsWheel(uint64_t p) { uint64_t vt = 0; return Read(p, vt) && vt == g_base + g_build->wheelVtable; }
static bool IsWheelModel(uint64_t p) { uint64_t w = 0; return Read(p + 0x2C8, w) && IsWheel(w); }

// For the log when the chain above finds no wheels: every way from root to a wheel object within two pointer steps
// (as the wheel itself, as a wheel model, or as an array whose first entry is one of those).
static void SearchWheels(const wchar_t *name, uint64_t root, int &hits)
{
    auto kind = [](uint64_t p) -> const wchar_t * {
        uint64_t first = 0;
        if (IsWheel(p)) return L"wheel";
        if (IsWheelModel(p)) return L"wheel model";
        if (Read(p, first) && IsWheel(first)) return L"array of wheels";
        if (Read(p, first) && IsWheelModel(first)) return L"array of wheel models";
        return nullptr;
    };
    for (uint64_t a = 0; a < 0x1000 && hits < 60; a += 8)
    {
        uint64_t p = 0;
        if (!Read(root + a, p) || p < 0x10000) continue;
        if (const wchar_t *k = kind(p)) { Log(L"  %s+0x%llX = %s %p", name, a, k, (void *)p); hits++; continue; }
        for (uint64_t b = 0; b < 0x800 && hits < 60; b += 8)
        {
            uint64_t q = 0;
            if (!Read(p + b, q) || q < 0x10000) continue;
            if (const wchar_t *k = kind(q)) { Log(L"  %s+0x%llX -> +0x%llX = %s %p", name, a, b, k, (void *)q); hits++; }
        }
    }
}

static void Diagnose()
{
    uint64_t control = 0, vehicle = 0;
    Read(g_base + g_build->control, control);
    Read(control + 8, vehicle);
    uint64_t arr = 0, end = 0;
    Read(vehicle + 0x200, arr);
    Read(vehicle + 0x208, end);
    Log(L"diagnosis: control %p, vehicle %p, wheel models %p .. %p", (void *)control, (void *)vehicle, (void *)arr, (void *)end);
    for (int i = -4; i <= 4; i++)
    {
        uint64_t v = 0;
        Read(g_base + g_build->control + i * 8, v);
        Log(L"  global %+d: %p", i * 8, (void *)v);
    }
    int hits = 0;
    if (vehicle) SearchWheels(L"vehicle", vehicle, hits);
    if (control) SearchWheels(L"control", control, hits);
    Log(L"diagnosis: %d ways to a wheel", hits);
}

static bool Near(float a, float b) { return fabsf(a - b) <= 0.0005f * max(1.0f, fabsf(b)); }

// How far the hub of a tire of radius R can sit below that radius, on hard ground, before the game draws the tread
// under the ground. The game flattens the drawn tire by a squash it takes from the hub's height over the contact
// (the wheel model's update, RVA 0xc235f0): min(1.5 h, 0.25 R) + 0.02 for a hub h below R. Its wheel vertex shader
// (SpinTires/TruckWheel.fxs, g_softParams.w) pulls the tread in by squash x (0.8 - 1.1 a / zone) at the angle a from
// the contact, zone = (min(0.2 R, squash) + 0.1) x 0.8 / R half turns. A flat contact needs R - (R - h) / cos a
// there: past some h the squash stops growing and the tread sinks in on both sides of the contact (2026-10-04, a
// scout on 0.47 m tires at 10 cm more: 37 mm under the ground). Half a percent of R under the ground is let through:
// nobody sees it, and on very large wheels the shader's shape misses a flat contact by that much early on.
static float FlattenLimit(float R)
{
    float best = 0.0f;
    for (float h = 0.001f; h < R * 0.5f; h += 0.001f)
    {
        const float squash = min(1.5f * h, 0.25f * R) + 0.02f;
        const float zone = (min(0.2f * R, squash) + 0.1f) * 0.8f / R * 3.14159265f;
        const float end = acosf((R - h) / R);
        bool fits = true;
        for (float a = 0.0f; a <= end && fits; a += 0.00436f) // a quarter of a degree
            fits = R - (R - h) / cosf(a) <= squash * (0.8f - 1.1f * min(1.0f, a / zone)) + 0.005f * R;
        if (!fits) break;
        best = h;
    }
    return best;
}

// Expeditions (its set-pressure-mode function, RVA 0xcd40c0 there) takes the mode's AdditionalRadiusOffset off the
// collision radius as it is, in metres, not scaled by the wheel. SnowRunner only draws a tire so flat (FlattenLimit):
// on a tire with less room than the deepest mode asks for, every mode's offset shrinks by the same share, so the
// deepest one takes all the room there is. The cylinder keeps at least 40% of the wheel radius whatever the ini says.
static float Cylinder(const Stock &s, const Mode &m, float radius)
{
    const float share = g_deepest > s.room ? s.room / g_deepest : 1.0f;
    return max(radius * 0.4f, s.cylinder - m.radiusOffset * share);
}

// What a wheel's gear speeds are due, for the cylinder written to it. A real tire's belt keeps its length: flattened,
// it rolls nearly as far per turn as before. With the hub h below the tire's radius R the contact reaches R sin p to
// either side (cos p = (R - h) / R) while the belt runs R p, so the tire rolls as a wheel of radius R sin p / p. The
// game's wheel is the collision cylinder, R - h, which loses about three times as much speed (the scout of
// 2026-10-04 at 6.7 cm: 14.9% for a real tire's 4.9%). The factor is what the gears have to make up: 1 at stock.
static float RollFactor(const Stock &s)
{
    if (!(s.radius > 0.0f) || !(s.cylinder > 0.0f) || !(s.writtenCylinder > 0.0f)) return 1.0f;
    auto rolling = [&](float cylinder) {
        const float p = acosf(min(1.0f, max(0.3f, cylinder / s.radius)));
        return p < 0.01f ? s.radius : s.radius * sinf(p) / p;
    };
    return rolling(s.writtenCylinder) / rolling(s.cylinder) * s.cylinder / s.writtenCylinder;
}

// Brings one wheel to the factors m. A wheel not seen before, or one whose values are not the ones written last (the
// game rebuilt it at the same address), gives its current values as stock first.
static bool ApplyWheel(const WheelRef &w, const Mode &m, bool logWrite, bool announce)
{
    // The body's own friction (body+0xCC, the grip on plain ground) is NOT written: the game sets it itself while driving
    // (per surface), and a build that wrote it took the changed value for a rebuilt wheel, took the scaled values as new
    // stock and scaled them again, over and over (2026-10-02: frictions in the thousands, flat tires, a rolled truck).
    float asphalt, substance, cylinder, radius, offset;
    BYTE broken = 0;
    // a broken tire (the wheel model's flag, +0x328) is left as it is until the game repairs it, as Expeditions leaves it
    if (Read(w.model + 0x328, broken) && broken) return false;
    if (!Read(w.wheel + 0xA4, asphalt) || !Read(w.wheel + 0xA0, substance) || !Read(w.shape + 0x28, cylinder) || !Read(w.wheel + 0x94, radius) || !Read(w.wheel + 0xB4, offset)) return false;
    // the stock cylinder is known without a capture: the wheel object keeps the radius and offset it was built from
    const float stockCylinder = radius - offset;
    if (!(radius > 0.05f && radius < 5.0f) || !(offset >= 0.0f && offset <= radius * 0.3f)) return false;
    auto it = g_wheels.find(w.wheel);
    if (it != g_wheels.end())
    {
        Stock &s = it->second;
        const Mode &o = s.written;
        if (!Near(asphalt, s.asphalt * o.asphalt) || !Near(substance, s.substance * o.substance)) it = g_wheels.end();
        else if (!Near(cylinder, s.writtenCylinder))
        {
            // The frictions are the ones written, the cylinder is not: the wheel has another shape. A tire the game
            // repaired (RVA 0xc25ad0 builds the shape anew) comes back with a cylinder of the stock size and the
            // written frictions still on it, so only the flattening is due again. (Taken for a fresh wheel it would
            // give the scaled frictions as stock.)
            // The same holds for another wheel the game built at this address with the same grip values, so the
            // tire's size is taken again as well.
            if (!Near(cylinder, stockCylinder)) return false;
            s.cylinder = s.writtenCylinder = stockCylinder;
            s.written.radiusOffset = 0.0f;
            s.room = max(0.0f, FlattenLimit(radius) - offset);
            s.radius = radius;
        }
    }
    if (it == g_wheels.end())
    {
        // stock is only ever taken from a wheel that looks like the game just built it: its cylinder at the stock size
        // and frictions inside what a tire file can hold. Anything else is left alone.
        if (!Near(cylinder, stockCylinder) || !(asphalt > 0.0f && asphalt <= 10.5f) || !(substance >= 0.0f && substance <= 10.5f))
        {
            static int warned = 0;
            if (warned++ < 8) Log(L"wheel %p left alone: asphalt %.3f, mud %.3f, cylinder %.4f (stock size %.4f) are not a fresh wheel's", (void *)w.wheel, asphalt, substance, cylinder, stockCylinder);
            g_wheels.erase(w.wheel);
            return false;
        }
        it = g_wheels.insert_or_assign(w.wheel, Stock{ 0.0f, asphalt, substance, stockCylinder, kStockMode, stockCylinder,
                                                       max(0.0f, FlattenLimit(radius) - offset), radius }).first;
    }
    Stock &s = it->second;
    const Mode &o = s.written;
    // the cylinder goes by the radius written last, not by the mode's offset: the share of it a tire gets changes
    // when the settings page changes the deepest mode
    const float newCylinder = Cylinder(s, m, radius);
    const bool same = o.asphalt == m.asphalt && o.substance == m.substance && newCylinder == s.writtenCylinder;
    if (!same)
    {
        if (!StillThere(w)) return false;
        // Each value is noted as it goes in, so the record says what is on the wheel even when a write fails part way.
        // (Dropping the record there let the next pass take a friction already scaled for the tire's own.)
        if (!Write(w.wheel + 0xA4, s.asphalt * m.asphalt)) return false;
        s.written.asphalt = m.asphalt;
        if (!Write(w.wheel + 0xA0, s.substance * m.substance)) return false;
        s.written.substance = m.substance;
        if (!Write(w.shape + 0x28, newCylinder)) return false;
        s.written = m;
        s.writtenCylinder = newCylinder;
    }
    // announce = the end of a mode change: every wheel gets its line, also one whose last value went in a few steps
    // earlier (2026-10-02: Reduced ended with no wheel lines in the log)
    if (announce || (logWrite && !same))
        Log(L"wheel %p -> %s: asphalt %.3f, mud %.3f, cylinder %.4f (stock %.3f, %.3f, %.4f; the game draws this tire up to %.3f flatter)", (void *)w.wheel,
            m.name, s.asphalt * m.asphalt, s.substance * m.substance, newCylinder, s.asphalt, s.substance, s.cylinder, s.room);
    return !same;
}

// The truck's own values a mode scales (exe stamp 0x6a607c05): the fuel rate function (RVA 0xd670b0) multiplies by
// vehicle+0xE38 plus vehicle+0xE3C (the engine's and the gearbox's FuelConsumption), and the per-frame vehicle update reads
// SteerSpeed at vehicle+0x69C. Each value keeps its own stock: one the game changed gives its new value as stock, the
// others keep theirs, so nothing scaled can be taken for stock (see ApplyWheel for what that did once).
struct Scaled { float stock, written; };
struct VehicleStock { Scaled fuelA, fuelB, steering; std::vector<Scaled> ground, gears; };
static std::unordered_map<uint64_t, VehicleStock> g_vehicles;

static void ApplyValue(uint64_t addr, Scaled &s, float factor, float lowest, float highest, float cap = 3.0e38f)
{
    float now;
    if (!Read(addr, now)) return;
    if (!(now == s.written))
    {
        // not what was written last (or never written): the game's own value
        if (!(now >= lowest && now <= highest)) return;
        s.stock = s.written = now;
    }
    const float want = min(cap, s.stock * factor);
    if (want != s.written && Write(addr, want)) s.written = want;
}

// ground: the ground grip factor of each wheel (ground grip index = wheel index): the mode's factor for the ground
// under it times its vanilla balance factor. roll: the factor for the gear speeds (RollFactor, the wheels' mean).
static void ApplyVehicle(uint64_t vehicle, size_t wheelCount, const Mode &m, bool announce, const std::vector<float> &ground, float roll)
{
    if (!vehicle) return;
    auto it = g_vehicles.find(vehicle);
    const bool first = it == g_vehicles.end();
    if (first)
    {
        const float none = nanf("");
        it = g_vehicles.insert({ vehicle, VehicleStock{ { none, none }, { none, none }, { none, none } } }).first;
    }
    VehicleStock &v = it->second;
    ApplyValue(vehicle + 0xE38, v.fuelA, m.fuel, 0.0f, 1000.0f);
    ApplyValue(vehicle + 0xE3C, v.fuelB, m.fuel, 0.0f, 1000.0f);
    ApplyValue(vehicle + 0x69C, v.steering, m.steering, 0.0001f, 10.0f);
    // Plain-ground grip: when a wheel is not on asphalt (byte wheel+0x1C8), the contact function (RVA 0xc402d0) sets the
    // body friction from a float per wheel, the tire BodyFriction, in the vector at [vehicle+0x68]+0x98 .. +0xA0 (the
    // truck action). Expeditions keeps its scaled frictions between 0.05 and 10; so does this.
    uint64_t action = 0, begin = 0, end = 0;
    Read(vehicle + 0x68, action);
    const bool groundOk = wheelCount && Read(action + 0x98, begin) && Read(action + 0xA0, end) && end > begin && end - begin == wheelCount * 4;
    if (groundOk)
    {
        if (v.ground.size() != wheelCount) v.ground.assign(wheelCount, Scaled{ nanf(""), nanf("") });
        for (size_t i = 0; i < wheelCount; i++) ApplyValue(begin + i * 4, v.ground[i], i < ground.size() ? ground[i] : m.body, 0.05f, 10.0f, 10.0f);
    }
    // Gear speeds: the truck action's list of wheel turning speeds, one per gear (reverse, the gears, the high gear),
    // at [vehicle+0x68]+0x58 .. +0x60. The gearbox setup fills it (RVA 0xd763b0) and the drive reads the current
    // gear's (index at +0x74) every frame (RVA 0xd717e0). A fitted gearbox gives new values, taken as stock.
    uint64_t gearBegin = 0, gearEnd = 0;
    const bool gearsOk = Read(action + 0x58, gearBegin) && Read(action + 0x60, gearEnd) && gearEnd > gearBegin && (gearEnd - gearBegin) % 4 == 0 &&
                         gearEnd - gearBegin <= 32 * 4;
    if (gearsOk)
    {
        const size_t count = (size_t)(gearEnd - gearBegin) / 4;
        if (v.gears.size() != count) v.gears.assign(count, Scaled{ nanf(""), nanf("") });
        for (size_t i = 0; i < count; i++) ApplyValue(gearBegin + i * 4, v.gears[i], roll, -1000.0f, 1000.0f);
    }
    if (first || announce)
    {
        Log(L"vehicle %p%s%s: fuel consumption %.4f + %.4f (stock %.4f + %.4f), steering speed %.5f (stock %.5f)", (void *)vehicle, announce ? L" -> " : L" found",
            announce ? m.name : L"", v.fuelA.written, v.fuelB.written, v.fuelA.stock, v.fuelB.stock, v.steering.written, v.steering.stock);
        if (gearsOk) Log(L"vehicle %p gear speeds x%.3f, %zu of them, the first forward one: %.3f (stock %.3f)", (void *)vehicle, roll, v.gears.size(),
                         v.gears.size() > 1 ? v.gears[1].written : v.gears[0].written, v.gears.size() > 1 ? v.gears[1].stock : v.gears[0].stock);
        else Log(L"vehicle %p gear speeds NOT found: action %p, list %p .. %p", (void *)vehicle, (void *)action, (void *)gearBegin, (void *)gearEnd);
        if (groundOk) Log(L"vehicle %p ground grip of %zu wheels, the first: %.3f (stock %.3f)", (void *)vehicle, wheelCount, v.ground[0].written, v.ground[0].stock);
        else Log(L"vehicle %p ground grip NOT found: action %p, list %p .. %p for %zu wheels", (void *)vehicle, (void *)action, (void *)begin, (void *)end, wheelCount);
    }
}

// Moves factors one step of dt seconds towards a mode's; true once they are there.
static bool StepTowards(Mode &now, const Mode &to, float dt)
{
    bool there = true;
    auto step = [&](float &v, float target, float span) {
        const float d = g_seconds > 0.0f ? span * dt / g_seconds : span;
        if (fabsf(target - v) <= d || d <= 0.0f) v = target;
        else { v += target > v ? d : -d; there = false; }
    };
    // span: Normal <-> Low (or Increased, if that is further) takes Seconds, a smaller change takes its share of that
    const Mode &n = g_modes[kNormal], &l = g_modes[kLow], &h = g_modes[kIncreased];
    auto span = [](float normal, float low, float high, float least) { return max(least, max(fabsf(low - normal), fabsf(high - normal))); };
    step(now.body, to.body, span(n.body, l.body, h.body, 0.01f));
    step(now.asphalt, to.asphalt, span(n.asphalt, l.asphalt, h.asphalt, 0.01f));
    step(now.substance, to.substance, span(n.substance, l.substance, h.substance, 0.01f));
    step(now.radiusOffset, to.radiusOffset, span(n.radiusOffset, l.radiusOffset, h.radiusOffset, 0.001f));
    step(now.fuel, to.fuel, span(n.fuel, l.fuel, h.fuel, 0.01f));
    step(now.steering, to.steering, span(n.steering, l.steering, h.steering, 0.01f));
    step(now.gravel, to.gravel, span(n.gravel, l.gravel, h.gravel, 0.01f));
    step(now.sand, to.sand, span(n.sand, l.sand, h.sand, 0.01f));
    step(now.rock, to.rock, span(n.rock, l.rock, h.rock, 0.01f));
    if (there) now = to;
    return there;
}

static const float kPass = 0.05f; // the main loop's step in seconds

// The air for a filling comes out of a tank, and a compressor fills the tank again once it has fallen far enough. So
// the compressor does not run at every filling: at every second where the pressure goes up by two modes or more
// (Low to Normal), at every fourth where it goes up by one. Then it runs on until the tank is full, a quarter of a
// minute or so, and lets its own air go as it stops. level: 1 = a full tank.
static const float kTankUse = 0.17f;    // of the tank per mode the pressure is raised by
static const float kTankCutIn = 0.45f;  // below this the compressor starts
static const float kTankRefill = 0.04f; // of the tank per second while it runs
struct AirTank { float level = 1.0f; bool compressor = false; };

// One pass: the pressure went up by `raised` modes, dt seconds went by. True: the compressor has just stopped.
static bool TankStep(AirTank &t, float raised, float dt)
{
    t.level = max(0.0f, t.level - raised * kTankUse);
    if (!t.compressor)
    {
        t.compressor = t.level < kTankCutIn;
        return false;
    }
    t.level += kTankRefill * dt;
    if (t.level < 1.0f) return false;
    t.level = 1.0f;
    t.compressor = false;
    return true;
}

// Moves the factors in force one pass towards the chosen mode; true once they are there.
static bool StepTowardsMode() { return StepTowards(g_now, g_modes[g_mode], kPass); }

// How long the factors in force take to the chosen mode, in seconds of passes; 0: they are there, or get there in one.
static float SecondsToMode()
{
    Mode now = g_now;
    int passes = 1;
    while (passes < 100000 && !StepTowards(now, g_modes[g_mode], kPass)) passes++;
    return passes > 1 ? (float)passes * kPass : 0.0f;
}

static bool SameFactors(const Mode &a, const Mode &b)
{
    return a.body == b.body && a.asphalt == b.asphalt && a.substance == b.substance && a.radiusOffset == b.radiusOffset && a.fuel == b.fuel &&
           a.steering == b.steering && a.gravel == b.gravel && a.sand == b.sand && a.rock == b.rock;
}

// The mode's ground grip factor for the ground under a wheel. The game's per-frame ground check (RVA 0xc22f40, through
// 0xbcd720) blends the terrain layers under the wheel and leaves a flag where a kind makes up more than half:
// wheel+0x1C9 gravel, +0x1CA sand, +0x1CD hard (the main layer does not deform: rock, stone). Paved ground (+0x1C8)
// has its own grip value on the wheel, and mud its own.
static float GroundFactor(const Mode &m, uint64_t wheel)
{
    BYTE f[6] = {}; // +0x1C8 .. +0x1CD
    if (!Read(wheel + 0x1C8, f)) return m.body;
    return f[2] ? m.sand : f[1] ? m.gravel : f[5] ? m.rock : m.body;
}

// The truck's forward speed in m/s, as the game's dashboard takes it (RVA 0xd71790): the chassis body's velocity
// along its forward axis. chassis = [[vehicle+0x60]+0x230] (a Havok rigid body): velocity +0x230, the axis +0x170,
// its position +0x1A0 (pos: a paused game keeps the velocity but the truck stays where it is).
static bool ForwardSpeed(uint64_t vehicle, float &speed, float pos[3])
{
    uint64_t model = 0, chassis = 0;
    float v[3], axis[3], p[3];
    if (!Read(vehicle + 0x60, model) || !Read(model + 0x230, chassis) || !Read(chassis + 0x230, v) || !Read(chassis + 0x170, axis) || !Read(chassis + 0x1A0, p)) return false;
    speed = v[0] * axis[0] + v[1] * axis[1] + v[2] * axis[2];
    memcpy(pos, p, sizeof p);
    return fabsf(speed) < 200.0f; // false for a NaN too
}

// ---- tire damage at speed ----
// The game keeps a part's damage in a shared data block behind a handle (its accessor is RVA 0xa749a0): the block is
// the handle without bits 48 to 55, the word at +0x10 says whether it holds data (bit 0, and not bit 2), and the data
// starts 0x38 in (a 0x30 header for data aligned to 16 or less, RVA 0x164fc60, then 8).
static uint64_t BlockData(uint64_t handleAt)
{
    uint64_t block = 0;
    WORD flags = 0;
    if (!Read(handleAt, block)) return 0;
    block &= 0xFF00FFFFFFFFFFFFull;
    if (!block || !Read(block + 0x10, flags) || (flags & 4) || !(flags & 1)) return 0;
    return block + 0x38;
}

// A wheel's damage: { int damage, int capacity, float effect }, damage == capacity is a broken tire. The handles of
// all wheels are a list behind the handle at vehicle+0x218 (pointer at +8, count at +0x10; the damage update, RVA
// 0xd6f690, walks it). Taken only when the capacity is the one the wheel model was built with (model+0x324, written
// to the block by RVA 0xd77524), so a wrong guess about the layout cannot write anywhere.
static uint64_t WheelDamage(uint64_t vehicle, const WheelRef &w, int &damage, int &capacity)
{
    uint64_t list = 0;
    int count = 0, block[2] = {}, built = 0;
    const uint64_t all = BlockData(vehicle + 0x218);
    if (!all || !Read(all + 8, list) || !Read(all + 0x10, count) || w.index < 0 || w.index >= count) return 0;
    const uint64_t data = BlockData(list + (uint64_t)w.index * 8);
    if (!data || !Read(data, block) || !Read(w.model + 0x324, built) || built <= 0 || block[1] != built || block[0] < 0 || block[0] > built) return 0;
    damage = block[0];
    capacity = block[1];
    return data;
}

// A worn out tire made flat by hand, for when the game's own damage update cannot be reached (see OnTruckUpdate
// below). The game's break (RVA 0xc25ad0) builds a new Havok shape, which needs its own thread; this writes the values
// that count instead: the radius and the collision cylinder at the broken radius (wheel+0x9C), the body's angular
// damping for a broken wheel (wheel+0xA8, a Havok half float at body+0x216), and the wheel model's broken flag
// (+0x328, the old one at +0x329), so the game's damage update finds nothing left to do. A repair then rebuilds the
// wheel through the game's own code.
// A float as Havok keeps its half floats: the upper 16 bits, after the game's rounding factor.
static WORD HavokHalf(float v)
{
    const float scaled = v * 1.003906f;
    DWORD bits = 0;
    memcpy(&bits, &scaled, sizeof bits);
    return (WORD)(bits >> 16);
}

static bool BreakWheel(const WheelRef &w)
{
    float radius = 0, damping = 0;
    BYTE broken = 0;
    if (!Read(w.model + 0x328, broken) || broken || !Read(w.wheel + 0x9C, radius) || !Read(w.wheel + 0xA8, damping) || !(radius > 0.03f && radius < 5.0f)) return false;
    return StillThere(w) && Write(w.wheel + 0x94, radius) && Write(w.shape + 0x28, radius) && WriteAs(w.body + 0x216, HavokHalf(damping)) &&
           WriteAs(w.model + 0x329, broken) && WriteAs(w.model + 0x328, (BYTE)1);
}

// The reverse of BreakWheel, for the ProbeDamage diagnostic by hand only (a repair in the game goes through the game's
// own code): the radius and the cylinder back at the wheel's own size, the normal angular damping (wheel+0xAC), the flag off.
static bool UnbreakWheel(const WheelRef &w)
{
    float radius = 0, offset = 0, damping = 0;
    BYTE broken = 0;
    if (!Read(w.model + 0x328, broken) || !broken || !Read(w.wheel + 0x98, radius) || !Read(w.wheel + 0xB4, offset) || !Read(w.wheel + 0xAC, damping) || !(radius > 0.05f && radius < 5.0f)) return false;
    return StillThere(w) && Write(w.wheel + 0x94, radius) && Write(w.shape + 0x28, radius - offset) && WriteAs(w.body + 0x216, HavokHalf(damping)) &&
           WriteAs(w.model + 0x329, broken) && WriteAs(w.model + 0x328, (BYTE)0);
}

// The damage of one tick for a speed: MinDamage at MinVel rising to MaxDamage at MaxVel (Expeditions: RVA 0xccfdf0
// in its exe). With MaxVel at or below MinVel there is nothing to rise through: MaxDamage from MaxVel on.
static int DamageForSpeed(const Tick &tk, float speed)
{
    const float span = tk.maxVel - tk.minVel;
    const float t = span > 0.0001f ? min(1.0f, max(0.0f, (speed - tk.minVel) / span)) : speed >= tk.maxVel ? 1.0f : 0.0f;
    return (int)((float)tk.minDamage * (1.0f - t) + (float)tk.maxDamage * t);
}

// The game's own damage update (Build::damageUpdate, RVA 0xd6f690 in the Steam build; one argument: the vehicle). From
// each part's damage it makes a worn
// out tire flat or mends a repaired one, sets the parts' effects (a wheel's: 1 when flat), sums the damage
// (vehicle+0x108, the capacity at +0x10C) and tells the truck's script how much the sum changed (RVA 0xb27760). In
// the game that shows as the truck card coming up for a few seconds and, for a flat tire, the HUD's wheel icon turning
// red; damage only written to the wheels' data does neither. The game calls the update right after it changes a
// part's damage itself (its SetDamage, RVA 0xd6fe00: the value, then this).

// Deals damage to the wheels. add: every wheel on the ground (byte wheel+0x1A4) takes that much, as far as it has
// room (one tick; Expeditions: RVA 0xae1dbe in its exe). set >= 0: every wheel's damage becomes that instead (the
// ProbeDamage diagnostic). onGameThread: the game's damage update runs afterwards and does the rest; else a worn out
// tire is made flat by hand.
// known: wheels whose damage data was found; grounded: those on the ground; hit: those whose damage changed; flat:
// those worn out by it; most / capacity: the most damaged wheel afterwards.
struct TickResult { int known, grounded, hit, flat, most, capacity; };
static TickResult DealDamage(uint64_t vehicle, const std::vector<WheelRef> &wheels, int add, int set, bool onGameThread)
{
    TickResult r = {};
    for (const WheelRef &w : wheels)
    {
        BYTE contact = 0;
        int damage = 0, capacity = 0;
        const uint64_t data = WheelDamage(vehicle, w, damage, capacity);
        if (!data) continue;
        r.known++;
        const bool grounded = Read(w.wheel + 0x1A4, contact) && contact;
        if (grounded) r.grounded++;
        const int to = set >= 0 ? min(set, capacity) : grounded ? min(capacity, damage + max(0, add)) : damage;
        if (to != damage && WriteAs(data, to))
        {
            damage = to;
            r.hit++;
            if (to >= capacity) r.flat++;
            if (!onGameThread)
            {
                // by hand: the tire's state, and the damage's effect as the game's update sets it for a wheel (1 = flat)
                to >= capacity ? BreakWheel(w) : UnbreakWheel(w);
                WriteAs(data + 8, to >= capacity ? 1.0f : 0.0f);
            }
        }
        if (damage >= r.most) { r.most = damage; r.capacity = capacity; }
    }
    if (onGameThread && r.hit) ((void (*)(uint64_t))(g_base + g_build->damageUpdate))(vehicle);
    return r;
}

// ---- damage dealt on the game's own thread ----
// The driven truck's per-frame update (RVA 0xa56970; its first argument is an object with the vehicle at +0x20) asks
// Windows which window is in front (the call at RVA 0xa5752f, through the import slot at RVA 0x21be360) just before
// it updates the truck's controls, and it is one of the places where the game changes damage itself (the SetDamage
// call at RVA 0xa572c4). The import slot is pointed at ForegroundHook below (data, no code is changed): for that one
// caller it first deals the damage this mod's thread asked for and runs the game's damage update, on the game's
// thread and at a point of its frame where the game does the same. Every other caller just gets its answer.
// The RVAs above are the Steam build's; Build has each build's own (foregroundSlot, truckUpdateReturn).
// Build::firstArgument is the way from the address of that call's return address to the update's saved first
// argument: the function keeps it in its caller's home space (entry rsp + 8), and in the Steam build the call sits
// 0x410 below the entry rsp (8 pushes, a 0x3C8 frame, the call), which makes 0x418.
struct DamageJob { uint64_t vehicle; int add, set; float speed; ULONGLONG posted; };
static SRWLOCK g_jobLock = SRWLOCK_INIT;
static DamageJob g_job = {};        // under g_jobLock, with g_jobResult
static TickResult g_jobResult = {};
static volatile LONG g_jobPending = 0, g_jobDone = 0;
static volatile LONG g_truckUpdates = 0;         // times the game's truck update came through the hook
static volatile LONG64 g_truckUpdateVehicle = 0; // the vehicle it ran for when a job was last waiting
static volatile LONG g_truckUpdateThread = 0, g_truckUpdateThreadChanges = 0; // for the log: which thread it runs on
static HWND (WINAPI *g_foregroundReal)() = nullptr;

static void OnTruckUpdate(uint64_t returnSlot)
{
    InterlockedIncrement(&g_truckUpdates);
    const LONG thread = (LONG)GetCurrentThreadId();
    if (InterlockedExchange(&g_truckUpdateThread, thread) != thread) InterlockedIncrement(&g_truckUpdateThreadChanges);
    if (!g_jobPending) return;
    uint64_t first = 0, vehicle = 0;
    if (!Read(returnSlot + g_build->firstArgument, first) || !Read(first + 0x20, vehicle) || !vehicle) return;
    InterlockedExchange64(&g_truckUpdateVehicle, (LONG64)vehicle);
    if (!TryAcquireSRWLockExclusive(&g_jobLock)) return; // the mod's thread is posting: next frame
    // only for the truck the game is updating right now, and only while it is the one the mod's thread works on
    if (g_jobPending && g_job.vehicle == vehicle)
    {
        uint64_t driven = 0;
        const std::vector<WheelRef> wheels = CurrentWheels(&driven);
        if (driven == vehicle)
        {
            g_jobResult = DealDamage(vehicle, wheels, g_job.add, g_job.set, true);
            InterlockedExchange(&g_jobPending, 0);
            InterlockedExchange(&g_jobDone, 1);
        }
    }
    ReleaseSRWLockExclusive(&g_jobLock);
}

static HWND WINAPI ForegroundHook()
{
    if ((uint64_t)_ReturnAddress() == g_base + g_build->truckUpdateReturn) OnTruckUpdate((uint64_t)_AddressOfReturnAddress());
    return g_foregroundReal();
}

// Points the import slot at ForegroundHook, once the game's code is as this build has it: the call at the expected
// place through the expected slot, and the damage update's first bytes.
static bool HookTruckUpdate()
{
    static const BYTE kUpdateHead[8] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55 };
    BYTE call[6] = {}, head[8] = {};
    int rel = 0;
    void *real = nullptr;
    void **const slot = (void **)(g_base + g_build->foregroundSlot);
    if (g_foregroundReal) return true;
    if (!Read(g_base + g_build->truckUpdateReturn - 6, call) || call[0] != 0xFF || call[1] != 0x15) return false;
    memcpy(&rel, call + 2, 4);
    if (g_build->truckUpdateReturn + (int64_t)rel != g_build->foregroundSlot) return false;
    if (!Read(g_base + g_build->damageUpdate, head) || memcmp(head, kUpdateHead, sizeof head)) return false;
    MEMORY_BASIC_INFORMATION mi = {};
    if (!Read((uint64_t)slot, real) || !real || !VirtualQuery(real, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
        !(mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) return false;
    g_foregroundReal = (HWND (WINAPI *)())real;
    InterlockedExchangePointer(slot, (void *)ForegroundHook);
    VirtualProtect(slot, sizeof *slot, old, &old);
    return true;
}

// Hands a job to the game's thread; the answer is taken on a later pass of this thread (Run).
static void PostDamage(uint64_t vehicle, int add, int set, float speed)
{
    AcquireSRWLockExclusive(&g_jobLock);
    g_job = { vehicle, add, set, speed, GetTickCount64() };
    InterlockedExchange(&g_jobDone, 0);
    InterlockedExchange(&g_jobPending, 1);
    ReleaseSRWLockExclusive(&g_jobLock);
}

// The log line for damage dealt: a tick, or (set >= 0) the ProbeDamage diagnostic.
static void LogDamage(uint64_t vehicle, size_t wheelCount, const TickResult &r, int add, int set, float speed, bool onGameThread)
{
    int total = 0, totalCapacity = 0; // the game's own sums, which its damage update refreshes
    Read(vehicle + 0x108, total);
    Read(vehicle + 0x10C, totalCapacity);
    const wchar_t *how = onGameThread ? L"on the game's thread with its damage update after" : L"by hand";
    if (set >= 0)
        Log(L"probe: every wheel's damage set to %d, %s: %d of %zu changed, %d worn out (%d with damage data; the most worn has %d of %d; the game's sums %d / %d)", set,
            how, r.hit, wheelCount, r.flat, r.known, r.most, r.capacity, total, totalCapacity);
    else
        Log(L"tire damage: %d of %zu wheels took %d at %.0f km/h in %s, %s (%d with damage data, %d on the ground; the most worn has %d of %d; the game's sums %d / %d)%s",
            r.hit, wheelCount, add, speed * 3.6f, g_modes[g_mode].name, how, r.known, r.grounded, r.most, r.capacity, total, totalCapacity,
            r.flat ? L"; worn out and flat now" : L"");
}

// ---- diagnostics (ini ProbeFriction, ProbeWriters; both 0 unless set by hand) ----
// ProbeFriction=1 logs the first driven wheel's body friction (body+0xCC) whenever it changes: which values the game
// puts there on which ground. ProbeWriters=<seconds>: for that long, starting 2 s after the truck is first found, a
// hardware write breakpoint on that value (set on every thread and handled here, no debugger) lists the code that
// writes it. The breakpoint traps after the write, so the address is the next instruction; when that is a `ret` (a
// tiny setter) the return address is taken instead.
static volatile LONG g_everArmed = 0;
struct Writer { volatile LONG64 key; volatile LONG count; volatile LONG lastBits; volatile LONG viaRet; volatile LONG thread; };
static Writer g_writersSeen[48];

static LONG CALLBACK ProbeHandler(EXCEPTION_POINTERS *e)
{
    // once a breakpoint was ever set, every single-step exception is swallowed here: one that nobody handles ends the game
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !g_everArmed) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = e->ContextRecord;
    const bool ours = (c->Dr6 & 1) || ((c->Dr7 & 1) && c->Dr0);
    c->Dr6 &= ~(DWORD64)0xF;
    if (!ours || !c->Dr0) return EXCEPTION_CONTINUE_EXECUTION;
    const LONG bits = *(volatile LONG *)c->Dr0; // the value just written
    const bool viaRet = *(const BYTE *)c->Rip == 0xC3;
    const LONG64 key = viaRet ? *(const LONG64 *)c->Rsp : (LONG64)c->Rip;
    for (Writer &w : g_writersSeen)
    {
        LONG64 cur = w.key;
        if (cur == 0) cur = InterlockedCompareExchange64(&w.key, key, 0) == 0 ? key : w.key;
        if (cur == key) { InterlockedIncrement(&w.count); w.lastBits = bits; w.viaRet = viaRet; w.thread = (LONG)GetCurrentThreadId(); break; }
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Sets (addr != 0) or clears the write breakpoint in debug register 0 of every other thread of the game.
static void SetWatch(uint64_t addr, bool log = true)
{
    if (addr) InterlockedExchange(&g_everArmed, 1);
    const DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    int done = 0, failed = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof te };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        const HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) { failed++; continue; }
        // nothing that could take a lock happens between the suspend and the resume
        if (SuspendThread(t) != (DWORD)-1)
        {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            bool ok2 = GetThreadContext(t, &c) != 0;
            if (ok2)
            {
                c.Dr7 &= ~(DWORD64)0xF0003;                                        // breakpoint 0 off
                c.Dr0 = addr;
                if (addr) c.Dr7 |= 0x1 | ((DWORD64)0x1 << 16) | ((DWORD64)0x3 << 18); // on: writes, 4 bytes
                c.Dr6 = 0;
                ok2 = SetThreadContext(t, &c) != 0;
            }
            ResumeThread(t);
            ok2 ? done++ : failed++;
        }
        else failed++;
        CloseHandle(t);
    }
    CloseHandle(snap);
    if (log) Log(L"probe: write breakpoint %s %p on %d threads (%d failed)", addr ? L"set at" : L"cleared, was", (void *)addr, done, failed);
}

// Lists the writers: all of them (the summary), or only those not reported yet (a drive may end before the summary).
static void LogWriters(bool onlyNew)
{
    static bool reported[sizeof g_writersSeen / sizeof g_writersSeen[0]];
    int n = 0;
    for (size_t i = 0; i < sizeof g_writersSeen / sizeof g_writersSeen[0]; i++)
    {
        const Writer &w = g_writersSeen[i];
        if (!w.key) continue;
        n++;
        if (onlyNew && reported[i]) continue;
        reported[i] = true;
        float last;
        const LONG bits = w.lastBits;
        memcpy(&last, &bits, 4);
        const uint64_t key = (uint64_t)w.key;
        const bool inExe = key >= g_base && key < g_base + g_build->image;
        Log(L"probe: %swriter %s%s %llx, %ld writes, last value %.4f, on thread %lu", onlyNew ? L"new " : L"", w.viaRet ? L"(caller of a setter) " : L"", inExe ? L"exe rva" : L"address",
            inExe ? key - g_base : key, (long)w.count, last, (unsigned long)w.thread);
    }
    if (!onlyNew) Log(L"probe: %d writers", n);
}

// ProbeFuel=1: finds the truck's fuel amount. It copies the vehicle object, the objects it points to and the objects
// those point to, 3 s after the truck is found, again 11 s later and compares with the state 18 s after that: the floats
// that went down both times (fuel burns while the engine runs), as litres (1..5000, down by 0.001..30) or, with
// ProbeFuel=2, also as a share (0..1, down by 0.0005..0.3). ProbeWatch=<hex>[,<hex>[,<hex>]] then makes ProbeWriters watch vehicle+a, [vehicle+a]+b
// or [[vehicle+a]+b]+c instead of the wheel's friction: the code that burns the fuel.
struct Snap { uint64_t base; int a, b; std::vector<BYTE> first, second; }; // a, b: offsets of the pointers leading here, -1 = none
static std::vector<Snap> g_fuelSnap;

static bool ReadBlock(uint64_t addr, std::vector<BYTE> &out, size_t size)
{
    out.resize(size);
    SIZE_T got = 0;
    ReadProcessMemory(GetCurrentProcess(), (void *)addr, out.data(), size, &got); // a block that ends early still counts
    out.resize(got & ~(SIZE_T)7);
    return out.size() >= 0x40;
}

static void FuelSnapshot()
{
    g_fuelSnap.clear();
    const uint64_t v = CurrentVehicle();
    if (!v) return;
    std::unordered_map<uint64_t, int> seen;
    auto add = [&](uint64_t base, int a, int b, size_t size) {
        if (seen.count(base) || g_fuelSnap.size() >= 20000) return false;
        Snap s{ base, a, b, {}, {} };
        if (!ReadBlock(base, s.first, size)) return false;
        seen[base] = 1;
        g_fuelSnap.push_back(std::move(s));
        return true;
    };
    auto pointerAt = [&](size_t snap, size_t off) -> uint64_t {
        uint64_t p;
        memcpy(&p, g_fuelSnap[snap].first.data() + off, 8);
        return p < 0x10000 || p > 0x7FFFFFFFFFFFull || (p & 7) || (p >= g_base && p < g_base + g_build->image) || (p >= v && p < v + 0x4000) ? 0 : p;
    };
    if (!add(v, -1, -1, 0x4000)) return;
    for (size_t off = 0; off + 8 <= min(g_fuelSnap[0].first.size(), (size_t)0x2000); off += 8)
        if (const uint64_t p = pointerAt(0, off)) add(p, (int)off, -1, 0x2000);
    const size_t level1 = g_fuelSnap.size();
    for (size_t i = 1; i < level1; i++)
        for (size_t off = 0; off + 8 <= min(g_fuelSnap[i].first.size(), (size_t)0x800); off += 8)
            if (const uint64_t p = pointerAt(i, off)) add(p, g_fuelSnap[i].a, (int)off, 0x800);
    Log(L"probe: fuel snapshot of vehicle %p, %zu objects it points to and %zu objects those point to", (void *)v, level1 - 1, g_fuelSnap.size() - level1);
}

static void FuelSecond()
{
    for (Snap &s : g_fuelSnap) ReadBlock(s.base, s.second, s.first.size());
}

static void FuelCompare()
{
    int lines = 0;
    std::vector<BYTE> now;
    for (const Snap &s : g_fuelSnap)
    {
        if (s.second.empty() || !ReadBlock(s.base, now, s.first.size())) continue;
        const size_t n = min(now.size(), min(s.first.size(), s.second.size()));
        for (size_t off = 0; off + 4 <= n && lines < 1500; off += 4)
        {
            float a, b, c;
            memcpy(&a, s.first.data() + off, 4);
            memcpy(&b, s.second.data() + off, 4);
            memcpy(&c, now.data() + off, 4);
            if (!(c < b && b < a)) continue;
            const float d = a - c;
            const bool litres = a >= 1.0f && a <= 5000.0f && d > 0.001f && d < 30.0f, share = g_probeFuel > 1 && a > 0.001f && a <= 1.0f && d > 0.0005f && d < 0.3f;
            if (!litres && !share) continue;
            wchar_t path[96];
            if (s.a < 0) swprintf_s(path, L"vehicle+0x%zX", off);
            else if (s.b < 0) swprintf_s(path, L"[vehicle+0x%X]+0x%zX", s.a, off);
            else swprintf_s(path, L"[[vehicle+0x%X]+0x%X]+0x%zX", s.a, s.b, off);
            Log(L"probe: fuel candidate %s: %.5f -> %.5f -> %.5f", path, a, b, c);
            lines++;
        }
    }
    Log(L"probe: %d fuel candidates", lines);
    g_fuelSnap.clear();
}

// Beeps play on a thread of their own: Beep only returns when the sound is over, and the mod's loop must not stand
// still for it (three beeps for Low took 0.4 s: key and pad presses in that time were lost).
struct BeepNote { DWORD freq, ms, gap; };
static SRWLOCK g_beepLock = SRWLOCK_INIT;
static std::vector<BeepNote> g_beeps;
static HANDLE g_beepWake = nullptr;

static DWORD WINAPI BeepThread(void *)
{
    for (;;)
    {
        WaitForSingleObject(g_beepWake, INFINITE);
        for (;;)
        {
            BeepNote note = {};
            AcquireSRWLockExclusive(&g_beepLock);
            const bool any = !g_beeps.empty();
            if (any) { note = g_beeps.front(); g_beeps.erase(g_beeps.begin()); }
            ReleaseSRWLockExclusive(&g_beepLock);
            if (!any) break;
            Beep(note.freq, note.ms);
            if (note.gap) Sleep(note.gap);
        }
    }
}

// Queues a beep (and a pause after it); called from the mod's thread only. More than eight waiting are dropped.
static void BeepLater(DWORD freq, DWORD ms, DWORD gap = 0)
{
    if (!g_beepWake)
    {
        g_beepWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        const HANDLE t = g_beepWake ? CreateThread(nullptr, 0, BeepThread, nullptr, 0, nullptr) : nullptr;
        if (!t)
        {
            // no thread to play it: on this one, as it used to be
            if (g_beepWake) { CloseHandle(g_beepWake); g_beepWake = nullptr; }
            Beep(freq, ms);
            return;
        }
        CloseHandle(t);
    }
    AcquireSRWLockExclusive(&g_beepLock);
    if (g_beeps.size() < 8) g_beeps.push_back(BeepNote{ freq, ms, gap });
    ReleaseSRWLockExclusive(&g_beepLock);
    SetEvent(g_beepWake);
}

static bool GameInFront()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// A copy of the running exe's image, for the look through the game's code. Read like everything else here, so a page
// that cannot be read stays zero in the copy and nothing faults.
static bool CopyOwnImage(std::vector<uint8_t> &out)
{
    DWORD header = 0, size = 0;
    if (!Read(g_base + 0x3C, header) || !Read(g_base + header + 24 + 56, size) || size < 0x10000 || size > 0x20000000) return false;
    out.assign(size, 0);
    const HANDLE self = GetCurrentProcess();
    for (size_t o = 0; o < size; o += 0x10000)
    {
        const size_t chunk = min((size_t)0x10000, (size_t)size - o);
        SIZE_T got = 0;
        if (ReadProcessMemory(self, (const void *)(g_base + o), out.data() + o, chunk, &got) && got == chunk) continue;
        for (size_t page = o; page < o + chunk; page += 0x1000)
            ReadProcessMemory(self, (const void *)(g_base + page), out.data() + page, min((size_t)0x1000, (size_t)size - page), &got);
    }
    return true;
}

static DWORD WINAPI Run(void *)
{
    // One copy of the mod per game: the loader loads every .asi in the folder, and a second file of this mod (a copy
    // left under another name) would take the first one's values for the game's own and scale them again, 20 times a
    // second. The mutex is the process's; the second copy adds its line to the first one's log and ends.
    wchar_t once[64];
    swprintf_s(once, L"Local\\SnowRunnerTirePressure-%lu", GetCurrentProcessId());
    if (CreateMutexW(nullptr, FALSE, once) && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        wchar_t file[MAX_PATH] = {};
        GetModuleFileNameW(g_self, file, MAX_PATH);
        Sleep(1000); // after the first copy's first line, which starts the log anew
        g_logStarted = true;
        Log(L"this mod is in the folder twice: only one copy runs, and this one (%s) stands down. Keep one .asi file of it.", file);
        return 0;
    }
    const BYTE *image = (const BYTE *)g_base;
    const DWORD stamp = *(const DWORD *)(image + *(const DWORD *)(image + 0x3C) + 8);
    Log(L"TirePressure " TP_VERSION L": exe stamp %08lx", stamp);
    SettingsDefaults(g_set); // what the settings page shows until the ini is read
    g_reshadeOk = PanelInit(g_self);
    // The mod's places in the exe, looked for by what the game's code is like around them (build_find.h), so it needs
    // no list of builds. Steam's exe only makes its code readable as it starts, so the first looks can come too early:
    // they are repeated for a minute. Without every place and every check after that, this is a game version the mod
    // cannot work on: it stands down and has changed nothing.
    std::wstring missing;
    bool found = false;
    int looks = 0;
    for (; looks < 120 && !found; looks++)
    {
        if (looks) Sleep(500);
        std::vector<uint8_t> copy;
        found = CopyOwnImage(copy) && BuildFindAll({ copy.data(), copy.size(), g_base }, g_buildFound, missing);
    }
    if (!found)
    {
        Log(L"this game version is not one the mod can work on: standing down. Not as the mod needs it: %s", missing.c_str());
        InterlockedExchange(&g_modOff, 1);
        return 0;
    }
    Log(L"game code found at look %d: control %llx, wheel class %llx, cylinder class %llx, damage update %llx, foreground slot %llx, truck update return %llx, first argument +%llx",
        looks, g_build->control, g_build->wheelVtable, g_build->cylinderVtable, g_build->damageUpdate, g_build->foregroundSlot, g_build->truckUpdateReturn, g_build->firstArgument);
    LoadIni();
    if (!SoundStart(g_dir)) Log(L"sound: none, as its thread did not start");
    g_now = g_modes[g_mode]; // with a base grip, Normal is not the stock values: the first wheels get it straight away
    Log(L"panel: %s", g_ui ? L"drawn through ReShade's overlay" : g_uiWanted ? L"ReShade not found: the key cycles the modes directly" : L"off (UI=0): the key cycles the modes directly");
    if (g_reshadeOk) Log(L"settings: the Tire Inflation System tab in ReShade's overlay");
    bool down = false, chainSeen = false, moving = false, panelOpen = false;
    int sel = 0;
    uint32_t padPrev = 0;
    ULONGLONG lastInput = 0, saveAt = 0;
    if (g_probeWriters) AddVectoredExceptionHandler(1, ProbeHandler);
    int probeState = 0, probeLines = 0; // writers probe: 0 waiting for the truck, 1 found, 2 breakpoint set, 3 over
    ULONGLONG probeTime = 0, probeRefresh = 0, fuelTime = 0;
    int fuelState = 0; // fuel finder: 0 waiting, 1 first copy taken, 2 second copy taken, 3 over
    uint64_t probeWatched = 0;
    float probeLast = -1.0f;
    for (DWORD tick = 0;; tick++)
    {
        Sleep(50);
        // The game fills its pointers to XInputGetState as it goes (this build has two, and one is only set when
        // that part of the game first reads the pad): every two seconds the new ones are sent through the filter.
        if (g_reshadeOk && tick % 40 == 0)
        {
            const int n = PadInit(g_base);
            if (n)
            {
                g_padOk = true;
                Log(L"pad: %d more of the game's XInputGetState pointers go through the panel's filter", n);
            }
        }
        // the settings page: edits count on this pass (the modes walk to edited factors like to a new mode) and go
        // into the ini once they have rested a second; "read the ini again" applies the file as it is
        bool edited = false;
        if (InterlockedExchange(&g_reloadAsked, 0))
        {
            TpSettings s;
            ReadIni(s);
            AcquireSRWLockExclusive(&g_setLock);
            g_set = s;
            ReleaseSRWLockExclusive(&g_setLock);
            InterlockedExchange(&g_setDirty, 0);
            ApplySettings(s, true);
            LoadSoundNames();
            Log(L"settings: TirePressure.ini read again");
            saveAt = 0;
            edited = true;
        }
        if (InterlockedExchange(&g_setDirty, 0))
        {
            TpSettings s;
            SettingsGet(s);
            ApplySettings(s, false);
            saveAt = GetTickCount64() + 1000;
            edited = true;
        }
        if (saveAt && GetTickCount64() >= saveAt)
        {
            TpSettings s;
            SettingsGet(s);
            WriteIni(s);
            saveAt = 0;
            Log(L"settings: saved to TirePressure.ini (vanilla balance %s, base grip %g / %g / %g)", s.vanillaBalance ? L"on" : L"off", s.base[0], s.base[1], s.base[2]);
        }
        if (edited)
        {
            // Increased switched off while in force: back to Normal
            if (g_mode >= g_modeCount) { g_mode = kNormal; Log(L"mode Normal (Increased is off now)"); }
            const Mode &to = g_modes[g_mode];
            if (!SameFactors(to, g_now)) moving = true;
        }
        const bool front = GameInFront();
        const bool now = front && (GetAsyncKeyState(g_key) & 0x8000);
        const bool pressed = now && !down;
        down = now;
        // the keyboard's keys for the open panel (lower, raise, confirm, close): a press counts when the panel is open;
        // they are looked at on every pass, so a key already held as the panel opens does not count
        static bool keyHeld[4] = {};
        bool keyHit[4] = {};
        for (int i = 0; i < 4; i++)
        {
            const bool held = front && g_stepKeys[i] && (GetAsyncKeyState(g_stepKeys[i]) & 0x8000);
            keyHit[i] = held && !keyHeld[i] && panelOpen;
            keyHeld[i] = held;
        }
        // The driven truck's wheels, with the vehicle they belong to. Until the first truck is found they are looked
        // up twice a second (and at a key press); from then on every pass, as the ground under each wheel and the
        // truck's speed count.
        const bool probing = g_probeFriction || (g_probeWriters && probeState < 3) || (g_probeFuel && fuelState < 3);
        const bool look = chainSeen || pressed || moving || probing || panelOpen || tick % 10 == 0;
        uint64_t vehicle = 0;
        const std::vector<WheelRef> wheels = look ? CurrentWheels(&vehicle) : std::vector<WheelRef>();
        if (wheels.empty()) vehicle = 0;
        // the pad is the panel's only while a truck is driven: in the game's menus its buttons are the game's alone
        PadLive(!wheels.empty());
        // the pad: a binding counts when all its buttons are down and were not all down on the last pass; not while
        // the settings page waits for buttons to bind
        const LONG capture = g_pageCapturing;
        const bool capturing = capture && GetTickCount() - (DWORD)capture < 500;
        const uint32_t pad = front && g_ui && !wheels.empty() && !capturing ? PadButtons() : 0;
        auto edge = [&](uint32_t mask) { return mask && (pad & mask) == mask && (padPrev & mask) != mask; };
        // with the panel open the d-pad is the panel's (the filter hides it from the game then), as in Expeditions:
        // a binding also counts without its shoulder button
        auto bound = [&](uint32_t mask) { return edge(mask) || (panelOpen && edge(mask & ~kPadShoulders)); };
        const bool pOpen = bound(g_padOpen), pLower = bound(g_padLower), pRaise = bound(g_padRaise), pConfirm = bound(g_padConfirm),
                   pCancel = bound(g_padCancel);
        padPrev = pad;
        // a step binding of two or more buttons opens the panel and steps at once
        const bool padOpens = !panelOpen && (pOpen || (pLower && PadChord(g_padLower)) || (pRaise && PadChord(g_padRaise)));
        if (g_probeFuel && fuelState < 3 && chainSeen)
        {
            const ULONGLONG t = GetTickCount64();
            if (!fuelTime) fuelTime = t;
            else if (fuelState == 0 && t - fuelTime >= 3000) { FuelSnapshot(); fuelState = 1; fuelTime = t; }
            else if (fuelState == 1 && t - fuelTime >= 11000) { FuelSecond(); fuelState = 2; fuelTime = t; }
            else if (fuelState == 2 && t - fuelTime >= 18000) { FuelCompare(); fuelState = 3; }
        }
        if (!look) continue;
        if (!wheels.empty() && !chainSeen)
        {
            chainSeen = true;
            Log(L"driven truck found: %zu wheels", wheels.size());
            if (g_reshadeOk && !g_padOk) Log(L"pad: none of the game's XInputGetState pointers found so far: the pad works the panel, the game sees the presses too");
            if (g_probeByHand) Log(L"tire damage: dealt by hand (ProbeByHand=1)");
            else if (HookTruckUpdate()) Log(L"tire damage: dealt on the game's own thread, from its truck update, with its damage update after");
            else Log(L"tire damage: the game's truck update is not where this build has it, so the damage is dealt by hand");
        }
        if (g_probeFriction && !wheels.empty() && probeLines < 3000)
        {
            // with the friction: the wheel's asphalt value and what the per-frame ground check (RVA 0xc22f40, through
            // 0xbcd720) left in the wheel: +0x1C8 paved, +0x1C9 gravel, +0x1CA sand, +0x1CD hard (not extrudable),
            // +0x1CE soil (blended layer weight over 0.5), +0x1C0 ice with +0x1C4 its friction, +0x1A4 contact,
            // +0x1CB and the ids +0x1B8 / +0x1D0 (meaning open). A line when the friction or any of these changes.
            static BYTE lastFlags[0x14] = {};
            static int lastIds[2] = {};
            float friction = 0, asphalt = 0, iceFriction = 0;
            BYTE flags[0x14] = {}, contact = 0;
            int ids[2] = {};
            const uint64_t w0 = wheels[0].wheel;
            if (Read(wheels[0].body + 0xCC, friction) && Read(w0 + 0xA4, asphalt) && Read(w0 + 0x1C0, flags) && Read(w0 + 0x1C4, iceFriction) &&
                Read(w0 + 0x1A4, contact) && Read(w0 + 0x1B8, ids[0]) && Read(w0 + 0x1D0, ids[1]))
            {
                const bool flagsChanged = memcmp(flags, lastFlags, 0x10) != 0 || ids[0] != lastIds[0] || ids[1] != lastIds[1];
                if (flagsChanged || fabsf(friction - probeLast) > 0.005f * max(1.0f, fabsf(probeLast)))
                {
                    Log(L"probe: body friction %.4f (asphalt value %.4f) paved %u gravel %u sand %u hard %u soil %u ice %u %.3f, contact %u, +1CB %u, +1CC %u, ids %d %d",
                        friction, asphalt, flags[0x8], flags[0x9], flags[0xA], flags[0xD], flags[0xE], flags[0x0], iceFriction, contact, flags[0xB], flags[0xC], ids[0], ids[1]);
                    memcpy(lastFlags, flags, sizeof lastFlags);
                    lastIds[0] = ids[0];
                    lastIds[1] = ids[1];
                    probeLast = friction;
                    probeLines++;
                }
            }
        }
        if (g_probeWriters && probeState < 3)
        {
            const ULONGLONG t = GetTickCount64();
            uint64_t want = wheels.empty() ? 0 : wheels[0].body + 0xCC;
            if (g_watchA >= 0)
            {
                // ProbeWatch: vehicle+a, [vehicle+a]+b or [[vehicle+a]+b]+c
                uint64_t p = vehicle;
                if (p) p += (uint64_t)g_watchA;
                if (p && g_watchB >= 0) p = Read(p, p) && p > 0x10000 ? p + (uint64_t)g_watchB : 0;
                if (p && g_watchC >= 0) p = Read(p, p) && p > 0x10000 ? p + (uint64_t)g_watchC : 0;
                want = p & ~(uint64_t)3;
            }
            if (probeState == 0 && want) { probeState = 1; probeTime = t; }
            else if (probeState == 1 && t - probeTime >= 2000 && want) { SetWatch(want); probeWatched = want; probeState = 2; probeTime = probeRefresh = t; }
            else if (probeState == 2)
            {
                // another truck or rebuilt wheels: follow them; threads made since the last pass: give them the breakpoint
                if (want && want != probeWatched) { SetWatch(want); probeWatched = want; probeRefresh = t; }
                else if (t - probeRefresh >= 60000) { SetWatch(probeWatched, false); probeRefresh = t; }
                LogWriters(true);
                if (t - probeTime >= (ULONGLONG)g_probeWriters * 1000) { SetWatch(0); LogWriters(false); probeState = 3; }
            }
        }
        int chosen = -1; // a mode picked on this pass
        if ((pressed || padOpens) && wheels.empty())
        {
            Log(L"key pressed, no driven truck found");
            static int diagnosed = 0;
            if (diagnosed++ < 2) Diagnose();
            if (g_beep) BeepLater(220, 200);
            continue;
        }
        // the key steps the pressure down, from Low round to the highest mode in use
        auto stepDown = [](int p) { return p > kLow ? p - 1 : g_modeCount - 1; };
        if (!g_ui)
        {
            // the panel switched off while it was open (the settings page, or the ini read again): it closes, unchanged
            if (panelOpen) { panelOpen = false; Log(L"panel: closed without a change (switched off)"); }
            g_view.open = 0;
            g_view.confirmMs = -1;
            if (pressed) chosen = stepDown(g_mode);
        }
        else
        {
            // the panel: the key or the pad's open binding opens it at the mode in force, a step binding of two or more
            // buttons (LB + left / right) opens it one step lower / higher; then the key steps the pressure down (round),
            // the pad's lower / raise bindings step it; confirm (pad), the open binding again, or no input for
            // ConfirmSeconds applies it; cancel (pad) closes it unchanged. Leaving the truck closes it too.
            const ULONGLONG t = GetTickCount64();
            if (!panelOpen)
            {
                if (pressed || padOpens)
                {
                    panelOpen = true;
                    sel = g_mode;
                    lastInput = t;
                    g_view.viaPad = pressed ? 0 : 1;
                    if (padOpens && !pOpen && pLower) sel = max((int)kLow, sel - 1);
                    if (padOpens && !pOpen && pRaise) sel = min(g_modeCount - 1, sel + 1);
                }
            }
            else
            {
                sel = min(sel, g_modeCount - 1); // Increased switched off while the panel pointed at it
                if (pressed) { sel = stepDown(sel); lastInput = t; g_view.viaPad = 0; }
                if (pLower || keyHit[0]) { sel = max((int)kLow, sel - 1); lastInput = t; g_view.viaPad = pLower ? 1 : 0; }
                if (pRaise || keyHit[1]) { sel = min(g_modeCount - 1, sel + 1); lastInput = t; g_view.viaPad = pRaise ? 1 : 0; }
                const bool timeUp = g_confirmMs > 0 && t - lastInput >= (ULONGLONG)g_confirmMs;
                if (pCancel || keyHit[3] || wheels.empty()) { panelOpen = false; Log(L"panel: closed without a change"); }
                else if (pConfirm || pOpen || keyHit[2] || timeUp) { panelOpen = false; if (sel != g_mode) chosen = sel; }
            }
            g_view.selected = sel;
            g_view.confirmMs = panelOpen && g_confirmMs > 0 ? (LONG)max(0LL, (long long)g_confirmMs - (long long)(t - lastInput)) : -1;
            g_view.open = panelOpen ? 1 : 0;
        }
        if (chosen >= 0)
        {
            g_mode = chosen;
            Log(L"mode %s, %zu wheels", g_modes[g_mode].name, wheels.size());
            // once for Normal, twice for Reduced, three times for Low, high for Increased
            if (g_beep && g_mode == kIncreased) BeepLater(1320, 120);
            else if (g_beep) for (int i = g_mode; i <= kNormal; i++) BeepLater(880, 70, 60);
            moving = true;
        }
        g_view.current = g_mode;
        // The sounds of a change (sound.h): air let out on the way down, air going in on the way up. Where the pressure
        // stands is counted in modes (kLow = 0, a half = between two of them) and moves with the factors, so a choice
        // changed half way still knows which way it goes.
        static int soundMode = kNormal, changing = 0; // changing: -1 the pressure falls, 1 it rises, 0 neither
        static float pressure = (float)kNormal, pressureFrom = (float)kNormal, changeFor = 0.0f, changeTakes = 0.0f;
        if (g_mode != soundMode)
        {
            soundMode = g_mode;
            pressureFrom = pressure;
            changeFor = 0.0f;
            changeTakes = SecondsToMode();
            changing = changeTakes <= 0.0f ? 0 : (float)g_mode > pressure + 0.01f ? 1 : (float)g_mode < pressure - 0.01f ? -1 : 0;
        }
        const bool arrived = moving && StepTowardsMode();
        float soundProgress = 1.0f;
        if (moving && !arrived && changing)
        {
            changeFor += kPass;
            soundProgress = min(1.0f, changeFor / changeTakes);
        }
        else changing = 0;
        const float pressureWas = pressure;
        pressure = pressureFrom + ((float)g_mode - pressureFrom) * soundProgress;
        // The air that went into the tires came out of the tank; the compressor runs when the tank says so, and as
        // it stops it lets its air go. All of it is heard while a truck is driven and the game is in front.
        static AirTank tank;
        static LONG purges = 0;
        const bool heard = front && !wheels.empty();
        if (TankStep(tank, max(0.0f, pressure - pressureWas), kPass) && heard) purges++;
        SoundWant want = {};
        want.volume = g_soundVolume;
        want.on[kVoiceHiss] = heard && changing < 0;
        want.on[kVoiceAir] = heard && changing > 0;
        want.on[kVoiceCompressor] = heard && tank.compressor;
        want.progress[kVoiceHiss] = min(1.0f, max(-0.5f, ((float)kNormal - pressure) / 2.0f)); // how empty the tires are
        want.progress[kVoiceAir] = soundProgress;
        want.progress[kVoiceCompressor] = min(1.0f, max(0.0f, (tank.level - 0.3f) / 0.7f));
        want.shots[kVoicePurge] = purges;
        SoundSet(want);
        // vanilla balance: a factor per wheel from its tire's own grip, once all three are known (the ground grip list
        // only fills when the truck works); the modes and the base grip then act on the balanced tire
        std::vector<float> balance(wheels.size(), 1.0f);
        // (the vehicle's record is looked up each time: ApplyVehicle below adds one for a new truck)
        auto tireGrip = [&](size_t i, float &ground, float &asphalt, float &mud) {
            const auto wi = g_wheels.find(wheels[i].wheel);
            const auto vs = g_vehicles.find(vehicle);
            if (wi == g_wheels.end() || vs == g_vehicles.end() || vs->second.ground.size() != wheels.size() || !(vs->second.ground[i].stock > 0.0f)) return false;
            ground = vs->second.ground[i].stock;
            asphalt = wi->second.asphalt;
            mud = wi->second.substance;
            return true;
        };
        float ground = 0.0f, asphalt = 0.0f, mud = 0.0f;
        for (size_t i = 0; i < wheels.size(); i++)
            if (g_balanceOn && tireGrip(i, ground, asphalt, mud)) balance[i] = VanillaBalance(ground, asphalt, mud, g_balanceStrength);
        for (size_t i = 0; i < wheels.size(); i++)
        {
            // the asphalt floor lifts slippery tires to at least AsphaltFloor on paved ground, as if their tire file
            // said so (without touching the paks); the modes and base grip act on top
            float paved = balance[i];
            const auto wi = g_wheels.find(wheels[i].wheel);
            if (g_asphaltFloor > 0.0f && wi != g_wheels.end() && wi->second.asphalt > 0.0f) paved = max(paved, g_asphaltFloor / wi->second.asphalt);
            Mode m = g_now;
            m.asphalt *= paved;
            m.substance *= balance[i];
            ApplyWheel(wheels[i], m, !moving, arrived);
        }
        // the truck's speed: the fuel factor only counts while it moves (Expeditions' fuel rate, RVA 0xe168a0 in its
        // exe, takes the modifier in above a speed), and soft tires wear when it is too fast for them
        static uint64_t speedVehicle = 0, noDamageVehicle = 0;
        static bool rolling = false;
        static float tickTimer = 0.0f;
        static ULONGLONG lastPass = 0;
        const ULONGLONG passAt = GetTickCount64();
        const float dt = lastPass ? min(0.5f, (float)(passAt - lastPass) / 1000.0f) : 0.0f;
        lastPass = passAt;
        static float lastPos[3] = {};
        float speed = 0.0f, pos[3] = {};
        bool speedOk = vehicle && ForwardSpeed(vehicle, speed, pos);
        speed = fabsf(speed);
        // a paused game leaves the last velocity in the body: the speed stops counting once the truck has not changed
        // place for four passes (0.2 s)
        static int still = 0;
        const float moved = sqrtf((pos[0] - lastPos[0]) * (pos[0] - lastPos[0]) + (pos[1] - lastPos[1]) * (pos[1] - lastPos[1]) + (pos[2] - lastPos[2]) * (pos[2] - lastPos[2]));
        memcpy(lastPos, pos, sizeof pos);
        still = moved < 0.00001f ? still + 1 : 0;
        if (still >= 4) speed = 0.0f;
        if (vehicle != speedVehicle)
        {
            speedVehicle = vehicle;
            rolling = false;
            tickTimer = 0.0f;
            int damage = 0, capacity = 0;
            if (vehicle && !wheels.empty())
            {
                if (WheelDamage(vehicle, wheels[0], damage, capacity)) Log(L"vehicle %p tire damage: the first wheel has %d of %d", (void *)vehicle, damage, capacity);
                else Log(L"vehicle %p tire damage: the wheels' damage data was not found, its tires do not wear", (void *)vehicle);
            }
        }
        rolling = speedOk && speed > (rolling ? 0.25f : 0.5f);
        // the ground under each wheel picks its ground grip factor
        std::vector<float> groundFactor(wheels.size(), 1.0f);
        for (size_t i = 0; i < wheels.size(); i++) groundFactor[i] = GroundFactor(g_now, wheels[i].wheel) * balance[i];
        Mode forVehicle = g_now;
        if (!rolling) forVehicle.fuel = 1.0f;
        // the gears make up for the smaller collision cylinders, but for what a real tire loses (the wheels' mean)
        float roll = 0.0f;
        int rolled = 0;
        for (const WheelRef &w : wheels)
        {
            const auto wi = g_wheels.find(w.wheel);
            if (wi == g_wheels.end()) continue;
            roll += RollFactor(wi->second);
            rolled++;
        }
        roll = g_rollingRadius && rolled ? roll / (float)rolled : 1.0f;
        if (vehicle) ApplyVehicle(vehicle, wheels.size(), forVehicle, arrived, groundFactor, roll);
        if (arrived) moving = false;
        // tire damage at speed, as Expeditions does it (the check in its per-frame truck update, RVA 0xae1d8f there):
        // while the truck is faster than the mode's MinVel a timer runs, and each time it passes DamageTick the wheels
        // on the ground take damage; slowing down starts the wait again. Expeditions looks every frame; this looks 20
        // times a second from outside the game's frame, so the wait only starts again after a full second below the
        // speed (a single missed reading must not cost seven seconds).
        static float slowFor = 0.0f;
        const Tick &tk = g_ticks[g_mode];
        const bool tooFast = g_tireDamage && speedOk && tk.maxDamage > 0 && tk.minVel > 0.0001f && speed > tk.minVel;
        g_view.warn = tooFast ? 1 : 0;
        if (tooFast)
        {
            // for the warning: how worn the most worn tire is
            int wear = -1;
            for (const WheelRef &w : wheels)
            {
                int damage = 0, capacity = 0;
                if (WheelDamage(vehicle, w, damage, capacity) && capacity > 0) wear = max(wear, damage * 100 / capacity);
            }
            g_view.wear = wear;
        }
        if (tooFast) slowFor = 0.0f;
        else if ((slowFor += dt) > 1.0f) tickTimer = 0.0f;
        // The game's truck update comes through ForegroundHook every frame while a truck is driven: the damage is
        // then dealt there, on the game's thread, and its answer logged on a later pass. Without it (the hook not
        // set) the damage is dealt from here by hand.
        static LONG updatesSeen = 0;
        static ULONGLONG updatesSeenAt = 0;
        const LONG updates = g_truckUpdates;
        if (updates != updatesSeen) { updatesSeen = updates; updatesSeenAt = passAt; }
        const bool onGameThread = g_foregroundReal && updatesSeenAt && passAt - updatesSeenAt < 500;
        if (tooFast && (tickTimer += dt) > tk.seconds)
        {
            tickTimer = 0.0f;
            const int add = DamageForSpeed(tk, speed);
            if (onGameThread) PostDamage(vehicle, add, -1, speed);
            else
            {
                const TickResult r = DealDamage(vehicle, wheels, add, -1, false);
                LogDamage(vehicle, wheels.size(), r, add, -1, speed, false);
                if (r.hit && g_beep) BeepLater(r.flat ? 220 : 330, r.flat ? 400 : 120);
            }
        }
        // diagnostics: ProbeDamage=N sets every wheel of the driven truck to N damage once, 10 s after it is found:
        // its capacity or more wears it out (flat, as a tick does), less than that takes a flat tire back; with
        // ProbeDamageBack=s the damage goes back to none s seconds later (a repair). The wheels' state is logged two
        // seconds after each.
        static ULONGLONG probeDamageAt = 0, probeDamageLogAt = 0;
        static int probeDamageStep = 0; // 0: ProbeDamage to come, 1: ProbeDamageBack seconds later back to none, 2: over
        if (g_probeDamage >= 0 && vehicle && !wheels.empty() && probeDamageStep < 2)
        {
            if (!probeDamageAt) probeDamageAt = passAt + 10000;
            else if (passAt >= probeDamageAt)
            {
                const int to = probeDamageStep == 0 ? g_probeDamage : 0;
                probeDamageStep = probeDamageStep == 0 && g_probeDamageBack > 0 ? 1 : 2;
                probeDamageAt = passAt + (ULONGLONG)g_probeDamageBack * 1000;
                probeDamageLogAt = passAt + 2000;
                if (onGameThread) PostDamage(vehicle, 0, to, speed);
                else LogDamage(vehicle, wheels.size(), DealDamage(vehicle, wheels, 0, to, false), 0, to, speed, false);
            }
        }
        if (probeDamageLogAt && passAt >= probeDamageLogAt)
        {
            probeDamageLogAt = 0;
            for (const WheelRef &w : wheels)
            {
                int damage = -1, capacity = -1;
                BYTE broken = 0;
                float radius = 0, cylinder = 0;
                WheelDamage(vehicle, w, damage, capacity);
                Read(w.model + 0x328, broken);
                Read(w.wheel + 0x94, radius);
                Read(w.shape + 0x28, cylinder);
                Log(L"probe: wheel %d has %d of %d damage, broken flag %u, radius %.4f, cylinder %.4f", w.index, damage, capacity, broken, radius, cylinder);
            }
        }
        // the game's thread's answer to a job; none within a second (the game stopped updating the truck): taken back
        if (g_jobPending || g_jobDone)
        {
            AcquireSRWLockExclusive(&g_jobLock);
            const DamageJob job = g_job;
            const TickResult r = g_jobResult;
            const bool done = InterlockedExchange(&g_jobDone, 0) != 0;
            const bool late = !done && g_jobPending && (long long)(GetTickCount64() - job.posted) > 1000;
            if (late) InterlockedExchange(&g_jobPending, 0);
            ReleaseSRWLockExclusive(&g_jobLock);
            if (done)
            {
                LogDamage(job.vehicle, wheels.size(), r, job.add, job.set, job.speed, true);
                if (r.hit && g_beep && job.set < 0) BeepLater(r.flat ? 220 : 330, r.flat ? 400 : 120);
            }
            if (late)
                Log(L"tire damage: the game did not update vehicle %p within a second (the last it updated was %p): nothing dealt", (void *)job.vehicle,
                    (void *)(uint64_t)g_truckUpdateVehicle);
        }
        // diagnostics: ProbeDrive=1 logs once a second what the truck does
        static ULONGLONG probeDriveAt = 0;
        if (g_probeDrive && vehicle && !wheels.empty() && passAt >= probeDriveAt)
        {
            probeDriveAt = passAt + 1000;
            static LONG updatesLogged = 0;
            uint64_t action = 0;
            BYTE handbrake = 0, f[6] = {}, contact = 0, broken = 0;
            int damage = 0, capacity = 0, total = 0;
            float cylinder = 0;
            Read(vehicle + 0x68, action);
            Read(action + 0x48, handbrake);
            Read(wheels[0].wheel + 0x1C8, f);
            Read(wheels[0].wheel + 0x1A4, contact);
            Read(wheels[0].model + 0x328, broken);
            Read(wheels[0].shape + 0x28, cylinder);
            Read(vehicle + 0x108, total);
            WheelDamage(vehicle, wheels[0], damage, capacity);
            Log(L"probe: %.1f km/h%s, handbrake %u, first wheel contact %u paved %u gravel %u sand %u hard %u, damage %d of %d, broken %u, cylinder %.4f, the game's sum %d, timer %.1f s, %ld truck updates",
                speed * 3.6f, still >= 4 ? L" (not changing place)" : L"", handbrake, contact, f[0], f[1], f[2], f[5], damage, capacity, broken, cylinder, total, tickTimer,
                (long)(updates - updatesLogged));
            updatesLogged = updates;
        }
        // which of the game's threads read the pad and present, once (diagnostics)
        static bool threadsLogged = false;
        static ULONGLONG threadsAt = 0;
        if (!threadsAt && chainSeen) threadsAt = passAt + 5000;
        if (!threadsLogged && threadsAt && passAt >= threadsAt && g_padThread && g_overlayThread)
        {
            threadsLogged = true;
            DWORD windowThread = 0;
            EnumWindows([](HWND h, LPARAM out) -> BOOL {
                DWORD pid = 0;
                const DWORD tid = GetWindowThreadProcessId(h, &pid);
                wchar_t cls[64] = {};
                if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || !GetClassNameW(h, cls, 64) || wcscmp(cls, L"SnowRunner")) return TRUE;
                *(DWORD *)out = tid;
                return FALSE;
            }, (LPARAM)&windowThread);
            Log(L"threads: the pad is read on %lu (%ld changes of thread so far), the overlay is drawn on %lu, the game's window belongs to %lu, the truck update runs on %lu (%ld changes, %ld runs)",
                (unsigned long)g_padThread, (long)g_padThreadChanges, (unsigned long)g_overlayThread, (unsigned long)windowThread, (unsigned long)g_truckUpdateThread,
                (long)g_truckUpdateThreadChanges, (long)g_truckUpdates);
        }
        // what the settings page shows: the first wheel's tire
        {
            TpLive live = {};
            live.wheels = (int)wheels.size();
            live.mode = g_mode;
            live.balance = wheels.empty() ? 1.0f : balance[0];
            live.speed = speedOk ? speed * 3.6f : 0.0f;
            if (!wheels.empty())
            {
                BYTE f[6] = {};
                Read(wheels[0].wheel + 0x1C8, f);
                live.surface = f[0] ? kSurfacePaved : f[2] ? kSurfaceSand : f[1] ? kSurfaceGravel : f[5] ? kSurfaceRock : kSurfaceDirt;
                WheelDamage(vehicle, wheels[0], live.damage, live.capacity);
            }
            if (!wheels.empty() && !tireGrip(0, live.ground, live.asphalt, live.mud))
            {
                const auto wi = g_wheels.find(wheels[0].wheel);
                if (wi != g_wheels.end()) { live.asphalt = wi->second.asphalt; live.mud = wi->second.substance; }
            }
            live.room = -1.0f;
            if (!wheels.empty())
            {
                const auto wi = g_wheels.find(wheels[0].wheel);
                if (wi != g_wheels.end()) live.room = wi->second.room;
            }
            AcquireSRWLockExclusive(&g_setLock);
            g_live = live;
            ReleaseSRWLockExclusive(&g_setLock);
        }
        // the records of long gone wheels go; those of the wheels in use stay (dropping them would make the next pass
        // take a flattened wheel for one it must leave alone, or its scaled frictions for the tire's own)
        if (g_wheels.size() > 4096)
        {
            std::unordered_map<uint64_t, Stock> kept;
            for (const WheelRef &w : wheels)
            {
                const auto wi = g_wheels.find(w.wheel);
                if (wi != g_wheels.end()) kept.insert(*wi);
            }
            g_wheels.swap(kept);
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t *name = wcsrchr(path, L'\\');
    if (!name || _wcsicmp(name + 1, L"SnowRunner.exe")) return TRUE; // loaded into another program: do nothing
    g_dir.assign(path, name + 1 - path);
    g_base = (uint64_t)GetModuleHandleW(nullptr);
    g_self = inst;
    const HANDLE t = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    return TRUE;
}

#ifdef TP_PROBE_TEST
// Offline test of the write breakpoint: build.bat makes out\test\probe_test.exe from this file with TP_PROBE_TEST. A
// thread writes one float directly and through a tiny setter; the probe must list both and the program must live.
static volatile float g_testValue = 0;
__declspec(noinline) static void TestSetter(volatile float *p, float v) { *p = v; }
static DWORD WINAPI TestWriter(void *stop)
{
    for (float f = 1; !*(volatile LONG *)stop; f += 1) { g_testValue = f; TestSetter(&g_testValue, f + 0.5f); Sleep(1); }
    return 0;
}
static bool SameSettings(const TpSettings &a, const TpSettings &b)
{
    bool same = a.key == b.key && !memcmp(a.panelKeys, b.panelKeys, sizeof a.panelKeys) && a.beep == b.beep && a.seconds == b.seconds && a.soundVolume == b.soundVolume && a.ui == b.ui && a.uiScale == b.uiScale &&
                a.confirmSeconds == b.confirmSeconds && a.vanillaBalance == b.vanillaBalance && a.balanceStrength == b.balanceStrength;
    for (int i = 0; i < 5; i++) same = same && !strcmp(a.pad[i], b.pad[i]);
    for (int i = 0; i < 3; i++) same = same && a.base[i] == b.base[i];
    same = same && a.asphaltFloor == b.asphaltFloor && a.increased == b.increased && a.tireDamage == b.tireDamage && a.rollingRadius == b.rollingRadius;
    for (int m = 0; m < 3; m++)
        for (int k = 0; k < kModeKeyCount; k++) same = same && a.mode[m][k] == b.mode[m][k];
    return same;
}

// The settings page's ini round trip, in out\test: a missing ini comes out as the defaults with the help text, every
// key the page writes reads back the same (every setting is given a value that is not its default, so a key that is
// never written shows), the help text and keys the page does not know survive a write, a value that is no number
// gives the default, and a copy of an ini in use (argument) reads back unchanged after a write.
static bool IniTest(const wchar_t *userIni)
{
    const std::wstring ini = g_dir + L"TirePressure.ini";
    DeleteFileW(ini.c_str());
    const bool gone = GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES;
    TpSettings d, a, b, c;
    SettingsDefaults(d);
    ReadIni(a);
    ReadIni(b);
    bool ok = gone && SameSettings(a, d) && SameSettings(b, d);
    printf("%s ini: a missing ini is written with the defaults and reads back as them%s\n", ok ? "ok  " : "FAIL", gone ? "" : " (the old test ini could not be deleted)");
    WritePrivateProfileStringW(L"TirePressure", L"ProbeFriction", L"0", ini.c_str());
    c = d;
    c.key = 'K';
    static const int stepKeys[4] = { 'J', 'L', VK_SPACE, 0 };
    memcpy(c.panelKeys, stepKeys, sizeof stepKeys);
    c.beep = false;
    c.seconds = 4.5f;
    c.soundVolume = 35.0f;
    c.ui = false;
    c.uiScale = 1.25f;
    c.confirmSeconds = 0.0f;
    static const char *pads[5] = { "RB+DPadUp", "None", "X+Y", "LS", "Back" };
    for (int i = 0; i < 5; i++) strcpy_s(c.pad[i], pads[i]);
    c.base[0] = 0.8f;
    c.base[1] = 0.9f;
    c.base[2] = 0.75f;
    c.vanillaBalance = true;
    c.balanceStrength = 0.5f;
    c.asphaltFloor = 3.5f;
    c.increased = false;
    c.tireDamage = false;
    c.rollingRadius = false;
    int unchanged = 0;
    for (int m = 0; m < 3; m++)
        for (int k = 0; k < kModeKeyCount; k++)
        {
            c.mode[m][k] = kModeRange[k].lo + (kModeRange[k].hi - kModeRange[k].lo) * (0.11f + 0.07f * (float)m + 0.013f * (float)k);
            unchanged += c.mode[m][k] == d.mode[m][k];
        }
    for (int i = 0; i < 5; i++) unchanged += !strcmp(c.pad[i], d.pad[i]);
    for (int i = 0; i < 4; i++) unchanged += c.panelKeys[i] == d.panelKeys[i];
    unchanged += c.key == d.key || c.seconds == d.seconds || c.soundVolume == d.soundVolume || c.uiScale == d.uiScale || c.confirmSeconds == d.confirmSeconds || c.base[0] == d.base[0] ||
                 c.base[1] == d.base[1] || c.base[2] == d.base[2] || c.balanceStrength == d.balanceStrength || c.asphaltFloor == d.asphaltFloor ||
                 c.beep == d.beep || c.ui == d.ui || c.vanillaBalance == d.vanillaBalance || c.increased == d.increased || c.tireDamage == d.tireDamage ||
                 c.rollingRadius == d.rollingRadius;
    WriteIni(c);
    ReadIni(b);
    wchar_t probe[16] = {};
    GetPrivateProfileStringW(L"TirePressure", L"ProbeFriction", L"", probe, 16, ini.c_str());
    FILE *f = nullptr;
    wchar_t first[80] = {};
    if (_wfopen_s(&f, ini.c_str(), L"r, ccs=UTF-16LE") == 0 && f) { fgetws(first, 80, f); fclose(f); }
    const bool round = !unchanged && SameSettings(b, c), kept = !wcscmp(probe, L"0") && !wcsncmp(first, L"; TirePressure:", 15);
    printf("%s ini: every key written reads back the same%s%s\n", round && kept ? "ok  " : "FAIL", kept ? ", help text and other keys kept" : ", help text or other keys LOST",
           unchanged ? " (the test left a setting at its default)" : "");
    ok = ok && round && kept;
    // "nan" passes every range check after it (the min and max macros), so it must not come out of the ini at all
    WritePrivateProfileStringW(L"Low", L"OnModelFriction", L"nan", ini.c_str());
    WritePrivateProfileStringW(L"TirePressure", L"BaseMud", L"inf", ini.c_str());
    ReadIni(b);
    const bool finite = b.mode[1][kModeGround] == d.mode[1][kModeGround] && b.base[2] == d.base[2];
    printf("%s ini: \"nan\" and \"inf\" in the file give the defaults (%g, %g)\n", finite ? "ok  " : "FAIL", b.mode[1][kModeGround], b.base[2]);
    ok = ok && finite;
    // an ini from before 1.1.0 has no IniVersion: its 3 seconds become the new time, once (the file then carries the
    // version, and 3 set after that stays); another time in such an ini stays as it is
    TpSettings before, after, other;
    WritePrivateProfileStringW(L"TirePressure", L"IniVersion", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"TirePressure", L"Seconds", L"3", ini.c_str());
    ReadIni(before);
    WritePrivateProfileStringW(L"TirePressure", L"Seconds", L"3", ini.c_str());
    ReadIni(after);
    WritePrivateProfileStringW(L"TirePressure", L"IniVersion", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"TirePressure", L"Seconds", L"4.5", ini.c_str());
    ReadIni(other);
    const bool taken = before.seconds == kSecondsDefault && after.seconds == kSecondsBefore && other.seconds == 4.5f;
    printf("%s ini: the %g s of an ini from before 1.1.0 become %g s, once (read %g, then %g for a 3 set after that, %g for an old ini's 4.5)\n", taken ? "ok  " : "FAIL",
           kSecondsBefore, kSecondsDefault, before.seconds, after.seconds, other.seconds);
    ok = ok && taken;
    if (userIni && !CopyFileW(userIni, ini.c_str(), FALSE))
    {
        printf("FAIL ini: %ls could not be copied (error %lu)\n", userIni, GetLastError());
        return false;
    }
    if (userIni)
    {
        ReadIni(a);
        WriteIni(a);
        ReadIni(b);
        const bool user = SameSettings(a, b);
        printf("%s ini: a copy of %ls reads back the same after a write (key %d, UI %d, vanilla balance %d, base %g/%g/%g)\n", user ? "ok  " : "FAIL",
               userIni, a.key, a.ui, a.vanillaBalance, a.base[0], a.base[1], a.base[2]);
        ok = ok && user;
    }
    return ok;
}

// The flattening limit against values worked out apart from this code (the same formulas in a script, 2026-10-04),
// and the share of its offset a mode gets on the scout of that day (radius 0.47, own offset 0.0188): the deepest mode
// takes all the tire's room, another mode the same share of its own, and modes that fit stay as they are.
static bool FlattenTest()
{
    static const struct { float radius, limit; } byHand[] = { { 0.35f, 0.071f }, { 0.47f, 0.086f }, { 0.65f, 0.102f }, { 0.80f, 0.108f }, { 1.0f, 0.103f } };
    bool limits = true;
    for (const auto &t : byHand) limits = limits && fabsf(FlattenLimit(t.radius) - t.limit) <= 0.0025f;
    const float stock = 0.47f - 0.0188f;
    Stock s = { 0.0f, 2.0f, 3.3f, stock, kStockMode, stock, max(0.0f, FlattenLimit(0.47f) - 0.0188f), 0.47f };
    Mode low = kStockMode, reduced = kStockMode;
    low.radiusOffset = 0.10f;
    reduced.radiusOffset = 0.06f;
    g_deepest = 0.10f;
    const float byLow = stock - Cylinder(s, low, 0.47f), byReduced = stock - Cylinder(s, reduced, 0.47f);
    const bool shared = fabsf(byLow - s.room) < 0.0005f && fabsf(byReduced - s.room * 0.6f) < 0.0005f;
    g_deepest = low.radiusOffset = 0.05f;
    const bool fits = fabsf(stock - Cylinder(s, low, 0.47f) - 0.05f) < 0.0001f;
    printf("%s flattening: a 0.47 m tire can sit %.3f m low (0.086 by hand); with %.3f m of its own, a 0.10 m mode gets %.3f m, a 0.06 m mode %.3f m, a 0.05 m mode all of it\n",
           limits && shared && fits ? "ok  " : "FAIL", FlattenLimit(0.47f), 0.0188f, byLow, byReduced);
    // the gears: nothing at stock; at the scout's Low the cylinder is 14.9% smaller and a real tire rolls 4.9% slower
    // (by hand: 0.9509 / 0.8511 = 1.117)
    const float atStock = RollFactor(s);
    s.writtenCylinder = stock - byLow;
    const float atLow = RollFactor(s), speed = atLow * s.writtenCylinder / stock;
    const bool rolls = atStock == 1.0f && fabsf(atLow - 1.117f) < 0.004f && fabsf(speed - 0.951f) < 0.003f;
    printf("%s rolling radius: gear speeds x%.3f at stock, x%.3f at that Low (1.117 by hand): the truck keeps %.1f%% of its speed where the smaller wheel alone keeps %.1f%%\n",
           rolls ? "ok  " : "FAIL", atStock, atLow, speed * 100.0f, s.writtenCylinder / stock * 100.0f);
    return limits && shared && fits && rolls;
}

// An exe from a file, as the loader lays it out: a copy of the image from a running game is that already (file offset
// = RVA), an exe as it lies on disk gets its sections put in place. base: what its stored pointers are relative to.
static bool LoadImageFile(const wchar_t *file, std::vector<uint8_t> &image, uint64_t &base)
{
    FILE *f = nullptr;
    if (_wfopen_s(&f, file, L"rb") != 0 || !f) return false;
    std::vector<uint8_t> raw;
    static uint8_t part[1 << 16];
    for (size_t n; (n = fread(part, 1, sizeof part, f)) > 0;) raw.insert(raw.end(), part, part + n);
    fclose(f);
    const BuildImage r = { raw.data(), raw.size(), 0 };
    if (raw.size() < 0x1000 || r.u16(0) != 0x5A4D) return false;
    base = r.u64(r.optional() + 24);
    const size_t size = r.u32(r.optional() + 56);
    if (raw.size() >= size) { image.swap(raw); return true; }
    image.assign(size, 0);
    memcpy(image.data(), raw.data(), min((size_t)r.u32(r.optional() + 60), raw.size()));
    const size_t sections = r.u16(r.header() + 6), first = r.optional() + r.u16(r.header() + 20);
    for (size_t s = 0; s < sections; s++)
    {
        const size_t h = first + s * 40, va = r.u32(h + 12), at = r.u32(h + 20);
        const size_t inFile = r.u32(h + 16), inMemory = r.u32(h + 8) ? r.u32(h + 8) : inFile;
        if (!at || at >= raw.size() || va >= size) continue;
        memcpy(image.data() + va, raw.data() + at, min(min(inFile, inMemory), min(raw.size() - at, size - va)));
    }
    return true;
}

// The look through the game's code (build_find.h) on an exe in a file. Steam's build of 22 July 2026, on which the mod
// was made, has to give the numbers it was made with. Steam's exe as it lies on disk will not do: its code is only
// readable in the running game. No file named: nothing to test here, as the game's exe is no part of this project.
static bool BuildTest(const wchar_t *file)
{
    if (!file || !*file) { printf("     game code: no image of the game's exe to look through (SR_IMAGE names one): not tested\n"); return true; }
    std::vector<uint8_t> image;
    uint64_t base = 0;
    if (!LoadImageFile(file, image, base)) { printf("FAIL game code: %ls could not be read as an exe\n", file); return false; }
    Build b;
    std::wstring missing;
    const bool found = BuildFindAll({ image.data(), image.size(), base }, b, missing);
    static const Build kSteam = { 0x6a607c05, 0x2e02000, 0x2a8eb78, 0x2258918, 0x23c2c08, 0xd6f690, 0x21be360, 0xa57535, 0x418 };
    const bool known = b.stamp == kSteam.stamp;
    const bool same = b.image == kSteam.image && b.control == kSteam.control && b.wheelVtable == kSteam.wheelVtable && b.cylinderVtable == kSteam.cylinderVtable &&
                      b.damageUpdate == kSteam.damageUpdate && b.foregroundSlot == kSteam.foregroundSlot && b.truckUpdateReturn == kSteam.truckUpdateReturn &&
                      b.firstArgument == kSteam.firstArgument;
    const bool ok = found && (!known || same);
    printf("%s game code in %ls (build stamp %08x, image %x): control %llx, wheel class %llx, cylinder class %llx, damage update %llx, foreground slot %llx, "
           "truck update return %llx, first argument +%llx%s\n", ok ? "ok  " : "FAIL", file, b.stamp, b.image, b.control, b.wheelVtable, b.cylinderVtable, b.damageUpdate,
           b.foregroundSlot, b.truckUpdateReturn, b.firstArgument,
           !found ? "" : !known ? ": a build the test has no numbers for" : same ? ": as the mod was made with" : ": NOT the numbers the mod was made with");
    if (!found) printf("     not as the mod needs it: %ls\n", missing.c_str());
    return ok;
}

// A WAV file in memory: the header for a format, then the samples.
static std::vector<uint8_t> TestWav(int tag, int channels, int rate, int bits, int align, const std::vector<uint8_t> &samples)
{
    std::vector<uint8_t> w;
    auto put = [&](uint32_t v, int bytes) { for (int i = 0; i < bytes; i++) w.push_back((uint8_t)(v >> (8 * i))); };
    auto text = [&](const char *s) { w.insert(w.end(), s, s + 4); };
    text("RIFF"); put((uint32_t)(36 + samples.size()), 4); text("WAVE");
    text("fmt "); put(16, 4); put(tag, 2); put(channels, 2); put(rate, 4); put(rate * align, 4); put(align, 2); put(bits, 2);
    text("data"); put((uint32_t)samples.size(), 4);
    w.insert(w.end(), samples.begin(), samples.end());
    return w;
}

// A pak (zip) in memory with two files that lie in it as they are: "[sound]\other.pcm" and, second, name with data.
// wide: the second in the 64-bit form (sizes and place in an extra field, the 64-bit end record and its locator).
// method: 0 = stored, 8 = compressed (which the mod does not read).
static std::vector<uint8_t> TestPak(const char *name, const std::vector<uint8_t> &data, bool wide, int method)
{
    std::vector<uint8_t> z, dir;
    auto put = [](std::vector<uint8_t> &to, uint64_t v, int bytes) { for (int i = 0; i < bytes; i++) to.push_back((uint8_t)(v >> (8 * i))); };
    const std::vector<uint8_t> otherData = { 1, 2, 3, 4, 5 };
    const struct { const char *name; const std::vector<uint8_t> *data; bool wide; int method; } files[2] = { { "[sound]\\other.pcm", &otherData, false, 0 }, { name, &data, wide, method } };
    for (const auto &f : files)
    {
        const size_t at = z.size(), nameLen = strlen(f.name);
        const uint32_t inDir = f.wide ? 0xFFFFFFFFu : (uint32_t)f.data->size();
        put(z, 0x04034b50, 4); put(z, 20, 2); put(z, 0, 2); put(z, f.method, 2); put(z, 0, 4); put(z, 0, 4); put(z, (uint32_t)f.data->size(), 4);
        put(z, (uint32_t)f.data->size(), 4); put(z, nameLen, 2); put(z, 0, 2);
        z.insert(z.end(), f.name, f.name + nameLen);
        z.insert(z.end(), f.data->begin(), f.data->end());
        put(dir, 0x02014b50, 4); put(dir, 20, 2); put(dir, 20, 2); put(dir, 0, 2); put(dir, f.method, 2); put(dir, 0, 4); put(dir, 0, 4);
        put(dir, inDir, 4); put(dir, inDir, 4); put(dir, nameLen, 2); put(dir, f.wide ? 28 : 0, 2); put(dir, 0, 2); put(dir, 0, 2); put(dir, 0, 2);
        put(dir, 0, 4); put(dir, f.wide ? 0xFFFFFFFFu : (uint32_t)at, 4);
        dir.insert(dir.end(), f.name, f.name + nameLen);
        if (f.wide) { put(dir, 1, 2); put(dir, 24, 2); put(dir, f.data->size(), 8); put(dir, f.data->size(), 8); put(dir, at, 8); }
    }
    const size_t dirAt = z.size();
    z.insert(z.end(), dir.begin(), dir.end());
    if (wide)
    {
        const size_t recordAt = z.size();
        put(z, 0x06064b50, 4); put(z, 44, 8); put(z, 45, 2); put(z, 45, 2); put(z, 0, 4); put(z, 0, 4); put(z, 2, 8); put(z, 2, 8); put(z, dir.size(), 8); put(z, dirAt, 8);
        put(z, 0x07064b50, 4); put(z, 0, 4); put(z, recordAt, 8); put(z, 1, 4);
    }
    put(z, 0x06054b50, 4); put(z, 0, 2); put(z, 0, 2); put(z, wide ? 0xFFFF : 2, 2); put(z, wide ? 0xFFFF : 2, 2);
    put(z, wide ? 0xFFFFFFFFu : (uint32_t)dir.size(), 4); put(z, wide ? 0xFFFFFFFFu : (uint32_t)dirAt, 4); put(z, 0, 2);
    return z;
}

static bool TestSave(const std::wstring &path, const std::vector<uint8_t> &bytes)
{
    FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    return ok;
}

// A loop as a 16-bit WAV file, to listen to or to measure.
static bool TestSaveLoop(const std::wstring &path, const SoundClip &c)
{
    std::vector<uint8_t> samples;
    for (const float v : c.x)
    {
        const int s = (int)floorf(max(-1.0f, min(1.0f, v)) * 32767.0f + 0.5f);
        samples.push_back((uint8_t)s);
        samples.push_back((uint8_t)(s >> 8));
    }
    return TestSave(path, TestWav(1, 1, (int)c.rate, 16, 2, samples));
}

// The sound module without an audio device (sound.h): a WAV file in memory reads back as its samples; hand-made ADPCM
// blocks give the values worked out by hand; a file comes out of a hand-made pak in both zip forms, by a name in
// other case and with other slashes, and a compressed or missing one does not; the loops made by code join, hold no
// bad number and have the loudness asked for; the shapes stay within what a voice takes; and the time a change is
// said to take is the time it takes.
static bool SoundTest()
{
    // 16-bit PCM, two channels, 1000 frames of (1000, 3000): one channel of 2000
    std::vector<uint8_t> pcm;
    for (int i = 0; i < 1000; i++) { pcm.push_back(0xE8); pcm.push_back(0x03); pcm.push_back(0xB8); pcm.push_back(0x0B); }
    const std::vector<uint8_t> wav = TestWav(1, 2, 8000, 16, 4, pcm);
    SoundClip clip;
    std::wstring why;
    bool plain = SoundDecode(wav, clip, why) && clip.rate == 8000 && clip.x.size() == 1000;
    for (size_t i = 0; plain && i < clip.x.size(); i++) plain = fabsf(clip.x[i] - 2000.0f / 32768.0f) < 1e-6f;
    printf("%s sound: a 16-bit WAV file of two channels reads as one channel of their mean%s%ls\n", plain ? "ok  " : "FAIL", plain ? "" : ": ", plain ? L"" : why.c_str());
    // ADPCM, one channel, blocks of 22 bytes (32 samples). By hand, from the format's rules:
    //   predictor 0 (256, 0), step 16, samples 100 and 50, then +1, -1 and nothing: 50, 100, 116, 100, 100, ...
    //   predictor 1 (512, -256), samples 100 and 50, nothing: every sample 50 more than the last, up to 1600
    //   predictor 4 (240, 0), samples -3 and 0, nothing: 0, -3, -3, -3, ... (-720 / 256 rounded down, as the codec does)
    std::vector<uint8_t> blocks;
    static const struct { uint8_t pred; int16_t s1, s2; uint8_t first; } kinds[3] = { { 0, 100, 50, 0x1F }, { 1, 100, 50, 0x00 }, { 4, -3, 0, 0x00 } };
    for (int b = 0; b < 15; b++)
    {
        const auto &k = kinds[b % 3];
        const uint8_t head[7] = { k.pred, 16, 0, (uint8_t)k.s1, (uint8_t)(k.s1 >> 8), (uint8_t)k.s2, (uint8_t)(k.s2 >> 8) };
        blocks.insert(blocks.end(), head, head + 7);
        blocks.push_back(k.first);
        blocks.insert(blocks.end(), 14, (uint8_t)0);
    }
    SoundClip packed;
    bool adpcm = SoundDecode(TestWav(2, 1, 4000, 4, 22, blocks), packed, why) && packed.rate == 4000 && packed.x.size() == 15 * 32;
    static const int first[3][5] = { { 50, 100, 116, 100, 100 }, { 50, 100, 150, 200, 250 }, { 0, -3, -3, -3, -3 } };
    int wrongAt = -1;
    for (int b = 0; adpcm && b < 15 && wrongAt < 0; b++)
        for (int i = 0; i < 32 && wrongAt < 0; i++)
        {
            const float want = i < 5 ? (float)first[b % 3][i] : b % 3 == 0 ? 100.0f : b % 3 == 1 ? 50.0f * (float)(i + 1) : -3.0f;
            if (fabsf(packed.x[b * 32 + i] * 32768.0f - want) >= 0.01f) wrongAt = b * 32 + i;
        }
    adpcm = adpcm && wrongAt < 0;
    printf("%s sound: ADPCM blocks give the samples worked out by hand", adpcm ? "ok  " : "FAIL");
    if (wrongAt >= 0) printf(": sample %d is %g", wrongAt, packed.x[wrongAt] * 32768.0f);
    else if (!adpcm) printf(": %zu samples at %u Hz, %ls", packed.x.size(), packed.rate, why.c_str());
    printf("\n");
    // the WAV file out of a pak, in the plain and in the 64-bit form
    const std::wstring pak = g_dir + L"sound_test.pak";
    bool paks = true, refused = true;
    for (int wide = 0; wide < 2; wide++)
    {
        std::vector<uint8_t> out;
        SoundClip back;
        paks = paks && TestSave(pak, TestPak("[sound]\\Test\\Loop.pcm", wav, wide != 0, 0)) && PakRead(pak, L"[sound]/test/loop.PCM", out, why) && out == wav &&
               SoundDecode(out, back, why) && back.x.size() == 1000;
        refused = refused && !PakRead(pak, L"[sound]\\test\\none.pcm", out, why);
        refused = refused && TestSave(pak, TestPak("[sound]\\Test\\Loop.pcm", wav, wide != 0, 8)) && !PakRead(pak, L"[sound]\\test\\loop.pcm", out, why);
    }
    DeleteFileW(pak.c_str());
    printf("%s sound: a file comes out of a pak by its name, in the plain and the 64-bit zip form%s%ls\n", paks ? "ok  " : "FAIL", paks ? "" : ": ", paks ? L"" : why.c_str());
    printf("%s sound: a name the pak does not have and a compressed file are refused\n", refused ? "ok  " : "FAIL");
    // a WAV file that marks a loop: what lies before the loop stays in front of it, what follows it is dropped
    std::vector<uint8_t> loopWav = wav, smpl(68, 0);
    memcpy(smpl.data(), "smpl", 4);
    smpl[4] = 60;             // the chunk's size
    smpl[8 + 28] = 1;         // one loop
    smpl[8 + 36 + 8] = 100;   // from sample 100
    smpl[8 + 36 + 12] = 0x83; // to sample 899
    smpl[8 + 36 + 13] = 0x03;
    loopWav.insert(loopWav.begin() + 36, smpl.begin(), smpl.end());
    const uint32_t riff = (uint32_t)loopWav.size() - 8;
    memcpy(loopWav.data() + 4, &riff, 4);
    // the same marks in a file twice as long: 900 samples follow the loop (0.11 s), and those are its ending
    std::vector<uint8_t> longPcm = pcm, endWav;
    longPcm.insert(longPcm.end(), pcm.begin(), pcm.end());
    endWav = TestWav(1, 2, 8000, 16, 4, longPcm);
    smpl[8 + 36 + 12] = 0x4B; // to sample 1099
    smpl[8 + 36 + 13] = 0x04;
    endWav.insert(endWav.begin() + 36, smpl.begin(), smpl.end());
    const uint32_t riffLong = (uint32_t)endWav.size() - 8;
    memcpy(endWav.data() + 4, &riffLong, 4);
    SoundClip looped, ended, air;
    std::vector<uint8_t> carried;
    const bool mark = SoundDecode(loopWav, looped, why) && looped.marked && looped.loopFrom == 100 && looped.x.size() == 900 && !looped.loopTo && !clip.marked && !clip.loopFrom &&
                      SoundDecode(endWav, ended, why) && ended.loopFrom == 100 && ended.loopTo == 1100 && ended.x.size() == 2000 && SoundLoopEnd(ended) == 1100 && SoundLoopEnd(looped) == 900;
    printf("%s sound: a loop marked in a WAV file is read (from sample %zu, %zu samples in all), and what follows it is kept as the ending when it is long enough (%zu samples)\n",
           mark ? "ok  " : "FAIL", looped.loopFrom, looped.x.size(), ended.x.size() - SoundLoopEnd(ended));
    // the two recordings this program carries, as the mod does: each a start, a loop of a second or more, an ending
    bool own = true;
    for (const int k : { kVoiceHiss, kVoiceAir })
    {
        const bool read = SoundCarried(kSoundOwn[k].carried, carried) && SoundDecode(carried, air, why) && air.marked && air.loopFrom > air.rate / 4 &&
                          SoundLoopEnd(air) - air.loopFrom > air.rate && air.x.size() - SoundLoopEnd(air) > air.rate / 4;
        printf("%s sound: the recording of the %ls that the mod carries reads at %u Hz as a start of %.2f s, %.2f s that go round and an ending of %.2f s\n",
               read ? "ok  " : "FAIL", kSoundVoices[k].what, air.rate, air.rate ? (double)air.loopFrom / air.rate : 0.0,
               air.rate ? (double)(SoundLoopEnd(air) - air.loopFrom) / air.rate : 0.0, air.rate ? (double)(air.x.size() - SoundLoopEnd(air)) / air.rate : 0.0);
        own = own && read;
        air = SoundClip{};
    }
    // the sounds made by code: the two that go round join, the one played once dies away
    bool loops = true;
    float seams[2] = {}, rms[2] = {};
    for (int k = 0; k < 2; k++)
    {
        SoundClip c;
        if (k == 0) SoundMakeHiss(c);
        else SoundMakeCompressor(c);
        SoundLevel(c, false);
        float steepest = 0.0f, peak = 0.0f;
        bool finite = true;
        for (size_t i = 0; i < c.x.size(); i++)
        {
            finite = finite && isfinite(c.x[i]);
            peak = max(peak, fabsf(c.x[i]));
            if (i) steepest = max(steepest, fabsf(c.x[i] - c.x[i - 1]));
        }
        seams[k] = fabsf(c.x.front() - c.x.back()) / max(1e-9f, steepest);
        rms[k] = SoundRms(c.x);
        loops = loops && finite && c.rate == 44100 && c.x.size() >= 44100 && seams[k] <= 1.0f && rms[k] <= kSoundRms * 1.001f && rms[k] >= kSoundRms * 0.5f && peak <= kSoundPeak * 1.001f;
    }
    SoundClip puff;
    SoundMakePuff(puff);
    SoundLevel(puff, true);
    float puffPeak = 0.0f;
    for (const float v : puff.x) puffPeak = max(puffPeak, fabsf(v));
    loops = loops && puff.x.size() == 44100 && puffPeak > 0.05f && puffPeak <= kSoundPeak * 1.001f && fabsf(puff.x.back()) < 0.01f;
    printf("%s sound: the loops made by code join (the step from end to start is %.2f and %.2f of the steepest inside) at loudness %.3f and %.3f; the one played once dies away\n",
           loops ? "ok  " : "FAIL", seams[0], seams[1], rms[0], rms[1]);
    // the shapes: silent at nothing, a pitch a voice can play, and never past full scale at the highest volume with
    // every peak at once: the air let out alone, and the three that can fall together at a filling
    bool shapes = true;
    for (float env = 0.0f; env <= 1.0f; env += 0.05f)
        for (float p = -0.5f; p <= 1.0f; p += 0.1f) // from -0.5: air let out of tires above Normal
        {
            float together = 0.0f;
            for (int k = 0; k < kVoiceCount; k++)
            {
                float gain = -1.0f, pitch = -1.0f;
                SoundShapeAt(k, env, k == kVoiceHiss ? p : max(0.0f, p), gain, pitch);
                shapes = shapes && isfinite(gain) && isfinite(pitch) && gain >= 0.0f && (kSoundVoices[k].once || env > 0.0f || gain == 0.0f) && pitch >= 0.4f && pitch <= 1.3f;
                shapes = shapes && gain * kSoundVoices[k].level * kSoundPeak <= 1.0f;
                if (k != kVoiceHiss) together += gain * kSoundVoices[k].level * kSoundPeak;
            }
            shapes = shapes && together <= 1.0f;
        }
    float atNormal = 0.0f, atLow = 0.0f, same = 0.0f;
    SoundShapeAt(kVoiceHiss, 1.0f, 0.0f, atNormal, same);
    SoundShapeAt(kVoiceHiss, 1.0f, 1.0f, atLow, same);
    shapes = shapes && atNormal == 1.0f && fabsf(20.0f * log10f(atLow) + 9.0f) < 0.1f;
    printf("%s sound: loudness and pitch stay within what a voice takes, at any moment; the air let out is %.1f dB weaker at Low than at Normal\n", shapes ? "ok  " : "FAIL",
           -20.0f * log10f(atLow));
    // the tank: fillings of two modes (Low to Normal, 6 s) start the compressor in the second, fillings of one mode
    // (3 s) in the fourth; it then runs on for a quarter of a minute or so and says once that it has stopped
    auto startsIn = [](float modes, float &ranFor) {
        AirTank t;
        int started = 0, stops = 0;
        ranFor = 0.0f;
        for (int filling = 1; filling <= 8 && !stops; filling++)
            for (float at = 0.0f, seconds = 3.0f * modes; at < seconds + 40.0f; at += kPass) // the filling, then nothing for a while
            {
                const bool was = t.compressor;
                stops += TankStep(t, at < seconds ? modes * kPass / seconds : 0.0f, kPass);
                if (t.compressor && !was && !started) started = filling;
                if (t.compressor) ranFor += kPass;
            }
        return stops == 1 ? started : -1;
    };
    float ranTwo = 0.0f, ranOne = 0.0f;
    const int two = startsIn(2.0f, ranTwo), one = startsIn(1.0f, ranOne);
    const bool tank = two == 2 && one == 4 && ranTwo > 8.0f && ranTwo < 30.0f && ranOne > 8.0f && ranOne < 30.0f;
    printf("%s sound: the compressor starts in filling %d of two modes each and runs %.0f s, in filling %d of one mode each and runs %.0f s\n", tank ? "ok  " : "FAIL",
           two, ranTwo, one, ranOne);
    // the time a change is said to take against the passes it takes (the defaults: Normal to Low all of Seconds,
    // Normal to Reduced four fifths of it, as the dirt grip has that far to go)
    TpSettings d;
    SettingsDefaults(d);
    ApplySettings(d, false);
    bool times = true;
    float said[2] = {};
    static const int to[2] = { kLow, kReduced };
    for (int i = 0; i < 2; i++)
    {
        g_mode = kNormal;
        g_now = g_modes[kNormal];
        g_mode = to[i];
        said[i] = SecondsToMode();
        int passes = 1;
        while (passes < 100000 && !StepTowardsMode()) passes++;
        times = times && fabsf(said[i] - (float)passes * kPass) < 0.001f && fabsf(said[i] - d.seconds * (i ? 0.8f : 1.0f)) < 0.11f && SecondsToMode() == 0.0f;
    }
    g_mode = kNormal;
    g_now = g_modes[kNormal];
    printf("%s sound: a change is said to take as long as it does (Normal to Low %.2f s, to Reduced %.2f s, at Seconds=%g)\n", times ? "ok  " : "FAIL", said[0], said[1], d.seconds);
    return plain && adpcm && paks && refused && mark && own && loops && shapes && tank && times;
}

// "probe_test sound [volume 0..100] [seconds] [pak] [the four names]": plays what the mod plays in the game, through
// the default audio device: the air let out for `seconds`; then a filling of `seconds` in whose middle the compressor
// starts, to run on for two seconds and stop with its air let go. Not part of test.bat: it needs a device and ears.
static int SoundPlay(int argc, wchar_t **argv)
{
    const float volume = argc > 2 ? min(1.0f, max(0.0f, (float)_wtof(argv[2]) / 100.0f)) : 0.5f, seconds = argc > 3 ? max(0.5f, (float)_wtof(argv[3])) : 6.0f;
    std::wstring names[kVoiceCount];
    for (int k = 0; k < kVoiceCount && argc > 5 + k; k++) names[k] = argv[5 + k];
    SoundNames(names);
    if (!SoundStart(g_dir, argc > 4 ? argv[4] : L"")) { printf("FAIL sound: no thread\n"); return 1; }
    SoundWant want = {};
    want.volume = volume;
    auto pass = [&]() { SoundSet(want); Sleep(50); };
    want.on[kVoiceHiss] = true;
    for (float t = 0.0f; t < seconds; t += kPass) { want.progress[kVoiceHiss] = t / seconds; pass(); }
    want.on[kVoiceHiss] = false;
    for (float t = 0.0f; t < 1.0f; t += kPass) pass();
    want.on[kVoiceAir] = true;
    for (float t = 0.0f; t < seconds + 2.0f; t += kPass)
    {
        want.on[kVoiceAir] = t < seconds;
        want.on[kVoiceCompressor] = t >= seconds / 2.0f;
        want.progress[kVoiceAir] = min(1.0f, t / seconds);
        want.progress[kVoiceCompressor] = max(0.0f, (t - seconds / 2.0f) / (seconds / 2.0f + 2.0f));
        pass();
    }
    want.on[kVoiceCompressor] = false;
    want.shots[kVoicePurge] = 1;
    for (float t = 0.0f; t < 2.5f; t += kPass) pass();
    const bool ok = g_soundOpens >= 1 && (float)g_soundPlayedMs[kVoiceHiss] > seconds * 800.0f && g_soundEndings[kVoiceHiss] == 1 &&
                    (float)g_soundPlayedMs[kVoiceAir] > seconds * 800.0f &&
                    (float)g_soundPlayedMs[kVoiceCompressor] > (seconds / 2.0f + 2.0f) * 800.0f && g_soundStarts[kVoiceAir] == 1 && g_soundEndings[kVoiceAir] == 1 &&
                    g_soundStarts[kVoicePurge] == 1;
    printf("%s sound: %ld engine starts; the air let out played %ld ms, the air going in %ld ms with its ending %ld time(s), the compressor %ld ms, its stop %ld time(s) (%.1f s each way, volume %.0f%%); see TirePressure.log next to this program\n",
           ok ? "ok  " : "FAIL", (long)g_soundOpens, (long)g_soundPlayedMs[kVoiceHiss], (long)g_soundPlayedMs[kVoiceAir], (long)g_soundEndings[kVoiceAir],
           (long)g_soundPlayedMs[kVoiceCompressor], (long)g_soundStarts[kVoicePurge], seconds, volume * 100.0f);
    return ok ? 0 : 1;
}

// "probe_test loops <folder> [pak] [sample name]": the two loops made by code, and a sample out of a pak, as WAV files.
static int SoundLoops(int argc, wchar_t **argv)
{
    const std::wstring dir = std::wstring(argv[2]) + L"\\";
    SoundClip hiss, compressor, sample;
    SoundMakeHiss(hiss);
    SoundMakeCompressor(compressor);
    SoundLevel(hiss, false);
    SoundLevel(compressor, false);
    bool ok = TestSaveLoop(dir + L"code_hiss.wav", hiss) && TestSaveLoop(dir + L"code_compressor.wav", compressor);
    if (argc > 4)
    {
        std::vector<uint8_t> file;
        std::wstring why;
        const bool got = PakRead(argv[3], argv[4], file, why) && SoundDecode(file, sample, why);
        if (got) printf("     %ls: %.2f s at %u Hz, loudness %.4f\n", argv[4], (double)sample.x.size() / sample.rate, sample.rate, SoundRms(sample.x));
        else printf("FAIL %ls: %ls\n", argv[4], why.c_str());
        ok = ok && got && TestSaveLoop(dir + L"pak_sample.wav", sample);
    }
    printf("%s loops written to %ls\n", ok ? "ok  " : "FAIL", argv[2]);
    return ok ? 0 : 1;
}

int wmain(int argc, wchar_t **argv)
{
    // "probe_test find <exe or image>": only the look through that exe
    if (argc > 2 && !wcscmp(argv[1], L"find")) return BuildTest(argv[2]) ? 0 : 1;
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    g_dir.assign(path, wcsrchr(path, L'\\') + 1 - path);
    g_base = (uint64_t)GetModuleHandleW(nullptr);
    if (argc > 1 && !wcscmp(argv[1], L"sound")) return SoundPlay(argc, argv);
    if (argc > 2 && !wcscmp(argv[1], L"loops")) return SoundLoops(argc, argv);
    if (!IniTest(argc > 1 ? argv[1] : nullptr)) return 1;
    if (!FlattenTest()) return 1;
    if (!SoundTest()) return 1;
    wchar_t imageFile[1024] = {};
    GetEnvironmentVariableW(L"SR_IMAGE", imageFile, 1024);
    if (!BuildTest(imageFile)) return 1;
    volatile LONG stop = 0;
    const HANDLE t = CreateThread(nullptr, 0, TestWriter, (void *)&stop, 0, nullptr);
    Sleep(100);
    AddVectoredExceptionHandler(1, ProbeHandler);
    SetWatch((uint64_t)&g_testValue);
    Sleep(500);
    SetWatch(0);
    Sleep(100);
    const float afterClear = g_testValue;
    Sleep(100);
    stop = 1;
    const bool ended = WaitForSingleObject(t, 2000) == WAIT_OBJECT_0;
    LogWriters(false);
    int writers = 0, viaRet = 0;
    long hits = 0;
    for (const Writer &w : g_writersSeen) if (w.key) { writers++; hits += w.count; viaRet += w.viaRet; }
    // (how many writes fit into the half second depends on the machine's timer: a handful is proof enough)
    const bool ok = writers == 2 && viaRet == 1 && hits >= 8 && ended && g_testValue > afterClear;
    printf("%s write breakpoint: %d writers (%d through the setter), %ld writes seen, writer thread %s and still writing after the clear\n",
           ok ? "ok  " : "FAIL", writers, viaRet, hits, ended ? "ended normally" : "DID NOT END");
    return ok ? 0 : 1;
}
#endif
