/* pufo_ship.c - see pufo_ship.h for the evidence map. */
#include "game/pufo_ship.h"

#include <stdio.h>   /* snprintf, the bounded stand-in for the original's j_itoa */
#include <stdlib.h>
#include <string.h>

#include "game/pufo_boot.h"    /* PUFO_TEXT_REC_STRIDE, the craft-name records */
#include "util/pufo_str.h"     /* pufo_strcat */
#include "game/pufo_strateg.h" /* StrategList_RemoveAt, StrategItem */
#include "game/pufo_unit.h"    /* Soldier */
#include "game/pufo_geoscape.h" /* BaseRec_AddItemQty */

BaseRec *BaseRec_Ctor(BaseRec *pBase, i32 nSlotIndex, u8 bUnk186558,
                      ObjectRecFull *const *apObjects, i32 nObjectCount)
{
    if (pBase == NULL) {
        return NULL;
    }

    if (apObjects != NULL && nObjectCount > 0) {
        for (i32 i = 0; i < nObjectCount; ++i) {
            if (apObjects[i] == NULL) {
                continue;
            }
            if (nSlotIndex >= 0 && nSlotIndex < (i32)(sizeof(apObjects[i]->auPerBaseVal)
                                                      / sizeof(apObjects[i]->auPerBaseVal[0]))) {
                apObjects[i]->auPerBaseVal[nSlotIndex] = 0;
            }
        }
    }

    pBase->nStoresTotal = 0;
    pBase->bUnk4 = bUnk186558;
    pBase->bSlot = (u8)nSlotIndex;
    return pBase;
}

Ship *Ship_Ctor(Ship *pShip, u8 nCraftType, i32 nId, i32 nHomeBase, u8 bSkip,
                ShipCtorEnv *pEnv)
{
    u8 bType;

    if (pShip == NULL || pEnv == NULL) {
        return NULL;
    }
    if (bSkip != 0) { /* 0x45C2C: the whole body is behind this byte test */
        return pShip;
    }

    pShip->bId = (u8)nId;
    pShip->bHomeBase = (u8)nHomeBase;
    bType = 1; /* 0x45C44: the value a craft type above 4 keeps */
    pShip->bAmmo25 = 0;
    pShip->bAmmo26 = 0;
    pShip->bAmmo27 = 0;
    pShip->bAmmo29 = 0;
    pShip->bAmmo30 = 0;
    pShip->anCargoQty[0] = 0;
    pShip->anCargoQty[1] = 0;
    pShip->anCargoQty[2] = 0;
    pShip->anCargoQty[3] = 0;
    pShip->bContactMsg = 0;
    pShip->bIntercepting = 0;
    pShip->nStrategIdx = 0;
    pShip->uRangeRemain = 0;
    pShip->bAtBase = 1;   /* 0x45C80 stores the u16 1 here: bAtBase = 1, bRefueling = 0 */
    pShip->bRefueling = 0;
    pShip->bWeaponsHot = 0;
    pShip->bCraftType = nCraftType;

    /* 0x45C98: the cargo column is the ship count as it stands RIGHT NOW. */
    pShip->nSlotIdx = pEnv->nShipCount;
    if (pEnv->apObjects != NULL && pEnv->nObjectCount > 0) {
        for (i32 i = 0; i < pEnv->nObjectCount; ++i) {
            if (pEnv->apObjects[i] == NULL) {
                continue;
            }
            if (pShip->nSlotIdx >= 0 &&
                pShip->nSlotIdx < (i32)(sizeof(pEnv->apObjects[i]->aItemQty)
                                        / sizeof(pEnv->apObjects[i]->aItemQty[0]))) {
                pEnv->apObjects[i]->aItemQty[pShip->nSlotIdx] = 0;
            }
        }
    }

    if (nCraftType < 5) { /* 0x45CEC..0x45D78: bType comes from the per-type counter */
        bType = (u8)(pEnv->abTypeCounters[nCraftType] + 1);
        pEnv->abTypeCounters[nCraftType] = bType;
    }
    pShip->bType = bType;

    pShip->bCrewSlots = 2;
    if (nCraftType == 1) {
        pShip->bCrewSlots = 1;
    }

    switch (nCraftType) {
    case 0:
        pShip->bCrewSlots = 0;
        pShip->bCanDogfight = 1;
        pShip->uRangeMax = 10000;
        pShip->uRangeRate = 20;
        pShip->bNeedsSupply = 0;
        pShip->uFuelMax = 150;
        pShip->nFuel = 150;
        pShip->uUnk14 = 2;
        pShip->uFuelCap = 760;
        break;
    case 1:
        pShip->uRangeMax = 30000;
        pShip->uFuelMax = 800;
        pShip->nFuel = 800;
        pShip->uFuelCap = 3100;
        pShip->uRangeRate = 2;
        pShip->bNeedsSupply = 1;
        pShip->bCanDogfight = 1;
        pShip->uUnk14 = 2;
        break;
    case 2:
        pShip->uRangeMax = 60000; /* 0x45E50 writes -5536 into a u16 */
        pShip->uFuelMax = 1200;
        pShip->nFuel = 1200;
        pShip->uFuelCap = 5400;
        pShip->uRangeRate = 2;
        pShip->bNeedsSupply = 1;
        pShip->bCanDogfight = 1;
        pShip->uUnk14 = 2;
        break;
    case 3:
        pShip->bCanDogfight = 0;
        pShip->uRangeMax = 1000;
        pShip->uRangeRate = 20;
        pShip->bNeedsSupply = 0;
        pShip->uFuelMax = 100;
        pShip->nFuel = 100;
        pShip->uFuelCap = 3000;
        pShip->uUnk14 = 2;
        break;
    case 4:
        pShip->uRangeRate = 2;
        pShip->bNeedsSupply = 1;
        pShip->uRangeMax = 20000;
        pShip->uFuelMax = 500;
        pShip->nFuel = 500;
        pShip->bCanDogfight = 0;
        pShip->uFuelCap = 4500;
        pShip->uUnk14 = 2;
        break;
    default:
        break; /* no range or fuel field is set for a type above 4 */
    }

    pShip->bWeaponL = 0; /* 0x45DD0..0x45DD4: every path that sets fields ends here */
    pShip->bWeaponR = 0;
    return pShip;
}

void Ship_TickRangeRecover(Ship *pShip)
{
    if (pShip->bAtBase != 0) {
        u16 uRangeRemain = pShip->uRangeRemain;
        if (uRangeRemain < pShip->uRangeMax && pShip->bNeedsSupply == 0)
            pShip->uRangeRemain = (u16)(pShip->uRangeRate + uRangeRemain);
    }
    if (pShip->uRangeRemain > pShip->uRangeMax)
        pShip->uRangeRemain = pShip->uRangeMax;
}

/* Ship_FindIndexById @ 0x69ffc. The first craft whose Ship.bId (+0x01) equals nId.
 *
 * The count is read first and a non-positive one answers -1 straight away: 0x6A018 branches to
 * the `MOV R0, #0xFFFFFFFF` at 0x6A044, which is also where the exhausted loop falls through.
 * The match path at 0x6A050 returns the index through `MOV R3, R2, LSL #24` / `MOV R0, R3, ASR
 * #24`, i.e. sign-extended from a byte -- all a byte count can produce.
 *
 * This is the plain half of Ship_FindIndexByStrategType @ 0x5a92c: that one reads the id out of
 * a strateg item first, this one takes it as an argument and has no type or active gate.
 *
 * Deviation: 0x6A024 loads apCraft[i] and dereferences it with no NULL test. The port skips a
 * NULL slot. */
i32 Ship_FindIndexById(Ship *const *apCraft, i32 nCraftCount, i32 nId)
{
    if (apCraft == NULL || nCraftCount <= 0) {
        return -1; /* 0x6A018 -> 0x6A044 */
    }
    for (i32 i = 0; i < nCraftCount; ++i) {
        if (apCraft[i] != NULL && (i32)apCraft[i]->bId == nId) {
            return i; /* 0x6A028 -> 0x6A050 */
        }
    }
    return -1; /* 0x6A044 */
}

void Ship_TickRefuelAtBase(Ship *pShip)
{
    if (pShip->bAtBase != 0) {
        i16 nFuel = pShip->nFuel;
        if (nFuel < (i16)pShip->uFuelMax) {
            pShip->bRefueling = 1;
            switch (pShip->bCraftType) {
            case 0:
            case 3:
                pShip->nFuel = (i16)(nFuel + 1);
                break;
            case 1:
            case 2:
            case 4:
                pShip->nFuel = (i16)(nFuel + 2);
                break;
            default:
                break;
            }
        }
    }
    if (pShip->nFuel >= (i16)pShip->uFuelMax) {
        pShip->nFuel = (i16)pShip->uFuelMax;
        pShip->bRefueling = 0;
    }
}

/* The cargo pair shares one body; only the sign of the two updates differs. */
static Ship *ShipCargoApply(Ship *pSel, ObjectRecFull *pItem, i32 nDelta, bool bAdd)
{
    i32 nSlot;

    if (pSel == NULL || pItem == NULL) {
        return pSel;
    }

    nSlot = pSel->nSlotIdx;
    if (nSlot < 0 ||
        nSlot >= (i32)(sizeof(pItem->aItemQty) / sizeof(pItem->aItemQty[0]))) {
        return pSel; /* deviation: the original indexes this blindly */
    }

    if (bAdd) {
        pItem->aItemQty[nSlot] = (u16)((i32)pItem->aItemQty[nSlot] + nDelta); /* 0x45EB0 */
        pSel->uUnk4 = (u16)((i32)pSel->uUnk4 + nDelta);                        /* 0x45EC0 */
    } else {
        pItem->aItemQty[nSlot] = (u16)((i32)pItem->aItemQty[nSlot] - nDelta); /* 0x45EEC */
        pSel->uUnk4 = (u16)((i32)pSel->uUnk4 - nDelta);                        /* 0x45EFC */
    }
    return pSel;
}

