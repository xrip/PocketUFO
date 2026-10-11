/* Tactical HUD action buttons. See pufo_tacthud.h. */

#include "game/pufo_tacthud.h"

#include "game/pufo_path.h"

#include <math.h>
#include <stddef.h>
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
