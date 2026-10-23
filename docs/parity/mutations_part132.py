#!/usr/bin/env python3
"""Part 132's mutation proofs: 28 `strat-ui` rows -- nine close thunks, seven panel prev/next pairs, five weapon
select copies.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part132.py
An optional substring argument runs one case:      python -I docs/parity/mutations_part132.py 79a70

Rows and their measured records (each record walked item by item, code vs pool separated; no two-word MOV immediate
occurs in these 28 -- see docs/parity/game-core.md's Part 132 section):
  close thunks  0x557bc 0x589c0 0x59b54 0x64ae4 0x6e3d4 0x72ffc 0x75154 (12 B = 2 insns + 1 pool: LDR R0,=pool /
                B Geoscape_CloseUI / DCD -- shape-identical, they differ only in the branch-displacement word) +
                0x577c0 BaseAtScreen_CloseThunk (72 B = 14 insns + 4 pool) + 0x615b0 NewBaseScreen_CloseThunk
                (32 B = 7 insns + 1 pool).
  panel pairs   0x589cc/0x589dc (16 B = 3 insns + 1 pool, the only single-panel pair), 0x6dd4c/0x6dd94 (72 B =
                14 + 4), 0x715e8/0x71648 (96 B = 19 + 5), 0x72e30/0x72e68 (56 B = 11 + 3), 0x79a20/0x79a48
                (40 B = 8 + 2), 0x79a70/0x79ac8 (88 B = 17 + 5), 0x967e4/0x9683c (88 B = 17 + 5, same 17
                instructions and same 5 pool words as 0x76088/0x760e0 -- 5 of 22 words differ, all branch
                displacements).
  weapon copies 0x79b64 0x79ba8 0x79bec 0x79c30 0x79c74 (68 B = 13 insns + 4 pool each; consecutive members
                differ in exactly 3 words: the two MOVNE immediates #0x1A..#0x1E and the tail-B displacement).

The cases attack the claim each cited check pins: that a thunk reaches the dispatcher, that the follow-up byte is
what gates the list removal, that the sample played on the new-base close is 17, how many panels a pair moves and
in which slot order, that the tail-called last panel moves too, and which mount number a weapon copy hands out.
Where the port keeps ONE body for two original functions (the transfer pair shares the buy/sell body) the case
mutates the wiring line that seats the original address, because that is where the pair is distinguished.
`src/` is mixed: pufo_baseui.c is LF-only, pufo_world.c is CRLF; case() picks the EOL from the file.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

BUI = "src/pufo/game/pufo_baseui.c"
WLD = "src/pufo/app/pufo_world.c"
B = "test_baseui"
G = "test_geoscapeframe"

CASES = []


def case(name, target, path, old, new, show=8):
    data = open(path, "rb").read()
    crlf = data.count(b"\r\n") > data.count(b"\n") - data.count(b"\r\n")
    eol = "\r\n" if crlf else "\n"
    CASES.append((name, target, path, old.replace("\n", eol).encode(),
                  new.replace("\n", eol).encode(), show))


# ---- the seven members of the 12-byte close-thunk shape ---------------------------------------------------
# Each is 12 bytes: `LDR R0,=sGeoscapeScreenBlock` (the pool word after the B), `B Geoscape_CloseUI`, DCD. The
# port writes one tail call per name; the checks in TestCloseThunks read the dispatcher's own effects (the
# sample it plays, the action-fail dword it clears, the popups it takes down), so dropping the call is what the
# row claims against. (void)pEnv keeps the parameter referenced -- deleting the call alone trips C4100 under
# /W4 /WX and a BUILD FAILED is not a proof (Part 131).
for nm, addr in [("BaseInfoScreen_CloseThunk", "557bc"), ("BaseFacScreen_CloseThunkA", "589c0"),
                 ("BaseFacScreen_CloseThunk", "59b54"), ("SoldierScreen_CloseThunk", "64ae4"),
                 ("ManufScreen_CloseThunk", "6e3d4"), ("ResearchScreen_CloseThunk", "72ffc"),
                 ("BuySellScreen_CloseThunk", "75154")]:
    case(f"{nm}-0x{addr}-no-tail-call", B, BUI,
         f"void {nm}(const GeoscapeCloseEnv *pEnv)\n"
         "{\n"
         "    Geoscape_CloseUI(pEnv);\n"
         "}",
         f"void {nm}(const GeoscapeCloseEnv *pEnv)\n"
         "{\n"
         "    (void)pEnv; /* mutation: the B Geoscape_CloseUI tail call is gone */\n"
         "}")

# NewBaseScreen_CloseThunk @ 0x615b0 (32 B = 7 insns + 1 pool): the only member of the family that plays a sample before
# the dispatcher -- 0x615BC MOV R0,#0x11 / BL Sound_Play, and CloseUI's own 0x8B95C play makes two in total.
case("NewBaseScreen_CloseThunk-0x615b0-sample-number", B, BUI,
     "        pEnv->pfnPlaySample(17, pEnv->pSoundUser); /* 0x615BC, and CloseUI plays 17 again */",
     "        pEnv->pfnPlaySample(18, pEnv->pSoundUser); /* mutation: not sample 0x11 */")

# BaseAtScreen_CloseThunk @ 0x577c0: 72 B = 14 insns + 4 pool words, and the ONLY test in it is `LDRB
# bBaseAtMissionFollows` + `ANDS` + `BNE 0x577EC`. Dropping the byte test (keeping the null guard) makes the
# follow-up argument inert: the landed-craft case then removes the contact and unpauses.
case("BaseAtScreen_CloseThunk-0x577c0-flag-ignored", B, BUI,
     "    if (pEnv->pbMissionFollows == NULL || *pEnv->pbMissionFollows == 0) {          /* 0x577C8 */",
     "    if (pEnv->pbMissionFollows == NULL) { /* mutation: the follow-up byte no longer gates the arm */")

# ---- the panel prev/next pairs ---------------------------------------------------------------------------

# BaseFacScreen_PanelPrev @ 0x589cc / PanelNext @ 0x589dc: 16 bytes each = 3 instructions + 1 pool word, and the
# only pair in the game that moves ONE panel. Swapping the step direction is the smallest change that moves the
# cursor the wrong way.
case("BaseFacScreen_PanelPrev-0x589cc-steps-forward", B, BUI,
     "    UiPanel_ScrollPrev(pPanel); /* 0x589cc is a tail call to 0x3f040 */",
     "    UiPanel_ScrollNext(pPanel); /* mutation: prev steps the wrong way */")

case("BaseFacScreen_PanelNext-0x589dc-steps-back", B, BUI,
     "    UiPanel_ScrollNext(pPanel); /* 0x589dc is a tail call to 0x3f00c */",
     "    UiPanel_ScrollPrev(pPanel); /* mutation: next steps the wrong way */")

# ManufScreen_PanelPrev @ 0x6dd4c / PanelNext @ 0x6dd94 (72 B = 14 insns + 4 pool words over pManufProductPanel,
# pManufPanelB/C/D). The last step of each body is the tail call, so cutting the set at three leaves the
# tail-called column where it was.
case("ManufScreen_PanelPrev-0x6dd4c-three-of-four", B, BUI,
     "    ScrollPanelsPrev(apPanels, 4); /* 0x6DD54, 0x6DD64, 0x6DD74, 0x6DD84 */",
     "    ScrollPanelsPrev(apPanels, 3); /* mutation: the tail-called column is left out */")

case("ManufScreen_PanelNext-0x6dd94-three-of-four", B, BUI,
     "    ScrollPanelsNext(apPanels, 4); /* 0x6DD9C, 0x6DDAC, 0x6DDBC, 0x6DDCC */",
     "    ScrollPanelsNext(apPanels, 3); /* mutation: the tail-called column is left out */")

# PsiScreen_PanelPrev @ 0x715e8 / PanelNext @ 0x71648 (96 B = 19 insns + 5 pool words): four arrays indexed by
# the SIGNED byte_1870F4, re-read before every step (0x715F4, 0x71600, 0x71610, 0x71620). The port resolves that
# byte at the caller, so the pair's own claim here is its four-column width.
case("PsiScreen_PanelPrev-0x715e8-three-of-four", B, BUI,
     "    ScrollPanelsPrev(apPanels, 4); /* 0x715F8, 0x71610, 0x71628, 0x71640 */",
     "    ScrollPanelsPrev(apPanels, 3); /* mutation: one of the four arrays is not stepped */")

case("PsiScreen_PanelNext-0x71648-three-of-four", B, BUI,
     "    ScrollPanelsNext(apPanels, 4); /* 0x71658, 0x71670, 0x71688, 0x716A0 */",
     "    ScrollPanelsNext(apPanels, 3); /* mutation: one of the four arrays is not stepped */")

# ResearchScreen_PanelPrev @ 0x72e30 / PanelNext @ 0x72e68 (56 B = 11 insns + 3 pool words over pResearchTopicPanel,
# pResearchPanelB, pResearchPanelC): three pool words, three steps, the third a tail call.
case("ResearchScreen_PanelPrev-0x72e30-wrong-slot", B, BUI,
     "        UiPanel_ScrollPrev(pThird); /* 0x72E54, unk_15EF00 */",
     "        UiPanel_ScrollPrev(pFirst); /* mutation: the tail-called slot is the first panel twice */")

case("ResearchScreen_PanelNext-0x72e68-third-dropped", B, BUI,
     "        UiPanel_ScrollNext(pThird); /* 0x72E8C */\n",
     "")

# BuySellScreen_ListPanelPrev @ 0x79a20 / ListPanelNext @ 0x79a48 (40 B = 8 insns + 2 pool words over
# pPlaneCrewPanel 0x161EDC and pPlaneCrewStatusPanel 0x161A3C): two steps, the second the tail call.
case("BuySellScreen_ListPanelPrev-0x79a20-second-dropped", B, BUI,
     "        UiPanel_ScrollPrev(pSecond); /* 0x79A38, unk_161A3C */\n",
     "")

case("BuySellScreen_ListPanelNext-0x79a48-second-dropped", B, BUI,
     "        UiPanel_ScrollNext(pSecond); /* 0x79A60 */\n",
     "")

# BuySellScreen_DetailPanelPrev @ 0x79a70 / DetailPanelNext @ 0x79ac8 (88 B = 17 insns + 5 pool words over
# pPlaneSlotPanel, pPlaneSelRowPanel, pPlaneCommitRowPanel, pPlaneTakenPanel, pPlaneAvailPanel).
case("BuySellScreen_DetailPanelPrev-0x79a70-four-of-five", B, BUI,
     "    ScrollPanelsPrev(apPanels, 5); /* 0x79A78, 0x79A88, 0x79A98, 0x79AA8, 0x79AB8 */",
     "    ScrollPanelsPrev(apPanels, 4); /* mutation: the tail-called column is left out */")

case("BuySellScreen_DetailPanelNext-0x79ac8-four-of-five", B, BUI,
     "    ScrollPanelsNext(apPanels, 5); /* 0x79AD0, 0x79AE0, 0x79AF0, 0x79B00, 0x79B10 */",
     "    ScrollPanelsNext(apPanels, 4); /* mutation: the tail-called column is left out */")

# Transfer_ScrollPrevAll @ 0x967e4 / ScrollNextAll @ 0x9683c: 88 B = 17 insns + 5 pool words over
# pBuySellPanelA..E; compared word by word with BuySellScreen_PanelPrev/Next @ 0x76088/0x760e0 they share the 17
# instructions and the 5 pool words and differ only in the 5 branch displacements (four BL plus the tail B), and the
# port keeps the one BuySell body for both. Ui_InitTransfer seats them at 0x99604 and
# 0x99614, so the wiring line is where this pair is distinguished from the buy/sell pair -- that is what each
# case mutates.
case("Transfer_ScrollPrevAll-0x967e4-wired-to-next", G, WLD,
     "    pW->transferInit.pfnPanelPrev     = World_BuySellPanelPrevThunk;       /* Transfer_ScrollPrevAll @ 0x967e4 is",
     "    pW->transferInit.pfnPanelPrev     = World_BuySellPanelNextThunk;       /* mutation: prev gets the next handler; @ 0x967e4 is")

case("Transfer_ScrollNextAll-0x9683c-wired-to-prev", G, WLD,
     "    pW->transferInit.pfnPanelNext     = World_BuySellPanelNextThunk;       /* Transfer_ScrollNextAll @ 0x9683c = 0x760e0 */",
     "    pW->transferInit.pfnPanelNext     = World_BuySellPanelPrevThunk;       /* mutation: next gets the prev handler; was 0x9683c = 0x760e0 */")

# ---- the five remaining weapon-select copies -------------------------------------------------------------
# 68 B = 13 insns + 4 pool words each; the members differ only in `MOVNE R1,#0x1A .. #0x1E`, which the port puts
# in the wiring (planeInit.apfnWeaponClick[2..6], World_PlaneSelectWeapon26..30Thunk). Changing the number is the
# mutation that matches the claim: this copy hands out this mount.
for n, addr in [(26, "79b64"), (27, "79ba8"), (28, "79bec"), (29, "79c30"), (30, "79c74")]:
    case(f"SelectWeapon{n}-0x{addr}-wrong-mount", G, WLD,
         f"        (void)BasePlaneScreen_SelectWeapon(&g_pActiveWorld->planeStatPanels, {n},\n",
         f"        (void)BasePlaneScreen_SelectWeapon(&g_pActiveWorld->planeStatPanels, {n - 1}, /* mutation: not MOVNE R1,#0x{0x1A + n - 26:X} */\n")


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    run = 0
    skipped = 0
    inert = []
    total_fails = 0
    for (name, target, path, old, new, show) in CASES:
        if only and only not in name:
            continue
        run += 1
        lines = run_case(name, target, path, old, new, show)
        if lines is None:
            skipped += 1
        else:
            total_fails += len(lines)
            if len(lines) == 0:
                inert.append(name)
    print()
    print(f"cases run: {run}  skipped: {skipped}  inert: {len(inert)}  failing checks seen: {total_fails}")
    if inert:
        print("INERT (no teeth):", ", ".join(inert))


if __name__ == "__main__":
    main()
