/* pufo_input.c - see pufo_input.h for evidence and the field map. */
#include "game/pufo_input.h"
#include "game/pufo_mapcell.h"   /* MapCell, TactItem, ObjectRecFull */
#include "game/pufo_path.h"       /* g_aStepDir */
#include "game/pufo_tactstate.h"  /* Map_QueueDoorAction, Unit_FaceToward, TactState_KickQueue */
#include "game/pufo_unit.h"       /* Unit_bIsLarge, Soldier.pCell/bFacing */

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

/* Input_OnConfirm @ 0x15f3c.
 *
 * The gate is read off the ARM at 0x15F3C..0x15FBC: bGeoscapeUi (0x44) takes the
 * geoscape branch; otherwise bTactFlag (0x00), bFireStatus2 (0x52), bTrajHitStop
 * (0x1406D), bFireStatus1 (0x51) and TactState.bQueueActive (0x111D6) must all be
 * clear/zero before the switch on nUiScreen (0x3A, LDRH/STRH). */
void Input_OnConfirm(TactInputState *pState, const TactInputHooks *pHooks)
{
    if (pState->bGeoscapeUi) {
        if (pHooks->pfnGeoscapeCloseUi != NULL) {
            pHooks->pfnGeoscapeCloseUi(pHooks->pUser);
        }
        return;
    }
    if (!pState->bTactFlag || !TactInput_CanScroll(pState)) {
        return;
    }

    switch (pState->nUiScreen) {
    case UI_SCREEN_NONE:
        /* 0x16010: byte 0x22 selects commit or cancel. */
        if (pState->bMultiTargetArmed == 0) {
            Input_CallSample(pHooks, 7);
            pState->bDeployPanelOpen = (pState->bDeployPanelOpen == 0) ? 1u : 0u;
            return;
        }
        if (pState->pbScratchFlag9 != NULL) {
            *pState->pbScratchFlag9 = 0;
        }
        pState->bMultiTargetArmed = 0;
        if (pHooks->pfnSetCursorPos != NULL) {
            pHooks->pfnSetCursorPos(pState->nCursorX, pState->nCursorY, pHooks->pUser);
        }
        /* 0x16070: only the two multi-target weapon kinds clear their marks. */
        if (pState->pSelWeapon != NULL && pState->pSelWeapon->pObj != NULL) {
            u16 nSortKey = pState->pSelWeapon->pObj->uSortKey;
            if (nSortKey == 13u || nSortKey == 35u) {
                for (i32 i = 0; i < pState->nMultiTargetCount; ++i) {
                    /* Deviation: the original dereferences the slot unconditionally. */
                    if (pState->apMultiTargets[i] != NULL) {
                        pState->apMultiTargets[i]->bFlags29 &= (u8)~0x40;
                    }
                }
            }
        }
        pState->nMultiTargetCount = 0;
        return;

    case UI_SCREEN_INVENTORY:
        Input_CallSample(pHooks, 16);
        if (pHooks->pfnInvCloseAndCommit != NULL) {
            pHooks->pfnInvCloseAndCommit(pHooks->pUser);
        }
        return;

    case UI_SCREEN_LOOK:
        /* 0x1610C: the unit stored for the camera's current view cell. */
        if (pHooks->pfnFocusOnViewCell != NULL) {
            pHooks->pfnFocusOnViewCell((i32)pState->nLayerScratch,
                                       pState->anCamWorldY, pState->anCamWorldX,
                                       pHooks->pUser);
        }
        pState->nUiScreen = 0;
        Input_CallSample(pHooks, 16);
        return;

    case 3u:
    case 5u:
    case 10u:
    case 11u:
    case UI_SCREEN_ESCAPE_ALT:
        /* 0x161E4: UiModal_End(pMap), then the shared screen-clear tail. */
        if (pHooks->pfnModalEnd != NULL) {
            pHooks->pfnModalEnd(pHooks->pUser);
        }
        pState->nUiScreen = 0;
        return;

    case UI_SCREEN_COMMAND:
        /* 0x16168. */
        if (pState->bLookCommitFlag != 0) {
            pState->pSelUnit = (Soldier *)pState->pLookCommitUnit;
        }
        pState->nUiScreen = (pState->bUiSlot8Byte0 != 0) ? 1u : 0u;
        pState->bLookCommitFlag = 0;
        pState->bUiSlot8Byte0 = 0;
        Input_CallSample(pHooks, 16);
        return;

    case UI_SCREEN_OPTIONS:
        Input_CallSample(pHooks, 16);
        pState->nUiScreen = 0;
        return;

    case UI_SCREEN_ESCAPE:
        Input_CallSample(pHooks, 14);
        pState->nUiScreen = 0;
        return;

    case UI_SCREEN_T12:
        if (pHooks->pfnSaveOptions != NULL) {
            pHooks->pfnSaveOptions(pHooks->pUser);
        }
        pState->nUiScreen = 0;
        return;

    default:
        /* 8, 9, 13, 14 and anything above 15 fall straight to the epilogue. */
        return;
    }
}

