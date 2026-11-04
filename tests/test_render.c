/* test_render.c - checks the render layer against the shipped palettes.
 *
 * Expected values were read out of main.dat independently (see
 * docs/parity/render-audio.md), so a mismatch means the ported code is wrong.
 *
 * Exit code 0 = all checks pass, 1 = at least one failure.
 */
#include "asset/pufo_asset.h"
#include "render/pufo_render.h"

#include <stdio.h>
#include <string.h>

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static void CheckEqU32(u32 nGot, u32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (0x%x)\n", pszWhat, (unsigned)nGot);
    } else {
        printf("  FAIL %s: got 0x%x, want 0x%x\n", pszWhat, (unsigned)nGot, (unsigned)nWant);
        g_nFailures += 1;
    }
}

/* Surface allocation must produce the original's 4-byte header layout:
 * pAlloc holds u16 w, u16 h, and pPixels is pAlloc + 4. */
static void TestAlloc(void)
{
    SoftSurf surf;
    u16      anHdr[2];

    printf("SoftSurf_AllocPixels (240x320, mode 2)\n");
    SoftSurf_ctor(&surf);
    Check(SoftSurf_AllocPixels(&surf, 240, 320, PIXMODE_16BPP) != NULL, "allocated");
    CheckEqI32(SoftSurf_GetWidth(&surf), 240, "  width");
    CheckEqI32(SoftSurf_GetHeight(&surf), 320, "  height");
    CheckEqI32(surf.nBytesPP, 2, "  bytes per pixel");
    Check(surf.pPixels == surf.pAlloc + 4, "  pPixels == pAlloc + 4 (header prefix)");
    CheckEqI32(surf.nBlitW, 240, "  nBlitW mirrors width");
    CheckEqI32(surf.nBlitMode, PIXMODE_16BPP, "  nBlitMode mirrors pixel mode");

    memcpy(anHdr, surf.pAlloc, 4);
    CheckEqI32(anHdr[0], 240, "  header width");
    CheckEqI32(anHdr[1], 320, "  header height");

    /* A pixel mode outside 0..3 leaves nBytesPP at 0 in the original, which
     * then allocates a 4-byte buffer. Verify that quirk is preserved. */
    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 4, 4, 9);
    CheckEqI32(surf.nBytesPP, 0, "  out-of-range pixel mode leaves nBytesPP 0");
    SoftSurf_dtor(&surf);

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 240, 320, PIXMODE_16BPP);
    SoftSurf_dtor(&surf);
}

/* The SoftSurf object rows. SoftSurf_ctor @ 0x9f96c is three instructions: MOV R1,#0 then two
 * stores, [R0,#0x10] at 0x9F970 and [R0,#0x14] at 0x9F974 -- the two pointers and nothing else,
 * so every other field of the 40-byte record survives a ctor. The accessors are single loads at
 * fixed offsets: SoftSurf_GetWidth @ 0x9f994 is `LDR R0,[R0]` (+0x00), SoftSurf_GetHeight @ 0x9f99c
 * is `LDR R0,[R0,#4]` (+0x04), SoftSurf_GetPixels @ 0x9fcb4 is `LDR R0,[R0,#0x14]` (+0x14). The
 * record ALSO holds a blit descriptor at +0x18..+0x24, which is what the blitters read, so each
 * accessor is tested against a descriptor holding a different value. */
static void TestSoftSurfObject(void)
{
    SoftSurf surf;
    u8      *pPixels;

    printf("SoftSurf ctor and accessors\n");

    memset(&surf, 0x5A, sizeof(surf));    /* garbage everywhere, no live pointer anywhere */
    SoftSurf_ctor(&surf);
    Check(surf.pAlloc == NULL, "  ctor clears pAlloc (+0x10, STR at 0x9F970)");
    Check(surf.pPixels == NULL, "  ctor clears pPixels (+0x14, STR at 0x9F974)");
    CheckEqI32(surf.nBytesPP, 0x5A5A5A5A, "  ctor reaches no third field (only two STR)");
    CheckEqI32(surf.nWidth, 0x5A5A5A5A, "  ... not the header width at +0x00 either");
    CheckEqI32(surf.nHeight, 0x5A5A5A5A, "  ... not the header height at +0x04");
    CheckEqI32(surf.nBlitW, 0x5A5A5A5A, "  the +0x18 descriptor survives it");

    SoftSurf_AllocPixels(&surf, 3, 2, PIXMODE_16BPP);
    pPixels = surf.pPixels;
    surf.nBlitW = 11;                          /* the descriptor's width, a different field */
    CheckEqI32(SoftSurf_GetWidth(&surf), 3, "  GetWidth reads +0x00 (0x9F994), not +0x18");
    surf.nHeight = 5;
    surf.nBlitH  = 7;
    CheckEqI32(SoftSurf_GetHeight(&surf), 5, "  GetHeight reads +0x04 (0x9F99C), not +0x1C");
    surf.pBlitSrc = (u8 *)&surf + 8;           /* a third pointer, deliberately different */
    Check(SoftSurf_GetPixels(&surf) == pPixels, "  GetPixels reads +0x14 (0x9FCB4)");
    Check(SoftSurf_GetPixels(&surf) != surf.pBlitSrc, "  ... and not the blit source at +0x24");

    /* SoftSurf_dtor @ 0x9f97c: `LDR R3,[R0,#0x14]` at 0x9F980, `CMP R3,#0` at 0x9F984, and both
     * the pointer load (0x9F988) and the delete (0x9F98C) are BLNE -- the free is gated on the
     * pixel pointer, not on the allocation pointer, and the original clears neither field, so
     * running it twice would delete the same block twice. The port nulls both (stated deviation),
     * which is what a second dtor pins here: it must be a no-op, not a second free. */
    SoftSurf_dtor(&surf);
    Check(surf.pPixels == NULL && surf.pAlloc == NULL,
          "  dtor nulls both (deviation: 0x9f97c leaves them dangling)");
    SoftSurf_dtor(&surf);
    Check(surf.pPixels == NULL && surf.pAlloc == NULL, "  a second dtor changes nothing");

    /* The gate in the other direction is not observable from C: "frees nothing when pPixels is 0"
     * cannot be told apart from "calls free(NULL)" without a heap checker. It is recorded on the
     * IDB note for 0x9f97c instead of claimed as a check. */
    SoftSurf_ctor(&surf);
    SoftSurf_dtor(&surf);
    Check(surf.pPixels == NULL, "  dtor after a bare ctor leaves the pair clear");
}

/* Blit_LoadSurface @ 0xa3964: four LDR/STR pairs, 0xA3970..0xA39A4, that copy the surface's
 * +0x18 descriptor into the first four fields of the global blit state -- nSurf0 from +0x18,
 * nSurf1 from +0x1C, nSurf2 from +0x20 and pSrc from +0x24. It writes nothing else: the target
 * half of the state (+0x10..+0x24, published by Render_SetTarget) is left alone, and the copy
 * comes from the DESCRIPTOR, not from the surface's own header fields. */
static void TestBlitStateLoading(void)
{
    SoftSurf surf;
    static u8 aScratch[16];

    printf("Blit_LoadSurface (0xa3964)\n");

    memset(&surf, 0, sizeof(surf));
    surf.nBlitW = 31; surf.nBlitH = 17; surf.nBlitMode = 2; surf.pBlitSrc = aScratch;
    /* A header that disagrees with the descriptor: a loader reading the wrong half shows up. */
    surf.nWidth = 1; surf.nHeight = 1; surf.pPixels = aScratch + 8;

    g_blitState.nSurf0 = -1; g_blitState.nSurf1 = -1; g_blitState.nSurf2 = -1;
    g_blitState.pSrc = NULL;
    g_blitState.nWidth = 100; g_blitState.nHeight = 200; g_blitState.pDst = aScratch;

    Blit_LoadSurface(&surf);
    CheckEqI32(g_blitState.nSurf0, 31, "  nSurf0 <- +0x18 (STR at 0xA3974)");
    CheckEqI32(g_blitState.nSurf1, 17, "  nSurf1 <- +0x1C (0xA3984)");
    CheckEqI32(g_blitState.nSurf2, 2,  "  nSurf2 <- +0x20 (0xA3994)");
    Check(g_blitState.pSrc == aScratch, "  pSrc <- +0x24 (0xA39A4)");
    Check(g_blitState.pSrc != surf.pPixels, "  the descriptor is copied, not the header pixel base");
    CheckEqI32(g_blitState.nWidth, 100, "  0xa3964 writes four fields only: width untouched");
    CheckEqI32(g_blitState.nHeight, 200, "  height untouched");
    Check(g_blitState.pDst == aScratch, "  pDst untouched");
}

/* Palette conversion from the real CLASS.PAL (768 bytes, RGB triples). */
static void TestPalette(void)
{
    printf("Palette_Load(CLASS.PAL) -> RGB565\n");
    memset(g_aPalette, 0, sizeof(g_aPalette));
    g_ffFormat = PUFO_FF_565;
    Palette_Load("class.pal", 18);
    /* main.dat CLASS.PAL entry 0 is rgb(4,8,7) -> 0x0040, entry 255 is white. */
    CheckEqU32(g_aPalette[18][0], 0x0040, "  entry 0");
    CheckEqU32(g_aPalette[18][255], 0xFFFF, "  entry 255");
    CheckEqU32(g_aPalette[18][1], 0x32E9, "  entry 1");

    printf("Palette_Load(CLASS.PAL) -> RGB555\n");
    memset(g_aPalette, 0, sizeof(g_aPalette));
    g_ffFormat = PUFO_FF_555;
    Palette_Load("class.pal", 0);
    CheckEqU32(g_aPalette[0][0], 0x0020, "  entry 0");
    CheckEqU32(g_aPalette[0][255], 0x7FFF, "  entry 255");

    printf("Palette_Load(CLASS.PAL) -> BGR888\n");
    memset(g_aPalette, 0, sizeof(g_aPalette));
    g_ffFormat = PUFO_FF_888;
    Palette_Load("class.pal", 0);
    CheckEqU32(g_aPalette[0][0], (u32)4 | (8u << 8) | (7u << 16), "  entry 0");
    CheckEqU32(g_aPalette[0][255], 0x00FFFFFFu, "  entry 255");

    printf("Palette_Load(missing name)\n");
    g_ffFormat = PUFO_FF_565;
    memset(g_aPalette, 0xAA, sizeof(g_aPalette));
    Palette_Load("NO_SUCH.PAL", 0);
    CheckEqU32(g_aPalette[0][0], 0xAAAAAAAAu, "  untouched on miss");
}

/* Indexed glyph blit, with clipping. Uses a hand-built 4x2 glyph so the
 * expected pixels are obvious. */
