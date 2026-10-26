// panel.h: the Tire Inflation System panel (drawn by Next Gen's own overlay), its Tire Inflation tab in the NEXT GEN
// menu, and the gamepad (XInput) side of TirePressure.asi. tire_pressure.cpp runs the logic and owns the settings;
// the render thread only reads g_view and hands edited settings back through SettingsPut.
#pragma once
#include <windows.h>
#include <xinput.h>
#define TP_VERSION "1.3.2"
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
    volatile LONG hold;      // 1 while a pressure change waits for the truck to stand still (StandStill): the hint
};

extern PanelView g_view;
extern PanelMode g_panelModes[kModeCount];
extern float g_panelScale;        // ini UIScale
extern char g_panelKeyName[16];   // the keyboard key that opens and steps the panel, as text ("F3")
extern int g_panelConfirmMs;      // ini ConfirmSeconds in ms: the time the choice takes to confirm itself
extern volatile LONG g_pageCapturing; // 1 while the settings page waits for pad buttons: the mod ignores the pad then
extern volatile LONG g_modOff;        // 1 = the mod stood down (a game version it cannot work on): the page says so
extern volatile LONG g_padThread, g_padThreadChanges, g_overlayThread; // thread ids, for the log (0 = not seen yet)

bool PanelInit(HMODULE self);     // starts the overlay (panel + the menu's tab); false = it cannot start
void PanelLog(const wchar_t *fmt, ...); // a line in TirePressure.log (tire_pressure.cpp)

// Everything in TirePressure.ini that the settings page edits ([TirePressure], [Reduced], [Low], [Increased]).
struct TpSettings
{
    int key;                 // Key: virtual key code of the panel key
    bool beep;               // Beep
    float seconds;           // Seconds: how long a pressure change takes
    float soundVolume;       // SoundVolume: air and compressor while the pressure changes, 0..100 (0 = no sound)
    bool ui;                // UI: the panel (off = the key cycles the modes directly)
    float uiScale;           // UIScale
    float confirmSeconds;    // ConfirmSeconds: the panel's choice confirms itself after this (0 = never)
    char pad[5][32];         // PadOpen, PadLower, PadRaise, PadConfirm, PadCancel ("LB+DPadDown", "None")
    float padCurve[2];       // PadThrottleCurve, PadBrakeCurve: a trigger's share to this power before the game reads it, 1 (as it is) to 3, 2 by default
    float base[3];           // BaseGround, BaseAsphalt, BaseMud: grip of every mode, Normal too
    bool vanillaBalance;     // VanillaBalance: tires that out-grip every vanilla tire come down to the vanilla envelope
    float balanceStrength;   // VanillaBalanceStrength 0..2 (above 1: below the vanilla envelope)
    float asphaltFloor;      // AsphaltFloor: every tire grips at least this on paved ground, before the modes (0 = off)
    float wornGrip;          // WornGrip: the share of its grip a worn out or broken tire keeps, percent, 0 to 30 (0 = none)
    bool increased;         // Increased: the Increased pressure mode above Normal
    bool tireDamage;         // TireDamage: driving too fast for the pressure wears the tires (the modes' MinVel .. DamageTick)
    bool rollingRadius;      // RollingRadius: the gears make up for the flattened wheels, but for what a real tire loses
    bool standStill;         // StandStill: a pressure change only goes on while the truck stands still
    bool perTruck;           // PerTruck: each truck keeps its own pressure mode (TirePressure.trucks.ini); 0 = one mode for all, Normal at every start
    bool temperature;       // Temperature: the tires feel the outside temperature (the air in them, the rubber's grip; outside.h)
    float ambientC;          // AmbientC: the outside temperature where no other mod publishes one, -40 to 50
    bool hardMode;           // HardMode: the realistic numbers and options (HardSettings) in place of the settings above and the modes
    bool insaneMode;         // InsaneMode: a hardcore set harder than real tires (InsaneSettings) in the same place; it wins over HardMode
    float mode[3][14];       // [Reduced], [Low], [Increased]: the keys of kModeKeys
};
static const int kSettingsModes[3] = { kReduced, kLow, kIncreased }; // PressureMode of TpSettings::mode[i]
static const char *const kPadKeys[5] = { "PadOpen", "PadLower", "PadRaise", "PadConfirm", "PadCancel" };
static const char *const kPadCurveKeys[2] = { "PadThrottleCurve", "PadBrakeCurve" }; // TpSettings::padCurve: the right and the left trigger
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
// ConfirmSeconds: the keyboard has no key to confirm with, its choice applies when this time is up. 5 since 1.1.0
// (15 before), and an ini from before that which still says 15 is taken along the same way.
static const float kConfirmDefault = 5.0f, kConfirmBefore = 15.0f;
static const int kIniVersion = 2; // ini IniVersion: 2 since 1.1.0; an ini without the key is from 1.0
// WornGrip: enough grip that a truck on a worn out tire still moves (the testers asked for 10 to 12 percent).
static const float kWornGripDefault = 10.0f, kWornGripMost = 30.0f;

