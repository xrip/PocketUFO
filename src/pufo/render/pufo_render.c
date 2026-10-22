/* pufo_render.c - software surface, blitter, and palette loading.
 *
 * Evidence: see pufo_render.h for the full address list.
 */
#include "render/pufo_render.h"

#include <stdlib.h>
#include <string.h>

#include "asset/pufo_asset.h"
#include "dat/pufo_dat.h"
#include "util/pufo_str.h"

BlitState    g_blitState;
RenderTarget g_renderTarget;

/* unk_156A7C @ 0x156a7c: the global 240x320 surface every non-Ex blit uses. */
SoftSurf *g_pMainSurface;

/* aPalette @ 0x1472d0. The original does not bounds-check the slot; the highest
 * slot any caller passes is 0x3F, so 64 slots is what that requires. */
u32 g_aPalette[PUFO_PAL_SLOTS][PUFO_PAL_SIZE];

/* unk_1183F4 @ 0x1183f4 (Map + 0x4A3C): the palette slot base that
 * Font_BlitGlyphToSoft @ 0x9ec48 adds to the glyph palette index
 * (`0x1472D0 + 256 * (unk_1183F4 + nPal)`). Render_Frame's default branch writes 34 and
 * Geoscape_InitUI writes per screen; it is 0 at startup, which is why the start-menu text
 * draws from the still-empty slot 0 (black glyphs). Only the font path adds this base --
 * SoftSurf_BlitIndexed @ 0x9e9f4 uses its slot argument directly. */
i32 g_nGlyphPalSlotBase;

/* gxDisp.ffFormat @ 0x188398+0x14. The Win64 target is RGB565. */
u32 g_ffFormat = PUFO_FF_565;

/* Map_nGamma @ 0x128AB8 (Map+0x15100) lives in the shell's Map model and ARM reaches it as a global from
 * two places: 0x37368 loads it once before the palette LUT loops, and 0x2d860 loads it on every call of
 * Gfx_PackRgb565Pair. Neither passes it as an argument, so the LUT pass reads the cell the shell installed
 * once per world (Render_SetGammaCell, from World_Init) and the colour pack reads the cell its env points
 * at -- both stand-ins for the same dword. NULL means nothing was installed and 0 is then the value: the
 * BSS initial. The original's own startup value is not 0 -- Map_InitAndLoadRegistry writes the registry
 * "gamma" there at 0x227A0, default 2 (0x22738, the out-of-range fallback at 0x22794). */
static const i32 *g_pnGammaCell;

void Render_SetGammaCell(const i32 *pCell)
{
    g_pnGammaCell = pCell;
}

static i32 Gamma_Current(void)
{
    return (g_pnGammaCell != NULL) ? *g_pnGammaCell : 0;
}

static i32 ColorEnvGamma(const GfxColorEnv *pEnv)
{
    return (pEnv->pnGamma != NULL) ? *pEnv->pnGamma : 0;
}

/* Blitter scratch, matching blitColorKey..s_nBlendAlpha @ 0xa3028..0xa3040. */
static i32 s_nColorKey;   /* blitColorKey @ 0xa3028: glyph index treated as transparent */
static i32 s_nColorMid;   /* blitColorMid @ 0xa302c: green field source of the fill colour */
static i32 s_nColorLow;   /* blitColorLow @ 0xa3030: red field source of the fill colour */
static i32 s_nRectW;      /* blitRectW @ 0xa3034: fill width, and its clip bound */
static i32 s_nRectH;      /* blitRectH @ 0xa3038: fill height, and its clip bound */
static i32 s_nMaskPtr;    /* dword_A303C, mask pointer for the Flip blend */
static i32 s_nBlendAlpha; /* dword_A3040 */

void SoftSurf_ctor(SoftSurf *pSurf)
{
    pSurf->pPixels = NULL;
    pSurf->pAlloc  = NULL;
}

void SoftSurf_dtor(SoftSurf *pSurf)
{
    if (pSurf->pPixels != NULL) {
        free(pSurf->pAlloc);
    }
    pSurf->pPixels = NULL;
    pSurf->pAlloc  = NULL;
}

void *SoftSurf_AllocPixels(SoftSurf *pSurf, i32 nWidth, i32 nHeight, i32 nPixelMode)
{
    i32 nBytesPP = 0;
    u16 anHeader[2];

    if (pSurf->pPixels != NULL) {
        free(pSurf->pAlloc);
    }

    pSurf->nWidth     = nWidth;
    pSurf->nHeight    = nHeight;
    pSurf->nPixelMode = nPixelMode;

    /* The original leaves nBytesPP at 0 for a mode outside 0..3 and then
     * allocates 4 bytes. Reproduced as-is rather than "fixed". */
    if (nPixelMode == 0) {
        nBytesPP = 1;
    } else if (nPixelMode >= 1 && nPixelMode <= 2) {
        nBytesPP = 2;
    } else if (nPixelMode == 3) {
        nBytesPP = 3;
    }
    pSurf->nBytesPP = nBytesPP;

    pSurf->pAlloc = malloc((size_t)(nBytesPP * nWidth * nHeight) + 4u);
    if (pSurf->pAlloc == NULL) {
        pSurf->pPixels = NULL;
        return NULL;
    }

    /* 4-byte header: u16 width, u16 height. pPixels points past it. */
    anHeader[0] = (u16)nWidth;
    anHeader[1] = (u16)nHeight;
    memmove(pSurf->pAlloc, anHeader, 4);

    pSurf->nBlitW    = nWidth;
    pSurf->nBlitH    = nHeight;
    pSurf->nBlitMode = nPixelMode;
    pSurf->pPixels   = pSurf->pAlloc + 4;
    pSurf->pBlitSrc  = pSurf->pPixels;
    return pSurf->pPixels;
}

i32 SoftSurf_GetWidth(const SoftSurf *pSurf)
{
    return pSurf->nWidth;
}

i32 SoftSurf_GetHeight(const SoftSurf *pSurf)
{
    return pSurf->nHeight;
}

u8 *SoftSurf_GetPixels(const SoftSurf *pSurf)
{
    return pSurf->pPixels;
}

/* SoftSurf_GetAlloc @ 0x9fcbc. The raw allocation, which is four bytes below pPixels
 * because the glyph header is the allocation prefix. */
void *SoftSurf_GetAlloc(const SoftSurf *pSurf)
{
    return pSurf->pAlloc;
}

void Render_SetTarget(i32 nW, i32 nH, i32 nBytes, i32 nPitchX, i32 nPitchY, u8 *pBits)
{
    g_renderTarget.nWidth      = nW;
    g_renderTarget.nHeight     = nH;
    g_renderTarget.nPixelBytes = nBytes;
    g_renderTarget.nPitchX     = nPitchX;
    g_renderTarget.nPitchY     = nPitchY;
    g_renderTarget.pBits       = pBits;
    Blit_SetTargetParams(&g_renderTarget);
}

void Blit_LoadSurface(const SoftSurf *pSurf)
{
    g_blitState.nSurf0 = pSurf->nBlitW;
    g_blitState.nSurf1 = pSurf->nBlitH;
    g_blitState.nSurf2 = pSurf->nBlitMode;
    g_blitState.pSrc   = pSurf->pBlitSrc;
}

