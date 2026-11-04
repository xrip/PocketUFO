/* test_isodraw.c - the per-cell isometric glyph draw helpers
 * (MapView_DrawCellFloorGlyph @ 0x1c904, MapView_DrawCellOccupantGlyph @ 0x1b45c,
 * MapView_SetWallPalFromSlotA @ 0x1c7dc, MapView_SetWallPalFromSlotB @ 0x1c870,
 * TactUnit_DrawAnimGlyphs @ 0x1c6ac, TactUnit_DrawFacingGlyph @ 0x1c754).
 *
 * The wall-palette tests look at branching only. The two TactUnit helpers are index arithmetic, so
 * they run against a probe font that reports which glyph index was asked for. See InstallProbeFont.
 *
 * MapView_DrawUnit @ 0x1c958 is covered through the IsoDrawCtx it leaves behind: the direction bytes,
 * the walk state, nObjParam, nPal and the selection pad's origin. Ink is used only for the two cursor
 * markers, where the question is whether a glyph reached Font_DrawGlyph at all. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/pufo_font.h"
#include "game/pufo_isodraw.h"
#include "game/pufo_mapctx.h" /* PUFO_MAP_CELL_DIM, the minimap grid extent */
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
    CheckEqI32((i32)pPix[15 * 240 + 15], 0, "showborder off suppresses the guide");
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
    printf("iso projection constants at process start (CRT stores 0x37098 / 0x370e8)\n");

    /* Asserted as derived facts rather than restatements of the calls. */
    Check(g_dblIsoScale != 0.0 && g_dblIsoAngle != 0.0,
          "both CRT-stored projection values are present before any draw test");
    Check(fabs(g_dblIsoScale - 1.1180339887498949) < 1e-12, "scale is sqrt(320)/16, which is sqrt(5)/2");
    Check(fabs(tan(g_dblIsoAngle) - 2.0) < 1e-12,
          "the angle's tangent is 2, matching the 32 by 16 iso tile");
    Check(g_dblIsoAngle > 1.107148717794 && g_dblIsoAngle < 1.107148717795,
          "the angle is atan2(32,16), about 63.43 degrees");
}

static void TestHeightGlyph(void)
{
    IsoDrawCtx     iso;
    IsoHeightGate  gate;
    IsoSpriteBases bases;

    printf("MapView_DrawUnitHeightGlyph @ 0x1c240\n");

    memset(&iso, 0, sizeof(iso));
    memset(&gate, 0, sizeof(gate));
    memset(&bases, 0, sizeof(bases));

    /* The glyph is drawn with palette 1, and InstallProbeFont inks slot 0. Swap that: ink slot 1 and
     * clear slot 0, so ink can only appear if palette 1 really was the one passed through. */
    memset(g_aPalette[0], 0, sizeof(g_aPalette[0]));
    memset(g_aPalette[1], 0x7F, sizeof(g_aPalette[1]));

    iso.nDrawX = 100;
    iso.nDrawY = 60;
    iso.nMapX  = 7;
    iso.nMapY  = 5;
    iso.nLayer = 2;

    gate.bFireStatus2      = 1;
    gate.bFireStatus9      = 1;
    gate.nFireTgtX         = 5; /* compared against nMapY */
    gate.nFireTgtY         = 7; /* compared against nMapX */
    gate.nFireTgtZ         = 2;
    gate.nHeightGlyphWord  = 3;
    gate.nHeightSrcX       = 50; /* % 16 = 2 */
    gate.nHeightSrcY       = 20; /* % 16 = 4 */
    gate.nHeightSrcZ       = 7;  /* % 12 = 7 */
    gate.nFireTickProgress = 5;
    bases.nSmokeBase       = 0;

    ClearSurface();
    CheckEqI32(MapView_DrawUnitHeightGlyph(&iso, &gate, &bases), 1,
               "the gate passes and the glyph is drawn");
    CheckEqI32(iso.nHeightOff, -2, "nHeightOff is A - B, which is 2 - 4");
    CheckEqI32(iso.nHeightAux, -1,
               "nHeightAux is (A + B)/2 - 2C + 10, which is 3 - 14 + 10");

    /* Index = 10*3 + 5 + 0 + 16 = 51, and probe glyph 51 inks the row at y + 51. The drawn position
     * is (nHeightOff + nDrawX, nDrawY + nHeightAux + 15) = (98, 74), so one reading reports both the
     * index and the position. */
    CheckEqI32(InkRow(98), 74 + 51, "the glyph is index 51 drawn at x 98, y 74");
    CheckEqI32(InkRow(97), -1, "nothing is drawn one column to the left");

    /* Each gate condition on its own. */
    gate.bFireStatus2 = 0;
    CheckEqI32(MapView_DrawUnitHeightGlyph(&iso, &gate, &bases), 0, "bFireStatus2 clear blocks it");
    gate.bFireStatus2 = 1;

    gate.bFireStatus9 = 0;
    CheckEqI32(MapView_DrawUnitHeightGlyph(&iso, &gate, &bases), 0, "bFireStatus9 clear blocks it");
    gate.bFireStatus9 = 1;

    gate.nFireTgtY = 8;
    CheckEqI32(MapView_DrawUnitHeightGlyph(&iso, &gate, &bases), 0,
               "nMapX must equal nFireTgtY, not nFireTgtX");
    gate.nFireTgtY = 7;

    gate.nFireTgtX = 6;
    CheckEqI32(MapView_DrawUnitHeightGlyph(&iso, &gate, &bases), 0,
               "nMapY must equal nFireTgtX");
    gate.nFireTgtX = 5;

    gate.nFireTgtZ = 3;
    CheckEqI32(MapView_DrawUnitHeightGlyph(&iso, &gate, &bases), 0, "nLayer must equal nFireTgtZ");
    gate.nFireTgtZ = 2;

    gate.nHeightGlyphWord = 0;
    CheckEqI32(MapView_DrawUnitHeightGlyph(&iso, &gate, &bases), 0,
               "a zero height glyph id blocks it");
    gate.nHeightGlyphWord = 3;

    /* The gate tests the whole dword while the index reads the low half, so a value that is only
     * non-zero in the high half passes the gate and then contributes nothing to the index. */
    ClearSurface();
    gate.nHeightGlyphWord = 0x00010000u;
    CheckEqI32(MapView_DrawUnitHeightGlyph(&iso, &gate, &bases), 1,
               "ORIGINAL BUG (0x1C2B4), reproduced: the gate tests the dword but the index reads a u16");
    CheckEqI32(InkRow(98), 74 + 21, "and the high half contributes nothing to the index");
    gate.nHeightGlyphWord = 3;

    /* Negative operands: C's % truncates toward zero, matching the ARM modulo idiom. */
    ClearSurface();
    gate.nHeightSrcX = -50; /* % 16 = -2 */
    gate.nHeightSrcY = 20;  /* % 16 = 4  */
    CheckEqI32(MapView_DrawUnitHeightGlyph(&iso, &gate, &bases), 1, "a negative source still draws");
    CheckEqI32(iso.nHeightOff, -6, "-50 % 16 is -2, so nHeightOff is -2 - 4");
    gate.nHeightSrcX = 50;

    memset(g_aPalette[1], 0, sizeof(g_aPalette[1]));
    memset(g_aPalette[0], 0x7F, sizeof(g_aPalette[0]));
}

/* ------------------------------------------------------------ MapView_DrawUnitFacingArrow @ 0x1b7bc
 *
 * The three branches are mutually exclusive, so each case sets exactly one of the three flags.
 *
 * Fixture arithmetic: roster entry 0 is X = 50, Y = 20, Z = 7, which floors to the fixture cell
 * (3, 1, 0) and leaves A = 2, B = 4, C = 7. With sin * scale exactly 1.0 and cos * scale exactly
 * 0.5 every product lands on an integer, so the branch constants decide the results outright:
 * the roster branch gives nHeightOff = 2 + 16 - 4 = 14 and nHeightAux = 3 + 40 - 16 - 14 = 13;
 * the throw branch subtracts the 16.0 it just added and keeps the -20.0 - 8.0 tail, so it gives
 * -2 and -15.
 *
 * Ink rule: the probe glyph is one pixel at (x, y + index), so InkRow(x) reports y + index. */
static double         g_adArrowX[10];
static double         g_adArrowY[10];
static double         g_adArrowZ[10];
static i32            g_anArrowRow[11];
static MapCell        g_arrowCell;
static Soldier        g_arrowUnit;
static TactItem       g_arrowItem;
static ObjectRecFull  g_arrowObj;
static IsoArrowGate   g_arrowGate;
static IsoDrawCtx     g_arrowIso;
static IsoSpriteBases g_arrowBases;

