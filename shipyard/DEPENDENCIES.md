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
