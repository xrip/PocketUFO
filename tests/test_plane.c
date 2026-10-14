/* test_plane.c - the craft loadout screen's weapon selection.
 * (BasePlaneScreen_SelectWeapon25..30 @ 0x79b20..0x79c74.) */
#include "game/pufo_plane.h"
#include "pufo_test.h"

#include <stdio.h>
#include <string.h>

static i32 g_nSwapCalls;
static i32 g_nSwapResult;

static i32 ApplySwap(void *pUser)
{
    (void)pUser;
    ++g_nSwapCalls;
    return g_nSwapResult;
}

static void TestSelectWeapon(void)
{
    PlaneWeaponPanels p;

    printf("craft loadout weapon selection\n");

    /* Both panels open: both selections move together. */
    memset(&p, 0, sizeof(p));
    p.nPanelASel = 1;
    p.nPanelBSel = 2;
    p.bPanelAOpen = 1;
    p.bPanelBOpen = 1;
    g_nSwapCalls = 0;
    g_nSwapResult = 41;

    TestEqI32("the handler returns the swap result",
              BasePlaneScreen_SelectWeapon(&p, 25, ApplySwap, NULL), 41);
    TestEqI32("panel A takes the new weapon", p.nPanelASel, 25);
    TestEqI32("panel B takes it too", p.nPanelBSel, 25);
    TestEqI32("the swap runs once", g_nSwapCalls, 1);

    /* Only panel A open: panel B keeps its old selection. */
    memset(&p, 0, sizeof(p));
    p.nPanelASel = 1;
    p.nPanelBSel = 7;
    p.bPanelAOpen = 1;
    p.bPanelBOpen = 0;
    g_nSwapResult = 0;
    TestEqI32("a closed panel keeps its selection",
              BasePlaneScreen_SelectWeapon(&p, 30, ApplySwap, NULL), 0);
    TestEqI32("the open panel still moves", p.nPanelASel, 30);
    TestEqI32("the closed panel does not", p.nPanelBSel, 7);

    /* Only panel B open. */
    memset(&p, 0, sizeof(p));
    p.nPanelASel = 9;
    p.nPanelBSel = 3;
    p.bPanelAOpen = 0;
    p.bPanelBOpen = 1;
    BasePlaneScreen_SelectWeapon(&p, 28, ApplySwap, NULL);
    TestEqI32("panel A is gated off", p.nPanelASel, 9);
    TestEqI32("panel B moves", p.nPanelBSel, 28);

    /* Neither open: nothing is written, and the swap still runs. */
    memset(&p, 0, sizeof(p));
    p.nPanelASel = 11;
    p.nPanelBSel = 12;
    g_nSwapCalls = 0;
    BasePlaneScreen_SelectWeapon(&p, 26, ApplySwap, NULL);
    TestEqI32("no gate means no write", p.nPanelASel, 11);
    TestEqI32("the swap runs regardless", g_nSwapCalls, 1);

    /* The selection is a byte: 0x79B34 uses STRBNE. */
    memset(&p, 0, sizeof(p));
    p.bPanelAOpen = 1;
    BasePlaneScreen_SelectWeapon(&p, 0x119, ApplySwap, NULL);
    TestEqI32("the index is stored as a byte", p.nPanelASel, 0x19);

    TestEqI32("NULL panels are tolerated",
              BasePlaneScreen_SelectWeapon(NULL, 25, ApplySwap, NULL), 0);
    g_nSwapResult = 0;
    TestEqI32("and a missing swap hook returns zero",
              BasePlaneScreen_SelectWeapon(&p, 25, NULL, NULL), 0);
}

