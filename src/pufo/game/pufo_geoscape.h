/* pufo_geoscape.h - the geoscape minute / hour / day tick chain.
 *
 * Evidence: Geoscape_TickShips @ 0x5d4e4, Geoscape_TickHourly @ 0x5d1bc,
 * Geoscape_TickBases @ 0x5d3bc, driven by Geoscape_AdvanceTime @ 0x5ce40
 * (ported in pufo_clock.c).
 *
 * Subsystems that are not part of this module (intercept reachability,
 * mission-site spawning, facility construction, research and manufacturing
 * accumulation) enter as hooks, each named after the original function it
 * stands for. That keeps the tick order — the part that has to match —
 * testable on its own.
 */
#ifndef PUFO_GEOSCAPE_H
#define PUFO_GEOSCAPE_H

#include "pufo_common.h"
#include "game/pufo_clock.h"
#include "game/pufo_ship.h"
#include "game/pufo_strateg.h"
#include "game/pufo_unit.h"
#include "render/pufo_render.h" /* GfxColorEnv, the colour pair RadarScreen_Update packs */
#include "ui/pufo_control.h"   /* UiControl, for the nav speed controls */
#include "ui/pufo_linelist.h"  /* UiLineList, the message list the build-complete report posts */
#include "ui/pufo_panel.h"     /* UiAnimBlitEnv, the radar panel's animation progress */

/* ------------------------------------------------- the facility grid layout
 *
 * Evidence: the original's own IDA type names for gBaseFacGrid @ 0x159768, size 1524:
 * buildDays u8[8][6][6] at +0x000 followed by fac BaseFacBlock[8] at +0x174, each block
 * 144 bytes of tileA, tileB, layer2 and facType, each a 6 by 6 BaseFacLayer6. The long
 * note at Base_UpdateWorkshopNeeded below maps these onto the offsets the port used
 * before the original's names were recovered. Game_StartNew @ 0x94554 stamps the whole
 * grid (0x9485C..0x948EC): buildDays and layers 0/1/2 zero, facType 0xFF. */
#define PUFO_GRID_SIZE          1524 /* gBaseFacGrid @ 0x159768 */
#define PUFO_GRID_DAYS_STRIDE   36   /* buildDays[nBase], 6 by 6 cells */
#define PUFO_GRID_FAC_OFF       0x174 /* fac BaseFacBlock[8] */
#define PUFO_GRID_FAC_STRIDE    144  /* one BaseFacBlock */
#define PUFO_GRID_TILEA_OFF     0    /* fac[nBase].tileA */
#define PUFO_GRID_TILEB_OFF     36   /* fac[nBase].tileB */
#define PUFO_GRID_LAYER2_OFF    72   /* the needed-flag layer, grid + 444 + 144*nBase */
#define PUFO_GRID_FAC_TYPE_OFF  108  /* the cell kind, grid + 480 + 144*nBase */
#define PUFO_GRID_CELL_COUNT    36   /* 6 by 6 cells per layer */
#define PUFO_FAC_TYPE_NONE      0xFF /* the empty-cell sentinel Game_StartNew stamps */

/* The facility kinds, 0..16. The same numbers index the 17 aBaseFacFlags records
 * @ 0x186E48 (PUFO_BASEFAC_COUNT) and fill fac[nBase].facType. The names are the shipped
 * text records Geoscape_InitUI @ 0x86A58 copies into each record: main.dat TEXTS.DAT
 * records 106..118, 463 and 120..122, in the order g_anFacNameRec uses. */
typedef enum FacilityKind {
    FAC_ACCESS_LIFT         = 0,  /* rec 106 "Access Lift" */
    FAC_LIVING_QUARTERS     = 1,  /* rec 107 "Living Quarters" */
    FAC_LABORATORY          = 2,  /* rec 108 "Laboratory" */
    FAC_WORKSHOP            = 3,  /* rec 109 "Workshop" */
    FAC_SHORT_RANGE_RADAR   = 4,  /* rec 110 "Short Range Radar" */
    FAC_LONG_RANGE_RADAR    = 5,  /* rec 111 "Long Range Radar" */
    FAC_GENERAL_STORES      = 6,  /* rec 112 "General stores" */
    FAC_ALIEN_CONTAINMENT   = 7,  /* rec 113 "Alien Containment" */
    FAC_PSI_LABORATORY      = 8,  /* rec 114 "Psi Laboratory" */
    FAC_HYPER_WAVE_DECODER  = 9,  /* rec 115 "Hyper Wave Decoder" */
    FAC_MIND_SHIELD         = 10, /* rec 116 "Mind Shield" */
    FAC_HANGAR              = 11, /* rec 117 "Hangar" */
    FAC_GRAVITY_SHIELD      = 12, /* rec 118 "Gravity shield" */
    FAC_MISSILE_DEFENCES    = 13, /* rec 463 "Missile Defences" */
    FAC_LASER_DEFENCE       = 14, /* rec 120 "Laser Defence" */
    FAC_PLASMA_DEFENCE      = 15, /* rec 121 "Plasma Defence" */
    FAC_FUSION_BALL_DEFENCE = 16  /* rec 122 "Fusion Ball Defence" */
} FacilityKind;

/* The 28-byte facility record at aBaseFacFlags @ 0x186E48, indexed by FacilityKind.
 * Save_WriteBases @ 0x3F2CC saves one byte per record (bFlag20), which is why the save docs
 * call this table BaseFacFlag. */
#define PUFO_BASEFAC_COUNT 17
typedef struct BaseFacFlag {
    char szName[20]; /* +0, from a text record */
    u8   bFlag20;    /* +20, the saved place flag; Game_StartNew sets it, InitUI leaves 0 */
    u8   bFlag21;    /* +21, InitUI stores the record index here */
    u8   gap22[2];   /* +22 */
    u32  uCost;      /* +24, read as a number by BaseFacScreen_TryBuySelected @ 0x58910 */
} BaseFacFlag;
_Static_assert(sizeof(BaseFacFlag) == 28, "aBaseFacFlags records are 28 bytes");

typedef struct GeoscapeState {
    GameTime     time;
    GeoscapeWorld world;
    StrategList *pList;

    u16  nDaysSinceStart;   /* word_1472C8 @ 0x1472c8, capped at 200 */
    i32  nSelectedShipIdx;  /* nSelectedShipIdx @ 0x187054 */
    i32  nSelectedShipAux;  /* gUiRootPanel.anLineAux[140] */
    u8   bLowSupplyFlag;    /* dword_1870C8 */

    BaseRec *const *apBases;      /* pCtxBase.apBases */
    ObjectRecFull  *pSupplyItem;  /* pCtxBase.apObjects[105] */
    i32  nBaseCount;              /* SLOBYTE(gUiRootPanel.anLineAux[79]) */
    i32 *apEmptyFacilityCells;    /* pMap.gap_TrajRest[26644], one per base */

    Soldier *const *apSoldiers;   /* pCtxBase.apSoldiers */
    u16  nSoldierCount;           /* pCtxBase.nSoldierCount (u16) */
} GeoscapeState;

typedef struct GeoscapeHooks {
    /* StrategList_MaybeSpawnCraft_Process (0x7e420) (ProcessEvents side). */
    void (*pfnProcessEvents)(u8 bSpawn, void *pUser);
    /* UiPanel_AppendLineWithSound @ 0x5a394. nMsg: 15 contact, 13 intercept
     * lost, 43 low supply, 10 radar contact. GameTime_Pause @ 0x5d6ac is NOT
     * part of the hook: 15, 13 and 10 pause the clock, 43 does not, so the
     * port writes time.bPaused itself at the three pausing sites. */
    void (*pfnContactMsg)(i32 nMsg, i32 nArg3, i32 nArg4, void *pUser);
    /* Geoscape_SpawnMissionSite (0x7e49c). */
    void (*pfnSpawnMissionSite)(StrategItem *pUfo, i32 nMissionSub, void *pUser);
    /* Smap_GrantSiteRecovery @ 0x98b50, ported in pufo_baseui.c behind SmapGrantRecoveryEnv. */
    void (*pfnGrantSiteRecovery)(void *pUser);
    /* ManufScreen_RefreshBaseTabs (0x70ebc), NOT ported. */
    void (*pfnRefreshBaseTabs)(void *pUser);
    /* ResearchScreen_AccumulateAllocated @ 0x74914, ported in pufo_baseui.c behind
     * ResearchAccumulateEnv; the shell wires this hook to it. */
    void (*pfnAccumulateResearch)(void *pUser);
    /* Base_TickFacilityConstruction @ 0x62824, ported in this file behind BaseTickConstructionEnv. */
    void (*pfnTickFacilityConstruction)(void *pUser);
    /* Base_ClearFacilityGridRow @ 0x6a0e0; its byte result becomes the
     * per-base site state byte_161F38[i]. */
    i32 (*pfnClearFacilityGridRow)(i32 nBaseIdx, void *pUser);
    /* Base_CountEmptyFacilityCells @ 0x6aee0. */
    i32 (*pfnCountEmptyFacilityCells)(i32 nBaseIdx, void *pUser);
    /* FundingScreen_OpenMonthly (0x5d160), NOT ported. */
    void (*pfnOpenMonthly)(void *pUser);
    /* Base_FindIndexBySiteId (0x5a8d0) is a second copy of Base_FindIndexByTypeByte @ 0x6ac30 and
     * the port keeps one implementation; this hook stays because the detect pass is called from
     * unported screens that resolve the base list their own way. */
    i32 (*pfnFindBaseBySiteId)(i32 nSiteId, void *pUser);
    void *pUser;
} GeoscapeHooks;

/* Geoscape_TickShips @ 0x5d4e4. */
void Geoscape_TickShips(GeoscapeState *pState, const GeoscapeHooks *pHooks);

/* Geoscape_TickHourly @ 0x5d1bc. */
void Geoscape_TickHourly(GeoscapeState *pState, const GeoscapeHooks *pHooks);

/* Geoscape_TickBases @ 0x5d3bc. */
void Geoscape_TickBases(GeoscapeState *pState, const GeoscapeHooks *pHooks);

/* Convenience: Geoscape_AdvanceTime with the three tick hooks pre-wired to
 * this module, plus the monthly funding hook. */
void Geoscape_Advance(GeoscapeState *pState, const GeoscapeHooks *pHooks,
                      i32 nDeltaSec);

/* Base_GetItemQtyA @ 0x66198 and Base_GetItemQtyB @ 0x661b8. Both read a u16 out of an
 * object's per-base array and differ ONLY in which object slot they reach:
 *
 *     A -> pCtxBase.apObjects[113]->auPerBaseVal[nBase]
 *     B -> pCtxBase.apObjects[112]->auPerBaseVal[nBase]
 *
 * so the port takes the object and keeps one implementation. The original's index is a
 * `char` used with no bound check, so an index past the eight entries reads whatever
 * follows; the port refuses and returns 0. */
i32 Base_GetItemQty(const ObjectRecFull *pItem, i32 nBaseIdx);

/* Geoscape_SetPanelFlagA @ 0x8b448 and Geoscape_SetPanelFlagB @ 0x8b478. Each sets one
 * byte of the block at 0x1864F4, and only while the screen id at 0x186BC4 is 0 or 2;
 * for any other screen id nothing at all is written. A writes +1, B writes +3. Returns
 * true when the flag was set. */
typedef struct GeoscapePanelFlags {
    u8 bUnk0;   /* 0x1864F4 */
    u8 bPanelA; /* 0x1864F5 */
    u8 bUnk2;   /* 0x1864F6 */
    u8 bPanelB; /* 0x1864F7 */
} GeoscapePanelFlags;

bool Geoscape_SetPanelFlagA(i32 nScreenMode, GeoscapePanelFlags *pFlags);
bool Geoscape_SetPanelFlagB(i32 nScreenMode, GeoscapePanelFlags *pFlags);

/* Base_FindIndexByTypeByte @ 0x6ac30, and its second copy Base_FindIndexBySiteId @ 0x5a8d0.
 * Linear search over the base array for the first base whose type byte (BaseRec.bUnk4 @ +0x04)
 * equals nType.
 *
 * The two originals are the same search on the same field with the same -1, and they differ only
 * in how carefully they bound the walk: 0x6AC30 returns -1 on an empty list and then loops with
 * NO bound at all, while 0x5A8D0 reads the count through LDRSB from abFacilityGrid+0x618 and
 * bounds its loop by it. One implementation covers both; see docs/parity/renames.tsv.
 *
 * With a base count of zero it returns -1 immediately (0x6ac50). Otherwise the loop has
 * NO bound at all: it walks forward until some entry happens to match, off the end of
 * the array if none does. The port bounds it by nBaseCount and returns -1 (deviation).
 *
 * The comparison is SIGNED on the field side and unsigned on the argument side: 0x6AC68 loads
 * BaseRec.bUnk4 with LDRSB while 0x6AC58 masks the argument with `AND R1, R1, #0xFF`, so a field of
 * 0x80 or more matches nothing in 0..255. Base_FindIndexBySiteId @ 0x5a8d0 loads the same field with
 * LDRSB (0x5A900) and compares it against an unmasked i32. Both originals are reproduced; the port
 * compared the field as u8 until Smap_HitTestStrategSelect @ 0x82be8 made the LDRSB visible. */
i32 Base_FindIndexByTypeByte(BaseRec *const *apBases, i32 nBaseCount, u8 nType);

/* BaseGrid_GetByte0x654 @ 0x6acc0 and BaseGrid_SetByteAtFound @ 0x6ac90. Both resolve a
 * type byte to a base index through Base_FindIndexByTypeByte and then read or write one
 * byte of the grid the original keeps at gUiRootPanel.anLineAux[94]. The getter returns
 * the byte (0 when there is no match), the setter returns the found index and writes
 * nothing when there is none. */
i32 BaseGrid_GetTypeByte(BaseRec *const *apBases, i32 nBaseCount, const u8 *abGrid,
                         u8 nType);
i32 BaseGrid_SetTypeByte(BaseRec *const *apBases, i32 nBaseCount, u8 *abGrid,
                         u8 nType, i8 bValue);

/* BaseGrid_SubByte0x654 @ 0x6acec. Subtract from one base's grid byte, found by type byte. It
 * only subtracts when the byte is already non-zero, but has no floor, so a positive byte can go
 * negative. The original does not check the found index and touches abGrid[-1] when no base has
 * the type; the port refuses. */
i32 BaseGrid_SubTypeByte(BaseRec *const *apBases, i32 nBaseCount, u8 *abGrid,
                         u8 nType, i8 nAmount);

/* ------------------------------------------------- the facility layout classifier
 *
 * Evidence: Base_ClearFacilityGridRow @ 0x6a0e0.
 *
 * THE NAME IS WRONG. It clears nothing. It reads one base's facility layout and
 * stores a state byte into the per-base site-state array at 0x161F38, which is the
 * same array the port carries as GeoscapeWorld.apSiteState, indexed by the very
 * argument the original calls a2. That also settles what a2 is: a base index.
 *
 * The two grids are the same pair Base_CountEmptyFacilityCells (0x6aee0) walks --
 * occupancy at grid + 36*nBaseIndex, cell type at grid + 480 + 144*nBaseIndex, both
 * 6 wide, indexed 6*column + row.
 *
 * Three passes, and the value they write is a small classification:
 *
 *   1. any cell of type 4 with its occupancy byte 0  -> state = 1
 *   2. any cell of type 5 with its occupancy byte 0  -> state = 2, overwriting the 1
 *   3. any cell of type 9 with its occupancy byte 0  -> state += 10, then stop
 *
 * Passes 1 and 2 assign rather than add, so 2 wins over 1 and the two are never
 * both present. Pass 3 exits on the FIRST free type-9 cell, so the 10 is added at
 * most once. A base with a free type-9 slot therefore ends up as 11 or 12.
 *
 * Note the order matters: pass 3 runs last and can add to whatever 1 or 2 was
 * chosen, but a base with no free 4 or 5 slot and a free 9 slot ends up as 10. */
i32 Base_ClearFacilityGridRow(u8 *abFacilityGrid, i32 nBaseIndex, u8 *apSiteState);

/* Base_CalcWorkshopCap @ 0x65894: the workshop capacity of one base.
 *
 * It counts the cells whose type byte is 3 -- a workshop -- whose matching occupancy
 * byte is 0, and returns that count times 50. So one free workshop cell is worth
 * exactly 50 units of manufacturing capacity, and the grid is the only input: no
 * globals, no dependencies.
 *
 * The same 6 by 6 pair Base_ClearFacilityGridRow walks, with the cell type at
 * grid + 480 + 144*nBaseIndex and the occupancy at grid + 36*nBaseIndex.
 *
 * Worth noting for whoever reads the caller: Base_UpdateWorkshopNeeded calls this at
 * 0x65BC0 and DISCARDS the result. It then derives its budget from
 * Base_CountActiveManuf instead. The capacity is used by other callers, not that one. */
i32 Base_CalcWorkshopCap(const u8 *abFacilityGrid, i32 nBaseIndex);

/* Base_CalcLabCap @ 0x65818. Identical to Base_CalcWorkshopCap in every respect except
 * the cell kind it looks for: it counts free cells of kind 2, a lab, and returns
 * count * 50. The original carries these as two separate copies of the same loop, so the
 * port keeps two names over one implementation. */
i32 Base_CalcLabCap(const u8 *abFacilityGrid, i32 nBaseIndex);

/* The rest of the capacity family. Every one is the same 6 by 6 walk over
 * fac[nBaseIndex].facType and buildDays[nBaseIndex]; only the cell kind and the weight
 * differ. A cell counts when its kind matches AND its build days are zero.
 *
 *   Base_CalcLivingQuartersCap     @ 0x6579c  kind 1,  +50 each
 *   Base_CalcStoresCapacity        @ 0x65720  kind 6,  +300 each
 *   Base_CalcAlienContainmentCap   @ 0x65910  kind 7,  +10 each
 *   Base_CalcHangarCap             @ 0x6611c  kind 11, +1 each
 *
 * The weights are the capacities themselves: one living-quarters cell houses 50 people, one
 * stores cell holds 300 units, one containment cell holds 10 aliens, and a hangar cell is
 * simply one craft slot. */
i32 Base_CalcLivingQuartersCap(const u8 *abFacilityGrid, i32 nBaseIndex);
i32 Base_CalcStoresCapacity(const u8 *abFacilityGrid, i32 nBaseIndex);

/* Base_CalcStorageUsed @ 0x6555c. How much of the base's own stock is taking up room: the sum of
 * bCargoSize * auPerBaseVal[nBase] over a fixed set of object indices - 1..69, 105..111, 122..124
 * and 140. Objects outside those ranges never count, no matter what they hold. An object whose
 * column for this base is zero contributes nothing, which is the only test the original makes.
 *
 * This is one of the two inputs Base_UpdateStoresNeeded uses for its budget inside the original;
 * the other is Base_CountItemQtyAcrossBases (0x67d68). Both are ported, and ManufScreen_IncEngineers
 * (0x6e13c) calls this pair directly rather than going through the needed pass. */
i32 Base_CalcStorageUsed(ObjectRecFull *const *apObjects, i32 nObjectCount, i32 nBase);

/* One in-memory facility record. The original keeps eight of these at 0x186C40 and walks them
 * 64 bytes at a time. The wire form is SaveBaseFacility, 61 bytes in a scattered field order;
 * this is the shape in memory, and the field offsets come from that wire type's own annotations. */
typedef struct BaseSiteRec {
    u8   bType;          /* +0x00 */
    u8   gap_01[1];
    u16  uUnk2;          /* +0x02 */
    u16  uUnk4;          /* +0x04 */
    u8   abUnk6[20];     /* +0x06 */
    char abName[20];     /* +0x1a */
    u8   gap_2E[2];
    i32  nUnk48;         /* +0x30 */
    i32  nDemolishState; /* +0x34 */
    i32  nBuildState;    /* +0x38 */
    i32  nFacType;       /* +0x3c */
} BaseSiteRec;

_Static_assert(sizeof(BaseSiteRec) == 64, "BaseSiteRec must be 64 bytes");

/* Every per-base column in the compaction is 200 entries wide, whether the entries are u16 or
 * u32: 400 bytes for the u16 columns and 800 for the u32 ones, which is what the strides
 * MOV R3,#0x190 and MOV R3,#0x320 at 0x66E4C and 0x66E80 say. */
#define PUFO_BASE_COLUMN_ENTRIES 200

/* Evidence: Base_DemolishFacility @ 0x66b70 (1600 bytes).
 *
 * Despite the name this removes a WHOLE BASE. It finds the base whose facility record has nFacType
 * equal to the argument, drops that base's strateg entry, deletes the soldiers stationed there,
 * removes every craft homed there, marks its map sites unassigned, renumbers the home bases and
 * soldier assignments above it, compacts every per-base array, drops the base count, deletes the
 * last apBases pointer, and renumbers BaseRec.bSlot. It returns the new base count.
 *
 * The argument is used with two different meanings, and that is the original's design rather than
 * a transcription problem: at 0x66BB8 it is a facility type compared against nFacType, and at
 * 0x66BE4 it is a SITE ID compared against StrategItem.nSiteId.
 *
 * A miss in the first search is not an error. The index lands on the base count, the compaction
 * guard fails, and the function still drops the count and deletes apBases[count-1] - so asking to
 * demolish a type no base has removes the LAST base. Reproduced. */
typedef struct BaseDemolishEnv {
    Ship **apCraft;
    u8 *pnShipCount;
    Soldier **apSoldiers;
    u16 *pnSoldierCount;
    BaseRec **apBases;
    ObjectRecFull **apObjects;
    u16 *pnObjectCount;
    u8 *abFacilityGrid;      /* 1524 bytes: buildDays then eight 144-byte blocks */
    BaseSiteRec *apSiteRecs; /* 0x186C40 */
    u16 *apManufCols;        /* 0x15E19C, 200 u16 per base */
    u16 *apProductC;         /* 0x15D09C, 200 u16 per base */
    i32  *apProductB;        /* 0x15B79C, 200 u32 per base */
    u16 *apResearchCols;     /* 0x16022C, 144 u16 per base */
    u16 *pnResearchEntries;  /* 0x160C4C, shared by every base */
    struct SmapSite *apSites;  /* 0x162030 */
    i32 nSiteCount;          /* 0x186410, signed */
    u8 *pbBaseCount;         /* 0x159D80, signed char */
    /* Reused whole for Ship_RemoveWithCargo, which is also what supplies the strateg list, the
     * world, the spawn gate and the spawn context this function needs. */
    const ShipRemovalEnv *pShipEnv;
} BaseDemolishEnv;

i32 Base_DemolishFacility(const BaseDemolishEnv *pEnv, i8 nFacType);
i32 Base_CalcAlienContainmentCap(const u8 *abFacilityGrid, i32 nBaseIndex);
i32 Base_CalcHangarCap(const u8 *abFacilityGrid, i32 nBaseIndex);

/* Base_HasResearchSlotFree @ 0x662bc and Base_HasWorkshopSlotFree @ 0x66340. Both compute the
 * whole capacity and then return `nNeeded <= capacity`, so they are the capacity passes with a
 * threshold compare bolted on -- not a search for a single free cell. */
