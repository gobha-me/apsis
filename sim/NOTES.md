# Working notes (v2), for continuity
## Layer 2 follow-ups (owner review 2026-11-18 evening)
1. War gate: 1,000 seeds, rate ratio on/off within 0.75-1.25 OR 95% CIs overlap. Keep delayStrategic 10.
2. Core stability vs v0.20 on the same 1,000 seeds.
3. Supply: fed rate and station failure rate, off (v0.20) vs on, same seeds; explain any drop.
4. Confirm month-38 divergence on 110478 was only the prospector port.
6. Threads: default single-threaded; pool = tested option; parallel experiment branches = speed plan.
## Layer 3 (then)
- ships (hulls/parts by mass+space), jumps in J, misjump risk, beacons lit/blind, fuel tank (jumps), refuel cash/advance/debt,
  rescue (priced by legs x risk, distress beacon speed, debt + garnish), point of no return, hauling, floor job, refits by part size.
- tank sweep 3/4/6 + early-game feel; opening = nearby bodies (no jump) -> one dark system out and back -> board opens.
- 5: per map, share of home stations with a dark system within stock range; how often stock ship can do the dark out-and-back without
  pushing past effective range. If not reliable: adjust stock drive/hull, not the opening.
- rescue debt clearing: median/p90/max per style, flag p90 > ~3 years.
- 7: contested bids again once pilots bid.
## Layer 2 follow-up results (done)
- prospector confirm: re-adding it makes all 7 seeds diverge (257/38/245/257/92/54/98); at each first divergence only ds/kp differ (+ghost on 110478, same month). Removed -> all match.
- 1,000 seeds (world_wide): war off 7.3% [5.8,9.1], on 6.7% [5.3,8.4] (ratio 0.92, CIs overlap) PASS. Core weakened 26.0 -> 22.8, home lost 0.9 -> 0.7, fell 0.9 -> 0.7,
  stable-start Cores at war 4.6 -> 4.1. Supply dips back above line within 30 d: 77.4 -> 83.6; failures 0.001/station-yr both; stage2+ 0.01% both.
- spot check: 10/10 of the 1,000 seeds match v0.20 every month for 60 years (spot_l2.txt).
## Layer 3 design (pilot), KISS
- v0.20 pilots start at the Core home (net.core.home = station 0). Styles this layer: hauler, crisis (gate), pathfinder (opening/charts; expeditions+pockets -> layer 5).
- Jumps: pilot jumps between any two systems within effective range (rEff = rating*massRef/mass, in J; push to 1.5x). Leg risk per spec B1; lit dest = bk.ds==0.
  Misjump: fuel spent, days spent, damage, stay at origin, retry/divert. Toll on arrival at a lit tollgate (junction) when carrying. Leg days = ceil(daysPerJ*L/J), blind x2.
- Fuel: refill at any non-abandoned station: cash, else advance from delivery (+25%), else debt. Point of no return: never plan legs > fuel; destination must be dockable.
- Rescue: stranded (fuel 0, undocked) -> nearest dockable station sends crew (standard ship 1.0 J): price = sum over crew legs out+back crew*(1+p/10pm) + tow; response 10 d
  (+30 per blind crew leg if not within 1 J of lit space). Wallet then debt to rescuer's owner; garnish 25% of income. Episode clear time recorded.
- Refit price by part size (<=8: 2.5k, 9-16: 5k, 17+/hull: 7.5k); hull swap carries parts that fit. Span steps = cumulative refit spend 2.5k/7.5k/15k.
- Jobs: emergency, blockade (cargo-loss risk 60pm at contested dest), floor. New pilots (first 360 days) see only jobs within effective range to lit systems.
- Opening: SURVEY home bodies (no jump, 3 d, pays bodyPay from a great house), then a dark out-and-back within effective range (chart, sell 600), then the board.
## Layer 3 results (done, 2026-11-18)
- Stock ship final: drive 1.5, Medium 120 space / 40 mass, Heavy mass 80, tank 6 -> rEff 1.59 J (tank 3: 1.70). Range: out-and-back 83/87 homes at tank 6; the 4 misses are maps with 1 dark system.
  Tried drive 2.0 @ mass 50: 84/87 but kills the drive refit step; drive 3.0: 87/87. Kept the hull change.
