#!/bin/sh
# Standalone tooling build, using the same single-thread WASI flags as sim/.
set -eu
cd "$(dirname "$0")/../.."

mode=${1:-wasm}
case "$mode" in
  wasm|native) ;;
  *) printf '%s\n' 'Usage: sh tools/viewer/build.sh [wasm|native]' >&2; exit 2 ;;
esac
if [ "$mode" = wasm ] && ! python3 -c 'import importlib.util; raise SystemExit(importlib.util.find_spec("ziglang") is None)'; then
  printf '%s\n' 'Missing Python ziglang package; skipping WASM build (no package installed).' >&2
  exit 2
fi
revision=$(git rev-parse HEAD)
set -- -std=c++23 -O2 -fno-exceptions -DAPSIS_NO_THREADS \
  "-DAPSIS_VIEWER_REVISION=\"$revision\"" -Isim/src \
  tools/viewer/export.cpp sim/src/pilot/pilot.cpp sim/src/pilot/law.cpp \
  sim/src/world/serialize.cpp sim/src/world/gen.cpp sim/src/world/econ.cpp \
  sim/src/world/power.cpp sim/src/world/mapdark.cpp sim/src/world/sim.cpp sim/src/world/intents.cpp
mkdir -p tools/viewer/build
if [ "$mode" = wasm ]; then
  set -- python3 -m ziglang c++ "$@" -target wasm32-wasi -o tools/viewer/build/viewer.wasm
else
  set -- "${CXX:-c++}" "$@" -o tools/viewer/build/viewer-export
fi
exec "$@"
