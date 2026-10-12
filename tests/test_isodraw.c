/* test_isodraw.c - the per-cell isometric glyph draw helpers
 * (MapView_DrawCellFloorGlyph @ 0x1c904, MapView_DrawCellOccupantGlyph @ 0x1b45c,
 * MapView_SetWallPalFromSlotA @ 0x1c7dc, MapView_SetWallPalFromSlotB @ 0x1c870,
 * TactUnit_DrawAnimGlyphs @ 0x1c6ac, TactUnit_DrawFacingGlyph @ 0x1c754).
 *
 * The wall-palette tests look at branching only. The two TactUnit helpers are index arithmetic, so
 * they run against a probe font that reports which glyph index was asked for. See InstallProbeFont. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/pufo_font.h"
#include "game/pufo_isodraw.h"
#include "game/pufo_unit.h"
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

static void TestResolve(void)
{
    /* The array is indexed 60 * (60 * nLayer + nMapX) + nMapY. Every slot points at one ordinary
     * cell except the one a given case is expected to land on, which points at a distinctive cell.
     * That makes the index arithmetic an exact assertion at both layer 0 and layer 1 without a pool
     * of four thousand cells. */
    static MapCell *apGrid[3844];
    static MapCell  aOrdinary;
    static MapCell  aTarget;
    static Mc1Rec   ceilSpr;
    IsoGrid grid;
    IsoDrawCtx iso;
    i32 k;

    printf("MapView_bResolveIsoCell @ 0x1b0d4\n");

    for (k = 0; k < 3844; ++k) {
        apGrid[k] = &aOrdinary;
    }
    grid.apCells = apGrid;
    grid.nDimX = 1;   /* the limit is 10 * 1 - 1 = 9 */
    grid.nDimY = 1;

    memset(&iso, 0, sizeof(iso));
    memset(&aOrdinary, 0, sizeof(aOrdinary));

    iso.nIsoOffA = 0;
    iso.nIsoOffB = 0;
    iso.nLayer = 0;
    iso.nTileX = 2;
    iso.nTileY = 4;
    apGrid[4] = &aTarget;

    Check(MapView_bResolveIsoCell(&iso, &grid), "an even tile resolves");
    CheckEqI32(iso.nMapY, 4, "nMapY = nIsoOffA + nTileX + nTileY / 2");
    CheckEqI32(iso.nMapX, 0, "nMapX = nTileY / 2 - nTileX + nIsoOffB");
    CheckEqI32(iso.nDrawX, 48, "nDrawX = 32 * nTileX - 16");
    CheckEqI32(iso.nDrawY, 0, "nDrawY = 8 * (nTileY - 3 * nLayer - 4)");
    CheckEqI32(iso.bOob, 0, "in bounds");
    Check(iso.pCell == &aTarget, "the cell lands at 60 * nMapX + nMapY for layer 0");
    apGrid[4] = &aOrdinary;

    /* Odd tile row: nMapX gains one and nDrawX shifts by a further 16. */
    iso.nTileY = 5;
    apGrid[64] = &aTarget;
    Check(MapView_bResolveIsoCell(&iso, &grid), "an odd tile resolves");
    CheckEqI32(iso.nMapX, 1, "an odd nTileY adds one to nMapX");
    CheckEqI32(iso.nMapY, 4, "nMapY is unchanged by the odd correction");
    CheckEqI32(iso.nDrawX, 32, "an odd nTileY shifts nDrawX to 32 * nTileX - 32");
    CheckEqI32(iso.nDrawY, 8, "nDrawY follows the new row");
    Check(iso.pCell == &aTarget, "the odd correction moves the cell by one column");
    apGrid[64] = &aOrdinary;

    /* A negative odd row: the divide truncates toward zero, and the odd correction still applies. */
    iso.nTileY = -5;
    iso.nTileX = 0;
    iso.nIsoOffA = 10;
    iso.nIsoOffB = 3;
    Check(!MapView_bResolveIsoCell(&iso, &grid),
          "a cell whose top is 40 or more pixels below the screen is skipped");
    CheckEqI32(iso.nMapY, 8, "-5 / 2 truncates toward zero, giving -2");
    CheckEqI32(iso.nMapX, 2, "the odd correction still applies to a negative row");
    CheckEqI32(iso.nDrawY, -72, "nDrawY is -72, so nDrawY + 40 is not positive");

    /* The cutoff is exactly 40: -40 fails and -32 passes. */
    iso.nTileY = -1;
    iso.nIsoOffA = 0;
    iso.nIsoOffB = 0;
    Check(!MapView_bResolveIsoCell(&iso, &grid), "nDrawY of exactly -40 is rejected");
    CheckEqI32(iso.nDrawY, -40, "nDrawY is exactly -40");

    iso.nTileY = 0;
    apGrid[0] = &aTarget;
    Check(MapView_bResolveIsoCell(&iso, &grid), "nDrawY of -32 is accepted");
    CheckEqI32(iso.nDrawY, -32, "nDrawY is -32");
    Check(iso.pCell == &aTarget, "tile row 0 at layer 0 lands on slot 0");

    /* A higher layer moves the window and the index: tile row 7 at layer 1 resolves to
     * nMapX 4, nMapY 3, which is slot 60 * (60 * 1 + 4) + 3 = 3843. */
    iso.nTileY = 7;
    iso.nLayer = 1;
    apGrid[3843] = &aTarget;
    Check(MapView_bResolveIsoCell(&iso, &grid), "a higher layer shifts the window");
    CheckEqI32(iso.nDrawY, 0, "layer 1 puts tile row 7 at nDrawY 0");
    CheckEqI32(iso.nMapX, 4, "nMapX for layer 1 tile row 7");
    CheckEqI32(iso.nMapY, 3, "nMapY for layer 1 tile row 7");
    Check(iso.pCell == &aTarget, "layer 1 indexes 60 * (60 * nLayer + nMapX) + nMapY");
    apGrid[3843] = &aOrdinary;
    iso.nLayer = 0;

    /* Out of bounds on each side. */
    iso.nTileY = 4;
    iso.nTileX = 8;
    Check(!MapView_bResolveIsoCell(&iso, &grid), "nMapY above 10 * nDimX - 1 is out of bounds");
    CheckEqI32(iso.nMapY, 10, "nMapY is 0 + 8 + 2 against a limit of 9");
    CheckEqI32(iso.bOob, 1, "bOob is set");

    iso.nTileX = 0;
    iso.nIsoOffB = 20;
    Check(!MapView_bResolveIsoCell(&iso, &grid), "nMapX above 10 * nDimY - 1 is out of bounds");
    CheckEqI32(iso.nMapX, 22, "nMapX is 22 against a limit of 9");

    iso.nIsoOffB = 0;
    iso.nIsoOffA = -10;
    Check(!MapView_bResolveIsoCell(&iso, &grid), "a negative nMapY is out of bounds");
    CheckEqI32(iso.bOob, 1, "bOob is set for a negative coordinate");

    /* nPal: 255 unless bFlags29 bit 4 is set, then 0 for animation kind 4 and bUnk56 otherwise.
     * These run on the tile that lands on slot 0, whose cell is aTarget. */
    iso.nIsoOffA = 0;
    iso.nIsoOffB = 0;
    iso.nTileX = 0;
    iso.nTileY = 0;
    memset(&aTarget, 0, sizeof(aTarget));
    aTarget.bFlags29 = 0;
    Check(MapView_bResolveIsoCell(&iso, &grid), "resolved for the palette tests");
    CheckEqI32(iso.nPal, 255, "no bFlags29 bit 4 gives palette 255");

    aTarget.bFlags29 = 0x10;
    aTarget.nFlags32 = 4;
    aTarget.bUnk56 = 7;
    (void)MapView_bResolveIsoCell(&iso, &grid);
    CheckEqI32(iso.nPal, 0, "bit 4 with animation kind 4 gives palette 0");

    aTarget.nFlags32 = 2;
    (void)MapView_bResolveIsoCell(&iso, &grid);
    CheckEqI32(iso.nPal, 7, "bit 4 with any other kind gives the cell's bUnk56");

    /* nObjParam is the ceiling sprite's signed iCeilParam, or zero with no ceiling sprite. */
    aTarget.pCeilSpr = NULL;
    (void)MapView_bResolveIsoCell(&iso, &grid);
    CheckEqI32(iso.nObjParam, 0, "no ceiling sprite gives nObjParam 0");

    ceilSpr.iCeilParam = -3;
    aTarget.pCeilSpr = &ceilSpr;
    (void)MapView_bResolveIsoCell(&iso, &grid);
    CheckEqI32(iso.nObjParam, -3, "iCeilParam is read as a signed byte");

    aTarget.pUnit = (Soldier *)&aTarget;
    (void)MapView_bResolveIsoCell(&iso, &grid);
    Check(iso.pUnit == (Soldier *)&aTarget, "pUnit is bound from the resolved cell");

    apGrid[0] = &aOrdinary;
}

