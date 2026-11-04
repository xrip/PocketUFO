# Launch-first follow-up

The current work uses real game flows first. All execution below used separate
PufoTest desktops, with the input desktop kept at Default. Full parity remains open.

## Enemy-turn crash repaired

The natural base-defense flow crashed in Release. A private Debug replay located
the access violation in `TactState_EnqueueTurn`, reached through
`AfterEnemyStep -> EnemyAi_EnqueueBurst -> World_TactFaceTowardThunk`.
The callback received `PufoWorldState *` from EnemyAiActCtx but cast it to
`TactState *`. The debugger shows the World address used as the queue owner and
an invalid derived entry address:
[first stack](../../.workers/port-goal/base-crash-542c3b842a714bb78b8a50bc796c5f04/cdb.log),
[pointer values](../../.workers/port-goal/base-crash-37f3cb6b93424202826c8150a0ce6beb/cdb.log).

The callback now reads `pWorld->pTactState`. Its two direct fire callers also
pass the World, so all three callers use one context contract. There is no
queue-size guard hiding the bad pointer and no change to combat rules.

Raw `TactState_EnqueueTurn @0x49B84` loads `pMapTactical` at0x49B88/0x49B94,
reads its halfword queue count at+0x111D4 and uses56-byte entries. The original
`Unit_FaceToward @0x49BF0` receives a Soldier and coordinates, then calls that
global queue operation. The IDB function comment records this owner contract;
it was re-decompiled, read back and saved.

The same base-defense flow now runs32 full turns without a crash:
[repaired game log](../../build/e2e_base_defense_defeat/out/20261029T054301Z-1236/base_defense_defeat.log).
It ends with3 XCOM,4 enemies and no reserve, after52 observed HP drops and17
roster removals. Its32-turn defeat driver therefore does not pass its terminal
loss check. This is a surviving battle at the driver limit, not evidence of a
new crash or of a completed natural loss.

The short air-to-ground mission also passes after the callback repair, including
player Rifle input and its queued turn/fire:
[result](../../.workers/port-goal/natural-game.log),
[game log](../../build/e2e_normal_ground_play/run_20261029T055406Z_1299/normal_ground_play.log).

## Campaign results

All three [economy flows](../../.workers/port-goal/economy-f9838721eff9422f8264f65a048a8d2a/result.json)
passed on the prior317ACF Release image. The later gameplay change is the tactical
facing callback above; these earlier results retain their exact image scope.

- [Research14 -> Motion Scanner20](../../.workers/port-goal/economy-f9838721eff9422f8264f65a048a8d2a/newgame_manufacture20.log):
  normal research completion, live production order, cold pending-order load,
 431/432 work at hour43, completion at hour44, cost31500, one item and ten free
 Engineers after a fresh load.
- [Research47 -> Laser pistol6](../../.workers/port-goal/economy-f9838721eff9422f8264f65a048a8d2a/research47.log):
 360 measured hourly pulses, normal status acknowledgement, unlock and fresh load.
- [Build stores -> transfer -> delivery](../../.workers/port-goal/economy-f9838721eff9422f8264f65a048a8d2a/newgame_transfer.log):
 player-built destination stores, source stock20->19, one24-hour shipment,
 destination stock1 and fresh-load persistence.

The outside-plane battle reaches native victory with7 survivors,3 enemy turns
and43 actions, then returns to the globe and saves:
[unchanged winning run](../../build/e2e_victory_outside_plane/20261029T053735Z-818/victory_outside_plane.log).
The input driver previously stopped when no affordable prep move remained; it
now uses the existing real End Turn path, within the same12-turn/160-action cap.
The checker now allows side-list compaction while requiring the same Soldier
pointer and a valid current slot. Later crew can prepare their exit after the
first soldier is outside. The recorded run passes; wrong-identity and slot999
negative copies both fail. No battle replay was needed to correct these checks.

[Ending fixtures](../../.workers/port-goal/endings-6bcd11ee1a194e60b1fb7e22de75530a/result.log)
pass bad ending, victory ending, bankruptcy, screen drawing and input return to
startup/New Game. They stage ending conditions; they do not establish a natural
whole-campaign victory or loss. The driver accepts a separate output directory
so older results stay intact.

## Fixture corrections and final gate

The original no-move/PSI tests entered AI work from the initial inventory screen.
They now confirm that screen with real pointer input and take the first World
tick. This also fixes a fixture crash that drew inventory after its controlled
scene had changed the selected unit. The actual PSI control/restoration checks
then run and pass.

The flight fixture now checks the original camera clamp when a real UFO leg ends
outside the fixed hover band, and still checks the visible glyph. Hire and
transfer fixtures acknowledge their real status41 arrival notices before base
navigation. Capacity-only fixtures explicitly allocate a valid product row when
research leaves the panel empty; their unlock bypass remains explicit. The stores
boundary uses current allocation, as ARM0x6E2EC..0x6E30C compares before adding1.
`--hire-delivery` runs this existing fixture directly for short focused checks.

Final full gate: both builds exit0 with no compiler diagnostics; both configurations
pass67/67 tests. [Debug](../../.workers/port-goal/gate-ea26340c9324470db47c9eba0dd0f6c9/debug-tests.log),
[Release](../../.workers/port-goal/gate-ea26340c9324470db47c9eba0dd0f6c9/release-tests.log),
[build/test result](../../.workers/port-goal/gate-ea26340c9324470db47c9eba0dd0f6c9/result.json),
[desktop isolation](../../.workers/luna-isolated-runtime/runs/PufoTest_25080adc3d384aba99841ff82b9769d8/child-result.json).
Source fingerprint before build:
`D6154D53ED076B9471ECD04D8354AB2529704048B7EBD96582AD82B94C98AE1F`.
Algorithm: SHA256 of sorted relative POSIX path + space + lowercase file SHA256
+ LF for434 src/tests/cmake files and root CMakeLists.txt.

Current Debug image: `2B812C47698B352C8A8AC706901AA64C220821750DD218993A85D44F7E55160A`.
Current Release image: `9420F7FB01CD5C7E0EF0CD26BC0B5F87D0847F1DC4DA2AF44FBC112DF0182D85`.

Remaining: finish the original-function/subsystem parity map and explicit
adaptations; complete unproved gameplay edges, including natural final loss and
whole-campaign transitions. Independent original-created saves remain unavailable.
