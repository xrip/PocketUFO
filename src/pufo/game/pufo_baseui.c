/* pufo_baseui.c - see pufo_baseui.h for evidence. */
#include "game/pufo_baseui.h"
#include <stdlib.h>  /* atexit, for StrategList_RegisterAtexit @ 0x838a0 */
#include <stdio.h>   /* snprintf, the bounded stand-in for the original's j_itoa */
#include "util/pufo_str.h" /* pufo_strcpy/pufo_strcat, the line writes of ResearchScreen_RefreshPanels */
#include "audio/pufo_audio.h" /* Sound_Play, the screen-open sample of SoldierScreen_Toggle */
#include "game/pufo_strateg.h" /* StrategList_Clear */
#include "game/pufo_font.h"    /* Text_DrawString / Text_MeasureWidth, the dialog's labels */
#include "game/pufo_boot.h"    /* PUFO_TEXT_REC_STRIDE, the craft-name records */
#include "game/pufo_mainmenu.h" /* PUFO_MAINMENU_MODE_*, the list mode the slot handlers set */

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

/* Which auResearchCol entry the two quantity buttons act on: the topic number the panel
 * carries for the cached row, plus that base's 144-entry column offset. -1 when the original
 * would have dereferenced something the port does not have. */
static i32 ResearchQtyIndex(const ResearchQtyEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pTopicPanel == NULL || pEnv->apResearchCols == NULL) {
        return -1;
    }
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    if (nBase < 0 || nBase >= PUFO_FAC_BASE_COUNT) {
        return -1;
    }
    const i32 nRow = (pEnv->pnTopicRow != NULL) ? *pEnv->pnTopicRow : 0;
    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return -1;
    }
    const i32 nTopic = pEnv->pTopicPanel->anLineAux[nRow]; /* 0x72F30 / 0x72F88 */
    if (nTopic < 0 || nTopic >= PUFO_RESEARCH_TOPIC_COUNT) {
        return -1;
    }
    return nTopic + PUFO_RESEARCH_TOPIC_COUNT * nBase; /* MUL #0x90, ADD topic */
}

/* ResearchScreen_IncSelectedQty @ 0x72ea0. The scientist gate first, then the lab-slot gate,
 * then the increment. Base_HasResearchSlotFree returns a plain 0/1 (MOV R0,#0 / MOVLE R0,#1 at
 * 0x66334), so the original's ANDS R3,R0,#0xFF test and this one agree. */
i32 ResearchScreen_IncSelectedQty(const ResearchQtyEnv *pEnv)
{
    const i32 nIdx = ResearchQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puResearchEntries != NULL) ? (i32)*pEnv->puResearchEntries : 0;
    /* nIdx already carries the 144*base term, so the count functions get the base's own column
     * while the store goes straight into the flat array. */
    const u16 *pCol = pEnv->apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase;

    const i32 nSci = Base_GetScientistCount(pEnv->pScientistRec, pCol, nBase, nEntries);
    if (nSci <= 0) {
        return nSci; /* 0x72EC8: R0 still holds the count the compare tested */
    }

    const i32 nActive = Base_CountActiveResearch(pCol, nEntries); /* 0x72ED4 */
    if (!Base_HasResearchSlotFree(pEnv->abFacilityGrid, nBase, nActive + 1)) { /* 0x72EEC */
        return 0; /* 0x72F5C: R0 holds that call's 0 */
    }

    u16 *pEntry = &pEnv->apResearchCols[nIdx];
    *pEntry = (u16)(*pEntry + 1); /* 0x72F58, the LSL #16 / LSR #16 truncation */
    return PUFO_RESEARCH_COL_OFFSET; /* 0x72F3C left 4900 in R0 */
}

/* ResearchScreen_DecSelectedQty @ 0x72f70. No gate at all beyond the entry being non-zero, and
 * 4900 in R0 whether it stored anything or not. */
i32 ResearchScreen_DecSelectedQty(const ResearchQtyEnv *pEnv)
{
    const i32 nIdx = ResearchQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }

    u16 *pEntry = &pEnv->apResearchCols[nIdx];
    if (*pEntry != 0) { /* 0x72FC8 */
        *pEntry = (u16)(*pEntry - 1); /* 0x72FE4 */
    }
    return PUFO_RESEARCH_COL_OFFSET;
}

/* ResearchScreen_UpdateProgress @ 0x72d14. The total scientist count fixes the iteration count;
 * the two gates decide how many of those iterations actually store, and because both gates get
 * worse by one per store the button commits everything the base can spare. */
i32 ResearchScreen_UpdateProgress(const ResearchQtyEnv *pEnv)
{
    const i32 nIdx = ResearchQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puResearchEntries != NULL) ? (i32)*pEnv->puResearchEntries : 0;
    const u16 *pCol = pEnv->apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase;
    u16 *pEntry = &pEnv->apResearchCols[nIdx];

    i32 nTotal = Base_GetItemQty(pEnv->pScientistRec, nBase); /* Base_GetItemQtyA, 0x72D30 */
    if (nTotal <= 0) {
        return nTotal; /* 0x72D38: the loop never runs */
    }

    i32 nIter = 0; /* R9 */
    for (;;) {
        const i32 nSci = Base_GetScientistCount(pEnv->pScientistRec, pCol, nBase, nEntries);
        if (nSci > 0) { /* 0x72D94 */
            const i32 nActive = Base_CountActiveResearch(pCol, nEntries); /* 0x72DA8 */
            if (Base_HasResearchSlotFree(pEnv->abFacilityGrid, nBase, nActive + 1)) { /* 0x72DC4 */
                *pEntry = (u16)(*pEntry + 1); /* 0x72DF4 */
            }
        }
        ++nIter; /* 0x72E1C */
        nTotal = Base_GetItemQty(pEnv->pScientistRec, nBase); /* 0x72E20 */
        if (nIter >= nTotal) {
            break; /* 0x72E28: CMP R9, R0 / BLT */
        }
    }
    return nTotal; /* 0x72E2C */
}

/* ResearchScreen_CancelSelectedAndClose @ 0x73380. The store has no gate at all, and the close is
 * a tail call, so it runs whatever the store did. */
void ResearchScreen_CancelSelectedAndClose(const ResearchCancelEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    const i32 nIdx = ResearchQtyIndex(&pEnv->qty);
    if (nIdx >= 0) {
        pEnv->qty.apResearchCols[nIdx] = 0; /* 0x733D8: MOV R2,#0 / STRH R2 */
    }
    if (pEnv->close.pfnCloseUi != NULL) {
        pEnv->close.pfnCloseUi(pEnv->close.pUser); /* 0x733DC */
    }
}

/* ResearchScreen_AccumulateAllocated @ 0x74914. Eight base slots regardless of how many bases
 * exist, and the topic index is the column index, so every base feeds the same 144 records. */
i32 ResearchScreen_AccumulateAllocated(const ResearchAccumulateEnv *pEnv)
{
    if (pEnv == NULL || pEnv->aTopics == NULL || pEnv->apResearchCols == NULL) {
        return 0;
    }

    i32 nEntries = (pEnv->puResearchEntries != NULL) ? (i32)*pEnv->puResearchEntries : 0;
    if (nEntries > PUFO_RESEARCH_TOPIC_COUNT) {
        nEntries = PUFO_RESEARCH_TOPIC_COUNT; /* deviation: the original would run one base's
                                               * column straight into the next one */
    }

    for (i32 nBase = 0; nBase < PUFO_FAC_BASE_COUNT; ++nBase) { /* 0x7491C: MOV R6,#8 */
        const u16 *pCol = pEnv->apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase;
        for (i32 nTopic = 0; nTopic < nEntries; ++nTopic) {
            const u16 w = pCol[nTopic]; /* 0x74950 */
            if (w != 0) { /* 0x74960 */
                pEnv->aTopics[nTopic].wProgress = (u16)(pEnv->aTopics[nTopic].wProgress + w); /* 0x7496C */
            }
        }
    }

    if (pEnv->pfnRebuild != NULL) {
        return pEnv->pfnRebuild(pEnv->pRebuildUser); /* 0x749A4: tail call */
    }
    return 0;
}

/* Which auManufCol entry the manufacturing buttons act on: the product number the panel carries
 * for the cached row, plus that base's 200-entry column offset. */
static i32 ManufQtyIndex(const ManufQtyEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pProductPanel == NULL || pEnv->apManufCols == NULL) {
        return -1;
    }
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    if (nBase < 0 || nBase >= PUFO_FAC_BASE_COUNT) {
        return -1;
    }
    const i32 nRow = (pEnv->pnProductRow != NULL) ? *pEnv->pnProductRow : 0;
    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return -1;
    }
    const i32 nProduct = pEnv->pProductPanel->anLineAux[nRow]; /* 0x6DEF8, 0x6E0F0, 0x6DF30 */
    if (nProduct < 0 || nProduct >= PUFO_PRODUCT_ENTRIES_PER_BASE) {
        return -1;
    }
    return nProduct + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase; /* MUL #0xC8, ADD product */
}

/* ORIGINAL BUG (dead code): the gate every one of these buttons opens with. A non-zero
 * bManufQtyGate @ 0x15EE50 stops it dead and the byte itself becomes the return value -- 0x6DDE0,
 * 0x6DEC4, 0x6E0A0, 0x6E140, 0x6E334 and 0x6DFA8 all read it and bail -- but no code in the
 * original ever writes a non-zero value to that byte: the only store to it anywhere is
 * ManufScreen_SelectListed @ 0x6E970 writing 0. The gate never fires in the shipped build. The
 * port keeps it because the original has it. */
static i32 ManufQtyGateValue(const ManufQtyEnv *pEnv)
{
    return (pEnv->pbQtyGate != NULL) ? (i32)*pEnv->pbQtyGate : 0;
}

/* ManufScreen_IncSelectedQty @ 0x6dddc. The engineer gate first, then the workshop-slot gate,
 * then the increment -- the research pair's shape with the crew and facility kinds swapped. */
i32 ManufScreen_IncSelectedQty(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }
    const i32 nGate = ManufQtyGateValue(pEnv);
    if (nGate != 0) {
        return nGate; /* 0x6DDE4 */
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puManufEntries != NULL) ? (i32)*pEnv->puManufEntries : 0;
    const u16 *pCol = pEnv->apManufCols + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase;
    u16 *pEntry = &pEnv->apManufCols[nIdx];

    const i32 nEng = Base_GetEngineerCount(pEnv->pEngineerRec, pCol, nBase, nEntries);
    if (nEng <= 0) {
        return nEng; /* 0x6DE0C: R0 still holds the count the compare tested */
    }
    const i32 nActive = Base_CountActiveManuf(pCol, nEntries); /* 0x6DE20 */
    if (!Base_HasWorkshopSlotFree(pEnv->abFacilityGrid, nBase, nActive + 1)) { /* 0x6DE38 */
        return 0;
    }
    *pEntry = (u16)(*pEntry + 1); /* 0x6DEA4 */
    return PUFO_MANUF_COL_OFFSET; /* 0x6DE88 */
}