void Blit_SetTargetParams(const RenderTarget *pTarget)
{
    g_blitState.nWidth     = pTarget->nWidth;
    g_blitState.nHeight    = pTarget->nHeight;
    g_blitState.nPixelMode = pTarget->nPixelBytes;
    g_blitState.nDstPitchX = pTarget->nPitchX;
    g_blitState.nDstPitchY = pTarget->nPitchY;
    g_blitState.pDst       = pTarget->pBits;
}

void BlitParams_Set(i32 nAlpha, i32 nMaskPtr)
{
    s_nBlendAlpha = nAlpha;
    s_nMaskPtr    = nMaskPtr;
}

/* Blit_SetRectAndColor @ 0xa3f5c. Renamed in the IDB from Blit_SetSrcRect: the
 * assignment is not positional and the old argument names were wrong. arg0/arg1
 * become the rectangle size; arg2/arg3 are only ever read as colour operands. */
void Blit_SetRectAndColor(i32 nRectW, i32 nRectH, i32 nColorMid, i32 nColorLow)
{
    s_nRectW    = nRectW;
    s_nRectH    = nRectH;
    s_nColorMid = nColorMid;
    s_nColorLow = nColorLow;
}

/* Blit_SetColorKey @ 0xa3624. The one writer of blitColorKey, which
 * SoftSurf_BlitIndexedKeyed_Inner compares against every glyph byte; a match skips
 * the pixel, so this is the transparency index. */
i32 Blit_SetColorKey(i32 nKey)
{
    s_nColorKey = nKey;
    return nKey;
}

/* Blit_CopyRows @ 0xa3a14: logical surface -> render target, 1:1.
 *
 * SoftSurf_BlitCopyRaw @ 0xa3db8 is the same function twice in the original - same three
 * pixel-mode branches, same loop bodies, same 420 bytes. One implementation covers both;
 * the duplicate is recorded in docs/parity/bugs.tsv rather than copied. */
void Blit_CopyRows(void)
{
    i32 nWidth  = g_blitState.nWidth;
    i32 nHeight = g_blitState.nHeight;
    i32 nPitchX = g_blitState.nDstPitchX;
    i32 nPitchY = g_blitState.nDstPitchY;
    i32 y;

    if (g_blitState.nPixelMode == 0) {
        u8       *pDst = g_blitState.pDst;
        const u8 *pSrc = g_blitState.pSrc;

        for (y = 0; y < nHeight; ++y) {
            u8 *pRow = pDst;
            i32 x;

            for (x = 0; x < nWidth; ++x) {
                *pDst = *pSrc++;
                pDst += nPitchX;
            }
            pDst = pRow + nPitchY;
        }
    } else if (g_blitState.nPixelMode == 1 || g_blitState.nPixelMode == 2) {
        u16       *pDst = (u16 *)g_blitState.pDst;
        const u16 *pSrc = (const u16 *)g_blitState.pSrc;

        for (y = 0; y < nHeight; ++y) {
            u16 *pRow = pDst;
            i32  x;

            for (x = 0; x < nWidth; ++x) {
                *pDst = *pSrc++;
                pDst = (u16 *)((u8 *)pDst + nPitchX);
            }
            pDst = (u16 *)((u8 *)pRow + nPitchY);
        }
    } else {
        u8       *pDst = g_blitState.pDst;
        const u8 *pSrc = g_blitState.pSrc;

        for (y = 0; y < nHeight; ++y) {
            u8 *pRow = pDst;
            i32 x;

            for (x = 0; x < nWidth; ++x) {
                pDst[0] = pSrc[0];
                pDst[1] = pSrc[1];
                pDst[2] = pSrc[2];
                pSrc += 3;
                pDst += nPitchX;
            }
            pDst = pRow + nPitchY;
        }
    }
}

/* The fill colour Blit_CopyToXY and SoftSurf_BlitSrcRect_Inner write: the index
 * supplies the 5-bit top field, the two colour operands the middle and low ones. */
static u32 Blit_PackedColor(u32 nIdx)
{
    return ((nIdx >> 3) << 11)
         + 32u * ((u32)s_nColorMid >> 2)
         + ((u32)s_nColorLow >> 3);
}

/* The clip every rectangle blit in this module performs. *pnW / *pnH are the requested size;
 * on return they are the size to draw, and *pnSkipX / *pnSkipY say how many source columns and
 * rows fell outside the surface. The guards are the original's, including x <= nSurf0 rather
 * than x < nSurf0. */
static bool Blit_ClipRect(i32 *px, i32 *py, i32 *pnW, i32 *pnH, i32 *pnSkipX, i32 *pnSkipY)
{
    i32 x = *px, y = *py;
    i32 nW = *pnW, nH = *pnH;
    i32 nXEnd = x + nW, nYEnd = y + nH;

    *pnSkipX = 0;
    *pnSkipY = 0;

    if (nW < 0) { nW = 0; }
    if (nH < 0) { nH = 0; }
    if (!(x <= g_blitState.nSurf0 && y <= g_blitState.nSurf1
          && nXEnd >= 0 && nYEnd >= 0)) {
        *pnW = 0; *pnH = 0;
        return false;
    }
    if (x < 0) { nW += x; *pnSkipX = -x; x = 0; }
    if (y < 0) { nH += y; *pnSkipY = -y; y = 0; }
    if (nXEnd > g_blitState.nSurf0) { nW -= nXEnd - g_blitState.nSurf0; }
    if (nYEnd > g_blitState.nSurf1) { nH -= nYEnd - g_blitState.nSurf1; }

    *px = x; *py = y; *pnW = nW; *pnH = nH;
    return nW != 0 && nH != 0;
}

static bool Blit_ClipFillRect(i32 *px, i32 *py, i32 *pnW, i32 *pnH)
{
    i32 nSkipX, nSkipY;

    *pnW = s_nRectW;
    *pnH = s_nRectH;
    return Blit_ClipRect(px, py, pnW, pnH, &nSkipX, &nSkipY);
}

/* Blit_CopyToXY @ 0xa42e4. Fills the rectangle in the LOADED SURFACE, not the render
 * target: the destination is blitState.pSrc, stride blitState.nSurf0. Runs only for
 * pixel mode 1 or 2; mode 0 and anything else falls through and draws nothing. */
void Blit_CopyToXY(i32 x, i32 y, u32 nIdx)
{
    u32  nColor = Blit_PackedColor(nIdx);
    i32  nFillW, nFillH;
    u16 *pRow;
    i32  nRow, nCol;

    if (!(g_blitState.nPixelMode == 1 || g_blitState.nPixelMode == 2)) {
        return;
    }
    if (!Blit_ClipFillRect(&x, &y, &nFillW, &nFillH)) {
        return;
    }

    pRow = (u16 *)g_blitState.pSrc + x + g_blitState.nSurf0 * y;
    for (nRow = nFillH; nRow != 0; --nRow) {
        u16 *pStart = pRow;
        for (nCol = nFillW; nCol != 0; --nCol) {
            *pRow++ = (u16)nColor;
        }
        pRow = pStart + g_blitState.nSurf0;
    }
}

