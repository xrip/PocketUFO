/* pufo_rand.c - the game's RNG entry point.
 *
 * Evidence: Game_RandU16 @ 0x3e59c, and the seeding that feeds it in InitInstance @ 0x9fd48
 * (0xA00D8..0xA024C: GetSystemTime, srand, the warm-up draws, and the nine draws whose two live
 * results are Map_nMissionType and Map+0x1511E).
 *
 * The original is:
 *     return (u16)((float)rand() * (1.0f / 32768.0f) * (float)nMax);
 * with 0x38000000 as the scale constant. It assumes rand() returns 0..32767,
 * which is true on WinCE and on MSVC's CRT, but NOT on POSIX/musl where
 * rand() returns 0..RAND_MAX (2^31-1). To keep the game's distributions and
 * its saved-RNG-driven content identical to the original, the raw rand()
 * result is reduced modulo 32768 before scaling. Without this the port would
 * silently produce different unit rosters and mission types than the original.
 */
#include "util/pufo_rand.h"

#include <stdlib.h>
#include <time.h>

#include <windows.h>

#define RAND_ORIGINAL_MODULUS 32768 /* original rand() range, 0..32767 */
#define RAND_SCALE (1.0f / (float)RAND_ORIGINAL_MODULUS)

u16 Game_RandU16(u16 nMax)
{
    int   nRand;
    float fScaled;

    /* No branch on nMax in the original: 0x3e59c is one basic block and `BL rand` at 0x3E5A4 is the
     * first thing it does, before nMax is moved into a register (MOV R3, R4, LSL#16 at 0x3E5CC). A zero
     * bound therefore CONSUMES a draw and still returns 0 -- the multiply by 0 happens after the draw,
     * not instead of it. The port's early return skipped that draw, and a zero bound is reached: the
     * fire-damage roll (pufo_turn.c) computes `u16 nMax = (nRaw / 4) & 0xFFFF`, which is 0 whenever the
     * base rounds down to nothing, and the enemy AI's pick from the multi-target list runs the same
     * seam on a signed byte. After such a call the port's stream was one draw ahead of the original's. */
    nRand = rand() % RAND_ORIGINAL_MODULUS;
    fScaled = (float)nRand * RAND_SCALE * (float)nMax;
    return (u16)fScaled;
}

/* The seeding InitInstance does at 0xA00D8..0xA011C: GetSystemTime, srand(100 * wSecond +
 * wMilliseconds), then plain rand() wSecond times. The seed value and the number of draws the warm-up
 * spends come out of the same clock field, so one without the other is not the original's stream. */

void Game_SeedRng(void)
{
    SYSTEMTIME st;
    u16 n;

    GetSystemTime(&st);                                        /* 0xA00D8 */
    srand((unsigned)(100 * st.wSecond + st.wMilliseconds));    /* srand @ 0xA00F8 */
    for (n = 0; n < st.wSecond; ++n) {                         /* 0xA0108..0xA011C: rand(), wSecond times */
        (void)rand();
    }
}

void Game_SeedRngFixed(void)
{
    u16 n;

    srand((unsigned)(100 * PUFO_SHELL_CLOCK_SECOND + PUFO_SHELL_CLOCK_MS));
    for (n = 0; n < PUFO_SHELL_CLOCK_SECOND; ++n) {
        (void)rand();
    }
}

/* InitInstance @ 0x9FD48's draw-and-pick block, 0xA0134..0xA024C, run straight after the seed. The
 * record's order is 5, 3, then 2 ONLY when the 3-draw is not 1 (the gate at 0xA0154 stores 0 and skips
 * the draw), 8, 2, 4, 0x46, 0xC8, then the mission pick: 10, redrawn while it lands on 7. Seven of the
 * pre-pick results land on cells that no function in the image ever reads (0xAC6C0, 0xAC6C4, 0xAC6C8,
 * 0xAC6CC, 0x188D58, 0x188D5C, 0x188D60) -- they are still draws, so they still move the stream the
 * whole game then reads. The two live results come out through the arguments. */
void Game_SeedStartupPicks(u8 *pnMissionType, u8 *pbUnk1511E)
{
    u16 nTier;
    u8  nType;

    *pnMissionType = 0;
    *pbUnk1511E = 0;

    (void)Game_RandU16(5);                    /* 0xA0134 dword_AC6C0 */
    nTier = Game_RandU16(3);                  /* 0xA014C unk_188D58 */
    if (nTier != 1) {
        (void)Game_RandU16(2);                /* 0xA016C dword_AC6C4; 0xA0154's `== 1` arm skips this draw */
    }
    (void)Game_RandU16(8);                    /* 0xA01A0 dword_AC6C8; its 0 was raised to 1 at 0xA01A8, unread */
    if (Game_RandU16(2) == 0) {
        *pbUnk1511E = 1;                      /* 0xA01C0/0xA01C8 Map+0x1511E (unk_128AD6) */
    }
    (void)Game_RandU16(4);                    /* 0xA01E4 dword_AC6CC = draw + 1, overwritten with 2 at 0xA035C */
    (void)Game_RandU16(0x46);                 /* 0xA01FC word_188D5C */
    (void)Game_RandU16(0xC8);                 /* 0xA0214 unk_188D60 */

    do {
        nType = (u8)Game_RandU16(0xA);        /* 0xA0220 */
    } while (nType == 7);                     /* 0xA0230 */
    *pnMissionType = nType;                   /* 0xA0228 Map_nMissionType (0x128AD2) */

    if (nType == 3 || nType == 4) {
        *pbUnk1511E = 0;                      /* 0xA0244/0xA024C */
    }
}
