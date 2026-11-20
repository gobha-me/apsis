// Layer-1 gates. Prints one line per gate and writes gates_l1.json.
//   gates [vectors.txt] [days]        native, threads 1/2/8
//   gates --digest                     a fixed scenario's digest, to compare native with WebAssembly
#include <algorithm>
#include <cstdio>
#include <format>
#include <map>
#include <set>
#include <sstream>
#include <vector>
#include <string>
#include "../src/core/fixed.hpp"
#include "../src/core/rng.hpp"
#include "../src/proto/proto.hpp"

using namespace apsis;
namespace pr = apsis::proto;

static const uint32_t SEEDS[] = {333860, 110478, 697804, 57206, 632322, 517908, 552723};
struct Gate { std::string name; int ok, n; std::string note; };
static std::vector<Gate> gates;
static void report(const std::string& name, int ok, int n, const std::string& note = "") {
  gates.push_back({name, ok, n, note});
  std::printf("%-78s %d/%d %s%s\n", name.c_str(), ok, n, ok == n ? "PASS" : "FAIL", note.empty() ? "" : ("  " + note).c_str());
}

// Run a live career: the bot submits each day; returns the per-day hashes.
static std::vector<std::string> live(pr::World& w, int64_t days, bool checkLedger, int* ledgerBad = nullptr) {
  std::vector<std::string> hs;
  for (int64_t d = 0; d < days; ++d) {
    for (const auto& a : pr::bot(w)) pr::submit(w, a);
    pr::tick(w); hs.push_back(pr::hashState(w));
    if (checkLedger && !pr::ledgerOk(w) && ledgerBad) ++*ledgerBad;
  }
  return hs;
}
static std::string digest(const std::vector<std::string>& hs) { std::string all; for (auto& h : hs) all += h; return hashBytes(std::vector<uint8_t>(all.begin(), all.end())); }

static int rngGate(const char* path) {
  std::FILE* fp = std::fopen(path, "rb"); if (!fp) { report("RNG and apportionment = v0.20, bit for bit (vectors file missing)", 0, 1); return 1; }
  std::string text; { char buf[65536]; size_t k; while ((k = std::fread(buf, 1, sizeof buf, fp)) > 0) text.append(buf, k); std::fclose(fp); }
  std::istringstream in(text); std::string line; int ok = 0, n = 0; std::map<char, std::pair<int, int>> by;
  while (std::getline(in, line)) {
    if (line.empty()) { continue; }
    const char t = line[0]; bool good = false;
    if (t == 'S') {
      const auto q1 = line.find('"'), q2 = line.rfind('"');
      const std::string s = line.substr(q1 + 1, q2 - q1 - 1); const uint32_t h = (uint32_t)std::stoul(line.substr(q2 + 2));
      good = strHash(s) == h;
    } else {
      std::istringstream is(line.substr(2)); size_t k; is >> k;
      if (t == 'H') { std::vector<int64_t> a(k); for (auto& x : a) is >> x; uint64_t h; is >> h; good = hashSpan(a) == (uint32_t)h; }
      else if (t == 'A') { std::vector<int64_t> w(k), o(k); int64_t tot; for (auto& x : w) is >> x; is >> tot; for (auto& x : o) is >> x; good = apportion(w, tot) == o; }
      else if (t == 'C') { std::vector<int64_t> w(k), c(k), o(k); int64_t tot; for (auto& x : w) is >> x; for (auto& x : c) is >> x; is >> tot; for (auto& x : o) is >> x; good = cappedApportion(w, c, tot) == o; }
    }
    ++n; ok += good; by[t].first += good; by[t].second++;
  }
  report("RNG and apportionment = v0.20, bit for bit (hashN, strHash, apportion, cappedApportion)", ok, n,
         std::format("hashN {}/{}, strHash {}/{}, apportion {}/{}, capped {}/{}", by['H'].first, by['H'].second, by['S'].first, by['S'].second, by['A'].first, by['A'].second, by['C'].first, by['C'].second));
  return ok == n ? 0 : 1;
}