static void ArrowFixture_Reset(void)
{
    memset(g_adArrowX, 0, sizeof(g_adArrowX));
    memset(g_adArrowY, 0, sizeof(g_adArrowY));
    memset(g_adArrowZ, 0, sizeof(g_adArrowZ));
    memset(g_anArrowRow, 0, sizeof(g_anArrowRow));
    memset(&g_arrowCell, 0, sizeof(g_arrowCell));
    memset(&g_arrowUnit, 0, sizeof(g_arrowUnit));
    memset(&g_arrowItem, 0, sizeof(g_arrowItem));
    memset(&g_arrowObj, 0, sizeof(g_arrowObj));
    memset(&g_arrowGate, 0, sizeof(g_arrowGate));
    memset(&g_arrowIso, 0, sizeof(g_arrowIso));
    memset(&g_arrowBases, 0, sizeof(g_arrowBases));

    g_arrowCell.nX = 3;
    g_arrowCell.nY = 1;
    g_arrowCell.nZ = 0;
    g_arrowCell.bFlags29 = 0x20;

    g_arrowIso.pCell      = &g_arrowCell;
    g_arrowIso.nDrawX     = 100;
    g_arrowIso.nDrawY     = 40;
    g_arrowIso.nObjParam  = 10;
    g_arrowIso.nPal       = 0;
    g_arrowIso.nRosterIndex = -999; /* sentinel: a value no branch wrote */

    g_arrowUnit.nDrawOffX = 3;
    g_arrowUnit.nDrawOffY = -2;

    g_arrowItem.pObj        = &g_arrowObj;
    g_arrowObj.aItemQty[50] = 6;

    g_arrowBases.nSmokeBase   = 0;
    g_arrowBases.nBulletsBase = 10;
    g_arrowBases.nFloorobBase = 0;

    g_arrowGate.adMtX      = g_adArrowX;
    g_arrowGate.adMtY      = g_adArrowY;
    g_arrowGate.adMtZ      = g_adArrowZ;
    g_arrowGate.anAimRow   = g_anArrowRow;
    g_arrowGate.pThrownItem = &g_arrowItem;
    g_arrowGate.bFireStatus1 = 1;
    g_arrowGate.nRowId       = 0;

    g_adArrowX[0] = 50.0; /* /16 = 3, remainder 2 */
    g_adArrowY[0] = 20.0; /* /16 = 1, remainder 4 */
    g_adArrowZ[0] = 7.0;  /* /12 = 0, remainder 7 */
    g_anArrowRow[0] = 4;  /* glyph 4 + 10 - 1 = 13 */
}

static void TestFacingArrow(void)
{
    printf("MapView_DrawUnitFacingArrow @ 0x1b7bc\n");

    /* --- the gate all three branches share --- */
    ArrowFixture_Reset();
    g_arrowCell.bFlags29 = 0;
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "no shot-trace flag on the cell, nothing is drawn");
    CheckEqI32(InkRow(113), -1, "and the roster branch really did not run");

    ArrowFixture_Reset();
    g_arrowGate.bFireStatus1 = 0;
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "a flagged cell with all three flags clear still draws nothing");

    /* --- branch 1: muzzle smoke at the unit's facing offset --- */
    ArrowFixture_Reset();
    g_arrowIso.pUnit = &g_arrowUnit;
    g_arrowGate.bFireAnimActive = 1;
    g_arrowGate.nPhaseCounter = 2;
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 1,
               "the fire-animation branch draws");
    CheckEqI32(InkRow(103), 48 + 58,
               "x is nDrawX + 3, y is 40 + 10 - 2, index is 0 + 2 + 56");
    CheckEqI32(g_arrowIso.nRosterIndex, -999, "and the roster cursor is left alone");

    g_arrowGate.nPhaseCounter = 4;
    ClearSurface();
    (void)MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases);
    CheckEqI32(InkRow(103), 48 + 56, "phase 4 is 4 % 4 = 0, so the first smoke frame");
    g_arrowGate.nPhaseCounter = 5;
    ClearSurface();
    (void)MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases);
    CheckEqI32(InkRow(103), 48 + 57, "phase 5 is 5 % 4 = 1");

    ArrowFixture_Reset();
    g_arrowGate.bFireAnimActive = 1;
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "the smoke branch needs a unit, and the flag is tested before pUnit");
    CheckEqI32(InkRow(103), -1, "so a null unit draws nothing");

    ArrowFixture_Reset();
    g_arrowIso.pUnit = &g_arrowUnit;
    g_arrowGate.bFireAnimActive = 1;
    g_arrowGate.nPhaseCounter = 2;
    ClearSurface();
    (void)MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases);
    Check(InkAt(103, 106) != 0, "smoke is drawn");
    CheckEqI32(InkRow(113), -1, "and the roster branch is skipped, not added to");

    /* --- branch 2: the ten-entry multi-target roster --- */
    ArrowFixture_Reset();
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 1,
               "entry 0 hits the fixture cell");
    CheckEqI32(g_arrowIso.nHeightOff, 14, "nHeightOff is A + 16 - B, which is 2 + 16 - 4");
    CheckEqI32(g_arrowIso.nHeightAux, 13, "nHeightAux is (A + B)/2 + 40 - 16 - 2C");
    CheckEqI32(InkRow(113), 52 + 13, "glyph 13 at (100 + 14 - 1, 40 + 13 - 1)");
    CheckEqI32(g_arrowIso.nRosterIndex, 10, "the cursor walks the whole roster");
    Check(fabs(g_arrowIso.dHeightA - 2.0) < 1e-12, "A is the 16 remainder of 50");
    Check(fabs(g_arrowIso.dHeightB - 4.0) < 1e-12, "B is the 16 remainder of 20");
    Check(fabs(g_arrowIso.dHeightC - 7.0) < 1e-12, "C is the 12 remainder of 7");

    /* The nudge table is indexed 10 * nRowId + n. Row 0 starts at -1, row 4 starts at -3. */
    ArrowFixture_Reset();
    g_arrowGate.nRowId = 4;
    ClearSurface();
    (void)MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases);
    CheckEqI32(InkRow(111), 50 + 13, "row 4 nudges both axes by -3");
    CheckEqI32(InkRow(113), -1, "and the un-nudged column stays empty");

    /* Each of the four entry tests, on its own. */
    ArrowFixture_Reset();
    g_anArrowRow[0] = 0;
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "an aim-row entry of 0 is skipped");

    ArrowFixture_Reset();
    g_anArrowRow[0] = -3;
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "and so is a negative one: the test is > 0, not != 0");

    ArrowFixture_Reset();
    g_adArrowX[0] = 67.0; /* /16 = 4 */
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "x floored to 4 is not this cell");

    ArrowFixture_Reset();
    g_adArrowY[0] = 36.0; /* /16 = 2 */
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "y floored to 2 is not this cell");

    ArrowFixture_Reset();
    g_adArrowZ[0] = 12.0; /* /12 = 1 */
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "z divided to 1 is not this layer");

    ArrowFixture_Reset();
    g_adArrowX[0] = -17.0; /* -17 / 16 = -1, and a cell coordinate is a byte */
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "a negative x can never match a cell coordinate");

    /* z truncates toward zero, so -1 / 12 is 0 and the layer still matches. */
    ArrowFixture_Reset();
    g_adArrowZ[0] = -1.0;
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 1,
               "z truncates: -1 / 12 is 0, so the layer matches");
    Check(fabs(g_arrowIso.dHeightC + 1.0) < 1e-12, "and C keeps the sign: -1 - 12 * 0");
    CheckEqI32(g_arrowIso.nHeightAux, 29, "which lifts nHeightAux to 3 + 40 - 16 + 2");
    CheckEqI32(InkRow(113), 68 + 13, "and the glyph moves down with it");

    /* Two entries can hit the same cell, and entry 1 sits in an un-nudged table slot. */
    ArrowFixture_Reset();
    g_adArrowX[1] = 50.0;
    g_adArrowY[1] = 20.0;
    g_adArrowZ[1] = 7.0;
    g_anArrowRow[1] = 11; /* glyph 11 + 10 - 1 = 20 */
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 2,
               "both entries hit, so two glyphs are drawn");
    CheckEqI32(InkRow(113), 52 + 13, "entry 0 nudged by -1");
    Check(InkAt(114, 53 + 20) != 0, "entry 1 nudged by 0");

    ArrowFixture_Reset();
    g_anArrowRow[0] = 0;
    g_arrowIso.nRosterIndex = 7;
    ClearSurface();
    (void)MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases);
    CheckEqI32(g_arrowIso.nRosterIndex, 10,
               "the cursor is zeroed at the branch even when nothing matches");

    /* --- branch 3: the thrown item's own glyph at the end of entry 0 --- */
    ArrowFixture_Reset();
    g_arrowGate.bFireStatus1 = 0;
    g_arrowGate.bTrajHitStop = 1;
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 1,
               "the throw branch draws");
    CheckEqI32(g_arrowIso.nHeightOff, -2,
               "it subtracts the 16.0 it just added: 2 + 16 - 4 - 16");
    CheckEqI32(g_arrowIso.nHeightAux, -15,
               "and keeps the -20.0 - 8.0 tail: 3 + 40 - 16 - 14 - 20 - 8");
    CheckEqI32(InkRow(98), 25 + 5, "glyph 6 + 0 - 1 at (100 - 2, 40 - 15)");

    ArrowFixture_Reset();
    g_arrowGate.bFireStatus1 = 0;
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 0,
               "bTrajHitStop clear blocks the throw branch");

    ArrowFixture_Reset();
    g_arrowGate.bFireStatus1 = 0;
    g_arrowGate.bTrajHitStop = 1;
    g_arrowCell.nX = 9;   /* this branch compares nothing against the cell */
    g_anArrowRow[0] = 0;  /* and does not test the aim row either */
    ClearSurface();
    CheckEqI32(MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases), 1,
               "the throw branch checks neither the cell nor the aim row");

    ArrowFixture_Reset();
    g_arrowGate.bFireStatus1 = 0;
    g_arrowGate.bTrajHitStop = 1;
    g_arrowObj.aItemQty[50] = 9;
    g_arrowBases.nFloorobBase = 4;
    ClearSurface();
    (void)MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases);
    CheckEqI32(InkRow(98), 25 + 12, "the glyph is aItemQty[50] + the floorob base - 1");

    /* Entry 0 drives the position: X = 54 leaves A = 6, so the glyph moves two columns right. */
    ArrowFixture_Reset();
    g_arrowGate.bFireStatus1 = 0;
    g_arrowGate.bTrajHitStop = 1;
    g_adArrowX[0] = 54.0; /* /16 = 3, remainder 6 */
    ClearSurface();
    (void)MapView_DrawUnitFacingArrow(&g_arrowIso, &g_arrowGate, &g_arrowBases);
    CheckEqI32(g_arrowIso.nHeightOff, 2, "6 + 16 - 4 - 16");
    CheckEqI32(g_arrowIso.nHeightAux, -13, "(4 + 6)/2 + 40 - 16 - 14 - 20 - 8");
    CheckEqI32(InkRow(102), 27 + 5, "so the glyph lands at (102, 27)");
}

