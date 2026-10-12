/* Tactical HUD action buttons. See pufo_tacthud.h. */

#include "game/pufo_tacthud.h"

#include "game/pufo_path.h"
#include "game/pufo_boot.h"
#include "game/pufo_font.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static MapCell *HudCell(const TactHudCtx *pCtx, i32 nX, i32 nY, i32 nLayer)
{
    return pCtx->pfnLookup(nX, nY, nLayer, pCtx->pLookupUser);
}

/* TactHud_ToggleKneel @ 0x16570.
 *
 * Gate, in the ARM order at 0x16584..0x165CC: unit type 0..3, the unit's cell
 * has a floor sprite, at least 2 TU, and the armour-TU warning is clear. Then
 * 2 TU is spent and Soldier.bFlags bit 0 is flipped, with nAnimPose moved by
 * hand (up when standing back up, down when kneeling) instead of recomputing
 * the pose. The status record is the marksmanship note, not a kneel note. */
void TactHud_ToggleKneel(const TactHudCtx *pCtx)
{
    if (pCtx->pfnPlaySample != NULL) {
        pCtx->pfnPlaySample(17, pCtx->pCallbackUser);
    }

    Soldier *pUnit = pCtx->pSelUnit;
    if (pUnit == NULL || pUnit->nUnitType > 3u) {
        return;
    }
    MapCell *pCell = pUnit->pCell;
    if (pCell == NULL || pCell->pFloorSpr == NULL) {
        return;
    }
    if (pUnit->uTu < 2u) {
        return;
    }
    if (pCtx->pfnArmorTuWarn != NULL
        && pCtx->pfnArmorTuWarn(pUnit, 2, pCtx->pCallbackUser)) {
        return;
    }

    pUnit->uTu = (u16)(pUnit->uTu - 2u);

    if ((pUnit->bFlags & 1u) != 0) {
        /* Was kneeling: stand up and lose the bonus. 0x16600..0x1661C. */
        pUnit->bFlags &= (u8)~0x01u;
        pUnit->nAnimPose = (u8)(pUnit->nAnimPose + 1);
        if (pCtx->pfnPushTextRecord != NULL) {
            pCtx->pfnPushTextRecord(PUFO_TXTREC_KNEEL_OFF, pCtx->pCallbackUser);
        }
    } else {
        /* Kneel and gain the bonus. 0x16638..0x16654. */
        pUnit->bFlags |= 1u;
        pUnit->nAnimPose = (u8)(pUnit->nAnimPose - 1);
        if (pCtx->pfnPushTextRecord != NULL) {
            pCtx->pfnPushTextRecord(PUFO_TXTREC_KNEEL_ON, pCtx->pCallbackUser);
        }
    }
}

/* The large-unit half of both climb checks, 0x16948..0x16AB8 in ClimbUp and the
 * matching run in ClimbDown. A large unit covers three tiles, so the two extra
 * tiles of the target layer must have no wall facing the climb cell and no
 * occupant. The original tests (x+1) wall-A, (y+1) wall-B, then (x+1,y+1)
 * against both. */
static bool HudLargeRoom(const TactHudCtx *pCtx, i32 nX, i32 nY, i32 nLayer)
{
    static const i8 aDx[4] = { 1, 0, 1, 1 };
    static const i8 aDy[4] = { 0, 1, 1, 1 };
    static const CellFlag aWall[4] = {
        CELLFLAG_WALL_A_BLANK, CELLFLAG_WALL_B_BLANK,
        CELLFLAG_WALL_A_BLANK, CELLFLAG_WALL_B_BLANK
    };

    for (i32 i = 0; i < 4; ++i) {
        MapCell *p = HudCell(pCtx, nX + aDx[i], nY + aDy[i], nLayer);
        if (p != NULL && MapCell_TestFlag(p, aWall[i])) {
            return false;
        }
    }
    for (i32 i = 0; i < 3; ++i) {
        MapCell *p = HudCell(pCtx, nX + aDx[i], nY + aDy[i], nLayer);
        if (p != NULL && MapCell_HasOccupant(p)) {
            return false;
        }
    }
    return true;
}

/* The target-cell checks both climb handlers share: the cell must not be blank,
 * must be free of occupants, and a large unit needs room in its other tiles. */
static bool HudCellClear(const TactHudCtx *pCtx, MapCell *pCell)
{
    if (MapCell_TestFlag(pCell, CELLFLAG_IS_BLANK)) {
        return false;
    }
    if (MapCell_HasOccupant(pCell)) {
        return false;
    }
    if (Unit_bIsLarge(pCtx->pSelUnit)
        && !HudLargeRoom(pCtx, pCell->nX, pCell->nY, pCell->nZ)) {
        return false;
    }
    return true;
}

