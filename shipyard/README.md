# Shipyard core

C++23 authoring library, independent of simulation and galaxy internals.
This issue implements loading, schema checks, direct totals, and explicit v3
migration. It does not provide the public CLI/HTTP adapters.

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

See [CONTRACT.md](CONTRACT.md) for input, formula, migration, numeric comparison,
and canonical serialization contracts for the later independent tests.
