#!/usr/bin/env python3
"""Part 149 teeth: the two signed-off src fixes.
Run from the repo root:  python -I docs/parity/mutations_part149.py  (tee to build/crops/mut149.log)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mutate_run import run_case  # noqa: E402

TUI = "src/pufo/game/pufo_tactui.c"
MAP = "src/pufo/game/pufo_mapcell.c"


def nl(path):
    return b"\r\n" if b"\r\n" in open(path, "rb").read() else b"\n"


CASES = [
    # 1. put the wrong caption back: the record-index check must notice.
    ("caption back to record 6 (out of ammo)", "test_tactui", TUI,
     b"    Ui_Status(pHooks, PUFO_TXT_OUT_OF_TU);   /* 0x17A54: StatusMsg_Push with aTextRec1_OutOfTime, "
     b"record 1 */",
     b"    Ui_Status(pHooks, PUFO_TXT_OUT_OF_AMMO);"),
    # 2. do what the old comment claimed: leave the removed tail's link pointing at the removed node.
    ("ListRemove keeps the tail link (the old comment's claim)", "test_mapcell", MAP,
     b"    if (pPrev->pNext != NULL) {\n        pPrev->pNext = pNode->pNext;\n    }".replace(b"\n", nl(MAP)),
     b"    if (pPrev->pNext != NULL && pNode->pNext != NULL) {\n        pPrev->pNext = pNode->pNext;\n    }"
     .replace(b"\n", nl(MAP))),
]

for label, target, path, old, new in CASES:
    data = open(path, "rb").read()
    n = data.count(old)
    if n != 1:
        print(f"SKIP {label}: anchor occurs {n} times in {path}")
        continue
    run_case(label, target, path, old, new)
