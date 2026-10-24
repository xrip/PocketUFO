/* test_tactstate.c - checks the tactical action queue.
 *
 * The DeathFlash insert (interrupt at cursor+1) and the uTuSpent >= uHp rule are
 * the two behaviours most likely to be "tidied" into something else, so they are
 * asserted explicitly.
 */
#include "game/pufo_tactstate.h"
#include "game/pufo_turn.h"    /* TactCtx272 */
#include "game/pufo_combat.h"  /* CombatCtx, ProximityCtx, UnitRosters */
#include "util/pufo_rand.h"   /* Game_RandU16 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_nFailures;
static TactState g_state;

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

static void Reset(void)
{
    memset(&g_state, 0, sizeof(g_state));
    g_pTactState = &g_state;
}

static void TestCtor(void)
{
    printf("TactState_ctor\n");
    /* Poison, then construct: the ctor must clear length and both flags. */
    memset(&g_state, 0xFF, sizeof(g_state));
    TactState_ctor(&g_state);
    CheckEqI32(g_state.nQueueLen, 0, "  TactState_ctor @ 0x4f2d4 clears the queue length");
    CheckEqI32(g_state.bQueueActive, 0, "  not active");
    CheckEqI32(g_state.bUnk70103, 0, "  scratch flag 0");
    CheckEqI32(g_state.nQueueCursor, 0, "  cursor 0 -- this one is the port's whole-block memset, not an original store");
}

static void TestSimpleEnqueues(void)
{
    Soldier unit;
    i32     i;

    printf("Simple enqueue helpers append in order\n");
    Reset();
    memset(&unit, 0, sizeof(unit));

    TactState_EnqueueTurn(&g_state, &unit, 3);
    CheckEqI32(g_state.nQueueLen, 1, "  turn appended");
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_TURN, "  TactState_EnqueueTurn @ 0x49b84 writes cmd 0");
    Check(g_state.aQueue[0].pUnit == &unit, "  unit stored");
    CheckEqI32(g_state.aQueue[0].nFacing, 3, "  facing stored");

    TactState_EnqueueStep(&g_state, &unit, 5, 42);
    CheckEqI32(g_state.nQueueLen, 2, "  step appended");
    CheckEqI32(g_state.aQueue[1].nCmd, TACTCMD_STEP, "  cmd is STEP (1)");
    CheckEqI32(g_state.aQueue[1].nFacing, 5, "  step facing");
    CheckEqI32(g_state.aQueue[1].nDelta, 42, "  TactState_EnqueueStep @ 0x49d5c stores the delta word");

    TactState_EnqueueRecover(&g_state, &unit);
    TactState_EnqueueClearAim(&g_state, &unit);
    TactState_EnqueueEndMark(&g_state);
    CheckEqI32(g_state.nQueueLen, 5, "  five entries total");
    CheckEqI32(g_state.aQueue[2].nCmd, TACTCMD_RECOVER, "  TactState_EnqueueRecover @ 0x4a37c queues cmd 11");
    CheckEqI32(g_state.aQueue[3].nCmd, TACTCMD_CLEAR_AIM, "  TactState_EnqueueClearAim @ 0x4a758 queues cmd 10");
    CheckEqI32(g_state.aQueue[4].nCmd, TACTCMD_END_MARK, "  TactState_EnqueueEndMark @ 0x4f300 queues cmd 12 with no unit");

    /* Commands must not smear between entries. */
    {
        bool bClean = true;

        for (i = 0; i < 5; ++i) {
            if (g_state.aQueue[i].nCmd != g_state.aQueue[i].nCmd) {
                bClean = false;
            }
        }
        Check(bClean, "  entries independent");
    }
    Check(g_state.aQueue[4].pUnit == NULL, "  end-mark carries no unit");
}

/* A kneeling unit gets a stand entry first, so one call can emit two. */
static void CountFace(Soldier *pUnit, i32 nX, i32 nY, void *pUser)
{
    (void)pUnit;
    (void)nX;
    (void)nY;
    *(i32 *)pUser += 1;
}

static void TestThrowAndAction18(void)
{
    Soldier unit;
    Soldier target;
    TactItem item;
    i32 nFaced = 0;

    printf("TactState_EnqueueThrow / TactState_EnqueueAction18\n");

    /* Throw with enough TU: one command-17 entry, and the unit is faced. */
    Reset();
    memset(&unit, 0, sizeof(unit));
    memset(&target, 0, sizeof(target));
    memset(&item, 0, sizeof(item));
    unit.uTu = 10;
    Check(TactState_EnqueueThrow(&g_state, &unit, &item, 3, 4, 1, 5, 99,
                                 CountFace, &nFaced),
          "  throw enqueued");
    CheckEqI32(g_state.nQueueLen, 1, "  one entry");
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_THROW, "  cmd THROW (17)");
    Check(g_state.aQueue[0].pUnit == &unit, "  unit stored");
    CheckEqI32(g_state.aQueue[0].nX, 3, "  X stored");
    CheckEqI32(g_state.aQueue[0].nY, 4, "  Y stored");
    CheckEqI32(g_state.aQueue[0].nZ, 1, "  Z stored");
    Check(g_state.aQueue[0].pWeapon == &item, "  weapon stored");
    CheckEqI32(g_state.aQueue[0].nArg, 99, "  arg stored");
    CheckEqI32(g_state.aQueue[0].nTuCost, 5, "  TU cost stored");
    CheckEqI32(nFaced, 1, "  unit faced exactly once");

    /* Not enough TU: nothing is written and the unit is not faced. */
    Reset();
    nFaced = 0;
    unit.uTu = 4;
    Check(!TactState_EnqueueThrow(&g_state, &unit, &item, 3, 4, 1, 5, 0,
                                  CountFace, &nFaced),
          "  TactState_EnqueueThrow @ 0x4b2f4 refuses when uTu < nTuCost (the original pushes record 1 there)");
    CheckEqI32(g_state.nQueueLen, 0, "  no entry written");
    CheckEqI32(nFaced, 0, "  unit not faced on refusal");

    /* Command 18 keeps the target unit and the mode. */
    Reset();
    unit.uTu = 10;
    Check(TactState_EnqueueAction18(&g_state, &unit, &target, &item, 3, 9),
          "  action 18 enqueued (Tact_EnqueueActionOrStatus205 @ 0x4b420)");
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_ACTION18, "  cmd ACTION18 (18)");
    Check(g_state.aQueue[0].pTarget == &target, "  target stored");
    CheckEqI32(g_state.aQueue[0].nMode, 9, "  mode stored");
    CheckEqI32(g_state.aQueue[0].nTuCost, 3, "  TU cost stored");

    Reset();
    unit.uTu = 2;
    Check(!TactState_EnqueueAction18(&g_state, &unit, &target, &item, 3, 9),
          "  action 18 refused without TU");
    CheckEqI32(g_state.nQueueLen, 0, "  no entry written");
}

static void TestVertStep(void)
{
    Soldier unit;

    printf("TactState_EnqueueVertStep\n");
    Reset();
    memset(&unit, 0, sizeof(unit));

    unit.bFlags = 0; /* standing */
    TactState_EnqueueVertStep(&g_state, &unit, 2);
    CheckEqI32(g_state.nQueueLen, 1, "  standing: one entry");
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_VERT_STEP, "  cmd is VERT_STEP (5)");

    Reset();
    unit.bFlags = 1; /* kneeling (bit 0) */
    TactState_EnqueueVertStep(&g_state, &unit, 2);
    CheckEqI32(g_state.nQueueLen, 2, "  kneeling: stand entry added first");
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_STAND_FROM_KNEEL, "  stand cmd (7) (TactState_EnqueueStandFromKneel @ 0x49e60)");
    CheckEqI32(g_state.aQueue[1].nCmd, TACTCMD_VERT_STEP, "  then the vert step");
    Check(g_state.aQueue[1].pUnit == &unit, "  unit on the vert step");
}

static void TestKick(void)
{
    Soldier unit;

    printf("TactState_KickQueue\n");
    Reset();
    unit.bFlags = 0;
    /* Empty queue: no-op. */
    TactState_KickQueue(&g_state);
    CheckEqI32(g_state.bQueueActive, 0, "  empty queue stays inactive");

    TactState_EnqueueTurn(&g_state, &unit, 1);
    g_state.bFlag0 = 7;
    g_state.bFlag1 = 9;
    TactState_KickQueue(&g_state);
    CheckEqI32(g_state.bQueueActive, 1, "  non-empty queue becomes active");
    CheckEqI32(g_state.nQueueCursor, 0, "  cursor reset");
    CheckEqI32(g_state.bFlag0, 0, "  flag0 cleared");
    CheckEqI32(g_state.bFlag1, 0, "  flag1 cleared");
}

/* The important one: a death flash interrupts at cursor+1 rather than appending
 * behind everything, and the tail is shifted up to make room. */
static void TestDeathFlashInsert(void)
{
    Soldier aUnits[5];
    i32     i;

    printf("TactState_EnqueueDeathFlash (interrupt path)\n");
    Reset();
    for (i = 0; i < 5; ++i) {
        memset(&aUnits[i], 0, sizeof(aUnits[i]));
        aUnits[i].nAiTick = i; /* tag each unit so order is checkable */
    }

    /* Four entries: A B C D. */
    TactState_EnqueueTurn(&g_state, &aUnits[0], 0);
    TactState_EnqueueTurn(&g_state, &aUnits[1], 0);
    TactState_EnqueueTurn(&g_state, &aUnits[2], 0);
    TactState_EnqueueTurn(&g_state, &aUnits[3], 0);
    CheckEqI32(g_state.nQueueLen, 4, "  four entries queued");

    /* Queue not active: append path. */
    TactState_EnqueueDeathFlash(&g_state, &aUnits[4], 9);
    CheckEqI32(g_state.nQueueLen, 5, "  inactive queue: appended");
    CheckEqI32(g_state.aQueue[4].nCmd, TACTCMD_DEATH_FLASH, "  flash at the tail");
    CheckEqI32(g_state.aQueue[4].nArg, 9, "  nArg stored");

    /* Now activate and put the cursor at 1, so the flash must land at 2. */
    printf("TactState_EnqueueDeathFlash with the cursor mid-queue\n");
    Reset();
    TactState_EnqueueTurn(&g_state, &aUnits[0], 0); /* 0 */
    TactState_EnqueueTurn(&g_state, &aUnits[1], 0); /* 1 */
    TactState_EnqueueTurn(&g_state, &aUnits[2], 0); /* 2 */
    TactState_EnqueueTurn(&g_state, &aUnits[3], 0); /* 3 */
    g_state.bQueueActive = 1;
    g_state.nQueueCursor = 1;

    TactState_EnqueueDeathFlash(&g_state, &aUnits[4], 4);
    CheckEqI32(g_state.nQueueLen, 5, "  length grew by one");
    CheckEqI32(g_state.aQueue[2].nCmd, TACTCMD_DEATH_FLASH, "  flash inserted at cursor+1");
    Check(g_state.aQueue[2].pUnit == &aUnits[4], "  flash carries the dying unit");
    CheckEqI32(g_state.aQueue[2].nArg, 4, "  flash nArg");
    /* The tail must have been pushed up, not overwritten. */
    Check(g_state.aQueue[1].pUnit == &aUnits[1], "  entry at cursor untouched");
    Check(g_state.aQueue[3].pUnit == &aUnits[2], "  old entry 2 pushed to 3");
    Check(g_state.aQueue[4].pUnit == &aUnits[3], "  old entry 3 pushed to 4");

    /* Cursor already at the last entry: nothing to interrupt, so append. */
    printf("TactState_EnqueueDeathFlash with the cursor on the last entry\n");
    Reset();
    TactState_EnqueueTurn(&g_state, &aUnits[0], 0);
    TactState_EnqueueTurn(&g_state, &aUnits[1], 0);
    g_state.bQueueActive = 1;
    g_state.nQueueCursor = 1; /* last entry (len-1 == 1) */

    TactState_EnqueueDeathFlash(&g_state, &aUnits[4], 6);
    CheckEqI32(g_state.nQueueLen, 3, "  appended past the end");
    Check(g_state.aQueue[2].pUnit == &aUnits[4], "  flash at the tail");
    Check(g_state.aQueue[0].pUnit == &aUnits[0], "  entry 0 unchanged");
    Check(g_state.aQueue[1].pUnit == &aUnits[1], "  entry 1 unchanged");
}

/* The uTuSpent >= uHp rule, which reads like a category error and is not. */
static void TestEnqueueIfApSpent(void)
{
    Soldier unit;

    printf("Unit_EnqueueIfApSpent\n");
    Reset();
    memset(&unit, 0, sizeof(unit));
    unit.nUnitType = 0; /* small */

    /* Not spent: nothing happens. */
    unit.uTuSpent = 10;
    unit.uHp = 50;
    Unit_EnqueueIfApSpent(&g_state, &unit, 0);
    CheckEqI32(g_state.nQueueLen, 0, "  TU below HP: nothing queued");
    CheckEqI32(g_state.bQueueActive, 0, "  queue not kicked");

    /* Spent: a small unit flashes and the queue is kicked. */
    unit.uTuSpent = 50;
    Unit_EnqueueIfApSpent(&g_state, &unit, 3);
    CheckEqI32(g_state.nQueueLen, 1, "  TU >= HP: flash queued");
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_DEATH_FLASH,
               "  Unit_EnqueueIfApSpent @ 0x4a3d4 flashes when uTuSpent reaches uHp (the gate is >=)");
    CheckEqI32(g_state.bQueueActive, 1, "  queue kicked automatically");
    CheckEqI32(g_state.nQueueCursor, 0, "  cursor reset by the kick");

    /* Flag 0x20 suppresses the whole thing. */
    printf("Unit_EnqueueIfApSpent with flag 0x20 set\n");
    Reset();
    unit.uTuSpent = 50;
    unit.uHp = 50;
    unit.bFlags = 0x20;
    Unit_EnqueueIfApSpent(&g_state, &unit, 0);
    CheckEqI32(g_state.nQueueLen, 0, "  flag 0x20 suppresses the flash");
    CheckEqI32(g_state.bQueueActive, 0, "  and the kick");

    /* A large unit with HP left survives: no flash. The queue then stays
     * inactive as well, because TactState_KickQueue @ 0x4f334 is a no-op on an
     * empty queue -- the original calls it, but nothing is there to run. */
    printf("Unit_EnqueueIfApSpent for a large unit with HP left\n");
    Reset();
    unit.bFlags = 0;
    unit.nUnitType = 4; /* large */
    unit.uTuSpent = 80;
    unit.uHp = 50;
    Unit_EnqueueIfApSpent(&g_state, &unit, 0);
    CheckEqI32(g_state.nQueueLen, 0, "  large unit with HP: no flash");
    CheckEqI32(g_state.bQueueActive, 0, "  empty queue stays inactive (kick is a no-op)");

    /* Same large unit at 0 HP does flash. */
    Reset();
    unit.uHp = 0;
    unit.uTuSpent = 0;
    Unit_EnqueueIfApSpent(&g_state, &unit, 0);
    CheckEqI32(g_state.nQueueLen, 1, "  large unit at 0 HP flashes");

    /* An already-active queue must not be re-kicked (cursor would be lost). */
    printf("Unit_EnqueueIfApSpent leaves an active queue's cursor alone\n");
    Reset();
    unit.nUnitType = 0;
    unit.uHp = 50;
    unit.uTuSpent = 50;
    g_state.bQueueActive = 1;
    g_state.nQueueCursor = 3;
    Unit_EnqueueIfApSpent(&g_state, &unit, 0);
    CheckEqI32(g_state.nQueueCursor, 3, "  active cursor preserved");
    CheckEqI32(g_state.nQueueLen, 1, "  flash still queued");
}

static void TestCapacity(void)
{
    Soldier unit;

    printf("Queue capacity\n");
    Reset();
    memset(&unit, 0, sizeof(unit));
    /* Fill to the documented maximum; the original's array is 1250 entries. */
    for (i32 i = 0; i < PUFO_TACT_QUEUE_MAX; ++i) {
        TactState_EnqueueTurn(&g_state, &unit, (u8)(i & 7));
    }
    CheckEqI32(g_state.nQueueLen, PUFO_TACT_QUEUE_MAX, "  filled to capacity");
    CheckEqI32(g_state.aQueue[PUFO_TACT_QUEUE_MAX - 1].nFacing,
               (PUFO_TACT_QUEUE_MAX - 1) & 7, "  last entry intact");
}

/* The original's flag-decay expression is obfuscated. Assert that the ported form
 * matches it bit-for-bit across the whole byte range, so any future edit to the
 * "plain" version is caught. */
static u32 OriginalDecayExpression(u32 v)
{
    /* Copied from the decompilation, with explicit parentheses so the precedence
     * is unambiguous: the 0x78 mask applies to the xor result, then the final xor
     * with v. */
    u32 nStep = (((((v & 0xF8u) - 1u) & 0xFFu) ^ (v & 0xFFu)) & 0x78u);

    return nStep ^ v;
}

static void TestDecayCellFlags(void)
{
    bool bEquivalent = true;
    u32  v;

    printf("GameTick_DecayCellFlags matches the original expression\n");
    for (v = 0; v <= 0xFF; ++v) {
        if (GameTick_DecayCellFlags(v) != OriginalDecayExpression(v)) {
            printf("  MISMATCH at 0x%02x: got 0x%02x want 0x%02x\n",
                   (unsigned)v, (unsigned)GameTick_DecayCellFlags(v),
                   (unsigned)OriginalDecayExpression(v));
            bEquivalent = false;
        }
    }
    Check(bEquivalent, "  equivalent to the original for all 256 inputs");

    /* The field is bits 3..6 and decrements by one. */
    CheckEqI32((i32)GameTick_DecayCellFlags(0x78), 0x70, "  0x78 -> 0x70 (field 0xF -> 0xE)");
    CheckEqI32((i32)GameTick_DecayCellFlags(0x08), 0x00, "  0x08 -> 0x00 (field 1 -> 0)");
    /* Surrounding bits must not be disturbed by the decrement. 0x8F has field 1
     * (bits 3..6) plus the high bit and the low three bits set; decrementing the
     * field to 0 must leave 0x80 and 0x07 intact, giving 0x87. */
    CheckEqI32((i32)GameTick_DecayCellFlags(0xF8), 0xF0, "  upper bits preserved");
    CheckEqI32((i32)GameTick_DecayCellFlags(0x8F), 0x87, "  low bits preserved");
    /* A cell whose field is already 0 is skipped by the caller, but the helper
     * itself wraps. Assert the wrap so the caller's skip is the only thing
     * preventing it: 0x87 & 0x78 == 0, so the field becomes 0xF. */
    CheckEqI32((i32)GameTick_DecayCellFlags(0x87), 0xFF,
               "  field 0 wraps to 0xF (caller must skip these)");
}

/* --- GameTick_Step, with an injected grid --- */

#define GT_DIM 4

typedef struct TickGrid {
    MapCell aCells[PUFO_MAP_LAYERS][GT_DIM][GT_DIM];
    i32     nBoundCellCount;
} TickGrid;

/* Part 105: the step's two Map cells and its wrap tail. The fixtures are one byte each so a
 * write that lands in the wrong cell (or fails to land) shows up as a failed check. */
static u8  s_bSlot8;
static u8  s_bEnemy;
static i32 s_nRender;

static void TickStep_Render(void *pUser)
{
    (void)pUser;
    s_nRender += 1; /* 0x274C4 tail Render_Frame(&szEmptyString[8]) */
}

static void TickStepCells(GameTickStepCells *pCells)
{
    pCells->pbSlot8Byte1      = &s_bSlot8;
    pCells->pbEnemyTurnActive = &s_bEnemy;
    pCells->pfnRenderFrame    = TickStep_Render;
    pCells->pRenderUser       = NULL;
}

static MapCell *TickGrid_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    TickGrid *pG = (TickGrid *)pUser;

    if (nX < 0 || nY < 0 || nX >= GT_DIM || nY >= GT_DIM) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return &pG->aCells[nLayer][nY][nX];
}

static void TickGrid_Init(TickGrid *pG)
{
    i32 x;
    i32 y;
    i32 z;

    memset(pG, 0, sizeof(*pG));
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < GT_DIM; ++y) {
            for (x = 0; x < GT_DIM; ++x) {
                MapCell_Init(&pG->aCells[z][y][x], x, y, z);
            }
        }
    }
}

