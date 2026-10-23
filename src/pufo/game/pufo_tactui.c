/* pufo_tactui.c - tactical action-mode, UI-screen and option-pad transitions.
 *
 * Evidence: see pufo_tactui.h for the address list.
 */
#include "game/pufo_tactui.h"

#include "game/pufo_path.h"      /* g_aStepDir, the facing delta */
#include "game/pufo_tactstate.h"

#include <string.h>

/* The sample every one of these functions plays. Only OpenCommandScreen (16),
 * OpenScreen7 (14) and OpenScreen15 (15) differ. */
#define UI_SAMPLE_DEFAULT 17

/* ------------------------------------------------------------------ helpers */

static void Ui_Play(const TactUiHooks *pHooks, i32 nSample)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(nSample, pHooks->pUser);
    }
}

static void Ui_Status(const TactUiHooks *pHooks, i32 nTextRecord)
{
    if (pHooks != NULL && pHooks->pfnStatusMsg != NULL) {
        pHooks->pfnStatusMsg(nTextRecord, pHooks->pUser);
    }
}

static void Ui_RebuildList(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pHooks != NULL) {
        Inv_BuildVisibleList(pUi, pHooks->pSelUnit);
    }
}

/* ------------------------------------------------------ the ground item list */

void Inv_BuildVisibleList(TactUiState *pUi, Soldier *pSelUnit)
{
    TactItem *pItem;
    i32 nPos;
    i32 nScan;

    if (pUi == NULL) {
        return;
    }
    /* 0x2d598: the count is cleared before anything is read. */
    pUi->nInvListCount = 0;
    if (pSelUnit == NULL || pSelUnit->pCell == NULL) {
        return;
    }
    pItem = pSelUnit->pCell->pItemList;   /* 0x2d5ac: MapCell.pItemList @ 0x34 */
    if (pItem == NULL) {
        return;
    }

    do {
        ObjectRec *pObj = pItem->pObj;   /* 0x2d5d0 */
        u8 nSel;

        /* 0x2d5dc: an item whose object is two-handed mode 2 is never listed. */
        if (pObj != NULL && pObj->bTwoHanded != 2) {
            nSel = pUi->nInvListSel;     /* 0x4A26 */
            if (nSel == 0xFFu || pObj->nCategory == nSel) {
                i32 nMatch = -1;

                for (nScan = 0; nScan < (i32)pUi->nInvListCount; ++nScan) {
                    if (pUi->apInvList[nScan]->pObj == pObj) {
                        nMatch = nScan;
                        break;
                    }
                }
                if (nMatch >= 0) {
                    /* 0x2d654: keep the copy with the larger ammo, `existing < new`,
                     * so an equal ammo keeps the entry already in the list. */
                    if (pUi->apInvList[nMatch]->nAmmo < pItem->nAmmo) {
                        pUi->apInvList[nMatch] = pItem;
                    }
                    pUi->anInvCount[nMatch] =
                        (u16)(pUi->anInvCount[nMatch] + 1u);   /* 0x2d680 */
                } else if ((i32)pUi->nInvListCount < PUFO_INV_LIST_MAX) {
                    pUi->anInvCount[pUi->nInvListCount] = 1;   /* 0x2d69c */
                    pUi->apInvList[pUi->nInvListCount] = pItem; /* 0x2d6a8 */
                    pUi->nInvListCount = (u16)(pUi->nInvListCount + 1u);
                }
            }
        }
        pItem = pItem->pNext;
    } while (pItem != NULL);

    /* 0x2d6d0..0x2d78c: selection sort, ascending by ObjectRec.uSortKey @ 0xC8.
     * The ARM runs the outer pass while pos + 1 < count and scans pos+1 .. count-1,
     * swapping the count array in step with the pointer array. */
    for (nPos = 0; nPos + 1 < (i32)pUi->nInvListCount; ++nPos) {
        for (nScan = nPos + 1; nScan < (i32)pUi->nInvListCount; ++nScan) {
            if (pUi->apInvList[nScan]->pObj->uSortKey <
                pUi->apInvList[nPos]->pObj->uSortKey) {
                TactItem *pTmpItem = pUi->apInvList[nPos];
                u16 nTmpCount = pUi->anInvCount[nPos];

                pUi->apInvList[nPos] = pUi->apInvList[nScan];
                pUi->apInvList[nScan] = pTmpItem;
                pUi->anInvCount[nPos] = pUi->anInvCount[nScan];
                pUi->anInvCount[nScan] = nTmpCount;
            }
        }
    }
}

/* ------------------------------------------------------------- inventory pads */

/* Which pad each filter number turns on. This mapping is measured from the seven
 * functions and is deliberately NOT the pad's address order:
 *
 *     filter 0 -> pad[0]      filter 3 -> pad[1]
 *     filter 1 -> pad[3]      filter 4 -> pad[2]
 *     filter 2 -> pad[4]      filter 5 -> the X pad
 *
 * It is an original quirk, not a transcription error. */
typedef enum FilterPad {
    FILTER_PAD_0 = 0,
    FILTER_PAD_1,
    FILTER_PAD_2,
    FILTER_PAD_3,
    FILTER_PAD_4,
    FILTER_PAD_X,
    FILTER_PAD_ALL
} FilterPad;