/* ManufScreen_DecSelectedQty @ 0x6e09c. No gate beyond the entry being non-zero, and 17156 in R0
 * whether it stored anything or not. */
i32 ManufScreen_DecSelectedQty(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }
    const i32 nGate = ManufQtyGateValue(pEnv);
    if (nGate != 0) {
        return nGate; /* 0x6E0A4 */
    }

    u16 *pEntry = &pEnv->apManufCols[nIdx];
    if (*pEntry != 0) { /* 0x6E108 */
        *pEntry = (u16)(*pEntry - 1); /* 0x6E120 */
    }
    return PUFO_MANUF_COL_OFFSET; /* 0x6E0FC */
}

/* ManufScreen_ReduceSelectedQtyToOne @ 0x6dec0, renamed in the IDB from
 * ManufScreen_GetSelectedProductId because it never returns a product id. The ARM steps the value
 * down one at a time while it is above 1 (0x6DF30..0x6DF54, re-reading anLineAux each pass), which
 * is exactly a clamp to 1; the port writes the clamp directly and gets the same result. */
i32 ManufScreen_ReduceSelectedQtyToOne(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }
    const i32 nGate = ManufQtyGateValue(pEnv);
    if (nGate != 0) {
        return nGate; /* 0x6DEC8 */
    }

    u16 *pEntry = &pEnv->apManufCols[nIdx];
    if (*pEntry > 1) { /* 0x6DF64: CMP R2, #1 / BHI */
        *pEntry = 1;
    }
    return PUFO_MANUF_COL_OFFSET; /* 0x6DF10 */
}

/* ManufScreen_DecEngineers @ 0x6e330. The engineers assigned to this product (aManufProductB, a
 * dword per product per base) may not fall to or below the quantity being made plus one. */
i32 ManufScreen_DecEngineers(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }
    const i32 nGate = ManufQtyGateValue(pEnv);
    if (nGate != 0) {
        return nGate; /* 0x6E338 */
    }

    if (pEnv->apProductB != NULL) {
        const i32 nAlloc = pEnv->apProductB[nIdx]; /* 0x6E394 */
        const i32 nQty = (i32)pEnv->apManufCols[nIdx] + 1; /* 0x6E3A4..0x6E3AC */
        if (nAlloc > nQty) { /* 0x6E3B0: CMP R4, R1 / SUBGT */
            pEnv->apProductB[nIdx] = nAlloc - 1; /* 0x6E3B8 */
        }
    }
    return PUFO_MANUF_ANCHOR; /* 0x6E3BC: R0 still holds the 0x159E98 product-block anchor */
}

/* ManufScreen_UpdateProgress @ 0x6df6c. The manufacturing twin of ResearchScreen_UpdateProgress:
 * the engineer total fixes the iteration count, the two gates decide what stores. The gate byte is
 * re-read on every iteration (0x6DFA8), and when it is set the iteration skips the gates and the
 * store but still counts. */
i32 ManufScreen_UpdateProgress(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puManufEntries != NULL) ? (i32)*pEnv->puManufEntries : 0;
    const u16 *pCol = pEnv->apManufCols + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase;
    u16 *pEntry = &pEnv->apManufCols[nIdx];

    i32 nTotal = Base_GetItemQty(pEnv->pEngineerRec, nBase); /* Base_GetItemQtyB, 0x6DF80 */
    if (nTotal <= 0) {
        return nTotal; /* 0x6DF88 */
    }

    i32 nIter = 0; /* R9 */
    for (;;) {
        if (ManufQtyGateValue(pEnv) == 0) { /* 0x6DFA8..0x6DFB4 */
            const i32 nEng = Base_GetEngineerCount(pEnv->pEngineerRec, pCol, nBase, nEntries);
            if (nEng > 0) { /* 0x6DFD8 */
                const i32 nActive = Base_CountActiveManuf(pCol, nEntries); /* 0x6DFEC */
                if (Base_HasWorkshopSlotFree(pEnv->abFacilityGrid, nBase, nActive + 1)) { /* 0x6E010 */
                    *pEntry = (u16)(*pEntry + 1); /* 0x6E040 */
                }
            }
        }
        ++nIter; /* 0x6E078 */
        nTotal = Base_GetItemQty(pEnv->pEngineerRec, nBase); /* 0x6E084 */
        if (nIter >= nTotal) {
            break; /* 0x6E090: CMP R9, R0 / BLT */
        }
    }
    return nTotal; /* 0x6E098 */
}

/* ManufScreen_SelectListed @ 0x6e914. The product list's row click. */
i32 ManufScreen_SelectListed(const ManufSelectEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pProductPanel == NULL) {
        return 0;
    }
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(7, pEnv->pSampleUser); /* 0x6E920 */
    }

    const i32 nRow = pEnv->pProductPanel->nUnk3032; /* 0x6E938: LDR [R0,#0xBD8] */
    if (pEnv->pnProductRow != NULL) {
        *pEnv->pnProductRow = nRow; /* 0x6E944 */
    }
    if (pEnv->pbProductSelected != NULL) {
        *pEnv->pbProductSelected = 1; /* 0x6E950 */
    }
    /* 0x6E958: MOV R2,#0 / STRB R2,[R0,#0xBD8]. A byte store into a dword field, so only the low
     * byte of the cursor is cleared and the upper 24 bits survive. */
    pEnv->pProductPanel->nUnk3032 =
        (i32)(((u32)pEnv->pProductPanel->nUnk3032) & 0xFFFFFF00u);
    if (pEnv->pbAnimStarted != NULL) {
        *pEnv->pbAnimStarted = 0; /* 0x6E964 */
    }
    if (pEnv->pbQtyGate != NULL) {
        *pEnv->pbQtyGate = 0; /* 0x6E970: the only write bManufQtyGate ever gets */
    }
    return nRow;
}

/* The manufacturing screen's selected product: the panel's cached row cursor indexes its aux
 * column, and that cell holds the product number -- the same row-to-entity mapping the hangar crew
 * list uses. -1 means the row is outside the panel, which the original never checks. */
static i32 ManufSelectedProduct(const UiPanel *pPanel, const i32 *pnRow)
{
    if (pPanel == NULL || pnRow == NULL) {
        return -1;
    }
    const i32 nRow = *pnRow;
    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return -1;
    }
    return pPanel->anLineAux[nRow];
}

#define PUFO_PRODUCT_COLUMN_TOTAL (PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT)

void ManufScreen_ClearProductSlot(const ManufClearEnv *pEnv, i32 nBaseIndex, i32 nProduct)
{
    if (pEnv == NULL) {
        return;
    }

    nProduct &= 0xFF;                  /* 0x6F8D0 AND R3,R2,#0xFF */
    nBaseIndex = (i32)(i8)nBaseIndex;  /* 0x6F8D4 LSL#24 then ASR#24 sign-extends the byte */

    const i32 nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nBaseIndex + nProduct;
    if (nIdx >= 0 && nIdx < PUFO_PRODUCT_COLUMN_TOTAL) {
        if (pEnv->apProductB != NULL) {
            pEnv->apProductB[nIdx] = 0; /* 0x6F8FC, the u32 column at 0x15B79C */
        }
        if (pEnv->apManufCols != NULL) {
            pEnv->apManufCols[nIdx] = 0; /* 0x6F910, auManufCol @ 0x15E19C */
        }
        if (pEnv->apProductA != NULL) {
            pEnv->apProductA[nIdx] = 0; /* 0x6F918, the u32 column at 0x159E9C */
        }
        if (pEnv->apProductC != NULL) {
            pEnv->apProductC[nIdx] = 0; /* 0x6F92C, the u16 column at 0x15D09C */
        }
    }

    if (nProduct < PUFO_MANUF_HANGAR_PRODUCT_FIRST || nProduct > PUFO_MANUF_HANGAR_PRODUCT_LAST) {
        return; /* 0x6F930 BLS and 0x6F938 BCS both bail on the range test */
    }

    const i32 nSelBase = (pEnv->pnSelectedBaseIdx != NULL)
                             ? (i32)(i8)*pEnv->pnSelectedBaseIdx
                             : 0;
    if (pEnv->apBases == NULL || nSelBase < 0 || nSelBase >= pEnv->nBaseCount) {
        return; /* deviation: the original dereferences apBases[byte] with no check at all */
    }

    /* 0x6F968 LDRB [R3,#4]: the selected base's type byte. GetByte0x654 reads the grid byte for
     * that type and SubByte0x654 then subtracts that very amount from the same cell, so the pair
     * zeroes it -- and SubByte0x654 leaves a zero alone, which is why this is not just a store. */
    const u8 bType = pEnv->apBases[nSelBase]->bUnk4;
    const i32 nAmount = BaseGrid_GetTypeByte(pEnv->apBases, pEnv->nBaseCount,
                                             pEnv->abGridBytes, bType);
    (void)BaseGrid_SubTypeByte(pEnv->apBases, pEnv->nBaseCount, pEnv->abGridBytes,
                               bType, (i8)nAmount);
}

void ManufScreen_ConfirmAndClose(const ManufClearEnv *pEnv, const GeoscapeCloseHooks *pHooks)
{
    if (pEnv == NULL) {
        return;
    }

    const i32 nProduct = ManufSelectedProduct(pEnv->pProductPanel, pEnv->pnProductRow);
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)(i8)*pEnv->pnSelectedBaseIdx : 0;
    ManufScreen_ClearProductSlot(pEnv, nBase, nProduct); /* 0x6E9C4 */

    if (pHooks != NULL && pHooks->pfnCloseUi != NULL) {
        pHooks->pfnCloseUi(pHooks->pUser); /* 0x6E9C8, tail-call into Geoscape_CloseUI */
    }
}