static void TestBlitIndexed(void)
{
    SoftSurf surf;
    u16     *pPix;
    u8       aGlyph[4 + 8];
    i32      i;

    printf("SoftSurf_BlitIndexedEx (16bpp, 8x4 surface)\n");
    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 8, 4, PIXMODE_16BPP);
    pPix = (u16 *)SoftSurf_GetPixels(&surf);
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }

    /* 4x2 glyph, indices 1,2,3,4 / 5,6,7,8 */
    aGlyph[0] = 4; aGlyph[1] = 0; /* width  */
    aGlyph[2] = 2; aGlyph[3] = 0; /* height */
    for (i = 0; i < 8; ++i) {
        aGlyph[4 + i] = (u8)(i + 1);
    }
    for (i = 0; i < 256; ++i) {
        g_aPalette[3][i] = (u32)(0x1000 + i);
    }

    SoftSurf_BlitIndexedEx(&surf, 1, 1, aGlyph, 3);
    CheckEqU32(pPix[1 * 8 + 1], 0x1001, "  top-left pixel");
    CheckEqU32(pPix[1 * 8 + 4], 0x1004, "  top-right pixel");
    CheckEqU32(pPix[2 * 8 + 1], 0x1005, "  bottom-left pixel");
    CheckEqU32(pPix[0], 0, "  outside glyph untouched");

    /* Clip on the left/top edge: the glyph is drawn at (-2,-1). */
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    SoftSurf_BlitIndexedEx(&surf, -2, -1, aGlyph, 3);
    /* Visible source pixels start at glyph x=2,y=1 -> index 7 (row 1, col 2). */
    CheckEqU32(pPix[0], 0x1007, "  clipped top-left");
    CheckEqU32(pPix[1], 0x1008, "  clipped next pixel");
    CheckEqU32(pPix[8], 0, "  row 1 untouched");

    /* Clip on the right: at x=6 only 2 of 4 columns fit. */
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    SoftSurf_BlitIndexedEx(&surf, 6, 0, aGlyph, 3);
    CheckEqU32(pPix[6], 0x1001, "  right-clip column 0");
    CheckEqU32(pPix[7], 0x1002, "  right-clip column 1");

    /* Fully off-surface draws nothing and must not fault. */
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    SoftSurf_BlitIndexedEx(&surf, 20, 20, aGlyph, 3);
    SoftSurf_BlitIndexedEx(&surf, -50, 0, aGlyph, 3);
    CheckEqU32(pPix[0], 0, "  off-surface draw is a no-op");

    SoftSurf_dtor(&surf);
}

/* Colour-keyed blit: index equal to the key is skipped. */
static void TestBlitKeyed(void)
{
    SoftSurf surf;
    u16     *pPix;
    u8       aGlyph[4 + 4];

    printf("SoftSurf_BlitIndexedKeyedEx\n");
    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 4, 1, PIXMODE_16BPP);
    pPix = (u16 *)SoftSurf_GetPixels(&surf);
    pPix[0] = 0xDEAD; pPix[1] = 0xDEAD; pPix[2] = 0xDEAD; pPix[3] = 0xDEAD;

    aGlyph[0] = 4; aGlyph[1] = 0;
    aGlyph[2] = 1; aGlyph[3] = 0;
    aGlyph[4] = 9; aGlyph[5] = 9; aGlyph[6] = 9; aGlyph[7] = 7;
    g_aPalette[1][9] = 0x0999;
    g_aPalette[1][7] = 0x0777;

    SoftSurf_BlitIndexedKeyedEx(&surf, 0, 0, aGlyph, 1, 9);
    CheckEqU32(pPix[0], 0xDEAD, "  keyed pixel skipped");
    CheckEqU32(pPix[1], 0xDEAD, "  keyed pixel skipped (2)");
    CheckEqU32(pPix[2], 0xDEAD, "  keyed pixel skipped (3)");
    CheckEqU32(pPix[3], 0x0777, "  non-key pixel drawn");
    SoftSurf_dtor(&surf);
}

/* FillRect fills with one palette entry, clipped to the surface. */
static void TestFillRect(void)
{
    SoftSurf surf;
    u16     *pPix;
    i32      i;

    printf("SoftSurf_FillRectEx\n");
    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 8, 4, PIXMODE_16BPP);
    pPix = (u16 *)SoftSurf_GetPixels(&surf);
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    g_aPalette[2][5] = 0x0555;

    SoftSurf_FillRectEx(&surf, 1, 1, 3, 2, 2, 5);
    CheckEqU32(pPix[1 * 8 + 1], 0x0555, "  fill top-left");
    CheckEqU32(pPix[1 * 8 + 3], 0x0555, "  fill top-right");
    CheckEqU32(pPix[2 * 8 + 2], 0x0555, "  fill bottom");
    CheckEqU32(pPix[1 * 8 + 4], 0, "  just outside fill");
    CheckEqU32(pPix[0], 0, "  row 0 untouched");

    /* Oversized fill clips to the surface instead of overrunning. */
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    SoftSurf_FillRectEx(&surf, -4, -4, 100, 100, 2, 5);
    CheckEqU32(pPix[0], 0x0555, "  clipped fill covers origin");
    CheckEqU32(pPix[31], 0x0555, "  clipped fill covers last pixel");
    SoftSurf_dtor(&surf);
}

/* The full-surface row copy writes the logical surface to the target 1:1. */
static void TestCopyRows(void)
{
    SoftSurf surf;
    u16      aTarget[12];
    u16     *pPix;
    i32      i;

    printf("Render_BlitSurface (1:1 row copy)\n");
    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 4, 3, PIXMODE_16BPP);
    pPix = (u16 *)SoftSurf_GetPixels(&surf);
    for (i = 0; i < 12; ++i) {
        pPix[i] = (u16)(i + 1);
        aTarget[i] = 0;
    }

    /* Target pitch: 2 bytes per pixel, 4 pixels per row. */
    Render_SetTarget(4, 3, 2, 2, 8, (u8 *)aTarget);
    Render_BlitSurface(&surf);
    for (i = 0; i < 12; ++i) {
        if (aTarget[i] != (u16)(i + 1)) {
            printf("  FAIL copy mismatch at %d: got %u want %u\n", (int)i,
                   (unsigned)aTarget[i], (unsigned)(i + 1));
            g_nFailures += 1;
        }
    }
    CheckEqU32(aTarget[0], 1, "  row 0 first pixel");
    CheckEqU32(aTarget[11], 12, "  last pixel");
    CheckEqI32(g_blitState.nWidth, 4, "  blit state width from target");
    CheckEqI32(g_blitState.nPixelMode, 2, "  blit state pixel mode from target");

    SoftSurf_dtor(&surf);
}

/* SoftSurf_BlitRectEx / SoftSurf_BlitSrcRectEx / Blit_CopyToXY /
 * SoftSurf_BlitSrcRect_Inner: the rectangle fill and blend pair. */
