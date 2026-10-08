/* pufo_tactstate.c - the tactical action queue.
 *
 * Evidence: see pufo_tactstate.h for the full address list.
 */
#include "game/pufo_tactstate.h"
#include "game/pufo_path.h"   /* g_aStepDir for Map_QueueDoorAction */

#include <string.h>

/* unk_157198 @ 0x157198. */
TactState *g_pTactState;

void TactState_ctor(TactState *pState)
{
    memset(pState, 0, sizeof(*pState));
    pState->nQueueLen    = 0;
    pState->bQueueActive = 0;
    pState->bUnk70103    = 0;
}

void TactState_EnqueueTurn(TactState *pState, Soldier *pUnit, u8 nFacing)
{
    TactQueueEntry *pEntry = &pState->aQueue[pState->nQueueLen];

    pEntry->nCmd    = TACTCMD_TURN;
    pEntry->pUnit   = pUnit;
    pEntry->nFacing = nFacing;
    pState->nQueueLen += 1;
}

void TactState_EnqueueStep(TactState *pState, Soldier *pUnit, u8 nFacing, i32 nDelta)
{
    TactQueueEntry *pEntry = &pState->aQueue[pState->nQueueLen];

    pEntry->nCmd    = TACTCMD_STEP;
    pEntry->pUnit   = pUnit;
    pEntry->nFacing = nFacing;
    pEntry->nDelta  = nDelta;
    pState->nQueueLen += 1;
}

void TactState_EnqueueStandFromKneel(TactState *pState, Soldier *pUnit)
{
    TactQueueEntry *pEntry = &pState->aQueue[pState->nQueueLen];

    pEntry->nCmd  = TACTCMD_STAND_FROM_KNEEL;
    pEntry->pUnit = pUnit;
    pState->nQueueLen += 1;
}

/* TactState_EnqueueVertStep @ 0x49ddc. A kneeling unit stands first, which is
 * why this can emit two entries. */
void TactState_EnqueueVertStep(TactState *pState, Soldier *pUnit, u8 nFacing)
{
    TactQueueEntry *pEntry;

    if ((pUnit->bFlags & 1) != 0) {
        TactState_EnqueueStandFromKneel(pState, pUnit);
    }
    pEntry = &pState->aQueue[pState->nQueueLen];
    pEntry->nCmd    = TACTCMD_VERT_STEP;
    pEntry->nFacing = nFacing;
    pEntry->pUnit   = pUnit;
    pState->nQueueLen += 1;
}

void TactState_EnqueueRecover(TactState *pState, Soldier *pUnit)
{
    TactQueueEntry *pEntry = &pState->aQueue[pState->nQueueLen];

    pEntry->nCmd  = TACTCMD_RECOVER;
    pEntry->pUnit = pUnit;
    pState->nQueueLen += 1;
}

void TactState_EnqueueClearAim(TactState *pState, Soldier *pUnit)
{
    TactQueueEntry *pEntry = &pState->aQueue[pState->nQueueLen];

    pEntry->nCmd  = TACTCMD_CLEAR_AIM;
    pEntry->pUnit = pUnit;
    pState->nQueueLen += 1;
}

void TactState_EnqueueEndMark(TactState *pState)
{
    pState->aQueue[pState->nQueueLen].nCmd = TACTCMD_END_MARK;
    pState->nQueueLen += 1;
}

/* TactState_EnqueueDeathFlash @ 0x4a29c.
 *
 * Disassembly, step by step, so the two paths are unambiguous:
 *     LDRB  R3, [R2,#0x111D6]   ; bQueueActive
 *     CMP   R3, #0 / BEQ  append
 *     LDRH  R3, [R2,#0x111D4]   ; nQueueLen
 *     LDR   R5, [R2,#0x10]      ; nQueueCursor
 *     SUB   R11, R3, #1 / CMP R11, R5 / BEQ append   ; last entry -> append
 *     ADD   R11, R5, #1 / CMP R3, R11 / BLE  no_shift
 *     shift: copy entry[i-1] -> entry[i], i from nQueueLen down to cursor+2
 *     no_shift: insertIndex = cursor + 1
 *     write cmd/p unit/arg at insertIndex, then ++nQueueLen
 *
 * So the flash is inserted directly after the entry being executed, which is why
 * a death animation interrupts the current action instead of queueing behind
 * everything else. Note the shift copies whole 56-byte entries upward, from the
 * end backwards, so it is safe to do in place. */
