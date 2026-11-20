# Apsis logistics sandbox v2 — layers 1 to 4a

A clean C++23 rebuild of the sandbox from the approved spec (phase 1 doc). It compiles to WebAssembly for the browser explorer. v0.20 stays frozen as the reference.

- **Layer 1:** the core (hash RNG, integer maths, ledgers, checkpoints, input channel, intents and clearing, the tick). See gates_l1.json.
- **Layer 2:** the world (map, economy, factions) with no player, plus faction decisions as intents. See gates_l2.json.
- **Layer 3:** one pilot: ships by mass and space, jumps in J, fuel, rescue and debt, refits, jobs, the opening, three bot styles. See pilot_gates_l3.json, pilot_switches_l3.txt, agg_l3.txt.
- **Layer 4a:** the three end states (death, debt; prison deferred), law and logs, courier and spy work with rendezvous, pirates against pilots, and six law styles. See pilot_endstates_l4.txt, pilot_law_l4.txt, pilot_switches_l4.txt, agg_l4.txt, abl_l4.json.

## Build and run

```
cmake -S . -B build && cmake --build build -j   # native (g++ 13 or clang 18, C++23)
pip install ziglang && ./build_wasm.sh            # WebAssembly (wasm32-wasi, single thread)
./gates.sh                                        # layer 1, native and WebAssembly
V020=/path/to/v0.20 ./gates_l2.sh                 # layer 2 (the port gate needs the frozen v0.20 files and Node)
./build/pilot_gates gates                         # layer 3: replay, resume, round trip, input order
./build/pilot_gates switches                      # layers 3-4: each rule switched off (ledgers exact; weapons must change outcomes)
./build/pilot_gates endstates                     # layer 4: death, bankruptcy, repay, life-support refit
./build/pilot_gates law                           # layer 4: ways out, logs, odd departures, exile and breach, grave
./build/pilot_exp 40 && python3 tools/agg3.py     # layer 3: tank sweep, rescue, debt, opening, range (agg_l3.txt)
python3 tools/agg4.py                             # layer 4a: per style at the stock tank: law, logs, recovery, pirates (agg_l4.txt)
./build/pilot_exp 40 300 && python3 tools/aggwide.py   # wide sample: 300 random seeds (rescue debt, end states)
./build/world_wide                                # 1,000-seed war, Core and supply (world_wide_l2.txt)
./build/pilot_bench                               # per pilot-year speed
./build/params_md                                 # every rule switch and placeholder (the tables below)
```

`pilot_exp` takes `APSIS_SET=name=value,...` to change any placeholder and `APSIS_OFF=rule,...` to switch rules off (the E4 ablations: `tools/abl4.py` compares them), plus the older `APSIS_DRIVE`, `APSIS_MSPACE` and `APSIS_MMASS`.

**Toolchain.** Nothing in src/ is tied to a toolchain. Threads are switched off by a plain macro (`APSIS_NO_THREADS`), so moving from zig to Emscripten is a build change only:

```
em++ -std=c++23 -O2 -DAPSIS_NO_THREADS ...
```

Drop the macro and add `-pthread` for threaded WebAssembly. The world runs single-threaded by default (`World::threads = 1`); the pool stays as a tested option (the 1/2/8-thread gates). Experiments get their speed from running branches in parallel (`pilot_exp` runs one seed per thread).

## Layer-2 follow-ups (owner notes)

