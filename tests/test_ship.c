/* test_ship.c - Ship_TickRangeRecover @ 0x45f80, Ship_TickRefuelAtBase @
 * 0x45f04, Ship_TickRangeRecoverConsume @ 0x45fc8. The per-craft-type fuel
 * rates (0/3 -> +1, 1/2/4 -> +2, else 0) and the 1000x supply consumption
 * step are transcribed from the decompilation.
 */
#include "game/pufo_ship.h"
#include "game/pufo_strateg.h"
#include "game/pufo_unit.h"
#include "util/pufo_ptrarray.h"

#include <stdio.h>
#include <string.h>

static int g_nFailures;

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static void TestRangeRecover(void)
{
    printf("Ship_TickRangeRecover\n");
    Ship s;
    memset(&s, 0, sizeof(s));
    s.bAtBase = 1;
    s.uRangeRemain = 100;
    s.uRangeMax = 1000;
    s.uRangeRate = 50;

    Ship_TickRangeRecover(&s);
    CheckEqI32(s.uRangeRemain, 150, "at base adds the rate");
    Ship_TickRangeRecover(&s);
    CheckEqI32(s.uRangeRemain, 200, "recovers every minute");

    s.bNeedsSupply = 1;
    Ship_TickRangeRecover(&s);
    CheckEqI32(s.uRangeRemain, 200, "needsSupply blocks the plain path");

    s.bAtBase = 0;
    s.bNeedsSupply = 0;
    s.uRangeRemain = 1200;
    Ship_TickRangeRecover(&s);
    CheckEqI32(s.uRangeRemain, 1000, "clamp to max is unconditional");
}

static void TestRefuel(void)
{
    printf("Ship_TickRefuelAtBase\n");
    Ship s;
    memset(&s, 0, sizeof(s));
    s.bAtBase = 1;
    s.uFuelMax = 100;
    s.nFuel = 10;

    s.bCraftType = 0;
    Ship_TickRefuelAtBase(&s);
    CheckEqI32(s.nFuel, 11, "craft type 0 refuels +1");
    CheckEqI32(s.bRefueling, 1, "refueling flag set");

    s.bCraftType = 1;
    Ship_TickRefuelAtBase(&s);
    CheckEqI32(s.nFuel, 13, "craft type 1 refuels +2");

    s.bCraftType = 4;
    Ship_TickRefuelAtBase(&s);
    CheckEqI32(s.nFuel, 15, "craft type 4 refuels +2");

    s.bCraftType = 3;
    Ship_TickRefuelAtBase(&s);
    CheckEqI32(s.nFuel, 16, "craft type 3 refuels +1");

    s.bCraftType = 6;
    Ship_TickRefuelAtBase(&s);
    CheckEqI32(s.nFuel, 16, "unknown craft type does not refuel");

    s.bCraftType = 0;
    s.nFuel = 99;
    Ship_TickRefuelAtBase(&s);
    CheckEqI32(s.nFuel, 100, "clamped to uFuelMax");
    CheckEqI32(s.bRefueling, 0, "refueling cleared at full");

    s.bAtBase = 0;
    s.nFuel = 5;
    Ship_TickRefuelAtBase(&s);
    CheckEqI32(s.nFuel, 5, "away from base does not refuel");
}

/* --- supply recording --- */

static i32 g_nMsgHome, g_nMsgSlot, g_nMsgCount;

static void Hook_LowSupply(i32 nHome, i32 nSlot, void *pUser)
{
    (void)pUser;
    ++g_nMsgCount;
    g_nMsgHome = nHome;
    g_nMsgSlot = nSlot;
}

/* Ship_TickRangeRecoverConsume @ 0x45fc8 reads the gate at
 * apObjects[105]->auPerBaseVal[bHomeBase] while BaseRec_SubItemQty @ 0x5571c
 * writes auPerBaseVal[apBases[bHomeBase]->bSlot]. Two different indices.
 * BaseRec_ctor @ 0x55650 sets bSlot = gUiRootPanel.anLineAux[79], the base
 * count at the moment the base is created, so in a real game bSlot equals the
 * base's own index and the two slots coincide. Test A pins that normal case;
 * test B pins what happens when they do not. */
