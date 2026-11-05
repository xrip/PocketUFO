# QoL `--landscape`: 320x240 screen, upright portrait viewport (branch `landscape`, 2026-11-04)

Shell QoL mode, NOT original behavior: `pocketufo.exe --landscape` puts the
game on a 320x240 screen while the 240x320 portrait viewport stays UPRIGHT (no
rotation, no mirror), scaled to the window height and centered. In a 320x240
client the viewport is 180x240 at x=70; the 70-px side bands are painted black.
The centered game UI stays centered on the screen. Resize the window and the
viewport grows with the height.

The logical surface, the game core, frame dumps and every scripted/test path
are untouched; the parity gate is unchanged.

## Mapping (port)

`Gx_ViewportRect(winW,winH,240,320,fitHeight)`: fill the client height,
`w = 240*h/320`, centered. Gx_Present and App_MapPointer call the SAME
function, so the pointer can never drift from the picture.
`Gx_PointerToLogical` inverts it (x = (cx-vpX)*240/vpW, y = (cy-vpY)*320/vpH)
and clamps to the surface rect: a side-band click lands on the nearest edge
pixel, where the game's own screen-17 hit tests (rows are x 50..190, the
0x13DBC/0x13E04 boxes) reject it and the release fires no row.

## Deviations from the record

1. The scale 0.75 is not an integer multiple. The original never scaled down
   (mode 2 halves, mode 3 halves+rotates at 0xA077C); a 240x320 portrait in a
   320x240 client has no integer fit, so QoL uses COLORONCOLOR downscale.
2. The black side bands have no original counterpart: a WinCE screen was
   exactly the surface. WndProc's WM_ERASEBKGND answer (1) stays, and present
   PatBlts the client black before the blit.
3. The flag is shell scaffolding like `--script`, not a recreated feature.
4. The original's own wide-screen modes (pointer table 0xA0768..0xA080C,
   DisplayMode @ 0x187F48: mode 1 swaps (y,x) @ 0xA07D4, mode 3 halves and
   rotates @ 0xA077C) are NOT what this mode implements; the QoL mode keeps
   the portrait axes unswapped. The mode table stays recorded in
   docs/parity/bugs.tsv as unselected.

## Evidence

- test_appshell: 13 checks pin the fit rect (180x240 at (70,0) in 320x240),
  center-to-center mapping, corners, band clamping to the edge, and portrait
  identity (240x320 at 1x maps to itself).
- Manual window runs (2026-11-04): 320x240 client shows the upright centered
  menu with black bands; a real click at client (160,93) = logical (120,124),
  the new-game row, advanced to the placement screen; a band click at
  (20,120) = edge (0,160) fires no row (before the clamp it opened a row --
  found by screenshot, fixed by clamping).
- Full ctest -C Release: 67/67 passed on this branch.

## Wide view P1-P3 (2026-11-05): geoscape and tactical fill 320x240

The user's follow-up: the two map windows (geoscape map incl. new-base
placement, tactical) render the live 320x240 surface; menus, dialogs, base
screens and every other UI screen keep the pillarboxed portrait surface.

- View geometry (Gx_ViewGeometry): wide = landscape && (geoscape screen 0/2
  || tact live); surface 320x240, UI_OFF_X=40, UI_OFF_Y=-80 for y>=160.
  Scripted runs render under the same rule WITHOUT the flag they stay
  240x320, so every dump, e2e reference and test is untouched.
- Surface/DIB are reallocated at the switch (SoftSurf_AllocPixels +
  Gx_Resize); present and pointer are data-driven through Gx_ViewportRect.
- Tactical: iso column bound 9 -> 11 (Map_InitFacingScratch takes the view
  width; portrait parity 9 pinned by test_tactctl); HUD pad origins, guide
  lines, status stack, multi-target bars, fade covers, edge tests and hit
  bands shift through the shared shim (ui/pufo_uigeom, identity default).
- Geoscape: the screen-centre pair (PUFO_RADAR_HALF_SCREEN_X/Y) is the live
  surface half-extent (portrait exactly 120/160), crosshair lines span the
  surface, base strip / globe labels / new-base centering shimmed.
