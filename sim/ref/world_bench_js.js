// v0.20 world-only speed (no player), 7 curated seeds x 300 years, best of 3: node ref/world_bench_js.js logi20.js exp20.js
const [,, lp, ep] = process.argv, L = require(lp), { R } = require(ep);
const S = [[333860, 4], [110478, 4], [697804, 4], [57206, 4], [632322, 4], [517908, 4], [552723, 5]]; let best = 1e18;
for (let rep = 0; rep < 3; rep++) { const t0 = Date.now(); for (const [s, m] of S) { const W = L.create(s, { rules: R, majors: m }); for (let d = 0; d < 300 * 360; d++) L.tick(W); } best = Math.min(best, (Date.now() - t0) / 1000); }
console.log(JSON.stringify({ mode: 'v0.20 (Node)', years: 2100, s: +best.toFixed(3), yearsPerS: Math.round(2100 / best), msPerYear: +(best * 1000 / 2100).toFixed(3) }));
