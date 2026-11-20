// Counter-based hash RNG, bit-exact with v0.20 (logi20.js mix32 / hashN / strHash).
// A draw depends only on its arguments, never on how many draws came before it,
// so call order and thread count cannot change a result.
#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace apsis {

constexpr uint32_t mix32(uint32_t h) {
  h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h;
}

// JS: h = mix32(h ^ mix32(((arg >>> 0) + 0x9e3779b9 + i) >>> 0)). ToUint32 of an integer = its value mod 2^32.
constexpr uint32_t hashSpan(std::span<const int64_t> a) {
  uint32_t h = 0x2545F491u;
  for (uint32_t i = 0; i < a.size(); ++i) h = mix32(h ^ mix32(static_cast<uint32_t>(static_cast<uint64_t>(a[i])) + 0x9e3779b9u + i));
  return h;
}

template <class... A>
constexpr uint32_t hashN(A... a) {
  const int64_t v[] = {static_cast<int64_t>(a)...};
  return hashSpan(std::span<const int64_t>(v, sizeof...(A)));
}

// FNV-1a over the string's code units (ASCII keys only).
constexpr uint32_t strHash(std::string_view s) {
  uint32_t h = 0x811c9dc5u;
  for (unsigned char c : s) { h ^= c; h *= 0x01000193u; }
  return h;
}

}  // namespace apsis