static void TestSupplyConsume(void)
{
    printf("Ship_TickRangeRecoverConsume\n");
    BaseRec base;
    ObjectRecFull item;
    memset(&base, 0, sizeof(base));
    memset(&item, 0, sizeof(item));
    base.bSlot = 0; /* == bHomeBase, as BaseRec_ctor makes it */
    base.nStoresTotal = 500;
    item.auPerBaseVal[0] = 30;

    u8 bLowFlag = 0;
    BaseRec *apBases[1] = { &base };
    ShipSupplyHooks hooks = { apBases, &item, &bLowFlag, Hook_LowSupply, 0 };

    Ship s;
    memset(&s, 0, sizeof(s));
    s.bAtBase = 1;
    s.bNeedsSupply = 1;
    s.bHomeBase = 0;
    s.nSlotIdx = 3;
    s.uRangeRemain = 100;
    s.uRangeMax = 20000;
    s.uRangeRate = 10;

    Ship_TickRangeRecoverConsume(&s, &hooks);
    CheckEqI32(s.uRangeRemain, 100 + 1000 * 10, "consumes supply for 1000x rate");
    CheckEqI32(item.auPerBaseVal[0], 20, "base stock reduced by the rate");
    CheckEqI32(base.nStoresTotal, 490, "base total reduced too");
    CheckEqI32(g_nMsgCount, 0, "no low-supply message while stock is enough");

    /* Next tick: stock 20 > 10 consumes, leaving 10, which is NOT > rate
     * afterwards -> 10 < rate is false (10 < 10 == false), no message. */
    Ship_TickRangeRecoverConsume(&s, &hooks);
    CheckEqI32(item.auPerBaseVal[0], 10, "second consumption");
    CheckEqI32(g_nMsgCount, 0, "stock == rate does not warn");

    /* Stock 10 is not > rate 10 -> no consumption at all. The ship is back
     * below uRangeMax first, so it is the gate that blocks and not the clamp. */
    s.uRangeRemain = 0;
    Ship_TickRangeRecoverConsume(&s, &hooks);
    CheckEqI32(item.auPerBaseVal[0], 10, "stock == rate blocks consumption");

    /* Make stock below rate reachable: rate 11 with stock 10 blocks too
     * (needs strictly greater). Drop stock to 12: consume 11 -> 1 < 11 -> warn. */
    s.uRangeRemain = 0;
    item.auPerBaseVal[0] = 12;
    s.uRangeRate = 11;
    Ship_TickRangeRecoverConsume(&s, &hooks);
    CheckEqI32(item.auPerBaseVal[0], 1, "consumed down to 1");
    CheckEqI32(g_nMsgCount, 1, "low-supply message fired");
    CheckEqI32(bLowFlag, 1, "low-supply flag set");
    CheckEqI32(g_nMsgHome, 0, "message carries the home base");
    CheckEqI32(g_nMsgSlot, 3, "message carries the slot index");

    /* Not needing supply -> untouched. */
    s.bNeedsSupply = 0;
    item.auPerBaseVal[0] = 50;
    Ship_TickRangeRecoverConsume(&s, &hooks);
    CheckEqI32(item.auPerBaseVal[0], 50, "no consumption without bNeedsSupply");

    /* Gate slot and stock slot are different indices in the original. With
     * bSlot != bHomeBase the gate never moves, so the low-supply branch is
     * unreachable even while the stock slot drains below the rate. */
    printf("Ship_TickRangeRecoverConsume, bSlot != bHomeBase\n");
    memset(&base, 0, sizeof(base));
    memset(&item, 0, sizeof(item));
    memset(&bLowFlag, 0, sizeof(bLowFlag));
    g_nMsgCount = 0;
    base.bSlot = 1;
    base.nStoresTotal = 500;
    item.auPerBaseVal[0] = 30; /* gate, read as auPerBaseVal[bHomeBase] */
    item.auPerBaseVal[1] = 25; /* stock, written as auPerBaseVal[bSlot] */
    s.bNeedsSupply = 1;
    s.uRangeRate = 10;
    s.uRangeRemain = 0;

    Ship_TickRangeRecoverConsume(&s, &hooks);
    CheckEqI32(item.auPerBaseVal[0], 30, "gate slot is not the written slot");
    CheckEqI32(item.auPerBaseVal[1], 15, "stock slot takes the subtraction");
    Ship_TickRangeRecoverConsume(&s, &hooks);
    CheckEqI32(item.auPerBaseVal[1], 5, "stock drained below the rate");
    CheckEqI32(g_nMsgCount, 0, "low-supply warning unreachable here");
    CheckEqI32(bLowFlag, 0, "low-supply flag stays clear");
}

static void TestCargoPair(void)
{
    printf("InvSel_AddItemQty / InvSel_SubItemQty\n");
    Ship ship;
    ObjectRecFull item;

    memset(&ship, 0, sizeof(ship));
    memset(&item, 0, sizeof(item));
    ship.nSlotIdx = 3;

    CheckEqI32((int)(InvSel_AddItemQty(&ship, &item, 5) == &ship), 1,
               "both return the ship pointer, not a status");
    CheckEqI32(item.aItemQty[3], 5, "the object's column for this ship is what changes");
    CheckEqI32(item.aItemQty[4], 0, "a neighbouring ship's column is untouched");
    CheckEqI32(ship.uUnk4, 5, "and the craft's own cargo total at +0x04 follows");

    InvSel_SubItemQty(&ship, &item, 2);
    CheckEqI32(item.aItemQty[3], 3, "the subtract pair reverses it");
    CheckEqI32(ship.uUnk4, 3, "in both places");

    /* Two ships with different slots own different columns of the same object. */
    memset(&ship, 0, sizeof(ship));
    memset(&item, 0, sizeof(item));
    ship.nSlotIdx = 0;
    Ship other;
    memset(&other, 0, sizeof(other));
    other.nSlotIdx = 1;
    InvSel_AddItemQty(&ship, &item, 7);
    InvSel_AddItemQty(&other, &item, 11);
    CheckEqI32(item.aItemQty[0], 7, "ship one owns column zero");
    CheckEqI32(item.aItemQty[1], 11, "ship two owns column one");
    CheckEqI32(ship.uUnk4, 7, "each craft totals only its own");
    CheckEqI32(other.uUnk4, 11, "and the other its own");

    /* u16 wrap, reproduced: neither update clamps. */
    memset(&ship, 0, sizeof(ship));
    memset(&item, 0, sizeof(item));
    ship.nSlotIdx = 0;
    ship.uUnk4 = 0;
    InvSel_SubItemQty(&ship, &item, 1);
    CheckEqI32(ship.uUnk4, 65535, "the cargo total wraps rather than floors at zero");

    /* Deviation: an out-of-range slot skips both updates instead of writing off the end. */
    memset(&ship, 0, sizeof(ship));
    memset(&item, 0, sizeof(item));
    ship.nSlotIdx = 52;
    ship.uUnk4 = 9;
    InvSel_AddItemQty(&ship, &item, 4);
    CheckEqI32(ship.uUnk4, 9, "an out-of-range slot writes nothing at all");
    CheckEqI32(item.aItemQty[0], 0, "and the object is left alone");
}

