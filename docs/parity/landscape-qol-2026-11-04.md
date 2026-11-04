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
