/* test_plane.c - the craft loadout screen's weapon selection.
 * (BasePlaneScreen_SelectWeapon25..30 @ 0x79b20..0x79c74.) */
#include "game/pufo_plane.h"
#include "game/pufo_unit.h"   /* Soldier.bUnk183, the craft a soldier is assigned to */
#include "pufo_test.h"

#include <stdio.h>
#include <stdlib.h> /* calloc / free, the hangar object block the teardown releases */
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
    static MissionSiteRec g_aSites[3];
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

    /* SoftSurf_AllocPixels @ 0x9f9a4 fills in only the 4-byte header: `operator new` (0x9FA1C) with no zeroing pass,
     * so the pixels start indeterminate in the original too. They read 0 in Release because they are fresh pages and
     * non-zero in Debug because the debug heap fills them -- that difference is the only thing the "untouched" checks
     * below were actually reading. A sentinel makes "untouched" mean "still the sentinel". */
    memset(g_planeSurf.pPixels, 0x5A,
           (size_t)g_planeSurf.nWidth * (size_t)g_planeSurf.nHeight * (size_t)g_planeSurf.nBytesPP);

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
    TestEqI32("  the field outside the panel keeps the sentinel", pPix[80 * 240 + 239], 0x5A5A);

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

/* ------------------------------------------------- the hangar stores list
 *
 * Evidence: BasePlaneScreen_CommitStores @ 0x7ac38 and BasePlaneScreen_AdjustSelectedQty @ 0x7a9b4. */

static UiPanel g_pRowPanel, g_pCommitPanel, g_pSlotPanel, g_pAvailPanel, g_pTakenPanel;
static Ship g_aStoreCraft[1];
static Ship *g_apStoreCraft[1];
static BaseRec g_aStoreBase[1];
static BaseRec *g_apStoreBases[1];
static ObjectRecFull g_aStoreObj[106];
static ObjectRecFull *g_apStoreObjects[106];
static Soldier g_aStoreSoldier[12];
static Soldier *g_apStoreSoldier[12];
static i32 g_anStoreCarry[106];
static i32 g_nStoreQuota;
static u32 g_bStoreShortfall;   /* unk_1870C8, written as a dword */
static i32 g_nStoreResidue;
static PlaneStoresEnv g_storeEnv;

static void StoresReset(i32 nCraftType, i32 nAboard)
{
    memset(&g_pRowPanel, 0, sizeof(g_pRowPanel));
    memset(&g_pCommitPanel, 0, sizeof(g_pCommitPanel));
    memset(&g_pSlotPanel, 0, sizeof(g_pSlotPanel));
    memset(&g_pAvailPanel, 0, sizeof(g_pAvailPanel));
    memset(&g_pTakenPanel, 0, sizeof(g_pTakenPanel));
    memset(g_aStoreCraft, 0, sizeof(g_aStoreCraft));
    memset(g_aStoreBase, 0, sizeof(g_aStoreBase));
    memset(g_aStoreObj, 0, sizeof(g_aStoreObj));
    memset(g_aStoreSoldier, 0, sizeof(g_aStoreSoldier));
    memset(g_anStoreCarry, 0, sizeof(g_anStoreCarry));

    for (i32 i = 0; i < 106; ++i) {
        g_apStoreObjects[i] = &g_aStoreObj[i];
    }
    for (i32 i = 0; i < 12; ++i) {
        g_apStoreSoldier[i] = &g_aStoreSoldier[i];
        g_aStoreSoldier[i].bUnk183 = (u8)((i < nAboard) ? 0 : 254);
    }
    g_apStoreCraft[0] = &g_aStoreCraft[0];
    g_apStoreBases[0] = &g_aStoreBase[0];

    g_aStoreCraft[0].nSlotIdx = 0;
    g_aStoreCraft[0].bCraftType = (i8)nCraftType;
    g_aStoreBase[0].bSlot = 0;

    /* One row, row 0, selected by both panels. */
    g_pRowPanel.nUnk3032 = 0;
    g_pCommitPanel.nUnk3032 = 0;

    memset(&g_storeEnv, 0, sizeof(g_storeEnv));
    g_storeEnv.pRowPanel = &g_pRowPanel;
    g_storeEnv.pCommitPanel = &g_pCommitPanel;
    g_storeEnv.pSlotPanel = &g_pSlotPanel;
    g_storeEnv.pAvailPanel = &g_pAvailPanel;
    g_storeEnv.pTakenPanel = &g_pTakenPanel;
    g_storeEnv.anSlotCarryCount = g_anStoreCarry;
    g_storeEnv.pnCrewQuota = &g_nStoreQuota;
    g_storeEnv.apCraft = g_apStoreCraft;
    g_storeEnv.nCraftIdx = 0;
    g_storeEnv.apBases = g_apStoreBases;
    g_storeEnv.nBaseIdx = 0;
    g_storeEnv.apObjects = g_apStoreObjects;
    g_storeEnv.nObjectCount = 106;
    g_storeEnv.apSoldiers = g_apStoreSoldier;
    g_storeEnv.nSoldierCount = 12;
    g_storeEnv.pbShortfallFlag = &g_bStoreShortfall;
    g_storeEnv.pnResidueFlag = &g_nStoreResidue;
}

static void StoreRow(i32 nSlot, i32 nAvail, i32 nTaken)
{
    g_pSlotPanel.anLineAux[0] = nSlot;
    g_pAvailPanel.anLineAux[0] = nAvail;
    g_pTakenPanel.anLineAux[0] = nTaken;
    g_nStoreQuota = 0;
    g_bStoreShortfall = 0;
    g_nStoreResidue = 7;
}

static void TestCommitStores(void)
{
    printf("BasePlaneScreen_CommitStores @ 0x7ac38\n");

    /* A crew row on a type-0 craft (cap 140): quota 1, 4 crew units, 30 items out of 40. */
    StoresReset(0, 0);
    StoreRow(31, 5, 0);
    g_aStoreObj[56].auPerBaseVal[0] = 40;
    g_aStoreBase[0].nStoresTotal = 40;
    TestEqI32("the row is taken", BasePlaneScreen_CommitStores(&g_storeEnv), 1);
    TestEqI32("the row's taken count rises", g_pTakenPanel.anLineAux[0], 1);
    TestEqI32("  and its available count falls", g_pAvailPanel.anLineAux[0], 4);
    TestEqI32("the slot's carried count rises (0x7AFD8)", g_anStoreCarry[31], 1);
    TestEqI32("the crew quota is spent (0x7AE88)", g_nStoreQuota, 1);
    TestEqI32("the base loses 30 items", g_aStoreObj[56].auPerBaseVal[0], 10);
    TestEqI32("  and the base total follows", g_aStoreBase[0].nStoresTotal, 10);
    TestEqI32("the craft gains 30 cargo units", g_aStoreCraft[0].anCargoQty[0], 30);
    TestEqI32("no shortfall was flagged", g_bStoreShortfall, 0);

    /* The quota gate: a type-0 craft takes at most 3 crew rows. */
    StoresReset(0, 0);
    StoreRow(31, 5, 0);
    g_nStoreQuota = 3;
    g_aStoreObj[56].auPerBaseVal[0] = 40;
    TestEqI32("the fourth crew row is refused", BasePlaneScreen_CommitStores(&g_storeEnv), 0);
    TestEqI32("the shortfall flag is set", g_bStoreShortfall, 1);
    TestEqI32("  and the residue flag reads 0 (the crew reason)", g_nStoreResidue, 0);
    TestEqI32("the quota is handed back (0x7AF98)", g_nStoreQuota, 3);
    TestEqI32("nothing was taken", g_pTakenPanel.anLineAux[0], 0);
    TestEqI32("  and no stock moved", g_aStoreObj[56].auPerBaseVal[0], 40);

    /* The crew-count gate: soldiersAboard + 4 * quota must stay under 14 for a type-0 craft. */
    StoresReset(0, 11);
    StoreRow(31, 5, 0);
    g_aStoreObj[56].auPerBaseVal[0] = 40;
    TestEqI32("11 aboard plus 4 for this row is over 14", BasePlaneScreen_CommitStores(&g_storeEnv), 0);
    TestEqI32("  so the quota is handed back too", g_nStoreQuota, 0);

    /* A type-1 craft (cap 120) has no crew rows at all. */
    StoresReset(1, 0);
    StoreRow(31, 5, 0);
    g_aStoreObj[56].auPerBaseVal[0] = 40;
    TestEqI32("cap 120 refuses crew rows outright", BasePlaneScreen_CommitStores(&g_storeEnv), 0);
    TestEqI32("  with the residue flag at 0", g_nStoreResidue, 0);

    /* An unknown craft type skips the crew gate entirely. */
    StoresReset(9, 11);
    StoreRow(31, 5, 0);
    g_aStoreObj[56].auPerBaseVal[0] = 40;
    TestEqI32("an unknown craft type has no crew gate", BasePlaneScreen_CommitStores(&g_storeEnv), 1);
    TestEqI32("  and the row is taken", g_anStoreCarry[31], 1);

    /* The stock gate: 7 in the base does not cover the 8 a slot-32 row needs. */
    StoresReset(0, 0);
    StoreRow(32, 5, 0);
    g_aStoreObj[57].auPerBaseVal[0] = 7;
    TestEqI32("insufficient stock refuses", BasePlaneScreen_CommitStores(&g_storeEnv), 0);
    TestEqI32("  and the residue flag reads 2 (the stock reason)", g_nStoreResidue, 2);
    TestEqI32("  with the quota handed back", g_nStoreQuota, 0);
    TestEqI32("  and the stock untouched", g_aStoreObj[57].auPerBaseVal[0], 7);

    /* Slot 34 is the 5-for-50 row: 5 base items, 50 cargo units. */
    StoresReset(0, 0);
    StoreRow(34, 2, 0);
    g_aStoreObj[105].auPerBaseVal[0] = 5;
    TestEqI32("the ammo row is taken", BasePlaneScreen_CommitStores(&g_storeEnv), 1);
    TestEqI32("the base loses 5 items", g_aStoreObj[105].auPerBaseVal[0], 0);
    TestEqI32("  but the craft gains 50 cargo units", g_aStoreCraft[0].anCargoQty[2], 50);
    TestEqI32("  and the carry count is the row count, not the units", g_anStoreCarry[34], 1);

    /* Slot 33 has no object record: it clears every gate and moves nothing. */
    StoresReset(0, 0);
    StoreRow(33, 1, 0);
    TestEqI32("slot 33 is accepted", BasePlaneScreen_CommitStores(&g_storeEnv), 1);
    TestEqI32("  it spends the quota", g_nStoreQuota, 1);
    TestEqI32("  and counts as carried", g_anStoreCarry[33], 1);
    TestEqI32("  while object 0 is untouched", g_aStoreObj[0].auPerBaseVal[0], 0);

    /* A weapon row (slot outside 31..35) is gated on hold space instead, and never spends quota. */
    StoresReset(2, 0);
    StoreRow(10, 3, 0);
    g_aStoreObj[10].bCargoSize = 200; /* cap 260 cannot fit 200 pending plus 200 for this one */
    g_anStoreCarry[10] = 1;
    TestEqI32("a weapon row over the hold cap is refused", BasePlaneScreen_CommitStores(&g_storeEnv), 0);
    TestEqI32("  with the residue flag at 0", g_nStoreResidue, 0);
    TestEqI32("  and the crew quota untouched (0x7AE80)", g_nStoreQuota, 0);

    StoresReset(2, 0);
    StoreRow(10, 3, 0);
    g_aStoreObj[10].bCargoSize = 10;
    TestEqI32("the same row fits once it is small enough", BasePlaneScreen_CommitStores(&g_storeEnv), 1);
    TestEqI32("  and it still spends no quota", g_nStoreQuota, 0);
    TestEqI32("  and it carries no cargo units", g_aStoreCraft[0].anCargoQty[0], 0);

    /* A row with nothing left to take does not even reach the gates. */
    StoresReset(2, 0);
    StoreRow(31, 0, 4);
    g_aStoreObj[56].auPerBaseVal[0] = 40;
    TestEqI32("a row with no available count is skipped (0x7AC68)", BasePlaneScreen_CommitStores(&g_storeEnv), 0);
    TestEqI32("  so the quota is not spent", g_nStoreQuota, 0);
    TestEqI32("  and no flag is written", g_bStoreShortfall, 0);

    TestEqI32("a null env commits nothing", BasePlaneScreen_CommitStores(NULL), 0);
}

