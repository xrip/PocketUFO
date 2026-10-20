/* test_turn.c - checks the tactical turn state machine.
 *
 * The opportunity-fire walk is the part worth pinning: it indexes at the CURRENT
 * count (one past the last valid entry) and walks down, which looks like an
 * off-by-one and is the original's actual behaviour.
 */
#include "game/pufo_turn.h"
#include "game/pufo_boot.h"   /* PUFO_TEXT_REC_STRIDE */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

/* --- recording hooks --- */

typedef struct Rec {
    i32 nInputCalls;
    bool bLastInput;
    i32 nCursorSetCalls;
    i32 nLastCursorX;
    i32 nLastCursorY;
    i32 nLastLayer;
    i32 nSelectCalls;
    Soldier *pLastSelected;
    i32 nFocusCalls;
    i32 nFireCalls;
    Soldier *pLastFired;
    i32 nX;
    i32 nY;
    i32 nZ;
    i32 nAiTickCalls;
    i32 nEndOppFireCalls;
    i32 nVisibilityCalls;
    /* cursor readback */
    i32 nReadX;
    i32 nReadY;
    i32 nReadLayer;
} Rec;

static void Rec_SetInput(bool bEnabled, void *pUser)
{
    Rec *p = (Rec *)pUser;
    p->nInputCalls += 1;
    p->bLastInput = bEnabled;
}
static void Rec_SetCursor(i32 x, i32 y, i32 l, void *pUser)
{
    Rec *p = (Rec *)pUser;
    p->nCursorSetCalls += 1;
    p->nLastCursorX = x;
    p->nLastCursorY = y;
    p->nLastLayer = l;
}
static void Rec_ReadCursor(i32 *px, i32 *py, i32 *pl, void *pUser)
{
    Rec *p = (Rec *)pUser;
    *px = p->nReadX;
    *py = p->nReadY;
    *pl = p->nReadLayer;
}
static void Rec_Select(Soldier *pUnit, void *pUser)
{
    Rec *p = (Rec *)pUser;
    p->nSelectCalls += 1;
    p->pLastSelected = pUnit;
}
static void Rec_Focus(void *pUser) { ((Rec *)pUser)->nFocusCalls += 1; }
static void Rec_Fire(Soldier *pUnit, i32 x, i32 y, i32 z, void *pUser)
{
    Rec *p = (Rec *)pUser;
    p->nFireCalls += 1;
    p->pLastFired = pUnit;
    p->nX = x;
    p->nY = y;
    p->nZ = z;
}
static void Rec_AiTick(TactCtx272 *pCtx, void *pUser)
{
    (void)pCtx;
    ((Rec *)pUser)->nAiTickCalls += 1;
}
static void Rec_EndOppFire(void *pUser) { ((Rec *)pUser)->nEndOppFireCalls += 1; }
static void Rec_Visibility(Soldier *pUnit, void *pUser)
{
    (void)pUnit;
    ((Rec *)pUser)->nVisibilityCalls += 1;
}

/* Builds a hook set bound to `pRec`. pUser MUST be non-NULL: every recording
 * hook dereferences it. */
static TurnHooks MakeHooks(Rec *pRec)
{
    TurnHooks h;

    h.pfnSetPlayerInput        = Rec_SetInput;
    h.pfnSetCursor             = Rec_SetCursor;
    h.pfnReadCursor            = Rec_ReadCursor;
    h.pfnSelectUnit            = Rec_Select;
    h.pfnFocusSelected         = Rec_Focus;
    h.pfnRequestFire           = Rec_Fire;
    h.pfnEnemyAiTick           = Rec_AiTick;
    h.pfnEnqueueEndOfOppFire   = Rec_EndOppFire;
    h.pUser                    = pRec;
    return h;
}

static void TestLayout(void)
{
    printf("TactCtx272 layout\n");
    Check(offsetof(TactCtx272, pScanUnit) == 0x000, "  pScanUnit at 0x000");

    /* TactCtx272 is pointer-heavy, so every offset past the first pointer shifts
     * on Win64. The IDB reports the ILP32 layout, so the exact numbers are
     * asserted there and the field ORDER is asserted everywhere. */
#if UINTPTR_MAX == 0xFFFFFFFFu
    Check(offsetof(TactCtx272, apOppFire) == 0x004, "  apOppFire at 0x004 (ILP32)");
    Check(offsetof(TactCtx272, nOppFireCount) == 0x0F4, "  nOppFireCount at 0x0F4 (ILP32)");
    Check(offsetof(TactCtx272, bBusy) == 0x0F8, "  bBusy at 0x0F8 (ILP32)");
    Check(offsetof(TactCtx272, nPhase) == 0x0FC, "  nPhase at 0x0FC (ILP32)");
    Check(offsetof(TactCtx272, bAiFocusSaved) == 0x100, "  bAiFocusSaved at 0x100 (ILP32)");
    /* +0x100 is one byte wide: the original writes it with STRB and the next named
     * field starts at 0x104, so three pad bytes sit between them. */
    Check(sizeof(((TactCtx272 *)0)->bAiFocusSaved) == 1, "  bAiFocusSaved is one byte");
    Check(offsetof(TactCtx272, pSavedSelUnit) - offsetof(TactCtx272, bAiFocusSaved) == 4,
          "  pSavedSelUnit is 4 bytes past bAiFocusSaved (ILP32)");
    Check(offsetof(TactCtx272, pSavedSelUnit) == 0x104, "  pSavedSelUnit at 0x104 (ILP32)");
    Check(offsetof(TactCtx272, pCameraUnit) == 0x108, "  pCameraUnit at 0x108 (ILP32)");
    Check(offsetof(TactCtx272, pTrackUnit) == 0x10C, "  pTrackUnit at 0x10C (ILP32)");
    Check(sizeof(TactCtx272) == 272, "  sizeof is 272 (ILP32)");
#else
    Check(offsetof(TactCtx272, apOppFire) == offsetof(TactCtx272, pScanUnit) + sizeof(void *),
          "  apOppFire directly after pScanUnit");
    Check(offsetof(TactCtx272, nOppFireCount) > offsetof(TactCtx272, apOppFire),
          "  nOppFireCount after the array");
    Check(offsetof(TactCtx272, bBusy) < offsetof(TactCtx272, nPhase), "  bBusy before nPhase");
    Check(offsetof(TactCtx272, nPhase) < offsetof(TactCtx272, bAiFocusSaved),
          "  nPhase before bAiFocusSaved");
    Check(sizeof(((TactCtx272 *)0)->bAiFocusSaved) == 1, "  bAiFocusSaved is one byte");
    Check(offsetof(TactCtx272, bAiFocusSaved) < offsetof(TactCtx272, pSavedSelUnit),
          "  bAiFocusSaved before pSavedSelUnit");
    Check(offsetof(TactCtx272, pSavedSelUnit) < offsetof(TactCtx272, pCameraUnit),
          "  pSavedSelUnit before pCameraUnit");
    Check(offsetof(TactCtx272, pCameraUnit) < offsetof(TactCtx272, pTrackUnit),
          "  pCameraUnit before pTrackUnit");
#endif
}

static void TestCtor(void)
{
    TactCtx272 ctx;

    printf("TactCtx_ctor\n");
    memset(&ctx, 0xFF, sizeof(ctx));
    TactCtx_ctor(&ctx);
    CheckEqI32(ctx.nOppFireCount, 0, "  count cleared");
    CheckEqI32(ctx.nPhase, 0, "  phase cleared");
    CheckEqI32(ctx.bBusy, 0, "  busy cleared");
    Check(ctx.pScanUnit == NULL, "  scan unit cleared");
}