static void TestUnloadWeaponStores(void)
{
    static Ship g_aCraft[1];
    static Ship *g_apCraft[1];
    static BaseRec g_aBase[1];
    static BaseRec *g_apBases[1];
    static ObjectRecFull g_aObj52, g_aObj105;
    static ObjectRecFull *g_apObjects[106];
    PlaneWeaponEnv env;

    printf("BasePlane_UnloadWeaponStores\n");

    memset(g_aCraft, 0, sizeof(g_aCraft));
    memset(g_aBase, 0, sizeof(g_aBase));
    memset(&g_aObj52, 0, sizeof(g_aObj52));
    memset(&g_aObj105, 0, sizeof(g_aObj105));
    memset(g_apObjects, 0, sizeof(g_apObjects));
    g_apObjects[52] = &g_aObj52;
    g_apObjects[105] = &g_aObj105;
    g_apCraft[0] = &g_aCraft[0];
    g_apBases[0] = &g_aBase[0];

    g_aCraft[0].nSlotIdx = 0;
    g_aBase[0].bSlot = 0;
    env.apCraft = g_apCraft;
    env.apObjects = g_apObjects;
    env.apBases = g_apBases;
    env.nCraftIdx = 0;
    env.nBaseIdx = 0;

    /* Weapon 25: object 52, load 10. Stock below the load takes the whole stock. */
    g_aObj52.auPerBaseVal[0] = 4;
    g_aBase[0].nStoresTotal = 4;
    TestEqI32("a stock of 4 against a load of 10 takes 4",
              BasePlane_UnloadWeaponStores(25, &env), 4);
    TestEqI32("the craft carries what was taken", g_aCraft[0].bAmmo25, 4);
    TestEqI32("the base stock drops by the same amount", g_aObj52.auPerBaseVal[0], 0);
    TestEqI32("and the base total follows", g_aBase[0].nStoresTotal, 0);

    /* Stock at or above the load takes exactly one load, no more. */
    g_aObj52.auPerBaseVal[0] = 25;
    g_aBase[0].nStoresTotal = 25;
    TestEqI32("a stock of 25 against a load of 10 takes exactly 10",
              BasePlane_UnloadWeaponStores(25, &env), 10);
    TestEqI32("so the craft gains 10 on top of what it had", g_aCraft[0].bAmmo25, 14);
    TestEqI32("and 15 stays in the base", g_aObj52.auPerBaseVal[0], 15);

    /* Weapon 27 has a load of 2, the smallest. */
    memset(&g_aObj52, 0, sizeof(g_aObj52));
    g_aCraft[0].bAmmo27 = 0;
    g_apObjects[54] = &g_aObj52;
    g_aObj52.auPerBaseVal[0] = 9;
    TestEqI32("weapon 27 is capped by its load of 2",
              BasePlane_UnloadWeaponStores(27, &env), 2);
    TestEqI32("and only 2 lands on the craft", g_aCraft[0].bAmmo27, 2);

    /* Weapon 29 counts rounds of 10 per base item. */
    g_aObj105.auPerBaseVal[0] = 3;
    g_aCraft[0].bAmmo29 = 0;
    TestEqI32("weapon 29 with a stock of 3 takes 3 base items",
              BasePlane_UnloadWeaponStores(29, &env), 3);
    TestEqI32("but the craft counts them as 30 rounds", g_aCraft[0].bAmmo29, 30);
    TestEqI32("while the base loses only 3 items", g_aObj105.auPerBaseVal[0], 0);

    g_aObj105.auPerBaseVal[0] = 40;
    TestEqI32("a stock of 40 is capped at the load of 10",
              BasePlane_UnloadWeaponStores(29, &env), 10);
    TestEqI32("which is 100 rounds on the craft", g_aCraft[0].bAmmo29, 130);

    /* An unhandled weapon does nothing at all. */
    g_aCraft[0].bAmmo25 = 130;
    TestEqI32("weapon 24 is not one of the five", BasePlane_UnloadWeaponStores(24, &env), 0);
    TestEqI32("and no carried byte moves", g_aCraft[0].bAmmo25, 130);

    /* The object's per-craft column is not what this touches; only the base column is. */
    TestEqI32("the craft column of the object stays untouched", g_aObj52.aItemQty[0], 0);
}