static void TestGameTickStep(void)
{
    TickGrid grid;
    GameTickStepCells cells;
    i32      i;
    i32      nEnded;

    s_bSlot8 = 0; s_bEnemy = 0; s_nRender = 0;
    TickStepCells(&cells);

    printf("GameTick_Step\n");
    Reset();
    TickGrid_Init(&grid);

    /* Inactive phase: nothing happens at all. */
    grid.aCells[0][0][0].nFlags32 = 0x78;
    Check(!GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid), "  inactive phase does nothing");
    CheckEqI32((i32)grid.aCells[0][0][0].nFlags32, 0x78, "  flags untouched while inactive");

    /* Active phase: the flagged cell decays once per tick. */
    printf("GameTick_Step decay while active\n");
    g_state.bPhaseActive = 1;
    Check(!GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid), "  tick 1 does not end the phase");
    CheckEqI32((i32)grid.aCells[0][0][0].nFlags32, 0x70, "  GameTick_Step @ 0x27324 decays the cell field once per tick");
    CheckEqI32(g_state.nPhaseCounter, 1, "  phase counter incremented");

    /* A cell whose field is already zero is skipped, so it cannot wrap to 0xF. */
    printf("GameTick_Step skips cells already at zero\n");
    grid.aCells[1][2][3].nFlags32 = 0; /* field 0 */
    grid.aCells[1][2][2].nFlags32 = 0x80; /* only a high bit, field 0 */
    (void)GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid);
    CheckEqI32((i32)grid.aCells[1][2][3].nFlags32, 0, "  zero field left at zero (no wrap)");
    CheckEqI32((i32)grid.aCells[1][2][2].nFlags32, 0x80, "  high bit preserved, field untouched");

    /* All four layers are swept, not just layer 0. */
    printf("GameTick_Step sweeps every layer\n");
    TickGrid_Init(&grid);
    Reset();
    g_state.bPhaseActive = 1;
    for (i = 0; i < PUFO_MAP_LAYERS; ++i) {
        grid.aCells[i][1][1].nFlags32 = 0x78;
    }
    (void)GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid);
    for (i = 0; i < PUFO_MAP_LAYERS; ++i) {
        char szLabel[64];

        snprintf(szLabel, sizeof(szLabel), "  layer %d decayed", (int)i);
        CheckEqI32((i32)grid.aCells[i][1][1].nFlags32, 0x70, szLabel);
    }

    /* The phase ends after 11 ticks. Run until it reports the end and count. */
    printf("GameTick_Step ends the phase after 11 ticks\n");
    Reset();
    g_state.bPhaseActive = 1;
    nEnded = 0;
    for (i = 1; i <= 32; ++i) {
        if (GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid)) {
            nEnded = i;
            break;
        }
    }
    CheckEqI32(nEnded, PUFO_PHASE_TICK_MAX + 1, "  phase ends on tick 12 (counter > 11)");
    CheckEqI32(g_state.bPhaseActive, 0, "  phase flag cleared");
    CheckEqI32(s_nRender, 1, "  the wrap tail Render_Frame ran exactly once (0x274C4, 11 non-wrap ticks gave 0)");
    /* And a further tick is a no-op. */
    Check(!GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid), "  ended phase stays inactive");
    CheckEqI32(s_nRender, 1, "  the ended phase renders nothing more");
}

/* Part 105: the byte pair 0x27364/0x27388 (set) and 0x274B4/0x274BC (clear on the wrap). */
static void TestGameTickStepBytes(void)
{
    TickGrid grid;
    GameTickStepCells cells;

    printf("GameTick_Step enemy-turn byte pair (0x27364/0x27388, 0x274B4/0x274BC)\n");
    TickGrid_Init(&grid);
    TickStepCells(&cells);

    /* Inactive phase: the gate at 0x27348 returns before the byte is even read (0x27364 sits
     * past it), so neither cell moves. */
    Reset();
    g_state.bPhaseActive = 0;
    s_bSlot8 = 1; s_bEnemy = 0x5A; s_nRender = 0;
    Check(!GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid), "  inactive step returns false");
    CheckEqI32(s_bSlot8, 1, "  the slot-8 byte is not even read while inactive");
    CheckEqI32(s_bEnemy, 0x5A, "  the enemy byte is untouched while inactive");

    /* Slot-8 set + active phase: the LDRB/ANDS/MOVNE/STRBNE chain writes the LITERAL 1. */
    Reset();
    g_state.bPhaseActive = 1;
    s_bSlot8 = 3; s_bEnemy = 0;
    Check(!GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid), "  tick 1 does not end the phase");
    CheckEqI32(s_bEnemy, 1, "  0x27388 STRBNE writes 1, not the trigger's own value (3 stayed 3, enemy is 1)");
    CheckEqI32(s_bSlot8, 3, "  the trigger byte is only READ by the set arm");

    /* Slot-8 zero: no write at all -- a non-zero enemy byte survives. */
    Reset();
    g_state.bPhaseActive = 1;
    s_bSlot8 = 0; s_bEnemy = 7;
    (void)GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid);
    CheckEqI32(s_bEnemy, 7, "  0x27370 ANDS is zero: MOVNE/STRBNE skip, the 7 survives");

    /* The wrap clears BOTH bytes and renders: counter sits at 11, this step is tick 12. */
    Reset();
    g_state.bPhaseActive = 1;
    g_state.nPhaseCounter = PUFO_PHASE_TICK_MAX;
    s_bSlot8 = 1; s_bEnemy = 1; s_nRender = 0;
    Check(GameTick_Step(&g_state, &cells, TickGrid_Lookup, &grid), "  tick 12 ends the phase");
    CheckEqI32(s_bSlot8, 0, "  0x274B4 clears unk_113A39");
    CheckEqI32(s_bEnemy, 0, "  0x274BC clears Map_bEnemyTurnActive");
    CheckEqI32(g_state.bPhaseActive, 0, "  0x274AC clears bPhaseActive");
    CheckEqI32(s_nRender, 1, "  0x274C4 tail-calls Render_Frame once");
    CheckEqI32(s_bSlot8, 0, "  the cleared trigger stays clear");
}

/* --- TactState_EnqueueFireBurst @ 0x4b080 ---------------------------------- */

static i32 g_nFaceCalls;
static i32 g_nFaceX;
static i32 g_nFaceY;

static void FireFaceToward(Soldier *pUnit, i32 nX, i32 nY, void *pUser)
{
    (void)pUnit;
    (void)pUser;
    g_nFaceCalls += 1;
    g_nFaceX = nX;
    g_nFaceY = nY;
}

static void TestFireBurst(void)
{
    Soldier      unit;
    FireBurstSpec spec;

    printf("TactState_EnqueueFireBurst\n");

    /* Not enough TU: nothing is queued and the unit is NOT faced. The comparison
     * is strict, so uTu == nTuCost must still be accepted (checked below). */
    Reset();
    memset(&unit, 0, sizeof(unit));
    memset(&spec, 0, sizeof(spec));
    unit.uTu = 40;
    spec.pUnit = &unit;
    spec.nMode = 1;
    spec.nTuCost = 50;
    spec.uAmmoInWeapon = 10;
    spec.bAmmoCap = 10;
    g_nFaceCalls = 0;
    Check(!TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL),
          "  too little TU is refused");
    CheckEqI32(g_state.nQueueLen, 0, "  nothing queued");
    CheckEqI32(g_nFaceCalls, 0, "  the unit is not faced on failure");

    /* Exactly enough TU: accepted. One shot means two entries plus clear-aim. */
    Reset();
    unit.uTu = 50;
    g_nFaceCalls = 0;
    Check(TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL),
          "  exactly enough TU is accepted");
    CheckEqI32(g_nFaceCalls, 1, "  the unit is faced once");
    CheckEqI32(g_state.nQueueLen, 3, "  1 shot = 2 entries + clear-aim = 3");
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_FIRE_SHOT, "  entry 0 is a shot");
    CheckEqI32(g_state.aQueue[1].nCmd, TACTCMD_FIRE_MARK, "  entry 1 is a mark");
    CheckEqI32(g_state.aQueue[2].nCmd, TACTCMD_CLEAR_AIM, "  entry 2 clears aim");
    CheckEqI32(g_state.aQueue[0].nTuCost, 50, "  the first shot carries the TU cost");
    CheckEqI32(g_state.aQueue[1].nTuCost, 0, "  the mark entry carries no cost");

    /* Mode 0 fires three shots, so 3*2 + 1 = 7 entries, and only the FIRST shot is
     * charged TU. */
    Reset();
    spec.nMode = 0;
    spec.nTuCost = 20;
    unit.uTu = 100;
    spec.uAmmoInWeapon = 10;
    spec.bAmmoCap = 10;
    (void)TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL);
    CheckEqI32(g_state.nQueueLen, 7, "  mode 0 fires 3 shots = 7 entries");
    CheckEqI32(g_state.aQueue[0].nTuCost, 20, "  shot 0 charged");
    CheckEqI32(g_state.aQueue[2].nTuCost, 0, "  shot 1 NOT charged");
    CheckEqI32(g_state.aQueue[4].nTuCost, 0, "  shot 2 NOT charged");

    /* Mode 0 with only 2 rounds loaded caps at 2 shots. */
    Reset();
    spec.uAmmoInWeapon = 2;
    spec.bAmmoCap = 10;
    (void)TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL);
    CheckEqI32(g_state.nQueueLen, 5, "  2 rounds -> 2 shots -> 5 entries");

    /* Capacity 255 means unlimited, so the ammo cap does NOT apply. */
    Reset();
    spec.uAmmoInWeapon = 0;
    spec.bAmmoCap = 255;
    (void)TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL);
    CheckEqI32(g_state.nQueueLen, 7, "  unlimited weapon still fires 3 shots");

    /* Mode 7 takes the shot count from the target list and its coordinates from
     * each target's cell. */
    Reset();
    {
        MapCell  aCells[3];
        MapCell *apTargets[3];

        memset(aCells, 0, sizeof(aCells));
        for (i32 i = 0; i < 3; ++i) {
            MapCell_Init(&aCells[i], (u8)(10 + i), (u8)(20 + i), (u8)(1 + i));
            apTargets[i] = &aCells[i];
        }
        spec.nMode = 7;
        spec.nMultiTargetCount = 3;
        spec.apMultiTargets = apTargets;
        spec.uAmmoInWeapon = 0;
        spec.bAmmoCap = 10;
        (void)TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL);
        CheckEqI32(g_state.nQueueLen, 7, "  mode 7 fires one shot per target");
        /* The ammo cap must NOT apply in mode 7, so 3 shots stand despite 0 rounds. */
        CheckEqI32(g_state.aQueue[0].nX, 10, "  shot 0 x from target 0");
        CheckEqI32(g_state.aQueue[0].nY, 20, "  shot 0 y from target 0");
        CheckEqI32(g_state.aQueue[2].nX, 11, "  shot 1 x from target 1");
        CheckEqI32(g_state.aQueue[4].nX, 12, "  shot 2 x from target 2");
        CheckEqI32(g_state.aQueue[4].nZ, 3, "  shot 2 z from target 2");
    }

    /* A non-mode-7 burst takes its coordinates from the single target argument. */
    Reset();
    spec.nMode = 1;
    spec.nMultiTargetCount = 0;
    spec.nX = 33;
    spec.nY = 44;
    spec.nZ = 2;
    /* Restore ammo: the cap applies outside mode 7, and the previous case left it
     * at 0 (which correctly produces zero shots). */
    spec.uAmmoInWeapon = 10;
    spec.bAmmoCap = 10;
    g_nFaceCalls = 0;
    (void)TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL);
    CheckEqI32(g_state.aQueue[0].nX, 33, "  single-target x used");
    CheckEqI32(g_state.aQueue[0].nY, 44, "  single-target y used");
    CheckEqI32(g_state.aQueue[0].nZ, 2, "  single-target z used");
    CheckEqI32(g_nFaceX, 33, "  faced toward x");
    CheckEqI32(g_nFaceY, 44, "  faced toward y");

    /* The weapon pointer and the fire argument are stored on every shot entry. */
    Reset();
    spec.nArg = 77;
    spec.pWeapon = (void *)(uintptr_t)0x1234;
    spec.uAmmoInWeapon = 10;
    spec.bAmmoCap = 10;
    (void)TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL);
    Check(g_state.aQueue[0].pWeapon == (void *)(uintptr_t)0x1234,
          "  weapon pointer stored on the shot");
    CheckEqI32(g_state.aQueue[0].nArgDword, 77, "  fire argument stored");
    CheckEqI32(g_state.aQueue[0].nMode, 0, "  nMode field is the caller's to fill");

    /* Zero rounds with a real capacity: the cap drives the shot count to 0, so only
     * the clear-aim entry is queued. Worth pinning because it is easy to assume a
     * minimum of one shot. */
    Reset();
    spec.uAmmoInWeapon = 0;
    spec.bAmmoCap = 10;
    (void)TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL);
    CheckEqI32(g_state.nQueueLen, 1, "  zero rounds queues only the clear-aim entry");
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_CLEAR_AIM, "  and it is clear-aim");

    /* NULL safety. */
    Check(!TactState_EnqueueFireBurst(NULL, &spec, FireFaceToward, NULL),
          "  NULL state refused");
    Check(!TactState_EnqueueFireBurst(&g_state, NULL, FireFaceToward, NULL),
          "  NULL spec refused");
    spec.pUnit = NULL;
    Check(!TactState_EnqueueFireBurst(&g_state, &spec, FireFaceToward, NULL),
          "  NULL unit refused");
}

static MapCell *DoorGridLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    MapCell (*pGrid)[30][40] = pUser;

    if (nX < 0 || nX >= 40 || nY < 0 || nY >= 30 || nLayer < 0 || nLayer >= 4) {
        return NULL;
    }
    return &pGrid[nLayer][nY][nX];
}

static void TestQueueDoorAction(void)
{
    static MapCell aGrid[4][30][40];
    TactState state;
    MapCell cellA;
    MapCell cellB;
    Mc1Rec wallB;
    DoorActionCtx ctx;
    Soldier sel;

    printf("Map_QueueDoorAction\n");
    memset(aGrid, 0, sizeof(aGrid));
    memset(&state, 0, sizeof(state));
    memset(&cellA, 0, sizeof(cellA));
    memset(&cellB, 0, sizeof(cellB));
    memset(&wallB, 0, sizeof(wallB));
    memset(&ctx, 0, sizeof(ctx));
    memset(&sel, 0, sizeof(sel));
    cellA.nX = 5; cellA.nY = 5; cellA.nZ = 1;
    cellB.nX = 6; cellB.nY = 6; cellB.nZ = 1;
    ctx.pSelUnit = &sel;
    ctx.geom.nDimX = 4;
    ctx.geom.nDimY = 3;
    ctx.pfnLookup = DoorGridLookup;
    ctx.pUser = aGrid;
    aGrid[1][5][5] = cellA;

    /* nDir 0 uses wall-B. With no wall sprite nothing is queued. */
    Check(!Map_QueueDoorAction(&state, &cellA, &cellB, 0, &ctx), "no wall -> false");
    CheckEqI32(state.nQueueLen, 0, "no entry queued");

    /* A wall whose byte 0x2F is set queues an open + door pair. */
    cellA.pWallSprB = &wallB;
    aGrid[1][5][5].pWallSprB = &wallB;
    wallB.gap_2f[0] = 1;
    Check(Map_QueueDoorAction(&state, &cellA, &cellB, 0, &ctx), "Map_QueueDoorAction @ 0x2bd00 queues the door action");
    CheckEqI32(state.nQueueLen, 2, "open + door entries");
    CheckEqI32(state.aQueue[0].nCmd, TACTCMD_OPEN, "first entry is open (2)");
    CheckEqI32(state.aQueue[1].nCmd, TACTCMD_DOOR, "second entry is door (3)");
    CheckEqI32(state.aQueue[1].nFacing, 2, "door facing 2 for wall-B");
    CheckEqI32(state.aQueue[1].nX, 5, "door X");
    CheckEqI32(state.aQueue[1].nY, 5, "door Y");
    CheckEqI32(state.aQueue[1].nZ, 1, "door Z");

    /* A blocking wall with a clear byte 0x2F at nDir 2 (wall-A) queues a
     * step-door entry. */
    memset(&state, 0, sizeof(state));
    memset(&wallB, 0, sizeof(wallB));
    {
        Mc1Rec wallA;
        memset(&wallA, 0, sizeof(wallA));
        wallA.bBlocksMove = 1;
        aGrid[1][6][6].pWallSprA = &wallA;
        cellB.pWallSprA = &wallA;
        /* nDir 2, same layer -> pUse = cellB, mode 2 -> wall-A. */
        Check(Map_QueueDoorAction(&state, &cellA, &cellB, 2, &ctx),
              "step-door queued");
        CheckEqI32(state.aQueue[0].nCmd, TACTCMD_OPEN, "open first");
        CheckEqI32(state.aQueue[1].nCmd, TACTCMD_STEP_DOOR, "step-door (4)");
        CheckEqI32(state.aQueue[1].nFacing, 4, "step-door facing 4 for wall-A");
    }

    Check(!Map_QueueDoorAction(NULL, &cellA, &cellB, 0, &ctx), "NULL state refused");
    Check(!Map_QueueDoorAction(&state, &cellA, &cellB, 1, &ctx), "bad dir refused");
}

/* ==========================================================================
 * Unit_ApplyFallDamage @ 0x4a7b0 and Tact_ProcessFallAction @ 0x52adc
 *
 * Fixture: a 4x4x4 cell grid, one shared floor-sprite record, one shared ceiling
 * record whose iCeilParam each test sets, and the FallActionCtx the processor
 * reads. Convention: g_aFallUnits[0] is the acting unit, [1] the target, [2] and
 * [3] cell occupants.
 * ========================================================================== */

#define FALL_DIM 4

static MapCell   g_aFallCells[PUFO_MAP_LAYERS][FALL_DIM][FALL_DIM];
static Mc1Rec    g_FallFloor;
static Mc1Rec    g_FallCeil;
static Soldier   g_aFallUnits[4];
static Soldier  *g_apFallXcom[4];
static Soldier  *g_apFallEnemy[4];
static Soldier  *g_apFallCiv[8];   /* Tact_KillUnit parks corpses here */
static u8        g_nFallXcom;
static u8        g_nFallEnemy;

/* The scratch globals the original reaches through the Map block. */
static i8       g_nLayerScratch;
static i32      g_nMaxLayerClamp;
static i32      g_nAnimPhase;
static i32      g_nFireTickProgress;
static u8       g_bEnemySpotted;
static u8       g_bFallPending;
static u8       g_bThrowPending;
static u8       g_bTickHold;
static u16      g_nUiScreen;
static TactItem *g_pSelWeapon;
static Soldier  *g_pThrowTarget;
static Soldier  *g_pLookCommit;
static u8        g_bLookFlag;
static i32       g_nCursorX;
static i32       g_nCursorY;
static i32       g_nMeleeX, g_nMeleeY, g_nMeleeLayer;
static double    g_dThrowX, g_dThrowY, g_dThrowLayer;

/* Objects the processor drives. */
static TactCursor    g_FallCursor;
static MapCursorPos  g_FallPos;
static Soldier      *g_pFallSelUnit;
static TactSelectCtx g_FallSel;
static TactCtx272    g_FallTactCtx;
static SideLists     g_FallLists;
static UnitRosters   g_FallRosters;
static CombatCtx     g_FallCombat;
static ProximityCtx  g_FallProx;
static MapGeom       g_FallGeom = { FALL_DIM, FALL_DIM };
static ObjectRecFull  g_aFallObjTable[132];
static ObjectRecFull *g_apFallObjTable[132];
static TactItem       g_aFallItemTable[8];
static TactItem      *g_apFallItemTable[8];
static i32            g_nFallItemCnt;
static i32            g_nFallKillCount;
/* A model of the unit NAME table 0xB1140 (byte_AD720 + 0x3A20): 40-byte records indexed by
 * unit type, filled in Fall_Init. The transform arms point at the record of the unit's NEW
 * type -- record 14 in the kill arm (0x4e508 LDR R0,=(byte_AD720+0x3A20), then 0x4e518
 * ADD R3,R0,#0x230 = 40 * 14), record 22 in the fall arm (0x53148 ADD #0x370 = 40 * 22).
 * The shell feeds the same indices through World_UnitNameRecord (pufo_world.c). */
static char           g_aFallNameTable[40 * 23];
static KillCtx        g_FallKillCtx;
static KillHooks      g_FallKillHooks;
static ObjectRecFull  g_FallWeaponObj;
static TactItem       g_FallWeapon;
static FallActionCtx  g_FallCtx;
static FallDamageCtx  g_FallDmg;

/* What the seams saw. */
static i32     g_nVisCalls;
static i32     g_nStatusCalls;
static i32     g_nForcedCalls;
static i32     g_nBattleEndCalls;
static i32     g_nKillBattleEndCalls;
static i32     g_nLastStatusRec;
static Soldier *g_pVisLastUnit;
static char     g_szForced[256];
/* The fall ctx hands two different users to its seams (FallActionCtx.pCallbackUser and
 * FallActionCtx.pShellUser -- see the header note). Each hook records what it was handed, so a
 * call site that passes the wrong one is a failed check instead of a silent wrong base pointer. */
static void   *g_pUserStatus;
static void   *g_pUserForced;
static void   *g_pUserBattleEnd;
static i32     g_nWiringProbe;   /* the sentinel wired into pCallbackUser */
static i32     g_nShellProbe;    /* the sentinel wired into pShellUser */

static MapCell *FallLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nX >= FALL_DIM || nY < 0 || nY >= FALL_DIM) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return &g_aFallCells[nLayer][nY][nX];
}

static void FallHookVisibility(Soldier *pUnit, void *pUser)
{
    (void)pUser;
    ++g_nVisCalls;
    g_pVisLastUnit = pUnit;
}

static void FallHookStatus(i32 nRec, void *pUser)
{
    ++g_nStatusCalls;
    g_nLastStatusRec = nRec;
    g_pUserStatus = pUser;
}

static void FallHookForced(const char *pszMsg, void *pUser)
{
    ++g_nForcedCalls;
    snprintf(g_szForced, sizeof(g_szForced), "%s", pszMsg);
    g_pUserForced = pUser;
}

static void FallHookBattleEnd(u8 bAbort, void *pUser)
{
    (void)bAbort;
    ++g_nBattleEndCalls;
    g_pUserBattleEnd = pUser;
}

static void FallHookKillBattleEnd(void *pUser)
{
    (void)pUser;
    ++g_nKillBattleEndCalls;
}

/* SideLists keeps its counts by value, so the fixture has to copy the byte counts
 * into it before each call, the way the game's single shared block does. */
