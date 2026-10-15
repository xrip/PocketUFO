/* pufo_turn.c - the tactical turn state machine and per-phase cursor handling.
 *
 * Evidence: TactCtx272_ctor @ 0x4c9f8, Tact_ProcessActionPhase @ 0x4d950,
 *           Tact_BeginEnemyTurn @ 0x4ca18, Tact_EndEnemyTurn @ 0x4cb74,
 *           Tact_RefreshTeamVisibility @ 0x4f368.
 */
#include "game/pufo_turn.h"

#include <string.h>

/* The phase constants moved to pufo_turn.h: Tact_EnemyAiTick @ 0x4cbe0 writes phase 3
 * from its own tail, so the machine's states are no longer private to this file. */

/* The flag that makes a unit ineligible to act, from the opportunity-fire walk
 * (`(bFlags & 0x20) == 0` continues; set means skip). */
#define PUFO_UNIT_FLAG_CANNOT_ACT 0x20

void TactCtx_ctor(TactCtx272 *pCtx)
{
    memset(pCtx, 0, sizeof(*pCtx));
}

/* The opportunity-fire walk, shared by three arms of the machine.
 *
 * The original, verbatim:
 *     while (1) {
 *         n = pCtx->nOppFireCount;
 *         if ((*(_BYTE *)(*(&pCtx->pScanUnit + n) + 4) & 0x20) == 0) break;
 *         n = n - 1;
 *         pCtx->nOppFireCount = n;
 *         if (n <= 0) goto exhausted;
 *     }
 *
 * So it starts by indexing at the CURRENT count (one past the last valid entry,
 * since the count is decremented after each use elsewhere), and walks down while
 * the indexed unit has the cannot-act flag set. On reaching 0 it exits without
 * firing. Reproduced exactly; the index-vs-count mismatch is the original's.
 *
 * Returns the unit to fire with, or NULL when the list is exhausted. */
static Soldier *Turn_PickOppFireUnit(TactCtx272 *pCtx)
{
    for (;;) {
        i32      n = pCtx->nOppFireCount;
        Soldier *pUnit;

        if (n < 0 || n > PUFO_OPPFIRE_MAX) {
            return NULL;
        }
        /* `*(&pCtx->pScanUnit + n)` is the array base plus n entries, so index 0
         * is pScanUnit itself and index 1 is apOppFire[0]. */
        pUnit = (n == 0) ? pCtx->pScanUnit : pCtx->apOppFire[n - 1];
        if (pUnit == NULL) {
            return NULL;
        }
        if ((pUnit->bFlags & PUFO_UNIT_FLAG_CANNOT_ACT) == 0) {
            return pUnit;
        }
        n -= 1;
        pCtx->nOppFireCount = n;
        if (n <= 0) {
            return NULL;
        }
    }
}

/* The fire arm, shared by the busy=0/phase=1 and busy=1/phase=1 paths.
 *
 * IMPORTANT asymmetry, straight from the decompilation: the UNIT fired with is
 * `*(&pCtx->pScanUnit + nOppFireCount)` (the picked entry), but the COORDINATES
 * and the facing target come from `pCtx->pScanUnit->pCell` -- i.e. always index 0,
 * not the picked unit's own cell. Reproduced, because using the picked unit's
 * cell instead would aim the shot somewhere the original never aims it. */
static void Turn_FireAtOppFireTarget(TactCtx272 *pCtx, const TurnHooks *pHooks)
{
    Soldier *pUnit = Turn_PickOppFireUnit(pCtx);
    MapCell *pCell;
    i32      nX;
    i32      nY;
    i32      nZ;

    if (pUnit == NULL || pCtx->pScanUnit == NULL || pCtx->pScanUnit->pCell == NULL) {
        return;
    }

    pCell = pCtx->pScanUnit->pCell;
    nX = pCell->nX;
    nY = pCell->nY;
    nZ = pCell->nZ;

    /* Face toward the same cell: a no-op rotation, but it is what the call sites
     * do and it feeds the animation queue. */
    (void)Unit_FacingToward(nX, nY, nX, nY);

    if (pHooks->pfnSelectUnit != NULL) {
        pHooks->pfnSelectUnit(pUnit, pHooks->pUser);
    }
    if (pHooks->pfnSetCursor != NULL) {
        pHooks->pfnSetCursor(-2, -2, nZ, pHooks->pUser);
    }
    if (pHooks->pfnRequestFire != NULL) {
        pHooks->pfnRequestFire(pUnit, nX, nY, nZ, pHooks->pUser);
    }
}

/* Tact_ProcessActionPhase @ 0x4d950.
 *
 * State machine over bBusy x nPhase. See the header for the state table. */
