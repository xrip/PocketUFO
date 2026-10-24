/* pufo_tactstate.c - the tactical action queue.
 *
 * Evidence: see pufo_tactstate.h for the full address list.
 */
#include "game/pufo_tactstate.h"
#include "game/pufo_path.h"   /* g_aStepDir, Map_FacingDelta, Map_bFacingTurnPositive */
#include "audio/pufo_audio.h" /* Sound_Play */
#include "game/pufo_turn.h"    /* Tact_RefreshTeamVisibility */
#include "game/pufo_combat.h"  /* g_aFireSoundBySortKey, ProximityCtx, Combat_ResolveShot */
#include "game/pufo_font.h"    /* Font_GetSprByIndex, Mc1Rec.nGlobalIdx */
#include "util/pufo_rand.h"    /* Game_RandU16 */

#include <math.h>
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

/* TactState_EnqueueVertStep @ 0x49ddc. A unit that still has to stand up does so
 * first, which is why this can emit two entries.
 *
 * The layer delta goes into the entry's nFacing slot, not into nDelta: the original
 * writes `pMapTactical->aQueue[n].nFacing = nFacing` with the caller's second
 * argument, and the vertical step's draw offset is the field it reads back
 * (`nDrawOffY = -2 * tick * nFacing`, 0x50FBC..0x50FFC). Both callers pass a layer
 * delta - Unit_PathToCell @ 0x2c2a8 passes the step's dz (0x2c664) and the HUD's
 * climb helpers pass +1 and -1. */
