# Layer 4a source

- Zip filename: `apsis-sandbox-v2-layer4a.zip`
- Zip SHA256: `71683e9ce41ec2fa4bab7df2a8bb3e5c20ba5a8c8203dba25b58a588e0daacdf`
- Imported tree: 120 regular files, unchanged. The archive listing reports 129
  entries, including nine directories. This manifest and the spec copy bring
  `sim/` to 122 regular files.
- Spec filename: `apsis-v2-layer4a-spec.md` (copied unchanged beside this manifest)
- Spec SHA256: `978eef9d69e646d2009827af7b72a15ee56eadd1f45b697d6d4227dfd839421c`

A later layer 4b or jump-bubble zip should replace `sim/` wholesale; keep or
refresh this manifest.

CI builds the native C++23 targets outside this tree. It runs the native
layer-1 `gates` binary from `gates.sh` with the bundled `ref/vectors.txt` and
3600 days, `world_core gates`, and `pilot_gates gates`, `switches`, `endstates`,
and `law`. Reports go to runner temp storage to preserve imported artifacts.

CI does not invoke `gates.sh` directly because it also builds WebAssembly;
WASM tooling and native/WASM parity are deferred. It does not invoke
`gates_l2.sh`: its port gate requires the frozen v0.20 tree, which is absent
from this zip. No substitute v0.20 is supplied. The script's 300-year ports,
1,000-seed `world_wide`, `pilot_exp` wide samples, and benchmarks are excluded
from this import CI. No gate scripts, assertions, game rules, or designs were
changed, and this session added no new test suite.