static void TestLoadWeaponStores(void)
{
    static Ship g_aCraft[1];
    static Ship *g_apCraft[1];
    static BaseRec g_aBase[1];
    static BaseRec *g_apBases[1];
    static ObjectRecFull g_aObj52, g_aObj105;
    static ObjectRecFull *g_apObjects[106];
    static PlaneWeaponEnv env;
    PlaneWeaponPanels panels;

    printf("BasePlane_LoadWeaponStores\n");

    memset(g_aCraft, 0, sizeof(g_aCraft));
    memset(g_aBase, 0, sizeof(g_aBase));
    memset(&g_aObj52, 0, sizeof(g_aObj52));
    memset(&g_aObj105, 0, sizeof(g_aObj105));
    memset(g_apObjects, 0, sizeof(g_apObjects));
    g_apObjects[52] = &g_aObj52;
    g_apObjects[105] = &g_aObj105;
    g_apCraft[0] = &g_aCraft[0];
    g_apBases[0] = &g_aBase[0];
    memset(&panels, 0, sizeof(panels));
    env.apCraft = g_apCraft;
    env.apObjects = g_apObjects;
    env.apBases = g_apBases;
    env.nCraftIdx = 0;
    env.nBaseIdx = 0;

    /* No gate set: mode 0, so the whole carried amount goes back and the byte is zeroed. */
    g_aCraft[0].bAmmo25 = 7;
    TestEqI32("with no panel gate the whole load returns",
              BasePlane_LoadWeaponStores(25, &env, &panels), 7);
    TestEqI32("and the carried byte is zeroed", g_aCraft[0].bAmmo25, 0);
    TestEqI32("landing in base stores", g_aObj52.auPerBaseVal[0], 7);

    /* Gate set and this weapon is NOT the other hand's weapon: mode 2, keep one load. */
    memset(&g_aObj52, 0, sizeof(g_aObj52));
    panels.bPanelAOpen = 1;
    g_aCraft[0].bWeaponR = 26;   /* not 25, so weapon 25 is being replaced outright */
    g_aCraft[0].bAmmo25 = 25;
    TestEqI32("mode 2 returns only the excess above one load",
              BasePlane_LoadWeaponStores(25, &env, &panels), 15);
    TestEqI32("and the craft keeps exactly one load of 10", g_aCraft[0].bAmmo25, 10);

    /* Carrying less than one load means no excess at all. */
    memset(&g_aObj52, 0, sizeof(g_aObj52));
    g_aCraft[0].bAmmo25 = 4;
    TestEqI32("carrying 4 against a load of 10 returns nothing",
              BasePlane_LoadWeaponStores(25, &env, &panels), 0);
    TestEqI32("and the 4 stays on the craft", g_aCraft[0].bAmmo25, 4);

    /* Gate set but this weapon IS the other hand's weapon: mode 1, so everything returns. */
    memset(&g_aObj52, 0, sizeof(g_aObj52));
    g_aCraft[0].bWeaponR = 25;   /* panel A is changing and 25 is the RIGHT-hand weapon */
    g_aCraft[0].bAmmo25 = 25;
    TestEqI32("a weapon shared with the other hand returns everything",
              BasePlane_LoadWeaponStores(25, &env, &panels), 25);
    TestEqI32("and its byte is zeroed", g_aCraft[0].bAmmo25, 0);

    /* The second gate test overwrites the first, so only panel B decides when both are set. */
    memset(&g_aObj52, 0, sizeof(g_aObj52));
    panels.bPanelAOpen = 1;
    panels.bPanelBOpen = 1;
    g_aCraft[0].bWeaponR = 99;   /* panel A would have said mode 2 ... */
    g_aCraft[0].bWeaponL = 25;   /* ... but panel B says this is the left-hand weapon, mode 1 */
    g_aCraft[0].bAmmo25 = 20;
    TestEqI32("with both gates only panel B's test survives",
              BasePlane_LoadWeaponStores(25, &env, &panels), 20);
    TestEqI32("so the byte is zeroed rather than capped", g_aCraft[0].bAmmo25, 0);

    /* Weapon 29 converts rounds back into base items on both paths. */
    memset(&g_aObj105, 0, sizeof(g_aObj105));
    panels.bPanelAOpen = 0;   /* no gate at all, so mode 0 and everything returns */
    panels.bPanelBOpen = 0;
    g_aCraft[0].bAmmo29 = 45;
    TestEqI32("45 rounds of weapon 29 return as 4 base items",
              BasePlane_LoadWeaponStores(29, &env, &panels), 4);
    TestEqI32("the odd round is dropped, not carried over", g_aCraft[0].bAmmo29, 0);
    TestEqI32("and 4 items land in the base", g_aObj105.auPerBaseVal[0], 4);

    memset(&g_aObj105, 0, sizeof(g_aObj105));
    panels.bPanelAOpen = 1;
    g_aCraft[0].bWeaponR = 26;
    g_aCraft[0].bAmmo29 = 130;   /* 100 rounds is one load, so the excess is 30 rounds */
    TestEqI32("mode 2 for weapon 29 keeps 100 rounds and returns 30",
              BasePlane_LoadWeaponStores(29, &env, &panels), 3);
    TestEqI32("leaving one load on the craft", g_aCraft[0].bAmmo29, 100);

    /* An unhandled weapon does nothing. */
    TestEqI32("weapon 24 is not one of the five", BasePlane_LoadWeaponStores(24, &env, &panels), 0);
}

