/* Tests for the live tactical context (the IDB `Map` block) and the real
 * Tact_ProcessMoveAction callbacks.
 *
 * Evidence: the Map type layout (152632 bytes, context base 0x1139b8),
 * Map.apCells @ 0x15338, StatusMsg_Push @ 0x2dda4, Unit_bArmorTuWarn @ 0x4e254,
 * Unit_UpdateVisibility @ 0x48580, Tact_ProcessMoveAction @ 0x4f3f0. */
#include "game/pufo_mapctx.h"
#include "game/pufo_boot.h"
#include "game/pufo_combat.h"
#include "game/pufo_vistry.h"
#include "util/pufo_str.h"
#include "pufo_common.h"

#include <stdio.h>
#include <string.h>

static int g_nChecks;
static int g_nFails;

static void Check(const char *pWhat, int bOk)
{
    ++g_nChecks;
    if (!bOk) {
        ++g_nFails;
        printf("  FAIL %s\n", pWhat);
    }
}

/* The original's grid is 4 layers of 60 x 60 cells, which is the whole grid at
 * nMapDimX = nMapDimY = 6. */
static GameMap g_Map;
static TactState g_State;
static Soldier g_aUnits[4];
static TactItem g_aItems[2];
static ObjectRecFull g_aObjs[2];
static Mc1Rec g_aCeil;

/* A synthetic aTextRecs @ 0xb38d0: 205-byte records, record n at 205 * n. */
static u8 g_aText[205 * 60];

static VisRay g_aRays[PUFO_VIS_RAY_COUNT];
static u8 g_aSteps[PUFO_VIS_TABLE_BYTES];
static LosTables g_Tables;
static SpotCtx g_Spot;
static i32 g_nFlag1036;
static Soldier *g_pScratch;
static UnitRosters g_Rosters;
static MoveActionWiring g_Wiring;
static MoveActionCtx g_Ctx;

static void FillText(void)
{
    memset(g_aText, 0, sizeof(g_aText));
    pufo_strcpy((char *)g_aText + 205 * 1, "not enough time");
    pufo_strcpy((char *)g_aText + 205 * 2, "armour cost");
    pufo_strcpy((char *)g_aText + 205 * 4, "throw failed");
    pufo_strcpy((char *)g_aText + 205 * 48, "tu warning head");
    pufo_strcpy((char *)g_aText + 205 * 49, "tu slot 0");
    pufo_strcpy((char *)g_aText + 205 * 50, "tu slot 1");
    pufo_strcpy((char *)g_aText + 205 * 51, "tu slot 2");
}

static void Reset(void)
{
    memset(&g_Map, 0, sizeof(g_Map));
    memset(&g_State, 0, sizeof(g_State));
    memset(g_aUnits, 0, sizeof(g_aUnits));
    memset(g_aItems, 0, sizeof(g_aItems));
    memset(g_aObjs, 0, sizeof(g_aObjs));
    memset(&g_aCeil, 0, sizeof(g_aCeil));
    memset(&g_Spot, 0, sizeof(g_Spot));
    memset(&g_Tables, 0, sizeof(g_Tables));
    memset(&g_Rosters, 0, sizeof(g_Rosters));
    memset(&g_Wiring, 0, sizeof(g_Wiring));
    memset(&g_Ctx, 0, sizeof(g_Ctx));

    g_Map.geom.nDimX = 6;
    g_Map.geom.nDimY = 6;
    g_Map.nMapLayers = 4;
    Map_InitCells(&g_Map);

    FillText();

    g_nFlag1036 = 9;
    g_Spot.pnTrajFront1036 = &g_nFlag1036;
    g_pScratch = NULL;
    Tact_BuildVisRayTable(g_aRays, g_aSteps);

    g_Wiring.pMap          = &g_Map;
    g_Wiring.pTextRecs     = g_aText;
    g_Wiring.nTextRecCount = 60;
    g_Wiring.visCtx.pSpot          = &g_Spot;
    g_Wiring.visCtx.pTables        = &g_Tables;
    g_Wiring.visCtx.pfnLookup      = Map_LookupCell;
    g_Wiring.visCtx.pUser          = &g_Map;
    g_Wiring.visCtx.pGeom          = &g_Map.geom;
    g_Wiring.visCtx.apRays         = g_aRays;
    g_Wiring.visCtx.pRosters       = &g_Rosters;
    g_Wiring.visCtx.ppScratchUnit  = &g_pScratch;
    g_Wiring.visCtx.pbFireStatus1  = &g_Map.bFireStatus1;

    g_Rosters.apXcom  = g_Map.apXcomUnits;
    g_Rosters.apEnemy = g_Map.apEnemyUnits;
    g_Rosters.apCiv   = g_Map.apCivilians;
}

