#!/usr/bin/env python3
"""Part 134: mutations for the checks that close PLAN row 133a.

Row 133a said two things had no observer in the suite: the defence burn each barrage pair takes out of the attack
budget, and how far the buy screen's teardown clear actually runs. Both now have checks, and each check is proved
by a mutation that moves the value past the interval the checks pin.

Eight cases for the burns (each constant's interval has a lower and an upper end -- the mutation moves the constant
just past the end that the corresponding check pins), four for the teardown (each clear loop shortened by one u16
slot, plus the store the record does not have at site+0x206).

Run from the repo root:  python -I docs/parity/mutations_part134.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

H = "src/pufo/game/pufo_baseui.h"
C = "src/pufo/game/pufo_baseui.c"
T = "test_baseui"

# ---- the four burns, one case per pinned end ----------------------------------------------
# missile: the record burns 500 (`SUB #0x1F4`, 0x57CE0). Checks: 500 empties a 500 budget in one pair; a burn of
# 600 or more would stop the 1200-budget run after two pairs.
run_case("missile burn 500 -> 499 (a 500 budget would survive one pair)", T, H,
         b"#define PUFO_BASEAT_DAMAGE_MISSILE    500", b"#define PUFO_BASEAT_DAMAGE_MISSILE    499")
run_case("missile burn 500 -> 600 (the third missile pair would not run)", T, H,
         b"#define PUFO_BASEAT_DAMAGE_MISSILE    500", b"#define PUFO_BASEAT_DAMAGE_MISSILE    600")

# laser: 600 (`SUB #0x258`, 0x57E8C).
run_case("laser burn 600 -> 599 (a 1200 budget would outlast two pairs)", T, H,
         b"#define PUFO_BASEAT_DAMAGE_LASER      600", b"#define PUFO_BASEAT_DAMAGE_LASER      599")
run_case("laser burn 600 -> 750 (the fifth round would not fire)", T, H,
         b"#define PUFO_BASEAT_DAMAGE_LASER      600", b"#define PUFO_BASEAT_DAMAGE_LASER      750")

# plasma: 900 (`SUB #0x384`, 0x5803C).
run_case("plasma burn 900 -> 749 (four rounds of it would fit in 3000)", T, H,
         b"#define PUFO_BASEAT_DAMAGE_PLASMA     900", b"#define PUFO_BASEAT_DAMAGE_PLASMA     749")
run_case("plasma burn 900 -> 1000 (the fourth round's gravity pair would be skipped)", T, H,
         b"#define PUFO_BASEAT_DAMAGE_PLASMA     900", b"#define PUFO_BASEAT_DAMAGE_PLASMA     1000")

# fusion: 1500 (`MOV #0x5D0` / `ORR #0xC` at 0x581E0, `SUB R3,R2,R3` at 0x581EC).
run_case("fusion burn 1500 -> 1499 (a 3000 budget would reach the third pair)", T, H,
         b"#define PUFO_BASEAT_DAMAGE_FUSION     1500", b"#define PUFO_BASEAT_DAMAGE_FUSION     1499")
run_case("fusion burn 1500 -> 2200 (the first pair would empty a 2200 budget)", T, H,
         b"#define PUFO_BASEAT_DAMAGE_FUSION     1500", b"#define PUFO_BASEAT_DAMAGE_FUSION     2200")

# ---- the three teardown clear loops (0x76C18/0x76C20, 0x76C54, 0x76C74) ---------------------
run_case("item-quantity clear one slot short", T, C,
         b"memset(pSite->auItemQty, 0, sizeof(pSite->auItemQty));   /* 0x76C18 */",
         b"memset(pSite->auItemQty, 0, sizeof(pSite->auItemQty) - 2);   /* 0x76C18 */")
run_case("soldier id clear one slot short", T, C,
         b"memset(pSite->anSoldierIds, 0, sizeof(pSite->anSoldierIds)); /* 0x76C54 */",
         b"memset(pSite->anSoldierIds, 0, sizeof(pSite->anSoldierIds) - 2); /* 0x76C54 */")
run_case("craft id clear one slot short", T, C,
         b"memset(pSite->anCraftIds, 0, sizeof(pSite->anCraftIds));     /* 0x76C74 */",
         b"memset(pSite->anCraftIds, 0, sizeof(pSite->anCraftIds) - 2);     /* 0x76C74 */")

# the claim the port states in a comment only: the teardown reaches no store at site+0x206 (the record's store run
# 0x76C08..0x76C80 has none), and the build branch is the single writer, at 0x761DC.
run_case("teardown clears the byte the record leaves alone", T, C,
         b"            /* bUnk518 is NOT cleared here -- only the build branch writes it, at 0x761DC. */",
         b"            pSite->bUnk518 = 0; /* clearing a byte the record leaves alone */")
