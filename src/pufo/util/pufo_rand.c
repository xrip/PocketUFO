/* pufo_rand.c - the game's RNG entry point.
 *
 * Evidence: Game_RandU16 @ 0x3e59c, seeding in InitInstance @ 0x9fd48.
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

/* Game_SeedRngFixed's seed: shell scaffolding, not a value the original uses. */
#define PUFO_SHELL_RNG_SEED 1

u16 Game_RandU16(u16 nMax)
{
    int   nRand;
    float fScaled;

    /* No branch on nMax in the original: 0x3e59c is one basic block and `BL rand` at 0x3e5A4 is the
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

void Game_SeedRng(void)
{
    SYSTEMTIME st;

    GetSystemTime(&st);
    srand((unsigned)(100 * st.wSecond + st.wMilliseconds));
}

void Game_SeedRngFixed(void)
{
    srand(PUFO_SHELL_RNG_SEED);
}
