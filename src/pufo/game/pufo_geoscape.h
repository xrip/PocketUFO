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
#include "ui/pufo_geouimanage.h" /* GeoscapeUiState and PUFO_GEOUI_ROOT_CTLS, the 0x1B arm's thirteen controls */

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

/* The 24-byte mission-site record at aMissionSites @ 0x1865C0 -- the 0x1864F8 root's +0xC8, which
 * InitUI reaches as `LDR R7, =0x1864F8 / ADD R1, R7, #0xC8` (0x86C6C, 0x86CA4). InitUI reads 24 bytes
 * from "<i>.cts" for 56 records (0x86C40..0x86CDC, the loop test is CMP R0, #0x38) and copies text
 * record 1655+i into +4; with lang.dat loaded only 1650 text records exist, so every name is empty in
 * the original too. The flag-0x10 status panel indexes the same table at 0x90574, which is why the
 * record lives here and not in ui/pufo_geoiinit.h. */
#define PUFO_MISSION_SITE_COUNT 56
typedef struct MissionSiteRec {
    u32  uField0;    /* +0, from the .cts file */
    char szName[20]; /* +4, 0x90584 ADD R1, R3, #4 */
} MissionSiteRec;
_Static_assert(sizeof(MissionSiteRec) == 24, "the mission-site record is 24 bytes");