void Tact_ProcessActionPhase(TactCtx272 *pCtx, const TurnHooks *pHooks,
                             ActionPhaseSave *pSave)
{
    if (pHooks == NULL || pSave == NULL) {
        return;
    }

    if (pCtx->bBusy != 0) {
        if (pCtx->nPhase == PUFO_PHASE_OPP_FIRE) {
            /* Save the cursor, fire or run out, then advance to phase 2. */
            pHooks->pfnReadCursor(&pSave->nCursorX, &pSave->nCursorY, &pSave->nLayer,
                                  pHooks->pUser);
            Turn_FireAtOppFireTarget(pCtx, pHooks);
            pCtx->nOppFireCount -= 1;
            if (pCtx->nOppFireCount + 1 == 1) {
                pCtx->nPhase = PUFO_PHASE_RESTORE;
            }
            return;
        }

        if (pCtx->nPhase == PUFO_PHASE_RESTORE) {
            /* Restore the cursor and layer, then move to the enemy AI phase. */
            if (pHooks->pfnSetCursor != NULL) {
                pHooks->pfnSetCursor(pSave->nCursorX, pSave->nCursorY, pSave->nLayer,
                                     pHooks->pUser);
            }
            pCtx->nPhase = PUFO_PHASE_ENEMY_AI;
        } else if (pCtx->nPhase != PUFO_PHASE_ENEMY_AI) {
            return;
        }

        if (pHooks->pfnEnemyAiTick != NULL) {
            pHooks->pfnEnemyAiTick(pCtx, pHooks->pUser);
        }
        return;
    }

    /* Not busy. */
    if (pCtx->nPhase == PUFO_PHASE_RESTORE) {
        if (pHooks->pfnSetPlayerInput != NULL) {
            pHooks->pfnSetPlayerInput(true, pHooks->pUser);
        }
        if (pHooks->pfnSetCursor != NULL) {
            pHooks->pfnSetCursor(pSave->nCursorX, pSave->nCursorY, pSave->nLayer,
                                 pHooks->pUser);
        }
        pCtx->nPhase = PUFO_PHASE_NONE;
        return;
    }
    if (pCtx->nPhase != PUFO_PHASE_OPP_FIRE) {
        return;
    }

    /* Player opportunity fire: disable input, save the cursor, fire. */
    if (pHooks->pfnSetPlayerInput != NULL) {
        pHooks->pfnSetPlayerInput(false, pHooks->pUser);
    }
    pHooks->pfnReadCursor(&pSave->nCursorX, &pSave->nCursorY, &pSave->nLayer,
                          pHooks->pUser);

    Turn_FireAtOppFireTarget(pCtx, pHooks);

    pCtx->nOppFireCount -= 1;
    if (pCtx->nOppFireCount + 1 == 1) {
        pCtx->nPhase = PUFO_PHASE_RESTORE;
        if (pHooks->pfnEnqueueEndOfOppFire != NULL) {
            pHooks->pfnEnqueueEndOfOppFire(pHooks->pUser);
        }
    }
}

void Tact_BeginEnemyTurn(TactCtx272 *pCtx, const TurnHooks *pHooks,
                         const TurnUnitLists *pLists,
                         void (*pfnFrameStart)(void *pUser),
                         void (*pfnBeforeEnemyStep)(TactCtx272 *pCtx, void *pUser))
{
    i32 i;

    pCtx->pCameraUnit = NULL;
    if (pHooks != NULL) {
        pHooks->pfnSetPlayerInput(false, pHooks->pUser);
    }
    pCtx->bBusy = 1;

    if (pfnFrameStart != NULL) {
        pfnFrameStart(pHooks != NULL ? pHooks->pUser : NULL);
    }
    if (pfnBeforeEnemyStep != NULL) {
        pfnBeforeEnemyStep(pCtx, pHooks != NULL ? pHooks->pUser : NULL);
    }
    pCtx->nPhase = PUFO_PHASE_ENEMY_AI;

    /* Clear per-unit AI and spotting state, in the original's order: enemy,
     * then civilian, then XCom. */
    if (pLists != NULL) {
        for (i = 0; i < pLists->nEnemy && pLists->apEnemy != NULL; ++i) {
            if (pLists->apEnemy[i] != NULL) {
                pLists->apEnemy[i]->bAiDone = 0;
                pLists->apEnemy[i]->nSpottedCount = 0;
            }
        }
        for (i = 0; i < pLists->nCivilian && pLists->apCivilian != NULL; ++i) {
            if (pLists->apCivilian[i] != NULL) {
                pLists->apCivilian[i]->bAiDone = 0;
                pLists->apCivilian[i]->nSpottedCount = 0;
            }
        }
        for (i = 0; i < pLists->nXcom && pLists->apXcom != NULL; ++i) {
            if (pLists->apXcom[i] != NULL) {
                pLists->apXcom[i]->bAiDone = 0;
                pLists->apXcom[i]->nSpottedCount = 0;
            }
        }
        if (pLists->nXcom > 0 && pLists->apXcom != NULL) {
            pCtx->pSavedSelUnit = pLists->apXcom[0];
        }
    }

    /* The original saves the selected unit and then runs the action phase. */
    if (pHooks != NULL) {
        if (pHooks->pfnSelectUnit != NULL && pCtx->pSavedSelUnit != NULL) {
            /* Selection is left to the phase machine; only the save is required
             * here. */
        }
        Tact_ProcessActionPhase(pCtx, pHooks, &(ActionPhaseSave){ 0, 0, 0 });
    }
}