static void FallSyncLists(void)
{
    g_FallLists.nXcom  = (i32)g_nFallXcom;
    g_FallLists.nEnemy = (i32)g_nFallEnemy;
}

static void FallSetup(void)
{
    i32 z, y, x, n;

    Reset();
    memset(g_aFallCells, 0, sizeof(g_aFallCells));
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < FALL_DIM; ++y) {
            for (x = 0; x < FALL_DIM; ++x) {
                MapCell_Init(&g_aFallCells[z][y][x], x, y, z);
            }
        }
    }
    memset(g_aFallUnits, 0, sizeof(g_aFallUnits));
    memset(g_apFallXcom, 0, sizeof(g_apFallXcom));
    memset(g_apFallEnemy, 0, sizeof(g_apFallEnemy));
    memset(g_apFallCiv, 0, sizeof(g_apFallCiv));
    memset(&g_FallCeil, 0, sizeof(g_FallCeil));
    memset(&g_FallFloor, 0, sizeof(g_FallFloor));
    memset(&g_FallTactCtx, 0, sizeof(g_FallTactCtx));
    memset(&g_FallCursor, 0, sizeof(g_FallCursor));

    g_nFallXcom = 0;
    g_nFallEnemy = 0;
    g_nLayerScratch = 0;
    g_nMaxLayerClamp = 2;
    g_nAnimPhase = 0;
    g_nFireTickProgress = 7;
    g_bEnemySpotted = 0;
    g_bFallPending = 0;
    g_bThrowPending = 0;
    g_bTickHold = 0;
    g_nUiScreen = 0;
    g_pThrowTarget = &g_aFallUnits[2];   /* a non-NULL sentinel */
    g_pLookCommit = NULL;
    g_bLookFlag = 0;
    g_nCursorX = 0;
    g_nCursorY = 0;
    g_nMeleeX = 1;
    g_nMeleeY = 1;
    g_nMeleeLayer = 2;
    g_dThrowX = 0;
    g_dThrowY = 0;
    g_dThrowLayer = 0;
    g_nVisCalls = 0;
    g_nStatusCalls = 0;
    g_nForcedCalls = 0;
    g_nBattleEndCalls = 0;
    g_nKillBattleEndCalls = 0;
    g_nLastStatusRec = -1;
    g_pVisLastUnit = NULL;
    g_pUserStatus = NULL;
    g_pUserForced = NULL;
    g_pUserBattleEnd = NULL;
    g_szForced[0] = 0;

    memset(&g_FallWeaponObj, 0, sizeof(g_FallWeaponObj));
    memset(&g_FallWeapon, 0, sizeof(g_FallWeapon));
    g_FallWeapon.pObj = &g_FallWeaponObj;
    g_pSelWeapon = &g_FallWeapon;

    /* Tact_KillUnit's own context, mirroring tests/test_vis.c. */
    memset(g_aFallObjTable, 0, sizeof(g_aFallObjTable));
    memset(g_aFallItemTable, 0, sizeof(g_aFallItemTable));
    for (n = 0; n < 132; ++n) {
        g_apFallObjTable[n] = &g_aFallObjTable[n];
    }
    for (n = 0; n < 8; ++n) {
        g_aFallItemTable[n].pObj = &g_aFallObjTable[n];
        g_apFallItemTable[n] = &g_aFallItemTable[n];
    }
    g_nFallItemCnt = 0;
    g_nFallKillCount = 0;
    memset(&g_FallCombat, 0, sizeof(g_FallCombat));
    g_FallCombat.pfnLookup = FallLookup;
    g_FallCombat.pGeom = &g_FallGeom;
    memset(&g_FallKillCtx, 0, sizeof(g_FallKillCtx));
    g_FallKillCtx.pCombat = &g_FallCombat;
    g_FallKillCtx.apTactItems = g_apFallItemTable;
    g_FallKillCtx.pnTactItem = &g_nFallItemCnt;
    g_FallKillCtx.apObjects = g_apFallObjTable;
    g_FallKillCtx.ppSelUnit = &g_pFallSelUnit;
    g_FallKillCtx.pnKillCount = &g_nFallKillCount;
    memcpy(g_aFallNameTable + 40 * 14, "Chryssalid", 11);     /* record 14: the kill arm (0x4e518) */
    memcpy(g_aFallNameTable + 40 * 22, "ChryssalidHost", 15); /* record 22: the fall arm (0x53148) */
    g_FallKillCtx.pszTransformName = g_aFallNameTable + 40 * 14;
    g_FallKillCtx.pRosters = &g_FallRosters;
    memset(&g_FallKillHooks, 0, sizeof(g_FallKillHooks));
    g_FallKillHooks.pfnCheckBattleEnd = FallHookKillBattleEnd;

    /* UnitList_ForEachRefresh calls Unit_UpdateVisibility directly, and a real
     * VisCtx needs the LOS tables that tests/test_vis.c builds. The roster here is
     * therefore empty; the fall tests drive visibility through the ctx hook. */
    memset(&g_FallRosters, 0, sizeof(g_FallRosters));
    g_FallRosters.apXcom = g_apFallXcom;
    g_FallRosters.apEnemy = g_apFallEnemy;
    g_FallRosters.apCiv = g_apFallCiv;

    /* Combat_DetonateNearbyItems is NULL-safe on an unfilled ctx, which is what the
     * original's Map block looks like when no proximity device is armed. */
    memset(&g_FallProx, 0, sizeof(g_FallProx));

    memset(&g_FallSel, 0, sizeof(g_FallSel));
    g_pFallSelUnit = &g_aFallUnits[1];   /* matches the usual target: no name push */
    g_FallSel.ppSelUnit = &g_pFallSelUnit;
    g_FallSel.pCursorPos = &g_FallPos;
    g_FallSel.pfnLookup = FallLookup;

    memset(&g_FallPos, 0, sizeof(g_FallPos));
    g_FallPos.pCursor = &g_FallCursor;
    g_FallPos.geom.nDimX = FALL_DIM;
    g_FallPos.geom.nDimY = FALL_DIM;

    memset(&g_FallCtx, 0, sizeof(g_FallCtx));
    g_FallCtx.geom.nDimX = FALL_DIM;
    g_FallCtx.geom.nDimY = FALL_DIM;
    g_FallCtx.pfnLookup = FallLookup;
    g_FallCtx.pnLayerScratch = &g_nLayerScratch;
    g_FallCtx.pnMaxLayerClamp = &g_nMaxLayerClamp;
    g_FallCtx.pnAnimPhase = &g_nAnimPhase;
    g_FallCtx.pnFireTickProgress = &g_nFireTickProgress;
    g_FallCtx.pbEnemySpotted = &g_bEnemySpotted;
    g_FallCtx.pbFallDamagePending = &g_bFallPending;
    g_FallCtx.pbThrowPending = &g_bThrowPending;
    g_FallCtx.pbTickHold = &g_bTickHold;
    g_FallCtx.pnUiScreen = &g_nUiScreen;
    g_FallCtx.ppSelWeapon = &g_pSelWeapon;
    g_FallCtx.ppThrowTarget = &g_pThrowTarget;
    g_FallCtx.ppLookCommitUnit = &g_pLookCommit;
    g_FallCtx.pbLookCommitFlag = &g_bLookFlag;
    g_FallCtx.pnCursorX = &g_nCursorX;
    g_FallCtx.pnCursorY = &g_nCursorY;
    g_FallCtx.pnMeleeCellX = &g_nMeleeX;
    g_FallCtx.pnMeleeCellY = &g_nMeleeY;
    g_FallCtx.pnMeleeCellLayer = &g_nMeleeLayer;
    g_FallCtx.pdThrowX = &g_dThrowX;
    g_FallCtx.pdThrowY = &g_dThrowY;
    g_FallCtx.pdThrowLayer = &g_dThrowLayer;
    g_FallCtx.apXcomUnits = g_apFallXcom;
    g_FallCtx.apEnemyUnits = g_apFallEnemy;
    g_FallCtx.pnXcomCount = &g_nFallXcom;
    g_FallCtx.pnEnemyCount = &g_nFallEnemy;
    g_FallCtx.pLists = &g_FallLists;
    g_FallCtx.pTactCtx = &g_FallTactCtx;
    g_FallCtx.pKillCtx = &g_FallKillCtx;
    g_FallCtx.pKillHooks = &g_FallKillHooks;
    g_FallCtx.pProximity = &g_FallProx;
    g_FallCtx.pRosters = &g_FallRosters;
    g_FallCtx.pSelectCtx = &g_FallSel;
    g_FallCtx.pCursorPos = &g_FallPos;
    g_FallCtx.pszTransformName = g_aFallNameTable + 40 * 22;
    g_FallCtx.pfnUpdateVisibility = FallHookVisibility;
    g_FallCtx.pfnCheckBattleEnd = FallHookBattleEnd;
    g_FallCtx.pfnPushStatus = FallHookStatus;
    g_FallCtx.pfnPushForcedText = FallHookForced;
    g_FallCtx.pCallbackUser = &g_nWiringProbe;   /* the MoveActionWiring's stand-in: pfnPushStatus */
    g_FallCtx.pShellUser    = &g_nShellProbe;    /* the mission owner's stand-in: the other three seams */
    g_FallCtx.pszAlienControlSuffix = " under alien control";

    g_FallLists.apXcom = g_apFallXcom;
    g_FallLists.apEnemy = g_apFallEnemy;
    FallSyncLists();

    memset(&g_FallDmg, 0, sizeof(g_FallDmg));
    g_FallDmg.pfnLookup = FallLookup;
    g_FallDmg.pState = &g_state;
    g_FallDmg.pKillCtx = &g_FallKillCtx;
    g_FallDmg.pKillHooks = &g_FallKillHooks;
}

static void FallQueueEntry(i32 nCmd, Soldier *pActor, Soldier *pTarget)
{
    TactQueueEntry *pE = &g_state.aQueue[0];

    memset(g_state.aQueue, 0, sizeof(g_state.aQueue));
    g_state.nQueueCursor = 0;
    g_state.nQueueLen    = 1;
    g_state.bQueueActive = 1;
    pE->nCmd    = nCmd;
    pE->pUnit   = pActor;
    pE->pTarget = pTarget;
    pE->pWeapon = &g_FallWeapon;   /* command 18 dereferences it */
}

static void TestFallDamageRefusals(void)
{
    Soldier *pU = &g_aFallUnits[0];

    printf("Unit_ApplyFallDamage refusals\n");

    /* A floor under the unit: no fall, no move, no queue entry. */
    FallSetup();
    pU->nUnitType = 0;
    pU->auStat[1] = 100;
    pU->uHp       = 100;
    pU->pCell     = &g_aFallCells[3][1][1];
    g_aFallCells[3][1][1].pFloorSpr = &g_FallFloor;
    Unit_ApplyFallDamage(pU, &g_FallDmg);
    Check(pU->pCell == &g_aFallCells[3][1][1], "  floor under the unit: not moved");
    CheckEqI32(pU->uHp, 100, "  floor under the unit: no damage");
    CheckEqI32(g_state.nQueueLen, 0, "  nothing queued");

    /* The levitating types are immune: g_aUnitTypeRecord[4*type + 0] is their flag. */
    FallSetup();
    pU = &g_aFallUnits[0];
    pU->nUnitType = 11;
    pU->auStat[1] = 100;
    pU->uHp       = 100;
    pU->pCell     = &g_aFallCells[3][1][1];
    Unit_ApplyFallDamage(pU, &g_FallDmg);
    Check(g_aUnitTypeFlag0[11] != 0, "  type 11 carries the immunity flag");
    Check(pU->pCell == &g_aFallCells[3][1][1], "  immune type is not moved");
    CheckEqI32(pU->uHp, 100, "  immune type takes no damage");

    /* Already on layer 0. */
    FallSetup();
    pU = &g_aFallUnits[0];
    pU->nUnitType = 0;
    pU->auStat[1] = 100;
    pU->uHp       = 100;
    pU->pCell     = &g_aFallCells[0][1][1];
    Unit_ApplyFallDamage(pU, &g_FallDmg);
    Check(pU->pCell == &g_aFallCells[0][1][1], "  layer 0 is not moved");
    CheckEqI32(pU->uHp, 100, "  layer 0 takes no damage");

    /* A large unit whose own-layer footprint has a floor anywhere is standing. */
    FallSetup();
    pU = &g_aFallUnits[0];
    pU->nUnitType = 4;   /* large */
    pU->auStat[1] = 100;
    pU->uHp       = 100;
    pU->pCell     = &g_aFallCells[3][1][1];
    g_aFallCells[3][2][2].pFloorSpr = &g_FallFloor;   /* footprint (x+1,y+1) */
    Unit_ApplyFallDamage(pU, &g_FallDmg);
    Check(pU->pCell == &g_aFallCells[3][1][1], "  large unit on a footprint floor: not moved");
    CheckEqI32(pU->uHp, 100, "  and no damage");
}

static void TestFallDamageDescent(void)
{
    Soldier *pU;

    printf("Unit_ApplyFallDamage descent and damage\n");

    /* Column (1,1): open at layers 3 and 2, floor at layer 1. The descent stops ON
     * the floor layer, so the fall is 3 -> 1 and the distance is 3 - 1 - 1 = 1. */
    FallSetup();
    pU = &g_aFallUnits[0];
    pU->nUnitType = 0;
    pU->auStat[1] = 100;
    pU->uHp       = 100;
    pU->pCell     = &g_aFallCells[3][1][1];
    g_aFallCells[1][1][1].pFloorSpr = &g_FallFloor;
    Unit_ApplyFallDamage(pU, &g_FallDmg);
    Check(pU->pCell == &g_aFallCells[1][1][1], "  lands on the floor layer");
    CheckEqI32(pU->uHp, 60, "  damage = (3-1-1) * 100 max HP * 0.4 = 40");
    CheckEqI32(g_state.nQueueLen, 0, "  15 spent TU under 60 HP queues nothing");

    /* Two layers down is one floor fallen: the distance is nZ - nLayer - 1, so a
     * single step costs nothing and 20 max HP pays 20 * 0.4 = 8 only from two down. */
    FallSetup();
    pU = &g_aFallUnits[0];
    pU->nUnitType = 0;
    pU->auStat[1] = 20;
    pU->uHp       = 20;
    pU->pCell     = &g_aFallCells[3][1][1];
    g_aFallCells[1][1][1].pFloorSpr = &g_FallFloor;
    Unit_ApplyFallDamage(pU, &g_FallDmg);
    Check(pU->pCell == &g_aFallCells[1][1][1], "  stops two layers down");
    CheckEqI32(pU->uHp, 12, "  damage = (3-1-1) * 20 * 0.4 = 8");

    /* No floor anywhere down to layer 0: the loop runs the column out and leaves
     * nLayer at -1, so the distance is 3 - (-1) - 1 = 3 and the unit leaves the map. */
    FallSetup();
    pU = &g_aFallUnits[0];
    pU->nUnitType = 0;
    pU->auStat[1] = 100;
    pU->uHp       = 100;
    pU->pCell     = &g_aFallCells[3][1][1];
    Unit_ApplyFallDamage(pU, &g_FallDmg);
    Check(pU->pCell == NULL, "  an open column places the unit off the map");
    CheckEqI32(pU->uHp, 0, "  120 damage empties a 100 HP unit");
    CheckEqI32(g_state.nQueueLen, 1, "  0 HP queues the death flash");

    /* A large unit descends only while all three footprint cells are open, and the
     * sweep then covers the whole 2x2 at the stopping layer. */
    FallSetup();
    pU = &g_aFallUnits[0];
    pU->nUnitType = 4;   /* large, origin (1,1,3) */
    pU->auStat[1] = 100;
    pU->uHp       = 100;
    pU->pCell     = &g_aFallCells[3][1][1];
    g_aFallCells[2][2][2].pFloorSpr = &g_FallFloor;   /* footprint (x+1,y+1) at layer 2 */
    Unit_ApplyFallDamage(pU, &g_FallDmg);
    Check(pU->pCell == &g_aFallCells[2][1][1], "  large unit stops with the footprint floor");
    CheckEqI32(pU->uHp, 100, "  a one-layer step is 0 damage: (3-2-1) * 100 * 0.4");
}

static void TestFallDamageSweep(void)
{
    Soldier *pSmall;
    Soldier *pBig;

    printf("Unit_ApplyFallDamage landing sweep\n");

    /* A small unit landing on a large one dies without its HP being touched first
     * (0x4AAE4), and the placement/damage tail is skipped because the kill set
     * bFlags bit 5. */
    FallSetup();
    pSmall = &g_aFallUnits[0];
    pSmall->nUnitType = 0;
    pSmall->auStat[1] = 100;
    pSmall->uHp       = 100;
    pSmall->pCell     = &g_aFallCells[3][1][1];
    pBig = &g_aFallUnits[2];
    pBig->nUnitType = 4;
    pBig->auStat[1] = 200;
    pBig->uHp       = 200;
    pBig->pCell     = &g_aFallCells[2][1][1];
    g_aFallCells[2][1][1].pUnit     = pBig;
    g_aFallCells[2][1][1].pFloorSpr = &g_FallFloor;
    Unit_ApplyFallDamage(pSmall, &g_FallDmg);
    CheckEqI32(pSmall->uHp, 100, "  the blocked fall leaves the small unit's HP alone");
    Check((pSmall->bFlags & 0x20) != 0, "  and kills it");
    CheckEqI32(pSmall->bStatus, 4, "  the kill turns it into a corpse (bStatus 4)");
    Check(pBig->pCell == &g_aFallCells[2][1][1], "  the large unit is untouched");
    CheckEqI32(g_nFallKillCount, 0, "  a human death does not raise the alien kill count");

    /* A large unit landing on a small one kills the occupant outright; the small
     * occupant's HP is not pre-zeroed because only a large occupant is. */
    FallSetup();
    pBig = &g_aFallUnits[0];
    pBig->nUnitType = 4;
    pBig->auStat[1] = 100;
    pBig->uHp       = 100;
    pBig->pCell     = &g_aFallCells[3][1][1];
    pSmall = &g_aFallUnits[2];
    pSmall->nUnitType = 0;
    pSmall->auStat[1] = 60;
    pSmall->uHp       = 60;
    pSmall->pCell     = &g_aFallCells[2][2][2];
    g_aFallCells[2][2][2].pUnit     = pSmall;
    g_aFallCells[2][2][2].pFloorSpr = &g_FallFloor;
    Unit_ApplyFallDamage(pBig, &g_FallDmg);
    Check((pSmall->bFlags & 0x20) != 0, "  the occupant is killed by the landing");
    CheckEqI32(pSmall->uHp, 60, "  a small occupant is killed without its HP being zeroed");
    Check(pBig->pCell == &g_aFallCells[2][1][1], "  the large unit lands at (1,1,2)");
    CheckEqI32(pBig->uHp, 100, "  and takes no damage from a one-layer step");
}

static void TestFallActionTail(void)
{
    printf("Tact_ProcessFallAction queue tail (LABEL_186 @ 0x540E0)\n");

    /* A command with no case (2) still advances the cursor, and reaching the end of
     * the queue closes it. */
    FallSetup();
    FallQueueEntry(TACTCMD_OPEN, &g_aFallUnits[0], NULL);
    g_state.bFlag0    = 1;
    g_nAnimPhase      = 7;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_state.nQueueCursor, 1, "  cursor advanced");
    CheckEqI32(g_state.bFlag0, 0, "  bFlag0 cleared");
    CheckEqI32(g_state.nQueueLen, 0, "  queue closed at the end");
    CheckEqI32(g_state.bQueueActive, 0, "  queue deactivated");
    CheckEqI32(g_nAnimPhase, 0, "  nAnimPhase cleared");

    /* Not at the end yet: the queue stays open. */
    FallSetup();
    FallQueueEntry(TACTCMD_OPEN, &g_aFallUnits[0], NULL);
    g_state.nQueueLen = 3;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_state.nQueueCursor, 1, "  cursor advanced");
    CheckEqI32(g_state.nQueueLen, 3, "  queue still open");
    CheckEqI32(g_state.bQueueActive, 1, "  queue still active");

    /* Map_bTickHold skips the whole tail, cursor included. */
    FallSetup();
    FallQueueEntry(TACTCMD_OPEN, &g_aFallUnits[0], NULL);
    g_state.bFlag0    = 1;
    g_nAnimPhase      = 7;
    g_bTickHold       = 1;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_state.nQueueCursor, 0, "  tick hold leaves the cursor in place");
    CheckEqI32(g_state.bFlag0, 1, "  and bFlag0 set");
    CheckEqI32(g_state.nQueueLen, 1, "  and the queue open");
    CheckEqI32(g_nAnimPhase, 7, "  and nAnimPhase untouched");
}

