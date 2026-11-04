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
#include "game/pufo_combat.h"   /* PUFO_SPLASH_DWORDS, the hit-window size the Map block holds */
#include "game/pufo_los.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_path.h"    /* PUFO_PATH_MAX_CELLS, the walked-path array the Map block holds */
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

/* The ten shot-sample world positions, Map.adMtX/Y/Z. The IDB lays the three blocks 80 bytes
 * apart (Map_adMtX @ 0x1278A0, Map_adMtY @ 0x1278F0, Map_adMtZ @ 0x127940), which is ten doubles
 * each -- the same ten the ballistician fills, PUFO_AIM_SAMPLE_COUNT. The second slot of every
 * block has its own name in the IDB (Map_dThrowX @ 0x1278A8, Map_dThrowY, Map_dThrowLayer), which
 * is the throw position World_InitFallRecord points at. The multi-target CELL list is a different
 * thing with its own bound, PUFO_MULTI_TARGET_MAX 8 in pufo_input.h; the two constants share
 * nothing but a former name. */
#define PUFO_MAP_MT_SAMPLE_COUNT PUFO_AIM_SAMPLE_COUNT

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
    i8      nLayerScratch;     /* 0x15138, signed byte. NOTE: the live walker wiring points the
                                * processor contexts at tactEnv.cursor.nFlags instead -- 0x15138 is
                                * the SAME cell as TactCursor.nFlags (the recorded multi-view issue). */
    i32     nMaxLayerClamp;    /* 0x15074 */
    i32     nAnimPhase;        /* 0x00F8 */
    /* There is no four-byte sprite probe at 0x0016..0x0019. The bytes there are apMultiTargets[3..4]
     * halves, and the four bytes Tact_MapTypeToGlyphIdx really scans are Map_abSpriteSet at
     * 0x15116..0x15119 -- an earlier pass took a Soldier-relative `LDRB [R,#0x16]` (0x498DC, in
     * Unit_ResetBattleState) for a Map-relative one. The world feeds the live sprite set to
     * IsoUnitDrawGate.paTypeProbe. */
    i32     nShotCount;        /* 0x002C, Hex-Rays' nStatusDrawY */
    i32     nBurstShots;       /* 0x0028 Map_nBurstShots, the shots a mode-7 burst still owes */
    u8      bActionLatch;      /* 0x0002, unk_1139BA: 0 none, 3 multi-target, 4 fire mode */
    u8      bFireStatus1;      /* 0x0051 */
    u8      bFireStatus9;      /* 0x0059 unk_113A11, written from bFireHitResult at 0x51F50 */
    u8      bEnemySpotted;     /* 0x4E48 unk_118800, set by Unit_SpotCellOccupant @ 0x48344 */
    Soldier *pLookCommitUnit;  /* 0x15088 unk_128A40, the look/psi action's committed target */
    u8      bLookCommitFlag;   /* 0x1508C unk_128A44 */
    u8      nMissionType;      /* 0x1511A */
    Soldier *pThrowTarget;     /* 0x150F0 */
    i32     nFireTickProgress; /* 0x1405C */
    union {                                   /* 0x0004..0x0024, the block above, one storage two readings */
        MapCell *apShotCells[PUFO_MAP_SHOT_MAX];   /* the move FSM's muzzle-origin read at 0x50008 */
        struct {
            i32      nMultiTargetCount;      /* 0x0004 Map_nMultiTargetCount: what the AI's pick randoms range over */
            MapCell *apMultiTargets[8];      /* 0x0008..0x0024 Map_apMultiTargets */
        };
    };

    /* --- shot plumbing --- */
    AimState        aim;      /* 0x13FD8..0x14088 */
    ScatterOffset   scatter;  /* 0x15058 / 0x1505C / 0x15060 */
    double          adMtX[PUFO_MAP_MT_SAMPLE_COUNT]; /* 0x13EE8 Map_adMtX; [1] = Map_dThrowX */
    double          adMtY[PUFO_MAP_MT_SAMPLE_COUNT]; /* 0x13F38 Map_adMtY; [1] = Map_dThrowY */
    double          adMtZ[PUFO_MAP_MT_SAMPLE_COUNT]; /* 0x13F88 Map_adMtZ; [1] = Map_dThrowLayer */
    /* The two units Map_TraceVoxelOccupancy @ 0x3242c skips are pSelUnit @ 0x15334
     * and pThrowTarget @ 0x150F0 (the IDB's pThrowTargetUnit) — both already modelled
     * above, so there is no separate exclusion pair here. An earlier version of this
     * struct claimed a Soldier* pair at 0x1406C/0x14070; those two slots are
     * Map.bFireHitResult (a byte) and Map.pArrowThrownItem (a TactItem*), not units. */
    u8              bFireHitResult;    /* 0x1406C Map_bFireHitResult: cleared at 0x2FC54 by every
                                        * Tact_TraceProjectileArc call, set at 0x3010C when the walked
                                        * sample leaves the map (the port's ShotTrace.bLeftMap) */
    u8              bTrajHitStop;      /* 0x1406D Map.bTrajHitStop, the in-flight flag. Three writers only:
                                        * raised to 1 by every Tact_SimulateThrowArc call (0x31A0C), cleared by
                                        * the landing walk (0x322C4 volume, 0x32390 occupied voxel), cleared by
                                        * Tact_ProcessFallAction's throw arm (0x53D04). The port names it
                                        * ThrowPending in the three action contexts, and the input handlers read
                                        * it as a refusal gate (0x116F4 and nine siblings). */
    TactItem       *pArrowThrownItem;  /* 0x14070 Map_pArrowThrownItem: the thrown item every arc call stores
                                        * at 0x319FC with its own argument -- the landing call's item first, then
                                        * Tact_ProcessMoveAction's own call at 0x50960 with Map.pSelWeapon, so
                                        * pSelWeapon is what survives the throw. MapView_DrawUnitFacingArrow
                                        * dereferences it (unguarded, like the original) whenever bTrajHitStop
                                        * stands. MoveThrow writes both cells through MoveActionCtx. */
    ThrowArc        throwArc;
    ThrowLanding    throwLanding;
    ThrowBudget     throwBudget;     /* not an original slot: 0x31A44..0x31A78 composes the budget inline;
                                      * MoveThrow writes this scratch at the throw site -- see MoveActionCtx */

    /* --- combat scratch: Map.gap_TrajRest @ 0x4EBC --- */
    u32 aHitWindow[PUFO_SPLASH_DWORDS];      /* 0x4EBC, the 41 x 41 hit window (26896 bytes) */
    u32 aHitWindowSave[PUFO_SPLASH_DWORDS];  /* gap_TrajRest + 26896, the snapshot copy */
    /* --- fire scratch --- */
    i32 nFireArg5;                /* 0x1404C nFireArg5, the LOS outcome code */
    const ObjectRecFull *pFireArg6; /* 0x14050 nFireArg6, the weapon record the shot resolves with */

    /* --- the iso layer's height glyph (MapView_DrawUnitHeightGlyph @ 0x1c240) ---
     * The gate dword the function tests whole is 0x14000, which is aim.anAimRow[10] -- one cell,
     * one owner (the IDB names it nHeightGlyphId). No ported code writes it yet. */
    i32 nHeightSrcX;              /* 0x14060 nHeightSrcX: the arc's source tile, used mod 16 */
    i32 nHeightSrcY;              /* 0x14064 nHeightSrcY, mod 16 */
    i32 nHeightSrcZ;              /* 0x14068 nHeightSrcZ, mod 12 */

    /* --- action scratch: unk_1279F8 / FC / 00 @ Map+0x14040 / 44 / 48 ---
     * These three are AimState.nTgtX/Y/Z above, one cell each, not a second triple:
     * the aim block spans 0x13FD8..0x14088 and 0x14040/44/48 fall inside it. The IDB
     * names the slot "melee cell" because TactAction_ConfirmMeleeMode6 @ 0x17bb8 writes
     * it, and the arc walk writes it too (0x301CC). Every context that needs it takes a
     * pointer into pMap->aim (see Map_BuildMoveActionCtx), so an arc-written target and
     * a confirm-written target are the same value the resolve reads. */
    /* No field here for Map+0x230 (0x113BE8): the original never reads or writes it (zero
     * xrefs). The chryssalid transform name comes from the unit NAME table 0xB1140 --
     * record 14 in the kill arm (0x4e518 ADD #0x230), record 22 in the fall arm (0x53148
     * ADD #0x370) -- and reaches KillCtx/FallCtx through the shell's World_UnitNameRecord.
     * The port's earlier aTransformName[32] was that misread; removed in Part 106. */

    /* --- pathfinding scratch (Unit_PathToCell @ 0x2c2a8, wired from the shell in Part 52) --- */
    MapCell *apPathCells[PUFO_PATH_MAX_CELLS]; /* Map_apPathCells @ Map+0x4A48 (0x118400): the walked path,
                                                * destination-first (the store at 0x2C39C), replayed as queue entries */
    i32      nPathCount;                       /* Map+0x4E44 (0x1187FC Map_nPathCount): the walked length */
    Soldier *pPathDestUnit;                    /* Map+0x150F4 (0x128AB0 Map_pPathDestUnit): the bFlag=0
                                                * bookkeeping slot, cleared when bFlag is set */

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
     * which abArmorTu slot the cost comes from. The shell feeds these from
     * sTactUi.abRadio[0..2] before every move-action wiring. */
    u8           abRadioOpt[3];
    VisCtx       visCtx;         /* for Unit_UpdateVisibility @ 0x48580 */
    StatusHooks  statusHooks;    /* standalone fallback for StatusMsg_Push @ 0x2dda4 */
    const StatusHooks *pStatusHooks; /* optional caller-owned override; NULL uses local hooks */
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