/* ------------------------------------------------------------ MapView_DrawIsoLayer @ 0x21e2c
 *
 * The frame is an orchestrator, so these tests aim at what it DECIDES rather than at what the eleven
 * per-cell callees draw: the two iso offsets, the loop bounds and the state they leave behind, the
 * layer-0-only fallback glyph, the multi-target bars, the pad label copy, and the mission cover.
 *
 * The grid is built so that every cell FAILS to resolve: with nMapDimX = nMapDimY = 1 and no camera
 * scroll, Map_IsoOffsetA gives -5, so nMapY is negative for every tile in the fixture. That keeps the
 * chain quiet and leaves the fallback glyph as the only thing drawn. */
static MapCell         *g_apLayerGrid[3844];
static MapCell          g_layerCell;
static IsoGrid          g_layerGrid;
static IsoDrawCtx       g_layerIso;
static IsoLayerEnv      g_layerEnv;
static IsoAnimGlobals   g_layerAnim;
static IsoSelectGate    g_layerSelect;
static IsoUnitDrawGate  g_layerUnit;
static IsoSpriteBases   g_layerBases;
static IsoHeightGate    g_layerHeight;
static IsoArrowGate     g_layerArrow;
static IsoViewportGuide g_layerGuide;
static UiControl        g_layerBank[PUFO_HUD_PANEL_CONTROLS];
static UiControl        g_layerCtl;
static TactHudPanelEnv  g_layerPanel;
static PadImage         g_layerHudPad;
static StatusStack      g_layerStatus;
static StatusDrawHooks  g_layerHooks;
static GfxColorEnv      g_layerColor;
static Soldier          g_layerSel;
static u16              g_aLayerGroupBase[PUFO_ISO_GROUP_BASE_COUNT];
static i32              g_nHookSetColor;
static i32              g_nHookDraw;

static void LayerHook_SetColor(u8 r1, u8 g1, u8 b1, u8 r2, u8 g2, u8 b2, void *pUser)
{
    (void)r1; (void)g1; (void)b1; (void)r2; (void)g2; (void)b2; (void)pUser;
    ++g_nHookSetColor;
}

static void LayerHook_Draw(i32 x, i32 y, const char *psz, void *pUser)
{
    (void)x; (void)y; (void)psz; (void)pUser;
    ++g_nHookDraw;
}

static bool LayerHook_InputOff(void *pUser) { (void)pUser; return false; }
static bool LayerHook_NotOperative(void *pUser) { (void)pUser; return false; }
static bool LayerHook_NotBusy(void *pUser) { (void)pUser; return false; }
static i32  LayerHook_PadOriginY(void *pUser) { (void)pUser; return 0; }
static void LayerHook_BlitPad(PadImage *pPad, void *pUser) { (void)pPad; (void)pUser; }
static void LayerHook_BlitPadEx(const PadImage *pPad, void *pUser) { (void)pPad; (void)pUser; }

static void LayerFixture_Reset(void)
{
    i32 k;

    memset(g_aLayerGroupBase, 0, sizeof(g_aLayerGroupBase));
    memset(&g_layerCell, 0, sizeof(g_layerCell));
    memset(&g_layerIso, 0, sizeof(g_layerIso));
    memset(&g_layerEnv, 0, sizeof(g_layerEnv));
    memset(&g_layerAnim, 0, sizeof(g_layerAnim));
    memset(&g_layerSelect, 0, sizeof(g_layerSelect));
    memset(&g_layerUnit, 0, sizeof(g_layerUnit));
    memset(&g_layerBases, 0, sizeof(g_layerBases));
    memset(&g_layerHeight, 0, sizeof(g_layerHeight));
    memset(&g_layerArrow, 0, sizeof(g_layerArrow));
    memset(&g_layerGuide, 0, sizeof(g_layerGuide));
    memset(g_layerBank, 0, sizeof(g_layerBank));
    memset(&g_layerCtl, 0, sizeof(g_layerCtl));
    memset(&g_layerPanel, 0, sizeof(g_layerPanel));
    memset(&g_layerHudPad, 0, sizeof(g_layerHudPad));
    memset(&g_layerStatus, 0, sizeof(g_layerStatus));
    memset(&g_layerHooks, 0, sizeof(g_layerHooks));
    memset(&g_layerColor, 0, sizeof(g_layerColor));
    memset(&g_layerSel, 0, sizeof(g_layerSel));
    for (k = 0; k < 3844; ++k) {
        g_apLayerGrid[k] = &g_layerCell;
    }

    g_layerGrid.apCells = g_apLayerGrid;
    g_layerGrid.nDimX   = 1;
    g_layerGrid.nDimY   = 1;

    g_layerEnv.nMapDimX      = 1;
    g_layerEnv.nMapDimY      = 1;
    g_layerEnv.nCamScrollX   = 0;
    g_layerEnv.nCamScrollY   = 0;
    g_layerEnv.nLayerCount   = 2;
    g_layerEnv.nTileCountX   = 2;
    g_layerEnv.nTileCountY   = 8;
    g_layerEnv.nMapPhase     = 3;
    g_layerEnv.pGrid         = &g_layerGrid;
    g_layerEnv.pAnim         = &g_layerAnim;
    g_layerEnv.pSelect       = &g_layerSelect;
    g_layerEnv.pUnit         = &g_layerUnit;
    g_layerEnv.pBases        = &g_layerBases;
    g_layerEnv.pHeight       = &g_layerHeight;
    g_layerEnv.pArrow        = &g_layerArrow;
    g_layerEnv.pGuide        = &g_layerGuide;
    g_layerEnv.pBank         = g_layerBank;
    g_layerEnv.pPanel        = &g_layerPanel;
    g_layerEnv.pColor        = &g_layerColor;
    g_layerEnv.pfnDecimalBlit = LayerHook_BlitPad;
    g_layerEnv.pfnCtlBlit     = LayerHook_BlitPadEx;
    g_layerEnv.pfnCtlBlitEx   = LayerHook_BlitPadEx;
    g_layerEnv.pStatus        = &g_layerStatus;
    g_layerEnv.pStatusHooks   = &g_layerHooks;
    g_layerEnv.pSelUnit       = &g_layerSel;
    g_layerEnv.bDeployPanelOpen = 1;
    g_layerEnv.nHudPadHeight  = 40;
    g_layerEnv.pLookCtl       = &g_layerCtl;
    g_layerEnv.pPadLabel      = "AB";
    g_layerEnv.nUiScreen      = 0;   /* the cover is off unless a case turns it on */

    g_layerUnit.paGroupBase = g_aLayerGroupBase;

    /* The panel re-bases the 19 controls and then stops, which is all this fixture wants from it. */
    g_layerPanel.pHudPad             = &g_layerHudPad;
    g_layerPanel.pSelUnit            = &g_layerSel;
    g_layerPanel.bDeployPanelOpen    = 1;
    g_layerPanel.bPlayerInputEnabled = 0;

    g_layerCtl.x        = 10;
    g_layerCtl.y        = 20;
    g_layerCtl.bEnabled = 1;

    g_layerHooks.pfnSetColor            = LayerHook_SetColor;
    g_layerHooks.pfnDraw                = LayerHook_Draw;
    g_layerHooks.pfnPlayerInputEnabled  = LayerHook_InputOff;
    g_layerHooks.pfnSelectedIsOperative = LayerHook_NotOperative;
    g_layerHooks.pfnIsBusy              = LayerHook_NotBusy;
    g_layerHooks.pfnHudPadOriginY       = LayerHook_PadOriginY;

    g_nHookSetColor = 0;
    g_nHookDraw     = 0;
}