/* Phase machine: the transitions, and that the cursor round-trips. */
static void TestPhaseMachine(void)
{
    TurnHooks      hooks;
    TactCtx272     ctx;
    ActionPhaseSave save;
    Rec            rec;
    Soldier        unit;
    static MapCell s_cell;

    printf("Tact_ProcessActionPhase (busy, phase 1 -> 2 -> 3)\n");
    memset(&ctx, 0, sizeof(ctx));
    memset(&rec, 0, sizeof(rec));
    hooks = MakeHooks(&rec);
    memset(&save, 0, sizeof(save));
    memset(&unit, 0, sizeof(unit));
    memset(&s_cell, 0, sizeof(s_cell));
    /* The fire coordinates come from pScanUnit->pCell, not the picked unit's
     * cell (a verified asymmetry of the original), so it must be set. */
    s_cell.nX = 3;
    s_cell.nY = 4;
    s_cell.nZ = 1;
    unit.pCell = &s_cell;

    /* One opportunity-fire entry. The walk indexes at nOppFireCount, so with a
     * count of 1 it reads apOppFire[0]. */
    /* Count 1 indexes apOppFire[0], so the fire target lives there. */
    ctx.pScanUnit     = &unit;
    ctx.apOppFire[0]  = &unit;
    ctx.nOppFireCount = 1;
    ctx.bBusy = 1;
    ctx.nPhase = 1;
    rec.nReadX = 7;
    rec.nReadY = 9;
    rec.nReadLayer = 2;

    Tact_ProcessActionPhase(&ctx, &hooks, &save);
    CheckEqI32(rec.nFireCalls, 1, "  fired once");
    Check(rec.pLastFired == &unit, "  fired at the scan unit");
    CheckEqI32(rec.nCursorSetCalls, 1, "  cursor parked during the fire");
    CheckEqI32(rec.nLastCursorX, -2, "  cursor parked off-map");
    CheckEqI32(ctx.nPhase, 2, "  advanced to phase 2");
    CheckEqI32(ctx.nOppFireCount, 0, "  count decremented");

    printf("Tact_ProcessActionPhase (phase 2 restores, moves to 3, AND ticks)\n");
    Tact_ProcessActionPhase(&ctx, &hooks, &save);
    CheckEqI32(rec.nLastCursorX, 7, "  cursor restored to X");
    CheckEqI32(rec.nLastCursorY, 9, "  cursor restored to Y");
    CheckEqI32(rec.nLastLayer, 2, "  layer restored");
    CheckEqI32(ctx.nPhase, 3, "  advanced to phase 3");
    /* The original has NO return between the phase-2 restore and the AI tick:
     * `if (nPhase == 2) {...} else if (nPhase != 3) return;` then unconditionally
     * `Tact_EnemyAiTick(pCtx);`. So phase 2 ALSO ticks. Reproduced. */
    CheckEqI32(rec.nAiTickCalls, 1, "  phase 2 also falls through to the AI tick");

    printf("Tact_ProcessActionPhase (phase 3 runs the AI tick)\n");
    Tact_ProcessActionPhase(&ctx, &hooks, &save);
    CheckEqI32(rec.nAiTickCalls, 2, "  phase 3 ticks as well");

    printf("Tact_ProcessActionPhase (busy, phase 0 does nothing)\n");
    ctx.nPhase = 0;
    rec.nAiTickCalls = 0;
    Tact_ProcessActionPhase(&ctx, &hooks, &save);
    CheckEqI32(rec.nAiTickCalls, 0, "  no AI tick at phase 0");

    /* Not busy, phase 2: re-enable input and clear the phase. */
    printf("Tact_ProcessActionPhase (not busy, phase 2)\n");
    memset(&rec, 0, sizeof(rec));
    hooks = MakeHooks(&rec);
    ctx.bBusy = 0;
    ctx.nPhase = 2;
    Tact_ProcessActionPhase(&ctx, &hooks, &save);
    CheckEqI32(rec.nInputCalls, 1, "  input re-enabled");
    Check(rec.bLastInput, "  input enabled (not disabled)");
    CheckEqI32(ctx.nPhase, 0, "  phase cleared");
}

/* The walk must skip units flagged 0x20 and stop when exhausted. */
static void TestOppFireWalk(void)
{
    TurnHooks      hooks;
    TactCtx272     ctx;
    ActionPhaseSave save;
    Rec            rec;
    Soldier        aUnits[3];

    printf("Opportunity-fire walk skips flagged units\n");
    memset(&ctx, 0, sizeof(ctx));
    memset(&rec, 0, sizeof(rec));
    hooks = MakeHooks(&rec);
    memset(&save, 0, sizeof(save));
    memset(aUnits, 0, sizeof(aUnits));

    /* Count 3: index 3 -> apOppFire[2] flagged, index 2 -> apOppFire[1] flagged,
     * index 1 -> apOppFire[0] clean, so apOppFire[0] is the target. */
    /* The walk indexes at the COUNT, so count 3 reads apOppFire[2] first, then
     * [1], then [0]. The last two are flagged, so apOppFire[0] is the target. */
    ctx.pScanUnit     = &aUnits[0];
    ctx.apOppFire[0]  = &aUnits[0];
    ctx.apOppFire[1]  = &aUnits[1];
    ctx.apOppFire[2]  = &aUnits[2];
    ctx.nOppFireCount = 3;
    aUnits[1].bFlags  = 0x20;
    aUnits[2].bFlags  = 0x20;
    /* Give the target a cell so the fire path has coordinates. */
    {
        static MapCell s_cell;

        memset(&s_cell, 0, sizeof(s_cell));
        s_cell.nX = 4;
        s_cell.nY = 5;
        s_cell.nZ = 1;
        aUnits[0].pCell = &s_cell;
    }

    ctx.bBusy = 1;
    ctx.nPhase = 1;
    Tact_ProcessActionPhase(&ctx, &hooks, &save);
    CheckEqI32(rec.nFireCalls, 1, "  fired exactly once");
    Check(rec.pLastFired == &aUnits[0], "  fired at the first clean unit");
    CheckEqI32(rec.nX, 4, "  fire X from the unit's cell");
    CheckEqI32(rec.nZ, 1, "  fire Z from the unit's cell");

    /* All flagged: no fire, and the count reaches 0. */
    printf("Opportunity-fire walk with every unit flagged\n");
    memset(&rec, 0, sizeof(rec));
    hooks = MakeHooks(&rec);
    memset(&ctx, 0, sizeof(ctx));
    ctx.pScanUnit     = &aUnits[0];
    ctx.apOppFire[0]  = &aUnits[0];
    ctx.apOppFire[1]  = &aUnits[1];
    ctx.nOppFireCount = 2;
    aUnits[0].bFlags  = 0x20;
    aUnits[1].bFlags  = 0x20;
    ctx.bBusy = 1;
    ctx.nPhase = 1;
    Tact_ProcessActionPhase(&ctx, &hooks, &save);
    CheckEqI32(rec.nFireCalls, 0, "  no fire when all units are flagged");
    /* The walk already drove the count to 0, then the shared tail decrements it
     * AGAIN (`v20 = nOppFireCount; nOppFireCount = v20 - 1`), so it goes to -1.
     * That looks like a bug and is the original's behaviour. */
    CheckEqI32(ctx.nOppFireCount, -1, "  count decremented twice, ending at -1");
    /* And because the value tested was 0 rather than 1, the phase is NOT
     * advanced -- the machine stays in phase 1. */
    CheckEqI32(ctx.nPhase, 1, "  phase stays 1 when the pre-decrement count was 0");
}

/* Player opportunity fire disables input and raises the queue flag when done. */
static void TestPlayerOppFire(void)
{
    TurnHooks      hooks;
    TactCtx272     ctx;
    ActionPhaseSave save;
    Rec            rec;
    Soldier        unit;
    MapCell        cell;

    printf("Tact_ProcessActionPhase (not busy, player opportunity fire)\n");
    memset(&ctx, 0, sizeof(ctx));
    memset(&rec, 0, sizeof(rec));
    hooks = MakeHooks(&rec);
    memset(&save, 0, sizeof(save));
    memset(&unit, 0, sizeof(unit));
    memset(&cell, 0, sizeof(cell));
    cell.nX = 1;
    cell.nY = 2;
    cell.nZ = 0;
    unit.pCell = &cell;

    ctx.pScanUnit     = &unit;
    ctx.apOppFire[0]  = &unit;
    ctx.nOppFireCount = 1;
    ctx.bBusy = 0;
    ctx.nPhase = 1;

    Tact_ProcessActionPhase(&ctx, &hooks, &save);
    CheckEqI32(rec.nInputCalls, 1, "  input toggled once");
    Check(!rec.bLastInput, "  input disabled for the fire");
    CheckEqI32(rec.nFireCalls, 1, "  fired");
    CheckEqI32(rec.nEndOppFireCalls, 1, "  end-of-opportunity-fire raised");
    CheckEqI32(ctx.nPhase, 2, "  phase advanced to 2");
}

