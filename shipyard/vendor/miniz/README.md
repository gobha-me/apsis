# miniz 3.0.2 vendoring record

Source: [richgel999/miniz tag 3.0.2](https://github.com/richgel999/miniz/tree/3.0.2),
commit `293d4db1b7d0ffee9756d035b9ac6f7431ef8492`. License: MIT; complete text
is in `LICENSE`. The unmodified source files were fetched from this exact
commit, not a moving branch. There is no download at build or runtime.

One small native dependency supplies both deterministic RGBA PNG encoding and
the standard ZIP writer, without introducing another image/archive framework.
The renderer calls only its in-memory encoder/writer functions. The inflater
and archive reader sources are retained for upstream build completeness; they
are not used to admit caller-provided images or archives. No untrusted PNG/ZIP
is decoded by the renderer.

Build configuration disables stdio, clocks/timezones, zlib API/name aliases,
and unaligned loads/stores:
`MINIZ_NO_STDIO`, `MINIZ_NO_TIME`, `MINIZ_NO_ZLIB_APIS`,
`MINIZ_NO_ZLIB_COMPATIBLE_NAMES`, `MINIZ_USE_UNALIGNED_LOADS_AND_STORES=0`.
Source inspection covered PNG RGBA encoding, memory ownership, archive write
callbacks and finalization, and the no-time branches used by this integration.
This is an integration review, not a security certification.

Local changes are deliberately small:

- `miniz_export.h` provides an empty `MINIZ_EXPORT` for the static target;
  upstream CMake normally generates this visibility header.
- The two `miniz_zip.c` default `dos_date` initializers change from 0 to 33.
  With `MINIZ_NO_TIME`, this represents valid fixed DOS date 1980-01-01 instead
  of an invalid zero month/day. Both `dos_time` defaults stay zero. No runtime
  clock or timezone is consulted. No compression or CRC algorithm is changed.

The following table records exact upstream input hashes. Only `miniz_zip.c`
differs among these files:

| Upstream file | SHA-256 |
| --- | --- |
| `miniz.h` | `8033197e77c9567de66425939ab6164405e54aa3c60acf9312905d32c8cddc03` |
| `miniz.c` | `d8b837863cc6536a270751893a9778c4241467d8906719ae6cb787a62fa797bc` |
| `miniz_common.h` | `8b60e76e726dfcf41a338c3a4c54c70038041a21e6314aa54fd3b7f729ea3412` |
| `miniz_tdef.h` | `12b09b189c27aa7d0b0ebe0be2f3021a3bab26fe7b19e9b3b6f5c4df7c21e9df` |
| `miniz_tdef.c` | `69a1d22a01e698286a7d2a8df90884aa33b4f073e080749e1fce76c5117a5a29` |
| `miniz_tinfl.h` | `040aad2d9ca23c487652c150277d950be12e72c5453ae1f7c6713ed2efd16b4e` |
| `miniz_tinfl.c` | `9b7edc1fab708166e10f5e90a618a91c95a91496c4692e38160fdae810c1149a` |
| `miniz_zip.h` | `c9343f0d251d92f3730d5fe684d4af67aa0bed25d2d1d39d5fd4746a3c63f796` |
| `miniz_zip.c` | `7179062fdaa868d18e86ae9391fe47316ef4632c5a24fe3d3ce73d6535c7dd5c` |
| `LICENSE` | `0115478d567121238cf6cc1c0c361926cf07a49d9e4c9e66da97fac6a01646b3` |

Committed local file hashes:

- `miniz_zip.c`: `9d1453d5805e8aea81197ea4e06e7ae71d463152de76cb4d5377d0f4e942a641`.
- `miniz_export.h`: `5006c26c4b24e8c4ed3a31e8a3a9ca90da50728f508fb1523c707e1ad99f821e`.