static MapCell *At(i32 nX, i32 nY, i32 nL)
{
    return &g_Map.aCells[nL][nY][nX];
}

/* --- the grid --------------------------------------------------------------- */

static void TestGrid(void)
{
    printf("GameMap grid and Map_LookupCell\n");

    Check("lookup origin is the embedded cell",
          Map_LookupCell(0, 0, 0, &g_Map) == At(0, 0, 0));
    Check("lookup far corner is layer 3",
          Map_LookupCell(59, 59, 3, &g_Map) == At(59, 59, 3));
    Check("x past 10 * nMapDimX is out of range",
          Map_LookupCell(60, 0, 0, &g_Map) == NULL);
    Check("negative y is out of range",
          Map_LookupCell(0, -1, 0, &g_Map) == NULL);
    Check("layer 4 is out of range",
          Map_LookupCell(0, 0, 4, &g_Map) == NULL);
    Check("negative layer is out of range",
          Map_LookupCell(0, 0, -1, &g_Map) == NULL);

    g_Map.geom.nDimX = 1;
    Check("the dimension test follows nMapDimX",
          Map_LookupCell(9, 0, 0, &g_Map) != NULL
          && Map_LookupCell(10, 0, 0, &g_Map) == NULL);
    g_Map.geom.nDimX = 6;

    Check("InitCells stamps the cell coordinates",
          At(7, 11, 2)->nX == 7 && At(7, 11, 2)->nY == 11
          && At(7, 11, 2)->nZ == 2);
    Check("InitCells stamps the last cell",
          At(59, 59, 3)->nX == 59 && At(59, 59, 3)->nZ == 3);
}

/* --- MoveActionCtx over the live context ------------------------------------ */

