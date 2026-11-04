# Initial raw-body audit index

The initial batch reviewed40 members from pointer arrays10, pad images6,
visual-panel/line-list methods12 and DatFile12. The live
[address-keyed index](body-audit-index-2026-10-29.json) now also includes the
[second batch](body-audit-batch2-summary-2026-10-29.md) and
[third batch](body-audit-batch3-summary-2026-10-29.md), for89 members total.
It links complete worker evidence and hashes. Raw byte coverage is not runtime
parity: statuses retain input domains, adaptations and unresolved boundaries.
It is a subset of the1103-function inventory, not a whole-port pass.

The DatFile batch exposed the [repaired file contracts](datfile-contract-review-2026-10-29.md).
The other batches found no new valid-input production defect in their checked
domains. External allocator failures and unsafe negative removal inputs remain
outside proven behavior. Pad draw return paths were followed through decoded
destinations rather than stopped at a short linear window.

## IDB declarations and application

Thirteen function prototypes were refined using raw owner/caller evidence.
PtrArray self parameters now use the existing8-byte ARM type, read-only getters
use const pointers, Insert's stored item is a pointer, and DeleteAll has no
consumed result. The constructor's existing return model was retained.
No PtrArray member or layout changed.

UiLineListView models200 byte flags at0,200 four-dword rows at0xC8 and a byte
count at0xD48. The accessed extent is a3401-byte lower bound. Its3404-byte
declaration includes three explicit opaque alignment bytes; this is a prefix
view, not a claim that the full original owner allocation is3404 bytes.
It is applied to the six proven method parameters, const for getters.
GetFlag caller setup passes pointer in R0 and row in R1; its LDRSB address is
their commutative sum. The raw signed return remains typed signed; C's u8 result
has a stated high-flag limit outside observed writer values.

The named visual UiPanel type stays7092 bytes. Global0x158890 currently has an
unresolved #109 reference; it was left untouched, not silently retyped to the
prefix view. The new view uses ordinal210. No exact debug identifier or C++
vtable/RTTI claim was inferred. Field widths, offsets and owner applications
were checked; getters/arrays and opaque padding are explicit.

[Before](body-audit-types-before-2026-10-29.json),
[view preview](uilinelist-view-preview-2026-10-29.json) and
[saved application readback](body-audit-types-after-2026-10-29.json) record the work.
All13 owners decompiled after application, and component models were refreshed.
Global #109 and the PtrArray layout read back unchanged. This completes the
scoped type application; the full global layout is a separate open item.

## Tool interpretation checks

Twenty apparent instruction gaps were
[eight-byte macro tails](uipanel-macro-tails-2026-10-29.json), not data heads.
No code definitions or instruction bytes were changed. Component graphs also
missed direct calls; raw targets supplied those edges.

The label def_8C1B0 names a switch default at0x944B8. Branch0x9305C goes there,
not to0x8C1B0; R0 is overwritten at0x944C0. The
[decoded-target record](pad-return-branch-target-2026-10-29.json) supersedes an
incorrect intermediate return-flow hypothesis.

No test or native flow was executed. Remaining full function mapping and joined
playable-flow acceptance are still open.