static void TestBeginEndTurn(void)
{
    TurnHooks      hooks;
    TactCtx272     ctx;
    Rec            rec;
    TurnUnitLists  lists;
    Soldier        aEnemy[2];
    Soldier        aCiv[1];
    Soldier        aXcom[2];
    Soldier       *apEnemy[2];
    Soldier       *apCiv[1];
    Soldier       *apXcom[2];
    u8             bActive;
    i32            i;

    printf("Tact_BeginEnemyTurn\n");
    memset(&ctx, 0, sizeof(ctx));
    memset(&rec, 0, sizeof(rec));
    hooks = MakeHooks(&rec);
    memset(aEnemy, 0, sizeof(aEnemy));
    memset(aCiv, 0, sizeof(aCiv));
    memset(aXcom, 0, sizeof(aXcom));
    for (i = 0; i < 2; ++i) {
        aEnemy[i].bAiDone = 1;
        aEnemy[i].nSpottedCount = 5;
        aXcom[i].bAiDone = 1;
        aXcom[i].nSpottedCount = 5;
        apEnemy[i] = &aEnemy[i];
        apXcom[i] = &aXcom[i];
    }
    aCiv[0].bAiDone = 1;
    aCiv[0].nSpottedCount = 5;
    apCiv[0] = &aCiv[0];

    lists.apEnemy = apEnemy;
    lists.nEnemy = 2;
    lists.apCivilian = apCiv;
    lists.nCivilian = 1;
    lists.apXcom = apXcom;
    lists.nXcom = 2;

    ctx.pCameraUnit = &aXcom[0];
    bActive = 7;
    Tact_BeginEnemyTurn(&ctx, &hooks, &lists, &aXcom[1], &bActive, NULL, NULL);
    Check(ctx.pCameraUnit == NULL, "  camera unit cleared");
    CheckEqI32(bActive, 1, "  enemy-turn byte set (0x4CA30)");
    CheckEqI32(ctx.bBusy, 1, "  marked busy");
    CheckEqI32(ctx.nPhase, 3, "  phase set to the enemy AI phase");
    CheckEqI32(rec.nInputCalls, 1, "  input disabled once");
    Check(!rec.bLastInput, "  input disabled");

    {
        bool bAllCleared = true;

        for (i = 0; i < 2; ++i) {
            if (aEnemy[i].bAiDone != 0 || aEnemy[i].nSpottedCount != 0) {
                bAllCleared = false;
            }
            if (aXcom[i].bAiDone != 0 || aXcom[i].nSpottedCount != 0) {
                bAllCleared = false;
            }
        }
        if (aCiv[0].bAiDone != 0 || aCiv[0].nSpottedCount != 0) {
            bAllCleared = false;
        }
        Check(bAllCleared, "  every side's AI and spotting state cleared");
    }
    Check(ctx.pSavedSelUnit == &aXcom[1], "  the CURRENT selection is saved as-is (0x4CB4C, not the roster head)");

    printf("Tact_EndEnemyTurn\n");
    memset(&rec, 0, sizeof(rec));
    hooks = MakeHooks(&rec);
    Tact_EndEnemyTurn(&ctx, &hooks, &aXcom[0], &bActive, NULL);
    CheckEqI32(bActive, 0, "  enemy-turn byte cleared (0x4CB88)");
    CheckEqI32(ctx.bBusy, 0, "  no longer busy");
    CheckEqI32(ctx.nPhase, 0, "  phase cleared");
    CheckEqI32(rec.nInputCalls, 1, "  input re-enabled once");
    Check(rec.bLastInput, "  input enabled");
    CheckEqI32(rec.nSelectCalls, 1, "  a unit was selected");
    CheckEqI32(rec.nFocusCalls, 1, "  view re-focused");

    /* A dead saved unit falls back to the supplied first XCom unit. */
    printf("Tact_EndEnemyTurn falls back when the saved unit is dead\n");
    ctx.pSavedSelUnit = &aEnemy[0];
    aEnemy[0].bStatus = 3; /* not operative */
    memset(&rec, 0, sizeof(rec));
    hooks = MakeHooks(&rec);
    Tact_EndEnemyTurn(&ctx, &hooks, &aXcom[0], &bActive, NULL);
    Check(rec.pLastSelected == &aXcom[0], "  fell back to the first XCom unit");
}

static void RecordTry(Soldier *pTarget, Soldier *pShooter, void *pUser)
{
    (void)pTarget;
    (void)pShooter;
    ((TactCtx272 *)pUser)->nOppFireCount += 1;
}

static void TestScanOpportunityFire(void)
{
    TactCtx272 ctx;
    Soldier    target;
    Soldier    aEnemyUnits[4];
    Soldier    aXcomUnits[3];
    Soldier   *aEnemy[4];
    Soldier   *aXcom[3];
    u8         nEnemyCount = 4;
    u8         nXcomCount = 3;

    printf("Unit_bScanOpportunityFire\n");
    memset(&ctx, 0, sizeof(ctx));
    memset(&target, 0, sizeof(target));
    memset(aEnemyUnits, 0, sizeof(aEnemyUnits));
    memset(aXcomUnits, 0, sizeof(aXcomUnits));
    for (i32 i = 0; i < 4; ++i) {
        aEnemy[i] = &aEnemyUnits[i];
    }
    for (i32 i = 0; i < 3; ++i) {
        aXcom[i] = &aXcomUnits[i];
    }

    /* An operative unit scans the enemy list; the shooter's morale decides. */
    target.bStatus = UNIT_STATUS_OPERATIVE;
    aEnemyUnits[0].bMorale = 0x1E; /* not above the threshold */
    aEnemyUnits[1].bMorale = 0x1F;
    aEnemyUnits[2].bMorale = 0x20;
    aEnemyUnits[3].bMorale = 0x1E;
    ctx.nOppFireCount = 7;
    Unit_bScanOpportunityFire(&ctx, &target, aEnemy, &nEnemyCount,
                              aXcom, &nXcomCount, RecordTry, &ctx);
    CheckEqI32(ctx.nOppFireCount, 2, "two enemy shooters above morale 0x1E");
    Check(ctx.pScanUnit == &target, "scan unit stored");

    /* A non-operative unit scans the XCom list instead. */
    ctx.nOppFireCount = 0;
    target.bStatus = 0;
    aXcomUnits[0].bMorale = 0x20;
    aXcomUnits[1].bMorale = 0x10;
    aXcomUnits[2].bMorale = 0x20;
    Unit_bScanOpportunityFire(&ctx, &target, aEnemy, &nEnemyCount,
                              aXcom, &nXcomCount, RecordTry, &ctx);
    CheckEqI32(ctx.nOppFireCount, 2, "two XCom shooters above morale 0x1E");
}

static void TestRefreshVisibility(void)
{
    static TactState queue;
    Rec              rec;
    Soldier          aXcom[3];
    Soldier         *apXcom[3];
    i32              i;

    printf("Tact_RefreshTeamVisibility\n");
    memset(&queue, 0, sizeof(queue));
    memset(&rec, 0, sizeof(rec));
    for (i = 0; i < 3; ++i) {
        memset(&aXcom[i], 0, sizeof(aXcom[i]));
        apXcom[i] = &aXcom[i];
    }
    queue.nQueueCursor = 4;

    Tact_RefreshTeamVisibility(&queue, apXcom, 3, Rec_Visibility, &rec);
    CheckEqI32(queue.aQueue[5].nCmd, TACTCMD_END_MARK,
               "  the end mark is appended at cursor + 1 (0x4f388 STRB #0xC at byte 0x50 = aQueue base 0x18 + one 56-byte entry)");
    CheckEqI32(queue.aQueue[4].nCmd, 0, "  the cursor's own entry is NOT overwritten");
    CheckEqI32(queue.nQueueLen, 6, "  queue length = cursor + 2, which covers the appended entry");
    CheckEqI32(queue.bFlag1, 1, "  flag1 raised");
    CheckEqI32(rec.nVisibilityCalls, 3, "  visibility refreshed for every XCom unit");

    /* A NULL queue must not fault. */
    Tact_RefreshTeamVisibility(NULL, apXcom, 3, Rec_Visibility, &rec);
    Check(true, "  NULL queue is safe");

    /* Out-of-range cursor must not write out of bounds. */
    memset(&queue, 0, sizeof(queue));
    queue.nQueueCursor = PUFO_TACT_QUEUE_MAX + 10;
    Tact_RefreshTeamVisibility(&queue, NULL, 0, NULL, NULL);
    CheckEqI32(queue.bFlag1, 1, "  flags still set with an out-of-range cursor");
}

/* --- the enemy-turn bracket -------------------------------------------------
 *
 * Tact_UpdateUnitControl @ 0x4a444, BeforeEnemyStep @ 0x4b9b8, AfterEnemyStep @ 0x4c4a0.
 * The fixture is a whole 20x20x4 grid so the burn walk and the six-face loop actually run,
 * plus three rosters, a status stack, a text table with distinct male/female records and a
 * scripted RNG.
 */

#include "game/pufo_enemyai.h"
#include "game/pufo_statusmsg.h"

#define ET_GRID_X 20
#define ET_GRID_Y 20
#define ET_GRID_Z 4
#define ET_CELLS  (ET_GRID_X * ET_GRID_Y * ET_GRID_Z)

