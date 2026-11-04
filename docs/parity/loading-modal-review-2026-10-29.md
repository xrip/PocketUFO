# Startup loading panel and shared modal state

A normal start-menu slot click sets list mode3. For an existing save, LoadGame
renders before opening the file and resetting the mode. Raw Render_Frame reaches
MainMenu_Draw, which sets cursor40 and draws the one-line, no-button panel.
App previously passed NULL modal animation, so its renderer returned without
drawing that panel. Local tests supplied an animation and missed the App gap.

App now supplies a mode3 animation/layout view from the shared cursor, modal
progress/selection, phase and color owners, and publishes the changed fields.
The layout starts from live coordinates; no-button output preserves them and
clears the hit-area count.

MainMenu also synchronizes the forced40 cursor into the animation before drawing
and publishes the result afterward. Raw UiModal_DrawDialog wraps40 to1 and clears
progressive drawing before it writes the text. The old tests expected the cursor
copies to disagree (40 versus1) and progressive drawing to become1. Those
expectations were corrected using the raw stores, not by an observed test pass.

Evidence:

- [Original caller/mode chain](../../.workers/luna-start-menu-modal-connection/RESULT.md)
- [Parent cursor bytes](loading-modal-parent-raw-2026-10-29.json) and
  [layout stores](modal-layout-parent-raw-2026-10-29.json)
- [Post-fix owner/lifetime review](../../.workers/luna-start-menu-modal-connection/POST_FIX.md)
- [Regression purpose and limits](../../.workers/luna-loading-modal-regression/RESULT.md)

Both full Debug/Release builds returned exit0,68 executable outputs and zero
diagnostics with unchanged source; see the
[manifest](loading-modal-build-artifacts-2026-10-29.json). No tests, game, decoder,
audio/device, debugger or window execution ran.

The regressions verify supplied local menu/frame contexts, panel/text ink,
cursor1/progressive0 and layout output. They do not exercise private App_DrawFrame
through a native slot click; that acceptance remains open on an authorized host.