/* SoftSurf_BlitSrcRect_Inner @ 0xa4450. Same rectangle, same clipping, same colour as
 * Blit_CopyToXY, but it blends the destination with that colour per 5-6-5 field using
 * nKey and 255-nKey, so nKey=255 is opaque and nKey=0 leaves the pixel alone. Note the
 * gate here reads nSurf2, the SURFACE mode, where Blit_CopyToXY reads nPixelMode, the
 * TARGET mode: the original really does gate the two on different things. */
void SoftSurf_BlitSrcRect_Inner(i32 x, i32 y, u32 nIdx, i32 nKey)
{
    u32  nColor = Blit_PackedColor(nIdx);
    i32  nFillW, nFillH;
    u16 *pRow;
    i32  nRow, nCol;
    i32  nInv = 255 - nKey;

    if (!(g_blitState.nSurf2 == 1 || g_blitState.nSurf2 == 2)) {
        return;
    }
    if (!Blit_ClipFillRect(&x, &y, &nFillW, &nFillH)) {
        return;
    }

    pRow = (u16 *)g_blitState.pSrc + x + g_blitState.nSurf0 * y;
    for (nRow = nFillH; nRow != 0; --nRow) {
        u16 *pStart = pRow;
        for (nCol = nFillW; nCol != 0; --nCol) {
            u16 nDst = *pRow;
            u16 nLow = (u16)(((nDst & 0x1Fu)    * (u32)nInv) >> 8)
                     + (u16)(((nColor & 0x1Fu)  * (u32)nKey) >> 8);
            u16 nMid = (u16)((((nDst & 0x7E0u)   >> 5) * (u32)nInv) >> 8)
                     + (u16)((((nColor & 0x7E0u) >> 5) * (u32)nKey) >> 8);
            u16 nTop = (u16)((((nDst & 0xF800u)   >> 11) * (u32)nInv) >> 8)
                     + (u16)((((nColor & 0xF800u) >> 11) * (u32)nKey) >> 8);

            *pRow = (u16)(nLow + 32u * nMid + ((u32)nTop << 11));
            ++pRow;
        }
        pRow = pStart + g_blitState.nSurf0;
    }
}

/* SoftSurf_BlitRectEx @ 0x9fc30 and SoftSurf_BlitSrcRectEx @ 0x9fc70: load the surface
 * descriptor, publish the rectangle and colour operands, then fill or blend. */
void SoftSurf_BlitRectEx(const SoftSurf *pSurf, i32 x, i32 y, i32 nRectW, i32 nRectH,
                         u32 nIdx, i32 nColorMid, i32 nColorLow)
{
    Blit_LoadSurface(pSurf);
    Blit_SetRectAndColor(nRectW, nRectH, nColorMid, nColorLow);
    Blit_CopyToXY(x, y, nIdx);
}

void SoftSurf_BlitSrcRectEx(const SoftSurf *pSurf, i32 x, i32 y, i32 nRectW, i32 nRectH,
                            u32 nIdx, i32 nColorMid, i32 nColorLow, i32 nKey)
{
    Blit_LoadSurface(pSurf);
    Blit_SetRectAndColor(nRectW, nRectH, nColorMid, nColorLow);
    SoftSurf_BlitSrcRect_Inner(x, y, nIdx, nKey);
}

/* SoftSurf_BlitRect @ 0x9ebb0 and SoftSurf_BlitSrcRect @ 0x9ebf8: the bound forms that
 * pin g_pMainSurface. */
void SoftSurf_BlitRect(i32 x, i32 y, i32 nRectW, i32 nRectH, u32 nIdx,
                       i32 nColorMid, i32 nColorLow)
{
    SoftSurf_BlitRectEx(g_pMainSurface, x, y, nRectW, nRectH, nIdx, nColorMid, nColorLow);
}

void SoftSurf_BlitSrcRect(i32 x, i32 y, i32 nRectW, i32 nRectH, u32 nIdx,
                          i32 nColorMid, i32 nColorLow, i32 nKey)
{
    SoftSurf_BlitSrcRectEx(g_pMainSurface, x, y, nRectW, nRectH, nIdx,
                           nColorMid, nColorLow, nKey);
}

/* SoftSurf_SamplePixel @ 0xa4630. Reads one byte out of the loaded surface: the byte of an
 * 8bpp pixel, the low byte of a 16bpp pixel, or the first byte of a 24bpp pixel. The three
 * branches are the three strides, and the third one is written in the original as
 * "2*x + 2*y*nSurf0 + x + y*nSurf0", which is just 3*(x + y*nSurf0).
 *
 * The bounds test is x <= nSurf0 and y <= nSurf1, not <. A coordinate equal to the dimension
 * is accepted and reads past the row - reproduced, recorded in docs/parity/bugs.tsv. */
i32 SoftSurf_SamplePixel(i32 x, i32 y)
{
    const u8 *p;
    i32       nStride;

    if (!(x <= g_blitState.nSurf0 && y <= g_blitState.nSurf1 && x >= 0 && y >= 0)) {
        return 0;
    }

    if (g_blitState.nSurf2 == 0) {
        nStride = 1;
    } else if (g_blitState.nSurf2 == 1 || g_blitState.nSurf2 == 2) {
        nStride = 2;
    } else {
        nStride = 3;
    }

    p = (const u8 *)g_blitState.pSrc;
    return p[nStride * (x + y * g_blitState.nSurf0)];
}

/* SoftSurf_BlurPixels @ 0xa2000. A four-neighbour box blur over the INTERIOR only: it
 * starts at offset (nWidth + 1) and runs (nHeight - 2) rows by (nWidth - 2) columns, so the
 * border is never touched - which is why SoftSurf_DrawBorderAndBlur fills the border first.
 *
 * Two things the original does that are easy to miss:
 *   - the average excludes the centre pixel: left, right, up, down only;
 *   - it blurs in place, so the up neighbour of any pixel was already overwritten earlier in
 *     the same pass. The result is a cascade, not a single convolution.
 * If the average is non-zero it is then reduced by one; a zero average stays zero.
 *
 * The original returns its pPixels argument as an int. The port returns it as the pointer it
 * is, which is the same value without a pointer-to-int cast. */
u8 *SoftSurf_BlurPixels(u8 *pPixels, i32 nWidth, i32 nHeight)
{
    u8 *p     = pPixels + nWidth + 1;
    i32 nRows = nHeight - 2;

    do {
        u8 *pRowStart = p;
        i32 nCols     = nWidth - 2;

        do {
            u32 nAvg = ((u32)p[-1] + (u32)p[1] + (u32)p[-nWidth] + (u32)p[nWidth]) >> 2;

            if (nAvg != 0) {
                nAvg -= 1;
            }
            *p++ = (u8)nAvg;
            --nCols;
        } while (nCols != 0);

        p = pRowStart + nWidth;
        --nRows;
    } while (nRows != 0);

    return pPixels;
}

