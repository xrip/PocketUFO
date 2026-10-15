/* test_enemyai.c - checks the enemy AI's multi-target type predicate.
 *
 * The predicate is driven by a 4-byte-stride table whose two used columns were
 * decoded byte by byte. The interesting part is the asymmetry: roles 1 and 2 also
 * require the unit to be SMALL, while roles 3 and 4 test only the flag column.
 */
#include "game/pufo_enemyai.h"

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

    memset(&g_Ctx, 0, sizeof(g_Ctx));
    g_Ctx.pfnRandU16        = Ai_Rand;
    g_Ctx.pfnPathToCell     = Ai_Path;
    g_Ctx.pfnFillActionCosts = Ai_FillCosts;
    g_Ctx.pfnPushStatus     = Ai_Push;
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

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
