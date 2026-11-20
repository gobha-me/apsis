// Writes ref/vectors.txt from the frozen v0.20 code: hashN, strHash, apportion, cappedApportion.
// node ref/rng_ref.js /path/to/logi20.js
const fs = require('fs'), path = process.argv[2], src = fs.readFileSync(path, 'utf8'), L = require(path);
const strHash = eval('(' + src.match(/function strHash\(s\) \{[^\n]*?return h >>> 0; \}/)[0] + ')');
let s = 12345; const r = () => (s = (Math.imul(s, 1103515245) + 12345) >>> 0);
const out = [];
for (let k = 0; k < 20000; k++) {
  const n = 1 + r() % 7, a = [];
  for (let i = 0; i < n; i++) { const m = r() % 5; a.push(m === 0 ? 0 : m === 1 ? -(r() % 100000) : m === 2 ? r() : m === 3 ? r() * 1024 + (r() % 1024) : r() % 1000); }
  out.push('H ' + n + ' ' + a.join(' ') + ' ' + L.hashN(...a));
}
for (const t of ['net', 'icpt', 'pos', 'stock', 'contract', 'a', '', 'The quick brown fox', 'apsis-drift-v2']) out.push('S ' + JSON.stringify(t) + ' ' + strHash(t));
for (let k = 0; k < 5000; k++) {
  const n = 1 + r() % 8, w = [], c = [];
  for (let i = 0; i < n; i++) { w.push(r() % 4 === 0 ? 0 : r() % 5000); c.push(r() % 3000); }
  if (k % 7 === 0) w.fill(w[0]);
  const total = r() % 20000 - 50;
  out.push('A ' + n + ' ' + w.join(' ') + ' ' + total + ' ' + L.apportion(w, total).join(' '));
  out.push('C ' + n + ' ' + w.join(' ') + ' ' + c.join(' ') + ' ' + total + ' ' + L.cappedApportion(w, c, total).join(' '));
}
fs.writeFileSync(__dirname + '/vectors.txt', out.join('\n') + '\n');
console.log('vectors', out.length);
