/* pufo_enemyai.c - the enemy AI's decision predicates.
 *
 * Evidence: see pufo_enemyai.h for the address list and the table decode.
 */
#include <string.h>

#include "game/pufo_enemyai.h"

bool EnemyAi_MultiTargetTypeMatch(const Soldier *pUnit, const Soldier *pOther)
{
    u8 nRole;
    u8 bFlag0;

    if (pUnit == NULL || pOther == NULL) {
        return false;
    }
    /* The original indexes the 4-byte-stride table with no bounds check. An
     * out-of-range type would read past it; the port refuses instead and documents
     * that (deviation 1). */
    if (pUnit->nUnitType >= PUFO_UNIT_TYPE_COUNT) {
        return false;
    }

    /* The role code is read from the OTHER unit, not the candidate. */
    nRole = (u8)pOther->nMultiTarget;
    bFlag0 = g_aUnitTypeFlag0[pUnit->nUnitType];

    switch (nRole) {
        case 1:
            /* Flag set AND small. */
            return bFlag0 == 1 && g_aUnitTypeIsLarge[pUnit->nUnitType] == 0;
        case 2:
            /* Flag clear AND small. */
            return bFlag0 == 0 && g_aUnitTypeIsLarge[pUnit->nUnitType] == 0;
        case 3:
            /* Flag set; size is NOT tested here. */
            return bFlag0 == 1;
        case 4:
            /* Flag clear; size is NOT tested here. */
            return bFlag0 == 0;
        default:
            /* Every other role accepts any type. */
            return true;
    }
}

void EnemyAi_EnqueueRandomTurn(Soldier *pUnit, const EnemyAiHooks *pHooks)
{
    u32 nFacing;

    if (pUnit == NULL || pHooks == NULL || pHooks->pfnRandU16 == NULL) {
        return;
    }

    /* The draw comes first, then the enqueue, then the kick -- which in the original
     * are two separate calls performed back to back, so they are one hook here. */
    nFacing = pHooks->pfnRandU16(PUFO_ENEMYAI_FACING_MAX, pHooks->pUser);
    if (pHooks->pfnEnqueueTurnAndKick != NULL) {
        pHooks->pfnEnqueueTurnAndKick(pUnit, (u8)nFacing, pHooks->pUser);
    }
}

/* --- EnemyAi_TryFireOrWander @ 0x4abc8 --------------------------------------------- */

/* 0x4ac24..0x4ac90: the left hand is tested first, then the right. A hand is usable
 * when it exists, its object is not two-handed, and it either has ammo or its object
 * never runs out (bAmmoCap 255). */
static TactItem *EnemyAi_PickOneHanded(const Soldier *pUnit)
{
    TactItem *apHands[2] = { pUnit->pHandL, pUnit->pHandR };

    for (i32 i = 0; i < 2; ++i) {
        TactItem *pHand = apHands[i];
        if (pHand == NULL) {
            continue;
        }
        if (pHand->pObj->bTwoHanded != 0) {
            continue;
        }
        if (pHand->nAmmo != 0 || pHand->pObj->bAmmoCap == 255) {
            return pHand;
        }
    }
    return NULL;
}

/* 0x4ae44..0x4ae90: the drop descent. From the unit's own layer, walk DOWN while the
 * cell has no floor sprite.
 *
 * ORIGINAL BUG (0x4ae8c): the loop decrements and tests `> 0`, so layers nZ..1 are
 * tested and layer 0 is reached untested -- `SUB R7,#1; CMP R7,#0; BGT` exits before
 * the layer-0 slot is ever loaded. The floor walk later in the SAME function
 * (EnemyAi_FloorCellBelow, 0x4afd0 `SUBS / BPL`) does test layer 0. The port keeps the
 * asymmetry. */
static i32 EnemyAi_FloorLayerBelow(const EnemyAiActCtx *pCtx, const MapCell *pCell)
{
    i32 nLayer = (i32)pCell->nZ;

    if (nLayer <= 0) {
        return nLayer;
    }
    for (;;) {
        MapCell *pProbe = pCtx->pfnLookupCell(pCell->nX, pCell->nY, nLayer, pCtx->pUser);
        if (pProbe != NULL && pProbe->pFloorSpr != NULL) {
            break;
        }
        --nLayer;
        if (nLayer <= 0) {
            break;
        }
    }
    return nLayer;
}

/* 0x4af84..0x4afd0: the wander descent. Same idea, one layer deeper — the ARM's
 * `SUBS / BPL` tests layers nZ..0 inclusive, so a column with no floor anywhere ends
 * on the layer-0 cell. */
static MapCell *EnemyAi_FloorCellBelow(const EnemyAiActCtx *pCtx, i32 nX, i32 nY, i32 nLayer)
{
    MapCell *pCell = NULL;

    for (i32 n = nLayer; n >= 0; --n) {
        pCell = pCtx->pfnLookupCell(nX, nY, n, pCtx->pUser);
        if (pCell != NULL && pCell->pFloorSpr != NULL) {
            break;
        }
    }
    return pCell;
}

