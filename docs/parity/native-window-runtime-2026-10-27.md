# Native window runtime checks

Both invocations of `tools/window_smoke.ps1` passed with exit 0, serially, on
the Debug binary with SHA-256
`31F7A057E896BB543DC0D799E746304DC678CD9CD2309CE6B6926246A0A5EE40`.
The runner hash was
`8B2FDE6E19C370C925B9EE202A140EAAB3E4E6B7048B0F9657DDCD34E53DD9EA`.
No build ran between these two probes. Later App helper edits were not in this
identified binary snapshot.

| Probe | Direct runtime evidence |
|---|---|
| Startup | Real HWND, gamma owner check, App/World New Game, C351 draw and C352 animation callbacks; geoscape enabled, one base, eight soldiers, screen 2; installed timer adapters resolve to `Bridge_KillAll` and `Bridge_StartAll`. |
| Tactical | Real HWND, C350 game tick and C353 pulse callbacks; posted End Turn reaches AI, end-enemy-turn and player-input callbacks. Final input=1, enemy-turn-active=0, tactical=1, screen=0. |

The startup child was PID 22116/HWND 198115668; tactical was PID 33868/HWND
115804498. Both CDB logs contain the detach marker and passed the stable final
log read. The runner then posted `WM_CLOSE` to each verified owned window;
both children exited and no CDB process remained.

Evidence:

- [Startup CDB log](../../.workers/luna-window-smoke/cdb-geoscape-20261027-004017-476.log).
- [Tactical CDB log](../../.workers/luna-window-smoke/cdb-tactical-20261027-004100-638.log).
- [Commands, wrapper exits, hashes and process records](../../.workers/luna-runtime-corrected-port/RESULT.md).

The tactical setup is a controlled fixture. These checks establish the stated
native window/callback path; they do not establish a complete native mission,
physical mouse input, or complete original-game parity.
