#!/usr/bin/env python3
"""Part 129's mutation proofs: the 50 `ui-core` rows row 121a had uncited.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part129.py
An optional substring argument runs one case:      python -I docs/parity/mutations_part129.py 11020

Rows: PadImage_ctor 0x11000, PadImage_FreeData 0x1100c, PadImage_Load 0x11020, PadImage_BlitEx 0x11110,
UiBank_FreeAllPads 0x1117c, UiBank_CtorAll 0x11320, UiScreen_OnBack 0x15670, UiScreen_DismissHigh 0x159ec,
UiBank_SelectRadio0 0x16e04, UiBank_SelectRadio2 0x16e7c, UiBank_SelectRadio3 0x16eb8,
Ui_ToggleMusicMute 0x19948, Ui_ToggleSfxMute 0x199ac, EmptyBtn_OnClick 0x1a228, UiControl_ctor 0x1a28c,
UiControl_FreePads 0x1a2c8, UiControl_BlitActivePad 0x1a340, UiControl_SetPosMode 0x1a5c0,
UiControl_Setup 0x1a75c, StatusMsg_DrawStack 0x1afc4, Font_LoadTactSprites 0x257c4,
Ui_FillActionCosts 0x26104, Text_MeasureWidth 0x2dca8, StatusMsg_Shift 0x2dd20, PadImage_BlitDecimal 0x36c58,
UiPanel_FreeLines 0x3e8c8, UiPanel_SetBytes6to11 0x3e914, UiPanel_SetBytes3to5 0x3e93c,
UiScrollList_ClickRow 0x3eee0, UiPanel_SetBytes0to2 0x3f05c, UiPanel_SetLineMeta 0x3f06c,
Font_Init 0x460e8, Font_Free 0x46144, Font_BuildIndex 0x46480, Font_GetSprByIndex 0x46600,
Font_DrawGlyph 0x4661c, Font_DrawGlyph_IndexedOffset 0x46630, UiPanel_InsertLine 0x5a294,
UiPanel_GetLineColor 0x5a33c, UiPanel_AppendLineWithSound 0x5a394, UiPanel_SetLineColors 0x5a450,
Ui_SwapStringSlots 0x6aaac, Ui_CloseScreen18 0x84ecc, Ui_OpenFunding 0x85164, Ui_InitTransfer 0x9955c,
UiPanel_ScrollNext 0x99f78, UiPanel_ScrollPrev 0x99f88, UiPanel_ScrollNextSel 0x99f98,
UiPanel_ScrollPrevSel 0x99fb4, Font_BlitGlyphToSoft 0x9ec48.

Every case attacks the one thing that row's own checks pin -- which field a setter publishes, which argument a
wrapper forwards, which span a walk covers, which operand a formula reads. `src/` is CRLF, so `case()`
translates a "\n" in the anchor into the file's own line ending; a multi-line anchor that does not is a silent
SKIP.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

RENDER = "src/pufo/render/pufo_render.c"
CONTROL = "src/pufo/ui/pufo_control.c"
PANEL = "src/pufo/ui/pufo_panel.c"
LINELIST = "src/pufo/ui/pufo_linelist.c"
FONT = "src/pufo/game/pufo_font.c"
MENU = "src/pufo/game/pufo_menu.c"
TACTUI = "src/pufo/game/pufo_tactui.c"
INPUT = "src/pufo/game/pufo_input.c"
STATUS = "src/pufo/game/pufo_statusmsg.c"
HUD = "src/pufo/game/pufo_tacthud.c"
BASEUI = "src/pufo/game/pufo_baseui.c"

T_RENDER = "test_render"
T_CONTROL = "test_control"
T_PANEL = "test_panel"
T_FONT = "test_font"
T_MENU = "test_menu"
T_TACTUI = "test_tactui"
T_INPUT = "test_input"
T_STATUS = "test_statusmsg"
T_HUD = "test_tacthud"
T_UIBANK = "test_uibank"
T_BASEUI = "test_baseui"

CASES = []


def case(name, target, path, old, new, show=8):
    data = open(path, "rb").read()
    crlf = data.count(b"\r\n") > data.count(b"\n") - data.count(b"\r\n")
    eol = "\r\n" if crlf else "\n"
    CASES.append((name, target, path, old.replace("\n", eol).encode(),
                  new.replace("\n", eol).encode(), show))


# ---- PadImage family (pufo_render.c) ---------------------------------------------------------------

# PadImage_ctor @ 0x11000 writes the one field: pData. The record around it is left as it was.
case("PadImage_ctor-0x11000-no-store", T_RENDER, RENDER,
     "void PadImage_ctor(PadImage *pPad)\n{\n    pPad->pData = NULL;\n}",
     "void PadImage_ctor(PadImage *pPad)\n{\n}")

# PadImage_FreeData @ 0x1100c frees; the port also nulls (a recorded deviation). Drop the null.
case("PadImage_FreeData-0x1100c-no-null", T_RENDER, RENDER,
     "    free(pPad->pData);\n    pPad->pData = NULL;",
     "    free(pPad->pData);")

# PadImage_Load @ 0x11020 reads the u16 pair in order: width from bytes 0..1, height from 2..3.
case("PadImage_Load-0x11020-size-order", T_RENDER, RENDER,
     "    pPad->uWidth  = (u16)pPad->pData[0] | ((u16)pPad->pData[1] << 8);\n"
     "    pPad->uHeight = (u16)pPad->pData[2] | ((u16)pPad->pData[3] << 8);",
     "    pPad->uWidth  = (u16)pPad->pData[2] | ((u16)pPad->pData[3] << 8);\n"
     "    pPad->uHeight = (u16)pPad->pData[0] | ((u16)pPad->pData[1] << 8);")

# PadImage_BlitEx @ 0x11110 forwards the caller's colour key; the key is what skips a pixel.
case("PadImage_BlitEx-0x11110-key-dropped", T_RENDER, RENDER,
     "    SoftSurf_BlitIndexedKeyed(pPad->nOriginX, pPad->nOriginY, pPad->pData, pPad->nMode, nKey);",
     "    SoftSurf_BlitIndexedKeyed(pPad->nOriginX, pPad->nOriginY, pPad->pData, pPad->nMode, 0);")

# ---- the bank spans (pufo_control.c) ---------------------------------------------------------------

# UiBank_CtorAll @ 0x11320 ends by clearing the two bank flags (0x113FC, then 0x113EC).
case("UiBank_CtorAll-0x11320-action-flag", T_UIBANK, CONTROL,
     "        *pbActionFlag = false;   /* UiBank_bActionFlag @ 0xAD711 */",
     "        *pbActionFlag = true;")

# UiBank_FreeAllPads @ 0x1117c walks the same spans backwards: its first action is widget 129.
case("UiBank_FreeAllPads-0x1117c-first-span", T_UIBANK, CONTROL,
     "    for (i32 g = PUFO_PBANK_SPAN_COUNT - 1; g >= 0; --g) {",
     "    for (i32 g = PUFO_PBANK_SPAN_COUNT - 2; g >= 0; --g) {")

# UiControl_ctor @ 0x1a28c sets width and height to 10 (`MOV R2,#0xA` at 0x1A290).
case("UiControl_ctor-0x1a28c-width", T_CONTROL, CONTROL,
     "    pCtl->uWidth   = 10;",
     "    pCtl->uWidth   = 0;")

# UiControl_FreePads @ 0x1a2c8 releases each pad's data.
case("UiControl_FreePads-0x1a2c8-no-freedata", T_CONTROL, CONTROL,
     "            PadImage_FreeData(pCtl->apPads[i]);\n            /* The original operator-deletes",
     "            /* mutated: the pad record is left alone */\n            /* The original operator-deletes")

# UiControl_BlitActivePad @ 0x1a340 picks BlitEx on bUnk17 and plain Blit otherwise (0x1A3BC).
case("BlitActivePad-0x1a340-unk17-polarity", T_CONTROL, CONTROL,
     "    if (pCtl->bUnk17 != 0) {",
     "    if (pCtl->bUnk17 == 0) {")

# UiControl_SetPosMode @ 0x1a5c0: mode 2 (`CMP R2,#2` at 0x1A5D8) is the only mode keeping the size.
case("SetPosMode-0x1a5c0-mode-gate", T_CONTROL, CONTROL,
     "    if (nMode != 2) {",
     "    if (nMode == 2) {")

# UiControl_Setup @ 0x1a75c adds the descriptor's own offsets to the base (ADD y 0x1A790, ADD x 0x1A794).
case("Setup-0x1a75c-offset-pair", T_CONTROL, CONTROL,
     "    UiControl_SetPosMode(pCtl, (i16)(xBase + pDesc->xOff), (i16)(yBase + pDesc->yOff),",
     "    UiControl_SetPosMode(pCtl, (i16)(xBase + pDesc->yOff), (i16)(yBase + pDesc->xOff),")

# EmptyBtn_OnClick @ 0x1a228 plays 0x10 = 16 (0x1A230) before clearing the draw mode.
case("EmptyBtn_OnClick-0x1a228-sample", T_CONTROL, CONTROL,
     "        pfnPlaySample(PUFO_CONTROL_SAMPLE_CLICK, pUser);",
     "        pfnPlaySample(17, pUser);")

# ---- the back / dismiss pair (pufo_input.c) ---------------------------------------------------------

# UiScreen_OnBack @ 0x15670 clears UiBank_bActionFlag first (0x15688), whatever the screen.
case("OnBack-0x15670-action-flag", T_INPUT, INPUT,
     "        *pbEscapeFlag = false; /* UiBank_bActionFlag = 0 */",
     "        *pbEscapeFlag = true; /* mutated: the flag is left set */")

# UiScreen_DismissHigh @ 0x159ec clears the screen id after UiModal_End (0x15A38 then the write at 0x15A40).
case("DismissHigh-0x159ec-screen-clear", T_INPUT, INPUT,
     "            pState->nUiScreen = UI_SCREEN_NONE;\n            return PUFO_UI_DISMISS_MODAL;",
     "            return PUFO_UI_DISMISS_MODAL;")

# ---- the radio handlers (pufo_tactui.c) -------------------------------------------------------------

# 0x16E0C loads #0x11 and 0x16E10 calls Sound_Play: the sample comes before the field writes.
case("SelectRadio-0x16e04-sample", T_TACTUI, TACTUI,
     "    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);\n    memset(pUi->abRadio, 0, sizeof(pUi->abRadio));",
     "    memset(pUi->abRadio, 0, sizeof(pUi->abRadio));")

# 0x16E94..0x16EAC: the four STRB write a one-hot field, so the other three must go to zero.
case("SelectRadio-0x16e7c-one-hot", T_TACTUI, TACTUI,
     "    memset(pUi->abRadio, 0, sizeof(pUi->abRadio));\n    pUi->abRadio[nRadio] = 1;",
     "    pUi->abRadio[nRadio] = 1;")

# 0x16EB8 writes its own option: the store lands on the index the handler names.
case("SelectRadio-0x16eb8-index", T_TACTUI, TACTUI,
     "    pUi->abRadio[nRadio] = 1;",
     "    pUi->abRadio[0] = 1;")

# ---- the mute pair (pufo_menu.c) ---------------------------------------------------------------------

# Ui_ToggleMusicMute @ 0x19948 applies the music volume on both arms (0x19974, 0x19994).
case("ToggleMusicMute-0x19948-clear-arm", T_MENU, MENU,
     "        pOpt->bMusicOff = 0;",
     "        pOpt->bMusicOff = 1;")

# Ui_ToggleSfxMute @ 0x199ac stores the new flag into its pad last (0x199A0).
case("ToggleSfxMute-0x199ac-pad-value", T_MENU, MENU,
     "    if (pPad != NULL) {\n        *pPad = bNew ? 1 : 0;\n    }\n    return bNew;\n}\n\n"
     "/* Menu_ToggleForceNoDrag",
     "    if (pPad != NULL) {\n        *pPad = bNew ? 0 : 1;\n    }\n    return bNew;\n}\n\n"
     "/* Menu_ToggleForceNoDrag")

# ---- status messages (pufo_statusmsg.c) --------------------------------------------------------------

# StatusMsg_Shift @ 0x2dd20 moves the TTL dwords as well as the text (0x2DD58 / 0x2DD60).
case("StatusMsg_Shift-0x2dd20-ttl", T_STATUS, STATUS,
     "        pStack->anTtl[i - 1] = pStack->anTtl[i];",
     "        (void)0;")

# StatusMsg_DrawStack @ 0x1afc4 steps once by 14 before the per-message 12.
case("StatusMsg_DrawStack-0x1afc4-first-step", T_STATUS, STATUS,
     "    nDrawY -= 14;",
     "    nDrawY -= 12;")

# ---- the decimal digit table (pufo_tacthud.c) --------------------------------------------------------

# PadImage_BlitDecimal @ 0x36c58 fills with slot 10 when bLeadZero and slot 0 otherwise (0x36CB0).
case("BlitDecimal-0x36c58-filler-slot", T_HUD, HUD,
     "        const i32 nFill = bLeadZero ? 10 : 0;",
     "        const i32 nFill = bLeadZero ? 0 : 10;")

# ---- the panel style setters (pufo_panel.c) ----------------------------------------------------------

# UiPanel_SetBytes0to2 @ 0x3f05c publishes bytes 0..2 of aStyleRgb.
case("SetBytes0to2-0x3f05c-byte2", T_PANEL, PANEL,
     "    pPanel->aStyleRgb[2] = b2;",
     "    pPanel->aStyleRgb[2] = 0;")

# UiPanel_SetBytes3to5 @ 0x3e93c publishes the disjoint range 3..5.
case("SetBytes3to5-0x3e93c-byte4", T_PANEL, PANEL,
     "    pPanel->aStyleRgb[4] = b4;",
     "    pPanel->aStyleRgb[4] = 0;")

# UiPanel_SetBytes6to11 @ 0x3e914 publishes the six bytes 6..11.
case("SetBytes6to11-0x3e914-byte9", T_PANEL, PANEL,
     "    pPanel->aStyleRgb[9]  = b9;",
     "    pPanel->aStyleRgb[9]  = 0;")

# UiPanel_SetLineMeta @ 0x3f06c writes meta +0..+5; byte 3 is one of the six.
case("SetLineMeta-0x3f06c-byte3", T_PANEL, PANEL,
     "    pMeta[3] = b3;",
     "    pMeta[3] = 0;")

# UiPanel_FreeLines @ 0x3e8c8 frees exactly nLineCount entries, not the capacity.
case("FreeLines-0x3e8c8-bound", T_PANEL, PANEL,
     "    for (i = 0; i < pPanel->nLineCount && i < PUFO_PANEL_LINES; ++i) {",
     "    for (i = 0; i < PUFO_PANEL_LINES; ++i) {")

# UiScrollList_ClickRow @ 0x3eee0 stores the row into nHoverRow (+0xC) and never touches nUnk3028.
case("ClickRow-0x3eee0-field-choice", T_PANEL, PANEL,
     "    pPanel->nHoverRow = nRow;\n    pPanel->bHoverActive = 0;",
     "    pPanel->nUnk3028 = nRow;\n    pPanel->bHoverActive = 0;")

# Ui_SwapStringSlots @ 0x6aaac swaps the aux dword of each slot along with the string.
case("SwapLines-0x6aaac-aux", T_PANEL, PANEL,
     "    pPanel->anLineAux[nIdxA] = nAuxB;",
     "    pPanel->anLineAux[nIdxA] = nAuxA;")

# UiPanel_ScrollNext @ 0x99f78 adds one while the cursor is under nLineCount - nStyle.
case("ScrollNext-0x99f78-step", T_PANEL, PANEL,
     "        pPanel->nUnk3028 += 1;",
     "        pPanel->nUnk3028 += 2;")

# UiPanel_ScrollPrev @ 0x99f88 subtracts one while the cursor is above zero.
case("ScrollPrev-0x99f88-step", T_PANEL, PANEL,
     "        pPanel->nUnk3028 -= 1;",
     "        pPanel->nUnk3028 -= 2;")

# ---- the line-list family (pufo_linelist.c) -----------------------------------------------------------

# UiPanel_InsertLine @ 0x5a294 ends with the byte increment at 0x5A32C, on every path.
case("InsertLine-0x5a294-count-increment", T_PANEL, LINELIST,
     "    pList->nLineCount = (u8)(pList->nLineCount + 1);",
     "    /* mutated: the count byte is not written */")

# UiPanel_GetLineColor @ 0x5a33c answers the slot the caller named (slot 0..3, nothing else).
case("GetLineColor-0x5a33c-slot-read", T_PANEL, LINELIST,
     "    return pList->anColor[nLine][nSlot];",
     "    return pList->anColor[nLine][0];")

# UiPanel_AppendLineWithSound @ 0x5a394 plays 7 after the insert.
case("AppendLineWithSound-0x5a394-sample", T_PANEL, LINELIST,
     "        pfnPlaySample(7, pUser);",
     "        pfnPlaySample(17, pUser);")

# UiPanel_SetLineColors @ 0x5a450 writes the flag byte as well as the four colour slots.
case("SetLineColors-0x5a450-flag", T_PANEL, LINELIST,
     "    if (pList == NULL || (i32)nLine >= PUFO_LINELIST_CAPACITY) {\n"
     "        return;\n    }\n"
     "    Line_Write(pList, (i32)nLine, bFlag, nC0, nC1, nC2, nC3);",
     "    if (pList == NULL || (i32)nLine >= PUFO_LINELIST_CAPACITY) {\n"
     "        return;\n    }\n"
     "    Line_Write(pList, (i32)nLine, 0, nC0, nC1, nC2, nC3);")

# ---- the font family (pufo_font.c) ---------------------------------------------------------------------

# Font_Init @ 0x460e8 clears the block and then sets bInited and the counts it re-reads.
case("Font_Init-0x460e8-no-clear", T_FONT, FONT,
     "    memset(pFont, 0, sizeof(*pFont));",
     "    /* mutated: the block is not cleared */")

# Font_Free @ 0x46144 leaves the counts cleared while bInited is the only flag it resets.
case("Font_Free-0x46144-count-kept", T_FONT, FONT,
     "    pFont->nSlotCount  = 0;",
     "    /* mutated: nSlotCount keeps its value */")

# Font_BuildIndex @ 0x46480 stores `block + offset`, the offset read from +2+2k.
case("BuildIndex-0x46480-offset", T_FONT, FONT,
     "                pFont->apSprIndex[nSpr++] = (u8 *)(pSpr + nOff);",
     "                pFont->apSprIndex[nSpr++] = (u8 *)(pSpr + nOff + 1);")

# Font_GetSprByIndex @ 0x46600 has no bound at all in the original; the port bounds it.
case("GetSprByIndex-0x46600-bound", T_FONT, FONT,
     "    if (pFont->apMc1Index == NULL || (u32)nIndex >= pFont->nMc1IndexCount) {",
     "    if (pFont->apMc1Index == NULL) {")

# Font_DrawGlyph @ 0x4661c clamps nPal before it is used.
case("DrawGlyph-0x4661c-pal-clamp", T_FONT, FONT,
     "    if (nPal > PUFO_FONT_PAL_MAX) {\n        nPal = PUFO_FONT_PAL_MAX;\n    }",
     "    /* mutated: nPal is not clamped */")

# Font_BlitGlyphToSoft @ 0x9ec48 picks the palette as (glyph base + nPal).
case("BlitGlyphToSoft-0x9ec48-base", T_FONT, FONT,
     "                   g_aPalette[(g_nGlyphPalSlotBase + nPal) & (PUFO_PAL_SLOTS - 1)]);",
     "                   g_aPalette[nPal & (PUFO_PAL_SLOTS - 1)]);")

# Font_DrawGlyph_IndexedOffset @ 0x46630 adds the BYTE stored in the record at nIdx (0x46654).
case("IndexedOffset-0x46630-byte-read", T_FONT, FONT,
     "    nSprIdx = (u16)(g_fontSys.anSprBase[pTbl->nSlot] + ((const u8 *)pTbl)[nIdx]);",
     "    nSprIdx = (u16)(g_fontSys.anSprBase[pTbl->nSlot] + nIdx);")

# Text_MeasureWidth @ 0x2dca8 sums the per-byte advance table.
case("Text_MeasureWidth-0x2dca8-advance", T_FONT, FONT,
     "        nWidth += g_fontMetrics.aAdvance[(u8)*psz];",
     "        nWidth += 1;")

# Font_LoadTactSprites @ 0x257c4 opens with the sprite total at the time it is entered (0x257DC).
case("LoadTactSprites-0x257c4-first-base", T_FONT, FONT,
     "    pFont->anFlagSprBase[0] = pFont->nTotalSprFrames; /* 0x257DC */",
     "    pFont->anFlagSprBase[0] = pFont->nTotalMc1;")

# ---- the action cost table (pufo_tactui.c) -------------------------------------------------------------

# Ui_FillActionCosts @ 0x26104: the offered amount uses abCostPctPacked, the price uses abFlagByMode.
case("FillActionCosts-0x26104-input-choice", T_TACTUI, TACTUI,
     "                    nEnabled = (i32)((double)pIn->auStat4\n"
     "                                     * (double)pIn->abCostPctPacked[m]\n"
     "                                     * PUFO_ACTION_COST_SCALE);",
     "                    nEnabled = (i32)((double)pIn->auStat4\n"
     "                                     * (double)pIn->abFlagByMode[m]\n"
     "                                     * PUFO_ACTION_COST_SCALE);")

# ---- the baseui screens (pufo_baseui.c) ----------------------------------------------------------------

# Ui_CloseScreen18 @ 0x84ecc hands options the literal -1 (0x84EC8 MOV R1,#0xFFFFFFFF).
case("CloseScreen18-0x84ecc-mode-literal", T_BASEUI, BASEUI,
     "        pHooks->pfnOptionsOpenWithSound(-1, pHooks->pUser); /* 0x84EC8 MOV R1,#0xFFFFFFFF */",
     "        pHooks->pfnOptionsOpenWithSound(0, pHooks->pUser);")

# Ui_OpenFunding @ 0x85164 sets the screen id to 15 before opening the funding screen.
case("OpenFunding-0x85164-screen-id", T_BASEUI, BASEUI,
     "    if (pState != NULL) {\n        pState->nGeoscapeScreen = 15;\n    }\n"
     "    if (pHooks != NULL && pHooks->pfnFundingOpen != NULL) {",
     "    if (pState != NULL) {\n        pState->nGeoscapeScreen = 18;\n    }\n"
     "    if (pHooks != NULL && pHooks->pfnFundingOpen != NULL) {")

# Ui_InitTransfer @ 0x9955c marks the transfer possible (0x995B8) after the panels are built.
case("InitTransfer-0x9955c-transfer-ok", T_BASEUI, BASEUI,
     "        *pEnv->pbTransferOk = 1; /* 0x995B8 */",
     "        *pEnv->pbTransferOk = 0;")

# UiPanel_ScrollPrevSel @ 0x99fb4 is the handler slot 6 carries; UiPanel_ScrollNextSel @ 0x99f98 is slot 7's.
case("ScrollPrevSel-0x99fb4-slot6", T_BASEUI, BASEUI,
     "        pEnv->apCtl[4]->pfnClick = pEnv->pfnSelPrev;   /* 0x9DE8C */",
     "        pEnv->apCtl[4]->pfnClick = pEnv->pfnSelNext;")
case("ScrollNextSel-0x99f98-slot7", T_BASEUI, BASEUI,
     "        pEnv->apCtl[5]->pfnClick = pEnv->pfnSelNext;   /* 0x9DE98 */",
     "        pEnv->apCtl[5]->pfnClick = pEnv->pfnSelPrev;")


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
