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

Jump drive price scales with range: 0.5 J = 1,500 cr, 1.0 J = 2,500 cr, 3.0 J = 7,500 cr.

`apsis-rules-v3.json`, parts `drive05`, `drive10`, and `drive30`: v3 charges
2,500 cr for 0.5 J; the other two specified anchors already match. Apply the
decision in v4 rules and migration only (#49/#55); leave v3 goldens byte-identical.
For Luna: v3 also has 1.5 J at 5,000 cr and 2.0 J at 7,500 cr. The decision does
not specify these prices or an interpolation rule. Preserve their provisional
values pending confirmation; do not invent a new pricing curve.

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

Open for Luna. `index.html:checkDocking` collects interface labels from any
reachable, clear visitor airlock and checks label equality. It does not compare
transformed airlock positions or facing. The owner's alignment/clearance rule is
settled; what placement, orientation, and alignment representation should the
headless check use? Specify that convention before #50/#52 finalize it. Do not
claim interface-label equality proves geometric alignment or invent a new rule.

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
v4 expectations in an independent test session (#48/#50/#51). For Luna: confirm
whether a separate structurally valid, underpowered stock example is desired;
do not repair the golden or reclassify its structural errors to manufacture one.

## F08 — Automation still depends on prototype packaging and Canvas

`index.html`, CommonJS core setup, expects `./rules.json`, whereas the supplied
export is named `apsis-rules-v3.json`. `canvasForPreview` requires DOM Canvas.
Browser automation helpers are useful reference contracts, but the supplied
single-file artifact is not the proposed standalone CLI/HTTP/native renderer.
Use explicit packaged rules/assets and a browser-independent renderer (#49/#52/#53).
The preview manifest is an edited five-deck Kestrel and is not a golden render
set for the canonical definitions; compare only equivalent inputs.

## F09 — UI limits drift from the exported rules

`index.html`, ship-name change handler, truncates to 40 characters; the rules'
`limits.maxBlueprintName` is 80. Its budget change handler caps at 10,000,000 cr;
`limits.maxBudget` is 100,000,000 cr. The core reads rules limits. Production
adapters must read those same limits rather than perpetuate independent constants
(#49/#52). Any proposed change to the actual game limits goes to Luna.

## F10 — Formula fallback can conceal invalid rule data

`index.html:formula` substitutes zero for missing variables, zero denominators,
invalid logarithm input, and nonfinite results. This can make a bad formula look
like a computed value. The v4 loader must reject malformed formulas, unknown
variables/operators, and invalid arity (#49). For Luna: specify undefined numeric
outcomes separately from legitimate zero-thrust/no-drive cases. Do not silently
change game semantics while hardening rule evaluation.