- Deviations: wider iso grid and wider map view show more world than the
  original device view (view geometry is a QoL parameter, saves unaffected);
  scripted campaign feeds author portrait coordinates and route through the
  shim (App_ScriptPointerLogical).

Evidence: ctest -C Release 67/67 on every phase; wide scripted frames
build/shot/tact_wide.ppm (live mission, launch PASS) and geo_wide.ppm /
geo_map_wide.ppm (placement + map, crosshair centered).

## Wide UI acceptance fixes (2026-11-05)

User report: in landscape the geoscape speed/base menu and the tactical HUD
"are not shown and constantly disappear", and hovering made them appear.
Root causes found, all fixed (screenshot acceptance battery in build/shot:
acc_geo_map/acc_tact_inventory/acc_tact_deploy):

1. The gamemenu strip pad origin was written once in the wiring function, so
   the wide face kept the portrait (0,236) origin, clipped below 240; hover
   redrew it through the control shim, so it "appeared on hover". The origin
   is now written in World_RefreshGeoscapeFrame per frame (portrait identity).
2. The tactical HUD pad origin was shimmed from the pad's own live origin
   every frame, so x grew +40/px per frame and the HUD slid off-screen
   (geoscape strip origin write moved out of wiring). Fixed: the design
   origin is saved, the pad is shimmed for the blit, then restored.
3. Geoscape screen-to-tile math used the hard-coded device centre (120,160)
   in Smap_GetTileAtScreen, SmapHit_CellAt and the pick pen's sampler; the
   live centre (PUFO_RADAR_HALF_SCREEN_*) is used now (identity portrait,
   original constants in no-surface test envs). Smap_ClampScrollY follows the
   live height. This is what made the scripted pick fail on the wide face.
4. The geoscape pointer strip-band gate used the raw SURF_H - uHeight; it now
   goes through the shim (portrait identity: the original gate exactly).
5. Geometry excludes the portrait dialogs that keep the map/battle screen id:
   geoscape radar (bRadarScreenActive) and options (bOptionsScreenOpen), tact
   inventory (screen 1), options (12) and promotion report (13). The radar
   dialog on a base with no radar facility is a no-op (original gate).
6. Status panel text (StatusPanel_DrawAt) and the geoscape clock pads now
   shift through the shim; identity portrait.

Shell flag `--nosound` added (no audio backend install) for graphics QA runs.
Evidence: ctest -C Release 67/67; e2e pictures 11/11 (only the pre-existing
stale 00_mainmenu ref); scripted acceptance flows: campaign launch/deploy PASS
in landscape; new-base pick converges on the wide face; nav clicks open the
options dialog, which switches the face to portrait.


## Tactical and report windows on the wide face (2026-11-05)

User pictures: mid-battle with the HUD clipped to bottom tabs, the tact context
menu, the deploy panel, and the geoscape НЛО contact card all rendered raw
portrait geometry on the 320x240 face. Root cause: those states are portrait
design windows (their origins are written at y 228..303), not map views, so the
geometry gate now keeps them on the portrait face, and the report panels that
stay on the map face shift through the shim.

1. Gate additions in App_UpdateViewGeometry: tact minimap window (screen 6),
   bMapPromotionReport, sTactInput.bDeployPanelOpen and sModal.bOpen (both
   arms). Verified with the user's mid-mission save (sav0.sav, deploy-panel
   state): scripted dump is 240x320, window run shows the pillarboxed portrait
   battle view with intact HUD; state probe prints `deploy 1`.
2. Script `state` probe prints modal/deploy/radar/opts flags.
3. Geoscape status panel boxes (the report card frames: descent/contact/order/
   target/detail/pause/city/list/alien-base arms, StatusPanel_Anim,
   ActionFail): every Ui_AnimExpandBlit box origin shifts MapX/MapY; the card
   box, title and stacked controls are now centred on the wide face.
