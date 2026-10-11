/* pufo_mapcell.c - the tactical map cell and its tile/occupant state.
 *
 * Evidence: see pufo_mapcell.h for the full address list.
 *
 * The flag masking in Init/Reset/SetTileSprites looks arbitrary because it is:
 * each function preserves some bits while clearing others, and the preserved
 * sets differ. Every mask below is copied literally from the disassembly rather
 * than tidied, because "cleaning them up" is exactly how parity is lost.
 */
#include "game/pufo_mapcell.h"

#include <string.h>

MapCell *MapCell_Init(MapCell *pCell, i32 nX, i32 nY, i32 nZ)
{
    pCell->bUnk16 = 0;
    pCell->bUnk17 = 0;
    pCell->bUnk18 = 0;
    pCell->bUnk19 = 0;
    pCell->nZ     = (u8)nZ;

    pCell->pFloorSpr = NULL;
    pCell->pWallSprA = NULL;
    pCell->bFlags29  = (u8)(pCell->bFlags29 & 0x88);
    pCell->pWallSprB = NULL;
    pCell->pCeilSpr  = NULL;
    pCell->bFlags36  = (u8)(pCell->bFlags36 & 0xFE);

    pCell->nX       = (u8)nX;
    pCell->bUnk24   = 0;
    pCell->nFlags40 = pCell->nFlags40 & 0xFFFFFF80u;
    pCell->nUnk20   = 0;
    pCell->nY       = (u8)nY;
    pCell->nFlags32 = pCell->nFlags32 & 0xFFFFFF80u;
    pCell->bUnk56   = 0;
    /* The original loads bFlags44 through a dword temporary and keeps only the
     * low byte; same result as a byte mask. */
    pCell->bFlags44 = (u8)(pCell->bFlags44 & 0xFE);
    return pCell;
}

MapCell *MapCell_Reset(MapCell *pCell)
{
    pCell->bUnk24    = 0;
    pCell->nUnk20    = 0;
    pCell->bFlags44  = (u8)(pCell->bFlags44 & 0xFE);
    pCell->pItemList = NULL;
    pCell->pUnit     = NULL;
    pCell->bFlags57[0] = (u8)(pCell->bFlags57[0] & 0xF3);
    pCell->bUnk56    = 0;
    pCell->bFlags57[1] = 0;
    pCell->bFlags29  = (u8)(pCell->bFlags29 & 0xAF);
    pCell->bUnk16    = 0;
    pCell->bUnk17    = 0;
    pCell->bUnk18    = 0;
    pCell->bUnk19    = 0;
    pCell->nFlags32  = pCell->nFlags32 & 0xFFFFFFF8u;
    pCell->bFlags36  = (u8)(pCell->bFlags36 & ~1u);
    pCell->nFlags40  = pCell->nFlags40 & 0xFFFFFFF8u;
    pCell->bFlags29  = (u8)(pCell->bFlags29 & ~0x20u);
    return pCell;
}

MapCell *MapCell_SetTileSprites(MapCell *pCell, Mc1Rec *pFloor, Mc1Rec *pWallA,
                                Mc1Rec *pWallB, Mc1Rec *pCeil)
{
    if (pFloor != NULL) {
        pCell->pFloorSpr = pFloor;
    }
    if (pWallA != NULL) {
        pCell->pWallSprA = pWallA;
    }
    if (pWallB != NULL) {
        pCell->pWallSprB = pWallB;
    }
    pCell->nFlags32 = pCell->nFlags32 & 0xFFFFFF80u;
    if (pCeil != NULL) {
        pCell->pCeilSpr = pCeil;
    }
    pCell->pUnit    = NULL;
    pCell->bFlags36 = (u8)(pCell->bFlags36 & 0xFE);
    pCell->bUnk24   = 0;
    pCell->nUnk20   = 0;
    pCell->nFlags40 = pCell->nFlags40 & 0xFFFFFF80u;
    pCell->bFlags29 = (u8)(pCell->bFlags29 & ~0x20u);
    pCell->bFlags44 = (u8)(pCell->bFlags44 & ~1u);
    return pCell;
}

/* MapCell_TestFlag @ 0x549c8. The selector meanings were inferred from the
 * caller reaches; the comparisons themselves are literal.
 *
 * bTransparent is compared to 255, and the blocking bytes are tested for
 * non-zero. Note that "blocks" flags return true when the byte != 0, i.e. the
 * byte is a yes/no marker not a threshold. */