static void TestApplyWeaponSwap(void)
{
    static Ship g_aCraft[1];
    static Ship *g_apCraft[1];
    static BaseRec g_aBase[1];
    static BaseRec *g_apBases[1];
    static ObjectRecFull g_aObj25, g_aObj26, g_aObj52, g_aObj53;
    static ObjectRecFull *g_apObjects[256];
    static PlaneWeaponEnv env;
    PlaneWeaponPanels panels;

    printf("BasePlaneScreen_ApplyWeaponSwap\n");

    memset(g_aCraft, 0, sizeof(g_aCraft));
    memset(g_aBase, 0, sizeof(g_aBase));
    memset(&g_aObj25, 0, sizeof(g_aObj25));
    memset(&g_aObj26, 0, sizeof(g_aObj26));
    memset(&g_aObj52, 0, sizeof(g_aObj52));
    memset(&g_aObj53, 0, sizeof(g_aObj53));
    memset(g_apObjects, 0, sizeof(g_apObjects));
    /* A weapon and its ammo are SEPARATE object records. The swap moves apObjects[weapon]
     * through cargo, while the two store functions move apObjects[52..55] and [105]. */
    g_apObjects[25] = &g_aObj25;
    g_apObjects[26] = &g_aObj26;
    g_apObjects[52] = &g_aObj52;  /* weapon 25's ammo */
    g_apObjects[53] = &g_aObj53;  /* weapon 26's ammo */
    g_apCraft[0] = &g_aCraft[0];
    g_apBases[0] = &g_aBase[0];
    env.apCraft = g_apCraft;
    env.apObjects = g_apObjects;
    env.apBases = g_apBases;
    env.nCraftIdx = 0;
    env.nBaseIdx = 0;

    /* Swapping 25 out for 26 on the left, with 25 already aboard. */
    g_aCraft[0].nSlotIdx = 0;
    g_aBase[0].bSlot = 0;
    g_aCraft[0].bWeaponL = 25;
    g_aCraft[0].bWeaponR = 0;
    g_aCraft[0].bAmmo25 = 12;   /* one load is 10, so 2 goes back to the base */
    g_aObj25.aItemQty[0] = 1;
    g_aCraft[0].uUnk4 = 1;
    g_aObj25.auPerBaseVal[0] = 5;
    g_aObj26.auPerBaseVal[0] = 3;
    g_aObj52.auPerBaseVal[0] = 0;   /* weapon 25's ammo in base stores */
    g_aObj53.auPerBaseVal[0] = 5;   /* weapon 26's ammo, above its load of 3 */
    g_aBase[0].nStoresTotal = 13;

    memset(&panels, 0, sizeof(panels));
    panels.nPanelASel = 26;
    panels.bPanelAOpen = 1;

    TestEqI32("the swap always returns 0", BasePlaneScreen_ApplyWeaponSwap(&panels, &env), 0);
    TestEqI32("the left hand now carries the new weapon", g_aCraft[0].bWeaponL, 26);
    TestEqI32("the right hand is untouched", g_aCraft[0].bWeaponR, 0);

    /* The old weapon's ammo: mode 2 keeps one load of 10 and returns the other 2. */
    TestEqI32("the old weapon keeps one load on the craft", g_aCraft[0].bAmmo25, 10);
    TestEqI32("and its 2 surplus go to the ammo record in base stores", g_aObj52.auPerBaseVal[0], 2);

    /* The new weapon's ammo is loaded from its own record, capped at its load of 3. */
    TestEqI32("the new weapon loads 3 of its 5 available", g_aCraft[0].bAmmo26, 3);
    TestEqI32("leaving 2 in base stores", g_aObj53.auPerBaseVal[0], 2);

    /* The weapon records themselves move one each, and the base total follows all four moves. */
    TestEqI32("the old weapon record returns to base stores", g_aObj25.auPerBaseVal[0], 6);
    TestEqI32("and the new one leaves them", g_aObj26.auPerBaseVal[0], 2);
    TestEqI32("the base total nets to 12", g_aBase[0].nStoresTotal, 12);

    /* Cargo: this is where the order is load-bearing. The weapon byte is assigned LAST, so the
     * removal at 0x79838 still reads the OLD index. Assign it earlier and the wrong record is
     * taken out of the craft. */
    TestEqI32("the new weapon is in the craft's cargo column", g_aObj26.aItemQty[0], 1);
    TestEqI32("the OLD weapon is the one removed from it", g_aObj25.aItemQty[0], 0);
    TestEqI32("and the cargo total is still 1", g_aCraft[0].uUnk4, 1);

    TestEqI32("both panel gates are cleared afterwards", panels.bPanelAOpen, 0);
    TestEqI32("including the one that was never set", panels.bPanelBOpen, 0);

    /* With no weapon aboard the base-store call for the new weapon moves to the END, after the
     * load instead of before it. Same starting stock, and the ammo load is unaffected because
     * the weapon and its ammo are different records. */
    memset(g_aCraft, 0, sizeof(g_aCraft));
    memset(&g_aObj26, 0, sizeof(g_aObj26));
    memset(&g_aObj53, 0, sizeof(g_aObj53));
    g_aCraft[0].nSlotIdx = 0;
    g_aObj26.auPerBaseVal[0] = 3;
    g_aObj53.auPerBaseVal[0] = 5;
    memset(&panels, 0, sizeof(panels));
    panels.nPanelASel = 26;
    panels.bPanelAOpen = 1;

    BasePlaneScreen_ApplyWeaponSwap(&panels, &env);
    TestEqI32("an empty craft still loads the full 3", g_aCraft[0].bAmmo26, 3);
    TestEqI32("and the weapon record leaves base stores only at the end", g_aObj26.auPerBaseVal[0], 2);
    TestEqI32("with no old weapon, nothing is removed from cargo", g_aObj25.aItemQty[0], 0);
    TestEqI32("so the craft gains one cargo entry", g_aCraft[0].uUnk4, 1);

    /* Both gates set: each side moves independently. */
    memset(g_aCraft, 0, sizeof(g_aCraft));
    memset(&g_aObj25, 0, sizeof(g_aObj25));
    memset(&g_aObj26, 0, sizeof(g_aObj26));
    memset(&g_aObj52, 0, sizeof(g_aObj52));
    memset(&g_aObj53, 0, sizeof(g_aObj53));
    g_aCraft[0].nSlotIdx = 0;
    g_aObj25.auPerBaseVal[0] = 4;
    g_aObj26.auPerBaseVal[0] = 4;
    memset(&panels, 0, sizeof(panels));
    panels.nPanelASel = 25;
    panels.nPanelBSel = 26;
    panels.bPanelAOpen = 1;
    panels.bPanelBOpen = 1;

    BasePlaneScreen_ApplyWeaponSwap(&panels, &env);
    TestEqI32("panel A sets the left hand", g_aCraft[0].bWeaponL, 25);
    TestEqI32("panel B sets the right hand", g_aCraft[0].bWeaponR, 26);
    TestEqI32("two weapons means two cargo entries", g_aCraft[0].uUnk4, 2);

    /* No gate set: nothing moves at all. */
    memset(&panels, 0, sizeof(panels));
    panels.nPanelASel = 30;
    BasePlaneScreen_ApplyWeaponSwap(&panels, &env);
    TestEqI32("a selection with no gate changes no weapon byte", g_aCraft[0].bWeaponL, 25);
}

