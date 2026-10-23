#!/usr/bin/env python3
"""Part 151 teeth: one mutation per closed `tact` row, each inside the ported function the row names.
Run in chunks from the repo root (never kill a chunk mid-case). 24 cases: 22 rows, 0x2b534 has three.
    python -u -I docs/parity/mutations_part151.py 1 3
    python -u -I docs/parity/mutations_part151.py 4 6
    python -u -I docs/parity/mutations_part151.py 7 24
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mutate_run import run_case  # noqa: E402

TUI = "src/pufo/game/pufo_tactui.c"
ROS = "src/pufo/game/pufo_roster.c"
PAP = "src/pufo/game/pufo_path.c"
CTL = "src/pufo/game/pufo_tactctl.c"
END = "src/pufo/game/pufo_tactend.c"
UNI = "src/pufo/game/pufo_unit.c"
CMB = "src/pufo/game/pufo_combat.c"
TRN = "src/pufo/game/pufo_turn.c"
STA = "src/pufo/game/pufo_tactstate.c"
AEA = "src/pufo/game/pufo_enemyai.c"
TPL = "src/pufo/game/pufo_template.c"


def nl(path):
    return b"\r\n" if b"\r\n" in open(path, "rb").read() else b"\n"


def j(path, *lines):
    return nl(path).join(lines)


CASES = [
    ("0x18260 ConfirmSpecial9: the mode written is 8, not 9", "test_tactui", TUI,
     b"pUi->nActionMode = 9;", b"pUi->nActionMode = 8;"),
    ("0x197b8 NextFiltered: the walk direction flipped", "test_roster", ROS,
     b"Roster_WalkFiltered(pRoster, true, Filter_Accept, NextFiltered_Visit",
     b"Roster_WalkFiltered(pRoster, false, Filter_Accept, NextFiltered_Visit"),
    ("0x19708 PrevFiltered: the walk direction flipped", "test_roster", ROS,
     b"Roster_WalkFiltered(pRoster, false, Filter_Accept, NULL, &ctx);",
     b"Roster_WalkFiltered(pRoster, true, Filter_Accept, NULL, &ctx);"),
    ("0x2b534 EnqueueCell: the appended slot is NULL instead of the cell", "test_path", PAP,
     b"    apList[*pnCount] = pCell;", b"    apList[*pnCount] = NULL;"),
    ("0x2b534 EnqueueCell: the already-queued refusal turned into an acceptance", "test_path", PAP,
     j(PAP, b"        if (apList[i] == pCell) {", b"            return false; /* already queued */"),
     j(PAP, b"        if (apList[i] == pCell) {", b"            return true; /* mutated */")),
    ("0x2b534 EnqueueCell: the queued mark cleared instead of set", "test_path", PAP,
     b"pCell->bFlags44 = (u8)(pCell->bFlags44 | 2u);",
     b"pCell->bFlags44 = (u8)(pCell->bFlags44 & ~2u);"),
    ("0x2b604 ExpandCell: the floor-sprite gate inverted", "test_path", PAP,
     b"if (!bFlag && pCur->pFloorSpr == NULL) {", b"if (!bFlag && pCur->pFloorSpr != NULL) {"),
    ("0x3331c CameraBounds: the map-width bound widened by one", "test_tactctl", CTL,
     b"*pnX1 = PUFO_CELLS_PER_TILE * (i32)pCtx->nMapDimX;",
     b"*pnX1 = PUFO_CELLS_PER_TILE * (i32)pCtx->nMapDimX + 1;"),
    ("0x33bf0 MarkAllSoldiersSeen: the link counter credited 2 instead of 1", "test_roster", ROS,
     b"*puCounter = (u16)(*puCounter + 1);", b"*puCounter = (u16)(*puCounter + 2);"),
    ("0x33c80 TallyItemRecovery: the box's upper bound made inclusive (the original is exclusive "
     "at 0x33EB8/0x33EC4)", "test_tactend", END,
     j(END, b"        for (nY = nY0; nY < nY1; ++nY) {", b"            for (nX = nX0; nX < nX1; ++nX) {"),
     j(END, b"        for (nY = nY0; nY < nY1; ++nY) {", b"            for (nX = nX0; nX <= nX1; ++nX) {")),
    ("0x36b8c RemoveAt: the count not decremented", "test_unit", UNI,
     b"return nCount - 1;", b"return nCount;"),
    ("0x47280 PromoteByTypeScores: the rookie gate inverted (the 0x47654 arm)", "test_roster", ROS,
     b"if (Soldier_MissionScore(pUnit) <= PROMOTE_SCORE_ROOKIE) {",
     b"if (Soldier_MissionScore(pUnit) >= PROMOTE_SCORE_ROOKIE) {"),
    ("0x476c8 Unit_ctor: the whole-struct zero fill replaced by 0xFF", "test_unit", UNI,
     b"    memset(pUnit, 0, sizeof(*pUnit));", b"    memset(pUnit, 0xFF, sizeof(*pUnit));"),
    ("0x478e4 SelectBestFireMode: the right-hand preference inverted", "test_combat", CMB,
     b"if (nCostR < 0xFFFF) {", b"if (nCostR > 0xFFFF) {"),
    ("0x48028 ScanOpportunityFire: the count seeded at 1 instead of cleared", "test_turn", TRN,
     b"    pCtx->nOppFireCount = 0;", b"    pCtx->nOppFireCount = 1;"),
    ("0x4978c UpdateAnimPose: the dead pose is 19, not 18", "test_unit", UNI,
     b"pUnit->nAnimPose = 18;", b"pUnit->nAnimPose = 19;"),
    ("0x49bf0 FacingToward: same-cell AND loosened to OR", "test_unit", UNI,
     b"if (nToX == nFromX && nToY == nFromY) {", b"if (nToX == nFromX || nToY == nFromY) {"),
    ("0x49e60 EnqueueStandFromKneel: the command written is 8, not 7", "test_tactstate", STA,
     b"pEntry->nCmd  = TACTCMD_STAND_FROM_KNEEL;", b"pEntry->nCmd  = 8;"),
    ("0x4ab98 EnqueueRandomTurn: the RNG draw skipped", "test_enemyai", AEA,
     b"nFacing = pHooks->pfnRandU16(PUFO_ENEMYAI_FACING_MAX, pHooks->pUser);", b"nFacing = 0;"),
    ("0x4b420 EnqueueActionOrStatus205: the command written is 19, not 18", "test_tactstate", STA,
     b"pEntry->nCmd    = (u8)TACTCMD_ACTION18;", b"pEntry->nCmd    = (u8)(TACTCMD_ACTION18 + 1);"),
    ("0x4b6a4 TryReloadClip: the capacity test inverted", "test_tactui", TUI,
     b"if (pItem->nAmmo == 0 && bWeaponAmmoCap != 255", b"if (pItem->nAmmo == 0 && bWeaponAmmoCap == 255"),
    ("0x4b928 MultiTargetTypeMatch: the role read from the candidate instead of the other", "test_enemyai", AEA,
     b"return EnemyAi_MultiTargetRoleMatch(pUnit, (u8)pOther->nMultiTarget);",
     b"return EnemyAi_MultiTargetRoleMatch(pUnit, (u8)pUnit->nMultiTarget);"),
    ("0x4c9f8 TactCtx_ctor: the zero fill replaced by 0xFF", "test_turn", TRN,
     b"    memset(pCtx, 0, sizeof(*pCtx));", b"    memset(pCtx, 0xFF, sizeof(*pCtx));"),
    ("0x54ba4 MapTemplate_Load: the wrong extension asked for", "test_template", TPL,
     b'pufo_strcat(szName, ".ma1", sizeof(szName))', b'pufo_strcat(szName, ".ma2", sizeof(szName))'),
]

lo = int(sys.argv[1]) if len(sys.argv) > 1 else 1
hi = int(sys.argv[2]) if len(sys.argv) > 2 else len(CASES)

for label, target, path, old, new in CASES[lo - 1:hi]:
    data = open(path, "rb").read()
    n = data.count(old)
    if n != 1:
        print(f"SKIP {label}: anchor occurs {n} times in {path}")
        continue
    run_case(label, target, path, old, new)