/* SoftSurf_BlitRawGlyph @ 0xa33c8, named SoftSurf_BlitIndexed_Mode12 in the original although
 * nothing here indexes a palette: it copies the glyph's own pixel values into the loaded
 * surface. The glyph is the usual 4-byte header (u16 width, u16 height) followed by pixels.
 *
 * Branch 1 (surface mode 1 or 2) reads 2-byte pixels. Branch 2 (any other mode) reads 3-byte
 * pixels - and there the original has a confirmed stride bug: it writes three bytes per pixel
 * but advances the destination by two (LDRH/STRH, LDRB/STRB, ADD R7,R7,#2 at 0xa35e4) while the
 * row step is 3 * nSurf0. Pixels therefore overlap each other and a row of n of them covers 2n
 * bytes of a 3n-byte row. Reproduced verbatim; recorded in docs/parity/bugs.tsv.
 *
 * The glyph pointer is advanced by however much the clip threw away, so a partially off-surface
 * glyph still draws the right pixels. */
void SoftSurf_BlitRawGlyph(i32 x, i32 y, const u8 *pGlyph)
{
    i32  nGlyphW = (i32)((pGlyph[0]) | (pGlyph[1] << 8));
    i32  nGlyphH = (i32)((pGlyph[2]) | (pGlyph[3] << 8));
    const u8 *pPix = pGlyph + 4;
    i32 nDrawW = nGlyphW, nDrawH = nGlyphH;
    i32 nSkipX, nSkipY, nRow, nCol;

    if (!Blit_ClipRect(&x, &y, &nDrawW, &nDrawH, &nSkipX, &nSkipY)) {
        return;
    }

    if (g_blitState.nSurf2 == 1 || g_blitState.nSurf2 == 2) {
        u16 *pRow = (u16 *)g_blitState.pSrc + x + g_blitState.nSurf0 * y;

        pPix += 2 * nSkipX + 2 * nSkipY * nGlyphW;
        for (nRow = nDrawH; nRow != 0; --nRow) {
            u16 *pStart = pRow;
            for (nCol = nDrawW; nCol != 0; --nCol) {
                *pRow++ = (u16)((pPix[0]) | (pPix[1] << 8));
                pPix += 2;
            }
            pPix += 2 * (nGlyphW - nDrawW);
            pRow = pStart + g_blitState.nSurf0;
        }
        return;
    }

    {
        u8 *pRow = (u8 *)g_blitState.pSrc + 3 * x + 3 * g_blitState.nSurf0 * y;

        pPix += 3 * nSkipX + 3 * nSkipY * nGlyphW;
        for (nRow = nDrawH; nRow != 0; --nRow) {
            u8 *pStart = pRow;
            for (nCol = nDrawW; nCol != 0; --nCol) {
                pRow[0] = pPix[0];
                pRow[1] = pPix[1];
                pRow[2] = pPix[2];
                /* ORIGINAL BUG (0xa35E4), reproduced: the destination advances by 2 while the
                 * row stride is 3 * nSurf0. */
                pRow += 2;
                pPix += 3;
            }
            pPix += 3 * (nGlyphW - nDrawW);
            pRow = pStart + 3 * g_blitState.nSurf0;
        }
    }
}

/* Gfx_PackRgb565Pair @ 0x2d82c - the central colour helper, with over 100 call sites across
 * every draw function in the game.
 *
 * What it does: builds a scale factor from a Map-resident value, divides each channel by it,
 * divides again by a fade factor when a fade is in progress, clamps to 5/6/5 bits and packs
 * (r<<11) | (g<<5) | b. It runs only when the display format is 565 (bit 7 of ffFormat).
 *
 * The factor is (double)Map_nGamma * 0.1 + 0.7. The constants are exact doubles:
 * 0x3FB99999:0x9999999A is 0.1 and 0x3FE66666:0x66666666 is 0.7. The value comes from
 * unk_128AB8 (Map + 0x15100) at 0x2D858/0x2D860 -- a global read on every call, not an argument;
 * Hex-Rays renders it as if it came from r1. The port reads the cell the env points at.
 *
 * The channel shifts are not symmetric: red and blue are >> 3 (8 to 5 bits) and green is >> 2
 * (8 to 6 bits).
 *
 * The original reads ffFormat through its first argument, which IDA named nScale but is a pointer
 * into the GX display block (LDR R0,[R0,#0x14] at 0x2d840). The port already owns g_ffFormat, so
 * it reads that instead - recorded as a deviation, not hidden.
 *
 * The two stores, 0x147B54 and 0x147B3C, are palette slot 2 entries 33 and 27. They have one
 * xref each (the write here) because the reader reaches them through the palette table:
 * Text_DrawString forces the glyph palette base to 2, and the font glyphs index 27/33. That is
 * the text colour mechanism. */
u32 Gfx_PackRgb565Pair(const GfxColorEnv *pEnv, i32 r1, i32 g1, i32 b1,
                       i32 r2, i32 g2, i32 b2)
{
    i32    nGamma  = ColorEnvGamma(pEnv);   /* 0x2d860: Map_nGamma, read on every call */
    double dFactor = (double)nGamma * 0.1 + 0.7;
    double dFade   = 0.0;
    i32    nR1, nG1, nB1, nR2, nG2, nB2;
    u32    nFirst, nSecond;

    if ((g_ffFormat & PUFO_FF_565) == 0) {
        return 0;
    }

    nR1 = (i32)((double)(r1 >> 3) / dFactor);
    nG1 = (i32)((double)(g1 >> 2) / dFactor);
    nB1 = (i32)((double)(b1 >> 3) / dFactor);

    if (pEnv->nFadeCount > 0) {
        /* The inner division is integer in the original: itod is applied to the result of
         * (11 - nFadeCount) / 2, not to the difference itself. */
        dFade = (double)((11 - pEnv->nFadeCount) / 2 + 1);
        nR1 = (i32)((double)nR1 / dFade);
        nG1 = (i32)((double)nG1 / dFade);
        nB1 = (i32)((double)nB1 / dFade);
    }

    if (nR1 > 31) { nR1 = 31; }
    if (nG1 > 63) { nG1 = 63; }
    if (nB1 > 31) { nB1 = 31; }

    nFirst  = (u32)(nB1 + 32 * (nG1 + (nR1 << 6)));
    g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_FIRST] = nFirst; /* unk_147B54 */

    nR2 = (i32)((double)(r2 >> 3) / dFactor);
    nG2 = (i32)((double)(g2 >> 2) / dFactor);
    nB2 = (i32)((double)(b2 >> 3) / dFactor);

    if (pEnv->nFadeCount > 0) {
        nR2 = (i32)((double)nR2 / dFade);
        nG2 = (i32)((double)nG2 / dFade);
        nB2 = (i32)((double)nB2 / dFade);
    }

    if (nR2 > 31) { nR2 = 31; }
    if (nG2 > 63) { nG2 = 63; }
    if (nB2 > 31) { nB2 = 31; }

    nSecond = (u32)(nB2 + 32 * (nG2 + (nR2 << 6)));
    g_aPalette[PUFO_TEXT_PAL_SLOT][PUFO_TEXT_PAL_IDX_SECOND] = nSecond; /* unk_147B3C */

    return nSecond;
}

/* BlitState_Get @ 0xa479c. Returns the width of the currently loaded surface. */
i32 BlitState_Get(void)
{
    return g_blitState.nSurf0;
}

/* SoftSurf_BlitRectProxy @ 0x2dce4. A caller-side shim: its first argument is dropped and
 * the last three are widened from bytes before the bound fill is called. */
