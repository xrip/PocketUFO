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
    /* Input_OnAction sets byte_AD711 and UiScreen_OnBack clears the same byte, so
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