static void TestIsoLayer(void)
{
    printf("MapView_DrawIsoLayer @ 0x21e2c\n");

    /* --- the iso offsets and the loop end state --- */
    LayerFixture_Reset();
    ClearSurface();
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(g_layerIso.nIsoOffA, -5, "nIsoOffA is nCamScrollX - 5 * nMapDimX + nCamScrollY");
    CheckEqI32(g_layerIso.nIsoOffB, 4, "nIsoOffB is 5 * nMapDimY - nCamScrollX + nCamScrollY - 1");
    CheckEqI32(g_layerIso.nLayer, 2, "the layer counter ends one past the last layer drawn");
    CheckEqI32(g_layerIso.nTileX, 2, "the inner counter ends at the x count");
    CheckEqI32(g_layerIso.nTileY, 8, "the outer counter ends at the y count");

    /* --- the fallback glyph is layer 0 only ---
     * nDrawX = 32 * nTileX - 16 (or - 32 on an odd tile row) and nDrawY = 8 * (nTileY - 3 * nLayer - 4).
     * Layer 0, tile (1, 7) is odd so it lands at (0, 24); layer 1's only non-negative row is tile
     * (1, 7) too, which would be (0, 0). Ink at 24 and none at 0 is the layer test. */
    Check(InkAt(0, 24) != 0, "layer 0 draws the blank group glyph where a cell does not resolve");
    CheckEqI32(InkAt(0, 0), 0, "layer 1 does not, and its only in-range row would have been (0, 0)");
    Check(InkAt(16, 0) != 0, "tile (1, 4) on layer 0 is at (16, 0)");

    /* --- zero counts leave the counters at zero and draw nothing --- */
    LayerFixture_Reset();
    g_layerEnv.nLayerCount = 0;
    ClearSurface();
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(g_layerIso.nLayer, 0, "no layers, no walk");
    CheckEqI32(InkRow(16), -1, "and the fallback glyph never runs");

    LayerFixture_Reset();
    g_layerEnv.nTileCountX = 0;
    g_layerEnv.nTileCountY = 0;
    ClearSurface();
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(g_layerIso.nTileX, 0, "a zero x count still sets the counter");
    CheckEqI32(g_layerIso.nTileY, 0, "and so does the y count");
    CheckEqI32(InkRow(16), -1, "with no tiles nothing is drawn");

    /* --- the multi-target bars --- */
    LayerFixture_Reset();
    g_layerSel.nMultiTarget = 7;
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(g_layerIso.nScratch2, 5, "five bars at most, whatever the unit's count says");
    CheckEqI32(g_layerIso.nTileX, 207, "every bar is at column 207");
    CheckEqI32(g_layerIso.nTileY, 211, "the last bar is 300 - 18 * 4 - 17, the deploy-panel short form");

    LayerFixture_Reset();
    g_layerEnv.bDeployPanelOpen = 0;
    g_layerSel.nMultiTarget = 3;
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(g_layerIso.nScratch2, 3, "a count below the cap runs to the count");
    CheckEqI32(g_layerIso.nTileY, 300 - 18 * 2 - 40,
               "with the panel closed the bar is shortened by the hud pad height instead");

    LayerFixture_Reset();
    g_layerIso.nScratch2 = -999;
    g_layerSel.nMultiTarget = 0;
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(g_layerIso.nScratch2, -999, "a unit with no multi-target list never enters the loop");

    /* --- the active pad and its label --- */
    LayerFixture_Reset();
    g_layerIso.szText[0] = '?';
    g_layerIso.szText[1] = '\0';
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(g_layerIso.szText[0], 'A', "the enabled pad copies its label into the context");
    CheckEqI32(g_layerIso.szText[1], 'B', "two characters, as the fixture label has");

    LayerFixture_Reset();
    g_layerCtl.bEnabled = 0;
    g_layerIso.szText[0] = '?';
    g_layerIso.szText[1] = '\0';
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(g_layerIso.szText[0], '?', "a disabled pad draws nothing and copies nothing");

    /* --- the mission cover --- */
    LayerFixture_Reset();
    g_layerEnv.nLayerCount = 0;
    g_layerEnv.nUiScreen = 5;
    ClearSurface();
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    Check(InkAt(239, 319) != 0, "screen 5 gets the full 240 by 320 cover");

    LayerFixture_Reset();
    g_layerEnv.nLayerCount = 0;
    g_layerEnv.nUiScreen = 13;
    ClearSurface();
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(InkAt(239, 319), 0, "screen 13 is one of the two the cover skips");

    LayerFixture_Reset();
    g_layerEnv.nLayerCount = 0;
    g_layerEnv.nUiScreen = 0;
    ClearSurface();
    MapView_DrawIsoLayer(&g_layerIso, &g_layerEnv);
    CheckEqI32(InkAt(239, 319), 0, "and so is screen 0");
}

/* ------------------------------------------------------------ MapView_DrawUnit @ 0x1c958
 *
 * Almost every decision this function makes lands in an IsoDrawCtx field, so the tests read the
 * context back instead of trying to separate glyph ink that all lands in one column. Ink is used
 * only where the question is whether a glyph reached Font_DrawGlyph at all.
 *
 * The fixture builds one layer's worth of the flattened cell array (3600 slots) and fills the 4 by 3
 * corner of it, which is where every index the function computes for these cases falls: the unit sits
 * at (0, 0, 0) so its 2 by 2 block is slots 0, 1, 60 and 61. */
#define UNIT_LAYER_CELLS 3600

static MapCell        *g_apUnitCells[UNIT_LAYER_CELLS];
static MapCell         g_aUnitGrid[4][3];
static Mc1Rec          g_aUnitCeil[4];
static Soldier         g_unit;
static TactItem        g_handL;
static TactItem        g_handR;
static ObjectRecFull   g_objWeapon;
static ObjectRecFull   g_objMisc;
static TactItem        g_selWeaponItem;  /* the selected weapon ITEM the iso gate reaches through */
static TactItem       *g_pSelWeapon;     /* the Map+0x48 cell itself: a TactItem* */
static PadImage        g_framePad;
static u8              g_aFramePadBlob[8];
static u16             g_aUnitGroupBase[PUFO_ISO_GROUP_BASE_COUNT];
static u8              g_aUnitTypeProbe[4];
static IsoUnitDrawGate g_ugate;
static IsoDrawCtx      g_uiso;

