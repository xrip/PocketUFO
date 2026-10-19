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
#include "ui/pufo_panel.h"    /* UiPanel_ctor / SetBytes6to11 / WrapText / FreeLines, the tact init hooks */
#include "game/pufo_plane.h"   /* InterceptMissionCtx, BasePlaneScreen_AddInterceptMission */
#include "game/pufo_timer.h"   /* GameTickCtx / GameTickHooks / GameTick_OnTimer @ 0x274f0 */
#include "game/pufo_tactend.h" /* Tact_CheckBattleEnd @ 0x34fbc, the mission-end check the shell runs on the live frame */
#include "util/pufo_rand.h"    /* Game_RandU16 @ 0x3e59c, the enemy AI's draw seam */
#include "game/pufo_roster.h"  /* Roster_CycleNext/Select*, the tact nav widgets (Part 54) */
#include "game/pufo_tacthud.h" /* TactHud_OpenInventory (0x16ef4), the inventory the mission opens (Part 55) */
#include "game/pufo_tactctl.h" /* Map_BuildDeployConfirmYesNo @ 0x18a68, the deploy confirm the shell wires (Part 58) */

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
i32 World_MapClick(PufoWorldState *pW)
{
    MapClickEnv env;

    if (pW == NULL) {
        return 0;
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

    return Geoscape_OnMapClick(&env);
}

/* UiControl.pfnClick takes no arguments, so the installed handler is this wrapper. This one is
 * Geoscape_OnMapClick_Thunk @ 0x857e8: the placement screen's map click, no sample and no gate. */
static void World_MapClickThunk(void)
{
    (void)World_MapClick(g_pActiveWorld);
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

/* The tail call NewBaseScreen_TryConfirm makes at 0x61588 (LDR R0,=0x1864F8; BL Geoscape_OnMapClick), in the
 * argument shape NewBaseConfirmEnv wants. */
static i32 World_MapClickForConfirm(void *pUser)
{
    return World_MapClick((PufoWorldState *)pUser);
}

/* NewBaseScreen_TryConfirm @ 0x6152c is what the confirm control's pfnClick has to run -- Sound_Play(17, 4)
 * at 0x61538, then the funds gate at 0x6156C (skipped while the pick is pending), then OnMapClick. Both
 * installs (InitUI's 0x95418 block and NewBaseScreen_Open at 0x6162C) point at this. The env is built here
 * because the gate reads the region byte, nFunds and the two fail dwords at the call. */
static void World_ConfirmClick(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    NewBaseConfirmEnv env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.pfnPlaySample    = World_PlaySampleWithArg;                /* Sound_Play(17, 4) at 0x61538 */
    env.bPendingRegionId = pW->bPendingRegionId;                   /* bPendingBaseKind @ 0x159DF4 */
    env.pnFunds          = &pW->nFunds;                            /* unk_1598D8 */
    env.pbFlag1870C8     = (u8 *)&pW->nShortfall1870C8;           /* 0x1870C8: the original stores a dword, every reader
                                                                   * takes the byte, so the low byte of the shell's dword */
    env.pnFlag1870B4     = &pW->nResidue1870B4;                   /* dword_1870B4 */
    env.pfnMapClick      = World_MapClickForConfirm;              /* 0x61588 */
    env.pUser            = pW;
    (void)NewBaseScreen_TryConfirm(pW->bPlacementPending, &env);   /* 0x1870AC */
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

/* UiPanel_AppendLineWithSound @ 0x5a394 on the root message list @ 0x158890. The flag selects the
 * text record and the second argument is the value it carries: the new item's index for subtype 4,
 * `pUfo->nTargetIdx` for subtype 5. Same call the clock's contact seam uses at World_HookContactMsg. */
static void World_MissionAppendLine(i32 nFlag, i32 nArg2, i32 nArg3, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    UiLineList_AppendWithSound(&pW->msgLines, (u8)nFlag, (u32)nArg2, (u32)nArg3, 0u, 0u,
                               pW->pfnPlaySample, pW->pSoundUser);
}

/* BasePlaneScreen_AddInterceptMission @ 0x7de14, the hangar's intercept item: the mission-site record
 * picked by craft class (LDRSH +0 / +2 at 0x7DE48 / 0x7DE3C) becomes a type-3 entry, its nTimer is set
 * to 2880 (0x7DE80), and the new index lands in 0x186BE4 (0x7DE98) -- the cell the shell calls
 * nSelectedStrategIdx. */
static i32 World_MissionAddIntercept(i32 bCraftClass, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    InterceptMissionCtx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.pList        = &pW->strategList;                    /* 0x186534 */
    ctx.pnZoomMode   = &pW->view.nMode;                     /* read live by StrategList_AddItem */
    ctx.pnSeed       = &pW->nStrategSeed;                   /* 0x18640C */
    ctx.apSites      = pW->aMissionSites;                   /* 0x1865C0 */
    ctx.nSiteCount   = PUFO_MISSION_SITE_COUNT;
    ctx.pnMissionId  = &pW->nMissionId;                     /* 0x18654C */
    ctx.pnLastIndex  = &pW->nSelectedStrategIdx;            /* 0x186BE4 */

    return BasePlaneScreen_AddInterceptMission(bCraftClass, &ctx);
}

/* The base-attack screen's three callees, reached from two seams: the assault branch of
 * Geoscape_SpawnMissionSite (0x7EF38..0x7EF80) opens it, Geoscape_CloseUI case 20 (0x8BAF4) runs the same
 * opener with -1 as the teardown, and the frame's case 20 calls the draw (0x944B4). BaseAtScreen_Open
 * installs a click handler and a tail-called update, and BOTH take no arguments -- the original reaches
 * every cell through globals -- so the envs the last open built live in file scope, the way g_pActiveWorld
 * does for UiControl.pfnClick. */
static BaseAtUpdateEnv g_BaseAtUpd;
static BaseAtCloseEnv  g_BaseAtClose;

static void World_BaseAtUpdateThunk(i32 nMode) { BaseAtScreen_Update(nMode, &g_BaseAtUpd); }
static void World_BaseAtClickThunk(void) { BaseAtScreen_CloseThunk(&g_BaseAtClose); }

static void World_BuildBaseAtEnvs(PufoWorldState *pW)
{
    memset(&g_BaseAtUpd, 0, sizeof(g_BaseAtUpd));
    g_BaseAtUpd.pbActive           = &pW->bBaseAtActive;          /* 0x158864 */
    g_BaseAtUpd.pbConfirmShown     = &pW->bBaseAtConfirmShown;    /* 0x158854 */
    g_BaseAtUpd.pbMissionFollows   = &pW->bBaseAtMissionFollows;  /* 0x158850 */
    g_BaseAtUpd.ppPad              = &pW->pBaseAtPad;             /* 0x15884C */
    g_BaseAtUpd.ppPanel            = &pW->pBaseAtPanel;           /* 0x15885C */
    g_BaseAtUpd.pnLineLimit        = &pW->nBaseAtLineLimit;       /* 0x158858 */
    g_BaseAtUpd.pnLineCap          = &pW->nBaseAtLineCap;         /* 0x158860 */
    g_BaseAtUpd.pbCraftClass       = (const u8 *)&pW->nBaseAtClass; /* 0x158840, read SIGNED at 0x5784C */
    g_BaseAtUpd.abFacilityGrid     = pW->abFacilityGrid;          /* gBaseFacGrid @ 0x159768 */
    g_BaseAtUpd.pTextRecs          = (pW->pAssets != NULL) ? (const char *)pW->pAssets->pTextRecs : NULL;
    g_BaseAtUpd.pbSpawnReplacement = &pW->bSpawnOnce;             /* 0x162028, set by the release arm */
    g_BaseAtUpd.pnRadarCooldownR   = &pW->frame.g.nTapCooldown;   /* nRadarCooldownR @ 0xAC218 */

    memset(&g_BaseAtClose, 0, sizeof(g_BaseAtClose));
    g_BaseAtClose.pbMissionFollows   = &pW->bBaseAtMissionFollows;
    g_BaseAtClose.pStrategList       = &pW->strategList;          /* 0x1864F8 + 0x3C */
    g_BaseAtClose.pWorld             = &pW->geo.world;
    g_BaseAtClose.pnStrategIdx       = &pW->nBaseAtStrategIdx;    /* 0x158844 */
    g_BaseAtClose.pbSpawnReplacement = &pW->bSpawnOnce;
    g_BaseAtClose.pSpawnCtx          = &pW->spawnCtx;
    g_BaseAtClose.pTime              = &pW->geo.time;             /* its bPaused is 0x15961C */
    g_BaseAtClose.pClose             = &pW->closeEnv;             /* the tail call at 0x577FC */
}

/* The opener, both seams through one body: 0x7EF80 hands it the base index, 0x8BB00 hands it -1. The
 * control is the shared confirm control at 0x186BA4, the one the draw centres on (120, 290). */
static void World_BaseAtOpen(PufoWorldState *pW, i32 nBaseIdx)
{
    BaseAtScreenOpenHooks hooks;

    World_BuildBaseAtEnvs(pW);
    hooks.pfnClick  = World_BaseAtClickThunk;
    hooks.pfnUpdate = World_BaseAtUpdateThunk;
    BaseAtScreen_Open(pW->ui.apCtl[GEOUI_CTL_186BA4], nBaseIdx, &hooks);
}

/* 0x8BB00, Geoscape_CloseUI case 20. It runs Update's release arm, and this dispatcher passes -1 the way
 * the original does; the mission-follows test right after it reads bBaseAtMissionFollows @ 0x158850. */
static void World_BaseAtOpenHook(void *pUser)
{
    World_BaseAtOpen((PufoWorldState *)pUser, -1);
}

/* The assault branch's side effects, in the original's order: pause the clock (0x7EF4C), fill the record
 * (0x7EF58..0x7EF70), write the screen id 20 (0x7EF78), open the screen with the base index (0x7EF80).
 * The callee reaches none of them itself -- the port's MissionLanding only calls this hook. */
static void World_ContactScreen(StrategItem *pUfo, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    pW->geo.world.pTime->bPaused = 1;                       /* bGameClockPaused @ 0x15961C */
    pW->nBaseAtClass        = (i8)pUfo->bCraftClass;        /* 0x7EF58 */
    pW->nBaseAtStrategIdx   = pUfo->nSeed;                  /* 0x7EF60: the seed at +0x34, not the index */
    pW->nAssaultBaseRace    = (i8)pUfo->bRace;              /* 0x7EF68 */
    pW->nCraftHomeBase      = (i8)pUfo->nTargetIdx;         /* 0x7EF70 */
    pW->screen.nGeoscapeScreen = 20;                        /* 0x7EF78 */
    World_BaseAtOpen(pW, pUfo->nTargetIdx);                 /* 0x7EF80, the tail call */
}

/* The context Geoscape_SpawnMissionSite @ 0x7e49c builds out of globals in the original. The tile grid
 * is pW->sets[0].pTiles because that is the object Geoscape_InitUI stores into unk_186B74 (STR at
 * 0x85910 and 0x85920, the only two writers in the binary), and unk_186B74 is what
 * Tilemap_GetAt @ 0x8217c reads -- its own first argument is unused, so the grid does not travel with
 * the viewport. nGameTotalSec is GameTime.nTotalSec, the clock's apLines[105], which
 * StrategEntry_InitTravel stores as nTravelEpochSec (0x7FB6C). */
void World_FillMissionCtx(PufoWorldState *pW, SpawnMissionCtx *pCtx)
{
    memset(pCtx, 0, sizeof(*pCtx));
    pCtx->pList             = &pW->strategList;          /* 0x186534 */
    pCtx->pnSeed            = &pW->nStrategSeed;         /* 0x18640C */
    pCtx->pView             = &pW->view;                 /* gSmapCam @ 0x1863E8 */
    pCtx->pTilemap          = pW->sets[0].pTiles;        /* unk_186B74, the level-0 "m100" grid */
    pCtx->apBases           = pW->apBases;
    pCtx->nBaseCount        = (i32)pW->nBaseCount;       /* 0x159D80, signed byte */
    pCtx->apSites           = pW->aMissionSites;         /* 0x1865C0 */
    pCtx->nSiteCount        = PUFO_MISSION_SITE_COUNT;
    pCtx->abFacilityGrid    = pW->abFacilityGrid;        /* gBaseFacGrid @ 0x159768 */
    pCtx->pnRadarPulseTimer = &pW->nRadarPulseTimer;     /* 0xAC224, subtype 5 sets 250 */
    pCtx->pWorld            = &pW->geo.world;
    pCtx->nGameTotalSec     = pW->geo.time.nTotalSec;
    pCtx->hooks.pfnAppendLine     = World_MissionAppendLine;
    pCtx->hooks.pfnAddIntercept   = World_MissionAddIntercept;
    pCtx->hooks.pfnContactScreen  = World_ContactScreen;   /* 0x7EF80's side effects, see above */
    pCtx->hooks.pUser             = pW;
}

/* 0x81BEC: StrategList_RollCraftStats hands the mission subtype it just rolled straight to the spawner,
 * which starts it -- nPhase 3 for subtype 0, 4 for every other subtype, with the target cleared. */
static int World_SpawnMission(StrategItem *pUfo, i32 nMissionSub, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    SpawnMissionCtx ctx;

    World_FillMissionCtx(pW, &ctx);
    return (int)Geoscape_SpawnMissionSite(pUfo, nMissionSub, &ctx);
}

/* StrategList_ProcessEvents @ 0x7fcb0, the geoscape event pass StrategList_MaybeSpawnCraft_Process
 * tail-calls. Two sub-envs the shell does not own yet (the radar session and the tactical-loot env)
 * stay NULL, which skips those side effects; the guards are in the ported function. The mission ctx is
 * filled: 0x809B8 is the per-item mission advance (`Geoscape_SpawnMissionSite(pItem, -1, ...)`) that
 * runs once an item's travel time is up. */
static int World_ProcessEvents(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    StrategEventsEnv env;
    SpawnMissionCtx missionCtx;

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
    World_FillMissionCtx(pW, &missionCtx);
    env.pMissionCtx = &missionCtx;
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
    pW->spawnCtx.pfnSpawnMission = World_SpawnMission;
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
    env.paRegionBuildCount = pW->aRegionBuildCount; /* g_anRegionBuildCount @ 0x18655D */
    env.pEconomy = &pW->economy;
    env.paFacFlags = pW->aFac;
    env.pTextRecs = pAssets->pTextRecs;
    env.pScreen = &pW->screen;
    env.pCtlResearchPanel = &pW->ctlResearchPanel;
    /* The original installs TryConfirm through the global control POINTER *pGeoscapeCtlConfirm @ 0x186BA4
     * (0x95454 LDR R0,=pGeoscapeCtlConfirm; 0x9545c LDR R0,[R0]; 0x95464 STR R3,[R0,#0xC]) -- the very control
     * InitUI allocated at 0x86100..0x864DC, which is what the hover seats at 0x679F0 and what both pointer
     * dispatchers hit-test. A second object here gets the handler but never the click. */
    env.pCtlConfirm = pW->ui.apCtl[GEOUI_CTL_186BA4];
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

    env.pfnTryConfirmClick = World_ConfirmClick; /* NewBaseScreen_TryConfirm @ 0x6152c, installed by 0x95454..0x95464 */
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

/* The GeoscapeInitEnv Geoscape_InitUI is called with, built from the state's own owners. The
 * mission-end rebuild (UiScreen_OnCancel 0x162C8, mode 1) hands the very same construction a second
 * time, so the construction cannot stay inside World_NewGame. pAssets / pFont / pOptions and the
 * pad and sound hooks are on the state before this runs. */
static void World_FillInitUiEnv(PufoWorldState *pW, GeoscapeInitEnv *pEnv)
{
    i32 i;

    AssetReader_Clear(&pW->rdr);
    pW->ui.pFont = pW->pFont;       /* fontSys @ 0x158268: the tact env reads it, as does the alien sprite loader */
    pW->ui.pbGeoscapeUi = &pW->bGeoscapeUi;

    memset(pEnv, 0, sizeof(*pEnv));
    pEnv->pUi = &pW->ui;
    pEnv->pView = &pW->view;
    pEnv->pSetsOut = pW->sets;
    pEnv->pnScreenMode = &pW->screen.nGeoscapeScreen;
    pEnv->pbGeoscapeUi = &pW->bGeoscapeUi;
    pEnv->pbRoot34 = &pW->bRoot34;
    pEnv->pbRoot35 = &pW->bRoot35;
    pEnv->pFont = pW->pFont;
    pEnv->pRdr = &pW->rdr;
    pEnv->pTextRecs = pW->pAssets->pTextRecs;
    pEnv->pTime = &pW->geo.time;
    pEnv->apTopics = pW->aTopics;
    pEnv->puResearchEntries = &pW->nResearchEntries;
    pEnv->paFacFlags = pW->aFac;
    pEnv->apObjects = pW->pAssets->apObjects;
    pEnv->nObjectCount = pW->pAssets->nObjectCount;
    pEnv->pnGlyphPalBase = &g_nGlyphPalSlotBase; /* the original's one global, Map_nGlyphPalBase */
    pEnv->abFacilityGrid = pW->abFacilityGrid;
    pEnv->pWorld = &pW->geo.world;
    pEnv->pbBaseCount = &pW->nBaseCount;
    pEnv->pnSmapOriginX = &pW->nSmapOriginX;
    pEnv->paMissionSites = pW->aMissionSites;
    pEnv->aRegionBuildCount = pW->aRegionBuildCount;
    pEnv->pbSaveVersion = &pW->nSaveVersion;
    pEnv->pOptions = pW->pOptions;
    for (i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        pEnv->apfnRootClick[i] = World_NoopClick;
    }
    pEnv->pfnCraftSelectClick = World_NoopClick;
    pEnv->pfnRadarClick = World_RadarClickThunk; /* Geoscape_OnRadarListClick @ 0x854c4 */
    pEnv->pfnStartNew = World_StartNewHook;
    pEnv->pHookUser = pW;
    pEnv->pfnLoadPads = pW->pfnLoadPads;
    pEnv->pLoadPadsUser = pW->pLoadPadsUser;
}

i32 World_NewGame(PufoWorldState *pW, BootAssets *pAssets, PufoFont *pFont,
                  const PufoOptions *pOptions, const WorldHooks *pHooks)
{
    GeoscapeInitEnv env;

    pW->pAssets = pAssets;
    pW->pFont = pFont;       /* fontSys @ 0x158268: the tact env reads it, as does the alien sprite loader */
    pW->pOptions = pOptions; /* its gamma cell is Map+0x15100; its volume steps feed the tact tail */
    pW->gameClock.nSpeed = (u32)pOptions->nGameSpeed; /* unk_128AB4 @ Map+0x150FC, which the option load
                                                       * (Map_InitAndLoadRegistry @ 0x22648) fills: the shell's
                                                       * startup speed is the registry value, default 4 */
    pW->nMapGamma = pOptions->nGamma;                 /* Map+0x15100, same model and same copy */
    pW->pfnLoadPads = pHooks->pfnLoadPads;
    pW->pLoadPadsUser = pHooks->pLoadPadsUser;
    pW->pfnPlaySample = pHooks->pfnPlaySample;
    pW->pSoundUser = pHooks->pSoundUser;
    pW->pFrameHooks = pHooks;   /* the mission-end rebuild rewires the frame through this same object */
    g_pActiveWorld = pW;

    memset(&pW->optionsHooks, 0, sizeof(pW->optionsHooks));
    pW->optionsHooks.pfnPlaySample = World_OptionsPlaySampleStub;
    pW->optionsHooks.pfnInit = World_OptionsInitStub;
    pW->optionsHooks.pfnOpen = World_OptionsOpenStub;

    memset(&pW->ctorEnv, 0, sizeof(pW->ctorEnv));
    pW->ctorEnv.apObjects = pAssets->apObjects;
    pW->ctorEnv.nObjectCount = pAssets->nObjectCount;

    memset(&pW->geo.world, 0, sizeof(pW->geo.world));
    pW->geo.world.apCraft = pW->apCraft;
    pW->geo.world.nShipCount = pW->bCraftCount;
    pW->geo.world.apSiteState = pW->abSiteState;
    pW->geo.world.apRadarPct = pW->abRadarPct;
    pW->geo.world.pTime = &pW->geo.time;

    World_FillInitUiEnv(pW, &env);

    Geoscape_InitUI(&env, 0); /* 0x857f4, mode 0: the new-game branch */

    /* The pads and controls InitUI just created are what the geoscape frame reads. */
    World_WireGeoscapeFrame(pW, pHooks);

    return pW->nLastStartNewRet;
}

void World_Free(PufoWorldState *pW)
{
    GeoscapeRoster roster;
    i32 i;

    /* A live tactical mission is released first: its item and cell records go, and the soldier
     * records they reference go with GeoscapeUi_FreeAll right below (one owner, as in the original). */
    World_FreeTactical(pW);

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

/* The LoadGame teardown hooks. All three callees ARE ported -- Tact_Shutdown @ 0x3d568 in
 * pufo_tactend.c, Tact_InitMission @ 0x3c550 in pufo_tactinit.c, Geoscape_InitUI @ 0x857f4 in
 * pufo_geoiinit.c -- and stay stubs here for a different reason: each needs an env the shell does not
 * own on this path (a TactShutdownEnv / TactInitEnv for the tactical half, and the InitUI env whose
 * allocations World_NewGame owns). The sav7 path tests/test_save.c pins (a geoscape save with no
 * tactical state) reaches none of them, so the missing envs are untested rather than harmless. */
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
    SpawnMissionCtx missionCtx;
    DatFile dat;
    i32 nRet;

    pW->pOptions = pOptions; /* same ownership as World_NewGame; the load path has no font, so a
                              * tactical bring-up on this path would refuse -- the load hooks stay stubs */
    pW->gameClock.nSpeed = (u32)pOptions->nGameSpeed; /* the same Map+0x150FC mirror as World_NewGame */
    pW->nMapGamma = pOptions->nGamma;                 /* Map+0x15100 */

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
     * 0x1864F8 pad and control singletons are whatever a previous new game left, or NULL. The mission
     * ctx is built from the cells this function already owns, including the level-0 tile grid, which is
     * why it must be built after the UI objects: pW->sets[0].pTiles is NULL until InitUI has run, and
     * the callee's own guard turns that into the same no-op the missing state was. */
    World_FillMissionCtx(pW, &missionCtx);
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
    /* 0x44E8C..0x44F64, the version-<3 tail: StrategList_PurgeType5 drops every type-5 item no craft
     * claims (and StrategList_RemoveAt @ 0x82B50 then refills the craft quota through the spawn seam),
     * and any active craft item off the map is teleported to (150, 250) and given a subtype-0 mission.
     * Both need the spawn ctx and the 0x162028 one-shot byte, which the shell owns. */
    env.pSpawnCtx = &pW->spawnCtx;
    env.pMissionCtx = &missionCtx;
    env.pbSpawnOnceFlag = &pW->bSpawnOnce;
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
 * The one hook that still does nothing is pfnContactScreen inside the mission ctx: the assault branch
 * needs the record at 0x158840..0x158849, which no shell cell models yet. Recorded as its own issue.
 */

/* 0x5d1dc..0x5d28c in Geoscape_TickHourly: an entry that drifted past |1000| on x or y is re-seeded at
 * (150, 250) and then gets a fresh subtype-0 mission. GeoscapeHooks asks for the void form, so the
 * spawner's return value is dropped exactly as the original's tail does. */
static void World_HookSpawnMissionSite(StrategItem *pUfo, i32 nMissionSub, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    SpawnMissionCtx ctx;

    World_FillMissionCtx(pW, &ctx);
    (void)Geoscape_SpawnMissionSite(pUfo, nMissionSub, &ctx);
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

/* --- tactical mission bring-up -------------------------------------------------------------
 *
 * The launch hand-over at 0x83AC4 used to be a seam. It is real now: World_InitTactical fills
 * TactInitEnv over the state's tactical block (see the block in pufo_world.h) and runs the ported
 * Tact_InitMission @ 0x3c550 with it. Everything the mission builds belongs to the state, and
 * World_FreeTactical releases exactly that -- the shell's own teardown, NOT Tact_Shutdown @ 0x3d568:
 * that function's outcome table acts on the mission's result, and the mission-end unit (whose head
 * is the unported Tact_CheckBattleEnd @ 0x34fbc) is what will call it. Docs/parity/game-core.md
 * Unit 108 records the storage decisions and the three hook seams below.
 */

static bool World_TactPanelCtorHook(UiPanel *pPanel, i32 nLineCount, void *pUser)
{
    (void)pUser;
    return UiPanel_ctor(pPanel, nLineCount);            /* 0x3e730 */
}

static void World_TactPanelStyleHook(UiPanel *pPanel, u8 b6, u8 b7, u8 b8, u8 b9, u8 b10, u8 b11,
                                     void *pUser)
{
    (void)pUser;
    UiPanel_SetBytes6to11(pPanel, b6, b7, b8, b9, b10, b11); /* 0x3e914 */
}

static i32 World_TactPanelWrapHook(UiPanel *pPanel, u8 nMaxW, u8 nLineStart, const char *pszText,
                                   void *pUser)
{
    (void)pUser;
    return UiPanel_WrapText(pPanel, nMaxW, nLineStart, pszText); /* 0x37144 */
}

/* Map_BuildMissionTerrain reaches Font_LoadTactSprites @ 0x257c4 through its hook because the loader
 * ends with Font_BuildIndex, which rebuilds the index the builder's own passes then read. The env is
 * built from the same Map scalars the original reads, exactly as tests/test_font.c's TacEnv does. */
static void World_TactLoadTactSpritesHook(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    TactSpriteEnv env;

    memset(&env, 0, sizeof(env));
    env.nMissionType     = pW->pTactMap->nMissionType;  /* Map+0x1511A */
    env.nMapLayers       = pW->pTactMap->nMapLayers;    /* Map+0x15114 */
    env.nTerrainVariant  = pW->nMapTerrainVariant;      /* Map+0x15111 */
    env.nTerrainParam    = pW->nMapMissionTierRaw;      /* Map+0x1510C */
    env.paTerrainSets    = g_aTactTerrainSetName;       /* 0xA6858 */
    env.paUfoSets        = g_aTactUfoSetName;           /* 0xA69C0 */
    env.paPlaneNames     = g_aPlaneName;                /* 0xA6668 */
    env.paUfoClass       = g_abTactUfoClassByVariant;   /* 0xA6AE0 */
    env.nTerrainSetCount = 10;
    env.nUfoClassCount   = 8;
    env.nVariantCount    = 5;
    Font_LoadTactSprites(pW->pFont, &env);
}

static bool World_TactSoundInitHook(const char *pszPrefix, void *pUser)
{
    (void)pUser;
    return Sound_InitAndLoadSamples(pszPrefix);         /* 0x9e268, the tail's "tact" */
}

static void World_TactMusicHook(i32 nTrack, void *pUser)
{
    (void)pUser;
    Sound_LoadAndPlayMusic(nTrack);                     /* 0x9e130, the tail's track 1 */
}

static void World_TactMusicVolHook(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pOptions != NULL) {
        Sound_ApplyMusicVolume(Options_MusicLevel(pW->pOptions), pW->pOptions->bMusicOff); /* 0x9e454 */
    }
}

static void World_TactSfxVolHook(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pOptions != NULL) {
        Sound_ApplySfxVolume(Options_SfxLevel(pW->pOptions), pW->pOptions->bSoundOff);     /* 0x9e4dc */
    }
}

/* Tact_LoadUiPads @ 0x3b068's three injected operations (Part 53). The original's pfnAlloc is
 * operator new(0x14) followed by PadImage_ctor @ 0x11000 -- a zeroed record with pData NULL --
 * pfnLoad is PadImage_Load @ 0x11020 (member "<name>.pad" uppercased through AssetReader_Find),
 * and pfnStore is the Map slot write the port models as the shell's apTactPads table. */
static PadImage *World_TactPadAllocThunk(void *pUser)
{
    (void)pUser;
    PadImage *pPad = (PadImage *)malloc(sizeof(PadImage));

    if (pPad != NULL) {
        memset(pPad, 0, sizeof(*pPad));   /* operator new + PadImage_ctor: pData NULL, origins 0 */
    }
    return pPad;
}

static bool World_TactPadLoadThunk(PadImage *pPad, const char *pszName, u8 nMode, void *pUser)
{
    (void)pUser;
    return PadImage_Load(pPad, pszName, nMode);          /* 0x11020 */
}

static void World_TactPadStoreThunk(i32 nSlot, PadImage *pPad, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (nSlot >= 0 && nSlot < PUFO_UIPAD_SLOT_COUNT) {   /* the original stores through the Map offsets unguarded */
        pW->apTactPads[nSlot] = pPad;
    }
}

static bool World_TactLoadUiPadsThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    UiPadHooks padHooks;

    padHooks.pfnAlloc  = World_TactPadAllocThunk;
    padHooks.pfnLoad   = World_TactPadLoadThunk;
    padHooks.pfnStore  = World_TactPadStoreThunk;
    padHooks.pUser     = pW;
    return Tact_LoadUiPads(&padHooks);                   /* 0x3b068, all 54 slots */
}

/* The cell resolver the env's own Init_LookupCell is for everything Tact_InitMission wires; the
 * spawnAliens env and the drop hooks need the same lookup from outside that module. Same table,
 * same index, same bounds tests as Init_LookupCell (pufo_tactinit.c:54). */
static MapCell *World_TactLookupCell(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    const PufoWorldState *pW = (const PufoWorldState *)pUser;

    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    if (nY < 0 || nY >= PUFO_MAP_CELL_DIM || nX < 0 || nX >= PUFO_MAP_CELL_DIM) {
        return NULL;
    }
    return pW->apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer + PUFO_MAP_CELL_DIM * nY + nX];
}

static u8 World_TactObjWeightHook(TactItem *pItem, void *pUser)
{
    (void)pUser;
    return (pItem != NULL && pItem->pObj != NULL) ? pItem->pObj->bWeight : 0;
}

static u8 World_TactObjTwoHandedHook(TactItem *pItem, void *pUser)
{
    (void)pUser;
    return (pItem != NULL && pItem->pObj != NULL) ? pItem->pObj->bTwoHanded : 0;
}

static u8 World_TactObjAmmoCapHook(TactItem *pItem, void *pUser)
{
    (void)pUser;
    return (pItem != NULL && pItem->pObj != NULL) ? pItem->pObj->bAmmoCap : 0;
}

static u8 World_TactAmmoWeightHook(i32 nIndex, void *pUser)
{
    const PufoWorldState *pW = (const PufoWorldState *)pUser;

    if (pW->pAssets == NULL || nIndex < 0 || nIndex >= (i32)pW->pAssets->nObjectCount) {
        return 0;
    }
    return pW->pAssets->apObjects[nIndex]->bWeight;
}

static void World_TactDropPlaySampleHook(i32 nSample, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pfnPlaySample != NULL) {
        pW->pfnPlaySample(nSample, pW->pSoundUser);
    }
}

static ObjectRec *World_TactFindObjectHook(i32 nIndex, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pAssets == NULL || nIndex < 0 || nIndex >= (i32)pW->pAssets->nObjectCount) {
        return NULL;
    }
    return pW->pAssets->apObjects[nIndex];
}

