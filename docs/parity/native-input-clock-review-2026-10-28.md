# Native input and clock routes: bounded source review

Checked 2026-10-28 by Luna xhigh from raw ARM and current source. No new
App/WndProc dispatch or clock-owner defect was confirmed. No executable,
native input, OS timer or audio device was run.

The input review follows mouse/key routing through startup, geoscape, tactical,
credits, debrief and end-screen gates. App and World screen copies can differ
in inactive modes: credits uses the startup owner, tactical dispatch syncs from
World, and geoscape input branches on the live geoscape flag before its screen
word. The review does not justify blindly forcing all copies to match.

Synchronous End Turn does not call UI sync-out that dereferences an absent Map.
Its pre-step damage/proximity paths enqueue later work rather than directly
calling kill/battle-end/shutdown. Actual kill/fall callbacks run under the
timer-depth bracket; immediate cancel paths use guarded input synchronization.
No freed-Map input continuation was established in these checked paths.

The clock review follows the stable window handle and timer bridge through
new/load setup, timer C353, UI pulse, live World time, pause/speed, hourly and
monthly callbacks. Original strategy pulse uses a20ms timer and reads the live
game-time step; tactical animation speed has its separate clock/timer path.
The port owners and call order match the inspected original path.

Static PE resource-directory inspection found no RT_ACCELERATOR(type9) in the
original image, despite its unconditional LoadAcceleratorsW(resource3) call.
No missing accelerator table was inferred from that API call. The port's
WM_COMMAND1 quit path remains separate from normal key dispatch. This used
file bytes only, without loading or executing the original image.
[Input hash and resource types](original-accelerator-resource-2026-10-28.json).

Existing tests exercise timer selectors and pulse layers separately. The shell
timer cases omit C353, and direct World/hourly calls do not test the complete
native WndProc-to-live-clock chain. Native acceptance still must exercise that
chain, real controls, screen transitions, actual elapsed time and campaign work
on the frozen build. Source wiring alone is not a runtime pass.

[Input/lifetime raw and source map](../../.workers/luna-native-input/RESULT.md).
[Timer/clock raw and source map](../../.workers/luna-native-clock/RESULT.md).
