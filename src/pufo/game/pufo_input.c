/* pufo_input.c - see pufo_input.h for evidence and the field map. */
#include "game/pufo_input.h"

/* The original's UI-screen ids. */
#define UI_SCREEN_NONE          0u
#define UI_SCREEN_INVENTORY     1u
#define UI_SCREEN_LOOK          2u
#define UI_SCREEN_COMMAND       4u
#define UI_SCREEN_OPTIONS       6u
#define UI_SCREEN_ESCAPE        7u
#define UI_SCREEN_TARGET_BEGIN  8u
#define UI_SCREEN_TARGET_END    9u
#define UI_SCREEN_T12           12u
#define UI_SCREEN_ESCAPE_ALT    15u

bool TactInput_CanScroll(const TactInputState *pState)
{
    return pState->bFireStatus2 == 0
        && pState->bTrajPad27_2 == 0
        && pState->bFireStatus1 == 0
        && !pState->bQueueActive;
}

static void Input_CallSample(const TactInputHooks *pHooks, u32 nSample)
{
    if (pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(nSample, pHooks->pUser);
    }
}

void Input_OnLeft(TactInputState *pState, const TactInputHooks *pHooks)
{
    if (pState->bGeoscapeUi) {
        if (pHooks->pfnGeoscapePanelA != NULL) {
            pHooks->pfnGeoscapePanelA(pHooks->pUser);
        }
        return;
    }
    if (!TactInput_CanScroll(pState)) {
        return;
    }
    if (pState->nUiScreen != UI_SCREEN_NONE) {
        if (pState->nUiScreen == UI_SCREEN_INVENTORY) {
            if (pHooks->pfnInvScrollUp != NULL) {
                pHooks->pfnInvScrollUp(pHooks->pUser);
            }
        } else if (pState->nUiScreen == UI_SCREEN_LOOK) {
            --pState->anCamWorldX;
        }
    } else if (pState->nCamScrollX > -(i32)(pState->nTileShiftX >> 1)) {
        --pState->nCamScrollX;
        if (pHooks->pfnCursorChanged != NULL) {
            pHooks->pfnCursorChanged(pHooks->pUser);
        }
    }
}

void Input_OnRight(TactInputState *pState, const TactInputHooks *pHooks)
{
    if (pState->bGeoscapeUi) {
        if (pHooks->pfnGeoscapePanelB != NULL) {
            pHooks->pfnGeoscapePanelB(pHooks->pUser);
        }
        return;
    }
    if (!TactInput_CanScroll(pState)) {
        return;
    }
    if (pState->nUiScreen != UI_SCREEN_NONE) {
        if (pState->nUiScreen == UI_SCREEN_INVENTORY) {
            if (pHooks->pfnInvScrollDown != NULL) {
                pHooks->pfnInvScrollDown(pHooks->pUser);
            }
        } else if (pState->nUiScreen == UI_SCREEN_LOOK) {
            ++pState->anCamWorldX;
        }
    } else if (pState->nCamScrollX < 10 * pState->nMapDimX - (i32)(pState->nTileShiftX >> 1)) {
        ++pState->nCamScrollX;
        if (pHooks->pfnCursorChanged != NULL) {
            pHooks->pfnCursorChanged(pHooks->pUser);
        }
    }
}

void Input_OnUp(TactInputState *pState, const TactInputHooks *pHooks)
{
    if (pState->bGeoscapeUi) {
        if (pHooks->pfnGeoscapeFocusPrev != NULL) {
            pHooks->pfnGeoscapeFocusPrev(pHooks->pUser);
        }
        return;
    }
    if (!TactInput_CanScroll(pState)) {
        return;
    }
    if (pState->nUiScreen != UI_SCREEN_NONE) {
        if (pState->nUiScreen == UI_SCREEN_INVENTORY) {
            if (pHooks->pfnSelectNextUnit != NULL) {
                pHooks->pfnSelectNextUnit(pHooks->pUser);
            }
        } else if (pState->nUiScreen == UI_SCREEN_LOOK) {
            if (pState->nLayerScratch < 4) {
                ++pState->nLayerScratch;
            }
            Input_CallSample(pHooks, 17);
        }
    } else if (pState->nCamScrollY > -(i32)(pState->nTileShiftY >> 2)) {
        --pState->nCamScrollY;
        if (pHooks->pfnCursorChanged != NULL) {
            pHooks->pfnCursorChanged(pHooks->pUser);
        }
    }
}