/* InvSel_AddItemQty @ 0x45e8c. */
Ship *InvSel_AddItemQty(Ship *pSel, ObjectRecFull *pItem, i16 nDelta)
{
    return ShipCargoApply(pSel, pItem, nDelta, true);
}

/* InvSel_SubItemQty @ 0x45ec8. */
Ship *InvSel_SubItemQty(Ship *pSel, ObjectRecFull *pItem, i16 nDelta)
{
    return ShipCargoApply(pSel, pItem, nDelta, false);
}

/* BaseRec_SubItemQty @ 0x5571c. */
void BaseRec_SubItemQty(BaseRec *pBase, ObjectRecFull *pItem, i32 nDelta)
{
    pItem->auPerBaseVal[(u8)pBase->bSlot] = (u16)(pItem->auPerBaseVal[(u8)pBase->bSlot] - nDelta);
    pBase->nStoresTotal -= nDelta;
}

void Ship_TickRangeRecoverConsume(Ship *pShip, const ShipSupplyHooks *pHooks)
{
    if (pShip->bAtBase != 0) {
        u16 uRangeRemain = pShip->uRangeRemain;
        if (uRangeRemain < pShip->uRangeMax && pShip->bNeedsSupply != 0) {
            u16 uRangeRate = pShip->uRangeRate;
            BaseRec *pHome = pHooks->apBases[pShip->bHomeBase];
            ObjectRecFull *pItem = pHooks->pSupplyItem;
            if (pItem->auPerBaseVal[pShip->bHomeBase] > uRangeRate) {
                pShip->uRangeRemain = (u16)(1000u * uRangeRate + uRangeRemain);
                BaseRec_SubItemQty(pHome, pItem, uRangeRate);
                if (pItem->auPerBaseVal[pShip->bHomeBase] < uRangeRate) {
                    *pHooks->pbLowSupplyFlag = 1;
                    pHooks->pfnLowSupplyMsg(pShip->bHomeBase, pShip->nSlotIdx,
                                            pHooks->pUser);
                }
            }
        }
    }
    if (pShip->uRangeRemain > pShip->uRangeMax)
        pShip->uRangeRemain = pShip->uRangeMax;
}

/* Ship_RemoveWithCargo @ 0x36830.
 *
 * The ship array is compacted by struct copy: ARM at 0x36A50 does
 * memmove(apCraft[i-1], apCraft[i], 0x44), 68 bytes, sizeof(Ship). The pointer array keeps
 * its pointers and only the last object is deleted, at 0x36B40 after the count drops.
 * The cargo column moves with the ships: ARM at 0x36A24 reads ObjectRecFull.aItemQty[i] at
 * byte 0x3A + 2*i and stores it at 0x3A + 2*i - 2. */
i32 Ship_RemoveWithCargo(const ShipRemovalEnv *pEnv, i32 nShipId)
{
    if (pEnv == NULL || pEnv->apCraft == NULL || pEnv->pnShipCount == NULL) {
        return 0;
    }

    i32 nCount = (u8)*pEnv->pnShipCount;
    i32 nIdx = 0;

    /* 0x3684C..0x3687C: find the craft by its id byte. */
    while (nIdx < nCount && pEnv->apCraft[nIdx]->bId != (u8)nShipId) {
        ++nIdx;
    }
    if (nIdx >= nCount) {
        /* The original proceeds with an index equal to the count and deletes apCraft[count]. */
        return nCount;
    }

    Ship *pShip = pEnv->apCraft[nIdx];

    /* 0x368CC..0x368D8: clear this craft's hangar flag. Six bytes per cell, and the three
     * bytes feeding the index are the home base and the two hangar coordinates. */
    if (pEnv->abHangarFlag != NULL) {
        i32 nFlagIdx = 6 * (24 * (i32)pShip->bHomeBase + (i32)pShip->bHangarY + 12)
                     + (i32)pShip->bHangarX;
        if (nFlagIdx >= 0 && nFlagIdx < pEnv->nHangarFlagCap) {
            pEnv->abHangarFlag[nFlagIdx] = 0;
        }
    }

    /* 0x368E0..0x36934: drop the strateg entry that carries this craft. The gate is
     * nType == 1 and nDisplayId == Ship.bId. */
    if (pEnv->pStrategList != NULL) {
        i32 nItems = pEnv->pStrategList->nCount;
        i32 nStrateg = 0;
        while (nStrateg < nItems) {
            StrategItem *pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, nStrateg);
            if (pItem != NULL && pItem->nType == 1 && pItem->nDisplayId == (i32)pShip->bId) {
                break;
            }
            ++nStrateg;
        }
        if (nStrateg < nItems) {
            StrategList_RemoveAt(pEnv->pStrategList, pEnv->pWorld, nStrateg,
                                 pEnv->pnSpawnOnceFlag, pEnv->pSpawnCtx);
        }
    }

    /* 0x36948..0x369BC: delete the crew. A removal shifts the list, so the walk does not
     * advance; only a soldier that stays moves the cursor on. */
    if (pEnv->apSoldiers != NULL && pEnv->pnSoldierCount != NULL) {
        u16 nSoldiers = *pEnv->pnSoldierCount;
        i32 i = 0;
        while (i < (i32)nSoldiers) {
            if (pEnv->apSoldiers[i]->bUnk183 == (u8)nIdx) {
                free(pEnv->apSoldiers[i]);
                for (i32 k = i + 1; k < (i32)nSoldiers; ++k) {
                    pEnv->apSoldiers[k - 1] = pEnv->apSoldiers[k];
                }
                --nSoldiers;
                *pEnv->pnSoldierCount = nSoldiers;
            } else {
                ++i;
            }
        }
    }

    /* 0x369D8..0x36A70: shift every craft above this one down, and shift the cargo column
     * in every object record along with it. */
    for (i32 i = nIdx + 1; i < nCount; ++i) {
        if (pEnv->apObjects != NULL) {
            for (u16 o = 0; o < pEnv->nObjectCount; ++o) {
                pEnv->apObjects[o]->aItemQty[i - 1] = pEnv->apObjects[o]->aItemQty[i];
            }
        }
        memmove(pEnv->apCraft[i - 1], pEnv->apCraft[i], sizeof(Ship));
    }

    /* 0x36A88..0x36AE4: every remaining soldier's ship index above the gap moves down one.
     * 254 and 255 are the "not assigned" sentinels and are left alone. */
    if (pEnv->apSoldiers != NULL) {
        u16 nSoldiers = *pEnv->pnSoldierCount;
        for (i32 i = 0; i < (i32)nSoldiers; ++i) {
            u8 b = pEnv->apSoldiers[i]->bUnk183;
            if (b != 255 && b != 254 && (i32)b > nIdx) {
                pEnv->apSoldiers[i]->bUnk183 = (u8)(b - 1);
            }
        }
    }

    /* 0x36AEC..0x36B20: the assigned-ship list at 0x16146C, count at 0x161468. */
    if (pEnv->anAssignedShip != NULL && pEnv->pnAssignedShipCount != NULL) {
        for (i32 i = 0; i < *pEnv->pnAssignedShipCount; ++i) {
            if (pEnv->anAssignedShip[i] > nIdx) {
                pEnv->anAssignedShip[i] -= 1;
            }
        }
    }

    /* 0x36B34..0x36B40: drop the count, then delete the object the last slot held. */
    nCount -= 1;
    *pEnv->pnShipCount = (u8)nCount;
    free(pEnv->apCraft[nCount]);

    /* 0x36B50..0x36B80: renumber the hangar slots of everything that is left. */
    for (i32 i = 0; i < nCount; ++i) {
        pEnv->apCraft[i]->nSlotIdx = i;
    }

    return nCount;
}

/* Craft_GetTypeCargoCap @ 0x65ac4. The hold cap is chosen by Ship.bCraftType @ 0x02 and
 * nothing else. The -1 for an unknown craft type is not a cap; a caller comparing against
 * it gets a hold nothing fits into. */
CraftCargoCap Craft_GetTypeCargoCap(const Ship *pShip)
{
    if (pShip == NULL) {
        return CRAFT_CARGO_CAP_UNKNOWN;
    }

    switch (pShip->bCraftType) {
    case 0: return CRAFT_CARGO_CAP_TYPE0; /* 140 */
    case 1: return CRAFT_CARGO_CAP_TYPE1; /* 120 */
    case 2: return CRAFT_CARGO_CAP_TYPE2; /* 260 */
    case 3:
    case 4: return CRAFT_CARGO_CAP_NONE;  /*   0 */
    default: return CRAFT_CARGO_CAP_UNKNOWN;
    }
}

/* Base_CalcWeaponStorageUsed @ 0x65b30. What the craft already carries, charged at each
 * object's per-unit size.
 *
 * The ARM states the index filter literally at 0x65B74:
 *   (index > 0 && index < 25) || (index > 35 && index < 52)
 * The first half looks like a pointer compare against the Map base because the compiler
 * walked apObjects as Map + 4*index; with the loop starting at index 1 it is just
 * `index > 0`. */
