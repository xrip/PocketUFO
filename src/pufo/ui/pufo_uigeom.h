/* pufo_uigeom.h - QoL wide-view UI geometry shim (shell extension).
 *
 * Landscape wide screens (320x240) show native-size UI centered: controls keep
 * their original portrait coordinates and are shifted at the shared draw/hit
 * points: x + offX for everything; y + offY only for bottom-anchored UI
 * (portrait y >= anchor). Defaults are the identity: portrait runs, hidden
 * scripted runs, tests and the whole parity surface are untouched until the
 * shell sets a geometry. See docs/parity/landscape-qol-2026-11-04.md.
 */
#ifndef PUFO_UIGEOM_H
#define PUFO_UIGEOM_H

#include "pufo_common.h"

/* The bottom-anchor line: UI at/below this portrait y sits on the bottom
 * screen edge and shifts with the surface height. */
#define PUFO_UI_BOTTOM_ANCHOR_Y 160

/* Portrait runs never call it: the statics start at 0, the identity. */
void PufoUi_SetGeometry(int nOffX, int nOffY);

int  PufoUi_MapX(int nX); /* nX + offX */
int  PufoUi_MapY(int nY); /* nY >= anchor ? nY + offY : nY */
int  PufoUi_MapCentreY(int nY); /* whole-window centre shift: nY + offY / 2 */
int  PufoUi_OffY(void); /* the raw y offset, for sites that store a compensated y */

#endif /* PUFO_UIGEOM_H */
