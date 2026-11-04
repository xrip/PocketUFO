# Keyed control pads: live App callback

Status: source repair reviewed; both full builds pass. No game or test ran.

Raw UiControl_BlitActivePad branches on control byte+0x11, then sets R1=0
at0x1A3C4 before PadImage_BlitEx at0x1A3C8. The
[instruction capture](control-pad-key-raw-2026-10-29.json) records that fixed
transparency index. The live App callback instead passed255.

App_BlitPadEx now passes0. Its only named bindings are the pointer/UI hit
context and main-menu control renderer, both control paths. Direct generic Ex
calls retain their own keys. Tactical decimal pads already use the separate
World_TactDecimalBlit key0 route and required no change. A live keyed tactical
control is created by the raw byte+0x11 write at0x3B8A4.

The [control audit](../../.workers/luna-body-uicontrol/RESULT.md) checks all seven
bodies. Its [post-fix note](../../.workers/luna-body-uicontrol/POST_FIX.md) records
the test boundary: test_control injects fake blitters, test_appshell drives input,
and App_InitializeWorld does not install the static App render callbacks.
The C351 timer can enter App_DrawFrame, but only full App_Run startup installs
this callback. No new test-only API was introduced to manufacture a passing test.

Both full Debug/Release builds returned exit0 with68 executable outputs and
zero diagnostics. The [manifest](control-key-build-artifacts-2026-10-29.json)
records equal source fingerprints before/after and source/data/image identity.
Visible control pixels through native startup remain an acceptance check on an
authorized test host. No current visual result is claimed.

The inaccurate LoadPads return comment now distinguishes empty third-name
strcmp0 from nonempty PadImage_Load output; checked callers discard the result.
The stale large-unit test comment was also corrected without changing assertions.

The call-site note was backed up with the raw capture, guarded, read back and
saved: [IDB readback](control-pad-key-idb-after-2026-10-29.json). No binary byte
or original function type changed for this repair.