i32 Base_CalcWeaponStorageUsed(ObjectRecFull *const *apObjects, i32 nObjectCount,
                               i32 nCraftIdx)
{
    i32 nUsed = 0;

    if (apObjects == NULL || nObjectCount <= 1 || nCraftIdx < 0 || nCraftIdx >= 52) {
        return 0;
    }

    for (i32 i = 1; i < nObjectCount; ++i) {
        const bool bWeapon = (i > 0 && i < 25);
        const bool bAmmo = (i > 35 && i < 52);

        if (!bWeapon && !bAmmo) {
            continue;
        }

        ObjectRecFull *pObj = apObjects[i];
        if (pObj == NULL) {
            continue;
        }

        u16 nCarried = pObj->aItemQty[nCraftIdx];
        if (nCarried != 0) {
            nUsed += (i32)pObj->bCargoSize * (i32)nCarried; /* 0x65B98 */
        }
    }

    return nUsed;
}

/* Craft_LoadStoresFromBase @ 0x693ec.
 *
 * Fill the craft from its home base, one unit at a time. Each unit is gated on the hold
 * having room for it: Base_CalcWeaponStorageUsed + that object's bCargoSize has to stay under
 * Craft_GetTypeCargoCap, and the capacity is re-read every unit, so a load that fills the hold
 * stops early rather than overshooting.
 *
 * The default case is one ammo object: take min(what the craft carries of the weapon object,
 * what the base has of the ammo object).
 *
 * Kinds 7, 8 and 10 split the wanted load across three ammo pools in order. ARM
 * 0x69884..0x698E4 works it out as: take what the first pool has, or the whole want if the
 * first pool covers it; the remainder goes to the second pool the same way; what is left after
 * the second pool is capped by the third. */
static i32 LoadUnits(const CraftStoreLoadEnv *pEnv, Ship *pShip, BaseRec *pBase,
                     ObjectRecFull *pObj, i32 nCraftIdx, i32 nUnits)
{
    for (i32 i = 0; i < nUnits; ++i) {
        i32 nUsed = Base_CalcWeaponStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nCraftIdx);
        i32 nCap = (i32)Craft_GetTypeCargoCap(pShip);
        if (nUsed + (i32)pObj->bCargoSize < nCap) {
            (void)InvSel_AddItemQty(pShip, pObj, 1);
            (void)BaseRec_SubItemQty(pBase, pObj, 1);
        }
    }
    return nUnits;
}

void Craft_LoadStoresFromBase(const CraftStoreLoadEnv *pEnv, u8 nItemKind, u8 nSrc,
                              i32 nCraftIdx)
{
    if (pEnv == NULL || pEnv->apCraft == NULL || pEnv->apObjects == NULL ||
        pEnv->apBases == NULL || nCraftIdx < 0 || nCraftIdx >= 52) {
        return;
    }

    Ship *pShip = pEnv->apCraft[nCraftIdx];
    if (pShip == NULL) {
        return;
    }

    i32 nHome = (i32)pShip->bHomeBase;
    if (nHome < 0 || nHome >= 8) { /* auPerBaseVal has eight columns */
        return;
    }

    BaseRec *pBase = pEnv->apBases[nHome];
    if (pBase == NULL) {
        return;
    }

    i32 nWeapon = (i32)nItemKind;
    if (nWeapon < 0 || nWeapon >= pEnv->nObjectCount || pEnv->apObjects[nWeapon] == NULL) {
        return;
    }

    i32 nWant = (i32)pEnv->apObjects[nWeapon]->aItemQty[nCraftIdx];

    i32 aPool[3];
    i32 nPool = 1;

    switch (nItemKind) {
    case 7:  aPool[0] = 40; aPool[1] = 41; aPool[2] = 42; nPool = 3; break;
    case 8:  aPool[0] = 43; aPool[1] = 44; aPool[2] = 45; nPool = 3; break;
    case 10: aPool[0] = 46; aPool[1] = 47; aPool[2] = 48; nPool = 3; break;
    default: aPool[0] = (i32)nSrc; break;
    }

    /* Split the wanted load across the pools, first pool first. */
    i32 nTake[3];
    i32 nRemain = nWant;
    for (i32 p = 0; p < nPool; ++p) {
        i32 nIdx = aPool[p];
        if (nIdx < 0 || nIdx >= pEnv->nObjectCount || pEnv->apObjects[nIdx] == NULL) {
            nTake[p] = 0;
            continue;
        }
        i32 nStock = (i32)pEnv->apObjects[nIdx]->auPerBaseVal[nHome];
        nTake[p] = (nRemain < nStock) ? nRemain : nStock;
        nRemain -= nTake[p];
    }

    for (i32 p = 0; p < nPool; ++p) {
        if (nTake[p] > 0) {
            (void)LoadUnits(pEnv, pShip, pBase, pEnv->apObjects[aPool[p]], nCraftIdx, nTake[p]);
        }
    }
}

/* Base_StripAllCraftCargo @ 0x69a38.
 *
 * For every craft with Ship.bCanDogfight @ 0x21 set, dump the ammo it is carrying into its
 * home base and then refill it from that base. Ten groups, and each group is the same three
 * steps: a gate, a deposit, a refill.
 *
 * The gate is always "the craft carries less ammo than it carries weapons", where the ammo is
 * the sum over the group. The comparison is unsigned and it is taken against the values BEFORE
 * the deposit, so a group that already has enough is left completely alone.
 *
 * The deposit is per ammo object: subtract what the craft carries from the craft, add the same
 * amount to the base. Then Craft_LoadStoresFromBase pulls back min(weapons carried, base stock),
 * so the net effect is that the craft ends up carrying exactly as many rounds as it has weapons,
 * as far as the base can supply them.
 *
 * Callers are Base_TransferTacticalLoot and Base_DistributeRecoveredItems, not the hangar UI:
 * the hangar has its own CommitStores / Rearm / BasePlane_LoadWeaponStores path. */
typedef struct {
    u8 nKind;      /* the nItemKind to hand Craft_LoadStoresFromBase */
    u8 aAmmo[3];   /* the ammo objects in the group, in pool order */
    u8 nAmmoCount;
    u8 nWeaponObj; /* the weapon object the carried count is read from */
} CraftAmmoGroup;

static const CraftAmmoGroup g_aCraftAmmoGroups[10] = {
    {  1, { 36,  0,  0 }, 1,  1 },
    {  3, { 37,  0,  0 }, 1,  3 },
    {  4, { 38,  0,  0 }, 1,  4 },
    {  6, { 39,  0,  0 }, 1,  6 },
    {  7, { 40, 41, 42 }, 3,  7 },
    {  8, { 43, 44, 45 }, 3,  8 },
    { 10, { 46, 47, 48 }, 3, 10 },
    { 11, { 49,  0,  0 }, 1, 11 },
    { 12, { 50,  0,  0 }, 1, 12 },
    { 13, { 51,  0,  0 }, 1, 13 },
};

void Base_StripAllCraftCargo(const CraftStoreLoadEnv *pEnv, i32 nShipCount)
{
    if (pEnv == NULL || pEnv->apCraft == NULL || pEnv->apObjects == NULL ||
        pEnv->apBases == NULL) {
        return;
    }

    for (i32 n = 0; n < nShipCount; ++n) {
        Ship *pShip = pEnv->apCraft[n];
        if (pShip == NULL || pShip->bCanDogfight == 0) {
            continue;                                   /* 0x69A7C */
        }

        i32 nHome = (i32)pShip->bHomeBase;
        if (nHome < 0 || nHome >= 8) {
            continue;
        }
        BaseRec *pBase = pEnv->apBases[nHome];
        if (pBase == NULL) {
            continue;
        }

        for (i32 g = 0; g < 10; ++g) {
            const CraftAmmoGroup *pGroup = &g_aCraftAmmoGroups[g];

            if (pGroup->nWeaponObj >= pEnv->nObjectCount ||
                pEnv->apObjects[pGroup->nWeaponObj] == NULL) {
                continue;
            }

            i32 nWeapons = (i32)pEnv->apObjects[pGroup->nWeaponObj]->aItemQty[n];
            i32 nCarried = 0;
            for (i32 a = 0; a < pGroup->nAmmoCount; ++a) {
                if (pGroup->aAmmo[a] < pEnv->nObjectCount &&
                    pEnv->apObjects[pGroup->aAmmo[a]] != NULL) {
                    nCarried += (i32)pEnv->apObjects[pGroup->aAmmo[a]]->aItemQty[n];
                }
            }

            if (!(nCarried < nWeapons)) {
                continue;                               /* 0x69AA0 and friends */
            }

            for (i32 a = 0; a < pGroup->nAmmoCount; ++a) {
                ObjectRecFull *pAmmo = pEnv->apObjects[pGroup->aAmmo[a]];
                if (pAmmo == NULL) {
                    continue;
                }
                i32 nAmount = (i32)pAmmo->aItemQty[n];
                (void)InvSel_SubItemQty(pShip, pAmmo, (i16)nAmount);  /* 0x69AAC */
                BaseRec_AddItemQty(pBase, pAmmo, nAmount);            /* 0x69AC8 */
            }

            Craft_LoadStoresFromBase(pEnv, pGroup->nKind, pGroup->aAmmo[0], n); /* 0x69ADC */
        }
    }
}

/* The five weapon mounts Base_DistributeRecoveredItems resupplies. nWeapon is the value
 * Ship.bWeaponL / bWeaponR must hold, nObject the ammo record, nOff the Ship byte holding this
 * weapon's quantity, and nTenths the scale that byte is stored in -- Elerium travels in tenths. */