bool MapCell_TestFlag(const MapCell *pCell, CellFlag nWhich)
{
    const Mc1Rec *pRec;

    switch (nWhich) {
    case CELLFLAG_IS_BLANK:
        if (pCell->pCeilSpr != NULL && pCell->pCeilSpr->bTransparent == 255) {
            return true;
        }
        return pCell->pFloorSpr != NULL && pCell->pFloorSpr->bTransparent == 255;

    case CELLFLAG_WALL_B_BLANK:
        pRec = pCell->pWallSprB;
        return pRec != NULL && pRec->bTransparent == 255;

    case CELLFLAG_WALL_A_BLANK:
        pRec = pCell->pWallSprA;
        return pRec != NULL && pRec->bTransparent == 255;

    case CELLFLAG_WALL_A_BLOCKS:
        pRec = pCell->pWallSprA;
        if (pRec == NULL) {
            return false;
        }
        return pRec->bBlocksMove != 0;

    case CELLFLAG_WALL_B_BLOCKS:
        pRec = pCell->pWallSprB;
        if (pRec == NULL) {
            return false;
        }
        return pRec->bBlocksMove != 0;

    case CELLFLAG_HAS_FLOOR:
        return pCell->pFloorSpr != NULL;

    case CELLFLAG_FLOOR_BLOCKS:
        pRec = pCell->pFloorSpr;
        if (pRec == NULL) {
            return false;
        }
        return pRec->bBlocksSight != 0;

    default:
        return false;
    }
}

MapCell *MapCell_SetOccupant(MapCell *pCell, Soldier *pUnit)
{
    pCell->pUnit = pUnit;
    return pCell;
}

/* MapCell_HasOccupant @ 0x5487c. A unit with bStatus >= 3 is not counted, which
 * is how dead/removed units stop occupying a cell without being unlinked.
 *
 * Disassembly proof of the offset:
 *     LDR   R0, [R0,#0x30]   ; pCell->pUnit
 *     CMP   R0, #0 / BEQ     ; NULL check
 *     LDRB  R3, [R0,#0x17]   ; pUnit->bStatus  <- 0x17, not 0
 *     CMP   R3, #3 / BCC
 * The status byte sits at 0x17 in Soldier, so it is read through that offset
 * rather than assuming the first byte. */
bool MapCell_HasOccupant(const MapCell *pCell)
{
    const u8 *pBytes = (const u8 *)pCell->pUnit;

    if (pCell->pUnit == NULL) {
        return false;
    }
    return pBytes[PUFO_SOLDIER_BSTATUS_OFF] < 3u;
}

MapCell *MapCell_SetBFlags57_3(MapCell *pCell, bool bSet)
{
    pCell->bFlags57[0] = (u8)((pCell->bFlags57[0] & 0xF7) | (8 * (bSet ? 1 : 0)));
    return pCell;
}

MapCell *MapCell_SetBFlags29_4(MapCell *pCell)
{
    pCell->bFlags29 = (u8)(pCell->bFlags29 | 0x10u);
    return pCell;
}

/* MapCell_ListInsertByPri @ 0x54b14.
 *
 * The comparison inverts the usual sense: when the head's priority is >= the new
 * node's, the new node is spliced AFTER the head; otherwise it becomes the new
 * head. So the list ends up ordered by descending priority with the lowest
 * priority first. Reproduced exactly, including the fact that the successor of
 * the head is overwritten without walking the list -- this is a two-position
 * insert, not a sorted insert. */
MapCell *MapCell_ListInsertByPri(MapCell *pCell, TactItem *pNode)
{
    TactItem *pHead = pCell->pItemList;

    if (pHead != NULL && pHead->pObj->bListPri >= pNode->pObj->bListPri) {
        pNode->pNext = pHead->pNext;
        pHead->pNext = pNode;
    } else {
        pNode->pNext     = pHead;
        pCell->pItemList = pNode;
    }
    return pCell;
}

/* MapCell_ListRemove @ 0x54b58. The original walks with a trailing pointer and
 * only relinks when the successor is non-NULL, so removing the tail leaves the
 * previous node's link dangling rather than clearing it. Reproduced as-is. */
MapCell *MapCell_ListRemove(MapCell *pCell, TactItem *pNode)
{
    TactItem *pPrev = pCell->pItemList;

    if (pPrev == NULL) {
        return pCell;
    }
    if (pPrev == pNode) {
        pCell->pItemList = pNode->pNext;
        return pCell;
    }

    while (pPrev->pNext != pNode && pPrev->pNext != NULL) {
        pPrev = pPrev->pNext;
    }
    if (pPrev->pNext != NULL) {
        pPrev->pNext = pNode->pNext;
    }
    return pCell;
}

