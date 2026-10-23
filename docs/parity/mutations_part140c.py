#!/usr/bin/env python3
"""Part 140, last case: Ship_ClearInterceptByStrategIdx @ 0x5a5b8's selection gate (0x5A614).

The first two attempts were inert for the same reason: the fixture had no craft intercepting anything, so the
arm that does the work never ran and any mutation inside it was invisible. tests/test_ship.c now arms one
craft on the cleared index and leaves another on a different index, which is what makes the gate observable.

Run from the repo root:  python -I docs/parity/mutations_part140c.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

SHIP = "src/pufo/game/pufo_ship.c"

data = open(SHIP, "rb").read()
eol = b"\r\n" if b"\r\n" in data else b"\n"
lines = [l.decode("utf-8") for l in data.split(eol)]
for line in lines:
    if re.search(r"pShip->nStrategIdx == nStrategIdx\) \{ /\* 0x5A614 \*/", line):
        new = re.sub(r"pShip->nStrategIdx == nStrategIdx",
                     "pShip->nStrategIdx == nStrategIdx + 1 /* MUTATION */", line, count=1)
        run_case("Ship_ClearInterceptByStrategIdx @ 0x5a5b8 selects one index higher",
                 "test_ship", SHIP, line.encode("utf-8"), new.encode("utf-8"))
        break
else:
    print("SKIP: anchor not found")