static const FilterPad s_aFilterPad[6] = {
    FILTER_PAD_0, FILTER_PAD_3, FILTER_PAD_4, FILTER_PAD_1, FILTER_PAD_2, FILTER_PAD_X
};

bool Ui_SetInvFilter(TactUiState *pUi, i32 nFilter, const TactUiHooks *pHooks)
{
    FilterPad ePad;

    if (pUi == NULL) {
        return false;
    }
    if (nFilter == PUFO_INV_FILTER_ALL) {
        ePad = FILTER_PAD_ALL;
    } else if (nFilter >= 0 && nFilter < 6) {
        ePad = s_aFilterPad[nFilter];
    } else {
        return false;
    }

    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);

    /* Every one of the seven clears all seven pads first. */
    pUi->bFilterPadX = 0;
    pUi->bFilterPadAll = 0;
    memset(pUi->abFilterPad, 0, sizeof(pUi->abFilterPad));

    switch (ePad) {
        case FILTER_PAD_X:
            pUi->bFilterPadX = 1;
            break;
        case FILTER_PAD_ALL:
            pUi->bFilterPadAll = 1;
            break;
        default:
            pUi->abFilterPad[(int)ePad] = 1;
            break;
    }

    pUi->nInvFilterType = 0;
    /* All selects -1; every numbered filter selects its own number. */
    pUi->nInvListSel = (nFilter == PUFO_INV_FILTER_ALL) ? (u8)-1 : (u8)nFilter;

    Ui_RebuildList(pUi, pHooks);
    return true;
}

void Ui_InvScrollUp(TactUiState *pUi)
{
    if (pUi != NULL && pUi->nInvFilterType != 0) {
        pUi->nInvFilterType -= 1;
    }
}

void Ui_InvScrollDown(TactUiState *pUi)
{
    if (pUi == NULL) {
        return;
    }
    /* The original compares an UNSIGNED widened value: `(u32)nInvFilterType + 4
     * < nInvListCount`. Kept as written; with the u8 widened first, a value near
     * 252 would wrap, which is what the original does too. */
    if ((u32)pUi->nInvFilterType + 4u < (u32)pUi->nInvListCount) {
        pUi->nInvFilterType += 1;
    }
}

bool Ui_SelectRadio(TactUiState *pUi, i32 nRadio, const TactUiHooks *pHooks)
{
    if (pUi == NULL || nRadio < 0 || nRadio >= 4) {
        return false;
    }
    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    memset(pUi->abRadio, 0, sizeof(pUi->abRadio));
    pUi->abRadio[nRadio] = 1;
    return true;
}

/* ------------------------------------------------------------ screen switches */

void Ui_OpenCommandScreen(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, 16);
    pUi->bUnk1508C = 0;
    pUi->nUiScreen = 4;
}

void Ui_OpenScreen7(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    pUi->nUiScreen = 7;
    Ui_Play(pHooks, 14);
}

void Ui_OpenScreen15(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    pUi->nUiScreen = 15;
    Ui_Play(pHooks, 15);
}

/* ------------------------------------------------------------- cursor layers */

void Ui_LayerUp(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    /* The original loads the byte SIGNED and only steps while it is < 4. */
    if ((i8)pUi->nLayerScratch < PUFO_LAYER_MAX) {
        u8 nNew = (u8)(pUi->nLayerScratch + 1);

        pUi->nLayerScratch = nNew;
        if (pHooks != NULL && pHooks->pfnSetCursorFlags != NULL) {
            pHooks->pfnSetCursorFlags(nNew, pHooks->pUser);
        }
    }
}

void Ui_LayerDown(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    /* The upper bound is 1, not 0, so layer 0 is unreachable through this path. */
    if ((i8)pUi->nLayerScratch > PUFO_LAYER_MIN) {
        u8 nNew = (u8)(pUi->nLayerScratch - 1);

        pUi->nLayerScratch = nNew;
        if (pHooks != NULL && pHooks->pfnSetCursorFlags != NULL) {
            pHooks->pfnSetCursorFlags(nNew, pHooks->pUser);
        }
    }
}

/* ----------------------------------------------------------------- fire modes */

/* Tact_SetFireModeAimed/Snap/Auto share one body. `nMode` is the nActionMode
 * value and `nUnk40Value` the separate byte the original writes. */
bool Ui_SetFireMode(TactUiState *pUi, i32 nMode, const FireModeWeapon *pWeapon,
                    const TactUiHooks *pHooks)
{
    i32 nUnk40Value;

    if (pUi == NULL) {
        return false;
    }
    switch (nMode) {
        case PUFO_ACTION_MODE_AUTO:  nUnk40Value = 3; break;
        case PUFO_ACTION_MODE_SNAP:  nUnk40Value = 1; break;
        case PUFO_ACTION_MODE_AIMED: nUnk40Value = 1; break;
        default:
            return false;
    }

    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);

    /* The refusal test: an empty weapon whose object does NOT have capacity 255.
     * 255 means unlimited, so an empty-but-unlimited weapon is allowed. The port
     * additionally refuses a NULL weapon, which the original would dereference
     * (documented deviation: a crash is not behaviour worth reproducing). */
    if (pWeapon == NULL) {
        Ui_Status(pHooks, PUFO_TXT_OUT_OF_AMMO);
        return false;
    }
    if (pWeapon->nAmmo == 0 && pWeapon->bAmmoCap != 255) {
        Ui_Status(pHooks, PUFO_TXT_OUT_OF_AMMO);
        return false;
    }

    pUi->nActionMode = nMode;
    pUi->nUnk40 = nUnk40Value;
    return true;
}