static MapCell       g_aGrid[ET_GRID_Z][ET_GRID_Y][ET_GRID_X];
static MapGeom       g_Geom;
static Soldier       g_aUnits[16];
static Soldier      *g_apXcom[16];
static Soldier      *g_apEnemy[16];
static Soldier      *g_apCiv[16];
static Soldier      *g_apHidden[16];
static i32           g_nXcom, g_nEnemy, g_nCiv, g_nReserve;
static u8            g_nMissionType;
static Soldier      *g_pSelUnit;
static TactCursor    g_Cursor;
static MapCursorPos  g_CursorPos;
static TactSelectCtx g_Select;
static TactState     g_Queue;
static TactCtx272    g_Tact;
static StatusStack   g_Stack;
static StatusHooks   g_StatusHooks;
static RmpData       g_Rmp;
static ObjectRecFull g_FireWeapon;
static char          g_aTextRecs[64 * PUFO_TEXT_REC_STRIDE];
static char          g_aRankNames[9][PUFO_RANK_NAME_STRIDE];
static EnemyAiActCtx g_ActCtx;
static EnemyStepCtx  g_Step;
static Mc1Rec        g_aSpr[8];
static TactItem      g_aItem[4];
static Soldier       g_Occupant;
static char          g_aLinkSentinel[8];   /* a real object to point pLinked1040 at */

static const u32 *g_pRandScript;
static i32        g_nRandLen;
static i32        g_nRandIdx;
static i32        g_nRandCalls;
static i32        g_nRandOverflow;
static const u32 *g_pActScript;
static i32        g_nActLen;
static i32        g_nActIdx;
static i32        g_nProx;
static i32        g_nCellsRefresh;
static i32        g_nVisRefresh;
static i32        g_nVisUnits;
static Soldier   *g_pVisUnit;
static i32        g_nActPathCalls;
static i32        g_nActPushCalls;
static i32        g_nActCalls;
static i32        g_nActOverflow;

static MapCell *Cell(i32 nX, i32 nY, i32 nLayer)
{
    return &g_aGrid[nLayer][nY][nX];
}

static MapCell *Et_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nY < 0 || nLayer < 0 || nX >= ET_GRID_X || nY >= ET_GRID_Y
        || nLayer >= ET_GRID_Z) {
        return NULL;
    }
    return Cell(nX, nY, nLayer);
}

static u32 Et_Rand(u32 nMax, void *pUser)
{
    (void)pUser;
    g_nRandCalls += 1;
    if (g_nRandIdx >= g_nRandLen) {
        g_nRandOverflow += 1;
        return 0;
    }
    (void)nMax;
    return g_pRandScript[g_nRandIdx++];
}

/* The AI's wander loop retries until it draws an in-bounds target, so an exhausted script
 * would spin forever. This one cycles and counts the overrun instead, and the tests assert the
 * overrun stayed zero. */
static u32 Et_ActRand(u32 nMax, void *pUser)
{
    u32 nValue;

    (void)nMax;
    (void)pUser;
    g_nActCalls += 1;
    if (g_nActIdx >= g_nActLen) {
        g_nActOverflow += 1;
        g_nActIdx = 0;
    }
    nValue = g_pActScript[g_nActIdx++];
    return nValue;
}

static void Et_Prox(MapCell *pCell, void *pUser)
{
    (void)pUser;
    g_nProx += 1;
    (void)pCell;
}
static void Et_RefreshCells(void *pUser) { (void)pUser; g_nCellsRefresh += 1; }
static void Et_RefreshVis(void *pUser) { (void)pUser; g_nVisRefresh += 1; }
static void Et_UpdateVis(Soldier *pUnit, void *pUser)
{
    (void)pUser;
    g_nVisUnits += 1;
    g_pVisUnit = pUnit;
}
/* Returning true is the success case: the AI-action flag stays up and the unit keeps its TU,
 * and the queue walker's completion tail is what clears the flag. */
static bool Et_PathToCell(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser)
{
    (void)nX; (void)nY; (void)nLayer; (void)bFlag; (void)pUser;
    g_nActPathCalls += 1;
    return true;
}
static void Et_Noop(void *pUser) { (void)pUser; }
static void Et_Push(const char *pszMsg, void *pUser)
{
    (void)pszMsg; (void)pUser;
    g_nActPushCalls += 1;
}
static void Et_Face(Soldier *pUnit, i32 nX, i32 nY, void *pUser)
{
    (void)pUnit; (void)nX; (void)nY; (void)pUser;
}
static bool Et_IsBusy(void *pUser) { (void)pUser; return false; }
static bool Et_ShouldRender(void *pUser) { (void)pUser; return false; }

/* The scripted draws are consumed in call order, so a test can name exactly the draws the
 * original's loop makes and then assert how many actually happened. */
static void EtReset(const u32 *pScript, i32 nLen)
{
    memset(g_aGrid, 0, sizeof(g_aGrid));
    for (i32 z = 0; z < ET_GRID_Z; ++z) {
        for (i32 y = 0; y < ET_GRID_Y; ++y) {
            for (i32 x = 0; x < ET_GRID_X; ++x) {
                MapCell *pCell = Cell(x, y, z);
                pCell->nX = (u8)x;
                pCell->nY = (u8)y;
                pCell->nZ = (u8)z;
            }
        }
    }
    /* Zero, not a fill pattern: Soldier holds real pointers, and a 0x5A fill would turn
     * pHandR / apBelt / pCell into wild addresses the moment a callee dereferences them. */
    memset(g_aUnits, 0, sizeof(g_aUnits));
    memset(g_aSpr, 0, sizeof(g_aSpr));
    memset(g_aItem, 0, sizeof(g_aItem));
    memset(&g_Occupant, 0, sizeof(g_Occupant));
    memset(&g_Queue, 0, sizeof(g_Queue));
    memset(&g_Tact, 0, sizeof(g_Tact));
    memset(&g_Stack, 0, sizeof(g_Stack));
    memset(&g_Rmp, 0, sizeof(g_Rmp));
    memset(&g_FireWeapon, 0, sizeof(g_FireWeapon));
    memset(g_aTextRecs, 0, sizeof(g_aTextRecs));
    memset(g_apHidden, 0, sizeof(g_apHidden));

    /* Distinct male/female records so a swapped pair is visible. */
    snprintf(g_aTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_CAME_TO_M,
             PUFO_TEXT_REC_STRIDE, "%s", " came-to");
    snprintf(g_aTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_CAME_TO_F,
             PUFO_TEXT_REC_STRIDE, "%s", " woke-up");
    snprintf(g_aTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_RELEASED_M,
             PUFO_TEXT_REC_STRIDE, "%s", " released-m");
    snprintf(g_aTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_RELEASED_F,
             PUFO_TEXT_REC_STRIDE, "%s", " released-f");
    for (i32 i = 0; i < 9; ++i) {
        snprintf(g_aRankNames[i], PUFO_RANK_NAME_STRIDE, "Rank%d", i);
    }

    g_Geom.nDimX = 2;   /* 20 x 20 cells */
    g_Geom.nDimY = 2;

    /* The default actor: a Sectoid (type 9, small), already flagged as unable to act so the
     * side transfers stay closed unless a test opens them. */
    for (i32 i = 0; i < 16; ++i) {
        g_aUnits[i].nUnitType = 9;
        g_aUnits[i].uHp = 50;
        g_aUnits[i].bMorale = 50;
        g_aUnits[i].bFlags = PUFO_UNIT_FLAG_CANNOT_ACT;
        g_aUnits[i].pCell = Cell(5, 5, 1);
        g_aUnits[i].bStatus = 0;
        g_aUnits[i].bControlRecovered = 1;   /* inert until a test arms it */
        g_aUnits[i].uTu = 20;
        g_aUnits[i].uTuSpent = 0;
        g_aUnits[i].auStat[0] = 60;
        g_aUnits[i].auStat[8] = 20;
        g_aUnits[i].auStat[9] = 40;
        g_aUnits[i].uStunOrArmor = 30;
        g_aUnits[i].uInvBurden = 10;
        g_aUnits[i].bKneelOrPrimed = 10;
        g_aUnits[i].nUnk436 = 0;
        g_aUnits[i].pLinked1040 = g_aLinkSentinel;
        snprintf(g_aUnits[i].szName, sizeof(g_aUnits[i].szName), "Unit%d", (int)i);
    }

    g_nXcom = 1;  g_apXcom[0] = &g_aUnits[0];
    g_nEnemy = 0;
    g_nCiv = 0;
    g_nReserve = 0;
    g_nMissionType = 0;
    g_pSelUnit = NULL;
    g_Rmp.nNodeCount = 0;   /* Rmp_PickSpawnForUnit returns -1 without drawing RNG */

    memset(&g_Cursor, 0, sizeof(g_Cursor));
    memset(&g_CursorPos, 0, sizeof(g_CursorPos));
    g_CursorPos.pCursor = &g_Cursor;
    g_CursorPos.geom = g_Geom;
    memset(&g_Select, 0, sizeof(g_Select));
    g_Select.ppSelUnit = &g_pSelUnit;
    g_Select.pCursorPos = &g_CursorPos;
    g_Select.pfnLookup = Et_Lookup;
    g_Select.pStatus = &g_Stack;
    g_Select.panRankLow = &g_aRankNames[0][0];
    g_Select.panRankOther = &g_aRankNames[0][0];

    g_StatusHooks.pfnIsBusy = Et_IsBusy;
    g_StatusHooks.pfnShouldRender = Et_ShouldRender;
    g_StatusHooks.pfnRender = Et_Noop;

    static i32 anCost[12];
    static i32 anEnabled[12];
    static i32 nActionMode;
    static u8 bActionLatch;
    static TactItem *pSelWeapon;
    for (i32 i = 0; i < 12; ++i) {
        anCost[i] = 25 + i;
        anEnabled[i] = 700 + i;
    }
    nActionMode = 0;
    bActionLatch = 0;
    pSelWeapon = NULL;

    memset(&g_ActCtx, 0, sizeof(g_ActCtx));
    g_ActCtx.pfnRandU16 = Et_ActRand;
    g_ActCtx.pfnPathToCell = Et_PathToCell;
    g_ActCtx.pfnFillActionCosts = Et_Noop;
    g_ActCtx.pfnPushStatus = Et_Push;
    g_ActCtx.pfnFaceToward = Et_Face;
    g_ActCtx.pfnLookupCell = Et_Lookup;
    g_ActCtx.pState = &g_Queue;
    g_ActCtx.pGeom = &g_Geom;
    g_ActCtx.ppSelWeapon = &pSelWeapon;
    g_ActCtx.pnActionMode = &nActionMode;
    g_ActCtx.anActionCost = anCost;
    g_ActCtx.anActionEnabled = anEnabled;
    g_ActCtx.pbActionLatch = &bActionLatch;

    memset(&g_Step, 0, sizeof(g_Step));
    g_Step.pfnTickProximity = Et_Prox;
    g_Step.pfnRefreshCells = Et_RefreshCells;
    g_Step.pfnRefreshVisibility = Et_RefreshVis;
    g_Step.pfnUpdateVisibility = Et_UpdateVis;
    g_Step.pfnRandU16 = Et_Rand;
    g_Step.pfnLookup = Et_Lookup;
    g_Step.pGeom = &g_Geom;
    g_Step.pQueue = &g_Queue;
    g_Step.pSelect = &g_Select;
    g_Step.pStatus = &g_Stack;
    g_Step.pStatusHooks = &g_StatusHooks;
    g_Step.pTextRecs = g_aTextRecs;
    g_Step.pFireWeapon = &g_FireWeapon;
    g_Step.pActCtx = &g_ActCtx;
    g_Step.pRmp = &g_Rmp;
    g_Step.apXcom = g_apXcom;   g_Step.pnXcom = &g_nXcom;
    g_Step.apEnemy = g_apEnemy; g_Step.pnEnemy = &g_nEnemy;
    g_Step.apCiv = g_apCiv;     g_Step.pnCiv = &g_nCiv;
    g_Step.ppHidden = g_apHidden;
    g_Step.pnReserveCount = &g_nReserve;
    g_Step.pbMissionType = &g_nMissionType;
    g_Step.ppSelUnit = &g_pSelUnit;

    g_pRandScript = pScript;
    g_nRandLen = nLen;
    g_nRandIdx = 0;
    g_nRandCalls = 0;
    g_nRandOverflow = 0;
    g_pActScript = NULL;
    g_nActLen = 0;
    g_nActIdx = 0;
    g_nProx = 0;
    g_nCellsRefresh = 0;
    g_nVisRefresh = 0;
    g_nVisUnits = 0;
    g_pVisUnit = NULL;
    g_nActPathCalls = 0;
    g_nActPushCalls = 0;
    g_nActCalls = 0;
    g_nActOverflow = 0;
}