static void TestSelectMarkers(void)
{
    MapCell       cell;
    Soldier       unit;
    IsoDrawCtx    iso;
    IsoSelectGate gate;
    IsoSpriteBases bases;

    printf("select markers @ 0x1b280 / 0x1b584\n");

    /* Cursor base 10 and glyph base 0 keep every index inside the 64-entry probe table, so the inked
     * row reports the index directly. */
    memset(&bases, 0, sizeof(bases));
    bases.nCursorBase = 10;
    bases.nGlyphBase = 0;

    memset(&gate, 0, sizeof(gate));
    gate.nMapLayers = 1;   /* layer 0 is the top layer */
    gate.nAnimPhase = 0;

    memset(&cell, 0, sizeof(cell));
    memset(&unit, 0, sizeof(unit));
    unit.bStatus = 0;      /* occupies and is operative */
    cell.pUnit = &unit;
    cell.bFlags57[0] = 8;  /* the flagged bit both markers need */

    memset(&iso, 0, sizeof(iso));
    iso.pCell = &cell;
    iso.nDrawX = 5;
    iso.nDrawY = 0;
    iso.nLayer = 0;

    /* The queue gate. */
    gate.bQueueActive = 1;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), -1, "a busy tactical queue suppresses the low marker");
    ClearSurface();
    MapView_DrawCellSelectMarkerHi(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), -1, "and the high one");
    gate.bQueueActive = 0;

    /* The gates differ: the low marker also fires on bFlags29 bit 6, the high one does not. */
    cell.bFlags57[0] = 0;
    cell.bFlags29 = 0x40;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    Check(InkRow(5) != -1, "bFlags29 bit 6 alone draws the low marker");
    ClearSurface();
    MapView_DrawCellSelectMarkerHi(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), -1, "but not the high marker");
    cell.bFlags57[0] = 8;
    cell.bFlags29 = 0;

    /* Mode 0 on the top layer, occupant target: A = phase >> 2. */
    gate.nActionLatch = 0;
    gate.nAnimPhase = 8;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 10 + 2, "mode 0 with an occupant target gives A = phase >> 2");

    /* No occupant gives A = 0. */
    cell.pUnit = NULL;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 10, "no occupant gives A = 0");
    cell.pUnit = &unit;

    /* Not the top layer, and no bFlags29 bit 6: A = 2 regardless of the mode. */
    iso.nLayer = 0;
    gate.nMapLayers = 2;
    gate.nActionLatch = 4;
    gate.nAnimPhase = 8;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 12, "a lower layer gives A = 2");
    gate.nMapLayers = 1;

    /* Mode 2 uses phase >> 1, not phase >> 2. */
    gate.nActionLatch = 2;
    gate.nAnimPhase = 4;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 10 + 9, "mode 2 gives A = (phase >> 1) + 7");

    cell.pUnit = NULL;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 16, "mode 2 without an occupant target gives A = 6");
    cell.pUnit = &unit;

    /* Mode 3 with bFlags29 bit 6: A = 7, and B is the multi-target slot plus the glyph base plus 49.
     * That B is absolute - it is drawn without adding the cursor base. */
    cell.bFlags29 = 0x40;
    gate.apMultiTargets[2] = &cell;
    gate.nActionLatch = 3;
    gate.nAnimPhase = 8;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 17, "mode 3 with bit 6 gives A = 7");
    Check(InkAt(5, 51) != 0, "B is the multi-target slot plus 49, drawn as an absolute index");
    CheckEqI32(InkAt(5, 61), 0, "and is not offset by the cursor base");

    /* A cell that is not in the list still gets a glyph, at index 8. */
    gate.apMultiTargets[2] = NULL;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    Check(InkAt(5, 57) != 0, "a miss still draws, at slot 8");
    cell.bFlags29 = 0;

    /* Mode 5 or above leaves A at whatever the previous cell stored. Reproduced, not tidied. */
    gate.nActionLatch = 5;
    iso.nGlyphA = 33;
    ClearSurface();
    MapView_DrawCellSelectMarker(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 43, "an unhandled mode draws with the stale context value");
    iso.nGlyphA = 0;

    /* The high marker. */
    gate.nActionLatch = 0;
    gate.nAnimPhase = 8;
    ClearSurface();
    MapView_DrawCellSelectMarkerHi(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 15, "mode 0 gives A = (phase >> 2) + 3");
    CheckEqI32(InkAt(5, 25), 0, "mode 0 draws no second glyph");

    gate.nActionLatch = 1;
    ClearSurface();
    MapView_DrawCellSelectMarkerHi(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 15, "mode 1 keeps the same A");
    Check(InkAt(5, 27) != 0, "mode 1 adds B = (phase >> 2) + 15, offset by the cursor base");

    gate.nActionLatch = 2;
    gate.nAnimPhase = 4;
    ClearSurface();
    MapView_DrawCellSelectMarkerHi(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 19, "mode 2 gives A = (phase >> 1) + 7");

    gate.nActionLatch = 3;
    gate.nAnimPhase = 8;
    ClearSurface();
    MapView_DrawCellSelectMarkerHi(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 15, "mode 3 keeps A = (phase >> 2) + 3");
    Check(InkAt(5, 25) != 0, "mode 3 adds B = (phase >> 2) + 13");

    gate.nActionLatch = 4;
    ClearSurface();
    MapView_DrawCellSelectMarkerHi(&iso, &gate, &bases);
    Check(InkAt(5, 23) != 0, "mode 4 adds B = (phase >> 2) + 11 instead");

    gate.nMapLayers = 2;
    gate.nActionLatch = 3;
    ClearSurface();
    MapView_DrawCellSelectMarkerHi(&iso, &gate, &bases);
    CheckEqI32(InkRow(5), 15, "a lower layer gives A = 5");
    CheckEqI32(InkAt(5, 25), 0, "and no second glyph");
}