static void TestAddInterceptMission(void)
{
    static MissionSiteRecord g_aSites[3];
    PtrArray list;
    i32 seed = 0;
    i32 zoom = 0;
    i32 missionId = 100;
    i32 lastIndex = -1;
    InterceptMissionCtx ctx;
    StrategItem *pItem;

    printf("BasePlaneScreen_AddInterceptMission\n");

    memset(g_aSites, 0, sizeof(g_aSites));
    g_aSites[0].x = 120;  g_aSites[0].y = -40;
    g_aSites[1].x = 300;  g_aSites[1].y = 250;
    g_aSites[2].x = -1;   g_aSites[2].y = 799;

    PtrArray_ctor(&list);
    ctx.pList = &list;
    ctx.pnZoomMode = &zoom;
    ctx.pnSeed = &seed;
    ctx.apSites = g_aSites;
    ctx.nSiteCount = 3;
    ctx.pnMissionId = &missionId;
    ctx.pnLastIndex = &lastIndex;

    TestEqI32("the return value is the id counter AFTER the increment", BasePlaneScreen_AddInterceptMission(1, &ctx), 101);
    TestEqI32("the list grew by one", list.nCount, 1);

    pItem = (StrategItem *)PtrArray_Get(&list, 0);
    TestCheck("the new item is in the list", pItem != NULL);
    if (pItem != NULL) {
        TestEqI32("nType is 3, a mission site", pItem->nType, 3);
        TestEqI32("bActive is 1", pItem->bActive, 1);
        TestEqI32("bVisible is 1", pItem->bVisible, 1);
        TestEqI32("bCraftClass is the argument", pItem->bCraftClass, 1);
        TestEqI32("x comes from the record", pItem->nX, 300);
        TestEqI32("y is read as SIGNED", pItem->nY, 250);
        /* nType 3 puts the id in the site id and refuses the display id. */
        TestEqI32("the id the item got is the counter BEFORE the increment", pItem->nSiteId, 100);
        TestEqI32("and a type 3 item has no display id", pItem->nDisplayId, -1);
        TestEqI32("the new item's timer is set to 2880", pItem->nTimer, 2880);
    }

    TestEqI32("the last-index global holds the new item's index", lastIndex, 0);
    TestEqI32("and the id counter moved on for the next mission", missionId, 101);

    /* A negative y in the table stays negative; the loads are LDRSH. */
    BasePlaneScreen_AddInterceptMission(0, &ctx);
    pItem = (StrategItem *)PtrArray_Get(&list, 1);
    if (pItem != NULL) {
        TestEqI32("a negative y is not widened to a positive", pItem->nY, -40);
        TestEqI32("and its x is 120", pItem->nX, 120);
    }
    TestEqI32("the second item's index is 1", lastIndex, 1);

    /* nType 3 is not scaled by zoom, unlike nType 5. */
    zoom = 1;
    BasePlaneScreen_AddInterceptMission(2, &ctx);
    pItem = (StrategItem *)PtrArray_Get(&list, 2);
    if (pItem != NULL) {
        TestEqI32("a type 3 item is not rescaled at zoom 1", pItem->nX, -1);
        TestEqI32("even though its y is above the level-0 width", pItem->nY, 799);
    }

    /* Out of range: the port refuses, and refusing must not disturb the id counter. */
    TestEqI32("a craft class past the table adds nothing", BasePlaneScreen_AddInterceptMission(9, &ctx), 103);
    TestEqI32("so the list stays where it was", list.nCount, 3);
    TestEqI32("and the id counter did not move", missionId, 103);

    PtrArray_DeleteAll(&list);
}

