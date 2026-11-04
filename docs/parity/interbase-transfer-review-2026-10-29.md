# Inter-base transfer connections and joined World fixture

Status: bounded raw/source review and compiled fixture; no assertions ran.
No new ordinary-path port defect was confirmed.

## Normal route and identity

Source is the selected base index. Destination is a separate strip index;
the transit site stores the destination BaseRec+4 key, then arrival resolves
that key back to the current live base index. World supplies the live source,
destination, cursor, overlay, count and strip-gate owners.

The transfer-built hit-test path selects minimum X10 at0x6723C and rejoins
normal row checks. Draw stores the destination at0x98994, its base key at0x989C4,
and clears the picker when destination differs from source at0x989D0..0x989DC.
The prior IDB statement that this clear was unreachable was false.
[Backup/raw](transfer-picker-raw-2026-10-28.json) and
[saved readback](transfer-picker-idb-after-2026-10-28.json) record the correction.

| Actor | Transit and arrival |
|---|---|
|Existing Soldier|Store roster ID; set nKind255; arrival updates that same Soldier's base|
|Existing craft|Store craft ID; clear source home/hangar; arrival rehomes the same craft|
|Items, engineers, scientists|Subtract source quantities; add destination quantities|
|Newly hired people|Separate recovered-person count creates new Soldiers; not the existing-ID path|

The [worker audit](../../.workers/luna-interbase-transfer/RESULT.md) and
[raw capture](../../.workers/luna-interbase-transfer/raw-interbase-transfer.json)
record the ordinary UI, source/destination, commit and hourly-arrival paths.

## Positive joined fixture

After recruiting one Soldier into base1, TestWorldPurchaseHireDelivery now
transfers that same Soldier to base0. It selects source through World input,
holds the cursor over another destination across a draw, verifies the picker
closes without changing source, and finds the Soldier row by both marker and ID.
Commit uses the real control after destination selection, not a direct call
through the overlay's blocked control path.

The fixture checks transit state and the stable destination key; it explicitly
selects fast time through World input and checks elapsed World pulse time.
Before the last hourly step, the same object is still in transit. Arrival and
the following pulse must preserve total roster size, next-ID and funds, move
only that Soldier between base counts, and avoid duplicate delivery.
It reacquires the close control after the screen transition.

Both complete Debug/Release builds pass with68 executable outputs and zero
diagnostics. The [manifest](interbase-clock-build-artifacts-2026-10-29.json)
records source fingerprints before/after, original data hashes and image hashes.
No test, game, native timer or desktop input was executed. Prepared base/quarters
remain setup conditions; full construction and other transferred actor classes
are outside this joined fixture.

## Preserved original edges and fixture limits

- Overlay-open release can use a stale row cache and queue a different actor
  from the aimed row. Choosing a destination afterward does not repair that
  staged mismatch. The raw path can strand a craft; the port preserves it.
  The older World case leaves the cursor at source and directly invokes commit;
  it checks an internal edge, not ordinary destination-first transfer.
- Staff rows display free headcount, but the increment gate uses total stock
  minus pending quantity and can queue assigned staff too. No parity-breaking
  restriction to free staff was added.
- Soldier counters are32-bit while transfer IDs are16-bit; base counters are
  wider than their byte keys. Large lifetime values can alias conditionally.
  No ordinary fixture establishes that those counts are reached.

These raw/source findings are not native runtime results or a full campaign pass.
