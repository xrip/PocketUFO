# App confirm and secondary-release hooks

Status: input bridges and the later view-index correction pass AppShell in
Debug and Release. The [initial hook snapshot](../../.workers/luna-runtime-appshell-matrix/job-2b06a2d0391a4d02864462a05fbc187b/RESULT.md)
passed 67/67 in each configuration before that later index correction.

## Original instruction proof

This record uses ARM instruction words and branch/data flow. IDA names below
identify addresses; comments and pseudocode are not evidence.

Geoscape confirm:

- `0x15F48: E5D40044` loads the byte at the state base plus `0x44`.
- `0x15F4C: E21030FF` tests its low byte.
- `0x15F50: 0A000004` skips the geoscape arm when zero.
- `0x15F54: E59F0004` loads the screen-block pointer.
- `0x15F58: EB01D67B` calls `0x8B94C`.

Secondary release and screen 13:

- `0xA0D90: EBFDD315` calls `0x159EC`.
- `0x15A1C: E351000D` compares the screen with 13; the following
  conditional branch skips the call on a mismatch.
- `0x15A24: EB0001F5` calls `0x16200`.
- `0x16278: E350000D` and `0x1627C: 0A000009` select its screen-13 arm.
- `0x162A8: E3A0CC1A` and `0x162AC: E38CC020` form offset `0x1A20`.
- `0x162B0: E7D4000C` loads that state byte; the test and nonzero branch
  at `0x162B4/0x162B8` select `0x162D0`.
- The zero arm passes 0 to `0x3D568` at `0x162C0`, then passes 1 to
  `0x857F4` at `0x162C8`.
- The nonzero arm loads 19 (`0x162D0: E3A00013`) and stores it as a
  halfword at state offset `0x3A` (`0x162E4: E1C403BA`).

## Port connection and test purpose

The App cleared its input-hook table but did not assign
`pfnGeoscapeCloseUi` or `pfnOnCancel`. Both public input routes could return
without their required state change. Shared startup hook setup now assigns
the existing `World_CloseGeoscape` and `World_MissionCancelEnd` bridges.

`test_appshell.c` sends `VK_BACK` down through `PocketUFO_WndProc` and
checks geoscape screen 1 becomes 0. It sends `VK_SPACE` up on screen 13
with a promotion report and checks screen 19 is shown while the same
tactical map and queue owners remain live. The fixture uses minimal World
state and does not prove full mission victory, loot or home arrival.

The promotion route must retain its owners. The unconditional escape
callback is a separate path and cannot replace this cancel bridge.

## Focus bridges in the same snapshot

The App also left the target-row cell and look-mode view-cell callbacks
empty. The [raw focus audit](../../.workers/luna-hook-wiring-audit/NEXT-GAPS.md)
separates their callers and shows camera/cursor changes without a selection
change. Both are now wired and their controlled input checks pass.

The look-mode layer field is offset `0x15138`: words `E3A0CA15` and
`E38CCF4E` form `0x15000 | 0x138`. The IDB symbol `Map_nLayerCount` at that
address must not be confused with `Map_nMapLayers` at offset `0x15114`.

New Game's World hook table now also supplies `App_DrawFrameHook`, matching
the existing save/load callback tables. This closes the static Options-row
pre-I/O rendering gap; a real Options-row draw probe is still due.

## Correct view-cell address

The first bridge wired the call but used the wrong physical index. Raw
screen-2 code reads `Map+0x140F0+4*viewIndex`; physical cells start at
`Map+0x15338`. The same Map literal is used, so the physical index is
`viewIndex-1170`, where `viewIndex=layer*3600+viewY*60+viewX`.

The corrected bridge uses signed 64-bit arithmetic and a flat backing-table
bound, accepting valid negative camera coordinates. [Current App tests](../../.workers/luna-runtime-appgate/retry-fixed-3c17170d4a914f00b415f514ad3b2dd5/RESULT.md)
in Debug and Release distinguish raw tuple `(1,18,-10)` target slot 3500 from
decoy 4670, and `(1,10,10)` target 3040 from decoy 4210. The earlier fixture
seeded the direct-index cell and did not prove this conversion. [Raw audit](../../.workers/luna-hook-wiring-audit/FOCUS-VIEW-INDEX-AUDIT.md).
