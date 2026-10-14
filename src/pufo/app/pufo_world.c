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

#include "dat/pufo_dat.h"    /* DatFile, the slot file LoadGame opens */
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

static PufoWorldState *g_pActiveWorld; /* UiControl.pfnClick takes no arguments */

/* The build dialog's OK (BaseFacScreen_TryBuySelected @ 0x58910), installed by BaseFacScreen_Toggle
 * as the panel's +3020 handler. UiControl.pfnClick takes no arguments, so it reaches the world
 * through the active pointer. */
static void World_FacBuyThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    BaseFacBuyEnv env;

    if (pW == NULL || pW->pFacPanel == NULL) {
        return;
    }

    memset(&env, 0, sizeof(env));
    env.pPanel = pW->pFacPanel;
    env.paFac = pW->aFac;
    env.pnFunds = &pW->nFunds;
    env.pbArmed = &pW->bArmedPlacement;
    env.pbNewBaseMode = &pW->bNewBaseMode;
    env.pnPanelSel = &pW->nPanelSel;
    env.pdwShortfall = &pW->nShortfall1870C8;
    env.pnResidue = &pW->nResidue1870B4;
    (void)BaseFacScreen_TryBuySelected(&env);
}

/* BaseFacScreen_Toggle @ 0x589ec: the build dialog's open/close switch. */
static void World_FacToggleThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW == NULL) {
        return;
    }

    memset(&pW->facToggleEnv, 0, sizeof(pW->facToggleEnv));
    pW->facToggleEnv.pbOpen = &pW->bFacDialogOpen;
    pW->facToggleEnv.apPanelCtl[0] = &pW->ctlFacUp;
    pW->facToggleEnv.apPanelCtl[1] = &pW->ctlFacDown;
    pW->facToggleEnv.ppPanel = &pW->pFacPanel;
    pW->facToggleEnv.pnPanelSel = &pW->nPanelSel;
    pW->facToggleEnv.pbArmed = &pW->bArmedPlacement;
    pW->facToggleEnv.pbNewBaseMode = &pW->bNewBaseMode;
    pW->facToggleEnv.abFacilityGrid = pW->abFacilityGrid;
    pW->facToggleEnv.pnSelectedBaseIdx = &pW->nSelectedBase;
    pW->facToggleEnv.paFac = pW->aFac;
    pW->facToggleEnv.pszLastRowName = (const char *)pW->abLastRowName;
    pW->facToggleEnv.pfnBuyThunk = World_FacBuyThunk;
    BaseFacScreen_Toggle(&pW->facToggleEnv);
}