static TactItem *World_TactAllocItemHook(void *pUser)
{
    (void)pUser;
    TactItem *pItem = (TactItem *)malloc(sizeof(TactItem)); /* operator new(0x18) */

    if (pItem != NULL) {
        TactItem_ctor(pItem);                               /* 0x3e718 */
    }
    return pItem;
}

static void World_TactFreeItemHook(TactItem *pItem, void *pUser)
{
    (void)pUser;
    free(pItem);                                            /* operator delete @ 0xa115c */
}

/* 0x10DA38 is the image's standing empty string (the baseui reads it as "None", 0x6F000 and friends).
 * Init_Briefing copies it into the briefing panel's line 1 / tail line. */
static const char s_szBriefLineEmpty[1] = "";

/* The tactical walker wiring (Part 50). The hooks and the dispatch live in the walker block further
 * down this file; World_InitTactical wires them at the end of the launch, so they are declared here. */
static void World_TactRenderFrame(PufoWorldState *pW);
static bool World_TactStatusIsBusy(void *pUser);
static bool World_TactStatusShouldRender(void *pUser);
static void World_TactStatusRender(void *pUser);
static void World_TactSetPlayerInput(bool bEnabled, void *pUser);
static void World_TactReadCursor(i32 *pnX, i32 *pnY, i32 *pnLayer, void *pUser);
static void World_TactSetCursor(i32 nX, i32 nY, i32 nLayer, void *pUser);
static void World_TactSelectThunk(Soldier *pUnit, void *pUser);
static void World_TactFocusSelectedThunk(void *pUser);
static void World_TactRequestFireThunk(Soldier *pUnit, i32 nX, i32 nY, i32 nZ, void *pUser);
static void World_TactEndOfOppFireThunk(void *pUser);
static void World_TactProcessPhaseThunk(void *pUser);
static void World_TactFocusCellThunk(const MapCell *pCell, void *pUser);
static void World_TactSpotFocusThunk(MapCell *pCell, void *pUser);
static void World_TactRenderThunk(void *pUser);
static void World_TactSetCursorPosThunk(i32 nX, i32 nY, void *pUser);
static void World_TactGameTickStepThunk(void *pUser);
static void World_TactAreaFireThunk(void *pUser);
static bool World_TactWeaponFireableThunk(i32 nSortKey, void *pUser);
static void World_TactFallBattleEndThunk(u8 bAbort, void *pUser);
static void World_TactKillBattleEndThunk(void *pUser);
static void World_TactFallTryOpportunityThunk(Soldier *pTarget, Soldier *pShooter, void *pUser);
static void World_TactPushForcedThunk(const char *pszMsg, void *pUser);
static void World_TactTickActiveUnit(void *pUser);
static void World_ModalEndThunk(void);
static void World_TactDropModalThunk(i32 nWidth, i32 nTextRec, i32 nLines, const char *szLabel,
                                     void *pUser);
static void World_TactEnemyAiTickThunk(TactCtx272 *pTact, void *pUser);
static void World_TactFaceTowardThunk(Soldier *pUnit, i32 nX, i32 nY, void *pUser);
static u32 World_TactRandU16Thunk(u32 nMax, void *pUser);
static bool World_TactPathToCellThunk(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser);
static void World_TactFillActionCostsThunk(void *pUser);
static void World_TactEnqueueTurnKickThunk(Soldier *pUnit, u8 nFacing, void *pUser);
static void World_TactInitUiControlsThunk(void *pUser); /* Tact_InitUiControls (0x3b498), Part 54 */
static bool World_TactOpenInventoryThunk(void *pUser);   /* TactHud_OpenInventory (0x16ef4), Part 55 */
static void World_TactAfterEnemyStepThunk(TactCtx272 *pTact, void *pUser); /* AfterEnemyStep (0x4c4a0), Part 56 */
static u8 World_TactClipAmmoCapThunk(u16 nAmmoTypeId, void *pUser);
static void World_TactEnemyAiVisThunk(Soldier *pUnit, void *pUser);
static void World_TactEnemyAiPushTextThunk(i32 nRecIndex, void *pUser);

bool World_InitTactical(PufoWorldState *pW, i32 nMode)
{
    TactInitEnv *pE;
    GameMap *pMap;

    if (pW->bTactLive) {
        return false;
    }
    /* The seam's 0x83AC4 call passes 0 (new mission). A save-load mode would need the slot file and
     * the TactLoadHooks block this launch path does not have, so the seam refuses anything else. */
    if (nMode != 0 || pW->pAssets == NULL || pW->pFont == NULL || pW->pOptions == NULL) {
        return false;
    }

    pMap = (GameMap *)malloc(sizeof(GameMap));
    if (pMap == NULL) {
        return false;
    }
    memset(pMap, 0, sizeof(*pMap));
    /* Map_nMissionType @ Map+0x1511A and Map_nMapLayers @ Map+0x15114 are single cells; the geoscape
     * side (0x840FC, 0x840F0) wrote the world copies, and Tact_InitMission reads them through pMap,
     * so the mirror happens here. (Two struct views of those cells are the recorded separate issue.) */
    pMap->nMissionType = pW->nMapMissionType;
    pMap->nMapLayers   = pW->nMapMapLayers;
    pW->pTactMap = pMap;

    if (pW->pTactState == NULL) {
        pW->pTactState = (TactState *)malloc(sizeof(TactState)); /* pMapTactical @ 0x157198, 0x111E8 */
        if (pW->pTactState == NULL) {
            free(pMap);
            pW->pTactMap = NULL;
            return false;
        }
    }
    memset(pW->apTactCells, 0, sizeof(pW->apTactCells)); /* Map_BuildMissionTerrain fills every slot */
    memset(&pW->tactEnv, 0, sizeof(pW->tactEnv));
    pE = &pW->tactEnv;

    /* --- the Map-block scalars the original reaches through the context base --- */
    pE->pMap             = pMap;
    pE->pbTactFlag       = &pW->bTactFlag;            /* Map+0x00 */
    pE->pbUnkMap01       = &pW->bMapUnk1139B9;        /* Map+0x01 */
    pE->pbPlayerInput    = &pW->bMapPlayerInput;      /* Map+0x38 */
    pE->puUiScreen       = &pW->nUiScreen;            /* unk_1139F2 @ Map+0x3A */
    pE->pbFire0          = &pW->bMapFire0;            /* Map+0x50 (Map+0x51 is pMap->bFireStatus1) */
    pE->pbFire2          = &pW->bMapFire2;            /* Map+0x52 */
    pE->pbGameTickHold   = &pW->bMapTickHold;         /* Map+0x5A */
    pE->pbBattleEnded    = &pW->bMapBattleEnded;      /* Map+0x5C */
    pE->pnUiSlot1        = &pW->nMapUiSlot1;          /* Map+0x64 */
    pE->pbSweepArmed     = &pW->bMapSweepArmed;       /* Map+0x68, LOBYTE(apUiSlotsA[2]) */
    pE->pbSlot8Byte2     = &pW->bMapSlot8Byte2;       /* Map+0x82 */
    pE->pbSlot8Byte3     = &pW->bMapSlot8Byte3;       /* Map+0x83 */
    pE->pnUiSlot9        = &pW->nMapUiSlot9;          /* Map+0x84 */
    pE->pnUiSlot10       = &pW->nMapUiSlot10;         /* Map+0x88 */
    pE->pnUiSlot11       = &pW->nMapUiSlot11;         /* Map+0x8C */
    pE->pbAltOutcomePath = &pW->bMapAltOutcomePath;   /* unk_1153D9 @ Map+0x1A21 */
    pE->pnGamma          = &pW->nMapGamma;            /* Map_nGamma @ Map+0x15100, synced from options */
    pE->pbUnkMap1511E    = &pW->bMapUnk1511E;         /* Map+0x1511E */
    pE->pnCraftMapX      = &pW->bMapCraftMapX;        /* Map+0x15139 / 0x1513A (0x128AF1 / 0x128AF2) */
    pE->pnCraftMapY      = &pW->bMapCraftMapY;
    pE->pnTerrainVariant = &pW->nMapTerrainVariant;   /* Map+0x15111 */
    pE->pnTier           = &pW->bMapUnk15112;         /* Map+0x15112 */
    pE->pnTierSource     = &pW->nMapMissionTierRaw;   /* Map+0x1510C */
    pE->pbUnk15115       = &pW->bMapUnk15115;         /* Map+0x15115 */
    pE->pnShotX          = &pW->nMapShotOrigX;        /* Map+0x14054 */
    pE->pnShotY          = &pW->nMapShotOrigY;        /* Map+0x14058 */
    pE->pnSpawnTileX     = &pW->nMapSpawnTileX;       /* Map+0x1514C */
    pE->pnSpawnTileY     = &pW->nMapSpawnTileY;       /* Map+0x15150 */
    pE->pbSpawnAlt       = &pW->bMapSpawnAlt;         /* Map+0x15148, written at 0x83A6C */
    pE->pbDrawIsoLayer   = &pW->bMapDrawIsoLayer;     /* Map+0x254B0 */
    pE->pbLightEnabled   = &pW->bMapLightEnabled;     /* Map+0x254B1 */
    pE->pnUnkMap254B4    = &pW->nMapUnk138E6C;        /* Map+0x254B4 */
    pE->pnUnkMap254B8    = &pW->nMapUnk138E70;        /* Map+0x254B8 */
    pE->pbUnkMap254D4    = &pW->bMissionCount;        /* unk_138E8C @ Map+0x254D4: the field the state
                                                       * already had under the IDB's old name */
    pE->pClock           = &pW->gameClock;            /* unk_128AB4 @ Map+0x150FC */

    /* --- the storage the mission fills --- */
    pE->pTactState     = pW->pTactState;
    pE->pRmp           = &pW->sTactRmp;
    pE->apCells        = pW->apTactCells;             /* Map.apCells @ Map+0x15338 */
    pE->ppDeployCell   = &pW->pTactDeployCell;        /* 0x157204 */
    pE->ppShowShip     = &pW->pTactShowShip;          /* dword_156A78 */
    pE->pLightmap      = pW->abTactLightmap;          /* unk_156AB8 */
    pE->ppBriefPad     = &pW->pTactBriefPad;          /* unk_15692C */
    pE->pFont          = pW->pFont;                   /* fontSys @ 0x158268 */
    pE->apObjects      = pW->pAssets->apObjects;      /* Map.apObjects @ Map+0x108 */
    pE->pWeaponSel     = &pW->sTactWeaponSel;         /* unk_10DB90 */
    pE->pUnk138EA0     = &pW->sMapSentinel138EA0;     /* Map+0x254E8 */
    pE->pItem156A88    = &pW->sTactItem156A88;
    pE->pItem156910    = &pW->sTactItem156910;
    pE->pItem156A58    = &pW->sTactItem156A58;
    pE->apVisRays      = pW->aTactVisRays;            /* unk_B2278 */
    pE->paVisSteps     = pW->abTactVisSteps;          /* byte_AD720 */
    pE->pTextRecs      = pW->pAssets->pTextRecs;      /* aTextRecs @ 0xB38D0 */
    pE->nTextRecCount  = pW->pAssets->nTextRecCount;
    pE->pszBriefLine   = s_szBriefLineEmpty;          /* 0x10DA38 */
    pE->pDeployOffsets = g_aDeployCellOffsets;        /* aDeployCellOffsets @ 0xA66B8 */
    pE->pDeployPointCounts = g_abDeployPointCounts;   /* aDeployPointCounts @ 0xA67F0 */
    pE->pTileTypes     = pW->abMapTileTypes;          /* 0x159D5C, built at 0x84124 */
    pE->pLoft          = &pW->sTactLoft;
    pE->apSoldiers     = pW->apSoldiers;              /* Map.apSoldiers @ Map+0x1198 */
    pE->pnSoldierCount = &pW->nSoldierCount;          /* Map+0x1998 */
    pE->nSoldierCapacity = PUFO_SOLDIER_CAPACITY;
    pE->apTactItems    = pW->apTactItems;             /* Map.apTactItems @ Map+0x23438 */
    pE->pnTactItemCount = &pW->nTactItemCount;       /* Map+0x1513C */
    pE->pnFlobjsWide   = &pW->nMapFlobjsWide;         /* Map+0x118 */
    pW->nMapObjectCountU16 = (u16)pW->pAssets->nObjectCount; /* Map_nObjectCount @ Map+0x10A8 */
    pE->nObjectCount   = (i32)pW->pAssets->nObjectCount;

    /* --- callee envs. Init_FillCalleeEnvs fills the mission-dependent fields; these are the
     *     world-dependent fields the original reaches through globals it reads at the call. --- */
    pE->select.panRankLow   = (const char *)g_textTables.pE;  /* aszTextE @ 0xB2200, Xcom ranks */
    pE->select.panRankOther = (const char *)g_textTables.pA;  /* aszTextA @ 0xB2148, alien ranks */

    pE->teamSelect.apSoldiers    = pW->apSoldiers;
    pE->teamSelect.nSoldierCount = pW->nSoldierCount;
    pE->teamSelect.apCraft       = pW->apCraft;
    pE->teamSelect.bMissionMode  = pW->bMapMissionMode;       /* 1 on the assault path @ 0x8BB20 */
    pE->teamSelect.nCraftIndex   = pW->nMapCraftIndex;        /* Map+0x15113 */
    pE->teamSelect.nSelectedBase = pW->nSelectedBase;         /* 0x159D81, written at 0x84130 */

    pE->spawnAliens.create.apSoldiers      = pW->apSoldiers;
    pE->spawnAliens.create.pnSoldierCount  = &pW->nSoldierCount;
    pE->spawnAliens.create.pnNextSoldierId = &pW->nNextSoldierId;
    pE->spawnAliens.apEnemyUnits       = pMap->apEnemyUnits;       /* unk_128B84 @ Map+0x151CC */
    pE->spawnAliens.pnEnemyUnitCount   = &pMap->nEnemyCount;       /* Map+0x15124 */
    pE->spawnAliens.apHiddenSoldiers   = pW->apTactHidden;         /* Map+0x25438 */
    pE->spawnAliens.pnEnemyReserveCount = &pW->nMapEnemyReserveCount; /* Map+0x15144 */
    pE->spawnAliens.apTactItems        = pW->apTactItems;
    pE->spawnAliens.pnTactItemCount    = &pW->nTactItemCount;
    pE->spawnAliens.apObjects          = pW->pAssets->apObjects;
    pE->spawnAliens.pFont              = pW->pFont;
    pE->spawnAliens.pRmp               = &pW->sTactRmp;
    pE->spawnAliens.pfnLookupCell      = World_TactLookupCell;    /* for Rmp_PickSpawnForUnit */
    pE->spawnAliens.pLookupUser        = pW;
    pE->spawnAliens.pInvHooks          = &pW->tactInvHooks;
    /* Tact_KillUnit (0x4e430) at the spawn tail stays a seam: the kill path needs the whole KillCtx,
     * which belongs to the combat unit (a recorded deviation -- the original kills its stealth-flagged
     * aliens, this path leaves them in the reserve). */
    pE->spawnAliens.pfnKillUnit = NULL;
    pE->spawnAliens.pKillUser   = NULL;
    pE->spawnAliens.nMissionMode    = pW->bMapMissionMode;
    pE->spawnAliens.nMissionType    = pW->nMapMissionType;
    pE->spawnAliens.nTerrainVariant = pW->nMapTerrainVariant;
    pE->spawnAliens.nAlienTier      = pW->bMapUnk15112;
    pE->spawnAliens.nAlienPoolScale = pW->bMapUnk15115;
    memcpy(pE->spawnAliens.abSpriteSet, pW->abMapSpriteSet, 4);    /* 0x84054..0x840E4 filled */
    pE->spawnAliens.nAlienPoolAddend = pW->nAlienPoolAddend;       /* 0x1472c8, read LDRH */
    pE->spawnAliens.panUnitName      = (const char *)g_textTables.pB; /* 0xB1140, 40-byte records */
    pE->spawnAliens.nAlienSetIndex   = 0;   /* only used above mode 3; the port computes 0..3 */
    pE->spawnAliens.nFourthRaceCount = 0;   /* the original's unwritten var_24 -- see bugs.tsv */

    pE->cargo.apCraft        = pW->apCraft;            /* Map.apCraft @ Map+0x10AC */
    pE->cargo.pnCraftCount   = &pW->bCraftCount;       /* Map+0x1194 */
    pE->cargo.pnObjectCount  = &pW->nMapObjectCountU16;/* Map+0x10A8 as a u16* */
    pE->cargo.panCargoSplit  = pW->aTactCargoSplit;    /* unk_156CD0 */
    pE->cargo.panCargoWork   = pW->aTactCargoWork;     /* unk_157208 */
    pE->cargo.nCraftIndex    = pW->nMapCraftIndex;     /* Map+0x15113 */
    pE->cargo.nBaseIndex     = (i8)pW->nCraftHomeBase; /* 0x158848, read LDRSB */

    pE->deploy.create.apSoldiers      = pW->apSoldiers;
    pE->deploy.create.pnSoldierCount  = &pW->nSoldierCount;
    pE->deploy.create.pnNextSoldierId = &pW->nNextSoldierId;
    pE->deploy.pnDeployedHwps   = &pW->nMapDeployedHwps; /* unk_128AF8 @ Map+0x15140 */
    pE->deploy.pHwpsOffsets     = g_aHwpsDeployOffsets;  /* aHwpsDeployOffsets @ 0xA6688 */
    pE->deploy.panScatterMid    = pW->aMapScatterMid;    /* unk_1556D0 */
    pE->deploy.panScatterLast   = pW->aMapScatterLast;   /* unk_155FD0 */
    pE->deploy.pnScatterCount   = &pW->nMapScatterCount; /* unk_157200 */
    pE->deploy.pTileTypes       = pW->abMapTileTypes;    /* 0x159D5C, phase-5 scatter filter */

    /* Unit_RecalcInventoryStat's injected object reads (pufo_unit.h:452). */
    pW->tactInvHooks.pfnObjWeight    = World_TactObjWeightHook;
    pW->tactInvHooks.pfnObjTwoHanded = World_TactObjTwoHandedHook;
    pW->tactInvHooks.pfnObjAmmoCap   = World_TactObjAmmoCapHook;
    pW->tactInvHooks.pfnAmmoWeight   = World_TactAmmoWeightHook;
    pW->tactInvHooks.pUser           = pW;

    memset(&pW->sTactHit, 0, sizeof(pW->sTactHit));
    memset(&pW->sTactUi, 0, sizeof(pW->sTactUi));
    memset(&pW->sTactDrop, 0, sizeof(pW->sTactDrop));
    memset(&pW->sTactDropHooks, 0, sizeof(pW->sTactDropHooks));
    pW->sTactDrop.pHit      = &pW->sTactHit;
    pW->sTactDrop.pUi       = &pW->sTactUi;
    pW->sTactDrop.pSentinel = &pW->sMapSentinel138EA0;  /* unk_138EA0 @ Map+0x254E8 */
    pW->sTactDrop.pHooks    = &pW->sTactDropHooks;
    pW->sTactDropHooks.pfnPlaySample  = World_TactDropPlaySampleHook;  /* Sound_Play @ 0x9e0c0 */
    pW->sTactDropHooks.pfnFindObject  = World_TactFindObjectHook;
    pW->sTactDropHooks.pfnAllocItem    = World_TactAllocItemHook;
    pW->sTactDropHooks.pfnFreeItem     = World_TactFreeItemHook;
    pW->sTactDropHooks.apItemPool      = pW->apTactItems;             /* unk_136DF0 @ Map+0x23438 */
    pW->sTactDropHooks.pnItemCount     = &pW->nTactItemCount;
    pW->sTactDropHooks.pfnLookupCell   = World_TactLookupCell;
    pW->sTactDropHooks.pLookupUser     = pW;
    pW->sTactDropHooks.pInvStat        = &pW->tactInvHooks;
    pW->sTactDropHooks.pUser           = pW;
    /* pfnModal = the drop-modal opener, the UiModal_Begin shape of the call at 0x123E0 (Part 51).
     * Tact_EquipTeamFromCraft's parked-pointer path still never reaches the modal arm -- the opener
     * is wired so the ctx matches the original's. */
    pW->sTactDropHooks.pfnModal = World_TactDropModalThunk;
    pE->equip.pHit  = &pW->sTactHit;
    pE->equip.pDrop = &pW->sTactDrop;

    /* The VisCtx trio Init_FillCalleeEnvs does not fill (pufo_unit.h:700): the shared spot state, the
     * roster view and the action-scratch exclusion slot. */
    memset(&pW->sTactSpot, 0, sizeof(pW->sTactSpot));
    pW->sTactSpot.pnTrajFront1036 = &pW->nMapTrajFront1036;
    pW->sTactRosters.apXcom = pMap->apXcomUnits;
    pW->sTactRosters.nXcom  = (i32)pMap->nXcomCount;   /* 0 here; team select fills the array itself */
    pW->sTactRosters.apEnemy = pMap->apEnemyUnits;
    pW->sTactRosters.nEnemy  = (i32)pMap->nEnemyCount;
    pW->sTactRosters.apCiv   = pMap->apCivilians;
    pW->sTactRosters.nCiv    = pMap->nCivilianCount;
    pE->vis.pSpot         = &pW->sTactSpot;
    pE->vis.pRosters      = &pW->sTactRosters;
    pE->vis.ppScratchUnit = &pW->pTactScratchUnit;

    /* Map+0x00 / Map+0x44: the input handlers branch on these two bytes (the gates at 0x13CE0 and
     * 0x15F64). A tactical mission takes the tact branch; the rest of sTactInput is the shell's own
     * startup state, which the original never clears at mission start either. */
    pW->sTactInput.bTactFlag    = true;
    pW->sTactInput.bGeoscapeUi  = false;

    /* --- the hooks Tact_InitMission itself calls --- */
    pE->hooks.pfnPanelCtor            = World_TactPanelCtorHook;
    pE->hooks.pfnPanelStyle6to11      = World_TactPanelStyleHook;
    pE->hooks.pfnPanelWrapText        = World_TactPanelWrapHook;
    pE->hooks.pfnLoadTactSprites      = World_TactLoadTactSpritesHook;
    pE->hooks.pfnSoundInit            = World_TactSoundInitHook;
    pE->hooks.pfnSoundPlayMusic       = World_TactMusicHook;
    pE->hooks.pfnSoundApplyMusicVolume = World_TactMusicVolHook;
    pE->hooks.pfnSoundApplySfxVolume  = World_TactSfxVolHook;
    pE->hooks.pfnLoadUiPads           = World_TactLoadUiPadsThunk; /* Tact_LoadUiPads (0x3b068), the 54-slot pad table (Part 53) */
    pE->hooks.pfnInitUiControls       = World_TactInitUiControlsThunk; /* Tact_InitUiControls (0x3b498): the bank's click handlers, the look control and the group placement (Part 54) */
    pE->hooks.pfnOpenInventory        = World_TactOpenInventoryThunk; /* TactHud_OpenInventory (0x16ef4) at the 0x3D0B8 site, the Xcom-team branch (Part 55) */
    pE->hooks.pUser                   = pW;

    /* Mode 0 needs none of the save blocks (pDat, pSaveHooks, pfnLoadRmp all stay NULL). */

    if (!Tact_InitMission(pE, nMode)) {
        World_FreeTactical(pW);
        return false;
    }
    pW->bTactLive = true;

    /* --- the walker wiring (Part 50). After Tact_InitMission so the pieces taken from tactEnv
     * here see the init's own filling (select's cursor/lookup/status, the vis ctx). --- */
    pW->sTactStatusHooks.pfnIsBusy       = World_TactStatusIsBusy;
    pW->sTactStatusHooks.pfnShouldRender = World_TactStatusShouldRender;
    pW->sTactStatusHooks.pfnRender       = World_TactStatusRender;
    pW->sTactStatusHooks.pUser           = pW;
    pE->select.pStatusHooks = &pW->sTactStatusHooks;

    memset(&pW->sTactMoveWiring, 0, sizeof(pW->sTactMoveWiring));
    pW->sTactMoveWiring.pMap          = pMap;
    pW->sTactMoveWiring.pTextRecs     = pW->pAssets->pTextRecs;
    pW->sTactMoveWiring.nTextRecCount = pW->pAssets->nTextRecCount;
    /* Deviation: the radio option bank (unk_AC81A / 83A / 85A) is registry-loaded state the shell
     * does not own yet; zero means the default armour slot and no suppressed warning. */

    pW->sTactTurnHooks.pfnSetPlayerInput      = World_TactSetPlayerInput;
    pW->sTactTurnHooks.pfnSetCursor           = World_TactSetCursor;
    pW->sTactTurnHooks.pfnReadCursor          = World_TactReadCursor;
    pW->sTactTurnHooks.pfnSelectUnit          = World_TactSelectThunk;
    pW->sTactTurnHooks.pfnFocusSelected       = World_TactFocusSelectedThunk;
    pW->sTactTurnHooks.pfnRequestFire         = World_TactRequestFireThunk;
    pW->sTactTurnHooks.pfnEnemyAiTick         = World_TactEnemyAiTickThunk;
    pW->sTactTurnHooks.pfnEnqueueEndOfOppFire = World_TactEndOfOppFireThunk;
    pW->sTactTurnHooks.pUser                  = pW;
    memset(&pW->sTactPhaseSave, 0, sizeof(pW->sTactPhaseSave));

    pW->sTactTickHooks.pfnProcessActionPhase = World_TactProcessPhaseThunk;
    pW->sTactTickHooks.pfnRenderFrame        = World_TactRenderThunk;
    pW->sTactTickHooks.pfnFocusOnCell        = World_TactFocusCellThunk;
    pW->sTactTickHooks.pfnSetCursorPos       = World_TactSetCursorPosThunk;
    pW->sTactTickHooks.pfnGameTickStep       = World_TactGameTickStepThunk;
    pW->sTactTickHooks.pUser                 = pW;

    pW->sTactFireGate.pfnWeaponFireable = World_TactWeaponFireableThunk;
    pW->sTactFireGate.pUser             = pW;
    memset(&pW->sTactActionCosts, 0, sizeof(pW->sTactActionCosts));
    pW->nMapActionMode = 0;

    /* --- the enemy phase and its path walk (Part 52). The cost/enabled pointers go to
     * sTactUi's banks: Ui_FillActionCosts fills exactly those cells (Map_anActionCost @
     * 0x118804 / Map_anActionEnabled @ 0x118834, the enemy AI refills then reads within one
     * tick, so reader and writer share the object). */
    memset(&pW->sTactPathScratch, 0, sizeof(pW->sTactPathScratch));
    memset(&pW->sTactPath, 0, sizeof(pW->sTactPath));
    pW->sTactPath.pState         = pW->pTactState;
    pW->sTactPath.ppSelUnit      = &pMap->pSelUnit;         /* Map_pSelUnit @ Map+0x15334 */
    pW->sTactPath.ppPathDestUnit = &pMap->pPathDestUnit;    /* Map_pPathDestUnit @ Map+0x150F4 */
    pW->sTactPath.apPathCells    = pMap->apPathCells;       /* Map_apPathCells @ Map+0x4A48 */
    pW->sTactPath.pnPathCount    = &pMap->nPathCount;       /* Map_nPathCount @ Map+0x4E44 */
    pW->sTactPath.pScratch       = &pW->sTactPathScratch;
    pW->sTactPath.pfnLookup      = World_TactLookupCell;
    /* Deviation: pfnProbe stands for Unit_CanStepTo @ 0x27bb8, still unported; the path port
     * treats a NULL probe as permissive (pufo_path.c:139), so door/step refusals are not
     * applied on a walked path yet. */
    pW->sTactPath.pfnProbe       = NULL;
    pW->sTactPath.pProbeUser     = NULL;
    pW->sTactPath.pLookupUser    = pW;
    pW->sTactPath.pGeom          = &pMap->geom;

    memset(&pW->sTactEnemyAi, 0, sizeof(pW->sTactEnemyAi));
    pW->sTactEnemyAi.pfnRandU16            = World_TactRandU16Thunk;
    pW->sTactEnemyAi.pfnPathToCell         = World_TactPathToCellThunk;
    pW->sTactEnemyAi.pfnFillActionCosts    = World_TactFillActionCostsThunk;
    pW->sTactEnemyAi.pfnUpdateVisibility   = World_TactEnemyAiVisThunk;
    pW->sTactEnemyAi.pfnEnqueueTurnAndKick = World_TactEnqueueTurnKickThunk;
    pW->sTactEnemyAi.pfnAfterEnemyStep     = World_TactAfterEnemyStepThunk; /* AfterEnemyStep (0x4c4a0) over the shell-owned EnemyStepCtx (Part 56) */
    pW->sTactEnemyAi.pbEnemyTurnActive     = &pW->bMapEnemyTurnActive;      /* 0x113A13; the EndEnemyTurn tail clears it (0x4CB88) */
    pW->sTactEnemyAi.pfnClipAmmoCap        = World_TactClipAmmoCapThunk;
    pW->sTactEnemyAi.pfnLookup             = World_TactLookupCell;
    pW->sTactEnemyAi.geom                  = pMap->geom;      /* value copy, refreshed per tick */
    pW->sTactEnemyAi.pQueue                = pW->pTactState;  /* pMapTactical @ 0x157198 */
    pW->sTactEnemyAi.pSelect               = &pE->select;     /* Tact_SelectUnit @ 0x2c6c4 */
    pW->sTactEnemyAi.pTurnHooks            = &pW->sTactTurnHooks;
    pW->sTactEnemyAi.apEnemyUnits          = pMap->apEnemyUnits;   /* Map_apEnemyUnits @ Map+0x151CC */
    pW->sTactEnemyAi.pnEnemyCount          = &pMap->nEnemyCount;   /* Map+0x15124 */
    pW->sTactEnemyAi.ppSelUnit             = &pMap->pSelUnit;      /* Map+0x15334 */
    pW->sTactEnemyAi.pFirstXcom            = pMap->apXcomUnits[0]; /* Map_apXcomUnits[0] @ Map+0x15154, refreshed per tick */
    pW->sTactEnemyAi.ppSelWeapon           = &pMap->pSelWeapon;    /* Map.pSelWeapon @ Map+0x48 */
    pW->sTactEnemyAi.pnActionMode          = &pW->nMapActionMode;  /* Map.nActionMode @ Map+0x4C */
    pW->sTactEnemyAi.pnMultiTargetCount    = &pMap->nMultiTargetCount;
    pW->sTactEnemyAi.apMultiTargets        = pMap->apMultiTargets;
    pW->sTactEnemyAi.pnBurstShots          = &pMap->nBurstShots;   /* Map_nBurstShots @ Map+0x28 */
    pW->sTactEnemyAi.pnMeleeCellX          = &pMap->nMeleeCellX;
    pW->sTactEnemyAi.pnMeleeCellY          = &pMap->nMeleeCellY;
    pW->sTactEnemyAi.pnMeleeCellLayer      = &pMap->nMeleeCellLayer;
    pW->sTactEnemyAi.anActionCost          = pW->sTactUi.anActionCost;
    pW->sTactEnemyAi.anActionEnabled       = pW->sTactUi.anActionEnabled;
    pW->sTactEnemyAi.pItemMelee            = &pW->sTactItem156910; /* TactItem_156910 @ 0x156910 */
    pW->sTactEnemyAi.pItemAction13         = &pW->sTactItem156A58; /* TactItem_156A58 @ 0x156A58 */
    pW->sTactEnemyAi.pRmp                  = &pW->sTactRmp;        /* pRmpMissionData @ 0x15881C */
    pW->sTactEnemyAi.pActCtx               = &pW->sTactEnemyAiAct;
    pW->sTactEnemyAi.pfnPushTextRecord     = World_TactEnemyAiPushTextThunk;
    pW->sTactEnemyAi.pPushTextUser         = pW;
    pW->sTactEnemyAi.pUser                 = pW;

    memset(&pW->sTactEnemyAiAct, 0, sizeof(pW->sTactEnemyAiAct));
    pW->sTactEnemyAiAct.pfnRandU16         = World_TactRandU16Thunk;
    pW->sTactEnemyAiAct.pfnPathToCell      = World_TactPathToCellThunk;
    pW->sTactEnemyAiAct.pfnFillActionCosts = World_TactFillActionCostsThunk;
    pW->sTactEnemyAiAct.pfnPushStatus      = World_TactPushForcedThunk;
    pW->sTactEnemyAiAct.pfnPushTextRecord  = World_TactEnemyAiPushTextThunk;
    pW->sTactEnemyAiAct.pPushTextUser      = pW;
    pW->sTactEnemyAiAct.pfnFaceToward      = World_TactFaceTowardThunk;
    pW->sTactEnemyAiAct.pfnLookupCell      = World_TactLookupCell;
    pW->sTactEnemyAiAct.pState             = pW->pTactState;
    pW->sTactEnemyAiAct.pGeom              = &pMap->geom;
    pW->sTactEnemyAiAct.ppSelWeapon        = &pMap->pSelWeapon;
    pW->sTactEnemyAiAct.pnActionMode       = &pW->nMapActionMode;
    pW->sTactEnemyAiAct.anActionCost       = pW->sTactUi.anActionCost;
    pW->sTactEnemyAiAct.anActionEnabled    = pW->sTactUi.anActionEnabled;
    pW->sTactEnemyAiAct.pbActionLatch      = &pMap->bActionLatch;  /* Map+0x02 */
    pW->sTactEnemyAiAct.pUser              = pW;

    pW->sTactShotHooks.pfnComputeFov = NULL; /* the argument list is still open (pufo_combat.h:93) */
    pW->sTactShotHooks.pfnAreaFire   = World_TactAreaFireThunk;
    pW->sTactShotHooks.pUser         = pW;
    memset(&pW->sTactShotOut, 0, sizeof(pW->sTactShotOut));

    /* With the walker live, the spot seam can follow a spotted unit too: Unit_SpotCellOccupant's
     * MapView_FocusOnUnit is the same focus the walker's camera arms use. */
    pW->sTactSpot.pfnFocusOnCell = World_TactSpotFocusThunk;
    pW->sTactSpot.pUser          = pW;

    return true;
}