void Ui_SetMode11(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    pUi->nActionMode = PUFO_ACTION_MODE_MENU;
}

bool Ui_TryFireMode(TactUiState *pUi, i32 nMode, u16 uTu, const TactUiHooks *pHooks)
{
    i32 nCost;

    if (pUi == NULL) {
        return false;
    }
    if (nMode < 0 || nMode >= PUFO_ACTION_MODE_COUNT) {
        return false;
    }

    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);

    /* nActionMode is set to the requested mode BEFORE the affordability test, and
     * the failure path overwrites it with 12. */
    pUi->nActionMode = nMode;
    nCost = pUi->anActionCost[nMode];

    if ((i32)uTu >= nCost) {
        pUi->bActionLatch = PUFO_ACTION_LATCH_FIRE;
        pUi->nUnk40 = 0;
        pUi->nUiScreen = 0;
        return true;
    }

    pUi->nUiScreen = 0;
    pUi->nActionMode = PUFO_ACTION_MODE_NONE;
    pUi->bActionLatch = PUFO_ACTION_LATCH_NONE;
    pUi->nUnk40 = 0;
    Ui_Status(pHooks, PUFO_TXT_OUT_OF_TU);   /* 0x17A54: StatusMsg_Push with aTextRec1_OutOfTime, record 1 */
    return false;
}

/* ------------------------------------------------------------ action costs */

/* The original multiplies two u16/u8 operands through the ARM soft-double helpers
 * and then multiplies by the pair (1202590843, 1065646817), which is the IEEE754
 * double 0x3F847AE147AE147B = 0.01. The product is truncated to int. */
#define PUFO_ACTION_COST_SCALE 0.01

void Ui_FillActionCosts(const ActionCostInput *pIn, TactUiState *pUi)
{
    if (pIn == NULL || pUi == NULL) {
        return;
    }

    for (i32 m = 0; m < PUFO_ACTION_MODE_COUNT; ++m) {
        i32 nPct = 0;      /* v5: the percentage of auStat[0] */
        i32 nEnabled = 0;  /* v4: whether the mode is offered */

        switch (m) {
            case 0:
            case 1:
            case 2:
                /* Only for weapons that are neither type 13 nor 35. */
                if (pIn->uSortKey != 13 && pIn->uSortKey != 35) {
                    nEnabled = (i32)((double)pIn->auStat4
                                     * (double)pIn->abCostPctPacked[m]
                                     * PUFO_ACTION_COST_SCALE);
                    /* Kneeling (bFlags bit 0) raises the price by a quarter. */
                    if ((pIn->bFlags & 1u) != 0) {
                        nEnabled = nEnabled + nEnabled / 4;
                    }
                    nPct = (i32)pIn->abFlagByMode[m];
                }
                break;

            case 3:
                /* Enabled only for a category-3 object with bUnk8 clear and a sort
                 * key that is not 14. The percentage is always 20. */
                nPct = 20;
                if (pIn->nCategory == 3 && pIn->bWeaponUnk8 == 0 && pIn->uSortKey != 14) {
                    nEnabled = 1;
                }
                break;

            case 4:
            case 10:
                nPct = 34;
                nEnabled = (pIn->uSortKey == 24) ? 1 : 0;
                break;

            case 5:
                nPct = 50;
                nEnabled = (pIn->uSortKey == 24) ? 1 : 0;
                break;

            case 6:
                if (pIn->uSortKey == 22) {
                    nEnabled = 1;
                    nPct = 40;
                }
                break;

            case 7:
                if (pIn->uSortKey == 13 || pIn->uSortKey == 35) {
                    nPct = (i32)pIn->abFlagByMode[2];
                    nEnabled = 1;
                }
                break;

            case 8:
                if (pIn->uSortKey == 20 || pIn->uSortKey == 21 || pIn->uSortKey == 23) {
                    nEnabled = 1;
                    nPct = (pIn->uSortKey == 21) ? 80 : 40;
                }
                break;

            case 9:
                if (pIn->uSortKey == 21) {
                    nEnabled = 1;
                    nPct = 80;
                }
                break;

            case 11:
                if (pIn->uSortKey < 0x1F || pIn->uSortKey > 0x23) {
                    nPct = 35;
                    nEnabled = (i32)pIn->auStat5;
                }
                /* An operative non-human unit never gets mode 11. */
                if (pIn->nUnitType > 3u && pIn->bOperative) {
                    nEnabled = 0;
                }
                break;

            default:
                nPct = 1;
                nEnabled = 0;
                break;
        }

        pUi->anActionCost[m] = (i32)pIn->auStat0 * nPct / 100;
        pUi->anActionEnabled[m] = nEnabled;

        /* 0x26404/0x2640c/0x2641c: the same flag again, as the bEnabled byte of the bank's
         * twelve fire-mode widgets. The ARM takes the boolean of the enabled value --
         * `CMP R5,#0 / MOV R2,#0 / MOVNE R2,#1` -- so a mode worth 30 TU enables its pad with
         * 1, not with 30. UiControl_HitTest (0x1A3F0) refuses a widget whose bEnabled is 0,
         * which is what keeps an unoffered row of the roster unclickable. */
        if (pIn->pActionPads != NULL) {
            pIn->pActionPads[m].bEnabled = (u8)(nEnabled != 0);
        }
    }
}