static void TestUpdateControlGuard(void)
{
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 1;
    g_aUnits[0].uHp = 50;
    g_aUnits[0].bMorale = 50;
    g_aUnits[0].uTuSpent = 7;

    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);

    CheckEqI32(g_aUnits[0].uHp, 50, "the +0x1B2 guard leaves uHp alone");
    CheckEqI32(g_aUnits[0].bMorale, 50, "and leaves morale alone");
    CheckEqI32(g_aUnits[0].uTuSpent, 7, "and leaves uTuSpent alone");
    CheckEqI32(g_Queue.nQueueLen, 0, "and queues nothing");
    CheckEqI32(g_nRandCalls, 0, "and draws no RNG");

    /* Armed: HP, morale and the flag all move. */
    g_aUnits[0].bControlRecovered = 0;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_aUnits[0].uHp, 40, "uHp drops by bKneelOrPrimed");
    CheckEqI32(g_aUnits[0].bMorale, 55, "morale gains 5");
    CheckEqI32(g_aUnits[0].bControlRecovered, 1, "and the guard byte is set");

    /* HP below the pending damage floors at zero rather than wrapping. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].uHp = 4;
    g_aUnits[0].bKneelOrPrimed = 10;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_aUnits[0].uHp, 0, "uHp floors at 0, never negative");

    /* Morale caps at 100 only from 95 up. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].bMorale = 95;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_aUnits[0].bMorale, 100, "morale 95 becomes 100");
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].bMorale = 94;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_aUnits[0].bMorale, 99, "morale 94 becomes 99");
}

static void TestUpdateControlTuSpent(void)
{
    /* Burning cell: +3. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].uTuSpent = 5;
    Cell(5, 5, 1)->nFlags32 = PUFO_CELL_FIRE_STATE_ON;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_aUnits[0].uTuSpent, 8, "a burning cell adds 3 to uTuSpent");

    /* Not burning, at least 10 spent: -10. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].uTuSpent = 12;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_aUnits[0].uTuSpent, 2, "otherwise 10 comes off uTuSpent");

    /* Not burning, under 10: zero. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].uTuSpent = 9;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_aUnits[0].uTuSpent, 0, "and under 10 it goes to zero");

    /* A large unit (type 4 is large in g_aUnitTypeIsLarge) is left alone entirely. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].nUnitType = 4;
    g_aUnits[0].uTuSpent = 12;
    Cell(5, 5, 1)->nFlags32 = PUFO_CELL_FIRE_STATE_ON;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_aUnits[0].uTuSpent, 12, "a large unit keeps its uTuSpent");
}

static void TestUpdateControlRecover(void)
{
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].bFlags = PUFO_UNIT_FLAG_CANNOT_ACT;   /* the gate needs the bit SET */
    g_aUnits[0].uHp = 50;
    g_aUnits[0].uTuSpent = 10;   /* 10 < 50 after the -10 relief: 0 */
    g_aUnits[0].bFemale = 0;
    g_aUnits[0].nUnitType = 2;   /* <= 3, so the message is pushed */

    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);

    CheckEqI32(g_Queue.nQueueLen, 1, "the recovery arm queues one entry");
    CheckEqI32(g_Queue.aQueue[0].nCmd, TACTCMD_RECOVER, "and it is the RECOVER command");
    CheckEqI32(g_Queue.bQueueActive, 1, "and KickQueue activated the queue");
    CheckEqI32(g_Stack.nCount, 1, "and one status message was pushed");
    Check(strcmp(g_Stack.aszMsg[0], "Unit0 came-to") == 0,
          "the message is the name plus the male record 39");
    Check(g_aUnits[0].pLinked1040 == NULL, "and pLinked1040 is cleared");

    /* The female record is 40, not 39. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].bFemale = 1;
    g_aUnits[0].nUnitType = 2;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    Check(strcmp(g_Stack.aszMsg[0], "Unit0 woke-up") == 0,
          "a female unit gets record 40 instead");

    /* Type above 3 recovers but says nothing. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].nUnitType = 9;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_Queue.nQueueLen, 1, "type 9 still recovers");
    CheckEqI32(g_Stack.nCount, 0, "but pushes no message");

    /* With bFlags bit 0x20 clear the whole arm is skipped. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].bFlags = 0;
    g_aUnits[0].nUnitType = 2;
    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_Queue.nQueueLen, 0, "bFlags bit 0x20 clear skips the recovery arm");
}

static void TestUpdateControlSideTransfer(void)
{
    /* An XCom-side unit of an enemy type joins the enemy roster. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].bFlags = 0;              /* the transfers need bit 0x20 CLEAR */
    g_aUnits[0].bStatus = 0;
    g_aUnits[0].nUnitType = 9;           /* > 8 */
    g_aUnits[0].uTu = 15;
    g_apEnemy[0] = NULL;
    g_pSelUnit = &g_aUnits[0];
    g_nEnemy = 0;

    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);

    CheckEqI32(g_nEnemy, 1, "the unit is appended to the enemy roster");
    Check(g_apEnemy[0] == &g_aUnits[0], "in the slot the count points at");
    CheckEqI32(g_nXcom, 0, "and it was removed from the XCom roster");
    CheckEqI32(g_aUnits[0].bStatus, 1, "bStatus becomes 1");
    CheckEqI32(g_aUnits[0].uTu, 0, "and uTu is zeroed");
    Check(g_pSelUnit == &g_aUnits[0], "re-selecting lands on the unit itself");

    /* It was the selected unit, so Tact_SelectUnit ran with apXcomUnits[0]. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].bFlags = 0;
    g_aUnits[0].bStatus = 0;
    g_aUnits[0].nUnitType = 9;
    g_nXcom = 2;
    g_apXcom[0] = &g_aUnits[1];
    g_apXcom[1] = &g_aUnits[0];
    g_aUnits[1].nUnitType = 2;
    g_pSelUnit = &g_aUnits[0];
    g_nEnemy = 0;

    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    Check(g_pSelUnit == &g_aUnits[1], "the selected unit falls back to apXcomUnits[0]");

    /* A controlled unit of an XCom type returns, unprompted message and all. */
    EtReset(NULL, 0);
    g_aUnits[0].bControlRecovered = 0;
    g_aUnits[0].bFlags = 0;
    g_aUnits[0].bStatus = 1;
    g_aUnits[0].nUnitType = 2;   /* <= 8 */
    g_aUnits[0].bFemale = 1;
    g_nEnemy = 1;
    g_apEnemy[0] = &g_aUnits[0];
    g_nXcom = 0;

    Tact_UpdateUnitControl(&g_aUnits[0], &g_Step);
    CheckEqI32(g_nXcom, 1, "the unit is appended to the XCom roster");
    Check(g_apXcom[0] == &g_aUnits[0], "in the slot the count points at");
    CheckEqI32(g_nEnemy, 0, "and it left the enemy roster");
    CheckEqI32(g_aUnits[0].bStatus, 0, "bStatus becomes 0");
    CheckEqI32(g_Stack.nCount, 1, "and the return message is pushed");
    Check(strcmp(g_Stack.aszMsg[0], "Unit0 released-f") == 0,
          "using the female record 53");
}

