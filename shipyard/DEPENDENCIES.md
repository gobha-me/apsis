# Dependencies

- **nlohmann/json 3.11.3**, MIT: the vendored single header and license come
  from the [official project](https://github.com/nlohmann/json/tree/v3.11.3).
  `vendor/nlohmann/json.hpp` SHA-256:
  `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`.
  It supplies JSON parsing and serialization; duplicate keys, nesting limits,
  and nonfinite values are checked by the shipyard loader.
- **OpenSSL 3.x libcrypto**, Apache-2.0: a system build dependency used only
  for SHA-256 of the exact installed rules bytes. Locally built with 3.0.13.
  See the [official license](https://www.openssl.org/source/license.html).

There is no external schema runtime dependency. The core implements the bounded
JSON Schema vocabulary used by the bundled Draft 2020-12 schemas, with no remote
reference retrieval; the supported vocabulary is documented in `CONTRACT.md`.
Python `jsonschema` 4.25.1 was used in disposable session scratch to check schema
structure and data shapes; it is not a library or build dependency.

- **miniz 3.0.2**, MIT: pinned native RGBA PNG encoder and ZIP writer, from
  [the official 3.0.2 source](https://github.com/richgel999/miniz/tree/3.0.2),
  commit `293d4db1b7d0ffee9756d035b9ac6f7431ef8492`. About 385 KiB of upstream
  source plus license. Stdio and clocks are disabled; all writes are bounded
  memory callbacks. [The vendoring record](vendor/miniz/README.md) lists source
  hashes, the static export header, and the two fixed-DOS-date initializers.
  No other upstream logic is modified. OpenSSL SHA-256 also hashes render
  artifacts and the normalized rendered definition.
- **Apsis bitmap font 1**, BSD-3-Clause: the original 5-by-7 glyph table in
  `src/render_font.hpp` uses the repository license. It is compiled into the
  renderer and hashed in every manifest; no external font/assets are loaded.