/* TactHud_ClimbUp @ 0x1674c.
 *
 * The ladder test has two halves. First, unless the unit type carries the
 * byte_A7B80[type][0] flag, both the unit's own cell and the cell one layer up
 * must have a floor sprite whose blocking byte is set (0x167A4..0x16808). Then
 * the layer above must exist (nZ + 1 <= 3) and, if it has a floor sprite, that
 * floor and the unit's own floor must both block (0x1680C..0x168B0). */
void TactHud_ClimbUp(const TactHudCtx *pCtx)
{
    if (pCtx->pfnPlaySample != NULL) {
        pCtx->pfnPlaySample(17, pCtx->pCallbackUser);
    }

    Soldier *pUnit = pCtx->pSelUnit;
    if (pUnit == NULL || pUnit->pCell == NULL || pCtx->pfnLookup == NULL) {
        return;
    }
    MapCell *pOwn = pUnit->pCell;
    const i32 nX = pOwn->nX, nY = pOwn->nY, nZ = pOwn->nZ;

    if (g_aUnitTypeFlag0[pUnit->nUnitType] != 1) {
        MapCell *pUp = HudCell(pCtx, nX, nY, nZ + 1);
        if (pUp == NULL) {
            return;
        }
        if (!MapCell_TestFlag(pOwn, CELLFLAG_FLOOR_BLOCKS)
            || !MapCell_TestFlag(pUp, CELLFLAG_FLOOR_BLOCKS)) {
            return;
        }
    }

    if (nZ + 1 > 3) {
        return;
    }
    MapCell *pUp = HudCell(pCtx, nX, nY, nZ + 1);
    if (pUp == NULL) {
        return;
    }
    if (MapCell_TestFlag(pUp, CELLFLAG_HAS_FLOOR)
        && (!MapCell_TestFlag(pUp, CELLFLAG_FLOOR_BLOCKS)
            || !MapCell_TestFlag(pOwn, CELLFLAG_FLOOR_BLOCKS))) {
        return;
    }

    if (!HudCellClear(pCtx, pUp)) {
        return;
    }

    TactState_EnqueueVertStep(pCtx->pQueue, pUnit, 1u);
    TactState_KickQueue(pCtx->pQueue);
}

/* TactHud_ClimbDown @ 0x16af0. The mirror image, with the layer below as the
 * target and no unit-type flag in the ladder test: the unit's own floor may be
 * absent, or both the floor below and the unit's own floor must block. */
void TactHud_ClimbDown(const TactHudCtx *pCtx)
{
    if (pCtx->pfnPlaySample != NULL) {
        pCtx->pfnPlaySample(17, pCtx->pCallbackUser);
    }

    Soldier *pUnit = pCtx->pSelUnit;
    if (pUnit == NULL || pUnit->pCell == NULL || pCtx->pfnLookup == NULL) {
        return;
    }
    MapCell *pOwn = pUnit->pCell;
    const i32 nX = pOwn->nX, nY = pOwn->nY, nZ = pOwn->nZ;

    if (nZ - 1 < 0) {
        return;
    }
    if (MapCell_TestFlag(pOwn, CELLFLAG_HAS_FLOOR)) {
        MapCell *pDown = HudCell(pCtx, nX, nY, nZ - 1);
        if (pDown == NULL) {
            return;
        }
        if (!MapCell_TestFlag(pDown, CELLFLAG_FLOOR_BLOCKS)
            || !MapCell_TestFlag(pOwn, CELLFLAG_FLOOR_BLOCKS)) {
            return;
        }
    }

    MapCell *pDown = HudCell(pCtx, nX, nY, nZ - 1);
    if (pDown == NULL || !HudCellClear(pCtx, pDown)) {
        return;
    }

    TactState_EnqueueVertStep(pCtx->pQueue, pUnit, 0xFFu);
    TactState_KickQueue(pCtx->pQueue);
}

/* Inv_OpenScreen @ 0x16ef4. See the header for the gate order.
 *
 * The cursor is re-stamped at the position it already holds: the original passes
 * Map.nCursorX/nCursorY straight back in (ARM 0x16FB0/0x16FBC), which re-runs the
 * footprint clear and set for the current cell. The latch was just cleared, so
 * bSuppressLarge is false and a large selected unit does spread the stamp. */