/* NewBaseScreen_TryConfirm @ 0x6152c tail-calls Geoscape_OnMapClick @ 0x8b4a8; this is the seam. */
static void World_TryConfirmClick(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    MapClickEnv env;

    if (pW == NULL) {
        return;
    }

    /* The rosters and the selected base can change between clicks; the env holds them by value. */
    pW->hangarEnv.nSelectedBaseIdx = pW->nSelectedBase;
    pW->hangarEnv.nCraftCount = pW->bCraftCount;
    pW->hangarEnv.nBaseCount = pW->nBaseCount;

    memset(&env, 0, sizeof(env));
    env.abFacilityGrid = pW->abFacilityGrid;
    env.pnScreenId = &pW->screen.nGeoscapeScreen;
    env.pbPlaceActive = &pW->bPlaceActive;
    env.pbPlaceFail = &pW->bPlaceFail;
    env.nTapX = pW->nSmapTapX;
    env.nTapY = pW->nSmapTapY;
    env.pHangar = &pW->hangarEnv;
    env.bPendingKind = pW->bPendingFacilityKind;
    env.pbConfirm = &pW->bPlacementPending;
    env.pbFacDone = &pW->bFacDone;
    env.nPlaceX = pW->nPlaceX;
    env.nPlaceY = pW->nPlaceY;
    env.apBases = pW->apBases;
    env.apSiteRecs = pW->aSiteRecs;
    env.pStrategList = &pW->strategList;
    env.pTextRecs = (pW->pAssets != NULL) ? pW->pAssets->pTextRecs : NULL;
    env.abKindTable = (i8 *)pW->abKindTable;
    env.pszLabelScratch = pW->szLabelScratch;
    env.cchLabelScratch = (i32)sizeof(pW->szLabelScratch);
    env.pbBaseCount = &pW->nBaseCount;
    env.pnSelectedBaseIdx = &pW->nSelectedBase;
    env.pFacInit = &pW->facInitEnv;

    (void)Geoscape_OnMapClick(&env);
}

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
    /* bPendingBaseKind @ 0x159DF4 keeps the image's .data value 0. Nothing in the new-game path
     * writes it: Game_StartNew ends on geoscape screen 2 (NewBaseScreen_Open 0x61620) with the
     * pending kind 0 and the pending cell 0,0 (0x159D94/0x159D98 are only written by
     * BaseFacScreen_DrawPlacementOverlay 0x59AF4). 0xFF is the "nothing pending" sentinel the
     * readers use (Geoscape_OnMapClick 0x8B4CC, Geoscape_OnPointerUp 0x89694); the build flow
     * installs it, the startup path does not. */
    pW->bPendingFacilityKind = 0;
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
    /* The map click's own envs. The rosters and the selected base are refreshed on every click,
     * so only the pointers are wired here. */
    memset(&pW->hangarEnv, 0, sizeof(pW->hangarEnv));
    pW->hangarEnv.abFacilityGrid = pW->abFacilityGrid;
    pW->hangarEnv.abGridBytes = pW->saveBases.abUnk159DBC; /* the 0x159DBC byte table */
    pW->hangarEnv.apCraft = pW->apCraft;
    pW->hangarEnv.apBases = pW->apBases;

    memset(&pW->facInitEnv, 0, sizeof(pW->facInitEnv));
    pW->facInitEnv.pCloseCtl = &pW->ctlFacClose;
    pW->facInitEnv.pUpCtl = &pW->ctlFacUp;
    pW->facInitEnv.pDownCtl = &pW->ctlFacDown;
    pW->facInitEnv.pnUnk15887C = &pW->nUnk15887C;
    pW->facInitEnv.pnUnk158880 = &pW->nUnk158880;
    pW->facInitEnv.pbArmed = &pW->bArmedPlacement;
    pW->facInitEnv.pfnPanelPrev = World_NoopClick; /* BaseFacScreen_PanelPrev/Next, panel not wired */
    pW->facInitEnv.pfnPanelNext = World_NoopClick;
    pW->facInitEnv.pfnCloseThunk = World_NoopClick; /* Geoscape_CloseUI is not wired yet */
    pW->facInitEnv.pfnLoadPads = pW->pfnLoadPads;
    pW->facInitEnv.pPadUser = pW->pLoadPadsUser;
    pW->facInitEnv.pfnToggle = World_FacToggleThunk; /* BaseFacScreen_Toggle @ 0x589ec */
    pW->facInitEnv.pToggleUser = pW;

    env.pfnTryConfirmClick = World_TryConfirmClick; /* NewBaseScreen_TryConfirm's tail call */
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

    /* The build dialog's panel is the shell's own allocation; Toggle's close branch releases it. */
    if (pW->bFacDialogOpen != 0) {
        World_FacToggleThunk(pW);
    }

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

/* The LoadGame teardown hooks. Tact_Shutdown (0x3d568) and Tact_InitMission (0x3c550) are not
 * ported, and the start-menu re-entry (Geoscape_InitUI from the load path) is not mapped yet, so
 * those three are stubs. The sav7 path tests/test_save.c pins (a geoscape save with no tactical
 * state) reaches none of them. */
static void Load_HookInitUi(i32 nMode, void *pUser)
{
    (void)nMode;
    (void)pUser;
}

static void Load_HookTactShutdown(void *pUser)
{
    (void)pUser;
}