static void TestWiring(void)
{
    printf("Map_BuildMoveActionCtx over GameMap\n");

    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);

    Check("geometry is copied", g_Ctx.geom.nDimX == 6 && g_Ctx.geom.nDimY == 6);
    Check("lookup user is the map itself", g_Ctx.pUser == &g_Map);

    *g_Ctx.pnLayerScratch  = -3;
    *g_Ctx.pnMaxLayerClamp = 2;
    *g_Ctx.pnAnimPhase     = 5;
    *g_Ctx.pnShotCount     = 1;
    *g_Ctx.pbFlag22        = 7;
    *g_Ctx.pbFireStatus1   = 8;
    *g_Ctx.ppSelWeapon     = &g_aItems[0];
    *g_Ctx.ppThrowTarget   = &g_aUnits[1];
    *g_Ctx.pnUnk1405C      = 1;
    Check("scratch globals write through to the map",
          g_Map.nLayerScratch == -3 && g_Map.nMaxLayerClamp == 2
          && g_Map.nAnimPhase == 5 && g_Map.nShotCount == 1
          && g_Map.bFlag22 == 7 && g_Map.bFireStatus1 == 8
          && g_Map.pSelWeapon == &g_aItems[0]
          && g_Map.pThrowTarget == &g_aUnits[1]
          && g_Map.nFireTickProgress == 1);

    g_Map.nXcomCount = 3;
    g_Map.nEnemyCount = 2;
    g_Map.nCivilianCount = 4;
    Check("roster counts write through",
          *g_Ctx.pnXcomCount == 3 && *g_Ctx.pnEnemyCount == 2
          && *g_Ctx.pnCivilianCount == 4);
    Check("roster arrays are the map arrays",
          g_Ctx.apXcomUnits == g_Map.apXcomUnits
          && g_Ctx.apEnemyUnits == g_Map.apEnemyUnits
          && g_Ctx.apCivilians == g_Map.apCivilians);

    g_Map.scatter.nDx = 11;
    Check("the scatter triple is the map's", g_Ctx.pScatter->nDx == 11);
    g_Map.adMtX[3] = 1.5;
    Check("the multi-target arrays are the map's", g_Ctx.pAdMtX[3] == 1.5);
    Check("the aim block is the map's", g_Ctx.pAim == &g_Map.aim);
    Check("the throw scratch is the map's",
          g_Ctx.pThrowArc == &g_Map.throwArc
          && g_Ctx.pThrowLanding == &g_Map.throwLanding
          && g_Ctx.pThrowBudget == &g_Map.throwBudget);
    Check("the shot-history array is the map's",
          g_Ctx.apShotCells == g_Map.apShotCells);

    g_Map.tact.bBusy = 1;
    g_Map.tact.pTrackUnit = &g_aUnits[2];
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Check("the busy gate and tracked unit come from unk_157180",
          g_Ctx.bAnimBusy && g_Ctx.pTrackUnit == &g_aUnits[2]);

    Check("the three callbacks are the real ones",
          g_Ctx.pfnArmorTuWarn == Map_ArmorTuWarnCb
          && g_Ctx.pfnPushStatus == Map_PushStatusCb
          && g_Ctx.pfnUpdateVisibility == Map_UpdateVisibilityCb
          && g_Ctx.pCallbackUser == &g_Wiring
          && g_Ctx.pVisCtx == &g_Wiring.visCtx);
}

/* --- the real status push --------------------------------------------------- */

static void TestStatusPush(void)
{
    printf("Map_PushStatusCb -> StatusMsg_Push @ 0x2dda4\n");

    g_Map.tact.bBusy = 0;
    Map_PushStatusCb(1, &g_Wiring);
    Check("one message stored", g_Map.status.nCount == 1);
    Check("the text is aTextRecs record 1",
          strcmp(g_Map.status.aszMsg[0], "not enough time") == 0);

    Map_PushStatusCb(4, &g_Wiring);
    Check("second message appended", g_Map.status.nCount == 2);
    Check("record 4 is the throw failure",
          strcmp(g_Map.status.aszMsg[1], "throw failed") == 0);

    /* The busy gate: unk_157180->bBusy drops an unforced push. */
    g_Map.tact.bBusy = 1;
    Map_PushStatusCb(2, &g_Wiring);
    Check("a busy game drops the push", g_Map.status.nCount == 2);
    g_Map.tact.bBusy = 0;

    /* No text blob: the push still happens, with an empty record. */
    g_Wiring.pTextRecs = NULL;
    Map_PushStatusCb(1, &g_Wiring);
    Check("missing text blob pushes an empty record",
          g_Map.status.nCount == 3 && g_Map.status.aszMsg[2][0] == '\0');
    g_Wiring.pTextRecs = g_aText;
}

/* --- the real armour-TU warning --------------------------------------------- */

static Soldier *ArmouredUnit(u8 bOpt0, u8 bOpt1, u8 bOpt2)
{
    Soldier *pUnit = &g_aUnits[0];

    pUnit->bStatus    = 0;   /* operative */
    pUnit->nUnitType  = 0;
    pUnit->auStat[0]  = 100;
    pUnit->uTu        = 100;
    pUnit->pCell      = At(3, 3, 0);

    g_aObjs[0].abArmorTu[0] = 100;
    g_aObjs[0].abArmorTu[1] = 50;
    g_aObjs[0].abArmorTu[2] = 25;
    g_aObjs[0].bTwoHanded   = 0;
    g_aItems[0].pObj        = &g_aObjs[0];
    pUnit->pHandL           = &g_aItems[0];

    g_Wiring.abRadioOpt[0] = bOpt0;
    g_Wiring.abRadioOpt[1] = bOpt1;
    g_Wiring.abRadioOpt[2] = bOpt2;
    return pUnit;
}