void SoftSurf_BlitRectProxy(i32 nIgnored, i32 x, i32 y, i32 nRectW, i32 nRectH,
                            u8 nIdx, u8 nColorMid, u8 nColorLow)
{
    (void)nIgnored;
    SoftSurf_BlitRect(x, y, nRectW, nRectH, (u32)nIdx, (i32)nColorMid, (i32)nColorLow);
}

/* Render_BlitSoft_LoRes @ 0x9fa7c / HiRes @ 0x9fa90.
 *
 * The HiRes variant (Blit_CopyRows_HiRes @ 0xa3bb8) writes each source pixel as
 * a 2x2 block, which is how the original filled a 480x640 panel from the
 * 240x320 surface. The Win64 target presents the logical surface at integer
 * scale instead, so the 1:1 row copy is the correct path here. */
void Render_BlitSurface(const SoftSurf *pSurf)
{
    Blit_LoadSurface(pSurf);
    Blit_CopyRows();
}

/* Shared clipping preamble of the glyph blitters. Returns the shifted index
 * pointer, or NULL when the glyph is fully off-surface. */
typedef struct GlyphClip {
    i32       nW;
    i32       nH;
    const u8 *pIdx;
    i32       nPadX;
    i32       nStride;
} GlyphClip;

static const u8 *Blit_ClipGlyph(i32 *px, i32 *py, const u8 *pIdx,
                                i32 nGlyphW, i32 nGlyphH, GlyphClip *pOut)
{
    /* 0xA30B8/0xA30BC compute the glyph's far edges from the CALLER's x and y, and the two
     * clamps at 0xA310C..0xA3120 subtract against those saved edges. They are not re-derived
     * from the clamped x/y, which is what makes a partially off-surface glyph keep exactly the
     * columns that are on surface: with x = -280, nGlyphW = 800 and a 240-wide surface the
     * original clips to 800 - 280 - (520 - 240) = 240 columns, while re-measuring from x = 0
     * would give 800 - 280 - (800 - 240) = -40 and drop the whole blit. */
    i32 x = *px;
    i32 y = *py;
    i32 nW = nGlyphW;
    i32 nH = nGlyphH;
    const i32 nFarX = x + nGlyphW; /* R8 at 0xA30B8 */
    const i32 nFarY = y + nGlyphH; /* R9 at 0xA30BC */

    if (!(x <= g_blitState.nSurf0 && y <= g_blitState.nSurf1
          && nFarX >= 0 && nFarY >= 0)) {
        return NULL;
    }
    if (x < 0) {
        pIdx -= x;
        nW   += x;
        x     = 0;
    }
    if (y < 0) {
        pIdx -= y * nGlyphW;
        nH   += y;
        y     = 0;
    }
    if (nFarX > g_blitState.nSurf0) {
        nW -= nFarX - g_blitState.nSurf0;
    }
    if (nFarY > g_blitState.nSurf1) {
        nH -= nFarY - g_blitState.nSurf1;
    }
    if (nW <= 0 || nH <= 0) {
        return NULL;
    }

    *px = x;
    *py = y;
    pOut->nW      = nW;
    pOut->nH      = nH;
    pOut->pIdx    = pIdx;
    pOut->nPadX   = nGlyphW - nW;
    pOut->nStride = g_blitState.nSurf0;
    return pIdx;
}

/* SoftSurf_BlitIndexed_Inner @ 0xa306c / Keyed_Inner @ 0xa3638. Dispatches on
 * the surface pixel mode:
 *   0   -> 1 byte per pixel, index written directly
 *   1,2 -> u16 palette entry
 *   >2  -> 3 bytes per pixel, palette RGB triple, stride 3
 * The 24bpp row base is pSrc + 3*nSurf0*y + 3*x and the row advance is
 * 3*nSurf0, confirmed from the disassembly at 0xa33xx. */
static void Blit_Indexed(i32 x, i32 y, const u8 *pGlyph, const u32 *pPal,
                         bool bKeyed, i32 nKey)
{
    u16       nGlyphW = (u16)pGlyph[0] | ((u16)pGlyph[1] << 8);
    u16       nGlyphH = (u16)pGlyph[2] | ((u16)pGlyph[3] << 8);
    GlyphClip clip;
    const u8 *pIdx;
    i32       y2;

    if (nGlyphW == 0 || nGlyphH == 0) {
        return;
    }
    pIdx = Blit_ClipGlyph(&x, &y, pGlyph + 4, nGlyphW, nGlyphH, &clip);
    if (pIdx == NULL) {
        return;
    }

    for (y2 = 0; y2 < clip.nH; ++y2) {
        i32 x2;

        if (g_blitState.nSurf2 == 0) {
            u8 *pDst = (u8 *)g_blitState.pSrc + clip.nStride * (y + y2) + x;

            for (x2 = 0; x2 < clip.nW; ++x2) {
                u8 idx = *pIdx++;

                if (!bKeyed || (i32)idx != nKey) {
                    *pDst = idx;
                }
                ++pDst;
            }
        } else if (g_blitState.nSurf2 == 1 || g_blitState.nSurf2 == 2) {
            u16 *pDst = (u16 *)((u8 *)g_blitState.pSrc
                                + 2 * (clip.nStride * (y + y2) + x));

            for (x2 = 0; x2 < clip.nW; ++x2) {
                u8 idx = *pIdx++;

                if (!bKeyed || (i32)idx != nKey) {
                    *pDst = (u16)pPal[idx];
                }
                ++pDst;
            }
        } else {
            u8 *pDst = (u8 *)g_blitState.pSrc + 3 * (clip.nStride * (y + y2) + x);

            for (x2 = 0; x2 < clip.nW; ++x2) {
                u8         idx = *pIdx++;
                const u8  *pEntry = (const u8 *)&pPal[idx];

                if (!bKeyed || (i32)idx != nKey) {
                    pDst[0] = pEntry[0];
                    pDst[1] = pEntry[1];
                    pDst[2] = pEntry[2];
                }
                pDst += 3;
            }
        }
        pIdx += clip.nPadX;
    }
}

/* SoftSurf_Blit_Inner @ 0xa47d0: constant-alpha blend, only defined for a 16bpp
 * surface (nSurf2 == 2). Source weight is dword_A3040; each 5-6-5 channel is
 * blended separately then repacked. Dest uses (255 - alpha). */