static void TestBeforeEnemyStepBasics(void)
{
    EtReset(NULL, 0);
    g_Tact.pTrackUnit = &g_aUnits[3];
    g_aUnits[0].bUnk1037 = 99;
    g_aUnits[0].bControlRecovered = 1;
    g_aUnits[0].nUnitType = 9;
    Soldier *pEnemy = &g_aUnits[1];
    Soldier *pCiv   = &g_aUnits[2];

    g_nEnemy = 1;
    g_apEnemy[0] = pEnemy;
    pEnemy->nUnitType = 9;
    pEnemy->nAiTick = 11;
    pEnemy->bControlRecovered = 1;
    pEnemy->bMorale = 50;
    pEnemy->uTu = 3;
    pEnemy->uStunOrArmor = 1;
    pEnemy->auStat[0] = 60;
    pEnemy->auStat[9] = 40;
    g_nCiv = 1;
    g_apCiv[0] = pCiv;
    pCiv->bControlRecovered = 1;

    BeforeEnemyStep(&g_Tact, &g_Step);

    Check(g_Tact.pTrackUnit == NULL, "pTrackUnit is cleared");
    CheckEqI32(g_aUnits[0].bUnk1037, 0, "the XCom unit's bUnk1037 is cleared");
    CheckEqI32(g_apEnemy[0]->nAiTick, 0, "every enemy's nAiTick is zeroed");
    /* BeforeEnemyStep clears the guard byte (0x4BB80) and Tact_UpdateUnitControl immediately
     * re-arms it, so the observable end state is 1. What proves the callee ran is its
     * arithmetic: morale 50 -> 55 and uHp 50 -> 40 (bKneelOrPrimed is 10). */
    CheckEqI32(pEnemy->bControlRecovered, 1, "the guard byte is re-armed by UpdateUnitControl");
    CheckEqI32(pEnemy->uHp, 40, "and the enemy took the pending knockout damage");
    CheckEqI32(pEnemy->bMorale, 55, "and gained 5 morale");
    CheckEqI32(g_apEnemy[0]->uTu, 60, "and the enemy refilled uTu from auStat[0]");
    CheckEqI32(g_apEnemy[0]->uStunOrArmor, 40, "and its stun from auStat[9]");
    CheckEqI32(g_apCiv[0]->bControlRecovered, 0, "the civilian's flag is cleared too");
    CheckEqI32(g_nProx, ET_CELLS, "every cell of all four layers was proximity-ticked");
    CheckEqI32(g_nVisRefresh, 1, "and the roster visibility was refreshed once");
    CheckEqI32(g_nRandCalls, 0, "with no RNG drawn for a non-XCom-base mission");
}

static void TestBeforeEnemyStepReserveSpawn(void)
{
    /* rand(4) = 1 -> want 3, clamped to the 2 reserves that exist. Each pick is
     * rand(nReserveCount), so the draws are 1 + 2 = 3. */
    const u32 aScript[] = { 1, 0, 0 };
    EtReset(aScript, 3);
    g_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    g_nReserve = 2;
    g_nEnemy = 1;
    g_apEnemy[0] = &g_aUnits[1];
    g_apHidden[0] = &g_aUnits[4];
    g_apHidden[1] = &g_aUnits[5];
    g_apHidden[2] = &g_aUnits[6];   /* past nReserveCount, so it must never be picked */

    BeforeEnemyStep(&g_Tact, &g_Step);

    CheckEqI32(g_nEnemy, 3, "the clamped count put both reserves on the enemy roster");
    Check(g_apEnemy[1] == &g_aUnits[4] && g_apEnemy[2] == &g_aUnits[5],
          "in the order the picks drew them");
    CheckEqI32(g_nReserve, 0, "and the reserve count fell to zero");
    /* The swap-remove moves apHiddenSoldiers[nReserveCount - 1] into the vacated slot, so
     * slot 0 ends up holding the soldier that was at slot 1. */
    Check(g_apHidden[0] == &g_aUnits[5],
          "the swap pulled apHiddenSoldiers[nReserveCount - 1] into the vacated slot");
    Check(g_apHidden[2] == &g_aUnits[6], "and the entry past the count was never touched");
    CheckEqI32(g_nRandCalls, 3, "one draw for the spawn count and one per pick");
}

/* Wall B blocks on x = 1..nLastX of row y = 5, layer 1. With the burning cell at (5,5,1) and
 * the west-axis bit set, the probe steps EAST over 5, 6, 7 and the forward pass then starts at
 * 5 + (-1) * (nBack + 1) and walks back west from the end the probe reached. */
static void EtWallBurnRow(i32 nFromX, i32 nToX)
{
    g_aSpr[0].bBlocksMove = 1;
    for (i32 x = nFromX; x <= nToX; ++x) {
        Cell(x, 5, 1)->pWallSprB = &g_aSpr[0];
    }
}

