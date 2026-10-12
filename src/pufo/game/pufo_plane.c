/* pufo_plane.c - see pufo_plane.h for evidence. */
#include "game/pufo_plane.h"
#include "game/pufo_geoscape.h"   /* BaseRec_AddItemQty */

/* One body for BasePlaneScreen_SelectWeapon25..30; only the constant differs. */
i32 BasePlaneScreen_SelectWeapon(PlaneWeaponPanels *pPanels, i32 nWeaponIndex,
                                 i32 (*pfnApplySwap)(void *pUser), void *pUser)
{
    const u8 bWeapon = (u8)nWeaponIndex;

    if (pPanels != NULL) {
        /* LDRB the gate, STRBNE the selection: a closed panel keeps its old value. */
        if (pPanels->bPanelAOpen != 0) {
            pPanels->nPanelASel = bWeapon; /* 0x79B34 */
        }
        if (pPanels->bPanelBOpen != 0) {
            pPanels->nPanelBSel = bWeapon; /* 0x79B4C */
        }
    }

    /* 0x79B54 is `B BasePlaneScreen_ApplyWeaponSwap`, so the swap always runs and its
     * result is the handler's result. */
    return (pfnApplySwap != NULL) ? pfnApplySwap(pUser) : 0;
}

/* BasePlane_UnloadWeaponStores @ 0x790e8. Take min(stock, load) of this weapon's object out
 * of base stores and into the selected craft. The original writes the five weapons out as
 * separate branches with the comparison flipped between them; they are the same rule. */
i32 BasePlane_UnloadWeaponStores(i32 nWeapon, const PlaneWeaponEnv *pEnv)
{
    static const struct {
        i32 nWeapon;
        i32 nObjectIdx;
        i32 nLoad;
        i32 nRound;   /* carried units per base item: 10 for weapon 29, 1 otherwise */
    } aTable[5] = {
        { 25,  52, 10, 1 },
        { 26,  53,  3, 1 },
        { 27,  54,  2, 1 },
        { 29, 105, 10, 10 },
        { 30,  55,  2, 1 },
    };
    u8 *pbAmmo;
    i32 nStock;
    i32 nTake;
    i32 i;

    if (pEnv == NULL || pEnv->apCraft == NULL || pEnv->apObjects == NULL ||
        pEnv->apBases == NULL) {
        return 0;
    }

    for (i = 0; i < 5; ++i) {
        if (aTable[i].nWeapon == nWeapon) {
            break;
        }
    }
    if (i == 5) {
        return 0; /* 0x79118: an unhandled weapon changes nothing */
    }

    Ship *pCraft = pEnv->apCraft[pEnv->nCraftIdx];
    ObjectRecFull *pItem = pEnv->apObjects[aTable[i].nObjectIdx];
    BaseRec *pBase = pEnv->apBases[pEnv->nBaseIdx];

    if (pCraft == NULL || pItem == NULL || pBase == NULL) {
        return 0;
    }

    nStock = pItem->auPerBaseVal[(u8)pBase->bSlot];
    nTake = (nStock < aTable[i].nLoad) ? nStock : aTable[i].nLoad;

    /* The carried byte is one of five adjacent single bytes in Ship, selected by weapon. */
    switch (nWeapon) {
    case 25: pbAmmo = &pCraft->bAmmo25; break;
    case 26: pbAmmo = &pCraft->bAmmo26; break;
    case 27: pbAmmo = &pCraft->bAmmo27; break;
    case 29: pbAmmo = &pCraft->bAmmo29; break;
    default: pbAmmo = &pCraft->bAmmo30; break;
    }
    *pbAmmo = (u8)(*pbAmmo + nTake * aTable[i].nRound); /* 0x79374, 0x79220 and friends */

    /* BaseRec_AddItemQty with a negated delta is exactly what BaseRec_SubItemQty @ 0x5571c
     * does, and the port already has the public one. */
    BaseRec_AddItemQty(pBase, pItem, -nTake); /* 0x793C4 */

    return nTake;
}