static void clearingGates() {
  int ok = 0, n = 0;
  // inside a faction: two stations want 300 each from a source with 300 to give; priority decides
  { std::vector<Intent> g(2); g[0] = {0, 0, 7, 1, 50, 0, 0, 300, 0}; g[1] = {0, 0, 7, 1, 20, 1, 0, 300, 0};
    std::sort(g.begin(), g.end(), canonLess); auto r = clearDivisible(g, 300); ++n; ok += (g[0].priority == 50 && r[0] == 300 && r[1] == 0); }
  // between factions, divisible: pro rata by bid (bids 300 vs 100 over 400 units, each wants 400)
  { std::vector<Intent> g(2); g[0] = {0, 1, 2, 3, 0, 0, 0, 400, 300}; g[1] = {0, 1, 2, 5, 0, 0, 0, 400, 100};
    std::sort(g.begin(), g.end(), canonLess); auto r = clearDivisible(g, 400); ++n; ok += (r[0] == 300 && r[1] == 100); }
  // indivisible: highest bid wins; a tie is broken by hash, the same whatever order the intents arrived in
  { int same = 0, tries = 0; std::set<int> winners;
    for (uint32_t seed : SEEDS) for (int64_t day = 0; day < 40; ++day) {
      std::vector<Intent> g(4); for (int i = 0; i < 4; ++i) g[(size_t)i] = {day, 2, 9, i, 0, 0, 0, 1, (i == 1 || i == 3) ? 900 : 500};
      std::vector<Intent> h = g; std::reverse(h.begin(), h.end());
      std::sort(g.begin(), g.end(), canonLess); std::sort(h.begin(), h.end(), canonLess);
      const int a = clearIndivisible(g, seed, day), b = clearIndivisible(h, seed, day);
      ++tries; same += (g[(size_t)a].faction == h[(size_t)b].faction && g[(size_t)a].bid == 900); winners.insert(g[(size_t)a].faction);
    }
    ++n; ok += (same == tries && winners.size() == 2); }
  report("clearing: priority inside a faction, pro rata between, highest bid with hash tie-break", ok, n);
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--digest") {
    std::string all;
    for (uint32_t s : SEEDS) { pr::World w = pr::create(s); auto hs = live(w, 3600, false); all += digest(hs); }  // every day of every seed
    std::printf("digest %s\n", hashBytes(std::vector<uint8_t>(all.begin(), all.end())).c_str());
    return 0;
  }
  const char* vec = argc > 1 ? argv[1] : "ref/vectors.txt"; const int64_t days = argc > 2 ? std::stoll(argv[2]) : 3600;
  rngGate(vec);
  clearingGates();

  int okR = 0, okC = 0, okC2 = 0, okL = 0, okT = 0, okS = 0, teeth = 0, nT = 0; int ledgerBad = 0; int64_t ticks = 0; std::string lines;
  for (uint32_t seed : SEEDS) {
    // live run, with ledgers checked every tick
    pr::World w = pr::create(seed); auto hs = live(w, days, true, &ledgerBad); ticks += days;
    // replay = live: feed the log back day by day
    pr::World r = pr::create(seed); size_t li = 0; bool same = true;
    for (int64_t d = 0; d < days; ++d) { while (li < w.log.size() && w.log[li].day == d) pr::submit(r, w.log[li++].a); pr::tick(r); if (pr::hashState(r) != hs[(size_t)d]) { same = false; break; } }
    okR += same;
    // checkpoint resume at a third and two thirds, and byte-exact round trip
    for (int64_t cut : {days / 3, 2 * days / 3}) {
      pr::World a = pr::create(seed); live(a, cut, false); const auto ck = pr::checkpoint(a);
      pr::World b; pr::restore(ck, b); okC2 += (pr::checkpoint(b) == ck);
      auto rest = live(b, days - cut, false); okC += (rest.back() == hs.back());
    }
    // threads 1, 2, 8: identical per-day hash sequence
    bool thr = true; for (int t : {2, 8}) { pr::World x = pr::create(seed); x.threads = t; if (digest(live(x, days, false)) != digest(hs)) thr = false; } okT += thr;
    // input order: shuffle each day's actions; canonical order = same world. Teeth: raw order diverges
    std::set<std::string> raw; bool shuf = true;
    for (uint32_t k = 1; k <= 3; ++k) for (bool canon : {true, false}) {
      pr::World x = pr::create(seed); x.inbox.canonical = canon;
      for (int64_t d = 0; d < days; ++d) { auto acts = pr::bot(x); for (size_t i = acts.size(); i > 1; --i) std::swap(acts[i - 1], acts[hashN(seed, k, d, (int64_t)i) % i]); for (auto& a : acts) pr::submit(x, a); pr::tick(x); }
      if (canon) shuf = shuf && pr::hashState(x) == hs.back(); else raw.insert(pr::hashState(x));
    }
    okS += shuf; teeth += raw.size() > 1; ++nT;
  }
  // coverage: the gates mean something only if the world actually did these things
  { pr::World c = pr::create(SEEDS[0]); live(c, days, false); const auto& v = c.cov;
    const std::string note = std::format("days with 2+ factions deciding {}, intents {}, supply moves granted {}, hub sales {} ({} contested), contract wins {} ({} tied bids), player actions that moved money {}", v.multiDecideDays, v.intents, v.moveGranted, v.hubGranted, v.hubContested, v.contractWins, v.contractTies, v.actsApplied);
    report("coverage: parallel decisions, both clearings, ties and player actions all occur (seed 1)", (v.multiDecideDays > 0) + (v.moveGranted > 0) + (v.hubContested > 0) + (v.contractWins > 0) + (v.contractTies > 0) + (v.actsApplied > 0), 6, note);
    // teeth: a one-unit change is caught by the ledger and by the hash; a corrupted checkpoint is refused or differs
    pr::World t = pr::create(SEEDS[0]); live(t, 400, false); const std::string h0 = pr::hashState(t); t.S[3].stock[1] += 1;
    std::vector<uint8_t> ck = pr::checkpoint(t); ck[ck.size() / 2] ^= 1; pr::World back; const bool caught = !pr::restore(ck, back) || pr::hashState(back) != pr::hashState(t);
    report("  teeth: one unit moved breaks the ledger and the hash; a flipped checkpoint bit is caught", (!pr::ledgerOk(t)) + (pr::hashState(t) != h0) + caught, 3); }
  // threads on a large world, where the pool really runs (64 factions: several decide on most days)
  { int ok = 0; int64_t poolDays = 0;
    for (uint32_t seed : SEEDS) { pr::World a = pr::create(seed, 200, 64, 4); auto ha = live(a, 1800, false); bool same = true;
      for (int t : {2, 8}) { pr::World b = pr::create(seed, 200, 64, 4); b.threads = t; if (digest(live(b, 1800, false)) != digest(ha)) same = false; poolDays += b.cov.poolDays; }
      ok += same; }
    report("decisions at 1, 2 and 8 threads, 200 stations and 64 factions: identical hash every day", ok, (int)std::size(SEEDS), std::format("pool ran on {} days (of {})", poolDays, 2 * 1800 * (int)std::size(SEEDS))); }
  okL = ledgerBad == 0 ? (int)std::size(SEEDS) : 0;
  const int N = (int)std::size(SEEDS);
  report(std::format("replay (seed + input log) = live run, every day's hash ({} seeds x {} days)", N, days), okR, N);
  report("checkpoint resume = uninterrupted run (at 1/3 and 2/3)", okC, 2 * N);
  report("checkpoint round trip is byte-exact", okC2, 2 * N);
  report("goods and money ledgers exact on every tick", okL, N, std::format("{} ticks checked", ticks));
  report("decisions at 1, 2 and 8 threads: identical hash every day", okT, N);
  report("input-order shuffle (3 shuffles per seed), canonical order: same world", okS, N);
  report("  teeth: in raw arrival order, shuffles diverge", teeth, nT);

  std::string js = "[";
  for (size_t i = 0; i < gates.size(); ++i) js += (i ? "," : "") + std::format("{{\"gate\":\"{}\",\"ok\":{},\"n\":{},\"note\":\"{}\"}}", gates[i].name, gates[i].ok, gates[i].n, gates[i].note);
  js += "]\n"; if (std::FILE* o = std::fopen(argc > 3 ? argv[3] : "gates_l1.json", "wb")) { std::fwrite(js.data(), 1, js.size(), o); std::fclose(o); }
  int fails = 0; for (auto& g : gates) fails += g.ok != g.n; return fails ? 1 : 0;
}
