# Shipyard findings

Initial review for [#47](https://github.com/gobha-me/apsis/issues/47), using the
GPT-6 v3 single-file prototype, its rules, five canonical definitions, original
self-test report, and preview manifest. References below name prototype artifacts
and functions, not production files. These are static observations plus a read-only
recalculation of example results, not a complete interactive audit. No v3 files
are changed. Open game questions are for Luna; publishing this log surfaces them
for that review and does not record an answer on the owner's behalf.

## F01 — Jump drive pricing

Owner pick, 2026-11-20 4:53 PM ET (Luna question card): "Jump drives: scale price with range, 1,500 / 2,500 / 7,500"

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F01: 1.5 J = 3,750, 2.0 J = 5,000"

**Interpretation:** The 4:53 PM options were for 0.5 / 1.0 / 3.0 J.
Together the picks set 0.5 J = 1,500, 1.0 J = 2,500, 1.5 J = 3,750,
2.0 J = 5,000 and 3.0 J = 7,500 cr.

`apsis-rules-v3.json` charges 2,500 cr for 0.5 J, 5,000 cr for 1.5 J, and
7,500 cr for 2.0 J; the 1.0 J and 3.0 J prices already match. Apply the
decision in v4 rules and migration only (#49/#55); leave v3 goldens byte-identical.
Use all five specified prices without inventing an interpolation rule.

## F02 — Ports and hangars have different size rules

Owner pick, 2026-11-20 4:53 PM ET (Luna question card): "Docking: yes, ports check airlock and clearance, only hangars limit size"

**Interpretation:** The question was whether a 20×34 m ship should fit a
docking port. Ports have no size envelope; hangars limit width, length and deck
count. A 20×34 m visitor may pass a port if every F18 check passes. The quoted
size decision does not remove the structural or crew-reachability checks
confirmed in F18.

`apsis-rules-v3.json`, part `dockingPort`, has 40 m width, 48 m length and five-deck
caps. `index.html:checkDocking` applies the same dimensional tests to ports and
hangars. Remove the per-port envelope in v4 rules/checks (#49/#50/#55), retaining
hangar limits. Wren's recorded 20x34 m dimensions already fit the old numerical
port caps; that passing case alone cannot prove the new distinction. Leave all
v3 fixtures unchanged and test beyond the former port caps as well.

## F03 — Airlock alignment needs a precise convention

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F03: same deck, exterior, facing out, within 1 cell"

**Interpretation pending F21:** The pick requires the same deck, exterior hull
placement, outward facing, and alignment within 1 cell. Which visitor deck is
"the same deck" and the distance measure and reference point remain Open for
Luna, not guessed. F02 sets the port/hangar size distinction; F18 settles the
complete port pass rule and use of any compatible matching visitor airlock.

`index.html:checkDocking` collects interface labels from any
reachable, clear visitor airlock and checks label equality. It does not compare
transformed airlock positions or facing. The v4 docking check must evaluate
same-deck placement, exterior hull position, outward facing, and the 1-cell
alignment rule, alongside approach clearance (#50/#52). Treating the 1-cell
limit as inclusive is an interpretation pending Luna's F21 answer, along with
the visitor-deck mapping, distance measure, and reference point. Interface-label
equality alone does not prove alignment. Remove port size caps; retain hangar
width, length, and deck-count limits.

## F04 — Blank action inherits station mode

Known nit in [#55](https://github.com/gobha-me/apsis/issues/55).
`index.html`, `blankBtn` handler following `resetDesign`: after making a blank
default ship, it restores station mode/name when the previous design was a
station. Thus blank can open as a station. `createDesign('blank', buildType)`
already supports an explicit type. The proposed headless default is a ship,
with explicit station creation; do not inherit editor history. Preserve imported
definitions' explicit station mode. Tests come from a separate session.

## F05 — Generated deck names can disagree with physical level

Known nit in #55. `index.html:addDeckAt` uses total deck count plus one to name
an inserted deck regardless of insertion index. `moveDeck` and `removeDeck` keep
existing names. The preview manifest even has `Deck 05` at index 0 and `Deck 04`
at index 4. Generated names must track deck level in new production operations;
preserve explicit user names and historical fixture names. Track automatic versus
explicit naming so reorder/insert does not silently overwrite a user's label.

## F06 — Concealed-cargo tooltip is deferred to a UI

Deferred in #55. `index.html`, `concealedHelp`, contains claims about hiding cargo
from scans but not departure logs; the `hidden` part description also mentions
that distinction. Keep wording/visibility questions for Luna and future UI work.
Do not add a tooltip or scan/log simulation to this headless implementation.

## F07 — Passing self-test report does not cover Rustbucket

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F07: Rustbucket stays a deliberate failing example"

**Interpretation:** Rustbucket stays a known-bad fixture the validator must
reject. Keep it as a negative test.

`apsis-example-self-test-v3.json` and `index.html:selfTestExamples` cover embedded
keys `flat`, `kestrel`, `skiff`, `tender`, and `station`. The supplied canonical
definitions instead include Wren, Skiff, Kestrel Tender, Wayfarer Anchorage, and
Rustbucket. Do not identify fixtures by position or equate the passing report
with all canonical designs passing.

Rustbucket's stored `validation` is `ready: false`: it has `missing_airlock` and
an `unreachable_component` error affecting three components, alongside `power_shortfall`, `no_jump_fuel`,
and `no_cargo` warnings. Read-only recomputation matched the stored results for
all five definitions: the other four are ready; Rustbucket is not. Embedded rules
also match the standalone rules. Preserve the negative fixture and add explicit
v4 rejection expectations in an independent test session (#48/#50/#51). Do not
repair the golden or reclassify its structural errors to make Rustbucket pass.

## F08 — Automation still depends on prototype packaging and Canvas

`index.html`, CommonJS core setup, expects `./rules.json`, whereas the supplied
export is named `apsis-rules-v3.json`. `canvasForPreview` requires DOM Canvas.
Browser automation helpers are useful reference contracts, but the supplied
single-file artifact is not the proposed standalone CLI/HTTP/native renderer.
Use explicit packaged rules/assets and a browser-independent renderer (#49/#52/#53).
The preview manifest is an edited five-deck Kestrel and is not a golden render
set for the canonical definitions; compare only equivalent inputs.

## F09 — UI limits drift from the exported rules

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F09: rules file wins, 80 chars / 100M"

Jeff's typed question, 2026-11-20 5:14 PM ET (Luna question card): "Budget limits I thought it would be up to whatever could be afforded?"

Owner pick, 2026-11-20 5:14 PM ET (Luna question card): "Keep 100M as a technical sanity limit only; in game, budget is what the player can afford"

**Interpretation:** The rules file supplies the 80-character name limit and
100M cr technical input sanity cap. Label the cap under technical limits, not
as a game budget. The UI reads these limits from the rules. The name limit
stands. Player affordability belongs to the sim;
the shipyard does not enforce it.

`index.html`, ship-name change handler, truncates to 40 characters; the rules'
`limits.maxBlueprintName` is 80. Its budget change handler caps at 10,000,000 cr;
`limits.maxBudget` is 100,000,000 cr. In v4, label that value as
`technicalLimits.maxBudgetInputCr`, a technical input sanity cap, alongside
`technicalLimits.maxBlueprintNameCharacters` at 80. The core, production
adapters, and future UI must read these rules fields (#49/#52). The shipyard
does not enforce player affordability or treat 100M cr as a game budget ceiling.

## F10 — Formula fallback can conceal invalid rule data

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F10: reject with an error"

**Interpretation:** An undefined or invalid formula rejects the design with a
clear error, never a silent zero. Until Luna answers F20, F10 rejection excludes
these evaluations: in zero-mass blank ships and blank stations, `acceleration`
and `range` division by `mass`, `deltaV` division by `endMass` and ln(0), and
`fuelToDryMassRatio` division by `dryMass`; in engine-less designs, including
stations, full/current `burnSeconds` and `exhaustVelocity` division by zero
`burnRate`. Open for Luna, not guessed. Do not assign a result or handling rule
here.

`index.html:formula` substitutes zero for missing variables, zero denominators,
invalid logarithm input, and nonfinite results. This can make a bad formula look
like a computed value. The v4 loader must reject malformed formulas, unknown
variables/operators, and invalid arity (#49). At evaluation, missing variables,
division by zero, invalid logarithm input, and nonfinite or otherwise undefined
results reject the design with a clear formula error identifying its rule/path
(#49/#51), except exactly the F20 evaluations listed above and below: zero-mass
blank ships/stations' `acceleration` and `range` on `mass`, `deltaV` on `endMass`
and ln(0), `fuelToDryMassRatio` on `dryMass`; and engine-less designs' full/current
`burnSeconds` and `exhaustVelocity` on zero `burnRate`, including stations.
Open for Luna, not guessed. Do not assign a result or handling rule here.
Legitimate zero-thrust/no-drive values remain distinct from formula failures;
neither zero nor an unavailable value may hide a failed calculation outside
these unresolved F20 cases.

## F11 — Access inside a multi-deck cargo hold

**Open for Luna, not guessed.** For [#60](https://github.com/gobha-me/apsis/issues/60),
what ladder or other access rules apply inside a hold? The prototype has no
multi-deck holds, so there is nothing to match from v3. These findings leave v3
goldens unchanged; new rules await Luna.

## F12 — Walkability across the open space

**Open for Luna, not guessed.** A hold is one volume with no floor between the
decks it spans. What walkability rules apply across that open space, including
edges, railings, and falls? Do not infer traversable routes from volume membership.

## F13 — Holds meeting or overlapping hangars

**Open for Luna, not guessed.** How may a multi-deck hold meet or overlap a hangar,
and how do hangar deck-count limits apply at that boundary or overlap?

## F14 — Partial openings versus full spans

**Open for Luna, not guessed.** Can holds be partially open, or must their open
space fully span the decks they cover? Do not invent intermediate-floor exceptions.

## F15 — Minimum and maximum hold span

**Open for Luna, not guessed.** The requirement describes N >= 2 decks. What
minimum and maximum spans should the rules allow beyond that stated shape?

## F16 — Deck and level numbering across a hold

**Open for Luna, not guessed.** How are decks and levels numbered across a
multi-deck hold, including the intervening open space? Do not infer numbering
or labels from the prototype's single-deck cargo behavior.

## F17 — Docking and airlocks at a hold

**Open for Luna, not guessed.** How do docking and airlocks interact with a
multi-deck hold? Keep hold-specific placement, access, and alignment rules pending
Luna rather than extending existing docking rules by assumption.

## F18 — Port-dock checks and visitor airlock

**Closed by owner decision, 2026-11-20, via Luna.** The quoted questions and
readbacks are Luna's wording. Jeff's typed replies provide context; his
affirmation below is the owner decision.

Luna's dock-check question:

Luna's question, 2026-11-20 5:24 PM ET (Luna question card): "Keep the prototype's other dock checks (structural faults, crew can reach the port, matching airlock)?"

Jeff's typed reply/comment:

Jeff's typed reply/comment, 2026-11-20 5:24 PM ET (Luna question card): "Test"

Luna's readback:

Luna's readback, 2026-11-20 5:25 PM ET (Luna question card): "keep them and test each one"

Luna's visitor-airlock question:

Luna's question, 2026-11-20 5:25 PM ET (Luna question card): "Which airlock does the visiting ship use?"

Jeff's typed reply/comment:

Jeff's typed reply/comment, 2026-11-20 5:25 PM ET (Luna question card): "Autodock? Docking could be hard and damage both ... Thoughts"

**Luna's proposal, paraphrased:** The shipyard answers whether the ship can
dock at all, using the checks and any compatible matching airlock, without
picking an airlock in the tool. Autodock versus manual flight docking and
possible damage to both ships belong to the game/sim outside the shipyard.

Jeff's owner decision / Owner pick, answering three asks from Luna in order:

Owner pick, 2026-11-20 5:25 PM ET (Luna question card): "Yes and yes and yes"

The three asks were: (1) Luna read "Test" as keeping the three v3 checks and
testing each one; (2) send the Shipyard part to Argus and Engine Lead: the tool
checks only whether the ship can dock, any compatible matching airlock works,
and nothing is picked; (3) save autodock and docking damage as a game mechanic
for Claude. These readbacks are Luna's wording; Jeff's affirmation is the owner
decision.

**Interpretation — settled port-dock rule:** For a valid station port target
and visiting ship, `dock` passes only if all four conditions hold:

- (a) No structural fault in either ship (station host or visitor).
- (b) Station crew can reach the port.
- (c) At least one compatible matching visitor airlock is crew-reachable and
  clear, and meets F03: same deck, exterior, facing out, within 1 cell (F21
  remains open for the deck mapping and distance convention). A visitor
  reachability or clearance failure ties to check (a) at the rules' `error`
  severity (v3).
- (d) Approach clearance.

Each (a)–(d) check needs its own independent test in #50, failing when its own
check is disabled, with mutation evidence and no mocking of the unit under
test. Any matching airlock works; the tool does not pick an airlock. Ports have
no size envelope; hangars limit
width, length and deck count (F02). Flight docking and damage are outside the
shipyard, on the sim side. These ordinary port checks do not answer F17's
hold-specific question, which remains open.

Prototype evidence: `index.html:3791-3792` rejects host/visitor structural
errors; `:3636-3640` records visitor-airlock crew reachability and clearance,
and `:3793` filters matches to reachable, clear airlocks; `:3800` requires a
matching reachable exterior visitor airlock; `:3801` requires station crew
reachability. Visitor reachability and clearance also feed check (a) at the rules'
`error` severity (v3). F03 adds geometric alignment requirements, with its deck
mapping and distance convention still pending F21; F02 removes the prototype's
port size envelope. Issue #50 already contains these acceptance items and remains open.

## F19 — Allowed docking rotations

**Open for Luna, not guessed.** Which visitor rotations are allowed when
checking hangar width/length fit? The original test-strategy phrase "allowed
rotations after rule clarification" needs an owner rule before independent
tests can assert permitted orientations. Do not infer quarter-turn permission
or prohibition from the prototype; F02's port size decision does not settle
hangar rotations.

The v4 rules deliberately omit v3 `docking.allowQuarterTurn: true`
(`fixtures/v3/apsis-rules-v3.json:85`) pending Luna. The omission encodes
neither permission nor prohibition; F19 remains **Open for Luna, not guessed**.

## F20 — Zero denominators and undefined logarithms in blank and engine-less designs

**Open for Luna, not guessed.** How should v4 handle these evaluations?

- In zero-mass blank ships and blank stations: `acceleration` and `range`
  division by `mass` = 0; `deltaV` division by `endMass` = 0 and ln(0);
  `fuelToDryMassRatio` division by `dryMass` = 0.
- In engine-less designs, including stations: full/current `burnSeconds`
  (`fuel/burnRate`, twice) and `exhaustVelocity` (`thrust/burnRate`, once)
  division by `burnRate` = 0.

Argus's instrumented prototype compilation of `createDesign('blank', 'ship')`
and `createDesign('blank', 'station')` found, in each, four `acceleration` and
four `range` zero-mass divisions, two `burnSeconds` and one `exhaustVelocity`
zero-`burnRate` divisions, one `deltaV` zero-`endMass` division and one ln(0),
and one `fuelToDryMassRatio` zero-`dryMass` division. Among the five canonical
definitions, only Wayfarer Anchorage encounters these failures: the three
`burnRate` evaluations. It is a ready station with no engines.

F10 rejection excludes exactly the evaluations listed above until Luna answers;
these pending-F20 formula cases must not reject a blank `build` or reject
Wayfarer Anchorage. #51 must hold on F20 by returning a structured `pending F20`
issue rather than outputting excluded values until Luna answers. Do not invent
numeric stand-ins. Open for Luna, not guessed. Do not assign a result or handling
rule here.

## F21 — Same-deck mapping and 1-cell measurement

**Open for Luna, not guessed.** For F03, which visitor deck counts as 'the same
deck', and how is 'within 1 cell' measured, from what reference point?

The owner pick gives the alignment requirements but does not define the visitor
deck mapping, distance measure, or reference point. An inclusive 1-cell limit
is an interpretation pending this answer, not a settled rule. Keep F03's other
requirements and F18's port checks; do not guess the missing convention.

## F22 — Loader boundary and open prototype differences (#49)

The #49 loader boundary does not answer any open finding. The loader
validates input shape and active-rule scalar/reference limits,
then recomputes direct costs, capacities, and masses. It discards imported
derived/validation caches and embedded rules as authority. Structural legality,
carried-fit/empty-berth legality, readiness, performance, and docking remain
unimplemented for #50/#51; exported readiness is unset with structured pending
issues, including `pending_F20` for applicable blank/engine-less cases. No F20
evaluation path or excluded numeric value is exposed. In
particular, migration preserves Rustbucket's geometry without repairing it.

Stable serialization, exact input comparisons, absolute epsilon, and computed
rounding are specified in CONTRACT.md for the independent tests session. No
Unicode normalization occurs.

The following three prototype differences are **Open for Luna, not guessed**,
with no value picked. Current behaviour is an **interim interpretation**, not
a settled technical encoding contract:

- Name-length counting: the current loader counts Unicode scalar values; the
  prototype uses JavaScript UTF-16 length units (`index.html:3702`, `:3557`).
  Thus 41–80 astral characters currently fit v4's 80-character limit but exceed
  the prototype's limit. The counting choice remains open.
- Label trimming and fallback: the current loader preserves empty and
  whitespace-only labels without trimming or a fallback; the prototype's
  `label()` returns `v.trim()||fallback` (`index.html:3702`). The choice remains
  open.
- Migration-note overflow: the current loader preserves existing notes and
  leaves extra changes in the report with a warning; the prototype keeps the
  newest notes with `.slice(-maxMigrationNotes)` (`index.html:3766`). The choice
  remains open.

None of F11–F17 or F19–F21 is answered by this scope boundary or these interim
interpretations.

## F23 — Validator implementation boundary and canonical comparison (#50)

Implementation note, not a new game rule or a resolution of an open finding.
The public `validate` operation now recomputes direct totals and structural
issues with active rules. Canonical v3 inputs after migration give structural
PASS for Wren, Skiff, Kestrel Tender, and Wayfarer Anchorage; Rustbucket remains
FAIL with `missing_airlock` and three `unreachable_component` findings. Its
geometry is preserved. No canonical structural outcome changed under v4.
The original passing self-test report does not include Rustbucket (F07).

Argus M2 exposed two additional layouts where the initial v4 implementation
accepted what v3 rejected. The implementation now matches v3 with production
defaults: hull connectivity floods from the first command module across paired
ladder links, so an isolated floored deck without a ladder produces
`disconnected_hull`. Crew reach starts from the single v3 `bridgeStart` service
cell (bottom edge first), rather than every walkable cell beside every bridge.
The cockpit at x=4 in a single floor row no longer joins the reactor at x=1 to
life support at x=7; the component on the other side remains unreachable.
The former per-deck-only hull and multiple-start crew rules are superseded.

Performance advisories from the prototype, including Skiff's `no_cargo` and
Rustbucket's power/fuel warnings, are deferred to #51. Complete readiness and
jump readiness remain unset on structural success; structural failure sets
ready false. Wayfarer's three excluded burn-rate formula evaluations remain
nonblocking `pending_F20`, with no excluded values or numeric substitutes.
The #49 blank-design F20 exclusions remain unchanged.

The prototype required structural validity, a reachable and clear visitor
airlock, a clear approach, the size envelope, deck limits, and station crew
reachability. In v4 a compatible port candidate returns FAIL with `pending_F21`
until Luna defines same-deck mapping and the distance/reference/inclusivity
convention. Hangars report `widthLengthFit: true` without `pending_F19` when the
visitor fits unrotated. A visitor exceeding the envelope in both orientations
reports `widthLengthFit: false` with `dock_hangar_size_limit` at the active
docking severity (v3: error), including measured dimensions and limits, without
`pending_F19`. Only a fit requiring rotation remains uncertified with
`pending_F19`; the rule-defined deck cap is enforced.
Argus S2's definite-envelope behavior is implemented without an F19 owner pick.
The remaining pending results preserve F03/F18 without inventing F19/F21 answers.
Ports have no size envelope and the API selects no visitor airlock. F18(a)–(d)
have independent public switches, with selected-target access/approach rule
families disabled consistently when (b)/(d) is disabled; defaults retain all
structural faults. Visitor-airlock access and clearance remain structural
findings at their active rule severities (v3 error).
CLI and HTTP always use default options and never map request fields onto these
switches. The public option types remain solely because existing independent
tests exercise the real library through them; moving them would require changes
to those test callers.

Multi-deck holds remain unavailable: after schema-checking a bounded probe with
only unsupported `holds` fields removed, a requested `holds` field returns
structured pending F11–F17 issues once per finding kind before any guessed hold
totals, access, traversal, or ordinary missing-floor finding. No hold schema or
geometry is invented, and no opening is filled. The independent tests session must cover
the public switches with mutation evidence and the remaining pending gates.
F11–F17, F19, F20, and F21 remain Open for Luna.
