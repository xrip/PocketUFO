/* pufo_gx.c - Win64 replacement for the WinCE GAPI (gx.dll) display layer.
 *
 * Evidence: InitInstance @ 0x9fd48 (GXOpenDisplay/GXOpenInput, mode resolve)
 *           GX_PresentFrame @ 0x9ea7c (GXBeginDraw/GXEndDraw, pitch handling)
 *
 * The original draws a 240x320 16bpp surface and blits it into the GAPI
 * framebuffer. Here the surface is a DIB section and the blit target is the
 * window DC. GAPI-only concerns (raw framebuffer escape 0x20001, per-mode
 * pitch sign flips, GXKeyList) have no Win64 equivalent and are not recreated.
 */
#include "platform/pufo_gx.h"

#include <string.h>

static bool Gx_MakeDib(GxSurface *pGx)
{
    BITMAPV4HEADER hdr;
    void *pBits = NULL;

    memset(&hdr, 0, sizeof(hdr));
    hdr.bV4Size          = sizeof(hdr);
    hdr.bV4Width         = (LONG)pGx->cxWidth;
    hdr.bV4Height        = -(LONG)pGx->cyHeight; /* top-down, matches GAPI */
    hdr.bV4Planes        = 1;
    hdr.bV4BitCount      = (WORD)(pGx->cBPP * 8);
    hdr.bV4V4Compression = BI_BITFIELDS;
    hdr.bV4SizeImage     = 0;
    /* Explicit masks: the original surface is RGB565, not the 5-5-5 default
     * that 16bpp BI_RGB would give. */
    hdr.bV4RedMask   = 0xF800u;
    hdr.bV4GreenMask = 0x07E0u;
    hdr.bV4BlueMask  = 0x001Fu;

    pGx->hbm = CreateDIBSection(pGx->hdcWindow, (BITMAPINFO *)&hdr,
                                DIB_RGB_COLORS, &pBits, NULL, 0);
    if (pGx->hbm == NULL || pBits == NULL) {
        return false;
    }

    pGx->pOldObj = SelectObject(pGx->hdcMem, pGx->hbm);
    pGx->pBits   = (u8 *)pBits; /* the game renders straight into the DIB in every mode */

    /* The original stores pitch as signed and relies on it being positive for
     * the native 240x320 mode. A top-down DIB gives the same layout. */
    pGx->cbxPitch = pGx->cBPP;
    pGx->cbyPitch = (int)(pGx->cxWidth * (u32)pGx->cBPP);
    return true;
}

bool Gx_Open(GxSurface *pGx, HWND hWnd, u32 cxWidth, u32 cyHeight, int bFitHeight)
{
    if (pGx == NULL || hWnd == NULL || cxWidth == 0 || cyHeight == 0) {
        return false;
    }
    if (pGx->hWnd != NULL) {
        return false; /* already open */
    }

    memset(pGx, 0, sizeof(*pGx));
    pGx->hWnd       = hWnd;
    pGx->cxWidth    = cxWidth;
    pGx->cyHeight   = cyHeight;
    pGx->cBPP       = PUFO_SURF_BPP;
    pGx->bFitHeight = bFitHeight ? 1 : 0;

    pGx->hdcWindow = GetDC(hWnd);
    if (pGx->hdcWindow == NULL) {
        return false;
    }
    pGx->hdcMem = CreateCompatibleDC(pGx->hdcWindow);
    if (pGx->hdcMem == NULL) {
        Gx_Close(pGx);
        return false;
    }
    if (!Gx_MakeDib(pGx)) {
        Gx_Close(pGx);
        return false;
    }
    return true;
}

