/* pufo_ship.h - player craft records and their per-minute / per-hour ticks.
 *
 * Evidence: Ship_ctor @ 0x45c20, Ship_TickRangeRecover @ 0x45f80,
 * Ship_TickRefuelAtBase @ 0x45f04, Ship_TickRangeRecoverConsume @ 0x45fc8,
 * Ship_ClearExpiredIntercepts @ 0x8128c. The Ship layout is the IDB type
 * `Ship` (68 bytes) built up over earlier typing passes; the tick bodies are
 * transcribed from the decompilation.
 *
 * The original walks the craft array as `*(pCtxBase + 4*i)`; the port keeps
 * an explicit Ship* array plus the byte count at pCtxBase+0x1194.
 */
#ifndef PUFO_SHIP_H
#define PUFO_SHIP_H

#include "pufo_common.h"
#include "game/pufo_boot.h"   /* ObjectRecFull, for the supply item */
#include "util/pufo_ptrarray.h"

typedef struct BaseRec {
    i32 nStoresTotal;  /* +0x00 nUnk0: total stored item count */
    u8  bUnk4;         /* +0x04 */
    u8  bSlot;         /* +0x05 index into ObjectRecFull.auPerBaseVal */
    u8  gap_6[2];
    i32 nUnk8;         /* +0x08 */
    i32 nUnk12;        /* +0x0c */
    u8  bUnk16;        /* +0x10 */
    u8  bRegionId;     /* +0x11 the region the base sits in, 0x80..0x89 (PufoRegionId); the
                        * funding screen groups it into one of its five countries
                        * (FundingScreen_Init 0x5C1C8). Game_StartNew's starting base has 0. */
    u8  gap_18[2];
} BaseRec;

_Static_assert(sizeof(BaseRec) == 20, "BaseRec must be 20 bytes");

/* BaseRec_ctor @ 0x55650. Two effects, in this order:
 *
 * 1. 0x55660..0x556B0: for every record in the object table, store 0 into
 *    auPerBaseVal[nSlotIndex]. The index is the SIGNED byte at 0x159D80 at the
 *    moment the ctor runs -- the caller's current base slot, and in the save reader
 *    the base COUNT that was just read out of the chunk.
 * 2. 0x556B8..0x556D0: nStoresTotal = 0, bUnk4 = the global byte at 0x186558,
 *    bSlot = nSlotIndex.
 *
 * The original has NO bound check on the index. With the maximum eight bases the
 * index is 8, and 2*8 bytes past auPerBaseVal (0x2a) lands on aItemQty[0] at 0x3a,
 * so the ctor zeroes the first stock count of every item in the game. The port
 * clamps instead of reproducing that write; the deviation is recorded in
 * docs/parity/game-core.md.
 *
 * In the save reader all three field writes are immediately overwritten by the
 * values from disk, so the only live effect there is the column zeroing. */
BaseRec *BaseRec_Ctor(BaseRec *pBase, i32 nSlotIndex, u8 bUnk186558,
                      ObjectRecFull *const *apObjects, i32 nObjectCount);

/* The craft carries four cargo counts, at +0x24 through +0x33. */
#define PUFO_SHIP_CARGO_COLUMNS 4

typedef struct Ship {
    u8   bType;        /* +0x00 record kind */
    u8   bId;          /* +0x01 craft id, matched against StrategItem.nDisplayId */
    u8   bCraftType;   /* +0x02 CraftType 0..4 (see the enum above); drives fuel rate and detect radius */
    u8   bHomeBase;    /* +0x03 index into the base array */
    u16  uUnk4;        /* +0x04 */
    u8   bAtBase;      /* +0x06 */
    u8   bRefueling;   /* +0x07 */
    u16  uRangeRemain; /* +0x08 */
    u16  uRangeMax;    /* +0x0a */
    u16  uRangeRate;   /* +0x0c */
    u16  uUnk14;       /* +0x0e */
    u16  uFuelCap;     /* +0x10 */
    u8   bHangarX;     /* +0x12 */
    u8   bHangarY;     /* +0x13 */
    u8   bCrewSlots;   /* +0x14 */
    u8   gap_21;
    u16  uFuelMax;     /* +0x16 */
    i16  nFuel;        /* +0x18 signed; refuel compares as signed */
    u8   gap_26[2];
    i32  nSlotIdx;     /* +0x1c hangar slot, used in supply messages */
    u8   bNeedsSupply; /* +0x20 selects the consuming range-recovery path */
    u8   bCanDogfight; /* +0x21 */
    i8   bWeaponL;     /* +0x22 */
    i8   bWeaponR;     /* +0x23 */
    i32  anCargoQty[PUFO_SHIP_CARGO_COLUMNS]; /* +0x24 */
    u8   bWeaponsHot;  /* +0x34 gates proximity detection growth */
    u8   bIntercepting;/* +0x35 */
    u8   gap_54[2];
    i32  nStrategIdx;  /* +0x38 index into the strateg list of the intercept target */
    u8   bContactMsg;  /* +0x3c "contact" message already posted */
    u8   bAmmo25;      /* +0x3d */
    u8   bAmmo26;      /* +0x3e */
    u8   bAmmo27;      /* +0x3f */
    u8   bAmmo29;      /* +0x40 */
    u8   bAmmo30;      /* +0x41 */
    u8   gap_66[2];
} Ship;

_Static_assert(sizeof(Ship) == 68, "Ship must be 68 bytes");

