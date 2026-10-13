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

int MapView_DrawUnitHeightGlyph(IsoDrawCtx *pIso, const IsoHeightGate *pGate,
                                const IsoSpriteBases *pBases)
{
    double dA, dB, dC, dSin, dCos, dOff, dAux;

    /* The gate, in the order the original tests it. X and Y really are compared swapped. */
    if (pGate->bFireStatus2 == 0) return 0;
    if (pGate->bFireStatus9 == 0) return 0;
    if (pIso->nMapX != pGate->nFireTgtY) return 0;
    if (pIso->nMapY != pGate->nFireTgtX) return 0;
    if ((i32)pIso->nLayer != pGate->nFireTgtZ) return 0;
    if (pGate->nHeightGlyphWord == 0) return 0;

    /* ARM: MOVS / ADDMI #0xF / ASR #4 then SUB R0, R0, R3, LSL#4. That is a modulo, and C's %
     * already matches its truncation-toward-zero sign rule. */
    dA = (double)(pGate->nHeightSrcX % 16);
    dB = (double)(pGate->nHeightSrcY % 16);
    dC = (double)(pGate->nHeightSrcZ % 12);
    pIso->dHeightA = dA;
    pIso->dHeightB = dB;
    pIso->dHeightC = dC;

    /* The operation order is the original's, constants and all. sin * scale is exactly 1.0 and
     * cos * scale exactly 0.5, so the products land on integers and a different association could
     * move one across the truncation. */
    dSin = sin(g_dblIsoAngle);
    dOff = ((dA * dSin * g_dblIsoScale) + 2.0) - (dB * dSin * g_dblIsoScale) - 2.0;
    pIso->nHeightOff = (i32)dOff;

    dCos = cos(g_dblIsoAngle);
    dAux = (((dB * dCos) + (dA * dCos)) * g_dblIsoScale + 40.0) - 2.0 - (dC * 2.0) - 20.0 - 8.0;
    pIso->nHeightAux = (i32)dAux;

    /* LOWORD on both halves, and the glyph id is read as a u16 here even though the gate above
     * tested the whole dword. */
    (void)Font_DrawGlyph(pIso->nHeightOff + pIso->nDrawX,
                         pIso->nDrawY + pIso->nHeightAux + 15,
                         (u16)(10 * (i32)(pGate->nHeightGlyphWord & 0xFFFFu)
                               + (pGate->nFireTickProgress & 0xFFFF)
                               + (i32)pBases->nSmokeBase + 16),
                         1);
    return 1;
}

/* ------------------------------------------------------------ iso unit animation tables
 *
 * The nine static tables MapView_DrawUnit (0x1c958) reads. Dumped from pufobeta.exe and
 * checked byte for byte by docs/parity/audit_tables.py. Extents come from the image, not
 * from the pseudocode; the reasoning is in docs/parity/game-core.md.
 *
 * Index shapes, all proven from ARM in MapView_DrawUnit:
 *   g_aUnitWalkKind      5 * (bFacing + 16 * nUnitType + 8 * bWalkHalf) + bAnimStep  0x1CF6C
 *   g_aUnitAnimFrame      3 * nAnimFrame + k, three u16 per frame                    0x1D048
 *   g_aUnitWalkGlyph     27 * nUnitType + 9 * bUnitFlag4 + nDirection                0x1CBD0
 *   g_aPoseFrameTable    5 * (nAnimParam + g_aUnitTypePoseBase[nUnitType]) + nWalkKind 0x1CFE4
 *   g_aUnitTypePoseBase   nUnitType                                                  0x1CFE0
 *   g_aUnitHeightOff      8 * nUnitType + nAnimPhase                                 0x1D190
 *   g_aBobPhaseY          Map + 0x37                                                 0x1D568
 *   g_aUnitWeaponYOff     nUnitType                                                  0x1D3F4
 *   g_aUnitFacingDrawOff  2 * (8 * nUnitType + bFacing) + nAxis                      0x1D290
 */

/* aUnitWalkKind @ 0xa6b00. 23 unit types x 80 bytes. Each type block is two 40-byte halves,
 * selected by the ADDNE #8 at 0x1CF78, and each half is eight facings x five walk steps.
 * The five values are a permutation of the animation slots for that facing. */