void World_FreeTactical(PufoWorldState *pW)
{
    i32 i;

    /* The five panels: the line buffers UiPanel_ctor malloc'd, then the panel records Init malloc'd. */
    for (i = 0; i < PUFO_TACTINIT_PANELS; ++i) {
        UiPanel *pPanel = pW->tactEnv.apPanel[i];

        if (pPanel != NULL) {
            UiPanel_FreeLines(pPanel);   /* 0x3e8c8 */
            free(pPanel);
            pW->tactEnv.apPanel[i] = NULL;
        }
    }

    for (i = 0; i < (i32)(sizeof(pW->apTactCells) / sizeof(pW->apTactCells[0])); ++i) {
        free(pW->apTactCells[i]);        /* each Map_BuildMissionTerrain malloc, per slot */
    }
    memset(pW->apTactCells, 0, sizeof(pW->apTactCells));
    pW->pTactDeployCell = NULL;          /* a cell of the table just freed */

    for (i = 0; i < pW->nTactItemCount; ++i) {
        free(pW->apTactItems[i]);
    }
    memset(pW->apTactItems, 0, sizeof(pW->apTactItems));
    pW->nTactItemCount = 0;

    free(pW->pTactShowShip);             /* dword_156A78 */
    pW->pTactShowShip = NULL;

    if (pW->pTactBriefPad != NULL) {     /* unk_15692C -- the slot the next mission drops without
                                          * freeing; the shell releases what it owns */
        PadImage_FreeData(pW->pTactBriefPad);
        free(pW->pTactBriefPad);
        pW->pTactBriefPad = NULL;
    }

    /* The tact pad table (Part 53). Tact_Shutdown frees exactly these records in three families:
     * the 32-slot sprite set walked 4x2x4 from Map+0x199C (0x3E150), the eleven button pads at
     * Map+0xCC, and the named singletons (the twelve pPad frees at 0x3E17C..0x3E318 plus the early
     * pair at 0x3D67C/0x3D6C8 -- 0x15692C is the briefing slot already freed above). Each free is
     * PadImage's own data block plus the record. */
    for (i = 0; i < PUFO_UIPAD_SLOT_COUNT; ++i) {
        PadImage *pPad = pW->apTactPads[i];

        if (pPad != NULL) {
            PadImage_FreeData(pPad);
            free(pPad);
            pW->apTactPads[i] = NULL;
        }
    }

    /* The tact widget pads (Part 54). Tact_Shutdown frees the pads of the thirteen tact families
     * between 0x3D6D8 and 0x3DC44 -- bank widgets 0..112 plus 128 and 129 -- and the look control's
     * pads at 0x3DC98; it never touches the start-menu widgets 113..127, which StartUi_Shutdown
     * frees (pufo_menubtn.h:233). The bank record itself belongs to the caller (WorldHooks.pBank),
     * so the shell releases only the pads UiControl_Setup loaded into it. */
    if (pW->pFrameHooks != NULL && pW->pFrameHooks->pBank != NULL) {
        UiWidgetBank *pBank = pW->pFrameHooks->pBank;

        for (i = 0; i < 113; ++i) {
            UiControl_FreePads(&pBank->aCtl[i]);
        }
        UiControl_FreePads(&pBank->aCtl[128]);
        UiControl_FreePads(&pBank->aCtl[129]);
    }
    UiControl_FreePads(&pW->sTactLookCtl);
    pW->sTactInput.bTactFlag = false;
    pW->sTactInput.pbScratchFlag9 = NULL;

    Rmp_FreeNodes(&pW->sTactRmp);        /* the rmp nodes; the record itself is inline */

    free((void *)pW->sTactLoft.pLoftemps);
    free((void *)pW->sTactLoft.pUnitloft);
    memset(&pW->sTactLoft, 0, sizeof(pW->sTactLoft));

    free(pW->pTactMap);
    pW->pTactMap = NULL;
    free(pW->pTactState);                /* the block whose slot Tact_Shutdown would free */
    pW->pTactState = NULL;

    /* The rosters' and the hidden park's SOLDIER RECORDS are apSoldiers entries: World_Free's
     * GeoscapeUi_FreeAll frees them (the original has one owner of the soldier array). Here only the
     * mission's views of them go. */
    memset(pW->apTactHidden, 0, sizeof(pW->apTactHidden));
    pW->nMapEnemyReserveCount = 0;

    pW->bTactLive = false;
}

/* GameTick_OnTimer's hook for the 0x275E8 store (byte_138E74 = 0 at the end of every
 * tactical tick). */
static void World_TactClearFrameScratch(void *pUser)
{
    ((PufoWorldState *)pUser)->bMapFrameScratch = 0;
}

/* ==========================================================================
 * The tactical walker over the live frame -- Tact_TickActiveUnit @ 0x54130.
 *
 * GameTick_OnTimer reaches the walker through pfnTickActiveUnit (its call at 0x275D8 is
 * gated on !(bPhaseActive && slot8Byte1) at 0x275CC, which GameTick_OnTimer itself applies).
 * The walker dispatches the queue to the three processors, and each processor reaches the
 * world through the contexts the original reads out of the Map block. The port hands every
 * one of those as a context argument, so the walker thunk rebuilds them from pTactMap and
 * tactEnv on each tick -- the same live-read semantics the original gets from its globals.
 * ========================================================================== */

/* The caller's Render_Frame thunk. The shell calls it with pW, the same user it hands the
 * LoadGame teardown's pfnRenderFrame. */
static void World_TactRenderFrame(PufoWorldState *pW)
{
    if (pW->pFrameHooks != NULL && pW->pFrameHooks->pfnRenderFrame != NULL) {
        pW->pFrameHooks->pfnRenderFrame(pW);
    }
}

/* StatusMsg_Push @ 0x2dda4's busy gate: unk_157180->bBusy, i.e. TactCtx272.bBusy. The repaint
 * flag is LOBYTE(apUiSlotsA[2]) = bMapSweepArmed (Map+0x68), and the repaint itself is the
 * frame hook the caller handed the shell. */
static bool World_TactStatusIsBusy(void *pUser)
{
    const PufoWorldState *pW = (const PufoWorldState *)pUser;

    return pW->pTactMap != NULL && pW->pTactMap->tact.bBusy != 0;
}

static bool World_TactStatusShouldRender(void *pUser)
{
    return ((const PufoWorldState *)pUser)->bMapSweepArmed != 0;
}

static void World_TactStatusRender(void *pUser)
{
    World_TactRenderFrame((PufoWorldState *)pUser);
}

/* TurnHooks @ 0x4d950. The cursor triple is the TactCursor the launch owns: nX @ 0x15128,
 * nY @ 0x1512C, and the LAYER stored in nFlags @ 0x15138 -- the byte MapView_FocusOnCell
 * sets to the cell layer + 1 and ProcessActionPhase saves/restores around a fire. */
static void World_TactSetPlayerInput(bool bEnabled, void *pUser)
{
    ((PufoWorldState *)pUser)->bMapPlayerInput = bEnabled ? 1 : 0;
}

static void World_TactReadCursor(i32 *pnX, i32 *pnY, i32 *pnLayer, void *pUser)
{
    const TactCursor *pCursor = &((PufoWorldState *)pUser)->tactEnv.cursor;

    *pnX = pCursor->nX;
    *pnY = pCursor->nY;
    *pnLayer = pCursor->nFlags;
}

static void World_TactSetCursor(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    TactCursor *pCursor = &((PufoWorldState *)pUser)->tactEnv.cursor;

    pCursor->nX = nX;
    pCursor->nY = nY;
    pCursor->nFlags = (u8)nLayer;
}

static void World_TactSelectThunk(Soldier *pUnit, void *pUser)
{
    Tact_SelectUnit(&((PufoWorldState *)pUser)->tactEnv.select, pUnit);
}

/* MapView_FocusSelected @ 0x16710: sample 17 when LOBYTE(apUiSlotsA[2]) is set, then centre
 * on the selected unit's cell. */
static void World_TactFocusSelectedThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GameMap *pMap = pW->pTactMap;

    if (pMap == NULL) {
        return;
    }
    MapView_FocusSelected(&pW->tactEnv.focus,
                          pMap->pSelUnit != NULL ? pMap->pSelUnit->pCell : NULL,
                          pW->bMapSweepArmed, &pW->tactEnv.cursor);
}

/* Unit_FaceToward @ 0x49bf0 enqueues a command-0 turn, so the seam carries the queue. */
static void World_TactFaceTowardThunk(Soldier *pUnit, i32 nX, i32 nY, void *pUser)
{
    Unit_FaceToward((TactState *)pUser, pUnit, nX, nY);
}

/* TurnHooks.pfnRequestFire stands for TactState_EnqueueFireBurst @ 0x4b080 plus the KickQueue
 * @ 0x4f334 after it (turn.h). The weapon/mode pick the original does before the enqueue is
 * Unit_SelectBestFireMode @ 0x478e4; the Turn boundary hands it to this hook.
 * Deviation: the burst's per-entry nArg is the per-mode enable the original reads from the
 * second action bank (Map+0x4E7C, Ui_FillActionCosts @ 0x26104); the shell does not own that
 * bank yet, so the mode's TU-cost table stands in with the enable at 0. */
static void World_TactRequestFireThunk(Soldier *pUnit, i32 nX, i32 nY, i32 nZ, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GameMap *pMap = pW->pTactMap;
    FireBurstSpec spec;

    if (pMap == NULL || pW->pTactState == NULL) {
        return;
    }
    if (!Unit_SelectBestFireMode(pUnit, &pW->sTactFireGate, &pMap->pSelWeapon,
                                 &pW->nMapActionMode, &pW->sTactActionCosts)) {
        return;
    }
    TactItem *pItem = pMap->pSelWeapon;
    if (pItem == NULL || pItem->pObj == NULL) {
        return;
    }

    memset(&spec, 0, sizeof(spec));
    spec.pUnit         = pUnit;
    spec.pWeapon       = pItem;
    spec.nMode         = pW->nMapActionMode;
    spec.nX            = nX;
    spec.nY            = nY;
    spec.nZ            = (u8)nZ;
    spec.nTuCost       = pW->sTactActionCosts.nTuCost[pW->nMapActionMode];
    /* FireBurstSpec.nArg is pCtxBase.nUnk40 @ Map+0x28 (0x1139E0, the cell the IDB names
     * Map_nBurstShots) at call time -- Ui_SetFireMode writes 1/1/3 into it (pufo_tactui.h).
     * The Part 50 note that this needed the anActionEnabled bank was a misattribution:
     * anActionEnabled[11] is the THROW's argument (enemyai.h:270), not the burst's. */
    spec.nArg          = pMap->nBurstShots;
    spec.uAmmoInWeapon = pItem->nAmmo;
    spec.bAmmoCap      = pItem->pObj->bAmmoCap;
    (void)TactState_EnqueueFireBurst(pW->pTactState, &spec, World_TactFaceTowardThunk,
                                     pW->pTactState);
    TactState_KickQueue(pW->pTactState);
}

/* The opportunity-fire list ran out: bUnk70103 on the queue (TactState + 0x111D7). The walker's
 * epilogue at 0x547B4 is what consumes it. */
static void World_TactEndOfOppFireThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pTactState != NULL) {
        pW->pTactState->bUnk70103 = 1;
    }
}

static void World_TactProcessPhaseThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pTactMap == NULL) {
        return;
    }
    Tact_ProcessActionPhase(&pW->pTactMap->tact, &pW->sTactTurnHooks, &pW->sTactPhaseSave);
}

/* TickHooks.pfnFocusOnCell = MapView_FocusOnCell @ 0x2c758 (the IDB names it
 * MapView_FocusOnUnit): recentre the camera on the cell and put the cursor on its layer + 1. */
static void World_TactFocusCellThunk(const MapCell *pCell, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pCell == NULL) {
        return;
    }
    MapView_FocusOnCell(&pW->tactEnv.focus, pCell, &pW->tactEnv.cursor);
}

/* SpotCtx.pfnFocusOnCell takes the cell unqualified; same MapView_FocusOnCell @ 0x2c758. */
static void World_TactSpotFocusThunk(MapCell *pCell, void *pUser)
{
    World_TactFocusCellThunk(pCell, pUser);
}