void Input_OnDown(TactInputState *pState, const TactInputHooks *pHooks)
{
    if (pState->bGeoscapeUi) {
        if (pHooks->pfnGeoscapeFocusNext != NULL) {
            pHooks->pfnGeoscapeFocusNext(pHooks->pUser);
        }
        return;
    }
    if (!TactInput_CanScroll(pState)) {
        return;
    }
    if (pState->nUiScreen != UI_SCREEN_NONE) {
        if (pState->nUiScreen == UI_SCREEN_INVENTORY) {
            if (pHooks->pfnSelectPrevUnit != NULL) {
                pHooks->pfnSelectPrevUnit(pHooks->pUser);
            }
        } else if (pState->nUiScreen == UI_SCREEN_LOOK) {
            if (pState->nLayerScratch > 1) {
                --pState->nLayerScratch;
            }
            Input_CallSample(pHooks, 17);
        }
    } else if (pState->nCamScrollY < 10 * pState->nMapDimY - (i32)(pState->nTileShiftY >> 2)) {
        ++pState->nCamScrollY;
        if (pHooks->pfnCursorChanged != NULL) {
            pHooks->pfnCursorChanged(pHooks->pUser);
        }
    }
}

void Input_OnAction(TactInputState *pState, const TactInputHooks *pHooks,
                    bool *pbActionFlag)
{
    if (pState->bGeoscapeUi) {
        if (pHooks->pfnGeoscapeMapClick != NULL) {
            pHooks->pfnGeoscapeMapClick(pHooks->pUser);
        }
        return;
    }
    if (!pState->bTactFlag) {
        return;
    }
    if (pbActionFlag != NULL) {
        *pbActionFlag = true; /* byte_AD711 */
    }
    if (!TactInput_CanScroll(pState)) {
        return;
    }
    if (pState->nUiScreen != UI_SCREEN_NONE) {
        if (pState->nUiScreen <= 2u || pState->nUiScreen == UI_SCREEN_COMMAND) {
            if (pHooks->pfnOnConfirm != NULL) {
                pHooks->pfnOnConfirm(pState->nUiScreen, pHooks->pUser);
            }
        } else if (pState->nUiScreen == UI_SCREEN_OPTIONS
                || pState->nUiScreen == UI_SCREEN_T12
                || pState->nUiScreen == UI_SCREEN_ESCAPE_ALT) {
            pState->nUiScreen = UI_SCREEN_NONE;
        }
    } else if (pHooks->pfnHandleMapClick != NULL) {
        pHooks->pfnHandleMapClick(pHooks->pUser);
    }
}

void Input_OnMenuEscape(TactInputState *pState, const TactInputHooks *pHooks)
{
    if (!TactInput_CanScroll(pState)) {
        return;
    }
    switch (pState->nUiScreen) {
    case UI_SCREEN_NONE:
        pState->nUiScreen = UI_SCREEN_ESCAPE;
        Input_CallSample(pHooks, 14);
        break;
    case UI_SCREEN_INVENTORY:
    case UI_SCREEN_LOOK:
    case UI_SCREEN_COMMAND:
        if (pHooks->pfnOnConfirm != NULL) {
            pHooks->pfnOnConfirm(pState->nUiScreen, pHooks->pUser);
        }
        break;
    case UI_SCREEN_OPTIONS:
    case 10u:
    case 11u:
    case UI_SCREEN_T12:
    case UI_SCREEN_ESCAPE_ALT:
        pState->nUiScreen = UI_SCREEN_NONE;
        break;
    case UI_SCREEN_TARGET_BEGIN:
    case UI_SCREEN_TARGET_END:
        /* UiModal_End(); then fall through to clearing the screen. Modeled by the
         * caller through pfnOnConfirm with the target screen id, which owns the
         * modal end. */
        if (pHooks->pfnOnConfirm != NULL) {
            pHooks->pfnOnConfirm(pState->nUiScreen, pHooks->pUser);
        }
        pState->nUiScreen = UI_SCREEN_NONE;
        break;
    case 13u:
        /* UiScreen_OnCancel(); the screen is not cleared by the original. */
        if (pHooks->pfnOnConfirm != NULL) {
            pHooks->pfnOnConfirm(pState->nUiScreen, pHooks->pUser);
        }
        break;
    default:
        break;
    }
}

