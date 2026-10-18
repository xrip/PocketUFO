/* test_enemyai.c - checks the enemy AI's multi-target type predicate.
 *
 * The predicate is driven by a 4-byte-stride table whose two used columns were
 * decoded byte by byte. The interesting part is the asymmetry: roles 1 and 2 also
 * require the unit to be SMALL, while roles 3 and 4 test only the flag column.
 */
#include "game/pufo_enemyai.h"
#include "game/pufo_tactui.h" /* the PUFO_ACTION_MODE_* codes the tick writes */

#include <stdint.h>
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

/* The two table columns, as decoded from byte_A7B80 @ 0xa7b80. Repeating them here
 * makes the test independent of the implementation's own copy: if the port's table
 * drifts, these expectations still describe the original. */
/* Independent transcription of byte_A7B80 @ 0xa7b80, 24 rows of 4 bytes:
 * byte[+0] is flag 0, byte[+1] is the is-large flag Unit_bIsLarge tests. */
static const u8 k_aFlag0[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0
};
static const u8 k_aLarge[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 1, 1, 0, 0
};

static void TestTableMatchesDecode(void)
{
    printf("The unit-type table matches the decoded original bytes\n");
    for (i32 t = 0; t < PUFO_UNIT_TYPE_COUNT; ++t) {
        if (g_aUnitTypeFlag0[t] != k_aFlag0[t]
            || g_aUnitTypeIsLarge[t] != k_aLarge[t]) {
            printf("  MISMATCH at type %d: got flag0=%d large=%d\n", t,
                   g_aUnitTypeFlag0[t], g_aUnitTypeIsLarge[t]);
            g_nFailures += 1;
        }
    }
    Check(true, "  all 16 types agree with the byte_A7B80 decode");
}

static void TestPredicateExhaustive(void)
{
    Soldier unit;
    Soldier other;

    printf("EnemyAi_MultiTargetTypeMatch -- all types x all roles\n");
    memset(&unit, 0, sizeof(unit));
    memset(&other, 0, sizeof(other));

    /* Walk every type against every role in 0..5 and compare against the rule
     * transcribed from the decompilation. */
    for (i32 role = 0; role <= 5; ++role) {
        for (i32 t = 0; t < PUFO_UNIT_TYPE_COUNT; ++t) {
            bool bWant;

            unit.nUnitType = (u8)t;
            other.nMultiTarget = (i8)role;

            switch (role) {
                case 1:  bWant = (k_aFlag0[t] == 1 && k_aLarge[t] == 0); break;
                case 2:  bWant = (k_aFlag0[t] == 0 && k_aLarge[t] == 0); break;
                case 3:  bWant = (k_aFlag0[t] == 1); break;
                case 4:  bWant = (k_aFlag0[t] == 0); break;
                default: bWant = true; break;
            }
            if (EnemyAi_MultiTargetTypeMatch(&unit, &other) != bWant) {
                printf("  MISMATCH role %d type %d: got %d want %d\n", role, t,
                       (int)EnemyAi_MultiTargetTypeMatch(&unit, &other), (int)bWant);
                g_nFailures += 1;
            }
        }
    }
    CheckEqI32(g_nFailures, 0, "  every role/type pair matches the transcribed rule");
}

static void TestRoleReadFromOther(void)
{
    Soldier unit;
    Soldier other;

    printf("The role code is read from the OTHER unit\n");
    memset(&unit, 0, sizeof(unit));
    memset(&other, 0, sizeof(other));

    /* Type 3 has flag0 = 1 and is small. Role 1 accepts it. */
    unit.nUnitType = 3;
    other.nMultiTarget = 1;
    Check(EnemyAi_MultiTargetTypeMatch(&unit, &other),
          "  role 1 accepts a flag-set small unit");

    /* The candidate's own nMultiTarget must not be consulted: set it to a role that
     * would reject the unit type, and the answer must not change. */
    unit.nMultiTarget = 2;
    Check(EnemyAi_MultiTargetTypeMatch(&unit, &other),
          "  the candidate's own role field is ignored");

    /* Flip the OTHER unit's role and the answer changes. */
    other.nMultiTarget = 2;   /* requires flag0 == 0, but type 3 has flag0 == 1 */
    Check(!EnemyAi_MultiTargetTypeMatch(&unit, &other),
          "  flipping the other unit's role flips the answer");
}

static void TestSizeAsymmetry(void)
{
    Soldier unit;
    Soldier other;

    printf("Roles 1/2 test size; roles 3/4 do not\n");
    memset(&unit, 0, sizeof(unit));
    memset(&other, 0, sizeof(other));

    /* Type 7: flag0 = 1 and large = 1. It is the clean probe for the asymmetry. */
    unit.nUnitType = 7;
    CheckEqI32(g_aUnitTypeFlag0[7], 1, "  type 7 has flag0 set");
    CheckEqI32(g_aUnitTypeIsLarge[7], 1, "  type 7 is large");

    other.nMultiTarget = 1;   /* flag set AND small -> rejected because it is large */
    Check(!EnemyAi_MultiTargetTypeMatch(&unit, &other),
          "  role 1 rejects a LARGE unit even with the flag set");

    other.nMultiTarget = 3;   /* flag set, size not tested -> accepted */
    Check(EnemyAi_MultiTargetTypeMatch(&unit, &other),
          "  role 3 accepts the same large unit (size is not tested)");

    /* Type 4: flag0 = 0 and large = 1, for the same probe on the clear side. */
    unit.nUnitType = 4;
    CheckEqI32(g_aUnitTypeFlag0[4], 0, "  type 4 has flag0 clear");
    CheckEqI32(g_aUnitTypeIsLarge[4], 1, "  type 4 is large");

    other.nMultiTarget = 2;
    Check(!EnemyAi_MultiTargetTypeMatch(&unit, &other),
          "  role 2 rejects a large unit with the flag clear");

    other.nMultiTarget = 4;
    Check(EnemyAi_MultiTargetTypeMatch(&unit, &other),
          "  role 4 accepts it (size is not tested)");
}

static void TestOutOfRangeAndNull(void)
{
    Soldier unit;
    Soldier other;

    printf("Out-of-range type and NULL handling\n");
    memset(&unit, 0, sizeof(unit));
    memset(&other, 0, sizeof(other));

    /* The original indexes the table with no bounds check; the port refuses. */
    unit.nUnitType = 200;
    other.nMultiTarget = 0;
    Check(!EnemyAi_MultiTargetTypeMatch(&unit, &other),
          "  out-of-range type refused instead of reading past the table");

    unit.nUnitType = 0;
    Check(!EnemyAi_MultiTargetTypeMatch(NULL, &other), "  NULL candidate refused");
    Check(!EnemyAi_MultiTargetTypeMatch(&unit, NULL), "  NULL other refused");
}

/* --- random turn ----------------------------------------------------------- */

typedef struct TurnLog {
    i32 nDraws;
    u32 nLastMax;
    i32 nTurns;
    i32 nLastFacing;
} TurnLog;

static TurnLog g_turn;

static u32 Turn_Rand(u32 nMax, void *pUser)
{
    (void)pUser;
    g_turn.nDraws += 1;
    g_turn.nLastMax = nMax;
    return 5; /* a fixed facing, so the test is deterministic */
}

static void Turn_Enqueue(Soldier *pUnit, u8 nFacing, void *pUser)
{
    (void)pUnit;
    (void)pUser;
    g_turn.nTurns += 1;
    g_turn.nLastFacing = nFacing;
}

static void TestRandomTurn(void)
{
    Soldier      unit;
    EnemyAiHooks hooks;

    printf("EnemyAi_EnqueueRandomTurn\n");
    memset(&unit, 0, sizeof(unit));
    memset(&g_turn, 0, sizeof(g_turn));
    hooks.pfnRandU16 = Turn_Rand;
    hooks.pfnEnqueueTurnAndKick = Turn_Enqueue;
    hooks.pUser = NULL;

    EnemyAi_EnqueueRandomTurn(&unit, &hooks);
    CheckEqI32(g_turn.nDraws, 1, "  one RNG draw");
    CheckEqI32((i32)g_turn.nLastMax, (i32)PUFO_ENEMYAI_FACING_MAX,
               "  the draw bound is 8");
    CheckEqI32(g_turn.nTurns, 1, "  one turn enqueued and kicked");
    CheckEqI32(g_turn.nLastFacing, 5, "  the drawn facing was used");
    CheckEqI32(PUFO_ENEMYAI_FACING_MAX, 8, "  facing max is 8 (0..7)");

    /* NULL safety. */
    EnemyAi_EnqueueRandomTurn(NULL, &hooks);
    EnemyAi_EnqueueRandomTurn(&unit, NULL);
    Check(true, "  NULL unit and NULL hooks are safe");
}

/* --- EnemyAi_TryFireOrWander @ 0x4abc8 ---------------------------------------- */