/* The base store record each cargo column answers to: column 0 -> object 56, 1 -> 57, 2 -> 105,
 * 3 -> 58. Two independent ARM sites use exactly this mapping, Ship_ReturnToBase at 0x686F0 and
 * Tact_SetupCraftCargo at 0x39600, which is what makes the order a fact rather than a guess.
 * Column 2 is the odd one out: it is handed over in tens at both sites (0x68730 and 0x3963C). The
 * ten itself is not explained by anything in the binary, so it stays a literal.
 *
 * These are defines rather than one shared array because pufo_game and pufo_ui already depend on
 * each other (pufo_strateg.c calls UiLineList_Remove), and a shared symbol here drags that cycle
 * into every test link. Recorded in HANDOFF.md as a separate problem. */
#define PUFO_CARGO_TENS_COLUMN 2
#define PUFO_CARGO_STORE_0 56
#define PUFO_CARGO_STORE_1 57
#define PUFO_CARGO_STORE_2 105
#define PUFO_CARGO_STORE_3 58

/* Ship_TickRangeRecover @ 0x45f80. While at base and not needing supply, add
 * uRangeRate toward uRangeMax; the clamp to uRangeMax is unconditional. */
void Ship_TickRangeRecover(Ship *pShip);

/* Ship_TickRefuelAtBase @ 0x45f04. While at base and below uFuelMax, set
 * bRefueling and add per craft type: 0 -> +1, 1/2/4 -> +2, 3 -> +1,
 * anything else -> no change. Then if nFuel >= uFuelMax clamp and clear
 * bRefueling. nFuel is i16 and the compare is signed, like the original. */
void Ship_TickRefuelAtBase(Ship *pShip);

/* The supply item is pCtxBase.apObjects[105]; its per-base quantities are the
 * u16 array auPerBaseVal. apBases[bHomeBase] carries the base slot index. */
typedef struct ShipSupplyHooks {
    BaseRec *const *apBases;      /* pCtxBase.apBases */
    ObjectRecFull  *pSupplyItem;  /* pCtxBase.apObjects[105] */
    i32 *pbLowSupplyFlag;         /* dwActionFail @ 0x1870C8, written with STR at 0x46064: a DWORD store,
                                   * the same one RadarScreen_Update makes at 0x61340 and the same one
                                   * BaseFacScreen_TryBuySelected makes with STRMI at 0x58960 */
    void (*pfnLowSupplyMsg)(i32 nHomeBase, i32 nSlotIdx, void *pUser);
    void *pUser;
} ShipSupplyHooks;

/* Ship_TickRangeRecoverConsume @ 0x45fc8. At base, below max, and NEEDING
 * supply: if the home base holds more of the supply item than uRangeRate,
 * add 1000 * uRangeRate to uRangeRemain and subtract uRangeRate from the
 * base stock (BaseRec_SubItemQty @ 0x5571c). When the stock then drops below
 * uRangeRate, flag low supply and post message 43. Clamp at the end. */
void Ship_TickRangeRecoverConsume(Ship *pShip, const ShipSupplyHooks *pHooks);

/* Ship_ClearExpiredIntercepts @ 0x8128c needs the strateg list; it lives in
 * pufo_strateg.h to keep the dependency one-way, and is declared there. */

/* Ship_ctor @ 0x45c20. If the bSkip byte is set the ctor does NOTHING -- it returns
 * the record untouched.
 *
 * Otherwise, in order: bId = nId, bHomeBase = nHomeBase, the five ammo bytes, the four
 * cargo counts, bContactMsg, bIntercepting, nStrategIdx and uRangeRemain are cleared,
 * 0x45C80 stores the u16 1 at &bAtBase -- which sets bAtBase = 1 AND bRefueling = 0 --
 * bWeaponsHot is cleared, bCraftType is stored, and nSlotIdx takes the CURRENT ship
 * count. Then 0x45CB0..0x45CE4 zeroes aItemQty[nSlotIdx] in every object record: the
 * per-craft stock column, at 0x3a. That index is unbounded in the original (aItemQty
 * has 52 entries); the port clamps and records the deviation.
 *
 * bType is NOT saved in the ships chunk. It comes from a per-craft-type counter, one
 * byte each at 0x18653C .. 0x186540, incremented here. A craft type above 4 leaves
 * bType at 1 and sets no range or fuel field at all.
 *
 * The craft table below is the ctor's if-chain. Type 2's uRangeMax is written as -5536
 * into a u16, which is 60000. */
typedef struct ShipCtorEnv {
    u8  abTypeCounters[5];       /* unk_18653C .. unk_186540, one per craft type */
    i32 nShipCount;              /* pCtxBase.nShipCount: the new ship's cargo column */
    ObjectRecFull *const *apObjects;
    i32 nObjectCount;
} ShipCtorEnv;

Ship *Ship_Ctor(Ship *pShip, u8 nCraftType, i32 nId, i32 nHomeBase, u8 bSkip,
                ShipCtorEnv *pEnv);

/* Ship_FindIndexById @ 0x69ffc. First craft whose Ship.bId equals nId, -1 when the craft count is
 * not positive or none matches. Called from BaseInfoScreen_BuildPanels (0x557f4) and
 * BuySellScreen_CommitTransaction (0x74bf8), both ported in pufo_baseui.c. */
i32 Ship_FindIndexById(Ship *const *apCraft, i32 nCraftCount, i32 nId);