static void Blit_Blend(i32 x, i32 y, const u8 *pGlyph, const u32 *pPal)
{
    u16       nGlyphW = (u16)pGlyph[0] | ((u16)pGlyph[1] << 8);
    u16       nGlyphH = (u16)pGlyph[2] | ((u16)pGlyph[3] << 8);
    GlyphClip clip;
    const u8 *pIdx;
    u32       nAlpha;
    u32       nInv;
    i32       y2;

    if (g_blitState.nSurf2 != 2 || nGlyphW == 0 || nGlyphH == 0) {
        return;
    }
    pIdx = Blit_ClipGlyph(&x, &y, pGlyph + 4, nGlyphW, nGlyphH, &clip);
    if (pIdx == NULL) {
        return;
    }
    nAlpha = (u32)s_nBlendAlpha;
    nInv   = 255u - nAlpha;

    for (y2 = 0; y2 < clip.nH; ++y2) {
        u16 *pDst = (u16 *)((u8 *)g_blitState.pSrc
                            + 2 * (clip.nStride * (y + y2) + x));
        i32  x2;

        for (x2 = 0; x2 < clip.nW; ++x2) {
            u32 nSrc = pPal[*pIdx++];
            u32 nDst = *pDst;
            u32 nR = (((((nDst & 0xF800u) >> 11) * nInv) >> 8)
                      + ((((nSrc & 0xF800u) >> 11) * nAlpha) >> 8)) & 0x1Fu;
            u32 nG = (((((nDst & 0x07E0u) >> 5) * nInv) >> 8)
                      + ((((nSrc & 0x07E0u) >> 5) * nAlpha) >> 8)) & 0x3Fu;
            u32 nB = ((((nDst & 0x001Fu) * nInv) >> 8)
                      + (((nSrc & 0x001Fu) * nAlpha) >> 8)) & 0x1Fu;

            *pDst = (u16)((nR << 11) | (nG << 5) | nB);
            ++pDst;
        }
        pIdx += clip.nPadX;
    }
}

/* SoftSurf_BlitFlip_Inner @ 0xa49a0. The name is misleading: this is not a
 * mirror. It is the masked blend, where the per-pixel alpha comes from a second
 * byte array (dword_A303C) rather than a constant. Two details differ from
 * Blit_Inner and are reproduced exactly:
 *   - the destination weight is (256 - mask), not (255 - alpha)
 *   - the mask pointer is pre-offset by (maskBase + pIdx - glyphBase) and then
 *     advanced per visible pixel, so clipping shifts it with the glyph */
static void Blit_FlipBlend(i32 x, i32 y, const u8 *pGlyph, const u32 *pPal, const u8 *pMask)
{
    u16       nGlyphW = (u16)pGlyph[0] | ((u16)pGlyph[1] << 8);
    u16       nGlyphH = (u16)pGlyph[2] | ((u16)pGlyph[3] << 8);
    GlyphClip clip;
    const u8 *pIdx;
    const u8 *pMaskRow;
    i32       y2;

    if (g_blitState.nSurf2 != 2 || nGlyphW == 0 || nGlyphH == 0) {
        return;
    }
    pIdx = Blit_ClipGlyph(&x, &y, pGlyph + 4, nGlyphW, nGlyphH, &clip);
    if (pIdx == NULL) {
        return;
    }
    /* The original computes the mask row as `maskBase + pIdx - glyphBase`,
     * where pIdx is the clipped data pointer. Since the glyph record is
     * 4 header bytes + data, that is maskBase + 4 + shift: the mask buffer is
     * expected to carry the same 4-byte header. Reproduced exactly rather than
     * "corrected" to maskBase + shift. */
    pMaskRow = pMask + (pIdx - pGlyph);

    for (y2 = 0; y2 < clip.nH; ++y2) {
        u16 *pDst = (u16 *)((u8 *)g_blitState.pSrc
                            + 2 * (clip.nStride * (y + y2) + x));
        i32  x2;

        for (x2 = 0; x2 < clip.nW; ++x2) {
            u32 nMask = *pMaskRow++;
            u32 nSrc  = pPal[*pIdx++];
            u32 nDst  = *pDst;
            u32 nInv  = 256u - nMask;
            u32 nR = (((((nDst & 0xF800u) >> 11) * nInv) >> 8)
                      + ((((nSrc & 0xF800u) >> 11) * nMask) >> 8)) & 0x1Fu;
            u32 nG = (((((nDst & 0x07E0u) >> 5) * nInv) >> 8)
                      + ((((nSrc & 0x07E0u) >> 5) * nMask) >> 8)) & 0x3Fu;
            u32 nB = ((((nDst & 0x001Fu) * nInv) >> 8)
                      + (((nSrc & 0x001Fu) * nMask) >> 8)) & 0x1Fu;

            *pDst = (u16)((nR << 11) | (nG << 5) | nB);
            ++pDst;
        }
        pMaskRow += clip.nPadX;
        pIdx     += clip.nPadX;
    }
}

/* SoftSurf_BlitIndexedEx @ 0x9fadc. */
void SoftSurf_BlitIndexedEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph, i32 nPalSlot)
{
    Blit_LoadSurface(pSurf);
    Blit_Indexed(x, y, pGlyph, g_aPalette[nPalSlot & (PUFO_PAL_SLOTS - 1)], false, 0);
}

/* SoftSurf_BlitIndexedKeyedEx @ 0x9fb0c. The key travels through blitColorKey, not as an
 * argument: the original calls Blit_SetColorKey and the Inner reads the global. */
void SoftSurf_BlitIndexedKeyedEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph,
                                 i32 nPalSlot, u8 nKey)
{
    Blit_LoadSurface(pSurf);
    Blit_SetColorKey(nKey);
    Blit_Indexed(x, y, pGlyph, g_aPalette[nPalSlot & (PUFO_PAL_SLOTS - 1)], true, s_nColorKey);
}

void SoftSurf_BlitEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph,
                     i32 nPalSlot, i32 nAlpha)
{
    Blit_LoadSurface(pSurf);
    BlitParams_Set(nAlpha, 0);
    Blit_Blend(x, y, pGlyph, g_aPalette[nPalSlot & (PUFO_PAL_SLOTS - 1)]);
}

void SoftSurf_BlitFlipEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph,
                         const u32 *pPal, const u8 *pMask)
{
    Blit_LoadSurface(pSurf);
    Blit_FlipBlend(x, y, pGlyph, pPal, pMask);
}

/* SoftSurf_FillRect_Inner @ 0xa3f94. The size is not an argument: it comes from
 * blitRectW / blitRectH, which SoftSurf_FillRectEx publishes. An 8bpp surface is filled
 * with the raw index byte, a 16bpp surface with the palette entry. A surface mode that is
 * neither 0 nor 1 nor 2 draws nothing. */
void SoftSurf_FillRect_Inner(i32 x, i32 y, i32 nIdx, const u32 *pPal)
{
    i32 nFillW, nFillH;
    i32 nRow, nCol;

    if (!(g_blitState.nSurf2 == 0 || g_blitState.nSurf2 == 1 || g_blitState.nSurf2 == 2)) {
        return;
    }
    if (!Blit_ClipFillRect(&x, &y, &nFillW, &nFillH)) {
        return;
    }

    if (g_blitState.nSurf2 == 0) {
        u8 *pRow = (u8 *)g_blitState.pSrc + g_blitState.nSurf0 * y + x;

        for (nRow = nFillH; nRow != 0; --nRow) {
            u8 *pStart = pRow;
            for (nCol = nFillW; nCol != 0; --nCol) {
                *pRow++ = (u8)nIdx;
            }
            pRow = pStart + g_blitState.nSurf0;
        }
        return;
    }

    {
        u16  nColor = (u16)pPal[nIdx];
        u16 *pRow   = (u16 *)g_blitState.pSrc + g_blitState.nSurf0 * y + x;

        for (nRow = nFillH; nRow != 0; --nRow) {
            u16 *pStart = pRow;
            for (nCol = nFillW; nCol != 0; --nCol) {
                *pRow++ = nColor;
            }
            pRow = pStart + g_blitState.nSurf0;
        }
    }
}

