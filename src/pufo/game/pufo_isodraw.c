/* pufo_isodraw.c - see pufo_isodraw.h for evidence and the field map. */
#include "game/pufo_isodraw.h"

#include "game/pufo_font.h"   /* Font_DrawGlyph_IndexedOffset / g_fontSys */
#include "game/pufo_mapctx.h" /* PUFO_MAP_CELL_DIM */

void MapView_DrawCellFloorGlyph(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim)
{
    Mc1Rec *pFloorSpr;

    if (pIso == NULL || pIso->pCell == NULL) {
        return;
    }
    pFloorSpr = pIso->pCell->pFloorSpr;
    if (pFloorSpr == NULL) {
        return;
    }
    Font_DrawGlyph_IndexedOffset(pIso->nDrawX, pIso->nDrawY - (i32)pFloorSpr->gap_25,
                                 pFloorSpr, pAnim->nAnimPhase, pIso->nPal);
}

void MapView_DrawCellOccupantGlyph(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim)
{
    Mc1Rec *pCeilSpr;

    if (pIso == NULL || pIso->pCell == NULL) {
        return;
    }
    pCeilSpr = pIso->pCell->pCeilSpr;
    if (pCeilSpr == NULL) {
        return;
    }
    Font_DrawGlyph_IndexedOffset(pIso->nDrawX, pIso->nDrawY - (i32)pCeilSpr->gap_25,
                                 pCeilSpr, pAnim->nAnimPhase, pIso->nPal);
}

/* Shared body of the two wall helpers; only the sprite field differs. */
static void MapView_DrawWallGlyph(IsoDrawCtx *pIso, Mc1Rec *pWallSpr,
                                  const IsoAnimGlobals *pAnim)
{
    u8 bPal;

    if (pWallSpr->bBlocksMove != 0) {
        if ((pIso->pCell->bFlags29 & 1) != 0) {
            bPal = pAnim->nAnimPhaseScratch0;
        } else if ((pIso->pCell->bFlags29 & 4) != 0) {
            bPal = 7;
        } else {
            bPal = 0;
        }
        pIso->bWallPalIdx = bPal;
    } else {
        pIso->bWallPalIdx = pAnim->nAnimPhase;
    }
    Font_DrawGlyph_IndexedOffset(pIso->nDrawX, pIso->nDrawY, pWallSpr,
                                 pIso->bWallPalIdx, pIso->nPal);
}

void MapView_SetWallPalFromSlotA(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim)
{
    if (pIso == NULL || pIso->pCell == NULL || pIso->pCell->pWallSprA == NULL) {
        return;
    }
    MapView_DrawWallGlyph(pIso, pIso->pCell->pWallSprA, pAnim);
}

void MapView_SetWallPalFromSlotB(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim)
{
    if (pIso == NULL || pIso->pCell == NULL || pIso->pCell->pWallSprB == NULL) {
        return;
    }
    MapView_DrawWallGlyph(pIso, pIso->pCell->pWallSprB, pAnim);
}

/* unk_AA018, read from the binary: 00 08 0C 10 00 04 00 00. */
static const u8 g_aAnimGlyphOffset[8] = { 0, 8, 12, 16, 0, 4, 0, 0 };

/* TactUnit_DrawAnimGlyphs @ 0x1c6ac. */
void TactUnit_DrawAnimGlyphs(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim,
                             const IsoSpriteBases *pBases)
{
    i32 nKind;

    if (pIso == NULL || pIso->pCell == NULL) {
        return;
    }

    nKind = (i32)(pIso->pCell->nFlags32 & 7u);
    if (nKind == 0) {
        return;
    }

    (void)Font_DrawGlyph(pIso->nDrawX, pIso->nDrawY,
                         (u16)(g_aAnimGlyphOffset[nKind] + (pAnim->nAnimPhase >> 1) +
                               pBases->nSmokeBase),
                         pIso->nPal);

    if (nKind == 4) {
        /* The second glyph uses the entry for kind 1, which is 8. */
        (void)Font_DrawGlyph(pIso->nDrawX, pIso->nDrawY,
                             (u16)(g_aAnimGlyphOffset[1] + (pAnim->nAnimPhase >> 1) +
                                   pBases->nSmokeBase),
                             pIso->nPal);
    }
}

/* TactUnit_DrawFacingGlyph @ 0x1c754. */
void TactUnit_DrawFacingGlyph(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim,
                              const IsoSpriteBases *pBases)
{
    u32 nFlagsField;

    if (pIso == NULL || pIso->pCell == NULL) {
        return;
    }
    if (pAnim->bPhaseActive == 0) {
        return;
    }

    nFlagsField = pIso->pCell->nFlags32 & 0x78u;
    if (nFlagsField == 0 || nFlagsField > 0x40u) {
        return;
    }

    (void)Font_DrawGlyph(pIso->nDrawX - 48, pIso->nDrawY - 12,
                         (u16)(pBases->nExplBase - ((pIso->pCell->nFlags32 >> 3) & 0xFu) + 8u),
                         0);
}

/* MapView_bResolveIsoCell @ 0x1b0d4. */
bool MapView_bResolveIsoCell(IsoDrawCtx *pIso, const IsoGrid *pGrid)
{
    i32 nTileX;
    i32 nTileY;
    i32 nHalfY;
    i32 bOdd;
    i32 nLayer;
    i32 nMapX;
    i32 nMapY;

    nTileY = pIso->nTileY;
    nTileX = pIso->nTileX;

    /* C's / truncates toward zero, which is what __rt_sdiv does and what the ARM's
     * MOVS / ADDMI / ASR idiom computes. The odd test is the remainder being non-zero. */
    nHalfY = nTileY / 2;
    bOdd = (nTileY % 2) != 0;

    nMapY = pIso->nIsoOffA + nTileX + nHalfY;
    nMapX = nHalfY - nTileX + pIso->nIsoOffB;
    if (bOdd != 0) {
        nMapX += 1;
    }

    pIso->nMapY = nMapY;
    pIso->nMapX = nMapX;

    pIso->bOob = (nMapY < 0 || nMapX < 0 ||
                  nMapY > 10 * pGrid->nDimX - 1 ||
                  nMapX > 10 * pGrid->nDimY - 1) ? 1u : 0u;

    nLayer = (i32)pIso->nLayer;

    pIso->nDrawX = 32 * nTileX - 16;
    if (bOdd != 0) {
        pIso->nDrawX = 32 * nTileX - 32;
    }
    pIso->nDrawY = 8 * (nTileY - 3 * nLayer - 4);

    if (pIso->bOob != 0 || pIso->nDrawY + 40 <= 0) {
        return false;
    }

    /* The array is MapCell *[4][60][60]. The ARM multiplies by 0x3C twice, which is both the layer
     * stride in cells and sizeof(MapCell) on the original - the compiler folded them into one
     * constant. */
    pIso->pCell = pGrid->apCells[PUFO_MAP_CELL_DIM * (PUFO_MAP_CELL_DIM * nLayer + nMapX) + nMapY];
    pIso->pUnit = pIso->pCell->pUnit;

    if (pIso->pCell->pCeilSpr != NULL) {
        pIso->nObjParam = (i32)pIso->pCell->pCeilSpr->iCeilParam;
    } else {
        pIso->nObjParam = 0;
    }

    if ((pIso->pCell->bFlags29 & 0x10u) != 0) {
        if ((pIso->pCell->nFlags32 & 7u) == 4u) {
            pIso->nPal = 0;
        } else {
            pIso->nPal = (i32)pIso->pCell->bUnk56;
        }
    } else {
        pIso->nPal = 255;
    }

    return true;
}