All on the same 1,000 seeds (years 20–60), v0.20 = v2 with intents off (the port gate shows they're the same world). Source: world_wide_l2.txt.

1. **War gate.** v0.20 73 general wars (7.3%, 95% interval 5.8–9.1), intents on 67 (6.7%, 5.3–8.4). Ratio 0.92, intervals overlap: **PASS**, now a gate in gates_l2.sh. The strategic delay stays at 10 days.
2. **Core stability.**

   | | v0.20 | Intents on |
   | --- | --- | --- |
   | Core weakened | 26.0% | 22.8% |
   | Core home lost | 0.9% | 0.7% |
   | Core fell | 0.9% | 0.7% |
   | Core contested | 45.4% | 43.9% |
   | Stable-start Cores reaching general war | 4.6% | 4.1% |

   Intents are slightly steadier on every line, all within or near noise.
3. **Supply.** v0.20 has no relief requests, so its comparable "fed" figure is how often a supply dip recovers within 30 days.

   | | v0.20 | Intents on |
   | --- | --- | --- |
   | Dips back above the line within 30 days | 77.4% | 83.6% |
   | Station failures | 1,558 (0.0015 per station-year) | 1,037 (0.0010) |
   | Station-days in shortage (stage 2+) | 0.01% | 0.01% |
   | Relief lots fed | – | 7,998 of 9,624 asked (83%) |

   Intents raised the recovery rate and cut failures by a third, so there is no drop to explain. The earlier 374 of 1,465 was the curated 7 seeds over 300 years. The 1,000-seed window is years 20–60, so the two aren't the same measure.
4. **Month 38 on seed 110478.** Confirmed: the prospector port alone. Re-adding prospectors makes all 7 seeds diverge; at each first divergence only the prospector fields differ, plus one ghost record on 110478 in the same month. Without them, all 7 match for 300 years, and 10 of 10 random seeds from the 1,000 match v0.20 every month for 60 years (spot_l2.txt).
5. **Range vs opening:** see layer 3 below. The stock ship was adjusted, not the opening.
6. **Threads:** single-threaded by default, pool kept as a tested option (above).
7. **Contested bids:** see layer 3 below.

## Layer-3 review follow-ups (owner notes, 2026-11-20)

1. **Supply dips that don't recover** (world_wide_l3.txt, 1,000 seeds): they aren't deeper with intents on. v0.20: 4,507 slow dips (22.5% of dips), lowest stock averaging 15.2% of capacity, deepest tenth at or below 6.7%, 131 ran empty, 57 days below the line on average. Intents on: 2,925 (16.3%), 16.2%, 6.8%, 111 empty, 51 days. Recovered dips bottom out at 25.1% vs 25.2%.
2. **Rescue debt on a wide sample** (300 random seeds, 2,700 careers, `pilot_exp 40 300`). The curated seeds hid a debt trap: with B6 as written, 113 debts with p90 of 9.3 y (hauler), 3.4 y (crisis), 10.3 y (pathfinder) and a worst of 29 y. The cause was one crew leg pushed blind at 408‰ costing up to 25,000 against B6's 1,500 for a blind leg. Four fixes were measured and the owner chose to keep B6's prices with pushed crew legs priced as a blind in-range leg (`rescueRiskCapPm` 40). The owner also replaced "never stuck" with three end states (layer 4a, below). Debt is now reported, not gated.
3. **Tank 3 vs tank 6** is a balance lever, not a settled decision (see the tank sweep).
4. **The in-flight point-of-no-return warning** comes with the jump-bubble step, which the owner approved as the step right after 4a.
5. **`jumps` off and `hulls` off** stay "not built" unless a layer needs them.
6. **Weapons have teeth now:** pirates attack pilots, and turning weapons off changes 5 of 15 gate careers (pilot_switches_l4.txt). It's a gate.
7. **Contested bids** are still faction-only; re-check when pilots bid.
8. **Speed:** hauler 2.70, crisis 3.08, pathfinder 4.70 ms per pilot-year with law and pirates on, against 2.66 / 2.60 / 4.66 at layer 3. Well under the 2× line.

## Layer 2: what it is

**The world is v0.20's, ported.** Only the code paths the v0.20 world actually runs are ported:

- the Core map with hops and hazards, bodies and fair placement;
- stations and freighters, pirates and patrols;
- accounting and surveys;
- the power layer: contests, upstarts, patrons, falls and rises;
- tolls, carriers and inter-sector trade;
- system beacons;
- the dark edge.

Left out: the player, rules v0.20 had switched off, and measurement-only code. Names follow v0.20 so the two can be read side by side.

- **Port gate:** with `intents` off, v2 matches v0.20 at every month end, on 7 seeds × 300 years (3,601 months each).
- **Teeth:** changing one v0.20 number by one breaks the match within a month.
- **J (spec B1):** the map's median hop length: 12–16 map units on the curated seeds (J_l2.txt). v0.20's stock reach of 22 is therefore 1.4–1.8 J. Layer 3 measures jumps in J.

**Faction intents (spec B5, rule `intents`).** Every house decides on its own day, staggered by hash, from the world as it stands. Decisions run in parallel. Every intent lands after a delay of at least one day and clears at the end of that day:

- inside a house by priority;
- between houses, by bid for indivisible objects, ties broken by hash.

With intents on, the v0.20 direct decision paths are off. Intents decided in 7 × 300 years:

| Intent | Decided | Cleared | Delay |
| --- | --- | --- | --- |
| Edge patrol | 46,432 | 46,428 | 10 |
| Patron choice | 8,570 | 8,568 | 30 |
| Supply (per resource) | 1,465 | 374 fed | 10 |
| Fortify | 799 | 799 | 10 |
| Relight a beacon | 369 | 368 | 60 |
| Carrier contract | 300 | 300 | 30 |
| Contest | 267 | 266 | 10 |
| Expand | 54 | 54 | 10 |
| Toll rate | 46 | 45 | 30 |
| Trader buy | 10 | 10 | 30 |

Intents still waiting in the queue when the run ends account for the differences.


## Layer-2 gates (gates_l2.json)

| Gate | Result |
| --- | --- |
| Port: intents off = v0.20, state at every month end (7 seeds × 300 years) | 7 / 7 |
| Teeth: one v0.20 number off by one diverges | 1 / 1 |
| General war, intents off, reproduces v0.20 (4/21; stable Core 0/3) | pass |
| General war, 1,000 seeds: intents on / v0.20 within 0.75–1.25, or 95% intervals overlap (replaces the curated ±2 gate) | 67 vs 73, ratio 0.92, PASS |
| Stable Core never at general war, intents on | 0/3, pass |
| Ledgers exact every tick, off and on, 7 seeds × 300 years | pass |
| No intent lands with delay 0 (real world) | 0 of 58,312, pass |
| Per-resource supply: a house that can feed one feeds one (logged) | 14 cases, pass |
| Threads 1, 2, 8: identical world, intents on | 7 / 7 |
| Checkpoint at year 100, resume to 200 = uninterrupted (full state), off and on | 14 / 14 |
| Checkpoint round trip byte-exact | 14 / 14 |
| Teeth: corrupted checkpoints caught | 20 / 20 |
| Native = WebAssembly, world, every month, 7 seeds × 100 years | 1 / 1 |

## What layer 2 changed, and why (vetoable)

- **Strategic delay 10 days, not 30.** Contest, fortify, cut off and expand got their own placeholder (`delayStrategic`), because 30 days halved conflict on 200 seeds. At 10 days conflict volume is within 1–11% of v0.20. Stance and patron choice keep 30.

  | Delay | Contests | Captures | Junction-change months | General war |
  | --- | --- | --- | --- | --- |
  | v0.20 | 1,055 | 496 | 673 | 22 |
  | 30 | 594 | 222 | 369 | 15 |
  | 10 | 1,048 | 442 | 622 | 18 |
  | 1 | 1,113 | 482 | 672 | 14 |

- **Supply intents are new.** This is the per-resource decision the owner asked for:
  - A station below 30% of a supply asks its house for one relief lot (20% of its own capacity).
  - The lot comes from the house's best-stocked station, from surplus above that station's 60% reserve.
  - Requests clear by urgency, all or nothing, so a house that can feed one station feeds one. On the curated 7 seeds × 300 years, 374 of 1,465 requests were fed; the rest found no surplus. On 1,000 seeds (years 20–60), 7,998 of 9,624.
  - Logged case, seed 57206, day 51,698: house 6 fed station 12 in full, and station 11 got nothing that cycle.
  - Shortage onsets across the curated seeds: 1,022 → 898.
- **Posted-price step.** Bids on indivisible objects round down to a step of 5% of the bid (`bidStepPm`), with a floor (`bidStepMin`, default 1 = exact). In 2,100 sim-years:
  - 4 bids on contested indivisible objects;
  - none below one step;
  - rounding changed no winner.

  Worlds with exact bids are identical to rounded ones on all 7 seeds × 300 years, so rounding moved no station or faction outcome. The layer-1 test world still rounds to 250, on purpose: it forces ties so the tie-break is exercised.
- **No prospectors.** v0.20's rule set doesn't include them; the port gate caught this.
- **Explicit stances not yet modelled.** Treaty, embargo, war and peace don't exist in the v0.20 world (embargo is off). Per-relationship decisions in layer 2 are contest targets per rival and patron choice per client. Stance intents arrive with law and logs (layer 4).
- **Frontier ambiguity moved to layer 4.** It is measured from a pilot's sightings, so it needs pilots.


## Layer 3: what it is

One pilot in the layer-2 world. Every pilot input goes through the same input channel as the factions, so replay = live.

- **Ships (spec B2).** Hulls and parts have mass and space. Effective range in J = drive rating × reference mass / ship mass, and a pilot may push to 1.5× at rising risk.
- **Jumps (B1).** A ship jumps between any two systems within range. Risk per leg is base + slope × overreach, halved to a lit beacon and doubled blind, worse with wear. Days = 2 per J (×2 blind) plus gravity-well delay. A misjump spends the jump and leaves the ship where it was. Tolls are paid on arrival at a lit tollgate when carrying cargo.
- **Fuel.** The tank holds jumps. Refuelling works at any station that isn't abandoned: cash, else a fuel advance repaid from the delivery (+25%), else debt. A broke pilot is never stuck.
- **Point of no return.** A trip the fuel aboard can't cover is refused at departure; a trip to a dead system must also cover the way back. The explorer will show the refusal as the warning. There is no in-flight warning yet.
- **Rescue (B6).** A stranded ship is reached by a crew from the nearest lit, served station in a standard 1.0 J ship. Price = 2 × 300 × (1 + risk) per crew leg, there and back, plus a 500 tow. Response is 10 days, plus 30 per blind leg unless within 1 J of lit space (the distress beacon). The pilot's wallet pays first, the rest becomes debt to the rescuer's house, with 25% of income garnished.
- **Refits.** Priced by part size: up to 8 space 2,500; 9–16 5,000; 17 and up (and hulls) 7,500. A hull swap keeps the parts that fit.
- **Jobs.** Emergency (bonus ×2), blockade (×3, 6% cargo-loss risk), and a floor job to the nearest reachable station. For the first 360 days a new pilot sees only jobs within effective range to lit systems.
- **Opening.** Survey the home system's bodies (3 days, no jump, pays 150), then one dark system out and back within effective range (the chart sells for 600), then the board.
- **Bot styles:** hauler, crisis (jobs first) and pathfinder (charts and surveys). Pathfinder's expeditions and pockets come in layer 5.

### The stock ship (changed for owner item 5, vetoable)

| | Spec | Layer 3 |
| --- | --- | --- |
| Drive | 1.0 | 1.5 |
| Medium hull space | 100 | 120 |
| Medium hull mass | 50 | 40 |
| Heavy hull mass | 110 | 80 |
| Effective range, tank 3 / 4 / 6 | 1.04 J | 1.70 / 1.67 / 1.59 J |

v0.20's stock reach is 1.4–1.8 J, so the stock ship now sits mid-range. Why each change:

- **Drive 1.5.** At 1.0 too few homes had a dark system in reach.
- **Medium mass 40.** At 50, one home in eight still couldn't do the dark out-and-back.
- **Medium space 120.** The 1.5 drive and tank 6 didn't leave room for a hold refit.
- **Heavy mass 80.** At 110 a heavy swap left the ship unable to fly its routes.

I tried a 2.0 drive (1.75–1.85 J) instead of the lighter hull. It reaches one more home but takes away the drive upgrade step, so I kept the hull change.

### Range vs opening (owner item 5)

Out of 87 homes (7 maps × 3 starts, all tier-0 stations):

| Stock ship | Dark system one jump away, within effective range | Same, pushing | Dark out-and-back on one tank, never past effective range |
| --- | --- | --- | --- |
| Tank 3 | 49 | 67 | 73 (84%) |
| Tank 4 | 49 | 67 | 80 (92%) |
| **Tank 6** | 48 | 67 | **83 (95%)** |
| Before (drive 1.5, Medium mass 50), tank 6 | 43 | 66 | 76 (87%) |

- **The out-and-back can take several jumps**, all within effective range, through any systems. Only about half the homes have a dark system one jump away.
- **The 4 homes left at tank 6 are all on maps with exactly one dark system** (seed 57206 at every start, seed 333860 at year 25). No stock-ship change short of a 3.0 drive reaches them, so that's a map property, not a ship one.
- **In play, the bots' opening dark run happened in year 1 in 21 of 21 careers per style at tank 6**, median day 22.

Per map, tank 6 (homes with an out-and-back / homes; dark systems at start):

| Seed | Early start | Middle start | Late start |
| --- | --- | --- | --- |
| 333860 | year 25: 3/4 (1 dark) | year 95: 4/4 (2 dark) | year 160: 4/4 (5 dark) |
| 110478 | year 10: 4/4 (4 dark) | year 80: 4/4 (11 dark) | year 160: 4/4 (7 dark) |
| 697804 | year 30: 4/4 (6 dark) | year 90: 4/4 (4 dark) | year 180: 4/4 (4 dark) |
| 57206 | year 53: 3/4 (1 dark) | year 64: 3/4 (1 dark) | year 75: 3/4 (1 dark) |
| 632322 | year 10: 4/4 (7 dark) | year 90: 4/4 (4 dark) | year 165: 4/4 (4 dark) |
| 517908 | year 80: 4/4 (6 dark) | year 160: 4/4 (6 dark) | year 230: 4/4 (5 dark) |
| 552723 | year 75: 5/5 (7 dark) | year 120: 5/5 (5 dark) | year 200: 5/5 (5 dark) |

### Tank sweep (pilot_exp, agg_l3.txt)

3 styles × tanks 3/4/6 × 7 seeds × 3 starts × 40 years = 189 careers. Net worth gained per year (median), refit span, and strandings per 1,000 legs:

| Style | Tank | Net / yr | Span (3 refit steps, median years) | Strandings / 1,000 legs | Rescues | Year-1 dark run |
| --- | --- | --- | --- | --- | --- | --- |
| Hauler | 3 | 1,941 | PASS (0.2, 2.1, 8.1) | 0.39 | 20 | 19/21 |
| Hauler | 4 | 1,540 | PASS (0.2, 2.0, 7.2) | 0.12 | 6 | 21/21 |
| Hauler | **6** | 1,351 | PASS (0.3, 2.0, 7.3) | 0.06 | 3 | 21/21 |
| Crisis | 3 | 1,539 | PASS (0.2, 1.8, 6.9) | 0.58 | 30 | 20/21 |
| Crisis | 4 | 1,266 | PASS (0.2, 1.6, 5.9) | 0.20 | 10 | 21/21 |
| Crisis | **6** | 1,370 | PASS (0.2, 1.7, 5.7) | 0.06 | 3 | 21/21 |
| Pathfinder | 3 | 403 | PASS (0.2, 0.6, 6.4) | 0.49 | 20 | 21/21 |
| Pathfinder | 4 | 346 | PASS (0.2, 0.7, 4.5) | 0.07 | 3 | 21/21 |
| Pathfinder | **6** | 338 | PASS (0.2, 0.7, 4.8) | 0 | 0 | 21/21 |

Span PASS means at least 19 of 21 careers reach every step and each step comes within 10 years of the last. Every cell also has 0 stuck pilots, exact ledgers, and at most 20 days idle in a row.

**Stock tank 6 for now; tank 3 vs 6 is a balance lever, not a settled decision (owner, layer-3 review).** These numbers are from layer 3, before law and pirates; layer 4a's are below.

- **It's best for the opening.** All dark runs happen in year 1, strandings are the lowest, and it's best for range (95% of homes).
- **The cost is hauler income.** Tank 3 pays haulers 44% more, because the lighter ship reaches further and fits more hold, but strands 6–7× as often. Tank 4 is the middle ground.
- **Pathfinder earns a quarter of a hauler.** It has charts and surveys only; expeditions and pockets come in layer 5.

### Early-game feel (year 1, tank 6, 21 careers per style)

| | Hauler | Crisis | Pathfinder |
| --- | --- | --- | --- |
| Days to first nearby-body survey (median) | 1 | 1 | 1 |
| Days to first dark out-and-back (median) | 22 | 22 | 22 |
| Year-1 trips ≤1 J / 1–2 J / >2 J | 284 / 350 / 123 (38/46/16%) | 273 / 356 / 129 (36/47/17%) | 222 / 191 / 102 (43/37/20%) |
| Year-1 strandings, rescue cost | 0 | 0 | 0 |
| Share of year 1 docked waiting for work | 0% | 0% | 0% |

- **Trips are jobs and the pilot's own hauls**, measured by straight-line distance. The new-pilot filter applies to posted jobs only, so a >2 J trip is an own haul over several jumps.
- **"Waiting" means docked with nothing to do.** The 4-day turnaround at each stop isn't counted.
- **At tank 3,** the only year-1 stranding was one pathfinder rescue (500).

### Rescue and debt clearing (owner request)

95 rescues over 189 careers × 40 years. 79 were paid from the wallet; 16 left a debt. Time to clear each rescue debt, in years:

| Style | Debts | Median | p90 | Max | Flag (p90 > 3 y) |
| --- | --- | --- | --- | --- | --- |
| Hauler | 5 | 0.65 | 2.31 | 2.31 | no |
| Crisis | 7 | 0.65 | 2.94 | 2.94 | no, but close: all 6 tank-3 debts |
| Pathfinder | 4 | 0.38 | 1.15 | 1.15 | no |

- **No debt was still open at the end of any career.**
- **Rescue prices** have a median of 500 (a tow from next door) and a maximum of 3,500 (five crew legs).
- **The sample is small** because few strandings end in debt. The p90 rests on a handful of cases, so treat it as a smoke test. A wider run is cheap if wanted.
- **The flag fired before.** Under the earlier stock ship (Medium mass 50), hauler tank 3 had p90 3.71 y and pathfinder 5.96 / 5.65 y. The lighter hull removed both.

### Contested bids (owner item 7)

During the 63 tank-6 careers (2,520 pilot-years) the world cleared 0 contested indivisible objects; an earlier run of the same sweep saw 2 in 4 bids. Pilots don't bid in layer 3: they take posted jobs first come, first served. So this is still the faction count. It becomes a pilot measure once contracts are bid for.

## Layer-3 gates

| Gate | Result |
| --- | --- |
| Replay (inputs only, no bot) = live, every month, full state (world + pilot) | 5 / 5 |
| Checkpoint mid-career resume = uninterrupted | 5 / 5 |
| Checkpoint round trip byte-exact, with pilot | 5 / 5 |
| Same-tick actions reversed, canonical order: same world | 5 / 5 |
|   teeth: in raw arrival order the reversed actions change the world | 3 / 5 (the 3 careers with several actions in one tick) |
| Native = WebAssembly with pilots, 5 careers × 10 years, every month | PASS (digest 818d976d165604af) |
| Ledgers exact with the pilot (goods, money, hold, losses), every career in the sweep | 189 / 189 |
| No stuck pilot (never idle more than 20 days in a row) | 189 / 189 |
| Rule `rescue` off (flat tow): ledgers exact; careers changed | 15 / 15; 1 / 15 |
| Rule `shields` off: ledgers exact; careers changed (fixture: 2 shields fitted) | 15 / 15; 11 / 15 |
| Rule `weapons` off: ledgers exact; careers changed (fixture: 2 weapons fitted) | 15 / 15; **0 / 15** |
| Layer 1 and layer 2 gates rerun with layer 3 in place | all PASS |

**Open items on the switches:**

- **`weapons` has no teeth yet.** Weapons only act on a landing inside a rogue-body zone, and no career had a decisive roll. Pirates don't attack pilots until law and logs (layer 4).
- **`jumps` off and `hulls` off aren't built.** Those are the v0.20 hop model and fixed upgrades; layer 3 has only the J model.
- **`pockets` and `farpay` move to layer 5** with the pathfinder's expeditions.

**Found and fixed on the way:**

- **The bots' toll cache used `thread_local`.** It broke the WebAssembly link and quietly tied the core to a toolchain. It now lives in `World` as a cache, not state.
- **The checkpoint reader is strict.** It refuses a bool that isn't 0/1, an out-of-range int or char, and a duplicate map key. One of the 20 teeth corruptions had landed on a bool and read back as the same world.

## Layer 4a: what it is

**End states (owner, 2026-11-20).** These replace the spec's "never stuck". Each is a true end state, reported per style, not gated. A style above about 5% gets flagged.

- **Death, lost in space.** Life support is a ship system:
  - Days come from the hull (Light 60, Medium 90, Heavy 120). Medium and Heavy can cut off decks (+50%).
  - A life-support refit adds 30 days per point. The flight suit and seat add 10.
  - Health is lost to misjumps, bad landings and pirates; shields cut that, and it's repaired at a dock.
  - If the rescue crew can't arrive before life support runs out, the pilot dies.
- **Death by hazard.** A bad landing is fatal with `fatalPm` 2‰, which shields cut. 20‰ killed 17% of careers, because every style takes about 9 bad landings in 40 years.
- **Debt.** Ten years of continuous debt is bankruptcy. All debt runs on one clock that resets only at zero. There are warnings at 5, 8 and 9 years, and a new repay action. Bots repay cash above their reserve.
- **Prison is deferred.** Being exiled from every house doesn't end the game: the independent stations (3–17 per map, no patrols, no exile) are the rogue haven. Breaking an exile stays a serious charge plus a term restart, as the owner and Grok agreed.
- **The softlock check stays a gate.** A pilot with no move for more than 20 days is a bug, not an ending.

**Law and logs (spec A law, B3, C4, C7).** Interpretations marked * are vetoable.

- **Departure logs (B3).** Each departure is classed by the issuer, the origin owner and the destination owner:
  - **Own mission** (issuer = origin owner): no heat.
  - **Tolerated:** 25% heat.
  - **Stands:** 100% heat, including every contraband departure.
  - **Odd** (a rival's mission, none of its cargo aboard): 25% heat, and never evidence.
  - A friendly station (5+ deliveries) or a discontent one loses a standing log half the time.
- **Heat** is the active search per house. It fades daily, with the remainder carried (C1). At "noticed" and "watched" levels, scans become 2× and 4× likelier.
- **Warrants*** need heat at 100 plus evidence: a standing contraband log, a scan finding or a run. Legal and odd logs never make a warrant.
- **Arrival with no origin (C7):** suspicion grows with depth from unclaimed space when the arrival isn't from the last logged position.
- **Scans on arrival (C4):** odds scale with the house's patrol strength.
  - **Response:** a wanted pilot or exposed contraband means comply or run. Running is a blind jump out; escaping adds heat and evidence.
  - **Compartments:** a package in a hidden compartment is never found.
- **Charges:**
  - **Minor:** fine 300, keep flying.
  - **Serious:** fine 600, held 180 days, contraband seized. Legal cargo stays.
  - **Escalation:** each warrant or charge counts, paid ones included. The third offence with one house is grave: exile from that house's docks for 1,800 days plus 30 days' deportation, and it stops there.
  - **Lapsing:** minor warrants lapse after 720 days; serious ones stay on file.
- **Ways out of a warrant:** pay, inform (pays 800 and locks courier and spy work for 3,600 days), or debt with garnishment. A warrant served by a patrol or a dock check is a charge at its tier.
- **Courier and spy work.** It's issued by a house other than the station owner, by construction (B3). It unlocks after 3 / 8 deliveries of any kind.
- **Rendezvous*.** These are offered on courier and spy work.
  - **Where and when:** a meet system within 1 J of the destination. The contact is due when a stock ship would arrive and waits 10 days. Missing it fails the job with no charge.
  - **Pay:** direct pay plus one extra leg, plus 25%.
  - **Trace:** following the spec text ("a log is beaten only by a rendezvous, cover or a lost log"), a meet run leaves no departure log: the package never passes through a station.
- **Pirates against pilots.** Arriving with cargo or a package in a system on a pirated route triggers an encounter with chance `pirEncPm` 20‰ per pirate group of strength, capped at 250‰.
  - **Resist roll:** 300‰, plus 100‰ per weapon point.
  - **A lost encounter:** the cargo is looted, and the drive and life support take damage (shields cut it).
  - **Rate:** at 60‰, haulers lost 43% of their income, so I set 20‰: about 6–7 encounters per 40-year career.
- **Law styles (E1 intents):**
  - **careful:** a 50‰ catch limit, avoids hot houses.
  - **bold:** 150‰, backs off a house that warned it.
  - **reckless:** any job, runs from scans.
  - **payer:** like bold, pays every warrant at once.
  - **forgetful:** like careful, never pays.
  - **double agent:** spy work, informs when served.
  - **Builds:** all of them fit a hidden compartment first.

### Layer 4a results (curated 7 seeds × 3 starts × 40 years, stock tank 6; agg_l4.txt)

| Style | Net/yr | Span (median years per step) | Ends | Courier/spy jobs | Meets | Logged | Warrants | Charges minor / serious / grave | Recovery days minor / serious / grave |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| hauler | 1,319 | PASS (0.3, 2.0, 7.6) | 21 alive | – | – | – | 0 | 0 / 0 / 0 | – |
| crisis | 1,484 | PASS (0.2, 1.7, 6.8) | 21 alive | – | – | – | 0 | 0 / 0 / 0 | – |
| pathfinder | 332 | PASS (0.2, 0.7, 4.7) | 21 alive | – | – | – | 0 | 0 / 0 / 0 | – |
| careful | 770 | PASS (0.4, 0.5, 1.1) | 20, 1 hazard | 704 | 146 | 51.6% | 45 | 3 / 2 / 6 | 97 / 194 / 30 |
| bold | 868 | PASS (0.3, 0.5, 0.8) | 20, 1 hazard | 659 | 77 | 59.1% | 34 | 2 / 6 / 0 | 64 / 203 / – |
| reckless | 2,016 | PASS (0.3, 0.5, 0.8) | 21 alive | 2,766 | 1,115 | 35.1% | 487 | 101 / 11 / 148 | 20 / 205 / 30 |
| payer | 858 | PASS (0.3, 0.5, 1.0) | 21 alive | 716 | 69 | 59.4% | 31 | 0 / 6 / 0 | – / 199 / – |
| forgetful | 808 | PASS (0.4, 0.5, 1.0) | 21 alive | 1,102 | 205 | 45.7% | 56 | 48 / 3 / 8 | 33 / 189 / 30 |
| double agent | 771 | PASS (0.5, 0.7, 4.1) | 21 alive | 78 | 12 | 73.3% | 23 | 0 / 5 / 0 (21 informs) | – / 195 / – |

- **Law styles reach the span fast.** All nine styles pass the span; the law styles get there in about a year because courier pay is front-loaded. Against layer 3, the hauler lost a little to pirates (1,351 → 1,319). Crisis rose (1,370 → 1,484) for reasons I haven't traced.
- **Logs make tradeoffs (E3):** 2,815 of 6,283 smuggling departures logged, 44.8%, inside the 25–50% target. Per style it's 35–73%: bold, payer and the double agent take fewer meets, so more of their runs log.
- **Recovery (E2):** minor is at most serious for every style that had both. Reckless recovers in 20 / 205 / 30 days against v0.20's 242 / 260 / 286, so no worse.
  - **Serious is the 180-day hold plus about 20 days.** Grave is the 30-day deportation, because exile doesn't stop a pilot earning elsewhere.
  - **Spec issue:** v0.20 had grave above serious; the spec only asks minor ≤ serious and "no worse".
- **Career endings (E2):** on the curated runs, 2 deaths by hazard in 189 law-style careers and no others. On 300 random seeds (endstates_wide_l4.json) every style ends at 0.3–2.3%: 0–7 hazard deaths per 300, plus 1 pathfinder and 4 reckless bankruptcies. No style is near the 5% flag. Softlock 0 and ledgers exact everywhere.
- **Rescue debt now (wide sample, agg_wide.txt).** At the stock tank, debts are rare except the pathfinder: 10 debts, p90 9.4 y. Pooled over tanks 3/4/6 the p90 is 5.9 y (hauler, crisis) and 5.4 y (pathfinder), up from 2.3 / 2.5 / 5.3 before pirates. Lost cargo slows repayment. This is reported, not gated, now that debt is an end state. The flag is there for a look.

### Ablations, one switch off (E4, abl_l4.json; median net per style at the stock tank)

| Switch off | hauler | crisis | pathfinder | careful | bold | reckless | payer | forgetful | double agent |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| (base) | 1,319 | 1,484 | 332 | 770 | 868 | 2,016 | 858 | 808 | 771 |
| interest | 0% | 0% | 0% | +32% | −10% | +6% | +35% | +11% | −1% |
| meet | 0% | 0% | 0% | +3% | +17% | −41% | +31% | +17% | +6% |
| law | 0% | 0% | 0% | +26% | +12% | −54% | +8% | +15% | +20% |
| pirates | −2% | −10% | +1% | +20% | +3% | +5% | −9% | +9% | 0% |
| lifesupport | −1% | −8% | −1% | +4% | +11% | +11% | +13% | +12% | +13% |
| shields, weapons, rescue, hazdeath, bankrupt | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% | 0% |

- **Interest off moves smugglers' net by 5% or more (E3)** for 5 of 6 law styles. The double agent (−1%) fails, with only 86 smuggling departures in 21 careers.
  - **The direction is odd.** careful and payer earn *more* with every departure standing. Heat pushes them off courier work into legal hauling, which pays better than their risk-limited courier choices. Their job choice compares estimated pay per day, and pirates and holds aren't in the estimate. To fix in the builds pass.
- **Rendezvous pay per day vs direct (E3):** meets pay 120–151 a day against 96–98 direct for careful, bold, reckless, payer and forgetful (PASS). The double agent is 84 vs 109 (FAIL, 12 meets).
- **Weapons and shields:** no bot buys them yet, hence the 0%. The switch gate fits them as a fixture, and weapons off changes 5 of 15 careers. Builds are layer 5 (E3: "reported plainly as earning nothing" for now).
- **Rare switches:** rescue, hazdeath and bankrupt are rare events on the curated seeds. Their fixtures in the end-state gate have teeth.

## Layer-4a gates

| Gate | Result |
| --- | --- |
| End states (fixtures, each with its rule-off twin): rescued in time; life support failed = death lost in space; bankrupt at 10 years to the day with warnings at 5/8/9; repay resets the clock; fatal landing; life-support refit adds days and survives a hull swap | 9 / 9 |
| Law: three ways out of a warrant; contraband always stands; an odd departure never makes a warrant (40 in a row); interest off = every departure stands; exile is docking only; a breach is a serious charge and a term restart, never an ending; the third offence is grave | 9 / 9 |
| Rule switches off (law, interest, meet, pirates, lifesupport, hazdeath, bankrupt, rescue, shields, weapons): ledgers exact on 15 careers each; weapons off must change outcomes | all PASS (weapons changes 5/15) |
| Replay = live, mid-career resume, checkpoint round trip, canonical input order; cases now include reckless, careful and double agent | 5 / 5 each |
| Native = WebAssembly with the law styles, every month | PASS (digest 115a90f447c9cda6) |
| Softlock 0 and ledgers exact, every career (315 curated, 4,500 wide) | PASS |
| Layer 1 and 2 gates rerun with layer 4a in place | all PASS |

**Vetoable decisions made in 4a:**
- **End-state numbers:** life-support days, cut-off decks, the suit reserve, damage, `fatalPm` 2‰, warnings at 5/8/9 years.
- **Pirate rate:** 20‰ per group of strength, capped at 250‰.
- **Warrants need evidence:** a standing contraband log, a scan finding or a run.
- **Served warrants:** one served by a patrol or dock check is a charge at its tier.
- **Grave:** exile plus 30 days' deportation.
- **Rendezvous:** a meet run leaves no departure log; the contact is due at a stock ship's arrival time.
- **Unlocks and friendly stations:** every delivery counts, trade included.
- **Courier and spy pay:** 400 + 25 per map unit; spy 1,500.
- **Law-style catch limits:** 50 / 150 / 1,000 / 150 / 50 / 300‰.
- **Builds:** every law style fits a compartment first.
- **Exile while flying:** a pilot exiled mid-trip doesn't dock at that house; a player may choose to dock and breach (`dockAnyway`).

**What 4a doesn't do yet:**
- the in-flight point-of-no-return warning (with the bubble step);
- cover and stacked jobs (layer 5);
- builder, farmer, stances and frontier ambiguity (4b);
- bots buying shields or weapons (builds, layer 5).

**Found and fixed on the way:**
- A held courier job could softlock a pilot when its destination's house exiled them mid-trip. The bot now delivers or drops it (new action `A_DROP`).
- The engine's own diversions (short of fuel, undocked at a dead station) now avoid exiled houses' docks.
- My first courier pay placeholders were labelled "v0.20" but weren't v0.20's values. They're relabelled (layer 4, with v0.20's in the meaning).

## Speed (bench_l2.txt, bench_l3.txt and bench_l4.txt, same machine, same day)

**World only** (no pilot), 7 curated seeds × 300 years, best of 3:

| Build | ms per sim-year | Sim-years a second |
| --- | --- | --- |
| v0.20 (Node) | 6.18 | 162 |
| v2 native, intents off | 1.36 | 734 |
| v2 native, intents on | 1.52 | 660 |
| v2 WebAssembly, intents off | 1.59 | 628 |
| v2 WebAssembly, intents on | 1.75 | 571 |

**The thread pool never runs on real faction decisions.** Staggering means at most 3 houses decide on any one day, and the pool waits for 4. All decisions together cost 0.31 s over 2,100 sim-years, about 0.15 ms per sim-year, which is less than a thread handoff. Parallel experiment branches remain the dependable speed-up. 

**Per pilot-year** (career time including the world, 21 careers × 40 years per style):

| Build | Hauler | Crisis | Pathfinder |
| --- | --- | --- | --- |
| v0.20 (Node) | 13.35 ms | 14.41 ms | – |
| v2 native, layer 3 | 2.66 ms | 2.60 ms | 4.66 ms |
| v2 native, layer 4a (law, pirates, end states on) | 2.70 ms | 3.08 ms | 4.70 ms |

v2 is about 5× faster per pilot-year. The pathfinder went from 13.7 to 4.7 ms once routes and gravity-well delays were cached outside state; gprof had shown 30,807 route searches per career. The whole 189-career sweep runs in about 27 s of career time, spread across threads.

## Vetoable decisions made in layer 3

- **Stock ship:** drive 1.5, Medium hull 120 space / 40 mass, Heavy mass 80 (above). Stock tank 6.
- **Refit orders for the bots.**
  - Hauler and crisis: hold, hold, Heavy hull, hold × 4, drive 2.0, hold × 3.
  - Pathfinder: hold, drive 2.0, Light hull, shield.
  - A bot refits only when debt-free, with at least 1,500 left over, and only if the ship keeps at least 0.9 J.
- **New-pilot board.** For the first 360 days, posted jobs are limited to lit systems within effective range.
- **Opening bot rule.** Survey home bodies, then one dark out-and-back within effective range, then work. New placeholders: `bodyPay` 150, `surveyDays` 3, `chartPrice` 600, `newPilotDays` 360.
- **Rescue.** The crew always comes from the nearest lit, served station in a 1.0 J ship.
- **Pathfinder for now.** No expeditions or pockets until layer 5, so its income is low by design for now.

## Next

1. **The jump-bubble step** (owner-approved order): green/yellow/red landing bubbles, the in-flight point-of-no-return warning with the direct-vs-detour choice, and bots planning only on what their pilot knows.
2. **Layer 4b:** builder and farmer (beacons), stances as intents, frontier ambiguity.

## Rules and placeholders (generated from src/core/params.def and src/world/wparams.def)

| Rule | Default | Layer | Meaning |
| --- | --- | --- | --- |
| `jumps` | on | 3 | Jumps measured in J (B1); off = the v0.20 hop model (off not built yet: layer 3 has only the J model) |
| `hulls` | on | 3 | Ships as mass and space (B2); off = fixed v0.20 upgrades (off not built yet) |
| `rescue` | on | 3 | Priced rescue with debt (B6); off = the v0.20 flat tow |
| `pockets` | on | 5 | Deep frontier pockets (A12) |
| `farpay` | on | 5 | Far pay in the pockets only (A12) |
| `shields` | on | 3 | Shields cut misjump and hazard damage (B2) |
| `weapons` | on | 3 | Weapons add to the resist roll vs pirates and rogue bodies (B2) |
| `lifesupport` | on | 4 | Life support is a system with days and health; a rescue that comes too late is death (owner, 2026-11-20); off = the wait is always survivable (B6 as written) |
| `hazdeath` | on | 4 | A bad landing can be fatal (small chance, shields cut it) |
| `bankrupt` | on | 4 | Ten years of continuous debt is bankruptcy: the career ends (owner, 2026-11-20) |
| `interest` | on | 4 | Logs read by interest (B3); off = every departure stands |
| `meet` | on | 4 | Rendezvous jobs (B3) |
| `law` | on | 4 | Heat, warrants, charges, exile, scans (spec A law, C4); off = no law |
| `pirates` | on | 4 | Pirates attack pilots on arrival (resist roll, weapons help) |
| `stack` | on | 5 | Stacked jobs (B4); off = one job at a time |
| `cover` | on | 5 | A legal haul on the route is cover (B4) |
| `intents` | on | 2 | Faction intents and one clearing step (B5); off = the v0.20 faction cycle |

| Placeholder | Default | Unit | Meaning | From |
| --- | --- | --- | --- | --- |
| `jumpUnitPct` | 100 | % of the map's median lane length | 1 J | new |
| `overreachPm` | 1500 | per mille of effective range | Longest jump allowed | new |
| `misBasePm` | 20 | per mille | Misjump chance up to effective range | new |
| `misSlopePm` | 400 | per mille per 1.0 over | Rise in misjump chance past effective range | new |
| `beaconRiskPm` | 500 | per mille multiplier | Misjump multiplier at a lit beacon | new |
| `blindRiskPm` | 2000 | per mille multiplier | Misjump multiplier at a blind destination | new |
| `daysPerJ` | 2 | days per J | Universe time per J of jump length | new |
| `blindTimePct` | 200 | % | Blind jump time vs a beacon jump | v0.20 |
| `subLight` | 2 | money per jump leg | Sub-light running cost | v0.20 |
| `massRef` | 100 | mass | Reference mass (the stock ship as fitted) | new |
| `lightSpace` | 50 | space | Light hull space budget | owner |
| `lightMass` | 30 | mass | Light hull mass | new |
| `mediumSpace` | 120 | space | Medium hull space budget (stock); layer 3: 100 left no room for early refits | layer 3 |
| `mediumMass` | 40 | mass | Medium hull mass; layer 3: 50 left the stock ship short of a dark out-and-back from 1 home in 8 (owner item 5) | layer 3 |
| `heavySpace` | 160 | space | Heavy hull space budget | new |
| `heavyMass` | 80 | mass | Heavy hull mass; layer 3: 110 made the hull swap unflyable without a new drive | layer 3 |
| `drive05Space` | 4 | space | Drive 0.5 | new |
| `drive05Mass` | 4 | mass | Drive 0.5 | new |
| `drive10Space` | 8 | space | Drive 1.0 | new |
| `drive10Mass` | 10 | mass | Drive 1.0 | new |
| `drive15Space` | 14 | space | Drive 1.5 | new |
| `drive15Mass` | 18 | mass | Drive 1.5 | new |
| `drive20Space` | 22 | space | Drive 2.0 | new |
| `drive20Mass` | 28 | mass | Drive 2.0 | new |
| `drive30Space` | 40 | space | Drive 3.0 | new |
| `drive30Mass` | 50 | mass | Drive 3.0 | new |
| `tankSpace` | 3 | space per jump | Tank | new |
| `tankMass` | 2 | mass per jump | Tank | new |
| `holdSpace` | 8 | space per 10 units | Hold | new |
| `holdMass` | 3 | mass per 10 units | Hold | new |
| `compSpace` | 16 | space per 10 units | Hidden compartment | new |
| `compMass` | 4 | mass per 10 units | Hidden compartment | new |
| `shieldSpace` | 6 | space per point | Shield | new |
| `shieldMass` | 6 | mass per point | Shield | new |
| `shieldCutPct` | 20 | % per point | Misjump and hazard damage cut | new |
| `weaponSpace` | 6 | space per point | Weapon | new |
| `weaponMass` | 8 | mass per point | Weapon | new |
| `weaponResistPm` | 100 | per mille per point | Resist bonus vs pirates and rogue bodies | new |
| `partMaxPoints` | 3 | points | Most shield or weapon points | new |
| `stockDrivePm` | 1500 | per mille J | Stock drive rating; layer 3: 1.0 reached a dark system from too few homes (owner item 5) | layer 3 |
| `stockTank` | 6 | jumps | Stock tank (swept at 3 / 4 / 6, E6) | owner |
| `stockHold` | 80 | units | Stock hold | v0.20 |
| `refitSmall` | 2500 | money | Refit price, parts up to sizeSmallMax space | owner |
| `refitMid` | 5000 | money | Refit price, parts up to sizeMidMax space | owner |
| `refitLarge` | 7500 | money | Refit price, larger parts and any hull | owner |
| `sizeSmallMax` | 8 | space | Largest small part | new |
| `sizeMidMax` | 16 | space | Largest mid-size part | new |
| `refuelPerJump` | 10 | money | Refuelling at a dock, cash | new |
| `advancePct` | 25 | % | Extra taken from the delivery's pay for a fuel advance | owner |
| `rescueCrewLeg` | 300 | money per leg | Rescue crew cost per jump leg, before risk | owner |
| `rescueRiskDivPm` | 10 | per mille | Leg cost multiplier = 1 + misjump per mille / this | owner |
| `rescueRiskCapPm` | 40 | per mille | Crew-leg risk used for the rescue price is capped at this (the blind in-range level, so a leg costs at most 1,500 as in B6); layer 4: uncapped, one pushed blind crew leg cost up to 25,000 | new |
| `rescueCrewPm` | 1000 | per mille | Rescue crew ship drive rating (J of effective range, x1000) | owner |
| `rescueTow` | 500 | money | Tow and salvage | owner |
| `rescueRespDays` | 10 | days | Rescue response within 1 J of lit space | owner |
| `rescueBlindDays` | 30 | days per blind jump out | Extra response time deep in the black | owner |
| `stuckDebtYears` | 10 | years | Rescue debt not cleared by then = stuck | owner |
| `heatDepart` | 40 | heat | Standing departure log | v0.20 |
| `toleratedPct` | 25 | % | Departure heat, tolerated case | new |
| `oddPct` | 25 | % | Departure heat, empty departure on a rival's mission | new |
| `heatDepthPerJ` | 15 | heat per J | Arrival with no origin, per J of depth | v0.20 |
| `logLostPm` | 500 | per mille | Chance a friendly or discontent manager loses a log | v0.20 |
| `friendDel` | 5 | deliveries | Deliveries before a station manager is friendly | v0.20 |
| `allyRel` | 2000 | relation | Ally threshold for the interest lookup | new |
| `rivalRel` | -1000 | relation | Rival threshold (or at war) | new |
| `meetDepthPm` | 2000 | per mille J | Rendezvous offered at or past this arrival depth | new |
| `meetRangePm` | 1000 | per mille J | Meet point distance from the border | new |
| `meetWait` | 10 | days | Rendezvous wait window | v0.20 |
| `meetPremiumPct` | 25 | % | Rendezvous pay on top of the extra legs | new |
| `coverPct` | 50 | % | Departure heat and scan odds with cover | new |
| `packDepth` | 3 | jobs | Exhaustive packing depth for bots | new |
| `boardSize` | 12 | jobs | Jobs per station board | new |
| `minorLapseDays` | 720 | days unseen | Minor warrants lapse | new |
| `fineCap` | 600 | money | Flat fine cap | v0.18 |
| `exileDays` | 1800 | days | Exile term | v0.18 |
| `jailDays` | 180 | days | Jail | v0.17 |
| `deportDays` | 30 | days | Deportation | v0.17 |
| `informPay` | 800 | money | Pay for informing | v0.13 |
| `informLockDays` | 3600 | days | Courier and spy lockout after informing | v0.13 |
| `garnishPct` | 25 | % | Garnishment on debt | v0.17 |
| `pocketsN` | 2 | per map | Deep pockets | v0.20 |
| `pocketJumps` | 2 | jumps | Jumps past the farthest frontier | v0.20 |
| `pocketJumpPm` | 1900 | per mille J | Length of each pocket jump | v0.20 |
| `pocketCap` | 80 | units per good | Pocket market size | v0.20 |
| `pocketDrainPct` | 10 | % a month (daily rate) | Pocket drain | v0.20 |
| `pocketChart` | 1200 | money | Chart of a pocket | new |
| `pocketHazard` | 300 | money | Hazard data from a pocket | new |
| `farPayPct` | 50 | % | Far pay in the pockets | v0.20 |
| `factionCycle` | 30 | days | How often a faction decides (staggered by hash) | new |
| `delaySupply` | 10 | days | Intent delay: move supply | new |
| `delayPatrol` | 10 | days | Intent delay: assign patrols | new |
| `delayToll` | 30 | days | Intent delay: set a toll | new |
| `delayContract` | 30 | days | Intent delay: buy or sell a contract | new |
| `delayStrategic` | 10 | days | Intent delay: contest, fortify, cut off, expand (30 halved conflict on 200 seeds) | layer 2 |
| `delayStance` | 30 | days | Intent delay: change stance, patron choice | new |
| `delayBeacon` | 60 | days | Intent delay: build or relight a beacon | new |
| `plStartWallet` | 2000 | money | Starting wallet | v0.20 |
| `upkeepYr` | 120 | money a year | Ship upkeep, charged monthly | v0.20 |
| `misDmg` | 60 | drive health | Damage from a misjump or a bad landing | v0.20 |
| `wearPerLeg` | 2 | drive health | Wear per jump | v0.20 |
| `repairFee` | 150 | money | Repair fee | v0.20 |
| `repairFull` | 1000 | money | Repair cost of a fully worn drive | v0.20 |
| `repairAt` | 500 | drive health | Bots repair below this | v0.20 |
| `dockDays` | 4 | days | Days docked per stop | v0.20 |
| `plReservePct` | 40 | % | Stock a station keeps back from pilots | v0.20 |
| `emergencyBonus` | 2 | x value | Emergency job reward multiplier | v0.20 |
| `blockadeBonus` | 3 | x value | Blockade job reward multiplier | v0.20 |
| `blockadeRiskPm` | 60 | per mille | Cargo lost on arrival at a contested station | v0.20 |
| `jobDeadline` | 120 | days | Job deadline | v0.20 |
| `floorBase` | 40 | money | Floor job base pay | v0.20 |
| `floorPerLen` | 1 | money per map unit | Floor job pay per distance | v0.20 |
| `chartPrice` | 600 | money | Chart of a dark system | v0.20 |
| `chartMonths` | 6 | months | A sold chart shortens the buyer's survey | v0.20 |
| `bodyPay` | 150 | money | Survey of a nearby body (no jump) | layer 3 |
| `surveyDays` | 3 | days | Survey of a nearby body | layer 3 |
| `newPilotDays` | 360 | days | New pilots see only jobs within effective range, to lit systems | layer 3 |
| `refitReserve` | 1500 | money | Bots keep this after a refit | v0.20 |
| `lsLightDays` | 60 | days | Life support of a Light hull at full health | owner, layer 4 |
| `lsMediumDays` | 90 | days | Life support of a Medium hull at full health | owner, layer 4 |
| `lsHeavyDays` | 120 | days | Life support of a Heavy hull at full health | owner, layer 4 |
| `lsCutPct` | 50 | % | Cutting off decks stretches hull life support (Medium and Heavy only) | owner, layer 4 |
| `lsSuitDays` | 10 | days | Flight suit and seat reserve, always there (TBD) | owner, layer 4 |
| `lsPartDays` | 30 | days | Life support refit: days per point | layer 4 |
| `lsSpace` | 6 | space per point | Life support refit | layer 4 |
| `lsMass` | 4 | mass per point | Life support refit | layer 4 |
| `lsMisDmg` | 60 | per mille | Life support health lost to a misjump (shields cut it) | layer 4 |
| `lsHazDmg` | 100 | per mille | Life support health lost to a bad landing (shields cut it) | layer 4 |
| `repairLsFull` | 600 | money | Repair cost of fully failed life support | layer 4 |
| `repairLsAt` | 700 | per mille | Bots repair life support below this | layer 4 |
| `fatalPm` | 2 | per mille | Chance a bad landing is fatal, before shields (20 killed 17% of careers: about 9 bad landings per career whatever the style) | layer 4 |
| `bankruptYears` | 10 | years | Continuous debt this long is bankruptcy | owner, layer 4 |
| `debtWarn1` | 5 | years | First bankruptcy warning | layer 4 |
| `debtWarn2` | 8 | years | Second bankruptcy warning | layer 4 |
| `debtWarn3` | 9 | years | Last bankruptcy warning | layer 4 |
| `heatLegal` | 10 | heat | Heat of a standing legal departure log | layer 4 |
| `heatSpy` | 60 | heat | Heat of a standing spy departure log | layer 4 |
| `discontentMood` | 3000 | mood | A station below this mood is discontent (B3) | layer 4 |
| `heatFadePm` | 8 | per mille a day | Heat fades while unseen (daily, remainder carried, C1) | layer 4 |
| `heatT1` | 60 | heat | Noticed: scan odds x2 | layer 4 |
| `heatT2` | 150 | heat | Watched: scan odds x4 | layer 4 |
| `heatWarrant` | 100 | heat | A warrant is issued at this heat when there is evidence | layer 4 |
| `evidenceDays` | 360 | days | Evidence (a standing contraband log, a scan finding, a run) counts this long | layer 4 |
| `scanPm` | 40 | per mille | Patrol scan on arrival in a house's system, at mean patrol strength | layer 4 |
| `runEscapePm` | 600 | per mille | Running from a scan: blind jump out escapes (C4) | layer 4 |
| `heatRun` | 150 | heat | Heat from escaping a scan | layer 4 |
| `minorFine` | 300 | money | Minor charge fine (capped at 600) | owner |
| `seriousFine` | 600 | money | Serious charge fine (capped at 600) | owner |
| `graveAt` | 3 | offences | The offence with one house that becomes grave (exile) | layer 4 |
| `courierUnlock` | 3 | deliveries | Deliveries before courier work is offered | v0.20 |
| `spyUnlock` | 8 | deliveries | Deliveries before spy work is offered | v0.20 |
| `courierBase` | 400 | money | Courier pay base (v0.20: 100) | layer 4 |
| `courierPerLen` | 25 | money per map unit | Courier pay per distance (v0.20: 3) | layer 4 |
| `spyReward` | 1500 | money | Spy job pay (v0.20: 500) | layer 4 |
| `courierPct` | 50 | % | Share of stations offering a courier job each window (v0.20: 25 per 90 days) | layer 4 |
| `underWindow` | 30 | days | Courier and spy offers refresh (v0.20: 90 courier, 360 spy) | layer 4 |
| `refDaily` | 40 | money per day | Reference daily rate for rendezvous extra days (B3) | layer 4 |
| `pirEncPm` | 20 | per mille | Pirate encounter on arrival, per pirate group's worth of strength on routes through the system | layer 4 |
| `pirEncMaxPm` | 250 | per mille | Cap on the pirate encounter chance | layer 4 |
| `resistBasePm` | 300 | per mille | Resist roll against pirates without weapons | layer 4 |
| `pirDmg` | 100 | per mille | Drive and life-support damage from a lost pirate encounter (shields cut it) | layer 4 |

| World placeholder (v0.20 values unless noted) | Default | Meaning |
| --- | --- | --- |
| `rates` | [20, 30, 25, 10] | Daily burn per 10,000 people, by supply |
| `pop` | [10000, 6000, 2500] | Population by tier |
| `prodPct` | [[260, 240, 250, 300], [55, 60, 50, 40], [20, 35, 30, 10]] | Native production, % of burn, by tier and supply |
| `capDays` | [240, 150, 180] | Storage, days of burn, by tier |
| `targetPct` | 9000 | Stock target, per 10,000 of capacity |
| `hubReservePct` | 6000 | Hub reserve, per 10,000 |
| `distStep` | 20 | Route length step for freighter count |
| `freighterCap` | 3600 | Freighter capacity |
| `dwell` | 2 | Days docked per stop |
| `maxStops` | 3 | Stops per route |
| `valuePerUnit` | [3, 2, 2, 4] | Value per unit, by supply |
| `down` | [3000, 1200, 300] | Shortage stage thresholds down (per 10,000) |
| `up` | [5000, 2600, 1500] | Shortage stage thresholds up |
| `stageDwell` | 30 | Days before a stage can change |
| `burnPct` | [100, 82, 66, 40] | Burn % by stage |
| `failT` | 3500 | Failure threshold |
| `failMax` | 250 | Failure chance cap |
| `wearHit` | 300 | Wear per failure |
| `wearDecay` | 4 | Wear decay |
| `failWasteDays` | 2 | Days of stock wasted on failure |
| `moodTarget` | [7200, 4800, 2800, 1200] | Mood target by stage |
| `fundPct` | 45 | Patrol funding, % of route value |
| `patrolUnit` | 600 | Patrol unit size |
| `freighterDef` | 350 | Freighter defence |
| `pirGroup` | 700 | Pirate group size |
| `pirSensor` | 3 | Pirate sensor range |
| `patrolRange` | 4 | Patrol range |
| `lootPct` | 35 | Loot share on interception |
| `groupCooldown` | 6 | Pirate group cooldown days |
| `lurePct` | 7 | Pirates lured, % of route value |
| `pirCapPct` | 25 | Pirate growth cap % |
| `pirCapAbs` | 60 | Pirate growth cap, absolute |
| `upkeepPct` | 18 | Pirate upkeep % |
| `growthPct` | 45 | Pirate growth from loot % |
| `smoothDiv` | 6 | Patrol smoothing divisor |
| `capPct` | 8 | Patrol change cap % |
| `capAbs` | 150 | Patrol change cap, absolute |
| `lagMonths` | 6 | Patrol funding lag (history length) |
| `huntKillPct` | 20 | Patrol kill share |
| `huntLossPct` | 8 | Patrol loss share |
| `maxLegChain` | 22 | Longest leg chained onto a route |
| `frontierPer` | 2 | Frontier systems per sector |
| `frontierExtra` | 2 | Extra frontier systems |
| `clusterR` | [7, 12] | Home cluster radius |
| `frontierFromHome` | 18 | Frontier distance from any home |
| `frontierMaxD` | 34 | Frontier distance from its nearest home |
| `frontierGap` | 9 | Frontier spacing |
| `ownFrontierPct` | 30 | Chance a near frontier starts owned |
| `ownFrontierD` | 32 | Distance for an owned frontier |
| `majors` | 4 | Great houses (Core + sectors) |
| `mapSide` | [0, 0, 0, 70, 80, 90] | Map side by number of majors |
| `coreCluster` | 3 | Core cluster systems |
| `ringPct` | 38 | Sector ring radius, % of side |
| `coreExclude` | 22 | No frontier this close to the Core |
| `specMult` | [150, 120, 80, 50] | Sector specialisation multipliers |
| `hopNpc` | 22 | Hop range, map units (v0.20 stock reach) |
| `accW` | 500 | Hazard accident weight |
| `nebR` | 9 | Nebula radius |
| `nebAcc` | 150 | Nebula accident |
| `hzNeb` | 1 | Random nebulae |
| `hzWell` | 2 | Gravity wells |
| `hzRog` | 3 | Rogue-body fields |
| `wellR` | 9 | Well radius |
| `wellSub` | 3 | Well sub-light days |
| `rogR` | 6 | Rogue radius |
| `rogAcc` | 250 | Rogue accident |
| `bodyBudget` | [[0, 6], [16, 44], [24, 56]] | Body budget by tier (Core map) |
| `outpostBudgetPct` | 140 | Outpost body budget % |
| `bodyTau` | [[200, 450], [80, 200], [350, 600], [120, 300]] | Body lifetime years: world, belt, giant, ice |
| `fairK` | 2 | Fair region radius, jumps |
| `fairBand` | 15 | Fairness band % |
| `fairMaxTries` | 40 | Fairness redraws |
| `fairHorizonYears` | 200 | Fairness horizon |
| `rawCapDays` | 120 | Raw storage days |
| `minorPairPct` | 50 | Chance a minor starts with two systems |
| `minorT0` | 9000 | Minor treasury |
| `patronRel` | 4000 | Relation with patron |
| `treasury0` | 30000 | Great house treasury |
| `houseT0` | 20000 | Trader house treasury |
| `relInit` | [-3000, 3000] | Initial relation range |
| `splitPct` | 40 | Chance the Core starts split |
| `emaDiv` | 15 | Stock trend smoothing |
| `failLossPct` | 40 | Pirate loss on a failed raid % |
| `patrolLossPct` | 12 | Patrol loss on a successful raid % |
| `localEffPct` | 60 | Local refining efficiency % |
| `royaltyPct` | 50 | Owner royalty on local refining % |
| `rawValue` | 2 | Value of a raw unit |
| `floorDays` | 45 | Service floor: a stop at least every N days |
| `maxHulls` | 6 | Most hulls per route |
| `hullCostMonth` | 900 | Hull cost per month |
| `taxBp` | 4000 | Tax per 10,000 people |
| `patrolCostPct` | 50 | Patrol cost share |
| `indPopPct` | 30 | A dropped station stays independent above this % of its people |
| `windowMonths` | 60 | Accounting window |
| `dropPct` | 150 | Drop a station when cost exceeds return by this % |
| `adoptPct` | 70 | Adopt an independent when cost is under this % of return |
| `graceMonths` | 180 | Outpost grace period |
| `surveyT` | 5500 | Survey when output falls below this per 10,000 of base |
| `surveyMonths` | [36, 96] | Survey length |
| `maxStations` | 22 | Station cap (n0 + this - 12) |
| `foundPopPct` | 40 | Population of a founded or reclaimed station % |
| `reclaimMinPct` | 40 | Reclaim an abandoned station with this % of its deposits left |
| `foundNear` | 1 | Found outposts near home |
| `maxOutposts` | 1 | Live outposts per house |
| `dropMemory` | 360 | Months a dropped station is remembered |
| `oeDistK` | 6 | Overextension cost per distance |
| `oeCountK` | 30 | Overextension cost per station |
| `presK` | 4 | Presence falloff with distance |
| `homePct` | 50 | Home bonus % |
| `borderD` | 30 | Border reach (lanes) |
| `indMilDiv` | 4 | Independent militia divisor |
| `fortPct` | 60 | Fortification bonus % |
| `fortMonths` | 36 | Fortification months |
| `fortCost` | 8000 | Fortification cost |
| `ratioT` | 150 | Attack ratio needed vs independents |
| `ratioFacT` | 100 | Attack ratio needed vs factions |
| `coreRatioT` | 300 | Attack ratio needed vs a core |
| `contestCost` | 400 | Contest cost per month |
| `minDur` | 24 | Contest minimum months |
| `coreMinDur` | 60 | Core contest minimum months |
| `winT` | 400 | Contest win threshold |
| `coreWinT` | 1500 | Core contest win threshold |
| `maxContests` | 2 | Contests per house |
| `relTradeDiv` | 400 | Trade warming divisor |
| `relTradeMax` | 40 | Trade warming cap per month |
| `relDecay` | 120 | Relation decay divisor |
| `relStartHit` | 2000 | Relation hit for starting a contest |
| `relCaptureHit` | 2500 | Relation hit for a capture |
| `contestRelT` | -1000 | Hostile at or below this relation |
| `relBorderHouses` | 4 | Border souring per holding pair (Core map) |
| `housePriceK` | 4 | Trader house price per person |
| `houseMax` | 4 | Trader house station cap |
| `houseRecent` | 24 | Trader house buys stations dropped within N months |
| `upEvery` | 12 | Upstart check cycle |
| `upChance` | 2 | Upstart chance % |
| `upWeakT` | 400 | Thinly held threshold |
| `upMax` | 3 | Most upstarts |
| `upOwnerHit` | -5000 | Relation with the house an upstart broke from |
| `upRelOthers` | -1500 | Relation of others with a new upstart |
| `upT0` | 6000 | Upstart treasury |
| `upProve` | 60 | Upstart proving months |
| `upCrushT` | 100 | Crush ratio % |
| `upDepthK` | 20 | Crush depth falloff |
| `upMilitia` | 1500 | Upstart militia |
| `upCrushMonths` | 12 | Months of pressure to crush an upstart |
| `upDebt` | 20000 | Upstart dissolves below this debt |
| `expandT` | 40000 | Treasury to open an early survey |
| `takeFacPctHouses` | 150 | Take a faction station when cost is under this % of return |
| `servicePct` | 50 | Service fee share for foreign stops |
| `fogPct` | 30 | Contest fog % |
| `luckPct` | 70 | Contest luck % |
| `winPct` | 25 | Contest win share of defence |
| `decideEvery` | 3 | House decision cycle, months (v0.20 cycle) |
| `tDecay` | 200 | Treasury decay divisor |
| `strengthPopDiv` | 10 | Strength per person divisor |
| `coreStrength` | 500 | Strength per core |
| `tCapHouses` | 4000000 | Treasury counted toward strength, cap |
| `strengthTDivHouses` | 250 | Treasury strength divisor |
| `protectPct` | 60 | Patron protection % |
| `tributePct` | 15 | Client tribute % |
| `switchPct` | 200 | Patron switch margin % |
| `patronEvery` | 12 | Patron review cycle |
| `fallMax` | 1 | A house falls at or below this many stations |
| `fallNoHomeMax` | 3 | Or this many with no home |
| `riseHome` | 3 | A minor rises with a home and this many stations |
| `riseMin` | 5 | Or this many stations |
| `statusYears` | 2 | Years before status changes |
| `junctionProjPm` | 500 | Junction defence: share of holder's projection, per mille |
| `tollBase` | 25 | Base toll per 100 units |
| `tollFloor` | 8 | Toll floor |
| `tollMax` | 60 | Toll ceiling |
| `tollStep` | 3 | Toll step |
| `tollDist` | 60 | Toll weight in route choice |
| `spotPct` | 150 | Spot price without a contract % |
| `laneUpkeep` | 30 | Lane upkeep per month |
| `contractMonths` | 36 | Carrier contract months |
| `switchMarginPct` | 20 | Carrier switch margin % |
| `firstContract` | [12, 36] | First contract length range |
| `tradePct` | 50 | Inter-sector trade share of the gap % |
| `bcnUpkeepJ` | 60 | Beacon upkeep per month |
| `relightJ` | 4000 | Relight cost |
| `salvMult` | 3 | Relight multiplier for a salvaged beacon |
| `brokeMonths` | 12 | Broke = treasury under this many months of beacon bills |
| `sPerLaneJ` | 600 | Strength per kept beacon |
| `lapsePm` | 30 | Lapse chance per strain, per mille |
| `unkeptMonths` | 36 | Unkept beacons fail after N months |
| `relightReserve` | 4 | Relight only with this many times the cost |
| `relightPm` | 150 | Relight chance per mille |
| `relightGap` | 2 | Relight only below cap minus this |
| `prospKeep` | 120 | Prospector-kept beacon months |
| `darkTradePct` | 300 | Dark lane cost in route choice % |
| `darkLanePm` | 500 | Smugglers on a dark lane, per mille of trade |
| `darkLaneBasePm` | 1000 | Dark lane smuggler floor |
| `darkPm` | 200 | Smugglers per mille of trade |
| `darkLot` | 10 | Smuggler lot |
| `darkBasePm` | 1500 | Smuggler floor per lane |
| `darkFrontierPm` | 2000 | Extra smugglers at the frontier |
| `bandPm` | [[40, 250, 250, 250, 210], [20, 250, 250, 250, 230], [10, 200, 275, 275, 240]] | Blind arrival bands by drive |
| `detectPm` | [1000, 500, 250, 80, 0] | Detection by band |
| `quietPct` | [100, 75, 50] | Quieter drives % |
| `npcDrive` | 1 | NPC drive class |
| `edgeW` | 40 | Edge patrol threshold |
| `edgeCost` | 300 | Edge patrol cost |
| `edgeReserve` | 12 | Edge patrol reserve |
| `edgeMax` | 2 | Edge patrols per house |
| `edgeScanPm` | [0, 300, 200, 100, 0] | Edge scan by band |
| `psMin` | 50 | Patrol strength floor % |
| `psMax` | 200 | Patrol strength cap % |
| `edgePullPct` | 60 | Edge pull % |
| `cargoTrips` | 2 | Smuggler cargo trips |
| `cargoLot` | 10 | Smuggler cargo lot |
| `cargoSrcPct` | 60 | Smuggler source floor % |
| `ghostDecay` | 4 | Ghost decay divisor |
| `pirContacts` | 1 | Pirates show as contacts |
| `supplyLinePct` | 3000 | Supply intent: a station below this per 10,000 of capacity asks its house |
| `bidStepMin` | 1 | Posted-price step floor (1 = exact) |
| `bidStepPm` | 50 | Posted-price step, per mille of the bid (bids round down to it) |
| `reliefPct` | 2000 | Supply intent: one relief lot, per 10,000 of the asking station's capacity |