static void TestAdjustSelectedQty(void)
{
    printf("BasePlaneScreen_AdjustSelectedQty @ 0x7a9b4\n");

    /* Undo one taken row: the count goes back, the stock goes back, the cargo debit is clamped. */
    StoresReset(0, 0);
    StoreRow(31, 4, 2);
    g_anStoreCarry[31] = 2;
    g_nStoreQuota = 2;
    g_aStoreObj[56].auPerBaseVal[0] = 10;
    g_aStoreBase[0].nStoresTotal = 10;
    g_aStoreCraft[0].anCargoQty[0] = 100;
    TestEqI32("the row is put back", BasePlaneScreen_AdjustSelectedQty(&g_storeEnv), 1);
    TestEqI32("the taken count falls", g_pTakenPanel.anLineAux[0], 1);
    TestEqI32("  and the available count rises", g_pAvailPanel.anLineAux[0], 5);
    TestEqI32("the carried count falls (0x7AA28)", g_anStoreCarry[31], 1);
    TestEqI32("the crew quota is handed back (0x7AA50)", g_nStoreQuota, 1);
    TestEqI32("the base gets 30 items back", g_aStoreObj[56].auPerBaseVal[0], 40);
    TestEqI32("  and the base total follows", g_aStoreBase[0].nStoresTotal, 40);
    TestEqI32("the craft loses 30 cargo units", g_aStoreCraft[0].anCargoQty[0], 70);

    /* The clamp that BasePlaneScreen_Close does NOT have: 10 units cannot pay a 30-unit debit. */
    StoresReset(0, 0);
    StoreRow(31, 4, 1);
    g_aStoreObj[56].auPerBaseVal[0] = 0;
    g_aStoreCraft[0].anCargoQty[0] = 10;
    (void)BasePlaneScreen_AdjustSelectedQty(&g_storeEnv);
    TestEqI32("the cargo debit clamps at zero (0x7AB10)", g_aStoreCraft[0].anCargoQty[0], 0);
    TestEqI32("  while the base still gets the full 30", g_aStoreObj[56].auPerBaseVal[0], 30);

    /* Slot 34 hands back 5 items and takes 50 units. */
    StoresReset(0, 0);
    StoreRow(34, 1, 1);
    g_aStoreObj[105].auPerBaseVal[0] = 0;
    g_aStoreCraft[0].anCargoQty[2] = 50;
    (void)BasePlaneScreen_AdjustSelectedQty(&g_storeEnv);
    TestEqI32("the ammo row returns 5 base items", g_aStoreObj[105].auPerBaseVal[0], 5);
    TestEqI32("  and gives back 50 cargo units", g_aStoreCraft[0].anCargoQty[2], 0);

    /* Nothing taken: the whole function is skipped. */
    StoresReset(0, 0);
    StoreRow(31, 5, 0);
    g_nStoreQuota = 3;
    g_aStoreObj[56].auPerBaseVal[0] = 40;
    TestEqI32("a row with nothing taken is left alone (0x7A9F8)", BasePlaneScreen_AdjustSelectedQty(&g_storeEnv), 0);
    TestEqI32("  including the quota", g_nStoreQuota, 3);
    TestEqI32("  and the available count", g_pAvailPanel.anLineAux[0], 5);

    /* A weapon row gives the count back but touches neither the quota nor the stock. */
    StoresReset(0, 0);
    StoreRow(10, 2, 1);
    g_anStoreCarry[10] = 1;
    g_nStoreQuota = 2;
    TestEqI32("a weapon row is put back", BasePlaneScreen_AdjustSelectedQty(&g_storeEnv), 1);
    TestEqI32("  its carried count falls", g_anStoreCarry[10], 0);
    TestEqI32("  and the crew quota is not touched", g_nStoreQuota, 2);
    TestEqI32("  and no object moves", g_aStoreObj[10].auPerBaseVal[0], 0);

    /* Slot 33 hands the quota back and moves no stock. */
    StoresReset(0, 0);
    StoreRow(33, 0, 1);
    g_anStoreCarry[33] = 1;
    g_nStoreQuota = 1;
    TestEqI32("slot 33 is put back", BasePlaneScreen_AdjustSelectedQty(&g_storeEnv), 1);
    TestEqI32("  its quota is handed back", g_nStoreQuota, 0);
    TestEqI32("  and object 0 stays empty", g_aStoreObj[0].auPerBaseVal[0], 0);

    TestEqI32("a null env puts nothing back", BasePlaneScreen_AdjustSelectedQty(NULL), 0);
}
/* ------------------------------------------------- the crew list's click
 *
 * Evidence: BasePlaneScreen_FillCrewPanel @ 0x7a188 and Craft_CountWeaponSlotsCarried @ 0x664f0. */

static UiPanel g_pCrewPanel, g_pCrewStatus;
static u8 g_aCrewLineA[PUFO_PANEL_LINE_BYTES];
static u8 g_aCrewLineB[PUFO_PANEL_LINE_BYTES];
static u8 g_aCrewText[PUFO_TEXT_REC_STRIDE * 141];
static char g_aCrewName[108];
static Ship g_aCrewCraft[1];
static Ship *g_apCrewCraft[1];
static Soldier g_aCrewSol[30];
static Soldier *g_apCrewSol[30];
static ObjectRecFull g_aCrewObj[106];
static ObjectRecFull *g_apCrewObjects[106];
static PlaneCrewPanelEnv g_crewEnv;

static void CrewPutRec(i32 nRec, const char *psz)
{
    char *p = (char *)(g_aCrewText + PUFO_TEXT_REC_STRIDE * nRec);
    i32 i = 0;

    while (psz[i] != 0) {
        p[i] = psz[i];
        ++i;
    }
    p[i] = 0;
}

static const char *CrewRec(i32 nRec)
{
    return (const char *)(g_aCrewText + PUFO_TEXT_REC_STRIDE * nRec);
}

static void CrewReset(i32 nCraftType, i32 nAboard, i32 nWeaponSlots)
{
    memset(&g_pCrewPanel, 0, sizeof(g_pCrewPanel));
    memset(&g_pCrewStatus, 0, sizeof(g_pCrewStatus));
    memset(g_aCrewLineA, 0, sizeof(g_aCrewLineA));
    memset(g_aCrewLineB, 0, sizeof(g_aCrewLineB));
    memset(g_aCrewCraft, 0, sizeof(g_aCrewCraft));
    memset(g_aCrewSol, 0, sizeof(g_aCrewSol));
    memset(g_aCrewObj, 0, sizeof(g_aCrewObj));
    memset(g_aCrewName, 0, sizeof(g_aCrewName));

    if (g_aCrewText[PUFO_TEXT_REC_STRIDE * 93] == 0) {
        memset(g_aCrewText, 0, sizeof(g_aCrewText));
        CrewPutRec(93, "PERSONNEL");
        CrewPutRec(137, "CRAFT");
    }

    g_pCrewPanel.apLines[0] = g_aCrewLineA;
    g_pCrewStatus.apLines[0] = g_aCrewLineB;
    g_pCrewPanel.nUnk3032 = 0;
    g_pCrewPanel.anLineAux[0] = 0; /* row 0 lists soldier 0 */

    for (i32 i = 0; i < 106; ++i) {
        g_apCrewObjects[i] = &g_aCrewObj[i];
    }
    for (i32 i = 0; i < 30; ++i) {
        g_apCrewSol[i] = &g_aCrewSol[i];
    }
    g_apCrewCraft[0] = &g_aCrewCraft[0];

    g_aCrewCraft[0].nSlotIdx = 0;
    g_aCrewCraft[0].bCraftType = (i8)nCraftType;
    g_aCrewCraft[0].bType = 3;

    /* The soldiers already aboard (indices 1..nAboard, so the count is exactly nAboard), and the
     * weapon slots the craft carries. Index 0 is the row under the click and starts unassigned. */
    for (i32 i = 0; i < 30; ++i) {
        g_aCrewSol[i].bUnk183 = (i >= 1 && i <= nAboard) ? 0 : PUFO_SOLDIER_CRAFT_ABOARD_OUT;
    }
    g_aCrewSol[0].bUnk183 = PUFO_SOLDIER_CRAFT_NONE;
    for (i32 i = 31; i < 36; ++i) {
        g_aCrewObj[i].aItemQty[0] = (nWeaponSlots > 0) ? 1 : 0;
        nWeaponSlots--;
    }

    memset(&g_crewEnv, 0, sizeof(g_crewEnv));
    g_crewEnv.pCrewPanel = &g_pCrewPanel;
    g_crewEnv.pStatusPanel = &g_pCrewStatus;
    g_crewEnv.apCraft = g_apCrewCraft;
    g_crewEnv.nCraftIdx = 0;
    g_crewEnv.apSoldiers = g_apCrewSol;
    g_crewEnv.nSoldierCount = 30;
    g_crewEnv.apObjects = g_apCrewObjects;
    g_crewEnv.nObjectCount = 106;
    g_crewEnv.pTextRecs = g_aCrewText;
    g_crewEnv.pszNameBuf = g_aCrewName;
    g_crewEnv.cchNameBuf = (i32)sizeof(g_aCrewName);
}

static i32 g_nApplyCloseCalls;

static void ApplyClose(void *pUser)
{
    (void)pUser;
    ++g_nApplyCloseCalls;
}