/* A 20 x 20 x 4 cell grid, which is what Map_IsValidCell admits for a 2 x 2 map
 * (PUFO_CELL_SIZE is 10 cells per tile). The unit starts at (7,7,2) so that both
 * draws -- rand(nX+5)-2 and rand(nX+20)-10 -- can land on and off the unit. */
#define AI_GRID_X 20
#define AI_GRID_Y 20
#define AI_GRID_Z 4

/* A value no real TactItem has, so "pSelWeapon was cleared" is provable. */
#define AI_SENTINEL ((TactItem *)(uintptr_t)0x1)

static MapCell       g_aGrid[AI_GRID_Z][AI_GRID_Y][AI_GRID_X];
static Mc1Rec        g_FloorSpr;
static Soldier       g_Occupant;
static MapGeom       g_Geom;
static TactState     g_State;
static Soldier       g_Unit;
static TactItem      g_ItemL;
static TactItem      g_ItemR;
static ObjectRecFull g_ObjL;
static ObjectRecFull g_ObjR;
static TactItem     *g_pSelWeapon;
static i32           g_nActionMode;
static i32           g_anCost[12];
static i32           g_anEnabled[12];
static u8            g_bActionLatch;

static const u32 *g_pRandScript;
static i32        g_nRandLen;
static i32        g_nRandIdx;
static i32        g_nRandCalls;
static i32        g_nRandOverflow;
static i32        g_nPathCalls;
static i32        g_nPathX;
static i32        g_nPathY;
static i32        g_nPathLayer;
static i32        g_nPathFlag;
static bool       g_bPathOk;
static i32        g_nFillCalls;
static i32        g_nPushCalls;
static char       g_szPushed[256];
/* The TEXTS-record sink: the record-1 push the refused enqueue callees do in the original. */
static i32        g_aRecordPushes[8];
static i32        g_nRecordPushes;
static i32        g_nFaceCalls;
static EnemyAiActCtx g_Ctx;

static MapCell *Cell(i32 nX, i32 nY, i32 nLayer)
{
    return &g_aGrid[nLayer][nY][nX];
}

static MapCell *Ai_Lookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nY < 0 || nLayer < 0 || nX >= AI_GRID_X || nY >= AI_GRID_Y
        || nLayer >= AI_GRID_Z) {
        return NULL;
    }
    return Cell(nX, nY, nLayer);
}

/* The script is consumed in call order, so a test can name the exact draws the
 * original's loop would make and then assert how many draws actually happened.
 *
 * The values are RNG OUTPUTS, not coordinates: the fire path computes
 * `nX + rand(5) - 2` and the wander path `nX + rand(20) - 10`, so a wander draw of
 * 13 aimed from column 7 lands on column 10.
 *
 * Running past the end of the script is a fixture bug, and both original loops retry
 * forever on a rejected draw, so it would hang instead of failing. The valve below
 * turns that hang into a visible failure. */
static u32 Ai_Rand(u32 nMax, void *pUser)
{
    (void)pUser;
    (void)nMax;
    g_nRandCalls += 1;
    if (g_nRandIdx >= g_nRandLen) {
        g_nRandOverflow += 1;
        printf("  FAIL the RNG script ran dry at draw %d -- the draw loop wants more "
               "draws than the test scripted\n", g_nRandCalls);
        exit(2);
    }
    return g_pRandScript[g_nRandIdx++];
}

static bool Ai_Path(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser)
{
    (void)pUser;
    g_nPathCalls += 1;
    g_nPathX = nX;
    g_nPathY = nY;
    g_nPathLayer = nLayer;
    g_nPathFlag = bFlag;
    return g_bPathOk;
}

static void Ai_FillCosts(void *pUser)
{
    (void)pUser;
    g_nFillCalls += 1;
}

static void Ai_Push(const char *pszMsg, void *pUser)
{
    (void)pUser;
    g_nPushCalls += 1;
    snprintf(g_szPushed, sizeof(g_szPushed), "%s", pszMsg);
}

static void Ai_PushRecord(i32 nRecordIndex, void *pUser)
{
    (void)pUser;
    if (g_nRecordPushes < (i32)(sizeof(g_aRecordPushes) / sizeof(g_aRecordPushes[0]))) {
        g_aRecordPushes[g_nRecordPushes] = nRecordIndex;
    }
    g_nRecordPushes++;
}

static void Ai_Face(Soldier *pUnit, i32 nX, i32 nY, void *pUser)
{
    (void)pUnit;
    (void)nX;
    (void)nY;
    (void)pUser;
    g_nFaceCalls += 1;
}

static void AiReset(const u32 *pScript, i32 nLen)
{
    memset(g_aGrid, 0, sizeof(g_aGrid));
    for (i32 z = 0; z < AI_GRID_Z; ++z) {
        for (i32 y = 0; y < AI_GRID_Y; ++y) {
            for (i32 x = 0; x < AI_GRID_X; ++x) {
                MapCell *pCell = Cell(x, y, z);
                pCell->nX = (u8)x;
                pCell->nY = (u8)y;
                pCell->nZ = (u8)z;
            }
        }
    }

    memset(&g_State, 0, sizeof(g_State));
    memset(&g_Unit, 0, sizeof(g_Unit));
    memset(&g_ItemL, 0, sizeof(g_ItemL));
    memset(&g_ItemR, 0, sizeof(g_ItemR));
    memset(&g_ObjL, 0, sizeof(g_ObjL));
    memset(&g_ObjR, 0, sizeof(g_ObjR));
    memset(&g_Occupant, 0, sizeof(g_Occupant));

    g_Geom.nDimX = 2;
    g_Geom.nDimY = 2;

    g_Unit.pCell    = Cell(7, 7, 2);
    g_Unit.nUnitType = 0;   /* small, flag0 clear */
    g_Unit.uTu      = 100;
    snprintf(g_Unit.szName, sizeof(g_Unit.szName), "%s", "Sectoid");

    /* Two one-handed magazines, distinct priorities so the drop order is visible. */
    g_ObjL.bTwoHanded = 0;
    g_ObjL.bAmmoCap   = 30;
    g_ObjL.bListPri   = 1;
    g_ObjL.uSortKey   = 20;
    g_ObjR.bTwoHanded = 0;
    g_ObjR.bAmmoCap   = 30;
    g_ObjR.bListPri   = 2;
    g_ObjR.uSortKey   = 21;
    g_ItemL.pObj = &g_ObjL;
    g_ItemL.nAmmo = 1;
    g_ItemR.pObj = &g_ObjR;
    g_ItemR.nAmmo = 1;
    g_Unit.pHandL = &g_ItemL;
    g_Unit.pHandR = &g_ItemR;

    /* Distinct per-mode values so a wrong table or a wrong index is visible. */
    for (i32 i = 0; i < 12; ++i) {
        g_anCost[i]    = 25 + i;
        g_anEnabled[i] = 700 + i;
    }
    g_nActionMode   = 0;
    g_bActionLatch  = 3;
    g_pSelWeapon    = AI_SENTINEL;
    g_bPathOk       = true;

    g_pRandScript    = pScript;
    g_nRandLen       = nLen;
    g_nRandIdx       = 0;
    g_nRandCalls     = 0;
    g_nRandOverflow  = 0;
    g_nPathCalls     = 0;
    g_nFillCalls     = 0;
    g_nPushCalls     = 0;
    g_nFaceCalls     = 0;
    g_szPushed[0]    = 0;
    g_nRecordPushes  = 0;
    memset(g_aRecordPushes, 0, sizeof(g_aRecordPushes));

    memset(&g_Ctx, 0, sizeof(g_Ctx));
    g_Ctx.pfnRandU16        = Ai_Rand;
    g_Ctx.pfnPathToCell     = Ai_Path;
    g_Ctx.pfnFillActionCosts = Ai_FillCosts;
    g_Ctx.pfnPushStatus     = Ai_Push;
    g_Ctx.pfnPushTextRecord = Ai_PushRecord;
    g_Ctx.pPushTextUser     = NULL;
    g_Ctx.pfnFaceToward     = Ai_Face;
    g_Ctx.pfnLookupCell     = Ai_Lookup;
    g_Ctx.pState            = &g_State;
    g_Ctx.pGeom             = &g_Geom;
    g_Ctx.ppSelWeapon       = &g_pSelWeapon;
    g_Ctx.pnActionMode      = &g_nActionMode;
    g_Ctx.anActionCost      = g_anCost;
    g_Ctx.anActionEnabled   = g_anEnabled;
    g_Ctx.pbActionLatch     = &g_bActionLatch;
    g_Ctx.pUser             = NULL;
}

/* The first queue entry the burst wrote, or NULL. Mode 1 makes one shot, which is a
 * command-14 entry followed by a command-15 entry and then a command-10. */
