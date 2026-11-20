#!/bin/sh
# WebAssembly build (wasm32-wasi, no threads) with zig's clang + libc++: pip install ziglang
set -e
cd "$(dirname "$0")"
python3 -m ziglang c++ -std=c++23 -O2 -target wasm32-wasi -fno-exceptions -DAPSIS_NO_THREADS -Isrc tools/gates.cpp src/proto/proto.cpp -o build/gates.wasm
python3 -m ziglang c++ -std=c++23 -O2 -target wasm32-wasi -fno-exceptions -DAPSIS_NO_THREADS -Isrc tools/world_core.cpp src/world/serialize.cpp src/world/gen.cpp src/world/econ.cpp src/world/power.cpp src/world/mapdark.cpp src/world/sim.cpp src/world/intents.cpp -o build/world_core.wasm
python3 -m ziglang c++ -std=c++23 -O2 -target wasm32-wasi -fno-exceptions -DAPSIS_NO_THREADS -Isrc tools/pilot_gates.cpp src/pilot/pilot.cpp src/pilot/law.cpp src/pilot/bots.cpp src/world/serialize.cpp src/world/gen.cpp src/world/econ.cpp src/world/power.cpp src/world/mapdark.cpp src/world/sim.cpp src/world/intents.cpp -o build/pilot_gates.wasm
