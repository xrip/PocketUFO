/* test_ship.c - Ship_TickRangeRecover @ 0x45f80, Ship_TickRefuelAtBase @
 * 0x45f04, Ship_TickRangeRecoverConsume @ 0x45fc8. The per-craft-type fuel
 * rates (0/3 -> +1, 1/2/4 -> +2, else 0) and the 1000x supply consumption
 * step are transcribed from the decompilation.
 */
#include "game/pufo_ship.h"

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

int main(void)
{
    TestRangeRecover();
    TestRefuel();
    TestSupplyConsume();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
