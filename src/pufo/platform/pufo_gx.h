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
     * shell): the logical 240x320 surface is rotated 90 CW into a 320x240
     * DIB at present time, so the game UI (laid out centered) keeps its
     * center on a landscape screen. The surface itself never rotates. */
    int      bRotate90CW;
    int      nDibW;    /* DIB/window-face size: 320x240 rotated, 240x320 plain */
    int      nDibH;
    u8      *pFace;    /* rotated 90 CW copy of the surface, the DIB bits;
                        * NULL in plain mode, where the DIB bits ARE pBits */
    int      bOwnsSurface; /* 1 in rotated mode: pBits is a Gx-owned buffer */
    /* Win32 side */
    HWND      hWnd;
    HDC       hdcWindow;
    HBITMAP   hbm;
    HDC       hdcMem;
    void     *pOldObj;
} GxSurface;

/* Open the display and bind it to hWnd. Returns true on success.
 * bRotate90CW selects the QoL landscape face (see the struct comment). */
bool Gx_Open(GxSurface *pGx, HWND hWnd, u32 cxWidth, u32 cyHeight, int bRotate90CW);

/* Pure mapping of a point in the letterboxed face space (0..nDibW-1,
 * 0..nDibH-1 after scale/offset removal) into logical surface coordinates.
 * Plain face: identity. Rotated face (90 CW, top of the portrait on the right
 * edge): lx = sy, ly = cyHeight-1-sx. The pointer table the ORIGINAL used in
 * its mode 1 (0xA07D4) swaps (y,x) -- a transpose, matching the rotated
 * device's raw touch axes; a Win64 mouse is in window space, so the port uses
 * the true rotation inverse. Recorded in docs/parity/landscape-qol-2026-11-04.md. */
void Gx_PointerToLogical(int bRotate90CW, u32 cyLogical, int nSx, int nSy,
                         i32 *pnX, i32 *pnY);

/* Copy the logical surface to the window, letterboxed at integer scale. */
bool Gx_Present(GxSurface *pGx);

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
