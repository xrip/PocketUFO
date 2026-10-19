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

    if (nMax == 0) {
        return 0;
    }

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