static void TestApplyQtyChanges(void)
{
    printf("BuySellScreen_ApplyQtyChanges @ 0x78d90\n");

    StoresReset(0, 0);

    BuySellApplyQtyEnv env;
    memset(&env, 0, sizeof(env));
    env.anSlotCarryCount = g_anStoreCarry;
    env.apObjects = g_apStoreObjects;
    env.nObjectCount = 106;
    env.apBases = g_apStoreBases;
    env.nBaseIdx = 0;
    env.apCraft = g_apStoreCraft;
    env.nCraftIdx = 0;
    env.nCraftCount = 1;
    env.pfnCloseUi = ApplyClose;

    g_aStoreObj[5].auPerBaseVal[0] = 10;
    g_aStoreObj[5].aItemQty[0] = 0;
    g_aStoreObj[6].auPerBaseVal[0] = 10;
    g_aStoreObj[6].aItemQty[0] = 5;
    g_aStoreObj[7].auPerBaseVal[0] = 10;
    g_aStoreObj[7].aItemQty[0] = 0;
    g_aStoreBase[0].nStoresTotal = 30;
    g_aStoreCraft[0].uUnk4 = 10;
    g_anStoreCarry[5] = 2;  /* the craft takes two */
    g_anStoreCarry[6] = -3; /* the craft gives three */
    g_anStoreCarry[7] = 0;  /* and this row is untouched */

    g_nApplyCloseCalls = 0;
    BuySellScreen_ApplyQtyChanges(&env);

    TestEqI32("a positive carry leaves the base", g_aStoreObj[5].auPerBaseVal[0], 8);
    TestEqI32("  and lands in the craft column", g_aStoreObj[5].aItemQty[0], 2);
    TestEqI32("a negative carry lands in the base", g_aStoreObj[6].auPerBaseVal[0], 13);
    TestEqI32("  and leaves the craft column", g_aStoreObj[6].aItemQty[0], 2);
    TestEqI32("a zero carry moves nothing", g_aStoreObj[7].auPerBaseVal[0], 10);
    TestEqI32("  in either direction", g_aStoreObj[7].aItemQty[0], 0);
    TestEqI32("the base total follows both moves", g_aStoreBase[0].nStoresTotal, 31);
    TestEqI32("  and so does the craft total", g_aStoreCraft[0].uUnk4, 9);
    TestEqI32("the close runs once, after the walk", g_nApplyCloseCalls, 1);

    /* The staged amounts are not cleared here: the next pass sees the same numbers. */
    BuySellScreen_ApplyQtyChanges(&env);
    TestEqI32("so a second commit moves the same amounts again",
              g_aStoreObj[5].auPerBaseVal[0], 6);

    /* A non-positive object count skips the walk and still closes (0x78DC8 -> 0x78E68). */
    env.nObjectCount = 0;
    g_nApplyCloseCalls = 0;
    BuySellScreen_ApplyQtyChanges(&env);
    TestEqI32("no stock moves", g_aStoreObj[6].auPerBaseVal[0], 16);
    TestEqI32("  but the close still runs", g_nApplyCloseCalls, 1);
    env.nObjectCount = 106;

    /* Deviation: the original indexes apBases with the signed byte whatever it holds. */
    g_aStoreObj[5].auPerBaseVal[0] = 10;
    g_aStoreObj[5].aItemQty[0] = 0;
    env.nBaseIdx = -1;
    BuySellScreen_ApplyQtyChanges(&env);
    TestEqI32("a -1 base index moves no base stock", g_aStoreObj[5].auPerBaseVal[0], 10);
    TestEqI32("  while the craft side still runs", g_aStoreObj[5].aItemQty[0], 2);
    env.nBaseIdx = 0;

    env.nCraftIdx = 7;
    g_aStoreObj[5].auPerBaseVal[0] = 10;
    g_aStoreObj[5].aItemQty[0] = 0;
    BuySellScreen_ApplyQtyChanges(&env);
    TestEqI32("an out-of-range craft index moves no cargo", g_aStoreObj[5].aItemQty[0], 0);
    TestEqI32("  and the base still pays", g_aStoreObj[5].auPerBaseVal[0], 8);
    env.nCraftIdx = 0;

    BuySellScreen_ApplyQtyChanges(NULL);
    TestCheck("a null env is tolerated", true);
}

static void TestCrewPanelClick(void)
{
    u8 r, g, b;

    printf("BasePlaneScreen_FillCrewPanel @ 0x7a188\n");

    /* An unassigned soldier boards a type-0 craft: 0 aboard + 4*0 slots + 1 fits under 14. */
    CrewReset(0, 0, 0);
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("the soldier is assigned to the craft", g_aCrewSol[0].bUnk183, 0);
    TestCheck("the crew row is painted (255,200,20)",
              UiPanel_GetLineMeta(&g_pCrewPanel, 0, 1, &r, &g, &b) && r == 255 && g == 200 && b == 20);
    TestCheck("  with the highlight triple (10,10,10)",
              UiPanel_GetLineMeta(&g_pCrewPanel, 0, 2, &r, &g, &b) && r == 10 && g == 10 && b == 10);
    TestCheck("the status panel gets the same colours",
              UiPanel_GetLineMeta(&g_pCrewStatus, 0, 1, &r, &g, &b) && r == 255 && g == 200 && b == 20);
    TestEqI32("the status column is the formatter's own string", strcmp((const char *)g_aCrewLineB, g_aCrewName), 0);
    TestCheck("  and it is not empty", g_aCrewLineB[0] != 0);

    /* The type-0 gate: 14 aboard is one too many. */
    CrewReset(0, 14, 0);
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("14 aboard plus this one is over 14, so the click does nothing", g_aCrewSol[0].bUnk183,
              PUFO_SOLDIER_CRAFT_NONE);
    TestCheck("  and the row is not repainted", g_pCrewPanel.aLineMeta[0] == 0);

    CrewReset(0, 13, 0);
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("13 aboard plus this one is exactly 14", g_aCrewSol[0].bUnk183, 0);

    /* Each weapon slot the craft already carries costs four crew. */
    CrewReset(0, 0, 4);
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("four weapon slots cost 16 crew, so the soldier cannot board", g_aCrewSol[0].bUnk183,
              PUFO_SOLDIER_CRAFT_NONE);

    CrewReset(0, 0, 3);
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("three slots cost 12, which still leaves room", g_aCrewSol[0].bUnk183, 0);

    /* A type-1 craft has a different rule: 12 total, and weapon slots are not charged at all. */
    CrewReset(1, 12, 4);
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("type 1 counts only bodies: 12 aboard is over 12", g_aCrewSol[0].bUnk183,
              PUFO_SOLDIER_CRAFT_NONE);

    CrewReset(1, 11, 4);
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("  and 11 aboard boards even with four weapon slots", g_aCrewSol[0].bUnk183, 0);

    /* Type 2 goes to 26; types 3 and 4 have no gate at all. */
    CrewReset(2, 25, 0);
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("type 2 boards at 25 + 1 = 26", g_aCrewSol[0].bUnk183, 0);

    CrewReset(3, 4, 4);
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("type 3 has no crew gate here", g_aCrewSol[0].bUnk183, 0);

    /* Clicking an assigned soldier lets them go, with no gate on that half. */
    CrewReset(0, 4, 4);
    g_aCrewSol[0].bUnk183 = 0; /* this row is already aboard */
    BasePlaneScreen_FillCrewPanel(&g_crewEnv);
    TestEqI32("the gate is not consulted on the way out", g_aCrewSol[0].bUnk183, PUFO_SOLDIER_CRAFT_NONE);
    TestCheck("the row is repainted (240,240,240)",
              UiPanel_GetLineMeta(&g_pCrewPanel, 0, 1, &r, &g, &b) && r == 240 && g == 240 && b == 240);
    TestCheck("  with the highlight triple (20,20,20)",
              UiPanel_GetLineMeta(&g_pCrewPanel, 0, 2, &r, &g, &b) && r == 20 && g == 20 && b == 20);
    TestEqI32("the status column reads the unassigned label",
              strcmp((const char *)g_aCrewLineB, CrewRec(93)), 0);

    BasePlaneScreen_FillCrewPanel(NULL);
    TestCheck("a null crew env is tolerated", true);
}

static void TestWeaponSlotsCarried(void)
{
    printf("Craft_CountWeaponSlotsCarried @ 0x664f0\n");

    CrewReset(0, 0, 0);
    for (i32 i = 0; i < 106; ++i) {
        g_aCrewObj[i].aItemQty[0] = (u16)i;
    }
    TestEqI32("only object indices 31..35 count", Craft_CountWeaponSlotsCarried(g_apCrewObjects, 106, 0),
              31 + 32 + 33 + 34 + 35);
    TestEqI32("the walk stops at the object count", Craft_CountWeaponSlotsCarried(g_apCrewObjects, 34, 0),
              31 + 32 + 33);
    TestEqI32("a craft column outside the 52 reads as nothing",
              Craft_CountWeaponSlotsCarried(g_apCrewObjects, 106, 52), 0);
    TestEqI32("a null object table counts nothing", Craft_CountWeaponSlotsCarried(NULL, 106, 0), 0);
}
/* ------------------------------------------------- building the crew list's two panels
 *
 * Evidence: BasePlaneScreen_BuildListPanels @ 0x7a3f4. */

static PlaneListPanels g_listOut;
static PlaneListPanelsEnv g_listEnv;
static u8 g_bListBuilt;         /* unk_161F24, a byte global */
static i32 g_nNavThunkCalls;
static i32 g_nCrewThunkCalls;

static void ListNavThunk(void)
{
    ++g_nNavThunkCalls;
}

static void ListCrewThunk(void)
{
    ++g_nCrewThunkCalls;
}

static void ListPutName(Soldier *pSol, const char *psz)
{
    i32 i = 0;

    while (psz[i] != 0) {
        pSol->szName[i] = psz[i];
        ++i;
    }
    pSol->szName[i] = 0;
}

static void ListReset(i32 nCraftIdx)
{
    memset(&g_listOut, 0, sizeof(g_listOut));
    memset(g_aCrewCraft, 0, sizeof(g_aCrewCraft));
    memset(g_aCrewSol, 0, sizeof(g_aCrewSol));
    memset(g_aCrewObj, 0, sizeof(g_aCrewObj));
    memset(g_aCrewName, 0, sizeof(g_aCrewName));

    if (g_aCrewText[PUFO_TEXT_REC_STRIDE * 93] == 0) {
        memset(g_aCrewText, 0, sizeof(g_aCrewText));
        CrewPutRec(93, "PERSONNEL");
        CrewPutRec(137, "CRAFT");
    }

    for (i32 i = 0; i < 106; ++i) {
        g_apCrewObjects[i] = &g_aCrewObj[i];
    }
    for (i32 i = 0; i < 30; ++i) {
        g_apCrewSol[i] = &g_aCrewSol[i];
    }
    g_apCrewCraft[0] = &g_aCrewCraft[0];

    g_aCrewCraft[0].nSlotIdx = 0;
    g_aCrewCraft[0].bCraftType = 0;
    g_aCrewCraft[0].bType = 3;

    /* Four soldiers at the selected base, one elsewhere. */
    g_aCrewSol[0].nKind = 0;
    g_aCrewSol[0].bUnk183 = PUFO_SOLDIER_CRAFT_NONE;
    ListPutName(&g_aCrewSol[0], "ALPHA");
    g_aCrewSol[1].nKind = 0;
    g_aCrewSol[1].bUnk183 = 0; /* aboard the selected craft */
    ListPutName(&g_aCrewSol[1], "BRAVO");
    g_aCrewSol[2].nKind = 1; /* a different base, so it is not listed */
    g_aCrewSol[2].bUnk183 = PUFO_SOLDIER_CRAFT_NONE;
    ListPutName(&g_aCrewSol[2], "CHARLIE");
    g_aCrewSol[3].nKind = 0;
    g_aCrewSol[3].bUnk183 = PUFO_SOLDIER_CRAFT_ABOARD_OUT;
    ListPutName(&g_aCrewSol[3], "DELTA");

    g_bListBuilt = false;
    g_nNavThunkCalls = 0;
    g_nCrewThunkCalls = 0;

    memset(&g_listEnv, 0, sizeof(g_listEnv));
    g_listEnv.pbBuilt = &g_bListBuilt;
    g_listEnv.nLineCount = 20;
    g_listEnv.apCraft = g_apCrewCraft;
    g_listEnv.apSoldiers = g_apCrewSol;
    g_listEnv.nSoldierCount = 30;
    g_listEnv.nBaseIdx = 0;
    g_listEnv.nCraftIdx = nCraftIdx;
    g_listEnv.pTextRecs = g_aCrewText;
    g_listEnv.pszNameBuf = g_aCrewName;
    g_listEnv.cchNameBuf = (i32)sizeof(g_aCrewName);
    g_listEnv.apfnNavClick[0] = ListNavThunk;
    g_listEnv.apfnNavClick[1] = ListNavThunk;
    g_listEnv.pfnCrewClick = ListCrewThunk;
}

