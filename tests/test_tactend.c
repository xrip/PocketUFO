/* tests/test_tactend.c - Tact_Shutdown @ 0x3d568.
 *
 * The two pufo_ui callees (UiPanel_FreeLines, UiControl_FreePads) are reached through
 * hooks, so pufo_game does not depend on pufo_ui. The test binary still has to link
 * pufo_ui: Tact_Shutdown calls StrategList_RemoveAt, pufo_strateg.obj gets pulled in,
 * and that object reaches UiLineList_Remove in pufo_ui. That is the module cycle
 * recorded as a separate issue in PLAN.md.
 */
#include "game/pufo_tactend.h"

#include "game/pufo_strateg.h"
#include "game/pufo_unit.h"
#include "pufo_test.h"
#include "util/pufo_ptrarray.h"

#include <stdlib.h>
#include <string.h>

#define BANK_WIDGETS 130
#define CELL_SLOTS   14400

static UiControl  s_aBank[BANK_WIDGETS];
static PadImage  *s_apBankPad[BANK_WIDGETS][3];
static UiPanel   *s_apPanel[5];
static UiPanel   *s_pLinePanel;
static UiControl *s_pLookCtl;
static PadImage  *s_apPadEarly[PUFO_SHUTDOWN_PAD_EARLY];
static PadImage  *s_apPadLate[PUFO_SHUTDOWN_PAD_LATE];
static PadImage  *s_apPadTail[PUFO_SHUTDOWN_PAD_TAIL];
static PadImage  *s_apSpriteSet[PUFO_SHUTDOWN_SPRITE_SET];
static PadImage  *s_apMapPads[PUFO_SHUTDOWN_MAP_PADS];
static RmpData   *s_pRmp;
static void      *s_pBlock156A78;
static void      *s_pBlock187050;
static PufoFont   s_Font;
static u8         s_bActionLatch;
static TactItem  *s_apTactItems[8];
static i32        s_nTactItemCount;
static void      *s_apStrategCopy[4];
static u16        s_nStrategCopy;
static void      *s_apBaseCopy[4];
static i8         s_nBaseCount;
static void      *s_apCraftCopy[4];
static u8         s_nCraftCount;
static PtrArray   s_StrategList;
static Soldier   *s_apCivilian[4];
static i32        s_nCivilian;
static Soldier   *s_apEnemy[4];
static u8         s_nEnemy;
static Soldier   *s_apXcom[4];
static u8         s_nXcom;
static u8         s_bMissionMode;
static u8         s_bVictory;
static u8         s_bAltPath;
static u8         s_bFacType;
static i32        s_nStrategIdx;
static u8         s_nCraftIndex;
static u8         s_bSpawnOnce;
static MapCell   *s_apCells[CELL_SLOTS];
static u8         s_nMapDimX;
static u8         s_nMapDimY;
static void      *s_pLoftemps;
static void      *s_pUnitloft;
static void      *s_pMapTactical;
static void      *s_pMapCtx;

/* What the panel hook saw, so a line count set before the free is still checkable. */
static i32 s_aSeenLineCount[8];
static i32 s_nSeenLineCount;
static i32 s_nPanelHookCalls;
static i32 s_nControlHookCalls;

static PadImage *NewPad(void)
{
    return (PadImage *)calloc(1, sizeof(PadImage));
}

static void PanelFreeLinesHook(UiPanel *pPanel, void *pUser)
{
    (void)pUser;
    if (s_nSeenLineCount < (i32)(sizeof(s_aSeenLineCount) / sizeof(s_aSeenLineCount[0]))) {
        s_aSeenLineCount[s_nSeenLineCount++] = pPanel->nLineCount;
    }
    ++s_nPanelHookCalls;
}

static void ControlFreePadsHook(UiControl *pCtl, void *pUser)
{
    (void)pCtl;
    (void)pUser;
    ++s_nControlHookCalls;
}

static void NoopHook(void *pUser)
{
    (void)pUser;
}

static void Fixture_Reset(void)
{
    i32 i;

    memset(s_aBank, 0, sizeof(s_aBank));
    memset(s_apPanel, 0, sizeof(s_apPanel));
    memset(s_apPadEarly, 0, sizeof(s_apPadEarly));
    memset(s_apPadLate, 0, sizeof(s_apPadLate));
    memset(s_apPadTail, 0, sizeof(s_apPadTail));
    memset(s_apSpriteSet, 0, sizeof(s_apSpriteSet));
    memset(s_apMapPads, 0, sizeof(s_apMapPads));
    memset(s_apTactItems, 0, sizeof(s_apTactItems));
    memset(s_apStrategCopy, 0, sizeof(s_apStrategCopy));
    memset(s_apBaseCopy, 0, sizeof(s_apBaseCopy));
    memset(s_apCraftCopy, 0, sizeof(s_apCraftCopy));
    memset(s_apCivilian, 0, sizeof(s_apCivilian));
    memset(s_apEnemy, 0, sizeof(s_apEnemy));
    memset(s_apXcom, 0, sizeof(s_apXcom));
    memset(s_apCells, 0, sizeof(s_apCells));
    memset(&s_Font, 0, sizeof(s_Font));
    memset(&s_StrategList, 0, sizeof(s_StrategList));
    PtrArray_ctor(&s_StrategList);

    /* The original writes nLineCount into all five panels before it tests them for
     * null, so the env contract is that they exist. The fixture always supplies them. */
    for (i = 0; i < 4; ++i) {
        s_apPanel[i] = (UiPanel *)calloc(1, sizeof(UiPanel));
        s_apPanel[i]->nLineCount = 999;
    }
    s_pLinePanel = (UiPanel *)calloc(1, sizeof(UiPanel));
    s_pLinePanel->nLineCount = 999;
    s_pLookCtl = NULL;
    s_pRmp = NULL;
    s_pBlock156A78 = NULL;
    s_pBlock187050 = NULL;
    s_bActionLatch = 1;
    s_nTactItemCount = 0;
    s_nStrategCopy = 0;
    s_nBaseCount = 0;
    s_nCraftCount = 0;
    s_nCivilian = 0;
    s_nEnemy = 0;
    s_nXcom = 0;
    s_bMissionMode = 2;
    s_bVictory = 0;
    s_bAltPath = 1;   /* mode 2 on this path calls nothing, which keeps the unit tests local */
    s_bFacType = 0;
    s_nStrategIdx = 0;
    s_nCraftIndex = 0;
    s_bSpawnOnce = 0;
    s_nMapDimX = 0;
    s_nMapDimY = 0;
    s_pLoftemps = NULL;
    s_pUnitloft = NULL;
    s_pMapTactical = NULL;
    s_pMapCtx = NULL;
    s_nSeenLineCount = 0;
    s_nPanelHookCalls = 0;
    s_nControlHookCalls = 0;

    for (i = 0; i < BANK_WIDGETS; ++i) {
        s_apBankPad[i][0] = NULL;
        s_apBankPad[i][1] = NULL;
        s_apBankPad[i][2] = NULL;
    }
}

static void Fixture_Free(void)
{
    i32 i;
    i32 k;

    /* Only what survived the shutdown is freed here. The port already freed and nullled
     * everything it touched, so freeing the stale copies would be a double free. */
    for (i = 0; i < BANK_WIDGETS; ++i) {
        for (k = 0; k < 3; ++k) {
            if (s_aBank[i].apPads[k] != NULL) {
                free(s_aBank[i].apPads[k]);
                s_aBank[i].apPads[k] = NULL;
            }
            s_apBankPad[i][k] = NULL;
        }
    }
    for (i = 0; i < CELL_SLOTS; ++i) {
        free(s_apCells[i]);
        s_apCells[i] = NULL;
    }
    /* The rosters compact as units are removed, so only the first nCount slots are live. */
    for (i = 0; i < s_nCivilian; ++i) {
        free(s_apCivilian[i]);
    }
    for (i = 0; i < s_nEnemy; ++i) {
        free(s_apEnemy[i]);
    }
    for (i = 0; i < s_nXcom; ++i) {
        free(s_apXcom[i]);
    }
    for (i = 0; i < 4; ++i) {
        s_apCivilian[i] = NULL;
        s_apEnemy[i] = NULL;
        s_apXcom[i] = NULL;
        free(s_apPanel[i]);
        s_apPanel[i] = NULL;
    }
    free(s_pLinePanel);
    s_pLinePanel = NULL;
    PtrArray_Clear(&s_StrategList);
}

static void Bank_GivePads(void)
{
    i32 w;
    i32 k;

    for (w = 0; w < BANK_WIDGETS; ++w) {
        for (k = 0; k < 3; ++k) {
            s_apBankPad[w][k] = NewPad();
            s_aBank[w].apPads[k] = s_apBankPad[w][k];
        }
    }
}