bool Base_HasResearchSlotFree(const u8 *abFacilityGrid, i32 nBaseIndex, i32 nNeeded);
bool Base_HasWorkshopSlotFree(const u8 *abFacilityGrid, i32 nBaseIndex, i32 nNeeded);

/* Base_CountBuiltFacilities @ 0x682c8. The odd one out: it sums weights over cell kinds
 * 13, 14, 15 and 16 with +500, +600, +900 and +1500, and it does NOT test build days, so a
 * facility under construction counts the same as a finished one. Whatever this measures, it is
 * not a capacity. */
i32 Base_CountBuiltFacilities(const u8 *abFacilityGrid, i32 nBaseIndex);

/* Base_CalcPsiLabCap @ 0x6598c. Kind 8, 10 per free cell. Hex-Rays scrambles this one into a
 * do/while over a running pointer, but the two grids it reads are the usual pair. */
i32 Base_CalcPsiLabCap(const u8 *abFacilityGrid, i32 nBaseIndex);

/* Base_CountSoldiers @ 0x661d8. Soldiers whose base slot (Soldier.nKind) is this base -- the
 * plain assignment count. Base_CountSoldiersAssigned (0x66230) is the stricter version that also
 * asks whether the soldier is reachable. */
i32 Base_CountSoldiers(Soldier *const *apSoldiers, i32 nSoldierCount, i32 nBaseIndex);

typedef struct SmapSite SmapSite; /* forward, the two site helpers below need it */

/* Base_CountPsiSoldiers @ 0x65a08. Soldiers assigned to this base (nKind) that also carry the
 * psi marker bPsiAssigned @ 0x95. The budget of Base_UpdatePsiLabNeeded (divisor 10). */
i32 Base_CountPsiSoldiers(Soldier *const *apSoldiers, i32 nSoldierCount, i32 nBaseIndex);

/* Base_CountOutboundTransferSpace @ 0x68044. The recovery load (uRecoveredSoldiers +
 * bRecoveredSoldierAssign + auItemQty[112] + auItemQty[113]) of site nSiteIdx, plus, when
 * bScanSites, of every other site whose bFlag1 equals apBases[nBaseIndex]->bUnk4. Budget term
 * of Base_UpdateLivingQuartersNeeded. The original's first argument is never read.
 *
 * Two call shapes, both real. Base_UpdateLivingQuartersNeeded (0x65E50) passes nSiteIdx = -1 and
 * gets only the type-matched sites; BuySellScreen_ValidateCapacity (0x75E34) and
 * TransferScreen_ValidateCapacity (0x9666C) pass the site COUNT -- the transfer flow's working
 * record -- so the pending site's own load counts too. nSiteIdx is therefore bounded against
 * PUFO_SMAP_SITE_COUNT and not against nSiteCount, which is what the original leaves unbounded. */
i32 Base_CountOutboundTransferSpace(const SmapSite *aSites, i32 nSiteCount, i32 nSiteIdx,
                                    BaseRec *const *apBases, i32 nBaseIndex, bool bScanSites);

/* Base_CountInboundTransfers @ 0x67F78 (204 B). The mirror of Base_CountOutboundTransferSpace on
 * the craft side: the per-site term is the signed pair SmapSite.bRecoveredCraftCount (0x205) +
 * SmapSite.bUnk518 (0x206), both read with LDRSB and summed as signed bytes.
 *
 * The two call shapes differ, and both are real. BaseInfoScreen_BuildPanels (0x56634) passes
 * nSiteIdx = -1 and gets only the type-matched sites; BuySellScreen_ValidateCapacity (0x75C58) and
 * TransferScreen_ValidateCapacity (0x96548) pass nSiteIdx = nSiteCount -- the transfer flow's
 * working slot, the same record Transfer_ResetSite (0x9690c) clears -- so the pending site counts
 * too. BaseInfoScreen_BuildPanels then prints `hangarCap - inbound - shipsAtBase` (0x5663C/0x56644)
 * as the base's free hangar slots.
 *
 * Deviation: the original bounds nothing. It reads aSmapSites[nSiteIdx] on any nSiteIdx != -1 and
 * indexes apBases with the sign-extended low byte of nBaseIndex; the port bounds the site against
 * PUFO_SMAP_SITE_COUNT and the base against the eight apBases slots and skips what falls outside. */
i32 Base_CountInboundTransfers(const SmapSite *aSites, i32 nSiteCount, i32 nSiteIdx,
                               BaseRec *const *apBases, i32 nBaseIndex, bool bScanSites);

/* Base_CountItemQtyAcrossBases @ 0x67d68. Cargo volume (bCargoSize * auItemQty column) over the
 * filtered object indices 1..69, 105..111, 122..124, 140: site nSiteIdx itself, then — gated on
 * site nSiteIdx holding that item — every other site of this base's type. Budget term of
 * Base_UpdateStoresNeeded. A negative nSiteIdx means a zero gate (see the .c deviation note). */
i32 Base_CountItemQtyAcrossBases(const SmapSite *aSites, i32 nSiteCount, i32 nSiteIdx,
                                 BaseRec *const *apBases, i32 nBaseIndex,
                                 ObjectRecFull *const *apObjects, i32 nObjectCount,
                                 bool bScanSites);

/* Base_SumCraftAtBase @ 0x6a230, renamed: the IDB name describes one of the five terms. This is
 * the monthly cost of one base.
 *
 *   craft homed here   type 0 or 1 -> 500,000   2 -> 700,000   3 -> 600,000   4 -> 400,000
 *   soldiers assigned  20,000 each
 *   object 112 stock   20,000 per unit held at this base
 *   object 113 stock   30,000 per unit held at this base
 *   geoscape sites     20,000 * (auItemQty[224] + bRecoveredSoldierAssign)
 *                      + 30,000 * auItemQty[226], for sites whose bFlag1 is this base
 *   facility cells     50,000 for every cell whose type byte is not 255
 *
 * The last term reads the facility TYPE grid and never looks at build days, so an unfinished
 * facility is charged the same 50,000 as a finished one. */
typedef struct SmapSite SmapSite;
i32 Base_CalcMonthlyUpkeep(Ship *const *apCraft, i32 nCraftCount,
                           Soldier *const *apSoldiers, i32 nSoldierCount,
                           const SmapSite *aSites, i32 nSiteCount,
                           ObjectRecFull *const *apObjects, const u8 *abFacilityGrid,
                           i32 nBaseIndex);

/* Smap_SumSiteScores @ 0x96894 (120 B): the loot score of the site the transfer flow is working
 * on. That site lives in the slot AT the site count, not the last used one -- Transfer_ResetSite
 * (0x9690c) clears exactly `aSites[nSiteCount]`'s recovered fields and item quantities, and this
 * reads the same record -- so it is the working slot, not an off-by-one. The array is
 * PUFO_SMAP_SITE_COUNT (200) records, so the slot exists while the count is below 200.
 *
 * It sums three ranges of that record's 256-entry item-quantity array. The ARM composes each range
 * as post-increment halfword reads with writeback, which is why each starts one entry past the
 * address it loads:
 *
 *     ADD R2, R2, #0x9A ; LDRH R11,[R3,#2]! x28   offsets 156..210 -> auItemQty[77..104]
 *     LDRH R3, [R2,#0xE6]                          offset 230       -> auItemQty[114]
 *     ADD R2, R2, #0x11A ; LDRH R3,[R2,#2]! x8    offsets 284..298 -> auItemQty[141..148]
 *
 * Deviation: the original does not bound the site index, so a count of 200 reads one record past
 * the array; the port returns 0 there. */
#define PUFO_SITE_SCORE_A_FIRST 77
#define PUFO_SITE_SCORE_A_LAST  104
#define PUFO_SITE_SCORE_SINGLE  114
#define PUFO_SITE_SCORE_B_FIRST 141
#define PUFO_SITE_SCORE_B_LAST  148

i32 Smap_SumSiteScores(const SmapSite *aSites, i32 nSiteCount);

/* Base_CountSoldiersAssigned @ 0x66230. How many soldiers are actually available at this
 * base. A soldier counts when their base slot (Soldier.nKind) is this base, they are not
 * away in a craft (bUnk183 != 254), and either they are not in a craft at all (bUnk183 == 255)
 * or the craft they are in is currently at base.
 *
 * So 254 means "aboard a craft that is out" and 255 means "not aboard anything" -- the same
 * pair of sentinels Ship_RemoveWithCargo leaves alone when it renumbers ship indices.
 *
 * Deviation: the original indexes apCraft with bUnk183 without a bound. The port refuses a
 * soldier whose craft index is out of range rather than reading past the array. */
i32 Base_CountSoldiersAssigned(Soldier *const *apSoldiers, i32 nSoldierCount,
                               Ship *const *apCraft, i32 nCraftCount, i32 nBaseIndex);

/* Base_CountShipsAtBase @ 0x66090. Craft homed at this base, plus one byte of the shared
 * grid. The second half is the reason this is not just a count: it adds
 * BaseGrid_GetByte0x654(grid, apBases[nBaseIndex]->bUnk4), so the number a caller gets is
 * the craft at the base plus whatever that grid byte holds for the base's type byte.
 *
 * The arity question is settled: BaseGrid_GetByte0x654 takes two arguments (its first is
 * never read) and reaches both the grid and the base array through globals. The port's
 * BaseGrid_GetTypeByte takes the same two things as parameters. */
i32 Base_CountShipsAtBase(Ship *const *apCraft, i32 nCraftCount, BaseRec *const *apBases,
                          i32 nBaseCount, const u8 *abGrid, i32 nBaseIndex);

/* Base_FindFacilityCell @ 0x6ae54. Does this base have a free cell of facility kind nKind?
 * The same 6 by 6 walk as the two capacity passes, returning 1 at the first match. A free cell
 * means its buildDays byte is zero, that is nothing under construction there.
 *
 * The original's arguments are (grid, kind, baseIndex); the port puts the base index second to
 * match the rest of this family. */
i32 Base_FindFacilityCell(const u8 *abFacilityGrid, i32 nBaseIndex, u8 nKind);

/* Base_CountEmptyFacilityCells @ 0x6aee0. The same 6 by 6 walk over the same two grids, but
 * with weights of 2 and 3 rather than 50, summed over two cell kinds: a free kind-4 cell adds
 * 2 and a free kind-5 cell adds 3. Nothing else contributes.
 *
 * It is worth being clear about what this is not. The capacity passes count facility kinds and
 * produce a production limit; this one counts kinds 4 and 5, which are two of the three states
 * Base_ClearFacilityGridRow classifies, so it is a measure of how much room a base has left.
 * LoadGame (0x44bbc) stores the result in the array the port calls apRadarPct, which is the
 * same array Base_ClearFacilityGridRow writes its site state into. */
i32 Base_CountEmptyFacilityCells(const u8 *abFacilityGrid, i32 nBaseIndex);

/* Base_CountActiveResearch @ 0x68198. The same sum Base_CountActiveManuf performs, over a
 * different column, and reading the SAME count byte at 0x160C4C -- one entry count serves
 * both manufacturing and research.
 *
 * Its own constants, taken from the disassembly rather than the decompiler:
 *
 *     LDR  R2, =0x15EF08
 *     LDRB R3, [R2, #0x1D44]   the count byte, at 0x160C4C
 *     MUL  R11, nBase, #0x120  288 bytes per base, not 400
 *     MOV  R2, #0x1324         4900
 *     LDRH R2, [R1,R2]         the u16 entry, R1 advances by 2
 *
 * so the column base is 0x15EF08 + 4900 = 0x16022C, 288 bytes per base, which is 144 u16
 * entries.
 *
 * OPEN QUESTION, recorded rather than guessed: the manufacturing columns run from 0x15E19C
 * for 8 * 400 = 3200 bytes, ending at 0x160DDC, and 0x16022C falls inside that range. Two
 * arrays with different strides cannot both own that memory, so these are most likely two
 * views of one block rather than two separate arrays. Which one is the real layout is not
 * established, and nothing here depends on the answer because both take the column as an
 * argument. */
i32 Base_CountActiveResearch(const u16 *apResearchQty, i32 nEntries);

/* Base_CountActiveManuf @ 0x68264: total manufacturing quantity held by one base.
 *
 * It sums the first nEntries u16 entries of that base's quantity column and returns the
 * total. nEntries comes from the single global byte at 0x160C4C, which is shared by every
 * base rather than stored per base, and it is read as an unsigned byte, so the most the
 * original will ever walk is 255 entries. When it is zero the array is not touched at all.
 *
 * Where the column lives is worth spelling out, because the decompiler hides it. It renders
 * the address as gUiRootPanel.anLineAux[100*nBase + 149] plus 8578 u16 slots. The
 * disassembly is clearer and is what this section is taken from:
 *
 *     LDR  R1, =unk_159E98      base
 *     MUL  R11, nBase, #0x190   400 bytes per base
 *     MOV  R3, #0x4304          17156
 *     LDRH R3, [R1,R3]          the u16 entry, and the loop advances R1 by 2
 *
 * So the column base is 0x159E98 + 17156 = 0x15E19C, which is exactly where the product
 * block ends: the product block starts at 0x159E9C and PUFO_PRODUCT_BLOCK_BYTES is 17152.
 * The constant is 17156 rather than 17152 only because the anchor 0x159E98 sits four bytes
 * before the product block. The quantity columns therefore start immediately after the
 * product block, and each base gets 400 bytes, which is 200 u16 entries. The loop only
 * reads nEntries of those 200.
 *
 * The count byte is reached the same way: LDR R2, =0x15EF08 with LDRB R2, [R2, #0x1D44],
 * which is the byte at 0x160C4C.
 *
 * The original's first argument is never read; it is passed the grid pointer anyway. The
 * port drops it and takes the column directly. */
i32 Base_CountActiveManuf(const u16 *apManufQty, i32 nEntries);

/* Base_GetScientistCount @ 0x6812c and Base_GetEngineerCount @ 0x681f4: the two crew
 * availability getters, one per crew kind, and both the same expression
 *
 *     available = that crew item's per-base total - what this base already allocated
 *
 * The total is the u16 at record + 2*nBaseIndex + 42, which is ObjectRecFull.auPerBaseVal
 * [nBaseIndex] of the crew item the map block points at. The disassembly loads the pointer
 * out of the map root block and then indexes it, so the two originals differ only in the
 * slot they reach:
 *
 *     Base_GetScientistCount  LDR R0, =0x1139B8 / LDR R0, [R0,#0x2CC] -> 0x113C84, slot 113
 *     Base_GetEngineerCount   LDR R0, =0x1139B8 / LDR R0, [R0,#0x2C8] -> 0x113C80, slot 112
 *
 * (slots of the object pointer array whose slot 105 is Map_apObjects105 at 0x113C64, so the
 * array base is 0x113AC0 and slots 112 and 113 land exactly on 0x113C80 and 0x113C84). What
 * identifies the two as engineers and scientists is Base_CalcMonthlyUpkeep: it weights slot
 * 112 with 20000 next to the soldier count and slot 113 with 30000, and the site records use
 * the same two offsets, aItemQty[224] and aItemQty[226], with the same weights.
 *
 * The "already allocated" half is the quantity column sum over the SAME first nEntries
 * entries Base_CountActiveResearch / Base_CountActiveManuf sum, read with the same count
 * byte at 0x160C4C; only the column differs. The disassembly gives the two column bases:
 *
 *     scientists  MUL nBase,#0x120 (288 bytes) then MOV R2,#0x1324 (4900) over 0x15EF08
 *                 -> auResearchCol @ 0x16022C, 144 u16 per base
 *     engineers   MUL nBase,#0x190 (400 bytes) then MOV R3,#0x4304 (17156) over 0x159E98
 *                 -> the manufacturing column @ 0x15E19C, 200 u16 per base
 *
 * The subtraction has NO floor: a base whose column already holds more than its total
 * returns a negative count, and every caller tests `> 0`, so a negative behaves exactly like
 * zero. As with the two count functions, the original's first argument is never read -- it
 * gets the grid pointer anyway -- and the port drops it.
 *
 * Deviation: the port reads the total through Base_GetItemQty, which refuses an index outside
 * the eight per-base slots and yields 0 there; the original indexes blindly. */
i32 Base_GetScientistCount(const ObjectRecFull *pScientistRec, const u16 *apResearchCol,
                           i32 nBaseIndex, i32 nEntries);
i32 Base_GetEngineerCount(const ObjectRecFull *pEngineerRec, const u16 *apManufCol,
                          i32 nBaseIndex, i32 nEntries);

/* ------------------------------------------------- the workshop needed pass
 *
 * Evidence: Base_UpdateWorkshopNeeded @ 0x65bb4.
 *
 * It marks the first N workshop cells of one base as needed, where N is a budget built
 * from the manufacturing total:
 *
 *     N = (nActiveManuf == 0) ? 0 : nActiveManuf / 50 + 1
 *
 * So a base with any active manufacturing at all gets at least one needed workshop, and
 * one more per full 50 units. It then walks the 6 by 6 grid and, for every cell whose
 * type byte is 3, writes 1 into the needed-flag grid while the budget lasts and 0 once it
 * runs out. Cells that are not workshops are left untouched -- the pass never clears them.
 *
 * The needed-flag grid is the third grid in the same per-base block: cell type at
 * grid + 480 + 144*nBaseIndex, needed flags at grid + 444 + 144*nBaseIndex, occupancy at
 * grid + 36*nBaseIndex.
 *
 * The original's own type names say what those three are. IDA carries BaseFacilityGrid,
 * size 1524, as buildDays u8[8][6][6] at +0x000 followed by fac BaseFacBlock[8] at +0x174,
 * and BaseFacBlock, size 144, as tileA, tileB, layer2 and facType, each a 36-byte
 * BaseFacLayer6. Mapping those onto the offsets used here:
 *
 *     grid + 36*nBaseIndex          buildDays[nBase]
 *     grid + 444 + 144*nBaseIndex   fac[nBase].layer2
 *     grid + 480 + 144*nBaseIndex   fac[nBase].facType
 *
 * So what this port first called the occupancy grid is the original's buildDays, and a cell
 * counting as free means its build days are zero, that is nothing under construction there,
 * rather than that no facility owns the cell.
 *
 * Two things about the original worth recording.
 *
 * It calls Base_CalcWorkshopCap at 0x65BC0 and DISCARDS the result, then builds the budget
 * from Base_CountActiveManuf instead. That call is pure -- the capacity function reads the
 * grid and returns, with no store anywhere -- so the port omits it. This is the one
 * deviation, and it cannot change behaviour.
 *
 * Its return value is loop-counter residue. The inner counter is reset to 0 at 0x65C20 on
 * every row and always runs to 6, so the function returns 6 no matter what it did. The
 * port reproduces 6 rather than inventing a status. */
i32 Base_UpdateWorkshopNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nActiveManuf);

/* Base_UpdateLabNeeded @ 0x65c7c. The same pass as Base_UpdateWorkshopNeeded with the
 * cell kind changed to 2, a lab, and the budget taken from Base_CountActiveResearch.
 * It also discards Base_CalcLabCap at 0x65C88 for the same reason, and returns the same
 * loop-counter residue of 6. */
i32 Base_UpdateLabNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nActiveResearch);

/* ------------------------------------------------- the other three needed passes
 *
 * Evidence: Base_UpdateLivingQuartersNeeded @ 0x65e24, Base_UpdateStoresNeeded @ 0x65d44,
 * Base_UpdatePsiLabNeeded @ 0x65fc8.
 *
 * All five needed passes are the same machine. Only the cell kind and the divisor change:
 *
 *     pass                kind   divisor   budget total comes from
 *     LivingQuarters        1        50     CalcOccupiedSpace + CountOutboundTransferSpace
 *     Lab                   2        50     CountActiveResearch
 *     Workshop              3        50     CountActiveManuf
 *     Stores                6       300     CalcStorageUsed + CountItemQtyAcrossBases
 *     PsiLab                8        10     CountPsiSoldiers
 *
 * That table is also the cell-kind map for fac[nBase].facType: 1 living quarters, 2 lab,
 * 3 workshop, 6 stores, 8 psi lab, and 4, 5 and 9 the three states the site-state
 * classifier reads.
 *
 * Each one discards its own Base_Calc*Cap call, and each returns the same loop-counter
 * residue of 6. The budget totals come from functions that are not ported yet, so the port
 * takes each total as an argument, exactly as it does for the quantity columns. */
i32 Base_UpdateLivingQuartersNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nSpaceNeeded);
i32 Base_UpdateStoresNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nStoresNeeded);
i32 Base_UpdatePsiLabNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nPsiSoldiers);

/* Base_UpdateAlienContainmentNeeded @ 0x65f04. The sixth needed pass, and the same machine as the
 * other five: cell kind 7 (alien containment), budget `ammoItems / 10 + 1`, or 0 when there is no
 * ammo at all.
 *
 * Like the other four it calls its own capacity function first and throws the answer away. ARM at
 * 0x65F10 is `BL Base_CalcAlienContainmentCap` followed by `MOV R0, R4`, so the return register is
 * overwritten before anything reads it. The port simply does not make the call.
 *
 * The base index is narrowed to a char at 0x65F0C, reproduced. */
i32 Base_UpdateAlienContainmentNeeded(u8 *abFacilityGrid, i32 nBaseIndex, i32 nAmmoItems);

/* ------------------------------------------------- the daily build-timer countdown
 *
 * Evidence: Base_TickFacilityConstruction @ 0x62824 (396 B), called from Geoscape_TickBases @
 * 0x5d3bc with abFacilityGrid as its only argument.
 *
 * For every base (nBaseCount, read signed from abFacilityGrid + 0x618) it walks that base's
 * buildDays 6x6 block and decrements every non-zero byte. When a byte reaches zero the build
 * finished: the six needed-flag passes run for the base, the shortfall dword is raised, and
 * message 38 is posted. If the finished cell's kind is a hangar (11) three MORE buildDays bytes
 * are cleared too -- the cell directly below (0x628C4), the one below-right (0x628CC) and the one
 * to the right (0x628D4). Those three stores are the original's own arithmetic (R8[j+7] and
 * R8[j+1] on the running days pointer): they land in the days block rather than in any facility
 * layer, and at the last row they reach into the NEXT base's days block. The grid is big enough
 * for that, so the port reproduces it.
 *
 * The message payload is (38, base, row, col, 0). UiPanel_AppendLineWithSound takes six arguments,
 * so a3 is R2, a4 is R3, a5 is [SP+0] and a6 is [SP+4]: 0x62938 puts the base in R2, 0x6292C's
 * `AND R3, R7, #0xFF` puts the ROW in R3, 0x62944 stores the COLUMN (R5) at [SP+0] and 0x62930
 * stores R9 -- the zero the completion writes with -- at [SP+4]. The `var_8 = 6*nBase` running
 * offset is a LOCAL at [SP+8], past the callee's last argument; reading it as a7 is what makes the
 * payload look different from Hex-Rays. It is not.
 *
 * The six needed passes take the budgets the original computes inside them: ammo items, active
 * research, occupied space plus outbound transfer space (the -1 gate), psi soldiers, storage used
 * plus item quantity across bases (the -1 gate), active manufacturing.
 *
 * Deviation: the original returns the last day byte it read (register residue); the port returns
 * void. */
