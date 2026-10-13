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

/* --------------------------------------------------------------------------
 * Inv_HandlePointer @ 0x12260 - the drop path. See the header for the contract.
 * -------------------------------------------------------------------------- */

/* aOk @ 0xA62D8, the label every modal here passes. */
static const char s_szOk[] = "OK";

static i32 Drop_Foot(const ObjectRec *pObj, bool bWidth)
{
    if (pObj == NULL) {
        return 0;
    }
    return bWidth ? (i32)pObj->nFootW : (i32)pObj->nFootH;
}

/* The three modal shapes: 160,31 not enough time; 160,45 the weapon already holds
 * a clip; 200,43 a priority weapon already fills the other hand. */
static void Drop_Modal(const InvDropCtx *pCtx, i32 nX, i32 nY, i32 nCount)
{
    if (pCtx->pHooks->pfnModal != NULL) {
        pCtx->pHooks->pfnModal(nX, nY, nCount, s_szOk, pCtx->pHooks->pUser);
    }
}

/* The charge rule at 0x123C8 and its seven copies: free when the action latch is
 * set, or when the item goes back into the region it came from. */
static bool Drop_Charge(const InvDropCtx *pCtx, i32 nCost, i32 nOwnRegion)
{
    Soldier *pUnit = pCtx->pSelUnit;

    if (pCtx->pUi->bActionLatch != 0 || pCtx->pHit->nHitRegion == nOwnRegion) {
        return true;
    }
    if ((i32)pUnit->uTu < nCost) {
        Drop_Modal(pCtx, 160, 31, 1);
        return false;
    }
    pUnit->uTu = (u16)(pUnit->uTu - nCost);
    return true;
}

/* The clip load has no region exemption (0x12970, 0x12C94): the latch alone skips
 * the charge. */
static bool Drop_ChargeClip(const InvDropCtx *pCtx, i32 nCost)
{
    Soldier *pUnit = pCtx->pSelUnit;

    if (pCtx->pUi->bActionLatch != 0) {
        return true;
    }
    if ((i32)pUnit->uTu < nCost) {
        Drop_Modal(pCtx, 160, 31, 1);
        return false;
    }
    pUnit->uTu = (u16)(pUnit->uTu - nCost);
    return true;
}

/* 0x12AC4 and 0x12E2C, the same expression in both hands: a priority weapon blocks
 * the drop unless the incoming item is category 3, and a priority incoming item
 * blocks it unless the held one is category 3. */
static bool Drop_HandBlocked(const ObjectRec *pHeld, const ObjectRec *pIncoming)
{
    if (pHeld == NULL || pIncoming == NULL) {
        return false;
    }
    return (pHeld->bListPri == 1 && pIncoming->nCategory != 3) ||
           (pIncoming->bListPri == 1 && pHeld->nCategory != 3);
}

/* 0x128C8: the dragged item must be one of the three ammo types the held weapon
 * accepts, matched through apObjects on the anAmmoTypeId @ 0xA8 triple. */
static bool Drop_WeaponTakes(const InvDropCtx *pCtx, const TactItem *pWeapon,
                             const ObjectRec *pObj)
{
    const ObjectRec *pW = (pWeapon != NULL) ? pWeapon->pObj : NULL;

    if (pW == NULL || pObj == NULL || pCtx->pHooks->pfnFindObject == NULL) {
        return false;
    }
    for (i32 k = 0; k < 3; ++k) {
        if (pCtx->pHooks->pfnFindObject(pW->anAmmoTypeId[k], pCtx->pHooks->pUser) == pObj) {
            return true;
        }
    }
    return false;
}

/* 0x12A04..0x12A58: the weapon's current ammo index follows the clip it now holds.
 * The third type is only tested when the first two miss, and a miss leaves the
 * index untouched. */
static void Drop_SetAmmoIndex(const InvDropCtx *pCtx, TactItem *pWeapon,
                              const ObjectRec *pObj)
{
    const ObjectRec *pW = pWeapon->pObj;

    if (pW == NULL || pObj == NULL || pCtx->pHooks->pfnFindObject == NULL) {
        return;
    }
    for (i32 k = 0; k < 2; ++k) {
        if (pCtx->pHooks->pfnFindObject(pW->anAmmoTypeId[k], pCtx->pHooks->pUser) == pObj) {
            pWeapon->nIndex = pW->anAmmoTypeId[k];
            return;
        }
    }
    if (pCtx->pHooks->pfnFindObject(pW->anAmmoTypeId[2], pCtx->pHooks->pUser) == pObj) {
        pWeapon->nIndex = pW->anAmmoTypeId[2];
    }
}