static TactShutdownEnv Env_Build(void)
{
    TactShutdownEnv env;

    memset(&env, 0, sizeof(env));
    env.appMsgPanel = s_apPanel;
    env.appLinePanel = &s_pLinePanel;
    env.appLookCtl = &s_pLookCtl;
    env.pBank = s_aBank;
    env.appPadEarly = s_apPadEarly;
    env.appPadLate = s_apPadLate;
    env.appPadTail = s_apPadTail;
    env.appSpriteSet = s_apSpriteSet;
    env.appMapPads = s_apMapPads;
    env.appRmp = &s_pRmp;
    env.appBlock156A78 = &s_pBlock156A78;
    env.appBlock187050 = &s_pBlock187050;
    env.pFont = &s_Font;
    env.pbActionLatch = &s_bActionLatch;
    env.apTactItems = s_apTactItems;
    env.pnTactItemCount = &s_nTactItemCount;
    env.apStrategCraftCopy = s_apStrategCopy;
    env.pnStrategCraftCopy = &s_nStrategCopy;
    env.apBaseCopy = s_apBaseCopy;
    env.pnBaseCount = &s_nBaseCount;
    env.apCraftCopy = s_apCraftCopy;
    env.pnCraftCount = &s_nCraftCount;
    env.pStrategList = &s_StrategList;
    env.apCivilianUnits = s_apCivilian;
    env.pnCivilianCount = &s_nCivilian;
    env.apEnemyUnits = s_apEnemy;
    env.pnEnemyCount = &s_nEnemy;
    env.apXcomUnits = s_apXcom;
    env.pnXcomCount = &s_nXcom;
    env.pbMissionMode = &s_bMissionMode;
    env.pbMissionVictory = &s_bVictory;
    env.pbAlternateOutcomePath = &s_bAltPath;
    env.pbFacilityType = &s_bFacType;
    env.pnStrategIndex = &s_nStrategIdx;
    env.pnCraftIndex = &s_nCraftIndex;
    env.pbSpawnOnceFlag = &s_bSpawnOnce;
    env.apCells = s_apCells;
    env.pnMapDimX = &s_nMapDimX;
    env.pnMapDimY = &s_nMapDimY;
    env.appLoftemps = &s_pLoftemps;
    env.appUnitloft = &s_pUnitloft;
    env.appMapTactical = &s_pMapTactical;
    env.appMapCtx = &s_pMapCtx;
    env.hooks.pfnSoundShutdown = NoopHook;
    env.hooks.pfnKillTimers = NoopHook;
    env.hooks.pfnPanelFreeLines = PanelFreeLinesHook;
    env.hooks.pfnControlFreePads = ControlFreePadsHook;
    return env;
}

static Soldier *NewUnit(i32 nId, u8 nType, u8 bInZone)
{
    Soldier *pUnit = (Soldier *)calloc(1, sizeof(Soldier));

    pUnit->nUnk440 = nId;
    pUnit->nUnitType = nType;
    pUnit->bInZone = bInZone;
    return pUnit;
}

/* The thirteen groups cover 0..112 and 128..129; StartUi_Shutdown covers 113..127. */
static void TestBankGroups(void)
{
    TactShutdownEnv env;
    i32 w;
    i32 k;
    i32 nCleared = 0;
    i32 nKept = 0;

    printf("Tact_Shutdown - the bank pad groups\n");
    Fixture_Reset();
    Bank_GivePads();

    for (w = 0; w < BANK_WIDGETS; ++w) {
        for (k = 0; k < 3; ++k) {
            s_apBankPad[w][k]->uWidth = (u16)w; /* a marker so a survivor is identifiable */
        }
    }

    env = Env_Build();
    Tact_Shutdown(&env, 1);

    for (w = 0; w < BANK_WIDGETS; ++w) {
        for (k = 0; k < 3; ++k) {
            if (s_aBank[w].apPads[k] == NULL) {
                ++nCleared;
            } else {
                ++nKept;
            }
        }
    }
    TestEqI32("115 widgets x 3 pads cleared", nCleared, 115 * 3);
    TestEqI32("the 15 widgets StartUi_Shutdown owns survive", nKept, 15 * 3);

    for (w = 113; w <= 127; ++w) {
        TestCheck("widget 113..127 keeps its three pads",
                  s_aBank[w].apPads[0] != NULL && s_aBank[w].apPads[1] != NULL
                      && s_aBank[w].apPads[2] != NULL);
    }
    TestCheck("widget 112 is cleared (the group boundary)", s_aBank[112].apPads[0] == NULL);
    TestCheck("widget 128 is cleared", s_aBank[128].apPads[0] == NULL);

    Fixture_Free();
}

static void TestPanelsAndGlobals(void)
{
    TactShutdownEnv env;
    i32 i;

    printf("Tact_Shutdown - panels and the freed globals\n");
    Fixture_Reset();
    for (i = 0; i < PUFO_SHUTDOWN_PAD_EARLY; ++i) {
        s_apPadEarly[i] = NewPad();
    }
    for (i = 0; i < PUFO_SHUTDOWN_PAD_LATE; ++i) {
        s_apPadLate[i] = NewPad();
    }
    for (i = 0; i < PUFO_SHUTDOWN_PAD_TAIL; ++i) {
        s_apPadTail[i] = NewPad();
    }
    for (i = 0; i < PUFO_SHUTDOWN_SPRITE_SET; ++i) {
        s_apSpriteSet[i] = NewPad();
    }
    for (i = 0; i < PUFO_SHUTDOWN_MAP_PADS; ++i) {
        s_apMapPads[i] = NewPad();
    }
    s_pLookCtl = (UiControl *)calloc(1, sizeof(UiControl));
    s_pBlock156A78 = malloc(8);
    s_pBlock187050 = malloc(8);
    s_pLoftemps = malloc(8);
    s_pUnitloft = malloc(8);
    s_pMapTactical = malloc(8);
    s_pMapCtx = malloc(8);
    s_bActionLatch = 1;

    env = Env_Build();
    Tact_Shutdown(&env, 1);

    TestEqI32("the line panel is seen with nLineCount 28", s_aSeenLineCount[0], 28);
    TestEqI32("four message panels are seen with 14", s_aSeenLineCount[1], 14);
    TestEqI32("all five panels reach the hook", s_nPanelHookCalls, 5);
    TestEqI32("the look control reaches the control hook", s_nControlHookCalls, 1);
    TestEqI32("Map action latch cleared", s_bActionLatch, 0);

    TestCheck("line panel slot cleared", s_pLinePanel == NULL);
    for (i = 0; i < 4; ++i) {
        TestCheck("message panel slot cleared", s_apPanel[i] == NULL);
    }
    TestCheck("look control slot cleared", s_pLookCtl == NULL);
    for (i = 0; i < PUFO_SHUTDOWN_PAD_EARLY; ++i) {
        TestCheck("early pad slot cleared", s_apPadEarly[i] == NULL);
    }
    for (i = 0; i < PUFO_SHUTDOWN_PAD_LATE; ++i) {
        TestCheck("late pad slot cleared", s_apPadLate[i] == NULL);
    }
    for (i = 0; i < PUFO_SHUTDOWN_PAD_TAIL; ++i) {
        TestCheck("tail pad slot cleared", s_apPadTail[i] == NULL);
    }
    for (i = 0; i < PUFO_SHUTDOWN_SPRITE_SET; ++i) {
        TestCheck("sprite set slot cleared", s_apSpriteSet[i] == NULL);
    }
    for (i = 0; i < PUFO_SHUTDOWN_MAP_PADS; ++i) {
        TestCheck("Map pad slot cleared", s_apMapPads[i] == NULL);
    }
    TestCheck("the four unconditional deletes are cleared",
              s_pBlock156A78 == NULL && s_pBlock187050 == NULL && s_pLoftemps == NULL
                  && s_pUnitloft == NULL && s_pMapTactical == NULL && s_pMapCtx == NULL);

    Fixture_Free();
}