/* ------------------------------------------------------ the hand action menus */

/* aTextRecs record 1, "not enough time", at 0xB38D0 + 0xCD. */
#define PUFO_TXTREC_NO_TIME 1

bool Ui_OpenHandMenu(TactUiState *pUi, const HandMenuCtx *pCtx,
                     bool (*pfnWeaponHasMenu)(u16 uSortKey, void *pUser),
                     const TactUiHooks *pHooks, void *pGateUser)
{
    if (pUi == NULL || pCtx == NULL || pCtx->pCost == NULL) {
        return false;
    }

    Ui_Play(pHooks, 17);

    /* 0x1704C: any armed action blocks the menu. */
    if (pCtx->bActionLatch != 0) {
        return false;
    }

    /* 0x17090: the weapon's research record must offer an action menu. */
    if (pfnWeaponHasMenu == NULL || !pfnWeaponHasMenu(pCtx->uSortKey, pGateUser)) {
        return false;
    }

    /* 0x17098: sort key 24 needs the unit byte at +0x1A6. */
    if (pCtx->uSortKey == 24 && pCtx->bByte1A6 == 0) {
        return false;
    }

    Ui_FillActionCosts(pCtx->pCost, pUi);

    /* 0x170D0..0x17108: twelve four-byte steps looking for any mode that is both
     * offered and affordable. The found mode is not kept. */
    for (i32 m = 0; m < PUFO_ACTION_MODE_COUNT; ++m) {
        if (pUi->anActionEnabled[m] != 0 && pUi->anActionCost[m] <= (i32)pCtx->uTu) {
            pUi->nActionMode = 12;
            pUi->nUiScreen = 3;
            return true;
        }
    }

    Ui_Status(pHooks, PUFO_TXTREC_NO_TIME);
    return false;
}

/* ---------------------------------------------------------- weapon sub-modes */

bool Ui_SetSubMode(TactUiState *pUi, TactItem *pWeapon, Soldier *pUnit,
                   i32 nSubMode, const TactUiHooks *pHooks)
{
    if (pUi == NULL || pWeapon == NULL || pUnit == NULL) {
        return false;
    }
    if (nSubMode < 0 || nSubMode > 8) {
        return false;
    }
    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);

    /* The three writes, in the original's order: mark the weapon as "sub-mode
     * active", record which sub-mode, then charge the current action mode's TU
     * cost to the unit. */
    pWeapon->bUnk8 = 1;
    pWeapon->bUnk7 = (u8)nSubMode;
    /* Plain u16 wrap-down: the original does not clamp at zero. */
    pUnit->uTu = (u16)(pUnit->uTu - (u16)pUi->anActionCost[pUi->nActionMode]);
    return true;
}

/* ------------------------------------------------------------- option toggles */

/* The five pads, in id order. Each pad mirrors its option byte. */
static u8 *Toggle_Pad(TactUiState *pUi, i32 nWhich)
{
    if (nWhich < 0 || nWhich >= TACT_TOGGLE_COUNT) {
        return NULL;
    }
    return &pUi->abTogglePad[nWhich];
}

/* The option byte each id drives. These are the SAME bytes the options screen
 * reads: anActionPhaseSave0[93..97] = 0x150D5..0x150D9. */
static u8 *Toggle_OptionByte(TactUiState *pUi, i32 nWhich)
{
    switch (nWhich) {
        case TACT_TOGGLE_SCROLL: return &pUi->bScrollByPen;
        case TACT_TOGGLE_SHOW:   return &pUi->bShowBorder;
        case TACT_TOGGLE_RUN:    return &pUi->bRunByDblClick;
        case TACT_TOGGLE_TURN:   return &pUi->bTurnByPen;
        case TACT_TOGGLE_MENU:   return &pUi->bMenuByPen;
        default:                 return NULL;
    }
}

