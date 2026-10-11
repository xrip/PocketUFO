/* pufo_mapctx.h - the live tactical game context, the original's `Map` block.
 *
 * Evidence: the IDB local type `Map` (152632 bytes), which every tactical
 * function reaches through the context base pointer at 0x1139b8. Field comments
 * below name the Map offset each field stands for.
 *
 * Two deliberate deviations from the original block, both recorded here because
 * they are structural rather than behavioural:
 *   - Field ORDER differs. The original interleaves tactical, UI and inventory
 *   state in one 152632-byte block. This struct groups the tactical part and
 *   leaves the UI/inventory regions out until a consumer needs them. The block
 *   is heap state and is never serialised, so order carries no parity weight;
 *   the per-field offsets do, and they are all cited.
 *   - The original keeps `apCells` @ 0x15338 as a `MapCell *[4][60][60]` pointer
 *   table. The port embeds the cells and the lookup returns their addresses.
 *   Same 14400 cells, one less indirection, and no allocation step to fail.
 */
#ifndef PUFO_MAPCTX_H
#define PUFO_MAPCTX_H

#include "pufo_common.h"

#include "game/pufo_ballist.h"
#include "game/pufo_los.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_statusmsg.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_turn.h"
#include "game/pufo_unit.h"

/* Map.apCells is MapCell *[4][60][60]: 60 cells per edge, which is
 * PUFO_CELL_SIZE * 6 tiles. */
#define PUFO_MAP_CELL_DIM 60

/* Map.apXcomUnits @ 0x15154 and Map.apEnemyUnits @ 0x151CC are Soldier *[30];
 * Map.apCivilians @ 0x15244 is Soldier *[60]. */
#define PUFO_MAP_ROSTER_MAX 30
#define PUFO_MAP_CIV_MAX    60

/* The shot-history cells the move FSM reads as `ctx + 4 * nShotCount + 4` at
 * 0x50008. The IDB types the same block as nMultiTargetCount @ 0x04 followed by
 * apMultiTargets[8] @ 0x08, so the array runs from 0x04 to 0x24. */
#define PUFO_MAP_SHOT_MAX 9

/* The ten multi-target world positions, Map.adMtX/Y/Z. */
#define PUFO_MAP_MULTI_TARGET_MAX 10

typedef struct GameMap {
    /* --- geometry: nMapLayers @ 0x15114, nMapDimX @ 0x1511B, nMapDimY @ 0x1511C --- */
    MapGeom geom;
    u8      nMapLayers;

    /* --- the cell grid, apCells @ 0x15338 --- */
    MapCell aCells[PUFO_MAP_LAYERS][PUFO_MAP_CELL_DIM][PUFO_MAP_CELL_DIM];

    /* --- rosters --- */
    Soldier *apXcomUnits[PUFO_MAP_ROSTER_MAX];   /* 0x15154 */
    Soldier *apEnemyUnits[PUFO_MAP_ROSTER_MAX];  /* 0x151CC */
    Soldier *apCivilians[PUFO_MAP_CIV_MAX];      /* 0x15244 */
    u8       nXcomCount;                         /* 0x1511F, byte */
    u8       nEnemyCount;                        /* 0x15124, byte */
    i32      nCivilianCount;                     /* 0x15120, dword */

    /* --- selection --- */
    Soldier  *pSelUnit;   /* 0x15334 */
    TactItem *pSelWeapon; /* 0x0048 */

    /* --- tactical scratch --- */
    i8      nLayerScratch;     /* 0x15138, signed byte */
    i32     nMaxLayerClamp;    /* 0x15074 */
    i32     nAnimPhase;        /* 0x00F8 */
    i32     nShotCount;        /* 0x002C, Hex-Rays' nStatusDrawY */
    u8      bActionLatch;      /* 0x0002, unk_1139BA: 0 none, 3 multi-target, 4 fire mode */
    u8      bFireStatus1;      /* 0x0051 */
    u8      nMissionType;      /* 0x1511A */
    Soldier *pThrowTarget;     /* 0x150F0 */
    i32     nFireTickProgress; /* 0x1405C */
    MapCell *apShotCells[PUFO_MAP_SHOT_MAX]; /* 0x0004..0x0024, see above */

    /* --- shot plumbing --- */
    AimState        aim;      /* 0x13FD8..0x14088 */
    ScatterOffset   scatter;  /* 0x15058 / 0x1505C / 0x15060 */
    double          adMtX[PUFO_MAP_MULTI_TARGET_MAX]; /* 0x13EE8 */
    double          adMtY[PUFO_MAP_MULTI_TARGET_MAX]; /* 0x13F38 */
    double          adMtZ[PUFO_MAP_MULTI_TARGET_MAX]; /* 0x13F88 */
    Soldier        *apTraceExclude[2]; /* 0x1406C, 0x14070 */
    bool            bTrajHitStop;      /* 0x1406D */
    ThrowArc        throwArc;
    ThrowLanding    throwLanding;
    ThrowBudget     throwBudget;

    /* --- the status message stack: nStatusMsgCount @ 0x0054,
     *     anStatusMsgTTL @ 0x1408C, aszStatusMsg @ 0x140B4 --- */
    StatusStack status;

    /* --- the busy/track object, unk_157180 --- */
    TactCtx272 tact;
} GameMap;

