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

int main(void)
{
    TestRangeRecover();
    TestRefuel();
    TestSupplyConsume();
    TestCargoPair();
    TestShipRemoveWithCargo();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