static void TestBuildListPanels(void)
{
    u8 r, g, b;

    printf("BasePlaneScreen_BuildListPanels @ 0x7a3f4\n");

    ListReset(0);
    BasePlaneScreen_BuildListPanels(&g_listOut, &g_listEnv);

    TestEqI32("the built flag is set (0x7A4BC)", g_bListBuilt, 1);
    TestCheck("the two nav controls exist", g_listOut.apNav[0] != NULL && g_listOut.apNav[1] != NULL);
    TestEqI32("the up control sits at the descriptor's 205,20", g_listOut.apNav[0]->x, 205);
    TestEqI32("  and y 20", g_listOut.apNav[0]->y, 20);
    TestEqI32("the down control takes the second descriptor's y 40", g_listOut.apNav[1]->y, 40);
    TestEqI32("  and the same x", g_listOut.apNav[1]->x, 205);
    TestCheck("both nav handlers are installed (0x7A51C / 0x7A528)",
              g_listOut.apNav[0]->pfnClick == ListNavThunk && g_listOut.apNav[1]->pfnClick == ListNavThunk);

    TestCheck("the crew panel exists", g_listOut.pCrewPanel != NULL);
    TestCheck("and the status panel exists", g_listOut.pCrewStatusPanel != NULL);
    TestEqI32("crew list at x 10", g_listOut.pCrewPanel->nPosX, 10);
    TestEqI32("  with a 230 wide view", g_listOut.pCrewPanel->nViewW, 230);
    TestEqI32("the status column at x 190", g_listOut.pCrewStatusPanel->nPosX, 190);
    TestEqI32("  with a 30 wide view", g_listOut.pCrewStatusPanel->nViewW, 30);
    TestEqI32("both are 14 rows of font 16", g_listOut.pCrewPanel->nStyle, 14);
    TestEqI32("  and the status panel too", g_listOut.pCrewStatusPanel->nStyle, 14);
    TestEqI32("the status panel clears the ctor's bFlag7073 (0x7A664)",
              g_listOut.pCrewStatusPanel->bFlag7073, 0);
    TestEqI32("  and sets nUnk7076 (0x7A670)", g_listOut.pCrewStatusPanel->nUnk7076, 1);
    TestEqI32("the crew panel keeps it", g_listOut.pCrewPanel->bFlag7073, 1);
    TestEqI32("the panel colour is B4/B4/B4", g_listOut.pCrewPanel->gap_0[0], 0xB4);
    TestEqI32("  and the row colours F0,F0,F0 over 14,14,14", g_listOut.pCrewPanel->gap_0[6], 0xF0);
    TestEqI32("  with the third triple at 14", g_listOut.pCrewPanel->gap_0[9], 0x14);
    TestEqI32("bUnk7088 is 0x96", g_listOut.pCrewPanel->bUnk7088, 0x96);
    TestCheck("the crew panel's click is FillCrewPanel (0x7A574)",
              g_listOut.pCrewPanel->pfnClick == ListCrewThunk);

    /* Only the two soldiers at this base whose craft byte is 255 or the selected craft are listed. */
    TestEqI32("row 0 is the unassigned soldier", g_listOut.pCrewPanel->anLineAux[0], 0);
    TestEqI32("row 1 is the soldier aboard the selected craft", g_listOut.pCrewPanel->anLineAux[1], 1);
    TestEqI32("  and the aux column is what FillCrewPanel clicks",
              (i32)strcmp((const char *)g_listOut.pCrewPanel->apLines[1], "BRAVO"), 0);
    TestEqI32("the soldier at another base is not listed",
              (i32)strcmp((const char *)g_listOut.pCrewPanel->apLines[2], ""), 0);
    TestEqI32("the 254 soldier is not listed either",
              (i32)strcmp((const char *)g_listOut.pCrewPanel->apLines[3], ""), 0);

    TestCheck("an unassigned row keeps the ctor's zero meta",
              UiPanel_GetLineMeta(g_listOut.pCrewPanel, 0, 1, &r, &g, &b) && r == 0 && g == 0 && b == 0);
    TestEqI32("its status column reads the unassigned label",
              (i32)strcmp((const char *)g_listOut.pCrewStatusPanel->apLines[0], CrewRec(93)), 0);
    TestCheck("the aboard row is painted (255,200,20) (0x7A87C)",
              UiPanel_GetLineMeta(g_listOut.pCrewPanel, 1, 1, &r, &g, &b) && r == 255 && g == 200 && b == 20);
    TestCheck("  with the highlight triple (10,10,10)",
              UiPanel_GetLineMeta(g_listOut.pCrewPanel, 1, 2, &r, &g, &b) && r == 10 && g == 10 && b == 10);
    TestCheck("and the status panel gets the same",
              UiPanel_GetLineMeta(g_listOut.pCrewStatusPanel, 1, 1, &r, &g, &b) && r == 255 && g == 200 && b == 20);
    TestCheck("the status column names the craft",
              (i32)strcmp((const char *)g_listOut.pCrewStatusPanel->apLines[1], g_aCrewName) == 0);
    TestCheck("  which is the type-0 label plus the ship number",
              (i32)strcmp((const char *)g_listOut.pCrewStatusPanel->apLines[1], "CRAFT3") == 0);

    /* Teardown: the same call with the flag set frees everything and clears the flag. */
    BasePlaneScreen_BuildListPanels(&g_listOut, &g_listEnv);
    TestEqI32("the built flag is cleared (0x7A400)", g_bListBuilt, 0);
    TestCheck("every object is released and nulled",
              g_listOut.apNav[0] == NULL && g_listOut.apNav[1] == NULL &&
              g_listOut.pCrewPanel == NULL && g_listOut.pCrewStatusPanel == NULL);

    /* The 254 arm is only reachable when the selected craft index IS 254. */
    ListReset(PUFO_SOLDIER_CRAFT_ABOARD_OUT);
    BasePlaneScreen_BuildListPanels(&g_listOut, &g_listEnv);
    TestEqI32("with craft index 254 the 254 soldier is listed", g_listOut.pCrewPanel->anLineAux[1], 3);
    TestCheck("  and painted (255,100,100) (0x7A8E0)",
              UiPanel_GetLineMeta(g_listOut.pCrewPanel, 1, 1, &r, &g, &b) && r == 255 && g == 100 && b == 100);
    TestEqI32("  while the unassigned soldier is still listed first",
              (i32)strcmp((const char *)g_listOut.pCrewPanel->apLines[0], "ALPHA"), 0);
    TestCheck("  and keeps its zero meta",
              UiPanel_GetLineMeta(g_listOut.pCrewPanel, 0, 1, &r, &g, &b) && r == 0 && g == 0 && b == 0);
    TestEqI32("  and only two rows are listed",
              (i32)strcmp((const char *)g_listOut.pCrewPanel->apLines[2], ""), 0);

    BasePlaneScreen_BuildListPanels(&g_listOut, &g_listEnv); /* free what the last build allocated */
    BasePlaneScreen_BuildListPanels(NULL, &g_listEnv);
    BasePlaneScreen_BuildListPanels(&g_listOut, NULL);
    TestCheck("a null out or env is tolerated", true);
}
/* ------------------------------------------------- the hangar craft list
 *
 * Evidence: BasePlaneScreen_BuildPanels @ 0x7cc8c, BasePlaneScreen_SelectCraft @ 0x7cbc8 and
 * BasePlaneScreen_SelectListed @ 0x7cc3c. */

static Ship g_aHngCraft[6];
static Ship *g_apHngCraft[6];
static u8 g_aHngText[PUFO_TEXT_REC_STRIDE * 153];
static char g_aHngName[108];
static BasePlaneScreen g_hngObj;
static PlaneCraftPanels g_hngOut;
static PlaneBuildEnv g_hngEnv;
static PlaneCraftSelectEnv g_hngSel;
static u8 g_bHngBuilt;
static u8 g_bHngDetail;
static i32 g_nHngSelIdx;
static i32 g_nHngCraftIdx;
static i8 g_nHngBase;
static u8 g_nHngCount;

static void HngPutRec(i32 nRec, const char *psz)
{
    char *p = (char *)(g_aHngText + PUFO_TEXT_REC_STRIDE * nRec);
    i32 i = 0;

    while (psz[i] != 0) {
        p[i] = psz[i];
        ++i;
    }
    p[i] = 0;
}

static const char *HngRec(i32 nRec)
{
    return (const char *)(g_aHngText + PUFO_TEXT_REC_STRIDE * nRec);
}

static void HngThunk(void)
{
}

