/* pufo_world.c - the shell's geoscape bring-up: Geoscape_InitUI @ 0x857f4 mode 0 and
 * Game_StartNew @ 0x94554 run over the shell-owned PufoWorldState.
 *
 * The click handlers the geoscape screens would install are no-ops for now: those screens
 * (Geoscape_Draw, Geoscape_OnRadarListClick, the research and soldier screens' open paths)
 * are not ported yet, so the shell wires the slots the same way tests/test_startnew.c does.
 * The spawn pass's two hooks are stubs for the same reason: StrategList_ProcessEvents
 * (0x7fcb0) and the mission-spawn leaf are unported.
 */
#include "app/pufo_world.h"

#include "audio/pufo_audio.h" /* Sound_Play, the message-list sample the radar click plays */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* unk_AAC98 / byte_AACD8 / byte_AAD08, the roll tables tests/test_strateg.c verified against
 * the image. */
static const StrategRollTables g_WorldRoll = {
    {
        { 100, 0, 0, 0, 0, 0, 0, 0 },
        { 100, 0, 0, 0, 0, 0, 0, 0 },
        { 100, 0, 0, 0, 0, 0, 0, 0 },
        {  50, 50, 0, 0, 0, 0, 0, 0 },
        {  50, 0, 50, 0, 0, 0, 0, 0 },
        {  20, 0, 0, 40, 0, 40, 0, 0 },
        {   0, 0, 0, 30, 0, 0, 70, 25 },
        {   0, 0, 60, 0, 0, 15, 0, 0 },
    },
    {
        { 22, 33, 35,  5,  5,  0,  0,  0 },
        { 10, 30, 35, 10, 10,  5,  0,  0 },
        {  8, 20, 20, 15, 15,  7, 10,  5 },
        {  0, 20, 20, 15, 15,  8, 17,  5 },
        {  0, 15, 15, 15, 15,  8, 20, 12 },
        {  0, 10, 15, 15, 15,  8, 22, 15 },
    },
    {
        { 100, 0, 0, 0, 0 },
        {  70, 0, 30, 0, 0 },
        {  33, 0, 33, 34, 0 },
        {  25, 25, 25, 25, 0 },
        {  20, 20, 20, 20, 20 },
        {  20, 20, 20, 20, 20 },
    },
};

static void World_NoopClick(void) { }

/* The options screen is not ported yet; the radar click's type-0 branch reaches it through
 * these stubs (the sample still plays). */
static void World_OptionsPlaySampleStub(u32 nSample, u32 nArg, void *pUser)
{
    (void)nArg;
    (void)pUser;
    (void)Sound_Play((i32)nSample);
}

static void World_OptionsInitStub(void *pUser)
{
    (void)pUser; /* OptionsScreen_Init is not ported */
}

static void World_OptionsOpenStub(i32 nMode, void *pUser)
{
    (void)nMode;
    (void)pUser; /* the options screen itself is not ported */
}

static PufoWorldState *g_pActiveWorld; /* UiControl.pfnClick takes no arguments */

static void World_RadarClickThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    RadarClickEnv env;

    if (pW == NULL || pW->ui.pStatusPanel == NULL) {
        return;
    }

    memset(&env, 0, sizeof(env));
    env.pLines = &pW->msgLines;
    env.pStatusPanel = pW->ui.pStatusPanel;
    env.pStrategList = &pW->strategList;
    env.pnSelectedStrategIdx = &pW->nSelectedStrategIdx;
    env.pTime = &pW->time;
    env.pWorld = &pW->world;
    env.pnSelectedShipIdx = &pW->nSelectedShipIdx;
    env.pnStrategIndex = &pW->nStrategIndex;
    env.pnScreenId = &pW->screen.nGeoscapeScreen;
    env.pbFlag1870A0 = &pW->bFlag1870A0;
    env.pbFlag18708C = &pW->bFlag18708C;
    env.pbFlag187090 = &pW->bFlag187090;
    env.pbFlag187098 = &pW->bFlag187098;
    env.pnRadarPulseTimer = &pW->nRadarPulseTimer;
    env.pOptionsHooks = &pW->optionsHooks;
    env.pfnPlaySample = pW->pfnPlaySample;
    env.pSoundUser = pW->pSoundUser;

    (void)Geoscape_OnRadarListClick(&env);
}

