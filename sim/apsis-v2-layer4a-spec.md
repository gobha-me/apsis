# Apsis logistics sandbox v2 — design spec (phase 1)

Nov 18, 2026 · @Jeff

v2 is a clean rebuild of the logistics sandbox in C++23, compiled to WebAssembly for the browser explorer. It becomes the seed of the game's real core. v0.20 stays frozen and runnable as the reference. Phase 2 is checked against this spec, not tick for tick against v0.20.

## Scope and settled owner decisions

- **Language:** C++23 native core; the same code compiled to WebAssembly drives the browser explorer.
- **Jumps:** systems have coordinates; one jump unit (J) is the typical spacing between star systems; drive rating is range in J; risk rises with length ÷ rating, scaled by ship mass; a lit beacon at the destination cuts it; tolls and logs bite on arrival; universe time per jump scales with distance.
- **Ships:** mass and space, no slots. Drive, tank, hold, hidden compartments, shields and weapons all compete for them. Shields and weapons have pre-combat roles now.
- **Stealth:** hidden compartments inside the hold. Only cargo in a compartment escapes a scan.
- **Logs:** read by interest, by a computable rule, with a target share of smuggling departures logged.
- **Jobs:** pilots stack jobs; a legal haul on the same route is cover.
- **Factions:** intents decided in parallel from a snapshot, settled by one clearing step, taking effect after a delay.
- **Exile:** docking-only, confirmed as a deliberate change from v0.20 (A10).
- **Process:** phase 2 is built in layers with gates at each (E5); the v0.20 harness stays runnable for same-day baselines.

**Owner review, 2026-11-18 (approved with changes):**