static void TestRectFillAndBlend(void)
{
    SoftSurf surf;
    u16      aTarget[12];
    u16     *pPix;
    i32      i;

    printf("SoftSurf_BlitRect / SoftSurf_BlitSrcRect (rect fill and blend)\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 4, 3, PIXMODE_16BPP);
    pPix = (u16 *)SoftSurf_GetPixels(&surf);
    for (i = 0; i < 12; ++i) {
        pPix[i] = 0;
        aTarget[i] = 0;
    }
    Render_SetTarget(4, 3, 2, 2, 8, (u8 *)aTarget);

    /* All three operands at their maximum: idx 0xFF gives the top field, colorMid the
     * middle, colorLow the bottom, so the packed colour is 0xFFFF. */
    SoftSurf_BlitRectEx(&surf, 1, 1, 2, 2, 0xFF, 0xFF, 0xFF);
    CheckEqU32(pPix[1 * 4 + 1], 0xFFFF, "  (1,1) filled");
    CheckEqU32(pPix[1 * 4 + 2], 0xFFFF, "  (2,1) filled");
    CheckEqU32(pPix[2 * 4 + 1], 0xFFFF, "  (1,2) filled");
    CheckEqU32(pPix[2 * 4 + 2], 0xFFFF, "  (2,2) filled");
    CheckEqU32(pPix[0], 0, "  (0,0) outside the rect");
    CheckEqU32(pPix[3], 0, "  (3,0) outside the rect");

    /* The fill lands in the loaded SURFACE, not the render target. */
    for (i = 0; i < 12; ++i) {
        if (aTarget[i] != 0) {
            g_nFailures += 1;
            printf("  FAIL target pixel %d written: %u\n", (int)i, (unsigned)aTarget[i]);
        }
    }
    Check(1, "  render target untouched by Blit_CopyToXY");

    /* Colour really comes from the three operands, not from a constant:
     * idx 8 -> (8>>3)<<11 = 0x800, mid 4 -> 32*(4>>2) = 0x20, low 8 -> 8>>3 = 0x01. */
    SoftSurf_BlitRectEx(&surf, 0, 0, 1, 1, 0x08, 0x04, 0x08);
    CheckEqU32(pPix[0], 0x0821, "  idx 8 / mid 4 / low 8 packs to 0x0821");

    /* Negative origin: the guard is x <= nSurf0, and the size shrinks by the overflow. */
    for (i = 0; i < 12; ++i) { pPix[i] = 0; }
    SoftSurf_BlitRectEx(&surf, -1, -1, 4, 4, 0xFF, 0xFF, 0xFF);
    CheckEqU32(pPix[0], 0xFFFF, "  clipped fill starts at (0,0)");
    CheckEqU32(pPix[10], 0xFFFF, "  clipped fill ends at (2,2)");
    CheckEqU32(pPix[3], 0, "  column 3 left alone");
    CheckEqU32(pPix[11], 0, "  last pixel left alone");

    /* x == nSurf0 passes the guard but the width clips to zero, so nothing is drawn. */
    for (i = 0; i < 12; ++i) { pPix[i] = 0; }
    SoftSurf_BlitRectEx(&surf, 4, 0, 2, 2, 0xFF, 0xFF, 0xFF);
    CheckEqU32(pPix[0], 0, "  x == nSurf0 draws nothing");

    /* Pixel mode 0 (the 8bpp target) falls straight through. */
    pPix[1 * 4 + 1] = 0xFFFF;
    Render_SetTarget(4, 3, 0, 1, 4, (u8 *)aTarget);
    SoftSurf_BlitRectEx(&surf, 1, 1, 2, 2, 0xFF, 0xFF, 0xFF);
    CheckEqU32(pPix[1 * 4 + 1], 0xFFFF, "  mode 0 target leaves the surface as it was");
    Render_SetTarget(4, 3, 2, 2, 8, (u8 *)aTarget);

    /* The blend gate reads the SURFACE mode, not the target mode. An 8bpp surface has
     * nSurf2 == 0, so the blend does nothing even with a 16bpp target. */
    SoftSurf surf8;
    SoftSurf_ctor(&surf8);
    SoftSurf_AllocPixels(&surf8, 4, 3, PIXMODE_8BPP);
    SoftSurf_BlitSrcRectEx(&surf8, 0, 0, 2, 2, 0xFF, 0xFF, 0xFF, 255);
    CheckEqI32(g_blitState.nSurf2, 0, "  8bpp surface gives nSurf2 0");
    CheckEqI32(g_blitState.nPixelMode, 2, "  target mode is still 2");
    SoftSurf_dtor(&surf8);

    /* Blend arithmetic. dst 0xFFFF, colour 0xFFFF, either key:
     *   31 * 255 >> 8 = 30, 63 * 255 >> 8 = 62
     * so the result is 30 + 32*62 + (30<<11) = 0x1E + 0x7C0 + 0xF000 = 0xF7DE.
     * (f * 255) >> 8 is f - 1 for every non-zero field, so the original never
     * reproduces a field exactly even at a fully opaque key. Recorded, not corrected. */
    for (i = 0; i < 12; ++i) { pPix[i] = 0xFFFF; }
    SoftSurf_BlitSrcRectEx(&surf, 0, 0, 1, 1, 0xFF, 0xFF, 0xFF, 255);
    CheckEqU32(pPix[0], 0xF7DE, "  opaque blend still loses one per field");

    for (i = 0; i < 12; ++i) { pPix[i] = 0xFFFF; }
    SoftSurf_BlitSrcRectEx(&surf, 0, 0, 1, 1, 0xFF, 0xFF, 0xFF, 0);
    CheckEqU32(pPix[0], 0xF7DE, "  zero-key blend also loses one per field");

    for (i = 0; i < 12; ++i) { pPix[i] = 0x001F; }
    SoftSurf_BlitSrcRectEx(&surf, 0, 0, 1, 1, 0xFF, 0xFF, 0xFF, 0);
    CheckEqU32(pPix[0], 0x001E, "  nKey 0 scales the destination, it does not skip it");

    for (i = 0; i < 12; ++i) { pPix[i] = 0; }
    SoftSurf_BlitSrcRectEx(&surf, 0, 0, 1, 1, 0xFF, 0xFF, 0xFF, 0);
    CheckEqU32(pPix[0], 0, "  nKey 0 on an empty destination stays empty");

    /* The bound forms pin g_pMainSurface. */
    g_pMainSurface = &surf;
    for (i = 0; i < 12; ++i) { pPix[i] = 0; }
    SoftSurf_BlitRect(0, 0, 1, 1, 0xFF, 0xFF, 0xFF);
    CheckEqU32(pPix[0], 0xFFFF, "  SoftSurf_BlitRect uses g_pMainSurface");
    SoftSurf_BlitSrcRect(1, 1, 1, 1, 0xFF, 0xFF, 0xFF, 255);
    CheckEqU32(pPix[1 * 4 + 1], 0xF7DE, "  SoftSurf_BlitSrcRect uses g_pMainSurface");
    g_pMainSurface = NULL;

    CheckEqI32(Blit_SetColorKey(0x2A), 0x2A, "  Blit_SetColorKey returns its argument");

    SoftSurf_dtor(&surf);
}

/* SoftSurf_GetAlloc @ 0x9fcbc, BlitState_Get @ 0xa479c, SoftSurf_FillRect_Inner @ 0xa3f94,
 * SoftSurf_BlitRectProxy @ 0x2dce4. */
static void TestRectHelpers(void)
{
    SoftSurf surf;
    u16      aTarget[12];
    u16     *pPix;
    i32      i;

    printf("FillRect_Inner / GetAlloc / BlitState_Get / BlitRectProxy\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 4, 3, PIXMODE_16BPP);
    pPix = (u16 *)SoftSurf_GetPixels(&surf);
    for (i = 0; i < 12; ++i) { pPix[i] = 0; aTarget[i] = 0; }
    Render_SetTarget(4, 3, 2, 2, 8, (u8 *)aTarget);

    Check(surf.pPixels == (u8 *)SoftSurf_GetAlloc(&surf) + 4,
          "  GetAlloc returns the block four bytes below the pixels");

    Blit_LoadSurface(&surf);
    CheckEqI32(BlitState_Get(), 4, "  BlitState_Get returns the loaded surface width");

    /* The Inner takes its size from the published rectangle, not from arguments. */
    g_aPalette[1][5] = 0x1234;
    Blit_SetRectAndColor(2, 2, 0, 0);
    SoftSurf_FillRect_Inner(0, 0, 5, g_aPalette[1]);
    CheckEqU32(pPix[0], 0x1234, "  (0,0) from the published rect");
    CheckEqU32(pPix[1], 0x1234, "  (1,0)");
    CheckEqU32(pPix[4], 0x1234, "  (0,1)");
    CheckEqU32(pPix[5], 0x1234, "  (1,1)");
    CheckEqU32(pPix[2], 0, "  (2,0) outside the published rect");
    CheckEqU32(pPix[8], 0, "  (0,2) outside the published rect");

    /* A negative published rectangle clamps to zero and draws nothing. */
    for (i = 0; i < 12; ++i) { pPix[i] = 0; }
    Blit_SetRectAndColor(-4, -4, 0, 0);
    SoftSurf_FillRect_Inner(0, 0, 5, g_aPalette[1]);
    CheckEqU32(pPix[0], 0, "  negative rect draws nothing");

    /* 8bpp surface: the fill writes the raw index byte, not a palette entry. */
    {
        SoftSurf surf8;
        u8      *pBytes;

        SoftSurf_ctor(&surf8);
        SoftSurf_AllocPixels(&surf8, 4, 1, PIXMODE_8BPP);
        pBytes = SoftSurf_GetPixels(&surf8);
        pBytes[0] = 0; pBytes[1] = 0; pBytes[2] = 0; pBytes[3] = 0;
        Blit_LoadSurface(&surf8);
        Blit_SetRectAndColor(2, 1, 0, 0);
        SoftSurf_FillRect_Inner(0, 0, 0xAB, g_aPalette[1]);
        CheckEqI32(pBytes[0], 0xAB, "  8bpp fill writes the index byte");
        CheckEqI32(pBytes[1], 0xAB, "  8bpp fill covers the rect");
        CheckEqI32(pBytes[2], 0, "  8bpp fill stops at the rect width");
        SoftSurf_dtor(&surf8);
    }

    /* The proxy drops its first argument and widens the last three from bytes. It goes
     * through the bound fill, so it needs g_pMainSurface to be the test surface. */
    Render_SetTarget(4, 3, 2, 2, 8, (u8 *)aTarget);
    for (i = 0; i < 12; ++i) { pPix[i] = 0; }
    g_pMainSurface = &surf;
    SoftSurf_BlitRectProxy(0x7777, 0, 0, 1, 1, 0xFF, 0xFF, 0xFF);
    g_pMainSurface = NULL;
    CheckEqU32(pPix[0], 0xFFFF, "  proxy ignores its first argument and widens the bytes");

    SoftSurf_dtor(&surf);
}

/* SoftSurf_SamplePixel @ 0xa4630, and the fact that SoftSurf_BlitCopyRaw @ 0xa3db8 is a
 * duplicate of Blit_CopyRows @ 0xa3a14 in the original. */
static void TestSamplePixel(void)
{
    SoftSurf s16, s8, s24;

    printf("SoftSurf_SamplePixel\n");

    SoftSurf_ctor(&s16);
    SoftSurf_AllocPixels(&s16, 4, 3, PIXMODE_16BPP);
    {
        u16 *pPix = (u16 *)SoftSurf_GetPixels(&s16);
        i32  i;
        for (i = 0; i < 12; ++i) {
            pPix[i] = (u16)(0x1111 * (i + 1));
        }
    }
    Blit_LoadSurface(&s16);

    CheckEqI32(SoftSurf_SamplePixel(0, 0), 0x11, "  16bpp returns the low byte of the pixel");
    CheckEqI32(SoftSurf_SamplePixel(1, 0), 0x22, "  next pixel");
    CheckEqI32(SoftSurf_SamplePixel(0, 1), 0x55, "  next row");
    CheckEqI32(SoftSurf_SamplePixel(-1, 0), 0, "  x below zero returns 0");
    CheckEqI32(SoftSurf_SamplePixel(0, -1), 0, "  y below zero returns 0");
    CheckEqI32(SoftSurf_SamplePixel(5, 0), 0, "  x past the width returns 0");
    /* The guard is <=, not <. x == nSurf0 is accepted and reads byte offset 8, which is the
     * low byte of pixel index 4 - the first pixel of the next row. */
    CheckEqI32(SoftSurf_SamplePixel(4, 0), 0x55,
               "  x == nSurf0 is accepted and reads the next row (original off-by-one)");

    SoftSurf_ctor(&s8);
    SoftSurf_AllocPixels(&s8, 4, 1, PIXMODE_8BPP);
    {
        u8 *pBytes = SoftSurf_GetPixels(&s8);
        pBytes[0] = 0xA1; pBytes[1] = 0xB2; pBytes[2] = 0xC3; pBytes[3] = 0xD4;
    }
    Blit_LoadSurface(&s8);
    CheckEqI32(SoftSurf_SamplePixel(0, 0), 0xA1, "  8bpp stride is one byte");
    CheckEqI32(SoftSurf_SamplePixel(2, 0), 0xC3, "  8bpp second sample");

    SoftSurf_ctor(&s24);
    SoftSurf_AllocPixels(&s24, 4, 1, PIXMODE_24BPP);
    {
        u8 *pBytes = SoftSurf_GetPixels(&s24);
        pBytes[0] = 0x10; pBytes[1] = 0x11; pBytes[2] = 0x12;
        pBytes[3] = 0x20; pBytes[4] = 0x21; pBytes[5] = 0x22;
    }
    Blit_LoadSurface(&s24);
    CheckEqI32(SoftSurf_SamplePixel(0, 0), 0x10, "  24bpp stride is three bytes");
    CheckEqI32(SoftSurf_SamplePixel(1, 0), 0x20, "  24bpp second pixel");

    /* Blit_CopyRows and SoftSurf_BlitCopyRaw are the same 420-byte function twice in the
     * original. One port body, and this checks it against the raw copy the original does. */
    {
        u16 aTarget[12];
        i32  i;
        u16 *pPix = (u16 *)SoftSurf_GetPixels(&s16);
        for (i = 0; i < 12; ++i) { pPix[i] = (u16)(i + 1); aTarget[i] = 0; }
        Render_SetTarget(4, 3, 2, 2, 8, (u8 *)aTarget);
        Blit_LoadSurface(&s16);
        Blit_CopyRows();
        CheckEqU32(aTarget[0], 1, "  raw copy first pixel");
        CheckEqU32(aTarget[11], 12, "  raw copy last pixel");
    }

    SoftSurf_dtor(&s16);
    SoftSurf_dtor(&s8);
    SoftSurf_dtor(&s24);
}

/* SoftSurf_BlurPixels @ 0xa2000: interior-only, four-neighbour, in place. */
static void TestBlurPixels(void)
{
    u8 aBuf[16];
    i32 i;

    printf("SoftSurf_BlurPixels\n");

    /* 4x4, interior is rows 1..2 by columns 1..2. The blur reads neighbours it has already
     * rewritten in the same pass, so the numbers below are a cascade, not a convolution. */
    for (i = 0; i < 16; ++i) { aBuf[i] = 0; }
    aBuf[5] = 8; aBuf[6] = 8; aBuf[9] = 8; aBuf[10] = 8;
    SoftSurf_BlurPixels(aBuf, 4, 4);
    /* (0 + 8 + 0 + 8) >> 2 = 4, then minus one. */
    CheckEqI32(aBuf[5], 3, "  first interior pixel");
    /* (3 + 0 + 0 + 8) >> 2 = 2, minus one: the left neighbour is the value just written. */
    CheckEqI32(aBuf[6], 1, "  cascade uses the already-blurred left neighbour");
    /* (0 + 8 + 3 + 0) >> 2 = 2, minus one: the up neighbour is already blurred too. */
    CheckEqI32(aBuf[9], 1, "  cascade uses the already-blurred up neighbour");
    /* (1 + 0 + 1 + 0) >> 2 = 0, and a zero average is left at zero rather than decremented. */
    CheckEqI32(aBuf[10], 0, "  zero average stays zero");

    /* The border is never written, which is why the caller fills it first. */
    for (i = 0; i < 16; ++i) { aBuf[i] = 255; }
    aBuf[5] = 8; aBuf[6] = 8; aBuf[9] = 8; aBuf[10] = 8;
    SoftSurf_BlurPixels(aBuf, 4, 4);
    CheckEqI32(aBuf[0], 255, "  top-left border untouched");
    CheckEqI32(aBuf[3], 255, "  top-right border untouched");
    CheckEqI32(aBuf[12], 255, "  bottom-left border untouched");
    CheckEqI32(aBuf[15], 255, "  bottom-right border untouched");
    CheckEqI32(aBuf[1], 255, "  top edge untouched");
    CheckEqI32(aBuf[4], 255, "  left edge untouched");

    /* The centre is not part of the average: a lone bright pixel spreads to its neighbours
     * and leaves itself at the average of its surroundings. */
    for (i = 0; i < 16; ++i) { aBuf[i] = 0; }
    aBuf[5] = 100;
    SoftSurf_BlurPixels(aBuf, 4, 4);
    CheckEqI32(aBuf[5], 0, "  the centre pixel is not in its own average");

    Check(SoftSurf_BlurPixels(aBuf, 4, 4) == aBuf, "  returns the buffer it was given");
}

/* SoftSurf_BlitRawGlyph @ 0xa33c8: a raw copy of the glyph's own pixel values, and the
 * 24bpp branch's stride bug. */
static void TestBlitRawGlyph(void)
{
    SoftSurf s16, s24;
    u8       aGlyph[4 + 8];
    u16     *pPix;
    i32      i;

    printf("SoftSurf_BlitRawGlyph (0xa33c8)\n");

    /* 2 x 2 glyph of 16bpp pixels. */
    aGlyph[0] = 2; aGlyph[1] = 0;
    aGlyph[2] = 2; aGlyph[3] = 0;
    aGlyph[4] = 0x11; aGlyph[5] = 0x11;
    aGlyph[6] = 0x22; aGlyph[7] = 0x22;
    aGlyph[8] = 0x33; aGlyph[9] = 0x33;
    aGlyph[10] = 0x44; aGlyph[11] = 0x44;

    SoftSurf_ctor(&s16);
    SoftSurf_AllocPixels(&s16, 4, 3, PIXMODE_16BPP);
    pPix = (u16 *)SoftSurf_GetPixels(&s16);
    for (i = 0; i < 12; ++i) { pPix[i] = 0; }
    Blit_LoadSurface(&s16);

    SoftSurf_BlitRawGlyph(1, 1, aGlyph);
    CheckEqU32(pPix[1 * 4 + 1], 0x1111, "  glyph pixel 0 copied raw");
    CheckEqU32(pPix[1 * 4 + 2], 0x2222, "  glyph pixel 1");
    CheckEqU32(pPix[2 * 4 + 1], 0x3333, "  glyph row 1 pixel 0");
    CheckEqU32(pPix[2 * 4 + 2], 0x4444, "  glyph row 1 pixel 1");
    CheckEqU32(pPix[0], 0, "  outside the glyph rect");

    /* Half off the top-left: the clip throws away one column and one row, and the glyph
     * pointer has to advance by the same amount, so the surviving pixel is the last one. */
    for (i = 0; i < 12; ++i) { pPix[i] = 0; }
    SoftSurf_BlitRawGlyph(-1, -1, aGlyph);
    CheckEqU32(pPix[0], 0x4444, "  clipped glyph advances its own pointer");
    CheckEqU32(pPix[1], 0, "  only the surviving column is drawn");

    /* 24bpp branch: three bytes per pixel read, but the destination advances by two.
     * Confirmed in the disassembly at 0xa35e4: LDRH/STRH, LDRB/STRB, ADD R7,R7,#2, while the
     * row step is 3 * nSurf0. Reproduced, not corrected. */
    SoftSurf_ctor(&s24);
    SoftSurf_AllocPixels(&s24, 4, 1, PIXMODE_24BPP);
    {
        u8 *pBytes = SoftSurf_GetPixels(&s24);
        for (i = 0; i < 12; ++i) { pBytes[i] = 0; }
    }
    {
        u8 aGlyph24[4 + 6];
        u8 *pBytes;

        aGlyph24[0] = 2; aGlyph24[1] = 0;
        aGlyph24[2] = 1; aGlyph24[3] = 0;
        aGlyph24[4] = 0xAA; aGlyph24[5] = 0xBB; aGlyph24[6] = 0xCC;
        aGlyph24[7] = 0x11; aGlyph24[8] = 0x22; aGlyph24[9] = 0x33;

        Blit_LoadSurface(&s24);
        SoftSurf_BlitRawGlyph(0, 0, aGlyph24);

        pBytes = SoftSurf_GetPixels(&s24);
        CheckEqI32(pBytes[0], 0xAA, "  24bpp first pixel byte 0");
        CheckEqI32(pBytes[1], 0xBB, "  24bpp first pixel byte 1");
        /* The second pixel lands two bytes along, on top of the first pixel's third byte. */
        CheckEqI32(pBytes[2], 0x11, "  24bpp second pixel starts at +2, not +3");
        CheckEqI32(pBytes[3], 0x22, "  24bpp second pixel byte 1");
        CheckEqI32(pBytes[4], 0x33, "  24bpp second pixel byte 2");
        CheckEqI32(pBytes[5], 0, "  24bpp row only reaches 2n bytes of a 3n-byte row");
    }

    SoftSurf_dtor(&s16);
    SoftSurf_dtor(&s24);
}

/* Gfx_PackRgb565Pair @ 0x2d82c: the colour helper every draw function calls. */
static void TestPackRgb565Pair(void)
{
    GfxColorEnv env;
    u32         nSavedFormat = g_ffFormat;
    i32         nFadeCount = 0;
    static const i32 nG3 = 3;   /* the gamma cells this fixture states, in place of the global */
    static const i32 nG10 = 10;
    static const i32 nG2 = 2;

    printf("Gfx_PackRgb565Pair\n");

    /* gamma 3 gives factor 3 * 0.1 + 0.7 = 1.0, so the channels pass through at their
     * natural 5/6/5 widths: 255 >> 3 is 31, 255 >> 2 is 63. */
    env.pnGamma = &nG3;
    env.pnFadeCount = &nFadeCount;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 255, 255, 255, 128, 128, 128), 0x8410,
               "  returns the SECOND packed colour, not the first");

    /* gamma 10 -- outside the range the original can hold, but the divisor it produces is the
     * point: 1.7, so 31/1.7 = 18 and 63/1.7 = 37. Both colours are the same here because the
     * return value is the second one. */
    env.pnGamma = &nG10;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 255, 255, 255, 255, 255, 255) & 0xFFFF, 0x94B2,
               "  factor 1.7 scales every channel");

    /* A fade divides again, and the inner division is integer: (11 - 5) / 2 + 1 = 4.
     * 31/4 = 7, 63/4 = 15. */
    env.pnGamma = &nG3;
    nFadeCount = 5;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 255, 255, 255, 255, 255, 255) & 0xFFFF, 0x39E7,
               "  fade divides a second time by (11 - nFade)/2 + 1");

    /* This is the live Map+0x64 cell. Do not rebuild env between these two values: raw 0x2D968
     * reads it on each call. At 10 the divisor is 1; after the fade tick decrements it to 9,
     * the divisor is 2. The modal's fixed second RGB pair is (10,90,50). */
    g_ffFormat = PUFO_FF_565;
    env.pnGamma = &nG2;
    nFadeCount = 10;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 155, 255, 0, 10, 90, 50), 0x0B06,
               "  live fade 10 has divisor 1");
    nFadeCount = 9;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 155, 255, 0, 10, 90, 50), 0x0183,
               "  same live fade cell at 9 changes the modal pair to raw value 0x0183");

    /* Clamps are 5 / 6 / 5 bits. */
    env.pnGamma = &nG3;
    nFadeCount = 0;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 1000, 1000, 1000, 1000, 1000, 1000) & 0xFFFF, 0xFFFF,
               "  channels clamp to 31 / 63 / 31");

    /* Gate: the helper only runs for a 565 display format. */
    g_ffFormat = PUFO_FF_888;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 255, 255, 255, 255, 255, 255), 0,
               "  does nothing unless the format has the 565 bit");
    g_ffFormat = nSavedFormat;

    /* Green is shifted by 2 and red and blue by 3 - the widths are 6, 5 and 5. */
    env.pnGamma = &nG3;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 0, 255, 0, 0, 255, 0) & 0xFFFF, 0x7E0,
               "  green alone fills the 6-bit middle field");
    CheckEqU32(Gfx_PackRgb565Pair(&env, 255, 0, 0, 255, 0, 0) & 0xFFFF, 0xF800,
               "  red alone fills the top field");
    CheckEqU32(Gfx_PackRgb565Pair(&env, 0, 0, 255, 0, 0, 255) & 0xFFFF, 0x1F,
               "  blue alone fills the low field");
}