/* ------------------------------------------------- the craft cargo counterpart
 *
 * Evidence: InvSel_AddItemQty @ 0x45e8c, InvSel_SubItemQty @ 0x45ec8.
 *
 * These are the mirror of the BaseRec_AddItemQty and BaseRec_SubItemQty pair on the craft
 * side, and the shape is identical: one add into the object's per-owner column, one into
 * the owner's own running total.
 *
 *     pItem->aItemQty[pSel->nSlotIdx] += nDelta;   the object's per-craft column
 *     pSel->uUnk4                     += nDelta;   the craft's cargo total at +0x04
 *
 * The column index is Ship.nSlotIdx at +0x1c, which Ship_Ctor set to the ship count at the
 * moment the ship was created. That is what makes aItemQty a per-craft column rather than a
 * per-item count, and it is the same trick auPerBaseVal uses with BaseRec.bSlot.
 *
 * Both return the ship pointer, not a status.
 *
 * Deviation: the original never checks nSlotIdx against the 52 entries of aItemQty. The port
 * skips both adds when it is out of range, matching what it already does for the base-side
 * pair. */
Ship *InvSel_AddItemQty(Ship *pSel, ObjectRecFull *pItem, i16 nDelta);
Ship *InvSel_SubItemQty(Ship *pSel, ObjectRecFull *pItem, i16 nDelta);

/* ------------------------------------------------- losing a craft, cargo and all
 *
 * Evidence: Ship_RemoveWithCargo @ 0x36830.
 *
 * Called when a mission ends with the craft gone (Tact_Shutdown's outcome path and
 * Base_DemolishFacility). It removes the ship, its crew, its strateg entry, its hangar
 * flag and its cargo column, then renumbers everything above it.
 *
 * The ship array is compacted by STRUCT COPY, not by moving pointers: for every slot above
 * the removed one, ARM at 0x36A50 does memmove(apCraft[i-1], apCraft[i], 0x44) -- 68 bytes,
 * sizeof(Ship). The pointer array keeps its pointers; only the last object is deleted
 * (0x36B40 deletes apCraft[nShipCount-1] after the count is decremented).
 *
 * The cargo column moves with it. ARM at 0x36A24 reads ObjectRecFull.aItemQty[i] at byte
 * 0x3A + 2*i and stores it at 0x3A + 2*i - 2, so aItemQty[i-1] = aItemQty[i] for every
 * object record. That is the same 0x3A the ships chunk writes cargo through.
 *
 * Three index sets are decremented, because three things index the ship array positionally:
 *   - Soldier.bUnk183 @ 0x0B7, the soldier's ship, decremented when it is above the removed
 *     index and is neither 254 nor 255 (the two "not assigned" sentinels).
 *   - the crew list: every soldier whose bUnk183 equals the removed index is deleted outright,
 *     through Soldier_RemoveById.
 *   - the assigned-ship array at 0x16146C, whose count is the signed dword at 0x161468.
 * Finally Ship.nSlotIdx @ 0x1C is renumbered 0..count-1 for every remaining craft, which is
 * the same value Ship_ctor assigns at creation.
 *
 * The hangar flag is cleared at 0x158CD8 + 6*(24*Ship.bHomeBase + Ship.bHangarY + 12) +
 * Ship.bHangarX (0x368CC..0x368D8). The three bytes are the home base and the two hangar
 * grid coordinates, and the stride is six.
 *
 * Deviations, all of them cases where the original walks off the end:
 *   - An unknown ship id leaves the search index equal to the count, and the original then
 *     deletes apCraft[count] and shifts a list that has nothing to shift. The port returns
 *     the count unchanged.
 *   - The original clears the hangar flag with no bound on the computed index.
 *   - If no strateg entry matches, the original calls StrategList_RemoveAt with an index
 *     equal to the item count. The port skips the call. */
typedef struct ShipRemovalEnv {
    Ship **apCraft;
    u8 *pnShipCount;              /* Map + 0x1194, unsigned byte */
    struct Soldier **apSoldiers;
    u16 *pnSoldierCount;          /* Map + 0x1998, unsigned 16-bit */
    ObjectRecFull **apObjects;
    u16 nObjectCount;             /* Map + 0x10A8, unsigned 16-bit */
    PtrArray *pStrategList;       /* 0x1864F8 + 0x3C. The port keeps the list in the PtrArray
                                   * shape; the StrategList view used for searching is built from
                                   * it on demand, never stored, so it cannot go stale across a
                                   * removal. */
    const struct GeoscapeWorld *pWorld;
    u8 *pnSpawnOnceFlag;          /* 0x162028, the replacement-spawn gate */
    const struct StrategSpawnCtx *pSpawnCtx;
    u8 *abHangarFlag;             /* the six-byte-per-cell table at 0x158CD8 */
    i32 nHangarFlagCap;
    BaseRec *const *apBases;      /* Map + 0x1174, needed to retarget a craft to its home base */
    i32 *pnGameTotalSec;          /* the clock total, which StrategEntry_InitTravel needs */

/* BaseRec_Create @ 0x32c88. Allocate one 20-byte base record, run BaseRec_Ctor over it, and store
 * it at roster slot nBaseCount. The caller increments the count; this function does not. */
    i32 *anAssignedShip;          /* 0x16146C */
    i32 *pnAssignedShipCount;     /* 0x161468, signed */
} ShipRemovalEnv;

/* BaseRec_Create @ 0x32c88. Allocate one 20-byte base record, run BaseRec_Ctor over it, and store
 * it at roster slot nBaseCount. The caller increments the count; this function does not.
 *
 * bSiteId is the byte BaseRec_ctor stores at 0x556C0 (`pBase->bUnk4 = nNextBaseSiteId`, the counter
 * at 0x186558) beside the slot byte at 0x556D0. Base_CreateNamedEntry hands it the same counter it
 * gives the new base's strateg entry (0x669A8) and its site record (0x66A34), and that is what makes
 * the pair findable: SmapHit_FindBaseBySiteId matches BaseRec.bUnk4 against an entry's nSiteId
 * (0x82ED4), OptionsScreen_Open matches it against the mode (0x6D12C), and BaseGrid/Base_CalcDetect
 * reach the base's site record through it. */
