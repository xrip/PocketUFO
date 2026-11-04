# Control pad ownership repair

Updated: 2026-10-28. Source/raw cleanup contract checked; runtime pending.
No game, test or debugger executable was launched.

## Confirmed defect

UiControl_LoadPads allocates each PadImage record with malloc and constructs it.
Its reload path already frees both data and records. UiControl_FreePads instead
freed only the data and cleared each slot, permanently losing the record pointer.
The same destructor is used by bank exit, World tactical cleanup and screen closes.

This was not a required borrowed-object adaptation. The current header describes
owned pad records, production direct assignments come from the allocating loader
or clear slots, and the other owning screen shutdown paths already free records.
Drawing tests can borrow pads while drawing, but must not pass borrowed storage
to an owning destructor.

## Raw evidence, independent of comments

The function 0x1A2C8..0x1A340 reads the three pad pointers at original control
offsets 0x14,0x18,0x1C, each behind a null test. For each non-null pointer it calls
the data-release function, restores that same pointer into R0, and calls 0xA115C:

| Slot | Data-release call | Restore pointer | Delete call |
| --- | --- | --- | --- |
| 0 | 0x1A2E8 (47dbffeb) | 0x1A2EC (0400a0e1) | 0x1A2F0 (991b02eb) |
| 1 | 0x1A30C (3edbffeb) | 0x1A310 (0400a0e1) | 0x1A314 (901b02eb) |
| 2 | 0x1A330 (35dbffeb) | 0x1A334 (0400a0e1) | 0x1A338 (871b02eb) |

Raw 0xA115C (00c09fe5) loads the import-slot address into R12 and
0xA1160 (00f09ce5) loads PC from that slot. The literal at 0xA1164 is
0xA615C. The IDB import table identifies slot 0xA615C as
`__imp_??3@YAXPAX@Z`, ordinal1094: operator delete. This confirmation uses
instructions and import metadata, not the function's manually assigned label.

The repaired C loop frees the PadImage record after PadImage_FreeData.
Documented adaptations retained: null control is tolerated and freed slots are
cleared. The original leaves dangling slots. Record deletion itself is restored.

## Test corrections and purpose

- test_control's bank sweep now supplies heap records to the owning destructor.
  It continues to require that all130 bank slots are visited and cleared.
- test_control's leaf release case allocates three records and three pixel
  buffers. In Debug, CRT heap checkpoints require that release returns to its
  allocation baseline. Release still checks the cleared slots. The test no
  longer reads the deleted records or frees them a second time.
- test_uibank no longer reads deleted pixel pointers or manually frees records
  after Bank_AtexitThunk. It also avoids allocating a span-zero pad and then
  overwriting it with a different plant. Span and endpoint checks remain.
- test_baseui's research rebuild case supplies owned heap pads because the
  production rebuild releases them. Drawing-only static-pad fixtures remain.
- A source call scan checked direct destruction paths and flagged indirect
  borrowed fixtures for review. This is supporting review, not proof that all
  dynamically selected callback paths are safe.

The new heap assertion is not executed. Its purpose is to distinguish releasing
only pixel buffers from releasing both allocations; no mutation/red-green result
is claimed. Current full-suite runtime verification remains required because
ownership changes may expose other incorrect fixture or caller assumptions.

## Compile evidence and current candidate

Compiled both configurations with targets pocketufo, test_control, test_uibank,
test_baseui, test_geoscape, test_geoscapeframe and test_appshell. Both commands
returned exit0 and zero compiler diagnostics. Logs:
[Debug](../../.workers/luna-mission-init-audit/debug-control-ownership-compile.log),
[Release](../../.workers/luna-mission-init-audit/release-control-ownership-compile.log).

434-file source-tree fingerprint using the algorithm in the
[earlier candidate](candidate-2026-10-28-bindings.md):
**61090D2FAF1557D722810BD6EAB5E4746C9FECE47B490AE51F84000F62973130**.

| Artifact | SHA-256 |
| --- | --- |
| src/pufo/ui/pufo_control.c | 6C09B20B061170DDE75796B95A95343E54FBCF93E428BB75C55B902A2E2AF0C2 |
| tests/test_control.c | 40741CCBDB44F25C350073FFB558FD61981912F4910168FD81B6E8A16DE15EC3 |
| tests/test_uibank.c | 4A6D2810EDDA81289E674E64CFDC65E6E130EEB3C97E6772E9D37A06F189F3CD |
| tests/test_baseui.c | 58EB1E2C703B1AAD4AF6887E7632508DF5A5526C68C4E5106E00D9446F6A3686 |
| build/src/pufo/Debug/pocketufo.exe | D3A1E85FE537110D2D867DCA4E44FA0F2F9AD0A5F34986C57A3F1EA5D6F6FD8E |
| build/src/pufo/Release/pocketufo.exe | EC101C4F843E78DF5387FBBA279F77BB302E7D9615FF1F04ADAF4CF3A84A1C62 |
| build/tests/Debug/test_control.exe | 523162263710715076421B923E7B01D25FBCC93B27A0B976E7C3CEB34F65BA4B |
| build/tests/Release/test_control.exe | 493D102971206D5DF637B44E181DFC69BA7A447F79A77A56AA4681203D60EB0F |

This supersedes the startup-once candidate for runtime acceptance. Original
data files are unchanged. Six ledger rows had their source locations/status
refreshed. The source stabilization gate can close for this bounded repair;
gameplay, memory behavior and full parity remain unproven.
