# Older BASES load: live-state join repair

The original reads only four hangar bytes for signed save versions below 3 and
does not read the craft-home byte. The port reader respected those gates, but
its later record-to-live join copied all eight bytes and craft-home. Loading an
old slot into an existing campaign could therefore replace preserved live state
with stale record values.

## Evidence and repair

Raw `0x403AC` loads the version with LDRSB; `0x403B0` compares it with 3.
R2 starts at 4, and `0x403C0` conditionally sets it to 8 for the read at
`0x403C4`. A second signed load/compare at `0x403D8..0x403DC` guards the
craft-home read at `0x403F0`. Parent checked these bytes independently.

`Save_BasesRecordToLive` now takes the loaded version and writes the same fields
as the original. `LoadGame` passes its signed `nVer`, before the final stamp to
version 3. Version 3 still loads all eight bytes and craft-home.

The [six-body report](../../.workers/luna-body-save-base-product-research/RESULT.md)
and [raw evidence](../../.workers/luna-body-save-base-product-research/raw.json)
cover BASES, PRODUCT and RESEARCH. The
[peer review](../../.workers/luna-save-bases-join-peer/RESULT.md) checks the fix,
version lifetime and regression scope.

## Test purpose and limits

`TestLegacyProductWorldLoad` now tests the real `World_LoadSlot` join using the
existing structural v2 clone. Its BASES payload removes the four upper hangar
bytes and the craft-home byte. Live values differ from stale record values.
Post-load assertions require the saved first four bytes and funds to load, while
the unread live tail and home byte remain unchanged. The former unconditional
join would overwrite those distinct live values.

The older direct chunk test checked wire-state leftovers only; it could not
detect this missing version gate in the live join. No original save is available,
and a synthetic slot does not prove original-save interoperability.

Both full Debug and Release builds passed with 68 executable outputs each and
zero diagnostics. Source fingerprint was unchanged across compilation; see the
[build manifest](base-load-join-build-artifacts-2026-10-29.json).
No tests, game flows, decoder, audio/device code or debugger ran.