typedef struct BaseTickConstructionEnv {
    u8              *abFacilityGrid;   /* the argument, always abFacilityGrid @ 0x159768 */
    const u8        *pbBaseCount;      /* nBaseCount @ 0x159D80, read signed */
    Soldier         **apSoldiers;      /* Map.apSoldiers @ Map + 0x1198 */
    const i32       *pnSoldierCount;   /* nSoldierCount @ 0x115350 (u16 semantics) */
    BaseRec         **apBases;         /* Map.apBases @ Map + 0x1174 */
    ObjectRecFull   **apObjects;       /* Map.apObjects @ Map + 0x108 */
    i32              nObjectCount;     /* Map_nObjectCount @ 0x114A60 */
    SmapSite        *aSites;           /* aSmapSites @ 0x162030 */
    i32              nSiteCount;       /* nSmapSiteCount @ 0x186410 */
    const u16       *apResearchCols;   /* 0x16022C, 144 u16 per base */
    i32              nResearchEntries; /* 0x160C4C, shared by every base */
    const u16       *apManufCols;      /* 0x15E19C, 200 u16 per base */
    i32              nManufEntries;    /* PUFO_PRODUCT_ENTRIES_PER_BASE */
    u32             *pdwShortfall;     /* byte_1870C8, written as a dword */
    UiLineList      *pLines;           /* the root message list @ 0x158890 */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void *pSoundUser;
} BaseTickConstructionEnv;

void Base_TickFacilityConstruction(const BaseTickConstructionEnv *pEnv);

/* Base_CountAmmoItems @ 0x656ac. How much ammo is stored for one base: the per-base quantity column
 * of every object whose index is 77..104 inclusive, plus object 114.
 *
 * The original reaches apObjects through its first argument, which ARM shows is the Map base: at
 * 0x656F0 the load is `[R2 + 4*(i-1) + 4 + 0x108]`, which is apObjects[i]. The port takes the array
 * instead. The base index is a char in the original.
 *
 * The `!= 0` test on each quantity is cosmetic: adding zero is a no-op. */
i32 Base_CountAmmoItems(ObjectRecFull *const *apObjects, i32 nObjectCount, i32 nBaseIndex);

/* Base_CalcOccupiedSpace @ 0x655f8. How much of a base's space is taken: every soldier whose base
 * slot (Soldier.nKind) matches, plus the per-base quantity of object 112 and object 113.
 *
 * Those two indices are the only place either number is used, and the pair is checked with two
 * compares against 0x70 and 0x71 rather than a range test. */
i32 Base_CalcOccupiedSpace(Soldier *const *apSoldiers, i32 nSoldierCount,
                           ObjectRecFull *const *apObjects, i32 nObjectCount,
                           i32 nBaseIndex);

/* BaseRec_AddItemQty @ 0x556e4. Two adds, no clamping and no floor at zero:
 *
 *     pItem->auPerBaseVal[(char)pBase->bSlot] += nDelta;
 *     pBase->nStoresTotal                     += nDelta;
 *
 * The slot index is BaseRec.bSlot @ +0x05 widened as a char, unchecked. The port skips
 * both adds when the slot is outside the eight entries (deviation). */
void BaseRec_AddItemQty(BaseRec *pBase, ObjectRecFull *pItem, i32 nDelta);

/* BaseRec_SubItemQty @ 0x5571c. The mirror: subtract, with the same no-clamp rule and the same
 * unchecked slot byte. BasePlaneScreen_CommitStores @ 0x7ac38 is the caller that needs it. */
void BaseRec_SubItemQty(BaseRec *pBase, ObjectRecFull *pItem, i32 nDelta);

/* ------------------------------------------------- writing one facility into a base grid
 *
 * Evidence: BaseFac_SetCellBuildState @ 0x623bc, called from BaseFacScreen_DrawPlacementOverlay
 * (0x599a8) and ten times from Game_StartNew (0x94554).
 *
 * The grid is the original's BaseFacilityGrid, gBaseFacGrid @ 0x159768, 1524 bytes: buildDays
 * u8[8][6][6] at +0x000, then fac BaseFacBlock[8] at +0x174, each block 144 bytes of tileA,
 * tileB, layer2 and facType, each a 6 by 6 layer.
 *
 * Two row bases are formed and every store goes through one of them:
 *
 *     fac row   6 * (24*nBaseIndex + nY) + nX   == 144*nBaseIndex + 6*nY + nX
 *     days row  6 * (6*nBaseIndex  + nY) + nX   ==  36*nBaseIndex + 6*nY + nX
 *
 * The ARM multiplies nY, so nY is the coordinate the other readers of this grid call the column.
 *
 * nBuildDays is aFacBuildSpec[nFacType].nBuildDays, or 0 when bClear is set.
 *
 * An ordinary facility writes three bytes:
 *
 *     fac[nBase].tileA[nY][nX] = aFacBuildSpec[nFacType].bTileA
 *     fac[nBase].tileB[nY][nX] = aFacBuildSpec[nFacType].bTileB    (the +36 layer)
 *     buildDays[nBase][nY][nX] = nBuildDays
 *
 * Facility type 11 is the hangar -- the kind Base_CalcHangarCap counts -- and it is a 2 by 2
 * footprint. It writes four tileA quadrants instead of a tileA/tileB pair:
 *
 *     tileA[nY][nX] = 9, tileA[nY][nX+1] = 10, tileA[nY+1][nX] = 11, tileA[nY+1][nX+1] = 12
 *
 * and buildDays at (nY,nX), (nY,nX+1) and (nY+1,nX+1). The fourth buildDays cell, (nY+1,nX), is
 * never written: the ARM reaches the three as +0, +1 and +7 bytes from the anchor, and +7 is the
 * diagonal. tileB and layer2 are not touched for a hangar at all.
 *
 * Both paths then share the tail, which writes buildDays[nBase][nY][nX] a second time (the same
 * byte the hangar path already wrote) and fac[nBase].facType[nY][nX] = nFacType. Only the anchor
 * cell gets a facType byte, which is why a hangar counts once in Base_CalcHangarCap and not four
 * times even though it marks four tileA cells.
 *
 * Three things the original leaves the port to decide:
 *
 * - Its first argument is the grid pointer and it is never read; every store goes through the
 *   global 0x159768. Both callers pass the grid they mean, so the port uses the argument.
 * - Nothing bounds nBaseIndex, nX, nY or nFacType. aFacBuildSpec has 18 records, so a type above
 *   17 reads past it. The port refuses out-of-range indices (deviation).
 * - The return value is 6*(24*nBaseIndex + nY + 18), the address delta the facType store used. It
 *   is register residue, not a status, and the port reproduces it. */
#define PUFO_FAC_TYPE_HANGAR FAC_HANGAR /* 11, the FacilityKind above */
#define PUFO_FAC_SPEC_COUNT  18
#define PUFO_FAC_BASE_COUNT  8 /* the eight base slots the grid and the columns size for */

i32 BaseFac_SetCellBuildState(u8 *abFacilityGrid, i32 nBaseIndex, u8 nFacType,
                              i32 nX, i8 nY, u8 bClear);

/* aFacBuildSpec @ 0xAA8F8, 18 records of three bytes: the tileA byte, the tileB byte and the
 * build days. The days byte is read with LDRSB at 0x623EC, so it is signed. Record 11 is 0xFF,
 * 0xFF, 19 and is never used as tiles: type 11 is the hangar and takes the 2 by 2 path instead.
 * Ui_DrawBaseInfo @ 0x9B5F4 reads the two tile bytes as basebits glyph indices, which is why the
 * type lives in the header and not beside the table. */
typedef struct FacBuildSpec {
    u8 bTileA;
    u8 bTileB;
    i8 nBuildDays;
} FacBuildSpec;

#define PUFO_FACBUILDSPEC_COUNT 18 /* the aFacBuildSpec record count @ 0xAA8F8 */

/* aFacBuildSpec @ 0xAA8F8: the build days of one facility kind, the third byte of the three-byte
 * record. The original indexes the 18-record table unguarded (0x59E14, 0x623EC); the port returns 0
 * for a kind outside 0..17. */
i8 FacBuildSpec_GetDays(u8 nKind);

/* byte_A6568 @ 0xA6568: facility type -> placement code for the tactical tile-type grid. The
 * hangar entry (11) is 20, the empty sentinel, because the hangar's 2x2 stamp (16..19) overwrites
 * the anchor right after. See Smap_InitCraftSelectState below. */
extern const u8 g_aCraftPlacementCodeByFacType[32];

/* Smap_InitCraftSelectState @ 0x61b94. Fill the 36-byte tactical tile-type grid (0x159D5C) for an
 * assault on base nBaseIndex: 20 = empty; a built facility cell contributes
 * g_aCraftPlacementCodeByFacType[facType] (facType 0xFF and cells under construction contribute
 * nothing); the hangar stamps 16/17/18/19 at byte offsets +0/+1/+6/+7 from its anchor. The port
 * takes the tile grid as a separate pointer (the original reaches it at grid + 1524) and refuses
 * stamp stores past the 36-byte grid. */
void Smap_InitCraftSelectState(u8 *abFacilityGrid, u8 *abTileTypes, i32 nBaseIndex);

/* Base_GrantStarterItems @ 0x95fa4. Four BaseRec_AddItemQty calls against the base the original
 * keeps at Map+0x1174 and nothing else: object 105 gets +10, object 34 +2, object 31 +1 and object
 * 56 +33. The four are Map_apObjects105, Map_apObjects34, Map_apObjects31 and Map_apObjects56 --
 * slots of the object-pointer array that starts at Map+0x108. With bClear zero it does nothing and
 * returns its own argument; the first ARM argument is
 * unused there because the base comes from the global. The port takes the base and the object array
 * and returns nothing. */
void Base_GrantStarterItems(BaseRec *pBase, ObjectRecFull *const *apObjects, u8 bGrant);

/* ------------------------------------------------- the geoscape speed buttons
 *
 * Evidence: GameTime_Set5Sec @ 0x84f00, GameTime_Set1Min @ 0x84f64, GameTime_Set5Min @ 0x84fc8,
 * GameTime_SetSpeedIdx3 @ 0x8502c, GameTime_SetSpeedIdx4 @ 0x85094, GameTime_SetSpeedIdx5 @ 0x850fc.
 *
 * Six one-button handlers, all the same shape: play sample 17, store the tick interval into
 * GeoscapeWorld.nTickSeconds (dword_187070, read by StrategList_UpdateMotion and by the unported
 * Geoscape_PulseUpdate, cleared by Geoscape_CloseUI), and set the nState of the six nav speed
 * controls - the UiControl pointers Geoscape_InitUI keeps at 0x186500..0x186514 - so exactly the
 * button's own control is 1 and the other five are 0. The intervals are 5, 60, 300, 1380, 2460 and
 * 3600 seconds.
 *
 * Each original returns the sixth control pointer (register residue); no caller tests it, so the
 * port returns void. */
#define PUFO_GEOSCAPE_SPEED_COUNT 6
extern const i32 g_aGeoscapeSpeedSec[PUFO_GEOSCAPE_SPEED_COUNT];

void GameTime_Set5Sec(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT]);
void GameTime_Set1Min(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT]);
void GameTime_Set5Min(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT]);
void GameTime_SetSpeedIdx3(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT]);
void GameTime_SetSpeedIdx4(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT]);
void GameTime_SetSpeedIdx5(GeoscapeWorld *pWorld, UiControl *const apSpeedCtl[PUFO_GEOSCAPE_SPEED_COUNT]);

/* ------------------------------------------------- creating a new base entry
 *
 * Evidence: Base_CreateNamedEntry @ 0x66930. Two callers: Game_StartNew @ 0x94964 with
 * (-6000, -6000, 0, "new") for the starting base -- region 0, which is not one of the ten
 * regions and is the original's own quirk, see step 5 -- and BaseFacScreen_DrawPlacementOverlay @
 * 0x59AFC with the pending placement coordinates (Map+0x62C / +0x630) and the pending region
 * (byte_159DF4).
 *
 * What it does, in the original's order:
 *   1. BaseRec_Create: a new 20-byte BaseRec at apBases[nBaseCount] (the count is NOT raised
 *      yet). The original then dereferences apBases[nBaseCount] with no null test (0x66A08);
 *      the port guards (deviation).
 *   2. StrategList_AddItem: a type-0, inactive, invisible-flag-1 site entry at (x, y) with
 *      nId = the next site id (dword_186558).
 *   3. The strateg entry at index nBaseCount gets bRandA = nRegionId - 0x80 (0x669B4; Hex-Rays
 *      renders the SUB as + 0x80, the ARM is `SUB R11, R10, #0x80`).
 *   4. The BaseSiteRec for this base (0x186C40 + 64*nBaseCount): abName = the name copied to its
 *      NUL with no bound (0x669D0 - ORIGINAL BUG, the field is 20 bytes and the overflow tail
 *      lands in the four dwords and past the record), bType = nRegionId - 0x80, nUnk48 /
 *      nDemolishState / nBuildState = 0, nFacType = the site id.
 *   5. The BaseRec: bRegionId = nRegionId, nUnk8 = x, nUnk12 = y, and bUnk16 is the region
 *      counter minus one. The load is `0x1864DD + region` (0x66A64 LDRSB [R3,#-0x1B] off a
 *      0x1864F8 + region base), which for the ten regions 0x80..0x89 is the region counter table
 *      g_anRegionBuildCount @ 0x18655D. Game_StartNew passes region 0, so the starting base reads
 *      the byte at 0x1864DD itself -- the tail of szScratch186488, zero in the shipped image --
 *      and gets bUnk16 = -1. The port reads 0 for any region below 0x80 (deviation, same value).
 *   6. When Map_nObjectCount is non-zero, clear this base's economy columns: the first
 *      nObjectCount u16 of the research column (144 entries per base, so 149 objects spill five
 *      into the next base - reproduced), and entries [i] and [i + 2176] of the u16 product
 *      column, [i] and [i + 1600] of the u32 product column. The column pointers are passed
 *      already offset to this base: 0x159E9C + 800*n (u32), 0x15D09C + 400*n (u16),
 *      0x16022C + 288*n (u16).
 *   7. ++site id, ++base count.
 *
 * The return value is the constant 1560 (0x618, the offset the count store used - register
 * residue). Reproduced. */
typedef struct BaseCreateEnv {
    BaseRec **apBases;          /* Map + 0x1174 */
    ObjectRecFull *const *apObjects;
    u16 nObjectCount;           /* Map_nObjectCount */
    i8 *pbBaseCount;            /* 0x159D80, signed: the new base's slot, raised at the end */
    PtrArray *pStrategList;     /* strategList @ 0x186534 */
    i32 *pnSeed;                /* 0x18640C, the per-item seed counter */
    i32 nZoomMode;              /* 0x1863E8; type 0 does not scale coordinates */
    i32 *pnNextSiteId;          /* dword_186558 */
    BaseSiteRec *apSiteRecs;    /* 0x186C40, eight 64-byte records */
    const u8 *paRegionBuildCount; /* g_anRegionBuildCount @ 0x18655D, indexed by region - 0x80 */
    u32 *pProductU32Col;        /* 0x159E9C + 800 * nBaseCount */
    u16 *pProductU16Col;        /* 0x15D09C + 400 * nBaseCount */
    u16 *pResearchCol;          /* 0x16022C + 288 * nBaseCount */
} BaseCreateEnv;

i32 Base_CreateNamedEntry(const BaseCreateEnv *pEnv, i32 x, i32 y, i32 nRegionId, const char *pszName);

/* ------------------------------------------------- can this cell host a hangar
 *
 * Evidence: Base_CanPlaceHangarAt @ 0x629b0. Its only code caller is Geoscape_OnMapClick @
 * 0x8B688, in the screen-1 branch: a non-zero result lets the caller clear the tapped cells, a
 * zero result sets bBasePlaceFail (0x1870B8).
 *
 * IDA's signature is wrong and the port corrects it. The original's arguments are
 * (grid, nX, nY, unused). The base index is not an argument at all: it is the SIGNED byte at
 * grid + 0x619 -- nSelectedBaseIdx @ 0x159D81, which sits 1561 bytes into a 1524-byte grid and
 * is reached as `LDRSB R0,[R10,#0x619]` at 0x629C8 -- and the fourth argument is never read (the
 * ARM sign-extends only R1 and R2, at 0x629E0 and 0x629E8). Both coordinates are sign-extended
 * bytes, and the one multiplied by 6 is nY, the same row base BaseFac_SetCellBuildState forms.
 *
 * The decision, in the original's order:
 *
 *   1. buildDays[nBase][nY][nX] > 0 -> 1. Both tests (0x62A30 for a hangar cell, 0x62A50 for any
 *      other) jump to the `MOV R0,#1` tail at 0x62D44, so a cell that is under construction is
 *      accepted outright and none of the rest runs. The facType == 11 gate on the first test is
 *      redundant: the second test covers it.
 *   2. scratch[row][col] = 1 for a cell that carries a tileA stamp AND has finished build days,
 *      0 for everything else (0x62A7C..0x62AA8). A cell whose facType is 11 with finished build
 *      days also writes 1 into the three other cells of its 2 by 2 footprint (0x62AB8..0x62B10) -
 *      and that marking is dead. The very same loop visits those three cells afterwards, in
 *      row-major order, and its own `*v14 = v15` store overwrites each mark with the cell's own
 *      value. Only the row 5 marks escape that, and they are the stack overrun below. A real
 *      hangar is connected by its four stamped tileA bytes, not by this marking.
 *   3. the tapped footprint is cleared back to 0 (0x62B3C..0x62B60): the candidate site counts as
 *      free ground for the test that follows.
 *   4. the first remaining 1 in row-major order becomes 2 (0x62B8C..0x62BD0), then a sweep
 *      (0x62BE0..0x62CC8) turns every 2's four neighbours that are neither 0 nor 3 into 2 and
 *      marks the source 3, repeating the whole sweep while any sweep changed something. That is
 *      a flood fill of the connected component of finished cells. Note the "changed" flag is
 *      shared across the sweep rather than per cell, and a neighbour that is already 2 counts as
 *      a change -- both reproduced, because both change which cells end up marked 3.
 *   5. any 1 still left -> 0 (0x62CE0). So the placement is refused exactly when clearing the
 *      tapped footprint would cut some finished facility off from the rest of the base.
 *   6. a tapped cell whose facType is 11 answers instead `Base_CalcHangarCap -
 *      Base_CountShipsAtBase > 0` (0x62D14..0x62D40): a free hangar slot has to exist. Both
 *      callees are passed nSelectedBaseIdx.
 *   7. otherwise (0x62D50..0x62D6C): layer2[nY][nX] != 0 -> 0, facType == 0 -> 0, else 1. Empty
 *      ground cannot host a hangar, and neither can a cell the needed-flag layer has claimed.
 *
 * The hangar branch's second callee is the reason the env carries the rosters. The original hands
 * Base_CountShipsAtBase the facility grid pointer (0x62D38), but that pointer is dead: the callee
 * passes it to BaseGrid_GetByte0x654, whose own first argument is unused because it reads the
 * fixed table at abFacilityGrid + 1620 = 0x159DBC (0x6ACE4) -- the eight base grid bytes the save
 * layer carries as SaveBasesState.abUnk159DBC. The port passes that table instead.
 *
 * Deviations: the original bounds nothing (nBaseIndex, nX and nY are used raw), and its scratch
 * buffer is the 36 dwords between sp+0x10 and the end of the 0xA0 frame while the 2 by 2 marking
 * at row 5 writes scratch[36..42] into the saved registers -- Hex-Rays sizes the array as
 * var90[46] precisely because it sees those overrun stores. The port bounds the base index and
 * the coordinates and clamps the footprint stores to the 36 cells; both rows are in bugs.tsv. */
typedef struct HangarPlaceEnv {
    const u8 *abFacilityGrid;    /* gBaseFacGrid @ 0x159768 */
    const u8 *abGridBytes;       /* 0x159DBC, the eight bytes Base_CountShipsAtBase adds on */
    i8  nSelectedBaseIdx;        /* nSelectedBaseIdx @ 0x159D81, the original's grid + 0x619 */
    Ship *const *apCraft;
    i32 nCraftCount;
    BaseRec *const *apBases;
    i32 nBaseCount;
} HangarPlaceEnv;

i32 Base_CanPlaceHangarAt(const HangarPlaceEnv *pEnv, i32 nX, i32 nY);

/* ------------------------------------------------- the new-base label of the build dialog
 *
 * Evidence: BaseFac_FormatCostLabel @ 0x6a404. Two callers: BaseFacScreen_DrawPlacementOverlay @
 * 0x59AE4, and Geoscape_OnMapClick @ 0x8B550, which copies the result into the aBaseFacilities
 * record's label field (0x186C5A, that is record + 26) after storing region + 0x80 at record + 0
 * (the byte wraps, so the stored bType is the region index 0..9).
 *
 * Its second argument is the PENDING REGION id 0x80..0x89 (byte_159DF4), not a facility kind --
 * both callers pass byte_159DF4 straight through. It builds `TEXTS[905 + region]` + "-" +
 * decimal(g_anRegionBuildCount[region - 0x80]) into the shared scratch string at 0x186B10
 * (szRadarMsg, dword_186B08 + 8 -- the same buffer the radar screen, Soldier_FormatDisplayName and
 * Ship_FormatDisplayName write), and then INCREMENTS that counter. So the number the label shows
 * is the counter's value before the call, and calling it twice for the same region shows one more
 * the second time. TEXTS[905 + 0x80] = TEXTS[1033] is "Северная Америка", so a base placed in
 * North America is named "Северная Америка-0".
 *
 * g_anRegionBuildCount @ 0x18655D is a 10-byte runtime table, one signed counter per region, all
 * zero in the image, and it has no xref in the IDB at all: every access is computed as
 * 0x1864F8 + region - 0x1B for the load (0x6A46C, i.e. 0x1864DD + region) and 0x1864F8 + 0x65 +
 * region - 0x80 for the store (0x6A49C, i.e. 0x18655D + region - 0x80) -- the same byte either
 * way, which is why Hex-Rays renders it as an expression on the unrelated pObj global instead. It
 * is the same table Base_CreateNamedEntry reads for BaseRec.bUnk16, the same one InitUI seeds with
 * nine 1s and StartNew seeds with ten, and the same nine bytes the save writes at 0x3F304 (region
 * 0x89's tenth counter is not saved).
 *
 * The counter is loaded SIGNED (LDRSB) and the increment is sign-extended before the byte store
 * (0x6A490..0x6A49C), so 127 wraps to -128.
 *
 * The original's first argument (the grid pointer) is never read: R0 is replaced by the label
 * buffer before the first strcat (0x6A458). The port drops it. The name copy is bounded here
 * (pufo_strncpy) where the original copies with no bound at all. */
#define PUFO_REGION_NAME_REC_BASE   905 /* TEXTS[905 + region] is the region's own name */
#define PUFO_REGION_BUILD_COUNT     10  /* g_anRegionBuildCount @ 0x18655D, one per region */

char *BaseFac_FormatCostLabel(char *pszLabel, size_t cchLabel, const u8 *pTextRecs,
                              u8 nRegionId, i8 *paRegionBuildCount);

