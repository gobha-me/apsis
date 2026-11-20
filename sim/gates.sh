#!/bin/sh
# All layer-1 gates: native (threads 1/2/8), WebAssembly, and native = WebAssembly.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -G "Unix Makefiles" >/dev/null && cmake --build build -j2 >/dev/null
./build_wasm.sh
echo "== native"; ./build/gates ref/vectors.txt 3600 gates_l1.json
echo "== WebAssembly"; node tools/run_wasm.mjs build/gates.wasm ref/vectors.txt 3600 gates_l1_wasm.json 2>/dev/null
N=$(./build/gates --digest); W=$(node tools/run_wasm.mjs build/gates.wasm --digest 2>/dev/null)
echo "== native $N / WebAssembly $W"
if [ "$N" = "$W" ]; then R=7; else R=0; fi
python3 - "$R" "$N" <<'PY'
import json,sys
g=json.load(open('gates_l1.json')); g.append({"gate":"native = WebAssembly: every day's hash, 7 seeds x 3600 days","ok":int(sys.argv[1]),"n":7,"note":sys.argv[2]})
json.dump(g,open('gates_l1.json','w'))
print("native = WebAssembly", "PASS" if sys.argv[1]=="7" else "FAIL")
PY