bool Map_IsValidCell(const MapGeom *pMap, i32 nX, i32 nY, i32 nLayer)
{
    if (nX < 0) {
        return false;
    }
    if (nY < 0) {
        return false;
    }
    if (nX >= PUFO_CELL_SIZE * (i32)pMap->nDimX) {
        return false;
    }
    if (nY >= PUFO_CELL_SIZE * (i32)pMap->nDimY) {
        return false;
    }
    if (nLayer < 0) {
        return false;
    }
    return nLayer <= PUFO_MAP_LAYERS - 1;
}

/* The original indexes its grid as [layer][y][x] with the dimensions coming from
 * the context; see Map_ApplyTemplateToCells @ 0x54cf4, which strides by 2400
 * ((10*dimY) cells) per layer. */
size_t Map_CellIndex(i32 nDimX, i32 nDimY, i32 nX, i32 nY, i32 nLayer)
{
    size_t nRowCells = (size_t)(PUFO_CELL_SIZE * nDimX);

    return ((size_t)nLayer * (size_t)(PUFO_CELL_SIZE * nDimY) * nRowCells)
         + ((size_t)nY * nRowCells)
         + (size_t)nX;
}

i32 Map_IsoOffsetA(i32 nDimX, i32 nA, i32 nB)
{
    return nA - 5 * nDimX + nB;
}

i32 Map_IsoOffsetB(i32 nDimY, i32 nA, i32 nB)
{
    return 5 * nDimY - nA + nB - 1;
}

i32 Map_CenterOffsetX(i32 nDimX, i32 nA, i32 nB)
{
    return 5 * nDimX - (nA + nB + 1) / 2 + nA;
}

i32 Map_CenterOffsetY(i32 nA, i32 nB)
{
    return (nA + nB + 1) / 2;
}

/* --- Map_RefreshCellMeta @ 0x2c7f4 --- */

/* The per-source-cell occlusion height, from the decompilation:
 *     h = 0
 *     if (pFloorSpr && pFloorSpr[0x1C]) h = pFloorSpr[0x1C]
 *     h = MAX(h, pWallSprA[0x1C]) ; MAX(h, pWallSprB[0x1C]) ; MAX(h, pCeilSpr[0x1C])
 *     if ((nFlags40 & 0x78) != 0) h = 1
 * Sprite byte 0x1C is Mc1Rec.bHeight. A sprite byte of 0 means "no contribution",
 * which is why the floor is tested for non-zero before being taken. */
static u8 MapCell_OcclusionHeight(const MapCell *pCell)
{
    u8 h = 0;

    if (pCell->pFloorSpr != NULL && pCell->pFloorSpr->bHeight != 0) {
        h = pCell->pFloorSpr->bHeight;
    }
    if (pCell->pWallSprA != NULL && pCell->pWallSprA->bHeight > h) {
        h = pCell->pWallSprA->bHeight;
    }
    if (pCell->pWallSprB != NULL && pCell->pWallSprB->bHeight > h) {
        h = pCell->pWallSprB->bHeight;
    }
    if (pCell->pCeilSpr != NULL && pCell->pCeilSpr->bHeight > h) {
        h = pCell->pCeilSpr->bHeight;
    }
    /* The animated-tile flag field forces the minimum contribution. */
    if ((pCell->nFlags40 & 0x78u) != 0) {
        h = 1;
    }
    return h;
}

