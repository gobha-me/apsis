// v0.20 per-pilot-year speed for the hauler and crisis bots: 7 curated seeds x 3 starts x 40 years, career time only.
//   node ref/pilot_bench_js.js logi20.js bots20.js exp20.js
const [,, lp, bp, ep] = process.argv, L = require(lp), B = require(bp), { R } = require(ep);
const S = [[333860, 4, [25, 95, 160]], [110478, 4, [10, 80, 160]], [697804, 4, [30, 90, 180]], [57206, 4, [53, 64, 75]], [632322, 4, [10, 90, 165]], [517908, 4, [80, 160, 230]], [552723, 5, [75, 120, 200]]];
for (const style of ['hauler', 'crisis']) { let ms = 0, n = 0;
  for (const [s, m, starts] of S) for (const y of starts) { const W = L.create(s, { rules: R, majors: m }); while (W.state.day < y * 360) L.tick(W);
    const t0 = Date.now(); B.drive(W, B.POLICIES[style], 40 * 360, W.state.day + 40 * 360, W.net.core.home, { home: W.net.core.home }); ms += Date.now() - t0; n++; }
  console.log(JSON.stringify({ version: 'v0.20 (Node)', style, careers: n, pilotYears: n * 40, msPerPilotYear: +(ms / (n * 40)).toFixed(2) })); }