static const TactQueueEntry *AiFirstShot(void)
{
    if (g_State.nQueueLen <= 0) {
        return NULL;
    }
    return &g_State.aQueue[0];
}

static i32 AiBurstCount(void)
{
    i32 n = 0;
    for (i32 i = 0; i < g_State.nQueueLen; ++i) {
        if (g_State.aQueue[i].nCmd == TACTCMD_FIRE_SHOT) {
            n += 1;
        }
    }
    return n;
}

static void TestLargeUnitDoesNothing(void)
{
    static const u32 aScript[] = { 0 };
    printf("EnemyAi_TryFireOrWander: a large unit does nothing at all\n");

    AiReset(aScript, 1);
    g_Unit.nUnitType = 4;   /* byte_A7B80[4].large == 1 */
    CheckEqI32(g_aUnitTypeIsLarge[4], 1, "  type 4 is large");

    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);

    CheckEqI32(g_nRandCalls, 0, "  no RNG draw");
    Check(g_pSelWeapon == AI_SENTINEL, "  Map.pSelWeapon is not even cleared");
    CheckEqI32(g_State.bAiActionActive, 0, "  the AI-action flag is not raised");
    CheckEqI32(g_nPushCalls, 0, "  no status message");
    CheckEqI32(g_State.nQueueLen, 0, "  nothing queued");
    CheckEqI32(g_bActionLatch, 3, "  the action latch is untouched");
}

static void TestAiPairRaisedOnBothPaths(void)
{
    /* roll 1 -> wander. The pair is raised at 0x4AC04/0x4AC1C BEFORE the choice. */
    static const u32 aScript[] = { 1, 13, 13 };
    printf("The AI-action pair is raised before the path is chosen\n");

    AiReset(aScript, 3);
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);

    Check(g_State.pAiActionUnit == &g_Unit, "  the acting unit is stored on the wander path");
    CheckEqI32(g_State.bAiActionActive, 1, "  and the flag is raised");
    Check(g_pSelWeapon == &g_ItemL,
          "  the weapon pick still ran even though the unit wandered");
    CheckEqI32(g_nPathCalls, 1, "  the wander path ran");
    CheckEqI32(g_nFillCalls, 0, "  the action costs were not refilled");
}

static void TestWeaponPickRules(void)
{
    /* roll 0, so the weapon alone decides the path. The fire script carries three
     * bursts' worth of draws because the capacity-255 case runs three of them; the
     * one-burst cases never reach the tail. */
    static const u32 aScriptFire[]   = { 0, 4, 0, 1, 4, 0, 1, 4, 0, 1 };
    static const u32 aScriptWander[] = { 0, 13, 13 };

    printf("The one-handed weapon test (0x4ac24..0x4ac90)\n");

    /* Both hands two-handed -> wander. */
    AiReset(aScriptWander, 3);
    g_ObjL.bTwoHanded = 1;
    g_ObjR.bTwoHanded = 1;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nPathCalls, 1, "  two-handed on both hands sends it to wander");
    Check(g_pSelWeapon == NULL, "  and pSelWeapon stays cleared");

    /* Empty magazine, finite capacity -> unusable. */
    AiReset(aScriptWander, 3);
    g_ItemL.nAmmo = 0;
    g_ItemR.nAmmo = 0;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nPathCalls, 1, "  an empty finite magazine is unusable");

    /* Empty magazine, capacity 255 -> usable. */
    AiReset(aScriptFire, 10);
    g_ItemL.nAmmo = 0;
    g_ItemR.nAmmo = 0;
    g_ObjL.bAmmoCap = 255;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nPathCalls, 0, "  capacity 255 makes an empty weapon usable");
    Check(g_pSelWeapon == &g_ItemL, "  and the left hand wins");

    /* Left unusable, right usable -> the right hand is the one stored. */
    AiReset(aScriptFire, 10);
    g_ObjL.bTwoHanded = 1;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    Check(g_pSelWeapon == &g_ItemR, "  a two-handed left hand falls through to the right");

    /* Left usable, right two-handed -> left wins. */
    AiReset(aScriptFire, 10);
    g_ObjR.bTwoHanded = 1;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    Check(g_pSelWeapon == &g_ItemL, "  a usable left hand wins before the right is tested");
}

static void TestShotCount(void)
{
    /* Each burst iteration draws x, y, layer. x draw 4 -> 7+4-2 = 9 (a different
     * column), y draw 0 -> 5, layer draw 1. */
    static const u32 aScript[] = {
        0, 4, 0, 1, 4, 0, 1, 4, 0, 1
    };

    printf("The burst shot count (0x4acc0..0x4acd4)\n");

    /* Unlimited capacity: three bursts whatever the magazine says. */
    AiReset(aScript, 10);
    g_ObjL.bAmmoCap = 255;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(AiBurstCount(), 3, "  capacity 255 gives 3 bursts with 1 round loaded");

    /* Finite magazine of 1: one burst. */
    AiReset(aScript, 10);
    g_ItemL.nAmmo = 1;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(AiBurstCount(), 1, "  a 1-round magazine gives 1 burst");

    /* Finite magazine of 10: capped at 3. */
    AiReset(aScript, 10);
    g_ItemL.nAmmo = 10;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(AiBurstCount(), 3, "  a 10-round magazine is capped at 3");
    /* 100 TU against slot 1's 26: the same enqueue that refuses in TestTuRefusalStillRunsTheTail
     * takes the burst here and the call sites push nothing. */
    CheckEqI32(g_nRecordPushes, 0, "  an affordable burst pushes no TEXTS record");
}

static void TestSameColumnRedraw(void)
{
    /* Draw 1 lands in the shooter's own column (x draw 2 -> 7+2-2 = 7) and the shooter
     * is on row 7, not row 0. Draw 2 lands in column 9. */
    static const u32 aScript[] = { 0, 2, 0, 1, 4, 0, 1 };

    printf("A same-column draw redraws WITHOUT spending a shot (0x4ada4)\n");

    AiReset(aScript, 7);
    g_ItemL.nAmmo = 1;      /* one shot only */
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);

    CheckEqI32(AiBurstCount(), 1, "  the single shot is still fired after the rejected draw");
    CheckEqI32(g_nRandCalls, 7, "  the rejected draw cost a full redraw (1 roll + 3 + 3)");
    CheckEqI32(g_nRandOverflow, 0, "  and no draw ran past the script");

    const TactQueueEntry *pShot = AiFirstShot();
    Check(pShot != NULL, "  a shot entry exists");
    if (pShot != NULL) {
        CheckEqI32(pShot->nX, 9, "  the target is the SECOND draw's column");
        CheckEqI32(pShot->nY, 5, "  and its row");
        CheckEqI32(pShot->nZ, 1, "  and its layer");
    }
}

static void TestRowZeroFiresAtOwnCell(void)
{
    /* Shooter on row 0; the draw lands in its own column. 0x4ad90 replaces the drawn y
     * with the shooter's own row (0), and 0x4ada4 then does NOT redraw. */
    static const u32 aScript[] = { 0, 2, 3, 1 };

    printf("A shooter on row 0 fires at its own cell (0x4ada4 fall-through)\n");

    AiReset(aScript, 4);
    g_Unit.pCell = Cell(7, 0, 2);
    g_ItemL.nAmmo = 1;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);

    CheckEqI32(g_nRandCalls, 4, "  no redraw happened");
    CheckEqI32(AiBurstCount(), 1, "  one burst fired");

    const TactQueueEntry *pShot = AiFirstShot();
    if (pShot != NULL) {
        CheckEqI32(pShot->nX, 7, "  the target column is the shooter's own");
        CheckEqI32(pShot->nY, 0, "  the drawn row 3 was replaced by the shooter's row 0");
        CheckEqI32(pShot->nZ, 1, "  the drawn layer is kept");
    } else {
        Check(false, "  a shot entry exists");
    }
}

static void TestCostTablesAndLatch(void)
{
    static const u32 aScriptFire[]   = { 0, 4, 0, 1 };
    static const u32 aScriptWander[] = { 1, 13, 13 };

    printf("The action-cost tables, the latch and the queue kick\n");

    AiReset(aScriptFire, 4);
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nFillCalls, 1, "  the fire path refills the action costs once");
    CheckEqI32(g_nActionMode, 1, "  and forces action mode 1");

    const TactQueueEntry *pShot = AiFirstShot();
    if (pShot != NULL) {
        CheckEqI32(pShot->nTuCost, g_anCost[1], "  the TU cost is anActionCost[1] (Map+0x4E4C)");
        CheckEqI32(pShot->nArgDword, g_anEnabled[1],
                   "  the argument is anActionEnabled[1] (Map+0x4E7C)");
    } else {
        Check(false, "  a shot entry exists");
        Check(false, "  the argument is anActionEnabled[1]");
    }
    CheckEqI32(g_bActionLatch, 0, "  the fire path clears the action latch");
    CheckEqI32(g_State.bQueueActive, 1, "  and kicks the queue");

    AiReset(aScriptWander, 3);
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nFillCalls, 0, "  the wander path refills nothing");
    CheckEqI32(g_bActionLatch, 3, "  and leaves the action latch alone");
    CheckEqI32(g_State.bQueueActive, 0, "  and does not kick the queue");
}