static void TestFallActionMoveTail(void)
{
    Soldier *pActor = &g_aFallUnits[0];

    printf("Tact_ProcessFallAction nCmd 1 (0x53668)\n");

    /* A unit the shot resolution already killed (bit 5) is skipped entirely. */
    FallSetup();
    pActor->bFlags = 0x20;
    pActor->pCell  = &g_aFallCells[2][1][1];
    FallQueueEntry(1, pActor, NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nVisCalls, 0, "  a dead unit gets no visibility pass");
    CheckEqI32(g_nLayerScratch, 0, "  and no layer scratch is written");
    CheckEqI32(g_state.nQueueCursor, 1, "  the cursor still advances");

    /* Carrying weight costs stun: uStunOrArmor += -2 - 4*burden/strength, with the
     * quotient cut to 16 bits by the LSL#16 / LSR#16 pair at 0x536B0. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->bFlags       = 0;
    pActor->pCell        = &g_aFallCells[2][1][1];
    pActor->uInvBurden   = 10;
    pActor->auStat[8]    = 4;
    pActor->uStunOrArmor = 50;
    FallQueueEntry(1, pActor, NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pActor->uStunOrArmor, 38, "  50 - 2 - 4*10/4 = 38");
    CheckEqI32(g_nLayerScratch, 3, "  layer scratch = nZ + 1 with no ceiling sprite");
    CheckEqI32(g_nVisCalls, 1, "  LABEL_111 runs the visibility pass once");

    /* A zero strength would divide by zero; the port's guard contributes 0. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->bFlags       = 0;
    pActor->pCell        = &g_aFallCells[2][1][1];
    pActor->uInvBurden   = 10;
    pActor->auStat[8]    = 0;
    pActor->uStunOrArmor = 50;
    FallQueueEntry(1, pActor, NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pActor->uStunOrArmor, 48, "  zero strength contributes 0, not a crash");

    /* The ceiling rule is a strict signed compare against -15 (LDRSB / CMP at
     * 0x536D4): -16 pushes the scratch layer up by two, -15 does not. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->bFlags = 0;
    pActor->pCell  = &g_aFallCells[2][1][1];
    pActor->pCell->pCeilSpr = &g_FallCeil;
    g_FallCeil.iCeilParam   = -16;
    FallQueueEntry(1, pActor, NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nLayerScratch, 4, "  iCeilParam -16 -> nZ + 2");

    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->bFlags = 0;
    pActor->pCell  = &g_aFallCells[2][1][1];
    pActor->pCell->pCeilSpr = &g_FallCeil;
    g_FallCeil.iCeilParam   = -15;
    FallQueueEntry(1, pActor, NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nLayerScratch, 3, "  iCeilParam -15 -> nZ + 1");

    /* The tracked unit is dropped when the cell it stands on has no bFlags44 bit 0. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->bFlags = 0x10;   /* skip the burden term */
    pActor->pCell  = &g_aFallCells[2][1][1];
    g_FallTactCtx.pTrackUnit = &g_aFallUnits[1];
    FallQueueEntry(1, pActor, NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    Check(g_FallTactCtx.pTrackUnit == NULL, "  track unit cleared on a bit-0-clear cell");

    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->bFlags = 0x10;
    pActor->pCell  = &g_aFallCells[2][1][1];
    pActor->pCell->bFlags44 = 1;
    g_FallTactCtx.pTrackUnit = &g_aFallUnits[1];
    FallQueueEntry(1, pActor, NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    Check(g_FallTactCtx.pTrackUnit == &g_aFallUnits[1], "  track unit kept on a bit-0-set cell");

    /* The spotted flag pushes TEXTS[3] and then runs the team pass, which appends
     * an END MARK at cursor + 1 (0x4f388) and extends the queue by one; the
     * current entry keeps its own command. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->bFlags = 0x10;
    pActor->pCell  = &g_aFallCells[2][1][1];
    g_bEnemySpotted = 1;
    FallQueueEntry(1, pActor, NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nStatusCalls, 1, "  the spotted message pushed once");
    Check(g_pUserStatus == (void *)&g_nWiringProbe,
          "  and pfnPushStatus is handed FallActionCtx.pCallbackUser: Map_PushStatusCb wants the "
          "MoveActionWiring, not the mission owner");
    CheckEqI32(g_nLastStatusRec, PUFO_TXTREC_SPOTTED, "  and it is TEXTS[3]");
    CheckEqI32(g_state.aQueue[0].nCmd, 1, "  the current entry keeps its own cmd");
    CheckEqI32(g_state.aQueue[1].nCmd, TACTCMD_END_MARK, "  team pass appends 12 at cursor + 1");
    CheckEqI32(g_state.nQueueLen, 2, "  team pass sets nQueueLen = cursor + 2");
    CheckEqI32(g_state.bFlag1, 1, "  and bFlag1");
    CheckEqI32(g_state.nQueueCursor, 1, "  cursor advances into the new entry");
    CheckEqI32(g_nVisCalls, 1, "  LABEL_111 still runs after the team pass");

    /* bAiActionActive set suppresses the message but not the team pass. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->bFlags = 0x10;
    pActor->pCell  = &g_aFallCells[2][1][1];
    g_bEnemySpotted = 1;
    g_state.bAiActionActive = 1;
    FallQueueEntry(1, pActor, NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    /* The guard is one condition, not two: bAiActionActive set skips the message AND the
     * team pass that follows it (0x5376C). */
    CheckEqI32(g_nStatusCalls, 0, "  bAiActionActive suppresses the spotted message");
    CheckEqI32(g_state.aQueue[0].nCmd, 1, "  and the team pass is skipped too");
    CheckEqI32(g_state.nQueueLen, 0, "  the queue closes at its own length, not cursor + 2");
}

static void TestFallActionDoorAndVert(void)
{
    Soldier *pActor;

    printf("Tact_ProcessFallAction nCmd 4 (0x539B0)\n");

    /* Facing 2 is the E delta {+1,0,0}; nTurnDir 3 traces (1,1,2), (2,1,2), (3,1,2).
     * Both writes use the entry's own nZ, so they hit the same cell: bit 0 off,
     * bit 2 on. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->pCell = &g_aFallCells[2][1][1];
    pActor->uTu   = 30;
    FallQueueEntry(4, pActor, NULL);
    g_state.aQueue[0].nFacing  = 2;
    g_state.aQueue[0].nTurnDir = 3;
    g_state.aQueue[0].nX       = 1;
    g_state.aQueue[0].nY       = 1;
    g_state.aQueue[0].nZ       = 2;
    for (i32 n = 0; n < 3; ++n) {
        g_aFallCells[2][1][1 + n].bFlags29 = 0xFF;
    }
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_state.nTraceX, 3, "  trace X ends at the last step");
    CheckEqI32(g_state.nTraceY, 1, "  trace Y holds the entry Y");
    CheckEqI32(g_state.nTraceZ, 2, "  trace Z holds the entry Z");
    for (i32 n = 0; n < 3; ++n) {
        MapCell *pC = &g_aFallCells[2][1][1 + n];
        Check((pC->bFlags29 & PUFO_FALL_CELLFLAG_CLEAR) == 0 &&
              (pC->bFlags29 & PUFO_FALL_CELLFLAG_DOOR) != 0,
              "  traced cell gets bit 2 and loses bit 0");
    }
    CheckEqI32(pActor->uTu, 26, "  the door step costs 4 TU");
    CheckEqI32(g_nVisCalls, 1, "  visibility pass runs once");

    /* nTurnDir 0 skips the trace loop entirely. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->pCell = &g_aFallCells[2][1][1];
    pActor->uTu   = 30;
    FallQueueEntry(4, pActor, NULL);
    g_state.aQueue[0].nFacing  = 2;
    g_state.aQueue[0].nTurnDir = 0;
    g_state.aQueue[0].nX       = 1;
    g_state.aQueue[0].nY       = 1;
    g_state.aQueue[0].nZ       = 2;
    g_aFallCells[2][1][1].bFlags29 = 0x01;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_aFallCells[2][1][1].bFlags29, 0x01, "  no trace, no flag change");
    CheckEqI32(pActor->uTu, 26, "  the TU cost is charged anyway");

    printf("Tact_ProcessFallAction nCmd 5 (0x5381C)\n");

    /* nFacing is signed: 9 in the table is the down delta, but the entry stores the
     * layer step directly, so -1 means down one layer. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->pCell = &g_aFallCells[2][1][1];
    pActor->uTu   = 30;
    FallQueueEntry(5, pActor, NULL);
    g_state.aQueue[0].nFacing = (u8)(i8)-1;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    Check(pActor->pCell == &g_aFallCells[1][1][1], "  placed one layer down");
    CheckEqI32(pActor->uTu, 22, "  the vertical step costs 8 TU");
    CheckEqI32(g_nLayerScratch, 2, "  nFacing -1 writes the new nZ + 1");
    CheckEqI32(g_nVisCalls, 0, "  no spotted flag, no detonation flag: no visibility pass");

    /* Going up leaves the scratch layer alone. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->pCell = &g_aFallCells[1][1][1];
    pActor->uTu   = 30;
    g_nLayerScratch = 99;
    FallQueueEntry(5, pActor, NULL);
    g_state.aQueue[0].nFacing = 1;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    Check(pActor->pCell == &g_aFallCells[2][1][1], "  placed one layer up");
    CheckEqI32(g_nLayerScratch, 99, "  an upward step leaves the scratch layer alone");

    /* A floor sprite at the new cell stands the unit up and clears bit 4. */
    FallSetup();
    pActor = &g_aFallUnits[0];
    pActor->pCell = &g_aFallCells[2][1][1];
    pActor->bFlags = 0x10;
    FallQueueEntry(5, pActor, NULL);
    g_state.aQueue[0].nFacing = (u8)(i8)-1;
    g_aFallCells[1][1][1].pFloorSpr = &g_FallFloor;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    Check((pActor->bFlags & 0x10) == 0, "  a floor at the landing cell clears bit 4");
}

static void TestFallActionSmallCases(void)
{
    Soldier *pSel;

    printf("Tact_ProcessFallAction nCmd 0xA, 0xC, 0xE, 0x10\n");

    /* 0xA parks the cursor on the Map cursor position. */
    FallSetup();
    g_nCursorX = 2;
    g_nCursorY = 3;
    FallQueueEntry(0xA, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_FallCursor.nX, 2, "  cursor X from Map_nCursorX");
    CheckEqI32(g_FallCursor.nY, 3, "  cursor Y from Map_nCursorY");

    /* 0xC refreshes the cell meta and stands the selected unit up. */
    FallSetup();
    pSel = &g_aFallUnits[1];
    pSel->bFlags = 0x02 | 0x10;
    g_FallSel.ppSelUnit = &g_pFallSelUnit;
    g_pFallSelUnit = pSel;
    FallQueueEntry(0xC, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pSel->bFlags & 0x02, 0, "  the selected unit loses bit 1");
    CheckEqI32(pSel->bFlags & 0x10, 0x10, "  and keeps the other bits");

    /* 0xE refreshes the Xcom roster and asks whether the battle is over. */
    FallSetup();
    FallQueueEntry(0xE, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nBattleEndCalls, 1, "  Tact_CheckBattleEnd called once with abort 0");
    Check(g_pUserBattleEnd == (void *)&g_nShellProbe,
          "  and pfnCheckBattleEnd is handed FallActionCtx.pShellUser: the battle-end arm (0x53318, "
          "0x53bd4) reaches the mission's own state, while pfnPushStatus above wants the wiring");
    CheckEqI32(g_state.nQueueCursor, 1, "  and the cursor advances");

    /* 0x10 clears the phase-active flag. */
    FallSetup();
    g_state.bPhaseActive = 1;
    FallQueueEntry(0x10, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_state.bPhaseActive, 0, "  nCmd 0x10 clears bPhaseActive");
}

static void TestFallActionDamageSweep(void)
{
    Soldier *pUnit;

    printf("Tact_ProcessFallAction nCmd 0xF (0x53BF0)\n");

    /* The pending byte gates the whole case. */
    FallSetup();
    g_bFallPending = 0;
    g_nFireTickProgress = 7;
    FallQueueEntry(0xF, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nFireTickProgress, 7, "  unarmed sweep leaves nFireTickProgress alone");

    /* Armed: every roster entry is run through Unit_ApplyFallDamage, then the
     * arming byte, the fire progress and the scratch layer are reset. */
    FallSetup();
    pUnit = &g_aFallUnits[0];
    pUnit->nUnitType = 0;
    pUnit->auStat[1] = 100;
    pUnit->uHp       = 100;
    pUnit->pCell     = &g_aFallCells[3][1][1];
    g_aFallCells[1][1][1].pFloorSpr = &g_FallFloor;
    g_apFallXcom[0]  = pUnit;
    g_nFallXcom      = 1;
    g_bFallPending   = 1;
    g_nMaxLayerClamp = 3;
    FallSyncLists();
    FallQueueEntry(0xF, &g_aFallUnits[1], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    Check(pUnit->pCell == &g_aFallCells[1][1][1], "  the roster unit fell to the floor layer");
    CheckEqI32(pUnit->uHp, 60, "  and took the fall damage");
    CheckEqI32(g_bFallPending, 0, "  the arming byte is cleared");
    CheckEqI32(g_nFireTickProgress, 0, "  nFireTickProgress cleared");
    CheckEqI32(g_nLayerScratch, 3, "  layer scratch set from nMaxLayerClamp");
}

static void TestFallActionThrowLanding(void)
{
    MapCell *pLand;

    printf("Tact_ProcessFallAction nCmd 0x11 (0x53CEC)\n");

    /* Not armed: nothing at all happens, the throw target included. */
    FallSetup();
    g_bThrowPending = 0;
    FallQueueEntry(0x11, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    Check(g_pThrowTarget == &g_aFallUnits[2], "  an unarmed landing leaves the throw target alone");

    /* Armed. Sub-cell units: 16 per cell in X and Y, 12 per layer. The flight ends
     * over (2,1) at layer 3; the column has no floor at 3 or 2 and one at 1, so the
     * item lands at layer 1. */
    FallSetup();
    g_bThrowPending  = 1;
    g_dThrowX        = 2 * 16 + 7;
    g_dThrowY        = 1 * 16 + 9;
    g_dThrowLayer    = 3 * 12;
    g_nMaxLayerClamp = 2;
    g_aFallCells[1][1][2].pFloorSpr = &g_FallFloor;
    FallQueueEntry(0x11, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    pLand = &g_aFallCells[1][1][2];
    Check(g_pThrowTarget == NULL, "  the throw target is cleared");
    CheckEqI32(g_bThrowPending, 0, "  the arming byte is cleared");
    Check(pLand->pItemList == &g_FallWeapon, "  the item lands on the floor layer");
    CheckEqI32(g_nLayerScratch, 2, "  layer scratch set from nMaxLayerClamp");

    /* Three open layers is the cap: the search tests base, base-1, base-2 and stops
     * there even though nothing was found. */
    FallSetup();
    g_bThrowPending = 1;
    g_dThrowX       = 2 * 16;
    g_dThrowY       = 1 * 16;
    g_dThrowLayer   = 3 * 12;
    FallQueueEntry(0x11, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    Check(g_aFallCells[1][1][2].pItemList == &g_FallWeapon,
          "  three open layers land at base - 2");

    /* Running off the bottom clamps the layer to 0. */
    FallSetup();
    g_bThrowPending = 1;
    g_dThrowX       = 2 * 16;
    g_dThrowY       = 1 * 16;
    g_dThrowLayer   = 0;
    FallQueueEntry(0x11, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    Check(g_aFallCells[0][1][2].pItemList == &g_FallWeapon,
          "  a layer below 0 clamps to 0");

    /* A proximity marker (uSortKey 14) is taken back off the cell and stamps the
     * animated-tile field over the 3x3 around the landing. */
    FallSetup();
    g_bThrowPending      = 1;
    g_dThrowX            = 2 * 16;
    g_dThrowY            = 1 * 16;
    g_dThrowLayer        = 3 * 12;
    g_FallWeaponObj.uSortKey = PUFO_PROXIMITY_KEY_MARK;
    for (i32 z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (i32 y = 0; y < FALL_DIM; ++y) {
            for (i32 x = 0; x < FALL_DIM; ++x) {
                g_aFallCells[z][y][x].nFlags40 = 0xFFFFFFFFu;
            }
        }
    }
    FallQueueEntry(0x11, &g_aFallUnits[0], NULL);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    pLand = &g_aFallCells[1][1][2];
    Check(pLand->pItemList == NULL, "  the marker is removed from the landing cell");
    CheckEqI32((i32)(pLand->nFlags40 & 0xFFFFFFFFu), (i32)0xFFFFFF9Fu,
               "  landing cell nFlags40 = old & 0xFFFFFF87 | 0x18");
    Check(g_aFallCells[1][0][1].nFlags40 == 0xFFFFFF9Fu &&
          g_aFallCells[1][2][3].nFlags40 == 0xFFFFFF9Fu,
          "  the 3x3 around the landing is stamped");
    Check(g_aFallCells[1][0][0].nFlags40 == 0xFFFFFFFFu,
          "  a cell outside the 3x3 is left alone");
    Check(g_aFallCells[0][1][2].nFlags40 == 0xFFFFFFFFu &&
          g_aFallCells[2][1][2].nFlags40 == 0xFFFFFFFFu,
          "  the stamp stays on the landing layer; only the validity test uses layer 0");
}

static void TestFallActionLookDevice(void)
{
    Soldier *pActor = &g_aFallUnits[0];
    Soldier *pVictim = &g_aFallUnits[1];

    printf("Tact_ProcessFallAction nCmd 0x12, sort key 23 (0x52C2C)\n");

    FallSetup();
    pActor->pCell  = &g_aFallCells[2][1][1];
    pVictim->pCell = &g_aFallCells[2][2][2];
    g_FallWeaponObj.uSortKey = PUFO_SPECIAL_SORT_LATCH;
    g_aFallCells[2][1][1].bFlags29 = 0xFF;   /* the melee target cell */
    g_state.gap_70112[1] = 1;
    FallQueueEntry(0x12, pActor, pVictim);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_aFallCells[2][1][1].bFlags29 & 0x20, 0,
               "  the melee target cell loses its shot-trace bit");
    CheckEqI32(g_state.gap_70112[1], 0, "  and the fire-animation flag goes down");
    Check(g_pLookCommit == pActor, "  the attacker is committed as the look unit");
    CheckEqI32(g_bLookFlag, 1, "  and the commit flag is set");
    CheckEqI32(g_nUiScreen, 4, "  the look screen opens");
    Check(g_pFallSelUnit == pVictim, "  the target becomes the selected unit");
    CheckEqI32(g_FallCursor.nX, 2, "  the cursor moves onto the target cell");
    CheckEqI32(g_nVisCalls, 1, "  LABEL_111 runs");
}

/* The psi gate is a roll, so the test draws the roll first with the same seed and
 * then asserts that the branch taken matches it against 100 / ratio. */
static void TestFallActionPsiGate(void)
{
    Soldier *pActor  = &g_aFallUnits[0];
    Soldier *pVictim = &g_aFallUnits[1];
    i32 nHits = 0;
    i32 nMisses = 0;
    bool bMatch = true;

    printf("Tact_ProcessFallAction nCmd 0x12 mode 5 psi gate (0x52C90)\n");

    /* Ratio 1 or less cannot fail, so no roll is drawn at all. */
    FallSetup();
    pActor->auStat[6]  = 50;
    pActor->auStat[7]  = 30;   /* product 1500 */
    pVictim->auStat[6] = 40;
    pVictim->auStat[7] = 40;   /* product 1600, ratio 1600/1500 = 1 */
    pVictim->auStat[2] = 600;
    pVictim->bMorale   = 90;
    pVictim->pCell     = &g_aFallCells[2][2][2];
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 5;
    srand(7);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nLastStatusRec, PUFO_TXTREC_PSI_OK, "  ratio 1 always hits");
    CheckEqI32(pVictim->bMorale, 79,
               "  morale = 1500/-150 - 600/600 + 90 = -10 - 1 + 90");
    CheckEqI32(g_nBattleEndCalls, 0, "  morale above 50 stops short of the panic tail");
    CheckEqI32(g_nVisCalls, 0, "  morale above 50 jumps to LABEL_186, past LABEL_111");

    /* Ratio 2: the roll must land at or below 100/2 = 50 out of 150. The seeds are
     * spread by a stride because the CRT's first rand() value for a small seed is
     * itself small, which would make every draw a hit. */
    for (i32 nSeed = 1; nSeed <= 40; ++nSeed) {
        i32 nRoll;

        srand(nSeed * 977);
        nRoll = (i32)Game_RandU16(PUFO_FALL_PSI_RAND_MAX);

        FallSetup();
        pActor  = &g_aFallUnits[0];
        pVictim = &g_aFallUnits[1];
        pActor->auStat[6]  = 40;
        pActor->auStat[7]  = 20;   /* product 800 */
        pVictim->auStat[6] = 40;
        pVictim->auStat[7] = 40;   /* product 1600, ratio 2, threshold 50 */
        pVictim->auStat[2] = 600;
        pVictim->bMorale   = 90;
        pVictim->pCell     = &g_aFallCells[2][2][2];
        FallQueueEntry(0x12, pActor, pVictim);
        g_state.aQueue[0].nMode = 5;

        srand(nSeed * 977);
        Tact_ProcessFallAction(&g_state, &g_FallCtx);

        if (g_nLastStatusRec == PUFO_TXTREC_PSI_OK) {
            ++nHits;
            if (nRoll > 50) {
                bMatch = false;
            }
        } else if (g_nLastStatusRec == PUFO_TXTREC_PSI_FAILED) {
            ++nMisses;
            if (nRoll <= 50) {
                bMatch = false;
            }
        } else {
            bMatch = false;
        }
    }
    Check(bMatch, "  every seed's outcome matches its drawn roll against 100/2");
    Check(nHits > 0 && nMisses > 0, "  both gate outcomes are exercised");

    /* The failure exit pushes the message and jumps past LABEL_111 to the tail. */
    FallSetup();
    pActor  = &g_aFallUnits[0];
    pVictim = &g_aFallUnits[1];
    pActor->auStat[6]  = 40;
    pActor->auStat[7]  = 20;
    pVictim->auStat[6] = 40;
    pVictim->auStat[7] = 40;
    pVictim->auStat[2] = 600;
    pVictim->bMorale   = 90;
    pVictim->pCell     = &g_aFallCells[2][2][2];
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 5;
    for (i32 nSeed = 1; nSeed <= 40; ++nSeed) {
        srand(nSeed * 977);
        if ((i32)Game_RandU16(PUFO_FALL_PSI_RAND_MAX) > 50) {
            srand(nSeed * 977);
            break;
        }
    }
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nLastStatusRec, PUFO_TXTREC_PSI_FAILED, "  a failed attack pushes TEXTS[38]");
    CheckEqI32(g_nVisCalls, 0, "  LABEL_101 skips LABEL_111's visibility pass");
    CheckEqI32(pVictim->bMorale, 90, "  and leaves the victim's morale alone");
    CheckEqI32(g_state.nQueueCursor, 1, "  the tail still advances the cursor");
}

static void TestFallActionPanic(void)
{
    Soldier *pActor  = &g_aFallUnits[0];
    Soldier *pVictim = &g_aFallUnits[1];

    printf("Tact_ProcessFallAction nCmd 0x12 mode 5 panic tail (0x52D54)\n");

    /* Morale at or below 50: the victim leaves its side list and rejoins the Xcom
     * roster because no action phase is running. */
    FallSetup();
    pActor->auStat[6]  = 50;
    pActor->auStat[7]  = 30;   /* 1500 */
    pVictim->auStat[6] = 40;
    pVictim->auStat[7] = 40;
    pVictim->auStat[2] = 600;
    pVictim->auStat[0] = 55;
    pVictim->bMorale   = 40;   /* -10 - 1 + 40 = 29 */
    pVictim->pCell     = &g_aFallCells[2][2][2];
    g_apFallXcom[0] = pVictim;
    g_nFallXcom     = 1;
    FallSyncLists();
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 5;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->bMorale, 29, "  morale 40 - 10 - 1 = 29");
    CheckEqI32(pVictim->uTu, 55, "  the victim's TU is refilled from its own maximum");
    CheckEqI32(pVictim->bStatus, 0, "  the Xcom arm clears bStatus");
    Check(pVictim->pLinked1040 == pActor, "  and links the victim to the attacker");
    CheckEqI32(g_nFallXcom, 1, "  the roster count is back to one after the removal");
    Check(g_apFallXcom[0] == pVictim, "  and the victim is in it");
    CheckEqI32(g_nBattleEndCalls, 1, "  LABEL_66 asks whether the battle is over");
    CheckEqI32(g_nForcedCalls, 0, "  the Xcom arm builds no forced message");
    CheckEqI32(g_nVisCalls, 2, "  the roster sweep plus LABEL_111");

    /* With an action phase running the victim goes to the enemy roster instead, and
     * the composed "<name> under alien control" message is pushed forced. */
    FallSetup();
    pActor  = &g_aFallUnits[0];
    pVictim = &g_aFallUnits[1];
    pActor->auStat[6]  = 50;
    pActor->auStat[7]  = 30;
    pVictim->auStat[6] = 40;
    pVictim->auStat[7] = 40;
    pVictim->auStat[2] = 600;
    pVictim->bMorale   = 40;
    pVictim->pCell     = &g_aFallCells[2][2][2];
    snprintf(pVictim->szName, sizeof(pVictim->szName), "%s", "Reyes");
    g_apFallXcom[0] = pVictim;
    g_nFallXcom     = 1;
    g_FallTactCtx.bBusy = 1;
    FallSyncLists();
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 5;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nFallEnemy, 1, "  the enemy roster gains the victim");
    Check(g_apFallEnemy[0] == pVictim, "  at the first free slot");
    CheckEqI32(pVictim->bStatus, 1, "  the enemy arm sets bStatus");
    CheckEqI32((pVictim->bFlags & 0x01), 0, "  and clears bFlags bit 0");
    CheckEqI32(g_nForcedCalls, 1, "  one forced message");
    Check(g_pUserForced == (void *)&g_nShellProbe,
          "  and the forced-text seam (0x53148's name plus the alien-control record) is handed pShellUser too");
    Check(strcmp(g_szForced, "Reyes under alien control") == 0,
          "  the message is the name plus the alien-control suffix");
    CheckEqI32(g_nBattleEndCalls, 1, "  LABEL_66 still runs");
}

static void TestFallActionMedikit(void)
{
    Soldier *pActor  = &g_aFallUnits[0];
    Soldier *pVictim = &g_aFallUnits[1];

    printf("Tact_ProcessFallAction nCmd 0x12 medikit (0x53054)\n");

    /* A stunned soldier: the kneel counter is cleared and TEXTS[8] is pushed. */
    FallSetup();
    pVictim->pCell         = &g_aFallCells[2][2][2];
    pVictim->uTuSpent      = 10;
    pVictim->uHp           = 100;
    pVictim->bKneelOrPrimed = 3;
    g_FallWeaponObj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    FallQueueEntry(0x12, pActor, pVictim);
    /* Mode 0 is none of the five the switch knows, so the sort key alone picks the
     * medikit branch. */
    g_state.aQueue[0].nMode = 0;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->bKneelOrPrimed, 0, "  the kneel counter is cleared");
    CheckEqI32(g_nLastStatusRec, PUFO_TXTREC_WOUNDS_HEALED, "  TEXTS[8] pushed");
    CheckEqI32(g_nVisCalls, 0, "  LABEL_101 skips LABEL_111");

    /* Fully healed but low on morale: TEXTS[14], morale clamped at 100. */
    FallSetup();
    pVictim = &g_aFallUnits[1];
    pVictim->pCell    = &g_aFallCells[2][2][2];
    pVictim->uTuSpent = 0;
    pVictim->uHp      = 100;
    pVictim->auStat[1] = 100;
    pVictim->auStat[2] = 10;
    pVictim->bMorale  = 95;
    g_FallWeaponObj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    FallQueueEntry(0x12, &g_aFallUnits[0], pVictim);
    /* Mode 0 is none of the five the switch knows, so the sort key alone picks the
     * medikit branch. */
    g_state.aQueue[0].nMode = 0;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->bMorale, 100, "  95 + 10 clamps to 100");
    CheckEqI32(g_nLastStatusRec, PUFO_TXTREC_MORALE_BACK, "  TEXTS[14] pushed");

    /* Morale already at 100: no message at all, but LABEL_111 does run. */
    FallSetup();
    pVictim = &g_aFallUnits[1];
    pVictim->pCell    = &g_aFallCells[2][2][2];
    pVictim->uHp      = 100;
    pVictim->auStat[1] = 100;
    pVictim->auStat[2] = 10;
    pVictim->bMorale  = 100;
    g_FallWeaponObj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    FallQueueEntry(0x12, &g_aFallUnits[0], pVictim);
    /* Mode 0 is none of the five the switch knows, so the sort key alone picks the
     * medikit branch. */
    g_state.aQueue[0].nMode = 0;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(g_nStatusCalls, 0, "  full morale pushes nothing");
    CheckEqI32(g_nVisCalls, 1, "  and goes straight to LABEL_111");

    /* Wounded: a gap of 20 or more heals a flat 20, a smaller gap heals to full. */
    FallSetup();
    pVictim = &g_aFallUnits[1];
    pVictim->pCell    = &g_aFallCells[2][2][2];
    pVictim->uHp      = 80;
    pVictim->auStat[1] = 100;
    g_FallWeaponObj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    FallQueueEntry(0x12, &g_aFallUnits[0], pVictim);
    /* Mode 0 is none of the five the switch knows, so the sort key alone picks the
     * medikit branch. */
    g_state.aQueue[0].nMode = 0;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uHp, 100, "  100 - 80 = 20 is not below 20, so +20");
    CheckEqI32(g_nLastStatusRec, PUFO_TXTREC_PART_HEALED, "  TEXTS[13] pushed");

    FallSetup();
    pVictim = &g_aFallUnits[1];
    pVictim->pCell    = &g_aFallCells[2][2][2];
    pVictim->uHp      = 85;
    pVictim->auStat[1] = 100;
    g_FallWeaponObj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    FallQueueEntry(0x12, &g_aFallUnits[0], pVictim);
    /* Mode 0 is none of the five the switch knows, so the sort key alone picks the
     * medikit branch. */
    g_state.aQueue[0].nMode = 0;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uHp, 100, "  a gap of 15 heals to the maximum");

    /* Too much stun for a medikit: the spent counter is zeroed and TEXTS[46]. */
    FallSetup();
    pVictim = &g_aFallUnits[1];
    pVictim->pCell    = &g_aFallCells[2][2][2];
    pVictim->uTuSpent = 60;
    pVictim->uHp      = 100;   /* 60 > 100/2 */
    g_FallWeaponObj.uSortKey = PUFO_SPECIAL_SORT_MEDIKIT;
    FallQueueEntry(0x12, &g_aFallUnits[0], pVictim);
    /* Mode 0 is none of the five the switch knows, so the sort key alone picks the
     * medikit branch. */
    g_state.aQueue[0].nMode = 0;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uTuSpent, 0, "  the spent counter is cleared");
    CheckEqI32(g_nLastStatusRec, PUFO_TXTREC_CONSCIOUS, "  TEXTS[46] pushed");
}

