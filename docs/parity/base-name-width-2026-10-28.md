# Generated base names: live width and saved width

Status: source repair, full Debug/Release compile passed; assertions unrun.

## Reachable difference

Original `BaseFac_FormatCostLabel @ 0x6A404` formats a region prefix, a dash
and a signed-byte ordinal. `LDRSB @ 0x6A470` and the signed increment sequence
are in the [raw capture](base-name-width-raw-2026-10-28.json).
The shipped build-region prefix maximum is16 bytes. Ordinal100 therefore
needs20 text bytes; ordinal-128 needs21. The former 19-byte port limit dropped
valid suffix bytes. Base loss lowers the active base count without lowering
the ordinal, so the eight-active-base limit does not prevent these values.
The [worker evidence](../../.workers/luna-name-entry/BASE_LABEL_RAW.md) records
the raw callers, loss/rebuild path, original file hashes and static region bytes.
No loss/rebuild sequence was played.

## Exact repair

`Base_CreateNamedEntry @ 0x66930` originally copies to NUL at site+`0x1A`.
The next dword begins at+`0x30`; the record stride is64. The port now models
the name as22 contiguous bytes, consuming exactly the former two gap bytes.
The copy permits21 text bytes plus NUL. Static assertions preserve the name
offset, all following field offsets and the full record size. The initial-base
rename uses this same name member's size.

This bound covers the two original production name sources and shipped build
region text. Arbitrarily longer replacement language strings are not proved
equivalent to the original unbounded copy.

The save format remains unchanged: the original writes and reads exactly20
bytes from+`0x1A` at `0x3F31C` and `0x40440`. `SaveBaseFacility.szName[20]`,
its61-byte wire record and the production save join are unchanged. The extra
live name tail is not saved by the original format either; a cold load cannot
be claimed to restore it.

## Verification and limits

`TestGeneratedBaseLabelCapacity` in `tests/test_geoscape.c` calls the production
formatter, creator and save join with the shipped16-byte prefix at100 and-128.
It checks full live bytes/NUL, record fields, counter increments and the saved
20-byte prefixes. It releases the base and strategic allocations it creates.
It is a connected rule/save-join fixture, not a native UI or full file-load test.

Both complete builds passed with68 executable outputs and zero diagnostics;
the [source/image manifest](death-name-build-artifacts-2026-10-28.json) identifies
the exact candidate. No assertions, game, decoder, debugger or audio device ran.

Four IDB notes were backed up in the raw capture, guarded, updated and read back.
The [saved readback](base-name-width-idb-after-2026-10-28.json) records them.
No existing `BaseSiteRec` named IDB type was present; no speculative type was added.
