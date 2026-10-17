// settings_page.h: the Tire Inflation System page in ReShade's overlay, every TirePressure.ini setting edited in the game.
// Shared by the mod (panel.cpp, Dear ImGui through ReShade's function table) and the offline preview
// (test\panel_preview.cpp). Include after imgui.h. The page works on a copy of the settings in force and reports
// what to do with it; ReShade hands the overlay's keyboard to Dear ImGui, so a new panel key is read from there.
#pragma once
#include "panel.h"
#include "vanilla_balance.h"

struct PageState
{
    int capture = -1;             // -1 none, 0 the panel key, 1..5 a pad binding (kPadKeys[capture - 1])
    uint32_t peak = 0;            // the pad buttons held together so far while capturing
    bool armed = false;           // every pad button was up since the capture began: the press that began it is not read
    unsigned long long began = 0; // GetTickCount64 when the capture began (it gives up after kPageCaptureMs)
    unsigned long long last = 0;  // GetTickCount64 of the last frame the page was drawn
};
static const unsigned long long kPageCaptureMs = 8000;
enum { kPageChanged = 1, kPageReload = 2 };

// The Dear ImGui key as a virtual key, for the keys the panel key can be; 0 for others.
static int PageVirtualKey(int k)
{
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F24) return VK_F1 + (k - ImGuiKey_F1);
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return 'A' + (k - ImGuiKey_A);
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return '0' + (k - ImGuiKey_0);
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return VK_NUMPAD0 + (k - ImGuiKey_Keypad0);
    switch (k)
    {
    case ImGuiKey_Insert: return VK_INSERT;
    case ImGuiKey_Delete: return VK_DELETE;
    case ImGuiKey_Home: return VK_HOME;
    case ImGuiKey_End: return VK_END;
    case ImGuiKey_PageUp: return VK_PRIOR;
    case ImGuiKey_PageDown: return VK_NEXT;
    case ImGuiKey_Pause: return VK_PAUSE;
    case ImGuiKey_ScrollLock: return VK_SCROLL;
    default: return 0;
    }
}

static void PageTip(const char *text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

// A label in the left column, the widget after it.
static void PageLabel(const char *text, float x)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(x);
}

// One frame of a pad binding being set. padNow: the pad's buttons this frame; beganNow: the capture began on this very
// frame (the button that began it may have been pressed with the pad's A, which is then still down, so this frame never
// counts the pad as let go); escape: Esc was pressed. True when the capture is over: bound then holds the buttons to
// bind, 0 = keep the old binding. The press that began the capture is not read: reading starts once every button is
// up; the buttons then held together are kept when they are let go.
static bool PageCaptureStep(PageState &st, uint32_t padNow, bool beganNow, bool escape, unsigned long long now, uint32_t &bound)
{
    bound = 0;
    if (escape || (!st.peak && now - st.began > kPageCaptureMs)) return true;
    if (!st.armed) { st.armed = !beganNow && padNow == 0; return false; }
    if (padNow) { st.peak |= padNow; return false; }
    if (!st.peak) return false;
    bound = st.peak;
    return true;
}

