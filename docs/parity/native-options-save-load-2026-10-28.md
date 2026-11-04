# Native Options Save/Load runtime evidence

Current result: the [focus repair and isolated-desktop run](win64-focus-timers-2026-10-28.md)
pass both post-save and post-load globe checks on Debug3CC. The earlier I/O pass
and investigation below retain their original image scope. The current driver
refuses input-desktop execution and has no foreground activation helper.

The native callback and file-I/O flow passes on Debug
54B358EC3EE89E8630250ED3B1EDA55B0B4BB5611C91FE83CC797C772E9E4B9C,
with PDB CF848C5395C81002C1586F2113685450421307DB72CC94F3125A3CD3F0BF7BF1.

Run: [options-row-save-load-20261028T080326Z-6638b724](../../.workers/luna-options-driver-fix/current-debug/options-row-save-load-20261028T080326Z-6638b724).
The owned HWND receives Title New Game, placement, base close, Options, Save
tab/row0, then Options and Load tab/row0. The installed callbacks and actual
OS create/write/read paths are observed. Save uses mode2/CREATE_ALWAYS2;
load uses mode1/OPEN_EXISTING3. Metadata lookup precedes the load draw and
payload read. The real App draw callback has the correct world owner.
The post-save globe frame and enabled Options control pass. App exit is0,
CDB exits and cleanup passes. All17 source files and23 private copies still
match the manifest after the run.

| Evidence in run | SHA-256 |
| --- | --- |
| OPTIONS-PROBE-RESULT.json | B4B788A4ED66A830A2359972E703CC33D663FA2CDF085A0D27E8BA03D5A05BEE |
| logs/options-row-save-load.cdb.log | B284B4FFC2C1733020976B8EF8EE28DD0A1E2B60072691C04E5A94ABE73472CC |
| logs/options-row-save-load.driver.log | 6315F03883AF00FEED24839B09AEC562815DA592F68CB48D759E21DC5294AD64 |
| app/sav0.sav,185994 bytes | 82C1B817756D579F07D9AA18645533397ACE14FA93E52D5836BA5F1D79C91C20 |
| provenance/INPUT-MANIFEST.json | 49646465358F95E9885EEA934F5BD71A1B0C094A0642F5A4ADE9F164621F9A67 |

## What was repaired in the private driver

- Static hook/hit objects now use captured numeric addresses. Type-only `dt`
  output is not treated as a null pointer or a missing game binding.
- Raw0x94964 creates the pending base;0x66B5C..0x66B64 increments its count.
  Placement therefore starts with one base, not zero. Raw0x8B5FC..0x8B608
  opens base screen1 and clears pending; the driver now closes it normally.
- CDB byte-group dashes are parsed and the dump requests decimal16 columns.
  Offline checks reject unreadable bytes and truncation.
- Save/Load return markers have a leading blank line to separate CDB warnings.
  A malformed warning/marker line fails instead of waiting forever.
- Save pointer return is an explicit stopped handshake. Frame-count baselines
  are reset there, excluding frames rendered before the save returned.
- An already verified posted close waits on the pinned process handle;
  it does not re-query an exiting PID before that wait.

Two failed driver attempts were ended by terminating their exact private CDB
process after confirmed protocol failures. Their logs and outputs remain.
Those runs do not count as normal-shutdown or gameplay passes.

## Earlier timer investigation

The stronger run [f9c41f25](../../.workers/luna-options-driver-fix/current-debug/options-row-save-load-20261028T080534Z-f9c41f25)
requires a post-load rendered globe and enabled Options control. It stopped
before reaching Load: no fresh post-save timer frame appeared after its
explicit resume. A checked mouse move reached the App; Windows foreground and
keyboard focus were also checked. Bringing the owned window forward did not
close the wait. This does not yet distinguish timer lifecycle, focus-event
ordering and debugger instrumentation. No production change was made for it.

The owner posted WM_CLOSE to end that diagnostic. Its result records cleanup
true and App status-1073740972 after failed-run debugger close; it is not a
normal-exit pass. Result hash:
2638463BDE15746C7B742965349DCBCDE6AC3066A5CDC3D307A3C7B59E823D9D.

Those focus/timer traces were run and exposed nested focus activation. The
repair and isolated native verification are recorded in the linked focus note.
The earlier I/O pass does not prove this stronger gate or all loaded-state
semantics; separate saved-game checks cover only their named fixtures.