bool Ui_ToggleOption(TactUiState *pUi, i32 nWhich, const TactUiHooks *pHooks)
{
    u8 *pByte;
    u8 *pPad;

    if (pUi == NULL) {
        return false;
    }
    pByte = Toggle_OptionByte(pUi, nWhich);
    pPad  = Toggle_Pad(pUi, nWhich);
    if (pByte == NULL || pPad == NULL) {
        return false;
    }

    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);

    switch (nWhich) {
        case TACT_TOGGLE_SCROLL:
            if (*pByte != 0) {
                /* Turning scroll off also clears show and its pad. */
                pUi->bShowBorder = 0;
                pUi->abTogglePad[TACT_TOGGLE_SHOW] = 0;
                *pByte = 0;
                *pPad = 0;
            } else {
                *pByte = 1;
                *pPad = 1;
            }
            break;

        case TACT_TOGGLE_SHOW:
            /* Unlike the others this one does not flip: it recomputes from
             * scroll, so show can only be on while scroll is on. */
            if (pUi->bShowBorder == 0 && pUi->bScrollByPen != 0) {
                *pByte = 1;
                *pPad = 1;
            } else {
                *pByte = 0;
                *pPad = 0;
            }
            break;

        case TACT_TOGGLE_RUN:
            if (*pByte != 0) {
                *pByte = 0;
                *pPad = 0;
            } else {
                *pByte = 1;
                *pPad = 1;
            }
            break;

        case TACT_TOGGLE_TURN:
            if (*pByte != 0) {
                *pByte = 0;
                *pPad = 0;
            } else {
                /* Mutually exclusive with the menu option. */
                *pByte = 1;
                *pPad = 1;
                pUi->bMenuByPen = 0;
                pUi->abTogglePad[TACT_TOGGLE_MENU] = 0;
            }
            break;

        case TACT_TOGGLE_MENU:
            if (*pByte != 0) {
                *pByte = 0;
                *pPad = 0;
            } else {
                *pByte = 1;
                *pPad = 1;
                pUi->bTurnByPen = 0;
                pUi->abTogglePad[TACT_TOGGLE_TURN] = 0;
            }
            break;

        default:
            return false;
    }
    return true;
}

/* ----------------------------------------------------------- screen helpers */

void Ui_ClearScreen(TactUiState *pUi)
{
    if (pUi != NULL) {
        pUi->nUiScreen = 0;
    }
}

void Ui_Samples16_14_ClearScreen(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, 16);
    Ui_Play(pHooks, 14);
    pUi->nUiScreen = 0;
}

void Ui_Screen7_Samples16_14(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    pUi->nUiScreen = 7;
    Ui_Play(pHooks, 16);
    Ui_Play(pHooks, 14);
}

void Ui_Samples16_7_SetDeployFlag(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, 16);
    Ui_Play(pHooks, 7);
    pUi->bDeployPanelOpen = 1;
    pUi->nUiScreen = 0;
}

void Ui_Samples16_7_ClearDeployFlag(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, 16);
    Ui_Play(pHooks, 7);
    pUi->bDeployPanelOpen = 0;
    pUi->nUiScreen = 0;
}

void Ui_OpenCommandScreenFlagged(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, 16);
    pUi->bSlot80Low = 1;
    pUi->nUiScreen = 4;
}

void Ui_IncLayer(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    if ((i8)pUi->nLayerScratch < PUFO_LAYER_MAX) {
        pUi->nLayerScratch = (u8)(pUi->nLayerScratch + 1);
    }
}

void Ui_DecLayer(TactUiState *pUi, const TactUiHooks *pHooks)
{
    if (pUi == NULL) {
        return;
    }
    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    if ((i8)pUi->nLayerScratch > PUFO_LAYER_MIN) {
        pUi->nLayerScratch = (u8)(pUi->nLayerScratch - 1);
    }
}

/* ------------------------------------------------------- action confirmations */

/* TactAction_ConfirmUseItem @ 0x178d4. Action mode 3. */
i32 Ui_ConfirmUseItem(TactUiState *pUi, Soldier *pUnit, TactItem *pWeapon,
                      u16 uSortKey, const TactUiHooks *pHooks)
{
    if (pUi == NULL || pUnit == NULL || pWeapon == NULL) {
        return -1;
    }

    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    pUi->nActionMode = 3;

    /* The original reads `nTrajFront0[1052]`, which is the mode-3 entry of the same
     * table Ui_FillActionCosts fills (4*3 + 1040). */
    if (pUi->anActionCost[3] <= (i32)pUnit->uTu) {
        pUi->bActionLatch = PUFO_ACTION_LATCH_NONE;
        pUi->nUnk40 = 0;
        if (uSortKey == 14) {
            /* The instant-use path: spend the TU now and mark the weapon. */
            pUi->nUiScreen = 0;
            pWeapon->bUnk8 = 1;
            pUnit->uTu = (u16)(pUnit->uTu - (u16)pUi->anActionCost[pUi->nActionMode]);
            return 0;
        }
        /* Anything else opens the item sub-screen; no TU is spent yet. */
        pUi->nUiScreen = 5;
        return 5;
    }

    /* Not enough TU: report it and fall back to the "no action" mode. */
    Ui_Status(pHooks, PUFO_TXT_OUT_OF_AMMO);
    pUi->nUiScreen = 0;
    pUi->bActionLatch = PUFO_ACTION_LATCH_NONE;
    pUi->nUnk40 = 0;
    pUi->nActionMode = PUFO_ACTION_MODE_NONE;
    return PUFO_ACTION_MODE_NONE;
}