/* Input_OnSecondary @ 0x156e0. The secondary key (right click / second action).
 *
 * Gate read off the ARM at 0x156E0..0x1575C: bGeoscapeUi (0x44) takes a branch
 * that only calls nullsub_8 @ 0x8b948, so the geoscape does nothing here. The
 * tactical gate is the same five checks Input_OnConfirm uses.
 *
 * Screen routing is a compare chain, not a jumptable (0x15760..0x15784 and the
 * far block at 0x159C8): >6 goes to 10..12 and 15 clearing the screen, 6 clears
 * it, 1/2/4 delegate to Input_OnConfirm with the screen id as the second
 * argument, 3 and 5 do nothing. */
void Input_OnSecondary(TactInputState *pState, const TactInputHooks *pHooks,
                       const TactInputWorld *pWorld, bool *pbEscapeFlag)
{
    TactCursor    cursor;
    MapGeom       geom;
    Soldier      *pUnit;
    MapCell      *pCell;
    MapCell      *pTo;
    const StepDir *pDir;
    i32           nX1;
    i32           nY1;

    if (pState->bGeoscapeUi) {
        return; /* nullsub_8 @ 0x8b948 */
    }
    if (!pState->bTactFlag || !TactInput_CanScroll(pState)) {
        return;
    }

    if (pState->nUiScreen > 6u) {
        /* 0x159C8: 7, 8, 9, 13, 14 and anything above 15 fall to the epilogue. */
        if ((pState->nUiScreen >= 10u && pState->nUiScreen <= 12u)
            || pState->nUiScreen == UI_SCREEN_ESCAPE_ALT) {
            pState->nUiScreen = 0;
        }
        return;
    }
    if (pState->nUiScreen == UI_SCREEN_OPTIONS) {
        pState->nUiScreen = 0;
        return;
    }
    if (pState->nUiScreen != UI_SCREEN_NONE) {
        if (pState->nUiScreen <= UI_SCREEN_LOOK || pState->nUiScreen == UI_SCREEN_COMMAND) {
            Input_OnConfirm(pState, pHooks);
        }
        return;
    }

    /* Screen 0. 0x15794: byte 0x22 selects the mode. */
    if (pState->bMultiTargetArmed == 3u) {
        /* 0x15980: pop the last selected target. */
        if (pState->nMultiTargetCount <= 0) {
            return;
        }
        pTo = pState->apMultiTargets[pState->nMultiTargetCount - 1];
        if (pTo != NULL) {
            pTo->bFlags29 &= (u8)~0x40;
        }
        if (--pState->nMultiTargetCount == 0 && pState->pbScratchFlag9 != NULL) {
            *pState->pbScratchFlag9 = 0;
        }
        return;
    }

    if (pbEscapeFlag != NULL) {
        *pbEscapeFlag = true; /* unk_AD712 = 1 @ 0x157B0 */
    }

    cursor.nX     = pState->nCursorX;
    cursor.nY     = pState->nCursorY;
    cursor.nFlags = pState->nLayerScratch;
    geom.nDimX    = pState->nMapDimX;
    geom.nDimY    = pState->nMapDimY;
    if (!MapCursor_IsValid(&cursor, &geom)) {
        return;
    }

    /* Deviation: the original dereferences pSelUnit and its cell unconditionally. */
    pUnit = pState->pSelUnit;
    if (pUnit == NULL || pUnit->pCell == NULL || pWorld == NULL
        || pWorld->pQueue == NULL || pWorld->pfnLookup == NULL) {
        return;
    }
    pCell = pUnit->pCell;

    /* 0x157D8: the facing delta is aDirDeltaXY[3*bFacing], the same signed-byte
     * triple the port keeps as g_aStepDir. */
    pDir = &g_aStepDir[pUnit->bFacing];
    nX1  = (i32)pDir->dx + pCell->nX;
    nY1  = (i32)pDir->dy + pCell->nY;

    /* 0x15814..0x158E8. The parity test is __rt_sdiv(R0=2, R1=bFacing) with the
     * remainder in R1, so only the orthogonal facings reach the door path --
     * which is also the only set of facings Map_QueueDoorAction accepts. */
    if (!Unit_bIsLarge(pUnit) && (pUnit->bFacing % 2) == 0
        && Map_IsValidCell(&geom, nX1, nY1, pCell->nZ)) {
        MapCell *pDoor = pWorld->pfnLookup(nX1, nY1, pCell->nZ, pWorld->pLookupUser);
        if (pDoor != NULL && (pDoor->bFlags57[0] & 8u) != 0) {
            DoorActionCtx ctx = { pUnit, false, geom, pWorld->pfnLookup,
                                  pWorld->pLookupUser };
            /* 0x158C8: the cursor cell is apCells[nLayerScratch-1][nCursorY][nCursorX],
             * the base 0x11AF8 = apCells @ 0x15338 minus one 14400-byte layer. */
            Map_QueueDoorAction(pWorld->pQueue, pCell,
                                pWorld->pfnLookup(cursor.nX, cursor.nY,
                                                  (i32)(i8)cursor.nFlags - 1,
                                                  pWorld->pLookupUser),
                                pUnit->bFacing, &ctx);
        }
    }

    /* 0x158F8: the tail runs on every path that got here, and re-reads the unit's
     * cell. The cursor cell is the same one-layer-down lookup. */
    pTo = pWorld->pfnLookup(cursor.nX, cursor.nY, (i32)(i8)cursor.nFlags - 1,
                            pWorld->pLookupUser);
    if (pTo == NULL) {
        return;
    }
    if (pCell->nX != pTo->nX || pCell->nY != pTo->nY) {
        Unit_FaceToward(pWorld->pQueue, pUnit, pTo->nX, pTo->nY);
        TactState_KickQueue(pWorld->pQueue);
    }
}

