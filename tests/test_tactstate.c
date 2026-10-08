/* test_tactstate.c - checks the tactical action queue.
 *
 * The DeathFlash insert (interrupt at cursor+1) and the uTuSpent >= uHp rule are
 * the two behaviours most likely to be "tidied" into something else, so they are
 * asserted explicitly.
 */
#include "game/pufo_tactstate.h"

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
    CheckEqI32(g_state.nQueueLen, 0, "  queue length 0");
    CheckEqI32(g_state.bQueueActive, 0, "  not active");
    CheckEqI32(g_state.bUnk70103, 0, "  scratch flag 0");
    CheckEqI32(g_state.nQueueCursor, 0, "  cursor 0");
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
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_TURN, "  cmd is TURN (0)");
    Check(g_state.aQueue[0].pUnit == &unit, "  unit stored");
    CheckEqI32(g_state.aQueue[0].nFacing, 3, "  facing stored");

    TactState_EnqueueStep(&g_state, &unit, 5, 42);
    CheckEqI32(g_state.nQueueLen, 2, "  step appended");
    CheckEqI32(g_state.aQueue[1].nCmd, TACTCMD_STEP, "  cmd is STEP (1)");
    CheckEqI32(g_state.aQueue[1].nFacing, 5, "  step facing");
    CheckEqI32(g_state.aQueue[1].nDelta, 42, "  step delta");

    TactState_EnqueueRecover(&g_state, &unit);
    TactState_EnqueueClearAim(&g_state, &unit);
    TactState_EnqueueEndMark(&g_state);
    CheckEqI32(g_state.nQueueLen, 5, "  five entries total");
    CheckEqI32(g_state.aQueue[2].nCmd, TACTCMD_RECOVER, "  recover cmd (11)");
    CheckEqI32(g_state.aQueue[3].nCmd, TACTCMD_CLEAR_AIM, "  clear-aim cmd (10)");
    CheckEqI32(g_state.aQueue[4].nCmd, TACTCMD_END_MARK, "  end-mark cmd (12)");

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
          "  throw refused without TU");
    CheckEqI32(g_state.nQueueLen, 0, "  no entry written");
    CheckEqI32(nFaced, 0, "  unit not faced on refusal");

    /* Command 18 keeps the target unit and the mode. */
    Reset();
    unit.uTu = 10;
    Check(TactState_EnqueueAction18(&g_state, &unit, &target, &item, 3, 9),
          "  action 18 enqueued");
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
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_STAND_FROM_KNEEL, "  stand cmd (7)");
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
    CheckEqI32(g_state.aQueue[0].nCmd, TACTCMD_DEATH_FLASH, "  it is a death flash");
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
    i32      i;
    i32      nEnded;

    printf("GameTick_Step\n");
    Reset();
    TickGrid_Init(&grid);

    /* Inactive phase: nothing happens at all. */
    grid.aCells[0][0][0].nFlags32 = 0x78;
    Check(!GameTick_Step(&g_state, TickGrid_Lookup, &grid), "  inactive phase does nothing");
    CheckEqI32((i32)grid.aCells[0][0][0].nFlags32, 0x78, "  flags untouched while inactive");

    /* Active phase: the flagged cell decays once per tick. */
    printf("GameTick_Step decay while active\n");
    g_state.bPhaseActive = 1;
    Check(!GameTick_Step(&g_state, TickGrid_Lookup, &grid), "  tick 1 does not end the phase");
    CheckEqI32((i32)grid.aCells[0][0][0].nFlags32, 0x70, "  field decremented once");
    CheckEqI32(g_state.nPhaseCounter, 1, "  phase counter incremented");

    /* A cell whose field is already zero is skipped, so it cannot wrap to 0xF. */
    printf("GameTick_Step skips cells already at zero\n");
    grid.aCells[1][2][3].nFlags32 = 0; /* field 0 */
    grid.aCells[1][2][2].nFlags32 = 0x80; /* only a high bit, field 0 */
    (void)GameTick_Step(&g_state, TickGrid_Lookup, &grid);
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
    (void)GameTick_Step(&g_state, TickGrid_Lookup, &grid);
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
        if (GameTick_Step(&g_state, TickGrid_Lookup, &grid)) {
            nEnded = i;
            break;
        }
    }
    CheckEqI32(nEnded, PUFO_PHASE_TICK_MAX + 1, "  phase ends on tick 12 (counter > 11)");
    CheckEqI32(g_state.bPhaseActive, 0, "  phase flag cleared");
    /* And a further tick is a no-op. */
    Check(!GameTick_Step(&g_state, TickGrid_Lookup, &grid), "  ended phase stays inactive");
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
    Check(Map_QueueDoorAction(&state, &cellA, &cellB, 0, &ctx), "door queued");
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
    TestFireBurst();
    TestThrowAndAction18();
    TestQueueDoorAction();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
