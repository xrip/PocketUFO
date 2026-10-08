/* test_enemyai.c - checks the enemy AI's multi-target type predicate.
 *
 * The predicate is driven by a 4-byte-stride table whose two used columns were
 * decoded byte by byte. The interesting part is the asymmetry: roles 1 and 2 also
 * require the unit to be SMALL, while roles 3 and 4 test only the flag column.
 */
#include "game/pufo_enemyai.h"

#include <stdio.h>
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
static const u8 k_aFlag0[16] = { 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0 };
static const u8 k_aLarge[16] = { 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0 };

static void TestTableMatchesDecode(void)
{
    printf("The unit-type table matches the decoded original bytes\n");
    for (i32 t = 0; t < 16; ++t) {
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
        for (i32 t = 0; t < 16; ++t) {
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

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestTableMatchesDecode();
    TestPredicateExhaustive();
    TestRoleReadFromOther();
    TestSizeAsymmetry();
    TestOutOfRangeAndNull();
    TestRandomTurn();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
