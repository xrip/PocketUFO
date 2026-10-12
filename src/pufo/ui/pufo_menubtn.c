/* pufo_menubtn.c - see pufo_menubtn.h for the evidence and the reasoning. */
#include "ui/pufo_menubtn.h"

#include "game/pufo_menu.h"

static void Btn_Play(const MenuHooks *pHooks, i32 nSample)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(nSample, pHooks->pUser);
    }
}

static void Btn_PlayInput(const TactInputHooks *pHooks, i32 nSample)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample((u32)nSample, pHooks->pUser);
    }
}

/* The two labels the original keeps at 0xA62E8 ("YES") and 0xA62EC ("NO"). */
static char s_szYes[] = "YES";
static char s_szNo[]  = "NO";

void MenuBtn_Confirm(const UiButtonCtx *pCtx, i32 nSample)
{
    if (pCtx == NULL || pCtx->pState == NULL) {
        return;
    }
    Btn_PlayInput(pCtx->pHooks, nSample);
    Input_OnConfirm(pCtx->pState, pCtx->pHooks);
}

void MenuBtn_ConfirmClear(const UiButtonCtx *pCtx)
{
    if (pCtx == NULL || pCtx->pState == NULL) {
        return;
    }
    Btn_PlayInput(pCtx->pHooks, PUFO_BTN_SAMPLE_CONFIRM);
    pCtx->pState->nUiScreen = 0;
    Input_OnConfirm(pCtx->pState, pCtx->pHooks);
}

void MenuBtn_Secondary(const UiButtonCtx *pCtx)
{
    if (pCtx == NULL || pCtx->pState == NULL) {
        return;
    }
    Btn_PlayInput(pCtx->pHooks, PUFO_BTN_SAMPLE_CONFIRM);
    pCtx->pState->nUiScreen = 0;
    Input_OnSecondary(pCtx->pState, pCtx->pHooks, pCtx->pWorld, pCtx->pbEscapeFlag);
}

i32 MenuBtn_SecondaryDismiss(const UiButtonCtx *pCtx)
{
    if (pCtx == NULL || pCtx->pState == NULL) {
        return PUFO_UI_DISMISS_NONE;
    }
    MenuBtn_Secondary(pCtx);
    return UiScreen_DismissHigh(pCtx->pState, pCtx->pHooks, pCtx->pbEscapeFlag);
}

void MenuBtn_ActionBack(const UiButtonCtx *pCtx)
{
    if (pCtx == NULL || pCtx->pState == NULL) {
        return;
    }
    Btn_PlayInput(pCtx->pHooks, PUFO_BTN_SAMPLE_CONFIRM);
    pCtx->pState->nUiScreen = 0;
    /* Input_OnAction sets UiBank_bActionFlag and UiScreen_OnBack clears the same byte, so
     * the pair leaves it 0. Both take the caller's storage. */
    Input_OnAction(pCtx->pState, pCtx->pHooks, pCtx->pbActionFlag);
    UiScreen_OnBack(pCtx->pState, pCtx->pHooks, pCtx->pbActionFlag);
}

i32 MenuBtn_Cancel(const UiButtonCtx *pCtx)
{
    if (pCtx == NULL || pCtx->pState == NULL) {
        return 0;
    }
    Btn_PlayInput(pCtx->pHooks, PUFO_BTN_SAMPLE_CONFIRM);
    return UiScreen_OnCancel(pCtx->pState, pCtx->pHooks);
}

void Menu_OpenSlotList(u16 *pnUiScreen, i32 *pnListMode, bool bSaveMode,
                       void (*pfnFormatList)(void *pUser), const MenuHooks *pHooks)
{
    if (pnUiScreen == NULL) {
        return;
    }
    /* The list is rebuilt BEFORE the sample and before the screen changes. */
    if (pfnFormatList != NULL) {
        pfnFormatList(pHooks != NULL ? pHooks->pUser : NULL);
    }
    Btn_Play(pHooks, PUFO_BTN_SAMPLE_CONFIRM);
    *pnUiScreen = bSaveMode ? PUFO_UI_SCREEN_SAVE_SLOTS : PUFO_UI_SCREEN_LOAD_SLOTS;
    if (pnListMode != NULL) {
        *pnListMode = 0;
    }
}

i32 Menu_OpenQuitDialog(UiModalState *pModal, u16 *pnUiScreen,
                        void (*pfnYes)(void), void (*pfnNo)(void),
                        const MenuHooks *pHooks)
{
    Btn_Play(pHooks, PUFO_BTN_SAMPLE_CONFIRM);
    UiModal_Begin(pModal, 140, 24, 4, s_szYes, pfnYes, s_szNo, pfnNo);
    if (pnUiScreen != NULL) {
        *pnUiScreen = PUFO_UI_SCREEN_QUIT_DIALOG;
    }
    return PUFO_UI_SCREEN_QUIT_DIALOG;
}