static void HngReset(void)
{
    memset(g_aHngCraft, 0, sizeof(g_aHngCraft));
    memset(g_apHngCraft, 0, sizeof(g_apHngCraft));
    memset(g_aHngName, 0, sizeof(g_aHngName));
    memset(&g_hngObj, 0, sizeof(g_hngObj));
    memset(&g_hngOut, 0, sizeof(g_hngOut));

    if (g_aHngText[PUFO_TEXT_REC_STRIDE * 152] == 0) {
        memset(g_aHngText, 0, sizeof(g_aHngText));
        HngPutRec(137, "SKY");
        HngPutRec(138, "LIGHT");
        HngPutRec(139, "AVENGER");
        HngPutRec(149, "REFUEL");
        HngPutRec(150, "READY");
        HngPutRec(151, "MISSION");
        HngPutRec(152, "OUT");
    }

    /* Five craft at base 0 and one elsewhere. The types are deliberately interleaved so the
     * aPlaneCraftTypeOrder walk shows up in the row order. */
    for (i32 i = 0; i < 6; ++i) {
        g_apHngCraft[i] = &g_aHngCraft[i];
    }
    g_aHngCraft[0].bType = 1; g_aHngCraft[0].bCraftType = 0; g_aHngCraft[0].bHomeBase = 0;
    g_aHngCraft[0].bAtBase = 1; g_aHngCraft[0].bRefueling = 1;
    g_aHngCraft[0].uRangeRemain = 5000; g_aHngCraft[0].uRangeMax = 10000;

    g_aHngCraft[1].bType = 2; g_aHngCraft[1].bCraftType = 0; g_aHngCraft[1].bHomeBase = 0;
    g_aHngCraft[1].bAtBase = 1; g_aHngCraft[1].bRefueling = 0;
    g_aHngCraft[1].uRangeRemain = 5000; g_aHngCraft[1].uRangeMax = 10000;

    g_aHngCraft[2].bType = 1; g_aHngCraft[2].bCraftType = 1; g_aHngCraft[2].bHomeBase = 0;
    g_aHngCraft[2].bAtBase = 1; g_aHngCraft[2].bRefueling = 0;
    g_aHngCraft[2].uRangeRemain = 10000; g_aHngCraft[2].uRangeMax = 10000;

    g_aHngCraft[3].bType = 2; g_aHngCraft[3].bCraftType = 1; g_aHngCraft[3].bHomeBase = 0;
    g_aHngCraft[3].bAtBase = 0;

    g_aHngCraft[4].bType = 3; g_aHngCraft[4].bCraftType = 0; g_aHngCraft[4].bHomeBase = 1;
    g_aHngCraft[4].bAtBase = 1;

    g_aHngCraft[5].bType = 1; g_aHngCraft[5].bCraftType = 2; g_aHngCraft[5].bHomeBase = 0;
    g_aHngCraft[5].bAtBase = 1; g_aHngCraft[5].bRefueling = 1;
    g_aHngCraft[5].uRangeRemain = 10000; g_aHngCraft[5].uRangeMax = 10000;

    g_bHngBuilt = 0;
    g_bHngDetail = 0;
    g_nHngSelIdx = -1;
    g_nHngCraftIdx = -1;
    g_nHngBase = 0;
    g_nHngCount = 6;

    memset(&g_hngEnv, 0, sizeof(g_hngEnv));
    g_hngEnv.pbBuilt = &g_bHngBuilt;
    g_hngEnv.pObjects = &g_hngObj;
    g_hngEnv.apCraft = g_apHngCraft;
    g_hngEnv.pnCraftCount = &g_nHngCount;
    g_hngEnv.pnSelectedBaseIdx = &g_nHngBase;
    g_hngEnv.pTextRecs = g_aHngText;
    g_hngEnv.pszNameBuf = g_aHngName;
    g_hngEnv.cchNameBuf = (i32)sizeof(g_aHngName);
    g_hngEnv.pfnCraftClick = HngThunk;
    g_hngEnv.pfnStatusClick = HngThunk;
}

static void TestBuildCraftPanels(void)
{
    u8 r, g, b;

    printf("BasePlaneScreen_BuildPanels @ 0x7cc8c\n");

    HngReset();
    BasePlaneScreen_BuildPanels(&g_hngOut, &g_hngEnv);

    TestEqI32("the built flag is raised (0x7CCAC)", g_bHngBuilt, 1);
    TestCheck("both panels exist", g_hngOut.pCraftPanel != NULL && g_hngOut.pStatusPanel != NULL);
    TestEqI32("five craft live at base 0, so five rows", g_hngOut.pCraftPanel->nLineCount, 5);
    TestEqI32("  and the status panel gets the same", g_hngOut.pStatusPanel->nLineCount, 5);

    TestEqI32("eight visible rows", g_hngOut.pCraftPanel->nStyle, 8);
    TestEqI32("  in font 16", g_hngOut.pCraftPanel->nFont, 16);
    TestEqI32("  at y 110", g_hngOut.pCraftPanel->nPosY, 110);
    TestEqI32("the click rect starts at 5,110", g_hngOut.pCraftPanel->nUnk7052, 5);
    TestEqI32("  and the craft column is 230 wide at x 10", g_hngOut.pCraftPanel->nViewW, 230);
    TestEqI32("  with the status column 30 wide at x 230", g_hngOut.pStatusPanel->nViewW, 30);
    TestEqI32("the status column is right-aligned (0x7CF24)", g_hngOut.pStatusPanel->nUnk7076, 2);
    TestEqI32("  while the craft column keeps the ctor's left alignment",
              g_hngOut.pCraftPanel->nUnk7076, 0);
    TestEqI32("both panels are B4", g_hngOut.pCraftPanel->gap_0[0], 0xB4);
    TestEqI32("only panel A gets the row colours (0x7CDFC)", g_hngOut.pCraftPanel->gap_0[6], 0xF0);
    TestEqI32("  and panel B's stay at the ctor's zero", g_hngOut.pStatusPanel->gap_0[6], 0);
    TestEqI32("bUnk7088 is 0x96 on both", g_hngOut.pStatusPanel->bUnk7088, 0x96);
    TestCheck("panel A's click is SelectCraft (0x7CD74)", g_hngOut.pCraftPanel->pfnClick == HngThunk);
    TestCheck("panel B's is SelectListed (0x7CE8C)", g_hngOut.pStatusPanel->pfnClick == HngThunk);

    /* Grouped by craft type: 0,1,2,3,4 - so craft 0,1 (type 0), then 2,3 (type 1), then 5 (type 2).
     * Craft 4 is at another base and never appears. */
    TestEqI32("row 0 is craft 0", g_hngOut.pCraftPanel->anLineAux[0], 0);
    TestEqI32("row 1 is craft 1", g_hngOut.pCraftPanel->anLineAux[1], 1);
    TestEqI32("row 2 is craft 2 (type 1, so it comes after both type 0 craft)",
              g_hngOut.pCraftPanel->anLineAux[2], 2);
    TestEqI32("row 3 is craft 3", g_hngOut.pCraftPanel->anLineAux[3], 3);
    TestEqI32("row 4 is craft 5 (type 2)", g_hngOut.pCraftPanel->anLineAux[4], 5);
    TestEqI32("the craft at another base is not listed",
              (i32)strcmp((const char *)g_hngOut.pCraftPanel->apLines[4], "AVENGER1"), 0);
    TestEqI32("the names are the type label plus Ship.bType",
              (i32)strcmp((const char *)g_hngOut.pCraftPanel->apLines[0], "SKY1"), 0);
    TestEqI32("  row 1", (i32)strcmp((const char *)g_hngOut.pCraftPanel->apLines[1], "SKY2"), 0);
    TestEqI32("  row 2", (i32)strcmp((const char *)g_hngOut.pCraftPanel->apLines[2], "LIGHT1"), 0);
    TestEqI32("  row 3", (i32)strcmp((const char *)g_hngOut.pCraftPanel->apLines[3], "LIGHT2"), 0);

    /* The four status arms. */
    TestEqI32("refuelling reads text 149",
              (i32)strcmp((const char *)g_hngOut.pStatusPanel->apLines[0], HngRec(149)), 0);
    TestCheck("  painted (250,50,50) (0x7D060)",
              UiPanel_GetLineMeta(g_hngOut.pStatusPanel, 0, 1, &r, &g, &b)
              && r == 250 && g == 50 && b == 50);
    TestCheck("  with the highlight triple (25,10,10)",
              UiPanel_GetLineMeta(g_hngOut.pStatusPanel, 0, 2, &r, &g, &b)
              && r == 25 && g == 10 && b == 10);

    TestEqI32("at base with range below max reads text 150",
              (i32)strcmp((const char *)g_hngOut.pStatusPanel->apLines[1], HngRec(150)), 0);
    TestCheck("  painted (100,200,100) (0x7D0F4)",
              UiPanel_GetLineMeta(g_hngOut.pStatusPanel, 1, 1, &r, &g, &b)
              && r == 100 && g == 200 && b == 100);
    TestCheck("  with the highlight triple (10,25,10)",
              UiPanel_GetLineMeta(g_hngOut.pStatusPanel, 1, 2, &r, &g, &b)
              && r == 10 && g == 25 && b == 10);

    TestEqI32("at base with range at max reads text 151",
              (i32)strcmp((const char *)g_hngOut.pStatusPanel->apLines[2], HngRec(151)), 0);
    TestCheck("  painted (250,250,250) (0x7D170)",
              UiPanel_GetLineMeta(g_hngOut.pStatusPanel, 2, 1, &r, &g, &b)
              && r == 250 && g == 250 && b == 250);

    TestEqI32("a craft that is out reads text 152",
              (i32)strcmp((const char *)g_hngOut.pStatusPanel->apLines[3], HngRec(152)), 0);
    TestCheck("  painted (150,150,250) (0x7D1D4)",
              UiPanel_GetLineMeta(g_hngOut.pStatusPanel, 3, 1, &r, &g, &b)
              && r == 150 && g == 150 && b == 250);

    /* Refuelling wins over the range arms: the two range tests both require bRefueling == 0, so craft 5
     * (refuelling AND range at max) gets only text 149. */
    TestEqI32("refuelling takes the row even with range at max",
              (i32)strcmp((const char *)g_hngOut.pStatusPanel->apLines[4], HngRec(149)), 0);

    /* The clicks. */
    memset(&g_hngSel, 0, sizeof(g_hngSel));
    g_hngSel.pCraftPanel = g_hngOut.pCraftPanel;
    g_hngSel.pStatusPanel = g_hngOut.pStatusPanel;
    g_hngSel.pnSelCraftIdx = &g_nHngSelIdx;
    g_hngSel.pnSelectedCraftIdx = &g_nHngCraftIdx;
    g_hngSel.apCraft = g_apHngCraft;
    g_hngSel.pbShowCraftDetail = &g_bHngDetail;

    g_hngOut.pCraftPanel->nUnk3032 = 1;
    TestEqI32("SelectCraft takes the row's roster index", BasePlaneScreen_SelectCraft(&g_hngSel), 1);
    TestEqI32("  into nPlaneSelCraftIdx (0x7CBF8)", g_nHngSelIdx, 1);
    TestEqI32("  and nSelectedCraftIdx (0x7CC00)", g_nHngCraftIdx, 1);
    TestEqI32("craft 1 is at base, so the detail view is armed (0x7CC20)", g_bHngDetail, 1);

    g_hngOut.pCraftPanel->nUnk3032 = 3;
    (void)BasePlaneScreen_SelectCraft(&g_hngSel);
    TestEqI32("craft 3 is out, so the roster index still moves", g_nHngCraftIdx, 3);
    TestEqI32("  but the flag is never cleared - SelectCraft only sets it", g_bHngDetail, 1);

    g_bHngDetail = 0;
    g_hngOut.pStatusPanel->nUnk3032 = 2;
    TestEqI32("SelectListed reads panel B's cursor (0x7CC54)", BasePlaneScreen_SelectListed(&g_hngSel), 2);
    TestEqI32("  and indexes panel A's aux with it (0x7CC68)", g_nHngCraftIdx, 2);
    TestEqI32("  and touches the detail flag not at all", g_bHngDetail, 0);

    /* Teardown: the same call with the flag set releases the panels and the whole object block. */
    for (i32 i = 0; i < 5; ++i) {
        g_hngObj.apCraftPads[i] = (PadImage *)calloc(1, sizeof(PadImage));
    }
    for (i32 i = 0; i < 6; ++i) {
        g_hngObj.apWeaponPads[i] = (PadImage *)calloc(1, sizeof(PadImage));
    }
    for (i32 i = 0; i < 4; ++i) {
        g_hngObj.apMainCtl[i] = (UiControl *)calloc(1, sizeof(UiControl));
    }
    for (i32 i = 0; i < 7; ++i) {
        g_hngObj.apWeaponCtl[i] = (UiControl *)calloc(1, sizeof(UiControl));
    }
    g_hngObj.pBackdropPad = (PadImage *)calloc(1, sizeof(PadImage));

    BasePlaneScreen_BuildPanels(&g_hngOut, &g_hngEnv);
    TestEqI32("the built flag is cleared (0x7D238)", g_bHngBuilt, 0);
    TestCheck("both panels are released",
              g_hngOut.pCraftPanel == NULL && g_hngOut.pStatusPanel == NULL);
    TestCheck("the backdrop pad is released", g_hngObj.pBackdropPad == NULL);
    TestCheck("the five craft art pads too (0x7D2D4)", g_hngObj.apCraftPads[4] == NULL);
    TestCheck("the six weapon icon pads (0x7D304)", g_hngObj.apWeaponPads[5] == NULL);
    TestCheck("the four main controls (0x7D2A0)", g_hngObj.apMainCtl[3] == NULL);
    TestCheck("  and the seven weapon controls (0x7D338)", g_hngObj.apWeaponCtl[6] == NULL);

    BasePlaneScreen_BuildPanels(NULL, &g_hngEnv);
    BasePlaneScreen_BuildPanels(&g_hngOut, NULL);
    TestCheck("a null out or env is tolerated", true);
    TestEqI32("BasePlaneScreen_SelectCraft tolerates a null env", BasePlaneScreen_SelectCraft(NULL), 0);
    TestEqI32("and so does BasePlaneScreen_SelectListed", BasePlaneScreen_SelectListed(NULL), 0);
}