BaseRec *BaseRec_Create(BaseRec **apBases, i32 nBaseCount, u8 bSiteId,
                        ObjectRecFull *const *apObjects, i32 nObjectCount);

/* Ship_RetargetTravel @ 0x5a484. Send one craft home: drop a type-5 waypoint it was chasing,
 * restart its own entry's proximity timer, and run StrategEntry_InitTravel to its home base's
 * coordinates. */
i32 Ship_RetargetTravel(const ShipRemovalEnv *pEnv, i32 nShipIdx);

/* Ship_LaunchIntercept @ 0x5a64c, the mirror of Ship_RetargetTravel: it sends a craft OUT to a
 * strateg item instead of HOME to its base. IDA called it Ship_AbortCourse; it aborts nothing.
 *
 * The craft's own entry is found with StrategList_FindByShipId, its proximity timer is restarted at
 * 0, and nStartX/nStartY take the entry's CURRENT position. Then the one branch the "send home"
 * version does not have: when Ship.bAtBase is non-zero the leg starts at the home base instead --
 * nStartX/nStartY become BaseRec.nUnk8/nUnk12 and nX/nY are snapped to the same pair, so a craft
 * that is sitting in a hangar leaves from the hangar and not from wherever its entry last drew.
 *
 * Four ship fields then change, in the originals' order: bWeaponsHot (+0x34) = 1, nStrategIdx
 * (+0x38) = nStrategIdx, bAtBase (+0x06) = 0, bIntercepting (+0x35) = 1. The travel leg runs from
 * (nStartX, nStartY) to list[nStrategIdx]'s nX/nY with nFlag 0, bA/bB/bC 0 and bCopyStart 1 -- the
 * one caller that ever sets bCopyStart. Returns StrategEntry_InitTravel's travel epoch.
 *
 * Two deviations, both about a bad index. The original reads list[nStrategIdx].nX at 0x5A7B8 after
 * every store and faults if the index is stale; the port looks the target up first and refuses the
 * whole call, so a craft cannot end up half-launched. And the original indexes apBases with an
 * unchecked byte; the port bounds bHomeBase against PUFO_FAC_BASE_COUNT. */
i32 Ship_LaunchIntercept(const ShipRemovalEnv *pEnv, i32 nShipIdx, i32 nStrategIdx);

/* Ship_ClearInterceptByStrategIdx @ 0x5a5b8. Retarget every craft chasing one strateg item. */
i32 Ship_ClearInterceptByStrategIdx(const ShipRemovalEnv *pEnv, i32 nStrategIdx);

/* Ship_ClearWaypointIntercept @ 0x5a830. Ship_RetargetTravel with the travel step dropped and the
 * timer parked at 0xFFFF instead of restarted at 0. */
struct StrategItem *Ship_ClearWaypointIntercept(const ShipRemovalEnv *pEnv, i32 nShipIdx);

/* Base_RemoveAllShips @ 0x66874 (116 B): remove every craft the assigned-ship list names, then
 * answer how many craft the base has left.
 *
 * The list is the one Ship_RemoveWithCargo already maintains: a signed count at 0x161468 and the
 * indices at 0x16146C. Each entry is an index into apCraft, and the craft's Ship.bId @ +1 is what
 * Ship_RemoveWithCargo is given -- it searches by id, not by position. The index is re-read from
 * the list on every iteration (0x6689C), and that matters: Ship_RemoveWithCargo compacts apCraft
 * and decrements every stored index above the gap (0x36AEC..0x36B20), so the list tracks the
 * compaction as it goes and stays valid. The count is re-read too (0x668BC), though nothing here
 * changes it.
 *
 * The tail call is Base_CountShipsAtBase(a1, a2), whose first argument is the Map base and is dead
 * for the grid-byte half -- so the byte table at 0x159DBC has to be handed in separately, and the
 * result is craft homed at this base plus that base's byte in that table.
 *
 * The original's first argument is never read by anything but that tail call's dead half, and it
 * bounds nothing: an entry that is not a valid apCraft index is dereferenced. The port skips it. */
typedef struct BaseClearShipsEnv {
    ShipRemovalEnv ship;      /* Ship_RemoveWithCargo's wiring, including the assigned-ship list */
    i32            nBaseCount; /* the apBases length ShipRemovalEnv does not carry */
    const u8      *abGridBytes; /* 0x159DBC, the byte Base_CountShipsAtBase adds on */
} BaseClearShipsEnv;

i32 Base_RemoveAllShips(const BaseClearShipsEnv *pEnv, i32 nBaseIndex);

/* Returns the ship count after the removal. */
i32 Ship_RemoveWithCargo(const ShipRemovalEnv *pEnv, i32 nShipId);

/* ------------------------------------------------- the cargo hold and what is in it
 *
 * Evidence: Craft_GetTypeCargoCap @ 0x65ac4, Base_CalcWeaponStorageUsed @ 0x65b30.
 *
 * The hold cap is chosen by Ship.bCraftType @ 0x02 and nothing else:
 *
 *     0 Skyranger        140
 *     1 Lightning        120
 *     2 Avenger          260
 *     3 Interceptor or 4 Firestorm   0
 *     anything else       -1
 *
 * The -1 is not a cap, it is "unknown craft type", and a caller that compares against it
 * gets a hold that nothing fits into.
 *
 * Base_CalcWeaponStorageUsed adds up what the craft is already carrying, charging each
 * object its per-unit size: sum of ObjectRecFull.bCargoSize @ 0xC6 times
 * aItemQty[nCraftIdx]. Only two object ranges count, and the ARM states the filter
 * literally at 0x65B74:
 *
 *     (index > 0 && index < 25) || (index > 35 && index < 52)
 *
 * so objects 1..24 (the weapons) and 36..51 (the ammo) fill the hold, and 25..35 do not.
 * Object 0 does not either. The first half of the ARM's condition reads as a pointer
 * compare against the Map base, which is just `index > 0` because the pointer is
 * Map + 4*index.
 *
 * Deviation: the original takes the craft index as a signed char and does not bound it
 * against nObjectCount or the ship array. The port takes an i32 and refuses out-of-range
 * indices. */
