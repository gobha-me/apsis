// System beacons (upkeep, lapse, relight, prospectors) and the dark edge (smugglers, ghosts, edge patrols): v0.20 world parts.
#include <algorithm>
#include "ops.hpp"

namespace apsis::world {
namespace {
const Agent* agc(const World& W, int id) { auto it = W.pw.ag.find(id); return it == W.pw.ag.end() ? nullptr : &it->second; }
bool watcher(const World& W, int h) { const Agent* a = agc(W, h); return isAgent(h) && a && a->alive; }
int bandOf(const WParams& P, i64 drv, uint32_t v) { const auto& c = P.bandPm[(size_t)drv]; i64 x = v % 1000; for (size_t b = 0; b < c.size(); ++b) { if (x < c[b]) return (int)b; x -= c[b]; } return (int)c.size() - 1; }
int keeperS(const World& W, int x) { const int o = W.S[x].owner; if (watcher(W, o)) return o; const int k = W.bk.kp[(size_t)x]; if (k >= 0 && watcher(W, k)) return k; return k == -3 ? -3 : -1; }
void lapseJ(World& W, int x, int /*h*/) {
  auto& bk = W.bk; const auto& P = W.P; bk.ds[(size_t)x] = W.day + 1; bk.u[(size_t)x] = 0; bk.lapsed++; if (bk.kp[(size_t)x] == -3) bk.kp[(size_t)x] = -1; W.pw.pathV++;
  for (auto& s : W.S) {
    if (s.tier == 0 || !isAgent(s.owner) || s.owner == HOUSE || !bk.ds[(size_t)s.id]) continue; const Pt& pp = W.net.st[(size_t)s.id].pos; if (!pp.ok) continue;
    bool reach = false; for (const auto& y : W.S) if (y.id != s.id && !bk.ds[(size_t)y.id] && W.net.st[(size_t)y.id].pos.ok && edist(pp, W.net.st[(size_t)y.id].pos) <= P.hopNpc) { reach = true; break; }
    if (reach) continue; const int was = s.owner; bool other = false; for (const auto& y : W.S) if (&y != &s && y.owner == was) { other = true; break; } if (!other) continue;
    if (auto it = W.fac.find(was); it != W.fac.end() && it->second.base) it->second.base = std::max<i64>(1, it->second.base - depSum(s));
    s.owner = s.pop * 100 >= s.basePop * P.indPopPct ? IND : ABAN; s.dropped = W.day / 30; if (auto it = W.pw.ag.find(was); it != W.pw.ag.end()) it->second.drop[s.id] = s.dropped;
    if (s.owner == ABAN) s.stage = 3; bk.darkCut++; s.acct.owned = 0; s.acct.cost.clear(); s.acct.ret.clear();
  }
}
void relightJ(World& W, int x, int h) { auto& bk = W.bk; bk.ds[(size_t)x] = 0; bk.u[(size_t)x] = 0; bk.relit++; if (h >= 0 && W.S[x].owner != h) bk.kp[(size_t)x] = h; W.pw.pathV++; }
void addGhost(World& W, int s, int src) { auto& d = W.dark; if (d.cur.size() <= (size_t)s) { d.cur.resize((size_t)s + 1, A3{}); d.curOn.resize((size_t)s + 1, 0); } d.curOn[(size_t)s] = 1; d.cur[(size_t)s][(size_t)src]++; }
void npcArrive(World& W, int s, int src, uint32_t hv) {
  const auto& P = W.P; auto& d = W.dark; const int h = (size_t)s < W.S.size() ? W.S[(size_t)s].owner : -1; if (!watcher(W, h)) return;
  int b = bandOf(P, P.npcDrive, hv); if (src == 2 && b == 0) b = 1; const i64 ps = psOf(W, h);
  if (b == 0) return;
  auto e = d.edge.find(s);
  if (b < 4 && e != d.edge.end() && e->second == h && (!W.rules.intents || d.edgeUntil[s] >= W.day) && hashN((i64)hv, 3) % 1000 < (uint32_t)(P.edgeScanPm[(size_t)b] * ps / 100)) { d.edgeHit[s] |= (src == 1 ? 2 : 4); return; }
  if (hashN((i64)hv, 4) % 1000 < (uint32_t)(P.detectPm[(size_t)b] * P.quietPct[(size_t)P.npcDrive] / 100 * ps / 100)) addGhost(W, s, src);
}
void cargoMonth(World& W, i64 mo) {
  const auto& P = W.P;
  for (auto& s : W.S) { if (s.stage < 1 || s.owner == ABAN) continue; int k = 0; i64 wr = 1000000000; for (int g = 0; g < 4; ++g) { const i64 r = s.stock[g] * 1000 / s.cap[g]; if (r < wr) { wr = r; k = g; } }
    Station* src = nullptr; i64 sr = -1;
    for (const auto& l : W.net.lanes) { if (l.a != s.id && l.b != s.id) continue; const int yi = l.a == s.id ? l.b : l.a; if ((size_t)yi >= W.S.size()) continue; Station& y = W.S[(size_t)yi]; if (y.owner == ABAN) continue;
      const i64 r = y.stock[k] * 1000 / y.cap[k]; if (r > sr || (r == sr && y.id < src->id)) { sr = r; src = &y; } }
    if (!src || sr * 100 < P.cargoSrcPct * 1000) continue;
    for (i64 t = 0; t < P.cargoTrips; ++t) { const i64 u = std::min({P.cargoLot, src->stock[k] - src->cap[k] * P.cargoSrcPct / 100, s.cap[k] - s.stock[k]}); if (u <= 0) break;
      src->stock[k] -= u; s.stock[k] += u; npcArrive(W, s.id, 1, hashN(W.seed, K_MAP, 3, s.id, mo, t)); } }
}
}  // namespace

bool laneDarkI(const World& W, int li) { const auto& l = W.net.lanes[(size_t)li]; const auto& ds = W.bk.ds; return ((size_t)l.a < ds.size() && ds[(size_t)l.a]) || ((size_t)l.b < ds.size() && ds[(size_t)l.b]); }
i64 psOf(const World& W, int h) { auto it = W.dark.ps.find(h); return it != W.dark.ps.end() ? it->second : W.P.psMin; }

void mapMonthJ(World& W) {
  const auto& P = W.P; auto& bk = W.bk; auto& pw = W.pw; const i64 mo = W.day / 30;
  std::map<int, std::vector<int>> kept; std::map<int, int> strain;
  for (size_t si = 0; si < W.S.size(); ++si) { const int x = (int)si; if (bk.ds[si]) continue; const int k = keeperS(W, x);
    if (k == -3) { if (++bk.u[si] > P.prospKeep) { bk.kp[si] = -1; bk.u[si] = 0; } continue; }
    if (k < 0) { bk.u[si]++; if (bk.u[si] >= P.unkeptMonths) lapseJ(W, x, -1); continue; }
    bk.u[si] = 0; kept[k].push_back(x); }
  for (auto& [h, a] : pw.ag) { if (!a.alive) continue; auto& mine = kept[h]; const i64 bill = (i64)mine.size() * P.bcnUpkeepJ;
    const bool broke = a.T < bill * P.brokeMonths; bool war = false; for (const auto& c : pw.con) if (c.a == h || c.d == h) war = true; const bool over = (i64)mine.size() > std::max<i64>(1, a.S / P.sPerLaneJ);
    strain[h] = broke ? 1 : war ? 2 : over ? 3 : 0;
    if (mine.empty()) continue; a.T -= bill; bk.upkeep += bill;
    const i64 n = (broke ? 1 : 0) + (war ? 1 : 0) + (over ? 1 : 0);
    if (n && hashN(W.seed, K_MAP, 1, h, mo) % 1000 < (uint32_t)(P.lapsePm * n)) { std::vector<int> v = mine;
      std::stable_sort(v.begin(), v.end(), [&](int p, int q) { const i64 tp = W.S[p].tier, tq = W.S[q].tier; if (tp != tq) return tp < tq; const i64 dp = coreDist(W, h, p), dq = coreDist(W, h, q); if (dp != dq) return dq < dp; return q < p; });
      lapseJ(W, v.back(), h); } }
  if (!W.rules.intents) for (auto& [h, a] : pw.ag) { if (!a.alive || strain[h] || !isAgent(h)) continue; const i64 nm = kept.count(h) ? (i64)kept[h].size() : 0, cap = std::max<i64>(1, a.S / P.sPerLaneJ);
    if (nm >= cap - P.relightGap) continue;
    if (hashN(W.seed, K_MAP, 2, h, mo) % 1000 >= (uint32_t)P.relightPm) continue;
    std::vector<int> cand; for (const auto& y : W.S) { if (!bk.ds[(size_t)y.id]) continue; const Agent* o = agc(W, y.owner);
      if (y.owner == h || ((!isAgent(y.owner) || !o || !o->alive) && coreDist(W, h, y.id) <= P.hopNpc * 2)) cand.push_back(y.id); }
    std::stable_sort(cand.begin(), cand.end(), [&](int p, int q) { const int op = W.S[p].owner == h ? 0 : 1, oq = W.S[q].owner == h ? 0 : 1; if (op != oq) return op < oq; const i64 dp = coreDist(W, h, p), dq = coreDist(W, h, q); if (dp != dq) return dp < dq; return p < q; });
    if (cand.empty()) continue; const int x = cand[0]; const i64 cost = P.relightJ; if (a.T < cost * P.relightReserve) continue;
    a.T -= cost; if (W.S[x].owner != h) bk.lit++; relightJ(W, x, h); }
  // v0.20 has no prospectors (rule `prospect` is not in its rule set)
}

void darkMonth(World& W) {
  const auto& P = W.P; auto& d = W.dark; const Net& net = W.net; const i64 mo = W.day / 30;
  { std::map<int, i64> sum; for (size_t ri = 0; ri < net.routes.size(); ++ri) { const int o = W.S[net.routes[ri].hub].owner; if (isAgent(o)) sum[o] += W.RT[ri].P; }
    std::vector<int> ks; i64 tot = 0; for (auto& [k, v] : sum) if (v > 0) { ks.push_back(k); tot += v; }
    const i64 mean = ks.empty() ? 1 : tot / (i64)ks.size(); d.ps.clear(); for (int k : ks) d.ps[k] = clampI(sum[k] * 100 / std::max<i64>(1, mean), P.psMin, P.psMax); }
  if (d.flow.size() < net.lanes.size()) d.flow.resize(net.lanes.size(), 0);
  for (size_t li = 0; li < net.lanes.size(); ++li) { const Lane& l = net.lanes[li]; const bool dk = laneDarkI(W, (int)li);
    const bool front = ((size_t)l.a < W.S.size() && W.S[(size_t)l.a].tier >= 2) || ((size_t)l.b < W.S.size() && W.S[(size_t)l.b].tier >= 2);
    const i64 fl0 = P.darkBasePm + (front ? P.darkFrontierPm : 0) + (dk ? P.darkLaneBasePm : 0);
    const i64 fl = net.laneN0 ? fl0 * (net.laneN0 + (i64)net.st.size() - net.n0) / (i64)net.lanes.size() : fl0;
    const i64 n = d.flow[li] * (dk ? P.darkLanePm : P.darkPm) / (1000 * P.darkLot) + fl / 1000 + (hashN(W.seed, K_DARK, 7, li, mo) % 1000 < (uint32_t)(fl % 1000) ? 1 : 0);
    for (i64 k = 0; k < n; ++k) { const uint32_t hv = hashN(W.seed, K_DARK, 9, li, mo, k); npcArrive(W, hv % 2 ? l.a : l.b, 1, hashN((i64)hv, 5)); } }
  cargoMonth(W, mo);
  for (size_t ri = 0; ri < net.routes.size(); ++ri) { const auto& r = net.routes[ri]; const auto& gs = W.RT[ri].groups;
    for (size_t gi = 0; gi < gs.size(); ++gi) { const auto& g = gs[gi]; if (g.s <= 0) continue;
      for (const auto& lg : r.legs) if (g.pos >= lg.from && g.pos <= lg.from + lg.len) { npcArrive(W, g.pos - lg.from <= lg.len / 2 ? lg.a : lg.b, 2, hashN(W.seed, K_DARK, 10, ri, gi, mo)); break; } } }
  d.flow.assign(net.lanes.size(), 0);
  const size_t N = W.S.size(); if (d.g.size() < N) d.g.resize(N, 0); if (d.lastNpc.size() < N) d.lastNpc.resize(N, INT64_MIN);
  for (size_t s = 0; s < N; ++s) { const bool on = s < d.curOn.size() && d.curOn[s]; const A3 c = on ? d.cur[s] : A3{}; const i64 n = c[0] + c[1] + c[2];
    if (on && c[1] + c[2]) d.lastNpc[s] = mo; const i64 g = d.g[s]; if (g || n) d.g[s] = g - g / P.ghostDecay + n * 10; }
  d.cur.clear(); d.curOn.clear(); d.edgeHit.clear();
  if (W.rules.intents) { for (auto it = d.edge.begin(); it != d.edge.end();) { if (d.edgeUntil[it->first] < W.day) { d.edgeUntil.erase(it->first); it = d.edge.erase(it); } else ++it; } return; }
  d.edge.clear();
  std::map<int, std::vector<std::pair<i64, int>>> want;
  for (size_t s = 0; s < N; ++s) { const int h = W.S[s].owner; if (!watcher(W, h)) continue; const i64 w = (d.g[s] * (W.S[s].pop / 1000) / 10) * P.edgePullPct / 100; if (w >= P.edgeW) want[h].push_back({w, (int)s}); }
  for (auto& [h, v] : want) { Agent& a = W.pw.ag.at(h); std::stable_sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first != y.first ? x.first > y.first : x.second < y.second; });
    for (size_t i = 0; i < v.size() && (i64)i < P.edgeMax; ++i) { if (a.T < P.edgeCost * P.edgeReserve) break; a.T -= P.edgeCost; d.edge[v[i].second] = h; d.edgeN++; d.edgeCost += P.edgeCost; } }
}

}  // namespace apsis::world
