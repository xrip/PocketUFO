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

/* Seed the CRT RNG the way InitInstance does: srand(100 * wSecond + wMilliseconds) at 0xA00F8, then
 * `rand()` wSecond times (the warm-up loop 0xA0108..0xA011C). The warm-up count is part of the seed. */
void Game_SeedRng(void);

/* SHELL SCAFFOLDING, not the original: the shell's standing clock for Game_SeedRngFixed. It is a
 * second/ms PAIR rather than a seed, because the original derives both the seed (100 * second +
 * millisecond, 0xA00F8) and the warm-up draw count (0xA0108..0xA011C) from that one value. The second
 * is non-zero so the warm-up loop really runs -- and can be seen -- on the shell's own paths: seed
 * 3000, then 30 plain draws. */
#define PUFO_SHELL_CLOCK_SECOND 30
#define PUFO_SHELL_CLOCK_MS 0

/* SHELL SCAFFOLDING, not the original: the same two steps from the fixed clock above, so a picture
 * that has to match a committed reference gets the same draw order. The original's seed is wall-clock
 * time, so a real new game draws a different map every run. Only App_InitInstance's debug arm calls
 * this. */
void Game_SeedRngFixed(void);

/* InitInstance @ 0x9FD48's draw-and-pick block, 0xA0134..0xA024C, run right after the seed. The
 * record's order is 5, 3, then 2 ONLY when the 3-draw is not 1 (0xA0154 stores 0 and skips that
 * draw), 8, 2, 4, 0x46, 0xC8, then the mission pick: 10 redrawn while it lands on 7. Seven of the
 * pre-pick results land on cells no function in the image ever reads (0xAC6C0, 0xAC6C4, 0xAC6C8,
 * 0xAC6CC, 0x188D58, 0x188D5C, 0x188D60) but they are still draws, so they still move the stream the
 * rest of the game reads. The two live cells -- Map+0x1511E (unk_128AD6, set when the (2) draw is 0
 * and cleared again for mission types 3 and 4) and Map_nMissionType (0x128AD2) -- come out through
 * the arguments. */
void Game_SeedStartupPicks(u8 *pnMissionType, u8 *pbUnk1511E);

#endif /* PUFO_RAND_H */