/* --- Input_OnPointer @ 0x13c6c ------------------------------------------------
 *
 * The HUD edge flags live in one dword at Map+0x74, low byte first: left, right,
 * top, bottom. The prologue clears all four; each test sets one or two. */
#define PUFO_EDGE_LEFT   0
#define PUFO_EDGE_RIGHT  1
#define PUFO_EDGE_TOP    2
#define PUFO_EDGE_BOTTOM 3

static void PointerEdge(TactInputState *pState, i32 nEdge)
{
    pState->nUiSlot5 |= (u32)1u << (8 * nEdge);
}

static void PointerHitTestRange(UiWidgetBank *pBank, const UiControlHitCtx *pHit,
                                i32 nFirst, i32 nCount, i32 nX, i32 nY)
{
    i32 i;
    for (i = 0; i < nCount; ++i) {
        (void)UiControl_HitTest(&pBank->aCtl[nFirst + i], nX, nY, true, false, pHit);
    }
}

/* 0x13D08: screen 17 marks list rows instead of hit-testing widgets. Modes 0 and 1
 * are the same four-row box (0x13DBC and 0x13E04 are identical code); mode 2 is
 * seven rows and then the control. */
static void PointerScreen17(TactPointerWorld *pWorld, i32 nX, i32 nY)
{
    i32 nRow;
    i32 i;

    for (i = 0; i < 7; ++i) {
        pWorld->abListHit[i] = 0;
    }

    switch (pWorld->nListMode) {
    case 0:
    case 1:
        for (nRow = 0, i = 0; nRow < 88; nRow += 22, ++i) {
            if (nX >= 50 && nX <= 190 && nY >= nRow + 115 && nY <= nRow + 133) {
                pWorld->abListHit[i] = 1;
            }
        }
        break;
    case 2:
        for (nRow = 0, i = 0; nRow < 154; nRow += 22, ++i) {
            if (nX >= 24 && nX <= 216 && nY >= nRow + 65 && nY <= nRow + 83) {
                pWorld->abListHit[i] = 1;
            }
        }
        (void)UiControl_HitTest(pWorld->pListCtl, nX, nY, true, false, &pWorld->hit);
        break;
    default:
        break;
    }
}