/* 10 * nMapDimY rows by 10 * nMapDimX columns, four layers, row stride 60. */
static void TestCellFree(void)
{
    TactShutdownEnv env;
    i32 y;
    i32 x;
    i32 nLayer;
    i32 nFreed = 0;

    printf("Tact_Shutdown - the tactical cell array\n");
    Fixture_Reset();
    s_nMapDimX = 4;
    s_nMapDimY = 4;
    for (y = 0; y < 40; ++y) {
        for (x = 0; x < 40; ++x) {
            for (nLayer = 0; nLayer < 4; ++nLayer) {
                s_apCells[3600 * nLayer + 60 * y + x] = (MapCell *)calloc(1, sizeof(MapCell));
                ++nFreed;
            }
        }
    }
    /* One cell outside the bounds: row 40 is past 10 * nMapDimY. */
    s_apCells[60 * 40 + 0] = (MapCell *)calloc(1, sizeof(MapCell));

    env = Env_Build();
    Tact_Shutdown(&env, 1);

    TestEqI32("every in-bounds cell was visited", nFreed, 40 * 40 * 4);
    for (y = 0; y < 40; ++y) {
        for (x = 0; x < 40; ++x) {
            for (nLayer = 0; nLayer < 4; ++nLayer) {
                if (s_apCells[3600 * nLayer + 60 * y + x] != NULL) {
                    TestCheck("an in-bounds cell survived", 0);
                    y = 40;
                    x = 40;
                    nLayer = 4;
                }
            }
        }
    }
    TestCheck("all in-bounds cells are cleared", 1);
    TestCheck("a cell outside 10 * nMapDimY is NOT freed", s_apCells[60 * 40] != NULL);

    Fixture_Free();
}

static void TestKeepSomePath(void)
{
    TactShutdownEnv env;
    i32 i;

    printf("Tact_Shutdown - bKeepSome keeps the world\n");
    Fixture_Reset();
    s_nTactItemCount = 3;
    for (i = 0; i < 3; ++i) {
        s_apTactItems[i] = (TactItem *)calloc(1, sizeof(TactItem));
    }
    s_nStrategCopy = 2;
    for (i = 0; i < 2; ++i) {
        s_apStrategCopy[i] = malloc(16);
    }
    s_nBaseCount = 1;
    s_apBaseCopy[0] = malloc(16);
    s_nCraftCount = 2;
    for (i = 0; i < 2; ++i) {
        s_apCraftCopy[i] = malloc(16);
    }
    for (i = 0; i < 3; ++i) {
        PtrArray_Add(&s_StrategList, malloc(16));
    }
    s_nCivilian = 2;
    s_apCivilian[0] = NewUnit(1, 2, 0);
    s_apCivilian[1] = NewUnit(2, 2, 0);
    s_nEnemy = 2;
    s_apEnemy[0] = NewUnit(10, 5, 0);
    s_apEnemy[1] = NewUnit(11, 5, 0);

    env = Env_Build();
    Tact_Shutdown(&env, 1);

    TestEqI32("the strateg list is emptied", PtrArray_GetCount(&s_StrategList), 0);
    TestEqI32("civilians are left alone", s_nCivilian, 2);
    TestEqI32("enemies are left alone", s_nEnemy, 2);
    /* The original frees the pool but never resets the count. */
    TestEqI32("Map_nTactItemCount is not reset", s_nTactItemCount, 3);

    Fixture_Free();
}

/* The walk runs against a shrinking list while the index keeps advancing, so it skips
 * entries. Three enemies means one survivor. */
static void TestUnitRemoval(void)
{
    TactShutdownEnv env;

    printf("Tact_Shutdown - which tactical units go\n");
    Fixture_Reset();
    s_nEnemy = 3;
    s_apEnemy[0] = NewUnit(10, 5, 0);
    s_apEnemy[1] = NewUnit(11, 5, 0);
    s_apEnemy[2] = NewUnit(12, 5, 0);

    s_nCivilian = 2;
    s_apCivilian[0] = NewUnit(1, 2, 1);  /* in zone -> kept */
    s_apCivilian[1] = NewUnit(2, 2, 0);  /* not in zone -> removed */

    s_nXcom = 3;
    s_apXcom[0] = NewUnit(20, 5, 1);  /* type 4..8 -> removed */
    s_apXcom[1] = NewUnit(21, 2, 1);  /* type 2, in zone -> kept */
    s_apXcom[2] = NewUnit(22, 9, 0);  /* not in zone -> removed */

    PtrArray_Add(&s_StrategList, malloc(16));
    PtrArray_Add(&s_StrategList, malloc(16));

    env = Env_Build();
    Tact_Shutdown(&env, 0);

    TestEqI32("three enemies leave one survivor (the shrinking-list walk)", s_nEnemy, 1);
    TestEqI32("the surviving enemy is the original index 1",
              s_apEnemy[0] != NULL ? s_apEnemy[0]->nUnk440 : -1, 11);
    TestEqI32("one civilian removed", s_nCivilian, 1);
    TestEqI32("the surviving civilian is the one in zone",
              s_apCivilian[0] != NULL ? s_apCivilian[0]->nUnk440 : -1, 1);
    TestEqI32("two XCom removed", s_nXcom, 1);
    TestEqI32("the surviving XCom is the type-2 one in zone",
              s_apXcom[0] != NULL ? s_apXcom[0]->nUnk440 : -1, 21);
    TestEqI32("the strateg list is NOT emptied on the full path",
              PtrArray_GetCount(&s_StrategList), 2);

    Fixture_Free();
}

/* ========================================================================= the four tallies
 *
 * A 1 x 1 map (10 x 10 cells per layer, four layers) is enough to reach every branch, and every
 * cell of the flat 14400-slot block is allocated so the original's own index arithmetic
 * (3600 * nZ + 60 * nY + nX) is what the test exercises. */

#define T_CELL_SLOTS 14400
#define T_OBJ_COUNT  160

static MapCell  *t_apCells[T_CELL_SLOTS];
static Mc1Rec   *t_apSpr[64];
static i32       t_nSpr;
static TactItem *t_apItem[32];
static i32       t_nItem;
static ObjectRec *t_apObj[T_OBJ_COUNT];
static Ship      t_aCraft[4];
static Ship     *t_apCraft[4];
static SmapSite  t_aSites[PUFO_SMAP_SITE_COUNT];
static Soldier  *t_apSoldier[8];
static Soldier  *t_apTactXcom[8];
static Soldier  *t_apTactEnemy[8];
static Soldier  *t_apTactCiv[8];
static u16       t_nSoldier;
static u8        t_nXcom;
static u8        t_nEnemy;
static i32       t_nCiv;
static i32       t_nObjCount = T_OBJ_COUNT;
static i32       t_nCraftCount;
static u8        t_nCraftIndex;
static u8        t_nMissionType;
static u8        t_nTerrainVariant;
static i8        t_nHomeBase;
static i32       t_nSiteCount;
static u8        t_bVictory;
static u8        t_bAltPath;
static u8        t_nDimX;
static u8        t_nDimY;
static i32       t_nEleriumRoll;
static i32       t_nFoodRoll;

static MapCell *TCell(i32 nLayer, i32 nY, i32 nX)
{
    return t_apCells[3600 * nLayer + 60 * nY + nX];
}

static Mc1Rec *NewSpr(u8 nKind)
{
    Mc1Rec *pSpr = (Mc1Rec *)calloc(1, sizeof(Mc1Rec));

    pSpr->bFacilityKind = nKind;
    if (t_nSpr < (i32)(sizeof(t_apSpr) / sizeof(t_apSpr[0]))) {
        t_apSpr[t_nSpr++] = pSpr;
    }
    return pSpr;
}

static TactItem *NewItem(ObjectRec *pObj, u16 nIndex, u8 nAmmo, Soldier *pLinked)
{
    TactItem *pItem = (TactItem *)calloc(1, sizeof(TactItem));

    pItem->pObj = pObj;
    pItem->nIndex = nIndex;
    pItem->nAmmo = nAmmo;
    pItem->pLinkedSoldier = pLinked;
    if (t_nItem < (i32)(sizeof(t_apItem) / sizeof(t_apItem[0]))) {
        t_apItem[t_nItem++] = pItem;
    }
    return pItem;
}

/* Chain an item onto a cell's list, keeping the original's head order. */
static void CellAddItem(MapCell *pCell, TactItem *pItem)
{
    pItem->pNext = pCell->pItemList;
    pCell->pItemList = pItem;
}

/* The item an alien corpse leaves on its cell. Slot 150 is a placeholder whose bTwoHanded is a
 * value the chain walk has no case for, so the placeholder itself is never credited and only the
 * alien credit below it is observable. */
static void CellAddCorpseItem(MapCell *pCell, Soldier *pUnit)
{
    TactItem *pItem;

    t_apObj[150]->bTwoHanded = 3;
    pItem = NewItem(t_apObj[150], 150, 0, pUnit);
    CellAddItem(pCell, pItem);
}

static Soldier *NewTactUnit(i32 nId, u8 nType, u8 nRank, i32 nX, i32 nY, i32 nZ, u16 uHp)
{
    Soldier *pUnit = (Soldier *)calloc(1, sizeof(Soldier));

    pUnit->nUnk440 = nId;
    pUnit->nUnitType = nType;
    pUnit->bRankType = nRank;
    pUnit->uHp = uHp;
    pUnit->pCell = TCell(nZ, nY, nX);
    return pUnit;
}

