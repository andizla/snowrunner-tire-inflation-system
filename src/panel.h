// panel.h: the Tire Inflation System panel (drawn through ReShade's add-on overlay), its settings page in ReShade's
// overlay, and the gamepad (XInput) side of TirePressure.asi. tire_pressure.cpp runs the logic and owns the settings;
// the render thread only reads g_view and hands edited settings back through SettingsPut.
#pragma once
#include <windows.h>
#include <xinput.h>
#define TP_VERSION "1.1.0"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The pressure modes from the lowest pressure up; the panel's gauge runs from Low (left) to the highest one in use
// (right). Increased is optional (ini Increased).
enum PressureMode { kLow, kReduced, kNormal, kIncreased, kModeCount };

// One pressure mode as the panel shows it: the multipliers against Normal.
struct PanelMode { const char *title; float ground, asphalt, mud, fuel; };

struct PanelView
{
    volatile LONG open;      // 1 while the panel is shown
    volatile LONG selected;  // what the panel points at (PressureMode)
    volatile LONG current;   // the mode the truck is in (or moving to)
    volatile LONG confirmMs; // time until the selection confirms itself, -1 = none
    volatile LONG viaPad;    // 1 = the pad opened it: show pad glyphs, 0 = the keyboard
    volatile LONG count;     // modes on the gauge: 3 (Low to Normal) or 4 (Increased too)
    volatile LONG warn;      // 1 while the truck is too fast for its tire pressure (the tires take damage): the warning
    volatile LONG wear;      // the most worn tire's damage in percent of what it takes, -1 = not known (shown in the warning)
};

extern PanelView g_view;
extern PanelMode g_panelModes[kModeCount];
extern float g_panelScale;        // ini UIScale
// The keyboard's keys as the panel shows them: the one that opens it ("F3"), and those that lower, raise and confirm
// while it is open ("" = none).
struct PanelKeys { char open[16], lower[16], raise[16], confirm[16]; };
extern PanelKeys g_panelKeys;
extern int g_panelConfirmMs;      // ini ConfirmSeconds in ms: the time the choice takes to confirm itself
extern volatile LONG g_pageCapturing; // 1 while the settings page waits for pad buttons: the mod ignores the pad then
extern volatile LONG g_modOff;        // 1 = the mod stood down (a game version it cannot work on): the page says so
extern volatile LONG g_padThread, g_padThreadChanges, g_overlayThread; // thread ids, for the log (0 = not seen yet)

bool PanelInit(HMODULE self);     // registers with ReShade (panel + settings page); false = no ReShade
void PanelLog(const wchar_t *fmt, ...); // a line in TirePressure.log (tire_pressure.cpp)

