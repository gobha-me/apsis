# Shipyard core

C++23 authoring library, independent of simulation and galaxy internals.
The library implements loading, schema checks, direct totals, explicit v3
migration, structural validation, docking checks, and performance evaluation.
Public CLI/HTTP adapters remain separate work.

Build (CMake 3.20+, g++-13, OpenSSL 3.x development files):

~~~sh
cmake -S shipyard -B build/shipyard -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Release
cmake --build build/shipyard --parallel 2
~~~

Link the CMake target `apsis::shipyard` and include
`<apsis/shipyard/loader.hpp>`. Load `SchemaSet::load("shipyard/schemas")`,
then `load_rules(exact_file_bytes, schemas)` once. Pass the immutable snapshot
to `import_definition`. No runtime schema fetch or external reference access
occurs. Keep the supplied schemas with the library. See [DEPENDENCIES.md](DEPENDENCIES.md).

`Result<T>` contains an optional value and sorted machine-readable issues
(code, severity, message, JSON Pointer path, details). A missing value is a
loading/input/calculation failure. Successful import means an inspectable
definition, not structural approval: readiness is null and pending issues name
#50/#51 and unresolved docking conventions. The wrapper identifies schema,
active rules version, and lowercase SHA-256 of the exact rules bytes, including
whitespace. Failed rules loading produces no snapshot or rules identity.
Imported caches and embedded rules are discarded and replaced by active data.

Migration note overflow keeps the newest notes in order and one bounded
older-notes-dropped notice, within both active count and text limits. A cap of
one retains the notice alone; a one-character text cap uses reserved `…` plus a
full structured warning. Ordinary note text still validates before culling,
and stable lists reimport identically. V4 exports set `migrationNotesDropped`
when the first entry is a generated notice. Decision:
**Luna, under owner delegation 2026-11-23**.

Rules are the sole balance authority. Names and budgets are never trimmed or
clamped. `technicalLimits.maxBlueprintNameCharacters` is 80;
`technicalLimits.maxBudgetInputCr` is 100000000, solely an input sanity limit.
The shipyard does not enforce player affordability. Ports have no size envelope;
hangars retain their rule-defined dimensions and deck count. No docking
rotation or F03 distance convention is implemented.

Include `<apsis/shipyard/validator.hpp>` for the public operations:

~~~cpp
auto report = apsis::shipyard::validate(definitionBytes, rules, schemas);
auto docking = apsis::shipyard::dock(stationBytes, deckIndex, moduleId,
                                    visitorBytes, rules, schemas);
~~~

Both return the `apsis-shipyard-result-v1` JSON envelope. Validation includes a
recomputed definition when normalization succeeds, even on structural failure.
Docking reports each F18 check separately; `DockOptions` independently controls
`structural` (a), `portReachability` (b), `matchingAirlock` (c), and
`approachClearance` (d). CLI and HTTP always use defaults and never map request
fields to these switches; the public types remain for independent tests of the
real checks. Pending F21 prevents certifying a port dock; hangar rotations remain
pending F19. No visitor airlock is selected. See the precise pending and switch
semantics in CONTRACT.md.

Include `<apsis/shipyard/perf.hpp>` for performance and blank authoring sources:

~~~cpp
using namespace apsis::shipyard;
auto performance = perf(definitionBytes, rules, schemas);
auto loaded = perf(definitionBytes, rules, schemas, LoadFractions{.cargo = 0.5});
auto blankShip = perf(canonical_json(blank_blueprint(rules)), rules, schemas);
auto blankStation = perf(canonical_json(blank_blueprint(rules, BlankType::station)),
                        rules, schemas);
~~~

`perf` returns the same result envelope with `data.performance`, a recomputed
`data.definition`, and distinct `ready` and `jumpReady` flags. It reports current,
empty, half, and full scenarios, propellant and jump charges, endurance, and power
at idle, cruise, and jump. Optional fractions override current loads for the
requested design; carried craft retain their own current loads. Performance
findings use active rule severities, defaulting to `warning`; an active `error`
fails the result. Formula failures reject with structured errors, except the
exact pending-F20 evaluations in DESIGN.md: those emit `pending_F20` and omit
the values, including dependent values, without numeric substitutes. Blank
sources use active defaults and retain their structural validation report.

Include `<apsis/shipyard/authoring.hpp>` to edit a blueprint's decks through the
library. `insert_deck`, `move_deck`, `remove_deck`, and `rename_deck` return a
`Result<Json>` with the edited, normalized v4 blueprint; they do not imply
structural approval. Indices are zero-based, while generated labels use the
one-based deck level:

~~~cpp
auto blank = blank_blueprint(rules); // ship; BlankType::station is explicit
auto inserted = insert_deck(canonical_json(blank), 0, rules, schemas);
auto named = rename_deck(canonical_json(*inserted.value), 0,
                         std::string("Cargo deck"), rules, schemas);
~~~

With bundled rules, generated labels are `Deck 1`, `Deck 2`, and so on. Their
optional `nameGenerated: true` provenance lets insert/move/remove renumber only
generated labels. User/imported labels without that marker remain explicit,
even when they look generated. An explicit rename clears the marker; `nullopt`
restores generated naming. The active rules supply a literal `deckNamePrefix`;
older v4 snapshots fall back to `deckName` plus one space. Rules bound both the
prefix and the resulting label length. See CONTRACT.md for failure codes and
preservation guarantees. Concealed-cargo tooltip work remains deferred to a UI
(F06); no CLI, HTTP, container, or deploy code is added here.

See [CONTRACT.md](CONTRACT.md) for input, formula, migration, numeric comparison,
and canonical serialization contracts for the later independent tests.

<details>
<summary>Main efe5fd82 integration: preserved source text</summary>

These literal merge-source excerpts preserve both sides of the squash-history
merge. The current integrated contract and findings above govern this branch;
performance and authoring are already implemented. Superseded implementation
status is historical, and no open owner decision is resolved here.

~~~text
C++23 authoring library, independent of simulation and galaxy internals.
The library implements loading, schema checks, direct totals, explicit v3
migration, structural validation, and docking checks. It does not provide the
public CLI/HTTP adapters or performance evaluation.
~~~

</details>