/* Ship.bCraftType @ 0x02 values, named by what Ship_Ctor @ 0x45c20 actually gives them:
 *
 *     type  crew  dogfight  rangeMax  rangeRate  supply  fuelMax  fuelCap  cargo cap
 *     0       0       1       10000      20       no      150      760      140
 *     1       1       1       30000       2       yes      800     3100      120
 *     2       2       1       60000       2       yes     1200     5400      260
 *     3       2       0        1000      20       no      100     3000        0
 *     4       2       0       20000       2       yes      500     4500        0
 *
 * The names are mapped 1:1 onto the only five craft names the data ships, main.dat TEXTS
 * records 522..526 (Skyranger, Lightning, Avenger, Interceptor, Firestorm). ASSUMPTION,
 * recorded: no code in the image indexes a text record by bCraftType, so the order is the
 * shipped order, not a verified mapping. Two counter-indications to keep in mind: the
 * auto-name prefixes at 137..139 (Skyranger-, Lightning-, Avenger-) are three names while
 * Game_StartNew @ 0x94554 spawns types 0, 3 and 3; and the cargo caps (140, 120, 260, 0, 0)
 * give the "Firestorm" no hold at all. unk_18653C..unk_186540 (abTypeCounters) counts one
 * construction per type. */
#define PUFO_CRAFT_TYPE_COUNT 5
typedef enum CraftType {
    CRAFT_TYPE_SKYRANGER   = 0, /* crew 0, dogfight 1, rangeMax 10000, cap 140: Game_StartNew's cargo hauler */
    CRAFT_TYPE_LIGHTNING   = 1,
    CRAFT_TYPE_AVENGER     = 2,
    CRAFT_TYPE_INTERCEPTOR = 3, /* crew 2, dogfight 0, rangeMax 1000, cap 0: the two starter crew craft */
    CRAFT_TYPE_FIRESTORM   = 4
} CraftType;

typedef enum CraftCargoCap {
    CRAFT_CARGO_CAP_NONE = 0,
    CRAFT_CARGO_CAP_TYPE0 = 140,
    CRAFT_CARGO_CAP_TYPE1 = 120,
    CRAFT_CARGO_CAP_TYPE2 = 260,
    CRAFT_CARGO_CAP_UNKNOWN = -1
} CraftCargoCap;

/* Ship.bWeaponL @ 0x22 and Ship.bWeaponR @ 0x23 hold one of these seven ids. The resupply switch at
 * 0x68FDC (Base_DistributeRecoveredItems) and the radar mount switch at 0x5EE38 / 0x5F160
 * (RadarScreen_DrawCraftArmament) both branch on it, and the ammo bytes are named after it:
 * Ship.bAmmo25..bAmmo30 @ 0x3d..0x41.
 *
 * The names are lang.dat TEXTS records 1446 + id, read straight out of the shipped blob (1650
 * records, 200 bytes each): 1470 "Пси-усилитель", 1471 "Корабельная пушка", 1472 "Уст-ка Стингер",
 * 1473 "Уст-ка Эваланш", 1474 "Лазерная пушка", 1475 "Плазменная пушка", 1476 "Термояд. рак. уст."
 * -- the same seven the resupply note in Base_DistributeRecoveredItems names in English.
 *
 * WM_PSI_AMP is the one id no mount switch handles: it falls through both 0x68FDC and 0x5EE38, so a
 * psi-armed craft gets no ammo and no radar bar. WM_LASER is the weapon that never runs out -- the
 * radar gives it the -1 "unlimited" ammo sentinel and the resupply switch leaves it alone. */
#define PUFO_WEAPON_ID_MIN  24
#define PUFO_WEAPON_ID_MAX  30
typedef enum WeaponMount {
    WM_PSI_AMP   = 24, /* lang.dat TEXTS[1470] "Пси-усилитель" -- handled by no switch */
    WM_CANNON    = 25, /* TEXTS[1471] "Корабельная пушка", ammo object 52, 10 per mount */
    WM_STINGRAY  = 26, /* TEXTS[1472] "Уст-ка Стингер",        ammo object 53,  3 per mount */
    WM_AVALANCHE = 27, /* TEXTS[1473] "Уст-ка Эваланш",        ammo object 54,  2 per mount */
    WM_LASER     = 28, /* TEXTS[1474] "Лазерная пушка" -- unlimited, no ammo byte */
    WM_PLASMA    = 29, /* TEXTS[1475] "Плазменная пушка",      ammo object 105, 10 per mount in tenths */
    WM_FUSION    = 30  /* TEXTS[1476] "Термояд. рак. уст.",    ammo object 55,  2 per mount */
} WeaponMount;

/* The original's first argument is never read; the port takes the ship directly. */
CraftCargoCap Craft_GetTypeCargoCap(const Ship *pShip);

/* Base_CraftWeaponItemId @ 0x6641c. The selected base's stock of the item a craft stores slot
 * refers to. Slots 31, 32, 34 and 35 read the per-base column of object records 56, 57, 105 and
 * 58; slot 33 is a constant 1000 with no record touched; anything else is -1.
 *
 * The caller at 0x7ADC4 passes the column as the signed global byte 0x159D81, the byte next to
 * the base count at 0x159D80 -- the base the base screens are showing. */