typedef struct GeoscapeState {
    GameTime     time;
    GeoscapeWorld world;
    /* The strateg list block @ 0x186534: a {nCount, apItems} PtrArray with the two id counters at
     * +0x10 / +0x14, which is the shape every other env in this port takes. StrategList (pufo_strateg.h)
     * is a VIEW of that block, and its array pointer goes stale as soon as the list grows, so the
     * ticks rebuild the view from these three words at every point the original re-reads the list and
     * write the counters back (StrategList_UpdateDetect is the one callee that increments them). */
    PtrArray    *pList;             /* strategList @ 0x186534 */
    i32         *pnNextDisplayId;   /* nNextStrategDisplayId @ 0x186544 */
    i32         *pnNextSiteId;      /* nNextStrategSiteId @ 0x186548 */

    u16  nDaysSinceStart;   /* word_1472C8 @ 0x1472c8, capped at 200 */
    i32  nSelectedShipIdx;  /* nSelectedShipIdx @ 0x187054 */
    i32  nSelectedShipAux;  /* gUiRootPanel.anLineAux[140] */
    u8   bLowSupplyFlag;    /* dwActionFail @ 0x1870C8 -- one latch, two names: the ship pass writes it
                             * as the low-supply shortfall (a dword store at 0x61340), the status panel's
                             * action-fail line 0x8E658 and Geoscape_PulseUpdate's gate 0x954EC read it. */

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
    /* ManufScreen_RefreshBaseTabs @ 0x70ebc, ported in pufo_baseui.c behind ManufRefreshEnv. */
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
    /* FundingScreen_OpenMonthly @ 0x5d160, ported in pufo_baseui.c behind FundingInitEnv. */
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

/* The user pointer those four hooks carry. Geoscape_Advance builds it on the stack; a caller that
 * drives the clock through Geoscape_PulseUpdate (0x95618 hands Geoscape_AdvanceTime the hooks
 * directly, not a GeoscapeHooks) builds one here and keeps it with its env. */
typedef struct GeoscapeTickBundle {
    GeoscapeState       *pState;
    const GeoscapeHooks *pHooks;
} GeoscapeTickBundle;

/* Fill pOut with the four tick hooks over pBundle. The bundle must outlive pOut. */
void Geoscape_BuildClockHooks(GeoscapeTickBundle *pBundle, GeoscapeClockHooks *pOut);

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

/* ---- Geoscape_Draw @ 0x8c180 map-view chrome (0x8C2B0..0x8C760) ---------------------
 *
 * The cases 0 and 2 arm of the geoscape dispatcher, taken when bRadarScreenActive @ 0x159678
 * is zero: the world map itself. Three parts, in the ARM's order.
 *
 * 1. The overlay drift (0x8C2B0..0x8C3D4). _rt_sdiv takes the DIVISOR in R0, so 0x8C2F4's
 *    `MOV R0, #0xFFFFFFE8` / `MOV R1, R7` with R7 = pSmapPadCur->uWidth is
 *
 *        nDiv = (i32)uWidth / -24
 *
 *    and the double chain at 0x8C30C..0x8C3C8 is
 *
 *        nSmapOverlayOriginX = (int)((double)(nGameHour * nDiv)
 *                                    - ((double)uWidth * (double)nGameMinute) * (1.0 / 1440.0))
 *
 *    The 1/1440 is the literal double 0x3F46C16C16C16C17 at 0x8C38C (minutes in a day), and
 *    _dtoi truncates toward zero like the C cast. With the shipped 240-wide map pad this is
 *    -10 * nHour - nMinute / 6: the day/night overlay slides one map width per game day.
 *
 * 2. The scroll wrap (0x8C3D8..0x8C494). nScrollX is pulled into (-nHalfW - nMapW, -nHalfW]
 *    by at most one map width in each direction, and nScrollY the same way -- but the Y half
 *    runs ONLY when nGeoscapeScreen is 2. The X half runs for both ids. That asymmetry is the
 *    ARM's: 0x8C3F0 `BNE loc_8C478` sends every other screen id to a copy that clamps X and
 *    then falls straight into the blit block at 0x8C498 without touching Y.
 *
 * 3. The blits (0x8C498..0x8C690). The map pad is blitted at x = -(nScrollX + nHalfW),
 *    y = -(nScrollY + nHalfH); screen 2 adds two more at y + nMapH (one at x, one at
 *    x + nMapW); and every id ends with the x + nMapW copy at the original y. The layer pad
 *    then gets its three world-bitmap flips at the same three x offsets the radar backdrop
 *    uses, each skipped for screen 2 -- the same SoftSurf_BlitFlip triple as
 *    Smap_BlitWrappedBackdrop, with the layer pad's y copied from the map pad's last
 *    nOriginY. Unlike the radar copy, this one blits the map pad up to four times.
 *
 * Deviation: the original reaches every object through globals that are always live here; the
 * port takes them through the env and returns without drawing when one is missing. */
#define PUFO_SMAP_OVERLAY_DIVISOR       (-24) /* 0x8C2F4: MOV R0, #0xFFFFFFE8, _rt_sdiv's divisor */
#define PUFO_SMAP_OVERLAY_MINUTES_PER_DAY 1440 /* 0x8C38C: the double 0x3F46C16C16C16C17 */
#define PUFO_SMAP_CROSSHAIR_COOLDOWN       5 /* 0x8C6C4: CMP R2, #5 */
#define PUFO_SMAP_CROSSHAIR_INDEX         50 /* 0x8C6A0 / 0x8C6F8: the index and low byte */
#define PUFO_SMAP_CROSSHAIR_MID          200 /* 0x8C6A4 / 0x8C6E4: the mid byte */
#define PUFO_SMAP_CROSSHAIR_SHIFT          3 /* 0x8C6F4 / 0x8C71C: ADD Rn, R0, #3 on both axes */

typedef struct GeoscapeMapViewEnv {
    SmapView       *pView;          /* gSmapCam @ 0x1863E8, wrapped in place */
    PadImage       *pMapPad;        /* *pSmapPadCur @ 0x186E40 */
    PadImage       *pLayerPad;      /* *pSmapLayerPad @ 0x186BE0 */
    const u8       *pWorldBits;     /* *pSmapWorldBits @ 0x186BC0, the glyph record */
    const u32      *pMapPal;        /* aPalette slot 29, reached as aPalette + 0x7400 */
    const GameTime *pTime;          /* the clock block @ 0x159608: nGameHour @ 0x159630, nGameMinute @ 0x159634 */
    i32             nScreenId;      /* nGeoscapeScreen @ 0x186BC4 */
    i32            *pnOverlayOriginX;/* nSmapOverlayOriginX @ 0x186C20 */
} GeoscapeMapViewEnv;

/* The crosshair tail, 0x8C69C..0x8C760. While bSmapCrosshairPending is set and nRadarCooldownR
 * is below 5 it blits a 1-pixel vertical line at x = nSmapCrosshairX + 3 whose height is
 * 320 - pGeoscapeStripPad->uHeight, and a 240 x 1 horizontal line at y = nSmapCrosshairY + 3, both
 * in the 50/200 pair. Otherwise it clears the flag and parks the cooldown at 0xFF. */
typedef struct GeoscapeCrosshairEnv {
    const PadImage *pChromePad;     /* *pGeoscapeStripPad @ 0x186BBC, its uHeight bounds the line */
    i32             nCrosshairX;    /* nSmapCrosshairX @ 0x186BC8 */
    i32             nCrosshairY;    /* nSmapCrosshairY @ 0x186BCC */
    u8             *pbPending;      /* bSmapCrosshairPending @ 0x1870B0 */
    i32            *pnCooldownR;    /* nRadarCooldownR @ 0xAC218 */
} GeoscapeCrosshairEnv;

void Geoscape_BlitMapView(GeoscapeMapViewEnv *pEnv);
void Geoscape_BlitCrosshair(const GeoscapeCrosshairEnv *pEnv);

/* ---- Geoscape_Draw @ 0x8c180 status panel (0x8C760..0x8E658) -------------------------
 *
 * The panel the map view draws under the crosshair. Line 0 of the root message list @ 0x158890
 * decides everything: its flag byte is the case, its colour dwords are the arguments.
 *
 * 1. The head (0x8C760..0x8C82C). Gfx_PackRgb565Pair(0xDC,0x50,0x50 / 50,0x1E,0x1E): the second
 *    triple's red is R5, which still holds the crosshair's 50 loaded at 0x8C6A0. The flag is read
 *    three times and `<= 0x1E` (0x8C79C) and `>= 0x32` (0x8C7B0) both fall to the default arm.
 *    The four colour dwords of line 0 are read in slot order 3,2,1,0 (0x8C7C0..0x8C804) and
 *    BaseFac_BuildStatusMsg @ 0x6a4bc runs with a3 = slot 0, a4 = slot 1, a5 = slot 2; slot 3 is
 *    pushed at 0x8C814 and the callee never reads it. The callee's prologue (0x6A4C0..0x6A4D0 maps
 *    only R0..R3) and its use of a3 as the 64-byte base-record index at 0x6A558 fix that mapping;
 *    the IDB call-site comment had it shifted one slot right.
 *
 * 2. The thirteen arms (0x8C844, `SUB R0,#0x20 / CMP #0xC` on the same flag byte). Every arm runs
 *    Ui_AnimExpandBlit_IfEnabled(0, 0x6E, 0xEC, nH, 1) -- nH 0x68 for ' ' and '$', 0x5A for the
 *    rest, and 0x14/0xBE for '%' -- copies one TEXTS record into the original's stack buffer,
 *    draws the strings it wants centred on x=120, sets the two bottom controls, then runs the
 *    shared footer: TEXTS[1193] "OK" centred on x=28 and TEXTS[1194] centred on x=209, both at the
 *    arm's footer y (0xC0 for ' ' and '$', 0xB2 for the rest, 0xBC for '%'), with
 *    Gfx_PackRgb565Pair(0x44,0xF4,0xFC / 4,0x46,0x4B) packed before the first of the two.
 *
 *    Which strings an arm draws, and in what order:
 *      ' '  TEXTS[1191]@152, TEXTS[1192]@166, pszMsg@124, pszExtra@138   controls y 0xBD, foot 0xC0
 *      '!'  pszMsg@124, pszExtra@138, TEXTS[1195]@152
 *      '"'  TEXTS[1196], colour packed FIRST, then pszMsg@124, pszExtra@138, TEXTS[1196]@152
 *      '#'  pszMsg@124, pszExtra@138, TEXTS[1197]@152
 *      '$'  TEXTS[1198]@152, TEXTS[1199]@166, pszMsg@124                 controls y 0xBD, foot 0xC0
 *      '%'  see 3.
 *      '&'  identical to '"' (the same TEXTS record 1196)
 *      '\'' pszExtra@124, pszScratch@138, TEXTS[1204]@152
 *      '('  pszMsg@124, pszScratch@138, TEXTS[1205]@152
 *      ')'  TEXTS[1206], colour packed FIRST, then pszMsg@124, TEXTS[1206]@152 -- no 138 line
 *      '*'  pszMsg@124, TEXTS[1207]@152                                  -- no 138 line
 *      '+'  pszMsg@124, pszExtra@138, TEXTS[1208]@152
 *      ','  pszExtra@124, TEXTS[1209]@152                                -- no 124 line from pszMsg
 *
 *    The colour order is observable: an arm that packs before drawing renders its main lines in
 *    0x44,0xF4,0xFC, and every other arm renders them in the head pair 0xDC,0x50,0x50. Only '"',
 *    '&', ')' and '%' pack early; '"', '&' and ')' pack a second time after the controls.
 *
 * 3. The '%' arm (0x8D2F4..0x8D838), the research panel. TEXTS[1200] at y=26 and pszMsg at y=44.
 *    Then the confirm control (pGeoscapeCtlConfirm @ 0x186BA4) is parked at (1000,1000); when
 *    anUfopaediaEntryNum[nResearchTopicShown] is non-zero it is centred on 120 at y=75, gets
 *    Ui_OpenBaseInfoAlt @ 0x84d18 as its handler, is blitted, and TEXTS[1201] "Отчет" is drawn at
 *    y=75. Then the finished-topic walk over aResearchTopics[nResearchFinishedTopic]
 *    .abActivateTopics[k], k = 0.. while the byte is non-zero: a topic whose byte_160BBC entry is
 *    below 2 gets TEXTS[1202] at y=111 and TEXTS[aTopics[id].wGate4] at y = 127 + 14k, and its
 *    byte_160BBC entry is set to 1. If at least one topic was drawn, the apply control
 *    (pGeoscapeCtlApply @ 0x186BAC) is centred at y = 142 + 14k with Ui_OpenResearch @ 0x84bf8,
 *    blitted, and TEXTS[1203] drawn at the same y. Controls at y=0xB9, footer at 0xBC.
 *
 * 4. The default arm (0x8E5A4). Pack the status pair; if byte_18709C[4] @ 0x1870A0 is zero skip
 *    EVERYTHING -- 0x8E5E0's BEQ lands at 0x8E658, past both calls. Otherwise
 *    Ui_AnimExpandBlit(x 0xE, nW 0xD0, nH = nFont * nLineCount + 6, y = nPosY - 6, idx 0,
 *    src 0x28/0x32, key 0xB4, mode 0) and then UiPanelMsg_Draw on pStatusPanel @ 0x18641C.
 *
 * Deviations. (a) The original's walk counter is the spilled global dword_186B08 and its
 * "something was drawn" flag is an uninitialised stack byte (var_1A4 is written only in the drawn
 * branch); the port uses a local counter and starts the flag at zero. (b) byte_160BBC and
 * aTopics[] are indexed by the walk's topic byte with no bound; the port stops at
 * PUFO_RESEARCH_TOPIC_COUNT. (c) The original reaches pStatusPanel and the two controls through
 * globals that are always live here; the port returns without drawing when one is missing. */
#define PUFO_STATUS_TEXT_X_CENTRE   120 /* 0x78: the centred column for every main line */
#define PUFO_STATUS_TEXT_X_LEFT      28 /* 0x1C: the footer's left label */
#define PUFO_STATUS_TEXT_X_RIGHT    0xD1 /* 0x8E3D8: the footer's right label */
#define PUFO_STATUS_ANIM_Y           0x6E /* 0x8C894: every arm but '%' */
#define PUFO_STATUS_ANIM_W           0xEC /* 0x8C898 */
#define PUFO_STATUS_ANIM_H_TALL      0x68 /* 0x8C888: ' ' and '$' */
#define PUFO_STATUS_ANIM_H_STD       0x5A /* 0x8CAEC: the ten plain arms */
#define PUFO_STATUS_ANIM_Y_RESEARCH  0x14 /* 0x8D300 */
#define PUFO_STATUS_ANIM_H_RESEARCH  0xBE /* 0x8D2F4 */
#define PUFO_STATUS_Y_MSG            0x7C /* 0x8C978: pszMsg, szRadarMsg @ 0x186B10 */
#define PUFO_STATUS_Y_EXTRA          0x8A /* 0x8C9A8: pszExtra, unk_186420 */
#define PUFO_STATUS_Y_TEXT           0x98 /* 0x8C8F0: the arm's own TEXTS record */
#define PUFO_STATUS_Y_TEXT2          0xA6 /* 0x8C948: the second record, ' ' and '$' only */
#define PUFO_STATUS_Y_FOOT_STD       0xB2 /* 0x8E3C4: the shared tail's own y */
#define PUFO_STATUS_Y_FOOT_TALL      0xC0 /* 0x8D2E8: ' ' and '$' */
#define PUFO_STATUS_Y_FOOT_RESEARCH  0xBC /* 0x8d6e0-ish: '%' */
#define PUFO_STATUS_Y_RESEARCH_TITLE  0x1A /* 0x8D38C */
#define PUFO_STATUS_Y_RESEARCH_MSG    0x2C /* 0x8D3BC */
#define PUFO_STATUS_Y_RESEARCH_REPORT 0x4B /* 0x8D478 */
#define PUFO_STATUS_Y_RESEARCH_HEAD   0x6F /* 0x8D51C: TEXTS[1202] */
#define PUFO_STATUS_Y_RESEARCH_ROW    0x7F /* 0x8D5D0: + 14 * k */
#define PUFO_STATUS_Y_RESEARCH_APPLY  0x8E /* 0x8D670 / 0x8D6E0: + 14 * k */
#define PUFO_STATUS_RESEARCH_ROW_STEP  14 /* 0x8D5B4 / 0x8D648: MOV R2,#0xE */
#define PUFO_STATUS_CTL_X_SFX           9 /* 0x8C9BC: *unk_18704C */
#define PUFO_STATUS_CTL_X_OPEN        0xBE /* 0x8C9E4: *unk_187024 */
#define PUFO_STATUS_CTL_Y_TALL       0xBD /* 0x8C9CC: ' ' and '$' */
#define PUFO_STATUS_CTL_Y_STD        0xAF /* 0x8CBDC: the ten plain arms */
#define PUFO_STATUS_CTL_Y_RESEARCH   0xB9 /* 0x8d7xx: '%' */
#define PUFO_STATUS_CTL_PARKED       1000 /* 0x8D3D0: the confirm/apply park value */
#define PUFO_STATUS_FLAG_MIN         0x1F /* 0x8C798: CMP #0x1E / BLE sends 0x1E and below away */
#define PUFO_STATUS_FLAG_MAX         0x31 /* 0x8C7AC: CMP #0x32 / BGE sends 0x32 and above away */
#define PUFO_STATUS_TEXT_OK          1193 /* 0xEF425 "OK", the footer's left label */
#define PUFO_STATUS_TEXT_OK_RIGHT    1194 /* 0xEF4F2 "OKмдл", the footer's right label */
#define PUFO_STATUS_TEXT_STORAGE     1191 /* 0xEF28B "Нет места в хранилищах" No room in storage */
#define PUFO_STATUS_TEXT_PRODUCTION  1192 /* 0xEF358 "Ост. производство" Remaining production */
#define PUFO_STATUS_TEXT_MATERIALS   1195 /* 0xEF5BF "Нет специальных материалов" */
#define PUFO_STATUS_TEXT_DONE        1196 /* 0xEF68C "Выполнено" Completed, shared by '"' and '&' */
#define PUFO_STATUS_TEXT_MONEY       1197 /* 0xEF759 "Нет денег для производства" */
#define PUFO_STATUS_TEXT_NOWHERE     1198 /* 0xEF826 "Некуда поместить пришельца" */
#define PUFO_STATUS_TEXT_DIED        1199 /* 0xEF8F3 "Несколько пришельцев погибло" */
#define PUFO_STATUS_TEXT_RESEARCH_DONE 1200 /* 0xEF9C0 "Исследование завершено" */
#define PUFO_STATUS_TEXT_REPORT      1201 /* 0xEFA8D "Отчет" Report */
#define PUFO_STATUS_TEXT_RESEARCH_NEW 1202 /* 0xEFB5A "Доступны ноые исследования: " (sic) */
#define PUFO_STATUS_TEXT_RESEARCH    1203 /* 0xEFC27 "Исследование" */
#define PUFO_STATUS_TEXT_EQUIP_CREW  1204 /* 0xEFCF4 "Нет оборуд. для снаряж. команды" */
#define PUFO_STATUS_TEXT_EQUIP_CRAFT 1205 /* 0xEFDC1 "Нет оборуд. для снаряж. корабля" */
#define PUFO_STATUS_TEXT_ARRIVED     1206 /* 0xEFE8E "Прибыли новые вещи или персонал" */
#define PUFO_STATUS_TEXT_STORAGE2    1207 /* 0xEFF5B "Нет места в хранилищах", as 1191 */
#define PUFO_STATUS_TEXT_ELERIUM     1208 /* 0xF0028 "Нет элериума для заправки" */
#define PUFO_STATUS_TEXT_DESTROYED   1209 /* 0xF00F5 "Корабль уничтожен" */

/* BaseFacStatusEnv and ResearchTopicLive live in game/pufo_baseui.h, which includes this header,
 * so they stay incomplete here and are only dereferenced in the .c. */
typedef struct GeoscapeStatusPanelEnv {
    struct BaseFacStatusEnv *pFac;            /* the BuildStatusMsg call at 0x8C82C, wired by the caller */
    UiLineList        *pLines;            /* the root message list @ 0x158890 */
    const char        *pTextRecs;         /* aTextRecs @ 0xB38D0, stride PUFO_TEXT_REC_STRIDE */
    UiAnimBlitEnv     *pAnim;             /* the geoscape anim root @ 0x1864F8 */
    GfxColorEnv       *pColor;            /* Gfx_PackRgb565Pair's gamma and fade pair */
    UiControl         *pCtlSfx;           /* *unk_18704C, the left bottom control */
    UiControl         *pCtlOpen;          /* *unk_187024, the right bottom control */
    UiControl         *pCtlConfirm;       /* *pGeoscapeCtlConfirm @ 0x186BA4, '%' only */
    UiControl         *pCtlApply;         /* *pGeoscapeCtlApply @ 0x186BAC, '%' only */
    UiPanel           *pStatusPanel;      /* pStatusPanel @ 0x18641C, the default arm's panel */
    const u8          *pbMultiSelectPanel;/* byte_18709C[4] @ 0x1870A0, the default arm's gate */
    void             (*pfnCtlSfx)(void);        /* Geoscape_PrepScreenOpenSfx @ 0x852b8 */
    void             (*pfnCtlOpen)(void);       /* Geoscape_PrepScreenOpen @ 0x85194 */
    void             (*pfnConfirmReport)(void); /* Ui_OpenBaseInfoAlt @ 0x84d18 */
    void             (*pfnApplyResearch)(void); /* Ui_OpenResearch @ 0x84bf8 */
    UiControlBlitFn    pfnCtlBlit;
    UiControlBlitFn    pfnCtlBlitEx;
    void              *pCtlBlitUser;
    struct ResearchTopicLive *aTopics;    /* aResearchTopics @ 0x15EF08, '%' walk */
    u8                *abResearchActive;  /* byte_160BBC, 144 bytes, read AND written by '%' */
    const u8          *pbFinishedTopic;   /* nResearchFinishedTopic @ 0x15EEF8 */
    const i32         *pnShownTopic;      /* nResearchTopicShown @ 0x160C60 */
    /* The default arm's continuation, 0x8E658..0x8F24C. */
    const SmapView    *pView;             /* gSmapCam @ 0x1863E8; its nMapW / 2 - 120 is R8 */
    PtrArray          *pStrategList;      /* the 0x1864F8 root's PtrArray at +0x3C */
    const i32         *pnActionFail;      /* dwActionFail @ 0x1870C8 */
    const i32         *pnActionFailReason;/* dwActionFailReason @ 0x1870B4 */
    const u8          *pbMapSelectionFlag;/* bMapSelectionFlag @ 0x159E84 */
    char              *pszRadarMsg;       /* szRadarMsg @ 0x186B10, the original's dword_186B08+8 */
    char              *pszHyper;          /* unk_186420, the hyperwave panel's own line buffer */
    UiControl         *pCtlA;             /* *pGeoscapeCtlA @ 0x186BB4 */
    UiControl         *pCtlB;             /* *pGeoscapeCtlB @ 0x186BB0, the 0x0C arm's second control */
    void             (*pfnSelectTarget)(void);  /* Smap_OnPointerUp @ 0x84688 (0x89450 is Geoscape_OnPointerUp) */
    void             (*pfnClearSelection)(void);/* Smap_ClearSelection @ 0x84648 */
    /* The order questions, 0x8F250..0x8F9E0. */
    Ship *const       *apCraft;           /* Map.apCraft @ root + 0x10AC */
    BaseSiteRec       *apSiteRecs;        /* aSiteRecs @ 0x186C40, 64-byte records, name at +0x1A */
    i32              *pnSelectedShipIdx;  /* 0x187054: read by 0x0F and 0x0E, WRITTEN by 0x0B at 0x8FA90 */
    void             (*pfnResolveInterceptUI)(void);  /* Smap_ResolveInterceptUI @ 0x84964 */
    void             (*pfnDismissSelected)(void);     /* Smap_DismissSelected @ 0x84428 */
    void             (*pfnConfirmCraftTarget)(void);  /* Smap_ConfirmCraftTarget @ 0x842ec */
    void             (*pfnConfirmSelectedAlt)(void);  /* Smap_ConfirmSelectedAlt @ 0x853e4 */
    void             (*pfnSetWaypoint)(void);         /* Smap_SetWaypoint @ 0x85440 */
    /* The 0x0B craft-detail card. */
    const PadImage    *pStripPad;         /* *pGeoscapeStripPad @ 0x186BBC, re-blitted behind the card */
    u8                *pbInterceptDone;   /* bInterceptDone @ 0x18709C, byte 0 of the block the default
                                           * arm reads at +4 */
    UiControl         *pCtlReturnBase;    /* *pCtlNameEditBackspace @ 0x186BA8, labelled "Вернуться на базу" */
    UiControl         *pCtlNewTarget;     /* *pCtlNameEditAccept @ 0x186B9C, labelled "Выбрать нов. цель" */
    UiControl         *pCtlPatrol;        /* *unk_186B98, made in Geoscape_InitUI, labelled "Патруль" */
    void             (*pfnSelectMapTarget)(void);     /* Smap_SelectMapTarget @ 0x84484 */
    void             (*pfnConfirmSelected)(void);     /* Smap_ConfirmSelected @ 0x845d8 */
    /* The 0x0D trace-lost panel. */
    struct GameTime   *pTime;             /* 0x159618: GameTime_Pause writes bPaused here */
    /* The 0x10 attacked-city panel. */
    MissionSiteRec    *paMissionSites;    /* aMissionSites @ 0x1865C0 (root + 0xC8), 56 records */
    /* The 0x1B intercept list. */
    UiControl   *const *apRootCtl;        /* the root block's thirteen widgets @ 0x1864F8 + 4*i */
    PadImage    *const *apClockDigitPads; /* apClockDigitPads @ 0x186BF0, the ten "b0".."b9" pads */
    u8                *pbUfoCrashPanel;   /* bSmapUfoCrashPanel @ 0x18708C */
    u8                *pbSitePanel;       /* bSmapSitePanel @ 0x187090 */
    u8                *pbAlienBasePanel;  /* bSmapAlienBasePanel @ 0x187098 */
    UiControl         *pFinalMissionCtl;  /* pFinalMissionCtl @ 0x186B8C */
    u8                *pbFinalMissionCtl; /* bFinalMissionCtlArmed @ 0x187068 */
    /* The 0x14 map-target panel. */
    void             (*pfnCancelTarget)(void);      /* Smap_CancelTarget @ 0x84254 */
    /* The 0x17 descent panel. */
    Soldier *const   *apSoldiers;         /* the roster the original counts at 0x1139B8 -- Craft_CountSoldiersAboard
                                           * loads it itself, so the caller's first argument (abFacilityGrid at
                                           * 0x915E8) is dead */
    const u16        *pnSoldierCount;     /* nSoldierCount @ 0x115350, read as a u16 at 0x664B0 */
    void             (*pfnBeginCraftSelect)(void);  /* Smap_BeginCraftSelect @ 0x838dc */
    /* The chain's shared tail, 0x91CEC..0x91DB0. Its second branch needs the globe-hover env, whose owners
     * (the zoom tile grid at [0x186B04] and the three sweep counters at 0x159DF8 / 0x159DFC / 0x159E00) are
     * not owned by the shell yet -- Geoscape_DrawStatusPanel itself has no caller in the port. */
    const i32                    *pnScreenId;        /* nGeoscapeScreen @ 0x186BC4: 2 picks the hover branch */
    const struct GeoscapeGlobeHoverEnv *pGlobeEnv;   /* the env the 0x91D8C call gets: the caller sets R0 =
                                                      * abFacilityGrid @ 0x159768 (the callee keeps it and reads
                                                      * it at 0x67860), R1 = nSmapCursorX, R2 = nSmapCursorY, and
                                                      * the last two are dead -- see StatusPanel_DrawChainTail */
    u8                           *pbPendingRegionId; /* bPendingRegionId @ 0x159DF4, written with STRB at 0x91D94 */
    const u8                     *pbPlacementPending;/* bPlacementPending @ 0x1870AC: non-zero skips the close blit,
                                                      * and the globe hover reads the same byte at 0x679A8 */
    UiControl                    *pCloseCtl;         /* *g_pUiCloseCtl @ 0x186B00 */
} GeoscapeStatusPanelEnv;

void Geoscape_DrawStatusPanel(const GeoscapeStatusPanelEnv *pEnv);

/* ---- the default arm's continuation, 0x8E658..0x8F24C ------------------------------------
 *
 * The default arm does not end at UiPanelMsg_Draw. 0x8E5E0's BEQ and the 0x8E654 call both land
 * at 0x8E658, so this runs for every flag that reaches the default arm -- including 0x1E, 0x1F and
 * 0x31, and including the case where the panel-draw gate was closed.
 *
 * 0x8E658, the action-fail line. Read as an int, dwActionFail == 1 AND line 0's flag < 0x1E AND
 * dwActionFailReason == 0, then Ui_AnimExpandBlit_IfEnabled(x 0x14, y 0x82, 0xC8 x nMapW/2 - 120,
 * bEnabled = the SIGNED BYTE of dwActionFail -- 1, because the outer test just proved it) and
 * TEXTS[406] "Нет денег" centred at y=158.
 *
 * 0x8E6E0 re-reads line 0's flag a third time. Only 0x0A is ported here; 0x11, 0x0F, 0x0C, 0x0E,
 * 0x0B and the rest of the chain at 0x8F250..0x91CEC are the next unit.
 *
 * 0x8E708, the selected contact panel. Line 0's colour slot 0 indexes the 0x1864F8+0x3C PtrArray
 * (the strateg list) and the entry is a StrategItem. Line 0's colour slot 1 indexes the SAME list
 * again for the size line's kind byte -- the two slots are read separately and each call
 * re-resolves the entry.
 *
 *   bHyperDetect (+0x64) == 0: anim (0xE, 0x46) 0xD0 x nMapW/2 - 120, else anim (0, 0x32) 0xEC x 0xB4.
 *   psz = TEXTS[1210] "НЛО" + itoa(nDisplayId @ +0x10), centred at y=80.
 *   szRadarMsg = TEXTS[1211] "Размер..................", then strcat one of the five size labels
 *   picked by the SECOND entry's bCraftClass (+0x02, 0x8E8DC LDRSB [R0,#2] -- NOT nType), each
 *   prefixed by itoa(abs(base - 61 * bRandA)) where bRandA (+0x14) comes from the FIRST entry.
 *   bases 0..7: 2200, 2400, 2700, 4000, 4300, 4800, 3200, 5000; labels 1359, 1360, 1360, 1361,
 *   1361, 1362, 1362, 1363. Outside 0..7 the switch jumps to 0x8EBB8, past the strcat, so psz keeps
 *   TEXTS[1211] and szRadarMsg keeps the label alone; the speed line then prints that label. Drawn
 *   left-aligned at (40, 96).
 *   szRadarMsg = TEXTS[1364] "Скорость..............", then "0" only when nTimer (+0x2C) > 0, then
 *   TEXTS[1365] " узлов". Drawn at (40, 112).
 *   Controls: confirm gets Smap_OnPointerUp and pGeoscapeCtlA gets Smap_ClearSelection before the
 *   branch; with bHyperDetect clear they sit centred on 120 at y 136 and 160, otherwise at 180 and
 *   204. The label under the confirm control is TEXTS[1212] "Атака НЛО!" when bMapSelectionFlag is
 *   set and TEXTS[1213] "Центр на НЛО" when it is not, centred at 136 or 180; TEXTS[1144] "Отмена"
 *   is centred at 161 or 205.
 *   With bHyperDetect set (0x8ECAC LDRB [R0,#0x64]) three more lines are built in unk_186420
 *   instead, all three from the FIRST entry: TEXTS[1345] alone at (120, 60); TEXTS[1346] +
 *   TEXTS[1366..1373] picked by bCraftClass (0x8EE98 LDRSB [R0,#2]) at (40, 128); TEXTS[1347] +
 *   TEXTS[1374..1378] picked by bRace (+0x70), with NO label for a race outside 0..4, at (40, 144);
 *   and TEXTS[1348] + TEXTS[1379..1386] picked by nMissionSub (+0x39) through the non-monotonic
 *   map 0->1379, 1->1380, 2->1381, 3->1382, 4->1383, 5->1386, 6->1384, 7->1385, again with no label
 *   outside 0..7, at (40, 160).
 */
#define PUFO_STATUS_TEXT_NO_MONEY      406 /* "Нет денег" */
#define PUFO_STATUS_TEXT_CANCEL        1144 /* "Отмена" */
#define PUFO_STATUS_TEXT_UFO           1210 /* "НЛО" */
#define PUFO_STATUS_TEXT_SIZE_LABEL    1211 /* "Размер.................." */
#define PUFO_STATUS_TEXT_ATTACK        1212 /* "Атака НЛО!" */
#define PUFO_STATUS_TEXT_CENTRE        1213 /* "Центр на НЛО" */
#define PUFO_STATUS_TEXT_HYPERWAVE     1345 /* "Получена гиперволновая рассылка" */
#define PUFO_STATUS_TEXT_TYPE_LABEL    1346 /* "Тип НЛО.............." */
#define PUFO_STATUS_TEXT_RACE_LABEL    1347 /* "Раса......................." */
#define PUFO_STATUS_TEXT_MISSION_LABEL 1348 /* "Миссия.................." */
#define PUFO_STATUS_TEXT_SIZE_FIRST    1359 /* 1359..1363 size labels */
#define PUFO_STATUS_TEXT_SPEED_LABEL   1364 /* "Скорость.............." */
#define PUFO_STATUS_TEXT_KNOTS         1365 /* " узлов" */
#define PUFO_STATUS_TEXT_TYPE_FIRST    1366 /* 1366..1373 craft-type labels */
#define PUFO_STATUS_TEXT_RACE_FIRST    1374 /* 1374..1378 race labels */
#define PUFO_STATUS_TEXT_MISSION_FIRST 1379 /* 1379..1386 mission labels */
#define PUFO_STATUS_SIZE_BASE_STEP        61 /* 0x8E920 MOV R2, #0x3D */
#define PUFO_STATUS_TEXT_X_LEFT_PANEL   0x28 /* 40: the three left-aligned info lines */
#define PUFO_STATUS_Y_FAIL_TEXT         0x9E /* 0x8E6D0: TEXTS[406] */
#define PUFO_STATUS_Y_SEL_TITLE          0x50 /* 0x8E6E4-side: "НЛО" + id */
#define PUFO_STATUS_Y_SEL_SIZE           0x60 /* 0x8EBC0 */
#define PUFO_STATUS_Y_SEL_SPEED          0x70 /* 0x8EC5C */
#define PUFO_STATUS_Y_SEL_HYPER          0x3C /* 0x8EE44 */
#define PUFO_STATUS_Y_SEL_TYPE           0x80 /* 0x8EF3C */
#define PUFO_STATUS_Y_SEL_RACE           0x90 /* 0x8F008 */
#define PUFO_STATUS_Y_SEL_MISSION        0xA0 /* 0x8F0FC */
#define PUFO_STATUS_SELCTL_X              0x28 /* 40, the left-aligned control anchor */
#define PUFO_STATUS_SELCTL_Y_STD_CONF   0x88 /* 0x8ECCC: confirm, bHyperDetect clear */
#define PUFO_STATUS_SELCTL_Y_STD_CLEAR  0xA0 /* 0x8ECF4: pGeoscapeCtlA */
#define PUFO_STATUS_SELCTL_Y_HYP_CONF   0xB4 /* 0x8F124 */
#define PUFO_STATUS_SELCTL_Y_HYP_CLEAR  0xCC /* 0x8F150 */
#define PUFO_STATUS_Y_SEL_LABEL_STD      0x88 /* 0x8EDAC: the attack/centre label */
#define PUFO_STATUS_Y_SEL_LABEL_HYP      0xB4 /* 0x8F1E8 */
#define PUFO_STATUS_Y_SEL_CANCEL_STD     0xA1 /* 0x8EDF4 */
#define PUFO_STATUS_Y_SEL_CANCEL_HYP     0xCD /* 0x8F230 */
#define PUFO_STATUS_SELFLAG              0x0A /* the flag this panel answers */

/* ---- the default arm's order questions, 0x8F250..0x8F9E0 ------------------------------
 *
 * The flag chain at 0x8E6E0 continues past the contact panel with four tests on line 0's flag, each
 * re-read through UiLineList_GetFlag: 0x11 at 0x8F270, 0x0F at 0x8F41C, 0x0C at 0x8F5B4, 0x0E at
 * 0x8F768. They are independent tests, not a switch -- an arm's body falls straight into the next
 * test -- so exactly one of them runs for any one flag value. None of them packs a colour: they draw
 * in the status pair the default arm packed at 0x8E5CC.
 *
 * All four start with Ui_AnimExpandBlit at (0xE, 0x3C) size (0xD0, nH) with src (0x28, 0x32), key
 * 0xB4, nIdx 0 and nMode 0, nH being 0x5A for 0x11, 0x0F and 0x0C and 0x78 for 0x0E. All four end
 * with a centred TEXTS[1127] "ОК" under the control they arm -- aTextOk @ 0xEBF4B, CYRILLIC О and К,
 * not the Latin TEXTS[1193] "OK" the 13-arm footer uses -- and 0x0C adds a centred TEXTS[1144]
 * "Отмена" under its second control.
 *
 * 0x11, 0x8F270: TEXTS[1214] "База: " with the base name at aSiteRecs[slot 0] + 0x1A appended
 * (0x8F2E8, the colour value IS the 64-byte record index, unbounded) centred at y=70, TEXTS[1215]
 * "База потеряна" centred at y=90, and pGeoscapeCtlA centred at y=115 with Smap_ResolveInterceptUI,
 * "OK" at y=116.
 * 0x0F, 0x8F41C: TEXTS[1216] "Корабль: " with Ship_FormatDisplayName(0x187054) appended centred at
 * y=70, TEXTS[1217] "Мало топлива, иду на базу" at y=90, pGeoscapeCtlA at y=115 with
 * Smap_DismissSelected, "OK" at y=116.
 * 0x0C, 0x8F5B4: TEXTS[1218] "Лететь к выбранной точке" centred at y=70; pGeoscapeCtlA centred at
 * y=100 with Smap_ConfirmCraftTarget and pGeoscapeCtlB centred at y=124 with Smap_ResolveInterceptUI,
 * both blitted, "OK" centred at y=101 and TEXTS[1144] centred at y=125.
 * 0x0E, 0x8F768: TEXTS[1216] with Ship_FormatDisplayName(colour slot 0 -- the colour value IS the
 * ship index here, 0x8F7E0, and NOT 0x187054 as in the 0x0F arm) centred at y=70, TEXTS[1219]
 * "Выбранная точка достигнута" at y=86, TEXTS[1220] "Новое патрулирование?" at y=102, pGeoscapeCtlA
 * centred at y=124 and pGeoscapeCtlConfirm centred at y=150 blitted BEFORE their handlers are set
 * (0x8F91C/0x8F924 then 0x8F934/0x8F940): Smap_ConfirmSelectedAlt and Smap_SetWaypoint; "OK" centred
 * at y=125 and TEXTS[1221] "Перенапр. корабль" centred at y=150.
 */
#define PUFO_STATUS_FLAG_BASE_LOST    0x11 /* 0x8F270: the base the message names is gone */
#define PUFO_STATUS_FLAG_FUEL_LOW     0x0F /* 0x8F41C: a craft is short of range and heading home */
#define PUFO_STATUS_FLAG_FLY_TO_POINT 0x0C /* 0x8F5B4: "fly to the chosen point?" */
#define PUFO_STATUS_FLAG_POINT_REACHED 0x0E /* 0x8F768: point reached, patrol again? */
#define PUFO_STATUS_TEXT_BASE_LABEL     1214 /* "База: " */
#define PUFO_STATUS_TEXT_BASE_LOST      1215 /* "База потеряна" */
#define PUFO_STATUS_TEXT_SHIP_LABEL     1216 /* "Корабль: " */
#define PUFO_STATUS_TEXT_FUEL_LOW       1217 /* "Мало топлива, иду на базу" */
#define PUFO_STATUS_TEXT_FLY_TO_POINT   1218 /* "Лететь к выбранной точке" */
#define PUFO_STATUS_TEXT_POINT_REACHED  1219 /* "Выбранная точка достигнута" */
#define PUFO_STATUS_TEXT_NEW_PATROL     1220 /* "Новое патрулирование?" */
#define PUFO_STATUS_TEXT_RETARGET       1221 /* "Перенапр. корабль" */
#define PUFO_STATUS_TEXT_ORDER_OK       1127 /* aTextOk @ 0xEBF4B "ОК", Cyrillic -- not TEXTS[1193] */
#define PUFO_STATUS_RADAR_MSG_BYTES     108  /* szRadarMsg @ 0x186B10, 108 bytes up to nSmapTapX @ 0x186B7C */
#define PUFO_STATUS_ORDER_NH_SHORT       0x5A /* 0x8F264 MOV R8, #0x5A: the anim height of 0x11/0x0F/0x0C */
#define PUFO_STATUS_ORDER_NH_TALL        0x78 /* 0x8F78C MOV R3, #0x78: the 0x0E anim height */
#define PUFO_STATUS_Y_ORDER_LINE1        0x46 /* 0x8F318 MOV R2, #0x46: the "База:/Корабль:" line, y 70 */
#define PUFO_STATUS_Y_ORDER_LINE2        0x5A /* 0x8F370 MOV R2, #0x5A: the question line, y 90 */
#define PUFO_STATUS_Y_ORDER_0E_LINE2     0x56 /* 0x8F86C: "Выбранная точка достигнута", y 86 */
#define PUFO_STATUS_Y_ORDER_0E_LINE3     0x66 /* 0x8F8C4: "Новое патрулирование?", y 102 */
#define PUFO_STATUS_ORDERCTL_Y_ONE       0x73 /* 0x8F26C MOV R6, #0x73: the single control of 0x11/0x0F */
#define PUFO_STATUS_Y_ORDER_OK_ONE       0x74 /* 0x8F3F4 MOV R2, #0x74: its "OK" */
#define PUFO_STATUS_ORDERCTL_Y_TOP       0x64 /* 0x8F678 MOV R3, #0x64: the 0x0C upper control */
#define PUFO_STATUS_Y_ORDER_OK_TOP       0x65 /* 0x8F6F4 MOV R2, #0x65: its "OK" */
#define PUFO_STATUS_ORDERCTL_Y_BOTTOM    0x7C /* 0x8F5B0 MOV R6, #0x7C: the 0x0C lower control and 0x0E's */
#define PUFO_STATUS_Y_ORDER_CANCEL       0x7D /* 0x8F74C / 0x8F980 MOV R2, #0x7D: "Отмена" and 0x0E's "OK" */
#define PUFO_STATUS_ORDERCTL_Y_0E_CONF   0x96 /* 0x8F904 MOV R3, #0x96: the 0x0E confirm control */
#define PUFO_STATUS_Y_ORDER_RETARGET     0x96 /* 0x8F9D4 MOV R2, #0x96: its label */

/* ---- the selected craft's detail card, 0x8F9E4..0x9021C -------------------------------
 *
 * Flag 0x0B draws the card of the craft the message names. It is the only arm that WRITES the
 * selected-ship global: 0x8FA90 stores line 0's colour slot 0 into 0x187054 and every read after
 * that goes through the global, so the card's own craft is the colour value, not whatever the click
 * left there. Before anything else it re-blits the zoom-strip pad @ 0x186BBC at its own origin with
 * `SoftSurf_BlitSrcRect(pad.x, pad.y, pad.w, pad.h, 0x3C, 0x78, 0x82, 0x96)` (0x8F9F8..0x8FA30) and
 * then opens the anim frame at (0xE, 5) size (0xD0, 0xDC) (0x8FA34..0x8FA70).
 *
 * Four centred lines, then the weapon loop:
 *   TEXTS[1216] "Корабль: " + Ship_FormatDisplayName(0x187054) at y=15
 *   TEXTS[1214] "База: "    + aSiteRecs[Ship.bHomeBase @ +3].abName at y=30
 *   TEXTS[1222] "Скорость: " + itoa(Ship.uFuelCap @ +0x10 as u16) at y=45 -- this is the field the
 *       radar also calls the craft's speed (uFuelCap / 60), printed raw here
 *   TEXTS[1223] "Топливо: "  + itoa(100 * uRangeRemain @ +8 / uRangeMax @ +0xA) + "%" at y=60, the
 *       same _rt_sdiv shape as BasePlaneScreen_DrawCraftDetail's fuel line: the divisor is in R0,
 *       there is no zero test, and the RANGE pair is labelled as fuel
 *   for slot 0 and 1: TEXTS[1224] "Оружие 1: " / TEXTS[1225] "Оружие 2: " with the weapon name, and
 *   TEXTS[1226] "Боезапас: " with the count under it. Rows are 30 apart: the weapon row is
 *   y = 75 + 30*slot (0x8FE6C/0x8FE74) and the ammo row y = 90 + 30*slot (0x8FF70/0x8FF74). A weapon
 *   id of 0 draws NEITHER row (0x8FE50 CMP #0 / BEQ).
 *
 * The weapon names are only known for ids 25..30: they append TEXTS[1350..1355] and read the ammo
 * count out of Ship.bAmmo25/26/27 @ +0x3D..+0x3F and bAmmo29/30 @ +0x40..+0x41. Id 28 is the odd one
 * out -- it still appends TEXTS[1353] but sets the count to -1, which the tail prints as the literal
 * aNA "N.A." (0x8FF3C). An id outside 25..30 that is not zero appends NO name at all (the line stays
 * the "Оружие N: " label) and its count is the 0 that 0x8FCBC left in R11.
 *
 * The count is then halved. If the two weapon ids are equal and this is slot 0 and ammo/2 == 1 the
 * original rounds up (0x8FF0C..0x8FF18), otherwise it rounds down (0x8FF28..0x8FF30); both arms add 1
 * first when the value is negative, which only matters for the -1 sentinel that took the "N.A." exit
 * already.
 *
 * Four controls, all centred on 120, y 140 / 160 / 180 / 200, from *pCtlNameEditBackspace @ 0x186BA8,
 * *pCtlNameEditAccept @ 0x186B9C, *unk_186B98 and *pGeoscapeCtlB @ 0x186BB0, with TEXTS[1227]
 * "Вернуться на базу" at 140, TEXTS[1228] "Выбрать нов. цель" at 160, TEXTS[1229] "Патруль" at 180 and
 * TEXTS[1230] "Отмена" at 201 -- the last one only while bInterceptDone @ 0x18709C is zero, which is
 * also the condition that makes pGeoscapeCtlB blit a SECOND time (0x900AC..0x900B4). Their handlers
 * depend on Ship.bContactMsg @ +0x3C: with it set all four get Smap_ResolveInterceptUI; with it clear
 * they get Smap_DismissSelected, Smap_SelectMapTarget (which OVERWRITES the Smap_DismissSelected
 * store on the same control at 0x8FFE0/0x8FFEC), Smap_ConfirmSelected and Smap_ResolveInterceptUI.
 * The store through 0x186B98 at 0x8FFF8 is unconditional while its value is loaded conditionally, so
 * that control gets ResolveInterceptUI or ConfirmSelected depending on bContactMsg.
 */
#define PUFO_STATUS_FLAG_CRAFT_DETAIL   0x0B /* 0x8F9F0 CMP R0, #0xB: the selected craft's card */
#define PUFO_STATUS_TEXT_WEAPON1        1224 /* "Оружие 1: " */
#define PUFO_STATUS_TEXT_WEAPON2        1225 /* "Оружие 2: " */
#define PUFO_STATUS_TEXT_AMMO           1226 /* "Боезапас: " */
#define PUFO_STATUS_TEXT_RETURN_BASE    1227 /* "Вернуться на базу" */
#define PUFO_STATUS_TEXT_NEW_TARGET     1228 /* "Выбрать нов. цель" */
#define PUFO_STATUS_TEXT_PATROL         1229 /* "Патруль" */
#define PUFO_STATUS_TEXT_CANCEL_ORDER   1230 /* "Отмена", a second record with the 1144 text */
#define PUFO_STATUS_TEXT_SPEED_VALUE    1222 /* "Скорость: " */
#define PUFO_STATUS_TEXT_FUEL_VALUE     1223 /* "Топливо: " */
#define PUFO_STATUS_TEXT_WEAPON_NAME_FIRST 1350 /* 1350..1355, weapon ids 25..30 (0x8FE3C..0x8FDB0) */
#define PUFO_STATUS_WEAPON_ID_FIRST        0x19 /* 0x8FD80 CMP R8, #0x19 */
#define PUFO_STATUS_WEAPON_ID_LAST         0x1E /* 0x8FDAC CMP R8, #0x1E */
#define PUFO_STATUS_WEAPON_ID_NO_AMMO      0x1C /* 0x8FDF8 MOV R11, #-1: id 28 prints "N.A." */
#define PUFO_STATUS_DETAIL_NH              0xDC /* 0x8FA50 MOV R3, #0xDC: the card's anim height */
#define PUFO_STATUS_Y_DETAIL_SHIP          0x0F /* 0x8FAEC MOV R2, #0xF: "Корабль:" */
#define PUFO_STATUS_Y_DETAIL_BASE          0x1E /* 0x8FB74 MOV R2, #0x1E: "База:" */
#define PUFO_STATUS_Y_DETAIL_SPEED         0x2D /* 0x8FBF4 MOV R2, #0x2D: "Скорость:" */
#define PUFO_STATUS_Y_DETAIL_FUEL          0x3C /* 0x8FCA4 MOV R2, #0x3C: "Топливо:" */
#define PUFO_STATUS_DETAIL_ROW_STEP        0x1E /* 0x8FE64 MOV R8, #0x1E: the weapon rows are 30 apart */
#define PUFO_STATUS_DETAIL_Y_WEAPON0       0x4B /* 0x8FE74 ADD R2, R1, #0x4B: the first weapon row */
#define PUFO_STATUS_DETAIL_Y_AMMO0         0x5A /* 0x8FF70 ADD R1, R10, #3 then MUL by 0x1E: 90, 120 */
#define PUFO_STATUS_DETAILCTL_Y_RETURN     0x8C /* 0x9005C MOV R3, #0x8C */
#define PUFO_STATUS_DETAILCTL_Y_TARGET     0xA0 /* 0x90068 MOV R3, #0xA0 */
#define PUFO_STATUS_DETAILCTL_Y_PATROL     0xB4 /* 0x90074 MOV R3, #0xB4 */
#define PUFO_STATUS_DETAILCTL_Y_CANCEL     0xC8 /* 0x90080 MOV R3, #0xC8 */
#define PUFO_STATUS_Y_DETAIL_CANCEL_LBL    0xC9 /* 0x90210 MOV R2, #0xC9: "Отмена", only when not done */

/* 0x90220: flag 0x0D, the panel that says the trace was lost. This is the only arm in the chain that
 * touches the clock: 0x90234 loads 0x159618 and 0x90238 calls GameTime_Pause @ 0x5d6ac, which is a
 * store of 1 into 0x15961C, i.e. GameTime.bPaused.
 *
 * Then it re-blits the same strip pad rectangle as the 0x0B card (0x9023C..0x90274, the same four
 * reads out of the pad at 0x186BBC and the same 0x3C / 0x78 / 0x82 / 0x96), and animates its body at
 * (0xE, 0x46) size (0xD0, 0x78) from src (0x28, 0x32), key 0xB4, nIdx 0, nMode 0 -- box x 14..225,
 * y 70..195.
 *
 * Line 1 at y 90 depends on line 0's colour slot 0 compared against 0xFF (0x902C8). Equal to 0xFF it is
 * TEXTS[1231] "Цель потеряна" alone. Otherwise it is TEXTS[1210] "НЛО " + itoa(strategList[slot 0]
 * ->nDisplayId, the +0x10 dword read at 0x90368) + TEXTS[1356] " след потерян" -- the third part
 * carries its own leading space. The colour value is read twice here (0x902C0 for the test, 0x90350 for
 * the index) and never written.
 *
 * Line 2 at y 118 is TEXTS[1216] "Корабль: " + Ship_FormatDisplayName(colour slot 1) -- the second
 * colour slot, not the 0x187054 global. One control: *pGeoscapeCtlB @ 0x186BB0 centred on 120 from its
 * own uWidth (0x90444 LDRH [R0,#4], LSR #1, RSB #0x78) at y 155, blitted at 0x90464 BEFORE its handler
 * is installed at 0x9047C (Smap_ResolveInterceptUI), with TEXTS[1144] "Отмена" centred at y 156. */
#define PUFO_STATUS_FLAG_INTERCEPT_PAUSE   0x0D /* 0x9022C CMP R0, #0xD */
#define PUFO_STATUS_COLOUR_NO_TARGET       0xFF /* 0x902C8 CMP R0, #0xFF: slot 0 means "no target" */
#define PUFO_STATUS_TEXT_TARGET_LOST       1231 /* 0xF1293 "Цель потеряна" */
#define PUFO_STATUS_TEXT_TRACE_LOST        1356 /* 0xF76AC " след потерян", with its leading space */
#define PUFO_STATUS_STRATEG_DISPLAY_ID     0x10 /* 0x90368 LDR [R0,#0x10], StrategItem.nDisplayId */
#define PUFO_STATUS_PAUSE_ANIM_Y           0x46 /* 0x9029C MOV R2, #0x46 */
#define PUFO_STATUS_PAUSE_NH               0x78 /* 0x90278 MOV R7, #0x78 */
#define PUFO_STATUS_PAUSE_Y_LINE1          0x5A /* 0x903AC MOV R2, #0x5A */
#define PUFO_STATUS_PAUSE_Y_LINE2          0x76 /* 0x9042C MOV R2, #0x76 */
#define PUFO_STATUS_PAUSECTL_Y             0x9B /* 0x90454 MOV R3, #0x9B */
#define PUFO_STATUS_PAUSE_Y_LABEL          0x9C /* 0x904BC MOV R2, #0x9C */

/* 0x904D8: flag 0x10, the attacked-city panel. It is the only arm of this chain that names a mission
 * site, and the only one of the three big panels that does NOT re-blit the strip pad first: the flag
 * read at 0x904F8 goes straight to the animated body at (0xE, 0x46) size (0xD0, 0x78) from src
 * (0x28, 0x32), key 0xB4, nIdx 0, nMode 0 (0x9050C..0x90538) -- the same box as the 0x0D panel, x 14..225,
 * y 70..195. R9 = 0x39400 | 0x18 = 1144 * 205 and R7 = 0x78 are built at 0x904D8/0x904DC before the
 * test: R9 is this arm's second label record and R7 is the 0x1B arm's strip colour.
 *
 * Line 1 at y 86 is TEXTS[1232] "Город: " + aMissionSites[colour slot 1].szName. 0x9056C reads slot 1,
 * 0x90574 MULs it by 0x18 (the 24-byte record), 0x90578/0x9057C add it to 0x1864F8 + 0xC8 = 0x1865C0 and
 * 0x90584 takes the name at +4. The index is not bounds-checked against the 56 records (bugs.tsv).
 * Line 2 at y 104 is TEXTS[1233] "Город атакован пришельцами", whose shipped text ends in a space.
 *
 * Two controls, each blitted BEFORE its handler is installed: *pGeoscapeCtlConfirm @ 0x186BA4 centred on
 * 120 from its own uWidth at y 136 (0x90634 blit, 0x90648 handler = Smap_OnPointerUp), labelled TEXTS
 * [1234] "На зону террора" centred at the SAME y 136 (0x906D4); then *pGeoscapeCtlA @ 0x186BB4 centred at
 * y 160 (0x90670 blit, 0x9067C handler = Smap_ResolveInterceptUI), labelled TEXTS[1144] "Отмена" centred
 * at y 161 (0x90724). */
#define PUFO_STATUS_FLAG_CITY_ATTACKED     0x10 /* 0x90504 CMP R0, #0x10 */
#define PUFO_STATUS_TEXT_CITY              1232 /* 0xF1360 "Город: " */
#define PUFO_STATUS_TEXT_CITY_ATTACKED     1233 /* 0xF142D "Город атакован пришельцами ", with its trailing space */
#define PUFO_STATUS_TEXT_GO_TO_ZONE        1234 /* 0xF14FA "На зону террора", the confirm control's label */
#define PUFO_STATUS_CITY_ANIM_Y            0x46 /* 0x90518 MOV R2, #0x46: equal to PAUSE_ANIM_Y */
#define PUFO_STATUS_CITY_NH                0x78 /* 0x90534 STR R7: equal to PAUSE_NH */
#define PUFO_STATUS_CITY_Y_LINE1           0x56 /* 0x905AC MOV R2, #0x56: y 86 */
#define PUFO_STATUS_CITY_Y_LINE2           0x68 /* 0x90600 MOV R2, #0x68: y 104 */
#define PUFO_STATUS_CITYCTL_Y_CONFIRM      0x88 /* 0x9062C MOV R3, #0x88: y 136 */
#define PUFO_STATUS_CITY_Y_CONFIRM_LBL     0x88 /* 0x906C8 MOV R2, #0x88: the label shares its control's row */
#define PUFO_STATUS_CITYCTL_Y_ACTION       0xA0 /* 0x90664 MOV R3, #0xA0: y 160 */
#define PUFO_STATUS_CITY_Y_ACTION_LBL      0xA1 /* 0x90718 MOV R2, #0xA1: y 161 */

/* 0x90728: flag 0x1B, the intercept list. This is the only arm of the chain with early exits that end
 * the WHOLE default arm: 0x90798, 0x907A8, 0x907B8 and 0x90904 all branch to the jumptable's default
 * label, whose body is the shared tail at 0x944B4 — not to the next flag test. It is also the only arm
 * that draws the clock strip and the thirteen root controls.
 *
 * 0x90748 reads colour slot 0 and 0x90760 STORES it into 0x187054, so this arm writes the selected-craft
 * global just as the 0x0B card does. The strip pad is then re-blitted as a rect (0x9076C..0x90788, the
 * same four pad reads, nIdx 0x3C, mid R7 = 0x78, low 0x82) but with nKey 0x96 read at 0x9074C — the same
 * value the 0x0B card uses, built here instead of as an immediate next to the call.
 *
 * Only then the three gates: bSmapUfoCrashPanel @ 0x18708C, bSmapSitePanel @ 0x187090 and
 * bSmapAlienBasePanel @ 0x187098 (the flags RadarScreen's click arms for an active type-4, type-3 and
 * type-0 intercept). Any one of them ends the frame, AFTER the strip rect is already painted.
 *
 * With all three clear: PadImage_Blit of the strip pad itself (0x907C0), Geoscape_DrawClockPads (0x907C8,
 * with NO GameTime_Decode here — the decode belongs to Geoscape_AdvanceTime, Geoscape_PulseUpdate,
 * LoadGame and InitUI, and 0x6C35C in Geoscape_DrawScreen is the only draw-time one), then thirteen
 * UiControl_BlitActivePad calls over the root block's own widget array (0x907D8..0x907F4, the counter
 * living in dword_186B08 in the original).
 *
 * Then the body anim at (0xE, 0x104) size (0xD0, 0x28) from src (0x1E, 0x28) with key 0xFF, nIdx 0 and
 * nMode 0 — box x 14..225, y 260..305, i.e. it paints OVER the clock strip it just drew. TEXTS[1235]
 * "Выберете цель" is NOT centred: 0x90864 fixes x at 0x28 (40), 0x90860 gives y 0x113 (275). *pCtlB
 * @ 0x186BB0 is fixed at x 0x8C (140) — the only control in this chain that is not centred — at y 0x112
 * (274), blitted at 0x90894 before Smap_ResolveInterceptUI is installed at 0x908A4. Its TEXTS[1144]
 * label is centred on 173 and not on 120 (0x908EC RSB R1,R1,#0xAD), at the same y 275.
 *
 * Finally 0x908F8 tests bFinalMissionCtlArmed @ 0x187068: clear, the frame ends. Set, pFinalMissionCtl
 * @ 0x186B8C is blitted and TEXTS[1236] "Финальная миссия!" is drawn at y 1 with
 * x = (pGeoscapeCtlConfirm->uWidth >> 1) - (width >> 1) + 1 (0x90958..0x90970): the control's own x is
 * never read, so this centring is against the control's WIDTH at the top edge of the screen. */
#define PUFO_STATUS_FLAG_INTERCEPT_LIST     0x1B /* 0x90738 CMP R0, #0x1B */
#define PUFO_STATUS_TEXT_CHOOSE_TARGET      1235 /* 0xF15C7 "Выберете цель" */
#define PUFO_STATUS_TEXT_FINAL_MISSION      1236 /* 0xF1694 "Финальная миссия!" */
#define PUFO_STATUS_LIST_ANIM_Y             0x104 /* 0x9080C MOV R2, #0x104: y 260 */
#define PUFO_STATUS_LIST_NH                 0x28  /* 0x90824 STR R10, the R10 built at 0x904F4 */
#define PUFO_STATUS_LIST_X_LABEL            0x28  /* 0x90864 MOV R1, #0x28: fixed, not centred */
#define PUFO_STATUS_LIST_Y_LINE             0x113 /* 0x90858 / 0x90860 MOV R2,#0x110 then ORR R2,R2,#3 */
#define PUFO_STATUS_LISTCTL_X               0x8C  /* 0x90874 MOV R3, #0x8C: x 140 */
#define PUFO_STATUS_LISTCTL_Y               0x112 /* 0x90880 / 0x90888 MOV R3,#0x110 then ORR R3,R3,#2 */
#define PUFO_STATUS_LIST_CANCEL_ANCHOR      0xAD  /* 0x908EC RSB R1,R1,#0xAD: centred on 173 */
#define PUFO_STATUS_LIST_Y_LABEL            0x113 /* 0x908E8 ORR R2,R2,#3: the label shares the line's y */
#define PUFO_STATUS_FINAL_MISSION_Y         0x01  /* 0x90960 MOV R2, #1 */

/* 0x909A8: flag 0x14, the map-target panel. Its test prologue is what carries R7 = TEXTS[1238]
 * (0x909B0) and R10 = 0xBE4 (0x909AC/0x909B4, the MOV #0xBE0 then ORR #4 pair) into the arm.
 *
 * bMapSelectionFlag @ 0x159E84 branches at 0x909DC, and the two blocks it selects build the SAME
 * Ui_AnimExpandBlit call — 0x909E0..0x90A04 and 0x90BAC..0x90BD0 store the same nine values. The flag
 * only picks the text record: TEXTS[1237] "Крушение " when clear, TEXTS[1238] "Миссия: " when set. The
 * anim's nKey 0xB4, src 0x28/0x32, nIdx 0 and nMode 0 all come from registers the flag-0x10 test
 * prologue left behind (0x904DC R7=0x78, 0x904EC R11=0xB4, 0x904F0 R8=0, 0x904F4 R10=0x28), which is
 * why the values are not literals at this call site.
 *
 * Both arms then append `pStatusPanel->apLines[colour slot 0]` — the status panel's own row buffer,
 * apLines at panel + 0xBE4 — with no bounds test against nLineCount (20 in the shipped build) and no
 * null check. Only the CLEAR arm adds anything after it: a space (0x90A58), then the size word of
 * `strategList[colour slot 1]` by its bCraftClass byte at +0x02 (0x90A84 LDRSB, 8-case jumptable at
 * 0x90A8C: 0→1359, 1/2→1360, 3/4→1361, 5/6→1362, 7→1363, outside 0..7 no append).
 *
 * Clear arm: the line at y 0x46 (70); *pGeoscapeCtlA @ 0x186BB4 centred at y 0x73 (115) with
 * Smap_ResolveInterceptUI installed at 0x90B60 BEFORE the 0x90B68 blit (the reverse of the 0x0B/0x0D/
 * 0x10/0x1B order); TEXTS[1127] "ОК" centred at y 0x74 (116).
 * Set arm: the line at y 70; *pGeoscapeCtlB @ 0x186BB0 centred at y 0x66 (102) with Smap_CancelTarget
 * at 0x90C78 before the 0x90C80 blit; TEXTS[1127] centred at y 0x67 (103); *pGeoscapeCtlA centred at
 * y 0x7D (125) with Smap_ResolveInterceptUI; TEXTS[1144] "Отмена" centred at y 0x7E (126). The last
 * centring is shared: both arms run the same 0x90D44..0x90D5C tail with y in R2. */
#define PUFO_STATUS_FLAG_TARGET_PANEL       0x14 /* 0x909B8 CMP R0, #0x14 */
#define PUFO_STATUS_TEXT_CRASH              1237 /* 0xF1761 "Крушение ", trailing space */
#define PUFO_STATUS_TEXT_MISSION_PREFIX     1238 /* 0xF182E "Миссия: ", trailing space */
#define PUFO_STATUS_TARGET_ANIM_Y           0x3C /* 0x909C8 MOV R2, #0x3C: y 60 */
#define PUFO_STATUS_TARGET_NH               0x5A /* 0x909EC / 0x90BB8 MOV R3,#0x5A then STR */
#define PUFO_STATUS_TARGET_Y_LINE           0x46 /* 0x90B24 / 0x90C3C MOV R2,#0x46: y 70 */
#define PUFO_STATUS_TARGETCTL_Y_OK          0x73 /* 0x90B4C MOV R3,#0x73: CtlA, clear arm, y 115 */
#define PUFO_STATUS_TARGET_Y_OK_LBL         0x74 /* 0x90BA0 MOV R2,#0x74: y 116 */
#define PUFO_STATUS_TARGETCTL_Y_CANCEL      0x66 /* 0x90C64 MOV R3,#0x66: CtlB, set arm, y 102 */
#define PUFO_STATUS_TARGET_Y_CANCEL_LBL     0x67 /* 0x90CC8 MOV R2,#0x67: y 103 */
#define PUFO_STATUS_TARGETCTL_Y_OK2         0x7D /* 0x90CF0 MOV R3,#0x7D: CtlA, set arm, y 125 */
#define PUFO_STATUS_TARGET_Y_CANCELLED_LBL  0x7E /* 0x90D40 MOV R2,#0x7E: the TEXTS[1144] row, y 126 */

/* 0x90D78: flag 0x13, the intercept-mission panel. Its test prologue loads R11 = 0xBD8 (0x90D88), the
 * UiPanel nUnk3032 offset -- this arm reads the panel's own SELECTED ROW instead of a colour slot.
 *
 * Both bMapSelectionFlag branches build the same anim call as the 0x14 arm (0x90DB8..0x90DE4 and
 * 0x90F4C..0x90F78 store nKey 0xB4, srcY 0x32, srcX 0x28, nH 0x5A, nMode 0, nIdx 0 and nW 0xD0; here
 * the two zeros come from `MOV R0,#0` at 0x90DB0 rather than from R8).
 *
 * Line 1: the clear arm STRCOPYs apLines[*pStatusPanel + 0xBD8] alone (0x90DE8..0x90E14); the set arm
 * starts from TEXTS[1238] "Миссия: " and strcats the same row on (0x90F7C..0x90FB8). Drawn centred at
 * y 0x46 (70). The index is the panel's selected-row dword, written by UiPanel_TrackHoldRow as
 * cursor + row, and the original applies no bounds test and no null check to it -- recorded in bugs.tsv.
 *
 * Line 2, in BOTH branches: psz is RESET to TEXTS[1232] "Город: " (0x90E44 / 0x90FE8) and then
 * `aMissionSites[(i8)strategList[colour slot 1].bCraftClass].szName` is strcat on: 0x90E94 / 0x91038
 * LDRSB [R0,#2], ×0x18 (0x90E9C MUL with MOV R2,#0x18), + 0x1864F8 + 0xC8 = 0x1865C0, +4 (the name).
 * That byte carries two roles -- a size class 0..7 for spawned contacts and the mission-site index for
 * an intercept item, because BasePlaneScreen_AddInterceptMission @ 0x7DE14 indexes this very table with
 * the value it stores at +0x02. Drawn centred at y 0x56 (86).
 *
 * Clear arm controls: *pCtlA centred at y 0x73 (115), Smap_ResolveInterceptUI installed at 0x90F08
 * before the 0x90F10 blit; TEXTS[1127] centred at y 0x74 (116). Set arm: *pCtlB centred at y 0x69 (105)
 * with Smap_CancelTarget at 0x910AC before the 0x910B4 blit; TEXTS[1127] centred at y 0x6A (106);
 * *pCtlA centred at y 0x80 (128) with Smap_ResolveInterceptUI; TEXTS[1144] centred at y 0x81 (129).
 * Both arms end in the shared centring tail at 0x91174..0x9118C and fall through into the next test. */
#define PUFO_STATUS_FLAG_MISSION_SITE         0x13 /* 0x90D90 CMP R0, #0x13 */
#define PUFO_STATUS_PANEL_SEL_ROW            0xBD8 /* 0x90D88 MOV R11,#0xBD8: UiPanel nUnk3032, the row the pointer last selected */
#define PUFO_STATUS_SITE_Y_LINE2             0x56  /* 0x90ECC / 0x91070 MOV R2,#0x56: the "Город: " row, y 86 */
#define PUFO_STATUS_SITECTL_Y_OK             0x73  /* 0x90EF4 MOV R3,#0x73: CtlA, clear arm, y 115 */
#define PUFO_STATUS_SITE_Y_OK_LBL            0x74  /* 0x90F44 MOV R2,#0x74: y 116 */
#define PUFO_STATUS_SITECTL_Y_CANCEL         0x69  /* 0x91098 MOV R3,#0x69: CtlB, set arm, y 105 */
#define PUFO_STATUS_SITE_Y_CANCEL_LBL        0x6A  /* 0x910F8 MOV R2,#0x6A: y 106 */
#define PUFO_STATUS_SITECTL_Y_OK2            0x80  /* 0x91120 MOV R3,#0x80: CtlA, set arm, y 128 */
#define PUFO_STATUS_SITE_Y_CANCELLED_LBL     0x81  /* 0x91170 MOV R2,#0x81: the TEXTS[1144] row, y 129 */
#define PUFO_STATUS_MISSION_SITE_STRIDE      0x18  /* 0x90E90 / 0x91034 MOV R2, #0x18: sizeof MissionSiteRec */

/* 0x91190: flag 0x12, the travel panel. This arm is the flag-0x14 arm with ONE thing taken away: its
 * clear arm appends neither the space nor the size word (0x911F4..0x9122C copies apLines[colour slot 0]
 * and draws straight away). The anim arguments, both line rows, all four control rows and the two label
 * rows are the same immediates at 0x911C4..0x91464 as at 0x909E0..0x90D40, so the arm reuses the
 * PUFO_STATUS_TARGET_* constants; the duplication is recorded in bugs.tsv. */
#define PUFO_STATUS_FLAG_TRAVEL_PANEL         0x12 /* 0x9119C CMP R0, #0x12 */

/* 0x9148C..0x91688: flag 0x17, the descent panel. One path, and its rows are the 0x14 arm's SET-arm
 * rows again (70, 102, 103, 125, 126), so PUFO_STATUS_TARGET_* is reused a third time. Its record is
 * unk_F18FB = 0xB38D0 + 1239 * 205 = "Снижение: ". The read that decides CtlB's handler is
 * Craft_CountSoldiersAboard on the colour-slot-0 byte (0x915D4..0x915EC), and a zero count replaces
 * the already-blitted CtlB handler at 0x915F8..0x91604. */
#define PUFO_STATUS_FLAG_DESCENT_PANEL        0x17 /* 0x91498 CMP R0, #0x17 */

/* 0x91934: flag 0x16 runs a second copy of the flag-0x17 body (0x91938..0x91B1C). Both bodies are 484 bytes /
 * 121 instructions, same order, same frame slots, same callees; 117 of the 121 disassembled pairs are
 * identical and the other 4 are equivalent -- three BNEs naming their own arm's loop head and one
 * `STRB R2,[R0,R1]` (0x91AE4) against `STRB R2,[R1,R0]` (0x91650), the same additive address. */
#define PUFO_STATUS_FLAG_DESCENT_ALT          0x16 /* 0x91930 CMP R0, #0x16 */

/* 0x916A0..0x9191C: flag 0x15, the descent-target panel: flag 0x17's shape (TEXTS[1239] + the selected row,
 * CtlB centred with Smap_BeginCraftSelect and the same soldiers-aboard overwrite) on the 0x13 arm's control
 * rows 105 / 106 / 128 / 129, plus the city line -- but indexed by StrategItem.nTargetIdx (+0x84, read at
 * 0x91798) instead of the +0x02 byte. */
#define PUFO_STATUS_FLAG_DESCENT_TARGET       0x15 /* 0x91698 CMP R0, #0x15 */
#define PUFO_STATUS_TEXT_DESCENT              1239 /* unk_F18FB @ 0xF18FB, loaded into R6 at 0x91490 */

/* 0x91B34..0x91CE8: flag 0x3C, the alien-base panel. 0x3C is outside the 0x1F..0x31 range the jumptable at
 * 0x8C844 covers (0x8C798 CMP #0x1E BLE, 0x8C7AC CMP #0x32 BGE), so it reaches the default-case chain like
 * 0x12..0x17 do. Its anim is the widest in the chain (x 2 at 0x91B64, nW 0xE8 at 0x91B6C) and its handlers are
 * installed after both blits, unlike the descent arms'. Records 1445 and 1446 are uninitialised in the shipped
 * .data (the whole 0xB38D0 block is) and only carry text once TEXTS.DAT loads from lang.dat. */
#define PUFO_STATUS_FLAG_ALIEN_BASE_PANEL     0x3C /* 0x91B2C CMP R0, #0x3C */
#define PUFO_STATUS_TEXT_ALIEN_BASE           1445 /* unk_FBDF1: "Внимание! Обнаружена база пришельцев" */
#define PUFO_STATUS_TEXT_CENTRE_BASE          1446 /* unk_FBEBE: "Центр на базу" */
#define PUFO_STATUS_BASECTL_Y_CONFIRM         100  /* 0x91BE0 MOV R3, #0x64 */
#define PUFO_STATUS_BASE_Y_CONFIRM_LBL        100  /* 0x91C38 MOV R2, #0x64 -- the label row IS the control row */

/* 0x91CEC..0x91DB0: the tail every arm of the chain lands on. Not 2 means: blit *pGeoscapeStripPad, draw the clock
 * pads, then blit the thirteen root widgets at 0x1864F8 + 4*i for i in 0..12. == 2 means: run the new-base
 * globe hover, store its tile into bPendingRegionId @ 0x159DF4 as a BYTE, and -- only while bPlacementPending
 * @ 0x1870AC is clear -- blit *g_pUiCloseCtl. The screen id is set to 2 by the new-base confirm at 0x61620. */
#define PUFO_STATUS_SCREEN_ID_GLOBE_HOVER       2  /* 0x91CF4 CMP R0, #2 */

/* ---- the screen switch's shared frame end, 0x944B4..0x94550 -------------------------
 *
 * The body of the default case of the jumptable at 0x8C1B0 (`CMP R0, #0x14 / ADDLS`, 21 cases on
 * nGeoscapeScreen @ 0x186BC4). Every screen arm ends with `B def_8C1B0` and lands here, and so does
 * case 20, which falls through from 0x944B0. Screens 13 and 14 -- and any id outside 0..20 -- come
 * straight from the jumptable.
 *
 * Case 20 (0x944B0) loads 0x158840 into R0 and falls through to the BaseAtScreen_Draw call, and the
 * load is DEAD: BaseAtScreen_Draw @ 0x58728 takes no argument at all -- it reaches its own gate
 * (0x158864), pad pointer (0x158840) and cooldown (0x15885C) through globals. The default path gets
 * here with R0 = the screen id, which is just as dead.
 *
 * Then MainMenu_nListMode @ 0x157188 decides whether a one-line modal dialog goes over the frame:
 * mode 4 draws TEXTS[1103] and mode 3 draws TEXTS[1102], both 150 wide, both with no button labels,
 * and each writes 40 into Map.anActionPhaseSave0[112] @ 0x128AA0 first. The two record numbers are
 * built in registers as 0x440|0xF (0x944D4 / 0x944EC) and 0x440|0xE (0x94510 / 0x94528), and the mode
 * is re-read from memory at 0x944FC between the two tests. The values, the width and record 1102 are
 * the same ones MainMenu_Draw @ 0x1DBB8 uses for its own mode 3. Finally GX_Present @ 0x27788 --
 * which is why Render_Frame's geoscape branch never reaches its own present call (0x22328 / 0x22330).
 *
 * Deviation: the original reaches the modal renderer's anim state, the text table and the button
 * layout slots through the Map context (R4 = 0x1139B8); the port passes the sub-envs
 * UiModal_DrawDialog already takes. Neither call site passes a layout argument at all -- the callee
 * computes the button layout into apUiSlotsA[20..23] that this caller never reads back -- so the
 * port hands it the env's layout object, which the callers that DO read it share. */
#define PUFO_GEOFRAME_LIST_MODE_SAVING    4   /* 0x944C4 CMP R0, #4 */
#define PUFO_GEOFRAME_LIST_MODE_LOADING   3   /* 0x94500 CMP R0, #3 */
#define PUFO_GEOFRAME_MODAL_WIDTH       150   /* 0x944F0 / 0x9452C MOV R1, #0x96 */
#define PUFO_GEOFRAME_MODAL_LINES         1   /* 0x944E4 / 0x94520 MOV R3, #1 */
#define PUFO_GEOFRAME_PHASE_SAVE         40   /* 0x944CC / 0x94508 MOV R0, #0x28 -> Map+0x112 */
#define PUFO_GEOFRAME_TEXT_SAVING      1103   /* 0x440 | 0xF at 0x944D4 + 0x944EC */
#define PUFO_GEOFRAME_TEXT_LOADING     1102   /* 0x440 | 0xE at 0x94510 + 0x94528 */

typedef struct GeoscapeFrameEndEnv {
    const struct BaseAtDrawEnv *pBaseAtEnv;   /* BaseAtScreen_Draw's own env -- the callee takes no argument */
    const i32            *pnListMode;         /* MainMenu_nListMode @ 0x157188, read twice (0x944C0, 0x944FC) */
    i32                  *pnActionPhaseSave112; /* Map.anActionPhaseSave0[112] @ 0x128AA0 */
    struct UiModalAnim   *pModalAnim;         /* the Map-block anim cells the modal renderer works through */
    struct UiModalLayout *pModalLayout;       /* apUiSlotsA[20..23]: written here, never read back here */
    const u8             *pTextRecs;          /* aTextRecs @ 0xB38D0 */
    i32                (*pfnPresent)(void *pUser); /* GX_Present @ 0x27788, the thunk to GX_PresentFrame */
    void                  *pPresentUser;
} GeoscapeFrameEndEnv;

/* Returns the present thunk's value, which is what Render_Frame returns on the geoscape branch. */
i32 Geoscape_DrawFrameEnd(const GeoscapeFrameEndEnv *pEnv);

/* ---- the short screen arms, 0x93DD4..0x93F04, 0x94114..0x941A4, 0x941AC..0x942E8 -----------
 *
 * Seven arms of the screen switch are thin: blit a pad, blit a run of the screen's own control pads,
 * call the screen draw (all of them ported already), then the frame end above. The run is the same
 * loop nine times over -- counter spilled into the shared scratch dword_186B08, the control pointer
 * read out of the screen's own block on every iteration -- so the port writes it once.
 *
 * Which controls, in the original's order:
 *   8  0x94114..0x94154  ResearchScreen_Draw FIRST, then aResearchTopics + 0x1D48 / + 0x1D4C
 *                        (0x160C50, 0x160C54).
 *   9  0x9415C..0x941A4  *unk_159E98 blitted first, then 0x15EE1C / 0x15EE20, then ManufScreen_DrawFrame.
 *  10  0x93E5C..0x93EB4  *0x1870D8 (the transfer root array's pad), then the four controls at
 *                        0x1870DC..0x1870E8, then TransferScreen_Draw(R0 = the array, R1 = nSelectedBaseIdx
 *                        read SIGNED at 0x93EA8, R2 = byte_1870F4 read SIGNED at 0x93E98).
 *  11  0x93DD4..0x93E14  *pBuySellScreenPad @ 0x161A08, then g_aBuySellCtl[0] / [1] (0x161A0C / 0x161A10),
 *                        then BuySellScreen_Draw at 0x93E10, reached by falling through.
 *  12  0x93E20..0x93E58  the same three steps, then B 0x93E0C -- the SHARED BuySellScreen_Draw call. The
 *                        two prefixes are instruction-for-instruction identical, so screens 11 and 12 draw
 *                        the same frame; only their Geoscape_CloseUI cases differ (controls vs panels).
 *  15  0x94304           FundingScreen_Draw(&pFundPad @ 0x1595DC) and nothing else: no arm code to port.
 *  16  0x941AC..0x94288  Ui_DrawBaseInfo FIRST (R0 = the block base 0x1872E0), then ONE of three control
 *                        ranges of the same six-slot block -- see Geoscape_DrawUfopaediaArm.
 *  17  0x94288..0x942E8  PsiScreen_Draw(R0 = the block base 0x15EE78, R1 = byte_1870F4 read SIGNED at
 *                        0x94290), then the two scroll controls at 0x15EE7C / 0x15EE80 -- only with
 *                        byte_1870F8 clear AND g_bPsiPanelOpen set.
 *  18  0x942F4           Geoscape_DrawScreen(aOptionsListCtl) and nothing else: no arm code to port.
 *
 * Screens 10, 11 and 12 then fall into 0x93EB4..0x93F04: while the geoscape UI root block's own byte at
 * +0x35 (0x18652D, bBaseFacPreviewOk) is set, the map cursor position is fed to the two buy/sell scroll
 * lists. R7 is Geoscape_Draw's argument (0x8C190 MOV R7,R0) and Render_Frame sets it to 0x1864F8 at
 * 0x2232C, so the gate is that block's flag byte -- the same one BaseFacScreen_DrawPlacementOverlay
 * reads. The transfer arm reaches a buy/sell tail: that is the original's shape, not a port choice.
 *
 * Deviation: the original spills the loop counter into dword_186B08, a scratch dword every UI open path
 * reuses; the port keeps it local, as every other user of that global does here. */
#define PUFO_GEOARM_CTLS_2   2   /* 0x9414C / 0x94194 / 0x93E04 / 0x93E50 CMP R0, #2 */
#define PUFO_GEOARM_CTLS_4   4   /* 0x93E8C / 0x94240 / 0x9427C CMP R0, #4 */
#define PUFO_GEOARM_CTLS_6   6   /* 0x941FC CMP R0, #6: the end of the ufopaedia block's six slots */
#define PUFO_GEOARM_UFO_CAT_FIRST    4  /* 0x941DC MOV R0, #4 -- the category-scroll pair */
#define PUFO_GEOARM_UFO_ENTRY_FIRST  2  /* 0x9425C MOV R0, #2 -- the entry prev/next pair alone */

/* The control run is half-open -- [nFirst, nEnd) -- because that is exactly what the ARM writes:
 * MOV R0, #first, the body, then CMP R0, #end / BLT back to the body. */
typedef struct GeoscapeScreenArmEnv {
    UiControl *const   *apCtl;          /* the control pointers, in the original's order */
    UiControlBlitFn     pfnCtlBlit;
    UiControlBlitFn     pfnCtlBlitEx;
    void               *pCtlBlitUser;
} GeoscapeScreenArmEnv;

/* Screen 16's three gates, read straight out of the block the ufopaedia owns. */
typedef struct GeoscapeUfopaediaArmEnv {
    const u8 *pbOpen;        /* bUfopaediaOpen @ 0x18730C, LDRB at 0x941C0 */
    const u8 *pbEntryDirty;  /* bUfopaediaEntryDirty @ 0x187310, read at 0x941D0 and again at 0x94204 */
    const u8 *pbWidePanel;   /* g_bUfopaediaWidePanel @ 0x1871FC, LDRB at 0x94210 -- the wide text-panel layout
                              * flag that Ufopaedia_RefreshEntry sets from the entry-number band
                              * (0x9A154..0x9A244) and Geoscape_OnPointer reads as the same byte at 0x88E74 */
} GeoscapeUfopaediaArmEnv;

typedef struct GeoscapeBuySellTrackEnv {
    const u8   *pbPreviewOk;            /* bBaseFacPreviewOk @ 0x18652D, the root block's +0x35 byte */
    UiPanel   **ppPanelB;               /* pBuySellPanelB @ 0x161A00, the "<" column (0x93EC8, re-read 0x93ED4) */
    UiPanel   **ppPanelC;               /* pBuySellPanelC @ 0x161A20 (0x93EDC, re-read 0x93EE8) */
    const i16  *pnCursorX;              /* nSmapCursorX, LDRSH at 0x93ED0 / 0x93EE4 */
    const i16  *pnCursorY;              /* nSmapCursorY, LDRSH at 0x93ECC / 0x93EE0 */
} GeoscapeBuySellTrackEnv;

/* Tags the arm prototypes name. pufo_baseui.h owns the definitions and includes THIS header, so the
 * port cannot include it back -- the tags are enough for a pointer parameter. */
struct ResearchDrawEnv;
struct ManufFrameEnv;
struct TransferDrawEnv;
struct BuySellDrawEnv;
struct UiBaseInfoDrawEnv;
struct PsiScreenDrawEnv;
struct SoldierDrawEnv;

void Geoscape_DrawResearchArm(const struct ResearchDrawEnv *pResearchEnv,
                              const GeoscapeScreenArmEnv *pArmEnv);
void Geoscape_DrawManufArm(PadImage *pPad, const struct ManufFrameEnv *pManufEnv,
                           const GeoscapeScreenArmEnv *pArmEnv);
void Geoscape_DrawTransferArm(PadImage *pPad, const struct TransferDrawEnv *pTransferEnv,
                              const GeoscapeScreenArmEnv *pArmEnv,
                              const GeoscapeBuySellTrackEnv *pTrackEnv);
/* Screens 11 and 12: one function for the two identical prefixes. */
void Geoscape_DrawBuySellArm(PadImage *pPad, const struct BuySellDrawEnv *pDrawEnv,
                             const GeoscapeScreenArmEnv *pArmEnv,
                             const GeoscapeBuySellTrackEnv *pTrackEnv);

/* 0x941AC..0x94288: screen 16, the ufopaedia. Ui_DrawBaseInfo is called with the block base itself in R0
 * (0x941AC LDR R6,=g_pUfopaediaScreenPad / 0x941B0 MOV R0,R6), so it is the callee that blits the screen's
 * own pad (0x9B600) and its close control (0x9B7C0) -- this arm blits no pad. Then three ranges of the same
 * six-slot block, apCtl[i] being the word at the base + 8 + 4*i (0x1872E8..0x1872FC):
 *
 *   open && !dirty   4..6   0x941DC..0x94200  the category-scroll pair (aUiBaseInfoCtlDesc[4] / [5], the
 *                        "up"/"down" pair at x 200 y 18 / 38) -- the view with no entry drawn
 *   dirty  && wide   0..4   0x94220..0x94244  the text-panel scroll pair [0] / [1] (x 142 / 172 y 210) plus
 *                        the entry pair [2] / [3] ("leftp"/"rightp" at x 10 / 40 y 210)
 *   dirty  && !wide  2..4   0x9425C..0x94280  the entry pair only
 *
 * The two halves are independent on the dirty byte: 0x941C8 BEQ and 0x941D8 BNE both land on the dirty test
 * at 0x94204, so an open screen with a dirty entry blits NO category control, and a closed screen with a
 * dirty entry still blits the entry pair. Geoscape_OnPointer (0x88E28..0x88E54, 0x88E94..) drives exactly the
 * same three ranges on click, which is the cross-check for the bounds. */
void Geoscape_DrawUfopaediaArm(struct UiBaseInfoDrawEnv *pBaseInfoEnv,
                               const GeoscapeUfopaediaArmEnv *pGateEnv,
                               const GeoscapeScreenArmEnv *pArmEnv);

/* 0x94288..0x942E8: screen 17, the psi screen. The callee gets the block base 0x15EE78 in R0 and byte_1870F4
 * read SIGNED in R1 (LDRSB at 0x94290) -- the port carries that byte as PsiScreenDrawEnv.nBaseIdx. The two
 * controls are the block's own slots +4 / +8 (g_pPsiScrollPrevCtl 0x15EE7C, g_pPsiScrollNextCtl 0x15EE80), and
 * they run only with the base-picker overlay byte_1870F8 CLEAR (0x942A8 BNE) AND g_bPsiPanelOpen set (0x942B8
 * BEQ) -- both gates are fields of the callee's own env, the same globals. */
void Geoscape_DrawPsiArm(const struct PsiScreenDrawEnv *pPsiEnv, const GeoscapeScreenArmEnv *pArmEnv);

/* ---- the soldier-screen arms, 0x92DAC..0x92EE4 and 0x92EE4..0x9309C ---------------------------
 *
 * These are the only arms that POSITION a control before drawing: both write g_pUiCloseCtl's x/y (0x92DB0
 * STRH #0xE0 / 0x92DC8 STRH #1) and case 5 moves it again to (198,77) when a transfer is armed (0x92F20 /
 * 0x92F28). Both blit the close control LAST, after their own control run (0x92E04, 0x93058), and both hand
 * SoldierScreen_Draw the block base abFacilityGrid (0x92DCC / 0x92F34 / 0x93090 MOV R0,R5). Case 4 has no pad
 * blit; case 5 blits the block's pad word first (0x92EE8 / 0x92EEC) -- the same pad SoldierScreen_Draw blits
 * again while the screen is open (0x652D4).
 *
 * The control runs read the block again: case 4 takes slots at +0x148 (0x1598B0, 0x1598B4 -- CMP #2 at
 * 0x92DF8), case 5 the three at +0x150 (0x1598B8..0x1598C0 -- CMP #3 at 0x92F60 and again at 0x93088). Both
 * groups are Geoscape_BuildNavControls' own installs (0x67C70..0x67D24): the pair is SoldierScreen_PanelPrev /
 * PanelNext at (210,20) and (210,40), the triple is SoldierScreen_FindNextAtBase, SoldierScreen_FindPrevAtBase
 * and Ui_SetTransferFlag -- the third one arms the very byte case 5 branches on.
 *
 * Case 5 is the one arm whose ORDER depends on a gate. With bTransferFlag @ 0x159E10 set, the draw runs FIRST
 * (0x92F38), then the three controls, then the three captions; with it clear the three controls run first
 * (0x93070..0x9308C) and SoldierScreen_Draw runs AFTER them (0x93094), with no captions at all (the BEQ at
 * 0x92F18 jumps straight to 0x93064).
 *
 * The captions use the centring the original writes inline -- MOVS R1,R0 / ADDMI R1,R1,#1 / MOV R1,R1,ASR#1 /
 * RSB R1,R1,#centre, i.e. "if (w < 0) ++w; x = centre - (w >> 1)", the same shape as the shared tail at
 * 0x92ACC..0x92ADC. Record numbers come from the addresses: 0xB71AB/205 = 71 and so on.
 *   case 4  TEXTS[71] centred on 120 at y 26 (0x92E2C..0x92E60), TEXTS[88] at (38,50) (0x92E9C),
 *           TEXTS[89] at (170,50) (0x92EB8)
 *   case 5  TEXTS[90] centred on 104 at y 38 (0x92FA8..0x92FC8) -- the "Armour" caption SoldierScreen_DrawDetail
 *           draws at the same place, TEXTS[91] centred on 90 at y 80, TEXTS[92] centred on 170 at y 80
 * Every caption follows a Gfx_PackRgb565Pair call, and that call is NOT inert: it writes the two TEXT palette
 * entries Text_DrawString then draws with. Case 4 ends with a third pair (255,200,20 / 10,10,10) that nothing
 * in the arm uses -- the arm leaves it for the next drawing user. */
#define PUFO_SOLARM_CTLS_2      2   /* 0x92DF8 CMP #2: the block at +0x148 = 0x1598B0, 0x1598B4 */
#define PUFO_SOLARM_CTLS_3      3   /* 0x92F60 / 0x93088 CMP #3: +0x150 = 0x1598B8..0x1598C0 */
#define PUFO_SOLARM_CLOSE_X     224 /* 0x92DB0 / 0x92EF4 STRH #0xE0 */
#define PUFO_SOLARM_CLOSE_Y     1   /* 0x92DC8 / 0x92F00 STRH #1 */
#define PUFO_SOLTR_CLOSE_X      198 /* 0x92F20 STRH #0xC6 -- the transfer dialog's close button */
#define PUFO_SOLTR_CLOSE_Y      77  /* 0x92F28 STRH #0x4D */
#define PUFO_SOLARM_TEXT_TITLE   71 /* 0xB71AB: centred on 120, y 26 */
#define PUFO_SOLARM_TEXT_COL1    88 /* 0xB7F48: (38, 50) */
#define PUFO_SOLARM_TEXT_COL2    89 /* 0x4745:  (170, 50) -- the same record the plane screen labels with */
#define PUFO_SOLTR_TEXT_HEAD     90 /* 0xB80E2: centred on 104, y 38 */
#define PUFO_SOLTR_TEXT_ROW1     91 /* 0xB81AF: centred on 90, y 80 */
#define PUFO_SOLTR_TEXT_ROW2     92 /* 0xB827C: centred on 170, y 80 */
#define PUFO_SOLARM_CAP_CENTRE  120 /* 0x92E58 RSB #0x78 */
#define PUFO_SOLTR_HEAD_CENTRE  104 /* 0x92FC0 RSB #0x68 */
#define PUFO_SOLTR_ROW1_CENTRE   90 /* 0x93014 RSB #0x5A */
#define PUFO_SOLTR_ROW2_CENTRE  170 /* 0x93048 RSB #0xAA */

typedef struct GeoscapeSoldierArmEnv {
    GfxColorEnv *pColor;          /* pGX @ 0x1139B8, the pair the caption colours come from */
    const char  *pTextRecs;       /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart */
    UiControl   *pCloseCtl;       /* g_pUiCloseCtl: positioned, then blitted last */
    const u8    *pbTransferFlag;  /* bTransferFlag @ 0x159E10 -- case 5's gate and nothing else */
    PadImage    *pPad;            /* the block's soldier pad word @ 0x1598D4 -- case 5 only */
} GeoscapeSoldierArmEnv;

/* 0x92DAC..0x92EE4: screen 4, the soldier list. Position the close control, draw the screen, blit the two
 * panel controls, blit the close control, then the title and the two column captions. */
void Geoscape_DrawSoldierArm(const struct SoldierDrawEnv *pSoldierEnv, const GeoscapeSoldierArmEnv *pArmEnv,
                             const GeoscapeScreenArmEnv *pCtlEnv);

/* 0x92EE4..0x9309C: screen 5, the soldier name editor. The pad first, then the two orders described above. */
void Geoscape_DrawSoldierEditArm(const struct SoldierDrawEnv *pSoldierEnv, const GeoscapeSoldierArmEnv *pArmEnv,
                                 const GeoscapeScreenArmEnv *pCtlEnv);

/* ---- the base-facility arm, 0x92AF0..0x92DAC (it sits BEFORE case 4 in the function) ----------------
 *
 * Screen 7 is BaseFacScreen_Open (0x61858 sets nGeoscapeScreen = 7 and tail-calls BaseFacScreen_Init);
 * screen 1 is Base_EnterBuildMode (0x84E60), and the two share this arm's pad: the same global 0x186BA0 is
 * blitted at 0x91DCC (case 1) and at 0x92AF4 (case 7). The arm is rails, one separator, three callees, and
 * a control run that a flag can cancel.
 *
 * Rails (0x92B08..0x92CE4). Both loops run 24 times with the same colour idiom the soldier screen uses --
 * climbing 5*(k+20), falling 0xE6-5*k on the sides and 0xFA-5*k on the top and bottom edges. The side rails
 * are 1x7 rects at x 2 and 237, y 7k+1 and 7k+151 (0x92C08 ADD R9,R9,#7 with CMP #0xA8). The top and bottom
 * rails are 5x1 rects at y 315 and y 2, stepping x by 5 (0x92CD8 ADD R9,R9,#5, CMP #0x78): the climbing one
 * starts at x k+1 in both rows, but the falling row starts at x k+128 on top (0x92C80 ADD R1,R9,#0x80) and
 * at x k+120 on the bottom (0x92CC4 ADD R1,R9,#0x78) -- eight pixels further left, an asymmetry the original
 * has and the port keeps.
 *
 * Separator (0x92CE8..0x92D0C): one 208x1 bar at (2, 35) in the flat colour 160.
 *
 * Map_nGlyphPalBase is forced to 3 (0x92D14 LDR R12,=Map_nGlyphPalBase / 0x92D20 STR) and the arm never
 * restores it -- Text_DrawString saves and restores around itself, so 3 is what any other glyph user of the
 * frame sees after this arm. Then the three callees, in this order: BaseFacScreen_DrawNameGrid with the block
 * base and nSelectedBaseIdx read SIGNED (0x92D2C LDRSB / 0x92D34), Base_DrawFacilityIconStrip with the mark
 * -1 (0x92D38 MOV R1,#0xFFFFFFFF / 0x92D40), and BaseFacScreen_DrawBuildDialog with the build block
 * gBaseFacBuildBlock @ 0x158870 and nSelectedBaseIdx read again (0x92D44..0x92D58) -- the callee may have
 * changed it in between, so it is a second read, not a reuse.
 *
 * Control run (0x92D5C..0x92D94), the only one a flag can cancel: with bBaseFacPlaceArmed @ 0x158888 SET the
 * arm blits nothing (0x92D68 BNE def_8C1B0) -- the placement overlay owns the screen then. Otherwise the two
 * panel page controls gBaseFacBuildBlock[+4] / [+8] @ 0x158874 / 0x158878 blit, installed by BaseFacScreen_Init
 * with BaseFacScreen_PanelPrev and BaseFacScreen_PanelNext as handlers (0x5A20C, 0x5A21C). The original runs
 * the loop with its index in the shared scratch counter dword_186B08 -- it stores 0 at 0x92D74, uses and
 * re-reads it each step and leaves it at 2; every other user of that global initialises it first
 * (Ui_OpenBaseInfo 0x84B60, Geoscape_PrepScreenOpen 0x851FC, Geoscape_InitUI's pad loop 0x866B4), so the port
 * keeps a local counter. */

#define PUFO_BFA_RAIL_STEPS    24   /* 0x92C0C CMP #0xA8 over steps of 7, and 0x92CE0 CMP #0x78 over steps of 5 */
#define PUFO_BFA_SIDE_STEP      7   /* 0x92C08 ADD R9,R9,#7 */
#define PUFO_BFA_SIDE_H         7   /* 0x92B0C MOV R5,#7 */
#define PUFO_BFA_SIDE_X0        2   /* 0x92B2C MOV R1,#2 */
#define PUFO_BFA_SIDE_X1      237   /* 0x92B84 MOV R1,#0xED */
#define PUFO_BFA_SIDE_Y0        1   /* 0x92B14 ADD R10,R9,#1 */
#define PUFO_BFA_SIDE_Y1      151   /* 0x92B48 ADD R11,R9,#0x97 */
#define PUFO_BFA_EDGE_STEP      5   /* 0x92CD8 ADD R9,R9,#5 */
#define PUFO_BFA_EDGE_W         5   /* 0x92C44 MOV R3,#5 */
#define PUFO_BFA_EDGE_Y_TOP   315   /* 0x92C3C MOV R2,#0x138 / 0x92C4C ORR R2,R2,#3 */
#define PUFO_BFA_EDGE_Y_BOT     2   /* 0x92C98 MOV R2,#2 */
#define PUFO_BFA_EDGE_X_RISE    1   /* 0x92C38 ADD R11,R9,#1, used by both rows */
#define PUFO_BFA_EDGE_X_FALL  128   /* 0x92C80 ADD R1,R9,#0x80 -- the top row's falling ramp */
#define PUFO_BFA_EDGE_X_BOT   120   /* 0x92CC4 ADD R1,R9,#0x78 -- the bottom row's, 8 pixels left of it */
#define PUFO_BFA_SEP_X          2   /* 0x92D04 MOV R1,#2 */
#define PUFO_BFA_SEP_Y         35   /* 0x92CFC MOV R2,#0x23 */
#define PUFO_BFA_SEP_W        208   /* 0x92CF4 MOV R3,#0xD0 */
#define PUFO_BFA_SEP_COLOR    160   /* 0x92CE8 MOV R0,#0xA0, the same index in all three colour slots */
#define PUFO_BFA_CTLS_2         2   /* 0x92D90 CMP #2 */
#define PUFO_BFA_GLYPH_PAL      3   /* 0x92D18 MOV R0,#3 -> Map_nGlyphPalBase, never restored here */
#define PUFO_BFA_STRIP_MARK   (-1)  /* 0x92D38 MOV R1,#0xFFFFFFFF: no base is marked for the chevron, so the
                                     * arm overwrites whatever the env's nHighlightRow held */

struct FacNameGridEnv;
struct BuildDialogEnv;
struct BaseInfoDrawEnv;   /* pufo_baseui.h: BaseInfoScreen_Draw's env */
struct FundingDrawEnv;    /* pufo_baseui.h: FundingScreen_DrawExtended's env */

typedef struct GeoscapeBaseFacArmEnv {
    PadImage          *pPad;         /* pBaseFacScreenPad @ 0x186BA0: allocated in Geoscape_InitUI (0x859EC..0x85A00)
                                      * and loaded from the "research" member (0x860C8..0x860D4, mode 6) */
    UiControl *const  *apPanelCtl;   /* gBaseFacBuildBlock[+4] / [+8] @ 0x158874 / 0x158878 */
    const u8          *pbPlaceArmed; /* bBaseFacPlaceArmed @ 0x158888 -- set means NO control blit at all */
    UiControlBlitFn    pfnCtlBlit;
    UiControlBlitFn    pfnCtlBlitEx;
    void              *pCtlBlitUser;
} GeoscapeBaseFacArmEnv;

/* 0x92AF0..0x92DAC: screen 7 (and screen 1's pad). Rails, separator, glyph base 3, the three callees, then the
 * two page controls unless a placement is armed. */
void Geoscape_DrawBaseFacArm(const GeoscapeBaseFacArmEnv *pArmEnv, const struct FacNameGridEnv *pGridEnv,
                             const struct BaseStripDrawEnv *pStripEnv, const struct BuildDialogEnv *pDialogEnv);

/* ---- the base-info arm, 0x93F08..0x94104 (Geoscape_Draw case 3) ---------------------------------
 *
 * Screen 3 is the base-info screen: BaseInfoScreen_Open @ 0x6167C stores 3 into nGeoscapeScreen at
 * 0x6169C and runs BaseInfoScreen_Init @ 0x57688. The frame is case 7's rail idiom inset by three:
 * 1x7 rects at x 5 and 234 (0x93F48 MOV R1,#5 / 0x93F94 MOV R1,#0xED) instead of 2 / 237, and 5x1 rects
 * at y 5 and 315 (0x94004 MOV R2,#5, then 0x9401C MOV R2,#0x138 / 0x9402C ORR R2,R2,#3) instead of
 * 315 / 2. Same 24 steps (CMP #0xA8 at 0x93FD4 over steps of 7, CMP #0x78 at 0x940A4 over steps of 5) and
 * the same colours (climbing 5*(k+20), side falling 0xE6-5*k, edge falling 0xFA-5*k). Unlike case 7 the
 * two edge ramps come from ONE immediate: 0x9404C ADD R9,R8,#0x77 gives x = 5k+119 and is used at 0x94064
 * for the y 5 row and again at 0x9408C for the y 315 row, so the climbing ramp (x 1..120) and the falling
 * one (x 119..238) OVERLAP by two columns instead of leaving case 7's 7-pixel hole.
 *
 * The pad is *pBaseInfoPad @ 0x158828 (0x93F08 LDR / 0x93F0C LDR [R0] / 0x93F10 BL PadImage_Blit) and the
 * control run walks the two words AFTER it in the same block: 0x940B8..0x940D4 is ADD R3, R1, R0,LSL#2
 * with R1 = the ADDRESS of pBaseInfoPad, then LDR R0,[R3,#4] -- 0x15882C then 0x158830, the two controls
 * BaseInfoScreen_Init allocates at 0x576DC..0x57708 (BaseInfoScreen_OnOk, and the panel-mode toggle).
 * One global is therefore both a pad pointer and a control-array base; IDA names only 0x158828 and types
 * it PadImage *. The loop counter is the shared scratch dword_186B08 again (0x940B0 STR 0, re-read and
 * incremented per step at 0x940C8..0x940D0, left at 2) -- the port counts locally.
 *
 * Then the close control, but only while byte 0x1870F8 is 0: 0x940DC LDRB / 0x940E4 ANDS #0xFF sets the
 * flags and the predicated LDREQ / BLEQ at 0x940EC / 0x940F0 make the whole blit conditional. That byte is
 * shared by the base-info and psi screens (written at 0x557D0 in BaseInfoScreen_OnOk, 0x57614 in
 * BaseInfoScreen_Draw, 0x57794 in BaseInfoScreen_Init, and at 0x716B0 / 0x72A2C in the psi pair).
 *
 * Last, BaseInfoScreen_Draw @ 0x56FB8 (0x940F4..0x94100), and 0x94100 is its ONLY call site in the binary:
 * R0 = pBaseInfoPad (the block base again) and R1 = byte_1870F4 read SIGNED, the selected-base index the
 * psi screen and Geoscape_CloseUI share. */

#define PUFO_BI_RAIL_STEPS   24   /* 0x93FD4 CMP #0xA8 over steps of 7, 0x940A4 CMP #0x78 over steps of 5 */
#define PUFO_BI_SIDE_STEP      7   /* 0x93FCC ADD R9,R9,#7 */
#define PUFO_BI_SIDE_H         7   /* 0x93F20 MOV R5,#7 stored as nRectH at 0x93F28 */
#define PUFO_BI_SIDE_X0        5   /* 0x93F48 MOV R1,#5 */
#define PUFO_BI_SIDE_X1      234   /* 0x93F94 MOV R1,#0xED */
#define PUFO_BI_SIDE_Y0        1   /* 0x93F34 ADD R11,R9,#1 */
#define PUFO_BI_SIDE_Y1      151   /* 0x93F60 ADD R2,R9,#0x97 */
#define PUFO_BI_EDGE_STEP      5   /* 0x9409C ADD R8,R8,#5 */
#define PUFO_BI_EDGE_W         5   /* 0x93FFC MOV R3,#5 */
#define PUFO_BI_EDGE_Y_LO      5   /* 0x94004 MOV R2,#5 -- case 7 uses y 2 here */
#define PUFO_BI_EDGE_Y_HI    315   /* 0x9401C MOV R2,#0x138 / 0x9402C ORR R2,R2,#3 */
#define PUFO_BI_EDGE_X_RISE    1   /* 0x93FF8 ADD R11,R8,#1 */
#define PUFO_BI_EDGE_X_FALL  119   /* 0x9404C ADD R9,R8,#0x77 -- one immediate used by BOTH rows */
#define PUFO_BI_CTLS_2         2   /* 0x940D4 CMP #2 */

typedef struct GeoscapeBaseInfoArmEnv {
    PadImage **ppPad;          /* pBaseInfoPad @ 0x158828 -- also the block base the callee receives */
    const u8  *pbOverlayShown; /* unk_1870F8 @ 0x1870F8: non-zero suppresses the close-control blit */
    UiControl *pCloseCtl;      /* g_pUiCloseCtl */
} GeoscapeBaseInfoArmEnv;

/* 0x93F08..0x94104: screen 3. Pad, rails, two control slots of the pad's own block, the close control
 * while 0x1870F8 is clear, then BaseInfoScreen_Draw with the selected base index read signed. */
void Geoscape_DrawBaseInfoArm(const GeoscapeBaseInfoArmEnv *pArmEnv, const GeoscapeScreenArmEnv *pCtlEnv,
                              const struct BaseInfoDrawEnv *pBaseInfoEnv);

/* ---- the funding arm, 0x94314..0x9449C (Geoscape_Draw case 19) ----------------------------------
 *
 * Screen 19 is the funding screen (FundingScreen_Open @ 0x5B560). The arm is the monthly report, then a
 * bankruptcy panel that only appears when the projected balance is far enough in the red.
 *
 * 0x94314 loads the ADDRESS of pFundPad @ 0x1595DC and calls FundingScreen_DrawExtended without
 * dereferencing it: the callee reads the global itself (0x5B5CC LDR / 0x5B5D0 LDR [R0]), so R0 is a
 * discarded argument.
 *
 * 0x9431C..0x94374 sums Base_CalcMonthlyUpkeep over the bases and subtracts it from nFunds:
 * R6 = nFunds @ 0x1598D8 (read once, 0x94334), the loop counter lives in the shared scratch dword_186B08
 * (0x94338 STR 0, 0x94364..0x9436C per step), the base count is abFacilityGrid + 0x618 @ 0x159D80 read
 * SIGNED and re-read on every iteration (0x94330 and 0x9435C), and the bound test compares the counter
 * against that re-read count (0x94370 CMP / BLT). The callee is handed R0 = abFacilityGrid (0x9434C) and
 * R1 = the counter truncated to a signed byte (0x94344 MOV R3,R0,LSL#24 / 0x94348 MOV R1,R3,ASR#24).
 * The same loop, with the same re-read quirk, is what Geoscape_CloseUI case 19 runs at 0x8BB74..0x8BB90.
 *
 * 0x94378..0x943AC builds the projected balance: nFunds-minus-upkeep plus g_nFundBar2 + [1] + [3] + [0] +
 * [4] @ 0x186594..0x1865A4, in THAT order, then CMP against 0xFFD23940 and BGE past the whole panel. The
 * constant is -3,000,000 signed -- the same limit Geoscape_CloseUI uses at 0x8BBE8 (PUFO_FUNDS_BANKRUPT_LIMIT)
 * -- and BGE is unsigned, which for this constant is the same test as signed >= -3,000,000. So the panel is
 * drawn only when the balance is worse than -3,000,000.
 *
 * 0x943B0..0x943EC is the panel frame: Ui_AnimExpandBlit over the geoscape anim root (0x1864F8) at (2, 80)
 * size (232, 90), mode 4 (the centred 50*progress square), index 0, source origin 0x28/0x32, key 0xDC.
 * 0x943F0 ANDS R3,R0,#0xFF / BEQ skips everything after it unless the blit reports the animation complete.
 * Then one colour pair (150,50,50) over (50,1,1) at 0x943F8..0x94418 -- packed once for all three lines --
 * and three centred captions on x 120: TEXTS[1712] @ 0x1093C0 at y 100, TEXTS[1713] @ 0x10948D at y 120 and
 * TEXTS[1714] @ 0x10955A at y 140, the last one printed by jumping into the shared centring tail at
 * 0x92ACC (which centres R0 and falls through to MOV R0,R4 / BL Text_DrawString / B def_8C1B0 at 0x90974).
 * The centring is the floor form again: MOVS R1,R0 / ADDMI R1,R1,#1 / ASR #1 / RSB R1,R1,#0x78.
 *
 * In the shipped lang.dat the region around records 1712..1714 holds control bytes (0x1B, 0x01, '!', 0x05)
 * rather than words -- the readable part of the table ends near record 1640 -- so the panel's three lines
 * are what the loader left there, not a sentence. Recorded rather than invented. */

#define PUFO_FUND_PANEL_X        2   /* 0x943E8 MOV R1,#2 */
#define PUFO_FUND_PANEL_Y       80   /* 0x943E4 MOV R2,#0x50 */
#define PUFO_FUND_PANEL_W      232   /* 0x943DC MOV R3,#0xE8 */
#define PUFO_FUND_PANEL_H       90   /* 0x943D0 MOV R3,#0x5A stored as nH */
#define PUFO_FUND_PANEL_IDX      0   /* 0x943C8 MOV R3,#0 */
#define PUFO_FUND_PANEL_SRC_X   0x28 /* 0x943C0 MOV R3,#0x28 */
#define PUFO_FUND_PANEL_SRC_Y   0x32 /* 0x943E0 MOV R5,#0x32 */
#define PUFO_FUND_PANEL_KEY     0xDC /* 0x943B8 MOV R3,#0xDC */
#define PUFO_FUND_PANEL_MODE     4   /* 0x943B0 MOV R3,#4 */
#define PUFO_FUND_CAP_Y0      100    /* 0x94444 MOV R2,#0x64 */
#define PUFO_FUND_CAP_Y1      120    /* 0x94474 MOV R2,#0x78 */
#define PUFO_FUND_CAP_Y2      140    /* 0x94498 MOV R2,#0x8C */
#define PUFO_FUND_CAP_CENTRE  120    /* RSB R1,R1,#0x78 in all three */
#define PUFO_FUND_TEXT_CAP0  1712    /* unk_1093C0 */
#define PUFO_FUND_TEXT_CAP1  1713    /* unk_10948D */
#define PUFO_FUND_TEXT_CAP2  1714    /* unk_10955A */

typedef struct GeoscapeFundingArmEnv {
    const u8              *pbBaseCount;   /* abFacilityGrid + 0x618 @ 0x159D80, read SIGNED (0x94330) */
    const i32             *pnFunds;       /* nFunds @ 0x1598D8, read once (0x94334) */
    const i32             *anFundBar;     /* g_nFundBar0..4 @ 0x186594, summed in the original's order */
    const u8              *abFacilityGrid;/* abFacilityGrid @ 0x159768, the hand the upkeep callee gets */
    Ship *const           *apCraft;
    i32                    nCraftCount;
    Soldier *const        *apSoldiers;
    i32                    nSoldierCount;
    const SmapSite        *apSites;
    i32                    nSiteCount;
    ObjectRecFull *const  *apObjects;
    const UiAnimBlitEnv   *pAnim;         /* the started flag 0x187080 and the progress byte 0x187058 */
    const GfxColorEnv     *pColor;        /* pGX @ 0x1139B8 */
    const char            *pTextRecs;     /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart, for the three captions */
} GeoscapeFundingArmEnv;

/* 0x94314..0x9449C: screen 19. The monthly report, the upkeep sum, the -3,000,000 gate, the animated
 * panel frame, and three centred captions when the animation reports itself complete. */
void Geoscape_DrawFundingArm(struct FundingDrawEnv *pFundingEnv, const GeoscapeFundingArmEnv *pArmEnv);

/* ---- the hangar arm, 0x9309C..0x93DD4 (Geoscape_Draw case 6) ------------------------------------
 *
 * Screen 6 is the hangar (BasePlaneScreen_Open @ 0x7a514 stores 6 into nGeoscapeScreen). R10 is the
 * ADDRESS of pPlaneBackdropPad @ 0x161A48 for the whole arm, so the pad word is also the block base the
 * controls are read out of: +0x30..+0x3C are the four crew/row controls, +0x40..+0x58 the header plus the
 * six stat controls, +0x5C / +0x60 pPlaneNavUpCtl / pPlaneNavDownCtl and +0x64 / +0x68
 * pPlaneStoreNavPrevCtl / pPlaneStoreNavNextCtl. R4 is the Map context 0x1139B8 (0x8C19C) and R7 the
 * geoscape UI root block 0x1864F8 whose +0x35 byte is bBaseFacPreviewOk @ 0x18652D.
 *
 * 0x930A0 blits *pPlaneBackdropPad. 0x930A8..0x930F0 then feeds the map cursor to the two store-list
 * scroll panels -- only with bPlaneStoresCommitted @ 0x161F28 SET and the root block's +0x35 byte
 * non-zero (0x930B8 LDRB [R7,#0x35] / CMP #0 / BEQ), both cursors read LDRSH. Then
 * BasePlaneScreen_Draw @ 0x7d374 at 0x930F8, again with R0 = the block base.
 *
 * 0x930FC..0x93184 is the control prefix, gated on bPlaneShowCraftDetail @ 0x161F20 (0x93114 BEQ jumps
 * past ALL of it): the store pair at +0x64 while bPlaneStoresCommitted is set (0x93120, CMP #2 at
 * 0x93144), then the nav pair at +0x5C while bPlaneListPanelsBuilt @ 0x161F24 is set (0x93158, CMP #2 at
 * 0x9317C). The craft hand is apCraft @ 0x114A64 = the Map context's +0x10AC (0x9318C MOV R6,#0x1080 /
 * 0x93194 ORR #0x2C), indexed by nSelectedCraftIdx @ 0x161AC4.
 *
 * 0x93198..0x93290, the crew slots: with ALL FOUR flags clear (bStatPanelAOpen @ 0x161F18,
 * bStatPanelBOpen @ 0x161F1C, bPlaneListPanelsBuilt, bPlaneStoresCommitted) and Ship.bCrewSlots @ +0x14
 * above zero, one crew control per slot at +0x30 + 4*i, each followed by the colour pair (68,244,252)
 * over (4,70,75) and the two slot markers -- "1" at (28,103) whenever the slot count is above 0, "2" at
 * (205,103) when it is above 1, both predicated HI on the SAME re-read byte, so a 2-slot craft paints
 * both markers twice, at the very same pixels.
 *
 * 0x93294..0x9337C, the two captions: with Ship.bCanDogfight @ +0x21 non-zero AND both panel flags clear,
 * the last two crew controls (+0x30 + 4*i for i 2..3) each blit, then TEXTS[97] "Экипаж" centred on x 53
 * at y 201 and TEXTS[98] "Оборуд." centred on 53 at y 241 -- measured and drawn INSIDE the two-step loop,
 * so both captions are drawn twice.
 *
 * 0x93380..0x93D74, the stat panel, reached from four different jumps and gated on either stat panel byte
 * being set (0x9338C BNE / 0x9339C BEQ to the arm's end): 226x1 row bars at x 7, y 92 + 20*i and
 * 108 + 20*i for i 0..nPlaneStatRowCount-1 (0x933EC, 0x93420), then the header column TEXTS[99] "Нет"
 * centred on x 73 at y 92 with the header control at +0x40 blitted BETWEEN the measure and the draw
 * (0x93454..0x93488). Six rows follow in weapon order, each gated on its own count being above zero, and
 * each writing the control's y (STRH at +2) to 92 + 20*n before it blits. Every centring uses the width
 * TRUNCATED TO A SIGNED BYTE (0x934E0 MOV R3,R0,LSL#24 / 0x934E4 MOV R0,R3,ASR#24, then STRB into the
 * scratch byte at 0x186C24 and LDRSB back out at the header): the three columns are the weapon name on 73,
 * the count on 163 and the per-base stock of the weapon's own ammo object on 207 -- apObjects[52] for the
 * craft cannon, [53] Stingray, [54] Avalanche, [105] (Elerium-115) for the plasma cannon and [55] for the
 * fusion ball, exactly the resupply mapping Ship_ResupplyCraft documents at pufo_ship.h:488. The laser
 * cannon has no ammo object and draws the single "-" at (205, y) with no measure at all (0x93D98).
 *
 * The rows share the scratch counter with everything above, and a row advances it TWICE when its count is
 * above zero (the unconditional STR at 0x9349C and the second one at 0x93600..0x93608) -- so the three
 * column rails at the arm's end are 20 * (6 + matched-rows) tall, not 20 * nPlaneStatRowCount. ORIGINAL
 * BUG, reproduced; see the ledger row for 0x93604. */

#define PUFO_PLANE_ARM_MAIN_FIRST     0   /* block +0x30 (0x161A78): the four crew/row controls */
#define PUFO_PLANE_ARM_MAIN_LAST     4   /* 0x93378 CMP R0,#4 */
#define PUFO_PLANE_ARM_WEAPON_FIRST  4   /* block +0x40 (0x161A88): the header plus six stat controls */
#define PUFO_PLANE_ARM_LIST_FIRST   11   /* block +0x5C: pPlaneNavUpCtl / pPlaneNavDownCtl */
#define PUFO_PLANE_ARM_STORE_FIRST  13   /* block +0x64: pPlaneStoreNavPrevCtl / pPlaneStoreNavNextCtl */
#define PUFO_PLANE_ARM_CTLS         15   /* the block's control words, +0x30..+0x68 (0x161A78..0x161AB0) */
#define PUFO_PLANE_STAT_ROWS         6   /* the six weapon rows, in slot order 25..30 */
#define PUFO_PLANE_ROW_STEP         20   /* 0x933AC MOV R7,#0x14 */
#define PUFO_PLANE_STAT_Y0          92   /* 0x933DC ADD R2,R2,#0x5C: the row bars and the header line */
#define PUFO_PLANE_STAT_Y1         108   /* 0x93410 ADD R2,R2,#0x6C: the second bar of each row */
#define PUFO_PLANE_STAT_W         226   /* 0x933E0 MOV R3,#0xE2 */
#define PUFO_PLANE_STAT_X           7   /* 0x933E4 MOV R1,#7 */
#define PUFO_PLANE_COL_NAME        73   /* RSB R1,R1,#0x49: the weapon-name column */
#define PUFO_PLANE_COL_COUNT      163   /* RSB R1,R1,#0xA3: the count column */
#define PUFO_PLANE_COL_STOCK      207   /* RSB R1,R1,#0xCF: the per-base stock column */
#define PUFO_PLANE_COL_DASH       205   /* 0x939DC MOV R1,#0xCD: the laser cannon's "-" */
#define PUFO_PLANE_CREW_ONE_X      28   /* 0x9323C MOVHI R1,#0x1C */
#define PUFO_PLANE_CREW_TWO_X     205   /* 0x93264 MOVHI R1,#0xCD */
#define PUFO_PLANE_CREW_Y         103   /* 0x93238 MOVHI R2,#0x67 */
#define PUFO_PLANE_CAP_CREW_Y     201   /* 0x93330 MOV R2,#0xC9 */
#define PUFO_PLANE_CAP_EQUIP_Y    241   /* 0x9335C MOV R2,#0xF1 */
#define PUFO_PLANE_CAP_CENTRE      53   /* RSB R1,R1,#0x35 in both captions */
#define PUFO_PLANE_RAIL_Y          93   /* 0x93D08 MOV R2,#0x5D */
#define PUFO_PLANE_RAIL_X0        186   /* 0x93D10 MOV R1,#0xBA */
#define PUFO_PLANE_RAIL_X1        141   /* 0x93D24 MOV R1,#0x8D */
#define PUFO_PLANE_RAIL_X2        228   /* 0x93D50 MOV R1,#0xE4 */
#define PUFO_PLANE_ARM_TEXT_CREW      97   /* lang.dat "Экипаж" */
#define PUFO_PLANE_TEXT_EQUIP      98   /* "Оборуд." */
#define PUFO_PLANE_TEXT_STAT_HDR   99   /* "Нет", the stat panel's header column */
#define PUFO_PLANE_TEXT_STAT0     100   /* "Кораб. пушки", then 101..105 in row order */
#define PUFO_PLANE_AMMO_SLOT_NONE  (-1) /* the laser cannon: no ammo object, so a dash */

struct PlaneDrawEnv; /* pufo_plane.h owns the definition; the tag is enough for the pointer */

typedef struct GeoscapePlaneArmEnv {
    PadImage          **ppBackdropPad;   /* pPlaneBackdropPad @ 0x161A48, also the control block's base */
    UiControl *const   *apCtl;           /* the block's control words from +0x30, PUFO_PLANE_ARM_* indices */
    const u8           *pbStoresCommitted;   /* bPlaneStoresCommitted @ 0x161F28 */
    const u8           *pbListPanelsBuilt;   /* bPlaneListPanelsBuilt @ 0x161F24 */
    const u8           *pbShowCraftDetail;   /* bPlaneShowCraftDetail @ 0x161F20 */
    const u8           *pbStatPanelAOpen;    /* bStatPanelAOpen @ 0x161F18 */
    const u8           *pbStatPanelBOpen;    /* bStatPanelBOpen @ 0x161F1C */
    const u8           *pbPreviewOk;         /* bBaseFacPreviewOk @ 0x18652D, the root block's +0x35 byte */
    UiPanel           **ppSelRowPanel;       /* pPlaneSelRowPanel @ 0x161F00 */
    UiPanel           **ppCommitRowPanel;    /* pPlaneCommitRowPanel @ 0x161EEC */
    const i16          *pnCursorX;           /* nSmapCursorX, LDRSH at 0x930D4 */
    const i16          *pnCursorY;           /* nSmapCursorY, LDRSH at 0x930D0 */
    const i32          *pnSelCraftIdx;       /* nSelectedCraftIdx @ 0x161AC4 */
    const i8           *pnSelBaseIdx;        /* nSelectedBaseIdx @ 0x159D81, read SIGNED (0x93590) */
    Ship *const        *apCraft;             /* apCraft @ 0x114A64 = the Map context's +0x10AC */
    i32                 nCraftCount;         /* Map_nCraftCount @ 0x114B4C. The original indexes apCraft with the
                                              * raw nSelectedCraftIdx dword (0x931D8) with no test; the port bounds
                                              * it the way BasePlaneScreen_Draw bounds the same array. */
    ObjectRecFull *const *apObjects;         /* apObjects @ 0x113AC0 = the Map context's +0x108 */
    i32                 nObjectCount;        /* Map_nObjectCount @ 0x114A60, the ammo slots' array bound */
    GfxColorEnv        *pColor;              /* pGX @ 0x1139B8, the pair every row packs */
    const i32          *pnRowCount;          /* nPlaneStatRowCount @ 0x161A38 */
    const i32          *pnStats;             /* the six counts, row order: CraftCannon, Stingray, Avalanche,
                                              * LaserCannon, PlasmaCannon, FusionBall */
    const char         *pTextRecs;           /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart */
    const struct PlaneDrawEnv *pPlaneEnv;    /* BasePlaneScreen_Draw @ 0x7d374, called with the block base */
    const GeoscapeScreenArmEnv *pCtlEnv;     /* the blit seam every control here goes through */
} GeoscapePlaneArmEnv;

/* 0x9309C..0x93DD4: screen 6. Pad, the two store-list tracking calls, the hangar draw, the two paired
 * control runs, the crew slots and their markers, the two captions, then the stat panel's bars, header,
 * six rows and three column rails. */
void Geoscape_DrawPlaneArm(const GeoscapePlaneArmEnv *pArmEnv);

/* ---- the placement arm, 0x91DC8..0x92ADC (Geoscape_Draw case 1) ------------------------------------
 *
 * Screen 1 is Base_EnterBuildMode (0x84E60) -- the base/hangar placement screen. It shares its pad with
 * case 7 (0x91DCC and 0x92AF4 both blit *pBaseFacScreenPad @ 0x186BA0) and it shares its rails with case
 * 7's idiom, but not with case 7's numbers, and it is the only screen arm that MUTATES the map cursor.
 *
 * Side rails (0x91DD4..0x91EEC). 24 steps of 7 (0x91EDC ADD R9,R9,#7 / 0x91EE0 CMP #0xA8), 1x7 rects, and
 * THREE x positions where case 7 has two: x 2 (0x91E04), x 237 (0x91E58) and x 210 (0x91EA0) -- the extra
 * rail is the divider between the map area and the ten action captions. Each x gets the climbing rect at
 * y 7k+1 in colour 5*(k+20) and the falling rect at y 7k+151 in colour 0xE6-5*k, so six blits per step.
 * The step counter is read back as a BYTE (0x91DE8 LDRB R8,[var_1A8]) exactly as case 7 reads it.
 *
 * Top and bottom rails (0x91EF0..0x91FB4). 24 steps of 5 (0x91FAC / 0x91FB0 CMP #0x78), 5x1 rects at
 * y 315 (0x91F0C MOV R2,#0x138 / 0x91F20 ORR #3) and y 2 (0x91F68). The LEFT half of both rows uses the
 * flat colour 7 -- and 7 is not a colour choice, it is the leftover side-rail HEIGHT constant: 0x91DE4
 * MOV R5,#7 is `MOV R5,#7` for the 1x7 side rects and 0x91F14 / 0x91F64 / 0x91F6C STRB R5 put that same 7
 * into all three colour slots. The RIGHT half uses the falling ramp 0xFA-5*k (0x91F30 MUL / 0x91F3C RSB
 * #0xFA), starting at x k+128 on the top row (0x91F50 ADD R1,R9,#0x80) and x k+120 on the bottom one
 * (0x91F94 ADD R1,R9,#0x78) -- the same eight-pixel asymmetry case 7 has, here with the rising half
 * ending at x 120 and the falling half starting at 128 (top) so the top row gaps at x 121..127.
 *
 * Separator (0x91FB8..0x91FDC): 208x1 at (2,35) in flat colour 160, identical to case 7's.
 *
 * The ten nav controls (0x91FE0..0x92014). The arm stores 0 into the shared scratch counter dword_186B08,
 * then for i = 0..9 (0x92010 CMP #0xA) reads the pointer at abFacilityGrid + 0x120 + 4*i -- the ten nav
 * controls at 0x159888..0x1598AC, the same array Geoscape_BuildNavControls fills and Geoscape_CloseUI
 * case 1 frees -- tests it for null (0x91FF8 CMP #0 / 0x91FFC BEQ, so a null entry is skipped, not a
 * fault) and blits it. The counter is re-read after each blit (0x92004) and left at 10.
 *
 * Close control (0x92018..0x92044): g_pUiCloseCtl to (194, 4) (0x9201C MOV R3,#0xC2 / 0x92020 MOV R9,#4)
 * with click = Geoscape_CloseUI_ThunkA @ 0x838d0 (0x92030 LDR / 0x9203C STR [R1,#0xC]), then blitted.
 *
 * The ten action captions (0x92048..0x92290), each measured and drawn with the house centring
 * (MOVS / ADDMI #1 / ASR #1 / RSB). Colour pair (68,244,252) over (4,70,75). Two columns, centre 60 and
 * 180, rows y 229, 247, 265, 283, 301 -- records 69 "Новая база", 70 "Инфо базы", 71 "Солдаты",
 * 72 "Оборуд. корабля", 73 "Строить модули" on the left, 74 "Исследования", 75 "Производство",
 * 76 "Перевозка", 77 "Купить/Нанять", 78 "Продать/Уволить" on the right. The y values come from split
 * immediates again (0x920F0 MOV R2,#0x108 / 0x92104 ORR #1 -> 265, 0x92238 #0x118 / 0x9224C ORR #3 -> 283,
 * 0x92270 #0x12C / 0x92284 ORR #1 -> 301).
 *
 * Map_nGlyphPalBase = 3 (0x92294 MOV R0,#3 / 0x922AC STR, never restored here, exactly as case 7), then
 * BaseFacScreen_DrawNameGrid with the grid base and nSelectedBaseIdx read SIGNED (0x922A0 LDRSB / 0x922B4),
 * then Base_DrawFacilityIconStrip with the mark -1 (0x922B8 MOV R1,#0xFFFFFFFF / 0x922C0) -- the same dead
 * argument the case-7 arm has, recorded once in the ledger for that call.
 *
 * Base name (0x922E8..0x92344), only while bBasePlaceActive @ 0x1870BC is CLEAR (0x922EC LDRB / 0x922F4
 * BNE): apSiteRecs[nSelectedBaseIdx]'s name at +26 (0x92308 LDR R7,=0x186C40 / 0x9230C ADD R7,R3,LSL#6 /
 * 0x92310 ADD #0x1A -- eight 64-byte records), centred on 120 at y 1. Colour pair (130,130,250) over
 * (20,20,20) (0x92348..0x92368).
 *
 * Money line (0x92370..0x923D4): TEXTS[156] "Деньги: " copied into the frame's psz, strcat itoa(nFunds),
 * strcat "$" (aDollarSign), drawn at a FIXED (5, 20) -- not centred, unlike everything else in this arm.
 *
 * The cursor block (0x923D8..0x92ADC), only with bBasePlaceActive SET (0x923DC ANDS / 0x923E0 BEQ). The
 * cell byte is `abFacilityGrid[372 + 144*base + 6*tapY + tapX]`, i.e. fac[base].tileA[tapY][tapX]: 0x923EC
 * LDRSB nSelectedBaseIdx, 0x923F0 MOV R2,#0x18, 0x923F8 MUL R2,R3,R2, 0x92400 LDR R1,[=nSmapTapY],
 * 0x9240C ADD #0x3E, 0x92414 MUL R2,R3,#6, 0x92418 ADD R3,R2,[=nSmapTapX], 0x9241C LDRB R3,[R3,=grid] --
 * the three immediates 24, 6 and 62 are exactly PUFO_GRID_FAC_STRIDE, the 6-wide layer and
 * PUFO_GRID_FAC_OFF (24*62 = 372 = 0x174). What the byte says decides everything below it:
 *
 *   - 0      -> 0x92844 STRB R11,[R10] clears bBasePlaceActive and the whole block stops there.
 *   - <= 8, or >= 13 -> loc_925AC draws the ONE-CELL cursor: 32x2 at (originX+32*tapX, originY+32*tapY+36),
 *     2x32 at the same x, 2x32 at x+31 (0x9267C ADD #0x1F), 32x2 at y+67 (0x926BC ADD #0x43).
 *   - 9..12  -> loc_92460 draws the TWO-CELL cursor -- 64x2 at y+36, 2x64 at x, 2x64 at x+63 (0x9252C ADD
 *     #0x3F), 64x2 at y+99 (0x92564 ADD #0x63) -- and FIRST moves the tap: code 10 does tapX -= 1
 *     (0x92458/0x9245C), code 11 does tapY -= 1 (0x925A0/0x925A4), code 12 does BOTH (0x92450/0x92454 then
 *     falling into 0x92458), code 9 does neither. The nudge lands BEFORE the rects are positioned, so the
 *     box is drawn at the moved tap. Codes 9..12 are FacilityKind 9..12 -- Hyper Wave Decoder, Mind
 *     Shield, Hangar, Gravity shield -- the kinds whose footprint is two cells across.
 *   Colour of all eight rects: index 0, mid and low 10*(nMapPhase+15) on one slot only -- 0x92478 MUL R2,R3,R7
 *   with R7 = 0xA and R3 = nMapPhase+0xF, written to nColorMid while nIdx and nColorLow stay 0 -- so the
 *   cursor pulses with the map phase.
 *
 * Shared tail (0x926D0..0x92ADC), run by both cursor styles and the reason this block is a draw at all:
 * colour pair (68,244,252) over (4,70,75); aBaseFacFlags[cellByte].szName drawn at (6,16) (0x92710 MOV
 * R11,#0x1C is the 28-byte record stride, 0x92740 MUL R2,R3,R11, 0x92744 ADD R3,R2,=0x186E48); pGeoscapeCtlA
 * @ 0x186BB4 gets click = Geoscape_OnMapClick_Thunk @ 0x857e8 (0x92764 STR [R0,#0xC]), x = the label
 * width truncated to a signed 16-bit value plus 10 (0x927A4 MOV R3,R0,LSL#16 / 0x927AC ASR#16 / 0x927B0 ADD
 * #0xA / 0x927B4 STRH), y = 15 (0x927B8), then blitted; then TEXTS[1240] "Демонтаж" measured and drawn
 * centred ON THE CONTROL: x = ctl.x + ctl.w/2 - width/2, y = ctl.y + 1 (0x92810 LDRH [R1,#4] / 0x92820
 * LSR #1 / 0x92824 SUB R0,LR,R0,ASR#1).
 *
 * Demolish-failure panel (0x92848..0x92ADC), gated on bBasePlaceFail @ 0x1870B8 (0x9284C LDRB / 0x92854
 * BEQ -- clear means the arm goes straight to the frame end). Ui_AnimExpandBlit with x 0, y 100, nW 236,
 * nH 50, index 0x82, source 200/70, key 100, mode 0 (the full rect); then the colour pair (220,80,80)
 * over (50,30,30) and three centred lines: TEXTS[1241] at y 104, 1242 at 118, 1243 at 132 -- "Невозможно
 * демонтировать" / "Модуль используется или" / "служит для связи другого с лифтом". Then a second pair
 * (255,255,255) over (104,104,104) -- the low colour is `(u8)0x159768`, the arm reuses the grid-block
 * pointer as a colour byte (0x929C0 STR R5,[g2]) -- and three more lines from TEXTS records 80, 81, 82 at
 * y 108, 125, 142, all centred on 120. Those three records are EMPTY in the shipped lang.dat, so the
 * second panel half draws nothing while the first half draws the message; recorded in the ledger.
 * Finally the last line is drawn through the shared centring tail at 0x90974 and 0x92ADC jumps to the
 * frame end (def_8C1B0), which is where case 1 -- and every other arm -- ends.
 *
 * Deviations: the counter of the ten-control run is a local (the original runs it in dword_186B08 and
 * re-reads it after each blit); the base index and the cell byte's aBaseFacFlags index are bounds-checked
 * (the original indexes both blindly, so a 0xFF cell byte reads past the 17 records); the money line's
 * three-step copy/itoa/strcat chain becomes one bounded snprintf over the same buffer size. */

#define PUFO_PLA_RAIL_STEPS    24   /* 0x91EE0 CMP #0xA8 over steps of 7, 0x91FB0 CMP #0x78 over steps of 5 */
#define PUFO_PLA_SIDE_STEP      7   /* 0x91EDC ADD R9,R9,#7 */
#define PUFO_PLA_SIDE_H         7   /* 0x91DE4 MOV R5,#7, also the edge rails' flat colour -- see above */
#define PUFO_PLA_SIDE_X0        2   /* 0x91E04 MOV R1,#2 */
#define PUFO_PLA_SIDE_X1      237   /* 0x91E58 MOV R1,#0xED */
#define PUFO_PLA_SIDE_X2      210   /* 0x91EA0 MOV R1,#0xD2: case 7 has no third rail */
#define PUFO_PLA_SIDE_Y0        1   /* 0x91DEC ADD R10,R9,#1 */
#define PUFO_PLA_SIDE_Y1      151   /* 0x91E20 ADD R2,R9,#0x97 */
#define PUFO_PLA_EDGE_STEP      5   /* 0x91FA8 ADD R9,R9,#5 */
#define PUFO_PLA_EDGE_W         5   /* 0x91F18 MOV R3,#5 */
#define PUFO_PLA_EDGE_Y_TOP   315   /* 0x91F0C MOV R2,#0x138 / 0x91F20 ORR R2,R2,#3 */
#define PUFO_PLA_EDGE_Y_BOT     2   /* 0x91F68 MOV R2,#2 */
#define PUFO_PLA_EDGE_X_RISE    1   /* 0x91F10 ADD R1,R9,#1 and 0x91F70, both rows */
#define PUFO_PLA_EDGE_X_TOP   128   /* 0x91F50 ADD R1,R9,#0x80 */
#define PUFO_PLA_EDGE_X_BOT   120   /* 0x91F94 ADD R1,R9,#0x78 */
#define PUFO_PLA_NAV_CTLS      10   /* 0x92010 CMP #0xA over the ten controls at 0x159888..0x1598AC */
#define PUFO_PLA_GLYPH_PAL      3   /* 0x92294 MOV R0,#3 -> Map_nGlyphPalBase, never restored here */
#define PUFO_PLA_STRIP_MARK    (-1) /* 0x922B8 MOV R1,#0xFFFFFFFF, the same dead mark case 7 passes */
#define PUFO_PLA_CLOSE_X      194   /* 0x9201C MOV R3,#0xC2 */
#define PUFO_PLA_CLOSE_Y        4   /* 0x92020 MOV R9,#4 */
#define PUFO_PLA_CAP_L          60   /* 0x9209C/0x920D0/0x92108/0x92140/0x92178 RSB #0x3C */
#define PUFO_PLA_CAP_R         180   /* 0x921AC/0x921E0/0x92218/0x92250/0x92288 RSB #0xB4 */
#define PUFO_PLA_CAP_Y0        229   /* 0x92098 MOV R2,#0xE5, the first row of both columns */
#define PUFO_PLA_CAP_STEP       18   /* 229, 247, 265, 283, 301 */
#define PUFO_PLA_TEXT_ACTION_0  69   /* "Новая база",   centred 60,  y 229 */
#define PUFO_PLA_TEXT_ACTION_5  74   /* "Исследования", centred 180, y 229 */
#define PUFO_PLA_TEXT_MONEY    156   /* aTextRec156 "Деньги: ", the prefix of the money line */
#define PUFO_PLA_MONEY_X         5   /* 0x923CC MOV R1,#5 */
#define PUFO_PLA_MONEY_Y        20   /* 0x923C8 MOV R2,#0x14 */
#define PUFO_PLA_NAME_OFF       26   /* 0x92310 ADD R3,R7,#0x1A, BaseSiteRec's name field */
#define PUFO_PLA_NAME_CENTRE   120   /* 0x9233C RSB #0x78 */
#define PUFO_PLA_NAME_Y          1   /* 0x92324 MOV R2,#1 */
#define PUFO_PLA_TAP_CELL_STRIDE  6  /* 0x92414 MUL R2,R3,#6: the 6-wide tileA layer */
#define PUFO_PLA_TAP_BASE_STRIDE 24  /* 0x923F0 MOV R2,#0x18 before the first multiply */
#define PUFO_PLA_TAP_ROW_BIAS   62   /* 0x9240C ADD R3,R3,#0x3E; 24*62 = 372 = PUFO_GRID_FAC_OFF */
#define PUFO_PLA_TAP_CELL       32   /* every tap step is 32 pixels: ADD R1,R0,R1,LSL#5 */
#define PUFO_PLA_TAP_Y0         36   /* 0x924D0/0x92518/0x92564... ADD #0x24, both cursor sizes */
#define PUFO_PLA_TAP_BIG_W      64   /* 0x92480 MOV R3,#0x40 and 0x924BC: the two-cell box */
#define PUFO_PLA_TAP_BIG_X      63   /* 0x9252C ADD R1,R1,#0x3F */
#define PUFO_PLA_TAP_BIG_Y      99   /* 0x92564 ADD R2,R2,#0x63 */
#define PUFO_PLA_TAP_SMALL_W    32   /* 0x925D0 MOV R3,#0x20 and 0x92610: the one-cell box */
#define PUFO_PLA_TAP_SMALL_X    31   /* 0x9267C ADD R1,R1,#0x1F */
#define PUFO_PLA_TAP_SMALL_Y    67   /* 0x926BC ADD R2,R2,#0x43 */
#define PUFO_PLA_TAP_KIND_FIRST  9   /* FacilityKind 9..12 take the two-cell cursor and the tap nudge */
#define PUFO_PLA_TAP_KIND_LAST  12
#define PUFO_PLA_TAP_COLOR_STEP 10   /* 0x92464 MOV R7,#0xA */
#define PUFO_PLA_TAP_COLOR_BASE 15   /* 0x92470 ADD R3,R2,#0xF on nMapPhase */
#define PUFO_PLA_FAC_NAME_X      6   /* 0x92718 MOV R1,#6 */
#define PUFO_PLA_FAC_NAME_Y     16   /* 0x92748 MOV R2,#0x10 */
#define PUFO_PLA_MAPCTL_X_ADD   10   /* 0x927B0 ADD R3,R3,#0xA on the truncated label width */
#define PUFO_PLA_MAPCTL_Y       15   /* 0x927B8 MOV R3,#0xF */
#define PUFO_PLA_TEXT_DEMOLISH 1240  /* unk_F19C8 "Демонтаж", centred over pGeoscapeCtlA */
#define PUFO_PLA_ANIM_X          0   /* 0x92894 MOV R1,#0 */
#define PUFO_PLA_ANIM_Y        100   /* 0x92890 MOV R2,#0x64 */
#define PUFO_PLA_ANIM_W        236   /* 0x92888 MOV R3,#0xEC */
#define PUFO_PLA_ANIM_H         50   /* 0x92880 MOV R7,#0x32 / 0x9288C STR R7,[nKey] */
#define PUFO_PLA_ANIM_IDX      130   /* 0x92878 STRB R3,[nIdx] with R3 = 0x82 */
#define PUFO_PLA_ANIM_SRCX     200   /* 0x92870 STRB R3,[srcX] with R3 = 0xC8 */
#define PUFO_PLA_ANIM_SRCY      70   /* 0x92868 STRB R3,[srcY] with R3 = 0x46 */
#define PUFO_PLA_ANIM_KEY      100   /* 0x92860 STRB R3,[var_1B0] with R3 = 0x64 */
#define PUFO_PLA_ANIM_MODE       0   /* 0x92884 STRB R5,[nMode]: the full rect */
#define PUFO_PLA_TEXT_FAIL_0  1241   /* unk_F1A95, y 104; 1242 (0xF1B62) at 118, 1243 (0xF1C2F) at 132 */
#define PUFO_PLA_FAIL_Y0        104
#define PUFO_PLA_FAIL_STEP       14
#define PUFO_PLA_TEXT_EMPTY_0     80 /* unk_B78E0, y 108; 81 (0xB79AD) at 125, 82 (0xB7A7A) at 142 -- all three
                                      * records are empty in the shipped lang.dat */
#define PUFO_PLA_EMPTY_Y0        108  /* 0x92A24 MOV R2,#0x6C; 125 (0x92A7C) and 142 (0x92AC8) after it */
#define PUFO_PLA_EMPTY_STEP       17
#define PUFO_PLA_PANEL_CENTRE    120 /* 0x92908/0x9295C/0x929B0/0x92A28/0x92A80/0x92AD8 RSB #0x78 */
#define PUFO_PLA_WHITE_LOW       104 /* 0x929C0 STR R5,[g2]: (u8)abFacilityGrid, the grid pointer reused as colour */

typedef struct GeoscapePlacementArmEnv {
    PadImage          *pPad;             /* pBaseFacScreenPad @ 0x186BA0, shared with case 7 */
    UiControl *const  *apNavCtl;         /* 0x159888..0x1598AC: NavControls.apNav, read at grid+0x120 + 4*i */
    UiControl         *pCloseCtl;        /* g_pUiCloseCtl, positioned then blitted */
    UiControl         *pMapCtl;          /* pGeoscapeCtlA @ 0x186BB4 */
    u8                *pbPlaceActive;    /* bBasePlaceActive @ 0x1870BC: the gate AND the byte 0x92844 clears */
    const u8          *pbPlaceFail;      /* bBasePlaceFail @ 0x1870B8: gates the demolish panel */
    i32               *pnTapX;           /* nSmapTapX @ 0x186B7C, mutable -- the cursor block moves it */
    i32               *pnTapY;           /* nSmapTapY @ 0x186B80, same */
    const i32         *pnSmapOriginX;    /* nSmapOriginX @ 0x159DC4 */
    const i32         *pnSmapOriginY;    /* nSmapOriginY @ 0x159DC8 */
    const u8          *abFacilityGrid;   /* abFacilityGrid @ 0x159768: the cursor cell and the nav slots */
    const i8          *pnSelBaseIdx;     /* nSelectedBaseIdx @ 0x159D81, always read SIGNED here */
    const u8          *pnMapPhase;       /* nMapPhase @ 0x1139EF, drives the cursor colour */
    const i32         *pnFunds;          /* nFunds @ 0x1598D8 */
    BaseSiteRec       *apSiteRecs;       /* 0x186C40, eight 64-byte records, name at +26 */
    const BaseFacFlag *paFacFlags;       /* aBaseFacFlags @ 0x186E48, 28-byte records */
    const char        *pTextRecs;        /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart */
    GfxColorEnv       *pColor;           /* pGX @ 0x1139B8 */
    const UiAnimBlitEnv *pAnim;          /* the demolish panel's frame state */
    void             (*pfnCloseClick)(void);  /* Geoscape_CloseUI_ThunkA @ 0x838d0 */
    void             (*pfnMapClick)(void);    /* Geoscape_OnMapClick_Thunk @ 0x857e8 */
} GeoscapePlacementArmEnv;

/* 0x91DC8..0x92ADC: screen 1, the placement screen. Rails, separator, the ten nav controls, the close
 * control, the ten action captions, the two grid callees, the base name and money line, then the cursor
 * block (which moves the tap) or the demolish panel, and the frame end. */
void Geoscape_DrawPlacementArm(const GeoscapePlacementArmEnv *pArmEnv,
                               const GeoscapeScreenArmEnv *pCtlEnv,
                               const struct FacNameGridEnv *pGridEnv,
                               const struct BaseStripDrawEnv *pStripEnv);

/* ---- Geoscape_Draw's own head, 0x8C180..0x8C1B0, and its cases 0/2 radar branch,
 *      0x8C214..0x8C29C ---------------------------------------------------------------------
 *
 * Geoscape_Draw @ 0x8c180 (33,744 B) IS the geoscape frame. Every block inside it was ported
 * before this one: the map chrome 0x8C2B0..0x8C690 (Geoscape_BlitMapView), the crosshair
 * 0x8C69C..0x8C760 (Geoscape_BlitCrosshair), the status panel and its flag chain
 * 0x8C760..0x91DB0 (Geoscape_DrawStatusPanel), the twenty screen arms (the Geoscape_Draw*Arm
 * functions above), and the frame end 0x944B4..0x94550 (Geoscape_DrawFrameEnd). What was missing
 * is the head that chooses between them: the 12-instruction prologue at 0x8C180 and the 21-entry
 * jump table at 0x8C1B0. That head is Geoscape_Draw below.
 *
 * 1. The head (0x8C180..0x8C1B0). 0x8C190 MOV R7,R0 keeps the caller's block pointer and 0x8C1A4
 *    stores it at [SP,#0x1C0+pObj]; twenty-three instructions read it back (0x8E644, 0x8F414,
 *    0x8FA38, 0x90DE0, 0x9193C, ...), and Render_Frame supplies 0x1864F8 at 0x2232C. 0x8C1A8
 *    stores zero into the stack byte var_1A4: 0x8D4DC is the only writer anywhere in the function
 *    (it stores 1 inside the '%' research walk) and 0x8D628 the only read, so every port block
 *    that needs the flag keeps it as a local. 0x8C194/0x8C1A0 load nGeoscapeScreen @ 0x186BC4,
 *    0x8C19C parks the Map context 0x1139B8 in R4 (the callee base everywhere below), and
 *    0x8C1AC `CMP R0,#0x14` / 0x8C1B0 `ADDLS PC,PC,R0,LSL#2` jump through the twenty-one branch
 *    words at 0x8C1B4..0x8C20C, which the two literal words 0x1139B8 (0x8C20C) and nGeoscapeScreen
 *    (0x8C210) follow.
 *
 *      id        target   the port function it reaches
 *      0, 2      0x8C214  Geoscape_Draw itself: the radar branch, or the map chrome + crosshair +
 *                         status panel (0x8C2B0 / 0x8C69C / 0x8C760, in that order -- the crosshair
 *                         reaches 0x8C760 both by the B at 0x8C730 and by 0x8C75C falling into it)
 *      1         0x91DC8  Geoscape_DrawPlacementArm
 *      3         0x93F08  Geoscape_DrawBaseInfoArm
 *      4         0x92DAC  Geoscape_DrawSoldierArm
 *      5         0x92EE4  Geoscape_DrawSoldierEditArm
 *      6         0x9309C  Geoscape_DrawPlaneArm
 *      7         0x92AF0  Geoscape_DrawBaseFacArm
 *      8         0x94114  Geoscape_DrawResearchArm
 *      9         0x9415C  Geoscape_DrawManufArm
 *      10        0x93E5C  Geoscape_DrawTransferArm
 *      11        0x93DD4  Geoscape_DrawBuySellArm
 *      12        0x93E20  Geoscape_DrawBuySellArm (the two prefixes are instruction-for-instruction
 *                         identical and share the call at 0x93E10)
 *      13, 14    0x944B8  no arm at all -- straight to the frame end
 *      15        0x94304  FundingScreen_Draw(*pFundPad @ 0x1595DC) and nothing else
 *      16        0x941AC  Geoscape_DrawUfopaediaArm
 *      17        0x94288  Geoscape_DrawPsiArm
 *      18        0x942F4  Geoscape_DrawScreen(aOptionsListCtl @ 0x159E20) and nothing else
 *      19        0x94314  Geoscape_DrawFundingArm
 *      20        0x944B0  BaseAtScreen_Draw, which the case reaches by FALLING into it at 0x944B4
 *      > 20      0x8C1B4  the default word, the same target as 13 and 14
 *
 *    Every arm's last instruction is a `B def_8C1B0` (0x8C29C, 0x92ADC, 0x9305C, 0x93F04, 0x94158,
 *    0x942FC, 0x9430C, ...) or a fall-through into it, so the frame end runs exactly once per frame
 *    for every id, and its GX_Present at 0x94538 is the frame's present -- which is why
 *    Render_Frame's geoscape branch (0x22328 / 0x22330) skips its own. BaseAtScreen_Draw at 0x944B4
 *    is inside case 20's fall-through ONLY: every other id lands on 0x944B8 past it, so the port
 *    gives the frame end a BaseAt env for id 20 and none for any other id.
 *
 * 2. Cases 0 and 2 (0x8C214..0x8C29C): the radar branch, taken when bRadarScreenActive @ 0x159678
 *    is set (0x8C214 loads its address into R8, 0x8C218 LDRB, 0x8C220 BEQ to the map chrome). It
 *    does NOT draw the map: no chrome, no crosshair, no status panel -- 0x8C29C goes straight to
 *    the frame end. Four steps:
 *      - 0x8C22C blits *pGeoscapeStripPad @ 0x186BBC (the radar session's own strip).
 *      - 0x8C240..0x8C258 blits the thirteen widgets of the root block. 0x8C238 `LDR R7,=0x1864F8`
 *        REPLACES the caller's R7 here, so this loop ignores Geoscape_Draw's own argument, and the
 *        loop counter is the shared scratch dword_186B08, stored zero at 0x8C23C and re-read every
 *        iteration -- the same counter the UI open paths and the flag chain's tail use.
 *      - 0x8C25C..0x8C290 blits the strip pad's OWN rectangle out of the logical surface:
 *        x = pad+0, y = pad+4, nRectW = pad+8, nRectH = pad+0xA, with the four stack words at
 *        [SP+0]=nIdx 60, [SP+4]=nColorMid 120, [SP+8]=nColorLow 130, [SP+0xC]=nKey 150 (the frame
 *        names are nKey/b2/g2/srcY and SoftSurf_BlitSrcRect @ 0x9ebf8 copies them back at 0x9EC14
 *        ..0x9EC34, which is what pins which is which). 0x8C260 re-loads the pad pointer after the
 *        control loop rather than reusing the one it blitted at 0x8C228.
 *      - 0x8C294 `MOV R0,R8` / 0x8C298 calls RadarScreen_Update with the ADDRESS of
 *        bRadarScreenActive -- the same byte whose value chose this branch, and whose value the
 *        callee tests again at 0x6059C before drawing anything.
 *
 * Deviations. (a) The original reaches every screen block through globals that are always live
 * here; the port takes them through the env and skips that arm when the env is missing -- which
 * only happens in a partial fixture, and every skip is the arm not drawing at all. (b) The radar
 * branch's thirteen-widget loop reads the block out of the literal 0x1864F8 (0x8C238), not out of
 * the caller's pointer; the port hands it the env's array, which is that same block on every call
 * path mapped so far. (c) The loop counters the original spills into dword_186B08 stay local, as
 * everywhere else in this port. */
#define PUFO_GEO_SWITCH_MAX      0x14 /* 0x8C1AC CMP R0,#0x14: ids 0..20 have a table word */
#define PUFO_GEO_RADAR_CTLS         13 /* 0x8C254 CMP R0,#0xD: the root block's thirteen widgets */
#define PUFO_GEO_RADAR_IDX        0x3C /* 0x8C27C MOV R3,#0x3C, stored at [SP+0] */
#define PUFO_GEO_RADAR_MID        0x78 /* 0x8C270/0x8C274: 120 at [SP+4] */
#define PUFO_GEO_RADAR_LOW        0x82 /* 0x8C268/0x8C26C: 130 at [SP+8] */
#define PUFO_GEO_RADAR_KEY        0x96 /* 0x8C25C/0x8C264: 150 at [SP+0xC] */

typedef struct GeoscapeRadarArmEnv {
    const u8 *pbRadarActive;      /* bRadarScreenActive @ 0x159678: the gate at 0x8C218 and the value
                                   * RadarScreen_Update tests again at 0x6059C (its own env carries
                                   * the same byte at pSession->pbActive) */
    PadImage *pStripPad;          /* *pGeoscapeStripPad @ 0x186BBC (0x8C224): blitted, then its own
                                   * rectangle blitted out of the surface */
    UiControl *const *apRootCtl;  /* the root block's thirteen widgets, 0x1864F8 + 4*i (0x8C238) */
    const struct RadarUpdateEnv *pRadarEnv; /* 0x8C298 */
    UiControlBlitFn  pfnCtlBlit;
    UiControlBlitFn  pfnCtlBlitEx;
    void            *pCtlBlitUser;
} GeoscapeRadarArmEnv;

/* 0x8C214..0x8C29C: the cases 0/2 arm taken while the radar screen is open. */
void Geoscape_DrawRadarArm(const GeoscapeRadarArmEnv *pEnv);

/* Everything the switch dispatches to. One field per arm argument, in the ARM's order, each named
 * for the block the original loads into R0/R5/R6 at that call site. */
typedef struct GeoscapeDrawEnv {
    const i32 *pnScreenId;        /* nGeoscapeScreen @ 0x186BC4, read at 0x8C194/0x8C1A0 */

    /* Cases 0 and 2, 0x8C214..0x8C760. */
    const GeoscapeRadarArmEnv    *pRadarArm;    /* the radar branch, 0x8C214..0x8C29C */
    GeoscapeMapViewEnv           *pMapView;     /* 0x8C2B0..0x8C690 */
    const GeoscapeCrosshairEnv   *pCrosshair;   /* 0x8C69C..0x8C760 */
    const GeoscapeStatusPanelEnv *pStatusPanel; /* 0x8C760..0x91DB0, chain tail included */

    const GeoscapePlacementArmEnv *pPlacementArm;   /* case 1, 0x91DC8 */
    const GeoscapeScreenArmEnv    *pPlacementCtl;
    const struct FacNameGridEnv   *pPlacementGrid;
    const struct BaseStripDrawEnv *pPlacementStrip;

    const GeoscapeBaseInfoArmEnv  *pBaseInfoArm;    /* case 3, 0x93F08 */
    const GeoscapeScreenArmEnv    *pBaseInfoCtl;
    const struct BaseInfoDrawEnv  *pBaseInfoDraw;

    const struct SoldierDrawEnv   *pSoldierDraw;    /* cases 4 and 5 */
    const GeoscapeSoldierArmEnv   *pSoldierArm;
    const GeoscapeScreenArmEnv    *pSoldierPanelCtl; /* case 4's two: apSoldierPanelCtl @ 0x1598B0 */
    const GeoscapeScreenArmEnv    *pSoldierFindCtl;  /* case 5's three: apSoldierFindCtl @ 0x1598B8 */

    const GeoscapePlaneArmEnv     *pPlaneArm;       /* case 6, 0x9309C */

    const GeoscapeBaseFacArmEnv   *pBaseFacArm;     /* case 7, 0x92AF0 */
    const struct FacNameGridEnv   *pBaseFacGrid;
    const struct BaseStripDrawEnv *pBaseFacStrip;
    const struct BuildDialogEnv   *pBaseFacDialog;

    const struct ResearchDrawEnv  *pResearchDraw;   /* case 8, 0x94114 */
    const GeoscapeScreenArmEnv    *pResearchCtl;

    PadImage                      *pManufPad;       /* case 9, 0x94160: *unk_159E98 */
    const struct ManufFrameEnv    *pManufDraw;
    const GeoscapeScreenArmEnv    *pManufCtl;

    PadImage                      *pTransferPad;    /* case 10, 0x93E60: the transfer root array's pad */
    const struct TransferDrawEnv  *pTransferDraw;
    const GeoscapeScreenArmEnv    *pTransferCtl;
    const GeoscapeBuySellTrackEnv *pBuySellTrack;   /* the tail cases 10, 11 and 12 share, 0x93EB4 */

    PadImage                      *pBuySellPad;     /* cases 11 and 12, 0x93DD8: *pBuySellScreenPad */
    const struct BuySellDrawEnv   *pBuySellDraw;
    const GeoscapeScreenArmEnv    *pBuySellCtl;     /* the two of g_aBuySellCtl @ 0x161A0C */

    struct FundingDrawEnv         *pFundEnv;        /* case 15's FundingScreen_Draw AND case 19's arm */
    const GeoscapeFundingArmEnv   *pFundingArm;

    struct UiBaseInfoDrawEnv      *pUfopaediaDraw;  /* case 16, 0x941B4 */
    const GeoscapeUfopaediaArmEnv *pUfopaediaGates;
    const GeoscapeScreenArmEnv    *pUfopaediaCtl;

    const struct PsiScreenDrawEnv *pPsiDraw;        /* case 17, 0x94294 */
    const GeoscapeScreenArmEnv    *pPsiCtl;

    const struct GeoscapeScreenDrawEnv *pOptionsScreen; /* case 18, 0x942F8: Geoscape_DrawScreen, whose
                                                         * typedef is below this struct */

    const struct BaseAtDrawEnv    *pBaseAtEnv;      /* case 20 ONLY -- 0x944B4, inside its fall-through */
    const GeoscapeFrameEndEnv     *pFrameEnd;       /* 0x944B8..0x94544, the frame's own end */
} GeoscapeDrawEnv;

/* Geoscape_Draw @ 0x8c180. Returns the frame end's present value (0x94538), which is what
 * Render_Frame returns on its geoscape branch. */
i32 Geoscape_Draw(const GeoscapeDrawEnv *pEnv);

/* ---- the geoscape frame's own globals ---------------------------------------------------
 *
 * Seven scalars live in .data, are written by one frame and read by the next, and belong to no
 * screen: the map arm writes the first, the crosshair tail the next three, and Geoscape_OnPointer
 * the last three. The original keeps them as loose globals; the frame state owns them so one owner
 * survives from one frame to the next. */
typedef struct GeoscapeFrameGlobals {
    i32 nOverlayOriginX;      /* nSmapOverlayOriginX @ 0x186C20: written at 0x8C2F4..0x8C3C8, read
                               * back by RadarScreen_BlitMapBackdrop (0x5EBF0) and Geoscape_DrawScreen
                               * (0x6C250). */
    i32 nCrosshairX;          /* nSmapCrosshairX @ 0x186BC8 */
    i32 nCrosshairY;          /* nSmapCrosshairY @ 0x186BCC */
    u8  bCrosshairPending;    /* bSmapCrosshairPending @ 0x1870B0: the tail's gate at 0x8C69C */
    i32 nTapCooldown;         /* nRadarCooldownR @ 0xAC218: the tail parks it at 0xFF (0x8C754) and
                               * Geoscape_OnPointer sets 255 on a strateg hit (0x87C84) or 0 on a drag. */
    i32 nCursorX;             /* nSmapCursorX @ 0x186BD0, stored as a halfword at 0x8731C */
    i32 nCursorY;             /* nSmapCursorY @ 0x186BD4, stored as a halfword at 0x872F8 */
    u8  bDragActive;          /* bGeoscapeDragActive @ 0x187074, byte 0: 1 during a globe drag
                               * (0x87D58), 0 when a press misses (0x87C2C) or on release (0x896BC). */
} GeoscapeFrameGlobals;

/* The owner of these globals also holds one of every env above -- PufoWorldState in the shell, a
 * local set in the tests. The map arm's two pads and the screen id change per frame with the zoom
 * set the view selected, so the owner refreshes them before every Geoscape_Draw call.
 *
 * What the shell wiring covers: screens 0 and 2 (the map view, the crosshair, the status panel with
 * its chain tail and the globe hover) and the frame end. The other nineteen arms keep a NULL env:
 * the shell owns none of the screen state they read -- the research, manufacturing, craft, transfer,
 * buy/sell and ufopaedia blocks and their controls. Geoscape_Draw dereferences several of those arm
 * env pointers without a test, so a screen id whose arm is unwired faults in the port while the
 * original cannot reach it (its globals are always live); the shell keeps the screen id at the wired
 * screens for that reason. */

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
    PadImage       *pLayerPad;      /* pSmapLayerPad @ 0x186BE0 */
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
 *       @ 0x187310 is CLEAR (0x9576C ADD R5,R4,#0x10 / 0x95790 ADD R5,#4 / 2 steps), plus two reached
 *       from 0x1872E8 when the dirty byte is SET and g_bUfopaediaWidePanel @ 0x1871FC is set
 *       (0x957BC..0x957E0) -- and nothing at all when the wide byte is clear (0x957B4 BEQ), so the
 *       entry pair 2 / 3 never auto-repeats even though the draw arm blits it
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
    i32                 nStripHeight;   /* pGeoscapeStripPad @ 0x186BBC uHeight, Smap_ScrollBy's clamp */
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
