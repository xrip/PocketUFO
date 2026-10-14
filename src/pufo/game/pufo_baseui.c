/* pufo_baseui.c - see pufo_baseui.h for evidence. */
#include "game/pufo_baseui.h"
#include <stdlib.h>  /* atexit, for StrategList_RegisterAtexit @ 0x838a0 */
#include <stdio.h>   /* snprintf, the bounded stand-in for the original's j_itoa */
#include "util/pufo_str.h" /* pufo_strcpy/pufo_strcat, the line writes of ResearchScreen_RefreshPanels */
#include "audio/pufo_audio.h" /* Sound_Play, the screen-open sample of SoldierScreen_Toggle */
#include "game/pufo_strateg.h" /* StrategList_Clear */

void BaseUi_SetScratch4(ScreenScratch *pScratch)
{
    if (pScratch == NULL) {
        return;
    }
    pScratch->nLineAux145 = 4;
}

void BaseUi_SetScratch3AndUiY(ScreenScratch *pScratch)
{
    if (pScratch == NULL) {
        return;
    }
    pScratch->nLineAux145 = 3;
    pScratch->nControl0Y = 120;
    pScratch->nControl1Y = 142;
}

i32 BaseUi_Cache186BE8(ScreenScratch *pScratch, i32 nUnk3032)
{
    if (pScratch == NULL) {
        return nUnk3032;
    }
    pScratch->nCache186BE8 = nUnk3032;
    return nUnk3032;
}

i32 BaseUi_PlaySample7Cache160C74(ScreenScratch *pScratch, i32 nUnk3032,
                                  void (*pfnPlaySample)(u32 nSample, void *pUser),
                                  void *pUser)
{
    if (pfnPlaySample != NULL) {
        pfnPlaySample(7, pUser);
    }
    if (pScratch == NULL) {
        return nUnk3032;
    }
    pScratch->bFlag160C80 = true;
    pScratch->bFlag187080 = 0;
    pScratch->nCache160C74 = nUnk3032;
    return nUnk3032;
}

/* Ui_OpenScreen18 @ 0x84e9c. */
void Ui_OpenScreen18(GeoscapeScreenState *pState, const GeoscapeScreenHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(17, pHooks->pUser);
    }
    if (pState != NULL) {
        pState->nGeoscapeScreen = 18;
    }
    if (pHooks != NULL && pHooks->pfnOptionsRebuildWithSound != NULL) {
        pHooks->pfnOptionsRebuildWithSound(pHooks->pUser);
    }
}

/* Ui_CloseScreen18 @ 0x84ecc. Note it also sets the screen id to 18. */
void Ui_CloseScreen18(GeoscapeScreenState *pState, const GeoscapeScreenHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(17, pHooks->pUser);
    }
    if (pState != NULL) {
        pState->nGeoscapeScreen = 18;
    }
    if (pHooks != NULL && pHooks->pfnOptionsOpenWithSound != NULL) {
        pHooks->pfnOptionsOpenWithSound(pState != NULL ? pState->nLineAux119 : 0,
                                        pHooks->pUser);
    }
}

/* Ui_OpenFunding @ 0x85164. */
void Ui_OpenFunding(GeoscapeScreenState *pState, const GeoscapeScreenHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(17, pHooks->pUser);
    }
    if (pState != NULL) {
        pState->nGeoscapeScreen = 15;
    }
    if (pHooks != NULL && pHooks->pfnFundingOpen != NULL) {
        pHooks->pfnFundingOpen(pHooks->pUser);
    }
}

/* ------------------------------------------------- strategic screen list paging */

void BaseFacScreen_PanelPrev(UiPanel *pPanel)
{
    if (pPanel == NULL) {
        return; /* the original would fault on a null unk_15886C */
    }
    UiPanel_ScrollPrev(pPanel); /* 0x589cc is a tail call to 0x3f040 */
}

void BaseFacScreen_PanelNext(UiPanel *pPanel)
{
    if (pPanel == NULL) {
        return;
    }
    UiPanel_ScrollNext(pPanel); /* 0x589dc is a tail call to 0x3f00c */
}

void SoldierScreen_PanelPrev(UiPanel *pFirst, UiPanel *pSecond)
{
    /* 0x61874 / 0x61880: both panels move, apLines[186] first, anLineAux[106] second. */
    if (pFirst != NULL) {
        UiPanel_ScrollPrev(pFirst);
    }
    if (pSecond != NULL) {
        UiPanel_ScrollPrev(pSecond);
    }
}

void SoldierScreen_PanelNext(UiPanel *pFirst, UiPanel *pSecond)
{
    /* 0x618a8 and the returned second call. */
    if (pFirst != NULL) {
        UiPanel_ScrollNext(pFirst);
    }
    if (pSecond != NULL) {
        UiPanel_ScrollNext(pSecond);
    }
}

/* 0x76138 and 0x96ab0 are the same seven instructions as 0x7afec, so both forward to
 * the cache that already owns dword_186BE8. Only the source object differs between the
 * three, and the port takes the +0xBD8 value as an argument. */
i32 BuySellScreen_SyncSelection(ScreenScratch *pScratch, i32 nSel0BD8)
{
    return BaseUi_Cache186BE8(pScratch, nSel0BD8);
}

i32 Transfer_CacheSelIndex(ScreenScratch *pScratch, i32 nSel0BD8)
{
    return BaseUi_Cache186BE8(pScratch, nSel0BD8);
}

i32 ManufScreen_SyncSelection(i32 *pnLineAux147, i32 nSel0BD8)
{
    if (pnLineAux147 != NULL) {
        *pnLineAux147 = nSel0BD8; /* 0x6DD3C */
    }
    return nSel0BD8; /* 0x6DD40 */
}

signed char BaseInfoScreen_OnOk(BaseInfoOkState *pState)
{
    if (pState == NULL) {
        return 0;
    }
    pState->bPsiPending = 1;                        /* 0x557D0, MOV #1 / STRB */
    pState->nCurrentBase = pState->nPendingBase;    /* 0x557DC LDRSB, 0x557E0 STRB */
    return pState->nPendingBase;                    /* 0x557E4 returns R0 */
}

/* 0x716a8 is the same eight instructions as 0x557c8, down to the LDRSB. */
signed char PsiScreen_OnOk(BaseInfoOkState *pState)
{
    return BaseInfoScreen_OnOk(pState);
}

void BuySellScreen_ListPanelPrev(UiPanel *pFirst, UiPanel *pSecond)
{
    if (pFirst != NULL) {
        UiPanel_ScrollPrev(pFirst); /* 0x79A2C, unk_161EDC */
    }
    if (pSecond != NULL) {
        UiPanel_ScrollPrev(pSecond); /* 0x79A38, unk_161A3C */
    }
}

void BuySellScreen_ListPanelNext(UiPanel *pFirst, UiPanel *pSecond)
{
    if (pFirst != NULL) {
        UiPanel_ScrollNext(pFirst); /* 0x79A54 */
    }
    if (pSecond != NULL) {
        UiPanel_ScrollNext(pSecond); /* 0x79A60 */
    }
}

/* The six openers share one body. bIdAfterInit carries the ordering difference the
 * disassembly shows: Manuf (0x614EC), Transfer (0x6151C) and BaseInfo (0x6169C) write the
 * screen id after the Init, BuySell (0x617AC, 0x617DC) and BaseFac (0x6185C) before it. */
static void Geoscape_OpenScreen(GeoscapeScreenState *pState, i32 nScreenId,
                                bool bIdAfterInit, const ScreenInitHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(17, 4, pHooks->pUser); /* MOV R1, #4 / MOV R0, #0x11 */
    }
    if (!bIdAfterInit && pState != NULL) {
        pState->nGeoscapeScreen = nScreenId;
    }
    if (pHooks != NULL && pHooks->pfnInit != NULL) {
        pHooks->pfnInit(pHooks->pSlot, pHooks->pUser);
    }
    if (bIdAfterInit && pState != NULL) {
        pState->nGeoscapeScreen = nScreenId;
    }
}

void ManufScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 9, true, pHooks);
}

void TransferScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 10, true, pHooks);
}

void BaseInfoScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 3, true, pHooks);
}

void BuySellScreen_OpenBuy(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 11, false, pHooks);
}

void BuySellScreen_OpenSell(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 12, false, pHooks);
}

void BaseFacScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 7, false, pHooks);
}

/* BasePlaneScreen_Open @ 0x617F4: sample 17, the id 6 written at 0x61828 BEFORE the Init
 * tail call to BasePlaneScreen_Init (0x7d9e4) with the slot at unk_161A48. Same shape as
 * BuySell and BaseFac, so it reuses the shared opener. */
void BasePlaneScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 6, false, pHooks);
}