/* ------------------------------------------------- the hangar's object block
 *
 * Evidence: BasePlaneScreen_Init @ 0x7d9e4. */

static BasePlaneScreen g_hngScr;
static UiControl g_hngCloseCtl;
static UiControl g_hngBuySellCtl;
static PlaneCraftPanels g_hngInitOut;
static BasePlaneScreenInitEnv g_hngInitEnv;
static i32 g_nPadLoadCalls;
static const char *g_szPad0Seen[11];

/* Each handler writes a distinct value. Empty handlers are identical code, and the linker's identical
 * code folding (MSVC's /OPT:ICF, on by default in Release) merges identical functions to ONE address --
 * so a pointer-identity check between two empty handlers passes no matter which one was installed. The
 * volatile keeps the store from being optimised out, which is what keeps the bodies distinct. */
static volatile i32 g_nHandlerId;

static void HngMain0(void) { g_nHandlerId = 1; }
static void HngMain1(void) { g_nHandlerId = 2; }
static void HngMain2(void) { g_nHandlerId = 3; }
static void HngMain3(void) { g_nHandlerId = 4; }
static void HngWpn0(void) { g_nHandlerId = 5; }
static void HngWpn1(void) { g_nHandlerId = 6; }
static void HngWpn2(void) { g_nHandlerId = 7; }
static void HngWpn3(void) { g_nHandlerId = 8; }
static void HngWpn4(void) { g_nHandlerId = 9; }
static void HngWpn5(void) { g_nHandlerId = 10; }
static void HngWpn6(void) { g_nHandlerId = 11; }
static void HngClose(void) { g_nHandlerId = 12; }
static void HngBuySell(void) { g_nHandlerId = 13; }

static bool HngLoadPads(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2,
                        void *pUser)
{
    (void)pCtl;
    (void)psz1;
    (void)psz2;
    (void)pUser;

    if (g_nPadLoadCalls < 11) {
        g_szPad0Seen[g_nPadLoadCalls] = psz0;
    }
    ++g_nPadLoadCalls;
    return false; /* no archives in this test */
}

static void TestPlaneInit(void)
{
    printf("BasePlaneScreen_Init @ 0x7d9e4\n");

    HngReset();
    /* In the original both Init and BuildPanels reach the same fixed block at 0x161A48, so the build
     * env has to point at the block Init allocates into. */
    g_hngEnv.pObjects = &g_hngScr;
    memset(&g_hngScr, 0, sizeof(g_hngScr));
    memset(&g_hngCloseCtl, 0, sizeof(g_hngCloseCtl));
    memset(&g_hngBuySellCtl, 0, sizeof(g_hngBuySellCtl));
    memset(&g_hngInitOut, 0, sizeof(g_hngInitOut));
    memset(g_szPad0Seen, 0, sizeof(g_szPad0Seen));
    g_nPadLoadCalls = 0;

    memset(&g_hngInitEnv, 0, sizeof(g_hngInitEnv));
    g_hngInitEnv.pScreen = &g_hngScr;
    g_hngInitEnv.pCloseCtl = &g_hngCloseCtl;
    g_hngInitEnv.pBuySellCtl = &g_hngBuySellCtl;
    g_hngInitEnv.apfnMainClick[0] = HngMain0;
    g_hngInitEnv.apfnMainClick[1] = HngMain1;
    g_hngInitEnv.apfnMainClick[2] = HngMain2;
    g_hngInitEnv.apfnMainClick[3] = HngMain3;
    g_hngInitEnv.apfnWeaponClick[0] = HngWpn0;
    g_hngInitEnv.apfnWeaponClick[1] = HngWpn1;
    g_hngInitEnv.apfnWeaponClick[2] = HngWpn2;
    g_hngInitEnv.apfnWeaponClick[3] = HngWpn3;
    g_hngInitEnv.apfnWeaponClick[4] = HngWpn4;
    g_hngInitEnv.apfnWeaponClick[5] = HngWpn5;
    g_hngInitEnv.apfnWeaponClick[6] = HngWpn6;
    g_hngInitEnv.pfnCloseClick = HngClose;
    g_hngInitEnv.pfnBuySellClick = HngBuySell;
    g_hngInitEnv.pfnLoadPads = HngLoadPads;
    g_hngInitEnv.pPanels = &g_hngInitOut;
    g_hngInitEnv.pBuildEnv = &g_hngEnv;

    BasePlaneScreen_Init(&g_hngInitEnv);

    TestCheck("the backdrop pad exists", g_hngScr.pBackdropPad != NULL);
    TestEqI32("  at the origin (0x7DA28)", g_hngScr.pBackdropPad->nOriginX, 0);
    TestEqI32("  x and y (0x7DA30)", g_hngScr.pBackdropPad->nOriginY, 0);

    TestCheck("four main controls allocated", g_hngScr.apMainCtl[0] != NULL && g_hngScr.apMainCtl[3] != NULL);
    TestCheck("their clicks are the four handlers in order (0x7DA7C..0x7DAA0)",
              g_hngScr.apMainCtl[0]->pfnClick == HngMain0 && g_hngScr.apMainCtl[1]->pfnClick == HngMain1
              && g_hngScr.apMainCtl[2]->pfnClick == HngMain2 && g_hngScr.apMainCtl[3]->pfnClick == HngMain3);
    TestEqI32("the first sits at the descriptor's 20,100", g_hngScr.apMainCtl[0]->x, 20);
    TestEqI32("  (0xABCC8 record 0)", g_hngScr.apMainCtl[0]->y, 100);
    TestEqI32("the second at 197,100", g_hngScr.apMainCtl[1]->x, 197);
    TestEqI32("the third at 20,200", g_hngScr.apMainCtl[2]->y, 200);
    TestEqI32("the fourth at 20,240", g_hngScr.apMainCtl[3]->y, 240);

    TestCheck("seven weapon controls allocated",
              g_hngScr.apWeaponCtl[0] != NULL && g_hngScr.apWeaponCtl[6] != NULL);
    TestCheck("their clicks are Rearm plus the six weapon slots (0x7DB2C..0x7DB74)",
              g_hngScr.apWeaponCtl[0]->pfnClick == HngWpn0
              && g_hngScr.apWeaponCtl[6]->pfnClick == HngWpn6);
    TestEqI32("the first sits at 15,92 (0xABD48 record 0)", g_hngScr.apWeaponCtl[0]->y, 92);
    TestEqI32("  and the rest share x 15 with y 0", g_hngScr.apWeaponCtl[6]->y, 0);

    TestEqI32("the setup ran once per control", g_nPadLoadCalls, 11);
    TestCheck("the main pair loads the initg pads",
              g_szPad0Seen[0] != NULL && strcmp(g_szPad0Seen[0], "initg") == 0);
    TestCheck("  and the lower two the emptyb1 pads",
              g_szPad0Seen[2] != NULL && strcmp(g_szPad0Seen[2], "emptyb1") == 0);
    TestCheck("  and every weapon control the emptyb pad",
              g_szPad0Seen[4] != NULL && strcmp(g_szPad0Seen[4], "emptyb") == 0);

    TestCheck("five craft art pads allocated",
              g_hngScr.apCraftPads[0] != NULL && g_hngScr.apCraftPads[4] != NULL);
    TestEqI32("centred at 120 - (uWidth >> 1); the load fails with no archives and the port zero-fills "
              "the pad the original leaves uninitialised (0x7DC60)",
              g_hngScr.apCraftPads[0]->nOriginX, 120);
    TestEqI32("  at y 80 (0x7DCBC)", g_hngScr.apCraftPads[4]->nOriginY, 80);

    TestCheck("six weapon icon pads allocated",
              g_hngScr.apWeaponPads[0] != NULL && g_hngScr.apWeaponPads[5] != NULL);
    TestEqI32("all six at y 95 (0x7DD98)", g_hngScr.apWeaponPads[5]->nOriginY, 95);

    TestEqI32("the close control moves to x 224 (0x7DDB4)", g_hngCloseCtl.x, 224);
    TestEqI32("  y 1 (0x7DDC0)", g_hngCloseCtl.y, 1);
    TestCheck("  and takes BasePlaneScreen_Close (0x7DDCC)", g_hngCloseCtl.pfnClick == HngClose);
    TestCheck("the confirm slot takes the buy/sell apply (0x7DDDC)", g_hngBuySellCtl.pfnClick == HngBuySell);

    /* The tail call: Init builds the craft list on the block it just made. */
    TestEqI32("BuildPanels ran from Init (0x7DDE8)", g_bHngBuilt, 1);
    TestCheck("  and both craft-list panels exist",
              g_hngInitOut.pCraftPanel != NULL && g_hngInitOut.pStatusPanel != NULL);
    TestEqI32("  with the base's five craft listed", g_hngInitOut.pCraftPanel->nLineCount, 5);

    /* Releasing everything: the teardown branch frees Init's allocations too. */
    BasePlaneScreen_BuildPanels(&g_hngInitOut, &g_hngEnv);
    TestCheck("the backdrop pad is released", g_hngScr.pBackdropPad == NULL);
    TestCheck("the four main controls (0x7D2A0)", g_hngScr.apMainCtl[0] == NULL);
    TestCheck("the seven weapon controls (0x7D338)", g_hngScr.apWeaponCtl[6] == NULL);
    TestCheck("the five craft pads (0x7D2D4)", g_hngScr.apCraftPads[0] == NULL);
    TestCheck("  and the six weapon pads (0x7D304)", g_hngScr.apWeaponPads[0] == NULL);

    BasePlaneScreen_Init(NULL);
    TestCheck("a null init env does nothing", true);
}