/* SoftSurf_FillRectEx @ 0x9fbbc. Publishes the rectangle with both colour operands zero,
 * which is what makes the fill a flat palette entry rather than a packed colour. */
void SoftSurf_FillRectEx(const SoftSurf *pSurf, i32 x, i32 y, i32 nRectW, i32 nRectH,
                         i32 nPalSlot, i32 nIdx)
{
    Blit_LoadSurface(pSurf);
    Blit_SetRectAndColor(nRectW, nRectH, 0, 0);
    SoftSurf_FillRect_Inner(x, y, nIdx, g_aPalette[nPalSlot & (PUFO_PAL_SLOTS - 1)]);
}

/* Palette_Load @ 0x3e47c. Reads 768 bytes of RGB triples and converts to the
 * format selected by g_ffFormat. */
void Palette_Load(const char *pszName, u8 nSlot)
{
    AssetReader rdr;
    u8          aBuf[768];
    i32         i;

    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, pszName)) {
        return;
    }
    if (!AssetReader_Read(&rdr, aBuf, sizeof(aBuf))) {
        return;
    }

    for (i = 0; i < 256; ++i) {
        const u8 *pTriple = &aBuf[3 * i];
        u32       nEntry;

        if ((g_ffFormat & PUFO_FF_555) != 0) {
            nEntry = ((u32)(pTriple[0] >> 3) << 10)
                   | ((u32)(pTriple[1] >> 3) << 5)
                   | (u32)(pTriple[2] >> 3);
        } else if ((g_ffFormat & PUFO_FF_565) != 0) {
            nEntry = ((u32)(pTriple[0] >> 3) << 11)
                   | ((u32)(pTriple[1] >> 2) << 5)
                   | (u32)(pTriple[2] >> 3);
        } else if ((g_ffFormat & PUFO_FF_888) != 0) {
            nEntry = (u32)pTriple[0] | ((u32)pTriple[1] << 8) | ((u32)pTriple[2] << 16);
        } else {
            continue;
        }
        g_aPalette[nSlot & (PUFO_PAL_SLOTS - 1)][i] = nEntry;
    }
}

/* Soft-float conversions, reproduced rather than simplified.
 *
 * The original calls the WinCE runtime helpers through pointer tables: _itod, _muld, _addd, _divd,
 * _dtou (0xa6078) and _dtoi (0xa602c). dtou on a NEGATIVE double is the interesting one: it truncates
 * toward zero and the result is reinterpreted as unsigned, so dtou(-x) is (-x) mod 2^32. That is not a
 * guess - Tact_LoadPalettes writes slot 34 over the top of the slot it reads from, and at i = 0 the
 * scale is exactly 0.5, so the pass has to be the identity or it would destroy tactical.pal. Only the
 * wrapping reading makes it the identity. */
static u32 Pufo_dtou(double d)
{
    return (u32)(i32)d;
}

static i32 Pufo_dtoi(double d)
{
    return (i32)d;
}

/* Tact_BuildPaletteLuts @ 0x3734c.
 *
 * Walks 57 palette slots and 256 entries each, in place. For every entry it takes the low u16 of the
 * 4-byte slot, splits it into the channels the display format says, divides each channel by
 * 0.1 * gamma + 0.7, clamps to the channel width, and repacks.
 *
 * The divisor comes from soft-float constants 0.1 and 0.7 (ARM at 0x373CC loads lo 0x9A6C04DA /
 * hi 0x3FB1EB85, and 0x37408 loads 0x9A6C04DA / 0x3F81EB85 - unpacked those are exactly 0.1 and 0.7),
 * applied to unk_128AB8, which the five MenuBtn_SetGamma handlers write as 0..4.
 *
 * One thing to get right: the format word is NOT the GX format at 0x188398. ARM at 0x37350 loads
 * [R0,#0x14], so the operand is the dword at 0x1883AC - the same one Gfx_PackRgb565Pair and
 * Palette_Load use. Hex-Rays renders it as MEMORY[0x188398].ffFormat, which is the wrong member.
 *
 * The repack looks like it can overflow a signed short - 32 * (31 << 6 | 63) is 65504 - and it does,
 * but the wrap is the answer: the original keeps only 16 bits anyway, so computing in u32 and masking
 * gives the same value. */
void Tact_BuildPaletteLuts(void)
{
    u32 ff = g_ffFormat;
    i32 nGamma = Gamma_Current();   /* 0x37368: one read of Map_nGamma before the loops */
    i32 nSlot;
    i32 i;

    for (nSlot = 0; nSlot < PUFO_PAL_LUT_SLOTS; ++nSlot) {
        for (i = 0; i < PUFO_PAL_SIZE; ++i) {
            u16 w = (u16)g_aPalette[nSlot][i];
            double dDiv = (double)nGamma * 0.1 + 0.7;
            u32 out;

            if ((ff & PUFO_FF_555) != 0) {
                i32 nHi = Pufo_dtoi((double)((w & 0x7C00) >> 10) / dDiv);
                i32 nMid = Pufo_dtoi((double)((w & 0x3E0) >> 5) / dDiv);
                i32 nLo = Pufo_dtoi((double)(w & 0x1F) / dDiv);
                if (nHi > 31) { nHi = 31; }
                if (nMid > 31) { nMid = 31; }
                if (nLo > 31) { nLo = 31; }
                out = ((u32)(32 * (32 * nHi + nMid)) + (u32)nLo) & 0xFFFFu;
            } else if ((ff & PUFO_FF_565) != 0) {
                i32 nHi = Pufo_dtoi((double)(w >> 11) / dDiv);
                i32 nMid = Pufo_dtoi((double)((w & 0x7E0) >> 5) / dDiv);
                i32 nLo = Pufo_dtoi((double)(w & 0x1F) / dDiv);
                if (nHi > 31) { nHi = 31; }
                if (nMid > 63) { nMid = 63; }
                if (nLo > 31) { nLo = 31; }
                out = ((u32)(32 * (((u32)nHi << 6) + (u32)nMid)) + (u32)nLo) & 0xFFFFu;
            } else if ((ff & PUFO_FF_888) != 0) {
                /* ORIGINAL BUG (0x37718), reproduced: this branch reads HIBYTE(w) and (u8)w, so it only
                 * ever sees the low 16 bits of the entry, and it writes back a 16-bit value. Palette_Load
                 * packs 888 as R | G<<8 | B<<16, so on a 24-bit display this pass drops the whole blue
                 * channel from every palette. Harmless on the 16bpp devices the game shipped on. */
                i32 nHi = Pufo_dtoi((double)(w >> 8) / dDiv);
                i32 nLo = Pufo_dtoi((double)(w & 0xFF) / dDiv);
                if (nHi > 255) { nHi = 255; }
                if (nLo > 255) { nLo = 255; }
                out = (((u32)nHi << 8) + (u32)nLo) & 0xFFFFu;
            } else {
                /* No format bit set: the entry is stored back unchanged. */
                out = w;
            }

            g_aPalette[nSlot][i] = out;
        }
    }
}

