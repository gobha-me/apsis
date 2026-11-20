// v0.20 reference for the layer-2 port gate: runs the frozen v0.20 world (no player) and prints, per month,
// the hash of a state dump in the same format as apsis::world::dump(). With --dump D it prints the dump at day D.
//   node ref/world_ref.js /path/to/logi20.js /path/to/exp20.js seed majors years [--dump D]
const [,, lpath, epath, seedS, majS, yrsS, flag, dayS] = process.argv;
const L = require(lpath), { R } = require(epath);
const W = L.create(+seedS, { rules: R, majors: +majS });
const KIND = { base: 0, house: 1, minor: 2, guild: 3, upstart: 4 };
function hashBytes(s) { let h = 0x811c9dc5, h2 = 0x1b873593; for (let i = 0; i < s.length; i++) { const c = s.charCodeAt(i); h = Math.imul(h ^ c, 0x01000193) >>> 0; h2 = Math.imul(h2 ^ c, 0x5bd1e995) >>> 0; }
  return h.toString(16).padStart(8, '0') + h2.toString(16).padStart(8, '0'); }
function dump(W) {
  const st = W.state, pw = st.pw, o = []; const arr = a => a.join(',');
  o.push('day ' + st.day);
  for (const s of st.S) o.push('S' + s.id + ' o' + s.owner + ' t' + s.tier + ' p' + s.pop + ' k' + arr(s.stock) + ' g' + s.stage + ' m' + s.mood + ' e' + s.ema + ' w' + s.wear + ' r' + arr(s.raw) + ' d' + arr(s.dep.map(d => d.res)));
  for (const f of st.F) { let c = 0; for (const x of f.cargo) c += x[0] + x[1] + x[2] + x[3]; o.push('F' + f.id + ' r' + f.r + ' l' + f.leg + ' t' + f.t + ' c' + c + ' x' + (f.dead || 0) + (f.retire || 0)); }
  st.RT.forEach((r, i) => o.push('R' + i + ' P' + r.P + ' Q' + r.Q + ' g' + r.groups.map(g => g.s + '@' + g.pos + ';').join('') + ' u' + r.units.map(u => u.s + ';').join('')));
  for (const id of Object.keys(pw.ag).map(Number).sort((a, b) => a - b)) { const a = pw.ag[id]; o.push('A' + id + ' k' + KIND[a.kind] + ' T' + a.T + ' S' + a.S + ' a' + a.alive + ' s' + a.seat + ' p' + (a.patron === undefined ? -1 : a.patron) + ' c' + (a.crush || 0)); }
  const rk = Object.keys(pw.rel).map(k => k.split(',').map(Number)).sort((x, y) => x[0] - y[0] || x[1] - y[1]);
  for (const [a, b] of rk) o.push('rel' + a + ',' + b + ' ' + pw.rel[a + ',' + b]);
  for (const c of pw.con) o.push('C' + c.x + ' ' + c.a + '>' + c.d + ' m' + c.m + ' g' + c.margin);
  o.push('rate' + Object.keys(pw.rate || {}).map(Number).sort((a, b) => a - b).map(k => ' ' + k + ':' + pw.rate[k]).join(''));
  o.push('contract' + Object.keys(pw.contract || {}).map(Number).sort((a, b) => a - b).map(k => ' ' + k + ':' + pw.contract[k].carrier + '/' + pw.contract[k].until).join(''));
  o.push('lanes ' + W.net.lanes.length + ' own' + W.net.lanes.map(l => ' ' + (l.owner === undefined ? -1 : l.owner)).join(''));
  const bk = st.bk, n = st.S.length; o.push('ds ' + st.S.map((s, i) => bk.ds[i] || 0).join(',')); o.push('u ' + st.S.map((s, i) => bk.u[i] || 0).join(',')); o.push('kp ' + st.S.map((s, i) => bk.kp[i] === undefined ? -1 : bk.kp[i]).join(','));
  const d = st.dark; o.push('ghost' + d.g.map((g, i) => g ? ' ' + i + ':' + g : '').join('')); o.push('edge' + Object.keys(d.edge).map(Number).sort((a, b) => a - b).map(s => ' ' + s + ':' + d.edge[s]).join(''));
  st.fac.forEach((F, f) => { if (F) o.push('fac' + f + ' b' + F.base + ' s' + F.survey + ' t' + F.t); });
  const Ld = st.led; o.push('led ' + [Ld.init, Ld.prod, Ld.burn, Ld.spill, Ld.waste, Ld.loot, Ld.rawExt, Ld.rawConv, Ld.rawLoot, Ld.rawSpill].join(' '));
  return o.join('\n') + '\n';
}
if (flag === '--dump') { const D = +dayS; while (W.state.day < D) L.tick(W); process.stdout.write(dump(W)); }
else { const days = +yrsS * 360, out = [hashBytes(dump(W))]; const t0 = Date.now(); for (let d = 1; d <= days; d++) { L.tick(W); if (d % 30 === 0) out.push(hashBytes(dump(W))); }
  process.stdout.write(out.join('\n') + '\n'); process.stderr.write('ms ' + (Date.now() - t0) + '\n'); }