/* ------------------------------------------------- losing a craft, cargo and all */

static void TestEqI32Local(const char *pszWhat, i32 nGot, i32 nWant)
{
    CheckEqI32(nGot, nWant, pszWhat);
}

static void TestShipRemoveWithCargo(void)
{
    Ship *apShip[3];
    static Ship *g_apCraft[3];
    Soldier *apSoldier[4];
    static Soldier *g_apSoldier[4];
    static ObjectRecFull g_aObject[2];
    static ObjectRecFull *g_apObject[2];
    StrategItem *apItem[2];
    void *apStrateg[2];
    static u8 g_abHangar[256];
    static i32 g_anAssigned[4];
    static PtrArray list;
    static GeoscapeWorld world;
    static StrategSpawnCtx ctx;
    static i32 nSeed;
    ShipRemovalEnv env;
    u8 nShipCount;
    u16 nSoldierCount;
    i32 nAssignedCount;
    u8 once;

    printf("Ship_RemoveWithCargo\n");

    /* Everything the teardown deletes has to be a heap object, because the original
     * operator-deletes it and the port frees it. */
    for (int i = 0; i < 3; ++i) {
        apShip[i] = (Ship *)calloc(1, sizeof(Ship));
        apShip[i]->bId = (u8)(10 + i);
        apShip[i]->nSlotIdx = i;
        g_apCraft[i] = apShip[i];
    }
    apShip[1]->bHomeBase = 0;
    apShip[1]->bHangarX = 3;
    apShip[1]->bHangarY = 0;

    for (int i = 0; i < 4; ++i) {
        apSoldier[i] = (Soldier *)calloc(1, sizeof(Soldier));
        apSoldier[i]->nUnk440 = 100 + i;
        g_apSoldier[i] = apSoldier[i];
    }
    apSoldier[0]->bUnk183 = 0;
    apSoldier[1]->bUnk183 = 1;   /* on the doomed craft */
    apSoldier[2]->bUnk183 = 2;
    apSoldier[3]->bUnk183 = 255; /* not assigned to anything */

    for (int i = 0; i < 2; ++i) {
        apItem[i] = (StrategItem *)calloc(1, sizeof(StrategItem));
        apStrateg[i] = apItem[i];
    }
    apItem[0]->nType = 0;
    apItem[1]->nType = 1;
    apItem[1]->nDisplayId = 11;

    for (int i = 0; i < 2; ++i) {
        g_apObject[i] = &g_aObject[i];
        for (int s = 0; s < 3; ++s) {
            g_aObject[i].aItemQty[s] = (u16)(i * 10 + s + 5);
        }
    }

    list.nCount = 2;
    /* PtrArray_DeleteAt reallocs the item array, so it has to be a heap array. */
    list.apItems = (void **)calloc(2, sizeof(void *));
    list.apItems[0] = apItem[0];
    list.apItems[1] = apItem[1];

    memset(g_abHangar, 0xEE, sizeof(g_abHangar));
    g_anAssigned[0] = 0;
    g_anAssigned[1] = 2;
    g_anAssigned[2] = 1;
    g_anAssigned[3] = 255;

    memset(&world, 0, sizeof(world));
    world.apCraft = g_apCraft;
    world.nShipCount = 3;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnSeed = &nSeed;
    once = 0;

    nShipCount = 3;
    nSoldierCount = 4;
    nAssignedCount = 4;

    memset(&env, 0, sizeof(env));
    env.apCraft = g_apCraft;
    env.pnShipCount = &nShipCount;
    env.apSoldiers = g_apSoldier;
    env.pnSoldierCount = &nSoldierCount;
    env.apObjects = g_apObject;
    env.nObjectCount = 2;
    env.pStrategList = &list;
    env.pWorld = &world;
    env.pnSpawnOnceFlag = &once;
    env.pSpawnCtx = &ctx;
    env.abHangarFlag = g_abHangar;
    env.nHangarFlagCap = (i32)sizeof(g_abHangar);
    env.anAssignedShip = g_anAssigned;
    env.pnAssignedShipCount = &nAssignedCount;

    /* The hangar cell this craft occupies: 6*(24*0 + 0 + 12) + 3 = 75. */
    g_abHangar[75] = 9;


    i32 nResult = Ship_RemoveWithCargo(&env, 11);

    TestEqI32Local("the count after the removal is two", nResult, 2);
    TestEqI32Local("and the count byte agrees", (i32)nShipCount, 2);

    /* Struct copy compaction: slot 1 now holds what slot 2 held, which is id 12. */
    TestEqI32Local("slot 0 still holds id 10", (i32)g_apCraft[0]->bId, 10);
    TestEqI32Local("slot 1 was overwritten by slot 2", (i32)g_apCraft[1]->bId, 12);

    /* The cargo column shifts left with the ships. */
    TestEqI32Local("object 0 column 1 took column 2", (i32)g_aObject[0].aItemQty[1], 7);
    TestEqI32Local("object 1 column 1 took column 2", (i32)g_aObject[1].aItemQty[1], 17);
    TestEqI32Local("and column 0 was left alone", (i32)g_aObject[0].aItemQty[0], 5);

    /* Crew: the soldier on the doomed craft is gone, the one above it follows the gap down. */
    TestEqI32Local("one soldier was removed", (i32)nSoldierCount, 3);
    TestEqI32Local("the unassigned sentinel is untouched", (i32)g_apSoldier[2]->bUnk183, 255);
    TestEqI32Local("the soldier below the gap keeps its index", (i32)g_apSoldier[0]->bUnk183, 0);
    TestEqI32Local("the soldier above the gap moved down", (i32)g_apSoldier[1]->bUnk183, 1);

    /* The strateg entry carrying the craft is the one with nType 1 and the matching id. */
    TestEqI32Local("one strateg item was removed", (i32)list.nCount, 1);
    TestEqI32Local("and the base entry survived", (i32)((StrategItem *)list.apItems[0])->nType, 0);

    /* Hangar flag. */
    TestEqI32Local("the craft's hangar cell is cleared", (i32)g_abHangar[75], 0);
    TestEqI32Local("its neighbour is not", (i32)g_abHangar[76], 0xEE);

    /* The assigned list has no sentinel test: 255 above the gap becomes 254. */
    TestEqI32Local("assigned 0 stays", g_anAssigned[0], 0);
    TestEqI32Local("assigned 2 moves to 1", g_anAssigned[1], 1);
    TestEqI32Local("assigned 1 is not above the gap so it stays", g_anAssigned[2], 1);
    TestEqI32Local("and 255 is decremented too", g_anAssigned[3], 254);

    /* Slots are renumbered over the remaining count. */
    TestEqI32Local("slot 0 keeps index 0", g_apCraft[0]->nSlotIdx, 0);
    TestEqI32Local("slot 1 is renumbered to 1", g_apCraft[1]->nSlotIdx, 1);

    /* An unknown id is refused rather than deleting past the end. */
    nResult = Ship_RemoveWithCargo(&env, 99);
    TestEqI32Local("an unknown id leaves the count alone", nResult, 2);
    TestEqI32Local("and nothing is deleted", (i32)nShipCount, 2);

    /* What the teardown already released: the last craft object, the crew member on it,
     * and the strateg entry that carried it. Freeing those again would be a double free. */
    free(apShip[0]);
    free(apShip[1]);
    free(apSoldier[0]);
    free(apSoldier[2]);
    free(apSoldier[3]);
    free(apItem[0]);
    free(list.apItems);
}