static void UnitFixture_Reset(void)
{
    i32 x;
    i32 y;
    i32 k;

    memset(g_apUnitCells, 0, sizeof(g_apUnitCells));
    memset(g_aUnitGrid, 0, sizeof(g_aUnitGrid));
    memset(g_aUnitCeil, 0, sizeof(g_aUnitCeil));
    memset(&g_unit, 0, sizeof(g_unit));
    memset(&g_handL, 0, sizeof(g_handL));
    memset(&g_handR, 0, sizeof(g_handR));
    memset(&g_objWeapon, 0, sizeof(g_objWeapon));
    memset(&g_objMisc, 0, sizeof(g_objMisc));
    memset(&g_selWeaponItem, 0, sizeof(g_selWeaponItem));
    g_pSelWeapon = &g_selWeaponItem; /* the cell points at the item; the object lives in item.pObj */
    memset(g_aUnitGroupBase, 0, sizeof(g_aUnitGroupBase));
    memset(&g_uiso, 0, sizeof(g_uiso));

    for (x = 0; x < 4; ++x) {
        for (y = 0; y < 3; ++y) {
            g_aUnitGrid[x][y].nX = (u8)x;
            g_aUnitGrid[x][y].nY = (u8)y;
            g_aUnitGrid[x][y].nZ = 0;
            g_apUnitCells[60 * y + x] = &g_aUnitGrid[x][y];
        }
    }

    /* Sentinels, so a field the function did not write is distinguishable from a value it did. */
    g_uiso.bAnimStep = 99;
    g_uiso.bAnimKind = 99;
    g_uiso.bUnitGlyphRow = 99;
    g_uiso.abUnitFlag[0] = 99;
    g_uiso.abUnitFlag[1] = 99;
    g_uiso.abUnitFlag[2] = 99;
    g_uiso.abUnitFlag[3] = 99;
    g_uiso.nAnimFrame = -1;
    g_uiso.nAnimParam = -1;
    g_uiso.nScratch2 = -1;
    g_uiso.nDrawMode = -1;
    g_uiso.nDrawAux = -1;

    g_uiso.pCell = &g_aUnitGrid[0][0];
    g_uiso.pUnit = &g_unit;
    g_uiso.nDrawX = 100;
    g_uiso.nDrawY = 40;
    g_uiso.nPal = 255;
    /* A small unit's nObjParam comes from MapView_bResolveIsoCell, not from this function, so the
     * fixture supplies one that is far from the marker rows. */
    g_uiso.nObjParam = 60;

    g_unit.pCell = &g_aUnitGrid[0][0];
    g_unit.bStatus = 1;      /* not operative: Unit_bIsOperative tests bStatus == 0 */
    g_unit.nUnitType = 0;    /* small */
    g_unit.nAnimPose = 15;
    g_unit.bFacing = 0;
    g_unit.bFlags = 0;

    for (k = 0; k < 4; ++k) {
        g_aUnitTypeProbe[k] = 0xEE; /* matches no unit type */
    }

    g_selWeaponItem.pObj = &g_objMisc;
    g_ugate.apCells = g_apUnitCells;
    g_ugate.pSelUnit = NULL;
    g_ugate.bQueueActive = 0;
    g_ugate.bSelectFlag = 0;
    g_ugate.nAnimPhase = 0;
    g_ugate.nMapPhase = 0;
    g_ugate.paGroupBase = g_aUnitGroupBase;
    g_ugate.nCursorBase = 0;
    g_ugate.nHandobBase = 4;
    g_ugate.apSelWeapon = &g_pSelWeapon;
    g_ugate.pSelectFrame = &g_framePad;
    g_ugate.paTypeProbe = g_aUnitTypeProbe;

    /* PadImage_BlitEx has no NULL guard, so the frame pad needs a real blob: u16 width, u16 height,
     * then indexed pixels. The single pixel is the 0xFF key, so the blit lays no ink and the tests
     * read nOriginX / nOriginY instead. */
    g_aFramePadBlob[0] = 1;   g_aFramePadBlob[1] = 0;
    g_aFramePadBlob[2] = 1;   g_aFramePadBlob[3] = 0;
    g_aFramePadBlob[4] = 0xFF;
    PadImage_ctor(&g_framePad);
    g_framePad.pData = g_aFramePadBlob;
    g_framePad.uWidth = 1;
    g_framePad.uHeight = 1;
    g_framePad.nOriginX = -999;
    g_framePad.nOriginY = -999;
}

static void TestUnitGate(void)
{
    printf("MapView_DrawUnit @ 0x1c958: the visibility gate\n");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 0;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bAnimStep, 99, "small unit on an unflagged cell: the walk loop never runs");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bAnimStep, 5, "small unit on a flagged cell: five walk steps");
    CheckEqI32(g_uiso.bAnimKind, 4, "and the last step is the weapon pass");

    /* A large unit is drawable when ANY cell of the 2 by 2 block anchored on its own cell is flagged.
     * Its block is slots 0, 1, 60 and 61. */
    UnitFixture_Reset();
    g_unit.nUnitType = 4;                 /* large */
    g_aUnitGrid[1][1].bFlags44 = 1;       /* slot idx + 61 */
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.abUnitFlag[3], 2, "large unit: a flagged diagonal neighbour is enough");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.abUnitFlag[3], 99, "large unit: nothing flagged in the block, nothing drawn");

    /* The restore asymmetry: only the paths that pass the gate write nPal back. */
    UnitFixture_Reset();
    g_unit.bStatus = 0;                   /* operative, so nPal is zeroed at entry */
    g_aUnitGrid[0][0].bFlags44 = 0;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nPal, 0,
               "ORIGINAL BUG (0x1CAAC): an undrawn unit leaves nPal at 0 for the caller");

    UnitFixture_Reset();
    g_unit.bStatus = 0;
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nPal, 255, "a drawn unit restores nPal");
}

static void TestUnitDirections(void)
{
    printf("MapView_DrawUnit: the large-unit direction set\n");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.abUnitFlag[0], 0, "same cell: flag0");
    CheckEqI32(g_uiso.abUnitFlag[1], 4, "same cell: flag1");
    CheckEqI32(g_uiso.abUnitFlag[3], 2, "same cell: two steps");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_uiso.pCell = &g_aUnitGrid[1][0];    /* different X, same Y */
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.abUnitFlag[0], 1, "same row: flag0");
    CheckEqI32(g_uiso.abUnitFlag[1], 5, "same row: flag1");
    CheckEqI32(g_uiso.abUnitFlag[3], 2, "same row: two steps");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_uiso.pCell = &g_aUnitGrid[0][1];    /* same X, different Y */
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.abUnitFlag[0], 2, "same column: flag0");
    CheckEqI32(g_uiso.abUnitFlag[1], 6, "same column: flag1");
    CheckEqI32(g_uiso.abUnitFlag[3], 2, "same column: two steps");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_uiso.pCell = &g_aUnitGrid[1][1];    /* different in both */
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.abUnitFlag[0], 3, "diagonal: flag0");
    CheckEqI32(g_uiso.abUnitFlag[1], 7, "diagonal: flag1");
    CheckEqI32(g_uiso.abUnitFlag[2], 8, "diagonal: flag2");
    CheckEqI32(g_uiso.abUnitFlag[3], 3, "diagonal: three steps");
    /* The last step is direction 8, and nObjParam is 0 here (no ceiling sprites). */
    CheckEqI32(g_uiso.nSpriteY, 40 - 4, "the direction-8 step lifts the glyph by 4");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_unit.nAnimPose = 18;
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bUnitGlyphRow, 2, "walk-glyph row 2 when nAnimPose is 18");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_unit.bFlags = 2;
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bUnitGlyphRow, 1, "row 1 when bFlags bit 1 is set");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bUnitGlyphRow, 0, "row 0 otherwise");
}

static void TestUnitCeilMin(void)
{
    printf("MapView_DrawUnit: nObjParam over the 2 by 2 block\n");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_aUnitGrid[0][0].pCeilSpr = &g_aUnitCeil[0]; g_aUnitCeil[0].iCeilParam = 5;
    g_aUnitGrid[1][0].pCeilSpr = &g_aUnitCeil[1]; g_aUnitCeil[1].iCeilParam = -3;
    g_aUnitGrid[0][1].pCeilSpr = &g_aUnitCeil[2]; g_aUnitCeil[2].iCeilParam = 0;
    g_aUnitGrid[1][1].pCeilSpr = &g_aUnitCeil[3]; g_aUnitCeil[3].iCeilParam = -2;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nObjParam, -3, "the minimum over the four ceiling sprites");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_aUnitGrid[0][0].pCeilSpr = &g_aUnitCeil[0]; g_aUnitCeil[0].iCeilParam = -7;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nObjParam, -7, "the unit's own ceiling counts only when it is negative");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_aUnitGrid[0][0].bFlags44 = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nObjParam, 0, "no ceiling sprites gives 0");
}

