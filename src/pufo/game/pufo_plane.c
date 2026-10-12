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

/* The object table the weapon indices address. The original never bounds a weapon index against
 * it here, so the port bounds it only to keep a negative i8 weapon from reading out of range. */
#define PUFO_PLANE_OBJECT_LIMIT 256

/* One side of BasePlaneScreen_ApplyWeaponSwap @ 0x79748. The original writes the left and right
 * halves out longhand; they are the same code with bWeaponL/bWeaponR and the two selection
 * globals swapped, so the port keeps one helper and calls it twice.
 *
 * The order matters and is not the obvious one. When the craft already carries a weapon:
 *
 *   1. put one of the OLD weapon back into base stores
 *   2. return the OLD weapon's carried ammo to base stores
 *   3. take one of the NEW weapon OUT of base stores
 *   4. add the NEW weapon to the craft's cargo
 *   5. load the NEW weapon's ammo from base stores into the craft
 *   6. remove one of the OLD weapon from the craft's cargo
 *
 * A weapon and its ammo are SEPARATE object records: the swap moves apObjects[weapon] through
 * cargo and base stores, while the two store functions move apObjects[52..55] and [105]. So the
 * take-out running before the load does NOT reduce the ammo the load sees; they touch different
 * records. The order that is load-bearing is the weapon-byte assignment, which comes last: the
 * cargo removal at 0x79838 re-reads the weapon byte, and assigning it earlier would remove the
 * NEW weapon from the craft instead of the old one.
 *
 * When the craft carries nothing, steps 1, 2 and 6 are absent and the base-store call for the
 * new weapon moves to the END, after the load rather than before it. The original's call to
 * BasePlane_LoadWeaponStores in that branch passes weapon index 0, which is not one of the five
 * handled weapons and therefore does nothing; the port omits the call and says so. */
static void PlaneSwapSide(Ship *pCraft, ObjectRecFull *const *apObjects, BaseRec *pBase,
                          i32 nNewWeapon, i8 *pbWeapon, const PlaneWeaponEnv *pEnv,
                          const PlaneWeaponPanels *pPanels)
{
    i32 nOld = (i32)*pbWeapon;
    ObjectRecFull *pNew;
    ObjectRecFull *pOld;

    if (nNewWeapon < 0 || nNewWeapon >= PUFO_PLANE_OBJECT_LIMIT) {
        return;
    }
    pNew = apObjects[nNewWeapon];
    if (pNew == NULL) {
        return;
    }

    if (nOld != 0) {
        if (nOld < 0 || nOld >= PUFO_PLANE_OBJECT_LIMIT || apObjects[nOld] == NULL) {
            return;
        }
        pOld = apObjects[nOld];

        BaseRec_AddItemQty(pBase, pOld, 1);              /* 0x797B0 */
        BasePlane_LoadWeaponStores(nOld, pEnv, pPanels); /* 0x797C4 */
        BaseRec_AddItemQty(pBase, pNew, -1);             /* 0x797F0, BaseRec_SubItemQty */
        InvSel_AddItemQty(pCraft, pNew, 1);              /* 0x79810 */
        BasePlane_UnloadWeaponStores(nNewWeapon, pEnv);  /* 0x79818 */
        InvSel_SubItemQty(pCraft, pOld, 1);              /* 0x79838 */
    } else {
        /* 0x79854 would call LoadWeaponStores(0), which handles nothing. */
        InvSel_AddItemQty(pCraft, pNew, 1);              /* 0x79878 */
        BasePlane_UnloadWeaponStores(nNewWeapon, pEnv);  /* 0x79880 */
        BaseRec_AddItemQty(pBase, pNew, -1);             /* 0x798A8, and this one comes LAST */
    }

    *pbWeapon = (i8)nNewWeapon; /* 0x798BC */
}

/* BasePlaneScreen_ApplyWeaponSwap @ 0x79748. Applies the loadout the two weapon panels selected.
 * Both gate flags are cleared at the end whether or not either side did anything, and the
 * original always returns 0. */
