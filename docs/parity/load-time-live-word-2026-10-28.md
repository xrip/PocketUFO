# Load restores the live day/spawn word

Checked 2026-10-28. Confirmed source connection fault; no executable was run.

The time chunk contains seven clock values and a trailing u16. Save_ReadTime
at 0x41684 loads destination R1 from literal 0x4169C, whose raw value is
0x1472C8; R2=2 at 0x41688 and the tail branch at 0x41694 reads directly
into that live global. The name assigned by IDA is not needed for this proof.

The port reader correctly stored that field in SaveTimeRecord.nWord1472C8.
World_SaveSlot also wrote it from World.nAlienPoolAddend. However, LoadGame
copied only the seven clock values from its save record into live state.
The day/spawn word stayed at its pre-load value despite a correct file.
World binds that same live owner to the daily counter, spawn early gate and
tactical alien setup; a file roundtrip alone did not test those consumers.

LoadGameEnv now takes the live u16 owner. World_LoadSlot supplies its address,
and LoadGame publishes it immediately after the other time fields. This
placement matters: the original calls Save_ReadTime at 0x44DA0, before
tactical initialization at 0x44E24 and the later legacy mission migration.
Restoring only when World_LoadSlot returns would leave those consumers stale.
Standalone format fixtures without a live owner may leave the pointer null.

## Regression purpose

TestLoadGame saves42 and starts a separate live word at0xDEAD; after the
production driver loads the slot, the live word must be42. The legacy-load
fixture repeats this with its mission-context refresh callback checking42
before migration can spawn a mission. These are driver/fixture checks, not
native UI or original-save acceptance. The old TestTime wire roundtrip remains
useful for format order but could pass while this live-state defect existed.

The saved file layout is unchanged. No original save is available; port-written
fixtures do not establish compatibility with original saves. New assertions
have compiled in Debug and Release but have not run.

[Raw reader, literal and caller trace](load-time-live-word-raw-2026-10-28.json).
The parent preserved the old IDB note at0x41684, wrote the supported finding,
checked readback and saved the database with backup.

Both full incremental default builds completed with exit0,68 executable outputs
and zero compiler diagnostics. [Current source and binary identities](load-time-live-word-build-artifacts-2026-10-28.json)
supersede the mission-return-only candidate. No tests or game flows were run.
