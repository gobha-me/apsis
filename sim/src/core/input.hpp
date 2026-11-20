// The input channel. Same-tick actions are applied in one canonical order (kind, then fields),
// so the order they arrived in can never change the world. A save is seed + this log.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include "serial.hpp"

namespace apsis {

constexpr size_t kActionFields = 6;
struct Action {
  int32_t kind = 0;                          // the domain numbers kinds in canonical order
  std::array<int64_t, kActionFields> f{};    // fields, compared in order; unused = 0
  friend bool operator<(const Action& a, const Action& b) { return a.kind != b.kind ? a.kind < b.kind : a.f < b.f; }
  friend bool operator==(const Action&, const Action&) = default;
};
struct LogEntry { int64_t day; Action a; };

inline void put(Writer& w, const Action& a) { w.i(a.kind); for (auto v : a.f) w.i(v); }
inline void get(Reader& r, Action& a) { a.kind = static_cast<int32_t>(r.i()); for (auto& v : a.f) v = r.i(); }
inline void put(Writer& w, const LogEntry& e) { w.i(e.day); put(w, e.a); }
inline void get(Reader& r, LogEntry& e) { e.day = r.i(); get(r, e.a); }

struct Inbox {
  std::vector<Action> pending;
  bool canonical = true;                     // off only for the gate's teeth test
  void submit(const Action& a) { pending.push_back(a); }
  std::vector<Action> drain() {
    auto out = std::move(pending); pending.clear();
    if (canonical) std::stable_sort(out.begin(), out.end());
    return out;
  }
};

}  // namespace apsis
