/* pufo_invdrag.c - the inventory screen hit test. See pufo_invdrag.h. */
#include "game/pufo_invdrag.h"

/* aInvHitRects @ 0xA8DD8, read out of the executable (25 records of 14 bytes).
 * Columns: x0, y0, x1, y1, region, slot, col.
 *
 * The layout is the inventory screen: pockets 0 and 1 along the top row (y 22..41),
 * pockets 2 and 3 in the row at y 160..179, the left hand at x 3..42 and the right
 * hand at x 122..161 in the band y 61..119, the backpack as a 4 x 2 grid at
 * x 160..236 / y 160..198, and the belt as a 3 x 3 grid at x 170..227 / y 22..79.
 * Only six of the eight backpack cells have a rect: the second row has slots 0 and
 * 3 only, so backpack slots 1 and 2 of that row are unreachable by pointer. */
static const InvHitRect s_aHitRect[PUFO_INV_HITRECT_COUNT] = {
    {  22,  22,  41,  41, 0, 0, 0 },
    {  41,  22,  61,  41, 0, 1, 0 },
    { 103,  22, 122,  41, 1, 0, 0 },
    { 122,  22, 141,  41, 1, 1, 0 },
    {  21, 160,  40, 179, 2, 0, 0 },
    {  40, 160,  60, 179, 2, 1, 0 },
    { 103, 160, 122, 179, 3, 0, 0 },
    { 122, 160, 141, 179, 3, 1, 0 },
    {   3,  61,  42, 119, 4, 0, 0 },
    { 122,  61, 161, 119, 5, 0, 0 },
    { 160, 160, 179, 179, 6, 0, 0 },
    { 179, 160, 198, 179, 6, 1, 0 },
    { 198, 160, 217, 179, 6, 2, 0 },
    { 217, 160, 236, 179, 6, 3, 0 },
    { 160, 179, 179, 198, 6, 0, 1 },
    { 217, 179, 236, 198, 6, 3, 1 },
    { 170,  22, 189,  41, 7, 0, 0 },
    { 189,  22, 208,  41, 7, 1, 0 },
    { 208,  22, 227,  41, 7, 2, 0 },
    { 170,  41, 189,  60, 7, 0, 1 },
    { 189,  41, 208,  60, 7, 1, 1 },
    { 208,  41, 227,  60, 7, 2, 1 },
    { 170,  60, 189,  79, 7, 0, 2 },
    { 189,  60, 208,  79, 7, 1, 2 },
    { 208,  60, 227,  79, 7, 2, 2 }
};

/* -20 * foot / 2. The ARM halves with MOVS/ADDMI/ASR#1, which on an even product
 * is the plain division. */
static void Hit_DragOffsets(InvHitState *pHit, const TactItem *pItem)
{
    u8 nFoot;

    nFoot = (pItem->pObj != NULL) ? pItem->pObj->nFootW : 0;
    pHit->nDragOffX = PUFO_INV_DRAG_SCALE * (i32)nFoot / 2;
    nFoot = (pItem->pObj != NULL) ? pItem->pObj->nFootH : 0;
    pHit->nDragOffY = PUFO_INV_DRAG_SCALE * (i32)nFoot / 2;
}

