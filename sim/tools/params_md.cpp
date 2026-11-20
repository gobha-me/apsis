// Prints the rules and placeholders table (README, report) from params.def.
#include <cstdio>
#include <string>
#include "../src/world/wparams.hpp"
static std::string fmt(int64_t v) { return std::to_string(v); }
template <class T> static std::string fmt(const T& a) { std::string s = "["; for (size_t i = 0; i < a.size(); ++i) s += (i ? ", " : "") + fmt(a[i]); return s + "]"; }
int main() {
  std::puts("| Rule | Default | Layer | Meaning |\n| --- | --- | --- | --- |");
#define RULE(n, d, l, m) std::printf("| `%s` | %s | %d | %s |\n", #n, (d) ? "on" : "off", l, m);
#define PARAM(n, d, u, m, f)
#include "../src/core/params.def"
#undef RULE
#undef PARAM
  std::puts("\n| Placeholder | Default | Unit | Meaning | From |\n| --- | --- | --- | --- | --- |");
#define RULE(n, d, l, m)
#define PARAM(n, d, u, m, f) std::printf("| `%s` | %lld | %s | %s | %s |\n", #n, (long long)(d), u, m, f);
#include "../src/core/params.def"
#undef RULE
#undef PARAM
  std::puts("\n| World placeholder (v0.20 values unless noted) | Default | Meaning |\n| --- | --- | --- |");
  const apsis::world::WParams W;
#define WP(T, n, d, m) std::printf("| `%s` | %s | %s |\n", #n, fmt(W.n).c_str(), m);
#include "../src/world/wparams.def"
#undef WP
}
