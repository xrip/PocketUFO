/* pufo_baseui.c - see pufo_baseui.h for evidence. */
#include "game/pufo_baseui.h"
#include <stdlib.h>  /* atexit, for StrategList_RegisterAtexit @ 0x838a0 */
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
void Ui_OpenScreen18(GeoscapeScreenState *pState, const GeoscapeHooks *pHooks)
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
void Ui_CloseScreen18(GeoscapeScreenState *pState, const GeoscapeHooks *pHooks)
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
void Ui_OpenFunding(GeoscapeScreenState *pState, const GeoscapeHooks *pHooks)
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