/* 0x12A5C..0x12A94 then 0x12DB4 / 0x12DCC: close the pool hole with the LAST entry
 * (ARM reads index count-1 through the alias constant 0x23434), delete the item and
 * decrement. The search only runs when the count is above 1, and the decrement is
 * unconditional, so an item that was never in the pool still costs one entry. */
static void Drop_PoolTakeOut(const InvDropCtx *pCtx, TactItem *pItem)
{
    const InvDropHooks *pHooks = pCtx->pHooks;
    i32 n = *pHooks->pnItemCount;

    if (n > 1) {
        for (i32 i = 0; i < n; ++i) {
            if (pHooks->apItemPool[i] == pItem) {
                pHooks->apItemPool[i] = pHooks->apItemPool[n - 1];
                break;
            }
        }
    }
    if (pHooks->pfnFreeItem != NULL) {
        pHooks->pfnFreeItem(pItem, pHooks->pUser);
    }
    *pHooks->pnItemCount = n - 1;
}

/* 0x12438..0x124A4 and LABEL_161 @ 0x1330C: the covered slots take the sentinel and
 * a meta byte naming the anchor, then the anchor slot takes the item itself. */
static void Drop_MarkPocket(const InvDropCtx *pCtx, i32 nPocket, i32 nSub0, i32 nFootW)
{
    InvPocketPair *pPk = &pCtx->pSelUnit->aPockets[nPocket];

    for (i32 i = 0; i < nFootW; ++i) {
        i32 n = nSub0 + i;
        if (n < 0 || n >= 2) {
            continue;
        }
        pPk->apItem[n] = pCtx->pSentinel;
        pPk->abMeta[n] = (u8)nSub0;
    }
    if (nSub0 >= 0 && nSub0 < 2) {
        pPk->apItem[nSub0] = pCtx->pHit->pHitItem;
    }
}

static void Drop_MarkBackpack(const InvDropCtx *pCtx, i32 nSub0, i32 nSub1,
                              i32 nFootW, i32 nFootH)
{
    Soldier *pUnit = pCtx->pSelUnit;

    for (i32 nRow = 0; nRow < nFootH; ++nRow) {
        for (i32 i = 0; i < nFootW; ++i) {
            /* 0x13044: index sub0 + 4*sub1 + 4*row + col, meta A then meta B at +8. */
            i32 n = nSub0 + 4 * nSub1 + 4 * nRow + i;
            if (n < 0 || n >= 8) {
                continue;
            }
            pUnit->apBackpack[n] = pCtx->pSentinel;
            pUnit->abBackpackMeta[n] = (u8)nSub0;
            pUnit->abBackpackMeta[n + 8] = (u8)nSub1;
        }
    }
    /* LABEL_161 with index sub0 + 4*sub1 + 79. */
    if (nSub0 + 4 * nSub1 >= 0 && nSub0 + 4 * nSub1 < 8) {
        pUnit->apBackpack[nSub0 + 4 * nSub1] = pCtx->pHit->pHitItem;
    }
}

static void Drop_MarkBelt(const InvDropCtx *pCtx, i32 nSub0, i32 nSub1,
                          i32 nFootW, i32 nFootH)
{
    Soldier *pUnit = pCtx->pSelUnit;

    for (i32 nRow = 0; nRow < nFootH; ++nRow) {
        for (i32 i = 0; i < nFootW; ++i) {
            /* 0x1325C: index sub0 + 3*sub1 + 3*row + col, row stride 3 slots. */
            i32 n = nSub0 + 3 * nSub1 + 3 * nRow + i;
            if (n < 0 || n >= 9) {
                continue;
            }
            pUnit->apBelt[n] = pCtx->pSentinel;
            pUnit->abBeltMetaA[n] = (u8)nSub0;
            pUnit->abBeltMetaB[n] = (u8)nSub1;
        }
    }
    /* LABEL_161 with index 3*sub1 + sub0 + 91. */
    if (3 * nSub1 + nSub0 >= 0 && 3 * nSub1 + nSub0 < 9) {
        pUnit->apBelt[3 * nSub1 + nSub0] = pCtx->pHit->pHitItem;
    }
}

/* 0x12F3C and 0x1314C: the whole footprint must be empty before the drop is even
 * considered. The first occupied cell stops it. */