/* Tact_BuildPaletteLuts @ 0x3734c and Tact_LoadPalettes @ 0x377ec.
 *
 * The LUT expectations below were computed offline from the decoded specification - divisor
 * 0.1 * gamma + 0.7, per-channel division, truncation toward zero, clamp to the channel width - and are
 * written here as literals rather than recomputed in C, so a change in the port shows up as a failure
 * instead of changing with it. */
/* Tact_BuildPaletteLuts reads a gamma cell the host installed (Map_nGamma in the original,
 * 0x37368); this fixture has no world, so it installs its own cell. */
static i32 s_nGammaCell;

static void GammaCell(i32 nValue)
{
    s_nGammaCell = nValue;
    Render_SetGammaCell(&s_nGammaCell);
}

static void TestPaletteLut(void)
{
    static const u32 aWant565[5][3] = {
        /* gamma 0..4, entries 0x39E7 (R7 G15 B7), 0xFFFF, 0x0001 */
        { 0x52AA, 0xFFFF, 0x0001 },
        { 0x4248, 0xFFFF, 0x0001 },
        { 0x3A07, 0xFFFF, 0x0001 },
        { 0x39E7, 0xFFFF, 0x0001 },
        { 0x31A6, 0xE73C, 0x0000 },
    };
    static const u32 aWant555[5][2] = {
        /* gamma 0..4, entries 0x7FFF and 0x1234 */
        { 0x7FFF, 0x171C },
        { 0x7FFF, 0x16B9 },
        { 0x7FFF, 0x1256 },
        { 0x7FFF, 0x1234 },
        { 0x739C, 0x0DF2 },
    };
    u32 ffSave = g_ffFormat;
    i32 nGamma;
    i32 i;

    for (i = 0; i < PUFO_PAL_SLOTS; ++i) {
        memset(g_aPalette[i], 0, sizeof(g_aPalette[i]));
    }

    /* 16bpp RGB565. Slot 57 and above must not be touched: the pass covers 57 slots, 0 through 56. */
    g_ffFormat = PUFO_FF_565;
    g_aPalette[0][0] = 0x39E7;
    g_aPalette[0][1] = 0xFFFF;
    g_aPalette[0][2] = 0x0001;
    g_aPalette[57][0] = 0xBEEF;

    for (nGamma = 0; nGamma <= PUFO_PAL_GAMMA_MAX; ++nGamma) {
        g_aPalette[0][0] = 0x39E7;
        g_aPalette[0][1] = 0xFFFF;
        g_aPalette[0][2] = 0x0001;
        GammaCell(nGamma);
        Tact_BuildPaletteLuts();
        CheckEqU32(g_aPalette[0][0], aWant565[nGamma][0], "lut 565 R7 G15 B7");
        CheckEqU32(g_aPalette[0][1], aWant565[nGamma][1], "lut 565 white");
        CheckEqU32(g_aPalette[0][2], aWant565[nGamma][2], "lut 565 blue 1");
    }

    /* gamma 3 is the neutral setting: divisor is exactly 1.0, so the pass must be the identity. */
    g_aPalette[0][0] = 0x39E7;
    GammaCell(3);
    Tact_BuildPaletteLuts();
    CheckEqU32(g_aPalette[0][0], 0x39E7, "lut 565 gamma 3 is identity");
    CheckEqU32(g_aPalette[57][0], 0xBEEF, "lut covers slots 0..56 only");

    /* 15bpp RGB555. */
    g_ffFormat = PUFO_FF_555;
    for (nGamma = 0; nGamma <= PUFO_PAL_GAMMA_MAX; ++nGamma) {
        g_aPalette[0][0] = 0x7FFF;
        g_aPalette[0][1] = 0x1234;
        GammaCell(nGamma);
        Tact_BuildPaletteLuts();
        CheckEqU32(g_aPalette[0][0], aWant555[nGamma][0], "lut 555 white");
        CheckEqU32(g_aPalette[0][1], aWant555[nGamma][1], "lut 555 0x1234");
    }

    /* ORIGINAL BUG (0x37718): the 888 branch reads HIBYTE(w) and (u8)w, so it only ever sees the low
     * 16 bits, and it writes back 16 bits. Palette_Load packs 888 as R | G<<8 | B<<16, so on a 24-bit
     * display this drops the blue channel out of every palette. Reproduced, so the test pins it. */
    g_ffFormat = PUFO_FF_888;
    g_aPalette[0][0] = 0x00AABB1234ULL & 0xFFFFFFFFu;   /* R 0x34, G 0x12, B 0xBB, A 0xAA in the top byte */
    GammaCell(3);
    Tact_BuildPaletteLuts();
    CheckEqU32(g_aPalette[0][0], 0x00001234u, "lut 888 drops the high half of the entry");

    g_ffFormat = ffSave;
}