static int World_SpawnMissionStub(StrategItem *pUfo, i32 nMissionSub, void *pUser)
{
    (void)pUfo;
    (void)nMissionSub;
    (void)pUser;
    return 0; /* the mission-spawn leaf is not ported */
}

static int World_ProcessEventsStub(void *pUser)
{
    (void)pUser;
    return 0; /* StrategList_ProcessEvents @ 0x7fcb0 is not ported */
}

bool World_Init(PufoWorldState *pW)
{
    memset(pW, 0, sizeof(*pW));

    pW->apSites = (SmapSite *)malloc(PUFO_SMAP_SITE_COUNT * sizeof(SmapSite));
    if (pW->apSites == NULL) {
        return false;
    }
    memset(pW->apSites, 0, PUFO_SMAP_SITE_COUNT * sizeof(SmapSite));

    PtrArray_ctor(&pW->strategList);

    pW->nNextSoldierId = 1;
    pW->nCreateNextSiteId = 1;
    pW->nUnk159DC4 = 10; /* the original's 0x159DC4 starts at 10 */
    pW->view.nMode = 2;  /* the geoscape starts at the closest zoom; StartNew zooms out twice */
    return true;
}

/* The mode-0 hook: Game_StartNew over the same state. */
static void World_StartNewHook(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    BootAssets *pAssets = pW->pAssets;
    UnitCreateEnv unitEnv;
    ShipSpawnEnv shipEnv;
    BaseCreateEnv createEnv;
    StrategSpawnCtx spawnCtx;
    StartNewEnv env;
    i32 i;

    memset(&unitEnv, 0, sizeof(unitEnv));
    unitEnv.apSoldiers = pW->apSoldiers;
    unitEnv.pnSoldierCount = &pW->nSoldierCount;
    unitEnv.pnNextSoldierId = &pW->nNextSoldierId;

    memset(&shipEnv, 0, sizeof(shipEnv));
    shipEnv.apCraft = pW->apCraft;
    shipEnv.pnCraftCount = &pW->bCraftCount;
    shipEnv.pnNextCraftId = &pW->nNextCraftId;
    shipEnv.apBases = pW->apBases;
    shipEnv.abFacilityGrid = pW->abFacilityGrid;
    shipEnv.pCtorEnv = &pW->ctorEnv;
    shipEnv.pStrategList = &pW->strategList;
    shipEnv.pnStrategSeed = &pW->nStrategSeed;
    shipEnv.pnZoomMode = &pW->view.nMode;

    memset(&createEnv, 0, sizeof(createEnv));
    createEnv.apBases = pW->apBases;
    createEnv.apObjects = pAssets->apObjects;
    createEnv.nObjectCount = pAssets->nObjectCount;
    createEnv.pbBaseCount = &pW->nBaseCount;
    createEnv.pStrategList = &pW->strategList;
    createEnv.pnSeed = &pW->nStrategSeed;
    createEnv.nZoomMode = pW->view.nMode;
    createEnv.pnNextSiteId = &pW->nCreateNextSiteId;
    createEnv.apSiteRecs = pW->aSiteRecs;
    createEnv.abKindTable = pW->abKindTable;
    createEnv.pProductU32Col = pW->auProductU32;
    createEnv.pProductU16Col = pW->auProductU16;
    createEnv.pResearchCol = pW->auResearchCol;

    /* The original reads the difficulty and the day gate from globals inside the callee; the
     * port lifted them into arguments. The shell has no difficulty setting yet, so it uses
     * difficulty 0 and an open gate (99), the same pair tests/test_strateg.c pins. */
    memset(&spawnCtx, 0, sizeof(spawnCtx));
    spawnCtx.pTables = &g_WorldRoll;
    spawnCtx.nDifficulty = 0;
    spawnCtx.nEarlyGate = 99;
    spawnCtx.nZoomMode = pW->view.nMode;
    spawnCtx.pnSeed = &pW->nStrategSeed;
    spawnCtx.pfnSpawnMission = World_SpawnMissionStub;
    spawnCtx.pfnProcessEvents = World_ProcessEventsStub;

    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        pW->navEnv.apfnNavClick[i] = World_NoopClick;
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        pW->navEnv.apfnPanelClick[i] = World_NoopClick;
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        pW->navEnv.apfnFindClick[i] = World_NoopClick;
    }
    pW->navEnv.pfnLoadPads = pW->pfnLoadPads;
    pW->navEnv.pLoadPadsUser = pW->pLoadPadsUser;

    memset(&env, 0, sizeof(env));
    env.apObjects = pAssets->apObjects;
    env.nObjectCount = pAssets->nObjectCount;
    env.apSoldiers = pW->apSoldiers;
    env.pnSoldierCount = &pW->nSoldierCount;
    env.apCraft = pW->apCraft;
    env.pbCraftCount = &pW->bCraftCount;
    env.apBases = pW->apBases;
    env.pbBaseCount = &pW->nBaseCount;
    env.pnSelectedBaseIdx = &pW->nSelectedBase;
    env.pnFunds = &pW->nFunds;
    env.pnMonthCounter = &pW->nMonthCounter;
    env.pnAlienPoolAddend = &pW->nAlienPoolAddend;
    env.pTime = &pW->time;
    env.abFacilityGrid = pW->abFacilityGrid;
    env.pProductU16Col = pW->auProductU16;
    env.pProductU32Col = pW->auProductU32;
    env.pManufCol = pW->auManufCol;
    env.pResearchCol = pW->auResearchCol;
    env.abResearchZero = pW->abResearchZero;
    env.abResearchActive = pW->abResearchActive;
    env.bResearchCount = (u8)pW->nResearchEntries; /* 0x160C4C, the byte InitUI filled */
    env.paTopics = pW->aTopics;
    env.apSites = pW->apSites;
    env.pnSiteCount = &pW->nSiteCount;
    env.apRadarPct = pW->abRadarPct;
    env.pStrategList = &pW->strategList;
    env.pSpawnCtx = &spawnCtx;
    env.pbPlacementPending = &pW->bPlacementPending;
    env.pbStrategCount = &pW->bStrategCount;
    env.abTypeCounters = pW->abTypeCounters;
    env.pnNextDisplayId = &pW->nNextDisplayId;
    env.pnNextSiteId = &pW->nNextSiteId;
    env.pnMissionId = &pW->nMissionId;
    env.abSaveFlags = pW->abFlags18655D;
    env.pEconomy = &pW->economy;
    env.pvFundTable = pW->anFundTable;
    env.paFacFlags = pW->aFac;
    env.pTextRecs = pAssets->pTextRecs;
    env.pScreen = &pW->screen;
    env.pCtlResearchPanel = &pW->ctlResearchPanel;
    env.pCtlConfirm = &pW->ctlConfirm;
    env.pView = &pW->view;
    env.pZoomSets = pW->sets;
    env.pWorldOut = pW->ui.pWorldSize; /* the pair InitUI allocated at 0x186BC0 */
    env.pbFlag187080 = &pW->bFlag187080;
    env.pfnTryConfirmClick = World_NoopClick; /* NewBaseScreen_TryConfirm needs the map-click seam */
    env.pNav = &pW->nav;
    env.pNavEnv = &pW->navEnv;
    env.pUnitEnv = &unitEnv;
    env.pShipEnv = &shipEnv;
    env.pCreateEnv = &createEnv;

    pW->nLastStartNewRet = Game_StartNew(&env);

    /* The original shares one type-counter block between Ship_ctor and the root; the port's
     * ShipCtorEnv carries it by value, so the shell folds it back after the spawns. */
    for (i = 0; i < PUFO_CRAFT_TYPE_COUNT; ++i) {
        pW->abTypeCounters[i] = pW->ctorEnv.abTypeCounters[i];
    }
}

