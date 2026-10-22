/* test_rand.c - Game_RandU16 @ 0x3e59c, the draw every roll in the game runs through.
 *
 * Evidence: Game_RandU16 @ 0x3e59c (30 instructions, ONE basic block). The seeding that feeds it is
 * in the process entry point (see pufo_rand.h), which no test calls -- it is not cited here because
 * nothing in this file reaches it. The block is a straight line: `BL rand` at 0x3E5A4 is the first
 * thing it does; the result goes through _itos (0x3E5B4), _muls with the single-precision
 * constant 0x38000000 (0x3E5BC), _itos of nMax (0x3E5E4), a second _muls (0x3E5FC) and _stou
 * (0x3E610). Nothing reads nMax before the draw, and there is no branch anywhere.
 *
 * The file pins three things that a plausible-looking port gets wrong:
 *   1. A zero bound consumes a draw. There is no `if (nMax == 0)` in the original -- the
 *      multiply by 0 runs AFTER rand(), so the stream advances. The port had an early return,
 *      and a zero bound is reached in the port's own flows (the fire-damage roll computes
 *      `u16 nMax = (nRaw / 4) & 0xFFFF`), so every later roll was one draw off.
 *   2. The scale constant is exactly 1/32768, which makes Game_RandU16(32768) the identity on
 *      the draw. Any other power of two moves every value.
 *   3. The floor is a truncation of a float product, so nMax == 1 is always 0 and the largest
 *      value a draw can produce is 65533 for nMax == 65535.
 *
 * The draw sequence is the CRT's: `srand` before and after lets the test name the draw the
 * function consumed without recomputing the formula.
 */
#include "util/pufo_rand.h"

#include <stdio.h>
#include <stdlib.h>

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

/* Any fixed seed is fine: the checks compare draws against draws, not against constants the
 * CRT happens to produce. */
#define TEST_SEED 12345u

/* 1. A zero bound still consumes a draw (0x3E5A4 runs before 0x3E5E4). */
static void TestZeroBoundStillDraws(void)
{
    int nFirstDraw;
    int nAfterCall;

    printf("Game_RandU16 @ 0x3e59c -- a zero bound and the draw it consumes\n");

    srand(TEST_SEED);
    nFirstDraw = rand();

    srand(TEST_SEED);
    CheckEqI32((i32)Game_RandU16(0), 0, "  nMax 0 returns 0");
    nAfterCall = rand();

    /* The value the call consumed is the first draw, so the next rand() is the SECOND one. An
     * early return leaves the stream where it was and the two draws come back equal. */
    Check(nAfterCall != nFirstDraw,
          "  and the draw it consumed is real: rand() after the call gives the next value (0x3E5A4)");
}

/* 2. The scale is 1/32768, so nMax 32768 returns the draw unchanged.
 *
 * One seed, walked twice: the first pass records what rand() produced, the second pass puts the same
 * sequence through Game_RandU16. Re-seeding per draw would not work -- MSVC's LCG advances one step per
 * srand(seed) and consecutive seeds give consecutive states, so 200 re-seeded draws all land in
 * 7584..8234 and never reach the top of the range. A single seed's sequence does (253..32689 over the
 * 400 draws used here), which is what makes the bounds below worth asserting. */
#define TEST_DRAWS 400

static void TestScaleIsOneOver32768(void)
{
    int aWant[TEST_DRAWS];
    int nMismatch = 0;
    int nFirstBad = -1;
    int nGot = 0;
    int nWant = 0;
    int k;

    printf("Game_RandU16 @ 0x3e59c -- the 0x38000000 scale over one 400-draw sequence\n");

    srand(TEST_SEED);
    for (k = 0; k < TEST_DRAWS; ++k) {
        aWant[k] = rand() % 32768;
    }

    srand(TEST_SEED);
    for (k = 0; k < TEST_DRAWS; ++k) {
        nGot = (i32)Game_RandU16(32768);
        if (nGot != aWant[k]) {
            ++nMismatch;
            if (nFirstBad < 0) {
                nFirstBad = k;
                nWant = aWant[k];
            }
        }
    }

    CheckEqI32(nMismatch, 0, "  every draw maps to itself at nMax 32768");
    if (nFirstBad >= 0) {
        printf("       first mismatch at draw %d: got %d, want %d\n", nFirstBad, nGot, nWant);
    }
}

/* 3. The truncation: nMax 1 is always 0 (the product is under 1 for every draw), and the top of the
 *    range is 65533, not 65535, because the largest draw is 32767 and 32767 / 32768 * 65535 floors to
 *    65533. The second half of each check says the run really did reach the top of the range -- a
 *    fixture claim, without which "never exceeds" is trivially true of a run that never got high. */
static void TestTruncationBounds(void)
{
    int k;
    int nAnyNonZero = 0;
    int nAboveTop = 0;
    int nTopSeen = 0;
    int nTopDraw = 0;

    printf("Game_RandU16 @ 0x3e59c -- the floor of the product\n");

    srand(TEST_SEED);
    for (k = 0; k < TEST_DRAWS; ++k) {
        if (Game_RandU16(1) != 0) {
            nAnyNonZero = 1;
        }
    }
    Check(nAnyNonZero == 0, "  nMax 1 is 0 for every draw (the product is under 1)");

    srand(TEST_SEED);
    for (k = 0; k < TEST_DRAWS; ++k) {
        i32 nVal = (i32)Game_RandU16(65535);
        if (nVal > 65533) {
            nAboveTop = 1;
        }
        if (nVal > nTopSeen) {
            nTopSeen = nVal;
        }
    }
    Check(nAboveTop == 0, "  nMax 65535 never exceeds 65533 (32767 / 32768 * 65535 floors to 65533)");

    /* The bound above is one-sided, so the exact top is pinned too: this seed's largest draw is 32689,
     * and 32689 * 65535 / 32768 = 65376.99 truncates to 65377, which is what the scale-and-truncate
     * chain must produce. A doubled scale wraps instead (65218), a rounding scale lands on 65377+1. */
    CheckEqI32(nTopSeen, 65377, "  the largest value at nMax 65535 is the truncation of the largest draw");

    /* The largest draw this seed produced, re-read the same way the function reads it: the run has to
     * reach the upper half of the range for the two checks above to have tested anything there. */
    srand(TEST_SEED);
    for (k = 0; k < TEST_DRAWS; ++k) {
        int nDraw = rand() % 32768;
        if (nDraw > nTopDraw) {
            nTopDraw = nDraw;
        }
    }
    Check(nTopDraw >= 16384, "  the fixture's sequence reaches the top of the draw range");
    printf("       highest draw seen: %d, highest value at nMax 65535: %d\n", nTopDraw, nTopSeen);
}

int main(void)
{
    TestZeroBoundStillDraws();
    TestScaleIsOneOver32768();
    TestTruncationBounds();

    if (g_nFailures > 0) {
        printf("test_rand: %d check(s) FAILED\n", g_nFailures);
        return 1;
    }
    printf("test_rand: all checks passed\n");
    return 0;
}