/* TickHooks.pfnRenderFrame = Render_Frame @ 0x22310. The walker reaches it from the busy
 * multi-entry move arm at 0x5422C. The shell's tactical paint is still the recorded gap (the
 * iso-draw env belongs to the banner unit), so the repaint goes to the caller's frame hook and
 * is a no-op while the shell draws nothing on a tactical screen. */
static void World_TactRenderThunk(void *pUser)
{
    World_TactRenderFrame((PufoWorldState *)pUser);
}

/* TickHooks.pfnSetCursorPos = MapCursor_SetPos @ 0x27014, over the launch's cursor triple. */
static void World_TactSetCursorPosThunk(i32 nX, i32 nY, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    MapCursor_SetPos(&pW->tactEnv.cursorPos, nX, nY, World_TactLookupCell, pW);
}

/* TickHooks.pfnGameTickStep = GameTick_Step @ 0x27324, the walker's re-run pass at 0x54814. */
static void World_TactGameTickStepThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pTactState == NULL) {
        return;
    }
    (void)GameTick_Step(pW->pTactState, World_TactLookupCell, pW);
}

/* ShotHooks.pfnAreaFire stands for the two writes at 0x2FA80/0x2FA94. The bUnk70115 store is
 * gated on pCtxBase.gap_2[0] == 0 (Map+0x02 = bActionLatch); the nUnk70024[4] clear is not. */
static void World_TactAreaFireThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pTactMap == NULL || pW->pTactState == NULL) {
        return;
    }
    if (pW->pTactMap->bActionLatch == 0) {
        pW->pTactState->bPhaseActive = 1;
    }
    pW->pTactState->nUnk70024Slot4 = 0;
}

/* Part 51 modal bank seams. World_ModalEndThunk stands for the zero-argument
 * UiModal_EndThunk @ 0x12254 that the original stores into modal bank slot 16 (the store at
 * 0x123E0, UiModal_EndThunk loaded from 0x123E4): Input_OnPointerUp's hover-column arm calls
 * the stored function after ending the modal (0x14B94). Zero-argument, so it reaches the bank
 * through g_pActiveWorld, the pattern the other stored callbacks use. */
static void World_ModalEndThunk(void)
{
    if (g_pActiveWorld != NULL) {
        UiModal_End(&g_pActiveWorld->sModal);
    }
}

/* InvDropHooks.pfnModal = the UiModal_Begin shape the drop modals use at 0x123E0:
 * Begin(width, text record, line count, "OK", UiModal_EndThunk, 0, 0) -- one button, and its
 * stored callback is the modal-end thunk. The labels come from Drop_Modal (aOk @ 0xA62D8). */
static void World_TactDropModalThunk(i32 nWidth, i32 nTextRec, i32 nLines, const char *szLabel,
                                     void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    UiModal_Begin(&pW->sModal, nWidth, nTextRec, nLines, (void *)szLabel, World_ModalEndThunk, NULL, NULL);
}

/* Part 52 enemy-phase thunks.

 * TurnHooks.pfnEnemyAiTick is the phase-3 arm of Tact_ProcessActionPhase @ 0x4d950. The tick
 * handles one enemy unit per call (selection gates at 0x4CC1C..0x4CC5C) and ends the enemy
 * turn at its tail (0x4D920) when nothing acted. The value-held parts of the ctx (the geometry
 * copy, the first-Xcom pointer the tail hands to Tact_EndEnemyTurn) are refreshed from the
 * live frame on every call. */
static void World_TactEnemyAiTickThunk(TactCtx272 *pTact, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pTactMap == NULL) {
        return;
    }
    pW->sTactEnemyAi.geom       = pW->pTactMap->geom;
    pW->sTactEnemyAi.pFirstXcom = pW->pTactMap->apXcomUnits[0];
    Tact_EnemyAiTick(pTact, &pW->sTactEnemyAi);
}

/* Game_RandU16 @ 0x3e59c (pufo_rand.h), the draw seam every AI roll runs through. */
static u32 World_TactRandU16Thunk(u32 nMax, void *pUser)
{
    (void)pUser;
    return (u32)Game_RandU16((u16)nMax);
}

/* Unit_PathToCell @ 0x2c2a8 over the shell's ctx. */
static bool World_TactPathToCellThunk(i32 nX, i32 nY, i32 nLayer, u8 bFlag, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    return Unit_PathToCell(nX, nY, nLayer, bFlag, &pW->sTactPath);
}

/* Ui_FillActionCosts @ 0x26104 (the enemy AI calls it before reading the cost table).
 * The input is read the way the original's call site reads it: the selected weapon's object
 * record bytes 0xB0..0xB5 plus the unit's stats. */
static void World_TactBuildCostInput(Soldier *pUnit, TactItem *pItem, ActionCostInput *pIn)
{
    memset(pIn, 0, sizeof(*pIn));
    pIn->uSortKey  = pItem->pObj->uSortKey;
    pIn->nCategory = pItem->pObj->nCategory;
    memcpy(pIn->abCostPctPacked, pItem->pObj->abCostPct, sizeof(pIn->abCostPctPacked));
    memcpy(pIn->abFlagByMode, pItem->pObj->abArmorTu, sizeof(pIn->abFlagByMode));
    pIn->auStat0     = pUnit->auStat[0];
    pIn->auStat4     = pUnit->auStat[4];
    pIn->auStat5     = pUnit->auStat[5];
    pIn->bFlags      = pUnit->bFlags;
    pIn->nUnitType   = pUnit->nUnitType;
    pIn->bOperative  = Unit_bIsOperative(pUnit);
    pIn->bWeaponUnk8 = pItem->bUnk8;
}

static void World_TactFillActionCostsThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GameMap *pMap = pW->pTactMap;
    ActionCostInput in;

    if (pMap == NULL) {
        return;
    }
    if (pMap->pSelUnit == NULL || pMap->pSelWeapon == NULL || pMap->pSelWeapon->pObj == NULL) {
        return;
    }
    World_TactBuildCostInput(pMap->pSelUnit, pMap->pSelWeapon, &in);
    Ui_FillActionCosts(&in, &pW->sTactUi);
}

/* EnemyAi_EnqueueRandomTurn's pair (pufo_enemyai.h:69): enqueue the turn command, then kick. */
static void World_TactEnqueueTurnKickThunk(Soldier *pUnit, u8 nFacing, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pTactState == NULL) {
        return;
    }
    TactState_EnqueueTurn(pW->pTactState, pUnit, nFacing);
    TactState_KickQueue(pW->pTactState);
}

/* Stands for the clip lookup inside TactItem_TryReloadClip (pufo_tactui.h:292):
 * pCtxBase.apObjects[id]->bAmmoCap. The port bounds-checks, the original does not. */
static u8 World_TactClipAmmoCapThunk(u16 nAmmoTypeId, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pAssets == NULL || nAmmoTypeId >= (u16)pW->pAssets->nObjectCount) {
        return 0;
    }
    return pW->pAssets->apObjects[nAmmoTypeId]->bAmmoCap;
}

/* Unit_UpdateVisibility @ 0x48580 over the move wiring's vis ctx -- the same ctx the move
 * FSM's seam uses. */
static void World_TactEnemyAiVisThunk(Soldier *pUnit, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    Map_UpdateVisibilityCb(pUnit, &pW->sTactMoveWiring);
}

/* The TEXTS-record status push (record index, not a string): the same text the move wiring
 * pushes by index, Map_PushStatusCb @ pufo_mapctx.h. */
static void World_TactEnemyAiPushTextThunk(i32 nRecIndex, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    Map_PushStatusCb(nRecIndex, &pW->sTactMoveWiring);
}

/* FireGate: aResearchTopics[g_abSlotTopicGate[uSortKey]].wGate4 != 0 (pufo_combat.h:218).
 * Out of table the original reads past the 156 bytes; the port refuses to guess and allows
 * the shot. */
static bool World_TactWeaponFireableThunk(i32 nSortKey, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (nSortKey < 0 || nSortKey >= PUFO_SLOT_TOPIC_GATE_COUNT) {
        return true;
    }
    return pW->aTopics[g_abSlotTopicGate[nSortKey]].wGate4 != 0;
}

/* FallActionCtx.pfnCheckBattleEnd = the Tact_CheckBattleEnd call sites inside the fall
 * processor at 0x53318 and 0x53bd4; KillHooks.pfnCheckBattleEnd = the one inside Tact_KillUnit
 * at 0x4EFc8. Both land on the shell's mission-end entry wired in Part 49. */
static void World_TactFallBattleEndThunk(u8 bAbort, void *pUser)
{
    (void)World_MissionEndCheck((PufoWorldState *)pUser, bAbort);
}

static void World_TactKillBattleEndThunk(void *pUser)
{
    (void)World_MissionEndCheck((PufoWorldState *)pUser, 0);
}

/* FallActionCtx.pfnTryOpportunityFire = Unit_bTryOpportunityFire @ 0x47c38, over the shell's
 * gate, selection cells and cost table. The result lands nowhere in the original. */
static void World_TactFallTryOpportunityThunk(Soldier *pTarget, Soldier *pShooter, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pTactMap == NULL) {
        return;
    }
    (void)Unit_bTryOpportunityFire(pTarget, pShooter, &pW->sTactFireGate, &pW->pTactMap->pSelWeapon,
                                   &pW->nMapActionMode, &pW->sTactActionCosts, &pW->tactEnv.vis);
}

/* FallActionCtx.pfnPushForcedText: the one message the fall processor builds itself (the
 * panicked unit's name + the alien-control record) pushed with bForce set. */
static void World_TactPushForcedThunk(const char *pszMsg, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW->pTactMap == NULL) {
        return;
    }
    (void)StatusMsg_Push(&pW->pTactMap->status, pszMsg, true, &pW->sTactStatusHooks);
}

/* The one live dispatch: rebuild the processor contexts from the Map block, then walk. */
static void World_TactTickActiveUnit(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GameMap *pMap = pW->pTactMap;
    TactState *pQ = pW->pTactState;
    MoveActionCtx move;
    FireActionCtx fire;
    FallActionCtx fall;
    CombatCtx combat;
    KillCtx kill;
    KillHooks killHooks;
    ProximityCtx prox;
    SideLists sideLists;
    TactTickCtx tick;

    if (pMap == NULL || pQ == NULL) {
        return;
    }
    /* The value-copied gates the contexts carry (the separate issue recorded for them): refresh
     * them from the live state every tick so a context built here reads this frame's truth. */
    pW->sTactRosters.nXcom  = pMap->nXcomCount;
    pW->sTactRosters.nEnemy = pMap->nEnemyCount;
    pW->sTactRosters.nCiv   = pMap->nCivilianCount;
    pW->sTactSpot.bBusy        = pMap->tact.bBusy != 0;
    pW->sTactSpot.bQueueActive = pQ->bQueueActive != 0;
    pW->sTactSpot.pSelUnit     = pMap->pSelUnit;
    pW->tactEnv.cursorPos.bBusy = pMap->tact.bBusy != 0;
    pW->tactEnv.cursorPos.bSelUnitLarge = pMap->pSelUnit != NULL && Unit_bIsLarge(pMap->pSelUnit);
    pW->sTactMoveWiring.visCtx  = pW->tactEnv.vis;

    Map_BuildMoveActionCtx(pMap, &pW->sTactLoft, &move);
    Map_WireMoveActionCtx(&move, &pW->sTactMoveWiring);
    /* 0x15138 has one live owner, the TactCursor the launch owns. The cast re-views the same
     * byte as the signed scratch the processors expect. */
    move.pnLayerScratch = (i8 *)&pW->tactEnv.cursor.nFlags;

    memset(&combat, 0, sizeof(combat));
    combat.pfnLookup     = World_TactLookupCell;
    combat.pUser         = pW;
    combat.pGeom         = &pMap->geom;
    combat.pState        = pQ;
    combat.pSelUnit      = pMap->pSelUnit;
    combat.pCtxWeapon    = (ObjectRecFull *)pMap->pFireArg6; /* 0x14050 nFireArg6 */
    combat.nShotOriginX  = pW->nMapShotOrigX;                /* 0x14054/0x14058 */
    combat.nShotOriginY  = pW->nMapShotOrigY;

    memset(&kill, 0, sizeof(kill));
    kill.pCombat          = &combat;
    kill.pRosters         = &pW->sTactRosters;
    kill.apTactItems      = pW->apTactItems;
    kill.pnTactItem       = &pW->nTactItemCount;
    kill.apObjects        = pW->pAssets->apObjects;
    kill.ppSelUnit        = &pMap->pSelUnit;
    kill.pnKillCount      = &pMap->nKillCount;  /* gap_TrajRest + 4 * 49940 */
    kill.pszTransformName = pMap->aTransformName;
    kill.bTickHold        = pW->bMapTickHold != 0;
    kill.bFrameScratch    = pW->bMapFrameScratch != 0;
    kill.bUiSlotArmed     = pW->bMapSweepArmed != 0;   /* LOBYTE(apUiSlotsA[2]) @ 0x68 */
    kill.pStatus          = &pMap->status;
    kill.pStatusHooks     = &pW->sTactStatusHooks;
    kill.pVisCtx          = &pW->tactEnv.vis;

    memset(&killHooks, 0, sizeof(killHooks));
    killHooks.pfnCheckBattleEnd = World_TactKillBattleEndThunk;  /* 0x4EFc8 */
    killHooks.pShotHooks        = &pW->sTactShotHooks;
    killHooks.pUser             = pW;

    memset(&prox, 0, sizeof(prox));
    prox.pCombat        = &combat;
    prox.pShotHooks     = &pW->sTactShotHooks;
    prox.pRosters       = &pW->sTactRosters;
    prox.pGeom          = &pMap->geom;
    prox.pfnLookupCell  = World_TactLookupCell;
    prox.pLookupUser    = pW;
    prox.paSteps        = pW->abTactVisSteps;            /* byte_AD720 */
    prox.pbBlastScanFlag = &pQ->gap_70112[0];            /* unk_157198 + 70112 */
    prox.pFocus         = &pW->tactEnv.focus;
    prox.pCursor        = &pW->tactEnv.cursor;
    prox.pbViewMoved    = &pW->bMapSlot8Byte1;           /* BYTE1(apUiSlotsA[8]) */

    sideLists.apXcom  = pMap->apXcomUnits;
    sideLists.nXcom   = pMap->nXcomCount;
    sideLists.apEnemy = pMap->apEnemyUnits;
    sideLists.nEnemy  = pMap->nEnemyCount;

    memset(&fire, 0, sizeof(fire));
    fire.geom               = pMap->geom;
    fire.pfnLookup          = World_TactLookupCell;
    fire.pUser              = pW;
    fire.pnLayerScratch     = (i8 *)&pW->tactEnv.cursor.nFlags;
    fire.pnMaxLayerClamp    = &pMap->nMaxLayerClamp;
    fire.pnAnimPhase        = &pMap->nAnimPhase;
    fire.pnShotCount        = &pMap->nShotCount;
    fire.pnBurstShots       = &pMap->nBurstShots;       /* 0x28 Map_nBurstShots */
    fire.pbFireStatus1      = &pMap->bFireStatus1;      /* 0x51 */
    fire.pbFireStatus9      = &pMap->bFireStatus9;      /* 0x59 */
    fire.pbFallDamagePending = &pW->bMapFire2;          /* 0x52 byte_113A0A */
    fire.pbThrowPending     = &pMap->bTrajHitStop;      /* 0x1406D */
    fire.pbFireHitResult    = &pMap->bFireHitResult;    /* 0x1406C */
    fire.pbEnemySpotted     = &pMap->bEnemySpotted;     /* 0x4E48 unk_118800 */
    fire.pnFireTickProgress = &pMap->nFireTickProgress;
    fire.pnFireSlot         = &pMap->nFireArg5;         /* 0x1404C nFireArg5 */
    fire.ppFireObject       = &pMap->pFireArg6;         /* 0x14050 nFireArg6 */
    fire.pAim               = &pMap->aim;
    fire.pThrowArc          = &pMap->throwArc;
    fire.pTactCtx           = &pMap->tact;
    fire.pVisCtx            = &pW->tactEnv.vis;
    fire.apXcomUnits        = pMap->apXcomUnits;
    fire.pnXcomCount        = &pMap->nXcomCount;
    fire.pCombat            = &combat;
    fire.pShotHooks         = &pW->sTactShotHooks;
    fire.pRosters           = &pW->sTactRosters;
    fire.apHits             = pMap->aHitWindow;         /* Map.gap_TrajRest @ 0x4EBC */
    fire.apHitsSave         = pMap->aHitWindowSave;
    fire.pShotOut           = &pW->sTactShotOut;
    fire.pFont              = pW->pFont;
    fire.pfnArmorTuWarn     = Map_ArmorTuWarnCb;
    fire.pfnPushStatus      = Map_PushStatusCb;
    fire.pfnUpdateVisibility = Map_UpdateVisibilityCb;
    fire.pCallbackUser      = &pW->sTactMoveWiring;

    memset(&fall, 0, sizeof(fall));
    fall.geom               = pMap->geom;
    fall.pfnLookup          = World_TactLookupCell;
    fall.pUser              = pW;
    fall.pnLayerScratch     = (i8 *)&pW->tactEnv.cursor.nFlags;
    fall.pnMaxLayerClamp    = &pMap->nMaxLayerClamp;
    fall.pnAnimPhase        = &pMap->nAnimPhase;
    fall.pnFireTickProgress = &pMap->nFireTickProgress;
    fall.pbEnemySpotted     = &pMap->bEnemySpotted;
    fall.pbFallDamagePending = &pW->bMapFire2;
    fall.pbThrowPending     = &pMap->bTrajHitStop;
    fall.pbTickHold         = &pW->bMapTickHold;
    fall.pnUiScreen         = &pW->nUiScreen;
    fall.ppSelWeapon        = &pMap->pSelWeapon;
    fall.ppThrowTarget      = &pMap->pThrowTarget;
    fall.ppLookCommitUnit   = &pMap->pLookCommitUnit;   /* 0x15088 unk_128A40 */
    fall.pbLookCommitFlag   = &pMap->bLookCommitFlag;   /* 0x1508C unk_128A44 */
    fall.pnCursorX          = &pW->tactEnv.cursor.nX;
    fall.pnCursorY          = &pW->tactEnv.cursor.nY;
    fall.pnMeleeCellX       = &pMap->nMeleeCellX;       /* unk_1279F8 @ 0x14040 */
    fall.pnMeleeCellY       = &pMap->nMeleeCellY;
    fall.pnMeleeCellLayer   = &pMap->nMeleeCellLayer;
    /* The throw flight's end is the SECOND sample slot of the multi-target arrays: unk_1278A8
     * = Map+0x13EF0 = adMtX[1], and the same pair offsets on Y and Z. */
    fall.pdThrowX           = &pMap->adMtX[1];
    fall.pdThrowY           = &pMap->adMtY[1];
    fall.pdThrowLayer       = &pMap->adMtZ[1];
    fall.apXcomUnits        = pMap->apXcomUnits;
    fall.apEnemyUnits       = pMap->apEnemyUnits;
    fall.pnXcomCount        = &pMap->nXcomCount;
    fall.pnEnemyCount       = &pMap->nEnemyCount;
    fall.pLists             = &sideLists;   /* counts by value: the recorded separate issue */
    fall.pTactCtx           = &pMap->tact;
    fall.pVisCtx            = &pW->tactEnv.vis;
    fall.pKillCtx           = &kill;
    fall.pKillHooks         = &killHooks;
    fall.pProximity         = &prox;
    fall.pRosters           = &pW->sTactRosters;
    fall.pStatus            = &pMap->status;
    fall.pStatusHooks       = &pW->sTactStatusHooks;
    fall.pSelectCtx         = &pW->tactEnv.select;
    fall.pCursorPos         = &pW->tactEnv.cursorPos;
    fall.pszTransformName   = pMap->aTransformName;
    fall.bLightEnabled      = pW->bMapLightEnabled != 0;
    fall.pLightmap          = pW->abTactLightmap;
    fall.pfnTryOpportunityFire = World_TactFallTryOpportunityThunk;
    fall.pfnCheckBattleEnd  = World_TactFallBattleEndThunk;   /* 0x53318 / 0x53bd4 */
    fall.pfnPushStatus      = Map_PushStatusCb;
    fall.pfnPushForcedText  = World_TactPushForcedThunk;
    fall.pszAlienControlSuffix = (const char *)pW->pAssets->pTextRecs
                                 + PUFO_TEXT_REC_STRIDE * PUFO_TXTREC_ALIEN_CONTROL;
    fall.pCallbackUser      = &pW->sTactMoveWiring;

    memset(&tick, 0, sizeof(tick));
    tick.pTactCtx      = &pMap->tact;
    tick.pHooks        = &pW->sTactTickHooks;
    tick.pMoveCtx      = &move;
    tick.pFireCtx      = &fire;
    tick.pFallCtx      = &fall;
    tick.pfnLookup     = World_TactLookupCell;
    tick.pLookupUser   = pW;
    tick.pGeom         = &pMap->geom;
    tick.pAim          = &pMap->aim;
    tick.ppSelUnit     = &pMap->pSelUnit;
    tick.apXcomUnits   = pMap->apXcomUnits;
    tick.pMapTactical  = pQ;
    tick.pbTickHold    = &pW->bMapTickHold;
    tick.pbFrameScratch = &pW->bMapFrameScratch;
    tick.pnUiSlot10    = &pW->nMapUiSlot10;

    (void)Tact_TickActiveUnit(pQ, &tick);
}

bool World_GameTickOnce(PufoWorldState *pW)
{
    GameTickCtx ctx;
    GameTickHooks hooks;

    memset(&ctx, 0, sizeof(ctx));
    memset(&hooks, 0, sizeof(hooks));

    ctx.bGameTickHold = pW->bMapTickHold != 0;     /* Map_bTickHold @ Map+0x5A, gate 1 (0x274FC) */
    ctx.bGeoscapeUi   = pW->bGeoscapeUi != 0;      /* Map_bGeoscapeUi @ Map+0x44, gate 2 (0x27508):
                                                    * cleared by the launch seam's FreeAll at 0x86D34 */
    ctx.bTactFlag     = pW->bTactFlag != 0;        /* Map+0x00, gate 4 (0x27548) */
    ctx.nUiSlot10     = pW->nMapUiSlot10;          /* apUiSlotsA[10] @ Map+0x88, gate 3 (0x2753C) */
    ctx.bSlot8Byte1   = pW->bMapSlot8Byte1 != 0;   /* Map+0x81, the TickActiveUnit gate (0x275CC) */
    ctx.bBusy         = pW->bTactLive && pW->pTactMap->tact.bBusy != 0;        /* TactCtx272.bBusy @ +0xF8 */
    ctx.bPhaseActive  = pW->bTactLive && pW->pTactState->bPhaseActive != 0;    /* TactState @ +0x111E3 */

    hooks.pfnClearFrameScratch = World_TactClearFrameScratch;
    hooks.pUser                = pW;
    /* Tact_TickActiveUnit @ 0x54130 runs over the live frame (Part 50): the thunk rebuilds the
     * three processor contexts from pTactMap/tactEnv and dispatches. The phase gate at 0x275CC
     * (skip when bPhaseActive && slot8Byte1) is GameTick_OnTimer's own, applied before this call. */
    hooks.pfnTickActiveUnit    = World_TactTickActiveUnit;

    return GameTick_OnTimer(&ctx,
                            pW->bTactLive ? &pW->pTactMap->status : NULL,
                            pW->bTactLive ? pW->pTactState : NULL,
                            &hooks, World_TactLookupCell, pW);
}

/* The three hand-overs a mission launch ends with, and why the shell takes them as seams.
 *
 *   GeoscapeUi_FreeAll @ 0x86d20 (0x83ABC, 0x84228). Its FreeWorld half releases the soldiers,
 *   bases, craft and strateg items World_Free releases too -- the original has ONE owner of them
 *   -- and the rest of it frees the font and shuts the audio down. It is the game-over teardown,
 *   not a screen close, so calling it from a mission launch would end the run.
 *   EndScreen_Init @ 0x388b0 (0x84230). It writes unk_157194, unk_B35D8, unk_1571C0 and
 *   unk_138E88, none of which the shell owns, and the shell has no ending flow to hand control to.
 *
 * Tact_InitMission @ 0x3c550 (0x83AC4) is no longer on this list: World_InitTactical runs it over the
 * state's tactical block, which is what the 0xC350 blocker in PLAN.md was waiting for.
 *
 * What the shell DOES keep is the state those paths set: Map_bTickHold and Map_bBattleEnded
 * (0x84238, 0x84240) are the original's own "this run is over" markers, and they are live here. */
static void World_MissionSeamFreeUi(i32 bFreeWorld, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GeoscapeRoster roster;

    memset(&roster, 0, sizeof(roster));
    roster.apSoldiers     = pW->apSoldiers;
    roster.nSoldierCount  = (u16)pW->nSoldierCount;
    roster.apBases        = pW->apBases;
    roster.nBaseCount     = pW->nBaseCount;
    roster.apCraft        = pW->apCraft;
    roster.nCraftCount    = pW->bCraftCount;
    roster.pStrategList   = &pW->strategList;

    /* 0x83ABC in the launch tail (a1 = 0: the UI only); the bankruptcy tails pass 1.
     * GeoscapeUi_FreeAll kills the D timer (0x86D28), CLEARS Map_bGeoscapeUi at 0x86D34 -- the
     * write that opens GameTick_OnTimer's tactical branch for the mission -- releases fontSys
     * (0x86D3C) and the root/pad/layer/control singletons, and with a1 non-zero the soldier,
     * base and craft records plus the site list. Tact_InitMission re-inits the font right after
     * (PufoFont_Init inside the bring-up), exactly as the original's 0x83AC4 follows this call. */
    GeoscapeUi_FreeAll(&pW->ui, &roster, (char)bFreeWorld);
}

static i32 World_MissionSeamInitMission(i32 nMode, void *pUser)
{
    /* 0x83AC4. The original's Tact_InitMission hands back 1; the Smap tail returns it (CloseUI's
     * case 20 does not test it, so the value only matters to the caller chain). */
    return World_InitTactical((PufoWorldState *)pUser, nMode) ? 1 : 0;
}