bool TactHud_OpenInventory(const TactHudInvOpenCtx *pCtx)
{
    MapCursorPos pos;

    if (pCtx == NULL || pCtx->pUi == NULL) {
        return false;
    }
    if (pCtx->bSweepGate != 0 && pCtx->pfnPlaySample != NULL) {
        pCtx->pfnPlaySample(16, pCtx->pCallbackUser);
    }
    if (pCtx->nXcomCount == 0) {
        return false;
    }
    if (pCtx->pSelUnit == NULL || pCtx->pSelUnit->nUnitType > 3u) {
        return false;
    }

    pCtx->pUi->nInvListSel = (u8)-1;   /* 0xFF at 0x4A26: every category */
    pCtx->pUi->nInvFilterType = 0;     /* 0x1A22: the ground list starts at row 0 */
    Inv_BuildVisibleList(pCtx->pUi, pCtx->pSelUnit);

    /* The seven filter pads: "All" lit at pBank+0x30A, the other six dark. */
    pCtx->pUi->bFilterPadX = 0;
    pCtx->pUi->bFilterPadAll = 1;
    memset(pCtx->pUi->abFilterPad, 0, sizeof(pCtx->pUi->abFilterPad));
    pCtx->pUi->bActionLatch = PUFO_ACTION_LATCH_NONE;

    pos.pCursor = pCtx->pCursor;
    pos.geom = pCtx->geom;
    pos.bBusy = false;                 /* unk_157180->bBusy, not modelled here */
    pos.bSelUnitLarge = pCtx->bSelUnitLarge;
    pos.bSuppressLarge = pCtx->pUi->bActionLatch != 0;
    MapCursor_SetPos(&pos, pCtx->pCursor->nX, pCtx->pCursor->nY,
                     pCtx->pfnLookup, pCtx->pLookupUser);

    if (pCtx->pInvStat != NULL) {
        Unit_RecalcInventoryStat(pCtx->pSelUnit, pCtx->pInvStat);
    }
    pCtx->pUi->nUiScreen = 1;
    pCtx->pUi->bDrawIsoLayer = 1;
    return true;
}

/* The shared body of the two sort-key-20 roster sweeps, 0x180D4..0x18180 for the
 * enemies and 0x18198..0x18240 for the Xcom team. Both walk the roster array and
 * write Soldier.bSpecialMark @ 0x1B0 from one test: the unit's cell must lie
 * within 10 cells of the selected unit's cell, measured as sqrt(dx*dx + dy*dy) in
 * IEEE754 double (ARM 0x18138 sqrt, 0x18154 gtd against 0x40240000_00000000). */
static void Special_MarkRoster(Soldier *const *apUnits, i32 nCount, MapCell *pFrom)
{
    for (i32 i = 0; i < nCount; ++i) {
        MapCell *pCell = apUnits[i]->pCell;
        i32 nDx = (i32)pCell->nX - (i32)pFrom->nX;
        i32 nDy = (i32)pCell->nY - (i32)pFrom->nY;
        double dDist = sqrt((double)(nDx * nDx + nDy * nDy));

        apUnits[i]->bSpecialMark = (u8)(dDist > PUFO_SPECIAL_MARK_RADIUS ? 0u : 1u);
    }
}

/* The medikit target search, 0x17E00..0x17F3C. It also returns the cell whose
 * coordinates the handler writes into nFireTgtX/Y/Z: the adjacent cell when the
 * target came from an occupant, the healer's own cell when it came from the
 * civilian scan. The layer is the healer's own layer in both cases. */
typedef struct SpecialTarget {
    Soldier *pUnit;
    i32      nX;
    i32      nY;
    i32      nZ;
    bool     bCivilian;
} SpecialTarget;

static SpecialTarget Special_FindTarget(const TactHudSpecialCtx *pCtx, Soldier *pUnit)
{
    SpecialTarget out = { NULL, 0, 0, 0, false };
    MapCell *pCell = pUnit->pCell;
    GameMap *pMap = pCtx->pMap;

    if (pCell == NULL) {
        return out;
    }
    out.nZ = (i32)pCell->nZ;

    if (pUnit->bFacing < PUFO_PATH_DIR_COUNT) {
        const StepDir *pStep = &g_aStepDir[pUnit->bFacing];

        out.nX = (i32)pCell->nX + pStep->dx;
        out.nY = (i32)pCell->nY + pStep->dy;
        if (Unit_CanStepExitDir(pCell, pUnit->bFacing, &pMap->geom,
                                pCtx->pfnLookup, pCtx->pLookupUser)) {
            MapCell *pNext = pCtx->pfnLookup(out.nX, out.nY, out.nZ,
                                             pCtx->pLookupUser);
            if (pNext != NULL) {
                out.pUnit = pNext->pUnit;
            }
        }
    }

    if (out.pUnit == NULL) {
        /* No occupant in front: look for a civilian standing on the healer's own
         * cell. The scan stops at the first small, humanoid, living one. */
        out.nX = (i32)pCell->nX;
        out.nY = (i32)pCell->nY;
        for (i32 i = 0; i < pMap->nCivilianCount; ++i) {
            Soldier *pCiv = pMap->apCivilians[i];

            if (pCiv == NULL || pCiv->pCell != pCell) {
                continue;
            }
            if (!Unit_bIsLarge(pCiv) && pCiv->nUnitType <= 3u && pCiv->uHp != 0u) {
                out.pUnit = pCiv;
                out.bCivilian = true;
                break;
            }
        }
    }
    return out;
}