typedef struct CraftWeaponResupply {
    i8  nWeapon;
    u16 nObject;
    u8  nOff;
    u8  nPerMount;
    u8  nTenths;
} CraftWeaponResupply;

static const CraftWeaponResupply g_aCraftWeaponResupply[5] = {
    { 25, OBJ_CRAFT_CANNON_ROUNDS, 0x3D, 10, 1 },  /* 0x69174 */
    { 26, OBJ_STINGRAY_ROCKET,     0x3E,  3, 1 },  /* 0x69120 */
    { 27, OBJ_AVALANCHE_ROCKET,    0x3F,  2, 1 },  /* 0x690D8 */
    { 29, OBJ_ELERIUM_115,         0x40, 10, 10 }, /* 0x69074 */
    { 30, OBJ_FUSION_BALL,         0x41,  2, 1 },  /* 0x6900C */
};

/* One mount, both passes. The craft's quantity goes to the base, the byte is cleared, and the craft
 * is re-supplied with perMount * load -- or with whatever the base had, if that is less. */
static void Distribute_ResupplyMount(BaseRec *pBase, Ship *pShip,
                                     ObjectRecFull **apObjects,
                                     const CraftWeaponResupply *pRule, i32 nBase, i32 nLoad)
{
    ObjectRecFull *pItem = apObjects[pRule->nObject];
    if (pItem == NULL) {
        return;
    }

    u8 *const pbQty = (u8 *)pShip + pRule->nOff;
    const i32 nTenths = pRule->nTenths;

    BaseRec_AddItemQty(pBase, pItem, (nTenths == 1) ? (i32)*pbQty : ((i32)*pbQty) / 10);
    *pbQty = 0;

    const i32 nUnits = (i32)pRule->nPerMount * nLoad;
    const i32 nStock = (i32)pItem->auPerBaseVal[nBase];

    if (nStock < nUnits) {
        *pbQty = (u8)(nTenths * nStock);
        pItem->auPerBaseVal[nBase] = 0; /* 0x69150 */
    } else {
        *pbQty = (u8)(nTenths * nUnits);
        BaseRec_SubItemQty(pBase, pItem, nUnits);
    }
}

/* Base_DistributeRecoveredItems @ 0x68f2c. See the header for the mount table, the load factor and
 * the Elerium tenths. */
void Base_DistributeRecoveredItems(const BaseRecoverDistributeEnv *pEnv)
{
    if (pEnv == NULL || pEnv->apCraft == NULL || pEnv->apBases == NULL ||
        pEnv->apObjects == NULL) {
        return;
    }

    const i32 nCraftCount = (pEnv->pnCraftCount != NULL) ? (i32)*pEnv->pnCraftCount : 0;

    CraftStoreLoadEnv load;
    load.apCraft = pEnv->apCraft;
    load.apBases = pEnv->apBases;
    load.apObjects = pEnv->apObjects;
    load.nObjectCount = pEnv->nObjectCount;
    Base_StripAllCraftCargo(&load, nCraftCount); /* 0x68F3C */

    const i32 nBaseCount = (pEnv->pbBaseCount != NULL) ? (i8)*pEnv->pbBaseCount : 0;
    if (nBaseCount <= 0) {
        return; /* 0x68F58 */
    }

    /* Deviation: the original bounds nothing here. auPerBaseVal has eight columns and apBases has
     * eight records, so a base count above eight would read past both. */
    const i32 nBases = (nBaseCount > PUFO_FAC_BASE_COUNT) ? PUFO_FAC_BASE_COUNT : nBaseCount;

    for (i32 nBase = 0; nBase < nBases; ++nBase) {
        BaseRec *pBase = pEnv->apBases[nBase];

        for (i32 nCraft = 0; nCraft < nCraftCount; ++nCraft) {
            Ship *pShip = pEnv->apCraft[nCraft];
            if (pShip == NULL || pShip->bAtBase == 0 || (i32)pShip->bHomeBase != nBase) {
                continue; /* 0x68FC0 */
            }

            const i32 nLoad = (pShip->bWeaponL == pShip->bWeaponR) ? 2 : 1; /* 0x68FC4 */

            for (i32 k = 0; k < 2; ++k) {
                const i8 nWeapon = (k == 0) ? pShip->bWeaponL : pShip->bWeaponR;
                for (i32 r = 0; r < 5; ++r) {
                    if (g_aCraftWeaponResupply[r].nWeapon == nWeapon) {
                        Distribute_ResupplyMount(pBase, pShip, pEnv->apObjects,
                                                 &g_aCraftWeaponResupply[r], nBase, nLoad);
                        break;
                    }
                }
            }
        }
    }
}

/* Base_CraftWeaponItemId @ 0x6641c. The selected base's stock of the item a craft stores slot
 * refers to. Its second argument is the slot, and the call site at 0x7ADC4 passes the third as
 * the signed global byte 0x159D81 -- the byte sitting right next to the base count at 0x159D80,
 * which is the base the base screens are currently showing.
 *
 *   slot 31 -> apObjects[56]   slot 32 -> apObjects[57]
 *   slot 34 -> apObjects[105]  slot 35 -> apObjects[58]
 *   slot 33 -> 1000, a constant, with no object record touched
 *   anything else -> -1
 *
 * Slot 33 returning a fixed 1000 is the odd one: whatever that slot holds, the original does not
 * keep a per-base count for it.
 *
 * Deviations: the original indexes auPerBaseVal with a signed byte and no bound, and faults on a
 * NULL object record. The port bounds the column to the eight real columns and returns -1 for a
 * NULL record. */
i32 Base_CraftWeaponItemId(ObjectRecFull *const *apObjects, i32 nSlot, i32 nBaseColumn)
{
    static const i32 anSlotObject[5] = { 56, 57, -1, 105, 58 }; /* 0x6642C..0x66484 */

    if (nSlot < 31 || nSlot > 35) {
        return -1; /* 0x6644C */
    }
    if (nSlot == 33) {
        return 1000; /* 0x66470: no object record is read */
    }
    if (nBaseColumn < 0 || nBaseColumn >= 8) {
        return -1; /* deviation: the original has no bound on the column */
    }
    if (apObjects == NULL) {
        return -1;
    }

    const i32 nObject = anSlotObject[nSlot - 31];
    if (nObject < 0 || apObjects[nObject] == NULL) {
        return -1; /* deviation: the original faults here */
    }

    return (i32)apObjects[nObject]->auPerBaseVal[nBaseColumn]; /* 0x66498 */
}

/* Craft_CountSoldiersAboard @ 0x6649c, renamed in the IDB from Base_CountSoldiersAtBase.
 * It counts soldiers whose bUnk183 equals the argument, and bUnk183 is the craft a soldier is
 * assigned to -- 254 and 255 being the sentinels Ship_RemoveWithCargo leaves alone. The argument
 * is a craft index, not a base index: BasePlaneScreen_CommitStores passes unk_161AC4 here and
 * passes the same value to Craft_GetTypeCargoCap and Base_CalcWeaponStorageUsed as nCraftIdx.
 *
 * Deviation: the original dereferences every slot without a NULL test. */
i32 Craft_CountSoldiersAboard(Soldier *const *apSoldiers, i32 nSoldierCount, i32 nCraftIndex)
{
    i32 nCount = 0;

    if (apSoldiers == NULL) {
        return 0;
    }

    for (i32 i = 0; i < nSoldierCount; ++i) {
        if (apSoldiers[i] != NULL && (i32)apSoldiers[i]->bUnk183 == nCraftIndex) {
            ++nCount; /* 0x664DC */
        }
    }

    return nCount;
}

/* BasePlane_CalcWeaponStoresUsed @ 0x78e78. What the hangar screen is about to load: the per-slot
 * carried counts it has accumulated, weighted by each object's per-unit cargo size. It walks EVERY
 * object index, not a filtered range -- the filtering already happened when the counts were
 * recorded.
 *
 * anCarried is unk_161AD0, which BasePlaneScreen_CommitStores increments at 0x7AFD8 for each row
 * it accepts. Hex-Rays renders this loop as one 64-bit value because the index lives in the high
 * word and the running total in the low word. */
i32 BasePlane_CalcWeaponStoresUsed(ObjectRecFull *const *apObjects, i32 nObjectCount,
                                   const i32 *anCarried)
{
    i32 nUsed = 0;

    if (apObjects == NULL || anCarried == NULL || nObjectCount <= 0) {
        return 0; /* 0x78E98 */
    }

    for (i32 i = 0; i < nObjectCount; ++i) {
        if (apObjects[i] == NULL) {
            continue; /* deviation: the original faults on an empty record */
        }
        nUsed += (i32)apObjects[i]->bCargoSize * anCarried[i]; /* 0x78EBC */
    }

    return nUsed;
}

/* Craft_CountWeaponSlotsCarried @ 0x664f0, renamed in the IDB from Base_CountAmmoObjectsAtBase.
 * Sums apObjects[i]->aItemQty[nCraftIdx] over object index 31..35 only. Neither half of the old name
 * was true: the field is aItemQty @ +0x3A (auPerBaseVal is @ +0x2A), which is the PER-CRAFT column,
 * and objects 31..35 are the weapon slots, not the ammo records (those are 56/57/105/58). Both
 * callers pass a craft index -- BasePlaneScreen_FillCrewPanel passes nSelectedCraftIdx, and
 * OptionsScreen_Open passes the same register it hands Craft_CountSoldiersAboard.
 *
 * Deviation: the original bounds nothing and faults on an empty record. The port bounds the craft
 * column to the 52 real entries and skips a NULL record. */