const u8 g_aUnitWalkKind[PUFO_ISO_WALK_KIND_COUNT] = {
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       3,    1,    2,    0,    4,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       3,    0,    2,    1,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    3,    0,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    0,    3,    1,    2,
       4,    0,    3,    2,    1,    0,    1,    2,    4,    3,
       0,    1,    2,    3,    4,    1,    2,    3,    4,    0,
       3,    1,    0,    2,    4,    1,    0,    2,    4,    3,
       0,    4,    3,    2,    1,    4,    3,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    0,    1,    2,    4,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    4,    1,    2,    3,    0,    4,    1,    2,    3,
       0,    1,    2,    4,    3,    1,    2,    0,    4,    3,
       1,    2,    0,    4,    3,    1,    2,    0,    4,    3,
       4,    3,    1,    2,    0,    3,    4,    0,    1,    2,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       0,    1,    2,    3,    4,    0,    1,    2,    3,    4,
       0,    1,    2,    3,    4,    1,    2,    0,    3,    4,
       3,    4,    1,    2,    0,    3,    4,    1,    2,    0,
       3,    4,    1,    2,    0,    4,    3,    0,    1,    2,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0,
       1,    4,    0,    0,    0,    1,    4,    0,    0,    0,
       1,    4,    0,    0,    0,    4,    1,    0,    0,    0,
       4,    1,    0,    0,    0,    4,    1,    0,    0,    0
};

/* aUnitAnimFrame @ 0xa7230. 78 frames of three u16: a base, a phase scale and a facing scale.
 * 78 * 6 = 468 bytes and the four bytes after it are zero padding, so the table ends at
 * 0xA7404 rather than at the 0xA7408 where aUnitWalkGlyph begins. */
const u16 g_aUnitAnimFrame[PUFO_ISO_ANIM_FRAME_COUNT][3] = {
    {    0,    1,    1 },
    {  240,    1,    1 },
    {   40,    8,   24 },
    {  240,    1,    1 },
    {   32,    1,    1 },
    {  267,    1,    1 },
    {   16,    1,    1 },
    {   24,    1,    1 },
    {   56,    8,   24 },
    {  275,    1,    1 },
    {    8,    1,    1 },
    {  232,    1,    1 },
    {  248,    1,    1 },
    {  256,    1,    1 },
    {   48,    8,   24 },
    {  232,    1,    1 },
    {  248,    1,    1 },
    {  264,    3,    0 },
    {    8,    1,    1 },
    {   75,    1,    1 },
    {   16,    1,    1 },
    {   24,    5,    5 },
    {   64,    3,    0 },
    {    0,    1,    1 },
    {   91,    1,    1 },
    {   67,    1,    1 },
    {   83,    1,    1 },
    {   24,    1,    1 },
    {   32,    8,    8 },
    {   96,    3,    0 },
    {  107,    1,    1 },
    {  123,    1,    1 },
    {   99,    1,    1 },
    {  115,    1,    1 },
    {    0,    1,    1 },
    {    8,    8,    8 },
    {   72,    3,    0 },
    {    0,    1,    1 },
    {   32,    8,   24 },
    {   24,    1,    1 },
    {   16,    1,    1 },
    {   48,    8,   24 },
    {    8,    1,    1 },
    {   40,    8,   24 },
    {  224,    3,    0 },
    {    0,    8,    0 },
    {    8,    8,    0 },
    {   26,    2,    0 },
    {    0,    6,    0 },
    {    0,    3,    0 },
    {    0,    1,    1 },
    {    8,    1,    1 },
    {   16,    1,    1 },
    {   24,    1,    1 },
    {   64,    1,    1 },
    {   72,    1,    1 },
    {   80,    1,    1 },
    {   88,    1,    1 },
    {   96,    1,    1 },
    {   32,    1,    1 },
    {   40,    1,    1 },
    {   48,    1,    1 },
    {   56,    1,    1 },
    {  104,    8,    0 },
    {  112,    8,    0 },
    {  120,    8,    0 },
    {    0,    8,    0 },
    {    8,    8,    0 },
    {   16,    8,    0 },
    {   24,    8,    0 },
    {   32,    8,    0 },
    {   40,    8,    0 },
    {   48,    8,    0 },
    {   32,    4,   16 },
    {   36,    4,   16 },
    {   40,    4,   16 },
    {   44,    4,   16 },
    {   72,   16,    0 },
};