/* TactHud_UseSpecialItem20 @ 0x17d54. See the header for the three branches. */
void TactHud_UseSpecialItem(const TactHudSpecialCtx *pCtx)
{
    GameMap *pMap;
    TactUiState *pUi;
    Soldier *pUnit;
    SpecialTarget tgt;
    i32 nCost;
    u16 uSortKey;

    if (pCtx == NULL || pCtx->pMap == NULL || pCtx->pUi == NULL) {
        return;
    }
    pMap = pCtx->pMap;
    pUi = pCtx->pUi;

    if (pCtx->pfnPlaySample != NULL) {
        pCtx->pfnPlaySample(17, pCtx->pCallbackUser);
    }

    /* Written before the weapon is even looked at: 0x17D74 and 0x17D78. */
    pUi->nActionMode = PUFO_ACTION_MODE_SPECIAL;
    pUi->nUiScreen = 0;

    pUnit = pMap->pSelUnit;
    if (pUnit == NULL || pMap->pSelWeapon == NULL || pMap->pSelWeapon->pObj == NULL) {
        return;
    }
    uSortKey = pMap->pSelWeapon->pObj->uSortKey;
    nCost = pUi->anActionCost[pUi->nActionMode];

    if (uSortKey == PUFO_SPECIAL_SORT_MARK) {
        /* 0x18078..0x18248: the cost is paid off the unit directly, not through
         * the queue, and the paid value is the cost cut to 16 signed bits. */
        if ((i32)pUnit->uTu < nCost) {
            if (pCtx->pfnPushTextRecord != NULL) {
                pCtx->pfnPushTextRecord(1, pCtx->pCallbackUser);
            }
            return;
        }
        pUnit->uTu = (u16)(pUnit->uTu - (i32)(i16)(u16)nCost);
        Special_MarkRoster(pMap->apEnemyUnits, pMap->nEnemyCount, pUnit->pCell);
        Special_MarkRoster(pMap->apXcomUnits, pMap->nXcomCount, pUnit->pCell);
        pUi->nUiScreen = 6;
        return;
    }

    if (uSortKey == PUFO_SPECIAL_SORT_LATCH) {
        if ((i32)pUnit->uTu < nCost) {
            if (pCtx->pfnPushTextRecord != NULL) {
                pCtx->pfnPushTextRecord(1, pCtx->pCallbackUser);
            }
            return;
        }
        pMap->bActionLatch = PUFO_ACTION_LATCH_FIRE;
        return;
    }

    if (uSortKey != PUFO_SPECIAL_SORT_MEDIKIT) {
        return;
    }
    if ((i32)pUnit->uTu < nCost) {
        if (pCtx->pfnPushTextRecord != NULL) {
            pCtx->pfnPushTextRecord(1, pCtx->pCallbackUser);
        }
        return;
    }

    tgt = Special_FindTarget(pCtx, pUnit);

    /* A civilian found on the healer's own cell skips the operative and size
     * tests; anything else has to pass all three. 0x17F40..0x17F70. */
    if (!tgt.bCivilian
        && (tgt.pUnit == NULL || !Unit_bIsOperative(tgt.pUnit)
            || Unit_bIsLarge(tgt.pUnit))) {
        if (pCtx->pfnPushTextRecord != NULL) {
            pCtx->pfnPushTextRecord(PUFO_TXTREC_MEDKIT_WHY, pCtx->pCallbackUser);
            pCtx->pfnPushTextRecord(PUFO_TXTREC_MEDKIT_WHY2, pCtx->pCallbackUser);
        }
        return;
    }

    /* A target that is not primed, is at full HP, is in good spirits and has not
     * spent half its HP in TU does not need the kit. 0x17FB0..0x18008. */
    if (tgt.pUnit->bKneelOrPrimed == 0
        && tgt.pUnit->uHp == tgt.pUnit->auStat[1]
        && tgt.pUnit->bMorale == 100u
        && (i32)tgt.pUnit->uTuSpent <= ((i32)tgt.pUnit->uHp >> 1)) {
        if (pCtx->pfnPushTextRecord != NULL) {
            pCtx->pfnPushTextRecord(PUFO_TXTREC_MEDKIT_FIT, pCtx->pCallbackUser);
            pCtx->pfnPushTextRecord(PUFO_TXTREC_MEDKIT_FIT2, pCtx->pCallbackUser);
        }
        return;
    }

    pUi->nFireTgtX = tgt.nX;
    pUi->nFireTgtY = tgt.nY;
    pUi->nFireTgtZ = tgt.nZ;
    TactState_EnqueueAction18(pCtx->pQueue, pUnit, tgt.pUnit, pMap->pSelWeapon,
                              nCost, pUi->nActionMode);
    TactState_KickQueue(pCtx->pQueue);
}

/* --------------------------------------------------------------------------
 * PadImage_BlitDecimal @ 0x36c58 - a decimal number drawn as pad images.
 * -------------------------------------------------------------------------- */