static void TestHpGlyphs(void)
{
    MapCell      cell;
    ObjectRec    objHead;
    ObjectRec    objNext;
    TactItem     itemHead;
    TactItem     itemNext;
    IsoDrawCtx   iso;
    IsoSpriteBases bases;

    printf("TactUnit_DrawHpGlyphs @ 0x1b4b0\n");

    memset(&bases, 0, sizeof(bases));
    bases.nBulletsBase = 10;
    bases.nFloorobBase = 20;

    memset(&cell, 0, sizeof(cell));
    memset(&objHead, 0, sizeof(objHead));
    memset(&objNext, 0, sizeof(objNext));
    memset(&itemHead, 0, sizeof(itemHead));
    memset(&itemNext, 0, sizeof(itemNext));

    itemHead.pObj = &objHead;
    itemHead.pNext = &itemNext;
    itemNext.pObj = &objNext;
    itemNext.pNext = NULL;
    cell.pItemList = &itemHead;

    memset(&iso, 0, sizeof(iso));
    iso.pCell = &cell;
    iso.nDrawX = 5;
    iso.nDrawY = 0;
    iso.nObjParam = 0;
    iso.nPal = 0;

    /* Head is not two-handed-exclusive, so the bullets glyph is drawn immediately, and the count
     * glyph follows at floorob base + (value - 1). */
    objHead.bTwoHanded = 1;
    objHead.aItemQty[50] = 3;
    ClearSurface();
    TactUnit_DrawHpGlyphs(&iso, &bases);
    CheckEqI32(InkRow(5), 17, "bullets base + 7 drawn for a non two-handed-exclusive head");
    Check(InkAt(5, 22) != 0, "count glyph at floorob base + (3 - 1)");
    CheckEqI32(iso.nHpGlyph, 2, "nHpGlyph holds value - 1");

    /* The walk skips leading two-handed-exclusive items, but the COUNT still comes from the head.
     * The found item holds a different value, so a port that used the cursor would draw at 28. */
    objHead.bTwoHanded = 2;
    objHead.aItemQty[50] = 1;
    objNext.bTwoHanded = 0;
    objNext.aItemQty[50] = 9;
    ClearSurface();
    TactUnit_DrawHpGlyphs(&iso, &bases);
    CheckEqI32(InkRow(5), 17, "the walk still draws the bullets glyph at the found item");
    Check(InkAt(5, 20) != 0, "but the count comes from the HEAD item");
    CheckEqI32(InkAt(5, 28), 0, "not from the item the walk stopped at");

    /* Every item two-handed-exclusive: the walk runs off the end, the bullets glyph is skipped, and
     * the count glyph is still drawn. */
    objHead.bTwoHanded = 2;
    objNext.bTwoHanded = 2;
    ClearSurface();
    TactUnit_DrawHpGlyphs(&iso, &bases);
    CheckEqI32(InkRow(5), 20, "an exhausted walk skips the bullets glyph but not the count");

    /* A count of zero draws nothing. */
    objHead.bTwoHanded = 1;
    objHead.aItemQty[50] = 0;
    ClearSurface();
    TactUnit_DrawHpGlyphs(&iso, &bases);
    CheckEqI32(InkRow(5), 17, "the bullets glyph is drawn regardless of the count");
    CheckEqI32(InkAt(5, 19), 0, "a count of zero draws no second glyph");
    CheckEqI32(iso.nHpGlyph, 0, "nHpGlyph holds 0");

    /* No items at all. */
    cell.pItemList = NULL;
    ClearSurface();
    TactUnit_DrawHpGlyphs(&iso, &bases);
    CheckEqI32(InkRow(5), -1, "an empty item list draws nothing");

    /* Both glyphs land at nObjParam + nDrawY, not at nDrawY alone. */
    cell.pItemList = &itemHead;
    objHead.bTwoHanded = 1;
    objHead.aItemQty[50] = 1;
    iso.nObjParam = -6;
    ClearSurface();
    TactUnit_DrawHpGlyphs(&iso, &bases);
    CheckEqI32(InkRow(5), 17 - 6, "the draw y is nObjParam + nDrawY");
}

