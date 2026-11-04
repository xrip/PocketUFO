# Signed SiteId and byte-key base lookup

Status: source repair reviewed; full Debug/Release builds pass. Assertions unrun.

The [raw functions](base-search-contract-raw-2026-10-29.json) have different
contracts. Base_FindIndexBySiteId @0x5A8D0 bounds its loop by the signed base
count and compares the signed BaseRec+4 byte with the full unmasked i32 key.
Base_FindIndexByTypeByte @0x6AC30 masks its key to u8, compares the same signed
field and has no nonempty-loop bound in the original.

The port had narrowed the World SiteId hook to u8 and reused TypeByte. It now
has distinct public contracts over a shared bounded signed-field loop. The
World hook preserves its i32 key; the transfer diagnostic sign-extends its site
byte as raw0x99AC8 does. Byte-key grid wrappers retain their previous normalized
key and safe no-match behavior. The unbounded original search is not recreated
as an unsafe scan.

Tests distinguish -128 from128 and -1 from255, reject256 instead of aliasing
key0, and retain ordinary/empty/missing cases. The fresh two-base World fixture
checks both its real normal callback result and key+256 rejection. This is a
callback contract check, not proof of lifetime campaign counts reaching256.

Both full builds returned exit0,68 executable outputs and zero diagnostics;
the [manifest](base-search-build-artifacts-2026-10-29.json) identifies source,
data and images. No test, game, decoder, debugger, UI or audio device ran.
The [worker audit](../../.workers/luna-body-basegrid/RESULT.md) records all five
members and the grid-slice/return adaptations.

The two search-site notes were guarded against their captured values, read back
and saved: [raw/backup](base-search-contract-raw-2026-10-29.json),
[saved notes](base-search-contract-idb-after-2026-10-29.json).