/* 0x13E58: the status-line hit box, live only while Map+0xac is set. The row is 18
 * pixels tall and each column 51 pixels wide. */
static void PointerStatusLine(TactInputState *pState, i32 nX, i32 nY)
{
    i32 nHit = 0;

    if (pState->bSlot19Active == 0) {
        return;
    }
    if (pState->nSlot23H > 0 && nY >= pState->nSlot22Y && nY <= pState->nSlot22Y + 17) {
        if (nX >= pState->nSlot20X && nX <= pState->nSlot20X + 50) {
            nHit = 1;
        } else if (nX >= pState->nSlot21X && nX <= pState->nSlot21X + 50) {
            nHit = 2;
        }
    }
    pState->nSlot24Hit = nHit;
}

/* 0x1407C: the tactical map. */
static void PointerScreen0(TactInputState *pState, const TactInputHooks *pHooks,
                           TactPointerWorld *pWorld, bool bDown, i32 nX, i32 nY)
{
    static const i32 anMulti[4] = { 2, 7, 9, 15 };
    UiWidgetBank *pBank = pWorld->pBank;
    Soldier      *pSel  = pState->pSelUnit;
    i32           nBase, nThird, nUpper, nRow, i;

    if (UiControl_HitTest(pWorld->pLookCtl, nX, nY, true, false, &pWorld->hit)) {
        return;
    }

    /* 0x140A0: the multi-target list under the HUD, at most five rows (the loop
     * stops once the row offset reaches -90), 18 pixels apart. */
    if (pSel != NULL && pSel->nMultiTarget > 0) {
        nRow = 0;
        for (i = 0; i < pSel->nMultiTarget && nRow > -90; ++i) {
            i32 nTop = (pState->bDeployPanelOpen == 0 && pWorld->pHudPad != NULL)
                     ? nRow + 300 - (i32)pWorld->pHudPad->uHeight
                     : nRow + 283;
            if (nX >= 207 && nY >= nTop && nX <= 237 && nY <= nTop + 16) {
                Soldier *pTarget = pSel->multiOrAnim.apMultiTargets[i];
                if (pHooks->pfnFocusOnCell != NULL && pTarget != NULL && pTarget->pCell != NULL) {
                    pHooks->pfnFocusOnCell(pTarget->pCell, pHooks->pUser);
                }
                if (pHooks->pfnPlaySample != NULL) {
                    pHooks->pfnPlaySample(17, pHooks->pUser);
                }
                return;
            }
            nRow -= 18;
        }
    }

    /* 0x14130: the HUD band, and its thirds. */
    nBase  = (pState->bDeployPanelOpen != 0)
           ? 303
           : 320 - (i32)(pWorld->pHudPad != NULL ? pWorld->pHudPad->uHeight : 0);
    nThird = nBase / 3;
    nUpper = nBase - nThird;

    if (pState->bPlayerInputOn != 0 && pWorld->pHudPad != NULL &&
        nY >= pWorld->pHudPad->nOriginY) {
        goto widgets;
    }
    if (pState->bEdgeTestOn == 0) {
        goto camera;
    }

    /* 0x141BC..0x14318: the eleven edge tests, in the order the original chains
     * them. Each either sets flags and goes to the widgets, or falls through. */
    if (nX < 15 && nY < nUpper && nY > nThird) {
        PointerEdge(pState, PUFO_EDGE_LEFT);
        goto widgets;
    }
    if (nX > 225 && nY < nUpper && nY > nThird) {
        PointerEdge(pState, PUFO_EDGE_RIGHT);
        goto widgets;
    }
    if (nY < 15 && nX < 160 && nX > 80) {
        PointerEdge(pState, PUFO_EDGE_TOP);
        goto widgets;
    }
    if (nY < nBase && nY > nBase - 15 && nX < 160 && nX > 80) {
        PointerEdge(pState, PUFO_EDGE_RIGHT);
        PointerEdge(pState, PUFO_EDGE_BOTTOM);
        goto widgets;
    }
    if (nX < 15 && nY < nThird) {
        PointerEdge(pState, PUFO_EDGE_TOP);
        PointerEdge(pState, PUFO_EDGE_LEFT);
        goto widgets;
    }
    if (nY < 15 && nX < 80) {
        PointerEdge(pState, PUFO_EDGE_TOP);
        PointerEdge(pState, PUFO_EDGE_LEFT);
        goto widgets;
    }
    if (nX > 225 && nY > nUpper && nY < nBase) {
        PointerEdge(pState, PUFO_EDGE_RIGHT);
        PointerEdge(pState, PUFO_EDGE_BOTTOM);
        goto widgets;
    }
    if (nX > 160 && nY < nBase && nY > nBase - 15) {
        PointerEdge(pState, PUFO_EDGE_RIGHT);
        PointerEdge(pState, PUFO_EDGE_BOTTOM);
        goto widgets;
    }
    if (nX > 160 && nY < 15) {
        PointerEdge(pState, PUFO_EDGE_TOP);
        PointerEdge(pState, PUFO_EDGE_RIGHT);
        goto widgets;
    }
    if (nX > 225 && nY < nThird) {
        PointerEdge(pState, PUFO_EDGE_TOP);
        PointerEdge(pState, PUFO_EDGE_RIGHT);
        goto widgets;
    }
    if (nX < 15 && nY > nUpper && nY < nBase) {
        PointerEdge(pState, PUFO_EDGE_LEFT);
        PointerEdge(pState, PUFO_EDGE_BOTTOM);
        goto widgets;
    }
    if (nX < 80 && nY < nBase && nY > nBase - 15) {
        PointerEdge(pState, PUFO_EDGE_LEFT);
        PointerEdge(pState, PUFO_EDGE_BOTTOM);
        goto widgets;
    }

camera:
    /* 0x143D0: the pointer is over the map, so this is a camera drag. */
    {
        i32          nHX = nX, nHY = nY;
        i32          nSavedX = pState->nCursorX, nSavedY = pState->nCursorY;
        i32          nDragX = 0, nDragY = 0;
        TactCursor   cursor;
        MapCursorPos ctx;
        i32          nOffB, nOffA;

        if (pState->bNoHalfCursor == 0) {
            nHX = nX / 2;
            nHY = nY / 2;
        }
        cursor.nX     = pState->nCursorX;
        cursor.nY     = pState->nCursorY;
        cursor.nFlags = pState->nLayerScratch;
        ctx.pCursor          = &cursor;
        ctx.geom.nDimX       = pState->nMapDimX;
        ctx.geom.nDimY       = pState->nMapDimY;
        ctx.bBusy            = pWorld->bCursorBusy;
        ctx.bSelUnitLarge    = (pSel != NULL) && Unit_bIsLarge(pSel);
        ctx.bSuppressLarge   = pWorld->bSuppressLarge;

        MapView_SetCamOffset(&ctx.geom, nHX, nHY, true, pState->nLayerScratch,
                             pWorld->pCamOffset);

        /* 0x1443C: a drag in progress remembers where the cursor was. The original
         * reads uninitialised stack otherwise; the port reads zero. */
        if (bDown && pState->nUiSlot3 > 0) {
            nDragX = pState->nCursorX;
            nDragY = pState->nCursorY;
        }

        nOffB = Map_IsoOffsetB(pState->nMapDimY, pState->nCamScrollX, pState->nCamScrollY);
        nOffA = Map_IsoOffsetA(pState->nMapDimX, pState->nCamScrollX, pState->nCamScrollY);
        MapCursor_SetPos(&ctx, nOffA + (i8)pWorld->pCamOffset->bScratch40,
                         nOffB + (i8)pWorld->pCamOffset->bScratch41,
                         pWorld->tact.pfnLookup, pWorld->tact.pLookupUser);
        pState->nCursorX      = cursor.nX;
        pState->nCursorY      = cursor.nY;
        pState->nLayerScratch = cursor.nFlags;

        /* 0x144CC: a press that did not move the cursor is a click on the cell. */
        if (pState->bClickCommits != 0 && bDown) {
            if (pState->nUiSlot3 > 0 && nDragX == pState->nCursorX &&
                nDragY == pState->nCursorY) {
                Input_OnAction(pState, pHooks, &pWorld->bActionFlag);
                pState->nUiSlot3 = 0;
            } else {
                pState->nUiSlot3 = 8;
            }
        }

        /* 0x14534: a release that moved the cursor marks a scroll. */
        if (pState->bDragScrollA != 0 || pState->bDragScrollB != 0) {
            if (bDown) {
                pState->nUiSlot4 = 0;
            } else if (nSavedX != pState->nCursorX || nSavedY != pState->nCursorY) {
                pState->nUiSlot4 = 200;
            }
        }
        return;
    }

widgets:
    if (pState->bMultiTargetArmed != 0) {
        for (i = 0; i < 4; ++i) {
            PointerHitTestRange(pBank, &pWorld->hit, anMulti[i], 1, nX, nY);
        }
    } else {
        PointerHitTestRange(pBank, &pWorld->hit, 0, 19, nX, nY);
    }
}