void TactState_EnqueueDeathFlash(TactState *pState, Soldier *pUnit, u8 nArg)
{
    u16 nInsert;

    if (pState->bQueueActive == 0
        || (u16)(pState->nQueueLen - 1u) == (u16)pState->nQueueCursor) {
        nInsert = pState->nQueueLen;
    } else {
        nInsert = (u16)(pState->nQueueCursor + 1);
        if (pState->nQueueLen > (u16)(pState->nQueueCursor + 1)) {
            i32 i;

            for (i = pState->nQueueLen; i > pState->nQueueCursor + 1; --i) {
                pState->aQueue[i] = pState->aQueue[i - 1];
            }
        }
    }

    pState->aQueue[nInsert].nCmd  = TACTCMD_DEATH_FLASH;
    pState->aQueue[nInsert].pUnit = pUnit;
    pState->aQueue[nInsert].nArg  = nArg;
    pState->nQueueLen += 1;
}

/* TactState_KickQueue @ 0x4f334. Starting an empty queue is a no-op: the
 * original only arms the cursor and flags when there is something to run. */
void TactState_KickQueue(TactState *pState)
{
    if (pState->nQueueLen != 0) {
        pState->nQueueCursor = 0;
        pState->bQueueActive = 1;
        pState->bFlag0       = 0;
        pState->bFlag1       = 0;
    }
}

/* Unit_EnqueueIfApSpent @ 0x4a3d4.
 *
 * The uTuSpent >= uHp test looks like a category error -- time units compared
 * against hit points -- but it is what the original does, and it is the rule that
 * decides when a unit is done. Kept literally. */
void Unit_EnqueueIfApSpent(TactState *pState, Soldier *pUnit, u8 nArg)
{
    if (pUnit->uTuSpent < (u32)pUnit->uHp) {
        return;
    }
    if ((pUnit->bFlags & 0x20) != 0) {
        return;
    }
    /* A large unit with HP left survives; anything else flashes and dies. */
    if (!Unit_bIsLarge(pUnit) || pUnit->uHp == 0) {
        TactState_EnqueueDeathFlash(pState, pUnit, nArg);
    }
    if (pState->bQueueActive == 0) {
        TactState_KickQueue(pState);
    }
}

/* GameTick_DecayCellFlags @ 0x27324.
 *
 * The original's expression is:
 *     new = ((((u8)((v & 0xF8) - 1)) ^ (u8)v) & 0x78) ^ v
 * That is an obfuscated way of writing "decrement the 4-bit field at bits 3..6".
 * Checked exhaustively for v in 0..0x1FF: it is equivalent to
 *     (v & ~0x78) | ((((v >> 3) & 0xF) - 1) << 3)
 * for every v with the field non-zero, so the plain form is used here. The field
 * wraps rather than saturating: 0x1 -> 0x0 and 0x0 is never decremented because
 * the caller skips cells whose field is already zero.
 * The port keeps this as one function so the arithmetic is testable directly. */
u32 GameTick_DecayCellFlags(u32 nFlags32)
{
    u32 nField = (nFlags32 >> 3) & 0x0Fu;

    nField = (nField - 1u) & 0x0Fu;
    return (nFlags32 & ~0x78u) | (nField << 3);
}

/* GameTick_Step @ 0x27324.
 *
 * Step 2 is the animated-tile decay: every cell on every layer has its bits 3..6
 * field decremented, but only when that field is non-zero. Cells whose field is
 * already zero are skipped, which is what keeps the 4-bit field from wrapping to
 * 0xF in place. */
bool GameTick_Step(TactState *pState, MapCellLookup pfnLookup, void *pUser)
{
    i32 nX;
    i32 nY;
    i32 nLayer;

    if (pState->bPhaseActive == 0) {
        return false;
    }

    for (nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (nY = 0; nY < PUFO_CELL_GRID_MAX; ++nY) {
            for (nX = 0; nX < PUFO_CELL_GRID_MAX; ++nX) {
                MapCell *pCell = pfnLookup(nX, nY, nLayer, pUser);

                if (pCell == NULL) {
                    continue;
                }
                if ((pCell->nFlags32 & 0x78u) == 0) {
                    continue;
                }
                pCell->nFlags32 = GameTick_DecayCellFlags(pCell->nFlags32);
            }
        }
    }

    pState->nPhaseCounter += 1;
    if (pState->nPhaseCounter > PUFO_PHASE_TICK_MAX) {
        pState->bPhaseActive = 0;
        return true;
    }
    return false;
}