static bool Drop_FootprintFree(const Soldier *pUnit, i32 nSub0, i32 nSub1,
                               i32 nFootW, i32 nFootH, i32 nRowStride, i32 nSlots)
{
    const TactItem *const *apSlots = (nRowStride == 4)
        ? (const TactItem *const *)pUnit->apBackpack
        : (const TactItem *const *)pUnit->apBelt;

    for (i32 nRow = 0; nRow < nFootH; ++nRow) {
        for (i32 i = 0; i < nFootW; ++i) {
            i32 n = nSub0 + nRowStride * nSub1 + nRowStride * nRow + i;
            if (n >= 0 && n < nSlots && apSlots[n] != NULL) {
                return false;
            }
        }
    }
    return true;
}

/* 0x13440..0x134B8: the item lands in the selected unit's cell, on the lowest layer
 * at or below the unit's own layer whose cell has a floor sprite (the probe reads
 * the cell's first dword, which is pFloorSpr). */
static void Drop_Ground(const InvDropCtx *pCtx)
{
    const InvDropHooks *pHooks = pCtx->pHooks;
    MapCell *pSrc = pCtx->pSelUnit->pCell;
    i32 nZ;

    if (pSrc == NULL || pHooks->pfnLookupCell == NULL) {
        return;
    }
    nZ = pSrc->nZ;
    while (nZ > 0) {
        MapCell *pProbe = pHooks->pfnLookupCell(pSrc->nX, pSrc->nY, nZ, pHooks->pLookupUser);
        if (pProbe == NULL || pProbe->pFloorSpr != NULL) {
            break;
        }
        --nZ;
    }
    MapCell *pDst = pHooks->pfnLookupCell(pSrc->nX, pSrc->nY, nZ, pHooks->pLookupUser);
    if (pDst == NULL) {
        return;
    }
    MapCell_ListInsertByPri(pDst, pCtx->pHit->pHitItem);
    Inv_BuildVisibleList(pCtx->pUi, pCtx->pSelUnit);
}

/* 0x1291C..0x12DF0: the dragged item is a clip and the weapon is empty, so the clip
 * count moves into the weapon, the weapon's ammo index follows the clip, and the
 * clip object is destroyed and taken out of the pool. */
static bool Drop_LoadClip(const InvDropCtx *pCtx, TactItem *pWeapon)
{
    TactItem *pItem = pCtx->pHit->pHitItem;

    if (pWeapon->nAmmo != 0) {
        Drop_Modal(pCtx, 160, 45, 1);
        return false;
    }
    if (!Drop_ChargeClip(pCtx, PUFO_INV_TU_HAND)) {
        return false;
    }
    pWeapon->nAmmo = pItem->nAmmo;
    if (pCtx->pHooks->pfnPlaySample != NULL) {
        pCtx->pHooks->pfnPlaySample(6, pCtx->pHooks->pUser);
    }
    Drop_SetAmmoIndex(pCtx, pWeapon, pItem->pObj);
    Drop_PoolTakeOut(pCtx, pItem);
    return true;
}

/* The switch at 0x12340. True when the item is now in the slot. A false return is
 * the original's LABEL_164: the drop failed and the tail gets its turn. */