/* ------------------------------------------------- the cargo hold */

static void TestCargoHold(void)
{
    Ship ship;
    static ObjectRecFull g_aObj[53];
    static ObjectRecFull *g_apObj[53];

    printf("Craft_GetTypeCargoCap\n");

    memset(&ship, 0, sizeof(ship));
    for (u8 t = 0; t <= 5; ++t) {
        ship.bCraftType = t;
        i32 nCap = (i32)Craft_GetTypeCargoCap(&ship);
        switch (t) {
        case 0:  TestEqI32Local("craft type 0 holds 140", nCap, 140); break;
        case 1:  TestEqI32Local("craft type 1 holds 120", nCap, 120); break;
        case 2:  TestEqI32Local("craft type 2 holds 260", nCap, 260); break;
        case 3:  TestEqI32Local("craft type 3 holds nothing", nCap, 0); break;
        case 4:  TestEqI32Local("craft type 4 holds nothing", nCap, 0); break;
        default: TestEqI32Local("an unknown craft type reports -1, not a cap", nCap, -1); break;
        }
    }
    TestEqI32Local("no ship at all is the same as an unknown type",
                   (i32)Craft_GetTypeCargoCap(NULL), -1);

    printf("Base_CalcWeaponStorageUsed\n");

    /* Every object carries ten units of size one. Only the weapon range 1..24 and the
     * ammo range 36..51 are charged, so 40 objects contribute. */
    for (int i = 0; i < 53; ++i) {
        g_apObj[i] = &g_aObj[i];
        g_aObj[i].bCargoSize = 1;
        g_aObj[i].aItemQty[0] = 10;
    }
    TestEqI32Local("the weapon and ammo ranges fill the hold",
                   Base_CalcWeaponStorageUsed(g_apObj, 53, 0), 400);

    /* The ranges the original leaves out: object 0, 25..36, and 52. Give them a size that
     * would be impossible to miss if they were counted. */
    g_aObj[0].bCargoSize = 100;
    for (int i = 25; i <= 35; ++i) {
        g_aObj[i].bCargoSize = 100;
    }
    g_aObj[52].bCargoSize = 100;
    TestEqI32Local("object 0, 25..35 and 52 are never charged",
                   Base_CalcWeaponStorageUsed(g_apObj, 53, 0), 400);

    /* The charge is size times units, not a unit count. */
    memset(g_aObj, 0, sizeof(g_aObj));
    for (int i = 0; i < 53; ++i) {
        g_apObj[i] = &g_aObj[i];
    }
    g_aObj[1].bCargoSize = 2;
    g_aObj[1].aItemQty[0] = 3;   /* 6 */
    g_aObj[24].bCargoSize = 5;
    g_aObj[24].aItemQty[0] = 1;  /* 5 */
    g_aObj[37].bCargoSize = 4;
    g_aObj[37].aItemQty[0] = 10; /* 40 */
    g_aObj[51].bCargoSize = 7;
    g_aObj[51].aItemQty[0] = 2;  /* 14 */
    TestEqI32Local("each object is charged its size times what it carries",
                   Base_CalcWeaponStorageUsed(g_apObj, 53, 0), 65);

    /* A different craft column reads a different load. */
    g_aObj[1].aItemQty[7] = 9;
    TestEqI32Local("the column is the craft index",
                   Base_CalcWeaponStorageUsed(g_apObj, 53, 7), 18);

    /* An empty hold, and a count that leaves nothing to walk. */
    TestEqI32Local("one object means nothing is counted",
                   Base_CalcWeaponStorageUsed(g_apObj, 1, 0), 0);
    TestEqI32Local("no objects at all means nothing is counted",
                   Base_CalcWeaponStorageUsed(g_apObj, 0, 0), 0);
    TestEqI32Local("an out-of-range craft index is refused",
                   Base_CalcWeaponStorageUsed(g_apObj, 53, 60), 0);
}


