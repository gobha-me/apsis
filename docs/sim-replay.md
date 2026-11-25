# Recorded replay determinism and input properties

Build the test-only adapters with an explicit optimization level. This setting
must apply to the simulation libraries as well as the test executable:

```sh
cmake -S tests/sim -B "$TMPDIR/apsis-replay-o2" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG'
cmake --build "$TMPDIR/apsis-replay-o2" \
  --target sim_replay_driver sim_input_properties --parallel 2
APSIS_SIM_REPLAY_DRIVER="$TMPDIR/apsis-replay-o2/sim_replay_driver" \
  python -m unittest discover -s tests -p test_sim_replay.py -v
```

The independent CTest entry point runs both native suites, setting driver paths
automatically. The clearly labelled test-only CI step builds this entry point at
`-O2` and runs these two CTest entries with at most two workers:

```sh
ctest --test-dir "$TMPDIR/apsis-replay-o2" --output-on-failure --parallel 2
```

Three tiny recorded input streams and expected daily hashes live in
[`tests/fixtures/sim-replay`](../tests/fixtures/sim-replay/README.md). They cover
hauler, pathfinder and reckless careers, including same-day commands, surveys
and flights, over 60 days each. The native driver compares complete checkpoint
bytes every tick for fresh reruns, replay of the actual canonical input log,
reversed same-day submissions and midpoint save/load. Python checks separate
processes against pinned daily full-checkpoint hashes. Changed-seed/command
probes demonstrate that the final fingerprints detect those differences.
Fixtures total less than 10 KB including documentation. Tests never regenerate
them; the fixture README records the explicit recording procedure.

To compare complete builds at `-O0` and `-O2`, use a second build directory:

```sh
cmake -S tests/sim -B "$TMPDIR/apsis-replay-o0" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 '-DCMAKE_CXX_FLAGS_RELEASE=-O0 -DNDEBUG'
cmake --build "$TMPDIR/apsis-replay-o0" --target sim_replay_driver --parallel 2
APSIS_SIM_REPLAY_DRIVER="$TMPDIR/apsis-replay-o2/sim_replay_driver" \
APSIS_SIM_REPLAY_DRIVER_O0="$TMPDIR/apsis-replay-o0/sim_replay_driver" \
  python -m unittest discover -s tests -p test_sim_replay.py -v
```

These tests establish parity for this bounded corpus on the tested toolchain.
They do not certify other seeds, compilers, architectures, WASM, or threading.
The production checkpoint excludes transient input inboxes/logs and coverage
counters. Saves happen between drained ticks; the remaining recorded input log
is supplied externally after restore. Existing 1/2/8-thread standing gates
remain independent. Native replay tests skip unless the primary driver is set;
only the cross-optimization test additionally needs the O0 driver. Fixture
structure/size checks always run in ordinary Python discovery.

## Seeded input properties

```sh
APSIS_SIM_INPUT_PROPERTIES="$TMPDIR/apsis-replay-o2/sim_input_properties" \
  python -m unittest discover -s tests -p test_sim_input_properties.py -v
# Reproduce or explore another uint32 seed without changing the fixed workload:
"$TMPDIR/apsis-replay-o2/sim_input_properties" 12345
```

The default seed is `0x5eed1234`. Every run tests 128 cases of 64 serialized
commands, including signed int32 kind and int64 field extrema, byte-exact log round trips, truncated
log rejection, canonical input permutations, duplicate retention, drained
inboxes and inbox reuse. Arbitrary signed extrema are exercised only in the
serialization/inbox layer, not submitted as game commands.

Eight additional 30-day real-sim streams submit four bounded random commands
per day after START: POLICY, REPAY, REPAIR, SURVEY, FLY and DROP. Both accepted
and refused domain inputs are included. Each tick checks resource ledgers and
full-checkpoint equality under reversed same-day submission. A midpoint
save/load continues the same stream. Failures report the seed and case index.
Operation counts are fixed for reproducibility; the Python wrapper enforces a
15-second deadline for the entire native process. This is a bounded property
suite, not an unbounded campaign or exhaustive malformed-checkpoint fuzzer.
Native property tests skip explicitly unless their driver variable is set.
Use the host's required heavy-workload wrapper for these builds and native runs.
