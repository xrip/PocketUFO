/* pufo_isodraw.c - see pufo_isodraw.h for evidence and the field map. */
#include "game/pufo_isodraw.h"
#include "ui/pufo_uigeom.h" /* QoL wide-view shim: identity in portrait */

#include "game/pufo_ballist.h" /* Ball_TruncDiv16, the original's voxel-to-cell divide */
#include "game/pufo_font.h"   /* Font_DrawGlyph_IndexedOffset / Font_DrawGlyph / g_fontSys */
#include "game/pufo_mapctx.h" /* PUFO_MAP_CELL_DIM */
#include "game/pufo_tactui.h" /* Tact_MapTypeToGlyphIdx */
#include "game/pufo_unit.h"   /* Unit_bIsOperative / Unit_bIsLarge */
#include "render/pufo_render.h" /* SoftSurf_BlitSrcRect / PadImage_BlitEx */

#include "util/pufo_str.h" /* pufo_strcat, the bounded label append */

#include <math.h>
#include <stdio.h>  /* snprintf, the bounded stand-in for the original's itoa and byte copy */

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

/* Raw _cinit @ 0xA1628 tail-calls the function table [0xA61E0,0xA621C) before WinMain.
 * Entries 0xA61F8/0xA61FC are 0x37094/0x370E4; each raw B branches to the store at
 * 0x37098/0x370E8. Use the same Windows CRT section for every executable linking this object. */
static void __cdecl IsoMath_CrtInit(void)
{
    Math_StoreSqrtConst();
    Math_StoreAtan2Const();
}

