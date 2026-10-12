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
    u8  bUnk17;        /* +0x11 */
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

typedef struct Ship {
    u8   bType;        /* +0x00 record kind */
    u8   bId;          /* +0x01 craft id, matched against StrategItem.nDisplayId */
    u8   bCraftType;   /* +0x02 0 interceptor .. 4 ; drives fuel rate and detect radius */
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
    i32  anCargoQty[4];/* +0x24 */
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
    u8  *pbLowSupplyFlag;         /* dword_1870C8 */
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
    PtrArray *pStrategList;       /* 0x1864F8 + 0x3C */
    const struct GeoscapeWorld *pWorld;
    u8 *pnSpawnOnceFlag;          /* 0x162028, the replacement-spawn gate */
    const struct StrategSpawnCtx *pSpawnCtx;
    u8 *abHangarFlag;             /* the six-byte-per-cell table at 0x158CD8 */
    i32 nHangarFlagCap;
    i32 *anAssignedShip;          /* 0x16146C */
    i32 *pnAssignedShipCount;     /* 0x161468, signed */
} ShipRemovalEnv;

/* Returns the ship count after the removal. */
i32 Ship_RemoveWithCargo(const ShipRemovalEnv *pEnv, i32 nShipId);

/* ------------------------------------------------- the cargo hold and what is in it
 *
 * Evidence: Craft_GetTypeCargoCap @ 0x65ac4, Base_CalcWeaponStorageUsed @ 0x65b30.
 *
 * The hold cap is chosen by Ship.bCraftType @ 0x02 and nothing else:
 *
 *     0 interceptor      140
 *     1                  120
 *     2                  260
 *     3 or 4               0
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
typedef enum CraftCargoCap {
    CRAFT_CARGO_CAP_NONE = 0,
    CRAFT_CARGO_CAP_TYPE0 = 140,
    CRAFT_CARGO_CAP_TYPE1 = 120,
    CRAFT_CARGO_CAP_TYPE2 = 260,
    CRAFT_CARGO_CAP_UNKNOWN = -1
} CraftCargoCap;

/* The original's first argument is never read; the port takes the ship directly. */
CraftCargoCap Craft_GetTypeCargoCap(const Ship *pShip);

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

#endif /* PUFO_SHIP_H */
