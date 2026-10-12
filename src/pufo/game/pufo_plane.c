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

/* The five weapons that move ammo to and from base stores, shared by the unload and load
 * passes. nRound is the carried units per base item: 10 for weapon 29, 1 for the rest. */
static const struct PlaneWeaponStore {
    i32 nWeapon;
    i32 nObjectIdx;
    i32 nLoad;
    i32 nRound;
} g_aWeaponStores[5] = {
    { 25,  52, 10, 1 },
    { 26,  53,  3, 1 },
    { 27,  54,  2, 1 },
    { 29, 105, 10, 10 },
    { 30,  55,  2, 1 },
};

static const struct PlaneWeaponStore *WeaponStoreFind(i32 nWeapon)
{
    i32 i;
    for (i = 0; i < 5; ++i) {
        if (g_aWeaponStores[i].nWeapon == nWeapon) {
            return &g_aWeaponStores[i];
        }
    }
    return NULL;
}

/* The carried byte in Ship, selected by weapon. */
static u8 *WeaponStoreAmmo(Ship *pCraft, i32 nWeapon)
{
    switch (nWeapon) {
    case 25: return &pCraft->bAmmo25;
    case 26: return &pCraft->bAmmo26;
    case 27: return &pCraft->bAmmo27;
    case 29: return &pCraft->bAmmo29;
    default: return &pCraft->bAmmo30;
    }
}

/* BasePlane_UnloadWeaponStores @ 0x790e8. Take min(stock, load) of this weapon's object out
 * of base stores and into the selected craft. The original writes the five weapons out as
 * separate branches with the comparison flipped between them; they are the same rule. */
i32 BasePlane_UnloadWeaponStores(i32 nWeapon, const PlaneWeaponEnv *pEnv)
{
    const struct PlaneWeaponStore *pEntry;
    Ship *pCraft;
    ObjectRecFull *pItem;
    BaseRec *pBase;
    u8 *pbAmmo;
    i32 nStock;
    i32 nTake;

    if (pEnv == NULL || pEnv->apCraft == NULL || pEnv->apObjects == NULL ||
        pEnv->apBases == NULL) {
        return 0;
    }

    pEntry = WeaponStoreFind(nWeapon);
    if (pEntry == NULL) {
        return 0; /* 0x79118: an unhandled weapon changes nothing */
    }

    pCraft = pEnv->apCraft[pEnv->nCraftIdx];
    pItem = pEnv->apObjects[pEntry->nObjectIdx];
    pBase = pEnv->apBases[pEnv->nBaseIdx];

    if (pCraft == NULL || pItem == NULL || pBase == NULL) {
        return 0;
    }

    nStock = pItem->auPerBaseVal[(u8)pBase->bSlot];
    nTake = (nStock < pEntry->nLoad) ? nStock : pEntry->nLoad;

    pbAmmo = WeaponStoreAmmo(pCraft, nWeapon);
    *pbAmmo = (u8)(*pbAmmo + nTake * pEntry->nRound); /* 0x79374, 0x79220 and friends */

    /* BaseRec_AddItemQty with a negated delta is exactly what BaseRec_SubItemQty @ 0x5571c
     * does, and the port already has the public one. */
    BaseRec_AddItemQty(pBase, pItem, -nTake); /* 0x793C4 */

    return nTake;
}

/* BasePlane_LoadWeaponStores @ 0x793e0. The reverse: put carried ammo back into base stores.
 *
 * A mode decides how much comes back, and it is computed from the two panel gates before the
 * weapon is looked at. If panel A's gate is set the mode is 2, unless this weapon is the
 * craft's RIGHT-hand weapon, in which case it drops to 1. If panel B's gate is set the mode is
 * 2, unless this weapon is the craft's LEFT-hand weapon, in which case it drops to 1. The
 * second test overwrites the first, so when both gates are set only the second one matters.
 * With neither gate the mode stays 0.
 *
 * Mode 2 means the weapon is being replaced outright: leave one load on the craft and return
 * only the excess above it. Any other mode returns the entire carried amount and zeroes the
 * byte. Weapon 29 converts between rounds and base items on both paths. */
i32 BasePlane_LoadWeaponStores(i32 nWeapon, const PlaneWeaponEnv *pEnv,
                               const PlaneWeaponPanels *pPanels)
{
    const struct PlaneWeaponStore *pEntry;
    Ship *pCraft;
    ObjectRecFull *pItem;
    BaseRec *pBase;
    u8 *pbAmmo;
    i32 nMode = 0;
    i32 nCarried;
    i32 nAdd;

    if (pEnv == NULL || pPanels == NULL || pEnv->apCraft == NULL ||
        pEnv->apObjects == NULL || pEnv->apBases == NULL) {
        return 0;
    }

    pEntry = WeaponStoreFind(nWeapon);
    if (pEntry == NULL) {
        return 0; /* 0x79490 */
    }

    pCraft = pEnv->apCraft[pEnv->nCraftIdx];
    if (pCraft == NULL) {
        return 0;
    }

    if (pPanels->bPanelAOpen != 0) { /* 0x79410 */
        nMode = ((char)nWeapon == (char)pCraft->bWeaponR) ? 1 : 2;
    }
    if (pPanels->bPanelBOpen != 0) { /* 0x79440: this one overwrites the previous result */
        nMode = ((char)nWeapon == (char)pCraft->bWeaponL) ? 1 : 2;
    }

    pItem = pEnv->apObjects[pEntry->nObjectIdx];
    pBase = pEnv->apBases[pEnv->nBaseIdx];
    if (pItem == NULL || pBase == NULL) {
        return 0;
    }

    pbAmmo = WeaponStoreAmmo(pCraft, nWeapon);
    nCarried = *pbAmmo;

    if (nMode == 2) {
        i32 nKeep = pEntry->nLoad * pEntry->nRound; /* 100 for weapon 29, its load in rounds */
        i32 nExcess = (nCarried > nKeep) ? nCarried - nKeep : 0;
        *pbAmmo = (u8)(nCarried - nExcess); /* 0x796F8: the craft keeps one load */
        nAdd = nExcess / pEntry->nRound;    /* 0x79560: back to base items */
    } else {
        *pbAmmo = 0;                        /* 0x79740 */
        nAdd = nCarried / pEntry->nRound;
    }

    BaseRec_AddItemQty(pBase, pItem, nAdd); /* 0x79700 */
    return nAdd;
}