i32 Base_CraftWeaponItemId(ObjectRecFull *const *apObjects, i32 nSlot, i32 nBaseColumn);

/* Craft_CountSoldiersAboard @ 0x6649c. The IDB called this Base_CountSoldiersAtBase; that name is
 * wrong. It counts soldiers whose bUnk183 -- the craft they are assigned to -- equals the
 * argument, and the call site in BasePlaneScreen_CommitStores passes the same value it uses as
 * nCraftIdx everywhere else. */
i32 Craft_CountSoldiersAboard(Soldier *const *apSoldiers, i32 nSoldierCount, i32 nCraftIndex);

/* Craft_CountWeaponSlotsCarried @ 0x664f0 (IDB: was Base_CountAmmoObjectsAtBase). Weapon-slot
 * objects 31..35 carried by one craft, counted in the object record's per-craft column. */
i32 Craft_CountWeaponSlotsCarried(ObjectRecFull *const *apObjects, i32 nObjectCount, i32 nCraftIdx);

/* BasePlane_CalcWeaponStoresUsed @ 0x78e78. Sum of bCargoSize * carried count over every object
 * index. The carried counts are what the hangar screen accumulated, so no filtering happens here. */
i32 BasePlane_CalcWeaponStoresUsed(ObjectRecFull *const *apObjects, i32 nObjectCount,
                                   const i32 *anCarried);

/* Objects 1..24 and 36..52 are the ones that occupy the hold. */
i32 Base_CalcWeaponStorageUsed(ObjectRecFull *const *apObjects, i32 nObjectCount,
                               i32 nCraftIdx);

/* Evidence: Craft_LoadStoresFromBase @ 0x693ec.
 *
 * Fill one craft from its home base. The wanted amount is what the craft carries of the weapon
 * object apObjects[nItemKind]; the supply comes from apObjects[nSrc]->auPerBaseVal[bHomeBase].
 * For kinds 7, 8 and 10 the supply is split across three ammo pools in fixed order:
 * 40/41/42, 43/44/45 and 46/47/48.
 *
 * Deviation: the original returns whatever the last BaseRec_SubItemQty left in R0, which is a
 * BaseRec pointer nothing reads. The port returns void. */
typedef struct CraftStoreLoadEnv {
    Ship **apCraft;
    BaseRec **apBases;
    struct ObjectRecFull **apObjects;
    i32 nObjectCount;
} CraftStoreLoadEnv;

void Craft_LoadStoresFromBase(const CraftStoreLoadEnv *pEnv, u8 nItemKind, u8 nSrc,
                              i32 nCraftIdx);

/* Evidence: Base_StripAllCraftCargo @ 0x69a38.
 *
 * For every craft with Ship.bCanDogfight @ 0x21 set, dump the ammo it carries into its home base
 * and refill it from that base. Ten groups, each a gate, a deposit and a refill; the gate is
 * "the craft carries less ammo than it carries weapons", taken against the values before the
 * deposit. The group table is g_aCraftAmmoGroups in the .c file: 36/1, 37/3, 38/4, 39/6,
 * 40+41+42/7, 43+44+45/8, 46+47+48/10, 49/11, 50/12, 51/13.
 *
 * Callers are Base_TransferTacticalLoot (0x68344) and Base_DistributeRecoveredItems, not the
 * hangar UI. */
void Base_StripAllCraftCargo(const CraftStoreLoadEnv *pEnv, i32 nShipCount);

/* --- Base_DistributeRecoveredItems @ 0x68f2c (1216 B) : re-arm every craft at its home base ---
 *
 * Callers are Smap_GrantSiteRecovery (0x98b50) and Base_TransferTacticalLoot's neighbours; it runs
 * after a site's recovered cargo has been handed out. It opens with Base_StripAllCraftCargo (the
 * infantry-ammo pass) and then walks bases outer, craft inner.
 *
 * The gate (0x68FA0): `Ship.bAtBase != 0 && Ship.bHomeBase == nBase` -- the craft has to be sitting
 * at the base being processed.
 *
 * The two weapon mounts, `Ship.bWeaponL @ 0x22` and `Ship.bWeaponR @ 0x23`, hold weapon object
 * slots, and only five of them have a resupply rule:
 *
 *   25 Craft cannon        -> apObjects[52]  "Craft cannon rounds[x50]", 10 rounds per mount
 *   26 Stingray launcher   -> apObjects[53]  "Stingray rocket",           3 per mount
 *   27 Avalanche launcher  -> apObjects[54]  "Avalanche rocket",          2 per mount
 *   29 Plasma cannon       -> apObjects[105] "Elerium-115",              10 per mount, in tenths
 *   30 Fusion ball launcher-> apObjects[55]  "Fusion ball",               2 per mount
 *
 * Weapon 28 (Laser cannon) and 24 (the psi amplifier) fall through the switch and are left alone --
 * the laser is the weapon that never runs out. Both ids are in the WeaponMount enum above. Each
 * mount's own quantity lives in the craft, in `Ship.bAmmo25..bAmmo30 @ 0x3d..0x41`, one byte per
 * weapon id.
 *
 * The load factor is 2 when both mounts hold the same weapon and 1 otherwise (0x68FC4: it compares
 * bWeaponL with bWeaponR), so a twin-mount craft re-arms with double the standard load.
 *
 * Each mount is handled the same three steps (0x6900C..0x691B4 for the left, 0x691F4..0x69388 for
 * the right): the quantity the craft holds is added to the base, the craft's byte is cleared, and
 * then the craft is re-supplied with `units = perMount * load`. If the base's own count for that
 * object, read as `apObjects[slot]->auPerBaseVal[nBase]`, is below `units`, the craft takes what the
 * base had and the base's count is zeroed; otherwise the craft takes the full `units` and the base
 * loses `units` through BaseRec_SubItemQty.
 *
 * Elerium is the odd one out: its craft byte is in tenths. The amount handed to the base is
 * `bAmmo29 / 10`, and the byte written back is `10 * what the craft ends up carrying` -- so 100 *
 * load when the base can supply it, 10 * the base's count when it cannot.
 *
 * Both mounts run even when the first one matched no weapon, and when both mounts hold the same
 * weapon the second pass reads the byte the first pass has just refilled and hands that back to the
 * base too. That is what the code does, and the port reproduces it.
 *
 * Deviation: the per-base count is indexed by the loop counter here (`auPerBaseVal` at record + 42 +
 * 2 * nBase), while BaseRec_AddItemQty and BaseRec_SubItemQty index it by `BaseRec.bSlot`. The port
 * keeps both, as the original has them; they agree as long as a base's slot byte equals its roster
 * index, which is what BaseRec_Create establishes. */