static bool Drop_InSlot(const InvDropCtx *pCtx, const InvHitRect *pRect)
{
    Soldier *pUnit = pCtx->pSelUnit;
    TactItem *pItem = pCtx->pHit->pHitItem;
    ObjectRec *pObj = pItem->pObj;
    i32 nSub0 = pRect->nSlot;
    i32 nSub1 = pRect->nCol;
    i32 nFootW = Drop_Foot(pObj, true);
    i32 nFootH = Drop_Foot(pObj, false);

    switch (pRect->nRegion) {
    case 0: /* pockets, table region 0..3 */
    case 1:
    case 2:
    case 3:
        if (nSub0 < 0 || nSub0 >= 2) {
            return false;
        }
        if (pUnit->aPockets[pRect->nRegion].apItem[nSub0] != NULL) {
            return false; /* 0x12384 */
        }
        if (nFootH > 1 || nSub0 + nFootW > 2) {
            return false; /* 0x123A8 */
        }
        if (!Drop_Charge(pCtx, (pRect->nRegion < 2) ? PUFO_INV_TU_POCKET_A
                                                    : PUFO_INV_TU_POCKET_B,
                         INV_REGION_POCKET0 + pRect->nRegion)) {
            return false;
        }
        Drop_MarkPocket(pCtx, pRect->nRegion, nSub0, nFootW);
        return true;

    case 4: /* the left hand */
        if (pUnit->pHandL != NULL) {
            TactItem *pWeapon = pUnit->pHandL;
            if (pObj == NULL || pObj->bTwoHanded != 1) {
                return false;
            }
            if (pWeapon->pObj != NULL && pWeapon->pObj->bListPri == 1 &&
                pUnit->pHandR != NULL) {
                Drop_Modal(pCtx, 200, 43, 2); /* LABEL_55 @ 0x128A8 */
                return false;
            }
            if (!Drop_WeaponTakes(pCtx, pWeapon, pObj)) {
                return false;
            }
            return Drop_LoadClip(pCtx, pWeapon);
        }
        if (pUnit->pHandR != NULL && Drop_HandBlocked(pUnit->pHandR->pObj, pObj)) {
            return false;
        }
        if (!Drop_Charge(pCtx, PUFO_INV_TU_HAND, INV_REGION_HAND_L)) {
            return false;
        }
        pUnit->pHandL = pItem; /* 0x12B7C */
        return true;

    case 5: /* the right hand */
        if (pUnit->pHandR == NULL) {
            if (pUnit->pHandL != NULL && Drop_HandBlocked(pUnit->pHandL->pObj, pObj)) {
                return false; /* 0x12E28 and 0x12E44 */
            }
            if (!Drop_Charge(pCtx, PUFO_INV_TU_HAND, INV_REGION_HAND_R)) {
                return false;
            }
            pUnit->pHandR = pItem; /* 0x12ED4 */
            return true;
        }
        if (pObj == NULL || pObj->bTwoHanded != 1) {
            return false;
        }
        if (pUnit->pHandR->pObj != NULL && pUnit->pHandR->pObj->bListPri == 1 &&
            pUnit->pHandL != NULL) {
            Drop_Modal(pCtx, 200, 43, 2);
            return false;
        }
        if (!Drop_WeaponTakes(pCtx, pUnit->pHandR, pObj)) {
            return false;
        }
        return Drop_LoadClip(pCtx, pUnit->pHandR);

    case 6: /* the backpack, 4 columns x 2 rows */
        if (nSub0 < 0 || nSub0 > 3 || nSub1 < 0 || nSub1 > 1) {
            return false;
        }
        if (pUnit->apBackpack[nSub0 + 4 * nSub1] != NULL) {
            return false;
        }
        if (nFootH + nSub1 > 2 || nFootW + nSub0 > 4) {
            return false;
        }
        if (!Drop_FootprintFree(pUnit, nSub0, nSub1, nFootW, nFootH, 4, 8)) {
            return false;
        }
        if (!Drop_Charge(pCtx, PUFO_INV_TU_BACKPACK, INV_REGION_BACKPACK)) {
            return false;
        }
        Drop_MarkBackpack(pCtx, nSub0, nSub1, nFootW, nFootH);
        return true;

    case 7: /* the belt, 3 columns x 3 rows */
        if (nSub0 < 0 || nSub0 > 2 || nSub1 < 0 || nSub1 > 2) {
            return false;
        }
        if (pUnit->apBelt[nSub0 + 3 * nSub1] != NULL) {
            return false;
        }
        if (nFootH + nSub1 > 3 || nFootW + nSub0 > 3) {
            return false;
        }
        if (!Drop_FootprintFree(pUnit, nSub0, nSub1, nFootW, nFootH, 3, 9)) {
            return false;
        }
        if (!Drop_Charge(pCtx, PUFO_INV_TU_BELT, INV_REGION_BELT)) {
            return false;
        }
        Drop_MarkBelt(pCtx, nSub0, nSub1, nFootW, nFootH);
        return true;

    default:
        /* 0x12340 default: LABEL_162, which counts as placed. Unreachable with the
         * shipped table, whose regions are all 0..7. */
        return true;
    }
}

/* 0x136A0: the drop failed, so the item goes back into the region and slot the hit
 * test recorded. No charge, no sound. */
static void Drop_Restore(const InvDropCtx *pCtx)
{
    Soldier *pUnit = pCtx->pSelUnit;
    TactItem *pItem = pCtx->pHit->pHitItem;
    i32 nSub0 = pCtx->pHit->nHitSub0;
    i32 nSub1 = pCtx->pHit->nHitSub1;
    i32 nFootW = Drop_Foot(pItem->pObj, true);
    i32 nFootH = Drop_Foot(pItem->pObj, false);

    switch (pCtx->pHit->nHitRegion) {
    case INV_REGION_GROUND:
        Drop_Ground(pCtx);
        break;
    case INV_REGION_POCKET0:
    case INV_REGION_POCKET1:
    case INV_REGION_POCKET2:
    case INV_REGION_POCKET3:
        if (nSub0 >= 0 && nSub0 < 2) {
            Drop_MarkPocket(pCtx, pCtx->pHit->nHitRegion - 1, nSub0, nFootW);
        }
        break;
    case INV_REGION_HAND_L:
        pUnit->pHandL = pItem; /* 0x139A8 */
        break;
    case INV_REGION_HAND_R:
        pUnit->pHandR = pItem; /* 0x139BC */
        break;
    case INV_REGION_BACKPACK:
        Drop_MarkBackpack(pCtx, nSub0, nSub1, nFootW, nFootH);
        break;
    case INV_REGION_BELT:
        Drop_MarkBelt(pCtx, nSub0, nSub1, nFootW, nFootH);
        break;
    default:
        break;
    }
}