static void World_MissionSeamInitEnd(u8 bGoodEnd, void *pUser)
{
    (void)bGoodEnd;
    (void)pUser;
}

/* ==========================================================================
 * Tact UI control bring-up -- Tact_InitUiControls @ 0x3b498 from the shell (Part 54).
 *
 * Tact_InitMission reaches this right after the pad table is loaded (pufo_tactinit.c:712), and
 * installs g_aUiClickName's handlers on the caller's widget bank. The installed thunks are zero-arg
 * (UiControl.pfnClick takes nothing) and reach the live mission through g_pActiveWorld -- the same
 * route the geoscape controls take (pufo_world.c:58).
 *
 * Ownership, recorded as this Part's assumption: the bank stays the caller's, because UiBank_CtorAll
 * builds it at startup and StartUi_Shutdown frees its widgets 113..127 (pufo_menubtn.h:233); the
 * look control (the original's heap allocation at unk_1571E4), the 29 option pad bytes and the
 * input-side value model move onto the world. sTactUi is a STAGING copy: every installed handler
 * runs between SyncIn and SyncOut, which move the Map cells sTactUi models to and from their live
 * owners (the owners Parts 50..52 established). sTactInput is the live input state -- MenuBtn_Confirm
 * hands it straight to Input_OnConfirm @ 0x15f3c -- so it is synced only where its fields mirror a
 * cell owned elsewhere (the latch, the cursor triple, the multi-target block, pSelUnit/pSelWeapon).
 * The multi-target cells 0x04..0x24 have two shell views: the GameMap union (the walker's view) and
 * sTactInput (the input handlers' view); the bank handlers use the union and InputSync moves them.
 *
 * Handlers left NULL, each a recorded separate issue:
 *   57   Menu_ConfirmQuitDialog        -- needs the screen-8 escape seam wired for tact
 *   58   MenuBtn_ResizeWindow @ 0x19930 -- App_ResizeMainWindow is a caller-window call, not world state
 * ========================================================================== */

static PufoWorldState *World_TactLiveMission(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    return (pW != NULL && pW->pTactMap != NULL && pW->pTactState != NULL) ? pW : NULL;
}

/* Copy the Map cells sTactUi models in from their live owners, run the handler, copy them back.
 * The action-cost/enabled banks stay put: World_TactFillActionCostsThunk fills exactly sTactUi's
 * banks and the walker reads them back from there (Part 52, world.c:1898). */
static void World_TactUiSyncIn(PufoWorldState *pW)
{
    TactUiState *pUi = &pW->sTactUi;
    GameMap *pMap = pW->pTactMap;
    const PufoOptions *pOpt = pW->pOptions;
    i32 i;

    pUi->nUiScreen        = pW->nUiScreen;                       /* Map+0x3A */
    pUi->nActionMode      = pW->nMapActionMode;                  /* Map+0x4C */
    pUi->nUnk40           = pMap->nBurstShots;                   /* Map+0x28 */
    pUi->bActionLatch     = pMap->bActionLatch;                  /* gap_2[1] @ Map+0x02 */
    pUi->bUnk1508C        = pMap->bLookCommitFlag;               /* 0x1508C unk_128A44 */
    pUi->nLayerScratch    = pW->tactEnv.cursor.nFlags;           /* 0x15138 */
    pUi->nFireTgtX        = pMap->nMeleeCellX;                   /* 0x1279F8 */
    pUi->nFireTgtY        = pMap->nMeleeCellY;                   /* 0x1279FC */
    pUi->nFireTgtZ        = pMap->nMeleeCellLayer;               /* 0x127A00 */
    pUi->bDeployPanelOpen = pW->sTactInput.bDeployPanelOpen;     /* gap_86272[16] @ 0x15110 */
    pUi->bSlot80Low       = pW->sTactInput.bUiSlot8Byte0;        /* LOBYTE(apUiSlotsA[8]) @ 0x80 */
    pUi->bDrawIsoLayer    = pW->bMapDrawIsoLayer;                /* unk_138E68 @ 0x254B0 */
    if (pOpt != NULL) {
        pUi->bScrollByPen   = pOpt->bScrollByPen;                /* byte 93 @ 0x150D5 -- the option
                                                                    * block models the same bytes */
        pUi->bShowBorder    = pOpt->bShowBorder;
        pUi->bRunByDblClick = pOpt->bRunByDblClick;
        pUi->bTurnByPen     = pOpt->bTurnByPen;
        pUi->bMenuByPen     = pOpt->bMenuByPen;
    }
    for (i = 0; i < PUFO_INV_FILTER_PADS; ++i) {
        pUi->abTogglePad[i] = pW->sTactMenuPads.abPen[i];        /* 0xACF9A.. the same pad bytes the
                                                                    * options screen writes */
    }
}

static void World_TactUiSyncOut(PufoWorldState *pW)
{
    const TactUiState *pUi = &pW->sTactUi;
    GameMap *pMap = pW->pTactMap;
    PufoOptions *pOpt;
    i32 i;

    pW->nUiScreen          = pUi->nUiScreen;
    pW->nMapActionMode     = pUi->nActionMode;
    pMap->nBurstShots      = pUi->nUnk40;
    pMap->bActionLatch     = pUi->bActionLatch;
    pMap->bLookCommitFlag  = pUi->bUnk1508C;
    pW->tactEnv.cursor.nFlags = pUi->nLayerScratch;
    pMap->nMeleeCellX      = pUi->nFireTgtX;
    pMap->nMeleeCellY      = pUi->nFireTgtY;
    pMap->nMeleeCellLayer  = pUi->nFireTgtZ;
    pW->sTactInput.bDeployPanelOpen = pUi->bDeployPanelOpen;
    pW->sTactInput.bUiSlot8Byte0    = pUi->bSlot80Low;
    pW->bMapDrawIsoLayer   = pUi->bDrawIsoLayer;
    pOpt = (pW->pFrameHooks != NULL) ? pW->pFrameHooks->pTactOptions : NULL;
    if (pOpt != NULL) {
        pOpt->bScrollByPen   = pUi->bScrollByPen;
        pOpt->bShowBorder    = pUi->bShowBorder;
        pOpt->bRunByDblClick = pUi->bRunByDblClick;
        pOpt->bTurnByPen     = pUi->bTurnByPen;
        pOpt->bMenuByPen     = pUi->bMenuByPen;
    }
    for (i = 0; i < PUFO_INV_FILTER_PADS; ++i) {
        pW->sTactMenuPads.abPen[i] = pUi->abTogglePad[i];
    }
}

/* The input view of the shared cells: Input_OnConfirm works on sTactInput and its screen-0 arm
 * commits/abandons the multi-target selection, so those Map cells round-trip through the union the
 * walker also views. */
static void World_TactInputSyncIn(PufoWorldState *pW)
{
    TactInputState *pS = &pW->sTactInput;
    GameMap *pMap = pW->pTactMap;
    i32 i;

    pS->nUiScreen        = pW->nUiScreen;
    pS->nActionLatch     = pMap->bActionLatch;
    pS->nActionMode      = pW->nMapActionMode;      /* unk_113A04 @ Map+0x4C, the same cell the walker
                                                     * reads; TactInput_CanScroll gates on it */
    pS->nLayerScratch    = pW->tactEnv.cursor.nFlags;
    pS->nCursorX         = pW->tactEnv.cursor.nX;
    pS->nCursorY         = pW->tactEnv.cursor.nY;
    pS->bLookCommitFlag  = pMap->bLookCommitFlag;
    pS->pLookCommitUnit  = pMap->pLookCommitUnit;
    pS->pSelUnit         = pMap->pSelUnit;
    pS->pSelWeapon       = pMap->pSelWeapon;
    pS->nMultiTargetCount = pMap->nMultiTargetCount;
    for (i = 0; i < PUFO_MULTI_TARGET_MAX; ++i) {
        pS->apMultiTargets[i] = pMap->apMultiTargets[i];
    }
}

static void World_TactInputSyncOut(PufoWorldState *pW)
{
    const TactInputState *pS = &pW->sTactInput;
    GameMap *pMap = pW->pTactMap;
    i32 i;

    pW->nUiScreen         = pS->nUiScreen;
    pMap->bActionLatch    = pS->nActionLatch;
    pW->nMapActionMode    = pS->nActionMode;
    pW->tactEnv.cursor.nFlags = pS->nLayerScratch;
    pW->tactEnv.cursor.nX = pS->nCursorX;
    pW->tactEnv.cursor.nY = pS->nCursorY;
    pMap->bLookCommitFlag = pS->bLookCommitFlag;
    pMap->pLookCommitUnit = pS->pLookCommitUnit;
    pMap->pSelUnit        = pS->pSelUnit;
    pMap->pSelWeapon      = pS->pSelWeapon;
    pMap->nMultiTargetCount = pS->nMultiTargetCount;
    for (i = 0; i < PUFO_MULTI_TARGET_MAX; ++i) {
        pMap->apMultiTargets[i] = pS->apMultiTargets[i];
    }
}

/* ------------------------------------------------------------- shared hook plumbing */

static void World_TactBankSampleThunk(i32 nSample, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    (void)pW;

    if (g_pActiveWorld != NULL && g_pActiveWorld->pfnPlaySample != NULL) {
        g_pActiveWorld->pfnPlaySample(nSample, g_pActiveWorld->pSoundUser);
    }
}

static void World_TactBankSampleU32Thunk(u32 nSample, void *pUser)
{
    (void)pUser;
    World_TactBankSampleThunk((i32)nSample, g_pActiveWorld);
}

/* MapCursor_SetFlags @ 0x26f78 writes Map+0x15138; the staging byte mirrors it so SyncOut cannot
 * write a stale layer back over a handler that moved the cursor through the hook. */
static void World_TactSetCursorFlagsThunk(u8 nFlags, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    pW->tactEnv.cursor.nFlags = nFlags;
    pW->sTactUi.nLayerScratch = nFlags;
}

static TactUiHooks World_TactUiHooksFor(PufoWorldState *pW)
{
    TactUiHooks h;

    memset(&h, 0, sizeof(h));
    h.pfnPlaySample      = World_TactBankSampleThunk;
    h.pSelUnit           = pW->pTactMap->pSelUnit;              /* Map.pSelUnit @ 0x15334 */
    h.pfnStatusMsg       = World_TactEnemyAiPushTextThunk;       /* StatusMsg_Push @ 0x2dda4 */
    h.pfnSetCursorFlags  = World_TactSetCursorFlagsThunk;
    h.pUser              = pW;
    return h;
}

/* The original's gamma buttons tail-call Tact_LoadPalettes after writing the gamma dword; the hook
 * reads the value the handler just wrote into the option block. */
static void World_TactLoadPalettesThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    Tact_LoadPalettes((pW->pOptions != NULL) ? pW->pOptions->nGamma : pW->nMapGamma); /* 0x377ec */
}

static MenuHooks World_TactMenuHooksFor(PufoWorldState *pW)
{
    MenuHooks h;

    memset(&h, 0, sizeof(h));
    h.pfnPlaySample       = World_TactBankSampleThunk;
    h.pfnApplyMusicVolume = World_TactMusicVolHook;              /* Sound_ApplyMusicVolume @ 0x9e454 */
    h.pfnApplySfxVolume   = World_TactSfxVolHook;                /* Sound_ApplySfxVolume @ 0x9e4dc */
    h.pfnLoadPalettes     = World_TactLoadPalettesThunk;
    h.pUser               = pW;
    return h;
}

/* Menu_OpenSlotList injects SaveSlot_FormatList because it reads the save files; the caller's
   builder and its context come through WorldHooks. */
static void World_TactFormatSlotListThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    const WorldHooks *pHooks = pW->pFrameHooks;

    if (pHooks != NULL && pHooks->pfnFormatSlotList != NULL) {
        pHooks->pfnFormatSlotList(pHooks->pFormatSlotListUser);   /* 0x45904 */
    }
}

/* ------------------------------------------------------------- value-model wrappers */

typedef void (*TactUiHandler)(TactUiState *, const TactUiHooks *);

static void World_TactUiCall(TactUiHandler fn)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactUiHooks h;

    if (pW == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    fn(&pW->sTactUi, &h);
    World_TactUiSyncOut(pW);
}

static void World_TactUiArgCall(bool (*fn)(TactUiState *, i32, const TactUiHooks *), i32 nArg)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactUiHooks h;

    if (pW == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    fn(&pW->sTactUi, nArg, &h);
    World_TactUiSyncOut(pW);
}

/* The seven filter buttons: 21 -> 5, 24 -> ALL, 25 -> 0, 26 -> 3, 27 -> 4, 28 -> 1, 29 -> 2, the
 * original's own table order (pufo_uibank.c:210). */
static void World_TactFilterCall(i32 nFilter) { (void)World_TactUiArgCall(Ui_SetInvFilter, nFilter); }
static void World_TactRadioCall(i32 nRadio)   { (void)World_TactUiArgCall(Ui_SelectRadio, nRadio); }
static void World_TactToggleCall(i32 nWhich)  { (void)World_TactUiArgCall(Ui_ToggleOption, nWhich); }

static void World_ClickInvScrollUp(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    Ui_InvScrollUp(&pW->sTactUi);
    World_TactUiSyncOut(pW);
}

static void World_ClickInvScrollDown(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    Ui_InvScrollDown(&pW->sTactUi);
    World_TactUiSyncOut(pW);
}

/* The weapon pair the fire-mode tests read, from the live selected weapon. */
static bool World_TactFireWeapon(PufoWorldState *pW, FireModeWeapon *pOut)
{
    TactItem *pItem = pW->pTactMap->pSelWeapon;

    if (pItem == NULL || pItem->pObj == NULL) {
        return false;
    }
    pOut->nAmmo    = pItem->nAmmo;                    /* 0x06 */
    pOut->bAmmoCap = pItem->pObj->bAmmoCap;           /* 0xAE; 255 means unlimited */
    return true;
}

static void World_TactFireModeCall(i32 nMode)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactUiHooks h;
    FireModeWeapon w;

    if (pW == NULL || !World_TactFireWeapon(pW, &w)) {
        return;   /* the original would dereference the selected weapon */
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    (void)Ui_SetFireMode(&pW->sTactUi, nMode, &w, &h);
    World_TactUiSyncOut(pW);
}

static void World_TactTryFireCall(i32 nMode)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactUiHooks h;

    if (pW == NULL || pW->pTactMap->pSelUnit == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    (void)Ui_TryFireMode(&pW->sTactUi, nMode, pW->pTactMap->pSelUnit->uTu, &h);
    World_TactUiSyncOut(pW);
}

static void World_TactSubModeCall(i32 nSub)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactUiHooks h;

    if (pW == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    (void)Ui_SetSubMode(&pW->sTactUi, pW->pTactMap->pSelWeapon, pW->pTactMap->pSelUnit, nSub, &h);
    World_TactUiSyncOut(pW);
}