i32 TactHud_BlitDecimal(PadImage *const *apPads, i32 x, i32 y, i32 nVal,
                        i32 nWidth, bool bLeadZero,
                        HudPadBlitFn pfnBlitEx, void *pUser)
{
    char szNum[20];
    i32 nLen;
    i32 nPad;
    i32 nResult;

    if (apPads == NULL) {
        return 0;
    }

    /* Deviation: snprintf stands in for itoa(nVal, buf, 10). Same digits, bounded. */
    snprintf(szNum, sizeof(szNum), "%d", (int)nVal);
    nLen = (i32)strlen(szNum);

    /* 0x36C94: the filler count is the field width minus the digit count, and zero
     * when the width is not larger. */
    nPad = (nWidth > 0 && nWidth > nLen) ? nWidth - nLen : 0;

    if (nPad > 0) {
        /* 0x36CB0: slot 10 is the leading pad, slot 0 is the plain one. */
        const i32 nFill = bLeadZero ? 10 : 0;
        for (i32 i = 0; i < nPad; ++i) {
            PadImage *pPad = apPads[nFill];
            if (pPad == NULL) {
                break;
            }
            pPad->nOriginX = x;
            pPad->nOriginY = y;
            if (pfnBlitEx != NULL) {
                pfnBlitEx(pPad, pUser);
            }
            /* 0x36CF8: the filler advance is width + 1. */
            x += (i32)pPad->uWidth + 1;
        }
    }

    nResult = nLen;   /* 0x36D04: the return value starts as the digit count. */
    for (i32 i = 0; i < nLen; ++i) {
        const i32 nDigit = (i32)(u8)szNum[i] - 48;
        PadImage *pPad;

        /* 0x36D28 indexes the table by the character itself; a non-digit cannot come
         * out of the decimal conversion, but the port does not read past the table. */
        if (nDigit < 0 || nDigit >= 10) {
            break;
        }
        pPad = apPads[nDigit];
        if (pPad == NULL) {
            break;
        }
        pPad->nOriginX = x;
        pPad->nOriginY = y;
        if (pfnBlitEx != NULL) {
            pfnBlitEx(pPad, pUser);
        }
        /* 0x36D54 and the for increment at 0x36D14: the next digit starts one pixel
         * after this one ends. */
        nResult = x + (i32)pPad->uWidth;
        x = nResult + 1;
    }
    return nResult;
}

/* Copy one 205-byte text record into a caller buffer, as the original's byte-at-a-time
 * do/while does. The port uses the full record size rather than the original's scratch. */
static void CopyTextRecord(const u8 *pTextRecs, i32 nRec, char *pszOut)
{
    const u8 *pSrc = pTextRecs + PUFO_TEXT_REC_STRIDE * nRec;
    i32 k = 0;

    for (;;) {
        pszOut[k] = (char)pSrc[k];
        if (pszOut[k] == 0) {
            break;
        }
        ++k;
    }
}

/* The centring rule the HUD label draws share: pad.x + (pad.uWidth - text width) / 2, at
 * pad.y + a fixed vertical offset. The menu uses +3, the modal panels use +2. */
static void DrawLabelOnPad(const UiControl *pPad, const char *psz, i32 nVertOffset)
{
    i32 nWidth = Text_MeasureWidth(psz);

    Text_DrawString((i32)pPad->x + ((i32)pPad->uWidth - nWidth) / 2,
                    (i32)pPad->y + nVertOffset, psz);
}

/* Menu_DrawLabeledPads @ 0x217e0.
 *
 * The label records are a sparse map: index 5 reads record 402 and index 6 reads record 23,
 * while 0..4 read 18..22. Seven menu labels are therefore NOT seven consecutive records. */
i32 Menu_LabelRecordIndex(i32 nIdx)
{
    if (nIdx == 5) {
        return 402;
    }
    if (nIdx == 6) {
        return 23;
    }
    return nIdx + 18;
}

i32 Menu_DrawLabeledPads(UiControl *pPads, const u8 *pTextRecs, const GfxColorEnv *pColor,
                         UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser)
{
    char szLabel[PUFO_TEXT_REC_STRIDE];
    i32 nIdx;
    i32 nResult;

    Gfx_PackRgb565Pair(pColor, 26, 244, 252, 13, 122, 126);

    for (nIdx = 0; ; ++nIdx) {
        (void)UiControl_BlitActivePad(&pPads[nIdx], pfnBlit, pfnBlitEx, pUser);

        CopyTextRecord(pTextRecs, Menu_LabelRecordIndex(nIdx), szLabel);
        DrawLabelOnPad(&pPads[nIdx], szLabel, 3);

        nResult = nIdx + 1;
        if (nResult >= PUFO_HUD_MENU_PADS) {
            break;
        }
    }
    return nResult;
}

/* TactHud_DrawModalLeft @ 0x21bac and TactHud_DrawModalRight @ 0x21cec, one body.
 *
 * Eight pads are blitted and only seven labels are drawn; the eighth pad has no label. That
 * mismatch is the original's, reproduced rather than tidied. The modal text is drawn only when
 * the mode flag (0x157188) is exactly 1, and 40 is written into the action-phase scratch cell
 * anActionPhaseSave0[112] first. */