bool Inv_HandlePointer(const InvDropCtx *pCtx)
{
    const InvDropHooks *pHooks;
    Soldier *pUnit;
    TactItem *pItem;
    i32 nX, nY;
    bool bPlaced = false;

    if (pCtx == NULL || pCtx->pHit == NULL || pCtx->pUi == NULL ||
        pCtx->pHooks == NULL || pCtx->pSelUnit == NULL ||
        pCtx->pHit->pHitItem == NULL) {
        return false;
    }
    pHooks = pCtx->pHooks;
    pUnit = pCtx->pSelUnit;
    pItem = pCtx->pHit->pHitItem;

    /* 0x122A8 / 0x122C0: the drag anchor is the stored drag offset plus the pen
     * position in the widget bank plus 10. */
    nX = pCtx->pHit->nDragOffX + pCtx->nPtrX + 10;
    nY = pCtx->pHit->nDragOffY + pCtx->nPtrY + 10;

    for (i32 i = 0; i < PUFO_INV_HITRECT_COUNT; ++i) {
        const InvHitRect *pRect = &s_aHitRect[i];
        if (nX < pRect->x0 || nX > pRect->x1 || nY < pRect->y0 || nY > pRect->y1) {
            continue;
        }
        bPlaced = Drop_InSlot(pCtx, pRect);
        break;
    }

    /* LABEL_164 @ 0x133A4: the ground strip, the same window the hit test uses. A
     * time-unit failure here still falls through to the split test. */
    if (!bPlaced && nX >= PUFO_INV_GROUND_X0 && nX <= PUFO_INV_GROUND_X1 &&
        nY >= PUFO_INV_GROUND_Y0 && nY <= PUFO_INV_GROUND_Y1) {
        if (Drop_Charge(pCtx, PUFO_INV_TU_GROUND, INV_REGION_GROUND)) {
            Drop_Ground(pCtx);
            bPlaced = true;
        }
    }

    /* 0x1353C: the ammo split. A single-item weapon with a clip in hand, dropped on
     * the hand panel while both hands are empty, becomes the right hand and its clip
     * a new item in the left hand. This one tests uTu even when the latch is set. */
    if (!bPlaced && nX >= PUFO_INV_DROP_GROUND_X0 && nX <= PUFO_INV_DROP_GROUND_X1 &&
        nY >= PUFO_INV_DROP_GROUND_Y0 && nY <= PUFO_INV_DROP_GROUND_Y1 &&
        pItem->pObj != NULL && pItem->pObj->bTwoHanded == 0 &&
        pItem->nAmmo != 0 && pItem->nAmmo != 255 &&
        pUnit->pHandL == NULL && pUnit->pHandR == NULL) {
        if ((i32)pUnit->uTu < PUFO_INV_TU_SPLIT) {
            Drop_Modal(pCtx, 160, 31, 1);
        } else {
            i32 nIdx = *pHooks->pnItemCount;
            TactItem *pNew = (pHooks->pfnAllocItem != NULL)
                ? pHooks->pfnAllocItem(pHooks->pUser) : NULL;

            if (pCtx->pUi->bActionLatch == 0) {
                pUnit->uTu = (u16)(pUnit->uTu - PUFO_INV_TU_SPLIT);
            }
            if (nIdx >= 0 && nIdx < PUFO_INV_ITEM_POOL_MAX) {
                pHooks->apItemPool[nIdx] = pNew;
            }
            if (pNew != NULL) {
                if (pHooks->pfnFindObject != NULL) {
                    pNew->pObj = pHooks->pfnFindObject(pItem->nIndex, pHooks->pUser);
                }
                pNew->nAmmo = pItem->nAmmo;
            }
            pItem->nAmmo = 0;
            *pHooks->pnItemCount = nIdx + 1;
            pUnit->pHandR = pItem;   /* 0x1362C */
            pUnit->pHandL = pNew;    /* 0x13650, the pool slot just written */
            bPlaced = true;
        }
    }

    if (bPlaced) {
        if (pHooks->pfnPlaySample != NULL) {
            pHooks->pfnPlaySample(17, pHooks->pUser);
        }
    } else {
        Drop_Restore(pCtx);
    }

    /* 0x13C40 / 0x13C5C: both run whatever happened. */
    if (pHooks->pInvStat != NULL) {
        Unit_RecalcInventoryStat(pUnit, pHooks->pInvStat);
    }
    Unit_UpdateAnimPose(pUnit, NULL, NULL);
    return bPlaced;
}