/* TactState_EnqueueFireBurst @ 0x4b080.
 *
 * Verified entry field offsets (ILP32, relative to the 56-byte entry):
 *   nCmd 0x00, pUnit 0x04, nX 0x09, nY 0x0A, nZ 0x18, pWeapon 0x1C,
 *   nArgDword 0x20, nTuCost 0x24, nMode 0x28
 * The disassembly confirms nCmd/pUnit come first (TactState 0x18/0x1C), then the
 * coordinate bytes at TactState 0x21/0x22/0x30, i.e. entry 0x09/0x0A/0x18.
 */
bool TactState_EnqueueFireBurst(TactState *pState, const FireBurstSpec *pSpec,
                                void (*pfnFaceToward)(Soldier *pUnit, i32 nX, i32 nY,
                                                      void *pUser),
                                void *pUser)
{
    i32 nShots;

    if (pState == NULL || pSpec == NULL || pSpec->pUnit == NULL) {
        return false;
    }

    /* Step 1: the TU check happens BEFORE facing, and its comparison is strict, so
     * uTu == nTuCost is allowed. No queue entry is written on failure. */
    if ((i32)pSpec->pUnit->uTu < pSpec->nTuCost) {
        return false;
    }

    /* Step 2: face the target. */
    if (pfnFaceToward != NULL) {
        pfnFaceToward(pSpec->pUnit, pSpec->nX, pSpec->nY, pUser);
    }

    /* Step 3: the shot count. Mode 7 (multi-target) uses the target list length;
     * mode 0 (auto) fires three; every other mode fires one. */
    if (pSpec->nMode == 7) {
        nShots = pSpec->nMultiTargetCount;
    } else {
        nShots = (pSpec->nMode == 0) ? 3 : 1;
        /* Outside mode 7 the count is capped by the loaded ammo, unless the weapon
         * is unlimited (capacity byte 255). */
        if (pSpec->bAmmoCap != 255 && (i32)pSpec->uAmmoInWeapon < nShots) {
            nShots = (i32)pSpec->uAmmoInWeapon;
        }
    }

    /* Two entries per shot, so guard the capacity up front for both. The original
     * has no guard because its queue is sized well above any reachable burst. */
    if ((i32)pState->nQueueLen + 2 * nShots + 1 > PUFO_TACT_QUEUE_MAX) {
        return false;
    }

    for (i32 i = 0; i < nShots; ++i) {
        TactQueueEntry *pShot;
        u8 nX;
        u8 nY;
        u8 nZ;

        /* Step 4a: the shot entry. */
        pShot = &pState->aQueue[pState->nQueueLen];
        pShot->nCmd  = (u8)TACTCMD_FIRE_SHOT;
        pShot->pUnit = pSpec->pUnit;

        /* Coordinates: mode 7 takes them from apMultiTargets[i]'s cell, every other
         * mode from the single target argument. */
        if (pSpec->nMode == 7 && pSpec->apMultiTargets != NULL
            && pSpec->apMultiTargets[i] != NULL) {
            nX = pSpec->apMultiTargets[i]->nX;
            nY = pSpec->apMultiTargets[i]->nY;
            nZ = pSpec->apMultiTargets[i]->nZ;
        } else {
            nX = (u8)pSpec->nX;
            nY = (u8)pSpec->nY;
            nZ = pSpec->nZ;
        }
        pShot->nX = nX;
        pShot->nY = nY;
        pShot->nZ = nZ;

        pShot->pWeapon   = pSpec->pWeapon;
        pShot->nArgDword = pSpec->nArg;
        /* Only the FIRST shot is charged TU; the rest carry 0. */
        pShot->nTuCost   = (i == 0) ? pSpec->nTuCost : 0;
        pState->nQueueLen += 1;

        /* Step 4b: the mark entry that pairs with it. */
        pShot = &pState->aQueue[pState->nQueueLen];
        pShot->nCmd  = (u8)TACTCMD_FIRE_MARK;
        pShot->pUnit = pSpec->pUnit;
        pState->nQueueLen += 1;
    }

    /* Step 5: clear the aim afterwards. */
    TactState_EnqueueClearAim(pState, pSpec->pUnit);
    return true;
}