i32 Craft_CountWeaponSlotsCarried(ObjectRecFull *const *apObjects, i32 nObjectCount, i32 nCraftIdx)
{
    i32 nTotal = 0;

    if (apObjects == NULL || nCraftIdx < 0 || nCraftIdx >= 52) {
        return 0;
    }

    for (i32 i = 0; i < nObjectCount; ++i) {
        if (i > 30 && i < 36 && apObjects[i] != NULL) {
            nTotal += (i32)apObjects[i]->aItemQty[nCraftIdx]; /* 0x66520 */
        }
    }

    return nTotal;
}

/* StrategViewOf, the PtrArray -> StrategList search view, now lives in pufo_strateg.h so the
 * load driver can use it too. */

/* BaseRec_Create @ 0x32c88. Allocate one 20-byte base record, run BaseRec_Ctor over it, and put
 * it in the first free roster slot -- index nBaseCount, which the CALLER increments. This function
 * does not touch the count.
 *
 * The original stores the result even when operator new returns null, so a failed allocation leaves
 * a NULL in the roster. The port does the same, because the save layer already has to cope with
 * short reads leaving slots empty. */
BaseRec *BaseRec_Create(BaseRec **apBases, i32 nBaseCount, ObjectRecFull *const *apObjects,
                        i32 nObjectCount)
{
    BaseRec *pNew = (BaseRec *)malloc(sizeof(BaseRec)); /* 0x32C94: operator new(0x14) */

    if (pNew != NULL) {
        /* 0x32CA0 calls BaseRec_ctor with only the record. The port's ctor takes the roster slot
         * and the object table as well, because the ctor zeroes auPerBaseVal[byte 0x159D80] in
         * every object record; the new base's slot is its own index. */
        BaseRec_Ctor(pNew, nBaseCount, 0, apObjects, nObjectCount);
    }

    if (apBases != NULL && nBaseCount >= 0) {
        apBases[nBaseCount] = pNew; /* 0x32CC8 */
    }

    return pNew;
}

/* Ship_RetargetTravel @ 0x5a484. Send a craft home. Three steps, in this order.
 *
 * 1. If the craft is intercepting a waypoint item (nType 5), drop that item from the strateg list
 *    and clear bIntercepting. A waypoint is a one-shot contact, so it is removed rather than left
 *    pointing at a craft that no longer wants it.
 * 2. Find the craft's own strateg entry and clear its nTimer (+0x2C) to 0, which restarts the
 *    proximity-detect accumulation.
 * 3. Run StrategEntry_InitTravel from the entry's current position to its HOME BASE's coordinates
 *    (BaseRec.nUnk8 / nUnk12), with nFlag 0, bA 0, bB 1, bC 1, bCopyStart 0.
 *
 * Ship_ClearWaypointIntercept (0x5a830) is this same body with step 3 replaced by nothing and step
 * 2 setting nTimer to 0xFFFF instead of 0 -- "never fire" rather than "start counting". */
i32 Ship_RetargetTravel(const ShipRemovalEnv *pEnv, i32 nShipIdx)
{
    if (pEnv == NULL || pEnv->apCraft == NULL || nShipIdx < 0 ||
        nShipIdx >= (i32)*pEnv->pnShipCount) {
        return -1;
    }

    Ship *pShip = pEnv->apCraft[nShipIdx];
    if (pShip == NULL) {
        return -1;
    }

    if (pShip->bIntercepting != 0 && pEnv->pStrategList != NULL) {
        const StrategItem *pWaypoint =
            (const StrategItem *)PtrArray_Get(pEnv->pStrategList, pShip->nStrategIdx);
        if (pWaypoint != NULL && pWaypoint->nType == 5) { /* 0x5A4E8 */
            StrategList_RemoveAt(pEnv->pStrategList, pEnv->pWorld, pShip->nStrategIdx,
                                 pEnv->pnSpawnOnceFlag, pEnv->pSpawnCtx);
            pShip->bIntercepting = 0; /* 0x5A500 */
        }
    }

    const StrategList view = StrategViewOf(pEnv->pStrategList);
    const i32 nEntryIdx = StrategList_FindByShipId(&view, pEnv->pWorld, nShipIdx);
    StrategItem *pEntry = (StrategItem *)PtrArray_Get(pEnv->pStrategList, nEntryIdx);
    if (pEntry == NULL) {
        return -1; /* deviation: the original dereferences it */
    }

    pEntry->nTimer = 0; /* 0x5A524: restart the proximity accumulation */

    const BaseRec *pHome = (pEnv->apBases != NULL && pShip->bHomeBase >= 0)
                               ? pEnv->apBases[pShip->bHomeBase]
                               : NULL;
    if (pHome == NULL) {
        return -1; /* deviation: the original indexes apBases with an unchecked byte */
    }

    const i32 nResult = StrategEntry_InitTravel(pEntry, pEntry->nX, pEntry->nY,
                                                pHome->nUnk8, pHome->nUnk12, 0, 0, 1, 1, 0,
                                                pEnv->pWorld,
                                                pEnv->pnGameTotalSec != NULL
                                                    ? *pEnv->pnGameTotalSec
                                                    : 0);
    pShip->bIntercepting = 0; /* 0x5A59C */
    return nResult;
}

/* Ship_LaunchIntercept @ 0x5a64c. The mirror of Ship_RetargetTravel: the craft goes OUT to a strateg
 * item instead of HOME to its base. IDA called this Ship_AbortCourse; it aborts nothing. */
i32 Ship_LaunchIntercept(const ShipRemovalEnv *pEnv, i32 nShipIdx, i32 nStrategIdx)
{
    if (pEnv == NULL || pEnv->apCraft == NULL || pEnv->pStrategList == NULL ||
        nShipIdx < 0 || nShipIdx >= (i32)*pEnv->pnShipCount) {
        return -1;
    }

    Ship *pShip = pEnv->apCraft[nShipIdx];
    if (pShip == NULL) {
        return -1;
    }

    const StrategList view = StrategViewOf(pEnv->pStrategList);
    const i32 nEntryIdx = StrategList_FindByShipId(&view, pEnv->pWorld, nShipIdx);
    StrategItem *pEntry = (StrategItem *)PtrArray_Get(pEnv->pStrategList, nEntryIdx);
    if (pEntry == NULL) {
        return -1; /* deviation: the original dereferences it */
    }

    /* Deviation: the original reads list[nStrategIdx].nX at 0x5A7B8, after every store below, and
     * faults outright when the index is out of range. The port looks the target up here and refuses
     * the whole call instead, so a stale target index cannot leave the craft half-launched. */
    const StrategItem *pTarget = (const StrategItem *)PtrArray_Get(pEnv->pStrategList, nStrategIdx);
    if (pTarget == NULL) {
        return -1;
    }

    pEntry->nTimer = 0;                /* 0x5A67C: restart the proximity accumulation */
    pEntry->nStartX = pEntry->nX;      /* 0x5A6A4 */
    pEntry->nStartY = pEntry->nY;      /* 0x5A6C8 */

    /* 0x5A6E8: a craft that is sitting at its base leaves from the base, not from wherever its entry
     * last drew. The leg start AND the entry's own position are both snapped to the base. */
    if (pShip->bAtBase != 0) {
        /* Deviation: the original indexes apBases with an unchecked byte, so a bHomeBase outside
         * 0..7 reads past the eight-slot array. The port bounds it, as the other base-index users do. */
        const BaseRec *pHome = (pEnv->apBases != NULL && pShip->bHomeBase < PUFO_FAC_BASE_COUNT)
                                   ? pEnv->apBases[pShip->bHomeBase]
                                   : NULL;
        if (pHome != NULL) {
            pEntry->nStartX = pHome->nUnk8;   /* 0x5A718 */
            pEntry->nStartY = pHome->nUnk12;  /* 0x5A73C */
            pEntry->nX = pEntry->nStartX;     /* 0x5A760 */
            pEntry->nY = pEntry->nStartY;     /* 0x5A780 */
        }
    }

    pShip->bWeaponsHot = 1;             /* 0x5A790 */
    pShip->nStrategIdx = nStrategIdx;   /* 0x5A79C */
    pShip->bAtBase = 0;                 /* 0x5A7A4 */
    pShip->bIntercepting = 1;           /* 0x5A7AC */

    /* 0x5A818. The only caller of StrategEntry_InitTravel that ever sets bCopyStart. */
    return StrategEntry_InitTravel(pEntry, pEntry->nStartX, pEntry->nStartY,
                                   pTarget->nX, pTarget->nY, 0, 0, 0, 0, 1,
                                   pEnv->pWorld,
                                   pEnv->pnGameTotalSec != NULL ? *pEnv->pnGameTotalSec : 0);
}

/* Ship_ClearInterceptByStrategIdx @ 0x5a5b8. Retarget every craft that is chasing ONE strateg
 * item -- the cleanup a removed contact needs. A craft qualifies when its home base is a real one
 * (bHomeBase < 0xFE), it is intercepting, and its nStrategIdx is the item. Returns the ship count,
 * re-read after each retarget because StrategList_RemoveAt can spawn a replacement craft. */
