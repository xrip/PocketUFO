# Slot-list live owner repair

The App formatter hook used scratch GameTime and game-state globals even when
invoked by the campaign Options tabs. Labels were correct, but side effects
landed on state that campaign readers did not use.

## Original contract

Raw SaveSlot_FormatList at0x45904 saves live total seconds from0x159618, reads
each existing slot TIME into that same clock and calls GameTime_Decode. It reads
the slot GAMESTATE into0x15821C. At0x45C10 it restores only total seconds; decoded
date fields and the game-state byte remain from the last inspected slot.
This original side effect is retained, including its temporary date change.
The repair does not replace it with a cleaner save-and-restore of the whole clock.

Parent checked the [entry/exit bytes](slot-list-clock-parent-raw-2026-10-29.json)
and [TIME/GAMESTATE reads and literals](slot-list-clock-read-parent-raw-2026-10-29.json).
The [driver audit](../../.workers/luna-body-save-drivers/RESULT.md) records the
complete three bodies and the active Options call path.

## Port repair

App_HookFormatSlots now selects g_world.geo.time and g_world.bGameState when
g_bWorldReady is set, with the existing scratch fields as the not-ready fallback.
It binds on each call, so the choice follows World initialization/shutdown.
World_Init zeros the owners before startup message processing; ready does not
mean that a new campaign must already have been started.

The [peer review](../../.workers/luna-slot-list-owner-peer/RESULT.md) confirms the
binding and exposes a separate cold-load Options initialization gap. Parent has
moved those Options hooks to shared setup. That additional repair now has a
compiled fresh-consumer regression in the
[cold-load review](cold-load-options-review-2026-10-29.md); it is newer than the
slot-owner-only manifest below.

## Verification and limits

Both Debug and Release default targets returned exit0, with68 executable outputs
and zero diagnostics on unchanged source; see the
[compiled manifest](slot-owner-build-artifacts-2026-10-29.json).
No game, tests, decoder, debugger or audio/device code ran.

TestSlotList already checks total restore and the decoded date/state left by the
last file, but supplies a local context. It cannot detect the App owner mistake.
Current public App tests do not install and invoke the private formatter hook.
No new test-only API was added; native Options/slot-list acceptance remains open.