/* ------------------------------------------------- refilling a craft from its base */

static void TestLoadStores(void)
{
    static Ship g_aShip[1];
    static Ship *g_apCraft[1];
    static BaseRec g_aBase[1];
    static BaseRec *g_apBase[1];
    static ObjectRecFull g_aObj[53];
    static ObjectRecFull *g_apObj[53];
    CraftStoreLoadEnv env;

    printf("Craft_LoadStoresFromBase\n");

    memset(g_aShip, 0, sizeof(g_aShip));
    memset(g_aBase, 0, sizeof(g_aBase));
    memset(g_aObj, 0, sizeof(g_aObj));
    g_aShip[0].bCraftType = 0;      /* hold 140 */
    g_aShip[0].bHomeBase = 0;
    g_aShip[0].nSlotIdx = 0;
    g_aBase[0].bSlot = 0;
    g_apCraft[0] = &g_aShip[0];
    g_apBase[0] = &g_aBase[0];
    for (int i = 0; i < 53; ++i) {
        g_apObj[i] = &g_aObj[i];
        g_aObj[i].bCargoSize = 1;
    }

    memset(&env, 0, sizeof(env));
    env.apCraft = g_apCraft;
    env.apBases = g_apBase;
    env.apObjects = g_apObj;
    env.nObjectCount = 53;

    /* One weapon object, one ammo object. The craft carries five cannons, the base has
     * three rounds, so three rounds move and no more. */
    g_aObj[1].aItemQty[0] = 5;
    g_aObj[36].auPerBaseVal[0] = 3;
    g_aBase[0].nStoresTotal = 10;

    Craft_LoadStoresFromBase(&env, 1, 36, 0);

    TestEqI32Local("the craft took three rounds", (i32)g_aObj[36].aItemQty[0], 3);
    TestEqI32Local("and the base is out", (i32)g_aObj[36].auPerBaseVal[0], 0);
    TestEqI32Local("the base's store total dropped by three", (i32)g_aBase[0].nStoresTotal, 7);
    TestEqI32Local("the craft's cargo total rose by three", (i32)g_aShip[0].uUnk4, 3);

    /* Nothing to take. */
    Craft_LoadStoresFromBase(&env, 1, 36, 0);
    TestEqI32Local("an empty base loads nothing", (i32)g_aObj[36].aItemQty[0], 3);

    /* Three pools, split in order: 4 from the first, 3 from the second, the last 3 from
     * the third even though it has twenty. */
    memset(g_aObj, 0, sizeof(g_aObj));
    for (int i = 0; i < 53; ++i) {
        g_aObj[i].bCargoSize = 1;
    }
    g_aObj[7].aItemQty[0] = 10;
    g_aObj[40].auPerBaseVal[0] = 4;
    g_aObj[41].auPerBaseVal[0] = 3;
    g_aObj[42].auPerBaseVal[0] = 20;

    Craft_LoadStoresFromBase(&env, 7, 0, 0);

    TestEqI32Local("first pool gave all four it had", (i32)g_aObj[40].aItemQty[0], 4);
    TestEqI32Local("second pool gave all three it had", (i32)g_aObj[41].aItemQty[0], 3);
    TestEqI32Local("third pool gave only the remainder", (i32)g_aObj[42].aItemQty[0], 3);
    TestEqI32Local("and kept the other seventeen", (i32)g_aObj[42].auPerBaseVal[0], 17);

    /* A craft whose hold cap is zero takes nothing at all, because the gate is
     * used + size < cap and nothing is under zero. */
    memset(g_aObj, 0, sizeof(g_aObj));
    for (int i = 0; i < 53; ++i) {
        g_aObj[i].bCargoSize = 1;
    }
    g_aShip[0].bCraftType = 3;
    g_aObj[1].aItemQty[0] = 5;
    g_aObj[36].auPerBaseVal[0] = 3;

    Craft_LoadStoresFromBase(&env, 1, 36, 0);
    TestEqI32Local("a craft with no hold loads nothing", (i32)g_aObj[36].aItemQty[0], 0);
    TestEqI32Local("and the base keeps its stock", (i32)g_aObj[36].auPerBaseVal[0], 3);

    /* An out-of-range craft index is refused. */
    Craft_LoadStoresFromBase(&env, 1, 36, 60);
    TestEqI32Local("an out-of-range craft index loads nothing", (i32)g_aObj[36].aItemQty[0], 0);
}


