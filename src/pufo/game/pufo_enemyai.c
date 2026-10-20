/* pufo_enemyai.c - the enemy AI's decision predicates.
 *
 * Evidence: see pufo_enemyai.h for the address list and the table decode.
 */
#include <math.h>
#include <string.h>

#include "game/pufo_boot.h"   /* PUFO_TEXT_REC_STRIDE, the 205-byte TEXTS slot stride */
#include "game/pufo_enemyai.h"
#include "game/pufo_path.h"   /* g_aStepDir, the aDirDeltaXY triples @ 0xa7ae0 */
#include "game/pufo_tactui.h" /* TactItem_TryReloadClip, the PUFO_ACTION_MODE_* codes */

/* The shared core of EnemyAi_bMultiTargetTypeMatch @ 0x4b928. It is split out because
 * Tact_EnemyAiTick feeds it the role byte of an RMP node (RmpNode.nMultiTargetRole @ 0x1A),
 * which the original reaches by casting the node to a Soldier* so the read lands on
 * Soldier.nMultiTarget (0x01A on ILP32). Soldier shifts on Win64, so the cast would read
 * the wrong byte here; the port passes the role explicitly instead. */
static bool EnemyAi_MultiTargetRoleMatch(const Soldier *pUnit, u8 nRole)
{
    u8 bFlag0;

    if (pUnit == NULL) {
        return false;
    }
    /* The original indexes the 4-byte-stride table with no bounds check. An
     * out-of-range type would read past it; the port refuses instead and documents
     * that (deviation 1). */
    if (pUnit->nUnitType >= PUFO_UNIT_TYPE_COUNT) {
        return false;
    }

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

bool EnemyAi_MultiTargetTypeMatch(const Soldier *pUnit, const Soldier *pOther)
{
    if (pUnit == NULL || pOther == NULL) {
        return false;
    }
    /* The role code is read from the OTHER unit, not the candidate. */
    return EnemyAi_MultiTargetRoleMatch(pUnit, (u8)pOther->nMultiTarget);
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

/* The shared "no free time" record, record 1. In the original the refused enqueue callee pushes
 * it itself (TactState_EnqueueFireBurst 0x4B0D8, TactState_EnqueueThrow 0x4B318,
 * Tact_EnqueueActionOrStatus205 0x4B42C) and the call sites never mention it; the port's helpers
 * only report the failure, so every AI call site that can be refused pushes it here. */
static void EnemyAi_PushOutOfTu(void (*pfnPushTextRecord)(i32 nTextRecordIndex, void *pUser),
                                void *pUser)
{
    if (pfnPushTextRecord != NULL) {
        pfnPushTextRecord(PUFO_TXT_OUT_OF_TU, pUser);
    }
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

    /* This is the one enqueue in the panic AI that can actually be refused: the site passes
     * Map_anActionCost[Map_nActionMode] straight to the callee with no gate of its own (0x4AE08),
     * so a unit with too little TU lands on 0x4B0D8, which pushes record 1 and drops the burst.
     * The original gets that push from the callee; the port's helper only reports the failure. */
    if (!TactState_EnqueueFireBurst(pCtx->pState, &spec, pCtx->pfnFaceToward, pCtx->pUser)) {
        EnemyAi_PushOutOfTu(pCtx->pfnPushTextRecord, pCtx->pPushTextUser);
    }
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

        /* 0x4af88..0x4af94: types whose flag0 column is clear must land on a floor.
         * The descent itself is the shared MapCell_FloorCellBelow -- the same
         * inclusive `SUBS / BPL` walk Tact_HandleMapClick @ 0x15500 does. */
        if (pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT
            && g_aUnitTypeFlag0[pUnit->nUnitType] == 0) {
            pTarget = MapCell_FloorCellBelow(pCtx->pfnLookupCell, pCtx->pUser,
                                             nX, nY, (i32)pCell->nZ);
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

    /* 0x4b028..0x4b068: both paths end here. The name (Unit + 0xBA) is copied with LDRSB into the
     * 200-byte stack scratch, then `LDR R0,=0xB38D0 / LDR R3,=(unk_104078-0xB38D0) / ADD R1,R0,R3`
     * hands unk_104078 to strcat at 0x4B058. unk_104078 is not scratch: 0x104078 ==
     * 0xB38D0 + 205 * 1608, so it is TEXTS[1608], lang.dat's " в панике" with its leading space,
     * and the message is "<name> in panic". The three reads at 0x4B04C..0x4B074 are the only xrefs
     * a census sees on a TEXTS slot -- the boot load fills it through the record-copy loop at
     * Assets_LoadStartup, and a loop store leaves no xref. */
    while (nLen < sizeof(pUnit->szName) && pUnit->szName[nLen] != 0
           && nLen + 1 < sizeof(szMsg)) {
        szMsg[nLen] = pUnit->szName[nLen];
        ++nLen;
    }
    if (pCtx->pTextRecs != NULL) {
        const char *pszSuffix = (const char *)pCtx->pTextRecs
                              + PUFO_TEXT_REC_STRIDE * PUFO_TXTREC_ENEMYAI_PANIC;

        while (*pszSuffix != 0 && nLen + 1 < sizeof(szMsg)) {
            szMsg[nLen++] = *pszSuffix++;
        }
    }
    szMsg[nLen] = 0;

    if (pCtx->pfnPushStatus != NULL) {
        pCtx->pfnPushStatus(szMsg, pCtx->pUser);
    }
}

/* --- Tact_EnemyAiTick @ 0x4cbe0 ----------------------------------------------------- */

/* What one acting unit did, so the roster loop can tell "run the tail" from
 * "keep walking". LABEL_110, LABEL_82 and LABEL_96 in the original. */
typedef enum AiActPath {
    AI_PATH_TAIL,     /* go to the function tail; the roster loop ends */
    AI_PATH_FALLBACK, /* fall into the LABEL_82 random turn / RMP move */
    AI_PATH_NEXT_UNIT /* LABEL_96: nothing for this unit, next enemy */
} AiActPath;

/* A cell distance the original cannot produce: it reads the footprint cells and the eight
 * neighbour cells with no bounds test at all (0x4CE50, 0x4D09C), so a unit on the last row
 * or the top layer walks past apCells. The port refuses instead, and a refused cell gets a
 * distance no reach test can accept. */
#define PUFO_ENEMYAI_NO_DIST 1000000.0

static double EnemyAi_Distance(i32 nAx, i32 nAy, i32 nBx, i32 nBy)
{
    i32 nDx = nAx - nBx;
    i32 nDy = nAy - nBy;

    return sqrt((double)(nDx * nDx) + (double)(nDy * nDy));
}

static double EnemyAi_CellDistanceD(const MapCell *pA, const MapCell *pB)
{
    if (pA == NULL || pB == NULL) {
        return PUFO_ENEMYAI_NO_DIST;
    }
    return EnemyAi_Distance((i32)pA->nX, (i32)pA->nY, (i32)pB->nX, (i32)pB->nY);
}

/* The truncated form the original uses for every reach test: coredll's _dtoi on the sqrt. */
static i32 EnemyAi_CellDistance(const MapCell *pA, const MapCell *pB)
{
    return (i32)EnemyAi_CellDistanceD(pA, pB);
}

/* Game_RandU16 over the unit's own multi-target count, read as a SIGNED byte at 0x4D194. */
static i32 EnemyAi_RandMultiIndex(const Soldier *pUnit, const EnemyAiTickCtx *pCtx)
{
    return (i32)pCtx->pfnRandU16((u32)(i32)pUnit->nMultiTarget, pCtx->pUser);
}

/* Unit_FaceToward needs the queue it belongs to, and the two enqueues that face a unit
 * take a plain (unit, x, y) seam, so the queue travels as the seam's user pointer. */
static void EnemyAi_FaceTowardQueue(Soldier *pUnit, i32 nX, i32 nY, void *pUser)
{
    Unit_FaceToward((TactState *)pUser, pUnit, nX, nY);
}

/* 0x4d660..0x4d734: hand a melee action to ANOTHER enemy that is ready to act.
 *
 * ORIGINAL BUG (0x4d660): the acting unit found the usable multi-target, but the action is
 * built entirely around the neighbour found here -- Tact_SelectUnit(neighbour), the enqueue
 * against the target cell's occupant, and `Map_pSelUnit->uTu >>= 1` all address the
 * neighbour -- the halving is `LDRH R3,[R0,#6]; MOV R3,R3,LSR#1; STRH R3,[R0,#6]` on the
 * slot at 0x4d728..0x4d730. The unit this tick was picked for spends nothing, is not marked
 * done, and is a candidate again on the next tick. The port reproduces it. */
static void EnemyAi_MeleeNeighbour(Soldier *pOther, const MapCell *pTargetCell,
                                   const EnemyAiTickCtx *pCtx)
{
    i32 nMode;

    Tact_SelectUnit(pCtx->pSelect, pOther);
    nMode = (pCtx->pfnRandU16(PUFO_ENEMYAI_NEIGHBOUR_ROLL, pCtx->pUser) == 0)
              ? PUFO_ENEMYAI_MODE_NEIGHBOUR_A
              : PUFO_ENEMYAI_MODE_NEIGHBOUR_B;
    *pCtx->ppSelWeapon = pCtx->pItemMelee;
    *pCtx->pnActionMode = nMode;
    *pCtx->pnBurstShots = 0;
    *pCtx->pnMeleeCellX = pTargetCell->nX;
    *pCtx->pnMeleeCellY = pTargetCell->nY;
    *pCtx->pnMeleeCellLayer = pTargetCell->nZ;

    /* Map_pSelUnit is read back rather than using pOther because that is the slot the
     * original reaches through, and Tact_SelectUnit @ 0x2c6c4 has just written it. */
    (void)TactState_EnqueueAction18(pCtx->pQueue, *pCtx->ppSelUnit, pTargetCell->pUnit,
                                    pCtx->pItemMelee, 0, nMode);
    (*pCtx->ppSelUnit)->uTu >>= 1;
}

/* 0x4cde0..0x4d188: the melee branch, for the six unit types whose flag-3 column is set. */
static AiActPath EnemyAi_MeleeWalk(Soldier *pUnit, TactCtx272 *pTact, const EnemyAiTickCtx *pCtx)
{
    MapCell *pOwn = pUnit->pCell;
    i32 nTries = PUFO_ENEMYAI_MELEE_TRIES;

    for (;;) {
        Soldier *pTarget = pUnit->multiOrAnim.apMultiTargets[EnemyAi_RandMultiIndex(pUnit, pCtx)];
        MapCell *pTargetCell = pTarget->pCell;
        MapCell *pNorth, *pEast, *pNorthEast;
        const StepDir *pDir;
        MapCell *pCell;
        i32 nOwn;
        bool bReach;
        i32 nLayer;

        --nTries;

        /* The other three cells of a large unit's 2x2 footprint: one row north, one column
         * east, and the diagonal corner. Loaded every pass, used only for a large unit. */
        pNorth     = pCtx->pfnLookup((i32)pOwn->nX, (i32)pOwn->nY + 1, (i32)pOwn->nZ, pCtx->pUser);
        pEast      = pCtx->pfnLookup((i32)pOwn->nX + 1, (i32)pOwn->nY, (i32)pOwn->nZ, pCtx->pUser);
        pNorthEast = pCtx->pfnLookup((i32)pOwn->nX + 1, (i32)pOwn->nY + 1, (i32)pOwn->nZ, pCtx->pUser);

        pDir = &g_aStepDir[pUnit->bFacing];
        nLayer = (i32)pTargetCell->nZ;
        if (Map_IsValidCell(&pCtx->geom, (i32)pOwn->nX + pDir->dx, (i32)pOwn->nY + pDir->dy, nLayer)) {
            pCell = pCtx->pfnLookup((i32)pOwn->nX + pDir->dx, (i32)pOwn->nY + pDir->dy, nLayer,
                                    pCtx->pUser);
            /* 0x4CEC4: the drawn cell counts only when the target really stands on it. */
            if (pCell == NULL || pCell->pUnit != pTarget) {
                pCell = pTargetCell;
            }
        } else {
            pCell = pTargetCell;
        }

        nOwn   = EnemyAi_CellDistance(pOwn, pCell);
        bReach = nOwn < PUFO_ENEMYAI_MELEE_REACH;
        if (Unit_bIsLarge(pUnit)) {
            bReach = bReach
                  || EnemyAi_CellDistance(pNorth, pCell) < PUFO_ENEMYAI_MELEE_REACH
                  || EnemyAi_CellDistance(pEast, pCell) < PUFO_ENEMYAI_MELEE_REACH
                  || EnemyAi_CellDistance(pNorthEast, pCell) < PUFO_ENEMYAI_MELEE_REACH;
        }

        if (bReach) {
            /* 0x4D73C: the melee commit. The action itself is charged 0 TU and the unit's
             * TU is simply zeroed, so the unit ends its turn with the attack. */
            pUnit->uTu = 0;
            *pCtx->pnMeleeCellX = pCell->nX;
            *pCtx->pnMeleeCellY = pCell->nY;
            *pCtx->pnMeleeCellLayer = pTargetCell->nZ;
            (void)TactState_EnqueueAction18(pCtx->pQueue, pUnit, pTarget,
                                            pCtx->pItemAction13, 0, PUFO_ENEMYAI_CMD13);
            pTact->bAiFocusSaved = 1;
            return AI_PATH_TAIL;
        }

        /* 0x4D09C..0x4D150: eight directions, on the layer the unit's own ceiling raises it
         * to. ORIGINAL BUG (0x4D09C): the cell is fetched straight out of apCells with no
         * Map_IsValidCell test, so a unit on the last column or row, or on layer 3 with a
         * low ceiling byte, reads a cell pointer outside the grid. The port bounds-checks
         * and skips instead. */
        nLayer = (pOwn->pCeilSpr != NULL && pOwn->pCeilSpr->iCeilParam < PUFO_ENEMYAI_CEIL_RAISE)
               ? (i32)pOwn->nZ + 1
               : (i32)pOwn->nZ;
        for (i32 i = 0; i < PUFO_ENEMYAI_DIR_SCAN; ++i) {
            i32 nX = (i32)pOwn->nX + g_aStepDir[i].dx;
            i32 nY = (i32)pOwn->nY + g_aStepDir[i].dy;
            MapCell *pCand;
            Soldier *pOcc;

            if (!Map_IsValidCell(&pCtx->geom, nX, nY, nLayer)) {
                continue;
            }
            pCand = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);
            pOcc = (pCand != NULL) ? pCand->pUnit : NULL;
            if (pOcc == NULL) {
                continue;
            }
            if (pOcc->bStatus != 0 && pOcc->nUnitType != PUFO_UNIT_TYPE_SPOTTED_A
                && pOcc->nUnitType != PUFO_UNIT_TYPE_SPOTTED_B) {
                continue;
            }

            Unit_FaceToward(pCtx->pQueue, pUnit, nX, nY);
            (void)TactState_EnqueueAction18(pCtx->pQueue, pUnit, pOcc,
                                            pCtx->pItemAction13, 0, PUFO_ENEMYAI_CMD13);
            pTact->bAiFocusSaved = 1;
            return AI_PATH_TAIL;
        }

        if (pCtx->pfnPathToCell(pTargetCell->nX, pTargetCell->nY, pTargetCell->nZ, 0,
                                pCtx->pUser)) {
            pTact->bAiFocusSaved = 1;
            return AI_PATH_TAIL;
        }
        if (nTries <= 0) {
            return AI_PATH_FALLBACK;
        }
    }
}

/* 0x4d858..0x4d890: for a unit of type 0..3 the weapon is the LAST hand holding a
 * one-handed object of category 0..2, so the right hand wins when both qualify. */
static TactItem *EnemyAi_PickSmallWeapon(const Soldier *pUnit)
{
    TactItem *apHands[2] = { pUnit->pHandL, pUnit->pHandR };
    TactItem *pWeapon = NULL;

    for (i32 i = 0; i < 2; ++i) {
        TactItem *pHand = apHands[i];

        if (pHand != NULL && pHand->pObj->bTwoHanded == 0
            && pHand->pObj->nCategory <= PUFO_ENEMYAI_WEAPON_CATEGORY_MAX) {
            pWeapon = pHand;
        }
    }
    return pWeapon;
}

/* 0x4d194..0x4d8b0: the throw, or the shot. */
static AiActPath EnemyAi_ThrowOrFire(Soldier *pUnit, TactCtx272 *pTact, const EnemyAiTickCtx *pCtx)
{
    Soldier *pTarget = pUnit->multiOrAnim.apMultiTargets[EnemyAi_RandMultiIndex(pUnit, pCtx)];
    MapCell *pTargetCell = pTarget->pCell;
    i32 nDist = EnemyAi_CellDistance(pUnit->pCell, pTargetCell);
    TactItem *pWeapon;
    FireBurstSpec spec;
    double dCost;
    i32 nMode;

    /* 0x4d7c4..0x4d800: the throw needs the roll, the range, a belt item in object
     * category 3, and a unit type above 3. */
    if (pCtx->pfnRandU16(PUFO_ENEMYAI_THROW_ROLL, pCtx->pUser) == 0
        && nDist <= PUFO_ENEMYAI_THROW_DIST_MAX
        && pUnit->apBelt[0] != NULL
        && pUnit->apBelt[0]->pObj->nCategory == PUFO_ENEMYAI_THROW_CATEGORY
        && (i32)pUnit->nUnitType > (i32)PUFO_ENEMYAI_DROP_TYPE_MAX) {
        TactItem *pItem = pUnit->apBelt[0];

        pUnit->apBelt[0] = NULL;
        pUnit->pHandR = pItem;
        *pCtx->ppSelWeapon = pItem;
        if (pCtx->pfnFillActionCosts != NULL) {
            pCtx->pfnFillActionCosts(pCtx->pUser);
        }
        /* 0x4D818 stores pHandR into Map.pSelWeapon a second time after the refill. */
        *pCtx->ppSelWeapon = pUnit->pHandR;
        pItem->bUnk7 = 0;
        pItem->bUnk8 = 1;
        *pCtx->pnActionMode = PUFO_ACTION_MODE_MENU;

        /* Map_nThrowTuCost @ 0x118830 and Map_nThrowArg @ 0x118860 are loaded by literal at
         * 0x4D82C and 0x4D80C; both are just slot 11 of the two action tables.
         * 0x4D844 is the reachable refusal of this tick: the branch is chosen on the roll, the
         * distance, the belt item and the unit type, and the only TU test on the way here is the
         * unit gate `uTu >= 15` at 0x4cc1c, which says nothing about slot 11's cost. So the throw
         * lands on 0x4B318 for a short of time unit; the callee pushes record 1 there, and the
         * push belongs here for the port. The tail (bAiFocusSaved, bAiDone) runs either way. */
        if (!TactState_EnqueueThrow(pCtx->pQueue, pUnit, pUnit->pHandR,
                                    pTargetCell->nX, pTargetCell->nY, pTargetCell->nZ,
                                    pCtx->anActionCost[PUFO_ACTION_MODE_MENU],
                                    pCtx->anActionEnabled[PUFO_ACTION_MODE_MENU],
                                    EnemyAi_FaceTowardQueue, pCtx->pQueue)) {
            EnemyAi_PushOutOfTu(pCtx->pfnPushTextRecord, pCtx->pPushTextUser);
        }
        pTact->bAiFocusSaved = 1;
        pUnit->bAiDone = 1;
        return AI_PATH_TAIL;
    }

    if ((i32)pUnit->nUnitType <= (i32)PUFO_ENEMYAI_DROP_TYPE_MAX) {
        pWeapon = EnemyAi_PickSmallWeapon(pUnit);
        *pCtx->ppSelWeapon = pWeapon;   /* 0x4D858 clears the slot first, then each hand wins */
    } else {
        if (pUnit->pHandL == NULL) {
            return AI_PATH_FALLBACK;    /* 0x4D8A0: Map.pSelWeapon is left as it was */
        }
        pWeapon = pUnit->pHandL;
        *pCtx->ppSelWeapon = pWeapon;
    }
    if (pWeapon == NULL) {
        return AI_PATH_FALLBACK;
    }

    if (pCtx->pfnFillActionCosts != NULL) {
        pCtx->pfnFillActionCosts(pCtx->pUser);
    }
    if (!TactItem_TryReloadClip(pWeapon, pWeapon->pObj->bAmmoCap, pWeapon->pObj->anAmmoTypeId[0],
                                pUnit, pCtx->pfnClipAmmoCap, pCtx->pUser)) {
        return AI_PATH_FALLBACK;
    }

    if (pWeapon->pObj->uSortKey != PUFO_ENEMYAI_SORTKEY_MULTI) {
        nMode = PUFO_ACTION_MODE_SNAP;
        *pCtx->pnActionMode = nMode;
    } else {
        if (pTarget->nUnitType == PUFO_UNIT_TYPE_SPOTTED_A
            || pTarget->nUnitType == PUFO_UNIT_TYPE_SPOTTED_B
            || nDist < PUFO_ENEMYAI_MULTI_DIST_MIN) {
            return AI_PATH_FALLBACK;
        }
        nMode = PUFO_ACTION_MODE_MULTI;
        *pCtx->pnActionMode = nMode;
        *pCtx->pnMultiTargetCount = 1;
        *pCtx->pnBurstShots = 1;
        pCtx->apMultiTargets[0] = pTargetCell;
    }

    /* 0x4d448 / 0x4d458: the soft-float gate, 0x3FE5555555555555 = 2/3 and
     * 0x4020000000000000 = 8.0. The unit must be able to pay the FULL table cost plus the
     * bias, but what it is charged is the two-thirds value truncated to an integer. */
    dCost = (double)pCtx->anActionCost[nMode];
    if ((double)pUnit->uTu < dCost * PUFO_ENEMYAI_FIRE_COST_SCALE + PUFO_ENEMYAI_FIRE_COST_BIAS) {
        return AI_PATH_FALLBACK;
    }

    memset(&spec, 0, sizeof(spec));
    spec.pUnit             = pUnit;
    spec.pWeapon           = pWeapon;
    spec.nMode             = nMode;
    spec.nX                = pTargetCell->nX;
    spec.nY                = pTargetCell->nY;
    spec.nZ                = pTargetCell->nZ;
    spec.nTuCost           = (i32)(dCost * PUFO_ENEMYAI_FIRE_COST_SCALE);
    spec.nArg              = pCtx->anActionEnabled[nMode];
    spec.uAmmoInWeapon     = pWeapon->nAmmo;
    spec.bAmmoCap          = pWeapon->pObj->bAmmoCap;
    spec.apMultiTargets    = pCtx->apMultiTargets;
    spec.nMultiTargetCount = *pCtx->pnMultiTargetCount;

    /* 0x4D910: no push here, because this call can never be refused. The gate at 0x4d448..0x4d458
     * let us through only when uTu >= cost*(2/3) + 8.0, and the cost handed to the callee is
     * (int)(cost*(2/3)) -- strictly below that uTu. The callee's own test at 0x4B0D8 therefore
     * cannot fail on this path, unlike the panic AI's 0x4AE08 and this tick's 0x4D844. */
    (void)TactState_EnqueueFireBurst(pCtx->pQueue, &spec, EnemyAi_FaceTowardQueue, pCtx->pQueue);
    pTact->bAiFocusSaved = 1;
    return AI_PATH_TAIL;
}

/* LABEL_82, 0x4d8b8..0x4d918: turn, or walk to an RMP node. */
static AiActPath EnemyAi_FallbackMove(Soldier *pUnit, TactCtx272 *pTact, const EnemyAiTickCtx *pCtx)
{
    EnemyAiHooks hooks;
    const RmpData *pRmp = pCtx->pRmp;
    i32 nCount;
    i32 nIndex;
    i32 nFails = 0;
    i32 nScanned = 0;
    i32 nRoll;

    if (pUnit->bRankType == PUFO_ENEMYAI_RANK_LEADER || pUnit->bRankType == PUFO_ENEMYAI_RANK_COMMANDER) {
        nRoll = ((i32)pCtx->pfnRandU16(PUFO_ENEMYAI_RANK_ROLL, pCtx->pUser) == PUFO_ENEMYAI_RANK_KEEP)
              ? 1 : 0;
    } else {
        nRoll = (i32)pCtx->pfnRandU16(PUFO_ENEMYAI_MOVE_ROLL, pCtx->pUser);
    }

    if (nRoll == 0) {
        memset(&hooks, 0, sizeof(hooks));
        hooks.pfnRandU16             = pCtx->pfnRandU16;
        hooks.pfnEnqueueTurnAndKick  = pCtx->pfnEnqueueTurnAndKick;
        hooks.pUser                  = pCtx->pUser;
        EnemyAi_EnqueueRandomTurn(pUnit, &hooks);
        pUnit->bAiDone = 1;
        pTact->bAiFocusSaved = 1;
        return AI_PATH_TAIL;
    }

    nCount = (i32)pRmp->nNodeCount;
    if (nCount == 0) {
        return AI_PATH_NEXT_UNIT;
    }

    nIndex = (i32)pCtx->pfnRandU16((u32)nCount, pCtx->pUser);

    for (;;) {
        const RmpNode *pNode;

        /* 0x4D8F8: the index is advanced BEFORE it is used, so the starting draw is never
         * itself a candidate; it only decides where the walk begins. */
        if (++nIndex > nCount - 1) {
            nIndex = 0;
        }
        pNode = pRmp->apNodes[nIndex];

        /* The distance test uses the untruncated sqrt, unlike every reach test above:
         * 0x4D8E8 compares it with `_led` against 0x403E000000000000 = 19.875. */
        if (pNode != NULL
            && EnemyAi_Distance((i32)pUnit->pCell->nX, (i32)pUnit->pCell->nY,
                                (i32)pNode->nX, (i32)pNode->nY) <= PUFO_ENEMYAI_RMP_DIST_MAX
            && EnemyAi_MultiTargetRoleMatch(pUnit, pNode->bFlag19)) {
            Tact_SelectUnit(pCtx->pSelect, pUnit);
            if (pCtx->pfnPathToCell(pNode->nX, pNode->nY, pNode->nZ, 1, pCtx->pUser)) {
                pTact->bAiFocusSaved = 1;
                return AI_PATH_TAIL;
            }
            if (++nFails > PUFO_ENEMYAI_RMP_FAIL_MAX) {
                pTact->bAiFocusSaved = 1;
                pUnit->bAiDone = 1;
                return AI_PATH_TAIL;
            }
        }

        ++nScanned;
        nCount = (i32)pRmp->nNodeCount;   /* the original re-reads the count every pass */
        if (nScanned >= nCount) {
            return AI_PATH_NEXT_UNIT;
        }
    }
}

/* Everything one acting unit can do, in the original's order. */
static AiActPath EnemyAi_ActUnit(Soldier *pUnit, TactCtx272 *pTact, const EnemyAiTickCtx *pCtx)
{
    Soldier *pTarget = NULL;
    const MapCell *pTargetCell = NULL;
    i32 nEnemyCount;
    i32 i;
    AiActPath ePath;

    /* 0x4CC6C: the acting unit refreshes its own sight, and the camera link is dropped
     * unless it already tracked this unit. */
    if (pCtx->pfnUpdateVisibility != NULL) {
        pCtx->pfnUpdateVisibility(pUnit, pCtx->pUser);
    }
    if (pUnit != pTact->pTrackUnit) {
        pTact->pTrackUnit = NULL;
    }

    if (pUnit->bMorale < PUFO_ENEMYAI_MORALE_MAX
        && (i32)pUnit->nUnitType > (i32)PUFO_ENEMYAI_DROP_TYPE_MAX) {
        Tact_SelectUnit(pCtx->pSelect, pUnit);
        EnemyAi_TryFireOrWander(pUnit, pCtx->pActCtx);
        pTact->bAiFocusSaved = 1;
        pUnit->bAiDone = 1;
        return AI_PATH_TAIL;
    }

    if (pUnit->nMultiTarget > 0) {
        for (i = 0; i < PUFO_ENEMYAI_MULTI_PICK_TRIES; ++i) {
            pTarget = pUnit->multiOrAnim.apMultiTargets[EnemyAi_RandMultiIndex(pUnit, pCtx)];
            if (!Unit_bIsLarge(pTarget)) {
                break;
            }
        }
    }

    /* 0x4cd60..0x4cda0: the pick is usable when it is small, not one of the two
     * spotted-only types, and a 1-in-5 draw comes up zero. The original also re-tests
     * nEnemyUnitCount here; it cannot be 0, because the entry test at 0x4CC18 already
     * refused it and nothing between the two changes it. */
    if (pTarget != NULL && !Unit_bIsLarge(pTarget)
        && pTarget->nUnitType != PUFO_UNIT_TYPE_SPOTTED_A
        && pTarget->nUnitType != PUFO_UNIT_TYPE_SPOTTED_B
        && pCtx->pfnRandU16(PUFO_ENEMYAI_MULTI_REJECT_ROLL, pCtx->pUser) == 0) {
        pTargetCell = pTarget->pCell;

        nEnemyCount = (i32)*pCtx->pnEnemyCount;
        for (i = 0; i < nEnemyCount; ++i) {
            Soldier *pOther = pCtx->apEnemyUnits[i];

            if (pOther->bPsiActive != 0 && pOther != pUnit && pOther->uTu > PUFO_ENEMYAI_TU_MIN) {
                break;
            }
        }
        if (i < nEnemyCount) {
            EnemyAi_MeleeNeighbour(pCtx->apEnemyUnits[i], pTargetCell, pCtx);
            pTact->bAiFocusSaved = 1;
            return AI_PATH_TAIL;
        }
    }

    /* LABEL_24, 0x4cda4. */
    if (pUnit->nMultiTarget == 0) {
        return EnemyAi_FallbackMove(pUnit, pTact, pCtx);
    }

    Tact_SelectUnit(pCtx->pSelect, pUnit);
    if (pUnit->nUnitType < PUFO_UNIT_TYPE_COUNT && g_aUnitTypeFlag3[pUnit->nUnitType] != 0) {
        ePath = EnemyAi_MeleeWalk(pUnit, pTact, pCtx);
    } else {
        ePath = EnemyAi_ThrowOrFire(pUnit, pTact, pCtx);
    }
    if (ePath == AI_PATH_TAIL) {
        return AI_PATH_TAIL;
    }
    return EnemyAi_FallbackMove(pUnit, pTact, pCtx);
}

void Tact_EnemyAiTick(TactCtx272 *pTact, const EnemyAiTickCtx *pCtx)
{
    i32 nCount = (i32)*pCtx->pnEnemyCount;

    /* 0x4CC00: the focus byte at +0x100 is cleared on entry and is what the tail reads. */
    pTact->bAiFocusSaved = 0;

    if (nCount == 0) {
        goto TAIL;
    }

    for (i32 i = 0; i < nCount; ++i) {
        Soldier *pUnit = pCtx->apEnemyUnits[i];

        /* Deviation for a latent original fault: an Xcom-base reinforcement unit that
         * Map_PickRmpSpawnForUnit @ 0x55418 could not place (its rank gate matches no shipped
         * .rmp node) stays in apEnemyUnits with no cell. The original would reach it here and
         * fault inside Unit_UpdateVisibility @ 0x48580, which reads its cell first. The port
         * parks such a unit as done: it never acts, and the turn still closes. */
        if (pUnit->pCell == NULL) {
            pUnit->bAiDone = 1;
            continue;
        }
        /* 0x4CC34: past the tick limit a unit is skipped WITHOUT being advanced, so it
         * stays skipped for the rest of the mission. */
        if (pUnit->nAiTick > PUFO_ENEMYAI_TICK_MAX) {
            continue;
        }
        ++pUnit->nAiTick;
        if (pUnit->bAiDone != 0) {
            continue;
        }
        if (pUnit->uTu >= PUFO_ENEMYAI_TU_MIN) {
            if (EnemyAi_ActUnit(pUnit, pTact, pCtx) == AI_PATH_TAIL) {
                goto TAIL;
            }
        } else {
            pUnit->bAiDone = 1;
        }
    }

TAIL:
    /* 0x4D920. */
    pTact->nPhase = PUFO_PHASE_ENEMY_AI;
    if (pTact->bAiFocusSaved == 0) {
        Tact_EndEnemyTurn(pTact, pCtx->pTurnHooks, pCtx->pFirstXcom,
                          pCtx->pbEnemyTurnActive, pCtx->pfnAfterEnemyStep);
    }
    TactState_KickQueue(pCtx->pQueue);
}
