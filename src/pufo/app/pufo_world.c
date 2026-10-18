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
#include "render/pufo_render.h" /* g_nGlyphPalSlotBase, the shell's one Map_nGlyphPalBase */

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
    env.pbNewBaseMode = &pW->bFacDone; /* byte_187074[4] @ 0x187078 is bBaseFacDone, one byte */
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
    pW->facToggleEnv.pbNewBaseMode = &pW->bFacDone; /* the same 0x187078 byte */
    pW->facToggleEnv.abFacilityGrid = pW->abFacilityGrid;
    pW->facToggleEnv.pnSelectedBaseIdx = &pW->nSelectedBase;
    pW->facToggleEnv.paFac = pW->aFac;
    pW->facToggleEnv.pszLastRowName = (const char *)pW->abLastRowName;
    pW->facToggleEnv.pfnBuyThunk = World_FacBuyThunk;
    BaseFacScreen_Toggle(&pW->facToggleEnv);
}

/* Soldier_FormatDisplayName @ 0x6654c: the soldier screen's craft column. The original writes into
 * the shared scratch string at 0x186B10 and returns it; the port uses the world's own copy of that
 * buffer, szLabelScratch, which is the same slot the placement overlay's label uses. */
static const char *World_SoldierName(void *pUser, Soldier *pSol)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW == NULL || pW->pAssets == NULL) {
        return "";
    }
    return Soldier_FormatDisplayName(pSol, pW->apCraft, pW->pAssets->pTextRecs,
                                     pW->szLabelScratch, sizeof(pW->szLabelScratch));
}

/* SoldierScreen_SelectListed @ 0x64a9c, panel C's click. */
static void World_SoldierSelectThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    SoldierSelectEnv env;

    if (pW == NULL || pW->pSolPanelNames == NULL) {
        return;
    }

    memset(&env, 0, sizeof(env));
    env.pPanel = pW->pSolPanelNames;
    env.pnCursor = &pW->nSoldierCursor;
    env.pnScreenId = &pW->screen.nGeoscapeScreen;
    (void)SoldierScreen_SelectListed(&env);
}

/* SoldierScreen_EquipSelected @ 0x619cc, panel A's equip-row click. */
static void World_SoldierEquipThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    SoldierEquipEnv env;
    u16 nCount;

    if (pW == NULL || pW->pSolPanelEquip == NULL || pW->pAssets == NULL) {
        return;
    }

    nCount = (u16)pW->nSoldierCount;

    memset(&env, 0, sizeof(env));
    env.apSoldiers = pW->apSoldiers;
    env.pnSoldierCount = &nCount;
    env.apCraft = pW->apCraft;
    env.apBases = pW->apBases;
    env.apObjects = pW->pAssets->apObjects;
    env.pnCursor = &pW->nSoldierCursor;
    env.pnSelectedBaseIdx = &pW->nSelectedBase;
    env.pPanelEquip = pW->pSolPanelEquip;
    env.pbTransferFlag = &pW->bTransferFlag;
    (void)SoldierScreen_EquipSelected(&env);
}

