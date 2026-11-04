# Status-panel slow button caption

User-requested text correction, 2026-10-29. The right footer button now shows
`Медл.` instead of the shipped Russian abbreviation `OKмдл`.

## Evidence and scope

- Original `Geoscape_Draw` at 0x8DFE0/0x8DFE4 loads offset 0x3BC22;
  0x8DFEC adds TEXTS base 0xB38D0, selecting record 1194 at 0xEF4F2.
  Instruction bytes are `EF 0B A0 E3`, `22 00 80 E3`, `00 20 86 E0`.
- The original archives contain `4F 4B EC E4 EB 00` (`OKмдл`, CP1251)
  in lang.dat record 1194 and `Slow` in the English main.dat record.
- The right handler, `Geoscape_PrepScreenOpen` at 0x85194, calls
  `GameTime_Set5Sec` at 0x85258 (`28 FF FF EB`) before removing the notice.
  The left OK handler keeps the current interval.
- The native footer replaces only that exact Russian caption with
  `CC E5 E4 EB 2E 00` (`Медл.`). Other text records, languages, archive bytes,
  control positions and click behavior are unchanged. This is an explicit
  user-requested display deviation from the shipped Russian data.
- The finding was added as a repeatable IDB comment at 0x8DFE0, read back,
  and saved. No names, types, prototypes or original instructions changed;
  their repair and debug-string naming checks are not applicable to this text fix.

## Check

Release `pocketufo` and `test_geoscape` build cleanly. The existing Geoscape
suite passes (1/1); the full suite was not repeated for this text-only change.
The saved transfer-arrival flow runs on a private desktop, displays the new
caption, and accepts the right button. The input desktop stays Default.

- [Build log](../../.workers/port-goal/slow-label-build.log)
- [Test log](../../.workers/slow-label-check/tests.log)
- [Arrival check](../../.workers/slow-label-check/arrival.log)
- [Checked screen image](../../.workers/slow-label-check/slow-label.png)

The already-running user game was kept open. Its executable was retained as
`build/src/pufo/Release/pocketufo-before-caption-fix.exe`; the rebuilt game is
at the usual `build/src/pufo/Release/pocketufo.exe` path for the next launch.