/* aUnitWalkGlyph @ 0xa7408. 27 bytes per unit type: three rows of nine, one row per value
 * of bUnitFlag4. Types 0 through 21 have data and type 22 does not - 27 * 22 = 594 and the
 * pose table begins six bytes later, at 0xA7660. */
const u8 g_aUnitWalkGlyph[PUFO_ISO_WALK_GLYPH_TYPE_COUNT][3][9] = {
    /* type  0 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type  1 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type  2 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type  3 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type  4 */ { {    0,    0,    0,    0,   51,   52,   53,   54,   55 },
                   {    0,    0,    0,    0,   51,   52,   53,   54,   55 },
                   {    0,    0,    0,    0,   51,   52,   53,   54,   55 } },
    /* type  5 */ { {    0,    0,    0,    0,   51,   52,   53,   54,   56 },
                   {    0,    0,    0,    0,   51,   52,   53,   54,   56 },
                   {    0,    0,    0,    0,   51,   52,   53,   54,   56 } },
    /* type  6 */ { {    0,    0,    0,    0,   51,   52,   53,   54,   57 },
                   {    0,    0,    0,    0,   51,   52,   53,   54,   57 },
                   {    0,    0,    0,    0,   51,   52,   53,   54,   57 } },
    /* type  7 */ { {    0,   64,   65,   66,   60,   61,   62,   63,   58 },
                   {    0,   64,   65,   66,   60,   61,   62,   63,   58 },
                   {    0,    0,    0,    0,   60,   61,   62,   63,   58 } },
    /* type  8 */ { {    0,   64,   65,   66,   60,   61,   62,   63,   59 },
                   {    0,   64,   65,   66,   60,   61,   62,   63,   59 },
                   {    0,    0,    0,    0,   60,   61,   62,   63,   59 } },
    /* type  9 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 10 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 11 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 12 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 13 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 14 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 15 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 16 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 17 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 18 */ { {    0,   71,   72,   73,   67,   68,   69,   70,    0 },
                   {    0,   71,   72,   73,   67,   68,   69,   70,    0 },
                   {    0,   71,   72,   73,   67,   68,   69,   70,    0 } },
    /* type 19 */ { {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 },
                   {    0,    0,    0,    0,    0,    0,    0,    0,    0 } },
    /* type 20 */ { {    0,    0,    0,    0,   51,   52,   53,   54,    0 },
                   {    0,    0,    0,    0,   74,   75,   76,   77,    0 },
                   {    0,    0,    0,    0,   51,   52,   53,   54,    0 } },
    /* type 21 */ { {    0,    0,    0,    0,   51,   52,   53,   54,    0 },
                   {    0,    0,    0,    0,   74,   75,   76,   77,    0 },
                   {    0,    0,    0,    0,   51,   52,   53,   54,    0 } },
};

/* aPoseFrameTable @ 0xa7660. 152 rows of five. The row is nAnimParam plus the unit type's
 * base from g_aUnitTypePoseBase, and the column is the walk-kind value just read out of
 * g_aUnitWalkKind. 152 rows is forced, not guessed: the largest base is 133, the largest
 * pose offset is 17, and 5 * (133 + 17) + 4 = 754 fits 760 and nothing smaller. */
