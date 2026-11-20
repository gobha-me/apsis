// Exact ledgers: every tracked quantity (a good, money) counts what was made and what was destroyed.
// A domain checks that its state totals equal made - gone after every tick.
#pragma once
#include <cstdint>
#include <vector>
#include "serial.hpp"

namespace apsis {

struct Ledger {
  std::vector<int64_t> made, gone;
  explicit Ledger(size_t n = 0) : made(n, 0), gone(n, 0) {}
  void make(size_t q, int64_t v) { made[q] += v; }
  void lose(size_t q, int64_t v) { gone[q] += v; }
  int64_t expected(size_t q) const { return made[q] - gone[q]; }
};
inline void put(Writer& w, const Ledger& l) { w.vec(l.made); w.vec(l.gone); }
inline void get(Reader& r, Ledger& l) { r.vec(l.made); r.vec(l.gone); }

}  // namespace apsis