i32 TactHud_DrawModalPanel(UiControl *pPads, const u8 *pLabelTable, i32 nModalMode,
                           i32 nTextRecIndex, i32 *pnActionPhaseSave112,
                           UiModalAnim *pAnim, const u8 *pTextRecs,
                           UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser)
{
    UiModalLayout layout;
    i32 i;

    for (i = 0; i < PUFO_HUD_MODAL_PADS; ++i) {
        (void)UiControl_BlitActivePad(&pPads[i], pfnBlit, pfnBlitEx, pUser);
    }

    for (i = 0; i < PUFO_HUD_MODAL_LABELS; ++i) {
        Gfx_PackRgb565Pair(pAnim->pColor, 26, 244, 252, 13, 122, 126);
        DrawLabelOnPad(&pPads[i], (const char *)(pLabelTable + PUFO_HUD_MODAL_LABEL_STRIDE * i), 2);
    }

    if (nModalMode != 1) {
        return nModalMode;
    }

    *pnActionPhaseSave112 = 40;
    return UiModal_DrawDialog(pAnim, pTextRecs, 150, nTextRecIndex, 1, NULL, NULL, &layout);
}

/* TactHud_CopyStatusText @ 0x21688.
 *
 * The status line is text record 5 (unk_B3CD1 = aTextRecs + 205*5), drawn in white centred on
 * the 240-pixel screen at y = 100. Then nine pads each get their own index printed as a decimal,
 * centred on a FIXED 23 rather than on the pad's own width - the only place in this family that
 * does not use the pad width. */
i32 TactHud_CopyStatusText(UiControl *pPads, const char *pszStatus, const GfxColorEnv *pColor,
                           UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser)
{
    char szNum[16];
    i32 nIdx;
    i32 nResult;
    i32 nWidth;

    Gfx_PackRgb565Pair(pColor, 255, 255, 255, 10, 10, 10);
    nWidth = Text_MeasureWidth(pszStatus);
    Text_DrawString((240 - nWidth) / 2, 100, pszStatus);

    Gfx_PackRgb565Pair(pColor, 26, 244, 252, 13, 122, 126);

    for (nIdx = 0; ; ++nIdx) {
        (void)UiControl_BlitActivePad(&pPads[nIdx], pfnBlit, pfnBlitEx, pUser);

        sprintf(szNum, "%d", nIdx);   /* j_itoa (0x9e9f0, a 4-byte thunk to the CRT), base 10 */
        nWidth = Text_MeasureWidth(szNum);
        Text_DrawString((i32)pPads[nIdx].x + (23 - nWidth) / 2, (i32)pPads[nIdx].y + 4, szNum);

        nResult = nIdx + 1;
        if (nResult >= PUFO_HUD_STATUS_PADS) {
            break;
        }
    }
    return nResult;
}

/* TactHud_ActionButtonRecord @ 0x21430, the per-button visibility rule. */
i32 TactHud_ActionButtonRecord(const TactHudActionGate *pGate, i32 nIdx)
{
    switch (nIdx) {
        case 0:
            return 396;
        case 1:
            if (pGate->nActionMode != 1 && pGate->nActionMode != 2
                && pGate->nActionMode != 4 && pGate->nActionMode != 3) {
                return -1;
            }
            return (pGate->nActionMode == 3) ? 1111 : 1108;
        case 2:
            if (pGate->nActionMode != 3 || pGate->nMultiTargetCount < 1) {
                return -1;
            }
            return 1109;
        case 3:
            if (pGate->nActionMode == 0) {
                return -1;
            }
            return 397;   /* written in the ARM as 396 | 1 */
        case 4:
            if (pGate->nActionMode != 0) {
                return -1;
            }
            return 1110;
        case 5:
            if (pGate->bDeployPanelOpen != 0) {
                return -1;
            }
            return 398;
        case 6:
            if (pGate->bDeployPanelOpen == 0) {
                return -1;
            }
            return 399;
        case 7:
            return 400;
        case 8:
            return 401;   /* written in the ARM as 400 | 1 */
        default:
            return -1;
    }
}

/* TactHud_DrawActionButtons @ 0x21430.
 *
 * Nine buttons, each first disabled and then re-enabled by its own rule. A visible button's y is
 * rewritten to 19 * (visible count so far) + 87, so the stack is packed: hiding a middle button
 * moves every later one up. The label is centred on the pad with a vertical offset of 1. */
i32 TactHud_DrawActionButtons(UiControl *pPads, const TactHudActionGate *pGate,
                              const u8 *pTextRecs, const GfxColorEnv *pColor,
                              UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser)
{
    char szLabel[PUFO_TEXT_REC_STRIDE];
    i32 nIdx;
    i32 nVisible = 0;
    i32 nResult;

    for (nIdx = 0; ; ++nIdx) {
        i32 nRec;

        pPads[nIdx].bEnabled = 0;

        nRec = TactHud_ActionButtonRecord(pGate, nIdx);
        if (nRec >= 0) {
            pPads[nIdx].bEnabled = 1;
            pPads[nIdx].y = (i16)(19 * nVisible + 87);

            (void)UiControl_BlitActivePad(&pPads[nIdx], pfnBlit, pfnBlitEx, pUser);

            Gfx_PackRgb565Pair(pColor, 26, 244, 252, 13, 122, 126);
            CopyTextRecord(pTextRecs, nRec, szLabel);
            DrawLabelOnPad(&pPads[nIdx], szLabel, 1);

            ++nVisible;
        }

        nResult = nIdx + 1;
        if (nResult >= PUFO_HUD_ACTION_PADS) {
            break;
        }
    }
    return nResult;
}

