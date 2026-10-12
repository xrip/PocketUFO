/* test_isodraw.c - the per-cell isometric glyph draw helpers
 * (MapView_DrawCellFloorGlyph @ 0x1c904, MapView_DrawCellOccupantGlyph @ 0x1b45c,
 * MapView_SetWallPalFromSlotA @ 0x1c7dc, MapView_SetWallPalFromSlotB @ 0x1c870,
 * TactUnit_DrawAnimGlyphs @ 0x1c6ac, TactUnit_DrawFacingGlyph @ 0x1c754).
 *
 * The wall-palette tests look at branching only. The two TactUnit helpers are index arithmetic, so
 * they run against a probe font that reports which glyph index was asked for. See InstallProbeFont. */
#include <stdio.h>
#include <string.h>

#include "game/pufo_font.h"
#include "game/pufo_isodraw.h"
#include "render/pufo_render.h"

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

static void TestWallPals(void)
{
    MapCell cell;
    Mc1Rec  wallA;
    Mc1Rec  wallB;
    IsoDrawCtx iso;
    IsoAnimGlobals anim;

    printf("MapView_SetWallPalFromSlotA/B\n");
    memset(&cell, 0, sizeof(cell));
    memset(&wallA, 0, sizeof(wallA));
    memset(&wallB, 0, sizeof(wallB));
    memset(&iso, 0, sizeof(iso));
    anim.nAnimPhase = 11;
    anim.nAnimPhaseScratch0 = 3;
    iso.pCell = &cell;
    iso.nPal = 0;

    /* bBlocksMove clear -> the animation phase is used. */
    cell.pWallSprA = &wallA;
    wallA.bBlocksMove = 0;
    MapView_SetWallPalFromSlotA(&iso, &anim);
    CheckEqI32(iso.bWallPalIdx, 11, "wall A: phase when bBlocksMove clear");

    /* bBlocksMove set, flag bit 0 -> the animation scratch byte. */
    wallA.bBlocksMove = 1;
    cell.bFlags29 = 1;
    MapView_SetWallPalFromSlotA(&iso, &anim);
    CheckEqI32(iso.bWallPalIdx, 3, "wall A: scratch byte on flag bit 0");

    /* flag bit 2 -> 7. */
    cell.bFlags29 = 4;
    MapView_SetWallPalFromSlotA(&iso, &anim);
    CheckEqI32(iso.bWallPalIdx, 7, "wall A: 7 on flag bit 2");

    /* neither flag -> 0. */
    cell.bFlags29 = 0;
    MapView_SetWallPalFromSlotA(&iso, &anim);
    CheckEqI32(iso.bWallPalIdx, 0, "wall A: 0 with no flag");

    /* bit 0 wins over bit 2. */
    cell.bFlags29 = 5;
    MapView_SetWallPalFromSlotA(&iso, &anim);
    CheckEqI32(iso.bWallPalIdx, 3, "wall A: bit 0 wins over bit 2");

    /* Wall B uses the same rules on its own sprite. */
    cell.pWallSprB = &wallB;
    cell.bFlags29 = 4;
    wallB.bBlocksMove = 1;
    MapView_SetWallPalFromSlotB(&iso, &anim);
    CheckEqI32(iso.bWallPalIdx, 7, "wall B: 7 on flag bit 2");
}

static void TestNulls(void)
{
    MapCell cell;
    IsoDrawCtx iso;
    IsoAnimGlobals anim;

    printf("isodraw NULL tolerance\n");
    memset(&cell, 0, sizeof(cell));
    memset(&iso, 0, sizeof(iso));
    memset(&anim, 0, sizeof(anim));
    iso.pCell = &cell;

    MapView_DrawCellFloorGlyph(&iso, &anim);
    MapView_DrawCellOccupantGlyph(&iso, &anim);
    MapView_SetWallPalFromSlotA(&iso, &anim);
    MapView_SetWallPalFromSlotB(&iso, &anim);
    Check(true, "NULL sprites are safe");

    MapView_DrawCellFloorGlyph(NULL, &anim);
    MapView_SetWallPalFromSlotA(&iso, NULL);
    Check(true, "NULL context / anim globals are safe");
}