/* --- RadarScreen_BlitOverlay @ 0x5d6d4 (16 B) ---
 *
 * The whole function is `PadImage_Blit(pRadarOverlayPad @ 0x1596E4)`, the pad RadarScreen_Init
 * (0x5F490) allocates. The port takes the pad as an argument. */
void RadarScreen_BlitOverlay(PadImage *pOverlayPad);

/* --- RadarScreen_RollWeaponHit @ 0x5db24 (352 B) : the intercept weapon roll ---
 *
 * One roll of Game_RandU16(100) gates the event, and a second roll picks the damage band. The
 * target is the intercept pair the radar screen keeps: nInterceptStrategIdx @ 0x159680 selects the
 * strateg item and nInterceptTargetHp @ 0x159698 is its running HP, which the function writes back
 * into StrategItem.nHp @ +0x6c.
 *
 *   event 25 (0x5DC6C): roll < 50,  hp -= 50
 *   event 26 (0x5DC44): roll < 80,  hp += RandU16(20) - 100
 *   event 27 (0x5DC1C): roll < 90,  hp += RandU16(20) - 120
 *   event 28 (0x5DBF4): roll < 70,  hp += RandU16(30) - 90
 *   event 29 (0x5DBCC): roll < 70,  hp += RandU16(50) - 140
 *   event 30 (0x5DB70): roll < 95,  hp += RandU16(30) - 230
 *
 * Any other event, or a roll that fails its gate, changes nothing and returns false. The HP write
 * at 0x5DB90 comes before the strateg write at 0x5DBB0, and the HP can go negative -- nothing
 * clamps it here. Deviation: the original dereferences PtrArray_Get(strategList, nInterceptStrategIdx)
 * without a bounds or null test; the port skips the strateg write when the lookup fails. */
typedef struct RadarHitRollEnv {
    PtrArray *pStrategList;   /* strategList @ 0x186534 */
    const i32 *pnStrategIdx;  /* nInterceptStrategIdx @ 0x159680 */
    i32 *pnTargetHp;          /* nInterceptTargetHp @ 0x159698 */
} RadarHitRollEnv;

bool RadarScreen_RollWeaponHit(const RadarHitRollEnv *pEnv, i32 nEventType);

/* --- the radar screen's four corner buttons @ 0x5e768 / 0x5e808 / 0x5e93c / 0x5ea70 ---
 *
 * Evidence: RadarScreen_SetCursorNW (160 B), RadarScreen_SetCursorNE (308 B),
 * RadarScreen_SetCursorSW (308 B), RadarScreen_SetCursorSE (160 B).
 *
 * All four open on byte_159714: a non-zero value makes them do nothing. RadarScreen_Init clears it
 * (0x5F4E0); RadarScreen_Update and RadarScreen_RollDetection read it, and neither is ported, so
 * the port takes it as an injected byte.
 *
 * They park the radar cursor at one of four corners. That cursor really is a pixel position:
 * RadarScreen_Update (0x60570, not ported) loads unk_1596D4 at its 0x60F4C into
 * SoftSurf_BlitRectProxy's x argument (`ADD R1, R0, R1,ASR#1 ; ADD R1, R1, #1 ; x`). The four values
 * pair up as west/east and
 * north/south:
 *
 *     NW 117, 86    NE 161, 86    SW 117, 107    SE 161, 107
 *
 * and both halves clear the message timer and the shared scratch string at 0x186B10. The string
 * "copy" is a clear, not a write: the loop runs from a source that is all zero in the image
 * (0x10360F, 0x1036DC, 0x1037A9, 0x103876 -- each referenced only by its own setter), so exactly one
 * NUL lands. Nothing in the binary fills those four sources.
 *
 * The two halves differ in three ways, and the differences are not symmetric:
 *   - NW and SE write a fixed 560 into the range target. NE and SW compute
 *     bound + 560 * (26 - nLevel) / 114 with a SIGNED divide (_rt_sdiv, which this binary calls with
 *     the dividend in R1 and the divisor in R0 -- the same convention Base_UpdateWorkshopNeeded uses
 *     for its total/50), taking the MAX of the two bounds for NE (0x5E884 BGT path) and the MIN for
 *     SW (0x5E9B8 BGT path).
 *   - NE and SW bail first when both intercept slots hold the -2 sentinel (0x5E818 / 0x5E94C). NW
 *     and SE have no such guard.
 *   - SE is the only one that sets the flip byte to 1; NW, NE and SW all set it to 0.
 *
 * The range target is the value RadarScreen_CyclePalette @ 0x5ed0c animates nRadarRangeCur toward,
 * one step at a time, which is why setting it is not the same as setting the range.
 *
 * All four are installed as click handlers (0x5FCB4..0x5FD24) and their return values -- the gate
 * byte, or &nRadarMsgTimer -- are never read, so the port returns void. */
#define PUFO_RADAR_CURSOR_WEST    117
#define PUFO_RADAR_CURSOR_EAST    161
#define PUFO_RADAR_CURSOR_NORTH    86
#define PUFO_RADAR_CURSOR_SOUTH   107
#define PUFO_RADAR_RANGE_FULL     560 /* the fixed target NW and SE use, and the scale the other two use */
#define PUFO_RADAR_LEVEL_MAX       26 /* 26 - nLevel is the fraction of that scale still to add */
#define PUFO_RADAR_RANGE_DIVISOR  114
#define PUFO_RADAR_NO_TARGET       (-2) /* both intercept slots hold this and NE/SW do nothing */

typedef struct RadarCursorEnv {
    const u8 *pbGate;        /* byte_159714 */
    i32      *pnCursorX;     /* unk_1596D4 */
    i32      *pnCursorY;     /* unk_1596D8 */
    i32      *pnRangeTarget; /* nRadarRangeTarget @ 0x15965C, animated toward by nRadarRangeCur */
    u8       *pbFlip;        /* unk_159718 */
    i32      *pnMsgTimer;    /* nRadarMsgTimer */
    char     *pszMsg;        /* szRadarMsg @ 0x186B10 = dword_186B08 + 8 */
    size_t    cchMsg;
    const i32 *pnInterceptA; /* unk_159654, the NE/SW guard pair */
    const i32 *pnInterceptB; /* unk_159664 */
    const i32 *pnLevel;      /* unk_1596E0 */
    const i32 *pnBoundA;     /* dword_1596E8 */
    const i32 *pnBoundB;     /* dword_1596EC */
} RadarCursorEnv;

void RadarScreen_SetCursorNW(const RadarCursorEnv *pEnv);
void RadarScreen_SetCursorNE(const RadarCursorEnv *pEnv);
void RadarScreen_SetCursorSW(const RadarCursorEnv *pEnv);
void RadarScreen_SetCursorSE(const RadarCursorEnv *pEnv);

/* ------------------------------------------------- the radar palette cycle and the range animation
 *
 * Evidence: RadarScreen_CyclePalette @ 0x5ed0c (232 B).
 *
 * Two jobs in one function.
 *
 * 1. Rotate a palette. The ARM takes aPalette @ 0x1472D0 as its base, works from base+0x60 down to
 *    base+0x4, and each step copies [v+0x57FC] over [v+0x5800]; then it stores the saved top entry
 *    back into the bottom. base+0x5800 is aPalette + 22 * 1024, so the block is slot 22 entries 0..24
 *    (0x14CAD0..0x14CB30): 25 entries shifted up one place with entry 24 wrapping into entry 0. The
 *    IDB gives those two endpoints no xref outside this function, so the reader is reached through the
 *    aPalette base in the radar draw path -- RadarScreen_BlitMapBackdrop (0x5eb10, not ported),
 *    RadarScreen_Update (0x60570, not ported) and RadarScreen_Init (0x5f490, not ported) all load
 *    0x1472D0 -- but which of them picks slot 22 is not resolved in the IDB and is not asserted here.
 *
 * 2. Nudge the animated range toward its target. Four conditional steps, each storing:
 *      if (cur < tgt) cur++;   if (cur > tgt) cur--;   if (cur < tgt) cur++;   if (cur > tgt) cur--;
 *    The pairs are an overshoot-and-correct: a value well below the target ends up two steps closer,
 *    a value well above ends up two steps lower, and a value already at the target does not move.
 *    Then, only when the flip byte unk_159718 is set (SE is the corner that sets it) and the value is
 *    still below the target, up to four more single steps. The flip branch is the only asymmetric
 *    part: there is no matching branch for a value still ABOVE the target. */
#define PUFO_RADAR_PAL_SLOT  22 /* aPalette + 22 * 1024 = 0x14CAD0 */
#define PUFO_RADAR_PAL_CYCLE 25 /* 0x14CAD0 .. 0x14CB30 inclusive */

typedef struct RadarPaletteEnv {
    i32      *pnRangeCur;    /* nRadarRangeCur @ 0x15966C, the animated value */
    i32      *pnRangeTarget; /* nRadarRangeTarget @ 0x15965C */
    const u8 *pbFlip;        /* unk_159718, set by RadarScreen_SetCursorSE */
} RadarPaletteEnv;

void RadarScreen_CyclePalette(const RadarPaletteEnv *pEnv);

/* --- the geoscape base-tab strip @ 0x671B0 ---
 *
 * Evidence: Base_HitTestFacilityStrip (204 B), drawn by Base_DrawFacilityIconStrip (0x6250c, not
 * ported).
 *
 * The IDB name says "facility", and the drawer confirms what the strip actually is: one cell per
 * base, stacked down the right edge of the 240x320 screen. Base_DrawFacilityIconStrip blits the
 * selected cell's erase rect at x = 211, y = 28 * nSelectedBase + 3, 26x26 (0x62520), and walks the
 * bases with a 28-pixel vertical pitch (0x6250C's `v5 += 28`). So the hit test's box is a vertical
 * band, not a horizontal one:
 *
 *     x in [211, 237]   y in [3, 28 * nBaseCount]   hit -> base index = y / 30
 *
 * Both callers pass the geoscape cursor pair, x first: Geoscape_OnPointer (0x87F30, R5 =
 * nSmapCursorX, R8 = nSmapCursorY) stores the result straight into nSelectedBaseIdx (0x87F34), and
 * TransferScreen_Draw (0x98980) stores it into unk_1870F4 and then indexes apBases with it (0x9899C
 * / 0x989B0, Map + 0x1174). A miss returns the byte it was going to overwrite -- the same
 * nSelectedBaseIdx read back out of abFacilityGrid + 0x619 -- so the caller's unconditional store
 * leaves the selection unchanged.
 *
 * Three gate bytes disable the strip entirely. When any of unk_1870EC (the transfer screen),
 * unk_15883C (the base-info screen) or byte_15EEF0 (the psi screen / base-stats panel) is non-zero,
 * the ARM jumps to 0x6723C, overwrites the x argument with 10, and branches back INTO the middle of
 * the compare chain at 0x671E8. 10 < 211, so the range test always misses. The port keeps the
 * forced value rather than short-circuiting, because that is literally what the code does.
 *
 * A hit also clears the low byte of dword_1870B4[2] @ 0x1870BC (0x67250). That byte is the geoscape
 * tap flag: Geoscape_OnPointer sets it to 1 at 0x87ED4 after computing the tap cell coordinates,
 * Geoscape_OnMapClick tests it at 0x8B660 before the hangar-placement branch, and clicking a base
 * tab cancels the pending tap.
 *
 * The two base bytes are reached in the original as abFacilityGrid + 0x618 and + 0x619 -- 1560 and
 * 1561, which is 36 bytes PAST the 1524-byte facility grid the symbol names. They are the base
 * count and the selected base sitting in the same block, not part of the grid, so the port takes
 * them as their own pointers instead of indexing a 1524-byte array past its end.
 *
 * ORIGINAL BUG (bugs.tsv): in Base_HitTestFacilityStrip @ 0x671B0 the drawer's pitch and the hit
 * test's divisor disagree. Base_DrawFacilityIconStrip (0x6250c, not ported) steps 28 pixels per
 * base, but 0x67264 divides the y by 30, so from the third cell down
 * the top of each cell selects the PREVIOUS base -- y = 59 (the first row of the third cell, drawn
 * at 28*2 + 3) reports base 1, and y = 199 (the first row of the eighth) reports 6. Reproduced,
 * not fixed. */
#define PUFO_BASE_STRIP_X_FIRST 211 /* the drawer's own x, and the erase rect's */
#define PUFO_BASE_STRIP_X_LAST  237 /* 211 + 26 - 1, the erase rect's right edge */
#define PUFO_BASE_STRIP_Y_FIRST   3 /* the drawer's y = 28 * base + 3 */
#define PUFO_BASE_STRIP_PITCH    28 /* the drawer's per-base step, and the hit bound */
#define PUFO_BASE_STRIP_DIVISOR  30 /* the hit test's own divisor -- see the bug note above */

typedef struct BaseStripHitEnv {
    const u8 *pbBaseCount;    /* 0x159D80 = abFacilityGrid + 0x618, read signed */
    const u8 *pbSelectedBase; /* 0x159D81 = abFacilityGrid + 0x619, read signed: a miss returns it */
    const u8 *pbGateTransfer; /* unk_1870EC, the transfer screen */
    const u8 *pbGateBaseInfo; /* unk_15883C, the base-info screen */
    const u8 *pbGatePsi;      /* byte_15EEF0, the psi screen / base-stats panel */
    u8       *pbTapFlag;      /* low byte of dword_1870B4[2] @ 0x1870BC */
} BaseStripHitEnv;

i32 Base_HitTestFacilityStrip(const BaseStripHitEnv *pEnv, i32 nX, i32 nY);

/* --- the base-tab strip drawer @ 0x6250c ---
 *
 * Evidence: Base_DrawFacilityIconStrip (792 B), called from BaseInfoScreen_Draw (0x57488),
 * PsiScreen_Draw (0x727d8, with the highlight argument -1), Geoscape_Draw twice
 * (0x922c0, 0x92d40) and TransferScreen_Draw (0x98744).
 *
 * Both arguments come from the same block: every caller passes &abFacilityGrid in R0, and the
 * function reads its two count bytes back out of that pointer at +0x618 and +0x619 -- the same
 * nBaseCount and nSelectedBaseIdx the hit test reads. R1 is the row to mark.
 *
 * First (0x62520..0x6257C): when nSelectedBaseIdx != nBaseCount, a 26x26 block at
 * (211, 28 * nSelectedBaseIdx + 3) with 0xFF / 0xFF / 0x64 -- a red-topped erase rect over the
 * selected cell. With no base selected the byte is -1 and the rect lands at y -25, off screen.
 *
 * Then one 6x6 block per base (0x625C4..0x62818), 28 pixels apart, six rows of six cells at
 * x = 212 + 4 * column and y = 28 * base + 4 + 4 * row. Each cell reads two bytes:
 *
 *   abFacilityGrid[36 * base + 6 * row + column]              -- buildDays, the construction timer
 *   abFacilityGrid[372 + 144 * base + 6 * row + column]      -- fac[base].tileA, the tile itself
 *
 * A cell with no tile is grey: 4x4 at 0x14 and 2x2 at 0x28, all three channels equal (0x626B8).
 * A cell with a tile gets a status marker instead -- under construction (buildDays non-zero) puts
 * 0x96 in the red field and 0xFF in the 2x2, finished puts 0x96 in the green field and 0xFF there
 * (0x6260C / 0x62660). Because the grey pair is drawn after the status pair it wins, so an empty
 * cell never shows a status colour.
 *
 * The highlight argument marks one base row with a chevron: 23 two-pixel blocks down-right from
 * (212, 28 * base + 4) and 23 up-right from (212, 28 * base + 26), meeting at x 234 (0x62720..
 * 0x62798). It sits inside the per-cell loop, so the original repaints the same 46 blocks 36 times
 * for the marked base -- idempotent, and reproduced. -1 matches no row.
 *
 * The chevron loop runs on the register holding the base row's y and decrements it 23 times; the
 * loop end reloads it from the stack slot, so the corruption is discarded. The port computes both
 * diagonals from the row's y directly. */
#define PUFO_STRIP_CELL_X_FIRST   212 /* 0x62600: x = 212 + 4 * column */
#define PUFO_STRIP_CELL_PITCH       4 /* the step between the 4x4 cell blocks */
#define PUFO_STRIP_CELL_LAST_X    234 /* 212 + 4 * 22, where the chevron meets */
#define PUFO_STRIP_ROW_Y_FIRST      4 /* 0x625D8: y = 28 * base + 4 + 4 * row */
#define PUFO_STRIP_CELL_COUNT       6 /* six columns and six rows per base */
#define PUFO_STRIP_HILITE_STEPS    23 /* 0x62798: the chevron's 23 two-pixel steps */

typedef struct BaseStripDrawEnv {
    const u8 *abFacilityGrid;  /* the grid both byte arrays are read from */
    const u8 *pbBaseCount;     /* 0x159D80 = abFacilityGrid + 0x618, read signed */
    const u8 *pbSelectedBase;  /* 0x159D81 = abFacilityGrid + 0x619, read signed */
    i32       nHighlightRow;   /* the second argument, sign-extended; -1 matches no row */
} BaseStripDrawEnv;

void Base_DrawFacilityIconStrip(const BaseStripDrawEnv *pEnv);

/* ------------------------------------------------- the radar screen's map backdrop
 *
 * Evidence: RadarScreen_BlitMapBackdrop @ 0x5eb10 (508 B), installed as a callback at
 * +0x0C of the radar screen's four corner controls (0x5FCEC, 0x5FCF4) by RadarScreen_Init.
 *
 * It is a phase toggle first and a draw second: when bRadarBlinkPhase @ 0x159730 is zero it
 * sets it and returns without drawing anything; when it is set it clears it and draws. Radar
 * screen frames therefore show the backdrop on alternate calls. RadarScreen_Init clears the
 * flag (0x5F4CC), and RadarScreen_Update reads it to pick 50 against 0 (0x60584) and to gate
 * the flashing message colour block (0x61144).
 *
 * The two half-extents are the map's half size minus the screen's half size:
 *
 *     nHalfW = gSmapCam.nMapW / 2 - 120      (0x5EB44..0x5EB54, held in R7)
 *     nHalfH = gSmapCam.nMapH / 2 - 160      (0x5EB58..0x5EB68, held in R6)
 *
 * The ARM's `MOVS` / `ADDMI R3,R3,#1` / `ASR #1` triple is C's division by two, not an
 * arithmetic shift: for a negative nMapW it adds one first, so -5 becomes -2.
 *
 * Two PadImage_Blit calls of the current map pad, at x = -(nScrollX + nHalfW) and at
 * x = -(nScrollX + nMapW + nHalfH's pair unchanged), both with y = -(nScrollY + nHalfH). The
 * second call rewrites nOriginY with the identical value (0x5EBBC..0x5EBC4) — reproduced, not
 * corrected.
 *
 * Then three masked blits of the whole world bitmap through the layer pad, at x offsets
 * -nHalfW, -nMapW-nHalfW and +nMapW-nHalfW of (nSmapOverlayOriginX - nScrollX), all at the
 * layer pad's y, which is copied from the map pad's nOriginY rather than recomputed. Each is
 * skipped when nGeoscapeScreen @ 0x186BC4 is 2 (Cydonia). The blit is SoftSurf_BlitFlip, whose
 * third argument is the glyph record itself and whose fourth is the palette base:
 *
 *     pSrc  = pSmapWorldBits @ 0x186BC0, a {u16 w, u16 h, u8 indexed[w*h]} record that
 *             Geoscape_InitUI (0x85A18) allocates as 800*400+4 bytes and the zoom steps only
 *             resize (600x300, 472x236) through the same pointer
 *     pPal  = aPalette slot 29 (0x14E6D0, reached only as aPalette + 0x7400 at 0x5EC28)
 *     pMask = pSmapLayerPad->pData, the layer pad's own byte plane used as per-pixel alpha
 *
 * The three x offsets are one map width apart, which is how the original wraps the world
 * horizontally: the same bitmap is painted left of, on, and right of the viewport. */
#define PUFO_RADAR_MAP_PAL_SLOT   29 /* aPalette + 29 * 1024 = 0x14E6D0 */
#define PUFO_RADAR_HALF_SCREEN_X 120 /* 0x5EB54: SUB R7, R3, #0x78 */
#define PUFO_RADAR_HALF_SCREEN_Y 160 /* 0x5EB68: SUB R6, R3, #0xA0 */
#define PUFO_RADAR_SCREEN_NO_WRAP  2 /* 0x5EC14: CMP R0, #2 skips all three layer blits */

typedef struct RadarBackdropEnv {
    SmapView *pView;          /* gSmapCam @ 0x1863E8 */
    PadImage *pMapPad;        /* pSmapPadCur @ 0x186E40 */
    PadImage *pLayerPad;      /* pSmapLayerPad @ 0x186BE0 */
    const u8 *pWorldBits;     /* pSmapWorldBits @ 0x186BC0, the glyph record */
    i32       nOverlayOriginX;/* nSmapOverlayOriginX @ 0x186C20, written by Geoscape_Draw */
    i32       nScreenId;      /* nGeoscapeScreen @ 0x186BC4 */
    u8       *pbBlinkPhase;   /* bRadarBlinkPhase @ 0x159730 */
} RadarBackdropEnv;

/* Returns true when the backdrop was drawn, false on the phase-setting call that draws
 * nothing. The original's own return value is 1 on that first call and otherwise whatever the
 * last blit or the screen id left in R0; no caller uses it — the function is reached through
 * the +0x0C control slot. */
bool RadarScreen_BlitMapBackdrop(const RadarBackdropEnv *pEnv);

/* Smap_DrawStrategIcons @ 0x822c4 (1,060 B). One glyph per strateg entry, at the entry's map
 * coordinates converted into screen coordinates.
 *
 * The half-extents are the same halves the map click and the radar backdrop use: 0x822EC..0x82304 is
 * MOVS / ADDMI #1 / ASR #1, which is C's `n / 2` (ADDMI only fixes a NEGATIVE value's rounding), so
 * nHalfW = nMapW / 2 - 120 and nHalfH = nMapH / 2 - 160.
 * Map_nGlyphPalBase is forced to 32 for the whole pass (0x82318 MOV R0, #0x20 / 0x82328 STR), so
 * every glyph here takes palette slot 32 whatever the rest of the frame used. nPal is always 0
 * (R9 = 0 at 0x822D8).
 *
 * Zoom scales the stored map coordinates exactly as Smap_HitTestStrategSelect does: 1/1.333 at mode
 * 1 (0x823B4, the muld pair 1350030981:1072169353) and 1/1.694 at mode 2 (0x8243C,
 * 831611141:1071834084), each truncated by dtoi. Mode 0 and any other mode leave them alone.
 *
 * 0x824A4..0x824D0: x = nX - nScrollX - nHalfW and y = nY - nScrollY - nHalfH, then each is wrapped
 * by the map size with ONE conditional subtraction (CMP / SUBGT) — the same single-wrap shape as the
 * 0x84894 crosshair bug, so a value more than one map size out of range stays out of range.
 *
 * The glyph index is (nMapBitsBase + nAnimPhase + k) for types 0, 3, 4, 5 and for an ACTIVE type-1
 * entry whose nTimer is non-zero, but (nMapBitsBase + 2) and (nMapBitsBase + 4) WITHOUT the animation
 * phase for an inactive visible type-1 entry (0x825DC) and for an idling one (0x82620). k is 6 for an
 * active visible type 0, 8 for type 3, 10 for type 4, 14 for type 5, 12 for an active type 1 with a
 * timer, and 0 for an inactive type 0.
 *
 * Visibility gates differ by type: a type-1 entry draws only when bVisible is set; a type-0 entry
 * draws whenever it is inactive regardless of bVisible, but needs bVisible when active; types 3, 4 and
 * 5 ignore bActive and bVisible entirely. */