/* TactHud_DrawConfirmDialog @ 0x1eb68.
 *
 * A backdrop pad, one bank pad (widget 129) centred on its own label, a title centred at y = 10,
 * two fixed side labels at (10, 30) and (170, 30), then two message panels. The side labels are
 * green; the pad label is the usual cyan. */
i32 TactHud_DrawConfirmDialog(UiControl *pPad, const PadImage *pBackdrop,
                              const char *szPadLabel, const char *szTitle,
                              const char *szLeft, const char *szRight,
                              UiPanel *pPanelA, UiPanel *pPanelB,
                              const GfxColorEnv *pColor,
                              UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser)
{
    i32 nWidth;

    PadImage_Blit(pBackdrop);

    (void)UiControl_BlitActivePad(pPad, pfnBlit, pfnBlitEx, pUser);

    Gfx_PackRgb565Pair(pColor, 26, 244, 252, 13, 122, 126);
    DrawLabelOnPad(pPad, szPadLabel, 1);

    Gfx_PackRgb565Pair(pColor, 0, 255, 0, 0, 90, 0);
    nWidth = Text_MeasureWidth(szTitle);
    Text_DrawString((240 - nWidth) / 2, 10, szTitle);

    Gfx_PackRgb565Pair(pColor, 0, 255, 0, 0, 90, 0);
    Text_DrawString(10, 30, szLeft);

    Gfx_PackRgb565Pair(pColor, 0, 255, 0, 0, 90, 0);
    Text_DrawString(170, 30, szRight);

    UiPanel_DrawText(pPanelA);
    UiPanel_DrawText(pPanelB);

    return 1;
}

/* TactHud_DrawStatusBanner @ 0x1e920.
 *
 * Skipped whole when the fade counter is positive. The banner text is green when the ready flag
 * is set and red otherwise; the timer text is the other way round, red only when its value is
 * exactly 45. */
i32 TactHud_DrawStatusBanner(UiControl *pPad, const PadImage *pBackdrop,
                             const TactHudBannerState *pState, const char *szFooter,
                             const char *szPadLabel,
                             UiPanel *pPanelA, UiPanel *pPanelB,
                             const GfxColorEnv *pColor,
                             UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser)
{
    i32 nWidth;

    if (pState->nFadeCount > 0) {
        return pState->nFadeCount;
    }

    PadImage_Blit(pBackdrop);

    if (pState->nReadyFlag != 0) {
        Gfx_PackRgb565Pair(pColor, 0, 255, 0, 0, 90, 0);
    } else {
        Gfx_PackRgb565Pair(pColor, 255, 0, 0, 90, 0, 0);
    }
    nWidth = Text_MeasureWidth(pState->szBanner);
    Text_DrawString(120 - nWidth / 2, 10, pState->szBanner);

    UiPanel_DrawText(pPanelA);
    UiPanel_DrawText(pPanelB);

    Gfx_PackRgb565Pair(pColor, 0, 255, 0, 0, 90, 0);
    Text_DrawString(10, 260, szFooter);

    if (pState->nTimerValue == 45) {
        Gfx_PackRgb565Pair(pColor, 255, 0, 0, 90, 0, 0);
    } else {
        Gfx_PackRgb565Pair(pColor, 0, 255, 0, 0, 90, 0);
    }
    Text_DrawString(200, 260, pState->szTimer);

    (void)UiControl_BlitActivePad(pPad, pfnBlit, pfnBlitEx, pUser);

    Gfx_PackRgb565Pair(pColor, 26, 244, 252, 13, 122, 126);
    DrawLabelOnPad(pPad, szPadLabel, 1);

    return 1;
}

/* TactHud_DrawUnitRoster @ 0x21908.
 *
 * Twelve fire modes, only those with a non-zero entry drawn. A drawn pad's y is rewritten to
 * 23 * (drawn count so far) + 67, so the panel packs the visible modes. The mode name comes from
 * a 30-byte-stride table at 0xB3768, which sits BELOW aTextRecs @ 0xB38D0 and is not a text-record
 * table. The first number is drawn only for modes 0..2 and 11 - the ARM test is `idx < 3 || idx > 10` -
 * and it is the same value that gates the row, so a row either shows one number or two. */
