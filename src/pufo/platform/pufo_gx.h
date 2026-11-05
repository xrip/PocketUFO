/* pufo_gx.h - replacement for the WinCE GAPI (gx.dll) presentation layer.
 *
 * The original used GXOpenDisplay/GXBeginDraw/GXEndDraw on a raw framebuffer
 * (see GX_PresentFrame @ 0x9ea7c and InitInstance @ 0x9fd48). Win64 has no
 * GAPI, so this module owns an offscreen RGB565 DIB and presents it to the
 * window. GX display capabilities and per-mode pitch sign tricks have no
 * Win64 equivalent and are dropped; the 240x320 logical surface is kept.
 */
#ifndef PUFO_GX_H
#define PUFO_GX_H

#include "pufo_common.h"

#include <windows.h>

typedef struct GxSurface {
    u8      *pBits;    /* top-left pixel of logical surface */
    int      cbxPitch; /* per-pixel stride in bytes (signed, as in the original) */
    int      cbyPitch; /* per-row stride in bytes (LOGICAL surface row, 240*2) */
    u32      cxWidth;  /* logical surface size (always 240x320, QoL: see below) */
    u32      cyHeight;
    int      cBPP;
    /* QoL landscape presentation (shell extension, not in the original Win64
     * shell): the 240x320 portrait surface is shown UPRIGHT (never rotated)
     * in a wider screen, fitted to the window height and centered, with the
     * extra width as side bands. The game UI, laid out centered, stays
     * centered. The surface and the DIB stay 240x320. */
    int      bFitHeight;
    /* Win32 side */
    HWND      hWnd;
    HDC       hdcWindow;
    HBITMAP   hbm;
    HDC       hdcMem;
    void     *pOldObj;
} GxSurface;

/* Open the display and bind it to hWnd. Returns true on success.
 * bFitHeight selects the QoL landscape fit (see the struct comment). */
bool Gx_Open(GxSurface *pGx, HWND hWnd, u32 cxWidth, u32 cyHeight, int bFitHeight);

/* Pure viewport rect for a surface inside a client area; Gx_Present and
 * App_MapPointer share it so the pointer can never drift from the picture.
 * bFitHeight 0: integer scale, centered letterbox (the plain shell path).
 * bFitHeight 1: QoL landscape -- fill the client height, width =
 * nSurfW*nWinH/nSurfH (240x320 in a 320x240 client -> 180x240 at (70,0)),
 * centered. */
void Gx_ViewportRect(int nWinW, int nWinH, int nSurfW, int nSurfH, int bFitHeight,
                     int *pnX, int *pnY, int *pnW, int *pnH);

/* Pure inverse of Gx_ViewportRect: a client point to logical surface
 * coordinates, clamped to the surface rect. A side-band point lands on the
 * nearest edge pixel, where the game's own hit tests reject it. */
void Gx_PointerToLogical(int nVpX, int nVpY, int nVpW, int nVpH,
                         int nSurfW, int nSurfH, int nClientX, int nClientY,
                         i32 *pnX, i32 *pnY);

/* Copy the logical surface to the window, letterboxed at integer scale. */
bool Gx_Present(GxSurface *pGx);

/* QoL landscape wide view (see docs/parity/landscape-qol-2026-11-04.md):
 * rebuild the DIB at new surface dimensions. The window client is NOT touched:
 * in landscape mode the client is 320x240 and Gx_ViewportRect adapts the face
 * (wide surface 320x240 -> 1:1, portrait surface 240x320 -> 180x240 centered).
 * The DC pair survives; only the bitmap is recreated. */
bool Gx_Resize(GxSurface *pGx, u32 cxWidth, u32 cyHeight);

/* Pure view-geometry choice for one frame.
 * wide (landscape shell on a geoscape-map/tactical screen): surface 320x240,
 *   UI drawn at native size centered: UI_OFF_X = (320-240)/2 = 40,
 *   bottom-anchored UI (portrait y >= 160) shifts UI_OFF_Y = 240-320 = -80.
 * portrait (every other case): surface 240x320, offsets 0 -- exactly today's
 *   pillarbox contract. Scripted runs render under this same rule; the e2e
 *   gate and every committed dump run WITHOUT --landscape. */
void Gx_ViewGeometry(int bLandscape, int bWideScreen,
                     int *pnW, int *pnH, int *pnUiX, int *pnUiY);

/* GXSuspend @ 0xa61b4 and GXResume @ 0xa61b8, the display half of PocketUFO_WndProc's focus arms: 0xA04B4..0xA04C0
 * and 0xA051C..0xA0528 release the display on WM_KILLFOCUS, 0xA0578..0xA0584 and 0xA05D4..0xA05E0 take it back on
 * WM_SETFOCUS. GAPI hands the physical framebuffer to another application while the game is not focused; the Win64
 * seam owns its DIB and its window DC for the whole life of the process, so there is nothing to hand over and nothing
 * to take back. They are kept so the two arms read like the original and the pair has a counterpart in the parity map. */
void Gx_Suspend(GxSurface *pGx);
void Gx_Resume(GxSurface *pGx);

/* Release all display resources. Safe to call twice. */
void Gx_Close(GxSurface *pGx);

#endif /* PUFO_GX_H */