void Tact_EndEnemyTurn(TactCtx272 *pCtx, const TurnHooks *pHooks, Soldier *pFirstXcom,
                       void (*pfnAfterEnemyStep)(TactCtx272 *pCtx, void *pUser))
{
    Soldier *pSaved;

    if (pfnAfterEnemyStep != NULL) {
        pfnAfterEnemyStep(pCtx, pHooks != NULL ? pHooks->pUser : NULL);
    }

    pCtx->bBusy  = 0;
    pCtx->nPhase = PUFO_PHASE_NONE;
    if (pHooks != NULL) {
        pHooks->pfnSetPlayerInput(true, pHooks->pUser);
    }

    /* A dead or flagged saved unit is replaced by the first XCom unit. */
    pSaved = pCtx->pSavedSelUnit;
    if (pSaved == NULL
        || (pSaved->bFlags & PUFO_UNIT_FLAG_CANNOT_ACT) != 0
        || !Unit_bIsOperative(pSaved)) {
        pCtx->pSavedSelUnit = pFirstXcom;
    }
    if (pHooks != NULL) {
        if (pHooks->pfnSelectUnit != NULL && pCtx->pSavedSelUnit != NULL) {
            pHooks->pfnSelectUnit(pCtx->pSavedSelUnit, pHooks->pUser);
        }
        if (pHooks->pfnFocusSelected != NULL) {
            pHooks->pfnFocusSelected(pHooks->pUser);
        }
    }
}

/* Tact_RefreshTeamVisibility @ 0x4f368.
 *
 * The three queue writes come straight from the decompilation:
 *     *((u8*)pQueue + 56 * nQueueCursor + 80) = 12;   -> aQueue[cursor].nCmd
 *     *((u16*)pQueue + 35050) = nQueueLen + 2;        -> nQueueLen
 *     *((u8*)pQueue + 1) = 1;                         -> bFlag1
 * The 56-byte stride and the +80 offset are the queue entry layout, and 35050
 * words is byte 70100, i.e. the nQueueLen field. Expressed through the struct. */
void Tact_RefreshTeamVisibility(TactState *pQueue, Soldier **apXcom, i32 nXcom,
                                TurnUpdateVisibilityFn pfnUpdateVisibility,
                                void *pUser)
{
    i32 i;

    if (pQueue == NULL) {
        return;
    }
    /* Index 0 of aQueue is at offset 0x18 in the original's TactState, not 0, so
     * cursor addressing uses the entry array directly. */
    if (pQueue->nQueueCursor >= 0
        && (u32)pQueue->nQueueCursor < (u32)PUFO_TACT_QUEUE_MAX) {
        pQueue->aQueue[pQueue->nQueueCursor].nCmd = TACTCMD_END_MARK;
    }
    pQueue->nQueueLen = (u16)(pQueue->nQueueCursor + 2);
    pQueue->bFlag1    = 1;

    if (apXcom == NULL || pfnUpdateVisibility == NULL) {
        return;
    }
    for (i = 0; i < nXcom; ++i) {
        if (apXcom[i] != NULL) {
            pfnUpdateVisibility(apXcom[i], pUser);
        }
    }
}

/* Unit_bScanOpportunityFire @ 0x48028. */
bool Unit_bScanOpportunityFire(TactCtx272 *pCtx, Soldier *pUnit,
                               Soldier *const *apEnemy, const u8 *pnEnemyCount,
                               Soldier *const *apXcom, const u8 *pnXcomCount,
                               void (*pfnTryOpportunityFire)(Soldier *pTarget,
                                                             Soldier *pShooter,
                                                             void *pUser),
                               void *pUser)
{
    i32 v = 0;

    if (pCtx == NULL) {
        return false;
    }
    pCtx->nOppFireCount = 0;
    pCtx->pScanUnit = pUnit;

    if (Unit_bIsOperative(pUnit)) {
        i32 nCount = (pnEnemyCount != NULL) ? *pnEnemyCount : 0;

        while (v < nCount) {
            Soldier *pShooter = apEnemy[v];
            if (pShooter != NULL && pShooter->bMorale > 0x1E) {
                if (pfnTryOpportunityFire != NULL) {
                    pfnTryOpportunityFire(pUnit, pShooter, pUser);
                }
                nCount = (pnEnemyCount != NULL) ? *pnEnemyCount : 0;
            }
            ++v;
        }
    } else {
        i32 nCount = (pnXcomCount != NULL) ? *pnXcomCount : 0;

        while (v < nCount) {
            Soldier *pShooter = apXcom[v];
            if (pShooter != NULL && pShooter->bMorale > 0x1E) {
                if (pfnTryOpportunityFire != NULL) {
                    pfnTryOpportunityFire(pUnit, pShooter, pUser);
                }
                nCount = (pnXcomCount != NULL) ? *pnXcomCount : 0;
            }
            ++v;
        }
    }
    return pCtx->nOppFireCount > 0;
}