/* TactAction_ConfirmFireBurst @ 0x18774. */
bool Ui_ConfirmFireBurst(TactUiState *pUi, TactState *pQueue, Soldier *pUnit,
                         TactItem *pWeapon, MapCell **apMultiTargets,
                         i32 *pnMultiTargetCount, u16 uAmmoInWeapon, u8 bAmmoCap,
                         void (*pfnFaceToward)(Soldier *pUnit, i32 nX, i32 nY,
                                               void *pUser),
                         void *pUser, const TactUiHooks *pHooks)
{
    FireBurstSpec spec;
    i32           nCount;
    i32           nMode;
    bool          bEnqueued;

    if (pUi == NULL || pQueue == NULL || pUnit == NULL || pnMultiTargetCount == NULL) {
        return false;
    }
    nCount = *pnMultiTargetCount;

    Ui_Play(pHooks, 16);
    pUi->bActionLatch = PUFO_ACTION_LATCH_NONE;
    /* The original publishes the target count into nUnk40 before clearing it. */
    pUi->nUnk40 = nCount;

    /* Clear the targeting marker (bFlags29 bit 0x40) on every multi-target cell.
     * The original loop runs `count` times from index 0. */
    if (apMultiTargets != NULL) {
        for (i32 i = 0; i < nCount; ++i) {
            if (apMultiTargets[i] != NULL) {
                apMultiTargets[i]->bFlags29 &= (u8)~0x40u;
            }
        }
    }
    *pnMultiTargetCount = 0;

    /* The burst is aimed at the FIRST multi-target cell. The original dereferences
     * it unconditionally, which is a NULL read when the count is zero; the port
     * refuses instead (documented deviation). */
    if (nCount <= 0 || apMultiTargets == NULL || apMultiTargets[0] == NULL) {
        return false;
    }

    nMode = pUi->nActionMode;

    memset(&spec, 0, sizeof(spec));
    spec.pUnit = pUnit;
    spec.pWeapon = pWeapon;
    spec.nMode = nMode;
    spec.nX = apMultiTargets[0]->nX;
    spec.nY = apMultiTargets[0]->nY;
    spec.nZ = apMultiTargets[0]->nZ;
    spec.nTuCost = pUi->anActionCost[nMode];
    spec.nArg = pUi->anActionEnabled[nMode];
    spec.uAmmoInWeapon = uAmmoInWeapon;
    spec.bAmmoCap = bAmmoCap;
    /* Mode 7 is the shot count AND the coordinates: 0x4B144 takes nAmmo from Map_nBurstShots, which
     * 0x187C0 published from the multi-target count moments earlier, and 0x4B198..0x4B1D8 take every
     * shot's cell from unk_1139C0[i] -- the same list case 3 (0x15344) appended. Without these two
     * fields a confirmed multi-target burst wrote ZERO shot entries: the loop at 0x4B150 ran
     * nShots == 0 times and only the aim-clear entry (0x4B2E4) reached the queue, so the shot never
     * resolved and no HP ever came off the target. The count is the published one, not the cleared
     * global: 0x187F4 zeroes unk_1139BC only after 0x187C0 has copied it. */
    spec.apMultiTargets    = apMultiTargets;
    spec.nMultiTargetCount = nCount;

    /* 0x18844 then 0x1884C: the enqueue's refusal is the callee's own work -- 0x4B0D8 returns
     * StatusMsg_Push(aTextRec1_OutOfTime, 0) when the unit cannot pay the mode's cost -- and the kick
     * at 0x1884C runs whatever the enqueue did. The port's helper only reports the failure, so the
     * push that the original does inside the callee is done here; the order is the same (message,
     * then kick). */
    if (TactState_EnqueueFireBurst(pQueue, &spec, pfnFaceToward, pUser)) {
        bEnqueued = true;
    } else {
        Ui_Status(pHooks, PUFO_TXT_OUT_OF_TU); /* record 1; this file's PUFO_TXTREC_NO_TIME names it too */
        bEnqueued = false;
    }
    TactState_KickQueue(pQueue); /* 0x1884C, unconditional in the original */
    return bEnqueued;
}

/* -------------------------------------------------- combat-menu item queries */

/* TactItem_GetHandSlot @ 0x3e620. Transcribed branch by branch from the
 * decompilation: the id sets are irregular, so this mirrors the original's nesting
 * rather than inventing a table. */
i32 TactItem_GetHandSlot(i32 nActionId)
{
    if (nActionId <= 19) {
        if (nActionId == 19) {
            return HAND_SLOT_BOTH;      /* 2 */
        }
        if (nActionId == 14) {
            return HAND_SLOT_LEFT;      /* 1 */
        }
        if (nActionId != 15) {
            if (nActionId != 16) {
                if (nActionId != 17) {
                    /* 18 -> 1, everything else in range -> 0 (right). */
                    return nActionId == 18 ? HAND_SLOT_LEFT : HAND_SLOT_RIGHT;
                }
                return HAND_SLOT_NONE;  /* 17 -> -1 */
            }
            return HAND_SLOT_RIGHT;     /* 16 -> 0 */
        }
        return HAND_SLOT_NONE;          /* 15 -> -1 */
    }
    if (nActionId != 36) {
        if (nActionId != 37 && nActionId != 38) {
            /* 39 -> 1, anything else -> 0. */
            return nActionId == 39 ? HAND_SLOT_LEFT : HAND_SLOT_RIGHT;
        }
        return HAND_SLOT_NONE;          /* 37, 38 -> -1 */
    }
    return HAND_SLOT_SPECIAL;           /* 36 -> -2 */
}