/* TactState_EnqueueThrow @ 0x4b2f4. */
bool TactState_EnqueueThrow(TactState *pState, Soldier *pUnit, TactItem *pItem,
                            u8 nX, u8 nY, u8 nZ, i32 nTuCost, i32 nArg,
                            void (*pfnFaceToward)(Soldier *pUnit, i32 nX, i32 nY,
                                                  void *pUser),
                            void *pUser)
{
    TactQueueEntry *pEntry;

    if (pState == NULL || pUnit == NULL) {
        return false;
    }
    if ((i32)pUnit->uTu < nTuCost) {
        return false;
    }
    if (pfnFaceToward != NULL) {
        pfnFaceToward(pUnit, nX, nY, pUser);
    }
    if (pState->nQueueLen >= PUFO_TACT_QUEUE_MAX) {
        return false;
    }
    pEntry = &pState->aQueue[pState->nQueueLen];
    pEntry->nCmd    = (u8)TACTCMD_THROW;
    pEntry->pUnit   = pUnit;
    pEntry->nX      = nX;
    pEntry->nY      = nY;
    pEntry->nZ      = nZ;
    pEntry->pWeapon = pItem;
    pEntry->nArg    = (u8)nArg;
    pEntry->nTuCost = nTuCost;
    pState->nQueueLen += 1;
    return true;
}

/* Tact_EnqueueActionOrStatus205 @ 0x4b420. */
bool TactState_EnqueueAction18(TactState *pState, Soldier *pUnit, Soldier *pTarget,
                               TactItem *pItem, i32 nTuCost, i32 nMode)
{
    TactQueueEntry *pEntry;

    if (pState == NULL || pUnit == NULL) {
        return false;
    }
    if ((i32)pUnit->uTu < nTuCost) {
        return false;
    }
    if (pState->nQueueLen >= PUFO_TACT_QUEUE_MAX) {
        return false;
    }
    pEntry = &pState->aQueue[pState->nQueueLen];
    pEntry->nCmd    = (u8)TACTCMD_ACTION18;
    pEntry->nMode   = nMode;
    pEntry->pTarget = pTarget;
    pEntry->nTuCost = nTuCost;
    pEntry->pWeapon = pItem;
    pEntry->pUnit   = pUnit;
    pState->nQueueLen += 1;
    return true;
}

/* Append a command-2 open entry (unit only). Returns false when the queue is full. */
static bool TactQueue_AppendOpen(TactState *pState, Soldier *pUnit)
{
    TactQueueEntry *pEntry;

    if (pState->nQueueLen >= PUFO_TACT_QUEUE_MAX) {
        return false;
    }
    pEntry = &pState->aQueue[pState->nQueueLen];
    pEntry->nCmd = (u8)TACTCMD_OPEN;
    pEntry->pUnit = pUnit;
    pState->nQueueLen += 1;
    return true;
}