#define PUFO_GLYPH_PAL_STRATEG_ICONS 32 /* 0x82318 MOV R0, #0x20 -> Map_nGlyphPalBase */

typedef struct SmapIconDrawEnv {
    const PtrArray *pStrategList; /* 0x1864F8 + 0x3C */
    u16             nAnimPhase;   /* nUiSlot0AnimPhase @ 0x113A18, Map + 0x60, read with LDRH at 0x82634 */
    u16             nMapBitsBase; /* fontSys.nMapBitsBase @ 0x15829C */
} SmapIconDrawEnv;

void Smap_DrawStrategIcons(const SmapView *pView, const SmapIconDrawEnv *pEnv);

/* Geoscape_DrawClockPads @ 0x5c5c8 (2,168 B). The clock strip at the bottom of the geoscape:
 * HH:MM:SS as digit pads, then the weekday name, the day, the month name and the four-digit year.
 *
 * All seven values come from GameTime_Decode @ 0x5cfd4, which writes them into the block based at
 * 0x159608 (nYear +0x18, nMonth +0x1C, nDay +0x20, nWeekday +0x24, nHour +0x28, nMinute +0x2C,
 * nSecond +0x30). The two digit halves come apart with _rt_sdiv, divisor 10 in R0 again.
 *
 * Positions, all fixed: hour tens/ones at x 90/100, the two literal ':' glyphs (0xAB078) at 110 and
 * 135, minute tens/ones at 115/125, second tens/ones at 140/150, all at y 260. The weekday name is
 * centred on 124 at y 272. The day pair sits at y 287 x 90/100, TEXTS[1251] at 110, the month name at
 * 130. The year's four digits are at y 304, x 105/115/125/135.
 *
 * The day's tens digit is blitted ONLY when it is non-zero (0x5CA00 CMP R8,#0 / 0x5CA14 LDRNE /
 * 0x5CA18 BLNE) — that is how the original suppresses a leading zero — and 0x5CA0C MOVNE R1,#0 forces
 * its bFlag to 0 before the only call, so the call is always PadImage_BlitEx(pad, 0). The ones digit
 * at x 100 is always blitted.
 *
 * nMonth outside 1..12 is deterministic: psz still holds the TEXTS[1251] separator copied at 0x5CA48,
 * and 0x5CCFC draws that, which the port reproduces. nWeekday outside 1..7 leaves psz at whatever the
 * stack held (the original never writes it on that path), so the port draws an empty string there
 * instead — the one deviation. */
#define PUFO_TEXT_WEEKDAY_FIRST 1244 /* TEXTS[1244..1250], the seven weekday names */
#define PUFO_TEXT_CLOCK_SEP     1251 /* TEXTS[1251], between the day and the month */
#define PUFO_TEXT_MONTH_FIRST   1252 /* TEXTS[1252..1263], the twelve month names */

typedef struct GeoscapeClockDrawEnv {
    PadImage *const *apDigitPads; /* apClockDigitPads @ 0x186BF0, the ten pads "b0".."b9" */
    const char       *pTextRecs;  /* aTextRecs @ 0xB38D0 */
    const GameTime   *pTime;
    GfxColorEnv      *pColor;     /* the pair the strip packs is 68,244,252 / 4,70,75 */
} GeoscapeClockDrawEnv;

void Geoscape_DrawClockPads(const GeoscapeClockDrawEnv *pEnv);

/* ------------------------------------------------- the radar fuel gauge and range bar
 *
 * Evidence: RadarScreen_DrawFuelGauge @ 0x5d918 (524 B), RadarScreen_DrawRangeBar @ 0x5d6e4 (564 B).
 *
 * The gauge reads the CONSUMED fuel percentage, not the remaining one:
 *
 *     n = (int)(100.0 - 100.0 * (double)Ship.nFuel / (double)Ship.uFuelMax)
 *
 * (0x5D950..0x5D9D8: two _itod, _divd, _muld by 100.0, _subd from 100.0, _dtoi). When n reaches 100
 * it clamps there and sets bRadarCraftOutOfFuel @ 0x1596DC — the only thing the original keeps of the
 * number, and RadarScreen_Update reads that flag nine times.
 *
 * Three _rt_sdiv calls split it into digits (the divisor travels in R0, the dividend in R1, the
 * quotient comes back in R0 and the remainder in R1): hundreds = n / 100, tens = (n - 100*hundreds)
 * / 10, units = n % 10. Each digit picks a pad out of aRadarDigitPads @ 0x1596A0 — eleven PadImage
 * pointers RadarScreen_Init allocates and loads as i0..i9 through palette slot 0x19, the eleventh at
 * 0x1596C8 — and moves it to a fixed origin, then blits it keyed with 1:
 *
 *     hundreds -> (162, 60), blitted only when hundreds != 0
 *     tens     -> (172, 60), blitted when tens != 0 OR hundreds == 1
 *     units    -> (182, 60), always blitted
 *     slot 10  -> (192, 60), always blitted
 *
 * The tens rule is the ARM's branch shape, not a rounding trick: 0x5DA9C skips the blit when the tens
 * digit is non-zero and 0x5DAA4 skips it when the hundreds digit is not 1, so the only case that
 * blits a zero tens digit is 100%. A leading zero is dropped, except when there is a hundreds digit
 * to justify it. Every origin is written whether or not the pad is blitted.
 *
 * The range bar has three gates and then a random fuel penalty. Gate 1: the intercept item
 * (strategList[nInterceptStrategIdx]) must have a non-zero bCraftClass. Gate 2:
 * nRadarRangeCur + 560*(nRadarCraftLevel - 13)/114 must be <= nRadarRangeLimit — nRadarCraftLevel is
 * the per-class level RadarScreen_Init stores (8, 6, 4, 2, 0 at 0x5F6D8, 0x5F7A8, 0x5F878, 0x5F944,
 * 0x5F9E8), so the term is always negative and the gate loosens with class. Gate 3:
 * nRadarPulseTimer must be 255. Two rects then share a y and a height derived from the range:
 * y = 183 - 114*nRadarRangeCur/560 and height = that quotient - 15, one 3 wide at x 67 (index 0x28,
 * mid 0x64) and one 1 wide at x 68 (index 0x50, mid 0xDC). Then Game_RandU16(100) < 70 — a 70% roll —
 * picks a fuel penalty by bCraftClass and adds it to apCraft[nInterceptShipIdx]->nFuel as i16:
 *
 *     class 1 or 2: rand(8)  - 20      class 3 or 4: rand(10) - 40     class 5: rand(30) - 120
 *     class 6:      rand(20) - 80      class 7:      rand(30) - 140    anything else: no penalty
 *
 * and the pulse timer is set to 230 on every path that got past gate 3. */
#define PUFO_RADAR_DIGIT_PADS        11 /* aRadarDigitPads @ 0x1596A0, i0..i9 plus 0x1596C8 */
#define PUFO_RADAR_PAD_KEY            1 /* the key every PadImage_BlitEx here passes */
#define PUFO_RADAR_FUEL_Y            60 /* 0x5DA64: MOV R6, #0x3C, shared by all four pads */
#define PUFO_RADAR_FUEL_X_HUNDREDS  162 /* 0x5DA5C: MOV R2, #0xA2 */
#define PUFO_RADAR_FUEL_X_TENS      172 /* 0x5DA88: MOV R2, #0xAC */
#define PUFO_RADAR_FUEL_X_UNITS     182 /* 0x5DABC: MOV R2, #0xB6 */
#define PUFO_RADAR_FUEL_X_PERCENT   192 /* 0x5DADC: MOV R3, #0xC0 */
#define PUFO_RADAR_RANGE_BAR_X     67   /* 0x5D794: MOV R1, #0x43; the 1-wide bar is at 68 */
#define PUFO_RADAR_RANGE_BAR_Y    183   /* 0x5D7A8: RSB R2, R0, #0xB7 */
#define PUFO_RADAR_RANGE_SCALE   560    /* the _rt_sdiv divisor of both range divisions */
#define PUFO_RADAR_RANGE_PITCH   114    /* the multiplier, and the gate's divisor */
#define PUFO_RADAR_RANGE_TRIM     15    /* 0x5D7A0: SUB R3, R0, #0xF */
#define PUFO_RADAR_LEVEL_BIAS     13    /* 0x5F710-ish: SUB R3, R0, #0xD */
#define PUFO_RADAR_PULSE_ARMED    255   /* 0x5D75C: CMP R0, #0xFF */
#define PUFO_RADAR_PULSE_NEXT     230   /* 0x5D90C: MOV R0, #0xE6 */
#define PUFO_RADAR_FIRE_CHANCE     70   /* 0x5D814: CMP R3, #0x46, taken when the roll is below */

typedef struct RadarFuelEnv {
    const Ship *pCraft;                        /* apCraft[nInterceptShipIdx], read only */
    PadImage *apDigitPads[PUFO_RADAR_DIGIT_PADS]; /* aRadarDigitPads, origins rewritten */
    u8 *pbOutOfFuel;                           /* bRadarCraftOutOfFuel @ 0x1596DC */
} RadarFuelEnv;

/* Returns the consumed percentage it drew (0..100). The original returns the last
 * PadImage_BlitEx result, which no caller uses. */
i32 RadarScreen_DrawFuelGauge(const RadarFuelEnv *pEnv);

typedef struct RadarRangeBarEnv {
    const StrategItem *pInterceptItem;  /* strategList[nInterceptStrategIdx] */
    Ship              *pCraft;          /* apCraft[nInterceptShipIdx], its nFuel is written */
    const i32         *pnRangeCur;      /* nRadarRangeCur @ 0x15966C, animated by CyclePalette */
    i32                nRangeLimit;     /* nRadarRangeLimit @ 0x159668 */
    i32                nCraftLevel;     /* nRadarCraftLevel @ 0x1596E0 */
    i32               *pnPulseTimer;    /* nRadarPulseTimer @ 0xAC224 */
} RadarRangeBarEnv;

/* Returns true when the bar was drawn, i.e. when all three gates passed. */
bool RadarScreen_DrawRangeBar(const RadarRangeBarEnv *pEnv);

/* ------------------------------------------------- opening and closing the radar screen
 *
 * Evidence: RadarScreen_Init @ 0x5f490 (3600 B), RadarScreen_SetActive @ 0x6144c (76 B).
 *
 * RadarScreen_Init is ONE function doing both directions, toggled on bRadarScreenActive @ 0x159678
 * (LDRB at 0x5F494): the flag zero path opens the screen, the flag-one path closes it. That is why
 * RadarScreen_SetActive calls it on both of its own branches -- Init decides which half runs by
 * reading the flag the caller just decided. SetActive then writes bActive into GameTime.bPaused
 * (STRB at 0x159618+4, 0x61464 / 0x61480) and into bUiFrameStyle (0x186566, the panel root's
 * frame-style byte at root+0x6E, 0x6148C): opening the radar screen pauses the game clock and picks
 * the bright panel frame. Its first argument is never read and its result (&pObj, 0x1864F8) is
 * never used, so the port returns void.
 *
 * The open path, in order:
 *   1. Sound_Play(7) (0x5F49C; the MOV R1, #4 before it is dead -- Sound_Play @ 0x9E0C0 reads only
 *      R0), set bRadarScreenActive, and clear bRadarCraftOutOfFuel, bRadarBlinkPhase, bRadarFlip,
 *      bRadarTraceLost and bRadarInterceptDone. All five are STRB, so all five are bytes.
 *   2. Five single pads, each operator new(0x14) + PadImage_ctor, storing NULL when the allocation
 *      fails: pRadarFramePad (0x1596FC), pRadarUfoPad (0x1596D0), pRadarUfoMissilePad (0x159670),
 *      pRadarUfoFlashPad (0x1596F0), pRadarOverlayPad (0x1596E4). Only the frame pad is loaded here
 *      ("radar", mode 22, origin (26, 56)); the other four wait for the class switch.
 *   3. nRadarMsgTimer = 255, nInterceptTargetHp = 0.
 *   4. The class switch on (i8)strategList[nInterceptStrategIdx]->bCraftClass (LDRSB at +2, 0x5F668).
 *      Eight cases, and they are one flat table: cases 1, 3 and 5 set the HP/palette/overlay/limit
 *      half and then fall through into cases 2, 4 and 6 for the art and the level half. The
 *      "if (nInterceptTargetHp == 0)" guard inside cases 2, 4 and 6 is always true, because step 3
 *      just zeroed that field -- so the fall-through never changes anything a preceding case set.
 *      The port reads it as the table g_aRadarUfoClass below.
 *   5. pRadarOverlayPad->nOriginX = 115 - (uWidth >> 1) (LDRH + LSR #1 at 0x5FA48), nOriginY = 180.
 *   6. The HP re-sync (0x5FA74): if StrategItem.nHp (+0x6C) is -1 the item has not fought yet, so it
 *      takes the table HP; otherwise the table HP is replaced by the item's, in both
 *      nInterceptTargetHp and nInterceptTargetHpMax.
 *   7. The ammo draw (0x5FAAC..0x5FBFC). nRadarAmmoL and nRadarAmmoR start at -2 ("no weapon on that
 *      mount"), then Ship.bWeaponL / bWeaponR (both LDRSB at +0x22 / +0x23) pick the source byte:
 *      25, 26, 27, 29, 30 read Ship.bAmmo25..bAmmo30 (+0x3D..0x41), 28 gives -1 ("unlimited"),
 *      anything else leaves -2. Then, when both mounts hold the SAME weapon kind and the pool is
 *      neither -1 nor -2, the single pool is split: nRadarAmmoR = ammo / 2 and nRadarAmmoL =
 *      ammo / 2 + (ammo % 2). The divide is _rt_sdiv(2, ammo) and the test is on the remainder in
 *      R1 (0x5FBFC..0x5FC04), and the ARM's MOVS/ADDMI/ASR pair at 0x5FBE4 is the same truncating
 *      divide, so both halves agree.
 *   8. nRadarRangeTarget = nRadarRangeCur = 560, pRadarUfoPad->nOriginX = 55.
 *   9. The palette copy at 0x5FC34, then Palette_Load("base.pal", 3). See the overrun note on
 *      PUFO_RADAR_PAL_COPY_COUNT and the bugs.tsv row for 0x5FC34.
 *  10. Five UiControl new(0x20) + UiControl_ctor into aRadarControls (0x159684), then their click
 *      handlers written at +0xC in the order NW, NE, SW, SE, BlitMapBackdrop (0x5FCB8..0x5FCF4), then
 *      five button pads new(0x14) + PadImage_ctor into aRadarButtonPads (0x159700), then
 *      UiControl_Setup(aRadarControls[i], 0, 0, aRadarCtlDesc[i]) for i = 0..4 with the descriptor
 *      table at 0xAB2A0 (stride 0x20, the IDB's UiControlDesc), then the five button pads loaded as
 *      "i_but3", "i_but4", "i_but2", "i_but5", "i_but6" in mode 26 at origins (117,86), (161,86),
 *      (117,107), (161,107), (117,128) -- the same five origins the descriptors carry.
 *  11. nRadarCursorX = 117, nRadarCursorY = 86 (the NW corner), then eleven digit pads new+ctor+load
 *      as "i0".."i9" and "perc" in mode 25, then bUiAnimStarted = 0.
 *
 * The close path, in order:
 *   1. The ammo write-back (0x5FF88..0x60144). If bWeaponL == bWeaponR, then for kind 25, 26, 27, 29
 *      or 30 the craft's bAmmo<kind> byte becomes (u8)(nRadarAmmoL + nRadarAmmoR) -- the two mounts
 *      re-arm from one pool, which is why the split in step 7 sums back to the original count. Kind
 *      28 and any other kind write nothing. If the mounts differ, each side is written separately
 *      from its own counter, with the same kind filter. The store is STRB, so the sum truncates.
 *   2. bRadarScreenActive = 0.
 *   3. Free, in this order: the eleven digit pads, the five controls (UiControl_FreePads + delete),
 *      the five button pads, then pRadarFramePad, pRadarUfoPad, pRadarUfoFlashPad,
 *      pRadarUfoMissilePad, pRadarOverlayPad. Every one is null-checked.
 *   4. Palette_Load("menu.pal", 1). Note this restores only slot 1 -- slot 2 is still holding what
 *      step 9 wrote over it.
 *
 * Deviation: the original frees through operator new/delete; the port uses malloc/free. The original
 * leaves the freed pointers stored in the globals (they are only read again after the next open,
 * which reassigns all of them), and the port NULLs them. */
#define PUFO_RADAR_BUTTON_PADS      5 /* aRadarButtonPads @ 0x159700 */
#define PUFO_RADAR_CONTROLS         5 /* aRadarControls @ 0x159684 */
#define PUFO_RADAR_FRAME_MODE      22 /* the "radar" pad's palette slot */
#define PUFO_RADAR_UFO_MODE        23 /* ufo_<n> */
#define PUFO_RADAR_FLASH_MODE      27 /* ufo_<n>f and ufo_<n>m, and the ufoflash.pal slot */
#define PUFO_RADAR_OVERLAY_MODE    17 /* the 85..92 overlay pads, and the slot their .pal loads into */
#define PUFO_RADAR_BUTTON_MODE     26 /* i_but2..i_but6, and the all_btn.pal slot */
#define PUFO_RADAR_DIGIT_MODE      25 /* i0..i9 and perc, and the cic.pal slot */
#define PUFO_RADAR_FRAME_X         26 /* 0x5F54C: MOV R3, #0x1A */
#define PUFO_RADAR_FRAME_Y         56 /* 0x5F554: MOV R3, #0x38 */
#define PUFO_RADAR_UFO_X           55 /* 0x5FC28: MOV R3, #0x37 */
#define PUFO_RADAR_OVERLAY_X_CENTRE 115 /* 0x5FA50: RSB R3, R3, #0x73 -- 115 - (uWidth >> 1) */
#define PUFO_RADAR_OVERLAY_Y       180 /* 0x5FA5C: MOV R3, #0xB4 */
#define PUFO_RADAR_MSG_TIMER_INIT  255 /* 0x5F60C: MOV R1, #0xFF */
#define PUFO_RADAR_AMMO_NONE        (-2) /* no weapon on that mount */
#define PUFO_RADAR_AMMO_UNLIMITED   (-1) /* mount kind 28 */
#define PUFO_STRATEG_HP_FRESH       (-1) /* StrategItem.nHp meaning "this contact has not fought yet" */
#define PUFO_RADAR_PAL_COPY_COUNT  766 /* 0x5FC2C: MOV R1, #0x2FC ; ORR R1, R1, #3 -> 767, decremented first */
#define PUFO_RADAR_PAL_SRC_SLOT     26 /* 0x5FC38: ADD R3, R0, #0x6800 = aPalette + 26 * 1024 */
#define PUFO_RADAR_PAL_DST_SLOT      1 /* 0x5FC44: STR R3, [R0, #0x400] = aPalette + 1 * 1024 */

/* ORIGINAL BUG (0x5FC34), reproduced: the copy runs 766 dwords, but a palette slot holds 256. The
 * stores cover slot 1 (all 256), slot 2 (all 256) and slot 3 (254 of 256), and the reads run from
 * slot 26 through slot 27 and 254 entries of slot 28. Palette_Load("base.pal", 3) at 0x5FC58 repairs
 * slot 3, and the close path reloads slot 1 with menu.pal, but nothing ever reloads slot 2: after
 * the first radar screen it stays holding ufoflash.pal. The port copies the original's count. */

/* One row per StrategItem.bCraftClass, read out of the switch's literal pool (0x5F698..0x5F9F8) and
 * out of RadarScreen_RollDetection's speed table (0x602E0..0x60404). The doubles are the original's
 * own literals: 36.6666, 40.0, 45.0, 66.6666, 71.0, 80.0, 53.6666, 83.3333. */
typedef struct RadarUfoClass {
    const char *pszArt;     /* ufo_<n> into pRadarUfoPad, mode 23 */
    const char *pszFlash;   /* ufo_<n>f into pRadarUfoFlashPad, mode 27 */
    const char *pszMissile; /* ufo_<n>m into pRadarUfoMissilePad, mode 27 */
    i8          nLevel;     /* nRadarCraftLevel @ 0x1596E0 */
    i32         nHp;        /* nInterceptTargetHp @ 0x159698 and nInterceptTargetHpMax @ 0x159658 */
    const char *pszPalette; /* loaded into palette slot 17 */
    const char *pszOverlay; /* pRadarOverlayPad, mode 17 */
    i32         nRangeLimit;/* nRadarRangeLimit @ 0x159668 */
    double      dSpeed;     /* the speed RadarScreen_RollDetection writes into StrategItem.dSpeed */
} RadarUfoClass;

#define PUFO_RADAR_CLASS_COUNT 8

extern const RadarUfoClass g_aRadarUfoClass[PUFO_RADAR_CLASS_COUNT];

/* The objects RadarScreen_Init owns between an open and a close. The original keeps them in eleven
 * separate globals; the port groups them, and Init fills and empties the whole struct. */
typedef struct RadarUiState {
    PadImage  *pFramePad;
    PadImage  *pUfoPad;
    PadImage  *pUfoFlashPad;
    PadImage  *pUfoMissilePad;
    PadImage  *pOverlayPad;
    PadImage  *apButtonPad[PUFO_RADAR_BUTTON_PADS];
    PadImage  *apDigitPad[PUFO_RADAR_DIGIT_PADS];
    UiControl *apCtl[PUFO_RADAR_CONTROLS];
} RadarUiState;