static void TestBeforeEnemyStepBurnRun(void)
{
    /* Case A: the forward walk stops on "the side cell holds the same occupant", which
     * cancels the clearing for the whole run. */
    EtReset(NULL, 0);
    EtWallBurnRow(1, 7);
    Cell(5, 5, 1)->bFlags29 = PUFO_CELL_FLAG_BURNING | PUFO_CELL_FLAG_FIRE_AXIS;
    Cell(3, 5, 1)->bFlags29 = PUFO_CELL_FLAG_BURNING;  /* no axis: its own tick clears nothing */
    Cell(7, 5, 1)->pUnit = &g_Occupant;
    Cell(7, 4, 1)->pUnit = &g_Occupant;                /* the side cell holds the SAME pointer */

    BeforeEnemyStep(&g_Tact, &g_Step);
    Check((Cell(5, 5, 1)->bFlags29 & PUFO_CELL_FLAG_BURNING) != 0,
          "a run that stops on a shared occupant keeps its burning bit");
    Check((Cell(3, 5, 1)->bFlags29 & PUFO_CELL_FLAG_BURNING) != 0,
          "and the cancellation covers the whole run, not just the cell it stopped on");

    /* Case B: the same layout with the occupants removed, so the run clears. */
    EtReset(NULL, 0);
    EtWallBurnRow(1, 7);
    Cell(5, 5, 1)->bFlags29 = PUFO_CELL_FLAG_BURNING | PUFO_CELL_FLAG_FIRE_AXIS;
    Cell(3, 5, 1)->bFlags29 = PUFO_CELL_FLAG_BURNING;
    Cell(0, 5, 1)->bFlags29 = PUFO_CELL_FLAG_BURNING;  /* its own tick clears nothing either */

    BeforeEnemyStep(&g_Tact, &g_Step);
    Check((Cell(5, 5, 1)->bFlags29 & PUFO_CELL_FLAG_BURNING) == 0,
          "the same run with no shared occupant clears the burning bit");
    Check((Cell(3, 5, 1)->bFlags29 & PUFO_CELL_FLAG_BURNING) == 0,
          "including the cells west of the start cell");
    Check((Cell(0, 5, 1)->bFlags29 & PUFO_CELL_FLAG_BURNING) != 0,
          "and the run covers exactly seven cells, so x = 0 is out of reach");

    /* Case C: the wall at x = 7 is missing, so the east probe stops at x = 6 and the forward
     * pass starts there instead of at x = 7. It then runs west until the wall at x = 4 is
     * missing, which is two cells: x = 6 and x = 5. */
    EtReset(NULL, 0);
    EtWallBurnRow(5, 6);
    Cell(5, 5, 1)->bFlags29 = PUFO_CELL_FLAG_BURNING | PUFO_CELL_FLAG_FIRE_AXIS;
    Cell(4, 5, 1)->bFlags29 = PUFO_CELL_FLAG_BURNING;  /* its own tick clears nothing */

    BeforeEnemyStep(&g_Tact, &g_Step);
    Check((Cell(5, 5, 1)->bFlags29 & PUFO_CELL_FLAG_BURNING) == 0,
          "the run starts at the east end the probe reached, not at the burning cell");
    Check((Cell(4, 5, 1)->bFlags29 & PUFO_CELL_FLAG_BURNING) != 0,
          "and it stops at the west gap, so the cell beyond it keeps its burning bit");
}

static void TestBeforeEnemyStepOccupantBurn(void)
{
    /* base = 20 - (1 - 0/100) * (12/3) = 16; nMax = 16/4 = 4; rand = 2 -> 14 damage. */
    const u32 aScript[] = { 2, 1 };
    EtReset(aScript, 2);
    Cell(5, 5, 1)->nFlags32 = PUFO_CELL_FIRE_STATE_ON;
    Cell(5, 5, 1)->nFlags40 = 8;
    Cell(5, 5, 1)->pUnit = &g_Occupant;
    g_Occupant.nUnitType = 9;
    g_Occupant.uHp = 20;
    g_Occupant.uTuSpent = 0;
    g_Occupant.abReady252[2] = 0;
    g_Occupant.bUnk248 = 12;
    g_Occupant.nUnk436 = 100;
    g_Occupant.bStatus = 0;

    BeforeEnemyStep(&g_Tact, &g_Step);

    CheckEqI32(g_Occupant.uHp, 6, "the occupant took 14 of fire damage");
    CheckEqI32(g_Occupant.nUnk436, 114, "and the damage accumulated into Soldier+0x1B4");
    CheckEqI32(g_nRandCalls, 2, "one draw for the damage roll, one for the countdown re-arm");
    CheckEqI32(g_nRandOverflow, 0, "and the script was not exhausted");
    /* The countdown: 0x78 field 8 -> 0, then the low 3 bits re-arm to rand(2) + 2 = 3 and
     * the burning state in nFlags32 steps down from 4 to 3. */
    CheckEqI32(Cell(5, 5, 1)->nFlags32 & PUFO_CELL_FIRE_STATE_MASK, 3,
               "the burning state decremented when the countdown hit zero");
    CheckEqI32(Cell(5, 5, 1)->nFlags40, 3, "and the countdown re-armed to rand(2) + 2");
}

static void TestBeforeEnemyStepFaceHeat(void)
{
    EtReset(NULL, 0);
    MapCell *pCell = Cell(5, 5, 1);
    pCell->nFlags32 = PUFO_CELL_FIRE_STATE_ON;
    pCell->nFlags40 = 5;                 /* low 3 bits 5 -> 4, no re-arm, no RNG */
    pCell->bFlags36 = 1;                 /* the hard step: 20 */
    pCell->nPathCost = 0;
    pCell->abFaceDamage[0] = 10;
    pCell->abFaceDamage[1] = 3;
    pCell->abFaceDamage[2] = 15;
    pCell->abFaceDamage[3] = 10;
    pCell->pFloorSpr = NULL;             /* no sprite: the counter grows */
    pCell->pWallSprA = NULL;             /* none here either, and the west pass reuses it */
    pCell->pWallSprB = &g_aSpr[1];       g_aSpr[1].bStrength = 30;
    pCell->pCeilSpr  = &g_aSpr[2];       g_aSpr[2].bStrength = 255;
    Cell(4, 5, 1)->nFlags32 = PUFO_CELL_FIRE_STATE_ON;   /* the west neighbour burns */
    Cell(4, 5, 1)->nFlags40 = 3;                         /* ... and does not re-arm */
    Cell(5, 4, 1)->nFlags32 = 0;                         /* the north neighbour does not */

    BeforeEnemyStep(&g_Tact, &g_Step);

    /* The term is counter + step (0x4C10C / 0x4C110) and the store adds it to the counter
     * again (0x4C1F0), so a face with no sprite goes 10 -> 10 + 30. */
    CheckEqI32(pCell->abFaceDamage[0], 40, "no sprite: the floor goes 10 -> 10 + (10 + 20)");
    /* Wall A is charged twice: the cell's own pass, then the west-neighbour pass, which
     * rewrites the slot index to 1 and reuses the same counter and the same sprite. */
    CheckEqI32(pCell->abFaceDamage[1], 72,
               "wall A takes its own hit (3 -> 26) and the west neighbour's (26 -> 72)");
    CheckEqI32(pCell->abFaceDamage[2], 15,
               "bStrength 30 is below 15 + 20, so the sprite gives way but the counter does not");
    Check(pCell->pWallSprB == NULL, "and wall B was replaced by its extra variant (none here)");
    CheckEqI32(pCell->abFaceDamage[3], 10,
               "bStrength 255 absorbs 10 + 20, so the ceiling counter is unchanged");
    CheckEqI32(pCell->nPathCost, 0, "and the six passes never reach past abFaceDamage[3]");
    CheckEqI32(pCell->nFlags40, 4, "the countdown counted down without re-arming");
    CheckEqI32(g_nRandCalls, 0, "and no cell drew RNG");
}

static void TestBeforeEnemyStepCountdownGuards(void)
{
    /* Burning state 0 takes NEITHER countdown: the 0x78 field is at its maximum so 0x4C014
     * refuses, and the low 3 bits are skipped by the state test at 0x4C038. */
    EtReset(NULL, 0);
    Cell(5, 5, 1)->nFlags32 = 0;
    Cell(5, 5, 1)->nFlags40 = 0x78u | 5u;
    BeforeEnemyStep(&g_Tact, &g_Step);
    CheckEqI32(Cell(5, 5, 1)->nFlags40, 0x78u | 5u,
               "a cell whose burning state is 0 takes neither countdown");

    /* Burning, with the 0x78 field at its maximum: the field holds, the low 3 bits step. */
    EtReset(NULL, 0);
    Cell(5, 5, 1)->nFlags32 = PUFO_CELL_FIRE_STATE_ON;
    Cell(5, 5, 1)->nFlags40 = 0x78u | 5u;
    BeforeEnemyStep(&g_Tact, &g_Step);
    CheckEqI32(Cell(5, 5, 1)->nFlags40, 0x78u | 4u,
               "the 0x78 field holds at its maximum and only the low 3 bits step down");
    CheckEqI32(g_nRandCalls, 0, "and no re-arm draw happened");

    /* A field below the maximum does decrement: 3 -> 2, and the low 3 bits 5 -> 4. */
    EtReset(NULL, 0);
    Cell(5, 5, 1)->nFlags32 = PUFO_CELL_FIRE_STATE_ON;
    Cell(5, 5, 1)->nFlags40 = 0x18u | 5u;
    BeforeEnemyStep(&g_Tact, &g_Step);
    CheckEqI32(Cell(5, 5, 1)->nFlags40, 0x10u | 4u,
               "a 0x78 field below its maximum decrements too");
}

