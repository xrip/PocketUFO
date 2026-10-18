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
#include "ui/pufo_linelist.h" /* UiLineList_AppendWithSound, the tick's contact message seam */

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

/* Twelve 12-byte close thunks tail-call Geoscape_CloseUI @ 0x8b94c over the root block: BaseInfoScreen
 * @ 0x557bc, BaseFacScreen_CloseThunkA @ 0x589c0, BaseFacScreen_CloseThunk @ 0x59b54, FundingScreen
 * @ 0x5abd8, FundingScreen_ExitThunk @ 0x5c078, SoldierScreen @ 0x64ae4, ManufScreen @ 0x6e3d4,
 * ResearchScreen @ 0x72ffc, BuySellScreen @ 0x75154, and Geoscape_CloseUI_ThunkA/B/C @ 0x838d0, 0x96020,
 * 0x9a8c0. UiControl.pfnClick takes no arguments, so the shell reaches the same dispatcher through the
 * active pointer and the env World_WireGeoscapeClock built -- one handler is enough for all twelve.
 * NewBaseScreen_CloseThunk @ 0x615b0 is the odd one: it plays sample 17 (0x615bc) first. */
static void World_CloseGeoscapeThunk(void)
{
    if (g_pActiveWorld != NULL) {
        World_CloseGeoscape(g_pActiveWorld);
    }
}

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
    pW->solEnv.pCloseCtl = pW->ui.apCtl[GEOUI_CTL_186B00]; /* *g_pUiCloseCtl @ 0x186B00, the one instance */
    /* SoldierScreen_CloseThunk @ 0x64ae4 is a 12-byte tail call into Geoscape_CloseUI @ 0x8b94c, stored at
     * +12 by SoldierScreen_Toggle (0x651AC). */
    pW->solEnv.pfnCloseSoldierThunk = World_CloseGeoscapeThunk;
    pW->solEnv.pfnEquipThunk = World_SoldierEquipThunk;
    pW->solEnv.pfnSelectThunk = World_SoldierSelectThunk;
    pW->solEnv.pfnFormatName = World_SoldierName;   /* Soldier_FormatDisplayName @ 0x6654c */
    pW->solEnv.pFormatUser = pW;
    SoldierScreen_Toggle(&pW->solEnv);
}

/* Geoscape_OnMapClick @ 0x8b4a8, reached from NewBaseScreen_TryConfirm @ 0x6152c and from
 * Geoscape_OnMapClick_Thunk @ 0x857e8. Its screen-2 branch is the game's own way out of the new-base
 * pick: 0x8B600 writes the screen id 1 and 0x8B608 clears the pending byte Game_StartNew set at 0x945E0. */
void World_MapClick(PufoWorldState *pW)
{
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

/* UiControl.pfnClick takes no arguments, so the installed handler is this wrapper. */
static void World_TryConfirmClick(void)
{
    World_MapClick(g_pActiveWorld);
}

/* NewBaseScreen_CloseThunk @ 0x615b0: Sound_Play(17) at 0x615bc first, then the dispatcher. The only
 * close thunk in the image that plays a sample. */
static void World_NewBaseCloseThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW == NULL) {
        return;
    }
    if (pW->pfnPlaySample != NULL) {
        pW->pfnPlaySample(17, pW->pSoundUser);
    }
    World_CloseGeoscape(pW);
}

/* The screen openers call Sound_Play with a SECOND argument (MOV R1,#4 at 0x614D4, 0x61504, 0x615B4, ...),
 * which the shell's own two-argument message-list seam does not carry. This adapter is what the opener and
 * flag hooks get; its pUser is the world, so it can reach that seam. */
static void World_PlaySampleWithArg(u32 nSample, u32 nArg, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    (void)nArg;
    if (pW != NULL && pW->pfnPlaySample != NULL) {
        pW->pfnPlaySample((i32)nSample, pW->pSoundUser);
    }
}

/* NewBaseScreen_Open @ 0x615d0 -- nav control slot 0's handler (installed at 0x67B74). Sample 17, the
 * close control to (224,1), the id 2 written at 0x61620, the confirm handler, two Smap_ZoomOut on the
 * camera, 0x187080 cleared, then the close handler at 0x61654. The gate is the signed base count
 * (0x159D80) against 8: with eight bases the opener does nothing at all. */
static void World_NewBaseOpenThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)NewBaseScreen_Open(&g_pActiveWorld->screen, &g_pActiveWorld->newBaseOpen);
    }
}

/* BaseFacScreen_Init @ 0x5a198, reached through the opener's Init hook. */
static void World_BaseFacInitThunk(void *pSlot, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    (void)pSlot; /* the original passes its own slot global; the port takes the env instead */
    if (pW != NULL) {
        (void)BaseFacScreen_Init(&pW->facInitEnv);
    }
}

/* BaseFacScreen_Open @ 0x61844 -- nav control slot 4's handler (installed at 0x67BA4): sample 17, the id 7
 * BEFORE the Init (0x6185C), then the tail call into BaseFacScreen_Init. */
static void World_BaseFacOpenThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BaseFacScreen_Open(&g_pActiveWorld->screen, &g_pActiveWorld->facOpenHooks);
    }
}

/* BaseFacScreen_PanelPrev @ 0x589cc / PanelNext @ 0x589dc: the dialog's one panel at 0x15886C. */
static void World_FacPanelPrevThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BaseFacScreen_PanelPrev(g_pActiveWorld->pFacPanel);
    }
}

static void World_FacPanelNextThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BaseFacScreen_PanelNext(g_pActiveWorld->pFacPanel);
    }
}

/* SoldierScreen_PanelPrev @ 0x61874 / PanelNext @ 0x6189c -- the two page buttons (0x67C84, 0x67C90). The
 * pair is the names panel then the craft panel, and the second call is the one the original returns. */
static void World_SolPanelPrevThunk(void)
{
    if (g_pActiveWorld != NULL) {
        SoldierScreen_PanelPrev(g_pActiveWorld->pSolPanelNames, g_pActiveWorld->pSolPanelCraft);
    }
}

static void World_SolPanelNextThunk(void)
{
    if (g_pActiveWorld != NULL) {
        SoldierScreen_PanelNext(g_pActiveWorld->pSolPanelNames, g_pActiveWorld->pSolPanelCraft);
    }
}

/* SoldierScreen_FindNextAtBase @ 0x6192c / FindPrevAtBase @ 0x618c4 -- the two find buttons (0x67D10,
 * 0x67D1C). They move nSoldierCursor @ 0x159E08 to the next/previous soldier of the SELECTED base; Prev
 * never reads the count (the original's walk is bounded by zero), which the port keeps. */
static void World_SolFindNextThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW != NULL) {
        (void)SoldierScreen_FindNextAtBase(&pW->nSoldierCursor, pW->apSoldiers, (u16)pW->nSoldierCount,
                                           pW->nSelectedBase);
    }
}

static void World_SolFindPrevThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW != NULL) {
        (void)SoldierScreen_FindPrevAtBase(&pW->nSoldierCursor, pW->apSoldiers, pW->nSelectedBase);
    }
}

/* Ui_SetTransferFlag @ 0x619a8 -- the third find button (0x67D28): Sound_Play(17, 4) then
 * bTransferFlag @ 0x159E10 = 1. */
