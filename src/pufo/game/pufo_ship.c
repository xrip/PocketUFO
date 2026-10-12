/* pufo_ship.c - see pufo_ship.h for the evidence map. */
#include "game/pufo_ship.h"

#include <string.h>

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