/* Tact_LoadPalettes @ 0x377ec.
 *
 * Eight named palettes, then a 20-slot brightness ramp derived from tactical.pal, then the gamma pass.
 *
 * The ramp is the part that looks complicated and is not. For each i in 0..19 the original builds
 * scale = 1 / (0.07 * i * i + 2.0), splits every source colour into its three 5-bit fields, multiplies
 * two of them by NEGATIVE scale and one by positive scale, converts each with dtou, then recombines as
 * lo - 32 * (hi + 64 * mid). The two negatives cancel against the subtraction, so the whole inner loop
 * is per-field scaling of the colour by 2 * scale:
 *
 *     out = trunc(2*lo*scale) + 32*ceil(2*hi*scale) + 2048*ceil(2*mid*scale)   (mod 2^16)
 *
 * It is per-field, not per-colour: each 5-bit field is rounded on its own, so no carry crosses a field
 * boundary. That is a real difference from trunc(w * 2 * scale) and the port reproduces the original's
 * operations rather than the algebra, so the rounding matches.
 *
 * Slot 34 is both the source and the destination at i = 0, where scale is exactly 0.5 and the pass is
 * the identity. Slot 53 (i = 19) is forced to zero, so the ramp ends in black. */
void Tact_LoadPalettes(void)
{
    static const struct {
        const char *szName;
        u8          nSlot;
    } aPalettes[8] = {
        { "tactical.pal", 0x22 },
        { "menu.pal",     0x01 },
        { "enemystp.pal", 0x09 },
        { "endmiss.pal",  0x0B },
        { "twonone.pal",  0x0F },
        { "scanner.pal",  0x10 },
        { "class.pal",    0x12 },
        { "soldier.pal",  0x08 },
    };
    i32 i;
    i32 j;

    for (i = 0; i < 8; ++i) {
        Palette_Load(aPalettes[i].szName, aPalettes[i].nSlot);
    }

    for (i = 0; i < PUFO_PAL_RAMP_COUNT; ++i) {
        /* ARM builds these as divd(-1.0, v) and divd(1.0, v) in two calls. IEEE division is sign
         * symmetric, so one magnitude and a negation is the same pair of doubles. */
        double dScale = 1.0 / (0.07 * (double)(i * i) + 2.0);
        double dNeg = -dScale;
        u32 *pSrc = g_aPalette[PUFO_PAL_TACTICAL];
        u32 *pDst = g_aPalette[PUFO_PAL_TACTICAL + i];

        for (j = 0; j < PUFO_PAL_SIZE; ++j) {
            u16 w = (u16)pSrc[j];
            u32 nHi = Pufo_dtou((double)((w & 0x7E0) >> 5) * dNeg * 2.0);
            u32 nMid = Pufo_dtou((double)(w >> 11) * dNeg * 2.0);
            u32 nLo = Pufo_dtou((double)(w & 0x1F) * dScale * 2.0);
            u32 out = (nLo - ((nHi + (nMid << 6)) << 5)) & 0xFFFFu;

            if (i == 19) {
                out = 0;                                  /* 0x37A58: MOVEQ R0, #0 */
            }
            pDst[j] = out;
        }
    }

    Tact_BuildPaletteLuts();   /* 0x37A98: the tail call, no argument */
}

/* PadImage_ctor @ 0x11000. The original clears only pData. */
void PadImage_ctor(PadImage *pPad)
{
    pPad->pData = NULL;
}

/* PadImage_FreeData @ 0x1100c. Frees pData; the original does not null it, the
 * port does for idempotence. */
void PadImage_FreeData(PadImage *pPad)
{
    free(pPad->pData);
    pPad->pData = NULL;
}

/* PadImage_Load @ 0x11020. Appends ".pad" to pszName and reads that member. */
bool PadImage_Load(PadImage *pPad, const char *pszName, u8 nMode)
{
    AssetReader rdr;
    char        szFull[64];

    AssetReader_Clear(&rdr);
    PadImage_FreeData(pPad);

    if (strlen(pszName) + 5u > sizeof(szFull)) {
        return false;
    }
    pufo_strcpy(szFull, pszName);
    if (!pufo_strcat(szFull, ".pad", sizeof(szFull))) {
        return false;
    }

    if (!AssetReader_Find(&rdr, szFull)) {
        return false;
    }
    {
        u32 cb = AssetReader_Size(&rdr);

        if (cb < 4) {
            return false;
        }
        pPad->pData = malloc(cb);
        if (pPad->pData == NULL) {
            return false;
        }
        if (!AssetReader_Read(&rdr, pPad->pData, cb)) {
            PadImage_FreeData(pPad);
            return false;
        }
    }

    /* The glyph record begins with the u16 dimensions, little-endian (ARM). */
    pPad->uWidth  = (u16)pPad->pData[0] | ((u16)pPad->pData[1] << 8);
    pPad->uHeight = (u16)pPad->pData[2] | ((u16)pPad->pData[3] << 8);
    pPad->nMode   = nMode;
    return true;
}

/* Non-Ex wrappers, matching the original's use of the global surface. */
void SoftSurf_BlitIndexed(i32 x, i32 y, const u8 *pGlyph, i32 nPalSlot)
{
    SoftSurf_BlitIndexedEx(g_pMainSurface, x, y, pGlyph, nPalSlot);
}

void SoftSurf_BlitIndexedKeyed(i32 x, i32 y, const u8 *pGlyph, i32 nPalSlot, u8 nKey)
{
    SoftSurf_BlitIndexedKeyedEx(g_pMainSurface, x, y, pGlyph, nPalSlot, nKey);
}

void SoftSurf_Blit(i32 x, i32 y, const u8 *pGlyph, i32 nPalSlot, i32 nAlpha)
{
    SoftSurf_BlitEx(g_pMainSurface, x, y, pGlyph, nPalSlot, nAlpha);
}

void SoftSurf_BlitFlip(i32 x, i32 y, const u8 *pGlyph, const u32 *pPal, const u8 *pMask)
{
    SoftSurf_BlitFlipEx(g_pMainSurface, x, y, pGlyph, pPal, pMask);
}

void SoftSurf_FillRect(i32 x, i32 y, i32 nW, i32 nH, i32 nPalSlot, i32 nIdx)
{
    SoftSurf_FillRectEx(g_pMainSurface, x, y, nW, nH, nPalSlot, nIdx);
}

/* PadImage_Blit @ 0x110dc. */
void PadImage_Blit(const PadImage *pPad)
{
    SoftSurf_BlitIndexed(pPad->nOriginX, pPad->nOriginY, pPad->pData, pPad->nMode);
}

/* PadImage_BlitEx @ 0x11110. The keyed form of the same blit: the caller supplies the colour key
 * (0xFF for the unit markers). The original also passes uWidth and uHeight into
 * SoftSurf_BlitIndexedKeyed @ 0x9ea34, which takes five arguments and reads the size from the
 * glyph header, so those two are dead arguments. Reproduced as the five the callee uses. */
void PadImage_BlitEx(const PadImage *pPad, u8 nKey)
{
    SoftSurf_BlitIndexedKeyed(pPad->nOriginX, pPad->nOriginY, pPad->pData, pPad->nMode, nKey);
}
