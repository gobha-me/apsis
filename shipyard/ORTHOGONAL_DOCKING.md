# Orthogonal hangar docking (F19)

Decision authority: **Luna, under owner delegation 2026-11-23**, recorded in
[#77](https://github.com/gobha-me/apsis/issues/77), Refs #60:

> Docking permits the four right-angle facings only; F19 is resolved by that orthogonal-only rule.

For hangar width/length fit, the four facings have two distinct envelopes.
With visitor dimensions `w,l` and active rule-defined hangar limits `W,L`, fit is
`(w <= W && l <= L) || (l <= W && w <= L)`. Both orientations use exact input
bounds; diagonal fit and numeric epsilon do not extend them. The independent
rule-defined deck cap still applies.

The existing `dock` signature and `apsis-shipyard-result-v1` envelope remain.
`data.hangar.widthLengthFit` is always a JSON boolean. If neither permitted
envelope fits, `dock_hangar_size_limit` reports the existing measured dimensions
and limits at the active docking severity. `pending_F19` is no longer emitted.
The operation does not select a facing or visitor airlock.

All F18 checks remain active with production defaults. The separate F03/F21
alignment work can still leave docking uncertified. Hold representation, access,
capacity/mass, hangar overlap/deck accounting, walkability and hold-specific
airlock/door integration are separate #77 slices; this note defines none of them.

This F19-only implementation is stacked on #85 (including #68/#65). Shared
DESIGN/CONTRACT/FINDINGS/README files also overlap open #67 and are preserved
until their owning changes land or an explicit combined stack is used. Their
historical pending-F19 and arbitrary-angle wording is superseded by the decision
and public behavior documented here. No v3 fixtures or rule values are changed.
