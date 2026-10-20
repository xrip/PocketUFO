/* pufo_turn.c - the tactical turn state machine and per-phase cursor handling.
 *
 * Evidence: TactCtx272_ctor @ 0x4c9f8, Tact_ProcessActionPhase @ 0x4d950,
 *           Tact_BeginEnemyTurn @ 0x4ca18, Tact_EndEnemyTurn @ 0x4cb74,
 *           Tact_RefreshTeamVisibility @ 0x4f368.
 */
#include "game/pufo_turn.h"

#include <string.h>

#include "game/pufo_boot.h"     /* PUFO_TEXT_REC_STRIDE */
#include "game/pufo_combat.h"   /* Unit_ApplyCellDamage, UnitRosters */
#include "game/pufo_enemyai.h"  /* EnemyAi_TryFireOrWander */
#include "game/pufo_path.h"     /* g_aStepDir, the aDirDeltaXY triples @ 0xa7ae0 */
#include "game/pufo_rmp.h"      /* Rmp_PickSpawnForUnit */
#include "game/pufo_roster.h"   /* SideLists_RemoveUnit */

/* The phase constants moved to pufo_turn.h: Tact_EnemyAiTick @ 0x4cbe0 writes phase 3
 * from its own tail, so the machine's states are no longer private to this file. */

/* PUFO_UNIT_FLAG_CANNOT_ACT moved to pufo_turn.h: the enemy-turn tests assert on it too. */

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
                         const TurnUnitLists *pLists, Soldier *pSelUnit, u8 *pbEnemyTurnActive,
                         void (*pfnFrameStart)(void *pUser),
                         void (*pfnBeforeEnemyStep)(TactCtx272 *pCtx, void *pUser))
{
    i32 i;

    pCtx->pCameraUnit = NULL;
    if (pbEnemyTurnActive != NULL) {
        *pbEnemyTurnActive = 1;                                   /* 0x4CA30 */
    }
    pCtx->bBusy = 1;

    if (pfnFrameStart != NULL) {
        pfnFrameStart(pHooks != NULL ? pHooks->pUser : NULL);     /* Render_Frame (0x22310) stand-in */
    }
    if (pfnBeforeEnemyStep != NULL) {
        pfnBeforeEnemyStep(pCtx, pHooks != NULL ? pHooks->pUser : NULL);
    }
    pCtx->nPhase = PUFO_PHASE_ENEMY_AI;
    if (pHooks != NULL) {
        pHooks->pfnSetPlayerInput(false, pHooks->pUser);          /* 0x4CA58, after the phase write */
    }

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
    }

    /* 0x4CB4C: the save is the CURRENT selection (Map_pSelUnit), not a roster head --
     * the head substitution belongs to the EndEnemyTurn tail. */
    pCtx->pSavedSelUnit = pSelUnit;
    if (pHooks != NULL) {
        Tact_ProcessActionPhase(pCtx, pHooks, &(ActionPhaseSave){ 0, 0, 0 });
    }
}