- the stock tank is chosen by a sweep, not picked (E6);
- refuelling away from home is paid in cash or with a fuel advance against the delivery;
- a priced rescue replaces the flat tow (B6);
- the Light hull has 50 space;
- refits are priced by part size;
- hull swaps keep the parts that fit;
- contraband departures always stand.
- **Owner decisions, 2026-11-20:** three true end states replace "a pilot is never stuck": death (lost in space when rescue can't arrive before life support runs out, or a fatal hazard), debt (10 years of continuous debt is bankruptcy) and prison (deferred; exile from every house leaves the independent stations as a rogue haven). End states are reported per style, not gated; a softlock (no move at all) is still a gate. Rescue keeps the B6 prices, with a crew leg that must push priced as a blind leg in range.

The sim stays tick-based (1 tick = 1 day) as the model of the economy and its consequences; player flight, UI and interactions will be real time (section C). Every rule has a switch; every number is a vetoable placeholder (section P).

## A. Settled rules that carry over

These are rules, not history. Each one is either kept as stated or changed where marked.

**Setting and pillars**

1. **Pillars:** Exploration and Building. Player arc: pilot for hire, then cargo hauler, then pathfinder.
2. **World:** the Core is the strongest faction and draws its power from freight. It owns the known-world lanes and beacons and tolls transits at its tollgates into the heartland. Sector houses surround it; beacons out in the sectors belong to whoever holds them. Minor houses, frontier stations, pirates and patrols exist as in v0.20. A Core that keeps its home is weakened, not fallen.
3. **Beacon jumps vs blind jumps:** a jump to a lit beacon is safer, tolled and logged; a blind jump is free, untolled and riskier.

**Styles and pay**

4. **Styles are modes of play, not balanced pay.** The test is whether each of the 11 styles (hauler, careful, bold, reckless, payer, double agent, forgetful, builder, farmer, pathfinder, crisis) supports itself through upgrades and building within the span on the flatter ladder: 2,500 / 5,000 / 7,500 for the first three refits.
5. **Mixed styles choose jobs only.** Habits belong to the pilot: paying warrants, risk limit, stealth choices, routing.

**Law**

6. **Getting caught hurts but doesn't punish.** Everything short of a true end state is recoverable, and a floor job is always available.
7. **Charges:** minor or serious. Each has a flat fine, capped at 600. A paid warrant still counts toward escalation. Grave exists only through escalation, and it stops at exile.
8. **Warrants:** minor warrants lapse after a set time; serious warrants stay on file.
9. **Heat is the active search** and fades while the pilot goes unseen. Warrants and wanted posters are the record: they stay on file and stop being actively hunted when heat is low. A patrol scan can still match them.
10. **Exile** covers one house's space and is enforced only by that house's patrols.
    - It is enforced **at docking only**. This is a deliberate change from v0.20, where taking the house's jobs or being scanned mid-offense also counted as a breach.
    - A breach is a serious charge plus a term restart, never a career ending.
    - Unclaimed space has no patrols; bounty hunters arrive with combat after 1.0.
11. **Ways out of a warrant:** pay the fine; inform (pays, then locks the pilot out of courier and spy work for a set time); or go into debt with garnishment.

**Exploration and fuel**

12. **Deep frontier pockets:** a stock ship can't reach them, and they carry scarcity pricing. Far pay applies only if scarcity isn't enough. Pockets drain and refill, so they stay worth revisiting.
13. **Stranding is** recoverable if rescue arrives in time (B6), at a cost in money, time and possibly debt. If it can't arrive before life support runs out, the pilot dies: one of the three true end states (owner, 2026-11-20).

**Additions: load-bearing pieces missing from the brief's list, and why**

14. **Idle twin.** Every career runs beside a world without the player. Consequences are measured as the difference.
15. **Exact ledgers.** Supply, raw goods and player money are conserved and checked every run; the gates rest on them.
16. **Input channel with replay and checkpoint.** A save is seed + action log, so this is a game requirement, not just a test. Same-tick inputs are applied in a canonical order.
17. **Exploration mechanics.** These are what the Exploration pillar actually is:
    - beacons that lapse when a house is strained, and relighting them;
    - dark systems;
    - hazards: gravity wells, nebulae, pulsars, rogue bodies;
    - charting, salvage, prospecting and expeditions.
18. **Running costs.** Upkeep, sub-light fuel as a running cost, drive wear and repairs.
19. **Job kinds.** Haul, courier package, spy, toll-dodge and floor job. Courier and spy unlock after a set number of deliveries.
20. **The recovery measure.** Worth counts upgrades at kept value; recovery is measured against a docked-and-free reference; the measure flags a pilot whose losses compound or who is stuck.

## B. New core rules

### B1. Jumps measured in jump units (switch: `jumps`)

One jump unit (J) is the median lane length of the map. Lanes join neighbouring systems; on the 7 curated seeds that length is 12–16 map units, median 14. v0.20's stock reach was 22 map units, about 1.6 J.

- **Fuel:** the tank holds N jumps. Every jump uses one, whatever its length. The tank refills only when docked, at any station, for cash or for a fuel advance taken out of the delivery's pay (C6). With no cash and no delivery aboard, fuel goes on debt, so a broke pilot is never stuck. The ship warns at the point of no return (B6).
- **Drive rating:** the drive's range in J. Ratings 0.5, 1.0 (standard), 1.5, 2.0 and 3.0. A 0.25 rating is not offered.
- **Mass scales the drive.** Effective range is the rating scaled by the reference mass over the ship's actual mass. A ship can push a jump to 1.5× its effective range, at rising risk.
- **Misjumps:** risk is flat up to effective range and climbs past it. A lit beacon at the destination halves the risk; a blind destination doubles it. A misjump uses the jump, lands off target in v0.20's arrival bands, and costs drive damage.
- **Bad landings:** hazard strikes on arrival (wells, rogue bodies, pulsar windows) work as in v0.20.
- **Time:** a jump takes universe time proportional to its length. A blind jump takes twice as long as a beacon jump.
- **Sub-light:** travel inside a system stays a flat running cost per jump leg.
- **Bots vs player:** bots weigh full travel cost. The player rule stays: X jumps, and the drive sets range and safety.

```latex
r_{\text{eff}} = \text{rating} \times \frac{M_{\text{ref}}}{M_{\text{ship}}}, \qquad x = \frac{L}{r_{\text{eff}}} \le 1.5
```

```latex
p_{\text{mis}} = \left(p_0 + k \cdot \max(0,\, x - 1)\right) \times \begin{cases} \tfrac12 & \text{lit beacon} \\ 2 & \text{blind} \end{cases}
```

### B2. Ships as mass and space (switch: `hulls`)

A hull has a space budget and a base mass. Every component takes space and adds mass, and the space budget is the only cap; there is no slot count. Cargo mass is not counted in v2.

| Part | Unit | Space | Mass | Effect |
| --- | --- | --- | --- | --- |
| Light hull | — | 50 budget | 30 | Small and fast |
| Medium hull (stock) | — | 120 budget | 40 | Starting ship |
| Heavy hull | — | 160 budget | 80 | Big hold, needs a big drive |
| Drive 0.5 / 1.0 / 1.5 / 2.0 / 3.0 | each | 4 / 8 / 14 / 22 / 40 | 4 / 10 / 18 / 28 / 50 | Range in J |
| Tank | 1 jump | 3 | 2 | One jump between docks |
| Hold | 10 units | 8 | 3 | Cargo |
| Hidden compartment | 10 units | 16 | 4 | Cargo inside escapes a scan |
| Shield | 1 point | 6 | 6 | −20% misjump and hazard damage, at most 3 points |
| Weapon | 1 point | 6 | 8 | +100‰ on the resist roll vs pirates and rogue bodies, at most 3 points |

**Example builds:**

- **Stock (Medium):** drive 1.5, hold 80, tank 6 (chosen by the sweep in E6). Space 96 of 120, mass 94, so effective range 1.59 J; with tank 3 it's 1.70 J. v0.20's stock reach was 1.4–1.8 J. Changed in layer 3 so the opening works (E6).
- **Redneck (Light):** drive 2.0, 4 jumps, and either 20 hold or one 10-unit compartment (space 50 of 50). With the compartment: mass 70, so effective range 2.9 J. It crosses the Core and can run courier and spy work, with no shields and no guns.
- **Freighter (Heavy):** drive 2.0, 6 jumps, hold 100. Mass 150, so effective range 1.33 J. An ordinary jump needs a big drive.

**Refits.** A refit buys one part: a drive rating, +2 jumps, +10 hold, +10 compartment, a shield point, a weapon point, or a hull. The price follows the flatter ladder by part size: up to 8 space costs 2,500; 9–16 space, 5,000; 17 or more space, and any hull, 7,500. A drive is priced by the new drive's size. The span's three steps become cumulative refit spend: 2,500, 7,500 and 15,000. On a hull swap, parts that fit the new hull carry over. The pilot chooses which if not all fit; bots follow their style's order. Parts left behind are lost; there is no resale in v2.

**Pre-combat roles.** Shields reduce damage that already exists. Weapons use the resist-roll seam against pirate encounters and rogue-body strikes. Resisting a lawful patrol stays out until combat, because it would be a charge. If either earns nothing in testing, the report says so; neither is tuned to pass.

### B3. Logs read by interest (switch: `interest`)

Each departure from a station owned by O, on a job issued by I and bound for a destination owned by D, is classed by a lookup on relations with O:

- **Ally:** treaty or patron, or relation at or above the ally threshold.
- **Rival:** at war, or relation at or below the contest threshold.
- **Neutral:** everything else.

| Case | Rule | Departure heat |
| --- | --- | --- |
| Own mission | I = O | None to others; the log is O's to keep or lose |
| Tolerated | I ≠ O, D is O or O's ally, legal cargo | 25% |
| Stands | Anything else, including all contraband | 100% |
| Odd | A rival's mission and no cargo of that job aboard | 25%, never a charge (could be exploring) |

- **Lost logs:** a friendly station manager (5+ deliveries to that owner) or a discontent station loses a standing log with 500‰ chance.
- **Smuggling and spy jobs are issued by parties other than the station owner by construction.** This is the fix for v0.20, where 103 of about 109 bold departures per career were the owner's own jobs and went unlogged.
- **Arrival with no origin:** suspicion is proportional to depth, measured in J from the nearest unclaimed system.
- **Target:** 25–50% of smuggling departures logged (v0.20: about 4%).

**Heat from a run** is the sum of its trace: departure log, arrival suspicion and anything a scan finds. Cargo in hidden compartments is never found. Contraband departures always stand at 100%. Compartments beat scans only; a log is beaten only by a rendezvous, cover or a lost log. The v0.20 stealth extras (faster heat fade, quieter arrival) are dropped, so stealth has one testable role.

**Rendezvous** are offered when a direct run would leave a standing departure log or arrive 2 J or more deep.

- **Meet:** a point in empty space within 1 J of the destination's border, with a wait window of 10 days.
- **Trace:** no log at the meet.
- **Pay:** the direct pay, plus the extra legs (jumps × fuel price + days × the reference daily rate), plus a 25% premium.
- **A missed meet fails the job,** with no charge.

### B4. Stacked jobs (switch: `stack`)

A pilot carries a manifest of any number of jobs, limited only by hold, route and time:

- legal cargo fits the hold;
- contraband fits the hold or the compartments, and only compartment cargo hides;
- every destination lies on one flight plan;
- every deadline is met.

**Cover:** a legal haul whose route shares the departure and a destination with a contraband job halves that job's departure heat and the scan odds on the shared legs.

**Seizure:** if caught, the contraband is seized and the charge applies. Legal jobs continue.

**Bots:** a bot packs by exhaustive search over up to 3 jobs from the board (at most 220 subsets on a 12-job board), then fills the remaining hold greedily.

### B5. Faction intents and one clearing step (switch: `intents`)

Each faction decides per resource and per relationship (each ally and rival), from a snapshot of the world, every 30 days. Factions are staggered by a hash of their id, so decisions don't all land on one day.

An intent is: faction, kind, object, amount, priority, bid, and the day it takes effect. The kinds are: move supply, buy or sell a contract, build or relight a beacon, assign patrols, set a toll, contest a junction or station, and change stance toward a house (treaty, embargo, war or peace). Each kind has a delay (placeholders in section P), so nothing reacts within the same tick. Layer 2 added two concrete rules. A supply request is one relief lot (20% of the asking station's capacity), and a station asks only below 30% stock. Bids on indivisible objects are posted prices that round down to a step of 5% of the bid, with a floor that is a parameter.

At the end of each tick, intents due that day clear by object:

1. **Inside a faction:** by priority, ties broken by hash.
2. **Between factions, divisible objects** (market supply, contract volume): pro rata by bid.
3. **Between factions, indivisible objects** (a station, a junction, a contract award): highest bid wins, ties broken by hash.
4. An intent that doesn't clear is dropped, and the faction decides again next cycle.

This replaces the brief's fixed seeded order. One mechanism covers both cases. No faction gains from acting first. And because the decisions are pure functions of the snapshot and the clearing is a sorted serial step, the result is the same at any thread count.

### B6. Stranding and rescue (switch: `rescue`)

Rescue is a real operation with risk to its crew, and it is priced that way. It replaces v0.20's flat tow.

- Life support is a ship system (owner, 2026-11-20). Its days come from the hull (Light 60, Medium 90, Heavy 120), plus 50% where decks can be cut off (Medium and Heavy), refit points, and the flight suit and seat. Misjumps, bad landings and pirates damage it; shields cut that; a dock repairs it. A rescue that can't arrive in time means death.
- **Point of no return:** the ship warns when the jumps left equal the jumps needed to reach the nearest dock on the planned route. Bots treat it as a stop unless their plan refuels.
- **Distress beacon:** response is fast within 1 J of a lane or lit beacon, and much slower deep in the black: 10 days, plus 30 days per blind jump out.
- **Price:** the crew's jumps out and back, each leg priced by its own misjump risk, plus the tow and the universe time.
- **Paying:** from the wallet first, then as debt with garnishment (A11). Four or more beaconless jumps out should cost several ladder steps.
- **Debt as an end state (owner, 2026-11-20):** 10 years of continuous debt (all debt, one clock, reset only at zero) is bankruptcy and ends the career. The pilot is warned at 5, 8 and 9 years and can repay from the wallet at any time. A crew leg that must push past its range is priced as a blind leg in range (at most 1,500).

```latex
\text{rescue} = \sum_{\text{legs out and back}} c_{\text{leg}} \left(1 + \frac{p_{\text{mis,leg}}}{10\text{‰}}\right) + \text{tow}
```

At the placeholders, a beacon leg (10‰) costs 600 and a blind leg (40‰) costs 1,500. Four blind jumps out come to about 12,500 with the tow.

## C. Real time vs tick

The sim keeps a 1-day tick, but nothing in v2 may need a whole trip or a whole month to resolve at once. Every player-facing step is an event the real-time game can replay one at a time.

| # | v0.20 turn-based assumption | How v2 models it |
| --- | --- | --- |
| C1 | Monthly clock: upkeep, heat fade, exile terms, garnishment and the pocket drain land on the 30th | Daily rates with integer remainders carried forward. No 30-day batching anywhere in pilot or law state. Faction decisions stay on 30-day cycles, staggered (B5). |
| C2 | Same-tick actions sorted into a canonical order | Kept for the sim (gate). The game uses the same order as a server-authoritative timeline. |
| C3 | A trip is one decision: route, tolls, fuel, misjumps and strandings fixed at departure | A trip is a flight plan of jump legs. Each leg resolves on arrival: misjump, hazard, toll, log, scan. The pilot or bot can re-plan, turn back or abort at any arrival. Stranding happens when the tank is empty off a dock. |
| C4 | Encounters (scans, stops) resolve instantly | An encounter is an event with a response: comply, or run (blind jump out, if there's fuel). Resist arrives with combat. Bots answer at once; the game gives the player a window. |
| C5 | Rendezvous as a wait counter | The meet is a scheduled event at a point in space with a time window. The contact is an NPC craft with a position and an arrival time; missing the window fails the job. |
| C6 | Instant refuelling, and beacon routes topping up at every lit system | Refuelling is a dock action. It takes dock time and leaves a log at that station. Passing through a lit system refuels nothing. |
| C7 | Arrival with no origin judged from the whole blind trip | Each arrival is judged from the pilot's last logged position. A station sees "no origin" when that position isn't the previous leg's system. |
| C8 | Bots read the job board as a daily snapshot | Jobs are posted and expire as timed events. Bots read the board when they dock; the game shows it live. |
| C9 | Universe time spent per trip | Universe time is spent per jump leg, by its length (B1). Dock time and waits are separate events. |

## D. Architecture

v2 is a C++23 library with a thin native runner for experiments and a WebAssembly build for the browser explorer. The same source and the same seed give the same hash on both.

&#91;embedded content: one tick · parallel decide, serial clear and apply\]

Everything that reads the world in a tick reads one snapshot, so it can run on any number of threads. Everything that writes runs in one sorted order.

### Modules

Each module owns its state and its rules. It reads other modules through const views and writes only in the apply step. Each module can be tested alone and replaced alone.

| Module | Owns |
| --- | --- |
| core | Hash RNG, fixed-point maths, ledgers, checkpoint and restore, the input channel, the rules and parameters table |
| world | Map, coordinates and J, hazards, beacon sites, pocket geometry |
| economy | Production, stocks, markets, freighters, pocket stocks, pricing |
| factions | Relations, intents and clearing, tolls, patrols, contests, beacon upkeep |
| law | Heat, warrants, charges, fines, exile, logs, scans |
| ships | Hulls and components, the jump model, wear and repairs, fuel, tows |
| jobs | Board, job kinds, interest class, rendezvous, payouts |
| pilots | Player state and manifest, bots, styles, mixes |
| measure | Idle twin, recovery watcher, summaries, report data |
| explorer | WebAssembly bindings and the existing HTML explorer UI |

### Determinism

- **State is integers only:** 64-bit integers and per-mille fixed point. Floats are allowed in the UI only.
- **The RNG is counter-based:** a hash of seed, stream, day, entity and purpose (a port of v0.20's hashN). A draw never depends on how many draws came before it, so thread count and call order can't change it.
- **No hash-map iteration and no unstable sorts on any state path.** Every sort key is total.
- **Caches live outside state.** They are keyed by day and their inputs, and never change the hash. v0.20's legs and haul caches were built this way.
- **Gates:** the same hash at 1, 2 and 8 threads, and native = WebAssembly.

### Switches and placeholders

One table defines every rule switch and every number: name, default, unit, meaning and version. The C++ parameter struct, the README table and the report table are all generated from it, so they can't drift.

### Performance

Reference, same machine, 2026-11-18: v0.20 in Node runs 41.8 sim-years a second, which is 34.4 ms per flying pilot-year.

- **Goal:** at most 34 ms per flying pilot-year in the browser (WebAssembly), and at most 5 ms native on one thread. World-only and pilot cost are reported separately.
- **Data layout:** state stored as arrays per field, not per object.
- **Per-day caches:** the jump graph for each effective-range profile, the job board per station, and route costs.
- **Parallel work:** faction decisions run in parallel, and so do experiment branches, which are independent.
- **Profiling:** a profile is taken at each layer's gate (E5).

## E. Acceptance tests for phase 2

All tests run on 7 curated seeds × 3 starts × 40-year careers (21 careers per style), with an ablation for every new rule. v0.20 numbers are the reference; a threshold is what v2 must meet.

### E1. Frozen style intents

The bots may be rebuilt, but each style's intent may not be re-tuned to pass a test.

| Style | Intent |
| --- | --- |
| hauler | Legal freight only; never carries contraband |
| careful | Packages and smuggling inside a low risk limit; avoids known patrols |
| bold | Packages and smuggling at a higher risk limit; backs off a house that warned it |
| reckless | Takes any profitable job; runs from scans |
| payer | Like bold, but pays every fine at once |
| double agent | Spy work for any house; sells either side out when it pays |
| forgetful | Like careful, but leaves warrants unpaid and forgets them |
| builder | Relights dark beacons a house will buy back, salvages the abandoned ones, hauls in between |
| farmer | Builds and keeps system beacons for the tolls, hauls in between |
| pathfinder | Explores, and sells charts and beacon sites |
| crisis | Takes emergency and blockade jobs first |

### E2. Must reproduce or beat

| Test | v0.20 reference | v2 threshold |
| --- | --- | --- |
| Styles in the span | 11 of 11 | 11 of 11: at least 19 of 21 careers reach the third step (cumulative refit spend 15,000); each median step at most 10 years after the previous |
| Softlocked pilots (no move at all) | 0 | 0 |
| Stranding | 3.4% of deep runs, all towed | Every stranding rescued unless life support runs out first; 1–8% of deep runs. Rescue cost and debt reported per style; debt older than 3 years flagged (debt is an end state, not a gate) |
| Career endings | 0 by default; 1 under the v0.19 exile rule | Reported per style: death, debt (prison deferred); a style above about 5% is flagged |
| Recovery, median days (reckless) | minor 242, serious 260, grave 286 | No worse; minor at most serious for every style. v0.20 failed this for forgetful: 332 vs 305 |
| General war within 40 years | burner 5/21, idle 4/21 | Each within ±2 of v0.20 |
| Stable Core goes to war | 0 of 3 | 0 |
| Frontier ambiguity: alone at a frontier contact | careful 18.4%, bold 19.0% | 12–25% |
| Speed per flying pilot-year | 34.4 ms (Node, same day) | At most 34 ms in WebAssembly, at most 5 ms native; world-only reported separately |
| Standing gates | All pass | Input order, replay, checkpoint, exact ledgers, ways out of a warrant, identical hash at 1, 2 and 8 threads, native = WebAssembly |

### E3. Must show what v0.20 couldn't

| Test | v0.20 | v2 threshold |
| --- | --- | --- |
| Stealth earns | Dropping stealth raised bold from 1,468 to 1,787 net a year | For bold, reckless and double agent, the best build has a hidden compartment and nets at least 10% more than the same build without |
| Builds differ | One build best for 10 of 11 styles | No build best for more than 4 styles; at least 3 distinct best builds |
| Shields and weapons | Not modelled | Reported per style. Pass = fitted in some style's best build, or reported plainly as earning nothing (not tuned) |
| Stacked jobs used | Not possible | At least 25% of laden trips carry 2+ jobs for the trading styles; the no-stacking ablation lowers net for at least 6 styles |
| Cover works | Not possible | Contraband runs with cover leave at least 30% less heat per run than runs without |
| Logs make tradeoffs | About 4% of smuggling departures logged | 25–50% logged. Turning the interest rule off moves smugglers' net by at least 5%. An empty departure on a rival's mission never brings a charge |
| Rendezvous pay | Removing them raised bold from 1,468 to 1,809 | Net per day on rendezvous jobs at least that of the direct alternative, for every style that takes them |
| Explorers reach pockets | Pathfinder: 0 deep runs | Pathfinder reaches a pocket in at least 15 of 21 careers and earns at least 15% of its net there from charting, beacons and hazard data |

### E4. Ablations

One switch off per branch, all 11 styles:

- `jumps`: the v0.20 hop model;
- `hulls`: fixed v0.20 upgrades;
- `interest`: every departure logged as "stands";
- `stack`: one job at a time;
- `intents`: the v0.20 faction cycle, ported;
- shields off; weapons off; cover off; rendezvous off; pockets off; far pay off.

### E6. Tank sweep and the opening

The stock tank is tested at 3, 4 and 6 jumps, and the default is chosen from the results.

**The intended opening:**

1. Short explorations of nearby bodies, with no jump.
2. A dark system, out and back.
3. Then the board opens outward.

Anything is possible from day one. For their first 360 days, new pilots see only posted jobs to lit systems within effective range.

**Reported per tank size:**

- **Pay:** net per year and years to each step.
- **Early-game feel:** days to the first nearby-body exploration; days to the first dark-system out-and-back; first-year jobs by distance; first-year strandings and rescue cost; the share of year one spent waiting docked.

**Layer 3 result (2026-11-18, 189 careers, vetoable):**

- **The stock tank stays at 6.** It gives the fewest strandings (0–0.06 per 1,000 legs), the dark run in year 1 in every career, and the best range. Tank 3 pays haulers 44% more but strands 6–7× as often.
- **The stock ship was changed, not the opening.** Drive 1.5 (was 1.0), Medium hull 120 space / 40 mass (was 100 / 50), Heavy mass 80 (was 110).
- **Range.** 83 of 87 homes can do a dark out-and-back on one tank without passing effective range. The 4 that can't are on maps with a single dark system.
- **Early game, tank 6:** first body survey on day 1, first dark out-and-back on day 22 (medians), no year-1 strandings, no time docked waiting for work.
- **Rescue debt** clears in at most 2.94 years (p90, crisis at tank 3); no style passes the 3-year flag.
- **New placeholders:** `bodyPay` 150, `surveyDays` 3, `chartPrice` 600, `newPilotDays` 360.

### E5. Build in layers, gated

Each layer must pass its gates before the next one starts.

1. **Core.** RNG, fixed point, ledgers, checkpoint, input channel. *Gate:* replay, checkpoint, identical hash across thread counts, native = WebAssembly.
2. **World, economy and factions; no player.** *Gate:* the world tests in E2 (general war, Core stability, frontier ambiguity), exact ledgers, world-only speed.
3. **Ships, jumps and fuel**, plus hauling, the floor job and the trading styles. *Gate:* span for hauler and crisis, stuck 0, strandings all towed.
4. **Law and logs.** Heat, warrants, exile, interest logs, rendezvous, ways out. *Gate:* recovery, career endings, the log tests.
5. **Stacked jobs, cover and full builds.** All 11 styles and mixes. *Gate:* all of E2–E4.

The v0.20 harness stays runnable, so every speed figure is measured against v0.20 on the same day.

## P. Placeholders (all vetoable)

Every v0.20 value carries over unless this table replaces it. The v0.20 values include costs, heat thresholds, tolls, hazards, faction strength and the economy. The component space and mass figures are the table in B2. In phase 2, this table and its generated copies are the single source.

| Name | Default | Meaning | From |
| --- | --- | --- | --- |
| `jumpUnit` | median lane length | 1 J, in map units (12–16 on the curated seeds, median 14) | new |
| `overreach` | 1.5× | Longest jump allowed, as a multiple of effective range | new |
| `misBase`, `misSlope` | 20‰, 400‰ per 1.0 over | Misjump chance up to effective range, and the rise past it (220‰ at 1.5×) | new |
| `beaconRisk`, `blindRisk` | ×½, ×2 | Misjump multiplier at a lit beacon / at a blind destination | new |
| `daysPerJ`, blind time | 2 days, ×2 | Universe time per J of jump length | new / v0.20 |
| `subLight` | 2 per jump leg | Sub-light running cost | v0.20 |
| `massRef` | 100 | Reference mass (the stock ship as fitted) | new |
| Stock ship | Medium hull, drive 1.5, hold 80, tank 6 (swept at 3 / 4 / 6 in layer 3) | Starting build | new |
| `refitPrice` | ≤8 space 2,500; 9–16 space 5,000; 17+ space and hulls 7,500 | Refit price by part size; span steps are cumulative spend of 2,500 / 7,500 / 15,000 | owner |
| `shieldCut`, `weaponResist` | 20% / 100‰ per point, at most 3 | Damage cut / resist bonus | new |
| `refuel` | 10 a jump in cash, or an advance of 10 + 25% taken from the delivery's pay; debt if neither | Refuelling at a dock | new |
| `rescue` | crew 300 a leg × (1 + misjump‰ ÷ 10), a pushed crew leg priced at 40‰; tow 500; response 10 days + 30 per blind jump out; bankruptcy after 10 years of continuous debt | Rescue (B6) | v0.20 |
| `heatDepart` | 40 | Heat from a standing departure log | v0.20 |
| `tolerated`, `odd` | 25%, 25% | Share of departure heat for those cases | new |
| `heatDepth` | 15 per J | Arrival with no origin, per J of depth | v0.20 (was per hop) |
| `logLostPm`, `friendDel` | 500‰, 5 deliveries | Lost-log chance; deliveries before a manager is friendly | v0.20 |
| `allyRel`, `rivalRel` | +2,000, −1,000 or at war | Relation thresholds for the interest lookup | new |
| Logged target | 25–50% | Smuggling departures logged | new |
| `meetDepth`, `meetRange`, `meetWait` | 2 J, 1 J, 10 days | Rendezvous offer depth, distance from the border, wait window | new / v0.20 |
| `meetPremium` | 25% | Rendezvous pay on top of the extra legs | new |
| `coverCut` | ×½ | Departure heat and scan odds with cover | new |
| `packDepth`, board size | 3 jobs, 12 jobs | Exhaustive packing depth for bots; jobs per station board | new |
| `minorLapse` | 720 days unseen | Minor warrants lapse | new |
| `fineCap`, `exileDays` | 600, 1,800 days | Flat fine cap; exile term | v0.18 |
| `jailDays`, `deportDays` | 180, 30 | Jail and deportation | v0.17 |
| `informPay`, `informLock`, `garnish` | 800, 3,600 days, 25% | Ways out of a warrant | v0.13–v0.17 |
| `pockets`, distance | 2 per map, 2 jumps of about 1.9 J past the farthest frontier | Out of stock reach even when pushing to 1.5× | v0.20 (in J) |
| `pocketCap`, drain | 80 per good, 10% a month as a daily rate | Pocket market size and drain | v0.20 |
| Pocket charts, hazard data | 1,200 a chart, 300 a hazard | Non-cargo value for explorers | new |
| `farPayPct` | +50%, pockets only | On only while scarcity alone doesn't pay the trip | v0.20 |
| `factionCycle` | 30 days, staggered by hash | How often a faction decides | new |
| Intent delays | strategic orders (contest, fortify, cut off, expand) 10 (layer 2: 30 halved conflict on 200 seeds); supply 10, patrols 10, toll 30, contract 30, stance and patron 30, beacon 60 days | Days from decision to effect | new |

## F. Owner questions (resolved)

All four were settled at the review on 2026-11-18.

1. **The size of J and the stock tank:** J is the median lane length; the tank is chosen by the sweep in E6.
2. **Cargo mass:** out of v2.
3. **End states:** nothing ends a career before combat.
4. **Stealth's v0.20 extras:** dropped.

Phase 2 has started at layer 1 (E5).
