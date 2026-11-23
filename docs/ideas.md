# Gameplay Improvement Ideas (`ideas.md`)

Backlog of concrete gameplay ideas that fit the current engine and RAM budgets (~1.1 KB allocatable free in `fpilot.prg`, ~13.5 KB in `ppilot.prg`).

---

## Combat feedback & readability (`fpilot`)

1. **Dedicated gunsight bitmap for `fpilot` (0 runtime bytes, 0 cycles)**
   - Sprite block 80 (`$D400`) holds the front-view orientation mark. Generating a subtle crosshair/reticle bitmap for `fpilot` in `tools/generate_sprites.py` makes deflection shooting clearer at zero runtime cost.

2. **Quick-peek views (~60–100 B)**
   - Checking behind you in Missions 05 and 10 currently requires toggling `4` then `2`; supporting momentary look-around or a quick-return to forward view would make defensive flying smoother.

3. **Combat pacing tweaks (~60–100 B)**
   - **Wave notice:** Show `NEW WAVE INBOUND` when waves 2–3 spawn in Missions 04, 05, 06, and 08.
   - **Short-burst discipline:** Optional light muzzle drift or burst heat on sustained `Space`/Fire holds to reward aimed short bursts.

---

## Flight & missions (`ppilot`)

4. **Activate crosswind (`kMissionWindX` / `kMissionWindY`, ~100–150 B)**
   - `kMissionWindX` and `kMissionWindY` already exist in `c64o/mission.h` and `c64o/pmission.cc` as zero placeholders.
   - Applying a small per-step drift in `flight.cc` (for `ppilot`, which has ~13.5 KB free) adds crosswind landings and navigation drift to missions such as *Ferry Flight*, *Airshow*, and *Fuel Challenge*.