void TactState_EnqueueVertStep(TactState *pState, Soldier *pUnit, u8 nLayerDelta)
{
    TactQueueEntry *pEntry;

    if ((pUnit->bFlags & PUFO_UNITFLAG_STAND_UP) != 0) {
        TactState_EnqueueStandFromKneel(pState, pUnit);
    }
    pEntry = &pState->aQueue[pState->nQueueLen];
    pEntry->nCmd    = TACTCMD_VERT_STEP;
    pEntry->nFacing = nLayerDelta;
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
 * 0xF in place.
 *
 * The two byte arms (Part 105): 0x27364 loads unk_113A39 (Map+0x81) and 0x27388
 * STRBNEs the literal 1 into Map_bEnemyTurnActive when it is non-zero -- the ONLY
 * in-game setter of that byte apart from the enemy-AI owner, and the wrap at
 * 0x274B4/0x274BC clears both bytes before the tail Render_Frame at 0x274C4. */
bool GameTick_Step(TactState *pState, const GameTickStepCells *pCells,
                   MapCellLookup pfnLookup, void *pUser)
{
    i32 nX;
    i32 nY;
    i32 nLayer;

    if (pState->bPhaseActive == 0) {
        return false;
    }

    if (pCells->pbSlot8Byte1 != NULL && *pCells->pbSlot8Byte1 != 0 &&
        pCells->pbEnemyTurnActive != NULL) {
        *pCells->pbEnemyTurnActive = 1; /* 0x27380 MOVNE R1,#1 / 0x27388 STRBNE */
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
        pState->bPhaseActive = 0;               /* 0x274AC */
        if (pCells->pbSlot8Byte1 != NULL) {
            *pCells->pbSlot8Byte1 = 0;          /* 0x274B4 */
        }
        if (pCells->pbEnemyTurnActive != NULL) {
            *pCells->pbEnemyTurnActive = 0;     /* 0x274BC */
        }
        if (pCells->pfnRenderFrame != NULL) {
            pCells->pfnRenderFrame(pCells->pRenderUser); /* 0x274C4 tail */
        }
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
 * bAiActionActive is set. Cases 1, 3, 4, 5, 7 and 0xE land here; cases 0x11 and 0x12
 * use LABEL_3, which pushes unconditionally. */
static void MoveAbortNoTime(TactState *pState, const MoveActionCtx *pCtx)
{
    if (pState->bAiActionActive == 0) {
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

    /* 0x2FC74, the first thing the original's arc walk does: Map.pFireArg6 (0x14050)
     * takes apObjects[pSelWeapon->nIndex] -- the item's OWN index at TactItem+4, not the
     * record its pObj points at. The cargo build sets a weapon item's nIndex to the loaded
     * magazine's slot (0x3948C..0x39504: nAmmo comes from apObjects[anAmmoTypeId[k]]->bAmmoCap
     * "and its nIndex (+4) is that ammo type id"), an ammo item's to its own slot, and the
     * reload arms rewrite it (0x15D38, 0x15E3C). That is where a clip-fed weapon's damage
     * lives: main.dat Rifle (slot 4) carries uFirePower 0 and Rifle clip (slot 38) carries
     * 30, while a self-powered weapon names its own slot as its ammo type (Laser rifle,
     * slot 5, bAmmoCap 255, anAmmoTypeId[0] = 5), so the one lookup serves both.
     * Combat_ResolveShot reads the fire type at 0x2F150, the readiness selector at 0x2F44C
     * and the power at 0x2F4A4 off this record, and takes the shot sound from it (0x2F2D8,
     * 0x2F3C0) -- publishing pObj instead resolves every clip-fed shot as ZERO damage.
     * Tact_ProcessFireAction reads the slot back twice at the resolve (0x51CDC, 0x51F04) and
     * 0x2F24C stores it there a second time. With nothing published here the slot stays NULL,
     * Combat_ResolveShot's first read of it at 0x2f2c0 dereferences a null pointer, and no
     * shot ever resolves at all. The original dereferences the index unconditionally; the port
     * falls back to the item's own record when no table was wired or the index is out of
     * range, and says so. */
    const TactItem *pSel = (pCtx->ppSelWeapon != NULL) ? *pCtx->ppSelWeapon : NULL;
    if (pCtx->ppFireWeaponSlot != NULL && pSel != NULL) {
        const ObjectRecFull *pShotObj = (const ObjectRecFull *)pSel->pObj;

        if (pCtx->apObjectTable != NULL && (i32)pSel->nIndex < pCtx->nObjectCount) {
            pShotObj = pCtx->apObjectTable[pSel->nIndex];
        }
        *pCtx->ppFireWeaponSlot = pShotObj;
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

    /* The range budget, composed exactly where the original composes it -- inside
     * the landing call at 0x31A44..0x31A78. Its soldier is Map.pSelUnit @ 0x15334
     * (0x31A54), the SELECTED unit rather than the acting one, carried here as
     * pTraceExcludeSel (Map_BuildMoveActionCtx @ pufo_mapctx.c:96); its weight is
     * the thrown entry's own object record (entry->pWeapon @ 0x1c -- v124[13] at
     * 0x507F8, then pObj->bWeight @ 0xA5 at 0x31A58). The original reads the
     * soldier unguarded; a mission always has a selected unit when a throw runs. */
    pCtx->pThrowBudget->nStatStrength  = pCtx->pTraceExcludeSel->auStat[PUFO_STAT_STRENGTH];
    pCtx->pThrowBudget->nStatStamina   = pCtx->pTraceExcludeSel->auStat[PUFO_STAT_STAMINA];
    pCtx->pThrowBudget->nStunOrArmor   = pCtx->pTraceExcludeSel->uStunOrArmor;
    pCtx->pThrowBudget->nWeight        = ((const TactItem *)pEntry->pWeapon)->pObj->bWeight;

    const i32 nReachable = Tact_ResolveThrowLanding(pCtx->pThrowArc, pCtx->pThrowLanding,
                                                    pCtx->pTables, &pCtx->geom, pCtx->pfnLookup,
                                                    pCtx->pUser, pUnit, *pCtx->ppThrowTarget,
                                                    pCtx->pThrowBudget, pCtx->geom.nDimX,
                                                    nX0, nY0, nZ0, nX1, nY1, nZ1);

    /* Publish before the result is tested: both stores belong to the call itself. The item
     * store 0x319FC takes the call's own argument -- the thrown entry's weapon, the value
     * pushed for the landing call at 0x508E0 (`LDR R3,[R3,#0x34]`, the same object the budget
     * reads at 0x31A58) -- and 0x31A0C raises the flag, which the walk may then clear at
     * 0x322C4 or 0x32390. When this call fails the FSM returns below, so these are the values
     * the Map block keeps: the tail arc call never runs. The rejected case is the exception --
     * 0x31CD4 returns before Tact_SimulateThrowArc is reached, so neither store happens. */
    if (!pCtx->pThrowLanding->bRejected) {
        *pCtx->ppArrowItem    = (TactItem *)pEntry->pWeapon;
        *pCtx->pbThrowPending = (u8)(pCtx->pThrowLanding->bHitStop ? 1 : 0);
    }

    if (nReachable == 0) {
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
                             pCtx->geom.nDimX,
                             nX0, nY0, nZ0,
                             nX1 + pCtx->pScatter->nDx,
                             nY1 + pCtx->pScatter->nDy, nZ1);
    /* The doubled-scatter call is the one whose dTravel survives (0x323BC below); it stores the
     * same item again -- the pushed entry+0x34 value at 0x508E0 -- and leaves its walk's byte
     * in 0x1406D, which the tail arc raises back to 1 in a moment. Same rule: a call the range
     * test rejects (0x31CD4) writes neither cell. */
    if (!pCtx->pThrowLanding->bRejected) {
        *pCtx->ppArrowItem    = (TactItem *)pEntry->pWeapon;
        *pCtx->pbThrowPending = (u8)(pCtx->pThrowLanding->bHitStop ? 1 : 0);
    }

    /* 0x323BC: the landing's walked distance goes to Map+0x14038, where
     * Tact_ProcessFireAction cmd 0x11 compares its own flight counter against it.
     * The first landing call writes the same slot; this is the write that survives. */
    pCtx->pAim->dTravelSave = pCtx->pThrowLanding->dTravel;

    Tact_SimulateThrowArc(pCtx->pThrowArc, &pCtx->geom, pCtx->pfnLookup,
                          pCtx->pUser, nX0, nY0, nZ0,
                          nX1 + pCtx->pScatter->nDx,
                          nY1 + pCtx->pScatter->nDy,
                          nZ1 + pCtx->pScatter->nDz);

    /* 0x319FC / 0x31A0C, the arc tail's two Map stores. This is the call whose values survive
     * the throw: the item is the Map.pSelWeapon loaded at 0x5090C for the call at 0x50960, and
     * the flag goes back to 1 whatever the landing walk left. MapView_DrawUnitFacingArrow
     * (0x1BDC8/0x1BDD0 gate, 0x1C1F4/0x1C1FC deref) reads both while the flag stands, and only
     * Tact_ProcessFallAction's throw arm (0x53CFC/0x53D04) clears them, once the item lands. */
    *pCtx->ppArrowItem    = *pCtx->ppSelWeapon;
    *pCtx->pbThrowPending = 1;

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

/* ==========================================================================
 * Unit_ApplyFallDamage @ 0x4a7b0 and Tact_ProcessFallAction @ 0x52adc
 * ========================================================================== */

/* coredll's _rt_sdiv is called with R0 = divisor and R1 = dividend; the quotient
 * comes back in R0 and the remainder in R1 (proven at 0x1B118, where MOVS LR,R1
 * tests the remainder of nTileY % 2). A zero divisor is not defined behaviour in
 * C, and the ARM runtime has no check for it, so the port returns 0 and every
 * call site below says which divisor it stands for. */
static i32 Fall_Div(i32 nDivisor, i32 nDividend)
{
    return nDivisor != 0 ? nDividend / nDivisor : 0;
}

static void Fall_Push(const FallActionCtx *pCtx, i32 nTextRec)
{
    if (pCtx->pfnPushStatus != NULL) {
        pCtx->pfnPushStatus(nTextRec, pCtx->pCallbackUser);
    }
}

/* Unit_UpdateVisibility @ 0x48580. The original calls it directly; the port prefers
 * the ctx's visibility hook when one is supplied, because that is the same seam
 * Tact_RefreshTeamVisibility is driven through, and it lets a test see whether an
 * exit reached LABEL_111 or jumped past it to LABEL_101/LABEL_186. */
static void Fall_UpdateVisibility(const FallActionCtx *pCtx, Soldier *pUnit)
{
    if (pCtx->pfnUpdateVisibility != NULL) {
        pCtx->pfnUpdateVisibility(pUnit, pCtx->pCallbackUser);
    } else if (pCtx->pVisCtx != NULL) {
        Unit_UpdateVisibility(pUnit, pCtx->pVisCtx);
    }
}

/* 0x52EC4..0x52EE0, 0x53B8C..0x53BC8 and 0x53C04..0x53C30: the Xcom roster
 * visibility sweep. The count is re-read every iteration in the original because
 * a sweep can end a mission; the port reads it once, which is the same unless a
 * callee shrinks the roster mid-loop. */
static void Fall_RefreshXcom(const FallActionCtx *pCtx)
{
    for (i32 i = 0; i < (i32)*pCtx->pnXcomCount; ++i) {
        Fall_UpdateVisibility(pCtx, pCtx->apXcomUnits[i]);
    }
}

/* LABEL_150. */
static void Fall_TeamVisibility(const FallActionCtx *pCtx, TactState *pState)
{
    Tact_RefreshTeamVisibility(pState, pCtx->apXcomUnits, (i32)*pCtx->pnXcomCount,
                               pCtx->pfnUpdateVisibility, pCtx->pCallbackUser);
}

/* SideLists carries its counts by value, so the byte globals every other tactical
 * reader uses have to be written back after a removal. See "Separate issues" in
 * PLAN.md: the by-value counts are the design problem, not this call site. */
static void Fall_SyncCounts(const FallActionCtx *pCtx)
{
    if (pCtx->pLists == NULL) {
        return;
    }
    if (pCtx->pnXcomCount != NULL)  { *pCtx->pnXcomCount  = (u8)pCtx->pLists->nXcom; }
    if (pCtx->pnEnemyCount != NULL) { *pCtx->pnEnemyCount = (u8)pCtx->pLists->nEnemy; }
}

/* 0x53960..0x539AC and 0x53AF8..0x53B40: the "I see the enemy" message is pushed
 * when the visibility pass that just ran spotted someone (unk_118800, set by
 * Unit_SpotCellOccupant @ 0x48344), and a command-2 entry is spliced in right
 * after the entry being executed so the walk-in animation runs next. */
static void Fall_SpotTail(const FallActionCtx *pCtx, TactState *pState)
{
    if (*pCtx->pbEnemySpotted == 0) {
        return;
    }
    Fall_Push(pCtx, PUFO_TXTREC_SPOTTED);
    pState->nQueueLen    = (u16)(pState->nQueueCursor + 2);
    pState->aQueue[pState->nQueueCursor + 1].nCmd = TACTCMD_OPEN;
}

/* LABEL_186 (0x540E0..0x54128). A set Map_bTickHold skips the whole tail, so the
 * cursor stays on the entry that ran. */
static void Fall_Tail(TactState *pState, const FallActionCtx *pCtx)
{
    if (*pCtx->pbTickHold != 0) {
        return;
    }
    pState->bFlag0 = 0;
    const i32 nNext = pState->nQueueCursor + 1;
    pState->nQueueCursor = nNext;
    if (nNext >= (i32)pState->nQueueLen) {
        Map_RefreshCellMeta(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom,
                            pCtx->bLightEnabled, pCtx->pLightmap);
        pState->nQueueLen    = 0;
        pState->bQueueActive = 0;
        *pCtx->pnAnimPhase   = 0;
    }
}

/* --- Unit_ApplyFallDamage @ 0x4a7b0 ------------------------------------------ */

/* The footprint cell at (nX + dx, nY + dy) on nLayer. The original indexes the
 * cell array with no bounds test at all (0x4A820..0x4A854), so an out-of-map
 * footprint reads a neighbouring row; the port's lookup returns NULL instead. */
static MapCell *Fall_FootprintCell(const FallDamageCtx *pCtx, const MapCell *pCell,
                                   i32 nLayer, i32 nDx, i32 nDy)
{
    return pCtx->pfnLookup(pCell->nX + nDx, pCell->nY + nDy, nLayer, pCtx->pUser);
}

static bool Fall_CellHasFloor(const MapCell *pCell)
{
    return pCell != NULL && MapCell_TestFlag(pCell, CELLFLAG_HAS_FLOOR);
}

void Unit_ApplyFallDamage(Soldier *pUnit, const FallDamageCtx *pCtx)
{
    MapCell *pOrigin = pUnit->pCell;

    if (MapCell_TestFlag(pOrigin, CELLFLAG_HAS_FLOOR)) { return; }   /* 0x4A7C4 */
    if (g_aUnitTypeFlag0[pUnit->nUnitType] != 0)        { return; }   /* 0x4A7E0 */
    if (pOrigin->nZ == 0)                               { return; }   /* 0x4A7F0 */

    const bool bLarge = Unit_bIsLarge(pUnit);

    /* 0x4A858..0x4A89C: a large unit with a floor anywhere in its footprint at its
     * own layer is standing on something, so there is no fall at all. */
    if (bLarge) {
        if (Fall_CellHasFloor(Fall_FootprintCell(pCtx, pOrigin, pOrigin->nZ, 1, 0))
            || Fall_CellHasFloor(Fall_FootprintCell(pCtx, pOrigin, pOrigin->nZ, 0, 1))
            || Fall_CellHasFloor(Fall_FootprintCell(pCtx, pOrigin, pOrigin->nZ, 1, 1))) {
            return;
        }
    }

    /* The descent. A small unit tests its own column; a large unit tests the other
     * three cells of its footprint and only descends when all three are open. The
     * walk stops at the first layer that HAS a floor, or at -1 when the whole
     * column down to layer 0 is open (0x4A8B8..0x4A988). */
    i32     nLayer = (i32)pOrigin->nZ;
    MapCell *pCellX = NULL, *pCellY = NULL, *pCellXY = NULL;

    for (;;) {
        if (!bLarge) {
            if (Fall_CellHasFloor(pCtx->pfnLookup(pOrigin->nX, pOrigin->nY, nLayer,
                                                  pCtx->pUser))) {
                break;
            }
        } else {
            pCellX  = Fall_FootprintCell(pCtx, pOrigin, nLayer, 1, 0);
            pCellY  = Fall_FootprintCell(pCtx, pOrigin, nLayer, 0, 1);
            pCellXY = Fall_FootprintCell(pCtx, pOrigin, nLayer, 1, 1);
            if (Fall_CellHasFloor(pCellX) || Fall_CellHasFloor(pCellY)
                || Fall_CellHasFloor(pCellXY)) {
                break;
            }
        }
        if (nLayer == 0) {
            nLayer = -1;   /* --nLayer >= 0 failed: the column never had a floor */
            break;
        }
        --nLayer;
    }

    /* 0x4A98C..0x4AA14. _itos / _muls / _stoi are coredll's soft-float helpers and
     * 0x3ECCCCCD is the float 0.4f, so the damage is 40% of max HP per floor
     * fallen. The clamp is the MOVMI/STRMI pair at 0x4AA0C. */
    i32 nDamage = (i32)((float)(pOrigin->nZ - nLayer - 1)
                        * (float)pUnit->auStat[1] * 0.4f);
    if (nDamage < 0) {
        nDamage = 0;
    }

    /* 0x4AA3C..0x4AB1C: sweep the landing cells - one for a small unit, the whole
     * 2x2 for a large one - and kill every occupant. */
    const i32 nCells = bLarge ? 4 : 1;
    bool      bBlockedByLarge = false;

    for (i32 n = 0; n < nCells; ++n) {
        MapCell *pLand = (n == 0) ? pCtx->pfnLookup(pOrigin->nX, pOrigin->nY, nLayer,
                                                    pCtx->pUser)
                                  : (n == 1) ? pCellX
                                  : (n == 2) ? pCellY
                                  : pCellXY;

        /* The original indexes the cell array with no bounds test, so a landing
         * below layer 0 reads whatever sits before the array; the port's lookup
         * returns NULL and there is nothing to sweep there. */
        if (pLand == NULL || !MapCell_HasOccupant(pLand)) {
            continue;
        }
        Soldier *pOcc = pLand->pUnit;
        if (Unit_bIsLarge(pOcc) && !bLarge) {
            bBlockedByLarge = true;   /* 0x4AAE4: a small unit landing on a large one */
            break;
        }
        if (Unit_bIsLarge(pOcc)) {
            pOcc->uHp = 0;            /* 0x4AAFC */
        }
        (void)Unit_bIsOperative(pOcc);   /* 0x4AB04: called, result dropped */
        Tact_KillUnit(pCtx->pKillCtx, pCtx->pKillHooks, pOcc);
    }

    if (bBlockedByLarge) {
        if (bLarge) {
            pUnit->uHp = 0;
        }
        Tact_KillUnit(pCtx->pKillCtx, pCtx->pKillHooks, pUnit);
    }

    /* 0x4AB4C..0x4AB8C. A unit the sweep already killed has bFlags bit 5 set and
     * skips the placement and the damage. */
    if ((pUnit->bFlags & 0x20) == 0) {
        Unit_PlaceOnMap(pUnit, pOrigin->nX, pOrigin->nY, nLayer,
                        pCtx->pfnLookup, pCtx->pUser);
        const i32 nHp = pUnit->uHp;
        pUnit->uHp = (u16)(nHp <= nDamage ? 0 : nHp - nDamage);
        Unit_EnqueueIfApSpent(pCtx->pState, pUnit, 0);
    }
}

/* --- Tact_ProcessFallAction @ 0x52adc ---------------------------------------- */

/* auStat[6] * auStat[7], the power product both psi gates divide. */
static i32 Fall_PowerProduct(const Soldier *pUnit)
{
    return (i32)pUnit->auStat[6] * (i32)pUnit->auStat[7];
}

/* 0x52C90..0x52CD8, 0x52F18..0x52F60, 0x53548..0x53590: the victim's power is
 * floored at PUFO_FALL_POWER_MIN, divided by the attacker's, and the attack cannot
 * fail while that ratio is 1 or less. Above 1 the roll has to land at or below
 * 100 / ratio out of 150. */
static bool Fall_bPsiHits(i32 nActorPower, i32 nVictimPower)
{
    if (nVictimPower < PUFO_FALL_POWER_MIN) {
        nVictimPower = PUFO_FALL_POWER_MIN;
    }
    const i32 nRatio = Fall_Div(nActorPower, nVictimPower);
    if (nRatio <= 1) {
        return true;
    }
    return (i32)Game_RandU16(PUFO_FALL_PSI_RAND_MAX)
           <= PUFO_FALL_PSI_PERCENT / nRatio;
}

/* 0x52D0C..0x52D40, 0x52FEC..0x52FE4, 0x535FC..0x53618: the morale a successful
 * psi attack leaves behind. The MOVMI clamp is the zero floor and the AND #0xFF is
 * the byte truncation. */
static void Fall_PsiMorale(Soldier *pVictim, i32 nActorPower, i32 nReactDiv)
{
    i32 n = Fall_Div(-PUFO_FALL_MORALE_DIVISOR, nActorPower)
          - Fall_Div((i32)pVictim->auStat[2], nReactDiv)
          + (i32)pVictim->bMorale;
    if (n < 0) {
        n = 0;
    }
    pVictim->bMorale = (u8)n;
}

/* 0x52E70..0x52EA4: "<name> под контролем пришельцев", pushed forced. The original
 * copies the name terminator-inclusive into a 140-byte stack buffer and strcat's
 * the suffix onto it; the port bounds both copies. */
static void Fall_PushControlMessage(const FallActionCtx *pCtx, Soldier *pVictim)
{
    char  szMsg[140];
    i32   n = 0;

    while (n < (i32)sizeof(szMsg) - 1 && pVictim->szName[n] != 0) {
        szMsg[n] = pVictim->szName[n];
        ++n;
    }
    szMsg[n] = 0;
    /* The original copies the name terminator-inclusive and then strcat's the
     * suffix onto it: the suffix starts AT the terminator and brings its own. */
    i32 nSuffix = 0;
    if (pCtx->pszAlienControlSuffix != NULL) {
        while (n + nSuffix < (i32)sizeof(szMsg) - 1
               && pCtx->pszAlienControlSuffix[nSuffix] != 0) {
            szMsg[n + nSuffix] = pCtx->pszAlienControlSuffix[nSuffix];
            ++nSuffix;
        }
    }
    szMsg[n + nSuffix] = 0;
    if (pCtx->pfnPushForcedText != NULL) {
        pCtx->pfnPushForcedText(szMsg, pCtx->pShellUser);
    }
}

/* 0x52CDC..0x52EE0: the psi attack that breaks the victim. The panicked unit is
 * taken out of its side list and put into the other roster - the enemy one while
 * the animation is running, the Xcom one otherwise. */
static void Fall_PanicVictim(TactState *pState, const FallActionCtx *pCtx,
                             Soldier *pActor, Soldier *pVictim)
{
    SideLists_RemoveUnit(pCtx->pLists, pVictim);
    Fall_SyncCounts(pCtx);

    /* Both arms refill the victim's TU from its own maximum (0x52E64, 0x52DC8). */
    pVictim->uTu = pVictim->auStat[0];

    if (pCtx->pTactCtx != NULL && pCtx->pTactCtx->bBusy != 0) {
        pVictim->bStatus = 1;
        pCtx->apEnemyUnits[(i32)(*pCtx->pnEnemyCount)++] = pVictim;
        Map_RefreshCellMeta(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom,
                            pCtx->bLightEnabled, pCtx->pLightmap);
        UnitList_ForEachRefresh(pCtx->pRosters, pCtx->pVisCtx);
        pVictim->bFlags &= (u8)~0x01u;
        Unit_UpdateAnimPose(pVictim, NULL, NULL);
        Fall_PushControlMessage(pCtx, pVictim);
    } else {
        pVictim->bStatus = 0;
        pCtx->apXcomUnits[(i32)(*pCtx->pnXcomCount)++] = pVictim;
        Map_RefreshCellMeta(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom,
                            pCtx->bLightEnabled, pCtx->pLightmap);
        UnitList_ForEachRefresh(pCtx->pRosters, pCtx->pVisCtx);
        Tact_SelectUnit(pCtx->pSelectCtx, pVictim);
    }
    pVictim->pLinked1040 = pActor;
    Fall_RefreshXcom(pCtx);
    if (pCtx->pfnCheckBattleEnd != NULL) {
        pCtx->pfnCheckBattleEnd(0, pCtx->pShellUser);
    }
    (void)pState;
}

/* 0x53130..0x53294: the chryssalid transform. The victim leaves its side list,
 * becomes unit type 22 with the type-22 alien record's stats, and joins the enemy
 * roster. Its current TU comes from the ATTACKER's maximum, not its own. */
static void Fall_ChryssalidTransform(TactState *pState, const FallActionCtx *pCtx,
                                     Soldier *pActor, Soldier *pVictim)
{
    SideLists_RemoveUnit(pCtx->pLists, pVictim);
    Fall_SyncCounts(pCtx);

    pVictim->bStatus   = 1;
    pVictim->nUnitType = PUFO_FALL_TYPE_TRANSFORMED;

    if (pCtx->pszTransformName != NULL) {
        i32 n = 0;
        while (n < (i32)sizeof(pVictim->szName) - 1 && pCtx->pszTransformName[n] != 0) {
            pVictim->szName[n] = pCtx->pszTransformName[n];
            ++n;
        }
        pVictim->szName[n] = 0;
    }

    const AlienUnitStatRec *pRec = AlienUnitStats_ForType(PUFO_FALL_TYPE_TRANSFORMED);
    if (pRec != NULL) {
        pVictim->auStat[0] = (u16)pRec->nTu;
        /* ORIGINAL QUIRK (0x53194): the attacker's maximum TU is written here, and
         * Unit_ResetBattleState @ 0x49994 then refills uTu from auStat[0], so the
         * value that survives is the record's. Reproduced, not corrected. */
        pVictim->uTu = pActor->auStat[0];
        pVictim->auStat[9] = (u16)pRec->nStat9;
        pVictim->auStat[1] = (u16)(Game_RandU16(0x14) - 10 + pRec->nHp);
        pVictim->auStat[2] = (u16)(Game_RandU16(0x14) - 10 + pRec->nStat2);
        pVictim->auStat[3] = (u16)(Game_RandU16(0x14) - 10 + pRec->nStat3);
        /* ORIGINAL BUG (0x531EC..0x531F4): the record's stat 5 is stored over
         * auStat[4], so stat 5 is never written and the stat-4 value is lost. */
        pVictim->auStat[4] = (u16)pRec->nStat4;
        pVictim->auStat[4] = (u16)pRec->nStat5;
        pVictim->auStat[8] = (u16)(Game_RandU16(0x14) - 10 + pRec->nStat8);
        pVictim->bUnk248   = (u8)pRec->nUnk248;
        for (i32 n = 0; n < 5; ++n) {
            pVictim->abReady252[n] = (u8)pRec->nReady[n];
        }
    }

    pVictim->bPsiActive = 0;
    /* The cell is saved first because Unit_ResetBattleState clears pCell (0x498E0).
     * The original then places from the saved cell (0x53268), not from pCell. */
    pState->pStepFromCell = pVictim->pCell;
    Unit_ResetBattleState(pVictim, pVictim->bFacing, pVictim->nUnitType,
                          pVictim->auStat, PUFO_UNIT_STAT_COUNT);
    Unit_PlaceOnMap(pVictim, pState->pStepFromCell->nX, pState->pStepFromCell->nY,
                    pState->pStepFromCell->nZ, pCtx->pfnLookup, pCtx->pUser);
    pActor->bPsiActive = 0;
    pCtx->apEnemyUnits[(i32)(*pCtx->pnEnemyCount)++] = pVictim;
}

/* LABEL_93: the melee attacker spends 20 TU and the victim's out-of-TU check runs. */
static void Fall_MeleeTail(TactState *pState, Soldier *pActor, Soldier *pVictim)
{
    Unit_EnqueueIfApSpent(pState, pVictim, 1);
    pActor->uTu = (pActor->uTu <= PUFO_FALL_TU_MELEE)
                ? 0 : (u16)(pActor->uTu - PUFO_FALL_TU_MELEE);
}

/* 0x530F8..0x534F0: the alien melee, resolved by the attacker's unit type. */
static void Fall_AlienMelee(TactState *pState, const FallActionCtx *pCtx,
                            Soldier *pActor, Soldier *pVictim)
{
    switch (pActor->nUnitType) {
    case PUFO_FALL_TYPE_CHRYSSALID: {
        Unit_DropAllItemsToCell(pVictim);
        if (!Unit_bIsLarge(pVictim)
            && (pVictim->nUnitType <= 3 || pVictim->nUnitType == 16
                || pVictim->nUnitType == 17)) {
            Fall_ChryssalidTransform(pState, pCtx, pActor, pVictim);
        } else {
            /* 0x532B4..0x532F8: the kill test uses the full half-HP figure while
             * the damage actually dealt is half of it. */
            const i32 nHalf = Unit_bIsLarge(pVictim) ? ((i32)pVictim->auStat[1] >> 1)
                                                     : (i32)pVictim->auStat[1];
            if ((i32)pVictim->uHp <= nHalf) {
                pVictim->uHp = 0;
                Tact_KillUnit(pCtx->pKillCtx, pCtx->pKillHooks, pVictim);
            } else {
                pVictim->uHp    = (u16)((i32)pVictim->uHp - nHalf / 2);
                pVictim->nUnk436 += nHalf / 2;
            }
        }
        if (pCtx->pfnCheckBattleEnd != NULL) {
            pCtx->pfnCheckBattleEnd(0, pCtx->pShellUser);
        }
        return;
    }
    case PUFO_FALL_TYPE_CELACID: {
        /* 0x53320..0x533E0. The acid power falls as the victim's readiness byte
         * rises, the roll is subtracted from it, and the kneel counter grows by one
         * per ten points over half the victim's HP. */
        const i32 nPower = PUFO_FALL_CELACID_BASE
                         - (1 - Fall_Div(100, (i32)pVictim->abReady252[2]))
                           * (i32)pVictim->bUnk248;
        i32 nDamage = nPower - (i32)Game_RandU16((u16)(nPower / 4));
        if (nDamage < 0) {
            nDamage = 0;
        }
        if (nDamage > (i32)pVictim->uHp) {
            pVictim->uHp = 0;
        } else {
            pVictim->uHp     = (u16)((i32)pVictim->uHp - nDamage);
            pVictim->nUnk436 += nDamage;
        }
        if (nDamage >= ((i32)pVictim->auStat[1] >> 1) && !Unit_bIsLarge(pVictim)) {
            pVictim->bKneelOrPrimed = (u8)(pVictim->bKneelOrPrimed
                                           + (nDamage - ((i32)pVictim->auStat[1] >> 1)) / 10 + 1);
        }
        pVictim->uTuSpent = (u16)(pVictim->uTuSpent + PUFO_FALL_CELACID_TU);
        Fall_MeleeTail(pState, pActor, pVictim);
        return;
    }
    case PUFO_FALL_TYPE_SILACOID:
    case PUFO_FALL_TYPE_RIPER: {
        /* 0x533F4..0x534F0: a flat 80, and the kneel counter only grows when the
         * victim's HP is low enough for the 80 to matter. */
        if ((i32)pVictim->uHp < PUFO_FALL_FIXED_HIT) {
            pVictim->uHp = 0;
        } else {
            pVictim->uHp     = (u16)((i32)pVictim->uHp - PUFO_FALL_FIXED_HIT);
            pVictim->nUnk436 += PUFO_FALL_FIXED_HIT;
        }
        if (((i32)pVictim->auStat[1] & 0xFFFE) > 0xA0 || Unit_bIsLarge(pVictim)) {
            Fall_MeleeTail(pState, pActor, pVictim);
            return;
        }
        pVictim->bKneelOrPrimed = (u8)(pVictim->bKneelOrPrimed
                                       + (PUFO_FALL_FIXED_HIT - ((i32)pVictim->auStat[1] >> 1)) / 10 + 1);
        Fall_MeleeTail(pState, pActor, pVictim);
        return;
    }
    default:
        return;
    }
}

/* 0x52C5C..0x530E4: the command-18 resolution. */
static void Fall_Action18(TactState *pState, const FallActionCtx *pCtx,
                          TactQueueEntry *pEntry, Soldier *pActor, Soldier *pVictim)
{
    /* 0x52BE0..0x52BFC: the melee target cell loses its shot-trace flag and the
     * fire animation flag goes down with it. */
    MapCell *pMelee = pCtx->pfnLookup(*pCtx->pnMeleeCellX, *pCtx->pnMeleeCellY,
                                      *pCtx->pnMeleeCellLayer, pCtx->pUser);
    if (pMelee != NULL) {
        pMelee->bFlags29 &= (u8)~0x20u;
    }
    pState->gap_70112[1] = 0;

    TactItem *pWeapon = (TactItem *)pEntry->pWeapon;
    const u16 uSortKey = pWeapon->pObj->uSortKey;
    const i32 nMode    = pEntry->nMode;

    if (uSortKey == PUFO_SPECIAL_SORT_LATCH) {
        /* 0x52C2C..0x52C4C: the look device commits the attacker and opens the
         * look screen on the target. */
        *pCtx->ppLookCommitUnit = pActor;
        *pCtx->pbLookCommitFlag = 1;
        Tact_SelectUnit(pCtx->pSelectCtx, pVictim);
        *pCtx->pnUiScreen = 4;
        Fall_UpdateVisibility(pCtx, pActor);
        return;
    }

    const i32 nActorPower  = Fall_PowerProduct(pActor);
    const i32 nVictimPower = Fall_PowerProduct(pVictim);

    /* LABEL_111 is Unit_UpdateVisibility(pActor), and not every exit reaches it:
     * the message-only exit LABEL_101 (0x5359C) jumps straight to the queue tail,
     * as do the two "morale survived" exits at 0x52D48 and 0x53620. The flag keeps
     * that difference visible instead of flattening it. */
    bool bUpdateVis = true;

    switch (nMode) {
    case 5: {
        if (!Fall_bPsiHits(nActorPower, nVictimPower)) {
            Fall_Push(pCtx, PUFO_TXTREC_PSI_FAILED);   /* LABEL_100 -> LABEL_101 */
            bUpdateVis = false;
            break;
        }
        Fall_Push(pCtx, PUFO_TXTREC_PSI_OK);
        Fall_PsiMorale(pVictim, nActorPower, PUFO_FALL_PSI_REACT_600);
        if ((i32)pVictim->bMorale > PUFO_FALL_MORALE_PANIC) {
            bUpdateVis = false;
            break;
        }
        Fall_PanicVictim(pState, pCtx, pActor, pVictim);   /* ends at LABEL_66 */
        break;
    }
    case 4: {
        if (Fall_bPsiHits(nActorPower, nVictimPower)) {
            Fall_Push(pCtx, PUFO_TXTREC_PSI_OK);
            Fall_PsiMorale(pVictim, nActorPower, PUFO_FALL_PSI_REACT_1000);
        } else {
            Fall_Push(pCtx, PUFO_TXTREC_PSI_FAILED);
        }
        break;
    }
    case 6: {
        /* 0x52FF8..0x53050: the human melee. The weapon's fire power is added to
         * the victim's spent-TU counter; once that reaches HP the counter is
         * re-rolled to 10 * (rand(6) + 7) + HP, which is what makes the stun last. */
        const i32 nHp = pVictim->uHp;
        pVictim->uTuSpent = (u16)(pVictim->uTuSpent + pWeapon->pObj->uFirePower);
        if ((i32)pVictim->uTuSpent >= nHp) {
            pVictim->uTuSpent = (u16)(10 * ((i32)Game_RandU16(6) + 7) + nHp);
        }
        if ((i32)pVictim->uTuSpent >= nHp && !Unit_bIsLarge(pVictim)) {
            pVictim->pLinked1040 = pActor;
        }
        Unit_EnqueueIfApSpent(pState, pVictim, 0);   /* LABEL_106 -> LABEL_111 */
        break;
    }
    default:
        /* 0x53058: the switch's default tests the medikit sort key FIRST, so a
         * medikit carried by a mode-10 or mode-13 action takes the healing branch,
         * not the psi or alien-melee one. */
        if (uSortKey == PUFO_SPECIAL_SORT_MEDIKIT) {
            /* 0x53054..0x530E0: the medikit. Four outcomes, one message each. */
            i32 nMsg = -1;
            if ((i32)pVictim->uTuSpent <= ((i32)pVictim->uHp >> 1)) {
                if (pVictim->bKneelOrPrimed != 0) {
                    pVictim->bKneelOrPrimed = 0;
                    nMsg = PUFO_TXTREC_WOUNDS_HEALED;
                } else if ((i32)pVictim->uHp >= (i32)pVictim->auStat[1]) {
                    if ((i32)pVictim->bMorale >= PUFO_FALL_MORALE_MAX) {
                        nMsg = -1;   /* 0x530C4: no message, straight to LABEL_111 */
                    } else {
                        i32 nMorale = (i32)pVictim->bMorale + (i32)pVictim->auStat[2];
                        if (nMorale > PUFO_FALL_MORALE_MAX) {
                            nMorale = PUFO_FALL_MORALE_MAX;
                        }
                        pVictim->bMorale = (u8)nMorale;
                        nMsg = PUFO_TXTREC_MORALE_BACK;
                    }
                } else {
                    if ((i32)pVictim->auStat[1] - (i32)pVictim->uHp < 20) {
                        pVictim->uHp = pVictim->auStat[1];
                    } else {
                        pVictim->uHp = (u16)((i32)pVictim->uHp + 20);
                    }
                    nMsg = PUFO_TXTREC_PART_HEALED;
                }
            } else {
                pVictim->uTuSpent = 0;
                nMsg = PUFO_TXTREC_CONSCIOUS;
            }
            if (nMsg >= 0) {
                Fall_Push(pCtx, nMsg);   /* LABEL_101 -> LABEL_186 */
                bUpdateVis = false;
            }
        } else if (nMode == 13) {
            Fall_AlienMelee(pState, pCtx, pActor, pVictim);
        } else if (nMode == 10) {
            if (!Fall_bPsiHits(nActorPower, nVictimPower)) {
                Fall_Push(pCtx, PUFO_TXTREC_PSI_FAILED);
                bUpdateVis = false;
            } else {
                Fall_Push(pCtx, PUFO_TXTREC_PSI_OK);
                Fall_PsiMorale(pVictim, nActorPower, PUFO_FALL_PSI_REACT_600);
                if ((i32)pVictim->bMorale > PUFO_FALL_MORALE_PANIC) {
                    bUpdateVis = false;
                } else {
                    /* 0x53624..0x53658: no panic here - the victim is stunned in place. */
                    const i32 nHp = pVictim->uHp;
                    pVictim->uTuSpent = (u16)(nHp + PUFO_FALL_FIXED_HIT);
                    if ((i32)pVictim->uTuSpent >= nHp && !Unit_bIsLarge(pVictim)) {
                        pVictim->pLinked1040 = pActor;
                    }
                    Unit_EnqueueIfApSpent(pState, pVictim, 0);
                }
            }
        }
        break;
    }

    if (bUpdateVis) {
        Fall_UpdateVisibility(pCtx, pActor);   /* LABEL_111 @ 0x5365C */
    }
}

/* 0x53CDC..0x540D4: the thrown item's landing. The flight end is held as three
 * doubles (16 sub-cells to a cell in X and Y, 12 to a layer); the search steps the
 * layer down while the cell has no floor sprite, for at most three layers. */
static void Fall_ThrowLanding(TactState *pState, const FallActionCtx *pCtx)
{
    (void)pState;

    const i32 nLayerBase = (i32)*pCtx->pdThrowLayer / 12;
    const i32 nX = (i32)*pCtx->pdThrowX / 16;
    const i32 nY = (i32)*pCtx->pdThrowY / 16;

    i32  nStep  = 0;
    i32  nLayer = nLayerBase;
    bool bClamp = false;

    for (;;) {
        nLayer = nLayerBase - nStep;
        if (!Map_IsValidCell(&pCtx->geom, nX, nY, nLayer)) {
            bClamp = true;                       /* 0x53E98 -> 0x53EC8 */
            break;
        }
        MapCell *pTry = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);
        if (pTry != NULL && pTry->pFloorSpr == NULL) {
            nStep += 1;
            if (nStep < PUFO_FALL_THROW_DESCEND) {
                continue;
            }
        }
        break;
    }
    if (bClamp) {
        nLayer = nLayerBase - nStep;
        if (nLayer < 0) { nLayer = 0; }
        if (nLayer > 3) { nLayer = 3; }
    }

    if (Map_IsValidCell(&pCtx->geom, nX, nY, nLayer)) {
        MapCell     *pLand   = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);
        TactItem    *pItem   = *pCtx->ppSelWeapon;

        MapCell_ListInsertByPri(pLand, pItem);
        Sound_Play(PUFO_FALL_SOUND_ITEM_LANDED);

        if (pItem->pObj->uSortKey == PUFO_PROXIMITY_KEY_MARK) {
            /* 0x54020..0x540B4: a proximity marker is taken back off the cell and
             * stamps the animated-tile field over the 3x3 around the landing. The
             * validity test uses layer 0 while the lookup uses the landing layer -
             * that is what the original does. */
            MapCell_ListRemove(pLand, pItem);
            Map_RefreshCellMeta(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom,
                                pCtx->bLightEnabled, pCtx->pLightmap);
            for (i32 nDy = -1; nDy < 2; ++nDy) {
                for (i32 nDx = -1; nDx < 2; ++nDx) {
                    if (!Map_IsValidCell(&pCtx->geom, pLand->nX + nDx,
                                         pLand->nY + nDy, 0)) {
                        continue;
                    }
                    MapCell *pC = pCtx->pfnLookup(pLand->nX + nDx, pLand->nY + nDy,
                                                  pLand->nZ, pCtx->pUser);
                    if (pC != NULL) {
                        pC->nFlags40 = (pC->nFlags40 & 0xFFFFFF87u) | 0x18u;
                    }
                }
            }
        }
    }
    *pCtx->pnLayerScratch = (i8)*pCtx->pnMaxLayerClamp;
}

/* nCmd 1 (0x53668..0x537E4): the tail of a step. */
static void FallStepTail(TactState *pState, const FallActionCtx *pCtx, Soldier *pActor)
{
    const u8 bFlags = pActor->bFlags;

    if ((bFlags & 0x20) != 0) {
        return;
    }
    pActor->nDrawOffX = 0;
    pActor->nDrawOffY = 0;
    if ((bFlags & 0x10) == 0) {
        /* 0x536A0..0x536BC: carrying weight costs stun, scaled by strength. The
         * quotient is cut to 16 signed bits by the LSL#16 / LSR#16 pair. */
        const i32 nQuot = Fall_Div((i32)pActor->auStat[8],
                                   (i32)pActor->uInvBurden * 4) & 0xFFFF;
        pActor->uStunOrArmor = (u16)((i32)pActor->uStunOrArmor - 2 - nQuot);
    }

    MapCell *pCell = pActor->pCell;
    *pCtx->pnLayerScratch = (pCell->pCeilSpr != NULL && pCell->pCeilSpr->iCeilParam < -15)
                          ? (i8)(pCell->nZ + 2) : (i8)(pCell->nZ + 1);

    Combat_DetonateNearbyItems(pCtx->pProximity, pCell);

    if (Unit_bScanOpportunityFire(pCtx->pTactCtx, pActor, pCtx->apEnemyUnits,
                                  pCtx->pnEnemyCount, pCtx->apXcomUnits,
                                  pCtx->pnXcomCount, pCtx->pfnTryOpportunityFire,
                                  pCtx->pShellUser)) {
        pActor->bFlags &= (u8)~0x02u;
        Unit_UpdateAnimPose(pActor, NULL, NULL);
        Fall_TeamVisibility(pCtx, pState);
        pCtx->pTactCtx->nPhase = 1;
        return;
    }

    Fall_UpdateVisibility(pCtx, pActor);

    if (*pCtx->pbEnemySpotted != 0 && pState->bAiActionActive == 0) {
        Fall_Push(pCtx, PUFO_TXTREC_SPOTTED);
        pActor->bFlags &= (u8)~0x02u;
        Unit_UpdateAnimPose(pActor, NULL, NULL);
        Fall_TeamVisibility(pCtx, pState);
        return;
    }
    if (pState->gap_70112[0] != 0) {
        pActor->bFlags &= (u8)~0x02u;
        Unit_UpdateAnimPose(pActor, NULL, NULL);
        Fall_TeamVisibility(pCtx, pState);
        return;
    }
    if (pCtx->pTactCtx->pTrackUnit != NULL && (pCell->bFlags44 & 1) == 0) {
        pCtx->pTactCtx->pTrackUnit = NULL;
    }
}

void Tact_ProcessFallAction(TactState *pState, const FallActionCtx *pCtx)
{
    TactQueueEntry *pEntry  = &pState->aQueue[pState->nQueueCursor];
    Soldier        *pActor  = pEntry->pUnit;
    Soldier        *pVictim = pEntry->pTarget;
    const i32       nFacing = (i8)pEntry->nFacing;

    switch (pEntry->nCmd) {
    case 1:
        FallStepTail(pState, pCtx, pActor);
        break;

    case 3:
        Combat_DetonateNearbyItems(pCtx->pProximity, pActor->pCell);
        if (pState->gap_70112[0] != 0) {
            Fall_TeamVisibility(pCtx, pState);
            break;
        }
        Fall_UpdateVisibility(pCtx, pActor);
        Fall_SpotTail(pCtx, pState);
        UnitList_ForEachRefresh(pCtx->pRosters, pCtx->pVisCtx);
        break;

    case 4: {
        /* 0x539B0..0x53AB: the door span. Both cell writes use the entry's own nZ,
         * so in the original they land on the same cell: bit 0 off, bit 2 on. */
        if ((i32)pEntry->nTurnDir > 0) {
            const StepDir *pDelta = &g_aStepDir[nFacing];
            for (i32 n = 0; n < (i32)pEntry->nTurnDir; ++n) {
                pState->nTraceX = (i32)pDelta->dx * n + (i32)pEntry->nX;
                pState->nTraceY = (i32)pDelta->dy * n + (i32)pEntry->nY;
                pState->nTraceZ = (i32)pEntry->nZ;
                MapCell *pC = pCtx->pfnLookup(pState->nTraceX, pState->nTraceY,
                                              pState->nTraceZ, pCtx->pUser);
                if (pC != NULL) {
                    pC->bFlags29 = (u8)(((pC->bFlags29 & (u8)~PUFO_FALL_CELLFLAG_CLEAR)
                                        | PUFO_FALL_CELLFLAG_DOOR));
                }
            }
        }
        pActor->uTu = (u16)(pActor->uTu - PUFO_FALL_TU_STEP);
        Fall_UpdateVisibility(pCtx, pActor);
        Fall_SpotTail(pCtx, pState);
        UnitList_ForEachRefresh(pCtx->pRosters, pCtx->pVisCtx);
        Combat_DetonateNearbyItems(pCtx->pProximity, pActor->pCell);
        if (pState->gap_70112[0] != 0) {
            Fall_TeamVisibility(pCtx, pState);
        }
        break;
    }

    case 5: {
        /* 0x5381C..0x53920: the vertical step. nFacing is signed, so 9 means down. */
        pActor->uTu = (u16)(pActor->uTu - PUFO_FALL_TU_VERT);
        Unit_PlaceOnMap(pActor, pActor->pCell->nX, pActor->pCell->nY,
                        (i32)pActor->pCell->nZ + nFacing, pCtx->pfnLookup, pCtx->pUser);
        pActor->nDrawOffY = 0;
        if (pActor->pCell->pFloorSpr != NULL) {
            pActor->bFlags &= (u8)~0x10u;
            Unit_UpdateAnimPose(pActor, NULL, NULL);
        }
        Map_RefreshCellMeta(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom,
                            pCtx->bLightEnabled, pCtx->pLightmap);
        if (nFacing == -1) {
            *pCtx->pnLayerScratch = (i8)(pActor->pCell->nZ + 1);
        }
        if (*pCtx->pbEnemySpotted != 0) {
            Fall_Push(pCtx, PUFO_TXTREC_SPOTTED);
            Fall_TeamVisibility(pCtx, pState);
            break;
        }
        Combat_DetonateNearbyItems(pCtx->pProximity, pActor->pCell);
        if (pState->gap_70112[0] != 0) {
            Fall_TeamVisibility(pCtx, pState);
            break;
        }
        Fall_RefreshXcom(pCtx);
        break;
    }

    case 8:
        Tact_KillUnit(pCtx->pKillCtx, pCtx->pKillHooks, pActor);
        break;

    case 0xA:
        MapCursor_SetPos(pCtx->pCursorPos, *pCtx->pnCursorX, *pCtx->pnCursorY,
                         pCtx->pfnLookup, pCtx->pUser);
        break;

    case 0xC:
        Map_RefreshCellMeta(pCtx->pfnLookup, pCtx->pUser, &pCtx->geom,
                            pCtx->bLightEnabled, pCtx->pLightmap);
        (*pCtx->pSelectCtx->ppSelUnit)->bFlags &= (u8)~0x02u;
        Unit_UpdateAnimPose(*pCtx->pSelectCtx->ppSelUnit, NULL, NULL);
        break;

    case 0xE:
        Fall_RefreshXcom(pCtx);
        if (pCtx->pfnCheckBattleEnd != NULL) {
            pCtx->pfnCheckBattleEnd(0, pCtx->pShellUser);
        }
        break;

    case 0xF: {
        if (*pCtx->pbFallDamagePending == 0) {
            break;
        }
        FallDamageCtx fall = { pCtx->pfnLookup, pCtx->pUser, pState,
                               pCtx->pKillCtx, pCtx->pKillHooks };
        for (i32 i = 0; i < (i32)*pCtx->pnXcomCount; ++i) {
            Unit_ApplyFallDamage(pCtx->apXcomUnits[i], &fall);
        }
        for (i32 i = 0; i < (i32)*pCtx->pnEnemyCount; ++i) {
            Unit_ApplyFallDamage(pCtx->apEnemyUnits[i], &fall);
        }
        *pCtx->pbFallDamagePending = 0;
        *pCtx->pnFireTickProgress  = 0;
        *pCtx->pnLayerScratch      = (i8)*pCtx->pnMaxLayerClamp;
        UnitList_ForEachRefresh(pCtx->pRosters, pCtx->pVisCtx);
        break;
    }

    case 0x10:
        pState->bPhaseActive = 0;
        break;

    case 0x11:
        if (*pCtx->pbThrowPending == 0) {
            break;
        }
        *pCtx->ppThrowTarget = NULL;
        *pCtx->pbThrowPending = 0;
        Fall_ThrowLanding(pState, pCtx);
        break;

    case 0x12:
        Fall_Action18(pState, pCtx, pEntry, pActor, pVictim);
        break;

    default:
        /* nCmd 2, 6, 7, 9, 11 and 13 only advance the cursor. */
        break;
    }

    Fall_Tail(pState, pCtx);
}

/* --- Tact_ProcessFireAction @ 0x50adc --------------------------------------- */

/* LABEL_157 @ 0x52AC8. Every exit that ends the phase raises bFlag1, which is
 * what tells Tact_TickActiveUnit @ 0x54130 to run the fall processor next. */
static void Fire_EndPhase(TactState *pState)
{
    pState->bFlag1 = 1;
}

/* The original indexes the cell grid with no bounds test at all; the port's
 * lookup returns NULL outside the map, which is the deviation recorded in
 * docs/parity/game-core.md for this unit. */
static MapCell *FireCell(const FireActionCtx *pCtx, i32 nX, i32 nY, i32 nLayer)
{
    if (!Map_IsValidCell(&pCtx->geom, nX, nY, nLayer)) {
        return NULL;
    }
    return pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);
}

static void Fire_UpdateVisibility(const FireActionCtx *pCtx, Soldier *pUnit)
{
    if (pCtx->pfnUpdateVisibility != NULL) {
        pCtx->pfnUpdateVisibility(pUnit, pCtx->pCallbackUser);
    } else if (pCtx->pVisCtx != NULL) {
        Unit_UpdateVisibility(pUnit, pCtx->pVisCtx);
    }
}

static void Fire_TeamVisibility(const FireActionCtx *pCtx, TactState *pState)
{
    Tact_RefreshTeamVisibility(pState, pCtx->apXcomUnits, (i32)*pCtx->pnXcomCount,
                               pCtx->pfnUpdateVisibility, pCtx->pCallbackUser);
}

static void Fire_Push(const FireActionCtx *pCtx, i32 nTextRec)
{
    if (pCtx->pfnPushStatus != NULL) {
        pCtx->pfnPushStatus(nTextRec, pCtx->pCallbackUser);
    }
}

/* The ten arc samples sit in voxel units: 16 to a cell in X and Y, 12 to a layer.
 * ORIGINAL BUG (0x51264, 0x51E5C): the original converts them two ways in one
 * function and they are NOT interchangeable. 1/12 is not exact in binary, so
 * (int)(z * (1.0 / 12.0)) lands one below (int)z / 12 at exact multiples of 12 -
 * and the muzzle heights the aim setup builds are 12 * nZ + 6, so the difference
 * is reachable. The shot loop (0x51264) uses the reciprocal multiply; the three
 * resolve loops (0x51E5C, 0x52124, 0x527B8) truncate first and integer-divide.
 * Both truncate toward zero, which is C's rule, so the port keeps both forms. */
static i32 Fire_SampleLayerMul(double dZ)
{
    return (i32)(dZ * (1.0 / (double)PUFO_VOX_PER_CELL_Z));
}

static i32 Fire_SampleCoordMul(double dV)
{
    return (i32)(dV * (1.0 / (double)PUFO_VOX_PER_CELL_X));
}

static i32 Fire_SampleLayerDiv(double dZ)
{
    return (i32)dZ / PUFO_VOX_PER_CELL_Z;
}

static i32 Fire_SampleCoordDiv(double dV)
{
    return (i32)dV / PUFO_VOX_PER_CELL_X;
}

/* 0x517DC, 0x51A74 and 0x51D44: walk the ten samples and set or clear the
 * shot-trace flag on each one's cell. bIntDiv picks the resolve loops' integer
 * division over the shot loop's reciprocal multiply. */
static void Fire_SweepSamples(const FireActionCtx *pCtx, bool bSet, bool bIntDiv)
{
    AimState *pAim = pCtx->pAim;

    if (pAim->padMtX == NULL || pAim->padMtY == NULL || pAim->padMtZ == NULL) {
        return;
    }
    for (i32 i = 0; i < PUFO_AIM_SAMPLE_COUNT; ++i) {
        const i32 nLayer = bIntDiv ? Fire_SampleLayerDiv(pAim->padMtZ[i])
                                   : Fire_SampleLayerMul(pAim->padMtZ[i]);
        const i32 nY = bIntDiv ? Fire_SampleCoordDiv(pAim->padMtY[i])
                               : Fire_SampleCoordMul(pAim->padMtY[i]);
        const i32 nX = bIntDiv ? Fire_SampleCoordDiv(pAim->padMtX[i])
                               : Fire_SampleCoordMul(pAim->padMtX[i]);
        MapCell *pCell = FireCell(pCtx, nX, nY, nLayer);

        if (pCell == NULL) {
            continue;
        }
        if (bSet) {
            pCell->bFlags29 |= PUFO_CELLFLAG_SHOT_TRACE;
        } else {
            pCell->bFlags29 &= (u8)~PUFO_CELLFLAG_SHOT_TRACE;
        }
    }
}

/* 0x51A40, 0x517A8 and 0x525D8: the renderer's layer scratch, raised to
 * nMaxLayerClamp when it falls below. The original reaches the same value two
 * ways, (int)(z * 1/12) + 1 and 1 - (int)(z * -1/12); they agree for z >= 0,
 * which is every value the flight code can produce. */
static void Fire_SetLayerScratch(const FireActionCtx *pCtx, i32 nLayer)
{
    *pCtx->pnLayerScratch = (i8)nLayer;
    if (*pCtx->pnLayerScratch < *pCtx->pnMaxLayerClamp) {
        *pCtx->pnLayerScratch = (i8)*pCtx->pnMaxLayerClamp;
    }
}

/* 0x51CD4 and 0x51EF4. The target cell is the one the arc stopped on, which the
 * UI confirmations also write; the slot is AimState.nTgtX/Y/Z @ 0x14040/44/48. */
static void Fire_ResolveShot(const FireActionCtx *pCtx)
{
    if (pCtx->pCombat == NULL) {
        return;
    }
    Combat_ResolveShot(pCtx->pCombat, pCtx->pShotHooks, pCtx->pRosters,
                       pCtx->pAim->nTgtX, pCtx->pAim->nTgtY, pCtx->pAim->nTgtZ,
                       *pCtx->pnFireSlot, *pCtx->ppFireObject,
                       pCtx->apHits, pCtx->apHitsSave, pCtx->pShotOut);
}

/* cmd 1's draw-offset ladder, 0x50C38..0x50D98. Two pairs drive all eight
 * facings: nHalf is (t <= 3 ? t + 1 : t - 7) and nNeg is its sign-flipped twin
 * (-1 - t, 7 - t). Each facing writes one or both of the two scratch offsets and
 * leaves the other at whatever the previous frame left. */
static void Fire_StepDrawOffsets(TactState *pState, u8 nFacing, i32 nTick)
{
    const bool bFirst = (nTick <= 3);
    const i32 nHalf = bFirst ? nTick + 1 : nTick - 7;
    const i32 nNeg  = bFirst ? -1 - nTick : 7 - nTick;

    switch (nFacing) {
    case 0: pState->nStepDrawOffX = 2 * nHalf; pState->nStepDrawOffY = nNeg;  break;
    case 1: pState->nStepDrawOffY = 0;         pState->nStepDrawOffX = 4 * nHalf; break;
    case 2: pState->nStepDrawOffY = nHalf;     pState->nStepDrawOffX = 2 * nHalf; break;
    case 3: pState->nStepDrawOffX = 0;         pState->nStepDrawOffY = 2 * nHalf; break;
    case 4: pState->nStepDrawOffX = 2 * nNeg;  pState->nStepDrawOffY = nHalf;  break;
    case 5: pState->nStepDrawOffY = 0;         pState->nStepDrawOffX = 4 * nNeg; break;
    case 6: pState->nStepDrawOffY = nNeg;      pState->nStepDrawOffX = 2 * nNeg; break;
    case 7: pState->nStepDrawOffX = 0;         pState->nStepDrawOffY = 2 * nNeg; break;
    default: break;
    }
}

void Tact_ProcessFireAction(TactState *pState, const FireActionCtx *pCtx)
{
    TactQueueEntry *pEntry = &pState->aQueue[pState->nQueueCursor];
    Soldier        *pUnit  = pEntry->pUnit;
    AimState       *pAim   = pCtx->pAim;

    switch (pEntry->nCmd) {

    /* Turn. 0x50EAC..0x50F94. */
    case TACTCMD_TURN: {
        pState->nPhaseTick += 1;
        if (pState->nPhaseMax < pState->nPhaseTick) {
            Fire_EndPhase(pState);
            return;
        }
        /* The phase object says the unit is mid-turn: no TU is spent at all.
         * A NULL phase object is a port-local case the original cannot have. */
        if (pCtx->pTactCtx == NULL || pCtx->pTactCtx->nPhase != 1) {
            if (pUnit->uTu == 0) {
                Fire_Push(pCtx, MOVE_MSG_NO_TIME);
                Fire_TeamVisibility(pCtx, pState);
                return;
            }
            if (pCtx->pfnArmorTuWarn != NULL
                && pCtx->pfnArmorTuWarn(pUnit, 1, pCtx->pCallbackUser)) {
                Fire_TeamVisibility(pCtx, pState);
                return;
            }
            pUnit->uTu = (u16)(pUnit->uTu - 1);
        }
        i32 nFacing = (i32)pUnit->bFacing + (i32)pEntry->nTurnDir;
        if (nFacing > 7) {
            nFacing -= 8;
        }
        if (nFacing < 0) {
            nFacing += 8;
        }
        pUnit->bFacing = (u8)nFacing;
        Fire_UpdateVisibility(pCtx, pUnit);
        if (*pCtx->pbEnemySpotted != 0 && pState->bAiActionActive == 0) {
            pUnit->bAiDone = 0;
            Fire_Push(pCtx, PUFO_TXTREC_SPOTTED);
            Fire_TeamVisibility(pCtx, pState);
        }
        return;
    }

    /* The fire-frame step. 0x50B74..0x50E9C. */
    case TACTCMD_STEP: {
        if ((pUnit->bFlags & 0x20u) != 0) {
            return;   /* the unit is flagged dead: the phase is left running */
        }
        pState->nPhaseTick += 1;
        *pCtx->pnAnimPhase += 1;
        if (*pCtx->pnAnimPhase > 7) {
            *pCtx->pnAnimPhase = 0;
        }
        if (pState->nPhaseTick % 2 == 0) {
            pUnit->uTu = (u16)(pUnit->uTu - 1);
        }
        const StepDir *pDir = &g_aStepDir[pUnit->bFacing];
        const i32 nTick = pState->nPhaseTick;

        if (nTick == 4) {
            Unit_PlaceOnMap(pUnit,
                            (i32)pDir->dx + (i32)pUnit->pCell->nX,
                            (i32)pUnit->pCell->nY + (i32)pDir->dy,
                            pEntry->nDelta + (i32)pUnit->pCell->nZ,
                            pCtx->pfnLookup, pCtx->pUser);
        }
        Fire_StepDrawOffsets(pState, pUnit->bFacing, nTick);
        if (nTick <= 3) {
            pState->nStepDrawOffY -= pState->nStepFrameDiv * nTick;
        } else {
            pState->nStepDrawOffY += (7 - nTick) * pState->nStepFrameDiv;
        }
        pUnit->nDrawOffX = (i8)pState->nStepDrawOffX;
        pUnit->nDrawOffY = (i8)pState->nStepDrawOffY;

        /* The original reads pStepFromCell unconditionally; the phase owner sets it
         * before the fire phase runs. A NULL is a port-local case and leaves the
         * scratch cell alone. */
        const MapCell *pFrom = pState->pStepFromCell;
        MapCell *pScratch = (pFrom != NULL)
                          ? FireCell(pCtx, (i32)pFrom->nX + pDir->dx,
                                     (i32)pFrom->nY + pDir->dy, (i32)pFrom->nZ)
                          : NULL;
        pState->pStepCellScratch = pScratch;

        /* 0x50E54: on tick 4, and only for the types whose type-record flag 0 is
         * set, the unit stands up when the destination cell has a floor sprite and
         * kneels when it has none. The original tests the cell's first dword, which
         * is MapCell.pFloorSpr. */
        if (nTick == 4 && g_aUnitTypeFlag0[pUnit->nUnitType] == 1) {
            const bool bKneeling = (pUnit->bFlags & PUFO_UNITFLAG_KNEEL) != 0;
            const bool bHasFloor = (pScratch != NULL && pScratch->pFloorSpr != NULL);
            if (bKneeling && bHasFloor) {
                pUnit->bFlags &= (u8)~PUFO_UNITFLAG_KNEEL;
                Unit_UpdateAnimPose(pUnit, NULL, NULL);
            } else if (!bKneeling && !bHasFloor) {
                pUnit->bFlags |= PUFO_UNITFLAG_KNEEL;
                Unit_UpdateAnimPose(pUnit, NULL, NULL);
            }
        }
        if (nTick > PUFO_STEP_FRAME_MAX) {
            Fire_EndPhase(pState);
        }
        return;
    }

    /* The door animation. 0x51010..0x51168. */
    case TACTCMD_DOOR: {
        pState->nPhaseTick += 1;
        if (pState->nPhaseTick != PUFO_DOOR_SWAP_TICK) {
            if (pState->nPhaseTick >= 4) {
                Fire_EndPhase(pState);   /* LABEL_70 @ 0x5116C */
            }
            return;
        }
        MapCell *pCell = pCtx->pfnLookup(pEntry->nX, pEntry->nY, pEntry->nZ, pCtx->pUser);
        if (pCell == NULL || pCtx->pFont == NULL) {
            return;
        }
        /* The door swings the other way when the entry's facing byte is 2. The
         * sprite neighbour is looked up through the font index, so a missing wall
         * sprite is a case the original would crash on. */
        if ((i8)pEntry->nFacing == 2) {
            if (pCell->pWallSprB != NULL) {
                pCell->pWallSprA = Font_GetSprByIndex(pCtx->pFont,
                                                      (u16)(pCell->pWallSprB->nGlobalIdx - 1));
            }
            pCell->pWallSprB = NULL;
        } else {
            if (pCell->pWallSprA != NULL) {
                pCell->pWallSprB = Font_GetSprByIndex(pCtx->pFont,
                                                      (u16)(pCell->pWallSprA->nGlobalIdx + 1));
            }
            pCell->pWallSprA = NULL;
        }
        return;
    }

    /* Plain animation advance. 0x51188..0x511A8. */
    case TACTCMD_STEP_DOOR:
        pState->nPhaseTick += 1;
        *pCtx->pnAnimPhase += 1;
        if (pState->nPhaseTick > PUFO_ANIM_ADVANCE_MAX) {
            Fire_EndPhase(pState);
        }
        return;

    /* The vertical step's draw offset. 0x50FBC..0x50FFC. */
    case TACTCMD_VERT_STEP:
        pState->nPhaseTick += 1;
        if (pState->nPhaseMax < pState->nPhaseTick) {
            Fire_EndPhase(pState);
            return;
        }
        pUnit->nDrawOffY = (i8)(-2 * pState->nPhaseTick * (i8)pEntry->nFacing);
        return;

    /* The death flash. 0x529D4..0x52A30. */
    case TACTCMD_DEATH_FLASH:
        if (Unit_bIsLarge(pUnit) || pEntry->nArg != 0) {
            Fire_EndPhase(pState);
            return;
        }
        *pCtx->pnAnimPhase += 1;
        pState->nPhaseTick += 1;
        if (pState->nPhaseTick <= PUFO_DEATH_FLASH_TICK_MAX) {
            return;
        }
        *pCtx->pnAnimPhase = 0;
        Fire_EndPhase(pState);
        return;

    /* The shot flight. 0x511B4..0x51F88. */
    case TACTCMD_FIRE_SHOT: {
        if ((pUnit->bFlags & PUFO_UNITFLAG_AIMING) == 0) {
            Fire_EndPhase(pState);
            return;
        }
        if (pAim->padMtX == NULL || pAim->padMtY == NULL || pAim->padMtZ == NULL) {
            Fire_EndPhase(pState);   /* port-local: the ctx must wire the sample arrays */
            return;
        }
        pAim->dTravel += PUFO_FIRE_TRAVEL_STEP;

        /* 0x51218..0x516A8: each sample is cleared off the cell it sat on last
         * frame, then rebuilt from the muzzle at (dTravel - 1.41 * i). A sample
         * that falls behind the muzzle becomes -1000.0, which no cell test accepts. */
        const double dCosPitch = cos(pAim->dPitch);
        const double dCosYaw   = cos(pAim->dYaw);
        const double dSinYaw   = sin(pAim->dYaw);
        const double dSinPitch = sin(pAim->dPitch);
        for (i32 i = 0; i < PUFO_AIM_SAMPLE_COUNT; ++i) {
            MapCell *pCell = FireCell(pCtx,
                                      Fire_SampleCoordMul(pAim->padMtX[i]),
                                      Fire_SampleCoordMul(pAim->padMtY[i]),
                                      Fire_SampleLayerMul(pAim->padMtZ[i]));
            if (pCell != NULL) {
                pCell->bFlags29 &= (u8)~PUFO_CELLFLAG_SHOT_TRACE;
            }
            const double dBack = pAim->dTravel - (double)i * PUFO_SAMPLE_STEP;
            if (dBack > 0.0) {
                pAim->padMtX[i] = (double)pAim->nOriginX + (dCosYaw * dCosPitch) * dBack;
                pAim->padMtY[i] = (double)pAim->nOriginY + (dSinYaw * dCosPitch) * dBack;
                pAim->padMtZ[i] = (double)pAim->nOriginZ + dSinPitch * dBack;
            } else {
                pAim->padMtX[i] = PUFO_SAMPLE_OFFMAP;
                pAim->padMtY[i] = PUFO_SAMPLE_OFFMAP;
                pAim->padMtZ[i] = PUFO_SAMPLE_OFFMAP;
            }
        }

        /* 0x516D8..0x51758: the shot lands once the flight passes the distance the
         * trace walked. The two multi-target weapons land as soon as they reach the
         * bound, provided the burst still has shots left. */
        const bool bPastRange = !(pAim->dTravel <= pAim->dTravelSave);
        const TactItem *pWeapon = (const TactItem *)pEntry->pWeapon;
        const u16 uKey = (pWeapon != NULL && pWeapon->pObj != NULL)
                       ? pWeapon->pObj->uSortKey : 0;
        const bool bMultiTarget = (uKey == PUFO_SORT_KEY_MULTI_TARGET_A
                                   || uKey == PUFO_SORT_KEY_MULTI_TARGET_B);
        if (!(bPastRange
              || (bMultiTarget
                  && *pCtx->pnShotCount < *pCtx->pnBurstShots
                  && pAim->dTravel >= pAim->dBound))) {
            Fire_SetLayerScratch(pCtx, 1 - (i32)(pAim->padMtZ[0] * (-1.0 / (double)PUFO_VOX_PER_CELL_Z)));
            Fire_SweepSamples(pCtx, true, false);
            return;
        }

        /* 0x519BC: the aim's arc walk left the map. 1 means the traced shot already
         * ran off the edge, so the resolve happens on this first pass. */
        const u8 bArcOffMap = *pCtx->pbFireHitResult;
        if (pEntry->nMode != 7 || bArcOffMap != 0) {
            *pCtx->pbFireStatus1 = 0;
        } else {
            Fire_SetLayerScratch(pCtx, Fire_SampleLayerMul(pAim->padMtZ[0]) + 1);
            Fire_SweepSamples(pCtx, true, false);
        }

        if (pEntry->nMode == 7) {
            if (bArcOffMap == 0
                && (pAim->dTravelSave <= pAim->dBound
                    || *pCtx->pnShotCount >= *pCtx->pnBurstShots)) {
                Fire_ResolveShot(pCtx);
                /* 0x51D28 writes only the low byte of the dword. */
                pState->nStepZDelta = (pState->nStepZDelta & ~0xFF) | 1;
                *pCtx->pbFallDamagePending = 1;
                Fire_SweepSamples(pCtx, false, true);
            }
        } else {
            if (bArcOffMap == 0) {
                Fire_ResolveShot(pCtx);
            }
            *pCtx->pbFallDamagePending = 1;
            *pCtx->pbFireStatus1 = 0;
        }
        /* 0x51EE4 and 0x51F50 re-read Map.bFireHitResult after the resolve. The fire
         * phase never writes it and no arc walk runs in between, so both re-reads
         * return the value the aim setup left there. */
        *pCtx->pbFireStatus9 = (u8)(*pCtx->pbFireHitResult == 0);
        *pCtx->pnFireTickProgress = 0;
        pState->bFlag1 = 1;
        return;
    }

    /* The fall-damage progress tick. 0x52864..0x528A4. */
    case TACTCMD_FIRE_MARK:
        if (*pCtx->pbFallDamagePending == 0 || *pCtx->pbFireHitResult != 0) {
            *pCtx->pbFallDamagePending = 0;
            Fire_EndPhase(pState);
            return;
        }
        *pCtx->pnFireTickProgress += 1;
        if (*pCtx->pnFireTickProgress >= PUFO_FALL_PROGRESS_MAX) {
            Fire_EndPhase(pState);
        }
        return;

    /* The whole-map animated-tile decay, then the phase tick. 0x528BC..0x529C8. */
    case 0x10: {
        const i32 nRows = 10 * (i32)pCtx->geom.nDimY;
        const i32 nCols = 10 * (i32)pCtx->geom.nDimX;

        for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
            for (i32 nY = 0; nY < nRows; ++nY) {
                for (i32 nX = 0; nX < nCols; ++nX) {
                    MapCell *pCell = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);
                    if (pCell == NULL || (pCell->nFlags32 & 0x78u) == 0) {
                        continue;
                    }
                    pCell->nFlags32 = GameTick_DecayCellFlags(pCell->nFlags32);
                }
            }
        }
        pState->nPhaseCounter += 1;
        pState->nPhaseTick += 1;
        if (pState->nPhaseTick > PUFO_PHASE_TICK_MAX) {
            Fire_EndPhase(pState);
        }
        return;
    }

    /* The thrown item's flight. 0x51F9C..0x5285C. */
    case 0x11: {
        if (*pCtx->pbThrowPending == 0) {
            Fire_EndPhase(pState);
            return;
        }
        if (pAim->padMtX == NULL || pAim->padMtY == NULL || pAim->padMtZ == NULL) {
            Fire_EndPhase(pState);   /* port-local, as in cmd 0xE */
            return;
        }
        /* 0x51FCC..0x520D8: the step is 5 voxels, scaled down by bound / rise when
         * the horizontal bound is shorter than the vertical rise. The scaled branch
         * cannot divide by zero: it needs bound < rise, and the bound is never
         * negative. */
        const i32 nRise = pAim->nTargetZ - pAim->nOriginZ;
        const i32 nAbsRise = nRise < 0 ? -nRise : nRise;
        if (pAim->dBound < (double)nAbsRise) {
            pAim->dTravel += (pAim->dBound / (double)nAbsRise) * PUFO_THROW_TRAVEL_STEP;
        } else {
            pAim->dTravel += PUFO_THROW_TRAVEL_STEP;
        }

        /* 0x52124: the flag comes off the cell the item is over now. */
        MapCell *pCell = FireCell(pCtx,
                                  Fire_SampleCoordDiv(pAim->padMtX[0]),
                                  Fire_SampleCoordDiv(pAim->padMtY[0]),
                                  Fire_SampleLayerDiv(pAim->padMtZ[0]));
        if (pCell != NULL) {
            pCell->bFlags29 &= (u8)~PUFO_CELLFLAG_SHOT_TRACE;
        }

        /* 0x52260: Map_dThrowX/Y/Layer @ 0x13EE8 + 8 are sample slot 1, the
         * previous position the renderer draws the trail from. */
        pAim->padMtX[1] = pAim->padMtX[0];
        pAim->padMtY[1] = pAim->padMtY[0];
        pAim->padMtZ[1] = pAim->padMtZ[0];

        /* 0x522E4..0x525A4: the projectile equation, from the origin rather than
         * from the previous position. z = origin + travel * tan(pitch)
         * - g * travel^2 / (2 * v0^2 * cos^2(pitch)), with v0 = dArcDenom. */
        const double dTravel = pAim->dTravel;
        const double dCosPitch = cos(pAim->dPitch);
        const double dDenom = pCtx->pThrowArc != NULL ? pCtx->pThrowArc->dArcDenom : 0.0;
        const double dDrop = ((dTravel * dTravel) * PUFO_THROW_GRAVITY)
                           / (((dDenom * dDenom) * dCosPitch) * dCosPitch * 2.0);
        pAim->padMtX[0] = (double)pAim->nOriginX + cos(pAim->dYaw) * dTravel;
        pAim->padMtY[0] = (double)pAim->nOriginY + sin(pAim->dYaw) * dTravel;
        pAim->padMtZ[0] = (double)pAim->nOriginZ
                        + (tan(pAim->dPitch) * dTravel - dDrop);

        Fire_SetLayerScratch(pCtx, 1 - (i32)(pAim->padMtZ[0] * (-1.0 / (double)PUFO_VOX_PER_CELL_Z)));

        if (pAim->dTravel <= pAim->dTravelSave && *pCtx->pbThrowPending != 0) {
            /* ORIGINAL BUG (0x52718): the upper bound on y is 160 * nMapDimX, the
             * same expression the x test uses - nMapDimY is never read here, so a
             * non-square map bounds the flight by the wrong dimension. */
            const double dLimit = PUFO_THROW_XY_SCALE * (double)pCtx->geom.nDimX;
            if (pAim->padMtX[0] >= 0.0 && pAim->padMtY[0] >= 0.0 && pAim->padMtZ[0] >= 0.0
                && pAim->padMtX[0] < dLimit && pAim->padMtY[0] < dLimit
                && pAim->padMtZ[0] < PUFO_THROW_Z_LIMIT) {
                MapCell *pNew = FireCell(pCtx,
                                         Fire_SampleCoordDiv(pAim->padMtX[0]),
                                         Fire_SampleCoordDiv(pAim->padMtY[0]),
                                         Fire_SampleLayerDiv(pAim->padMtZ[0]));
                if (pNew != NULL) {
                    pNew->bFlags29 |= PUFO_CELLFLAG_SHOT_TRACE;
                }
            }
        } else {
            pState->bFlag1 = 1;
        }
        return;
    }

    /* The melee-target stamp. 0x52A40..0x52AC0. */
    case 0x12: {
        if (pState->gap_70112[1] == 0) {
            Fire_EndPhase(pState);
            return;
        }
        MapCell *pCell = FireCell(pCtx, pAim->nTgtX, pAim->nTgtY, pAim->nTgtZ);
        if (pCell != NULL) {
            pCell->bFlags29 |= PUFO_CELLFLAG_SHOT_TRACE;
        }
        pState->nPhaseCounter += 1;
        /* ORIGINAL BUG (0x52ABC): this case hands the phase COUNTER to the shared
         * LABEL_155 while cmd 0x10 hands it the phase tick (0x529C4). */
        if (pState->nPhaseCounter > PUFO_PHASE_TICK_MAX) {
            Fire_EndPhase(pState);
        }
        return;
    }

    default:
        Fire_EndPhase(pState);
        return;
    }
}

/* --- Tact_TickActiveUnit @ 0x54130 -------------------------------------------------- */

/* 0x5429C..0x54344 and 0x545B8..0x54700: the cell the last aim sample fell into.
 *   layer = (int)Map_adMtZ[0] / 12   (_rt_sdiv at 0x542C4)
 *   y     = (int)Map_adMtY[0] / 16   (MOVS / ADDMI #0xF / ASR#4 at 0x54304 = C truncation)
 *   x     = (int)Map_adMtX[0] / 16   (0x5432C)
 * and the index 60*(60*layer + y) + x is applied to Map.apCells with no bounds test.
 * The port goes through the injected lookup instead, so an out-of-map sample gives NULL
 * and the camera simply does not follow. */
static MapCell *Tick_SampleCell(const TactTickCtx *pCtx)
{
    const AimState *pAim = pCtx->pAim;
    i32 nLayer = (i32)pAim->padMtZ[0] / PUFO_VOX_PER_CELL_Z;
    i32 nY     = (i32)pAim->padMtY[0] / PUFO_VOX_PER_CELL_Y;
    i32 nX     = (i32)pAim->padMtX[0] / PUFO_VOX_PER_CELL_X;
    return pCtx->pfnLookup(nX, nY, nLayer, pCtx->pLookupUser);
}

static void Tick_Focus(const TactTickCtx *pCtx, const MapCell *pCell)
{
    const TickHooks *h = pCtx->pHooks;
    if (h != NULL && h->pfnFocusOnCell != NULL) {
        h->pfnFocusOnCell(pCell, h->pUser);
    }
}

bool Tact_TickActiveUnit(TactState *pState, const TactTickCtx *pCtx)
{
    const TickHooks *h = pCtx->pHooks;
    struct TactCtx272 *pTact = pCtx->pTactCtx;

    for (;;) {
        /* 0x54188: an empty queue hands the turn to the phase machine, which may fill
         * the queue before the re-read at 0x54198. */
        if (pState->nQueueLen == 0 && h != NULL && h->pfnProcessActionPhase != NULL) {
            h->pfnProcessActionPhase(h->pUser);
        }
        if (pState->nQueueLen == 0 || pState->bQueueActive == 0) {
            return false;
        }

        TactQueueEntry *pEntry = &pState->aQueue[pState->nQueueCursor];

        if (pState->bFlag0 != 0) {
            /* --- the fire phase (0x54584) --- */
            Tact_ProcessFireAction(pState, pCtx->pFireCtx);

            /* 0x5458c: only a busy context follows the camera, and only for a shot. */
            if (pTact->bBusy != 0 && pEntry->nCmd == TACTCMD_FIRE_SHOT) {
                MapCell *pCell = Tick_SampleCell(pCtx);
                if (pCell != NULL && (pCell->bFlags44 & 1) != 0) {
                    /* 0x54710: with no tracked unit the camera always follows; with one
                     * it re-centres only every 60 voxels of travelled distance. */
                    if (pTact->pTrackUnit == NULL
                        || (i32)pCtx->pAim->dTravel % PUFO_CAMERA_RECENTRE_STRIDE == 0) {
                        Tick_Focus(pCtx, pCell);
                        if (pTact->pTrackUnit == NULL) {
                            pTact->pTrackUnit = pCtx->apXcomUnits[0]; /* 0x5446C */
                        }
                    }
                }
            }
        } else {
            /* --- the move phase (0x541c8) --- */
            if (pState->nQueueCursor == 0) {
                pState->uKillCredit = 0; /* 0x541DC, a dword store */
            }
            /* 0x541E0: a multi-entry busy queue drops the camera unit and repaints. */
            if (pTact->bBusy != 0 && pState->nQueueLen > 1) {
                Soldier *pSel = *pCtx->ppSelUnit;
                pSel->bUnk148 = 0;
                if (pSel != pTact->pCameraUnit) {
                    pTact->pCameraUnit = NULL;
                    if (h != NULL && h->pfnRenderFrame != NULL) {
                        h->pfnRenderFrame(h->pUser);
                    }
                }
            }

            Tact_ProcessMoveAction(pState, pCtx->pMoveCtx);

            if (pTact->bBusy != 0) {
                switch (pEntry->nCmd) {
                case TACTCMD_TURN:
                case TACTCMD_STEP:
                case TACTCMD_DEATH_FLASH:
                    /* 0x54558: the unit's own cell. ORIGINAL BUG (0x54558): pUnit and
                     * pCell are dereferenced with no test that the entry carries either. */
                    if (pEntry->pUnit != NULL && pEntry->pUnit->pCell != NULL
                        && (pEntry->pUnit->pCell->bFlags44 & 1) != 0) {
                        if (pTact->pTrackUnit == NULL) {
                            pTact->pTrackUnit = pEntry->pUnit; /* 0x54574, before the focus */
                        }
                        Tick_Focus(pCtx, pEntry->pUnit->pCell);
                    }
                    break;

                case TACTCMD_FIRE_SHOT:
                    /* 0x54390: the sample cell, with NO bounds test here -- unlike the
                     * fire path, which tests Map_IsValidCell first at 0x54654. */
                    {
                        MapCell *pCell = Tick_SampleCell(pCtx);
                        if (pCell != NULL && (pCell->bFlags44 & 1) != 0) {
                            Tick_Focus(pCtx, pCell);
                            if (pTact->pTrackUnit == NULL) {
                                pTact->pTrackUnit = pCtx->apXcomUnits[0];
                            }
                        }
                    }
                    break;

                case TACTCMD_ACTION18:
                    /* 0x54288: gated on the target's own cell, then focused on the
                     * sample cell, then the target is adopted. ORIGINAL BUG (0x54288):
                     * pTarget and its pCell are dereferenced with no test; only the
                     * psi command writes pTarget. */
                    if (pEntry->pTarget != NULL && pEntry->pTarget->pCell != NULL
                        && (pEntry->pTarget->pCell->bFlags44 & 1) != 0) {
                        MapCell *pCell = Tick_SampleCell(pCtx);
                        if (pCell != NULL) {
                            Tick_Focus(pCtx, pCell);
                        }
                        if (pTact->pTrackUnit == NULL) {
                            pTact->pTrackUnit = pEntry->pTarget; /* 0x54374, after the focus */
                        }
                    }
                    break;

                default:
                    break;
                }
            }
        }

        /* --- 0x54478: the fall processor and the queue-completion tail --- */
        if (pState->bFlag1 != 0) {
            Tact_ProcessFallAction(pState, pCtx->pFallCtx);

            if (*pCtx->pbTickHold != 0 || *pCtx->pbFrameScratch != 0) {
                return false;
            }

            /* 0x544AC: a finished AI action parks the unit it was running. ORIGINAL BUG
             * (0x544D8): pAiActionUnit is dereferenced with no NULL test. */
            if (pState->bAiActionActive != 0 && pState->bQueueActive == 0) {
                pState->bAiActionActive = 0;
                pState->pAiActionUnit->uTu = 0;
            }

            /* 0x544E4: credit the kills to the first shooter in the part of the queue
             * already executed. The gate is nUnitType <= 8 AND operative (0x5451C,
             * 0x5452C) -- a civilian or a panicking unit does not collect. */
            if (pState->bQueueActive == 0) {
                bool bCredited = false;
                for (i32 i = 0; i < pState->nQueueCursor && !bCredited; ++i) {
                    if (pState->aQueue[i].nCmd != TACTCMD_FIRE_SHOT) {
                        continue;
                    }
                    Soldier *pUnit = pState->aQueue[i].pUnit;
                    if (pUnit->nUnitType > 8 || !Unit_bIsOperative(pUnit)) {
                        continue;
                    }
                    pUnit->uKills += pState->uKillCredit;
                    pState->uKillCredit = 0; /* 0x54790, a dword store */
                    bCredited = true;
                }
            }

            if (*pCtx->pbTickHold != 0) {
                return false;
            }

            if (pState->bQueueActive == 0) {
                /* 0x547B4: the opportunity-fire epilogue parks the cursor and re-selects. */
                if (pState->bUnk70103 != 0) {
                    if (h != NULL && h->pfnSetCursorPos != NULL) {
                        h->pfnSetCursorPos(-2, -2, h->pUser);
                    }
                    Soldier *pScan = pTact->pScanUnit;
                    if ((pScan->bFlags & 0x20) != 0) {
                        pScan = pCtx->apXcomUnits[0];
                    }
                    *pCtx->ppSelUnit = pScan;
                    pState->bUnk70103 = 0;
                }
                if (h != NULL && h->pfnProcessActionPhase != NULL) {
                    h->pfnProcessActionPhase(h->pUser);
                }
            }
        }

        /* --- 0x54814: keep ticking while the context is busy and nothing is tracked --- */
        if (pTact->bBusy == 0 || pTact->pTrackUnit != NULL
            || pCtx->pMapTactical->bPhaseActive != 0) {
            return false;
        }
        if (h != NULL && h->pfnGameTickStep != NULL) {
            h->pfnGameTickStep(h->pUser);
        }
        if (pCtx->pMapTactical->bPhaseActive != 0 || *pCtx->pnUiSlot10 != 0) {
            return false;
        }
    }
}