// Draws the page on s (the settings in force) and returns kPageChanged when s was edited, kPageReload for the ini
// button. padNow: the pad's buttons this frame, for a pad binding being set.
static int SettingsPage(TpSettings &s, const TpLive &live, PageState &st, uint32_t padNow)
{
    int act = 0;
    auto changed = [&](bool c) { if (c) act |= kPageChanged; };
    const unsigned long long now = GetTickCount64();
    if (now - st.last > 1000) st.capture = -1; // the overlay was closed in between
    st.last = now;
    const float em = ImGui::GetFontSize(), labelX = em * 12.0f, w = em * 14.0f;
    char buf[64];
    static const char *modeNames[kModeCount] = { "Low", "Reduced", "Normal", "Increased" };
    // hard mode: its numbers and options are shown, greyed out, in place of the player's own, which the ini keeps
    TpSettings hard = s;
    PresetSettings(hard);
    TpSettings &e = s.hardMode || s.insaneMode ? hard : s;

    ImGui::SeparatorText("Truck");
    if (live.wheels > 0)
    {
        ImGui::Text("%d wheels, %s pressure.", live.wheels, modeNames[live.mode < 0 || live.mode >= kModeCount ? kNormal : live.mode]);
        if (live.ground > 0.0f)
        {
            ImGui::Text("Its tires: ground %.2f, asphalt %.2f, mud %.2f.", live.ground, live.asphalt, live.mud);
            const float t = VanillaBalanceFactor(live.ground, live.asphalt, live.mud);
            if (t >= 0.999f) ImGui::TextDisabled("No vanilla tire grips less in all three: vanilla balance leaves these alone.");
            else
            {
                const float k = e.vanillaBalance ? live.balance : t;
                ImGui::Text("Vanilla balance %s x%.2f: ground %.2f, asphalt %.2f, mud %.2f.", e.vanillaBalance ? "makes that" : "would make that",
                            k, live.ground * k, live.asphalt * k, live.mud * k);
            }
        }
        else ImGui::TextDisabled("Its ground grip shows once the truck has moved.");
        static const char *surfaces[5] = { "dirt", "gravel", "sand", "rock", "paved ground" };
        ImGui::Text("First wheel on %s, %.0f km/h.", surfaces[live.surface < 0 || live.surface > kSurfacePaved ? kSurfaceDirt : live.surface], live.speed);
        if (live.capacity > 0) ImGui::Text("Its tire: %d of %d damage.", live.damage, live.capacity);
        if (live.room >= 0.0f && live.sink > 0.0f)
            ImGui::Text("Its contact sinks %.1f cm into the ground; the game draws it up to %.1f cm flatter.", live.sink * 100.0f, live.room * 100.0f);
        else if (live.room >= 0.0f) ImGui::Text("The game draws it up to %.1f cm flatter.", live.room * 100.0f);
        ImGui::Text("Outside %.0f C (%s): the tires hold %.0f%% of Normal's pressure, %.0f%% once the air's temperature counts.", live.outsideC,
                    live.outsideFrom ? "another mod's, by map and hour" : "AmbientC", live.pressure * 100.0f, live.pressureAir * 100.0f);
    }
    else ImGui::TextDisabled("No truck driven.");

    ImGui::SeparatorText("Realism");
    if (ImGui::Checkbox("Hard mode", &s.hardMode)) { if (s.hardMode) s.insaneMode = false; act |= kPageChanged; }
    PageTip("The numbers and options nearest to real tires, all at once: a change takes half a minute and only while the truck "
            "stands, the tires feel the outside temperature, soft tires wear, flattened tires roll like real ones, vanilla "
            "balance is on, and the modes get gentler numbers (half the pressure is about half again the contact patch). "
            "While it is on, everything below but the outside temperature, the controls, the panel and the sounds is set by "
            "it and greyed out; TirePressure.ini keeps your own numbers for when it is off.");
    if (ImGui::Checkbox("Globalhacker insanity mode", &s.insaneMode)) { if (s.insaneMode) s.hardMode = false; act |= kPageChanged; }
    PageTip("Built from the player Globalhacker's own settings, and harder than real tires: Low grips x2 on dirt and x0.4 on asphalt, soft tires wear from 15 km/h and are worn out "
            "within seconds a little faster, a change takes 25 s and only while the truck stands, flattened tires lose the "
            "speed the smaller wheel loses, no Increased mode, and the vanilla balance at 2 puts tires that out-grip every "
            "vanilla tire below it. Greys out the same settings as hard mode.");
    const bool preset = s.hardMode || s.insaneMode;
    ImGui::BeginDisabled(preset);
    changed(ImGui::Checkbox("Pressure changes only while the truck stands still", &e.standStill));
    PageTip("A choice made while driving waits, and a change under way pauses, until the truck stands. A hint at the top left says so.");
    changed(ImGui::Checkbox("Tires feel the outside temperature", &e.temperature));
    PageTip("The air in the tires follows the outside temperature (the gas law, Normal taken as 35 psi): cold air is less "
            "pressure, so the tires run softer, flatter and with more grip on soft ground, and Low is as soft as they go; "
            "cold rubber grips less, most on asphalt and rock; warm air is a little more pressure, warm rubber a little more "
            "grip on asphalt; in the heat soft tires wear from lower speeds. The modes' numbers are for 15 C.");
    ImGui::EndDisabled();
    PageLabel("Outside temperature", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderFloat("##outside", &s.ambientC, -40.0f, 50.0f, "%.0f C", ImGuiSliderFlags_AlwaysClamp));
    PageTip("Where no other mod publishes the outside temperature. A gauge mod by the same author gives it by map and hour "
            "while it runs, and this slider is then not in force.");

    ImGui::SeparatorText("Grip");
    ImGui::BeginDisabled(preset);
    changed(ImGui::Checkbox("Vanilla balance", &e.vanillaBalance));
    PageTip("Tires that grip better than every vanilla tire on ground, asphalt and mud at once come down to the best "
            "vanilla tire of their kind. Vanilla tires stay as they are.");
    ImGui::BeginDisabled(!e.vanillaBalance);
    PageLabel("Balance strength", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderFloat("##strength", &e.balanceStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp));
    PageTip("1 = all the way to the vanilla tire, 0.5 = half way. Above 1 a tire that out-grips every vanilla tire ends below "
            "them: at 2 its ratio to the vanilla tire counts twice, so three times the vanilla grip ends at a third of it. "
            "Vanilla tires stay as they are at any strength.");
    ImGui::EndDisabled();
    static const char *baseNames[3] = { "Ground grip", "Asphalt grip", "Mud grip" };
    for (int i = 0; i < 3; i++)
    {
        PageLabel(baseNames[i], labelX);
        ImGui::SetNextItemWidth(w);
        ImGui::PushID(i);
        changed(ImGui::SliderFloat("##base", &e.base[i], 0.3f, 2.0f, "x%.2f", ImGuiSliderFlags_AlwaysClamp));
        ImGui::PopID();
        PageTip("Every mode, Normal too, on top of the vanilla balance.");
    }
    PageLabel("Asphalt floor", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderFloat("##floor", &e.asphaltFloor, 0.0f, 5.0f, e.asphaltFloor > 0.0f ? "%.2f" : "off", ImGuiSliderFlags_AlwaysClamp));
    PageTip("Every tire grips at least this much on paved ground, as if its tire file said so; the modes act on top. "
            "Vanilla off-road and mud tires have 0.4 to 1.2, highway tires about 3.");

    ImGui::SeparatorText("Pressure modes, against Normal");
    changed(ImGui::Checkbox("Increased pressure mode", &e.increased));
    PageTip("A road mode above Normal: more grip on asphalt, less on dirt and in mud, less fuel, quicker steering.");
    // the columns in pressure order; TpSettings::mode holds Reduced, Low, Increased
    static const struct { const char *name; int index; } columns[3] = { { "Low", 1 }, { "Reduced", 0 }, { "Increased", 2 } };
    if (ImGui::BeginTable("modes", 4, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, labelX - ImGui::GetStyle().CellPadding.x * 2.0f);
        for (const auto &c : columns) ImGui::TableSetupColumn(c.name, ImGuiTableColumnFlags_WidthFixed, w * 0.5f);
        ImGui::TableHeadersRow();
        // key: the TpSettings::mode index of the row (kMode...)
        static const struct { int key; const char *label, *format, *tip; float speed; } rows[9] = {
            { kModeGround, "Dirt grip", "x%.2f", "OnModelFriction: grip on plain ground: dirt, grass, and rocks and logs lying on it.", 0.01f },
            { kModeGravel, "Gravel grip", "x%.2f", "GravelFriction: grip where the ground is gravel.", 0.01f },
            { kModeSand, "Sand grip", "x%.2f", "SandFriction: grip where the ground is sand.", 0.01f },
            { kModeRock, "Rock grip", "x%.2f", "RockFriction: grip where the ground is rock or stone.", 0.01f },
            { kModeAsphalt, "Asphalt grip", "x%.2f", "BodyFrictionAsphalt: grip on paved ground (asphalt roads, concrete, pavers, bridges).", 0.01f },
            { kModeMud, "Mud grip", "x%.2f", "SubstanceFriction: grip in mud.", 0.01f },
            { kModeOffset, "Flattening", "%.3f m", "AdditionalRadiusOffset: metres off the tire's radius. The game only draws a tire so flat (see the "
              "truck's line above): on a tire with less room than the deepest mode asks for, every mode shrinks alike. A flatter tire also rolls "
              "slower in every gear.", 0.001f },
            { kModeFuel, "Fuel use", "x%.2f", "FuelConsumptionModifier: counts while the truck moves.", 0.01f },
            { kModeSteering, "Steering speed", "x%.2f", "SteeringSpeedModifier.", 0.01f } };
        for (int r = 0; r < 9; r++)
        {
            const int k = rows[r].key;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(rows[r].label);
            PageTip(rows[r].tip);
            for (int c = 0; c < 3; c++)
            {
                const int m = columns[c].index;
                ImGui::TableNextColumn();
                ImGui::PushID(r * 3 + c);
                ImGui::BeginDisabled(m == 2 && !e.increased);
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed(ImGui::DragFloat("##v", &e.mode[m][k], rows[r].speed, kModeRange[k].lo, kModeRange[k].hi, rows[r].format, ImGuiSliderFlags_AlwaysClamp));
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    changed(ImGui::Checkbox("Flattened tires roll like real ones", &e.rollingRadius));
    PageTip("A real tire's belt keeps its length, so a flattened tire rolls nearly as far per turn as before. In the game the "
            "wheel itself shrinks, which takes about three times as much speed off the truck. With this on the gears make up "
            "the difference.");

    ImGui::SeparatorText("Tire damage at speed");
    changed(ImGui::Checkbox("Soft tires wear when driven fast", &e.tireDamage));
    PageTip("Faster than the first speed, every wheel on the ground takes damage each time the interval passes, more "
            "the faster, up to the second speed. A worn out tire goes flat and has no grip until it is repaired. A wheel takes about 50.");
    ImGui::BeginDisabled(!e.tireDamage);
    if (ImGui::BeginTable("wear", 3, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, labelX - ImGui::GetStyle().CellPadding.x * 2.0f);
        for (int c = 0; c < 2; c++) ImGui::TableSetupColumn(columns[c].name, ImGuiTableColumnFlags_WidthFixed, w * 0.5f);
        ImGui::TableHeadersRow();
        // the speeds are kept in metres per second, as Expeditions' tire files have them, and shown in km/h
        static const struct { int key; const char *label, *format, *tip; float scale, speed; } rows[5] = {
            { kModeMinVel, "Wear from", "%.0f km/h", "MinVel: above this speed the tires wear.", 3.6f, 0.5f },
            { kModeMaxVel, "Most wear at", "%.0f km/h", "MaxVel: from this speed on they take the most damage.", 3.6f, 0.5f },
            { kModeMinDamage, "Least damage", "%.0f", "MinDamage: what a wheel takes at the first speed.", 1.0f, 0.05f },
            { kModeMaxDamage, "Most damage", "%.0f", "MaxDamage: what a wheel takes at the second speed. 0 = no wear in this mode.", 1.0f, 0.05f },
            { kModeDamageTick, "Every", "%.0f s", "DamageTick: the time above the first speed before the first damage, and between damages.", 1.0f, 0.05f } };
        for (int r = 0; r < 5; r++)
        {
            const int k = rows[r].key;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(rows[r].label);
            PageTip(rows[r].tip);
            for (int c = 0; c < 2; c++)
            {
                const int m = columns[c].index;
                float shown = e.mode[m][k] * rows[r].scale;
                ImGui::TableNextColumn();
                ImGui::PushID(100 + r * 2 + c);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##w", &shown, rows[r].speed, kModeRange[k].lo * rows[r].scale, kModeRange[k].hi * rows[r].scale, rows[r].format, ImGuiSliderFlags_AlwaysClamp))
                {
                    // whole numbers for the damages and the interval, as the game counts damage in whole points
                    e.mode[m][k] = rows[r].scale == 1.0f ? floorf(shown + 0.5f) : shown / rows[r].scale;
                    act |= kPageChanged;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled(); // hard mode

    ImGui::SeparatorText("Controls");
    PageLabel("Panel key", labelX);
    KeyName(s.key, buf, sizeof buf);
    if (ImGui::Button(st.capture == 0 ? "Press a key (Esc: keep)###key" : (strcat_s(buf, "###key"), buf), ImVec2(w, 0)))
        st.capture = st.capture == 0 ? -1 : 0;
    PageTip("Opens the panel and steps the pressure down.");
    if (st.capture == 0)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) st.capture = -1;
        else
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++)
            {
                const int vk = PageVirtualKey(k);
                if (vk && ImGui::IsKeyPressed((ImGuiKey)k, false)) { s.key = vk; st.capture = -1; act |= kPageChanged; break; }
            }
    }
    changed(ImGui::Checkbox("Show the panel", &s.ui));
    PageTip("Off: the key changes the pressure at once, without the panel.");
    ImGui::BeginDisabled(!s.ui);
    PageLabel("Panel size", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderFloat("##size", &s.uiScale, 0.5f, 2.0f, "x%.2f", ImGuiSliderFlags_AlwaysClamp));
    PageLabel("Auto confirm", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderFloat("##confirm", &s.confirmSeconds, 0.0f, 30.0f, s.confirmSeconds > 0.0f ? "%.0f s" : "never", ImGuiSliderFlags_AlwaysClamp));
    PageTip("Without input for this long the panel's choice applies itself. Never: confirm with the pad.");
    ImGui::EndDisabled();
    PageLabel("Change takes", labelX);
    ImGui::SetNextItemWidth(w);
    ImGui::BeginDisabled(preset);
    changed(ImGui::SliderFloat("##seconds", &e.seconds, 0.0f, 30.0f, "%.1f s", ImGuiSliderFlags_AlwaysClamp));
    PageTip("How long the tires take from Normal to Low; a smaller step takes its share.");
    ImGui::EndDisabled();
    PageLabel("Sound volume", labelX);
    ImGui::SetNextItemWidth(w);
    changed(ImGui::SliderFloat("##sound", &s.soundVolume, 0.0f, 100.0f, s.soundVolume > 0.0f ? "%.0f %%" : "off", ImGuiSliderFlags_AlwaysClamp));
    PageTip("The air going out and in, and the compressor filling its tank.");
    changed(ImGui::Checkbox("Beep on a change", &s.beep));

    ImGui::SeparatorText("Pad");
    static const char *padLabels[5] = { "Open the panel", "Lower the pressure", "Raise the pressure", "Confirm", "Close unchanged" };
    bool beganNow = false;
    for (int i = 0; i < 5; i++)
    {
        ImGui::PushID(i);
        PageLabel(padLabels[i], labelX);
        sprintf_s(buf, "%s###bind", s.pad[i]);
        if (ImGui::Button(buf, ImVec2(w, 0)) && st.capture < 1)
        {
            st.capture = i + 1;
            st.peak = 0;
            st.armed = false;
            st.began = now;
            beganNow = true;
        }
        PageTip("Press it, then hold the button or buttons on the pad and let go.");
        ImGui::SameLine();
        if (ImGui::SmallButton("None###clear"))
        {
            strcpy_s(s.pad[i], "None");
            act |= kPageChanged;
        }
        ImGui::PopID();
    }
    if (st.capture >= 1)
    {
        // The buttons are read in a window of its own: ReShade's overlay can be worked with the pad, and on the page
        // the buttons being held would walk the widgets and press them (PageCaptureStep has the rest). B closes a
        // popup, so it is opened again each frame it is found closed.
        uint32_t bound = 0;
        const bool done = PageCaptureStep(st, padNow, beganNow, ImGui::IsKeyPressed(ImGuiKey_Escape, false), now, bound);
        if (bound)
        {
            PadName(bound, s.pad[st.capture - 1], sizeof s.pad[0]);
            act |= kPageChanged;
        }
        if (!ImGui::IsPopupOpen("###padbind", 0)) ImGui::OpenPopup("###padbind", 0);
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Pad buttons###padbind", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::Text("%s", padLabels[st.capture - 1]);
            if (!st.armed) ImGui::TextUnformatted("Let go of the pad.");
            else if (!st.peak) ImGui::TextUnformatted("Hold the button or buttons on the pad, then let go.");
            else
            {
                PadName(st.peak, buf, sizeof s.pad[0]);
                ImGui::Text("%s", buf);
            }
            if (!st.peak)
            {
                const long long left = (long long)kPageCaptureMs - (long long)(now - st.began);
                ImGui::TextDisabled("Keeps the old one in %lld s, or with Esc.", left > 0 ? (left + 999) / 1000 : 0);
            }
            if (done) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (done)
        {
            st.capture = -1;
            st.peak = 0;
        }
    }
    InterlockedExchange(&g_pageCapturing, st.capture >= 1 ? (LONG)GetTickCount() : 0);

    ImGui::Separator();
    if (ImGui::Button("Defaults"))
    {
        SettingsDefaults(s);
        act |= kPageChanged;
    }
    ImGui::SameLine();
    if (ImGui::Button("Read the ini again")) act |= kPageReload;
    ImGui::TextDisabled("Changes count at once and go into TirePressure.ini. Version " TP_VERSION ".");
    return act;
}
