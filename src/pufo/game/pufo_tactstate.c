/* pufo_tactstate.c - the tactical action queue.
 *
 * Evidence: see pufo_tactstate.h for the full address list.
 */
#include "game/pufo_tactstate.h"
#include "game/pufo_path.h"   /* g_aStepDir, Map_FacingDelta, Map_bFacingTurnPositive */
#include "audio/pufo_audio.h" /* Sound_Play */
#include "game/pufo_turn.h"    /* Tact_RefreshTeamVisibility */
#include "game/pufo_combat.h"  /* g_aFireSoundBySortKey */

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

/* Unit_FaceToward @ 0x49bf0. atan2 from the unit's own cell toward (nX,nY),
 * wrapped to 0..7 by Unit_FacingToward, then the turn command is appended.
 * The original's guard is `nX != pCell->nX || nY != pCell->nY`; the same-cell
 * case enqueues nothing. */
void Unit_FaceToward(TactState *pState, Soldier *pUnit, i32 nX, i32 nY)
{
    const MapCell *pCell;

    /* Deviation: the original dereferences pUnit and pUnit->pCell unconditionally. */
    if (pState == NULL || pUnit == NULL || pUnit->pCell == NULL) {
        return;
    }
    pCell = pUnit->pCell;
    if (nX == pCell->nX && nY == pCell->nY) {
        return;
    }
    TactState_EnqueueTurn(pState, pUnit, Unit_FacingToward(pCell->nX, pCell->nY, nX, nY));
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

/* =========================================================================
 * Tact_ProcessMoveAction @ 0x4f3f0
 *
 * Executes the queue entry at nQueueCursor. 19 commands (0..0x12); 6, 9, 12 and
 * 13 fall through the switch default and do nothing. Everything below is read
 * off the ARM, not the decompiler: the pseudocode gets the structure right but
 * it hides the context offsets behind MEMORY[0x1139B8] and it gets two signs
 * wrong, the target-cell ceiling term and the civilian roster index.
 *
 * Register contract, from the prologue at 0x4F3F0:
 *   R4 = pState, R7 = entry->pUnit, R8 = entry->nTuCost, R9 = 56,
 *   R10 = entry->nDelta, R5 = entry->nFacing read SIGNED (LDRSB),
 *   R6 = 1 and R11 = 0 for the whole function.
 * The compiler folds the queue base into the index, so an ARM displacement of
 * 0x18 + n addresses entry field n.
 *
 * Deviations from the original, all deliberate:
 *   - The original indexes its cell array as one flat dword table at
 *     pCtxBase + 0x15338, 3600 dwords per layer. The port goes through
 *     MapCellLookup, which resolves the same cells.
 *   - The original dereferences every cell slot unconditionally. The port skips
 *     a NULL the injected lookup may return.
 *   - The original's Sound_Play takes a mode in R1, 2 for the sprite-selected
 *     footstep samples and 4 for the fixed ones. The port's Sound_Play keeps the
 *     one-argument shape the rest of the port settled on, so the mode is lost.
 *   - The nStepZDelta test at 0x4F8DC compares against a leftover register
 *     instead of -1. The port implements the intended nDelta == -1.
 * ========================================================================= */

static MapCell *MoveCell(const MoveActionCtx *pCtx, i32 nX, i32 nY, i32 nZ)
{
    return pCtx->pfnLookup(nX, nY, nZ, pCtx->pUser);
}

/* LABEL_187: mark the entry aborted, then rebuild team visibility. */
static void MoveAbort(TactState *pState, const MoveActionCtx *pCtx)
{
    pState->aQueue[pState->nQueueCursor].nCmd = 13;
    Tact_RefreshTeamVisibility(pState, pCtx->apXcomUnits, (i32)*pCtx->pnXcomCount,
                               pCtx->pfnUpdateVisibility, pCtx->pCallbackUser);
}

/* LABEL_4 then LABEL_5: push one text record, then abort. */
static void MoveAbortMsg(TactState *pState, const MoveActionCtx *pCtx, i32 nMsg)
{
    pCtx->pfnPushStatus(nMsg, pCtx->pCallbackUser);
    MoveAbort(pState, pCtx);
}

/* LABEL_98 and LABEL_124: the "not enough time" record, suppressed while
 * bUnk70114 is set. Cases 1, 3, 4, 5, 7 and 0xE land here; cases 0x11 and 0x12
 * use LABEL_3, which pushes unconditionally. */
static void MoveAbortNoTime(TactState *pState, const MoveActionCtx *pCtx)
{
    if (pState->bUnk70114 == 0) {
        pCtx->pfnPushStatus(MOVE_MSG_NO_TIME, pCtx->pCallbackUser);
    }
    MoveAbort(pState, pCtx);
}

/* The footstep sample for a unit type, 0x4F94C..0x4FA18. Unit types 15 and 19
 * are silent. For the twelve sprite-selected types the sample comes from byte
 * 0x1D of the unit's cell ceiling sprite, or of its floor sprite when the cell
 * has no ceiling; a cell with no floor sprite at all gets sample 33. */
static void MovePlayStepSound(const Soldier *pUnit)
{
    switch (pUnit->nUnitType) {
    case 0: case 1: case 2: case 3: case 9: case 10:
    case 14: case 16: case 17: case 20: case 21: case 22: {
        const MapCell *pCell = pUnit->pCell;
        const Mc1Rec  *pSpr;
        if (pCell->pFloorSpr == NULL) { Sound_Play(33); return; }
        pSpr = (pCell->pCeilSpr != NULL) ? pCell->pCeilSpr : pCell->pFloorSpr;
        Sound_Play(pSpr->bCeilSoundSel == 1 ? 32 : 19);
        return;
    }
    case 4: case 5: case 6:  Sound_Play(31); return;
    case 7: case 8: case 18: Sound_Play(30); return;
    case 11: case 13:        Sound_Play(33); return;
    case 12:                 Sound_Play(34); return;
    default:                 return;
    }
}

/* cmd 0, turn in place. 0x4FA2C..0x4FA8C. */
static void MoveTurn(TactState *pState, const MoveActionCtx *pCtx,
                     TactQueueEntry *pEntry, Soldier *pUnit, i32 nFacing)
{
    pState->nPhaseTick = 0;
    pState->nPhaseMax = Map_FacingDelta(pUnit->bFacing, (u8)nFacing);
    if (pUnit->uTu == 0) {
        MoveAbortNoTime(pState, pCtx);
        return;
    }
    pEntry->nTurnDir = Map_bFacingTurnPositive(pUnit->bFacing, (u8)nFacing) ? 1 : -1;
}

/* cmd 1, step one cell. 0x4F5D0..0x4F940. */
static void MoveStep(TactState *pState, const MoveActionCtx *pCtx,
                     TactQueueEntry *pEntry, Soldier *pUnit, i32 nDelta)
{
    static const i8 k_aStepOff[8][2] = {
        { 2, -1}, { 4, 0}, { 2, 1}, { 0, 2},
        {-2,  1}, {-4, 0}, {-2,-1}, { 0,-2}
    };
    MapCell *pFrom, *pTo;

    if ((pUnit->bFlags & 0x20) != 0) { MoveAbort(pState, pCtx); return; }
    if (pUnit->uTu < 3) { MoveAbortNoTime(pState, pCtx); return; }
    if ((i32)pUnit->uStunOrArmor
        < 4 * (i32)pUnit->uInvBurden / (i32)pUnit->auStat[8] + 2) {
        MoveAbortMsg(pState, pCtx, MOVE_MSG_ARMOUR_BLOCK);
        return;
    }
    if (pCtx->pfnArmorTuWarn(pUnit, 4, pCtx->pCallbackUser)) {
        MoveAbort(pState, pCtx);
        return;
    }

    /* 0x4F63C..0x4F698: the destination is the neighbour in the entry's facing,
     * on the layer nZ + nDelta. */
    pFrom = pUnit->pCell;
    pState->pStepFromCell = pFrom;
    pTo = MoveCell(pCtx, (i32)pFrom->nX + g_aStepDir[pEntry->nFacing].dx,
                   (i32)pFrom->nY + g_aStepDir[pEntry->nFacing].dy,
                   (i32)pFrom->nZ + nDelta);
    if (pTo == NULL) { MoveAbort(pState, pCtx); return; }

    if (Unit_bIsLarge(pUnit)) {
        /* 0x4F718..0x4F794: a large unit needs the whole 2x2 footprint whose
         * near corner is the destination, grid deltas +0, +1, +60 and +61. */
        MapCell *pB = MoveCell(pCtx, pTo->nX + 1, pTo->nY, pTo->nZ);
        MapCell *pC = MoveCell(pCtx, pTo->nX, pTo->nY + 1, pTo->nZ);
        MapCell *pD = MoveCell(pCtx, pTo->nX + 1, pTo->nY + 1, pTo->nZ);
        if (MapCell_HasOccupant(pTo) && pTo->pUnit != pUnit) {
            MoveAbort(pState, pCtx); return;
        }
        if (pB != NULL && MapCell_HasOccupant(pB) && pB->pUnit != pUnit) {
            MoveAbort(pState, pCtx); return;
        }
        if (pC != NULL && MapCell_HasOccupant(pC) && pC->pUnit != pUnit) {
            MoveAbort(pState, pCtx); return;
        }
        if (pD != NULL && MapCell_HasOccupant(pD) && pD->pUnit != pUnit) {
            MoveAbort(pState, pCtx); return;
        }
    } else if (MapCell_HasOccupant(pTo) && pTo->pUnit != pUnit) {
        MoveAbort(pState, pCtx);
        return;
    }

    *pCtx->pnLayerScratch = (i8)(pUnit->pCell->nZ + 1);
    pState->nPhaseTick = 0;
    *pCtx->pnAnimPhase = 1;

    if (pUnit->bFacing <= 7) {
        pState->nStepDrawOffX = k_aStepOff[pUnit->bFacing][0];
        pState->nStepDrawOffY = k_aStepOff[pUnit->bFacing][1];
    }
    pUnit->nDrawOffX = (i8)pState->nStepDrawOffX;
    pUnit->nDrawOffY = (i8)pState->nStepDrawOffY;
    pUnit->bFlags |= 2u;

    /* The step height is the negated signed ceiling byte of each cell. */
    pState->nStepHeightSrc = (pFrom->pCeilSpr != NULL)
                                 ? -(i32)pFrom->pCeilSpr->iCeilParam : 0;
    pState->nStepHeightDst = (pTo->pCeilSpr != NULL)
                                 ? -(i32)pTo->pCeilSpr->iCeilParam : 0;
    if (nDelta == 1) {
        pState->nStepHeightDst = 40;
    } else if (nDelta == -1) {
        pState->nStepHeightDst -= 40;
    }
    pState->nStepFrameDiv = (pState->nStepHeightDst - pState->nStepHeightSrc) / 8;

    Unit_UpdateAnimPose(pUnit, NULL, NULL);

    /* 0x4F918..0x4F944: the footstep is skipped only when something is already
     * playing AND nothing is tracked AND the phase is not active. */
    if (!pCtx->bAnimBusy || pCtx->pTrackUnit != NULL || pState->bPhaseActive != 0) {
        MovePlayStepSound(pUnit);
    }
}

/* cmd 4, sweep the turn arc and flag the cells it passes. 0x4FC38..0x4FDA0.
 * The loop count is the entry's nTurnDir, which is the number of facings to
 * sweep; Map_QueueDoorAction is what stores it. */
static void MoveTurnTrace(TactState *pState, const MoveActionCtx *pCtx,
                          TactQueueEntry *pEntry, Soldier *pUnit, i32 nFacing)
{
    if (pUnit->uTu < 4) { MoveAbortNoTime(pState, pCtx); return; }
    if (pCtx->pfnArmorTuWarn(pUnit, 4, pCtx->pCallbackUser)) {
        MoveAbort(pState, pCtx);
        return;
    }

    if ((i8)pEntry->nTurnDir > 0) {
        const StepDir *pDir = &g_aStepDir[pEntry->nFacing];
        for (i32 nStep = 0; nStep < (i8)pEntry->nTurnDir; ++nStep) {
            pState->nTraceX = pDir->dx * nStep + pEntry->nX;
            pState->nTraceY = pDir->dy * nStep + pEntry->nY;
            pState->nTraceZ = pEntry->nZ;
            MapCell *pCell = MoveCell(pCtx, pState->nTraceX, pState->nTraceY,
                                      pState->nTraceZ);
            if (pCell == NULL) { continue; }
            pCell->bFlags29 |= 1u;
            if (nFacing == 2) { pCell->bFlags29 |= 2u; }
            else              { pCell->bFlags29 &= (u8)~0x02; }
        }
    }

    pState->nPhaseTick = 0;
    *pCtx->pnAnimPhase = 0;
    Sound_Play(27);
}

/* cmd 5, the vertical step. 0x4FAE4..0x4FBF4. Here the entry's signed nFacing is
 * the direction: -1 takes the cell one layer BELOW the unit, +1 takes the unit's
 * own cell and raises the layer clamp, anything else uses the cell pointer the
 * entry carries in its pWeapon slot. */
static void MoveVert(TactState *pState, const MoveActionCtx *pCtx,
                     TactQueueEntry *pEntry, Soldier *pUnit, i32 nFacing)
{
    if (pUnit->uTu < 8) { MoveAbortNoTime(pState, pCtx); return; }
    if (pCtx->pfnArmorTuWarn(pUnit, 8, pCtx->pCallbackUser)) {
        MoveAbort(pState, pCtx);
        return;
    }

    MapCell *pCell;
    i32 nHeight = 24;

    if (nFacing == -1) {
        pCell = MoveCell(pCtx, pUnit->pCell->nX, pUnit->pCell->nY,
                         (i32)pUnit->pCell->nZ - 1);
        *pCtx->pnLayerScratch = (i8)(pUnit->pCell->nZ + 1);
    } else if (nFacing == 1) {
        pCell = pUnit->pCell;
        pUnit->bFlags |= 0x10u;
        Unit_UpdateAnimPose(pUnit, NULL, NULL);
        if (*pCtx->pnLayerScratch - 1 < (i32)pUnit->pCell->nZ + 1) {
            *pCtx->pnLayerScratch = (i8)(pUnit->pCell->nZ + 2);
        }
    } else {
        pCell = (MapCell *)pEntry->pWeapon;
    }
    if (pCell == NULL) { MoveAbort(pState, pCtx); return; }

    if (pCell->pCeilSpr != NULL) { nHeight = (i32)pCell->pCeilSpr->iCeilParam + 24; }

    /* 0x4FBD8: nPhaseMax = (16 * height) / 32, rounding a negative up by 31. */
    i32 nScaled = 16 * nHeight;
    if (nScaled < 0) { nScaled += 31; }
    pState->nPhaseMax = nScaled >> 5;
    pState->nPhaseTick = 0;
}

/* cmd 0xB, pick a fallen unit back up. 0x5098C..0x50AD8. */
static void MoveRecover(const MoveActionCtx *pCtx, Soldier *pUnit)
{
    MapCell  *pCell = pUnit->pCell;
    TactItem *pItem = pCell->pItemList;

    while (pItem != NULL) {
        if (pItem->pLinkedSoldier == pUnit) {
            MapCell_ListRemove(pCell, pItem);
            break;
        }
        pItem = pItem->pNext;
    }

    if (pUnit->nUnitType > 3) {
        pUnit->bStatus = 1;
        pCtx->apEnemyUnits[(i32)(*pCtx->pnEnemyCount)++] = pUnit;
    } else {
        pUnit->bStatus = 0;
        pCtx->apXcomUnits[(i32)(*pCtx->pnXcomCount)++] = pUnit;
    }

    /* 0x50A44..0x50A90: drop the unit from the civilian roster by copying the
     * last entry over its slot and shrinking the count. Not a swap. */
    if (*pCtx->pnCivilianCount > 0) {
        i32 nIdx = 0;
        while (pCtx->apCivilians[nIdx] != pUnit) {
            ++nIdx;
            if (nIdx >= *pCtx->pnCivilianCount) { goto roster_done; }
        }
        pCtx->apCivilians[nIdx] = pCtx->apCivilians[*pCtx->pnCivilianCount - 1];
        --*pCtx->pnCivilianCount;
    }
roster_done:
    pUnit->nAnimPose = 1;
    pUnit->bFlags &= (u8)~0x20;
    MapCell_SetOccupant(pUnit->pCell, pUnit);
    /* The original always updates visibility. The port skips it when no visibility
     * context is injected, which is how a test drives the command. */
    if (pCtx->pVisCtx != NULL) { Unit_UpdateVisibility(pUnit, pCtx->pVisCtx); }
}

/* cmd 0xE, fire. 0x4FDC0..0x50350. */
static void MoveFire(TactState *pState, const MoveActionCtx *pCtx,
                     TactQueueEntry *pEntry, Soldier *pUnit)
{
    TactItem *pWeapon = (TactItem *)pEntry->pWeapon;
    ObjectRec *pObj   = pWeapon->pObj;
    const u16 uKey    = pObj->uSortKey;
    const bool bSpecial = (uKey == 13 || uKey == 35);
    i32 nX0, nY0, nZ0, nX1, nY1, nZ1;

    *pCtx->pbActionLatch = 0;

    if ((pUnit->bFlags & 0x20) != 0 || (i8)pState->nStepZDelta != 0) {
        /* 0x5035C..0x50530: the shot was cancelled mid-animation. Clear the
         * trace flag off each of the ten multi-target cells and stop. The three
         * arrays are doubles in world units: Z is scaled by 1/3 for the layer
         * test but by 1/12 for the cell index, X and Y by 1/16. */
        pEntry->nCmd = 13;
        *pCtx->pbFireStatus1 = 0;
        for (i32 n = 0; n < 10; ++n) {
            const i32 nLayer = (i32)(pCtx->pAdMtZ[n] * (1.0 / 3.0));
            const i32 nRow   = (i32)(pCtx->pAdMtY[n] * (1.0 / 16.0));
            const i32 nCol   = (i32)(pCtx->pAdMtX[n] * (1.0 / 16.0));
            MapCell *pCell;
            i32 nZ, nY, nX;
            if (!Map_IsValidCell(&pCtx->geom, nCol, nRow, nLayer)) { continue; }
            nZ = (i32)pCtx->pAdMtZ[n] / 12;
            nY = (i32)pCtx->pAdMtY[n];
            nX = (i32)pCtx->pAdMtX[n];
            if (nY < 0) { nY += 15; }
            if (nX < 0) { nX += 15; }
            pCell = MoveCell(pCtx, nX >> 4, nY >> 4, nZ);
            if (pCell != NULL) { pCell->bFlags29 &= (u8)~0x20; }
        }
        Tact_RefreshTeamVisibility(pState, pCtx->apXcomUnits,
                                   (i32)*pCtx->pnXcomCount,
                                   pCtx->pfnUpdateVisibility, pCtx->pCallbackUser);
        return;
    }

    Map_ClearShotTraceFlags(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom);

    /* 0x4FEA8..0x4FF00: the two special weapons skip the TU charge when a shot
     * has already been counted for this action. */
    if (!bSpecial || *pCtx->pnShotCount == 0) {
        if (pUnit->uTu < pEntry->nTuCost) { MoveAbortNoTime(pState, pCtx); return; }
        pUnit->uTu = (u16)(pUnit->uTu - pEntry->nTuCost);
    }

    Map_ClearShotTraceFlags(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom);

    pUnit->bFlags |= 4u;
    Unit_UpdateAnimPose(pUnit, NULL, NULL);

    /* 0x4FFC0..0x50100: the muzzle origin. A special weapon that has already
     * fired shoots from the recorded shot cell instead of from the unit. */
    if (bSpecial && *pCtx->pnShotCount > 0) {
        const MapCell *pShot = pCtx->apShotCells[*pCtx->pnShotCount];
        if (pShot == NULL) { return; }
        nX0 = 16 * (i32)pShot->nX + 8;
        nY0 = 16 * (i32)pShot->nY + 8;
        nZ0 = 12 * (i32)pShot->nZ + 6;
    } else {
        const MapCell     *pCell = pUnit->pCell;
        const MuzzleOffset m     = Tact_MuzzleOffset(pUnit, pObj);
        nX0 = m.nDx + Map_CellCentreX(pCell);
        nY0 = m.nDy + Map_CellCentreY(pCell);
        nZ0 = 12 * (i32)pCell->nZ + m.nDz + Map_CeilingLift(pCell) + 6;
    }

    nX1 = 16 * (i32)pEntry->nX + 8;
    nY1 = 16 * (i32)pEntry->nY + 8;
    nZ1 = 12 * (i32)pEntry->nZ + 6;
    {
        MapCell *pTarget = MoveCell(pCtx, pEntry->nX, pEntry->nY, pEntry->nZ);
        /* 0x5012C..0x50154: an occupied target lowers the aim point by the
         * occupant cell's own ceiling lift. */
        if (pTarget != NULL && MapCell_HasOccupant(pTarget)) {
            nZ1 -= Map_CeilingLift(pTarget);
        }
    }

    Tact_TraceProjectileArc(pCtx->pAim, pCtx->pTables, &pCtx->geom,
                            pCtx->pfnLookup, pCtx->pUser,
                            pCtx->pTraceExcludeSel, pCtx->pTraceExcludeExtra,
                            nX0, nY0, nZ0, nX1, nY1, nZ1, 1, NULL);

    Unit_RollChance(pCtx->pAim->dBound, pEntry->nArg, pCtx->pScatter);

    /* 0x501B0..0x5023C: mode 7 fires straight, anything else adds the scatter. */
    if (pEntry->nMode == 7) {
        pCtx->pScatter->nDx = 0;
        pCtx->pScatter->nDy = 0;
        pCtx->pScatter->nDz = 0;
    }
    Tact_TraceProjectileArc(pCtx->pAim, pCtx->pTables, &pCtx->geom,
                            pCtx->pfnLookup, pCtx->pUser,
                            pCtx->pTraceExcludeSel, pCtx->pTraceExcludeExtra,
                            nX0, nY0, nZ0,
                            nX1 + pCtx->pScatter->nDx,
                            nY1 + pCtx->pScatter->nDy,
                            nZ1 + pCtx->pScatter->nDz, 1, NULL);

    *pCtx->pnMaxLayerClamp = *pCtx->pnLayerScratch;

    Tact_ComputeAimVector(pCtx->pAim, &pCtx->geom, pCtx->pfnLookup, pCtx->pUser,
                          nX0, nY0, nZ0,
                          nX1 + pCtx->pScatter->nDx,
                          nY1 + pCtx->pScatter->nDy,
                          nZ1 + pCtx->pScatter->nDz, pObj->bReadySel);

    Tact_ChargeShotAmmo(pWeapon, pCtx->pnShotCount);

    Sound_Play(g_aFireSoundBySortKey[uKey]);
}

/* cmd 0x11, throw an item. 0x50630..0x50984. */
static void MoveThrow(TactState *pState, const MoveActionCtx *pCtx,
                      TactQueueEntry *pEntry, Soldier *pUnit)
{
    MapCell *pTarget;
    i32 nX0, nY0, nZ0, nX1, nY1, nZ1;

    if (pUnit->uTu < pEntry->nTuCost) {
        MoveAbortMsg(pState, pCtx, MOVE_MSG_NO_TIME);   /* LABEL_3, ungated */
        return;
    }

    Map_ClearShotTraceFlags(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom);

    pTarget = MoveCell(pCtx, pEntry->nX, pEntry->nY, pEntry->nZ);
    if (pTarget == NULL) { MoveAbort(pState, pCtx); return; }
    *pCtx->ppThrowTarget = pTarget->pUnit;

    nX0 = Map_CellCentreX(pUnit->pCell);
    nY0 = Map_CellCentreY(pUnit->pCell);
    nZ0 = 12 * (i32)pUnit->pCell->nZ + Map_CeilingLift(pUnit->pCell) + 6;
    nX1 = 16 * (i32)pEntry->nX + 8;
    nY1 = 16 * (i32)pEntry->nY + 8;
    nZ1 = 12 * (i32)pEntry->nZ;

    if (Tact_ResolveThrowLanding(pCtx->pThrowArc, pCtx->pThrowLanding,
                                 pCtx->pTables, &pCtx->geom, pCtx->pfnLookup,
                                 pCtx->pUser, pUnit, *pCtx->ppThrowTarget,
                                 pCtx->pThrowBudget, pCtx->geom.nDimX,
                                 pCtx->bThrowHitStop,
                                 nX0, nY0, nZ0, nX1, nY1, nZ1) == 0) {
        MoveAbortMsg(pState, pCtx, MOVE_MSG_THROW_FAIL);
        return;
    }

    pUnit->uTu = (u16)(pUnit->uTu - pEntry->nTuCost);
    if (pUnit->pHandL == *pCtx->ppSelWeapon) { pUnit->pHandL = NULL; }
    else                                      { pUnit->pHandR = NULL; }
    Unit_UpdateAnimPose(pUnit, NULL, NULL);
    Unit_RecalcInventoryStat(pUnit, pCtx->pInvHooks);

    /* The miss is resolved once, doubled, and resolved again from the same
     * origin; the second landing is where the item actually goes. */
    Unit_RollChance(pCtx->pAim->dBound, pEntry->nArg, pCtx->pScatter);
    pCtx->pScatter->nDx *= 2;
    pCtx->pScatter->nDy *= 2;

    Tact_ResolveThrowLanding(pCtx->pThrowArc, pCtx->pThrowLanding, pCtx->pTables,
                             &pCtx->geom, pCtx->pfnLookup, pCtx->pUser, pUnit,
                             *pCtx->ppThrowTarget, pCtx->pThrowBudget,
                             pCtx->geom.nDimX, pCtx->bThrowHitStop,
                             nX0, nY0, nZ0,
                             nX1 + pCtx->pScatter->nDx,
                             nY1 + pCtx->pScatter->nDy, nZ1);

    Tact_SimulateThrowArc(pCtx->pThrowArc, &pCtx->geom, pCtx->pfnLookup,
                          pCtx->pUser, nX0, nY0, nZ0,
                          nX1 + pCtx->pScatter->nDx,
                          nY1 + pCtx->pScatter->nDy,
                          nZ1 + pCtx->pScatter->nDz);

    *pCtx->pnMaxLayerClamp = *pCtx->pnLayerScratch;
    *pCtx->pbActionLatch = 0;
    Sound_Play(12);
}

void Tact_ProcessMoveAction(TactState *pState, const MoveActionCtx *pCtx)
{
    TactQueueEntry *pEntry  = &pState->aQueue[pState->nQueueCursor];
    Soldier        *pUnit   = pEntry->pUnit;
    const i32       nFacing = (i8)pEntry->nFacing;
    const i32       nDelta  = pEntry->nDelta;

    pState->bFlag0 = 1;
    pState->bFlag1 = 0;

    switch (pEntry->nCmd) {
    case 0:  MoveTurn(pState, pCtx, pEntry, pUnit, nFacing); break;
    case 1:  MoveStep(pState, pCtx, pEntry, pUnit, nDelta); break;
    case 2:
        pUnit->bFlags &= (u8)~0x02;
        Unit_UpdateAnimPose(pUnit, NULL, NULL);
        break;
    case 3:
        if (pUnit->uTu < 4) { MoveAbortNoTime(pState, pCtx); break; }
        if (pCtx->pfnArmorTuWarn(pUnit, 4, pCtx->pCallbackUser)) {
            MoveAbort(pState, pCtx);
            break;
        }
        pState->nPhaseTick = 0;
        Sound_Play(26);
        break;
    case 4:  MoveTurnTrace(pState, pCtx, pEntry, pUnit, nFacing); break;
    case 5:  MoveVert(pState, pCtx, pEntry, pUnit, nFacing); break;
    case 7:
        if ((pUnit->bFlags & 1) == 0) { break; }
        if (pUnit->uTu < 2) { MoveAbortNoTime(pState, pCtx); break; }
        if (pCtx->pfnArmorTuWarn(pUnit, 2, pCtx->pCallbackUser)) {
            MoveAbort(pState, pCtx);
            break;
        }
        pUnit->uTu = (u16)(pUnit->uTu - 2);
        pUnit->bFlags &= (u8)~0x01;
        Unit_UpdateAnimPose(pUnit, NULL, NULL);
        break;
    case 8:
        pState->nPhaseTick = 0;
        *pCtx->pnAnimPhase = 0;
        pUnit->bFlags |= 0x20u;
        Unit_UpdateAnimPose(pUnit, NULL, NULL);
        pUnit->bFlags &= (u8)~0x20;
        break;
    case 0xA:
        pUnit->bFlags &= (u8)~0x04;
        Unit_UpdateAnimPose(pUnit, NULL, NULL);
        *pCtx->pnLayerScratch = (i8)*pCtx->pnMaxLayerClamp;
        break;
    case 0xB: MoveRecover(pCtx, pUnit); break;
    case 0xE: MoveFire(pState, pCtx, pEntry, pUnit); break;
    case 0xF:
        *pCtx->pnLayerScratch = (i8)*pCtx->pnMaxLayerClamp;
        *pCtx->pnUnk1405C = 1;   /* R6 is the constant 1 from the prologue */
        break;
    case 0x10:
        if (pState->bPhaseActive != 0) {
            pState->gap_70112[1] = 0;
            pState->nPhaseTick = 0;
        } else {
            pEntry->nCmd = 13;
        }
        break;
    case 0x11: MoveThrow(pState, pCtx, pEntry, pUnit); break;
    case 0x12:
        if (pUnit->uTu < pEntry->nTuCost) {
            MoveAbortMsg(pState, pCtx, MOVE_MSG_NO_TIME);
            break;
        }
        Map_ClearShotTraceFlags(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom);
        pUnit->uTu = (u16)(pUnit->uTu - (u16)pEntry->nTuCost);
        pState->nPhaseCounter = 0;
        pState->gap_70112[1] = 1;
        Sound_Play(g_aFireSoundBySortKey
                       [((TactItem *)pEntry->pWeapon)->pObj->uSortKey]);
        break;
    default:
        /* 6, 9, 12 and 13 do nothing. */
        break;
    }
}
