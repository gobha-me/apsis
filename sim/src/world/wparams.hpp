#pragma once
#include <array>
#include <cstdint>
namespace apsis::world {
using A2 = std::array<int64_t, 2>; using A3w = std::array<int64_t, 3>; using A4w = std::array<int64_t, 4>; using A6 = std::array<int64_t, 6>;
using A32 = std::array<A2, 3>; using A5 = std::array<int64_t, 5>; using A35 = std::array<A5, 3>; using A42 = std::array<A2, 4>; using A34 = std::array<A4w, 3>;
struct WParams {
  using i64 = int64_t; using A3 = A3w; using A4 = A4w;
#define WP(T, n, d, m) T n = d;
#include "wparams.def"
#undef WP
};
}  // namespace apsis::world
