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

Rules are the sole balance authority. Names and budgets are never trimmed or
clamped. `technicalLimits.maxBlueprintNameCharacters` is 80;
`technicalLimits.maxBudgetInputCr` is 100000000, solely an input sanity limit.
The shipyard does not enforce player affordability. Ports have no size envelope;
hangars retain their rule-defined dimensions and deck count. Port F03 alignment
uses same-deck orthogonal centre distance. Hangar rotation remains pending F19.

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
real checks. The F03/F21 decision by Luna, under owner delegation 2026-11-23,
applies to ports. A compatible exterior outward-facing airlock on the port
deck satisfies check (c) if the exact footprint centres coincide or differ by
at most one cell along a single axis. The optional `docking.alignment` policy
fixes the limit at one cell, also used by older v4 snapshots. Hangar check (c)
reports `pending_F21` for an eligible airlock, independently of F19 rotation
handling.
See CONTRACT.md for complete check and diagnostic semantics.

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

See [CONTRACT.md](CONTRACT.md) for input, formula, migration, numeric comparison,
and canonical serialization contracts for the later independent tests.
