# Tiny simulation replay corpus

These are recorded canonical pilot input logs, not regenerated bot decisions at
verification time. The three stock-rule worlds use a hauler, pathfinder and
reckless pilot from day zero. Each covers 60 days. Together they include
same-day actions, surveys and flights. Every action retains all six signed
integer input fields.

The first line is `seed majors days`; subsequent lines are
`day action-kind field0 field1 field2 field3 field4 field5`. Days are absolute,
zero based, and actions enter before that day's tick. A save occurs only between
ticks; the remaining command log is supplied separately after restore.

`expected.json` pins the existing `hashBytes(checkpoint(world))` at day zero and
after every tick. This is the full world-plus-pilot checkpoint, including rules
and queued faction intents. It excludes transient inboxes/logs, thread settings,
and coverage counters as defined by the production serializer. These FNV-style
hashes are regression fingerprints, not cryptographic identities. The driver
compares complete checkpoint bytes for repeated creation, replay, canonical
same-day permutations, and midpoint save/load before it publishes any hashes.

Fixtures were recorded from main `96b2b26` with a test-only adapter; all simulation
translation units were compiled with GCC 13 and `-O2 -DNDEBUG`. Build from the
repository root, then record explicitly from the fixture directory:

```sh
O2_BUILD="$TMPDIR/apsis-replay-o2"
cmake -S tests/sim -B "$O2_BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG'
cmake --build "$O2_BUILD" --target sim_replay_driver --parallel 2
cd tests/fixtures/sim-replay
"$O2_BUILD/sim_replay_driver" record 333860 4 60 0 > hauler.txt
"$O2_BUILD/sim_replay_driver" record 632322 4 60 2 > pathfinder.txt
"$O2_BUILD/sim_replay_driver" record 697804 4 60 5 > reckless.txt
"$O2_BUILD/sim_replay_driver" < hauler.txt
"$O2_BUILD/sim_replay_driver" < pathfinder.txt
"$O2_BUILD/sim_replay_driver" < reckless.txt
```

The three JSON outputs were collected once into the `replays` map in
`expected.json`. Tests never update expectations. Review a proposed fixture
change alongside the source change and independently rerun both optimization
builds before accepting a new hash. Fixture updates do not authorize changes to
game rules or balance.
