/* test_vistry.c - parity checks for the visibility ray table.
 * Evidence: Tact_BuildVisRayTable @ 0x37ae4, byte_AD720 @ 0xad720. */
#include "game/pufo_vistry.h"

#include <stdio.h>
#include <string.h>

static int g_nChecks = 0;
static int g_nFails = 0;

static void Check(const char *pWhat, int bOk)
{
    ++g_nChecks;
    if (!bOk) {
        ++g_nFails;
        printf("  FAIL %s\n", pWhat);
    }
}

static void CheckEqI32(const char *pWhat, i32 nGot, i32 nWant)
{
    if (nGot != nWant) {
        printf("  FAIL %s: got %d want %d\n", pWhat, nGot, nWant);
        ++g_nChecks;
        ++g_nFails;
        return;
    }
    ++g_nChecks;
}

static VisRay g_aRays[PUFO_VIS_RAY_COUNT];
static u8 g_aSteps[PUFO_VIS_TABLE_BYTES];

static i32 Step(i32 nPass, i32 nRayInPass, i32 nSlot, i32 nByte)
{
    return g_aSteps[4 * (15 * (31 * nPass + nRayInPass) + nSlot) + nByte];
}

int main(void)
{
    memset(g_aRays, 0, sizeof(g_aRays));
    memset(g_aSteps, 0xEE, sizeof(g_aSteps));
    Tact_BuildVisRayTable(g_aRays, g_aSteps);

    printf("the 120 rays are the perimeter of the 31 x 31 box\n");
    CheckEqI32("ray 0 dx", g_aRays[0].nDx, -15);
    CheckEqI32("ray 0 dy", g_aRays[0].nDy, -15);
    CheckEqI32("ray 15 dx", g_aRays[15].nDx, 0);
    CheckEqI32("ray 29 dy", g_aRays[29].nDy, -15);
    CheckEqI32("ray 30 dx", g_aRays[30].nDx, 15);
    CheckEqI32("ray 45 dy", g_aRays[45].nDy, 0);
    CheckEqI32("ray 59 dy", g_aRays[59].nDy, 14);
    CheckEqI32("ray 60 dx", g_aRays[60].nDx, 15);
    CheckEqI32("ray 60 dy", g_aRays[60].nDy, 15);
    CheckEqI32("ray 75 dx", g_aRays[75].nDx, 0);
    CheckEqI32("ray 89 dx", g_aRays[89].nDx, -14);
    CheckEqI32("ray 90 dx", g_aRays[90].nDx, -15);
    CheckEqI32("ray 105 dy", g_aRays[105].nDy, 0);
    CheckEqI32("ray 119 dy", g_aRays[119].nDy, -14);

    printf("pass p activates rays (15p + k) mod 120\n");
    CheckEqI32("pass 0 ray 0", g_aRays[0].aPass[0], 1);
    CheckEqI32("pass 0 ray 30", g_aRays[30].aPass[0], 1);
    CheckEqI32("pass 0 ray 31 is off", g_aRays[31].aPass[0], 0);
    CheckEqI32("pass 3 ray 45", g_aRays[45].aPass[3], 1);
    CheckEqI32("pass 7 ray 105", g_aRays[105].aPass[7], 1);
    CheckEqI32("pass 7 ray 119", g_aRays[119].aPass[7], 1);
    CheckEqI32("pass 7 wraps to ray 0", g_aRays[0].aPass[7], 1);
    CheckEqI32("pass 7 wraps to ray 15", g_aRays[15].aPass[7], 1);
    CheckEqI32("pass 7 stops at ray 16", g_aRays[16].aPass[7], 0);
    {
        i32 nTotal = 0;
        for (i32 p = 0; p < PUFO_VIS_PASS_COUNT; ++p) {
            for (i32 r = 0; r < PUFO_VIS_RAY_COUNT; ++r) {
                nTotal += g_aRays[r].aPass[p] != 0;
            }
        }
        CheckEqI32("eight passes of 31 rays", nTotal, 8 * 31);
    }

    printf("an axis-aligned ray fills all fifteen steps\n");
    /* Ray 45 is (15, 0) and is the first active ray of pass 3. */
    CheckEqI32("step 0 dominant axis", Step(3, 0, 0, 0), 0);
    CheckEqI32("step 0 other axis", Step(3, 0, 0, 1), 0);
    CheckEqI32("step 0 wall A flag follows the walk direction", Step(3, 0, 0, 2), 1);
    CheckEqI32("step 0 wall B flag, unchanged other axis", Step(3, 0, 0, 3), 0);
    CheckEqI32("step 14 dominant axis", Step(3, 0, 14, 0), 14);
    CheckEqI32("step 14 is not the sentinel", Step(3, 0, 14, 0) != PUFO_VIS_RAY_END, 1);

    printf("a diagonal ray stops at distance 15\n");
    /* Ray 0 is (-15, -15): a tie, so it walks y, and other == v0. */
    CheckEqI32("step 0 other axis", Step(0, 0, 0, 1), 0);
    CheckEqI32("a negative walk direction sets the flag to 2", Step(0, 0, 0, 3), 2);
    CheckEqI32("the change flag lands on the previous step", Step(0, 0, 0, 2), 2);
    CheckEqI32("step 10 other axis", Step(0, 0, 10, 1), (i32)(u8)-10);
    CheckEqI32("step 11 is the end sentinel", Step(0, 0, 11, 0), PUFO_VIS_RAY_END);
    CheckEqI32("nothing is written past the sentinel", Step(0, 0, 12, 0), 0xEE);

    printf("a shallow ray truncates the perpendicular axis\n");
    /* Ray 31 is (15, -14), the second active ray of pass 2. other = v0 * -14/15. */
    CheckEqI32("step 1 truncates -0.93 to 0", Step(2, 1, 1, 1), 0);
    CheckEqI32("step 2 truncates -1.87 to -1", Step(2, 1, 2, 1), (i32)(u8)-1);
    CheckEqI32("step 10 other axis", Step(2, 1, 10, 1), (i32)(u8)-9);
    CheckEqI32("step 11 other axis", Step(2, 1, 11, 1), (i32)(u8)-10);
    CheckEqI32("step 12 is the end sentinel", Step(2, 1, 12, 0), PUFO_VIS_RAY_END);

    printf("the table is fully written for the rays it walks\n");
    Check("no untouched byte survives in pass 0 row 0",
          g_aSteps[0] != 0xEE && g_aSteps[3] != 0xEE);

    printf("%d checks, %d failures\n", g_nChecks, g_nFails);
    return g_nFails != 0;
}