typedef struct RadarSessionEnv {
    RadarUiState *pUi;
    u8   *pbActive;        /* bRadarScreenActive @ 0x159678, the open/closed flag Init toggles on */
    u8   *pbOutOfFuel;     /* bRadarCraftOutOfFuel @ 0x1596DC */
    u8   *pbBlinkPhase;    /* bRadarBlinkPhase @ 0x159730 */
    u8   *pbFlip;          /* bRadarFlip @ 0x159718 */
    u8   *pbTraceLost;     /* bRadarTraceLost @ 0x159714 */
    u8   *pbInterceptDone; /* bRadarInterceptDone @ 0x159650: cleared on open, set by
                            * RadarScreen_Update when the intercept ends (target dead, contact
                            * landed or contact crashed). Nothing else writes it. */
    i32  *pnScanOffset;    /* nRadarScanOffset @ 0x159724, the signal-bar scroll RadarScreen_Update
                            * advances by 10 and wraps above 39. RadarScreen_Init does NOT clear it. */
    u8   *pbAnimStarted;   /* bUiAnimStarted @ 0x187080 */
    i32  *pnMsgTimer;      /* nRadarMsgTimer @ 0xAC220 */
    i32  *pnTargetHp;      /* nInterceptTargetHp @ 0x159698 */
    i32  *pnTargetHpMax;   /* nInterceptTargetHpMax @ 0x159658 */
    i32  *pnCraftLevel;    /* nRadarCraftLevel @ 0x1596E0 */
    i32  *pnRangeLimit;    /* nRadarRangeLimit @ 0x159668 */
    i32  *pnRangeTarget;   /* nRadarRangeTarget @ 0x15965C */
    i32  *pnRangeCur;      /* nRadarRangeCur @ 0x15966C */
    i32  *pnAmmoL;         /* nRadarAmmoL @ 0x159654 */
    i32  *pnAmmoR;         /* nRadarAmmoR @ 0x159664 */
    i32  *pnCursorX;       /* nRadarCursorX @ 0x1596D4 */
    i32  *pnCursorY;       /* nRadarCursorY @ 0x1596D8 */
    const i32 *pnShipIdx;    /* nInterceptShipIdx @ 0x15967C */
    const i32 *pnStrategIdx; /* nInterceptStrategIdx @ 0x159680 */
    Ship *const *apCraft;    /* Map.apCraft @ 0x114A64 */
    i32 nCraftCount;
    PtrArray *pStrategList;  /* strategList @ 0x186534 */
    GameTime *pTime;         /* GameTime @ 0x159618; SetActive writes bPaused */
    i8   *pnFrameStyle;      /* bUiFrameStyle @ 0x186566, the panel root's frame-style byte */
    /* The five click handlers Init installs at UiControl +0xC, in the order the ARM writes them.
     * They are injected because the ported handlers take envs, not the original's globals. */
    void (*apfnClick[PUFO_RADAR_CONTROLS])(void);
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pLoadPadsUser;
} RadarSessionEnv;

/* Opens the radar screen when pUi's flag is 0 and closes it when it is 1. */
void RadarScreen_Init(const RadarSessionEnv *pEnv);

/* Init, then bPaused and the panel frame style both take bActive. */
void RadarScreen_SetActive(const RadarSessionEnv *pEnv, u8 bActive);

/* --- RadarScreen_RollDetection @ 0x602a0 (720 B) : the "trace lost" roll ---
 *
 * Game_RandU16(nChance) gates it: any non-zero roll skips the whole speed comparison and goes
 * straight to the flag check, so the event happens once in nChance.
 *
 * On a zero roll it first rewrites the tracked item: strategList[nInterceptStrategIdx]->dSpeed
 * becomes g_aRadarUfoClass[bCraftClass].dSpeed (class 0..7; any other class writes nothing), and
 * dTrackA becomes dSpeed * 0.02.
 *
 * Then it compares the craft's own speed against that: (double)apCraft[nInterceptShipIdx]->uFuelCap
 * * (1/60) < dSpeed. uFuelCap / 60 is the same speed basis StrategList_ProcessEvents uses for an
 * inactive craft (pufo_strateg.c:504) and Ship_BurnFuel uses for its per-tick burn, so the test is
 * literally "the tracked object outruns our intercept craft". When it does NOT outrun us, nothing is
 * set and the result is whatever bRadarTraceLost already held. When it does: bRadarFlip = 1,
 * bRadarTraceLost = 1, nRadarRangeTarget = 560 (the full-scale target, the same constant
 * PUFO_RADAR_RANGE_SCALE), and the result is 1.
 *
 * Finally, if the result byte is non-zero -- either set just now or already set by an earlier roll --
 * nRadarMsgTimer = 0 and TEXTS[1599] is copied into szRadarMsg. lang.dat decodes that record as
 * "След МАК потерян" in cp1251 (D1 EB E5 E4 20 CD CB CE 20 EF EE F2 E5 F0 FF ED -- the middle word is Cyrillic МАК, which reads like "MAX"). The copy is the original's unbounded strcpy-until-NUL
 * loop at 0x6054C; the port bounds it to the destination (deviation).
 *
 * bRadarTraceLost is also the gate the four corner buttons open on: once it is set they do nothing,
 * which is why losing the trace stops the cursor from moving. Nothing in the binary clears it except
 * the open path of RadarScreen_Init. */
#define PUFO_RADAR_TEXT_TRACE_LOST 1599 /* TEXTS record copied into szRadarMsg */
#define PUFO_RADAR_SPEED_TRACK_A   0.02 /* dTrackA = dSpeed * this, 0x60444 */
#define PUFO_RADAR_SPEED_SCALE     (1.0 / 60.0) /* uFuelCap / 60 is the craft's speed, 0x604A0 */

typedef struct RadarDetectEnv {
    PtrArray    *pStrategList;  /* strategList @ 0x186534 */
    const i32   *pnStrategIdx;  /* nInterceptStrategIdx @ 0x159680 */
    Ship *const *apCraft;       /* Map.apCraft @ 0x114A64 */
    i32          nCraftCount;
    const i32   *pnShipIdx;     /* nInterceptShipIdx @ 0x15967C */
    u8          *pbTraceLost;   /* bRadarTraceLost @ 0x159714 */
    u8          *pbFlip;        /* bRadarFlip @ 0x159718 */
    i32         *pnRangeTarget; /* nRadarRangeTarget @ 0x15965C */
    i32         *pnMsgTimer;    /* nRadarMsgTimer @ 0xAC220 */
    const u8    *pTextRecs;     /* aTextRecs @ 0xB38D0, stride PUFO_TEXT_REC_STRIDE */
    char        *pszMsg;        /* szRadarMsg @ 0x186B10 */
    size_t       cchMsg;
} RadarDetectEnv;

/* Returns the bRadarTraceLost value the roll ended with. RadarScreen_Update @ 0x60A3C is the only
 * caller and it discards the value. */
u8 RadarScreen_RollDetection(const RadarDetectEnv *pEnv, i32 nChance);

/* ------------------------------------------------- the two weapon mounts on the radar screen
 *
 * Evidence: RadarScreen_DrawCraftArmament @ 0x5edf4 (1692 B). RadarScreen_Update @ 0x60870 is its
 * only caller and it discards the returned bound, so the port returns void.
 *
 * It draws one half per mount, left first, and each half is the same five jobs: the weapon icon, the
 * range bar, the ammo count, the mount's own range ticks, and the state globals the shot animation
 * (RadarScreen_DrawOverlay, still unported) reads.
 *
 * 1. Map_nGlyphPalBase = 3 (0x5EE1C) for the whole call.
 * 2. The icon. (i8)Ship.bWeaponL picks a row of g_aRadarMountDraw and Font_DrawGlyph draws
 *    fontSys_nBaseBitsSprBase + nGlyphOff at (30, 136) in palette 0; the right half is the same row
 *    at (91, 136). The row's nRange -- 140, 260, 360, 180, 300, 420 -- is the weapon's own range and
 *    lands in nRadarBoundL / nRadarBoundR. Any id outside 25..30, WM_PSI_AMP included, draws no icon
 *    and stores 0.
 * 3. The engage gate. nRadarRangeCur + _rt_sdiv(114, 560 * (nRadarCraftLevel - 26)) is how far the
 *    radar currently reaches -- the same 560/114 pair RadarScreen_DrawRangeBar and the NE/SW cursor
 *    setters use. When that reach is GREATER than the mount's range the target is out of gunshot:
 *    bRadarEngageL = 0, and that mount's cooldown is forced to 255, already "ready" because there is
 *    nothing to shoot. Otherwise bRadarEngageL = 1 and the cooldown is left alone. The ARM writes both
 *    halves with predicated STRB/STR pairs (0x5EF88..0x5EFB0 left, 0x5F2A8..0x5F2C4 right), which is
 *    why Hex-Rays shows the left test twice.
 * 4. The bar. h = _rt_sdiv(560, 114 * nRange) is the range in pixels, and the bar is the 1-wide rect
 *    (37, 170 - h, 1, h - 34) -- it grows up from y 136, the row the icon sits on. When nRange > 170
 *    a 3x3 cap is added at (36, 169 - h). All of it in the bar colour 0x78/0xC8/0x28.
 * 5. The ammo count. nRadarAmmoL == -2 draws nothing (no weapon on that mount), -1 draws the single
 *    "-" from asc_AB340 (unlimited), anything else draws j_itoa. The string is centred on the bar:
 *    x = 37 - Text_MeasureWidth/2, y 153. The right half centres on 98.
 * 6. The ticks. When bWeaponL != 0, five 2x1 rects at x 54, 57, 60, 63, 66 (0x5F0EC: i = 54; i < 69;
 *    i += 3) sit on the bar's top edge at y 170 - h, with a 3x3 at (51, 169 - h). The right half uses
 *    x 69, 72, 75, 78, 81 (0x5F3F0: j = 69; j < 84; j += 3) and the 3x3 at (83, 169 - h).
 *
 * The halves are NOT mirror images in order: the left draws bar, cap, label, ticks; the right draws
 * label, bar, cap, ticks. The label and the bar overlap at the same x, so the port keeps each half's
 * own order.
 *
 * 7. The tail (0x5F45C..0x5F484). A mount holding the -2 sentinel inherits the OTHER mount's bound:
 *    nRadarAmmoL == -2 copies nRadarBoundR into nRadarBoundL, and nRadarAmmoR == -2 then copies
 *    nRadarBoundL into nRadarBoundR. With both empty the second copy moves the first copy's value, so
 *    both end at 0.
 *
 * Deviation: the original's j_itoa writes into a 52-byte stack buffer with no bound; the port uses
 * snprintf into the same size. */
#define PUFO_RADAR_MOUNT_PAL_BASE     3 /* 0x5EE1C: Map_nGlyphPalBase = 3 */
#define PUFO_RADAR_MOUNT_GLYPH_Y    136 /* 0x5EE84: MOV R2, #0x88, both halves */
#define PUFO_RADAR_MOUNT_GLYPH_X_L   30 /* 0x5EE7C: MOV R1, #0x1E */
#define PUFO_RADAR_MOUNT_GLYPH_X_R   91 /* 0x5F1A0: MOV R1, #0x5B */
#define PUFO_RADAR_BAR_BASE_Y       170 /* RSB R2, R6, #0xAA -- the bar's bottom edge */
#define PUFO_RADAR_BAR_CAP_Y        169 /* RSB R2, R6, #0xA9 -- the 3x3 caps sit one higher */
#define PUFO_RADAR_BAR_TRIM          34 /* SUB R3, R6, #0x22 -- the drawn height */
#define PUFO_RADAR_BAR_CAP_MIN_RANGE 170 /* 0x5F004: CMP R4, #0xAA -- a cap only for a longer range */
#define PUFO_RADAR_BAR_X_L           37 /* MOV R1, #0x25 */
#define PUFO_RADAR_BAR_X_R           98 /* MOV R1, #0x62 */
#define PUFO_RADAR_BAR_CAP_X_L       36 /* MOV R1, #0x24 */
#define PUFO_RADAR_BAR_CAP_X_R       97 /* MOV R1, #0x61 */
#define PUFO_RADAR_AMMO_LABEL_Y     153 /* MOV R2, #0x99 */
#define PUFO_RADAR_TICK_X_FIRST_L    54 /* 0x5F0EC: MOV R4, #0x36 */
#define PUFO_RADAR_TICK_X_FIRST_R    69 /* 0x5F3F0: MOV R4, #0x45 */
#define PUFO_RADAR_TICK_PITCH         3 /* ADD R4, R4, #3 */
#define PUFO_RADAR_TICK_LAST_X_L     69 /* 0x5F0E8: CMP R4, #0x45 */
#define PUFO_RADAR_TICK_LAST_X_R     84 /* 0x5F3EC: CMP R4, #0x54 */
#define PUFO_RADAR_TICK_CAP_X_L      51 /* MOV R1, #0x33 */
#define PUFO_RADAR_TICK_CAP_X_R      83 /* MOV R1, #0x53 */
#define PUFO_RADAR_BAR_COLOR_IDX   0x78 /* the three SoftSurf_BlitRectProxy colour arguments */
#define PUFO_RADAR_BAR_COLOR_MID   0xC8
#define PUFO_RADAR_BAR_COLOR_LOW   0x28
#define PUFO_RADAR_COOLDOWN_READY   255 /* 0x5EF90 / 0x5F2B0: MOVGT R1, #0xFF */

/* One row per mount id 25..30, read out of the two switches' literal pools (0x5EE7C..0x5EF3C left,
 * 0x5F1A4..0x5F25C right). The glyph offset is added to g_fontSys.nBaseBitsBase; the range is the
 * weapon's own reach, in the same units as nRadarRangeCur. */
typedef struct RadarMountDraw {
    i8   nMount;    /* the WeaponMount id the switch tests */
    u16  nGlyphOff; /* g_fontSys.nBaseBitsBase + this */
    i32  nRange;    /* nRadarBoundL / nRadarBoundR */
} RadarMountDraw;

#define PUFO_RADAR_MOUNT_COUNT 6 /* mount ids 25..30 */

extern const RadarMountDraw g_aRadarMountDraw[PUFO_RADAR_MOUNT_COUNT];

typedef struct RadarArmamentEnv {
    i32         *pnGlyphPalBase; /* Map_nGlyphPalBase @ 0x1183F4 */
    const i32   *pnShipIdx;      /* nInterceptShipIdx @ 0x15967C */
    Ship *const *apCraft;        /* Map.apCraft @ 0x114A64 */
    i32          nCraftCount;
    const i32   *pnRangeCur;     /* nRadarRangeCur @ 0x15966C */
    const i32   *pnCraftLevel;   /* nRadarCraftLevel @ 0x1596E0 */
    i32         *pnBoundL;       /* nRadarBoundL @ 0x1596E8, the left mount's weapon range */
    i32         *pnBoundR;       /* nRadarBoundR @ 0x1596EC */
    u8          *pbEngageL;      /* bRadarEngageL @ 0x159728, 1 while the target is in gunshot */
    u8          *pbEngageR;      /* bRadarEngageR @ 0x15972C */
    i32         *pnCooldownL;    /* nRadarCooldownL @ 0xAC21C, forced to 255 when out of range */
    i32         *pnCooldownR;    /* nRadarCooldownR @ 0xAC218 */
    i32         *pnShotYL;       /* nRadarShotYL @ 0x1596F8, zeroed when out of range */
    i32         *pnShotYR;       /* nRadarShotYR @ 0x1596F4 */
    const i32   *pnAmmoL;        /* nRadarAmmoL @ 0x159654 */
    const i32   *pnAmmoR;        /* nRadarAmmoR @ 0x159664 */
} RadarArmamentEnv;

void RadarScreen_DrawCraftArmament(const RadarArmamentEnv *pEnv);

/* ------------------------------------------------- the intercept shot animation
 *
 * Evidence: RadarScreen_DrawOverlay @ 0x5dc84 (2788 B). RadarScreen_Update @ 0x60898 is its only
 * caller.
 *
 * One half per mount, right first, and each half is: an idle check, two gates, one round consumed,
 * then either "the projectile is still in flight, advance and draw it" or "it landed, set the
 * cooldown and roll the hit".
 *
 * 1. Idle (0x5DCA8..0x5DCEC). A mount is idle when its ammo is exactly 0 AND its projectile offset
 *    is 0. An idle mount has its engage byte cleared. `bRadarLastShotHit` is cleared unconditionally
 *    at 0x5DCC0, before either half. Note the test is `== 0`, not `<= 0`: a mount holding the -2
 *    sentinel is not idle.
 * 2. Gates (0x5DCFC / 0x5DD0C for the right, 0x5E238 / 0x5E248 for the left). The half runs only when
 *    that mount's engage byte is non-zero AND its cooldown is exactly 255. The two halves are
 *    independent: the right half being skipped does not stop the left one.
 * 3. One round consumed (0x5DD10..0x5DD24 right, 0x5E24C..0x5E26C left), only when the projectile
 *    offset is 0, i.e. only when a shot starts. The -1 "unlimited" sentinel is exempt. The -2
 *    sentinel is NOT exempt and would go to -3 -- reproduced.
 * 4. In flight or landed, per mount id:
 *
 *      id  flight gate                     step            landed cooldown
 *      25  offset < level + barH - 33      +6, +8 flipped  255
 *      26  offset < level + barH - 33      +2, +4 flipped  240
 *      27  offset < level + barH - 33      +2, +4 flipped  235
 *      28  offset < 2                      +1              251
 *      29  offset < 2                      +1              249
 *      30  offset < level + barH - 33      +2, +4 flipped  230
 *
 *    with `barH = _rt_sdiv(560, 114 * nRadarRangeCur)` -- the same scale RadarScreen_DrawRangeBar and
 *    RadarScreen_DrawCraftArmament use, so the gate is "the projectile has not yet travelled past the
 *    target's distance". Landing writes the cooldown and stores RadarScreen_RollWeaponHit's result in
 *    bRadarLastShotHit.
 * 5. The laser and plasma (28, 29) have a third case nothing else has: when the offset is neither
 *    below 2 nor exactly 2 the half does **nothing at all** -- no cooldown, no roll. A beam that got
 *    past 2 without landing never resolves. (0x5DFEC `BNE loc_5E22C`, 0x5E518 `return`.)
 * 6. Landing zeroes the projectile offset and calls RadarScreen_RollWeaponHit with the mount id as the
 *    event. The ARM stores the offset first on the right (0x5E218) and the roll first on the left for
 *    ids 29/30 (0x5E498), which is not observable because RollWeaponHit reads neither offset.
 *
 * The two halves draw the same shapes at different columns, except the laser: the left beam is at x 64
 * and the right at x 72. */
#define PUFO_RADAR_SHOT_TOP_Y        165 /* 0x5E1C8: RSB R2, R0, #0xA5 -- the projectile's top edge */
#define PUFO_RADAR_SHOT_TOP_Y_ALT    164 /* 0x5E0A0: RSB R2, R0, #0xA4 -- the avalanche inner column */
#define PUFO_RADAR_SHOT_CROSS_Y      166 /* 0x5E388: RSB R2, R0, #0xA6 -- the fusion cross-bar */
#define PUFO_RADAR_BEAM_TOP_Y        194 /* 0x5DFCC: RSB R2, R0, #0xC2 -- the beam starts here */
#define PUFO_RADAR_SHOT_GATE_TRIM     33 /* SUB R3, R3, #0x21 -- how far past the target a shot may be */
#define PUFO_RADAR_BEAM_HEIGHT_TRIM   30 /* SUB R3, R3, #0x1E -- the beam's drawn height */
#define PUFO_RADAR_BEAM_FLIGHT         2 /* ids 28 and 29 count 0, 1, 2 and no further */
#define PUFO_RADAR_STEP_CANNON         6 /* 0x5E198: ADD R0, R8, #6 */
#define PUFO_RADAR_STEP_WIDE           2 /* 0x5E038 / 0x5DDA8 / 0x5E104 */
#define PUFO_RADAR_STEP_BEAM           1 /* 0x5DF84 / 0x5DEAC */
#define PUFO_RADAR_STEP_FLIP_EXTRA     2 /* 0x5E1BC: ADDNE R0, R0, #2, when bRadarFlip is set */
#define PUFO_RADAR_COOLDOWN_CANNON   255 /* 0x5E20C: MOV R0, #0xFF */
#define PUFO_RADAR_COOLDOWN_STINGRAY 240 /* 0x5E154: MOV R0, #0xF0 */
#define PUFO_RADAR_COOLDOWN_AVALANCHE 235 /* 0x5E0C0: MOV R0, #0xEB */
#define PUFO_RADAR_COOLDOWN_LASER    251 /* 0x5DFF0: MOV R0, #0xFB */
#define PUFO_RADAR_COOLDOWN_PLASMA   249 /* 0x5DF70: MOV R0, #0xF9 */
#define PUFO_RADAR_COOLDOWN_FUSION   230 /* 0x5DE90: MOV R0, #0xE6 */
#define PUFO_RADAR_SHOT_COLOR_IDX   0x50 /* the projectile body colour, 0x50/0xC8/0 = 0x5640 */
#define PUFO_RADAR_SHOT_COLOR_MID   0xC8
#define PUFO_RADAR_WIDE_COLOR_MID   0xA0 /* 0x50/0xA0/0 = 0x5500, the wide rockets */
#define PUFO_RADAR_PLASMA_COLOR_IDX 0x28 /* 0x28/0x64/0 = 0x2B20, the plasma outer column */
#define PUFO_RADAR_PLASMA_COLOR_MID 0x64
#define PUFO_RADAR_PLASMA_CORE_MID  0xDC /* 0x50/0xDC/0 = 0x56E0, the plasma inner column */

/* The columns each half draws at. The two halves agree everywhere except the beam: the left laser is
 * at x 64 (0x5DFDC uses 0x48 = 72 on the right, 0x5E4DC uses 0x40 = 64 on the left). */
typedef struct RadarShotSide {
    i32 xCannonA;  /* 67 both halves: the cannon's first 1x2 */
    i32 xCannonB;  /* 69 both halves: the cannon's second 1x2 */
    i32 xBody;     /* stingray / avalanche / plasma / fusion body: 64 left, 70 right */
    i32 xBodyAlt;  /* avalanche and plasma inner column: 65 left, 71 right */
    i32 xCross;    /* fusion cross-bar: 63 left, 69 right */
    i32 xBeam;     /* laser beam: 64 left, 72 right */
} RadarShotSide;

typedef struct RadarOverlayEnv {
    const i32   *pnShipIdx;      /* nInterceptShipIdx @ 0x15967C */
    Ship *const *apCraft;        /* Map.apCraft @ 0x114A64 */
    i32          nCraftCount;
    const i32   *pnRangeCur;     /* nRadarRangeCur @ 0x15966C */
    const i32   *pnCraftLevel;   /* nRadarCraftLevel @ 0x1596E0 */
    const u8    *pbFlip;         /* bRadarFlip @ 0x159718, the +2 extra step */
    u8          *pbEngageL;      /* bRadarEngageL @ 0x159728 */
    u8          *pbEngageR;      /* bRadarEngageR @ 0x15972C */
    i32         *pnCooldownL;    /* nRadarCooldownL @ 0xAC21C */
    i32         *pnCooldownR;    /* nRadarCooldownR @ 0xAC218 */
    i32         *pnShotYL;       /* nRadarShotYL @ 0x1596F8 */
    i32         *pnShotYR;       /* nRadarShotYR @ 0x1596F4 */
    i32         *pnAmmoL;        /* nRadarAmmoL @ 0x159654, decremented when a shot starts */
    i32         *pnAmmoR;        /* nRadarAmmoR @ 0x159664 */
    u8          *pbLastShotHit;  /* bRadarLastShotHit @ 0x159720 */
    /* RadarScreen_RollWeaponHit's own environment, passed through untouched. */
    const RadarHitRollEnv *pHitEnv;
} RadarOverlayEnv;

void RadarScreen_DrawOverlay(const RadarOverlayEnv *pEnv);