static void Tally_Reset(void)
{
    i32 i;

    for (i = 0; i < T_CELL_SLOTS; ++i) {
        free(t_apCells[i]);
        t_apCells[i] = (MapCell *)calloc(1, sizeof(MapCell));
        t_apCells[i]->nX = (u8)(i % 60);
        t_apCells[i]->nY = (u8)((i / 60) % 60);
        t_apCells[i]->nZ = (u8)(i / 3600);
    }
    for (i = 0; i < t_nSpr; ++i) {
        free(t_apSpr[i]);
    }
    for (i = 0; i < t_nItem; ++i) {
        free(t_apItem[i]);
    }
    t_nSpr = 0;
    t_nItem = 0;

    for (i = 0; i < T_OBJ_COUNT; ++i) {
        free(t_apObj[i]);
        t_apObj[i] = (ObjectRec *)calloc(1, sizeof(ObjectRec));
    }
    for (i = 0; i < 4; ++i) {
        free(t_apTactXcom[i]);
        free(t_apTactEnemy[i]);
        free(t_apTactCiv[i]);
        free(t_apSoldier[i]);
        t_apTactXcom[i] = NULL;
        t_apTactEnemy[i] = NULL;
        t_apTactCiv[i] = NULL;
        t_apSoldier[i] = NULL;
        t_apCraft[i] = &t_aCraft[i];
        memset(&t_aCraft[i], 0, sizeof(Ship));
    }
    t_nSoldier = 0;
    t_nXcom = 0;
    t_nEnemy = 0;
    t_nCiv = 0;
    t_nCraftCount = 1;
    t_nCraftIndex = 0;
    t_nMissionType = 0;
    t_nTerrainVariant = 0;
    t_nHomeBase = 0;
    t_nSiteCount = 0;
    t_bVictory = 1;
    t_bAltPath = 0;
    t_nDimX = 1;   /* 10 x 10 cells per layer */
    t_nDimY = 1;
    t_nEleriumRoll = 25;
    t_nFoodRoll = 7;
    memset(t_aSites, 0, sizeof(t_aSites));
}

static TactTileTallyEnv TileEnv(void)
{
    TactTileTallyEnv env;

    memset(&env, 0, sizeof(env));
    env.apCells = t_apCells;
    env.pnMapDimX = &t_nDimX;
    env.pnMapDimY = &t_nDimY;
    env.pbAltOutcomePath = &t_bAltPath;
    env.pbMissionVictory = &t_bVictory;
    env.pnTerrainVariant = &t_nTerrainVariant;
    env.pnMissionType = &t_nMissionType;
    env.pnEleriumRoll = &t_nEleriumRoll;
    env.pnFoodRoll = &t_nFoodRoll;
    return env;
}

static TactUnitTallyEnv UnitEnv(void)
{
    TactUnitTallyEnv env;

    memset(&env, 0, sizeof(env));
    env.apSoldiers = t_apSoldier;
    env.pnSoldierCount = &t_nSoldier;
    env.apXcomUnits = t_apTactXcom;
    env.pnXcomCount = &t_nXcom;
    env.apEnemyUnits = t_apTactEnemy;
    env.pnEnemyCount = &t_nEnemy;
    env.apCivilianUnits = t_apTactCiv;
    env.pnCivilianCount = &t_nCiv;
    env.pbMissionVictory = &t_bVictory;
    env.pbAltOutcomePath = &t_bAltPath;
    return env;
}

static TactItemTallyEnv ItemEnv(void)
{
    TactItemTallyEnv env;

    memset(&env, 0, sizeof(env));
    env.apCells = t_apCells;
    env.apObjects = t_apObj;
    env.pnObjectCount = &t_nObjCount;
    env.apCraft = t_apCraft;
    env.pnCraftIndex = &t_nCraftIndex;
    env.apEnemyUnits = t_apTactEnemy;
    env.pnEnemyCount = &t_nEnemy;
    env.apXcomUnits = t_apTactXcom;
    env.pnXcomCount = &t_nXcom;
    env.apCivilianUnits = t_apTactCiv;
    env.pnCivilianCount = &t_nCiv;
    env.pbMissionVictory = &t_bVictory;
    env.pbAltOutcomePath = &t_bAltPath;
    return env;
}

static TactCraftTallyEnv CraftEnv(void)
{
    TactCraftTallyEnv env;

    memset(&env, 0, sizeof(env));
    env.apObjects = t_apObj;
    env.pnObjectCount = &t_nObjCount;
    env.apCraft = t_apCraft;
    env.pnCraftCount = &t_nCraftCount;
    env.pnCraftIndex = &t_nCraftIndex;
    env.pnMissionType = &t_nMissionType;
    env.pnHomeBase = &t_nHomeBase;
    env.aSites = t_aSites;
    env.pnSiteCount = &t_nSiteCount;
    env.pbMissionVictory = &t_bVictory;
    env.pbAltOutcomePath = &t_bAltPath;
    return env;
}

static void TestTileTallyGate(void)
{
    TactBattleStats stats;
    TactTileTallyEnv env;

    printf("Tact_TallyBattleStats @ 0x3348c - the gate\n");
    Tally_Reset();
    TCell(0, 0, 0)->pFloorSpr = NewSpr(TILE_FAC_ELERIUM);
    TCell(0, 0, 1)->pFloorSpr = NewSpr(TILE_FAC_ALLOYS);

    t_bAltPath = 1;
    env = TileEnv();
    Tact_TallyBattleStats(&env, &stats);
    TestEqI32("alternate outcome path gates the whole walk out", stats.nAlloys, 0);
    TestEqI32("and the counters are still zeroed, not left stale", stats.nPowerSource, 0);

    t_bAltPath = 0;
    t_bVictory = 0;
    env = TileEnv();
    Tact_TallyBattleStats(&env, &stats);
    TestEqI32("a lost mission gates the walk out too", stats.nAlloys, 0);

    t_bVictory = 1;
    env = TileEnv();
    Tact_TallyBattleStats(&env, &stats);
    TestEqI32("won and not aborted: the walk runs", stats.nAlloys, 1);
}

static void TestTileTallyCounts(void)
{
    TactBattleStats stats;
    TactTileTallyEnv env;

    printf("Tact_TallyBattleStats @ 0x3348c - the facility byte\n");
    Tally_Reset();

    TCell(0, 0, 0)->pFloorSpr = NewSpr(TILE_FAC_ELERIUM);       /* kind 2 */
    TCell(0, 0, 1)->pWallSprA = NewSpr(TILE_FAC_NAVIGATION);    /* kind 3 */
    TCell(0, 1, 0)->pWallSprB = NewSpr(TILE_FAC_REPRODUCTION);  /* kind 5 */
    TCell(0, 1, 1)->pCeilSpr = NewSpr(TILE_FAC_ENTERTAINMENT);  /* kind 7 */
    TCell(0, 2, 0)->pFloorSpr = NewSpr(TILE_FAC_SURGERY);       /* kind 8 */
    TCell(0, 2, 1)->pFloorSpr = NewSpr(TILE_FAC_EXAMINATION);   /* kind 9 */
    TCell(0, 3, 0)->pFloorSpr = NewSpr(TILE_FAC_ALLOYS);        /* kind 10 */
    TCell(0, 3, 1)->pFloorSpr = NewSpr(0);                      /* never tallied */
    TCell(0, 4, 0)->pFloorSpr = NewSpr(1);                      /* never tallied */
    TCell(0, 4, 1)->pFloorSpr = NewSpr(12);                     /* never tallied */
    TCell(0, 5, 0)->pFloorSpr = NewSpr(14);                     /* never tallied */
    /* All four sprite slots of one cell are read, in the floor / wallA / wallB / ceiling order. */
    TCell(1, 0, 0)->pFloorSpr = NewSpr(TILE_FAC_ALLOYS);
    TCell(1, 0, 0)->pWallSprA = NewSpr(TILE_FAC_ALLOYS);
    TCell(1, 0, 0)->pWallSprB = NewSpr(TILE_FAC_ALLOYS);
    TCell(1, 0, 0)->pCeilSpr = NewSpr(TILE_FAC_ALLOYS);
    /* A cell with no sprites at all must not be touched. */
    TCell(2, 0, 0)->pFloorSpr = NULL;
    TCell(2, 0, 0)->pCeilSpr = NULL;

    env = TileEnv();
    Tact_TallyBattleStats(&env, &stats);

    TestEqI32("kind 2 gives one power source", stats.nPowerSource, 1);
    TestEqI32("kind 2 adds the seeded Elerium amount", stats.nElerium, 25);
    TestEqI32("kind 3 is navigation", stats.nNavigation, 1);
    TestEqI32("kind 5 is reproduction", stats.nReproduction, 1);
    TestEqI32("kind 7 is entertainment", stats.nEntertainment, 1);
    TestEqI32("kind 8 is surgery", stats.nSurgery, 1);
    TestEqI32("kind 9 is examination", stats.nExamination, 1);
    TestEqI32("kind 10 is alloys, four slots of one cell count four times", stats.nAlloys, 5);
    /* Fifteen sprites were placed, eleven of them on a tallied kind: the four carrying kinds 0, 1,
     * 12 and 14 contribute nothing to any counter. */
    TestEqI32("kinds 0, 1, 12 and 14 feed no counter at all",
              stats.nPowerSource + stats.nNavigation + stats.nReproduction
                  + stats.nEntertainment + stats.nSurgery + stats.nExamination + stats.nAlloys,
              11);
    TestEqI32("the food counter is not a tile count", stats.nFood, 0);

    /* The walk covers 10 * nMapDimY rows and 10 * nMapDimX columns in every one of four layers. */
    t_nDimX = 1;
    t_nDimY = 1;
    TCell(3, 9, 9)->pFloorSpr = NewSpr(TILE_FAC_ALLOYS);
    TCell(3, 10, 0)->pFloorSpr = NewSpr(TILE_FAC_ALLOYS);
    env = TileEnv();
    Tact_TallyBattleStats(&env, &stats);
    TestEqI32("row 9 of layer 3 is inside the walk", stats.nAlloys, 6);
    TestEqI32("row 10 is outside it", stats.nNavigation, 1);
}