static void TestDropHandsByType(void)
{
    static const u32 aScript[] = { 1, 13, 13 };

    printf("Only nUnitType <= 3 drops both hands (0x4ae3c)\n");

    AiReset(aScript, 3);
    /* A floor one layer below the shooter: the drop target is that layer. Set after
     * AiReset, which wipes the whole grid. */
    Cell(7, 7, 1)->pFloorSpr = &g_FloorSpr;
    g_Unit.nUnitType = 0;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);

    Check(g_Unit.pHandL == NULL, "  type 0 clears the left hand");
    Check(g_Unit.pHandR == NULL, "  and the right hand");
    Check(Cell(7, 7, 1)->pItemList == &g_ItemR,
          "  both items land in the floor cell one layer down (higher priority first)");
    Check(g_ItemR.pNext == &g_ItemL, "  the left item follows it");
    Check(Cell(7, 7, 2)->pItemList == NULL, "  nothing stays in the shooter's own cell");

    /* Type 9 is above the drop threshold and is not large. */
    Cell(7, 7, 1)->pItemList = NULL;
    AiReset(aScript, 3);
    g_Unit.nUnitType = 9;
    CheckEqI32(g_aUnitTypeIsLarge[9], 0, "  type 9 is small enough to act");
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    Check(g_Unit.pHandL == &g_ItemL, "  type 9 keeps its left hand");
    Check(g_Unit.pHandR == &g_ItemR, "  and its right hand");
    Check(Cell(7, 7, 1)->pItemList == NULL, "  and drops nothing");
}

static void TestFloorSearchOnlyForFlag0Clear(void)
{
    static const u32 aScript[] = { 1, 13, 13 };

    printf("Only flag0-clear types search down for a floor (0x4af88)\n");

    /* No floors anywhere, so the two readings differ by the layer the target ends on. */
    AiReset(aScript, 3);
    g_Unit.nUnitType = 3;   /* flag0 = 1 */
    CheckEqI32(g_aUnitTypeFlag0[3], 1, "  type 3 has flag0 set");
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nPathLayer, 2, "  flag0 set: the candidate stays on the shooter's layer");
    CheckEqI32(g_nPathX, 10, "  at the drawn column");
    CheckEqI32(g_nPathY, 10, "  and row");

    AiReset(aScript, 3);
    g_Unit.nUnitType = 0;   /* flag0 = 0 */
    CheckEqI32(g_aUnitTypeFlag0[0], 0, "  type 0 has flag0 clear");
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nPathLayer, 0, "  flag0 clear: the walk reaches layer 0");

    /* With a floor at layer 1 the walk stops there instead. */
    AiReset(aScript, 3);
    Cell(10, 10, 1)->pFloorSpr = &g_FloorSpr;
    g_Unit.nUnitType = 0;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nPathLayer, 1, "  and stops at the first cell that has a floor");
    Cell(10, 10, 1)->pFloorSpr = NULL;
}

static void TestWanderRetriesOwnCellAndOccupant(void)
{
    /* Type 3 so no floor walk moves the candidate off the shooter's own cell. */
    static const u32 aScript[] = { 1, 10, 10, 11, 11, 13, 13 };

    printf("The wander draw retries its own cell and an occupied cell\n");

    AiReset(aScript, 7);
    g_Unit.nUnitType = 3;
    Cell(8, 8, 2)->pUnit = &g_Occupant;   /* bStatus 0 -> an occupant */
    Check(MapCell_HasOccupant(Cell(8, 8, 2)), "  the fixture cell really is occupied");

    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);

    CheckEqI32(g_nPathCalls, 1, "  only the third draw reaches the path call");
    CheckEqI32(g_nRandCalls, 7, "  the two rejected draws cost a full redraw each");
    CheckEqI32(g_nPathX, 10, "  the accepted target is the third draw");
    CheckEqI32(g_nPathY, 10, "  at (10,10)");
    CheckEqI32(g_nPathLayer, 2, "  on the shooter's layer");

    Cell(8, 8, 2)->pUnit = NULL;
}

static void TestPathResultWritesTuAndFlag(void)
{
    static const u32 aScript[] = { 1, 13, 13 };

    printf("Unit_PathToCell's result decides the TU and the flag (0x4b00c)\n");

    AiReset(aScript, 3);
    g_bPathOk = true;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_Unit.uTu, 100, "  a successful path leaves the TU alone");
    CheckEqI32(g_State.bAiActionActive, 1, "  and leaves the flag up for the walker to clear");
    CheckEqI32(g_nPathFlag, 1, "  the call passes bFlag 1");

    AiReset(aScript, 3);
    g_bPathOk = false;
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_Unit.uTu, 0, "  a failed path leaves the unit with no TU");
    CheckEqI32(g_State.bAiActionActive, 0, "  and clears the flag itself");
}

static void TestStatusMessageOnBothPaths(void)
{
    static const u32 aScriptFire[]   = { 0, 4, 0, 1 };
    static const u32 aScriptWander[] = { 1, 13, 13 };

    printf("Both paths end by pushing the unit's name (0x4b028)\n");

    AiReset(aScriptFire, 4);
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nPushCalls, 1, "  the fire path pushes once");
    Check(strcmp(g_szPushed, "Sectoid") == 0, "  the message is the name and nothing else");

    AiReset(aScriptWander, 3);
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);
    CheckEqI32(g_nPushCalls, 1, "  the wander path pushes once");
    Check(strcmp(g_szPushed, "Sectoid") == 0, "  with the same text");
}

static void TestTuRefusalStillRunsTheTail(void)
{
    static const u32 aScript[] = { 0, 4, 0, 1 };

    printf("A burst the unit cannot afford still runs the latch, the kick and the message\n");

    AiReset(aScript, 4);
    g_anCost[1] = 500;   /* more than the unit's 100 TU */
    EnemyAi_TryFireOrWander(&g_Unit, &g_Ctx);

    CheckEqI32(g_State.nQueueLen, 0, "  no burst was queued");
    CheckEqI32(g_bActionLatch, 0, "  the latch is still cleared");
    /* TactState_KickQueue @ 0x4f334 arms the queue only when nQueueLen is non-zero,
     * so the kick the AI always performs has no effect here. The non-empty case is
     * covered in TestCostTablesAndLatch. */
    CheckEqI32(g_State.bQueueActive, 0, "  kicking an empty queue leaves it inactive");
    CheckEqI32(g_nPushCalls, 1, "  and the name is still pushed");
    CheckEqI32(g_nFaceCalls, 0, "  the enqueue refused before facing the target");
    /* 0x4AE08 hands the cost table's slot 1 straight to the callee and has no gate of its own, so
     * the refusal lands on 0x4B0D8, which pushes aTextRec1_OutOfTime (record 1) with bForce 0.
     * The callee does that push in the original; the port does it at the call site. */
    CheckEqI32(g_nRecordPushes, 1, "  the refused burst pushes exactly one TEXTS record");
    CheckEqI32(g_aRecordPushes[0], PUFO_TXT_OUT_OF_TU, "  and it is record 1, the out-of-TU record");
}

/* --- Tact_EnemyAiTick @ 0x4cbe0 ---------------------------------------------------- */

/* The tick reaches further than the panic AI does: it selects units, ends the enemy turn,
 * walks the RMP node list and drives the queue. So the fixture wires a real TactSelectCtx,
 * a real TactCtx272 and a real RmpData, and counts every seam. */
static TactCtx272     g_Tact;
static Soldier        g_aEnemy[4];
static Soldier       *g_apEnemy[4];
static u8             g_nEnemy;
static Soldier        g_Target;
static Soldier        g_Occupant;
static Soldier        g_Xcom;
static TactItem       g_ItemBelt;
static TactItem       g_ItemMelee;
static TactItem       g_ItemAction13;
static ObjectRecFull  g_ObjBelt;
static ObjectRecFull  g_ObjMelee;
static ObjectRecFull  g_ObjAction13;
static Mc1Rec         g_CeilSpr;
static Soldier       *g_pSelUnit;
static TactCursor     g_Cursor;
static MapCursorPos   g_CursorPos;
static StatusStack    g_Stack;
static StatusHooks    g_Hooks;
static TactSelectCtx  g_Select;
static RmpData        g_Rmp;
static RmpNode        g_aRmpNodes[16];
static MapCell       *g_apMapTargets[8];
static i32            g_nMultiTargetCount;
static i32            g_nBurstShots;
static i32            g_nMeleeX, g_nMeleeY, g_nMeleeLayer;
static TurnHooks      g_TurnHooks;
static EnemyAiTickCtx g_Tick;
static char           g_aRankNames[9][PUFO_RANK_NAME_STRIDE];

