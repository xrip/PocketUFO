# Incoming capacity: the negative-site gate is zero

Checked 2026-10-28 against raw ARM instructions in pufobeta.exe. No game,
test, decoder or debugger was run. This closes the specific source-parity
question left by the loot-index report; it is not whole-game acceptance.

## Address and initial state

At 0x67EE0..0x67EF8 the gate is a halfword at
`0x162030 + 2 * (0x173 * requestedSite + objectIndex) + 2`.
For requestedSite=-1 this is `0x161D4C + 2*objectIndex`.
The filter admits 1..69,105..111,122..124,140. Relative to the carry array
at 0x161AD0 these are halfwords within 32-bit words 159..229.

The original PE32 has image base 0x10000. Its .data section starts at
0xA6000, virtual size 929136, raw size 26624, raw file pointer 602112.
The gate starts at section offset 769356, beyond the raw data, so it is
loader zero-fill. IDA marks all 282 bytes at 0x161D4C..0x161E65 as having
no stored value. Its returned 0xFF fill must not be called original data.

## Reachable writes

References to every byte of 0x161AD0..0x161ECF were collected. They lead
to six routines: 0x78D90,0x78E78,0x7A9B4,0x7AC38,0x7B014,0x7C9E8.
The first two read staged counts; 0x7C9E8 reads crew entries 31,32,34,35.
0x7B038..0x7B058 clears 256 words with zero.

The nonzero writes are the decrement at 0x7AA34..0x7AA3C and increment
at 0x7AFD0..0x7AFD8. Their indices come from the slot panel's row IDs.
The builder starts its object walk at 1. Its raw tests at
0x7B184..0x7B1A4 admit only IDs 1..24 or 31..51; other temporary row
pairs stay zero and the later row pass skips them. The row pass stores
the object ID, not its packed row number, and sorting only permutes rows.
Thus these writes cannot reach words 159..229, regardless of object count.

This is a static normal-flow argument. Arbitrary memory corruption or a
debugger changing scratch memory is outside it. A negative index other
than the original callers' -1 is not covered by this finding.

## Port consequence and test purpose

The port's -1 scan result of zero matches this original state. The loot
adapter's current-storage-only sum therefore also matches this capacity
term; adding site inputs there would not change its result. No production
behavior or test expectation was changed in this pass.

The existing negative-index assertion in test_geoscape checks that live
incoming-site quantities are not accidentally used as the gate. It is a
local helper check with prepared records. It does not prove screen wiring,
gameplay, or the original memory invariant: the raw trace above supplies
that evidence. The latest tests still have not run.

Raw evidence: [gate, writers and references](incoming-capacity-gate-raw-2026-10-28.json)
and [row-ID write instructions](incoming-capacity-row-id-raw-2026-10-28.json).
The earlier IDB comment is preserved in the first artifact before correction.
[Counter initialization and IDB readback/save](incoming-capacity-idb-update-2026-10-28.json)
records R9=1 at 0x7B78C, the later row-ID increments, and the checked replacement
note saved with a database backup.

## Compile verification

Both full incremental default builds, Debug and Release, completed with exit0
and zero compiler diagnostics. No executable was run. Comment-only source changes
still produce new debug information and binary identities; the
[current manifest](incoming-capacity-build-artifacts-2026-10-28.json) supersedes
the loot-index build identities. Tests and full playable flows remain unverified.
