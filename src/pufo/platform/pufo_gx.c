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
    pGx->pBits   = (u8 *)pBits;

    /* The original stores pitch as signed and relies on it being positive for
     * the native 240x320 mode. A top-down DIB gives the same layout. */
    pGx->cbxPitch = pGx->cBPP;
    pGx->cbyPitch = (int)(pGx->cxWidth * (u32)pGx->cBPP);
    return true;
}

bool Gx_Open(GxSurface *pGx, HWND hWnd, u32 cxWidth, u32 cyHeight)
{
    if (pGx == NULL || hWnd == NULL || cxWidth == 0 || cyHeight == 0) {
        return false;
    }
    if (pGx->hWnd != NULL) {
        return false; /* already open */
    }

    memset(pGx, 0, sizeof(*pGx));
    pGx->hWnd     = hWnd;
    pGx->cxWidth  = cxWidth;
    pGx->cyHeight = cyHeight;
    pGx->cBPP     = PUFO_SURF_BPP;

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

bool Gx_Present(GxSurface *pGx)
{
    RECT rcClient;
    int  nWinW;
    int  nWinH;
    int  nScale;
    int  nDstW;
    int  nDstH;
    int  nX;
    int  nY;

    if (pGx == NULL || pGx->hWnd == NULL || pGx->pBits == NULL) {
        return false;
    }
    if (!GetClientRect(pGx->hWnd, &rcClient)) {
        return false;
    }

    /* Integer scale keeps the pixel-art sharp; the original never rescaled at
     * all, so anything other than 1:1 or an integer multiple would be a lie. */
    nWinW = rcClient.right - rcClient.left;
    nWinH = rcClient.bottom - rcClient.top;
    nScale = nWinW / (int)pGx->cxWidth;
    if (nWinH / (int)pGx->cyHeight < nScale) {
        nScale = nWinH / (int)pGx->cyHeight;
    }
    if (nScale < 1) {
        nScale = 1;
    }
    nDstW = (int)pGx->cxWidth * nScale;
    nDstH = (int)pGx->cyHeight * nScale;
    nX = (nWinW - nDstW) / 2;
    nY = (nWinH - nDstH) / 2;

    (void)SetStretchBltMode(pGx->hdcWindow, COLORONCOLOR);
    (void)StretchBlt(pGx->hdcWindow, nX, nY, nDstW, nDstH,
                     pGx->hdcMem, 0, 0, (int)pGx->cxWidth, (int)pGx->cyHeight,
                     SRCCOPY);
    return true;
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
