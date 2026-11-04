# Mission return: loot warning and shared state

Checked 2026-10-28. Source repair and raw ARM evidence; no game, test,
decoder or debugger execution. Full gameplay acceptance remains open.

## Confirmed faults and changes

The port reversed the ammo-shortage message gate. Entry 0x6835C saves the
low byte of R1. At 0x68EB0..0x68EDC the original reloads it, tests zero and
calls message 40 with BLEQ. Arrival passes R1=0 at 0x80E58 before the call
at 0x80E78. Held-base shutdown passes R1=1 at 0x3E004 before 0x3E00C.
The C body used a true test, suppressing the arrival warning and enabling
it for held-base shutdown. It now uses !bSuppressAmmoShortage; caller
values retain the original contract.

Both shortage branches write a full word of 1 to 0x1870C8: STR at 0x68930
and 0x68EC0. The latter's literal at 0x68EB8 resolves through 0x68A94 to
0x1870C8. The port exposed a byte pointer and cast the World u32 owner,
which could preserve its upper bytes. TacticalLootEnv now exposes
pdwUiRefreshFlag as u32*, directly bound to World.nShortfall1870C8.

The stores-shortage message also replaced a live argument with zero. Raw
0x68914 reads the low byte at 0x15967C. The craft-index use at
0x7FBC0..0x7FBF4 and the event write at 0x807B8 establish its owner.
Luna restored a pointer to World.nInterceptShipIdx in TacticalLootEnv;
the message reads its low byte when posted. A pointer matters here:
the event loop can change this cell after building its contexts and before
a later craft lands in that same pass. No radar subsystem was added.

## Connection evidence and limits

Arrival sets Ship+6 to 1, clears contact/mission/visibility state, and calls
loot with argument zero. Loot requires both Ship+6 and Ship+0x34, then
clears +0x34 before processing. These are distinct landing and pending
delivery flags. World_FillTacticalLootEnv supplies live craft/base/object
owners and counts to both World_ProcessEvents and shutdown outcome.
An independent Luna xhigh source/raw review confirmed these handoffs and
the reversed message gate. It did not execute the port.

The checked raw shutdown ranges cover unit-removal predicates and normal
mission outcome calls: 0x3DD00..0x3DE48 and 0x3DF10..0x3E060. They agree
with the current source in those ranges; this is not a full shutdown audit.
The checked save calls write/read the landing and pending bytes at offsets
+6/+0x34. The raw post-load normalization gate at 0x45014..0x450D8 agrees
with the port's pending-flag and off-map-coordinate test. This does not
prove the whole save format or compatibility with unavailable original saves.

## What the tests check

The existing test_ship shortage cases had booleans opposite to their own
stated purpose. Raw instructions justify correcting the calls: false must
announce, true must suppress, and both must replace the refresh word with 1.
The fixture now seeds 0x12340000 so a byte write cannot satisfy that check.
Its stores-message case supplies a live index0x101, then changes it to0x202
for the next call. Message C0 must change from1 to2. This checks the live
owner and low-byte rule separately from the message's craft/object fields.

New TestArrivalDeliversLootOnce in test_strateg calls production
return-order handling after prepared target removal, then uses the computed
home-flight duration for the arrival boundary. It then calls production
StrategList_ProcessEvents with a real TacticalLootEnv. Craft index0/type1
returns to home base2 with 9 cargo units, adding to stock7 for total16.
Assertions cover landing, pending-flag clear, cargo removal, home marker,
message40/base2/ammo52, full refresh-word replacement, and no repeated
credit or warning on the next event pass. A direct second loot call with
the pending flag clear and new stores aboard must leave those stores alone.

The contexts are prepared by the test. This checks two production game
functions together; it does not test World callback construction, battle
victory, return-route scheduling, native UI or save/load. The older arrival
case supplies no loot env and checks arrival fields only. Neither old
passes nor compilation establish execution of these new assertions.

## Evidence

- [Argument, callers, shutdown and save raw trace](mission-return-handoff-raw-2026-10-28.json).
- [Refresh-word stores and backed-up IDB note](loot-shortage-word-raw-2026-10-28.json).
- [Live stores-message owner trace](loot-stores-live-owner-raw-2026-10-28.json).
- [Luna's scoped review and implementation](../../.workers/luna-mission-return-audit/RESULT.md).

Parent updated IDB notes at 0x68EDC, 0x68930 and 0x68914, preserving previous notes,
guarding against concurrent changes, checking readback and saving with backup.
The [build manifest](mission-return-build-artifacts-2026-10-28.json) records
the final source and binary identities. Both full incremental default builds
completed with exit0 and zero compiler diagnostics. No tests or game flows
were executed; the latest assertions still require the authorized test host.
