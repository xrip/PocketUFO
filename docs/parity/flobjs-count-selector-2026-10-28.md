# Both tactical item-count reads use loaded TACTVER

Checked 2026-10-28 by Luna xhigh and parent raw capture. No game, test,
debugger or decoder was run. No original save sample is available.

LoadGame keeps its incoming slot context in R7 at0x44BC8 and stores the loaded
TACTVER word as a dword at context+0x118 at0x44C94. A direct caller loads the
context address0x156938 at0x19E00. Tactical init passes that same address to
allocation, soldier reading and tactical-state reading at0x3D220/0x3D228/0x3D230.
The tactical-state reader preserves it in R6 at0x4424C and forwards it to
the item-body reader at0x444AC.

Both original item allocation and body reading test context+0x118 at
0x431C0 and0x43270. Zero selects a one-byte count; nonzero selects four bytes.
This field is0x156A50 in the slot context, not Map+0x118 at0x113AD0. Earlier
claims that it was write-only residue or an unrelated Map cell were false.

The port allocation path already used World.nTactVerOut. The later body-read
context instead hardcoded1. For zero or absent TACTVER, allocation therefore
read one byte while body reading consumed four, misaligning the record data.
World now supplies the same loaded value to both reads. The current writer's
TACTVER1/four-byte output remains the same; the on-disk format is unchanged.

The existing direct decoder tests explicitly pass their width argument and
could not detect the World binding error. Current tactical roundtrips use
TACTVER1. A meaningful integration check must clone a valid tactical slot
with TACTVER0 AND a one-byte count prefix, then load through World_LoadSlot
and inspect nonempty item fields/links. Merely changing the version word
while retaining a four-byte count does not form a valid legacy fixture.
That regression is now implemented in test_geoscapeframe. A fixed-purpose
helper clones the valid slot62 tactical save: TACTVER becomes zero and only
the three surplus FLOBJS count bytes are removed. It requires1..255 items,
valid17-byte records and an unused target slot67. The full test fails clearly
if these prerequisites are unmet; no count truncation is allowed.

The World load checks the saved count, an item's object/scalar values, and
both next-item and soldier-owner links against the new live arrays using the
serialized indices. The helper snapshots the saved record after writer ID
assignment. The clone load runs after RNG-sensitive scenarios; original slot64
is loaded last. Per-run save flags and the --world-controls subset guard prevent
using stale slots, and only owned files are removed. This remains unexecuted;
no compatibility pass is claimed. The fixture targets a zero selector;
absent-TACTVER and original save samples have not been exercised.

The checked soldier/item references use separate roster/item array-index
namespaces, four-byte IDs,0x7FFF null and0x7D00 inventory sentinel. No other
reference-owner mismatch was confirmed in this bounded review.

[Raw context, literals, callers and width gates](flobjs-count-selector-raw-2026-10-28.json).
[Prior IDB note](flobjs-count-selector-idb-before-2026-10-28.json).
[Luna's reference audit and test limits](../../.workers/luna-wire-links/RESULT.md).
The parent corrected source comments and the IDB note at0x44C94, checked
readback and saved with backup. Runtime and original-save acceptance remain open.

Separate unmodified robustness finding: Save_ReadLinkedSoldiers compares a u32
wire ID through a signed cast, so a malformed high-bit ID can pass its upper
bound check. Valid original IDs do not use those values. This is outside the
valid-save parity repair and was not changed.

Both full incremental default builds completed with exit0,68 executable outputs
and zero compiler diagnostics. [Current source/image identities](lifecycle-width-build-artifacts-2026-10-28.json).
Compilation does not establish a pass of the new World regression.
