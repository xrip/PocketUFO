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

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestSelectWeapon();
    TestUnloadWeaponStores();

    return TestSummary("test_plane");
}