bool Ui_HitTestTactItem(const InvHitTestCtx *pCtx, i32 nX, i32 nY)
{
    const InvHitRect *pRect;
    TactItem *pItem;
    i32 i;

    if (pCtx == NULL || pCtx->pHit == NULL || pCtx->pUi == NULL) {
        return false;
    }
    pItem = NULL;
    pCtx->pHit->pHitItem = NULL;   /* 0x11AD4 */

    /* 0x11AF0..0x11B68: the ground list window. */
    if (nX >= PUFO_INV_GROUND_X0 && nX <= PUFO_INV_GROUND_X1 &&
        nY >= PUFO_INV_GROUND_Y0 && nY <= PUFO_INV_GROUND_Y1) {
        i32 nCol = (nX - PUFO_INV_GROUND_X0) / PUFO_INV_GROUND_COL_W;
        i32 nIdx = (i32)pCtx->pUi->nInvFilterType + nCol;

        if (nIdx < (i32)pCtx->pUi->nInvListCount) {
            pItem = pCtx->pUi->apInvList[nIdx];
            pCtx->pHit->pHitItem = pItem;
            pCtx->pHit->nHitSub0 = nCol;
            Hit_DragOffsets(pCtx->pHit, pItem);
            pCtx->pHit->nHitRegion = INV_REGION_GROUND;
        }
    }

    /* 0x11BC8..0x11C1C: the table scan, first match wins. */
    i = 0;
    while (i < PUFO_INV_HITRECT_COUNT) {
        if (nX >= s_aHitRect[i].x0 && nX <= s_aHitRect[i].x1 &&
            nY >= s_aHitRect[i].y0 && nY <= s_aHitRect[i].y1) {
            break;
        }
        ++i;
    }
    if (i >= PUFO_INV_HITRECT_COUNT) {
        return pItem != NULL;      /* 0x11F2C: LABEL_47 */
    }
    pRect = &s_aHitRect[i];

    switch (pRect->nRegion) {
        case 0: case 1: case 2: case 3: {
            const InvPocketPair *pPk;
            i32 nMeta;

            if (pCtx->pSelUnit == NULL || pRect->nSlot < 0 || pRect->nSlot > 1) {
                return pItem != NULL;
            }
            pPk = &pCtx->pSelUnit->aPockets[pRect->nRegion];
            pItem = pPk->apItem[pRect->nSlot];                 /* 0x11C80 */
            nMeta = pPk->abMeta[pRect->nSlot];                 /* 0x11C90 */
            pCtx->pHit->pHitItem = pItem;
            if (pItem == pCtx->pSentinel) {                    /* 0x11C98 */
                pItem = (nMeta >= 0 && nMeta <= 1)
                            ? pPk->apItem[nMeta] : NULL;
                pCtx->pHit->pHitItem = pItem;
            }
            if (pItem == NULL) {
                return false;                                  /* 0x11CA8 */
            }
            pCtx->pHit->nHitRegion = pRect->nRegion + 1;       /* 0x11D8C */
            pCtx->pHit->nHitSub0 = nMeta;                      /* 0x11DA0 */
            break;
        }
        case 4:
            if (pCtx->pSelUnit == NULL) {
                return pItem != NULL;
            }
            pItem = pCtx->pSelUnit->pHandL;                    /* 0x11DB4 */
            pCtx->pHit->pHitItem = pItem;
            if (pItem == NULL) {
                return false;
            }
            pCtx->pHit->nHitRegion = INV_REGION_HAND_L;        /* 0x11DEC */
            break;
        case 5:
            if (pCtx->pSelUnit == NULL) {
                return pItem != NULL;
            }
            pItem = pCtx->pSelUnit->pHandR;                    /* 0x11DD8 */
            pCtx->pHit->pHitItem = pItem;
            if (pItem == NULL) {
                return false;
            }
            pCtx->pHit->nHitRegion = INV_REGION_HAND_R;
            break;
        case 6: {
            i32 nIdx = pRect->nSlot + 4 * pRect->nCol;         /* 0x11E10 */
            i32 nSub0, nSub1;

            if (pCtx->pSelUnit == NULL || nIdx < 0 || nIdx >= 8) {
                return pItem != NULL;
            }
            pItem = pCtx->pSelUnit->apBackpack[nIdx];          /* 0x11E18 */
            nSub0 = pCtx->pSelUnit->abBackpackMeta[nIdx];      /* Soldier+0x15C */
            nSub1 = pCtx->pSelUnit->abBackpackMeta[8 + nIdx];  /* Soldier+0x164 */
            pCtx->pHit->pHitItem = pItem;
            if (pItem == pCtx->pSentinel) {                    /* 0x11E34 */
                i32 nMirror = nSub0 + 4 * nSub1;
                pItem = (nMirror >= 0 && nMirror < 8)
                            ? pCtx->pSelUnit->apBackpack[nMirror] : NULL;
                pCtx->pHit->pHitItem = pItem;
            }
            if (pItem == NULL) {
                return false;
            }
            pCtx->pHit->nHitRegion = INV_REGION_BACKPACK;      /* 0x11EC0 */
            pCtx->pHit->nHitSub0 = nSub0;
            pCtx->pHit->nHitSub1 = nSub1;
            break;
        }
        case 7: {
            i32 nIdx = pRect->nSlot + 3 * pRect->nCol;         /* 0x11E5C..0x11E60 */
            i32 nSub0, nSub1;

            if (pCtx->pSelUnit == NULL || nIdx < 0 || nIdx >= 9) {
                return pItem != NULL;
            }
            pItem = pCtx->pSelUnit->apBelt[nIdx];              /* 0x11E78 */
            nSub0 = pCtx->pSelUnit->abBeltMetaA[nIdx];         /* Soldier+0x190 */
            nSub1 = pCtx->pSelUnit->abBeltMetaB[nIdx];         /* Soldier+0x199 */
            pCtx->pHit->pHitItem = pItem;
            if (pItem == pCtx->pSentinel) {                    /* 0x11E9C */
                i32 nMirror = nSub0 + 3 * nSub1;
                pItem = (nMirror >= 0 && nMirror < 9)
                            ? pCtx->pSelUnit->apBelt[nMirror] : NULL;
                pCtx->pHit->pHitItem = pItem;
            }
            if (pItem == NULL) {
                return false;
            }
            pCtx->pHit->nHitRegion = INV_REGION_BELT;
            pCtx->pHit->nHitSub0 = nSub0;
            pCtx->pHit->nHitSub1 = nSub1;
            break;
        }
        default:
            /* Not reachable with the real table, whose region column is 0..7. The
             * original would fall through to the drag write with a stale pointer. */
            return pItem != NULL;
    }

    /* 0x11EF4 / 0x11F28: the drag offsets follow the item that actually won. */
    Hit_DragOffsets(pCtx->pHit, pItem);
    return pItem != NULL;
}