/* ------------------------------------------------- the radar screen's frame update
 *
 * Evidence: RadarScreen_Update @ 0x60570 (3804 B). It is the only caller of every other
 * RadarScreen_* function, the only writer of nRadarScanOffset @ 0x159724 and of
 * bRadarInterceptDone @ 0x159650, and one of only two writers of the replacement-spawn gate
 * at 0x162028 (the other is Tact_Shutdown).
 *
 * ## The panel, 0x60584..0x60788
 *
 * bRadarBlinkPhase picks 50 against 0 and the first Ui_AnimExpandBlit takes height
 * 140 + that, mode 4 (the centred growing square). Its return value gates the DRAW only:
 * when the animation is not finished the whole draw block is skipped and the three tail
 * gates still run -- 0x605F0 branches straight to 0x61234. The other four anim calls are
 * mode 0 at fixed rects, and five 2-pixel rails in index 0x3C / mid 0x96 / low 0xDC square
 * the panel off. Then RadarScreen_CyclePalette runs TWICE (the range therefore moves two
 * steps per frame) and the frame pad is blitted keyed 0xFF.
 *
 * ## The world half, 0x6078C..0x60864
 *
 * RadarScreen_DrawRangeBar runs only when the intercept is still live:
 * !bRadarInterceptDone && !bRadarCraftOutOfFuel && nInterceptTargetHp > 0.
 *
 * The ufo sprite's y is the same range quotient the range bar uses, from a different base:
 *     pRadarUfoPad->nOriginY = 170 - 114 * nRadarRangeCur / 560   (0x607C0..0x607F0)
 * against the range bar's 183 - the same quotient. The sprite blit is masked and is skipped
 * once the intercept is done:
 *     SoftSurf_BlitFlip(55, y, pRadarUfoPad->pData, aPalette slot 23,
 *                       pRadarUfoMissilePad->pData)
 * -- the missile pad's own byte plane is the per-pixel alpha, and slot 23 is the ufos.pal
 * slot RadarScreen_Init loads. Five UiControl_BlitActivePad calls walk aRadarControls[0..4].
 *
 * ## The weapons and the flash, 0x60868..0x60918
 *
 * RadarScreen_DrawCraftArmament runs unconditionally. RadarScreen_DrawOverlay runs only when
 * !bRadarInterceptDone && !bRadarCraftOutOfFuel, and the original RE-READS bRadarInterceptDone
 * afterwards (0x6089C) even though nothing DrawOverlay reaches can write it.
 *
 * The flash block then opens on !bRadarCraftOutOfFuel && !bRadarInterceptDone &&
 * bRadarLastShotHit. It copies the ufo pad's origin into pRadarUfoFlashPad and blits the
 * flash sprite through the same slot-23 palette and the same missile-pad mask. Crucially
 * nInterceptTargetHp is read AFTER DrawOverlay (0x60920): a shot that lands in this frame can
 * take the target to zero and drive the "destroyed" branch in the same frame it fired.
 *
 * ## The damage ladder, 0x60920..0x60C78
 *
 * nPct = 100 * nInterceptTargetHp / nInterceptTargetHpMax, both _rt_sdiv calls.
 *
 *   nInterceptTargetHp <= 0 && !bRadarInterceptDone -- the target is gone:
 *     nRadarMsgTimer = 0, TEXTS[1600] "МАК уничтожен" into szRadarMsg, 0x162028 = 1,
 *     StrategList_RemoveAt(nInterceptStrategIdx), Ship_ClearInterceptByStrategIdx of the same
 *     index, bRadarInterceptDone = 1.
 *   otherwise:
 *     40 <= nPct < 70 -- the contact may slip away. RadarScreen_RollDetection(2) first, then
 *     the escape needs all three of: !bRadarTraceLost, the contact's tile is NOT the 255
 *     ocean tile, and Game_RandU16(4) == 0. On escape: nRadarMsgTimer = 0, TEXTS[1601]
 *     "МАК приземлился", Funding_AddRegionBonus with that tile, item->nType = 4,
 *     item->nTimer = 7200, bRadarInterceptDone = 1.
 *     nPct < 40 (recomputed at 0x60B70 from a re-read nInterceptTargetHp) -- the contact is
 *     finished: nRadarMsgTimer = 0, TEXTS[1602] "МАК разбился", bRadarInterceptDone = 1 set
 *     BEFORE the tile test, then Funding_AddRegionBonus with the tile and either
 *     StrategList_RemoveAt (tile == 255, it went into the sea) or nType = 4 plus
 *     nTimer = 7200 (it came down on land).
 *
 * The two percentages are the same expression over the same value; the ladder is not
 * exclusive, so a contact that lands in the 40..69 band and is not escaped can still fall
 * into the < 40 branch in the same frame.
 *
 * ## The readout, 0x60CBC..0x6122C
 *
 * Twenty signal-bar rects and five cursor bars, all in index 0x28 / low 0x14, all offset by
 * nRadarScanOffset: five 10x22 at (n + 117/127/137/147/157, 57), five 10x14 at the same x
 * and y 156, five 20x14 at (2*(n + 13/23/33/43/53), 176), and five 5x20 at
 * (nRadarCursorX + n/2 + 1/6/11/16/21, nRadarCursorY + 1). The mids are the symmetric ramps
 * 0x55/0x5A/0x5C/0x5A/0x55 for the bars and 0x55/0x5F/0x69/0x5F/0x55 for the cursor. The
 * ARM's MOVS/ADDMI/ASR #1 pair at 0x60F64 is C division by two, rounding toward zero.
 *
 * Then the scroll advances and wraps: nRadarScanOffset += 10, and if the new value is ABOVE
 * 39 it becomes 0 (0x61084..0x610A0), so the cycle is 10, 20, 30, 0.
 *
 * TEXTS[1152] "Дист.: " is drawn at (120, 155) and itoa(nRadarRangeCur) at (180, 155); the
 * message string is drawn at (28, 175) only while nRadarMsgTimer < 30. When bRadarBlinkPhase
 * is set a 180x64 wash at (26, 176) in index 0x1F / mid 0x4A / low 0x75 goes down first and
 * then RadarScreen_BlitOverlay -- so the overlay sprite appears on alternate frames. Four of
 * the five button pads are blitted keyed 1 (0x611A8: MOV R8, #4): the fifth, the one
 * RadarScreen_Init loads last, is never drawn here. TEXTS[1153] "Урон: " is drawn at
 * (117, 59) and RadarScreen_DrawFuelGauge closes the frame.
 *
 * ## The three tail gates, 0x61234..0x6143C
 *
 * They run whether or not the panel was drawn, and only the first matching one runs.
 *
 * 1. bRadarInterceptDone && nRadarMsgTimer > 30 -- close, then send every empty craft home.
 *    For each craft (Map_nCraftCount is re-read every iteration, 0x612D8, because a
 *    replacement spawn can raise it): bHomeBase < 0xFE, bIntercepting set, and nStrategIdx
 *    equal to nInterceptStrategIdx. Craft_CountSoldiersAboard is called with the LOOP INDEX
 *    masked to a byte -- the IDB's own note on 0x612BC says the second argument is a craft
 *    index, not a base index. When the crew count is zero, Ship_RetargetTravel.
 * 2. bRadarCraftOutOfFuel && nRadarMsgTimer > 30 -- close, store 1 into the shortfall dword
 *    at 0x1870C8 (a DWORD store, as everywhere else), post message 44 with the ship index,
 *    copy Ship_FormatDisplayName's result into szScratch186488 @ 0x186488 (the geoscape
 *    caption scratch Geoscape_Draw and BaseFac_BuildStatusMsg also use), and tail-call
 *    Ship_RemoveWithCargo with that craft's Ship.bId. The craft is deleted, not sent home:
 *    an intercept craft that runs out of fuel is gone.
 * 3. bRadarFlip && nRadarRangeCur == 560 -- the range animation reached full zoom: close,
 *    Ship_RetargetTravel for the intercept craft, and when bRadarTraceLost is set the
 *    craft's own entry gets nTimer = 10.
 *
 * ORIGINAL BUG (0x61434), reproduced as dead code: the Ship_ClearInterceptByStrategIdx at
 * 0x6143C is conditional on ANDSEQ having left the zero flag set, and the only way to reach
 * it is bRadarTraceLost == 0 AND bRadarFlip == 0 -- but the gate at 0x613BC already requires
 * bRadarFlip to be non-zero. So the branch can never run, and when an intercept reaches full
 * zoom with the trace still held, the OTHER craft chasing the same contact are never sent
 * home. Recorded in bugs.tsv.
 *
 * Deviations: the original reads every one of its thirty-odd globals unguarded and indexes
 * apCraft[nInterceptStrategIdx] and the strateg array with no bound; the port checks the
 * pointers it was handed and refuses the frame when one is missing. The original's TEXTS
 * copies into the 100-byte stack buffer are unbounded strcpy-until-NUL loops; the port bounds
 * them (the longest of the five records used here is 21 bytes, so no real text is clipped). */
#define PUFO_RADAR_TEXT_DIST_LABEL     1152 /* lang.dat TEXTS[1152] "Дист.: " at (120, 155) */
#define PUFO_RADAR_TEXT_DAMAGE_LABEL   1153 /* lang.dat TEXTS[1153] "Урон: " at (117, 59) */
#define PUFO_RADAR_TEXT_TARGET_DEAD    1600 /* "МАК уничтожен" -- nInterceptTargetHp <= 0 */
#define PUFO_RADAR_TEXT_TARGET_LANDED  1601 /* "МАК приземлился" -- the contact slipped away */
#define PUFO_RADAR_TEXT_TARGET_CRASHED 1602 /* "МАК разбился" -- nInterceptTargetHp below 40% */
#define PUFO_RADAR_MSG_SHIP_LOST         44 /* 0x61344: the out-of-fuel message id */
#define PUFO_RADAR_UFO_PAL_SLOT          23 /* aPalette + 23 * 1024 = 0x14CED0, the ufos palette */
#define PUFO_RADAR_PANEL_X               20 /* 0x605DC: MOV R1, #0x14 */
#define PUFO_RADAR_PANEL_Y               50 /* 0x605D8: MOV R2, #0x32 */
#define PUFO_RADAR_PANEL_W              188 /* 0x605D0: MOV R3, #0xBC */
#define PUFO_RADAR_PANEL_H_BASE         140 /* 0x605AC: ADD R3, R0, #0x8C */
#define PUFO_RADAR_PANEL_H_BLINK         50 /* 0x60590: MOV R0, #0x32 when the phase is set */
#define PUFO_RADAR_UFO_Y_BASE           170 /* 0x607E4: RSB R3, R0, #0xAA */
#define PUFO_RADAR_UFO_X                 55 /* 0x60820: MOV R0, #0x37, the Init origin */
#define PUFO_RADAR_SCAN_STEP             10 /* 0x61084: ADD R0, R0, #0xA */
#define PUFO_RADAR_SCAN_WRAP             39 /* 0x61088: CMP R0, #0x27, MOVGT R0, #0 */
#define PUFO_RADAR_HP_ESCAPED_PCT        70 /* 0x60A28: CMP R0, #0x46, taken when at or above */
#define PUFO_RADAR_HP_CRASH_PCT          40 /* 0x60A30 and 0x60B90: CMP R0, #0x28 */
#define PUFO_RADAR_TIMER_LIMIT           30 /* 0x6112C draws the message below it, 0x6124C closes above */
#define PUFO_RADAR_ESCAPE_ROLL            4 /* 0x60A98: Game_RandU16(4), the escape needs 0 */
#define PUFO_RADAR_DETECT_CHANCE          2 /* 0x60A38: RadarScreen_RollDetection's argument */
#define PUFO_RADAR_TIMER_LANDED        7200 /* 0x60A20: MOV R9, #0x1C20, the landed contact's timer */
#define PUFO_RADAR_TIMER_TRACE_LOST      10 /* 0x6141C: MOV R3, #0xA */
#define PUFO_RADAR_FULL_RANGE           560 /* 0x613C4: CMP against PUFO_RADAR_RANGE_SCALE */
#define PUFO_RADAR_BAR_COUNT              5 /* 0x61088's five columns in each of the three rows */
#define PUFO_RADAR_BAR_X_FIRST          117 /* 0x60CDC: ADD R1, R0, #0x75 */
#define PUFO_RADAR_BAR_PITCH             10 /* 0x60D00: ADD R1, R0, #0x7F */
#define PUFO_RADAR_BAR_Y_TOP             57 /* 0x60CD4: MOV R2, #0x39 */
#define PUFO_RADAR_BAR_H_TOP             22 /* 0x60CC4: MOV R8, #0x16 */
#define PUFO_RADAR_BAR_Y_MID            156 /* 0x60DD0-ish: MOV R2, #0x9C */
#define PUFO_RADAR_BAR_H_MID             14 /* 0x60DCC-ish: MOV R8, #0xE */
#define PUFO_RADAR_BAR_Y_BOTTOM         176 /* 0x60E7C: MOV R2, #0xB0 */
#define PUFO_RADAR_BAR_H_BOTTOM          14
#define PUFO_RADAR_BAR_X_DOUBLED         13 /* 0x60E8C: 2 * (nRadarScanOffset + 13) */
#define PUFO_RADAR_BAR_IDX             0x28
#define PUFO_RADAR_BAR_LOW             0x14
#define PUFO_RADAR_CURSOR_X_FIRST         1 /* 0x60F78: ADD R1, R1, #1, then +5 per column */
#define PUFO_RADAR_CURSOR_PITCH           5
#define PUFO_RADAR_CURSOR_W               5 /* 0x60F48: MOV R3, #5 */
#define PUFO_RADAR_CURSOR_H              20 /* 0x60F70: STR R4, ... with R4 = 0x14 */
#define PUFO_RADAR_CURSOR_IDX            0x14
#define PUFO_RADAR_PANEL_RAIL_IDX      0x3C
#define PUFO_RADAR_PANEL_RAIL_MID      0x96
#define PUFO_RADAR_PANEL_RAIL_LOW      0xDC
#define PUFO_RADAR_WASH_IDX            0x1F /* 0x61164: the blink wash */
#define PUFO_RADAR_WASH_MID            0x4A
#define PUFO_RADAR_WASH_LOW            0x75
#define PUFO_RADAR_WASH_X              26
#define PUFO_RADAR_WASH_Y             176
#define PUFO_RADAR_WASH_W             180
#define PUFO_RADAR_WASH_H              64
#define PUFO_RADAR_BUTTON_BLIT_COUNT      4 /* 0x611A8: MOV R8, #4 -- the fifth pad is never drawn */
#define PUFO_RADAR_BUTTON_KEY             1
#define PUFO_RADAR_FRAME_KEY            0xFF

/* The five Ui_AnimExpandBlit calls, in the order the ARM makes them. The first one's height
 * is 140 + (bRadarBlinkPhase ? 50 : 0) and is filled in at run time; the table holds 0 for
 * it. All five share index 0x28, source origin 0x50/0x14 and key 0xFF. */
typedef struct RadarPanelBlit {
    i32 x;
    i32 y;
    i32 nW;
    i32 nH;
    i8  nMode;
} RadarPanelBlit;
extern const RadarPanelBlit g_aRadarPanelBlit[5];

/* The five 2-pixel rails that square the panel off, in ARM order. */
typedef struct RadarPanelRail {
    i32 x;
    i32 y;
    i32 nW;
    i32 nH;
} RadarPanelRail;
extern const RadarPanelRail g_aRadarPanelRail[5];

typedef struct RadarUpdateEnv {
    /* Everything Init/SetActive/the sub-drawers already need, reached through one pointer so
     * the caller builds the radar session once and hands the same object to both. */
    const RadarSessionEnv  *pSession;
    UiAnimBlitEnv          *pAnim;      /* byte_187080, the 0x187058 progress, root + 0x6E */
    const GfxColorEnv      *pColor;     /* Gfx_PackRgb565Pair's gamma and fade pair */
    const RadarPaletteEnv  *pPalette;   /* RadarScreen_CyclePalette, called twice */
    const RadarRangeBarEnv *pRangeBar;
    const RadarFuelEnv     *pFuel;
    const RadarArmamentEnv *pArmament;
    const RadarOverlayEnv  *pOverlay;
    const RadarDetectEnv   *pDetect;    /* also carries pTextRecs, szRadarMsg and its size */
    const ShipRemovalEnv   *pShipEnv;   /* the three Ship_* exits, the crew count, the spawn gate */
    const PadImage         *pTilemap;   /* 0x186B74, the current map grid pad Tilemap_GetAt reads */
    i32                    *anFundingBlock; /* unk_161F34, the five race scores and the multiplier */
    u32                    *pdwShortfall;   /* byte_1870C8, written as a dword */
    char                   *pszNameBuf;     /* szScratch186488 @ 0x186488, the caption scratch */
    size_t                  cchNameBuf;
    UiLineList             *pMsgList;   /* the root message list @ 0x158890 */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void                   *pSoundUser;
    /* UiControl_BlitActivePad's injected blits: the original calls PadImage_Blit and
     * PadImage_BlitEx(pPad, 0) itself, the port keeps the seam the control module defines. */
    UiControlBlitFn         pfnCtlBlit;
    UiControlBlitFn         pfnCtlBlitEx;
    void                   *pCtlBlitUser;
} RadarUpdateEnv;

void RadarScreen_Update(const RadarUpdateEnv *pEnv);

/* Geoscape_DrawScreen @ 0x6c16c (3,476 B). One geoscape frame: the wrapped map backdrop, the
 * strateg icons, the zoom strip pad, the clock strip, and then whichever overlay is open.
 *
 *   0x6C180  bMapSelectionFlag set -> tail-calls Geoscape_CloseUI(&0x1864F8) and NOTHING else
 *            is drawn this frame (0x6C188 BL, 0x6C194 B loc_6CEF0).
 *   0x6C1A4  the wrapped backdrop, instruction-identical to RadarScreen_BlitMapBackdrop's body.
 *   0x6C348  Smap_DrawStrategIcons(&gSmapCam), 0x6C354 PadImage_Blit([0x186BBC]),
 *            0x6C35C GameTime_Decode(0x159618), 0x6C364 Geoscape_DrawClockPads(0x159618).
 *   0x6C3B4  the options overlay: bOptionsScreenOpen AND Ui_AnimExpandBlit(..., mode 2). When the
 *            animation has not filled the rect yet the whole overlay is skipped and the frame
 *            falls through to the menu branch instead.
 *   0x6C640  otherwise gOptionsScratch[4] (a BYTE at 0x159E8C) gates the geoscape menu; zero and
 *            the frame ends. gOptionsScratch[0] (a SIGNED byte, LDRSB at 0x6C68C) picks the menu:
 *
 *                0  the options menu      "Опции" + Save / Load / Quit, four controls
 *                1  the save-slot list    "Выбрать запись" + seven MainMenu_pSlotCtx labels
 *                2  the load-slot list    "Выбрать загрузку" + the same seven labels
 *                3  the quit confirmation "Вы хотите выйти из игры", Да on ctlB / Нет on ctlA
 *                4  the volume screen     music and sound, six + six controls, levels 0..5
 *                any other value          the tail only (0x6C6B8 BNE loc_6CEB4)
 *
 *            Every case leaves its last string in psz and the shared tail (0x6CEB4) blits
 *            pGeoscapeCtlA and draws that string centred under it: "Отмена" for cases 0..3 and
 *            "OK" for case 4. Case 0 blits pGeoscapeCtlA TWICE (0x6CE84 and 0x6CEB8) — reproduced.
 *
 * Centring is `120 - Text_MeasureWidth(psz) / 2` at sixteen sites here (0x6C478, 0x6C5BC,
 * 0x6C7E8, 0x6C840, 0x6C968, 0x6C9CC, 0x6CA88, 0x6CAD0, 0x6CB9C, 0x6CBE4, 0x6CCF4, 0x6CD4C,
 * 0x6CDA8, 0x6CE08, 0x6CE68, 0x6CED8), all the same MOVS / ADDMI #1 / ASR #1 shape, which is C's
 * `/ 2`. Control `y` is read as a signed halfword (LDRSH [R1,#2] at 0x6C5B4, 0x6CBE0, 0x6CED0). */
#define PUFO_GEOOPT_MENU          0 /* gOptionsScratch[0]: the options menu itself */
#define PUFO_GEOOPT_SAVE_LIST     1 /* the seven save slots */
#define PUFO_GEOOPT_LOAD_LIST     2 /* the seven load slots */
#define PUFO_GEOOPT_QUIT_ASK      3 /* "Вы хотите выйти из игры" */
#define PUFO_GEOOPT_VOLUME        4 /* the music / sound level screen */
#define PUFO_GEOOPT_SLOT_COUNT    7 /* MainMenu_pSlotCtx @ 0x156938, seven 40-byte labels */
#define PUFO_GEOOPT_LABEL_STRIDE 40 /* 0x6CBFC ADD R5,R5,#0x28: the stride between slot labels */
#define PUFO_GEOOPT_VOL_STEPS     6 /* the 0..5 level columns, drawn at x 54 + 25*n */
#define PUFO_GEOOPT_VOL_X0       54 /* 0x6C888 ADD R5,R7,#0x36 */
#define PUFO_GEOOPT_VOL_STEP     25 /* 0x6C8B4 ADD R7,R7,#0x19, loop while R7 < 0x96 */
#define PUFO_GEOOPT_VOL_CONTROL0  9 /* case 4 walks aOptionsListCtl[9..20]: the six music buttons
                                     * at 0x159E44 then the six sound buttons at 0x159E5C */

/* The TEXTS records this frame draws (lang.dat texts.dat, 1650 records; the strings are the
 * shipped Russian text, read out of the file rather than guessed). */
#define PUFO_TEXT_REC_OPTIONS        20 /* "Опции" */
#define PUFO_TEXT_REC_VOL_MUSIC    1031 /* "Громкость музыки" */
#define PUFO_TEXT_REC_VOL_SOUND    1032 /* "Громкость звука" */
#define PUFO_TEXT_REC_OK           1104 /* "OK", the tail label under ctlA in the volume case */
#define PUFO_TEXT_REC_CANCEL       1144 /* "Отмена", the tail label under ctlA in every other case */
#define PUFO_TEXT_REC_OPT_COLUMNS  1154 /* "Корабль            Статус        База     Э\\Т\\О" */
#define PUFO_TEXT_REC_INTERCEPT    1155 /* "Перехват цели" */
#define PUFO_TEXT_REC_OPT_HEAD     1156 /* "Опции", the menu title */
#define PUFO_TEXT_REC_OPT_SAVE     1157 /* "Сохранить игру" */
#define PUFO_TEXT_REC_OPT_LOAD     1158 /* "Загрузить игру" */
#define PUFO_TEXT_REC_OPT_QUIT     1159 /* "Покинуть игру" */
#define PUFO_TEXT_REC_PICK_SAVE    1160 /* "Выбрать запись" */
#define PUFO_TEXT_REC_PICK_LOAD    1161 /* "Выбрать загрузку" */
#define PUFO_TEXT_REC_QUIT_ASK     1162 /* "Вы хотите выйти из игры" */
#define PUFO_TEXT_REC_YES          1163 /* "Да", under pGeoscapeCtlB */
#define PUFO_TEXT_REC_NO           1164 /* "Нет", under pGeoscapeCtlA by the shared tail */

/* The Ui_AnimExpandBlit call at 0x6C390. Hex-Rays labels the 0xB4 it stores twice, as nH AND as
 * nKey; the ARM stores 0xB4 in the nH slot (0x6C38C STR, dword) and ZERO in the nKey slot
 * (0x6C384 STRB into var_198), so nKey really is 0 here. */