/* ------------------------------------------------- dumping cargo home and refilling */

static void TestStripCraftCargo(void)
{
    static Ship g_aShip[2];
    static Ship *g_apCraft[2];
    static BaseRec g_aBase[1];
    static BaseRec *g_apBase[1];
    static ObjectRecFull g_aObj[53];
    static ObjectRecFull *g_apObj[53];
    CraftStoreLoadEnv env;

    printf("Base_StripAllCraftCargo\n");

    memset(g_aShip, 0, sizeof(g_aShip));
    memset(g_aBase, 0, sizeof(g_aBase));
    memset(g_aObj, 0, sizeof(g_aObj));
    g_aShip[0].bCanDogfight = 1;
    g_aShip[0].bCraftType = 0;   /* hold 140 */
    g_aShip[0].bHomeBase = 0;
    g_aShip[0].nSlotIdx = 0;
    g_aShip[1].bCanDogfight = 0; /* not stripped at all */
    g_aShip[1].bCraftType = 0;
    g_aShip[1].bHomeBase = 0;
    g_aShip[1].nSlotIdx = 1;
    g_aBase[0].bSlot = 0;
    g_apCraft[0] = &g_aShip[0];
    g_apCraft[1] = &g_aShip[1];
    g_apBase[0] = &g_aBase[0];
    for (int i = 0; i < 53; ++i) {
        g_apObj[i] = &g_aObj[i];
        g_aObj[i].bCargoSize = 1;
    }

    /* Group 1: five cannons, two rounds aboard, one round in the base. The gate is
     * 2 < 5, so the two rounds go home (base becomes three) and the craft pulls back
     * min(5, 3) = three. */
    g_aObj[1].aItemQty[0] = 5;
    g_aObj[36].aItemQty[0] = 2;
    g_aObj[36].auPerBaseVal[0] = 1;

    /* Group 3: one weapon, four rounds. 4 < 1 is false, so this group is left alone. */
    g_aObj[3].aItemQty[0] = 1;
    g_aObj[37].aItemQty[0] = 4;
    g_aObj[37].auPerBaseVal[0] = 2;

    /* Group 7 (three pools): six weapons, one round in each of the first two pools. */
    g_aObj[7].aItemQty[0] = 6;
    g_aObj[40].aItemQty[0] = 1;
    g_aObj[40].auPerBaseVal[0] = 1;
    g_aObj[41].aItemQty[0] = 1;
    g_aObj[41].auPerBaseVal[0] = 1;
    g_aObj[42].auPerBaseVal[0] = 5;

    /* The second craft carries the same kind of load but cannot dogfight, so it must be
     * skipped outright. Its column is 1. */
    g_aObj[1].aItemQty[1] = 5;
    g_aObj[36].aItemQty[1] = 2;
    g_aObj[36].auPerBaseVal[1] = 1;

    memset(&env, 0, sizeof(env));
    env.apCraft = g_apCraft;
    env.apBases = g_apBase;
    env.apObjects = g_apObj;
    env.nObjectCount = 53;

    Base_StripAllCraftCargo(&env, 2);

    TestEqI32Local("the craft ended with three rounds, one per weapon it can supply",
                   (i32)g_aObj[36].aItemQty[0], 3);
    TestEqI32Local("and the base is out of them", (i32)g_aObj[36].auPerBaseVal[0], 0);

    TestEqI32Local("a group that already has enough is left alone",
                   (i32)g_aObj[37].aItemQty[0], 4);
    TestEqI32Local("and its base stock is untouched", (i32)g_aObj[37].auPerBaseVal[0], 2);

    /* Three pools: 1+1 carried against 6 weapons, so both go home (each base pool becomes 2)
     * and the split pulls 2 + 2 + 2 back. */
    TestEqI32Local("first pool gave its whole stock", (i32)g_aObj[40].aItemQty[0], 2);
    TestEqI32Local("and is empty at base", (i32)g_aObj[40].auPerBaseVal[0], 0);
    TestEqI32Local("second pool gave its whole stock", (i32)g_aObj[41].aItemQty[0], 2);
    TestEqI32Local("and is empty at base", (i32)g_aObj[41].auPerBaseVal[0], 0);
    TestEqI32Local("third pool covered the remainder", (i32)g_aObj[42].aItemQty[0], 2);
    TestEqI32Local("and kept three at base", (i32)g_aObj[42].auPerBaseVal[0], 3);

    /* The craft without bCanDogfight keeps everything it had. */
    TestEqI32Local("a craft that cannot dogfight keeps its ammo",
                   (i32)g_aObj[36].aItemQty[1], 2);
    TestEqI32Local("and its base stock is untouched", (i32)g_aObj[36].auPerBaseVal[1], 1);
}