/* The ramp cross-check.
 *
 * The port reproduces the original's operations literally: two fields scaled by NEGATIVE scale through
 * dtou, one by positive scale, then lo - 32 * (a + 64 * b). Read as arithmetic instead of as register
 * moves, the same pass is per-channel floor with the weights restored:
 *
 *     out = floor(2*s*blue) + 32*floor(2*s*green) + 2048*floor(2*s*red),  s = 1/(0.07*i*i + 2)
 *
 * The test uses that second form and the port uses the first, on the real tactical.pal from main.dat,
 * over all 256 entries and all 19 derived slots. If either reading of the original is wrong, the two
 * disagree thousands of times. It also pins the per-channel reading: scaling the whole 16-bit value and
 * truncating once gives different results. */
static void TestPaletteRamp(void)
{
    u32 ffSave = g_ffFormat;
    u32 aSrc[PUFO_PAL_SIZE];
    u32 aBefore[PUFO_PAL_SLOTS];
    i32 i, j;

    g_ffFormat = PUFO_FF_565;
    memset(g_aPalette, 0, sizeof(g_aPalette));
    Palette_Load("tactical.pal", PUFO_PAL_TACTICAL);
    memcpy(aSrc, g_aPalette[PUFO_PAL_TACTICAL], sizeof(aSrc));

    Check(aSrc[0] != 0 || aSrc[1] != 0, "tactical.pal loaded from main.dat");

    for (i = 0; i < PUFO_PAL_SLOTS; ++i) {
        aBefore[i] = g_aPalette[i][0];
    }

    GammaCell(3);           /* gamma 3: the LUT pass is the identity, so the ramp is all that is visible */
    Tact_LoadPalettes();

    /* Slot 34 is both the source and the destination at i = 0, where the scale is exactly 0.5. If that
     * pass were not the identity it would destroy the palette the rest of the ramp reads from. */
    {
        bool bSame = true;
        for (j = 0; j < PUFO_PAL_SIZE; ++j) {
            if (g_aPalette[PUFO_PAL_TACTICAL][j] != aSrc[j]) {
                bSame = false;
            }
        }
        Check(bSame, "ramp slot 34 is the identity, tactical.pal survives its own pass");
    }

    /* Slot 53 is forced to zero, so the ramp ends in black. */
    {
        bool bZero = true;
        for (j = 0; j < PUFO_PAL_SIZE; ++j) {
            if (g_aPalette[PUFO_PAL_TACTICAL + 19][j] != 0) {
                bZero = false;
            }
        }
        Check(bZero, "ramp slot 53 is all zero");
    }

    for (i = 0; i < PUFO_PAL_RAMP_COUNT - 1; ++i) {
        double dScale = 1.0 / (0.07 * (double)(i * i) + 2.0);
        i32 nDiff = 0;
        for (j = 0; j < PUFO_PAL_SIZE; ++j) {
            u32 w = aSrc[j];
            u32 nBlue = (u32)(i32)((double)(w & 0x1F) * dScale * 2.0);
            u32 nGreen = (u32)(i32)((double)((w & 0x7E0) >> 5) * dScale * 2.0);
            u32 nRed = (u32)(i32)((double)(w >> 11) * dScale * 2.0);
            u32 nWant = (nBlue + 32u * nGreen + 2048u * nRed) & 0xFFFFu;
            if (g_aPalette[PUFO_PAL_TACTICAL + i][j] != nWant) {
                ++nDiff;
            }
        }
        CheckEqI32(nDiff, 0, "ramp matches the per-channel reading");
    }

    /* Each channel is scaled on its own, so no channel may grow and no channel may cross its neighbour. */
    {
        bool bMonotone = true;
        for (i = 1; i < PUFO_PAL_RAMP_COUNT - 1; ++i) {
            for (j = 0; j < PUFO_PAL_SIZE; ++j) {
                u32 a = aSrc[j];
                u32 b = g_aPalette[PUFO_PAL_TACTICAL + i][j];
                if (((b & 0x1F) > (a & 0x1F)) ||
                    (((b & 0x7E0) >> 5) > ((a & 0x7E0) >> 5)) ||
                    ((b >> 11) > (a >> 11))) {
                    bMonotone = false;
                }
            }
        }
        Check(bMonotone, "ramp darkens every channel independently");
    }

    /* The ramp writes 34..53 and the LUT pass writes 0..56, so 57 and above must be untouched. */
    {
        bool bSame = true;
        for (i = 57; i < PUFO_PAL_SLOTS; ++i) {
            if (g_aPalette[i][0] != aBefore[i]) {
                bSame = false;
            }
        }
        Check(bSame, "palettes above slot 56 are untouched");
    }

    g_ffFormat = ffSave;
}

/* The bound (non-Ex) forms: SoftSurf_Blit @ 0x9e558, SoftSurf_BlitFlip @ 0x9e590,
 * SoftSurf_BlitIndexed @ 0x9e9f4, SoftSurf_BlitIndexedKeyed @ 0x9ea34, SoftSurf_BlitRect @ 0x9ebb0,
 * SoftSurf_BlitSrcRect @ 0x9ebf8. Each one is PUSH {LR}, a few argument shuffles, LDR
 * g_pMainSurface + LDR [R0], BL the Ex form, ADD SP, POP {PC} - so a bound form is pinned by two
 * things: the surface it loads, and what each argument becomes on the way through. Two of them
 * change the arguments. SoftSurf_BlitRect loads nColorLow, nColorMid and nIdx with LDRB
 * (0x9EBB8, 0x9EBCC, 0x9EBDC), truncating all three to bytes while the two sizes stay dwords
 * (MOV R3 at 0x9EBC0, STR R3 at 0x9EBBC); SoftSurf_BlitSrcRect @ 0x9ebf8 loads every operand with
 * LDR and instead moves srcX (0x9EC24) into blitColorMid and srcY (0x9EC14) into blitColorLow. */
