/* pufo_ship.c - see pufo_ship.h for the evidence map. */
#include "game/pufo_ship.h"

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