/* -------------------------------------------------- the loadout from the craft */

/* unk_A62F0 @ 0xA62F0: 13 dwords, the order left-hand weapons are tried in. */static const i32 s_aHandObjects[PUFO_TACT_HAND_TABLE_COUNT] = {
     6, 11, 5, 9, 3, 2, 4, 7, 8, 1, 12, 13, 10
};

/* unk_A6328 @ 0xA6328: 6 dwords, the order the third pickup is tried in. */
static const i32 s_aPackObjects[PUFO_TACT_PACK_TABLE_COUNT] = {
    18, 17, 15, 16, 19, 14
};

/* The strength gate. auStat[8] is loaded with LDRH so it is unsigned, and the divide is
 * __rt_sdiv. ORIGINAL BUG (0x3C2A4), not reproduced: a zero strength is a divide by zero in the
 * original. The port reports the item as too heavy, which is the only safe reading. */
static i32 Equip_Divide(i32 nNumerator, i32 nStrength)
{
    if (nStrength == 0) {
        return nNumerator;
    }
    return nNumerator / nStrength;
}

/* Park the pointer on a slot rect and run the drop.
 *
 * 0xAD714 and 0xAD718 are pBank+0x1044 and pBank+0x1048, which are the pointer position
 * Inv_HandlePointer reads. Writing a hit rect origin into them is how the original equips
 * an item with no mouse involved at all.
 *
 * bClearDrag is true for the two slot passes and the ammo pickup and FALSE for the third pickup.
 * ORIGINAL BUG (0x3C4C8), reproduced: at 0x3C4C8 that pickup stores the unit, the item and the
 * pointer but never zeroes the drag offset, unlike 0x3BF38, 0x3C1D8 and 0x3C3EC. It inherits
 * whatever the last re-test left there. */
static void Equip_Drive(TactEquipCtx *pCtx, Soldier *pUnit, TactItem *pItem,
                        i32 nRectIdx, bool bClearDrag)
{
    const InvDropHooks *pHooks = pCtx->pDrop->pHooks;

    pCtx->pDrop->nPtrX = s_aHitRect[nRectIdx].x0;
    pCtx->pDrop->nPtrY = s_aHitRect[nRectIdx].y0;
    if (bClearDrag) {
        pCtx->pHit->nDragOffX = 0;
        pCtx->pHit->nDragOffY = 0;
    }
    pCtx->pDrop->pSelUnit = pUnit;
    pCtx->pHit->pHitItem = pItem;
    (void)Inv_HandlePointer(pCtx->pDrop);
    MapCell_ListRemove(pCtx->pItemListCell, pItem);
    (void)Unit_RecalcInventoryStat(pUnit, pHooks->pInvStat);
}

/* The three candidate gates, all of the form "can this soldier carry this much more". */
typedef enum {
    PUFO_EQUIP_HAND,   /* nAmmo != 0 and 4*(burden+weight+3)/strength == 0 */
    PUFO_EQUIP_AMMO,   /* 4*(burden+weight)/strength < 1 */
    PUFO_EQUIP_PACK    /* 4*(burden+weight+3)/strength < 2 */
} EquipGate;

/* First loose item whose object is pWant and which passes the gate. The original walks the
 * list once per candidate object, so a candidate that is not present costs a full walk. */
static TactItem *Equip_FindLoose(TactEquipCtx *pCtx, Soldier *pUnit, ObjectRec *pWant,
                                 EquipGate gate)
{
    if (pWant == NULL) {
        return NULL;
    }
    for (TactItem *pItem = pCtx->pItemListCell->pItemList; pItem != NULL; pItem = pItem->pNext) {
        i32 nQuot;
        i32 nWeight;

        if (pItem->pObj != pWant) {
            continue;
        }
        nWeight = (i32)pItem->pObj->bWeight;
        if (gate == PUFO_EQUIP_HAND) {
            if (pItem->nAmmo == 0) {
                continue;
            }
            nQuot = Equip_Divide(4 * (pUnit->uInvBurden + nWeight + 3), pUnit->auStat[8]);
            if (nQuot != 0) {
                continue;
            }
        } else if (gate == PUFO_EQUIP_AMMO) {
            nQuot = Equip_Divide(4 * (pUnit->uInvBurden + nWeight), pUnit->auStat[8]);
            if (nQuot >= 1) {
                continue;
            }
        } else {
            nQuot = Equip_Divide(4 * (pUnit->uInvBurden + nWeight + 3), pUnit->auStat[8]);
            if (nQuot >= 2) {
                continue;
            }
        }
        return pItem;
    }
    return NULL;
}

