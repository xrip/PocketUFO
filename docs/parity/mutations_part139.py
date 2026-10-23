#!/usr/bin/env python3
"""Part 139 mutations: one case per row of the funding / close-thunk / base-grid batch.

Anchors are taken FROM the file at run time, so they cannot drift. Most are single-line; the five
close thunks are identical two-instruction copies, so those use a block anchor: the function head is
found by name and the mutation lands on the call in the body of THAT function.

Run from the repo root:  python -I docs/parity/mutations_part139.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

BASEUI = "src/pufo/game/pufo_baseui.c"
GEO = "src/pufo/game/pufo_geoscape.c"
STRAT = "src/pufo/game/pufo_strateg.c"


def read_lines(path):
    """Split on the file's OWN end-of-line marker: pufo_baseui.c is LF-only while pufo_geoscape.c and
    pufo_strateg.c are CRLF-only, and assuming one silently makes every anchor of the other fail."""
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


def thunk_case(label, test, path, head, repl_body):
    """Mutate `Geoscape_CloseUI(pEnv);` inside the function whose head line matches `head`."""
    ls, eol = read_lines(path)
    for i, line in enumerate(ls):
        if re.search(head, line):
            body = ls[i + 2]
            if body.strip() != "Geoscape_CloseUI(pEnv);":
                print(f"SKIP {label}: body of {head!r} is {body!r}")
                return
            old = eol.join(s.encode("utf-8") for s in ls[i:i + 4])
            new = eol.join(s.encode("utf-8") for s in (ls[i], ls[i + 1], repl_body, ls[i + 3]))
            run_case(label, test, path, old.encode("utf-8"), new.encode("utf-8"))
            return
    print(f"SKIP {label}: head {head!r} not found in {path}")


DROP = "    (void)pEnv; /* MUTATION: the tail call is dropped */"

# ---- the five identical close thunks (each is its own original address) --------------------------

thunk_case("FundingScreen_CloseThunk @ 0x5abd8 skips the dispatcher", "test_baseui", BASEUI,
           r"^void FundingScreen_CloseThunk\(", DROP)
thunk_case("FundingScreen_ExitThunk @ 0x5c078 skips the dispatcher", "test_baseui", BASEUI,
           r"^void FundingScreen_ExitThunk\(", DROP)
thunk_case("Geoscape_CloseUI_ThunkA @ 0x838d0 skips the dispatcher", "test_baseui", BASEUI,
           r"^void Geoscape_CloseUI_ThunkA\(", DROP)
thunk_case("Geoscape_CloseUI_ThunkB @ 0x96020 skips the dispatcher", "test_baseui", BASEUI,
           r"^void Geoscape_CloseUI_ThunkB\(", DROP)
thunk_case("Geoscape_CloseUI_ThunkC @ 0x9a8c0 skips the dispatcher", "test_baseui", BASEUI,
           r"^void Geoscape_CloseUI_ThunkC\(", DROP)

# ---- the funding screen ----------------------------------------------------------------------------

case("Geoscape_OnMapClick_Thunk @ 0x857e8 stops forwarding the result", "test_baseui", BASEUI,
     r"return pHooks->pfnMapClick\(pHooks->pUser\);",
     "return 0; /* MUTATION: the forward at 0x857EC is gone */")
case("Funding_ScoreToBarLevel @ 0x5aa60 prices an up-step at 30000", "test_baseui", BASEUI,
     r"case  1: nAmount =  25000; break;", "case  1: nAmount =  30000; /* MUTATION */")
case("FundingScreen_Init @ 0x5c084 puts the close control at x 223", "test_baseui", BASEUI,
     r"pEnv->pCloseCtl->x = 224;                 /\* 0x5C0E0 \*/",
     "pEnv->pCloseCtl->x = 223; /* MUTATION */")
case("FundingScreen_Toggle @ 0x5abe4 leaves the open flag clear", "test_baseui", BASEUI,
     r"\*pEnv->pbOpen = 1;                              /\* 0x5AC00 \*/",
     "*pEnv->pbOpen = 0; /* MUTATION */")
case("FundingScreen_Open @ 0x5b560 wires the close control at x 223", "test_baseui", BASEUI,
     r"pEnv->pCloseCtl->x = 224;                             /\* 0x5B574 \*/",
     "pEnv->pCloseCtl->x = 223; /* MUTATION */")
case("FundingScreen_Draw @ 0x5b1fc draws while the screen is closed", "test_baseui", BASEUI,
     r"if \(pEnv == NULL \|\| pEnv->pbOpen == NULL \|\| \*pEnv->pbOpen == 0\) \{",
     "if (pEnv == NULL || pEnv->pbOpen == NULL || *pEnv->pbOpen == 1) { /* MUTATION: 0x5B210 gate inverted */")
case("FundingScreen_DrawExtended @ 0x5b59c draws the header uncentred", "test_baseui", BASEUI,
     r"Text_DrawString\(120 - nWidth / 2, 5, szHead\);",
     "Text_DrawString(120, 5, szHead); /* MUTATION: the centring at 0x5B7D4 is gone */")

# ---- the panel-flag pair and the base-grid byte pair ---------------------------------------------

case("Geoscape_SetPanelFlagA @ 0x8b448 sets B's byte", "test_geoscape", GEO,
     r"pFlags->bPanelA = 1; /\* unk_1864F5, 0x8b468 \*/",
     "pFlags->bPanelB = 1; /* MUTATION */")
case("Geoscape_SetPanelFlagB @ 0x8b478 sets A's byte", "test_geoscape", GEO,
     r"pFlags->bPanelB = 1; /\* unk_1864F7, 0x8b498 \*/",
     "pFlags->bPanelA = 1; /* MUTATION */")
case("BaseGrid_SetByteAtFound @ 0x6ac90 does not write the grid byte", "test_geoscape", GEO,
     r"abGrid\[nIdx\] = \(u8\)bValue;", "(void)bValue; /* MUTATION: 0x6ACB4 store dropped */")
case("BaseGrid_GetByte0x654 @ 0x6acc0 returns 0 instead of the byte", "test_geoscape", GEO,
     r"return \(i32\)abGrid\[nIdx\]; /\* 0x6ace4", "return 0; /* MUTATION: 0x6ACE4")

# ---- the base / cell / list leaves -----------------------------------------------------------------

case("Base_GrantStarterItems @ 0x95fa4 grants 11 elerium", "test_geoscape", GEO,
     r"BaseRec_AddItemQty\(pBase, apObjects\[OBJ_ELERIUM_115\], 10\);",
     "BaseRec_AddItemQty(pBase, apObjects[OBJ_ELERIUM_115], 11); /* MUTATION */")
case("BaseFac_SetCellBuildState @ 0x623bc stamps hangar 13", "test_geoscape", GEO,
     r"abFacilityGrid\[nFac\]     = 9;  /\* 0x62438 \*/",
     "abFacilityGrid[nFac]     = 13; /* MUTATION */")
case("StrategList_PurgeType5 @ 0x6ab58 keeps an unclaimed item", "test_strateg", STRAT,
     r"bKeep = 0; /\* 0x6ABA0 \*/", "bKeep = 1; /* MUTATION */")
case("StrategList_AddAlienSiteIfValid @ 0x7e3b4 gates on 254, not 255", "test_strateg", STRAT,
     r"if \(Tilemap_GetAt\(pMap, x, y\) == 255\) \{ /\* 0x7E3D8: ocean \*/",
     "if (Tilemap_GetAt(pMap, x, y) == 254) { /* MUTATION */")
