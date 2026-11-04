# Item-use path and persistent shot weapon owner

Updated: 2026-10-28. Source repair; affected Debug/Release targets compile cleanly.
Runtime checks pending. No game, test or debugger executable was launched.

## Item confirmation contract

Raw0x178D4 plays sample17, writes action mode3, then compares signed32-bit
cost entry3 with the selected unit's u16 TU. With enough TU it clears the action
latch and burst count. Object sort key14 sets screen0, marks item byte8=1,
and charges the current action cost with u16 wrap. Other sort keys open screen5
without spending TU at this point. Insufficient TU pushes text record1, clears
screen/latch/burst and writes action mode12.

Ui_ConfirmUseItem preserves those valid-input branches, and the live World
callback passes the selected unit, weapon and object's sort key. It does not
enqueue an action here. Marked items are later consumed by timed item ticking
and nearby-device scanning. Their full algorithms were not re-proved in this pass.

[Raw evidence](item-use-shot-owner-raw-current.json) records the confirmation
body, resolver prefix/store words and these original resolver call sites:

| Original consumer | BL site to0x2F12C | Encoded word |
| --- | --- | --- |
| 0x32fa8 | 0x330f4 | 0cf0ffeb |
| 0x4b724 | 0x4b824 | 408effeb |
| 0x4b724 | 0x4b8b0 | 1d8effeb |
| 0x4b724 | 0x4b91c | 028effeb |

## Confirmed missing owner store

The original resolver constructs offset0x14050 with instruction words at
0x2F24C and0x2F250, then0x2F254 (036084e7) stores the current object in
[R4,R3]. R4 is the Map argument; R6 is the current weapon/object argument.
This is persistent shared Map state, not just a local working pointer.

The port already assigned CombatCtx.pCtxWeapon for the current resolver walk.
Most World paths initialized that field from Map.pFireArg6, but the assignment
was not copied back. Only the terrain-shot adapter explicitly wrote the Map.
Consequently a proximity resolution could leave the Map owner holding the
previous object even though the local walk used the correct current one.
This repair restores that state effect; runtime gameplay impact is unmeasured.

## Repair and ownership review

Added optional CombatCtx.ppCtxWeapon beside the context's existing live origin
owners. Combat_ResolveShot updates the local pointer and the owner, when bound,
after clearing roster flags and before clearing the hit window, matching the
original store's stage. Standalone contexts may leave the owner null.

All four World construction sites bind the actual Map cell:
terrain construction, spawn-time kill/explosion, active-unit ticking, and the
shared step/proximity combat context. The terrain adapter's separate store was
removed so the common resolver owns this effect. The pointer is valid during
each call, and shared contexts are rebuilt for their current Map.

All found automatic CombatCtx constructions are zeroed/designated-initialized;
global test contexts start zeroed. The manual combat fixture reset now clears
the new pointer explicitly. The context is live port state; no original .dat
or save-file reader/writer was changed.

## Test purpose, not an executed result

- Existing direct-hit case starts its persistent owner at a different object,
  resolves the shot, then requires both local cache and persistent owner to
  identify the current object. The earlier assertion checked only the local
  cache despite describing it as a Map publication; its label is corrected.
- Existing timed sort-key15 case binds a different prior owner, ticks its due
  item, and requires the owner to identify that explosive. Existing origin,
  countdown and item-removal checks remain.

Both assertions target the missing shared-state write. Neither was executed;
no red/green result or current gameplay pass is claimed. The leaf fixture does
not prove the four World bindings at runtime or native priming/throw/end-turn flow.

## Build and latest candidate

Commands, both exit0 with zero compiler diagnostics:

```
cmake --build build --config Debug --target pocketufo test_combat test_unit test_vis test_tactstate test_turn test_step test_enemyai test_ballist test_geoscapeframe test_appshell --parallel 2
cmake --build build --config Release --target pocketufo test_combat test_unit test_vis test_tactstate test_turn test_step test_enemyai test_ballist test_geoscapeframe test_appshell --parallel 2
```

Logs: [Debug](../../.workers/luna-mission-init-audit/debug-shot-owner-compile.log),
[Release](../../.workers/luna-mission-init-audit/release-shot-owner-compile.log).

Source fingerprint, same434-file algorithm as earlier candidates:
**529F9F2E6B287889C2339AF727C7B8E68D504A2A4C4791C764E73C411B9EA5A2**.

| Artifact | SHA-256 |
| --- | --- |
| src/pufo/game/pufo_combat.h | CB21EF67A2D9A6A467783A22C1E8630B20E7B6F17CA3D0C067089D7BA506D605 |
| src/pufo/game/pufo_combat.c | 3D7A6F840E516AEDAA4ED375252B153C30E741EDAE66F7F062AECD83C7ABCE8D |
| src/pufo/app/pufo_world.c | 0A30538858B3878DA14B16CDB5EEC532B26BD8D48EBAC0083C42D61EF174FEF1 |
| tests/test_combat.c | E1AE3ACF88A8FAADF6F4DB2038F9756AAB06F3A556CCB2C66BF52DED910011EE |
| build/src/pufo/Debug/pocketufo.exe | 0EE0AA06313E01D4126125F3C7B7C6BC8E08899D55877D980E963974839E7375 |
| build/src/pufo/Debug/pocketufo.pdb | 877A119623AE7CC43E3B9D42D808C2252D9DCF56269F177C1B6298AD87FD86DB |
| build/src/pufo/Release/pocketufo.exe | 0333290B7F428F9250583DC3E2F8357090B4C94490EE8B369EA5948E18067B4F |
| build/tests/Debug/test_combat.exe | 889F8B277379D3F00B5EB5A6913D524234FEAC0386142250323F34FADDC9F133 |
| build/tests/Release/test_combat.exe | 5B86386FC7D536F256C7214ADB637E46B4C6B18B53FAC2AEE4BFD011491C265A |

This supersedes the font-exit candidate. Source-citation, header-body and import
location ledgers were refreshed. Older runtime passes remain historical.

On the authorized test host, rebuild all targets before running full suites:
only the listed affected targets were built here. Require the new assertions,
real item priming/sub-mode selection, throw/drop, end-turn detonation, nearby
device triggering, visible damage and retained Map weapon identity, then the
complete gameplay matrix. The port remains incomplete.