void ManufScreen_IncEngineers(const ManufEngineerEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pbQtyGate != NULL && (*pEnv->pbQtyGate & 0xFFu) != 0) {
        return; /* 0x6E14C */
    }

    /* No byte mask here, unlike ClearProductSlot: this function uses the aux cell's raw dword for
     * both the column index and the object lookup. */
    const i32 nProduct = ManufSelectedProduct(pEnv->pProductPanel, pEnv->pnProductRow);
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)(i8)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nBase + nProduct;
    const bool bWritable = (nIdx >= 0 && nIdx < PUFO_PRODUCT_COLUMN_TOTAL &&
                            pEnv->apProductB != NULL);

    if (nProduct >= PUFO_MANUF_HANGAR_PRODUCT_FIRST && nProduct <= PUFO_MANUF_HANGAR_PRODUCT_LAST) {
        const i32 nCap = Base_CalcHangarCap(pEnv->abFacilityGrid, nBase);
        const i32 nAt = Base_CountShipsAtBase(pEnv->apCraft, pEnv->nCraftCount,
                                              pEnv->apBases, pEnv->nBaseCount,
                                              pEnv->abGridBytes, nBase);
        const i32 nFree = nCap - nAt; /* 0x6E1B0 SUBS LR, R11, R0 */

        if (nCap == nAt) { /* 0x6E1B4: a full hangar, so the free-slot count is zero */
            if (pEnv->pdwShortfall != NULL) {
                *pEnv->pdwShortfall = 1; /* 0x6E1BC, a dword store into byte_1870C8 */
            }
            if (pEnv->pnResidue != NULL) {
                *pEnv->pnResidue = 1; /* 0x6E1C8 */
            }
        }

        if (pEnv->pbQtyGate != NULL && (*pEnv->pbQtyGate & 0xFFu) != 0) {
            return; /* 0x6E1CC: the gate is read a second time, after those two stores */
        }

        /* With a full hangar nFree is 0 and the allocation is never negative, so this test fails
         * and the flags above are the whole result. */
        if (bWritable && pEnv->apProductB[nIdx] < nFree) {
            pEnv->apProductB[nIdx] = pEnv->apProductB[nIdx] + 1; /* 0x6E228 */
        }
        return;
    }

    const i32 nUsed = Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nBase);   /* 0x6E274 */
    const i32 nAcross = Base_CountItemQtyAcrossBases(pEnv->aSites, pEnv->nSiteCount, nBase,
                                                     pEnv->apBases, nBase,
                                                     pEnv->apObjects, pEnv->nObjectCount, true);
    const i32 nCap = Base_CalcStoresCapacity(pEnv->abFacilityGrid, nBase);                /* 0x6E29C */

    /* 0x6E214: the object is apObjects[nProduct] -- the PRODUCT number indexes the object table,
     * not the base -- and only its bCargoSize byte at +0xC6 is read. */
    i32 nCargo = 0;
    if (pEnv->apObjects != NULL && nProduct >= 0 && nProduct < pEnv->nObjectCount) {
        nCargo = (i32)pEnv->apObjects[nProduct]->bCargoSize;
    }

    if (!bWritable) {
        return;
    }

    const i32 nAlloc = pEnv->apProductB[nIdx];
    if (nCargo * nAlloc + nAcross + nUsed < nCap) { /* 0x6E300 CMP R2, R0 */
        pEnv->apProductB[nIdx] = nAlloc + 1;        /* 0x6E30C */
    } else {
        if (pEnv->pdwShortfall != NULL) {
            *pEnv->pdwShortfall = 1; /* 0x6E314 */
        }
        if (pEnv->pnResidue != NULL) {
            *pEnv->pnResidue = 0; /* 0x6E320: 0 here, 1 on the full-hangar path */
        }
    }
}

/* The four item columns the buy/sell confirm test adds to the cargo sum and the soldier count
 * (0x750E8, 0x750F4, 0x750EC, 0x750F8 -- the ARM loads them out of order and sums them). */
static const i32 g_anBuySellConfirmItems[4] = {112, 113, 117, 118};

/* BuySellScreen_ConfirmSiteAndClose @ 0x75090. Promote the transfer flow's working site -- the
 * record AT the site count -- into a real site when it holds anything, then close the screen. */
void BuySellScreen_ConfirmSiteAndClose(const BuySellConfirmEnv *pEnv,
                                       const GeoscapeCloseHooks *pHooks)
{
    if (pEnv == NULL) {
        return;
    }

    const i32 nSiteCount = (pEnv->pnSiteCount != NULL) ? *pEnv->pnSiteCount : 0;
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)(i8)*pEnv->pnSelectedBaseIdx : 0;

    if (pEnv->aSites != NULL && nSiteCount >= 0 && nSiteCount < PUFO_SMAP_SITE_COUNT) {
        SmapSite *pSite = &pEnv->aSites[nSiteCount];
        bool bConfirm = false;

        /* 0x750B0: the site index is the site count, the base index is nSelectedBaseIdx, and the
         * scan-sites flag is OFF -- so only this one record's cargo is measured. */
        bConfirm = Base_CountItemQtyAcrossBases(pEnv->aSites, nSiteCount, nSiteCount, pEnv->apBases,
                                                nBase, pEnv->apObjects, pEnv->nObjectCount,
                                                false) != 0;

        if (!bConfirm && pSite->uRecoveredSoldiers != 0) {
            bConfirm = true; /* 0x750DC */
        }
        if (!bConfirm) {
            i32 nSum = 0;
            for (i32 i = 0; i < (i32)(sizeof(g_anBuySellConfirmItems) /
                                     sizeof(g_anBuySellConfirmItems[0])); ++i) {
                nSum += pSite->auItemQty[g_anBuySellConfirmItems[i]];
            }
            bConfirm = nSum != 0; /* 0x75104 ADDS / BEQ */
        }

        if (bConfirm) {
            pSite->bFlag0 = PUFO_SITE_FLAG_CONFIRM; /* 0x75120 STRB */
            if (pEnv->pnSiteCount != NULL) {
                *pEnv->pnSiteCount = nSiteCount + 1; /* 0x75124 */
            }
        }
    }

    if (pEnv->pnFundsSnapshot != NULL) {
        *pEnv->pnFundsSnapshot = pEnv->nFunds; /* 0x75130, the buy/sell scratch copy */
    }
    if (pHooks != NULL && pHooks->pfnCloseUi != NULL) {
        pHooks->pfnCloseUi(pHooks->pUser); /* 0x7513C, the tail call */
    }
}

/* race id minus 0x80 -> the funding slot it feeds, -1 when the race gives no bonus. The order is
 * the compare chain's, not the slot order: 0x84 feeds slot 1 while 0x82 and 0x83 feed slots 2
 * and 3. */
static const i8 g_anFundingSlotByRace[PUFO_RACE_ID_COUNT] = {0, 0, 2, 3, 1, 4, -1, -1, 0, -1};

/* Funding_AddRaceBonus @ 0x7debc. One radar contact of one race adds two to that race's funding
 * score, or -- for the no-race tile -- to the monthly multiplier. */
void Funding_AddRaceBonus(i32 *anFundingSlots, i32 nRaceId)
{
    if (anFundingSlots == NULL) {
        return;
    }

    const u32 bRace = (u32)(u8)nRaceId; /* 0x7DEBC AND R0,R1,#0xFF */
    i32 nSlot = -1;

    if (bRace >= PUFO_RACE_ID_FIRST && bRace < PUFO_RACE_ID_FIRST + PUFO_RACE_ID_COUNT) {
        nSlot = g_anFundingSlotByRace[bRace - PUFO_RACE_ID_FIRST];
    } else if (bRace == PUFO_RACE_ID_NONE) {
        nSlot = PUFO_FUNDING_MULT_SLOT; /* 0x7DF48 */
    }

    if (nSlot >= 0) {
        anFundingSlots[nSlot] += PUFO_FUNDING_BONUS;
    }
}

/* ResearchScreen_StartSelected @ 0x73240. Allocation turns into running work only when the topic
 * is idle and this base has actually allocated something to it. */
i32 ResearchScreen_StartSelected(const ResearchStartEnv *pEnv)
{
    const i32 nIdx = ResearchQtyIndex(&pEnv->qty);
    if (nIdx < 0 || pEnv->aTopics == NULL) {
        return 0;
    }

    if (pEnv->pbTopicSelected != NULL) {
        *pEnv->pbTopicSelected = 0; /* 0x7324C */
    }
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, pEnv->pSampleUser); /* 0x73258 */
    }

    const i32 nBase = (pEnv->qty.pnSelectedBaseIdx != NULL) ? (i32)*pEnv->qty.pnSelectedBaseIdx : 0;
    const i32 nTopic = nIdx - PUFO_RESEARCH_TOPIC_COUNT * nBase;
    const i32 nEntries = (pEnv->qty.puResearchEntries != NULL) ? (i32)*pEnv->qty.puResearchEntries : 0;
    const u16 *pResearchCol = pEnv->qty.apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase;

    if (pEnv->aTopics[nTopic].wProgress == 0 && pEnv->qty.apResearchCols[nIdx] != 0) {
        pEnv->aTopics[nTopic].wProgress = 1; /* 0x732C8 tests, 0x732D8 stores */
    }

    (void)ResearchScreen_RefreshPanels(pEnv->aTopics, nEntries, pResearchCol,
                                       pEnv->qty.pTopicPanel, pEnv->pPanelQty,
                                       pEnv->pPanelProgress); /* 0x732DC */

    if (pEnv->qty.abFacilityGrid != NULL) {
        const i32 nActiveManuf = (pEnv->apManufCols != NULL)
                                     ? Base_CountActiveManuf(pEnv->apManufCols +
                                                             PUFO_PRODUCT_ENTRIES_PER_BASE * nBase,
                                                             nEntries)
                                     : 0;
        (void)Base_UpdateWorkshopNeeded(pEnv->qty.abFacilityGrid, nBase, nActiveManuf); /* 0x732F0 */
        (void)Base_UpdateLabNeeded(pEnv->qty.abFacilityGrid, nBase,
                                   Base_CountActiveResearch(pResearchCol, nEntries)); /* 0x73304 */
    }

    if (pEnv->pfnRebuild != NULL) {
        return pEnv->pfnRebuild(pEnv->pRebuildUser); /* 0x73320: tail call */
    }
    return 0;
}

/* Base_EnterBuildMode @ 0x84e20. */
void Base_EnterBuildMode(const BaseBuildModeEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, pEnv->pSampleUser); /* 0x84E2C */
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puResearchEntries != NULL) ? (i32)*pEnv->puResearchEntries : 0;

    /* Deviation: the original feeds nSelectedBaseIdx straight through, so an index outside 0..7
     * reads auManufCol/auResearchCol outside the eight-block array and, for base 8, writes the
     * needed grid past its 1524 bytes. The port refuses the pair the same way ResearchQtyIndex
     * does; the sample, the screen id, the nav rebuild and the parked control are unaffected. */
    if (pEnv->abFacilityGrid != NULL && nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT) {
        const i32 nActiveManuf = (pEnv->apManufCols != NULL)
                                     ? Base_CountActiveManuf(pEnv->apManufCols +
                                                             PUFO_PRODUCT_ENTRIES_PER_BASE * nBase,
                                                             nEntries)
                                     : 0;
        (void)Base_UpdateWorkshopNeeded(pEnv->abFacilityGrid, nBase, nActiveManuf); /* 0x84E44 */
        const i32 nActiveResearch = (pEnv->apResearchCols != NULL)
                                        ? Base_CountActiveResearch(pEnv->apResearchCols +
                                                                   PUFO_RESEARCH_TOPIC_COUNT * nBase,
                                                                   nEntries)
                                        : 0;
        (void)Base_UpdateLabNeeded(pEnv->abFacilityGrid, nBase, nActiveResearch); /* 0x84E58 */
    }

    if (pEnv->pnScreenId != NULL) {
        *pEnv->pnScreenId = 1; /* 0x84E64 */
    }
    (void)Geoscape_BuildNavControls(pEnv->pNavOut, pEnv->pNavEnv); /* 0x84E6C */

    if (pEnv->pParkCtl != NULL) {
        pEnv->pParkCtl->x = 1000; /* 0x84E80 */
        pEnv->pParkCtl->y = 1000; /* 0x84E88 */
    }
}