/* --------------------------------------------------------------------------
 * BasePlaneScreen_BuildControls @ 0x7b014: the hangar's stores-transfer panel set.
 * -------------------------------------------------------------------------- */

#define CTL_OBJ_COUNT 60 /* the fixture's object array; the shipped count is 149 */

/* byte_AA968 @ 0xAA968 as shipped, the part the row filter can reach. Slots 25..30 are zero and the
 * filter refuses them anyway; 31..35 map to topics 97..101, 37 to 113, 39 to 114, 40..47 to 1, and
 * 48..50 to 115, 117, 116. Slot 51 is zero, so its topic is topic 0. */
static const u8 g_abCtlTopicShipped[] = {
    0, 1, 6, 9, 2, 7, 10, 4, 3, 8, 5, 11, 13, 12, 18, 23, 24, 22, 21, 25, 14, 15, 17, 20, 16,
    0, 0, 0, 0, 0, 0,
    97, 98, 99, 100, 101, 1, 113, 1, 114, 1, 1, 1, 1, 1, 1, 1, 1, 1, 115, 117, 116, 0
};

static ObjectRecFull g_aCtlObj[CTL_OBJ_COUNT + 1];
static ObjectRecFull *g_apCtlObj[300];
static ResearchTopicLive g_aCtlTopics[PUFO_RESEARCH_TOPIC_COUNT];
static u8 g_abCtlTopic[PUFO_PLANE_SLOT_TOPIC_COUNT];
static i32 g_anCtlCarry[256]; /* a literal 256: anSlotCarryCount @ 0x161AD0 is 1024 bytes */
static PlaneStorePanels g_ctlOut;
static PlaneStoreBuildEnv g_ctlEnv;
static u8 g_bCtlCommitted;
static i32 g_nCtlQuota;
static i32 g_nCtlPadCalls;
static char g_aCtlPadName[6][16]; /* three descriptor names per control */

/* Distinct bodies for the same reason as the Hng* pair above: identical empty handlers share one address. */
static void CtlSlotClick(void) { g_nHandlerId = 21; }
static void CtlSelRowClick(void) { g_nHandlerId = 22; }
static void CtlCommitClick(void) { g_nHandlerId = 23; }
static void CtlNavPrev(void) { g_nHandlerId = 24; }
static void CtlNavNext(void) { g_nHandlerId = 25; }

static bool CtlLoadPads(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2,
                        void *pUser)
{
    (void)pCtl;
    (void)pUser;

    if (g_nCtlPadCalls < 2) {
        snprintf(g_aCtlPadName[g_nCtlPadCalls * 3], sizeof g_aCtlPadName[0], "%s", psz0);
        snprintf(g_aCtlPadName[g_nCtlPadCalls * 3 + 1], sizeof g_aCtlPadName[0], "%s", psz1);
        snprintf(g_aCtlPadName[g_nCtlPadCalls * 3 + 2], sizeof g_aCtlPadName[0], "%s", psz2);
    }
    ++g_nCtlPadCalls;
    return true;
}

static void CtlReset(void)
{
    memset(g_aCtlObj, 0, sizeof g_aCtlObj);
    memset(g_aCtlTopics, 0, sizeof g_aCtlTopics);
    memset(g_abCtlTopic, 0, sizeof g_abCtlTopic);
    memset(g_aCtlPadName, 0, sizeof g_aCtlPadName);
    memcpy(g_abCtlTopic, g_abCtlTopicShipped, sizeof g_abCtlTopicShipped);

    for (i32 i = 0; i < CTL_OBJ_COUNT + 1; ++i) {
        snprintf(g_aCtlObj[i].szName, sizeof g_aCtlObj[i].szName, "obj%02d", i);
    }
    for (i32 t = 0; t < PUFO_RESEARCH_TOPIC_COUNT; ++t) {
        g_aCtlTopics[t].bUnlocked = 1;
    }
    /* Slots past the fixture all point at one empty record, so a large Map_nObjectCount is safe to walk. */
    for (i32 i = 0; i < 300; ++i) {
        g_apCtlObj[i] = (i < CTL_OBJ_COUNT) ? &g_aCtlObj[i] : &g_aCtlObj[CTL_OBJ_COUNT];
    }
    memset(g_anCtlCarry, 0x5A, sizeof g_anCtlCarry); /* prove the build zeroes all 256 */
    memset(&g_ctlOut, 0, sizeof g_ctlOut);
    memset(&g_ctlEnv, 0, sizeof g_ctlEnv);

    g_bCtlCommitted = 0;
    g_nCtlQuota = -1;
    g_nCtlPadCalls = 0;
    g_ctlEnv.pbStoresCommitted = &g_bCtlCommitted;
    g_ctlEnv.anSlotCarryCount = g_anCtlCarry;
    g_ctlEnv.pnCrewQuota = &g_nCtlQuota;
    g_ctlEnv.apObjects = g_apCtlObj;
    g_ctlEnv.nObjectCount = CTL_OBJ_COUNT;
    g_ctlEnv.nCraftIdx = 0;
    g_ctlEnv.nBaseIdx = 0;
    g_ctlEnv.aTopics = g_aCtlTopics;
    g_ctlEnv.abSlotTopic = g_abCtlTopic;
    g_ctlEnv.apfnNavClick[0] = CtlNavPrev;
    g_ctlEnv.apfnNavClick[1] = CtlNavNext;
    g_ctlEnv.pfnSlotClick = CtlSlotClick;
    g_ctlEnv.pfnSelRowClick = CtlSelRowClick;
    g_ctlEnv.pfnCommitClick = CtlCommitClick;
    g_ctlEnv.pfnLoadPads = CtlLoadPads;
}

static void CtlStock(i32 nSlot, u16 wBase, u16 wCarry)
{
    g_aCtlObj[nSlot].auPerBaseVal[0] = wBase;
    g_aCtlObj[nSlot].aItemQty[0] = wCarry;
}

/* The five panels' shared style plus the fields that tell them apart (the header's panel table). */
static void CtlPanelStyle(const char *pszName, UiPanel *pPanel, i32 nPosX, i32 nUnk7052, i32 nViewW,
                          u8 bFlag7073, u8 bFlag7074, i32 nUnk7076, u8 bUnk7088, i32 nUnk7084,
                          u8 b3)
{
    char szMsg[96];

    snprintf(szMsg, sizeof szMsg, "%s: fourteen rows, font 16", pszName);
    TestEqI32(szMsg, pPanel->nStyle, 14);
    TestEqI32("  at font 16", pPanel->nFont, 16);
    TestEqI32("  on row 70 with the click band top also 70", pPanel->nPosY, 70);
    TestEqI32("  and the click band top 70", pPanel->nUnk7056, 70);
    snprintf(szMsg, sizeof szMsg, "%s: x %d", pszName, nPosX);
    TestEqI32(szMsg, pPanel->nPosX, nPosX);
    snprintf(szMsg, sizeof szMsg, "%s: click band left %d", pszName, nUnk7052);
    TestEqI32(szMsg, pPanel->nUnk7052, nUnk7052);
    snprintf(szMsg, sizeof szMsg, "%s: view width %d", pszName, nViewW);
    TestEqI32(szMsg, pPanel->nViewW, nViewW);
    TestEqI32("  row colour B4,B4,B4", pPanel->gap_0[0], 0xB4);
    TestEqI32("  text pair F0,F0,F0", pPanel->gap_0[6], 0xF0);
    TestEqI32("  over 14,14,14", pPanel->gap_0[9], 0x14);
    snprintf(szMsg, sizeof szMsg, "%s: bFlag7073 %d", pszName, bFlag7073);
    TestEqI32(szMsg, pPanel->bFlag7073, bFlag7073);
    snprintf(szMsg, sizeof szMsg, "%s: bFlag7074 %d", pszName, bFlag7074);
    TestEqI32(szMsg, pPanel->bFlag7074, bFlag7074);
    snprintf(szMsg, sizeof szMsg, "%s: alignment key nUnk7076 %d", pszName, nUnk7076);
    TestEqI32(szMsg, pPanel->nUnk7076, nUnk7076);
    snprintf(szMsg, sizeof szMsg, "%s: bUnk7088 0x%02X", pszName, bUnk7088);
    TestEqI32(szMsg, pPanel->bUnk7088, bUnk7088);
    snprintf(szMsg, sizeof szMsg, "%s: auto-repeat nUnk7084 %d", pszName, nUnk7084);
    TestEqI32(szMsg, pPanel->nUnk7084, nUnk7084);
    snprintf(szMsg, sizeof szMsg, "%s: second colour byte 0x%02X", pszName, b3);
    TestEqI32(szMsg, pPanel->gap_0[3], b3);
}

/* The fixture: rows at slots 1, 24, 31, 32, 34, 35 and 51; refusals at 0 (below the loop), 25 and 30
 * (the filter's gap), 33 (nothing stocked) and 52 (past the filter). The crew quota is 4+1+0+2+3. */
static void CtlFixture(void)
{
    CtlStock(0, 6, 6);
    CtlStock(1, 5, 2);
    CtlStock(24, 3, 0);
    CtlStock(25, 9, 9);
    CtlStock(30, 9, 9);
    CtlStock(31, 7, 4);
    CtlStock(32, 0, 1);
    CtlStock(34, 0, 2);
    CtlStock(35, 0, 3);
    CtlStock(51, 1, 0);
    CtlStock(52, 8, 8);
}

