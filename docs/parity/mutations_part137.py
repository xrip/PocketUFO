#!/usr/bin/env python3
"""Part 137 mutations: one case per row of the base capacity/need/count family that this Part closes.

Every anchor is taken FROM the file at run time (a regex finds the line and the case mutates that exact text),
so an anchor cannot drift out of step with the source the way a hand-copied one does. A case whose anchor is
not found reports "SKIP ... anchor occurs 0 times" rather than silently doing nothing.

Each mutation is the wrong constant or the wrong comparison the record rules out -- see the 0x68264 note in the
IDB: Base_CountActiveManuf @ 0x68264 reads its entry count from the byte at 0x160C4C (144, stored once at
0x859E4), so case 26 is the one this Part's fix is about.

Run from the repo root:  python -I docs/parity/mutations_part137.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

G = "src/pufo/game/pufo_geoscape.c"
S = "src/pufo/game/pufo_ship.c"


def anchor(path, pattern):
    """Return the exact text of the first line in `path` matching `pattern`, or None."""
    with open(path, encoding="utf-8", newline="") as f:
        for line in f:
            if re.search(pattern, line):
                return line.rstrip("\r\n")
    return None


def case(label, test, path, pattern, repl):
    old = anchor(path, pattern)
    if old is None:
        print(f"SKIP {label}: anchor {pattern!r} not found in {path}")
        return
    new = re.sub(pattern, repl, old, count=1)
    if new == old:
        print(f"SKIP {label}: substitution changed nothing")
        return
    run_case(label, test, path,
             old.replace("\n", "\r\n").encode("utf-8"),
             new.replace("\n", "\r\n").encode("utf-8"))


# ---- the six capacity twins: one weight each (FacilityScoreKind's per-cell weight) ----------------

case("Base_CalcStoresCapacity @ 0x65720 charges a stores cell 250, not 300", "test_geoscape", G,
     r"FacilityScoreKind\(abFacilityGrid, nBaseIndex, 6, 300, true\)", "FacilityScoreKind(abFacilityGrid, nBaseIndex, 6, 250, true)")
case("Base_CalcLivingQuartersCap @ 0x6579c charges a living-quarters cell 40, not 50", "test_geoscape", G,
     r"FacilityScoreKind\(abFacilityGrid, nBaseIndex, 1, 50, true\)", "FacilityScoreKind(abFacilityGrid, nBaseIndex, 1, 40, true)")
case("Base_CalcLabCap @ 0x65818 charges a lab cell 45, not 50", "test_geoscape", G,
     r"FacilityScoreKind\(abFacilityGrid, nBaseIndex, 2, 50, true\)", "FacilityScoreKind(abFacilityGrid, nBaseIndex, 2, 45, true)")
case("Base_CalcWorkshopCap @ 0x65894 charges a workshop cell 55, not 50", "test_geoscape", G,
     r"FacilityScoreKind\(abFacilityGrid, nBaseIndex, 3, 50, true\)", "FacilityScoreKind(abFacilityGrid, nBaseIndex, 3, 55, true)")
case("Base_CalcAlienContainmentCap @ 0x65910 charges a containment cell 12, not 10", "test_geoscape", G,
     r"FacilityScoreKind\(abFacilityGrid, nBaseIndex, 7, 10, true\)", "FacilityScoreKind(abFacilityGrid, nBaseIndex, 7, 12, true)")
case("Base_CalcPsiLabCap @ 0x6598c charges a psi cell 9, not 10", "test_geoscape", G,
     r"FacilityScoreKind\(abFacilityGrid, nBaseIndex, 8, 10, true\)", "FacilityScoreKind(abFacilityGrid, nBaseIndex, 8, 9, true)")

# ---- the needed-flag twins: the budget divisor each pass divides by --------------------------------

case("Base_UpdateWorkshopNeeded @ 0x65bb4 budgets workshops on manuf / 51", "test_geoscape", G,
     r"FacilityBudget\(nActiveManuf, 50\)", "FacilityBudget(nActiveManuf, 51)")
# The two divisors below were tried first and were inert: at the small totals these tests use,
# total/50 + 1 and total/60 + 1 (or /300 + 1 and /250 + 1) come out the same, so the mark pattern does not
# change. The cell kind is what each pass really selects, and that is measurable.
case("Base_UpdateLabNeeded @ 0x65c7c marks workshop cells instead of lab cells", "test_geoscape", G,
     r"FacilityMarkNeeded\(abFacilityGrid, nBaseIndex, 2,", "FacilityMarkNeeded(abFacilityGrid, nBaseIndex, 3, /* MUTATION: kind 2 */")
case("Base_UpdateStoresNeeded @ 0x65d44 marks containment cells instead of stores cells", "test_geoscape", G,
     r"FacilityMarkNeeded\(abFacilityGrid, nBaseIndex, 6,", "FacilityMarkNeeded(abFacilityGrid, nBaseIndex, 7, /* MUTATION: kind 6 */")
case("Base_UpdateLivingQuartersNeeded @ 0x65e24 budgets living quarters on / 60", "test_geoscape", G,
     r"FacilityBudget\(nSpaceNeeded, 50\)", "FacilityBudget(nSpaceNeeded, 60)")
case("Base_UpdatePsiLabNeeded @ 0x65fc8 budgets psi labs on / 11", "test_geoscape", G,
     r"FacilityBudget\(nPsiSoldiers, 10\)", "FacilityBudget(nPsiSoldiers, 11)")

# ---- the grid walkers ------------------------------------------------------------------------------

case("Base_CountEmptyFacilityCells @ 0x6aee0 weighs a free kind-4 cell 3 and kind-5 2", "test_geoscape", G,
     r"FacilityScoreKind\(abFacilityGrid, nBaseIndex, 4, 2, true\)", "FacilityScoreKind(abFacilityGrid, nBaseIndex, 4, 3, true)")
case("Base_CountBuiltFacilities @ 0x682c8 weighs kind 16 as 1400", "test_geoscape", G,
     r"FacilityScoreKind\(abFacilityGrid, nBaseIndex, 16, 1500, false\)", "FacilityScoreKind(abFacilityGrid, nBaseIndex, 16, 1400, false)")
case("Base_FindFacilityCell @ 0x6ae54 answers 2 where the record answers 1", "test_geoscape", G,
     r"FacilityScoreKind\(abFacilityGrid, nBaseIndex, nKind, 1, true\) != 0 \? 1 : 0",
     "FacilityScoreKind(abFacilityGrid, nBaseIndex, nKind, 1, true) != 0 ? 2 : 0")
case("Base_ClearFacilityGridRow @ 0x6a0e0 adds 11 for a free kind-9 cell", "test_geoscape", G,
     r"nState = \(i8\)\(nState \+ 10\)", "nState = (i8)(nState + 11)")

# ---- the sums and the crew leaf ---------------------------------------------------------------------

case("Base_CountActiveManuf @ 0x68264 through the hoisted call reads the whole 200-entry column", "test_geoscape", G,
     r"pEnv->nManufEntries\)\);", "PUFO_PRODUCT_ENTRIES_PER_BASE)); /* MUTATION: 0x160C4C ignored */")
case("QuantitySumColumn @ 0x68264/0x68198 caps the count at 128 instead of 255", "test_geoscape", G,
     r"nEntries = 255;", "nEntries = 128;")
case("CrewAvailable @ 0x681f4 adds the committed column instead of subtracting it", "test_geoscape", G,
     r"return Base_GetItemQty\(pCrewRec, nBaseIndex\) - QuantitySumColumn",
     "return Base_GetItemQty(pCrewRec, nBaseIndex) + QuantitySumColumn")
case("Base_GetItemQty @ 0x66198/0x661b8 reads the column one higher", "test_geoscape", G,
     r"return \(i32\)pItem->auPerBaseVal\[nBaseIdx\];", "return (i32)pItem->auPerBaseVal[nBaseIdx] + 1;")

# ---- the counters ------------------------------------------------------------------------------------

# An earlier version of this case replaced only the head of the multi-line BaseGrid_GetTypeByte call and broke
# the build (reported as BUILD FAILED, no failing check). Counting the wrong craft is the same row's other arm.
case("Base_CountShipsAtBase @ 0x66090 counts the craft homed elsewhere", "test_geoscape", G,
     r"\(i32\)apCraft\[i\]->bHomeBase == nBaseIndex", "(i32)apCraft[i]->bHomeBase != nBaseIndex")
case("Base_CountSoldiers @ 0x661d8 counts the soldiers NOT assigned to the base", "test_geoscape", G,
     r"apSoldiers\[i\]->nKind == nBaseIndex", "apSoldiers[i]->nKind != nBaseIndex")
case("Base_HasResearchSlotFree @ 0x662bc refuses a need equal to the capacity", "test_geoscape", G,
     r"return nNeeded <= Base_CalcLabCap", "return nNeeded < Base_CalcLabCap")
case("Base_HasWorkshopSlotFree @ 0x66340 refuses a need equal to the capacity", "test_geoscape", G,
     r"return nNeeded <= Base_CalcWorkshopCap", "return nNeeded < Base_CalcWorkshopCap")

# ---- the two cargo sums ------------------------------------------------------------------------------

case("Base_CalcStorageUsed @ 0x6555c charges one slot extra per object", "test_ship", G,
     r"nTotal \+= \(i32\)pObj->bCargoSize \* \(i32\)pObj->auPerBaseVal\[nBase\];",
     "nTotal += (i32)pObj->bCargoSize * (i32)pObj->auPerBaseVal[nBase] + 1;")
case("Base_CalcWeaponStorageUsed @ 0x65b30 charges one slot extra per carried object", "test_ship", S,
     r"nUsed \+= \(i32\)pObj->bCargoSize \* \(i32\)nCarried;",
     "nUsed += (i32)pObj->bCargoSize * (i32)nCarried + 1;")
case("Base_CraftWeaponItemId @ 0x6641c maps store slot 34 to object 106", "test_ship", S,
     r"anSlotObject\[5\] = \{ 56, 57, -1, 105, 58 \}", "anSlotObject[5] = { 56, 57, -1, 106, 58 }")

# ---- the monthly cost --------------------------------------------------------------------------------

case("Base_CalcMonthlyUpkeep @ 0x6a230 prices a type-0/1 craft at 450000", "test_geoscape", G,
     r"anCraftCost\[5\] = \{ 500000, 500000, 700000, 600000, 400000 \}",
     "anCraftCost[5] = { 450000, 450000, 700000, 600000, 400000 }")
