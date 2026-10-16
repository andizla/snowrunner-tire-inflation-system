# Changelog

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
