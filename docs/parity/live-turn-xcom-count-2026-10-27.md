# Live XCOM count at turn start

The old port copied the XCOM roster count before `BeforeEnemyStep`. That hook
can return a controlled human to XCOM. The reset loop then missed the returned
unit. The port now reads the live count after the hook and on each loop pass.

## Original instruction evidence

The independent review used raw bytes and instruction operands from
`pufobeta.exe.i64`; IDB comments and pseudocode were not evidence.

| Address | Raw evidence | Contract |
|---|---|---|
| `0x4CA48` | `DA FB FF EB`, BL target `0x4B9B8` | Run the pre-step hook before the reset loop. |
| `0x4CAFC`, `0x4CB34` | `0C 30 D4 E7`, `LDRB R3,[R4,R12]` | Load the XCOM count after the hook and reload each loop pass. |
| `0x4A64C..0x4A66C` | Operand tests at Soldier offsets `0x17`, `0xA0`, `4` | The return path requires nonzero status, type at most 8, and flags bit `0x20` clear. |
| `0x4A6A4`, `0x4A6B4`, `0x4A6BC` | Append pointer, increment count, store count | The hook can add the same controlled unit back to XCOM. |
| `0x4BB88` | `2D FA FF EB`, BL target `0x4A444` | This updater call is in the hook's enemy-roster loop. No civilian-count mutation was found in this path. |

## Port and adaptation

`TurnUnitLists.pnXcomCount` points to the existing long-lived
`PufoWorldState.nStepXcomCount`. `World_ClickEndTurn` seeds it from the map;
the existing thunk synchronizes it around real `BeforeEnemyStep`.
`Tact_BeginEnemyTurn` reads the mirror after the callback and during iteration.
This replaces the stale snapshot without adding another callback or changing
civilian handling. No original behavior deviation is intended.

[Frozen source review and hashes](../../.workers/luna-integration-review-current/TURN-REVIEW.md)
cover the header, turn code, World wiring, all callers, and the fixture.

## Verification and limits

The same corrected test calls real `BeforeEnemyStep`, transfers a controlled
human back to XCOM, and checks the old and returned members' AI/spotting reset.
The frozen old header uses the fixture's old-field branch; both versions compile.

- [Old core](../../.workers/luna-turn-count-finish/runtime-isolated-20261027T125119Z-2a1e2dcc/logs/old-ctest-debug-red.log): exactly the reset assertion fails.
- [New Debug](../../.workers/luna-turn-count-finish/runtime-isolated-20261027T125119Z-2a1e2dcc/logs/green-ctest-debug.log): passes.
- [New Release](../../.workers/luna-turn-count-finish/runtime-isolated-20261027T125119Z-2a1e2dcc/logs/green-ctest-release.log): passes.

The corrected identical fixture hash is
`6DE575191C447C90587F685B7F25BBFC23F1275C0A619E311311E618C4803CF8`.
An earlier invalid fixture assignment caused a compile failure; that failure
does not count as proof of the game defect. No shadow-only hook field is used.
This fixture does not prove a complete live battle, and the shot probe did not
show this edge occurring. The combined build/test gate is recorded separately.