/* Pass A, 0x3BE8C. The 25 declared slots whose object is not two-handed and not category 3.
 *
 * auUnk962[nSlot] at Soldier+0x3C2 is the object index the slot declares; abUnk1012[nSlot] at
 * Soldier+0x3F4 is a byte naming the object index that slot should end up holding. Hex-Rays
 * renders the cursor advancing by 2 and reading index 0 each time, which is this walk. */
static void Equip_SlotPass(TactEquipCtx *pCtx, Soldier *pUnit)
{
    const InvDropHooks *pHooks = pCtx->pDrop->pHooks;

    for (i32 nSlot = 0; nSlot < PUFO_INV_HITRECT_COUNT; ++nSlot) {
        if (pUnit->auUnk962[nSlot] == 0) {
            continue;
        }
        ObjectRec *pObj = pCtx->apObjects[pUnit->auUnk962[nSlot]];
        if (pObj == NULL || pObj->bTwoHanded != 0 || pObj->nCategory == 3) {
            continue;
        }
        for (TactItem *pItem = pCtx->pItemListCell->pItemList; pItem != NULL; pItem = pItem->pNext) {
            if (pItem->pObj != pObj || pItem->nAmmo == 0) {
                continue;
            }
            Equip_Drive(pCtx, pUnit, pItem, nSlot, true);

            if (pItem->pObj->bAmmoCap == 255) {
                break;
            }
            i32 nIndex = pItem->nIndex;
            if (nIndex == pUnit->abUnk1012[nSlot]) {
                break;
            }

            /* The slot wants object abUnk1012[nSlot]. Find a loose item whose object sort key is
             * that index, move this item's rounds into a new item carrying the object this one was
             * holding, and take the found item's rounds and identity for this slot. */
            TactItem *pMatch = pCtx->pItemListCell->pItemList;
            while (pMatch != NULL && pMatch->pObj != NULL &&
                   pMatch->pObj->uSortKey != pUnit->abUnk1012[nSlot]) {
                pMatch = pMatch->pNext;
            }
            if (pMatch != NULL) {
                TactItem *pNew = pHooks->pfnAllocItem != NULL
                    ? pHooks->pfnAllocItem(pHooks->pUser) : NULL;
                if (pNew != NULL && pHooks->apItemPool != NULL && pHooks->pnItemCount != NULL &&
                    *pHooks->pnItemCount < PUFO_INV_ITEM_POOL_MAX) {
                    pHooks->apItemPool[*pHooks->pnItemCount] = pNew;   /* 0x3C04C */
                    pNew->pObj = pCtx->apObjects[pItem->nIndex];       /* 0x3C070 */
                    pNew->nAmmo = pItem->nAmmo;                        /* 0x3C08C */
                    ++*pHooks->pnItemCount;
                    /* Hex-Rays renders this as v12[36109] after a base advance. ARM loads
                     * Map + 4*newCount + 0x23434, which is the slot just written. */
                    MapCell_ListInsertByPri(pCtx->pItemListCell, pNew);
                }
                pItem->nAmmo = 0;                        /* 0x3C094 */
                pItem->nIndex = pMatch->pObj->uSortKey;  /* 0x3C0D8 */
                pItem->nAmmo = pMatch->nAmmo;            /* 0x3C0E0 */
                MapCell_ListRemove(pCtx->pItemListCell, pMatch);
            }
            /* ORIGINAL BUG (0x3C0F0), not reproduced: when no entry matches by sort key the
             * original walks the whole list a second time for a match its own entry gate has made
             * impossible. It reaches this path only because nIndex != abUnk1012[nSlot], and the
             * match at 0x3C000 needs nIndex == abUnk1012[nSlot] with the same two operands. So the
             * swap at 0x3C0F0 is unreachable and the walk changes nothing. */
            break;
        }
    }
}

