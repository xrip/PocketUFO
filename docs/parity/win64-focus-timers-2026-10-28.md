# Win64 focus loss and timer repair

Status: repaired and checked against native traces. Full port parity is still
incomplete. Desktop tests now require a separate non-input desktop.

## Fault and raw evidence

The original ARM focus-loss branches call SetWindowPos with HWND_NOTOPMOST
and flags0, then kill their timers. The raw instructions are:

| Raw range | Effect |
| --- | --- |
| 0xA0480..0xA04AC | Store fuFlags0, set R1=-2, call SetWindowPos |
| 0xA04B0 | Call the tactical timer kill |
| 0xA04E8..0xA0514 | Store fuFlags0, set R1=-2, call SetWindowPos |
| 0xA0518 | Call the geoscape timer kill |

These claims use instructions and operands, not IDA comments. On desktop
Windows, SetWindowPos without SWP_NOACTIVATE can activate the window.
[Microsoft's API contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)
defines the flag. The direct port therefore allowed this actual nested order:
FocusLost -> FocusGained -> Timer_StartD -> return to FocusLost -> Timer_KillD.
The App remained responsive to pointer input, but its render timer was gone.

The old [0c807bb9 trace](../../.workers/luna-options-driver-fix/current-debug/options-row-save-load-20261028T081538Z-0c807bb9/logs/options-row-save-load.cdb.log)
records the outer loss stack at0x15F8E8 and nested gain at0x15F4F8. The gain
starts timers50001..50004 before the outer loss kills those same IDs.
The first activation-wrapper hypothesis was rejected; that wrapper was not changed.

## Repair and checks

App_FocusReposition adds SWP_NOACTIVATE only for HWND_NOTOPMOST. This preserves
the loss-path geometry and z-order work without taking focus back. It is an
explicit Win64 adaptation from the raw flags0 call. Gain-path flags are unchanged.
App source SHA-256:
99D7057AB3A26A5372988432275920C4A99DA84684356F4E78B4E549F6D58B39.

The repaired [c5b2376c trace](../../.workers/luna-options-driver-fix/current-debug/options-row-save-load-20261028T082130Z-c5b2376c/logs/options-row-save-load.cdb.log)
records loss -> kill -> gain -> start. The same offline lifecycle checker
rejects the old trace and accepts this repaired trace. That run itself failed
on an immediate foreground-query race in the private driver; it is not counted
as the final native flow pass.

The final [41909cb7 native run](../../.workers/luna-options-driver-fix/current-debug/options-row-save-load-20261028T083517Z-41909cb7/OPTIONS-PROBE-RESULT.json)
passes New Game, placement, base close, Options Save/Load row0, actual OS file
I/O, the post-save globe and the post-load globe with enabled Options callback.
App exit0, debugger exit and cleanup pass. All17 source files and23 private
copies retain their recorded hashes. This checks the named flow; it does not
prove every loaded-state field, screen pixel or game subsystem.

| Artifact | SHA-256 |
| --- | --- |
| Debug EXE | 3CC597FE7CCA243A0788073072EFD06CEADC7D35BC9998339142BE4729CC7348 |
| Debug PDB | B9CAD3D0AB7F5F49A810BFD0056D11D4CFC0359848B8CFE035221E19F95D7BDC |
| Native result | B4623E348233DFFA02ABF4F4F698FBE5C87E3DA1E91CD936465D86260AF2C862 |
| Native CDB log | 222A3B97A771FB1A7AD3496398E4DB58CFBD4F8A5A5485541E2DE1E208208A70 |
| Native driver log | 5392DEC02700DBFAD18AAE159974592DA498586BC71FEE351C3A5EE9D4F31BE7 |
| Native save0 | 94EBC0363A433DF98AAC5CF6634AE5A7DE1A70E7624498A27DDC66DBBDED73C3 |

## Desktop isolation

Foreground activation was removed from the private driver after the user
reaffirmed the no-focus rule. The driver now refuses to run on the input
desktop. A smoke job confirmed this before any game launch.

The [launcher](../../.workers/luna-isolated-runtime/run.ps1) creates a unique
desktop without a desktop-switch right and sets the child's STARTUPINFO desktop.
It contains no desktop-switch call. The child checks its actual thread desktop
against the expected private name and the input desktop before running a job.
The whole runner, CDB and game process tree uses that private desktop.
No foreground activation helper is used.

The final native job used PufoTest_fae2b9288e90473c874d6b2ab2262277;
the input desktop was Default before and after. The
[child record](../../.workers/luna-isolated-runtime/runs/PufoTest_fae2b9288e90473c874d6b2ab2262277/child-result.json)
hash is2888EA59314B567C3C4A29FCA1184A20FE940FC4D38033DCC582951E0F72D80E;
the [parent record](../../.workers/luna-isolated-runtime/runs/PufoTest_fae2b9288e90473c874d6b2ab2262277/parent-result.json)
hash isDAC68282C0EBC82AE1450B4215C44087B4FF7D22B11ED586A0699F2445FD6C29.
This run needed no manual desktop input or activation.

## Full build and test gate

After the focus repair, both Debug and Release builds pass without compiler
warnings/errors and both full suites pass67/67. They ran serially on
PufoTest_89861a74f54b4d14bcdaa4816ee7d421; the input desktop stayed Default.
The [isolated gate record](../../.workers/luna-isolated-runtime/runs/PufoTest_89861a74f54b4d14bcdaa4816ee7d421/child-result.json)
links the exact build/test job. The logs are
[Debug build](../../.workers/luna-parent/focus-final-debug-build.log),
[Debug tests](../../.workers/luna-parent/focus-final-debug-tests.log),
[Release build](../../.workers/luna-parent/focus-final-release-build.log) and
[Release tests](../../.workers/luna-parent/focus-final-release-tests.log).
The Debug EXE/PDB hashes above remain exact. Release EXE SHA-256 is
9D54084825D15149B3025C10A8167BD4F5B0FF8F9DCE8AF2D85F739D5F7DB2F0.
The test catalog states each fixture's scope;67/67 is not a whole-game parity claim.
