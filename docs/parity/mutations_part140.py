#!/usr/bin/env python3
"""Part 140 mutations: the ship / BaseRec / strateg-list half of the remaining `strat` rows.

The helper reads each file's own end-of-line marker (pufo_baseui.c is LF-only, pufo_ship.c,
pufo_strateg.c and pufo_geoscape.c are CRLF-only) -- see memory `source-eol-mutation-anchors`.

Run from the repo root:  python -I docs/parity/mutations_part140.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

SHIP = "src/pufo/game/pufo_ship.c"
STRAT = "src/pufo/game/pufo_strateg.c"
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


# ---- the two base-record leaves --------------------------------------------------------------------

# The site byte BaseRec_Ctor receives is asserted by no check, so a +1 there measured 0 failing checks; the
# SLOT it receives is asserted ("the ctor zeroed slot 0 in every object", "and left the other slots alone").
case("BaseRec_Create @ 0x32c88 hands the ctor the next roster slot", "test_geoscape", SHIP,
     r"BaseRec_Ctor\(pNew, nBaseCount, bSiteId, apObjects, nObjectCount\);",
     "BaseRec_Ctor(pNew, nBaseCount + 1, bSiteId, apObjects, nObjectCount); /* MUTATION */")
case("BaseRec_AddItemQty @ 0x556e4 adds one more than the delta", "test_geoscape", GEO,
     r"pItem->auPerBaseVal\[nSlot\] = \(u16\)\(\(i32\)pItem->auPerBaseVal\[nSlot\] \+ nDelta\);",
     "pItem->auPerBaseVal[nSlot] = (u16)((i32)pItem->auPerBaseVal[nSlot] + nDelta + 1); /* MUTATION */")

# ---- the ship family -------------------------------------------------------------------------------

case("Ship_Ctor @ 0x45c20 keeps type 2 for an out-of-range craft type", "test_save", SHIP,
     r"bType = 1; /\* 0x45C44", "bType = 2; /* MUTATION: 0x45C44")
case("Ship_TickRefuelAtBase @ 0x45f04 refuels the small craft at +2", "test_ship", SHIP,
     r"pShip->nFuel = \(i16\)\(nFuel \+ 1\);", "pShip->nFuel = (i16)(nFuel + 2); /* MUTATION */")
# The selection gate (0x5A614) is only observable once a fixture actually has a craft on the cleared index --
# tests/test_ship.c arms one and leaves another on a different index. Dropping the 0x5A62C count refresh was
# measured inert for the same reason the old fixture was: the arm never ran.
case("Ship_ClearInterceptByStrategIdx @ 0x5a5b8 selects one index higher", "test_ship", SHIP,
     r"pShip->nStrategIdx == nStrategIdx\) \{ /\* 0x5A614 \*/",
     "pShip->nStrategIdx == nStrategIdx + 1 /* MUTATION */")
# Ship_ClearWaypointIntercept writes the intercept clear TWICE in the original (0x5A88C after the waypoint
# removal, 0x5A8B8 unconditionally at the end), so dropping either is inert by construction; the store only
# this function makes is the timer parked at 0xFFFF (0x5A8B0).
case("Ship_ClearWaypointIntercept @ 0x5a830 restarts the timer instead of parking it", "test_ship", SHIP,
     r"pEntry->nTimer = 0xFFFF; /\* 0x5A8B0: parked, not restarted \*/",
     "pEntry->nTimer = 0; /* MUTATION */")
case("Ship_FindIndexByStrategType @ 0x5a92c compares the craft id one higher", "test_baseui", STRAT,
     r"pItem->nDisplayId == pWorld->apCraft\[i\]->bId\) \{",
     "pItem->nDisplayId == (i32)pWorld->apCraft[i]->bId + 1) { /* MUTATION */")
case("Ship_ClearExpiredIntercepts @ 0x8128c does not pause the clock", "test_strateg", STRAT,
     r"pWorld->pTime->bPaused = 1; /\* GameTime_Pause @ 0x5d6ac, 0x81340 \*/",
     "pWorld->pTime->bPaused = 0; /* MUTATION */")

# ---- the strateg-list leaves -----------------------------------------------------------------------

case("StrategList_AddItem @ 0x826e8 stores type 0", "test_strateg", STRAT,
     r"pItem->nType   = \(u8\)nType;   /\* 0x82728 \*/", "pItem->nType   = 0; /* MUTATION */")
case("StrategEntry_InitTravel @ 0x7f60c leaves the destination at the start", "test_strateg", STRAT,
     r"pEntry->nDestX      = nX1;    /\* 0x7F618 \*/", "pEntry->nDestX      = nX0; /* MUTATION */")
