/* pufo_isodraw.c - see pufo_isodraw.h for evidence and the field map. */
#include "game/pufo_isodraw.h"

#include "game/pufo_font.h"   /* Font_DrawGlyph_IndexedOffset / g_fontSys */
#include "game/pufo_mapctx.h" /* PUFO_MAP_CELL_DIM */
#include "render/pufo_render.h" /* SoftSurf_BlitSrcRect */

#include <math.h>

double g_dblIsoAngle; /* unk_1571F8 */
double g_dblIsoScale; /* unk_156AA8 */

/* Math_StoreSqrtConst @ 0x37098. The multiplier is the double 0x3FB0000000000000, which is 2^-4,
 * so this is sqrt(320)/16 = sqrt(5)/2. */
void Math_StoreSqrtConst(void)
{
    g_dblIsoScale = sqrt(320.0) * 0.0625;
}

/* Math_StoreAtan2Const @ 0x370e8. atan2(32, 16) is the angle of the 32 by 16 iso tile, so its
 * tangent is exactly 2. */
void Math_StoreAtan2Const(void)
{
    g_dblIsoAngle = atan2(32.0, 16.0);
}

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

/* MapView_bCellIsOccupantTarget: the predicate both select markers repeat verbatim. */
bool MapView_bCellIsOccupantTarget(const MapCell *pCell)
{
    return MapCell_HasOccupant(pCell) &&
           (((pCell->bFlags44 & 1u) != 0) || Unit_bIsOperative(pCell->pUnit));
}

/* The pair draw both markers end in. A is always relative to the cursor base. B is relative only for
 * the high marker; the low marker's B is already an absolute index. */
static void DrawSelectGlyphPair(IsoDrawCtx *pIso, const IsoSpriteBases *pBases, bool bAbsoluteB)
{
    i32 nB;

    (void)Font_DrawGlyph(pIso->nDrawX, pIso->nDrawY,
                         (u16)((i32)(u16)pIso->nGlyphA + (i32)pBases->nCursorBase), 0);

    if (pIso->nGlyphB != 0) {
        nB = bAbsoluteB ? pIso->nGlyphB
                        : (i32)pBases->nCursorBase + (i32)(u16)pIso->nGlyphB;
        (void)Font_DrawGlyph(pIso->nDrawX, pIso->nDrawY, (u16)nB, 0);
    }
}

/* MapView_DrawCellSelectMarker @ 0x1b280. */
void MapView_DrawCellSelectMarker(IsoDrawCtx *pIso, const IsoSelectGate *pGate,
                                  const IsoSpriteBases *pBases)
{
    MapCell *pCell = pIso->pCell;
    i32      nIdx;

    if (pGate->bQueueActive != 0) {
        return;
    }
    if (((pCell->bFlags57[0] & 8u) == 0) && ((pCell->bFlags29 & 0x40u) == 0)) {
        return;
    }

    pIso->nGlyphB = 0;

    if ((i32)pIso->nLayer == pGate->nMapLayers - 1 || (pCell->bFlags29 & 0x40u) != 0) {
        if (pGate->nActionLatch > 1) {
            switch (pGate->nActionLatch) {
            case 2:
                pIso->nGlyphA = MapView_bCellIsOccupantTarget(pCell)
                                    ? (pGate->nAnimPhase >> 1) + 7
                                    : 6;
                DrawSelectGlyphPair(pIso, pBases, true);
                return;

            case 3:
                if ((pCell->bFlags29 & 0x40u) != 0) {
                    /* The original scans the eight apMultiTargets slots and keeps the index even on a
                     * miss, so a cell that is not multi-targeted still gets a glyph, at index 8. */
                    pIso->nGlyphA = 7;
                    for (nIdx = 0; nIdx < PUFO_ISODRAW_MULTI_TARGET_SLOTS; ++nIdx) {
                        if (pGate->apMultiTargets[nIdx] == pCell) {
                            break;
                        }
                    }
                    pIso->nGlyphB = nIdx + (i32)pBases->nGlyphBase + 49;
                    DrawSelectGlyphPair(pIso, pBases, true);
                    return;
                }
                break;

            case 4:
                break;

            default:
                /* Mode 5 or above: the original draws with whatever the previous cell left in the
                 * context. Reproduced rather than tidied. */
                DrawSelectGlyphPair(pIso, pBases, true);
                return;
            }
        }

        pIso->nGlyphA = MapView_bCellIsOccupantTarget(pCell) ? (pGate->nAnimPhase >> 2) : 0;
    } else {
        pIso->nGlyphA = 2;
    }

    DrawSelectGlyphPair(pIso, pBases, true);
}