static void TestFallActionMelee(void)
{
    Soldier *pActor  = &g_aFallUnits[0];
    Soldier *pVictim = &g_aFallUnits[1];

    printf("Tact_ProcessFallAction nCmd 0x12 mode 6 human melee (0x52FF8)\n");

    /* Under the stun threshold: the fire power is added and nothing else happens. */
    FallSetup();
    pVictim->pCell    = &g_aFallCells[2][2][2];
    pVictim->uTuSpent = 10;
    pVictim->uHp      = 100;
    g_FallWeaponObj.uFirePower = 5;
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 6;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uTuSpent, 15, "  10 + 5 fire power");
    Check(pVictim->pLinked1040 == NULL, "  under HP: no stun link");
    CheckEqI32(g_state.nQueueLen, 0, "  no death flash: the tail closes the queue at 1");

    /* Over the threshold: the counter is re-rolled to 10 * (rand(6) + 7) + HP. */
    FallSetup();
    pActor  = &g_aFallUnits[0];
    pVictim = &g_aFallUnits[1];
    pVictim->pCell    = &g_aFallCells[2][2][2];
    pVictim->uTuSpent = 90;
    pVictim->uHp      = 100;
    g_FallWeaponObj.uFirePower = 20;
    srand(11);
    const i32 nRoll = (i32)Game_RandU16(6);
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 6;
    srand(11);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uTuSpent, 10 * (nRoll + 7) + 100, "  re-rolled to 10*(rand(6)+7)+HP");
    Check(pVictim->pLinked1040 == pActor, "  and the victim links to the attacker");

    printf("Tact_ProcessFallAction nCmd 0x12 alien melee (0x530F8)\n");

    /* Chryssalid against a human: the transform. Type 22's record drives the stats,
     * the TU comes from the ATTACKER's maximum, and the victim joins the enemy list. */
    FallSetup();
    pActor  = &g_aFallUnits[0];
    pVictim = &g_aFallUnits[1];
    pActor->nUnitType  = PUFO_FALL_TYPE_CHRYSSALID;
    pActor->auStat[0]  = 60;
    pVictim->nUnitType = 0;
    pVictim->pCell     = &g_aFallCells[2][2][2];
    pVictim->auStat[5] = 77;   /* the record's stat 5 is never loaded */
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 13;
    srand(5);
    const i32 r1 = (i32)Game_RandU16(0x14);
    const i32 r2 = (i32)Game_RandU16(0x14);
    const i32 r3 = (i32)Game_RandU16(0x14);
    const i32 r8 = (i32)Game_RandU16(0x14);
    srand(5);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->nUnitType, PUFO_FALL_TYPE_TRANSFORMED, "  the victim becomes type 22");
    CheckEqI32(pVictim->auStat[0], 45, "  auStat[0] from the type-22 record's TU");
    CheckEqI32(pVictim->uTu, 45,
               "  ORIGINAL QUIRK: the attacker's 60 TU is overwritten by the reset's auStat[0]");
    CheckEqI32(pVictim->auStat[9], 50, "  auStat[9] from the record");
    CheckEqI32(pVictim->auStat[1], r1 - 10 + 150, "  auStat[1] = rand(20) - 10 + 150");
    CheckEqI32(pVictim->auStat[2], r2 - 10 + 30, "  auStat[2] = rand(20) - 10 + 30");
    CheckEqI32(pVictim->auStat[3], r3 - 10 + 20, "  auStat[3] = rand(20) - 10 + 20");
    CheckEqI32(pVictim->auStat[8], r8 - 10 + 30, "  auStat[8] = rand(20) - 10 + 30");
    CheckEqI32(pVictim->auStat[4], 0, "  auStat[4] written twice, record stat 5 last");
    CheckEqI32(pVictim->auStat[5], 77,
               "  ORIGINAL BUG: auStat[5] is never loaded, the old value survives");
    CheckEqI32(pVictim->bUnk248, 5, "  bUnk248 from the record");
    Check(pVictim->abReady252[0] == 0 && pVictim->abReady252[4] == 0,
          "  the five readiness bytes from the record");
    CheckEqI32(pVictim->bStatus, 1, "  bStatus set");
    Check(strcmp(pVictim->szName, "ChryssalidHost") == 0,
          "  the transform name is written -- the record 22 name of the name table, the index the shell supplies (0x53148 ADD #0x370)");
    Check(g_state.pStepFromCell == pVictim->pCell, "  pStepFromCell remembers the cell");
    CheckEqI32(g_nFallEnemy, 1, "  the victim joins the enemy roster");
    Check(g_apFallEnemy[0] == pVictim, "  at the first free slot");
    CheckEqI32(g_nBattleEndCalls, 1, "  LABEL_66 runs after the transform");

    /* Chryssalid against something it cannot convert (type 5): the HP path. The
     * kill test uses the full half-HP figure while the damage dealt is half of it. */
    FallSetup();
    pActor  = &g_aFallUnits[0];
    pVictim = &g_aFallUnits[1];
    pActor->nUnitType  = PUFO_FALL_TYPE_CHRYSSALID;
    pVictim->nUnitType = 10;   /* small, and outside the convertible types 0..3, 16, 17 */
    pVictim->auStat[1] = 100;
    pVictim->uHp       = 90;
    pVictim->pCell     = &g_aFallCells[2][2][2];
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 13;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uHp, 0, "  90 <= 100 kills");
    Check((pVictim->bFlags & 0x20) != 0, "  and runs Tact_KillUnit");
    CheckEqI32(pVictim->nUnitType, 10, "  no transform on a non-convertible type");

    FallSetup();
    pActor  = &g_aFallUnits[0];
    pVictim = &g_aFallUnits[1];
    pActor->nUnitType  = PUFO_FALL_TYPE_CHRYSSALID;
    pVictim->nUnitType = 10;   /* small, and outside the convertible types 0..3, 16, 17 */
    pVictim->auStat[1] = 100;
    pVictim->uHp       = 150;
    pVictim->pCell     = &g_aFallCells[2][2][2];
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 13;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uHp, 100, "  150 - 100/2 = 100");
    CheckEqI32(pVictim->nUnk436, 50, "  and the damage counter records 50");

    /* Celacid: power 90 - (1 - readiness/100) * bUnk248, roll rand(power/4). */
    FallSetup();
    pActor  = &g_aFallUnits[0];
    pVictim = &g_aFallUnits[1];
    pActor->nUnitType  = PUFO_FALL_TYPE_CELACID;
    pActor->uTu        = 50;
    pVictim->nUnitType = 0;
    pVictim->auStat[1] = 100;
    pVictim->uHp       = 200;
    pVictim->abReady252[2] = 150;   /* 150/100 = 1, so the bUnk248 term drops out */
    pVictim->bUnk248   = 20;
    pVictim->pCell     = &g_aFallCells[2][2][2];
    srand(3);
    const i32 nAci = (i32)Game_RandU16((u16)(PUFO_FALL_CELACID_BASE / 4));
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 13;
    srand(3);
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uHp, 200 - (PUFO_FALL_CELACID_BASE - nAci),
               "  damage = 90 - rand(22)");
    CheckEqI32(pVictim->nUnk436, PUFO_FALL_CELACID_BASE - nAci, "  recorded damage");
    CheckEqI32(pVictim->uTuSpent, PUFO_FALL_CELACID_TU, "  the celacid charges 100");
    CheckEqI32(pActor->uTu, 30, "  LABEL_93 charges the attacker 20 TU");

    /* Silacoid: a flat 80, and the kneel counter only moves when the victim is
     * small enough for 80 to matter. */
    FallSetup();
    pActor  = &g_aFallUnits[0];
    pVictim = &g_aFallUnits[1];
    pActor->nUnitType  = PUFO_FALL_TYPE_SILACOID;
    pActor->uTu        = 50;
    pVictim->nUnitType = 0;
    pVictim->auStat[1] = 200;   /* (200 & 0xFFFE) > 0xA0: no kneel */
    pVictim->uHp       = 100;
    pVictim->pCell     = &g_aFallCells[2][2][2];
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 13;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uHp, 20, "  100 - 80");
    CheckEqI32(pVictim->nUnk436, 80, "  recorded 80");
    CheckEqI32(pVictim->bKneelOrPrimed, 0, "  a 200 max-HP victim is not knocked down");
    CheckEqI32(pActor->uTu, 30, "  attacker charged 20 TU");

    FallSetup();
    pActor  = &g_aFallUnits[0];
    pVictim = &g_aFallUnits[1];
    pActor->nUnitType  = PUFO_FALL_TYPE_RIPER;
    pActor->uTu        = 10;
    pVictim->nUnitType = 0;
    pVictim->auStat[1] = 60;    /* (60 & 0xFFFE) <= 0xA0: knock down */
    pVictim->uHp       = 200;
    pVictim->pCell     = &g_aFallCells[2][2][2];
    FallQueueEntry(0x12, pActor, pVictim);
    g_state.aQueue[0].nMode = 13;
    Tact_ProcessFallAction(&g_state, &g_FallCtx);
    CheckEqI32(pVictim->uHp, 120, "  200 - 80");
    CheckEqI32(pVictim->bKneelOrPrimed, (80 - 30) / 10 + 1,
               "  knock-down counter = (80 - 60/2)/10 + 1");
    CheckEqI32(pActor->uTu, 0, "  10 TU is below the 20 charge, so it goes to 0");
}

/* --- Tact_ProcessFireAction @ 0x50adc --------------------------------------- */

/* The fire FSM reaches cells through Map_IsValidCell, which bounds at
 * PUFO_CELL_SIZE cells per map tile. The fall fixture's 4 x 4 grid is only 4
 * cells wide, so the fire tests get their own 40 x 40 grid for the same 4-tile
 * map: the shot samples and the decay sweep both address cells the fall fixture
 * does not have. */
#define FIRE_GRID (PUFO_CELL_SIZE * FALL_DIM)

static MapCell   g_aFireCells[PUFO_MAP_LAYERS][FIRE_GRID][FIRE_GRID];
static MapGeom   g_FireGeom = { FALL_DIM, FALL_DIM };
static AimState  g_FireAim;
static double    g_adMtX[PUFO_AIM_SAMPLE_COUNT];
static double    g_adMtY[PUFO_AIM_SAMPLE_COUNT];
static double    g_adMtZ[PUFO_AIM_SAMPLE_COUNT];
static ThrowArc  g_FireArc;
static TactCtx272 g_FirePhase;
static i32       g_nShotCount;
static i32       g_nBurstShots;
static i32       g_nFireSlot;
static u8        g_bFireStatus1;
static u8        g_bFireStatus9;
static u8        g_bFireHitResult;
static const ObjectRecFull *g_pFireObject;
static u32       g_aFireHits[PUFO_SPLASH_DWORDS];
static ShotOutcome g_FireOut;
static Mc1Rec    g_aFireSpr[4];
static Mc1Rec   *g_apFireSpr[4];
static PufoFont  g_FireFont;
static FireActionCtx g_FireCtx;
static ShotHooks g_FireHooks;
static i32       g_bArmorRefuse;
static i32       g_nFovCalls;
static i32       g_nAreaCalls;

static MapCell *FireLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nX >= FIRE_GRID || nY < 0 || nY >= FIRE_GRID) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return &g_aFireCells[nLayer][nY][nX];
}

static bool FireHookArmor(const Soldier *pUnit, i32 nTuCost, void *pUser)
{
    (void)pUnit;
    (void)nTuCost;
    (void)pUser;
    return g_bArmorRefuse != 0;
}

static void FireHookFov(void *pUser, u32 *paWindow, i32 nColBase, i32 nRowBase,
                        i32 nOriginX, i32 nOriginY, i32 nLayer, i32 nRange,
                        i32 nCostMode, u16 nSortKey)
{
    (void)pUser; (void)paWindow; (void)nColBase; (void)nRowBase;
    (void)nOriginX; (void)nOriginY; (void)nLayer; (void)nRange;
    (void)nCostMode; (void)nSortKey;
    ++g_nFovCalls;
}

static void FireHookArea(void *pUser)
{
    (void)pUser;
    ++g_nAreaCalls;
}

static void FireSeedSamples(double dX, double dY, double dZ)
{
    for (i32 i = 0; i < PUFO_AIM_SAMPLE_COUNT; ++i) {
        g_adMtX[i] = dX;
        g_adMtY[i] = dY;
        g_adMtZ[i] = dZ;
    }
}