static void World_SoldierToggleThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    u16 nCount;

    if (pW == NULL) {
        return;
    }

    nCount = (u16)pW->nSoldierCount; /* unk_115350 is the u16 count */

    memset(&pW->solEnv, 0, sizeof(pW->solEnv));
    pW->solEnv.pbOpen = &pW->bSolScreenOpen;
    pW->solEnv.pnScreenId = &pW->screen.nGeoscapeScreen;
    pW->solEnv.apSoldiers = pW->apSoldiers;
    pW->solEnv.pnSoldierCount = &nCount;
    pW->solEnv.pnSelectedBaseIdx = &pW->nSelectedBase;
    pW->solEnv.ppPanelEquip = &pW->pSolPanelEquip;
    pW->solEnv.ppPanelStores = &pW->pSolPanelStores;
    pW->solEnv.ppPanelNames = &pW->pSolPanelNames;
    pW->solEnv.ppPanelCraft = &pW->pSolPanelCraft;
    pW->solEnv.ppPad = &pW->pSolPad;
    pW->solEnv.pszPadName = "soldier";
    pW->solEnv.pCloseCtl = &pW->ctlFacClose;
    /* The original installs SoldierScreen_CloseThunk (0x64ae4), which only tail-calls
     * Geoscape_CloseUI (0x8b94c); until that dispatcher is ported the seam stays a no-op. */
    pW->solEnv.pfnCloseSoldierThunk = World_NoopClick;
    pW->solEnv.pfnEquipThunk = World_SoldierEquipThunk;
    pW->solEnv.pfnSelectThunk = World_SoldierSelectThunk;
    pW->solEnv.pfnFormatName = World_SoldierName;   /* Soldier_FormatDisplayName @ 0x6654c */
    pW->solEnv.pFormatUser = pW;
    SoldierScreen_Toggle(&pW->solEnv);
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
    env.bPendingRegionId = pW->bPendingRegionId;
    env.pbConfirm = &pW->bPlacementPending;
    env.pbFacDone = &pW->bFacDone;
    env.nPlaceX = pW->nPlaceX;
    env.nPlaceY = pW->nPlaceY;
    env.apBases = pW->apBases;
    env.apSiteRecs = pW->aSiteRecs;
    env.pStrategList = &pW->strategList;
    env.pTextRecs = (pW->pAssets != NULL) ? pW->pAssets->pTextRecs : NULL;
    env.paRegionBuildCount = (i8 *)pW->aRegionBuildCount;
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
    env.pnMsgShipIdx = &pW->nMsgShipIdx;
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

/* StrategList_ProcessEvents @ 0x7fcb0, the geoscape event pass StrategList_MaybeSpawnCraft_Process
 * tail-calls. The four sub-envs the shell does not own yet (the radar session, the tactical-loot
 * env, the mission-spawn ctx and the ship-removal env) are NULL, which skips those side effects;
 * the guards are in the ported function. */
