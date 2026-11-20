# Shipyard v3 golden fixtures

These files come from the GPT-6 v3 prototype. They are read-only historical
reference and must stay byte-identical to the prototype originals.

`SHA256SUMS` records the seven golden files. CI verifies the manifest against a
pinned SHA-256 digest, then verifies each golden against the manifest. To check
the golden files locally, run `sha256sum --check --strict SHA256SUMS` from this
directory.

Owner decisions dated 2026-11-20 take effect in the v4 rules file, not by editing
these v3 copies. The decisions are quoted exactly below:

1. Jump drive price scales with range: 0.5 J = 1,500 cr, 1.0 J = 2,500 cr, 3.0 J = 7,500 cr.
2. Docking ports check only airlock alignment and approach clearance, with no size limit, so a 20x34 m ship fits a port. Only hangars limit size, by width, length and deck count.
