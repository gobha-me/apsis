# Standalone galaxy tooling

This C++23 library and native JSON CLI implement the geometry in [issue #24](https://github.com/gobha-me/apsis/issues/24). They have no `sim/` includes or link dependencies. The library uses integer state and standard C++ facilities suitable for WebAssembly; this directory does not include a WASM viewer.

Build from the repository root with CMake and a C++23 compiler:

```sh
cmake -S galaxy -B build/galaxy -DCMAKE_BUILD_TYPE=Release
cmake --build build/galaxy
build/galaxy/galaxy-cli --seed 42 --region 2,0 --region 3,0
```

The library target is `apsis::galaxy`; include `<apsis/galaxy/map.hpp>`. Both targets compile with `-Wall -Wextra -Wpedantic`. No third-party libraries are required.

## Facade and visibility

`apsis::galaxy::Map` takes a `uint64_t` universe seed, an immutable `Config` and a format version (currently `1`). Construction reveals nothing and `raw_units_per_j()` initially returns no value. `view_region({cx, cy})` explicitly views the **entire cell**. It returns a `Status`; invalid configuration, unsupported format and failed calibration add no revelations.

`revealed_systems()` and `revealed_lanes()` return ID-sorted, facade-owned spans valid until the next mutation. System IDs are `(RegionId, slot)`, ordered by `cx`, `cy`, then slot. Lane IDs are their ordered endpoint pairs. `distance_milli_j(a, b)` returns direct Euclidean chord distance, with no value unless both IDs are revealed. Serialize mutations of each facade. Regional generation is pure; separate facades can be computed independently on workers and merged by IDs.

Private neighbor generation and calibration never reveal cells. A lane becomes visible only when both endpoint systems are revealed; viewing an adjacent cell refreshes boundary lanes. A viewer must present every requested cell as viewed, including portions outside its viewport.

## Geometry and scale

The SMBH is at `(0,0)`. Positions use 1,000 raw ticks per map unit. Defaults are an even cell side `S = 64000`, inner radius `2*S = 128000`, and outer radius `32*S = 2048000`. The annulus includes the inner boundary and excludes the outer boundary. A region's half-open bounds are `[cx*S,(cx+1)*S) × [cy*S,(cy+1)*S)`; negative positions follow mathematical floor division.

Supported configurations require even `4 <= S <= INT32_MAX` and `0 <= Rin < Rout <= INT32_MAX`. These bounds keep cell-coordinate products in signed 64-bit integers and squared distances in unsigned 64-bit integers. Other configurations return `invalid_config` before revelation.

Each region has four fixed candidate slots, `slot = 2*qy + qx`. Coordinates within a cell use `q*(S/2) + 1 + draw % (S/2 - 1)` in each axis. Candidates outside the annulus are clipped without renumbering. Each star has a circular orbit around the origin, with a raw radius and fixed-epoch position vector. There are no mass, period, motion, gravity or hazard mechanics.

Every distinct system pair at squared distance at most `S*S` has a lane. Generation needs only its cell and the eight neighboring cells. The smaller endpoint owns the lane. There is no global bridging or connectivity guarantee. Integer square roots round radii and lengths to nearest, ties upward.

The first successful view privately addresses 64 calibration counters into the bounding cell grid `[-ceil(Rout/S), ceil(Rout/S)-1]` on each axis. Cells are deduplicated without redraws, including empty cells. The calibration collects and deduplicates lanes owned by systems in sampled cells, using private one-cell halos. `raw_units_per_j()` freezes the upper median, `sorted_lengths[size/2]`; an empty sample is an explicit calibration error. Later discovery never changes this scale. Coordinates and radii remain raw ticks. Distances are reported as `roundNearest(1000 * raw_distance / raw_units_per_j)`, ties upward, with checked intermediates.

## Format 1 addressed randomness

Every draw hashes exactly nine unsigned 32-bit words, in order: `(seedLo, seedHi, 0x47414c58, formatVersion, cxBits, cyBits, slot, purpose, counter)`. Signed cell coordinates convert to their unsigned 32-bit representations. The domain is `GALX`; purposes are `1` for x jitter, `2` for y jitter, `3` for calibration x and `4` for calibration y. Jitter uses counter `0`. Calibration uses zero cell coordinates and slot, and counters `0` through `63`.

All arithmetic below wraps modulo `2^32`:

```text
mix32(v):
  v ^= v >> 16
  v *= 0x85ebca6b
  v ^= v >> 13
  v *= 0xc2b2ae35
  v ^= v >> 16
  return v

h = 0x6d2b79f5
for each of the nine words w:
  h = mix32(h ^ mix32(w + 0x9e3779b9))
draw = h
```

Calibration maps each draw modulo the grid width, then subtracts `ceil(Rout/S)`. This hash, explicit word order, purpose tags, selection, four-slot scheme, lane rule and rounding are frozen by format version `1`. Draws never depend on call count, generation order, caches or thread scheduling.

## CLI JSON

`--seed N` is required exactly once; `--region cx,cy` is repeatable. Integers are strict decimal values: seed is unsigned 64-bit, cell coordinates are signed 32-bit. With no regions, JSON contains empty reveal arrays and a null J. `--help` prints usage.

Output records `format_version`, the seed as a decimal **string** (preserving all 64 bits in JavaScript), configuration and raw-tick scale, status, immutable `raw_units_per_j`, sorted unique `viewed_regions`, and ID-sorted `systems` and `lanes`. Systems include raw positions and orbital geometry; lanes include raw length and `milli_j`. Argument errors produce a JSON error on stderr and exit `2`; generator status failures produce map JSON on stdout and exit `1`. Successful output exits `0`.

## Integration boundary

This is geometry tooling. A future adapter to `Pt`, `NetSt`, `Lane` and `Net.J` depends on the simulation import and must respect its exact whole-map median contract rather than substitute this sampled estimate. Geometry does not supply simulation owners, tiers, homes/hubs, routes, hazards or eager all-pairs state. No such adapter or gameplay defaults are included here.

Determinism, invariance and native/WASM tests belong to a separate `Agent-Role: tests` session and stacked tests PR. A small WASM galaxy-map viewer is a later follow-up, distinct from issue #13's replay viewer.