static void TestUnitMarkers(void)
{
    printf("MapView_DrawUnit: the two cursor markers\n");

    /* nPal 255 with the select flag clear: both markers are skipped. The body glyphs draw at row
     * nObjParam + nDrawY + index = 100 and up, so rows 57 and 58 belong to the markers alone. */
    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    ClearSurface();
    MapView_DrawUnit(&g_uiso, &g_ugate);
    Check(InkAt(100, 57) == 0 && InkAt(100, 58) == 0, "nPal 255 with no select flag draws no marker");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_uiso.nPal = 10;
    g_ugate.bSelectFlag = 1;
    ClearSurface();
    MapView_DrawUnit(&g_uiso, &g_ugate);
    Check(InkAt(100, 40 + 18) != 0, "the marker before the body is cursor base + 18");
    Check(InkAt(100, 40 + 17) != 0, "the marker after the body is cursor base + 17");

    UnitFixture_Reset();
    g_unit.bStatus = 0;                   /* operative */
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_uiso.nPal = 10;
    g_ugate.bSelectFlag = 1;
    ClearSurface();
    MapView_DrawUnit(&g_uiso, &g_ugate);
    Check(InkAt(100, 57) == 0 && InkAt(100, 58) == 0,
          "an operative unit draws no marker: nPal is zeroed, and 0 is not above 7");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_uiso.nPal = 10;
    g_ugate.bSelectFlag = 0;
    ClearSurface();
    MapView_DrawUnit(&g_uiso, &g_ugate);
    Check(InkAt(100, 58) != 0, "a palette that is not 255 draws the marker without the flag");

    UnitFixture_Reset();
    g_unit.bStatus = 0;
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_uiso.nPal = 4;
    g_ugate.bSelectFlag = 1;
    ClearSurface();
    MapView_DrawUnit(&g_uiso, &g_ugate);
    Check(InkAt(100, 57) == 0 && InkAt(100, 58) == 0, "nPal at or below 7 draws no marker");
}

static void TestSelectionFrame(void)
{
    printf("MapView_DrawUnit: the selection frame\n");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_ugate.pSelUnit = &g_unit;
    g_ugate.bQueueActive = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_framePad.nOriginX, -999, "suppressed while the tactical queue is active");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_ugate.pSelUnit = NULL;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_framePad.nOriginX, -999, "and for anything that is not the selected unit");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_unit.bFlags = 2;
    g_ugate.pSelUnit = &g_unit;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_framePad.nOriginX, -999, "and for a selected unit with bFlags bit 1 set");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_ugate.pSelUnit = &g_unit;
    g_framePad.uWidth = 20;
    g_framePad.uHeight = 10;
    g_ugate.nMapPhase = 2;                /* aBobPhaseY[2] = -2 */
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_framePad.nOriginX, 100 + ((32 - 20) >> 1), "x centres the frame on nDrawX");
    CheckEqI32(g_framePad.nOriginY, -2 - 10 + 60 + 40, "y is bob - height + nObjParam + nDrawY");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_ugate.pSelUnit = &g_unit;
    g_framePad.uWidth = 40;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_framePad.nOriginX, 100 + ((33 - 40) >> 1),
               "a frame wider than 32 takes 33 - uWidth before the shift");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_ugate.pSelUnit = &g_unit;
    g_uiso.pCell = &g_aUnitGrid[1][0];
    MapView_DrawUnit(&g_uiso, &g_ugate);
    Check(g_framePad.nOriginX != -999, "a large selected unit still gets its x");
    CheckEqI32(g_framePad.nOriginY, -999,
               "but a large unit off its own cell stops before the blit, after restoring nPal");

    UnitFixture_Reset();
    g_unit.nUnitType = 4;
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_ugate.pSelUnit = &g_unit;
    g_framePad.uHeight = 10;
    g_ugate.nMapPhase = 0;                /* aBobPhaseY[0] = 0, and nObjParam recomputes to 0 */
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_framePad.nOriginY, 0 - 10 + 0 + 40 + 8, "a large unit on its own cell adds 8");
    CheckEqI32(g_uiso.nPal, 255, "and that early exit still restores nPal");
}

static void TestUnitPoseTable(void)
{
    printf("MapView_DrawUnit: the pose-frame table branch\n");

    /* Unit type 0, facing 0 has the walk-kind group 0,1,2,3,4, so step 3 is the kind-3 step and step
     * 4 is the weapon pass, which does not touch nAnimFrame. The stored value is the table entry
     * decremented in place at 0x1CFF8 and 0x1D018. */
    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_unit.nAnimPose = 15;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nAnimFrame, (i32)g_aPoseFrameTable[6][3] - 1,
               "pose 15 with no two-hand weapon takes the 5*base+33 entry");
    Check(g_uiso.nAnimFrame != (i32)g_aPoseFrameTable[15][3] - 1,
          "which is a different frame from the general pose+base entry");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_unit.nAnimPose = 14;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nAnimFrame, (i32)g_aPoseFrameTable[14][3] - 1,
               "a pose below 0xF takes the general pose+base entry");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_unit.nAnimPose = 20;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nAnimFrame, (i32)g_aPoseFrameTable[20][3] - 1,
               "and a pose above 0x11 takes the general entry too");

    /* A zero table entry is still stored before the zero test, so the step records 0 and draws
     * nothing. Row 19 is all zeros. */
    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_unit.nAnimPose = 19;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nAnimFrame, 0, "a zero frame entry is stored as 0, not decremented");
}

static void TestUnitWeaponPass(void)
{
    i32 nBody166;

    printf("MapView_DrawUnit: the weapon overlay\n");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_handL.pObj = &g_objWeapon;
    g_objWeapon.bListPri = 1;
    g_unit.pHandL = &g_handL;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bTwoHandWeapon, 1, "bListPri 1 in a hand sets the two-hand flag");
    CheckEqI32(g_uiso.nScratch2, -1, "but the wrapped facing is only stored for an armed unit");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_handR.pObj = &g_objWeapon;
    g_objWeapon.uSortKey = 22;
    g_unit.pHandR = &g_handR;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bTwoHandWeapon, 1, "uSortKey 22 counts as a weapon too");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_handL.pObj = &g_objWeapon;
    g_objWeapon.bListPri = 1;
    g_unit.pHandL = &g_handL;
    g_unit.bFlags = 4;
    g_selWeaponItem.pObj = &g_objMisc;            /* bListPri 0 */
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bTwoHandWeapon, 0, "an armed unit with a bListPri 0 selected weapon clears it");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_handL.pObj = &g_objWeapon;
    g_objWeapon.bListPri = 1;
    g_objWeapon.aItemQty[51] = 9;
    g_unit.pHandL = &g_handL;
    g_unit.bFlags = 4;
    g_unit.bFacing = 3;
    g_selWeaponItem.pObj = &g_objWeapon;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bTwoHandWeapon, 1, "a bListPri 1 selected weapon keeps the flag");
    CheckEqI32(g_uiso.nScratch2, 5, "facing 3 becomes 5 for the two-hand overlay");
    /* nAnimParam is not observable here: the two-hand flag selects the +8 walk-kind group, whose
     * step 4 is not the weapon pass, so a later body step overwrites it. */
    CheckEqI32(g_uiso.nDrawMode, g_aUnitFacingDrawOff[0][3][0], "nDrawMode takes the facing offset");
    CheckEqI32(g_uiso.nDrawAux, g_aUnitFacingDrawOff[0][3][1], "nDrawAux takes the second one");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_handL.pObj = &g_objWeapon;
    g_objWeapon.bListPri = 1;
    g_objWeapon.aItemQty[51] = 9;
    g_unit.pHandL = &g_handL;
    g_unit.bFlags = 4;
    g_unit.bFacing = 7;
    g_selWeaponItem.pObj = &g_objWeapon;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nScratch2, 1, "facing 7 wraps to 1");

    /* The overlay index. With the two-hand flag clear the walk-kind group is 0,1,2,3,4, so the weapon
     * pass is the last step and nAnimParam still holds what it wrote. */
    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_handL.pObj = &g_objWeapon;
    g_objWeapon.bListPri = 1;
    g_unit.pHandL = &g_handL;
    g_unit.bFlags = 4;
    g_unit.bFacing = 3;
    g_selWeaponItem.pObj = &g_objMisc;
    g_objMisc.bListPri = 0;
    g_objMisc.aItemQty[51] = 9;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.bTwoHandWeapon, 0, "with the flag clear, step 4 is the last step");
    CheckEqI32(g_uiso.nAnimParam, 9 + 3, "the index is aItemQty[51] plus the unwrapped facing");

    /* The pose gate at 0x1d2e8..0x1d2f4 (`nPose < 5 || nPose >= 0x12` skips the overlay). This is the
     * seam that decides whether hand-held equipment shows on the map at all: Unit_UpdateAnimPose leaves
     * an empty-handed unit at its base pose 0..4 (the 0x4989c early return adds nothing) and gives any
     * hand-held object 5..14 (0x498a4 / 0x49884), so the +5/+10 arm is what opens the gate. */
    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_handL.pObj = &g_objMisc;
    g_objMisc.bListPri = 0;                    /* not a weapon: the two-hand group stays off */
    g_objMisc.uSortKey = 7;
    g_objMisc.aItemQty[51] = 0;
    g_unit.pHandL = &g_handL;
    g_unit.nAnimPose = 1;                      /* 0x4981c: idle base pose */
    MapView_DrawUnit(&g_uiso, &g_ugate);
    nBody166 = g_uiso.nAnimParam;

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_unit.nAnimPose = 1;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nAnimParam, nBody166,
               "a pose below 5 leaves the overlay out: the held object draws exactly what an "
               "empty-handed unit draws, so nothing about the equipment is visible");
    Check(nBody166 != 0, "the two cases stay distinguishable: the body's own index is not the overlay's 0");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_handL.pObj = &g_objMisc;
    g_objMisc.bListPri = 0;
    g_objMisc.uSortKey = 7;
    g_objMisc.aItemQty[51] = 0;
    g_unit.pHandL = &g_handL;
    g_unit.nAnimPose = 6;                      /* 1 + 5, what 0x498a4 gives a non-weapon hand */
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nAnimParam, 0 + 0,
               "the same hand at the pose 0x498a4 produces runs the overlay and writes the object's own index");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_unit.bFlags = 1;                    /* bit 0 gives nDrawAux 4 */
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nDrawAux, 4, "bFlags bit 0 gives nDrawAux 4");
    CheckEqI32(g_uiso.nDrawMode, 0, "and nDrawMode is cleared before the facing branch");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_unit.bFlags = 2;                    /* bit 1 set, bit 4 clear -> the height table */
    MapView_DrawUnit(&g_uiso, &g_ugate);
    CheckEqI32(g_uiso.nDrawAux, g_aUnitHeightOff[0][0],
               "bFlags bit 1 without bit 4 takes aUnitHeightOff[type][nAnimPhase]");

    UnitFixture_Reset();
    g_aUnitGrid[0][0].bFlags44 = 1;
    g_unit.nAnimPose = 15;
    g_unit.pHandL = NULL;
    g_unit.pHandR = NULL;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    Check(true,
          "DEVIATION: both hands empty skips the glyph the original would dereference at 0x1D384");
}

