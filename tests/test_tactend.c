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
static i32        s_nEnemy;
static Soldier   *s_apXcom[4];
static i32        s_nXcom;
static u8         s_bMissionMode;
static u8         s_bMissionAborted;
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
    s_bMissionAborted = 0;
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
    env.pbMissionAborted = &s_bMissionAborted;
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

int main(void)
{
    TestBankGroups();
    TestPanelsAndGlobals();
    TestCellFree();
    TestKeepSomePath();
    TestUnitRemoval();

    printf("test_tactend: %d checks, %d failures\n", g_nChecks, g_nFails);
    return g_nFails == 0 ? 0 : 1;
}