static void TestArmorWarn(void)
{
    printf("Map_ArmorTuWarnCb -> Unit_bArmorTuWarn @ 0x4e254\n");

    memset(&g_Map.status, 0, sizeof(g_Map.status));
    Soldier *pUnit = ArmouredUnit(0, 0, 0);

    /* abArmorTu[0] * auStat[0] / 100 = 100. uTu is exactly 100 and the command
     * costs 4, so the unit can just afford the armour but not the action. */
    Check("warns when the action would drop below the armour cost",
          Map_ArmorTuWarnCb(pUnit, 4, &g_Wiring));
    Check("the warning pushed two records", g_Map.status.nCount == 2);
    Check("record 48 is byte_B5F40",
          strcmp(g_Map.status.aszMsg[0], "tu warning head") == 0);
    Check("record 49 is the slot-0 tail",
          strcmp(g_Map.status.aszMsg[1], "tu slot 0") == 0);

    memset(&g_Map.status, 0, sizeof(g_Map.status));
    pUnit->uTu = 200;
    Check("no warning when the unit can pay both",
          !Map_ArmorTuWarnCb(pUnit, 4, &g_Wiring));
    Check("no records pushed", g_Map.status.nCount == 0);

    memset(&g_Map.status, 0, sizeof(g_Map.status));
    ArmouredUnit(0, 1, 0);   /* byte_AC83A selects slot 1 */
    pUnit->uTu = 50;
    Check("byte_AC83A selects abArmorTu[1]",
          Map_ArmorTuWarnCb(pUnit, 4, &g_Wiring));
    Check("the tail record follows the slot",
          g_Map.status.nCount == 2
          && strcmp(g_Map.status.aszMsg[1], "tu slot 1") == 0);

    memset(&g_Map.status, 0, sizeof(g_Map.status));
    ArmouredUnit(0, 0, 1);   /* byte_AC85A selects slot 2 */
    pUnit->uTu = 25;
    Check("byte_AC85A selects abArmorTu[2]",
          Map_ArmorTuWarnCb(pUnit, 4, &g_Wiring));
    Check("slot 2 tail record",
          strcmp(g_Map.status.aszMsg[1], "tu slot 2") == 0);

    memset(&g_Map.status, 0, sizeof(g_Map.status));
    ArmouredUnit(1, 0, 0);   /* byte_AC81A suppresses the whole check */
    pUnit->uTu = 100;
    Check("byte_AC81A suppresses the warning",
          !Map_ArmorTuWarnCb(pUnit, 4, &g_Wiring));
    Check("nothing pushed", g_Map.status.nCount == 0);

    memset(&g_Map.status, 0, sizeof(g_Map.status));
    ArmouredUnit(0, 0, 0);
    pUnit->pHandL = NULL;
    Check("an empty hand has no armour cost",
          !Map_ArmorTuWarnCb(pUnit, 4, &g_Wiring));
}

/* --- the real visibility update --------------------------------------------- */

static void TestVisibility(void)
{
    printf("Map_UpdateVisibilityCb -> Unit_UpdateVisibility @ 0x48580\n");

    Soldier *pUnit = &g_aUnits[0];
    MapCell *pOwn  = At(4, 4, 0);

    memset(pUnit, 0, sizeof(*pUnit));
    pUnit->bStatus   = 0;
    pUnit->nUnitType = 0;
    pUnit->pCell     = pOwn;
    pOwn->pUnit      = pUnit;
    g_Map.apXcomUnits[0] = pUnit;
    g_Map.nXcomCount = 1;
    g_Rosters.nXcom  = 1;

    Map_UpdateVisibilityCb(pUnit, &g_Wiring);

    Check("the unit's own cell gets the vertical-reveal flag",
          (pOwn->bFlags29 & 0x10u) != 0);
    Check("the cell above is revealed up the column",
          (At(4, 4, 1)->bFlags29 & 0x10u) != 0);
    Check("the multi-target scratch flag is cleared", g_nFlag1036 == 0);
}