i32 World_NewGame(PufoWorldState *pW, BootAssets *pAssets, PufoFont *pFont,
                  const PufoOptions *pOptions, const WorldHooks *pHooks)
{
    GeoscapeInitEnv env;
    i32 i;

    pW->pAssets = pAssets;
    pW->pfnLoadPads = pHooks->pfnLoadPads;
    pW->pLoadPadsUser = pHooks->pLoadPadsUser;
    pW->pfnPlaySample = pHooks->pfnPlaySample;
    pW->pSoundUser = pHooks->pSoundUser;
    g_pActiveWorld = pW;

    memset(&pW->optionsHooks, 0, sizeof(pW->optionsHooks));
    pW->optionsHooks.pfnPlaySample = World_OptionsPlaySampleStub;
    pW->optionsHooks.pfnInit = World_OptionsInitStub;
    pW->optionsHooks.pfnOpen = World_OptionsOpenStub;

    memset(&pW->ctorEnv, 0, sizeof(pW->ctorEnv));
    pW->ctorEnv.apObjects = pAssets->apObjects;
    pW->ctorEnv.nObjectCount = pAssets->nObjectCount;

    AssetReader_Clear(&pW->rdr);

    pW->ui.pFont = pFont;
    pW->ui.pbGeoscapeUi = &pW->bGeoscapeUi;

    memset(&pW->world, 0, sizeof(pW->world));
    pW->world.apCraft = pW->apCraft;
    pW->world.nShipCount = pW->bCraftCount;
    pW->world.apSiteState = pW->abSiteState;
    pW->world.apRadarPct = pW->abRadarPct;
    pW->world.pTime = &pW->time;

    memset(&env, 0, sizeof(env));
    env.pUi = &pW->ui;
    env.pView = &pW->view;
    env.pSetsOut = pW->sets;
    env.pnScreenMode = &pW->screen.nGeoscapeScreen;
    env.pbGeoscapeUi = &pW->bGeoscapeUi;
    env.pbRoot34 = &pW->bRoot34;
    env.pbRoot35 = &pW->bRoot35;
    env.pFont = pFont;
    env.pRdr = &pW->rdr;
    env.pTextRecs = pAssets->pTextRecs;
    env.pTime = &pW->time;
    env.apTopics = pW->aTopics;
    env.puResearchEntries = &pW->nResearchEntries;
    env.paFacFlags = pW->aFac;
    env.apObjects = pAssets->apObjects;
    env.nObjectCount = pAssets->nObjectCount;
    env.pnGlyphPalBase = &pW->nGlyphPalBase;
    env.abFacilityGrid = pW->abFacilityGrid;
    env.pWorld = &pW->world;
    env.pbBaseCount = &pW->nBaseCount;
    env.pnUnk159DC4 = &pW->nUnk159DC4;
    env.paMissionSites = pW->aMissionSites;
    env.abUnk18655D = pW->abFlags18655D;
    env.pbSaveVersion = &pW->nSaveVersion;
    env.pOptions = pOptions;
    for (i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        env.apfnRootClick[i] = World_NoopClick;
    }
    env.pfnCraftSelectClick = World_NoopClick;
    env.pfnRadarClick = World_RadarClickThunk; /* Geoscape_OnRadarListClick @ 0x854c4 */
    env.pfnStartNew = World_StartNewHook;
    env.pHookUser = pW;
    env.pfnLoadPads = pHooks->pfnLoadPads;
    env.pLoadPadsUser = pHooks->pLoadPadsUser;

    Geoscape_InitUI(&env, 0); /* 0x857f4, mode 0: the new-game branch */

    return pW->nLastStartNewRet;
}

void World_Free(PufoWorldState *pW)
{
    GeoscapeRoster roster;
    i32 i;

    memset(&roster, 0, sizeof(roster));
    roster.apSoldiers = pW->apSoldiers;
    roster.nSoldierCount = (u16)pW->nSoldierCount;
    roster.apBases = pW->apBases;
    roster.nBaseCount = pW->nBaseCount;
    roster.apCraft = pW->apCraft;
    roster.nCraftCount = pW->bCraftCount;
    roster.pStrategList = &pW->strategList;

    GeoscapeUi_FreeAll(&pW->ui, &roster, 1); /* 0x86d20: the UI plus the roster */

    /* Geoscape_BuildNavControls' fifteen controls are not part of pUi (the original never
     * freed them either); the shell does. */
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        free(pW->nav.apNav[i]);
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {
        free(pW->nav.apPanel[i]);
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {
        free(pW->nav.apFind[i]);
    }
    free(pW->apSites);
    pW->apSites = NULL;
}
