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
#include "ui/pufo_control.h"   /* UiControl, for the nav speed controls */

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
    /* Smap_CanInterceptReach (0x7e074), NOT ported. */
    bool (*pfnCanInterceptReach)(i32 nShipIdx, void *pUser);
    /* UiPanel_AppendLineWithSound @ 0x5a394. nMsg: 15 contact, 13 intercept
     * lost, 43 low supply, 10 radar contact. GameTime_Pause @ 0x5d6ac is NOT
     * part of the hook: 15, 13 and 10 pause the clock, 43 does not, so the
     * port writes time.bPaused itself at the three pausing sites. */
    void (*pfnContactMsg)(i32 nMsg, i32 nArg3, i32 nArg4, void *pUser);
    /* Geoscape_SpawnMissionSite (0x7e49c). */
    void (*pfnSpawnMissionSite)(StrategItem *pUfo, i32 nMissionSub, void *pUser);
    /* Smap_GrantSiteRecovery (0x98b50), NOT ported. */
    void (*pfnGrantSiteRecovery)(void *pUser);
    /* ManufScreen_RefreshBaseTabs (0x70ebc), NOT ported. */
    void (*pfnRefreshBaseTabs)(void *pUser);
    /* ResearchScreen_AccumulateAllocated @ 0x74914, ported in pufo_baseui.c behind
     * ResearchAccumulateEnv; the shell wires this hook to it. */
    void (*pfnAccumulateResearch)(void *pUser);
    /* Base_TickFacilityConstruction (0x62824), NOT ported. */
    void (*pfnTickFacilityConstruction)(void *pUser);
    /* Base_ClearFacilityGridRow @ 0x6a0e0; its byte result becomes the
     * per-base site state byte_161F38[i]. */
    i32 (*pfnClearFacilityGridRow)(i32 nBaseIdx, void *pUser);
    /* Base_CountEmptyFacilityCells @ 0x6aee0. */
    i32 (*pfnCountEmptyFacilityCells)(i32 nBaseIdx, void *pUser);
    /* FundingScreen_OpenMonthly (0x5d160), NOT ported. */
    void (*pfnOpenMonthly)(void *pUser);
    /* Base_FindIndexBySiteId (0x5a8d0), NOT ported; passed through to the detect pass. */
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

/* Base_FindIndexByTypeByte @ 0x6ac30. Linear search over the base array for the first
 * base whose type byte (BaseRec.bUnk4 @ +0x04) equals nType.
 *
 * With a base count of zero it returns -1 immediately (0x6ac50). Otherwise the loop has
 * NO bound at all: it walks forward until some entry happens to match, off the end of
 * the array if none does. The port bounds it by nBaseCount and returns -1 (deviation).
 *
 * The comparison is against a `char` field. WinCE ARM treats plain char as unsigned,
 * and the port compares as u8, which is what the original does in practice. */
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
 * the other is Base_CountItemQtyAcrossBases (0x67d68), still unported. */
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
 * psi marker bUnk149 @ 0x95. The budget of Base_UpdatePsiLabNeeded (divisor 10). */
i32 Base_CountPsiSoldiers(Soldier *const *apSoldiers, i32 nSoldierCount, i32 nBaseIndex);

/* Base_CountOutboundTransferSpace @ 0x68044. The recovery load (uRecoveredSoldiers +
 * bRecoveredSoldierAssign + auItemQty[112] + auItemQty[113]) of site nSiteIdx, plus, when
 * bScanSites, of every other site whose bFlag1 equals apBases[nBaseIndex]->bUnk4. Budget term
 * of Base_UpdateLivingQuartersNeeded. The original's first argument is never read. */