static void World_ClickConfirmUseItem(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactUiHooks h;
    TactItem *pWeapon;

    if (pW == NULL || pW->pTactMap->pSelUnit == NULL) {
        return;
    }
    pWeapon = pW->pTactMap->pSelWeapon;
    if (pWeapon == NULL || pWeapon->pObj == NULL) {
        return;   /* the original dereferences pObj->uSortKey */
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    (void)Ui_ConfirmUseItem(&pW->sTactUi, pW->pTactMap->pSelUnit, pWeapon,
                            pWeapon->pObj->uSortKey, &h);
    World_TactUiSyncOut(pW);
}

static void World_ClickMelee6(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactUiHooks h;

    if (pW == NULL || pW->pTactMap->pSelUnit == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    (void)Ui_ConfirmMeleeMode6(&pW->sTactUi, pW->pTactState, pW->pTactMap->pSelUnit,
                               pW->pTactMap->pSelWeapon, &pW->pTactMap->geom,
                               World_TactLookupCell, pW, &h);
    World_TactUiSyncOut(pW);
}

static void World_ClickConfirmSpecial9(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    Soldier *pUnit;
    TactUiHooks h;

    if (pW == NULL) {
        return;
    }
    pUnit = pW->pTactMap->pSelUnit;
    if (pUnit == NULL || pUnit->pCell == NULL) {
        return;   /* the target is the unit's OWN cell @ 0x18364 */
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    (void)Ui_ConfirmSpecial9(&pW->sTactUi, pW->pTactState, pUnit, pW->pTactMap->pSelWeapon,
                             pUnit->pCell, &h);
    World_TactUiSyncOut(pW);
}

static void World_ClickBeginMultiTarget(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactUiHooks h;
    FireModeWeapon w;

    if (pW == NULL || !World_TactFireWeapon(pW, &w)) {
        return;
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    (void)Ui_BeginMultiTarget(&pW->sTactUi, &w, &pW->pTactMap->nMultiTargetCount,
                              pW->tactEnv.cursor.nX, pW->tactEnv.cursor.nY,
                              World_TactSetCursorPosThunk, pW, &h);
    World_TactUiSyncOut(pW);
}

/* The look control's handler, TactAction_ConfirmFireBurst @ 0x18774: the burst aimed at the first
 * parked multi-target cell. pUser feeds Unit_FaceToward @ 0x49bf0, which wants the queue. */
static void World_TactLookClickThunk(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactItem *pWeapon;
    TactUiHooks h;

    if (pW == NULL) {
        return;
    }
    pWeapon = pW->pTactMap->pSelWeapon;
    if (pW->pTactMap->pSelUnit == NULL || pWeapon == NULL || pWeapon->pObj == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    h = World_TactUiHooksFor(pW);
    (void)Ui_ConfirmFireBurst(&pW->sTactUi, pW->pTactState, pW->pTactMap->pSelUnit, pWeapon,
                              pW->pTactMap->apMultiTargets, &pW->pTactMap->nMultiTargetCount,
                              (u16)pWeapon->nAmmo, pWeapon->pObj->bAmmoCap,
                              World_TactFaceTowardThunk, pW->pTactState, &h);
    World_TactUiSyncOut(pW);
}

/* ------------------------------------------------------------ the HUD nav widgets */

static TactHudCtx World_TactHudCtxFor(PufoWorldState *pW)
{
    TactHudCtx c;

    memset(&c, 0, sizeof(c));
    c.pSelUnit         = pW->pTactMap->pSelUnit;
    c.pQueue           = pW->pTactState;                    /* unk_157198 */
    c.geom             = pW->pTactMap->geom;                /* 0x1511B / 0x1511C */
    c.pfnLookup        = World_TactLookupCell;
    c.pLookupUser      = pW;
    c.pfnArmorTuWarn   = Map_ArmorTuWarnCb;                 /* the walker's own out-of-TU warning */
    c.pfnPushTextRecord = World_TactEnemyAiPushTextThunk;
    c.pfnPlaySample    = World_TactBankSampleThunk;
    c.pCallbackUser    = pW;
    return c;
}

static void World_TactHudCall(void (*fn)(const TactHudCtx *))
{
    PufoWorldState *pW = World_TactLiveMission();
    TactHudCtx c;

    if (pW == NULL || pW->pTactMap->pSelUnit == NULL) {
        return;
    }
    c = World_TactHudCtxFor(pW);
    fn(&c);
}

static void World_ClickUseSpecialItem(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    TactHudSpecialCtx c;

    if (pW == NULL || pW->pTactMap->pSelUnit == NULL) {
        return;
    }
    memset(&c, 0, sizeof(c));
    c.pMap             = pW->pTactMap;
    c.pUi              = &pW->sTactUi;
    c.pQueue           = pW->pTactState;
    c.pfnLookup        = World_TactLookupCell;
    c.pLookupUser      = pW;
    c.pfnPlaySample    = World_TactBankSampleThunk;
    c.pfnPushTextRecord = World_TactEnemyAiPushTextThunk;
    c.pCallbackUser    = pW;
    World_TactUiSyncIn(pW);
    TactHud_UseSpecialItem(&c);
    World_TactUiSyncOut(pW);
}

/* --------------------------------------------------------------- the roster pair */

static void World_TactRosterPrepare(PufoWorldState *pW, UnitRoster *pR, RosterHooks *pH)
{
    GameMap *pMap = pW->pTactMap;
    i32 i;

    for (i = 0; i < PUFO_ROSTER_MAX; ++i) {
        pR->apUnits[i] = pMap->apXcomUnits[i];
    }
    pR->nCount        = (i32)pMap->nXcomCount;
    pR->pSelected     = pMap->pSelUnit;
    pR->bActionLatch  = pMap->bActionLatch;

    memset(pH, 0, sizeof(*pH));
    pH->pfnPlaySample     = World_TactBankSampleThunk;
    pH->pfnSelectUnit     = World_TactSelectThunk;        /* Tact_SelectUnit @ 0x2c6c4: writes
                                                           * tactEnv.select.ppSelUnit = &pMap->pSelUnit */
    pH->pfnFocusSelected  = World_TactFocusSelectedThunk; /* MapView_FocusSelected @ 0x16710 */
    pH->pUi               = &pW->sTactUi;                 /* prev/next end with Inv_BuildVisibleList */
    pH->pUser             = pW;
}

static void World_TactRosterCall(i32 (*fn)(UnitRoster *, const RosterHooks *))
{
    PufoWorldState *pW = World_TactLiveMission();
    UnitRoster roster;
    RosterHooks hooks;

    if (pW == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    World_TactRosterPrepare(pW, &roster, &hooks);
    (void)fn(&roster, &hooks);
    pW->sTactUi.bActionLatch  = roster.bActionLatch;      /* the roster's latch write-back, before
                                                           * SyncOut republishes it to Map+0x02 */
    pW->sTactUi.nLayerScratch = pW->tactEnv.cursor.nFlags; /* the focus hook moved the cursor layer */
    World_TactUiSyncOut(pW);
}

static bool World_TactSlot8FilterThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    return pW->sTactInput.bUiSlot8Byte0 != 0;             /* LOBYTE(apUiSlotsA[8]) @ 0x80 */
}

static void World_TactRosterFilteredCall(i32 (*fn)(UnitRoster *, const RosterFilterHooks *))
{
    PufoWorldState *pW = World_TactLiveMission();
    UnitRoster roster;
    RosterFilterHooks hooks;

    if (pW == NULL) {
        return;
    }
    World_TactUiSyncIn(pW);
    memset(&hooks, 0, sizeof(hooks));
    World_TactRosterPrepare(pW, &roster, &hooks.base);
    hooks.pfnIsFilterActive = World_TactSlot8FilterThunk;
    hooks.pFilterUser       = pW;
    (void)fn(&roster, &hooks);
    pW->sTactUi.bActionLatch  = roster.bActionLatch;
    pW->sTactUi.nLayerScratch = pW->tactEnv.cursor.nFlags;
    World_TactUiSyncOut(pW);
}

/* ---------------------------------------------------------------- the camera pair */

static void World_ClickFocusSelected(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW != NULL) {
        World_TactFocusSelectedThunk(pW);
    }
}

static void World_ClickEnterLookMode(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    FocusCtx *pFocus;
    LookModeCtx c;

    if (pW == NULL) {
        return;
    }
    pFocus = &pW->tactEnv.focus;
    memset(&c, 0, sizeof(c));
    c.nUiScreen    = pW->nUiScreen;
    c.nMapTileW    = pFocus->nMapTileW;                   /* Map+0x30/0x32, held by tactEnv.focus */
    c.nMapTileH    = pFocus->nMapTileH;
    c.nCamScrollX  = pFocus->nCamScrollX;                 /* 0x15130 / 0x15134 */
    c.nCamScrollY  = pFocus->nCamScrollY;
    c.nTrajFront4  = (i8)pFocus->camOffset.bScratch40;    /* 0x4A40/0x4A41 read SIGNED */
    c.nTrajFront5  = (i8)pFocus->camOffset.bScratch41;
    c.nCamWorldOff0 = pW->sTactInput.anCamWorldX;         /* 0x4EAC / 0x4EB0 */
    c.nCamWorldOff1 = pW->sTactInput.anCamWorldY;
    c.nDimX        = pW->pTactMap->geom.nDimX;
    c.nDimY        = pW->pTactMap->geom.nDimY;
    c.camOffset    = pFocus->camOffset;
    MapView_EnterLookMode(&c, World_TactBankSampleU32Thunk, pW);
    pFocus->nCamScrollX = c.nCamScrollX;
    pFocus->nCamScrollY = c.nCamScrollY;
    pFocus->camOffset   = c.camOffset;
    pW->sTactInput.anCamWorldX = c.nCamWorldOff0;
    pW->sTactInput.anCamWorldY = c.nCamWorldOff1;
    pW->nUiScreen = c.nUiScreen;                           /* the callee writes screen 2 */
}

/* ---------------------------------------------------------------- the option screen */

static PufoOptions *World_TactLiveOptions(PufoWorldState *pW)
{
    return (pW->pFrameHooks != NULL) ? pW->pFrameHooks->pTactOptions : NULL;
}

static void World_TactSpeedCall(i32 nStep)
{
    PufoWorldState *pW = World_TactLiveMission();
    MenuHooks h;
    PufoOptions *pOpt;

    if (pW == NULL || (pOpt = World_TactLiveOptions(pW)) == NULL) {
        return;
    }
    h = World_TactMenuHooksFor(pW);
    (void)Menu_SetSpeed(pOpt, &pW->gameClock, &pW->sTactMenuPads, nStep, &h);
}

static void World_TactGammaCall(i32 nStep)
{
    PufoWorldState *pW = World_TactLiveMission();
    MenuHooks h;
    PufoOptions *pOpt;

    if (pW == NULL || (pOpt = World_TactLiveOptions(pW)) == NULL) {
        return;
    }
    h = World_TactMenuHooksFor(pW);
    (void)Menu_SetGamma(pOpt, &pW->sTactMenuPads, nStep, &h);
    pW->nMapGamma = pOpt->nGamma;                          /* Map_nGamma @ 0x15100 follows the byte
                                                            * the handler wrote @ 0x15100 */
}

static void World_TactMusicVolCall(i32 nStep)
{
    PufoWorldState *pW = World_TactLiveMission();
    MenuHooks h;
    PufoOptions *pOpt;

    if (pW == NULL || (pOpt = World_TactLiveOptions(pW)) == NULL) {
        return;
    }
    h = World_TactMenuHooksFor(pW);
    (void)Menu_SetMusicVolume(pOpt, &pW->sTactMenuPads, nStep, &h);
}

static void World_TactSfxVolCall(i32 nStep)
{
    PufoWorldState *pW = World_TactLiveMission();
    MenuHooks h;
    PufoOptions *pOpt;

    if (pW == NULL || (pOpt = World_TactLiveOptions(pW)) == NULL) {
        return;
    }
    h = World_TactMenuHooksFor(pW);
    (void)Menu_SetSfxVolume(pOpt, &pW->sTactMenuPads, nStep, &h);
}

static void World_ClickMusicMute(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    MenuHooks h;
    PufoOptions *pOpt;

    if (pW == NULL || (pOpt = World_TactLiveOptions(pW)) == NULL) {
        return;
    }
    h = World_TactMenuHooksFor(pW);
    (void)Menu_ToggleMusicMute(pOpt, &pW->sTactMenuPads.bMusicMute, &h);
}

static void World_ClickSfxMute(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    MenuHooks h;
    PufoOptions *pOpt;

    if (pW == NULL || (pOpt = World_TactLiveOptions(pW)) == NULL) {
        return;
    }
    h = World_TactMenuHooksFor(pW);
    (void)Menu_ToggleSfxMute(pOpt, &pW->sTactMenuPads.bSfxMute, &h);
}

static void World_ClickForceNoDrag(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    PufoOptions *pOpt;

    if (pW == NULL || (pOpt = World_TactLiveOptions(pW)) == NULL) {
        return;
    }
    (void)Menu_ToggleForceNoDrag(pOpt, &pW->sTactMenuPads.bForceNoDrag);
}

static void World_ClickOpenOptions(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    MenuHooks h;

    if (pW == NULL) {
        return;
    }
    h = World_TactMenuHooksFor(pW);
    Menu_SyncOptionPads(&pW->sTactMenuPads, pW->pOptions, &pW->nUiScreen, &h); /* 0x188e4, screen 12 */
}

static void World_TactSlotListCall(bool bSaveMode)
{
    PufoWorldState *pW = World_TactLiveMission();
    const WorldHooks *pHooks;
    MenuHooks h;

    if (pW == NULL || pW->pFrameHooks == NULL) {
        return;
    }
    pHooks = pW->pFrameHooks;
    if (pHooks->pnListMode == NULL) {
        return;                                            /* no *0x157188 cell to drive */
    }
    h = World_TactMenuHooksFor(pW);
    Menu_OpenSlotList(&pW->nUiScreen, pHooks->pnListMode, bSaveMode,
                      World_TactFormatSlotListThunk, &h);  /* 0x18864 load / 0x188a4 save */
}

static void World_TactSaveLoadCall(i32 nSlot)
{
    PufoWorldState *pW = World_TactLiveMission();
    const WorldHooks *pHooks;

    if (pW == NULL || pW->pFrameHooks == NULL) {
        return;
    }
    pHooks = pW->pFrameHooks;
    if (pHooks->pSaveLoadHooks == NULL || pHooks->pnListMode == NULL) {
        return;
    }
    (void)SaveLoad_Slot(nSlot, (i32)pW->nUiScreen, pHooks->pnListMode, pHooks->pSaveLoadHooks);
}

/* ---------------------------------------------------------------- the button family */

static bool World_TactButtonPrep(PufoWorldState *pW, UiButtonCtx *pCtx, TactInputWorld *pWorld)
{
    const WorldHooks *pHooks = pW->pFrameHooks;

    if (pHooks == NULL || pHooks->pTactInputHooks == NULL) {
        return false;
    }
    pWorld->pQueue       = pW->pTactState;
    pWorld->pfnLookup    = World_TactLookupCell;
    pWorld->pLookupUser  = pW;
    pCtx->pState         = &pW->sTactInput;                /* the live input state: MenuBtn_Confirm
                                                            * hands it to Input_OnConfirm @ 0x15f3c */
    pCtx->pHooks         = pHooks->pTactInputHooks;
    pCtx->pWorld         = pWorld;                         /* only MenuBtn_Secondary needs it */
    pCtx->pbActionFlag   = pHooks->pbBankActionFlag;       /* UiBank_bActionFlag */
    pCtx->pbEscapeFlag   = (pHooks->pBank != NULL) ? &pHooks->pBank->bEscapeFlag : NULL; /* byte_AD712 */
    return true;
}

static void World_TactButtonCall(void (*fn)(const UiButtonCtx *))
{
    PufoWorldState *pW = World_TactLiveMission();
    UiButtonCtx ctx;
    TactInputWorld world;

    if (pW == NULL || !World_TactButtonPrep(pW, &ctx, &world)) {
        return;
    }
    World_TactInputSyncIn(pW);
    fn(&ctx);
    World_TactInputSyncOut(pW);
}

/* Confirm0..5 are byte-identical with sample 16; ConfirmSnd15 plays 15 (pufo_menubtn.h:96). */
static void World_TactConfirmCall(i32 nSample)
{
    PufoWorldState *pW = World_TactLiveMission();
    UiButtonCtx ctx;
    TactInputWorld world;

    if (pW == NULL || !World_TactButtonPrep(pW, &ctx, &world)) {
        return;
    }
    World_TactInputSyncIn(pW);
    MenuBtn_Confirm(&ctx, nSample);
    World_TactInputSyncOut(pW);
}

static void World_TactDismissCall(i32 (*fn)(const UiButtonCtx *))
{
    PufoWorldState *pW = World_TactLiveMission();
    UiButtonCtx ctx;
    TactInputWorld world;

    if (pW == NULL || !World_TactButtonPrep(pW, &ctx, &world)) {
        return;
    }
    World_TactInputSyncIn(pW);
    (void)fn(&ctx);
    World_TactInputSyncOut(pW);
}

/* ---------------------------------------------------- the one-line click bindings */

static void World_ClickCycleNext(void)          { World_TactRosterCall(Roster_CycleNext); }
static void World_ClickSelectPrev(void)         { World_TactRosterCall(Roster_SelectPrev); }
static void World_ClickSelectNext(void)         { World_TactRosterCall(Roster_SelectNext); }
static void World_ClickSelectPrevFiltered(void) { World_TactRosterFilteredCall(Roster_SelectPrevFiltered); }
static void World_ClickSelectNextFiltered(void) { World_TactRosterFilteredCall(Roster_SelectNextFiltered); }

static void World_TactHudClickUp(void)   { World_TactHudCall(TactHud_ClimbUp); }
static void World_TactHudClickDown(void) { World_TactHudCall(TactHud_ClimbDown); }
static void World_ClickToggleKneel(void) { World_TactHudCall(TactHud_ToggleKneel); }

static void World_ClickLayerUp(void)   { World_TactUiCall(Ui_LayerUp); }
static void World_ClickLayerDown(void) { World_TactUiCall(Ui_LayerDown); }
static void World_ClickIncLayer(void)  { World_TactUiCall(Ui_IncLayer); }
static void World_ClickDecLayer(void)  { World_TactUiCall(Ui_DecLayer); }

static void World_ClickOpenScreen7(void)   { World_TactUiCall(Ui_OpenScreen7); }
static void World_ClickOpenScreen15(void)  { World_TactUiCall(Ui_OpenScreen15); }
static void World_ClickOpenCommand(void)   { World_TactUiCall(Ui_OpenCommandScreen); }
static void World_ClickOpenCommandFlagged(void) { World_TactUiCall(Ui_OpenCommandScreenFlagged); }
static void World_ClickMode11(void)        { World_TactUiCall(Ui_SetMode11); }
static void World_ClickSamples1614Clear(void) { World_TactUiCall(Ui_Samples16_14_ClearScreen); }
static void World_ClickScreen7Samples(void){ World_TactUiCall(Ui_Screen7_Samples16_14); }
static void World_ClickDeployOpen(void)    { World_TactUiCall(Ui_Samples16_7_SetDeployFlag); }
static void World_ClickDeployClose(void)   { World_TactUiCall(Ui_Samples16_7_ClearDeployFlag); }

static void World_ClickFilter5(void)   { World_TactFilterCall(5); }
static void World_ClickFilterAll(void) { World_TactFilterCall(PUFO_INV_FILTER_ALL); }
static void World_ClickFilter0(void)   { World_TactFilterCall(0); }
static void World_ClickFilter3(void)   { World_TactFilterCall(3); }
static void World_ClickFilter4(void)   { World_TactFilterCall(4); }
static void World_ClickFilter1(void)   { World_TactFilterCall(1); }
static void World_ClickFilter2(void)   { World_TactFilterCall(2); }

static void World_ClickRadio0(void) { World_TactRadioCall(0); }
static void World_ClickRadio1(void) { World_TactRadioCall(1); }
static void World_ClickRadio2(void) { World_TactRadioCall(2); }
static void World_ClickRadio3(void) { World_TactRadioCall(3); }

static void World_ClickFireAuto(void)  { World_TactFireModeCall(PUFO_ACTION_MODE_AUTO); }
static void World_ClickFireSnap(void)  { World_TactFireModeCall(PUFO_ACTION_MODE_SNAP); }
static void World_ClickFireAimed(void) { World_TactFireModeCall(PUFO_ACTION_MODE_AIMED); }

static void World_ClickTryFire4(void)  { World_TactTryFireCall(4); }
static void World_ClickTryFire5(void)  { World_TactTryFireCall(5); }
static void World_ClickTryFire10(void) { World_TactTryFireCall(10); }

static void World_ClickSubMode0(void) { World_TactSubModeCall(0); }
static void World_ClickSubMode1(void) { World_TactSubModeCall(1); }
static void World_ClickSubMode2(void) { World_TactSubModeCall(2); }
static void World_ClickSubMode3(void) { World_TactSubModeCall(3); }
static void World_ClickSubMode4(void) { World_TactSubModeCall(4); }
static void World_ClickSubMode5(void) { World_TactSubModeCall(5); }
static void World_ClickSubMode6(void) { World_TactSubModeCall(6); }
static void World_ClickSubMode7(void) { World_TactSubModeCall(7); }
static void World_ClickSubMode8(void) { World_TactSubModeCall(8); }

static void World_ClickOpenLoadSlots(void) { World_TactSlotListCall(false); }
static void World_ClickOpenSaveSlots(void) { World_TactSlotListCall(true); }

/* TactHud_OpenInventory @ 0x16ef4: the inventory the Xcom-team branch of Init_Tactical opens at
 * 0x3D0B8 (the hook at tactinit.c:560, right after Tact_SelectUnit picks apXcomUnits[0]), and the
 * same composition the inventory pad (bank widget 4) carries. The ctx is rebuilt live per call
 * because the callee's `invOpen` env copies pSelUnit/nXcomCount before that selection lands. */
static bool World_TactOpenInventoryBody(PufoWorldState *pW)
{
    GameMap *pMap = pW->pTactMap;
    TactHudInvOpenCtx c;
    bool bOpen;

    if (pMap == NULL || pW->pTactState == NULL) {
        return false;
    }
    memset(&c, 0, sizeof(c));
    World_TactUiSyncIn(pW);
    c.pUi           = &pW->sTactUi;
    c.pSelUnit      = pMap->pSelUnit;
    c.pCursor       = &pW->tactEnv.cursor;
    c.geom          = pMap->geom;
    c.pfnLookup     = World_TactLookupCell;
    c.pLookupUser   = pW;
    c.nXcomCount    = pMap->nXcomCount;                          /* 0x1511F */
    c.bSweepGate    = pW->bMapSweepArmed;                        /* LOBYTE(apUiSlotsA[2]) @ 0x68 */
    c.bSelUnitLarge = pMap->pSelUnit != NULL && Unit_bIsLarge(pMap->pSelUnit);
    c.pfnPlaySample = World_TactBankSampleThunk;
    c.pInvStat      = &pW->tactInvHooks;                         /* Unit_RecalcInventoryStat's seam */
    c.pCallbackUser = pW;
    bOpen = TactHud_OpenInventory(&c);
    World_TactUiSyncOut(pW);
    return bOpen;
}

static bool World_TactOpenInventoryThunk(void *pUser)
{
    return World_TactOpenInventoryBody((PufoWorldState *)pUser);
}

static void World_ClickOpenInventory(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW != NULL) {
        (void)World_TactOpenInventoryBody(pW);
    }
}

/* ==========================================================================
 * Part 56: the EndTurn pad runs the whole enemy-turn bracket.
 * MenuBtn_EndTurn @ 0x16494 plays 17 and calls Tact_BeginEnemyTurn @ 0x4ca18 --
 * the only original caller (pTactCtx272 @ 0x157180 is the frame's TactCtx272, the
 * port's pMap->tact). Begin sets the enemy-turn byte (0x4CA30) / busy / phase 3
 * and disables input, runs BeforeEnemyStep (0x4b9b8) over the shell-owned
 * EnemyStepCtx, and hands the phase to Tact_ProcessActionPhase, whose phase-3 arm
 * is the Part 52 enemy AI tick; its tail (0x4D920) runs Tact_EndEnemyTurn ->
 * AfterEnemyStep (0x4c4a0), clears the byte (0x4CB88) and restores the selection.
 * Render_Frame's slot stays NULL: the iso renderer is a recorded separate issue.
 * ========================================================================== */

/* EnemyStepCtx reads the side counts through i32*; GameMap owns 0x128AD7/0x128ADC as
 * byte cells. The step thunks copy in, let the callee bump the mirrors (the hidden-
 * reserve spawns), and truncate back. */
static void World_TactStepSyncIn(PufoWorldState *pW)
{
    pW->nStepXcomCount  = pW->pTactMap->nXcomCount;
    pW->nStepEnemyCount = pW->pTactMap->nEnemyCount;
}

static void World_TactStepSyncOut(PufoWorldState *pW)
{
    pW->pTactMap->nXcomCount  = (u8)pW->nStepXcomCount;
    pW->pTactMap->nEnemyCount = (u8)pW->nStepEnemyCount;
}

/* EnemyStepCtx.pfnTickProximity: MapCell_TickProximityItems @ 0x4b724 over the shell's
 * proximity ctx -- the same build the walker tick assembles every frame. */
static void World_TactStepProxThunk(MapCell *pCell, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    MapCell_TickProximityItems(&pW->sTactProx, pCell);
}

/* EnemyStepCtx.pfnRefreshCells: Map_RefreshCellMeta @ 0x2c7f4, the light rebuild the
 * tactinit launch also runs (0x3C...). Gate byte 0x254B1, lightmap unk_156AB8. */
static void World_TactStepRefreshCellsThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    (void)Map_RefreshCellMeta(World_TactLookupCell, pW, &pW->pTactMap->geom,
                              pW->bMapLightEnabled != 0, pW->abTactLightmap);
}

/* EnemyStepCtx.pfnRefreshVisibility: UnitList_ForEachRefresh @ 0x22b54 over the live
 * roster view (counts copied fresh, as the struct stores them by value). */
static void World_TactStepRefreshVisThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    pW->sTactRosters.nXcom  = pW->pTactMap->nXcomCount;
    pW->sTactRosters.nEnemy = pW->pTactMap->nEnemyCount;
    pW->sTactRosters.nCiv   = pW->pTactMap->nCivilianCount;
    UnitList_ForEachRefresh(&pW->sTactRosters, &pW->tactEnv.vis);
}

static void World_TactFillStepCtx(PufoWorldState *pW)
{
    GameMap *pMap = pW->pTactMap;
    EnemyStepCtx *c = &pW->sTactStep;

    memset(c, 0, sizeof(*c));
    c->pfnTickProximity     = World_TactStepProxThunk;
    c->pfnRefreshCells      = World_TactStepRefreshCellsThunk;
    c->pfnRefreshVisibility = World_TactStepRefreshVisThunk;
    c->pfnUpdateVisibility  = World_TactEnemyAiVisThunk;
    c->pfnRandU16           = World_TactRandU16Thunk;
    c->pUser                = pW;
    c->pfnLookup            = World_TactLookupCell;
    c->pGeom                = &pMap->geom;
    c->pQueue               = pW->pTactState;
    c->pSelect              = &pW->tactEnv.select;
    c->pStatus              = &pMap->status;
    c->pStatusHooks         = &pW->sTactStatusHooks;
    c->pTextRecs            = (const char *)pW->pAssets->pTextRecs; /* aTextRecs @ 0xB38D0 */
    /* Deviation: the fire tick's weapon is the Map+0x1B0 slot-42 scratch (Map_apObjects42,
     * read at 0x4BFF0). The slot has no port owner: zeroed BSS on a new game, and the save
     * blob leaves it unmodeled, so the port runs with NULL. */
    c->pFireWeapon          = NULL;
    c->pActCtx              = &pW->sTactEnemyAiAct;
    c->pRmp                 = &pW->sTactRmp;
    c->apXcom               = pMap->apXcomUnits;
    c->pnXcom               = &pW->nStepXcomCount;
    c->apEnemy              = pMap->apEnemyUnits;
    c->pnEnemy              = &pW->nStepEnemyCount;
    c->apCiv                = pMap->apCivilians;
    c->pnCiv                = &pMap->nCivilianCount;
    c->ppHidden             = pW->apTactHidden;                   /* apHiddenSoldiers @ 0x138DF0 */
    c->pnReserveCount       = &pW->nMapEnemyReserveCount;         /* 0x128AFC */
    c->pbMissionType        = &pMap->nMissionType;                /* 0x128AD2 */
    c->ppSelUnit            = &pMap->pSelUnit;

    /* Combat + proximity ctxs for the proximity seam, mirroring World_TactTickActiveUnit. */
    memset(&pW->sTactCombat, 0, sizeof(pW->sTactCombat));
    pW->sTactCombat.pfnLookup    = World_TactLookupCell;
    pW->sTactCombat.pUser        = pW;
    pW->sTactCombat.pGeom        = &pMap->geom;
    pW->sTactCombat.pState       = pW->pTactState;
    pW->sTactCombat.pSelUnit     = pMap->pSelUnit;
    pW->sTactCombat.pCtxWeapon   = (ObjectRecFull *)pMap->pFireArg6; /* 0x14050 nFireArg6 */
    pW->sTactCombat.nShotOriginX = pW->nMapShotOrigX;                /* 0x14054/0x14058 */
    pW->sTactCombat.nShotOriginY = pW->nMapShotOrigY;

    memset(&pW->sTactProx, 0, sizeof(pW->sTactProx));
    pW->sTactProx.pCombat         = &pW->sTactCombat;
    pW->sTactProx.pShotHooks      = &pW->sTactShotHooks;
    pW->sTactProx.pRosters        = &pW->sTactRosters;
    pW->sTactProx.pGeom           = &pMap->geom;
    pW->sTactProx.pfnLookupCell   = World_TactLookupCell;
    pW->sTactProx.pLookupUser     = pW;
    pW->sTactProx.paSteps         = pW->abTactVisSteps;             /* byte_AD720 */
    pW->sTactProx.pbBlastScanFlag = &pW->pTactState->gap_70112[0];  /* unk_157198 + 70112 */
    pW->sTactProx.pFocus          = &pW->tactEnv.focus;
    pW->sTactProx.pCursor         = &pW->tactEnv.cursor;
    pW->sTactProx.pbViewMoved     = &pW->bMapSlot8Byte1;            /* BYTE1(apUiSlotsA[8]) */
}

static void World_TactBeforeEnemyStepThunk(TactCtx272 *pTact, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    World_TactStepSyncIn(pW);
    World_TactFillStepCtx(pW);
    BeforeEnemyStep(pTact, &pW->sTactStep);
    World_TactStepSyncOut(pW);
}

static void World_TactAfterEnemyStepThunk(TactCtx272 *pTact, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    World_TactStepSyncIn(pW);
    World_TactFillStepCtx(pW);
    AfterEnemyStep(pTact, &pW->sTactStep);
    World_TactStepSyncOut(pW);
}

static void World_ClickEndTurn(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    GameMap *pMap;
    TurnUnitLists lists;

    if (pW == NULL) {
        return;
    }
    pMap = pW->pTactMap;
    World_TactBankSampleThunk(17, pW); /* MenuBtn_EndTurn 0x164A0 */
    lists.apEnemy    = pMap->apEnemyUnits;
    lists.nEnemy     = pMap->nEnemyCount;
    lists.apCivilian = pMap->apCivilians;
    lists.nCivilian  = pMap->nCivilianCount;
    lists.apXcom     = pMap->apXcomUnits;
    lists.nXcom      = pMap->nXcomCount;
    Tact_BeginEnemyTurn(&pMap->tact, &pW->sTactTurnHooks, &lists, pMap->pSelUnit,
                        &pW->bMapEnemyTurnActive,
                        NULL, /* Render_Frame (0x22310) -- the iso renderer seam, a recorded separate issue */
                        World_TactBeforeEnemyStepThunk);
}

/* ---------------------------------------------------------------------------
 * Part 57: the hand-menu pads (bank widgets 17/18 = Tact_OpenLeftHandMenu @ 0x17030
 * and Tact_OpenRightHandMenu @ 0x17148 -- same body, left/right hand). The game
 * half is ported as Ui_OpenHandMenu (pufo_tactui.c); this is its first live
 * caller. Order per the ARM: sample 17, bail on an armed latch, STORE the hand
 * slot into Map.pSelWeapon (Map+0x48) before any gate, bail on an empty slot,
 * research gate aResearchTopics[g_abSlotTopicGate[uSortKey]].bUnlocked, the
 * Soldier+0x1A6 (bPsiActive) gate for sort key 24, then fill costs and scan the
 * twelve modes for one offered AND affordable -> mode 12, screen 3; none found
 * pushes text record 1. Deviation: the original reads through a NULL
 * Map_pSelUnit at 0x1705C; the port bails instead.
 * --------------------------------------------------------------------------- */

/* The research gate the game half injects. */
static bool World_TactWeaponHasMenuThunk(u16 uSortKey, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    return pW->aTopics[g_abSlotTopicGate[uSortKey]].bUnlocked != 0;   /* aResearchTopics @ 0x15EF08 */
}

static bool World_TactOpenHandMenuBody(PufoWorldState *pW, bool bRight)
{
    GameMap *pMap = pW->pTactMap;
    Soldier *pUnit;
    TactItem *pItem;
    ActionCostInput in;
    HandMenuCtx c;
    TactUiHooks h;
    bool bOpen;

    if (pMap == NULL) {
        return false;
    }
    pUnit = pMap->pSelUnit;
    pItem = (pUnit != NULL) ? (bRight ? pUnit->pHandR : pUnit->pHandL) : NULL;

    if (pUnit == NULL || pItem == NULL || pItem->pObj == NULL) {
        World_TactBankSampleThunk(17, pW); /* 0x1703C: the sample plays on every path */
        return false;
    }
    if (pMap->bActionLatch != 0) {
        World_TactBankSampleThunk(17, pW); /* the latch bail still played 17 first */
        return false;
    }

    /* 0x17060: the hand slot becomes the selected weapon BEFORE the gates. */
    pMap->pSelWeapon = pItem;

    World_TactUiSyncIn(pW);
    World_TactBuildCostInput(pUnit, pItem, &in);
    c.pCost         = &in;
    c.bActionLatch  = pMap->bActionLatch;
    c.uTu           = pUnit->uTu;
    c.uSortKey      = pItem->pObj->uSortKey;
    c.bByte1A6      = pUnit->bPsiActive;            /* 0x1A6 @ 0x170A8/0x170B0 */
    h = World_TactUiHooksFor(pW);
    bOpen = Ui_OpenHandMenu(&pW->sTactUi, &c, World_TactWeaponHasMenuThunk, &h, pW);
    World_TactUiSyncOut(pW);
    return bOpen;
}

static void World_ClickOpenLeftHand(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW != NULL) {
        (void)World_TactOpenHandMenuBody(pW, false);
    }
}

static void World_ClickOpenRightHand(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW != NULL) {
        (void)World_TactOpenHandMenuBody(pW, true);
    }
}

/* ------------------------------------------------- deploy confirm (Part 58)
 *
 * Widget 56 -> Map_BuildDeployConfirmYesNo @ 0x18a68, ported in pufo_tactctl.c. The shell owns the
 * two message buffers the original keeps in the scratch pair unk_E90D9 / unk_E9273, the text-record
 * reader for hook pfnTextRecord, and the modal bank the opener fills. UiModal_Begin stores the two
 * button callbacks into sModal slots 16/18 (Part 51): the original stores j_Tact_CheckBattleEnd
 * @ 0x18a44 -- Tact_CheckBattleEnd(map, 1), which World_MissionEndCheck runs on the live frame --
 * and Map_ClearUiScreen @ 0x18a54, a direct Map_nUiScreen = 0 write. Running the stored callbacks
 * is the tact pointer dispatch, still the recorded separate issue. */

static const char *World_TactTextRecordThunk(i32 nRecord, void *pUser)
{
    const PufoWorldState *pW = (const PufoWorldState *)pUser;

    if (pW->pAssets == NULL || pW->pAssets->pTextRecs == NULL) {
        return NULL;                               /* Deploy_Text reads NULL as an empty fragment */
    }
    return (const char *)pW->pAssets->pTextRecs + (size_t)nRecord * PUFO_TEXT_REC_STRIDE;
}

static void World_DeployYesThunk(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW != NULL) {
        (void)World_MissionEndCheck(pW, 1);        /* j_Tact_CheckBattleEnd @ 0x18a44 */
    }
}

static void World_DeployNoThunk(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW != NULL) {
        pW->nUiScreen = 0;                         /* Map_ClearUiScreen @ 0x18a54 */
    }
}

