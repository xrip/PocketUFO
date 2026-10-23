#!/usr/bin/env python3
"""Part 146 teeth: the fifteen rows whose test already carried a Check naming them.

Multi-line anchors join with the target file's own newline (memory source-eol-mutation-anchors).
Slice:  python -u -I docs/parity/mutations_part146.py 0 4
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

CTL = "src/pufo/game/pufo_tactctl.c"
STEP = "src/pufo/game/pufo_step.c"
CELL = "src/pufo/game/pufo_mapcell.c"
TACT = "src/pufo/game/pufo_tactstate.c"
UNIT = "src/pufo/game/pufo_unit.c"
SHIP = "src/pufo/game/pufo_ship.c"
TUI = "src/pufo/game/pufo_tactui.c"
ROSTER = "src/pufo/game/pufo_roster.c"


def nl(path):
    return b"\r\n" if b"\r\n" in open(path, "rb").read() else b"\n"


def j(path, *lines):
    return nl(path).join(lines)


MATCH = b"        if (Soldier_MatchesSmall(apSoldiers[i], nType)) {"

CASES = [
    # 0x275fc MapCursor_IsValid: the x bound is >= 10*dimX.
    ("cursor_isvalid: the x bound accepts the cell past the map", "test_tactctl", CTL,
     b"    if (pCursor->nX >= PUFO_CELL_SIZE * (i32)pMap->nDimX) {",
     b"    if (pCursor->nX > PUFO_CELL_SIZE * (i32)pMap->nDimX) {"),

    # 0x2778c Map_ProbeStepOccupants: the step below the map needs a floor.
    ("probestep: the missing-floor refusal turned into an and", "test_step", STEP,
     b"            if (pBelow == NULL || pBelow->pFloorSpr == NULL) {",
     b"            if (pBelow == NULL && pBelow->pFloorSpr == NULL) {"),

    # 0x2bc9c Map_IsValidCell: a negative x is out of range.
    ("isvalidcell: negative x no longer rejected", "test_mapcell", CELL,
     b"    if (nX < 0) {", b"    if (nX < -1) {"),

    # 0x2bd00 Map_QueueDoorAction: direction 0 is the first branch.
    ("queuedoor: direction 0 matches no branch", "test_tactstate", TACT,
     b"    if (nDir == 0) {", b"    if (nDir == 8) {"),

    # 0x2e59c Unit_GetExtraSpr: base then facing.
    ("getextraspr: the base and the facing index swapped", "test_unit", UNIT,
     j(UNIT, b"    return Rec_GetExtraSpr(pUnit->multiOrAnim.anim.uExtraSprBase,",
         b"                           (u8)pUnit->multiOrAnim.anim.uFacingResetH);"),
     j(UNIT, b"    return Rec_GetExtraSpr(pUnit->multiOrAnim.anim.uFacingResetH,",
         b"                           (u8)pUnit->multiOrAnim.anim.uExtraSprBase);")),

    # 0x36830 Ship_RemoveWithCargo: the panel-list cleanup.
    ("ship_remove: the strateg-list cleanup skipped", "test_geoscape", SHIP,
     b"    if (pEnv->pStrategList != NULL) {", b"    if (pEnv->pStrategList == NULL) {"),

    # 0x36d68 Unit_CanStepExitDir: probe kind 2 is the blocking-wall probe.
    ("canstepexit: the blocking probe kind changed", "test_unit", UNIT,
     b"        if (s_aExitProbe[nDir][v6][2] == 2) {", b"        if (s_aExitProbe[nDir][v6][2] == 1) {"),

    # 0x3e620 TactItem_GetHandSlot: id 14 is the left hand.
    ("gethandslot: id 14 stops being the left hand", "test_tactui", TUI,
     b"        if (nActionId == 14) {", b"        if (nActionId == 15) {"),

    # 0x3e69c TactItem_GetEquipDelta: ids 14,15,16 all equip.
    ("getequipdelta: id 16 stops equipping", "test_tactui", TUI,
     b"            if (nActionId == 14 || nActionId == 15 || nActionId == 16) {",
     b"            if (nActionId == 14 || nActionId == 15) {"),

    # 0x46fe0 Soldier_PickBestScoreAmong: the best score is the largest.
    ("pickbestscore: the comparison inverted", "test_roster", ROSTER,
     b"        if (nBest < nScore) {", b"        if (nBest > nScore) {"),

    # 0x47164 Soldier_FindNextSmallByType: the first match wins.
    ("findnextsmall: the match test negated", "test_roster", ROSTER,
     j(ROSTER, MATCH, b"            return i;"),
     j(ROSTER, b"        if (!Soldier_MatchesSmall(apSoldiers[i], nType)) {", b"            return i;")),

    # 0x471f8 Soldier_CountSmallByType: every match counts.
    ("countsmall: the match test negated", "test_roster", ROSTER,
     j(ROSTER, MATCH, b"            nCount += 1;"),
     j(ROSTER, b"        if (!Soldier_MatchesSmall(apSoldiers[i], nType)) {", b"            nCount += 1;")),

    # 0x48138 Unit_SpotCellOccupant: same status on the same side is a spot.
    ("spotcell: the equal-status spot test negated", "test_unit", UNIT,
     j(UNIT, b"        if (nOccStatus == bStatus) {", b"            return true;"),
     j(UNIT, b"        if (nOccStatus != bStatus) {", b"            return true;")),

    # 0x4834c Unit_ScanVisibleUnits: the scan range in cells.
    ("scanvisible: every unit beyond distance zero is out of range", "test_unit", UNIT,
     b"        if ((i32)sqrt((double)nDx * nDx + (double)nDy * nDy) > PUFO_SCAN_RANGE_CELLS) {",
     b"        if ((i32)sqrt((double)nDx * nDx + (double)nDy * nDy) > 0) {"),

    # 0x49ef0 Unit_RecalcInventoryStat: the burden starts at 1.
    ("recalcinvburden: the base burden is 2", "test_unit", UNIT,
     b"    pUnit->uInvBurden = 1;", b"    pUnit->uInvBurden = 2;"),
]

if __name__ == "__main__":
    i0 = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    i1 = int(sys.argv[2]) if len(sys.argv) > 2 else len(CASES)
    for name, target, path, old, new in CASES[i0:i1]:
        run_case(name, target, path, old, new)
