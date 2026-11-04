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

#include <stdlib.h>
#include <string.h>

static bool Gx_MakeDib(GxSurface *pGx)
{
    BITMAPV4HEADER hdr;
    void *pBits = NULL;

    memset(&hdr, 0, sizeof(hdr));
    hdr.bV4Size          = sizeof(hdr);
    hdr.bV4Width         = (LONG)pGx->nDibW;
    hdr.bV4Height        = -(LONG)pGx->nDibH; /* top-down, matches GAPI */
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
    if (pGx->bRotate90CW) {
        pGx->pFace = (u8 *)pBits; /* the DIB receives the rotated face each present */
    } else {
        pGx->pBits = (u8 *)pBits; /* plain: the game renders straight into the DIB */
    }

    /* The original stores pitch as signed and relies on it being positive for
     * the native 240x320 mode. A top-down DIB gives the same layout. */
    pGx->cbxPitch = pGx->cBPP;
    pGx->cbyPitch = (int)(pGx->cxWidth * (u32)pGx->cBPP);
    return true;
}

bool Gx_Open(GxSurface *pGx, HWND hWnd, u32 cxWidth, u32 cyHeight, int bRotate90CW)
{
    if (pGx == NULL || hWnd == NULL || cxWidth == 0 || cyHeight == 0) {
        return false;
    }
    if (pGx->hWnd != NULL) {
        return false; /* already open */
    }

    memset(pGx, 0, sizeof(*pGx));
    pGx->hWnd        = hWnd;
    pGx->cxWidth     = cxWidth;
    pGx->cyHeight    = cyHeight;
    pGx->cBPP        = PUFO_SURF_BPP;
    pGx->bRotate90CW = bRotate90CW ? 1 : 0;
    pGx->nDibW       = (int)(bRotate90CW ? cyHeight : cxWidth);
    pGx->nDibH       = (int)(bRotate90CW ? cxWidth  : cyHeight);

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
    if (pGx->bRotate90CW) {
        /* The game keeps rendering the 240x320 surface; the DIB only ever
         * holds the rotated face, so the surface needs its own buffer. */
        pGx->pBits = (u8 *)malloc((size_t)cxWidth * (size_t)cyHeight * (size_t)pGx->cBPP);
        if (pGx->pBits == NULL) {
            Gx_Close(pGx);
            return false;
        }
        memset(pGx->pBits, 0, (size_t)cxWidth * (size_t)cyHeight * (size_t)pGx->cBPP);
        pGx->bOwnsSurface = 1;
    }
    return true;
}

void Gx_PointerToLogical(int bRotate90CW, u32 cyLogical, int nSx, int nSy,
                         i32 *pnX, i32 *pnY)
{
    if (bRotate90CW) {
        *pnX = (i32)nSy;
        *pnY = (i32)cyLogical - 1 - nSx;
        return;
    }
    *pnX = (i32)nSx;
    *pnY = (i32)nSy;
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

    if (pGx->bRotate90CW) {
        /* 90 CW: dst(wx,wy) = src(lx=wy, ly=cyHeight-1-wx). The portrait top
         * edge lands on the face's right edge; the centered UI stays centered. */
        const u16 *pSrc16  = (const u16 *)pGx->pBits;
        u16       *pDst16  = (u16 *)pGx->pFace;
        size_t     srcRows = (size_t)pGx->cyHeight;
        size_t     srcW    = (size_t)pGx->cxWidth;

        for (int wy = 0; wy < pGx->nDibH; ++wy) {
            u16 *pDstRow = pDst16 + (size_t)wy * (size_t)pGx->nDibW;
            for (int wx = 0; wx < pGx->nDibW; ++wx) {
                pDstRow[wx] = pSrc16[(size_t)wy + (srcRows - 1u - (size_t)wx) * srcW];
            }
        }
    }

    /* Integer scale keeps the pixel-art sharp; the original never rescaled at
     * all, so anything other than 1:1 or an integer multiple would be a lie. */
    nWinW = rcClient.right - rcClient.left;
    nWinH = rcClient.bottom - rcClient.top;
    nScale = nWinW / pGx->nDibW;
    if (nWinH / pGx->nDibH < nScale) {
        nScale = nWinH / pGx->nDibH;
    }
    if (nScale < 1) {
        nScale = 1;
    }
    nDstW = pGx->nDibW * nScale;
    nDstH = pGx->nDibH * nScale;
    nX = (nWinW - nDstW) / 2;
    nY = (nWinH - nDstH) / 2;

    (void)SetStretchBltMode(pGx->hdcWindow, COLORONCOLOR);
    (void)StretchBlt(pGx->hdcWindow, nX, nY, nDstW, nDstH,
                     pGx->hdcMem, 0, 0, pGx->nDibW, pGx->nDibH,
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
    if (pGx->bOwnsSurface) {
        free(pGx->pBits);
        pGx->bOwnsSurface = 0;
    }
    pGx->pBits = NULL;
    pGx->pFace = NULL;
    pGx->hWnd  = NULL;
}