bool Map_RefreshCellMeta(MapCellLookup pfnLookup, void *pUser,
                         const MapGeom *pMap, bool bLightEnabled,
                         const u8 *pLightmap)
{
    i32 nSrcX;
    i32 nSrcY;
    i32 nDimX;
    i32 nDimY;

    if (!bLightEnabled || pfnLookup == NULL || pMap == NULL || pLightmap == NULL) {
        return false;
    }
    nDimX = PUFO_CELL_SIZE * (i32)pMap->nDimX;
    nDimY = PUFO_CELL_SIZE * (i32)pMap->nDimY;

    /* Phase 1: clear the occlusion height on every cell of every layer. */
    for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (i32 y = 0; y < nDimY; ++y) {
            for (i32 x = 0; x < nDimX; ++x) {
                MapCell *pCell = pfnLookup(x, y, z, pUser);

                if (pCell != NULL) {
                    pCell->bUnk56 = 0xFFu;
                }
            }
        }
    }

    /* Phase 2: spread each source cell's height over the lightmap footprint. */
    for (nSrcY = 0; nSrcY < nDimY; ++nSrcY) {
        for (nSrcX = 0; nSrcX < nDimX; ++nSrcX) {
            MapCell *pSrc = pfnLookup(nSrcX, nSrcY, 0, pUser);
            u8       h;

            if (pSrc == NULL) {
                continue;
            }
            h = MapCell_OcclusionHeight(pSrc);

            /* Spread only when the cell casts something, or when an OPERATIVE
             * unit stands on it (bStatus == 0). Read through the proven status
             * offset, since Soldier is only forward-declared here. */
            if (h == 0
                && !(MapCell_HasOccupant(pSrc)
                     && ((const u8 *)pSrc->pUnit)[PUFO_SOLDIER_BSTATUS_OFF] == 0)) {
                continue;
            }
            /* An occupant suppresses the sprite contribution: the shadow is cast
             * by the unit, and the unit's own footprint is handled below. */
            if (MapCell_HasOccupant(pSrc)) {
                h = 0;
            }

            /* The footprint is 23x23 CENTRED on the source cell: both dx and dy
             * run -11..11.
             *
             * That is not obvious from the decompilation, which computes the
             * destination as `base + 4*v18 + 4*v20 + 4*v27` with
             * `v18 = 60*(v26 + v17)`. Reading v18 as a row offset gives
             * `y = v26 + v17`, and v17 runs -11..11, so the row offset is signed
             * exactly like the column offset v20. An earlier version of this code
             * used `dy = 0..22`, which shifted the whole footprint down by 11 rows;
             * the tests caught it. The lightmap row index is therefore dy + 11. */
            for (i32 dy = -11; dy <= 11; ++dy) {
                for (i32 dx = -11; dx <= 11; ++dx) {
                    u32 nLight;

                    if (!Map_IsValidCell(pMap, nSrcX + dx, nSrcY + dy, 0)) {
                        continue;
                    }
                    nLight = (u32)h
                           + (u32)pLightmap[PUFO_LIGHTMAP_DIM * (dy + 11) + (dx + 11)];
                    if (nLight > 0xFFu) {
                        nLight = 0xFFu;
                    }
                    /* The same value lands on all four layers. */
                    for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
                        MapCell *pDst = pfnLookup(nSrcX + dx, nSrcY + dy, z, pUser);

                        if (pDst != NULL && nLight < (u32)pDst->bUnk56) {
                            pDst->bUnk56 = (u8)nLight;
                        }
                    }
                }
            }
        }
    }
    return true;
}

/* TactItem_ctor @ 0x3e718. Only four fields are cleared; the rest are the caller's. */
void TactItem_ctor(TactItem *pItem)
{
    if (pItem == NULL) {
        return;
    }
    pItem->pNext  = NULL;
    pItem->bUnk8  = 0;
    pItem->bUnk7  = 0;
    pItem->pLinkedSoldier = NULL;
}

/* --- shot-geometry helpers --- */

i32 Map_CeilingLift(const MapCell *pCell)
{
    return (pCell->pCeilSpr != NULL)
               ? ((-12 * (i32)pCell->pCeilSpr->iCeilParam) / 24)
               : 0;
}

i32 Map_CellCentreX(const MapCell *pCell) { return 16 * (i32)pCell->nX + 8; }
i32 Map_CellCentreY(const MapCell *pCell) { return 16 * (i32)pCell->nY + 8; }

i32 Map_CellCentreZ(const MapCell *pCell)
{
    return 12 * (i32)pCell->nZ + Map_CeilingLift(pCell) + 6;
}

void Map_ClearShotTraceFlags(MapCellLookup pfnLookup, void *pUser,
                             const MapGeom *pMap)
{
    const i32 nRows = 10 * (i32)pMap->nDimY;
    const i32 nCols = 10 * (i32)pMap->nDimX;

    for (i32 nLayer = 0; nLayer < 4; ++nLayer) {
        for (i32 nRow = 0; nRow < nRows; ++nRow) {
            for (i32 nCol = 0; nCol < nCols; ++nCol) {
                MapCell *pCell = pfnLookup(nCol, nRow, nLayer, pUser);
                if (pCell != NULL) {
                    pCell->bFlags29 &= (u8)~0x20;
                }
            }
        }
    }
}