static i32     g_nVisCalls;
static Soldier *g_pVisUnit;
static i32     g_nTurnCalls;
static Soldier *g_pTurnUnit;
static u8      g_nTurnFacing;
static i32     g_nAfterStepCalls;
static i32     g_nInputCalls;
static i32     g_nFocusCalls;
static i32     g_nPathSeq;
static i32     g_aPathX[64];
static i32     g_aPathY[64];

static bool Tick_NotBusy(void *pUser) { (void)pUser; return false; }
static bool Tick_NoRepaint(void *pUser) { (void)pUser; return false; }
static void Tick_NoFrame(void *pUser) { (void)pUser; }

static void Tick_Vis(Soldier *pUnit, void *pUser)
{
    (void)pUser;
    g_nVisCalls += 1;
    g_pVisUnit = pUnit;
}

static void Tick_Turn(Soldier *pUnit, u8 nFacing, void *pUser)
{
    (void)pUser;
    g_nTurnCalls += 1;
    g_pTurnUnit = pUnit;
    g_nTurnFacing = nFacing;
}

static void Tick_AfterStep(TactCtx272 *pCtx, void *pUser)
{
    (void)pCtx;
    (void)pUser;
    g_nAfterStepCalls += 1;
}

static void Tick_SetInput(bool bEnabled, void *pUser)
{
    (void)bEnabled;
    (void)pUser;
    g_nInputCalls += 1;
}

static void Tick_Focus(void *pUser) { (void)pUser; g_nFocusCalls += 1; }

static u8 Tick_ClipCap(u16 nAmmoTypeId, void *pUser)
{
    (void)nAmmoTypeId;
    (void)pUser;
    return 30;
}

/* Records the coordinates of every path request, in order, so a test can prove the
 * RMP walk's visit order rather than just its count. */
static bool Ai_PathSeq(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser)
{
    (void)nLayer;
    (void)pUser;
    if (g_nPathSeq < 64) {
        g_aPathX[g_nPathSeq] = nX;
        g_aPathY[g_nPathSeq] = nY;
        g_nPathSeq += 1;
    }
    g_nPathCalls += 1;
    g_nPathFlag = bFlag;
    return g_bPathOk;
}

static void TickReset(const u32 *pScript, i32 nLen)
{
    i32 i;

    AiReset(pScript, nLen);

    memset(&g_Tact, 0, sizeof(g_Tact));
    memset(g_aEnemy, 0, sizeof(g_aEnemy));
    memset(&g_Target, 0, sizeof(g_Target));
    memset(&g_Occupant, 0, sizeof(g_Occupant));
    memset(&g_Xcom, 0, sizeof(g_Xcom));
    memset(&g_Rmp, 0, sizeof(g_Rmp));
    memset(g_aRmpNodes, 0, sizeof(g_aRmpNodes));
    memset(g_apMapTargets, 0, sizeof(g_apMapTargets));
    memset(&g_Cursor, 0, sizeof(g_Cursor));
    memset(&g_Stack, 0, sizeof(g_Stack));
    memset(&g_CeilSpr, 0, sizeof(g_CeilSpr));
    memset(&g_ItemBelt, 0, sizeof(g_ItemBelt));
    memset(&g_ItemMelee, 0, sizeof(g_ItemMelee));
    memset(&g_ItemAction13, 0, sizeof(g_ItemAction13));
    memset(&g_ObjBelt, 0, sizeof(g_ObjBelt));
    memset(&g_ObjMelee, 0, sizeof(g_ObjMelee));
    memset(&g_ObjAction13, 0, sizeof(g_ObjAction13));
    for (i = 0; i < 9; ++i) {
        snprintf(g_aRankNames[i], PUFO_RANK_NAME_STRIDE, "Rank%d", i);
    }

    /* Sectoid-shaped enemies: small, flag0 clear, flag3 clear, morale high enough not
     * to panic, and enough TU to act. */
    for (i = 0; i < 4; ++i) {
        g_apEnemy[i] = &g_aEnemy[i];
        g_aEnemy[i].pCell = Cell(7, 7, 2);
        g_aEnemy[i].uTu = 20;
        g_aEnemy[i].nUnitType = 9;
        g_aEnemy[i].bMorale = 100;
        snprintf(g_aEnemy[i].szName, sizeof(g_aEnemy[i].szName), "Alien%d", i);
    }
    g_aEnemy[0].pHandL = &g_ItemL;
    g_aEnemy[0].pHandR = &g_ItemR;
    g_nEnemy = 1;

    /* The multi-target the AI aims at, standing two cells east of the actor. */
    g_Target.pCell = Cell(9, 7, 2);
    g_Target.nUnitType = 9;
    g_Target.nMultiTarget = 5;   /* role 5: the predicate accepts every type */
    Cell(9, 7, 2)->pUnit = &g_Target;
    g_aEnemy[0].nMultiTarget = 1;
    g_aEnemy[0].multiOrAnim.apMultiTargets[0] = &g_Target;

    /* A throwable belt item: object category 3 is what the throw requires. */
    g_ObjBelt.nCategory = PUFO_ENEMYAI_THROW_CATEGORY;
    g_ItemBelt.pObj = &g_ObjBelt;
    g_ItemBelt.nAmmo = 4;
    g_aEnemy[0].apBelt[0] = &g_ItemBelt;

    g_ObjMelee.bTwoHanded = 0;
    g_ObjMelee.bAmmoCap = 255;
    g_ItemMelee.pObj = &g_ObjMelee;
    g_ObjAction13.bTwoHanded = 0;
    g_ObjAction13.bAmmoCap = 255;
    g_ItemAction13.pObj = &g_ObjAction13;

    g_Xcom.bStatus = 0;
    g_Xcom.nUnitType = 0;

    g_pSelUnit = NULL;
    g_CursorPos.pCursor = &g_Cursor;
    g_CursorPos.geom = g_Geom;
    memset(&g_Select, 0, sizeof(g_Select));
    g_Select.ppSelUnit = &g_pSelUnit;
    g_Select.pCursorPos = &g_CursorPos;
    g_Select.pfnLookup = Ai_Lookup;
    g_Select.pStatus = &g_Stack;
    g_Hooks.pfnIsBusy = Tick_NotBusy;
    g_Hooks.pfnShouldRender = Tick_NoRepaint;
    g_Hooks.pfnRender = Tick_NoFrame;
    g_Hooks.pUser = NULL;
    g_Select.pStatusHooks = &g_Hooks;
    g_Select.panRankLow = &g_aRankNames[0][0];
    g_Select.panRankOther = &g_aRankNames[0][0];

    memset(&g_TurnHooks, 0, sizeof(g_TurnHooks));
    g_TurnHooks.pfnSetPlayerInput = Tick_SetInput;
    g_TurnHooks.pfnFocusSelected = Tick_Focus;
    g_TurnHooks.pUser = NULL;

    g_nMultiTargetCount = 0;
    g_nBurstShots = 0;
    g_nMeleeX = -1;
    g_nMeleeY = -1;
    g_nMeleeLayer = -1;
    g_nVisCalls = 0;
    g_pVisUnit = NULL;
    g_nTurnCalls = 0;
    g_pTurnUnit = NULL;
    g_nTurnFacing = 0xFF;
    g_nAfterStepCalls = 0;
    g_nInputCalls = 0;
    g_nFocusCalls = 0;
    g_nPathSeq = 0;

    memset(&g_Tick, 0, sizeof(g_Tick));
    g_Tick.pfnRandU16             = Ai_Rand;
    g_Tick.pfnPathToCell          = Ai_PathSeq;
    g_Tick.pfnFillActionCosts     = Ai_FillCosts;
    g_Tick.pfnUpdateVisibility    = Tick_Vis;
    g_Tick.pfnEnqueueTurnAndKick  = Tick_Turn;
    g_Tick.pfnAfterEnemyStep      = Tick_AfterStep;
    g_Tick.pfnClipAmmoCap         = Tick_ClipCap;
    g_Tick.pfnLookup              = Ai_Lookup;
    g_Tick.geom                   = g_Geom;
    g_Tick.pQueue                 = &g_State;
    g_Tick.pSelect                = &g_Select;
    g_Tick.pTurnHooks             = &g_TurnHooks;
    g_Tick.apEnemyUnits           = g_apEnemy;
    g_Tick.pnEnemyCount           = &g_nEnemy;
    g_Tick.ppSelUnit              = &g_pSelUnit;
    g_Tick.pFirstXcom             = &g_Xcom;
    g_Tick.ppSelWeapon            = &g_pSelWeapon;
    g_Tick.pnActionMode           = &g_nActionMode;
    g_Tick.pnMultiTargetCount     = &g_nMultiTargetCount;
    g_Tick.apMultiTargets         = g_apMapTargets;
    g_Tick.pnBurstShots           = &g_nBurstShots;
    g_Tick.pnMeleeCellX           = &g_nMeleeX;
    g_Tick.pnMeleeCellY           = &g_nMeleeY;
    g_Tick.pnMeleeCellLayer       = &g_nMeleeLayer;
    g_Tick.anActionCost           = g_anCost;
    g_Tick.anActionEnabled        = g_anEnabled;
    g_Tick.pItemMelee             = &g_ItemMelee;
    g_Tick.pItemAction13          = &g_ItemAction13;
    g_Tick.pRmp                   = &g_Rmp;
    g_Tick.pActCtx                = &g_Ctx;
    g_Tick.pfnPushTextRecord      = Ai_PushRecord;
    g_Tick.pPushTextUser          = NULL;
    g_Tick.pUser                  = NULL;
}