/* 0x14624: the inventory screen. */
static void PointerScreen1(TactInputState *pState, const TactInputHooks *pHooks,
                           TactPointerWorld *pWorld, bool bDown, i32 nX, i32 nY)
{
    if (pState->bSlot19Active != 0) {
        return;
    }
    if (pState->bInvPanelOpen != 0 || pState->bInvDragActive == 0) {
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 19, 13, nX, nY);
    }

    if (pState->bInvPanelOpen == 0 || !bDown) {
        if (!bDown) {
            return;
        }
        if (pHooks->pfnHitTestTactItem != NULL &&
            pHooks->pfnHitTestTactItem(nX, nY, pHooks->pUser)) {
            if (pHooks->pfnInvClearHitSlot != NULL) {
                pHooks->pfnInvClearHitSlot(pHooks->pUser);
            }
            pState->bInvDragActive = 1;
        }
        return;
    }

    if (pState->bInvDragActive != 0) {
        if (pHooks->pfnInvClearHitSlot != NULL) {
            pHooks->pfnInvClearHitSlot(pHooks->pUser);
        }
        if (pHooks->pfnInvHandlePointer != NULL) {
            pHooks->pfnInvHandlePointer(pWorld->pBank, pHooks->pUser);
        }
        pState->bInvDragActive = 0;
    } else if (pHooks->pfnHitTestTactItem != NULL &&
               pHooks->pfnHitTestTactItem(nX, nY, pHooks->pUser)) {
        pState->bInvDragActive = 1;
    }
}

