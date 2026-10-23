#!/usr/bin/env python3
"""Part 138 rerun: the five cases the first run got wrong, with the corrected anchors.

The first run (build/crops/mut138.log) measured 23 cases / 98 failing checks and found
  - 2 SKIPs: the GameTime_Unpause anchor matched two lines in pufo_clock.c, and StrategList_RegisterAtexit
    lives in pufo_baseui.c, not in pufo_strateg.c;
  - 3 inert cases (0 failing checks): the OnZoomOut sample was not checked at all (the test read the
    third zoom-IN sample while its message said zoom-out), the "ctor" mutation added a cast instead of
    changing a store, and dropping PtrArray_DeleteAll leaves a leak no check reads.
Each case here is the corrected version of one of those five. Run from the repo root:
  python -I docs/parity/mutations_part138b.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

STRAT = "src/pufo/game/pufo_strateg.c"
CLOCK = "src/pufo/game/pufo_clock.c"
BASEUI = "src/pufo/game/pufo_baseui.c"


def anchor(path, pattern):
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


case("GameTime_Unpause @ 0x5d6c0 sets the pause byte instead of clearing it", "test_geoscape", CLOCK,
     r"pTime->bPaused = 0; /\* 0x5d6c8", "pTime->bPaused = 1; /* MUTATION: 0x5d6c8")
case("Smap_OnZoomOut @ 0x84ab8 plays sample 18", "test_strateg", STRAT,
     r"pfnPlay\(17, pUser\); /\* 0x84AC4 \*/", "pfnPlay(18, pUser); /* MUTATION */")
case("StrategList_ctor @ 0x83890 drops the item-pointer store", "test_strateg", STRAT,
     r"PtrArray_ctor\(pList\); /\* 0x83890", "pList->nCount = 0; /* MUTATION: 0x83890")
case("StrategList_Clear @ 0x838ac drops the 0x838c8 empty-out", "test_strateg", STRAT,
     r"PtrArray_Clear\(pList\);", "(void)pList; /* MUTATION: 0x838c8 dropped */")
# The first form of this case (`return -1;`) dropped the only reference to the static thunk and /W4 /WX
# failed the build (C4505) -- reported as BUILD FAILED, not as a failing check. Keeping the call and
# changing only the returned value is the build-safe version.
case("StrategList_RegisterAtexit @ 0x838a0 reports a failed registration", "test_baseui", BASEUI,
     r"return atexit\(StrategList_ClearAtexitThunk\);",
     "(void)atexit(StrategList_ClearAtexitThunk); return -1; /* MUTATION */")
