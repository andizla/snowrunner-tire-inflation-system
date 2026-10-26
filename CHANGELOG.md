# Changelog

## [Unreleased]

- The overlay copies a swap chain's method table that another program has put at the start of its memory. Before, it drew nothing there (no panel and no NEXT GEN menu on two players' PCs). When a table still cannot be copied, `TirePressure.log` says in whose code the table and its Present lie, so the program can be named.
- LB + d-pad left or right works only while the panel is open; the two chords no longer open the panel a step lower or higher on their own, so a chord hit while driving changes nothing.
- A worn out or broken tire keeps a slight grip, `WornGrip` percent of its own, 10 by default, so the truck still moves; 0 gives the old none (the testers' ask).
- The pad bindings rest while the NEXT GEN menu is open, as the panel key does.

## [1.3.1] 2026-10-25

- Each truck keeps its own pressure mode, `PerTruck` in the ini and a checkbox in the tab's Realism section (on by default). The mode is saved by the truck kind's name in `TirePressure.trucks.ini` next to the exe and comes back the moment that truck is driven again, without a sound, the compressor or a wait, across map loads and game starts. A truck not driven since 1.3.1 starts at Normal, and two trucks of one kind share it. `PerTruck=0` keeps one mode for every truck, Normal at each game start, as before. The tab's truck section names the truck's kind. Asked for by a player on mod.io.

## [1.3.0] 2026-10-24

- Pad throttle and brake curves, `PadThrottleCurve` and `PadBrakeCurve` in the ini and the tab's Controls section (1 to 3, 2 by default): while a truck is driven each trigger's share is raised to that power before the game reads it, so a light pull gives less and the full pull stays full; at 2 half the trigger gives a quarter, at 1 the trigger is as it is. A tenth of the trigger was a lot of throttle for the game's trucks.
- The panel and the settings are drawn by the mod's own overlay, Dear ImGui compiled in, on the game's DirectX 11 device, no longer through ReShade's overlay; Next Gen Gameplay draws with the same code. The zip brings ReShade 6.8.0 without its start banner and with a `ReShade.ini` that loads no effects and keeps Home for ReShade's own window, and the mod takes the game's swap chains from it, drawing after ReShade and its add-ons. Without ReShade the mod draws on its own. The game can then crash with the Steam, Discord or RivaTuner overlay or with multiple monitors.
- When ReShade is missing, takes no add-ons, or another program's `dxgi.dll` is in its place, the mod says so at the start: in a line at the top for 15 seconds and in the NEXT GEN menu, with what to do, and in its log.
- The NEXT GEN menu opens on F2. A `NextGen.ini` from a test build that still holds that build's default key (Delete or Page Down) moves to F2 once; a fresh `NextGen.ini` is made so that a Delete or Page Down set in it by hand stays. The panel's key does nothing while the menu is open.
- With Next Gen Graphics installed the NEXT GEN menu has a Graphics tab with its switches and status lines. Its F keys are off while the tab is there.
- The settings are in the Tire Inflation tab of the NEXT GEN menu, which F2 opens and closes; with Next Gen Gameplay installed its Gameplay tab is in the same window. The key is set in `NextGen.ini` next to the exe, or in the menu: click the key's name at its top right, then press the new key. While the menu is open the game gets no mouse and no keys.
- Works alongside other mods that draw over the game. A minimap mod with a ReShade add-on mode of its own needs that mode on next to ReShade: its DirectX probe at start otherwise ends the game while ReShade sets up its hooks.
- Should one Next Gen mod's overlay fail, its tab leaves the NEXT GEN menu and the other mod's tab shows; with no tab left the menu closes, so the game never loses its mouse and keys to a menu nobody draws. The log says where it failed.
- The loader, `version.dll`, loads the mods into the game only. SnowRunner's crash reporter and its other programs in `Bin` load it too: they get no `.asi` file, and `AsiLoader.log` keeps the game's lines, with one line added when the crash reporter starts.
- Works with the Steam overlay of Steam's client from 21 October 2026. Without ReShade it ended the game at its first frame on a tester's PC: Steam hooked both DirectX's Present and the mod's, and the two called each other until the game ran out of stack. The mod now hooks only the swap chains the game makes, each through a method table of its own, and leaves DirectX's as they are.
- Two Next Gen mods that start in the same moment take turns at the game's import table, so neither finds it locked again under its own write, which would have ended the game at start.
- A Next Gen mod or tester build from before 23 October next to this one is named in the log and keeps a menu of its own: its hook in DirectX's table can bring back the crash with the Steam overlay, so every Next Gen mod should be the same release.
- The mod is now called Next Gen Tire Inflation; its files keep the name `TirePressure`, and `TirePressure.ini` keeps working.
- A tire that comes soft no longer goes far too flat. The game draws a tire by where it touches the ground, and a soft tire's contact sinks into the ground under the truck's weight, so at Low its tread went under the ground (a tester's modded truck). The mod measures how far each wheel's contact sits inside its round shape, smoothed over a couple of seconds and in 5 mm steps, and takes that off the flattening the tire has room for. The wheel lines in the log and the tab's truck section show it. A vanilla tire sinks next to nothing and is flattened as before.
- In the cold Low flattens no further than the tire has room for. Below 15 C its flattening is carried past 10 cm, up to 12, and was shared out for the 10 cm, so it went up to a fifth past what the game draws flat.
- The gear speeds make up a flattened tire's speed in every gear. The game turns a gear's number a into a top wheel speed of 2a + 5 (a + 2 in the high gear); the mod scaled a alone, which fell short in the low gears (at the scout's Low a gear of number 5 rose x1.078 for x1.117).
- The vanilla balance's strength goes up to 2 (1.2: up to 1). Above 1 a tire that out-grips every vanilla tire ends below the vanilla tire: at 2 its ratio to the vanilla tire counts twice, so three times the vanilla grip ends at a third of it. Vanilla tires stay as they are at any strength. Asked for by a player with modded trucks that should be harder to drive than vanilla ones.
- Globalhacker insanity mode, `InsaneMode`: a hardcore set harder than real tires, built from the own settings of the player Globalhacker, who asked for the stronger balance above. Low grips x2 on dirt and x0.4 on asphalt, soft tires wear from 15 km/h in Low and 20 km/h in Reduced and are worn out within seconds a little faster, a change takes 25 seconds and only while the truck stands, flattened tires lose the speed the smaller wheel loses, no Increased mode, and the vanilla balance at 2. A checkbox under hard mode; the two exclude each other, and the same settings are greyed out.

## [1.2.1]

- The global that leads to the driven truck, which the mod cannot do without, is looked for three ways: by the code that uses it as before, by the same code with the stack frame left open, and by the small function that hands out the object it sits in. On the Game Pass build of the game (a player's log of 2026-10-15) the first way found nothing and the mod stood down; the other two are for that build. Every way has to find one place.
- `DumpImage=1` in the ini writes the game's code as the mod sees it into `TirePressure_image_<stamp>.bin` next to the exe at the next start, for a report when the mod stands down on a build it does not know. The ini's guide says so.

## [1.2.0] 2026-10-15

- Pressure changes only while the truck stands still, `StandStill`, on by default: a choice made while driving waits, a change under way pauses, and a hint at the top left says so.
- The tires feel the outside temperature, `Temperature`, on by default. The air in them follows the gas law (Normal taken as 35 psi: 83% of its pressure at 20 below, 110% at 35 above), so cold tires run softer, flatter and with more grip on soft ground; cold rubber grips less, most on asphalt and rock; in the heat soft tires wear from lower speeds. `AmbientC` sets the temperature (15 C); a gauge mod by the same author hands it over by map and hour while it runs. The tab shows the temperature in force and the pressure the tires hold.
- Hard mode, `HardMode`: the numbers and options nearest to real tires, all at once, with every other gameplay setting greyed out in the tab while it is on. `TirePressure.ini` keeps your own numbers.
- The pressure moves between the modes on one scale (Low 45%, Reduced 65%, Normal 100%, Increased 125% of Normal's) at one pace: Normal to Low in `Seconds`, Normal to Reduced in about two thirds of that (1.1: four fifths), a step to Increased in a little under half.
- A factor edited in the tab counts at once (1.1: the tires walked to it over `Seconds`).
- The mod publishes its state for other mods in a block of shared memory (the mode, the pressure and its target, the outside temperature in force, the most worn tire, flags), 64 bytes; `src\tire_state.h` has the layout. A gauge cluster can show the pressure level from it.
- A worn out tire has no grip until it is repaired: 0 on the ground, on asphalt and in mud, and it is drawn flat, as flat as the game draws a tire. Before, a flat tire kept its pressure mode's grip and stayed round at its smaller size.
- The mod runs on a build of the game where it finds everything but the game's damage update or the truck update's call into Windows. Tire damage is then dealt from the mod's own thread, and the log says so. Before, the mod stood down on such a build. The six checks of the game's objects count when found anywhere, not only once.

## [1.1.0] 2026-10-07

- Sounds. Deflating plays the air being let out, which gets weaker as the tires empty. Filling plays the air going in. Both are recordings of a real tire.
- A compressor with an air tank. The fillings draw from the tank, and the compressor fills it again at every second to fourth filling. It runs for about 17 seconds and lets its own air go as it stops. The compressor is made by the mod's code. The air it lets go is one of SnowRunner's own sounds, read out of the game's sound file while the game runs.
- Sound volume in the settings tab and as `SoundVolume` in the ini. 0 turns the sounds off.
- `AirOutSound`, `AirInSound`, `CompressorSound` and `CompressorStopSound` in the ini put a WAV file, or another of the game's samples, in place of one of the four sounds.
- A choice on the panel confirms itself after 5 seconds (1.0: 15). That wait is how the keyboard confirms: F3 opens the panel and steps the pressure down, then leave it alone. A `TirePressure.ini` from 1.0 that still says `ConfirmSeconds=15` is set to 5 once. Any other number in it stays.
- The beeps are off unless you switch them on (`Beep=1`, or the box in the settings tab). A `TirePressure.ini` from 1.0 has them switched off once.
- A pressure change takes up to 6 seconds (1.0: 3). A `TirePressure.ini` from 1.0 that still says `Seconds=3` is set to 6 once. Any other number in it stays.

## [1.0.1] 2026-10-05

- Tire damage starts at higher speeds. Low: wear above 20 km/h, the most from 30 km/h (1.0.0: 15 and 25). Reduced: wear above 35 km/h, the most from 45 km/h (1.0.0: 25 and 35).
- A `TirePressure.ini` that 1.0.0 wrote keeps its own numbers. To get the new ones, set them in the settings tab or delete the file.
- The zip brings ReShade 6.8.0 with full add-on support in a `ReShade` folder, for players who have none yet.
- The mod no longer needs one exact build of the game's exe. At its start it finds its places in the game's code and checks how the game's objects are laid out, and it stands down when anything is missing. That is meant for the Epic Games Store version, which has not been tested, and for game updates that leave that code alone.

## [1.0.0] 2026-10-04

- Four tire pressure modes for the truck being driven: Low, Reduced, Normal and Increased. A mode scales the grip by the ground under each wheel (dirt, gravel, sand, rock, asphalt, mud), the tire's radius, fuel use while moving and steering speed.
- A tire flattens as far as the game can draw it, and the truck then rolls as much slower as a real flattened tire would.
- Soft tires driven too fast take damage, with a warning on screen that shows how worn they are. A worn out tire goes flat. It is the game's own wheel damage and is repaired like any other.
- Expeditions' Tire Inflation System panel, drawn in the game's font through ReShade. LB + d-pad on a pad, F3 on the keyboard.
- A Tire Inflation System tab in ReShade's overlay edits every setting while the game runs and saves it to `TirePressure.ini`.
- Vanilla balance, base grip and an asphalt floor for tires that grip too much or too little as they come.
- `version.dll`, a loader for `.asi` files in the game folder.