static bool Load_HookTactInitMission(i32 nMode, void *pUser)
{
    (void)nMode;
    (void)pUser;
    return false;
}

static void Load_HookFreeAll(void *pUser)
{
    (void)pUser;
}

i32 World_LoadSlot(PufoWorldState *pW, const char *pszAppDir, i32 nSlot, BootAssets *pAssets,
                   const PufoOptions *pOptions, const WorldHooks *pHooks)
{
    LoadGameEnv env;
    LoadGameHooks hooks;
    DatFile dat;
    i32 nRet;

    (void)pOptions;

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnRenderFrame = pHooks->pfnRenderFrame;
    hooks.pfnStartUiShutdown = pHooks->pfnStartUiShutdown;
    hooks.pfnGeoscapeInitUi = Load_HookInitUi;
    hooks.pfnTactShutdown = Load_HookTactShutdown;
    hooks.pfnGeoscapeUiFreeAll = Load_HookFreeAll;
    hooks.pfnTactInitMission = Load_HookTactInitMission;
    hooks.pUser = pW;

    /* The live world mirrors the roster the readers rebuild. */
    pW->world.apCraft = pW->apCraft;
    pW->world.nShipCount = pW->bCraftCount;
    pW->world.apSiteState = pW->abSiteState;
    pW->world.apRadarPct = pW->abRadarPct;
    pW->world.pTime = &pW->time;

    memset(&env, 0, sizeof(env));
    env.pszAppDir = pszAppDir;
    env.nSlot = nSlot;
    env.pbTactFlag = &pW->bTactFlag;
    env.pbGeoscapeUi = &pW->bGeoscapeUi;
    env.pbGameState = &pW->bGameState;
    env.puUiScreen = &pW->nUiScreen;
    env.pnSoldierCount = &pW->nSoldierCount;
    env.pnListMode = &pW->nListMode;
    env.pbMissionCount = &pW->bMissionCount;
    env.pbCraftSelectAssaultFlag = &pW->bCraftSelectAssaultFlag;
    env.pBases = &pW->saveBases;
    env.apBases = pW->apBases;
    env.nOldBaseCount = pW->nBaseCount;
    env.pProductBlock = pW->abProductBlock;
    env.pResearch = &pW->saveResearch;
    env.apCraft = pW->apCraft;
    env.pbCraftCount = &pW->bCraftCount;
    env.pShipEnv = &pW->ctorEnv;
    env.pSmap = &pW->saveSmap;
    env.pView = &pW->view;
    env.paZoomSets = pW->sets;
    env.pWorldSize = &pW->loadWorldSize;
    env.pStrateg = &pW->saveStrateg;
    env.pStrategList = &pW->strategList;
    env.pnSeed = &pW->nStrategSeed;
    env.apSoldiers = pW->apSoldiers;
    env.nSoldierCapacity = PUFO_SOLDIER_CAPACITY;
    env.apObjects = pAssets->apObjects;
    env.nObjectCount = pAssets->nObjectCount;
    env.pnTactVerOut = &pW->nTactVerOut;
    env.pWorld = &pW->world;
    env.abFacilityGrid = pW->abFacilityGrid;
    env.apResearchCols = pW->auResearchCol;
    env.apManufCols = pW->auManufCol;
    env.puResearchEntries = &pW->nResearchEntries;
    env.puShotObjectWords = (u16 *)pAssets->apObjects[0]; /* 0x45764: the shot object's aItemQty words */
    env.pTimeRec = &pW->saveTime;
    env.pTime = &pW->time;
    env.pSpawnCtx = NULL;   /* StrategList_PurgeType5 and Geoscape_SpawnMissionSite need the
                             * mission-spawn leaf, which is not ported; the load path runs with
                             * them absent, as tests/test_save.c does. */
    env.pMissionCtx = NULL;
    env.pbSpawnOnceFlag = NULL;
    env.pHooks = &hooks;

    DatFile_ctor(&dat);
    nRet = LoadGame(&dat, &env);
    DatFile_Close(&dat);
    return nRet;
}
