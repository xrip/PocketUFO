/* test_ship.c - Ship_TickRangeRecover @ 0x45f80, Ship_TickRefuelAtBase @
 * 0x45f04, Ship_TickRangeRecoverConsume @ 0x45fc8. The per-craft-type fuel
 * rates (0/3 -> +1, 1/2/4 -> +2, else 0) and the 1000x supply consumption
 * step are transcribed from the decompilation.
 */
#include "game/pufo_ship.h"
#include "game/pufo_geoscape.h"
#include "game/pufo_strateg.h"
#include "game/pufo_unit.h"
#include "util/pufo_ptrarray.h"
#include "game/pufo_boot.h"    /* PUFO_TEXT_REC_STRIDE, the craft-name records */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void PutStr(char *pszDst, const char *pszSrc)
{
    while (*pszSrc != 0) {
        *pszDst++ = *pszSrc++;
    }
    *pszDst = 0;
}

static void PutTextRec(u8 *pText, i32 nRec, const char *psz)
{
    PutStr((char *)(pText + PUFO_TEXT_REC_STRIDE * nRec), psz);
}

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

    i32 bLowFlag = 0; /* dwActionFail @ 0x1870C8 is written with STR: a dword */
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


/* Base_RemoveAllShips @ 0x66874 */
static void TestRemoveAllShips(void)
{
    static Ship *apCraft[3];
    static BaseRec aBases[2];
    static BaseRec *apBases[2];
    static ObjectRecFull aObjs[1];
    static ObjectRecFull *apObjs[1];
    static u8 abHangar[256];
    static i32 anAssigned[4];
    static PtrArray list;
    static GeoscapeWorld world;
    static StrategSpawnCtx ctx;
    static u8 aGridBytes[8];
    Ship *apShip[3];
    BaseClearShipsEnv env;
    u8 nShipCount;
    i32 nAssignedCount;
    u8 once = 0;
    i32 nSeed = 0;
    int i;

    printf("Base_RemoveAllShips @ 0x66874\n");

    for (i = 0; i < 3; ++i) {
        apShip[i] = (Ship *)calloc(1, sizeof(Ship));
        apShip[i]->bId = (u8)(10 + i);
        apShip[i]->nSlotIdx = i;
        apCraft[i] = apShip[i];
    }
    apShip[0]->bHomeBase = 0;
    apShip[1]->bHomeBase = 0;
    apShip[2]->bHomeBase = 1;

    aBases[0].bUnk4 = 4;
    aBases[1].bUnk4 = 4; /* both bases share a type, which is what the grid-byte lookup keys on */
    apBases[0] = &aBases[0];
    apBases[1] = &aBases[1];

    apObjs[0] = &aObjs[0];
    memset(&list, 0, sizeof(list));
    memset(&world, 0, sizeof(world));
    world.apCraft = apCraft;
    world.nShipCount = 3;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pnSeed = &nSeed;

    nShipCount = 3;
    nAssignedCount = 3;
    anAssigned[0] = 0; anAssigned[1] = 1; anAssigned[2] = 2; anAssigned[3] = 255;

    memset(&env, 0, sizeof(env));
    env.ship.apCraft = apCraft;
    env.ship.pnShipCount = &nShipCount;
    env.ship.apObjects = apObjs;
    env.ship.nObjectCount = 1;
    env.ship.pStrategList = &list;
    env.ship.pWorld = &world;
    env.ship.pnSpawnOnceFlag = &once;
    env.ship.pSpawnCtx = &ctx;
    env.ship.abHangarFlag = abHangar;
    env.ship.nHangarFlagCap = (i32)sizeof(abHangar);
    env.ship.apBases = apBases;
    env.ship.anAssignedShip = anAssigned;
    env.ship.pnAssignedShipCount = &nAssignedCount;
    env.nBaseCount = 2;
    env.abGridBytes = aGridBytes;

    /* 1. the whole list goes, and the list tracks the compaction as it goes. */
    TestEqI32Local("every listed craft is removed", Base_RemoveAllShips(&env, 0), 0);
    TestEqI32Local("the craft count is empty", (i32)nShipCount, 0);
    TestEqI32Local("the list itself is not emptied, only renumbered", nAssignedCount, 3);
    TestEqI32Local("and its last entry followed the three gaps down to 0", anAssigned[2], 0);

    /* 2. nothing listed: the return is Base_CountShipsAtBase, craft at the base plus the
     * 0x159DBC byte for that base's type. */
    for (i = 0; i < 3; ++i) {
        apShip[i] = (Ship *)calloc(1, sizeof(Ship));
        apShip[i]->bId = (u8)(10 + i);
        apCraft[i] = apShip[i];
    }
    apShip[0]->bHomeBase = 0;
    apShip[1]->bHomeBase = 0;
    apShip[2]->bHomeBase = 1;
    nShipCount = 3;
    nAssignedCount = 0;
    aGridBytes[0] = 3;

    TestEqI32Local("two craft at base 0 plus the grid byte 3", Base_RemoveAllShips(&env, 0), 5);
    TestEqI32Local("base 1 has one craft and reads the same byte", Base_RemoveAllShips(&env, 1), 4);
    TestEqI32Local("nothing was removed", (i32)nShipCount, 3);

    /* 3. the byte is found by TYPE: both bases are type 4, so the search answers base 0 and base 1
     * gets base 0's byte. */
    aGridBytes[0] = 0;
    aGridBytes[1] = 9;
    TestEqI32Local("base 1 sees base 0's byte, not its own", Base_RemoveAllShips(&env, 1), 1);

    /* 4. deviation: an entry that is not a valid craft index is skipped, where the original
     * dereferences apCraft[entry]. */
    nAssignedCount = 1;
    anAssigned[0] = 5;
    TestEqI32Local("an out-of-range entry removes nothing", Base_RemoveAllShips(&env, 0), 2);
    TestEqI32Local("and the count is unchanged", (i32)nShipCount, 3);

    /* 5. a null env answers zero rather than faulting. */
    TestEqI32Local("a null env removes nothing", Base_RemoveAllShips(NULL, 0), 0);

    /* The removals already freed the craft objects they dropped. */
    for (i = 0; i < 3; ++i) {
        if (nShipCount > i) {
            free(apShip[i]);
        }
    }
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

static void TestDistributeRecoveredItems(void)
{
    static ObjectRecFull g_aObj[106];
    static ObjectRecFull *g_apObj[106];
    static BaseRec g_aBase[2];
    static BaseRec *g_apBase[2];
    static Ship g_aShip[2];
    static Ship *g_apShip[2];
    BaseRecoverDistributeEnv env;
    u8 nCraftCount, nBaseCount;

    printf("Base_DistributeRecoveredItems @ 0x68f2c\n");

    memset(&env, 0, sizeof(env));
    env.apCraft = g_apShip; env.pnCraftCount = &nCraftCount;
    env.apBases = g_apBase; env.pbBaseCount = &nBaseCount;
    env.apObjects = g_apObj; env.nObjectCount = 106;

    /* Clears everything and parks one craft at base 0 with no weapons; the caller then sets the
     * mounts and the stock it wants and runs the pass. */
#define DIST_RESET()                                                          \
    for (i32 i = 0; i < 106; ++i) {                                           \
        memset(&g_aObj[i], 0, sizeof(g_aObj[i]));                             \
        g_apObj[i] = &g_aObj[i];                                              \
    }                                                                         \
    memset(g_aBase, 0, sizeof(g_aBase));                                      \
    g_aBase[0].bSlot = 0; g_aBase[1].bSlot = 1;                               \
    g_apBase[0] = &g_aBase[0]; g_apBase[1] = &g_aBase[1];                     \
    memset(g_aShip, 0, sizeof(g_aShip));                                      \
    g_apShip[0] = &g_aShip[0]; g_apShip[1] = &g_aShip[1];                     \
    g_aShip[0].bAtBase = 1; g_aShip[0].bHomeBase = 0;                          \
    nCraftCount = 1; nBaseCount = 1;

    /* Craft cannon: 7 rounds in, the standard 10 back out, the base pays the difference. */
    DIST_RESET();
    g_aShip[0].bWeaponL = 25;
    g_aShip[0].bAmmo25 = 7;
    g_aObj[52].auPerBaseVal[0] = 100;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("the Craft cannon is re-supplied with 10 rounds", (i32)g_aShip[0].bAmmo25, 10);
    TestEqI32Local("the base paid 10 and got 7", (i32)g_aObj[52].auPerBaseVal[0], 97);
    TestEqI32Local("and its stores total follows", (i32)g_aBase[0].nStoresTotal, -3);

    /* Twin mounts double the load -- and the second pass reads the byte the first one refilled. */
    DIST_RESET();
    g_aShip[0].bWeaponL = 26;
    g_aShip[0].bWeaponR = 26;
    g_aShip[0].bAmmo26 = 4;
    g_aObj[53].auPerBaseVal[0] = 100;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("twin Stingray launchers carry 2 * 3", (i32)g_aShip[0].bAmmo26, 6);
    TestEqI32Local("the second pass hands the refilled 6 back too",
                   (i32)g_aObj[53].auPerBaseVal[0], 98);
    TestEqI32Local("and the net across both passes is the 2 the craft gained",
                   (i32)g_aBase[0].nStoresTotal, -2);

    /* A base that cannot cover the standard load gives what it has and its count goes to zero. */
    DIST_RESET();
    g_aShip[0].bWeaponL = 27;
    g_aObj[54].auPerBaseVal[0] = 1;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("the Avalanche mount takes the single round the base had",
                   (i32)g_aShip[0].bAmmo27, 1);
    TestEqI32Local("and the base's count is cleared (0x69150)", (i32)g_aObj[54].auPerBaseVal[0], 0);
    TestEqI32Local("with no subtraction on the short path", (i32)g_aBase[0].nStoresTotal, 0);

    /* Elerium travels in tenths: 35 in the byte is 3 to the base, 10 carried is 100 in the byte. */
    DIST_RESET();
    g_aShip[0].bWeaponL = 29;
    g_aShip[0].bAmmo29 = 35;
    g_aObj[105].auPerBaseVal[0] = 100;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("the Plasma cannon's byte is 10 * 10", (i32)g_aShip[0].bAmmo29, 100);
    TestEqI32Local("35 / 10 went in and 10 came out", (i32)g_aObj[105].auPerBaseVal[0], 93);
    TestEqI32Local("so the net is 7", (i32)g_aBase[0].nStoresTotal, -7);

    DIST_RESET();
    g_aShip[0].bWeaponL = 29;
    g_aObj[105].auPerBaseVal[0] = 5;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("a short Elerium base still writes tenths", (i32)g_aShip[0].bAmmo29, 50);
    TestEqI32Local("and clears its count", (i32)g_aObj[105].auPerBaseVal[0], 0);

    /* The mounts with no rule are left alone: 28 is the Laser cannon, 24 the Psi-arm. */
    DIST_RESET();
    g_aShip[0].bWeaponL = 28;
    g_aShip[0].bWeaponR = 28;
    g_aObj[52].auPerBaseVal[0] = 50;
    g_aObj[105].auPerBaseVal[0] = 50;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("the Laser cannon is not resupplied", (i32)g_aShip[0].bAmmo25, 0);
    TestEqI32Local("and no ammo record moves", (i32)g_aObj[52].auPerBaseVal[0], 50);
    TestEqI32Local("not even Elerium", (i32)g_aObj[105].auPerBaseVal[0], 50);

    /* The gate: away, or homed elsewhere. */
    DIST_RESET();
    g_aShip[0].bAtBase = 0;
    g_aShip[0].bWeaponL = 25;
    g_aShip[0].bAmmo25 = 7;
    g_aObj[52].auPerBaseVal[0] = 100;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("a craft that is not at base keeps its ammo (0x68FC0)",
                   (i32)g_aShip[0].bAmmo25, 7);
    TestEqI32Local("and the base is untouched", (i32)g_aObj[52].auPerBaseVal[0], 100);

    DIST_RESET();
    g_aShip[0].bHomeBase = 1;
    g_aShip[0].bWeaponL = 25;
    g_aShip[0].bAmmo25 = 7;
    g_aObj[52].auPerBaseVal[0] = 100;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("a craft homed at another base is skipped with one base in the roster",
                   (i32)g_aShip[0].bAmmo25, 7);

    /* Homed at base 1, and base 1 is in the roster: it is served from base 1's own column. */
    DIST_RESET();
    nBaseCount = 2;
    g_aShip[0].bHomeBase = 1;
    g_aShip[0].bWeaponL = 25;
    g_aShip[0].bAmmo25 = 7;
    g_aObj[52].auPerBaseVal[1] = 100;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("the second base serves its own craft", (i32)g_aShip[0].bAmmo25, 10);
    TestEqI32Local("from its own column", (i32)g_aObj[52].auPerBaseVal[1], 97);
    TestEqI32Local("and the first base's column is untouched", (i32)g_aObj[52].auPerBaseVal[0], 0);

    /* Counts. */
    DIST_RESET();
    nBaseCount = 0;
    g_aShip[0].bWeaponL = 25;
    g_aShip[0].bAmmo25 = 7;
    g_aObj[52].auPerBaseVal[0] = 100;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("no bases means no resupply (0x68F58)", (i32)g_aShip[0].bAmmo25, 7);

    DIST_RESET();
    nCraftCount = 0;
    g_aShip[0].bWeaponL = 25;
    g_aShip[0].bAmmo25 = 7;
    g_aObj[52].auPerBaseVal[0] = 100;
    Base_DistributeRecoveredItems(&env);
    TestEqI32Local("no craft means no resupply", (i32)g_aShip[0].bAmmo25, 7);

    Base_DistributeRecoveredItems(NULL);
    DIST_RESET();
    g_aShip[0].bWeaponL = 25;
    g_aShip[0].bAmmo25 = 7;
    g_aObj[52].auPerBaseVal[0] = 100;
    env.apObjects = NULL;
    Base_DistributeRecoveredItems(&env);
    env.apObjects = g_apObj;
    TestEqI32Local("a null object table does nothing instead of faulting",
                   (i32)g_aShip[0].bAmmo25, 7);
#undef DIST_RESET
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

static void TestLaunchIntercept(void)
{
    static Ship *apCraftL[1];
    static BaseRec *apBaseL[1];
    static BaseRec baseL;
    static PtrArray listL;
    static GeoscapeWorld worldL;
    static StrategSpawnCtx ctxL;
    static ObjectRecFull *apObjL[1];
    static Soldier *apSoldierL[1];
    static u8 abHangarL[64];
    static i32 nAssignedL[1];
    static i32 nSeedL;
    static i32 nTotalSecL;
    static Ship ship;
    static StrategItem own;
    static StrategItem target;
    static void *apSlots[2];
    ShipRemovalEnv env;
    u8 nShipCount;
    u16 nSoldierCount;
    u8 once;

    printf("\nShip_LaunchIntercept @ 0x5a64c\n");

    memset(&ship, 0, sizeof(ship));
    ship.bId = 20;
    ship.bHomeBase = 0;
    ship.nSlotIdx = 0;
    apCraftL[0] = &ship;

    memset(&own, 0, sizeof(own));
    own.nType = 1;
    own.bActive = 0;
    own.nDisplayId = 20; /* the ship's bId, which is how FindByShipId matches it */
    own.nTimer = 777;
    own.nX = 40;
    own.nY = 60;

    memset(&target, 0, sizeof(target));
    target.nType = 5;
    target.nX = 300;
    target.nY = 400;

    apSlots[0] = &own;
    apSlots[1] = &target;
    listL.nCount = 2;
    listL.apItems = apSlots;

    baseL.nUnk8 = 100;
    baseL.nUnk12 = 200;
    apBaseL[0] = &baseL;

    nShipCount = 1;
    nSoldierCount = 0;
    once = 0;
    nTotalSecL = 0;
    nSeedL = 0;
    memset(&ctxL, 0, sizeof(ctxL));
    ctxL.pnSeed = &nSeedL;
    worldL.apCraft = apCraftL;
    worldL.nShipCount = 1;

    memset(&env, 0, sizeof(env));
    env.apCraft = apCraftL;
    env.pnShipCount = &nShipCount;
    env.apSoldiers = apSoldierL;
    env.pnSoldierCount = &nSoldierCount;
    env.apObjects = apObjL;
    env.nObjectCount = 0;
    env.pStrategList = &listL;
    env.pWorld = &worldL;
    env.pnSpawnOnceFlag = &once;
    env.pSpawnCtx = &ctxL;
    env.abHangarFlag = abHangarL;
    env.nHangarFlagCap = 64;
    env.anAssignedShip = nAssignedL;
    env.pnAssignedShipCount = &nAssignedL[0];
    env.apBases = apBaseL;
    env.pnGameTotalSec = &nTotalSecL;

    /* 1. A craft that is NOT at its base starts the leg from where its entry is. */
    ship.bAtBase = 0;
    own.nTimer = 777;
    own.nX = 40;
    own.nY = 60;
    (void)Ship_LaunchIntercept(&env, 0, 1);
    CheckEqI32(own.nStartX, 40, "the leg starts at the entry's own x (0x5A6A4)");
    CheckEqI32(own.nStartY, 60, "  and its y (0x5A6C8)");
    CheckEqI32(own.nDestX, 300, "the destination is the target entry's x");
    CheckEqI32(own.nDestY, 400, "  and its y");
    CheckEqI32(own.nX, 40, "bCopyStart snaps the entry to the leg start (0x7F66C)");
    CheckEqI32(own.nY, 60, "  in both axes");
    CheckEqI32(own.nTimer, 0, "the proximity timer is restarted at zero (0x5A67C)");
    CheckEqI32(ship.bWeaponsHot, 1, "bWeaponsHot is set (0x5A790)");
    CheckEqI32(ship.nStrategIdx, 1, "nStrategIdx becomes the target entry index (0x5A79C)");
    CheckEqI32(ship.bAtBase, 0, "bAtBase is cleared (0x5A7A4)");
    CheckEqI32(ship.bIntercepting, 1, "and the craft is intercepting (0x5A7AC)");

    /* 2. A craft that IS at its base leaves from the base, and its entry is snapped there too. */
    ship.bAtBase = 1;
    own.nTimer = 777;
    own.nX = 40;
    own.nY = 60;
    own.nStartX = 999;
    own.nStartY = 999;
    (void)Ship_LaunchIntercept(&env, 0, 1);
    CheckEqI32(own.nStartX, 100, "at base: the leg starts at the home base's x (0x5A718)");
    CheckEqI32(own.nStartY, 200, "  and its y (0x5A73C)");
    CheckEqI32(own.nX, 100, "and the entry itself is snapped to the base (0x5A760)");
    CheckEqI32(own.nY, 200, "  in both axes (0x5A780)");
    CheckEqI32(own.nDestX, 300, "the destination is still the target");
    CheckEqI32(own.nDestY, 400, "  in both axes");

    /* 3. Out-of-range ship index does nothing. */
    own.nTimer = 777;
    own.nX = 40;
    own.nY = 60;
    CheckEqI32(Ship_LaunchIntercept(&env, 5, 1), -1, "an out-of-range ship index is refused");
    CheckEqI32(own.nTimer, 777, "  and nothing is touched");
    CheckEqI32(ship.bIntercepting, 1, "  including the intercept flag");

    CheckEqI32(Ship_LaunchIntercept(NULL, 0, 1), -1, "a null env is refused");
}

#define LOOT_N_OBJECTS 150

static ObjectRecFull g_aLootObj[LOOT_N_OBJECTS];
static ObjectRecFull *g_apLootObj[LOOT_N_OBJECTS];
static Ship g_aLootShip[1];
static Ship *g_apLootShip[1];
static BaseRec g_aLootBase[1];
static BaseRec *g_apLootBase[1];
static Soldier *g_apLootSoldier[2];
static u8 g_abLootGrid[1524];
static Soldier g_aLootSoldier[2];
static u8 g_bLootRefresh;
static u16 g_nLootSoldiers = 2;

static int g_nMsgCount;
static int g_aMsgFlag[8];
static int g_aMsgC0[8];
static int g_aMsgC1[8];
static int g_aMsgC2[8];

static void LootMsg(i32 bFlag, i32 nC0, i32 nC1, i32 nC2, void *pUser)
{
    (void)pUser;
    if (g_nMsgCount < 8) {
        g_aMsgFlag[g_nMsgCount] = bFlag;
        g_aMsgC0[g_nMsgCount] = nC0;
        g_aMsgC1[g_nMsgCount] = nC1;
        g_aMsgC2[g_nMsgCount] = nC2;
        ++g_nMsgCount;
    }
}

static void TestTransferTacticalLoot(void)
{
    TacticalLootEnv env;

    printf("Base_TransferTacticalLoot\n");

    for (int i = 0; i < LOOT_N_OBJECTS; ++i) {
        g_apLootObj[i] = &g_aLootObj[i];
    }
    g_apLootShip[0] = &g_aLootShip[0];
    g_apLootBase[0] = &g_aLootBase[0];
    g_apLootSoldier[0] = &g_aLootSoldier[0];
    g_apLootSoldier[1] = &g_aLootSoldier[1];
    g_aLootBase[0].bSlot = 0;

    /* An empty base: every facility cell is 255, and every layer2 cell is occupied so that only
     * the one cell below the hangar reads as free. */
    memset(g_abLootGrid, 255, sizeof(g_abLootGrid));
    memset(g_abLootGrid, 0, 36 * 8); /* buildDays: nothing under construction anywhere */
    for (int i = 0; i < 8; ++i) {
        g_abLootGrid[444 + 144 * i + 6 * 1 + 1] = 0;
    }

    /* Base_CalcStorageUsed first, before the transfer moves anything. */
    g_aLootObj[5].bCargoSize = 3;
    g_aLootObj[5].auPerBaseVal[0] = 10;  /* 3 * 10 counts */
    g_aLootObj[70].bCargoSize = 9;
    g_aLootObj[70].auPerBaseVal[0] = 100; /* index 70 is outside every range */
    g_aLootObj[105].bCargoSize = 2;
    g_aLootObj[105].auPerBaseVal[0] = 5;  /* 2 * 5 counts */
    CheckEqI32(Base_CalcStorageUsed(g_apLootObj, LOOT_N_OBJECTS, 0), 40,
               "storage used counts only the ranges 1..69, 105..111, 122..124 and 140");
    g_aLootObj[5].bCargoSize = 0;
    g_aLootObj[5].auPerBaseVal[0] = 0;
    g_aLootObj[70].auPerBaseVal[0] = 0;
    g_aLootObj[105].bCargoSize = 0;
    g_aLootObj[105].auPerBaseVal[0] = 0;

    /* A hangar cell at base 0 (0,0), free down-right, nothing under construction. */
    g_abLootGrid[480 + 6 * 0 + 0] = 11;
    g_abLootGrid[444 + 6 * 1 + 1] = 0;

    Ship *pShip = &g_aLootShip[0];
    pShip->bHomeBase = 0;
    pShip->bAtBase = 1;
    pShip->bWeaponsHot = 1;
    pShip->bCraftType = 1;
    pShip->bCanDogfight = 0; /* keeps Base_StripAllCraftCargo out of the way */
    pShip->nSlotIdx = 0;
    pShip->bWeaponL = 25;
    pShip->bWeaponR = 25;
    pShip->bAmmo25 = 7;
    pShip->anCargoQty[0] = 300;
    pShip->anCargoQty[1] = 80;
    pShip->anCargoQty[2] = 400;
    pShip->anCargoQty[3] = 80;

    /* One weapon the base cannot supply ammo for, so the stores report fires. */
    g_aLootObj[1].aItemQty[0] = 5;
    g_aLootObj[1].anAmmoTypeId[0] = 0;
    g_aLootObj[1].anAmmoTypeId[1] = 0;
    g_aLootObj[1].anAmmoTypeId[2] = 0;

    /* Craft stores for the four cargo slots, and enough base stock that the reload does not wrap. */
    g_aLootObj[31].aItemQty[0] = 4;
    g_aLootObj[32].aItemQty[0] = 3;
    g_aLootObj[34].aItemQty[0] = 4;
    g_aLootObj[35].aItemQty[0] = 5;
    g_aLootObj[52].auPerBaseVal[0] = 100;

    g_aLootSoldier[0].bUnk183 = 0; /* aboard craft 0 */
    g_aLootSoldier[0].bUnk247 = 0;
    g_aLootSoldier[0].nUnk436 = 100;
    g_aLootSoldier[1].bUnk183 = 0;
    g_aLootSoldier[1].bUnk247 = 5; /* already credited, so skipped */

    memset(&env, 0, sizeof(env));
    env.apCraft = g_apLootShip;
    env.apBases = g_apLootBase;
    env.apObjects = g_apLootObj;
    env.apSoldiers = g_apLootSoldier;
    env.pnSoldierCount = &g_nLootSoldiers;
    env.abFacilityGrid = g_abLootGrid;
    env.pbUiRefreshFlag = &g_bLootRefresh;
    env.nBaseCount = 1;
    env.nShipCount = 1;
    env.nObjectCount = LOOT_N_OBJECTS;
    env.pfnAppendLine = LootMsg;
    g_nMsgCount = 0;

    Base_TransferTacticalLoot(&env, true);

    CheckEqI32(pShip->bWeaponsHot, 0, "the gate clears the hot flag as it tests it");
    CheckEqI32(pShip->bHangarX, 1, "the craft is parked one column in");
    CheckEqI32(pShip->bHangarY, 1, "and one row down");
    CheckEqI32(g_abLootGrid[444 + 144 * 1 + 6 * 1 + 1], 34,
               "the marker lands in the block numbered by the CRAFT TYPE, not the home base");
    CheckEqI32(g_abLootGrid[444 + 6 * 1 + 1], 0,
               "and base 0's own layer2 is left alone, which is the original's bug");

    CheckEqI32(g_aLootSoldier[0].bUnk247, 40, "an uncredited crew member takes 40 percent");
    CheckEqI32(g_aLootSoldier[0].bUnk183, 254, "and is marked away in a craft");
    CheckEqI32(g_aLootSoldier[1].bUnk247, 5, "a crew member already credited is skipped");

    CheckEqI32(g_aLootObj[56].auPerBaseVal[0], 180, "slot 31 reload charges the base 30 per unit");
    CheckEqI32(g_aLootObj[57].auPerBaseVal[0], 56, "slot 32 charges 8");
    CheckEqI32(g_aLootObj[105].auPerBaseVal[0], 20, "slot 34 charges 5");
    CheckEqI32(g_aLootObj[58].auPerBaseVal[0], 40, "slot 35 charges 8");
    CheckEqI32(pShip->anCargoQty[2], 200,
               "while the craft is credited 50 for slot 34, not 5 - the asymmetry");
    CheckEqI32(pShip->anCargoQty[0], 120, "and 30 for slot 31");

    CheckEqI32(g_bLootRefresh, 1, "the stores shortage raises the UI refresh flag");
    CheckEqI32(g_nMsgCount, 1, "and only the stores shortage is announced when ammo is plentiful");
    CheckEqI32(g_aMsgFlag[0], 39, "the stores report is message 39");
    CheckEqI32(g_aMsgC2[0], 255,
               "named by no object, because the weapon-vs-ammo test names nothing");

    CheckEqI32(pShip->bAmmo25, 20, "two mounts of the same weapon share one demand of 20");
    CheckEqI32(g_aLootObj[52].auPerBaseVal[0], 87, "the base pays 20 once per mount, twice");

    /* Now the same landing with almost no ammo at the base. */
    pShip->bWeaponsHot = 1;
    pShip->bAmmo25 = 7;
    g_aLootObj[52].auPerBaseVal[0] = 3;
    g_bLootRefresh = 0;
    g_nMsgCount = 0;
    Base_TransferTacticalLoot(&env, true);

    CheckEqI32(pShip->bAmmo25, 10,
               "a shortfall hands the craft the whole base column, which already includes the 7 it "
               "just gave back");
    CheckEqI32(g_aLootObj[52].auPerBaseVal[0], 0, "and clears the base column");
    CheckEqI32(g_nMsgCount, 2, "the ammo shortage is announced as well");
    CheckEqI32(g_aMsgFlag[1], 40, "message 40");
    CheckEqI32(g_aMsgC1[1], 52, "naming the ammo record");

    /* The same shortage with the second argument non-zero is silent but still refreshes the UI. */
    pShip->bWeaponsHot = 1;
    pShip->bAmmo25 = 7;
    g_aLootObj[52].auPerBaseVal[0] = 3;
    g_bLootRefresh = 0;
    g_nMsgCount = 0;
    Base_TransferTacticalLoot(&env, false);

    CheckEqI32(g_nMsgCount, 1, "a non-zero second argument silences the ammo report only");
    CheckEqI32(g_bLootRefresh, 1, "and the refresh flag is raised either way");
}

static void TestSpawnAtSlot(void)
{
    static Ship *apCraft[8];
    static BaseRec aBases[8];
    static BaseRec *apBases[8];
    static u8 grid[1524];
    static ObjectRecFull objs[1];
    static ObjectRecFull *apObjs[1];
    static ShipCtorEnv ctorEnv;
    static PtrArray list;
    static u8 nCountByte;
    static i32 nNextId;
    static i32 nSeed;
    static i32 nZoom;
    ShipSpawnEnv env;

    printf("Ship_SpawnAtSlot @ 0x2cb14\n");

    /* fac[] is grid + 0x174 with tileA at +0, tileB at +36, layer2 at +72 and facType at +108;
     * buildDays is its own array at grid + 36*nBase. Base 0 only, so no base term here. */
#define FAC(l, y, x) grid[0x174 + (l) + 6 * (y) + (x)]
#define DAYS(y, x)   grid[6 * (y) + (x)]

    for (i32 i = 0; i < 8; ++i) {
        memset(&aBases[i], 0, sizeof(aBases[i]));
        apBases[i] = &aBases[i];
        aBases[i].nUnk8 = 100 + i;  /* +0x08, the x the strateg item is placed at */
        aBases[i].nUnk12 = 200 + i; /* +0x0C, the y */
    }
    apObjs[0] = &objs[0];
    PtrArray_ctor(&list);
    nCountByte = 0;
    nNextId = 7;
    nSeed = 0;
    nZoom = 0;
    ctorEnv.apObjects = apObjs;
    ctorEnv.nObjectCount = 1;

    env.apCraft = apCraft;
    env.pnCraftCount = &nCountByte;
    env.pnNextCraftId = &nNextId;
    env.apBases = apBases;
    env.abFacilityGrid = grid;
    env.pCtorEnv = &ctorEnv;
    env.pStrategList = &list;
    env.pnStrategSeed = &nSeed;
    env.pnZoomMode = &nZoom;

    /* Nothing is a hangar, so the scan finds nothing. */
    memset(grid, 0, sizeof(grid));
    CheckEqI32(Ship_SpawnAtSlot(&env, 0, 0), 5, "with no hangar cell the scan returns 5");
    CheckEqI32(nCountByte, 1, "the craft count grows whether or not a cell was found");
    CheckEqI32(nNextId, 8, "and so does the next craft id");
    CheckEqI32(apCraft[0] != NULL ? 1 : 0, 1, "the craft is stored at the slot the count named");
    CheckEqI32(list.nCount, 1, "one strateg item was added");
    CheckEqI32(((StrategItem *)PtrArray_Get(&list, 0))->nDisplayId, 7,
               "the item carries the id the craft got, before it was bumped");
    CheckEqI32(((StrategItem *)PtrArray_Get(&list, 0))->nSiteId, -1,
               "a type 1 item sets nSiteId to -1");

    /* A hangar at facType[2][3]. layer2[1][4] is the cell the test looks at and it is clear. */
    memset(grid, 0, sizeof(grid));
    FAC(108, 2, 3) = PUFO_FAC_TYPE_HANGAR;
    CheckEqI32(Ship_SpawnAtSlot(&env, 2, 0), 3, "the scan returns the column it matched");
    CheckEqI32(apCraft[1]->bHangarX, 4, "bHangarX is result + 1");
    CheckEqI32(apCraft[1]->bHangarY, 3, "bHangarY is v10 + 1");
    CheckEqI32(FAC(72, 3, 4), 35,
               "the stamp lands at layer2[v10+1][result+1], which is 35 for craft type 2");
    CheckEqI32(FAC(108, 2, 3), PUFO_FAC_TYPE_HANGAR, "the facType byte is left alone");

    /* The layer2 test is pinned to row 1 whatever row the scan is on. Block row 1 and the
     * facType match at row 3 must be refused even though layer2[4][1] is clear. */
    memset(grid, 0, sizeof(grid));
    FAC(108, 3, 0) = PUFO_FAC_TYPE_HANGAR;
    FAC(72, 1, 1) = 1;   /* the cell the test actually reads */
    FAC(72, 4, 1) = 0;   /* the cell the stamp would use, and it is clear */
    CheckEqI32(Ship_SpawnAtSlot(&env, 1, 0), 5,
               "the layer2 test reads row 1, not row v10+1, so the match is refused");
    CheckEqI32(FAC(72, 4, 1), 0, "and nothing is stamped");

    /* A craft type outside the table matches the cell, sets the coordinates, and then falls
     * through the switch without stamping -- and the loop keeps going. */
    memset(grid, 0, sizeof(grid));
    FAC(108, 2, 3) = PUFO_FAC_TYPE_HANGAR;
    CheckEqI32(Ship_SpawnAtSlot(&env, 5, 0), 5,
               "an unknown craft type stamps nothing and the scan runs to the end");
    /* This is the fourth craft, so it lives at slot 3: the refused scan before it took slot 2. */
    CheckEqI32(apCraft[3]->bHangarX, 4, "but the coordinates were already written");
    CheckEqI32(apCraft[3]->bHangarY, 3, "and they stay written");
    CheckEqI32(FAC(72, 3, 4), 0, "with no glyph in the grid");

    /* Only five rows and five columns are ever considered. */
    memset(grid, 0, sizeof(grid));
    FAC(108, 5, 5) = PUFO_FAC_TYPE_HANGAR;
    CheckEqI32(Ship_SpawnAtSlot(&env, 0, 0), 5,
               "a hangar at row 5 column 5 is never seen: the scan is 5 by 5 of a 6 by 6 grid");

    for (i32 i = 0; i < 8; ++i) {
        free(apCraft[i]);
        apCraft[i] = NULL;
    }
    PtrArray_DeleteAll(&list);

#undef FAC
#undef DAYS
}

/* Ship_FormatDisplayName @ 0x666cc. The five craft labels are aTextRecs records 137, 138, 139, 418
 * and 419 -- one per CraftType -- and the number is Ship.bType, the per-craft-type ordinal
 * Ship_Ctor took from 0x18653C..0x186540, not the craft id. Types 3 and 4 also put a dash between
 * the label and the number. */
static void TestCraftDisplayName(void)
{
    static u8 aText[PUFO_TEXT_REC_STRIDE * 420];
    static Ship aShips[5];
    static Ship *apCraft[5];
    static char szOut[108];
    i32 i;

    printf("Ship_FormatDisplayName @ 0x666cc\n");

    PutTextRec(aText, 137, "INTERCEPTOR");
    PutTextRec(aText, 138, "TRANSPORT");
    PutTextRec(aText, 139, "BOMBER");
    PutTextRec(aText, 418, "UFO");
    PutTextRec(aText, 419, "RECON");

    for (i = 0; i < 5; ++i) {
        memset(&aShips[i], 0, sizeof(aShips[i]));
        aShips[i].bCraftType = (u8)i;
        aShips[i].bType = (u8)(i + 3);
        apCraft[i] = &aShips[i];
    }

    Check(strcmp(Ship_FormatDisplayName(apCraft, 0, aText, szOut, sizeof(szOut)), "INTERCEPTOR3") == 0,
          "type 0 is record 137 plus bType (0x66808)");
    Check(strcmp(Ship_FormatDisplayName(apCraft, 1, aText, szOut, sizeof(szOut)), "TRANSPORT4") == 0,
          "type 1 is record 138 (0x667D4)");
    Check(strcmp(Ship_FormatDisplayName(apCraft, 2, aText, szOut, sizeof(szOut)), "BOMBER5") == 0,
          "type 2 is record 139 (0x667A0)");
    Check(strcmp(Ship_FormatDisplayName(apCraft, 3, aText, szOut, sizeof(szOut)), "UFO-6") == 0,
          "type 3 is record 418 with the dash before the number (0x6675C, 0x6677C)");
    Check(strcmp(Ship_FormatDisplayName(apCraft, 4, aText, szOut, sizeof(szOut)), "RECON-7") == 0,
          "type 4 is record 419, also dashed (0x66720)");

    /* The number is bType, not bId. */
    aShips[0].bType = 9;
    aShips[0].bId = 42;
    Check(strcmp(Ship_FormatDisplayName(apCraft, 0, aText, szOut, sizeof(szOut)), "INTERCEPTOR9") == 0,
          "the number is Ship.bType @ +0, not the craft id (0x6682C)");

    /* A type outside the five falls through the switch; the original then copies an uninitialised
     * local into the scratch, the port writes nothing. */
    aShips[0].bCraftType = 5;
    PutStr(szOut, "stale");
    Check(strcmp(Ship_FormatDisplayName(apCraft, 0, aText, szOut, sizeof(szOut)), "") == 0,
          "an unknown craft type yields the empty string (0x666F8 default)");

    /* The caller's bound is honoured, where the original copies without one. */
    aShips[0].bCraftType = 0;
    aShips[0].bType = 99;
    Check(strlen(Ship_FormatDisplayName(apCraft, 0, aText, szOut, 8)) == 7,
          "the label is cut to the caller's buffer (0x66854 copies without a bound)");

    Check(strcmp(Ship_FormatDisplayName(NULL, 0, aText, szOut, sizeof(szOut)), "") == 0,
          "a null roster yields the empty string");
    Check(strcmp(Ship_FormatDisplayName(apCraft, 0, NULL, szOut, sizeof(szOut)), "") == 0,
          "and so does a missing TEXTS blob");
}

static void TestFindIndexById(void)
{
    Ship aShips[3];
    Ship *apShips[3];

    printf("Ship_FindIndexById @ 0x69ffc\n");

    for (int i = 0; i < 3; ++i) {
        memset(&aShips[i], 0, sizeof(aShips[i]));
        aShips[i].bId = (u8)(4 + i);
        apShips[i] = &aShips[i];
    }

    CheckEqI32(Ship_FindIndexById(apShips, 3, 4), 0, "the first craft's id");
    CheckEqI32(Ship_FindIndexById(apShips, 3, 5), 1, "the middle one");
    CheckEqI32(Ship_FindIndexById(apShips, 3, 6), 2, "the last one");
    CheckEqI32(Ship_FindIndexById(apShips, 3, 7), -1, "an id nobody has answers -1");
    CheckEqI32(Ship_FindIndexById(apShips, 1, 5), -1,
               "the count bounds the walk, so a match past it is not found");

    /* 0x6A018 branches straight to the -1 at 0x6A044 when the count is not positive. */
    CheckEqI32(Ship_FindIndexById(apShips, 0, 4), -1, "a zero craft count answers -1");
    CheckEqI32(Ship_FindIndexById(apShips, -1, 4), -1, "and so does a negative one");
    CheckEqI32(Ship_FindIndexById(NULL, 3, 4), -1, "no array answers -1");

    /* Deviation: 0x6A024 dereferences apCraft[i] with no NULL test. */
    apShips[1] = NULL;
    CheckEqI32(Ship_FindIndexById(apShips, 3, 6), 2,
               "a NULL slot is stepped over, where the original would fault");
    CheckEqI32(Ship_FindIndexById(apShips, 3, 5), -1, "and the craft behind it is simply absent");
}

static void TestProductSlotToType(void)
{
    static const i32 anSlot[5] = {
        OBJ_CRAFT_COUNT_SKYRANGER, OBJ_CRAFT_COUNT_INTERCEPTOR, OBJ_CRAFT_COUNT_FIRESTORM,
        OBJ_CRAFT_COUNT_LIGHTNING, OBJ_CRAFT_COUNT_AVENGER
    };
    /* Smap_GrantSiteRecovery's own switch on the slot INDEX, 0x99360..0x993A8. */
    static const i32 anIndexMap[5] = { 0, 3, 4, 1, 2 };
    i32 anSeen[PUFO_CRAFT_TYPE_COUNT] = { 0, 0, 0, 0, 0 };
    bool bOneToOne = true;

    printf("Ship_ProductSlotToType @ 0x65a6c\n");

    CheckEqI32(Ship_ProductSlotToType(OBJ_CRAFT_COUNT_SKYRANGER), CRAFT_TYPE_SKYRANGER,
               "117 answers Skyranger");
    CheckEqI32(Ship_ProductSlotToType(OBJ_CRAFT_COUNT_INTERCEPTOR), CRAFT_TYPE_INTERCEPTOR,
               "118 answers Interceptor");
    CheckEqI32(Ship_ProductSlotToType(OBJ_CRAFT_COUNT_FIRESTORM), CRAFT_TYPE_FIRESTORM,
               "119 answers Firestorm");
    CheckEqI32(Ship_ProductSlotToType(OBJ_CRAFT_COUNT_LIGHTNING), CRAFT_TYPE_LIGHTNING,
               "120 answers Lightning");
    CheckEqI32(Ship_ProductSlotToType(OBJ_CRAFT_COUNT_AVENGER), CRAFT_TYPE_AVENGER,
               "121 answers Avenger");

    for (i32 i = 0; i < 5; ++i) {
        const i32 nType = Ship_ProductSlotToType(anSlot[i]);
        CheckEqI32(nType, anIndexMap[i],
                   "the slot map is the same map Smap_GrantSiteRecovery inlines at 0x99360");
        anSeen[nType] += 1;
    }
    for (i32 t = 0; t < PUFO_CRAFT_TYPE_COUNT; ++t) {
        if (anSeen[t] != 1) {
            bOneToOne = false;
        }
    }
    Check(bOneToOne, "the five craft slots cover every CraftType exactly once");

    CheckEqI32(Ship_ProductSlotToType(116), -1, "the slot below the block is unmapped");
    CheckEqI32(Ship_ProductSlotToType(122), -1, "and so is the slot above it");
    CheckEqI32(Ship_ProductSlotToType(0), -1, "an ordinary item slot is unmapped");
    CheckEqI32(Ship_ProductSlotToType(255), -1, "0xFF is unmapped");
    CheckEqI32(Ship_ProductSlotToType(-1), -1,
               "the -1 a caller would pass masks to 0xFF and stays unmapped");

    /* 0x65A6C is AND R0,R1,#0xFF, so only the low byte of the slot id is ever compared. */
    CheckEqI32(Ship_ProductSlotToType(117 + 256), CRAFT_TYPE_SKYRANGER,
               "a slot id one byte too wide folds back onto 117");
    CheckEqI32(Ship_ProductSlotToType(121 + 0x10000), CRAFT_TYPE_AVENGER,
               "and the same for a whole word too high");
}

int main(void)
{
    TestRangeRecover();
    TestRefuel();
    TestSupplyConsume();
    TestCargoPair();
    TestShipRemoveWithCargo();
    TestRemoveAllShips();
    TestCargoHold();
    TestLoadStores();
    TestStripCraftCargo();
    TestDistributeRecoveredItems();
    TestCraftWeaponItemId();
    TestCrewAndStoresTotals();
    TestInterceptRetarget();
    TestLaunchIntercept();
    TestTransferTacticalLoot();
    TestSpawnAtSlot();
    TestCraftDisplayName();
    TestFindIndexById();
    TestProductSlotToType();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
