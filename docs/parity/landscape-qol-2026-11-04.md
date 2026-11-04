# QoL `--landscape`: 320x240 presentation (branch `landscape`, 2026-11-04)

Shell QoL mode, NOT original behavior: `pocketufo.exe --landscape` presents the
240x320 logical surface rotated 90 CW in a 320x240 window client. The logical
surface, the game core, the frame dumps and every test path are untouched, so
the parity gate stays exactly as it was. The centered UI stays centered: the
rotated surface fills the face 1:1 and the pointer center maps to the surface
center.

## IDB grounding

The original carries the same idea in its pointer table (0xA0768..0xA080C,
DisplayMode @ 0x187F48): mode 1 (320x240) swaps (y,x) at 0xA07D4; mode 3
(640x480) halves and rotates at 0xA077C (`RSB R0,R3,#0xF0` = 240 - y/2). The
Win64 shell had no path selecting those modes (recorded in docs/parity/bugs.tsv);
this QoL flag gives its own rotated path, not the mode table.

## Mapping (port)

Present: face(wx,wy) = surface(wy, 319-wx) — 90 CW, portrait top edge on the
face's right edge, the WinCE landscape convention.
Pointer: logical(lx,ly) = face(wy, 319-wx) inverse, lx = wy, ly = 319 - wx,
after the same integer-scale letterbox inverse the portrait shell uses
(Gx_PointerToLogical in pufo_gx.c).

## Deviations from the record

1. The original mode-1 pointer row is a pure (y,x) transpose — it fits the
   rotated device's raw touch axes. The Win64 mouse is in window space, so the
   port uses the true rotation inverse (no mirror). The transpose and the
   rotation differ by a horizontal mirror; the rotation is what a desktop user
   expects.
2. The flag itself is shell scaffolding like `--script`/`--new-game`, not a
   recreated feature; the original selects modes from the device, not a flag.
3. Gx in rotated mode owns a separate surface buffer (the DIB holds the face);
   the plain mode keeps the DIB-as-surface layout. No parity surface changes.

## Evidence

- test_appshell: 10 new checks pin corners, center and portrait identity of
  Gx_PointerToLogical.
- Manual window run (2026-11-04): 320x240 client screenshot shows the rotated
  centered menu; a real mouse click at face (195,120) — logical (120,124), the
  new-game row — advanced to the placement screen.
- Full ctest -C Release: 67/67 passed on this branch.
