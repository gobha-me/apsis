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

- **utf8proc 2.12.0**, project MIT license plus Unicode-derived-data license:
  unchanged source from [the official release commit](https://github.com/JuliaStrings/utf8proc/tree/79cdd5ab40c79afa559f689ffc13b76812dee1ac),
  Unicode 18.0.0. The private static C library supplies extended UAX #29 grapheme
  boundaries for blueprint and default ship/station names only. It performs no
  normalization; generic JSON Schema, deck and note lengths remain scalar-based.
  The full upstream `vendor/utf8proc/LICENSE.md` preserves both licenses.
  SHA-256 of the vendored bytes:

  - `utf8proc.c`: `f38438eb986ba7eedad0f2e820a70fae1aaf05197364ba0381a5e84775099ef8`.
  - `utf8proc.h`: `08da85e820efaa3613500946c26b2dc427a07f76001ed7c16bc90f298b814c35`.
  - `utf8proc_data.c`: `b4911d4162d216afa612317a9b981851e15d672f4d93b25f3a1eb986f8c0aedb`.
  - `LICENSE.md`: `3b510150d34f248a221bb88e1d811238d6c6c18b51231822c42974c39bb07256`.