4. Globe hover (new-base placement screen 2, the user's "start geoscape is
   broken"): the wash covers the live surface dims, the crosshair rules and
   tick marks center through the shim, and the bottom panel box, scan washes,
   scan bar and sweep lines shift MapX/MapY. Placement verified in a visible
   window: full-face globe, centred crosshair, centred panel and readouts.
5. Geoscape refresh writes the bpanel pad (GEOUI_PAD_187048, design y 228)
   through the shim per frame, next to the strip pad.

Portrait stays identity (offX 0, offY 0): e2e refs byte-identical.
Evidence: ctest -C Release 67/67; e2e pictures 11/11 (only the pre-existing
stale 00_mainmenu ref); QA-desktop window shots: new-game placement full-face,
ocean confirm refusal centred, user save deploy state pillarboxed portrait.
Separate pre-existing bug, NOT touched: `close` on a loaded user save
SEGFAULTs in Geoscape_CloseUI -> GameTime_Set5Sec (NULL speed cell);
reproduces on portrait at master.

## Geoscape and tactical fill the whole window (2026-11-05, supersedes the
## portrait-face exclusions of the section above)

User requirement: the geoscape and tactical screens occupy the ENTIRE window
in every state that shows the map. The gate now drops the face to portrait
only for the menu screens (geoscape options dialog, tact inventory 1, tact
options 12, promotion report 13). The radar window, deploy panel, hand menu,
minimap overview (6) and modal dialogs stay on the wide face, centred.

1. Shared modal dialog (UiModal_DrawDialog): the box centres on the live
   face ((faceH - h) / 2 and MapX of the portrait nX), exact portrait, exact
   parity formula; the modal layout cells and button hit boxes follow.
2. UiPanel_DrawText shifts its rect, rules, hover band and rows through the
   shim (identity portrait): every panel over a wide map lands centred.
3. TactHud: pad label helpers, status line, confirm dialog (backdrop, title,
   side labels), status banner, unit roster rows, unit markers + overlay pad,
   top bar rows and the HUD item strip (icons, counts, empty-hand pad) all
   shift MapX/MapY; a shared PadImage_BlitShimmed helper blits fixed-origin
   backdrop pads centred.
4. Viewport guide: the four edge blits the P2 pass left raw now shift.
5. Minimap overview (screen 6): glyphs, label and the mini-cursor pad centre
   in x (the window is top-anchored, y stays).
6. Geoscape radar window: panel frame blits, rails, signal/cursor bars,
   labels, wash, button pads and the fuel gauge shift MapX/MapY.

Evidence: ctest -C Release 67/67 (the modal centering keeps the exact
(259/2 = 148)-style truncation, so the existing modal probes pass untouched); e2e pictures
11/11 (only the known stale 00_mainmenu ref); scripted wide dumps on the
user's sav0.sav: deploy-panel state 320x240 with the deploy bar bottom-
anchored, battlefield after closing the panel shows the full 117-row HUD
centred; the НЛО contact card and the new-base placement stay centred as in
the section above. Radar window parity is by construction (no radar facility
in the QA map; identity portrait in the unit tests).

Tactical right-edge void (2026-11-05): the iso column count (Map+0x34) is
written once at mission init and restored from saves, so a landscape load
kept 9 columns: the 240-wide view ended at x 272, leaving a 48-pixel void
where edge glyphs (units, labels, the multi-target bars at 207+40) drew over
nothing. World_RefreshTactFrame now recomputes the count from the live face
width ((faceW + 16) / 32 + 1: 240 -> 9 parity, 320 -> 11), so the 11
32-px columns cover -16..336 and the map fills the window. nViewW 0
(non-landscape, tests) keeps the parity cell; ctest 67/67, e2e 11/11.

Tact main menu raised (2026-11-05): the seven-row in-battle menu (widgets
53..59) is laid out by OptionsScreen_LayoutButtons/Options_BindSlotRows at
90 + 25 * i (and 40 + 22 * i); the bottom-anchor shim split that stack and
left it on the wide HUD. Options_CentreRow now drops each row at
PufoUi_MapCentreY(design y) = y + offY / 2: the whole window rides at the
face centre (rows 50+25*i wide, identity portrait), computed from the
stable design base so re-running the layout is idempotent, and the pad hit
boxes follow the drawn rows (the first drawer-side attempt at 60+23*i
clobbered the live layout and desynced the last row's hover -- reverted).
test_baseui pins 90+25*i portrait and 50+25*i wide. The tail rows 4..6 (descriptor 152 + 23 * i) are centre-shifted by the drawer from the same stable base (112..158 wide): one shift rule per writer, so labels, pads and hit boxes agree and every row hovers. test_tacthud pins both. ctest 67/67.
