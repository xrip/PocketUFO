/* test_turn.c - checks the tactical turn state machine.
 *
 * The opportunity-fire walk is the part worth pinning: it indexes at the CURRENT
 * count (one past the last valid entry) and walks down, which looks like an
 * off-by-one and is the original's actual behaviour.
 */
#include "game/pufo_turn.h"

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
    Tact_BeginEnemyTurn(&ctx, &hooks, &lists, NULL, NULL);
    Check(ctx.pCameraUnit == NULL, "  camera unit cleared");
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
    Check(ctx.pSavedSelUnit == &aXcom[0], "  first XCom unit saved as the selection");

    printf("Tact_EndEnemyTurn\n");
    memset(&rec, 0, sizeof(rec));
    hooks = MakeHooks(&rec);
    Tact_EndEnemyTurn(&ctx, &hooks, &aXcom[0], NULL);
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
    Tact_EndEnemyTurn(&ctx, &hooks, &aXcom[0], NULL);
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
    CheckEqI32(queue.aQueue[4].nCmd, TACTCMD_END_MARK, "  cursor entry marked END_MARK");
    CheckEqI32(queue.nQueueLen, 6, "  queue length = cursor + 2");
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

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