/* Smap_SetWaypoint @ 0x85440. Colour slot 0 of message line 0 carries the ship index. */
void Smap_SetWaypoint(const SmapWaypointEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pLines == NULL) {
        return;
    }

    const i32 nShipIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x85460 */
    if (pEnv->pnStrategIndex != NULL) {
        *pEnv->pnStrategIndex = nShipIdx; /* 0x85460 */
    }
    if (pEnv->pnSelectedShipIdx != NULL) {
        *pEnv->pnSelectedShipIdx = nShipIdx; /* 0x85468 */
    }

    (void)Ship_ClearWaypointIntercept(pEnv->pShipEnv, nShipIdx); /* 0x85470 */
    UiLineList_Remove(pEnv->pLines, 0); /* 0x8547C */

    /* 0x85490 reloads the ship index with LDRB from 0x187054, and UiPanel_InsertLine masks its
     * slot-0 argument with AND R3,R3,#0xFF (0x5A2F4) and byte-loads slots 1..3 from the stack.
     * Every colour slot this call can write is therefore a byte; the cast is the original's LDRB. */
    UiLineList_Insert(pEnv->pLines, 0, 11, (u32)(u8)nShipIdx, 0, 0, 0); /* 0x854A0 */

    if (pEnv->pTime != NULL) {
        pEnv->pTime->bPaused = 1; /* 0x854B0: GameTime_Pause is STRB #1 at 0x15961C */
    }
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

void BaseInfoScreen_TogglePanelMode(const BaseInfoToggleEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pbModeFlag == NULL) {
        return;
    }

    /* 0x55764 ANDS R3,R1,#0xFF. Every flag test below answers THIS value. */
    const bool bWasZero = ((*pEnv->pbModeFlag & 0xFFu) == 0);

    if (bWasZero) {
        *pEnv->pbModeFlag = 1; /* 0x55778 STRBEQ */
    } else {
        *pEnv->pbModeFlag = 0; /* 0x5576C STRBNE */
    }

    /* 0x55770 MOVNE #0x10 then 0x55784 MOVEQ #0: the cursor follows the flag's ORIGINAL value, so
     * a flag that was zero now holds 1 while the cursor written is 0. */
    const i32 nCursor = bWasZero ? 0 : PUFO_BASEINFO_CURSOR_ON;

    if (pEnv->pPanelB != NULL) {
        pEnv->pPanelB->nUnk3028 = nCursor; /* 0x55788, unk_158824 */
    }
    if (pEnv->pPanelA != NULL) {
        pEnv->pPanelA->nUnk3028 = nCursor; /* 0x55798, unk_158820 */
    }
    if (pEnv->pPanelC != NULL) {
        pEnv->pPanelC->nUnk3028 = nCursor; /* 0x557A4, unk_158838 */
    }
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
        /* 0x85560 reloads the index with LDRB, so the stored colour slot is its low byte --
         * UiPanel_InsertLine masks slot 0 to a byte (0x5A2F4). */
        UiLineList_Insert(pEnv->pLines, 0, 10, (u32)(u8)nItemIdx, 0u, 0u, 0u); /* 0x8556C */
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

/* --- SoldierScreen_Draw @ 0x65290 --- */

i32 SoldierScreen_Draw(const SoldierDrawEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pnScreenId == NULL || pEnv->pbOpen == NULL) {
        return 0;
    }

    if (*pEnv->pnScreenId == PUFO_SOL_SCREEN_TEXT_INPUT) {   /* 0x652A4 */
        if (pEnv->pfnTextInput == NULL) {
            return 0;
        }
        return pEnv->pfnTextInput(pEnv->pnCursor != NULL ? pEnv->pnCursor[0] : 0,
                                  pEnv->pTextInputUser);     /* 0x652B0 Ui_TextInputDialog */
    }

    if (*pEnv->pbOpen == 0) {                                /* 0x652CC */
        return 0;
    }

    if (pEnv->pPad != NULL) {
        PadImage_Blit(pEnv->pPad);                           /* 0x652D4 */
    }
    if (pEnv->pPanelNames != NULL) {
        UiPanel_DrawText(pEnv->pPanelNames);                 /* 0x652E0 */
    }
    if (pEnv->pPanelCraft != NULL) {
        UiPanel_DrawText(pEnv->pPanelCraft);                 /* 0x652EC */
    }

    SoftSurf_BlitRectProxy(0, 69, 23, 97, 1,
                           PUFO_SOL_BEVEL_LIGHT, PUFO_SOL_BEVEL_LIGHT, PUFO_SOL_BEVEL_LIGHT);   /* 0x65318 */
    SoftSurf_BlitRectProxy(0, 69, 44, 97, 1,
                           PUFO_SOL_BEVEL_DARK, PUFO_SOL_BEVEL_DARK, PUFO_SOL_BEVEL_DARK);      /* 0x65340 */
    SoftSurf_BlitRectProxy(0, 69, 24, 1, 21,
                           PUFO_SOL_BEVEL_DARK, PUFO_SOL_BEVEL_DARK, PUFO_SOL_BEVEL_DARK);      /* 0x65368 */
    SoftSurf_BlitRectProxy(0, 166, 23, 1, 22,
                           PUFO_SOL_BEVEL_LIGHT, PUFO_SOL_BEVEL_LIGHT, PUFO_SOL_BEVEL_LIGHT);   /* 0x65390 */

    /* The side rails: 24 steps, i runs 174..312 while the climbing half runs 5..166 in jumps of 7. */
    for (i32 k = 0, yUp = 5, i = 174; i < 318; ++k, yUp += 7, i += 6) {                          /* 0x6539C */
        const u8 nRise = (u8)(PUFO_SOL_RAMP_STEP * (k + PUFO_SOL_RAMP_BASE));                   /* 100..215 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_SIDE - PUFO_SOL_RAMP_STEP * k);                     /* 230..115 */

        SoftSurf_BlitRectProxy(0, 2, yUp, 1, 8, nRise, nRise, nRise);                           /* 0x653D8 */
        SoftSurf_BlitRectProxy(0, 2, i, 1, 6, nFall, nFall, nFall);                             /* 0x65408 */
        SoftSurf_BlitRectProxy(0, 235, yUp, 1, 8, nRise, nRise, nRise);                         /* 0x65430 */
        SoftSurf_BlitRectProxy(0, 235, i, 1, 6, nFall, nFall, nFall);                           /* 0x65458 */
    }

    /* The top and bottom rails: 24 steps of 5, the left half climbing and the right half falling. */
    for (i32 j = 0, k = 0; j < 120; j += PUFO_SOL_RAMP_STEP, ++k) {                             /* 0x6547C */
        const i32 nXLo = j + 1;
        const i32 nXHi = j + 119;
        const u8 nRise = (u8)(PUFO_SOL_RAMP_STEP * (k + PUFO_SOL_RAMP_BASE));                   /* 100..215 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_EDGE - PUFO_SOL_RAMP_STEP * k);                     /* 250..135 */

        SoftSurf_BlitRectProxy(0, nXLo, 5, 5, 1, nRise, nRise, nRise);                          /* 0x654B0 */
        SoftSurf_BlitRectProxy(0, nXLo, 315, 5, 1, nRise, nRise, nRise);                        /* 0x654D8 */
        SoftSurf_BlitRectProxy(0, nXHi, 5, 5, 1, nFall, nFall, nFall);                          /* 0x65508 */
        SoftSurf_BlitRectProxy(0, nXHi, 315, 5, 1, nFall, nFall, nFall);                        /* 0x65530 */
    }

    return 0;
}

/* --- BaseFacScreen_DrawPlacementOverlay @ 0x58e10 --- */

/* The ARM keeps one flat pointer per layer (grid + 0x174 + 144*base for the fac block, grid + 36*base
 * for buildDays) and adds small deltas to it, so a 2x2 at the last row reads on into the next layer.
 * Both stay inside the 1524-byte grid for every tap the in-bounds test can produce. */
static u8 FacOverlay_TileA(const u8 *pGrid, i32 nBase, i32 nRow, i32 nCol)
{
    return pGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase + 6 * nRow + nCol];
}

static u8 FacOverlay_Days(const u8 *pGrid, i32 nBase, i32 nRow, i32 nCol)
{
    return pGrid[PUFO_GRID_DAYS_STRIDE * nBase + 6 * nRow + nCol];
}

/* The four-side walk (0x592D0..0x5957C). A neighbour only counts when its buildDays byte is zero. */
static i32 FacOverlay_Score(const FacOverlayEnv *pEnv, i32 nSpan, i32 nTapX, i32 nTapY)
{
    const u8 *pGrid = pEnv->abFacilityGrid;
    i32 nBase = pEnv->nSelectedBaseIdx;
    i32 nLimit = 5 - (nSpan / PUFO_FAC_OVERLAY_SPAN);
    i32 nFoot;
    i32 nScore = 0;

    if (nSpan != 0) {
        nFoot = FacOverlay_TileA(pGrid, nBase, nTapY, nTapX)
              + FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX)
              + FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX + 1)
              + FacOverlay_TileA(pGrid, nBase, nTapY, nTapX + 1);
    } else {
        nFoot = FacOverlay_TileA(pGrid, nBase, nTapY, nTapX);
    }
    nFoot &= 0xFF;                                           /* 0x592CC AND #0xFF */
    if (nFoot != 0) {
        return 0;                                            /* the footprint is occupied */
    }

    if (nTapY > 0) {                                         /* 0x592C8 CMP R1 -- the ROW */
        if (FacOverlay_Days(pGrid, nBase, nTapY - 1, nTapX) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY - 1, nTapX);
        if (nSpan != 0 && FacOverlay_Days(pGrid, nBase, nTapY - 1, nTapX + 1) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY - 1, nTapX + 1);
    }
    if (nTapY < nLimit) {
        if (FacOverlay_Days(pGrid, nBase, nTapY + 1, nTapX) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX);
        if (nSpan != 0) {
            if (FacOverlay_Days(pGrid, nBase, nTapY + 2, nTapX) == 0)
                nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 2, nTapX);   /* v19[384] */
            if (FacOverlay_Days(pGrid, nBase, nTapY + 2, nTapX + 1) == 0)
                nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 2, nTapX + 1); /* v19[385] */
        }
    }
    if (nTapX > 0) {
        if (FacOverlay_Days(pGrid, nBase, nTapY, nTapX - 1) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY, nTapX - 1);
        if (nSpan != 0 && FacOverlay_Days(pGrid, nBase, nTapY + 1, nTapX - 1) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX - 1);
    }
    if (nTapX < nLimit) {
        if (FacOverlay_Days(pGrid, nBase, nTapY, nTapX + 1) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY, nTapX + 1);
        if (nSpan != 0) {
            if (FacOverlay_Days(pGrid, nBase, nTapY, nTapX + 2) == 0)
                nScore += FacOverlay_TileA(pGrid, nBase, nTapY, nTapX + 2);
            if (FacOverlay_Days(pGrid, nBase, nTapY + 1, nTapX + 2) == 0)
                nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX + 2);
        }
    }
    return nScore & 0xFF;                                    /* every accumulate is a STRB */
}