// Everything in TirePressure.ini that the settings page edits ([TirePressure], [Reduced], [Low], [Increased]).
struct TpSettings
{
    int key;                 // Key: virtual key code of the panel key
    int panelKeys[4];        // KeyLower, KeyRaise, KeyConfirm, KeyCancel: the keyboard's keys while the panel is open (0 = none)
    bool beep;               // Beep
    float seconds;           // Seconds: how long a pressure change takes
    float soundVolume;       // SoundVolume: air and compressor while the pressure changes, 0..100 (0 = no sound)
    bool ui;                // UI: the panel (off = the key cycles the modes directly)
    float uiScale;           // UIScale
    float confirmSeconds;    // ConfirmSeconds: the panel's choice confirms itself after this (0 = never)
    char pad[5][32];         // PadOpen, PadLower, PadRaise, PadConfirm, PadCancel ("LB+DPadDown", "None")
    float base[3];           // BaseGround, BaseAsphalt, BaseMud: grip of every mode, Normal too
    bool vanillaBalance;     // VanillaBalance: tires that out-grip every vanilla tire come down to the vanilla envelope
    float balanceStrength;   // VanillaBalanceStrength 0..1
    float asphaltFloor;      // AsphaltFloor: every tire grips at least this on paved ground, before the modes (0 = off)
    bool increased;          // Increased: the Increased pressure mode above Normal
    bool tireDamage;         // TireDamage: driving too fast for the pressure wears the tires (the modes' MinVel .. DamageTick)
    bool rollingRadius;      // RollingRadius: the gears make up for the flattened wheels, but for what a real tire loses
    float mode[3][14];       // [Reduced], [Low], [Increased]: the keys of kModeKeys
};
static const int kSettingsModes[3] = { kReduced, kLow, kIncreased }; // PressureMode of TpSettings::mode[i]
static const char *const kPadKeys[5] = { "PadOpen", "PadLower", "PadRaise", "PadConfirm", "PadCancel" };
static const char *const kPanelKeyNames[4] = { "KeyLower", "KeyRaise", "KeyConfirm", "KeyCancel" }; // TpSettings::panelKeys[i]
// Per mode: grip multipliers on plain ground (dirt, and whatever is none of the others), on paved ground and in mud,
// metres off the tire's radius, multipliers for fuel use and steering speed; then the ground grip again where the
// ground is gravel, sand or rock, and Expeditions' TickDamageParams: above MinVel (m/s) every wheel on the ground takes
// MinDamage every DamageTick seconds, rising to MaxDamage at MaxVel.
enum { kModeGround, kModeAsphalt, kModeMud, kModeOffset, kModeFuel, kModeSteering, kModeGravel, kModeSand, kModeRock,
       kModeMinVel, kModeMaxVel, kModeMinDamage, kModeMaxDamage, kModeDamageTick, kModeKeyCount };
static const char *const kModeKeys[kModeKeyCount] = { "OnModelFriction", "BodyFrictionAsphalt", "SubstanceFriction", "AdditionalRadiusOffset",
                                                      "FuelConsumptionModifier", "SteeringSpeedModifier", "GravelFriction", "SandFriction",
                                                      "RockFriction", "MinVel", "MaxVel", "MinDamage", "MaxDamage", "DamageTick" };
// The range of each key (Expeditions' own ranges for its keys).
static const struct { float lo, hi; } kModeRange[kModeKeyCount] = { { 0.1f, 10.0f }, { 0.1f, 10.0f }, { 0.1f, 10.0f }, { 0.0f, 0.25f }, { 0.1f, 10.0f },
                                                                    { 0.1f, 10.0f }, { 0.1f, 10.0f }, { 0.1f, 10.0f }, { 0.1f, 10.0f }, { 0.0f, 100.0f },
                                                                    { 0.0f, 100.0f }, { 0.0f, 100.0f }, { 0.0f, 100.0f }, { 1.0f, 100.0f } };

// Seconds: Expeditions' own change takes 3. With the sounds of 1.1.0 it takes 6 here, and an ini from before that
// which still says 3 is taken along (kIniVersion, ReadIni).
static const float kSecondsDefault = 6.0f, kSecondsBefore = 3.0f;
static const int kIniVersion = 2; // ini IniVersion: 2 since 1.1.0; an ini without the key is from 1.0

