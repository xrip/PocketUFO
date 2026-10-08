/* test_isodraw.c - the per-cell isometric glyph draw helpers
 * (MapView_DrawCellFloorGlyph @ 0x1c904, MapView_DrawCellOccupantGlyph @ 0x1b45c,
 * MapView_SetWallPalFromSlotA @ 0x1c7dc, MapView_SetWallPalFromSlotB @ 0x1c870).
 *
 * The draw call itself is a no-op with no loaded font, so these tests focus on the
 * wall-palette selection, which is the only branching behaviour. */
#include <stdio.h>
#include <string.h>

#include "game/pufo_isodraw.h"

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

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestWallPals();
    TestNulls();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