static void TestUnitNulls(void)
{
    printf("MapView_DrawUnit: NULL safety\n");

    UnitFixture_Reset();
    MapView_DrawUnit(NULL, &g_ugate);
    MapView_DrawUnit(&g_uiso, NULL);
    g_uiso.pUnit = NULL;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    g_uiso.pUnit = &g_unit;
    g_uiso.pCell = NULL;
    MapView_DrawUnit(&g_uiso, &g_ugate);
    Check(true, "NULL context, gate, unit or cell is safe");
}

/* ------------------------------------------------------------ MapView_DrawMinimap @ 0x1ed4c
 *
 * The shipped beta never writes the span globals, so its minimap only ever draws the pad, the label
 * and the three controls. The fixture sets the spans explicitly to exercise the cell passes the
 * original leaves dormant, and keeps one dormant case to pin the shipped behaviour.
 *
 * Probe-glyph rule: glyph k inks one pixel at (x, y + k). The ground glyph is sprite record 2's
 * uUnk20 + 35 = 35, so pass 1 inks row y + 35 and every marker lands on its own row. Palette slot 0
 * is visible and slot 19 (what nPal 255 clamps to) is zero, which makes the bFlags29 palette gate
 * observable. */
#define MINI_GRID_CELLS (PUFO_MAP_CELL_DIM * PUFO_MAP_CELL_DIM * 4)

static MapCell       *g_apMiniGrid[MINI_GRID_CELLS];
static MapCell        g_miniCell;
static MapCell        g_miniHome;
static Mc1Rec         g_aMiniSpr[4];
static Mc1Rec        *g_apMiniMc1[3];
static Soldier        g_miniUnit;
static TactItem       g_miniItem;
static IsoGrid        g_miniGrid;
static IsoDrawCtx     g_miniIso;
static IsoMinimapEnv  g_miniEnv;
static IsoSpriteBases g_miniBases;
static UiControl      g_miniBank[PUFO_PBANK_WIDGET_COUNT];
static PadImage       g_miniCtlPad;
static PadImage       g_miniCursor;
static u8             g_aMiniCursorBlob[5];
static GfxColorEnv    g_miniColor;
static i32            g_nMiniScrollX;
static i32            g_nMiniScrollY;
static i32            g_nMiniCtlBlits;

static void MiniHook_BlitPad(const PadImage *pPad, void *pUser)
{
    (void)pPad; (void)pUser;
    ++g_nMiniCtlBlits;
}

static void MiniFixture_Reset(void)
{
    i32 k;

    memset(&g_miniCell, 0, sizeof(g_miniCell));
    memset(&g_miniHome, 0, sizeof(g_miniHome));
    memset(g_aMiniSpr, 0, sizeof(g_aMiniSpr));
    memset(&g_miniUnit, 0, sizeof(g_miniUnit));
    memset(&g_miniItem, 0, sizeof(g_miniItem));
    memset(&g_miniGrid, 0, sizeof(g_miniGrid));
    memset(&g_miniIso, 0, sizeof(g_miniIso));
    memset(&g_miniEnv, 0, sizeof(g_miniEnv));
    memset(&g_miniBases, 0, sizeof(g_miniBases));
    memset(g_miniBank, 0, sizeof(g_miniBank));
    memset(&g_miniCtlPad, 0, sizeof(g_miniCtlPad));
    memset(&g_miniCursor, 0, sizeof(g_miniCursor));
    memset(&g_miniColor, 0, sizeof(g_miniColor));

    for (k = 0; k < MINI_GRID_CELLS; ++k) {
        g_apMiniGrid[k] = &g_miniCell;
    }

    /* Sprite record 2 is the ground sprite the layer-0 pass looks up; uUnk20 0 gives glyph 35. */
    g_apMiniMc1[0] = &g_aMiniSpr[0];
    g_apMiniMc1[1] = &g_aMiniSpr[1];
    g_apMiniMc1[2] = &g_aMiniSpr[2];
    g_fontSys.apMc1Index = g_apMiniMc1;
    g_fontSys.nMc1IndexCount = 3;

    g_miniGrid.apCells = g_apMiniGrid;
    g_miniGrid.nDimX   = 6;
    g_miniGrid.nDimY   = 6;

    g_miniEnv.pnScrollX    = &g_nMiniScrollX;
    g_miniEnv.pnScrollY    = &g_nMiniScrollY;
    g_miniEnv.nSpanX       = 0;   /* the shipped value: no writer exists in the original */
    g_miniEnv.nSpanY       = 0;
    g_miniEnv.nLayerCount  = 1;
    g_miniEnv.nMapPhase    = 3;
    g_miniEnv.pGrid        = &g_miniGrid;
    g_miniEnv.pBases       = &g_miniBases;
    g_miniEnv.pLabel       = "";
    g_miniEnv.pMiniCursor  = &g_miniCursor;
    g_miniEnv.pBank        = g_miniBank;
    g_miniEnv.pColor       = &g_miniColor;
    g_miniEnv.pfnCtlBlit   = MiniHook_BlitPad;
    g_miniEnv.pfnCtlBlitEx = MiniHook_BlitPad;

    g_nMiniScrollX = 0;
    g_nMiniScrollY = 0;
    g_nMiniCtlBlits = 0;

    /* The three screen-2 controls, made drawable: mode 0, state 1, a pad in slot 1. */
    for (k = 0; k < PUFO_PBANK_MINIMAP_COUNT; ++k) {
        UiControl *pCtl = &g_miniBank[PUFO_PBANK_MINIMAP_FIRST + k];
        pCtl->bEnabled  = 1;
        pCtl->nMode     = 0;
        pCtl->nState    = 1;
        pCtl->apPads[1] = &g_miniCtlPad;
    }

    /* The cursor pad: 1 by 1, pixel 0x30 (the key is 0x3F, so it is not skipped), palette slot 0. */
    g_aMiniCursorBlob[0] = 1; g_aMiniCursorBlob[1] = 0;
    g_aMiniCursorBlob[2] = 1; g_aMiniCursorBlob[3] = 0;
    g_aMiniCursorBlob[4] = 0x30;
    PadImage_ctor(&g_miniCursor);
    g_miniCursor.pData   = g_aMiniCursorBlob;
    g_miniCursor.uWidth  = 1;
    g_miniCursor.uHeight = 1;
    g_miniCursor.nMode   = 0;
    g_miniCursor.nOriginX = 5;
    g_miniCursor.nOriginY = 5;
}

