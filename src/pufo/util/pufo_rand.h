/* pufo_rand.h - the game's RNG entry point.
 *
 * Evidence: Game_RandU16 @ 0x3e59c.
 * The original calls the CRT rand(), scales by 1/32768 (the single-precision
 * constant 0x38000000), multiplies by nMax, and truncates to 16 bits. Seeded
 * from the system clock in InitInstance @ 0x9fd48.
 */
#ifndef PUFO_RAND_H
#define PUFO_RAND_H

#include "pufo_common.h"

/* Returns a value in [0, nMax). nMax == 0 yields 0. */
u16 Game_RandU16(u16 nMax);

/* Seed the CRT RNG the way InitInstance does. */
void Game_SeedRng(void);

#endif /* PUFO_RAND_H */
