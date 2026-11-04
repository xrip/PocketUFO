# FLOBJS allocation count width

Date: 2026-10-27
Status: source wiring fixed; focused world-load regression is written. The executor's first targeted build found two test-source compile errors; both are corrected below the source freeze. No CTest command ran.

## Raw owner

The `+0x118` load in `Save_AllocFlobjs` is relative to a slot-load context, not the original `Map` block. The literal word at `0x3CDB0` is `0x00156938`. `SaveLoad_Slot0` loads that value at `0x19E00` and calls `LoadGame` at `0x19E20`; `Tact_InitMission` loads it again at `0x3D220` and calls `Save_AllocFlobjs` at `0x3D224`.

`LoadGame @ 0x44BBC` keeps its input in R7 (`0x44BC8`). It finds TACTVER, reads its two-byte value into a zeroed dword (`0x44C5C`-`0x44C90`), and stores that dword at `[R7,#0x118]` (`0x44C94`). It calls `Tact_InitMission` at `0x44E24` after the store. `Save_AllocFlobjs` checks `[R4,#0x118]` at `0x431C0`, defaults to a one-byte count (`0x431C4`), and selects four bytes when nonzero (`0x431E0`). The target writer stores TACTVER value 1 (`0x44A14`-`0x44A28`), so target-written saves take the four-byte allocation branch.

`Save_ReadFlobjs` has a different raw input. `Tact_InitMission` loads literal `0x000AAF70` from `0x3CDB8` at `0x3D230`, then calls `Save_ReadTGameData @ 0x44248` at `0x3D234`. `Save_ReadTGameData` copies R0 to R6 at `0x4424C`; R6 is unchanged through `0x444AC`, where it is moved back to R0 before `Save_ReadFlobjs @ 0x43258` at `0x444B0`. The dword at `0xAAF70+0x118` is `0x0000302E` (`2E 30 00 00`), so this body reader uses four bytes. The port keeps this body-reader flag wide.

`Map+0x118` is a separate address, `0x113AD0`; its direct references at `0x94FAC` and `0x69B4C` are loads in `Game_StartNew` and `Base_StripAllCraftCargo`. The offset alone was not enough to identify the FLOBJS gate.

## Port change

`World_InitTactical` now points `TactInitEnv.pnFlobjsWide` at `PufoWorldState.nTactVerOut`, which `World_LoadSlot` fills from TACTVER before the tactical-init hook. The unused zero-valued `nMapFlobjsWide` field is removed. `pnFlobjsWide` now uses `u32 *`, matching the existing TACTVER output type. `tests/test_tactinit.c` changes its fixture cell to `u32`.

For a TACTVER 1 save with 257 tactical items, the old world wiring read only the low count byte (`1`) for allocation. The later body reader read the full dword (`257`). The new wiring feeds the loaded TACTVER value to allocation. Counts above 255 now allocate the full pool. No original `.sav` sample was found, so this proves the code path, not a real-file load result.

## Regression fixture

[`test_save_world.c`](../../tests/test_save_world.c) starts a world and tactical mission, makes the live pool exactly 257 items, saves through `World_SaveSlot`, and hand-encodes the FLOBJS payload in the existing chunk. The raw count bytes are `01 01 00 00`; record 255 links to record 256. Record 256 carries `nIndex=0x1234`, `nAmmo=0xA5`, `bUnk7=0x5A`, and `bUnk8=0xC3`. It then reloads through `World_LoadSlot` and checks TACTVER 1, count 257, the marker fields, and the high-index link. The payload is written directly as little-endian fields; it does not use `Save_WriteFlobjs`.

CTest registers this as `test_save_world`, links `pufo_world`, and runs it from the source root so `main.dat` and `lang.dat` resolve. The existing all-executable OpenMPT runtime-stage target supplies its runtime files; this test adds no copy rule.

Before the wiring fix, `World_FreeTactical` clears the item count before `World_LoadSlot` calls `Tact_InitMission`. The zero `nMapFlobjsWide` therefore selects the one-byte allocation arm, which reads `01` and allocates one item. The later body reader reads the four-byte count 257; the high-index item is absent. This is the expected pre-fix failure.

The test is synthetic. It does not claim original-save compatibility. The first
executor build failed on two test-source naming errors, which were corrected.
The later Debug build was clean and all three requested CTests passed:
`test_save_world`, `test_save`, and `test_tactinit`. The successful world test
checked the exact 4373-byte FLOBJS payload, loaded TACTVER1/count257, tail marker
fields, and link255→256. Its private temporary save directory was removed by
test cleanup. See [batch6 evidence](../../.workers/luna-runtime-corrected-port/RESULT.md).