void BaseAtScreen_Open(UiControl *pCtl, i32 nMode, const BaseAtScreenOpenHooks *pHooks)
{
    if (pCtl != NULL) {
        pCtl->x = (i16)(120 - (pCtl->uWidth >> 1)); /* 0x588E4: LDRH, LSR #1, RSB #0x78 */
        pCtl->y = (i16)(0x120 | 2);                 /* 0x588F4: 290 built as ORR #2 */
        if (pHooks != NULL) {
            pCtl->pfnClick = pHooks->pfnClick;      /* 0x58900: STR [R1,#0xC] */
        }
    }
    if (pHooks != NULL && pHooks->pfnUpdate != NULL) {
        pHooks->pfnUpdate(nMode); /* 0x58908, tail call. nMode is R1; R0 was overwritten. */
    }
}

/* Seven thunks, one tail call: LDR R0, =0x1864F8 / B Geoscape_CloseUI. */
static void Geoscape_CloseScreenThunk(const GeoscapeCloseHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnCloseUi != NULL) {
        pHooks->pfnCloseUi(pHooks->pUser);
    }
}

void BaseInfoScreen_CloseThunk(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void BaseFacScreen_CloseThunkA(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void BaseFacScreen_CloseThunk(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void SoldierScreen_CloseThunk(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void ManufScreen_CloseThunk(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void ResearchScreen_CloseThunk(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void BuySellScreen_CloseThunk(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void NewBaseScreen_CloseThunk(const GeoscapeCloseHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(17, pHooks->pUser); /* 0x615BC */
    }
    Geoscape_CloseScreenThunk(pHooks); /* 0x615C8 */
}

i32 OptionsScreen_ReturnToTitle(ScreenScratch *pScratch, const TitleReturnHooks *pHooks)
{
    if (pScratch != NULL) {
        pScratch->nLineAux145 = 0; /* 0x6B478 */
    }
    if (pHooks != NULL && pHooks->pfnCloseUi != NULL) {
        pHooks->pfnCloseUi(pHooks->pUser); /* 0x6B480 */
    }
    if (pHooks != NULL && pHooks->pfnFreeGeoscapeUi != NULL) {
        pHooks->pfnFreeGeoscapeUi(1, pHooks->pUser); /* 0x6B488 */
    }
    if (pHooks != NULL && pHooks->pfnStartupUi != NULL) {
        return pHooks->pfnStartupUi(pHooks->pUser); /* 0x6B490 tail call */
    }
    return 0;
}

/* Five more copies of the same two instructions. */
void FundingScreen_CloseThunk(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void FundingScreen_ExitThunk(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void Geoscape_CloseUI_ThunkA(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void Geoscape_CloseUI_ThunkB(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

void Geoscape_CloseUI_ThunkC(const GeoscapeCloseHooks *pHooks)
{
    Geoscape_CloseScreenThunk(pHooks);
}

i32 Geoscape_OnMapClick_Thunk(const MapClickHooks *pHooks)
{
    /* 0x857EC tail-calls Geoscape_OnMapClick (0x8b4a8) and drops the four callback
     * arguments the widget framework passes. */
    if (pHooks != NULL && pHooks->pfnMapClick != NULL) {
        return pHooks->pfnMapClick(pHooks->pUser);
    }
    return 0;
}

void ResearchScreen_PanelPrev(UiPanel *pFirst, UiPanel *pSecond, UiPanel *pThird)
{
    if (pFirst != NULL) {
        UiPanel_ScrollPrev(pFirst); /* 0x72E3C, unk_160C68 */
    }
    if (pSecond != NULL) {
        UiPanel_ScrollPrev(pSecond); /* 0x72E48, unk_15EEFC */
    }
    if (pThird != NULL) {
        UiPanel_ScrollPrev(pThird); /* 0x72E54, unk_15EF00 */
    }
}

void ResearchScreen_PanelNext(UiPanel *pFirst, UiPanel *pSecond, UiPanel *pThird)
{
    if (pFirst != NULL) {
        UiPanel_ScrollNext(pFirst); /* 0x72E74 */
    }
    if (pSecond != NULL) {
        UiPanel_ScrollNext(pSecond); /* 0x72E80 */
    }
    if (pThird != NULL) {
        UiPanel_ScrollNext(pThird); /* 0x72E8C */
    }
}

/* Research_TopicNeedsItem @ 0x72c8c. The gate is two ARM bands, not the boolean the
 * pseudocode reconstructed: 0x72C9C branches on <=54 into the 117/143 test, so a topic at or
 * below 54 answers 0; 55..65 fall through to the body; 66..117 reach the same test and answer
 * 0; 118..142 reach the body; >=143 answers 0. 135 and 140 are excluded first, at 0x72C8C and
 * 0x72C94. The body reads the item id from the in-memory record at +14 and answers whether the
 * object's per-base count at the selected base is above zero (LDRH/CMP/BHI at 0x72D00). */
i32 Research_TopicNeedsItem(const ResearchTopicLive *aTopics,
                            ObjectRecFull *const *apObjects, i32 nTopic, i8 nBaseIdx)
{
    if (nTopic == 135 || nTopic == 140) { /* 0x72C8C, 0x72C94 */
        return 0;
    }
    if (!(nTopic > 54 && nTopic < 66) && !(nTopic > 117 && nTopic < 143)) {
        return 0; /* 0x72C9C..0x72CB8 */
    }
    const ObjectRecFull *pItem = apObjects[aTopics[nTopic].nItemId]; /* 0x72CD4, 0x72CEC */
    return pItem->auPerBaseVal[nBaseIdx] != 0 ? 1 : 0; /* 0x72CF4..0x72D04 */
}

/* ResearchScreen_RefreshPanels @ 0x73008. Both passes walk topics 1..count-1 (the walk starts
 * at 0x15EF2A, one record past the table base), and only bActive topics take a line. The
 * quantity entry index is the topic number itself: the ARM composes 0x15EF08 + 4900 + 288*base
 * + 2*topic (MUL #0x90, ADD topic, LSL #1 at 0x730C0..0x730E4) — the "+4" Hex-Rays showed is
 * the 0xBE4 apLines offset leaking through register R4, not a column header. */
i32 ResearchScreen_RefreshPanels(const ResearchTopicLive *aTopics, i32 nTopicCount,
                                 const u16 *apResearchQty, UiPanel *pPanelCount,
                                 UiPanel *pPanelQty, UiPanel *pPanelProgress)
{
    i32 nActive = 0; /* 0x73014 */
    if (nTopicCount > 1) { /* 0x73028 */
        for (i32 nTopic = 1; nTopic < nTopicCount; ++nTopic) {
            if (aTopics[nTopic].bActive == 1) { /* 0x73034, 0x73044 */
                ++nActive;
            }
        }
    }

    if (pPanelCount != NULL)    { pPanelCount->nLineCount    = nActive; } /* 0x73070 */
    if (pPanelQty != NULL)      { pPanelQty->nLineCount      = nActive; } /* 0x7307C */
    if (pPanelProgress != NULL) { pPanelProgress->nLineCount = nActive; } /* 0x73084 */

    if (nTopicCount > 1) { /* 0x7309C */
        i32 nLine = 0; /* R5 counts line bytes, /4 here */
        for (i32 nTopic = 1; nTopic < nTopicCount; ++nTopic) { /* 0x73228..0x73234 */
            if (aTopics[nTopic].bActive != 1) { /* 0x730B8 */
                continue;
            }
            char szNum[16];
            const u16 wQty = apResearchQty[nTopic]; /* 0x730E8 */
            if (wQty != 0) { /* 0x730EC */
                (void)snprintf(szNum, sizeof(szNum), "%u", (unsigned)wQty); /* j_itoa, 0x730F8 */
            } else {
                szNum[0] = '\0'; /* the empty string at 0x10DA38, 0x7313C */
            }
            pufo_strcpy((char *)pPanelQty->apLines[nLine], szNum); /* the LDRSB/STRB copy at 0x73108 */

            if (aTopics[nTopic].wProgress != 0 && aTopics[nTopic].wTotal != 0) { /* 0x73160 */
                const i32 nPct = (100 * (i32)aTopics[nTopic].wProgress) / (i32)aTopics[nTopic].wTotal;
                (void)snprintf(szNum, sizeof(szNum), "%d", nPct); /* rt_sdiv 0x73184, itoa 0x73198 */
                pufo_strcpy((char *)pPanelProgress->apLines[nLine], szNum);
                (void)pufo_strcat((char *)pPanelProgress->apLines[nLine], "%",
                                  PUFO_PANEL_LINE_BYTES); /* 0x731D0 */
            } else {
                *(char *)pPanelProgress->apLines[nLine] = '\0'; /* 0x731EC: the empty string */
            }
            ++nLine; /* 0x7321C */
        }
    }
    return nTopicCount; /* 0x73218, 0x73238 */
}

/* Byte +10 of a UiControl is nState, the selected/radio byte. The original returns the
 * sixth pointer, loaded at 0x6B9C4 / 0x6BA04 before its final store. */
static UiControl *OptionsScreen_ClearRadioGroup(UiControl *apCtl[6])
{
    for (i32 i = 0; i < 6; ++i) {
        if (apCtl[i] != NULL) {
            apCtl[i]->nState = 0;
        }
    }
    return apCtl[5];
}

UiControl *OptionsScreen_ClearMusicButtons(UiControl *apMusic[6])
{
    return OptionsScreen_ClearRadioGroup(apMusic); /* 0x6b994, anLineAux[128..133] */
}

UiControl *OptionsScreen_ClearSfxButtons(UiControl *apSfx[6])
{
    return OptionsScreen_ClearRadioGroup(apSfx); /* 0x6b9d4, anLineAux[134..139] */
}

/* The original can pass StrategList_Clear straight to atexit because its strategic list is
 * a global and its clear takes no arguments. The port's takes the list, so it registers a
 * thunk over the list handed in here. */
static PtrArray *g_pStrategListToClear;

static void StrategList_ClearAtexitThunk(void)
{
    if (g_pStrategListToClear != NULL) {
        StrategList_Clear(g_pStrategListToClear);
    }
}

int StrategList_RegisterAtexit(PtrArray *pList)
{
    g_pStrategListToClear = pList;
    return atexit(StrategList_ClearAtexitThunk); /* 0x838A0 */
}

void OptionsScreen_RebuildWithSound(const OptionsScreenHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(7, 4, pHooks->pUser); /* 0x6CF08: MOV R1, #4 */
    }
    if (pHooks != NULL && pHooks->pfnInit != NULL) {
        pHooks->pfnInit(pHooks->pUser); /* 0x6CF14, tail call */
    }
}

void OptionsScreen_OpenWithSound(i32 nIgnored, i32 nMode, const OptionsScreenHooks *pHooks)
{
    (void)nIgnored; /* 0x6DD08 saves R1 into R4 and never touches R0 */
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(7, 4, pHooks->pUser); /* 0x6DD10 */
    }
    if (pHooks != NULL && pHooks->pfnOpen != NULL) {
        pHooks->pfnOpen(nMode, pHooks->pUser); /* 0x6DD20, tail call with R4 */
    }
}

/* Funding_ScoreToBarLevel @ 0x5aa60. How much money one funding bar step is worth.
 *
 * The first argument is not an amount and not an index into a table of five: the mapping is
 * 0 -> 3, 1 -> 5, 2 -> 4, 3 -> 1, 4 -> 3, and anything else gives 0. Two scores share 3.
 *
 * The second is a direction, not a magnitude: -2 and -1 mean down, 1 and 2 mean up, and 0 or
 * anything outside -2..2 returns 0 outright -- the level lookup is discarded on that path.
 *
 * Hex-Rays renders the level as `v2 << 24 >> 24`, which is the compiler widening a signed char.
 * Every level is 1..5, so the port keeps a plain int multiply. */
i32 Funding_ScoreToBarLevel(i32 nScore, i32 nDirection)
{
    static const i32 anLevelByScore[5] = { 3, 5, 4, 1, 3 }; /* 0x5AA6C..0x5AAE4 */
    i32 nAmount;

    const i32 nLevel = (nScore >= 0 && nScore <= 4) ? anLevelByScore[nScore] : 0;

    switch (nDirection) {
    case -2: nAmount = -50000; break;
    case -1: nAmount = -25000; break;
    case  1: nAmount =  25000; break;
    case  2: nAmount =  50000; break;
    default: return 0; /* 0x5AAD0, 0x5AAC8: the level is thrown away */
    }

    return nLevel * nAmount; /* 0x5AB2C */
}

/* OptionsScreen_PanelPrev @ 0x6d01c and OptionsScreen_PanelNext @ 0x6d064. The options screen has
 * four scrolling lists and its prev/next buttons move ALL FOUR cursors one row at once -- they are
 * not per-panel controls. Both handlers touch the same four panels in the same order, and each one
 * is the plain scroll step: prev is `if (cursor > 0) --cursor`, next is
 * `if (cursor < lineCount - style) ++cursor`.
 *
 * The original reaches the panels through gUiRootPanel.anLineAux[117], [141], [142] and [118], which
 * is a dword array doubling as a pointer array. The port takes them as an explicit four-element
 * array so the coupling is visible: a caller that passes them in the wrong order scrolls the wrong
 * lists, and nothing in the original would tell you. */
void OptionsScreen_PanelPrev(UiPanel *const apPanels[4])
{
    for (int i = 0; i < 4; ++i) {
        if (apPanels[i] != NULL) {
            UiPanel_ScrollPrev(apPanels[i]); /* 0x6D028, 0x6D034, 0x6D040, 0x6D04C */
        }
    }
}

void OptionsScreen_PanelNext(UiPanel *const apPanels[4])
{
    for (int i = 0; i < 4; ++i) {
        if (apPanels[i] != NULL) {
            UiPanel_ScrollNext(apPanels[i]); /* 0x6D070, 0x6D07C, 0x6D088, 0x6D094 */
        }
    }
}

/* Ufopaedia_CacheSelRow @ 0x99fd0. A one-shot cache, and the one-shot part is the behaviour.
 *
 * While the flag is zero it copies the ufopaedia list's current selected row into the cache and
 * raises the flag. Once the flag is raised it does nothing at all -- the cached row is never
 * refreshed by this path. So whatever row was current the first time this ran is the row the rest
 * of the ufopaedia code sees, until some other path clears the flag.
 *
 * The original returns the address of whichever global it touched: the cache when it cached, the
 * flag when it did not. The port returns whether it cached, which is the same information without
 * handing out addresses. */
bool Ufopaedia_CacheSelRow(u8 *pbFlag, i32 *pnCachedRow, i32 nPanelSelectedRow)
{
    if (pbFlag == NULL || pnCachedRow == NULL) {
        return false;
    }

    if (*pbFlag != 0) {
        return false; /* 0x99FD4: the flag is already up, nothing happens */
    }

    *pbFlag = 1;                              /* 0x99FE4 */
    *pnCachedRow = nPanelSelectedRow;         /* 0x9A000: dword_187204->nUnk3032 */
    return true;
}

/* Funding_DeltaToArrow @ 0x5ab34. Which arrow to draw next to a funding figure: -2, -1, 0, 1, 2.
 *
 * It is a chain of compares, not a switch, and the thresholds come from the total: a2/8 and a2/2,
 * with integer division truncating toward zero. That makes the bands asymmetric for a negative
 * total, which is the point -- a base in the red gets a different arrow for the same delta.
 *
 * The third test reads `a1 <= a2 / -8`, not `a2 / 8`. Dividing by -8 is what puts the middle band
 * on the other side of zero. */
i32 Funding_DeltaToArrow(i32 nDelta, i32 nTotal)
{
    if (nDelta <= nTotal / 8 + nTotal / 2) {
        return -2; /* 0x5AB54 */
    }
    if (nDelta <= nTotal / 8) {
        return -1; /* 0x5AB60 */
    }
    if (nDelta <= nTotal / -8) {
        return 0; /* 0x5AB7C */
    }
    if (nDelta > nTotal / -8 - nTotal / 2) {
        return 2; /* 0x5ABAC */
    }
    return 1; /* 0x5ABD4 */
}

/* SoldierScreen_FindNextAtBase @ 0x6192c. See the header for the evidence. */
i32 SoldierScreen_FindNextAtBase(i32 *pnCursor, Soldier *const *apSoldiers, u16 nSoldierCount,
                                 i8 nSelectedBaseIdx)
{
    i32 nIdx = *pnCursor + 1;                    /* 0x61948 ADD R0, #1 */
    if (nIdx >= (i32)nSoldierCount) {            /* 0x61950 CMP/BGE, LDRH count at 0x61940 */
        return nIdx;                             /* cursor untouched */
    }
    while ((i32)apSoldiers[nIdx]->nKind != (i32)nSelectedBaseIdx) { /* 0x61968..0x6198C */
        nIdx += 1;                               /* 0x61978/0x6197C */
        if (nIdx >= (i32)nSoldierCount) {        /* 0x6198C */
            return nIdx;                         /* == the count, cursor untouched */
        }
    }
    *pnCursor = nIdx;                            /* 0x619A0 */
    return nIdx;
}

/* SoldierScreen_FindPrevAtBase @ 0x618c4: the same walk downward. It never reads the soldier
 * count (0x618C4..0x61928 has no LDRH) — the walk is bounded by zero, not by the count. */
i32 SoldierScreen_FindPrevAtBase(i32 *pnCursor, Soldier *const *apSoldiers, i8 nSelectedBaseIdx)
{
    i32 nIdx = *pnCursor - 1;                    /* 0x618D0 SUBS R0, #1 */
    if (nIdx < 0) {                              /* 0x618D4 BMI */
        return nIdx;                             /* cursor untouched */
    }
    while ((i32)apSoldiers[nIdx]->nKind != (i32)nSelectedBaseIdx) { /* 0x618F0..0x61910 */
        nIdx -= 1;                               /* 0x61900/0x61904 */
        if (nIdx < 0) {                          /* 0x61910 BPL falls out */
            return nIdx;                         /* == -1, cursor untouched */
        }
    }
    *pnCursor = nIdx;                            /* 0x61924 */
    return nIdx;
}

/* Ui_SetTransferFlag @ 0x619a8. */
void Ui_SetTransferFlag(u8 *pbTransferFlag, void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser),
                        void *pUser)
{
    if (pfnPlaySample != NULL) {
        pfnPlaySample(17, 4, pUser); /* 0x619B4, MOV R1, #4 */
    }
    if (pbTransferFlag != NULL) {
        *pbTransferFlag = 1;         /* 0x619C0 STRB */
    }
}

/* aNewBaseCostByRace @ 0xAB488, ten dwords, indexed by (byte_159DF4 - 128). */
const i32 g_anNewBaseCostByKind[PUFO_NEW_BASE_COST_COUNT] = {
    800000, 600000, 1000000, 800000, 750000, 650000, 900000, 950000, 800000, 0
};

/* NewBaseScreen_Open @ 0x615d0. */
void NewBaseScreen_Open(GeoscapeScreenState *pState, const NewBaseOpenEnv *pEnv)
{
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, 4, pEnv->pUser); /* 0x615DC */
    }
    if (*pEnv->pbBaseCount == 8) {               /* 0x615F0 LDRSB, 0x615F4 CMP #8 */
        return;
    }
    if (pEnv->pCtlFrame != NULL) {
        pEnv->pCtlFrame->x = 224;                /* 0x61608 STRH #0xE0 */
        pEnv->pCtlFrame->y = 1;                  /* 0x61614 STRH #1 */
    }
    pState->nGeoscapeScreen = 2;                 /* 0x61620, the screen id at 0x186BC4 */
    if (pEnv->pCtlConfirm != NULL) {
        pEnv->pCtlConfirm->pfnClick = pEnv->pfnTryConfirm; /* 0x6162C */
    }
    (void)Smap_ZoomOut(pEnv->pView, pEnv->pZoomSets, pEnv->pWorldOut); /* 0x61634 */
    (void)Smap_ZoomOut(pEnv->pView, pEnv->pZoomSets, pEnv->pWorldOut); /* 0x6163C */
    *pEnv->pbFlag187080 = 0;                     /* 0x6164C STRB */
    if (pEnv->pCtlFrame != NULL) {
        pEnv->pCtlFrame->pfnClick = pEnv->pfnCloseThunk; /* 0x61654 */
    }
}

/* NewBaseScreen_TryConfirm @ 0x6152c. */
i32 NewBaseScreen_TryConfirm(u8 bPlacementPending, const NewBaseConfirmEnv *pEnv)
{
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, 4, pEnv->pUser); /* 0x61538 */
    }

    if (bPlacementPending == 0) {                /* 0x6153C LDRB 0x1870AC, ANDS/BNE */
        i32 nKind = (i32)pEnv->bPendingKind - 128;
        i32 nCost = 0; /* deviation: the original reads the table unguarded (0x61554..0x61568);
                        * an out-of-range kind reads adjacent image bytes, which are zero for
                        * the 0xFF sentinel the pointer handlers use. */
        if (nKind >= 0 && nKind < PUFO_NEW_BASE_COST_COUNT) {
            nCost = g_anNewBaseCostByKind[nKind];
        }
        if (*pEnv->pnFunds - nCost < 0) {        /* 0x61570 SUBS, MI */
            *pEnv->pbFlag1870C8 = 1;             /* 0x61574 STRMI (dword in the original, byte
                                                  * everywhere it is read; see the header) */
            *pEnv->pnFlag1870B4 = 0;             /* 0x61580 */
            return 0;                            /* 0x61590 returns dword_1870B4, just cleared */
        }
    }

    return pEnv->pfnMapClick(pEnv->pUser);       /* 0x61588 LDR R0,=0x1864F8; BL Geoscape_OnMapClick */
}

/* stru_AB4B0 @ 0xAB4B0: the ten nav-button descriptors. All ten carry pads "emptyb"/"cemptyb"
 * with no third pad and mode 0; only the offsets differ. */
const UiControlDesc g_aGeodescNav[PUFO_NAV_CTLS] = {
    {   1, 229, "emptyb", "cemptyb", "", 0 },
    {   1, 247, "emptyb", "cemptyb", "", 0 },
    {   1, 265, "emptyb", "cemptyb", "", 0 },
    {   1, 283, "emptyb", "cemptyb", "", 0 },
    {   1, 301, "emptyb", "cemptyb", "", 0 },
    { 120, 229, "emptyb", "cemptyb", "", 0 },
    { 120, 247, "emptyb", "cemptyb", "", 0 },
    { 120, 265, "emptyb", "cemptyb", "", 0 },
    { 120, 283, "emptyb", "cemptyb", "", 0 },
    { 120, 301, "emptyb", "cemptyb", "", 0 },
};

/* stru_AB5F0 @ 0xAB5F0: the soldier-list page buttons. */
const UiControlDesc g_aGeodescPanel[PUFO_NAV_PANEL_CTLS] = {
    { 210, 20, "up",   "sup",   "", 0 },
    { 210, 40, "down", "sdown", "", 0 },
};

/* stru_AB630 @ 0xAB630: the soldier-find buttons. */
const UiControlDesc g_aGeodescFind[PUFO_NAV_FIND_CTLS] = {
    { 35, 37, "bnexts",  "bnext",  "", 0 },
    {  3, 37, "bprevs",  "bprev",  "", 0 },
    { 70, 37, "emptyb1", "cemptyb1", "", 0 },
};

/* operator new(0x20) + UiControl_ctor, as the three creation loops do (0x67B30, 0x67C18, 0x67CC8). */
static UiControl *NavCtl_New(void)
{
    UiControl *pCtl = (UiControl *)malloc(sizeof(UiControl));
    if (pCtl != NULL) {
        UiControl_ctor(pCtl);
    }
    return pCtl;
}

/* Geoscape_BuildNavControls @ 0x67b1c. See the header for the evidence and the deviations. */
bool Geoscape_BuildNavControls(NavControls *pOut, const NavCtlEnv *pEnv)
{
    i32 i;
    bool bOk = true;

    for (i = 0; i < PUFO_NAV_CTLS; ++i) {           /* 0x67B2C..0x67B5C */
        pOut->apNav[i] = NavCtl_New();
        if (pOut->apNav[i] == NULL) {
            bOk = false;
        }
    }
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {           /* 0x67B74..0x67BE0 */
        if (pOut->apNav[i] != NULL) {
            pOut->apNav[i]->pfnClick = pEnv->apfnNavClick[i];
        }
    }
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {           /* 0x67BF4, UiControl_Setup(ctl, 0, 0, desc) */
        if (pOut->apNav[i] != NULL) {
            UiControl_Setup(pOut->apNav[i], 0, 0, &g_aGeodescNav[i],
                            pEnv->pfnLoadPads, pEnv->pLoadPadsUser);
        }
    }

    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {     /* 0x67C10..0x67C60 */
        pOut->apPanel[i] = NavCtl_New();
        if (pOut->apPanel[i] == NULL) {
            bOk = false;
        }
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {     /* 0x67C84/0x67C90 */
        if (pOut->apPanel[i] != NULL) {
            pOut->apPanel[i]->pfnClick = pEnv->apfnPanelClick[i];
        }
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {     /* 0x67CA4 */
        if (pOut->apPanel[i] != NULL) {
            UiControl_Setup(pOut->apPanel[i], 0, 0, &g_aGeodescPanel[i],
                            pEnv->pfnLoadPads, pEnv->pLoadPadsUser);
        }
    }

    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {      /* 0x67CC0..0x67CF0 */
        pOut->apFind[i] = NavCtl_New();
        if (pOut->apFind[i] == NULL) {
            bOk = false;
        }
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {      /* 0x67D10..0x67D28 */
        if (pOut->apFind[i] != NULL) {
            pOut->apFind[i]->pfnClick = pEnv->apfnFindClick[i];
        }
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {      /* 0x67D3C */
        if (pOut->apFind[i] != NULL) {
            UiControl_Setup(pOut->apFind[i], 0, 0, &g_aGeodescFind[i],
                            pEnv->pfnLoadPads, pEnv->pLoadPadsUser);
        }
    }

    return bOk;
}

/* Geoscape_OnRadarListClick @ 0x854c4. See the header for the six branches and the deviations. */
i32 Geoscape_OnRadarListClick(const RadarClickEnv *pEnv)
{
    StrategList view;
    StrategItem *pItem;
    i32 nRowIdx;
    i32 nItemIdx;
    i32 nShipIdx;

    if (pEnv == NULL || pEnv->pLines == NULL || pEnv->pStatusPanel == NULL ||
        pEnv->pStrategList == NULL || pEnv->pnSelectedStrategIdx == NULL ||
        pEnv->pTime == NULL) {
        return 0;
    }

    nRowIdx = pEnv->pStatusPanel->nUnk3032;
    nItemIdx = pEnv->pStatusPanel->anLineAux[nRowIdx]; /* 0x85500: the aux column is the list index */

    *pEnv->pbFlag1870A0 = 0;                            /* 0x85504, byte_18709C[4] */
    *pEnv->pnSelectedStrategIdx = nItemIdx;             /* 0x8550C, the dword at 0x186BE4 */
    pEnv->pTime->bPaused = 0;                           /* 0x85510: unpause before the dispatch */

    pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, nItemIdx);
    if (pItem == NULL) {
        return 0; /* deviation: the original dereferences whatever PtrArray_Get hands back */
    }

    if (pItem->nType == 1 && pItem->bActive == 1) {     /* 0x85538 */
        pEnv->pTime->bPaused = 1;                       /* GameTime_Pause @ 0x5d6ac */
        UiLineList_Remove(pEnv->pLines, 0);             /* 0x8554C */
        UiLineList_Insert(pEnv->pLines, 0, 10, (u32)nItemIdx, 0u, 0u, 0u); /* 0x8556C */
    }

    if (pItem->nType == 0 && pItem->bActive == 0) {     /* 0x8559C */
        pEnv->pTime->bPaused = 1;
        *pEnv->pnScreenId = 18;                         /* 0x855B0 */
        OptionsScreen_OpenWithSound(0, pItem->nSiteId, pEnv->pOptionsHooks); /* 0x855C8 */
    }

    if (pItem->nType == 1 && pItem->bActive == 0) {     /* 0x855FC */
        pEnv->pTime->bPaused = 1;
        view.apItems = (StrategItem **)pEnv->pStrategList->apItems;
        view.nCount = pEnv->pStrategList->nCount;
        view.nNextDisplayId = 0; /* not read by the lookup */
        nShipIdx = Ship_FindIndexByStrategType(pEnv->pWorld, &view, nItemIdx); /* 0x85624 */
        *pEnv->pnSelectedShipIdx = nShipIdx;            /* 0x187054, then 0x159E74 at 0x85630 */
        *pEnv->pnStrategIndex = nShipIdx;
        UiLineList_AppendWithSound(pEnv->pLines, 11, (u32)nShipIdx, 0u, 0u, 0u,
                                   pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x8563C */
        *pEnv->pnRadarPulseTimer = 250;                 /* 0x85644 */
    }

    if (pItem->nType == 4 && pItem->bActive == 1) {     /* 0x85674 */
        pEnv->pTime->bPaused = 1;
        *pEnv->pbFlag18708C = 1;                        /* 0x85688 */
        UiLineList_Remove(pEnv->pLines, 0);             /* 0x85694 */
        UiLineList_AppendWithSound(pEnv->pLines, 20, (u32)nRowIdx, (u32)nItemIdx, 0u, 0u,
                                   pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x856B4 */
    }

    if (pItem->nType == 3 && pItem->bActive == 1) {     /* 0x856E4 */
        pEnv->pTime->bPaused = 1;                       /* 0x856EC, a direct store here */
        *pEnv->pbFlag187090 = 1;                        /* 0x856F8 */
        UiLineList_Remove(pEnv->pLines, 0);             /* 0x85704 */
        UiLineList_AppendWithSound(pEnv->pLines, 19, (u32)nRowIdx, (u32)nItemIdx, 0u, 0u,
                                   pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x85724 */
        *pEnv->pnRadarPulseTimer = 250;                 /* 0x8572C */
    }

    if (pItem->nType == 0 && pItem->bActive == 1) {     /* 0x85744, the tail case */
        pEnv->pTime->bPaused = 1;                       /* 0x85764 */
        *pEnv->pbFlag187098 = 1;                        /* 0x85770 */
        UiLineList_Remove(pEnv->pLines, 0);             /* 0x8577C */
        UiLineList_AppendWithSound(pEnv->pLines, 18, (u32)nRowIdx, (u32)nItemIdx, 0u, 0u,
                                   pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x8579C */
        *pEnv->pnRadarPulseTimer = 250;                 /* 0x857A4 */
        return 250;
    }

    return 0; /* the original hands back the item pointer here; nothing reads it */
}

/* stru_AB220 @ 0xAB220: the two build-dialog panel buttons BaseFacScreen_Init positions. Printed
 * and checked by tools/gen_geoiinit_tables.py. */
static const UiControlDesc g_aBaseFacPanelDesc[PUFO_BASEFAC_PANEL_DESC_COUNT] = {
    { 11, 252, "up", "sup", "", 0 },
    { 11, 277, "down", "sdown", "", 0 },
};

/* BaseFacScreen_Init @ 0x5a198. See the header for the five steps and the deviations. */
i32 BaseFacScreen_Init(const BaseFacInitEnv *pEnv)
{
    UiControl *apCtl[PUFO_BASEFAC_PANEL_DESC_COUNT];
    i32 i;

    if (pEnv == NULL) {
        return 0;
    }
    apCtl[0] = pEnv->pUpCtl;
    apCtl[1] = pEnv->pDownCtl;

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseThunk; /* 0x5A1C4 */
    }
    if (pEnv->pnUnk15887C != NULL) {
        *pEnv->pnUnk15887C = 0;                          /* 0x5A1AC */
    }
    if (pEnv->pnUnk158880 != NULL) {
        *pEnv->pnUnk158880 = 0;                          /* 0x5A1B4 */
    }

    /* 0x5A1CC..0x5A200: operator new(0x20) then UiControl_ctor, stored into 0x158874 and 0x158878.
     * The port's controls are caller-owned, but the ctor still runs, as the original runs it. */
    for (i = 0; i < PUFO_BASEFAC_PANEL_DESC_COUNT; ++i) {
        if (apCtl[i] != NULL) {
            UiControl_ctor(apCtl[i]);
        }
    }

    if (apCtl[0] != NULL) {
        apCtl[0]->pfnClick = pEnv->pfnPanelPrev;         /* 0x5A214: STR [R0,#0xC] */
    }
    if (apCtl[1] != NULL) {
        apCtl[1]->pfnClick = pEnv->pfnPanelNext;         /* 0x5A220 */
    }

    for (i = 0; i < PUFO_BASEFAC_PANEL_DESC_COUNT; ++i) { /* 0x5A224..0x5A244 */
        if (apCtl[i] != NULL) {
            UiControl_Setup(apCtl[i], 0, 0, &g_aBaseFacPanelDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseThunk; /* 0x5A254, the same handler again */
        pEnv->pCloseCtl->x = 224;                        /* 0x5A260 */
        pEnv->pCloseCtl->y = 229;                        /* 0x5A26C */
    }
    if (pEnv->pbArmed != NULL) {
        *pEnv->pbArmed = 0;                              /* 0x5A274 */
    }

    if (pEnv->pfnToggle != NULL) {
        pEnv->pfnToggle(pEnv->pToggleUser);              /* 0x5A27C, the tail call */
    }
    return 0; /* the original returns BaseFacScreen_Toggle's register; the hook returns nothing */
}

/* Geoscape_OnMapClick @ 0x8b4a8. The header carries the two branches and the loop arithmetic.
 *
 * The port returns a status instead of the original's residue:
 *   0  nothing ran (any other screen id, an inactive hangar placement, or one that was applied)
 *   1  the hangar placement was refused and bBasePlaceFail was set
 *   2  the pending facility was committed to base 0
 *   3  the base-facility screen was opened
 * The original returns 6, &bBasePlaceFail (0x1870B8), &bBaseFacDone (0x187078) and
 * BaseFacScreen_Init's value in those four cases; nothing reads any of them - the function is
 * installed as a widget click handler and NewBaseScreen_TryConfirm tail-calls it. */
i32 Geoscape_OnMapClick(const MapClickEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pnScreenId == NULL || pEnv->abFacilityGrid == NULL) {
        return 0;
    }

    if (*pEnv->pnScreenId == 1) {
        i32 nSel, nFac, nDays, nTile, i;
        u8 *abGrid = pEnv->abFacilityGrid;

        if (pEnv->pbPlaceActive == NULL || *pEnv->pbPlaceActive == 0) {
            return 0;                                   /* 0x8B664: the gate is the active byte */
        }
        if (pEnv->pHangar == NULL) {
            return 0;
        }
        if (Base_CanPlaceHangarAt(pEnv->pHangar, pEnv->nTapX, pEnv->nTapY) == 0) {
            if (pEnv->pbPlaceFail != NULL) {
                *pEnv->pbPlaceFail = 1;                 /* 0x8B93C */
            }
            return 1;
        }

        nSel  = (i32)pEnv->pHangar->nSelectedBaseIdx;
        nFac  = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nSel + 6 * pEnv->nTapY + pEnv->nTapX;
        nDays = PUFO_GRID_DAYS_STRIDE * nSel + 6 * pEnv->nTapY + pEnv->nTapX;
        nTile = abGrid[nFac + PUFO_GRID_TILEA_OFF];

        if (nTile <= 8 || nTile >= 13) {
            /* 0x8B88C..0x8B92C: not a hangar quadrant, so only the tapped cell goes. */
            abGrid[nDays]                    = 0;
            abGrid[nFac + PUFO_GRID_TILEA_OFF]  = 0;
            abGrid[nFac + PUFO_GRID_TILEB_OFF]  = 0;
            abGrid[nFac + PUFO_GRID_LAYER2_OFF] = 0;
            abGrid[nFac + PUFO_GRID_FAC_TYPE_OFF] = PUFO_FAC_TYPE_NONE;
        } else {
            /* 0x8B6FC..0x8B7B8: the three-iteration loop. Its index arithmetic is worth spelling
             * out because Hex-Rays scrambles it: the four stores per iteration land at
             * 144*nBase + 36*i + 6*nY + nX + {0, 1, 6, 7}, so i walks the three 36-byte layers
             * tileA, tileB and layer2 and {0,1,6,7} is the 2 by 2 footprint inside each. */
            for (i = 0; i < 3; ++i) {
                const i32 nLayer = nFac + 36 * i;
                abGrid[nLayer]     = 0;                 /* 0x8B728 */
                abGrid[nLayer + 1] = 0;                 /* 0x8B754 */
                abGrid[nLayer + 6] = 0;                 /* 0x8B780 */
                abGrid[nLayer + 7] = 0;                 /* 0x8B7B4 */
            }
            abGrid[nFac + PUFO_GRID_FAC_TYPE_OFF] = PUFO_FAC_TYPE_NONE; /* 0x8B7E4 */
            abGrid[nDays]     = 0;                      /* 0x8B804 */
            abGrid[nDays + 6] = 0;                      /* 0x8B828: (nY+1,nX) */
            abGrid[nDays + 1] = 0;                      /* 0x8B84C: (nY,nX+1) */
            abGrid[nDays + 7] = 0;                      /* 0x8B870: the diagonal */
        }
        return 0;
    }

    if (*pEnv->pnScreenId == 2) {
        BaseRec *pBase;
        i32 i, nCount;

        if (pEnv->bPendingKind == 0xFF) {
            return 0;                                   /* 0x8B4D0 */
        }

        if (pEnv->pbConfirm != NULL && *pEnv->pbConfirm != 0) {
            pBase = (pEnv->apBases != NULL) ? pEnv->apBases[0] : NULL;

            if (pBase != NULL) {
                pBase->nUnk8  = pEnv->nPlaceX;          /* 0x8B504 */
                pBase->nUnk12 = pEnv->nPlaceY;          /* 0x8B518 */
                pBase->bUnk17 = pEnv->bPendingKind;     /* 0x8B52C */
                pBase->bUnk16 = 1;                      /* 0x8B53C */
            }
            if (pEnv->apSiteRecs != NULL) {
                pEnv->apSiteRecs[0].bType = (u8)(pEnv->bPendingKind + 0x80); /* 0x8B548 */
            }

            /* 0x8B550..0x8B56C: the label is built in the shared scratch string and then copied
             * into the facility record's name field (0x186C5A, record + 26). */
            if (pEnv->pszLabelScratch != NULL && pEnv->apSiteRecs != NULL) {
                (void)BaseFac_FormatCostLabel(pEnv->pszLabelScratch, (size_t)pEnv->cchLabelScratch,
                                              pEnv->pTextRecs, pEnv->bPendingKind,
                                              pEnv->abKindTable);
                pufo_strncpy(pEnv->apSiteRecs[0].abName, pEnv->pszLabelScratch,
                             sizeof(pEnv->apSiteRecs[0].abName));
            }

            /* 0x8B570..0x8B5B4: the entry whose site id equals the base's type byte. The compare
             * is a SIGNED byte (LDRSB at 0x8B598) against the entry's dword. */
            nCount = (pEnv->pStrategList != NULL) ? PtrArray_GetCount(pEnv->pStrategList) : 0;
            for (i = 0; i < nCount; ++i) {
                StrategItem *pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, i);
                if (pItem != NULL && pBase != NULL &&
                    (i32)(i8)pBase->bUnk4 == pItem->nSiteId) {
                    break;
                }
            }
            if (i < nCount) {
                StrategItem *pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, i);
                if (pItem != NULL) {
                    pItem->nX = pEnv->nPlaceX;          /* 0x8B5E4 */
                    pItem->nY = pEnv->nPlaceY;          /* 0x8B5F8 */
                }
            }
            /* Deviation: on a miss the original leaves i at the count and writes nX / nY past the
             * last entry. The port skips the write. */

            *pEnv->pnScreenId = 1;                      /* 0x8B600 */
            if (pEnv->pbConfirm != NULL) {
                *pEnv->pbConfirm = 0;                   /* 0x8B608 */
            }
            if (pEnv->pbFacDone != NULL) {
                *pEnv->pbFacDone = 0;                   /* 0x8B60C */
            }
            return 2;
        }

        if (pEnv->pnSelectedBaseIdx != NULL && pEnv->pbBaseCount != NULL) {
            *pEnv->pnSelectedBaseIdx = *pEnv->pbBaseCount; /* 0x8B644: the base COUNT, signed */
        }
        *pEnv->pnScreenId = 7;                          /* 0x8B64C */
        (void)BaseFacScreen_Init(pEnv->pFacInit);       /* 0x8B654, the tail call */
        return 3;
    }

    return 0; /* any other screen id: the original returns the screen id itself */
}

/* --- BaseFacScreen_Toggle @ 0x589ec --- */

void BaseFacScreen_Toggle(const BaseFacToggleEnv *pEnv)
{
    UiPanel *pPanel;
    i32 nCount = 0;
    i32 nRows = 0;
    i32 nAux;
    i32 i;

    if (pEnv == NULL || pEnv->pbOpen == NULL || pEnv->ppPanel == NULL || pEnv->paFac == NULL) {
        return;
    }

    if (*pEnv->pbOpen != 0) {
        /* The close branch (0x58D98..0x58E04). */
        *pEnv->pbOpen = 0;
        for (i = 0; i < PUFO_BASEFAC_PANEL_CTL_COUNT; ++i) {
            UiControl *pCtl = pEnv->apPanelCtl[i]; /* unk_158874, unk_158878 */
            if (pCtl != NULL) {
                UiControl_FreePads(pCtl);          /* 0x58DB4 */
            }
            /* The original also operator delete's the control (0x58DBC). The port's controls are
             * caller-owned, so only their pads are released. */
        }
        pPanel = *pEnv->ppPanel;
        if (pPanel != NULL) {
            pPanel->nLineCount += 1;               /* 0x58DE4: the build branch left it one short */
            UiPanel_FreeLines(pPanel);             /* 0x58DF8 */
            free(pPanel);                          /* 0x58E00 */
            *pEnv->ppPanel = NULL;
        }
        return;
    }

    *pEnv->pbOpen = 1;                             /* 0x58A10 */

    for (i = 0; i < PUFO_BASEFAC_COUNT; ++i) {     /* 0x58A1C..0x58A30: records 0..16 */
        if (pEnv->paFac[i].bFlag20 != 0) {
            ++nCount;
        }
    }

    pPanel = (UiPanel *)malloc(sizeof(UiPanel));   /* 0x58A44: operator new(0x1BB4) */
    if (pPanel == NULL) {
        return; /* deviation: the original stores through the null panel at 0x58A7C */
    }
    if (!UiPanel_ctor(pPanel, nCount)) {           /* 0x58A54 */
        free(pPanel);
        return;
    }
    *pEnv->ppPanel = pPanel;

    pPanel->nStyle   = 5;                          /* 0x58A7C */
    pPanel->nFont    = 16;                         /* 0x58A88 */
    pPanel->pfnClick = pEnv->pfnBuyThunk;          /* 0x58A9C, the +3020 handler */
    pPanel->nPosX    = 120;                        /* 0x58AB0 */
    pPanel->nPosY    = 234;                        /* 0x58AC4 */
    pPanel->nUnk7052 = 5;                          /* 0x58AD4 */
    pPanel->nUnk7056 = 234;                        /* 0x58AE8 */
    pPanel->nViewW   = 230;                        /* 0x58AFC */
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);                        /* 0x58B0C */
    UiPanel_SetBytes6to11(pPanel, 0x44, 0xF4, 0xFC, 0x04, 0x46, 0x4B);    /* 0x58B38 */
    UiPanel_SetBytes3to5(pPanel, 0x46, 0x96, 0xAA);                        /* 0x58B4C */
    pPanel->bUnk7088  = 0x96;                      /* 0x58B60, the byte -106 */
    pPanel->bFlag7073 = 1;                         /* 0x58B74 */
    pPanel->bFlag7072 = 0;                         /* 0x58B88 */
    pPanel->nUnk7076  = 1;                         /* 0x58B9C */

    nAux = 1;
    for (i = 0; i < PUFO_BASEFAC_COUNT - 1; ++i) { /* 0x58BAC..0x58C0C: records 1..16 */
        const BaseFacFlag *pFac = &pEnv->paFac[i + 1];
        if (pFac->bFlag20 != 0) {
            UiPanel_SetLineText(pPanel, nRows, pFac->szName);
            UiPanel_SetLineAux(pPanel, nRows, nAux); /* the record number is the facility kind */
            ++nRows;
        }
        ++nAux;
    }

    /* 0x58C18: the 17th row, read from aBaseFacFlags[17] -- the four bytes at 0x187024, which is
     * a UiControl pointer slot rather than a facility record (bugs.tsv). */
    UiPanel_SetLineText(pPanel, nRows, (pEnv->pszLastRowName != NULL) ? pEnv->pszLastRowName : "");
    UiPanel_SetLineAux(pPanel, nRows, 0);          /* 0x58C50 */
    pPanel->nLineCount -= 1;                       /* 0x58C68 */

    if (pEnv->pbNewBaseMode != NULL && *pEnv->pbNewBaseMode != 0) {
        const i32 nBase = (i32)((pEnv->pnSelectedBaseIdx != NULL) ? *pEnv->pnSelectedBaseIdx : 0);
        if (pEnv->pnPanelSel != NULL) {
            *pEnv->pnPanelSel = nRows;             /* 0x58C88 */
        }
        if (pEnv->pbArmed != NULL) {
            *pEnv->pbArmed = 1;                    /* 0x58C94 */
        }
        for (i = 0; i < 6; ++i) {                  /* 0x58C9C..0x58D74: the whole 6 by 6 base */
            i32 k;
            for (k = 0; k < 6; ++k) {
                const i32 nCell = 6 * i + k;
                const i32 nFac = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase;
                pEnv->abFacilityGrid[PUFO_GRID_DAYS_STRIDE * nBase + nCell] = 0;
                pEnv->abFacilityGrid[nFac + PUFO_GRID_TILEA_OFF + nCell] = 0;
                pEnv->abFacilityGrid[nFac + PUFO_GRID_TILEB_OFF + nCell] = 0;
                pEnv->abFacilityGrid[nFac + PUFO_GRID_LAYER2_OFF + nCell] = 0;
                pEnv->abFacilityGrid[nFac + PUFO_GRID_FAC_TYPE_OFF + nCell] = PUFO_FAC_TYPE_NONE;
            }
        }
    }
}

/* --- BaseFacScreen_TryBuySelected @ 0x58910 --- */

i32 BaseFacScreen_TryBuySelected(const BaseFacBuyEnv *pEnv)
{
    i32 nSel;
    i32 nKind;
    u32 uCost;
    i32 nLeft;

    if (pEnv == NULL || pEnv->pPanel == NULL || pEnv->paFac == NULL || pEnv->pnFunds == NULL) {
        return 0;
    }

    nSel = pEnv->pPanel->nUnk3032;                 /* 0x58920, +0xBD8 */
    nKind = UiPanel_GetLineAux(pEnv->pPanel, nSel);/* 0x58938, anLineAux[sel] */
    if (nKind < 0 || nKind >= PUFO_BASEFAC_COUNT) {
        /* Deviation: the original indexes aBaseFacFlags with the unchecked aux value (0x5894C).
         * The port refuses the purchase instead of reading past the table. */
        return 0;
    }
    uCost = pEnv->paFac[nKind].uCost;              /* 0x5894C..0x58954, record + 0x18 */
    nLeft = *pEnv->pnFunds - (i32)uCost;           /* 0x58958 */

    if (nLeft < 0) {
        if (pEnv->pdwShortfall != NULL) {
            *pEnv->pdwShortfall = 1;               /* 0x58960, a dword store into a byte global */
        }
        if (pEnv->pnResidue != NULL) {
            *pEnv->pnResidue = 0;                  /* 0x5896C */
        }
        return 0;
    }

    if (pEnv->pbArmed != NULL) {
        *pEnv->pbArmed = 1;                        /* 0x5897C, arms the placement */
    }
    if (pEnv->pbNewBaseMode != NULL && *pEnv->pbNewBaseMode == 0 && pEnv->pnPanelSel != NULL) {
        *pEnv->pnPanelSel = nSel;                  /* 0x58994 */
    }
    return 1;
}

/* --- SoldierScreen_Toggle @ 0x64af0 --- */

static UiPanel *SoldierScreen_AllocPanel(i32 nLineCount)
{
    UiPanel *pPanel = (UiPanel *)malloc(sizeof(UiPanel)); /* 0x64B78: operator new(0x1BB4) */

    if (pPanel == NULL) {
        return NULL;
    }
    if (!UiPanel_ctor(pPanel, nLineCount)) {
        free(pPanel);
        return NULL;
    }
    return pPanel;
}

static void SoldierScreen_FreePanel(UiPanel **ppPanel)
{
    UiPanel *pPanel;

    if (ppPanel == NULL) {
        return;
    }
    pPanel = *ppPanel;
    if (pPanel != NULL) {
        UiPanel_FreeLines(pPanel);
        free(pPanel);
        *ppPanel = NULL;
    }
}

void SoldierScreen_Toggle(const SoldierScreenEnv *pEnv)
{
    UiPanel *pEquip;
    UiPanel *pStores;
    UiPanel *pNames;
    UiPanel *pCraft;
    PadImage *pPad;
    Soldier *const *apSoldiers;
    i32 nCount;
    i32 nRows;
    i32 i;

    if (pEnv == NULL || pEnv->pbOpen == NULL || pEnv->pnScreenId == NULL) {
        return;
    }

    Sound_Play(17);                                /* 0x64AFC */
    *pEnv->pnScreenId = 4;                         /* 0x64B08 */

    if (*pEnv->pbOpen != 0) {
        /* The close branch (0x651BC..0x65288). */
        *pEnv->pnScreenId = 1;                     /* 0x651C4 */
        *pEnv->pbOpen = 0;                         /* 0x651CC */
        if (pEnv->ppPanelEquip != NULL && *pEnv->ppPanelEquip != NULL) {
            (*pEnv->ppPanelEquip)->nLineCount = 4; /* 0x651D4, unguarded in the original */
        }
        if (pEnv->ppPanelStores != NULL && *pEnv->ppPanelStores != NULL) {
            (*pEnv->ppPanelStores)->nLineCount = 4;/* 0x651DC, unguarded in the original */
        }
        SoldierScreen_FreePanel(pEnv->ppPanelEquip);
        SoldierScreen_FreePanel(pEnv->ppPanelStores);
        if (pEnv->ppPad != NULL && *pEnv->ppPad != NULL) {
            pPad = *pEnv->ppPad;
            PadImage_FreeData(pPad);               /* 0x65230 */
            free(pPad);                            /* 0x65238 */
            *pEnv->ppPad = NULL;
        }
        SoldierScreen_FreePanel(pEnv->ppPanelNames);
        SoldierScreen_FreePanel(pEnv->ppPanelCraft);
        return;
    }

    *pEnv->pbOpen = 1;                             /* 0x64B28 */

    pPad = (PadImage *)malloc(sizeof(PadImage));   /* 0x64B30: operator new(0x14) */
    if (pPad != NULL) {
        PadImage_ctor(pPad);                       /* 0x64B40 */
        if (pEnv->pszPadName != NULL) {
            (void)PadImage_Load(pPad, pEnv->pszPadName, 8); /* 0x64B50 */
        }
        pPad->nOriginX = 0;                        /* 0x64B5C */
        pPad->nOriginY = 0;                        /* 0x64B64 */
    }
    if (pEnv->ppPad != NULL) {
        *pEnv->ppPad = pPad;
    }

    pEquip = SoldierScreen_AllocPanel(4);
    if (pEnv->ppPanelEquip != NULL) {
        *pEnv->ppPanelEquip = pEquip;
    }
    if (pEquip != NULL) {
        pEquip->nStyle   = 4;                      /* 0x64BA8 */
        pEquip->nFont    = 18;                     /* 0x64BAC */
        pEquip->pfnClick = pEnv->pfnEquipThunk;    /* 0x64BC0, +3020 */
        pEquip->nPosX    = 55;                     /* 0x64BD4 */
        pEquip->nPosY    = 102;                    /* 0x64BE0 */
        pEquip->nUnk7052 = 46;                     /* 0x64BE8 */
        pEquip->nUnk7056 = 102;                    /* 0x64BF0 */
        pEquip->nViewW   = 167;                    /* 0x64BF8 */
        UiPanel_SetBytes0to2(pEquip, 0x44, 0xF4, 0xFC);                     /* 0x64C08 */
        UiPanel_SetBytes6to11(pEquip, 0xC8, 0xF4, 0xFC, 0x04, 0x46, 0x4B);  /* 0x64C28 */
        UiPanel_SetBytes3to5(pEquip, 0x32, 0xA0, 0xBD);                     /* 0x64C84 */
        pEquip->bFlag7073 = 1;                     /* 0x64C90 */
        pEquip->bUnk7088  = 0xFF;                  /* 0x64C9C, the byte -1 */
    }

    pStores = SoldierScreen_AllocPanel(4);
    if (pEnv->ppPanelStores != NULL) {
        *pEnv->ppPanelStores = pStores;
    }
    if (pStores != NULL) {
        pStores->nStyle   = 4;                     /* 0x64CC8 */
        pStores->nFont    = 18;                    /* 0x64CE0 */
        pStores->nPosX    = 190;                   /* 0x64CE4 */
        pStores->nPosY    = 102;                   /* 0x64CEC */
        pStores->nUnk7052 = 46;                    /* 0x64CF4 */
        pStores->nUnk7056 = 102;                   /* 0x64D00 */
        pStores->nViewW   = 167;                   /* 0x64D08 */
        UiPanel_SetBytes0to2(pStores, 0x44, 0xF4, 0xFC);                    /* 0x64D14 */
        UiPanel_SetBytes6to11(pStores, 0x44, 0xF4, 0xFC, 0x04, 0x46, 0x4B); /* 0x64D24 */
        UiPanel_SetBytes3to5(pStores, 0x32, 0x32, 0xFA);                    /* 0x64D68 */
        pStores->bFlag7072 = 0;                    /* 0x64D78 */
    }

    /* 0x64BA4..0x64CC4: how many soldiers are on the selected base. */
    nCount = 0;
    apSoldiers = pEnv->apSoldiers;
    if (apSoldiers != NULL && pEnv->pnSoldierCount != NULL && pEnv->pnSelectedBaseIdx != NULL) {
        for (i = 0; i < (i32)*pEnv->pnSoldierCount; ++i) {
            if (apSoldiers[i] != NULL &&
                (i32)apSoldiers[i]->nKind == (i32)*pEnv->pnSelectedBaseIdx) {
                ++nCount;
            }
        }
    }

    pNames = SoldierScreen_AllocPanel(nCount);
    if (pEnv->ppPanelNames != NULL) {
        *pEnv->ppPanelNames = pNames;
    }
    if (pNames != NULL) {
        pNames->nStyle   = 13;                     /* 0x64E14 */
        pNames->nFont    = 18;                     /* 0x64E28 */
        pNames->pfnClick = pEnv->pfnSelectThunk;   /* 0x64E38, +3020 */
        pNames->nPosX    = 10;                     /* 0x64E44 */
        pNames->nPosY    = 70;                     /* 0x64E48 */
        pNames->nUnk7052 = 2;                      /* 0x64E4C */
        pNames->nUnk7056 = 70;                     /* 0x64E58 */
        pNames->nViewW   = 234;                    /* 0x64E68 */
        UiPanel_SetBytes0to2(pNames, 0xB4, 0xB4, 0xB4);                     /* 0x64E74 */
        UiPanel_SetBytes6to11(pNames, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);  /* 0x64E88 */
        UiPanel_SetBytes3to5(pNames, 0x32, 0x32, 0xFA);                     /* 0x64EA8 */
        pNames->bFlag7073 = 1;                     /* 0x64EC8 */
        pNames->bUnk7088  = 0x96;                  /* 0x64ED8, the byte -106 */
    }

    pCraft = SoldierScreen_AllocPanel(nCount);
    if (pEnv->ppPanelCraft != NULL) {
        *pEnv->ppPanelCraft = pCraft;
    }
    if (pCraft != NULL) {
        pCraft->nStyle   = 13;                     /* 0x64F1C */
        pCraft->nFont    = 18;                     /* 0x64F20 */
        pCraft->nPosX    = 230;                    /* 0x64F28 */
        pCraft->nPosY    = 70;                     /* 0x64F34 */
        pCraft->nUnk7052 = 5;                      /* 0x64F40 */
        pCraft->nUnk7056 = 70;                      /* 0x64F48 */
        pCraft->nViewW   = 40;                     /* 0x64F54 */
        UiPanel_SetBytes0to2(pCraft, 0xB4, 0xB4, 0xB4);                     /* 0x64F64 */
        UiPanel_SetBytes6to11(pCraft, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);  /* 0x64F74 */
        pCraft->bFlag7073 = 0;                     /* 0x64FB8 */
        pCraft->nUnk7076  = 2;                     /* 0x64FC4 */
        pCraft->bUnk7088  = 0x96;                  /* 0x64FD8, the byte -106 */
    }

    /* 0x65018..0x651A0: the row pass. */
    nRows = 0;
    if (pNames != NULL && pCraft != NULL && apSoldiers != NULL &&
        pEnv->pnSoldierCount != NULL && pEnv->pnSelectedBaseIdx != NULL) {
        for (i = 0; i < (i32)*pEnv->pnSoldierCount; ++i) {
            Soldier *pSol = apSoldiers[i];
            const char *pszName;

            if (pSol == NULL || (i32)pSol->nKind != (i32)*pEnv->pnSelectedBaseIdx) {
                continue;
            }
            UiPanel_SetLineText(pNames, nRows, pSol->szName);       /* 0x65048, soldier + 0xBA */
            UiPanel_SetLineAux(pNames, nRows, i);                   /* 0x65078, the roster index */
            pszName = (pEnv->pfnFormatName != NULL) ? pEnv->pfnFormatName(pEnv->pFormatUser, pSol)
                                                    : "";
            UiPanel_SetLineText(pCraft, nRows, (pszName != NULL) ? pszName : "");
            if (pSol->bUnk183 == 254) {
                UiPanel_SetLineMeta(pNames, nRows, 250, 150, 150, 20, 20, 20);  /* 0x650D0 */
                UiPanel_SetLineMeta(pCraft, nRows, 250, 150, 150, 20, 20, 20);
            } else if (pSol->bUnk183 == 255) {
                UiPanel_SetLineMeta(pNames, nRows, 150, 150, 250, 20, 20, 20);  /* 0x65124 */
                UiPanel_SetLineMeta(pCraft, nRows, 150, 150, 250, 20, 20, 20);
            }
            ++nRows;
        }
    }

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseSoldierThunk;       /* 0x651AC */
    }
}

/* --- SoldierScreen_SelectListed @ 0x64a9c --- */

i32 SoldierScreen_SelectListed(const SoldierSelectEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pPanel == NULL || pEnv->pnCursor == NULL) {
        return 0;
    }
    if (pEnv->pnScreenId != NULL) {
        *pEnv->pnScreenId = 5;                     /* 0x64AA8 */
    }
    *pEnv->pnCursor = UiPanel_GetLineAux(pEnv->pPanel, pEnv->pPanel->nUnk3032); /* 0x64AD0 */
    return *pEnv->pnCursor;
}

/* --- SoldierScreen_EquipSelected @ 0x619cc --- */

i32 SoldierScreen_EquipSelected(const SoldierEquipEnv *pEnv)
{
    Soldier *pSol;
    BaseRec *pBase;
    i32 nCursor;
    i32 nOld;
    i32 nRow;

    if (pEnv == NULL || pEnv->apSoldiers == NULL || pEnv->pnCursor == NULL ||
        pEnv->pnSoldierCount == NULL || pEnv->pPanelEquip == NULL ||
        pEnv->pbTransferFlag == NULL) {
        return 0;
    }

    Sound_Play(17);                                          /* 0x619D8 */

    nCursor = *pEnv->pnCursor;
    if (nCursor < 0 || nCursor >= (i32)*pEnv->pnSoldierCount) {
        return 0;
    }
    pSol = pEnv->apSoldiers[nCursor];                        /* 0x619F4 */
    if (pSol == NULL) {
        return 0;
    }

    /* 0x61A1C: run when the soldier has no craft (the 254/255 sentinels) or when its craft is
     * sitting at a base; a soldier on a craft that is off-base is left alone. */
    if (pSol->bUnk183 < 0xFE) {
        Ship *pCraft;
        if (pEnv->apCraft == NULL || pSol->bUnk183 >= PUFO_FAC_BASE_COUNT) {
            return 0;
        }
        pCraft = pEnv->apCraft[pSol->bUnk183];
        if (pCraft == NULL || pCraft->bAtBase == 0) {
            return 0;
        }
    }

    if (pEnv->apBases == NULL || pEnv->pnSelectedBaseIdx == NULL ||
        *pEnv->pnSelectedBaseIdx < 0 || *pEnv->pnSelectedBaseIdx >= PUFO_FAC_BASE_COUNT) {
        return 0;
    }
    pBase = pEnv->apBases[*pEnv->pnSelectedBaseIdx];         /* index 1117 dwords = Map + 0x1174 */

    nOld = pSol->nUnitType;
    nRow = UiPanel_GetLineAux(pEnv->pPanelEquip, pEnv->pPanelEquip->nUnk3032); /* 0x61A44 */

    switch (nRow) {
    case 0:
        if (nOld != 0) {
            if (pEnv->apObjects != NULL && nOld <= 3) {      /* 0x61B60 */
                BaseRec_AddItemQty(pBase,
                                   pEnv->apObjects[PUFO_SOLDIER_EQUIP_OBJ_BASE + nOld], 1);
            }
            pSol->nUnitType = 0;                             /* 0x61B74 */
        }
        break;
    case 1:
    case 2:
    case 3: {
        ObjectRecFull *pNew;
        pSol->nUnitType = (u8)nRow;                          /* 0x61AF0 / 0x61AE0 / 0x61A6C */
        if (pEnv->apObjects == NULL) {
            break;
        }
        pNew = pEnv->apObjects[PUFO_SOLDIER_EQUIP_OBJ_BASE + nRow];
        if (nOld != 0) {
            if (nOld <= 3) {
                /* BaseRec_AddItemQty with a negated delta is BaseRec_SubItemQty @ 0x5571c. */
                BaseRec_AddItemQty(pBase, pNew, -1);         /* 0x61A80 */
                BaseRec_AddItemQty(pBase,
                                   pEnv->apObjects[PUFO_SOLDIER_EQUIP_OBJ_BASE + nOld], 1); /* 0x61AC4 */
            }
        } else {
            BaseRec_AddItemQty(pBase, pNew, -1);             /* 0x61B04 */
        }
        break;
    }
    default:
        break;
    }

    *pEnv->pbTransferFlag = 0;                               /* 0x61B78 */
    return 1;
}