inline void SettingsDefaults(TpSettings &s)
{
    s = TpSettings{};
    s.key = VK_F3;
    // the arrow keys step the open panel, Enter confirms, Backspace closes it unchanged
    static const int keys[4] = { VK_LEFT, VK_RIGHT, VK_RETURN, VK_BACK };
    memcpy(s.panelKeys, keys, sizeof keys);
    s.beep = true;
    s.seconds = kSecondsDefault;
    s.soundVolume = 50.0f;
    s.ui = true;
    s.uiScale = 1.0f;
    s.confirmSeconds = 15.0f;
    // with LB, as plain d-pad presses open the game's own screens and move through its menus
    static const char *pads[5] = { "LB+DPadDown", "LB+DPadLeft", "LB+DPadRight", "A", "B" };
    for (int i = 0; i < 5; i++) strcpy_s(s.pad[i], pads[i]);
    s.base[0] = s.base[1] = s.base[2] = 1.0f;
    s.vanillaBalance = false;
    s.balanceStrength = 1.0f;
    s.asphaltFloor = 0.0f;
    s.increased = true;
    s.tireDamage = true;
    s.rollingRadius = true;
    // Reduced and Low: the "default tire inflation" set of an Expeditions tire file, but for asphalt: Expeditions'
    // 1.3 / 2.0 are for its rock models, while in SnowRunner that grip only acts on paved ground (IsAsphalt roads,
    // concrete, pavers, bridges; rocks count as ground). Aired down on pavement the wider patch is eaten up by the
    // softer sidewall (vaguer steering, squirm), more so the lower it goes, so paved grip only falls below Normal.
    // Increased: a road mode. Expeditions' own (newer tire files) trades some grip for fuel and steering; here it also
    // grips better on asphalt, where SnowRunner's tires are slippery, and less on dirt and in mud.
    // By ground: a soft tire gains most on sand (it floats), as much on rock as on dirt (it wraps), least on gravel
    // (a loose layer over a hard one); a hard tire loses in the same order.
    // Damage at speed: the TickDamageParams of Expeditions' off-road tires (its _templates\trucks.xml: Low 2 to 4 from
    // 27 to 36 km/h, Reduced 1 to 3 from 36 to 54 km/h) with twice the damage at lower speeds (set by driving on
    // 2026-10-04: a SnowRunner truck is slower off the road and its tires hardly wore). In metres per second: Low from
    // 20 to 30 km/h, Reduced from 35 to 45 km/h (1.0.0 had 15 to 25 and 25 to 35, which more driving found too low).
    // A SnowRunner wheel takes about 50 damage.
    static const float modes[3][kModeKeyCount] = {
        { 3.0f, 0.95f, 1.15f, 0.06f, 1.25f, 0.85f, 2.4f, 3.4f, 3.0f, 35.0f / 3.6f, 45.0f / 3.6f, 2.0f, 6.0f, 10.0f },
        { 3.5f, 0.85f, 1.3f, 0.10f, 1.5f, 0.7f, 2.75f, 4.0f, 3.5f, 20.0f / 3.6f, 30.0f / 3.6f, 4.0f, 8.0f, 7.0f },
        { 0.9f, 1.3f, 0.85f, 0.0f, 0.85f, 1.1f, 0.95f, 0.85f, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f } };
    memcpy(s.mode, modes, sizeof modes);
}

void SettingsGet(TpSettings &out);        // the settings in force (any thread)
void SettingsPut(const TpSettings &in);   // edited settings: the mod applies them at once and saves the ini
void SettingsReload();                    // read the ini again (the page's button)

// What the settings page shows about the truck being driven.
struct TpLive
{
    int wheels;                  // 0 = no truck driven
    float ground, asphalt, mud;  // the first wheel's own grip (from its tire file)
    float balance;               // the vanilla balance factor on that wheel (1 = untouched)
    int mode;                    // PressureMode
    int surface;                 // the ground under the first wheel (kSurface...)
    float speed;                 // km/h
    int damage, capacity;        // the first wheel's tire damage; capacity 0 = not found
    float room;                  // metres the game can draw the first wheel's tire flatter (the modes' flattening fits in it); below 0 = not known
};
enum { kSurfaceDirt, kSurfaceGravel, kSurfaceSand, kSurfaceRock, kSurfacePaved };
void LiveGet(TpLive &out);

// Gamepad. The game loads XInputGetState itself and keeps it in more than one pointer; PadInit points every copy at a
// filter that hides the panel's buttons from the game: the open binding whenever it is held, a step binding of two or
// more buttons whenever it is held (it opens the panel too), every panel button while the panel is open, and any of
// them still held when it closes, until released. Called again it catches copies made since. Without the filter (no
// pointer found) the pad still works the panel, but the game sees the presses too.
int PadInit(uint64_t exeBase);            // pointers newly sent through the filter
void PadMasks(uint32_t open, uint32_t lower, uint32_t raise, uint32_t confirm, uint32_t cancel); // the bindings
void PadLive(bool live);                  // the filter only takes buttons from the game while live (a truck is driven)
uint32_t PadButtons();                    // the buttons of all connected pads together, 0 without a pad

