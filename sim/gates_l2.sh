#!/bin/sh
# Layer-2 gates. Needs the frozen v0.20 files for the port gate: V020=/path/to/v0.20 ./gates_l2.sh
set -e
cd "$(dirname "$0")"; V=${V020:-/home/claude/logi}; HERE=$(pwd)
cmake -S . -B build -G "Unix Makefiles" >/dev/null && cmake --build build -j2 >/dev/null
echo "== port: intents off = v0.20, state at every month end, 7 seeds x 300 years"
PORT=0; for sm in 333860:4 110478:4 697804:4 57206:4 632322:4 517908:4 552723:5; do s=${sm%:*}; m=${sm#*:}
  (cd "$V" && node "$HERE/ref/world_ref.js" "$V/logi20.js" "$V/exp20.js" $s $m 300 > /tmp/js_$s.txt 2>/dev/null)
  ./build/world_port $s $m 300 > /tmp/cc_$s.txt 2>/dev/null
  if paste /tmp/js_$s.txt /tmp/cc_$s.txt | awk '$1!=$2{exit 1}'; then PORT=$((PORT+1)); echo "  $s match $(wc -l < /tmp/cc_$s.txt) months"; else echo "  $s MISMATCH"; fi; done
APSIS_TEETH=1 ./build/world_port 333860 4 300 > /tmp/teeth.txt 2>/dev/null; if paste /tmp/js_333860.txt /tmp/teeth.txt | awk '$1!=$2{exit 1}'; then TEETH=0; else TEETH=1; fi
echo "  port $PORT/7, teeth (one number off by one diverges) $TEETH/1"
echo "== world gates"; ./build/world_gates 300 | grep -E "general war|intents|threads"; ./build/world_gates sample 200
echo "== standing gates"; ./build/world_core gates
./build_wasm.sh; N=$(./build/world_core digest); W=$(node tools/run_wasm.mjs build/world_core.wasm digest 2>/dev/null); [ "$N" = "$W" ] && NW=1 || NW=0; echo "native = WebAssembly (world, 7 seeds x 100 years, every month): $NW/1 ($N)"
python3 - $PORT $TEETH $NW <<'PY'
import json,sys
port,teeth,nw=map(int,sys.argv[1:4])
def wilson(k,n,z=1.96):
  p=k/n; d=1+z*z/n; c=(p+z*z/(2*n))/d; h=z*((p*(1-p)/n+z*z/(4*n*n))**0.5)/d; return c-h,c+h
def warGate():
  ww=json.load(open('world_wide_l2.json')); a,b=ww['off'],ww['on']; r=(b['war']/b['n'])/(a['war']/a['n']); lo1,hi1=wilson(a['war'],a['n']); lo2,hi2=wilson(b['war'],b['n'])
  print('  war 1,000 seeds: v0.20 %d, on %d, ratio %.2f, intervals [%.3f,%.3f] [%.3f,%.3f]'%(a['war'],b['war'],r,lo1,hi1,lo2,hi2)); return int(0.75<=r<=1.25 or (lo2<=hi1 and lo1<=hi2))
w=json.load(open('world_l2.json')); c=json.load(open('world_core_l2.json')); i=w['intents']
g=[["port: intents off = v0.20, state at every month end (7 seeds x 300 years)",port,7],["  teeth: one v0.20 number off by one diverges",teeth,1],
 ["general war with intents off reproduces v0.20 (4/21, stable Core 0/3)",int(w['war_off']['wars']==4 and w['war_off']['stable']==0),1],
 ["general war, 1,000 seeds: intents on / v0.20 within 0.75-1.25, or 95% intervals overlap (owner, layer 2; world_wide_l2.json)",warGate(),1],
 ["stable Core never at general war, intents on",int(w['war_on']['stable']==0),1],
 ["ledgers exact every tick, off and on, 7 seeds x 300 years",int(all(x['off']['ledgerBad']==0 and x['on']['ledgerBad']==0 for x in w['character'])),1],
 ["no intent lands with delay 0 (real world)",int(i['zeroDelay']==0),1],
 ["per-resource supply: a house that can feed one feeds one (logged cases)",int(i['feedOne']>0),1],
 ["threads 1, 2, 8: identical world, intents on",i['threadsOk'],7],
 ["checkpoint resume = uninterrupted (full state), off and on",c['checkpointResume'][0],c['checkpointResume'][1]],
 ["checkpoint round trip byte-exact",c['roundTrip'][0],c['roundTrip'][1]],["  teeth: corrupted checkpoints caught",c['teeth'][0],20],
 ["native = WebAssembly, world, every month",nw,1]]
json.dump(g,open('gates_l2.json','w'))
for x in g: print("%-80s %d/%d %s"%(x[0],x[1],x[2],"PASS" if x[1]==x[2] else "FAIL"))
PY
