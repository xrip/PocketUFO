# Deferred type-9 follow-up mission

Status: fix applied and verified. The [targeted and full core gate](../../.workers/luna-runtime-footprint-gate/green-fullgate-f57e5f5bfb89486c9390088c9bc525ed/RESULT.md)
passes the real-tick case and both 67-test suites with clean builds.

## Original sequence

Raw ARM in `Tact_CheckBattleEnd` checks mission type 9 and state byte
`+0x15148`, then calls shutdown with argument 0:

- `0x365A8 E3A00000`; `0x365AC EB001BED` calls `0x3D568`.
- The next stores select type 4, mode 2, terrain variant 0, selector byte
  `+0x15114 = 0`, and `+0x15115 = 0`.
- `0x365E8 EB0017D8` calls init at `0x3C550` with argument 0.
- `0x365F4 E3A02001` and `0x365F8 E7C52003` write frame scratch 1.

That scratch value is intermediate on the timer path. `0x275D8` calls the
active-unit handler; `0x275DC` loads zero and `0x275E8` stores it to scratch
before the timer returns. Final scratch must remain zero.

The [raw ownership audit](../../.workers/luna-hook-wiring-audit/SAVE-DEBRIEF-GAPS.md)
records the in-tick caller chain and native hook bindings. Comments and
function names were navigation aids, not behavior proof.

## Native ownership gap and fix

The existing Win64 timer guard delays map destruction until its active
action stack returns. The old init callback still ran immediately and was
rejected because that old map remained live. The timer then freed the old
map without creating its replacement.

Mode-0 restart now records a world-owned request. At the outermost timer
return, the dispatcher copies the rewritten selector byte from the old map
to the World seed, consumes both requests, frees the old owner, then creates
the replacement. The selector is a layout/template input, separate from the
four-layer backing table. Tactical cleanup also clears the restart request.

Mode 1 remains synchronous because its load buffer is borrowed for that call.
The existing unchecked init-result contract is retained. No retry or early
free of the active map was added.

## Regression purpose and evidence

The [valid before-fix run](../../.workers/luna-runtime-footprint-gate/frame-rednext-95021ab3e58f4152af06e41e6835b0fd/RESULT.md)
uses normal queued deaths and `World_GameTickOnce`: 19 earlier enemies are
removed, one retained enemy remains in both active list prefixes, and the
natural reserve/hidden counts are zero. Unused array tails remain diagnostic
data; they are not required to be null. The last queued death reaches the
real end-check hook. Exactly two checks fail: no live replacement map, and
World selector seed 3 instead of 0. The test does not crash.

The fixed case must leave a live type-4 map, mode 2, zero selector/terrain
fields, drained requests/depth, and final scratch zero. It never writes the
dispatch-depth or pending-request fields to trigger the behavior. This is a
controlled World lifecycle test, not a full played campaign to the final map.

All listed replacement checks pass: the natural purge completes 19 removals
in 157 death ticks, then the retained enemy's queued death returns with the
new live map and selector zero in both World and map storage.