i32 Base_CountOutboundTransferSpace(const SmapSite *aSites, i32 nSiteCount, i32 nSiteIdx,
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
 * (-6000, -6000, 0, "new") for the starting base, and BaseFacScreen_DrawPlacementOverlay @
 * 0x59AFC with the pending placement coordinates (Map+0x62C / +0x630) and nFacType (0x159DF4).
 *
 * What it does, in the original's order:
 *   1. BaseRec_Create: a new 20-byte BaseRec at apBases[nBaseCount] (the count is NOT raised
 *      yet). The original then dereferences apBases[nBaseCount] with no null test (0x66A08);
 *      the port guards (deviation).
 *   2. StrategList_AddItem: a type-0, inactive, invisible-flag-1 site entry at (x, y) with
 *      nId = the next site id (dword_186558).
 *   3. The strateg entry at index nBaseCount gets bRandA = nKind - 0x80 (0x669B4; Hex-Rays
 *      renders the SUB as + 0x80, the ARM is `SUB R11, R10, #0x80`).
 *   4. The BaseSiteRec for this base (0x186C40 + 64*nBaseCount): abName = the name copied to its
 *      NUL with no bound (0x669D0 - ORIGINAL BUG, the field is 20 bytes and the overflow tail
 *      lands in the four dwords and past the record), bType = nKind - 0x80, nUnk48 /
 *      nDemolishState / nBuildState = 0, nFacType = the site id.
 *   5. The BaseRec: bUnk17 = nKind, nUnk8 = x, nUnk12 = y, bUnk16 = abKindTable[nKind] - 1,
 *      where abKindTable is the runtime block at 0x1864DD (all zero in the image).
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
    const u8 *abKindTable;      /* 0x1864DD, indexed by nKind */
    u32 *pProductU32Col;        /* 0x159E9C + 800 * nBaseCount */
    u16 *pProductU16Col;        /* 0x15D09C + 400 * nBaseCount */
    u16 *pResearchCol;          /* 0x16022C + 288 * nBaseCount */
} BaseCreateEnv;

i32 Base_CreateNamedEntry(const BaseCreateEnv *pEnv, i32 x, i32 y, i32 nKind, const char *pszName);

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

/* ------------------------------------------------- the facility label of the build dialog
 *
 * Evidence: BaseFac_FormatCostLabel @ 0x6a404. Two callers: BaseFacScreen_DrawPlacementOverlay @
 * 0x59AE4, and Geoscape_OnMapClick @ 0x8B550, which copies the result into the aBaseFacilities
 * record's label field (0x186C5A, that is record + 26) after storing kind + 0x80 at record + 0.
 *
 * It builds `TEXTS[905 + nFacType] name` + "-" + decimal(abBaseKindTable[nFacType]) into the
 * shared scratch string at 0x186B10 (szRadarMsg, dword_186B08 + 8 -- the same buffer the radar
 * screen, Soldier_FormatDisplayName and Ship_FormatDisplayName write), and then INCREMENTS
 * abBaseKindTable[nFacType]. So the number the label shows is the table's value before the call,
 * and calling it twice for the same kind shows one more the second time.
 *
 * abBaseKindTable @ 0x1864DD is a 15-byte runtime table, all zero in the image, and it has no
 * xref in the IDB at all: every access is computed as 0x1864F8 + nFacType - 0x1B for the load
 * (0x6A46C) and 0x1864F8 + 0x65 + nFacType - 0x80 for the store (0x6A49C), which is why Hex-Rays
 * renders it as an expression on the unrelated pObj global instead. It is the same table
 * Base_CreateNamedEntry reads for BaseRec.bUnk16.
 *
 * The value is loaded SIGNED (LDRSB) and the increment is sign-extended before the byte store
 * (0x6A490..0x6A49C), so 127 wraps to -128. Facility kinds run 0..16 while the table has 15
 * slots, so kinds 15 and 16 read and write past it into the pSmapPadZoom2 / pSmapPadZoom1
 * globals -- recorded in bugs.tsv.
 *
 * The original's first argument (the grid pointer) is never read: R0 is replaced by the label
 * buffer before the first strcat (0x6A458). The port drops it. The name copy is bounded here
 * (pufo_strncpy) where the original copies with no bound at all. */
#define PUFO_FAC_NAME_REC_BASE   905 /* TEXTS record of facility kind 0 */
#define PUFO_BASE_KIND_TABLE_LEN 15  /* abBaseKindTable @ 0x1864DD */

char *BaseFac_FormatCostLabel(char *pszLabel, size_t cchLabel, const u8 *pTextRecs,
                              u8 nFacType, i8 *abKindTable);

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

#endif /* PUFO_GEOSCAPE_H */