void Tact_EndEnemyTurn(TactCtx272 *pCtx, const TurnHooks *pHooks, Soldier *pFirstXcom,
                       u8 *pbEnemyTurnActive,
                       void (*pfnAfterEnemyStep)(TactCtx272 *pCtx, void *pUser))
{
    Soldier *pSaved;

    if (pfnAfterEnemyStep != NULL) {
        pfnAfterEnemyStep(pCtx, pHooks != NULL ? pHooks->pUser : NULL);
    }

    if (pbEnemyTurnActive != NULL) {
        *pbEnemyTurnActive = 0;                                   /* 0x4CB88 */
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
 *     *((u8*)pQueue + 56 * nQueueCursor + 80) = 12;   -> aQueue[cursor + 1].nCmd
 *     *((u16*)pQueue + 35050) = LOWORD(cursor) + 2;   -> nQueueLen
 *     *((u8*)pQueue + 1) = 1;                         -> bFlag1
 * The stride is 56 bytes (0x4f380 MUL cursor,56) and the stamp byte is 0x50 =
 * the aQueue base 0x18 plus one whole entry, with nCmd as the first dword of an
 * entry -- so the end mark is APPENDED at cursor + 1, next to the entry the
 * walker is busy with, not written over it. nQueueLen = cursor + 2 covers the
 * same range. 35050 words is byte 70100, the nQueueLen field. Expressed through
 * the struct. (An earlier reading put the +80 inside the cursor's own entry and
 * stamped aQueue[cursor]; the ARM addressing rules that out and the stamp moved
 * here on 2026-10-20.) */
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
        && (u32)(pQueue->nQueueCursor + 1) < (u32)PUFO_TACT_QUEUE_MAX) {
        pQueue->aQueue[pQueue->nQueueCursor + 1].nCmd = TACTCMD_END_MARK;
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

/* --- the enemy-turn bracket -------------------------------------------------
 *
 * Tact_UpdateUnitControl @ 0x4a444, BeforeEnemyStep @ 0x4b9b8 and AfterEnemyStep @ 0x4c4a0.
 * The behaviour contract is in pufo_turn.h. Every branch below was read off the
 * address-tagged ARM rather than the pseudocode, which is wrong in three places that matter:
 * it renders the west/north neighbour cells as `Map.pSelUnit` and `Map.apCivilians[1]`
 * (they are cursor - 4 and cursor - 240), it prints the 0.4 TU top-up multiplier as raw
 * constant-pool dwords, and it writes auStat[8] as `auStat[offsetof(Soldier, uStunOrArmor)]`.
 */

#define PUFO_UNIT_TYPE_HUMAN_MAX 3  /* only these types get the recovery message */
#define PUFO_UNIT_TYPE_XCOM_MAX  8  /* at most this belongs on the XCom side */

/* The fire tick's heat step, picked by MapCell.bFlags36 bit 0 (0x4C10C / 0x4C110). */
#define PUFO_FIRE_STEP_HARD 20
#define PUFO_FIRE_STEP_SOFT 5
/* The power the fire tick hands Unit_ApplyCellDamage (0x4C000, MOV R2, #0x14). */
#define PUFO_FIRE_ITEM_POWER 20
/* The probe counter runs 0, -1, -2 (the loop test at 0x4BCF8 is `R8 > -3`) and is multiplied by
 * the axis delta, so it probes against the axis; the run itself covers at most 7 cells. */
#define PUFO_BURN_BACK_LIMIT (-3)
#define PUFO_BURN_RUN_MAX    7
/* The face counter saturates here (0x4C1F4, CMP R0, #0xFF). */
#define PUFO_FACE_DAMAGE_MAX 255
/* The fire damage base and the divisor applied to it before the roll (0x4BF80..0x4BF90). */
#define PUFO_FIRE_DAMAGE_BASE 20
#define PUFO_FIRE_DAMAGE_ROLL_DIV 4
/* The roll's upper bound is truncated to 16 bits (0x4BF90 `LSL#16` then 0x4BF94 `LSR#16`). */
#define PUFO_FIRE_DAMAGE_ROLL_MASK 0xFFFFu

/* The original's obfuscated "put nNew into the low 3 bits of v": ((nNew ^ v) & 7) ^ v.
 * With nNew = v - 1 that is a decrement of the bitfield. */
static u32 Turn_ReloadLow3(u32 v, u32 nNew)
{
    return ((nNew ^ v) & 7u) ^ v;
}

/* 0x4A544..0x4A594 / 0x4A6C0..0x4A70C: strcpy(pUnit->szName) into a 52-byte stack buffer and
 * then strcat(aTextRecs + 205*nRec). szName is 40 bytes and a text record is 200, so the
 * original can run off the end of that buffer; the port truncates instead. */
static void Turn_PushUnitNameMsg(Soldier *pUnit, i32 nRecMale, i32 nRecFemale, bool bForce,
                                 const EnemyStepCtx *pCtx)
{
    const char *pRec = pCtx->pTextRecs
                     + PUFO_TEXT_REC_STRIDE * ((pUnit->bFemale != 0) ? nRecFemale : nRecMale);
    char szMsg[52];
    size_t nName = strlen(pUnit->szName);
    size_t nRec;

    if (nName > sizeof(szMsg) - 1) {
        nName = sizeof(szMsg) - 1;
    }
    memcpy(szMsg, pUnit->szName, nName);
    szMsg[nName] = '\0';

    nRec = strlen(pRec);
    if (nRec > sizeof(szMsg) - nName - 1) {
        nRec = sizeof(szMsg) - nName - 1;
    }
    memcpy(szMsg + nName, pRec, nRec);
    szMsg[nName + nRec] = '\0';

    (void)StatusMsg_Push(pCtx->pStatus, szMsg, bForce, pCtx->pStatusHooks);
}

/* SideLists carries the two counts by value (see pufo_roster.h), so every caller that
 * removes from it has to write them back to the canonical roster counts. */
static void Turn_SyncSideCounts(const EnemyStepCtx *pCtx, const SideLists *pLists)
{
    *pCtx->pnXcom = pLists->nXcom;
    *pCtx->pnEnemy = pLists->nEnemy;
}

void Tact_UpdateUnitControl(Soldier *pUnit, const EnemyStepCtx *pCtx)
{
    SideLists lists = { pCtx->apXcom, *pCtx->pnXcom, pCtx->apEnemy, *pCtx->pnEnemy };

    /* 0x4A450..0x4A498: the idempotence guard on Soldier+0x1B2, then the HP / bUnk1037 /
     * uTuSpent / morale arithmetic, which Unit_ApplyControlRecovery already owns. The
     * original applies morale last (0x4A714); see the header for why that is neutral here. */
    if (!Unit_ApplyControlRecovery(pUnit)) {
        return;
    }

    /* 0x4A4E0..0x4A5A4. Note bFlags bit 0x20 must be SET here, and CLEAR for both side
     * transfers below -- the original is not consistent, and neither is reproduced away. */
    if (pUnit->uTuSpent < (u32)pUnit->uHp
        && (pUnit->bFlags & PUFO_UNIT_FLAG_CANNOT_ACT) != 0
        && pUnit->pCell != NULL
        && !MapCell_HasOccupant(pUnit->pCell)
        && pUnit->uHp != 0
        && !Unit_bIsLarge(pUnit)) {
        TactState_EnqueueRecover(pCtx->pQueue, pUnit);
        if ((i32)pUnit->nUnitType <= PUFO_UNIT_TYPE_HUMAN_MAX) {
            Turn_PushUnitNameMsg(pUnit, PUFO_TEXT_REC_CAME_TO_M, PUFO_TEXT_REC_CAME_TO_F,
                                 true, pCtx);
        }
        TactState_KickQueue(pCtx->pQueue);
        pUnit->pLinked1040 = NULL;
    }

    /* 0x4A5A8..0x4A648: an XCom-side unit of an enemy type becomes an enemy. */
    if (pUnit->bStatus == 0 && (i32)pUnit->nUnitType > PUFO_UNIT_TYPE_XCOM_MAX
        && (pUnit->bFlags & PUFO_UNIT_FLAG_CANNOT_ACT) == 0) {
        (void)SideLists_RemoveUnit(&lists, pUnit);
        /* SideLists holds the counts by value, so the removal has to be written back before
         * the append reads the target count -- exactly what Fall_SyncCounts does for
         * Tact_ProcessFallAction. The original does it implicitly because
         * Tact_RemoveUnitFromSideLists works on the Map fields themselves. */
        Turn_SyncSideCounts(pCtx, &lists);
        pUnit->bStatus = 1;
        pUnit->uTu = 0;
        /* ORIGINAL BUG (0x4A604): appended at the current count with no bound, so a roster that
         * is already full is written past its end. The XCom side at 0x4A6A4 is the same. */
        pCtx->apEnemy[*pCtx->pnEnemy] = pUnit;
        *pCtx->pnEnemy = (i32)((u8)(*pCtx->pnEnemy + 1));
        if (*pCtx->ppSelUnit == pUnit) {
            Tact_SelectUnit(pCtx->pSelect, pCtx->apXcom[0]);
        }
        pUnit->pLinked1040 = NULL;
    }

    /* 0x4A64C..0x4A710: a controlled unit of an XCom type returns to the XCom roster. */
    if (pUnit->bStatus != 0 && (i32)pUnit->nUnitType <= PUFO_UNIT_TYPE_XCOM_MAX
        && (pUnit->bFlags & PUFO_UNIT_FLAG_CANNOT_ACT) == 0) {
        (void)SideLists_RemoveUnit(&lists, pUnit);
        Turn_SyncSideCounts(pCtx, &lists);
        pUnit->bStatus = 0;
        pUnit->uTu = 0;
        pCtx->apXcom[*pCtx->pnXcom] = pUnit;
        *pCtx->pnXcom = (i32)((u8)(*pCtx->pnXcom + 1));
        Turn_PushUnitNameMsg(pUnit, PUFO_TEXT_REC_RELEASED_M, PUFO_TEXT_REC_RELEASED_F,
                             false, pCtx);
        pUnit->pLinked1040 = NULL;
    }

    Unit_EnqueueIfApSpent(pCtx->pQueue, pUnit, 0);
}

/* The cell lookup the burn walk uses: always the burning cell's own layer, while its bounds
 * tests use layer 0. Both are the original's (0x4BC88 passes nLayer = 0 while 0x4BCB0 indexes
 * with pCell->nZ); harmless because Map_IsValidCell only bounds-checks. */
static MapCell *Turn_CellOnBurnLayer(const MapCell *pBase, i32 nX, i32 nY,
                                     const EnemyStepCtx *pCtx)
{
    return pCtx->pfnLookup(nX, nY, (i32)pBase->nZ, pCtx->pUser);
}

/* The side wall of a cell: wall B when the burn runs along the west/east axis, wall A when it
 * runs along the north/south axis (0x4BCD4/0x4BCD8 and 0x4BDE0/0x4BE24). */
static Mc1Rec *Turn_BurnSideWall(const MapCell *pCell, bool bWestAxis)
{
    return bWestAxis ? pCell->pWallSprB : pCell->pWallSprA;
}

/* 0x4BC2C..0x4BF20: the burn run of one burning cell.
 *
 * The probe multiplies the axis delta by a counter running 0, -1, -2, so with the west axis it
 * steps EAST while the run itself walks west: it finds the far end of the run by probing
 * against the axis, then walks back from `cell + axis * (nBack + 1)` for at most 7 cells. If
 * the walk never stopped on "the side cell holds the same occupant", the burning bit is
 * cleared on the first nRun cells of the run. */
static void Turn_TickBurnRun(MapCell *pCell, const EnemyStepCtx *pCtx)
{
    const bool bWestAxis = (pCell->bFlags29 & PUFO_CELL_FLAG_FIRE_AXIS) != 0;
    const StepDir *pAxis = &g_aStepDir[bWestAxis ? 6 : 0];
    const StepDir *pSide = &g_aStepDir[bWestAxis ? 0 : 6];
    i32 nBack = 0;
    i32 nStartX;
    i32 nStartY;
    i32 nRun = 0;
    bool bSharedOccupant = false;

    for (nBack = 0; nBack > PUFO_BURN_BACK_LIMIT; --nBack) {
        i32 nX = (i32)pCell->nX + (i32)pAxis->dx * nBack;
        i32 nY = (i32)pCell->nY + (i32)pAxis->dy * nBack;
        MapCell *pStep;

        if (!Map_IsValidCell(pCtx->pGeom, nX, nY, 0)) {
            break;
        }
        pStep = Turn_CellOnBurnLayer(pCell, nX, nY, pCtx);
        if (pStep == NULL) {
            break;   /* deviation: the original dereferences the slot */
        }
        {
            Mc1Rec *pWall = Turn_BurnSideWall(pStep, bWestAxis);

            if (pWall == NULL || pWall->bBlocksMove == 0) {
                break;
            }
        }
    }

    nStartX = (i32)pCell->nX + (i32)pAxis->dx * (nBack + 1);
    nStartY = (i32)pCell->nY + (i32)pAxis->dy * (nBack + 1);

    for (nRun = 0; nRun < PUFO_BURN_RUN_MAX; ++nRun) {
        i32 nX = nStartX + (i32)pAxis->dx * nRun;
        i32 nY = nStartY + (i32)pAxis->dy * nRun;
        i32 nSideX = nX + (i32)pSide->dx;
        i32 nSideY = nY + (i32)pSide->dy;
        MapCell *pStep;
        Mc1Rec *pWall;

        if (!Map_IsValidCell(pCtx->pGeom, nX, nY, 0)) {
            break;
        }
        if (!Map_IsValidCell(pCtx->pGeom, nSideX, nSideY, 0)) {
            break;
        }
        pStep = Turn_CellOnBurnLayer(pCell, nX, nY, pCtx);
        if (pStep == NULL) {
            break;   /* deviation: the original dereferences the slot */
        }
        pWall = Turn_BurnSideWall(pStep, bWestAxis);
        if (pWall == NULL || pWall->bBlocksMove == 0) {
            break;
        }
        if (pStep->pUnit != NULL) {
            MapCell *pSideCell = Turn_CellOnBurnLayer(pCell, nSideX, nSideY, pCtx);

            /* 0x4BE40..0x4BE68: the run ends when the side cell holds the SAME occupant
             * pointer. The original reads the side slot without a null check. */
            if (pSideCell != NULL && pSideCell->pUnit == pStep->pUnit) {
                bSharedOccupant = true;
                break;
            }
        }
    }

    if (bSharedOccupant) {
        return;
    }
    for (i32 i = 0; i < nRun; ++i) {
        i32 nX = nStartX + (i32)pAxis->dx * i;
        i32 nY = nStartY + (i32)pAxis->dy * i;
        MapCell *pStep;

        if (!Map_IsValidCell(pCtx->pGeom, nX, nY, 0)) {
            break;
        }
        pStep = Turn_CellOnBurnLayer(pCell, nX, nY, pCtx);
        if (pStep == NULL) {
            break;
        }
        pStep->bFlags29 &= (u8)~PUFO_CELL_FLAG_BURNING;
    }
}

/* 0x4BF24..0x4BFE0: the occupant of a burning cell burns.
 *
 * base = 20 - (1 - abReady252[2] / 100) * (bUnk248 / 3), both divisions signed and integer;
 * the roll is rand(base / 4 rounded toward zero), which the original reaches with the
 * `(x + 3) >> 2` idiom for a possibly negative x (0x4BF84..0x4BF90). */
static void Turn_BurnOccupant(MapCell *pCell, const EnemyStepCtx *pCtx)
{
    Soldier *pUnit = pCell->pUnit;
    i32 nBase = PUFO_FIRE_DAMAGE_BASE
              - (1 - (i32)pUnit->abReady252[2] / 100) * ((i32)pUnit->bUnk248 / 3);
    i32 nRaw = (nBase < 0) ? (nBase + 3) : nBase;
    /* ORIGINAL BUG (0x4BF84): a negative base gets +3 before the arithmetic shift, so the roll
     * range is not base / 4 and can exceed the base; the damage then floors at 0. The mask is
     * the ARM's LSL#16 / LSR#16 truncation to 16 bits. */
    u16 nMax = (u16)((nRaw / PUFO_FIRE_DAMAGE_ROLL_DIV) & PUFO_FIRE_DAMAGE_ROLL_MASK);
    i32 nDmg = nBase - (i32)pCtx->pfnRandU16(nMax, pCtx->pUser);

    if (nDmg < 0) {
        nDmg = 0;
    }
    if (nDmg <= (i32)pUnit->uHp) {
        pUnit->uHp = (u16)((i32)pUnit->uHp - nDmg);
        pUnit->nUnk436 += nDmg;
    } else {
        pUnit->uHp = 0;
    }
    Unit_EnqueueIfApSpent(pCtx->pQueue, pUnit, 0);
}

static void Turn_AddFaceDamage(u8 *pCounter, i32 nSum)
{
    *pCounter = (nSum < PUFO_FACE_DAMAGE_MAX) ? (u8)nSum : (u8)PUFO_FACE_DAMAGE_MAX;
}

/* 0x4C048..0x4C210: the six passes a burning cell makes over its four face counters.
 *
 * Passes 4 and 5 are the WEST and NORTH neighbour cells, gated on that neighbour burning. They
 * have no counter and no sprite of their own -- 0x4C09C and 0x4C0EC rewrite the slot index to
 * 1 and 2, so each one charges the same counter and uses the same sprite as the cell's own wall
 * pass. A wall therefore takes its own hit plus its neighbour's.
 *
 * The value compared against the sprite is counter + step (0x4C10C / 0x4C110), not the step
 * alone, and the store at 0x4C1F0 adds that same value to the counter again -- so a face with
 * no sprite grows as 2 * counter + step. That is the original's arithmetic, reproduced as-is. */
static void Turn_TickCellFaces(MapCell *pCell, i32 nX, i32 nY, i32 nLayer,
                               const EnemyStepCtx *pCtx)
{
    for (i32 nPass = 0; nPass < PUFO_CELL_FACE_PASSES; ++nPass) {
        Mc1Rec *pSpr;
        u8 *pCounter;
        i32 nTotal;
        i32 nSlot = nPass;

        if (nPass == PUFO_CELL_PASS_WEST || nPass == PUFO_CELL_PASS_NORTH) {
            i32 nNx = (nPass == PUFO_CELL_PASS_WEST) ? nX - 1 : nX;
            i32 nNy = (nPass == PUFO_CELL_PASS_NORTH) ? nY - 1 : nY;
            MapCell *pNeigh;

            if (!Map_IsValidCell(pCtx->pGeom, nNx, nNy, nLayer)) {
                continue;
            }
            pNeigh = pCtx->pfnLookup(nNx, nNy, nLayer, pCtx->pUser);
            if (pNeigh == NULL) {
                continue;   /* deviation: the original dereferences the grid slot */
            }
            if ((pNeigh->nFlags32 & PUFO_CELL_FIRE_STATE_MASK) != PUFO_CELL_FIRE_STATE_ON) {
                continue;
            }
            nSlot = (nPass == PUFO_CELL_PASS_WEST) ? PUFO_CELL_FACE_WALL_A
                                                   : PUFO_CELL_FACE_WALL_B;
        }

        switch (nSlot) {
        case PUFO_CELL_FACE_FLOOR:   pSpr = pCell->pFloorSpr;  break;
        case PUFO_CELL_FACE_WALL_A:  pSpr = pCell->pWallSprA;  break;
        case PUFO_CELL_FACE_WALL_B:  pSpr = pCell->pWallSprB;  break;
        default:                     pSpr = pCell->pCeilSpr;   break;
        }
        pCounter = &pCell->abFaceDamage[nSlot];
        nTotal = (i32)*pCounter
               + ((pCell->bFlags36 & 1u) ? PUFO_FIRE_STEP_HARD : PUFO_FIRE_STEP_SOFT);

        /* ORIGINAL BUG (0x4C1F0): the term built at 0x4C10C is counter + step, and the store
         * adds that same term to the counter again, so a face with no sprite grows to
         * 2 * counter + step rather than counter + step. Reproduced as-is. */
        if (pSpr == NULL) {
            Turn_AddFaceDamage(pCounter, (i32)*pCounter + nTotal);
            continue;
        }
        if (nTotal <= (i32)pSpr->bStrength) {
            nTotal = 0;   /* 0x4C160; the comparison below then always skips the slot */
        }
        if (pSpr->bStrength == PUFO_FACE_DAMAGE_MAX) {
            Turn_AddFaceDamage(pCounter, (i32)*pCounter + nTotal);
            continue;
        }
        if ((i32)pSpr->bStrength >= nTotal) {
            continue;
        }
        /* The sprite gives way: replaced by its extra variant, and the counter is NOT
         * touched. The floor sprite only swaps when it has a variant at all (0x4C1D4). */
        if (nSlot == PUFO_CELL_FACE_FLOOR) {
            if (pSpr->bExtraSprFacing != 0) {
                pCell->pFloorSpr = Rec_GetExtraSpr(pSpr->nSlot, pSpr->bExtraSprFacing);
            }
            continue;
        }
        if (nSlot == PUFO_CELL_FACE_WALL_A) {
            pCell->pWallSprA = Rec_GetExtraSpr(pCell->pWallSprA->nSlot,
                                               pCell->pWallSprA->bExtraSprFacing);
            continue;
        }
        if (nSlot == PUFO_CELL_FACE_WALL_B) {
            pCell->pWallSprB = Rec_GetExtraSpr(pCell->pWallSprB->nSlot,
                                               pCell->pWallSprB->bExtraSprFacing);
            continue;
        }
        pCell->pCeilSpr = Rec_GetExtraSpr(pCell->pCeilSpr->nSlot,
                                          pCell->pCeilSpr->bExtraSprFacing);
    }
}

/* One cell of the pre-enemy tick, in the original's order. */
static void Turn_TickCell(MapCell *pCell, i32 nX, i32 nY, i32 nLayer, const EnemyStepCtx *pCtx)
{
    u32 nState;

    if ((pCell->bFlags29 & PUFO_CELL_FLAG_BURNING) != 0) {
        Turn_TickBurnRun(pCell, pCtx);
    }

    nState = pCell->nFlags32 & PUFO_CELL_FIRE_STATE_MASK;

    if (MapCell_HasOccupant(pCell) && nState == PUFO_CELL_FIRE_STATE_ON) {
        Turn_BurnOccupant(pCell, pCtx);
    }
    if (nState == PUFO_CELL_FIRE_STATE_ON) {
        Unit_ApplyCellDamage(pCell, PUFO_FIRE_ITEM_POWER, pCtx->pFireWeapon);
    }

    /* 0x4C004..0x4C02C: the bits 3..6 countdown, which BeforeEnemyStep also refuses to
     * decrement when it already holds its maximum. GameTick_Step @ 0x27324 shares the
     * arithmetic but has no maximum guard. */
    {
        u32 nField = pCell->nFlags40 & 0x78u;

        if (nField != 0 && nField != 0x78u) {
            pCell->nFlags40 = GameTick_DecayCellFlags(pCell->nFlags40);
        }
    }

    if (nState == PUFO_CELL_FIRE_STATE_NONE) {
        pCtx->pfnTickProximity(pCell, pCtx->pUser);
        return;
    }
    if (nState == PUFO_CELL_FIRE_STATE_ON) {
        Turn_TickCellFaces(pCell, nX, nY, nLayer, pCtx);
    }

    /* 0x4C214..0x4C268: the low 3-bit countdown. At zero it advances the burning state in
     * nFlags32 and re-arms itself with rand(2) + 2. */
    if ((pCell->nFlags40 & 7u) != 0) {
        pCell->nFlags40 = Turn_ReloadLow3(pCell->nFlags40, pCell->nFlags40 - 1u);
    } else {
        pCell->nFlags32 = Turn_ReloadLow3(pCell->nFlags32, pCell->nFlags32 - 1u);
        pCell->nFlags40 = Turn_ReloadLow3(pCell->nFlags40,
                                          pCtx->pfnRandU16(2, pCtx->pUser) + 2u);
    }

    pCtx->pfnTickProximity(pCell, pCtx->pUser);
}

void BeforeEnemyStep(TactCtx272 *pTact, const EnemyStepCtx *pCtx)
{
    i32 nXcom = *pCtx->pnXcom;

    pTact->pTrackUnit = NULL;

    for (i32 i = 0; i < nXcom; ++i) {
        pCtx->apXcom[i]->bUnk1037 = 0;
    }

    /* 0x4BA3C..0x4BB48: XCom-base defence pulls reinforcements out of the hidden reserve.
     * The removal is a swap with the last remaining entry, which is why the read at 0x4BAD8
     * is apHiddenSoldiers[nReserveCount - 1] and not a separate table. */
    if (*pCtx->pbMissionType == PUFO_MISSION_TYPE_XCOM_BASE) {
        i32 nWant = (i32)pCtx->pfnRandU16(4, pCtx->pUser) + 2;

        if (*pCtx->pnReserveCount < nWant) {
            nWant = *pCtx->pnReserveCount;
        }
        while (nWant > 0) {
            i32 nReserve = *pCtx->pnReserveCount;
            i32 nPick = (i32)pCtx->pfnRandU16((u32)nReserve, pCtx->pUser);
            Soldier *pUnit = pCtx->ppHidden[nPick];

            pCtx->ppHidden[nPick] = pCtx->ppHidden[nReserve - 1];
            pCtx->apEnemy[*pCtx->pnEnemy] = pUnit;
            *pCtx->pnReserveCount = nReserve - 1;
            *pCtx->pnEnemy = (i32)((u8)(*pCtx->pnEnemy + 1));
            (void)Rmp_PickSpawnForUnit(pCtx->pRmp, pUnit, pCtx->pfnLookup, pCtx->pUser, true);
            --nWant;
        }
    }

    /* 0x4BB5C..0x4BBAC. The count is re-read every iteration, so a unit Tact_UpdateUnitControl
     * moves ONTO the enemy roster is processed in the same pass. */
    for (i32 i = 0; i < *pCtx->pnEnemy; ++i) {
        Soldier *pUnit = pCtx->apEnemy[i];

        pUnit->bControlRecovered = 0;
        Tact_UpdateUnitControl(pUnit, pCtx);
        pUnit->nAiTick = 0;
    }

    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nY = 0; nY < PUFO_CELLS_PER_TILE * (i32)pCtx->pGeom->nDimY; ++nY) {
            for (i32 nX = 0; nX < PUFO_CELLS_PER_TILE * (i32)pCtx->pGeom->nDimX; ++nX) {
                MapCell *pCell = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);

                if (pCell == NULL) {
                    continue;   /* deviation: the original dereferences the slot */
                }
                Turn_TickCell(pCell, nX, nY, nLayer, pCtx);
            }
        }
    }

    for (i32 i = 0; i < *pCtx->pnCiv; ++i) {
        pCtx->apCiv[i]->bControlRecovered = 0;
    }
    for (i32 i = 0; i < *pCtx->pnXcom; ++i) {
        pCtx->apXcom[i]->bControlRecovered = 0;
    }

    /* 0x4C38C..0x4C3D4: every enemy refills from its own stat block. */
    for (i32 i = 0; i < *pCtx->pnEnemy; ++i) {
        Soldier *pUnit = pCtx->apEnemy[i];

        pUnit->uTu = pUnit->auStat[0];
        pUnit->uStunOrArmor = pUnit->auStat[9];
    }

    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nY = 0; nY < PUFO_CELLS_PER_TILE * (i32)pCtx->pGeom->nDimY; ++nY) {
            for (i32 nX = 0; nX < PUFO_CELLS_PER_TILE * (i32)pCtx->pGeom->nDimX; ++nX) {
                MapCell *pCell = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);

                if (pCell != NULL) {
                    pCell->bFlags44 &= (u8)~1u;
                }
            }
        }
    }

    pCtx->pfnRefreshVisibility(pCtx->pUser);
}

