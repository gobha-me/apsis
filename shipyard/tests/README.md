# Independent loader tests

These tests exercise the public API in `loader.hpp`, with no mocks. Expectations
come from DESIGN.md, CONTRACT.md, the owner decisions, and the unchanged v3
fixtures. They cover loading and direct totals only. Structural rejection
(including Rustbucket), performance evaluation, and docking remain #50/#51 work;
no open game decision is assigned a value here.

Run with g++-13, C++23, CMake, and OpenSSL 3.x development files:

```sh
cmake -S shipyard -B build/shipyard -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_CXX_FLAGS=-Werror
cmake --build build/shipyard --parallel 2
ctest --test-dir build/shipyard --output-on-failure
```

`shipyard-formula-nesting` covers the formula nesting boundary. It can also be
run directly:

```sh
build/shipyard/shipyard-rules-tests --formula-nesting
```

CONTRACT.md specifies a 64-level formula guard independently of the configurable
JSON parser nesting guard. The test raises `InputLimits.maxNesting` to 128 and
supplies 64 nested `floor` operators over a constant. That documented boundary
must load; 65 levels must fail with a readable `invalid_formula` issue at
`/formulas/structureMass`.

The ordinary suites still check parser depth boundaries, import resource guards,
carried-craft depth, formula operand bounds, and malformed formula rejection.
