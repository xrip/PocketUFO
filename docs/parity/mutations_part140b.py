#!/usr/bin/env python3
"""Part 140 reruns: the four cases the first run skipped or measured inert.

  - BaseRec_AddItemQty and Ship_ClearInterceptByStrategIdx: the anchors named lines that do not exist in the
    port (a guessed store form, and `);` where the source has `) {`).
  - BaseRec_Create: the site byte the ctor receives is not asserted by any check, so a +1 there is invisible.
    The slot the ctor receives IS asserted -- "the ctor zeroed slot 0 in every object" and "and left the
    other slots alone" -- so that is the mutation that has teeth.
  - Ship_ClearWaypointIntercept: NOT a weak check. The original writes the intercept clear twice -- 0x5A88C
    after the waypoint removal and 0x5A8B8 unconditionally at the end -- so dropping either is inert in the
    original too (decompile 0x5a830). The teeth come from the store only this function makes: the proximity
    timer parked at 0xFFFF (0x5A8B0) instead of restarted at 0.

Run from the repo root:  python -I docs/parity/mutations_part140b.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

SHIP = "src/pufo/game/pufo_ship.c"
GEO = "src/pufo/game/pufo_geoscape.c"


def read_lines(path):
    data = open(path, "rb").read()
    eol = b"\r\n" if b"\r\n" in data else b"\n"
    return [l.decode("utf-8") for l in data.split(eol)], eol


def case(label, test, path, pattern, repl):
    ls, eol = read_lines(path)
    for line in ls:
        if re.search(pattern, line):
            new = re.sub(pattern, repl, line, count=1)
            if new == line:
                print(f"SKIP {label}: substitution changed nothing")
                return
            run_case(label, test, path, line.encode("utf-8"), new.encode("utf-8"))
            return
    print(f"SKIP {label}: anchor {pattern!r} not found in {path}")


case("BaseRec_Create @ 0x32c88 hands the ctor the next roster slot", "test_geoscape", SHIP,
     r"BaseRec_Ctor\(pNew, nBaseCount, bSiteId, apObjects, nObjectCount\);",
     "BaseRec_Ctor(pNew, nBaseCount + 1, bSiteId, apObjects, nObjectCount); /* MUTATION */")
case("BaseRec_AddItemQty @ 0x556e4 adds one more than the delta", "test_geoscape", GEO,
     r"pItem->auPerBaseVal\[nSlot\] = \(u16\)\(\(i32\)pItem->auPerBaseVal\[nSlot\] \+ nDelta\);",
     "pItem->auPerBaseVal[nSlot] = (u16)((i32)pItem->auPerBaseVal[nSlot] + nDelta + 1); /* MUTATION */")
case("Ship_ClearInterceptByStrategIdx @ 0x5a5b8 never re-reads the ship count", "test_ship", SHIP,
     r"nShipCount = \(i32\)\*pEnv->pnShipCount; /\* 0x5A62C \*/",
     "nShipCount = 0; /* MUTATION: the 0x5A62C refresh is gone */")
case("Ship_ClearWaypointIntercept @ 0x5a830 restarts the timer instead of parking it", "test_ship", SHIP,
     r"pEntry->nTimer = 0xFFFF; /\* 0x5A8B0: parked, not restarted \*/",
     "pEntry->nTimer = 0; /* MUTATION */")