/* TactItem_GetEquipDelta @ 0x3e69c. Same transcription approach. */
i32 TactItem_GetEquipDelta(i32 nActionId)
{
    if (nActionId <= 19) {
        if (nActionId != 19) {
            if (nActionId == 14 || nActionId == 15 || nActionId == 16) {
                return -1;
            }
            if (nActionId != 17) {
                if (nActionId == 18) {
                    return -1;
                }
                return 0;
            }
            /* 17 falls through to the -2 return below. */
        }
        return -2;                      /* 19 and 17 */
    }
    if (nActionId != 36) {
        if (nActionId == 37) {
            return 1;
        }
        if (nActionId != 38) {
            if (nActionId == 39) {
                return -1;
            }
            return 0;
        }
    }
    return -3;                          /* 36 and 38 */
}

/* TactAction_ConfirmMeleeMode6 @ 0x17bb8. */
bool Ui_ConfirmMeleeMode6(TactUiState *pUi, TactState *pQueue, Soldier *pUnit,
                          TactItem *pWeapon, const MapGeom *pGeom,
                          MapCellLookup pfnLookup, void *pLookupUser,
                          const TactUiHooks *pHooks)
{
    MapCell        *pOrigin;
    MapCell        *pTarget;
    const StepDir  *pDir;
    i32             nX;
    i32             nY;
    i32             nZ;
    bool            bOk;

    if (pUi == NULL || pQueue == NULL || pUnit == NULL || pGeom == NULL
        || pfnLookup == NULL) {
        return false;
    }

    /* 0x17BC8: sample 17. The MOV R1, #4 before it is dead -- Sound_Play @ 0x9e0c0
     * takes one argument. 0x17BD8 then commits the mode before any gate runs. */
    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    pUi->nActionMode = PUFO_ACTION_MODE_MELEE;

    /* Deviation: 0x17BEC dereferences pUnit->pCell with no NULL test. */
    pOrigin = pUnit->pCell;
    if (pOrigin == NULL) {
        return false;
    }

    /* 0x17BF4..0x17C14: the candidate cell is the unit's own cell shifted by
     * aDirDeltaXY[3*bFacing]. The layer is the unit's own -- bFacing is 0..7, so the
     * table's two vertical entries (8 and 9) are never reachable here. */
    pDir = &g_aStepDir[pUnit->bFacing % PUFO_PATH_DIR_COUNT];
    nX = (i32)pOrigin->nX + pDir->dx;
    nY = (i32)pOrigin->nY + pDir->dy;
    nZ = (i32)pOrigin->nZ;

    /* Gate 1, 0x17C1C: the wall probe. R2 still holds bFacing at the BL, which is how
     * the port's nDir argument is sourced. */
    bOk = Unit_CanStepExitDir(pOrigin, pUnit->bFacing, pGeom, pfnLookup, pLookupUser);
    if (!bOk) {
        Ui_Status(pHooks, PUFO_TXT_MELEE_NEED_TARGET);
        Ui_Status(pHooks, PUFO_TXT_MELEE_NEED_TARGET_2);
        return false;
    }

    /* Gate 2, 0x17C54..0x17C8C. The original indexes apCells directly; the port goes
     * through the lookup and refuses on NULL (deviation). pMeleeTarget is stored at
     * 0x17C64 BEFORE the occupant test, so it keeps the candidate's -- possibly NULL --
     * occupant even when the gate then fails. */
    if (bOk) {
        pTarget = pfnLookup(nX, nY, nZ, pLookupUser);
        if (pTarget == NULL) {
            bOk = false;
        } else {
            pUi->pMeleeTarget = pTarget->pUnit;
            if (!MapCell_HasOccupant(pTarget) || Unit_bIsLarge(pUi->pMeleeTarget)) {
                bOk = false;
            }
        }
    }

    /* 0x17D0C: every failure route joins here and pushes the same two-line refusal. */
    if (!bOk) {
        Ui_Status(pHooks, PUFO_TXT_MELEE_NEED_TARGET);
        Ui_Status(pHooks, PUFO_TXT_MELEE_NEED_TARGET_2);
        return false;
    }

    /* Success, 0x17C90..0x17CF4: the melee triple takes the CANDIDATE coordinates, the
     * command-18 action is enqueued at the mode-6 cost, and the queue is kicked. */
    pUi->nFireTgtX = nX;
    pUi->nFireTgtY = nY;
    pUi->nFireTgtZ = nZ;
    if (!TactState_EnqueueAction18(pQueue, pUnit, pUi->pMeleeTarget, pWeapon,
                                   pUi->anActionCost[PUFO_ACTION_MODE_MELEE],
                                   PUFO_ACTION_MODE_MELEE)) {
        Ui_Status(pHooks, PUFO_TXT_OUT_OF_TU);
    }
    TactState_KickQueue(pQueue);
    return true;
}