// A binding of two or more buttons (a step binding like that also works while the panel is closed).
inline bool PadChord(uint32_t mask) { return (mask & (mask - 1)) != 0; }
// While the panel is open the d-pad is the panel's, as in Expeditions: a binding also counts without these.
static const uint32_t kPadShoulders = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER;

static const struct { const char *name; uint32_t mask; } kPadButtons[14] = {
    { "DPadUp", XINPUT_GAMEPAD_DPAD_UP }, { "DPadDown", XINPUT_GAMEPAD_DPAD_DOWN }, { "DPadLeft", XINPUT_GAMEPAD_DPAD_LEFT },
    { "DPadRight", XINPUT_GAMEPAD_DPAD_RIGHT }, { "Start", XINPUT_GAMEPAD_START }, { "Back", XINPUT_GAMEPAD_BACK },
    { "LS", XINPUT_GAMEPAD_LEFT_THUMB }, { "RS", XINPUT_GAMEPAD_RIGHT_THUMB }, { "LB", XINPUT_GAMEPAD_LEFT_SHOULDER },
    { "RB", XINPUT_GAMEPAD_RIGHT_SHOULDER }, { "A", XINPUT_GAMEPAD_A }, { "B", XINPUT_GAMEPAD_B }, { "X", XINPUT_GAMEPAD_X },
    { "Y", XINPUT_GAMEPAD_Y } };

// "LB+DPadDown" -> XINPUT_GAMEPAD_* mask; 0 for "None" or a name that is not a button
inline uint32_t PadParse(const char *text)
{
    char buf[64];
    strncpy_s(buf, text, _TRUNCATE);
    uint32_t mask = 0;
    char *ctx = nullptr;
    for (char *tok = strtok_s(buf, "+ ", &ctx); tok; tok = strtok_s(nullptr, "+ ", &ctx))
    {
        uint32_t m = 0;
        for (const auto &b : kPadButtons) if (!_stricmp(tok, b.name)) m = b.mask;
        if (!m) return 0;
        mask |= m;
    }
    return mask;
}

// mask -> "LB+DPadDown" (shoulders and sticks first), "None" for 0
inline void PadName(uint32_t mask, char *out, size_t size)
{
    static const int order[14] = { 8, 9, 6, 7, 4, 5, 0, 1, 2, 3, 10, 11, 12, 13 };
    out[0] = 0;
    for (int i : order)
        if ((mask & kPadButtons[i].mask) && strlen(out) + strlen(kPadButtons[i].name) + 2 <= size) // a name that does not fit is left out
        {
            if (out[0]) strcat_s(out, size, "+");
            strcat_s(out, size, kPadButtons[i].name);
        }
    if (!out[0]) strcpy_s(out, size, "None");
}

// A virtual key's name as the panel and the settings page show it.
inline void KeyName(int vk, char *out, size_t size)
{
    static const struct { int vk; const char *name; } names[] = {
        { VK_INSERT, "Insert" }, { VK_DELETE, "Delete" }, { VK_HOME, "Home" }, { VK_END, "End" }, { VK_PRIOR, "Page Up" },
        { VK_NEXT, "Page Down" }, { VK_UP, "Up" }, { VK_DOWN, "Down" }, { VK_LEFT, "Left" }, { VK_RIGHT, "Right" },
        { VK_PAUSE, "Pause" }, { VK_SCROLL, "Scroll Lock" }, { VK_TAB, "Tab" }, { VK_SPACE, "Space" }, { VK_BACK, "Backspace" },
        { VK_RETURN, "Enter" } };
    if (vk >= VK_F1 && vk <= VK_F24) sprintf_s(out, size, "F%d", vk - VK_F1 + 1);
    else if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) sprintf_s(out, size, "%c", (char)vk);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) sprintf_s(out, size, "Num %d", vk - VK_NUMPAD0);
    else
    {
        sprintf_s(out, size, "Key %d", vk);
        for (const auto &n : names) if (n.vk == vk) strcpy_s(out, size, n.name);
    }
}