typedef struct BaseRecoverDistributeEnv {
    Ship          **apCraft;       /* Map.apCraft @ Map + 0x10AC */
    const u8      *pnCraftCount;   /* Map_nCraftCount @ 0x114B4C, read unsigned */
    BaseRec       **apBases;       /* Map.apBases @ Map + 0x1174 */
    const u8      *pbBaseCount;    /* nBaseCount @ 0x159D80, read signed */
    ObjectRecFull **apObjects;     /* Map.apObjects @ 0x113AC0 */
    i32            nObjectCount;   /* for Base_StripAllCraftCargo's env */
} BaseRecoverDistributeEnv;

void Base_DistributeRecoveredItems(const BaseRecoverDistributeEnv *pEnv);

/* Evidence: Base_TransferTacticalLoot @ 0x68344 (3048 bytes, 87 blocks).
 *
 * The landing pass. It walks every base and every craft, and for each craft that is at its home
 * base with its weapons hot it: parks it in a hangar cell, credits the crew it was carrying,
 * dumps and refills cargo, checks that the craft does not carry more weapons than the base has
 * ammo for, reloads its stores from the base, and finally re-draws each weapon mount's ammo.
 *
 * The gate at 0x683DC clears Ship.bWeaponsHot as it tests it, so a craft is processed once per
 * landing and a cold craft is skipped entirely.
 *
 * Two things the original does that are easy to mistake for a transcription error. Both are
 * reproduced here and both are named in the .c file.
 *
 *   - The hangar marker byte is written into the facility block numbered by Ship.bCraftType, not
 *     by Ship.bHomeBase. The three grid READS use bHomeBase; the one WRITE uses bCraftType.
 *   - The Base_UpdateStoresNeeded call passes (i8)(2 * ship index) as its base index.
 *
 * bAnnounceAmmoShortage is the original's second argument. An ammo shortage always raises the UI
 * refresh flag, but only announces itself when that argument is zero, so the same transfer can run
 * silently. A stores shortage always announces itself. */
typedef struct TacticalLootEnv {
    Ship **apCraft;
    BaseRec **apBases;
    struct ObjectRecFull **apObjects;
    Soldier **apSoldiers;
    u16 *pnSoldierCount;
    u8 *abFacilityGrid;
    u8 *pbUiRefreshFlag; /* unk_1870C8, set by either report */
    i32 nBaseCount;
    i32 nShipCount;
    i32 nObjectCount;
    /* UiPanel_AppendLineWithSound @ 0x5a394 on the message panel at 0x158890. bFlag is 39 for a
     * stores shortage and 40 for an ammo shortage. */
    void (*pfnAppendLine)(i32 bFlag, i32 nC0, i32 nC1, i32 nC2, void *pUser);
    void *pUser;
} TacticalLootEnv;

void Base_TransferTacticalLoot(const TacticalLootEnv *pEnv, bool bAnnounceAmmoShortage);

/* ------------------------------------------------- spawning a craft and finding its hangar cell
 *
 * Evidence: Ship_SpawnAtSlot @ 0x2cb14, called from ManufScreen_CommitProduction (0x6f650), four
 * times from Game_StartNew (0x94F7C, 0x951B4, 0x95268) and from 0x993B8.
 *
 * It allocates one 68-byte Ship, runs Ship_Ctor with the CURRENT value of the next-craft id, stores
 * the pointer at apCraft[nCraftCount] (Map+0x10AC, count byte at Map+0x1194), adds a StrategItem of
 * type 1 at the home base's own x/y, and only then increments both the id and the count.
 *
 * The second half searches the home base's facility grid for a hangar cell to attach the craft to:
 *
 *     fac[nBase].facType[v10][result]  == 11    (0x2CC0C: R9 = LR + 0x174, and LR takes `ADD LR,LR,#6`
 *                                                at 0x2CCD4 on every row)
 *     fac[nBase].layer2[v10+1][result+1] == 0   (0x2CC1C..0x2CC2C: grid + R6 + 0x1C3 + result, with R6
 *                                                taking `ADD R6,R6,#6` at 0x2CCD0 on every row;
 *                                                0x1C3 = 451 = 0x174 + 72 + 6 + 1)
 *     buildDays[nBase][v10][result]    == 0
 *
 * with v10 and result each running 0..4 -- a 5 by 5 scan of a 6 by 6 grid, so the sixth row and the
 * sixth column are never considered. The layer2 test is the down-right neighbour of the hangar's
 * facType cell, which is the same cell the stamp writes, so a hangar that already holds a craft is
 * skipped and a new game's three craft land in three different hangars. (An earlier reading of this
 * scan held the test pinned to row 1 -- that was the row advance `ADD R6,R6,#6` misread as a +1; the
 * same misread sat in the recovered-craft scan at 0x99174.)
 *
 * On the first match it writes Ship.bHangarX = result+1 (+0x12) and Ship.bHangarY = v10+1 (+0x13) --
 * the BR cell's column and row, which is what BaseFacScreen_DrawNameGrid's hangar pass compares
 * against -- and stamps the craft-type glyph into the grid at fac[nBase] + 6*(v10+1) + (result+1):
 * 33 for type 0, 34 for 1, 35 for 2, 36 for 3, 37 for 4. Any other type matches the cell and then
 * falls through the switch without stamping, and the loop does NOT continue -- it returns immediately
 * with the hangar coordinates set and nothing stamped.
 *
 * One asymmetry is the original's, not the port's: the return value is the inner counter, the column
 * index on a match and 5 when nothing matched. It is loop residue, and no caller uses it.
 *
 * Deviations: the original stores the new pointer even when operator new returned NULL and then, if a
 * hangar cell matches, writes the two coordinates through that NULL. The port skips the coordinate
 * write in that case. The port also bounds the base index against the eight base blocks. */