/* Pass B, 0x3C140. The same 25 slots with the complementary gate, and no ammo bookkeeping. */
static void Equip_TwoHandedPass(TactEquipCtx *pCtx, Soldier *pUnit)
{
    for (i32 nSlot = 0; nSlot < PUFO_INV_HITRECT_COUNT; ++nSlot) {
        if (pUnit->auUnk962[nSlot] == 0) {
            continue;
        }
        ObjectRec *pObj = pCtx->apObjects[pUnit->auUnk962[nSlot]];
        if (pObj == NULL ||
            (pObj->bTwoHanded == 0 && pObj->nCategory != 3 && pObj->nCategory != 5)) {
            continue;
        }
        for (TactItem *pItem = pCtx->pItemListCell->pItemList; pItem != NULL; pItem = pItem->pNext) {
            if (pItem->pObj != pObj) {
                continue;
            }
            Equip_Drive(pCtx, pUnit, pItem, nSlot, true);
            break;
        }
    }
}

/* A soldier that has never been equipped: left hand, then ammo for that weapon, then belt. */
static void Equip_FromCraft(TactEquipCtx *pCtx, Soldier *pUnit)
{
    const InvDropHooks *pHooks = pCtx->pDrop->pHooks;

    /* Left hand, 0x3C238. The only one of the four pickups that does not run the drop path:
     * it stores the item straight into pHandL and takes it out of the list itself. */
    for (i32 nTbl = 0; nTbl < PUFO_TACT_HAND_TABLE_COUNT; ++nTbl) {
        TactItem *pItem = Equip_FindLoose(pCtx, pUnit, pCtx->apObjects[s_aHandObjects[nTbl]],
                                          PUFO_EQUIP_HAND);
        if (pItem == NULL) {
            continue;
        }
        pUnit->pHandL = pItem;                                                    /* 0x3C2D8 */
        MapCell_ListRemove(pCtx->pItemListCell, pItem);
        (void)Unit_RecalcInventoryStat(pUnit, pHooks->pInvStat);
        break;
    }

    /* Ammo for whatever ended up in the left hand, 0x3C2EC. The gate is the ITEM's nAmmo, not
     * the weapon's ammo cap: nAmmo == 255 skips the search entirely. The weapon's three
     * anAmmoTypeId entries are tried in order and only the first hit is taken. */
    TactItem *pHand = pUnit->pHandL;
    if (pHand != NULL && pHand->pObj != NULL && pHand->nAmmo != 255) {
        for (i32 nSlot = 0; nSlot < 3; ++nSlot) {
            u16 nAmmoObj = pHand->pObj->anAmmoTypeId[nSlot];
            if (nAmmoObj == 0) {
                continue;
            }
            TactItem *pAmmo = Equip_FindLoose(pCtx, pUnit, pCtx->apObjects[nAmmoObj],
                                              PUFO_EQUIP_AMMO);
            if (pAmmo == NULL) {
                continue;
            }
            i32 nRect = (pAmmo->pObj->nFootW >= 3 || pAmmo->pObj->nFootH != 1) ? 16 : 4;
            Equip_Drive(pCtx, pUnit, pAmmo, nRect, true);
            break;
        }
    }

    /* The third pickup, 0x3C430. It stores aInvHitRects[10], and rect 10 is x 160..179 /
     * y 160..179, which is BACKPACK slot 0,0 - the belt rects are 16 through 24. So whatever
     * these six objects were meant to be, they land in the backpack. The drag offset is
     * deliberately left alone here, unlike the other three pickups. */
    for (i32 nTbl = 0; nTbl < PUFO_TACT_PACK_TABLE_COUNT; ++nTbl) {
        TactItem *pItem = Equip_FindLoose(pCtx, pUnit, pCtx->apObjects[s_aPackObjects[nTbl]],
                                          PUFO_EQUIP_PACK);
        if (pItem == NULL) {
            continue;
        }
        Equip_Drive(pCtx, pUnit, pItem, 10, false);
        break;
    }
}

/* Tact_EquipTeamFromCraft @ 0x3be24. */
void Tact_EquipTeamFromCraft(TactEquipCtx *pCtx)
{
    if (pCtx == NULL || pCtx->apXcomUnits == NULL || pCtx->pnXcomUnitCount == NULL ||
        pCtx->apObjects == NULL || pCtx->pItemListCell == NULL || pCtx->pHit == NULL ||
        pCtx->pDrop == NULL || pCtx->pDrop->pHooks == NULL) {
        return;
    }

    for (i32 nUnit = 0; nUnit < *pCtx->pnXcomUnitCount; ++nUnit) {
        Soldier *pUnit = pCtx->apXcomUnits[nUnit];

        if (pUnit == NULL) {
            continue;
        }
        if (pUnit->bUnk961 != 0) {
            Equip_SlotPass(pCtx, pUnit);
            Equip_TwoHandedPass(pCtx, pUnit);
        } else {
            Equip_FromCraft(pCtx, pUnit);
            pUnit->bUnk961 = 1;   /* 0x3C510 */
        }
    }
}