/* Fill every cell of the grid with its own coordinates, which is what the
 * original's map setup leaves behind before sprites and items are attached. */
void Map_InitCells(GameMap *pMap);

/* MapCellLookup over GameMap.aCells. Out-of-range coordinates give NULL; the
 * original indexes its pointer table with no bounds test at all. */
MapCell *Map_LookupCell(i32 nX, i32 nY, i32 nLayer, void *pUser);

/* Point a MoveActionCtx at a live GameMap. The three callbacks are left to the
 * caller; MoveActionWiring below provides the real ones. */
void Map_BuildMoveActionCtx(GameMap *pMap, const LosTables *pTables,
                            MoveActionCtx *pOut);

/* --- the real callbacks Tact_ProcessMoveAction asks for --- */

/* aTextRecs @ 0xb38d0 is 205-byte records. Unit_bArmorTuWarn @ 0x4e254 pushes
 * two of them: index 0 is byte_B5F40 (= 0xb38d0 + 205 * 48) and index 1 is
 * aTextRecs[205 * slot + 10045] (10045 = 205 * 49). */
#define PUFO_TXTREC_ARMOR_HEAD 48
#define PUFO_TXTREC_ARMOR_TAIL 49

typedef struct MoveActionWiring {
    GameMap     *pMap;
    const u8    *pTextRecs;      /* aTextRecs @ 0xb38d0, may be NULL */
    u32          nTextRecCount;
    /* The radio option bank's one-hot bytes at 0xAC81A / 0xAC83A / 0xAC85A.
     * The first suppresses the armour-TU warning entirely, the other two pick
     * which abArmorTu slot the cost comes from. */
    u8           abRadioOpt[3];
    VisCtx       visCtx;         /* for Unit_UpdateVisibility @ 0x48580 */
    StatusHooks  statusHooks;    /* for StatusMsg_Push @ 0x2dda4 */
} MoveActionWiring;

/* Unit_bArmorTuWarn @ 0x4e254 with the hands read off the unit: pHandL @ 0x104
 * and pHandR @ 0x108, each through TactItem.pObj's abArmorTu[3] @ 0xB3 and
 * bTwoHanded @ 0x28. */
bool Map_ArmorTuWarnCb(const Soldier *pUnit, i32 nTuCost, void *pUser);

/* StatusMsg_Push @ 0x2dda4 with the text taken from aTextRecs record nIndex.
 * bForce is 0, matching the move FSM's call at 0x507F8. */
void Map_PushStatusCb(i32 nTextRecIndex, void *pUser);

/* Unit_UpdateVisibility @ 0x48580, the call at the end of command 0xB. */
void Map_UpdateVisibilityCb(Soldier *pUnit, void *pUser);

/* Fill the three callback slots of an already-built MoveActionCtx. */
void Map_WireMoveActionCtx(MoveActionCtx *pCtx, MoveActionWiring *pWiring);

#endif /* PUFO_MAPCTX_H */