static void TestCraftWeaponItemId(void)
{
    static ObjectRecFull g_aObj[106];
    static ObjectRecFull *g_apObj[106];

    printf("Base_CraftWeaponItemId\n");

    for (int i = 0; i < 106; ++i) {
        memset(&g_aObj[i], 0, sizeof(g_aObj[i]));
        g_apObj[i] = &g_aObj[i];
    }
    g_aObj[56].auPerBaseVal[3] = 11;
    g_aObj[57].auPerBaseVal[3] = 22;
    g_aObj[58].auPerBaseVal[3] = 33;
    g_aObj[105].auPerBaseVal[3] = 44;

    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 31, 3), 11, "slot 31 reads object record 56");
    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 32, 3), 22, "slot 32 reads 57");
    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 34, 3), 44, "slot 34 reads 105");
    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 35, 3), 33, "slot 35 reads 58");

    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 33, 3), 1000,
               "slot 33 is a constant, with no object record touched");
    g_aObj[56].auPerBaseVal[3] = 999;
    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 33, 3), 1000,
               "and changing every record leaves slot 33 alone");

    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 30, 3), -1, "slot 30 is not a craft stores slot");
    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 36, 3), -1, "and neither is slot 36");

    /* The column is the signed byte 0x159D81 at the call site, and there are eight columns. */
    g_aObj[56].auPerBaseVal[7] = 77;
    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 31, 7), 77, "the eighth column is reachable");
    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 31, 8), -1,
               "a column past the eight is refused, where the original reads on");
    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 31, -1), -1, "and so is a negative one");

    g_apObj[57] = NULL;
    CheckEqI32(Base_CraftWeaponItemId(g_apObj, 32, 3), -1,
               "an empty object record gives -1, where the original faults");
    g_apObj[57] = &g_aObj[57];

    CheckEqI32(Base_CraftWeaponItemId(NULL, 31, 3), -1, "no object array at all gives -1");
}

static void TestCrewAndStoresTotals(void)
{
    static Soldier g_aSol[4];
    static Soldier *g_apSol[4];
    static ObjectRecFull g_aObj[3];
    static ObjectRecFull *g_apObj[3];

    printf("Craft_CountSoldiersAboard and BasePlane_CalcWeaponStoresUsed\n");

    for (int i = 0; i < 4; ++i) {
        memset(&g_aSol[i], 0, sizeof(g_aSol[i]));
        g_apSol[i] = &g_aSol[i];
    }
    g_aSol[0].bUnk183 = 255; /* not aboard anything */
    g_aSol[1].bUnk183 = 0;
    g_aSol[2].bUnk183 = 1;
    g_aSol[3].bUnk183 = 254; /* away in a craft */

    CheckEqI32(Craft_CountSoldiersAboard(g_apSol, 4, 0), 1, "one soldier is assigned to craft 0");
    CheckEqI32(Craft_CountSoldiersAboard(g_apSol, 4, 1), 1, "and one to craft 1");
    CheckEqI32(Craft_CountSoldiersAboard(g_apSol, 4, 2), 0, "nobody is on craft 2");
    CheckEqI32(Craft_CountSoldiersAboard(g_apSol, 4, 255), 1,
               "there is no sentinel test, so asking for craft 255 counts the unassigned");
    CheckEqI32(Craft_CountSoldiersAboard(g_apSol, 4, 254), 1,
               "and 254 is just another number to this function");

    for (int i = 0; i < 3; ++i) {
        memset(&g_aObj[i], 0, sizeof(g_aObj[i]));
        g_apObj[i] = &g_aObj[i];
    }
    g_aObj[0].bCargoSize = 2;
    g_aObj[1].bCargoSize = 3;
    g_aObj[2].bCargoSize = 1;

    static const i32 anCarried[3] = { 5, 1, 0 };
    CheckEqI32(BasePlane_CalcWeaponStoresUsed(g_apObj, 3, anCarried), 2 * 5 + 3 * 1,
               "carried counts weighted by each object's per-unit size");

    /* Every index is walked, including 0, which Base_CalcWeaponStorageUsed skips. */
    static const i32 anCarried0[3] = { 7, 0, 0 };
    CheckEqI32(BasePlane_CalcWeaponStoresUsed(g_apObj, 3, anCarried0), 14,
               "index 0 counts here, unlike in Base_CalcWeaponStorageUsed");

    CheckEqI32(BasePlane_CalcWeaponStoresUsed(g_apObj, 0, anCarried), 0,
               "a zero object count reads nothing");
    CheckEqI32(BasePlane_CalcWeaponStoresUsed(g_apObj, 3, NULL), 0, "no carried array gives 0");

    g_apObj[1] = NULL;
    CheckEqI32(BasePlane_CalcWeaponStoresUsed(g_apObj, 3, anCarried), 10,
               "an empty object record is skipped, where the original faults");
}

