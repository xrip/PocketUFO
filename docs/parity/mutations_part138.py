#!/usr/bin/env python3
"""Part 138 mutations: one case per row of the GameTime / Smap / StrategList / Tilemap / Craft batch.

Same discipline as Part 137: every anchor is taken FROM the file at run time (a regex finds the line and the case
mutates that exact text), so an anchor cannot drift. A case whose anchor is not found reports
"SKIP ... anchor occurs 0 times" rather than silently doing nothing.

Run from the repo root:  python -I docs/parity/mutations_part138.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

STRAT = "src/pufo/game/pufo_strateg.c"
GEO = "src/pufo/game/pufo_geoscape.c"
CLOCK = "src/pufo/game/pufo_clock.c"
SHIP = "src/pufo/game/pufo_ship.c"
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


# ---- GameTime: the pause byte and the five speed picks ------------------------------------------------

case("GameTime_Unpause @ 0x5d6c0 sets the pause byte instead of clearing it", "test_geoscape", CLOCK,
     r"pTime->bPaused = 0; /\* 0x5d6c8", "pTime->bPaused = 1; /* MUTATION: 0x5d6c8")
case("GameTime_Set1Min @ 0x84f64 selects speed 2, not 1", "test_geoscape", GEO,
     r"GameTime_SetSpeedIdx\(pWorld, apSpeedCtl, 1\);", "GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 2);")
case("GameTime_Set5Min @ 0x84fc8 selects speed 3, not 2", "test_geoscape", GEO,
     r"GameTime_SetSpeedIdx\(pWorld, apSpeedCtl, 2\);", "GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 3);")
case("GameTime_SetSpeedIdx3 @ 0x8502c selects speed 4, not 3", "test_geoscape", GEO,
     r"GameTime_SetSpeedIdx\(pWorld, apSpeedCtl, 3\);", "GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 4);")
case("GameTime_SetSpeedIdx4 @ 0x85094 selects speed 5, not 4", "test_geoscape", GEO,
     r"GameTime_SetSpeedIdx\(pWorld, apSpeedCtl, 4\);", "GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 5);")
case("GameTime_SetSpeedIdx5 @ 0x850fc selects speed 3, not 5", "test_geoscape", GEO,
     r"GameTime_SetSpeedIdx\(pWorld, apSpeedCtl, 5\);", "GameTime_SetSpeedIdx(pWorld, apSpeedCtl, 3);")

# ---- Smap: the view walker, the clamp, the zoom pair, the overlay lookup, the dismiss ------------------

case("Smap_ScrollBy @ 0x820d0 adds the drag instead of subtracting it", "test_strateg", STRAT,
     r"pView->nScrollX - nDx \+ pView->nLastDx", "pView->nScrollX + nDx + pView->nLastDx")
case("Smap_ClampScrollY @ 0x837f8 clamps against nMapH/2 + 160", "test_strateg", STRAT,
     r"const i32 nHalfH = pView->nMapH / 2 - 160;", "const i32 nHalfH = pView->nMapH / 2 + 160;")
case("Smap_GetTileAtMode @ 0x821c4 forgets the x origin", "test_strateg", STRAT,
     r"Tilemap_GetAt\(aOverlay\[pView->nMode\], nX - nOriginX, nY\)",
     "Tilemap_GetAt(aOverlay[pView->nMode], nX + nOriginX, nY)")
case("Smap_ZoomOut @ 0x81c08 lands on level 1 instead of 0", "test_strateg", STRAT,
     r"pView->nMode = 0;", "pView->nMode = 1; /* MUTATION: 0x81DD4 lands on 1 */")
case("Smap_ZoomIn @ 0x81e44 lands on level 0 instead of 2", "test_strateg", STRAT,
     r"pView->nMode = 2;", "pView->nMode = 0; /* MUTATION: 0x81EF4 lands on 0 */")
case("Smap_OnZoomOut @ 0x84ab8 plays sample 18", "test_strateg", STRAT,
     r"pfnPlay\(17, pUser\); /\* 0x84AC4 \*/", "pfnPlay(18, pUser); /* MUTATION */")
case("Smap_OnZoomIn @ 0x84ad8 plays sample 18", "test_strateg", STRAT,
     r"pfnPlay\(17, pUser\); /\* 0x84AE4 \*/", "pfnPlay(18, pUser); /* MUTATION */")
case("Smap_DismissSelected @ 0x84428 leaves the selection flag set", "test_geoscape", STRAT,
     r"\*pbSelectionFlag = 0; /\* 0x84470 \*/", "*pbSelectionFlag = 1; /* MUTATION */")

# ---- Tilemap: one lookup expression, two globals --------------------------------------------------------

case("Tilemap_GetAt @ 0x8217c reads one byte further", "test_strateg", STRAT,
     r"pMap->pData\[\(i32\)pMap->uWidth \* nY \+ nX\]", "pMap->pData[(i32)pMap->uWidth * nY + nX + 1]")
case("Tilemap_GetAtAlt @ 0x821a0 swaps the stride to the x axis", "test_strateg", STRAT,
     r"pMap->pData\[\(i32\)pMap->uWidth \* nY \+ nX\]", "pMap->pData[(i32)pMap->uWidth * nX + nY]")

# ---- the strateg list leaves ------------------------------------------------------------------------------

# StrategList_ctor is the PtrArray_ctor call, and PtrArray_ctor makes two stores. Dropping the item
# store is the mutation the new check sees (the test dirties both fields first). Dropping the count
# store is inert at a zero-filled fixture, and dropping PtrArray_DeleteAll leaks items no check reads:
# measured 0 failing checks in the first run of this file, so neither is used.
case("StrategList_ctor @ 0x83890 drops the item-pointer store", "test_strateg", STRAT,
     r"PtrArray_ctor\(pList\); /\* 0x83890", "pList->nCount = 0; /* MUTATION: 0x83890")
case("StrategList_Clear @ 0x838ac drops the 0x838c8 empty-out", "test_strateg", STRAT,
     r"PtrArray_Clear\(pList\);", "(void)pList; /* MUTATION: 0x838c8 dropped */")
case("StrategList_RegisterAtexit @ 0x838a0 reports a failed registration", "test_baseui", BASEUI,
     r"return atexit\(StrategList_ClearAtexitThunk\);", "return -1; /* MUTATION */")
case("StrategItem_AllocAdd @ 0x415cc seeds one higher", "test_strateg", STRAT,
     r"StrategItem_ctor\(pItem, nSeed\);", "StrategItem_ctor(pItem, nSeed + 1);")
case("StrategItem_ctor @ 0x82bd4 writes the seed one higher", "test_strateg", STRAT,
     r"pItem->nSeed = nSeed;", "pItem->nSeed = nSeed + 1;")

# ---- the two craft leaves ---------------------------------------------------------------------------------

case("Craft_CountSoldiersAboard @ 0x6649c counts the soldiers NOT aboard", "test_geoscapeframe", SHIP,
     r"\(i32\)apSoldiers\[i\]->bUnk183 == nCraftIndex", "(i32)apSoldiers[i]->bUnk183 != nCraftIndex")
case("Craft_LoadStoresFromBase @ 0x693ec draws ammo from pool 39", "test_ship", SHIP,
     r"case 7:  aPool\[0\] = 40;", "case 7:  aPool[0] = 39; /* MUTATION */")
