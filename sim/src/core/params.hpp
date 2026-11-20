// Rules and Params generated from params.def. Everything is an integer.
#pragma once
#include <cstdint>
#include <string_view>
#include "serial.hpp"

namespace apsis {

struct Rules {
#define RULE(n, d, l, m) bool n = d;
#define PARAM(n, d, u, m, f)
#include "params.def"
#undef RULE
#undef PARAM
};
struct Params {
#define RULE(n, d, l, m)
#define PARAM(n, d, u, m, f) int64_t n = d;
#include "params.def"
#undef RULE
#undef PARAM
  Rules rules;
};
// Set a rule switch by name (ablations); false if the name is unknown.
inline bool setRule(Rules& r, std::string_view name, bool v) {
#define RULE(n, d, l, m) if (name == #n) { r.n = v; return true; }
#define PARAM(n, d, u, m, f)
#include "params.def"
#undef RULE
#undef PARAM
  return false;
}
// Set a placeholder by name (experiments and what-if runs); false if the name is unknown.
inline bool setParam(Params& p, std::string_view name, int64_t v) {
#define RULE(n, d, l, m)
#define PARAM(n, d, u, m, f) if (name == #n) { p.n = v; return true; }
#include "params.def"
#undef RULE
#undef PARAM
  return false;
}

inline void put(Writer& w, const Params& p) {
#define RULE(n, d, l, m) w.u(p.rules.n ? 1 : 0);
#define PARAM(n, d, u_, m, f) w.i(p.n);
#include "params.def"
#undef RULE
#undef PARAM
}
inline void get(Reader& r, Params& p) {
#define RULE(n, d, l, m) p.rules.n = r.u() != 0;
#define PARAM(n, d, u_, m, f) p.n = r.i();
#include "params.def"
#undef RULE
#undef PARAM
}

}  // namespace apsis
