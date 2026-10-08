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

/* gxDisp.ffFormat @ 0x188398+0x14. The Win64 target is RGB565. */
u32 g_ffFormat = PUFO_FF_565;

/* Blitter scratch, matching dword_A3028..dword_A3040 @ 0xa3028..0xa3040. */
static i32 s_nSrcRectW;   /* dword_A3034 */
static i32 s_nSrcRectH;   /* dword_A3038 */
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

void Blit_SetSrcRect(i32 nW, i32 nH)
{
    s_nSrcRectW = nW;
    s_nSrcRectH = nH;
}

/* Blit_CopyRows @ 0xa3a14: logical surface -> render target, 1:1. */
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
    i32 x = *px;
    i32 y = *py;
    i32 nW = nGlyphW;
    i32 nH = nGlyphH;

    if (!(x <= g_blitState.nSurf0 && y <= g_blitState.nSurf1
          && x + nGlyphW >= 0 && y + nGlyphH >= 0)) {
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
    if (x + nGlyphW > g_blitState.nSurf0) {
        nW -= (x + nGlyphW) - g_blitState.nSurf0;
    }
    if (y + nGlyphH > g_blitState.nSurf1) {
        nH -= (y + nGlyphH) - g_blitState.nSurf1;
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

void SoftSurf_BlitIndexedEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph, i32 nPalSlot)
{
    Blit_LoadSurface(pSurf);
    Blit_Indexed(x, y, pGlyph, g_aPalette[nPalSlot & (PUFO_PAL_SLOTS - 1)], false, 0);
}

void SoftSurf_BlitIndexedKeyedEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph,
                                 i32 nPalSlot, u8 nKey)
{
    Blit_LoadSurface(pSurf);
    Blit_Indexed(x, y, pGlyph, g_aPalette[nPalSlot & (PUFO_PAL_SLOTS - 1)], true, (i32)nKey);
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

/* SoftSurf_FillRect_Inner @ 0xa3f94. Fills w x h with one palette entry. The
 * original takes the size from the source-rect globals that SoftSurf_FillRectEx
 * sets; taking them directly is equivalent and avoids the global hop. */
void SoftSurf_FillRectEx(const SoftSurf *pSurf, i32 x, i32 y, i32 nW, i32 nH,
                         i32 nPalSlot, i32 nIdx)
{
    i32 nFillW = nW;
    i32 nFillH = nH;
    i32 y2;

    if (nFillW < 0) {
        nFillW = 0;
    }
    if (nFillH < 0) {
        nFillH = 0;
    }

    Blit_LoadSurface(pSurf);

    if (x > g_blitState.nSurf0 || y > g_blitState.nSurf1
        || x + nFillW < 0 || y + nFillH < 0) {
        return;
    }
    if (x < 0) {
        nFillW += x;
        x       = 0;
    }
    if (y < 0) {
        nFillH += y;
        y       = 0;
    }
    if (x + nFillW > g_blitState.nSurf0) {
        nFillW -= (x + nFillW) - g_blitState.nSurf0;
    }
    if (y + nFillH > g_blitState.nSurf1) {
        nFillH -= (y + nFillH) - g_blitState.nSurf1;
    }
    if (nFillW <= 0 || nFillH <= 0) {
        return;
    }

    for (y2 = 0; y2 < nFillH; ++y2) {
        i32 x2;

        if (g_blitState.nSurf2 == 0) {
            u8 *pDst = (u8 *)g_blitState.pSrc + g_blitState.nSurf0 * (y + y2) + x;

            for (x2 = 0; x2 < nFillW; ++x2) {
                *pDst++ = (u8)nIdx;
            }
        } else {
            /* The original only implements the 16bpp branch here (nSurf2 1|2). */
            u16 *pDst   = (u16 *)((u8 *)g_blitState.pSrc
                                  + 2 * (g_blitState.nSurf0 * (y + y2) + x));
            u16  nColor = (u16)g_aPalette[nPalSlot & (PUFO_PAL_SLOTS - 1)][nIdx];

            for (x2 = 0; x2 < nFillW; ++x2) {
                *pDst++ = nColor;
            }
        }
    }
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
