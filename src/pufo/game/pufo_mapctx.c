/* pufo_mapctx.c - the live tactical context and the real move-FSM callbacks. */
#include "game/pufo_mapctx.h"
#include "game/pufo_boot.h"

#include <string.h>

/* --- the grid ----------------------------------------------------------------
 * The original's apCells @ 0x15338 is a fixed MapCell *[4][60][60] table, so a
 * coordinate is legal against the table even when it is outside the current
 * map's dimensions. Map_IsValidCell is the dimension test the original applies
 * in its own bounds-checked paths, and it is what the port uses here. */

void Map_InitCells(GameMap *pMap)
{
    for (i32 nZ = 0; nZ < PUFO_MAP_LAYERS; ++nZ) {
        for (i32 nY = 0; nY < PUFO_MAP_CELL_DIM; ++nY) {
            for (i32 nX = 0; nX < PUFO_MAP_CELL_DIM; ++nX) {
                MapCell_Init(&pMap->aCells[nZ][nY][nX], nX, nY, nZ);
            }
        }
    }
}

MapCell *Map_LookupCell(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    GameMap *pMap = (GameMap *)pUser;

    if (pMap == NULL) {
        return NULL;
    }
    if (!Map_IsValidCell(&pMap->geom, nX, nY, nLayer)) {
        return NULL;
    }
    return &pMap->aCells[nLayer][nY][nX];
}

/* --- MoveActionCtx over a live GameMap --------------------------------------
 * Every pointer below is a field of the context block, which is what the
 * original passes as pCtxBase + offset. The busy gate and the tracked unit are
 * copied by value, matching MoveActionCtx's shape: a caller that keeps one
 * MoveActionCtx across ticks has to rebuild it, which is also what the original
 * effectively does by re-reading the globals inside the function. */

void Map_BuildMoveActionCtx(GameMap *pMap, const LosTables *pTables,
                            MoveActionCtx *pOut)
{
    pOut->geom            = pMap->geom;
    pOut->pfnLookup       = Map_LookupCell;
    pOut->pUser           = pMap;

    pOut->pnLayerScratch  = &pMap->nLayerScratch;
    pOut->pnMaxLayerClamp = &pMap->nMaxLayerClamp;
    pOut->pnAnimPhase     = &pMap->nAnimPhase;
    pOut->pnShotCount     = &pMap->nShotCount;
    pOut->pbActionLatch   = &pMap->bActionLatch;
    pOut->pbFireStatus1   = &pMap->bFireStatus1;
    pOut->ppSelWeapon     = &pMap->pSelWeapon;
    pOut->ppThrowTarget   = &pMap->pThrowTarget;
    pOut->pnUnk1405C      = &pMap->nFireTickProgress;
    pOut->bAnimBusy       = pMap->tact.bBusy != 0;
    pOut->pTrackUnit      = pMap->tact.pTrackUnit;
    pOut->apShotCells     = pMap->apShotCells;

    pOut->apXcomUnits     = pMap->apXcomUnits;
    pOut->apEnemyUnits    = pMap->apEnemyUnits;
    pOut->apCivilians     = pMap->apCivilians;
    pOut->pnXcomCount     = &pMap->nXcomCount;
    pOut->pnEnemyCount    = &pMap->nEnemyCount;
    pOut->pnCivilianCount = &pMap->nCivilianCount;

    pOut->pAim            = &pMap->aim;
    /* The aim block and the ten arc samples are one region of the Map context, and
     * the trace writes the running sample into the Map's arrays (0x2FE6C). Pointing
     * the AimState at them is what lets it do that. */
    pMap->aim.padMtX      = pMap->adMtX;
    pMap->aim.padMtY      = pMap->adMtY;
    pMap->aim.padMtZ      = pMap->adMtZ;
    pOut->pTables         = pTables;
    pOut->pScatter        = &pMap->scatter;
    pOut->pAdMtX          = pMap->adMtX;
    pOut->pAdMtY          = pMap->adMtY;
    pOut->pAdMtZ          = pMap->adMtZ;
    pOut->pThrowArc       = &pMap->throwArc;
    pOut->pThrowLanding   = &pMap->throwLanding;
    pOut->pThrowBudget    = &pMap->throwBudget;
    pOut->bThrowHitStop   = pMap->bTrajHitStop != 0;
    /* 0x3242C skips Map.pSelUnit and the unit at Map.pThrowTargetUnit; the arc trace
     * takes them as arguments because the port has no Map block. */
    pOut->pTraceExcludeSel   = pMap->pSelUnit;    /* 0x15334 */
    pOut->pTraceExcludeExtra = pMap->pThrowTarget; /* 0x150F0 */
}

/* --- text records ------------------------------------------------------------
 * aTextRecs @ 0xb38d0 is a table of 205-byte records. The move FSM names three
 * of them (records 1, 2 and 4) and Unit_bArmorTuWarn names two more. */

