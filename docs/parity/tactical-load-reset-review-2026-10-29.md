# Tactical load: live input reset

Status: source repair peer-reviewed; both full configurations compile cleanly.
No tests or runtime code ran.

## Fault

Save_ReadTGameData writes its reset directly to original globals after restoring
rosters/items/map. The C reader stages the same reset, but World_TactApplyResetThunk
omitted fields whose live owner is persistent sTactInput. Mission allocation
clears a fresh GameMap and tactEnv, not the whole sTactInput. World_FreeTactical
also leaves these input fields intact. World_TactInputSyncIn/Out leaves these
sole-owned drag/edge slots alone. A load could thus retain prior drag/repeat/edge
state that the original clears. World_PulseTact uses these repeat fields and the
isometric guide reads the edge flags.

## Raw stores and current owners

| Raw store | Original field | Current owner and reset |
|---|---|---|
| 0x444F0 STRB0 at Map+0x3C | inventory drag | sTactInput.bInvDragActive=0 |
| 0x44588 STR0 at Map+0x60 | pulse phase | sTactInput.nUiSlot0=0; existing shared phase pointer also receives0 |
| 0x44560 STR0 at Map+0x6C | drag/repeat state | sTactInput.nUiSlot3=0 |
| 0x44568 STR200 at Map+0x70 | repeat countdown | sTactInput.nUiSlot4=200 |
| 0x44540..0x44558 STRB0 at Map+0x74..0x77 | edge flags | sTactInput.nUiSlot5=0 |
| 0x44570 STR0 at Map+0x7C | slot7 | sTactInput.nUiSlot7=0 |
| 0x44578 STRB0 at Map+0x80 | input slot8 low byte | sTactInput.bUiSlot8Byte0=0 |
| 0x444D8 STR0 at Map+0x2C | shot count | pTactMap.nShotCount=0 |
| 0x44520 STRB0 at Map+0x59 | fire result state | pTactMap.bFireStatus9=0 |

The latter two map fields already start zero from fresh allocation, so this pass
does not claim a demonstrated visible fault for those two alone. Their reset is
now explicit. The repair writes each modeled owner from TactResetState and keeps
loaded cursor/camera/deployment values intact; it does not clear the whole input
record. The former source comment claiming these fields had no readers was stale.

## Six-body audit scope

[Raw evidence](tactical-state-save-raw-2026-10-29.json) contains all bytes and
decoded rows for OBJBEFORE read/write, TGAMEDATA read/write and GAMESTATE read/write.
One component query was used for exactly six members. No transitive expansion or
new names/types were needed. IDB writes/readback: n/a. Member byte coverage: pass.
Child roster/item/map bodies remain separately assigned to workers.

The tactical header is43 bytes, followed by the four roster chunks, FLOBJS, MAP
and V124. The optional V124 tail uses two random draws when missing. The separate
GAMESTATE destination and two-byte object+0xCA fields match the raw instructions.
World_SaveSlot stages live owners; apply-state runs before font use, apply-rosters
before map references, and apply-reset before the mission-init continuation.

[Member contracts](tactical-state-save-result-2026-10-29.json) state input domains,
failure-path adaptations and open limits. Original saves are unavailable. No
native mission/load/input acceptance is claimed. A Luna xhigh peer owns a
regression through the existing World_LoadSlot fixture. The
[peer report](../../.workers/luna-tactical-load-reset-peer/RESULT.md) checks the
raw stores and fixture purpose. The old hook would retain the distinct input
sentinels. The test also requires the loaded deploy flag to remain1, catching a
blanket input clear. Parent removed cursor/camera equality assertions because
the later focus call can legitimately recenter the view.

Debug and Release default targets each returned exit0 with68 executable outputs
and zero diagnostics. Source fingerprint was unchanged across both builds; see
the [manifest](tactical-reset-build-artifacts-2026-10-29.json). Assertions remain
unrun. The two shot/fire assertions are result checks only: fresh map allocation
already masked those missing writes. Slot7 is a reset-contract sentinel, not
proof that normal input changes that field. No native input/load claim is made.