/* A probe font, so the index arithmetic these helpers compute is observable.
 *
 * Font_BlitGlyph reads a command stream: byte 0 is the start Y offset, byte 1 the start X offset,
 * then one byte per pixel until 0xFF. Glyph k therefore draws one pixel at (x, y + k), which makes
 * the row that receives ink a direct report of which glyph index was asked for.
 *
 * The existing wall-palette tests note that a draw is a no-op with no loaded font. That is true of
 * the original behaviour, and it is why the index rules were previously untested. */
static u8     g_aProbeGlyph[64][4];
static u8    *g_apProbeSpr[64];
static SoftSurf g_surf;

static void InstallProbeFont(void)
{
    i32 k;

    for (k = 0; k < 64; ++k) {
        g_aProbeGlyph[k][0] = (u8)k;
        g_aProbeGlyph[k][1] = 0;
        g_aProbeGlyph[k][2] = 0x30;
        g_aProbeGlyph[k][3] = 0xFF;
        g_apProbeSpr[k] = g_aProbeGlyph[k];
    }
    g_fontSys.apSprIndex = g_apProbeSpr;
    g_fontSys.nSprIndexCount = 64;
    g_fontSys.nGlyphBase = 0;

    /* Font_DrawGlyph clamps nPal above 19 down to 19, and indexes the slot with (nPal + 0) & 63.
     * Slot 0 is the one the probe glyph is visible in; slot 19 is deliberately left zero so that a
     * helper passing the cell palette through is distinguishable from one hard-wired to 0. */
    memset(g_aPalette[0], 0x7F, sizeof(g_aPalette[0]));
    memset(g_aPalette[19], 0, sizeof(g_aPalette[19]));

    /* Font_BlitGlyph refuses to draw unless the blit state's pixel mode is 1 or 2, and
     * Blit_LoadSurface does not set that field. */
    g_blitState.nPixelMode = 2;

    SoftSurf_ctor(&g_surf);
    SoftSurf_AllocPixels(&g_surf, 64, 128, PIXMODE_16BPP);
    g_pMainSurface = &g_surf;
}

static void ClearSurface(void)
{
    memset(g_surf.pPixels, 0, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);
}

/* The first row in column x that holds ink, or -1 for none. */
static i32 InkRow(i32 x)
{
    const u16 *p = (const u16 *)g_surf.pPixels;
    i32        y;

    for (y = 0; y < g_surf.nHeight; ++y) {
        if (p[y * g_surf.nWidth + x] != 0) {
            return y;
        }
    }
    return -1;
}

static i32 InkAt(i32 x, i32 y)
{
    const u16 *p = (const u16 *)g_surf.pPixels;

    return (i32)p[y * g_surf.nWidth + x];
}

static void TestAnimGlyphs(void)
{
    MapCell cell;
    IsoDrawCtx iso;
    IsoAnimGlobals anim;
    IsoSpriteBases bases;

    printf("TactUnit_DrawAnimGlyphs @ 0x1c6ac\n");

    memset(&cell, 0, sizeof(cell));
    memset(&iso, 0, sizeof(iso));
    memset(&anim, 0, sizeof(anim));
    memset(&bases, 0, sizeof(bases));
    bases.nSmokeBase = 8;

    iso.pCell = &cell;
    iso.nDrawX = 10;
    iso.nDrawY = 0;
    iso.nPal = 0;
    anim.nAnimPhase = 0;

    cell.nFlags32 = 0;
    ClearSurface();
    TactUnit_DrawAnimGlyphs(&iso, &anim, &bases);
    CheckEqI32(InkRow(10), -1, "kind 0 draws nothing");

    /* kind 1 -> table offset 8, plus phase/2 (0), plus smoke base 8 = index 16. */
    cell.nFlags32 = 1;
    ClearSurface();
    TactUnit_DrawAnimGlyphs(&iso, &anim, &bases);
    CheckEqI32(InkRow(10), 16, "kind 1 uses table offset 8");

    /* kind 2 -> 12, phase 4 -> 2, base 8 = 22. */
    cell.nFlags32 = 2;
    anim.nAnimPhase = 4;
    ClearSurface();
    TactUnit_DrawAnimGlyphs(&iso, &anim, &bases);
    CheckEqI32(InkRow(10), 22, "half the animation phase is added");
    anim.nAnimPhase = 0;

    /* kind 5 -> table offset 4, so 4 + 0 + 8 = 12. */
    cell.nFlags32 = 5;
    ClearSurface();
    TactUnit_DrawAnimGlyphs(&iso, &anim, &bases);
    CheckEqI32(InkRow(10), 12, "kind 5 uses table offset 4");

    /* kind 4 is the one that draws twice: its own offset is 0, and the second glyph uses the entry
     * for kind 1, which is 8. */
    cell.nFlags32 = 4;
    ClearSurface();
    TactUnit_DrawAnimGlyphs(&iso, &anim, &bases);
    CheckEqI32(InkRow(10), 8, "kind 4 draws its own glyph at offset 0");
    Check(InkAt(10, 16) != 0, "and a second glyph at offset 8 as well");

    /* The kind is the low 3 bits only: 13 is the same as 5. */
    cell.nFlags32 = 13;
    ClearSurface();
    TactUnit_DrawAnimGlyphs(&iso, &anim, &bases);
    CheckEqI32(InkRow(10), 12, "the kind is flags & 7, so 13 behaves as 5");

    /* This helper follows the cell palette. Slot 19 is zeroed, so a cell palette of 255 (clamped to
     * 19) draws an invisible colour. */
    cell.nFlags32 = 1;
    iso.nPal = 255;
    ClearSurface();
    TactUnit_DrawAnimGlyphs(&iso, &anim, &bases);
    CheckEqI32(InkRow(10), -1, "the animation glyph follows the cell palette");
    iso.nPal = 0;
}