- Sweep: all span PASS; tank 6 recommended (fewest strandings, 21/21 year-1 dark runs); tank 3 pays haulers +44% but 6-7x strandings.
- Debt p90: hauler 2.31, crisis 2.94 (tank-3 cases), pathfinder 1.15 y. No flag. 16 debts / 95 rescues.
- Fixed: thread_local toll cache -> World cache (wasm link); strict checkpoint reader (bool/int/char/dup key) -> teeth 20/20.
- Switches: rescue/shields/weapons wired (pilot_gates switches); weapons 0/15 teeth (rogue zones only); jumps/hulls off not built; pockets/farpay -> layer 5.
- gates_l2.sh: curated war +/-2 gate replaced by the 1,000-seed ratio/CI gate (PASS 0.92).
- Next: layer 4 (law & logs, remaining styles, stances, frontier ambiguity) after owner approval.

## Layer-3 review follow-ups (owner approved layer 3 on 2026-11-20; notes 1-8)
1. Unrecovered dips (world_wide_l3.txt, 1,000 seeds): v0.20 4,507 slow dips (22.5%), lowest stock mean 15.2% of cap, deepest tenth <=6.7%, hit empty 2.9% (131), 57 days below line;
   intents on 2,925 (16.3%), 16.2%, <=6.8%, 3.8% (111), 51 days. Recovered dips low mean 25.1 / 25.2%. Not worse.
2. Wide rescue-debt run (pilot_exp 40 N, random seeds hashN(20261120,i), start 20/80/140): with rescueRiskDivPm 10 (owner value), 300 seeds: 113 debts, p90 hauler 9.27 y, crisis 3.44, pathfinder 10.32, max 29 y, 10 "stuck" (debt > 10 y).
   Cause: crew leg multiplier 1 + pm/10 with v2 blind pushed legs at 408 pm -> x42 per leg, single rescues up to 27,500 -> debt trap (empty wallet, no cargo money).
   Tried: crew ship 1.6 J (p90 path 5.3), divisor 100 (path 3.3), both (path 2.7). Adopted divisor 100 (vetoable, owner value changed). 450 seeds: 131 debts; p90 hauler 1.68, crisis 1.60 (tank 3 1.60), pathfinder 2.50 (tank 4 3.29 FLAG). open debts counted as lower bounds. stuck 0.
   Added rescueCrewPm param (1000), setParam() + APSIS_SET=name=v,... what-if override in pilot_exp. Files: pilot_wide.json (adopted), pilot_wide_base.json (div 10), variants.
3. README: tank-3 hauler +44% = balance lever, not settled.  4. in-flight PoNR warning in layer 4.  5. jumps/hulls stay "not built".  6. weapons-off teeth gate in layer 4.  7. contested bids recheck when pilots bid.  8. don't chase pathfinder speed unless run > 2x layer 3.
Scope: split into 4a (law, logs, pirates, weapons gate) / 4b (styles, stances, frontier) if a gate is still red halfway.

## Owner decisions 2026-11-20 (design chat) — supersede spec where they differ
- "Stuck" (spec B6/E2 10-year rule) was MY phase-1 wording, not the owner's. Retired as a gate.
- THREE END STATES (true end states, reported per style, not gated; flag a style > ~5%):
  1. DEATH: (a) lost in space = rescue can't arrive before life support runs out; (b) hazard strikes can be fatal (small fatalPm, shields cut it).
     Life support is a SYSTEM: days scale with hull (placeholders Light 60, Medium 90, Heavy 120), suit+seat reserve (+10 d, TBD), deck cut-off hull trait
     (Medium/Heavy, +50%), optional LS refit (space/mass), LS health damaged by misjumps/hazards (shields cut), repaired at dock. Replaces B6 "wait is survivable".
  2. DEBT: bankruptcy = 10 years of continuous debt (one clock, all debt; resets only at zero). Warnings at 5/8/9 y. REPAY action (bots repay cash over a reserve).
  3. PRISON: deferred. Exile from all houses does NOT end the game: independent stations (owner 2, 3-17 per map, no patrols, no exile) are the rogue haven.
     Owner+Grok(Luna) compromise stands: breach = serious + term restart, never an ending.