/* 0x4adc4..0x4ae08: one burst at the drawn target. The cost and the argument come from
 * the two action tables Ui_FillActionCosts just refilled: Map+0x4E4C and Map+0x4E7C. */
static void EnemyAi_EnqueueBurst(Soldier *pUnit, TactItem *pWeapon, i32 nMode,
                                 i32 nX, i32 nY, i32 nLayer, const EnemyAiActCtx *pCtx)
{
    FireBurstSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.pUnit         = pUnit;
    spec.pWeapon       = pWeapon;
    spec.nMode         = nMode;
    spec.nX            = nX;
    spec.nY            = nY;
    spec.nZ            = (u8)nLayer;
    spec.nTuCost       = pCtx->anActionCost[nMode];
    spec.nArg          = pCtx->anActionEnabled[nMode];
    spec.uAmmoInWeapon = pWeapon->nAmmo;
    spec.bAmmoCap      = pWeapon->pObj->bAmmoCap;

    /* The original ignores the result: a unit with too little TU simply gets the
     * out-of-TU message from inside the enqueue and the burst is dropped. */
    (void)TactState_EnqueueFireBurst(pCtx->pState, &spec, pCtx->pfnFaceToward, pCtx->pUser);
}

/* 0x4aca4..0x4ae28. */
static void EnemyAi_FireBurst(Soldier *pUnit, TactItem *pWeapon, const EnemyAiActCtx *pCtx)
{
    const MapCell *pCell = pUnit->pCell;
    i32 nShots;

    *pCtx->pnActionMode = 1;
    if (pCtx->pfnFillActionCosts != NULL) {
        pCtx->pfnFillActionCosts(pCtx->pUser);
    }

    /* 0x4acc0..0x4acd4: an unlimited-ammo weapon gets the cap straight away; a magazine
     * gets min(nAmmo, 3). */
    if (pWeapon->pObj->bAmmoCap == 255) {
        nShots = PUFO_ENEMYAI_SHOTS_MAX;
    } else {
        nShots = pWeapon->nAmmo > PUFO_ENEMYAI_SHOTS_MAX ? PUFO_ENEMYAI_SHOTS_MAX
                                                         : (i32)pWeapon->nAmmo;
    }

    while (nShots > 0) {
        i32 nX, nY, nLayer;

        /* 0x4ace0..0x4ad70: draw and re-draw until the cell is on the map. The original
         * parks the three draws in Map scratch (0x128A20/0x128A24/0x128A28) and reads
         * them back; those three globals have no other reference anywhere in the binary,
         * so they are this function's private scratch and the port keeps them local. */
        for (;;) {
            nX     = (i32)pCell->nX
                   + (i32)pCtx->pfnRandU16(PUFO_ENEMYAI_FIRE_SPAN, pCtx->pUser)
                   - PUFO_ENEMYAI_FIRE_DROP;
            nY     = (i32)pCell->nY
                   + (i32)pCtx->pfnRandU16(PUFO_ENEMYAI_FIRE_SPAN, pCtx->pUser)
                   - PUFO_ENEMYAI_FIRE_DROP;
            nLayer = (i32)pCtx->pfnRandU16(PUFO_ENEMYAI_FIRE_LAYER_MAX, pCtx->pUser);
            if (Map_IsValidCell(pCtx->pGeom, nX, nY, nLayer)) {
                break;
            }
        }

        if (nX != (i32)pCell->nX) {
            EnemyAi_EnqueueBurst(pUnit, pWeapon, 1, nX, nY, nLayer, pCtx);
        } else {
            /* ORIGINAL BUG (0x4ada4): same column as the shooter. The drawn y is
             * replaced by the shooter's own row (0x4ad90/0x4ada0), and then a shooter
             * NOT on row 0 redraws from scratch without spending a shot (0x4ada4
             * `BNE loc_4ACE0` jumps back to the draw, not to the decrement at
             * 0x4ae0c), so the loop's length is unbounded. A shooter ON row 0 falls
             * through and fires a burst at its own cell. Hex-Rays shows the decrement
             * running for both cases, which would fire zero shots where the ARM fires
             * one. The port keeps both halves. */
            nY = (i32)pCell->nY;
            if (nY != 0) {
                continue;
            }
            EnemyAi_EnqueueBurst(pUnit, pWeapon, 1, nX, nY, nLayer, pCtx);
        }
        --nShots;
    }

    *pCtx->pbActionLatch = 0;
    TactState_KickQueue(pCtx->pState);
}

