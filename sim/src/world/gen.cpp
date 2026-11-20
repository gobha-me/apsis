// World generation: v0.20 genCoreMap (with jump hops and hazards), bodies, placement and fairness.
#include <algorithm>
#include <cmath>
#include <numeric>
#include "gen.hpp"
#include "../core/rng.hpp"

namespace apsis::world {

const uint32_t K_NET = strHash("net"), K_CORE = strHash("core"), K_BODY = strHash("body"), K_FAIR = strHash("fair"), K_JUMP = strHash("jump");

i64 edist(const Pt& p, const Pt& q) { const i64 dx = p.x - q.x, dy = p.y - q.y; return (i64)std::floor(std::sqrt((double)(dx * dx + dy * dy)) + 0.5); }

namespace {
const i64 DIRS[16][2] = {{1000, 0}, {924, 383}, {707, 707}, {383, 924}, {0, 1000}, {-383, 924}, {-707, 707}, {-924, 383}, {-1000, 0}, {-924, -383}, {-707, -707}, {-383, -924}, {0, -1000}, {383, -924}, {707, -707}, {924, -383}};
Pt mk(i64 x, i64 y) { Pt p; p.x = x; p.y = y; p.ok = true; return p; }
Pt dir64(i64 a) { a = ((a % 64) + 64) % 64; const auto& b = DIRS[a >> 2]; const auto& c = DIRS[((a >> 2) + 1) % 16]; const i64 f = a & 3; return mk(b[0] + idiv((c[0] - b[0]) * f, 4), b[1] + idiv((c[1] - b[1]) * f, 4)); }
bool inR(const Pt& p, const Pt& c, i64 r) { const i64 dx = p.x - c.x, dy = p.y - c.y; return dx * dx + dy * dy <= r * r; }
bool segHits(const Pt& p, const Pt& q, const Pt& c, i64 r) {
  const i64 abx = q.x - p.x, aby = q.y - p.y, apx = c.x - p.x, apy = c.y - p.y, ab2 = abx * abx + aby * aby, dot = apx * abx + apy * aby;
  if (ab2 == 0 || dot <= 0) return apx * apx + apy * apy <= r * r;
  if (dot >= ab2) { const i64 bx = c.x - q.x, by = c.y - q.y; return bx * bx + by * by <= r * r; }
  const i64 cr = apx * aby - apy * abx; return cr * cr <= r * r * ab2;
}
// a counter-based draw sequence: v0.20's { int, range } closures
struct Seq { uint32_t seed, k; int64_t salt; bool hasSalt; int64_t n = 0;
  i64 next() { return hasSalt ? hashN(seed, k, salt, n++) : hashN(seed, k, n++); }
  i64 inti(i64 m) { return next() % m; }
  i64 range(i64 a, i64 b) { return a + next() % (b - a + 1); } };
}  // namespace
Pt dir64pub(i64 a) { return dir64(a); }

void floyd(Net& net) {
  const size_t N = net.st.size(); const i64 INF = 1000000000;
  net.dist.assign(N, std::vector<i64>(N, INF)); net.next.assign(N, std::vector<int>(N, -1));
  for (size_t i = 0; i < N; ++i) { net.dist[i][i] = 0; net.next[i][i] = (int)i; }
  for (const auto& l : net.lanes) if (l.len < net.dist[l.a][l.b]) { net.dist[l.a][l.b] = net.dist[l.b][l.a] = l.len; net.next[l.a][l.b] = l.b; net.next[l.b][l.a] = l.a; }
  for (size_t k = 0; k < N; ++k) for (size_t i = 0; i < N; ++i) { const i64 dik = net.dist[i][k]; auto& di = net.dist[i]; const auto& dk = net.dist[k];
    for (size_t j = 0; j < N; ++j) if (dik + dk[j] < di[j]) { di[j] = dik + dk[j]; net.next[i][j] = net.next[i][k]; } }
}
std::vector<int> pathOf(const Net& net, int a, int b) { std::vector<int> p{a}; while (a != b) { a = net.next[a][b]; p.push_back(a); } return p; }

Route buildRoute(const Net& net, int h, const std::vector<int>& stops, const WParams& P, bool relay) {
  std::vector<int> nodes{h}; int cur = h; std::vector<int> all = stops; all.push_back(h);
  for (int s : all) { auto p = pathOf(net, cur, s); nodes.insert(nodes.end(), p.begin() + 1, p.end()); cur = s; }
  Route r; i64 pos = 0;
  for (size_t k = 0; k + 1 < nodes.size(); ++k) { Leg lg; lg.a = nodes[k]; lg.b = nodes[k + 1]; lg.from = pos; lg.len = net.dist[nodes[k]][nodes[k + 1]]; r.legs.push_back(lg); pos += lg.len; }
  for (size_t si = 0; si < stops.size(); ++si) {
    bool taken = false; for (const auto& m : r.legs) if (m.stopIdx == (int)si) taken = true;
    if (taken) continue;
    for (auto& lg : r.legs) if (lg.b == stops[si] && lg.stopIdx < 0) { lg.stopIdx = (int)si; break; }
  }
  i64 far = 0; bool first = true; for (int x : stops) { if (first || net.dist[h][x] > far) far = net.dist[h][x]; first = false; }
  const int fr = relay ? 1 : (int)std::max<i64>(1, std::min<i64>(3, 3 - far / P.distStep));
  r.id = (int)net.routes.size(); r.hub = h; r.owner = net.st[h].owner; r.stops = stops; r.len = pos; r.far = far; r.freighters = fr; r.base = fr; r.relay = relay ? 1 : 0;
  return r;
}

static std::vector<Hazard> genHazards(uint32_t seed, const WParams& P, i64 side, const Pt& core, const std::vector<Pt>& homes, const std::vector<i64>& ang) {
  std::vector<Hazard> hz;
  auto inMap = [&](i64 x, i64 y) { return mk(clampI(x, 2, side - 2), clampI(y, 2, side - 2)); };
  auto rnd = [&](i64 t, i64 k, i64 c) { return mk(3 + hashN(seed, K_JUMP, t, k, c, 1) % (side - 5), 3 + hashN(seed, K_JUMP, t, k, c, 2) % (side - 5)); };
  auto clear = [&](const Pt& q, i64 gap) { for (const auto& h : homes) if (inR(q, h, gap)) return false; return true; };
  auto place = [&](i64 t, i64 k, i64 gap) { for (i64 c = 0; c < 40; ++c) { const Pt q = rnd(t, k, c); if (clear(q, gap)) return q; } return rnd(t, k, 0); };
  if (homes.size() > 2) { const i64 NS = (i64)homes.size() - 1, rr = idiv(side * P.ringPct, 100);
    for (i64 i = 1; i <= NS; ++i) { const i64 a2 = i == NS ? ang[1] + 64 : ang[(size_t)i + 1]; const Pt d = dir64(idiv(ang[(size_t)i] + a2, 2));
      Hazard h; h.t = 0; h.p = inMap(core.x + idiv(d.x * rr, 1000), core.y + idiv(d.y * rr, 1000)); h.r = P.nebR; h.acc = P.nebAcc; h.rift = 1; hz.push_back(h); } }
  for (i64 k = 0; k < P.hzNeb; ++k) { Hazard h; h.t = 0; h.p = place(1, k, 10); h.r = P.nebR; h.acc = P.nebAcc; hz.push_back(h); }
  for (i64 k = 0; k < P.hzWell; ++k) { Hazard h; h.t = 1; h.p = place(2, k, 10); h.r = P.wellR; h.sub = P.wellSub; hz.push_back(h); }
  for (i64 k = 0; k < P.hzRog; ++k) { Hazard h; h.t = 3; h.p = place(4, k, 6); h.r = P.rogR; h.acc = P.rogAcc; hz.push_back(h); }
  for (size_t i = 0; i < hz.size(); ++i) hz[i].id = (int)i;
  return hz;
}

static void hopHaz(const std::vector<Pt>& pts, const WParams& P, const std::vector<Hazard>& hz, Lane& l) {
  i64 days = 0, acc = 0, wait = 0; const Pt& pa = pts[(size_t)l.a]; const Pt& pb = pts[(size_t)l.b];
  for (const auto& h : hz) {
    if (h.t == 1) { if (inR(pa, h.p, h.r)) days += h.sub; if (inR(pb, h.p, h.r)) days += h.sub; }
    else if (h.t == 0) { if (segHits(pa, pb, h.p, h.r)) acc += h.acc; }
    else if (h.t == 2) { if (inR(pa, h.p, h.r) || inR(pb, h.p, h.r)) wait += idiv((h.per - h.win) * (h.per - h.win), 2 * h.per); }
  }
  l.hw = (days + wait) * 100 + idiv(acc * P.accW, 100); l.hd = days; l.ha = acc;
}

std::vector<Lane> hopList(const std::vector<Pt>& pts, const WParams& P, const std::vector<Hazard>& hz) {
  const int N = (int)pts.size(); std::vector<Lane> out;
  for (int a = 0; a < N; ++a) for (int b = a + 1; b < N; ++b) { if (!pts[a].ok || !pts[b].ok) continue; const i64 d = edist(pts[a], pts[b]); if (d <= P.hopNpc) { Lane l; l.a = a; l.b = b; l.len = std::max<i64>(3, d); out.push_back(l); } }
  std::vector<int> par((size_t)N); std::iota(par.begin(), par.end(), 0);
  std::function<int(int)> fd = [&](int x) { return par[x] == x ? x : (par[x] = fd(par[x])); };
  for (const auto& l : out) par[fd(l.a)] = fd(l.b);
  for (;;) { bool have = false; int ba = 0, bb = 0; i64 bd = 0;
    for (int a = 0; a < N; ++a) for (int b = 0; b < N; ++b) if (pts[a].ok && pts[b].ok && fd(a) != fd(b)) { const i64 d = edist(pts[a], pts[b]);
      if (!have || d < bd || (d == bd && (a < ba || (a == ba && b < bb)))) { have = true; ba = a; bb = b; bd = d; } }
    if (!have) break;
    Lane l; l.a = std::min(ba, bb); l.b = std::max(ba, bb); l.len = bd; l.lng = 1; out.push_back(l); par[fd(ba)] = fd(bb); }
  for (auto& l : out) hopHaz(pts, P, hz, l);
  std::stable_sort(out.begin(), out.end(), [](const Lane& x, const Lane& y) { return x.a != y.a ? x.a < y.a : x.b < y.b; });
  return out;
}

Net genCoreMap(uint32_t seed, const WParams& P) {
  Seq R{seed, K_CORE, 0, false};
  const i64 M = clampI(P.majors, 3, 6), side = P.mapSide[(size_t)M] ? P.mapSide[(size_t)M] : 50 + 15 * M, NS = M - 1, F = NS * P.frontierPer + P.frontierExtra, mid = idiv(side, 2);
  auto inMap = [&](const Pt& q) { return mk(clampI(q.x, 2, side - 2), clampI(q.y, 2, side - 2)); };
  auto at = [&](const Pt& o, i64 a, i64 r) { const Pt d = dir64(a); return inMap(mk(o.x + idiv(d.x * r, 1000), o.y + idiv(d.y * r, 1000))); };
  const i64 cx = mid + R.range(-3, 3); const i64 cy = mid + R.range(-3, 3); const Pt core = mk(cx, cy);
  const i64 a0 = R.inti(64), ringR = idiv(side * P.ringPct, 100);
  std::vector<Pt> homes{core}; std::vector<i64> ang{0};
  for (i64 i = 0; i < NS; ++i) { const i64 a = a0 + idiv(64 * i, NS) + R.range(-2, 2); ang.push_back(a); const i64 rr = ringR + R.range(-3, 3); homes.push_back(at(core, a, rr)); }
  std::vector<Pt> pts = homes; std::vector<int> tier(homes.size(), 0), owner, reg; std::vector<std::string> name;
  for (size_t i = 0; i < homes.size(); ++i) { owner.push_back(MAJOR_IDS[i]); name.push_back(i ? std::string("Home-") + "ABCDEF"[i] : "Core"); reg.push_back((int)i); }
  auto place = [&](const Pt& h, i64 a, i64 r) { for (i64 t = 0; t < 40; ++t) { const Pt q = at(h, a + (t % 2 ? t : -t), r); bool ok = true; for (const auto& z : pts) if (edist(z, q) < 5) { ok = false; break; } if (ok) return q; } return at(h, a, r + 4); };
  for (i64 c = 0; c < P.coreCluster; ++c) { const i64 r = R.range(P.clusterR[0], P.clusterR[1]); const Pt q = place(core, a0 + 8 + idiv(64 * c, P.coreCluster), r); pts.push_back(q); tier.push_back(1); owner.push_back(MAJOR_IDS[0]); name.push_back("Core-" + std::to_string(c + 1)); reg.push_back(0); }
  for (i64 i = 1; i <= NS; ++i) for (int c = 0; c < 2; ++c) { const i64 a = ang[(size_t)i] + (c ? 9 : -9) + R.range(-2, 2); const i64 r = R.range(P.clusterR[0], P.clusterR[1]); const Pt q = place(homes[(size_t)i], a, r); pts.push_back(q); tier.push_back(1); owner.push_back(MAJOR_IDS[i]); name.push_back(std::string(1, "ABCDEF"[i]) + "-" + std::to_string(c + 1)); reg.push_back((int)i); }
  auto nearest = [&](const Pt& q) { size_t b = 0; for (size_t i = 1; i < homes.size(); ++i) if (edist(homes[i], q) < edist(homes[b], q)) b = i; return (int)b; };
  for (i64 f = 0; f < F; ++f) {
    Pt ok; bool got = false;
    for (int t = 0; t < 500 && !got; ++t) { const i64 x = R.range(3, side - 3); const i64 y = R.range(3, side - 3); const Pt q = mk(x, y); const int nh = nearest(q);
      if (nh > 0 && edist(core, q) >= P.coreExclude) { bool c1 = true; for (const auto& h : homes) if (edist(h, q) < P.frontierFromHome) { c1 = false; break; }
        if (c1 && edist(homes[(size_t)nh], q) <= P.frontierMaxD) { bool c2 = true; for (const auto& z : pts) if (edist(z, q) < P.frontierGap) { c2 = false; break; } if (c2) { ok = q; got = true; } } } }
    if (!got) { const i64 i = 1 + (f % NS); const i64 a = ang[(size_t)i] + R.range(-12, 12); ok = place(homes[(size_t)i], a, P.frontierFromHome + 4); }
    pts.push_back(ok); tier.push_back(2); owner.push_back(IND); name.push_back("Frontier-" + std::to_string(f + 1)); reg.push_back(std::max(1, nearest(ok)));
  }
  const int N = (int)pts.size(); Net net;
  for (int i = 0; i < N; ++i) { NetSt s; s.id = i; s.tier = tier[i]; s.name = name[i]; s.owner = owner[i]; s.pos = pts[i]; s.region = reg[i]; net.st.push_back(s); }
  auto allowed = [&](int a, int b) { return reg[a] == reg[b] || reg[a] == 0 || reg[b] == 0; };
  std::vector<Lane> lanes;
  auto has = [&](int a, int b) { for (const auto& l : lanes) if ((l.a == a && l.b == b) || (l.a == b && l.b == a)) return true; return false; };
  auto lane = [&](int a, int b) { if (a != b && !has(a, b)) { Lane l; l.a = std::min(a, b); l.b = std::max(a, b); l.len = std::max<i64>(3, edist(pts[a], pts[b])); lanes.push_back(l); } };
  for (int i = 0; i < N; ++i) { std::vector<int> o; for (int j = 0; j < N; ++j) if (j != i && allowed(i, j)) o.push_back(j);
    std::stable_sort(o.begin(), o.end(), [&](int a, int b) { const i64 da = edist(pts[i], pts[a]), db = edist(pts[i], pts[b]); return da != db ? da < db : a < b; });
    if (o.size() > 0) lane(i, o[0]); if (o.size() > 1) lane(i, o[1]); }
  { std::vector<int> inT{0}; std::vector<char> in((size_t)N, 0); in[0] = 1;
    while ((int)inT.size() < N) { bool have = false; int ba = 0, bb = 0; i64 bd = 0;
      for (int a : inT) for (int b = 0; b < N; ++b) if (!in[b] && allowed(a, b)) { const i64 d = edist(pts[a], pts[b]); if (!have || d < bd || (d == bd && (a < ba || (a == ba && b < bb)))) { have = true; ba = a; bb = b; bd = d; } }
      lane(ba, bb); inT.push_back(bb); in[bb] = 1; } }
  for (int i = 1; i <= NS; ++i) { bool gw = false; for (const auto& l : lanes) if ((reg[l.a] == 0) != (reg[l.b] == 0) && (reg[l.a] == i || reg[l.b] == i)) { gw = true; break; }
    if (!gw) { bool have = false; int ba = 0, bb = 0; i64 bd = 0; for (int a = 0; a < N; ++a) if (reg[a] == i) for (int b = 0; b < N; ++b) if (reg[b] == 0) { const i64 d = edist(pts[a], pts[b]); if (!have || d < bd) { have = true; ba = a; bb = b; bd = d; } } lane(ba, bb); } }
  std::stable_sort(lanes.begin(), lanes.end(), [](const Lane& x, const Lane& y) { return x.a != y.a ? x.a < y.a : x.b < y.b; });
  net.laneN0 = (i64)lanes.size();
  net.hz = genHazards(seed, P, side, core, homes, ang);
  net.lanes = hopList(pts, P, net.hz);
  { std::vector<i64> L; for (const auto& l : net.lanes) L.push_back(l.len); std::sort(L.begin(), L.end()); net.J = L.empty() ? 1 : L[L.size() / 2]; }
  for (auto& l : net.lanes) { const bool ja = reg[l.a] == 0, jb = reg[l.b] == 0; l.j = ja ? l.a : jb ? l.b : -1; }
  for (auto& x : net.st) if (x.tier == 2 && edist(homes[(size_t)x.region], x.pos) <= P.ownFrontierD && R.inti(100) < P.ownFrontierPct) x.owner = MAJOR_IDS[x.region];
  net.side = side; net.n0 = N; net.coreHome = 0;
  for (size_t i = 0; i < homes.size(); ++i) { net.hubs.push_back((int)i); net.majors.push_back(MAJOR_IDS[i]); if (i) { net.sectorHomes.push_back((int)i); net.fairHubs.push_back((int)i); } }
  for (int i = 0; i < N; ++i) if (reg[i] == 0) net.junctions.push_back(i);
  floyd(net);
  std::vector<std::vector<int>> assign(net.hubs.size());
  for (int i = (int)M; i < N; ++i) { int best = 0; for (int h = 1; h < M; ++h) if (net.dist[i][h] < net.dist[i][best]) best = h; assign[(size_t)best].push_back(i); }
  for (int h = 0; h < M; ++h) for (int tr : {1, 2}) {
    std::vector<int> left; for (int i : assign[(size_t)h]) if (net.st[i].tier == tr) left.push_back(i);
    std::stable_sort(left.begin(), left.end(), [&](int a, int b) { return net.dist[h][a] != net.dist[h][b] ? net.dist[h][a] < net.dist[h][b] : a < b; });
    while (!left.empty()) {
      std::vector<int> stops; int atS = h;
      while ((i64)stops.size() < P.maxStops && !left.empty()) {
        size_t bi = 0; for (size_t k = 1; k < left.size(); ++k) if (net.dist[atS][left[k]] < net.dist[atS][left[bi]] || (net.dist[atS][left[k]] == net.dist[atS][left[bi]] && left[k] < left[bi])) bi = k;
        if (!stops.empty() && net.dist[atS][left[bi]] > P.maxLegChain) break;
        atS = left[bi]; left.erase(left.begin() + (ptrdiff_t)bi); stops.push_back(atS);
        if (net.dist[h][atS] > 30 && stops.size() >= 2) break;
      }
      net.routes.push_back(buildRoute(net, h, stops, P, false));
    }
  }
  return net;
}

// ---------- bodies, placement, fairness ----------
static const std::vector<int> CAN[4] = {{0, 1}, {0}, {2}, {1}};   // world, belt, giant, ice
std::vector<Dep> genBodies(uint32_t seed, const NetSt& s, const WParams& P, i64 salt) {
  Seq R{seed, K_BODY, salt, true}; const int t = s.tier; const bool outpost = s.parent >= 0;
  i64 budget = R.range(P.bodyBudget[(size_t)t][0], P.bodyBudget[(size_t)t][1]); if (outpost) budget = idiv(budget * P.outpostBudgetPct, 100);
  struct B { int b, t; i64 w; }; std::vector<B> bodies;
  const i64 worlds = t == 0 ? R.range(1, 3) : R.range(0, 3);
  for (i64 i = 0; i < worlds; ++i) { const int tt = R.inti(100) < 50 ? 0 : 1; const i64 w = R.range(2, 5); bodies.push_back({0, tt, w}); }
  if (R.inti(100) < 45) { const i64 w = R.range(3, 6); bodies.push_back({1, 0, w}); }
  if (R.inti(100) < 50) { const i64 w = R.range(4, 8); bodies.push_back({2, 2, w}); }
  const i64 ices = R.range(0, 2); for (i64 i = 0; i < ices; ++i) { const i64 w = R.range(1, 3); bodies.push_back({3, 1, w}); }
  if (bodies.empty()) { const int tt = (int)R.inti(2); bodies.push_back({0, tt, 3}); }
  std::vector<i64> w; for (const auto& b : bodies) w.push_back(b.w);
  const auto rates = apportion(w, budget); std::vector<Dep> out;
  for (size_t i = 0; i < bodies.size(); ++i) { const i64 tau = R.range(P.bodyTau[(size_t)bodies[i].b][0], P.bodyTau[(size_t)bodies[i].b][1]) * 360;
    if (rates[i] > 0) { Dep d; d.t = bodies[i].t; d.rate0 = rates[i]; d.res0 = d.res = rates[i] * tau; d.b = bodies[i].b; out.push_back(d); } }
  return out;
}
static i64 depValue(const Dep& d, const WParams& P) { return idiv(d.rate0 * std::min(idiv(d.res0, std::max<i64>(1, d.rate0 * 360)), P.fairHorizonYears), 100); }
static std::vector<std::vector<int>> homeRegions(const Net& net, const WParams& P) {
  const int N = (int)net.st.size(); std::vector<std::vector<int>> adj((size_t)N);
  for (const auto& l : net.lanes) { adj[l.a].push_back(l.b); adj[l.b].push_back(l.a); }
  std::vector<std::vector<int>> out;
  for (int h : net.fairHubs) { std::vector<i64> hop((size_t)N, -1); hop[h] = 0; std::vector<int> q{h}; size_t qi = 0;
    while (qi < q.size()) { const int x = q[qi++]; for (int y : adj[x]) if (hop[y] < 0) { hop[y] = hop[x] + 1; q.push_back(y); } }
    std::vector<int> reg; for (int i = 0; i < N; ++i) { if (hop[i] < 0 || hop[i] > P.fairK) continue; bool ok = true;
      for (int g : net.hubs) if (!(g == h || net.dist[g][i] > net.dist[h][i] || (net.dist[g][i] == net.dist[h][i] && g > h))) { ok = false; break; }
      if (ok) reg.push_back(i); }
    out.push_back(reg); }
  return out;
}
static std::vector<i64> fairScores(const Net& net, const std::vector<Station>& S, const WParams& P) {
  std::vector<i64> sc; for (const auto& reg : homeRegions(net, P)) { i64 a = 0; for (int i : reg) for (const auto& d : S[i].dep) a += depValue(d, P); sc.push_back(a); } return sc; }
bool fairCheck(const Net& net, std::vector<Station>& S, const WParams& P, i64* rebalancedOut) {
  const auto regs = homeRegions(net, P); const i64 band = P.fairBand;
  auto sum = [](const std::vector<i64>& v) { return std::accumulate(v.begin(), v.end(), i64{0}); };
  auto sc = fairScores(net, S, P); i64 mean = idiv(sum(sc), (i64)sc.size()); const i64 sum0 = sum(sc);
  auto inBand = [&](i64 v) { return v * 100 >= mean * (100 - band) && v * 100 <= mean * (100 + band); };
  auto allIn = [&] { for (i64 v : sc) if (!inBand(v)) return false; return true; };
  i64 rebalanced = 0;
  for (int pass = 0; pass < 4 && !allIn(); ++pass) {
    const i64 target = idiv(*std::max_element(sc.begin(), sc.end()) * 100, 100 + band) + 2;
    for (size_t h = 0; h < regs.size(); ++h) { const i64 v = sc[h]; if (v >= target || v <= 0) continue;
      const i64 pm = std::min<i64>(5000, idiv(target * 1000, v) + 5);
      for (int i : regs[h]) for (auto& d : S[i].dep) { const i64 tau = idiv(d.res0, std::max<i64>(1, d.rate0)); d.rate0 = std::max<i64>(1, idiv(d.rate0 * pm, 1000)); d.res0 = d.res = d.rate0 * tau; }
      ++rebalanced; }
    sc = fairScores(net, S, P); mean = idiv(sum(sc), (i64)sc.size());
  }
  if (rebalanced) { const i64 sum1 = sum(sc), pm = idiv(sum0 * 1000, std::max<i64>(1, sum1));
    if (pm < 1000) { for (const auto& reg : regs) for (int i : reg) for (auto& d : S[i].dep) { const i64 tau = idiv(d.res0, std::max<i64>(1, d.rate0)); d.rate0 = std::max<i64>(1, idiv(d.rate0 * pm, 1000)); d.res0 = d.res = d.rate0 * tau; }
      sc = fairScores(net, S, P); mean = idiv(sum(sc), (i64)sc.size()); } }
  if (rebalancedOut) *rebalancedOut = rebalanced;
  return allIn();
}
void placementRule(const Net& net, std::vector<Station>& S, const WParams& P, uint32_t seed) {
  for (int h : net.hubs) {
    std::vector<int> ids; for (const auto& r : net.routes) if (r.hub == h && !r.relay) for (int x : r.stops) ids.push_back(x);
    std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<std::pair<int, size_t>> deps; for (int i : ids) for (size_t k = 0; k < S[i].dep.size(); ++k) deps.push_back({i, k});
    auto D = [&](const std::pair<int, size_t>& p) -> Dep& { return S[p.first].dep[p.second]; };
    for (int t = 0; t < 3; ++t) {
      if (deps.empty()) continue; bool any = false; for (auto& p : deps) if (D(p).t == t) any = true; if (any) continue;
      std::vector<std::pair<int, size_t>> can; for (auto& p : deps) { const auto& c = CAN[D(p).b]; if (std::find(c.begin(), c.end(), t) != c.end()) can.push_back(p); }
      std::stable_sort(can.begin(), can.end(), [&](auto& a, auto& b) { return D(a).rate0 > D(b).rate0; });
      if (!can.empty()) { int same = 0; for (auto& p : deps) if (D(p).t == D(can[0]).t) ++same; if (same > 1) { D(can[0]).t = t; continue; } }
      std::vector<int> hs = ids; std::stable_sort(hs.begin(), hs.end(), [&](int a, int b) { return S[a].dep.size() != S[b].dep.size() ? S[a].dep.size() < S[b].dep.size() : a < b; });
      const int host = hs[0], kind = t == 0 ? 1 : t == 1 ? 3 : 2; const i64 rate0 = 6 + hashN(seed, K_BODY, host, t) % 7, tau = P.bodyTau[(size_t)kind][0] * 360;
      Dep d; d.t = t; d.rate0 = rate0; d.res0 = d.res = rate0 * tau; d.b = kind; S[host].dep.push_back(d); deps.push_back({host, S[host].dep.size() - 1});
    }
  }
}

Station mkStation(const WParams& P, const NetSt& s, uint32_t seed) {
  Station st; const i64 pop = P.pop[(size_t)s.tier];
  for (int k = 0; k < 4; ++k) { st.burn0[k] = idiv(pop * P.rates[k], 10000); st.cap[k] = st.burn0[k] * P.capDays[(size_t)s.tier]; st.prod0[k] = idiv(st.burn0[k] * P.prodPct[(size_t)s.tier][k], 100); st.stock[k] = idiv(st.cap[k] * 7000, 10000); }
  st.id = s.id; st.tier = s.tier; st.owner = s.owner; st.basePop = st.pop = pop; st.mood = P.moodTarget[0];
  st.dep = genBodies(seed, s, P, s.id);
  for (int t = 0; t < 3; ++t) { if (s.tier == 0) st.rawCap[t] = 200000; else { i64 a = 0; for (const auto& d : st.dep) if (d.t == t) a += d.rate0; st.rawCap[t] = std::max<i64>(1, a * P.rawCapDays); } }
  return st;
}

}  // namespace apsis::world
