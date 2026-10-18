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
    int      cbyPitch; /* per-row stride in bytes */
    u32      cxWidth;
    u32      cyHeight;
    int      cBPP;
    /* Win32 side */
    HWND      hWnd;
    HDC       hdcWindow;
    HBITMAP   hbm;
    HDC       hdcMem;
    void     *pOldObj;
} GxSurface;

/* Open the display and bind it to hWnd. Returns true on success. */
bool Gx_Open(GxSurface *pGx, HWND hWnd, u32 cxWidth, u32 cyHeight);

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