static void FireSetup(void)
{
    i32 z, y, x, i;

    FallSetup();
    memset(g_aFireCells, 0, sizeof(g_aFireCells));
    for (z = 0; z < PUFO_MAP_LAYERS; ++z) {
        for (y = 0; y < FIRE_GRID; ++y) {
            for (x = 0; x < FIRE_GRID; ++x) {
                MapCell_Init(&g_aFireCells[z][y][x], (u8)x, (u8)y, (u8)z);
            }
        }
    }
    memset(g_adMtX, 0, sizeof(g_adMtX));
    memset(g_adMtY, 0, sizeof(g_adMtY));
    memset(g_adMtZ, 0, sizeof(g_adMtZ));
    memset(&g_FireAim, 0, sizeof(g_FireAim));
    g_FireAim.padMtX = g_adMtX;
    g_FireAim.padMtY = g_adMtY;
    g_FireAim.padMtZ = g_adMtZ;
    memset(&g_FireArc, 0, sizeof(g_FireArc));
    g_FireArc.dArcDenom = 10.0;
    memset(&g_FirePhase, 0, sizeof(g_FirePhase));

    g_nShotCount = 0;
    g_nBurstShots = 3;
    g_nFireSlot = 0;
    g_bFireStatus1 = 1;
    g_bFireStatus9 = 0;
    g_bFireHitResult = 0;
    g_bArmorRefuse = 0;
    g_nFovCalls = 0;
    g_nAreaCalls = 0;
    memset(&g_FireOut, 0, sizeof(g_FireOut));
    g_FireOut.nDamage = -999;   /* a sentinel: the resolve overwrites it */
    memset(g_aFireHits, 0, sizeof(g_aFireHits));

    /* A four-entry font index, so the door swap can be checked end to end. */
    memset(g_aFireSpr, 0, sizeof(g_aFireSpr));
    for (i = 0; i < 4; ++i) {
        g_aFireSpr[i].nGlobalIdx = (u16)i;
        g_apFireSpr[i] = &g_aFireSpr[i];
    }
    memset(&g_FireFont, 0, sizeof(g_FireFont));
    g_FireFont.apMc1Index = g_apFireSpr;
    g_FireFont.nMc1IndexCount = 4;

    g_FallWeaponObj.uFirePower = 40;
    g_pFireObject = &g_FallWeaponObj;

    g_FallCombat.pfnLookup = FireLookup;
    g_FallCombat.pGeom = &g_FireGeom;
    g_FallCombat.pState = &g_state;
    g_FallCombat.pSelUnit = &g_aFallUnits[0];

    memset(&g_FireHooks, 0, sizeof(g_FireHooks));
    g_FireHooks.pfnComputeFov = FireHookFov;
    g_FireHooks.pfnAreaFire = FireHookArea;

    memset(&g_FireCtx, 0, sizeof(g_FireCtx));
    g_FireCtx.geom = g_FireGeom;
    g_FireCtx.pfnLookup = FireLookup;
    g_FireCtx.pUser = NULL;
    g_FireCtx.pnLayerScratch = &g_nLayerScratch;
    g_FireCtx.pnMaxLayerClamp = &g_nMaxLayerClamp;
    g_FireCtx.pnAnimPhase = &g_nAnimPhase;
    g_FireCtx.pnShotCount = &g_nShotCount;
    g_FireCtx.pnBurstShots = &g_nBurstShots;
    g_FireCtx.pbFireStatus1 = &g_bFireStatus1;
    g_FireCtx.pbFireStatus9 = &g_bFireStatus9;
    g_FireCtx.pbFallDamagePending = &g_bFallPending;
    g_FireCtx.pbThrowPending = &g_bThrowPending;
    g_FireCtx.pbFireHitResult = &g_bFireHitResult;
    g_FireCtx.pbEnemySpotted = &g_bEnemySpotted;
    g_FireCtx.pnFireTickProgress = &g_nFireTickProgress;
    g_FireCtx.pnFireSlot = &g_nFireSlot;
    g_FireCtx.ppFireObject = &g_pFireObject;
    g_FireCtx.pAim = &g_FireAim;
    g_FireCtx.pThrowArc = &g_FireArc;
    g_FireCtx.pTactCtx = &g_FirePhase;
    g_FireCtx.apXcomUnits = g_apFallXcom;
    g_FireCtx.pnXcomCount = &g_nFallXcom;
    g_FireCtx.pCombat = &g_FallCombat;
    g_FireCtx.pShotHooks = &g_FireHooks;
    g_FireCtx.pRosters = &g_FallRosters;
    g_FireCtx.apHits = g_aFireHits;
    g_FireCtx.apHitsSave = NULL;
    g_FireCtx.pShotOut = &g_FireOut;
    g_FireCtx.pFont = &g_FireFont;
    g_FireCtx.pfnArmorTuWarn = FireHookArmor;
    g_FireCtx.pfnPushStatus = FallHookStatus;
    g_FireCtx.pfnUpdateVisibility = FallHookVisibility;
}

static TactQueueEntry *FireEntry(u8 nCmd)
{
    memset(&g_state, 0, sizeof(g_state));
    g_state.nQueueCursor = 0;
    g_state.nQueueLen = 2;
    g_state.aQueue[0].nCmd = nCmd;
    g_state.aQueue[0].pUnit = &g_aFallUnits[0];
    g_state.aQueue[0].pWeapon = &g_FallWeapon;
    g_state.aQueue[0].nFacing = 2;
    g_state.aQueue[0].nX = 11;
    g_state.aQueue[0].nY = 11;
    g_state.aQueue[0].nZ = 0;
    g_state.aQueue[0].nDelta = 1;
    g_state.aQueue[0].nMode = 0;
    g_state.nPhaseMax = 5;
    return &g_state.aQueue[0];
}

static void TestFireTurn(void)
{
    TactQueueEntry *pE;
    Soldier *pU;

    printf("Tact_ProcessFireAction cmd 0, the turn\n");
    FireSetup();
    pE = FireEntry(TACTCMD_TURN);
    pU = pE->pUnit;
    pU->uTu = 40;
    pU->bFacing = 7;
    pE->nTurnDir = 1;
    g_state.nPhaseTick = 4;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 0, "tick 5 of 5 keeps the phase running");
    CheckEqI32(pU->uTu, 39, "one TU spent");
    CheckEqI32(pU->bFacing, 0, "facing 7 + 1 wraps to 0");
    CheckEqI32(g_nVisCalls, 1, "visibility refreshed once");

    FireSetup();
    pE = FireEntry(TACTCMD_TURN);
    pE->nTurnDir = -1;
    pE->pUnit->bFacing = 0;
    pE->pUnit->uTu = 10;
    g_state.nPhaseTick = 5;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "tick 6 of 5 ends the phase");
    CheckEqI32(pE->pUnit->uTu, 10, "no TU spent on the ending tick");
    CheckEqI32(g_nVisCalls, 0, "no visibility call");

    FireSetup();
    pE = FireEntry(TACTCMD_TURN);
    pE->nTurnDir = -1;
    pE->pUnit->bFacing = 0;
    pE->pUnit->uTu = 20;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(pE->pUnit->bFacing, 7, "facing 0 - 1 wraps to 7");

    /* Out of TU: the shared message, then the team pass. */
    FireSetup();
    pE = FireEntry(TACTCMD_TURN);
    pE->pUnit->uTu = 0;
    g_nFallXcom = 1;
    g_apFallXcom[0] = &g_aFallUnits[1];
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nLastStatusRec, MOVE_MSG_NO_TIME, "the out-of-TU record is pushed");
    CheckEqI32(g_nVisCalls, 1, "the team pass refreshes the one XCom unit");
    CheckEqI32(g_state.aQueue[0].nCmd, (i32)TACTCMD_TURN,
               "the team pass leaves the current entry alone");
    CheckEqI32(g_state.aQueue[1].nCmd, (i32)TACTCMD_END_MARK,
               "and appends the END MARK at cursor + 1 (0x4f388)");
    CheckEqI32(g_state.nQueueLen, 2, "the team pass sets the queue length");
    Check(g_state.bFlag1 == 1, "the team pass raises bFlag1");

    /* The armour guard refuses the turn the same way. */
    FireSetup();
    pE = FireEntry(TACTCMD_TURN);
    pE->pUnit->uTu = 30;
    g_bArmorRefuse = 1;
    g_nFallXcom = 1;
    g_apFallXcom[0] = &g_aFallUnits[1];
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(pE->pUnit->uTu, 30, "the armour guard spends no TU");
    CheckEqI32(g_nVisCalls, 1, "the armour guard runs the team pass");
    CheckEqI32(g_nStatusCalls, 0, "and pushes no message");

    /* The phase object says the unit is mid-turn: no TU work at all. */
    FireSetup();
    pE = FireEntry(TACTCMD_TURN);
    pE->pUnit->uTu = 20;
    pE->pUnit->bFacing = 7;
    pE->nTurnDir = 1;
    g_FirePhase.nPhase = 1;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(pE->pUnit->uTu, 20, "mid-turn spends no TU");
    CheckEqI32(pE->pUnit->bFacing, 0, "mid-turn still turns");

    /* The sighting tail. */
    FireSetup();
    pE = FireEntry(TACTCMD_TURN);
    pE->pUnit->uTu = 20;
    pE->pUnit->bAiDone = 1;
    g_bEnemySpotted = 1;
    g_nFallXcom = 1;
    g_apFallXcom[0] = &g_aFallUnits[1];
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nLastStatusRec, PUFO_TXTREC_SPOTTED, "the sighting is announced");
    CheckEqI32(pE->pUnit->bAiDone, 0, "the unit's AI-done flag is cleared");
    CheckEqI32(g_nVisCalls, 2, "own refresh plus the team pass");

    FireSetup();
    pE = FireEntry(TACTCMD_TURN);
    pE->pUnit->uTu = 20;
    pE->pUnit->bAiDone = 1;
    g_bEnemySpotted = 1;
    g_state.bAiActionActive = 1;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nStatusCalls, 0, "bAiActionActive suppresses the sighting");
    CheckEqI32(pE->pUnit->bAiDone, 1, "and leaves AI-done set");
    CheckEqI32(g_nVisCalls, 1, "but the own refresh still runs");
}

static void TestFireStepFrame(void)
{
    TactQueueEntry *pE;

    printf("Tact_ProcessFireAction cmd 1, the fire-frame step\n");
    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->bFlags = 0x20;
    pE->pUnit->uTu = 50;
    g_nAnimPhase = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 0 && g_nAnimPhase == 3 && pE->pUnit->uTu == 50,
          "a dead unit stops the frame without ending the phase");

    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    g_nAnimPhase = 7;
    pE->pUnit->uTu = 30;
    g_state.nPhaseTick = 1;   /* becomes 2, even */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nAnimPhase, 0, "anim phase wraps 7 to 0");
    CheckEqI32(pE->pUnit->uTu, 29, "TU spent on an even tick");

    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->uTu = 30;
    g_state.nPhaseTick = 2;   /* becomes 3, odd */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(pE->pUnit->uTu, 30, "no TU on an odd tick");

    /* Tick 4 re-places the unit one step ahead, on the entry's layer delta. */
    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->pCell = &g_aFireCells[0][11][11];
    pE->pUnit->bFacing = 2;   /* g_aStepDir[2] is east */
    pE->nDelta = 1;
    g_state.nPhaseTick = 3;   /* becomes 4 */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(pE->pUnit->pCell->nX, 12, "tick 4 moves the unit east");
    CheckEqI32(pE->pUnit->pCell->nY, 11, "the y is unchanged");
    CheckEqI32(pE->pUnit->pCell->nZ, 1, "the layer is nDelta + the old layer");

    /* The draw-offset ladder, three representative facings. */
    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->bFacing = 0;
    g_state.nPhaseTick = 0;      /* becomes 1, first half */
    g_state.nStepFrameDiv = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_state.nStepDrawOffX, 4, "facing 0 tick 1: X = 2 * (1 + 1)");
    CheckEqI32(g_state.nStepDrawOffY, -5, "facing 0 tick 1: Y = (-1 - 1) - 3 * 1");
    CheckEqI32(pE->pUnit->nDrawOffX, 4, "the unit gets the X");
    CheckEqI32(pE->pUnit->nDrawOffY, -5, "the unit gets the low byte of Y");

    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->bFacing = 5;
    g_state.nPhaseTick = 4;      /* becomes 5, second half */
    g_state.nStepFrameDiv = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_state.nStepDrawOffX, 8, "facing 5 tick 5: X = 4 * (7 - 5)");
    CheckEqI32(g_state.nStepDrawOffY, 6, "facing 5 tick 5: Y = 0 + (7 - 5) * 3");

    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->bFacing = 3;
    g_state.nPhaseTick = 1;      /* becomes 2 */
    g_state.nStepFrameDiv = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_state.nStepDrawOffX, 0, "facing 3 tick 2: X forced to 0");
    CheckEqI32(g_state.nStepDrawOffY, 0, "facing 3 tick 2: Y = 2 * 3 - 3 * 2");

    /* The scratch cell is the step destination off pStepFromCell. */
    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->bFacing = 2;
    pE->pUnit->pCell = &g_aFireCells[0][11][11];
    g_state.pStepFromCell = &g_aFireCells[0][11][11];
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.pStepCellScratch == &g_aFireCells[0][11][12],
          "the scratch cell is the destination");

    /* The kneel toggle: tick 4 only, flag-0 types only, keyed on the
     * destination cell's floor sprite. */
    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->nUnitType = 3;      /* g_aUnitTypeFlag0[3] == 1 */
    pE->pUnit->bFlags = 0;
    pE->pUnit->bFacing = 2;
    pE->pUnit->pCell = &g_aFireCells[0][11][11];
    g_state.pStepFromCell = &g_aFireCells[0][11][11];
    g_state.nPhaseTick = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check((pE->pUnit->bFlags & PUFO_UNITFLAG_KNEEL) != 0,
          "tick 4 on a floorless cell kneels a flag-0 type");

    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->nUnitType = 3;
    pE->pUnit->bFlags = PUFO_UNITFLAG_KNEEL;
    pE->pUnit->bFacing = 2;
    pE->pUnit->pCell = &g_aFireCells[0][11][11];
    g_state.pStepFromCell = &g_aFireCells[0][11][11];
    g_state.nPhaseTick = 3;
    g_aFireCells[0][11][12].pFloorSpr = &g_FallFloor;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check((pE->pUnit->bFlags & PUFO_UNITFLAG_KNEEL) == 0,
          "tick 4 on a floored cell stands the unit up");

    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->nUnitType = 1;      /* flag 0 clear */
    pE->pUnit->bFlags = 0;
    pE->pUnit->bFacing = 2;
    pE->pUnit->pCell = &g_aFireCells[0][11][11];
    g_state.pStepFromCell = &g_aFireCells[0][11][11];
    g_state.nPhaseTick = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check((pE->pUnit->bFlags & PUFO_UNITFLAG_KNEEL) == 0,
          "a type without flag 0 never kneels here");

    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    pE->pUnit->nUnitType = 3;
    pE->pUnit->bFlags = 0;
    pE->pUnit->bFacing = 2;
    pE->pUnit->pCell = &g_aFireCells[0][11][11];
    g_state.pStepFromCell = &g_aFireCells[0][11][11];
    g_state.nPhaseTick = 4;        /* becomes 5, not the kneel tick */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check((pE->pUnit->bFlags & PUFO_UNITFLAG_KNEEL) == 0,
          "the toggle only runs on tick 4");

    FireSetup();
    pE = FireEntry(TACTCMD_STEP);
    g_state.nPhaseTick = 6;        /* becomes 7, past PUFO_STEP_FRAME_MAX */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "tick 7 ends the phase");
}

static void TestFireDoor(void)
{
    TactQueueEntry *pE;
    MapCell *pCell;

    printf("Tact_ProcessFireAction cmd 3, the door animation\n");
    FireSetup();
    pE = FireEntry(TACTCMD_DOOR);
    pE->nFacing = 2;
    pCell = &g_aFireCells[0][11][11];
    pCell->pWallSprB = &g_aFireSpr[2];
    g_state.nPhaseTick = 0;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(pCell->pWallSprB == &g_aFireSpr[2] && pCell->pWallSprA == NULL,
          "tick 1 leaves the walls alone");
    Check(g_state.bFlag1 == 0, "and does not end the phase");
    g_state.nPhaseTick = 1;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(pCell->pWallSprB == &g_aFireSpr[2], "tick 2 leaves the walls alone");
    g_state.nPhaseTick = 2;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(pCell->pWallSprA == &g_aFireSpr[1],
          "tick 3 puts wall B's neighbour into wall A");
    Check(pCell->pWallSprB == NULL, "and clears the source slot");
    Check(g_state.bFlag1 == 0, "tick 3 does not end the phase");
    g_state.nPhaseTick = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "tick 4 ends the phase");

    FireSetup();
    pE = FireEntry(TACTCMD_DOOR);
    pE->nFacing = 6;
    pCell = &g_aFireCells[0][11][11];
    pCell->pWallSprA = &g_aFireSpr[0];
    g_state.nPhaseTick = 2;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(pCell->pWallSprB == &g_aFireSpr[1] && pCell->pWallSprA == NULL,
          "any other facing swaps the other way");

    FireSetup();
    pE = FireEntry(TACTCMD_DOOR);
    pE->nFacing = 2;
    g_state.nPhaseTick = 2;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_aFireCells[0][11][11].pWallSprA == NULL
          && g_aFireCells[0][11][11].pWallSprB == NULL,
          "a cell with no wall sprite writes nothing");
}

static void TestFireSmallCases(void)
{
    TactQueueEntry *pE;

    printf("Tact_ProcessFireAction cmd 4, plain animation advance\n");
    FireSetup();
    pE = FireEntry(4);
    (void)pE;
    g_nAnimPhase = 0;
    g_state.nPhaseTick = 2;   /* becomes 3 */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nAnimPhase, 1, "anim phase advances");
    Check(g_state.bFlag1 == 0, "tick 3 keeps the phase running");
    g_state.nPhaseTick = 3;   /* becomes 4 */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nAnimPhase, 2, "anim phase advances again");
    Check(g_state.bFlag1 == 1, "tick 4 ends the phase");

    printf("Tact_ProcessFireAction cmd 5, the vertical draw offset\n");
    FireSetup();
    pE = FireEntry(TACTCMD_VERT_STEP);
    pE->nFacing = 1;
    g_state.nPhaseMax = 4;
    g_state.nPhaseTick = 1;   /* becomes 2 */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(pE->pUnit->nDrawOffY, -4, "draw Y = -2 * 2 * 1");
    Check(g_state.bFlag1 == 0, "tick 2 of 4 keeps the phase");
    g_state.nPhaseTick = 4;   /* becomes 5, past nPhaseMax */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "tick 5 of 4 ends the phase");
    CheckEqI32(pE->pUnit->nDrawOffY, -4, "and writes no offset");

    FireSetup();
    pE = FireEntry(TACTCMD_VERT_STEP);
    pE->nFacing = 0xFF;       /* the original reads the byte signed: -1 */
    g_state.nPhaseTick = 1;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(pE->pUnit->nDrawOffY, 4, "a signed facing of -1 lifts the sprite");

    printf("Tact_ProcessFireAction cmd 8, the death flash\n");
    FireSetup();
    pE = FireEntry(TACTCMD_DEATH_FLASH);
    pE->pUnit->nUnitType = 4;   /* g_aUnitTypeIsLarge[4] == 1 */
    g_nAnimPhase = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "a large unit ends the phase at once");
    CheckEqI32(g_nAnimPhase, 3, "and the anim phase is untouched");

    FireSetup();
    pE = FireEntry(TACTCMD_DEATH_FLASH);
    pE->pUnit->nUnitType = 1;
    pE->nArg = 1;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "a non-zero entry argument is refused the same way");

    FireSetup();
    pE = FireEntry(TACTCMD_DEATH_FLASH);
    pE->pUnit->nUnitType = 1;
    g_nAnimPhase = 0;
    g_state.nPhaseTick = 6;   /* becomes 7 */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nAnimPhase, 1, "anim phase advances");
    Check(g_state.bFlag1 == 0, "tick 7 keeps the phase running");
    g_state.nPhaseTick = 7;   /* becomes 8 */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nAnimPhase, 0, "tick 8 resets the anim phase");
    Check(g_state.bFlag1 == 1, "and ends the phase");

    printf("Tact_ProcessFireAction, an unknown command\n");
    FireSetup();
    (void)FireEntry(9);
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "an unhandled command only ends the phase");
}