static void World_DeployModalThunk(i32 nX, i32 nY, i32 nCount, const char *szYes, const char *szNo,
                                   void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    UiModal_Begin(&pW->sModal, nX, nY, nCount, (void *)szYes, World_DeployYesThunk,
                  (void *)szNo, World_DeployNoThunk);
}

static void World_ClickDeployConfirm(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    DeployConfirmHooks h;
    DeployConfirmCtx c;

    if (pW == NULL) {
        return;
    }
    h.pfnPlaySample = World_TactBankSampleThunk;   /* Sound 16 (0x18D70) */
    h.pfnModal      = World_DeployModalThunk;
    h.pfnTextRecord = World_TactTextRecordThunk;
    h.pUser         = pW;

    c.Count.Bounds.bUseCraft    = true;            /* Tact_CountDeployUnits forces it (0x18A84) */
    c.Count.Bounds.nMissionType = pW->pTactMap->nMissionType;
    c.Count.Bounds.nMapDimX     = pW->pTactMap->geom.nDimX;
    c.Count.Bounds.nMapDimY     = pW->pTactMap->geom.nDimY;
    c.Count.Bounds.nCraftMapX   = pW->bMapCraftMapX;
    c.Count.Bounds.nCraftMapY   = pW->bMapCraftMapY;
    c.Count.Bounds.bCraftType   = (pW->nMapCraftIndex < PUFO_FAC_BASE_COUNT
                                   && pW->apCraft[pW->nMapCraftIndex] != NULL)
                                      ? pW->apCraft[pW->nMapCraftIndex]->bCraftType : 0;
    c.Count.apXcom     = pW->pTactMap->apXcomUnits;
    c.Count.nXcomCount = pW->pTactMap->nXcomCount;
    c.Count.apCiv      = pW->pTactMap->apCivilians;
    c.Count.nCivCount  = pW->pTactMap->nCivilianCount;
    c.pUi     = &pW->sTactUi;
    c.aszMsg1 = pW->aszDeployMsg1;
    c.aszMsg2 = pW->aszDeployMsg2;
    c.nMsgCap = (i32)sizeof pW->aszDeployMsg1;
    c.pHooks  = &h;

    World_TactUiSyncIn(pW);
    (void)Map_BuildDeployConfirmYesNo(&c);
    World_TactUiSyncOut(pW);
}

static void World_ClickSpeed0(void) { World_TactSpeedCall(0); }
static void World_ClickSpeed1(void) { World_TactSpeedCall(1); }
static void World_ClickSpeed2(void) { World_TactSpeedCall(2); }
static void World_ClickSpeed3(void) { World_TactSpeedCall(3); }
static void World_ClickSpeed4(void) { World_TactSpeedCall(4); }

static void World_ClickGamma0(void) { World_TactGammaCall(0); }
static void World_ClickGamma1(void) { World_TactGammaCall(1); }
static void World_ClickGamma2(void) { World_TactGammaCall(2); }
static void World_ClickGamma3(void) { World_TactGammaCall(3); }
static void World_ClickGamma4(void) { World_TactGammaCall(4); }

/* nStep is 0-based in the port; the stored volume is nStep+1, the original's 1..5 naming (menu.h:138). */
static void World_ClickMusicVol1(void) { World_TactMusicVolCall(0); }
static void World_ClickMusicVol2(void) { World_TactMusicVolCall(1); }
static void World_ClickMusicVol3(void) { World_TactMusicVolCall(2); }
static void World_ClickMusicVol4(void) { World_TactMusicVolCall(3); }
static void World_ClickMusicVol5(void) { World_TactMusicVolCall(4); }

static void World_ClickSfxVol1(void) { World_TactSfxVolCall(0); }
static void World_ClickSfxVol2(void) { World_TactSfxVolCall(1); }
static void World_ClickSfxVol3(void) { World_TactSfxVolCall(2); }
static void World_ClickSfxVol4(void) { World_TactSfxVolCall(3); }
static void World_ClickSfxVol5(void) { World_TactSfxVolCall(4); }

static void World_ClickToggleScroll(void) { World_TactToggleCall(TACT_TOGGLE_SCROLL); }
static void World_ClickToggleShow(void)   { World_TactToggleCall(TACT_TOGGLE_SHOW); }
static void World_ClickToggleRun(void)    { World_TactToggleCall(TACT_TOGGLE_RUN); }
static void World_ClickToggleTurn(void)   { World_TactToggleCall(TACT_TOGGLE_TURN); }
static void World_ClickToggleMenu(void)   { World_TactToggleCall(TACT_TOGGLE_MENU); }

static void World_ClickConfirm0(void) { World_TactConfirmCall(16); }
static void World_ClickConfirm1(void) { World_TactConfirmCall(16); }
static void World_ClickConfirm2(void) { World_TactConfirmCall(16); }
static void World_ClickConfirm3(void) { World_TactConfirmCall(16); }
static void World_ClickConfirm4(void) { World_TactConfirmCall(16); }
static void World_ClickConfirm5(void) { World_TactConfirmCall(16); }
static void World_ClickConfirmSnd15(void) { World_TactConfirmCall(15); }

static void World_ClickConfirmClear(void)     { World_TactButtonCall(MenuBtn_ConfirmClear); }
static void World_ClickSecondary(void)        { World_TactButtonCall(MenuBtn_Secondary); }
static void World_ClickActionBack(void)       { World_TactButtonCall(MenuBtn_ActionBack); }
static void World_ClickSecondaryDismiss(void) { World_TactDismissCall(MenuBtn_SecondaryDismiss); }
static void World_ClickCancel(void)           { World_TactDismissCall(MenuBtn_Cancel); }

static void World_ClickSaveLoad0(void) { World_TactSaveLoadCall(0); }
static void World_ClickSaveLoad1(void) { World_TactSaveLoadCall(1); }
static void World_ClickSaveLoad2(void) { World_TactSaveLoadCall(2); }
static void World_ClickSaveLoad3(void) { World_TactSaveLoadCall(3); }
static void World_ClickSaveLoad4(void) { World_TactSaveLoadCall(4); }
static void World_ClickSaveLoad5(void) { World_TactSaveLoadCall(5); }
static void World_ClickSaveLoad6(void) { World_TactSaveLoadCall(6); }

/* ------------------------------------------------------------- the widget click table
 *
 * One entry per bank widget, in Tact_InitUiControls' install order; NULL where the original has no
 * handler (113..127) or where this Part defers the composition (4, 5, 17, 18, 56, 57, 58). The
 * names below match g_aUiClickName's own comments slot by slot. */
static void (*const g_aTactBankClicks[PUFO_PBANK_WIDGET_COUNT])(void) = {
    World_ClickCycleNext,          /* 0   Tact_CycleNextUnit @ 0x16684 */
    World_TactHudClickUp,          /* 1   TactHud_ClimbUp @ 0x1674c */
    World_ClickLayerUp,            /* 2   MapView_LayerUp @ 0x16530 */
    World_ClickEnterLookMode,      /* 3   MapView_EnterLookMode @ 0x163d4 */
    World_ClickOpenInventory,      /* 4   Inv_OpenScreen @ 0x16ef4 (Part 55) */
    World_ClickEndTurn,            /* 5   MenuBtn_EndTurn @ 0x16494 (Part 56) */
    World_TactHudClickDown,        /* 6   TactHud_ClimbDown @ 0x16af0 */
    World_ClickLayerDown,          /* 7   MapView_LayerDown @ 0x164f0 */
    World_ClickToggleKneel,        /* 8   TactHud_ToggleKneel @ 0x16570 */
    World_ClickFocusSelected,      /* 9   MapView_FocusSelected @ 0x16710 */
    World_ClickRadio0,             /* 10  UiBank_SelectRadio0 (starts selected: nState override 1) */
    World_ClickRadio1,             /* 11 */
    World_ClickRadio2,             /* 12 */
    World_ClickRadio3,             /* 13 */
    World_ClickOpenScreen7,        /* 14  Map_UiScreen7_PlaySample0E @ 0x164b8 */
    World_ClickOpenScreen15,       /* 15  Map_UiScreen0F_PlaySample0F @ 0x164d4 */
    World_ClickOpenCommand,        /* 16  Tact_OpenCommandScreen @ 0x16ffc */
    World_ClickOpenLeftHand,       /* 17  Tact_OpenLeftHandMenu @ 0x17030 (Part 57) */
    World_ClickOpenRightHand,      /* 18  Tact_OpenRightHandMenu @ 0x17148 (Part 57) */
    World_ClickSelectPrev,         /* 19  Tact_SelectPrevUnit @ 0x17260 */
    World_ClickSelectNext,         /* 20  Tact_SelectNextUnit @ 0x17304 */
    World_ClickFilter5,            /* 21  Inv_SetFilter5 @ 0x17710 */
    World_ClickInvScrollDown,      /* 22  Inv_ScrollDown @ 0x173a8 */
    World_ClickInvScrollUp,        /* 23  Inv_ScrollUp @ 0x173e4 */
    World_ClickFilterAll,          /* 24  Inv_SetFilterAll @ 0x17410 */
    World_ClickFilter0,            /* 25  Inv_SetFilter0 @ 0x17490 */
    World_ClickFilter3,            /* 26  Inv_SetFilter3 @ 0x17510 */
    World_ClickFilter4,            /* 27  Inv_SetFilter4 @ 0x17590 */
    World_ClickFilter1,            /* 28  Inv_SetFilter1 @ 0x17610 */
    World_ClickFilter2,            /* 29  Inv_SetFilter2 @ 0x17690 */
    World_ClickConfirm2,           /* 30  MenuBtn_Confirm2 @ 0x19884 */
    World_ClickOpenCommandFlagged, /* 31  Tact_OpenCommandScreenFlagged @ 0x198c4 */
    World_ClickFireAuto,           /* 32  Tact_SetFireModeAuto @ 0x17868 */
    World_ClickFireSnap,           /* 33  Tact_SetFireModeSnap @ 0x177fc */
    World_ClickFireAimed,          /* 34  Tact_SetFireModeAimed @ 0x17790 */
    World_ClickConfirmUseItem,     /* 35  TactAction_ConfirmUseItem @ 0x178d4 */
    World_ClickTryFire4,           /* 36  Tact_TryFireMode4 @ 0x179e4 */
    World_ClickTryFire5,           /* 37  Tact_TryFireMode5 @ 0x17b1c */
    World_ClickMelee6,             /* 38  TactAction_ConfirmMeleeMode6 @ 0x17bb8 */
    World_ClickBeginMultiTarget,   /* 39  Tact_BeginMultiTarget @ 0x18380 */
    World_ClickUseSpecialItem,     /* 40  TactHud_UseSpecialItem20 @ 0x17d54 */
    World_ClickConfirmSpecial9,    /* 41  TactAction_ConfirmSpecial9 @ 0x18260 */
    World_ClickTryFire10,          /* 42  Tact_TryFireMode10 @ 0x17a80 */
    World_ClickMode11,             /* 43  Map_PlaySample17_SetMode11 @ 0x179c0 */
    World_ClickSubMode0,           /* 44  Map_SetSubMode0_Play11 @ 0x18418 */
    World_ClickSubMode1, World_ClickSubMode2, World_ClickSubMode3,   /* 45..47 */
    World_ClickSubMode4, World_ClickSubMode5, World_ClickSubMode6,   /* 48..50 */
    World_ClickSubMode7, World_ClickSubMode8,                        /* 51, 52 */
    World_ClickOpenLoadSlots,      /* 53  Menu_OpenLoadSlots @ 0x18864 */
    World_ClickOpenSaveSlots,      /* 54  Menu_OpenSaveSlots @ 0x188a4 */
    World_ClickOpenOptions,        /* 55  Options_SyncUiFromMap_GotoScreen12 @ 0x188e4 */
    World_ClickDeployConfirm,    /* 56  Map_BuildDeployConfirmYesNo @ 0x18a68 (Part 58) */
    NULL,                          /* 57  Menu_ConfirmQuitDialog @ 0x18e2c (deferred) */
    NULL,                          /* 58  MenuBtn_ResizeWindow @ 0x19930 (caller window call) */
    World_ClickSamples1614Clear,   /* 59  Map_PlaySamples16_14_ClearUiScreen @ 0x18e9c */
    World_ClickSpeed0, World_ClickSpeed1, World_ClickSpeed2,         /* 60..62 MenuBtn_SetSpeed0..2 */
    World_ClickSpeed3, World_ClickSpeed4,                            /* 63, 64 */
    World_ClickGamma0, World_ClickGamma1, World_ClickGamma2,         /* 65..67 MenuBtn_SetGamma0..2 */
    World_ClickGamma3, World_ClickGamma4,                            /* 68, 69 */
    World_ClickToggleScroll,       /* 70  Map_ToggleFlag128A8D @ 0x19250 */
    World_ClickToggleShow,         /* 71  Map_ToggleFlag128A8E @ 0x192d4 */
    World_ClickToggleRun,          /* 72  Map_ToggleFlag128A8F @ 0x19348 */
    World_ClickToggleTurn,         /* 73  Map_ToggleFlag128A90 @ 0x193a0 */
    World_ClickToggleMenu,         /* 74  Map_ToggleFlag128A91 @ 0x1941c */
    World_ClickConfirm4,           /* 75  MenuBtn_Confirm4 @ 0x19910 */
    World_ClickMusicMute,          /* 76  Ui_ToggleMusicMute @ 0x19948 */
    World_ClickSfxMute,            /* 77  Ui_ToggleSfxMute @ 0x199ac */
    World_ClickMusicVol1, World_ClickMusicVol2, World_ClickMusicVol3, /* 78..80 Options_SetMusicVol1..3 */
    World_ClickMusicVol4, World_ClickMusicVol5,                       /* 81, 82 */
    World_ClickSfxVol1, World_ClickSfxVol2, World_ClickSfxVol3,       /* 83..85 MenuBtn_SetSfxVol1..3 */
    World_ClickSfxVol4, World_ClickSfxVol5,                           /* 86, 87 */
    World_ClickForceNoDrag,        /* 88  Menu_ToggleForceNoDrag @ 0x19d94 */
    World_ClickSecondary,          /* 89  MenuBtn_Secondary @ 0x19498 */
    World_ClickActionBack,         /* 90  MenuBtn_ActionBack1 @ 0x19618 */
    World_ClickSecondaryDismiss,   /* 91  MenuBtn_SecondaryDismiss @ 0x195e0 */
    World_ClickConfirmClear,       /* 92  MenuBtn_ConfirmClear @ 0x194c8 */
    World_ClickActionBack,         /* 93  MenuBtn_ActionBack0 @ 0x195a8 (byte-identical to 90) */
    World_ClickDeployOpen,         /* 94  Map_PlaySamples16_7_SetDeployFlag @ 0x19528 */
    World_ClickDeployClose,        /* 95  Map_PlaySamples16_7_ClearDeployFlag @ 0x19568 */
    World_ClickScreen7Samples,     /* 96  Map_UiScreen7_PlaySamples16_14 @ 0x194f8 */
    World_ClickConfirmSnd15,       /* 97  MenuBtn_ConfirmSnd15 @ 0x198f0 */
    World_ClickConfirm0,           /* 98  MenuBtn_Confirm0 @ 0x19650 */
    World_ClickIncLayer,           /* 99  Map_IncByte128AF0_Play11 @ 0x19670 */
    World_ClickDecLayer,           /* 100 Map_DecByte128AF0_Play11 @ 0x196ac */
    World_ClickConfirm1,           /* 101 MenuBtn_Confirm1 @ 0x196e8 */
    World_ClickSelectPrevFiltered, /* 102 Tact_SelectPrevUnitFiltered @ 0x19708 */
    World_ClickSelectNextFiltered, /* 103 Tact_SelectNextUnitFiltered @ 0x197b8 */
    World_ClickConfirm3,           /* 104 MenuBtn_Confirm3 @ 0x198a4 */
    World_ClickSaveLoad0, World_ClickSaveLoad1, World_ClickSaveLoad2, /* 105..107 SaveLoad_Slot0..2 */
    World_ClickSaveLoad3, World_ClickSaveLoad4, World_ClickSaveLoad5, /* 108..110 */
    World_ClickSaveLoad6,          /* 111 */
    World_ClickConfirm5,           /* 112 MenuBtn_Confirm5 @ 0x1a014 */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,        /* 113..120, never installed */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,              /* 121..127 */
    World_ClickCancel,             /* 128 MenuBtn_Cancel0 @ 0x1a24c */
    World_ClickCancel,             /* 129 MenuBtn_Cancel1 @ 0x1a26c (identical) */
};

static void World_TactInitUiControlsThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    const WorldHooks *pHooks = pW->pFrameHooks;
    UiBankInitCtx ctx;

    if (pHooks == NULL || pHooks->pBank == NULL) {
        return;                                            /* no caller bank to install onto */
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.pHudPad     = pW->apTactPads[PUFO_UIPAD_HUD];      /* unk_113AB4, Map+0xFC: its size places
                                                            * the tactical group (widgets 0..18) */
    ctx.pLookCtl    = &pW->sTactLookCtl;                   /* the original allocates unk_1571E4 here */
    ctx.apfnClick   = g_aTactBankClicks;
    ctx.pfnLoadPads = pW->pfnLoadPads;                     /* the caller's seam for UiControl_LoadPads
                                                            * @ 0x1a604 */
    ctx.pUser       = pW->pLoadPadsUser;
    ctx.pfnLookClick = World_TactLookClickThunk;           /* TactAction_ConfirmFireBurst @ 0x18774 */
    Tact_InitUiControls(pHooks->pBank, &ctx);

    /* Input_OnConfirm's armed multi-target commit clears *unk_1571E4+9 -- the look control's
     * bEnabled byte is the scratch pointer's target. */
    pW->sTactInput.pbScratchFlag9 = &pW->sTactLookCtl.bEnabled;
}

/* --------------------------------------------------------------- mission end (Part 49)
 *
 * Tact_CheckBattleEnd @ 0x34fbc is fully ported in pufo_tactend.c and unit-tested there; what was
 * missing was an owner to run it over a live frame. The original calls it from the combat side
 * (Tact_KillUnit 0x4efc8, Tact_ProcessFallAction 0x53318/0x53bd4) and from the abort back-button
 * (UiScreen_OnBack case 9, 0x156c8); those call sites belong to the tactical runtime unit, and
 * World_MissionEndCheck is the shell's stand-in until that unit lands. The debrief it writes is
 * pure state -- caption, score, two panels, the pending-fund sum, the site commit -- so the state
 * half is complete here; the banner and debrief DRAWS are the tact-HUD unit's. */

static void World_EndHookShutdown(u8 bKeepSome, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    /* The shell's stand-in for Tact_Shutdown @ 0x3d568, the same choice Part 47 made for the
     * teardown: Tact_Shutdown frees the ORIGINAL's global blocks, World_FreeTactical releases the
     * shell-owned counterparts of the same frame. It also stands in for Tact_Shutdown's first
     * store, the Map+0x00 action-latch clear: the tact flag goes down with the frame. bKeepSome
     * does not split the shell's teardown -- both arms release the same shell blocks, and the
     * geoscape rosters have one owner (World_Free), never the frame's. */
    (void)bKeepSome;
    pW->bTactFlag = 0;                                       /* 0x3D574 */
    PufoFont_Free(pW->pFont);      /* Tact_Shutdown releases the font too (TactShutdownEnv.pFont): the
                                     * tact sprites Tact_InitMission loaded go here, so the rebuild's
                                     * PufoFont_Init starts from the cleared block, like the original's
                                     * Geoscape_InitUI(1) at 0x162C8 after Tact_Shutdown(0). */
    World_FreeTactical(pW);
}

static void World_EndHookInitMission(i32 nMode, void *pUser)
{
    /* Only the Mars restart tail reaches this (0x36594..0x365F8): shutdown(0), the mission-type
     * rewrite, then Tact_InitMission in place. The seam mirror returns a value the hook discards. */
    (void)World_MissionSeamInitMission(nMode, pUser);
}