i32 Ship_ClearInterceptByStrategIdx(const ShipRemovalEnv *pEnv, i32 nStrategIdx)
{
    if (pEnv == NULL || pEnv->apCraft == NULL) {
        return 0;
    }

    i32 nShipCount = (i32)*pEnv->pnShipCount;

    for (i32 i = 0; i < nShipCount; ++i) {
        const Ship *pShip = pEnv->apCraft[i];
        if (pShip == NULL) {
            continue;
        }
        if (pShip->bHomeBase < 0xFE && pShip->bIntercepting != 0 &&
            pShip->nStrategIdx == nStrategIdx) { /* 0x5A614 */
            (void)Ship_RetargetTravel(pEnv, i);
            nShipCount = (i32)*pEnv->pnShipCount; /* 0x5A62C */
        }
    }

    return nShipCount;
}

/* Ship_ClearWaypointIntercept @ 0x5a830. Ship_RetargetTravel with the travel step dropped and the
 * timer cleared to 0xFFFF instead of 0. The craft keeps wherever it was heading; only the
 * intercept is cancelled, and its proximity timer is parked so it cannot fire.
 *
 * Returns the craft's strateg entry, which is what the original hands back. */
StrategItem *Ship_ClearWaypointIntercept(const ShipRemovalEnv *pEnv, i32 nShipIdx)
{
    if (pEnv == NULL || pEnv->apCraft == NULL || nShipIdx < 0 ||
        nShipIdx >= (i32)*pEnv->pnShipCount) {
        return NULL;
    }

    Ship *pShip = pEnv->apCraft[nShipIdx];
    if (pShip == NULL) {
        return NULL;
    }

    if (pShip->bIntercepting != 0 && pEnv->pStrategList != NULL) {
        const StrategItem *pWaypoint =
            (const StrategItem *)PtrArray_Get(pEnv->pStrategList, pShip->nStrategIdx);
        if (pWaypoint != NULL && pWaypoint->nType == 5) { /* 0x5A874 */
            StrategList_RemoveAt(pEnv->pStrategList, pEnv->pWorld, pShip->nStrategIdx,
                                 pEnv->pnSpawnOnceFlag, pEnv->pSpawnCtx);
            pShip->bIntercepting = 0; /* 0x5A88C */
        }
    }

    const StrategList view = StrategViewOf(pEnv->pStrategList);
    const i32 nEntryIdx = StrategList_FindByShipId(&view, pEnv->pWorld, nShipIdx);
    StrategItem *pEntry = (StrategItem *)PtrArray_Get(pEnv->pStrategList, nEntryIdx);

    if (pEntry != NULL) {
        pEntry->nTimer = 0xFFFF; /* 0x5A8B0: parked, not restarted */
    }

    pShip->bIntercepting = 0; /* 0x5A8B8 */
    return pEntry;
}

i32 Base_RemoveAllShips(const BaseClearShipsEnv *pEnv, i32 nBaseIndex)
{
    if (pEnv == NULL) {
        return 0;
    }

    const ShipRemovalEnv *pShip = &pEnv->ship;
    const i32 nListCount = (pShip->pnAssignedShipCount != NULL) ? *pShip->pnAssignedShipCount : 0;

    for (i32 i = 0; i < nListCount; ++i) {
        if (pShip->anAssignedShip == NULL) {
            break;
        }
        const i32 nCraftIdx = pShip->anAssignedShip[i];
        const i32 nCraftCount = (pShip->pnShipCount != NULL) ? (i32)*pShip->pnShipCount : 0;
        if (nCraftIdx < 0 || nCraftIdx >= nCraftCount || pShip->apCraft == NULL ||
            pShip->apCraft[nCraftIdx] == NULL) {
            continue; /* deviation: the original dereferences apCraft[entry] with no check */
        }
        (void)Ship_RemoveWithCargo(pShip, (i32)pShip->apCraft[nCraftIdx]->bId); /* 0x668B8 */
    }

    return Base_CountShipsAtBase(pShip->apCraft,
                                 (pShip->pnShipCount != NULL) ? (i32)*pShip->pnShipCount : 0,
                                 pShip->apBases, pEnv->nBaseCount,
                                 pEnv->abGridBytes, nBaseIndex); /* 0x668DC tail call */
}

/* ---- Base_TransferTacticalLoot @ 0x68344 ---------------------------------------- */

/* The three facility grids, laid out as documented in pufo_geoscape.h:
 *     buildDays : abGrid + 36 * nBase + 6 * row + col
 *     layer2    : abGrid + 444 + 144 * nBase + 6 * row + col
 *     facType   : abGrid + 480 + 144 * nBase + 6 * row + col
 * The scan proves them directly. 0x683F8 scales the row by 24 before the multiply by 6, which is
 * 144 per base, and adds 80, so 480 + 6*row + col. 0x6843C adds 0x1C0|3 = 451 = 444 + 6 + 1, that
 * is layer2 one row and one column further on. 0x68488 multiplies the base by 6 twice, which is
 * the 36-byte buildDays block. */
static u8 *FacTypeCell(u8 *abGrid, i32 nBase, i32 nRow, i32 nCol)
{
    return abGrid + 480 + 144 * nBase + 6 * nRow + nCol;
}

static u8 *Layer2Cell(u8 *abGrid, i32 nBase, i32 nRow, i32 nCol)
{
    return abGrid + 444 + 144 * nBase + 6 * nRow + nCol;
}

static u8 *BuildDaysCell(u8 *abGrid, i32 nBase, i32 nRow, i32 nCol)
{
    return abGrid + 36 * nBase + 6 * nRow + nCol;
}

/* One weapon mount, 0x68A18 for the left and 0x6C70 for the right. The original writes the two
 * copies out in full; they differ only in which byte they read and which byte they write, so one
 * helper covers both. nMounts is 2 when both mounts carry the same weapon and 1 when they differ,
 * which is how the original halves the demand for a single mount.
 *
 * The carried ammo is returned to the base first, then drawn back. Weapon 29 is the odd one: it is
 * returned in tens and drawn back in hundreds. Returns true when the base could not cover the
 * demand, in which case the craft takes whatever the base had and the base column is zeroed. */
static bool RefuelOneMount(Ship *pShip, BaseRec *pBase, ObjectRecFull *const *apObjects,
                           i32 nObjectCount, i8 nWeapon, i32 nMounts, i32 nBaseCol,
                           i32 *pnReportObj)
{
    i32 nObjId;
    i32 nDemand;
    u8 *pbAmmo;

    switch (nWeapon) {
    case 25: nObjId = 52;  nDemand = 10 * nMounts; pbAmmo = &pShip->bAmmo25; break;
    case 26: nObjId = 53;  nDemand = 3 * nMounts;  pbAmmo = &pShip->bAmmo26; break;
    case 27: nObjId = 54;  nDemand = 2 * nMounts;  pbAmmo = &pShip->bAmmo27; break;
    case 29: nObjId = 105; nDemand = 10 * nMounts; pbAmmo = &pShip->bAmmo29; break;
    case 30: nObjId = 55;  nDemand = 2 * nMounts;  pbAmmo = &pShip->bAmmo30; break;
    default: return false; /* 0x68A18: any other weapon is left exactly as it was */
    }

    if (nObjId >= nObjectCount || apObjects[nObjId] == NULL) {
        return false;
    }
    ObjectRecFull *pObj = apObjects[nObjId];

    /* Return what the craft carries. Weapon 29 is carried in tens and the base counts it in units.
     * 0x68AC8 for 29, 0x6A48 / 0x6B44 / 0x6B9C / 0x6C0C for the rest. */
    BaseRec_AddItemQty(pBase, pObj, (nObjId == 105) ? ((i32)*pbAmmo / 10) : (i32)*pbAmmo);
    *pbAmmo = 0;

    i32 nStock = (i32)pObj->auPerBaseVal[nBaseCol];
    if (nStock >= nDemand) {
        /* Enough at the base: the craft takes the demand, and weapon 29 takes it in hundreds. */
        *pbAmmo = (u8)((nObjId == 105) ? (100 * nMounts) : nDemand); /* 0x6B2C */
        BaseRec_SubItemQty(pBase, pObj, nDemand);                    /* 0x6C5C */
        return false;
    }

    /* Not enough: the craft takes whatever the base had, and the base column is cleared. */
    *pbAmmo = (u8)((nObjId == 105) ? (10 * nStock) : nStock); /* 0x6B00 */
    pObj->auPerBaseVal[nBaseCol] = 0;                         /* 0x6B10 */
    *pnReportObj = nObjId;
    return true;
}

