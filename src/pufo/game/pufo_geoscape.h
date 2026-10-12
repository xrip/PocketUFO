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
    /* Smap_CanInterceptReach @ 0x7e074. */
    bool (*pfnCanInterceptReach)(i32 nShipIdx, void *pUser);
    /* UiPanel_AppendLineWithSound @ 0x5a394. nMsg: 15 contact, 13 intercept
     * lost, 43 low supply, 10 radar contact. GameTime_Pause @ 0x5d6ac is NOT
     * part of the hook: 15, 13 and 10 pause the clock, 43 does not, so the
     * port writes time.bPaused itself at the three pausing sites. */
    void (*pfnContactMsg)(i32 nMsg, i32 nArg3, i32 nArg4, void *pUser);
    /* Geoscape_SpawnMissionSite (0x7e49c). */
    void (*pfnSpawnMissionSite)(StrategItem *pUfo, i32 nMissionSub, void *pUser);
    /* Smap_GrantSiteRecovery @ 0x98b50. */
    void (*pfnGrantSiteRecovery)(void *pUser);
    /* ManufScreen_RefreshBaseTabs @ 0x70ebc. */
    void (*pfnRefreshBaseTabs)(void *pUser);
    /* ResearchScreen_AccumulateAllocated @ 0x74914. */
    void (*pfnAccumulateResearch)(void *pUser);
    /* Base_TickFacilityConstruction @ 0x62824. */
    void (*pfnTickFacilityConstruction)(void *pUser);
    /* Base_ClearFacilityGridRow @ 0x6a0e0; its byte result becomes the
     * per-base site state byte_161F38[i]. */
    i32 (*pfnClearFacilityGridRow)(i32 nBaseIdx, void *pUser);
    /* Base_CountEmptyFacilityCells @ 0x6aee0. */
    i32 (*pfnCountEmptyFacilityCells)(i32 nBaseIdx, void *pUser);
    /* FundingScreen_OpenMonthly @ 0x5d160. */
    void (*pfnOpenMonthly)(void *pUser);
    /* Base_FindIndexBySiteId @ 0x5a8d0, passed through to the detect pass. */
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

/* BaseRec_AddItemQty @ 0x556e4. Two adds, no clamping and no floor at zero:
 *
 *     pItem->auPerBaseVal[(char)pBase->bSlot] += nDelta;
 *     pBase->nStoresTotal                     += nDelta;
 *
 * The slot index is BaseRec.bSlot @ +0x05 widened as a char, unchecked. The port skips
 * both adds when the slot is outside the eight entries (deviation). */
void BaseRec_AddItemQty(BaseRec *pBase, ObjectRecFull *pItem, i32 nDelta);

#endif /* PUFO_GEOSCAPE_H */
