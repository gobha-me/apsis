# Shipyard core

C++23 authoring library, independent of simulation and galaxy internals.
The library implements loading, schema checks, direct totals, explicit v3
migration, structural validation, and docking checks. It does not provide the
public CLI/HTTP adapters or performance evaluation.

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
`approachClearance` (d). Keep the defaults for ordinary use. Pending F21 prevents
certifying a port dock; hangar rotations remain pending F19. No visitor airlock
is selected. See the precise pending and switch semantics in CONTRACT.md.

See [CONTRACT.md](CONTRACT.md) for input, formula, migration, numeric comparison,
and canonical serialization contracts for the later independent tests.