static void TestTileTallyFood(void)
{
    TactBattleStats stats;
    TactTileTallyEnv env;

    printf("Tact_TallyBattleStats @ 0x3373C - the food tail\n");

    Tally_Reset();
    t_nTerrainVariant = 4;
    env = TileEnv();
    Tact_TallyBattleStats(&env, &stats);
    TestEqI32("terrain variant above 3 seeds the food line", stats.nFood, 7);

    Tally_Reset();
    t_nTerrainVariant = 0;
    t_nMissionType = PUFO_MISSION_TYPE_ALIEN_BASE;
    env = TileEnv();
    Tact_TallyBattleStats(&env, &stats);
    TestEqI32("an alien base mission seeds it whatever the variant", stats.nFood, 7);

    Tally_Reset();
    t_nTerrainVariant = 3;
    t_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    env = TileEnv();
    Tact_TallyBattleStats(&env, &stats);
    TestEqI32("variant 3 and a non-alien base leaves it zero", stats.nFood, 0);

    Tally_Reset();
    t_nTerrainVariant = 9;
    t_bVictory = 0;
    env = TileEnv();
    Tact_TallyBattleStats(&env, &stats);
    TestEqI32("the gate returns before the tail, so the food line stays zero", stats.nFood, 0);
}

static void TestUnitTallyMarks(void)
{
    TactUnitResults res;
    TactUnitTallyEnv env;

    printf("Tact_TallyUnitResults @ 0x33780 - the zone marks\n");
    Tally_Reset();

    t_apSoldier[0] = NewTactUnit(1, 0, 2, 0, 0, 0, 10);
    t_apSoldier[1] = NewTactUnit(2, 0, 2, 0, 0, 0, 10);
    t_nSoldier = 2;
    t_apSoldier[0]->bInZone = 1;
    t_apSoldier[1]->bInZone = 1;

    t_apTactXcom[0] = NewTactUnit(10, 2, 2, 5, 5, 0, 10);   /* in box, type <= 8 */
    t_apTactXcom[1] = NewTactUnit(11, 9, 2, 5, 5, 0, 10);   /* in box, type > 8 */
    t_apTactXcom[2] = NewTactUnit(12, 2, 2, 3, 5, 0, 10);   /* one below the box */
    t_apTactXcom[3] = NewTactUnit(13, 2, 2, 6, 6, 0, 10);   /* exactly the far corner */
    t_nXcom = 4;
    t_apTactXcom[0]->uMissions = 1;
    t_apTactXcom[1]->uMissions = 1;
    t_apTactXcom[2]->uMissions = 1;
    t_apTactXcom[3]->uMissions = 1;

    t_apTactEnemy[0] = NewTactUnit(20, 9, 2, 5, 5, 0, 10);
    t_nEnemy = 1;
    t_apTactEnemy[0]->bInZone = 1;   /* the enemy roster is never marked */

    t_apTactCiv[0] = NewTactUnit(30, 2, 2, 5, 5, 0, 10);   /* live, in box, type <= 8 */
    t_apTactCiv[1] = NewTactUnit(31, 2, 2, 5, 5, 0, 0);    /* dead, in box */
    t_apTactCiv[2] = NewTactUnit(32, 0x10, 2, 5, 5, 0, 10);/* live, in box, type above 8 */
    t_nCiv = 3;
    t_apTactCiv[0]->uMissions = 1;
    t_apTactCiv[1]->uMissions = 1;
    t_apTactCiv[2]->uMissions = 1;

    env = UnitEnv();
    Tact_TallyUnitResults(&env, 4, 4, 6, 6, &res);

    TestEqI32("the geoscape roster loses its zone mark", t_apSoldier[0]->bInZone, 0);
    TestEqI32("every entry of it", t_apSoldier[1]->bInZone, 0);
    TestEqI32("a live Xcom unit inside the box is marked", t_apTactXcom[0]->bInZone, 1);
    TestEqI32("and its mission count rises", t_apTactXcom[0]->uMissions, 2);
    TestEqI32("a unit above type 8 is not marked even inside the box",
              t_apTactXcom[1]->bInZone, 0);
    TestEqI32("its mission count does not rise", t_apTactXcom[1]->uMissions, 1);
    TestEqI32("one column below the box is not marked", t_apTactXcom[2]->bInZone, 0);
    TestEqI32("the enemy roster keeps whatever mark it had", t_apTactEnemy[0]->bInZone, 1);
    TestEqI32("a live civilian inside the box is marked", t_apTactCiv[0]->bInZone, 1);
    TestEqI32("a dead one is not", t_apTactCiv[1]->bInZone, 0);
    TestEqI32("the box bounds are inclusive: the far corner counts",
              t_apTactXcom[3]->bInZone, 1);
    TestEqI32("the marking pass has the same type 8 ceiling as the Xcom one, so a real civilian "
              "of type 0x10 is never marked",
              t_apTactCiv[2]->bInZone, 0);
    TestEqI32("and gains no mission", t_apTactCiv[2]->uMissions, 1);

    Tally_Reset();
    t_bVictory = 0;
    t_bAltPath = 0;
    t_apTactXcom[0] = NewTactUnit(10, 2, 2, 5, 5, 0, 10);
    t_nXcom = 1;
    t_apTactXcom[0]->bInZone = 1;
    t_apTactXcom[0]->uMissions = 1;
    env = UnitEnv();
    Tact_TallyUnitResults(&env, 4, 4, 6, 6, &res);
    TestEqI32("with both outcome flags clear the mark is cleared and not re-set",
              t_apTactXcom[0]->bInZone, 0);
    TestEqI32("and no mission is counted", t_apTactXcom[0]->uMissions, 1);
}