static void FacOverlay_Outline(const FacOverlayEnv *pEnv, i32 nSpan, i32 nTapX, i32 nTapY,
                               u8 nColor, bool bOk)
{
    i32 nX = pEnv->nOriginX + 32 * nTapX;
    i32 nY = pEnv->nOriginY + 32 * nTapY + 36;
    i32 nW = (nSpan != 0) ? 64 : 32;
    i32 nH = (nSpan != 0) ? 64 : 32;
    u8 nIdx = bOk ? 0 : nColor;
    u8 nLow = bOk ? nColor : 0;

    SoftSurf_BlitRectProxy(0, nX, nY, nW, 2, nIdx, 0, nLow);            /* top */
    SoftSurf_BlitRectProxy(0, nX, nY, 2, nH, nIdx, 0, nLow);            /* left */
    SoftSurf_BlitRectProxy(0, nX + nW - 1, nY, 2, nH, nIdx, 0, nLow);   /* right */
    SoftSurf_BlitRectProxy(0, nX, nY + nH - 1, nW, 2, nIdx, 0, nLow);   /* bottom */
}

i32 BaseFacScreen_DrawPlacementOverlay(const FacOverlayEnv *pEnv)
{
    i32 nSpan;
    i32 nTapX;
    i32 nTapY;
    i32 nColor;
    i32 nScore;
    i32 nKind;
    u8 *pGrid;

    if (pEnv == NULL || pEnv->abFacilityGrid == NULL || pEnv->pPanel == NULL ||
        pEnv->paFac == NULL || pEnv->pnFunds == NULL || pEnv->pnTapX == NULL ||
        pEnv->pnTapY == NULL || pEnv->pbPreviewActive == NULL || pEnv->pbPreviewOk == NULL ||
        pEnv->pbOverlayShown == NULL || pEnv->pbNewBaseMode == NULL) {
        return 0;
    }

    pGrid = pEnv->abFacilityGrid;

    nKind = UiPanel_GetLineAux(pEnv->pPanel, pEnv->nPanelSel);          /* 0x58E20 */
    if (nKind < 0 || nKind >= PUFO_BASEFAC_COUNT) {
        return 0;
    }

    nSpan = (pEnv->paFac[nKind].bFlag21 == PUFO_FAC_TYPE_HANGAR) ? PUFO_FAC_OVERLAY_SPAN : 0;
    nColor = (u8)(10 * ((i32)pEnv->nPhaseByte + 15));                   /* 0x5909C..0x590AC */

    nTapX = pEnv->nPendingX;                                            /* 0x58E58 */
    nTapY = pEnv->nPendingY;                                            /* 0x58E60 */
    *pEnv->pnTapX = nTapX;
    *pEnv->pnTapY = nTapY;

    if (*pEnv->pbPreviewActive != 0) {                                  /* 0x58E8C..0x58EC8 */
        nTapX = ((i32)pEnv->nCursorX - pEnv->nOriginX) / 32;
        nTapY = ((i32)pEnv->nCursorY - pEnv->nOriginY - 36) / 32;
        *pEnv->pnTapX = nTapX;
        *pEnv->pnTapY = nTapY;
    }

    /* The clamped outline for a 2x2 that would hang off the last row or column (0x58EDC..0x59168).
     * Always the invalid colour. */
    if (nTapX <= 5 && nTapY <= 5) {
        i32 nX = pEnv->nOriginX + 32 * nTapX;
        i32 nY = pEnv->nOriginY + 32 * nTapY + 36;

        if (nTapY == 5 && nSpan != 0) {
            if (nTapX == 5) {
                SoftSurf_BlitRectProxy(0, pEnv->nOriginX + 160, pEnv->nOriginY + 196, 32, 2,
                                       nColor, 0, 0);
                SoftSurf_BlitRectProxy(0, nX, nY, 2, 32, nColor, 0, 0);
            } else {
                SoftSurf_BlitRectProxy(0, nX, pEnv->nOriginY + 196, 64, 2, nColor, 0, 0);
                SoftSurf_BlitRectProxy(0, nX, nY, 2, 32, nColor, 0, 0);
                SoftSurf_BlitRectProxy(0, nX + 63, nY, 2, 32, nColor, 0, 0);
            }
        } else if (nTapX == 5 && nSpan != 0) {
            SoftSurf_BlitRectProxy(0, pEnv->nOriginX + 160, nY, 32, 2, nColor, 0, 0);
            SoftSurf_BlitRectProxy(0, nX, nY, 2, 64, nColor, 0, 0);
            SoftSurf_BlitRectProxy(0, nX, pEnv->nOriginY + 32 * nTapY + 99, 32, 2, nColor, 0, 0);
        }
    }

    nScore = 0;

    if (*pEnv->pbOverlayShown != 0 || *pEnv->pbPreviewActive != 0) {    /* 0x5917C */
        i32 nDx = (i32)pEnv->nCursorX - pEnv->nOriginX;
        i32 nDy = (i32)pEnv->nCursorY - pEnv->nOriginY;

        if (nDx > 0 && nDx < 192 - nSpan && nDy > 36 && nDy < 227 - nSpan) {
            *pEnv->pbOverlayShown = 1;                                  /* 0x59234 */
            if (nTapX >= 0 && nTapX <= 5 && nTapY >= 0 && nTapY <= 5) {
                nScore = FacOverlay_Score(pEnv, nSpan, nTapX, nTapY);
                if (*pEnv->pbNewBaseMode != 0) {
                    nScore = 1;                                         /* 0x597A0 */
                }
                FacOverlay_Outline(pEnv, nSpan, nTapX, nTapY, nColor, nScore != 0);
            }
        }

        if (nScore == 0) {                                              /* 0x599A0 */
            *pEnv->pbPreviewOk = 0;
            return 0;
        }
        if (*pEnv->pbPreviewOk == 0) {
            return 0;
        }
        if (nTapX != pEnv->nPendingX || nTapY != pEnv->nPendingY) {
            return 0;
        }
        if (*pEnv->pbPreviewActive == 0) {
            return 0;
        }

        {
            i32 nCost = (i32)pEnv->paFac[nKind].uCost;                 /* 0x59A1C */

            if (*pEnv->pnFunds - nCost < 0) {                          /* 0x59A24 */
                if (pEnv->pdwShortfall != NULL) {
                    *pEnv->pdwShortfall = 1;
                }
                if (pEnv->pnResidue != NULL) {
                    *pEnv->pnResidue = 0;
                }
                return 0;
            }

            (void)BaseFac_SetCellBuildState(pGrid, pEnv->nSelectedBaseIdx,
                                            pEnv->paFac[nKind].bFlag21, nTapX, (i8)nTapY, 0);
            *pEnv->pnFunds -= nCost;                                   /* 0x59AB4 */
        }

        if (*pEnv->pbNewBaseMode == 0) {
            return 1;
        }

        if (pEnv->pTextRecs != NULL && pEnv->pszLabel != NULL && pEnv->abKindTable != NULL) {
            (void)BaseFac_FormatCostLabel(pEnv->pszLabel, pEnv->cchLabel, pEnv->pTextRecs,
                                          pEnv->nPendingKind, pEnv->abKindTable);
        }
        if (pEnv->pCreateEnv != NULL) {
            (void)Base_CreateNamedEntry(pEnv->pCreateEnv, pEnv->nPlaceX, pEnv->nPlaceY,
                                        (i32)pEnv->nPendingKind, pEnv->pszLabel);
        }
        if (pEnv->paNewBaseCost != NULL && pEnv->nPendingKind >= 0x80 &&
            (i32)(pEnv->nPendingKind - 0x80) < PUFO_NEW_BASE_COST_COUNT) {
            *pEnv->pnFunds -= pEnv->paNewBaseCost[pEnv->nPendingKind - 0x80];
        }
        if (pEnv->pToggleEnv != NULL) {
            BaseFacScreen_Toggle(pEnv->pToggleEnv);
        }
        *pEnv->pbPreviewActive = 0;
        if (pEnv->pnScreenId != NULL) {
            *pEnv->pnScreenId = 1;
        }
        *pEnv->pbNewBaseMode = 0;
        *pEnv->pbPreviewOk = 0;
        return 1;
    }

    return 0;
}

/* --- BaseFacScreen_DrawBuildDialog @ 0x59b60 --- */

/* 0x59CEC and 0x5A168: MOVS, ADDMI #1, ASR#1. A negative width is nudged up by one before the
 * halving, which is what the ARM's arithmetic shift then does to it. */
static i32 BuildDialog_HalfWidth(i32 nWidth)
{
    if (nWidth < 0) {
        nWidth += 1;
    }
    return nWidth / 2;
}

/* The label copy loops (0x59BCC, 0x5A02C, 0x66580, 0x66650): LDRSB/STRB until the terminator, with
 * no bound on the destination. The port stops at the caller's buffer end. */
static void BaseUi_CopyLabel(char *pszDst, size_t cchDst, const char *pszSrc)
{
    size_t i = 0;

    if (pszSrc == NULL) {
        pszDst[0] = 0;
        return;
    }
    while (i + 1 < cchDst && pszSrc[i] != 0) {
        pszDst[i] = pszSrc[i];
        ++i;
    }
    pszDst[i] = 0;
}

/* The eight runtime-loaded labels may be absent in the port's env; the original always has bytes at
 * those addresses, possibly just a terminator. */
static void BuildDialog_DrawLabel(i32 x, i32 y, const char *psz)
{
    if (psz != NULL) {
        Text_DrawString(x, y, psz);
    }
}

/* 0x59BAC..0x59C1C and 0x5A078..0x5A0E4: the funds label, the funds value and a dollar sign,
 * drawn at (5, 20). */
static void BuildDialog_DrawFundsLine(const BuildDialogEnv *pEnv, char *psz)
{
    char szNum[16];

    BaseUi_CopyLabel(psz, PUFO_BUILD_LABEL_MAX, pEnv->pszFundsPrefix);
    snprintf(szNum, sizeof(szNum), "%d", *pEnv->pnFunds); /* j_itoa (0x9e9f0), base 10 */
    (void)pufo_strcat(psz, szNum, PUFO_BUILD_LABEL_MAX);
    (void)pufo_strcat(psz, "$", PUFO_BUILD_LABEL_MAX);    /* asc_AB1D8 @ 0xab1d8 */
    Text_DrawString(5, 20, psz);
}

