#!/usr/bin/env python3
"""Part 130's mutation proofs: 40 `strat-ui` rows -- the options-menu and start-menu button handlers.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part130.py
An optional substring argument runs one case:      python -I docs/parity/mutations_part130.py LoadSlot

Rows: MenuBtn_SetSpeed0..4 0x18ecc/0x18f20/0x18f74/0x18fc8/0x1901c, MenuBtn_Confirm1/2/3/4/5
0x196e8/0x19884/0x198a4/0x19910/0x1a014, MenuBtn_ResizeWindow 0x19930, Options_SetMusicVol1..5
0x19a10/0x19a64/0x19ab8/0x19b0c/0x19b60, MenuBtn_SetSfxVol1..5 0x19bb4/0x19c14/0x19c74/0x19cd4/0x19d34,
Menu_ToggleForceNoDrag 0x19d94, SaveLoad_Slot0..6 0x19de4/0x19e34/0x19e84/0x19ed4/0x19f24/0x19f74/0x19fc4,
MenuCtl1 0x1a034, MenuCtl2 0x1a064, MenuCtl3 0x1a080, MenuCtl0 0x1a09c, MenuCtl8..14
0x1a0bc/0x1a0f0/0x1a124/0x1a158/0x1a18c/0x1a1c0/0x1a1f4.

Each group is ONE port function whose originals are copies differing only in an immediate, so every case
attacks a different claim that group's checks pin -- the stored level, the pad index, the group a pad
belongs to, whether a sample happens at all, the order of the sample and the apply, which argument reaches
the callee, the low-byte test, and the mode a failure restores. `src/` is CRLF, so `case()` translates a
"\n" in the anchor into the file's own line ending.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

MENU = "src/pufo/game/pufo_menu.c"
BTN = "src/pufo/ui/pufo_menubtn.c"

T_MENU = "test_menu"
T_BTN = "test_menubtn"

CASES = []


def case(name, target, path, old, new, show=8):
    data = open(path, "rb").read()
    crlf = data.count(b"\r\n") > data.count(b"\n") - data.count(b"\r\n")
    eol = "\r\n" if crlf else "\n"
    CASES.append((name, target, path, old.replace("\n", eol).encode(),
                  new.replace("\n", eol).encode(), show))


# ---- the five speed buttons (Menu_SetSpeed) --------------------------------------------------------

# 0x18ecc lights unk_ACE5A, the FIRST of the five speed pads (0x18EE4); the index is the button's own.
case("SetSpeed0-0x18ecc-pad-index", T_MENU, MENU,
     "    Pads_MakeOneHot(pPads->abSpeed, nStep);",
     "    Pads_MakeOneHot(pPads->abSpeed, PUFO_MENU_STEPS - 1 - nStep);")

# 0x18F14 tails into GameSpeed_Set with the level in R1 (MOV R1,#1 for SetSpeed1 at 0x18F5C).
case("SetSpeed1-0x18f20-speed-arg", T_MENU, MENU,
     "    return GameSpeed_Set(pClock, (u32)nStep);",
     "    return GameSpeed_Set(pClock, (u32)(nStep + 1));")

# 0x18ED8 is `MOV R0,#0x11` + `MOV R1,#4` + BL Sound_Play: the speed family's sample id is 17, not the
# sfx family's 16. (Dropping the call instead leaves pHooks unreferenced and /WX stops the build.)
case("SetSpeed2-0x18f74-sample-id", T_MENU, MENU,
     "    Menu_Play(pHooks, PUFO_MENU_SAMPLE_PRE);\n    Pads_MakeOneHot(pPads->abSpeed, nStep);",
     "    Menu_Play(pHooks, 15);\n    Pads_MakeOneHot(pPads->abSpeed, nStep);")

# The speed button writes ONLY the speed group: the gamma/music/sfx pad groups are other globals.
case("SetSpeed3-0x18fc4-pad-group", T_MENU, MENU,
     "    Pads_MakeOneHot(pPads->abSpeed, nStep);\n    /* GameSpeed_Set",
     "    Pads_MakeOneHot(pPads->abGamma, nStep);\n    /* GameSpeed_Set")

# 0x1901C..0x19058 stores all five pad bytes -- four #0 and one #1 -- so the handler clears the group it
# lights. (The step-5 refusal is NOT this function's behaviour: dropping the port's ValidStep guard was
# behaviour-identical, because GameSpeed_Set bounds the level itself.)
case("SetSpeed4-0x1901c-pads-cleared", T_MENU, MENU,
     "    Pads_MakeOneHot(pPads->abSpeed, nStep);",
     "    pPads->abSpeed[nStep] = 1;")

# ---- the five music buttons (Menu_SetMusicVolume) ---------------------------------------------------

# 0x19A24 stores the immediate 1 (Vol5 stores 5 at 0x19B74): the stored value is 1..5, not 0..4.
case("MusicVol1-0x19a10-stored-value", T_MENU, MENU,
     "    pOpt->nMusicVolume = nStep + 1;",
     "    pOpt->nMusicVolume = nStep;")

# 0x19A8C lights the SECOND pad (unk_AD0BA) for Vol2: the pad index is the value minus one.
case("MusicVol2-0x19a64-pad-index", T_MENU, MENU,
     "    Pads_MakeOneHot(pPads->abMusic, nStep);",
     "    Pads_MakeOneHot(pPads->abMusic, PUFO_MENU_STEPS - 1 - nStep);")

# 0x19A10..0x19B60 contain no BL Sound_Play at all -- that is the whole difference from the sfx group.
case("MusicVol3-0x19ab8-sample-added", T_MENU, MENU,
     "    pOpt->nMusicVolume = nStep + 1;\n    Pads_MakeOneHot(pPads->abMusic, nStep);",
     "    pOpt->nMusicVolume = nStep + 1;\n    Menu_Play(pHooks, 16);\n"
     "    Pads_MakeOneHot(pPads->abMusic, nStep);")

# 0x19A28 is the apply, and it is the only thing this family does besides the two stores. Removing it
# leaves pHooks unreferenced, so the mutation keeps the call and puts it on the OTHER apply instead.
case("MusicVol4-0x19b0c-apply-target", T_MENU, MENU,
     "    pOpt->nMusicVolume = nStep + 1;\n    Pads_MakeOneHot(pPads->abMusic, nStep);\n"
     "    if (pHooks != NULL && pHooks->pfnApplyMusicVolume != NULL) {\n"
     "        pHooks->pfnApplyMusicVolume(pHooks->pUser);",
     "    pOpt->nMusicVolume = nStep + 1;\n    Pads_MakeOneHot(pPads->abMusic, nStep);\n"
     "    if (pHooks != NULL && pHooks->pfnApplySfxVolume != NULL) {\n"
     "        pHooks->pfnApplySfxVolume(pHooks->pUser);")

# The music pads are 0xAD09A..0xAD11A -- a different group from the sfx pads at 0xAD13A.
case("MusicVol5-0x19b60-pad-group", T_MENU, MENU,
     "    Pads_MakeOneHot(pPads->abMusic, nStep);",
     "    Pads_MakeOneHot(pPads->abPen, nStep);")

# ---- the five sfx buttons (Menu_SetSfxVolume) -------------------------------------------------------

# 0x19BC8 stores the volume and 0x19BCC applies it, and only then 0x19C08 tails into Sound_Play.
case("SfxVol1-0x19bb4-apply-then-sample", T_MENU, MENU,
     "    pOpt->nSoundVolume = nStep + 1;\n    Pads_MakeOneHot(pPads->abSfx, nStep);\n"
     "    if (pHooks != NULL && pHooks->pfnApplySfxVolume != NULL) {\n"
     "        pHooks->pfnApplySfxVolume(pHooks->pUser);\n    }\n"
     "    Menu_Play(pHooks, PUFO_MENU_SAMPLE_POST);\n    return true;",
     "    pOpt->nSoundVolume = nStep + 1;\n    Pads_MakeOneHot(pPads->abSfx, nStep);\n"
     "    Menu_Play(pHooks, PUFO_MENU_SAMPLE_POST);\n"
     "    if (pHooks != NULL && pHooks->pfnApplySfxVolume != NULL) {\n"
     "        pHooks->pfnApplySfxVolume(pHooks->pUser);\n    }\n    return true;")

# Vol2 stores the immediate 2 (0x19C1C MOV R1,#2, STR at 0x19C28).
case("SfxVol2-0x19c14-stored-value", T_MENU, MENU,
     "    pOpt->nSoundVolume = nStep + 1;",
     "    pOpt->nSoundVolume = nStep;")

# 0x19C3C lights the second sfx pad (unk_AD15A) for Vol2.
case("SfxVol3-0x19c74-pad-index", T_MENU, MENU,
     "    Pads_MakeOneHot(pPads->abSfx, nStep);",
     "    Pads_MakeOneHot(pPads->abSfx, PUFO_MENU_STEPS - 1 - nStep);")

# 0x19C60 loads #0x10: the sfx group's sample is 16, not the speed group's 17.
case("SfxVol4-0x19cd4-sample-value", T_MENU, MENU,
     "    Menu_Play(pHooks, PUFO_MENU_SAMPLE_POST);\n    return true;",
     "    Menu_Play(pHooks, PUFO_MENU_SAMPLE_PRE);\n    return true;")

# The sfx pads 0xAD13A..0xAD1BA are their own group.
case("SfxVol5-0x19d34-pad-group", T_MENU, MENU,
     "    Pads_MakeOneHot(pPads->abSfx, nStep);",
     "    Pads_MakeOneHot(pPads->abGamma, nStep);")

# ---- the remaining single rows ----------------------------------------------------------------------

# MenuBtn_ResizeWindow @ 0x19930: BL Sound_Play at 0x1993C and ONLY then the tail call at 0x19944.
case("ResizeWindow-0x19930-order", T_MENU, MENU,
     "    Menu_Play(pHooks, PUFO_MENU_SAMPLE_POST);\n"
     "    if (pfnResizeWindow != NULL) {\n"
     "        pfnResizeWindow(pHooks != NULL ? pHooks->pUser : NULL);\n    }",
     "    if (pfnResizeWindow != NULL) {\n        pfnResizeWindow(pHooks != NULL ? pHooks->pUser : NULL);\n"
     "    }\n    Menu_Play(pHooks, PUFO_MENU_SAMPLE_POST);")

# Menu_ToggleForceNoDrag @ 0x19d94 leaves the arm's value in R1 and the single pad store at 0x19DD4
# publishes it: 1 on the clear arm, 0 on the set arm, i.e. the inverse of the new flag.
case("ToggleForceNoDrag-0x19d94-pad-polarity", T_MENU, MENU,
     "        *pPad = bWasOn ? 1 : 0;",
     "        *pPad = bWasOn ? 0 : 1;")

# ---- the five plain confirm buttons (MenuBtn_Confirm) ------------------------------------------------

# All five bodies are the same 8 instructions: MOV R0,#0x10 / MOV R1,#4 / BL Sound_Play, LDR =pBank,
# POP, B Input_OnConfirm. The button's own sample is 16.
case("Confirm1-0x196e8-own-sample", T_BTN, BTN,
     "    Btn_PlayInput(pCtx->pHooks, nSample);\n    Input_OnConfirm(pCtx->pState, pCtx->pHooks);",
     "    Btn_PlayInput(pCtx->pHooks, nSample + 1);\n    Input_OnConfirm(pCtx->pState, pCtx->pHooks);")

# 0x196F4 is the button's own sample call and it is the FIRST of the two the screen-6 block counts;
# the original reaches it through the bank it loaded at 0x196EC, so the hooks are not NULL there.
case("Confirm2-0x19884-own-call-dropped", T_BTN, BTN,
     "    Btn_PlayInput(pCtx->pHooks, nSample);\n    Input_OnConfirm(pCtx->pState, pCtx->pHooks);",
     "    Btn_PlayInput(NULL, nSample);\n    Input_OnConfirm(pCtx->pState, pCtx->pHooks);")

# 0x196F8 loads =pBank and the tail call hands that pointer on; every hook the handler reaches comes
# from it. The mutation hands on a bank with no hooks in it: the state path runs, the adapter calls do not.
case("Confirm3-0x198a4-hook-argument", T_BTN, BTN,
     "    Btn_PlayInput(pCtx->pHooks, nSample);\n    Input_OnConfirm(pCtx->pState, pCtx->pHooks);",
     "    Btn_PlayInput(pCtx->pHooks, nSample);\n"
     "    Input_OnConfirm(pCtx->pState, &(TactInputHooks){0});")

# Unlike MenuBtn_ConfirmClear @ 0x194c8, these five never clear the screen id before the tail call --
# which is why a blocked handler leaves the screen id where it was.
case("Confirm4-0x19910-clears-the-screen", T_BTN, BTN,
     "    Btn_PlayInput(pCtx->pHooks, nSample);\n    Input_OnConfirm(pCtx->pState, pCtx->pHooks);",
     "    Btn_PlayInput(pCtx->pHooks, nSample);\n    pCtx->pState->nUiScreen = 0;\n"
     "    Input_OnConfirm(pCtx->pState, pCtx->pHooks);")

# 0x1A020..0x1A02C is Sound_Play(16,4), LDR R0,=pBank, POP and the tail B Input_OnConfirm: the handler
# call is the last thing this body does, and it is not optional.
case("Confirm5-0x1a014-tail-call-dropped", T_BTN, BTN,
     "    Btn_PlayInput(pCtx->pHooks, nSample);\n    Input_OnConfirm(pCtx->pState, pCtx->pHooks);",
     "    Btn_PlayInput(pCtx->pHooks, nSample);")

# ---- the seven save/load buttons (SaveLoad_Slot) -----------------------------------------------------

# 0x19DF4 stores 1 into MainMenu_nListMode -- not 3, which is what MenuCtl8..14 store.
case("SaveLoadSlot0-0x19de4-mode-store", T_BTN, BTN,
     "        *pnListMode = PUFO_MENU_MODE_SAVE_LOAD_LIST; /* 0x19DF4 */",
     "        *pnListMode = PUFO_MENU_MODE_LOADING;")

# 0x19E04/0x19E08: the direction is the CURRENT screen (CMP #0xA, BNE), not the button.
case("SaveLoadSlot1-0x19e34-direction", T_BTN, BTN,
     "    if (nUiScreen == PUFO_MENU_SCREEN_SAVE_LIST) {",
     "    if (nUiScreen != PUFO_MENU_SCREEN_SAVE_LIST) {")

# 0x19E0C is SaveGame with the slot the widget names in R1 (`MOV R1,#6` for Slot6 at 0x19FC4's twin).
case("SaveLoadSlot2-0x19e84-save-slot-arg", T_BTN, BTN,
     "        return pHooks->pfnSaveGame(nSlot, pHooks->pUser); /* 0x19E0C */",
     "        return pHooks->pfnSaveGame(0, pHooks->pUser);")

# 0x19E20 is LoadGame, and it gets the same slot immediate.
case("SaveLoadSlot3-0x19ed4-load-slot-arg", T_BTN, BTN,
     "    nResult = pHooks->pfnLoadGame(nSlot, pHooks->pUser); /* 0x19E20 */",
     "    nResult = pHooks->pfnLoadGame(0, pHooks->pUser);")

# 0x19E28 tests the LOW BYTE of the callee's value (`ANDS R3, R0, #0xFF`), not the whole dword.
case("SaveLoadSlot4-0x19f24-low-byte", T_BTN, BTN,
     "    if ((nResult & 0xFF) == 0) { /* 0x19E28, the LOW byte again */",
     "    if (nResult == 0) {")

# 0x19E2C restores 0, and the checks state that it is 0 and not 2.
case("SaveLoadSlot5-0x19f74-failure-mode", T_BTN, BTN,
     "            *pnListMode = 0; /* 0x19E2C, not 2 */",
     "            *pnListMode = PUFO_MENU_MODE_LOAD_LIST;")

# 0x19E10 returns straight to the epilogue on the save arm; the load arm's failure path returns 0.
case("SaveLoadSlot6-0x19fc4-failure-return", T_BTN, BTN,
     "        return 0; /* 0x19E28 */",
     "        return 1;")

# ---- the start-menu buttons (MenuCtl0..3) -------------------------------------------------------------

# 0x1A0AC runs StartUi_Shutdown and 0x1A0B8 only then tails into Geoscape_InitUI(0) (MOV R0,#0 at 0x1A0B0).
case("MenuCtl0-0x1a09c-shutdown-order", T_BTN, BTN,
     "    StartMenu_Play(pHooks);      /* 0x1A0A8 */\n    StartMenu_Shutdown(pHooks);  /* 0x1A0AC */",
     "    StartMenu_Play(pHooks);      /* 0x1A0A8 */")

# 0x1A048 runs SaveSlot_FormatList BEFORE the mode store at 0x1A054.
case("MenuCtl1-0x1a034-format-then-mode", T_BTN, BTN,
     "    StartMenu_Play(pHooks); /* 0x1A040 */\n"
     "    if (pHooks != NULL && pHooks->pfnFormatSlots != NULL) {\n"
     "        pHooks->pfnFormatSlots(pHooks->pUser); /* 0x1A048, BEFORE the mode is set */\n    }",
     "    StartMenu_Play(pHooks); /* 0x1A040 */")

# 0x1A074 StartUi_Shutdown, and only then the tail call into Titles_Init at 0x1A07C.
case("MenuCtl2-0x1a064-titles-order", T_BTN, BTN,
     "    StartMenu_Play(pHooks);\n    StartMenu_Shutdown(pHooks);\n"
     "    if (pHooks != NULL && pHooks->pfnTitles != NULL) {",
     "    StartMenu_Play(pHooks);\n    if (pHooks != NULL && pHooks->pfnTitles != NULL) {")

# 0x1A090 StartUi_Shutdown, then the tail into App_QuitDisplay at 0x1A098.
case("MenuCtl3-0x1a080-shutdown-dropped", T_BTN, BTN,
     "    StartMenu_Play(pHooks);\n    StartMenu_Shutdown(pHooks);\n"
     "    if (pHooks != NULL && pHooks->pfnQuit != NULL) {",
     "    StartMenu_Play(pHooks);\n    if (pHooks != NULL && pHooks->pfnQuit != NULL) {")

# ---- the seven load buttons (Menu_LoadSlot) -----------------------------------------------------------

# 0x1A0CC stores 3 into MainMenu_nListMode BEFORE LoadGame @ 0x1A0D4 runs.
case("LoadCtl8-0x1a0bc-mode-before-load", T_BTN, BTN,
     "    if (pnListMode != NULL) {\n"
     "        *pnListMode = PUFO_MENU_MODE_LOADING; /* 0x1A0CC, before the load runs */\n    }\n"
     "    nResult = (pfnLoadGame != NULL) ? pfnLoadGame(nSlot, pUser) : 0;",
     "    nResult = (pfnLoadGame != NULL) ? pfnLoadGame(nSlot, pUser) : 0;\n"
     "    if (pnListMode != NULL) {\n        *pnListMode = PUFO_MENU_MODE_LOADING;\n    }")

# 0x1A0DC is `ANDS R3, R0, #0xFF` -- the low byte decides, so 0x100 fails and 0x101 passes.
case("LoadCtl9-0x1a0f0-low-byte", T_BTN, BTN,
     "    if ((nResult & 0xFF) == 0) { /* 0x1A0DC tests the low BYTE of the result */",
     "    if (nResult == 0) {")

# 0x1A0E0 stores 2 and the function returns 2 -- not whatever LoadGame returned.
case("LoadCtl10-0x1a124-failure-return", T_BTN, BTN,
     "        return PUFO_MENU_MODE_LOAD_LIST; /* 0x1A0E0, not the callee's value */",
     "        return nResult;")

# The mode a failed load restores is 2 (the save list), not 0 and not 3.
case("LoadCtl11-0x1a158-failure-mode", T_BTN, BTN,
     "            *pnListMode = PUFO_MENU_MODE_LOAD_LIST;\n        }\n"
     "        return PUFO_MENU_MODE_LOAD_LIST; /* 0x1A0E0, not the callee's value */",
     "            *pnListMode = 0;\n        }\n        return PUFO_MENU_MODE_LOAD_LIST;")

# 0x1A0C8 loads the slot immediate (#0 for MenuCtl8, #6 for MenuCtl14) and 0x1A0D4 hands it to LoadGame.
# (Passing 0 instead leaves nSlot unreferenced and /WX stops the build, so the slot is shifted by one.)
case("LoadCtl12-0x1a18c-slot-argument", T_BTN, BTN,
     "    nResult = (pfnLoadGame != NULL) ? pfnLoadGame(nSlot, pUser) : 0;",
     "    nResult = (pfnLoadGame != NULL) ? pfnLoadGame(nSlot + 1, pUser) : 0;")

# 3 is the loading mode these seven store; 1 is the save/load list's and 2 is the load list's.
case("LoadCtl13-0x1a1c0-loading-mode", T_BTN, BTN,
     "        *pnListMode = PUFO_MENU_MODE_LOADING; /* 0x1A0CC, before the load runs */",
     "        *pnListMode = PUFO_MENU_MODE_SAVE_LOAD_LIST;")

# With no callee to run the original still reaches the failure arm (the port reads it as a zero result).
case("LoadCtl14-0x1a1f4-missing-callee", T_BTN, BTN,
     "    nResult = (pfnLoadGame != NULL) ? pfnLoadGame(nSlot, pUser) : 0;",
     "    nResult = (pfnLoadGame != NULL) ? pfnLoadGame(nSlot, pUser) : 1;")


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    skipped = 0
    inert = 0
    total_checks = 0
    for name, target, path, old, new, show in CASES:
        if only and only not in name:
            continue
        r = run_case(name, target, path, old, new, show)
        if r is None:
            skipped += 1
        elif len(r) == 0:
            inert += 1
        else:
            total_checks += len(r)
    print("cases:", len(CASES), "skipped:", skipped, "inert:", inert,
          "failing checks in total:", total_checks)


if __name__ == "__main__":
    main()