/* The footprint width guard: the original tests `pObj->nFootW != 0` before each
 * row (0x12174 / 0x12200, `LDRB [R3,#0xA6]`), so a zero-width item clears nothing. */
static i32 Clear_FootW(const TactItem *pItem)
{
    if (pItem == NULL || pItem->pObj == NULL) {
        return 0;
    }
    return (pItem->pObj->nFootW != 0) ? (i32)pItem->pObj->nFootW : 0;
}

void Inv_ClearHitSlot(const InvHitTestCtx *pCtx)
{
    TactItem *pItem;
    Soldier *pUnit;
    i32 nFootW;
    i32 nFootH;
    i32 nRow;

    if (pCtx == NULL || pCtx->pHit == NULL || pCtx->pSelUnit == NULL) {
        return;
    }
    pUnit = pCtx->pSelUnit;
    pItem = pCtx->pHit->pHitItem;

    switch (pCtx->pHit->nHitRegion) {
        case INV_REGION_GROUND:
            /* 0x11FB4: the item leaves the cell it lies on, then the visible list
             * is rebuilt so the window shows what is left. */
            if (pUnit->pCell != NULL && pItem != NULL) {
                MapCell_ListRemove(pUnit->pCell, pItem);
            }
            Inv_BuildVisibleList(pCtx->pUi, pUnit);
            return;
        case INV_REGION_POCKET0:
        case INV_REGION_POCKET1:
        case INV_REGION_POCKET2:
        case INV_REGION_POCKET3: {
            InvPocketPair *pPk = &pUnit->aPockets[pCtx->pHit->nHitRegion - 1];
            i32 nStart = pCtx->pHit->nHitSub0;

            /* 0x11FE0: dword index `sub0 + 67/70/73/76`, i.e. the pocket record
             * with its 12-byte stride. */
            nFootW = Clear_FootW(pItem);
            for (i32 i = 0; i < nFootW; ++i) {
                if (nStart + i >= 0 && nStart + i < 2) {
                    pPk->apItem[nStart + i] = NULL;
                }
            }
            return;
        }
        case INV_REGION_HAND_L:
            pUnit->pHandL = NULL;   /* 0x12124 */
            return;
        case INV_REGION_HAND_R:
            pUnit->pHandR = NULL;   /* 0x1213C */
            return;
        case INV_REGION_BACKPACK:
            if (pItem == NULL || pItem->pObj == NULL) {
                return;
            }
            nFootW = Clear_FootW(pItem);
            nFootH = (i32)pItem->pObj->nFootH;
            for (nRow = 0; nRow < nFootH; ++nRow) {
                /* 0x12164: index `sub0 + 4*sub1 + 79`, row stride 4 slots. */
                i32 nStart = pCtx->pHit->nHitSub0 +
                             4 * pCtx->pHit->nHitSub1 + 4 * nRow;
                for (i32 i = 0; i < nFootW; ++i) {
                    if (nStart + i >= 0 && nStart + i < 8) {
                        pUnit->apBackpack[nStart + i] = NULL;
                    }
                }
            }
            return;
        case INV_REGION_BELT:
            if (pItem == NULL || pItem->pObj == NULL) {
                return;
            }
            nFootW = Clear_FootW(pItem);
            nFootH = (i32)pItem->pObj->nFootH;
            for (nRow = 0; nRow < nFootH; ++nRow) {
                /* 0x121F0: index `sub0 + 3*sub1 + 91`, row stride 3 slots. */
                i32 nStart = pCtx->pHit->nHitSub0 +
                             3 * pCtx->pHit->nHitSub1 + 3 * nRow;
                for (i32 i = 0; i < nFootW; ++i) {
                    if (nStart + i >= 0 && nStart + i < 9) {
                        pUnit->apBelt[nStart + i] = NULL;
                    }
                }
            }
            return;
        default:
            /* 0x12250: an unknown region does nothing. */
            return;
    }
}