i32 BaseFacScreen_DrawBuildDialog(const BuildDialogEnv *pEnv)
{
    char szLabel[PUFO_BUILD_LABEL_MAX]; /* psz @ sp+0x18 */
    const BaseFacFlag *pRec;
    i32 nKind;
    i32 nWidth;

    if (pEnv == NULL || pEnv->pbDialogOpen == NULL || *pEnv->pbDialogOpen == 0) {
        return 0; /* 0x59B6C: a closed dialog draws nothing at all */
    }
    if (pEnv->pnFunds == NULL) {
        return 0;
    }

    if (pEnv->pbPlaceArmed != NULL && *pEnv->pbPlaceArmed != 0) {
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 130, 130, 250, 20, 20, 90); /* 0x59BAC */
        BuildDialog_DrawFundsLine(pEnv, szLabel);

        (void)Ui_AnimExpandBlit(pEnv->pAnim, 0, 228, 236, 85, 4, 0x46, 0x4B, 0xC8, 0); /* 0x59C60 */

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 50, 255, 50, 0, 20, 0);                /* 0x59C80 */
        nKind = (pEnv->pPanel != NULL && pEnv->paFac != NULL)
                    ? UiPanel_GetLineAux(pEnv->pPanel, pEnv->nPanelSel)                /* 0x59CB4 */
                    : -1;
        pRec = (nKind >= 0 && nKind < PUFO_BASEFAC_COUNT) ? &pEnv->paFac[nKind] : NULL;

        if (pRec != NULL) {
            BaseUi_CopyLabel(szLabel, PUFO_BUILD_LABEL_MAX, pRec->szName);
            nWidth = Text_MeasureWidth(szLabel);                                       /* 0x59CEC */
            Text_DrawString(120 - BuildDialog_HalfWidth(nWidth), 236, pRec->szName);  /* 0x59D0C */
        }

        BuildDialog_DrawLabel(6, 257, pEnv->pszRowCost);    /* 0x59D2C, unk_B7B47 */
        BuildDialog_DrawLabel(6, 275, pEnv->pszRowBuild);   /* 0x59D4C, unk_B7C14 */
        BuildDialog_DrawLabel(6, 293, pEnv->pszRowNewBase); /* 0x59D6C, unk_B7CE1 */

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 50, 150, 255, 0, 0, 20);               /* 0x59D8C */
        if (pRec != NULL) {
            snprintf(szLabel, sizeof(szLabel), "%d", (i32)pRec->uCost);               /* 0x59DB4 */
            Text_DrawString(185, 257, szLabel);                                        /* 0x59DCC */
        }
        Text_DrawString(185, 293, "50000"); /* 0x59DE4, a50000 @ 0xab260: the new-base row */

        if (pRec != NULL) {
            snprintf(szLabel, sizeof(szLabel), "%d",
                     (i32)FacBuildSpec_GetDays(pRec->bFlag21));                       /* 0x59E20 */
            (void)pufo_strcat(szLabel, " ", PUFO_BUILD_LABEL_MAX);                    /* 0x59E2C */
            (void)pufo_strcat(szLabel, pEnv->pszDaysSuffix, PUFO_BUILD_LABEL_MAX);    /* 0x59E40 */
        }
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 50, 150, 255, 0, 0, 20);               /* 0x59E60 */
        Text_DrawString(185, 275, szLabel);                                           /* 0x59E78 */

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);              /* 0x59EA4 */
        BuildDialog_DrawLabel(6, 4, pEnv->pszTitleArmed);                             /* 0x59EC0 */
        (void)UiControl_BlitActivePad(pEnv->pCtl, pEnv->pfnCtlBlit,
                                      pEnv->pfnCtlBlitEx, pEnv->pCtlUser);            /* 0x59ECC */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30);              /* 0x59EF4 */
        (void)BaseFacScreen_DrawPlacementOverlay(pEnv->pOverlayEnv);                  /* 0x59EF8 */
    } else {
        if (pEnv->pbOverlayShown != NULL) {
            *pEnv->pbOverlayShown = 0;                                               /* 0x59FAC */
        }
        (void)Ui_AnimExpandBlit(pEnv->pAnim, 0, 228, 236, 85, 4, 0x46, 0x4B, 0xC8, 0); /* 0x59FE0 */
        if (pEnv->pPanel != NULL) {
            UiPanel_DrawText(pEnv->pPanel);                                          /* 0x59FEC */
        }
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);             /* 0x5A00C */
        BaseUi_CopyLabel(szLabel, PUFO_BUILD_LABEL_MAX, pEnv->pszTitleList);
        Text_DrawString(6, 4, szLabel);                                              /* 0x5A050 */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 130, 130, 250, 20, 20, 90);           /* 0x5A078 */
        BuildDialog_DrawFundsLine(pEnv, szLabel);
        (void)UiControl_BlitActivePad(pEnv->pCtl, pEnv->pfnCtlBlit,
                                      pEnv->pfnCtlBlitEx, pEnv->pCtlUser);            /* 0x5A0F0 */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30);             /* 0x5A118 */
    }

    /* 0x5A138: the tail both branches share, reached only when a purchase was refused. */
    if (pEnv->pdwShortfall != NULL && *pEnv->pdwShortfall == 1
        && pEnv->pnResidue != NULL && *pEnv->pnResidue == 0) {
        (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, 20, 130, 200, 70, 1);         /* 0x5A138 */
        if (pEnv->pszShortfall != NULL) {
            nWidth = Text_MeasureWidth(pEnv->pszShortfall);                          /* 0x5A168 */
            Text_DrawString(120 - BuildDialog_HalfWidth(nWidth), 158,
                            pEnv->pszShortfall);                                     /* 0x5A188 */
        }
    }

    return 1;
}

/* --- Soldier_FormatDisplayName @ 0x6654c --- */

/* aTextRecs @ 0xB38D0 record indices. 93 is the "no craft" label the 0xFF sentinel gets and 140 the
 * label the 0xFE sentinel gets; 137, 138 and 139 are the three craft types this function names.
 * Ship_FormatDisplayName (0x666cc) uses the same three plus 418 and 419 for the two types that are
 * named on the craft side only. */
#define PUFO_SOLDIER_CRAFT_REC_BASE 137
#define PUFO_SOLDIER_NO_CRAFT_REC   93
#define PUFO_SOLDIER_UNASSIGNED_REC 140

char *Soldier_FormatDisplayName(const Soldier *pSol, Ship *const *apCraft, const u8 *pTextRecs,
                                char *pszOut, size_t cchOut)
{
    char szNum[16];
    Ship *pShip;
    u8 nCraft;

    if (pszOut == NULL || cchOut == 0) {
        return pszOut;
    }
    pszOut[0] = 0;
    if (pSol == NULL) {
        return pszOut;
    }
    nCraft = pSol->bUnk183;

    if (nCraft == 0xFF) {                                                          /* 0x66560 */
        if (pTextRecs != NULL) {
            BaseUi_CopyLabel(pszOut, cchOut,
                             (const char *)(pTextRecs + PUFO_TEXT_REC_STRIDE
                                            * PUFO_SOLDIER_NO_CRAFT_REC));
        }
        return pszOut;                                                             /* 0x66590 */
    }

    if (nCraft < 0xFE) {                                                           /* 0x665AC */
        if (apCraft != NULL && pTextRecs != NULL) {
            pShip = apCraft[nCraft];
            if (pShip != NULL && pShip->bCraftType < 3) {                          /* 0x665CC */
                BaseUi_CopyLabel(pszOut, cchOut,
                                 (const char *)(pTextRecs + PUFO_TEXT_REC_STRIDE
                                                * (PUFO_SOLDIER_CRAFT_REC_BASE
                                                   + pShip->bCraftType)));
                snprintf(szNum, sizeof(szNum), "%d", (i32)pShip->bType);           /* 0x66664 */
                (void)pufo_strcat(pszOut, szNum, cchOut);                          /* 0x66688 */
            }
        }
    }

    if (nCraft == 0xFE) {                                                          /* 0x66694 */
        if (pTextRecs != NULL) {
            BaseUi_CopyLabel(pszOut, cchOut,
                             (const char *)(pTextRecs + PUFO_TEXT_REC_STRIDE
                                            * PUFO_SOLDIER_UNASSIGNED_REC));
        }
    }
    return pszOut;                                                                 /* 0x666C8 */
}

/* ------------------------------------------------- the options screen's audio tab */

/* stru_AB750 @ 0xAB750: seven identical UiControlDesc records, the options screen's list rows.
 * stru_AB830 @ 0xAB830: twelve identical records, the volume radio buttons. Both tables are uniform
 * repeats of one record (dumped by tools/gen_geoiinit_tables.py), so the port keeps one each. */
static const UiControlDesc g_aOptionsListDesc = { 1, 229, "firebtn", "cfirebtn", "", 0 };
static const UiControlDesc g_aOptionsVolDesc  = { 1, 229, "initg", "cinitg", "cinitg", 0 };

/* The level the two appliers take, from Sound_ApplyMusicVolume @ 0x9E470/0x9E488:
 * 4 * (muted ? 0 : 12 * step + 4) - 1. Muted yields -1, which is the original's arithmetic. */
static i32 Options_MapLevel(i32 nStep, u8 bMuted)
{
    i32 v = (bMuted != 0) ? 0 : 12 * nStep + 4;

    return 4 * v - 1;
}

/* ORIGINAL BUG (0x6B93C / 0x6B984), not reproduced: the original indexes the 0x159E44 / 0x159E5C
 * arrays with the stored step and tests nothing, so a step outside 0..5 reads past the six pointers
 * and writes 2 into whatever it lands on. The port picks button 0 instead. */
static i32 Options_ClampStep(i32 nStep)
{
    return (nStep < 0 || nStep >= PUFO_OPTIONS_VOL_COUNT) ? 0 : nStep;
}

static void Options_SelectRadio(UiControl *pBtn)
{
    if (pBtn != NULL) {
        pBtn->nState = PUFO_CONTROL_MAX_STATE; /* 2: the selected state of a radio group */
    }
}

static void Options_SetClick(UiControl *pCtl, void (*pfn)(void))
{
    if (pCtl != NULL) {
        pCtl->pfnClick = pfn;
    }
}

/* Centre one list row on the 240-pixel field and drop it at nY. */
static void Options_CentreRow(UiControl *pCtl, i32 nY)
{
    if (pCtl != NULL) {
        pCtl->x = (i16)(120 - (pCtl->uWidth >> 1)); /* 0x6B718 / 0x6B514 */
        pCtl->y = (i16)nY;                          /* 0x6B73C / 0x6B538 */
    }
}

i32 OptionsScreen_SetMusicVol(OptionsVolumeEnv *pEnv, i32 nStep)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pnMusicVol == NULL || pEnv->pbMusicOff == NULL) {
        return 0;
    }

    *pEnv->pnMusicVol = nStep;                                            /* 0x6BA28 / 0x6BAD0 */
    OptionsScreen_ClearMusicButtons(pEnv->pCtl->apMusic);                 /* 0x6BA2C */
    Options_SelectRadio(pEnv->pCtl->apMusic[Options_ClampStep(nStep)]);   /* 0x6BA50 */
    *pEnv->pbMusicOff = (nStep == 0) ? 1 : 0;                             /* 0x6BA5C / 0x6BAA8 */
    Sound_ApplyMusicVolume(Options_MapLevel(*pEnv->pnMusicVol, *pEnv->pbMusicOff),
                           *pEnv->pbMusicOff);                             /* 0x9e454, the tail call */
    return 0;
}

