// Integer helpers. State is integers only: int64 and per-mille fixed point.
#pragma once
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace apsis {

// Truncating division, same as v0.20 idiv (Math.trunc(a / b)).
constexpr int64_t idiv(int64_t a, int64_t b) { return a / b; }
constexpr int64_t pm(int64_t v, int64_t perMille) { return idiv(v * perMille, 1000); }
constexpr int64_t pct(int64_t v, int64_t percent) { return idiv(v * percent, 100); }
constexpr int64_t clampI(int64_t v, int64_t a, int64_t b) { return v < a ? a : v > b ? b : v; }

// Largest-remainder apportionment, bit-exact with v0.20 apportion(): weights w, integer total.
// Ties in remainder go to the lower index.
inline std::vector<int64_t> apportion(const std::vector<int64_t>& w, int64_t total) {
  const size_t n = w.size();
  if (total > 0 && n && std::all_of(w.begin(), w.end(), [&](int64_t x) { return x == w[0]; }) && w[0] > 0) {
    std::vector<int64_t> o(n); const int64_t b = total / static_cast<int64_t>(n), r = total - b * static_cast<int64_t>(n);
    for (size_t i = 0; i < n; ++i) o[i] = b + (static_cast<int64_t>(i) < r ? 1 : 0);
    return o;
  }
  const int64_t sum = std::accumulate(w.begin(), w.end(), int64_t{0});
  std::vector<int64_t> out(n, 0);
  if (total <= 0) return out;
  if (!sum) { if (n) out[0] = total; return out; }
  std::vector<std::pair<int64_t, size_t>> order(n);
  int64_t given = 0;
  for (size_t i = 0; i < n; ++i) {
    const int64_t p = w[i] * total;  // weights x total stay below 2^53, where v0.20 (doubles) is exact
    out[i] = static_cast<int64_t>(p / sum); given += out[i];
    order[i] = {static_cast<int64_t>(p % sum), i};
  }
  std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
  for (int64_t k = 0; k < total - given; ++k) out[order[static_cast<size_t>(k)].second]++;
  return out;
}

// v0.20 cappedApportion(): apportion, cap, re-apportion the remainder among the uncapped (at most 12 rounds).
inline std::vector<int64_t> cappedApportion(const std::vector<int64_t>& w, const std::vector<int64_t>& caps, int64_t total) {
  std::vector<int64_t> got(w.size(), 0); int64_t remaining = total;
  std::vector<size_t> active;
  for (size_t i = 0; i < w.size(); ++i) if (w[i] > 0 && caps[i] > 0) active.push_back(i);
  for (int iter = 0; iter < 12 && remaining > 0 && !active.empty(); ++iter) {
    std::vector<int64_t> aw; for (size_t i : active) aw.push_back(w[i]);
    const auto a = apportion(aw, remaining); int64_t gave = 0;
    for (size_t k = 0; k < active.size(); ++k) { const size_t i = active[k]; const int64_t g = std::min(a[k], caps[i] - got[i]); got[i] += g; gave += g; }
    remaining -= gave;
    std::erase_if(active, [&](size_t i) { return got[i] >= caps[i]; });
    if (!gave) break;
  }
  return got;
}

}  // namespace apsis