/* ------------------------------------------------- the loadout screen's stat panels
 *
 * Evidence: BasePlaneScreen_DrawStatLabelA @ 0x79e00 and BasePlaneScreen_DrawStatLabelB @ 0x79fc4. */

static SoftSurf g_planeSurf;
static u8 g_aPlaneText[PUFO_TEXT_REC_STRIDE * 144];
static GfxColorEnv g_planeColor;

static void PlanePutRec(i32 nRec, const char *psz)
{
    char *p = (char *)(g_aPlaneText + PUFO_TEXT_REC_STRIDE * nRec);
    i32 i = 0;

    while (psz[i] != 0) {
        p[i] = psz[i];
        ++i;
    }
    p[i] = 0;
}

static const char *PlaneRec(i32 nRec)
{
    return (const char *)(g_aPlaneText + PUFO_TEXT_REC_STRIDE * nRec);
}

static void TestStatPanelLabels(void)
{
    PlaneWeaponPanels p;

    printf("BasePlaneScreen_DrawStatLabelA/B @ 0x79e00 / 0x79fc4\n");

    /* The frame draws through SoftSurf_BlitRectProxy, so the blit target has to exist. */
    g_blitState.nPixelMode = 2;
    SoftSurf_ctor(&g_planeSurf);
    SoftSurf_AllocPixels(&g_planeSurf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_planeSurf;

    memset(g_aPlaneText, 0, sizeof(g_aPlaneText));
    PlanePutRec(141, "AAAAAAAAAA"); /* ten characters, drawn whole */
    PlanePutRec(142, "BBBBBBBBBB"); /* ten, cut to seven at 0x79F34 */
    PlanePutRec(143, "CCCCCCCC");   /* eight, cut to five at 0x79F78 */

    g_planeColor.nScaleSource = 3;
    g_planeColor.nFadeCount = 0;

    memset(&p, 0, sizeof(p));
    BasePlaneScreen_DrawStatLabel(&p, 0, g_aPlaneText, &g_planeColor, 3);

    TestEqI32("panel A's gate opens (0x79E28)", p.bPanelAOpen, 1);
    TestEqI32("  panel B's does not", p.bPanelBOpen, 0);
    TestEqI32("record 141 is drawn whole", (i32)strlen(PlaneRec(141)), 10);
    TestEqI32("record 142 is cut to 7 characters (0x79F34)", (i32)strlen(PlaneRec(142)), 7);
    TestEqI32("record 143 is cut to 5 characters (0x79F78)", (i32)strlen(PlaneRec(143)), 5);

    /* Three nested fills, so the colour a pixel carries says which rect reached it last. The outer
     * one is 5,75 230 wide and 20 * (3 + 1) = 80 tall; the two inner ones start at 6,76 and 7,77,
     * so the single-pixel band at the outer edge keeps index 0x44's colour. */
    const u16 *pPix = (const u16 *)g_planeSurf.pPixels;
    TestEqI32("the outer rect fills 5,75 230x80", pPix[75 * 240 + 5], ((0x44 >> 3) << 11) + 32 * (0xF4 >> 2) + (0xFC >> 3));
    TestEqI32("  and the inner rects repaint 6,76 with index 0x14", pPix[76 * 240 + 6], ((0x14 >> 3) << 11) + 32 * (0x50 >> 2) + (0x63 >> 3));
    TestEqI32("  down to 232,152 inclusive", pPix[152 * 240 + 232], ((0x14 >> 3) << 11) + 32 * (0x50 >> 2) + (0x63 >> 3));
    TestEqI32("  the outer rect's own corner 234,154 keeps the outer colour", pPix[154 * 240 + 234], ((0x44 >> 3) << 11) + 32 * (0xF4 >> 2) + (0xFC >> 3));
    TestEqI32("  the field outside the panel is untouched", pPix[80 * 240 + 239], 0);

    /* The truncation is idempotent, so a second visit shortens nothing further. */
    BasePlaneScreen_DrawStatLabel(&p, 1, g_aPlaneText, &g_planeColor, 3);
    TestEqI32("panel B's gate opens on the second call (0x79FEC)", p.bPanelBOpen, 1);
    TestEqI32("  panel A's stays open too", p.bPanelAOpen, 1);
    TestEqI32("  record 142 is still seven characters", (i32)strlen(PlaneRec(142)), 7);
    TestEqI32("  record 143 is still five", (i32)strlen(PlaneRec(143)), 5);

    BasePlaneScreen_DrawStatLabel(NULL, 0, g_aPlaneText, &g_planeColor, 3);
    BasePlaneScreen_DrawStatLabel(&p, 0, NULL, &g_planeColor, 3);
    TestCheck("a null panel set or a null text table draws nothing", true);
}

/* ------------------------------------------------- putting carried weapon ammo back
 *
 * Evidence: BasePlane_RearmFromStores @ 0x78ed0. */

static Ship g_aRearmCraft[1];
static Ship *g_apRearmCraft[1];
static BaseRec g_aRearmBase[1];
static BaseRec *g_apRearmBases[1];
static ObjectRecFull g_aRearmObj[106];
static ObjectRecFull *g_apRearmObjects[106];

static void TestRearmFromStores(void)
{
    PlaneWeaponPanels p;
    PlaneWeaponEnv env;

    printf("BasePlane_RearmFromStores @ 0x78ed0\n");

    memset(g_aRearmCraft, 0, sizeof(g_aRearmCraft));
    memset(g_aRearmBase, 0, sizeof(g_aRearmBase));
    memset(g_aRearmObj, 0, sizeof(g_aRearmObj));
    for (i32 i = 0; i < 106; ++i) {
        g_apRearmObjects[i] = &g_aRearmObj[i];
    }
    g_apRearmCraft[0] = &g_aRearmCraft[0];
    g_apRearmBases[0] = &g_aRearmBase[0];

    memset(&env, 0, sizeof(env));
    env.apCraft = g_apRearmCraft;
    env.apObjects = g_apRearmObjects;
    env.apBases = g_apRearmBases;
    env.nCraftIdx = 0;
    env.nBaseIdx = 0;
    env.nObjectCount = 26;

    /* The craft carries weapon 25 in its left hand and nothing in its right. */
    g_aRearmCraft[0].nSlotIdx = 0;
    g_aRearmCraft[0].bWeaponL = 25;
    g_aRearmCraft[0].bWeaponR = 0;
    g_aRearmObj[25].aItemQty[0] = 2;
    g_aRearmObj[25].auPerBaseVal[0] = 7;
    g_aRearmBase[0].bSlot = 0;
    g_aRearmBase[0].nStoresTotal = 7;

    memset(&p, 0, sizeof(p));
    p.bPanelAOpen = 1;
    TestEqI32("the object count read at entry is what the function returns",
              BasePlane_RearmFromStores(&env, &p), 26);
    TestEqI32("the carried weapon went back to the base", g_aRearmObj[25].auPerBaseVal[0], 8);
    TestEqI32("  and left the craft's per-craft column", g_aRearmObj[25].aItemQty[0], 1);
    TestEqI32("  and the base total followed", g_aRearmBase[0].nStoresTotal, 8);
    TestEqI32("the left weapon byte is cleared (0x78FBC)", g_aRearmCraft[0].bWeaponL, 0);
    TestEqI32("the right byte was never touched", g_aRearmCraft[0].bWeaponR, 0);
    TestEqI32("both gates are cleared afterwards (0x790C4)", p.bPanelAOpen, 0);
    TestEqI32("  including the one that was already closed", p.bPanelBOpen, 0);

    /* A weapon the craft does not carry changes nothing, even with the gate open. */
    g_aRearmCraft[0].bWeaponL = 25;
    g_aRearmObj[25].aItemQty[0] = 0;
    memset(&p, 0, sizeof(p));
    p.bPanelAOpen = 1;
    (void)BasePlane_RearmFromStores(&env, &p);
    TestEqI32("an uncarried weapon leaves the base stock alone", g_aRearmObj[25].auPerBaseVal[0], 8);
    TestEqI32("  and the weapon byte stays put", g_aRearmCraft[0].bWeaponL, 25);

    /* With both gates closed nothing runs at all. */
    g_aRearmObj[25].aItemQty[0] = 2;
    memset(&p, 0, sizeof(p));
    (void)BasePlane_RearmFromStores(&env, &p);
    TestEqI32("closed gates mean no rearm", g_aRearmObj[25].auPerBaseVal[0], 8);
    TestEqI32("  and the craft still carries it", g_aRearmObj[25].aItemQty[0], 2);

    TestEqI32("a null env returns nothing", BasePlane_RearmFromStores(NULL, &p), 0);
}

/* ------------------------------------------------- closing the loadout screen
 *
 * Evidence: BasePlaneScreen_Close @ 0x7c9e8. */

static Ship g_aCloseCraft[1];
static Ship *g_apCloseCraft[1];
static BaseRec g_aCloseBase[1];
static BaseRec *g_apCloseBases[1];
static ObjectRecFull g_aCloseObj[106];
static ObjectRecFull *g_apCloseObjects[106];
static i32 g_anCloseSlot[36];
static i32 g_nCloseQuota;
static i32 g_nCloseUiCalls;

static void PlaneCloseUiHook(void *pUser)
{
    (void)pUser;
    ++g_nCloseUiCalls;
}

static void TestPlaneClose(void)
{
    PlaneCloseEnv env;

    printf("BasePlaneScreen_Close @ 0x7c9e8\n");

    memset(g_aCloseCraft, 0, sizeof(g_aCloseCraft));
    memset(g_aCloseBase, 0, sizeof(g_aCloseBase));
    memset(g_aCloseObj, 0, sizeof(g_aCloseObj));
    for (i32 i = 0; i < 106; ++i) {
        g_apCloseObjects[i] = &g_aCloseObj[i];
    }
    memset(g_anCloseSlot, 0, sizeof(g_anCloseSlot));
    g_apCloseCraft[0] = &g_aCloseCraft[0];
    g_apCloseBases[0] = &g_aCloseBase[0];

    g_aCloseBase[0].bSlot = 0;
    g_aCloseCraft[0].nSlotIdx = 0;
    for (i32 c = 0; c < PUFO_SHIP_CARGO_COLUMNS; ++c) {
        g_aCloseCraft[0].anCargoQty[c] = 100;
    }

    /* slot 31: 2 rows, slot 32: 1, slot 34: 3, slot 35: 4. Slot 33 has no arm. */
    g_anCloseSlot[31] = 2;
    g_anCloseSlot[32] = 1;
    g_anCloseSlot[33] = 9;
    g_anCloseSlot[34] = 3;
    g_anCloseSlot[35] = 4;
    g_nCloseQuota = 5;
    g_nCloseUiCalls = 0;

    memset(&env, 0, sizeof(env));
    env.apCraft = g_apCloseCraft;
    env.apObjects = g_apCloseObjects;
    env.apBases = g_apCloseBases;
    env.nCraftIdx = 0;
    env.nBaseIdx = 0;
    env.bStoresCommitted = 1;
    env.anSlotCount = g_anCloseSlot;
    env.pnCrewQuota = &g_nCloseQuota;
    env.pfnCloseUi = PlaneCloseUiHook;

    BasePlaneScreen_Close(&env);

    TestEqI32("slot 31 hands back 30 per row", g_aCloseObj[56].auPerBaseVal[0], 60);
    TestEqI32("  and the craft loses the same", g_aCloseCraft[0].anCargoQty[0], 40);
    TestEqI32("slot 32 hands back 8 per row", g_aCloseObj[57].auPerBaseVal[0], 8);
    TestEqI32("  and the craft loses 8", g_aCloseCraft[0].anCargoQty[1], 92);
    TestEqI32("slot 34 hands back 5 base items per row", g_aCloseObj[105].auPerBaseVal[0], 15);
    TestEqI32("  but the craft counts it in rounds of ten, so it loses 50",
              g_aCloseCraft[0].anCargoQty[2], -50);
    TestEqI32("slot 35 hands back 8 per row", g_aCloseObj[58].auPerBaseVal[0], 32);
    TestEqI32("  and the craft loses 8", g_aCloseCraft[0].anCargoQty[3], 68);
    TestEqI32("slot 33 has no arm and moves nothing", g_aCloseObj[0].auPerBaseVal[0], 0);
    TestEqI32("the base total follows the four credits", g_aCloseBase[0].nStoresTotal, 115);
    TestEqI32("the crew quota counter is cleared (0x7CBAC)", g_nCloseQuota, 0);
    TestEqI32("and the geoscape UI closes (0x7CBB4)", g_nCloseUiCalls, 1);

    /* Nothing committed: the close still runs, the stores do not move. */
    env.bStoresCommitted = 0;
    g_nCloseQuota = 4;
    g_nCloseUiCalls = 0;
    BasePlaneScreen_Close(&env);
    TestEqI32("an uncommitted close moves no stock", g_aCloseObj[56].auPerBaseVal[0], 60);
    TestEqI32("  but still clears the quota", g_nCloseQuota, 0);
    TestEqI32("  and still closes the UI", g_nCloseUiCalls, 1);

    BasePlaneScreen_Close(NULL);
    TestCheck("a null close env is tolerated", true);
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestSelectWeapon();
    TestUnloadWeaponStores();
    TestLoadWeaponStores();
    TestApplyWeaponSwap();
    TestAddInterceptMission();
    TestStatPanelLabels();
    TestRearmFromStores();
    TestPlaneClose();

    return TestSummary("test_plane");
}