static void TestFacingGlyph(void)
{
    MapCell cell;
    IsoDrawCtx iso;
    IsoAnimGlobals anim;
    IsoSpriteBases bases;

    printf("TactUnit_DrawFacingGlyph @ 0x1c754\n");

    memset(&cell, 0, sizeof(cell));
    memset(&iso, 0, sizeof(iso));
    memset(&anim, 0, sizeof(anim));
    memset(&bases, 0, sizeof(bases));
    bases.nExplBase = 20;

    iso.pCell = &cell;
    iso.nDrawX = 60;   /* the glyph lands at 60 - 48 = 12 */
    iso.nDrawY = 60;   /* and at 60 - 12 = 48, so index k lands on row 48 + k */
    iso.nPal = 255;    /* deliberately not 0: this helper ignores it */
    anim.bPhaseActive = 1;

    cell.nFlags32 = 0x40;
    ClearSurface();
    TactUnit_DrawFacingGlyph(&iso, &anim, &bases);
    CheckEqI32(InkRow(12), 48 + 20, "field 0x40 draws at expl base - 8 + 8");

    cell.nFlags32 = 0x18;
    ClearSurface();
    TactUnit_DrawFacingGlyph(&iso, &anim, &bases);
    CheckEqI32(InkRow(12), 48 + 25, "the index counts down as the field grows");

    /* 0x48 masked by 0x78 is 0x48, which is above the 0x40 cutoff. */
    cell.nFlags32 = 0x48;
    ClearSurface();
    TactUnit_DrawFacingGlyph(&iso, &anim, &bases);
    CheckEqI32(InkRow(12), -1, "a field above 0x40 draws nothing");

    cell.nFlags32 = 0x80;
    ClearSurface();
    TactUnit_DrawFacingGlyph(&iso, &anim, &bases);
    CheckEqI32(InkRow(12), -1, "a field of zero draws nothing");

    anim.bPhaseActive = 0;
    cell.nFlags32 = 0x40;
    ClearSurface();
    TactUnit_DrawFacingGlyph(&iso, &anim, &bases);
    CheckEqI32(InkRow(12), -1, "the phase-active flag gates the whole thing");
    anim.bPhaseActive = 1;

    /* Unlike its siblings this one always passes palette 0, so it is visible even though the cell
     * palette is the zeroed slot 19. */
    cell.nFlags32 = 0x40;
    ClearSurface();
    TactUnit_DrawFacingGlyph(&iso, &anim, &bases);
    Check(InkRow(12) != -1, "it uses palette 0 regardless of the cell palette");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    InstallProbeFont();

    TestWallPals();
    TestNulls();
    TestAnimGlyphs();
    TestFacingGlyph();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
