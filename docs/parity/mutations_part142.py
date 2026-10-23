#!/usr/bin/env python3
"""Part 142 mutations: the 32 tact click-table rows the live bank presses now pin.

Run in chunks, unbuffered, so the log survives and no chunk runs into a timeout:
    python -u -I docs/parity/mutations_part142.py 0 8  >> build/crops/mut142.log
    python -u -I docs/parity/mutations_part142.py 8 16 >> build/crops/mut142.log
    ...
Cases are collected first and only then run, so a slice is a slice of the same order every time.

Two anchor kinds. `line` replaces one line that occurs exactly once; `block` replaces a run of lines that
occurs exactly once -- the layer pair and the layer-byte pair share their gate text, so only the store line
under it tells them apart. Every mutation is per row: a shared callee (Ui_SetSubMode, Ui_SetFireMode,
Ui_TryFireMode, Ui_ToggleOption) is mutated through its wrapper's own constant, a unique callee through its own
body.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

TACTUI = "src/pufo/game/pufo_tactui.c"
WORLD = "src/pufo/app/pufo_world.c"
TEST = "test_geoscapeframe"

CASES = []


def read_lines(path):
    data = open(path, "rb").read()
    eol = b"\r\n" if b"\r\n" in data else b"\n"
    return [l.decode("utf-8") for l in data.split(eol)], eol


def case_line(label, path, old, new):
    def go(label=label, path=path, old=old, new=new):
        ls, eol = read_lines(path)
        hits = [i for i, l in enumerate(ls) if old in l]
        if not hits:
            print("SKIP", label, ": anchor not found in", path, flush=True)
            return
        if len(hits) > 1:
            print("SKIP", label, ": anchor occurs", len(hits), "times in", path, flush=True)
            return
        run_case(label, TEST, path, old.encode("utf-8"), new.encode("utf-8"))
    CASES.append(go)


def case_block(label, path, lines, new_lines):
    def go(label=label, path=path, lines=lines, new_lines=new_lines):
        ls, eol = read_lines(path)
        old = eol.join(l.encode("utf-8") for l in lines)
        new = eol.join(l.encode("utf-8") for l in new_lines)
        n = eol.join(l.encode("utf-8") for l in ls).count(old)
        if n == 0:
            print("SKIP", label, ": block anchor not found in", path, flush=True)
            return
        if n > 1:
            print("SKIP", label, ": block anchor occurs", n, "times in", path, flush=True)
            return
        run_case(label, TEST, path, old, new)
    CASES.append(go)


# The layer pair: MapView_LayerUp @ 0x16530 / MapView_LayerDown @ 0x164f0 step through a local.
case_line("MapView_LayerUp @ 0x16530 steps by two", TACTUI,
          "        u8 nNew = (u8)(pUi->nLayerScratch + 1);",
          "        u8 nNew = (u8)(pUi->nLayerScratch + 2); /* MUTATION */")
case_line("MapView_LayerDown @ 0x164f0 steps by two", TACTUI,
          "        u8 nNew = (u8)(pUi->nLayerScratch - 1);",
          "        u8 nNew = (u8)(pUi->nLayerScratch - 2); /* MUTATION */")

# The layer-byte pair stores the byte directly; the gate text is shared with the pair above, so the store
# line belongs to the anchor.
case_block("Map_IncByte128AF0_Play11 @ 0x19670 refuses at 2", TACTUI,
           ["    if ((i8)pUi->nLayerScratch < PUFO_LAYER_MAX) {",
            "        pUi->nLayerScratch = (u8)(pUi->nLayerScratch + 1);"],
           ["    if ((i8)pUi->nLayerScratch < PUFO_LAYER_MIN) { /* MUTATION */",
            "        pUi->nLayerScratch = (u8)(pUi->nLayerScratch + 1);"])
case_block("Map_DecByte128AF0_Play11 @ 0x196ac refuses at 3", TACTUI,
           ["    if ((i8)pUi->nLayerScratch > PUFO_LAYER_MIN) {",
            "        pUi->nLayerScratch = (u8)(pUi->nLayerScratch - 1);"],
           ["    if ((i8)pUi->nLayerScratch > PUFO_LAYER_MAX) { /* MUTATION */",
            "        pUi->nLayerScratch = (u8)(pUi->nLayerScratch - 1);"])

# Map_PlaySample17_SetMode11 @ 0x179c0 is the only writer of mode 11.
case_line("Map_PlaySample17_SetMode11 @ 0x179c0 sets mode 12", TACTUI,
          "    pUi->nActionMode = PUFO_ACTION_MODE_MENU;",
          "    pUi->nActionMode = PUFO_ACTION_MODE_NONE; /* MUTATION */")

# The fire-mode trio shares Ui_SetFireMode; each row's own contribution is its wrapper constant.
case_line("Tact_SetFireModeAuto @ 0x17868 asks for SNAP", WORLD,
          "static void World_ClickFireAuto(void)  { World_TactFireModeCall(PUFO_ACTION_MODE_AUTO); }",
          "static void World_ClickFireAuto(void)  { World_TactFireModeCall(PUFO_ACTION_MODE_SNAP); /* MUTATION */ }")
case_line("Tact_SetFireModeSnap @ 0x177fc asks for AIMED", WORLD,
          "static void World_ClickFireSnap(void)  { World_TactFireModeCall(PUFO_ACTION_MODE_SNAP); }",
          "static void World_ClickFireSnap(void)  { World_TactFireModeCall(PUFO_ACTION_MODE_AIMED); /* MUTATION */ }")
case_line("Tact_SetFireModeAimed @ 0x17790 asks for AUTO", WORLD,
          "static void World_ClickFireAimed(void) { World_TactFireModeCall(PUFO_ACTION_MODE_AIMED); }",
          "static void World_ClickFireAimed(void) { World_TactFireModeCall(PUFO_ACTION_MODE_AUTO); /* MUTATION */ }")

# The try-fire trio shares Ui_TryFireMode; each original is its mode.
case_line("Tact_TryFireMode4 @ 0x179e4 tries mode 5", WORLD,
          "static void World_ClickTryFire4(void)  { World_TactTryFireCall(4); }",
          "static void World_ClickTryFire4(void)  { World_TactTryFireCall(5); /* MUTATION */ }")
case_line("Tact_TryFireMode5 @ 0x17b1c tries mode 4", WORLD,
          "static void World_ClickTryFire5(void)  { World_TactTryFireCall(5); }",
          "static void World_ClickTryFire5(void)  { World_TactTryFireCall(4); /* MUTATION */ }")
case_line("Tact_TryFireMode10 @ 0x17a80 tries mode 5", WORLD,
          "static void World_ClickTryFire10(void) { World_TactTryFireCall(10); }",
          "static void World_ClickTryFire10(void) { World_TactTryFireCall(5); /* MUTATION */ }")

# The nine Map_SetSubModeN_Play11 handlers: each constant lives in its own wrapper.
SUBMODES = [("0x18418", 0), ("0x18478", 1), ("0x184d4", 2), ("0x18534", 3), ("0x18594", 4),
            ("0x185f4", 5), ("0x18654", 6), ("0x186b4", 7), ("0x18714", 8)]
for pszAddr, nSub in SUBMODES:
    nWrong = (nSub + 1) % 9
    case_line("Map_SetSubMode%d_Play11 @ %s writes bUnk7 %d" % (nSub, pszAddr, nWrong), WORLD,
              "static void World_ClickSubMode%d(void) { World_TactSubModeCall(%d); }" % (nSub, nSub),
              "static void World_ClickSubMode%d(void) { World_TactSubModeCall(%d); } /* MUTATION */" % (nSub, nWrong))

# The five option pads: each wrapper passes its own id.
TOGGLES = (("0x19250", "Map_ToggleFlag128A8D", "SCROLL", "SHOW"), ("0x192d4", "Map_ToggleFlag128A8E", "SHOW", "RUN"),
           ("0x19348", "Map_ToggleFlag128A8F", "RUN", "TURN"), ("0x193a0", "Map_ToggleFlag128A90", "TURN", "MENU"),
           ("0x1941c", "Map_ToggleFlag128A91", "MENU", "SCROLL"))
for pszAddr, pszRow, pszId, pszWrong in TOGGLES:
    # The wrapper lines differ in spacing after (void), so the anchor is the call token, which is unique.
    case_line("%s_Play11 @ %s drives %s" % (pszRow, pszAddr, pszWrong), WORLD,
              "World_TactToggleCall(TACT_TOGGLE_%s);" % pszId,
              "World_TactToggleCall(TACT_TOGGLE_%s); /* MUTATION */" % pszWrong)

# The deploy pair and the screen-7 opener: three thin handlers, each naming its callee.
case_line("Map_PlaySamples16_7_SetDeployFlag @ 0x19528 clears instead", WORLD,
          "static void World_ClickDeployOpen(void)    { World_TactUiCall(Ui_Samples16_7_SetDeployFlag); }",
          "static void World_ClickDeployOpen(void)    { World_TactUiCall(Ui_Samples16_7_ClearDeployFlag); /* MUTATION */ }")
case_line("Map_PlaySamples16_7_ClearDeployFlag @ 0x19568 sets instead", WORLD,
          "static void World_ClickDeployClose(void)   { World_TactUiCall(Ui_Samples16_7_ClearDeployFlag); }",
          "static void World_ClickDeployClose(void)   { World_TactUiCall(Ui_Samples16_7_SetDeployFlag); /* MUTATION */ }")
case_line("Map_UiScreen7_PlaySamples16_14 @ 0x194f8 clears instead of opening", WORLD,
          "static void World_ClickScreen7Samples(void){ World_TactUiCall(Ui_Screen7_Samples16_14); }",
          "static void World_ClickScreen7Samples(void){ World_TactUiCall(Ui_Samples16_14_ClearScreen); /* MUTATION */ }")

# Tact_OpenCommandScreenFlagged @ 0x198c4 is the one handler that sets the pen-slot-8 low byte.
case_block("Tact_OpenCommandScreenFlagged @ 0x198c4 leaves the flag down", TACTUI,
           ["    Ui_Play(pHooks, 16);",
            "    pUi->bSlot80Low = 1;",
            "    pUi->nUiScreen = 4;"],
           ["    Ui_Play(pHooks, 16);",
            "    pUi->bSlot80Low = 0; /* MUTATION */",
            "    pUi->nUiScreen = 4;"])

# Map_PlaySamples16_14_ClearUiScreen @ 0x18e9c plays 16 and 14 and closes the screen.
case_block("Map_PlaySamples16_14_ClearUiScreen @ 0x18e9c re-opens screen 7", TACTUI,
           ["    Ui_Play(pHooks, 16);",
            "    Ui_Play(pHooks, 14);",
            "    pUi->nUiScreen = 0;"],
           ["    Ui_Play(pHooks, 16);",
            "    Ui_Play(pHooks, 14);",
            "    pUi->nUiScreen = 7; /* MUTATION */"])

# Tact_CycleNextUnit @ 0x16684 reaches the roster only through its wrapper's callee.
case_line("Tact_CycleNextUnit @ 0x16684 never reaches the roster", WORLD,
          "static void World_ClickCycleNext(void)          { World_TactRosterCall(Roster_CycleNext); }",
          "static void World_ClickCycleNext(void)          { (void)0; /* MUTATION: no roster call */ }")

# Tact_OpenRightHandMenu @ 0x17148 is the `true` side of the shared hand-menu body.
case_block("Tact_OpenRightHandMenu @ 0x17148 does not run the body", WORLD,
           ["    if (pW != NULL) {",
            "        (void)World_TactOpenHandMenuBody(pW, true);"],
           ["    if (pW != NULL) {",
            "        (void)pW; /* MUTATION: the hand menu never runs */"])

if __name__ == "__main__":
    i0 = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    i1 = int(sys.argv[2]) if len(sys.argv) > 2 else len(CASES)
    print("=== chunk %d..%d of %d cases" % (i0, i1, len(CASES)), flush=True)
    for fn in CASES[i0:i1]:
        fn()
    print("=== chunk done", flush=True)