/* Map_QueueDoorAction @ 0x2bd00. */
bool Map_QueueDoorAction(TactState *pState, MapCell *pCellA, MapCell *pCellB,
                         i32 nDir, const DoorActionCtx *pCtx)
{
    MapCell *pUse = pCellA;
    Mc1Rec *pWall;
    i32 nMode;
    i32 nFacing;
    i32 nX;
    i32 nY;
    i32 nZ;

    if (pState == NULL || pCellA == NULL || pCellB == NULL || pCtx == NULL
        || pCtx->pfnLookup == NULL) {
        return false;
    }

    if (nDir == 0) {
        nMode = 1;
    } else if (nDir == 2) {
        if (!pCtx->bSelUnitLarge) {
            if (pCellB->nZ == pCellA->nZ) {
                pUse = pCellB;
            } else {
                pUse = pCtx->pfnLookup(pCellB->nX, pCellB->nY, pCellA->nZ, pCtx->pUser);
            }
            nMode = 2;
        } else {
            pUse = pCtx->pfnLookup(pCellB->nX + 1, pCellB->nY, pCellB->nZ, pCtx->pUser);
            nMode = 2;
        }
    } else if (nDir == 4) {
        pUse = pCtx->bSelUnitLarge
            ? pCtx->pfnLookup(pCellB->nX, pCellB->nY, pCellB->nZ, pCtx->pUser)
            : pCellB;
        nMode = 1;
    } else if (nDir == 6) {
        if (pCellB->nZ == pCellA->nZ) {
            pUse = pCellA;
        } else {
            pUse = pCtx->pfnLookup(pCellA->nX, pCellA->nY, pCellB->nZ, pCtx->pUser);
        }
        nMode = 2;
    } else {
        return false;
    }
    if (pUse == NULL) {
        return false;
    }

    nX = pUse->nX;
    nY = pUse->nY;
    nZ = pUse->nZ;
    if (nMode == 2) {
        pWall = pUse->pWallSprA;
        nFacing = 4;
    } else {
        pWall = pUse->pWallSprB;
        nFacing = 2;
    }
    if (pWall == NULL) {
        return false;
    }

    if (pWall->gap_2f[0] != 0) {
        TactQueueEntry *pEntry;

        if (!TactQueue_AppendOpen(pState, pCtx->pSelUnit)
            || pState->nQueueLen >= PUFO_TACT_QUEUE_MAX) {
            return false;
        }
        pEntry = &pState->aQueue[pState->nQueueLen];
        pEntry->nCmd = (u8)TACTCMD_DOOR;
        pEntry->pUnit = pCtx->pSelUnit;
        pEntry->nFacing = (u8)nFacing;
        pEntry->nX = (u8)nX;
        pEntry->nY = (u8)nY;
        pEntry->nZ = (u8)nZ;
        pState->nQueueLen += 1;
        return true;
    }

    if (pWall->bBlocksMove != 0) {
        MapCell *pTargetCell = pCtx->pfnLookup(nX, nY, nZ, pCtx->pUser);

        if (pTargetCell != NULL && (pTargetCell->bFlags29 & 4) == 0) {
            const StepDir *pStep = &g_aStepDir[nFacing];
            i32 nBackStep = 0;
            i32 nX2;
            i32 nY2;
            i32 nForward = 0;

            /* Back up to 3 cells to find the near edge of the door span. */
            for (i32 iter = 0; iter < 3; ++iter) {
                MapCell *pCell;
                Mc1Rec *pCellWall;

                if (!Map_IsValidCell(&pCtx->geom, nX + pStep->dx * nBackStep,
                                     nY + pStep->dy * nBackStep, 0)) {
                    break;
                }
                pCell = pCtx->pfnLookup(nX + pStep->dx * nBackStep,
                                        nY + pStep->dy * nBackStep, nZ, pCtx->pUser);
                pCellWall = (nMode == 2) ? pCell->pWallSprA : pCell->pWallSprB;
                if (pCellWall == NULL || pCellWall->bBlocksMove == 0) {
                    break;
                }
                nBackStep -= 1;
            }
            nX2 = pStep->dx * (nBackStep + 1) + nX;
            nY2 = pStep->dy * (nBackStep + 1) + nY;
            /* Step forward up to 7 cells to the far edge. */
            for (i32 iter = 0; iter < 7; ++iter) {
                MapCell *pCell;
                Mc1Rec *pCellWall;

                if (!Map_IsValidCell(&pCtx->geom, nX2 + pStep->dx * nForward,
                                     nY2 + pStep->dy * nForward, 0)) {
                    break;
                }
                pCell = pCtx->pfnLookup(nX2 + pStep->dx * nForward,
                                        nY2 + pStep->dy * nForward, nZ, pCtx->pUser);
                pCellWall = (nMode == 2) ? pCell->pWallSprA : pCell->pWallSprB;
                if (pCellWall == NULL || pCellWall->bBlocksMove == 0) {
                    break;
                }
                nForward += 1;
            }
            if (!TactQueue_AppendOpen(pState, pCtx->pSelUnit)
                || pState->nQueueLen >= PUFO_TACT_QUEUE_MAX) {
                return false;
            }
            pState->aQueue[pState->nQueueLen].nCmd = (u8)TACTCMD_STEP_DOOR;
            pState->aQueue[pState->nQueueLen].pUnit = pCtx->pSelUnit;
            pState->aQueue[pState->nQueueLen].nTurnDir = (i8)nForward;
            pState->aQueue[pState->nQueueLen].nX = (u8)nX2;
            pState->aQueue[pState->nQueueLen].nY = (u8)nY2;
            pState->aQueue[pState->nQueueLen].nZ = (u8)nZ;
            pState->aQueue[pState->nQueueLen].nFacing = (u8)nFacing;
            pState->nQueueLen += 1;
            return true;
        }
    }
    return false;
}