static void TestInterceptRetarget(void)
{
    static Ship *g_apCraft[2];
    static Soldier *g_apSoldier[1];
    static ObjectRecFull *g_apObj2[1];
    static BaseRec *g_apBase[1];
    static BaseRec g_Base;
    static PtrArray list;
    static GeoscapeWorld world;
    static StrategSpawnCtx ctx;
    static i32 nSeed;
    static i32 nTotalSec;
    static u8 g_abHangar[64];
    static i32 g_anAssigned[1];
    ShipRemovalEnv env;
    Ship *apShip[2];
    StrategItem *apItem[3];
    void **apSlots;
    u8 nShipCount;
    u16 nSoldierCount;
    i32 nAssignedCount;
    u8 once;

    printf("Ship_RetargetTravel / ClearInterceptByStrategIdx / ClearWaypointIntercept\n");

    for (int i = 0; i < 2; ++i) {
        apShip[i] = (Ship *)calloc(1, sizeof(Ship));
        apShip[i]->bId = (u8)(10 + i);
        apShip[i]->bHomeBase = 0;
        apShip[i]->nSlotIdx = i;
        apShip[i]->bIntercepting = 1;
        apShip[i]->nStrategIdx = 0;
        g_apCraft[i] = apShip[i];
    }

    /* Item 0 is the waypoint both craft are chasing; item 1 is craft 0's own entry. */
    apItem[0] = (StrategItem *)calloc(1, sizeof(StrategItem));
    apItem[1] = (StrategItem *)calloc(1, sizeof(StrategItem));
    apItem[2] = NULL;
    apItem[0]->nType = 5;
    apItem[0]->nTimer = 1234;
    apItem[1]->nType = 1;
    apItem[1]->bActive = 0;
    apItem[1]->nDisplayId = 10; /* Ship 0's bId */
    apItem[1]->nTimer = 999;
    apItem[1]->nX = 40;
    apItem[1]->nY = 60;

    /* PtrArray_Remove may free and replace apItems, so it has to be heap storage. */
    /* Ship 1 needs its own entry too, or there is nothing for RetargetTravel to send home. */
    apItem[2] = (StrategItem *)calloc(1, sizeof(StrategItem));
    apItem[2]->nType = 1;
    apItem[2]->bActive = 0;
    apItem[2]->nDisplayId = 11; /* Ship 1's bId */
    apItem[2]->nTimer = 555;
    apItem[2]->nX = 40;
    apItem[2]->nY = 60;

    apSlots = (void **)malloc(sizeof(void *) * 4);
    apSlots[0] = apItem[0];
    apSlots[1] = apItem[1];
    apSlots[2] = apItem[2];
    list.nCount = 3;
    list.apItems = apSlots;

    g_Base.nUnk8 = 100;
    g_Base.nUnk12 = 200;
    g_apBase[0] = &g_Base;

    nShipCount = 2;
    nSoldierCount = 0;
    nAssignedCount = 0;
    once = 0; /* no replacement spawn during the test */
    nTotalSec = 0;
    world.apCraft = g_apCraft;
    world.nShipCount = 2;

    env.apCraft = g_apCraft;
    env.pnShipCount = &nShipCount;
    env.apSoldiers = g_apSoldier;
    env.pnSoldierCount = &nSoldierCount;
    env.apObjects = g_apObj2;
    env.nObjectCount = 0;
    env.pStrategList = &list;
    env.pWorld = &world;
    env.pnSpawnOnceFlag = &once;
    env.pSpawnCtx = &ctx;
    env.abHangarFlag = g_abHangar;
    env.nHangarFlagCap = 64;
    env.anAssignedShip = g_anAssigned;
    env.pnAssignedShipCount = &nAssignedCount;
    env.apBases = g_apBase;
    env.pnGameTotalSec = &nTotalSec;
    memset(&ctx, 0, sizeof(ctx));
    nSeed = 0;
    ctx.pnSeed = &nSeed;

    /* ClearWaypointIntercept: the waypoint is a one-shot contact, so it is removed; the craft's
     * own entry has its timer PARKED at 0xFFFF rather than restarted. */
    StrategItem *pEntry = Ship_ClearWaypointIntercept(&env, 0);
    CheckEqI32(pEntry == apItem[1] ? 1 : 0, 1,
               "it hands back the craft's own strateg entry");
    CheckEqI32(apItem[1]->nTimer, 0xFFFF, "the proximity timer is parked, not restarted");
    CheckEqI32(apShip[0]->bIntercepting, 0, "and the intercept flag is cleared");
    CheckEqI32(list.nCount, 2, "the type-5 waypoint was removed from the list");

    /* RetargetTravel on the second craft: same waypoint removal (already gone), timer back to 0,
     * and travel initialised from the entry's position to the home base's coordinates. */
    /* Ship 1's own entry is item 2. Ship 0's entry keeps the parked 0xFFFF from the call above,
     * which is the difference between the two functions in one number. */
    (void)Ship_RetargetTravel(&env, 1);
    CheckEqI32(apItem[2]->nTimer, 0, "RetargetTravel restarts its own entry's timer at zero");
    CheckEqI32(apItem[1]->nTimer, 0xFFFF,
               "and leaves the other craft's parked timer alone - the difference between the two");
    CheckEqI32(apShip[1]->bIntercepting, 0, "and clears the intercept flag");
    CheckEqI32(apItem[2]->nDestX, 100, "the destination is the home base's x");
    CheckEqI32(apItem[2]->nDestY, 200, "and its y");

    /* ClearInterceptByStrategIdx: nothing is intercepting any more, so nothing is retargeted and
     * it just reports the ship count. */
    CheckEqI32(Ship_ClearInterceptByStrategIdx(&env, 0), 2,
               "with no craft intercepting it reports the ship count and changes nothing");

    for (int i = 0; i < 2; ++i) {
        free(apShip[i]);
    }
    for (int i = 0; i < list.nCount; ++i) {
        free(list.apItems[i]);
    }
    free(list.apItems);
}

int main(void)
{
    TestRangeRecover();
    TestRefuel();
    TestSupplyConsume();
    TestCargoPair();
    TestShipRemoveWithCargo();
    TestCargoHold();
    TestLoadStores();
    TestStripCraftCargo();
    TestCraftWeaponItemId();
    TestCrewAndStoresTotals();
    TestInterceptRetarget();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