const u8 g_aPoseFrameTable[PUFO_ISO_POSE_FRAME_COUNT][5] = {
    {    1,    5,    8,   11,    0 },
    {    1,    5,    7,   11,    0 },
    {    1,    5,   10,   11,    0 },
    {    3,    5,    9,   15,    0 },
    {    1,    5,   10,   11,    0 },
    {    1,    5,    8,   12,    0 },
    {    1,    5,    7,   12,    0 },
    {    1,    5,   10,   12,    0 },
    {    3,    5,    9,   16,    0 },
    {    1,    5,   10,   16,    0 },
    {    2,    5,    8,   13,    0 },
    {    2,    5,    7,   13,    0 },
    {    2,    5,   10,   13,    0 },
    {    4,    5,    9,   17,    0 },
    {    4,    5,   10,   17,    0 },
    {    1,    5,    8,   14,    0 },
    {    1,    5,    7,   14,    0 },
    {    1,    5,   10,   14,    0 },
    {   18,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {   19,   21,    0,   24,    0 },
    {   19,   21,    0,   24,    0 },
    {   19,   22,    0,   24,    0 },
    {   19,   22,    0,   24,    0 },
    {    0,    0,    0,    0,    0 },
    {   19,   21,    0,   25,    0 },
    {   19,   21,    0,   25,    0 },
    {   19,   22,    0,   25,    0 },
    {   19,   22,    0,   25,    0 },
    {    0,    0,    0,    0,    0 },
    {   20,   21,    0,   26,    0 },
    {   20,   21,    0,   26,    0 },
    {   20,   22,    0,   26,    0 },
    {   20,   22,    0,   26,    0 },
    {   19,    0,    0,    0,    0 },
    {   19,   21,    0,   27,    0 },
    {   19,   21,    0,   27,    0 },
    {   23,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    1,   28,    7,   11,    0 },
    {    0,    0,    0,    0,    0 },
    {    1,   28,   29,   11,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    1,   28,    7,   32,    0 },
    {    0,    0,    0,    0,    0 },
    {    1,   28,   29,   32,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {   31,   28,    7,   33,    0 },
    {    0,    0,    0,    0,    0 },
    {   31,   28,   29,   33,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    1,   28,    7,   34,    0 },
    {    0,    0,    0,    0,    0 },
    {   30,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   36,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   36,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   36,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {   37,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {   38,   40,   41,   43,    0 },
    {    0,    0,    0,    0,    0 },
    {   39,   40,   42,   44,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {   45,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   46,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   46,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   47,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {   48,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   49,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   49,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {   50,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   36,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   36,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   36,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,    0,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {    0,   35,    0,    0,    0 },
    {   78,    0,    0,    0,    0 },
};

/* aUnitTypePoseBase @ 0xa7958. One row base per unit type. Types 0 through 10 share row 0;
 * the rest are multiples of 19. Type 22 is 133, which is what fixes the pose table extent. */
const u8 g_aUnitTypePoseBase[PUFO_ISO_UNIT_TYPE_COUNT] = {
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   19,
      38,   57,   76,   95,   57,   57,    0,  114,    0,    0,  133
};

/* aUnitHeightOff @ 0xa7a28. Eight signed bytes per unit type, indexed by the animation
 * phase. 23 * 8 = 184 bytes, and aDirDeltaXY begins at 0xA7AE0, so the extent is closed
 * at both ends. */
const i8 g_aUnitHeightOff[PUFO_ISO_UNIT_TYPE_COUNT][8] = {
    {    1,    0,   -2,    0,    1,    0,   -2,    0 },
    {    1,    0,   -1,    0,    1,    0,   -1,    0 },
    {    1,    0,   -1,    0,    1,    0,   -1,    0 },
    {    1,    0,   -1,    0,    1,    0,   -1,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    1,    0,   -1,    0,    1,    0,   -1,    0 },
    {    1,    1,    0,   -1,    1,    1,    0,   -1 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    3,    3,    2,    1,    0,    0,    1,    2 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    1,    0,   -1,    0,    1,    0,   -1,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
    {    0,    0,    0,    0,    0,    0,    0,    0 },
};

/* aBobPhaseY @ 0xa7be0. The bob cycle for the eight animation phases. The extent is not a
 * guess: the very next bytes are the ASCII string xcom_0. */
const i8 g_aBobPhaseY[8] = {
       0,   -1,   -2,   -3,   -3,   -2,   -1,    0
};

/* aUnitWeaponYOff @ 0xa9310. One signed byte per unit type. Only type 9 (5) and type 11
 * (-1) are not zero. */
const i8 g_aUnitWeaponYOff[PUFO_ISO_UNIT_TYPE_COUNT] = {
       0,    0,    0,    0,    0,    0,    0,    0,    0,    5,    0,   -1,
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0
};

/* aUnitFacingDrawOff @ 0xa93a8. Sixteen signed bytes per unit type, two per facing, eight
 * facings. The table is 224 bytes, so it covers 14 unit types and no more. The next table,
 * unnamed in the IDB at 0xA9488, is a u16 table Map_TraceVoxelOccupancy (0x3242c) indexes
 * as 4 * (8 * nUnitType + bFacing); it runs 736 bytes and ends exactly at
 * g_aUnitMuzzleOffset @ 0xa9768. A unit of type 14 or above
 * that reaches the facing branch at 0x1D288 reads into that other table. */
const i8 g_aUnitFacingDrawOff[PUFO_ISO_FACING_OFF_TYPE_COUNT][8][2] = {
    /* type  0 */ { {   9,  -7 },
                   {  11,  -3 },
                   {   8,   1 },
                   {   2,   1 },
                   { -10,   1 },
                   { -14,  -4 },
                   {  -9,  -9 },
                   {  -2,  -8 } },
    /* type  1 */ { {   9,  -7 },
                   {  11,  -3 },
                   {   8,   1 },
                   {   2,   1 },
                   { -10,   1 },
                   { -14,  -4 },
                   {  -9,  -9 },
                   {  -2,  -8 } },
    /* type  2 */ { {   9,  -7 },
                   {  11,  -3 },
                   {   8,   1 },
                   {   2,   1 },
                   { -10,   1 },
                   { -14,  -4 },
                   {  -9,  -9 },
                   {  -2,  -8 } },
    /* type  3 */ { {   9,  -7 },
                   {  11,  -3 },
                   {   8,   1 },
                   {   2,   1 },
                   { -10,   1 },
                   { -14,  -4 },
                   {  -9,  -9 },
                   {  -2,  -8 } },
    /* type  4 */ { {  11,  -8 },
                   {  13,  -3 },
                   {   8,   1 },
                   {   3,   5 },
                   { -12,   1 },
                   { -16,  -4 },
                   {  -9,  -8 },
                   {  -2, -10 } },
    /* type  5 */ { {  11,  -8 },
                   {  13,  -3 },
                   {   8,   1 },
                   {   3,   5 },
                   { -12,   1 },
                   { -16,  -4 },
                   {  -9,  -8 },
                   {  -2, -10 } },
    /* type  6 */ { {  11,  -8 },
                   {  13,  -3 },
                   {   8,   1 },
                   {   3,   5 },
                   { -12,   1 },
                   { -16,  -4 },
                   {  -9,  -8 },
                   {  -2, -10 } },
    /* type  7 */ { {  11,  -8 },
                   {  13,  -3 },
                   {   8,   1 },
                   {   3,   5 },
                   { -12,   1 },
                   { -16,  -4 },
                   {  -9,  -8 },
                   {  -2, -10 } },
    /* type  8 */ { {  11,  -8 },
                   {  13,  -3 },
                   {   8,   1 },
                   {   3,   5 },
                   { -12,   1 },
                   { -16,  -4 },
                   {  -9,  -8 },
                   {  -2, -10 } },
    /* type  9 */ { {   8,  -7 },
                   {   8,  -2 },
                   {   2,   1 },
                   {   3,   8 },
                   {  -7,   1 },
                   { -10,  -3 },
                   {  -7,  -6 },
                   {  -2,  -6 } },
    /* type 10 */ { {  11,  -9 },
                   {  11,  -5 },
                   {   3,  -1 },
                   {  -1,   0 },
                   { -11,   0 },
                   { -14,  -6 },
                   {  -9,  -8 },
                   {  -2,  -6 } },
    /* type 11 */ { {  11,  -9 },
                   {  11,  -4 },
                   {   6,  -1 },
                   {   3,   2 },
                   { -11,   0 },
                   { -13,  -5 },
                   {  -8,  -8 },
                   {  -2, -11 } },
    /* type 12 */ { {  11,  -8 },
                   {  10,  -3 },
                   {   6,   1 },
                   {   3,   3 },
                   { -10,   1 },
                   { -13,  -4 },
                   {  -8,  -7 },
                   {  -2, -10 } },
    /* type 13 */ { {   7,  -6 },
                   {   7,  -3 },
                   {   5,   0 },
                   {   2,   5 },
                   {  -9,   0 },
                   {  -8,  -4 },
                   {  -9,  -8 },
                   {  -2, -10 } },
};