inline void SettingsDefaults(TpSettings &s)
{
    s = TpSettings{};
    s.key = VK_F3;
    s.beep = false; // off since 1.1.0, which has sounds for a change
    s.seconds = kSecondsDefault;
    s.soundVolume = 50.0f;
    s.ui = true;
    s.uiScale = 1.0f;
    s.confirmSeconds = kConfirmDefault;
    // with LB, as plain d-pad presses open the game's own screens and move through its menus
    static const char *pads[5] = { "LB+DPadDown", "LB+DPadLeft", "LB+DPadRight", "A", "B" };
    for (int i = 0; i < 5; i++) strcpy_s(s.pad[i], pads[i]);
    s.padCurve[0] = s.padCurve[1] = 2.0f; // both triggers shaped: half the trigger gives a quarter (the author's choice after a pad drive, 2026-10-24)
    s.base[0] = s.base[1] = s.base[2] = 1.0f;
    s.vanillaBalance = false;
    s.balanceStrength = 1.0f;
    s.asphaltFloor = 0.0f;
    s.wornGrip = kWornGripDefault;
    s.increased = true;
    s.tireDamage = true;
    s.rollingRadius = true;
    s.standStill = true;
    s.perTruck = true;
    s.temperature = true;
    s.ambientC = 15.0f;
    s.hardMode = false;
    s.insaneMode = false;
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

// Each mode's pressure as a share of Normal's: the scale the tires' pressure moves along between modes, and what the
// outside temperature works on. Normal is taken as 35 psi; the others are this mod's choice.
static const float kModePressure[kModeCount] = { 0.45f, 0.65f, 1.0f, 1.25f };

// Hard mode: the numbers and options nearest to real tires (HardMode=1). Airing a tire down to half its pressure
// lengthens its contact patch by about half: that much more grip on sand and soft ground, a little less on hard
// ground; over-inflating does the reverse and gains nothing on asphalt. The flattening is a few centimetres. Rolling
// resistance, and the fuel with it, rises as the pressure falls. A soft tire's sidewalls flex and heat at speed, so
// its wear starts higher up than the defaults but ends a tire faster. A change takes half a minute, as a truck's
// compressor does. The numbers are this mod's; the reasoning is the physics above.
static const float kHardModes[3][kModeKeyCount] = {
    { 1.3f, 0.9f, 1.18f, 0.025f, 1.12f, 0.9f, 1.15f, 1.45f, 1.3f, 50.0f / 3.6f, 75.0f / 3.6f, 1.0f, 6.0f, 10.0f },  // Reduced
    { 1.6f, 0.8f, 1.35f, 0.05f, 1.25f, 0.8f, 1.3f, 1.9f, 1.6f, 30.0f / 3.6f, 55.0f / 3.6f, 2.0f, 10.0f, 8.0f },   // Low
    { 0.92f, 0.97f, 0.9f, 0.0f, 0.96f, 1.05f, 0.95f, 0.85f, 0.92f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f } };            // Increased
static const float kHardSeconds = 30.0f;

// Puts hard mode's numbers and options into s: everything but the controls, the panel, the sounds and the outside
// temperature, which stay the player's.
inline void HardSettings(TpSettings &s)
{
    s.seconds = kHardSeconds;
    s.base[0] = s.base[1] = s.base[2] = 1.0f;
    s.vanillaBalance = true;
    s.balanceStrength = 1.0f;
    s.asphaltFloor = 0.0f;
    s.wornGrip = kWornGripDefault;
    s.increased = true;
    s.tireDamage = true;
    s.rollingRadius = true;
    s.standStill = true;
    s.temperature = true;
    memcpy(s.mode, kHardModes, sizeof s.mode);
}

// Globalhacker insanity mode: harder than real tires, built from the player Globalhacker's own hardcore set (2026-10). Low grips x2 on dirt and x0.4 on
// asphalt; soft tires wear from 15 km/h (Low) and 20 km/h (Reduced) and are worn out within seconds a little faster;
// flattened tires lose the speed the smaller wheel loses; no Increased mode; the vanilla balance at 2 puts tires that
// out-grip every vanilla tire below it.
static const float kInsaneModes[3][kModeKeyCount] = {
    { 1.5f, 0.45f, 1.25f, 0.06f, 1.5f, 0.75f, 1.3f, 1.7f, 1.5f, 20.0f / 3.6f, 25.0f / 3.6f, 4.0f, 12.0f, 2.0f },  // Reduced
    { 2.0f, 0.4f, 1.5f, 0.12f, 1.75f, 0.6f, 1.6f, 2.3f, 2.0f, 15.0f / 3.6f, 20.0f / 3.6f, 8.0f, 16.0f, 2.0f },     // Low
    { 0.92f, 0.97f, 0.9f, 0.0f, 0.96f, 1.05f, 0.95f, 0.85f, 0.92f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f } };             // Increased (off)
static const float kInsaneSeconds = 25.0f;

inline void InsaneSettings(TpSettings &s)
{
    s.seconds = kInsaneSeconds;
    s.base[0] = s.base[1] = s.base[2] = 1.0f;
    s.vanillaBalance = true;
    s.balanceStrength = 2.0f;
    s.asphaltFloor = 0.0f;
    s.wornGrip = kWornGripDefault;
    s.increased = false;
    s.tireDamage = true;
    s.rollingRadius = false;
    s.standStill = true;
    s.temperature = true;
    memcpy(s.mode, kInsaneModes, sizeof s.mode);
}

// The preset in force, if any: insane mode over hard mode over the player's own settings.
inline void PresetSettings(TpSettings &s)
{
    if (s.insaneMode) InsaneSettings(s);
    else if (s.hardMode) HardSettings(s);
}

void SettingsGet(TpSettings &out);        // the settings in force (any thread)
void SettingsPut(const TpSettings &in);   // edited settings: the mod applies them at once and saves the ini
void SettingsReload();                    // read the ini again (the page's button)

// What the settings page shows about the truck being driven.
struct TpLive
{
    int wheels;                  // 0 = no truck driven
    char truck[96];              // its kind's name ("western_star_6900xd"), "" = not read
    float ground, asphalt, mud;  // the first wheel's own grip (from its tire file)
    float balance;               // the vanilla balance factor on that wheel (1 = untouched)
    int mode;                    // PressureMode
    int surface;                 // the ground under the first wheel (kSurface...)
    float speed;                 // km/h
    int damage, capacity;        // the first wheel's tire damage; capacity 0 = not found
    float room;                  // metres the game can draw the first wheel's tire flatter (the modes' flattening fits in it); below 0 = not known
    float sink;                  // metres its contact sits inside the cylinder (a soft tire), already taken off room
    float outsideC;              // the outside temperature in force
    int outsideFrom;             // 0 = AmbientC, 1 = another mod's block (outside.h)
    float pressure;              // the tires' pressure as a share of Normal's, where it stands between the modes
    float pressureAir;           // the same as the outside temperature leaves it (what the grip goes by)
};
enum { kSurfaceDirt, kSurfaceGravel, kSurfaceSand, kSurfaceRock, kSurfacePaved };
void LiveGet(TpLive &out);

// Gamepad. The game loads XInputGetState itself and keeps it in more than one pointer; PadInit points every copy at a
// filter that hides the panel's buttons from the game: the open binding whenever it is held, every panel button while
// the panel is open, and any of them still held when it closes, until released. Called again it catches copies made since. Without the filter (no
// pointer found) the pad still works the panel, but the game sees the presses too.
int PadInit(uint64_t exeBase);            // pointers newly sent through the filter
void PadMasks(uint32_t open, uint32_t lower, uint32_t raise, uint32_t confirm, uint32_t cancel); // the bindings
void PadLive(bool live);                  // the filter only takes buttons from the game while live (a truck is driven)
void PadCurves(float throttle, float brake); // the triggers' curves, 1 = as they are: the filter maps each trigger while live
uint8_t PadCurved(uint8_t value, int trigger); // a trigger's value through its curve: 0 the right trigger, 1 the left
uint32_t PadButtons();                    // the buttons of all connected pads together, 0 without a pad

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