#define PUFO_GEOOPT_ANIM_X            0
#define PUFO_GEOOPT_ANIM_Y           25 /* 0x6C390 MOV R2, #0x19 */
#define PUFO_GEOOPT_ANIM_W          236 /* 0x6C388 MOV R3, #0xEC */
#define PUFO_GEOOPT_ANIM_H          180 /* 0x6C380 MOV R0, #0xB4; STR */
#define PUFO_GEOOPT_ANIM_SRC_X     0x28
#define PUFO_GEOOPT_ANIM_SRC_Y     0x32
#define PUFO_GEOOPT_ANIM_MODE         2 /* full width, height 50*progress centred */

/* The menu panel frame: the full-screen wash and the per-case panel rect all use index 4 with the
 * same colour pair, and the volume case's inner border uses index 0x44 with 0xF4 / 0xFC. */
#define PUFO_GEOOPT_WASH_IDX          4
#define PUFO_GEOOPT_WASH_MID         67 /* 0x6C65C MOV R6, #0x43 */
#define PUFO_GEOOPT_WASH_LOW         80 /* 0x6C654 MOV R5, #0x50 */
#define PUFO_GEOOPT_WASH_KEY        100 /* 0x6C658 MOV R7, #0x64 */
#define PUFO_GEOOPT_BORDER_IDX     0x44
#define PUFO_GEOOPT_BORDER_MID     0xF4
#define PUFO_GEOOPT_BORDER_LOW     0xFC

/* The options overlay's column separators: three vertical rules at x 80 / 150 / 200 from y 70,
 * each 6 * pOptPanelMidA->nFont tall (MUL R2,R3,R8 with R8 = 6), and one horizontal rule under
 * them 236 wide. Index 10, mid 80, low 90. */
#define PUFO_GEOOPT_RULE_X0          80
#define PUFO_GEOOPT_RULE_X1         150
#define PUFO_GEOOPT_RULE_X2         200
#define PUFO_GEOOPT_RULE_Y          70
#define PUFO_GEOOPT_RULE_WIDTH     236
#define PUFO_GEOOPT_RULE_FONT_MUL    6
#define PUFO_GEOOPT_RULE_IDX        10
#define PUFO_GEOOPT_RULE_MID        80
#define PUFO_GEOOPT_RULE_LOW        90

typedef struct GeoscapeScreenDrawEnv {
    SmapView       *pView;          /* gSmapCam @ 0x1863E8 */
    PadImage       *pMapPad;        /* pSmapPadCur @ 0x186E40 */
    PadImage       *pLayerPad;      /* pSmapChromePad @ 0x186BE0 */
    PadImage       *pStripPad;      /* [0x186BBC], the zoom-strip pad */
    const u8       *pWorldBits;     /* pSmapWorldBits @ 0x186BC0 */
    i32             nOverlayOriginX;/* nSmapOverlayOriginX @ 0x186C20 */
    i32             nScreenId;      /* nGeoscapeScreen @ 0x186BC4 */
    PtrArray       *pStrategList;   /* 0x1864F8 + 0x3C */
    u16             nMapBitsBase;   /* fontSys.nMapBitsBase, the strateg sprite base */
    u16             nAnimPhase;     /* nUiSlot0AnimPhase @ 0x113A18, low halfword */
    GameTime       *pTime;          /* the clock block at 0x159608 */
    PadImage *const *apClockPads;   /* apClockDigitPads @ 0x186BF0 */
    const char     *pTextRecs;      /* aTextRecs @ 0xB38D0 */
    u8             *pbMapSelection; /* bMapSelectionFlag @ 0x159E84 */
    u8             *pbOptionsOpen;  /* bOptionsScreenOpen @ 0x159E80 */
    i8             *abOptionsScratch; /* gOptionsScratch @ 0x159E88: [0] the menu id, [4] visible */
    UiAnimBlitEnv  *pAnim;          /* the geoscape anim root @ 0x1864F8 */
    UiPanel        *pPanelShips;    /* pOptPanelShips @ 0x159E18 */
    UiPanel        *pPanelMidA;     /* pOptPanelMidA @ 0x159E78, the one whose nFont sizes the rules */
    UiPanel        *pPanelMidB;     /* pOptPanelMidB @ 0x159E7C */
    UiPanel        *pPanelRight;    /* pOptPanelRight @ 0x159E1C */
    UiControl      *pCtlA;          /* pGeoscapeCtlA @ 0x186BB4, the cancel / OK control */
    UiControl      *pCtlB;          /* pGeoscapeCtlB @ 0x186BB0, the "Да" control */
    UiControl     **apOptCtl;       /* aOptionsListCtl @ 0x159E20, 21 slots through 0x159E73 */
    const char     *pSlotLabels;    /* MainMenu_pSlotCtx @ 0x156938, seven 40-byte labels */
    GfxColorEnv    *pColor;
    /* Geoscape_CloseUI(&0x1864F8), injected because that dispatcher lives in the base-ui module
     * and takes its own environment. */
    void (*pfnCloseUi)(void *pUser);
    void            *pCloseUser;
    UiControlBlitFn  pfnCtlBlit;
    UiControlBlitFn  pfnCtlBlitEx;
    void            *pCtlBlitUser;
} GeoscapeScreenDrawEnv;

void Geoscape_DrawScreen(const GeoscapeScreenDrawEnv *pEnv);

/* ------------------------------------------------- the geoscape pulse tick
 *
 * Evidence: Geoscape_PulseUpdate @ 0x954dc (1,812 B). The port already routes to it: Input_OnPointer
 * hands over through the injected pfnPulseUpdate slot in pufo_input.h.
 *
 * 1. 0x954E8/0x954F0: dwActionFail @ 0x1870C8 == 1 stops the whole tick, cooldowns included.
 * 2. 0x95500..0x955FC, the MAP screen only. For every craft whose bAtBase (Ship +0x06) is clear, the
 *    strateg list is scanned for an entry with nDisplayId == Ship.bId, bActive == 0 and nType == 1; a
 *    match gets bVisible = 1 (0x955C0) and the craft gets bWeaponsHot (Ship +0x34) = 1 (0x955C8). The
 *    IDB's own note on this function records what it is NOT: it is not the alien-site discovery pass,
 *    it makes an INACTIVE type-1 entry visible, which is the player's own craft record.
 * 3. 0x95608..0x95634: still map-screen only, gated on the pause byte 0x15961C, the clock runs
 *    Geoscape_AdvanceTime(nGeoscapeTickSeconds + 1) and then GameTime_Decode. The craft loop's
 *    `Map_nCraftCount == 0` exit jumps INTO this block (LABEL_13 at 0x95618), so an empty craft list
 *    still advances the clock -- it is not a skip of the whole screen-0 half.
 * 4. 0x95644..0x95654: the animation-phase gate. nUiSlot0AnimPhase @ 0x113A18 is counted up elsewhere
 *    (0x36664) and cleared by UiControl_HitTest. Here, if it is ALREADY above 1 the switch is skipped;
 *    otherwise it is incremented and the switch runs only when that increment lands above 1 -- so the
 *    switch fires exactly when the phase arrives as 1. Either way the phase ends at 0 (LABEL_88).
 * 5. Screens 0 and 2: the held-direction scroll. The four flag bytes unk_1864F4..F7 are written by the
 *    nav Prev/Next buttons (the panel-scroll dispatcher in pufo_baseui.h records which two) and here
 *    mean +y, +x, -y, -x. nLastDx / nLastDy are preset to 120 / 160 and the call is
 *    Smap_ScrollBy(view, dx + 120, dy + 160), so the drag is absolute rather than incremental -- which
 *    is what Smap_ScrollBy's `pos - delta + last_delta` form expects. The step is 25 on the map screen
 *    and 5 on the zoom screen. All four flags are cleared afterwards (0x95874..0x95880).
 * 6. Every other screen gets its controls driven with bForceClick = 1 -- the auto-repeat of a held
 *    button. The control arrays belong to the screens themselves, so the port takes them as a
 *    per-screen table; the original's own shapes are recorded in GeoscapePulseCtlGroup below.
 * 7. 0x95B80..0x95BCC: four saturating counters, each `if (x < 255) ++x` -- nRadarCooldownR @ 0xAC218,
 *    nRadarCooldownL @ 0xAC21C, nRadarMsgTimer @ 0xAC220, nRadarPulseTimer @ 0xAC224. They run on
 *    every screen, after the switch, and they are the only part that does.
 *
 * The original's per-screen groups, as the ARM addresses them:
 *   4   apSoldierPanelCtl @ 0x1598B0, then the control at 0x1598B4
 *   6   two passes; each pass runs the base+0x64 control while bPlaneStoresCommitted @ 0x161F28 is set
 *       and the base+0x5C control while bPlaneListPanelsBuilt @ 0x161F24 is set, with the base advancing
 *       one dword per pass from pPlaneBackdropPad @ 0x161A48
 *   7   the controls at 0x158874 and 0x158878, only while bBaseFacPlaceArmed @ 0x158888 is CLEAR
 *   8   two controls reached through the research topic table @ 0x15EF08, one pair when
 *       bUiRowSelected @ 0x160C80 is set and a different pair when it is clear
 *   9   six consecutive controls reached from 0x159E98
 *   10  two consecutive controls reached from 0x1870D8
 *   11  two consecutive controls reached from pBuySellScreenPad @ 0x161A08
 *   12  the same two again, reached from pBuySellScreenPad a second time
 *   16  two reached from 0x1872F0 while bUfopaediaOpen @ 0x18730C is set and bUfopaediaEntryDirty
 *       @ 0x187310 is CLEAR, plus two reached from 0x1872E0 when the dirty byte is set and 0x1871FC is
 *       set -- the halves are mutually exclusive on the dirty byte
 *   17  two reached from 0x15EE78 while byte_1870F8 is CLEAR
 *   18  aOptionsListCtl[7] and aOptionsListCtl[8] while bOptionsScreenOpen @ 0x159E80 is set */

/* One auto-repeat group: nCount consecutive controls, optionally gated on a flag byte. */
typedef struct GeoscapePulseCtlGroup {
    UiControl *const *apCtl;
    i32         nCount;
    const u8   *pbGate;    /* NULL = always; otherwise the group runs only while this is non-zero */
    const u8   *pbNotGate; /* NULL = always; otherwise the group runs only while this is zero */
} GeoscapePulseCtlGroup;

typedef struct GeoscapePulseScreen {
    i32                          nScreenId;
    const GeoscapePulseCtlGroup *pGroups;
    i32                          nGroupCount;
} GeoscapePulseScreen;

#define PUFO_PULSE_SCROLL_STEP_MAP  25  /* 0x95718, the map screen */
#define PUFO_PULSE_SCROLL_STEP_ZOOM  5  /* 0x95844, the zoom screen */
#define PUFO_PULSE_SCROLL_BASE_X   120  /* 0x95714, the nLastDx preset */
#define PUFO_PULSE_SCROLL_BASE_Y   160  /* 0x9570C, the nLastDy preset */
#define PUFO_PULSE_COOLDOWN_MAX    255  /* every one of the four counters saturates here */

/* unk_1864F4..F7, in the order the original reads them. */
#define PUFO_PULSE_FLAG_UP    0
#define PUFO_PULSE_FLAG_RIGHT 1
#define PUFO_PULSE_FLAG_DOWN  2
#define PUFO_PULSE_FLAG_LEFT  3

typedef struct GeoscapePulseEnv {
    const i32          *pnActionFail;   /* dwActionFail @ 0x1870C8 */
    i32                 nScreenId;      /* nGeoscapeScreen @ 0x186BC4 */
    SmapView           *pView;          /* gSmapCam @ 0x1863E8 */
    i32                 nStripHeight;   /* pSmapStripPad @ 0x186BBC uHeight, Smap_ScrollBy's clamp */
    u8                 *pabDragFlags;   /* unk_1864F4..F7 */
    Ship *const        *apCraft;        /* Map.apCraft @ Map+0x10AC */
    const u8           *pnCraftCount;   /* Map_nCraftCount @ 0x114B4C, read LDRB at 0x95524 / 0x955F4 */
    PtrArray           *pStrategList;   /* the strateg list */
    GameTime           *pTime;          /* the clock block @ 0x159608 */
    const GeoscapeClockHooks *pClockHooks;
    i32                 nTickSeconds;   /* nGeoscapeTickSeconds @ 0x187070 */
    i32                *pnAnimPhase;    /* nUiSlot0AnimPhase @ 0x113A18 */
    i32                *pnCooldownR;    /* nRadarCooldownR @ 0xAC218 */
    i32                *pnCooldownL;    /* nRadarCooldownL @ 0xAC21C */
    i32                *pnMsgTimer;     /* nRadarMsgTimer @ 0xAC220 */
    i32                *pnPulseTimer;   /* nRadarPulseTimer @ 0xAC224 */
    const GeoscapePulseScreen *pScreens;
    i32                 nScreenCount;
    const UiControlHitCtx *pHitCtx;     /* the context the forced clicks are handed */
} GeoscapePulseEnv;

void Geoscape_PulseUpdate(const GeoscapePulseEnv *pEnv);

/* ------------------------------------------------- the new-base globe hover frame
 *
 * Evidence: Geoscape_DrawGlobeHover @ 0x6727c (2,208 B), reached from the geoscape dispatcher's
 * jumptable default arm (screen ids 13 and 14) at 0x91D8C. That dispatcher is still unported, so it
 * is named by address only. The caller loads nSmapCursorX / nSmapCursorY into R1 / R2 first, but the
 * callee's prologue pushes only R0 (0x67280), so those two are dead; the callee's return value is
 * stored with STRB into bPendingRegionId @ 0x159DF4.
 *
 * 1. 0x67290..0x672D8: the wash key is Game_RandU16(10) and any roll below 8 becomes 0, so only 8
 *    and 9 survive. The whole 240x320 surface is washed with palette index 0xFF at that key, which is
 *    what lets the globe show through on two rolls out of ten.
 * 2. 0x672DC..0x67450: the crosshair. A 1-wide rule at x=120 from y=0 down to 320 - uHeight, a
 *    1-tall rule across the full width at (320 - uHeight) / 2 -- the halving triple at 0x6733C, so
 *    plain C division -- and eight tick marks at (i + 3)^2, that is 9, 16, 25, 36, 49, 64, 81, 100,
 *    mirrored on both axes. All in index 0x32 / 0xC8 / 0x32.
 * 3. 0x67450..0x67494: the bottom panel opens through Ui_AnimExpandBlit(0, 320 - uHeight, 236,
 *    uHeight - 6, index 4, source 0x46/0x4B, key 0xC8, mode 3). While the animation is still
 *    expanding the function returns 0 here and nothing below it runs.
 * 4. 0x67498..0x67674: the scan animation. Two 75x30 dark rects at (160, 200) and (5, 200), then a
 *    colour pair whose green is 5 * (51 - key) -- 255 for the ordinary key 0, 210 or 215 when the wash
 *    let the globe through -- then a 10x72 strip at y=242 driven by nGlobeScanX (a left copy gated on
 *    scanX + 10 < 230 and a right copy gated on 230 - scanX > 10), a pair of vertical rules driven by
 *    nGlobeSweepV while it is under 37 and a pair of horizontal rules driven by nGlobeSweepH while it
 *    is under 15. The counters advance after the drawing (0x67664..0x676A8): all three go up by one,
 *    nGlobeScanX wraps when it passes 320, and nGlobeSweepV + 1 > 40 zeroes BOTH sweep counters.
 * 5. 0x676AC..0x67854: two readout lines at (5, 200) and (5, 214). Each is " " followed by three
 *    itoa(Game_RandU16(900) + 100) joined by '.' -- six rolls per frame, none of them buffered.
 * 6. 0x67858..0x67894: the pending pick. The viewport's nPickX / nPickY are copied into the facility
 *    block at +0x62C / +0x630 (0x159D94 / 0x159D98), and the hovered tile is Smap_GetTileAtScreen at
 *    (120, (320 - uHeight) / 2) -- 0x67884..0x67890 is the halving triple again, so the odd +1 the
 *    decompiler shows for uHeight > 0x140 is just C's truncation toward zero, not a special case.
 * 7. 0x678B8..0x67B0C: the labels. Ocean (tile 0xFF) gets TEXTS[1042] centred at y=250 and nothing
 *    else. A region gets TEXTS[1136] + TEXTS[905 + tile] centred at 250, then TEXTS[1137] +
 *    itoa(aNewBaseCostByRegion[tile - 0x80]) + "$" centred at 270 -- only while bPlacementPending
 *    @ 0x1870AC is clear -- then the confirm control moved to (120 - uWidth/2, 290), blitted, and
 *    TEXTS[1603] centred over it at 290. The tile is the return value, which the caller stores as
 *    bPendingRegionId.
 *
 * The four label sources are TEXTS records reached by absolute address instead of through the base:
 * 0xE7B3A, 0xEC680, 0xEC74D and 0x103C77 are aTextRecs + 205*1042, +205*1136, +205*1137 and
 * +205*1603. With lang.dat loaded they read "Can`t build here", "Terrain: ", "Base cost: " and
 * "Build base". The region names are TEXTS[1033..1041] -- nine regions -- and TEXTS[1042] doubles as
 * the ocean label, which is why tile 0x89, cost 0 in aNewBaseCostByRegion, is the unbuildable slot. */
#define PUFO_GLOBE_TEXT_OCEAN     1042 /* "Can`t build here", also the name of tile 0x89 */
#define PUFO_GLOBE_TEXT_TERRAIN   1136 /* "Terrain: " */
#define PUFO_GLOBE_TEXT_COST      1137 /* "Base cost: " */
#define PUFO_GLOBE_TEXT_BUILD     1603 /* "Build base" */
#define PUFO_GLOBE_TEXT_REGION0    905 /* TEXTS[905 + tile] is the hovered region's name */

#define PUFO_GLOBE_WASH_IDX          0xFF
#define PUFO_GLOBE_WASH_KEY_SPAN     10 /* Game_RandU16(10) at 0x67290 */
#define PUFO_GLOBE_WASH_KEY_MIN       8 /* 0x672AC: rolls 0..7 collapse to 0 */
#define PUFO_GLOBE_RULE_IDX          0x32
#define PUFO_GLOBE_RULE_MID          0xC8
#define PUFO_GLOBE_RULE_LOW          0x32
#define PUFO_GLOBE_TICK_COUNT          8
#define PUFO_GLOBE_TICK_BASE           3 /* the tick offsets are (i + 3)^2 */
#define PUFO_GLOBE_PANEL_IDX           4
#define PUFO_GLOBE_PANEL_SRC_X      0x46
#define PUFO_GLOBE_PANEL_SRC_Y      0x4B
#define PUFO_GLOBE_PANEL_KEY        0xC8
#define PUFO_GLOBE_PANEL_WIDTH       236 /* 0x67484 MOV R3, #0xEC */
#define PUFO_GLOBE_PANEL_INSET         6 /* 0x67470 SUB R3, R0, #6 */
#define PUFO_GLOBE_READOUT_X           5
#define PUFO_GLOBE_READOUT_Y0        200
#define PUFO_GLOBE_READOUT_Y1        214
#define PUFO_GLOBE_READOUT_SPAN     0x383 /* Game_RandU16(900) at 0x676D0 and five more */
#define PUFO_GLOBE_READOUT_BASE      100 /* every readout number is roll + 100 */
#define PUFO_GLOBE_READOUT_PARTS       3
#define PUFO_GLOBE_SCAN_IDX            8
#define PUFO_GLOBE_SCAN_MID           80
#define PUFO_GLOBE_SCAN_LOW           85
#define PUFO_GLOBE_SCAN_KEY          100
#define PUFO_GLOBE_SCAN_X0            10
#define PUFO_GLOBE_SCAN_SPAN         230 /* 0x6752C / 0x67560: the two copies' bounds */
#define PUFO_GLOBE_SCAN_Y            242
#define PUFO_GLOBE_SCAN_W             10
#define PUFO_GLOBE_SCAN_H             72
#define PUFO_GLOBE_SCAN_WRAP         320 /* 0x67684: nGlobeScanX resets when it passes this */
#define PUFO_GLOBE_SWEEP_IDX        0x0A
#define PUFO_GLOBE_SWEEP_MID        0xFF
#define PUFO_GLOBE_SWEEP_LOW        0x0A
#define PUFO_GLOBE_SWEEP_X           160
#define PUFO_GLOBE_SWEEP_Y           200
#define PUFO_GLOBE_SWEEP_H_W          75
#define PUFO_GLOBE_SWEEP_V_H          30
#define PUFO_GLOBE_SWEEP_V_GATE       37 /* 0x67594: the vertical pair stops drawing above this */
#define PUFO_GLOBE_SWEEP_H_GATE       15 /* 0x67600: the horizontal pair stops above this */
#define PUFO_GLOBE_SWEEP_V_WRAP       40 /* 0x67694 CMP #0x28: both counters zero past this */
#define PUFO_GLOBE_SWEEP_V_BASE       80 /* 0x675C4 x = 2 * (nGlobeSweepV + 80) */
#define PUFO_GLOBE_SWEEP_V_SPAN      235 /* 0x675EC x = 235 - 2 * nGlobeSweepV */
#define PUFO_GLOBE_SWEEP_H_BASE      100 /* 0x6762C y = 2 * (nGlobeSweepH + 100) */
#define PUFO_GLOBE_SWEEP_H_SPAN      115 /* 0x67658 y = 2 * (115 - nGlobeSweepH) */
#define PUFO_GLOBE_LABEL_Y_NAME      250
#define PUFO_GLOBE_LABEL_Y_COST      270
#define PUFO_GLOBE_LABEL_Y_BUILD     290

typedef struct GeoscapeGlobeHoverEnv {
    const PadImage   *pStripPad;          /* the object [0x186BBC] points at; uHeight is the strip */
    const u8         *pTiles;             /* the current zoom tile grid, [0x186B04]->pData */
    SmapView         *pView;              /* gSmapCam @ 0x1863E8 */
    UiAnimBlitEnv    *pAnim;              /* the geoscape anim root @ 0x1864F8 */
    i32              *pnPendingPickX;     /* abFacilityGrid + 0x62C @ 0x159D94 */
    i32              *pnPendingPickY;     /* abFacilityGrid + 0x630 @ 0x159D98 */
    i32              *pnScanX;            /* nGlobeScanX @ 0x159E00 */
    i32              *pnSweepV;           /* nGlobeSweepV @ 0x159DFC */
    i32              *pnSweepH;           /* nGlobeSweepH @ 0x159DF8 */
    const u8         *pbPlacementPending; /* bPlacementPending @ 0x1870AC, gates the cost line */
    UiControl        *pCtlConfirm;        /* unk_186BA4, the "Build base" button */
    const char       *pTextRecs;          /* aTextRecs @ 0xB38D0 */
    const u32        *paRegionCost;       /* aNewBaseCostByRegion @ 0xAB488, indexed tile - 0x80 */
    GfxColorEnv      *pColor;
    UiControlBlitFn   pfnCtlBlit;
    UiControlBlitFn   pfnCtlBlitEx;
    void             *pCtlBlitUser;
} GeoscapeGlobeHoverEnv;

/* Returns the hovered tile, which the caller stores as bPendingRegionId, or 0 while the panel
 * animation is still expanding (0x67490 ANDS / BEQ loc_67B10). */
i32 Geoscape_DrawGlobeHover(const GeoscapeGlobeHoverEnv *pEnv);

#endif /* PUFO_GEOSCAPE_H */