void Gx_ViewportRect(int nWinW, int nWinH, int nSurfW, int nSurfH, int bFitHeight,
                     int *pnX, int *pnY, int *pnW, int *pnH)
{
    int nScale;

    if (bFitHeight) {
        *pnH = nWinH;
        *pnW = nSurfW * nWinH / nSurfH;
        *pnX = (nWinW - *pnW) / 2;
        *pnY = 0;
        return;
    }
    nScale = nWinW / nSurfW;
    if (nWinH / nSurfH < nScale) {
        nScale = nWinH / nSurfH;
    }
    if (nScale < 1) {
        nScale = 1;
    }
    *pnW = nSurfW * nScale;
    *pnH = nSurfH * nScale;
    *pnX = (nWinW - *pnW) / 2;
    *pnY = (nWinH - *pnH) / 2;
}

void Gx_PointerToLogical(int nVpX, int nVpY, int nVpW, int nVpH,
                         int nSurfW, int nSurfH, int nClientX, int nClientY,
                         i32 *pnX, i32 *pnY)
{
    *pnX = (i32)(nClientX - nVpX) * nSurfW / nVpW;
    *pnY = (i32)(nClientY - nVpY) * nSurfH / nVpH;
    /* A QoL side-band click becomes a click on the nearest surface edge:
     * the game's own hit tests (screen-17 rows are x 50..190) reject edge
     * points, and the release fires no row. No negative coordinates ever
     * reach the original's pointer arms. */
    if (*pnX < 0) {
        *pnX = 0;
    } else if (*pnX >= nSurfW) {
        *pnX = nSurfW - 1;
    }
    if (*pnY < 0) {
        *pnY = 0;
    } else if (*pnY >= nSurfH) {
        *pnY = nSurfH - 1;
    }
}

static void Gx_FreeBack(GxSurface *pGx)
{
    if (pGx->hdcBack != NULL) {
        if (pGx->pOldBack != NULL) {
            (void)SelectObject(pGx->hdcBack, pGx->pOldBack);
            pGx->pOldBack = NULL;
        }
        (void)DeleteDC(pGx->hdcBack);
        pGx->hdcBack = NULL;
    }
    if (pGx->hbmBack != NULL) {
        (void)DeleteObject(pGx->hbmBack);
        pGx->hbmBack = NULL;
    }
    pGx->nBackW = 0;
    pGx->nBackH = 0;
}

bool Gx_Present(GxSurface *pGx)
{
    RECT rcClient;
    int  nWinW;
    int  nWinH;
    int  nDstW;
    int  nDstH;
    int  nX;
    int  nY;
    HDC  hdcDst;

    if (pGx == NULL || pGx->hWnd == NULL || pGx->pBits == NULL) {
        return false;
    }
    if (!GetClientRect(pGx->hWnd, &rcClient)) {
        return false;
    }

    nWinW = rcClient.right - rcClient.left;
    nWinH = rcClient.bottom - rcClient.top;
    Gx_ViewportRect(nWinW, nWinH, (int)pGx->cxWidth, (int)pGx->cyHeight,
                    pGx->bFitHeight, &nX, &nY, &nDstW, &nDstH);

    /* The frame is composed in a client-sized offscreen bitmap and moved with
     * one BitBlt, so the screen only ever sees whole frames. The bands around
     * the viewport stay black (the original's framebuffer had no bands;
     * WndProc answers WM_ERASEBKGND, so nothing else paints them). Only the
     * bands are cleared: a whole-window BLACKNESS made every present show a
     * full black window between the clear and the blit -- visible flicker on
     * scaled windows, measured as fully black captured frames (2026-11-05). */
    if (pGx->nBackW != nWinW || pGx->nBackH != nWinH) {
        Gx_FreeBack(pGx);
        pGx->hdcBack = CreateCompatibleDC(pGx->hdcWindow);
        if (pGx->hdcBack != NULL) {
            pGx->hbmBack = CreateCompatibleBitmap(pGx->hdcWindow, nWinW, nWinH);
            if (pGx->hbmBack != NULL) {
                pGx->pOldBack = SelectObject(pGx->hdcBack, pGx->hbmBack);
                pGx->nBackW   = nWinW;
                pGx->nBackH   = nWinH;
            } else {
                (void)DeleteDC(pGx->hdcBack);
                pGx->hdcBack = NULL;
            }
        }
    }
    hdcDst = (pGx->hdcBack != NULL) ? pGx->hdcBack : pGx->hdcWindow;

    if (nX > 0) {
        (void)PatBlt(hdcDst, 0, 0, nX, nWinH, BLACKNESS);
    }
    if (nX + nDstW < nWinW) {
        (void)PatBlt(hdcDst, nX + nDstW, 0, nWinW - (nX + nDstW), nWinH, BLACKNESS);
    }
    if (nY > 0) {
        (void)PatBlt(hdcDst, nX, 0, nDstW, nY, BLACKNESS);
    }
    if (nY + nDstH < nWinH) {
        (void)PatBlt(hdcDst, nX, nY + nDstH, nDstW, nWinH - (nY + nDstH), BLACKNESS);
    }

    (void)SetStretchBltMode(hdcDst, COLORONCOLOR);
    (void)StretchBlt(hdcDst, nX, nY, nDstW, nDstH,
                     pGx->hdcMem, 0, 0, (int)pGx->cxWidth, (int)pGx->cyHeight,
                     SRCCOPY);

    if (hdcDst != pGx->hdcWindow) {
        (void)BitBlt(pGx->hdcWindow, 0, 0, nWinW, nWinH, hdcDst, 0, 0, SRCCOPY);
    }
    return true;
}