/* MapView_DrawCellSelectMarkerHi @ 0x1b584. */
void MapView_DrawCellSelectMarkerHi(IsoDrawCtx *pIso, const IsoSelectGate *pGate,
                                    const IsoSpriteBases *pBases)
{
    MapCell *pCell = pIso->pCell;
    i32      nPhase = pGate->nAnimPhase;

    if (pGate->bQueueActive != 0) {
        return;
    }
    if ((pCell->bFlags57[0] & 8u) == 0) {
        return;
    }

    pIso->nGlyphB = 0;

    if ((i32)pIso->nLayer != pGate->nMapLayers - 1) {
        pIso->nGlyphA = 5;
    } else if (pGate->nActionLatch == 1) {
        pIso->nGlyphB = (nPhase >> 2) + 15;
        pIso->nGlyphA = MapView_bCellIsOccupantTarget(pCell) ? (nPhase >> 2) + 3 : 3;
    } else if (pGate->nActionLatch == 2) {
        pIso->nGlyphA = MapView_bCellIsOccupantTarget(pCell) ? (nPhase >> 1) + 7 : 6;
    } else if (pGate->nActionLatch == 3) {
        pIso->nGlyphA = MapView_bCellIsOccupantTarget(pCell) ? (nPhase >> 2) + 3 : 3;
        pIso->nGlyphB = (nPhase >> 2) + 13;
    } else if (pGate->nActionLatch == 4) {
        pIso->nGlyphA = MapView_bCellIsOccupantTarget(pCell) ? (nPhase >> 2) + 3 : 3;
        pIso->nGlyphB = (nPhase >> 2) + 11;
    } else if (pGate->nActionLatch != 0) {
        /* Any other mode: A stays stale, B stays zero. */
    } else {
        pIso->nGlyphA = MapView_bCellIsOccupantTarget(pCell) ? (nPhase >> 2) + 3 : 3;
    }

    DrawSelectGlyphPair(pIso, pBases, false);
}

/* TactUnit_DrawHpGlyphs @ 0x1b4b0. */
void TactUnit_DrawHpGlyphs(IsoDrawCtx *pIso, const IsoSpriteBases *pBases)
{
    TactItem *pItem;
    i32       nValue;

    if (pIso->pCell->pItemList == NULL) {
        return;
    }

    /* Walk past the leading items whose object is two-handed-exclusive. The cursor is kept in the
     * context, as the original does. */
    pItem = pIso->pItemCursor = pIso->pCell->pItemList;
    while (pItem->pObj->bTwoHanded == 2) {
        pItem = pItem->pNext;
        pIso->pItemCursor = pItem;
        if (pItem == NULL) {
            break;
        }
    }

    if (pItem != NULL) {
        (void)Font_DrawGlyph(pIso->nDrawX, pIso->nObjParam + pIso->nDrawY,
                             (u16)((i32)pBases->nBulletsBase + 7), pIso->nPal);
    }

    /* The head item, reloaded - not the item the walk stopped at. */
    nValue = (i32)pIso->pCell->pItemList->pObj->aItemQty[50];
    pIso->nHpGlyph = nValue;

    if (nValue != 0) {
        pIso->nHpGlyph = nValue - 1;
        (void)Font_DrawGlyph(pIso->nDrawX, pIso->nObjParam + pIso->nDrawY,
                             (u16)((i32)pBases->nFloorobBase + nValue - 1), pIso->nPal);
    }
}

/* Every segment of the guide is the same source tile at the same key; only the rectangle moves. */
#define GUIDE_SRC_X 172
#define GUIDE_SRC_Y 184
#define GUIDE_KEY   150
#define GUIDE_KEY_EDGE 128

/* MapView_DrawViewportGuide @ 0x1aadc. */
void MapView_DrawViewportGuide(const IsoViewportGuide *pGuide)
{
    i32 v2;
    i32 v3;
    i32 v4;
    u32 nFlags = pGuide->nUiSlot5;

    if (pGuide->bGameTickHold != 0 || pGuide->bActionReady == 0 ||
        pGuide->bPlayerInputEnabled == 0 || pGuide->bCursorBusy != 0) {
        return;
    }

    v2 = (pGuide->bDeployPanelOpen != 0) ? 303 : 320 - pGuide->nHudPadHeight;
    v3 = v2 / 3;
    v4 = v2 - v3;

    SoftSurf_BlitSrcRect(15, 15, 210, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(15, 15, 1, v2 - 30, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(15, v2 - 15, 211, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(225, 15, 1, v2 - 30, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(80, 0, 1, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(0, v3, 15, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(160, 0, 1, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(225, v3, 15, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(225, v4, 15, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(160, v2 - 15, 1, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(80, v2 - 15, 1, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(0, v4, 15, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(0, v2, 240, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);

    /* Priority order is bottom, top, left, right. */
    if ((nFlags & 0xFF000000u) != 0) {
        if ((nFlags & 0xFFu) != 0) {
            SoftSurf_BlitSrcRect(0, v2 - v3, 15, v3 - 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
            SoftSurf_BlitSrcRect(0, v2 - 15, 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        } else if ((nFlags & 0xFF00u) != 0) {
            SoftSurf_BlitSrcRect(225, v2 - v3, 15, v3 - 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
            SoftSurf_BlitSrcRect(160, v2 - 15, 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        } else {
            SoftSurf_BlitSrcRect(80, v2 - 15, 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        }
        return;
    }

    if ((nFlags & 0xFF0000u) != 0) {
        if ((nFlags & 0xFFu) != 0) {
            SoftSurf_BlitSrcRect(0, 15, 15, v3 - 14, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
            SoftSurf_BlitSrcRect(0, 0, 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        } else if ((nFlags & 0xFF00u) != 0) {
            SoftSurf_BlitSrcRect(225, 15, 15, v3 - 14, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
            SoftSurf_BlitSrcRect(160, 0, 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        } else {
            SoftSurf_BlitSrcRect(80, 0, 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        }
        return;
    }

    if ((nFlags & 0xFFu) != 0) {
        SoftSurf_BlitSrcRect(0, v3, 15, v4 - v3, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        return;
    }

    if ((nFlags & 0xFF00u) != 0) {
        SoftSurf_BlitSrcRect(225, v3, 15, v4 - v3, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
    }
}
