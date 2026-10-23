#!/usr/bin/env python3
"""Part 145 teeth: the nine rows cited in test_tactctl / test_mapcell / test_tactstate / test_los / test_unit / test_turn.

Anchors are given as line LISTS and joined with the target file's own newline -- src/pufo/game/pufo_mapcell.c is
LF-only while src/pufo/game/pufo_tactstate.c and src/pufo/game/pufo_turn.c are CRLF-only, and a hard \n in a
multi-line anchor makes the case SKIP (memory source-eol-mutation-anchors).

Slice:  python -u -I docs/parity/mutations_part145.py 0 3
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

CTL = "src/pufo/game/pufo_tactctl.c"
CELL = "src/pufo/game/pufo_mapcell.c"
TACT = "src/pufo/game/pufo_tactstate.c"
LOS = "src/pufo/game/pufo_los.c"
UNIT = "src/pufo/game/pufo_unit.c"
TURN = "src/pufo/game/pufo_turn.c"


def nl(path):
    return b"\r\n" if b"\r\n" in open(path, "rb").read() else b"\n"


def j(path, *lines):
    return nl(path).join(lines)


CASES = [
    # 0x260c0 GameSpeed_Set: the range gate is `<= PUFO_SPEED_MAX`, and the top speed must still restart.
    ("gamespeed_set: the top speed stops restarting the timers", "test_tactctl", CTL,
     b"    if (nSpeed <= PUFO_SPEED_MAX) {", b"    if (nSpeed < PUFO_SPEED_MAX) {"),

    # 0x3a2ac Tact_SpawnAliens: auStat[0] comes from the unit record's TU field (0x3A93C).
    ("spawnaliens: auStat[0] no longer comes from the record's TU", "test_tactctl", CTL,
     b"pUnit->auStat[0] = (u16)pRec->nTu;", b"pUnit->auStat[0] = (u16)(pRec->nTu + 10);"),

    # 0x26f88 Map_IsoOffsetA: a - 5*dimX + b.
    ("isoffseta: the 5*dimX term becomes 4*dimX", "test_mapcell", CELL,
     b"    return nA - 5 * nDimX + nB;", b"    return nA - 4 * nDimX + nB;"),

    # 0x26fa8 Map_IsoOffsetB: 5*dimY - a + b - 1, the -1 is part of the original.
    ("isoffsetb: the -1 dropped", "test_mapcell", CELL,
     b"    return 5 * nDimY - nA + nB - 1;", b"    return 5 * nDimY - nA + nB;"),

    # 0x27324 GameTick_Step: the sweep skips a cell only when the decayed fields are zero.
    ("gametick_step: the skip gate inverted (flagged cells never decay)", "test_tactstate", TACT,
     b"        if ((pCell->nFlags32 & 0x78u) == 0) {", b"        if ((pCell->nFlags32 & 0x78u) != 0) {"),

    # 0x3242c Map_TraceVoxelOccupancy: the floor mask decides the hit.
    ("tracevoxel: the floor mask test inverted", "test_los", LOS,
     j(LOS, b"        if (Los_TestRowBit(pTables->pLoftemps, pTables->nLoftemps, nRow, nXBit)) {",
       b"            return LOS_HIT_FLOOR;"),
     j(LOS, b"        if (!Los_TestRowBit(pTables->pLoftemps, pTables->nLoftemps, nRow, nXBit)) {",
       b"            return LOS_HIT_FLOOR;")),

    # 0x49eb8 Unit_bIsOperative: status == 0.
    ("isoperative: status != 0 reads as operative", "test_unit", UNIT,
     b"    return pUnit->bStatus == UNIT_STATUS_OPERATIVE;",
     b"    return pUnit->bStatus != UNIT_STATUS_OPERATIVE;"),

    # 0x49410 Soldier_Generate: auStat[0] = RandU16(0x0A) + 50.
    ("soldier_generate: auStat[0] band shifted down by 10", "test_unit", UNIT,
     b"pUnit->auStat[0] = (u16)(Game_RandU16(0x0Au) + 50u);",
     b"pUnit->auStat[0] = (u16)(Game_RandU16(0x0Au) + 40u);"),

    # 0x4d950 Tact_ProcessActionPhase: draining the opportunity-fire list advances to phase 2.
    ("actionphase: the phase-1 tail skips straight to the AI phase", "test_turn", TURN,
     j(TURN, b"            if (pCtx->nOppFireCount + 1 == 1) {", b"                pCtx->nPhase = PUFO_PHASE_RESTORE;"),
     j(TURN, b"            if (pCtx->nOppFireCount + 1 == 1) {", b"                pCtx->nPhase = PUFO_PHASE_ENEMY_AI;")),
]

if __name__ == "__main__":
    i0 = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    i1 = int(sys.argv[2]) if len(sys.argv) > 2 else len(CASES)
    for name, target, path, old, new in CASES[i0:i1]:
        run_case(name, target, path, old, new)
