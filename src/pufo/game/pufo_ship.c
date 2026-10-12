/* pufo_ship.c - see pufo_ship.h for the evidence map. */
#include "game/pufo_ship.h"

#include <stdlib.h>
#include <string.h>

#include "game/pufo_strateg.h" /* StrategList_RemoveAt, StrategItem */
#include "game/pufo_unit.h"    /* Soldier */

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
static void BaseRec_SubItemQty(BaseRec *pBase, ObjectRecFull *pItem, i32 nDelta)
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