static void TestUnitTallyClassify(void)
{
    TactUnitResults res;
    TactUnitTallyEnv env;

    printf("Tact_TallyUnitResults @ 0x33A68 - the debrief lines\n");

    /* A won mission: Map_bMissionVictory set, alternate path clear. Only dead units are lost, and a
     * live civilian is rescued. */
    Tally_Reset();
    t_apTactXcom[0] = NewTactUnit(1, 0, 2, 5, 5, 0, 0);   /* dead operative */
    t_apTactXcom[1] = NewTactUnit(2, 1, 2, 5, 5, 0, 9);   /* live operative, in zone */
    t_apTactXcom[2] = NewTactUnit(3, 4, 2, 5, 5, 0, 0);   /* dead tank */
    t_apTactXcom[3] = NewTactUnit(4, 7, 2, 5, 5, 0, 5);   /* live hovertank, in zone */
    t_nXcom = 4;
    t_apTactXcom[1]->bInZone = 1;
    t_apTactXcom[3]->bInZone = 1;
    t_apTactCiv[0] = NewTactUnit(5, 9, 2, 5, 5, 0, 0);    /* dead alien */
    t_apTactCiv[1] = NewTactUnit(6, 0x15, 2, 5, 5, 0, 4); /* live alien */
    t_apTactCiv[2] = NewTactUnit(7, 0x10, 2, 5, 5, 0, 6); /* live civilian */
    t_apTactCiv[3] = NewTactUnit(8, 0x11, 2, 5, 5, 0, 0); /* dead civilian */
    t_nCiv = 4;

    env = UnitEnv();
    Tact_TallyUnitResults(&env, 4, 4, 6, 6, &res);
    TestEqI32("one operative lost", res.nOperativesLost, 1);
    TestEqI32("one tank lost", res.nTanksLost, 1);
    TestEqI32("one alien killed", res.nAliensKilled, 1);
    TestEqI32("one civilian rescued", res.nCiviliansRescued, 1);
    TestEqI32("one civilian killed", res.nCiviliansKilled, 1);

    /* Both flags clear: every operative and every hardware unit is counted as lost whatever its
     * hit points, and no civilian can be rescued. */
    Tally_Reset();
    t_bVictory = 0;
    t_bAltPath = 0;
    t_apTactXcom[0] = NewTactUnit(1, 0, 2, 5, 5, 0, 9);
    t_apTactXcom[1] = NewTactUnit(2, 8, 2, 5, 5, 0, 9);
    t_nXcom = 2;
    t_apTactCiv[0] = NewTactUnit(3, 0x10, 2, 5, 5, 0, 9);
    t_nCiv = 1;
    env = UnitEnv();
    Tact_TallyUnitResults(&env, 4, 4, 6, 6, &res);
    TestEqI32("a live operative still counts as lost", res.nOperativesLost, 1);
    TestEqI32("a live hovertank too", res.nTanksLost, 1);
    TestEqI32("a live civilian is not rescued", res.nCiviliansRescued, 0);
    TestEqI32("it lands on the killed line", res.nCiviliansKilled, 1);

    /* Alternate path: the zone mark is what decides, so a unit the marking pass could not reach is
     * lost and one it marked is not. */
    Tally_Reset();
    t_bVictory = 1;
    t_bAltPath = 1;
    t_apTactXcom[0] = NewTactUnit(1, 0, 2, 5, 5, 0, 9);   /* inside the box -> marked */
    t_apTactXcom[1] = NewTactUnit(2, 0, 2, 30, 30, 0, 9); /* outside it -> stays unmarked */
    t_nXcom = 2;
    env = UnitEnv();
    Tact_TallyUnitResults(&env, 4, 4, 6, 6, &res);
    TestEqI32("in zone and alive is not lost", t_apTactXcom[0]->bInZone, 1);
    TestEqI32("out of zone is lost", res.nOperativesLost, 1);
}

static void TestItemTallyChain(void)
{
    i32 nScore;
    TactItemTallyEnv env;
    MapCell *pCell;

    printf("Tact_TallyItemRecovery @ 0x33DF8 - the cell item chain\n");
    Tally_Reset();

    t_apObj[10]->bTwoHanded = 0;   /* a single-handed weapon */
    t_apObj[10]->bAmmoCap = 12;
    t_apObj[36]->bTwoHanded = 1;   /* a clip */
    t_apObj[36]->bAmmoCap = 12;
    t_apObj[59]->bTwoHanded = 2;   /* a Sectoid corpse */
    t_apObj[59]->uSortKey = 59;
    t_apObj[70]->bTwoHanded = 2;   /* a human corpse, outside the 59..69 block */
    t_apObj[70]->uSortKey = 70;

    pCell = TCell(0, 5, 5);
    CellAddItem(pCell, NewItem(t_apObj[70], 70, 0, NULL));
    CellAddItem(pCell, NewItem(t_apObj[59], 59, 0, NULL));
    CellAddItem(pCell, NewItem(t_apObj[36], 36, 11, NULL));
    CellAddItem(pCell, NewItem(t_apObj[36], 36, 12, NULL));
    CellAddItem(pCell, NewItem(t_apObj[10], 36, 0xFF, NULL));
    CellAddItem(pCell, NewItem(t_apObj[10], 36, 5, NULL));
    CellAddItem(pCell, NewItem(t_apObj[10], 36, 12, NULL));

    /* Outside the box: the upper bound is exclusive, so x == nX1 is not walked. This item would
     * credit apObjects[11] if the walk reached it. */
    t_apObj[11]->bTwoHanded = 0;
    CellAddItem(TCell(0, 5, 8), NewItem(t_apObj[11], 11, 0, NULL));

    env = ItemEnv();
    Tact_TallyItemRecovery(&env, 4, 4, 8, 8, &nScore);

    TestEqI32("a single-handed weapon with a full clip is recovered", t_apObj[10]->uRecovCount, 3);
    TestEqI32("and the clip object is credited once, for the full clip",
              t_apObj[36]->uRecovCount, 2);
    TestEqI32("an alien corpse with nothing holding it is recovered",
              t_apObj[59]->uRecovCount, 1);
    TestEqI32("a human corpse is not, whatever its flags", t_apObj[70]->uRecovCount, 0);
    TestEqI32("the box upper bound is exclusive", t_apObj[11]->uRecovCount, 0);
    TestEqI32("the cell walk adds no debrief score", nScore, 0);

    /* A corpse held by a linked soldier is not recovered. */
    Tally_Reset();
    t_apObj[59]->bTwoHanded = 2;
    t_apObj[59]->uSortKey = 59;
    t_apTactEnemy[0] = NewTactUnit(1, 9, 2, 5, 5, 0, 0);
    t_nEnemy = 1;
    CellAddItem(TCell(0, 5, 5), NewItem(t_apObj[59], 59, 0, t_apTactEnemy[0]));
    env = ItemEnv();
    Tact_TallyItemRecovery(&env, 4, 4, 8, 8, &nScore);
    TestEqI32("a corpse still linked to a soldier is not recovered",
              t_apObj[59]->uRecovCount, 0);
}

static void TestItemTallyAliens(void)
{
    i32 nScore;
    TactItemTallyEnv env;

    printf("Tact_TallyItemRecovery @ 0x33F08 - the live alien credits\n");
    Tally_Reset();

    t_apTactCiv[0] = NewTactUnit(1, 9, 2, 5, 5, 0, 10);    /* Sectoid soldier, live */
    t_apTactCiv[1] = NewTactUnit(2, 0xA, 7, 5, 5, 0, 10);  /* Muton medic, live */
    t_apTactCiv[2] = NewTactUnit(3, 9, 6, 5, 5, 0, 10);    /* Sectoid, rank the race has no record for */
    t_apTactCiv[3] = NewTactUnit(4, 0x10, 2, 5, 5, 0, 10); /* a civilian, not an alien */
    t_nCiv = 4;
    CellAddCorpseItem(TCell(0, 5, 5), t_apTactCiv[0]);
    CellAddCorpseItem(TCell(0, 5, 5), t_apTactCiv[1]);
    CellAddCorpseItem(TCell(0, 5, 5), t_apTactCiv[2]);
    CellAddCorpseItem(TCell(0, 5, 5), t_apTactCiv[3]);

    env = ItemEnv();
    Tact_TallyItemRecovery(&env, 4, 4, 8, 8, &nScore);

    TestEqI32("a live Sectoid soldier credits apObjects[77]", t_apObj[77]->uRecovCount, 1);
    TestEqI32("a live Muton medic credits the medic block, apObjects[145]",
              t_apObj[145]->uRecovCount, 1);
    TestEqI32("a rank the race has no record for still scores", nScore, 3);
    TestEqI32("but credits no object", t_apObj[78]->uRecovCount, 0);
    TestEqI32("a civilian is neither credited nor scored", t_apObj[150]->uRecovCount, 0);

    /* Dead, unlinked, or outside the box: no credit. */
    Tally_Reset();
    t_apTactCiv[0] = NewTactUnit(1, 9, 2, 5, 5, 0, 0);     /* dead */
    t_apTactCiv[1] = NewTactUnit(2, 9, 2, 5, 5, 0, 10);    /* live, nothing linked */
    t_apTactCiv[2] = NewTactUnit(3, 9, 2, 30, 30, 0, 10);  /* live, linked, outside the box */
    t_nCiv = 3;
    CellAddCorpseItem(TCell(0, 5, 5), t_apTactCiv[0]);
    CellAddCorpseItem(TCell(0, 30, 30), t_apTactCiv[2]);
    env = ItemEnv();
    Tact_TallyItemRecovery(&env, 4, 4, 8, 8, &nScore);
    TestEqI32("a dead alien is not credited", nScore, 0);
    TestEqI32("so nothing reaches its race record", t_apObj[77]->uRecovCount, 0);

    /* The Xcom half of the walk needs no cell test at all. */
    Tally_Reset();
    t_apTactXcom[0] = NewTactUnit(1, 0x15, 2, 40, 40, 0, 10); /* Sectopod parked outside the box */
    t_nXcom = 1;
    env = ItemEnv();
    Tact_TallyItemRecovery(&env, 4, 4, 8, 8, &nScore);
    TestEqI32("an Xcom-side alien is credited whatever its cell",
              t_apObj[104]->uRecovCount, 1);
    TestEqI32("and scores", nScore, 1);

    Tally_Reset();
    t_bVictory = 0;
    t_bAltPath = 0;
    t_apTactCiv[0] = NewTactUnit(1, 9, 2, 5, 5, 0, 10);
    t_nCiv = 1;
    CellAddCorpseItem(TCell(0, 5, 5), t_apTactCiv[0]);
    env = ItemEnv();
    Tact_TallyItemRecovery(&env, 4, 4, 8, 8, &nScore);
    TestEqI32("both outcome flags clear skips the civilian test", nScore, 0);
}