/* 0x4ae2c..0x4b024. */
static void EnemyAi_Wander(Soldier *pUnit, const EnemyAiActCtx *pCtx)
{
    MapCell *pCell = pUnit->pCell;

    /* 0x4ae3c: only the four small unit types drop what they are carrying. */
    if (pUnit->nUnitType <= PUFO_ENEMYAI_DROP_TYPE_MAX) {
        i32 nLayer = EnemyAi_FloorLayerBelow(pCtx, pCell);
        MapCell *pDrop = pCtx->pfnLookupCell(pCell->nX, pCell->nY, nLayer, pCtx->pUser);

        if (pUnit->pHandL != NULL) {
            (void)MapCell_ListInsertByPri(pDrop, pUnit->pHandL);
            pUnit->pHandL = NULL;
        }
        if (pUnit->pHandR != NULL) {
            (void)MapCell_ListInsertByPri(pDrop, pUnit->pHandR);
            pUnit->pHandR = NULL;
        }
        Unit_UpdateAnimPose(pUnit, NULL, NULL);
    }

    for (;;) {
        i32 nX = (i32)pCell->nX
               + (i32)pCtx->pfnRandU16(PUFO_ENEMYAI_WANDER_SPAN, pCtx->pUser)
               - PUFO_ENEMYAI_WANDER_DROP;
        i32 nY = (i32)pCell->nY
               + (i32)pCtx->pfnRandU16(PUFO_ENEMYAI_WANDER_SPAN, pCtx->pUser)
               - PUFO_ENEMYAI_WANDER_DROP;

        /* 0x4af4c: the bounds test is always against layer 0, whatever layer the
         * candidate is later taken from. */
        if (!Map_IsValidCell(pCtx->pGeom, nX, nY, 0)) {
            continue;
        }

        MapCell *pTarget = pCtx->pfnLookupCell(nX, nY, (i32)pCell->nZ, pCtx->pUser);

        /* 0x4af88..0x4af94: types whose flag0 column is clear must land on a floor. */
        if (pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT
            && g_aUnitTypeFlag0[pUnit->nUnitType] == 0) {
            pTarget = EnemyAi_FloorCellBelow(pCtx, nX, nY, (i32)pCell->nZ);
        }

        if (pTarget == NULL || pTarget == pCell || MapCell_HasOccupant(pTarget)) {
            continue;
        }

        if (!pCtx->pfnPathToCell(pTarget->nX, pTarget->nY, pTarget->nZ, 1, pCtx->pUser)) {
            /* 0x4b00c..0x4b024: a failed path leaves the unit with no TU and clears the
             * AI-action flag. On success the flag stays up and the queue walker's
             * completion tail (0x544D8) is what clears it. */
            pUnit->uTu = 0;
            pCtx->pState->bAiActionActive = 0;
        }
        return;
    }
}

void EnemyAi_TryFireOrWander(Soldier *pUnit, const EnemyAiActCtx *pCtx)
{
    TactItem *pWeapon;
    u32 nRoll;
    char szMsg[PUFO_ENEMYAI_MSG_SIZE];
    size_t nLen = 0;

    if (pUnit == NULL || pUnit->pCell == NULL || pCtx == NULL) {
        return;
    }

    /* 0x4abdc: a large unit does nothing at all -- not even the status message. */
    if (Unit_bIsLarge(pUnit)) {
        return;
    }

    nRoll = pCtx->pfnRandU16(PUFO_ENEMYAI_FIRE_ROLL_MAX, pCtx->pUser);

    /* 0x4ac04 and 0x4ac1c: the AI-action pair is raised before the path is chosen. */
    pCtx->pState->pAiActionUnit  = pUnit;
    pCtx->pState->bAiActionActive = 1;

    /* ORIGINAL BUG (0x4ac90): the chosen hand is stored into Map.pSelWeapon BEFORE the
     * path is chosen, and the wander path then drops that very item into a cell list
     * and clears the hand (0x4aec4..0x4aec8, 0x4aefc..0x4af00). Map.pSelWeapon is left
     * pointing at an item the unit no longer holds. The port reproduces the order. */
    *pCtx->ppSelWeapon = NULL;
    pWeapon = EnemyAi_PickOneHanded(pUnit);
    if (pWeapon != NULL) {
        *pCtx->ppSelWeapon = pWeapon;
    }

    if (nRoll != 0 || pWeapon == NULL) {
        EnemyAi_Wander(pUnit, pCtx);
    } else {
        EnemyAi_FireBurst(pUnit, pWeapon, pCtx);
    }

    /* 0x4b028..0x4b068: both paths end here. The original copies the name with LDRSB
     * into a 200-byte stack buffer and strcat's unk_104078 onto it; that buffer is all
     * zeros and its only three references in the whole binary are these instructions,
     * so it appends nothing and the message is just the name. */
    while (nLen < sizeof(pUnit->szName) && pUnit->szName[nLen] != 0
           && nLen + 1 < sizeof(szMsg)) {
        szMsg[nLen] = pUnit->szName[nLen];
        ++nLen;
    }
    szMsg[nLen] = 0;

    if (pCtx->pfnPushStatus != NULL) {
        pCtx->pfnPushStatus(szMsg, pCtx->pUser);
    }
}