/* TactAction_ConfirmSpecial9 @ 0x18260. */
bool Ui_ConfirmSpecial9(TactUiState *pUi, TactState *pQueue, Soldier *pUnit,
                        TactItem *pWeapon, MapCell *pCell,
                        const TactUiHooks *pHooks)
{
    if (pUi == NULL || pQueue == NULL || pUnit == NULL || pCell == NULL) {
        return false;
    }

    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    pUi->nActionMode = 9;
    pUi->nUiScreen = 0;

    /* The "nothing to do" guard: not kneel/primed AND at full HP AND full morale
     * AND having spent TU. All four must hold for the refusal. */
    if (pUnit->bKneelOrPrimed == 0
        && pUnit->uHp == pUnit->auStat[1]
        && pUnit->bMorale == 100
        && pUnit->uTuSpent != 0) {
        /* 0x182D8 / 0x182F0: the two records are unk_B419F (11) and unk_B426C (12),
         * not the out-of-ammo record the port used to push twice. */
        Ui_Status(pHooks, PUFO_TXT_MEDKIT_NOT_NEEDED);
        Ui_Status(pHooks, PUFO_TXT_MEDKIT_NOT_NEEDED_2);
        return false;
    }

    /* Otherwise the target is the unit's OWN cell, and 0x18364 enqueues the action with
     * the selected unit as BOTH actor and target (MOV R1, R0 with R0 still holding
     * Map.pSelUnit) at the mode-9 cost, then 0x18370 kicks the queue. */
    pUi->nFireTgtX = pCell->nX;
    pUi->nFireTgtY = pCell->nY;
    pUi->nFireTgtZ = pCell->nZ;
    if (!TactState_EnqueueAction18(pQueue, pUnit, pUnit, pWeapon,
                                   pUi->anActionCost[9], 9)) {
        Ui_Status(pHooks, PUFO_TXT_OUT_OF_TU);
    }
    TactState_KickQueue(pQueue);
    return true;
}

/* Tact_BeginMultiTarget @ 0x18380. */
i32 Ui_BeginMultiTarget(TactUiState *pUi, const FireModeWeapon *pWeapon,
                        i32 *pnMultiTargetCount, i32 nCursorX, i32 nCursorY,
                        void (*pfnSetCursor)(i32 nX, i32 nY, void *pUser),
                        void *pUser, const TactUiHooks *pHooks)
{
    if (pUi == NULL || pnMultiTargetCount == NULL) {
        return -1;
    }

    Ui_Play(pHooks, UI_SAMPLE_DEFAULT);
    pUi->nActionMode = 7;

    /* The same empty-weapon guard as the fire modes: an empty magazine refuses
     * unless the capacity is 255 (unlimited). */
    if (pWeapon == NULL || (pWeapon->nAmmo == 0 && pWeapon->bAmmoCap != 255)) {
        Ui_Status(pHooks, PUFO_TXT_OUT_OF_AMMO);
        return -1;
    }

    /* Park the cursor at its current cell, then start the selection. */
    if (pfnSetCursor != NULL) {
        pfnSetCursor(nCursorX, nCursorY, pUser);
    }
    *pnMultiTargetCount = 0;
    /* Latch 3 means "picking targets", not 4 which means "fire". */
    pUi->bActionLatch = PUFO_ACTION_LATCH_TARGETING;
    return PUFO_ACTION_LATCH_TARGETING;
}

/* Tact_MapTypeToGlyphIdx @ 0x46664. */
i32 Tact_MapTypeToGlyphIdx(u8 nType, const u8 *pProbeTable, i32 nProbeCount)
{
    /* The threshold is on the u8 value, so a type above 255 cannot reach here. */
    if (nType <= 3u) {
        return (i32)nType + 5;
    }
    if (nType < 9u) {
        return 18;
    }
    /* These two are checked BEFORE the table probe despite being above 9. */
    if (nType == 16u) {
        return 14;
    }
    if (nType == 17u) {
        return 13;
    }

    /* The probe: the original walks four bytes and stops on the first match, so a
     * duplicate type resolves to the FIRST matching index. */
    if (pProbeTable != NULL && nProbeCount > 0) {
        for (i32 i = 0; i < nProbeCount; ++i) {
            if (pProbeTable[i] == nType) {
                return i + 9;
            }
        }
    }
    return (i32)nType + 5;
}

/* TactItem_bTryReloadClip @ 0x4b6a4. */
bool TactItem_TryReloadClip(TactItem *pItem, u8 bWeaponAmmoCap, u16 nAmmoTypeId,
                            Soldier *pOwner,
                            u8 (*pfnClipAmmoCap)(u16 nAmmoTypeId, void *pUser),
                            void *pUser)
{
    if (pItem == NULL) {
        return false;
    }
    if (pItem->nAmmo == 0 && bWeaponAmmoCap != 255
        && pOwner != NULL && pOwner->bHumanoidInv != 0) {
        --pOwner->bHumanoidInv;
        pItem->nAmmo = (pfnClipAmmoCap != NULL)
            ? pfnClipAmmoCap(nAmmoTypeId, pUser)
            : 0;
    }
    if (pItem->nAmmo != 0) {
        return true;
    }
    return bWeaponAmmoCap == 255;
}