static void TestItemTallyHwp(void)
{
    i32 nScore;
    TactItemTallyEnv env;
    TactItem Clip;

    printf("Tact_TallyItemRecovery @ 0x342A8 - hardware soldiers\n");
    Tally_Reset();

    Clip.nAmmo = 20;
    t_aCraft[0].anCargoQty[0] = 999;
    t_aCraft[0].anCargoQty[3] = 999;

    t_apTactXcom[0] = NewTactUnit(1, 4, 2, 5, 5, 0, 10);  /* Tank cannon, in box */
    t_apTactXcom[1] = NewTactUnit(2, 6, 2, 5, 5, 0, 10);  /* Tank laser, unlimited ammo */
    t_apTactXcom[2] = NewTactUnit(3, 8, 2, 5, 5, 0, 10);  /* Hovertank blaster, in box */
    t_apTactXcom[3] = NewTactUnit(4, 5, 2, 30, 30, 0, 10);/* Tank rocket, outside the box */
    t_nXcom = 4;
    t_apTactXcom[0]->pHandL = &Clip;
    t_apTactXcom[1]->pHandL = &Clip;
    t_apTactXcom[2]->pHandL = &Clip;
    t_apTactXcom[3]->pHandL = &Clip;

    env = ItemEnv();
    Tact_TallyItemRecovery(&env, 4, 4, 8, 8, &nScore);

    TestEqI32("the deployed craft starts with an empty hold", t_aCraft[0].anCargoQty[0], 20);
    TestEqI32("the tank cannon weapon is recovered", t_apObj[31]->uRecovCount, 1);
    TestEqI32("the tank laser is recovered", t_apObj[33]->uRecovCount, 1);
    TestEqI32("the hovertank blaster is recovered", t_apObj[35]->uRecovCount, 1);
    TestEqI32("the tank laser has no cargo column", t_aCraft[0].anCargoQty[1], 0);
    TestEqI32("the blaster clip goes into column 3", t_aCraft[0].anCargoQty[3], 20);
    TestEqI32("outside the box is not recovered", t_apObj[32]->uRecovCount, 0);
    TestEqI32("hardware adds no debrief score", nScore, 0);

    /* Both outcome flags clear: the hold is still emptied, then the pass aborts at the first
     * hardware unit, so nothing is credited. */
    Tally_Reset();
    t_bVictory = 0;
    t_bAltPath = 0;
    t_aCraft[0].anCargoQty[0] = 999;
    t_apTactXcom[0] = NewTactUnit(1, 4, 2, 5, 5, 0, 10);
    t_apTactXcom[1] = NewTactUnit(2, 4, 2, 5, 5, 0, 10);
    t_nXcom = 2;
    t_apTactXcom[0]->pHandL = &Clip;
    t_apTactXcom[1]->pHandL = &Clip;
    env = ItemEnv();
    Tact_TallyItemRecovery(&env, 4, 4, 8, 8, &nScore);
    TestEqI32("the hold is emptied before the abort", t_aCraft[0].anCargoQty[0], 0);
    TestEqI32("and no weapon is credited", t_apObj[31]->uRecovCount, 0);
}

static void TestCraftTallyCap(void)
{
    i32 bLoot;
    TactCraftTallyEnv env;

    printf("Tact_TallyCraftRecovery @ 0x344b4 - capping against the cargo\n");
    Tally_Reset();

    t_nCraftIndex = 1;
    t_apObj[0]->aItemQty[1] = 10;
    t_apObj[0]->uRecovCount = 6;   /* fits, so it is absorbed and leaves nothing */
    t_apObj[1]->aItemQty[1] = 3;
    t_apObj[1]->uRecovCount = 8;   /* does not fit, five carry on */
    t_apObj[2]->aItemQty[1] = 0;
    t_apObj[2]->uRecovCount = 4;   /* no capacity at all */

    bLoot = 0;
    env = CraftEnv();
    Tact_TallyCraftRecovery(&env, &bLoot);

    TestEqI32("a recovery that fits is written into the craft quantity",
              t_apObj[0]->aItemQty[1], 6);
    TestEqI32("and clears the recovered count", t_apObj[0]->uRecovCount, 0);
    TestEqI32("a recovery that does not fit leaves the quantity alone",
              t_apObj[1]->aItemQty[1], 3);
    TestEqI32("and subtracts the capacity", t_apObj[1]->uRecovCount, 0);
    TestEqI32("the remainder lands in the working site record",
              t_aSites[0].auItemQty[1], 5);
    TestEqI32("indexed by the object index", t_aSites[0].auItemQty[2], 4);
    TestEqI32("an object that fits leaves nothing for the site",
              t_aSites[0].auItemQty[0], 0);
    TestEqI32("the site is marked as carrying loot", t_aSites[0].bHasLoot, 1);
    TestEqI32("and the out-param is set", bLoot, 1);

    Tally_Reset();
    t_bVictory = 0;
    t_bAltPath = 0;
    t_apObj[1]->aItemQty[1] = 3;
    t_apObj[1]->uRecovCount = 8;
    bLoot = 0;
    env = CraftEnv();
    Tact_TallyCraftRecovery(&env, &bLoot);
    TestEqI32("both outcome flags clear skips the whole tally", t_apObj[1]->uRecovCount, 8);
    TestEqI32("and records no loot", bLoot, 0);
}

static void TestCraftTallyDefendBase(void)
{
    i32 bLoot;
    TactCraftTallyEnv env;

    printf("Tact_TallyCraftRecovery @ 0x344D4 - the Xcom base defence\n");
    Tally_Reset();

    t_nMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
    t_nHomeBase = 2;
    t_nCraftCount = 3;
    t_aCraft[0].bHomeBase = 2;   /* listed */
    t_aCraft[1].bHomeBase = 1;   /* not */
    t_aCraft[2].bHomeBase = 2;   /* listed */

    t_apObj[0]->aItemQty[0] = 5;
    t_apObj[0]->aItemQty[2] = 4;
    t_apObj[0]->uRecovCount = 7;   /* 7 - 5 = 2, then 2 fits into slot 2 */
    t_apObj[1]->aItemQty[0] = 0;
    t_apObj[1]->aItemQty[2] = 0;
    t_apObj[1]->uRecovCount = 9;   /* nothing caps it */

    bLoot = 0;
    env = CraftEnv();
    Tact_TallyCraftRecovery(&env, &bLoot);

    TestEqI32("the first craft caps at its own quantity", t_apObj[0]->aItemQty[0], 5);
    TestEqI32("the second takes what is left", t_apObj[0]->aItemQty[2], 2);
    TestEqI32("the base quantity gains nothing when the remainder is zero",
              t_apObj[0]->auPerBaseVal[2], 0);
    TestEqI32("an uncapped remainder goes into the defended base's quantity",
              t_apObj[1]->auPerBaseVal[2], 9);
    TestEqI32("and the recovered count is deliberately left in place",
              t_apObj[1]->uRecovCount, 9);
    TestEqI32("the site record is not touched", t_aSites[0].bHasLoot, 0);
    TestEqI32("and no loot is reported", bLoot, 0);
}

/* --- Tact_ClearSoldierBattleFlags @ 0x346e0 -------------------------------- */

static Soldier   s_aClear[4];
static Soldier  *s_apClear[4];
static u16       s_nClear;
static u8        s_bPromotionReport;
static UiPanel   s_PanelName;
static UiPanel   s_PanelRank;

/* aszTextE @ 0xB2200 is a loaded TEXTS block (six 20-byte entries), not image bytes, so the
 * caller supplies the text and the test checks the 20 * bRankType indexing, not the wording. */
static char s_asRankName[6][20];

static void ClearFixture(void)
{
    i32 i;

    for (i = 0; i < 4; ++i) {
        memset(&s_aClear[i], 0, sizeof(s_aClear[i]));
        s_apClear[i] = &s_aClear[i];
    }
    s_nClear = 4;
    s_bPromotionReport = 0;

    (void)UiPanel_ctor(&s_PanelName, 8);
    (void)UiPanel_ctor(&s_PanelRank, 8);
    s_PanelName.nLineCount = 0;
    s_PanelRank.nLineCount = 0;

    for (i = 0; i < 6; ++i) {
        snprintf(s_asRankName[i], sizeof(s_asRankName[i]), "rank%d", i);
    }
}

/* A live, operative, in-zone soldier with a known base array and every auStat slot pre-filled
 * with a sentinel, so a stat the ARM does not recompute is still visible as the sentinel. */
