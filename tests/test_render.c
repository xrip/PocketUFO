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

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
