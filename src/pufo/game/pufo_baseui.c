/* pufo_baseui.c - see pufo_baseui.h for evidence. */
#include "game/pufo_baseui.h"
#include <stdlib.h>  /* atexit, for StrategList_RegisterAtexit @ 0x838a0 */
#include <stdio.h>   /* snprintf, the bounded stand-in for the original's j_itoa */
#include "util/pufo_str.h" /* pufo_strcpy/pufo_strcat, the line writes of ResearchScreen_RefreshPanels */
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
