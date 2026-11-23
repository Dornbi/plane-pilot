# Gameplay Improvement Ideas (`ideas.md`)

Backlog of concrete gameplay ideas that fit the current engine and RAM budgets (~1.1 KB allocatable free in `fpilot.prg`, ~13.5 KB in `ppilot.prg`).

---

## Combat feedback & readability (`fpilot`)

1. **Enemy hit flash & audio (~50–80 B)**
   - Currently `opponent_strike()` in `c64o/opponent.cc` spawns black smoke, which is hard to spot at 600–1,000 m.
   - Flash the struck opponent sprite white or yellow for 1–2 frames (~20 B, zero extra sprites) and add a crisp hit tick and ground-impact explosion on SID Voice 3 via the `kSoundV3*` tables.

2. **3D target callout: altitude & range (~120–180 B, zero steady-state cycles)**
   - `NAV 1` (`TARGET`) shows horizontal bearing via the nav lamps, but in 3D space there is no cue for whether an off-screen target is higher/lower or 500 m vs 4 km away.
   - Pressing `N` (or a dedicated callout key) could briefly show a row-13 notice such as `TARGET HIGH 2KM` or `TARGET LEVEL NEAR`, and `M` map view could mark active opponents distinctly from navpoint `1`.

3. **Dedicated gunsight bitmap for `fpilot` (0 runtime bytes, 0 cycles)**
   - Sprite block 80 (`$D400`) holds the front-view orientation mark. Generating a subtle crosshair/reticle bitmap for `fpilot` in `tools/generate_sprites.py` makes deflection shooting clearer at zero runtime cost.

4. **"Bandit on your six" & quick-peek views (~60–100 B)**
   - Checking behind you in Missions 05 and 10 currently requires toggling `4` then `2`.
   - Either add a brief HUD warning when a hunting fighter enters the firing cone behind the player, or support momentary look-around / quick-return to forward view.

5. **Combat pacing tweaks (~60–100 B)**
   - **Wave notice:** Show `NEW WAVE INBOUND` when waves 2–3 spawn in Missions 04, 05, 06, and 08.
   - **Short-burst discipline:** Optional light muzzle drift or burst heat on sustained `Space`/Fire holds to reward aimed short bursts.

---

## Flight & missions (`ppilot`)

6. **Activate crosswind (`kMissionWindX` / `kMissionWindY`, ~100–150 B)**
   - `kMissionWindX` and `kMissionWindY` already exist in `c64o/mission.h` and `c64o/pmission.cc` as zero placeholders.
   - Applying a small per-step drift in `flight.cc` (for `ppilot`, which has ~13.5 KB free) adds crosswind landings and navigation drift to missions such as *Ferry Flight*, *Airshow*, and *Fuel Challenge*.
