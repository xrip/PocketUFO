#!/usr/bin/env python3
"""Part 141 mutations: the last nine `strat` rows -- the base-block leaves, the plane weapon pair,
the hourly tick and the two scroll wrappers.

Geoscape_UiFocusPrev @ 0x8afa0 and Geoscape_UiFocusNext @ 0x8b1f4 share one body (Geoscape_FocusScroll); the
only thing either original contributes is the direction it asks for, so the mutation is that argument.

Run from the repo root:  python -I docs/parity/mutations_part141.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

GEO = "src/pufo/game/pufo_geoscape.c"
SHIP = "src/pufo/game/pufo_ship.c"
PLANE = "src/pufo/game/pufo_plane.c"
BASEUI = "src/pufo/game/pufo_baseui.c"


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


case("Base_DemolishFacility @ 0x66b70 leaves the new count one too high", "test_geoscape", GEO,
     r"\*pEnv->pbBaseCount = \(u8\)nNewCount; /\* 0x6714C \*/",
     "*pEnv->pbBaseCount = (u8)(nNewCount + 1); /* MUTATION */")
case("Base_TransferTacticalLoot @ 0x68344 leaves the weapons hot", "test_ship", SHIP,
     r"pShip->bWeaponsHot = 0; /\* 0x683E0 \*/", "pShip->bWeaponsHot = 1; /* MUTATION */")
case("Base_StripAllCraftCargo @ 0x69a38 gives the base one item more", "test_ship", SHIP,
     r"BaseRec_AddItemQty\(pBase, pAmmo, nAmount\);",
     "BaseRec_AddItemQty(pBase, pAmmo, nAmount + 1); /* MUTATION */")
case("BasePlane_CalcWeaponStoresUsed @ 0x78e78 counts one craft more", "test_ship", SHIP,
     r"nUsed \+= \(i32\)apObjects\[i\]->bCargoSize \* anCarried\[i\]; /\* 0x78EBC \*/",
     "nUsed += (i32)apObjects[i]->bCargoSize * (anCarried[i] + 1); /* MUTATION */")
case("BasePlane_UnloadWeaponStores @ 0x790e8 loads one round more per unit", "test_plane", PLANE,
     r"\*pbAmmo = \(u8\)\(\*pbAmmo \+ nTake \* pEntry->nRound\);",
     "*pbAmmo = (u8)(*pbAmmo + nTake * (pEntry->nRound + 1)); /* MUTATION */")
case("BasePlane_LoadWeaponStores @ 0x793e0 takes one more from the plane", "test_plane", PLANE,
     r"BaseRec_AddItemQty\(pBase, pItem, -nTake\); /\* 0x793C4 \*/",
     "BaseRec_AddItemQty(pBase, pItem, -nTake - 1); /* MUTATION */")
case("Geoscape_TickHourly @ 0x5d1bc teleports the stray mission to x 151", "test_geoscape", GEO,
     r"pItem->nX = 150;", "pItem->nX = 151; /* MUTATION */")
case("Geoscape_UiFocusPrev @ 0x8afa0 scrolls the next direction", "test_baseui", BASEUI,
     r"Geoscape_FocusScroll\(pEnv, true\);", "Geoscape_FocusScroll(pEnv, false); /* MUTATION */")
case("Geoscape_UiFocusNext @ 0x8b1f4 scrolls the prev direction", "test_baseui", BASEUI,
     r"Geoscape_FocusScroll\(pEnv, false\);", "Geoscape_FocusScroll(pEnv, true); /* MUTATION */")