void Menu_CancelQuitDialog(u16 *pnUiScreen)
{
    if (pnUiScreen != NULL) {
        *pnUiScreen = 0;
    }
}

static void StartMenu_Play(const StartMenuHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(PUFO_BTN_SAMPLE_CONFIRM, pHooks->pUser); /* 16 */
    }
}

static void StartMenu_Shutdown(const StartMenuHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnShutdownStartUi != NULL) {
        pHooks->pfnShutdownStartUi(pHooks->pUser); /* StartUi_Shutdown (0x38d7c) */
    }
}

void MenuCtl0_OnClick(const StartMenuHooks *pHooks)
{
    StartMenu_Play(pHooks);      /* 0x1A0A8 */
    StartMenu_Shutdown(pHooks);  /* 0x1A0AC */
    if (pHooks != NULL && pHooks->pfnNewGame != NULL) {
        pHooks->pfnNewGame(pHooks->pUser); /* Geoscape_InitUI(0) @ 0x1A0B8 */
    }
}

void MenuCtl1_OnClick(i32 *pnListMode, const StartMenuHooks *pHooks)
{
    StartMenu_Play(pHooks); /* 0x1A040 */
    if (pHooks != NULL && pHooks->pfnFormatSlots != NULL) {
        pHooks->pfnFormatSlots(pHooks->pUser); /* 0x1A048, BEFORE the mode is set */
    }
    if (pnListMode != NULL) {
        *pnListMode = PUFO_MENU_MODE_LOAD_LIST; /* 0x1A054 */
    }
}

void MenuCtl2_OnClick(const StartMenuHooks *pHooks)
{
    StartMenu_Play(pHooks);
    StartMenu_Shutdown(pHooks);
    if (pHooks != NULL && pHooks->pfnTitles != NULL) {
        pHooks->pfnTitles(pHooks->pUser); /* Titles_Init @ 0x1A078 */
    }
}

void MenuCtl3_OnClick(const StartMenuHooks *pHooks)
{
    StartMenu_Play(pHooks);
    StartMenu_Shutdown(pHooks);
    if (pHooks != NULL && pHooks->pfnQuit != NULL) {
        pHooks->pfnQuit(pHooks->pUser); /* App_QuitDisplay @ 0x1A098 */
    }
}

/* One body for the seven load buttons; only the slot number differs. */
i32 Menu_LoadSlot(i32 *pnListMode, i32 nSlot,
                  i32 (*pfnLoadGame)(i32 nSlot, void *pUser), void *pUser)
{
    i32 nResult;

    if (pnListMode != NULL) {
        *pnListMode = PUFO_MENU_MODE_LOADING; /* 0x1A0CC, before the load runs */
    }
    nResult = (pfnLoadGame != NULL) ? pfnLoadGame(nSlot, pUser) : 0;

    if ((nResult & 0xFF) == 0) { /* 0x1A0DC tests the low BYTE of the result */
        if (pnListMode != NULL) {
            *pnListMode = PUFO_MENU_MODE_LOAD_LIST;
        }
        return PUFO_MENU_MODE_LOAD_LIST; /* 0x1A0E0, not the callee's value */
    }
    return nResult;
}

i32 SaveLoad_Slot(i32 nSlot, i32 nUiScreen, i32 *pnListMode,
                  const SaveLoadSlotHooks *pHooks)
{
    i32 nResult;

    if (pHooks == NULL) {
        return 0;
    }

    if (pnListMode != NULL) {
        *pnListMode = PUFO_MENU_MODE_SAVE_LOAD_LIST; /* 0x19DF4 */
    }

    /* 0x19E08: the direction comes from the screen the button is drawn in. */
    if (nUiScreen == PUFO_MENU_SCREEN_SAVE_LIST) {
        if (pHooks->pfnSaveGame == NULL) {
            return 0;
        }
        return pHooks->pfnSaveGame(nSlot, pHooks->pUser); /* 0x19E0C */
    }

    if (pHooks->pfnLoadGame == NULL) {
        return 0;
    }
    nResult = pHooks->pfnLoadGame(nSlot, pHooks->pUser); /* 0x19E20 */

    if ((nResult & 0xFF) == 0) { /* 0x19E28, the LOW byte again */
        if (pnListMode != NULL) {
            *pnListMode = 0; /* 0x19E2C, not 2 */
        }
        return 0; /* 0x19E28 */
    }
    return nResult;
}