/* The first command-18 entry in the queue, or NULL. */
static const TactQueueEntry *AiFirstAction18(void)
{
    for (i32 i = 0; i < g_State.nQueueLen; ++i) {
        if (g_State.aQueue[i].nCmd == TACTCMD_ACTION18) {
            return &g_State.aQueue[i];
        }
    }
    return NULL;
}

static const TactQueueEntry *AiFirstShotEntry(void)
{
    for (i32 i = 0; i < g_State.nQueueLen; ++i) {
        if (g_State.aQueue[i].nCmd == TACTCMD_FIRE_SHOT) {
            return &g_State.aQueue[i];
        }
    }
    return NULL;
}

static const TactQueueEntry *AiFirstThrow(void)
{
    for (i32 i = 0; i < g_State.nQueueLen; ++i) {
        if (g_State.aQueue[i].nCmd == TACTCMD_THROW) {
            return &g_State.aQueue[i];
        }
    }
    return NULL;
}

static void TestNoEnemiesRunsTail(void)
{
    printf("Tact_EnemyAiTick with an empty roster still runs the tail\n");

    TickReset(NULL, 0);   /* any RNG draw at all would fail the test */
    g_nEnemy = 0;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    CheckEqI32(g_nAfterStepCalls, 1, "  Tact_EndEnemyTurn ran");
    CheckEqI32(g_nInputCalls, 1, "  and it re-enabled player input");
    /* The tail writes phase 3, then Tact_EndEnemyTurn @ 0x4cb74 writes 0 back. */
    CheckEqI32(g_Tact.nPhase, 0, "  the phase ends at 0 because the turn ended");
    CheckEqI32(g_Tact.bAiFocusSaved, 0, "  no focus was saved");
}

static void TestTickUnitGates(void)
{
    printf("The per-unit gates: tick limit, bAiDone and the TU floor\n");

    TickReset(NULL, 0);
    g_nEnemy = 3;
    g_aEnemy[0].nAiTick = PUFO_ENEMYAI_TICK_MAX + 1;   /* past the limit */
    g_aEnemy[0].uTu = 20;
    g_aEnemy[1].nAiTick = 5;
    g_aEnemy[1].bAiDone = 1;
    g_aEnemy[1].uTu = 20;
    g_aEnemy[2].nAiTick = 0;
    g_aEnemy[2].uTu = PUFO_ENEMYAI_TU_MIN - 1;         /* not enough TU */
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    /* 0x4CC34: a unit past the limit is skipped WITHOUT ++nAiTick, so it never comes
     * back. The other two are advanced. */
    CheckEqI32(g_aEnemy[0].nAiTick, PUFO_ENEMYAI_TICK_MAX + 1,
               "  the over-limit unit is not advanced");
    CheckEqI32(g_aEnemy[1].nAiTick, 6, "  the done unit is still advanced");
    CheckEqI32(g_aEnemy[2].nAiTick, 1, "  the short-TU unit is advanced");
    CheckEqI32(g_aEnemy[2].bAiDone, 1, "  and marked done for having too little TU");
    CheckEqI32(g_aEnemy[0].bAiDone, 0, "  the over-limit unit is not marked done");
    CheckEqI32(g_nVisCalls, 0, "  nobody acted, so nobody refreshed visibility");
    CheckEqI32(g_nAfterStepCalls, 1, "  so the enemy turn ended");
}

static void TestActingUnitVisibilityAndTrack(void)
{
    static const u32 aScript[] = { 0, 0, 0, 0 };

    printf("The acting unit refreshes visibility and the camera link\n");

    TickReset(aScript, 4);
    g_aEnemy[0].nMultiTarget = 0;   /* straight to the fallback: rand(3) then rand(8) */
    g_Tact.pTrackUnit = &g_aEnemy[0];
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    CheckEqI32(g_nVisCalls, 1, "  Unit_UpdateVisibility ran for the acting unit");
    Check(g_pVisUnit == &g_aEnemy[0], "  for the right unit");
    Check(g_Tact.pTrackUnit == &g_aEnemy[0],
          "  pTrackUnit is kept when it already points at the actor");

    TickReset(aScript + 2, 2);
    g_aEnemy[0].nMultiTarget = 0;
    g_Tact.pTrackUnit = &g_Target;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    Check(g_Tact.pTrackUnit == NULL,
          "  pTrackUnit is cleared when it points at another unit");
}

static void TestLowMoraleRunsThePanicAi(void)
{
    static const u32 aPanic[]  = { 1, 13, 13 };
    static const u32 aSteady[] = { 0, 0 };

    printf("bMorale < 20 with nUnitType > 3 hands the unit to the panic AI\n");

    TickReset(aPanic, 3);
    g_aEnemy[0].bMorale = PUFO_ENEMYAI_MORALE_MAX - 1;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    CheckEqI32(g_nPushCalls, 1, "  the panic AI pushed its status message");
    Check(strcmp(g_szPushed, "Alien0") == 0, "  the message is the unit name");
    CheckEqI32(g_nPathCalls, 1, "  and the unit wandered");
    CheckEqI32(g_nPathFlag, 1, "  the wander path asks for the flag-1 path");
    CheckEqI32(g_aEnemy[0].bAiDone, 1, "  the unit is marked done");
    CheckEqI32(g_Tact.bAiFocusSaved, 1, "  and the focus is saved");
    CheckEqI32(g_nAfterStepCalls, 0, "  so the enemy turn did not end");
    CheckEqI32(g_nTurnCalls, 0, "  the fallback move never ran");

    /* The boundary: morale exactly 20 is not panic. */
    TickReset(aSteady, 2);
    g_aEnemy[0].bMorale = PUFO_ENEMYAI_MORALE_MAX;
    g_aEnemy[0].nMultiTarget = 0;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    CheckEqI32(g_nPushCalls, 0, "  morale 20 does not panic");
    CheckEqI32(g_nTurnCalls, 1, "  and the unit takes the fallback turn instead");
}

static void TestFireAffordabilityGate(void)
{
    /* Draw order: the multi-target index, the 1-in-5 accept roll, then the throw branch's
     * own multi-target index and rand(3), and only then the fallback roll. */
    static const u32 aRefused[] = { 0, 1, 0, 1, 0, 0 };
    static const u32 aPaid[]    = { 0, 1, 0, 1 };
    const TactQueueEntry *pShot;

    printf("The fire gate is uTu >= cost * 2/3 + 8.0, charged at (int)(cost * 2/3)\n");

    /* anActionCost[1] is 26, so the gate is 26 * 2/3 + 8 = 25.333... */
    TickReset(aRefused, 6);
    g_aEnemy[0].uTu = 25;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    CheckEqI32(AiBurstCount(), 0, "  25 TU does not clear a 26-cost mode");
    CheckEqI32(g_nTurnCalls, 1, "  and the unit falls back to the fallback move");
    /* The site gate at 0x4d448 stops it here, so the callee at 0x4B0D8 never runs: this refusal is
     * silent in the original too, unlike the panic AI's 0x4AE08 or this tick's 0x4D844. */
    CheckEqI32(g_nRecordPushes, 0, "  a gate refusal at the fire site pushes no TEXTS record");

    TickReset(aPaid, 4);
    g_aEnemy[0].uTu = 26;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    pShot = AiFirstShotEntry();
    CheckEqI32(AiBurstCount(), 1, "  26 TU clears it");
    Check(pShot != NULL, "  a shot entry exists");
    if (pShot != NULL) {
        CheckEqI32(pShot->nTuCost, 17, "  the charge is (int)(26 * 2/3), not the table cost");
        CheckEqI32(pShot->nArgDword, g_anEnabled[PUFO_ACTION_MODE_SNAP],
                   "  the argument is anActionEnabled[mode]");
        CheckEqI32(pShot->nX, 9, "  the shot aims at the multi-target's cell");
        CheckEqI32(pShot->nY, 7, "  at its row");
        CheckEqI32(pShot->nZ, 2, "  at its layer");
    }
    CheckEqI32(g_nActionMode, PUFO_ACTION_MODE_SNAP, "  a non-burst weapon uses mode 1");
    CheckEqI32(g_nTurnCalls, 0, "  no fallback once the shot was queued");
}