#pragma section(".CRT$XCU", read)
__attribute__((used))
__declspec(allocate(".CRT$XCU"))
static void (__cdecl *const g_pfnIsoMathCrtInit)(void) = IsoMath_CrtInit;

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

    SoftSurf_BlitSrcRect(PufoUi_MapX(15), PufoUi_MapY(15), 210, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(15), PufoUi_MapY(15), 1, v2 - 30, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(15), PufoUi_MapY(v2 - 15), 211, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(225), PufoUi_MapY(15), 1, v2 - 30, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(80), PufoUi_MapY(0), 1, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(0), PufoUi_MapY(v3), 15, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(160), PufoUi_MapY(0), 1, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(225), PufoUi_MapY(v3), 15, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(225), PufoUi_MapY(v4), 15, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(160), PufoUi_MapY(v2 - 15), 1, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(80), PufoUi_MapY(v2 - 15), 1, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(0), PufoUi_MapY(v4), 15, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);
    SoftSurf_BlitSrcRect(PufoUi_MapX(0), PufoUi_MapY(v2), 240, 1, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY);

    /* Priority order is bottom, top, left, right. */
    if ((nFlags & 0xFF000000u) != 0) {
        if ((nFlags & 0xFFu) != 0) {
            SoftSurf_BlitSrcRect(PufoUi_MapX(0), PufoUi_MapY(v2 - v3), 15, v3 - 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
            SoftSurf_BlitSrcRect(PufoUi_MapX(0), PufoUi_MapY(v2 - 15), 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        } else if ((nFlags & 0xFF00u) != 0) {
            SoftSurf_BlitSrcRect(PufoUi_MapX(225), PufoUi_MapY(v2 - v3), 15, v3 - 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
            SoftSurf_BlitSrcRect(PufoUi_MapX(160), PufoUi_MapY(v2 - 15), 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        } else {
            SoftSurf_BlitSrcRect(PufoUi_MapX(80), PufoUi_MapY(v2 - 15), 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        }
        return;
    }

    if ((nFlags & 0xFF0000u) != 0) {
        if ((nFlags & 0xFFu) != 0) {
            SoftSurf_BlitSrcRect(0, 15, 15, v3 - 14, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
            SoftSurf_BlitSrcRect(PufoUi_MapX(0), PufoUi_MapY(0), 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        } else if ((nFlags & 0xFF00u) != 0) {
            SoftSurf_BlitSrcRect(225, 15, 15, v3 - 14, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
            SoftSurf_BlitSrcRect(PufoUi_MapX(160), PufoUi_MapY(0), 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
        } else {
            SoftSurf_BlitSrcRect(PufoUi_MapX(80), PufoUi_MapY(0), 80, 15, 0, GUIDE_SRC_X, GUIDE_SRC_Y, GUIDE_KEY_EDGE);
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

/* The x/y nudge MapView_DrawUnitFacingArrow adds to a roster glyph, indexed 10 * nRowId + n.
 *
 * The original keeps the same 64 bytes twice - byte_A9290 @ 0xA9290 for x, byte_A92D0 @ 0xA92D0 for
 * y - and the two are byte for byte identical, so the port keeps one copy. Both addresses are
 * registered against this symbol in docs/parity/audit_tables.py.
 *
 * The values are read with LDRSB, so they are signed: -3 to 0, and only 13 of the 64 are non-zero. */
#define PUFO_ARROW_GLYPH_OFF_COUNT 64

static const i8 g_abArrowGlyphOff[PUFO_ARROW_GLYPH_OFF_COUNT] = {
    -1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, -1, -1, -1, -1, -2,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, -2, -1,
    -1, -1, -1, -1, -1, -1,  0,  0, -3,  0,  0,  0,  0,  0,  0,  0,
     0,  0, -2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

int MapView_DrawUnitFacingArrow(IsoDrawCtx *pIso, const IsoArrowGate *pGate,
                                const IsoSpriteBases *pBases)
{
    double dSin, dCos, dX, dY, dZ, dA, dB, dC;
    i32 nDrawn = 0;
    i32 nFloorX, nFloorY, nZq, nSel;

    /* The one gate all three branches share: the shot-trace flag on the cell. */
    if ((pIso->pCell->bFlags29 & 0x20u) == 0) return 0;

    /* Branch 1, 0x1B7F0. The fire animation is running, so the glyph is muzzle smoke at the unit's
     * own facing offset. The flag is tested before pUnit, and a null unit ends the call. */
    if (pGate->bFireAnimActive != 0) {
        const Soldier *pUnit = pIso->pUnit;
        if (pUnit == NULL) return 0;
        (void)Font_DrawGlyph(pIso->nDrawX + pUnit->nDrawOffX,
                             pIso->nDrawY + pIso->nObjParam + pUnit->nDrawOffY,
                             (i32)pBases->nSmokeBase + (i32)(u16)(pGate->nPhaseCounter % 4) + 56,
                             pIso->nPal);
        return 1;
    }

    dSin = sin(g_dblIsoAngle);
    dCos = cos(g_dblIsoAngle);

    if (pGate->bFireStatus1 != 0) {
        /* Branch 2, 0x1B870: the ten-entry multi-target roster. The cursor lives in the context and
         * the original zeroes it here even when no entry matches. */
        pIso->nRosterIndex = 0;
        do {
            const i32 n = pIso->nRosterIndex;

            if (pGate->anAimRow[n] > 0) {
                dX = pGate->adMtX[n];
                dY = pGate->adMtY[n];
                dZ = pGate->adMtZ[n];

                /* Voxel to cell: x and y use the original's truncating idiom, z truncates. The compares are in the
                 * original's order and stop at the first mismatch. */
                nFloorX = Ball_TruncDiv16((i32)dX);
                nFloorY = Ball_TruncDiv16((i32)dY);
                nZq     = (i32)dZ / 12;

                if (nFloorX == (i32)pIso->pCell->nX
                    && nFloorY == (i32)pIso->pCell->nY
                    && nZq == (i32)pIso->pCell->nZ) {
                    dA = dX - (double)(16 * nFloorX);
                    dB = dY - (double)(16 * nFloorY);
                    dC = dZ - (double)(12 * nZq);
                    pIso->dHeightA = dA;
                    pIso->dHeightB = dB;
                    pIso->dHeightC = dC;

                    pIso->nHeightOff = (i32)(((dA * dSin) * g_dblIsoScale + 16.0)
                                             - (dB * dSin) * g_dblIsoScale);
                    pIso->nHeightAux = (i32)((((dB * dCos) + (dA * dCos)) * g_dblIsoScale
                                             + 40.0) - 16.0 - (dC * 2.0));

                    /* 0x1BD18 has no bound test: nRowId is the raw aim class id Tact_ComputeAimVector
                     * stores at Map+0x14088 (0x305C0), and 10 * nRowId + n runs past the 64-byte
                     * nudge table for anything above 5. The port assumes the documented 0..5. */
                    nSel = 10 * pGate->nRowId + n;
                    (void)Font_DrawGlyph(pIso->nDrawX + pIso->nHeightOff + g_abArrowGlyphOff[nSel],
                                         pIso->nDrawY + pIso->nHeightAux + g_abArrowGlyphOff[nSel],
                                         (i32)(u16)pGate->anAimRow[n]
                                             + (i32)pBases->nBulletsBase - 1,
                                         pIso->nPal);
                    ++nDrawn;
                }
            }
            pIso->nRosterIndex = n + 1;
        } while (pIso->nRosterIndex < 10);
        return nDrawn;
    }

    /* Branch 3, 0x1BDC8: the thrown item's own floor-object glyph, at the end of roster entry 0. No
     * cell compare and no aim-row test. ORIGINAL BUG (0x1C1F4): the original tests only the flag and
     * then dereferences the pointer, so a flag set without a stored item is a null dereference. The
     * port keeps the same unguarded dereference. */
    if (pGate->bTrajHitStop == 0) return 0;

    dX = pGate->adMtX[0];
    dY = pGate->adMtY[0];
    dZ = pGate->adMtZ[0];
    nFloorX = Ball_TruncDiv16((i32)dX);
    nFloorY = Ball_TruncDiv16((i32)dY);
    nZq     = (i32)dZ / 12;

    dA = dX - (double)(16 * nFloorX);
    dB = dY - (double)(16 * nFloorY);
    dC = dZ - (double)(12 * nZq);
    pIso->dHeightA = dA;
    pIso->dHeightB = dB;
    pIso->dHeightC = dC;

    /* This branch subtracts the 16.0 it just added, and keeps the height glyph's -20.0 -8.0 tail.
     * The subtraction is not folded away because the original does not fold it. */
    pIso->nHeightOff = (i32)(((dA * dSin) * g_dblIsoScale + 16.0)
                             - (dB * dSin) * g_dblIsoScale - 16.0);
    pIso->nHeightAux = (i32)(((((dB * dCos) + (dA * dCos)) * g_dblIsoScale
                               + 40.0) - 16.0 - (dC * 2.0)) - 20.0 - 8.0);

    (void)Font_DrawGlyph(pIso->nDrawX + pIso->nHeightOff,
                         pIso->nDrawY + pIso->nHeightAux,
                         (i32)pGate->pThrownItem->pObj->aItemQty[50]
                             + (i32)pBases->nFloorobBase - 1,
                         pIso->nPal);
    return 1;
}

/* ------------------------------------------------------------ the tactical map frame
 *
 * MapView_DrawIsoLayer @ 0x21e2c. The only caller of the per-cell chain, so it is where the seven
 * per-cell gates have to exist at the same time. */

/* The multi-target bars stop at five whatever the unit's count says. */
#define PUFO_ISO_MULTI_TARGET_BARS 5

void MapView_DrawIsoLayer(IsoDrawCtx *pIso, const IsoLayerEnv *pEnv)
{
    i32 nWidth, nOutDrawY;

    /* The original calls the two iso helpers with the context base and lets them read nMapDimX/Y
     * out of it; the port passes the dimensions in (see pufo_mapcell.h). */

    pIso->nIsoOffA = Map_IsoOffsetA(pEnv->nMapDimX, pEnv->nCamScrollX, pEnv->nCamScrollY);
    pIso->nIsoOffB = Map_IsoOffsetB(pEnv->nMapDimY, pEnv->nCamScrollX, pEnv->nCamScrollY);
    pIso->nLayer = 0;

    if ((i32)pEnv->nLayerCount > 0) {
        /* The layer counter is the context byte at 0x38, incremented and re-tested as a byte. */
        do {
            /* Pass 1: floor sprites only, so the floor sits under everything pass 2 draws. */
            for (pIso->nTileY = 0; pIso->nTileY < (i32)pEnv->nTileCountY; ++pIso->nTileY) {
                for (pIso->nTileX = 0; pIso->nTileX < (i32)pEnv->nTileCountX; ++pIso->nTileX) {
                    if (MapView_bResolveIsoCell(pIso, pEnv->pGrid)) {
                        MapView_DrawCellFloorGlyph(pIso, pEnv->pAnim);
                    } else if (pIso->nLayer == 0) {
                        /* A cell that does not resolve still gets the blank group glyph, but only on
                         * the bottom layer, and always with palette 0 rather than the cell's. */
                        (void)Font_DrawGlyph(pIso->nDrawX, pIso->nDrawY,
                                             pEnv->pUnit->paGroupBase[0], 0);
                    }
                }
            }


            /* Pass 2: the rest of the chain, in the original's order. */
            for (pIso->nTileY = 0; pIso->nTileY < (i32)pEnv->nTileCountY; ++pIso->nTileY) {
                for (pIso->nTileX = 0; pIso->nTileX < (i32)pEnv->nTileCountX; ++pIso->nTileX) {
                    if (MapView_bResolveIsoCell(pIso, pEnv->pGrid)) {
                        MapView_DrawCellSelectMarker(pIso, pEnv->pSelect, pEnv->pBases);
                        MapView_SetWallPalFromSlotA(pIso, pEnv->pAnim);
                        MapView_SetWallPalFromSlotB(pIso, pEnv->pAnim);
                        MapView_DrawCellOccupantGlyph(pIso, pEnv->pAnim);
                        TactUnit_DrawHpGlyphs(pIso, pEnv->pBases);
                        (void)MapView_DrawUnit(pIso, pEnv->pUnit);
                        (void)MapView_DrawUnitFacingArrow(pIso, pEnv->pArrow, pEnv->pBases);
                        (void)MapView_DrawUnitHeightGlyph(pIso, pEnv->pHeight, pEnv->pBases);
                        TactUnit_DrawAnimGlyphs(pIso, pEnv->pAnim, pEnv->pBases);
                        MapView_DrawCellSelectMarkerHi(pIso, pEnv->pSelect, pEnv->pBases);
                        TactUnit_DrawFacingGlyph(pIso, pEnv->pAnim, pEnv->pBases);
                    } else if (pIso->nLayer == 0) {
                        (void)Font_DrawGlyph(pIso->nDrawX, pIso->nDrawY,
                                             pEnv->pUnit->paGroupBase[0], 0);
                    }
                }
            }

            pIso->nLayer = (u8)(pIso->nLayer + 1);
        } while ((i32)pIso->nLayer < (i32)pEnv->nLayerCount);
    }


    MapView_DrawViewportGuide(pEnv->pGuide);

    (void)TactHud_DrawSelectedUnitPanel(pEnv->pBank, pEnv->pPanel, pEnv->pColor,
                                        pEnv->pfnDecimalBlit, pEnv->pDecimalUser,
                                        pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx, pEnv->pCtlUser);


    /* The multi-target bars. ORIGINAL BUG (0x22080): Unit_bIsOperative is called with Map.pSelUnit
     * with no null test, even though StatusMsg_DrawStack tests the same pointer. */
    if (Unit_bIsOperative(pEnv->pSelUnit) && (i32)pEnv->pSelUnit->nMultiTarget > 0) {
        pIso->nScratch2 = 0;
        do {
            const i32 nIdx = pIso->nScratch2;

            if (nIdx >= PUFO_ISO_MULTI_TARGET_BARS) break;

            pIso->nTileX = PufoUi_MapX(207);
            pIso->nTileY = PufoUi_MapY((pEnv->bDeployPanelOpen != 0)
                                           ? (300 - 18 * nIdx - 17)
                                           : (300 - 18 * nIdx - pEnv->nHudPadHeight));

            /* 0x2211C stores the bar index with STRB, so it is the low byte of 10 * phase + 128. */
            SoftSurf_BlitRectProxy(0, pIso->nTileX, pIso->nTileY, 30, 16,
                                   (u8)(10 * (i32)pEnv->nMapPhase + 128), 0, 0);

            (void)snprintf(pIso->szText, sizeof(pIso->szText), "%d", nIdx + 1);
            (void)Gfx_PackRgb565Pair(pEnv->pColor, 55, 255, 55, 0, 0, 0);
            nWidth = Text_MeasureWidth(pIso->szText);
            Text_DrawString(pIso->nTileX + (30 - nWidth) / 2, pIso->nTileY, pIso->szText);

            pIso->nScratch2 = nIdx + 1;
        } while (pIso->nScratch2 < (i32)pEnv->pSelUnit->nMultiTarget);
    }

    /* The original hands the draw context to StatusMsg_DrawStack where that function wants its
     * status scratch; the port gives it the real owner. */

    (void)StatusMsg_DrawStack(pEnv->pStatus, &nOutDrawY, pEnv->pStatusHooks);
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 26, 244, 252, 13, 122, 126);

    (void)UiControl_BlitActivePad(pEnv->pLookCtl, pEnv->pfnCtlBlit,
                                  pEnv->pfnCtlBlitEx, pEnv->pCtlUser);
    if (pEnv->pLookCtl->bEnabled != 0) {
        SoftSurf_BlitRectProxy(0, pEnv->pLookCtl->x + 50,
                               6 * ((i32)pEnv->nMapPhase / 3) + pEnv->pLookCtl->y + 2,
                               2, 4, 0xDC, 0, 0);

        /* ORIGINAL BUG (0x22254): the label is copied byte by byte to its NUL with no bound, and a
         * text record is 205 bytes while the context buffer is 84. snprintf bounds the copy. */
        (void)snprintf(pIso->szText, sizeof(pIso->szText), "%s", pEnv->pPadLabel);
        nWidth = Text_MeasureWidth(pIso->szText);
        Text_DrawString(pEnv->pLookCtl->x + (51 - nWidth) / 2,
                        pEnv->pLookCtl->y + 3, pIso->szText);
    }


    /* The mission fade-out cover, skipped on the two screens that already own the frame. */
    if (pEnv->nUiScreen != 0 && pEnv->nUiScreen != 13) {
        SoftSurf_BlitSrcRectEx(g_pMainSurface, 0, 0, g_pMainSurface->nWidth,
                                   g_pMainSurface->nHeight, 0x37, 55, 140, 128);
    }
}

/* ------------------------------------------------------------ the minimap
 *
 * MapView_DrawMinimap @ 0x1ed4c. The look-mode overview; see the header for the env and for the
 * tile-size pair at Map+0x30/0x32 that bounds both cell passes. */

/* One of the cell's four sprite records: uUnk20 is its minimap glyph id, and 0 means the sprite has
 * no minimap glyph at all. The glyph index is stored in the context scratch exactly as the original
 * stores it, even when nothing is drawn. */
static void DrawMinimapSpriteGlyph(IsoDrawCtx *pIso, const IsoSpriteBases *pBases,
                                   const Mc1Rec *pSpr, i32 x, i32 y, i32 nPal)
{
    i32 nIdx;

    if (pSpr == NULL) {
        return;
    }
    nIdx = (i32)pSpr->uUnk20 + 35;
    pIso->nScratch = nIdx;
    if (nIdx == 35) {
        return;
    }
    /* The overview window is top-anchored; the shim only centers x on the wide face. */
    (void)Font_DrawGlyph(PufoUi_MapX(x), y, (u16)((i32)pBases->nScangBase + nIdx), nPal);
}

void MapView_DrawMinimap(IsoDrawCtx *pIso, const IsoMinimapEnv *pEnv)
{
    const IsoGrid   *pGrid = pEnv->pGrid;
    MapCell         *pCell;
    MapGeom          geom;
    i32              nScrollX, nScrollY;
    i32              nIdx;

    SoftSurf_BlitRectProxy(0, 0, 0, g_pMainSurface->nWidth, g_pMainSurface->nHeight,
                               0, 0, 0);

    /* The scroll clamp, max first then min, each comparing the value the previous clamp left. The
     * ARM's MOVS / ADDMI #3 / ASR #2 is truncating division, which C's / reproduces; the dimension
     * is a u8 byte read with LDRB before the subtraction. */
    nScrollX = *pEnv->pnScrollX;
    if (nScrollX > 40 * (pGrid->nDimX - 3) / 4) {
        nScrollX = 40 * (pGrid->nDimX - 3) / 4;
        *pEnv->pnScrollX = nScrollX;
    }
    nScrollY = *pEnv->pnScrollY;
    if (nScrollY > 40 * (pGrid->nDimY - 4) / 4) {
        nScrollY = 40 * (pGrid->nDimY - 4) / 4;
        *pEnv->pnScrollY = nScrollY;
    }
    if (nScrollX < -30) {
        nScrollX = -30;
        *pEnv->pnScrollX = nScrollX;
    }
    if (nScrollY < -40) {
        nScrollY = -40;
        *pEnv->pnScrollY = nScrollY;
    }

    geom.nDimX = (u8)pGrid->nDimX;
    geom.nDimY = (u8)pGrid->nDimY;

    /* Pass 1: the ground glyph on every valid layer-0 cell. It runs even when there are no layers,
     * and its sprite is the fixed record at flat index 2. ORIGINAL BUG (0x1EEA4): the original
     * dereferences the lookup result with no null test. */
    for (pIso->nTileY = nScrollY; pIso->nTileY < (i32)pEnv->nSpanY >> 2; ++pIso->nTileY) {
        for (pIso->nTileX = nScrollX; pIso->nTileX < (i32)pEnv->nSpanX >> 2; ++pIso->nTileX) {
            if (Map_IsValidCell(&geom, pIso->nTileX, pIso->nTileY, 0)) {
                Mc1Rec *pGround = Font_GetSprByIndex(&g_fontSys, 2u);

                nIdx = (i32)pGround->uUnk20 + 35;
                pIso->nScratch = nIdx;
                pCell = pGrid->apCells[PUFO_MAP_CELL_DIM * pIso->nTileY + pIso->nTileX];
                (void)Font_DrawGlyph(PufoUi_MapX(4 * (pIso->nTileX - nScrollX)), 4 * (pIso->nTileY - nScrollY),
                                     (u16)((i32)pEnv->pBases->nScangBase + nIdx),
                                     (i32)pCell->bUnk56);
            }
        }
    }

    /* Pass 2: the per-layer content. Validity is still tested at layer 0 - the bounds are
     * layer-independent, so that is enough for the cell fetch below. */
    pIso->nLayer = 0;
    if ((i32)pEnv->nLayerCount > 0) {
        do {
            const i32 nLayer = (i32)pIso->nLayer;

            for (pIso->nTileY = nScrollY; pIso->nTileY < (i32)pEnv->nSpanY >> 2; ++pIso->nTileY) {
                for (pIso->nTileX = nScrollX; pIso->nTileX < (i32)pEnv->nSpanX >> 2; ++pIso->nTileX) {
                    if (Map_IsValidCell(&geom, pIso->nTileX, pIso->nTileY, 0)) {
                        const i32 nX = pIso->nTileX;
                        const i32 nY = pIso->nTileY;
                        const i32 nXs = PufoUi_MapX(4 * (nX - nScrollX));
                        const i32 nYs = 4 * (nY - nScrollY);

                        pCell = pGrid->apCells[PUFO_MAP_CELL_DIM * (PUFO_MAP_CELL_DIM * nLayer + nY)
                                               + nX];
                        pIso->nPal = ((pCell->bFlags29 & 0x10u) != 0) ? (i32)pCell->bUnk56 : 255;

                        DrawMinimapSpriteGlyph(pIso, pEnv->pBases, pCell->pFloorSpr, nXs, nYs,
                                               pIso->nPal);
                        DrawMinimapSpriteGlyph(pIso, pEnv->pBases, pCell->pWallSprA, nXs, nYs,
                                               pIso->nPal);
                        DrawMinimapSpriteGlyph(pIso, pEnv->pBases, pCell->pWallSprB, nXs, nYs,
                                               pIso->nPal);
                        DrawMinimapSpriteGlyph(pIso, pEnv->pBases, pCell->pCeilSpr, nXs, nYs,
                                               pIso->nPal);

                        if (pCell->pItemList != NULL && (pCell->bFlags29 & 0x10u) != 0) {
                            nIdx = (i32)pEnv->nMapPhase / 3 + 9;
                            pIso->nScratch = nIdx;
                            (void)Font_DrawGlyph(nXs, nYs,
                                                 (u16)((i32)pEnv->pBases->nScangBase + nIdx), 0);
                        }

                        if ((pCell->bFlags44 & 1u) != 0 && MapCell_HasOccupant(pCell)) {
                            Soldier *pUnit = pCell->pUnit;
                            pIso->pUnit = pUnit;

                            if (Unit_bIsLarge(pUnit)) {
                                const MapCell *pHome = pUnit->pCell;
                                const i32      nPhase = (i32)pEnv->nMapPhase / 3;

                                if ((i32)pHome->nX == nX) {
                                    nIdx = ((i32)pHome->nY == nY) ? 4 * (nPhase + 3)
                                                                  : 4 * nPhase + 14;
                                } else {
                                    nIdx = ((i32)pHome->nY == nY) ? 4 * nPhase + 13
                                                                  : 4 * nPhase + 15;
                                }
                                if (pUnit->bStatus != UNIT_STATUS_OPERATIVE) {
                                    nIdx += 12;
                                }
                            } else {
                                nIdx = (i32)pEnv->nMapPhase / 3;
                                if (pUnit->bStatus != UNIT_STATUS_OPERATIVE) {
                                    nIdx += 3;
                                }
                            }
                            pIso->nScratch = nIdx;
                            (void)Font_DrawGlyph(nXs, nYs,
                                                 (u16)((i32)pEnv->pBases->nScangBase
                                                      + (u16)pIso->nScratch), 0);
                        }
                    }
                }
            }

            pIso->nLayer = (u8)(pIso->nLayer + 1);
        } while ((i32)pIso->nLayer < (i32)pEnv->nLayerCount);
    }

    {
        /* QoL: blit the cursor pad at its shimmed origin (identity portrait). */
        PadImage *pCur = (PadImage *)pEnv->pMiniCursor;
        const i32 nCurX = pCur->nOriginX;
        const i32 nCurY = pCur->nOriginY;

        pCur->nOriginX = PufoUi_MapX(nCurX);
        pCur->nOriginY = PufoUi_MapY(nCurY);
        PadImage_BlitEx(pCur, 0x3F);
        pCur->nOriginX = nCurX;
        pCur->nOriginY = nCurY;
    }

    /* ORIGINAL BUG (0x1F4DC): the label is copied byte by byte to its NUL with no bound; the source
     * is a 205-byte text record and the context buffer is 80. snprintf bounds the copy. The layer
     * count is itoa'd into szText[50] and then appended, exactly as the original does it. */
    (void)snprintf(pIso->szText, sizeof(pIso->szText), "%s", pEnv->pLabel);
    (void)snprintf(&pIso->szText[50], sizeof(pIso->szText) - 50, "%d", (i32)pEnv->nLayerCount);
    (void)pufo_strcat(pIso->szText, &pIso->szText[50], sizeof(pIso->szText));

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 255, 255, 255, 90, 90, 90);
    Text_DrawString(PufoUi_MapX(0), 0, pIso->szText);

    pIso->nScratch2 = 0;
    do {
        (void)UiControl_BlitActivePad(&pEnv->pBank[PUFO_PBANK_MINIMAP_FIRST + pIso->nScratch2],
                                      pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx, pEnv->pCtlUser);
        pIso->nScratch2 = pIso->nScratch2 + 1;
    } while (pIso->nScratch2 < PUFO_PBANK_MINIMAP_COUNT);
}

/* ------------------------------------------------------------ the command pad panel (screen 4) */

/* The unit-type gate at 0x1FCEC and 0x1FFA4. Types 4..8, 14..20 and 22 are the craft family: they
 * skip the command pad slots and centre the second name line. */
static bool CmdPadSkipsUnit(u8 nUnitType)
{
    return nUnitType >= 4 &&
           (nUnitType <= 8 || (nUnitType > 0xD && (nUnitType <= 0x14 || nUnitType == 22)));
}

/* The two pad slots: pad A at (5, 2), pad B at (39, 2). The original rewrites the loaded pads' own
 * origins before every blit (0x1FD84..0x1FE80), so each cs pad keeps the last slot it was drawn at.
 * For classes 0..2 both slots use the same pad: x = 5, blit, x += 34, blit. */
static void BlitCmdPadPair(PadImage *pPadA, PadImage *pPadB)
{
    pPadA->nOriginX = 5;
    pPadA->nOriginY = 2;
    PadImage_Blit(pPadA);
    pPadB->nOriginX = 5;
    pPadB->nOriginY = 2;
    pPadB->nOriginX += 34;
    PadImage_Blit(pPadB);
}

/* szText = decimal(nA) "/" decimal(nB). The original runs j_itoa into szText[50], copies it down to
 * szText[0], appends the separator, then itoa's the second value into szText[50] and appends that
 * (0x20064..0x200c0). snprintf gives the same digits, bounded. */
static void CmdPadPair(IsoDrawCtx *pIso, i32 nA, i32 nB, const char *pszSep)
{
    (void)snprintf(&pIso->szText[50], sizeof(pIso->szText) - 50, "%d", nA);
    pufo_strcpy(pIso->szText, &pIso->szText[50]);
    (void)pufo_strcat(pIso->szText, pszSep, sizeof(pIso->szText));
    (void)snprintf(&pIso->szText[50], sizeof(pIso->szText) - 50, "%d", nB);
    (void)pufo_strcat(pIso->szText, &pIso->szText[50], sizeof(pIso->szText));
}

/* szText = decimal(nVal) alone. */
static void CmdPadNumber(IsoDrawCtx *pIso, i32 nVal)
{
    (void)snprintf(&pIso->szText[50], sizeof(pIso->szText) - 50, "%d", nVal);
    pufo_strcpy(pIso->szText, &pIso->szText[50]);
}

void Tact_DrawCommandPads(IsoDrawCtx *pIso, const TactCmdPadEnv *pEnv)
{
    Soldier *pUnit = pEnv->pSelUnit;

    PadImage_Blit(pEnv->pSoldierPad);

    if (!CmdPadSkipsUnit(pUnit->nUnitType)) {
        if (pUnit->uMissions <= 3) {
            /* Empty slots: a grey 32x32 frame with a black 30x30 inside at each position. */
            SoftSurf_BlitRectProxy(0, 5, 2, 32, 32, 0x96, 0x96, 0x96);
            SoftSurf_BlitRectProxy(0, 39, 2, 32, 32, 0x96, 0x96, 0x96);
            SoftSurf_BlitRectProxy(0, 6, 3, 30, 30, 0, 0, 0);
            SoftSurf_BlitRectProxy(0, 40, 3, 30, 30, 0, 0, 0);
        } else {
            switch (pUnit->nClass) {
            case 0: BlitCmdPadPair(pEnv->apCsPad[0], pEnv->apCsPad[0]); break;
            case 1: BlitCmdPadPair(pEnv->apCsPad[1], pEnv->apCsPad[1]); break;
            case 2: BlitCmdPadPair(pEnv->apCsPad[2], pEnv->apCsPad[2]); break;
            case 3: BlitCmdPadPair(pEnv->apCsPad[0], pEnv->apCsPad[1]); break;
            case 4: BlitCmdPadPair(pEnv->apCsPad[1], pEnv->apCsPad[2]); break;
            case 5: BlitCmdPadPair(pEnv->apCsPad[0], pEnv->apCsPad[2]); break;
            default: break;
            }
        }
    }

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 255, 255, 255, 90, 90, 90);
    Text_DrawString(75, 2, Unit_FormatDisplayName(pUnit, 1, pEnv->panRankLow, pEnv->panRankOther));

    if (CmdPadSkipsUnit(pUnit->nUnitType)) {
        /* The original calls mode 2 twice here (0x1FFB4 / 0x1FFC4); the second call only feeds
         * Text_MeasureWidth and returns the same buffer. The divide is the compiler's
         * truncating divide-by-two (MOVS / ADDMI #1 / ASR #1). */
        const char *pszName = Unit_FormatDisplayName(pUnit, 2, pEnv->panRankLow, pEnv->panRankOther);
        Text_DrawString(120 - Text_MeasureWidth(pszName) / 2, 24, pszName);
    } else {
        Text_DrawString(75, 20, Unit_FormatDisplayName(pUnit, 2, pEnv->panRankLow, pEnv->panRankOther));
    }

    /* TU, green: uTu / auStat[0], bar at y 61, remainder at y 62. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 20, 250, 20, 4, 90, 4);
    Text_DrawString(10, 45, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 56));
    CmdPadPair(pIso, pUnit->uTu, pUnit->auStat[0], "/");
    Text_DrawString(170, 45, pIso->szText);
    SoftSurf_BlitRectProxy(0, 10, 61, (i32)pUnit->auStat[0] >> 1, 6, 0x14, 0xFA, 0x14);
    SoftSurf_BlitRectProxy(0, ((i32)pUnit->uTu >> 1) + 11, 62,
                           ((i32)pUnit->auStat[0] >> 1) - ((i32)pUnit->uTu >> 1) - 2, 4,
                           0x14, 0x14, 0x14);

    /* Stun / armour, yellow: uStunOrArmor / auStat[9]. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 20, 90, 90, 4);
    Text_DrawString(10, 69, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 57));
    CmdPadPair(pIso, pUnit->uStunOrArmor, pUnit->auStat[9], "/");
    Text_DrawString(170, 69, pIso->szText);
    SoftSurf_BlitRectProxy(0, 10, 85, (i32)pUnit->auStat[9] >> 1, 6, 0xFA, 0xFA, 0x14);
    SoftSurf_BlitRectProxy(0, ((i32)pUnit->uStunOrArmor >> 1) + 11, 86,
                           ((i32)pUnit->auStat[9] >> 1) - ((i32)pUnit->uStunOrArmor >> 1) - 2, 4,
                           0x14, 0x14, 0x14);

    /* Hit points, red: uHp / auStat[1]. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 20, 20, 90, 4, 4);
    Text_DrawString(10, 93, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 58));
    CmdPadPair(pIso, pUnit->uHp, pUnit->auStat[1], "/");
    Text_DrawString(170, 93, pIso->szText);
    SoftSurf_BlitRectProxy(0, 10, 109, (i32)pUnit->auStat[1] >> 1, 6, 0xFA, 0x14, 0x14);
    SoftSurf_BlitRectProxy(0, ((i32)pUnit->uHp >> 1) + 11, 110,
                           ((i32)pUnit->auStat[1] >> 1) - ((i32)pUnit->uHp >> 1) - 2, 4,
                           0x14, 0x14, 0x14);

    /* Crouch / primed state, pink: the value alone, then one 12x6 block per point at y 133. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 20, 150, 90, 4, 30);
    Text_DrawString(10, 117, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 59));
    CmdPadNumber(pIso, pUnit->bKneelOrPrimed);
    Text_DrawString(170, 117, pIso->szText);
    pIso->nScratch2 = 0;
    if (pUnit->bKneelOrPrimed != 0) {
        do {
            SoftSurf_BlitRectProxy(0, 14 * pIso->nScratch2 + 10, 133, 12, 6, 0xFA, 0x14, 0x96);
            pIso->nScratch2 = pIso->nScratch2 + 1;
        } while (pIso->nScratch2 < (i32)pUnit->bKneelOrPrimed);
    }

    /* auStat[2], pink-white. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 255, 156, 190, 90, 31, 38);
    Text_DrawString(10, 141, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 60));
    CmdPadNumber(pIso, pUnit->auStat[2]);
    Text_DrawString(170, 141, pIso->szText);
    SoftSurf_BlitRectProxy(0, 10, 157, (i32)pUnit->auStat[2] >> 1, 6, 0xFF, 0x9C, 0xBE);

    /* Morale, purple: bMorale "/100", full bar 200 wide, remainder 2 * (99 - morale). */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 200, 20, 250, 80, 5, 90);
    Text_DrawString(10, 165, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 61));
    CmdPadNumber(pIso, pUnit->bMorale);
    (void)pufo_strcat(pIso->szText, "/100", sizeof(pIso->szText));
    Text_DrawString(170, 165, pIso->szText);
    SoftSurf_BlitRectProxy(0, 10, 181, 200, 6, 0xC8, 0x14, 0xFA);
    SoftSurf_BlitRectProxy(0, 2 * (i32)pUnit->bMorale + 11, 182,
                           2 * (99 - (i32)pUnit->bMorale), 4, 0x14, 0x14, 0x14);

    /* auStat[4] and auStat[5], purple pair. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 200, 120, 250, 80, 40, 90);
    Text_DrawString(10, 189, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 62));
    CmdPadNumber(pIso, pUnit->auStat[4]);
    Text_DrawString(170, 189, pIso->szText);
    SoftSurf_BlitRectProxy(0, 10, 205, (i32)pUnit->auStat[4] >> 1, 6, 0xC8, 0x78, 0xFA);

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 200, 120, 250, 80, 40, 90);
    Text_DrawString(10, 213, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 63));
    CmdPadNumber(pIso, pUnit->auStat[5]);
    Text_DrawString(170, 213, pIso->szText);
    SoftSurf_BlitRectProxy(0, 10, 229, (i32)pUnit->auStat[5] >> 1, 6, 0xC8, 0x78, 0xFA);

    /* auStat[8], red. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 20, 20, 90, 4, 4);
    Text_DrawString(10, 237, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 64));
    CmdPadNumber(pIso, pUnit->auStat[8]);
    Text_DrawString(170, 237, pIso->szText);
    SoftSurf_BlitRectProxy(0, 10, 253, (i32)pUnit->auStat[8] >> 1, 6, 0xFF, 0x14, 0x32);

    /* The two extra rows, only while bPsiActive. */
    if (pUnit->bPsiActive != 0) {
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 200, 210, 250, 20, 20, 20);
        Text_DrawString(10, 261, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 65));
        CmdPadNumber(pIso, pUnit->auStat[7]);
        Text_DrawString(170, 261, pIso->szText);
        SoftSurf_BlitRectProxy(0, 10, 277, (i32)pUnit->auStat[7] >> 1, 6, 0xD2, 0xD2, 0xFA);

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 200, 210, 250, 20, 20, 20);
        Text_DrawString(10, 285, (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * 66));
        CmdPadNumber(pIso, pUnit->auStat[6]);
        Text_DrawString(170, 285, pIso->szText);
        SoftSurf_BlitRectProxy(0, 10, 301, (i32)pUnit->auStat[6] >> 1, 6, 0xC8, 0xD2, 0xFA);
    }

    /* ORIGINAL BUG (0x20AD4): the ruler runs over the label rows that were just drawn. 12 rows at
     * y = 24 * row + 45, 24 columns each: a left tick at x = 5 * (col + 1) with index 5 * (col + 20)
     * and a right tick at x = 5 * (col + 23) with index 250 - 5 * col, each 5x1 with idx == mid ==
     * low, so it paints a colour ramp straight through every label and value. The ARM masks the
     * column counter to a byte before scaling (AND #0xFF at 0x20AE8); the counters are the context
     * dwords at 0x124 / 0x128. Reproduced as written. */
    pIso->nCmdTickRow = 0;
    do {
        pIso->nCmdTickCol = 0;
        do {
            u8 nLeft  = (u8)(5 * ((pIso->nCmdTickCol & 0xFF) + 20));
            u8 nRight = (u8)(250 - 5 * (pIso->nCmdTickCol & 0xFF));
            SoftSurf_BlitRectProxy(0, 5 * (pIso->nCmdTickCol + 1), 24 * pIso->nCmdTickRow + 45,
                                   5, 1, nLeft, nLeft, nLeft);
            SoftSurf_BlitRectProxy(0, 5 * (pIso->nCmdTickCol + 23), 24 * pIso->nCmdTickRow + 45,
                                   5, 1, nRight, nRight, nRight);
            pIso->nCmdTickCol = pIso->nCmdTickCol + 1;
        } while (pIso->nCmdTickCol < 24);
        pIso->nCmdTickRow = pIso->nCmdTickRow + 1;
    } while (pIso->nCmdTickRow < 12);

    /* The panel frame. */
    SoftSurf_BlitRectProxy(0, 5, 2, 230, 1, 0x96, 0x96, 0x96);
    SoftSurf_BlitRectProxy(0, 5, 2, 1, 308, 0x96, 0x96, 0x96);
    SoftSurf_BlitRectProxy(0, 235, 2, 1, 308, 0x96, 0x96, 0x96);

    (void)UiControl_BlitActivePad(&pEnv->pBank[PUFO_PBANK_CMD_FIRST],
                                  pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx, pEnv->pCtlUser);
    if (pEnv->bLookCommitFlag == 0) {
        pIso->nScratch2 = 1;
        do {
            (void)UiControl_BlitActivePad(&pEnv->pBank[PUFO_PBANK_CMD_FIRST + pIso->nScratch2],
                                          pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx, pEnv->pCtlUser);
            pIso->nScratch2 = pIso->nScratch2 + 1;
        } while (pIso->nScratch2 < PUFO_PBANK_CMD_COUNT);
    }
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

/* --------------------------------------------------------------- MapView_DrawUnit
 *
 * MapView_DrawUnit @ 0x1c958, 3,220 bytes, 126 blocks.
 *
 * Entry gate. A unit is drawn only when something at its position blocks movement:
 *   - a small unit: its own draw cell (pIso->pCell) has bFlags44 bit 0 set;
 *   - a large unit: any cell of the 2x2 block anchored on the unit's OWN cell has it set.
 * The 2x2 block is slots idx, idx+1, idx+60 and idx+61 of the flattened cell array: the cell plus
 * its right and lower neighbours.
 *
 * Two mutually exclusive bodies follow, large and small, and both end in the same selection-frame
 * tail. The large body classifies the unit's cell against the draw cell into one of four direction
 * sets written into abUnitFlag, then walks abUnitFlag[0 .. abUnitFlag[3]-1] through
 * g_aUnitWalkGlyph[type][row][dir]. The small body walks five fixed steps through
 * g_aUnitWalkKind[5*(16*type + facing + 8*twoHand) + step].
 *
 * Three Hex-Rays renderings here are wrong and the ARM is authoritative:
 *   - the two-handed-weapon flag is at IsoDrawCtx + 0x12C. Hex-Rays writes `gap_12D[7]`, which is
 *     0x134 -- that slot is the item-list cursor and belongs to other functions.
 *   - `LDRB [R6,#0x14]` is Soldier.nAnimPose, not bFlags. bFlags is at 0x04, confirmed at 0x1D504
 *     by `LDRB R3,[R0,#4]` followed by `ANDS R3,#2`.
 *   - unit types 20 and 21 read nAnimPhase (Map+0xF8); every other type reads unk_1139EF (Map+0x37).
 *     Hex-Rays renders the two swapped.
 *
 * Palette. nPal is saved on the stack at entry and zeroed when the unit is operative, but it is only
 * written back from 0x1D5DC onward. The two not-drawable exits at 0x1CA58 and 0x1CAAC jump straight to
 * the POP, so an operative unit behind an unflagged cell leaves pIso->nPal at 0 for the caller, which
 * reuses it for terrain.
 *
 * Deviation. In the weapon-glyph branch the original reaches `pHandR->pObj` when BOTH hands are
 * empty, which is a null dereference. The port skips the glyph instead. Whether that path is
 * reachable is not proven: Unit_DeriveAnimPose derives nAnimPose from bFlags and the two hands, so
 * poses 5..17 may imply a hand item, but nothing in the IDB proves it.
 */

#define ISO_CELLS_PER_LAYER 3600
#define ISO_CELLS_PER_ROW      60
#define ISO_TYPE_PROBE_COUNT     4  /* Map_abSpriteSet @ Map+0x15116..19, probed by Tact_MapTypeToGlyphIdx */

/* itos -> muls(0.125f) -> muls(scale) -> stoi. The original round-trips through decimal strings;
 * the port keeps the arithmetic in a double and truncates toward zero, the same assumption already
 * recorded for _stoi in pufo_vistry.c. */
static i32 Iso_AnimPhaseTerm(i32 nPhase, i32 nScale)
{
    return (i32)(((double)nPhase * 0.125) * (double)nScale);
}

static bool Iso_HandIsWeapon(const TactItem *pItem)
{
    const ObjectRecFull *pObj;
    u16 uSort;

    if (pItem == NULL) {
        return false;
    }
    pObj = pItem->pObj;
    uSort = pObj->uSortKey;
    return pObj->bListPri == 1 || uSort == 22 || uSort == 24;
}

/* The minimum over the 2x2 block of the ceiling sprite's signed iCeilParam at Mc1Rec + 0x24. Both
 * original reads are LDRSB, so the comparison is signed. */
static i32 Iso_MinCeilParam(MapCell *const *apCells, i32 nIdx)
{
    static const i32 anOff[4] = { 0, 1, ISO_CELLS_PER_ROW, ISO_CELLS_PER_ROW + 1 };
    i32 nMin = 0;
    i32 k;

    for (k = 0; k < 4; ++k) {
        MapCell *pCell = apCells[nIdx + anOff[k]];
        Mc1Rec *pCeil;

        if (pCell == NULL) {
            continue;
        }
        pCeil = pCell->pCeilSpr;
        if (pCeil != NULL && pCeil->iCeilParam < nMin) {
            nMin = pCeil->iCeilParam;
        }
    }
    return nMin;
}

static bool Iso_UnitIsDrawable(const IsoDrawCtx *pIso, const IsoUnitDrawGate *pGate)
{
    Soldier *pUnit = pIso->pUnit;
    MapCell *pCell;
    i32 nIdx;

    if (!Unit_bIsLarge(pUnit)) {
        return (pIso->pCell->bFlags44 & 1) != 0;
    }
    pCell = pUnit->pCell;
    nIdx = ISO_CELLS_PER_LAYER * (i32)pCell->nZ + ISO_CELLS_PER_ROW * (i32)pCell->nY
           + (i32)pCell->nX;
    return (pGate->apCells[nIdx]->bFlags44 & 1) != 0
        || (pGate->apCells[nIdx + 1]->bFlags44 & 1) != 0
        || (pGate->apCells[nIdx + ISO_CELLS_PER_ROW]->bFlags44 & 1) != 0
        || (pGate->apCells[nIdx + ISO_CELLS_PER_ROW + 1]->bFlags44 & 1) != 0;
}

/* 0x1CA60 and 0x1D458. The two cursor markers: 18 above the unit before the body runs, 17 after
 * it. Both need a palette above 7, the unit's cell flag set, and either the select flag or a
 * palette that is not exactly 255. */
static void Iso_DrawCursorMarker(const IsoDrawCtx *pIso, const IsoUnitDrawGate *pGate, u16 uOffset)
{
    i32 nPal = pIso->nPal;

    if (Unit_bIsOperative(pIso->pUnit) || nPal <= 7) {
        return;
    }
    if ((pIso->pUnit->pCell->bFlags44 & 1) != 0 && (pGate->bSelectFlag != 0 || nPal != 255)) {
        Font_DrawGlyph(pIso->nDrawX + pIso->pUnit->nDrawOffX,
                       pIso->nDrawY + pIso->pUnit->nDrawOffY,
                       (u16)(pGate->nCursorBase + uOffset), 0);
    }
}

/* 0x1CB90. A large unit whose cell differs from the drawn cell in BOTH coordinates walks three
 * directions, the third being 8 -- the direction code that lifts the glyph by 4 pixels. */
static void Iso_SetLargeUnitDiagonal(IsoDrawCtx *pIso)
{
    pIso->abUnitFlag[0] = 3;
    pIso->abUnitFlag[1] = 7;
    pIso->abUnitFlag[2] = 8;
    pIso->abUnitFlag[3] = 3;
}

static void Iso_DrawLargeUnitGlyphs(IsoDrawCtx *pIso, const IsoUnitDrawGate *pGate)
{
    Soldier *pUnit = pIso->pUnit;
    MapCell *pCell = pUnit->pCell;
    i32 nIdx;
    i32 nStep;

    pIso->bUnitGlyphRow = (pUnit->nAnimPose == 18) ? 2 : ((pUnit->bFlags & 2) != 0 ? 1 : 0);

    if (pCell == pIso->pCell) {
        pIso->abUnitFlag[0] = 0;
        pIso->abUnitFlag[1] = 4;
        pIso->abUnitFlag[3] = 2;
    } else if (pCell->nX != pIso->pCell->nX) {
        /* 0x1CB48 compares nX first. */
        if (pCell->nY == pIso->pCell->nY) {
            pIso->abUnitFlag[0] = 1;
            pIso->abUnitFlag[1] = 5;
            pIso->abUnitFlag[3] = 2;
        } else {
            Iso_SetLargeUnitDiagonal(pIso);
        }
    } else if (pCell->nY != pIso->pCell->nY) {
        /* 0x1CB78, reached only when nX is equal. */
        pIso->abUnitFlag[0] = 2;
        pIso->abUnitFlag[1] = 6;
        pIso->abUnitFlag[3] = 2;
    } else {
        /* Equal coordinates but a different cell object. The original falls through to the diagonal
         * set because it never tests object identity after the first compare. Not reachable through
         * MapView_bResolveIsoCell, which resolves pIso->pCell out of the same array. */
        Iso_SetLargeUnitDiagonal(pIso);
    }

    nIdx = ISO_CELLS_PER_LAYER * (i32)pCell->nZ + ISO_CELLS_PER_ROW * (i32)pCell->nY
           + (i32)pCell->nX;

    for (nStep = 0; nStep < pIso->abUnitFlag[3]; ++nStep) {
        u8 bDir = pIso->abUnitFlag[nStep];
        const u16 *pFrame;
        u8 nGlyph;
        u8 nPhase;
        i32 nGlyphIdx;

        pIso->bAnimStep = (u8)nStep;
        nGlyph = (pUnit->nUnitType < PUFO_ISO_WALK_GLYPH_TYPE_COUNT)
                     ? g_aUnitWalkGlyph[pUnit->nUnitType][pIso->bUnitGlyphRow][bDir]
                     : 0;
        pIso->nAnimFrame = nGlyph;
        if (nGlyph == 0) {
            continue;
        }
        pIso->nAnimFrame = nGlyph - 1;
        pFrame = g_aUnitAnimFrame[pIso->nAnimFrame];

        nPhase = (pUnit->nUnitType == 20 || pUnit->nUnitType == 21) ? pGate->nAnimPhase
                                                                    : pGate->nMapPhase;
        pIso->nAnimParam = pUnit->bFacing * pFrame[2]
                         + Iso_AnimPhaseTerm(nPhase, pFrame[1]) + pFrame[0];

        pIso->nObjParam = Iso_MinCeilParam(pGate->apCells, nIdx);

        pIso->nSpriteY = pIso->nObjParam + pUnit->nDrawOffY + pIso->nDrawY;
        pIso->nSpriteX = pIso->nDrawX + pUnit->nDrawOffX;
        if (bDir == 8) {
            pIso->nSpriteY -= 4;
        }

        nGlyphIdx = Tact_MapTypeToGlyphIdx(pUnit->nUnitType, pGate->paTypeProbe,
                                           ISO_TYPE_PROBE_COUNT);
        Font_DrawGlyph(pIso->nSpriteX, pIso->nSpriteY,
                       (u16)(pGate->paGroupBase[nGlyphIdx] + (u16)pIso->nAnimParam), 0);
    }

    /* 0x1CE94 stores the incremented step before the loop test, so the walk leaves bAnimStep equal to
     * the step count, not the last index used. */
    pIso->bAnimStep = (u8)nStep;
}

static void Iso_DrawSmallUnitGlyphs(IsoDrawCtx *pIso, const IsoUnitDrawGate *pGate)
{
    Soldier *pUnit = pIso->pUnit;
    i32 nStep;

    pIso->bTwoHandWeapon = (u8)(Iso_HandIsWeapon(pUnit->pHandL) || Iso_HandIsWeapon(pUnit->pHandR));
    /* 0x1CF2C..0x1CF48: an aiming unit (bFlags bit 4 -- "aiming", pufo_unit.h:374) takes the override
     * from the selected item's OBJECT record, not the item: 0x1CF38 loads Map+0x48 (pSelWeapon, a
     * TactItem*), 0x1CF3C derefs it, and 0x1CF40 tests the object's bListPri at +0xA4 against zero.
     * Deviation: the original has no null test on either hop. A mission loaded from a save can hold an
     * aiming unit while Map+0x48 is NULL (its only writers are the click and fire-mode handlers, and the
     * roster chunk restores no weapon), where the original would fault; the shell tests and leaves the
     * flag alone instead. */
    if ((pUnit->bFlags & 4) != 0 && *pGate->apSelWeapon != NULL
            && (*pGate->apSelWeapon)->pObj != NULL && (*pGate->apSelWeapon)->pObj->bListPri == 0) {
        pIso->bTwoHandWeapon = 0;
    }

    for (nStep = 0; nStep < 5; ++nStep) {
        u8 bKind;
        i32 nKey;
        i32 nPose;
        const u16 *pFrame;
        i32 nPhase;
        i32 nYTerm;
        i32 nGlyphIdx;

        pIso->bAnimStep = (u8)nStep;
        nKey = pUnit->bFacing + 16 * (i32)pUnit->nUnitType
             + (pIso->bTwoHandWeapon != 0 ? 8 : 0);
        bKind = g_aUnitWalkKind[5 * nKey + nStep];
        pIso->bAnimKind = bKind;

        if (bKind != 4) {
            i32 nFrame;

            /* The original indexes the flat 760-byte table as 5*(pose + base) + kind. The one
             * special case, 5*base + 33, is the same row form with pose = 6 and kind = 3. */
            nPose = pUnit->nAnimPose;
            i32 nRow = nPose + g_aUnitTypePoseBase[pUnit->nUnitType];
            if ((nPose < 0xF || nPose > 0x11) || pIso->bTwoHandWeapon != 0) {
                nFrame = g_aPoseFrameTable[nRow][bKind];
            } else if (bKind == 3) {
                nFrame = g_aPoseFrameTable[g_aUnitTypePoseBase[pUnit->nUnitType] + 6][3];
            } else {
                nFrame = g_aPoseFrameTable[nRow][bKind];
            }
            pIso->nAnimFrame = nFrame;
            if (bKind == 1 && pUnit->nUnitType <= 3u && pUnit->bFemale != 0) {
                ++pIso->nAnimFrame;
            }
            if (pIso->nAnimFrame == 0) {
                continue;
            }
            pIso->nAnimFrame -= 1;
            pFrame = g_aUnitAnimFrame[pIso->nAnimFrame];

            nPhase = (pUnit->nAnimPose == 1) ? pGate->nMapPhase : pGate->nAnimPhase;
            pIso->nAnimParam = pUnit->bFacing * pFrame[2]
                             + Iso_AnimPhaseTerm(nPhase, pFrame[1]) + pFrame[0];

            if (g_aUnitAnimFrame[pIso->nAnimFrame][1] != 1) {
                nYTerm = 0;
            } else if ((pUnit->bFlags & 1) != 0) {
                nYTerm = (bKind == 2) ? 0 : 4;
            } else if ((pUnit->bFlags & 2) == 0 || (pUnit->bFlags & 0x10) != 0) {
                nYTerm = 0;
            } else {
                nYTerm = g_aUnitHeightOff[pUnit->nUnitType][pGate->nAnimPhase];
            }

            pIso->nSpriteY = nYTerm + pUnit->nDrawOffY + pIso->nDrawY;
            pIso->nSpriteX = pIso->nDrawX + pUnit->nDrawOffX;

            nGlyphIdx = Tact_MapTypeToGlyphIdx(pUnit->nUnitType, pGate->paTypeProbe,
                                               ISO_TYPE_PROBE_COUNT);
            Font_DrawGlyph(pIso->nSpriteX, pIso->nObjParam + pIso->nSpriteY,
                           (u16)(pGate->paGroupBase[nGlyphIdx] + (u16)pIso->nAnimParam), 0);
            continue;
        }

        /* Kind 4 is the weapon pass. */
        if ((pUnit->bFlags & 1) != 0) {
            pIso->nDrawAux = 4;
        } else if ((pUnit->bFlags & 2) == 0 || (pUnit->bFlags & 0x10) != 0) {
            pIso->nDrawAux = 0;
        } else {
            pIso->nDrawAux = g_aUnitHeightOff[pUnit->nUnitType][pGate->nAnimPhase];
        }
        pIso->nDrawMode = 0;

        /* aUnitFacingDrawOff covers 14 unit types, not 23. A type 14 or above reaching here reads
         * past the table into the u16 occupancy table that follows it at 0xA9488. The port bounds it
         * and leaves nDrawMode at 0 and nDrawAux unchanged instead of reproducing that read. */
        if (pIso->bTwoHandWeapon != 0 && (pUnit->bFlags & 4) != 0
            && pUnit->nUnitType < PUFO_ISO_FACING_OFF_TYPE_COUNT) {
            pIso->nDrawMode = g_aUnitFacingDrawOff[pUnit->nUnitType][pUnit->bFacing][0];
            pIso->nDrawAux += g_aUnitFacingDrawOff[pUnit->nUnitType][pUnit->bFacing][1];
        } else if (pUnit->nUnitType == 9) {
            pIso->nDrawAux += 1;
        } else if (pUnit->nUnitType == 11) {
            pIso->nDrawAux -= 1;
        }

        nPose = pUnit->nAnimPose;
        if (nPose < 5 || nPose >= 0x12) {
            continue;
        }

        {
            const ObjectRecFull *pObj = NULL;
            const TactItem *pHandR = pUnit->pHandR;
            i32 nFacingTerm;

            if ((pUnit->bFlags & 4) != 0 && *pGate->apSelWeapon != NULL
                    && (*pGate->apSelWeapon)->pObj != NULL) {
                /* 0x1D300..0x1D30C: aiming wins over both hands -- the object the selected item points
                 * at. Same deviation as above: the original loads Map+0x48 and derefs it twice with no
                 * null test, so an aiming unit with no selected weapon faults there. */
                pObj = (*pGate->apSelWeapon)->pObj;
            } else if (Iso_HandIsWeapon(pUnit->pHandL)) {
                pObj = pUnit->pHandL->pObj;
            } else if (Iso_HandIsWeapon(pHandR)) {
                pObj = pHandR->pObj;
            } else if (pUnit->pHandL != NULL) {
                pObj = pUnit->pHandL->pObj;
            } else if (pHandR != NULL) {
                pObj = pHandR->pObj;
            } else {
                /* Deviation: the original dereferences the null pHandR here. */
                continue;
            }

            pIso->nAnimParam = pObj->aItemQty[51];
            if (pIso->bTwoHandWeapon != 0 && (pUnit->bFlags & 4) != 0) {
                nFacingTerm = pUnit->bFacing + 2;
                if ((unsigned)nFacingTerm >= 8u) {
                    nFacingTerm = pUnit->bFacing - 6;
                }
                pIso->nScratch2 = nFacingTerm;
            } else {
                nFacingTerm = pUnit->bFacing;
            }
            pIso->nAnimParam += nFacingTerm;

            Font_DrawGlyph(pIso->nDrawMode + pUnit->nDrawOffX + pIso->nDrawX,
                           pUnit->nDrawOffY + g_aUnitWeaponYOff[pUnit->nUnitType]
                               + pIso->nObjParam + pIso->nDrawAux + pIso->nDrawY,
                           (u16)pIso->nAnimParam + pGate->nHandobBase - 1, 0);
        }
    }

    /* 0x1D434, the same post-increment store as the walk loop: bAnimStep ends at 5. */
    pIso->bAnimStep = (u8)nStep;
}

int MapView_DrawUnit(IsoDrawCtx *pIso, const IsoUnitDrawGate *pGate)
{
    i32 nPalSaved;
    i32 nSide;

    if (pIso == NULL || pGate == NULL || pIso->pUnit == NULL || pIso->pCell == NULL) {
        return 0;
    }


    nPalSaved = pIso->nPal;
    if (Unit_bIsOperative(pIso->pUnit)) {
        pIso->nPal = 0; /* 0x1C984 */
    }

    if (!Iso_UnitIsDrawable(pIso, pGate)) {
        /* ORIGINAL BUG (0x1CAAC): both not-drawable exits jump straight to the POP, so they never
         * write the saved palette back. An operative unit whose cell is not flagged leaves nPal at 0
         * for the caller, which reuses it for terrain. Only the paths that pass the gate restore it. */
        return 0;
    }


    Iso_DrawCursorMarker(pIso, pGate, 18);


    if (Unit_bIsLarge(pIso->pUnit)) {
        Iso_DrawLargeUnitGlyphs(pIso, pGate);
    } else {
        Iso_DrawSmallUnitGlyphs(pIso, pGate);
    }


    Iso_DrawCursorMarker(pIso, pGate, 17);

    /* The selection frame. Suppressed while the tactical queue is active, for anything that is not
     * the selected unit, and for a selected unit whose bFlags bit 1 is set. */
    if (pGate->bQueueActive == 0 && pIso->pUnit == pGate->pSelUnit
        && (pGate->pSelUnit->bFlags & 2) == 0) {
        PadImage *pFrame = pGate->pSelectFrame;

        nSide = 32 - (i32)pFrame->uWidth;
        if (pFrame->uWidth > 0x20u) {
            nSide = 33 - (i32)pFrame->uWidth;
        }
        pFrame->nOriginX = pIso->nDrawX + (nSide >> 1);

        if (Unit_bIsLarge(pGate->pSelUnit)) {
            if (pGate->pSelUnit->pCell != pIso->pCell) {
                goto restore; /* 0x1D5DC: this early exit DOES restore */
            }
            pFrame->nOriginY = g_aBobPhaseY[pGate->nMapPhase] - (i32)pFrame->uHeight
                             + pIso->nObjParam + pIso->nDrawY + 8;
        } else {
            pFrame->nOriginY = g_aBobPhaseY[pGate->nMapPhase] - (i32)pFrame->uHeight
                             + pIso->nObjParam + pIso->nDrawY;
        }
        PadImage_BlitEx(pFrame, 0xFF);
    }

restore:
    pIso->nPal = nPalSaved;
    return 0;
}