void AfterEnemyStep(TactCtx272 *pTact, const EnemyStepCtx *pCtx)
{
    const i32 nEnemy = *pCtx->pnEnemy;

    (void)pTact;   /* the original touches no field of the context here */

    /* 0x4C4B8..0x4C594: nEnemy / 3 random enemies are topped up to 40% of their max TU. The
     * multiplier is the constant pair 0x3FD99999 / 0x9999999A at 0x4C520/0x4C528, i.e. 0.4;
     * Hex-Rays shows the raw halves, which is easy to misread as 0.2. */
    for (i32 i = 0; i < nEnemy / PUFO_ENEMYSTEP_TU_TOPUP_DIV; ++i) {
        Soldier *pUnit = pCtx->apEnemy[(i32)pCtx->pfnRandU16((u32)nEnemy, pCtx->pUser)];
        double dCap = (double)pUnit->auStat[0] * PUFO_ENEMYSTEP_TU_TOPUP_SCALE;

        if ((double)pUnit->uTu < dCap) {
            pUnit->uTu = (u16)dCap;
        }
    }

    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nY = 0; nY < PUFO_CELLS_PER_TILE * (i32)pCtx->pGeom->nDimY; ++nY) {
            for (i32 nX = 0; nX < PUFO_CELLS_PER_TILE * (i32)pCtx->pGeom->nDimX; ++nX) {
                MapCell *pCell = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);

                if (pCell == NULL) {
                    continue;
                }
                pCtx->pfnTickProximity(pCell, pCtx->pUser);
                pCell->bFlags44 &= (u8)~1u;
            }
        }
    }

    /* 0x4C6A4..0x4C6F4: the action item an enemy carried in its right hand goes back to the
     * belt. Types 0..3 keep theirs. */
    for (i32 i = 0; i < nEnemy; ++i) {
        Soldier *pUnit = pCtx->apEnemy[i];

        if (pUnit->pHandR != NULL && (i32)pUnit->nUnitType > PUFO_UNIT_TYPE_HUMAN_MAX) {
            TactItem *pItem = pUnit->pHandR;

            pItem->bUnk8 = 0;
            pUnit->pHandR = NULL;
            pUnit->apBelt[0] = pItem;
        }
    }

    for (i32 i = 0; i < nEnemy; ++i) {
        pCtx->apEnemy[i]->bUnk148 = 0;
    }

    /* 0x4C73C..0x4C82C: the XCom roster refills, then pays the burden penalty.
     *
     * The penalty's grouping is the original's, read off 0x4C77C..0x4C7C0:
     *     q = (4 * uInvBurden - 4) / auStat[8]      (signed _rt_sdiv, so a zero burden
     *                                                 gives a negative quotient)
     *     d = (2 * q * auStat[0]) / 10
     *     uTu = auStat[0] - d
     * auStat[8] is the mean-stun stat, NOT auStat[9]; Hex-Rays prints it as
     * auStat[offsetof(Soldier, uStunOrArmor)]. The port guards both divisors, which the
     * original does not. */
    for (i32 i = 0; i < *pCtx->pnXcom; ++i) {
        Soldier *pUnit = pCtx->apXcom[i];

        pUnit->uTu = pUnit->auStat[0];
        if (pUnit->auStat[8] != 0) {
            i32 q = ((4 * (i32)pUnit->uInvBurden - 4) / (i32)pUnit->auStat[8]);

            pUnit->uTu = (u16)((i32)pUnit->auStat[0] - (2 * q * (i32)pUnit->auStat[0]) / 10);
        }
        if (pUnit->uInvBurden != 0) {
            pUnit->uStunOrArmor = (u16)((i32)pUnit->uStunOrArmor
                                        + ((i32)pUnit->auStat[9] * (i32)pUnit->auStat[8])
                                          / (10 * (i32)pUnit->uInvBurden));
        }
        if (pUnit->uStunOrArmor > pUnit->auStat[9]) {
            pUnit->uStunOrArmor = pUnit->auStat[9];
        }
    }

    for (i32 i = 0; i < *pCtx->pnXcom; ++i) {
        Tact_UpdateUnitControl(pCtx->apXcom[i], pCtx);
    }

    pCtx->pfnRefreshCells(pCtx->pUser);
    pCtx->pfnRefreshVisibility(pCtx->pUser);

    for (i32 i = 0; i < *pCtx->pnCiv; ++i) {
        Tact_UpdateUnitControl(pCtx->apCiv[i], pCtx);
    }

    /* 0x4C8C0..0x4C904: a low-morale XCom unit falls back to the panic AI. The count is
     * re-read after each call because the panic AI can move units between rosters. */
    for (i32 i = 0; i < *pCtx->pnXcom; ++i) {
        Soldier *pUnit = pCtx->apXcom[i];

        if (pUnit->bMorale < PUFO_ENEMYAI_MORALE_MAX) {
            EnemyAi_TryFireOrWander(pUnit, pCtx->pActCtx);
        }
    }

    /* 0x4C8D4: every enemy's visibility is refreshed. A reinforcement unit the spawn pick never
     * placed stands in the list with no cell; Unit_UpdateVisibility @ 0x48580 reads its cell as
     * its first act, so the original faults on such a unit here. Deviation: the port skips it
     * (same latent fault as in the enemy AI tick guard and the two visibility scans). */
    for (i32 i = 0; i < *pCtx->pnEnemy; ++i) {
        if (pCtx->apEnemy[i]->pCell != NULL) {
            pCtx->pfnUpdateVisibility(pCtx->apEnemy[i], pCtx->pUser);
        }
    }

    TactState_KickQueue(pCtx->pQueue);
    pCtx->pfnRefreshCells(pCtx->pUser);
    pCtx->pfnRefreshVisibility(pCtx->pUser);

    for (i32 i = 0; i < *pCtx->pnXcom; ++i) {
        pCtx->apXcom[i]->bUnk1037 = 0;
    }
}
