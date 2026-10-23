#!/usr/bin/env python3
"""Part 135: mutations for the InitInstance @ 0x9fd48 startup block (0xA00D8..0xA024C).

Each case breaks one value the record fixes: the seed formula, the warm-up draw count, the gate at
0xA0154 that skips a draw, the two draws that must be spent, the mission pick's bound, the redraw-while-7
loop, and the Map+0x1511E clear at 0xA0244/0xA024C. Every case must make tests/test_rand.c print a
failing check that names 0x9fd48.

Run from the repo root:  python -I docs/parity/mutations_part135.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

R = "src/pufo/util/pufo_rand.c"
T = "test_rand"

# 1. the seed is 100 * wSecond + wMilliseconds (0xA00F8), not some other combination.
run_case("seed formula loses its 100 * seconds", T, R,
         b"    srand((unsigned)(100 * PUFO_SHELL_CLOCK_SECOND + PUFO_SHELL_CLOCK_MS));",
         b"    srand((unsigned)(10 * PUFO_SHELL_CLOCK_SECOND + PUFO_SHELL_CLOCK_MS));")

# 2. the warm-up loop at 0xA0108..0xA011C spends exactly wSecond draws.
run_case("warm-up loop spends one draw fewer", T, R,
         b"    srand((unsigned)(100 * PUFO_SHELL_CLOCK_SECOND + PUFO_SHELL_CLOCK_MS));\n"
         b"    for (n = 0; n < PUFO_SHELL_CLOCK_SECOND; ++n) {",
         b"    srand((unsigned)(100 * PUFO_SHELL_CLOCK_SECOND + PUFO_SHELL_CLOCK_MS));\n"
         b"    for (n = 0; n < PUFO_SHELL_CLOCK_SECOND - 1; ++n) {")

# 3. 0xA0154: the `== 1` arm stores 0 and SKIPS the draw at 0xA016C. Inverting it spends a draw the
#    record does not spend (and skips one it does).
run_case("the draw-skipping gate at 0xA0154 is inverted", T, R,
         b"    if (nTier != 1) {", b"    if (nTier == 1) {")

# 4./5. the two dead-cell draws at 0xA01FC and 0xA0214 are still draws: dropping one moves the mission
#    pick, the flag and every later roll.
run_case("the 0xA01FC draw is not spent", T, R,
         b"    (void)Game_RandU16(0x46);                 /* 0xA01FC word_188D5C */",
         b"    /* 0xA01FC draw dropped */")
run_case("the 0xA0214 draw is not spent", T, R,
         b"    (void)Game_RandU16(0xC8);                 /* 0xA0214 unk_188D60 */",
         b"    /* 0xA0214 draw dropped */")

# 6. 0xA0244/0xA024C: mission types 3 and 4 clear Map+0x1511E again.
run_case("the type-3/4 clear of Map+0x1511E never runs", T, R,
         b"    if (nType == 3 || nType == 4) {", b"    if (nType == 5) {")

# 7. the pick draws from 10 (0xA0220), not from 9.
run_case("the mission pick draws from 9 instead of 10", T, R,
         b"        nType = (u8)Game_RandU16(0xA);        /* 0xA0220 */",
         b"        nType = (u8)Game_RandU16(9);          /* 0xA0220 */")

# 8. 0xA0230 redraws while the pick is 7.
run_case("the redraw-while-7 loop is dropped", T, R,
         b"    } while (nType == 7);                     /* 0xA0230 */",
         b"    } while (0);                              /* 0xA0230 */")
