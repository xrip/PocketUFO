# New Game shortcuts use the startup-menu transition

Checked 2026-10-28 by Luna xhigh and parent raw capture. No executable or audio
device was run. Native playback and UI acceptance remain open.

The normal original New Game action plays its confirm sample at0x1A0A8,
calls StartUi_Shutdown at0x1A0AC, then enters Geoscape_InitUI at0x1A0B8.
Startup shutdown releases audio at0x38D80 and kills its timers at0x38D84.
The geoscape then loads its own sample bank and track2. This is an explicit
lifecycle edge, not just a call to the world new-game hook.

The port's optional --new-game branch and script newgame command called
App_HookNewGame directly. When startup UI was live, that left its audio bank,
track, controls and timer cleanup outside the transition before world setup.

Both shortcuts now call App_StartNewGameFromShortcut. If startup UI is live,
it invokes the existing MenuCtl0_OnClick action, including confirm, shutdown
and new-game order. If startup UI is already closed, it uses the existing
new-game hook directly. The normal menu callback is unchanged. A shared
screen-state publication already present in App_ShutdownStartUi was preserved;
it was not introduced by this worker's shortcut patch.

The broader bounded audio review found shared live options ownership from
registry through App, World, controls and backend mute pointers. Original
menu/geoscape/tactical/title/end-screen setup and cleanup calls are paired
in the inspected paths. Focus pause/resume and mute/volume source contracts
were traced; no audible or device-level equivalence is inferred for WinMM.

[Original transition and backed-up note](startup-newgame-lifecycle-raw-2026-10-28.json).
[Luna's detailed scope and source map](../../.workers/luna-audio-control/RESULT.md).
Required host checks include both startup shortcuts, normal menu New Game,
track changes, mute/volume persistence, focus pause/resume and clean exit.

Both full incremental default builds completed with exit0,68 executable outputs
and zero compiler diagnostics. [Current source/image identities](lifecycle-width-build-artifacts-2026-10-28.json).
No startup shortcut or native audio case was executed. The parent backed up
the IDB note at0x1A0AC, wrote the supported lifecycle finding, checked readback
and saved with backup.