static void TestBoundWrappers(void)
{
    SoftSurf surf;
    SoftSurf other;
    u16     *pPix;
    u16     *pOther;
    u16      aTarget[8];
    u8       aGlyph[8];
    static u32 aPal[16];
    static u8  aMask[8];
    i32        i;

    printf("Bound wrappers draw on g_pMainSurface\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 8, 4, PIXMODE_16BPP);
    SoftSurf_ctor(&other);
    SoftSurf_AllocPixels(&other, 8, 4, PIXMODE_16BPP);
    pPix   = (u16 *)SoftSurf_GetPixels(&surf);
    pOther = (u16 *)SoftSurf_GetPixels(&other);
    g_pMainSurface = &surf;

    memset(g_aPalette, 0, sizeof(g_aPalette));
    g_aPalette[3][7]    = 0x1111;
    g_aPalette[3][8]    = 0x1112;
    g_aPalette[3][0x11] = 0x3333;
    g_aPalette[4][7]    = 0x2222;
    g_aPalette[4][8]    = 0x2223;
    g_aPalette[5][7]    = 0xFFFF;
    g_aPalette[5][9]    = 0x001F; /* entry 5[8] stays 0: one glyph column with no colour */
    g_aPalette[6][5]    = 0x1234;
    g_aPalette[7][7]    = 0x1111; /* a slot numbered like the raw pointer below, and not the one used */

    aGlyph[0] = 2; aGlyph[1] = 0; aGlyph[2] = 1; aGlyph[3] = 0; /* u16 w = 2, u16 h = 1 */
    aGlyph[4] = 7; aGlyph[5] = 8; aGlyph[6] = 0; aGlyph[7] = 0;

    for (i = 0; i < 32; ++i) {
        pPix[i] = 0xEEEE;
        pOther[i] = 0xEEEE;
    }

    /* SoftSurf_BlitIndexedEx @ 0x9fadc loads the surface it is given; the bound form @ 0x9e9f4
     * loads g_pMainSurface, and its palette pointer is 0x1472D0 + nPal*0x400 (LDR =0x1472D0 at
     * 0x9E9FC, ADD R3,R2,R3,LSL#10 at 0x9EA04): a slot is 256 entries, not 256 bytes. */
    SoftSurf_BlitIndexedEx(&other, 0, 0, aGlyph, 3);
    CheckEqU32(pOther[0], 0x1111, "  SoftSurf_BlitIndexedEx @ 0x9fadc draws on the surface it was given");
    CheckEqU32(pOther[1], 0x1112, "  ... one glyph byte per palette entry, in order");

    SoftSurf_BlitIndexed(0, 0, aGlyph, 3);
    CheckEqU32(pPix[0], 0x1111, "  SoftSurf_BlitIndexed @ 0x9e9f4 draws on g_pMainSurface");
    CheckEqU32(pOther[2], 0xEEEE, "  ... not on the surface the last Ex call loaded");
    SoftSurf_BlitIndexed(3, 0, aGlyph, 4);
    CheckEqU32(pPix[3], 0x2222, "  slot 4 sits 256 entries from slot 3 (ADD ...,LSL#10 at 0x9EA04)");
    CheckEqU32(pPix[4], 0x2223, "  ... and the slot keeps its own entry order");

    /* SoftSurf_BlitIndexedKeyed @ 0x9ea34 takes five arguments; the key is published through
     * blitColorKey by Blit_SetColorKey @ 0xa3624 and the inner compares it against every glyph
     * byte, so a match leaves the pixel alone. */
    aGlyph[5] = 0xFF;
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0xEEEE;
    }
    SoftSurf_BlitIndexedKeyed(0, 0, aGlyph, 3, 0xFF);
    CheckEqU32(pPix[0], 0x1111, "  SoftSurf_BlitIndexedKeyed @ 0x9ea34 -> @ 0x9fb0c draws the index that is not the key");
    CheckEqU32(pPix[1], 0xEEEE, "  ... @ 0x9fb0c publishes the key through blitColorKey, so a match is skipped");
    CheckEqI32(Blit_SetColorKey(0x11), 0x11, "  Blit_SetColorKey @ 0xa3624 returns the key it stored");
    aGlyph[4] = 0x11;
    aGlyph[5] = 7;
    SoftSurf_BlitIndexedKeyed(0, 0, aGlyph, 3, 7);
    CheckEqU32(pPix[0], 0x3333, "  index 0x11 is drawn when the key is 7, so 0xa3624 really stored the new one");
    CheckEqU32(pPix[1], 0xEEEE, "  ... and index 7 is the one skipped");
    aGlyph[4] = 7;
    aGlyph[5] = 8;

    /* SoftSurf_Blit @ 0x9e558 forwards its fifth argument to SoftSurf_BlitEx @ 0x9fb44, which
     * publishes it as the SOURCE weight through BlitParams_Set @ 0xa47b0 (dword_A3040) - the
     * destination weight is 255 - alpha. If the pair were swapped the alpha cases below all move. */
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    SoftSurf_Blit(0, 0, aGlyph, 5, 255);
    CheckEqU32(pPix[0], 0xF7DE, "  SoftSurf_Blit @ 0x9e558 alpha 255 is opaque and still loses one per field");
    CheckEqU32(pPix[1], 0, "  ... the fifth argument is a weight, not a colour: entry 5[8] is 0");

    /* SoftSurf_Blit_Inner @ 0xa47d0 has no per-field clamp. 0xA4908..0xA496C builds the blue, green
     * and red terms of the destination (weight 255-alpha) and then of the source (weight alpha),
     * each MUL'd field product taken with LSR#8, all six added into R6, and 0xA4970 stores R6 as one
     * halfword. Inside alpha 0..255 the two weights sum to 255, so no term can overflow its field;
     * outside it the carry is what the original writes. */
    aGlyph[4] = 8;
    aGlyph[5] = 8;
    pPix[0] = 0x001F;
    pPix[1] = 0x001F;
    SoftSurf_Blit(0, 0, aGlyph, 5, 511);
    CheckEqU32(pPix[0], 0xFFE1, "  alpha 511, dst blue 31: 31*(255-511) = 0xFFFFE100, LSR#8, truncated by STRH");
    CheckEqU32(pPix[1], 0xFFE1, "  ... a per-field 0x1F clamp would leave 1 here instead of 0xFFE1");
    pPix[0] = 0x001F;
    pPix[1] = 0x001F;
    SoftSurf_Blit(0, 0, aGlyph, 5, 0);
    CheckEqU32(pPix[0], 0x001E, "  alpha 0 still scales the destination: (31*255)>>8 = 30, not a copy");
    aGlyph[4] = 9;
    aGlyph[5] = 9;
    pPix[0] = 0x001F;
    pPix[1] = 0x001F;
    SoftSurf_Blit(0, 0, aGlyph, 5, 128);
    CheckEqU32(pPix[0], 0x001E, "  alpha 128 between two equal blues: (31*127)>>8 + (31*128)>>8 = 15 + 15");
    aGlyph[4] = 7;
    aGlyph[5] = 8;

    /* SoftSurf_BlitFlip @ 0x9e590 -> SoftSurf_BlitFlipEx @ 0x9fb80 forwards the palette and mask
     * POINTERS unchanged (no aPalette[256*slot] conversion), and SoftSurf_BlitFlip_Inner @ 0xa49a0
     * weights the destination with 256-mask and starts the mask at maskBase + (pIdx - glyphBase),
     * so clipping the glyph shifts the mask with it. aPal is a local table; slot 7 of g_aPalette
     * holds a different value and is not what this reads. */
    for (i = 0; i < 16; ++i) {
        aPal[i] = 0;
    }
    aPal[7] = 0x001F;
    aPal[8] = 0x001F;
    aMask[0] = 0; aMask[1] = 0; aMask[2] = 0; aMask[3] = 0;
    aMask[4] = 0x00; aMask[5] = 0xFF; aMask[6] = 0; aMask[7] = 0;
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0x001F;
    }
    SoftSurf_BlitFlip(0, 0, aGlyph, aPal, aMask);
    CheckEqU32(pPix[0], 0x001F, "  mask 0 leaves the pixel exactly: the weight is 256-mask, not 255-mask @ 0xa49a0");
    CheckEqU32(pPix[1], 0x001E, "  mask 255 takes the raw table's value: (31*255)>>8 = 30, not slot 7's 0x11");
    pPix[0] = 0x001F;
    SoftSurf_BlitFlip(-1, 0, aGlyph, aPal, aMask);
    CheckEqU32(pPix[0], 0x001E, "  a clipped glyph uses mask[5]: the mask pointer is offset by the clip distance");

    /* The rectangle pair. Blit_CopyToXY @ 0xa42e4 gates on the TARGET pixel mode and
     * SoftSurf_BlitSrcRect_Inner @ 0xa4450 on the SURFACE mode; both build the colour as
     * ((idx >> 3) << 11) + 32 * (mid >> 2) + (low >> 3) with plain additions, so an operand too big
     * for its field carries into the neighbour instead of being masked. */
    Render_SetTarget(8, 4, 2, 2, 16, (u8 *)aTarget);
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    SoftSurf_BlitRect(0, 0, 2, 1, 0x1FF, 0x1FF, 0x1FF);
    CheckEqU32(pPix[0], 0xFFFF, "  SoftSurf_BlitRect @ 0x9ebb0 truncates idx/mid/low to bytes: 0xFF,0xFF,0xFF -> 0xFFFF");
    CheckEqU32(pPix[1], 0xFFFF, "  ... the untruncated 0x1FF operands would have given 0x081F");
    CheckEqU32(pPix[2], 0, "  ... the size argument stays a dword and only 2 columns are filled");

    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    SoftSurf_BlitSrcRect(0, 0, 1, 1, 0, 0x7C, 0xF8, 255);
    CheckEqU32(pPix[0], 0x03DE, "  SoftSurf_BlitSrcRect @ 0x9ebf8 maps srcX onto the middle operand (LDR 0x9EC24)");
    CheckEqU32(pPix[1], 0, "  ... and srcY onto the low one (LDR 0x9EC14): swapping them gives 0x07AE");

    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    SoftSurf_BlitSrcRect(0, 0, 1, 1, 0, 0x3FF, 0xF8, 200);
    CheckEqU32(pPix[0], 0x1638, "  ... no truncation here: 0x3FF>>2 = 255 and 32*255 carries a bit into the top field");

    /* Blit_SetRectAndColor @ 0xa3f5c is the only source of the rectangle size and of the two colour
     * operands, because Blit_CopyToXY takes nothing but x, y and the index. */
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    Blit_SetRectAndColor(3, 2, 0x3F, 0x1C);
    Blit_CopyToXY(0, 0, 0xF8);
    CheckEqU32(pPix[0], 0xF9E3, "  Blit_SetRectAndColor @ 0xa3f5c supplies both operands: (0xF8>>3)<<11 + 32*(0x3F>>2) + (0x1C>>3)");
    CheckEqU32(pPix[2], 0xF9E3, "  ... and the width: 3 columns");
    CheckEqU32(pPix[3], 0, "  ... the 4th column is outside the published width");
    CheckEqU32(pPix[8 + 2], 0xF9E3, "  ... 2 rows deep");
    CheckEqU32(pPix[8 + 3], 0, "  ... row 1 clipped at the same width");

    /* SoftSurf_FillRectEx @ 0x9fbbc loads the surface, publishes the rectangle with both colour
     * operands zero, and hands the index to SoftSurf_FillRect_Inner @ 0xa3f94, which for a 16bpp
     * surface reads pPal[nIdx] before clipping - a flat palette entry, not a packed colour, and the
     * entry is read even when the rectangle is clipped away. */
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
    }
    SoftSurf_FillRectEx(&surf, 0, 0, 2, 2, 6, 5);
    CheckEqU32(pPix[0], 0x1234, "  SoftSurf_FillRectEx @ 0x9fbbc fills with the palette entry, not a packed colour");
    CheckEqU32(pPix[1], 0x1234, "  ... the size is the one it publishes (2x2), not the 3x2 above");
    CheckEqU32(pPix[2], 0, "  ... and stops at that width");
    CheckEqU32(pPix[8 + 1], 0x1234, "  ... two rows deep");

    /* The two rectangle Ex forms @ 0x9fc30 and @ 0x9fc70 are the load + publish + fill/blend trio;
     * a bound form adds nothing but which surface gets loaded, so each Ex is checked on a surface
     * that is NOT g_pMainSurface. */
    for (i = 0; i < 32; ++i) {
        pPix[i] = 0;
        pOther[i] = 0;
    }
    SoftSurf_BlitRectEx(&other, 0, 0, 1, 1, 0xF8, 0x3F, 0x07);
    CheckEqU32(pOther[0], 0xF9E0, "  SoftSurf_BlitRectEx @ 0x9fc30 loads the surface it is given and fills it");
    CheckEqU32(pPix[0], 0, "  ... the main surface stays untouched");
    SoftSurf_BlitSrcRectEx(&other, 0, 0, 1, 1, 0, 0x7C, 0xF8, 255);
    CheckEqU32(pOther[0], 0x03DE, "  SoftSurf_BlitSrcRectEx @ 0x9fc70 publishes the operands and blends on that surface");
    CheckEqU32(pPix[0], 0, "  ... again not on g_pMainSurface");

    g_pMainSurface = NULL;
    SoftSurf_dtor(&surf);
    SoftSurf_dtor(&other);
}

