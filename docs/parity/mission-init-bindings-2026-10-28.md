# Mission initialization bindings and restart lifetime

Status: bounded source/raw audit complete; restart-layer lifetime repair applied.
Debug/Release game and World-test targets compile without warnings/errors.
No game or test executable was run. Runtime checks require another machine.

## Top-level hook instances

All11 TactInitHooks fields and their World user pointer are installed before
World_InitTactical calls Tact_InitMission. The mission-live flag is published
after that call. The wrapper bodies reach production implementations and do not
require that later live flag. Normal App-owned hooks provide the bank, assets,
font and options those implementations require.

| Hook | World target | Raw destination and call sites |
| --- | --- | --- |
| PanelCtor | World_TactPanelCtorHook | 0x3E730 at3C750,3C79C,3C7D0,3C804,3C838 |
| PanelStyle6to11 | World_TactPanelStyleHook | 0x3E914 at3C8C0,3C938,3C9B4,3CA30,3CAB0 |
| PanelWrapText | World_TactPanelWrapHook | 0x37144 at3D3C8,3D4C4 |
| LoadUiPads | World_TactLoadUiPadsThunk | 0x3B068 at3CB84, bytes37f9ffeb |
| InitUiControls | World_TactInitUiControlsThunk | 0x3B498 at3CB88, bytes42faffeb |
| LoadTactSprites | World_TactLoadTactSpritesHook | 0x257C4 at22E4C and4444C, bytes5c0a00eb /dc84ffeb |
| SoundInit | World_TactSoundInitHook | 0x9E268 at3D270, bytesfc8301eb |
| SoundPlayMusic | World_TactMusicHook | 0x9E130 at3D278, bytesac8301eb |
| SoundApplyMusicVolume | World_TactMusicVolHook | 0x9E454 at3D27C, bytes748401eb |
| SoundApplySfxVolume | World_TactSfxVolHook | 0x9E4DC at3D280, bytes958401eb |
| OpenInventory | World_TactOpenInventoryThunk | 0x16EF4 at3D0B8, bytes8d67ffeb |

Inventory's forwarding body synchronizes live UI data, supplies the selected
unit/current map/cursor and calls TactHud_OpenInventory. Its map/state guards
are satisfied during construction. The leaf requires XCOM and an eligible
selected unit, not bTactLive. The port does not overwrite its screen write in
a later World_InitTactical tail. Raw3D0B4 selects a unit,3D0B8 opens inventory,
and3D0BC branches onward; the call is not a missing late-stage UI connection.

The mode1 branch installs all seven TactLoadHooks, their World user, the open
save stream and the RMP callback before entry. The seven adapters reach sprite
loading, index building, RNG and state/reset/roster application without a
mission-live gate. Save_ReadRmp is called at3D23C (271b00eb ->0x43EE0), with
World_TactLoadRmpThunk as the port's module loader. This is binding/timing
evidence, not a complete field-by-field proof of those codecs.

## Actual original callers

The raw entry at3C550 saves R0 as its mode. Code references are:

| Site | Mode evidence |
| --- | --- |
| 0x44E24 | R0=1 at44E20: saved tactical mission |
| 0x83AC4 | R0=0 at83AC0: normal launch |
| 0x365E8 | R0=0 at365C8 and unchanged before the call: Mars restart |

The value2 at365BC is stored into mission-mode data at365C4; it is not the
Tact_InitMission argument. Thus the observed calls use the World wrapper's
supported0/1 modes. The data reference at189A28 belongs to .pdata and contains
the function start/unwind entry; it is not an additional observed code call.

## Restart-layer lifetime repair

Raw365A8..365AC calls shutdown(0), then writes the new mission header. The layer
store at365DC happens after shutdown. R6 is zeroed at34FE8 (0060a0e3) and has
no later code assignment before that store; literal-pool data was excluded.
The restart then calls init(0) and writes frame scratch. Original Map storage
is persistent, so these writes do not target a freed Map object.

The port used env.pnMapLayers=&pMap->nMapLayers. Outside an active game tick,
World_EndHookShutdown can free that heap Map immediately. The later layer store
therefore used a dangling pointer, and the replacement could also be seeded
from a stale World layer byte. During a tick, the existing deferred-release
mechanism kept the Map alive, hiding this direct-path gap.

World_MissionEndCheck now snapshots the live layer into World.nMapMapLayers and
binds the restart field to that stable byte. If release is pending and the tick
still owns the same old Map, it mirrors the rewritten header back there before
return. The existing tick-end handoff therefore still reads the zero layer and
builds the replacement only after the old tick unwinds. The direct path never
writes through the freed layer pointer.

## Validation still needed

- Run the existing queued Mars restart fixture; it checks replacement type4,
  mission mode2, zero layers in the World/new Map, and drained pending flags.
- Exercise a direct end check with both initial layer cells1 and the Mars
  restart condition. Require a live replacement with both layer cells0 and no
  freed-storage access. The in-tick fixture alone does not prove this path.
- Run the pending independent PSI setup and Debug panel heap fixture, then both
  full suites. No local UI/runtime test is authorized.

| Current artifact | SHA-256 |
| --- | --- |
| World source | 9E238BB8101EE87575ECDB2A6025C5E1F1A209430677BC83FC1EF8EF731CFB64 |
| Test source, unchanged this pass | 053E35A01BF54E08BD4B2F1218E520BA7C8A17485504F9FB138F7C897CB30A9E |
| Debug EXE | E363551AEC51845D731D22DD1FD88E7F781839B511B2C6D5459940B1864FC071 |
| Debug PDB | 5512E23611DB8CB95BD19B0E9E47B4CD89F7AD0A4AD30168C195DC02A8C4CC2F |
| Release EXE | 4ED8F444A45558525C472BBDF567D4C4C21EDDBFD9A500BC934A02BB8B7AF1F6 |

Compile logs: [Debug](../../.workers/luna-mission-init-audit/debug-compile.log),
[Release](../../.workers/luna-mission-init-audit/release-compile.log).
Preimage: [.workers/luna-mission-init-audit/pufo_world-before-layer-lifetime.c](../../.workers/luna-mission-init-audit/pufo_world-before-layer-lifetime.c).
The audit covers these hook instances, modes and restart-layer storage only.
It does not prove every nested environment or whole-game parity.