i32 OptionsScreen_SetSfxVol(OptionsVolumeEnv *pEnv, i32 nStep)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pnSfxVol == NULL || pEnv->pbSfxOff == NULL) {
        return 0;
    }

    OptionsScreen_ClearSfxButtons(pEnv->pCtl->apSfx);                     /* 0x6BC20 */
    *pEnv->pnSfxVol = nStep;                                              /* 0x6BC38 */
    Options_SelectRadio(pEnv->pCtl->apSfx[Options_ClampStep(nStep)]);     /* 0x6BC48 */
    *pEnv->pbSfxOff = (nStep == 0) ? 1 : 0;                               /* 0x6BC54 / 0x6BCB8 */
    Sound_ApplySfxVolume(Options_MapLevel(*pEnv->pnSfxVol, *pEnv->pbSfxOff),
                         *pEnv->pbSfxOff);                                /* 0x6BC58 */
    Sound_Play(17);                                                       /* 0x6BC64: hear the new level */
    return 0;
}

i32 OptionsScreen_SetMusicVol0(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 0); /* 0x6ba14: step 0 is the mute step */
}

i32 OptionsScreen_SetMusicVol1(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 1); /* 0x6ba70 */
}

i32 OptionsScreen_SetMusicVol2(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 2); /* 0x6babc */
}

i32 OptionsScreen_SetMusicVol3(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 3); /* 0x6bb18 */
}

i32 OptionsScreen_SetMusicVol4(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 4); /* 0x6bb74 */
}

i32 OptionsScreen_SetMusicVol5(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 5); /* 0x6bbd0 */
}

i32 OptionsScreen_SetSfxVol0(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 0); /* 0x6bc1c: step 0 is the mute step */
}

i32 OptionsScreen_SetSfxVol1(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 1); /* 0x6bc80 */
}

i32 OptionsScreen_SetSfxVol2(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 2); /* 0x6bce4 */
}

i32 OptionsScreen_SetSfxVol3(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 3); /* 0x6bd48 */
}

i32 OptionsScreen_SetSfxVol4(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 4); /* 0x6bdac */
}

i32 OptionsScreen_SetSfxVol5(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 5); /* 0x6be10 */
}

i32 OptionsScreen_RefreshVolumeHighlight(OptionsVolumeEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pnMusicVol == NULL || pEnv->pbMusicOff == NULL
        || pEnv->pnSfxVol == NULL || pEnv->pbSfxOff == NULL) {
        return 0;
    }

    if (*pEnv->pbMusicOff != 0) {
        *pEnv->pnMusicVol = 0;                                            /* 0x6B91C */
    }
    Options_SelectRadio(pEnv->pCtl->apMusic[Options_ClampStep(*pEnv->pnMusicVol)]); /* 0x6B944 */

    if (*pEnv->pbSfxOff != 0) {
        *pEnv->pnSfxVol = 0;                                              /* 0x6B968 */
    }
    Options_SelectRadio(pEnv->pCtl->apSfx[Options_ClampStep(*pEnv->pnSfxVol)]); /* 0x6B96C / 0x6B984 */
    return 0;
}

UiControl *OptionsScreen_LayoutButtons(OptionsTabEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pScratch == NULL) {
        return NULL;
    }

    pEnv->pScratch->nLineAux145 = 0;                        /* 0x6B6D0: the audio tab */
    Options_SetClick(pEnv->pCtl->apList[0], pEnv->pfnBindSaveSlots); /* 0x6B6DC */
    Options_SetClick(pEnv->pCtl->apList[1], pEnv->pfnBindLoadSlots); /* 0x6B6E8 */
    Options_SetClick(pEnv->pCtl->apList[2], pEnv->pfnSetTab3);       /* 0x6B6F4 */
    Options_SetClick(pEnv->pCtl->apList[3], pEnv->pfnSetTab4);       /* 0x6B700 */

    for (i32 i = 0; i < 4; ++i) {                           /* 0x6B704..0x6B744 */
        Options_CentreRow(pEnv->pCtl->apList[i], 90 + 25 * i);
    }

    if (pEnv->pSaveCtl != NULL) {
        pEnv->pSaveCtl->x = (i16)(120 - (pEnv->pSaveCtl->uWidth >> 1)); /* 0x6B75C */
        pEnv->pSaveCtl->y = 198;                                        /* 0x6B784 */
    }
    if (pEnv->pBackCtl != NULL) {
        u16 w = (pEnv->pSaveCtl != NULL) ? pEnv->pSaveCtl->uWidth : 0;

        /* ORIGINAL BUG (0x6B764), reproduced: the width read for the return hotspot comes from
         * *unk_186BB4 again (R1 is reloaded from the same R0), while the store at 0x6B778 lands on
         * *unk_186BB0. The two hotspots share a pad in practice, so the game does not show it. */
        pEnv->pBackCtl->x = (i16)(120 - (w >> 1));
    }
    return pEnv->pSaveCtl;
}

/* The save and load tabs are the same seven rows with a different tab byte and handler set. */
static void Options_BindSlotRows(OptionsTabEnv *pEnv, u8 nTab, void (*apfnSlot[])(void))
{
    pEnv->pScratch->nLineAux145 = nTab;                     /* 0x6B4A8 (save) / 0x6B594 (load) */

    for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {
        Options_SetClick(pEnv->pCtl->apList[i], apfnSlot[i]);        /* 0x6B4B4..0x6B4FC */
        Options_CentreRow(pEnv->pCtl->apList[i], 40 + 22 * i);       /* 0x6B514 / 0x6B538 */
    }

    if (pEnv->pSaveCtl != NULL) {
        pEnv->pSaveCtl->y = 215;                                     /* 0x6B550 / 0x6B63C */
    }
    if (pEnv->pfnFormatSlots != NULL) {
        pEnv->pfnFormatSlots(pEnv->pUser);                           /* 0x6B558 / 0x6B644 */
    }
}

void OptionsScreen_BindSaveSlots(OptionsTabEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pScratch == NULL) {
        return;
    }
    Options_BindSlotRows(pEnv, 1, pEnv->apfnSaveSlot); /* 0x6b49c */
}

void OptionsScreen_BindLoadSlots(OptionsTabEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pScratch == NULL) {
        return;
    }
    Options_BindSlotRows(pEnv, 2, pEnv->apfnLoadSlot); /* 0x6b588 */
}

void OptionsScreen_Init(OptionsInitEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pbControlsExist == NULL) {
        return;
    }

    if (*pEnv->pbControlsExist != 0) {                        /* 0x6BE84 takes the teardown branch */
        *pEnv->pbControlsExist = 0;                           /* 0x6C0FC */

        for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {   /* 0x6C108..0x6C130 */
            UiControl_FreePads(pEnv->pCtl->apList[i]);        /* 0x6C118; 0x6C120 deletes, caller-owned here */
        }
        for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {    /* 0x6C134..0x6C15C, the twelve buttons */
            UiControl_FreePads(pEnv->pCtl->apMusic[i]);
            UiControl_FreePads(pEnv->pCtl->apSfx[i]);
        }
        return;
    }

    *pEnv->pbControlsExist = 1;                               /* 0x6BE90 */

    for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {       /* 0x6BEA0..0x6BED0 */
        if (pEnv->pCtl->apList[i] != NULL) {
            UiControl_ctor(pEnv->pCtl->apList[i]);
        }
    }
    for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {       /* 0x6BED4..0x6BEF0 */
        if (pEnv->pCtl->apList[i] != NULL) {
            UiControl_Setup(pEnv->pCtl->apList[i], 0, 0, &g_aOptionsListDesc,
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }

    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {        /* 0x6BF34..0x6BF70, music then sfx */
        if (pEnv->pCtl->apMusic[i] != NULL) {
            UiControl_ctor(pEnv->pCtl->apMusic[i]);
        }
    }
    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        if (pEnv->pCtl->apSfx[i] != NULL) {
            UiControl_ctor(pEnv->pCtl->apSfx[i]);
        }
    }

    Options_SetClick(pEnv->pSaveCtl, pEnv->pfnSaveAudio);     /* 0x6BF1C */
    Options_SetClick(pEnv->pBackCtl, pEnv->pfnReturnToTitle); /* 0x6BF2C */

    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {        /* 0x6BF74..0x6BF90 */
        if (pEnv->pCtl->apMusic[i] != NULL) {
            UiControl_Setup(pEnv->pCtl->apMusic[i], 0, 0, &g_aOptionsVolDesc,
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }
    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        if (pEnv->pCtl->apSfx[i] != NULL) {
            UiControl_Setup(pEnv->pCtl->apSfx[i], 0, 0, &g_aOptionsVolDesc,
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }

    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        if (pEnv->pCtl->apMusic[i] != NULL) {
            pEnv->pCtl->apMusic[i]->y = 107;                  /* 0x6BFBC */
            pEnv->pCtl->apMusic[i]->x = (i16)(46 + 25 * i);   /* 0x6BFDC */
        }
        if (pEnv->pCtl->apSfx[i] != NULL) {
            pEnv->pCtl->apSfx[i]->y = 147;                    /* 0x6BFF8 */
            pEnv->pCtl->apSfx[i]->x = (i16)(46 + 25 * i);     /* 0x6C018: 25 * (i + 6) - 104 */
        }
    }

    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {        /* 0x6C02C..0x6C0B0 */
        Options_SetClick(pEnv->pCtl->apMusic[i], pEnv->apfnMusicVol[i]);
        Options_SetClick(pEnv->pCtl->apSfx[i], pEnv->apfnSfxVol[i]);
    }

    (void)OptionsScreen_RefreshVolumeHighlight(pEnv->pVolEnv); /* 0x6C0B4 */
    (void)OptionsScreen_LayoutButtons(pEnv->pTabEnv);          /* 0x6C0B8 */
}

/* ------------------------------------------------- the options screen's save and load slots */

i32 SaveGame_Slot_CloseUI(SaveSlotEnv *pEnv, i32 nSlot)
{
    if (pEnv == NULL || pEnv->pDat == NULL || pEnv->pIn == NULL || pEnv->pnListMode == NULL
        || pEnv->pScratch == NULL) {
        return 0;
    }

    *pEnv->pnListMode = PUFO_MAINMENU_MODE_SAVING;              /* 0x6AF90: no rows while writing */
    pEnv->pIn->nSlot = nSlot;                                   /* the original's second argument */
    (void)SaveGame(pEnv->pDat, pEnv->pIn);                      /* 0x6AF98 */
    pEnv->pScratch->nLineAux145 = 0;                            /* 0x6AFA4 */
    *pEnv->pnListMode = PUFO_MAINMENU_MODE_SLOTCLICK;           /* 0x6AFAC */

    if (pEnv->pfnCloseUi != NULL) {
        pEnv->pfnCloseUi(pEnv->pCloseUser);                     /* 0x6AFB8, the tail call */
    }
    return 0;
}

i32 LoadGame_Slot_InitOptions(SaveSlotEnv *pEnv, i32 nSlot)
{
    i32 nRet;

    if (pEnv == NULL || pEnv->pDat == NULL || pEnv->pLoadEnv == NULL || pEnv->pnListMode == NULL
        || pEnv->pScratch == NULL) {
        return 0;
    }

    pEnv->pScratch->nLineAux145 = 0;                            /* 0x6B1A4 */
    if (pEnv->pnGeoscapeScreen != NULL) {
        *pEnv->pnGeoscapeScreen = 0;                            /* 0x6B1B0 */
    }
    *pEnv->pnListMode = PUFO_MAINMENU_MODE_LOADING;             /* 0x6B1B8 */
    if (pEnv->pbControlsExist != NULL) {
        *pEnv->pbControlsExist = 1;                             /* 0x6B1C4 */
    }
    OptionsScreen_Init(pEnv->pOptionsInit);                     /* 0x6B1C8, so this is the teardown */

    pEnv->pLoadEnv->nSlot = nSlot;                              /* the original's second argument */
    nRet = LoadGame(pEnv->pDat, pEnv->pLoadEnv);               /* 0x6B1D4 */

    if (((u8)nRet) == 0) {                                      /* 0x6B1DC: the test is on the low byte */
        *pEnv->pnListMode = PUFO_MAINMENU_MODE_SLOTCLICK;       /* 0x6B1E0 */
        return 1;
    }
    return nRet;
}

void SaveGame_Slot0_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 0); /* 0x6af80 */
}

