# Shipyard v3 golden fixtures

These files come from the GPT-6 v3 prototype. They are read-only historical
reference and must stay byte-identical to the prototype originals.

`SHA256SUMS` records the seven golden files. CI verifies the manifest against a
pinned SHA-256 digest, then verifies each golden against the manifest. To check
the golden files locally, run `sha256sum --check --strict SHA256SUMS` from this
directory.

Owner decisions dated 2026-11-20 take effect in the v4 rules file, not by editing
these v3 copies. The 5:24–5:25 PM ET dock picks are recorded in `DESIGN.md` /
FINDINGS F18 and do not affect the v3 copies. Jeff's picks via Luna question
cards are quoted verbatim below; interpretation is labeled separately.

Owner pick, 2026-11-20 4:53 PM ET (Luna question card): "Jump drives: scale price with range, 1,500 / 2,500 / 7,500"

Interpretation: The options were for 0.5 / 1.0 / 3.0 J.

Owner pick, 2026-11-20 4:53 PM ET (Luna question card): "Docking: yes, ports check airlock and clearance, only hangars limit size"

Interpretation: The question was "Should a 20×34 m ship fit a docking port?"

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F01: 1.5 J = 3,750, 2.0 J = 5,000"

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F03: same deck, exterior, facing out, within 1 cell"

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F07: Rustbucket stays a deliberate failing example"

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F09: rules file wins, 80 chars / 100M"

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F10: reject with an error"

Jeff's typed question, 2026-11-20 5:14 PM ET (Luna question card): "Budget limits I thought it would be up to whatever could be afforded?"

Owner pick, 2026-11-20 5:14 PM ET (Luna question card): "Keep 100M as a technical sanity limit only; in game, budget is what the player can afford"

Interpretation: In v4, 100M cr is a technical input sanity cap; in-game budget is
what the player can afford. The 80-character name limit stands.