- Softlock (no action, idle > 20 days) stays a GATE (bug check).
- Rescue price: option B (B6 prices, divisor 10; crew-leg risk capped at rescueRiskCapPm 40 = blind in-range). In code now.
- NEXT STEP AFTER 4a (one step): jump BUBBLES green/yellow/red. Green fixed ~0.05 J capture; outside green = misjump. Yellow ~0.15*L healthy lit in-range,
  grows with push/blind/wear/mass; red 2-3x yellow, capped 1x L healthy, 2x L dead drive; ~80% misses yellow / 20% red, red share grows with factors.
  Landing = nearest system to the point (may be dark/uncharted), distance paid in sub-light days (no jumping from deep space, KISS).
  Plotting skill shrinks yellow/red within a cap; optional jump mini-game, auto-skip for low risk = median result. Bot styles fixed skill; test worst/median/best.
  Plus: in-flight PoNR warning with direct-vs-detour choice (bots weigh misjump x beyond-rescue-in-time vs detour by style risk limit),
  and bots plan only on what the pilot knows (truth / house charts / pilot charts). Core must not assume a closed map (verse is infinite, procedural).
- Order: 4a = end states, then law & logs (+ pirates, weapons-off gate). Then the bubble step. 4b = styles, stances, frontier.

## Layer 4a progress
- DONE end states (code): Ship.ls (P_LS refit), Pilot lsh/end/endDay/debtFrom/debtWarned/ev; lsDays(); rescue -> death if resp+outbound > lsDays (rule lifesupport);
  misjump/hazard damage LS (shields cut); fatal bad landing fatalPm 2 (rule hazdeath; 20 killed 17%); bankruptcy at 10 y continuous debt, warnings 5/8/9 (rule bankrupt);
  A_REPAY=7 (bots repay wallet over refitReserve); repair covers LS (repairLsFull 600, bots at repairLsAt 700).
  pilot_exp: careers stop at end; "stuck" now = softlock (maxIdle>20); longDebt kept; endState/endDay/bad in JSON.
  Wide 300 seeds: end states 0-3% per cell (hazard 0-5/300, lost 2 pathfinder t3, bankrupt 2 pathfinder); softlock 0; ledgers exact.
  Gates: ./build/pilot_gates endstates 9/9 (pilot_endstates_l4.json); gates 4/4; switches 3/3.
- NEXT: law & logs (spec A law, B3, rendezvous, C4/C7), pirates vs pilots + weapons-off teeth gate, law styles (careful/bold/reckless/payer/forgetful/double agent).
### Layer 4a law design (as built; interpretations marked *vetoable)
- Jobs: kind 3 courier (package), 4 spy (papers). Issuer = a great house other than the origin owner (B3 by construction). Unlock: courier after courierUnlock deliveries,
  spy after spyUnlock; inform locks both for informLockDays. Package/papers hide only with a compartment (comp >= 10). Haul-type jobs: issuer = destination owner; own trade = own mission*.