typedef struct ShipSpawnEnv {
    Ship        **apCraft;        /* Map.apCraft @ Map+0x10AC */
    u8           *pnCraftCount;   /* Map_nCraftCount @ Map+0x1194 */
    i32          *pnNextCraftId;  /* dword_186550, the id handed to the new craft */
    BaseRec      *const *apBases; /* Map.apBases @ Map+0x1174 */
    u8           *abFacilityGrid; /* gBaseFacGrid @ 0x159768, 1524 bytes */
    ShipCtorEnv  *pCtorEnv;
    PtrArray     *pStrategList;
    i32          *pnStrategSeed;  /* 0x18640C */
    i32          *pnZoomMode;     /* the viewport nMode at 0x1863E8 */
} ShipSpawnEnv;

i32 Ship_SpawnAtSlot(ShipSpawnEnv *pEnv, u8 nCraftType, i32 nHomeBase);

/* --- Ship_ProductSlotToType @ 0x65a6c (88 B) : a craft product slot -> its CraftType ---
 *
 * Renamed in the IDB from BaseFac_RaceCharToIndex: nothing about it is a race, and the five values
 * it tests are the craft-count object slots, not characters. AND R0,R1,#0xFF, then a five-way
 * compare chain on the masked byte:
 *
 *     117 -> 0 Skyranger   118 -> 3 Interceptor   119 -> 4 Firestorm
 *     120 -> 1 Lightning   121 -> 2 Avenger       anything else -> -1
 *
 * The five slots are OBJ_CRAFT_COUNT_* (TEXTS.DAT records 1563..1567 name them SKYRANGER,
 * INTERCEPTOR, FIRE STORM, LIGTHNING (sic) and AVENGER), so the slot name and the CraftType it maps
 * to agree one to one. The first ARM argument -- the grid pointer the caller leaves in R0 -- is
 * never read; the port drops it.
 *
 * One code caller, ManufScreen_CommitProduction @ 0x6F638, which masks the product index itself
 * (AND R1,R7,#0xFF) and sign-extends the result with LSL#24/ASR#24 into Ship_SpawnAtSlot's
 * nCraftType byte, so an unmapped slot would spawn a craft of type 0xFF. That caller's own guard
 * (0x6F5F4: product <= 118 or product >= 122 takes the item branch) keeps the call to 119..121, so
 * the -1 answer is unreachable there. Smap_GrantSiteRecovery carries the same map as an inline
 * switch on the slot INDEX at 0x99360..0x993A8 (0->0, 1->3, 2->4, 3->1, 4->2), which is why the port
 * shares this one function between the two. */
i32 Ship_ProductSlotToType(i32 nProductSlot);

/* --- Ship_FormatDisplayName @ 0x666cc (424 B) : the craft's display name ---
 *
 * Twelve call sites (BaseInfoScreen_BuildPanels, RadarScreen_Update, BaseFac_BuildStatusMsg,
 * OptionsScreen_Open, BasePlaneScreen_DrawCraftDetail, BasePlaneScreen_BuildPanels,
 * Smap_HitTestStrategSelect and Geoscape_Draw four times). The name is the CraftType label plus the
 * craft's own ordinal: `aTextRecs @ 0xB38D0` records 137, 138 and 139 for types 0..2 and 418 and 419
 * for types 3 and 4, and the last two put a "-" (asc_AB340 @ 0xAB340) between the label and the
 * number. The number is Ship.bType @ +0x00 — the per-craft-type counter Ship_Ctor took from
 * 0x18653C..0x186540 — not the craft id.
 *
 * Deviations: a CraftType outside 0..4 falls through the original's switch and it then copies its own
 * uninitialised 104-byte local into the scratch string (bugs.tsv); the port returns an empty string.
 * `apCraft[nShipIdx]` is unbounded in the original. The original builds the string in that local and
 * then copies it into the shared scratch at 0x186B10 (szRadarMsg, 108 bytes) with an unbounded loop;
 * the port writes straight into the caller's buffer with the bound the caller supplies. */
char *Ship_FormatDisplayName(Ship *const *apCraft, i32 nShipIdx, const u8 *pTextRecs,
                             char *pszOut, size_t cchOut);

#endif /* PUFO_SHIP_H */
