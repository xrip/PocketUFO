#!/usr/bin/env python3
"""Part 131's mutation proofs: 29 `strat-ui` rows -- the options screen's radio handlers and slot handlers.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part131.py
An optional substring argument runs one case:      python -I docs/parity/mutations_part131.py SfxVol1

Rows: OptionsScreen_SetMusicVol1..5 0x6ba70/0x6babc/0x6bb18/0x6bb74/0x6bbd0, OptionsScreen_SetSfxVol0..5
0x6bc1c/0x6bc80/0x6bce4/0x6bd48/0x6bdac/0x6be10, ClearMusicButtons 0x6b994, ClearSfxButtons 0x6b9d4,
OptionsScreen_PanelPrev 0x6d01c, PanelNext 0x6d064, OpenWithSound 0x6dd04, SaveGame_Slot1..6_CloseUI
0x6afcc..0x6b148, LoadGame_Slot1..6_InitOptions 0x6b1fc..0x6b404, Ufopaedia_CacheSelRow 0x99fd0.

The music and sfx handlers are six copies each. Measured 2026-10-23: the sfx six are 100 bytes = 23
instructions (2 of them 2-word MOV immediates) + 2 pool words and differ only in `MOV R1,#N`, in the widget
pointer offset `+4N` and in the mute byte; the music six are 92/76/92/92/92/76 bytes = 20/17/20/20/20/17
instructions + 2 pool words and differ in `MOV R0,#N` (92-byte form) or `MOV R1,#N` (76-byte form). The cases
attack different claims the cited checks pin: the stored step, the mute step, the group clear, which button
gets lit, whether the group plays a sample at all, and (for sfx) the two samples -- the BL at 0x6BC64 and the
tail `B Sound_Play` at 0x6BC74. Every address quoted below belongs to the member named in the case name.
`src/` is CRLF, so `case()` translates a "\n" in the anchor into the file's own line ending.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

BUI = "src/pufo/game/pufo_baseui.c"
T = "test_baseui"

CASES = []


def case(name, target, path, old, new, show=8):
    data = open(path, "rb").read()
    crlf = data.count(b"\r\n") > data.count(b"\n") - data.count(b"\r\n")
    eol = "\r\n" if crlf else "\n"
    CASES.append((name, target, path, old.replace("\n", eol).encode(),
                  new.replace("\n", eol).encode(), show))


# ---- the six sfx radio handlers (OptionsScreen_SetSfxVol) -------------------------------------------

# 0x19BC44/#1 in the start-menu family; here 0x6BC44 `MOV R1,#1` for Vol0 and #0 for the rest: step 0 is
# the mute step, and Sound_Play @ 0x9E10C tests Map_bSoundOff before it plays anything.
case("SfxVol0-0x6bc1c-mute-step", T, BUI,
     "    *pEnv->pbSfxOff = (nStep == 0) ? 1 : 0;                               /* 0x6BC54 / 0x6BCB8 */",
     "    *pEnv->pbSfxOff = 0;                                                  /* mutation: never mutes */")

# 0x6BC5C/0x6BC60 MOV R1,#4 / MOV R0,#0x11 then 0x6BC64 BL Sound_Play: the FIRST of the two plays.
case("SfxVol1-0x6bc80-first-play-dropped", T, BUI,
     "    Sound_Play(17);                          /* 0x6BC5C MOV R1,#4 / 0x6BC60 MOV R0,#0x11 / 0x6BC64 BL */\n",
     "")

# 0x6BD08 is this member's `LDR R3,[R1,#(aOptionsSfxBtn+8 - 0x159E20)]` (+0x24 in every member): the step
# picks the widget.
case("SfxVol2-0x6bce4-radio-index", T, BUI,
     "    Options_SelectRadio(pEnv->pCtl->apSfx[Options_ClampStep(nStep)]);     /* 0x6BC48 */",
     "    Options_SelectRadio(pEnv->pCtl->apSfx[0]);                            /* mutation: always button 0 */")

# +0x04 in every sfx member -- 0x6BD4C here -- is `BL OptionsScreen_ClearSfxButtons`, and it runs before the
# volume store.
case("SfxVol3-0x6bd48-group-clear-dropped", T, BUI,
     "    OptionsScreen_ClearSfxButtons(pEnv->pCtl->apSfx);                     /* 0x6BC20 */\n",
     "")

# 0x6BDB8 `MOV R1,#4` / 0x6BDC8 STR (+0x0C / +0x1C): the stored volume is the handler's own step.
case("SfxVol4-0x6bdac-stored-step", T, BUI,
     "    *pEnv->pnSfxVol = nStep;                                              /* 0x6BC38 */",
     "    *pEnv->pnSfxVol = nStep + 1;                                          /* mutation: off by one */")

# 0x6BE4C is this member's `BL Sound_ApplySfxVolume` (+0x3C): it sits between the mute store and the two
# samples, so the new level is applied before the player hears it.
case("SfxVol5-0x6be10-apply-dropped", T, BUI,
     "    Sound_ApplySfxVolume(Options_MapLevel(*pEnv->pnSfxVol, *pEnv->pbSfxOff),\n"
     "                         *pEnv->pbSfxOff);                                /* 0x6BC58 */",
     "    (void)Options_MapLevel(*pEnv->pnSfxVol, *pEnv->pbSfxOff);             /* mutation: no apply */")

# ---- the five music radio handlers (OptionsScreen_SetMusicVol) --------------------------------------

# 0x6BA98 `MOV R1,#0` then 0x6BAA8 `STRB Map_bMusicOff` in Vol1, against 0x6BA4C `MOV R0,#1` and 0x6BA5C
# `STRB` in Vol0: step 0 mutes music, the other five do not.
case("MusicVol1-0x6ba70-mute-step", T, BUI,
     "    *pEnv->pbMusicOff = (nStep == 0) ? 1 : 0;                             /* 0x6BA5C / 0x6BAA8 */",
     "    *pEnv->pbMusicOff = 0;                                                /* mutation: never mutes */")

# 92 bytes = 20 instructions (3 of them 2-word MOV immediates) + 2 pool words, and NO Sound_Play among them:
# that is what separates the music group from the sfx one, which plays 17 twice.
case("MusicVol2-0x6babc-sample-added", T, BUI,
     "    Sound_ApplyMusicVolume(Options_MapLevel(*pEnv->pnMusicVol, *pEnv->pbMusicOff),",
     "    Sound_Play(17);\n    Sound_ApplyMusicVolume(Options_MapLevel(*pEnv->pnMusicVol, *pEnv->pbMusicOff),")

# 0x6BB18's own body reaches its button through `ADD R3,R0,R1,LSL#2` over aOptionsListCtl -- the step picks
# the widget.
case("MusicVol3-0x6bb18-radio-index", T, BUI,
     "    Options_SelectRadio(pEnv->pCtl->apMusic[Options_ClampStep(nStep)]);   /* 0x6BA50 */",
     "    Options_SelectRadio(pEnv->pCtl->apMusic[0]);                          /* mutation: always button 0 */")

# 0x6BB7C `MOV R0,#4` / 0x6BB8C STR (+0x08 / +0x18 of this member): the stored volume is its own step.
case("MusicVol4-0x6bb74-stored-step", T, BUI,
     "    *pEnv->pnMusicVol = nStep;                                            /* 0x6BA28 / 0x6BAD0 */",
     "    *pEnv->pnMusicVol = nStep + 1;                                        /* mutation: off by one */")

# 0x6BBD4 is this member's `BL OptionsScreen_ClearMusicButtons` (+0x04 in the 76-byte members, +0x1C in the
# 92-byte ones): the whole group is deselected before one is lit.
case("MusicVol5-0x6bbd0-group-clear-dropped", T, BUI,
     "    OptionsScreen_ClearMusicButtons(pEnv->pCtl->apMusic);                 /* 0x6BA2C */\n",
     "")

# ---- the two group clears ----------------------------------------------------------------------------

# 0x6b994 is 64 bytes = 15 instructions + 1 pool word: LDR R0, MOV R1,#0, six LDR/STRB pairs
# (0x6B99C..0x6B9C8) over the six widget pointers, RET at 0x6B9CC, DCD at 0x6B9D0.
case("ClearMusic-0x6b994-loop-bound", T, BUI,
     "    for (i32 i = 0; i < 6; ++i) {\n        if (apCtl[i] != NULL) {\n            apCtl[i]->nState = 0;",
     "    for (i32 i = 0; i < 5; ++i) {\n        if (apCtl[i] != NULL) {\n            apCtl[i]->nState = 0;")

# 0x6BA04 (0x6b9d4 + 0x30) loads the SIXTH pointer before the last store at 0x6BA08, and 0x6BA0C is RET, so
# the return value is that pointer.
case("ClearSfx-0x6b9d4-return-value", T, BUI,
     "    return apCtl[5];",
     "    return NULL;")

# ---- the options screen's prev/next pair ------------------------------------------------------------

# 0x6d01c: four DecDwordIfPos_AtPlus0xBD4 calls (0x6D028, 0x6D034, 0x6D040 and the tail at 0x6D050) -- all
# four panels move on one press.
case("PanelPrev-0x6d01c-panel-count", T, BUI,
     "    for (int i = 0; i < 4; ++i) {\n        if (apPanels[i] != NULL) {\n"
     "            UiPanel_ScrollPrev(apPanels[i]); /* 0x6D028, 0x6D034, 0x6D040, 0x6D04C */",
     "    for (int i = 0; i < 3; ++i) {\n        if (apPanels[i] != NULL) {\n"
     "            UiPanel_ScrollPrev(apPanels[i]); /* 0x6D028, 0x6D034, 0x6D040, 0x6D04C */")

# 0x6d064 is the mirror over IncDwordIfInRange_AtPlus0xBD4 (0x3f00c), not over the prev step.
case("PanelNext-0x6d064-wrong-step", T, BUI,
     "            UiPanel_ScrollNext(apPanels[i]); /* 0x6D070, 0x6D07C, 0x6D088, 0x6D094 */",
     "            UiPanel_ScrollPrev(apPanels[i]); /* mutation: the prev step instead */")

# ---- the opener ---------------------------------------------------------------------------------------

# 0x6DD10 `MOV R0,#7` / 0x6DD0C `MOV R1,#4`: sample 7 with the same second argument the other openers use.
case("OpenWithSound-0x6dd04-sample-id", T, BUI,
     "        pHooks->pfnPlaySample(7, 4, pHooks->pUser); /* 0x6DD10 */",
     "        pHooks->pfnPlaySample(16, 4, pHooks->pUser); /* mutation: a different sample */")

# ---- the seven save handlers (SaveGame_Slot_CloseUI) --------------------------------------------------

# 0x6AFD4 `MOV R0,#4` / 0x6AFDC STR: mode 4 is stored before SaveGame runs.
case("SaveSlot1-0x6afcc-mode-while-writing", T, BUI,
     "    *pEnv->pnListMode = PUFO_MAINMENU_MODE_SAVING;              /* 0x6AF90: no rows while writing */",
     "    *pEnv->pnListMode = PUFO_MAINMENU_MODE_SLOTCLICK;           /* mutation: 1 instead of 4 */")

# 0x6B040 `MOV R0,#1` / 0x6B044 STR (+0x28 / +0x2C of Slot2): the mode is back to 1 BEFORE the tail call.
case("SaveSlot2-0x6b018-mode-before-close", T, BUI,
     "    *pEnv->pnListMode = PUFO_MAINMENU_MODE_SLOTCLICK;           /* 0x6AFAC */",
     "    *pEnv->pnListMode = PUFO_MAINMENU_MODE_LOADING;             /* mutation: 3 instead of 1 */")

# 0x6B080 (LDR gOptionsScratch) / 0x6B084 (MOV R1,#0) / 0x6B088 (STRB) of Slot3: the tab byte is cleared
# AFTER SaveGame, not before it.
case("SaveSlot3-0x6b064-tab-clear-dropped", T, BUI,
     "    *pEnv->pbTab = 0;                                           /* 0x6AFA4 */\n",
     "")

# 0x6B0E0 loads the screen block, 0x6B0E4 POPs and 0x6B0E8 tail-calls Geoscape_CloseUI (+0x30, +0x34, +0x38).
case("SaveSlot4-0x6b0b0-close-dropped", T, BUI,
     "    Geoscape_CloseUI(pEnv->pClose);                             /* 0x6AFB8, the tail call */\n",
     "")

# 0x6B108 `MOV R1,#5`: the slot immediate belongs to the handler, and SaveGame receives it.
case("SaveSlot5-0x6b0fc-slot-argument", T, BUI,
     "    pEnv->pIn->nSlot = nSlot;                                   /* the original's second argument */",
     "    pEnv->pIn->nSlot = nSlot + 1;                               /* mutation: off by one */")

# Slot6 stores mode 4 at 0x6B150/0x6B158 (+0x08, +0x10) BEFORE the BL SaveGame at 0x6B160 (+0x18) -- that is
# the only reason a render hook inside SaveGame sees 4 at all.
case("SaveSlot6-0x6b148-mode-after-save", T, BUI,
     "    *pEnv->pnListMode = PUFO_MAINMENU_MODE_SAVING;              /* 0x6AF90: no rows while writing */\n"
     "    pEnv->pIn->nSlot = nSlot;                                   /* the original's second argument */\n"
     "    (void)SaveGame(pEnv->pDat, pEnv->pIn);                      /* 0x6AF98 */",
     "    pEnv->pIn->nSlot = nSlot;                                   /* mutation: mode 4 stored too late */\n"
     "    (void)SaveGame(pEnv->pDat, pEnv->pIn);                      /* 0x6AF98 */\n"
     "    *pEnv->pnListMode = PUFO_MAINMENU_MODE_SAVING;")

# ---- the seven load handlers (LoadGame_Slot_InitOptions) ----------------------------------------------

# 0x6B244 `MOVEQ R0,#1` / 0x6B248 STREQ: a failed load puts the list mode back to 1.
case("LoadSlot1-0x6b1fc-failure-mode", T, BUI,
     "        *pEnv->pnListMode = PUFO_MAINMENU_MODE_SLOTCLICK;       /* 0x6B1E0 */",
     "        *pEnv->pnListMode = PUFO_MAINMENU_MODE_LOADING;         /* mutation: stays 3 */")

# Slot2: 0x6B27C loads nGeoscapeScreen and 0x6B280 stores 0 into it (+0x18, +0x1C) before anything else
# happens.
case("LoadSlot2-0x6b264-screen-clear-dropped", T, BUI,
     "    if (pEnv->pnGeoscapeScreen != NULL) {\n        *pEnv->pnGeoscapeScreen = 0;                            /* 0x6B1B0 */\n    }\n",
     "")

# Slot3: 0x6B2F4 (MOV R1,#1) / 0x6B2F8 (LDR =(gOptionsScratch+4)) / 0x6B2FC (STRB) raise the controls byte --
# the same byte OptionsScreen_Init tests at 0x6BE84, which is what forces its teardown branch.
case("LoadSlot3-0x6b2cc-controls-byte", T, BUI,
     "        *pEnv->pbControlsExist = 1;                             /* 0x6B1C4 */",
     "        *pEnv->pbControlsExist = 0;                             /* mutation: leaves the screen built */")

# 0x6B368 is Slot4's `BL OptionsScreen_Init` (+0x34, right after the byte it raises).
case("LoadSlot4-0x6b334-init-dropped", T, BUI,
     "    OptionsScreen_Init(pEnv->pOptionsInit);                     /* 0x6B1C8, so this is the teardown */\n",
     "")

# Slot5's failure arm is `ANDS R3,R0,#0xFF` at 0x6b3e0 (`+0x44`), `MOVEQ R0,#1` at 0x6B3E4 and the STREQ at
# 0x6B3E8: the handler returns 1 there, not the callee's value.
case("LoadSlot5-0x6b39c-failure-return", T, BUI,
     "        *pEnv->pnListMode = PUFO_MAINMENU_MODE_SLOTCLICK;       /* 0x6B1E0 */\n        return 1;",
     "        *pEnv->pnListMode = PUFO_MAINMENU_MODE_SLOTCLICK;       /* 0x6B1E0 */\n        return nRet;")

# 0x6B43C `MOV R1,#6` for MenuCtl Load slot 6: LoadGame receives the handler's own slot.
case("LoadSlot6-0x6b404-slot-argument", T, BUI,
     "    pEnv->pLoadEnv->nSlot = nSlot;                              /* the original's second argument */",
     "    pEnv->pLoadEnv->nSlot = nSlot + 1;                          /* mutation: off by one */")

# ---- the ufopaedia one-shot cache ---------------------------------------------------------------------

# 0x99FD4 returns as soon as the flag is up, so the cache is filled once and never refreshed.
case("CacheSelRow-0x99fd0-one-shot", T, BUI,
     "    *pbFlag = 1;                              /* 0x99FE4 */",
     "    *pbFlag = 0;                              /* mutation: re-arms itself every call */")


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    skipped = 0
    inert = 0
    total = 0
    for name, target, path, old, new, show in CASES:
        if only and only not in name:
            continue
        r = run_case(name, target, path, old, new, show)
        if r is None:
            skipped += 1
        elif len(r) == 0:
            inert += 1
        else:
            total += len(r)
    print("cases:", len(CASES), "skipped:", skipped, "inert:", inert,
          "failing checks in total:", total)


if __name__ == "__main__":
    main()