static void World_TransferFlagThunk(void)
{
    if (g_pActiveWorld != NULL) {
        Ui_SetTransferFlag(&g_pActiveWorld->bTransferFlag, World_PlaySampleWithArg, g_pActiveWorld);
    }
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
    env.pTime = &pW->geo.time;
    env.pWorld = &pW->geo.world;
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
    env.pWorld = &pW->geo.world;
    env.pTime = &pW->geo.time;
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
    pW->nSmapOriginX = 10; /* nSmapOriginX @ 0x159DC4 starts at 10; 0x159DC8 has no writer at all */
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

/* Base_CreateNamedEntry @ 0x66930 reads every one of its inputs from a global at the call: the base count
 * (0x159D80), the next site id (dword_186558), the zoom (0x1863E8) and the three economy columns already
 * OFFSET to the base it is about to create -- 0x159E9C + 800*n (u32), 0x15D09C + 400*n (u16),
 * 0x16022C + 288*n (u16), 200/200/144 entries per base. The port takes them through BaseCreateEnv, so the
 * shell fills it from its own state and re-offsets the columns with the count: Game_StartNew creates base 0
 * (0x94964) and the placement overlay's commit creates base *pbBaseCount (0x59AFC). */
static void World_WireCreateEnv(PufoWorldState *pW)
{
    BaseCreateEnv *pC = &pW->createEnv;
    BootAssets    *pAssets = pW->pAssets;
    i32            nBase = (i32)pW->nBaseCount;

    memset(pC, 0, sizeof(*pC));
    pC->apBases = pW->apBases;
    pC->apObjects = pAssets->apObjects;
    pC->nObjectCount = pAssets->nObjectCount;
    pC->pbBaseCount = &pW->nBaseCount;
    pC->pStrategList = &pW->strategList;
    pC->pnSeed = &pW->nStrategSeed;
    pC->nZoomMode = pW->view.nMode;                    /* 0x1863E8, read at the call */
    pC->pnNextSiteId = &pW->nCreateNextSiteId;         /* dword_186558 */
    pC->apSiteRecs = pW->aSiteRecs;                    /* 0x186C40, 64-byte records */
    pC->paRegionBuildCount = pW->aRegionBuildCount;    /* 0x18655D */
    pC->pProductU32Col = pW->auProductU32 + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase;
    pC->pProductU16Col = pW->auProductU16 + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase;
    pC->pResearchCol   = pW->auResearchCol + PUFO_RESEARCH_TOPIC_COUNT * nBase;
}

/* The mode-0 hook: Game_StartNew over the same state. */
static void World_StartNewHook(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    BootAssets *pAssets = pW->pAssets;
    UnitCreateEnv unitEnv;
    ShipSpawnEnv shipEnv;
    StartNewEnv env;
    i32 i;

    World_WireCreateEnv(pW); /* the create env is on the state now: the overlay commit needs it too */

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

    /* The fifteen handlers Geoscape_BuildNavControls installs (0x67B74..0x67D28). Three of the ten nav
     * slots are filled: the shell can draw the screen each one opens. The other seven -- BaseInfoScreen_Open
     * (id 3), BasePlaneScreen_Open (6), ResearchScreen_Open (8), ManufScreen_Open (9), TransferScreen_Open (10),
     * BuySellScreen_OpenBuy (11) and BuySellScreen_OpenSell (12) -- stay no-ops: their Init envs and their
     * Geoscape_Draw arms (pBaseInfoArm, pPlaneArm, pResearchDraw, pManufPad, pTransferPad, pBuySellPad) are
     * not built here, so opening them would land on a screen the shell cannot draw. */
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        pW->navEnv.apfnNavClick[i] = World_NoopClick;
    }
    pW->navEnv.apfnNavClick[0] = World_NewBaseOpenThunk;   /* NewBaseScreen_Open @ 0x615d0, id 2 (0x67B74) */
    pW->navEnv.apfnNavClick[2] = World_SoldierToggleThunk; /* SoldierScreen_Toggle @ 0x64af0, id 4 (0x67B8C) */
    pW->navEnv.apfnNavClick[4] = World_BaseFacOpenThunk;   /* BaseFacScreen_Open @ 0x61844, id 7 (0x67BA4) */
    pW->navEnv.apfnPanelClick[0] = World_SolPanelPrevThunk; /* SoldierScreen_PanelPrev @ 0x61874 (0x67C84) */
    pW->navEnv.apfnPanelClick[1] = World_SolPanelNextThunk; /* SoldierScreen_PanelNext @ 0x6189c (0x67C90) */
    pW->navEnv.apfnFindClick[0] = World_SolFindNextThunk;   /* SoldierScreen_FindNextAtBase @ 0x6192c (0x67D10) */
    pW->navEnv.apfnFindClick[1] = World_SolFindPrevThunk;   /* SoldierScreen_FindPrevAtBase @ 0x618c4 (0x67D1C) */
    pW->navEnv.apfnFindClick[2] = World_TransferFlagThunk;  /* Ui_SetTransferFlag @ 0x619a8 (0x67D28) */
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
    env.pTime = &pW->geo.time;
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
    pW->facInitEnv.pCloseCtl = pW->ui.apCtl[GEOUI_CTL_186B00]; /* *g_pUiCloseCtl, not a second instance */
    pW->facInitEnv.pUpCtl = &pW->ctlFacUp;
    pW->facInitEnv.pDownCtl = &pW->ctlFacDown;
    pW->facInitEnv.pnUnk15887C = &pW->nUnk15887C;
    pW->facInitEnv.pnUnk158880 = &pW->nUnk158880;
    pW->facInitEnv.pbArmed = &pW->bArmedPlacement;
    pW->facInitEnv.pfnPanelPrev = World_FacPanelPrevThunk; /* BaseFacScreen_PanelPrev @ 0x589cc */
    pW->facInitEnv.pfnPanelNext = World_FacPanelNextThunk; /* BaseFacScreen_PanelNext @ 0x589dc */
    pW->facInitEnv.pfnCloseThunk = World_CloseGeoscapeThunk; /* BaseFacScreen_CloseThunkA @ 0x589c0 (0x5A1C4)
                                                               * and BaseFacScreen_CloseThunk @ 0x59b54 (0x5A254):
                                                               * both tail-call Geoscape_CloseUI @ 0x8b94c */
    pW->facInitEnv.pfnLoadPads = pW->pfnLoadPads;
    pW->facInitEnv.pPadUser = pW->pLoadPadsUser;
    pW->facInitEnv.pfnToggle = World_FacToggleThunk; /* BaseFacScreen_Toggle @ 0x589ec */
    pW->facInitEnv.pToggleUser = pW;

    env.pfnTryConfirmClick = World_TryConfirmClick; /* NewBaseScreen_TryConfirm's tail call */
    env.pNav = &pW->nav;
    env.pNavEnv = &pW->navEnv;
    env.pUnitEnv = &unitEnv;
    env.pShipEnv = &shipEnv;
    env.pCreateEnv = &pW->createEnv;

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

    memset(&pW->geo.world, 0, sizeof(pW->geo.world));
    pW->geo.world.apCraft = pW->apCraft;
    pW->geo.world.nShipCount = pW->bCraftCount;
    pW->geo.world.apSiteState = pW->abSiteState;
    pW->geo.world.apRadarPct = pW->abRadarPct;
    pW->geo.world.pTime = &pW->geo.time;

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
    env.pTime = &pW->geo.time;
    env.apTopics = pW->aTopics;
    env.puResearchEntries = &pW->nResearchEntries;
    env.paFacFlags = pW->aFac;
    env.apObjects = pAssets->apObjects;
    env.nObjectCount = pAssets->nObjectCount;
    env.pnGlyphPalBase = &g_nGlyphPalSlotBase; /* the original's one global, Map_nGlyphPalBase */
    env.abFacilityGrid = pW->abFacilityGrid;
    env.pWorld = &pW->geo.world;
    env.pbBaseCount = &pW->nBaseCount;
    env.pnSmapOriginX = &pW->nSmapOriginX;
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
    pW->geo.world.apCraft = pW->apCraft;
    pW->geo.world.nShipCount = pW->bCraftCount;
    pW->geo.world.apSiteState = pW->abSiteState;
    pW->geo.world.apRadarPct = pW->abRadarPct;
    pW->geo.world.pTime = &pW->geo.time;

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
    env.pWorld = &pW->geo.world;
    env.abFacilityGrid = pW->abFacilityGrid;
    env.apResearchCols = pW->auResearchCol;
    env.apManufCols = pW->auManufCol;
    env.puResearchEntries = &pW->nResearchEntries;
    env.puShotObjectWords = (u16 *)pAssets->apObjects[0]; /* 0x45764: the shot object's aItemQty words */
    env.pTimeRec = &pW->saveTime;
    env.pTime = &pW->geo.time;
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

/* ---- the geoscape clock ---------------------------------------------------------------------
 *
 * The shell's half of the geoscape clock. WM_TIMER 0xC353 runs UiPulse_OnTimer @ 0x36630, whose
 * geoscape branch (0x36644) hands straight over to Geoscape_PulseUpdate @ 0x954dc. The pulse advances
 * the clock through Geoscape_AdvanceTime @ 0x5ce40 -- only while nGeoscapeScreen is 0 (0x95618), which
 * is why the clock halts while a geoscape screen is open -- and the clock calls the three tick chains
 * and the monthly funding report. Every callee behind GeoscapeHooks is ported; the thunks below build
 * each callee's env over the shell's state the way World_ProcessEvents does for the event pass.
 *
 * One gap, stated plainly: Geoscape_SpawnMissionSite @ 0x7e49c (4,464 B) is NOT ported, so
 * pfnSpawnMissionSite keeps the same documented stub the spawn pass already uses. TickHourly calls it
 * only for a mission entry that drifted past |1000| on x or y (0x5d1dc..0x5d1f8), where it re-seeds the
 * mission at 150/250 and then builds its site record; without it the entry is re-seeded and its site
 * record is missing. Recorded in PLAN.md.
 */

/* Geoscape_SpawnMissionSite @ 0x7e49c is not ported; see the block comment above. Same documented gap
 * World_SpawnMissionStub records for the spawn pass, in the void form GeoscapeHooks asks for. */
static void World_HookSpawnMissionSiteStub(StrategItem *pUfo, i32 nMissionSub, void *pUser)
{
    (void)pUfo;
    (void)nMissionSub;
    (void)pUser;
}

static void World_HookContactMsg(i32 nMsg, i32 nArg3, i32 nArg4, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    /* UiPanel_AppendLineWithSound @ 0x5a394 on the root message list @ 0x158890. The four ids the
     * clock posts are 15 (a craft out of range, 0x5d658), 13 (intercept lost), 10 (radar contact) and
     * 43 (low supply, 0x460ac). The GameTime_Pause that follows 15 / 13 / 10 is written by the tick
     * itself through geo.world.pTime, not here. */
    UiLineList_AppendWithSound(&pW->msgLines, (u8)nMsg, (u32)nArg3, (u32)nArg4, 0u, 0u,
                               pW->pfnPlaySample, pW->pSoundUser);
}

static i32 World_HookFindBaseBySiteId(i32 nSiteId, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    /* Base_FindIndexBySiteId @ 0x5a8d0 is a copy of Base_FindIndexByTypeByte @ 0x6ac30 and the port
     * keeps the one implementation. */
    return Base_FindIndexByTypeByte(pW->apBases, (i32)pW->nBaseCount, (u8)nSiteId);
}

static i32 World_HookCountEmptyFacilityCells(i32 nBaseIdx, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    return Base_CountEmptyFacilityCells(pW->abFacilityGrid, nBaseIdx);
}

static i32 World_HookClearFacilityGridRow(i32 nBaseIdx, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    return Base_ClearFacilityGridRow(pW->abFacilityGrid, nBaseIdx, pW->abSiteState);
}

static void World_HookProcessEvents(u8 bSpawn, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    /* StrategList_MaybeSpawnCraft_Process @ 0x7e420: the gate is bSpawn == 1 exactly, and the tail is
     * the event pass World_StartNewHook already wired into the spawn context. */
    (void)StrategList_MaybeSpawnCraft_Process(&pW->strategList, pW->spawnCtx.pnSeed,
                                             pW->spawnCtx.nZoomMode, pW->spawnCtx.pTables,
                                             pW->spawnCtx.nDifficulty, pW->spawnCtx.nEarlyGate,
                                             bSpawn, pW->spawnCtx.pfnSpawnMission,
                                             pW->spawnCtx.pfnProcessEvents,
                                             pW->spawnCtx.pUser);
}

static void World_HookTickFacilityConstruction(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    BaseTickConstructionEnv env;

    memset(&env, 0, sizeof(env));
    env.abFacilityGrid = pW->abFacilityGrid;
    env.pbBaseCount = (const u8 *)&pW->nBaseCount; /* 0x159D80, read SIGNED */
    env.apSoldiers = pW->apSoldiers;
    env.pnSoldierCount = &pW->nSoldierCount;
    env.apBases = pW->apBases;
    if (pW->pAssets != NULL) {
        env.apObjects = pW->pAssets->apObjects;
        env.nObjectCount = (i32)pW->pAssets->nObjectCount;
    }
    env.aSites = pW->apSites;
    env.nSiteCount = pW->nSiteCount;
    env.apResearchCols = pW->auResearchCol;
    env.nResearchEntries = (i32)pW->nResearchEntries;
    env.apManufCols = pW->auManufCol;
    env.nManufEntries = PUFO_PRODUCT_ENTRIES_PER_BASE;
    env.pdwShortfall = &pW->nShortfall1870C8;
    env.pLines = &pW->msgLines;
    env.pfnPlaySample = pW->pfnPlaySample;
    env.pSoundUser = pW->pSoundUser;

    Base_TickFacilityConstruction(&env); /* 0x62824 */
}

/* ManufScreen_ClearProductSlot's tables. apProductB @ 0x15B79C (the quantity the player asked for)
 * is not shell state yet -- the manufacturing screen's own set/get buttons are unwired -- and the
 * callee's own head test returns on it, so the clear pass is the documented gap here. */
static void World_BuildManufClearEnv(PufoWorldState *pW, ManufClearEnv *pClear)
{
    memset(pClear, 0, sizeof(*pClear));
    pClear->apProductA = pW->auProductU32;
    pClear->apProductC = pW->auProductU16;
    pClear->apManufCols = pW->auManufCol;
    pClear->abGridBytes = pW->abGridHangarBytes; /* 0x159DBC = abFacilityGrid + 1620, the eight base
                                                  * bytes the three hangar products clear */
    pClear->apBases = pW->apBases;
    pClear->nBaseCount = (i32)pW->nBaseCount;
    pClear->pnSelectedBaseIdx = &pW->nSelectedBase;
}

static void World_HookRefreshBaseTabs(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    ManufRefreshEnv env;
    ManufClearEnv clear;

    World_BuildManufClearEnv(pW, &clear);

    memset(&env, 0, sizeof(env));
    env.apManufCols = pW->auManufCol;
    env.apProductA = pW->auProductU32;
    env.abFacilityGrid = pW->abFacilityGrid;
    env.pbBaseCount = (const u8 *)&pW->nBaseCount; /* 0x159D80, read SIGNED */
    env.apBases = pW->apBases;
    env.aSites = pW->apSites;
    env.nSiteCount = pW->nSiteCount;
    env.pdwShortfall = &pW->nShortfall1870C8;
    env.pLines = &pW->msgLines;
    env.pClearEnv = &clear;
    env.pfnPlaySample = pW->pfnPlaySample;
    env.pSoundUser = pW->pSoundUser;
    if (pW->pAssets != NULL) {
        env.apObjects = pW->pAssets->apObjects;
        env.nObjectCount = (i32)pW->pAssets->nObjectCount;
    }

    ManufScreen_RefreshBaseTabs(&env); /* 0x70ebc */
}

static i32 World_HookRebuildResearchTopics(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    ResearchRebuildEnv env;

    memset(&env, 0, sizeof(env));
    env.aTopics = pW->aTopics;
    env.pbEntryCount = (const u8 *)&pW->nResearchEntries;   /* 0x160C4C, read with LDRB */
    env.apResearchCols = pW->auResearchCol;
    env.abResearchZero = pW->abResearchZero;
    env.paFac = pW->aFac;
    env.pdwShortfall = &pW->nShortfall1870C8;
    env.pLines = &pW->msgLines;
    env.apBases = pW->apBases;
    env.pnSelectedBaseIdx = &pW->nSelectedBase;
    env.pbFinishedTopic = &pW->nFinishedTopic;
    env.pnShownTopic = &pW->nShownTopic;
    env.pfnPlaySample = pW->pfnPlaySample;
    env.pUser = pW->pSoundUser;
    if (pW->pAssets != NULL) {
        env.apObjects = pW->pAssets->apObjects;
        env.pnObjectCount = &pW->pAssets->nObjectCount; /* BootAssets keeps it as a u16, as the image does */
    }

    return ResearchScreen_RebuildTopicList(&env); /* 0x74270, AccumulateAllocated's tail call at 0x749A4 */
}

static void World_HookAccumulateResearch(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    ResearchAccumulateEnv env;

    memset(&env, 0, sizeof(env));
    env.aTopics = pW->aTopics;
    env.apResearchCols = pW->auResearchCol;
    env.puResearchEntries = &pW->nResearchEntries;
    env.pfnRebuild = World_HookRebuildResearchTopics;
    env.pRebuildUser = pW;

    (void)ResearchScreen_AccumulateAllocated(&env); /* 0x74914 */
}

static void World_HookGrantSiteRecovery(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    SmapGrantRecoveryEnv env;
    UnitCreateEnv unitEnv;
    ShipSpawnEnv shipEnv;

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

    memset(&env, 0, sizeof(env));
    env.aSites = pW->apSites;
    env.pnSiteCount = &pW->nSiteCount;
    env.abFacilityGrid = pW->abFacilityGrid;
    env.pbBaseCount = (const u8 *)&pW->nBaseCount; /* 0x159D80, read SIGNED */
    env.apBases = pW->apBases;
    env.apCraft = pW->apCraft;
    env.pnCraftCount = &pW->bCraftCount;
    env.apSoldiers = pW->apSoldiers;
    env.pnSoldierCount = &pW->nSoldierCount;
    env.pLines = &pW->msgLines;
    env.pdwShortfall = &pW->nShortfall1870C8;
    env.pUnitEnv = &unitEnv;
    env.pShipEnv = &shipEnv;
    env.pfnPlaySample = pW->pfnPlaySample;
    env.pSoundUser = pW->pSoundUser;
    if (pW->pAssets != NULL) {
        env.apObjects = pW->pAssets->apObjects;
        env.nObjectCount = (i32)pW->pAssets->nObjectCount;
    }
    /* pszAppDir is the site dump's output directory (0x98E2C reaches it through the dump helper);
     * the shell has no app-directory field on the geoscape path, and the callee tests the pointer. */

    Smap_GrantSiteRecovery(&env); /* 0x98b50 */
}

static void World_HookOpenMonthly(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    /* FundingScreen_OpenMonthly @ 0x5d160: the month counter first, then the recompute, then screen id
     * 19 so Geoscape_CloseUI's case 19 runs on the way out, then bUiAnimStarted cleared. */
    FundingScreen_OpenMonthly(&pW->fundingInit, &pW->screen.nGeoscapeScreen, &pW->bUiAnimStarted);
}

void World_WireGeoscapeClock(PufoWorldState *pW, const WorldHooks *pHooks)
{
    GeoscapeState *pG = &pW->geo;

    /* The state block's cells, each pointed at the shell's ONE instance of the original global it
     * names. geo.world and geo.time are already filled by the bring-up / load paths (they are the
     * shell's GeoscapeWorld and GameTime), and geo.world.pTime is the shared pause byte the ticks
     * write (apLines[106]). */
    pG->pList            = &pW->strategList;                    /* 0x186534 */
    pG->pnNextDisplayId  = &pW->nNextDisplayId;                 /* 0x186544 */
    pG->pnNextSiteId     = &pW->nNextSiteId;                    /* 0x186548 */
    pG->pnDaysSinceStart = &pW->nAlienPoolAddend;               /* word_1472C8, the second name */
    pG->pnSelectedShipIdx = &pW->nSelectedShipIdx;             /* 0x187054 */
    pG->pnLowSupplyFlag  = (i32 *)&pW->nShortfall1870C8;        /* dwActionFail @ 0x1870C8, a dword */
    pG->nSelectedShipAux = -1;
    pG->apBases          = pW->apBases;
    pG->nBaseCount       = (i32)pW->nBaseCount;
    pG->apEmptyFacilityCells = pW->abRadarPct;                  /* 0x162008: the empty-cell counts ARE
                                                                 * the radar percentages, one block */
    pG->apSoldiers       = pW->apSoldiers;
    pG->pnSoldierCount   = &pW->nSoldierCount;
    pG->pSupplyItem      = (pW->pAssets != NULL)
                               ? pW->pAssets->apObjects[105]    /* Elerium-115, the supply item */
                               : NULL;

    pW->geoHooks.pfnProcessEvents           = World_HookProcessEvents;
    pW->geoHooks.pfnContactMsg              = World_HookContactMsg;
    pW->geoHooks.pfnSpawnMissionSite        = World_HookSpawnMissionSiteStub; /* 0x7e49c, NOT ported */
    pW->geoHooks.pfnGrantSiteRecovery       = World_HookGrantSiteRecovery;
    pW->geoHooks.pfnRefreshBaseTabs         = World_HookRefreshBaseTabs;
    pW->geoHooks.pfnAccumulateResearch      = World_HookAccumulateResearch;
    pW->geoHooks.pfnTickFacilityConstruction = World_HookTickFacilityConstruction;
    pW->geoHooks.pfnClearFacilityGridRow    = World_HookClearFacilityGridRow;
    pW->geoHooks.pfnCountEmptyFacilityCells = World_HookCountEmptyFacilityCells;
    pW->geoHooks.pfnOpenMonthly             = World_HookOpenMonthly;
    pW->geoHooks.pfnFindBaseBySiteId        = World_HookFindBaseBySiteId;
    pW->geoHooks.pUser                      = pW;

    pW->geoBundle.pState = &pW->geo;
    pW->geoBundle.pHooks = &pW->geoHooks;
    Geoscape_BuildClockHooks(&pW->geoBundle, &pW->geoClock);

    pW->pHitCtx    = (pHooks != NULL) ? pHooks->pHit : NULL;
    pW->pnAnimPhase = (pHooks != NULL) ? pHooks->pnAnimPhase : NULL;

    /* Geoscape_CloseUI @ 0x8b94c's env, on the same instances. The head, the five needed-flag passes and
     * the screens the shell can reach (1, 2, 4, 19) are wired. What is NOT: the base-info, hangar,
     * research, manufacturing, transfer, buy/sell and ufopaedia blocks (their cases keep NULL gates, which
     * the dispatcher tests), the three Map bytes the bankruptcy path writes (Map_bTickHold @ 0x113A12,
     * Map_bBattleEnded @ 0x113A14, Map_bMissionMode @ 0x128AD5 -- the tactical Map is not the shell's),
     * the EndScreen_Init env of that same path, and the byte_158834 / byte_1870F4 base-index pair case 19
     * writes on its psi branch. */
    GeoscapeCloseEnv *pC = &pW->closeEnv;

    memset(pC, 0, sizeof(*pC));
    pC->pfnPlaySample    = pW->pfnPlaySample;
    pC->pSoundUser       = pW->pSoundUser;
    pC->pnActionFail     = (i32 *)&pW->nShortfall1870C8;   /* dwActionFail @ 0x1870C8, cleared as a dword */
    pC->pbPopupA         = &pW->bRoot34;                   /* bPopupVisibleA @ 0x18652C */
    pC->pbPopupB         = &pW->bRoot35;                   /* bPopupVisibleB @ 0x18652D */
    pC->pWorld           = &pW->geo.world;                 /* nGeoscapeTickSeconds @ 0x187070 */
    pC->apSpeedCtl       = pW->ui.apRootCtl + 2;           /* apGeoscapeSpeedCtl @ 0x186500..0x186514 */

    pC->abFacilityGrid   = pW->abFacilityGrid;             /* 0x159768 */
    pC->pnBaseCount      = &pW->nBaseCount;                /* 0x159D80, read SIGNED */
    pC->pnSelectedBaseIdx = &pW->nSelectedBase;            /* 0x159D81, read SIGNED */
    pC->apCraft          = pW->apCraft;
    pC->pnCraftCount     = &pW->bCraftCount;
    pC->apSoldiers       = pW->apSoldiers;
    pC->nSoldierCount    = pW->nSoldierCount;
    pC->apObjects        = (pW->pAssets != NULL) ? pW->pAssets->apObjects : NULL; /* Map + 0x108 */
    pC->nObjectCount     = (pW->pAssets != NULL) ? (i32)pW->pAssets->nObjectCount : 0;
    pC->apSites          = pW->apSites;                    /* 0x162030 */
    pC->nSiteCount       = pW->nSiteCount;
    pC->apBases          = pW->apBases;
    pC->apManufCol       = pW->auManufCol;                 /* 0x15E19C */
    pC->apResearchCol    = pW->auResearchCol;              /* auResearchCol @ 0x16022C */
    pC->nResearchEntries = (i32)pW->nResearchEntries;

    pC->pnScreenId       = &pW->screen.nGeoscapeScreen;    /* nGeoscapeScreen @ 0x186BC4 */
    pC->pView            = &pW->view;                      /* gSmapCam @ 0x1863E8 */
    pC->pStripPad        = pW->ui.apPad[GEOUI_PAD_186BBC]; /* the strip height Smap_ClampScrollY reads */
    pC->pNav             = &pW->nav;                       /* case 1 frees these fifteen controls */
    pC->pbPlaceActive    = &pW->bPlaceActive;              /* 0x1870BC */
    pC->pbPlaceFail      = &pW->bPlaceFail;                /* 0x1870B8 */
    pC->pbBaseFacConfirm = &pW->bPlacementPending;         /* 0x1870AC */
    pC->pbBaseFacDone    = &pW->bFacDone;                  /* 0x187078 */
    pC->pbBaseFacArmed   = &pW->bArmedPlacement;           /* 0x158888 */
    pC->pbBaseFacDialog  = &pW->bFacDialogOpen;            /* 0x158884 */
    pC->pbDragActive     = &pW->frame.g.bDragActive;       /* 0x187074 */
    pC->pbSoldierOpen    = &pW->bSolScreenOpen;            /* 0x159DE0 */
    pC->pbTransferFlag   = &pW->bTransferFlag;             /* 0x159E10 */
    pC->pnFunds          = &pW->nFunds;
    pC->anFundBar        = pW->economy.anFundBar;          /* 0x186594..0x1865A4 */
    pC->ppFundPad        = &pW->pFundPad;                  /* 0x1595DC */
    pC->pFacToggle       = &pW->facToggleEnv;
    pC->pSoldierScreen   = &pW->solEnv;
    pC->pOptionsHooks    = &pW->optionsHooks;
    /* pfnFreeGeoscapeUi stays NULL on purpose. The only path that calls it is the bankruptcy branch
     * (0x8BBF0 then EndScreen_Init @ 0x8BBF8), and GeoscapeUi_FreeAll's FreeWorld releases the soldiers,
     * bases, craft and strateg items the shell's own World_Free also releases -- the original does not
     * have a second owner of them. Wiring it here would free the rosters twice. */
    pC->pUser            = pW;
}

void World_PulseGeoscape(PufoWorldState *pW)
{
    GeoscapePulseEnv env;

    /* Screen 7's auto-repeat pair: the two build-dialog scroll controls at 0x158874 / 0x158878, and
     * only while bBaseFacPlaceArmed @ 0x158888 is CLEAR (0x95770 ANDS / BEQ). The other per-screen
     * groups belong to the soldier, hangar, research, manufacturing, transfer, buy/sell, ufopaedia,
     * start and options screens, whose controls the shell does not build yet -- see the group table in
     * game/pufo_geoscape.h. */
    UiControl *apFacCtl[2] = { &pW->ctlFacUp, &pW->ctlFacDown };
    GeoscapePulseCtlGroup aGroups[1];
    GeoscapePulseScreen   aScreens[1];

    aGroups[0].apCtl = apFacCtl;
    aGroups[0].nCount = 2;
    aGroups[0].pbGate = NULL;
    aGroups[0].pbNotGate = &pW->bArmedPlacement;
    aScreens[0].nScreenId = 7;
    aScreens[0].pGroups = aGroups;
    aScreens[0].nGroupCount = 1;

    memset(&env, 0, sizeof(env));
    env.pnActionFail  = (i32 *)&pW->nShortfall1870C8;   /* 0x954EC: == 1 stops the whole tick */
    env.nScreenId     = pW->screen.nGeoscapeScreen;
    env.pView         = &pW->view;
    env.pabDragFlags  = pW->abScrollFlags;              /* 0x1864F4..0x1864F7 */
    env.apCraft       = pW->apCraft;
    env.pnCraftCount  = &pW->bCraftCount;
    env.pStrategList  = &pW->strategList;
    env.pTime         = &pW->geo.time;
    env.pClockHooks   = &pW->geoClock;
    env.nTickSeconds  = pW->geo.world.nTickSeconds;     /* nGeoscapeTickSeconds @ 0x187070 */
    env.pnAnimPhase   = pW->pnAnimPhase;                /* nUiSlot0AnimPhase @ 0x113A18, Map + 0x60 */
    env.pnCooldownR   = &pW->frame.g.nTapCooldown;      /* nRadarCooldownR @ 0xAC218 */
    env.pnCooldownL   = &pW->nRadarCooldownL;           /* 0xAC21C */
    env.pnMsgTimer    = &pW->nRadarMsgTimer;            /* 0xAC220 */
    env.pnPulseTimer  = &pW->nRadarPulseTimer;          /* 0xAC224 */
    env.pScreens      = aScreens;
    env.nScreenCount  = 1;
    env.pHitCtx       = pW->pHitCtx;
    if (pW->ui.apPad[GEOUI_PAD_186BBC] != NULL) {
        env.nStripHeight = (i32)pW->ui.apPad[GEOUI_PAD_186BBC]->uHeight; /* Smap_ScrollBy's clamp */
    }

    Geoscape_PulseUpdate(&env);
}

/* Geoscape_CloseUI @ 0x8b94c over the env World_WireGeoscapeClock built. The screen id it leaves behind is
 * what the next frame draws and what the pulse's clock gate reads: 2 -> 1 (0x8BE68), 1 -> 0 (0x8BE94), and
 * the clock only runs at 0. */
void World_CloseGeoscape(PufoWorldState *pW)
{
    Geoscape_CloseUI(&pW->closeEnv);
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
 *   - The screen ids the shell can now reach are 0, 1, 2 (the map and the placement screen), 4 and 5
 *     (nav slot 2 and SoldierScreen_SelectListed 0x64a9c), 7 (nav slot 4, and the map click's own
 *     0x8B64C), 15 and 19 (the funding screen the monthly close sets). Seven nav slots stay
 *     World_NoopClick -- BaseInfoScreen_Open (3), BasePlaneScreen_Open (6), ResearchScreen_Open (8),
 *     ManufScreen_Open (9), TransferScreen_Open (10) and BuySellScreen_OpenBuy/Sell (11, 12) -- because
 *     their Init envs and their arms (pBaseInfoArm, pPlaneArm, pResearchDraw, pManufPad, pTransferPad,
 *     pBuySellPad) are not built here, and Geoscape_Draw dereferences several unwired arm envs with no
 *     test of their own.
 *   - GeoscapePointerEnv's per-screen fields for those same screens (the strip, transfer, soldier-name,
 *     craft, research, manufacturing, site-info, buy/sell, screen-16, options and base-at blocks) stay
 *     NULL. The dispatcher reaches them only from those screen ids. One shell-owned block that could not
 *     be expressed before -- apFacCtl, the two build-dialog controls the shell holds as two embedded
 *     UiControls -- is now the arm's own array, pW->apFacPanelCtl. apRadarCtl wants 0x159684's control
 *     set, which the shell does not build.
 */
void World_WireGeoscapeFrame(PufoWorldState *pW, const WorldHooks *pHooks)
{
    GeoscapeFrameState *pF = &pW->frame;
    GeoscapeUiState    *pUi = &pW->ui;
    BootAssets         *pAssets = pW->pAssets;
    GeoscapePointerEnv *pPtr = &pW->ptrEnv;
    const UiControlHitCtx *pHit = (pHooks != NULL) ? pHooks->pHit : NULL;
    GfxColorEnv        *pColor = (pHooks != NULL) ? pHooks->pColor : NULL;
    const char         *pszText = (pAssets != NULL) ? (const char *)pAssets->pTextRecs : NULL; /* aTextRecs @ 0xB38D0 */

    memset(pF, 0, sizeof(*pF));
    memset(pPtr, 0, sizeof(*pPtr));

    /* Set here, not in World_WireGeoscapeClock: the placement arm below reads it, and that clock wiring
     * only runs at the end of this function. */
    pW->pnMapPhase = (pHooks != NULL) ? pHooks->pnMapPhase : NULL; /* nMapPhase @ 0x1139EF (Map + 0x37) */

    /* The 0x1864F8 anim root's three bytes: 0x187080, 0x187058 and the frame style at 0x186566, which
     * the shell keeps as the tenth byte of its region-counter run. */
    pF->anim.pbStarted   = &pW->bUiAnimStarted;
    pF->anim.pnProgress  = &pW->cUiAnimProgress;
    pF->anim.nFrameStyle = (i8)pW->aRegionBuildCount[9];

    /* BaseFac_BuildStatusMsg @ 0x6a4bc, the panel head's call at 0x8C82C. */
    pF->fac.pTime          = &pW->geo.time;
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
    pF->map.pTime      = &pW->geo.time;
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
    pF->panel.pTime           = &pW->geo.time;                           /* 0x159618: GameTime_Pause's byte */
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

    /* The placement screen, Geoscape_Draw case 1 (0x91DC8..0x92ADC). Geoscape_CloseUI case 2 puts the id
     * here (0x8BE68) and case 1 takes it away again (0x8BE94), so this is what a new game draws once its
     * close control is clicked. The two grid callees get their own envs because the arm builds them by
     * value: 0x922AC hands the grid block to BaseFacScreen_DrawNameGrid with the base index from
     * LDRSB R1,[R5,#(nSelectedBaseIdx-0x159768] at 0x922B0, and 0x922B8 MOV R1,#0xFFFFFFFF makes the
     * strip's highlight row this arm's argument -- -1, which marks no row. */
    pF->placement.pPad           = pUi->apPad[GEOUI_PAD_186BA0];   /* pBaseFacScreenPad @ 0x186BA0, 0x91DCC */
    pF->placement.apNavCtl       = pW->nav.apNav;                  /* 0x159888..0x1598AC, grid + 0x120 */
    pF->placement.pCloseCtl      = pUi->apCtl[GEOUI_CTL_186B00];   /* *g_pUiCloseCtl @ 0x186B00, moved to (194,4) */
    pF->placement.pMapCtl        = pUi->apCtl[GEOUI_CTL_186BB4];   /* *pGeoscapeCtlA @ 0x186BB4, the demolish key */
    pF->placement.pbPlaceActive  = &pW->bPlaceActive;              /* bBasePlaceActive @ 0x1870BC: gate and the
                                                                     * byte 0x92844 clears on an empty cell */
    pF->placement.pbPlaceFail    = &pW->bPlaceFail;                /* bBasePlaceFail @ 0x1870B8 */
    pF->placement.pnTapX         = &pW->nSmapTapX;                 /* 0x186B7C: the cursor block writes back */
    pF->placement.pnTapY         = &pW->nSmapTapY;                 /* 0x186B80 */
    pF->placement.pnSmapOriginX  = &pW->nSmapOriginX;              /* 0x159DC4 */
    pF->placement.pnSmapOriginY  = &pW->nSmapOriginY;              /* 0x159DC8 */
    pF->placement.abFacilityGrid = pW->abFacilityGrid;             /* gBaseFacGrid @ 0x159768 */
    pF->placement.pnSelBaseIdx   = &pW->nSelectedBase;             /* 0x159D81, read signed in every call here */
    pF->placement.pnMapPhase     = pW->pnMapPhase;                 /* nMapPhase @ 0x1139EF: the cursor colour */
    pF->placement.pnFunds        = &pW->nFunds;                    /* 0x923C4 */
    pF->placement.apSiteRecs     = pW->aSiteRecs;                  /* 0x186C40, 64-byte records, name at +26 */
    pF->placement.paFacFlags     = pW->aFac;                       /* aBaseFacFlags @ 0x186E48, 28-byte records */
    pF->placement.pColor         = pColor;
    pF->placement.pAnim          = &pF->anim;                      /* the demolish panel's frame state, 0x92898 */
    pF->placement.pfnCloseClick  = World_CloseGeoscapeThunk;       /* Geoscape_CloseUI_ThunkA @ 0x838d0 */
    pF->placement.pfnMapClick    = World_TryConfirmClick;          /* Geoscape_OnMapClick_Thunk @ 0x857e8 */

    pF->facNameGrid.abFacilityGrid = pW->abFacilityGrid;
    pF->facNameGrid.apCraft        = pW->apCraft;                  /* apCraft @ 0x114A64 */
    pF->facNameGrid.pnCraftCount   = &pW->bCraftCount;             /* Map_nCraftCount @ 0x114B4C, as a byte */
    pF->facNameGrid.pnOriginX      = &pW->nSmapOriginX;
    pF->facNameGrid.pnOriginY      = &pW->nSmapOriginY;
    pF->facNameGrid.pColor         = pColor;
    pF->facNameGrid.nBaseIdx       = pW->nSelectedBase;            /* by value: re-read every frame */

    pF->facStrip.abFacilityGrid = pW->abFacilityGrid;
    pF->facStrip.pbBaseCount    = (const u8 *)&pW->nBaseCount;     /* 0x159D80 = grid + 0x618, read signed */
    pF->facStrip.pbSelectedBase = (const u8 *)&pW->nSelectedBase;  /* 0x159D81 = grid + 0x619 */
    pF->facStrip.nHighlightRow  = PUFO_PLA_STRIP_MARK;             /* the arm overwrites the copy at 0x922B8 */

    pF->draw.pPlacementArm   = &pF->placement;
    pF->draw.pPlacementCtl   = &pF->placementCtl;
    pF->draw.pPlacementGrid  = &pF->facNameGrid;
    pF->draw.pPlacementStrip = &pF->facStrip;

    /* Case 4 (0x92DAC) and case 5 (0x92EE4), the soldier screens. Both end in SoldierScreen_Draw @ 0x65290,
     * which draws nothing while 0x159DE0 is clear (0x652CC) -- the byte nav slot 2's handler sets. */
    pF->solDraw.pnScreenId   = &pW->screen.nGeoscapeScreen;
    pF->solDraw.pbOpen       = &pW->bSolScreenOpen;              /* 0x159DE0 */
    pF->solDraw.pnCursor     = &pW->nSoldierCursor;              /* 0x159E08, the find buttons move it */
    pF->solDraw.pPad         = pW->pSolPad;                      /* unk_1598D4, the "soldier" pad */
    pF->solDraw.pPanelNames  = pW->pSolPanelNames;               /* 0x15975C */
    pF->solDraw.pPanelCraft  = pW->pSolPanelCraft;               /* 0x159DEC */
    pF->solDraw.pfnTextInput = NULL;                             /* the case-5 readout: SoldierScreen_DrawDetail
                                                                  * @ 0x632cc needs the keyboard controls and the
                                                                  * pad block that screen owns, which the shell
                                                                  * does not build, so 0x652A4 returns 0 */

    pF->solArm.pColor         = pColor;
    pF->solArm.pCloseCtl      = pUi->apCtl[GEOUI_CTL_186B00];    /* moved to (224,1) at 0x92DB0/0x92DC8 */
    pF->solArm.pbTransferFlag = &pW->bTransferFlag;              /* 0x159E10: case 5's order gate */
    pF->solArm.pPad           = pW->pSolPad;                     /* the arm's own 0x1598D4 read, case 5 */

    pF->solPanelCtl.apCtl = pW->nav.apPanel;                     /* apSoldierPanelCtl @ 0x1598B0/B4, case 4 */
    pF->solFindCtl.apCtl  = pW->nav.apFind;                      /* apSoldierFindCtl @ 0x1598B8..C0, case 5 */

    pF->draw.pSoldierDraw     = &pF->solDraw;
    pF->draw.pSoldierArm      = &pF->solArm;
    pF->draw.pSoldierPanelCtl = &pF->solPanelCtl;
    pF->draw.pSoldierFindCtl  = &pF->solFindCtl;

    /* Case 7 (0x92AF0), the build screen. It calls the same two grid callees as case 1 -- with the same -1
     * strip mark (0x92D38) -- then the build dialog, and it blits its two block controls ONLY while
     * bBaseFacPlaceArmed @ 0x158888 is clear (0x92D64). */
    pW->apFacPanelCtl[0] = &pW->ctlFacUp;                        /* the controls InitUI's callees allocate into
                                                                  * 0x158874 and 0x158878 */
    pW->apFacPanelCtl[1] = &pW->ctlFacDown;
    pF->baseFac.pPad         = pUi->apPad[GEOUI_PAD_186BA0];     /* 0x92AF0, the pad case 1 shares */
    pF->baseFac.apPanelCtl   = pW->apFacPanelCtl;
    pF->baseFac.pbPlaceArmed = &pW->bArmedPlacement;

    pF->facDialog.pColor         = pColor;
    pF->facDialog.pAnim          = &pF->anim;                    /* the panel frame, 0x59C60 */
    pF->facDialog.pPanel         = pW->pFacPanel;                /* 0x15886C */
    pF->facDialog.nPanelSel      = pW->nPanelSel;                /* by value: re-read every frame */
    pF->facDialog.paFac          = pW->aFac;                     /* 0x186E48 */
    pF->facDialog.pnFunds        = &pW->nFunds;
    pF->facDialog.pCtl           = pUi->apCtl[GEOUI_CTL_186B00];
    pF->facDialog.pbDialogOpen   = &pW->bFacDialogOpen;          /* 0x158884: a closed dialog draws nothing */
    pF->facDialog.pbPlaceArmed   = &pW->bArmedPlacement;         /* 0x158888 */
    pF->facDialog.pbOverlayShown = &pW->bFacOverlayShown;        /* 0x15888C, written at 0x59FAC */
    pF->facDialog.pdwShortfall   = &pW->nShortfall1870C8;        /* read as a dword at 0x1870C8 */
    pF->facDialog.pnResidue      = &pW->nResidue1870B4;
    pF->facDialog.pOverlayEnv    = &pF->facOverlay;               /* the armed branch calls the overlay at 0x59EF8 */

    /* The placement overlay, BaseFacScreen_DrawPlacementOverlay @ 0x58e10: the aimed preview on the build
     * screen and the commit that spends the money. Its kind comes out of the dialog panel's own row aux
     * (0x58E20), the same panel the dialog draws, so the panel pointer is refreshed with the dialog's. */
    pF->facOverlay.abFacilityGrid   = pW->abFacilityGrid;              /* gBaseFacGrid @ 0x159768 */
    pF->facOverlay.pPanel           = pW->pFacPanel;                   /* 0x15886C */
    pF->facOverlay.nPanelSel        = pW->nPanelSel;                   /* 0x158868, by value: 0x58E20 */
    pF->facOverlay.paFac            = pW->aFac;                        /* 0x186E48: bFlag21 the 2x2 marker,
                                                                        * uCost the commit's price */
    pF->facOverlay.pnFunds          = &pW->nFunds;                     /* 0x1598D8 */
    pF->facOverlay.nSelectedBaseIdx = pW->nSelectedBase;               /* 0x159D81, read signed */
    pF->facOverlay.pnTapX           = &pW->nSmapTapX;                  /* 0x186B7C: the overlay writes the tap
                                                                        * it resolved back here (0x58E58/0x58EC0) */
    pF->facOverlay.pnTapY           = &pW->nSmapTapY;                  /* 0x186B80 */
    pF->facOverlay.nPendingX        = pW->nUnk15887C;                  /* qword_15887C */
    pF->facOverlay.nPendingY        = pW->nUnk158880;                  /* qword_15887C + 4 */
    pF->facOverlay.nOriginX         = pW->nSmapOriginX;                /* 0x159DC4 */
    pF->facOverlay.nOriginY         = pW->nSmapOriginY;                /* 0x159DC8 */
    pF->facOverlay.nCursorX         = (i16)pF->g.nCursorX;             /* nSmapCursorX @ 0x186BD0, a halfword */
    pF->facOverlay.nCursorY         = (i16)pF->g.nCursorY;             /* 0x186BD4 */
    pF->facOverlay.pbPreviewActive  = &pW->bRoot34;                    /* unk_18652C: the aimed state */
    pF->facOverlay.pbPreviewOk      = &pW->bRoot35;                    /* unk_18652D, cleared on a zero score */
    pF->facOverlay.pbOverlayShown   = &pW->bFacOverlayShown;           /* 0x15888C, set at 0x59234 */
    pF->facOverlay.pbNewBaseMode    = &pW->bFacDone;                   /* byte_187074[4] @ 0x187078 */
    pF->facOverlay.nPendingRegionId = pW->bPendingRegionId;            /* byte_159DF4 */
    pF->facOverlay.paNewBaseCost    = g_anNewBaseCostByRegion;         /* aNewBaseCostByRegion @ 0xAB488 */
    pF->facOverlay.pdwShortfall     = &pW->nShortfall1870C8;           /* the unaffordable-purchase dword */
    pF->facOverlay.pnResidue        = &pW->nResidue1870B4;
    pF->facOverlay.pnScreenId       = &pW->screen.nGeoscapeScreen;     /* the commit writes 1 (0x59B0C) */
    pF->facOverlay.pszLabel         = pW->szLabelScratch;              /* szRadarMsg @ 0x186B10: the label the
                                                                        * commit formats and the new base is named */
    pF->facOverlay.cchLabel         = sizeof(pW->szLabelScratch);
    pF->facOverlay.paRegionBuildCount = (i8 *)pW->aRegionBuildCount;   /* g_anRegionBuildCount @ 0x18655D, read
                                                                        * signed by BaseFac_FormatCostLabel */
    pF->facOverlay.pCreateEnv       = &pW->createEnv;                  /* Base_CreateNamedEntry's env, 0x59AFC */
    pF->facOverlay.pToggleEnv       = &pW->facToggleEnv;               /* the closing BaseFacScreen_Toggle, 0x59B28 */
    pF->facOverlay.nPlaceX          = pW->nPlaceX;                     /* 0x159D94: the new base's coordinates */
    pF->facOverlay.nPlaceY          = pW->nPlaceY;                     /* 0x159D98 */
    pF->facOverlay.nPhaseByte       = (pW->pnMapPhase != NULL) ? (u8)*pW->pnMapPhase : 0; /* Map + 0x37 */
    if (pAssets != NULL) {
        pF->facOverlay.pTextRecs = pAssets->pTextRecs;                 /* the new-base label's TEXTS rows */
    }

    pF->draw.pBaseFacArm    = &pF->baseFac;
    pF->draw.pBaseFacGrid   = &pF->facNameGrid;
    pF->draw.pBaseFacStrip  = &pF->facStrip;
    pF->draw.pBaseFacDialog = &pF->facDialog;

    /* The two nav openers the shell can serve, held on the state because a click runs them. */
    pW->newBaseOpen.pfnPlaySample  = World_PlaySampleWithArg;         /* Sound_Play(17, 4) at 0x615dc */
    pW->newBaseOpen.pCtlFrame      = pUi->apCtl[GEOUI_CTL_186B00];    /* to (224,1) at 0x61608/0x61614 */
    pW->newBaseOpen.pCtlConfirm    = pUi->apCtl[GEOUI_CTL_186BA4];    /* 0x6162C installs the confirm handler */
    pW->newBaseOpen.pView          = &pW->view;                       /* the two Smap_ZoomOut at 0x61634/0x6163C */
    pW->newBaseOpen.pZoomSets      = pW->sets;
    pW->newBaseOpen.pWorldOut      = pUi->pWorldSize;
    pW->newBaseOpen.pbBaseCount    = &pW->nBaseCount;                 /* the != 8 gate at 0x615F4 */
    pW->newBaseOpen.pbFlag187080   = &pW->bUiAnimStarted;             /* cleared at 0x6164C */
    pW->newBaseOpen.pfnTryConfirm  = World_TryConfirmClick;
    pW->newBaseOpen.pfnCloseThunk  = World_NewBaseCloseThunk;         /* 0x61654 */
    pW->newBaseOpen.pUser          = pW;                              /* the sample adapter's world pointer */

    pW->facOpenHooks.pfnPlaySample = World_PlaySampleWithArg;         /* Sound_Play(17, 4) at 0x61850 */
    pW->facOpenHooks.pfnInit       = World_BaseFacInitThunk;          /* the tail call to 0x5a198 */
    pW->facOpenHooks.pSlot         = NULL;
    pW->facOpenHooks.pUser         = pW;

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
        pF->placementCtl.pfnCtlBlit   = pHit->pfnBlit;
        pF->placementCtl.pfnCtlBlitEx = pHit->pfnBlitEx;
        pF->placementCtl.pCtlBlitUser = pHit->pUser;
        pF->solPanelCtl.pfnCtlBlit    = pHit->pfnBlit;
        pF->solPanelCtl.pfnCtlBlitEx  = pHit->pfnBlitEx;
        pF->solPanelCtl.pCtlBlitUser  = pHit->pUser;
        pF->solFindCtl.pfnCtlBlit     = pHit->pfnBlit;
        pF->solFindCtl.pfnCtlBlitEx   = pHit->pfnBlitEx;
        pF->solFindCtl.pCtlBlitUser   = pHit->pUser;
        pF->baseFac.pfnCtlBlit        = pHit->pfnBlit;
        pF->baseFac.pfnCtlBlitEx      = pHit->pfnBlitEx;
        pF->baseFac.pCtlBlitUser      = pHit->pUser;
        pF->facDialog.pfnCtlBlit      = pHit->pfnBlit;
        pF->facDialog.pfnCtlBlitEx    = pHit->pfnBlitEx;
        pF->facDialog.pCtlUser        = pHit->pUser;
    }
    if (pAssets != NULL) {
        pF->panel.pTextRecs = pszText;
        pF->hover.pTextRecs = pszText;
        pF->placement.pTextRecs = pszText; /* the ten captions, 0x92070..0x92290 */
        pF->solArm.pTextRecs    = pszText; /* the soldier captions, 0x92E40..0x92EB8 */

        /* The build dialog's eight labels are absolute TEXTS addresses in the original; the record each
         * one is was read off the IDB name at that address (aTextRec156 = 0xB38D0 + 205*156 = 0xBB5BC, ...). */
        pF->facDialog.pszFundsPrefix = pszText + PUFO_TEXT_REC_STRIDE * 156;  /* unk_BB5BC, "Деньги: " */
        pF->facDialog.pszRowCost     = pszText + PUFO_TEXT_REC_STRIDE * 83;   /* unk_B7B47 */
        pF->facDialog.pszRowBuild    = pszText + PUFO_TEXT_REC_STRIDE * 84;   /* unk_B7C14 */
        pF->facDialog.pszRowNewBase  = pszText + PUFO_TEXT_REC_STRIDE * 85;   /* unk_B7CE1 */
        pF->facDialog.pszDaysSuffix  = pszText + PUFO_TEXT_REC_STRIDE * 154;  /* aFacDaysSuffix @ 0xBB422:
                                                                               * 0x59E30 adds 0x7B52 = 205*154,
                                                                               * the record start itself */
        pF->facDialog.pszTitleArmed  = pszText + PUFO_TEXT_REC_STRIDE * 86;   /* unk_B7DAE */
        pF->facDialog.pszTitleList   = pszText + PUFO_TEXT_REC_STRIDE * 1148; /* unk_ED01C */
        pF->facDialog.pszShortfall   = pszText + PUFO_TEXT_REC_STRIDE * 406;  /* unk_C7DEE */
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
    pPtr->apFacCtl       = pW->apFacPanelCtl;            /* 0x158874/0x158878, hit-tested at 0x87F88 and 0x89E40 */
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

    /* The funding screen: case 15 (0x94304) draws FundingScreen_Draw and case 19 (0x94314) draws the
     * monthly arm. Case 19 is the screen FundingScreen_OpenMonthly puts the geoscape into, so the
     * shell needs both envs the moment the clock can run a month -- Geoscape_DrawFundingArm
     * dereferences its arm env with no test at 0x94334. The three funding panels (pFundPanelBars
     * 0x1595E0, pFundPanelNames 0x1595E4, pFundPanelArrows 0x1595E8) belong to FundingScreen_Toggle,
     * which nothing here runs, so they stay NULL and FundingScreen_Draw's panel rows are skipped. */
    pW->fundingInit.ppPadSlot      = &pW->pFundPad;                    /* unk_1595DC */
    pW->fundingInit.pCloseCtl      = pUi->apCtl[GEOUI_CTL_186B00];     /* *g_pUiCloseCtl @ 0x186B00 */
    pW->fundingInit.pfnCloseClick  = World_CloseGeoscapeThunk;         /* FundingScreen_ExitThunk @ 0x5c078,
                                                                        * installed at 0x5C0F8 */
    pW->fundingInit.anCountryScore = pW->economy.anFundingBlock;       /* 0x186568..0x18657C, 6 slots */
    pW->fundingInit.anShare        = pW->economy.anFundingBlock + PUFO_ECON_RACE_SHARE_FIRST; /* 0x186580 */
    pW->fundingInit.anBaseline     = pW->anFundBaseline;               /* 0x1595F0 */
    pW->fundingInit.anBarTotal     = pW->economy.anFundBar;            /* 0x186594 */
    pW->fundingInit.anArrow        = pW->economy.aZero44_49;           /* 0x1865A8, slots 0..4 */
    pW->fundingInit.pnPendingTotal = &pW->economy.aZero44_49[5];       /* g_nFundPending @ 0x1865BC */
    pW->fundingInit.pnMonthCounter = &pW->nMonthCounter;               /* unk_159648 */
    pW->fundingInit.apBases        = (const BaseRec *const *)pW->apBases;
    pW->fundingInit.nBaseCount     = pW->nBaseCount;

    pW->fundingDraw.pbOpen        = &pW->bFundScreenOpen;              /* 0x1595EC */
    pW->fundingDraw.pnTotal       = &pW->nFundDisplayTotal;            /* 0x159604 */
    pW->fundingDraw.pCloseCtl     = pUi->apCtl[GEOUI_CTL_186B00];
    pW->fundingDraw.pColor        = pColor;
    pW->fundingDraw.anShare       = pW->economy.anFundingBlock + PUFO_ECON_RACE_SHARE_FIRST;
    pW->fundingDraw.anBaseline    = pW->anFundBaseline;
    pW->fundingDraw.anBar         = pW->economy.anFundBar;
    if (pAssets != NULL) {
        pW->fundingDraw.pTextRecs = pAssets->pTextRecs;
    }
    if (pHit != NULL) {
        pW->fundingDraw.pfnCtlBlit   = pHit->pfnBlit;
        pW->fundingDraw.pfnCtlBlitEx = pHit->pfnBlitEx;
        pW->fundingDraw.pCtlUser     = pHit->pUser;
    }

    pW->fundingArm.pbBaseCount    = (const u8 *)&pW->nBaseCount;                   /* 0x94330 LDRSB */
    pW->fundingArm.pnFunds        = &pW->nFunds;                       /* 0x94334 */
    pW->fundingArm.anFundBar      = pW->economy.anFundBar;             /* 0x9437C..0x943A0, summed in that order */
    pW->fundingArm.abFacilityGrid = pW->abFacilityGrid;
    pW->fundingArm.apCraft        = pW->apCraft;
    pW->fundingArm.nCraftCount    = (i32)pW->bCraftCount;
    pW->fundingArm.apSoldiers     = pW->apSoldiers;
    pW->fundingArm.nSoldierCount  = pW->nSoldierCount;
    pW->fundingArm.apSites        = pW->apSites;
    pW->fundingArm.nSiteCount     = pW->nSiteCount;
    pW->fundingArm.pAnim          = &pF->anim;                         /* 0x943B0, the panel frame */
    pW->fundingArm.pColor         = pColor;
    if (pAssets != NULL) {
        pW->fundingArm.apObjects = pAssets->apObjects;
        pW->fundingArm.pTextRecs = (const char *)pAssets->pTextRecs;
    }

    pF->draw.pFundEnv    = &pW->fundingDraw;
    pF->draw.pFundingArm = &pW->fundingArm;

    World_WireGeoscapeClock(pW, pHooks);
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
    /* BaseFacScreen_DrawNameGrid takes the base index by value, and the original reads it from the grid
     * block at every call (0x922B0 LDRSB R1,[R5,#(nSelectedBaseIdx-0x159768)]), so the copy follows the
     * selection each frame. The strip's highlight row is NOT refreshed: 0x922B8 makes it -1 for this arm. */
    pF->facNameGrid.nBaseIdx = pW->nSelectedBase;

    /* The two screens whose objects their own open handler allocates: SoldierScreen_Toggle @ 0x64af0 builds
     * the "soldier" pad (0x1598D4) and the four panels, BaseFacScreen_Toggle @ 0x589ec builds the dialog panel
     * (0x15886C). The original re-reads those globals per draw (0x92DD0, 0x92D44, 0x59B6C), so the env copies
     * are re-read here with the frame. */
    pF->solDraw.pPad        = pW->pSolPad;
    pF->solDraw.pPanelNames = pW->pSolPanelNames;
    pF->solDraw.pPanelCraft = pW->pSolPanelCraft;
    pF->solArm.pPad         = pW->pSolPad;                  /* the same 0x1598D4, read a second time at 0x92F14 */
    pF->facDialog.pPanel    = pW->pFacPanel;
    pF->facDialog.nPanelSel = pW->nPanelSel;                /* 0x158868, by value in both callers */

    /* The placement overlay reads its by-value inputs from the globals at the call (0x58E20 the panel row,
     * 0x58E58 the pending pair, 0x58E8C the cursor, 0x592D0 the base index), and the commit reads the picked
     * coordinates and the pending region with it. The create env goes too: Base_CreateNamedEntry offsets its
     * economy columns to the base it is about to create, and the commit creates base *pbBaseCount. The two
     * origins stay out: InitUI writes 0x159DC4 once and nothing writes 0x159DC8 at all. */
    pF->facOverlay.pPanel           = pW->pFacPanel;
    pF->facOverlay.nPanelSel        = pW->nPanelSel;
    pF->facOverlay.nSelectedBaseIdx = pW->nSelectedBase;
    pF->facOverlay.nPendingX        = pW->nUnk15887C;
    pF->facOverlay.nPendingY        = pW->nUnk158880;
    pF->facOverlay.nCursorX         = (i16)pF->g.nCursorX;  /* nSmapCursorX @ 0x186BD0 */
    pF->facOverlay.nCursorY         = (i16)pF->g.nCursorY;  /* 0x186BD4 */
    pF->facOverlay.nPlaceX          = pW->nPlaceX;          /* 0x159D94 */
    pF->facOverlay.nPlaceY          = pW->nPlaceY;          /* 0x159D98 */
    pF->facOverlay.nPendingRegionId = pW->bPendingRegionId; /* 0x159DF4 */
    pF->facOverlay.nPhaseByte       = (pW->pnMapPhase != NULL) ? (u8)*pW->pnMapPhase : 0;
    World_WireCreateEnv(pW);
    pF->anim.nFrameStyle  = (i8)pW->aRegionBuildCount[9]; /* bUiFrameStyle @ 0x186566 */
    pW->ptrEnv.pTiles     = (pSet->pTiles != NULL) ? pSet->pTiles->pData : NULL;

    /* The funding draw env carries two by-value cells: the pad pointer (FundingScreen_Init allocates
     * it into unk_1595DC at 0x5C0B4) and the month counter FundingScreen_DrawExtended clamps arrows
     * against (0x5B8CC). Both follow the clock, so they are re-read every frame. */
    pW->fundingDraw.pPad          = pW->pFundPad;
    pW->fundingDraw.nMonthCounter = pW->nMonthCounter;
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
