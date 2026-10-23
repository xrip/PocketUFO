#!/usr/bin/env python3
"""Part 139, the last case: Geoscape_OnMapClick_Thunk @ 0x857e8.

It is the only row of the batch that is not a copy of something already measured, so it gets its own run.
Run from the repo root:  python -I docs/parity/mutations_part139c.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

BASEUI = "src/pufo/game/pufo_baseui.c"

data = open(BASEUI, "rb").read()
eol = b"\r\n" if b"\r\n" in data else b"\n"
lines = [l.decode("utf-8") for l in data.split(eol)]
for line in lines:
    if re.search(r"return pHooks->pfnMapClick\(pHooks->pUser\);", line):
        new = "        return 0; /* MUTATION: the forward at 0x857EC is gone */"
        run_case("Geoscape_OnMapClick_Thunk @ 0x857e8 stops forwarding the result", "test_baseui",
                 BASEUI, line.encode("utf-8"), new.encode("utf-8"))
        break
else:
    print("SKIP: anchor not found")