- Departure log (B3) at startTrip from a station owned by O: own (I=O) 0; tolerated (I!=O, D is O or O's ally, legal) 25%; stands 100% (incl. all contraband); odd (rival's mission,
  none of that job's cargo aboard) 25% but never evidence. Lost log: friendly manager (5+ deliveries to O) or discontent station (mood < discontentMood): 500 per mille.
  Heat per log: heatLegal 10, heatPkg 120, heatSpy 150 x class. interest off = every departure stands.
- Heat = active search per house (milli-units, daily fade heatFadePm with remainder carry, C1). Tiers: noticed >= heatT1, watched >= heatT2 -> scan odds x2 / x4.
  Warrant only with EVIDENCE*: a standing contraband log or a scan finding or a run, and heat >= heatWarrant. Odd and legal logs are never evidence.
- Arrival suspicion (C7): arriving at a station of h from a system that isn't the last logged position adds heatArrPerJ x depth (J from the nearest unclaimed system).
- Scans (C4) on arriving in a system of house h: scanPm x psOf(h)/100 x tier mult. Wanted (warrant on file) -> served; contraband not hidden -> serious charge, seized.
  Response: comply or run (blind jump out if fuel; escape runEscapePm; escape = heat + evidence; caught = charge one step up).
- Docking at h: exiled -> breach = serious charge + exile term restart (never an ending); warrant on file -> served.
- Charges: minor fine 300, serious fine 600 + jail jailDays + seizure + job failed; cap 600. Each warrant/charge counts toward escalation (paid ones too);
  graveAt-th offence with h = grave -> exile from h for exileDays (docking only), stops there. Minor warrants lapse after minorLapseDays; serious stay on file.
- Ways out (A_LAW f0: 0 pay, 1 inform, 2 debt; f1 house). Served warrant: bot policy resolves on the spot.
- Rendezvous*: offered on courier/spy jobs whose direct run would stand-log or arrive >= 2 J deep: delivery at a meet = a system within 1 J of the destination (no docking:
  no arrival suspicion, no dock check), window meetWindow 10 days, pay = direct + extra legs (jumps x refuel price + days x refDaily) + 25%. Missed meet = failed, no charge.
  The departure log still stands (a meet beats the arrival trace; cover comes with stacking in layer 5)*.
- Pirates vs pilots: arriving at a system on a route with pirate groups, carrying cargo or a package: pirEncPm x strength/pirGroup (cap pirEncMaxPm).
  Resist roll resistBasePm + weapons x weaponResistPm (rule weapons). Fail = cargo looted (ploss), damage to drive and LS (shields cut).
- Recovery (E2): per charge, days until free (not jailed, no warrant with that house) and worth >= worth docked before the trip.
- Law styles: careful, bold, reckless, payer, forgetful, double agent (E1 intents).
- DONE law module (src/pilot/law.cpp): logs B3, heat/warrants/charges/exile/breach, scans + run, dock check, arrival suspicion C7, ways out (A_LAW 8), A_POLICY 9,
  courier/spy jobs (underJobs) with rendezvous variants (id ^ 0x4d454554), settleUnder, recovery watch, pirates vs cargo carriers (resist + weapons).
  Reused spec P-table law params (heatDepart 40, toleratedPct, oddPct, heatDepthPerJ 15, logLostPm, friendDel, allyRel 2000, rivalRel, meetDepthPm, meetRangePm, meetWait,
  minorLapseDays 720, fineCap 600, exileDays 1800, jailDays 180, informPay 800, informLockDays 3600). pirEncPm 60 halved hauler net -> 20 (6.6 encounters, 5 looted / career).
  After law+pirates (curated, tank 6): hauler 1319, crisis 1484, pathfinder 332; all spans PASS. Hauler tank 3 fell 1941 -> 1201 (check why).
- NEXT: law-style bots (careful 3, bold 4, reckless 5, payer 6, forgetful 7, double agent 8), then 4a gates.
- DONE law-style bots (bots.cpp lawStyle table; A_POLICY at start; ways out by style; pickups within 2 jumps; deliver-or-A_DROP(10) held jobs; never dock where exiled).
  Engine: arriving at an exiled house's station = don't dock (divert) unless p.dockAnyway (player choice -> breach). Grave = exile + deportation (deportDays 30).
  Rendezvous beats the departure log (spec text): meet runs aren't logged; contact due at stock-ship ETA + meetWait slack, waits meetWait; bots count the wait.
  Deliveries for unlock/friendly = all deliveries (trade + jobs) in delivTo. Courier params relabelled honestly (layer 4, v0.20 values in the meaning).
  Curated tank-6 results: all 9 spans PASS, softlock 0, ledgers exact; logged overall 44.8% (2815/6283; per style 35-73%); recovery minor <= serious everywhere.
- NEXT: Job.taken + rendezvous pay/day vs direct; setRule + ablations (APSIS_OFF); switches gate for new rules (weapons teeth via pirates); law gates (ways out, odd never charged,
  contraband stands, breach = serious + restart never ending); then docs/package. In-flight PoNR warning goes with the bubble step (owner-approved order).
- 4a DELIVERED (2026-11-20): README layer 4a sections, spec doc rev 32 (A, B6, E2, P, owner decisions bullet). Gates: endstates 9/9, law 9/9, switches all (weapons 5/15 teeth), replay suite with law styles,
  native=wasm 115a90f447c9cda6, l1/l2 rerun PASS. Open FAILs (reported): double agent rendezvous pay/day 84 vs 109 (12 meets), interest-off move double agent -1%; careful/payer earn MORE with interest off.
  Debt p90 pooled tanks 5.9/5.9/5.4 y (flag, report only). Next: bubble step (+ in-flight PoNR warning + known-map planning), then 4b.