static void TestThrowPath(void)
{
    static const u32 aScript[] = { 0, 1, 0, 0 };
    const TactQueueEntry *pThrow;

    printf("The throw needs roll 0, range, a category-3 belt item and type > 3\n");

    TickReset(aScript, 4);
    g_aEnemy[0].uTu = 40;   /* the throw is charged anActionCost[11], which is 36 */
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    Check(g_aEnemy[0].apBelt[0] == NULL, "  the belt slot is emptied");
    Check(g_aEnemy[0].pHandR == &g_ItemBelt, "  the item moves into the right hand");
    Check(g_pSelWeapon == &g_ItemBelt, "  and into Map.pSelWeapon");
    CheckEqI32(g_ItemBelt.bUnk7, 0, "  item +7 is cleared");
    CheckEqI32(g_ItemBelt.bUnk8, 1, "  item +8 is raised");
    CheckEqI32(g_nActionMode, PUFO_ACTION_MODE_MENU, "  action mode 11 is the throw slot");
    pThrow = AiFirstThrow();
    Check(pThrow != NULL, "  a command-17 entry was queued");
    if (pThrow != NULL) {
        CheckEqI32(pThrow->nTuCost, g_anCost[PUFO_ACTION_MODE_MENU],
                   "  its cost is anActionCost[11], the global the original loads by literal");
        CheckEqI32(pThrow->nArg, (u8)g_anEnabled[PUFO_ACTION_MODE_MENU],
                   "  its argument is anActionEnabled[11]");
        CheckEqI32(pThrow->nX, 9, "  at the multi-target's column");
    }
    CheckEqI32(g_aEnemy[0].bAiDone, 1, "  the thrower is marked done");

    CheckEqI32(g_nRecordPushes, 0, "  an affordable throw pushes no TEXTS record");

    /* The same roll, but the belt object is not category 3: no throw, and the unit fires. */
    TickReset(aScript, 4);
    g_ObjBelt.nCategory = 2;
    g_aEnemy[0].uTu = 26;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    Check(AiFirstThrow() == NULL, "  a category-2 belt item is not throwable");
    CheckEqI32(AiBurstCount(), 1, "  so the unit shoots instead");
    CheckEqI32(g_nRecordPushes, 0, "  and the fire path pushes none either -- its gate is upstream");
}

/* 0x4D844 is the tick's reachable refusal. The throw branch is chosen on the roll, the range, the
 * belt item and the unit type; the only TU test on the way is the unit gate `uTu >= 15` at 0x4cc1c,
 * which says nothing about slot 11's 36 TU. So a 20 TU thrower reaches 0x4B318, which pushes
 * record 1 and queues nothing -- while the hand swap, bAiFocusSaved and bAiDone still run, because
 * the caller never looks at the return value (0x4D848..0x4D858). */
static void TestThrowRefusalKeepsTheTail(void)
{
    static const u32 aScript[] = { 0, 1, 0, 0 };

    printf("A throw the unit cannot pay still swaps the hand and marks the unit done\n");

    TickReset(aScript, 4);
    g_aEnemy[0].uTu = 20;   /* enough to act, not enough for anActionCost[11] = 36 */
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    Check(AiFirstThrow() == NULL, "  no command-17 entry was queued");
    CheckEqI32(g_State.nQueueLen, 0, "  the queue stays empty");
    Check(g_aEnemy[0].apBelt[0] == NULL, "  the belt slot was still emptied");
    Check(g_aEnemy[0].pHandR == &g_ItemBelt, "  the item is still in the right hand");
    Check(g_pSelWeapon == &g_ItemBelt, "  and still in Map.pSelWeapon");
    CheckEqI32(g_aEnemy[0].bAiDone, 1, "  the thrower is still marked done");
    CheckEqI32(g_nRecordPushes, 1, "  the refusal pushes exactly one TEXTS record");
    CheckEqI32(g_aRecordPushes[0], PUFO_TXT_OUT_OF_TU, "  and it is record 1, the out-of-TU record");
}

static void TestMultiTargetMode7(void)
{
    static const u32 aFar[]  = { 0, 1, 0, 1 };
    static const u32 aNear[] = { 0, 1, 0, 1, 0, 0 };

    printf("A burst weapon at range >= 10 arms mode 7 and the Map multi-target block\n");

    /* anActionCost[7] is 32, so the gate is 32 * 2/3 + 8 = 29.333... */
    TickReset(aFar, 4);
    g_ObjL.uSortKey = PUFO_ENEMYAI_SORTKEY_MULTI;   /* AiReset rewrites it */
    g_Target.pCell = Cell(17, 7, 2);
    Cell(17, 7, 2)->pUnit = &g_Target;
    g_aEnemy[0].uTu = 30;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    CheckEqI32(g_nActionMode, PUFO_ACTION_MODE_MULTI, "  action mode 7");
    CheckEqI32(g_nMultiTargetCount, 1, "  Map.nMultiTargetCount is armed to 1");
    CheckEqI32(g_nBurstShots, 1, "  Map.nBurstShots is armed to 1");
    Check(g_apMapTargets[0] == Cell(17, 7, 2), "  Map.apMultiTargets[0] is the target cell");
    CheckEqI32(AiBurstCount(), 1, "  one shot, because the target list holds one cell");
    CheckEqI32(AiFirstShotEntry()->nTuCost, 21, "  charged at (int)(32 * 2/3)");

    /* The same weapon with the target only 8 cells away: no burst, fallback instead. */
    TickReset(aNear, 6);
    g_ObjL.uSortKey = PUFO_ENEMYAI_SORTKEY_MULTI;
    g_Target.pCell = Cell(15, 7, 2);
    Cell(15, 7, 2)->pUnit = &g_Target;
    g_aEnemy[0].uTu = 30;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    CheckEqI32(AiBurstCount(), 0, "  a target closer than 10 cells is not a burst target");
    CheckEqI32(g_nTurnCalls, 1, "  and the unit falls back");
    g_ObjL.uSortKey = 20;
}

static void TestMeleeNeighbourHandoff(void)
{
    static const u32 aScript[] = { 0, 0, 0 };
    const TactQueueEntry *pAct;

    printf("A usable multi-target hands the melee action to ANOTHER enemy (0x4d660)\n");

    TickReset(aScript, 3);   /* pick, accept roll, rand(2) for the mode */
    g_nEnemy = 2;
    g_aEnemy[1].bPsiActive = 1;
    g_aEnemy[1].uTu = 20;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    pAct = AiFirstAction18();
    Check(pAct != NULL, "  a command-18 entry was queued");
    if (pAct != NULL) {
        Check(pAct->pUnit == &g_aEnemy[1],
              "  the action belongs to the neighbour, not to the unit this tick picked");
        Check(pAct->pTarget == &g_Target, "  against the multi-target");
        Check(pAct->pWeapon == &g_ItemMelee, "  carrying TactItem_156910");
    }
    CheckEqI32(g_nActionMode, PUFO_ENEMYAI_MODE_NEIGHBOUR_A, "  rand(2) == 0 gives mode 4");
    CheckEqI32(g_nBurstShots, 0, "  Map.nBurstShots is cleared");
    CheckEqI32(g_nMeleeX, 9, "  the melee cell is the multi-target's cell");
    CheckEqI32(g_nMeleeY, 7, "  its row");
    CheckEqI32(g_nMeleeLayer, 2, "  its layer");
    CheckEqI32(g_aEnemy[1].uTu, 10, "  the neighbour pays half its TU");
    CheckEqI32(g_aEnemy[0].uTu, 20, "  the picked unit pays nothing");
    CheckEqI32(g_aEnemy[0].bAiDone, 0, "  and is not marked done, so it is picked again");
    /* Every Tact_EnqueueActionOrStatus205 call in this tick is handed nTuCost 0 (`MOV R3, #0` at
     * 0x4D77C and 0x4D7B4), so its gate at 0x4B42C cannot fire and no refusal record exists here. */
    CheckEqI32(g_nRecordPushes, 0, "  a zero-cost command-18 cannot be refused");
}

