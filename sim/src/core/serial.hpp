// Deterministic serialization (checkpoints, input logs) and the state hash.
// Little-endian zigzag varints: the same bytes on every machine and in WebAssembly.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace apsis {

struct Writer {
  std::vector<uint8_t> b;
  void u(uint64_t v) { while (v >= 0x80) { b.push_back(static_cast<uint8_t>(v | 0x80)); v >>= 7; } b.push_back(static_cast<uint8_t>(v)); }
  void i(int64_t v) { u((static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63)); }
  template <class T> void vec(const std::vector<T>& v) { u(v.size()); for (const auto& x : v) put(*this, x); }
};
// A programming error (not bad input): stop. The core never throws, so it builds without exceptions.
[[noreturn]] inline void fail(const char* why) { std::fputs(why, stderr); std::fputs("\n", stderr); std::abort(); }

// Bad input sets `bad` instead of throwing; the caller checks it.
struct Reader {
  const std::vector<uint8_t>& b; size_t p = 0; bool bad = false;
  uint64_t u() { uint64_t v = 0; int s = 0; for (;;) { if (p >= b.size() || s > 63) { bad = true; return 0; } const uint8_t c = b[p++]; v |= static_cast<uint64_t>(c & 0x7f) << s; if (!(c & 0x80)) return v; s += 7; } }
  int64_t i() { const uint64_t z = u(); return static_cast<int64_t>(z >> 1) ^ -static_cast<int64_t>(z & 1); }
  template <class T> void vec(std::vector<T>& v) { const uint64_t n = u(); if (n > b.size() - p) { bad = true; return; } v.resize(static_cast<size_t>(n)); for (auto& x : v) get(*this, x); }
};
inline void put(Writer& w, int64_t v) { w.i(v); }
inline void get(Reader& r, int64_t& v) { v = r.i(); }
inline void put(Writer& w, int32_t v) { w.i(v); }
inline void get(Reader& r, int32_t& v) { v = static_cast<int32_t>(r.i()); }

// Two 32-bit FNV-style lanes, printed as 16 hex digits (the v0.20 hashState format).
inline std::string hashBytes(const std::vector<uint8_t>& b) {
  uint32_t h = 0x811c9dc5u, h2 = 0x1b873593u;
  for (uint8_t c : b) { h = (h ^ c) * 0x01000193u; h2 = (h2 ^ c) * 0x5bd1e995u; }
  static const char* hx = "0123456789abcdef"; std::string s(16, '0');
  for (int k = 0; k < 8; ++k) { s[7 - k] = hx[(h >> (4 * k)) & 15]; s[15 - k] = hx[(h2 >> (4 * k)) & 15]; }
  return s;
}

}  // namespace apsis