void Base_TransferTacticalLoot(const TacticalLootEnv *pEnv, bool bAnnounceAmmoShortage)
{
    if (pEnv == NULL || pEnv->apCraft == NULL || pEnv->apBases == NULL ||
        pEnv->apObjects == NULL || pEnv->abFacilityGrid == NULL) {
        return;
    }

    for (i32 nBase = 0; nBase < pEnv->nBaseCount; ++nBase) {
        if (pEnv->apBases[nBase] == NULL) {
            continue;
        }
        BaseRec *pBase = pEnv->apBases[nBase];

        for (i32 nShip = 0; nShip < pEnv->nShipCount; ++nShip) {
            if (pEnv->apCraft[nShip] == NULL) {
                continue;
            }
            Ship *pShip = pEnv->apCraft[nShip];
            i32 nHome = (i32)pShip->bHomeBase;

            /* 0x683DC. The gate clears the hot flag as it tests it. */
            if (nHome != nBase || pShip->bAtBase == 0 || pShip->bWeaponsHot == 0) {
                continue;
            }
            pShip->bWeaponsHot = 0; /* 0x683E0 */

            /* 1. Park the craft. A hangar cell whose down-right neighbour in layer2 is free and
             * whose own build days are zero takes the craft. The scan does not stop at the first
             * hit unless a marker was actually written, so a craft type the table does not know
             * keeps scanning and can move the hangar coordinates again. */
            bool bParked = false;
            for (i32 nRow = 0; nRow < 5 && !bParked; ++nRow) {
                for (i32 nCol = 0; nCol < 5; ++nCol) {
                    if (*FacTypeCell(pEnv->abFacilityGrid, nHome, nRow, nCol) != 11) {
                        continue;
                    }
                    if (*Layer2Cell(pEnv->abFacilityGrid, nHome, nRow + 1, nCol + 1) != 0) {
                        continue;
                    }
                    if (*BuildDaysCell(pEnv->abFacilityGrid, nHome, nRow, nCol) != 0) {
                        continue;
                    }
                    pShip->bHangarX = (u8)(nCol + 1); /* 0x68468 */
                    pShip->bHangarY = (u8)(nRow + 1); /* 0x68478 */

                    /* ORIGINAL BUG (0x6858C), reproduced: the marker goes into the facility block
                     * numbered by the CRAFT TYPE, not the home base. A type 1 craft landing at base
                     * 3 writes into base 1's grid. */
                    if (pShip->bCraftType <= 4) {
                        *Layer2Cell(pEnv->abFacilityGrid, (i32)pShip->bCraftType, nRow + 1, nCol + 1)
                            = (u8)(33 + pShip->bCraftType); /* 0x684D0..0x68594 */
                        bParked = true;
                        break;
                    }
                }
            }

            /* 2. Credit the crew. 0x685E4. A soldier aboard this craft who has no credit yet takes
             * 40 percent of the mission value and is marked away in a craft. */
            if (pEnv->apSoldiers != NULL && pEnv->pnSoldierCount != NULL) {
                for (i32 k = 0; k < (i32)*pEnv->pnSoldierCount; ++k) {
                    Soldier *pSol = pEnv->apSoldiers[k];
                    if (pSol == NULL) {
                        continue;
                    }
                    if (pSol->bUnk183 == (u8)nShip && pSol->bUnk247 == 0 && pSol->nUnk436 != 0) {
                        pSol->bUnk247 = (u8)(40 * pSol->nUnk436 / 100); /* 0x68610 */
                        pSol->bUnk183 = 254;                           /* 0x6861C */
                    }
                }
            }

            /* 3. Dump and refill every craft's cargo, not just this one. 0x68644. */
            CraftStoreLoadEnv cargo;
            cargo.apCraft = pEnv->apCraft;
            cargo.apBases = pEnv->apBases;
            cargo.apObjects = pEnv->apObjects;
            cargo.nObjectCount = pEnv->nObjectCount;
            Base_StripAllCraftCargo(&cargo, pEnv->nShipCount);

            /* 4. A craft must not carry more of a weapon than the base has ammo for it. 0x68658.
             * Objects 1..13, each checked against the three records its anAmmoTypeId names. */
            bool bStoresShort = false;
            i32 nReportObj = 0;
            for (i32 k = 1; k <= 13 && k < pEnv->nObjectCount; ++k) {
                ObjectRecFull *pWeapon = pEnv->apObjects[k];
                if (pWeapon == NULL) {
                    continue;
                }
                i32 nCarried = (i32)pWeapon->aItemQty[nShip];
                i32 nAmmo = 0;
                for (int j = 0; j < 3; ++j) {
                    i32 nAmmoId = (i32)pWeapon->anAmmoTypeId[j];
                    if (nAmmoId < pEnv->nObjectCount && pEnv->apObjects[nAmmoId] != NULL) {
                        nAmmo += (i32)pEnv->apObjects[nAmmoId]->aItemQty[nShip];
                    }
                }
                if (nCarried > nAmmo) {
                    bStoresShort = true;
                    nReportObj = 255; /* 0x686C0: the report names no object */
                }
            }

            /* 5. The craft gives back what it was carrying. 0x686F0. Slot 34 comes back in tens. */
            static const i32 s_anCargoStore[PUFO_SHIP_CARGO_COLUMNS] = {
                PUFO_CARGO_STORE_0, PUFO_CARGO_STORE_1, PUFO_CARGO_STORE_2, PUFO_CARGO_STORE_3
            };
            static const i32 aWeaponObj[PUFO_SHIP_CARGO_COLUMNS] = { 31, 32, 34, 35 };
            static const i32 aWeight[PUFO_SHIP_CARGO_COLUMNS] = { 30, 8, 5, 8 };

            for (int j = 0; j < PUFO_SHIP_CARGO_COLUMNS; ++j) {
                if (s_anCargoStore[j] >= pEnv->nObjectCount || pEnv->apObjects[s_anCargoStore[j]] == NULL) {
                    continue;
                }
                i32 nGive = pShip->anCargoQty[j];
                if (j == PUFO_CARGO_TENS_COLUMN) {
                    nGive /= 10; /* 0x68730 */
                }
                BaseRec_AddItemQty(pBase, pEnv->apObjects[s_anCargoStore[j]], nGive);
                pShip->anCargoQty[j] = 0;
            }

            /* 6. Top up. Anything the craft carries beyond what the base can hold for that many
             * units is moved from the craft to the base. 0x68784..0x688F4. */
            for (int j = 0; j < PUFO_SHIP_CARGO_COLUMNS; ++j) {
                if (aWeaponObj[j] >= pEnv->nObjectCount || s_anCargoStore[j] >= pEnv->nObjectCount) {
                    continue;
                }
                ObjectRecFull *pWeapon = pEnv->apObjects[aWeaponObj[j]];
                ObjectRecFull *pStore = pEnv->apObjects[s_anCargoStore[j]];
                if (pWeapon == NULL || pStore == NULL) {
                    continue;
                }
                i32 nStock = (i32)pStore->auPerBaseVal[nBase];
                if (aWeight[j] * (i32)pWeapon->aItemQty[nShip] > nStock) {
                    i32 nDelta = (i32)pWeapon->aItemQty[nShip] - nStock / aWeight[j];
                    InvSel_SubItemQty(pShip, pWeapon, (i16)nDelta);
                    BaseRec_AddItemQty(pBase, pWeapon, nDelta);
                    nReportObj = s_anCargoStore[j];
                    bStoresShort = true;
                }
            }

            if (bStoresShort) {
                if (pEnv->pfnAppendLine != NULL) {
                    /* Six arguments: a3 = R2, a4 = R3, a5 = [SP+0], a6 = [SP+4]. 0x68914 loads a3
                     * from the radar-screen scratch byte at 0x15967C (the same block
                     * RadarScreen_Init/Update and Strateg_ConfirmInterceptTarget read for their own
                     * messages), 0x68918 loads a4 with the ship index, 0x68910 stores the reported
                     * object at [SP+0] and R10 -- 0 -- at [SP+4]. The port has no model of the
                     * radar block yet, so a3 is passed as 0, which is what that byte holds in the
                     * shipped image; recorded as a deviation rather than invented. */
                    pEnv->pfnAppendLine(39, 0, nShip, nReportObj, pEnv->pUser); /* 0x68924 */
                }
                if (pEnv->pbUiRefreshFlag != NULL) {
                    *pEnv->pbUiRefreshFlag = 1; /* 0x68930 */
                }
            }

            /* 7. Reload from base stores. The base is charged 30, 8, 5 and 8 per unit; the craft is
             * credited 30, 8, 50 and 8. Slot 34 is asymmetric, and that is the same asymmetry
             * BasePlaneScreen_CommitStores shows on the manual path. 0x68948..0x689E4. */
            static const i32 aCraftCredit[PUFO_SHIP_CARGO_COLUMNS] = { 30, 8, 50, 8 };
            for (int j = 0; j < PUFO_SHIP_CARGO_COLUMNS; ++j) {
                if (aWeaponObj[j] >= pEnv->nObjectCount || s_anCargoStore[j] >= pEnv->nObjectCount) {
                    continue;
                }
                ObjectRecFull *pWeapon = pEnv->apObjects[aWeaponObj[j]];
                ObjectRecFull *pStore = pEnv->apObjects[s_anCargoStore[j]];
                if (pWeapon == NULL || pStore == NULL) {
                    continue;
                }
                i32 nQty = (i32)pWeapon->aItemQty[nShip];
                BaseRec_SubItemQty(pBase, pStore, aWeight[j] * nQty);
                pShip->anCargoQty[j] = aCraftCredit[j] * nQty;
            }

            /* ORIGINAL BUG, reproduced: 0x689E4 sign-extends R8, and R8 was last written at 0x68654
             * as 2 * ship index. The base index this call receives is therefore twice the ship
             * index, not the base being processed. Hex-Rays renders it as (char)nBaseIdx.
             *
             * DEVIATION: the original computes the needed total inside Base_UpdateStoresNeeded from
             * Base_CalcStorageUsed (0x6555c) and Base_CountItemQtyAcrossBases (0x67d68). Only the
             * first is ported, so the pass this call performs is weaker than the original's. */
            Base_UpdateStoresNeeded(pEnv->abFacilityGrid, (i8)(2 * nShip),
                                    Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount,
                                                         (i8)(2 * nShip)));

            /* 8. Re-draw both mounts' ammo. 0x6A00: two mounts of the same weapon share one
             * demand of 2 units, two different weapons each demand 1. */
            i32 nMounts = (pShip->bWeaponL == pShip->bWeaponR) ? 2 : 1;
            bool bAmmoShort = false;

            bAmmoShort = RefuelOneMount(pShip, pBase, pEnv->apObjects, pEnv->nObjectCount,
                                        pShip->bWeaponL, nMounts, nBase, &nReportObj) || bAmmoShort;
            bAmmoShort = RefuelOneMount(pShip, pBase, pEnv->apObjects, pEnv->nObjectCount,
                                        pShip->bWeaponR, nMounts, nBase, &nReportObj) || bAmmoShort;

            if (bAmmoShort) {
                if (pEnv->pbUiRefreshFlag != NULL) {
                    *pEnv->pbUiRefreshFlag = 1; /* 0x68EC0 */
                }
                if (bAnnounceAmmoShortage && pEnv->pfnAppendLine != NULL) {
                    pEnv->pfnAppendLine(40, nBase, nReportObj, 0, pEnv->pUser); /* 0x68EDC */
                }
            }
        }
    }
}

