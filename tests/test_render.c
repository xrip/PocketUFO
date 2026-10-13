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

    printf("Gfx_PackRgb565Pair\n");

    /* nScaleSource 3 gives factor 3 * 0.1 + 0.7 = 1.0, so the channels pass through at their
     * natural 5/6/5 widths: 255 >> 3 is 31, 255 >> 2 is 63. */
    env.nScaleSource = 3;
    env.nFadeCount   = 0;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 255, 255, 255, 128, 128, 128), 0x8410,
               "  returns the SECOND packed colour, not the first");

    /* A scale source of 10 gives factor 1.7: 31/1.7 = 18, 63/1.7 = 37. Both colours are the
     * same here because the return value is the second one. */
    env.nScaleSource = 10;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 255, 255, 255, 255, 255, 255) & 0xFFFF, 0x94B2,
               "  factor 1.7 scales every channel");

    /* A fade divides again, and the inner division is integer: (11 - 5) / 2 + 1 = 4.
     * 31/4 = 7, 63/4 = 15. */
    env.nScaleSource = 3;
    env.nFadeCount   = 5;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 255, 255, 255, 255, 255, 255) & 0xFFFF, 0x39E7,
               "  fade divides a second time by (11 - nFade)/2 + 1");

    /* Clamps are 5 / 6 / 5 bits. */
    env.nScaleSource = 3;
    env.nFadeCount   = 0;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 1000, 1000, 1000, 1000, 1000, 1000) & 0xFFFF, 0xFFFF,
               "  channels clamp to 31 / 63 / 31");

    /* Gate: the helper only runs for a 565 display format. */
    g_ffFormat = PUFO_FF_888;
    CheckEqU32(Gfx_PackRgb565Pair(&env, 255, 255, 255, 255, 255, 255), 0,
               "  does nothing unless the format has the 565 bit");
    g_ffFormat = nSavedFormat;

    /* Green is shifted by 2 and red and blue by 3 - the widths are 6, 5 and 5. */
    env.nScaleSource = 3;
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
        Tact_BuildPaletteLuts(nGamma);
        CheckEqU32(g_aPalette[0][0], aWant565[nGamma][0], "lut 565 R7 G15 B7");
        CheckEqU32(g_aPalette[0][1], aWant565[nGamma][1], "lut 565 white");
        CheckEqU32(g_aPalette[0][2], aWant565[nGamma][2], "lut 565 blue 1");
    }

    /* gamma 3 is the neutral setting: divisor is exactly 1.0, so the pass must be the identity. */
    g_aPalette[0][0] = 0x39E7;
    Tact_BuildPaletteLuts(3);
    CheckEqU32(g_aPalette[0][0], 0x39E7, "lut 565 gamma 3 is identity");
    CheckEqU32(g_aPalette[57][0], 0xBEEF, "lut covers slots 0..56 only");

    /* 15bpp RGB555. */
    g_ffFormat = PUFO_FF_555;
    for (nGamma = 0; nGamma <= PUFO_PAL_GAMMA_MAX; ++nGamma) {
        g_aPalette[0][0] = 0x7FFF;
        g_aPalette[0][1] = 0x1234;
        Tact_BuildPaletteLuts(nGamma);
        CheckEqU32(g_aPalette[0][0], aWant555[nGamma][0], "lut 555 white");
        CheckEqU32(g_aPalette[0][1], aWant555[nGamma][1], "lut 555 0x1234");
    }

    /* ORIGINAL BUG (0x37718): the 888 branch reads HIBYTE(w) and (u8)w, so it only ever sees the low
     * 16 bits, and it writes back 16 bits. Palette_Load packs 888 as R | G<<8 | B<<16, so on a 24-bit
     * display this drops the blue channel out of every palette. Reproduced, so the test pins it. */
    g_ffFormat = PUFO_FF_888;
    g_aPalette[0][0] = 0x00AABB1234ULL & 0xFFFFFFFFu;   /* R 0x34, G 0x12, B 0xBB, A 0xAA in the top byte */
    Tact_BuildPaletteLuts(3);
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

    Tact_LoadPalettes(3);   /* gamma 3: the LUT pass is the identity, so the ramp is all that is visible */

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

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }

    TestAlloc();
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

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