void SaveGame_Slot1_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 1); /* 0x6afcc */
}

void SaveGame_Slot2_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 2); /* 0x6b018 */
}

void SaveGame_Slot3_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 3); /* 0x6b064 */
}

void SaveGame_Slot4_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 4); /* 0x6b0b0 */
}

void SaveGame_Slot5_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 5); /* 0x6b0fc */
}

void SaveGame_Slot6_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 6); /* 0x6b148 */
}

i32 LoadGame_Slot0_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 0); /* 0x6b194 */
}

i32 LoadGame_Slot1_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 1); /* 0x6b1fc */
}

i32 LoadGame_Slot2_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 2); /* 0x6b264 */
}

i32 LoadGame_Slot3_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 3); /* 0x6b2cc */
}

i32 LoadGame_Slot4_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 4); /* 0x6b334 */
}

i32 LoadGame_Slot5_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 5); /* 0x6b39c */
}

i32 LoadGame_Slot6_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 6); /* 0x6b404 */
}

/* The two scratch clears the three openers share. Both are strcpy loops reading from 0x10DA38,
 * which is all zero in the image, so the only byte that lands is the terminator. */
static void ScreenOpen_ClearScratch(char *psz, size_t cch)
{
    if (psz != NULL && cch > 0) {
        psz[0] = 0;
    }
}

/* The research "active" walk. The bound is the byte at byte_160BBC + 144 and it is re-read only
 * after a store (0x6173C / 0x84B9C / 0x85234 are MOVEQ / LDRBEQ), and the loop is a do/while, so a
 * non-zero count always examines entry 0. Returns the index reached, which Ui_OpenBaseInfo and
 * Geoscape_PrepScreenOpen spill into dword_186B08. */
static i32 Research_PromotePending(u8 *abActive, const u8 *pbCount)
{
    if (abActive == NULL || pbCount == NULL) {
        return 0;
    }

    i32 nCount = (i32)(*pbCount & 0xFFu);
    if (nCount == 0) {
        return 0; /* 0x61718 / 0x84B78 / 0x85214: CMP #0 / BLS out */
    }

    i32 i = 0;
    for (;;) {
        if ((abActive[i] & 0xFFu) == 1) {         /* 0x61724 CMP R3,#1 */
            abActive[i] = 2;                      /* 0x6172C STRBEQ */
            nCount = (i32)(*pbCount & 0xFFu);     /* 0x6173C LDRBEQ, only after a store */
        }
        ++i;                                      /* 0x61720 ADD R1,R1,#1 */
        /* 0x61748 CMP R1,R3 / BLT back edge; the second test is the port's bound. */
        if (i >= nCount || i >= PUFO_RESEARCH_TOPIC_COUNT) {
            break;
        }
    }
    return i;
}

/* The prologue all three run, in the originals' order. */
static void ScreenOpen_Prologue(const ScreenResetOpenEnv *pEnv)
{
    ScreenOpen_ClearScratch(pEnv->pszMsg, pEnv->cchMsg);
    ScreenOpen_ClearScratch(pEnv->pszScratch, pEnv->cchScratch);
    (void)Research_PromotePending(pEnv->abResearchActive, pEnv->pbResearchCount);
    if (pEnv->pdwShortfall != NULL) {
        *pEnv->pdwShortfall = 0; /* 0x61754 / 0x84BB0 / 0x8524C, a dword store into byte_1870C8 */
    }
    GameTime_Set5Sec(pEnv->pWorld, pEnv->apSpeedCtl); /* 0x61760 / 0x84BC4 / 0x85260 */
}

/* ResearchScreen_Open @ 0x616ac. Sample first, id 8 written before the Init tail call. */
void ResearchScreen_Open(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->init.pfnPlaySample != NULL) {
        pEnv->init.pfnPlaySample(17, 4, pEnv->init.pUser); /* 0x616B4 */
    }
    ScreenOpen_Prologue(pEnv);
    if (pEnv->pState != NULL) {
        pEnv->pState->nGeoscapeScreen = 8; /* 0x6176C */
    }
    if (pEnv->init.pfnInit != NULL) {
        pEnv->init.pfnInit(pEnv->init.pSlot, pEnv->init.pUser); /* 0x61774 -> ResearchScreen_Init */
    }
}

/* Ui_OpenBaseInfo @ 0x84AF8. Same shape, id 16, and the Init receives the root at unk_1872E0. */
void Ui_OpenBaseInfo(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->init.pfnPlaySample != NULL) {
        pEnv->init.pfnPlaySample(17, 4, pEnv->init.pUser); /* 0x84AFC */
    }
    ScreenOpen_Prologue(pEnv);
    if (pEnv->pState != NULL) {
        pEnv->pState->nGeoscapeScreen = 16; /* 0x84BD8 */
    }
    if (pEnv->init.pfnInit != NULL) {
        pEnv->init.pfnInit(pEnv->init.pSlot, pEnv->init.pUser); /* 0x84BE8 -> Ui_InitBaseInfo */
    }
}

/* Geoscape_PrepScreenOpen @ 0x85194. No sample and no screen id: it un-pauses the clock, runs the
 * prologue, drops line 0 of the root message list, and re-pauses when that line's flag lands in
 * (30, 50). */
/* The tail Geoscape_PrepScreenOpen and Geoscape_PrepScreenOpenSfx share: drop message line 0, then
 * pause the clock when the line that moved into slot 0 has a flag above 30 AND below 50. The two
 * reads are separate calls in the original (0x85270 / 0x85284 and 0x85398 / 0x853AC). */
static void ScreenOpen_DropLine0AndPause(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv->pLines == NULL) {
        return;
    }
    UiLineList_Remove(pEnv->pLines, 0); /* 0x8525C / 0x85384 */
    if (UiLineList_GetFlag(pEnv->pLines, 0) > 30) { /* 0x85270 / 0x85398, CMP #0x1E / BLE out */
        if (UiLineList_GetFlag(pEnv->pLines, 0) < 50) { /* the flag read a second time */
            if (pEnv->pTime != NULL) {
                pEnv->pTime->bPaused = 1; /* GameTime_Pause @ 0x5d6ac */
            }
        }
    }
}

void Geoscape_PrepScreenOpen(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pTime != NULL) {
        pEnv->pTime->bPaused = 0; /* 0x8519C STRB at 0x15961C */
    }
    ScreenOpen_Prologue(pEnv);
    ScreenOpen_DropLine0AndPause(pEnv);
}

/* Geoscape_PrepScreenOpenSfx @ 0x852b8. The same body with a sample inserted after the pause clear
 * and with GameTime_Set5Sec dropped: the only one of the six that leaves the tick interval alone. */
void Geoscape_PrepScreenOpenSfx(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pTime != NULL) {
        pEnv->pTime->bPaused = 0; /* 0x852C8 */
    }
    if (pEnv->init.pfnPlaySample != NULL) {
        pEnv->init.pfnPlaySample(17, 4, pEnv->init.pUser); /* 0x852D0 */
    }
    ScreenOpen_ClearScratch(pEnv->pszMsg, pEnv->cchMsg);
    ScreenOpen_ClearScratch(pEnv->pszScratch, pEnv->cchScratch);
    (void)Research_PromotePending(pEnv->abResearchActive, pEnv->pbResearchCount);
    if (pEnv->pdwShortfall != NULL) {
        *pEnv->pdwShortfall = 0; /* 0x85374 */
    }
    /* No GameTime_Set5Sec here: 0x8537C falls straight through to the line removal. */
    ScreenOpen_DropLine0AndPause(pEnv);
}

/* Ui_OpenBaseInfoAlt @ 0x84d18. Sets the ufopaedia reselect flag, runs the whole
 * Geoscape_PrepScreenOpen, and only then runs the prologue a second time. */
void Ui_OpenBaseInfoAlt(const ScreenResetOpen2Env *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pbUfopaediaReselect != NULL) {
        *pEnv->pbUfopaediaReselect = 1; /* 0x84D1C */
    }
    Geoscape_PrepScreenOpen(&pEnv->base); /* 0x84D24 */
    ScreenOpen_Prologue(&pEnv->base);
    if (pEnv->base.pState != NULL) {
        pEnv->base.pState->nGeoscapeScreen = 16; /* 0x84DE4 */
    }
    if (pEnv->base.init.pfnInit != NULL) {
        pEnv->base.init.pfnInit(pEnv->base.init.pSlot, pEnv->base.init.pUser); /* 0x84DF4 */
    }
}

/* Ui_OpenResearch @ 0x84bf8. The prologue without the tick, then the full prep, then the tick, then
 * the nav rebuild and the parked control. */
void Ui_OpenResearch(const ScreenResetOpen2Env *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    const ScreenResetOpenEnv *pBase = &pEnv->base;

    ScreenOpen_ClearScratch(pBase->pszMsg, pBase->cchMsg);
    ScreenOpen_ClearScratch(pBase->pszScratch, pBase->cchScratch);
    (void)Research_PromotePending(pBase->abResearchActive, pBase->pbResearchCount);
    if (pBase->pdwShortfall != NULL) {
        *pBase->pdwShortfall = 0; /* 0x84C9C */
    }
    Geoscape_PrepScreenOpen(pBase);          /* 0x84CBC */
    GameTime_Set5Sec(pBase->pWorld, pBase->apSpeedCtl); /* 0x84CC0 */
    (void)Geoscape_BuildNavControls(pEnv->pNavOut, pEnv->pNavEnv); /* 0x84CC8, R0 = abFacilityGrid */
    if (pEnv->pParkCtl != NULL) {
        pEnv->pParkCtl->x = 1000; /* 0x84CD8 STRH of 0x3E8 */
        pEnv->pParkCtl->y = 1000; /* 0x84CE4 STRH of 0x3E8 */
    }
    if (pBase->pState != NULL) {
        pBase->pState->nGeoscapeScreen = 8; /* 0x84CEC */
    }
    if (pBase->init.pfnInit != NULL) {
        pBase->init.pfnInit(pBase->init.pSlot, pBase->init.pUser); /* 0x84CF4 -> ResearchScreen_Init */
    }
}