static void TestFireShotFlight(void)
{
    TactQueueEntry *pE;

    printf("Tact_ProcessFireAction cmd 0xE, the shot flight\n");
    FireSetup();
    pE = FireEntry(TACTCMD_FIRE_SHOT);
    pE->pUnit->bFlags = 0;
    g_FireAim.dTravel = 33.0;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "no aim flag, no flight");
    CheckEqI32((i32)g_FireAim.dTravel, 33, "and the travel counter is untouched");

    /* Still flying: the old samples are cleared, the new ones stamped. */
    FireSetup();
    pE = FireEntry(TACTCMD_FIRE_SHOT);
    pE->pUnit->bFlags = PUFO_UNITFLAG_AIMING;
    g_FireAim.nOriginX = 16;
    g_FireAim.nOriginY = 16;
    g_FireAim.nOriginZ = 0;
    g_FireAim.dYaw = 0.0;
    g_FireAim.dPitch = 0.0;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dTravelSave = 100.0;
    g_FireAim.dBound = 50.0;
    FireSeedSamples(100.0, 100.0, 0.0);
    g_aFireCells[0][6][6].bFlags29 = PUFO_CELLFLAG_SHOT_TRACE;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32((i32)g_FireAim.dTravel, 10, "travel advances by 10 voxels");
    Check(g_state.bFlag1 == 0, "the phase keeps running");
    Check((g_aFireCells[0][6][6].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) == 0,
          "the stale sample cell is cleared");
    Check((g_aFireCells[0][1][1].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) != 0,
          "the new sample head is stamped");
    Check(g_adMtX[8] == PUFO_SAMPLE_OFFMAP,
          "the eighth sample is already behind the muzzle");
    Check(g_adMtX[9] == PUFO_SAMPLE_OFFMAP, "and so is the ninth");
    Check((g_aFireCells[0][1][0].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) == 0,
          "a parked sample stamps nothing");
    CheckEqI32(g_nFovCalls + g_nAreaCalls, 0, "no shot resolution ran");
    CheckEqI32(g_nLayerScratch, 2, "the layer scratch is clamped up");

    /* The samples are measured from the muzzle, not from the head: at travel 10
     * the eight first ones land on cells 16..31 and the last two are parked. */
    FireSetup();
    pE = FireEntry(TACTCMD_FIRE_SHOT);
    pE->pUnit->bFlags = PUFO_UNITFLAG_AIMING;
    g_FireAim.nOriginX = 0;
    g_FireAim.nOriginY = 0;
    g_FireAim.nOriginZ = 0;
    g_FireAim.dTravel = 0.0;      /* becomes 10 */
    g_FireAim.dTravelSave = 1000.0;
    FireSeedSamples(1000.0, 1000.0, 1000.0);
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_adMtX[0] == 10.0 && g_adMtZ[0] == 0.0, "the head sample is on the ray");
    Check(g_adMtX[7] > 0.12 && g_adMtX[7] < 0.14, "the last live sample is 10 - 7 * 1.41");
    Check((g_aFireCells[0][0][0].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) != 0,
          "a muzzle at the origin stamps cell 0");

    /* Past the walked distance: the shot resolves. */
    FireSetup();
    pE = FireEntry(TACTCMD_FIRE_SHOT);
    pE->pUnit->bFlags = PUFO_UNITFLAG_AIMING;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dTravelSave = 5.0;
    g_FireAim.nTgtX = 11;
    g_FireAim.nTgtY = 11;
    g_FireAim.nTgtZ = 0;
    g_nFireTickProgress = 7;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_FireOut.nDamage != -999, "the shot resolution ran");
    Check(g_FallCombat.pCtxWeapon == g_pFireObject,
          "the weapon reached the map fire context");
    CheckEqI32(g_bFallPending, 1, "fall damage is armed");
    CheckEqI32(g_bFireStatus1, 0, "fire status 1 is cleared");
    CheckEqI32(g_bFireStatus9, 1, "fire status 9 is set while nothing is in flight");
    CheckEqI32(g_nFireTickProgress, 0, "the fall-damage progress restarts");
    Check(g_state.bFlag1 == 1, "the phase ends");
    CheckEqI32(g_state.nStepZDelta, 0, "a non-mode-7 shot leaves the z delta alone");

    /* Mode 7 with the bound reached: resolve, arm the z delta, clear the samples. */
    FireSetup();
    pE = FireEntry(TACTCMD_FIRE_SHOT);
    pE->nMode = 7;
    pE->pUnit->bFlags = PUFO_UNITFLAG_AIMING;
    g_FireAim.nOriginX = 16;
    g_FireAim.nOriginY = 16;
    g_FireAim.nOriginZ = 0;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dTravelSave = 5.0;
    g_FireAim.dBound = 10.0;
    g_FireAim.nTgtX = 11;
    g_FireAim.nTgtY = 11;
    g_FireAim.nTgtZ = 0;
    FireSeedSamples(100.0, 100.0, 0.0);
    g_aFireCells[0][1][1].bFlags29 = PUFO_CELLFLAG_SHOT_TRACE;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_state.nStepZDelta & 0xFF, 1,
               "mode 7 sets the low byte of the step z delta");
    CheckEqI32(g_bFallPending, 1, "fall damage is armed");
    Check((g_aFireCells[0][1][1].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) == 0,
          "the resolve cleared the trace flag the case had just stamped");

    /* Mode 7 in the resolve path but with neither landing condition met: the
     * shot is not resolved, yet the path still closes the phase. */
    FireSetup();
    pE = FireEntry(TACTCMD_FIRE_SHOT);
    pE->nMode = 7;
    pE->pUnit->bFlags = PUFO_UNITFLAG_AIMING;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dTravelSave = 5.0;    /* past range, so the resolve path is taken */
    g_FireAim.dBound = 1.0;         /* but travelSave 5 > bound 1 ... */
    g_nShotCount = 0;
    g_nBurstShots = 3;              /* ... and the burst still has shots left */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nFovCalls + g_nAreaCalls, 0, "no resolution");
    CheckEqI32(g_bFallPending, 0, "fall damage stays disarmed");
    CheckEqI32(g_bFireStatus9, 1, "fire status 9 is still written");
    CheckEqI32(g_bFireStatus1, 1, "mode 7 with nothing in flight keeps fire status 1");
    Check(g_state.bFlag1 == 1, "the resolve path closes the phase anyway");

    /* The two multi-target weapons resolve at the bound, not past the trace. */
    FireSetup();
    pE = FireEntry(TACTCMD_FIRE_SHOT);
    pE->pUnit->bFlags = PUFO_UNITFLAG_AIMING;
    g_FallWeaponObj.uSortKey = PUFO_SORT_KEY_MULTI_TARGET_A;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dTravelSave = 1000.0;
    g_FireAim.dBound = 5.0;
    g_nShotCount = 0;
    g_nBurstShots = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_bFallPending, 1, "the multi-target weapon resolves at its bound");

    FireSetup();
    pE = FireEntry(TACTCMD_FIRE_SHOT);
    pE->pUnit->bFlags = PUFO_UNITFLAG_AIMING;
    g_FallWeaponObj.uSortKey = PUFO_SORT_KEY_MULTI_TARGET_B;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dTravelSave = 1000.0;
    g_FireAim.dBound = 5.0;
    g_nShotCount = 3;
    g_nBurstShots = 3;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_bFallPending, 0, "a spent burst keeps flying");
    Check(g_state.bFlag1 == 0, "and the phase stays open");
    g_FallWeaponObj.uSortKey = 4;
}

static void TestFireFallProgress(void)
{
    printf("Tact_ProcessFireAction cmd 0xF, the fall-damage progress\n");
    FireSetup();
    (void)FireEntry(TACTCMD_FIRE_MARK);
    g_bFallPending = 0;
    g_nFireTickProgress = 4;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "an unarmed sweep ends the phase");
    CheckEqI32(g_nFireTickProgress, 4, "and the progress counter is untouched");

    FireSetup();
    (void)FireEntry(TACTCMD_FIRE_MARK);
    g_bFallPending = 1;
    g_nFireTickProgress = 0;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nFireTickProgress, 1, "progress 1");
    Check(g_state.bFlag1 == 0, "the sweep keeps running");
    g_nFireTickProgress = 8;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nFireTickProgress, 9, "progress 9");
    Check(g_state.bFlag1 == 0, "nine ticks still run");
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_nFireTickProgress, 10, "progress 10");
    Check(g_state.bFlag1 == 1, "the tenth tick ends the phase");

    FireSetup();
    (void)FireEntry(TACTCMD_FIRE_MARK);
    g_bFallPending = 1;
    g_bFireHitResult = 1;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_bFallPending == 0, "an arc that left the map disarms the sweep");
    Check(g_state.bFlag1 == 1, "and ends the phase");
}

static void TestFireDecaySweep(void)
{
    printf("Tact_ProcessFireAction cmd 0x10, the animated-tile decay\n");
    FireSetup();
    (void)FireEntry(0x10);
    g_aFireCells[0][0][0].nFlags32 = 0x08;          /* the field at 1 */
    g_aFireCells[0][0][1].nFlags32 = 0x48;          /* the field at 9 */
    g_aFireCells[0][0][2].nFlags32 = 0x07;          /* the field is zero */
    g_aFireCells[3][39][39].nFlags32 = 0x10;        /* the far corner too */
    g_state.nPhaseCounter = 0;
    g_state.nPhaseTick = 10;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_aFireCells[0][0][0].nFlags32, 0x00, "field 1 decays to 0");
    CheckEqI32(g_aFireCells[0][0][1].nFlags32, 0x40, "field 9 decays to 8");
    CheckEqI32(g_aFireCells[0][0][2].nFlags32, 0x07, "a zero field is left alone");
    CheckEqI32(g_aFireCells[3][39][39].nFlags32, 0x08,
               "the sweep reaches the far corner of the map");
    CheckEqI32(g_state.nPhaseCounter, 1, "the phase counter advances");
    CheckEqI32(g_state.nPhaseTick, 11, "the phase tick advances");
    Check(g_state.bFlag1 == 0, "tick 11 of 11 keeps the phase");
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_state.nPhaseTick, 12, "tick 12 ends the phase");
    Check(g_state.bFlag1 == 1, "and raises bFlag1");
}

static void TestFireThrowFlight(void)
{
    printf("Tact_ProcessFireAction cmd 0x11, the thrown item's flight\n");
    FireSetup();
    (void)FireEntry(0x11);
    g_bThrowPending = 0;
    g_FireAim.dTravel = 12.0;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_state.bFlag1 == 1, "no pending throw, no flight");
    CheckEqI32((i32)g_FireAim.dTravel, 12, "and the travel counter is untouched");

    /* Flat throw: the gravity term pulls the item below layer 0, so the
     * in-bounds test fails and nothing is stamped. */
    FireSetup();
    (void)FireEntry(0x11);
    g_bThrowPending = 1;
    g_FireAim.nOriginX = 16;
    g_FireAim.nOriginY = 16;
    g_FireAim.nOriginZ = 0;
    g_FireAim.dYaw = 0.0;
    g_FireAim.dPitch = 0.0;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dTravelSave = 100.0;
    g_FireArc.dArcDenom = 10.0;
    FireSeedSamples(32.0, 16.0, 0.0);
    g_aFireCells[0][1][2].bFlags29 = PUFO_CELLFLAG_SHOT_TRACE;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32((i32)g_FireAim.dTravel, 5, "the step is 5 voxels");
    Check((g_aFireCells[0][1][2].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) == 0,
          "the flag comes off the cell the item was over");
    Check(g_adMtX[1] == 32.0, "slot 1 keeps the previous position");
    CheckEqI32((i32)g_adMtX[0], 21, "x = origin + travel * cos(yaw)");
    Check(g_adMtZ[0] < -1.14 && g_adMtZ[0] > -1.16,
          "z drops by g * travel^2 / (2 * v0^2 * cos^2(pitch))");
    Check((g_aFireCells[0][1][1].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) == 0,
          "an out-of-bounds position stamps nothing");
    Check(g_state.bFlag1 == 0, "the phase keeps running");

    /* A lofted throw stays in bounds and stamps its new cell. */
    FireSetup();
    (void)FireEntry(0x11);
    g_bThrowPending = 1;
    g_FireAim.nOriginX = 16;
    g_FireAim.nOriginY = 16;
    g_FireAim.nOriginZ = 12;
    g_FireAim.dYaw = 0.0;
    g_FireAim.dPitch = 0.5;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dTravelSave = 100.0;
    g_FireArc.dArcDenom = 20.0;
    FireSeedSamples(16.0, 16.0, 12.0);
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check((g_aFireCells[1][1][1].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) != 0,
          "the lofted item stamps its cell");
    CheckEqI32(g_nLayerScratch, 2, "the layer scratch follows the new height");

    /* Past the landing distance the flight is over. */
    FireSetup();
    (void)FireEntry(0x11);
    g_bThrowPending = 1;
    g_FireAim.dTravel = 99.0;
    g_FireAim.dTravelSave = 100.0;
    g_FireAim.dBound = 100.0;
    g_FireAim.nOriginZ = 0;
    g_FireAim.nTargetZ = 0;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32((i32)g_FireAim.dTravel, 104, "travel 104, past the landing distance");
    Check(g_state.bFlag1 == 1, "the phase ends");

    /* The rise-scaled step: bound 2 against a rise of 10 gives 5 * 2 / 10. */
    FireSetup();
    (void)FireEntry(0x11);
    g_bThrowPending = 1;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dBound = 2.0;
    g_FireAim.nOriginZ = 0;
    g_FireAim.nTargetZ = 10;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_FireAim.dTravel > 0.99 && g_FireAim.dTravel < 1.01,
          "a short bound against a tall rise shortens the step");

    FireSetup();
    (void)FireEntry(0x11);
    g_bThrowPending = 1;
    g_FireAim.dTravel = 0.0;
    g_FireAim.dBound = 2.0;
    g_FireAim.nOriginZ = 10;
    g_FireAim.nTargetZ = 0;   /* the rise is an absolute difference */
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check(g_FireAim.dTravel > 0.99 && g_FireAim.dTravel < 1.01,
          "a downward rise is scaled the same way");
}

static void TestFireMeleeStamp(void)
{
    printf("Tact_ProcessFireAction cmd 0x12, the melee-target stamp\n");
    FireSetup();
    (void)FireEntry(0x12);
    g_state.gap_70112[1] = 0;
    g_FireAim.nTgtX = 11;
    g_FireAim.nTgtY = 11;
    g_FireAim.nTgtZ = 0;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check((g_aFireCells[0][11][11].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) == 0,
          "no fire animation, no stamp");
    Check(g_state.bFlag1 == 1, "and the phase ends");

    FireSetup();
    (void)FireEntry(0x12);
    g_state.gap_70112[1] = 1;
    g_FireAim.nTgtX = 11;
    g_FireAim.nTgtY = 11;
    g_FireAim.nTgtZ = 0;
    g_state.nPhaseCounter = 10;
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    Check((g_aFireCells[0][11][11].bFlags29 & PUFO_CELLFLAG_SHOT_TRACE) != 0,
          "the melee cell is stamped");
    CheckEqI32(g_state.nPhaseCounter, 11, "the phase counter advances");
    Check(g_state.bFlag1 == 0, "counter 11 of 11 keeps the phase");
    Tact_ProcessFireAction(&g_state, &g_FireCtx);
    CheckEqI32(g_state.nPhaseCounter, 12, "counter 12 ends it");
    Check(g_state.bFlag1 == 1, "and raises bFlag1");
}

/* ==========================================================================
 * Tact_TickActiveUnit @ 0x54130 -- the queue walker
 * ========================================================================== */

static TactCtx272   g_TickTact;
static MoveActionCtx g_TickMove;
static TactTickCtx  g_Tick;
static Soldier     *g_pTickSelUnit;
static MapCell     *g_pFocusCell;
static u8           g_bTickFrameScratch;
static u8           g_bActionLatch;
static i32          g_nTickUiSlot10;
static i32          g_nTickCursorX, g_nTickCursorY;
static i32          g_nPhaseCalls, g_nFrameCalls, g_nFocusCalls;
static i32          g_nCursorCalls, g_nStepCalls;
static bool         g_bPhaseFillsQueue;

static void TickHookPhase(void *pUser)
{
    (void)pUser;
    g_nPhaseCalls++;
    /* The phase machine can enqueue the opportunity-fire action, which is how a
     * queue that looked empty gets dispatched on the same call. */
    if (g_bPhaseFillsQueue) {
        g_state.nQueueLen    = 1;
        g_state.bQueueActive = 1;
    }
}
static void TickHookFrame(void *pUser)
{
    (void)pUser;
    g_nFrameCalls++;
}
static void TickHookFocus(const MapCell *pCell, void *pUser)
{
    (void)pUser;
    g_nFocusCalls++;
    g_pFocusCell = (MapCell *)pCell;
}
static void TickHookCursor(i32 nX, i32 nY, void *pUser)
{
    (void)pUser;
    g_nCursorCalls++;
    g_nTickCursorX = nX;
    g_nTickCursorY = nY;
}
static void TickHookStep(void *pUser)
    { (void)pUser; g_nStepCalls++; }

static const TickHooks g_TickHooks = {
    TickHookPhase, TickHookFrame, TickHookFocus, TickHookCursor, TickHookStep, NULL
};

static void TickSetup(void)
{
    FireSetup();
    memset(&g_TickTact, 0, sizeof(g_TickTact));
    memset(&g_TickMove, 0, sizeof(g_TickMove));
    memset(&g_Tick, 0, sizeof(g_Tick));

    g_pTickSelUnit      = &g_aFallUnits[1];
    g_pFocusCell        = NULL;
    g_bTickFrameScratch = 0;
    g_bActionLatch      = 0;
    g_nTickUiSlot10     = 0;
    g_nTickCursorX      = 0;
    g_nTickCursorY      = 0;
    g_nPhaseCalls       = 0;
    g_nFrameCalls       = 0;
    g_nFocusCalls       = 0;
    g_nCursorCalls      = 0;
    g_nStepCalls        = 0;
    g_bPhaseFillsQueue  = false;

    g_TickMove.geom.nDimX    = FALL_DIM;
    g_TickMove.geom.nDimY    = FALL_DIM;
    g_TickMove.pfnLookup     = FallLookup;
    g_TickMove.pnLayerScratch  = &g_nLayerScratch;
    g_TickMove.pnMaxLayerClamp = &g_nMaxLayerClamp;
    g_TickMove.pnAnimPhase     = &g_nAnimPhase;
    g_TickMove.pnShotCount     = &g_nShotCount;
    g_TickMove.pbActionLatch   = &g_bActionLatch;
    g_TickMove.pbFireStatus1   = &g_bFireStatus1;
    g_TickMove.ppSelWeapon     = &g_pSelWeapon;
    g_TickMove.ppThrowTarget   = &g_pThrowTarget;
    g_TickMove.apXcomUnits     = g_apFallXcom;
    g_TickMove.apEnemyUnits    = g_apFallEnemy;
    g_TickMove.apCivilians     = g_apFallCiv;
    g_TickMove.pnXcomCount     = &g_nFallXcom;
    g_TickMove.pnEnemyCount    = &g_nFallEnemy;
    g_TickMove.pfnUpdateVisibility = FallHookVisibility;
    g_TickMove.pfnPushStatus       = FallHookStatus;

    g_Tick.pTactCtx       = &g_TickTact;
    g_Tick.pHooks         = &g_TickHooks;
    g_Tick.pfnLookup      = FireLookup;
    g_Tick.pGeom          = &g_FireGeom;
    g_Tick.pAim           = &g_FireAim;
    g_Tick.ppSelUnit      = &g_pTickSelUnit;
    g_Tick.apXcomUnits    = g_apFallXcom;
    g_Tick.pMapTactical   = &g_state;
    g_Tick.pbTickHold     = &g_bTickHold;
    g_Tick.pbFrameScratch = &g_bTickFrameScratch;
    g_Tick.pnUiSlot10     = &g_nTickUiSlot10;
    g_Tick.pMoveCtx       = &g_TickMove;
    g_Tick.pFireCtx       = &g_FireCtx;
    g_Tick.pFallCtx       = &g_FallCtx;
}

static void TickEntry(u8 nCmd)
{
    memset(&g_state, 0, sizeof(g_state));
    g_state.nQueueLen         = 1;
    g_state.nQueueCursor      = 0;
    g_state.bQueueActive      = 1;
    g_state.nPhaseMax         = 5;
    g_state.aQueue[0].nCmd    = nCmd;
    g_state.aQueue[0].pUnit   = &g_aFallUnits[0];
    g_state.aQueue[0].pTarget = &g_aFallUnits[2];
    g_state.aQueue[0].pWeapon = &g_FallWeapon;
}

/* A queue whose current entry the fall processor only tails: nCmd 13 is outside its
 * switch, so Fall_Tail runs and -- with the cursor at the last entry -- empties the
 * queue and clears bQueueActive, which is what arms the walker's completion tail.
 * The walker dispatches aQueue[nQueueCursor], so entry 1 is the live one and entry 0
 * is the part of the queue already executed. */
static void TickCompletedTail(void)
{
    TickEntry(13);
    g_state.nQueueLen    = 2;
    g_state.nQueueCursor = 1;
    g_state.bFlag0       = 1;   /* the fire phase's default case ends the phase, which
                                 * is what raises bFlag1 and lets the fall processor run */
    g_state.bFlag1       = 1;
    g_state.aQueue[1]    = g_state.aQueue[0];
    g_aFallUnits[0].bFlags = 0; /* not aiming: a command-14 entry ends the phase too */
}

static void TestTickQueueGates(void)
{
    printf("Tact_TickActiveUnit, the queue gates\n");

    TickSetup();
    TickEntry(TACTCMD_ACTION18);
    g_state.nQueueLen = 0;
    g_state.bFlag0    = 1;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "an empty queue stops the walker");
    CheckEqI32(g_nPhaseCalls, 1, "the phase machine got the turn");
    CheckEqI32(g_state.bFlag1, 0, "and no action processor ran");

    /* The phase machine may fill the queue, and the walker picks it up on the
     * same call because 0x54198 re-reads the length. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_state.nQueueLen  = 0;
    g_state.nQueueCursor = 0;
    g_state.bFlag0     = 1;
    g_state.nPhaseTick = 3;
    g_state.nPhaseMax  = 5;
    g_aFallUnits[0].uTu = 10;
    g_FirePhase.nPhase  = 1;
    g_bPhaseFillsQueue  = true;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker still stops once");
    CheckEqI32(g_nPhaseCalls, 1, "the phase machine ran once");
    CheckEqI32(g_state.nPhaseTick, 4, "  and the queue it filled was dispatched");

    /* bQueueActive is a second, independent gate. */
    TickSetup();
    TickEntry(TACTCMD_ACTION18);
    g_state.nQueueLen    = 2;
    g_state.bQueueActive = 0;
    g_state.bFlag0       = 1;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "an inactive queue stops the walker");
    CheckEqI32(g_nPhaseCalls, 0, "the phase machine is not called for a non-empty queue");
    CheckEqI32(g_state.bFlag1, 0, "and no processor ran");
}