bool Gx_Resize(GxSurface *pGx, u32 cxWidth, u32 cyHeight)
{
    if (pGx == NULL || pGx->hWnd == NULL || pGx->hdcMem == NULL
        || cxWidth == 0 || cyHeight == 0) {
        return false;
    }
    if (cxWidth == pGx->cxWidth && cyHeight == pGx->cyHeight) {
        return true;
    }
    if (pGx->pOldObj != NULL) {
        (void)SelectObject(pGx->hdcMem, pGx->pOldObj);
        pGx->pOldObj = NULL;
    }
    if (pGx->hbm != NULL) {
        (void)DeleteObject(pGx->hbm);
        pGx->hbm = NULL;
    }
    pGx->pBits = NULL;
    pGx->cxWidth  = cxWidth;
    pGx->cyHeight = cyHeight;
    return Gx_MakeDib(pGx);
}

void Gx_ViewGeometry(int bLandscape, int bWideScreen,
                     int *pnW, int *pnH, int *pnUiX, int *pnUiY)
{
    if (bLandscape && bWideScreen) {
        *pnW   = 320;
        *pnH   = 240;
        *pnUiX = (320 - 240) / 2;
        *pnUiY = 240 - 320;
        return;
    }
    *pnW   = 240;
    *pnH   = 320;
    *pnUiX = 0;
    *pnUiY = 0;
}

/* See pufo_gx.h: no Win64 counterpart for GAPI's display hand-off. The DIB, the memory DC and the window DC stay
   valid, so a present while unfocused still works and a present after resuming needs no re-acquisition. */
void Gx_Suspend(GxSurface *pGx)
{
    (void)pGx;
}

void Gx_Resume(GxSurface *pGx)
{
    (void)pGx;
}

void Gx_Close(GxSurface *pGx)
{
    if (pGx == NULL) {
        return;
    }
    Gx_FreeBack(pGx);
    if (pGx->hdcMem != NULL) {
        if (pGx->pOldObj != NULL) {
            (void)SelectObject(pGx->hdcMem, pGx->pOldObj);
            pGx->pOldObj = NULL;
        }
        (void)DeleteDC(pGx->hdcMem);
        pGx->hdcMem = NULL;
    }
    if (pGx->hbm != NULL) {
        (void)DeleteObject(pGx->hbm);
        pGx->hbm = NULL;
    }
    if (pGx->hdcWindow != NULL && pGx->hWnd != NULL) {
        (void)ReleaseDC(pGx->hWnd, pGx->hdcWindow);
        pGx->hdcWindow = NULL;
    }
    pGx->pBits = NULL;
    pGx->hWnd  = NULL;
}