/* Ship_ProductSlotToType @ 0x65a6c. The header has the map and the two callers. The mask is the
 * ARM's own AND R0,R1,#0xFF at 0x65A6C, so a slot id wider than a byte folds back into it: 0x175
 * answers Skyranger. */
i32 Ship_ProductSlotToType(i32 nProductSlot)
{
    switch (nProductSlot & 0xFF) {
        case OBJ_CRAFT_COUNT_SKYRANGER:   return CRAFT_TYPE_SKYRANGER;   /* 0x65A74 -> 0x65ABC */
        case OBJ_CRAFT_COUNT_INTERCEPTOR: return CRAFT_TYPE_INTERCEPTOR; /* 0x65A78 -> 0x65AB4 */
        case OBJ_CRAFT_COUNT_FIRESTORM:   return CRAFT_TYPE_FIRESTORM;   /* 0x65A80 -> 0x65AAC */
        case OBJ_CRAFT_COUNT_LIGHTNING:   return CRAFT_TYPE_LIGHTNING;   /* 0x65A88 -> 0x65AA4 */
        case OBJ_CRAFT_COUNT_AVENGER:     return CRAFT_TYPE_AVENGER;     /* 0x65A90 -> 0x65A9C */
        default:                          return -1;                     /* 0x65A94 MOVNE R0,#-1 */
    }
}

/* Ship_SpawnAtSlot @ 0x2cb14. The header has the scan and its two asymmetries.
 *
 * The grid offsets below are the ARM's own arithmetic, unrolled:
 *   fac block base  0x159768 + 0x174 + 144*nHomeBase   (0x2CBFC: MUL by 0x90, then +0x174)
 *   facType layer   block + 108, and LR advances by 6 per row (0x2CC0C: ADD R9,LR,#0x174)
 *   layer2 test     block + 79 + result, which is layer2[1][result+1] and does NOT follow v10
 *   stamp           block + 6*(v10+1) + (result+1)
 *   buildDays       0x159768 + 36*nHomeBase + 6*v10 + result
 */
i32 Ship_SpawnAtSlot(ShipSpawnEnv *pEnv, u8 nCraftType, i32 nHomeBase)
{
    Ship *pNew = (Ship *)malloc(sizeof(Ship)); /* 0x2CB2C: operator new(0x44) */
    const i32 nSlot = (i8)*pEnv->pnCraftCount; /* 0x2CB6C: the count byte at Map+0x1194 */
    u8 *pFac;
    u8 *pDays;
    BaseRec *pBase;
    i32 v10;
    i32 result;

    if (pNew != NULL) {
        /* Ship_ctor @ 0x45C98 reads the LIVE Map_nCraftCount for the cargo column, and the
         * port's ShipCtorEnv carries that count by value, so the spawn syncs it here. Without
         * this every craft would share column 0. */
        if (pEnv->pCtorEnv != NULL) {
            pEnv->pCtorEnv->nShipCount = nSlot;
        }
        (void)Ship_Ctor(pNew, nCraftType, *pEnv->pnNextCraftId, nHomeBase, 0, pEnv->pCtorEnv);
    }

    /* 0x2CB7C: the store happens before either counter is bumped, so the slot is the count
     * as it stands. A failed allocation still stores NULL -- the original does. */
    pEnv->apCraft[nSlot] = pNew;

    pBase = pEnv->apBases[nHomeBase];
    if (pBase != NULL) {
        (void)StrategList_AddItem(pEnv->pStrategList, pEnv->pnStrategSeed, *pEnv->pnZoomMode,
                                  1, 0, (i8)nCraftType, pBase->nUnk8, pBase->nUnk12,
                                  *pEnv->pnNextCraftId, 0);
    }

    ++*pEnv->pnNextCraftId;                        /* 0x2CBD4 */
    *pEnv->pnCraftCount = (u8)(nSlot + 1);         /* 0x2CBE8 */

    if (nHomeBase < 0 || nHomeBase >= 8) {
        return 5; /* deviation: the original bounds nothing */
    }

    pFac  = pEnv->abFacilityGrid + 0x174 + 144 * nHomeBase;
    pDays = pEnv->abFacilityGrid + 36 * nHomeBase;

    for (v10 = 0; v10 < 5; ++v10) {          /* 0x2CCD8: five rows of a six-row grid */
        for (result = 0; result < 5; ++result) { /* 0x2CC18: five columns of a six-column grid */
            Ship *pShip;
            u8 nGlyph;

            if (pFac[108 + 6 * v10 + result] != PUFO_FAC_TYPE_HANGAR) {
                continue; /* 0x2CC14 */
            }
            if (pFac[79 + result] != 0) {
                continue; /* 0x2CC2C: layer2[1][result+1], pinned to row 1 */
            }
            if (pDays[6 * v10 + result] != 0) {
                continue; /* 0x2CC44 */
            }

            pShip = pEnv->apCraft[nSlot]; /* 0x2CC74: re-read through the array, not the local */
            if (pShip != NULL) {
                pShip->bHangarX = (u8)(result + 1); /* 0x2CC8C, Ship + 0x12 */
                pShip->bHangarY = (u8)(v10 + 1);    /* 0x2CC98, Ship + 0x13 */
            }

            switch (nCraftType) {
                case 0: nGlyph = 33; break; /* 0x2CD9C */
                case 1: nGlyph = 34; break; /* 0x2CD78 */
                case 2: nGlyph = 35; break; /* 0x2CD54 */
                case 3: nGlyph = 36; break; /* 0x2CD30 */
                case 4: nGlyph = 37; break; /* 0x2CD0C */
                default: continue;          /* 0x2CCC0: the switch falls into the inner increment */
            }

            /* 0x2CDA0: the store is [R3,#0x4F] off fac[nBase] + 6*v10 + result, and 79 is
             * 72 + 6*1 + 1, so the glyph lands in layer2 at row v10+1, column result+1. */
            pFac[72 + 6 * (v10 + 1) + (result + 1)] = nGlyph;
            return result;                               /* 0x2CDA4: the column index */
        }
    }

    return result; /* 5: loop residue, and no caller uses it */
}

/* --- Ship_FormatDisplayName @ 0x666cc --- */

/* aTextRecs @ 0xB38D0 record index per CraftType, and the separator the last two put between the
 * label and the number. Types 0..2 are records 137, 138 and 139 -- the same three
 * Soldier_FormatDisplayName (0x6654c) uses -- and types 3 and 4 are 418 and 419. */
static const i32  g_anCraftNameRec[PUFO_CRAFT_TYPE_COUNT] = { 137, 138, 139, 418, 419 };
static const char g_abCraftNameSep[PUFO_CRAFT_TYPE_COUNT] = { 0, 0, 0, '-', '-' };

/* The label copy loops (0x66818, 0x667E4, ...): byte-at-a-time until the terminator, with no bound.
 * The port stops at the caller's buffer end. */
static void Ship_CopyLabel(char *pszDst, size_t cchDst, const char *pszSrc)
{
    size_t i = 0;

    while (i + 1 < cchDst && pszSrc[i] != 0) {
        pszDst[i] = pszSrc[i];
        ++i;
    }
    pszDst[i] = 0;
}

char *Ship_FormatDisplayName(Ship *const *apCraft, i32 nShipIdx, const u8 *pTextRecs,
                             char *pszOut, size_t cchOut)
{
    char szNum[16];
    char szSep[2];
    Ship *pShip;
    u8 nType;

    if (pszOut == NULL || cchOut == 0) {
        return pszOut;
    }
    pszOut[0] = 0;

    if (apCraft == NULL || pTextRecs == NULL || nShipIdx < 0) {
        return pszOut;
    }
    pShip = apCraft[nShipIdx];
    if (pShip == NULL || pShip->bCraftType >= PUFO_CRAFT_TYPE_COUNT) {
        return pszOut; /* the original's default, which then copies an uninitialised local */
    }
    nType = pShip->bCraftType;

    Ship_CopyLabel(pszOut, cchOut,
                   (const char *)(pTextRecs + PUFO_TEXT_REC_STRIDE * g_anCraftNameRec[nType]));
    if (g_abCraftNameSep[nType] != 0) {
        szSep[0] = g_abCraftNameSep[nType];
        szSep[1] = 0;
        (void)pufo_strcat(pszOut, szSep, cchOut);                    /* 0x6677C, asc_AB340 */
    }
    snprintf(szNum, sizeof(szNum), "%d", (i32)pShip->bType);         /* 0x6682C, j_itoa (0x9e9f0) */
    (void)pufo_strcat(pszOut, szNum, cchOut);                        /* 0x66844 */
    return pszOut;
}