/* 0x14704: the look-mode screen. Three widgets, otherwise the drag moves the view. */
static void PointerScreen2(TactInputState *pState, TactPointerWorld *pWorld,
                           bool bDown, i32 nX, i32 nY)
{
    bool bHit = false;
    i32  i;

    for (i = 0; i < 3; ++i) {
        if (UiControl_HitTest(&pWorld->pBank->aCtl[98 + i], nX, nY, true, false,
                              &pWorld->hit)) {
            bHit = true;
        }
    }
    if (bHit) {
        return;
    }
    if (bDown) {
        pState->anCamDragX = pState->anCamWorldX + nX / 4;
        pState->anCamDragY = pState->anCamWorldY + nY / 4;
    } else {
        pState->anCamWorldX = pState->anCamDragX - nX / 4;
        pState->anCamWorldY = pState->anCamDragY - nY / 4;
    }
}

void Input_OnPointer(TactInputState *pState, const TactInputHooks *pHooks,
                     TactPointerWorld *pWorld, i32 nX, i32 nY, bool bDown)
{
    if (pState == NULL || pHooks == NULL || pWorld == NULL || pWorld->pBank == NULL) {
        return;
    }

    /* 0x13C98: remember the position, and clear the four HUD edge flags. */
    pWorld->pBank->nPointerX = nX;
    pWorld->pBank->nPointerY = nY;
    pState->nUiSlot5 = 0;

    if (pState->bGeoscapeUi) {
        if (pHooks->pfnGeoscapePointer != NULL) {
            pHooks->pfnGeoscapePointer(nX, nY, bDown, pHooks->pUser);
        }
        return;
    }

    if (pState->nUiScreen == 17) {
        PointerScreen17(pWorld, nX, nY);
        return;
    }
    if (!pState->bTactFlag) {
        return;
    }

    PointerStatusLine(pState, nX, nY);

    /* 0x13ECC: the cancel screen drops the fire modes and the queue before the
     * switch runs. */
    if (pState->nUiScreen == 13) {
        pState->bFireStatus2   = 0;
        pState->bTrajPad27_2   = 0;
        pState->bFireStatus1   = 0;
        pState->bQueueActive   = false;
    }

    if (!TactInput_CanScroll(pState)) {
        return;
    }

    switch (pState->nUiScreen) {
    case 0:
        PointerScreen0(pState, pHooks, pWorld, bDown, nX, nY);
        break;
    case 1:
        PointerScreen1(pState, pHooks, pWorld, bDown, nX, nY);
        break;
    case 2:
        PointerScreen2(pState, pWorld, bDown, nX, nY);
        break;
    case 3:
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 32, 12, nX, nY);
        break;
    case 4:
        (void)UiControl_HitTest(&pWorld->pBank->aCtl[101], nX, nY, true, false,
                                &pWorld->hit);
        if (pState->bPhaseSave20 == 0) {
            PointerHitTestRange(pWorld->pBank, &pWorld->hit, 102, 2, nX, nY);
        }
        break;
    case 5:
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 44, 9, nX, nY);
        break;
    case 6:
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 104, 1, nX, nY);
        break;
    case 7:
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 53, 7, nX, nY);
        break;
    case 10:
    case 11:
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 105, 8, nX, nY);
        break;
    case 12:
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 60, 29, nX, nY);
        break;
    case 13:
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 128, 1, nX, nY);
        break;
    case 15:
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 89, 9, nX, nY);
        break;
    case 19:
        PointerHitTestRange(pWorld->pBank, &pWorld->hit, 129, 1, nX, nY);
        break;
    default:
        /* 0x148C8: screens 8, 9, 14, 16, 17 and 18 answer to nothing. */
        break;
    }
}