/* The 24bpp arm of the two indexed inners. SoftSurf_BlitIndexed_Inner @ 0xa306c writes the three
 * palette bytes at [R7], [R7+1], [R7+2] (0xA338C..0xA339C) and then advances the destination by
 * only TWO at 0xA33A0 (`ADD R7,R7,#2`) while advancing the index by one at 0xA33A4, so consecutive
 * pixels overlap by one byte. SoftSurf_BlitIndexedKeyed_Inner @ 0xa3638 has no such arm: its
 * mode > 2 tail computes the row pointer (LDR blitState.pSrc at 0xA393C, MUL blitState.nSurf0 at
 * 0xA3954, ADD at 0xA3958/0xA395C) and leaves through the bare POP at 0xA3960 - no pixel loop, no
 * store, and blitColorKey is never read, so a 24bpp keyed blit draws nothing at all. */
static void TestTwentyFourBppIndexed(void)
{
    SoftSurf surf;
    u8      *pPix;
    u8       aGlyph[8];

    printf("24bpp indexed blit\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 4, 1, PIXMODE_24BPP);
    pPix = SoftSurf_GetPixels(&surf);

    memset(g_aPalette, 0, sizeof(g_aPalette));
    g_aPalette[2][1] = 0x00A1B2C3; /* the three bytes written are C3, B2, A1 (little-endian) */

    aGlyph[0] = 3; aGlyph[1] = 0; aGlyph[2] = 1; aGlyph[3] = 0;
    aGlyph[4] = 1; aGlyph[5] = 1; aGlyph[6] = 1; aGlyph[7] = 0;

    memset(pPix, 0xEE, 12);
    SoftSurf_BlitIndexedEx(&surf, 0, 0, aGlyph, 2);
    CheckEqU32(pPix[0], 0xC3, "  3 bytes of the entry written at +0 @ 0xa306c");
    CheckEqU32(pPix[1], 0xB2, "  ... second byte");
    CheckEqU32(pPix[2], 0xC3, "  third pixel starts 2 bytes on (ADD R7,R7,#2 at 0xA33A0): it rewrites byte 2");
    CheckEqU32(pPix[3], 0xB2, "  ... overlapping the previous pixel by one byte");
    CheckEqU32(pPix[5], 0xB2, "  ... third pixel's second byte");
    CheckEqU32(pPix[6], 0xA1, "  ... and its third byte, which a stride of 3 would have left as 0xEE");
    CheckEqU32(pPix[7], 0xEE, "  nothing past 3 pixels * 2 bytes + 1");

    memset(pPix, 0xEE, 12);
    SoftSurf_BlitIndexedKeyedEx(&surf, 0, 0, aGlyph, 2, 0);
    Check(pPix[0] == 0xEE && pPix[1] == 0xEE && pPix[2] == 0xEE && pPix[3] == 0xEE,
          "  a 24bpp keyed blit draws nothing @ 0xa3638 (0xA3960 POP, no pixel loop)");
    Check(pPix[6] == 0xEE && pPix[11] == 0xEE, "  ... with any key: the mode > 2 arm never compares against it");

    SoftSurf_dtor(&surf);
}

/* Blit_CopyRows_HiRes @ 0xa3bb8, reached only through Render_BlitSoft_HiRes @ 0x9fa90, and the
 * LoRes pair Render_BlitSoft_LoRes @ 0x9fa7c / Blit_CopyRows @ 0xa3a14. All four wrappers are
 * PUSH {LR}; ADD R0,R0,#0x18; BL Blit_LoadSurface; POP {LR}; B <copy> - a tail call, so the only
 * difference is the copy body. The upscale comes from the strides: the loops count
 * blitState.nWidth x nHeight (the target's logical size) while the destination advances
 * 2 * nDstPitchX per column (ADD R8,R8,R0,LSL#1 at 0xA3C50) and restarts at rowStart +
 * 2 * nDstPitchY (ADD R8,R2,R1,LSL#1 at 0xA3C5C), writing each source value four times. */
static void TestHiResUpscale(void)
{
    SoftSurf s8, s16, s3;
    static u8 aDst8[16];
    static u8 aDst16[32];
    static u8 aDst3[32];
    u16      *p16;

    printf("Blit_CopyRows_HiRes (2x2 upscale)\n");

    /* 8bpp source arm: four source bytes, a 2x2 logical target, stride 1 and row pitch 4. */
    SoftSurf_ctor(&s8);
    SoftSurf_AllocPixels(&s8, 4, 1, PIXMODE_8BPP);
    memcpy(SoftSurf_GetPixels(&s8), "\xA1\xB2\xC3\xD4", 4);

    Render_SetTarget(2, 2, 0, 1, 4, aDst8);
    g_blitState.nSurf0 = 4; /* the surface is 4x1: the counts must come from the target, not from here */
    g_blitState.nSurf1 = 1;
    memset(aDst8, 0, sizeof(aDst8));
    Render_BlitSurface(&s8);
    CheckEqU32(aDst8[0], 0xA1, "  Render_BlitSurface @ 0x9fa7c copies 1:1 at the target's strides");
    CheckEqU32(aDst8[1], 0xB2, "  ... one byte per logical column");
    CheckEqU32(aDst8[4], 0xC3, "  ... row 1 lands at nDstPitchY");
    CheckEqU32(aDst8[5], 0xD4, "  ... four logical pixels in all");
    CheckEqU32(aDst8[2], 0, "  ... and nothing beyond the logical width");

    memset(aDst8, 0, sizeof(aDst8));
    Render_BlitSurface_HiRes(&s8);
    CheckEqU32(aDst8[0], 0xA1, "  Render_BlitSoft_HiRes @ 0x9fa90 writes the 2x2 block @ 0xa3bb8");
    CheckEqU32(aDst8[1], 0xA1, "  ... the second device column (LDRB then four STRB, 0xA3C34..0xA3C48)");
    CheckEqU32(aDst8[4], 0xA1, "  ... and the device row below it");
    CheckEqU32(aDst8[5], 0xA1, "  ... four times the same source byte");
    CheckEqU32(aDst8[2], 0xB2, "  the next source byte lands 2 * nDstPitchX on (ADD R8,R8,R0,LSL#1 at 0xA3C50)");
    CheckEqU32(aDst8[8], 0xC3, "  the next source row starts 2 * nDstPitchY on (ADD R8,R2,R1,LSL#1 at 0xA3C5C)");
    CheckEqU32(aDst8[15], 0xD4, "  ... so a 2x2 logical surface fills a 4x4 device");

    /* 16bpp arm: LDRH then four STRH (0xA3CBC..0xA3CD4), two source bytes per logical pixel. */
    SoftSurf_ctor(&s16);
    SoftSurf_AllocPixels(&s16, 2, 2, PIXMODE_16BPP);
    p16 = (u16 *)SoftSurf_GetPixels(&s16);
    p16[0] = 0x1111;
    p16[1] = 0x2222;
    p16[2] = 0x3333;
    p16[3] = 0x4444;
    Render_SetTarget(2, 2, 2, 2, 8, aDst16);
    memset(aDst16, 0, sizeof(aDst16));
    Render_BlitSurface_HiRes(&s16);
    p16 = (u16 *)aDst16;
    CheckEqU32(p16[0], 0x1111, "  16bpp arm keeps the halfword value (LDRH/STRH, 0xA3CBC/0xA3CC0)");
    CheckEqU32(p16[1], 0x1111, "  ... twice across");
    CheckEqU32(p16[2], 0x2222, "  ... and the next source halfword follows at 2 * nDstPitchX");
    CheckEqU32(p16[4], 0x1111, "  ... twice down as well");
    CheckEqU32(p16[10], 0x4444, "  ... last source pixel fills the last 2x2 block");
    Check(p16[0] == 0x1111 && p16[1] == 0x1111 && p16[2] == 0x2222 && p16[3] == 0x2222
          && p16[4] == 0x1111 && p16[5] == 0x1111 && p16[6] == 0x2222 && p16[7] == 0x2222
          && p16[8] == 0x3333 && p16[9] == 0x3333 && p16[10] == 0x4444 && p16[11] == 0x4444
          && p16[12] == 0x3333 && p16[13] == 0x3333 && p16[14] == 0x4444 && p16[15] == 0x4444,
          "  the whole 4x4 device pattern is the source pattern doubled in both directions");

    /* The mode > 2 arm reads ONE source byte three times - the LDRB at 0xA3D44, 0xA3D60 and 0xA3D7C
     * all address an un-advanced [R9], and R9 only moves by 3 at 0xA3D98 - so three destination
     * columns take one value while two source bytes are skipped. Each sub-write is a byte store at
     * [p] (0xA3D48) and halfword stores at [p+nDstPitchX], [p+nDstPitchY], [p+nDstPitchX+nDstPitchY],
     * which overlap each other as k runs 0, 1, 2. */
    SoftSurf_ctor(&s3);
    SoftSurf_AllocPixels(&s3, 1, 1, PIXMODE_24BPP);
    memcpy(SoftSurf_GetPixels(&s3), "\xAB\xCD\xEF", 3);
    Render_SetTarget(1, 1, 3, 2, 4, aDst3);
    memset(aDst3, 0, sizeof(aDst3));
    Render_BlitSurface_HiRes(&s3);
    CheckEqU32(aDst3[0], 0xAB, "  the byte store at [p] (0xA3D48)");
    CheckEqU32(aDst3[1], 0xAB, "  k = 1's byte store, from the SAME source byte");
    CheckEqU32(aDst3[2], 0xAB, "  k = 1's halfword store was 0x00AB, then k = 2's byte store rewrote it");
    CheckEqU32(aDst3[3], 0xAB, "  ... and k = 2's halfword store rewrote this one too");
    CheckEqU32(aDst3[4], 0xAB, "  ... the pattern carries: byte 4 is 0xAB at the end");
    CheckEqU32(aDst3[5], 0, "  ... while byte 5 ends as the high byte of a 0x00AB halfword");
    CheckEqU32(aDst3[6], 0xAB, "  ... 6 is a byte store again");
    CheckEqU32(aDst3[7], 0, "  ... 7 is a high byte");
    CheckEqU32(aDst3[8], 0xAB, "  ... 8 likewise");
    CheckEqU32(aDst3[9], 0, "  ... 9 is a high byte");
    CheckEqU32(aDst3[10], 0, "  nothing past the 2x2 block at the last k");
    Check(aDst3[1] == 0xAB && aDst3[2] == 0xAB, "  no source byte other than 0xAB was ever read (CD and EF are skipped)");

    SoftSurf_dtor(&s8);
    SoftSurf_dtor(&s16);
    SoftSurf_dtor(&s3);
}

/* Palette_Load @ 0x3e47c: one Find (0x3E49C), one Read of a fixed 0x300 bytes (`MOV R2,#0x300` at
 * 0x3E4AC), a conversion loop over 256 entries selected by the format bits in gxDisp.ffFormat
 * (LDR once at 0x3E4C0), and a store at 0x1472D0 + 4*(i + (slot & 0xFF)*256) (AND #0xFF at 0x3E568,
 * the index and slot combined at 0x3E578..0x3E580). Neither the Find nor the Read result is tested
 * in the original, so a miss or a short member leaves the slot holding whatever the stack held -
 * the port returns early instead, a deviation recorded in docs/parity/bugs.tsv. */
static void TestPaletteDispatch(void)
{
    u32 nFmtSave = g_ffFormat;

    printf("Palette_Load format dispatch and slot stride\n");

    /* Both bits set: the 0x40 (555) branch is the one the original tests first, so the 555 form
     * wins over the 565 form. CLASS.PAL entry 0 is rgb(4,8,7): 555 gives ((4>>3)<<10)|((8>>3)<<5)
     * |(7>>3) = 0x0020 while 565 gives ((4>>3)<<11)|((8>>2)<<5)|(7>>3) = 0x0040. */
    memset(g_aPalette, 0xAA, sizeof(g_aPalette));
    g_ffFormat = PUFO_FF_555 | PUFO_FF_565;
    Palette_Load("class.pal", 20);
    CheckEqU32(g_aPalette[20][0], 0x0020, "  555 is tested before 565 @ 0x3e47c: 555 gives (8>>3)<<5 = 0x20, 565 would give 0x40");
    CheckEqU32(g_aPalette[20][255], 0x7FFF, "  ... the 256th entry comes from file bytes 765..767 (MOV R2,#0x300 at 0x3E4AC)");

    /* No format bit set: the store is skipped for every entry, so the slot keeps what it had. */
    memset(g_aPalette, 0xAA, sizeof(g_aPalette));
    g_ffFormat = 0;
    Palette_Load("class.pal", 20);
    CheckEqU32(g_aPalette[20][0], 0xAAAAAAAAu, "  no format bit leaves every entry untouched (the store is inside the dispatch)");
    CheckEqU32(g_aPalette[20][255], 0xAAAAAAAAu, "  ... at both ends of the 256-entry loop");

    /* Slot stride: a slot is 256 entries (1024 bytes), so loading slot 20 must not touch slot 19
     * or 21 - the store address is 0x1472D0 + 4*(i + slot*256). */
    memset(g_aPalette, 0xAA, sizeof(g_aPalette));
    g_ffFormat = PUFO_FF_888;
    Palette_Load("class.pal", 20);
    CheckEqU32(g_aPalette[20][0], 0x00070804u, "  888 stores r | g<<8 | b<<16 (0x3E52C tail, shared with 565)");
    CheckEqU32(g_aPalette[19][255], 0xAAAAAAAAu, "  slot 19 is 256 entries below and is untouched");
    CheckEqU32(g_aPalette[21][0], 0xAAAAAAAAu, "  slot 21 is 256 entries above and is untouched");
    CheckEqU32(g_aPalette[20][255], 0x00FFFFFFu, "  ... the loop covers exactly 256 entries");

    g_ffFormat = nFmtSave;
}

/* The PadImage record and its four helpers: ctor @ 0x11000, FreeData @ 0x1100c, Load @ 0x11020 and the
 * keyed blit @ 0x11110. The record is 0x14 bytes -- operator new(0x14) -- with nOriginX +0, nOriginY +4,
 * uWidth +8, uHeight +0xA, pData +0xC and nMode +0x10. */
static void TestPadImageRecord(void)
{
    PadImage   pad;
    u8        *pRaw;
    static u8  aRec[4 + 2] = { 2, 0, 1, 0, 9, 7 };
    SoftSurf   surf;
    u16       *pPix;

    printf("PadImage record: ctor / free / load / keyed blit\n");

    /* 0x11000 is one store: `STR R1,[R0,#0xC]` with R1 = 0 at 0x11008. Nothing else in the record is
     * touched, which is why a ctor over a filled record must leave the fill alone. */
    pRaw = (u8 *)&pad;
    memset(pRaw, 0x5A, sizeof(pad));
    PadImage_ctor(&pad);
    Check(pad.pData == NULL, "  ctor clears pData @ 0x11000");
    Check(pad.nOriginX != 0 && pad.uWidth != 0 && pad.nMode != 0,
          "  ctor reaches no other field of the 0x14-byte record @ 0x11000");

    /* 0x1100c: `if (pPad->pData) operator delete(pPad->pData)` and nothing else -- the original leaves
     * the field pointing at freed memory, so a second call there is a second free. The port clears the
     * field, which is the stated deviation. */
    pad.pData = (u8 *)malloc(16);
    PadImage_FreeData(&pad);
    Check(pad.pData == NULL, "  FreeData releases the buffer and the port clears the field @ 0x1100c");
    PadImage_FreeData(&pad);
    Check(pad.pData == NULL, "  ... and the port's second call is a no-op @ 0x1100c");

    /* 0x11020: the old buffer goes first (the free at 0x1102C), then "<name>.pad" is looked up, and on a
     * hit the record is new(size) + Read + uWidth = the member's first u16 + uHeight = its second +
     * nMode = the caller's third argument. The original checks neither Find's nor Read's result. */
    memset(&pad, 0, sizeof(pad));
    pad.nMode = 0xEE;
    Check(PadImage_Load(&pad, "invv", 14), "  invv.pad loads @ 0x11020");
    Check(pad.pData != NULL, "  the member's bytes land in the record @ 0x11020");
    CheckEqI32(pad.uWidth, (i32)pad.pData[0] | ((i32)pad.pData[1] << 8),
               "  uWidth is the member's first little-endian u16 @ 0x11020");
    CheckEqI32(pad.uHeight, (i32)pad.pData[2] | ((i32)pad.pData[3] << 8),
               "  uHeight is its second u16, not the first repeated @ 0x11020");
    CheckEqI32(pad.nMode, 14, "  nMode is the third argument, not a byte off the member @ 0x11020");
    Check(!PadImage_Load(&pad, "nosuchpad", 3), "  a missing member is refused @ 0x11020");
    Check(pad.pData == NULL, "  the port leaves no buffer pointing at freed memory after a miss @ 0x11020");

    /* 0x11110 hands four things to SoftSurf_BlitIndexedKeyed @ 0x9ea34: the pad's own origin pair, the
     * buffer, nMode as the palette slot, and the caller's key. The two sizes it also passes are dead
     * arguments -- the callee reads the size from the glyph header. */
    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 4, 1, PIXMODE_16BPP);
    pPix = (u16 *)SoftSurf_GetPixels(&surf);
    pPix[0] = 0xDEAD; pPix[1] = 0xDEAD; pPix[2] = 0xDEAD; pPix[3] = 0xDEAD;
    memset(g_aPalette, 0, sizeof(g_aPalette));
    g_aPalette[3][7]  = 0x1234;
    g_aPalette[9][7]  = 0x5678;
    g_pMainSurface = &surf;

    memset(&pad, 0, sizeof(pad));
    pad.pData   = aRec;                      /* width 2, height 1, pixels 9 then 7 */
    pad.nMode   = 3;
    pad.nOriginX = 1;
    pad.nOriginY = 0;
    PadImage_BlitEx(&pad, 9);
    CheckEqU32(pPix[0], 0xDEAD, "  the origin x leaves column 0 alone @ 0x11110");
    CheckEqU32(pPix[1], 0xDEAD, "  the keyed index 9 is skipped @ 0x11110");
    CheckEqU32(pPix[2], 0x1234, "  the second pixel comes from the pad's nMode slot @ 0x11110");
    CheckEqU32(pPix[3], 0xDEAD, "  and the blit stops at the header's width of 2 @ 0x11110");

    pPix[2] = 0xDEAD;
    pad.nMode = 9;
    PadImage_BlitEx(&pad, 9);
    CheckEqU32(pPix[2], 0x5678, "  a different nMode picks a different slot @ 0x11110");

    g_pMainSurface = NULL;
    SoftSurf_dtor(&surf);
}

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }

    TestAlloc();
    TestSoftSurfObject();
    TestBlitStateLoading();
    TestPalette();
    TestBlitIndexed();
    TestBlitKeyed();
    TestFillRect();
    TestCopyRows();
    TestRectFillAndBlend();
    TestRectHelpers();
    TestSamplePixel();
    TestBlurPixels();
    TestBlitRawGlyph();
    TestPackRgb565Pair();
    TestPaletteLut();
    TestPaletteRamp();
    TestBoundWrappers();
    TestTwentyFourBppIndexed();
    TestHiResUpscale();
    TestPaletteDispatch();
    TestPadImageRecord();

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