static void TestViewportGuide(void)
{
    IsoViewportGuide g;
    u16 *pPix;

    printf("MapView_DrawViewportGuide @ 0x1aadc\n");

    memset(&g, 0, sizeof(g));
    g.bActionReady = 1;
    g.bPlayerInputEnabled = 1;
    g.bDeployPanelOpen = 0;
    g.nHudPadHeight = 20;   /* v2 = 300, v3 = 100, v4 = 200 */

    /* The guide blits from the surface to the surface, so the test fills the surface with a marker
     * colour and then clears the probe pixels it expects to be repainted. The source tile at
     * (172,184) keeps the marker, so any destination the guide touches comes back non-zero.
     * SoftSurf_AllocPixels frees the old pixels when they are non-NULL, so it doubles as the
     * reallocation here. */
    SoftSurf_AllocPixels(&g_surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_surf;
    pPix = (u16 *)g_surf.pPixels;
    for (i32 i = 0; i < 240 * 320; ++i) { pPix[i] = 0x1234; }

    /* Gate: every one of the four conditions must hold. */
    g.bGameTickHold = 1;
    pPix[15 * 240 + 15] = 0;
    MapView_DrawViewportGuide(&g);
    CheckEqI32((i32)pPix[15 * 240 + 15], 0, "a held game tick suppresses the guide");
    g.bGameTickHold = 0;

    g.bCursorBusy = 1;
    pPix[15 * 240 + 15] = 0;
    MapView_DrawViewportGuide(&g);
    CheckEqI32((i32)pPix[15 * 240 + 15], 0, "a busy cursor suppresses the guide");
    g.bCursorBusy = 0;

    g.bActionReady = 0;
    pPix[15 * 240 + 15] = 0;
    MapView_DrawViewportGuide(&g);
    CheckEqI32((i32)pPix[15 * 240 + 15], 0, "no action phase suppresses the guide");
    g.bActionReady = 1;

    g.bPlayerInputEnabled = 0;
    pPix[15 * 240 + 15] = 0;
    MapView_DrawViewportGuide(&g);
    CheckEqI32((i32)pPix[15 * 240 + 15], 0, "player input off suppresses the guide");
    g.bPlayerInputEnabled = 1;

    /* The frame itself. SoftSurf_BlitSrcRect blends source and destination by the colour key rather
     * than copying, so the value that lands depends on how many segments cover a pixel - 813 for one
     * pass, 1138 for two. What is assertable is that a covered pixel is no longer empty. */
    g.nUiSlot5 = 0;
    pPix[15 * 240 + 15] = 0;
    pPix[15 * 240 + 225] = 0;
    pPix[285 * 240 + 15] = 0;   /* y = v2 - 15 */
    pPix[300 * 240 + 0] = 0;    /* the full-width bottom line at y = v2 */
    pPix[0 * 240 + 80] = 0;     /* the x 80 tick at the top */
    MapView_DrawViewportGuide(&g);
    Check(pPix[15 * 240 + 15] != 0, "the frame's top-left corner");
    Check(pPix[15 * 240 + 225] != 0, "and its top-right");
    Check(pPix[285 * 240 + 15] != 0, "and the bottom of the left side at v2 - 15");
    Check(pPix[300 * 240 + 0] != 0, "and the full-width line at y = v2");
    Check(pPix[0 * 240 + 80] != 0, "and the tick at x 80");

    /* No edge flag draws no indicator. (0, v3 + 1) is inside the left bar but outside every frame
     * segment, so it reports the indicator alone. */
    pPix[101 * 240 + 0] = 0;
    MapView_DrawViewportGuide(&g);
    CheckEqI32((i32)pPix[101 * 240 + 0], 0, "no edge flag draws no indicator");

    g.nUiSlot5 = 0x000000FFu;   /* left */
    pPix[101 * 240 + 0] = 0;
    MapView_DrawViewportGuide(&g);
    Check(pPix[101 * 240 + 0] != 0, "the left flag draws a bar between the third lines");

    g.nUiSlot5 = 0x0000FF00u;   /* right */
    pPix[101 * 240 + 0] = 0;
    pPix[101 * 240 + 225] = 0;
    MapView_DrawViewportGuide(&g);
    CheckEqI32((i32)pPix[101 * 240 + 0], 0, "the right flag does not draw on the left edge");
    Check(pPix[101 * 240 + 225] != 0, "and draws on the right edge");

    /* Priority: bottom wins over top, top over left. The bottom bar starts at v2 - v3 = 200. */
    g.nUiSlot5 = 0xFF00FFFFu;   /* bottom, top and left all set */
    pPix[200 * 240 + 0] = 0;
    pPix[101 * 240 + 0] = 0;    /* the left bar would cover this */
    MapView_DrawViewportGuide(&g);
    Check(pPix[200 * 240 + 0] != 0, "bottom takes priority over top and left");
    CheckEqI32((i32)pPix[101 * 240 + 0], 0, "so the left bar is left alone");

    /* Top with no left or right flag gives the centre cap at (80,0), not a side bar. Add the left
     * flag and the top branch draws the left bar from y 15 instead. */
    g.nUiSlot5 = 0x00FF00FFu;   /* top and left */
    pPix[16 * 240 + 0] = 0;
    MapView_DrawViewportGuide(&g);
    Check(pPix[16 * 240 + 0] != 0, "top with left draws the bar from y 15, not between the thirds");
    CheckEqI32((i32)pPix[101 * 240 + 0], 0, "and not at the third line");

    /* The deploy panel changes v2, and with it every third line: v2 = 303, v3 = 101. */
    g.nUiSlot5 = 0x000000FFu;
    g.bDeployPanelOpen = 1;
    pPix[102 * 240 + 0] = 0;
    pPix[100 * 240 + 0] = 0;
    MapView_DrawViewportGuide(&g);
    Check(pPix[102 * 240 + 0] != 0, "the deploy panel moves v3 to 101, so the bar starts there");
    CheckEqI32((i32)pPix[100 * 240 + 0], 0, "and one row earlier is untouched");
}

static void TestIsoConstants(void)
{
    printf("iso projection constants @ 0x37098 / 0x370e8\n");

    Math_StoreSqrtConst();
    Math_StoreAtan2Const();

    /* Asserted as derived facts rather than restatements of the calls. */
    Check(fabs(g_dblIsoScale - 1.1180339887498949) < 1e-12, "scale is sqrt(320)/16, which is sqrt(5)/2");
    Check(fabs(tan(g_dblIsoAngle) - 2.0) < 1e-12,
          "the angle's tangent is 2, matching the 32 by 16 iso tile");
    Check(g_dblIsoAngle > 1.107148717794 && g_dblIsoAngle < 1.107148717795,
          "the angle is atan2(32,16), about 63.43 degrees");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    InstallProbeFont();

    TestWallPals();
    TestNulls();
    TestAnimGlyphs();
    TestFacingGlyph();
    TestResolve();
    TestSelectMarkers();
    TestHpGlyphs();
    TestViewportGuide();
    TestIsoConstants();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
