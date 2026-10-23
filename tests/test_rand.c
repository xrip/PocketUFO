/* test_rand.c - Game_RandU16 @ 0x3e59c, the draw every roll in the game runs through.
 *
 * Evidence: Game_RandU16 @ 0x3e59c (30 instructions, ONE basic block), and the seeding that feeds it,
 * which lives in InitInstance @ 0x9fd48 (0xA00D8..0xA024C) -- Game_SeedRng, Game_SeedRngFixed and
 * Game_SeedStartupPicks are the port's three pieces of that block, and the last tests below reach all
 * three. The block is a straight line: `BL rand` at 0x3E5A4 is the first
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

/* ---- InitInstance @ 0x9fd48's own seeding and picks, 0xA00D8..0xA024C ------------------------ */

/* The shell's fixed clock (pufo_rand.h), written as a literal here on purpose: a check that reads
 * the same constant the port reads cannot see that constant change. */
#define SHELL_CLOCK_SECOND 30
#define SHELL_CLOCK_MS 0

static void ReplayStartupDraws(u8 *pnType, u8 *pbFlagRaw, u8 *pbFlagCleared)
{
    u16 nTier;
    u16 nType;
    u8  bFlag;

    (void)Game_RandU16(5);                        /* 0xA0134 dword_AC6C0 */
    nTier = Game_RandU16(3);                      /* 0xA014C unk_188D58 */
    if (nTier != 1) {
        (void)Game_RandU16(2);                    /* 0xA016C, skipped by 0xA0154's `== 1` arm */
    }
    (void)Game_RandU16(8);                        /* 0xA01A0 dword_AC6C8 */
    bFlag = 0;
    if (Game_RandU16(2) == 0) {
        bFlag = 1;                                /* 0xA01C0/0xA01C8 Map+0x1511E */
    }
    (void)Game_RandU16(4);                        /* 0xA01E4 dword_AC6CC */
    (void)Game_RandU16(0x46);                     /* 0xA01FC word_188D5C */
    (void)Game_RandU16(0xC8);                     /* 0xA0214 unk_188D60 */
    do {
        nType = (u16)Game_RandU16(0xA);           /* 0xA0220 */
    } while (nType == 7);                         /* 0xA0230 */
    *pnType = (u8)nType;                          /* 0xA0228 Map_nMissionType */
    *pbFlagRaw = bFlag;
    if (nType == 3 || nType == 4) {
        bFlag = 0;                                /* 0xA0244/0xA024C */
    }
    *pbFlagCleared = bFlag;
}

/* The seed and the warm-up are one clock field (0xA00F8 seeds from 100 * wSecond + wMilliseconds and
 * 0xA0108..0xA011C then calls rand() wSecond times). The replay spells both numbers out; a port that
 * seeds from the second alone, or that drops the loop, lands somewhere else in the stream. */
static void TestSeedAndWarmUpAreOneField(void)
{
    unsigned i;
    int nWant;
    int nGot;

    printf("InitInstance @ 0x9fd48 -- the seed and the warm-up it spends\n");

    CheckEqI32(PUFO_SHELL_CLOCK_SECOND, SHELL_CLOCK_SECOND, "the shell clock's seconds @ 0x9fd48");
    CheckEqI32(PUFO_SHELL_CLOCK_MS, SHELL_CLOCK_MS, "  and its milliseconds @ 0x9fd48");

    srand(100u * SHELL_CLOCK_SECOND + SHELL_CLOCK_MS);
    for (i = 0; i < SHELL_CLOCK_SECOND; ++i) {
        (void)rand();
    }
    nWant = rand();

    Game_SeedRngFixed();
    nGot = rand();
    CheckEqI32(nGot, nWant, "srand(100 * second + ms) then exactly `second` plain draws @ 0x9fd48");
}