static const char *Map_TextRec(const MoveActionWiring *pWiring, i32 nRecord)
{
    if (pWiring->pTextRecs == NULL || nRecord < 0
        || (u32)nRecord >= pWiring->nTextRecCount) {
        return "";
    }
    return (const char *)pWiring->pTextRecs + PUFO_TEXT_REC_STRIDE * nRecord;
}

/* --- StatusMsg_Push @ 0x2dda4 ------------------------------------------------
 * bForce is 0, the value the move FSM passes at 0x507F8. */

static void Map_PushRecord(MoveActionWiring *pWiring, i32 nRecord)
{
    StatusMsg_Push(&pWiring->pMap->status, Map_TextRec(pWiring, nRecord),
                   false, &pWiring->statusHooks);
}

void Map_PushStatusCb(i32 nTextRecIndex, void *pUser)
{
    Map_PushRecord((MoveActionWiring *)pUser, nTextRecIndex);
}

/* --- Unit_bArmorTuWarn @ 0x4e254 ---------------------------------------------
 * The two hands are the unit's pHandL @ 0x104 and pHandR @ 0x108, each read
 * through TactItem.pObj: abArmorTu[3] @ 0xB3 and bTwoHanded @ 0x28. The warning
 * pushes two text records, and the second one's record index depends on the
 * armour slot the option bank selects, so the slot is recomputed here the same
 * way Unit_bArmorTuWarn picks it. */

static i32 Map_ArmorSlot(const MoveActionWiring *pWiring)
{
    if (pWiring->abRadioOpt[1] != 0) {
        return 1;
    }
    if (pWiring->abRadioOpt[2] != 0) {
        return 2;
    }
    return 0;
}

static void Map_PushArmorStatusCb(i32 nWhich, void *pUser)
{
    MoveActionWiring *pWiring = (MoveActionWiring *)pUser;
    const i32 nRecord = (nWhich == 0)
                            ? PUFO_TXTREC_ARMOR_HEAD
                            : PUFO_TXTREC_ARMOR_TAIL + Map_ArmorSlot(pWiring);

    Map_PushRecord(pWiring, nRecord);
}

static ArmorHandInfo Map_HandInfo(const TactItem *pItem)
{
    ArmorHandInfo hand;

    memset(&hand, 0, sizeof(hand));
    if (pItem != NULL && pItem->pObj != NULL) {
        hand.bPresent    = true;
        hand.bTwoHanded  = pItem->pObj->bTwoHanded;
        memcpy(hand.abArmorTu, pItem->pObj->abArmorTu, sizeof(hand.abArmorTu));
    }
    return hand;
}

bool Map_ArmorTuWarnCb(const Soldier *pUnit, i32 nTuCost, void *pUser)
{
    MoveActionWiring *pWiring = (MoveActionWiring *)pUser;
    const ArmorHandInfo handL = Map_HandInfo(pUnit->pHandL);
    const ArmorHandInfo handR = Map_HandInfo(pUnit->pHandR);

    return Unit_bArmorTuWarn(pUnit, &handL, &handR,
                             pWiring->abRadioOpt[0] != 0,
                             pWiring->abRadioOpt[1] != 0,
                             pWiring->abRadioOpt[2] != 0,
                             nTuCost, Map_PushArmorStatusCb, pWiring);
}

/* --- Unit_UpdateVisibility @ 0x48580 -----------------------------------------
 * The call at the end of command 0xB (0x50B44) discards the result. */

void Map_UpdateVisibilityCb(Soldier *pUnit, void *pUser)
{
    MoveActionWiring *pWiring = (MoveActionWiring *)pUser;

    (void)Unit_UpdateVisibility(pUnit, &pWiring->visCtx);
}

/* --- the busy gate for StatusMsg_Push ----------------------------------------
 * unk_157180->bBusy @ 0xF8, which the port keeps as TactCtx272.bBusy. The
 * repaint flag (LOBYTE(apUiSlotsA[2]) != 0) is UI state the port has not
 * recreated, so ShouldRender stays NULL and no repaint is requested. */

static bool Map_HookIsBusy(void *pUser)
{
    const MoveActionWiring *pWiring = (MoveActionWiring *)pUser;

    return pWiring->pMap->tact.bBusy != 0;
}

void Map_WireMoveActionCtx(MoveActionCtx *pCtx, MoveActionWiring *pWiring)
{
    pWiring->statusHooks.pfnIsBusy      = Map_HookIsBusy;
    pWiring->statusHooks.pfnShouldRender = NULL;
    pWiring->statusHooks.pfnRender      = NULL;
    pWiring->statusHooks.pUser          = pWiring;

    pCtx->pfnArmorTuWarn      = Map_ArmorTuWarnCb;
    pCtx->pfnPushStatus       = Map_PushStatusCb;
    pCtx->pfnUpdateVisibility = Map_UpdateVisibilityCb;
    pCtx->pCallbackUser       = pWiring;
    pCtx->pVisCtx             = &pWiring->visCtx;
}
