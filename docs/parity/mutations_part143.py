#!/usr/bin/env python3
"""Part 143 teeth: the seven MapCell rows, one mutation per row (plus the walk branch).

Every case edits src/pufo/game/pufo_mapcell.c at the line the cited check reads, runs
tests/test_mapcell, and restores the bytes. Slice the list to keep a chunk inside the run
cap:  python -u -I docs/parity/mutations_part143.py 0 4
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

PATH = "src/pufo/game/pufo_mapcell.c"

CASES = [
    # 0x5487c MapCell_HasOccupant: the original refuses bStatus >= 3.
    ("hasoccupant: status 3 counted as an occupant", "test_mapcell", PATH,
     b"return pBytes[PUFO_SOLDIER_BSTATUS_OFF] < 3u;",
     b"return pBytes[PUFO_SOLDIER_BSTATUS_OFF] < 4u;"),

    # 0x548a0 MapCell_SetBFlags57_3: the set value is 8 (bit3), not 4.
    ("setbflags57_3: writes bit2 instead of bit3", "test_mapcell", PATH,
     b"(8 * (bSet ? 1 : 0))",
     b"(4 * (bSet ? 1 : 0))"),

    # 0x548c0 MapCell_SetBFlags29_4: the set bit is 0x10 (bit4).
    ("setbflags29_4: sets bit5 instead of bit4", "test_mapcell", PATH,
     b"pCell->bFlags29 = (u8)(pCell->bFlags29 | 0x10u);",
     b"pCell->bFlags29 = (u8)(pCell->bFlags29 | 0x20u);"),

    # 0x548d0 MapCell_SetTileSprites: a NULL sprite argument must keep the old sprite.
    ("settilesprites: a NULL floor clears the stored floor sprite", "test_mapcell", PATH,
     b"    if (pFloor != NULL) {\n        pCell->pFloorSpr = pFloor;\n    }",
     b"    pCell->pFloorSpr = pFloor;"),

    # 0x54a90 MapCell_Reset: the bFlags57[0] mask is 0xF3; 0x20 belongs to bFlags29.
    ("reset: bFlags57[0] keeps bit2 (0xF7 mask)", "test_mapcell", PATH,
     b"pCell->bFlags57[0] = (u8)(pCell->bFlags57[0] & 0xF3);",
     b"pCell->bFlags57[0] = (u8)(pCell->bFlags57[0] & 0xF7);"),

    # 0x54b14 MapCell_ListInsertByPri: head >= new splices after the head.
    ("listinsertbypri: the priority comparison is inverted", "test_mapcell", PATH,
     b"pHead->pObj->bListPri >= pNode->pObj->bListPri",
     b"pHead->pObj->bListPri <= pNode->pObj->bListPri"),

    # 0x54b58 MapCell_ListRemove, head branch: the head pointer moves to the successor.
    ("listremove: the head branch does not advance the list", "test_mapcell", PATH,
     b"pCell->pItemList = pNode->pNext;",
     b"pNode->pNext = NULL;"),

    # 0x54b58 MapCell_ListRemove, walk branch: the walk stops at the node, not only at the end.
    ("listremove: the walk ignores the node it is looking for", "test_mapcell", PATH,
     b"while (pPrev->pNext != pNode && pPrev->pNext != NULL) {",
     b"while (pPrev->pNext != NULL) {"),
]

if __name__ == "__main__":
    i0 = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    i1 = int(sys.argv[2]) if len(sys.argv) > 2 else len(CASES)
    for name, target, path, old, new in CASES[i0:i1]:
        run_case(name, target, path, old, new)