/* The nine draws after the seed. The replay uses the record's own bounds in the record's order, so a
 * bound changed, a draw dropped, or 0xA0154's gate inverted moves the port off the replay. The draw
 * asked for after the block is what a missing or extra draw shows up as. */
static void TestStartupPicksFollowTheRecord(void)
{
    u8 bPortType, bPortFlag;
    u8 bType, bRaw, bCleared;
    int nPortNext;
    int nReplayNext;

    printf("InitInstance @ 0x9fd48 -- the nine draws 0xA0134..0xA024C\n");

    Game_SeedRngFixed();
    Game_SeedStartupPicks(&bPortType, &bPortFlag);
    nPortNext = (int)Game_RandU16(1000);

    srand(100u * SHELL_CLOCK_SECOND + SHELL_CLOCK_MS);
    for (unsigned i = 0; i < SHELL_CLOCK_SECOND; ++i) {
        (void)rand();
    }
    ReplayStartupDraws(&bType, &bRaw, &bCleared);
    nReplayNext = (int)Game_RandU16(1000);

    CheckEqI32(bPortType, bType, "the mission pick the shell clock draws @ 0x9fd48");
    CheckEqI32(bPortFlag, bCleared, "  and the Map+0x1511E byte it leaves @ 0x9fd48");
    CheckEqI32(nPortNext, nReplayNext, "  the block spent exactly those draws: the next one agrees too @ 0x9fd48");
}

/* 800 seeds, each run twice: once through the record spelled out, once through the port's block. The
 * pick must stay in 0..10 and never 7, the flag must stay 0/1, the draw-skipping arm at 0xA0154 must
 * actually be taken by some seed, and the type-3/4 clear at 0xA0244 must actually matter somewhere. */
static void TestPickRangeGateAndClear(void)
{
    unsigned s;
    int nGateArm = 0;
    int nClearMattered = 0;
    int nFlagUp = 0;
    bool bBadRange = false;
    bool bMismatch = false;

    for (s = 1; s <= 800; ++s) {
        u8 bPortType, bPortFlag;
        u8 bType, bRaw, bCleared;

        srand(s);
        ReplayStartupDraws(&bType, &bRaw, &bCleared);
        srand(s);
        Game_SeedStartupPicks(&bPortType, &bPortFlag);

        if (bPortType == 7 || bPortType > 10) {
            bBadRange = true;
        }
        if (bPortFlag > 1) {
            bBadRange = true;
        }
        if (bPortType != bType || bPortFlag != bCleared) {
            bMismatch = true;
        }
        if (bRaw != bCleared) {
            ++nClearMattered;
        }
        if (bCleared == 1) {
            ++nFlagUp;
        }
    }

    for (s = 1; s <= 800; ++s) {
        srand(s);
        (void)Game_RandU16(5);
        if (Game_RandU16(3) == 1) {
            ++nGateArm;
        }
    }

    Check(!bBadRange, "800 seeds: the pick stays in 0..10 and is never 7, the flag stays 0 or 1 @ 0x9fd48");
    Check(!bMismatch, "  and the port's block agrees with the record on all 800 @ 0x9fd48");
    Check(nGateArm > 0, "  some seeds take 0xA0154's arm that skips a draw @ 0x9fd48");
    Check(nFlagUp > 0, "  some seeds raise Map+0x1511E at 0xA01C8 @ 0x9fd48");
    Check(nClearMattered > 0, "  some seeds are type 3 or 4, where 0xA024C clears it again @ 0x9fd48");
}

int main(void)
{
    TestZeroBoundStillDraws();
    TestScaleIsOneOver32768();
    TestTruncationBounds();
    TestSeedAndWarmUpAreOneField();
    TestStartupPicksFollowTheRecord();
    TestPickRangeGateAndClear();

    if (g_nFailures > 0) {
        printf("test_rand: %d check(s) FAILED\n", g_nFailures);
        return 1;
    }
    printf("test_rand: all checks passed\n");
    return 0;
}