static Soldier *ClearMake(i32 i, u8 nRank, i32 nKills, i32 nMissions, i32 nClass)
{
    Soldier *pUnit = &s_aClear[i];

    pUnit->nUnitType = 0;
    pUnit->uHp = 50;
    pUnit->bStatus = 0;   /* UNIT_STATUS_OPERATIVE */
    pUnit->bInZone = 1;
    pUnit->bRankType = nRank;
    pUnit->uKills = (u16)nKills;
    pUnit->uMissions = (u16)nMissions;
    pUnit->nClass = nClass;
    for (i32 k = 0; k < 9; ++k) {
        pUnit->auStatBase[k] = (u16)(100 + k);
    }
    for (i32 k = 0; k < 10; ++k) {
        pUnit->auStat[k] = 777;
    }
    snprintf(pUnit->szName, sizeof(pUnit->szName), "unit%d", i);
    return pUnit;
}

static TactBattleFlagsEnv ClearEnv(void)
{
    TactBattleFlagsEnv env;

    memset(&env, 0, sizeof(env));
    env.apSoldiers = s_apClear;
    env.pnSoldierCount = &s_nClear;
    env.pPanelName = &s_PanelName;
    env.pPanelRank = &s_PanelRank;
    env.panRankName = &s_asRankName[0][0];
    env.pbPromotionReport = &s_bPromotionReport;
    return env;
}

static void TestClearFlagsStatGrowth(void)
{
    static const u16 aWant0[10] = { 124, 113, 109, 110, 110, 113, 777, 777, 119, 117 };
    static const u16 aWant5[10] = { 124, 109, 109, 110, 110, 110, 777, 777, 115, 117 };
    i32 i;

    printf("Tact_ClearSoldierBattleFlags - the per-class stat refresh\n");
    ClearFixture();
    ClearMake(0, 0, 0, 10, 0);
    ClearMake(1, 0, 0, 10, 5);
    ClearMake(2, 0, 0, 10, 3)->bInZone = 0;   /* outside the zone: no refresh at all */
    ClearMake(3, 0, 0, 10, 3)->bStatus = 2;   /* not an operative: no refresh at all */

    {
        TactBattleFlagsEnv env = ClearEnv();

        Tact_ClearSoldierBattleFlags(&env);
    }

    for (i = 0; i < 10; ++i) {
        TestEqU16("class 0 auStat", s_aClear[0].auStat[i], aWant0[i]);
    }
    for (i = 0; i < 10; ++i) {
        TestEqU16("class 5 auStat", s_aClear[1].auStat[i], aWant5[i]);
    }
    for (i = 0; i < 10; ++i) {
        TestEqU16("out-of-zone soldier keeps its stats", s_aClear[2].auStat[i], 777);
        TestEqU16("non-operative soldier keeps its stats", s_aClear[3].auStat[i], 777);
    }
    /* auStat[6] and auStat[7] are the two slots the ARM never recomputes. */
    TestEqU16("auStat[6] is never recomputed", s_aClear[0].auStat[6], 777);
    TestEqU16("auStat[7] is never recomputed", s_aClear[0].auStat[7], 777);

    UiPanel_FreeLines(&s_PanelName);
    UiPanel_FreeLines(&s_PanelRank);
}

static void TestClearFlagsPromotionReport(void)
{
    printf("Tact_ClearSoldierBattleFlags - the promotion report rows\n");
    ClearFixture();
    /* Mission score is 5 * (kills + 2 * missions): 50 clears the 45 rookie gate, 45 and 40 do
     * not. The rank-1 -> rank-2 rung ran before this one, when the roster held no rank-1
     * soldier, so nothing climbs further. */
    ClearMake(0, 0, 10, 0, 0);
    ClearMake(1, 0, 9, 0, 0);
    ClearMake(2, 0, 8, 0, 0);
    ClearMake(3, 0, 10, 0, 0)->bInZone = 0;  /* promoted (score 50), but outside the zone */
    s_aClear[1].bRankDirty = 1;              /* a stale flag the caller left behind */

    {
        TactBattleFlagsEnv env = ClearEnv();

        Tact_ClearSoldierBattleFlags(&env);
    }

    TestEqI32("the promoted soldier changes rank", s_aClear[0].bRankType, 1);
    TestEqI32("a score of exactly 45 does not", s_aClear[1].bRankType, 0);
    TestEqI32("the out-of-zone soldier still changes rank", s_aClear[3].bRankType, 1);
    TestEqI32("one report row", s_PanelName.nLineCount, 1);
    TestEqI32("one rank row", s_PanelRank.nLineCount, 1);
    TestCheck("the name column holds szName", strcmp((char *)s_PanelName.apLines[0], "unit0") == 0);
    TestCheck("the rank column holds aszTextE[20 * bRankType]",
              strcmp((char *)s_PanelRank.apLines[0], "rank1") == 0);
    TestCheck("the out-of-zone soldier gets no row",
              s_PanelName.apLines[1][0] == 0 && s_PanelRank.apLines[1][0] == 0);
    TestEqI32("Map_bPromotionReport set", s_bPromotionReport, 1);

    /* bRankDirty is cleared before the ladder runs, so a flag the caller left behind is not a
     * report row. */
    TestCheck("the stale flag is cleared", s_aClear[1].bRankDirty == 0);

    for (i32 i = 0; i < 4; ++i) {
        TestEqI32("bRankDirty ends clear", s_aClear[i].bRankDirty, 0);
    }

    UiPanel_FreeLines(&s_PanelName);
    UiPanel_FreeLines(&s_PanelRank);
}

static void TestClearFlagsReportEmpty(void)
{
    printf("Tact_ClearSoldierBattleFlags - nobody promoted, no report\n");
    ClearFixture();
    ClearMake(0, 0, 8, 0, 0);   /* 40 */
    ClearMake(1, 0, 7, 0, 0);   /* 35 */
    ClearMake(2, 0, 6, 0, 0);   /* 30 */
    ClearMake(3, 0, 5, 0, 0);   /* 25 */

    {
        TactBattleFlagsEnv env = ClearEnv();

        Tact_ClearSoldierBattleFlags(&env);
    }

    TestEqI32("no rank change", s_aClear[0].bRankType, 0);
    TestEqI32("no name rows", s_PanelName.nLineCount, 0);
    TestEqI32("no rank rows", s_PanelRank.nLineCount, 0);
    TestEqI32("Map_bPromotionReport stays clear", s_bPromotionReport, 0);

    UiPanel_FreeLines(&s_PanelName);
    UiPanel_FreeLines(&s_PanelRank);
}

static void TestClearFlagsReportOrder(void)
{
    printf("Tact_ClearSoldierBattleFlags - rows follow roster order\n");
    ClearFixture();
    ClearMake(0, 0, 20, 0, 0);  /* 100 */
    ClearMake(1, 0, 18, 0, 0);  /* 90  */
    ClearMake(2, 0, 16, 0, 0);  /* 80  */
    ClearMake(3, 0, 14, 0, 0);  /* 70  */

    {
        TactBattleFlagsEnv env = ClearEnv();

        Tact_ClearSoldierBattleFlags(&env);
    }

    TestEqI32("four name rows", s_PanelName.nLineCount, 4);
    TestEqI32("four rank rows", s_PanelRank.nLineCount, 4);
    for (i32 i = 0; i < 4; ++i) {
        char szWant[16];
        snprintf(szWant, sizeof(szWant), "unit%d", i);
        TestCheck("the name column follows the roster",
                  strcmp((char *)s_PanelName.apLines[i], szWant) == 0);
        TestCheck("every row shows the rank it reached",
                  strcmp((char *)s_PanelRank.apLines[i], "rank1") == 0);
        TestEqI32("promoted to rank 1", s_aClear[i].bRankType, 1);
    }

    UiPanel_FreeLines(&s_PanelName);
    UiPanel_FreeLines(&s_PanelRank);
}

int main(void)
{
    TestBankGroups();
    TestPanelsAndGlobals();
    TestCellFree();
    TestKeepSomePath();
    TestUnitRemoval();
    TestTileTallyGate();
    TestTileTallyCounts();
    TestTileTallyFood();
    TestUnitTallyMarks();
    TestUnitTallyClassify();
    TestItemTallyChain();
    TestItemTallyAliens();
    TestItemTallyHwp();
    TestCraftTallyCap();
    TestCraftTallyDefendBase();
    TestClearFlagsStatGrowth();
    TestClearFlagsPromotionReport();
    TestClearFlagsReportEmpty();
    TestClearFlagsReportOrder();

    printf("test_tactend: %d checks, %d failures\n", g_nChecks, g_nFails);
    return g_nFails == 0 ? 0 : 1;
}
