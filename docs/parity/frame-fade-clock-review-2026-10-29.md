# Frame fade and clock-owner corrections

Both full Debug/Release builds compile with zero diagnostics; assertions and
native flows remain unrun. See the
[source/image manifest](frame-clock-build-artifacts-2026-10-29.json).

## Live banner-fade fault

Raw Render_Frame_DecRef at0x36818 decrements Map+0x64 before entering Render_Frame.
The banner gate at0x1E930 and outer fade read at0x22504 therefore see the new
value. World previously copied that owner into two stack views, then decremented
the separate live counter. At1->0 it still skipped the banner and darkened that
frame using the old value1.

World now decrements before preparing either view and uses the ordinary frame
call. The standalone helper refreshes both frame and banner copies after
decrement. Its local banner copy remains alive through the render call. A direct
frame call also reads the cursor countdown after its tick; World already ticked
before snapshot construction, so that latter correction is not a new live World
fault and does not add a second tick.

[Full four-body raw evidence](frame-present-raw-2026-10-29.json),
[cursor update bytes](cursor-frame-parent-raw-2026-10-29.json), the
[peer proof](../../.workers/luna-frame-fade-peer/RESULT.md) and
[post-fix review](../../.workers/luna-frame-fade-peer/POST_FIX.md) record the order.

The [regressions](../../.workers/luna-frame-fade-regression/RESULT.md) use separate
owner/frame storage, a screen13 banner copy and a direct cursor9->10 transition.
The old test aliased the counter to the environment field, hiding the production
snapshot problem. Parent also removed an expired cursor pointer from the old
NULL-counter case and made the partial-alpha fixture use a consistent countdown.
These are local software-render tests, not an executed World/native scene.

## Pause helper/model correction

The old named GameTime_Pause helper took a GameClock speed object, and its test
paused that unrelated object. Actual production pause writes were already on
the correct strategic GameTime owner. No new live pause failure is claimed.

GameTime_Pause now belongs beside Unpause in pufo_clock.c/.h and takes GameTime.
The checked ship tick calls it instead of its equivalent inline write. The
unused false pause field was removed from GameClock; speed use is unchanged.
TestPause now checks a real strategic clock and preserves its total, hourly
stamp and decoded date. The stale tactical-init fixture seed had no reader or
assertion and was removed after the compiler identified it.

Raw loads0x159618 and writes byte+4; GameTime begins0x159608, so the named pause
field is+20 at0x15961C. The IDB already has no-argument int-pointer prototypes for
both original helpers; no IDB edit was needed. See
[parent raw check](game-time-pause-parent-raw-2026-10-29.json) and
[clock post-fix review](../../.workers/luna-body-geoscape-clock/POST_FIX.md).
Negative manual delta and extreme wrap limits remain outside the clock audit's
positive production-step domain; no clock arithmetic was changed.

## Other rendering boundaries

The frame's scene/fade dispatch and GX thunk/presentation bodies have scoped
[member records](frame-present-result-2026-10-29.json). Persistent RGB565 DIB and
letterbox presentation replace original GAPI acquisition/rotation modes. Native
pixel/device output and all return-value consumers are not proved by this pass.
The frontend FlipEx mask parameter is passed directly at native pointer width;
closed consumer checks show no stale mask or alpha read. Inner raster loops
remain a separate audit, not a claimed pixel-output pass.