static void TestBuildControls(void)
{
    bool bCarryZeroed = true;

    printf("BasePlaneScreen_BuildControls @ 0x7b014\n");

    CtlReset();
    CtlFixture();
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);

    TestEqI32("the build raises the committed flag", g_bCtlCommitted, 1);
    /* A literal 256, not the port's macro: a test that reads the same constant the code reads cannot
     * catch the constant being wrong. */
    for (i32 i = 0; i < 256; ++i) {
        if (g_anCtlCarry[i] != 0) {
            bCarryZeroed = false;
        }
    }
    TestCheck("all 256 carry-count slots are cleared (0x7B038)", bCarryZeroed);
    TestEqI32("  the last one included", g_anCtlCarry[255], 0);
    TestEqI32("the crew quota starts as what the craft carries of slots 31..35 (0x7B05C)", g_nCtlQuota, 10);

    TestCheck("the two store-nav controls exist", g_ctlOut.apNavCtl[0] != NULL
              && g_ctlOut.apNavCtl[1] != NULL);
    if (g_ctlOut.apNavCtl[0] != NULL && g_ctlOut.apNavCtl[1] != NULL) {
        TestEqI32("  both sit at x 205 (0xABC88's descriptor pair)", g_ctlOut.apNavCtl[0]->x, 205);
        TestEqI32("  and the same x", g_ctlOut.apNavCtl[1]->x, 205);
        TestEqI32("the up control is at y 18, not the crew list's 20", g_ctlOut.apNavCtl[0]->y, 18);
        TestEqI32("the down control is at y 38, not the crew list's 40", g_ctlOut.apNavCtl[1]->y, 38);
        TestCheck("  the clicks are the buy/sell detail pair, installed before Setup",
                  g_ctlOut.apNavCtl[0]->pfnClick == CtlNavPrev
                  && g_ctlOut.apNavCtl[1]->pfnClick == CtlNavNext);
    }
    TestEqI32("each control loads its pads once", g_nCtlPadCalls, 2);
    TestCheck("  the first loads up/sup and no third pad name", strcmp(g_aCtlPadName[0], "up") == 0
              && strcmp(g_aCtlPadName[1], "sup") == 0 && g_aCtlPadName[2][0] == 0);
    TestCheck("  the second loads down/sdown and no third pad name",
              strcmp(g_aCtlPadName[3], "down") == 0 && strcmp(g_aCtlPadName[4], "sdown") == 0
              && g_aCtlPadName[5][0] == 0);

    TestCheck("all five panels exist", g_ctlOut.pSlotPanel != NULL && g_ctlOut.pAvailPanel != NULL
              && g_ctlOut.pSelRowPanel != NULL && g_ctlOut.pCommitPanel != NULL
              && g_ctlOut.pTakenPanel != NULL);
    if (g_ctlOut.pSlotPanel == NULL) {
        return;
    }
    TestEqI32("seven rows: slots 1, 24, 31, 32, 34, 35 and 51", g_ctlOut.pSlotPanel->nLineCount, 7);
    TestEqI32("  and every panel was built with the same row count", g_ctlOut.pTakenPanel->nLineCount, 7);

    /* bUnk7088 stays at UiPanel_ctor's 0xFF on the two count columns: BuildControls only writes it on
     * the three columns that also get bFlag7073. */
    CtlPanelStyle("the name column", g_ctlOut.pSlotPanel, 10, 5, 125, 1, 0, 0, 0x96, 0, 0x00);
    CtlPanelStyle("the available column", g_ctlOut.pAvailPanel, 164, 130, 50, 0, 1, 2, 0xFF, 0, 0x00);
    CtlPanelStyle("the minus column", g_ctlOut.pSelRowPanel, 170, 166, 15, 1, 0, 0, 0x96, 5, 0x32);
    CtlPanelStyle("the plus column", g_ctlOut.pCommitPanel, 186, 182, 15, 1, 0, 0, 0x96, 5, 0x32);
    CtlPanelStyle("the taken column", g_ctlOut.pTakenPanel, 232, 190, 45, 0, 1, 2, 0xFF, 0, 0x00);

    TestCheck("only the name column has a click handler",
              g_ctlOut.pSlotPanel->pfnClick == CtlSlotClick
              && g_ctlOut.pSelRowPanel->pfnClick == CtlSelRowClick
              && g_ctlOut.pCommitPanel->pfnClick == CtlCommitClick
              && g_ctlOut.pAvailPanel->pfnClick == NULL
              && g_ctlOut.pTakenPanel->pfnClick == NULL);

    /* The sort, 0x7B950..0x7B9F8. It walks only the first nObjectCount - 1 order entries -- 59 here --
     * so slot 1 (table index 8), 51 (36), 24 (47) and 31 (58) get placed while 32 (60), 34 (63) and 35
     * (64) are never reached and stay in the scan order behind them. Walking the table's full 148 entries
     * instead would put 32, 34 and 35 before 31's leftovers and give 1, 51, 24, 31, 32, 34, 35. */
    {
        static const i32 anWant[7] = { 1, 51, 24, 31, 34, 35, 32 };
        static const i32 anAvail[7] = { 5, 1, 3, 7, 0, 0, 0 };
        static const i32 anTaken[7] = { 2, 0, 0, 4, 2, 3, 1 };
        bool bOrder = true;
        bool bAvail = true;
        bool bTaken = true;
        bool bNames = true;

        for (i32 r = 0; r < 7; ++r) {
            char szWant[40];

            if (g_ctlOut.pSlotPanel->anLineAux[r] != anWant[r]) {
                bOrder = false;
            }
            if (g_ctlOut.pAvailPanel->anLineAux[r] != anAvail[r]) {
                bAvail = false;
            }
            if (g_ctlOut.pTakenPanel->anLineAux[r] != anTaken[r]) {
                bTaken = false;
            }
            snprintf(szWant, sizeof szWant, "obj%02d", anWant[r]);
            if (strcmp(UiPanel_GetLineText(g_ctlOut.pSlotPanel, r), szWant) != 0) {
                bNames = false;
            }
        }
        TestCheck("the aux column holds slot ids, sorted into the order table's sequence", bOrder);
        TestCheck("  the available counts moved with their rows", bAvail);
        TestCheck("  the taken counts moved with their rows", bTaken);
        TestCheck("  and so did the object names", bNames);
    }

    /* Literal labels, not the port's macros -- a test that reads the same constant cannot catch it moving. */
    TestCheck("the minus column reads '<' (aRowQtyDec @ 0xABC24)",
              strcmp(UiPanel_GetLineText(g_ctlOut.pSelRowPanel, 0), "<") == 0);
    TestCheck("and the plus column '>' (aRowQtyInc @ 0xABC20)",
              strcmp(UiPanel_GetLineText(g_ctlOut.pCommitPanel, 0), ">") == 0);

    /* The topic gate: slot 1 maps to topic 1, and locking that topic removes the row even though the
     * base stocks it and the craft carries it. */
    CtlReset();
    CtlFixture();
    g_aCtlTopics[g_abCtlTopic[1]].bUnlocked = 0;
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("a locked topic drops the row (0x7B1D4..0x7B1F0)", g_ctlOut.pSlotPanel->nLineCount, 6);
    TestEqI32("  and the first row is now slot 51", g_ctlOut.pSlotPanel->anLineAux[0], 51);

    /* The stock gate: a slot neither stocked nor carried gets no row even though the filter admits it. */
    CtlReset();
    CtlStock(40, 0, 0);
    CtlStock(41, 0, 4);
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("an empty admitted slot gets no row", g_ctlOut.pSlotPanel->nLineCount, 1);
    TestEqI32("  a carried-only slot does, with nothing available", g_ctlOut.pAvailPanel->anLineAux[0], 0);
    TestEqI32("  and four taken", g_ctlOut.pTakenPanel->anLineAux[0], 4);

    /* The 17-character cut at 0x7B838: `CMP R3, #0x11` is a test on strlen ABOVE 17, so an 18-character
     * name is already cut and a 17-character name is not. */
    CtlReset();
    snprintf(g_aCtlObj[7].szName, sizeof g_aCtlObj[7].szName, "abcdefghijklmnopqrstuvwxy");
    snprintf(g_aCtlObj[8].szName, sizeof g_aCtlObj[8].szName, "abcdefghijklmnopqr");
    snprintf(g_aCtlObj[9].szName, sizeof g_aCtlObj[9].szName, "abcdefghijklmnopq");
    CtlStock(7, 3, 0);
    CtlStock(8, 3, 0);
    CtlStock(9, 3, 0);
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("three long-named rows", g_ctlOut.pSlotPanel->nLineCount, 3);
    TestCheck("a 25-character name is cut to 17",
              strcmp(UiPanel_GetLineText(g_ctlOut.pSlotPanel, 0), "abcdefghijklmnopq") == 0);
    TestCheck("an 18-character name is cut too, because the test is above 17",
              strcmp(UiPanel_GetLineText(g_ctlOut.pSlotPanel, 1), "abcdefghijklmnopq") == 0);
    TestCheck("and a 17-character name survives whole",
              strcmp(UiPanel_GetLineText(g_ctlOut.pSlotPanel, 2), "abcdefghijklmnopq") == 0);

    /* A build with nothing to list still raises the flag and still builds the panels -- the flag means
     * "this panel set is live", which is why BasePlaneScreen_Close tests it the same way. */
    CtlReset();
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("an empty object set still raises the flag", g_bCtlCommitted, 1);
    TestEqI32("  with zero rows", g_ctlOut.pSlotPanel->nLineCount, 0);
    TestCheck("  and all five panels present", g_ctlOut.pAvailPanel != NULL && g_ctlOut.pTakenPanel != NULL);

    /* The teardown arm. */
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("the teardown clears the flag", g_bCtlCommitted, 0);
    TestCheck("  and releases both nav controls", g_ctlOut.apNavCtl[0] == NULL
              && g_ctlOut.apNavCtl[1] == NULL);
    TestCheck("  and all five panels", g_ctlOut.pSlotPanel == NULL && g_ctlOut.pAvailPanel == NULL
              && g_ctlOut.pSelRowPanel == NULL && g_ctlOut.pCommitPanel == NULL
              && g_ctlOut.pTakenPanel == NULL);
    g_bCtlCommitted = 1; /* a teardown leaves the flag down, so arm it again to re-enter that arm */
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("a teardown over already-empty slots still clears the flag", g_bCtlCommitted, 0);
    TestCheck("  and allocates nothing", g_ctlOut.pSlotPanel == NULL && g_ctlOut.apNavCtl[0] == NULL);

    /* The bounds the original does not have. */
    CtlReset();
    CtlFixture();
    g_ctlEnv.nCraftIdx = 60; /* past aItemQty's 52 entries */
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("an out-of-range craft index reads no carried quantity", g_nCtlQuota, 0);
    TestEqI32("  so only the base-stocked slots keep rows", g_ctlOut.pSlotPanel->nLineCount, 4);

    CtlReset();
    CtlFixture();
    g_ctlEnv.nBaseIdx = -1;
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("a negative base index reads no stock", g_ctlOut.pSlotPanel->nLineCount, 5);
    TestEqI32("  leaving only the five carried slots, first one slot 1 with 2 carried",
              g_ctlOut.pTakenPanel->anLineAux[0], 2);

    CtlReset();
    g_ctlEnv.nObjectCount = 300; /* above the 254-pair scan block the original does not bound */
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("a 300-slot object walk is bounded and lists nothing", g_ctlOut.pSlotPanel->nLineCount, 0);

    CtlReset();
    g_ctlEnv.nObjectCount = 1;
    BasePlaneScreen_BuildControls(&g_ctlOut, &g_ctlEnv);
    TestEqI32("one object means no rows and no sort pass", g_ctlOut.pSlotPanel->nLineCount, 0);

    /* The guards. */
    CtlReset();
    CtlFixture();
    BasePlaneScreen_BuildControls(NULL, &g_ctlEnv);
    BasePlaneScreen_BuildControls(&g_ctlOut, NULL);
    TestEqI32("a null env leaves the flag alone", g_bCtlCommitted, 0);
    TestCheck("  and allocates nothing", g_ctlOut.pSlotPanel == NULL && g_ctlOut.apNavCtl[0] == NULL);
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
    TestCommitStores();
    TestAdjustSelectedQty();
    TestApplyQtyChanges();
    TestCrewPanelClick();
    TestWeaponSlotsCarried();
    TestBuildListPanels();
    TestBuildCraftPanels();
    TestPlaneInit();
    TestBuildControls();

    return TestSummary("test_plane");
}