/* --- the FSM against the live context --------------------------------------- */

static int g_bSuppressNoTime;

static void RunCmd(u8 nCmd, i32 nTuCost)
{
    TactQueueEntry *pEntry = &g_State.aQueue[0];

    memset(&g_State, 0, sizeof(g_State));
    g_State.nQueueCursor = 0;
    g_State.bUnk70114 = (u8)g_bSuppressNoTime;
    pEntry->nCmd    = nCmd;
    pEntry->pUnit   = &g_aUnits[0];
    pEntry->pWeapon = &g_aItems[0];
    pEntry->nTuCost = nTuCost;
    pEntry->nMode   = 0;
    Tact_ProcessMoveAction(&g_State, &g_Ctx);
}

static void TestFsmOnMap(void)
{
    printf("Tact_ProcessMoveAction @ 0x4f3f0 on the live context\n");

    /* Command 3 (turn sound) with no TU: the gated "not enough time" push and
     * then the abort. The abort writes 13 into the entry, and
     * Tact_RefreshTeamVisibility then overwrites it with the end mark. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    g_aUnits[0].bStatus = 0;
    g_aUnits[0].uTu = 0;
    g_aUnits[0].pCell = At(2, 2, 0);
    RunCmd(3, 4);
    Check("no TU aborts the entry", g_State.aQueue[0].nCmd == TACTCMD_END_MARK);
    Check("the abort pushed aTextRecs record 1",
          g_Map.status.nCount == 1
          && strcmp(g_Map.status.aszMsg[0], "not enough time") == 0);

    /* bUnk70114 suppresses the message but not the abort. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    g_aUnits[0].bStatus = 0;
    g_aUnits[0].uTu = 0;
    g_aUnits[0].pCell = At(2, 2, 0);
    g_bSuppressNoTime = 1;
    RunCmd(3, 4);
    g_bSuppressNoTime = 0;
    Check("bUnk70114 suppresses the message", g_Map.status.nCount == 0);
    Check("and still aborts", g_State.aQueue[0].nCmd == TACTCMD_END_MARK);

    /* Command 3 with TU and armour pressure: the armour callback's two records
     * are pushed by Unit_bArmorTuWarn itself, then the FSM aborts. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    ArmouredUnit(0, 0, 0);
    g_aUnits[0].pCell = At(2, 2, 0);
    RunCmd(3, 4);
    Check("the armour warning aborts the entry",
          g_State.aQueue[0].nCmd == TACTCMD_END_MARK);
    Check("the warning records are in the map's status stack",
          g_Map.status.nCount == 2
          && strcmp(g_Map.status.aszMsg[0], "tu warning head") == 0);

    /* Command 3 with TU and no armour: the sound path runs and the entry stays. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    ArmouredUnit(1, 0, 0);   /* AC81A suppresses the armour check */
    g_aUnits[0].pCell = At(2, 2, 0);
    g_aUnits[0].uTu = 40;
    RunCmd(3, 4);
    Check("a clean turn command leaves the entry alone",
          g_State.aQueue[0].nCmd == 3);
    Check("and resets the phase tick", g_State.nPhaseTick == 0);

    /* Command 0x12 (fire sound effect): the TU charge, the phase reset, and the
     * phase-active byte at 0x111E0[1]. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    g_aUnits[0].bStatus = 0;
    g_aUnits[0].nUnitType = 0;
    g_aUnits[0].uTu = 30;
    g_aUnits[0].pCell = At(2, 2, 0);
    g_aObjs[0].uSortKey = 0;
    g_aItems[0].pObj = &g_aObjs[0];
    g_State.nPhaseCounter = 5;
    RunCmd(0x12, 6);
    Check("the fire sound effect charges the TU", g_aUnits[0].uTu == 24);
    Check("it clears the phase counter", g_State.nPhaseCounter == 0);
    Check("it sets the phase-active byte", g_State.gap_70112[1] == 1);
    Check("and does not abort", g_State.aQueue[0].nCmd == 0x12);

    /* Command 1 (step) on the real grid. The step itself only sets up the
     * animation; the cell hand-off happens when the phase completes. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    {
        Soldier *pUnit = &g_aUnits[0];
        MapCell *pFrom = At(10, 10, 0);

        memset(pUnit, 0, sizeof(*pUnit));
        pUnit->bStatus = 0;
        pUnit->nUnitType = 0;
        pUnit->bFacing = 0;
        pUnit->uTu = 20;
        pUnit->uStunOrArmor = 100;
        pUnit->uInvBurden = 0;
        pUnit->auStat[8] = 100;
        pUnit->pCell = pFrom;
        pFrom->pUnit = pUnit;

        memset(&g_State, 0, sizeof(g_State));
        g_State.nQueueCursor = 0;
        g_State.aQueue[0].nCmd   = 1;
        g_State.aQueue[0].pUnit  = pUnit;
        g_State.aQueue[0].nFacing = 2;   /* g_aStepDir[2] is +x */
        g_State.aQueue[0].nDelta = 0;
        g_State.aQueue[0].nTuCost = 4;
        Tact_ProcessMoveAction(&g_State, &g_Ctx);

        Check("the step leaves the entry running", g_State.aQueue[0].nCmd == 1);
        Check("the step records the cell it steps from",
              g_State.pStepFromCell == pFrom);
        Check("the step sets the layer scratch to the unit's layer + 1",
              g_Map.nLayerScratch == 1);
        Check("the step starts the walk animation", g_Map.nAnimPhase == 1);
        Check("the step sets the walk flag", (pUnit->bFlags & 2u) != 0);
        Check("the facing 0 draw offset is (2,-1)",
              g_State.nStepDrawOffX == 2 && g_State.nStepDrawOffY == -1);
        Check("the step charges no TU", pUnit->uTu == 20);
    }

    /* Command 0xE (fire) end to end. nArg is the hit chance, and 100 makes
     * Unit_RollChance succeed every time, which clears the scatter triple and
     * makes the aim block deterministic. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    {
        Soldier *pUnit = &g_aUnits[0];
        MapCell *pFrom = At(10, 10, 0);

        memset(pUnit, 0, sizeof(*pUnit));
        pUnit->bStatus   = 0;
        pUnit->nUnitType = 0;
        pUnit->bFacing   = 0;
        pUnit->uTu       = 40;
        pUnit->pCell     = pFrom;
        pFrom->pUnit     = pUnit;

        g_aObjs[0].uSortKey  = 0;    /* a normal weapon, not the 13/35 pair */
        g_aObjs[0].bListPri  = 0;
        g_aObjs[0].bAmmoCap  = 100;  /* not the unlimited marker 255 */
        g_aObjs[0].bReadySel = 0;
        g_aItems[0].pObj     = &g_aObjs[0];
        g_aItems[0].nAmmo    = 5;
        pUnit->pHandR        = &g_aItems[0];

        memset(&g_State, 0, sizeof(g_State));
        g_State.nQueueCursor     = 0;
        g_State.aQueue[0].nCmd   = 0xE;
        g_State.aQueue[0].pUnit  = pUnit;
        g_State.aQueue[0].pWeapon = &g_aItems[0];
        g_State.aQueue[0].nTuCost = 8;
        g_State.aQueue[0].nArg   = 100;
        g_State.aQueue[0].nMode  = 0;
        g_State.aQueue[0].nX     = 11;
        g_State.aQueue[0].nY     = 10;
        g_State.aQueue[0].nZ     = 0;
        Tact_ProcessMoveAction(&g_State, &g_Ctx);

        Check("fire leaves the entry running", g_State.aQueue[0].nCmd == 0xE);
        Check("fire charges the TU", pUnit->uTu == 32);
        Check("fire sets the aiming flag", (pUnit->bFlags & 4u) != 0);
        Check("the weapon lost one round", g_aItems[0].nAmmo == 4);
        Check("the shot counter advanced", g_Map.nShotCount == 1);
        Check("the muzzle origin is the cell centre plus the type-0 offset",
              g_Map.aim.nOriginX == 16 * 10 + 8 + 5
              && g_Map.aim.nOriginY == 16 * 10 + 8 - 5
              && g_Map.aim.nOriginZ == 12 * 0 + 3 + 6);
        Check("a guaranteed hit aims straight at the target cell",
              g_Map.aim.nTargetX == 16 * 11 + 8
              && g_Map.aim.nTargetY == 16 * 10 + 8);
        Check("the layer clamp follows the layer scratch",
              g_Map.nMaxLayerClamp == g_Map.nLayerScratch);
    }

    /* Command 0xE with the unlimited-ammo marker: the round is not spent, but
     * the shot counter still advances. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    {
        Soldier *pUnit = &g_aUnits[0];

        memset(pUnit, 0, sizeof(*pUnit));
        pUnit->bStatus = 0;
        pUnit->nUnitType = 0;
        pUnit->uTu = 40;
        pUnit->pCell = At(10, 10, 0);
        g_aObjs[0].uSortKey = 0;
        g_aObjs[0].bAmmoCap = 255;
        g_aItems[0].pObj = &g_aObjs[0];
        g_aItems[0].nAmmo = 5;

        memset(&g_State, 0, sizeof(g_State));
        g_State.nQueueCursor = 0;
        g_State.aQueue[0].nCmd = 0xE;
        g_State.aQueue[0].pUnit = pUnit;
        g_State.aQueue[0].pWeapon = &g_aItems[0];
        g_State.aQueue[0].nTuCost = 8;
        g_State.aQueue[0].nArg = 100;
        g_State.aQueue[0].nX = 11;
        g_State.aQueue[0].nY = 10;
        g_State.aQueue[0].nZ = 0;
        Tact_ProcessMoveAction(&g_State, &g_Ctx);
        Check("bAmmoCap 255 never spends a round", g_aItems[0].nAmmo == 5);
        Check("and still counts the shot", g_Map.nShotCount == 1);
    }

    /* Command 0x11 (throw) end to end. The target is the unit's own cell, so
     * the arc bound is 0 and the landing test (travel >= bound - 8) passes on
     * the first bump. A throw at a neighbouring cell has bound 16 and cannot
     * land in one bump while Map.bTrajHitStop is clear, which is the failure
     * path checked after this one. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    {
        Soldier *pUnit = &g_aUnits[0];
        MapCell *pFrom = At(10, 10, 0);

        memset(pUnit, 0, sizeof(*pUnit));
        pUnit->bStatus = 0;
        pUnit->nUnitType = 0;
        pUnit->uTu = 20;
        pUnit->pCell = pFrom;
        pFrom->pUnit = pUnit;

        g_aObjs[0].uSortKey = 0;
        g_aItems[0].pObj = &g_aObjs[0];
        pUnit->pHandL = &g_aItems[0];
        g_Map.pSelWeapon = &g_aItems[0];

        /* 50 throw, 50 strength, weight 2, 60 stun: budget 15.0. */
        g_Map.throwBudget.nStatThrow   = 50;
        g_Map.throwBudget.nStatStrength = 50;
        g_Map.throwBudget.nWeight      = 2;
        g_Map.throwBudget.nStunOrArmor = 60;

        memset(&g_State, 0, sizeof(g_State));
        g_State.nQueueCursor = 0;
        g_State.aQueue[0].nCmd = 0x11;
        g_State.aQueue[0].pUnit = pUnit;
        g_State.aQueue[0].nTuCost = 4;
        g_State.aQueue[0].nArg = 100;
        g_State.aQueue[0].nX = 10;
        g_State.aQueue[0].nY = 10;
        g_State.aQueue[0].nZ = 0;
        g_Map.bFlag22 = 1;
        Tact_ProcessMoveAction(&g_State, &g_Ctx);

        Check("the throw reached its landing",
              g_State.aQueue[0].nCmd == 0x11);
        Check("the throw charged the TU", pUnit->uTu == 16);
        Check("the thrown weapon left the left hand", pUnit->pHandL == NULL);
        Check("the throw target slot holds the target cell's occupant",
              g_Map.pThrowTarget == pUnit);
        Check("the throw clears the flag at 0x22", g_Map.bFlag22 == 0);
        Check("the landing was not rejected by range",
              g_Map.throwLanding.bRejected == 0);
    }

    /* Command 0x11 at a neighbouring cell, with Map.bTrajHitStop clear: the
     * landing loop runs once, the throw falls short, and the FSM pushes
     * aTextRecs record 4 before aborting. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    {
        Soldier *pUnit = &g_aUnits[0];

        memset(pUnit, 0, sizeof(*pUnit));
        pUnit->bStatus = 0;
        pUnit->nUnitType = 0;
        pUnit->uTu = 20;
        pUnit->pCell = At(10, 10, 0);
        g_Map.throwBudget.nStatThrow = 50;
        g_Map.throwBudget.nStatStrength = 50;
        g_Map.throwBudget.nWeight = 2;
        g_Map.throwBudget.nStunOrArmor = 60;

        memset(&g_State, 0, sizeof(g_State));
        g_State.nQueueCursor = 0;
        g_State.aQueue[0].nCmd = 0x11;
        g_State.aQueue[0].pUnit = pUnit;
        g_State.aQueue[0].nTuCost = 4;
        g_State.aQueue[0].nArg = 100;
        g_State.aQueue[0].nX = 11;
        g_State.aQueue[0].nY = 10;
        g_State.aQueue[0].nZ = 0;
        Tact_ProcessMoveAction(&g_State, &g_Ctx);

        Check("a short throw with the hit-stop flag clear aborts",
              g_State.aQueue[0].nCmd == TACTCMD_END_MARK);
        Check("it pushed aTextRecs record 4",
              g_Map.status.nCount == 1
              && strcmp(g_Map.status.aszMsg[0], "throw failed") == 0);
        Check("and charged no TU", pUnit->uTu == 20);
    }

    /* Command 0x11 with no TU: the ungated push of aTextRecs record 1, then the
     * abort. Unlike the gated paths this one always pushes. */
    Reset();
    Map_BuildMoveActionCtx(&g_Map, &g_Tables, &g_Ctx);
    Map_WireMoveActionCtx(&g_Ctx, &g_Wiring);
    g_aUnits[0].bStatus = 0;
    g_aUnits[0].nUnitType = 0;
    g_aUnits[0].uTu = 0;
    g_aUnits[0].pCell = At(10, 10, 0);
    g_State.bUnk70114 = 1;
    g_bSuppressNoTime = 1;
    RunCmd(0x11, 4);
    g_bSuppressNoTime = 0;
    Check("a throw with no TU aborts", g_State.aQueue[0].nCmd == TACTCMD_END_MARK);
    Check("and pushes the no-time record even with bUnk70114 set",
          g_Map.status.nCount == 1
          && strcmp(g_Map.status.aszMsg[0], "not enough time") == 0);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    Reset();
    TestGrid();
    TestWiring();
    TestStatusPush();
    TestArmorWarn();
    TestVisibility();
    TestFsmOnMap();

    printf("%d checks, %d failures\n", g_nChecks, g_nFails);
    return g_nFails != 0;
}
