#!/usr/bin/env python3
"""Part 144 teeth: the eight tactstate rows.

The ctor row gets three cases because the port's `TactState_ctor` does BOTH a whole-block memset and the three
field clears the original writes, and the relationship between them is a claim: each explicit clear must carry
weight when the memset is gone, and the memset must be what carries the cursor. Case 2 is EXPECTED to be inert
and says so.

Slice:  python -u -I docs/parity/mutations_part144.py 0 5
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

PATH = "src/pufo/game/pufo_tactstate.c"

# The file is CRLF-only (2854 CRLF, 0 bare LF). Anchors built with a hard \n match nothing and the case SKIPs,
# which reads as "no teeth" if nobody looks at the SKIP line -- so the newline comes from the file itself.
NL = b"\r\n" if b"\r\n" in open(PATH, "rb").read() else b"\n"

MEMSET = b"    memset(pState, 0, sizeof(*pState));" + NL
THREE = (b"    pState->nQueueLen    = 0;" + NL
         + b"    pState->bQueueActive = 0;" + NL
         + b"    pState->bUnk70103    = 0;" + NL)

CASES = [
    # 0x4f2d4 TactState_ctor: the memset is what clears nQueueCursor, which the original never writes.
    ("ctor: whole-block memset removed (cursor check must fail, the three field checks must pass)",
     "test_tactstate", PATH, MEMSET, b""),

    # 0x4f2d4: the three explicit clears, with the memset still in place -- expected INERT, and that is the point.
    ("ctor: the three original field clears dropped, memset kept (EXPECTED inert: the memset covers them)",
     "test_tactstate", PATH, THREE, b""),

    # 0x4f2d4: nothing cleared at all.
    # An empty body makes the formal parameter unreferenced -> C4100 under /W4 /WX -> "BUILD FAILED", which is a
    # toolchain answer, not a coverage answer. The (void) keeps the parameter named so the check can fail instead.
    ("ctor: memset and the three clears both removed", "test_tactstate", PATH, MEMSET + THREE,
     b"    (void)pState;" + NL),

    # 0x49b84 TactState_EnqueueTurn: cmd 0.
    ("enqueue_turn: writes STEP instead of TURN", "test_tactstate", PATH,
     b"    pEntry->nCmd    = TACTCMD_TURN;", b"    pEntry->nCmd    = TACTCMD_STEP;"),

    # 0x49d5c TactState_EnqueueStep: the delta word.
    ("enqueue_step: the delta word is not stored", "test_tactstate", PATH,
     b"    pEntry->nDelta  = nDelta;" + NL, b"    (void)nDelta;" + NL),

    # 0x4a37c TactState_EnqueueRecover: cmd 11.
    ("enqueue_recover: cmd 11 becomes 10", "test_tactstate", PATH,
     b"    pEntry->nCmd  = TACTCMD_RECOVER;", b"    pEntry->nCmd  = TACTCMD_CLEAR_AIM;"),

    # 0x4a758 TactState_EnqueueClearAim: cmd 10.
    ("enqueue_clearaim: cmd 10 becomes 11", "test_tactstate", PATH,
     b"    pEntry->nCmd  = TACTCMD_CLEAR_AIM;", b"    pEntry->nCmd  = TACTCMD_RECOVER;"),

    # 0x4f300 TactState_EnqueueEndMark: cmd 12.
    ("enqueue_endmark: cmd 12 becomes 10", "test_tactstate", PATH,
     b"    pState->aQueue[pState->nQueueLen].nCmd = TACTCMD_END_MARK;",
     b"    pState->aQueue[pState->nQueueLen].nCmd = TACTCMD_CLEAR_AIM;"),

    # 0x4a3d4 Unit_EnqueueIfApSpent: the gate is uTuSpent >= uHp, so the boundary must flash.
    ("ifapspent: the boundary uTuSpent == uHp stops flashing", "test_tactstate", PATH,
     b"    if (pUnit->uTuSpent < (u32)pUnit->uHp) {", b"    if (pUnit->uTuSpent <= (u32)pUnit->uHp) {"),

    # 0x4b2f4 TactState_EnqueueThrow: the TU gate.
    ("enqueue_throw: the TU gate removed", "test_tactstate", PATH,
     b"    if ((i32)pUnit->uTu < nTuCost) {" + NL + b"        return false;" + NL + b"    }" + NL
     + b"    if (pfnFaceToward != NULL) {",
     b"    if (pfnFaceToward != NULL) {"),
]

if __name__ == "__main__":
    i0 = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    i1 = int(sys.argv[2]) if len(sys.argv) > 2 else len(CASES)
    for name, target, path, old, new in CASES[i0:i1]:
        run_case(name, target, path, old, new)
