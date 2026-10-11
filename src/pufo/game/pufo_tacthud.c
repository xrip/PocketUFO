/* Tactical HUD action buttons. See pufo_tacthud.h. */

#include "game/pufo_tacthud.h"

#include <stddef.h>

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