static void TestMeleeWalkCommit(void)
{
    static const u32 aScript[] = { 0, 1, 0 };
    const TactQueueEntry *pAct;

    printf("The flag-3 melee branch commits when the facing delta lands on the target\n");

    CheckEqI32(g_aUnitTypeFlag3[UNIT_TYPE_CHRYSSALID], 1,
               "  the Chryssalid row has flag3 set");

    TickReset(aScript, 3);   /* pick, a rejected accept roll, the melee attempt's index */
    g_aEnemy[0].nUnitType = UNIT_TYPE_CHRYSSALID;
    g_aEnemy[0].bFacing = 2;                 /* aDirDeltaXY[2] is (+1, 0) */
    g_Target.pCell = Cell(8, 7, 2);
    Cell(8, 7, 2)->pUnit = &g_Target;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    pAct = AiFirstAction18();
    Check(pAct != NULL, "  a command-18 entry was queued");
    if (pAct != NULL) {
        CheckEqI32(pAct->nMode, PUFO_ENEMYAI_CMD13, "  with mode 13");
        Check(pAct->pUnit == &g_aEnemy[0], "  executed by the picked unit");
        Check(pAct->pTarget == &g_Target, "  against the multi-target");
        Check(pAct->pWeapon == &g_ItemAction13, "  carrying TactItem_156A58");
    }
    CheckEqI32(g_aEnemy[0].uTu, 0, "  the melee attack zeroes the unit's TU");
    CheckEqI32(g_nMeleeX, 8, "  the melee cell is the cell the attack landed on");
    CheckEqI32(g_nMeleeY, 7, "  at the unit's own row");
    CheckEqI32(g_nMeleeLayer, 2, "  at the target's layer");
    CheckEqI32(g_nPathCalls, 0, "  no path attempt was needed");
}

static void TestCeilRaisesTheScanLayer(void)
{
    static const u32 aHit[]   = { 0, 1, 0 };
    static const u32 aMiss[]  = { 0, 1, 0, 0, 0, 0, 0 };
    const TactQueueEntry *pAct;

    printf("A ceiling byte below -15 raises the eight-direction scan one layer\n");

    /* The target is far away, so the reach test fails and the scan decides the outcome.
     * Facing 0 is (0, -1), so the scanned neighbour is (7, 6). */
    g_Occupant.bStatus = 0;
    g_Occupant.nUnitType = 9;

    TickReset(aHit, 3);
    Cell(7, 6, 3)->pUnit = &g_Occupant;
    g_Target.pCell = Cell(12, 12, 2);
    Cell(12, 12, 2)->pUnit = &g_Target;
    g_aEnemy[0].nUnitType = UNIT_TYPE_CHRYSSALID;
    g_aEnemy[0].bFacing = 0;
    g_CeilSpr.iCeilParam = -16;
    Cell(7, 7, 2)->pCeilSpr = &g_CeilSpr;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    pAct = AiFirstAction18();
    Check(pAct != NULL, "  the scan found the occupant one layer up");
    if (pAct != NULL) {
        Check(pAct->pTarget == &g_Occupant, "  the occupant of the raised-layer cell");
        CheckEqI32(pAct->nMode, PUFO_ENEMYAI_CMD13, "  as a command-13 action");
    }
    CheckEqI32(g_nPathCalls, 0, "  so no path was attempted");

    /* -10 is above the threshold, so the scan stays on layer 2 where nothing stands. */
    TickReset(aMiss, 7);
    g_aEnemy[0].nUnitType = UNIT_TYPE_CHRYSSALID;
    g_aEnemy[0].bFacing = 0;
    g_CeilSpr.iCeilParam = -10;
    Cell(7, 7, 2)->pCeilSpr = &g_CeilSpr;
    g_bPathOk = false;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    CheckEqI32(g_nPathCalls, PUFO_ENEMYAI_MELEE_TRIES,
               "  a ceiling byte of -10 leaves the scan on the unit's own layer");
    Check(AiFirstAction18() == NULL, "  and no melee action was queued");
    Cell(7, 6, 3)->pUnit = NULL;
    Cell(7, 7, 2)->pCeilSpr = NULL;
}

static void TestRmpWalkOrder(void)
{
    static const u32 aScript[] = { 1, 1 };

    printf("The RMP walk advances the index before using it, so the drawn index is skipped\n");

    TickReset(aScript, 2);
    g_aEnemy[0].nMultiTarget = 0;   /* straight to the fallback move */
    g_Rmp.nNodeCount = 3;
    for (i32 i = 0; i < 3; ++i) {
        g_aRmpNodes[i].nX = (u8)(i + 1);
        g_aRmpNodes[i].nY = (u8)(i + 1);
        g_aRmpNodes[i].bFlag19 = 5;   /* a role the predicate accepts for every type */
        g_Rmp.apNodes[i] = &g_aRmpNodes[i];
    }
    g_bPathOk = false;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    CheckEqI32(g_nPathCalls, 3, "  every node was tried, in scan order");
    /* Drawn index 1, then ++ before use: 2, 0, 1. */
    Check(g_nPathSeq == 3 && g_aPathX[0] == 3 && g_aPathY[0] == 3
              && g_aPathX[1] == 1 && g_aPathY[1] == 1
              && g_aPathX[2] == 2 && g_aPathY[2] == 2,
          "  the visit order is node 2, node 0, node 1 -- never the drawn node first");
    CheckEqI32(g_nPathFlag, 1, "  the RMP path is requested with bFlag 1");
    CheckEqI32(g_nAfterStepCalls, 1, "  scanning every node moves on, and the turn ends");
}

static void TestRmpFailLimitMarksUnitDone(void)
{
    static const u32 aScript[] = { 1, 0 };

    printf("Eleven failed RMP paths mark the unit done\n");

    TickReset(aScript, 2);
    g_aEnemy[0].nMultiTarget = 0;
    g_Rmp.nNodeCount = 12;
    for (i32 i = 0; i < 12; ++i) {
        g_aRmpNodes[i].nX = 1;
        g_aRmpNodes[i].nY = 1;
        g_aRmpNodes[i].bFlag19 = 5;
        g_Rmp.apNodes[i] = &g_aRmpNodes[i];
    }
    g_bPathOk = false;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);

    CheckEqI32(g_nPathCalls, PUFO_ENEMYAI_RMP_FAIL_MAX + 1,
               "  the limit is reached on the eleventh failure");
    CheckEqI32(g_aEnemy[0].bAiDone, 1, "  the unit is marked done");
    CheckEqI32(g_Tact.bAiFocusSaved, 1, "  and the focus is saved");
    CheckEqI32(g_nAfterStepCalls, 0, "  so the enemy turn continues");
}

static void TestRankChangesTheFallbackRoll(void)
{
    static const u32 aScript[] = { 2, 2 };

    printf("Rank 4/8 draws rand(6) and keeps only a 1; every other rank draws rand(3)\n");

    /* A draw of 2: rank 0 keeps it (non-zero -> walk), rank 4 throws it away (-> turn). */
    TickReset(aScript, 1);
    g_aEnemy[0].nMultiTarget = 0;
    g_aEnemy[0].bRankType = 0;
    g_Rmp.nNodeCount = 0;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    CheckEqI32(g_nTurnCalls, 0, "  rank 0 keeps a draw of 2 and walks instead");

    TickReset(aScript + 1, 2);
    g_aEnemy[0].nMultiTarget = 0;
    g_aEnemy[0].bRankType = PUFO_ENEMYAI_RANK_LEADER;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    CheckEqI32(g_nTurnCalls, 1, "  rank 4 discards a rand(6) draw that is not 1 and turns");

    TickReset(aScript + 1, 2);
    g_aEnemy[0].nMultiTarget = 0;
    g_aEnemy[0].bRankType = PUFO_ENEMYAI_RANK_COMMANDER;
    Tact_EnemyAiTick(&g_Tact, &g_Tick);
    CheckEqI32(g_nTurnCalls, 1, "  rank 8 behaves the same way");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestTableMatchesDecode();
    TestPredicateExhaustive();
    TestRoleReadFromOther();
    TestSizeAsymmetry();
    TestOutOfRangeAndNull();
    TestRandomTurn();
    TestLargeUnitDoesNothing();
    TestAiPairRaisedOnBothPaths();
    TestWeaponPickRules();
    TestShotCount();
    TestSameColumnRedraw();
    TestRowZeroFiresAtOwnCell();
    TestCostTablesAndLatch();
    TestDropHandsByType();
    TestFloorSearchOnlyForFlag0Clear();
    TestWanderRetriesOwnCellAndOccupant();
    TestPathResultWritesTuAndFlag();
    TestStatusMessageOnBothPaths();
    TestTuRefusalStillRunsTheTail();
    TestNoEnemiesRunsTail();
    TestTickUnitGates();
    TestActingUnitVisibilityAndTrack();
    TestLowMoraleRunsThePanicAi();
    TestFireAffordabilityGate();
    TestThrowPath();
    TestThrowRefusalKeepsTheTail();
    TestMultiTargetMode7();
    TestMeleeNeighbourHandoff();
    TestMeleeWalkCommit();
    TestCeilRaisesTheScanLayer();
    TestRmpWalkOrder();
    TestRmpFailLimitMarksUnitDone();
    TestRankChangesTheFallbackRoll();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
