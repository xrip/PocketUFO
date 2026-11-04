# Viewport guide and show-border option — 2026-10-25

## Native field

The byte at `Map + 0x150D6` (`0x128A8E`) is the persisted `showborder` option.
Raw instruction and data bytes establish its address and use:

- `Map_InitAndLoadRegistry` loads the Map base with `LDR R4,[PC,#0x64]` at
  `0x22724`; the literal at `0x22790` is `B8 39 11 00` (`0x1139B8`). It loads the
  registry key at `0x22A74`; the literal at `0x22B2C` is `4C 62 0A 00`, pointing
  to bytes at `0xA624C` that spell `showborder`. The conditional stores at
  `0x22AB8` and `0x22AC4` use an offset built by raw `MOV`/`ORR` bytes at
  `0x22AB0..0x22AB4` and `0x22ABC..0x22AC0`: `0x15000 | 0xD6 = 0x150D6`.
- `Options_SaveToRegistry` loads the same Map base from literal `0x116BC`
  (`B8 39 11 00`). It builds the same offset at `0x1163C/0x11640`, reads the
  byte at `0x11644`, then calls `Reg_SetString` at `0x11658` with the `showborder`
  key.
- `Options_SyncUiFromMap_GotoScreen12` loads the same Map base from literal
  `0x18A3C` (`B8 39 11 00`), builds the offset at `0x189C8/0x189CC`, and reads
  the byte at `0x189D0` to update the options control state.
- The option toggle at `0x192D4` reads the byte at `0x192F0` and writes it at
  `0x19338`; its Map-base literal at `0x19344` is `B8 39 11 00`.
- The viewport-guide draw routine loads the Map base from literal `0x1ADC4`
  (`B8 39 11 00`), reads the byte at `0x1AAFC`, and branches out at `0x1AB04`
  when it is zero.

The port's `PufoOptions.bShowBorder` is therefore the native field mapping for
this byte. The raw references are in registry load/save, the options toggle and
UI sync, and the viewport-guide draw gate. This byte is not an unwritten BSS
value.

## Port binding

The old tactical input sync left `TactInputState.bActionReady` at zero because it
was thought to have no native writer. The parent now reads `World_TactLiveOptions`
before drawing and sets the viewport guide's legacy `bActionReady` field from
`PufoOptions.bShowBorder`. `World_TactInputSyncIn` also copies the option into its
legacy field, and the field comments now identify it as the show-border option.
The existing tactical UI sync already copies `bShowBorder` between `PufoOptions`
and `TactUiState`.

The guide test comment now says that turning show-border off suppresses the guide.
The test's direct component setup remains a test of the draw gate; it does not
prove the live world option path by itself.

## Verification limit

This note verifies the native byte mapping from raw instructions and records the
port's new live binding. The actual rendered picture has not been checked after
the parent change. A full-frame run and screenshot are still needed before making
a visual parity claim.
