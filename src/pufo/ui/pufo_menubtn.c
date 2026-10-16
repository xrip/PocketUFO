/* pufo_menubtn.c - see pufo_menubtn.h for the evidence and the reasoning. */
#include "ui/pufo_menubtn.h"

#include <stdlib.h>
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

/* MenuBtn_SetGamma0 @ 0x19070, SetGamma1 @ 0x190d0, SetGamma2 @ 0x19130, SetGamma3 @ 0x19190,
 * SetGamma4 @ 0x191f0 - one function, five values.
 *
 * The originals are literally the same five stores in the same order with a different 1 and a different
 * constant, so the port keeps one implementation and takes the index. Which widget lights is the only
 * difference between the five click handlers; the interesting part is that changing gamma reloads and
 * rebuilds every palette, which is why the tail call is Tact_LoadPalettes and not just a flag write. */
void MenuBtn_SetGamma(UiControl *pBank, i32 nGamma, i32 *pnGamma, const MenuHooks *pHooks)
{
    i32 k;

    Btn_Play(pHooks, PUFO_BTN_SAMPLE_GAMMA);              /* 0x1907C and friends */

    for (k = 0; k < PUFO_BANK_GAMMA_COUNT; ++k) {
        pBank[PUFO_BANK_GAMMA_FIRST + k].nState = (u8)(k == nGamma ? 1 : 0);
    }

    if (pnGamma != NULL) {
        *pnGamma = nGamma;                                /* unk_128AB8, Map+0x15100 */
    }

    Tact_LoadPalettes(nGamma);                            /* tail call */
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

/* App_QuitFromTact @ 0x18e18: Tact_Shutdown(1) then App_QuitDisplay, in that order. See the header
 * for the injected-halves shape and the shell deviation. */
void App_QuitFromTact(void (*pfnShutdownTact)(u8 bKeepSome, void *pUser),
                      void (*pfnQuitDisplay)(void *pUser), void *pUser)
{
    if (pfnShutdownTact != NULL) {
        pfnShutdownTact(1, pUser); /* 0x18E20: Tact_Shutdown(1) */
    }
    if (pfnQuitDisplay != NULL) {
        pfnQuitDisplay(pUser); /* 0x18E28: App_QuitDisplay */
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

/* StartUi_Shutdown @ 0x38d7c.
 *
 * The decompiler renders the pad loop as `v2[113].pPad0` with `v2 = (char *)v2 + 4`, which reads
 * as three pad pointers four WIDGETS apart. The ARM says otherwise: `MOV R3, #0xE34` is loaded
 * once outside both loops and never modified, the inner step is `ADD R4, R4, #4` -- four BYTES --
 * and the outer step is `ADD R7, R7, #0x20`, one UiControl. Each of the 15 groups therefore frees
 * three ADJACENT dwords at pBank + 32*i + 0xE34.
 *
 * 0xE34 is 3636, and 3636 = 32 * 113 + 0x14, which is widget 113's pPad0. The three dwords are
 * pPad0, pPad1 and pPad2 of a single widget, and the outer step moves to the next widget. The loop
 * frees the three pads of bank widgets 113 through 127 inclusive: the tail of the 130-widget bank,
 * which is where the start-menu pads live. Nothing reaches past the bank. */
void StartUi_Shutdown(const StartUiShutdownEnv *pEnv)
{
    i32 i;

    if (pEnv == NULL) {
        return;
    }

    if (pEnv->hooks.pfnSoundShutdown != NULL) {
        pEnv->hooks.pfnSoundShutdown(pEnv->hooks.pUser); /* 0x38D80 */
    }
    if (pEnv->hooks.pfnKillTimers != NULL) {
        pEnv->hooks.pfnKillTimers(pEnv->hooks.pUser);    /* 0x38D84 */
    }

    if (pEnv->pBank != NULL) {
        for (i = 0; i < 15; ++i) {                        /* 0x38D8C, R8 = 15 */
            UiControl *pCtl = &pEnv->pBank[113 + i];
            i32 k;

            for (k = 0; k < PUFO_CONTROL_PADS; ++k) {     /* 0x38D94, R5 = 3 */
                PadImage *pPad = pCtl->apPads[k];
                if (pPad != NULL) {
                    PadImage_FreeData(pPad);             /* 0x38DB8 */
                    free(pPad);                          /* 0x38DC0, operator delete */
                    pCtl->apPads[k] = NULL;
                }
            }
        }
    }

    if (pEnv->hooks.pfnFontFree != NULL) {
        pEnv->hooks.pfnFontFree(pEnv->pFont, pEnv->hooks.pUser); /* 0x38DE8 */
    }

    if (pEnv->pStartCtl != NULL) {                        /* 0x38DF8 */
        UiControl_FreePads(pEnv->pStartCtl);              /* 0x38E00 */
        if (pEnv->hooks.pfnFree != NULL) {
            pEnv->hooks.pfnFree(pEnv->pStartCtl, pEnv->hooks.pUser); /* 0x38E08 */
        }
    }

    if (pEnv->hooks.pfnFree != NULL) {
        pEnv->hooks.pfnFree(pEnv->pBlockA, pEnv->hooks.pUser); /* 0x38E14 */
        pEnv->hooks.pfnFree(pEnv->pBlockB, pEnv->hooks.pUser); /* 0x38E20 */
    }

    if (pEnv->pnUiScreen != NULL) {
        *pEnv->pnUiScreen = 22;                           /* 0x38E2C */
    }
}
