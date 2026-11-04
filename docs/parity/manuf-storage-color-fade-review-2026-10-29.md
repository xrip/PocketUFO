# Manufacturing storage and live modal fade repairs

Baseline compiled source: `6401F9FAFE9D5986A4BDFE6C694AB3357DE3705A06BBCD2D55C1A173E9302D2D`.
The repairs below are newer than that baseline. Both full builds and the focused
storage/render/World checks have since run; see the
[playable checkpoint](playable-checkpoint-2026-10-29.md) for current scope and results.

## Manufacturing storage

Raw `0x6E27C` loads live site count into R1. R2 holds the selected base and
R3=1 enables site scanning for the call to `0x67D68` at `0x6E284`. The helper's
scan count is independently loaded from the live owner at `0x67E48`.
Baseline C passed selected base as requested site index, and passed a stale
wiring-time site count as the scan bound. The helper's site-count row is an
allocated working record, so substituting the base index can change storage
capacity decisions even when the array bounds are valid.

`ManufScreen_IncUnits` now passes the site count as requested site index;
`World_ManufIncUnitsThunk` refreshes the site count from World before each call.
See `manuf-storage-parent-raw-2026-10-29.json`. Neighboring base-count ownership
also needs a live refresh: the C ship-count helper's safety bound can suppress
the reserved-hangar grid term when a fresh load retains the pre-read zero
`manufUnits.nBaseCount`. Raw always adds that grid term for the selected base.
The callback now refreshes base count too. Full load wiring evidence and the
focused World fixture remain under worker review.

## Modal color during battle-end fade

Raw `Gfx_PackRgb565Pair @0x2D82C` reads Map+0x64 once at `0x2D968`.
The C `GfxColorEnv.nFadeCount` was a separate scalar with no production writer.
An inventory error modal can survive inventory close, movement and battle-end
setup. Battle end writes10; the fade frame decrements it to9 before drawing.
Thus this is a reachable state, rather than a hypothetical combination of fields.

For the modal label RGB pair `(10,90,50)` and gamma2, raw count9 gives divisor2
and palette output `0x0183`, while the stale zero gives `0x0B06`. The following
black alpha20 overlay preserves the difference: `0x0162` versus `0x02C5`.
At count10 divisor1 gives `0x0B06` in both. The overlay does not establish
equivalence. Raw chain and math are in
`../../.workers/luna-color-fade-owner/{RESULT.md,evidence.json}`.

The color environment now has `const i32 *pnFadeCount`, with NULL meaning zero
before World setup. `World_NewGame` and `World_LoadSlot` bind it to
`World.nMapUiSlot1` beside the existing live gamma binding. The packer reads the
cell once per call. This keeps one live owner and restores the raw divisor input.
It adds no extra per-frame copy or independent fade state.

The focused color test changes one live cell from10 to9 and checks the two raw
packed outputs. World tests check NewGame and LoadSlot bind the shared color
environment to its live fade cell. The parent clears the color pointer before
the load call, so the load assertion cannot inherit a successful NewGame bind.
These checks remain unrun; they do not pump the whole modal-to-debrief path.