static void TestAfterEnemyStepTopUp(void)
{
    const u32 aScript[] = { 0, 1 };
    EtReset(aScript, 2);
    /* Empty the other rosters: the XCom refill step writes uTu too, and leaving the default
     * roster in place would let it overwrite what the top-up just wrote. */
    g_nXcom = 0;
    g_nCiv = 0;
    g_nEnemy = 6;
    for (i32 i = 0; i < 6; ++i) {
        g_apEnemy[i] = &g_aUnits[i];
        g_aUnits[i].auStat[0] = 50;   /* 40% is 20 */
        g_aUnits[i].uTu = 5;
        g_aUnits[i].bControlRecovered = 1;   /* keep UpdateUnitControl inert */
    }
    g_aUnits[1].uTu = 30;   /* above the cap: must not be lowered */
    g_aUnits[2].uTu = 5;    /* never picked */

    AfterEnemyStep(&g_Tact, &g_Step);

    CheckEqI32(g_aUnits[0].uTu, 20, "the picked enemy was topped to 40% of auStat[0]");
    CheckEqI32(g_aUnits[1].uTu, 30, "the top-up never lowers uTu");
    CheckEqI32(g_aUnits[2].uTu, 5, "and only nEnemy/3 enemies are picked");
    CheckEqI32(g_nRandCalls, 2, "6/3 is two draws");
}

static void TestAfterEnemyStepHandToBelt(void)
{
    EtReset(NULL, 0);
    g_nEnemy = 2;
    g_apEnemy[0] = &g_aUnits[0];
    g_apEnemy[1] = &g_aUnits[1];
    g_aUnits[0].bControlRecovered = 1;
    g_aUnits[1].bControlRecovered = 1;
    g_aUnits[1].nUnitType = 2;   /* <= 3 keeps its hand item */
    g_aUnits[0].pHandR = &g_aItem[0];
    g_aUnits[0].apBelt[0] = NULL;
    g_aItem[0].bUnk8 = 1;
    g_aUnits[1].pHandR = &g_aItem[1];
    g_aUnits[1].apBelt[0] = NULL;
    g_aItem[1].bUnk8 = 1;
    g_aUnits[0].bUnk148 = 77;
    g_aUnits[1].bUnk148 = 77;

    AfterEnemyStep(&g_Tact, &g_Step);

    Check(g_aUnits[0].pHandR == NULL, "the enemy's right hand is emptied");
    Check(g_aUnits[0].apBelt[0] == &g_aItem[0], "and the item went to apBelt[0]");
    CheckEqI32(g_aItem[0].bUnk8, 0, "with its bUnk8 cleared");
    Check(g_aUnits[1].pHandR == &g_aItem[1], "a type <= 3 unit keeps its hand item");
    CheckEqI32(g_aUnits[0].bUnk148, 0, "Soldier+0x94 is cleared for every enemy");
    CheckEqI32(g_aUnits[1].bUnk148, 0, "for every enemy, not just the armed ones");
}

static void TestAfterEnemyStepXcomRefill(void)
{
    EtReset(NULL, 0);
    g_nXcom = 1;
    g_apXcom[0] = &g_aUnits[0];
    g_aUnits[0].bControlRecovered = 1;   /* isolate the refill arithmetic */
    g_aUnits[0].auStat[0] = 60;
    g_aUnits[0].auStat[8] = 20;
    g_aUnits[0].auStat[9] = 40;
    g_aUnits[0].uInvBurden = 10;
    g_aUnits[0].uStunOrArmor = 30;
    g_aUnits[0].uTu = 1;

    AfterEnemyStep(&g_Tact, &g_Step);

    /* q = (4*10 - 4) / 20 = 1; d = (2*1*60)/10 = 12; uTu = 60 - 12. */
    CheckEqI32(g_aUnits[0].uTu, 48, "uTu refills to auStat[0] minus the burden penalty");
    /* stun += 40*20/(10*10) = 8. */
    CheckEqI32(g_aUnits[0].uStunOrArmor, 38, "stun gains auStat[9]*auStat[8]/(10*burden)");

    /* The cap is auStat[9]. */
    EtReset(NULL, 0);
    g_nXcom = 1;
    g_apXcom[0] = &g_aUnits[0];
    g_aUnits[0].bControlRecovered = 1;
    g_aUnits[0].auStat[8] = 40;
    g_aUnits[0].auStat[9] = 40;
    g_aUnits[0].uInvBurden = 1;
    g_aUnits[0].uStunOrArmor = 39;
    AfterEnemyStep(&g_Tact, &g_Step);
    CheckEqI32(g_aUnits[0].uStunOrArmor, 40, "and stun is capped at auStat[9]");

    /* A zero mean-stun or zero burden skips the term instead of dividing by zero. */
    EtReset(NULL, 0);
    g_nXcom = 1;
    g_apXcom[0] = &g_aUnits[0];
    g_aUnits[0].bControlRecovered = 1;
    g_aUnits[0].auStat[8] = 0;
    g_aUnits[0].uInvBurden = 0;
    g_aUnits[0].uTu = 7;
    g_aUnits[0].uStunOrArmor = 12;
    AfterEnemyStep(&g_Tact, &g_Step);
    CheckEqI32(g_aUnits[0].uTu, 60, "a zero mean-stun skips the penalty, leaving auStat[0]");
    CheckEqI32(g_aUnits[0].uStunOrArmor, 12, "and a zero burden skips the stun gain");
}

static void TestAfterEnemyStepTail(void)
{
    EtReset(NULL, 0);
    g_nXcom = 2;
    g_apXcom[0] = &g_aUnits[0];
    g_apXcom[1] = &g_aUnits[1];
    g_nEnemy = 1;
    g_apEnemy[0] = &g_aUnits[2];
    g_nCiv = 1;
    g_apCiv[0] = &g_aUnits[3];
    for (i32 i = 0; i < 4; ++i) {
        g_aUnits[i].bUnk1037 = 99;
        g_aUnits[i].pCell = Cell(4, 4, 1);
    }
    g_aUnits[0].bControlRecovered = 1;
    g_aUnits[1].bControlRecovered = 1;
    g_aUnits[2].bControlRecovered = 1;
    g_aUnits[3].bControlRecovered = 1;
    g_aUnits[0].bMorale = 10;   /* < 20: panic AI */
    g_aUnits[1].bMorale = 50;   /* not */
    g_aUnits[0].nUnitType = 1;  /* small, so the panic AI does something */
    g_aUnits[1].nUnitType = 1;

    /* A non-zero fire roll sends it down the wander path; the two following draws are the
     * wander target, 4 + 11 - 10 = (5, 5) from the unit's cell at (4, 4). */
    const u32 aAct[] = { 1, 11, 11 };
    g_pActScript = aAct;
    g_nActLen = 3;

    AfterEnemyStep(&g_Tact, &g_Step);

    Check(g_Queue.pAiActionUnit == &g_aUnits[0] && g_Queue.bAiActionActive == 1,
          "the low-morale XCom unit went to the panic AI");
    Check(g_aUnits[1].bMorale == 50 && g_nActPathCalls == 1,
          "and only that one unit did, with one path attempt");
    /* A successful path leaves the TU the refill just wrote (60 - 12) and the AI-action flag up. */
    CheckEqI32(g_aUnits[0].uTu, 48, "a successful wander path leaves the unit's uTu alone");
    CheckEqI32(g_nActCalls, 3, "one fire roll and two draws for the wander target");
    CheckEqI32(g_nActOverflow, 0, "and the AI never ran out of scripted draws");
    CheckEqI32(g_nActPushCalls, 1, "and the panic AI pushed its one status message");
    CheckEqI32(g_nVisUnits, 1, "every enemy had its visibility updated");
    Check(g_pVisUnit == &g_aUnits[2], "and it was the enemy, not an XCom unit");
    CheckEqI32(g_nCellsRefresh, 2, "Map_RefreshCellMeta ran twice, once mid-way and once at the tail");
    CheckEqI32(g_nVisRefresh, 2, "and the visibility refresh likewise");
    CheckEqI32(g_aUnits[0].bUnk1037, 0, "the XCom roster's bUnk1037 is cleared at the tail");
    CheckEqI32(g_aUnits[2].bUnk1037, 99, "the enemy's is not");
    CheckEqI32(g_nProx, ET_CELLS, "every cell was proximity-ticked before the rosters");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestLayout();
    TestCtor();
    TestPhaseMachine();
    TestOppFireWalk();
    TestPlayerOppFire();
    TestBeginEndTurn();
    TestRefreshVisibility();
    TestScanOpportunityFire();

    TestUpdateControlGuard();
    TestUpdateControlTuSpent();
    TestUpdateControlRecover();
    TestUpdateControlSideTransfer();
    TestBeforeEnemyStepBasics();
    TestBeforeEnemyStepReserveSpawn();
    TestBeforeEnemyStepBurnRun();
    TestBeforeEnemyStepOccupantBurn();
    TestBeforeEnemyStepFaceHeat();
    TestBeforeEnemyStepCountdownGuards();
    TestAfterEnemyStepTopUp();
    TestAfterEnemyStepHandToBelt();
    TestAfterEnemyStepXcomRefill();
    TestAfterEnemyStepTail();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
