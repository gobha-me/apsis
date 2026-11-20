# Shipyard findings

Initial review for [#47](https://github.com/gobha-me/apsis/issues/47), using the
GPT-6 v3 single-file prototype, its rules, five canonical definitions, original
self-test report, and preview manifest. References below name prototype artifacts
and functions, not production files. These are static observations plus a read-only
recalculation of example results, not a complete interactive audit. No v3 files
are changed. Open game questions are for Luna; publishing this log surfaces them
for that review and does not record an answer on the owner's behalf.

## F01 — Jump drive pricing

**Owner decision, 2026-11-20**

Jump drive prices are 0.5 J = 1,500, 1.0 J = 2,500, 1.5 J = 3,750, 2.0 J = 5,000 and 3.0 J = 7,500 cr.

`apsis-rules-v3.json` charges 2,500 cr for 0.5 J, 5,000 cr for 1.5 J, and
7,500 cr for 2.0 J; the 1.0 J and 3.0 J prices already match. Apply the
decision in v4 rules and migration only (#49/#55); leave v3 goldens byte-identical.
Use all five specified prices without inventing an interpolation rule.

## F02 — Ports and hangars have different size rules

**Owner decision, 2026-11-20**

Docking ports check only airlock alignment and approach clearance, with no size limit, so a 20x34 m ship fits a port. Only hangars limit size, by width, length and deck count.

`apsis-rules-v3.json`, part `dockingPort`, has 40 m width, 48 m length and five-deck
caps. `index.html:checkDocking` applies the same dimensional tests to ports and
hangars. Remove the per-port envelope in v4 rules/checks (#49/#50/#55), retaining
hangar limits. Wren's recorded 20x34 m dimensions already fit the old numerical
port caps; that passing case alone cannot prove the new distinction. Leave all
v3 fixtures unchanged and test beyond the former port caps as well.

## F03 — Airlock alignment needs a precise convention

**Owner decision, 2026-11-20**

An airlock is aligned when it's on the same deck as the port, on the exterior hull, facing outward, and within 1 cell of offset from the port. Docking ports have no size limit; only hangars limit size.

`index.html:checkDocking` collects interface labels from any
reachable, clear visitor airlock and checks label equality. It does not compare
transformed airlock positions or facing. The v4 docking check must evaluate
same-deck placement, exterior hull position, outward facing, and the inclusive
1-cell offset limit, alongside approach clearance (#50/#52). Interface-label
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

**Owner decision, 2026-11-20**

Rustbucket stays a deliberate failing example, a known-bad fixture the validator must reject. Keep it as a negative test.

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

**Owner decision, 2026-11-20**

The rules file wins: names up to 80 characters, budgets up to 100M cr. The UI should read its limits from the rules.

Clarification (via Luna, 2026-11-20, 5:14 PM America/New_York):

- The 100M cr is only a technical sanity limit on input values, not a game rule. Label it that way in the rules and docs.
- In the game, a build's budget is whatever the player can afford (money plus available credit). The sim enforces that, not the shipyard.
- The 80-character name limit stands.

`index.html`, ship-name change handler, truncates to 40 characters; the rules'
`limits.maxBlueprintName` is 80. Its budget change handler caps at 10,000,000 cr;
`limits.maxBudget` is 100,000,000 cr. In v4, label that value as
`technicalLimits.maxBudgetInputCr`, a technical input sanity cap, alongside
`technicalLimits.maxBlueprintNameCharacters` at 80. The core, production
adapters, and future UI must read these rules fields (#49/#52). The shipyard
does not enforce player affordability or treat 100M cr as a game budget ceiling.

## F10 — Formula fallback can conceal invalid rule data

**Owner decision, 2026-11-20**

An undefined or invalid formula rejects the design with a clear error, never a silent zero.

`index.html:formula` substitutes zero for missing variables, zero denominators,
invalid logarithm input, and nonfinite results. This can make a bad formula look
like a computed value. The v4 loader must reject malformed formulas, unknown
variables/operators, and invalid arity (#49). At evaluation, missing variables,
division by zero, invalid logarithm input, and nonfinite or otherwise undefined
results reject the design with a clear formula error identifying its rule/path
(#49/#51). Legitimate zero-thrust/no-drive values remain distinct from formula
failures; neither zero nor an unavailable value may hide a failed calculation.

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