i32 BasePlaneScreen_ApplyWeaponSwap(PlaneWeaponPanels *pPanels, const PlaneWeaponEnv *pEnv)
{
    Ship *pCraft;
    BaseRec *pBase;

    if (pPanels == NULL || pEnv == NULL || pEnv->apCraft == NULL ||
        pEnv->apObjects == NULL || pEnv->apBases == NULL) {
        return 0;
    }

    pCraft = pEnv->apCraft[pEnv->nCraftIdx];
    pBase = pEnv->apBases[pEnv->nBaseIdx];
    if (pCraft == NULL || pBase == NULL) {
        pPanels->bPanelAOpen = 0; /* 0x79A10 */
        pPanels->bPanelBOpen = 0; /* 0x79A18 */
        return 0;
    }

    if (pPanels->bPanelAOpen != 0) {
        PlaneSwapSide(pCraft, pEnv->apObjects, pBase, pPanels->nPanelASel, &pCraft->bWeaponL,
                      pEnv, pPanels);
    }
    if (pPanels->bPanelBOpen != 0) {
        PlaneSwapSide(pCraft, pEnv->apObjects, pBase, pPanels->nPanelBSel, &pCraft->bWeaponR,
                      pEnv, pPanels);
    }

    pPanels->bPanelAOpen = 0; /* 0x79A10 */
    pPanels->bPanelBOpen = 0; /* 0x79A18 */
    return 0;                 /* 0x79A1C */
}

/* BasePlaneScreen_AddInterceptMission @ 0x7de14.
 *
 * IDA says this function takes no arguments. It does. The disassembly opens with
 * `MOV R3, R1, LSL #24` and `MOV R3, R3, ASR #24`, a sign-extend of R1's low byte, and R1 is
 * then used twice: as the index into the mission-site table (`MUL R2, R3, #0x18`, so a 24-byte
 * stride) and again unchanged as the bCraftClass argument.
 *
 * The coordinates are LDRSH loads, so both are signed 16-bit. */
i32 BasePlaneScreen_AddInterceptMission(i32 bCraftClass, const InterceptMissionCtx *pCtx)
{
    StrategItem *pItem;
    i32 nCount;

    if (pCtx == NULL || pCtx->pList == NULL || pCtx->pnZoomMode == NULL ||
        pCtx->pnSeed == NULL || pCtx->apSites == NULL || pCtx->pnMissionId == NULL) {
        return 0;
    }

    /* Deviation: the original indexes with the sign-extended char and never bounds it. */
    if (bCraftClass < 0 || bCraftClass >= pCtx->nSiteCount) {
        return *pCtx->pnMissionId;
    }

    StrategList_AddItem(pCtx->pList, pCtx->pnSeed, *pCtx->pnZoomMode, 3, 1, bCraftClass,
                        pCtx->apSites[bCraftClass].x,  /* 0x7DE48, LDRSH [R0,#0xC8] */
                        pCtx->apSites[bCraftClass].y,  /* 0x7DE3C, LDRSH [R0,#0xCA] */
                        *pCtx->pnMissionId, 1);        /* 0x7DE64 */

    nCount = PtrArray_GetCount(pCtx->pList);           /* 0x7DE6C */
    pItem = (StrategItem *)PtrArray_Get(pCtx->pList, nCount - 1);
    if (pItem != NULL) {
        pItem->nTimer = 2880;                          /* 0x7DE80 */
    }

    if (pCtx->pnLastIndex != NULL) {
        /* The count is read a third time here rather than reused from 0x7DE6C. */
        *pCtx->pnLastIndex = PtrArray_GetCount(pCtx->pList) - 1; /* 0x7DE98 */
    }

    *pCtx->pnMissionId = *pCtx->pnMissionId + 1;       /* 0x7DEAC */
    return *pCtx->pnMissionId;                         /* the incremented value */
}
