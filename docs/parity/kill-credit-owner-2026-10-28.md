# Casualty-to-shooter kill credit

Status: source repair reviewed; both full builds passed. Assertions remain unrun.

## Raw contract

The [instruction capture](kill-credit-raw-2026-10-28.json) records the credit
increment, reset, consume and caller setup. `Tact_KillUnit @ 0x4E430` saves
incoming R0 in R8 at `0x4E440`. For type>8, excluding16/17, with HP0, it
increments a dword at that state+`0x111D0` (`0x4E47C..0x4E490`). It returns
early for an already-dead flag before this increment.

Queue processing clears the same dword at `0x541DC`. At `0x54780` it reads
only the low halfword, adds it to Soldier+`0xFA` with a halfword store at
`0x54788`, then clears the whole credit dword at `0x54790`.
The credit is per action; it is not a cumulative mission kill total.

The port instead bound `KillCtx.pnKillCount` to `GameMap.nKillCount` at both
World kill contexts. No increment reached the queue's `uKillCredit`; existing
mission-counter assertions encoded that same mistaken owner. The queue member
was also modeled as a halfword plus two gap bytes despite its dword writes.

## IDB changes and checked limits

The named TactState member is now unsigned32 at original+`0x111D0`, consuming
exactly its former two-byte gap. The next queue field stays at+`0x111D4`.
The IDB size70120 and every other member's name, type, offset and size are
unchanged: [backup](kill-credit-type-before-2026-10-28.json),
[preview](kill-credit-type-preview-2026-10-28.json),
[saved readback](kill-credit-type-after-2026-10-28.json).

The function's first argument changed from Map* to TactState*. Two fall calls
pass the state held in R10 from the incoming R0; spawn paths load the queue
pointer through global `0x157198`. This agrees with the callee's credit access.
ARM32 register input locations and the two modeled pointer widths are checked;
unused trailing parameters cannot be recovered from these uses alone.
No exact debug identifier or C++ this/varargs/aggregate-return claim is made.
The existing int return remains a hypothesis; the checked callers ignore the
value. This scoped correction is not a completed whole-function ABI proof.

The [prototype backup](kill-credit-call-contract-before-2026-10-28.json) and
[readback](kill-credit-call-contract-after-2026-10-28.json) preserve that limit.
The [application check](kill-credit-application-2026-10-28.json) shows the member
used in both death and queue decompilation; raw instructions remain the proof.
Notes were guarded, read back and saved with DBFL_BAK. No instruction changed.

## Source repair and fixture scope

The production edits now bind both World contexts to the live queue credit;
both contexts guard that queue pointer before binding. The source member is a
dword. Interim parent builds of the pocketufo target returned exit0 with zero
diagnostics in Debug and Release:

- `.workers/luna-mission-init-audit/debug-kill-credit-game-compile.log`
- `.workers/luna-mission-init-audit/release-kill-credit-game-compile.log`

The affected Debug targets test_unit, test_vis and test_tactstate also compile
with exit0 (`debug-kill-credit-leaf-compile.log` in the same worker folder).
No assertion was executed. This does not cover the still-changing World fixture.

The final World fixture calls Unit_EnqueueIfApSpent, pumps command8 through
World_GameTickOnce, and checks the generated credit reaches an operative shooter,
the dword clears and the queue drains. It uses the type22 transform and a prepared
completed-shot prefix; it does not execute projectile damage or native input.
It requires another counted enemy to remain. Direct fixtures cover the dead
guard, type exclusions and low-half payout from a dword containing0x10003.

The [terminal raw gates](kill-credit-terminal-raw-2026-10-28.json) show why this
scope matters: the battle-end call at0x4EFC8 precedes payout. Tick hold causes
return at0x4EFD4 and, after the fall callback,0x54494, before0x54770..0x54790.
The last-enemy/no-reserve path sets that hold, so the original leaves its credit
unpaid. World defers tactical storage release until dispatch unwinds; it does
not bypass the hold. No extra final-kill award was added.
Full chain/roster details are in the
[worker report](../../.workers/luna-casualty-completion/RESULT.md) and
[raw excerpts](../../.workers/luna-casualty-completion/RAW_ARM.txt).

Both full configurations passed with68 executable outputs and zero diagnostics.
The [manifest](kill-credit-build-artifacts-2026-10-28.json) records identical
source fingerprints before/after builds and image hashes. No assertion or
gameplay acceptance is claimed. Runtime checks remain open.