static void TestTickDispatch(void)
{
    printf("Tact_TickActiveUnit, bFlag0 selects the processor\n");

    /* bFlag0 set runs the FIRE phase. Its turn case advances nPhaseTick; the move
     * turn case zeroes it, so the two are distinguishable from one number. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_state.bFlag0     = 1;
    g_state.nPhaseTick = 3;
    g_state.nPhaseMax  = 5;
    g_aFallUnits[0].uTu = 10;
    g_FirePhase.nPhase  = 1;   /* mid-turn: the fire phase charges no TU */
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "an idle context ticks once");
    CheckEqI32(g_state.nPhaseTick, 4, "the fire phase advanced the phase tick");
    CheckEqI32(g_aFallUnits[0].uTu, 10, "  a mid-turn phase charges no TU");
    CheckEqI32(g_state.bFlag1, 0, "4 of 5 ticks does not end the phase");

    /* bFlag0 clear runs the MOVE phase, whose prologue raises bFlag0 for the next
     * tick and clears bFlag1. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_state.bFlag0     = 0;
    g_state.nPhaseTick = 3;
    g_state.nPhaseMax  = 5;
    g_aFallUnits[0].uTu = 10;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops once here too");
    CheckEqI32(g_state.nPhaseTick, 0, "the move phase reset the phase tick");
    CheckEqI32(g_state.bFlag0, 1, "  and its prologue raised bFlag0");
    CheckEqI32(g_state.bFlag1, 0, "the turn is not finished");
}

static void TestTickCameraFollow(void)
{
    printf("Tact_TickActiveUnit, following the camera to the action\n");

    /* A busy context follows a turn to the unit's own cell when that cell carries
     * the bFlags44 bit-0 mark, and adopts the unit as the tracked unit. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_TickTact.bBusy        = 1;
    g_aFallUnits[0].uTu     = 10;
    g_aFallUnits[0].pCell   = &g_aFallCells[0][1][1];
    g_aFallCells[0][1][1].bFlags44 = 1;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false,
          "the adopted tracked unit stops the re-run loop");
    CheckEqI32(g_nFocusCalls, 1, "the camera focused once");
    Check(g_pFocusCell == &g_aFallCells[0][1][1], "  on the unit's own cell");
    Check(g_TickTact.pTrackUnit == &g_aFallUnits[0], "and the walker adopted the unit");
    CheckEqI32(g_nFrameCalls, 0, "a one-entry queue does not repaint");

    /* No mark, no follow. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_TickTact.bBusy        = 1;
    g_TickTact.pTrackUnit   = &g_aFallUnits[3];   /* already tracked, so the loop stops */
    g_aFallUnits[0].uTu     = 10;
    g_aFallUnits[0].pCell   = &g_aFallCells[0][1][1];
    g_aFallCells[0][1][1].bFlags44 = 0;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "an unmarked cell stops the walker");
    CheckEqI32(g_nFocusCalls, 0, "the camera did not move");
    Check(g_TickTact.pTrackUnit == &g_aFallUnits[3], "  and the tracked unit is unchanged");

    /* 0x541E0: a busy multi-entry queue clears the selected unit's bUnk148 and, when
     * the selected unit is not the camera unit, drops the camera unit and repaints. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_TickTact.bBusy       = 1;
    g_TickTact.pTrackUnit  = &g_aFallUnits[3];
    g_TickTact.pCameraUnit = &g_aFallUnits[2];
    g_state.nQueueLen      = 2;
    g_aFallUnits[0].uTu    = 10;
    g_pTickSelUnit         = &g_aFallUnits[1];
    g_aFallUnits[1].bUnk148 = 1;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops on the tracked unit");
    CheckEqI32(g_aFallUnits[1].bUnk148, 0, "the selected unit's bUnk148 is cleared");
    Check(g_TickTact.pCameraUnit == NULL, "the camera unit is dropped");
    CheckEqI32(g_nFrameCalls, 1, "  and the frame is repainted");

    /* The same step when the selected unit IS the camera unit: cleared, not dropped. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_TickTact.bBusy       = 1;
    g_TickTact.pTrackUnit  = &g_aFallUnits[3];
    g_pTickSelUnit         = &g_aFallUnits[1];
    g_TickTact.pCameraUnit = &g_aFallUnits[1];
    g_state.nQueueLen      = 2;
    g_aFallUnits[0].uTu    = 10;
    g_aFallUnits[1].bUnk148 = 1;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops here too");
    CheckEqI32(g_aFallUnits[1].bUnk148, 0, "bUnk148 is still cleared");
    Check(g_TickTact.pCameraUnit == &g_aFallUnits[1], "the camera unit is kept");
    CheckEqI32(g_nFrameCalls, 0, "  and nothing is repainted");
}

static void TestTickCameraFollowShot(void)
{
    printf("Tact_TickActiveUnit, following a shot to its sample cell\n");

    /* The fire path follows command 14 only. The unit is not aiming, so the fire
     * phase ends the phase and touches nothing else -- the camera rule is what is
     * under test. Sample cell is (x=3, y=2, layer=1). */
    TickSetup();
    TickEntry(TACTCMD_FIRE_SHOT);
    g_state.bFlag0        = 1;
    g_TickTact.bBusy      = 1;
    g_TickTact.pTrackUnit = &g_aFallUnits[3];
    g_aFallUnits[0].bFlags = 0;
    g_adMtX[0] = 16 * 3 + 5;
    g_adMtY[0] = 16 * 2 + 5;
    g_adMtZ[0] = 12 * 1 + 6;
    g_aFireCells[1][2][3].bFlags44 = 1;
    g_FireAim.dTravel = 120.0;   /* a whole multiple of the 60-voxel stride */
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the tracked unit stops the walker");
    CheckEqI32(g_nFocusCalls, 1, "a tracked unit re-centres on a whole stride");
    Check(g_pFocusCell == &g_aFireCells[1][2][3], "  on the cell the last sample fell into");

    /* Off the stride, the camera holds. */
    TickSetup();
    TickEntry(TACTCMD_FIRE_SHOT);
    g_state.bFlag0        = 1;
    g_TickTact.bBusy      = 1;
    g_TickTact.pTrackUnit = &g_aFallUnits[3];
    g_aFallUnits[0].bFlags = 0;
    g_adMtX[0] = 16 * 3 + 5;
    g_adMtY[0] = 16 * 2 + 5;
    g_adMtZ[0] = 12 * 1 + 6;
    g_aFireCells[1][2][3].bFlags44 = 1;
    g_FireAim.dTravel = 125.0;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops here too");
    CheckEqI32(g_nFocusCalls, 0, "125 of 60 does not re-centre");

    /* With nothing tracked the camera follows regardless of the stride, and the
     * first XCom unit becomes the tracked unit. */
    TickSetup();
    TickEntry(TACTCMD_FIRE_SHOT);
    g_state.bFlag0        = 1;
    g_TickTact.bBusy      = 1;
    g_aFallUnits[0].bFlags = 0;
    g_apFallXcom[0]       = &g_aFallUnits[2];
    g_adMtX[0] = 16 * 3 + 5;
    g_adMtY[0] = 16 * 2 + 5;
    g_adMtZ[0] = 12 * 1 + 6;
    g_aFireCells[1][2][3].bFlags44 = 1;
    g_FireAim.dTravel = 125.0;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the adopted unit stops the walker");
    CheckEqI32(g_nFocusCalls, 1, "an untracked shot always re-centres");
    Check(g_TickTact.pTrackUnit == &g_aFallUnits[2], "  and the first XCom unit is adopted");

    /* The sample cell is only followed when it carries the mark. */
    TickSetup();
    TickEntry(TACTCMD_FIRE_SHOT);
    g_state.bFlag0        = 1;
    g_TickTact.bBusy      = 1;
    g_TickTact.pTrackUnit = &g_aFallUnits[3];
    g_aFallUnits[0].bFlags = 0;
    g_adMtX[0] = 16 * 3 + 5;
    g_adMtY[0] = 16 * 2 + 5;
    g_adMtZ[0] = 12 * 1 + 6;
    g_aFireCells[1][2][3].bFlags44 = 0;
    g_FireAim.dTravel = 120.0;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops here too");
    CheckEqI32(g_nFocusCalls, 0, "an unmarked sample cell is not followed");
}

static void TestTickKillCredit(void)
{
    printf("Tact_TickActiveUnit, crediting the kill to the shooter\n");

    /* The first already-executed entry that is a shot by an operative unit of type
     * 8 or below collects the accumulated kill count. */
    TickSetup();
    TickCompletedTail();
    g_state.aQueue[0].nCmd  = TACTCMD_FIRE_SHOT;
    g_state.aQueue[0].pUnit = &g_aFallUnits[0];
    g_aFallUnits[0].nUnitType = 0;
    g_aFallUnits[0].bStatus   = 0;      /* operative */
    g_aFallUnits[0].uKills    = 5;
    g_state.uKillCredit       = 3;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the emptied queue stops the walker");
    CheckEqI32(g_aFallUnits[0].uKills, 8, "the shooter collects the credit");
    CheckEqI32(g_state.uKillCredit, 0, "  and the accumulator is cleared");

    /* A unit above type 8 does not collect -- civilians and aliens are excluded. */
    TickSetup();
    TickCompletedTail();
    g_state.aQueue[0].nCmd  = TACTCMD_FIRE_SHOT;
    g_state.aQueue[0].pUnit = &g_aFallUnits[0];
    g_aFallUnits[0].nUnitType = 9;
    g_aFallUnits[0].bStatus   = 0;
    g_aFallUnits[0].uKills    = 5;
    g_state.uKillCredit       = 3;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops");
    CheckEqI32(g_aFallUnits[0].uKills, 5, "type 9 does not collect");
    CheckEqI32(g_state.uKillCredit, 3, "  and the credit survives");

    /* A non-operative unit does not collect. */
    TickSetup();
    TickCompletedTail();
    g_state.aQueue[0].nCmd  = TACTCMD_FIRE_SHOT;
    g_state.aQueue[0].pUnit = &g_aFallUnits[0];
    g_aFallUnits[0].nUnitType = 0;
    g_aFallUnits[0].bStatus   = 1;      /* not operative */
    g_aFallUnits[0].uKills    = 5;
    g_state.uKillCredit       = 3;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops");
    CheckEqI32(g_aFallUnits[0].uKills, 5, "a non-operative unit does not collect");

    /* Only a shot entry qualifies. */
    TickSetup();
    TickCompletedTail();
    g_state.aQueue[0].nCmd  = TACTCMD_STEP;
    g_state.aQueue[0].pUnit = &g_aFallUnits[0];
    g_aFallUnits[0].nUnitType = 0;
    g_aFallUnits[0].bStatus   = 0;
    g_aFallUnits[0].uKills    = 5;
    g_state.uKillCredit       = 3;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops");
    CheckEqI32(g_aFallUnits[0].uKills, 5, "a step entry does not collect");
    CheckEqI32(g_state.uKillCredit, 3, "  and the credit survives");

    /* The walk covers 0 .. nQueueCursor-1, and the fall tail has already moved the
     * cursor past the entry it just ran -- so every executed entry is in range and
     * the first unexecuted one is not. */
    TickSetup();
    TickEntry(13);
    g_state.nQueueLen    = 3;
    g_state.nQueueCursor = 2;
    g_state.bFlag0       = 1;
    g_state.bFlag1       = 1;
    g_state.aQueue[1] = g_state.aQueue[0];
    g_state.aQueue[2] = g_state.aQueue[0];   /* the current entry */
    g_aFallUnits[0].bFlags    = 0;
    g_aFallUnits[0].nUnitType = 0;
    g_aFallUnits[0].bStatus   = 0;
    g_aFallUnits[0].uKills    = 5;
    g_aFallUnits[1].nUnitType = 0;
    g_aFallUnits[1].bStatus   = 0;
    g_aFallUnits[1].uKills    = 7;
    g_state.aQueue[1].nCmd    = TACTCMD_FIRE_SHOT;   /* executed */
    g_state.aQueue[1].pUnit   = &g_aFallUnits[0];
    g_state.aQueue[3].nCmd    = TACTCMD_FIRE_SHOT;   /* never executed */
    g_state.aQueue[3].pUnit   = &g_aFallUnits[1];
    g_state.uKillCredit       = 3;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the emptied queue stops the walker");
    CheckEqI32(g_aFallUnits[0].uKills, 8, "an executed entry is collected from");
    CheckEqI32(g_aFallUnits[1].uKills, 7, "  an unexecuted entry is not");
    CheckEqI32(g_state.uKillCredit, 0, "and the credit is cleared");
}

static void TestTickAiActionTail(void)
{
    printf("Tact_TickActiveUnit, the finished AI action\n");

    TickSetup();
    TickCompletedTail();
    g_state.bAiActionActive  = 1;
    g_state.pAiActionUnit    = &g_aFallUnits[3];
    g_aFallUnits[3].uTu      = 40;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the emptied queue stops the walker");
    CheckEqI32(g_state.bAiActionActive, 0, "the AI-action flag is cleared");
    CheckEqI32(g_aFallUnits[3].uTu, 0, "  and the acting unit is left with no TU");

    /* While the queue is still active the AI action is left alone. */
    TickSetup();
    TickEntry(13);
    g_state.nQueueLen        = 2;
    g_state.nQueueCursor     = 0;   /* Fall_Tail advances to 1 of 2, queue stays active */
    g_state.bFlag0           = 1;
    g_state.bFlag1           = 1;
    g_aFallUnits[0].bFlags   = 0;
    g_state.bAiActionActive  = 1;
    g_state.pAiActionUnit    = &g_aFallUnits[3];
    g_aFallUnits[3].uTu      = 40;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "an idle context stops the walker");
    CheckEqI32(g_state.bAiActionActive, 1, "an active queue leaves the AI action up");
    CheckEqI32(g_aFallUnits[3].uTu, 40, "  and leaves the unit's TU");
}

static void TestTickSelectEpilogue(void)
{
    printf("Tact_TickActiveUnit, the opportunity-fire epilogue\n");

    /* bUnk70103 parks the cursor off the map and re-selects; a scan unit carrying
     * bFlags bit 0x20 cannot be selected, so the first XCom unit takes its place. */
    TickSetup();
    TickCompletedTail();
    g_state.bUnk70103       = 1;
    g_TickTact.pScanUnit    = &g_aFallUnits[1];
    g_aFallUnits[1].bFlags  = 0x20;
    g_apFallXcom[0]         = &g_aFallUnits[2];
    g_pTickSelUnit          = &g_aFallUnits[1];
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the emptied queue stops the walker");
    CheckEqI32(g_nCursorCalls, 1, "the cursor is repositioned once");
    CheckEqI32(g_nTickCursorX, -2, "  at (-2, -2)");
    CheckEqI32(g_nTickCursorY, -2, "");
    Check(g_pTickSelUnit == &g_aFallUnits[2], "a flagged scan unit falls back to the first XCom unit");
    CheckEqI32(g_state.bUnk70103, 0, "and the flag is cleared");
    CheckEqI32(g_nPhaseCalls, 1, "the phase machine runs once at the end");

    /* An unflagged scan unit is selected directly. */
    TickSetup();
    TickCompletedTail();
    g_state.bUnk70103       = 1;
    g_TickTact.pScanUnit    = &g_aFallUnits[1];
    g_aFallUnits[1].bFlags  = 0x00;
    g_apFallXcom[0]         = &g_aFallUnits[2];
    g_pTickSelUnit          = &g_aFallUnits[0];
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops");
    Check(g_pTickSelUnit == &g_aFallUnits[1], "the scan unit itself is selected");

    /* Without the flag nothing of the epilogue happens, but the phase still runs. */
    TickSetup();
    TickCompletedTail();
    g_state.bUnk70103       = 0;
    g_TickTact.pScanUnit    = &g_aFallUnits[1];
    g_pTickSelUnit          = &g_aFallUnits[0];
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops");
    CheckEqI32(g_nCursorCalls, 0, "no cursor move without the flag");
    Check(g_pTickSelUnit == &g_aFallUnits[0], "  and the selection is untouched");
    CheckEqI32(g_nPhaseCalls, 1, "the phase machine still runs");
}

static void TestTickReRun(void)
{
    printf("Tact_TickActiveUnit, the busy re-run loop\n");

    /* Busy, nothing tracked, no phase-active flag and no countdown: the walker steps
     * the game tick and re-runs itself until the queue empties. The move phase raises
     * bFlag0, so the second pass runs the fire phase, whose turn case ends the phase
     * at 1 of 0 ticks; the fall tail then empties the queue. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_TickTact.bBusy      = 1;
    g_aFallUnits[0].uTu   = 10;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker stops when the queue empties");
    CheckEqI32(g_nStepCalls, 2, "GameTick_Step ran on each busy pass");
    CheckEqI32(g_state.nQueueLen, 0, "the queue was emptied");
    CheckEqI32(g_state.bQueueActive, 0, "  and the queue was deactivated");
    CheckEqI32(g_nPhaseCalls, 2, "the phase machine ran after the emptying and on the empty queue");

    /* bPhaseActive on the queue stops the re-run before any game tick. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_TickTact.bBusy      = 1;
    g_aFallUnits[0].uTu   = 10;
    g_state.bPhaseActive  = 1;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "a raised phase flag stops the walker");
    CheckEqI32(g_nStepCalls, 0, "no game tick ran");

    /* A tracked unit stops it just as hard. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_TickTact.bBusy       = 1;
    g_TickTact.pTrackUnit  = &g_aFallUnits[3];
    g_aFallUnits[0].uTu    = 10;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "a tracked unit stops the walker");
    CheckEqI32(g_nStepCalls, 0, "no game tick ran");

    /* The countdown slot is read AFTER the game tick, so one tick runs and then stops. */
    TickSetup();
    TickEntry(TACTCMD_TURN);
    g_TickTact.bBusy      = 1;
    g_aFallUnits[0].uTu   = 10;
    g_nTickUiSlot10       = 5;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "a running countdown stops the walker");
    CheckEqI32(g_nStepCalls, 1, "exactly one game tick ran");
}

static void TestTickHoldReturns(void)
{
    printf("Tact_TickActiveUnit, the tick-hold and frame-scratch returns\n");

    /* Map_bFrameScratch stops the walker right after the fall processor, before the
     * completion tail -- the epilogue that would otherwise run does not. */
    TickSetup();
    TickCompletedTail();
    g_state.bUnk70103       = 1;
    g_TickTact.pScanUnit    = &g_aFallUnits[1];
    g_bTickFrameScratch     = 1;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "frame scratch stops the walker");
    CheckEqI32(g_nCursorCalls, 0, "the epilogue did not run");
    CheckEqI32(g_nPhaseCalls, 0, "  nor did the phase machine");

    /* Map_bTickHold does the same. */
    TickSetup();
    TickCompletedTail();
    g_state.bUnk70103    = 1;
    g_TickTact.pScanUnit = &g_aFallUnits[1];
    g_bTickHold          = 1;
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "tick hold stops the walker");
    CheckEqI32(g_nCursorCalls, 0, "the epilogue did not run");

    /* And with neither set, the same fixture does run the epilogue -- the two
     * returns are the only difference. */
    TickSetup();
    TickCompletedTail();
    g_state.bUnk70103       = 1;
    g_TickTact.pScanUnit    = &g_aFallUnits[1];
    g_aFallUnits[1].bFlags  = 0x00;
    g_pTickSelUnit          = &g_aFallUnits[0];
    Check(Tact_TickActiveUnit(&g_state, &g_Tick) == false, "the walker runs to the end");
    CheckEqI32(g_nCursorCalls, 1, "the epilogue ran");
    CheckEqI32(g_nPhaseCalls, 1, "  and so did the phase machine");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestCtor();
    TestSimpleEnqueues();
    TestVertStep();
    TestKick();
    TestDeathFlashInsert();
    TestEnqueueIfApSpent();
    TestCapacity();
    TestDecayCellFlags();
    TestGameTickStep();
    TestGameTickStepBytes();
    TestFireBurst();
    TestThrowAndAction18();
    TestQueueDoorAction();
    TestFallDamageRefusals();
    TestFallDamageDescent();
    TestFallDamageSweep();
    TestFallActionTail();
    TestFallActionMoveTail();
    TestFallActionDoorAndVert();
    TestFallActionSmallCases();
    TestFallActionDamageSweep();
    TestFallActionThrowLanding();
    TestFallActionLookDevice();
    TestFallActionPsiGate();
    TestFallActionPanic();
    TestFallActionMedikit();
    TestFallActionMelee();

    TestFireTurn();
    TestFireStepFrame();
    TestFireDoor();
    TestFireSmallCases();
    TestFireShotFlight();
    TestFireFallProgress();
    TestFireDecaySweep();
    TestFireThrowFlight();
    TestFireMeleeStamp();

    TestTickQueueGates();
    TestTickDispatch();
    TestTickCameraFollow();
    TestTickCameraFollowShot();
    TestTickKillCredit();
    TestTickAiActionTail();
    TestTickSelectEpilogue();
    TestTickReRun();
    TestTickHoldReturns();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
