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

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestSelectWeapon();
    TestUnloadWeaponStores();
    TestLoadWeaponStores();
    TestApplyWeaponSwap();

    return TestSummary("test_plane");
}