static int World_ProcessEvents(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    StrategEventsEnv env;

    if (pW == NULL) {
        return 0;
    }

    memset(&env, 0, sizeof(env));
    env.pList = &pW->strategList;
    env.pWorld = &pW->world;
    env.pTime = &pW->time;
    env.pLines = &pW->msgLines;
    env.pStatusPanel = pW->ui.pStatusPanel;
    env.pnFundPending = &pW->economy.aZero44_49[5];   /* g_nFundPending @ 0x1865BC */
    env.pbSpawnOnce = &pW->bSpawnOnce;
    env.pbSelectionFlag = &pW->bMapSelectionFlag;
    env.pnMsgShipIdx = &pW->nMsgShipIdx;
    env.pnSelectedShipIdx = &pW->nSelectedShipIdx;
    env.pnInterceptShipIdx = &pW->nInterceptShipIdx;
    env.pnInterceptStrategIdx = &pW->nInterceptStrategIdx;
    env.pnRadarPulseTimer = &pW->nRadarPulseTimer;
    env.pTextRecs = (pW->pAssets != NULL) ? (const char *)pW->pAssets->pTextRecs : NULL;
    env.apSoldiers = pW->apSoldiers;
    env.nSoldierCount = pW->nSoldierCount;
    env.pSpawnCtx = &pW->spawnCtx;
    env.pfnPlaySample = pW->pfnPlaySample;
    env.pSoundUser = pW->pSoundUser;

    return (int)StrategList_ProcessEvents(&env);
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
    pW->bPendingRegionId = 0;
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
    createEnv.paRegionBuildCount = pW->aRegionBuildCount;
    createEnv.pProductU32Col = pW->auProductU32;
    createEnv.pProductU16Col = pW->auProductU16;
    createEnv.pResearchCol = pW->auResearchCol;

    /* The original reads the difficulty and the day gate from globals inside the callee; the
     * port lifted them into arguments. The shell has no difficulty setting yet, so it uses
     * difficulty 0 and an open gate (99), the same pair tests/test_strateg.c pins. */
    memset(&pW->spawnCtx, 0, sizeof(pW->spawnCtx));
    pW->spawnCtx.pTables = &g_WorldRoll;
    pW->spawnCtx.nDifficulty = 0;
    pW->spawnCtx.nEarlyGate = 99;
    pW->spawnCtx.nZoomMode = pW->view.nMode;
    pW->spawnCtx.pnSeed = &pW->nStrategSeed;
    pW->spawnCtx.pfnSpawnMission = World_SpawnMissionStub;
    pW->spawnCtx.pfnProcessEvents = World_ProcessEvents;
    pW->spawnCtx.pUser = pW;

    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        pW->navEnv.apfnNavClick[i] = World_NoopClick;
    }
    pW->navEnv.apfnNavClick[2] = World_SoldierToggleThunk; /* SoldierScreen_Toggle, 0x67B88 */
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
    env.pSpawnCtx = &pW->spawnCtx;
    env.pbPlacementPending = &pW->bPlacementPending;
    env.pbStrategCount = &pW->bStrategCount;
    env.abTypeCounters = pW->abTypeCounters;
    env.pnNextDisplayId = &pW->nNextDisplayId;
    env.pnNextSiteId = &pW->nNextSiteId;
    env.pnMissionId = &pW->nMissionId;
    env.abSaveFlags = pW->aRegionBuildCount;
    env.pEconomy = &pW->economy;
    env.paFacFlags = pW->aFac;
    env.pTextRecs = pAssets->pTextRecs;
    env.pScreen = &pW->screen;
    env.pCtlResearchPanel = &pW->ctlResearchPanel;
    env.pCtlConfirm = &pW->ctlConfirm;
    env.pView = &pW->view;
    env.pZoomSets = pW->sets;
    env.pWorldOut = pW->ui.pWorldSize; /* the pair InitUI allocated at 0x186BC0 */
    env.pbFlag187080 = &pW->bUiAnimStarted; /* bUiAnimStarted @ 0x187080 */
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
    env.pnGlyphPalBase = &g_nGlyphPalSlotBase; /* the original's one global, Map_nGlyphPalBase */
    env.abFacilityGrid = pW->abFacilityGrid;
    env.pWorld = &pW->world;
    env.pbBaseCount = &pW->nBaseCount;
    env.pnUnk159DC4 = &pW->nUnk159DC4;
    env.paMissionSites = pW->aMissionSites;
    env.aRegionBuildCount = pW->aRegionBuildCount;
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

    /* The pads and controls InitUI just created are what the geoscape frame reads. */
    World_WireGeoscapeFrame(pW, pHooks);

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
    /* Same for the soldier screen's four panels and its pad. */
    if (pW->bSolScreenOpen != 0) {
        World_SoldierToggleThunk();
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

    /* LoadGame renders through the frame (its teardown calls pfnRenderFrame), so the frame envs are
     * wired before the readers run. On this path Geoscape_InitUI is a stub (Load_HookInitUi), so the
     * 0x1864F8 pad and control singletons are whatever a previous new game left, or NULL. */
    pW->pAssets = pAssets;
    pW->pfnLoadPads = pHooks->pfnLoadPads;
    pW->pLoadPadsUser = pHooks->pLoadPadsUser;
    pW->pfnPlaySample = pHooks->pfnPlaySample;
    pW->pSoundUser = pHooks->pSoundUser;
    World_WireGeoscapeFrame(pW, pHooks);

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

/* ---- the geoscape frame wiring ------------------------------------------------------------
 *
 * Geoscape_Draw @ 0x8C180 reaches everything through the 0x1864F8 root block and the loose map and
 * panel globals; the port takes it all through the envs held in pW->frame. Every pointer here is the
 * shell's own copy of the original global named in its comment.
 *
 * What is NOT wired, and what that means on screen.
 *   - The radar arm's RadarUpdateEnv (the call at 0x8C298). Its eight sub-envs (the palette cycle, the
 *     range bar, the fuel and armament readouts, the overlay, the detect line, the ship-removal exits,
 *     the funding block) need the aRadarControls set at 0x159684 and the radar panel state, which the
 *     shell does not own. The arm runs only while bRadarScreenActive @ 0x159678 is set, and nothing in
 *     the shell sets that byte yet, so screens 0 and 2 draw the map arm.
 *   - The status panel's four screen openers (Geoscape_PrepScreenOpenSfx 0x852b8, Geoscape_PrepScreenOpen
 *     0x85194, Ui_OpenBaseInfoAlt 0x84d18, Ui_OpenResearch 0x84bf8). Each ends in a tail call into the
 *     ufopaedia, research or base-info screen's Init over that screen's own block (0x1872E0, 0x160C68,
 *     the 0x15EF08 panel pointers), none of which the shell owns. UiControl_HitTest tests the handler
 *     pointer, so the two bottom controls draw and simply do nothing on a click.
 *   - The same for the panel's strateg-list handlers (Smap_* @ 0x84254..0x84964) and the craft-select
 *     pair: they take their own Smap envs, which need the strateg item the line refers to.
 *   - The other nineteen arms of the screen switch, and with them the screen ids they draw. The shell's
 *     nav controls carry World_NoopClick handlers, so nGeoscapeScreen never leaves 0/2 here, and
 *     Geoscape_Draw dereferences several unwired arm envs with no test.
 *   - GeoscapePointerEnv's per-screen fields for those same screens (the strip, transfer, soldier-name,
 *     craft, build-dialog, research, manufacturing, site-info, buy/sell, screen-16, options and
 *     base-at blocks) stay NULL. The dispatcher reaches them only from those screen ids. Two of the
 *     shell-owned ones cannot be expressed yet: apFacCtl wants an array of the two build-dialog
 *     controls, which the shell holds as two embedded UiControls, and apRadarCtl wants 0x159684's
 *     control set, which the shell does not build.
 */
void World_WireGeoscapeFrame(PufoWorldState *pW, const WorldHooks *pHooks)
{
    GeoscapeFrameState *pF = &pW->frame;
    GeoscapeUiState    *pUi = &pW->ui;
    BootAssets         *pAssets = pW->pAssets;
    GeoscapePointerEnv *pPtr = &pW->ptrEnv;
    const UiControlHitCtx *pHit = (pHooks != NULL) ? pHooks->pHit : NULL;
    GfxColorEnv        *pColor = (pHooks != NULL) ? pHooks->pColor : NULL;

    memset(pF, 0, sizeof(*pF));
    memset(pPtr, 0, sizeof(*pPtr));

    /* The 0x1864F8 anim root's three bytes: 0x187080, 0x187058 and the frame style at 0x186566, which
     * the shell keeps as the tenth byte of its region-counter run. */
    pF->anim.pbStarted   = &pW->bUiAnimStarted;
    pF->anim.pnProgress  = &pW->cUiAnimProgress;
    pF->anim.nFrameStyle = (i8)pW->aRegionBuildCount[9];

    /* BaseFac_BuildStatusMsg @ 0x6a4bc, the panel head's call at 0x8C82C. */
    pF->fac.pTime          = &pW->time;
    pF->fac.abFacilityGrid = pW->abFacilityGrid;
    pF->fac.apSiteRecs     = pW->aSiteRecs;
    pF->fac.paFac          = pW->aFac;
    pF->fac.aTopics        = pW->aTopics;
    pF->fac.apCraft        = pW->apCraft;
    pF->fac.pszMsg         = pW->szLabelScratch;
    pF->fac.cchMsg         = sizeof(pW->szLabelScratch);
    pF->fac.pszExtra       = pW->szUiScratchStr;
    pF->fac.cchExtra       = sizeof(pW->szUiScratchStr);
    pF->fac.pszScratch     = pW->szScratch;
    pF->fac.cchScratch     = sizeof(pW->szScratch);
    if (pAssets != NULL) {
        pF->fac.apObjects   = pAssets->apObjects;
        pF->fac.nObjectCount = pAssets->nObjectCount;
        pF->fac.pTextRecs   = pAssets->pTextRecs;
    }

    /* The map arm, 0x8C2B0..0x8C690. The two pads and the screen id follow the zoom set, so they are
     * refreshed every frame; see World_DrawGeoscapeFrame. */
    pF->map.pView      = &pW->view;
    pF->map.pWorldBits = (const u8 *)pUi->pWorldSize; /* *pSmapWorldBits @ 0x186BC0: the 4 + 800*400
                                                       * glyph record InitUI allocated */
    pF->map.pMapPal    = g_aPalette[PUFO_RADAR_MAP_PAL_SLOT]; /* aPalette + 0x7400 */
    pF->map.pTime      = &pW->time;
    pF->map.pnOverlayOriginX = &pF->g.nOverlayOriginX;

    /* The crosshair tail, 0x8C69C..0x8C760. */
    pF->cross.pChromePad  = pUi->apPad[GEOUI_PAD_186BBC]; /* *pGeoscapeStripPad @ 0x186BBC */
    pF->cross.pbPending   = &pF->g.bCrosshairPending;     /* bSmapCrosshairPending @ 0x1870B0 */
    pF->cross.pnCooldownR = &pF->g.nTapCooldown;          /* nRadarCooldownR @ 0xAC218 */

    /* The radar arm, 0x8C214..0x8C29C: everything but RadarScreen_Update's own env. */
    pF->radar.pbRadarActive = &pW->bRadarScreenActive;    /* bRadarScreenActive @ 0x159678 */
    pF->radar.pStripPad     = pUi->apPad[GEOUI_PAD_186BBC];
    pF->radar.apRootCtl     = pUi->apRootCtl;             /* 0x8C238 loads 0x1864F8 + 4*i */

    /* The status panel, 0x8C760..0x91DB0 with its chain tail. */
    pF->panel.pFac            = &pF->fac;
    pF->panel.pLines          = &pW->msgLines;            /* the root message list @ 0x158890 */
    pF->panel.pAnim           = &pF->anim;
    pF->panel.pColor          = pColor;
    pF->panel.pCtlSfx         = pUi->apCtl[GEOUI_CTL_18704C]; /* *unk_18704C, the left bottom control */
    pF->panel.pCtlOpen        = pUi->apCtl[GEOUI_CTL_187024]; /* *unk_187024, the right one */
    pF->panel.pCtlConfirm     = pUi->apCtl[GEOUI_CTL_186BA4]; /* *pGeoscapeCtlConfirm */
    pF->panel.pCtlApply       = pUi->apCtl[GEOUI_CTL_186BAC]; /* *pGeoscapeCtlApply */
    pF->panel.pStatusPanel    = pUi->pStatusPanel;            /* pStatusPanel @ 0x18641C */
    pF->panel.pbMultiSelectPanel = &pW->bFlag1870A0;          /* byte_18709C[4] @ 0x1870A0 */
    pF->panel.aTopics         = pW->aTopics;                  /* aResearchTopics @ 0x15EF08 */
    pF->panel.abResearchActive = pW->abResearchActive;        /* byte_160BBC */
    pF->panel.pbFinishedTopic = &pW->nFinishedTopic;          /* nResearchFinishedTopic @ 0x15EEF8 */
    pF->panel.pnShownTopic    = &pW->nShownTopic;             /* nResearchTopicShown @ 0x160C60 */
    pF->panel.pView           = &pW->view;                    /* nMapW / 2 - 120 is the arm's R8 */
    pF->panel.pStrategList    = &pW->strategList;             /* the root block's PtrArray at +0x3C */
    pF->panel.pnActionFail    = (const i32 *)&pW->nShortfall1870C8;   /* dwActionFail @ 0x1870C8 */
    pF->panel.pnActionFailReason = &pW->nResidue1870B4;               /* dwActionFailReason @ 0x1870B4 */
    pF->panel.pbMapSelectionFlag = &pW->bMapSelectionFlag;           /* 0x159E84 */
    pF->panel.pszRadarMsg     = pW->szLabelScratch;                  /* szRadarMsg @ 0x186B10 */
    pF->panel.pszHyper        = pW->szUiScratchStr;                  /* szUiScratchStr @ 0x186420 */
    pF->panel.pCtlA           = pUi->apCtl[GEOUI_CTL_186BB4];        /* *pGeoscapeCtlA */
    pF->panel.pCtlB           = pUi->apCtl[GEOUI_CTL_186BB0];        /* *pGeoscapeCtlB */
    pF->panel.apCraft         = pW->apCraft;                         /* Map + 0x10AC */
    pF->panel.apSiteRecs      = pW->aSiteRecs;                       /* aSiteRecs @ 0x186C40 */
    pF->panel.pnSelectedShipIdx = &pW->nSelectedShipIdx;             /* 0x187054 */
    pF->panel.pStripPad       = pUi->apPad[GEOUI_PAD_186BBC];       /* re-blitted behind the card */
    pF->panel.pbInterceptDone = &pW->bInterceptDone;                /* bInterceptDone @ 0x18709C */
    pF->panel.pCtlReturnBase  = pUi->apCtl[GEOUI_CTL_186BA8];
    pF->panel.pCtlNewTarget   = pUi->apCtl[GEOUI_CTL_186B9C];
    pF->panel.pCtlPatrol      = pUi->apCtl[GEOUI_CTL_186B98];
    pF->panel.pTime           = &pW->time;                           /* 0x159618: GameTime_Pause's byte */
    pF->panel.paMissionSites  = pW->aMissionSites;                   /* unk_1865C0 */
    pF->panel.apRootCtl       = pUi->apRootCtl;                      /* 0x1864F8 + 4*i */
    pF->panel.apClockDigitPads = pUi->apClockDigitPads;              /* apClockDigitPads @ 0x186BF0 */
    pF->panel.pbUfoCrashPanel = &pW->bFlag18708C;
    pF->panel.pbSitePanel     = &pW->bFlag187090;
    pF->panel.pbAlienBasePanel = &pW->bFlag187098;
    pF->panel.pFinalMissionCtl = pUi->apCtl[GEOUI_CTL_186B8C];      /* pFinalMissionCtl @ 0x186B8C */
    pF->panel.pbFinalMissionCtl = &pW->bFinalMissionCtlArmed;       /* 0x187068 */
    pF->panel.apSoldiers      = pW->apSoldiers;                     /* Map + 0x1198 */
    pF->panel.pnSoldierCount  = (const u16 *)&pW->nSoldierCount;    /* Map + 0x1998, read as a halfword */
    pF->panel.pnScreenId      = &pW->screen.nGeoscapeScreen;        /* 0x186BC4: 2 picks the hover arm */
    pF->panel.pGlobeEnv       = &pF->hover;                         /* the call at 0x91D8C */
    pF->panel.pbPendingRegionId = &pW->bPendingRegionId;            /* bPendingBaseKind @ 0x159DF4 */
    pF->panel.pbPlacementPending = &pW->bPlacementPending;          /* 0x1870AC */
    pF->panel.pCloseCtl       = pUi->apCtl[GEOUI_CTL_186B00];       /* *g_pUiCloseCtl @ 0x186B00 */

    /* The new-base globe hover, 0x67264..0x67b10, reached from the chain tail at 0x91D8C. */
    pF->hover.pStripPad  = pUi->apPad[GEOUI_PAD_186BBC];
    pF->hover.pView      = &pW->view;
    pF->hover.pAnim      = &pF->anim;
    pF->hover.pnPendingPickX = &pW->nPlaceX;                        /* 0x159D94 */
    pF->hover.pnPendingPickY = &pW->nPlaceY;                        /* 0x159D98 */
    pF->hover.pnScanX    = &pW->nGlobeScanX;                        /* 0x159E00 */
    pF->hover.pnSweepV   = &pW->nGlobeSweepV;                       /* 0x159DFC */
    pF->hover.pnSweepH   = &pW->nGlobeSweepH;                       /* 0x159DF8 */
    pF->hover.pbPlacementPending = &pW->bPlacementPending;
    pF->hover.pCtlConfirm = pUi->apCtl[GEOUI_CTL_186BA4];           /* "Build base" */
    pF->hover.paRegionCost = (const u32 *)g_anNewBaseCostByRegion;  /* aNewBaseCostByRegion @ 0xAB488 */
    pF->hover.pColor     = pColor;

    /* The frame end, 0x944B8..0x94544. Its modal arm runs only for MainMenu_nListMode 1103/1102, the
     * start menu's own modal pair, so the modal anim and layout stay NULL on the geoscape path; and
     * case 20's BaseAtScreen_Draw env belongs to the base-at screen, which is not wired. */
    pF->frameEnd.pnListMode         = &pW->nListMode;              /* MainMenu_nListMode @ 0x157188 */
    pF->frameEnd.pnActionPhaseSave112 = &pW->nActionPhaseSave112;  /* Map.anActionPhaseSave0[112] */
    if (pAssets != NULL) {
        pF->frameEnd.pTextRecs = pAssets->pTextRecs;
    }

    /* The control plumbing every blit above goes through. */
    if (pHit != NULL) {
        pF->radar.pfnCtlBlit     = pHit->pfnBlit;
        pF->radar.pfnCtlBlitEx   = pHit->pfnBlitEx;
        pF->radar.pCtlBlitUser   = pHit->pUser;
        pF->panel.pfnCtlBlit     = pHit->pfnBlit;
        pF->panel.pfnCtlBlitEx   = pHit->pfnBlitEx;
        pF->panel.pCtlBlitUser   = pHit->pUser;
        pF->hover.pfnCtlBlit     = pHit->pfnBlit;
        pF->hover.pfnCtlBlitEx   = pHit->pfnBlitEx;
        pF->hover.pCtlBlitUser   = pHit->pUser;
    }
    if (pAssets != NULL) {
        pF->panel.pTextRecs = (const char *)pAssets->pTextRecs; /* aTextRecs @ 0xB38D0 */
        pF->hover.pTextRecs = (const char *)pAssets->pTextRecs;
    }

    /* The pointer dispatcher, Geoscape_OnPointer @ 0x872d0 and Geoscape_OnPointerUp @ 0x89450. Its
     * env shares one pUser between pfnPlaySample and pfnHitTestStrategSelect, so the shell sets it to
     * the sound user and leaves the strateg seam NULL (its SmapHitEnv is not built yet). */
    pPtr->apRootCtl  = pUi->apRootCtl;
    pPtr->pbPopupA   = &pW->bRoot34;                    /* bPopupVisibleA @ 0x18652C */
    pPtr->pbPopupB   = &pW->bRoot35;                    /* bPopupVisibleB @ 0x18652D */
    pPtr->apCtl      = pUi->apCtl;
    pPtr->pnScreenId = &pW->screen.nGeoscapeScreen;
    pPtr->pLines     = &pW->msgLines;
    pPtr->pnCursorX  = &pF->g.nCursorX;                 /* 0x186BD0, stored as a halfword */
    pPtr->pnCursorY  = &pF->g.nCursorY;                 /* 0x186BD4 */
    pPtr->pnTapX     = &pW->nSmapTapX;                  /* 0x186B7C */
    pPtr->pnTapY     = &pW->nSmapTapY;                  /* 0x186B80 */
    pPtr->pbDragActive = &pF->g.bDragActive;            /* 0x187074, byte 0 */
    pPtr->pbNewBaseMode = &pW->bFacDone;                /* 0x187078 */
    pPtr->pbPlaceFail   = &pW->bPlaceFail;              /* 0x1870B8 */
    pPtr->pbPlaceActive = &pW->bPlaceActive;            /* 0x1870BC */
    pPtr->pbFinalMissionCtlArmed = &pW->bFinalMissionCtlArmed;
    pPtr->pbInterceptDone     = &pW->bInterceptDone;
    pPtr->pbMultiSelectPanel  = &pW->bFlag1870A0;
    pPtr->pbPlacementPending  = &pW->bPlacementPending;
    pPtr->pbPendingRegionId   = &pW->bPendingRegionId;
    pPtr->pbMapSelectionFlag  = &pW->bMapSelectionFlag;
    pPtr->pView          = &pW->view;
    pPtr->pZoomOverlay   = pUi->apPad[GEOUI_PAD_186BBC]; /* 0x87C48 reads its uHeight as the strip */
    pPtr->pnTapCooldown  = &pF->g.nTapCooldown;
    pPtr->pnRadarPulseTimer = &pW->nRadarPulseTimer;     /* 0xAC224 */
    pPtr->pbRadarScreenActive = &pW->bRadarScreenActive;
    pPtr->pStatusPanel   = pUi->pStatusPanel;
    pPtr->pnSelectedBaseIdx = &pW->nSelectedBase;        /* 0x159D81 */
    pPtr->pnBaseCount    = &pW->nBaseCount;              /* 0x159D80, signed */
    pPtr->pbSolScreenOpen = &pW->bSolScreenOpen;         /* 0x159DE0 */
    pPtr->pSolPanelNames = pW->pSolPanelNames;           /* 0x15975C */
    pPtr->pSolPanelEquip = pW->pSolPanelEquip;           /* 0x159DF0 */
    pPtr->apSolPanelCtl  = pW->nav.apPanel;              /* apSoldierPanelCtl @ 0x1598B0 */
    pPtr->apSolFindCtl   = pW->nav.apFind;               /* apSoldierFindCtl @ 0x1598B8 */
    pPtr->pbUiAnimStarted = &pW->bUiAnimStarted;         /* 0x187080 */
    pPtr->pbTransferFlag = &pW->bTransferFlag;           /* 0x159E10 */
    pPtr->pFacPanel      = pW->pFacPanel;                /* 0x15886C */
    pPtr->pbFacPlaceArmed = &pW->bArmedPlacement;        /* 0x158888 */
    pPtr->pdwShortfall   = &pW->nShortfall1870C8;        /* 0x1870C8, read and written as a dword */
    pPtr->pnPendingPlaceX = &pW->nUnk15887C;             /* nPendingPlaceX @ 0x15887C */
    pPtr->pnPendingPlaceY = &pW->nUnk158880;             /* nPendingPlaceY @ 0x158880 */
    pPtr->apCraft        = pW->apCraft;
    pPtr->pHit           = pHit;
    pPtr->pfnPlaySample  = pW->pfnPlaySample;
    pPtr->pUser          = pW->pSoundUser;
    pPtr->pfnHitTestStrategSelect = (pHooks != NULL) ? pHooks->pfnHitTestStrategSelect : NULL;

    /* The dispatcher's arm wiring, 0x8C1B4..0x8C20C: the four envs the two map screens need plus the
     * frame end. Every other table word stays NULL, which is the arm not drawing at all. */
    pF->draw.pnScreenId   = &pW->screen.nGeoscapeScreen;
    pF->draw.pRadarArm    = &pF->radar;
    pF->draw.pMapView     = &pF->map;
    pF->draw.pCrosshair   = &pF->cross;
    pF->draw.pStatusPanel = &pF->panel;
    pF->draw.pFrameEnd    = &pF->frameEnd;
}

/* The per-frame fields: the original keeps the map pad, the layer pad and the tile grid in the three
 * globals the zoom handler publishes (0x186E40, 0x186BE0, 0x186B04), and the port keeps them as the
 * zoom set the view's mode selects. The save reader restores nMode without a bound (0x40C78), so the
 * trio lookup is clamped to the three levels the shell holds. */
static const SmapZoomSet *World_CurrentZoomSet(PufoWorldState *pW)
{
    i32 nMode = pW->view.nMode;

    if (nMode < 0 || nMode > 2) {
        nMode = 0;
    }
    return &pW->sets[nMode];
}

static void World_RefreshGeoscapeFrame(PufoWorldState *pW)
{
    GeoscapeFrameState *pF = &pW->frame;
    const SmapZoomSet  *pSet = World_CurrentZoomSet(pW);

    pF->map.pMapPad   = pSet->pGrid;    /* *pSmapPadCur @ 0x186E40 */
    pF->map.pLayerPad = pSet->pOverlay; /* *pSmapLayerPad @ 0x186BE0 */
    pF->map.nScreenId = pW->screen.nGeoscapeScreen;
    pF->hover.pTiles  = (pSet->pTiles != NULL) ? pSet->pTiles->pData : NULL; /* [0x186B04]->pData */
    pF->cross.nCrosshairX = pF->g.nCrosshairX;   /* nSmapCrosshairX @ 0x186BC8 */
    pF->cross.nCrosshairY = pF->g.nCrosshairY;   /* nSmapCrosshairY @ 0x186BCC */
    pF->anim.nFrameStyle  = (i8)pW->aRegionBuildCount[9]; /* bUiFrameStyle @ 0x186566 */
    pW->ptrEnv.pTiles     = (pSet->pTiles != NULL) ? pSet->pTiles->pData : NULL;
}

i32 World_DrawGeoscapeFrame(PufoWorldState *pW, i32 (*pfnPresent)(void *pUser), void *pUser)
{
    World_RefreshGeoscapeFrame(pW);
    pW->frame.frameEnd.pfnPresent   = pfnPresent;
    pW->frame.frameEnd.pPresentUser = pUser;
    return Geoscape_Draw(&pW->frame.draw);
}

void World_GeoscapePointer(PufoWorldState *pW, i32 nX, i32 nY, bool bDown)
{
    /* The dispatcher reads the strip height off the current overlay pad and the tile grid off the
     * current zoom set, so both are refreshed with the view's mode first. */
    World_RefreshGeoscapeFrame(pW);
    (void)Geoscape_OnPointer(&pW->ptrEnv, nX, nY, bDown);
}

void World_GeoscapePointerUp(PufoWorldState *pW)
{
    (void)Geoscape_OnPointerUp(&pW->ptrEnv);
}