i32 UiScreen_DismissHigh(TactInputState *pState, const TactInputHooks *pHooks,
                         bool *pbEscapeFlag)
{
    if (pbEscapeFlag != NULL) {
        *pbEscapeFlag = false; /* byte_AD712 = 0 */
    }
    if (pState == NULL) {
        return PUFO_UI_DISMISS_NONE;
    }
    if (pState->nUiScreen >= PUFO_UI_SCREEN_TARGET_BEGIN) {
        if (pState->nUiScreen <= PUFO_UI_SCREEN_TARGET_END) {
            /* UiModal_End(); cleared here through the confirm hook, which owns
             * the modal end in the port. */
            if (pHooks != NULL && pHooks->pfnOnConfirm != NULL) {
                pHooks->pfnOnConfirm(pState->nUiScreen, pHooks->pUser);
            }
            pState->nUiScreen = UI_SCREEN_NONE;
            return PUFO_UI_DISMISS_MODAL;
        }
        if (pState->nUiScreen == PUFO_UI_SCREEN_CANCEL) {
            /* UiScreen_OnCancel(); the screen id is not cleared. */
            if (pHooks != NULL && pHooks->pfnOnConfirm != NULL) {
                pHooks->pfnOnConfirm(pState->nUiScreen, pHooks->pUser);
            }
            return PUFO_UI_DISMISS_CANCEL;
        }
    }
    return PUFO_UI_DISMISS_NONE;
}

/* The screen ids used by the back/cancel handlers. */
#define UI_SCREEN_BACK_MODAL   8u
#define UI_SCREEN_BACK_ABORT   9u
#define UI_SCREEN_BACK_CANCEL  13u
#define UI_SCREEN_CANCEL_19    19u
#define UI_SCREEN_CANCEL_LO    0x13u
#define UI_SCREEN_CANCEL_HI    0x15u

void UiScreen_OnBack(TactInputState *pState, const TactInputHooks *pHooks,
                     bool *pbEscapeFlag)
{
    if (pbEscapeFlag != NULL) {
        *pbEscapeFlag = false; /* byte_AD711 = 0 */
    }
    if (pState == NULL) {
        return;
    }
    switch (pState->nUiScreen) {
    case UI_SCREEN_BACK_MODAL:
        if (pHooks->pfnShutdownQuit != NULL) {
            pHooks->pfnShutdownQuit(pHooks->pUser); /* Tact_Shutdown(1)+App_QuitDisplay */
        }
        break;
    case UI_SCREEN_BACK_ABORT:
        if (pHooks->pfnModalEndCheckBattle != NULL) {
            pHooks->pfnModalEndCheckBattle(pHooks->pUser);
        }
        break;
    case UI_SCREEN_BACK_CANCEL:
        if (pHooks->pfnOnCancel != NULL) {
            pHooks->pfnOnCancel(pHooks->pUser);
        }
        break;
    default:
        break;
    }
}

i32 UiScreen_OnCancel(TactInputState *pState, const TactInputHooks *pHooks)
{
    i32 nResult;

    if (pState == NULL) {
        return 0;
    }
    nResult = pState->bTactFlag ? 1 : 0;
    if (!pState->bTactFlag) {
        return nResult;
    }
    if (!TactInput_CanScroll(pState)) {
        /* The original also requires the game-tick hold flag here. */
        if (!pState->bGameTickHold) {
            return nResult;
        }
    }
    nResult = (i32)pState->nUiScreen;
    if (pState->nUiScreen < UI_SCREEN_BACK_MODAL) {
        return nResult;
    }
    if (pState->nUiScreen <= UI_SCREEN_BACK_ABORT) {
        if (pHooks->pfnModalEnd != NULL) {
            pHooks->pfnModalEnd(pHooks->pUser);
        }
        nResult = 0;
        pState->nUiScreen = 0;
        return nResult;
    }
    if (pState->nUiScreen == UI_SCREEN_BACK_CANCEL) {
        if (pState->bCancelTo19) {
            pState->nUiScreen = UI_SCREEN_CANCEL_19;
            return UI_SCREEN_CANCEL_19;
        }
        if (pHooks->pfnShutdownReinit != NULL) {
            pHooks->pfnShutdownReinit(pHooks->pUser);
        }
        return nResult;
    }
    if (pState->nUiScreen == UI_SCREEN_CANCEL_19) {
        if (pHooks->pfnShutdownReinit != NULL) {
            pHooks->pfnShutdownReinit(pHooks->pUser);
        }
        return nResult;
    }
    if (pState->nUiScreen > UI_SCREEN_CANCEL_LO && pState->nUiScreen <= UI_SCREEN_CANCEL_HI) {
        pState->nUiScreen = pState->nUiSlot11;
    }
    return nResult;
}
