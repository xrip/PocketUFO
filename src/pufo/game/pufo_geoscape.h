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
    /* ResearchScreen_AccumulateAllocated (0x74914), NOT ported. */
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

/* BaseRec_AddItemQty @ 0x556e4. Two adds, no clamping and no floor at zero:
 *
 *     pItem->auPerBaseVal[(char)pBase->bSlot] += nDelta;
 *     pBase->nStoresTotal                     += nDelta;
 *
 * The slot index is BaseRec.bSlot @ +0x05 widened as a char, unchecked. The port skips
 * both adds when the slot is outside the eight entries (deviation). */
void BaseRec_AddItemQty(BaseRec *pBase, ObjectRecFull *pItem, i32 nDelta);

#endif /* PUFO_GEOSCAPE_H */