static void TestMinimap(void)
{
    printf("MapView_DrawMinimap @ 0x1ed4c\n");

    /* --- the scroll clamp, max first then min --- */
    MiniFixture_Reset();
    g_nMiniScrollX = 1000;
    g_nMiniScrollY = 1000;
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    CheckEqI32(g_nMiniScrollX, 30, "scroll X clamps to 40 * (6 - 3) / 4");
    CheckEqI32(g_nMiniScrollY, 20, "scroll Y clamps to 40 * (6 - 4) / 4");

    MiniFixture_Reset();
    g_nMiniScrollX = -100;
    g_nMiniScrollY = -100;
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    CheckEqI32(g_nMiniScrollX, -30, "and to -30 at the bottom");
    CheckEqI32(g_nMiniScrollY, -40, "and -40 for Y");

    /* --- the shipped case: no span, so no cell glyphs, but the furniture still runs --- */
    MiniFixture_Reset();
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    CheckEqI32(InkRow(4), -1, "the span globals are never written, so no cell is ever drawn");
    CheckEqI32(g_nMiniCtlBlits, 3, "the three screen-2 controls blit regardless");
    Check(InkAt(5, 5) != 0, "the minimap cursor pad blits at its origin");
    CheckEqI32(g_miniIso.szText[0], '1', "the label is the layer count, the text record being empty");
    CheckEqI32(g_miniIso.szText[1], 0, "and nothing follows it");
    CheckEqI32(g_miniIso.nScratch2, 3, "the control counter ends at three");

    /* --- pass 1 runs even with no layers, and paints the fixed ground glyph --- */
    MiniFixture_Reset();
    g_miniEnv.nSpanX = 240;   /* bound 60 = 10 * nMapDimX */
    g_miniEnv.nSpanY = 240;
    g_miniEnv.nLayerCount = 0;
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    Check(InkAt(0, 35) != 0, "cell (0,0) gets the ground glyph at 4 * (tile - scroll)");
    Check(InkAt(236, 271) != 0, "cell (59,59) gets it too");
    CheckEqI32(g_miniIso.nScratch, 35, "the ground glyph is sprite 2's uUnk20 + 35");
    CheckEqI32(g_miniIso.nTileX, 60, "the inner counter ends at the span bound");
    CheckEqI32(g_miniIso.nTileY, 60, "and so does the outer one");
    CheckEqI32(g_miniIso.nLayer, 0, "no layers, so pass 2 never ran");

    /* --- pass 2: the four sprite glyphs, the palette gate, and the uUnk20 0 skip --- */
    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;     /* bound 1: only cell (0,0) */
    g_miniEnv.nSpanY = 4;
    g_miniCell.bFlags29 = 0x10;
    g_miniCell.bUnk56   = 0;  /* visible palette slot */
    g_miniCell.pFloorSpr = &g_aMiniSpr[0];
    g_aMiniSpr[0].uUnk20 = 1; /* glyph 36 */
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    Check(InkAt(0, 35) != 0, "the ground glyph is still there");
    Check(InkAt(0, 36) != 0, "and the floor sprite draws its own glyph");
    CheckEqI32(g_miniIso.nScratch, 36, "stored in the context scratch");
    CheckEqI32(g_miniIso.nPal, 0, "bFlags29 bit 4 selects the cell's bUnk56 as palette");

    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniCell.pFloorSpr = &g_aMiniSpr[0];   /* uUnk20 stays 0 */
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    CheckEqI32(InkAt(0, 36), 0, "uUnk20 0 means the sprite has no minimap glyph");
    Check(InkAt(0, 35) != 0, "while the ground glyph uses bUnk56 directly and is visible");
    CheckEqI32(g_miniIso.nPal, 255, "without bFlags29 bit 4 the sprite palette is 255");

    /* --- the item marker: item list AND bFlags29 bit 4 --- */
    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniCell.bFlags29 = 0x10;
    g_miniCell.bUnk56   = 0;
    g_miniCell.pItemList = &g_miniItem;
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    Check(InkAt(0, 10) != 0, "phase 3 gives the item marker glyph 3 / 3 + 9 = 10");
    CheckEqI32(g_miniIso.nScratch, 10, "and it lands in the scratch");

    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniCell.pItemList = &g_miniItem;   /* bFlags29 clear */
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    CheckEqI32(InkAt(0, 10), 0, "an item list without the flag draws no marker");

    /* --- the small-unit marker --- */
    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniCell.bFlags44 = 1;
    g_miniCell.pUnit    = &g_miniUnit;    /* type 0: small */
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    Check(InkAt(0, 1) != 0, "an operative small unit draws glyph phase / 3 = 1");

    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniCell.bFlags44 = 1;
    g_miniCell.pUnit    = &g_miniUnit;
    g_miniUnit.bStatus  = 1;
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    Check(InkAt(0, 4) != 0, "a non-operative small unit adds 3");

    /* --- the large-unit marker: direction from its home cell, +12 when not operative --- */
    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniCell.bFlags44 = 1;
    g_miniCell.pUnit    = &g_miniUnit;
    g_miniUnit.nUnitType = 4;             /* large */
    g_miniUnit.pCell     = &g_miniHome;
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    Check(InkAt(0, 16) != 0, "same cell gives 4 * (phase / 3 + 3) = 16");

    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniCell.bFlags44 = 1;
    g_miniCell.pUnit    = &g_miniUnit;
    g_miniUnit.nUnitType = 4;
    g_miniUnit.pCell     = &g_miniHome;
    g_miniHome.nX = 0; g_miniHome.nY = 1;
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    Check(InkAt(0, 18) != 0, "same column gives 4 * (phase / 3) + 14");

    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniCell.bFlags44 = 1;
    g_miniCell.pUnit    = &g_miniUnit;
    g_miniUnit.nUnitType = 4;
    g_miniUnit.pCell     = &g_miniHome;
    g_miniHome.nX = 1; g_miniHome.nY = 0;
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    Check(InkAt(0, 17) != 0, "same row gives 4 * (phase / 3) + 13");

    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniCell.bFlags44 = 1;
    g_miniCell.pUnit    = &g_miniUnit;
    g_miniUnit.nUnitType = 4;
    g_miniUnit.pCell     = &g_miniHome;
    g_miniHome.nX = 1; g_miniHome.nY = 1;
    g_miniUnit.bStatus   = 1;
    ClearSurface();
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    Check(InkAt(0, 31) != 0, "neither gives 4 * (phase / 3) + 15, plus 12 when not operative");

    /* --- the label: text record copy with the layer count appended --- */
    MiniFixture_Reset();
    g_miniEnv.pLabel      = "AB";
    g_miniEnv.nLayerCount = 2;
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    CheckEqI32(g_miniIso.szText[0], 'A', "the label is copied first");
    CheckEqI32(g_miniIso.szText[1], 'B', "byte for byte");
    CheckEqI32(g_miniIso.szText[2], '2', "and the decimal layer count is appended");
    CheckEqI32(g_miniIso.szText[3], 0, "with the terminator after it");

    /* --- the layer counter ends one past the last layer drawn --- */
    MiniFixture_Reset();
    g_miniEnv.nSpanX = 4;
    g_miniEnv.nSpanY = 4;
    g_miniEnv.nLayerCount = 2;
    MapView_DrawMinimap(&g_miniIso, &g_miniEnv);
    CheckEqI32(g_miniIso.nLayer, 2, "two layers run the pass twice");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestIsoConstants(); /* verify CRT initialization before installing fixtures or drawing */
    InstallProbeFont();

    TestWallPals();
    TestNulls();
    TestAnimGlyphs();
    TestFacingGlyph();
    TestResolve();
    TestSelectMarkers();
    TestHpGlyphs();
    TestViewportGuide();
    TestHeightGlyph();
    TestFacingArrow();
    TestIsoLayer();
    TestUnitGate();
    TestUnitDirections();
    TestUnitCeilMin();
    TestUnitMarkers();
    TestSelectionFrame();
    TestUnitPoseTable();
    TestUnitWeaponPass();
    TestUnitNulls();
    TestMinimap();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