bool World_MissionEndCheck(PufoWorldState *pW, u8 bAbort)
{
    TactBattleEndEnv  env;
    TactTileTallyEnv  tiles;
    TactUnitTallyEnv  units;
    TactItemTallyEnv  items;
    TactCraftTallyEnv craft;
    GameMap          *pMap;
    u16  nSoldiers;
    i32  nCraftCount;
    u8   nDimX, nDimY, nSpawnX, nSpawnY;

    if (!pW->bTactLive || pW->pTactMap == NULL) {
        return false;   /* without a mission there is no frame to check */
    }
    pMap = pW->pTactMap;

    memset(&env, 0, sizeof(env));
    memset(&tiles, 0, sizeof(tiles));
    memset(&units, 0, sizeof(units));
    memset(&items, 0, sizeof(items));
    memset(&craft, 0, sizeof(craft));

    /* Four scalars the envs want in a width the state does not store them in. The envs live only
     * for this call, so the mirrors are stack cells, not new state owners. */
    nSoldiers   = (u16)pW->nSoldierCount;   /* nSoldierCount @ 0x115350 is a u16 in the original */
    nCraftCount = (i32)pW->bCraftCount;     /* Map_nCraftCount @ Map+0x1194 */
    nDimX       = (u8)pMap->geom.nDimX;     /* Map_nMapDimX @ Map+0x1511B */
    nDimY       = (u8)pMap->geom.nDimY;     /* Map_nMapDimY @ Map+0x1511C */
    nSpawnX     = (u8)pW->nMapSpawnTileX;   /* Map_nSpawnTileX @ Map+0x1514C */
    nSpawnY     = (u8)pW->nMapSpawnTileY;   /* Map_nSpawnTileY @ Map+0x15150 */

    /* Tact_TallyBattleStats @ 0x3348c: the four-layer sprite walk and the two seeded rolls. */
    tiles.apCells          = pW->apTactCells;            /* apCells0 @ Map+0x15338 */
    tiles.pnMapDimX        = &nDimX;
    tiles.pnMapDimY        = &nDimY;
    tiles.pbAltOutcomePath = &pW->bMapAltOutcomePath;    /* Map+0x1A21, the gate at 0x334B4 */
    tiles.pbMissionVictory = &pW->bMapMissionVictory;    /* Map+0x150D4 */
    tiles.pnTerrainVariant = &pW->nMapTerrainVariant;    /* Map+0x15111 */
    tiles.pnMissionType    = &pW->nMapMissionType;       /* Map+0x1511A */
    tiles.pnEleriumRoll    = &pW->nMapUnk138E6C;         /* unk_138E6C @ Map+0x254B4 */
    tiles.pnFoodRoll       = &pW->nMapUnk138E70;         /* unk_138E70 @ Map+0x254B8 */

    /* Tact_TallyUnitResults @ 0x33780: the three rosters through the extraction box. */
    units.apSoldiers       = pW->apSoldiers;             /* Map+0x1198 */
    units.pnSoldierCount   = &nSoldiers;
    units.apXcomUnits      = pMap->apXcomUnits;          /* Map+0x15154 */
    units.pnXcomCount      = &pMap->nXcomCount;          /* Map+0x1511F */
    units.apEnemyUnits     = pMap->apEnemyUnits;         /* Map+0x151CC */
    units.pnEnemyCount     = &pMap->nEnemyCount;         /* Map+0x15124 */
    units.apCivilianUnits  = pMap->apCivilians;          /* Map+0x15244 */
    units.pnCivilianCount  = &pMap->nCivilianCount;      /* Map+0x15120 */
    units.pbMissionVictory = &pW->bMapMissionVictory;
    units.pbAltOutcomePath = &pW->bMapAltOutcomePath;

    /* Tact_TallyItemRecovery @ 0x33c80: empties carried items onto cells and tallies the box. */
    items.apCells          = pW->apTactCells;
    items.apObjects        = pW->pAssets->apObjects;     /* apObjects @ Map+0x108 */
    items.pnObjectCount    = &pW->tactEnv.nObjectCount;  /* the init-time copy of Map_nObjectCount @ Map+0x10A8 */
    items.apCraft          = pW->apCraft;                /* Map+0x10AC */
    items.pnCraftIndex     = &pW->nMapCraftIndex;        /* Map+0x15113 */
    items.apEnemyUnits     = pMap->apEnemyUnits;
    items.pnEnemyCount     = &pMap->nEnemyCount;
    items.apXcomUnits      = pMap->apXcomUnits;
    items.pnXcomCount      = &pMap->nXcomCount;
    items.apCivilianUnits  = pMap->apCivilians;
    items.pnCivilianCount  = &pMap->nCivilianCount;
    items.pbMissionVictory = &pW->bMapMissionVictory;
    items.pbAltOutcomePath = &pW->bMapAltOutcomePath;

    /* Tact_TallyCraftRecovery @ 0x344b4: caps recovery against the craft cargo, parks the rest. */
    craft.apObjects        = pW->pAssets->apObjects;
    craft.pnObjectCount    = &pW->tactEnv.nObjectCount;
    craft.apCraft          = pW->apCraft;
    craft.pnCraftCount     = &nCraftCount;
    craft.pnCraftIndex     = &pW->nMapCraftIndex;
    craft.pnMissionType    = &pW->nMapMissionType;
    craft.pnHomeBase       = &pW->nCraftHomeBase;        /* nCraftHomeBase @ 0x158848, the base being defended */
    craft.aSites           = pW->apSites;                /* aSmapSites @ 0x162030 */
    craft.pnSiteCount      = &pW->nSiteCount;            /* nSmapSiteCount @ 0x186410 */
    craft.pbMissionVictory = &pW->bMapMissionVictory;
    craft.pbAltOutcomePath = &pW->bMapAltOutcomePath;

    env.pTiles = &tiles;                                  /* 0x35240 */
    env.pUnits = &units;                                  /* 0x35284 */
    env.pItems = &items;                                  /* 0x352BC */
    env.pCraft = &craft;                                  /* 0x352E4 */

    /* Tact_ClearSoldierBattleFlags @ 0x346e0's env: the soldier roster, the two promotion-report
     * panels (apPanel[2]/[3] -- the 0x1568E0/0x1568DC pair in the tactinit allocation order) and
     * the six Xcom rank names. */
    env.flags.apSoldiers         = pW->apSoldiers;
    env.flags.pnSoldierCount     = &nSoldiers;
    env.flags.pPanelName         = pW->tactEnv.apPanel[2];
    env.flags.pPanelRank         = pW->tactEnv.apPanel[3];
    env.flags.panRankName        = (const char *)g_textTables.pE; /* aszTextE @ 0xB2200 */
    env.flags.pbPromotionReport  = &pW->bMapPromotionReport;      /* 0x1153D8 = Map+0x1A20 */

    /* Tact_GetCameraBounds @ 0x3519C reads its anchor flag off Map+0x1A21, which is the abort
     * byte CheckBattleEnd stored at its own head: a normal mission boxes the whole map, an
     * aborted one boxes the craft. */
    env.camera.bUseCraft   = pW->bMapAltOutcomePath != 0;
    env.camera.nMissionType = pW->nMapMissionType;
    env.camera.nMapDimX    = nDimX;
    env.camera.nMapDimY    = nDimY;
    env.camera.nCraftMapX  = pW->bMapCraftMapX;          /* Map+0x15139 */
    env.camera.nCraftMapY  = pW->bMapCraftMapY;          /* Map+0x1513A */
    env.camera.bCraftType  = (pW->nMapCraftIndex < (u8)pW->bCraftCount
                              && pW->apCraft[pW->nMapCraftIndex] != NULL)
                             ? pW->apCraft[pW->nMapCraftIndex]->bCraftType : 0;

    env.pbBattleEnded     = &pW->bMapBattleEnded;        /* Map+0x5C, the early exit at 0x34FEC */
    env.pbTickHold        = &pW->bMapTickHold;           /* Map+0x5A */
    env.pbAltOutcomePath  = &pW->bMapAltOutcomePath;     /* 0x34FE4: the caller's abort IS the alternate path */
    env.pbMissionVictory  = &pW->bMapMissionVictory;     /* 0x35070 / 0x350F0 / 0x351CC */
    env.pnMissionOutcome  = &pW->nMapMissionOutcome;     /* Map+0x1A1C: written 0/1/2, never read */
    env.pnMissionType     = &pW->nMapMissionType;
    env.pbSpawnAlt        = &pW->bMapSpawnAlt;           /* Map+0x15148 */
    env.pnFacilityEndFlag = &pW->nBaseCount;             /* the signed byte at 0x159D80 = abFacilityGrid + 0x618 (0x35160) */
    env.pnSpawnTileX      = &nSpawnX;
    env.pnSpawnTileY      = &nSpawnY;

    env.apEnemyUnits      = pMap->apEnemyUnits;
    env.pnEnemyCount      = &pMap->nEnemyCount;
    env.pnEnemyReserve    = &pW->nMapEnemyReserveCount;  /* Map+0x15144: the hidden park counts as live enemies at 0x350E8 */
    env.pnXcomCount       = &pMap->nXcomCount;

    env.puUiScreen        = &pW->nUiScreen;              /* Map+0x3A: 13 at 0x351EC */
    env.pnBannerFadeTicks = &pW->nMapUiSlot1;            /* dword_113A1C @ Map+0x64, the same dword Tact_InitMission zeroes as apUiSlotsA[1] */
    env.pbCtxBusy         = &pMap->tact.bBusy;           /* TactCtx272 -> bBusy @ +0xF8, cleared at 0x35228 */
    env.pbEnemyTurnActive = &pW->bMapEnemyTurnActive;    /* Map+0x5B, cleared at 0x3523C */
    env.pPanelLabel       = pW->tactEnv.apPanel[0];      /* pUiPanel0 @ 0x1568E8: the debrief labels */
    env.pPanelValue       = pW->tactEnv.apPanel[1];      /* pUiPanel1 @ 0x1568E4: its value column */
    env.pnDebriefLine     = &pW->nMapDebriefLine;        /* Map+0x150D0 */
    env.pszCaption        = pW->aMapMissionCaption;      /* Map+0x15094 */
    env.pszScore          = pW->aMapMissionScore;        /* Map+0x150B2 */
    env.pTextRecs         = (const char *)pW->pAssets->pTextRecs; /* aTextRecs @ 0xB38D0 */

    env.apBases           = pW->apBases;                 /* Map+0x1174: the site commit reads the home base's byte (0x36500) */
    env.pnSiteCount       = &pW->nSiteCount;             /* 0x186410, the working site slot the commit fills */

    env.pbSpawnOnceFlag   = &pW->bSpawnOnce;             /* 0x162028: set to 1 unconditionally at 0x36584 */
    env.pnFundPending     = &pW->economy.aZero44_49[5];  /* g_nFundPending @ 0x1865BC */
    env.pbMissionMode     = &pW->bMapMissionMode;        /* Map+0x1511D */
    env.pnTerrainVariant  = &pW->nMapTerrainVariant;
    env.pnMapLayers       = &pMap->nMapLayers;           /* Map+0x15114 */
    env.pbUnk15115        = &pW->bMapUnk15115;           /* Map+0x15115 */
    env.pbFrameScratch    = &pW->bMapFrameScratch;       /* unk_138E74 @ Map+0x254BC */

    env.hooks.pfnShutdown      = World_EndHookShutdown;
    /* EndScreen_Init writes unk_157194, unk_B35D8, unk_1571C0 and unk_138E88, none of which the
     * shell owns, and there is no ending flow to hand control to -- the same seam deviation the
     * launch already records (0x84230). It reaches only the victory-tile and cannot-complete arms. */
    env.hooks.pfnEndScreenInit = World_MissionSeamInitEnd;
    env.hooks.pfnInitMission   = World_EndHookInitMission; /* the Mars restart tail only */
    /* Tact_ScanVictoryTiles gates on the alien-base type with the alternate spawn (0x3328C) before
     * it ever calls the probe, so a base-assault frame never reaches it. NULL is also the callee's
     * "no" answer. */
    env.hooks.pfnCellHasNoItems = NULL;
    env.hooks.pUser             = pW;

    Tact_CheckBattleEnd(&env, bAbort);
    return pW->bMapBattleEnded != 0;
}

bool World_MissionCancelEnd(PufoWorldState *pW)
{
    GeoscapeInitEnv env;

    if (pW->bTactFlag == 0) {
        return false;                                    /* 0x16208: outside a mission there is nothing to cancel */
    }
    /* The in-flight gate (0x16218..0x16260). Map_bTrajHitStop @ 0x127A25 = Map+0x1406D is the
     * GameMap byte since Part 50, so all four cells now read live. */
    if (pW->bMapFire2 != 0
            || (pW->pTactMap != NULL && pW->pTactMap->bFireStatus1 != 0)
            || (pW->pTactMap != NULL && pW->pTactMap->bTrajHitStop != 0)
            || (pW->pTactState != NULL && pW->pTactState->bQueueActive != 0)) {
        if (pW->bMapTickHold == 0) {
            return false;
        }
    }
    if (pW->nUiScreen < 8u) {
        return false;                                    /* 0x1626C: below the mission screens */
    }
    if (pW->nUiScreen <= 9u) {
        /* 0x162DC / 0x162E0: UiModal_End then the screen write. The modal bank is the shell's
         * sModal since Part 51, so the end runs for real -- the Part 49 write-only deviation is
         * closed. End clears only the open byte (0x26494); the stored fields survive, as in the
         * original. */
        UiModal_End(&pW->sModal);
        pW->nUiScreen = 0;
        return true;
    }
    if (pW->nUiScreen > 0x13u && pW->nUiScreen <= 0x15u) {
        pW->nUiScreen = (u16)pW->nMapUiSlot11;           /* 0x16294: word_113A44 @ Map+0x8C, the saved screen */
        return true;
    }
    if (pW->nUiScreen == 13u && pW->bMapPromotionReport != 0) {
        pW->nUiScreen = 19u;                             /* 0x162B8..0x162D4: the promotion report comes first */
        return true;
    }
    if (pW->nUiScreen != 13u && pW->nUiScreen != 19u) {
        return false;                                    /* the original returns the id unchanged */
    }

    /* 0x162BC / 0x162C8: shut the mission down keeping nothing, then rebuild the geoscape UI the
     * launch seam freed. Mode 1 skips InitUI's new-game branch, so this is a rebuild of the
     * singletons, not a restart of the run. The original leaves Map_nUiScreen at 13 here -- only
     * the 8/9 and 0x14/0x15 arms write it -- and the port keeps that. */
    World_EndHookShutdown(0, pW);
    World_FillInitUiEnv(pW, &env);
    Geoscape_InitUI(&env, 1);
    if (pW->pFrameHooks != NULL) {
        World_WireGeoscapeFrame(pW, pW->pFrameHooks);    /* the frame envs re-point at the new singletons */
    }
    return true;
}

/* Smap_BeginCraftSelect @ 0x838dc, the mission-launch decision tree, and the two envs it reaches
 * through: the ship-removal pair (its lookups at 0x83968, its removal at 0x83F7C and 0x84428) and
 * the demolish call at 0x84208. They live on PufoWorldState because every cell they touch is
 * already the shell's: the Map mission header above, the rosters, the facility grid, the site
 * table, the economy dword at 0x1865BC and the three zoom overlay pads. */
static void World_WireMissionSeams(PufoWorldState *pW)
{
    ShipRemovalEnv *pS = &pW->shipEnv;
    BaseDemolishEnv *pD = &pW->demolishEnv;
    SmapCraftSelectEnv *pE = &pW->craftEnv;

    memset(pS, 0, sizeof(*pS));
    pS->apCraft         = pW->apCraft;                     /* Map + 0x10AC */
    pS->pnShipCount     = &pW->bCraftCount;                /* Map + 0x1194 */
    pS->apSoldiers      = pW->apSoldiers;                  /* Map + 0x1198 */
    pS->apObjects       = (pW->pAssets != NULL) ? pW->pAssets->apObjects : NULL; /* Map + 0x108 */
    pS->nObjectCount    = (pW->pAssets != NULL) ? (u16)pW->pAssets->nObjectCount : 0;
    pS->pStrategList    = &pW->strategList;                /* 0x1864F8 + 0x3C */
    pS->pWorld          = &pW->geo.world;
    pS->pnSpawnOnceFlag = &pW->bSpawnOnce;                 /* 0x162028 */
    pS->pSpawnCtx       = &pW->spawnCtx;
    pS->apBases         = pW->apBases;                     /* Map + 0x1174 */
    pS->pnGameTotalSec  = &pW->geo.time.nTotalSec;         /* GameTime.nTotalSec (0x159610) */
    /* Three fields stay NULL, and each is the callee's own guard, not a crash waiting to happen:
     *   pnSoldierCount -- ShipRemovalEnv wants a u16* to Map+0x1998 while the shell's owner of
     *     that count is an i32 (pufo_world.h:72, and pufo_save.h notes the original's u16 writes
     *     hit its LOW half). Pointing a u16* at the i32 would be a strict-aliasing violation, so
     *     the "soldiers aboard the craft that was removed" pass is skipped until the count has a
     *     u16 owner. Recorded as a deviation, not hidden.
     *   abHangarFlag @ 0x158CD8 -- the table's extent is nowhere in the IDB: no cross-reference
     *     names the address (the writes reach it as a displacement from a register base), so the
     *     shell does not invent a size for it.
     *   anAssignedShip @ 0x16146C with its count at 0x161468 -- the same: Ship_RemoveWithCargo,
     *     Base_RemoveAllShips and BuySellScreen_CommitTransaction maintain it, and the shell has
     *     no owner of it yet. */

    memset(pD, 0, sizeof(*pD));
    pD->apCraft           = pW->apCraft;
    pD->pnShipCount       = &pW->bCraftCount;
    pD->apSoldiers        = pW->apSoldiers;
    pD->apBases           = pW->apBases;
    pD->apObjects         = pS->apObjects;
    pD->abFacilityGrid    = pW->abFacilityGrid;            /* gBaseFacGrid @ 0x159768 */
    pD->apSiteRecs        = pW->aSiteRecs;                 /* 0x186C40 */
    pD->apManufCols       = pW->auManufCol;                /* 0x15E19C */
    pD->apProductC        = pW->auProductU16;              /* 0x15D09C */
    pD->apResearchCols    = pW->auResearchCol;             /* auResearchCol @ 0x16022C */
    pD->pnResearchEntries = &pW->nResearchEntries;         /* 0x160C4C */
    pD->apSites           = pW->apSites;                   /* 0x162030 */
    pD->nSiteCount        = pW->nSiteCount;                /* 0x186410 */
    pD->pbBaseCount       = (u8 *)&pW->nBaseCount;         /* 0x159D80, read SIGNED */
    pD->pShipEnv          = pS;
    /* apProductB @ 0x15B79C stays NULL for the reason World_BuildManufClearEnv already records:
     * the manufacturing screen's set/get buttons are unwired, so nothing in the shell writes it. */

    memset(pE, 0, sizeof(*pE));
    pE->apCraft             = pW->apCraft;
    pE->pnCraftCount        = &pW->bCraftCount;            /* Map + 0x1194 */
    pE->pbMissionMode       = &pW->bMapMissionMode;        /* Map+0x1511D, set at 0x8BB20 */
    pE->pbUnk1511E          = &pW->bMapUnk1511E;           /* 0x128AD6 */
    pE->pnMissionType       = &pW->nMapMissionType;        /* Map+0x1511A */
    pE->abSpriteSet         = pW->abMapSpriteSet;          /* Map+0x15116..19 */
    pE->pbSpawnAlt          = &pW->bMapSpawnAlt;           /* Map+0x15148 */
    pE->pbUnk15115          = &pW->bMapUnk15115;           /* Map+0x15115 */
    pE->pnTerrainVariant    = &pW->nMapTerrainVariant;     /* Map+0x15111 */
    pE->pnMapLayers         = &pW->nMapMapLayers;          /* Map+0x15114 */
    pE->pnTierRaw           = &pW->nMapMissionTierRaw;     /* Map+0x1510C */
    pE->pnCraftIndex        = &pW->nMapCraftIndex;         /* Map+0x15113 */
    pE->pbTickHold          = &pW->bMapTickHold;           /* 0x113A12, set at 0x84238 */
    pE->pbBattleEnded       = &pW->bMapBattleEnded;        /* 0x113A14, set at 0x84240 */
    pE->apBases             = pW->apBases;
    pE->pbHomeBase          = &pW->nCraftHomeBase;         /* 0x158848, read SIGNED */
    pE->pbBaseRace          = &pW->nAssaultBaseRace;       /* 0x158849, read SIGNED */
    pE->pStrategList        = &pW->strategList;            /* 0x186534 */
    pE->pLines              = &pW->msgLines;               /* the root line list @ 0x158890 */
    pE->pTime               = &pW->geo.time;
    pE->pnSelectedShipIdx   = &pW->nSelectedShipIdx;       /* 0x187054 */
    pE->pnMsgShipIdx        = &pW->nMsgShipIdx;            /* 0x159E74 */
    pE->pnSelectedStrategIdx = &pW->nSelectedStrategIdx;   /* 0x186BE4 */
    pE->pbAssaultFlag       = &pW->bCraftSelectAssaultFlag; /* 0x1870A8 */
    pE->pbSelectionFlag     = &pW->bMapSelectionFlag;      /* 0x159E84 */
    pE->pbFlag187084        = &pW->bScratch187084;         /* written at 0x841FC, never read */
    pE->pbGeoscapeFlag      = &pW->geo.time.bPaused;       /* bGameClockPaused @ 0x15961C */
    pE->pnRadarPulseTimer   = &pW->nRadarPulseTimer;       /* 0xAC224, set to 250 at 0x84164 */
    pE->pnFunding           = &pW->economy.aZero44_49[5];  /* g_nFundPending @ 0x1865BC */
    pE->pbBaseCount         = (u8 *)&pW->nBaseCount;       /* 0x159D80, read SIGNED at 0x84214 */
    pE->pbSelectedBaseIdx   = (u8 *)&pW->nSelectedBase;    /* 0x159D81, written at 0x84130 */
    pE->abFacilityGrid      = pW->abFacilityGrid;
    pE->abTileTypes         = pW->abMapTileTypes;          /* 0x159D5C, filled at 0x84124 */
    pE->apSoldiers          = pW->apSoldiers;
    pE->nSoldierCount       = pW->nSoldierCount;
    pE->apObjects           = pS->apObjects;
    pE->nObjectCount        = (i32)pS->nObjectCount;
    pE->pView               = &pW->view;                   /* gSmapCam @ 0x1863E8 */
    pE->apOverlay[0]        = pW->ui.apPad[GEOUI_PAD_186C28]; /* the three zoom overlays */
    pE->apOverlay[1]        = pW->ui.apPad[GEOUI_PAD_186C1C];
    pE->apOverlay[2]        = pW->ui.apPad[GEOUI_PAD_186BB8];
    pE->pTilemapAlt         = pW->ui.apPad[GEOUI_PAD_186C18]; /* unk_186C18 */
    pE->nOverlayOriginX     = pW->frame.g.nOverlayOriginX;  /* 0x186C20 */
    pE->pShipEnv            = pS;
    pE->pbSpawnOnceFlag     = &pW->bSpawnOnce;             /* 0x162028 */
    pE->pSpawnCtx           = &pW->spawnCtx;
    pE->pDemolishEnv        = pD;                          /* 0x84208 */
    pE->pfnFreeGeoscapeUi   = World_MissionSeamFreeUi;
    pE->pfnInitMission      = World_MissionSeamInitMission;
    pE->pfnInitEndScreen    = World_MissionSeamInitEnd;
    pE->pHookUser           = pW;
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
    pW->geoHooks.pfnSpawnMissionSite        = World_HookSpawnMissionSite;     /* 0x5d28c in Geoscape_TickHourly */
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
     * the screens the shell can reach (1, 2, 4, 19, and 20) are wired, and so is case 20's mission-follows
     * branch: Map_bMissionMode (0x8BB20) writes into the shell's own Map cell and 0x8BB24 hands over to
     * Smap_BeginCraftSelect through pW->craftEnv. What is NOT: the base-info, hangar, research,
     * manufacturing, transfer, buy/sell and ufopaedia blocks (their cases keep NULL gates, which the
     * dispatcher tests), case 19's bankruptcy tail (its two callees, listed with the wiring below -- the
     * Map pair it sets IS written), and the byte_158834 / byte_1870F4 base-index pair case 19 writes on
     * its psi branch. */
    GeoscapeCloseEnv *pC = &pW->closeEnv;

    memset(pC, 0, sizeof(*pC));
    pC->pfnPlaySample    = pW->pfnPlaySample;
    pC->pSoundUser       = pW->pSoundUser;
    pC->pnActionFail     = (i32 *)&pW->nShortfall1870C8;   /* dwActionFail @ 0x1870C8, cleared as a dword */
    pC->pbPopupA         = &pW->bRoot34;                   /* bBaseFacPreviewActive @ 0x18652C */
    pC->pbPopupB         = &pW->bRoot35;                   /* bBaseFacPreviewOk @ 0x18652D */
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
    pC->pfnBaseAtOpen    = World_BaseAtOpenHook;          /* case 20's Open(&0x158840, -1) at 0x8BB00 */
    /* Case 20's mission-follow branch (0x8BB10) is live: the shell owns the Map cells the two
     * branches write, and Smap_BeginCraftSelect gets a full env from World_WireMissionSeams. */
    pC->pbBaseAtAssault  = &pW->bBaseAtMissionFollows;     /* 0x158850, tested at 0x8BB10 */
    pC->pbMissionMode    = &pW->bMapMissionMode;           /* Map_bMissionMode @ 0x128AD5, set at 0x8BB20 */
    pC->pbTickHold       = &pW->bMapTickHold;              /* 0x113A12, set by case 19 at 0x8BC04 */
    pC->pbBattleEnded    = &pW->bMapBattleEnded;           /* 0x113A14, set by case 19 at 0x8BC0C */
    pC->pCraftSelect     = &pW->craftEnv;                  /* 0x8BB24 */
    /* pfnFreeGeoscapeUi and pEndScreen stay NULL here, and they gate case 19's bankruptcy tail
     * (0x8BBF0, 0x8BBF8): GeoscapeUi_FreeAll's FreeWorld half releases the rosters World_Free
     * releases, and EndScreen_Init wants four globals the shell has no owner of plus an ending
     * flow to hand control to. The Map pair above IS written either way -- the bankruptcy
     * markers land before the two hooks -- so the state the original records is there. */
    pC->pUser            = pW;
    World_WireMissionSeams(pW);
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
    pF->placement.pfnMapClick    = World_MapClickThunk;            /* Geoscape_OnMapClick_Thunk @ 0x857e8 */

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

    /* Case 20 (0x944B0), the base-attack report. The callee takes no argument: it reads the class byte
     * 0x158840, the pad 0x15884C and the panel 0x15885C -- so the arm is wired to the shell's own record,
     * and the `LDR R0, =nBaseAtClass` at 0x944B0 is the dead argument the IDB comment names. The env is
     * rebuilt by World_WireGeoscapeFrame, but the panel and pad pointers are read through the slots, so a
     * screen opened between two wirings still draws what Update built. */
    pF->baseAtDraw.pColor            = pColor;
    pF->baseAtDraw.pbActive          = &pW->bBaseAtActive;             /* 0x158864, the gate at 0x58730 */
    pF->baseAtDraw.ppPad             = &pW->pBaseAtPad;                /* 0x15884C */
    pF->baseAtDraw.ppPanel           = &pW->pBaseAtPanel;              /* 0x15885C */
    pF->baseAtDraw.pnLineLimit       = &pW->nBaseAtLineLimit;          /* 0x158858 */
    pF->baseAtDraw.pnRadarCooldownR  = &pW->frame.g.nTapCooldown;      /* nRadarCooldownR @ 0xAC218 */
    pF->baseAtDraw.pbConfirmShown    = &pW->bBaseAtConfirmShown;       /* 0x158854, set at 0x588B0 */
    pF->baseAtDraw.pCtlConfirm       = pUi->apCtl[GEOUI_CTL_186BA4];   /* 0x186BA4, centred at (120,290) */
    pF->draw.pBaseAtEnv              = &pF->baseAtDraw;                /* 0x944B4, inside the fall-through */
    if (pAssets != NULL) {
        pF->baseAtDraw.pTextRecs = (const char *)pAssets->pTextRecs;   /* TEXTS[1127] "ОК" at 0x588C4 */
    }

    /* The two nav openers the shell can serve, held on the state because a click runs them. */
    pW->newBaseOpen.pfnPlaySample  = World_PlaySampleWithArg;         /* Sound_Play(17, 4) at 0x615dc */
    pW->newBaseOpen.pCtlFrame      = pUi->apCtl[GEOUI_CTL_186B00];    /* to (224,1) at 0x61608/0x61614 */
    pW->newBaseOpen.pCtlConfirm    = pUi->apCtl[GEOUI_CTL_186BA4];    /* 0x6162C installs the confirm handler */
    pW->newBaseOpen.pView          = &pW->view;                       /* the two Smap_ZoomOut at 0x61634/0x6163C */
    pW->newBaseOpen.pZoomSets      = pW->sets;
    pW->newBaseOpen.pWorldOut      = pUi->pWorldSize;
    pW->newBaseOpen.pbBaseCount    = &pW->nBaseCount;                 /* the != 8 gate at 0x615F4 */
    pW->newBaseOpen.pbFlag187080   = &pW->bUiAnimStarted;             /* cleared at 0x6164C */
    pW->newBaseOpen.pfnTryConfirm  = World_ConfirmClick;           /* 0x6162C installs TryConfirm, not the bare click */
    pW->newBaseOpen.pfnCloseThunk  = World_NewBaseCloseThunk;         /* 0x61654 */
    pW->newBaseOpen.pUser          = pW;                              /* the sample adapter's world pointer */

    pW->facOpenHooks.pfnPlaySample = World_PlaySampleWithArg;         /* Sound_Play(17, 4) at 0x61850 */
    pW->facOpenHooks.pfnInit       = World_BaseFacInitThunk;          /* the tail call to 0x5a198 */
    pW->facOpenHooks.pSlot         = NULL;
    pW->facOpenHooks.pUser         = pW;

    /* The frame end, 0x944B8..0x94544. Its modal arm runs only for MainMenu_nListMode 1103/1102, the
     * start menu's own modal pair, so the modal anim and layout stay NULL on the geoscape path. The
     * dispatcher copies this env into its own and sets pBaseAtEnv only for id 20 (0x944B4), so the draw
     * env lives on GeoscapeDrawEnv above rather than here. */
    pF->frameEnd.pnListMode         = &pW->nListMode;              /* MainMenu_nListMode @ 0x157188 */
    pF->frameEnd.pnActionPhaseSave112 = &pW->sModal.nActionPhaseSave112;  /* Map.anActionPhaseSave0[112]
                                                                            * @ 0x128AA0, one cell: the frame
                                                                            * tail writes it (0x944E0/0x9451C) and
                                                                            * UiModal_Begin clears it (0x2644C) */
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
        pF->baseAtDraw.pfnCtlBlit     = pHit->pfnBlit;
        pF->baseAtDraw.pfnCtlBlitEx   = pHit->pfnBlitEx;
        pF->baseAtDraw.pCtlUser       = pHit->pUser;
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
    pPtr->pbPopupA   = &pW->bRoot34;                    /* bBaseFacPreviewActive @ 0x18652C */
    pPtr->pbPopupB   = &pW->bRoot35;                    /* bBaseFacPreviewOk @ 0x18652D */
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
    /* The pointer dispatcher dereferences the dialog panel with no test in both arms (0x87F60 on the press,
     * 0x89F10 on the release), and the overlay's commit frees that panel through the closing Toggle, so the
     * dispatcher's copy follows it exactly like the dialog's own. */
    pW->ptrEnv.pFacPanel = pW->pFacPanel;
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