i32 TactHud_DrawUnitRoster(UiControl *pPads, const TactHudRoster *pRoster,
                           const GfxColorEnv *pColor,
                           UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser)
{
    char szText[128];
    i32 nIdx;
    i32 nVisible = 0;
    i32 nResult;

    if (pRoster->pSelWeapon == NULL) {
        return 0;
    }

    for (nIdx = 0; ; ++nIdx) {
        if (pRoster->anModeFlag[nIdx] != 0) {
            pPads[nIdx].y = (i16)(23 * nVisible + 67);

            (void)UiControl_BlitActivePad(&pPads[nIdx], pfnBlit, pfnBlitEx, pUser);

            Gfx_PackRgb565Pair(pColor, 26, 244, 252, 13, 122, 126);
            Text_DrawString((i32)pPads[nIdx].x + 5, (i32)pPads[nIdx].y + 3,
                            (const char *)(pRoster->pNameTable + PUFO_HUD_ROSTER_NAME_STRIDE * nIdx));

            if (nIdx < 3 || nIdx > 10) {
                sprintf(szText, "%s%d", pRoster->szNumberPrefixA, pRoster->anModeFlag[nIdx]);
                /* The original sets the same colour twice here; reproduced rather than tidied. */
                Gfx_PackRgb565Pair(pColor, 26, 244, 252, 13, 122, 126);
                Gfx_PackRgb565Pair(pColor, 26, 244, 252, 13, 122, 126);
                Text_DrawString((i32)pPads[nIdx].x + 85, (i32)pPads[nIdx].y + 3, szText);
            }

            sprintf(szText, "%s%d", pRoster->szNumberPrefixB, pRoster->anModeValue[nIdx]);
            Gfx_PackRgb565Pair(pColor, 26, 244, 252, 13, 122, 126);
            Text_DrawString((i32)pPads[nIdx].x + 135, (i32)pPads[nIdx].y + 3, szText);

            ++nVisible;
        }

        nResult = nIdx + 1;
        if (nResult >= PUFO_HUD_ROSTER_PADS) {
            break;
        }
    }
    return nResult;
}

/* TactHud_DrawUnitMarkers @ 0x20c58.
 *
 * One overlay pad at (0,0), then a marker for every unit carrying bSpecialMark, placed relative to
 * the selected unit's cell at 11 pixels per cell of difference. XCOM rows use 11 * (dy + 14), which
 * is 11 * dy + 154, while enemy rows use 11 * dy + 155: the two bands are one pixel apart rather
 * than symmetric. Large units take the large mark, offset a further 2 for XCOM and 8 for enemies,
 * and are also the only case where the two differ; small units take the small mark at 4 either way. */
i32 TactHud_DrawUnitMarkers(UiControl *pPad, const TactHudMarkerEnv *pEnv,
                            Soldier *const *apXcom, i32 nXcomCount,
                            Soldier *const *apEnemy, i32 nEnemyCount,
                            UiControlBlitFn pfnBlit, UiControlBlitFn pfnBlitEx, void *pUser)
{
    i32 i;

    pEnv->pOverlayPad->nOriginX = 0;
    pEnv->pOverlayPad->nOriginY = 0;
    PadImage_Blit(pEnv->pOverlayPad);

    for (i = 0; i < nXcomCount; ++i) {
        const Soldier *pUnit = apXcom[i];
        i32 nX;
        i32 nY;
        PadImage *pMark;

        if (pUnit->bSpecialMark == 0) {
            continue;
        }
        nX = 11 * (pUnit->pCell->nX - pEnv->pSelCell->nX) + 120;
        nY = 11 * (pUnit->pCell->nY - pEnv->pSelCell->nY + 14);

        if (Unit_bIsLarge(pUnit)) {
            pMark = pEnv->pLargeMark;
            pMark->nOriginX = nX - 2;
            pMark->nOriginY = nY - 2;
        } else {
            pMark = pEnv->pSmallMark;
            pMark->nOriginX = nX - 4;
            pMark->nOriginY = nY - 4;
        }
        PadImage_BlitEx(pMark, 0xFFu);
    }

    for (i = 0; i < nEnemyCount; ++i) {
        const Soldier *pUnit = apEnemy[i];
        i32 nX;
        i32 nY;
        PadImage *pMark;

        if (pUnit->bSpecialMark == 0) {
            continue;
        }
        nX = 11 * (pUnit->pCell->nX - pEnv->pSelCell->nX) + 120;
        nY = 11 * (pUnit->pCell->nY - pEnv->pSelCell->nY) + 155;

        if (Unit_bIsLarge(pUnit)) {
            pMark = pEnv->pLargeMark;
            pMark->nOriginX = nX - 8;
            pMark->nOriginY = nY - 8;
        } else {
            pMark = pEnv->pSmallMark;
            pMark->nOriginX = nX - 4;
            pMark->nOriginY = nY - 4;
        }
        PadImage_BlitEx(pMark, 0xFFu);
    }

    (void)UiControl_BlitActivePad(pPad, pfnBlit, pfnBlitEx, pUser);

    return 1;
}
