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
#include "util/pufo_str.h"     /* pufo_strncpy, the banner's two 16-byte text copies */
#include "game/pufo_roster.h"  /* Roster_CycleNext/Select*, the tact nav widgets (Part 54) */
#include "game/pufo_tacthud.h" /* TactHud_OpenInventory (0x16ef4), the inventory the mission opens (Part 55) */
#include "game/pufo_tactctl.h" /* Map_BuildDeployConfirmYesNo @ 0x18a68, the deploy confirm the shell wires (Part 58) */
#include "game/pufo_menu.h"    /* Menu_ResizeWindow @ 0x19930 (widget 58), the last deferred handler (Part 61) */
#include "game/pufo_renderframe.h" /* Render_Frame @ 0x22310 + the frame envs, the tactical frame (Part 63) */
#include "game/pufo_invdrag.h"     /* Inv_CloseAndCommit @ 0x15a48, the inventory close that deploys the team (Part 66) */

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

/* BaseInfoScreen_Init @ 0x57688, the Init hook of BaseInfoScreen_Open (reached at 0x61690). It allocates
 * the pad and the two controls into the shell's fields; its tail call is BuildPanels (0x5779C). */
static void World_BaseInfoInitThunk(void *pSlot, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    (void)pSlot; /* the original passes its own slot global; the port takes the env instead */
    if (pW != NULL) {
        BaseInfoScreen_Init(&pW->baseInfoInit);
    }
}

/* BaseInfoScreen_BuildPanels @ 0x557f4: the build/release switch on bBaseInfoPanelsBuilt @ 0x15883C. */
static void World_BaseInfoBuildThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BaseInfoScreen_BuildPanels(&g_pActiveWorld->baseInfoBuild);
    }
}

/* The same switch through the close env's hook: Geoscape_CloseUI case 3 calls it at 0x8BDF8 when the
 * screen closes with the panels still built. */
static void World_BaseInfoBuildHook(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW != NULL) {
        BaseInfoScreen_BuildPanels(&pW->baseInfoBuild);
    }
}

/* BaseInfoScreen_OnOk @ 0x557c8, the (1,300) control's click: byte_1870F8 := 1, then byte_158834 :=
 * byte_1870F4 read SIGNED (0x557DC) and stored as a byte (0x557E0). */
static void World_BaseInfoOkThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    BaseInfoOkState st;

    if (pW == NULL) {
        return;
    }
    st.nPendingBase = pW->nBaseInfoWorkBase;   /* byte_1870F4 */
    st.bPsiPending  = pW->bBaseInfoOverlayOpen; /* byte_1870F8 */
    st.nCurrentBase = pW->nShownBaseIdx;       /* byte_158834 */
    (void)BaseInfoScreen_OnOk(&st);
    pW->bBaseInfoOverlayOpen = st.bPsiPending;
    pW->nShownBaseIdx        = st.nCurrentBase;
}

/* BaseInfoScreen_TogglePanelMode @ 0x55754, the (120,300) control's click. */
static void World_BaseInfoToggleThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BaseInfoScreen_TogglePanelMode(&g_pActiveWorld->baseInfoToggle);
    }
}

/* BaseInfoScreen_Open @ 0x6167c -- nav control slot 1's handler (installed at 0x67B7C): sample 17, the
 * Init at 0x61690, and only then nGeoscapeScreen := 3 (0x6169C), so the opener reuses the shared
 * Geoscape_OpenScreen with bIdAfterInit true, exactly as Manuf and Transfer do. */
static void World_BaseInfoOpenThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BaseInfoScreen_Open(&g_pActiveWorld->screen, &g_pActiveWorld->baseInfoOpenHooks);
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

/* ---- the hangar (screen 6) ------------------------------------------------------------------------
 *
 * BasePlaneScreen_Open @ 0x617f4 is nav control slot 3's handler; from there every control of the screen
 * is one of the thunks below. The original reaches its own globals from inside each handler; these reach
 * the shell's copy of the same objects, held on PufoWorldState.
 */

/* Geoscape_CloseUI @ 0x8b94c through the env-shaped hook the two hangar closers take as pfnCloseUi
 * (BasePlaneScreen_Close 0x7c9e8 and BuySellScreen_ApplyQtyChanges 0x78d90 both tail-call it). */
static void World_CloseGeoscapeHook(void *pUser)
{
    if (pUser != NULL) {
        World_CloseGeoscape((PufoWorldState *)pUser);
    }
}

/* BasePlaneScreen_DrawStatLabelA @ 0x79e00 / DrawStatLabelB @ 0x79fc4: the two stat-panel controls.
 * Drawing a panel is what marks it open, which is why BasePlane_RearmFromStores and
 * BasePlane_LoadWeaponStores have gates to consult. */
static void World_PlaneStatLabelAThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW == NULL) {
        return;
    }
    BasePlaneScreen_DrawStatLabel(&pW->planeStatPanels, 0,
                                  (pW->pAssets != NULL) ? pW->pAssets->pTextRecs : NULL,
                                  (pW->pFrameHooks != NULL) ? pW->pFrameHooks->pColor : NULL,
                                  pW->nPlaneStatRows);
}

static void World_PlaneStatLabelBThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW == NULL) {
        return;
    }
    BasePlaneScreen_DrawStatLabel(&pW->planeStatPanels, 1,
                                  (pW->pAssets != NULL) ? pW->pAssets->pTextRecs : NULL,
                                  (pW->pFrameHooks != NULL) ? pW->pFrameHooks->pColor : NULL,
                                  pW->nPlaneStatRows);
}

/* BasePlaneScreen_BuildListPanels @ 0x7a3f4: the crew pair and its nav pair, on bPlaneListPanelsBuilt. */
static void World_PlaneBuildListThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BasePlaneScreen_BuildListPanels(&g_pActiveWorld->planeListPanels, &g_pActiveWorld->planeList);
    }
}

/* BasePlaneScreen_BuildControls @ 0x7b014: the stores-transfer set, on bPlaneStoresCommitted. */
static void World_PlaneBuildControlsThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BasePlaneScreen_BuildControls(&g_pActiveWorld->planeStorePanels, &g_pActiveWorld->planeStoreBuild);
    }
}

/* BasePlaneScreen_ApplyWeaponSwap @ 0x79748: the swap the two weapon selections end in, and the reason
 * the six SelectWeapon handlers can share it. */
static i32 World_PlaneApplySwapThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW == NULL) {
        return 0;
    }
    return BasePlaneScreen_ApplyWeaponSwap(&pW->planeStatPanels, &pW->planeWeapon);
}

/* BasePlane_RearmFromStores @ 0x78ed0: the stat block's first control. */
static void World_PlaneRearmThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlane_RearmFromStores(&g_pActiveWorld->planeWeapon, &g_pActiveWorld->planeStatPanels);
    }
}

/* BasePlaneScreen_SelectWeapon25 @ 0x79b20 .. SelectWeapon30 @ 0x79c74: the six stat controls, one per
 * weapon slot, each a tail call into the swap. */
static void World_PlaneSelectWeapon25Thunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_SelectWeapon(&g_pActiveWorld->planeStatPanels, 25,
                                           World_PlaneApplySwapThunk, g_pActiveWorld);
    }
}

static void World_PlaneSelectWeapon26Thunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_SelectWeapon(&g_pActiveWorld->planeStatPanels, 26,
                                           World_PlaneApplySwapThunk, g_pActiveWorld);
    }
}

static void World_PlaneSelectWeapon27Thunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_SelectWeapon(&g_pActiveWorld->planeStatPanels, 27,
                                           World_PlaneApplySwapThunk, g_pActiveWorld);
    }
}

static void World_PlaneSelectWeapon28Thunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_SelectWeapon(&g_pActiveWorld->planeStatPanels, 28,
                                           World_PlaneApplySwapThunk, g_pActiveWorld);
    }
}

static void World_PlaneSelectWeapon29Thunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_SelectWeapon(&g_pActiveWorld->planeStatPanels, 29,
                                           World_PlaneApplySwapThunk, g_pActiveWorld);
    }
}

static void World_PlaneSelectWeapon30Thunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_SelectWeapon(&g_pActiveWorld->planeStatPanels, 30,
                                           World_PlaneApplySwapThunk, g_pActiveWorld);
    }
}

/* BasePlaneScreen_Close @ 0x7c9e8: the (224,1) control Init wires at 0x7DDCC. It hands the items a
 * committed screen took back to the base, then tail-calls the close dispatcher. */
static void World_PlaneCloseThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BasePlaneScreen_Close(&g_pActiveWorld->planeClose);
    }
}

/* BuySellScreen_ApplyQtyChanges @ 0x78d90: the confirm control (0x186BA4) Init wires at 0x7DDDC. It moves
 * every staged anSlotCarryCount entry between the selected base and the selected craft and closes. */
static void World_PlaneApplyQtyThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BuySellScreen_ApplyQtyChanges(&g_pActiveWorld->planeApply);
    }
}

/* BasePlaneScreen_SelectCraft @ 0x7cbc8 / SelectListed @ 0x7cc3c: the two craft-list columns. SelectCraft
 * additionally arms the detail view when the picked craft is at base. */
static void World_PlaneSelectCraftThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_SelectCraft(&g_pActiveWorld->planeSelect);
    }
}

static void World_PlaneSelectListedThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_SelectListed(&g_pActiveWorld->planeSelect);
    }
}

/* BasePlaneScreen_FillCrewPanel @ 0x7a188: the crew row's click -- assign the row's soldier to the
 * selected craft, or let it go. */
static void World_PlaneCrewClickThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BasePlaneScreen_FillCrewPanel(&g_pActiveWorld->planeCrew);
    }
}

/* BuySellScreen_ListPanelPrev @ 0x79a20 / ListPanelNext @ 0x79a48: the crew pair, unk_161EDC first and
 * unk_161A3C second. */
static void World_PlaneListPrevThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BuySellScreen_ListPanelPrev(g_pActiveWorld->planeListPanels.pCrewPanel,
                                    g_pActiveWorld->planeListPanels.pCrewStatusPanel);
    }
}

static void World_PlaneListNextThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BuySellScreen_ListPanelNext(g_pActiveWorld->planeListPanels.pCrewPanel,
                                    g_pActiveWorld->planeListPanels.pCrewStatusPanel);
    }
}

/* BuySellScreen_DetailPanelPrev @ 0x79a70 / DetailPanelNext @ 0x79ac8: the hangar's five stores panels, in
 * the order the handler walks them -- slot, sel-row, commit-row, taken, avail. */
static void World_PlaneStorePrevThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    UiPanel *apPanels[5];

    if (pW == NULL) {
        return;
    }
    apPanels[0] = pW->planeStorePanels.pSlotPanel;
    apPanels[1] = pW->planeStorePanels.pSelRowPanel;
    apPanels[2] = pW->planeStorePanels.pCommitPanel;
    apPanels[3] = pW->planeStorePanels.pTakenPanel;
    apPanels[4] = pW->planeStorePanels.pAvailPanel;
    BuySellScreen_DetailPanelPrev(apPanels);
}

static void World_PlaneStoreNextThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;
    UiPanel *apPanels[5];

    if (pW == NULL) {
        return;
    }
    apPanels[0] = pW->planeStorePanels.pSlotPanel;
    apPanels[1] = pW->planeStorePanels.pSelRowPanel;
    apPanels[2] = pW->planeStorePanels.pCommitPanel;
    apPanels[3] = pW->planeStorePanels.pTakenPanel;
    apPanels[4] = pW->planeStorePanels.pAvailPanel;
    BuySellScreen_DetailPanelNext(apPanels);
}

/* Cache_ObjPlus0xBD8_To_dword_186BE8 @ 0x7afec: the slot column's click caches its own row, and the two
 * quantity handlers and the hovered object line read that cache back. */
static void World_PlaneSlotClickThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW == NULL || pW->planeStorePanels.pSlotPanel == NULL) {
        return;
    }
    (void)BaseUi_Cache186BE8(&pW->planeScratch, pW->planeStorePanels.pSlotPanel->nUnk3032);
}

/* BasePlaneScreen_AdjustSelectedQty @ 0x7a9b4 (the "<" column) and BasePlaneScreen_CommitStores @ 0x7ac38
 * (the ">"): the undo and the staging of one row. */
static void World_PlaneAdjustQtyThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_AdjustSelectedQty(&g_pActiveWorld->planeStores);
    }
}

static void World_PlaneCommitStoresThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)BasePlaneScreen_CommitStores(&g_pActiveWorld->planeStores);
    }
}

/* BasePlaneScreen_Init @ 0x7d9e4, the Init hook of the opener (the BL at 0x617B8... 0x61828 writes the
 * screen id first, at 0x61828 before the Init). */
static void World_PlaneInitThunk(void *pSlot, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    (void)pSlot; /* the original passes its own block global; the port takes the env instead */
    if (pW != NULL) {
        BasePlaneScreen_Init(&pW->planeInit);
    }
}

/* BasePlaneScreen_Open @ 0x617f4 -- nav control slot 3's handler (installed at 0x67B98): sample 17, the
 * id 6 written BEFORE the Init (0x61828), then the tail call into Init. */
static void World_PlaneOpenThunk(void)
{
    if (g_pActiveWorld != NULL) {
        BasePlaneScreen_Open(&g_pActiveWorld->screen, &g_pActiveWorld->planeOpenHooks);
    }
}

/* Geoscape_CloseUI case 6's three rebuild hooks (0x8C028, 0x8C044, 0x8C098): each is the same builder the
 * screen's own control runs, so the close walks the same build/release switch. */
static void World_PlaneBuildListHook(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW != NULL) {
        BasePlaneScreen_BuildListPanels(&pW->planeListPanels, &pW->planeList);
    }
}

static void World_PlaneBuildControlsHook(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW != NULL) {
        BasePlaneScreen_BuildControls(&pW->planeStorePanels, &pW->planeStoreBuild);
    }
}

static void World_PlaneBuildPanelsHook(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW != NULL) {
        BasePlaneScreen_BuildPanels(&pW->planeCraftPanels, &pW->planeBuild);
    }
}

/* ---- the research screen (screen 8) ----------------------------------------------------------------
 *
 * ResearchScreen_Open @ 0x616ac is nav control slot 5's handler (installed at 0x67BAC). The original
 * reaches its pad, its four controls, its three panels and its two row cells through loose globals -- the
 * pad and the controls inside the topic table itself -- and these thunks reach the shell's copies held on
 * PufoWorldState. World_HookRebuildResearchTopics (the clock's copy of the same env) is defined below with
 * the tick hooks; the two seams here share it.
 */
static i32 World_HookRebuildResearchTopics(void *pUser);

/* ResearchScreen_Init @ 0x749a8, the opener's Init hook (the tail call at 0x61778). */
static void World_ResearchInitThunk(void *pSlot, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    (void)pSlot; /* the original passes the topic table as its argument; the port takes the env instead */
    if (pW != NULL) {
        ResearchScreen_Init(&pW->researchInit);
    }
}

/* ResearchScreen_Open @ 0x616ac -- nav control slot 5 (0x67BAC): sample 17 at 0x616B4, the prologue (the
 * two scratch clears, the research-active walk, the shortfall dword, GameTime_Set5Sec), id 8 at 0x6176C,
 * then the Init. No prep and no nav rebuild here -- those belong to Ui_OpenResearch @ 0x84bf8. */
static void World_ResearchOpenThunk(void)
{
    if (g_pActiveWorld != NULL) {
        ResearchScreen_Open(&g_pActiveWorld->researchOpen);
    }
}

/* ResearchScreen_BuildPanels @ 0x733f4: the build/release switch. This is Init's own call at 0x74BB8, and
 * Init then tail-calls RefreshPanels with the three panel values -- which the ORIGINAL re-reads from
 * 0x160C68 / 0x15EEFC / 0x15EF00 after the build. The port's Init carries them as values, so the switch's
 * result is mirrored back into the same env Init is holding. */
static void World_ResearchBuildThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW == NULL) {
        return;
    }
    ResearchScreen_BuildPanels(&pW->researchBuild);
    pW->researchInit.pPanelTopic    = pW->pResearchTopicPanel;
    pW->researchInit.pPanelQty      = pW->pResearchQtyPanel;
    pW->researchInit.pPanelProgress = pW->pResearchProgPanel;
}

/* ResearchScreen_RebuildTopicList @ 0x74270: Init's call at 0x74B00 and StartSelected's tail at 0x73310. */
static void World_ResearchRebuildThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)World_HookRebuildResearchTopics(g_pActiveWorld);
    }
}

/* ResearchScreen_PanelPrev @ 0x72e30 / PanelNext @ 0x72e68: the topic list's scroll pair, paging the three
 * panels together. */
static void World_ResearchPanelPrevThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW != NULL) {
        ResearchScreen_PanelPrev(pW->pResearchTopicPanel, pW->pResearchQtyPanel, pW->pResearchProgPanel);
    }
}

static void World_ResearchPanelNextThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW != NULL) {
        ResearchScreen_PanelNext(pW->pResearchTopicPanel, pW->pResearchQtyPanel, pW->pResearchProgPanel);
    }
}

/* ResearchScreen_IncSelectedQty @ 0x72ea0 and DecSelectedQty @ 0x72f70: the quantity pair, which writes the
 * selected row's auResearchCol entry through the cached row and the scientist/lab budget. */
static void World_ResearchIncQtyThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)ResearchScreen_IncSelectedQty(&g_pActiveWorld->researchQty);
    }
}

static void World_ResearchDecQtyThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)ResearchScreen_DecSelectedQty(&g_pActiveWorld->researchQty);
    }
}

/* ResearchScreen_UpdateProgress @ 0x72d14: unk_186B94, the allocate-all control the draw re-points at
 * (100,164) on every frame (0x74184..0x741A4). */
static void World_ResearchAllocAllThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)ResearchScreen_UpdateProgress(&g_pActiveWorld->researchQty);
    }
}

/* ResearchScreen_StartSelected @ 0x73240: pGeoscapeCtlB at (165,166). It clears the row flag, commits the
 * allocation, and tail-calls the topic rebuild. */
static void World_ResearchStartThunk(void)
{
    if (g_pActiveWorld != NULL) {
        (void)ResearchScreen_StartSelected(&g_pActiveWorld->researchStart);
    }
}

/* ResearchScreen_CancelSelectedAndClose @ 0x73380: pGeoscapeCtlA at (165,188), which drops one row's
 * allocation and tail-calls Geoscape_CloseUI @ 0x8b94c. */
static void World_ResearchCancelThunk(void)
{
    if (g_pActiveWorld != NULL) {
        ResearchScreen_CancelSelectedAndClose(&g_pActiveWorld->researchCancel);
    }
}

/* ResearchScreen_CloseThunk @ 0x72ffc: the shared close control at (224,1). */
static void World_ResearchCloseThunk(void)
{
    if (g_pActiveWorld != NULL) {
        ResearchScreen_CloseThunk(&g_pActiveWorld->closeEnv);
    }
}

/* PlaySample7_SetFlags_Cache0xBD8 @ 0x73328: the topic list's row click. It plays sample 7, sets
 * bUiRowSelected, clears bUiAnimStarted, and caches the panel's nUnk3032 into nResearchTopicRowCache --
 * the row the three quantity controls and the two bottom controls then read. */
static void World_ResearchPlaySample(u32 nSample, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    if (pW != NULL && pW->pfnPlaySample != NULL) {
        pW->pfnPlaySample((i32)nSample, pW->pSoundUser);
    }
}

static void World_ResearchTopicClickThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW == NULL || pW->pResearchTopicPanel == NULL) {
        return; /* deviation: 0x73350 derefs the panel global with no test */
    }
    /* 0x73328 hard-codes the sample (MOV R0,#7 / MOV R1,#4), which is what the port's helper passes. */
    (void)BaseUi_PlaySample7Cache160C74(&pW->researchScratch, pW->pResearchTopicPanel->nUnk3032,
                                        World_ResearchPlaySample, pW);
}

/* The options screen's sample: Sound_Play(7, 4) at 0x6CF08 / 0x6DD10. The second argument is dead
 * (Sound_Play takes the sample only), which is why the port keeps it for the record. */
static void World_OptionsPlaySample(u32 nSample, u32 nArg, void *pUser)
{
    PufoWorldState *pW = (pUser != NULL) ? (PufoWorldState *)pUser : g_pActiveWorld;

    (void)nArg;
    if (pW != NULL && pW->pfnPlaySample != NULL) {
        pW->pfnPlaySample((i32)nSample, pW->pSoundUser); /* the caller's seam, as every other geoscape
                                                          * handler takes it (World_Hook* / the close env) */
        return;
    }
    (void)Sound_Play((i32)nSample);
}

/* OptionsScreenHooks' two slots. `OptionsScreen_RebuildWithSound @ 0x6cf00` tail-calls
 * OptionsScreen_Init at 0x6CF14 and `OptionsScreen_OpenWithSound @ 0x6dd04` tail-calls
 * OptionsScreen_Open at 0x6DD20 with the mode it kept in R4; these are those two tail calls. */
static void World_OptionsInitRun(void *pUser)
{
    PufoWorldState *pW = (pUser != NULL) ? (PufoWorldState *)pUser : g_pActiveWorld;

    if (pW != NULL) {
        OptionsScreen_Init(&pW->options.init);
    }
}

static void World_OptionsOpenRun(i32 nMode, void *pUser)
{
    PufoWorldState *pW = (pUser != NULL) ? (PufoWorldState *)pUser : g_pActiveWorld;

    if (pW != NULL) {
        OptionsScreen_Open(&pW->options.open, nMode);
    }
}

/* The four menu rows OptionsScreen_LayoutButtons installs (0x6B6DC..0x6B700), the cancel hotspot
 * (0x6BF1C / 0x6D24C), and the twelve radio buttons (0x6C02C..0x6C0B0). UiControl.pfnClick takes no
 * arguments, so each reaches the live state through g_pActiveWorld -- the route every other geoscape
 * control handler in this file takes. */
static OptionsVolumeEnv *World_OptionsVolEnv(void)
{
    return (g_pActiveWorld != NULL) ? &g_pActiveWorld->options.vol : NULL;
}

static void World_OptionsSaveTab(void)
{
    if (g_pActiveWorld != NULL) {
        OptionsScreen_BindSaveSlots(&g_pActiveWorld->options.tab); /* 0x6b49c */
    }
}

static void World_OptionsLoadTab(void)
{
    if (g_pActiveWorld != NULL) {
        OptionsScreen_BindLoadSlots(&g_pActiveWorld->options.tab); /* 0x6b588 */
    }
}

static void World_OptionsQuitTab(void)
{
    if (g_pActiveWorld != NULL) {
        BaseUi_SetScratch3AndUiY(&g_pActiveWorld->abOptionsScratch[0],
                                 g_pActiveWorld->ui.apCtl[GEOUI_CTL_186BB0],  /* *pGeoscapeCtlB, y = 120 */
                                 g_pActiveWorld->ui.apCtl[GEOUI_CTL_186BB4]); /* *pGeoscapeCtlA, y = 142 */
    }
}

static void World_OptionsVolumeTab(void)
{
    if (g_pActiveWorld != NULL) {
        BaseUi_SetScratch4(&g_pActiveWorld->abOptionsScratch[0]); /* 0x6b6b0 */
    }
}

static void World_OptionsCancelClick(void)
{
    if (g_pActiveWorld != NULL) {
        (void)Options_SaveAudioSettings(&g_pActiveWorld->options.saveAudio); /* 0x6b7ac */
    }
}

static void World_OptionsMusicVol0(void) { (void)OptionsScreen_SetMusicVol0(World_OptionsVolEnv()); }
static void World_OptionsMusicVol1(void) { (void)OptionsScreen_SetMusicVol1(World_OptionsVolEnv()); }
static void World_OptionsMusicVol2(void) { (void)OptionsScreen_SetMusicVol2(World_OptionsVolEnv()); }
static void World_OptionsMusicVol3(void) { (void)OptionsScreen_SetMusicVol3(World_OptionsVolEnv()); }
static void World_OptionsMusicVol4(void) { (void)OptionsScreen_SetMusicVol4(World_OptionsVolEnv()); }
static void World_OptionsMusicVol5(void) { (void)OptionsScreen_SetMusicVol5(World_OptionsVolEnv()); }
static void World_OptionsSfxVol0(void) { (void)OptionsScreen_SetSfxVol0(World_OptionsVolEnv()); }
static void World_OptionsSfxVol1(void) { (void)OptionsScreen_SetSfxVol1(World_OptionsVolEnv()); }
static void World_OptionsSfxVol2(void) { (void)OptionsScreen_SetSfxVol2(World_OptionsVolEnv()); }
static void World_OptionsSfxVol3(void) { (void)OptionsScreen_SetSfxVol3(World_OptionsVolEnv()); }
static void World_OptionsSfxVol4(void) { (void)OptionsScreen_SetSfxVol4(World_OptionsVolEnv()); }
static void World_OptionsSfxVol5(void) { (void)OptionsScreen_SetSfxVol5(World_OptionsVolEnv()); }

/* The overlay's own three handlers. OptionsScreen_Open installs these on its two scroll controls
 * (0x6D208 / 0x6D214) and on the names panel (0x6D2E8); all four panels live in one array here
 * (0x159E18, 0x159E78, 0x159E7C, 0x159E1C), which is the order OptionsScreen_PanelPrev/Next page. */
static void World_OptionsPanelPrev(void)
{
    if (g_pActiveWorld != NULL) {
        OptionsScreen_PanelPrev(g_pActiveWorld->options.apPanel); /* 0x6d01c */
    }
}

static void World_OptionsPanelNext(void)
{
    if (g_pActiveWorld != NULL) {
        OptionsScreen_PanelNext(g_pActiveWorld->options.apPanel); /* 0x6d064 */
    }
}

static void World_OptionsConfirmClick(void)
{
    if (g_pActiveWorld != NULL) {
        OptionsScreen_OnConfirm(&g_pActiveWorld->options.confirm); /* 0x6cf18 */
    }
}

/* Geoscape_DrawScreen's 0x6C188 tail call: bMapSelectionFlag set runs Geoscape_CloseUI(&0x1864F8) and
 * draws nothing else this frame. */
static void World_OptionsCloseUiThunk(void *pUser)
{
    PufoWorldState *pW = (pUser != NULL) ? (PufoWorldState *)pUser : g_pActiveWorld;

    if (pW != NULL) {
        Geoscape_CloseUI(&pW->closeEnv);
    }
}

/* GeoscapeScreenHooks' sample slot: Ui_OpenScreen18 0x84EA4 and Ui_CloseScreen18 0x84ED4 both play
 * sample 17 on their way to the options screen. */
static void World_ScreenPlaySample(u32 nSample, void *pUser)
{
    PufoWorldState *pW = (pUser != NULL) ? (PufoWorldState *)pUser : g_pActiveWorld;

    if (pW != NULL && pW->pfnPlaySample != NULL) {
        pW->pfnPlaySample((i32)nSample, pW->pSoundUser);
        return;
    }
    (void)Sound_Play((i32)nSample);
}

/* GeoscapeScreenHooks' two slots: the shell's way back into the two Options*_WithSound wrappers, each
 * of which plays sample 7 and then tail-calls Init or Open on the shell's own envs. */
static void World_OptionsRebuildThunk(void *pUser)
{
    PufoWorldState *pW = (pUser != NULL) ? (PufoWorldState *)pUser : g_pActiveWorld;

    if (pW != NULL) {
        OptionsScreen_RebuildWithSound(&pW->optionsHooks);
    }
}

static void World_OptionsOpenWithSoundThunk(i32 nMode, void *pUser)
{
    PufoWorldState *pW = (pUser != NULL) ? (PufoWorldState *)pUser : g_pActiveWorld;

    if (pW != NULL) {
        OptionsScreen_OpenWithSound(0, nMode, &pW->optionsHooks);
    }
}

/* Root control [8] (the dword at 0x186518, the handler Geoscape_InitUI installs at 0x86590): the
 * geoscape's own options hotspot. Ui_OpenScreen18 plays sample 17, sets screen 18 and rebuilds. */
static void World_OptionsMenuClick(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW != NULL) {
        Ui_OpenScreen18(&pW->screen, &pW->options.screenHooks); /* 0x84e9c */
    }
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

    /* The original reads the roll's two scalars from globals inside the callee, and the port lifted
     * them into arguments, so the shell hands pointers to its own live cells: the row index is
     * nMonthCounter (0x81AB4) and the early gate is nAlienPoolAddend (0x81B7C). Neither is a
     * difficulty setting -- the clock writes both (FundingScreen_OpenMonthly @ 0x5D170,
     * Geoscape_TickBases @ 0x5D3BC) -- so a frozen pair would keep the alien class at 2 and every
     * weight row at 0 for the whole game. */
    memset(&pW->spawnCtx, 0, sizeof(pW->spawnCtx));
    pW->spawnCtx.pTables = &g_WorldRoll;
    pW->spawnCtx.pnDifficulty = &pW->nMonthCounter;
    pW->spawnCtx.pnEarlyGate = &pW->nAlienPoolAddend;
    pW->spawnCtx.nZoomMode = pW->view.nMode;
    pW->spawnCtx.pnSeed = &pW->nStrategSeed;
    pW->spawnCtx.pfnSpawnMission = World_SpawnMission;
    pW->spawnCtx.pfnProcessEvents = World_ProcessEvents;
    pW->spawnCtx.pUser = pW;

    /* The fifteen handlers Geoscape_BuildNavControls installs (0x67B74..0x67D28), one unrolled
     * `LDR R3,=fn / STR R3,[R0,#0xC]` per slot, in slot order 0..9 (0x67B74, 0x67B80, 0x67B8C, 0x67B98,
     * 0x67BA4, 0x67BB0, 0x67BBC, 0x67BC8, 0x67BD4, 0x67BE0). Six of the ten nav slots are filled: the
     * shell can draw the screen each one opens. The remaining four -- ManufScreen_Open (9),
     * TransferScreen_Open (10), BuySellScreen_OpenBuy (11) and BuySellScreen_OpenSell (12) -- stay no-ops:
     * their Init envs and their Geoscape_Draw arms (pManufPad, pTransferPad, pBuySellPad) are not built
     * here, so opening them would land on a screen the shell cannot draw. */
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {
        pW->navEnv.apfnNavClick[i] = World_NoopClick;
    }
    pW->navEnv.apfnNavClick[0] = World_NewBaseOpenThunk;   /* NewBaseScreen_Open @ 0x615d0, id 2 (0x67B74) */
    pW->navEnv.apfnNavClick[1] = World_BaseInfoOpenThunk;  /* BaseInfoScreen_Open @ 0x6167c, id 3 (0x67B80) */
    pW->navEnv.apfnNavClick[2] = World_SoldierToggleThunk; /* SoldierScreen_Toggle @ 0x64af0, id 4 (0x67B8C) */
    pW->navEnv.apfnNavClick[3] = World_PlaneOpenThunk;     /* BasePlaneScreen_Open @ 0x617f4, id 6 (0x67B98) */
    pW->navEnv.apfnNavClick[4] = World_BaseFacOpenThunk;   /* BaseFacScreen_Open @ 0x61844, id 7 (0x67BA4) */
    pW->navEnv.apfnNavClick[5] = World_ResearchOpenThunk;  /* ResearchScreen_Open @ 0x616ac, id 8 (0x67BAC) */
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
    env.pnStrategCount = &pW->saveStrateg.nStrategCount; /* 0x186530, the same cell Save_ReadStrateg
                                                           * fills and Save_WriteStrateg reads */
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
    pW->hangarEnv.abGridBytes = pW->abGridHangarBytes; /* the 0x159DBC byte table. It used to point at
                                                        * saveBases.abUnk159DBC, i.e. at the save record; the
                                                        * save join writes that field from abGridHangarBytes,
                                                        * so one array now holds the live table */
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
    /* Root control [8] -- the handler word at 0x186518, installed by Geoscape_InitUI at 0x86590 -- is the
     * geoscape's options hotspot: Ui_OpenScreen18 @ 0x84e9c. Slot [9] (0x18651C, Ui_CloseScreen18) stays
     * a no-op: its OpenWithSound tail reaches the overlay toggle through gUiRootPanel.anLineAux[119],
     * which has no shell owner yet (recorded in PLAN). */
    pEnv->apfnRootClick[8] = World_OptionsMenuClick;
    pEnv->pfnCraftSelectClick = World_NoopClick;
    pEnv->pfnRadarClick = World_RadarClickThunk; /* Geoscape_OnRadarListClick @ 0x854c4 */
    pEnv->pfnStartNew = World_StartNewHook;
    pEnv->pHookUser = pW;
    pEnv->pfnLoadPads = pW->pfnLoadPads;
    pEnv->pLoadPadsUser = pW->pLoadPadsUser;
}

/* ShipCtorEnv is the Map-block half of the craft create/destroy path, and Save_ReadShips reaches the
 * per-ship object columns through it (0x40C24's loop runs pEnv->nObjectCount times, the writer's loop
 * runs pIn->nObjectCount times). Both paths have to see the same object table, or the two counts drift
 * apart and every ship record after the first is read from the wrong offset. */
static void World_FillCtorEnv(PufoWorldState *pW, const BootAssets *pAssets)
{
    memset(&pW->ctorEnv, 0, sizeof(pW->ctorEnv));
    pW->ctorEnv.apObjects = pAssets->apObjects;
    pW->ctorEnv.nObjectCount = pAssets->nObjectCount;
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
    pW->optionsHooks.pfnPlaySample = World_OptionsPlaySample;
    pW->optionsHooks.pfnInit = World_OptionsInitRun;   /* OptionsScreen_Init @ 0x6be74, 0x6CF14's tail */
    pW->optionsHooks.pfnOpen = World_OptionsOpenRun;   /* OptionsScreen_Open @ 0x6d0ac, 0x6DD20's tail */
    pW->optionsHooks.pUser = pW;                       /* the two runs take the state, not g_pActiveWorld */

    World_FillCtorEnv(pW, pAssets);

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

    /* And for the base-info screen: the release arm (0x56F0C..0x56F90) frees the three panels and the two
     * controls and nulls all five slots; the pad is Geoscape_CloseUI case 3's free (0x8BE10), which the
     * shell owes when the game ends with the screen still open. */
    if (pW->bBaseInfoPanelsBuilt != 0) {
        World_BaseInfoBuildThunk();
    }
    if (pW->pBaseInfoPad != NULL) {
        PadImage_FreeData(pW->pBaseInfoPad);
        free(pW->pBaseInfoPad);
        pW->pBaseInfoPad = NULL;
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

/* The LoadGame teardown and bring-up hooks, all real since Part 65. Each callee was already ported --
 * Tact_Shutdown @ 0x3d568, Tact_InitMission @ 0x3c550, Geoscape_InitUI @ 0x857f4, GeoscapeUi_FreeAll @
 * 0x86d20 -- and what was missing was the env: the shell owns every one of them on the new-game path
 * (World_FillInitUiEnv builds the InitUI env, World_EndHookShutdown is the Tact_Shutdown mirror, and
 * the TactInitEnv is pW->tactEnv), so the same constructions run here. */
static void World_EndHookShutdown(u8 bKeepSome, void *pUser); /* forward: the mission-end seams define it */

static void Load_HookInitUi(i32 nMode, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GeoscapeInitEnv env;

    /* 0x44CBC / 0x44D18 both call Geoscape_InitUI(1) -- the rebuild arm, not a new run -- and the
     * frame envs then re-point at the singletons InitUI just made, exactly as World_MissionCancelEnd
     * does after its own rebuild. */
    World_FillInitUiEnv(pW, &env);
    Geoscape_InitUI(&env, nMode);
    if (pW->pFrameHooks != NULL) {
        World_WireGeoscapeFrame(pW, pW->pFrameHooks);
    }
}

static void Load_HookTactShutdown(void *pUser)
{
    (void)World_EndHookShutdown(0, pUser); /* Tact_Shutdown(0) @ 0x3D568, the same mirror the debrief cancel runs */
}

static bool Load_HookTactInitMission(i32 nMode, void *pUser)
{
    return World_InitTactical((PufoWorldState *)pUser, nMode); /* 0x44E24, mode 1, over the open slot file */
}

static void Load_HookFreeAll(void *pUser)
{
    /* 0x44D74: GeoscapeUi_FreeAll(0). The shell's own copy of that call lives in World_Free and passes
     * 1 (the roster half included); the load arm's argument 0 is the UI-only half. The port's
     * GeoscapeUi_FreeAll takes the mode as its last argument, so the shell hands it 0 and keeps the
     * roster records -- the chunk readers that follow rebuild the lists anyway. */
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GeoscapeRoster roster;

    memset(&roster, 0, sizeof(roster));
    roster.apSoldiers = pW->apSoldiers;
    roster.nSoldierCount = (u16)pW->nSoldierCount;
    roster.apBases = pW->apBases;
    roster.nBaseCount = pW->nBaseCount;
    roster.apCraft = pW->apCraft;
    roster.nCraftCount = pW->bCraftCount;
    roster.pStrategList = &pW->strategList;
    GeoscapeUi_FreeAll(&pW->ui, &roster, 0);
}

/* The bases chunk's live owners, as one struct (see SaveBasesLive in game/pufo_save.h): the grid, the two
 * base bytes, the eight hangar bytes, the funds, the defended-base byte, the 17 place flags, the nine
 * region counters and the eight site records. Save_BasesLiveToRecord / Save_BasesRecordToLive are their
 * only readers on this path -- they stand for the original's direct global reads and writes at
 * 0x3F21C..0x3F3A4 and 0x4032C..0x404C0. The last two pointers are byte pointers because SaveBasesLive is
 * declared in a header that does not see BaseFacFlag / BaseSiteRec; the field offsets those helpers use
 * are the original's own addressing. */
static void World_WireBasesLive(PufoWorldState *pW, SaveBasesLive *pLive)
{
    memset(pLive, 0, sizeof(*pLive));
    pLive->abFacilityGrid    = pW->abFacilityGrid;                     /* 0x159768 */
    pLive->pnBaseCount       = &pW->nBaseCount;                        /* 0x159D80 */
    pLive->pnSelectedBase    = &pW->nSelectedBase;                     /* 0x159D81 */
    pLive->abHangarBytes     = pW->abGridHangarBytes;                  /* 0x159DBC */
    pLive->pnFunds           = &pW->nFunds;                            /* 0x1598D8 */
    pLive->pnCraftHomeBase   = &pW->nCraftHomeBase;                    /* 0x158848 */
    pLive->abBaseFacRecords  = (u8 *)&pW->aFac[0];                     /* 0x186E48, stride 0x1C */
    pLive->aRegionBuildCount = pW->aRegionBuildCount;                  /* 0x18655D */
    pLive->abSiteRecords     = (u8 *)&pW->aSiteRecs[0];                /* 0x186C40, stride 0x40 */
}

i32 World_LoadSlot(PufoWorldState *pW, const char *pszAppDir, i32 nSlot, BootAssets *pAssets,
                   PufoFont *pFont, const PufoOptions *pOptions, const WorldHooks *pHooks)
{
    LoadGameEnv env;
    LoadGameHooks hooks;
    SpawnMissionCtx missionCtx;
    SaveBasesLive live;      /* the bases chunk's owners, wired below */
    DatFile dat;
    i32 nRet;

    pW->pOptions = pOptions; /* same ownership as World_NewGame. pFont joins the load path in Part 65:
                              * a save that carries a mission rebuilds it, and that reader needs fontSys
                              * @ 0x158268 for Font_LoadTactSprites and the xcom_<n> sets (0x4444C..0x4449C) */
    pW->pFont = pFont;
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
    World_FillCtorEnv(pW, pAssets); /* the ships chunk's per-ship object loop counts through this env */
    pW->pfnLoadPads = pHooks->pfnLoadPads;
    pW->pLoadPadsUser = pHooks->pLoadPadsUser;
    pW->pfnPlaySample = pHooks->pfnPlaySample;
    pW->pSoundUser = pHooks->pSoundUser;
    /* The same cell World_NewGame sets: LoadGame calls Geoscape_InitUI(1) from inside itself (0x44CBC),
     * and the rebuild arm re-wires the frame through pFrameHooks. Without it the frame keeps the wiring
     * this function did BEFORE the pads existed -- the map pad, the layer pad and the world bits are all
     * NULL there, and Geoscape_BlitMapView returns without drawing on any one of them. */
    pW->pFrameHooks = pHooks;
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
    World_WireBasesLive(pW, &live);
    env.pBasesLive = &live; /* Save_BasesRecordToLive writes the chunk back into these cells right after
                             * Save_ReadBases, the way Save_ReadBases writes 0x159768/0x159D80/0x1598D8 there */
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

    /* The mission bring-up runs INSIDE LoadGame, at 0x44E24, before DatFile_Close at 0x44E2C -- so the
     * open file is handed to the state for exactly that window, and Init_LoadMission's readers
     * (tgamedata, teamunits, flobjs, map, rmp) consume it there. */
    DatFile_ctor(&dat);
    pW->pLoadDat = &dat;
    nRet = LoadGame(&dat, &env);
    pW->pLoadDat = NULL;
    DatFile_Close(&dat);
    /* 0x458E4 stamps the loaded world's format version current; the live 0x18655C byte follows the
     * record back out of here, the same join Save_BasesRecordToLive does for the bases chunk. */
    pW->nSaveVersion = pW->saveStrateg.bSaveVersion;
    return nRet;
}

static MapCell *World_TactLookupCell(i32 nX, i32 nY, i32 nLayer, void *pUser); /* defined below */

/* SaveGame @ 0x44988 over this state (Part 62). The driver is pufo_save.c's SaveGame, tested in
 * tests/test_save.c; this unit is the shell's input builder, the mirror of World_LoadSlot: every
 * chunk source is a cell the world already owns. The tactical half is filled only when a mission is
 * live (bTactFlag with the map ctx and TactState), which is the same condition the original's
 * tact branch takes. Deviation, recorded: byte_138E8C is one byte in the Map block while
 * SaveGameInput wants an i32 pointer, so the counter crosses through a local mirror and is written
 * back masked -- SaveGame increments it only in the tactical branch, so a geoscape save leaves it
 * alone either way. The render seam reuses WorldHooks' frame hook with the world as its user. */
i32 World_SaveSlot(PufoWorldState *pW, const char *pszAppDir, i32 nSlot, const WorldHooks *pHooks)
{
    SaveGameInput in;
    TactSaveState tact;       /* the driver reaches these through in, so they live at function scope */
    TeamUnitsState rosters;
    FlobjsState flobjs;
    SaveBasesLive live;       /* the bases chunk's owners, wired below */
    DatFile dat;
    GameMap *pMap;
    i32 nMissionCount;
    bool bOk;

    if (pW == NULL || pszAppDir == NULL || pHooks == NULL) {
        return 0;
    }

    nMissionCount = (i32)pW->bMissionCount; /* byte_138E8C, the mirror above */

    memset(&in, 0, sizeof(in));
    in.pszAppDir = pszAppDir;
    in.nSlot = nSlot;
    in.bTactFlag = pW->bTactFlag;
    in.pnMissionCount = &nMissionCount;
    in.pnUiScreen = &pW->nUiScreen;
    in.pBases = &pW->saveBases;
    World_WireBasesLive(pW, &live);
    in.pBasesLive = &live; /* Save_BasesLiveToRecord stages these cells into the record right before
                            * Save_WriteBases reads it -- 0x3F21C..0x3F3A4 read the globals directly */
    in.apBases = (const BaseRec *const *)pW->apBases;
    in.pProductBlock = pW->abProductBlock;
    in.pResearch = &pW->saveResearch;
    in.apShips = (const Ship *const *)pW->apCraft;
    in.nShipCount = pW->bCraftCount;
    in.pSmap = &pW->saveSmap;
    /* Save_WriteStrateg hands two globals straight to DatFile_Write (0x3FD44 writes 0x1865A8 and
     * 0x3FD54 writes 0x186594), so it refuses when the port's pointer pair is unset. The shell's
     * copies of those globals are the economy block's first slots: g_nFundArrow0 and g_nFundBar0. */
    pW->saveStrateg.pnUnk1865A8 = &pW->economy.aZero44_49[0];  /* 0x1865A8 */
    pW->saveStrateg.pnUnk186594 = &pW->economy.anFundBar[0];   /* 0x186594 */
    /* nSaveFmtVer @ 0x18655C is one byte in the original and InitUI stamps it 3 (0x85864, the arm at
     * geoiinit.c's *pbSaveVersion = 3). Save_WriteStrateg writes it (0x3F1EC) and Save_ReadStrategVersion
     * reads it back, and Save_ReadBases/Save_ReadProduct branch on it -- so the record has to carry the
     * live byte or the file records version 0 while its bodies are written in the version-3 layout, and
     * every read after the version gate lands four bytes early. */
    pW->saveStrateg.bSaveVersion = pW->nSaveVersion;
    /* Save_WriteStrateg walks the item count the geoscape block keeps at +0x38 (0x186530, the loop
     * bound at 0x3FE28), not the PtrArray's own count. The original keeps that cell equal to the list
     * at every change -- StrategList_AddItem re-states PtrArray_GetCount into it at 0x82710 and
     * StrategList_RemoveAt at 0x82B34 -- so at the moment the writer reads it they are the same
     * number. The shell's list ops carry the count in the PtrArray alone, so the value is re-stated
     * here, at the one place the original's reader runs. */
    pW->saveStrateg.nStrategCount = PtrArray_GetCount(&pW->strategList);
    in.pStrateg = &pW->saveStrateg;
    in.pStrategList = &pW->strategList;
    /* Save_WriteTime @ 0x40210 keeps no record of its own: its eight writes read the clock block
     * itself -- 0x159618, 0x15961C, 0x159610, 0x15960C, 0x159608, 0x15963C, 0x159640 -- and the word at
     * 0x1472C8. The port writes through SaveTimeRecord, so the live cells are staged into that record
     * here, at the one point the writer runs. Left unstaged, the file records an all-zero clock and
     * every load restarts the calendar at 1999-01-01 00:00 whatever the saved game's date was. */
    pW->saveTime.nSlot105    = (u32)pW->geo.time.nTotalSec;        /* nGameTotalSec @ 0x159618 */
    pW->saveTime.nSlot106    = pW->geo.time.bPaused;               /* bGameClockPaused @ 0x15961C */
    pW->saveTime.nSlot103    = (u32)pW->geo.time.nLastTickDaySec;  /* 0x159610 */
    pW->saveTime.nSlot102    = (u32)pW->geo.time.nLastTickHourSec; /* 0x15960C */
    pW->saveTime.nSlot101    = (u32)pW->geo.time.nLastTickMinSec;  /* 0x159608 */
    pW->saveTime.nSlot114    = (u32)pW->geo.time.nUnk34;           /* 0x15963C */
    pW->saveTime.nSlot115    = (u32)pW->geo.time.nMonthEpochSec;   /* 0x159640 */
    pW->saveTime.nWord1472C8 = pW->nAlienPoolAddend;               /* 0x1472C8 */
    in.pTime = &pW->saveTime;
    in.apSoldiers = pW->apSoldiers;
    in.nSoldierCount = pW->nSoldierCount;
    in.apTactItems = pW->apTactItems;
    in.nTactItemCount = pW->nTactItemCount;
    if (pW->pAssets != NULL) {
        in.apObjects = (ObjectRecFull *const *)pW->pAssets->apObjects;
        in.nObjectCount = (i32)pW->pAssets->nObjectCount;
    }
    in.pfnRenderFrame = pHooks->pfnRenderFrame;
    in.pRenderUser = pW;

    pMap = pW->pTactMap;
    if (pW->bTactFlag != 0 && pMap != NULL && pW->pTactState != NULL) {
        /* The 23 tgamedata sources, pinned against Save_WriteTGameData @ 0x446a8's reads. */
        memset(&tact, 0, sizeof(tact));
        tact.bUnk15110 = pW->sTactInput.bDeployPanelOpen;   /* 0x15110 (0x128AC8) */
        tact.nUnk1510C = pW->nMapMissionTierRaw;            /* 0x1510C (0x128AC4) */
        tact.abUnk15111[0] = pW->nMapTerrainVariant;        /* 0x15111 */
        tact.abUnk15111[1] = pW->bMapUnk15112;              /* 0x15112 */
        tact.abUnk15111[2] = pW->nMapCraftIndex;            /* 0x15113 */
        tact.nMapLayers = pMap->nMapLayers;                 /* 0x15114 */
        tact.bUnk15115 = pW->bMapUnk15115;                  /* 0x15115 */
        memcpy(tact.abSpriteSet, pW->abMapSpriteSet, sizeof(tact.abSpriteSet)); /* 0x15116..19 */
        tact.nMissionType = pMap->nMissionType;             /* 0x1511A */
        tact.bMissionMode = pW->bMapMissionMode;            /* 0x1511D (0x128AD5) */
        tact.bUnk1511E = pW->bMapUnk1511E;                  /* 0x1511E (0x128AD6) */
        tact.anUnk15188[0] = pW->tactEnv.cursor.nX;         /* 0x15128 */
        tact.anUnk15188[1] = pW->tactEnv.cursor.nY;         /* 0x1512C */
        tact.anUnk15188[2] = pW->tactEnv.focus.nCamScrollX; /* 0x15130, tactEnv.focus is its owner */
        tact.anUnk15188[3] = pW->tactEnv.focus.nCamScrollY; /* 0x15134 */
        tact.abUnk15198[0] = pW->tactEnv.cursor.nFlags;     /* 0x15138 (0x128AF0) */
        tact.abUnk15198[1] = pW->bMapCraftMapX;             /* 0x15139 (0x128AF1) */
        tact.abUnk15198[2] = pW->bMapCraftMapY;             /* 0x1513A (0x128AF2) */
        tact.nUnk151A0 = pW->nMapDeployedHwps;              /* 0x15140 (0x128AF8) */
        tact.anCraftTile[0] = pW->bMapSpawnAlt;             /* 0x15148, one byte on disk */
        tact.anCraftTile[1] = pW->nMapSpawnTileX;           /* 0x1514C, one byte on disk */
        tact.anCraftTile[2] = pW->nMapSpawnTileY;           /* 0x15150, one byte on disk */
        tact.nUnk138E6C = pW->nMapUnk138E6C;                /* v124.dat first dword */
        tact.nUnk138E70 = pW->nMapUnk138E70;                /* v124.dat second dword */

        memset(&rosters, 0, sizeof(rosters));
        rosters.nXcomCount = pMap->nXcomCount;
        rosters.nEnemyCount = pMap->nEnemyCount;
        rosters.nCivilianCount = pMap->nCivilianCount;
        rosters.nHiddenCount = pW->nMapEnemyReserveCount;   /* unk_138DF0's count */
        rosters.apXcomUnits = pMap->apXcomUnits;
        rosters.apEnemyUnits = pMap->apEnemyUnits;
        rosters.apCivilians = pMap->apCivilians;
        rosters.apHiddenUnits = pW->apTactHidden;           /* unk_138DF0 */
        rosters.pSelUnit = pMap->pSelUnit;

        memset(&flobjs, 0, sizeof(flobjs));
        flobjs.apTactItems = (const TactItem *const *)pW->apTactItems;
        flobjs.nTactItemCount = pW->nTactItemCount;
        if (pW->pAssets != NULL) {
            flobjs.apObjects = (const ObjectRecFull *const *)pW->pAssets->apObjects;
            flobjs.nObjectCount = (i32)pW->pAssets->nObjectCount;
        }

        in.pTact = &tact;
        in.pRosters = &rosters;
        in.pFlobjs = &flobjs;
        in.pMap = &pMap->geom;
        in.pfnLookup = World_TactLookupCell;
        in.pLookupUser = pW;
        in.pRmp = &pW->sTactRmp;
    }

    DatFile_ctor(&dat);
    bOk = SaveGame(&dat, &in);
    DatFile_Close(&dat);

    pW->bMissionCount = (u8)nMissionCount; /* the mirror written back, masked to the byte cell */
    return bOk ? 1 : 0;
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
     * the event pass World_StartNewHook already wired into the spawn context. The roll pair is read
     * from the cells here, at the roll, exactly as 0x81AB4 and 0x81B7C read their globals. */
    (void)StrategList_MaybeSpawnCraft_Process(&pW->strategList, pW->spawnCtx.pnSeed,
                                             pW->spawnCtx.nZoomMode, pW->spawnCtx.pTables,
                                             pW->nMonthCounter, (i32)pW->nAlienPoolAddend,
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

/* The iso layer indexes the font block's sprite-group bases as `fontSys + 2 * idx` with no bound test
 * (0x1CE70 is `AND R3,R0,#0xFF`, 0x1CE74 `ADD R3,R5,R3,LSL#1`, 0x1CE7C `LDRH R3,[R3]`), and
 * Tact_MapTypeToGlyphIdx hands out 5..8 for unit types 0..3, 18 for types 5..8, 13 for type 16,
 * 14 for type 17, 9..12 for a probe-table match at Map+0x16..0x19, and `nType + 5` otherwise --
 * a type 22 unit reaches fontSys + 0x36. Hence 28 entries (PUFO_ISO_GROUP_BASE_COUNT). The shell
 * keeps its own copy in world order because PufoFont spreads the same words over named fields.
 * Index 15 (fontSys + 0x1E) has NO writer: 0x158286 has no cross-reference in the IDB at all, so a
 * unit type 10 reads the zero the global was born with -- reproduced as 0, not invented. */
static void World_FillTactGroupBases(PufoWorldState *pW)
{
    const PufoFont *pF = pW->pFont;
    u16            *a  = pW->anTactGroupBase;
    i32             i;

    if (pF == NULL) {
        return;
    }
    for (i = 0; i < 5; ++i) {
        a[i] = pF->anFlagSprBase[i];        /* +0x00 blanks, +0x02 cursor, +0x04 terrain,
                                             * +0x06 craft, +0x08 UFO modules */
    }
    for (i = 0; i < 4; ++i) {
        a[5 + i] = pF->anXcomSprBase[i];    /* +0x0A..0x10 xcom_0..3 */
    }
    for (i = 0; i < 4; ++i) {
        a[9 + i] = pF->anAlienSprBase[i];   /* +0x12..0x18 the four alien sets */
    }
    a[13] = pF->nCivmSprBase;               /* +0x1A */
    a[14] = pF->nCivfSprBase;               /* +0x1C */
    a[15] = 0;                              /* +0x1E: never written by the original */
    a[16] = pF->nHandobBase;                /* +0x20 */
    a[17] = pF->nFloorobBase;               /* +0x22 */
    a[18] = pF->nTanksBase;                 /* +0x24 */
    a[19] = pF->nBigObsBase;                /* +0x26 */
    a[20] = pF->nGlyphBase;                 /* +0x28 */
    a[21] = pF->nBulletsBase;               /* +0x2A */
    a[22] = pF->nBaseBitsBase;              /* +0x2C */
    a[23] = pF->nScangBase;                 /* +0x2E */
    a[24] = pF->nSmokeBase;                 /* +0x30 */
    a[25] = pF->nExplBase;                  /* +0x32 */
    a[26] = pF->nMapBitsBase;               /* +0x34 */
    a[27] = pF->nTotalSprFrames;            /* +0x36, the one word past the documented bases */
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
    World_FillTactGroupBases(pW); /* the iso layer's fontSys + 2*idx window, refreshed with the loaded
                                   * group bases (Part 63) */
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
static u32 World_TactRandU16Thunk(u32 nMax, void *pUser); /* forward: defined with the enemy AI's draw seam */

/* --- the load path's callbacks (Part 65) -------------------------------------------------------
 * Save_ReadTGameData stages what the original writes straight into the Map block. The two apply hooks
 * put it back at the moment the original's own writes happen, because the calls that follow read it
 * live: Font_LoadTactSprites reaches Map_nMapLayers @ 0x15114 (0x4444C) and Map_PatchCraftSprites
 * reads nMapLayers and the two craft tiles (0x3D240). */
static void World_TactApplySaveStateThunk(const TactSaveState *pSt, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GameMap *pMap = pW->pTactMap;

    pW->sTactInput.bDeployPanelOpen = pSt->bUnk15110;                    /* 0x15110 (0x128AC8) */
    pW->nMapMissionTierRaw   = pSt->nUnk1510C;                           /* 0x1510C (0x128AC4) */
    pW->nMapTerrainVariant   = pSt->abUnk15111[0];                       /* 0x15111 */
    pW->bMapUnk15112         = pSt->abUnk15111[1];                       /* 0x15112 */
    pW->nMapCraftIndex       = pSt->abUnk15111[2];                       /* 0x15113 */
    pW->bMapUnk15115         = pSt->bUnk15115;                           /* 0x15115 */
    memcpy(pW->abMapSpriteSet, pSt->abSpriteSet, sizeof(pSt->abSpriteSet)); /* 0x15116..0x15119 */
    pW->bMapMissionMode      = pSt->bMissionMode;                        /* 0x1511D (0x128AD5) */
    pW->bMapUnk1511E         = pSt->bUnk1511E;                           /* 0x1511E (0x128AD6) */
    pW->tactEnv.cursor.nX         = pSt->anUnk15188[0];                  /* 0x15128 */
    pW->tactEnv.cursor.nY         = pSt->anUnk15188[1];                  /* 0x1512C */
    pW->tactEnv.focus.nCamScrollX = pSt->anUnk15188[2];                  /* 0x15130 */
    pW->tactEnv.focus.nCamScrollY = pSt->anUnk15188[3];                  /* 0x15134 */
    pW->tactEnv.cursor.nFlags     = pSt->abUnk15198[0];                  /* 0x15138 (0x128AF0) */
    pW->bMapCraftMapX          = pSt->abUnk15198[1];                     /* 0x15139 (0x128AF1) */
    pW->bMapCraftMapY          = pSt->abUnk15198[2];                     /* 0x1513A (0x128AF2) */
    pW->nMapDeployedHwps       = pSt->nUnk151A0;                         /* 0x15140 (0x128AF8) */
    pW->bMapSpawnAlt           = pSt->anCraftTile[0];                    /* 0x15148, one byte on disk */
    pW->nMapSpawnTileX         = pSt->anCraftTile[1];                    /* 0x1514C */
    pW->nMapSpawnTileY         = pSt->anCraftTile[2];                    /* 0x15150 */
    if (pMap != NULL) {
        pMap->nMapLayers   = pSt->nMapLayers;                            /* 0x15114 */
        pMap->nMissionType = pSt->nMissionType;                          /* 0x1511A */
    }
    /* The two v124.dat dwords are NOT applied here: 0x445D4..0x446A0 fills them later in the same
     * reader, and World_InitTactical copies them once it returns. */
}

/* The tactical reset at 0x444C0..0x445CC, in place and at the original's moment -- before
 * Map_PatchCraftSprites and MapView_FocusSelected run. Four cells have no owner in the port and stay
 * recorded gaps: Map+0x2C (the IDB names it nShotCount), Map+0x3C (byte_1139F4, four bytes),
 * Map+0x58/0x59, and apUiSlotsA[3..7]; nothing ported reads any of them. */
static void World_TactApplyResetThunk(const TactResetState *pR, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GameMap *pMap = pW->pTactMap;

    pW->bMapUnk1139B9         = pR->abGap01[0];                          /* Map+0x01 */
    pW->bMapPlayerInput       = pR->bPlayerInputEnabled;                 /* Map+0x38: a SET, not a clear */
    pW->bMapFire0             = pR->abFire[0];                           /* Map+0x50 */
    pW->bMapFire2             = pR->abFire[2];                           /* Map+0x52 */
    pW->bMapTickHold          = pR->bGameTickHold;                       /* Map+0x5A */
    pW->bMapEnemyTurnActive   = pR->bEnemyTurnActive;                    /* Map+0x5B */
    pW->bMapBattleEnded       = pR->bBattleEnded;                        /* Map+0x5C */
    pW->bMapSlot8Byte1        = (u8)(pR->aUiSlot[8] & 0xFFu);            /* Map+0x81 */
    pW->bMapSlot8Byte2        = (u8)((pR->aUiSlot[8] >> 8) & 0xFFu);     /* Map+0x82 */
    pW->bMapSlot8Byte3        = (u8)((pR->aUiSlot[8] >> 16) & 0xFFu);    /* Map+0x83: slot 8, two half-writes */
    pW->nMapUiSlot9           = pR->aUiSlot[9];                          /* Map+0x84 */
    pW->nMapUiSlot10          = pR->aUiSlot[10];                         /* Map+0x88, the value 10 */
    pW->nUiScreen             = pR->nUiScreen;                           /* Map+0x3A */
    if (pW->pnMapPhase != NULL) {
        *pW->pnMapPhase = pR->nAnimPhase;                                /* nMapPhase @ 0x1139EF, Map+0x37 */
    }
    if (pW->pnAnimPhase != NULL) {
        *pW->pnAnimPhase = pR->aUiSlot[0];                               /* apUiSlotsA[0] @ Map+0x60 */
    }
    if (pMap != NULL) {
        pMap->bActionLatch      = pR->abGap01[1];                       /* Map+0x02 */
        pMap->nMultiTargetCount = pR->nMultiTargetCount;                /* Map+0x04 */
        pMap->bFireStatus1      = pR->abFire[1];                        /* Map+0x51 */
        pMap->status.nCount     = 0;                                    /* Map+0x54..0x57, cleared as one dword */
    }
}

/* 0x44048/0x4409C/0x44100/0x4416C/0x441D8: Save_ReadTeamUnits puts the four counts and the selected
 * unit straight into the live cells; the port stages them, so they land here. The four pointer arrays
 * already ARE the shell's (pMap's two, apTactHidden for the reserve), which is why only the counts and
 * pSelUnit need copying. */
static void World_TactApplyRostersThunk(const TeamUnitsState *pSt, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GameMap *pMap = pW->pTactMap;

    if (pMap == NULL) {
        return;
    }
    pMap->nXcomCount     = pSt->nXcomCount;                        /* 0x1511F */
    pMap->nEnemyCount    = pSt->nEnemyCount;                       /* 0x15124 */
    pMap->nCivilianCount = pSt->nCivilianCount;                    /* 0x15120 */
    pMap->pSelUnit       = pSt->pSelUnit;                          /* 0x15334, 0x7FFF means NULL */
    pW->nMapEnemyReserveCount = pSt->nHiddenCount;                 /* unk_138DF0's count */
}

/* 0x44488: Font_LoadSpr(fontSys, &aXcom0[9 * byte]) -- the table at 0xA7BE8 is the ten names
 * xcom_0..xcom_9, so the byte indexes it by name the same way. */
static bool World_TactLoadSprThunk(i32 nSpriteSet, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    char szName[8];

    memcpy(szName, "xcom_", 5);                       /* aXcom0 @ 0xA7BE8: xcom_0..xcom_9, stride 9 */
    szName[5] = (char)('0' + (nSpriteSet % 10));
    szName[6] = 0;
    return Font_LoadSpr(pW->pFont, szName);
}

static void World_TactBuildIndexThunk(void *pUser)
{
    Font_BuildIndex(((PufoWorldState *)pUser)->pFont);                  /* 0x444A4 */
}

/* Save_ReadRmp's per-module call: the original runs Map_LoadRmp(unk_15881C, ...) straight from the
 * context; Rmp_Load needs the map's tile dimensions for its link edge tests (context +86299/+86300),
 * and by now Save_ReadMap has put them in pMap->geom. */
static i32 World_TactLoadRmpThunk(RmpData *pRmp, const char *pszBase, i32 nTileX, i32 nTileY,
                                  i32 nSizeX, i32 nSizeY, i32 nLevel, void *pUser)
{
    const PufoWorldState *pW = (const PufoWorldState *)pUser;
    const GameMap *pMap = pW->pTactMap;

    return Rmp_Load(pRmp, pszBase, nTileX, nTileY, nSizeX, nSizeY, nLevel,
                    (pMap != NULL) ? (i32)pMap->geom.nDimX : 0,
                    (pMap != NULL) ? (i32)pMap->geom.nDimY : 0);
}

bool World_InitTactical(PufoWorldState *pW, i32 nMode)
{
    TactInitEnv *pE;
    GameMap *pMap;

    if (pW->bTactLive) {
        return false;
    }
    /* Mode 0 is the launch seam's 0x83AC4 call. Mode 1 is LoadGame's 0x44E24 call, which runs while
     * the slot file is still open -- pLoadDat is that file, handed over by World_LoadSlot and borrowed
     * only for the duration. Any other value is not a mode the original ever passes. */
    if ((nMode != 0 && nMode != 1) || pW->pAssets == NULL || pW->pFont == NULL
        || pW->pOptions == NULL || (nMode == 1 && pW->pLoadDat == NULL)) {
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
    pE->pnTileCountX     = &pW->sTactInput.nTileShiftX;   /* Map_nTileCountX @ Map+0x34: Map_InitFacingScratch
                                                       * writes it (0x26F48) and MapView_DrawIsoLayer plus the
                                                       * Input_OnLeft/Right clamps read it. The shell had a second
                                                       * copy of these two bytes inside MapFacingScratch that no
                                                       * reader used, which is why a mission that did not poke
                                                       * sTactInput drew an empty map: the iso column bound was 0. */
    pE->pnTileCountY     = &pW->sTactInput.nTileShiftY;   /* Map_nTileCountY @ Map+0x35 (0x26F70) */
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

    /* Mode 0 needs none of the save blocks. Mode 1 runs Init_LoadMission: the mission's map, rosters,
     * items and rmp modules all come out of the open slot file. */
    if (nMode == 1) {
        pE->pDat        = pW->pLoadDat;               /* borrowed: 0x44E24 sits before DatFile_Close at 0x44E2C */
        pE->pSaveState  = &pW->sTactSave;
        pE->pSaveRosters = &pW->sTactSaveRosters;
        pE->pSaveFlobjs  = &pW->sTactFlobjs;
        pE->pSaveReset   = &pW->sTactReset;
        pE->pSaveHooks   = &pW->sTactLoadHooks;
        pE->pfnLoadRmp   = World_TactLoadRmpThunk;    /* 0x3D23C's Save_ReadRmp: Map_LoadRmp per module */
        pE->pRmpUser     = pW;

        memset(&pW->sTactLoadHooks, 0, sizeof(pW->sTactLoadHooks));
        pW->sTactLoadHooks.pfnLoadTactSprites = World_TactLoadTactSpritesHook; /* 0x4444C, the same construction the build path installs */
        pW->sTactLoadHooks.pfnLoadSpr         = World_TactLoadSprThunk;        /* 0x44488 */
        pW->sTactLoadHooks.pfnBuildIndex      = World_TactBuildIndexThunk;     /* 0x444A4 */
        pW->sTactLoadHooks.pfnRandU16         = World_TactRandU16Thunk;        /* 0x446A4/0x44694, the v124.dat rolls */
        pW->sTactLoadHooks.pfnApplyState      = World_TactApplySaveStateThunk;
        pW->sTactLoadHooks.pfnApplyReset      = World_TactApplyResetThunk;
        pW->sTactLoadHooks.pfnApplyRosters    = World_TactApplyRostersThunk;
        pW->sTactLoadHooks.pUser              = pW;
        pW->sTactLoadHooks.pFont              = pW->pFont;                     /* fontSys @ 0x158268 */
        pW->sTactLoadHooks.pbUnk157198Flag    = (pW->pTactState != NULL)
                                              ? &pW->pTactState->bPhaseActive : NULL; /* 0x445AC: unk_157198's bUnk70115 */

        /* The four roster arrays and the item/soldier pools are the same owners World_SaveSlot wrote
         * from; the readers fill them in place. Map+0x118 (the flobjs count-width flag) is the shell's
         * bMapFlobjsWideFlag... which the shell does not own, so the reader takes the wide form. */
        memset(&pW->sTactSaveRosters, 0, sizeof(pW->sTactSaveRosters));
        pW->sTactSaveRosters.apXcomUnits    = pMap->apXcomUnits;
        pW->sTactSaveRosters.apEnemyUnits   = pMap->apEnemyUnits;
        pW->sTactSaveRosters.apCivilians    = pMap->apCivilians;
        pW->sTactSaveRosters.apHiddenUnits  = pW->apTactHidden;                /* unk_138DF0 */
        pW->sTactSaveRosters.pSelUnit       = NULL;

        memset(&pW->sTactFlobjs, 0, sizeof(pW->sTactFlobjs));
        pW->sTactFlobjs.nWideCountFlag   = 1;              /* the 4-byte count form; no port cell owns Map+0x118 */
        pW->sTactFlobjs.pnTactItemCount  = &pW->nTactItemCount;
        pW->sTactFlobjs.apTactItems      = (TactItem *const *)pW->apTactItems;
        pW->sTactFlobjs.apObjects        = (ObjectRecFull *const *)pW->pAssets->apObjects;
        pW->sTactFlobjs.nObjectCount     = pW->pAssets->nObjectCount;
        pW->sTactFlobjs.apSoldiers       = pW->apSoldiers;
        pW->sTactFlobjs.nSoldierCount    = pW->nSoldierCount;

        memset(&pW->sTactSave, 0, sizeof(pW->sTactSave));
        memset(&pW->sTactReset, 0, sizeof(pW->sTactReset));
    }

    if (!Tact_InitMission(pE, nMode)) {
        World_FreeTactical(pW);
        return false;
    }
    pW->bTactLive = true;

    if (nMode == 1) {
        /* 0x445D4..0x446A0: v124.dat, read inside the same reader but after both apply points, so the
         * two dwords land here rather than in the state hook. */
        pW->nMapUnk138E6C = pW->sTactSave.nUnk138E6C;
        pW->nMapUnk138E70 = pW->sTactSave.nUnk138E70;
    }

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
     * treats a NULL probe as permissive (Path_ExpandCell steps by the plain delta table when
     * the seam is NULL), so door/step refusals are not applied on a walked path yet. The
     * seam being NULL used to dereference a null pointer there -- fixed 2026-10-20; the click
     * move arm only reaches the search once the input mirror carries Map_nMapDimX/Y
     * (0x1511B/0x1511C) and Tact_HandleMapClick's cursor gate passes. */
    pW->sTactPath.pfnProbe       = NULL;
    /* Path_ComputeReachable carries ONE user, passed to both the lookup and the probe
     * (pufo_path.c) -- in the original the flood fill reads one shared context, the
     * globals. The shell's shared context is the live mission, so the probe user is the
     * same pW the lookup uses. It used to be NULL here, and a search that actually ran
     * (the click move arm on a live mission) called World_TactLookupCell through it --
     * a null dereference, fixed 2026-10-20. */
    pW->sTactPath.pProbeUser     = pW;
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
    /* The fire-target triple is AimState.nTgtX/Y/Z (Map+0x14040/44/48): the arc walk
     * and the UI confirmations write the same three cells, so every context points at
     * the aim block. */
    pW->sTactEnemyAi.pnMeleeCellX          = &pMap->aim.nTgtX;
    pW->sTactEnemyAi.pnMeleeCellY          = &pMap->aim.nTgtY;
    pW->sTactEnemyAi.pnMeleeCellLayer      = &pMap->aim.nTgtZ;
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
    pW->sTactEnemyAiAct.pTextRecs          = (pW->pAssets != NULL) ? pW->pAssets->pTextRecs : NULL;
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

/* World_FreeTactical is about to free the tactical cell grid and the tactical item pool, and every
 * roster unit keeps pointers into both: the item links (hand, pockets, backpack, belt) and the cell
 * link at Soldier+0x10. The original leaves them all dangling -- Soldier+0x10 has exactly two writers
 * in the whole beta, Unit_PlaceOnMap @ 0x49B2C and Save_ReadSoldiers @ 0x419DC, and MapCell_SetOccupant
 * @ 0x548b8 writes MapCell+0x30 only -- while Tact_Shutdown frees the cells at 0x3E328..0x3E3CC and the
 * item pool at 0x3DCC0 without clearing the soldiers it keeps (the removal arm at 0x3DD0C..0x3DE10 takes
 * out the civilians, every enemy and only some XCOM units). The next mission's Unit_PlaceOnMap then reads
 * the stale link and its unplace arm writes MapCell_SetOccupant(cell, nullptr) into memory the previous
 * mission freed. Dropping the links here is a recorded deviation: the shell's heap does not hand the same
 * 60-byte blocks back the way the original's did, so a stale link here is a read of freed memory rather
 * than a benign write into a reused cell. */
static void World_ClearUnitLinks(Soldier **apUnits, i32 nCount)
{
    i32 i, j, k;

    for (i = 0; i < nCount; ++i) {
        Soldier *pUnit = apUnits[i];

        if (pUnit == NULL) {
            continue;
        }
        pUnit->pCell = NULL; /* Soldier+0x10 */
        pUnit->pHandL = NULL;
        pUnit->pHandR = NULL;
        for (k = 0; k < 4; ++k) {
            pUnit->aPockets[k].apItem[0] = NULL;
            pUnit->aPockets[k].apItem[1] = NULL;
        }
        for (j = 0; j < 8; ++j) {
            pUnit->apBackpack[j] = NULL;
        }
        for (j = 0; j < 9; ++j) {
            pUnit->apBelt[j] = NULL;
        }
    }
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

    if (pW->pTactMap != NULL) { /* the rosters and the hidden park live as long as the map record */
        World_ClearUnitLinks(pW->pTactMap->apXcomUnits, pW->pTactMap->nXcomCount);
        World_ClearUnitLinks(pW->pTactMap->apEnemyUnits, pW->pTactMap->nEnemyCount);
        World_ClearUnitLinks(pW->pTactMap->apCivilians, pW->pTactMap->nCivilianCount);
        World_ClearUnitLinks(pW->apTactHidden, pW->nMapEnemyReserveCount);
    }

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

/* The shell's repaint request. Screens the frame builder does not cover yet (every tact screen
 * outside 0/8/9) fall back to the caller's Render_Frame thunk, the same user the LoadGame
 * teardown's pfnRenderFrame gets; the covered screens draw through World_TactFrame (Part 63). */
static void World_TactRenderFrame(PufoWorldState *pW)
{
    if (World_bTactFrameLive(pW)) {
        World_TactFrame(pW);
        return;
    }
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

/* --- the tactical frame (Part 63) ---------------------------------------------------------
 *
 * Render_Frame @ 0x22310's default branch over the live mission. Every cell the dispatcher and
 * its callees read has exactly one port owner, so the envs below are built from the state's own
 * fields: tactEnv.focus for the camera scroll 0x15130/34, tactEnv.cursor.nFlags for the layer
 * counter 0x15138, sTactInput for the tile shift 0x34/35 and the pen's slots 20..23, sModal for
 * the modal bank 0x90..0xAC plus its two scratch cells, nMapUiSlot1/10 for the fade pair, and
 * apTactPads[PUFO_UIPAD_ENEMYSTEP] for the fade pad. The Map+0x37 phase byte is the caller's
 * cell, handed to the shell through WorldHooks.pnMapPhase and kept in pW->pnMapPhase.
 *
 * The seam covers 0x2237C's jumptable group, "cases 0,3,5,7-12,15", plus 13, the status-banner arm
 * at 0x223E8. Part 76 added the group's six furniture envs -- the fire-mode roster (3), the item
 * sub-mode panel (5), the tactical menu (7), the modal slot lists (10/11), the options top bar (12)
 * and the action buttons (15) -- so the whole iso group now paints here. Part 80, Part 81, Part 82, Part 83,
 * Part 84 and Part 85 added the six arms that REPLACE the iso layer with their own env and read only cells the
 * bring-up already owns: 1 inventory (0x224AC), 2 the look-mode minimap (0x224B8), 4 the selected unit's command
 * panel (0x224C4), 6 the marked-unit overlay (0x224D0), 0x13 the promotion report (0x223F4) and 0x14/0x15 the
 * Cydonia briefing pair (0x223DC). Nothing of 0x2237C's tactical jumptable is off the seam now.
 *
 * Two deviations, both recorded in docs/parity/game-core.md:
 *   1. The cursor-direction tick runs HERE, before Render_Frame, and RenderFrameEnv.pCursorDir is
 *      left NULL. Render_Frame's env snapshots its scalars at call time, while the original reads
 *      dword_113A40 twice: the tick at 0x22360 moves it and the fade test at 0x2250c reads the
 *      moved value. Ticking first and handing the moved cell to nFadeIn keeps one cell for both.
 *   2. pSelUnit goes to the iso layer as the live Map.pSelUnit, NULL included. The original's
 *      multi-target bar gate at 0x22080 calls Unit_bIsOperative with no null test (the recorded
 *      original bug); on Win64 a mission with no selected unit faults there exactly where the
 *      original read its own memory. */

bool World_bTactFrameLive(const PufoWorldState *pW)
{
    if (pW == NULL || !pW->bTactLive || pW->bTactFlag == 0
        || pW->pTactMap == NULL || pW->pTactState == NULL) {
        return false;
    }
    switch (pW->nUiScreen) {
    case 0: case 3: case 5: case 7: case 8: case 9:      /* 0x2237C's jumptable group: */
    case 10: case 11: case 12: case 15:                  /* "cases 0,3,5,7-12,15"        */
    case PUFO_DEBRIEF_UI_SCREEN:                         /* 13: the status banner arm at 0x223E8 */
    case 1:                                              /* 0x224AC: the inventory arm, 2 the 0x224B8 */
    case 2:                                              /* minimap arm, 4 the 0x224C4 command-pad arm */
    case 4:                                              /* and 6 the 0x224D0 marker arm -- all outside */
    case 6:                                              /* that group, all drawing no iso layer at all */
    case PUFO_PROMOTION_UI_SCREEN:                       /* 0x13: 0x223F4, the promotion report */
    case 20: case 21:                                    /* 0x14/0x15: 0x223DC, the Cydonia briefing pair -- */
        return true;                                     /* 21 has no writer anywhere, see the env note  */
    default:
        return false;   /* 16..18 are the title and menu screens (the outer cases 0x10..0x12) */
    }
}

/* StatusMsg_DrawStack @ 0x1afc4's three calls, straight through: the colour pair at 0x1AFF8,
 * Text_DrawString at 0x1B0A8 (x 5, the stack's running y), and the four hud-pad conditions. */
static void World_TactStatusSetColor(u8 r1, u8 g1, u8 b1, u8 r2, u8 g2, u8 b2, void *pUser)
{
    const PufoWorldState *pW = (const PufoWorldState *)pUser;
    const GfxColorEnv    *pColor = (pW->pFrameHooks != NULL) ? pW->pFrameHooks->pColor : NULL;

    if (pColor != NULL) {
        (void)Gfx_PackRgb565Pair(pColor, r1, g1, b1, r2, g2, b2);
    }
}

static void World_TactStatusDrawText(i32 x, i32 y, const char *pszMsg, void *pUser)
{
    (void)pUser;
    Text_DrawString(x, y, pszMsg);
}

/* 0x1B00C, the stack's first condition: unk_1139F0 @ Map+0x38. */
static bool World_TactStatusInputOn(void *pUser)
{
    return ((const PufoWorldState *)pUser)->bMapPlayerInput != 0;
}

static bool World_TactStatusSelOperative(void *pUser)
{
    const PufoWorldState *pW = (const PufoWorldState *)pUser;

    return pW->pTactMap != NULL && pW->pTactMap->pSelUnit != NULL
        && Unit_bIsOperative(pW->pTactMap->pSelUnit);
}

static i32 World_TactStatusHudPadY(void *pUser)
{
    const PadImage *pPad = ((const PufoWorldState *)pUser)->apTactPads[PUFO_UIPAD_HUD];

    return (pPad != NULL) ? pPad->nOriginY : 0;   /* dword_113AB4->nOriginY, 0x1B048 */
}

/* PadImage_BlitDecimal @ 0x36c60 writes nOriginX/nOriginY and then calls PadImage_BlitEx(pad, 0)
 * per digit (0x36CE0, 0x36D40); the port injects that call as a hook, so the shell makes it. */
static void World_TactDecimalBlit(PadImage *pPad, void *pUser)
{
    (void)pUser;
    (void)PadImage_BlitEx(pPad, 0);
}

/* The options screen's lit pads. In the original there is one owner for these 29 bytes and it is the
 * bank itself: unk_ACE50 + 32*i + 0x0A is UiControl.nState of widgets 60..88, which is what
 * Options_SyncUiFromMap_GotoScreen12 @ 0x188e4 writes (clear all 29 at 0x188F4..0x18914, then light the
 * chosen speed at 0x18938, gamma 0x1894C, music 0x1896C, sfx 0x1898C, the five pen flags 0x189AC..0x18A10,
 * musicoff 0x189AC, soundoff 0x189C0, and the INVERTED forcenodrag at 0x18A2C). The port models the same
 * 29 cells as MenuPads, whose fields are in widget order (pufo_menu.h:84-105), so handing them to the
 * widgets is a straight copy in the same order. Done here, on the screen that reads them, because
 * TactHud_DrawTopBar blits the widgets exactly as the original does. */
static void World_TactPushOptionPads(const PufoWorldState *pW, UiControl *pBankCtl)
{
    const u8 *pPad = (const u8 *)&pW->sTactMenuPads;   /* 29 bytes, one per widget 60..88 */

    if (pBankCtl == NULL) {
        return;
    }
    for (i32 i = 0; i < PUFO_HUD_TOPBAR_PADS; ++i) {
        pBankCtl[PUFO_HUD_TOPBAR_FIRST + i].nState = pPad[i];
    }
}

/* The seven inventory filter pads, same shape and same reason: in the original they ARE bank
 * UiControl.nState bytes -- 0xAC97A (widget 21), 0xAC9DA (widget 24, the "All" pad) and
 * 0xAC9FA / 0xACA1A / 0xACA3A / 0xACA5A / 0xACA7A (widgets 25..29). TactHud_OpenInventory lights
 * 24 and darkens the six (0x16F68, 0x16F78..0x16F98) and Ui_SetInvFilter re-lights them on a tap,
 * while the port keeps them in TactUiState. TactHud_DrawInventoryScreen blits the widgets, so the
 * mirror runs on the screen that reads them. */
static void World_TactPushInventoryFilterPads(const PufoWorldState *pW, UiControl *pBankCtl)
{
    static const u8 anFilterWidget[PUFO_INV_FILTER_PADS] = { 25, 26, 27, 28, 29 };  /* 0xAC9FA + 0x20*i */

    if (pBankCtl == NULL) {
        return;
    }
    pBankCtl[PUFO_HUD_INV_FILTER_X].nState   = pW->sTactUi.bFilterPadX;    /* 0xAC97A */
    pBankCtl[PUFO_HUD_INV_FILTER_ALL].nState = pW->sTactUi.bFilterPadAll;  /* 0xAC9DA */
    for (i32 i = 0; i < PUFO_INV_FILTER_PADS; ++i) {
        pBankCtl[anFilterWidget[i]].nState = pW->sTactUi.abFilterPad[i];
    }
}

/* Stand-in for MainMenu_pSlotCtx when the caller hands the frame no slot-label block: seven labels
 * that draw as nothing. TactHud_DrawModalPanel reads seven rows of 40 unconditionally, so a short
 * string is not an option here. */
static const char g_aEmptySlotLabels[PUFO_HUD_MODAL_LABELS][PUFO_HUD_MODAL_LABEL_STRIDE];

/* aTextRecs @ 0xB38D0, record n at the 205-byte stride. The three tact HUD arms take fixed records
 * by number, and the shell's copy of the blob is the asset block's. "" stands for a blob that never
 * loaded -- the original would read its zero-filled .data, which draws nothing. */
static const char *World_TextRec(const PufoWorldState *pW, i32 nRec)
{
    return (pW->pAssets != NULL && pW->pAssets->pTextRecs != NULL)
               ? (const char *)pW->pAssets->pTextRecs + PUFO_TEXT_REC_STRIDE * nRec
               : "";
}

static PufoOptions *World_TactLiveOptions(PufoWorldState *pW); /* forward: defined with the option toggles */

static void World_TactFrameDraw(PufoWorldState *pW, bool bDecRef)
{
    GameMap              *pMap;
    TactState            *pTS;
    const PufoFont       *pFont;
    const WorldHooks     *pHooks;
    const UiControlHitCtx *pHit;
    PadImage             *pHudPad;   /* non-const: TactHudPanelEnv.pHudPad is a PadImage* */
    u8                    nPhase;
    CursorDirState        dir;
    IsoGrid               grid;
    IsoAnimGlobals        anim;
    IsoSelectGate         select;
    IsoUnitDrawGate       unit;
    IsoSpriteBases        bases;
    IsoHeightGate         height;
    IsoArrowGate          arrow;
    IsoViewportGuide      guide;
    TactHudPanelEnv       panel;
    StatusDrawHooks       statusHooks;
    IsoLayerEnv           layer;
    UiModalAnim           modalAnim;
    UiModalLayout         modalLayout;
    TactHudBannerState    banner;
    TactHudRoster         roster;      /* screen 3, the fire-mode panel */
    TactHudActionGate     actionGate;  /* screen 15, the nine action buttons */
    TactHudInventoryEnv   inv;         /* screen 1, the inventory */
    IsoMinimapEnv         mini;        /* screen 2, the look-mode minimap */
    TactCmdPadEnv         cmd;         /* screen 4, the selected unit's command panel */
    TactHudMarkerEnv      mark;        /* screen 6, the marked-unit overlay */
    char                  aModeNames[PUFO_HUD_ROSTER_PADS][PUFO_HUD_ROSTER_NAME_STRIDE]; /* 0xB3768 */
    UiControl            *pBankCtl;    /* the caller's 130-widget bank, the pad source */
    UiWidgetBank         *pBank;       /* that bank's own block: its pointer pair is 0xAD714/0xAD718 */
    PufoOptions          *pOpt;        /* the option block Map_bForceNoDrag lives in */
    RenderFrameEnv        rf;
    i32                   nStatusOutY;
    bool                  bModalOpen;

    if (!World_bTactFrameLive(pW)) {
        return;
    }
    pMap  = pW->pTactMap;
    pTS   = pW->pTactState;
    pFont = pW->pFont;
    pHooks = pW->pFrameHooks;
    pHit  = (pHooks != NULL) ? pHooks->pHit : NULL;
    pHudPad = pW->apTactPads[PUFO_UIPAD_HUD];
    nPhase = (pW->pnMapPhase != NULL) ? *pW->pnMapPhase : 0; /* nMapPhase @ 0x1139EF (Map+0x37) */
    bModalOpen = pW->sModal.bOpen;

    /* 0x22360, the tick every tactical screen runs (deviation 1: here, not inside Render_Frame). */
    dir.bSlot8Byte2 = pW->bMapSlot8Byte2;
    dir.bSlot8Byte3 = pW->bMapSlot8Byte3;
    dir.nSlot9      = pW->nMapUiSlot9;
    dir.nSlot10     = pW->nMapUiSlot10;
    TactHud_TickCursorDir(&dir, pMap->tact.bBusy != 0, pMap->tact.pTrackUnit != NULL,
                          pTS->bPhaseActive != 0);
    pW->bMapSlot8Byte2 = dir.bSlot8Byte2;
    pW->bMapSlot8Byte3 = dir.bSlot8Byte3;
    pW->nMapUiSlot9    = dir.nSlot9;
    pW->nMapUiSlot10   = dir.nSlot10;

    /* --- the per-cell chain --- */
    grid.apCells = pW->apTactCells;          /* the original's Map.apCells pointer table @ 0x15338 */
    grid.nDimX   = pMap->geom.nDimX;
    grid.nDimY   = pMap->geom.nDimY;

    anim.nAnimPhase         = nPhase;
    anim.nAnimPhaseScratch0 = (u8)pMap->nAnimPhase;   /* 0xF8 */
    anim.bPhaseActive       = pTS->bPhaseActive;

    select.bQueueActive   = pTS->bQueueActive;
    select.nActionLatch   = pMap->bActionLatch;
    select.nMapLayers     = pMap->nMapLayers;
    select.nAnimPhase     = pMap->nAnimPhase;
    memcpy(select.apMultiTargets, pMap->apMultiTargets, sizeof(select.apMultiTargets));

    unit.apCells      = pW->apTactCells;
    unit.pSelUnit     = pMap->pSelUnit;
    unit.bQueueActive = pTS->bQueueActive;
    unit.bSelectFlag  = pW->bMapUnk1511E;               /* unk_128AD6 @ 0x1511E */
    unit.nAnimPhase   = (u8)pMap->nAnimPhase;
    unit.nMapPhase    = nPhase;
    unit.paGroupBase  = pW->anTactGroupBase;            /* fontSys + 2*idx, filled with the font load */
    unit.nCursorBase  = (pFont != NULL) ? pFont->anFlagSprBase[1] : 0;   /* fontSys + 0x02 */
    unit.nHandobBase  = (pFont != NULL) ? pFont->nHandobBase : 0;        /* fontSys + 0x20 */
    unit.apSelWeapon  = &pMap->pSelWeapon;              /* 0x113A00, the selected weapon ITEM */
    unit.pSelectFrame = pW->apTactPads[PUFO_UIPAD_SELSOLD];             /* pPadSelSold @ 0x156928 */
    unit.paTypeProbe  = pMap->abTypeProbe;              /* 0x16..0x19 */

    bases.nCursorBase  = (pFont != NULL) ? pFont->anFlagSprBase[1] : 0; /* fontSys + 0x02 */
    bases.nFloorobBase = (pFont != NULL) ? pFont->nFloorobBase : 0;     /* +0x22 */
    bases.nBulletsBase = (pFont != NULL) ? pFont->nBulletsBase : 0;     /* +0x2A */
    bases.nScangBase   = (pFont != NULL) ? pFont->nScangBase : 0;       /* +0x2E */
    bases.nSmokeBase   = (pFont != NULL) ? pFont->nSmokeBase : 0;       /* +0x30 */
    bases.nExplBase    = (pFont != NULL) ? pFont->nExplBase : 0;        /* +0x32 */
    bases.nGlyphBase   = (pFont != NULL) ? pFont->nGlyphBase : 0;       /* +0x28 */

    height.bFireStatus2      = pW->bMapFire2;           /* byte_113A0A @ 0x52 */
    height.bFireStatus9      = pMap->bFireStatus9;      /* unk_113A11 @ 0x59 */
    height.nFireTgtX         = pMap->aim.nTgtX;         /* unk_1279F8 @ 0x14040 */
    height.nFireTgtY         = pMap->aim.nTgtY;         /* 0x14044 */
    height.nFireTgtZ         = pMap->aim.nTgtZ;         /* 0x14048 */
    height.nHeightGlyphWord  = (u32)pMap->aim.anAimRow[10]; /* 0x14000, the IDB's nHeightGlyphId */
    height.nHeightSrcX       = pMap->nHeightSrcX;       /* 0x14060 */
    height.nHeightSrcY       = pMap->nHeightSrcY;
    height.nHeightSrcZ       = pMap->nHeightSrcZ;
    height.nFireTickProgress = pMap->nFireTickProgress; /* 0x1405C */

    arrow.bFireAnimActive = pTS->gap_70112[1];          /* TactState+0x111E1 */
    arrow.nPhaseCounter   = pTS->nPhaseCounter;         /* 0x1118C */
    arrow.bFireStatus1    = pMap->bFireStatus1;         /* unk_113A09 @ 0x51 */
    arrow.adMtX           = pMap->adMtX;                /* 0x13EE8 */
    arrow.adMtY           = pMap->adMtY;
    arrow.adMtZ           = pMap->adMtZ;
    arrow.anAimRow        = pMap->aim.anAimRow;         /* 0x13FD8 */
    arrow.nRowId          = pMap->aim.nRowId;           /* 0x14088 */
    arrow.bTrajHitStop    = pMap->bTrajHitStop;         /* 0x1406D */
    arrow.pThrownItem     = pMap->pArrowThrownItem;     /* 0x14070: no writer yet (recorded gap) */

    guide.bGameTickHold       = pW->bMapTickHold;        /* 0x5A */
    guide.bActionReady        = pW->sTactInput.bActionReady;  /* anPhaseSave0[94] @ 0x150D6 */
    guide.bPlayerInputEnabled = pW->bMapPlayerInput;     /* unk_1139F0 @ 0x38 */
    guide.bCursorBusy         = pMap->tact.bBusy ? 1 : 0;
    guide.bDeployPanelOpen    = pW->sTactInput.bDeployPanelOpen;   /* 0x15110 */
    guide.nHudPadHeight       = (pHudPad != NULL) ? (i32)pHudPad->uHeight : 0;
    guide.nUiSlot5            = pW->sTactInput.nUiSlot5;

    panel.pHudPad             = pHudPad;
    panel.apDigitPads         = &pW->apTactPads[PUFO_UIPAD_BTN(0)]; /* the eleven "b0".."b10" digits */
    panel.pSelUnit            = pMap->pSelUnit;
    panel.bDeployPanelOpen    = pW->sTactInput.bDeployPanelOpen;
    panel.bPlayerInputEnabled = pW->bMapPlayerInput;
    panel.bTuSuppressed       = pTS->bAiActionActive;    /* TactState+0x111E2 */
    panel.nBigObsBase         = (pFont != NULL) ? pFont->nBigObsBase : 0;
    panel.nAnimCounter        = pW->sTactInput.nUiSlot25;
    panel.nAnimMode           = pW->sTactInput.nUiSlot26;

    memset(&statusHooks, 0, sizeof(statusHooks));
    statusHooks.pfnSetColor            = World_TactStatusSetColor;
    statusHooks.pfnDraw                = World_TactStatusDrawText;
    statusHooks.pfnPlayerInputEnabled  = World_TactStatusInputOn;  /* existing thunk, 0x1B00C */
    statusHooks.pfnSelectedIsOperative = World_TactStatusSelOperative;
    statusHooks.pfnIsBusy              = World_TactStatusIsBusy;
    statusHooks.pfnHudPadOriginY       = World_TactStatusHudPadY;
    statusHooks.pUser                  = pW;

    /* --- the iso layer env --- */
    memset(&layer, 0, sizeof(layer));
    layer.nMapDimX        = pMap->geom.nDimX;
    layer.nMapDimY        = pMap->geom.nDimY;
    layer.nCamScrollX     = pW->tactEnv.focus.nCamScrollX;   /* 0x15130, tactEnv.focus owns it */
    layer.nCamScrollY     = pW->tactEnv.focus.nCamScrollY;   /* 0x15134 */
    layer.nLayerCount     = (i8)pW->tactEnv.cursor.nFlags;   /* 0x15138 */
    layer.nTileCountX     = pW->sTactInput.nTileShiftX;      /* 0x34 */
    layer.nTileCountY     = pW->sTactInput.nTileShiftY;      /* 0x35 */
    layer.nMapPhase       = nPhase;                          /* 0x37 */
    layer.pGrid           = &grid;
    layer.pAnim           = &anim;
    layer.pSelect         = &select;
    layer.pUnit           = &unit;
    layer.pBases          = &bases;
    layer.pHeight         = &height;
    layer.pArrow          = &arrow;
    layer.pGuide          = &guide;
    layer.pBank           = (pHooks != NULL && pHooks->pBank != NULL) ? pHooks->pBank->aCtl : NULL;
    layer.pPanel          = &panel;
    layer.pColor          = (pHooks != NULL) ? pHooks->pColor : NULL;
    layer.pfnDecimalBlit  = World_TactDecimalBlit;
    layer.pfnCtlBlit      = (pHit != NULL) ? pHit->pfnBlit : NULL;
    layer.pfnCtlBlitEx    = (pHit != NULL) ? pHit->pfnBlitEx : NULL;
    layer.pCtlUser        = (pHit != NULL) ? pHit->pUser : NULL;
    layer.pStatus         = &pMap->status;
    layer.pStatusHooks    = &statusHooks;
    layer.pSelUnit        = pMap->pSelUnit;
    layer.bDeployPanelOpen = pW->sTactInput.bDeployPanelOpen;
    layer.nHudPadHeight   = guide.nHudPadHeight;
    layer.pLookCtl        = &pW->sTactLookCtl;               /* unk_1571E4 */
    layer.pPadLabel       = (pW->pAssets != NULL && pW->pAssets->pTextRecs != NULL)
                              ? (const char *)pW->pAssets->pTextRecs + 67 * PUFO_TEXT_REC_STRIDE
                              : NULL;                        /* aTextRecs + 0x35A7 (205 * 67) */
    layer.nUiScreen       = pW->nUiScreen;

    /* --- the modal dialog: the bank's cells and its two scratch cells --- */
    modalAnim.nCursor      = pW->sModal.nActionPhaseSave112;  /* 0x128AA0 = Map+0x150E8 */
    modalAnim.bProgressive = pW->sModal.nActionPhaseSave113;  /* 0x128AA4 = Map+0x150EC */
    modalAnim.nAnimPhase   = nPhase;                          /* 0x26814 reads Map+0x37 as a byte */
    modalAnim.nBand        = nPhase;                          /* 0x26648 reads the same cell, mod 4 */
    modalAnim.nSelButton   = pW->sModal.nField48;             /* apUiSlotsA[24] @ Map+0xC0 */
    modalAnim.pColor       = (pHooks != NULL) ? pHooks->pColor : NULL;
    memset(&modalLayout, 0, sizeof(modalLayout));

    /* --- the debrief status banner (screen 13, TactHud_DrawStatusBanner @ 0x1e920) ---
     * nReadyFlag is anActionPhaseSave0[92] @ 0x150D4, which the IDB names Map_bMissionVictory; the
     * banner text is Map_szMissionCaption @ 0x128A4C (anActionPhaseSave0[28]) and the timer text
     * Map_szMissionScore @ 0x128A6A ([58]). The red test at 0x1EA74 is on the STRING's first byte
     * (`Map_szMissionScore[0] == 45`), i.e. a leading '-', so the shell hands that byte over.
     * The pad label is unk_EACE0: nothing in the image writes it and the port owns no cell for it, so
     * the shell draws an empty label -- what a never-written scratch would hold (recorded gap). */
    banner.nFadeCount   = pW->nMapUiSlot1;                        /* Map_nBannerFadeTicks @ 0x64 */
    banner.nReadyFlag   = pW->bMapMissionVictory;
    banner.nTimerValue  = (i32)(u8)pW->aMapMissionScore[0];
    pufo_strncpy(banner.szBanner, pW->aMapMissionCaption, sizeof(banner.szBanner));
    pufo_strncpy(banner.szTimer, pW->aMapMissionScore, sizeof(banner.szTimer));

    /* --- the six iso-overlay arms (Part 76) ---------------------------------------------
     *
     * Render_Frame's second switch draws the iso layer and then the furniture of the screen it is
     * on. Every pad of that furniture is a widget span of the caller's bank, and each span has two
     * other identities in the image: it is a group of g_aUiBankSpans (pufo_control.c:291), and it is
     * the range Tact_InitUiControls @ 0x3b498 installs handlers on -- the twelve fire-mode handlers
     * land on 32..43, the nine sub-mode handlers on 44..52, the seven menu rows on 53..59, the option
     * pads on 60..88, the nine action buttons on 89..97, and SaveLoad_Slot0..6 plus MenuBtn_Confirm5
     * on 105..112. One span, three uses, so pointing the draw at the bank adds no state.
     *
     * Each of these screen ids has exactly one opener family in the image. Every writer of
     * Map_nUiScreen @ 0x1139F2 and the value it stores: 3 = Tact_OpenLeftHandMenu 0x17140 /
     * Tact_OpenRightHandMenu 0x17258; 5 = TactAction_ConfirmUseItem 0x179B8; 7 = Input_OnMenuEscape
     * 0x16398 and the two rows 0x164C0 / 0x19504; 10 = Menu_OpenSaveSlots 0x188C4;
     * 11 = Menu_OpenLoadSlots 0x18884; 12 = Options_SyncUiFromMap_GotoScreen12 0x18A34;
     * 15 = Map_UiScreen0F_PlaySample0F 0x164DC. */
    pBankCtl = (pHooks != NULL && pHooks->pBank != NULL) ? pHooks->pBank->aCtl : NULL;
    pBank    = (pHooks != NULL) ? pHooks->pBank : NULL;      /* the bank itself, for its pointer pair */
    pOpt     = World_TactLiveOptions(pW);                    /* the option block Map_bForceNoDrag lives in */

    /* Screen 3, TactHud_DrawUnitRoster @ 0x21908. The panel gate is Map.pSelWeapon (Map+0x48), which
     * Ui_OpenHandMenu stores at 0x17060 before its own gates run. The twelve rows come from the two
     * banks Ui_FillActionCosts writes -- Map+0x4E7C (offered) and Map+0x4E4C (TU cost), the row gate
     * and the printed number being the SAME cell at 0x21A88 -- and the names from the twelve 30-byte
     * rows at 0xB3768, which Assets_LoadStartup fills in three steps: a 10-step stride-30 copy out of
     * text-blob record 181 (0x386BC), then one strcpy of blob 0x104145 into entry 10 (0x38704), then
     * one strcpy of record 191 into entry 11 (0x3872C). That is aszTextF + szTextG + szTextH of
     * pufo_boot.h laid end to end. The prefixes are TEXTS[389] and TEXTS[390] (unk_C7051, unk_C711E). */
    memset(&roster, 0, sizeof(roster));
    roster.pSelWeapon = pMap->pSelWeapon;                                         /* 0x21918 gate */
    memcpy(roster.anModeFlag,  pW->sTactUi.anActionEnabled, sizeof(roster.anModeFlag));  /* Map+0x4E7C */
    memcpy(roster.anModeValue, pW->sTactUi.anActionCost,    sizeof(roster.anModeValue));  /* Map+0x4E4C */
    roster.pNameTable = (const u8 *)aModeNames;
    if (g_textTables.bLoaded) {
        for (i32 i = 0; i < PUFO_TXT_F_COUNT; ++i) {
            pufo_strncpy(aModeNames[i], g_textTables.pF[i], sizeof(aModeNames[i]));
        }
        pufo_strncpy(aModeNames[PUFO_TXT_F_COUNT], g_textTables.pG,
                     sizeof(aModeNames[PUFO_TXT_F_COUNT]));
        pufo_strncpy(aModeNames[PUFO_TXT_F_COUNT + 1], g_textTables.pH,
                     sizeof(aModeNames[PUFO_TXT_F_COUNT + 1]));
    }
    roster.szNumberPrefixA = World_TextRec(pW, PUFO_TEXT_REC_TU_LABEL);   /* TEXTS[389] unk_C7051 */
    roster.szNumberPrefixB = World_TextRec(pW, PUFO_TEXT_REC_TIME_LABEL); /* TEXTS[390] unk_C711E */

    /* Screen 15, TactHud_DrawActionButtons @ 0x21430: every button reads the latch byte
     * Map_bActionLatch @ Map+0x02 (0x214D0..0x2152C), button 2 also reads Map.nMultiTargetCount
     * @ 0x1139BC (0x21500) and buttons 5/6 read the deploy-panel byte 0x15110 (0x2154C/0x2156C). */
    actionGate.nActionMode       = pMap->bActionLatch;
    actionGate.nMultiTargetCount = pMap->nMultiTargetCount;
    actionGate.bDeployPanelOpen  = pW->sTactInput.bDeployPanelOpen;

    if (pW->nUiScreen == (i32)PUFO_MENU_SCREEN_OPTIONS) {
        World_TactPushOptionPads(pW, pBankCtl);   /* screen 12 (0x18A34): the pads it blits */
    }
    if (pW->nUiScreen == 1) {
        World_TactPushInventoryFilterPads(pW, pBankCtl);  /* screen 1 (0x16F68..0x16F98): the filter row */
    }

    /* Screen 1, TactHud_DrawInventoryScreen @ 0x1f58c. This arm REPLACES the iso layer (0x2237C sends
     * case 1 straight to 0x224AC, never through 0x22414), and every cell it reads is one the mission
     * bring-up already owns: the 13 panel controls are bank widgets 19..31 (`pBank + 0x260`, the same
     * 13 Input_OnPointer hit-tests here), the backdrop is the "invv" pad the pad loader stored at
     * Map+0x100, the unit portrait table is the 32-slot manual pad set at Map+0x199C indexed
     * 8*nUnitType + 4*bFemale + nLook, the item strip's empty-hand marker is the "twonone" pad at
     * 0x156A70, the empty-slot sentinel is unk_138EA0, the drag byte is the pen's own Map+0x3C
     * (unk_1139F4, written by Input_OnPointer / Input_OnPointerUp), and the drag-off option is
     * Map_bForceNoDrag @ 0x128A9C -- the byte widget 88's handler flips (Menu_ToggleForceNoDrag
     * @ 0x19d94), so the arm and the toggle read the one cell. */
    memset(&inv, 0, sizeof(inv));
    inv.pInvPad         = pW->apTactPads[PUFO_UIPAD_INV];        /* unk_113AB8, Map+0x100 "invv" */
    inv.aControls       = (pBankCtl != NULL) ? &pBankCtl[PUFO_HUD_INV_FIRST] : NULL;  /* 0x1f5a8 */
    inv.pSelUnit        = pMap->pSelUnit;
    inv.pHit            = &pW->sTactHit;                         /* the equip/drag hit state */
    inv.apPortraitPad   = pW->apTactPads;                        /* Map+0x199C: PUFO_UIPAD_MAN(0..31) */
    inv.pszBurdenLabel  = World_TextRec(pW, PUFO_TEXT_REC_BURDEN_LABEL);   /* TEXTS[55] "Вес: " */
    inv.pszBurdenSuffix = World_TextRec(pW, PUFO_TEXT_REC_BURDEN_SUFFIX);  /* TEXTS[68], empty */
    inv.panRankLow      = (const char *)g_textTables.pE;        /* aszTextE @ 0xB2200, Xcom ranks */
    inv.panRankOther    = (const char *)g_textTables.pA;        /* aszTextA @ 0xB2148, alien ranks */
    inv.nCursorX        = (pBank != NULL) ? pBank->nPointerX : 0;   /* +0x1044, 0xAD714 */
    inv.nCursorY        = (pBank != NULL) ? pBank->nPointerY : 0;   /* +0x1048, 0xAD718 */
    inv.bItemHeld       = pW->sTactInput.bInvDragActive;        /* unk_1139F4 @ Map+0x3C */
    inv.bForceNoDrag    = (pOpt != NULL) ? pOpt->bForceNoDrag : 0;
    inv.nBigObsBase     = (pFont != NULL) ? pFont->nBigObsBase : 0;        /* fontSys + 0x26 */
    inv.Strip.pUi           = &pW->sTactUi;                     /* the ground-item list and its filter */
    inv.Strip.pSelUnit      = pMap->pSelUnit;
    inv.Strip.pEmptyHandPad = pW->apTactPads[PUFO_UIPAD_TWONONE];          /* unk_156A70 "twonone" */
    inv.Strip.pEmptySlot    = &pW->sMapSentinel138EA0;                     /* unk_138EA0 @ Map+0x254E8 */
    inv.Strip.nBigObsBase   = inv.nBigObsBase;

    /* Screen 2, MapView_DrawMinimap @ 0x1ed4c: the look-mode overview, the other arm that REPLACES the
     * iso layer (0x2237C sends case 2 to 0x224B8, never through 0x22414). Its scroll pair is the world
     * offset pair Map_nMinimapScrollX/Y @ 0x118864 / 0x118868, which is the same cell the look-mode
     * pan arm of Input_OnLeft / Input_OnRight moves and the one MapView_EnterLookMode derives at 0x16408
     * from the camera pair -- tactEnv.focus owns 0x15130/0x15134, sTactInput owns 0x4EAC/0x4EB0.
     * The two loop bounds are the tile-size pair at Map+0x30/0x32, the cells Map_InitFacingScratch
     * fills with 240 / 320 at 0x26F38 / 0x26F40 (two plain STRHs against the Map pointer, so the xref
     * census sees no writer for 0x1139E8 / 0x1139EA even though the mission bring-up's call at 0x3CB30
     * is exactly what fills them). tactEnv.tileSize is that pair's port owner; focus holds the copy the
     * two camera calls use. The layer count is the cursor's own byte, read SIGNED at 0x1EF58.
     * The glyph group is the iso arm's bases block (nScangBase, fontSys + 0x2E), the label is text
     * record 17, and the cursor is the "minimcur" pad the pad loader stored at 0x1571F0. */
    memset(&mini, 0, sizeof(mini));
    mini.pnScrollX    = &pW->sTactInput.anCamWorldX;                       /* 0x118864, Map+0x4EAC */
    mini.pnScrollY    = &pW->sTactInput.anCamWorldY;                       /* 0x118868, Map+0x4EB0 */
    mini.nSpanX       = pW->tactEnv.tileSize.nTileW;                       /* Map_nMinimapSpanX @ 0x1139E8 */
    mini.nSpanY       = pW->tactEnv.tileSize.nTileH;                       /* Map_nMinimapSpanY @ 0x1139EA */
    mini.nLayerCount  = (i8)pW->tactEnv.cursor.nFlags;                     /* nLayerScratch @ 0x15138 */
    mini.nMapPhase    = nPhase;                                            /* nMapPhase @ 0x1139EF */
    mini.pGrid        = &grid;
    mini.pBases       = &bases;
    mini.pLabel       = World_TextRec(pW, PUFO_TEXT_REC_MINIMAP_LABEL);    /* aTextRec17 @ 0xB466D */
    mini.pMiniCursor  = pW->apTactPads[PUFO_UIPAD_MINICURSOR];             /* 0x1571F0 "minimcur" */
    mini.pBank        = pBankCtl;                    /* the arm indexes widgets 98..100 itself (0xAD310) */
    mini.pColor       = (pHooks != NULL) ? pHooks->pColor : NULL;
    mini.pfnCtlBlit   = (pHit != NULL) ? pHit->pfnBlit : NULL;
    mini.pfnCtlBlitEx = (pHit != NULL) ? pHit->pfnBlitEx : NULL;
    mini.pCtlUser     = (pHit != NULL) ? pHit->pUser : NULL;

    /* Screen 4, Tact_DrawCommandPads @ 0x1fc8c: the selected unit's status panel, the third arm that
     * REPLACES the iso layer (0x2237C case 4 -> 0x224C4). Bank widget 16 carries Tact_OpenCommandScreen
     * @ 0x16ffc, which plays 16, CLEARS the look-commit byte at 0x1701C and stores 4 at 0x17024 with no
     * gate on a selection -- and the arm then reads Map.pSelUnit with no null test (0x1FCC4), the same
     * shape as deviation 2. Widget 31 (Tact_OpenCommandScreenFlagged @ 0x198c4) sets that byte instead,
     * and it is what keeps widgets 102/103 off the panel. The backdrop is the "soldier" pad the pad loader
     * stored at Map+0x15108 (unk_128AC0); the two insignia slots are the startup pads cs0/cs1/cs2 the asset
     * block owns (0x186C30..0x186C38), whose origins the arm rewrites to (5,2) and (39,2) before each blit;
     * the eleven row labels are text records 56..66 read straight from the blob; the rank names are
     * aszTextE @ 0xB2200 and aszTextA @ 0xB2148; the controls are bank widgets 101..103
     * (unk_AD370 = pBank + 0xCA0). */
    memset(&cmd, 0, sizeof(cmd));
    cmd.pSelUnit        = pMap->pSelUnit;                        /* 0x128CEC, read with no null test */
    cmd.pSoldierPad     = pW->apTactPads[PUFO_UIPAD_SOLDIER];    /* unk_128AC0 @ Map+0x15108 "soldier" */
    if (pW->pAssets != NULL) {
        for (i32 c = 0; c < (i32)(sizeof(cmd.apCsPad) / sizeof(cmd.apCsPad[0])); ++c) {
            cmd.apCsPad[c] = &pW->pAssets->aPadCs[c];            /* 0x186C30 + 4c: cs0 / cs1 / cs2 */
        }
        cmd.pTextRecs = pW->pAssets->pTextRecs;                  /* records 56..66 are the row labels */
    }
    cmd.panRankLow      = (const char *)g_textTables.pE;         /* aszTextE @ 0xB2200 */
    cmd.panRankOther    = (const char *)g_textTables.pA;         /* aszTextA @ 0xB2148 */
    cmd.bLookCommitFlag = pMap->bLookCommitFlag;                 /* unk_128A44 @ Map+0x1508C */
    cmd.pBank           = pBankCtl;                    /* the arm indexes widgets 101..103 itself (0xAD370) */
    cmd.pColor          = (pHooks != NULL) ? pHooks->pColor : NULL;
    cmd.pfnCtlBlit      = (pHit != NULL) ? pHit->pfnBlit : NULL;
    cmd.pfnCtlBlitEx    = (pHit != NULL) ? pHit->pfnBlitEx : NULL;
    cmd.pCtlUser        = (pHit != NULL) ? pHit->pUser : NULL;

    /* Screen 6, TactHud_DrawUnitMarkers @ 0x20c58: the marked-unit overlay, the fourth arm that REPLACES
     * the iso layer (0x2237C case 6 -> 0x224D0). Its opener is the mark branch of
     * TactHud_UseSpecialItem20 @ 0x17d54 (bank widget 40): it pays the cost off Soldier.uTu (0x18078), marks
     * both rosters by radius (0x180A0..0x18184) and only then stores 6 at 0x18248, so the marks are the
     * mission's own bSpecialMark bytes. The overlay
     * is the "mscanv" pad at unk_113ABC (Map+0x104), re-originated to (0,0) by the arm at 0x20C6C; the two
     * marks are "sbsold" unk_138EB8 and "ssold" unk_156A80, placed 11 pixels per cell of difference from
     * Map_pSelUnit->pCell -- which the arm reads with no null test at 0x20CEC, like the command panel's
     * selection read; the XCOM band is 11*(dy+14) and the enemy band 11*dy+155, one pixel apart, and large
     * units offset 2 (XCOM) or 8 (enemy) while small ones offset 4. The control is bank widget 104
     * (unk_AD3D0 = pBank + 0xD00). */
    memset(&mark, 0, sizeof(mark));
    mark.pOverlayPad = pW->apTactPads[PUFO_UIPAD_MARKER];     /* unk_113ABC @ Map+0x104 "mscanv" */
    mark.pLargeMark  = pW->apTactPads[PUFO_UIPAD_SBSOLD];     /* unk_138EB8 "sbsold" */
    mark.pSmallMark  = pW->apTactPads[PUFO_UIPAD_SOLD];       /* unk_156A80 "ssold" */
    mark.pSelCell    = (pMap->pSelUnit != NULL) ? pMap->pSelUnit->pCell : NULL;

    /* --- the dispatch env --- */
    memset(&rf, 0, sizeof(rf));
    rf.bGeoscapeMode   = pW->bGeoscapeUi;   /* 0x2231C tests it before the screen id */
    rf.nUiScreen       = pW->nUiScreen;
    rf.bDrawIsoLayer   = pW->bMapDrawIsoLayer;
    rf.pfnPresent      = (pHooks != NULL) ? pHooks->pfnPresentFrame : NULL; /* GX_Present @ 0x27788 */
    rf.pPresentUser    = (pHooks != NULL) ? pHooks->pPresentUser : NULL;
    rf.bCursorBusy       = pMap->tact.bBusy != 0;
    rf.bCursorTrackUnit  = pMap->tact.pTrackUnit != NULL;
    rf.bCursorUnk70115   = pTS->bPhaseActive != 0;
    rf.pIso              = &pW->sTactIso;   /* the original's one IsoDrawCtx global */
    rf.pIsoLayer         = &layer;
    rf.pTextRecs         = (pW->pAssets != NULL) ? pW->pAssets->pTextRecs : NULL;
    rf.pColor            = layer.pColor;
    rf.pfnBlit           = (pHit != NULL) ? pHit->pfnBlit : NULL;
    rf.pfnBlitEx         = (pHit != NULL) ? pHit->pfnBlitEx : NULL;
    rf.pBlitUser         = (pHit != NULL) ? pHit->pUser : NULL;
    rf.pnActionPhaseSave112 = &pW->sModal.nActionPhaseSave112;
    rf.pModalAnim        = &modalAnim;
    rf.bModalActive      = bModalOpen ? 1 : 0;
    rf.nModalWidth       = pW->sModal.anRect12_14[0];
    rf.nModalRec         = pW->sModal.anRect12_14[1];
    rf.nModalLines       = pW->sModal.anRect12_14[2];
    rf.pszModalBtn1      = (const char *)pW->sModal.pLabel15;
    rf.pszModalBtn2      = (const char *)pW->sModal.p17;
    rf.pModalLayout      = &modalLayout;

    /* The six iso-overlay arms. Pads come from the bank spans; the scalars come from the cells the
     * mission already owns. Screens 10/11 draw the seven MainMenu_pSlotCtx labels onto pads
     * 105..112 (0x21d74 reads &MainMenu_pSlotCtx[40 * i]) and open the one-line modal dialog only
     * when MainMenu_nListMode is exactly 1 (0x21DE4) -- both slot openers store 0, so the dialog
     * stays closed on those two screens and the eight pads carry only their labels. */
    rf.pRosterPads        = (pBankCtl != NULL) ? &pBankCtl[PUFO_HUD_ROSTER_FIRST] : NULL; /* 0x224A0 */
    rf.pRoster            = &roster;
    rf.pStatusPads        = (pBankCtl != NULL) ? &pBankCtl[PUFO_HUD_STATUS_FIRST] : NULL; /* 0x22494 */
    rf.pszStatus          = World_TextRec(pW, PUFO_TEXT_REC_STATUS);
    rf.pLabeledPads       = (pBankCtl != NULL) ? &pBankCtl[PUFO_HUD_MENU_FIRST] : NULL;   /* 0x22488 */
    rf.pModalPanelPads    = (pBankCtl != NULL) ? &pBankCtl[PUFO_HUD_MODAL_FIRST] : NULL;  /* 0x2247C/0x22470 */
    rf.pModalLabelTable   = (const u8 *)((pHooks != NULL && pHooks->pSlotLabels != NULL)
                                             ? pHooks->pSlotLabels : &g_aEmptySlotLabels[0][0]);
    rf.nModalPanelMode    = (pHooks != NULL && pHooks->pnListMode != NULL) ? *pHooks->pnListMode : 0;
    rf.pTopBarPads        = (pBankCtl != NULL) ? &pBankCtl[PUFO_HUD_TOPBAR_FIRST] : NULL; /* 0x22464 */
    rf.pActionPads        = (pBankCtl != NULL) ? &pBankCtl[PUFO_HUD_ACTION_FIRST] : NULL; /* 0x22458 */
    rf.pActionGate        = &actionGate;
    rf.pInventory         = &inv;                          /* 0x224AC */
    rf.pMinimap           = &mini;                         /* 0x224B8 */
    rf.pCmdPads           = &cmd;                          /* 0x224C4 */
    rf.pMarkerPad         = (pBankCtl != NULL) ? &pBankCtl[PUFO_HUD_MARKER_FIRST] : NULL; /* 0x224D0, widget 104 */
    rf.pMarkers           = &mark;
    rf.apXcom             = pMap->apXcomUnits;             /* the rosters 0x20CB4 / 0x20DD8 walk */
    rf.nXcomCount         = pMap->nXcomCount;
    rf.apEnemy            = pMap->apEnemyUnits;
    rf.nEnemyCount        = pMap->nEnemyCount;

    rf.pBannerPad        = (pHooks != NULL && pHooks->pBank != NULL) ? &pHooks->pBank->aCtl[128] : NULL;
    rf.pBannerBackdrop   = pW->apTactPads[PUFO_UIPAD_ENDMISSION];   /* pPadEndMission @ 0x156A74 */
    rf.pBannerState      = &banner;
    rf.pszBannerFooter   = (pW->pAssets != NULL && pW->pAssets->pTextRecs != NULL)
                             ? (const char *)pW->pAssets->pTextRecs
                               + PUFO_DEBRIEF_TEXT_BANNER_FOOTER * PUFO_TEXT_REC_STRIDE
                             : "";                                  /* unk_BC8F4 = aTextRecs + 205*180 */
    rf.pszBannerPadLabel = World_TextRec(pW, PUFO_TEXT_REC_OK);      /* unk_EACE0 = TEXTS[1104] "OK" */
    rf.pBannerPanelA     = pW->tactEnv.apPanel[0];                  /* pUiPanel0 @ 0x1568E8 */
    rf.pBannerPanelB     = pW->tactEnv.apPanel[1];                  /* pUiPanel1 @ 0x1568E4 */

    /* Screen 0x13, TactHud_DrawConfirmDialog @ 0x1eb68: the promotion report, the fifth arm that REPLACES
     * the iso layer (0x2237C case 0x13 -> 0x223F4). Its opener is UiScreen_OnCancel's own branch: on screen
     * 13 with Map_bPromotionReport @ 0x1153D8 set, World_MissionCancelEnd (0x162B8..0x162D4) sends the debrief
     * to 19 instead of shutting the mission down. The backdrop is the same endmiss pad the banner uses; the
     * four labels are TEXTS records, not scratch -- unk_EACE0 = 1104 "OK" centred on bank widget 129's pad,
     * unk_EAF47 = 1107 "Продвижение" centred at y 10, unk_EADAD = 1105 "Имя солдата" at (10,30) and unk_EAE7A
     * = 1106 "Новое звание" at (170,30). The boot record-copy loop writes all four, which is why no xref
     * points at them. The two panels are the report's own columns: pUiPanel2 @ 0x1568E0 (names) and pUiPanel3
     * @ 0x1568DC (ranks), filled by Tact_ClearSoldierBattleFlags at 0x34E34..0x34EC0. The control is bank
     * widget 129 (unk_AD6F0 = pBank + 4128). */
    rf.pConfirmPad        = (pBankCtl != NULL) ? &pBankCtl[PUFO_HUD_CONFIRM_FIRST] : NULL;
    rf.pConfirmBackdrop   = pW->apTactPads[PUFO_UIPAD_ENDMISSION];   /* pPadEndMission @ 0x156A74 */
    rf.szConfirmPadLabel  = World_TextRec(pW, PUFO_TEXT_REC_OK);              /* unk_EACE0 = TEXTS[1104] */
    rf.szConfirmTitle     = World_TextRec(pW, PUFO_TEXT_REC_PROMOTION);       /* unk_EAF47 = TEXTS[1107] */
    rf.szConfirmLeft      = World_TextRec(pW, PUFO_TEXT_REC_PROMOTION_NAME);  /* unk_EADAD = TEXTS[1105] */
    rf.szConfirmRight     = World_TextRec(pW, PUFO_TEXT_REC_PROMOTION_RANK);  /* unk_EAE7A = TEXTS[1106] */
    rf.pConfirmPanelA     = pW->tactEnv.apPanel[2];                  /* pUiPanel2 @ 0x1568E0, the name column */
    rf.pConfirmPanelB     = pW->tactEnv.apPanel[3];                  /* pUiPanel3 @ 0x1568DC, the rank column */

    /* Screens 0x14 / 0x15, Cydonia_Blit_Then_UiPanelMsg_Draw @ 0x1a7a0: the Cydonia briefing pair, the sixth arm
     * that REPLACES the iso layer (0x2237C cases 0x14/0x15 -> 0x223DC). The handler is three instructions --
     * PadImage_Blit of the pad POINTER stored at unk_15692C (0x1A7AC), then UiPanelMsg_Draw of the panel at
     * unk_1571EC (0x1A7B4) -- and both operands are cells the mission already owns: pW->pTactBriefPad is that
     * pointer, and apPanel[4] is that panel, laid at 0x3C854..0x3CB00 as 28 line slots at (120,30), centre-aligned,
     * 14 visible rows of 16. The opener is Tact_InitMission's own tail gate: 0x3D284 reads Map_bSpawnAlt, 0x3D29C
     * tests the mode argument, either one fails out to 0x3D548, which drops unk_15692C to NULL (leaking the previous
     * mission's pad -- recorded original bug); the taken branch saves Map_nUiScreen into apUiSlotsA[11] at
     * 0x3D2A4/0x3D2A8 and stores 20 at 0x3D2B0 before allocating the pad (new(0x14), the 20-byte PadImage), running
     * its ctor and loading "cydonia" at mode 0x21 (0x3D2C0..0x3D2E0), then clearing the pad's two origin fields at
     * 0x3D2E8/0x3D2F0. Only 20 is ever stored: no writer of 21 exists in the binary, so case 0x15 is reachable only
     * if slot 11 holds 21, which nothing writes. On a failed allocation the original stores NULL anyway and faults
     * at 0x3D2E8 (recorded original bug); the port skips the ctor and the load, and its PadImage_Blit has no null
     * test either (0x110dc reads the fields straight), so the fault just moves from 0x3D2E8 to here (recorded
     * deviation). */
    rf.pCydoniaPad        = pW->pTactBriefPad;                       /* unk_15692C */
    rf.pCydoniaPanel      = pW->tactEnv.apPanel[PUFO_BRIEF_PANEL];   /* unk_1571EC */
    rf.nFadeCount        = pW->nMapUiSlot1;                     /* Map_nBannerFadeTicks @ 0x64 */
    rf.nFadeIn           = pW->nMapUiSlot10;                    /* dword_113A40 @ 0x88, post-tick */
    rf.pFadePad          = pW->apTactPads[PUFO_UIPAD_ENEMYSTEP]; /* pPadEnemyStep @ 0x156AA0 */
    rf.pStatusStack      = &pMap->status;
    rf.pnStatusOutY      = &nStatusOutY;  /* Map.nStatusDrawY: the iso tail and the fade tail share it */
    rf.pStatusHooks      = &statusHooks;

    if (bDecRef) {
        Render_Frame_DecRef(&pW->nMapUiSlot1, &rf); /* 0x3681C: --Map+0x64 first, then the frame */
    } else {
        (void)Render_Frame(&rf);
    }

    /* What the frame moved stays in the cells that own it. Only the modal arm writes the layout,
     * so the pen's status-line cells are touched only when the dialog was drawn (0x266D4,
     * 0x268E4, 0x268F0, 0x26AE4). */
    pW->sModal.nActionPhaseSave112 = modalAnim.nCursor;
    pW->sModal.nActionPhaseSave113 = modalAnim.bProgressive;
    if (bModalOpen) {
        pW->sTactInput.nSlot20X = modalLayout.nButtonX[0];
        pW->sTactInput.nSlot21X = modalLayout.nButtonX[1];
        pW->sTactInput.nSlot22Y = modalLayout.nButtonY;
        pW->sTactInput.nSlot23H = modalLayout.nButtonCount;
    }
}

void World_TactFrame(PufoWorldState *pW)
{
    if (World_bTactFrameLive(pW)) {
        World_TactFrameDraw(pW, false);
    }
}

/* 0xA0990..0xA09AC: `if (Map_nBannerFadeTicks > 0) { if (Map_nUiScreen == 0xD) Render_Frame_DecRef(); }`
 * -- the BLEQ is conditional, so no other screen ever decrements the counter here. */
void World_TickBannerFade(PufoWorldState *pW)
{
    if (!World_bTactFrameLive(pW)) {
        return;
    }
    if (pW->nMapUiSlot1 <= 0 || pW->nUiScreen != PUFO_DEBRIEF_UI_SCREEN) {
        return;
    }
    World_TactFrameDraw(pW, true);
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

/* MapCursor_SetPos @ 0x27014 reads its two gate bytes off the live record at call time: 0x27054
 * tests pTactCtx272->bBusy (the queue busy flag, the port's Map.tact.bBusy) and 0x27230 tests
 * Unit_bIsLarge(Map.pSelUnit). tactEnv.cursorPos is one context built at launch
 * (pufo_tactinit.c:500..504), so those two copies are only true right after a refresh -- and the
 * refresh that used to exist ran on tick ENTRY (this file's World_TactTickActiveUnit), i.e. before
 * the tick body changed the flag. The first mission section that hovered after an enemy turn saw
 * the mirror still holding 1 while the live byte was 0, and with bBusy 1 MapCursor_SetPos refuses
 * every VALID cell (0x27054 skips only "busy AND valid"), so the pen could not move the cursor at
 * all. Every entry that dispatches through the cached context re-stamps the two bytes here. */
static void World_TactCursorPosRefresh(PufoWorldState *pW)
{
    GameMap *pMap = pW->pTactMap;

    pW->tactEnv.cursorPos.bBusy         = (pMap != NULL) && pMap->tact.bBusy != 0;
    pW->tactEnv.cursorPos.bSelUnitLarge = (pMap != NULL) && pMap->pSelUnit != NULL
                                          && Unit_bIsLarge(pMap->pSelUnit);
}

/* TickHooks.pfnSetCursorPos = MapCursor_SetPos @ 0x27014, over the launch's cursor triple. */
static void World_TactSetCursorPosThunk(i32 nX, i32 nY, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    World_TactCursorPosRefresh(pW);
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
 * record bytes 0xB0..0xB5 plus the unit's stats. The bank row the fill mirrors its enabled
 * flags into is the caller's own bank: the original reaches the global pBank @ 0xAC6D0, and
 * +0x400 into it is widget 32 -- the first widget of the screen-3 fire-mode span. */
static void World_TactBuildCostInput(PufoWorldState *pW, Soldier *pUnit, TactItem *pItem,
                                     ActionCostInput *pIn)
{
    /* The bank is the caller's (WorldHooks.pBank); World_TactKeyCtxFill reaches it the same way. */
    UiWidgetBank *pBank = (pW->pFrameHooks != NULL) ? pW->pFrameHooks->pBank : NULL;

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
    pIn->pActionPads = (pBank != NULL) ? &pBank->aCtl[PUFO_HUD_ROSTER_FIRST] : NULL;   /* 0x26404 */
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
    World_TactBuildCostInput(pW, pMap->pSelUnit, pMap->pSelWeapon, &in);
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

/* Unit_UpdateVisibility @ 0x48580 over the mission's own VisCtx (tactEnv.vis, filled for this
 * mission at pufo_world.c:2177). The original reaches one shared vis state; sTactMoveWiring.visCtx
 * is only a copy of it, refreshed inside the move tick (0x..3327), so a caller outside that tick --
 * the inventory commit's per-unit pass (0x15C98) -- reads a stale wiring whose pSpot / pRosters /
 * ppScratchUnit are still the memset zeros and faults on pCtx->pSpot->pnTrajFront1036 (pufo_unit.c:1238).
 * The sibling refresh thunk already goes to tactEnv.vis the same way (World_TactStepRefreshVisThunk). */
static void World_TactEnemyAiVisThunk(Soldier *pUnit, void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;

    (void)Unit_UpdateVisibility(pUnit, &pW->tactEnv.vis);
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
    World_TactCursorPosRefresh(pW);
    pW->sTactMoveWiring.visCtx  = pW->tactEnv.vis;

    Map_BuildMoveActionCtx(pMap, &pW->sTactLoft, &move);
    Map_WireMoveActionCtx(&move, &pW->sTactMoveWiring);
    /* The live grid is the original's apCells pointer table @ Map+0x15338, which the shell owns
     * as apTactCells (each slot its own allocation, Map_BuildMissionTerrain). Map_LookupCell over
     * GameMap.aCells is the standalone fallback Map_BuildMoveActionCtx uses for a GameMap that owns
     * its cells inline -- a test. Here the move FSM must resolve through the same table the deploy,
     * the visibility pass, the enemy AI and Combat_ResolveShot resolve through, or it walks, aims
     * and shoots against a grid that has none of the mission's terrain, items or occupants in it:
     * the shot setup found the alien's target cell empty, and a traced shot stopped at nothing. */
    move.pfnLookup      = World_TactLookupCell;
    move.pUser          = pW;
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
    fall.pnMeleeCellX       = &pMap->aim.nTgtX;         /* unk_1279F8 @ 0x14040 */
    fall.pnMeleeCellY       = &pMap->aim.nTgtY;
    fall.pnMeleeCellLayer   = &pMap->aim.nTgtZ;
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
 * Every handler in the table is installed (Parts 54..61); widget 58 reaches the caller's window
 * through the pfnResizeMainWindow seam.
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
    pUi->nFireTgtX        = pMap->aim.nTgtX;                     /* 0x1279F8 */
    pUi->nFireTgtY        = pMap->aim.nTgtY;                     /* 0x1279FC */
    pUi->nFireTgtZ        = pMap->aim.nTgtZ;                     /* 0x127A00 */
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
    pMap->aim.nTgtX        = pUi->nFireTgtX;
    pMap->aim.nTgtY        = pUi->nFireTgtY;
    pMap->aim.nTgtZ        = pUi->nFireTgtZ;
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
    pS->nCamScrollX      = pW->tactEnv.focus.nCamScrollX;   /* Map_nCamScrollX @ 0x15130: tactEnv.focus owns it,
                                                             * the keyboard scroll arms and the pointer's
                                                             * Map_IsoOffsetA/B read it through this mirror */
    pS->nCamScrollY      = pW->tactEnv.focus.nCamScrollY;   /* 0x15134 */
    pS->nMapDimX         = (u8)pMap->geom.nDimX;   /* Map_nMapDimX @ 0x1511B: GameMap.geom owns the live
                                                    * cell; the pointer camera arm (0x1443C iso pairs), the
                                                    * scroll gates (0x1177C..) and the cost reads take it
                                                    * through this mirror. Read-only for the dispatch. */
    pS->nMapDimY         = (u8)pMap->geom.nDimY;   /* 0x1511C */
    /* The pen-gate option bytes. In the original Input_OnPointer reads 0x128A8D/8F/90/91 --
     * anPhaseSave0[93/95/96/97], the same cells the options record owns (Map_ToggleFlag...
     * @ 0x19250/0x19348/0x193a0/0x1941c toggle them, Map_InitAndLoadRegistry @ 0x22648 loads
     * them, Options_SaveToRegistry @ 0x114d8 stores them). The dispatch's staged copy read
     * zero for all of them, so a map press never committed (0x144CC gates on [95], which the
     * default-ON "runbydblclick" sets) and the edge tests / drag-scroll never ran. These are
     * read-only for the dispatch, like the dims: no SyncOut flush entry. bActionReady[94] is
     * left at zero -- nothing in the binary's pen path writes it; the viewport-guide gap
     * stands. */
    {
        const PufoOptions *pOpt = (pW->pFrameHooks != NULL) ? pW->pFrameHooks->pTactOptions : NULL;

        if (pOpt != NULL) {
            pS->bEdgeTestOn   = pOpt->bScrollByPen;   /* 0x128A8D @ 0x150D5: 0x141BC edge tests */
            pS->bClickCommits = pOpt->bRunByDblClick; /* 0x128A8F @ 0x150D7: 0x144CC commit gate */
            pS->bDragScrollA  = pOpt->bTurnByPen;     /* 0x128A90 @ 0x150D8: 0x14534 drag-scroll */
            pS->bDragScrollB  = pOpt->bMenuByPen;     /* 0x128A91 @ 0x150D9 */
            pS->bForceNoDrag  = pOpt->bForceNoDrag;   /* 0x150E4: tap-to-place vs drag */
        }
    }
    pS->bLookCommitFlag  = pMap->bLookCommitFlag;
    pS->pLookCommitUnit  = pMap->pLookCommitUnit;
    pS->pSelUnit         = pMap->pSelUnit;
    pS->pSelWeapon       = pMap->pSelWeapon;
    pS->nMultiTargetCount = pMap->nMultiTargetCount;
    for (i = 0; i < PUFO_MULTI_TARGET_MAX; ++i) {
        pS->apMultiTargets[i] = pMap->apMultiTargets[i];
    }
    /* Part 60: the pointer dispatch's gate cells, each with a live owner elsewhere. */
    pS->bTactFlag        = pW->bTactFlag != 0;              /* Map+0x00; Init/FreeTactical also set it */
    pS->bGeoscapeUi      = pW->bGeoscapeUi != 0;            /* Map+0x44, the 0x13CE0 / 0x14908 gate */
    pS->bPlayerInputOn   = pW->bMapPlayerInput;             /* unk_1139F0 @ Map+0x38 */
    pS->bGameTickHold    = pW->bMapTickHold != 0;           /* Map_bTickHold @ Map+0x5A */
    pS->bQueueActive     = pW->pTactState->bQueueActive;    /* unk_157198->bQueueActive */
    pS->bFireStatus1     = pMap->bFireStatus1;              /* Map+0x51 */
    pS->bFireStatus2     = pW->bMapFire2;                   /* Map+0x52 */
    pS->bTrajPad27_2     = pMap->bTrajHitStop;              /* 0x1406D */
    pS->nUiSlot11        = (u16)pW->nMapUiSlot11;           /* apUiSlotsA[11] low word @ 0x8C */
    pS->bSlot19Active    = (u8)(pW->sModal.bOpen ? 1u : 0u); /* the modal open byte @ Map+0xAC */
    pS->nSlot24Hit       = pW->sModal.nField48;             /* the hover cell @ Map+0xC0 */
    pW->sTactInputPre = *pS;                                /* the diff basis for SyncOut (Part 60) */
}

/* Part 60 write-back rule: each mirrored cell is flushed only if the dispatch changed it. A
 * release arm can run an installed handler inside the same window, and that handler writes the
 * SAME live cells from the other side (the EndTurn bracket restores pSelUnit, a screen handler
 * opens a screen). A flat snapshot flush would undo those handler writes; the diff makes the last
 * writer win per cell, which is what the original's single shared memory does. "Changed" is
 * measured against sTactInputPre, the dispatch's own snapshot -- never against the live cell:
 * comparing with the live cell flushes a mirror that never changed over a handler that moved the
 * live owner inside the window, which is exactly the case the rule exists for (the select thunk
 * writes Map_pSelUnit 0x15334 and the cursor 0x15128/0x1512C through tactEnv, MapView_FocusSelected
 * @ 0x16710 moves the camera scroll). A staged value equal to the snapshot IS the value SyncIn
 * just read from the live cell, so flushing it can only overwrite. bPlayerInputOn is
 * never flushed: the pfnSetPlayerInput hook owns that live byte directly. The cells sTactInput
 * alone owns (the 0x60/0x6C/0x70/0x74 slots, bDeployPanelOpen, bUiSlot8Byte0) are not mirrored at
 * all -- sTactInput IS their live owner. The camera scroll 0x15130/0x15134 is not: tactEnv.focus
 * owns it (Part 67 mirrors it both ways, because the keyboard scroll arms write it and the iso
 * layer and the pointer's cell mapping read it). */
static void World_TactInputSyncOut(PufoWorldState *pW)
{
    const TactInputState *pS = &pW->sTactInput;
    const TactInputState *pPre = &pW->sTactInputPre;
    GameMap *pMap = pW->pTactMap;
    i32 i;

    if (pS->nUiScreen != pPre->nUiScreen) {
        pW->nUiScreen = pS->nUiScreen;
    }
    if (pS->nActionLatch != pPre->nActionLatch) {
        pMap->bActionLatch = (u8)pS->nActionLatch;
    }
    if (pS->nActionMode != pPre->nActionMode) {
        pW->nMapActionMode = pS->nActionMode;
    }
    if (pS->nLayerScratch != pPre->nLayerScratch) {
        pW->tactEnv.cursor.nFlags = pS->nLayerScratch;
    }
    if (pS->nCursorX != pPre->nCursorX || pS->nCursorY != pPre->nCursorY) {
        pW->tactEnv.cursor.nX = pS->nCursorX;
        pW->tactEnv.cursor.nY = pS->nCursorY;
    }
    /* The camera scroll: the four directional arms' screen-0 branch moves it (0x1177C, 0x1186C,
     * 0x1197C, 0x11A7C) through the staged copy, and the focus seam (MapView_FocusSelected
     * @ 0x16710, MapView_FocusOnCell @ 0x2c758) recentres tactEnv.focus directly. A dispatch that
     * only ran the seam leaves the staged copy at its snapshot, so the seam's write stands; a
     * dispatch that moved the staged copy flushes it. Each side wins the cell it actually wrote. */
    if (pS->nCamScrollX != pPre->nCamScrollX) {
        pW->tactEnv.focus.nCamScrollX = pS->nCamScrollX;
    }
    if (pS->nCamScrollY != pPre->nCamScrollY) {
        pW->tactEnv.focus.nCamScrollY = pS->nCamScrollY;
    }
    if (pS->bLookCommitFlag != pPre->bLookCommitFlag) {
        pMap->bLookCommitFlag = pS->bLookCommitFlag;
    }
    if (pS->pLookCommitUnit != pPre->pLookCommitUnit) {
        pMap->pLookCommitUnit = pS->pLookCommitUnit;
    }
    if (pS->pSelUnit != pPre->pSelUnit) {
        pMap->pSelUnit = pS->pSelUnit;
    }
    if (pS->pSelWeapon != pPre->pSelWeapon) {
        pMap->pSelWeapon = pS->pSelWeapon;
    }
    if (pS->nMultiTargetCount != pPre->nMultiTargetCount) {
        pMap->nMultiTargetCount = pS->nMultiTargetCount;
    }
    for (i = 0; i < PUFO_MULTI_TARGET_MAX; ++i) {
        if (pS->apMultiTargets[i] != pPre->apMultiTargets[i]) {
            pMap->apMultiTargets[i] = pS->apMultiTargets[i];
        }
    }
    /* The Part 60 gate cells. The screen-13 press clears the queue and the fire-status trio
     * (0x13ECC); PointerStatusLine writes the hover cell; the screen-5/20/21 releases write
     * nUiScreen (mirrored above). */
    if (pS->bQueueActive != pPre->bQueueActive) {
        pW->pTactState->bQueueActive = pS->bQueueActive;
    }
    if (pS->bFireStatus1 != pPre->bFireStatus1) {
        pMap->bFireStatus1 = pS->bFireStatus1;
    }
    if (pS->bFireStatus2 != pPre->bFireStatus2) {
        pW->bMapFire2 = pS->bFireStatus2;
    }
    if (pS->bTrajPad27_2 != pPre->bTrajPad27_2) {
        pMap->bTrajHitStop = pS->bTrajPad27_2;
    }
    if (pS->nSlot24Hit != pPre->nSlot24Hit) {
        pW->sModal.nField48 = pS->nSlot24Hit;
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
    World_TactBuildCostInput(pW, pUnit, pItem, &in);
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

/* ------------------------------------------ inventory close commits the deploy (Part 66)
 *
 * The inventory screen's confirm runs Inv_CloseAndCommit @ 0x15a48, reached from Input_OnConfirm's
 * screen-1 arm (0x160FC plays sample 16, 0x16104 calls). Its second part (0x15C68..0x15CB0) is the
 * deployment itself: clear the commit flag at Map+0x01, then Tact_DeployTeamAndHwps @ 0x39980,
 * MapView_FocusSelected @ 0x16710, Map_RefreshCellMeta @ 0x2c7f4 and Unit_UpdateVisibility @ 0x48580
 * once per unit. This is where the team leaves the one cell it was parked on: Tact_SelectTeamUnits
 * @ 0x39744 builds its anchor ONCE (0x3978C..0x3979C) and calls Unit_PlaceOnMap with the same three
 * coordinates for every accepted soldier, so a bring-up roster of eight shares one cell by the
 * original's own design. Only the deploy's phase 3 (0x39E9C) walks the mission's deploy points and
 * re-places each pre-existing unit on the first cell with no occupant. Before this unit the shell
 * never ran the commit, so a live mission kept eight soldiers stacked and the iso layer's occupant
 * reads saw one. */

static bool World_TactUnitIsLargeThunk(const Soldier *pUnit, void *pUser)
{
    (void)pUser;
    return Unit_bIsLarge(pUnit);
}

/* Tact_DeployTeamAndHwps @ 0x39980 over the env Tact_InitMission filled for this mission
 * (pufo_tactinit.c:472..486). The pointers aim live cells and the live lookup and stay valid for the
 * whole mission; the four scalars are re-read because Map_BuildMissionTerrain re-rolls the craft pad
 * after that fill, and the mission type is the frame's own cell. */
static void World_TactDeployThunk(void *pUser)
{
    PufoWorldState *pW = (PufoWorldState *)pUser;
    GameMap *pMap = pW->pTactMap;
    TactDeployEnv *pE = &pW->tactEnv.deploy;

    if (pMap == NULL) {
        return;
    }
    pE->nMapLayers   = (i32)pMap->nMapLayers;   /* Map_nMapLayers @ Map+0x15114 */
    pE->nCraftMapX   = pW->bMapCraftMapX;       /* Map_nCraftMapX @ Map+0x15139 */
    pE->nCraftMapY   = pW->bMapCraftMapY;       /* Map_nCraftMapY @ Map+0x1513A */
    pE->nMissionType = pMap->nMissionType;      /* Map_nMissionType @ Map+0x1511A */
    (void)Tact_DeployTeamAndHwps(pE);
}

/* Inv_CloseAndCommit @ 0x15a48 over the live mission. Everything it reads through the Map block has
 * a live owner here, except Map_nUiScreen: the ctx wants an i32* and the cell is a u16, so the
 * screen id runs on a stack mirror and is stored back after the call. The Input_OnConfirm staging
 * compares its own snapshot, so that store is not written back over. */
static bool World_TactInvCommitBody(PufoWorldState *pW)
{
    GameMap *pMap = pW->pTactMap;
    InvCommitHooks hooks;
    InvCommitCtx ctx;
    i32 nUiScreen;
    i32 nResult;

    if (pMap == NULL || pW->pTactState == NULL || pW->pAssets == NULL) {
        return false;
    }
    nUiScreen = pW->nUiScreen; /* Map_nUiScreen @ Map+0x3A is a u16; the ctx takes an i32* */

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnUnitIsLarge      = World_TactUnitIsLargeThunk;
    hooks.pfnDeploy           = World_TactDeployThunk;
    hooks.pfnFocusSelected    = World_TactFocusSelectedThunk;
    hooks.pfnRefreshCells     = World_TactStepRefreshCellsThunk;
    hooks.pfnUpdateVisibility = World_TactEnemyAiVisThunk;
    hooks.pInvStat            = &pW->tactInvHooks;
    hooks.pUser               = pW;
    /* pfnFreeItem stays NULL: every pool entry is a malloc'd TactItem, so the fallback free() at
     * pufo_invdrag.c:1153 is the original's operator delete. */

    memset(&ctx, 0, sizeof(ctx));
    ctx.apXcomUnits     = pMap->apXcomUnits;        /* Map.apXcomUnits @ Map+0x15154 */
    ctx.pnXcomUnitCount = &pMap->nXcomCount;        /* nXcomUnitCount @ Map+0x1511F */
    ctx.apObjects       = pW->pAssets->apObjects;   /* Map.apObjects @ Map+0x108 */
    ctx.pSelUnit        = pMap->pSelUnit;           /* Map_pSelUnit @ 0x128CEC */
    ctx.apItemPool      = pW->apTactItems;          /* Map_apTactItems @ Map+0x23438 */
    ctx.pnItemCount     = &pW->nTactItemCount;      /* Map_nTactItemCount @ Map+0x1513C */
    ctx.pHit            = &pW->sTactHit;            /* the pHitItem scratch the merge uses */
    ctx.pbCommitFlag    = &pW->bMapUnk1139B9;       /* unk_1139B9 @ Map+0x01 */
    ctx.pnUiScreen      = &nUiScreen;               /* the screen-0 tail write (0x15F34) */
    ctx.pHooks          = &hooks;

    nResult = Inv_CloseAndCommit(&ctx);
    pW->nUiScreen = (u16)nUiScreen;                 /* the live cell, as the original writes it */
    return nResult != 0;
}

void World_InvCloseAndCommit(PufoWorldState *pW)
{
    (void)World_TactInvCommitBody(pW);
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

/* ---------------------------------------------------- quit confirm (Part 59)
 *
 * Widget 57 -> Menu_ConfirmQuitDialog @ 0x18e2c, ported as Menu_OpenQuitDialog (pufo_menubtn.c):
 * sample 16, UiModal_Begin(140, 24, 4, "YES", App_QuitFromTact, "NO", Menu_CancelQuitDialog),
 * then a DIRECT live Map_nUiScreen = 8 write (0x18E6C -- not through the staging copy, matching
 * the original). The YES callback runs the same screen-8 escape seam the app already wires for
 * TactInputHooks.pfnShutdownQuit (App_HookShutdownQuit -> App_QuitFromTact @ 0x18e18), so no new
 * shutdown path is invented; the NO callback is Menu_CancelQuitDialog @ 0x18e04, screen 0.
 * Running the stored callbacks stays the tact pointer dispatch, a recorded separate issue. */

static void World_QuitYesThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;      /* not World_TactLiveMission: the seam must run even
                                              * while the teardown is dropping the frame */
    const TactInputHooks *pIn;

    if (pW == NULL) {
        return;
    }
    pIn = (pW->pFrameHooks != NULL) ? pW->pFrameHooks->pTactInputHooks : NULL;
    if (pIn != NULL && pIn->pfnShutdownQuit != NULL) {
        pIn->pfnShutdownQuit(pIn->pUser);     /* App_QuitFromTact @ 0x18e18 */
    }
}

static void World_QuitNoThunk(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW != NULL) {
        Menu_CancelQuitDialog(&pW->nUiScreen); /* 0x18e04: Map_nUiScreen = 0 */
    }
}

static void World_ClickQuitConfirm(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    MenuHooks mh;

    if (pW == NULL) {
        return;
    }
    mh = World_TactMenuHooksFor(pW);          /* the Btn_Play sample seam (16) */
    (void)Menu_OpenQuitDialog(&pW->sModal, &pW->nUiScreen, World_QuitYesThunk,
                              World_QuitNoThunk, &mh);
}

/* Widget 58 -> MenuBtn_ResizeWindow @ 0x19930: sample 16 then App_ResizeMainWindow @ 0x9ecb8. Both
 * halves were ported long ago (Menu_ResizeWindow in game/pufo_menu.c, the SetWindowPos body in
 * pufo_app.c); the HWND and its stored size pair belong to the caller, so the world reaches them
 * through the WorldHooks seam. This closes the last NULL in the tact bank table. */
static void World_ResizeWindowSeamThunk(void *pUser)
{
    const PufoWorldState *pW = (const PufoWorldState *)pUser;
    const WorldHooks *pHooks = pW->pFrameHooks;

    if (pHooks != NULL && pHooks->pfnResizeMainWindow != NULL) {
        pHooks->pfnResizeMainWindow(pHooks->pResizeUser);
    }
}

static void World_ClickResizeWindow(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    MenuHooks mh;

    if (pW == NULL) {
        return;
    }
    mh = World_TactMenuHooksFor(pW);
    Menu_ResizeWindow(&mh, World_ResizeWindowSeamThunk);
}

/* ------------------------------------------------ pointer dispatch (Part 60)
 *
 * Input_OnPointer @ 0x13c6c and Input_OnPointerUp @ 0x148d0 are fully ported in pufo_input.c;
 * what was missing was the shell route that feeds them while a mission owns the pen. The world
 * fills the caller-owned TactPointerWorld's tactical fields, runs the dispatcher over sTactInput
 * between the input syncs, and leaves the caller's hit callbacks and bank in place. Presses record
 * UiControl press state; the release arm's HitTest(0, 0, false) half fires the installed pfnClick
 * (pufo_control.c:191), so the bank handlers Parts 54..59 installed are what the pen now runs. */

bool World_bTactPointerLive(const PufoWorldState *pW)
{
    return pW != NULL && pW->bTactFlag != 0 && pW->pTactMap != NULL && pW->pTactState != NULL;
}

static void World_TactPointerFill(PufoWorldState *pW, TactPointerWorld *pPtr)
{
    pPtr->tact.pQueue      = pW->pTactState;
    pPtr->tact.pfnLookup   = World_TactLookupCell;
    pPtr->tact.pLookupUser = pW;
    pPtr->pHudPad          = pW->apTactPads[PUFO_UIPAD_HUD]; /* dpanelv @ Map+0xFC */
    pPtr->pCamOffset       = &pW->sTactCamOffset;            /* Map+0x4A40/0x4A41 */
    World_TactCursorPosRefresh(pW);                           /* the two gate bytes, live now */
    pPtr->bCursorBusy      = pW->tactEnv.cursorPos.bBusy;    /* unk_157180->bBusy */
    pPtr->pLookCtl         = &pW->sTactLookCtl;              /* unk_1571E4 */
}

void World_TactPointerMove(PufoWorldState *pW, TactPointerWorld *pPtr, i32 nX, i32 nY, bool bDown)
{
    if (!World_bTactPointerLive(pW) || pPtr == NULL || pPtr->pBank == NULL
        || pW->pFrameHooks == NULL || pW->pFrameHooks->pTactInputHooks == NULL) {
        return;
    }
    World_TactPointerFill(pW, pPtr);
    World_TactInputSyncIn(pW);
    Input_OnPointer(&pW->sTactInput, pW->pFrameHooks->pTactInputHooks, pPtr, nX, nY, bDown);
    World_TactInputSyncOut(pW);
}

void World_TactPointerUp(PufoWorldState *pW, TactPointerWorld *pPtr)
{
    if (!World_bTactPointerLive(pW) || pPtr == NULL || pPtr->pBank == NULL
        || pW->pFrameHooks == NULL || pW->pFrameHooks->pTactInputHooks == NULL) {
        return;
    }
    World_TactPointerFill(pW, pPtr);
    World_TactInputSyncIn(pW);
    Input_OnPointerUp(&pW->sTactInput, pW->pFrameHooks->pTactInputHooks, pPtr);
    /* A release can run an installed handler that writes these same live cells from the inside (the
     * EndTurn bracket restores pSelUnit). The diff-based SyncOut (see above) keeps those writes. */
    World_TactInputSyncOut(pW);
}

/* ------------------------------------------------ key dispatch (Part 67)
 *
 * PocketUFO_WndProc's WM_KEYDOWN chain (0xA0DC8..0xA0F74) and WM_KEYUP chain (0xA0CE0..0xA0DC4) are
 * ported in pufo_input.c as Input_OnKeyDown/Input_OnKeyUp. What the world adds is the pen dispatch's
 * staging around them: the live Map cells in, the chain over sTactInput and the caller's hook table,
 * the live cells back out. The four held-repeat bytes are the caller's bank bytes -- the chain writes
 * UiBank_bHeldUp/Down/Left/Right @ 0xAD71C..0xAD71F, which is pBank+0x104C..0x104F, and the shell's
 * key codes (the original's record at 0x1884D8, filled by GXGetDefaultKeys(2)) are the shell's choice. */

static void World_MissionShutdownAndRebuildGeoscape(PufoWorldState *pW); /* forward: the mission-end seams */

static void World_TactKeyCtxFill(PufoWorldState *pW, KeyDispatchCtx *pCtx, TactInputWorld *pWorld)
{
    UiWidgetBank *pBank = (pW->pFrameHooks != NULL) ? pW->pFrameHooks->pBank : NULL;

    pWorld->pQueue      = pW->pTactState;                /* pMapTactical @ 0x157198 */
    pWorld->pfnLookup   = World_TactLookupCell;
    pWorld->pLookupUser = pW;
    pCtx->pState        = &pW->sTactInput;
    pCtx->pHooks        = pW->pFrameHooks->pTactInputHooks;
    pCtx->pWorld        = pWorld;
    pCtx->pbHeldDir     = (pBank != NULL) ? pBank->abHeldDir : NULL;
    pCtx->pbActionFlag  = pW->pFrameHooks->pbBankActionFlag;             /* UiBank_bActionFlag @ 0xAD711 */
    pCtx->pbEscapeFlag  = (pBank != NULL) ? &pBank->bEscapeFlag : NULL;  /* bUiEscapeFlag @ 0xAD712 */
}

void World_TactKeyDown(PufoWorldState *pW, PufoKeySlot eSlot)
{
    KeyDispatchCtx ctx;
    TactInputWorld world;

    if (!World_bTactPointerLive(pW) || pW->pFrameHooks == NULL
        || pW->pFrameHooks->pTactInputHooks == NULL) {
        return;
    }
    World_TactKeyCtxFill(pW, &ctx, &world);
    World_TactInputSyncIn(pW);
    Input_OnKeyDown(&ctx, eSlot);
    World_TactInputSyncOut(pW);
}

void World_TactKeyUp(PufoWorldState *pW, PufoKeySlot eSlot)
{
    KeyDispatchCtx ctx;
    TactInputWorld world;

    if (!World_bTactPointerLive(pW) || pW->pFrameHooks == NULL
        || pW->pFrameHooks->pTactInputHooks == NULL) {
        return;
    }
    World_TactKeyCtxFill(pW, &ctx, &world);
    World_TactInputSyncIn(pW);
    Input_OnKeyUp(&ctx, eSlot);
    World_TactInputSyncOut(pW);
}

/* The seams the chains reach through TactInputHooks. Each runs inside a key dispatch, i.e. while
 * sTactInput is already staged, so none of them re-stages -- re-syncing here would re-read the live
 * cells and drop what the calling arm wrote into the mirror. World_InvCloseAndCommit (Part 66) is the
 * same kind of seam. */

/* Input_OnAction's screens-<=2/4 arm (the BL at 0x15660) and Input_OnMenuEscape's 1/2/4 arms (0x163C8)
 * call Input_OnConfirm @ 0x15f3c directly: no sample, no screen staging, no modal end. */
void World_TactConfirmInPlace(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW == NULL || pW->pFrameHooks == NULL || pW->pFrameHooks->pTactInputHooks == NULL) {
        return;
    }
    Input_OnConfirm(&pW->sTactInput, pW->pFrameHooks->pTactInputHooks);
}

/* Tact_HandleMapClick @ 0x1512c, Input_OnAction's screen-0 arm (the BL at 0x15668). Every MapClickCtx
 * input already has a live owner in the shell: the cursor triple lives in tactEnv.cursor, the melee
 * triple in GameMap, the select context in tactEnv.select, the look control is the embedded
 * sTactLookCtl, and the walk is the mission's own sTactPath (World_InitTactical fills it, 0x2c2a8's
 * only two readers in the binary are this path and the move walker). */
void World_TactMapClick(void)
{
    PufoWorldState *pW = World_TactLiveMission();
    GameMap *pMap;
    TactInputWorld world;
    MapClickCtx ctx;

    if (pW == NULL || pW->pTactMap == NULL || pW->pTactState == NULL
        || pW->pFrameHooks == NULL || pW->pFrameHooks->pTactInputHooks == NULL) {
        return;
    }
    pMap = pW->pTactMap;
    world.pQueue      = pW->pTactState;
    world.pfnLookup   = World_TactLookupCell;
    world.pLookupUser = pW;

    memset(&ctx, 0, sizeof(ctx));
    ctx.pState            = &pW->sTactInput;
    ctx.pWorld            = &world;
    ctx.pLookCtl          = &pW->sTactLookCtl;              /* pActiveLookCtl @ 0x1571E4 */
    ctx.pSelect           = &pW->tactEnv.select;            /* Tact_SelectUnit @ 0x2c6c4 */
    ctx.pHooks            = pW->pFrameHooks->pTactInputHooks;
    ctx.pnMeleeCellX      = &pMap->aim.nTgtX;               /* 0x1279F8 */
    ctx.pnMeleeCellY      = &pMap->aim.nTgtY;               /* 0x1279FC */
    ctx.pnMeleeCellLayer  = &pMap->aim.nTgtZ;               /* 0x127A00 */
    ctx.pfnPathToCell     = Unit_PathToCell;                /* 0x2c2a8 */
    ctx.pUser             = &pW->sTactPath;
    Tact_HandleMapClick(&ctx);
}

/* The inventory screen's up/down keys (0x11974 / its mirror in Input_OnDown) select the next or
 * previous unit; left/right scroll the item filter (0x11774 -> Inv_ScrollUp @ 0x173e4). Same roster
 * and filter machinery the nav widgets use. */
void World_TactSelectNextUnit(void) { World_TactRosterCall(Roster_SelectNext); }
void World_TactSelectPrevUnit(void) { World_TactRosterCall(Roster_SelectPrev); }
void World_TactInvScrollUp(void)    { World_ClickInvScrollUp(); }
void World_TactInvScrollDown(void)  { World_ClickInvScrollDown(); }

/* MapCursor_SetPos @ 0x27014, the cursor re-placement Input_OnConfirm's armed multi-target arm runs
 * after the selection is committed (0x15618) and Input_OnPointerUp runs after a status-line release. */
void World_TactSetCursorPos(i32 nX, i32 nY)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW == NULL) {
        return;
    }
    World_TactCursorPosRefresh(pW);
    MapCursor_SetPos(&pW->tactEnv.cursorPos, nX, nY, World_TactLookupCell, pW);
}

/* StatusMsg_Push @ 0x2dda4 with aTextRecs record nRecIndex: Tact_HandleMapClick's two refusal records
 * (7 and 47) and Input_OnPointerUp's "not enough time". The wiring's pMap / pTextRecs / statusHooks
 * are launch-stable, so this is safe outside a move tick (unlike its visCtx half -- see the Part 66
 * note on World_TactEnemyAiVisThunk). */
void World_TactPushTextRecord(i32 nRecIndex)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW == NULL) {
        return;
    }
    Map_PushStatusCb(nRecIndex, &pW->sTactMoveWiring);
}

/* Tact_Shutdown(0) + Geoscape_InitUI(1), the arm UiScreen_OnCancel @ 0x16200 reaches for screens 13
 * and 19 (0x162BC / 0x162C8) and Input_OnMenuEscape reaches through it too. */
void World_TactEscapeToGeoscape(void)
{
    PufoWorldState *pW = World_TactLiveMission();

    if (pW != NULL) {
        World_MissionShutdownAndRebuildGeoscape(pW);
    }
}

/* Geoscape_SetPanelFlagA @ 0x8b448 / SetPanelFlagB @ 0x8b478: each writes one byte of the held-direction
 * block only while the screen id is 0 or 2. The world's abScrollFlags is that block, 0x1864F4..0x1864F7. */
void World_GeoscapePanelFlag(PufoWorldState *pW, bool bPanelA)
{
    GeoscapePanelFlags *pFlags;

    if (pW == NULL) {
        return;
    }
    pFlags = (GeoscapePanelFlags *)pW->abScrollFlags;
    if (bPanelA) {
        (void)Geoscape_SetPanelFlagA(pW->screen.nGeoscapeScreen, pFlags); /* 0x116D8 */
    } else {
        (void)Geoscape_SetPanelFlagB(pW->screen.nGeoscapeScreen, pFlags); /* 0x117C8 */
    }
}

/* Geoscape_UiFocusPrev @ 0x8afa0 / Geoscape_UiFocusNext @ 0x8b1f4 over the panels the shell owns: the
 * 0/2 arm's two flag bytes, the soldier screen's pair (0x15975C / 0x159DEC) and the base facility
 * panel (0x15886C). The hangar, research, manufacture, buy/sell, psi and ufopaedia panel sets have no
 * shell owner yet -- recorded gap, the dispatcher scrolls nothing for those screen ids. */
void World_GeoscapeFocusScroll(PufoWorldState *pW, bool bPrev)
{
    GeoscapeFocusEnv env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.nScreenId      = pW->screen.nGeoscapeScreen;  /* nGeoscapeScreen @ 0x186BC4 */
    env.pbFocusFlagA   = &pW->abScrollFlags[0];       /* unk_1864F4, the prev arm's byte */
    env.pbFocusFlagB   = &pW->abScrollFlags[2];       /* unk_1864F6, the next arm's byte */
    env.pSolPanelNames = pW->pSolPanelNames;          /* 0x15975C */
    env.pSolPanelCraft = pW->pSolPanelCraft;          /* 0x159DEC */
    env.pBaseFacPanel  = pW->pFacPanel;               /* 0x15886C */
    if (bPrev) {
        Geoscape_UiFocusPrev(&env);                   /* 0x118C8 */
    } else {
        Geoscape_UiFocusNext(&env);                   /* 0x119C8 */
    }
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
    World_ClickQuitConfirm,      /* 57  Menu_ConfirmQuitDialog @ 0x18e2c (Part 59) */
    World_ClickResizeWindow,     /* 58  MenuBtn_ResizeWindow @ 0x19930 (Part 61) */
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
     * store, the Map+0x00 action-latch clear: the tact flag goes down with the frame. */
    pW->bTactFlag = 0;                                       /* 0x3D574 */
    PufoFont_Free(pW->pFont);      /* Tact_Shutdown releases the font too (TactShutdownEnv.pFont): the
                                     * tact sprites Tact_InitMission loaded go here, so the rebuild's
                                     * PufoFont_Init starts from the cleared block, like the original's
                                     * Geoscape_InitUI(1) at 0x162C8 after Tact_Shutdown(0). */
    World_FreeTactical(pW);

    /* bKeepSome does split it. The branch it selects (pufo_tactend.c's `if (bKeepSome != 0)`) frees the
     * tactical copies of the rosters and ends with LDR R0,=sGeoscapeScreenBlock / ADD R0,#0x3C /
     * BL PtrArray_DeleteAll at 0x3E14C, 0x3E150, 0x3E154 -- the geoscape block's OWN strateg list at
     * +0x3C, emptied with its items freed. Those are the arms that end a run: Tact_CheckBattleEnd's
     * victory arm (0x35014) and its failed-base arm (0x35180), UiScreen_OnBack's quit arm (0x156D4) and
     * App_QuitFromTact (0x18E20), each followed by EndScreen_Init or App_QuitDisplay. The arms that return
     * to the geoscape pass 0 -- UiScreen_OnCancel 13/19 at 0x162C0, LoadGame's teardowns at 0x44D10 and
     * 0x44D80 -- and the bKeepSome == 0 branch runs Shutdown_RemoveUnits with Shutdown_MissionOutcome
     * (0x3DCF4), which reaches StrategList_RemoveAt only for the craft's own entry (0x3DF6C, 0x3DFBC).
     * The shell has no copy of the tactical rosters to free, so the branch's one store is the list.
     * Deviation, recorded: the original leaves the block's +0x38 count mirror (0x186530) at its stale
     * value here -- only StrategList_AddItem (0x82710) and StrategList_RemoveAt (0x82B34) refresh it -- so a
     * save made after this arm would write records for items that are gone. World_SaveSlot re-states the
     * mirror from the list, so the shell writes a clean empty chunk instead. */
    if (bKeepSome != 0) {
        PtrArray_DeleteAll(&pW->strategList);               /* 0x3E154 */
    }
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

/* 0x162BC / 0x162C8: shut the mission down keeping nothing, then rebuild the geoscape UI the launch
 * seam freed. Mode 1 skips InitUI's new-game branch, so this is a rebuild of the singletons, not a
 * restart of the run. The two callers are UiScreen_OnCancel's 13/19 arm (here and, through
 * TactInputHooks.pfnShutdownReinit, in World_TactEscapeToGeoscape). */
static void World_MissionShutdownAndRebuildGeoscape(PufoWorldState *pW)
{
    GeoscapeInitEnv env;

    World_EndHookShutdown(0, pW);
    World_FillInitUiEnv(pW, &env);
    Geoscape_InitUI(&env, 1);
    if (pW->pFrameHooks != NULL) {
        World_WireGeoscapeFrame(pW, pW->pFrameHooks);    /* the frame envs re-point at the new singletons */
    }
}

bool World_MissionCancelEnd(PufoWorldState *pW)
{
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

    /* The teardown and rebuild (0x162BC / 0x162C8). The original leaves Map_nUiScreen at 13 here --
     * only the 8/9 and 0x14/0x15 arms write it -- and the port keeps that. */
    World_MissionShutdownAndRebuildGeoscape(pW);
    return true;
}

/* The geoscape status panel's command handlers (Part 75).
 *
 * Geoscape_Draw installs thirteen handlers into the panel's own controls while it draws:
 * pGeoscapeCtlConfirm at 0x8EC84 / 0x8F938 / 0x90640 / 0x91CD0, pGeoscapeCtlA at 0x8EC90 / 0x8F39C /
 * 0x8F53C / 0x8F688 / 0x8F930 / 0x8FFC8 / 0x8FFDC / 0x90674 / 0x90B5C / 0x90D00 / 0x91130 / 0x91284 /
 * 0x91424 / 0x91630 / 0x918C4 / 0x91CE0, pGeoscapeCtlB at 0x8F69C / 0x8FFFC / 0x90468 / 0x90898 /
 * 0x90C74 / 0x910A8 / 0x91578 / 0x9180C, and the two footer controls at 0x8C9E0 / 0x8CA04. Ten panel
 * arms test the field they need and return before drawing when it is NULL -- StatusPanel_DrawBaseLost,
 * DrawFuelLow, DrawFlyToPoint, DrawPointReached, DrawTargetPanel, DrawMissionSite, DrawTravel,
 * DrawDescentPanel, DrawDescentTarget and DrawAlienBasePanel -- so until this commit the shell drew
 * none of the geoscape's message-answer panels, whatever the message line said.
 *
 * Two fields stay NULL on purpose: pfnConfirmReport (Ui_OpenBaseInfoAlt @ 0x84d18, which ends by
 * opening ufopaedia screen 16 at 0x84DE4 / 0x84DF4) and pfnApplyResearch (Ui_OpenResearch @ 0x84bf8,
 * research screen 8 at 0x84CEC / 0x84CF0). Neither gates an arm. The ufopaedia one still needs the Init
 * tail of a screen the shell has not built; the research one no longer does -- Part 88 wired nav slot 5
 * (the LDR at 0x67BAC) and ResearchScreen_Init @ 0x749a8 runs here, so what is missing is the
 * ScreenResetOpen2Env Ui_OpenResearch reads (pufo_baseui.h:5003), above all its pNavOut handle for the
 * fifteen nav controls that body rebuilds. Nav slots 6..9 stay at World_NoopClick. Recorded in PLAN.md.
 *
 * Every env here is built at click time out of live cells instead of assembled once at bring-up,
 * because two of the fields are handles a mission launch frees and rebuilds -- the strip pad at
 * 0x186BBC and the six nav-speed controls at 0x186500 -- and a context held across that launch would
 * keep the released ones. The Part 73 rule again: hold the live cell, never a copy of it. */

/* The env the two footer controls read, in one place because their handlers are the same body with a
 * sample inserted. `init` carries only the sample hook: neither prep calls an Init of its own. */
static void World_FillScreenOpenEnv(PufoWorldState *pW, ScreenResetOpenEnv *pEnv)
{
    memset(pEnv, 0, sizeof(*pEnv));
    pEnv->pState             = &pW->screen;                      /* nGeoscapeScreen @ 0x186BC4 */
    pEnv->pszMsg             = pW->szLabelScratch;               /* szRadarMsg @ 0x186B10 */
    pEnv->cchMsg             = sizeof(pW->szLabelScratch);
    pEnv->pszScratch         = pW->szUiScratchStr;               /* unk_186420 */
    pEnv->cchScratch         = sizeof(pW->szUiScratchStr);
    pEnv->abResearchActive   = pW->abResearchActive;             /* byte_160BBC */
    pEnv->pbResearchCount    = (const u8 *)&pW->nResearchEntries; /* 0x160C4C, read with LDRB */
    pEnv->pdwShortfall       = &pW->nShortfall1870C8;            /* 0x1870C8, written as a dword */
    pEnv->pTime              = &pW->geo.time;                    /* 0x159618, bPaused at 0x15961C */
    pEnv->pWorld             = &pW->geo.world;                   /* GameTime_Set5Sec's tick dword */
    pEnv->apSpeedCtl         = pW->ui.apRootCtl + 2;             /* 0x186500..0x186514 */
    pEnv->pLines             = &pW->msgLines;                    /* the root message list @ 0x158890 */
    pEnv->init.pfnPlaySample = World_PlaySampleWithArg;
    pEnv->init.pUser         = pW;
}

/* Smap_ResolveInterceptUI @ 0x84964 -- the answer to the message line under the map, the handler most
 * of the arms install (0x8F39C, 0x8F69C, 0x8FFC8, 0x8FFFC, 0x90468, 0x90674, 0x90898, 0x90B5C,
 * 0x90D00, 0x91130, 0x91284, 0x91424, 0x91630, 0x918C4). */
static void World_SmapResolveInterceptThunk(void)
{
    PufoWorldState       *pW = g_pActiveWorld;
    SmapResolveInterceptEnv env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.pLines             = &pW->msgLines;                 /* 0x158890 */
    env.pbInterceptDone    = &pW->bInterceptDone;           /* byte_18709C */
    env.pbFlag187090       = &pW->bFlag187090;
    env.pbFlag18708C       = &pW->bFlag18708C;
    env.pbFlag187098       = &pW->bFlag187098;
    env.pbSelectionFlag    = &pW->bMapSelectionFlag;        /* 0x159E84 */
    env.pbFinalMissionCtl  = &pW->bFinalMissionCtlArmed;    /* 0x187068 */
    env.pWorld             = &pW->geo.world;
    env.apSpeedCtl         = pW->ui.apRootCtl + 2;
    env.pShipEnv           = &pW->shipEnv;
    env.pTime              = &pW->geo.time;
    Smap_ResolveInterceptUI(&env);
}

/* Smap_DismissSelected @ 0x84428, sent to the craft and send it home (0x8F53C, 0x8FFDC). The original
 * takes four arguments through this handler, not an env. */
static void World_SmapDismissSelectedThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW == NULL) {
        return;
    }
    (void)Smap_DismissSelected(&pW->shipEnv, &pW->msgLines, &pW->geo.time, &pW->bMapSelectionFlag);
}

/* Smap_ConfirmCraftTarget @ 0x842ec: keep the pending order and put a fresh waypoint at the click
 * (0x8F688). The pending click is the viewport block's own pair -- 0x186414 / 0x186418 are
 * SmapView + 0x2C / + 0x30, so the same storage Smap_HitTestStrategSelect writes. */
static void World_SmapConfirmCraftTargetThunk(void)
{
    PufoWorldState        *pW = g_pActiveWorld;
    SmapConfirmTargetEnv  env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.pLines            = &pW->msgLines;
    env.pnSelectedShipIdx = &pW->nSelectedShipIdx;          /* 0x187054 */
    env.pStrategList      = &pW->strategList;               /* 0x1864F8 + 0x3C */
    env.pnSeed            = &pW->nStrategSeed;              /* 0x18640C */
    env.pView             = &pW->view;                      /* 0x1863E8, nMode scales a type-5 spot */
    env.pnPickX           = &pW->view.nPickX;               /* 0x186414 */
    env.pnPickY           = &pW->view.nPickY;               /* 0x186418 */
    env.pShipEnv          = &pW->shipEnv;
    env.pTime             = &pW->geo.time;
    env.pbSelectionFlag   = &pW->bMapSelectionFlag;
    Smap_ConfirmCraftTarget(&env);
}

/* Smap_ConfirmSelected @ 0x845d8 and Smap_ConfirmSelectedAlt @ 0x853e4: the craft keeps its course.
 * Same four-field env, the only difference is when the selection flag falls (0x85420 before the
 * line-count test at 0x8542C). */
static void World_SmapConfirmSelectedThunk(void)
{
    PufoWorldState    *pW = g_pActiveWorld;
    SmapSelectedEnv    env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.pLines          = &pW->msgLines;
    env.pShipEnv        = &pW->shipEnv;
    env.pTime           = &pW->geo.time;
    env.pbSelectionFlag = &pW->bMapSelectionFlag;
    Smap_ConfirmSelected(&env);
}

static void World_SmapConfirmSelectedAltThunk(void)
{
    PufoWorldState    *pW = g_pActiveWorld;
    SmapSelectedEnv    env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.pLines          = &pW->msgLines;
    env.pShipEnv        = &pW->shipEnv;
    env.pTime           = &pW->geo.time;
    env.pbSelectionFlag = &pW->bMapSelectionFlag;
    Smap_ConfirmSelectedAlt(&env);
}

/* Smap_SetWaypoint @ 0x84648 (0x8F940): the order the "point reached" panel offers. */
static void World_SmapSetWaypointThunk(void)
{
    PufoWorldState    *pW = g_pActiveWorld;
    SmapWaypointEnv    env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.pLines            = &pW->msgLines;
    env.pnMsgShipIdx      = &pW->nMsgShipIdx;               /* 0x159E74 */
    env.pnSelectedShipIdx = &pW->nSelectedShipIdx;          /* 0x187054 */
    env.pShipEnv          = &pW->shipEnv;
    env.pTime             = &pW->geo.time;
    Smap_SetWaypoint(&env);
}

/* Smap_SelectMapTarget @ 0x84484 (0x90640's sibling on the mission-site panel): the pending order,
 * the flag-27 line, and the arm that PAUSES the clock. */
static void World_SmapSelectMapTargetThunk(void)
{
    PufoWorldState        *pW = g_pActiveWorld;
    SmapSelectTargetEnv    env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.pLines             = &pW->msgLines;
    env.pnSelectedShipIdx  = &pW->nSelectedShipIdx;
    env.pnSlot159E74       = &pW->nMsgShipIdx;             /* written with the same value at 0x845A4 */
    env.pStrategList       = &pW->strategList;
    env.pShipEnv           = &pW->shipEnv;
    env.aTopics            = pW->aTopics;                  /* 0x15EF08, topic 28 arms the final mission */
    env.pTime              = &pW->geo.time;
    env.pbSelectionFlag    = &pW->bMapSelectionFlag;
    env.pbFinalMissionCtl  = &pW->bFinalMissionCtlArmed;
    Smap_SelectMapTarget(&env);
}

/* Smap_CancelTarget @ 0x84254 (0x90C74, 0x910A8, 0x9139C): launch at the pending target and drop the
 * selection. */
static void World_SmapCancelTargetThunk(void)
{
    PufoWorldState         *pW = g_pActiveWorld;
    SmapCancelTargetEnv     env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.pLines            = &pW->msgLines;
    env.pnSelectedShipIdx = &pW->nSelectedShipIdx;
    env.pbSelectionFlag   = &pW->bMapSelectionFlag;
    env.pbFlag187098      = &pW->bFlag187098;
    env.pbFlag18708C      = &pW->bFlag18708C;
    env.pbFlag187090      = &pW->bFlag187090;
    env.pTime             = &pW->geo.time;
    env.pShipEnv          = &pW->shipEnv;
    Smap_CancelTarget(&env);
}

/* Smap_OnPointerUp @ 0x84688 (0x8EC84, 0x90640, 0x91CD0): launch at the target the line names, or
 * recentre the view on it and light the crosshair tail. */
static void World_SmapPointerUpThunk(void)
{
    PufoWorldState      *pW = g_pActiveWorld;
    SmapPointerUpEnv     env;

    if (pW == NULL) {
        return;
    }
    memset(&env, 0, sizeof(env));
    env.pbSelectionFlag     = &pW->bMapSelectionFlag;
    env.pLines              = &pW->msgLines;
    env.pnSelectedShipIdx   = &pW->nSelectedShipIdx;
    env.pView               = &pW->view;
    env.pStrategList        = &pW->strategList;
    env.pStripPad           = pW->ui.apPad[GEOUI_PAD_186BBC]; /* *pGeoscapeStripPad @ 0x186BBC */
    env.pnCrosshairX        = &pW->frame.cross.nCrosshairX;   /* 0x186BC8 */
    env.pnCrosshairY        = &pW->frame.cross.nCrosshairY;   /* 0x186BCC */
    env.pbCrosshairPending  = &pW->frame.g.bCrosshairPending; /* 0x1870B0 */
    env.pnCooldownR         = &pW->frame.g.nTapCooldown;      /* 0xAC218 */
    env.pWorld              = &pW->geo.world;
    env.apSpeedCtl          = pW->ui.apRootCtl + 2;
    env.pShipEnv            = &pW->shipEnv;
    env.pTime               = &pW->geo.time;
    Smap_OnPointerUp(&env);
}

/* Smap_ClearSelection @ 0x84648 (0x8EC90, 0x91CE0): drop the line and, only if nothing is left on
 * screen, let the clock run again. */
static void World_SmapClearSelectionThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW == NULL) {
        return;
    }
    (void)Smap_ClearSelection(&pW->msgLines, &pW->geo.time, &pW->bMapSelectionFlag);
}

/* Smap_HitTestStrategSelect @ 0x82be8: what the press dispatcher (0x87C10, 0x87C94) finds under the
 * pointer. Its SmapHitEnv is made of nothing but the shell's own cells, so the shell builds it here and
 * the caller hook at WorldHooks.pfnHitTestStrategSelect stays an override rather than a requirement. The
 * dispatcher hands the view through the seam, and the pending-pick pair the flag-27 tail writes is the
 * viewport block's own 0x186414 / 0x186418 (SmapView + 0x2C / + 0x30), the same pair
 * World_SmapConfirmCraftTargetThunk reads. */
static u8 World_GeoHitTestStrategSelect(const SmapView *pView, i32 nX, i32 nY, void *pUser)
{
    PufoWorldState *pW = g_pActiveWorld;
    SmapHitEnv      env;

    (void)pUser; /* the dispatcher's pUser is its sound user, which this env carries as pSoundUser */

    if (pW == NULL) {
        return 0;
    }
    memset(&env, 0, sizeof(env));
    env.pStripPad            = pW->ui.apPad[GEOUI_PAD_186BBC];  /* *pGeoscapeStripPad @ 0x186BBC, the strip gate */
    env.pStatusPanel         = pW->ui.pStatusPanel;             /* unk_18641C, where the lines are built */
    env.pLines               = &pW->msgLines;                   /* the root message list @ 0x158890 */
    env.pStrategList         = &pW->strategList;               /* 0x1864F8 + 0x3C */
    env.pTextRecs            = (pW->pAssets != NULL) ? pW->pAssets->pTextRecs : NULL; /* records 1145..1147 */
    env.apBases              = pW->apBases;                    /* Map + 0x1174, the base search */
    env.pnBaseCount          = &pW->nBaseCount;                /* 0x159D80, read SIGNED (0x82EE8) */
    env.apSiteRecs           = pW->aSiteRecs;                  /* byte_186C24 == 0x186C40, the name table */
    env.apCraft              = pW->apCraft;                    /* Map + 0x10AC */
    env.pnCraftCount         = &pW->bCraftCount;               /* Map_nCraftCount @ 0x114B4C */
    env.pbSelectionFlag      = &pW->bMapSelectionFlag;         /* 0x159E84 */
    env.pnPendingPickX       = &pW->view.nPickX;               /* 0x186414, the flag-27 tail's write */
    env.pnPendingPickY       = &pW->view.nPickY;               /* 0x186418 */
    env.pnScreenId           = &pW->screen.nGeoscapeScreen;    /* 0x186BC4, written 18 by the base branch */
    env.pnSelectedStrategIdx = &pW->nSelectedStrategIdx;       /* 0x186BE4 */
    env.pnSelectedShipIdx    = &pW->nSelectedShipIdx;          /* 0x187054 */
    env.pnMsgShipIdx         = &pW->nMsgShipIdx;               /* 0x159E74 */
    env.pTime                = &pW->geo.time;                  /* GameTime_Pause @ 0x5d6ac */
    env.pbFlag18708C         = &pW->bFlag18708C;
    env.pbFlag187090         = &pW->bFlag187090;
    env.pbFlag187098         = &pW->bFlag187098;
    env.pbFlag1870A0         = &pW->bFlag1870A0;               /* the multi-select box byte */
    env.pnRadarPulseTimer    = &pW->nRadarPulseTimer;          /* 0xAC224 */
    /* pOptPanelShips @ 0x159E18, read at 0x83494 right after OptionsScreen_Open ran: the names panel the
     * open half just allocated, or NULL when its close half just freed it. Passed as the cell, so the
     * value is the one the original's global load would see at that same instruction. */
    env.ppOptPanelShips      = pW->options.apPanel;
    env.pOptionsHooks        = &pW->optionsHooks;
    env.pfnPlaySample        = pW->pfnPlaySample;
    env.pSoundUser           = pW->pSoundUser;
    return Smap_HitTestStrategSelect(pView, &env, nX, nY);
}

/* Smap_BeginCraftSelect @ 0x838dc, the descent panel's launch control (0x91578, 0x9180C) and the
 * mission-launch decision tree. Its env is the world's own, built by World_WireMissionSeams. */
static void World_BeginCraftSelectThunk(void)
{
    PufoWorldState *pW = g_pActiveWorld;

    if (pW == NULL) {
        return;
    }
    (void)Smap_BeginCraftSelect(&pW->craftEnv);
}

/* Geoscape_PrepScreenOpen @ 0x85194 and Geoscape_PrepScreenOpenSfx @ 0x852b8: the panel's two footer
 * controls (0x8C9E0, 0x8CA04). The Sfx twin is the only one of the six openers that leaves the tick
 * interval alone. */
static void World_PrepScreenOpenThunk(void)
{
    PufoWorldState       *pW = g_pActiveWorld;
    ScreenResetOpenEnv    env;

    if (pW == NULL) {
        return;
    }
    World_FillScreenOpenEnv(pW, &env);
    Geoscape_PrepScreenOpen(&env);
}

static void World_PrepScreenOpenSfxThunk(void)
{
    PufoWorldState       *pW = g_pActiveWorld;
    ScreenResetOpenEnv    env;

    if (pW == NULL) {
        return;
    }
    World_FillScreenOpenEnv(pW, &env);
    Geoscape_PrepScreenOpenSfx(&env);
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
     * Smap_BeginCraftSelect through pW->craftEnv. Case 3 (the base-info screen) IS wired: the built flag,
     * the pad slot the case frees at 0x8BE10 and the rebuild hook at 0x8BDF8. Case 6 (the hangar) is wired
     * too: its six plane flags and its three rebuild hooks. What is NOT: research, manufacturing, transfer,
     * buy/sell and ufopaedia blocks (their cases keep NULL gates, which
     * the dispatcher tests), case 19's bankruptcy tail (its two callees, listed with the wiring below -- the
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
    /* Case 6 (0x8C01C..0x8C09C), the hangar: three of its four arms are a rebuild hook, and the fourth
     * clears bPlaneShowCraftDetail before the screen goes back to 1 and the panels are released. */
    pC->pbPlaneListBuilt      = &pW->bPlaneListBuilt;      /* 0x161F24, tested at 0x8C01C */
    pC->pbPlaneStores         = &pW->bPlaneStoresBuilt;    /* 0x161F28, tested at 0x8C034 */
    pC->pbStatPanelA          = &pW->planeStatPanels.bPanelAOpen;  /* 0x161F18, cleared at 0x8C0B0 */
    pC->pbStatPanelB          = &pW->planeStatPanels.bPanelBOpen;  /* 0x161F1C, cleared at 0x8C0B4 */
    pC->pbPlaneBuilt          = &pW->bPlaneBuilt;          /* 0x161F08, the gate at 0x8C07C */
    pC->pbPlaneDetail         = &pW->bPlaneShowDetail;     /* 0x161F20, cleared at 0x8C08C */
    pC->pfnBuildPlaneListPanels = World_PlaneBuildListHook;     /* 0x8C028 */
    pC->pfnBuildPlaneControls   = World_PlaneBuildControlsHook; /* 0x8C044 */
    pC->pfnBuildPlanePanels     = World_PlaneBuildPanelsHook;   /* 0x8C098 */
    /* Case 8 (0x8BDB0..0x8BDD8), the research screen: with a row selected the close RUNS the start button
     * (0x8BDB4) and only then falls back to screen 1; without one it goes to 1 straight away and releases
     * the three panels through the same build switch the opener ran (0x8BDD8). */
    pC->pbResearchBuilt    = &pW->bResearchBuilt;                  /* bResearchPanelsBuilt @ 0x160C7C */
    pC->pbRowSelected      = &pW->researchScratch.bFlag160C80;    /* unk_160C80, tested at 0x8BDB0 */
    pC->pResearchStart     = &pW->researchStart;                   /* ResearchScreen_StartSelected, 0x8BDB4 */
    pC->pResearchBuild     = &pW->researchBuild;                   /* ResearchScreen_BuildPanels, 0x8BDD8 */
    pC->pnFunds          = &pW->nFunds;
    pC->anFundBar        = pW->economy.anFundBar;          /* 0x186594..0x1865A4 */
    pC->ppFundPad        = &pW->pFundPad;                  /* 0x1595DC */
    /* Case 3 (0x8BDE8..0x8BE18): back to screen 1, the panels rebuilt if they are still built, then the
     * pad freed. Live now that nav slot 1 opens the screen. */
    pC->pbBaseInfoBuilt        = &pW->bBaseInfoPanelsBuilt;            /* 0x15883C */
    pC->ppBaseInfoPad          = &pW->pBaseInfoPad;                    /* 0x158828, freed at 0x8BE10 */
    pC->pfnBuildBaseInfoPanels = World_BaseInfoBuildHook;              /* 0x8BDF8 */
    pC->pFacToggle       = &pW->facToggleEnv;
    pC->pSoldierScreen   = &pW->solEnv;
    pC->pOptionsHooks    = &pW->optionsHooks;
    /* Case 18's own three cells (0x8BCD0 / 0x8BCFC / 0x8BD10): the scratch mode byte, the scratch
     * "a menu is up" byte, and the toggle, plus the two handles the arm hands to 0x8BD08 and 0x8BD18 --
     * the SaveAudioEnv the cancel hotspot runs, and the OptionsScreen_Open handle whose -1 call at
     * 0x8BD18 releases the overlay. All five are the shell's own cells now (OptionsOwners). */
    pC->pnOptionsMode    = &pW->abOptionsScratch[0];              /* gOptionsScratch[0], LDRSB 0x8BCD0 */
    pC->pbOptionsActive  = (const u8 *)&pW->abOptionsScratch[4];  /* gOptionsScratch[4] @ 0x159E8C */
    pC->pbOptionsOpen    = &pW->bOptionsScreenOpen;               /* bOptionsScreenOpen @ 0x159E80 */
    pC->pSaveAudio       = &pW->options.saveAudio;               /* 0x8BD08 */
    pC->pOptionsOpen     = &pW->options.open;                    /* 0x8BD18, OptionsScreen_Open(-1) */
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
     * only while bBaseFacPlaceArmed @ 0x158888 is CLEAR (0x95770 ANDS / BEQ). Screen 6's two pairs: the
     * stores-nav pair at the block's +0x64 / +0x68 while bPlaneStoresCommitted is set, and the crew-nav
     * pair at +0x5C / +0x60 while bPlaneListPanelsBuilt is set -- the original runs them as two passes
     * over one dword each, which is the same four forced clicks as two consecutive-control groups.
     * The remaining groups belong to the soldier, research, manufacturing, transfer, buy/sell,
     * ufopaedia, start and options screens -- see the group table in game/pufo_geoscape.h. */
    UiControl *apFacCtl[2] = { &pW->ctlFacUp, &pW->ctlFacDown };
    UiControl *apPlaneStoreCtl[PUFO_PLANE_STORE_NAV] = { pW->planeStorePanels.apNavCtl[0],
                                                         pW->planeStorePanels.apNavCtl[1] };
    UiControl *apPlaneListCtl[PUFO_PLANE_LIST_NAV]   = { pW->planeListPanels.apNav[0],
                                                         pW->planeListPanels.apNav[1] };
    GeoscapePulseCtlGroup aFacGroups[1];
    GeoscapePulseCtlGroup aPlaneGroups[PUFO_PLANE_STORE_NAV];
    GeoscapePulseScreen   aScreens[2];

    aFacGroups[0].apCtl = apFacCtl;
    aFacGroups[0].nCount = 2;
    aFacGroups[0].pbGate = NULL;
    aFacGroups[0].pbNotGate = &pW->bArmedPlacement;
    aScreens[0].nScreenId = 7;
    aScreens[0].pGroups = aFacGroups;
    aScreens[0].nGroupCount = 1;

    aPlaneGroups[0].apCtl = apPlaneStoreCtl;
    aPlaneGroups[0].nCount = PUFO_PLANE_STORE_NAV;
    aPlaneGroups[0].pbGate = &pW->bPlaneStoresBuilt;   /* 0x161F28 */
    aPlaneGroups[0].pbNotGate = NULL;
    aPlaneGroups[1].apCtl = apPlaneListCtl;
    aPlaneGroups[1].nCount = PUFO_PLANE_LIST_NAV;
    aPlaneGroups[1].pbGate = &pW->bPlaneListBuilt;     /* 0x161F24 */
    aPlaneGroups[1].pbNotGate = NULL;
    aScreens[1].nScreenId = 6;
    aScreens[1].pGroups = aPlaneGroups;
    aScreens[1].nGroupCount = PUFO_PLANE_STORE_NAV;

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
    env.nScreenCount  = 2;
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
 *   - The screen ids the shell can now reach are 0, 1, 2 (the map and the placement screen), 3 (nav slot
 *     1), 4 and 5 (nav slot 2 and SoldierScreen_SelectListed 0x64a9c), 6 (nav slot 3 and
 *     BasePlaneScreen_Open 0x617f4: its arm, control seam, draw env and detail env), 7 (nav slot 4, and the
 *     map click's own 0x8B64C), 15 and 19 (the funding screen the monthly close sets). Five nav slots stay
 *     World_NoopClick -- ResearchScreen_Open (8), ManufScreen_Open (9), TransferScreen_Open (10) and
 *     BuySellScreen_OpenBuy/Sell (11, 12) -- because their Init envs and their arms (pResearchDraw,
 *     pManufPad, pTransferPad, pBuySellPad) are not built here, and Geoscape_Draw dereferences several
 *     unwired arm envs with no test of their own.
 *   - GeoscapePointerEnv's per-screen fields for those same screens (the transfer, soldier-name, craft,
 *     research, manufacturing, site-info, buy/sell, screen-16 and options blocks) stay NULL; the hangar's
 *     own block is wired (its 16 controls and its six panel pairs, re-read every frame in
 *     World_RefreshGeoscapeFrame). The
 *     dispatcher reaches them only from those screen ids. The strip block IS wired now, and that also
 *     makes the screen-1 arm live for the first time: `Geoscape_OnPointer` case 1 walks the ten slots at
 *     [root + 0x120] (0x87E30), which are apNavCtl -- the very controls the nav handlers sit on -- so a
 *     press on a nav row is a real handler call, and 0x87F34 makes the strip hit the base selection.
 *     Before this the arm dereferenced a NULL apStripCtl and a click anywhere on the placement screen
 *     faulted; the shell had never taken that path because its own scaffolding commands pressed the
 *     controls directly. One shell-owned block that could not be expressed before -- apFacCtl, the two
 *     build-dialog controls the shell holds as two embedded UiControls -- is now the arm's own array,
 *     pW->apFacPanelCtl. apRadarCtl wants 0x159684's control set, which the shell does not build.
 */
/* The options screen's owners, wired once per bring-up. Every field is a pointer onto a cell the state
 * already owns, so no env carries a copy of a live byte: the tab byte is abOptionsScratch[0] (the same
 * byte Geoscape_DrawScreen 0x6C68C, Geoscape_OnPointer 0x89128 and Geoscape_CloseUI 0x8BCD0 read), the
 * "a menu is up" byte is abOptionsScratch[4], and the toggle is bOptionsScreenOpen. The by-value fields
 * -- the four Map option bytes, the soldier count, and the whole draw env -- follow the frame in
 * World_RefreshGeoscapeFrame. */
static void World_WireOptions(PufoWorldState *pW, const WorldHooks *pHooks)
{
    OptionsOwners *pO = &pW->options;
    UiControl     *pCtlA = pW->ui.apCtl[GEOUI_CTL_186BB4]; /* *pGeoscapeCtlA @ 0x186BB4: cancel / OK / Нет */
    UiControl     *pCtlB = pW->ui.apCtl[GEOUI_CTL_186BB0]; /* *pGeoscapeCtlB @ 0x186BB0: the quit page's Да */
    BootAssets    *pAssets = pW->pAssets;
    i32            i;

    for (i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {
        pO->ctl.apList[i] = &pO->aListCtl[i];   /* aOptionsListCtl[0..6] @ 0x159E20..0x159E38 */
    }
    for (i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        pO->ctl.apMusic[i] = &pO->aMusicCtl[i]; /* [9..14] @ 0x159E44, aOptionsMusicBtn */
        pO->ctl.apSfx[i]   = &pO->aSfxCtl[i];   /* [15..20] @ 0x159E5C, aOptionsSfxBtn */
    }

    pO->vol.pCtl = &pO->ctl;
    if (pHooks != NULL && pHooks->pTactOptions != NULL) {
        /* The four Map option bytes the volume buttons and the registry write share: Map_nSoundVolume
         * @ 0x128A94, Map_nMusicVolume @ 0x128A98, Map_bSoundOff @ 0x128A93, Map_bMusicOff @ 0x128A92.
         * pHooks->pTactOptions is the caller's WRITABLE alias of the very block pW->pOptions names, so
         * the geoscape's radio buttons and the tact bank's change the same bytes -- and the same bytes
         * Sound_ApplyMusicVolume / Sound_ApplySfxVolume read at 0x9e454 / 0x9e4dc. */
        pO->vol.pnSfxVol         = &pHooks->pTactOptions->nSoundVolume;
        pO->vol.pnMusicVol       = &pHooks->pTactOptions->nMusicVolume;
        pO->vol.pbSfxOff         = &pHooks->pTactOptions->bSoundOff;
        pO->vol.pbMusicOff       = &pHooks->pTactOptions->bMusicOff;
        pO->saveAudio.pnSfxVol   = &pHooks->pTactOptions->nSoundVolume;   /* the itoa at 0x6B828 / 0x6B84C */
        pO->saveAudio.pnMusicVol = &pHooks->pTactOptions->nMusicVolume;
        pO->saveAudio.pbSfxOff   = &pHooks->pTactOptions->bSoundOff;      /* the shared "0" at 0x6B89C */
        pO->saveAudio.pbMusicOff = &pHooks->pTactOptions->bMusicOff;      /* and the "1" at 0x6B878 */
    }

    pO->tab.pCtl             = &pO->ctl;
    pO->tab.pbTab            = &pW->abOptionsScratch[0];
    pO->tab.pSaveCtl         = pCtlA;
    pO->tab.pBackCtl         = pCtlB;
    pO->tab.pfnBindSaveSlots = World_OptionsSaveTab;   /* 0x6b49c, row 0's handler at 0x6B6DC */
    pO->tab.pfnBindLoadSlots = World_OptionsLoadTab;   /* 0x6b588, row 1's at 0x6B6E8 */
    pO->tab.pfnSetTab3       = World_OptionsQuitTab;   /* 0x6b674, row 2's at 0x6B6F4 */
    pO->tab.pfnSetTab4       = World_OptionsVolumeTab; /* 0x6b6b0, row 3's at 0x6B700 */
    if (pHooks != NULL) {
        pO->tab.pfnFormatSlots = pHooks->pfnFormatSlotList; /* SaveSlot_FormatList @ 0x45904, 0x6B558 */
        pO->tab.pUser          = pHooks->pFormatSlotListUser;
    }
    /* Recorded gap: the seven slot handlers (SaveGame_Slot0..6_CloseUI 0x6af80..0x6b148,
     * LoadGame_Slot0..6_InitOptions 0x6b194..0x6b404) are ported and gated but reach SaveGame / LoadGame
     * through a DatFile the caller opens, and the shell opens slot files only on its tact bank path
     * (World_SaveSlot). They stay NULL, so a press on a slot row hits a cleared handler. */

    pO->init.pCtl             = &pO->ctl;
    pO->init.pVolEnv          = &pO->vol;
    pO->init.pTabEnv          = &pO->tab;
    pO->init.pbControlsExist  = (u8 *)&pW->abOptionsScratch[4]; /* gOptionsScratch[4] @ 0x159E8C */
    pO->init.pSaveCtl         = pCtlA;   /* 0x6BF1C installs Options_SaveAudioSettings here */
    pO->init.pBackCtl         = pCtlB;   /* 0x6BF2C installs OptionsScreen_ReturnToTitle here */
    pO->init.pfnSaveAudio     = World_OptionsCancelClick;
    pO->init.apfnMusicVol[0]  = World_OptionsMusicVol0; /* 0x6C02C..0x6C0B0, the twelve radio handlers */
    pO->init.apfnMusicVol[1]  = World_OptionsMusicVol1;
    pO->init.apfnMusicVol[2]  = World_OptionsMusicVol2;
    pO->init.apfnMusicVol[3]  = World_OptionsMusicVol3;
    pO->init.apfnMusicVol[4]  = World_OptionsMusicVol4;
    pO->init.apfnMusicVol[5]  = World_OptionsMusicVol5;
    pO->init.apfnSfxVol[0]    = World_OptionsSfxVol0;
    pO->init.apfnSfxVol[1]    = World_OptionsSfxVol1;
    pO->init.apfnSfxVol[2]    = World_OptionsSfxVol2;
    pO->init.apfnSfxVol[3]    = World_OptionsSfxVol3;
    pO->init.apfnSfxVol[4]    = World_OptionsSfxVol4;
    pO->init.apfnSfxVol[5]    = World_OptionsSfxVol5;
    if (pHooks != NULL) {
        pO->init.pfnLoadPads = pHooks->pfnLoadPads;
        pO->init.pPadUser    = pHooks->pLoadPadsUser;
    }

    pO->saveAudio.pbTab            = &pW->abOptionsScratch[0];
    pO->saveAudio.pbControlsExist  = (u8 *)&pW->abOptionsScratch[4];
    pO->saveAudio.pbOptionsOpen    = &pW->bOptionsScreenOpen;
    pO->saveAudio.pbSelectionFlag  = &pW->bMapSelectionFlag;
    pO->saveAudio.pTime            = &pW->geo.time;      /* 0x159618; its bPaused is the byte at 0x15961C */
    pO->saveAudio.pClose           = &pW->closeEnv;      /* Geoscape_CloseUI @ 0x8b94c, 0x6B7C4 / 0x6B7F0 */
    pO->saveAudio.pTabEnv          = &pO->tab;           /* 0x6B8AC's OptionsScreen_LayoutButtons */

    pO->open.pbOptionsOpen    = &pW->bOptionsScreenOpen;   /* 0x6D0C8 / 0x6DC40 */
    pO->open.pbAnimStarted    = &pW->bUiAnimStarted;       /* 0x6D0DC */
    pO->open.pbSelectionFlag  = &pW->bMapSelectionFlag;    /* 0x6D1A8 */
    pO->open.pnScreenId       = &pW->screen.nGeoscapeScreen; /* 0x6DC38 */
    pO->open.pnBaseCount      = &pW->nBaseCount;           /* 0x159D80, read SIGNED at 0x6D118 */
    pO->open.pnCraftCount     = &pW->bCraftCount;          /* Map + 0x1194, re-read at 0x6DC08 */
    pO->open.apBases          = pW->apBases;               /* 0x114B2C */
    pO->open.apCraft          = pW->apCraft;               /* 0x114A64 */
    pO->open.apSoldiers       = pW->apSoldiers;
    pO->open.apPairCtl        = pO->apPairCtl;             /* aOptionsListCtl + 0x1C @ 0x159E3C / 0x159E40 */
    pO->open.ppPanelName      = &pO->apPanel[0];           /* 0x159E18 */
    pO->open.ppPanelStores    = &pO->apPanel[1];           /* 0x159E78 */
    pO->open.ppPanelRegion    = &pO->apPanel[2];           /* 0x159E7C */
    pO->open.ppPanelCargo     = &pO->apPanel[3];           /* 0x159E1C */
    pO->open.pSaveCtl         = pCtlA;                     /* 0x6D24C / 0x6D25C / 0x6D26C */
    pO->open.pfnPanelPrev     = World_OptionsPanelPrev;    /* 0x6D208 */
    pO->open.pfnPanelNext     = World_OptionsPanelNext;    /* 0x6D214 */
    pO->open.pfnOnConfirm     = World_OptionsConfirmClick; /* 0x6D2E8 */
    pO->open.pfnSaveAudio     = World_OptionsCancelClick;  /* 0x6D24C, the same handler Init installs */
    if (pAssets != NULL) {
        pO->open.apObjects   = pAssets->apObjects;
        pO->open.nObjectCount = pAssets->nObjectCount;
        pO->open.pTextRecs   = pAssets->pTextRecs;         /* aTextRecs: the four state labels 149..152
                                                            * and the nine region labels 1036 / 1165..1172 */
    }
    if (pHooks != NULL) {
        pO->open.pfnLoadPads = pHooks->pfnLoadPads;
        pO->open.pPadUser    = pHooks->pLoadPadsUser;
    }

    pO->confirm.ppPanelName         = &pO->apPanel[0];    /* 0x6CF3C / 0x6CF4C */
    pO->confirm.ppPanelStores       = &pO->apPanel[1];    /* 0x6CF6C, the readiness gate */
    pO->confirm.pnMsgShipIdx        = &pW->nMsgShipIdx;   /* 0x159E74 */
    pO->confirm.pnSelectedShipIdx   = &pW->nSelectedShipIdx; /* 0x187054 */
    pO->confirm.pbSelectionFlag     = &pW->bMapSelectionFlag; /* 0x6CF78 */
    pO->confirm.pbFinalMissionCtl   = &pW->bFinalMissionCtlArmed; /* byte_187068, 0x6CFE0 */
    pO->confirm.apCraft             = pW->apCraft;
    pO->confirm.aTopics             = pW->aTopics;        /* aResearchTopics[28], the Cydonia gate */
    pO->confirm.pLines              = &pW->msgLines;      /* the root message list @ 0x158890, 0x6CFA4 */
    pO->confirm.pTime               = &pW->geo.time;      /* 0x6CFE4's GameTime_Pause */
    pO->confirm.pfnPlaySample       = pW->pfnPlaySample;  /* Sound_Play(7) at 0x6CF84, and the same seam
                                                           * UiLineList_AppendWithSound takes at 0x6CFA4 */
    pO->confirm.pUser               = pW->pSoundUser;

    /* The press arm reads the pair slots and the names panel OUT OF their cells at the call (0x89370
     * `LDR R0,[R0,#0x1C]`, 0x893B4 / 0x8AF1C), so the env carries the cells rather than copies: the
     * cancel inside a press frees what the same press's tail then reads. */
    pW->ptrEnv.apOptPairCtl        = pO->apPairCtl;
    pW->ptrEnv.ppOptPanelShips     = &pO->apPanel[0];

    pO->screenHooks.pfnPlaySample              = World_ScreenPlaySample;      /* sample 17, 0x84EA4 / 0x84ED4 */
    pO->screenHooks.pfnOptionsRebuildWithSound = World_OptionsRebuildThunk;    /* 0x84EC0's tail call */
    pO->screenHooks.pfnOptionsOpenWithSound    = World_OptionsOpenWithSoundThunk; /* 0x84EF4's tail call */
    pO->screenHooks.pUser                      = pW;

    pO->draw.pView           = &pW->view;                 /* gSmapCam @ 0x1863E8 */
    pO->draw.pStripPad       = pW->ui.apPad[GEOUI_PAD_186BBC]; /* [0x186BBC], the zoom-strip pad */
    pO->draw.pWorldBits      = (const u8 *)pW->ui.pWorldSize;  /* *pSmapWorldBits @ 0x186BC0 */
    pO->draw.pStrategList    = &pW->strategList;          /* 0x1864F8 + 0x3C, 0x6C348 */
    pO->draw.apClockPads     = pW->ui.apClockDigitPads;   /* apClockDigitPads @ 0x186BF0 */
    pO->draw.pTime           = &pW->geo.time;             /* 0x6C35C / 0x6C364 */
    pO->draw.pbMapSelection  = &pW->bMapSelectionFlag;    /* 0x6C180 */
    pO->draw.pbOptionsOpen   = &pW->bOptionsScreenOpen;   /* 0x6C368 */
    pO->draw.abOptionsScratch = pW->abOptionsScratch;     /* [0] 0x6C68C, [4] 0x6C640 */
    pO->draw.pAnim           = &pW->frame.anim;           /* Ui_AnimExpandBlit's root, 0x6C390 */
    pO->draw.pCtlA           = pCtlA;                     /* the cancel / OK / Нет hotspot */
    pO->draw.pCtlB           = pCtlB;                     /* the quit page's Да hotspot */
    pO->draw.apOptCtl        = pO->apCtlFlat;             /* aOptionsListCtl @ 0x159E20, 21 slots */
    pO->draw.pfnCloseUi      = World_OptionsCloseUiThunk; /* 0x6C188's Geoscape_CloseUI(&0x1864F8) */
    pO->draw.pCloseUser      = pW;
    if (pAssets != NULL) {
        pO->draw.pTextRecs = (const char *)pAssets->pTextRecs; /* aTextRecs @ 0xB38D0 */
    }
    if (pHooks != NULL) {
        pO->draw.pSlotLabels = pHooks->pSlotLabels;       /* MainMenu_pSlotCtx @ 0x156938, 0x6CBFC */
        pO->draw.pColor      = pHooks->pColor;
    }
    /* The pad-blit seam comes from pHooks->pHit, not from pW->pHitCtx: World_WireGeoscapeClock stores
     * that one at the END of World_WireGeoscapeFrame, and the other arms here read pHooks->pHit too. */
    if (pHooks != NULL && pHooks->pHit != NULL) {
        pO->draw.pfnCtlBlit     = pHooks->pHit->pfnBlit;
        pO->draw.pfnCtlBlitEx   = pHooks->pHit->pfnBlitEx;
        pO->draw.pCtlBlitUser   = pHooks->pHit->pUser;
    }
    /* OptionsScreen_ReturnToTitle's three callees -- Geoscape_CloseUI (0x6B480), GeoscapeUi_FreeAll
     * (0x6B488) and GameUI_InitStartup (0x6B490) -- belong to the startup-screen owner in pufo_app.c,
     * so ctlB keeps no handler here: the quit page's Да answers nothing yet. Recorded in PLAN. */
    pO->init.pfnReturnToTitle = NULL;
}

void World_WireGeoscapeFrame(PufoWorldState *pW, const WorldHooks *pHooks)
{
    GeoscapeFrameState *pF = &pW->frame;
    GeoscapeUiState    *pUi = &pW->ui;
    BootAssets         *pAssets = pW->pAssets;
    GeoscapePointerEnv *pPtr = &pW->ptrEnv;
    const UiControlHitCtx *pHit = (pHooks != NULL) ? pHooks->pHit : NULL;
    GfxColorEnv        *pColor = (pHooks != NULL) ? pHooks->pColor : NULL;
    const char         *pszText = (pAssets != NULL) ? (const char *)pAssets->pTextRecs : NULL; /* aTextRecs @ 0xB38D0 */
    i32                 i;

    memset(pF, 0, sizeof(*pF));
    memset(pPtr, 0, sizeof(*pPtr));
    World_WireOptions(pW, pHooks);

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
    /* The panel arms' click handlers. Each arm stores one of these into a control as it draws, and ten
     * arms return before drawing when the one they need is NULL -- which is why none of the geoscape's
     * message-answer panels ever appeared. Built at click time, not held here; see the block above
     * World_WireMissionSeams for the handler table and for the two that stay NULL. */
    pF->panel.pfnCtlSfx           = World_PrepScreenOpenSfxThunk;   /* 0x8C9E0 */
    pF->panel.pfnCtlOpen          = World_PrepScreenOpenThunk;      /* 0x8CA04 */
    pF->panel.pfnSelectTarget     = World_SmapPointerUpThunk;       /* 0x8EC84, 0x90640, 0x91CD0 */
    pF->panel.pfnClearSelection   = World_SmapClearSelectionThunk;  /* 0x8EC90, 0x91CE0 */
    pF->panel.pfnResolveInterceptUI = World_SmapResolveInterceptThunk; /* 0x8F39C and thirteen more */
    pF->panel.pfnDismissSelected  = World_SmapDismissSelectedThunk; /* 0x8F53C, 0x8FFDC */
    pF->panel.pfnConfirmCraftTarget = World_SmapConfirmCraftTargetThunk; /* 0x8F688 */
    pF->panel.pfnConfirmSelected  = World_SmapConfirmSelectedThunk;
    pF->panel.pfnConfirmSelectedAlt = World_SmapConfirmSelectedAltThunk; /* 0x8F930 */
    pF->panel.pfnSetWaypoint      = World_SmapSetWaypointThunk;     /* 0x8F940 */
    pF->panel.pfnSelectMapTarget  = World_SmapSelectMapTargetThunk;
    pF->panel.pfnCancelTarget     = World_SmapCancelTargetThunk;    /* 0x90C74, 0x910A8, 0x9139C */
    pF->panel.pfnBeginCraftSelect = World_BeginCraftSelectThunk;    /* 0x91578, 0x9180C */

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

    /* Case 18 (0x942F4..0x942F8) has no arm code of its own: Geoscape_Draw calls Geoscape_DrawScreen
     * with the 21-slot control array and that function draws the map, the overlay and the menu. The
     * shell's env for it is the options owner's own; its by-value fields follow the frame. */
    pF->draw.pOptionsScreen = &pW->options.draw;

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
     * the sound user, which is the same value the hit test's env carries as pSoundUser. */
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
    /* Screen 18's two gates. The dispatcher's case-18 arm derefs both with no null test, exactly as
     * 0x89110 (LDRB [gOptionsScratch,#4]) and 0x89338 (LDRB bOptionsScreenOpen) do, because in the
     * original those globals are always there. With the shell's cells wired the arm is safe while the
     * options screen itself is not built yet: both bytes are zero, so 0x89110 falls to the shared tail
     * at 0x8932C, the confirm gate at 0x89344 fails, and the press returns the hit-test result. The
     * control arrays the same arm walks are the shell's own: [0..3] at 0x892D0 and [0..6] at 0x89210 are
     * the seven menu rows, and 0x89160 walks aOptionsMusicBtn then aOptionsSfxBtn. The two pair controls
     * (0x89370) and the names panel (0x893B4) are by-value and follow the overlay in
     * World_RefreshGeoscapeFrame, because OptionsScreen_Open allocates them and its close half frees
     * them. */
    pPtr->pbOptionsConfirm    = &pW->bOptionsScreenOpen;              /* 0x159E80 */
    pPtr->pbOptionsPageActive = (const u8 *)&pW->abOptionsScratch[4]; /* 0x159E8C */
    pPtr->pnOptionsPage       = &pW->abOptionsScratch[0];             /* 0x159E88, read SIGNED */
    pPtr->apOptListCtl        = pW->options.apCtlFlat;  /* aOptionsListCtl @ 0x159E20: [0..6] are the rows */
    pPtr->apOptMusicCtl       = pW->options.ctl.apMusic; /* aOptionsMusicBtn @ 0x159E44 */
    pPtr->apOptSfxCtl         = pW->options.ctl.apSfx;   /* aOptionsSfxBtn @ 0x159E5C */
    pPtr->pView          = &pW->view;
    pPtr->pZoomOverlay   = pUi->apPad[GEOUI_PAD_186BBC]; /* 0x87C48 reads its uHeight as the strip */
    pPtr->pnTapCooldown  = &pF->g.nTapCooldown;
    pPtr->pnRadarPulseTimer = &pW->nRadarPulseTimer;     /* 0xAC224 */
    pPtr->pbRadarScreenActive = &pW->bRadarScreenActive;
    pPtr->pStatusPanel   = pUi->pStatusPanel;
    /* The base-tab strip the screen-1 pointer arm walks (0x87E30) and then hits (0x87F2C). Its ten
     * controls ARE apNavCtl @ 0x159888 -- the same ten objects Geoscape_BuildNavControls installs the
     * nav handlers on -- so a press on a nav row reaches that row's handler through this arm. The strip
     * gate bytes: the two base indexes and the tap flag the hit clears, plus the base-info gate. */
    pPtr->apStripCtl                = pW->nav.apNav;                  /* PUFO_GEO_STRIP_CTLS of 0x159888 */
    pPtr->stripHit.pbBaseCount      = (const u8 *)&pW->nBaseCount;    /* 0x159D80, read SIGNED (0x67204) */
    pPtr->stripHit.pbSelectedBase   = (const u8 *)&pW->nSelectedBase; /* 0x159D81 (0x67224) */
    pPtr->stripHit.pbTapFlag        = &pW->bPlaceActive;              /* 0x1870BC, cleared at 0x67250 */
    pPtr->stripHit.pbGateBaseInfo   = &pW->bBaseInfoPanelsBuilt;      /* unk_15883C */
    /* The transfer gate (unk_1870EC) and the psi gate (byte_15EEF0) stay NULL: their only writers are
     * the transfer and psi screens, which the shell does not run, and Base_HitTestFacilityStrip tests
     * both before it reads them (0x6723C). */
    /* Screen 3's own arm (0x88A08..0x88A24): the two controls it hit-tests are `unk_158828 + 4`, which
     * is BaseInfoScreen_Init's control pair -- the field is named for the base-attack screen because that
     * is where the earlier reading first saw the block, but 0x15882C / 0x158830 are the base-info
     * controls (OnOk and the panel-mode toggle). The row-select byte is byte_1870F8, the same one the
     * draw's overlay arm and Init use, and the two base indexes are byte_1870F4 (read SIGNED at 0x88AB0)
     * and byte_158834. */
    pPtr->apBaseAtCtl       = pW->apBaseInfoCtl;                  /* 0x15882C / 0x158830 */
    pPtr->pbTransferRowSel  = &pW->bBaseInfoOverlayOpen;          /* byte_1870F8 */
    pPtr->pnTransferBaseIdx = &pW->nBaseInfoWorkBase;             /* byte_1870F4 */
    pPtr->pbStripHitBase    = &pW->nShownBaseIdx;                 /* byte_158834 */
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
    /* The map select test. Its env is the shell's own -- World_GeoHitTestStrategSelect builds it from
     * the cells above -- so the press dispatcher reaches a real answer by default and the caller's hook
     * only replaces it. */
    pPtr->pfnHitTestStrategSelect = (pHooks != NULL && pHooks->pfnHitTestStrategSelect != NULL)
                                        ? pHooks->pfnHitTestStrategSelect
                                        : World_GeoHitTestStrategSelect;

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

    /* The base-info screen, case 3 (0x93F08..0x94104). Nav slot 1 opens it (0x67B80). The arm's pad slot
     * IS the slot BaseInfoScreen_Init allocates (0x158828), the two controls it blits are the ones Init
     * allocates at 0x15882C (OnOk) and 0x158830 (the panel-mode toggle), and the close control is the
     * shared one Init moves to (224, 1) at 0x57738/0x57744. The screen's own base-select overlay
     * (byte_1870F8) stays closed: its writers are the psi screen and Smap_OnBaseSelect, which the shell
     * does not run, so the draw's adopt arm -- byte_158834 != byte_1870F4 rebuilds the panels -- cannot
     * fire here. Init stamps 1870F8 = 0 and 1870F4 = nSelectedBase every time the screen opens (0x5778C,
     * 0x57794). */
    pW->baseInfoOpenHooks.pfnPlaySample = World_PlaySampleWithArg;      /* Sound_Play(17, 4) at 0x61688 */
    pW->baseInfoOpenHooks.pfnInit       = World_BaseInfoInitThunk;      /* 0x61690 */
    pW->baseInfoOpenHooks.pSlot         = NULL;
    pW->baseInfoOpenHooks.pUser         = pW;

    memset(&pW->baseInfoInit, 0, sizeof(pW->baseInfoInit));
    pW->baseInfoInit.ppPad             = &pW->pBaseInfoPad;             /* pBaseInfoPad @ 0x158828 */
    pW->baseInfoInit.papCtl            = pW->apBaseInfoCtl;             /* 0x15882C then 0x158830 */
    pW->baseInfoInit.pCtlClose         = pUi->apCtl[GEOUI_CTL_186B00];  /* *g_pUiCloseCtl, not a second one */
    pW->baseInfoInit.pfnOkClick        = World_BaseInfoOkThunk;         /* BaseInfoScreen_OnOk @ 0x557c8 */
    pW->baseInfoInit.pfnToggleClick    = World_BaseInfoToggleThunk;     /* BaseInfoScreen_TogglePanelMode @ 0x55754 */
    pW->baseInfoInit.pfnCloseClick     = World_CloseGeoscapeThunk;      /* BaseInfoScreen_CloseThunk @ 0x557bc */
    pW->baseInfoInit.pfnLoadPads       = pW->pfnLoadPads;               /* UiControl_Setup's pad triple */
    pW->baseInfoInit.pPadUser          = pW->pLoadPadsUser;
    pW->baseInfoInit.pnWorkBaseIdx     = &pW->nBaseInfoWorkBase;        /* byte_1870F4 */
    pW->baseInfoInit.pnSelectedBaseIdx = &pW->nSelectedBase;            /* nSelectedBaseIdx @ 0x159D81 */
    pW->baseInfoInit.pbOverlayOpen     = &pW->bBaseInfoOverlayOpen;     /* byte_1870F8 */
    pW->baseInfoInit.pfnBuildPanels    = World_BaseInfoBuildThunk;      /* Init's tail call at 0x5779C */

    memset(&pW->baseInfoBuild, 0, sizeof(pW->baseInfoBuild));
    pW->baseInfoBuild.pbBuilt        = &pW->bBaseInfoPanelsBuilt;       /* 0x15883C */
    pW->baseInfoBuild.pbModeFlag     = &pW->bBaseInfoPanelMode;         /* byte_AB190 @ 0xAB190 */
    pW->baseInfoBuild.pppPanel       = pW->apBaseInfoPanel;             /* [0] 0x158824, [1] 0x158820, [2] 0x158838 */
    pW->baseInfoBuild.papCtl         = pW->apBaseInfoCtl;               /* the release arm frees these two */
    pW->baseInfoBuild.apCraft        = pW->apCraft;
    pW->baseInfoBuild.pnCraftCount   = &pW->bCraftCount;                /* read UNSIGNED (0x55E38) */
    pW->baseInfoBuild.pnWorkBaseIdx  = &pW->nBaseInfoWorkBase;          /* read SIGNED (0x55E6C) */
    pW->baseInfoBuild.apSoldiers     = pW->apSoldiers;
    pW->baseInfoBuild.nSoldierCount  = pW->nSoldierCount;
    pW->baseInfoBuild.apManufCol     = pW->auManufCol;                  /* 0x15E19C */
    pW->baseInfoBuild.apResearchCol  = pW->auResearchCol;               /* 0x16022C */
    pW->baseInfoBuild.pbEntryCount   = (const u8 *)&pW->nResearchEntries; /* 0x160C4C */
    pW->baseInfoBuild.apBases        = pW->apBases;
    pW->baseInfoBuild.nBaseCount     = pW->nBaseCount;
    pW->baseInfoBuild.aSites         = pW->apSites;                     /* aSmapSites @ 0x162030 */
    pW->baseInfoBuild.nSiteCount     = pW->nSiteCount;
    pW->baseInfoBuild.abFacilityGrid = pW->abFacilityGrid;
    pW->baseInfoBuild.abGridBytes    = pW->abGridHangarBytes;           /* 0x159DBC */

    /* The toggle's three panels, in the ORIGINAL's register order: 0x158820 first, 0x158824 second,
     * 0x158838 last -- not the build order the two envs above use. Re-read every frame because the
     * panels exist only between BuildPanels' build arm and its release arm. */
    memset(&pW->baseInfoToggle, 0, sizeof(pW->baseInfoToggle));
    pW->baseInfoToggle.pbModeFlag = &pW->bBaseInfoPanelMode;
    pW->baseInfoToggle.pPanelA    = pW->apBaseInfoPanel[1];             /* unk_158820 */
    pW->baseInfoToggle.pPanelB    = pW->apBaseInfoPanel[0];             /* unk_158824 */
    pW->baseInfoToggle.pPanelC    = pW->apBaseInfoPanel[2];             /* unk_158838 */

    memset(&pW->baseInfoDraw, 0, sizeof(pW->baseInfoDraw));
    pW->baseInfoDraw.pColor         = pColor;
    pW->baseInfoDraw.pbBuilt        = &pW->bBaseInfoPanelsBuilt;
    pW->baseInfoDraw.pbModeFlag     = &pW->bBaseInfoPanelMode;
    pW->baseInfoDraw.pppPanel       = pW->apBaseInfoPanel;
    pW->baseInfoDraw.pnWorkBaseIdx  = &pW->nBaseInfoWorkBase;           /* byte_1870F4 */
    pW->baseInfoDraw.pnAltBaseIdx   = &pW->nShownBaseIdx;               /* byte_158834 */
    pW->baseInfoDraw.pbOverlayOpen  = &pW->bBaseInfoOverlayOpen;        /* byte_1870F8 */
    pW->baseInfoDraw.apSiteRecs     = pW->aSiteRecs;                    /* byte_186C40, abName at +0x1A */
    pW->baseInfoDraw.pnBaseCount    = &pW->nBaseCount;                  /* 0x159D80, read SIGNED */
    pW->baseInfoDraw.apCraft        = pW->apCraft;                      /* the moving bands' ship count */
    pW->baseInfoDraw.nCraftCount    = (i32)pW->bCraftCount;
    pW->baseInfoDraw.apBases        = pW->apBases;
    pW->baseInfoDraw.nBaseCount     = pW->nBaseCount;
    pW->baseInfoDraw.abGridBytes    = pW->abGridHangarBytes;            /* 0x159DBC */
    pW->baseInfoDraw.pfnBuildPanels = World_BaseInfoBuildThunk;         /* the adopt arm's rebuild */
    if (pAssets != NULL) {
        pW->baseInfoBuild.pTextRecs = pAssets->pTextRecs;               /* for Ship_FormatDisplayName */
        pW->baseInfoDraw.pTextRecs  = (const char *)pAssets->pTextRecs; /* TEXTS 1112 / 1113 / 1114 */
        pW->baseInfoBuild.apObjects    = pAssets->apObjects;            /* byte_113C80 / byte_113C84 slots */
        pW->baseInfoBuild.nObjectCount = (i32)pAssets->nObjectCount;
    }

    pW->baseInfoArm.ppPad          = &pW->pBaseInfoPad;                 /* 0x93F08 / 0x93F0C / 0x93F10 */
    pW->baseInfoArm.pbOverlayShown = &pW->bBaseInfoOverlayOpen;         /* 0x940DC, gates the close blit */
    pW->baseInfoArm.pCloseCtl      = pUi->apCtl[GEOUI_CTL_186B00];
    pW->baseInfoCtl.apCtl          = pW->apBaseInfoCtl;                 /* 0x940B8..0x940D4, the two slots */
    if (pHit != NULL) {
        pW->baseInfoCtl.pfnCtlBlit   = pHit->pfnBlit;
        pW->baseInfoCtl.pfnCtlBlitEx = pHit->pfnBlitEx;
        pW->baseInfoCtl.pCtlBlitUser = pHit->pUser;
    }

    pF->draw.pBaseInfoArm  = &pW->baseInfoArm;
    pF->draw.pBaseInfoCtl  = &pW->baseInfoCtl;
    pF->draw.pBaseInfoDraw = &pW->baseInfoDraw;

    /* Case 6 (0x9309C..0x93DD4), the hangar, opened by nav control slot 3.
     *
     * The screen is three panel sets behind four controls, and each set lives only between its own
     * builder's build arm and its release arm -- the same shape the base-info wiring above has, so the
     * pointers are re-read every frame and the envs stay pointed at whatever is currently alive. */

    /* The arm's flat view of the object block's control words (block +0x30..+0x68). The block owns the
     * four main and seven weapon controls; the two nav pairs belong to the crew builder and the stores
     * builder, so the flat array is rebuilt from all four sources here rather than aliased. */
    for (i = 0; i < PUFO_PLANE_MAIN_CTLS; ++i) {
        pW->apPlaneCtl[PUFO_PLANE_ARM_MAIN_FIRST + i] = pW->planeBlock.apMainCtl[i];
    }
    for (i = 0; i < PUFO_PLANE_WEAPON_CTLS; ++i) {
        pW->apPlaneCtl[PUFO_PLANE_ARM_WEAPON_FIRST + i] = pW->planeBlock.apWeaponCtl[i];
    }
    for (i = 0; i < PUFO_PLANE_LIST_NAV; ++i) {
        pW->apPlaneCtl[PUFO_PLANE_ARM_LIST_FIRST + i] = pW->planeListPanels.apNav[i];
    }
    for (i = 0; i < PUFO_PLANE_STORE_NAV; ++i) {
        pW->apPlaneCtl[PUFO_PLANE_ARM_STORE_FIRST + i] = pW->planeStorePanels.apNavCtl[i];
    }
    pW->nPlaneCursorX = (i16)pF->g.nCursorX;   /* 0x186BD0 / 0x186BD4, read LDRSH by the arm */
    pW->nPlaneCursorY = (i16)pF->g.nCursorY;

    memset(&pW->planeBuild, 0, sizeof(pW->planeBuild));
    pW->planeBuild.pbBuilt           = &pW->bPlaneBuilt;                /* bPlanePanelsBuilt @ 0x161F08 */
    pW->planeBuild.pObjects          = &pW->planeBlock;                 /* the block at 0x161A48 Init fills */
    pW->planeBuild.apCraft           = pW->apCraft;
    pW->planeBuild.pnCraftCount      = &pW->bCraftCount;                /* read LDRB (0x7CCE4) */
    pW->planeBuild.pnSelectedBaseIdx = &pW->nSelectedBase;              /* 0x159D81 */
    pW->planeBuild.pszNameBuf        = pW->szLabelScratch;              /* szRadarMsg @ 0x186B10 */
    pW->planeBuild.cchNameBuf        = (i32)sizeof(pW->szLabelScratch);
    pW->planeBuild.pfnCraftClick     = World_PlaneSelectCraftThunk;     /* BasePlaneScreen_SelectCraft @ 0x7cbc8 */
    pW->planeBuild.pfnStatusClick    = World_PlaneSelectListedThunk;    /* BasePlaneScreen_SelectListed @ 0x7cc3c */

    memset(&pW->planeInit, 0, sizeof(pW->planeInit));
    pW->planeInit.pScreen        = &pW->planeBlock;
    pW->planeInit.pCloseCtl      = pUi->apCtl[GEOUI_CTL_186B00];        /* g_pUiCloseCtl, moved to (224,1) at 0x7DDB4 */
    pW->planeInit.pBuySellCtl    = pUi->apCtl[GEOUI_CTL_186BA4];        /* unk_186BA4, the shared confirm slot (0x7DDDC) */
    pW->planeInit.apfnMainClick[0] = World_PlaneStatLabelAThunk;        /* BasePlaneScreen_DrawStatLabelA @ 0x79e00 */
    pW->planeInit.apfnMainClick[1] = World_PlaneStatLabelBThunk;        /* BasePlaneScreen_DrawStatLabelB @ 0x79fc4 */
    pW->planeInit.apfnMainClick[2] = World_PlaneBuildListThunk;         /* BasePlaneScreen_BuildListPanels @ 0x7a3f4 */
    pW->planeInit.apfnMainClick[3] = World_PlaneBuildControlsThunk;     /* BasePlaneScreen_BuildControls @ 0x7b014 */
    pW->planeInit.apfnWeaponClick[0] = World_PlaneRearmThunk;           /* BasePlane_RearmFromStores @ 0x78ed0 */
    pW->planeInit.apfnWeaponClick[1] = World_PlaneSelectWeapon25Thunk;  /* SelectWeapon25 @ 0x79b20 */
    pW->planeInit.apfnWeaponClick[2] = World_PlaneSelectWeapon26Thunk;  /* SelectWeapon26 */
    pW->planeInit.apfnWeaponClick[3] = World_PlaneSelectWeapon27Thunk;  /* SelectWeapon27 */
    pW->planeInit.apfnWeaponClick[4] = World_PlaneSelectWeapon28Thunk;  /* SelectWeapon28 */
    pW->planeInit.apfnWeaponClick[5] = World_PlaneSelectWeapon29Thunk;  /* SelectWeapon29 */
    pW->planeInit.apfnWeaponClick[6] = World_PlaneSelectWeapon30Thunk;  /* SelectWeapon30 @ 0x79c74 */
    pW->planeInit.pfnCloseClick  = World_PlaneCloseThunk;               /* BasePlaneScreen_Close @ 0x7c9e8 (0x7DDCC) */
    pW->planeInit.pfnBuySellClick = World_PlaneApplyQtyThunk;           /* BuySellScreen_ApplyQtyChanges @ 0x78d90 */
    pW->planeInit.pfnLoadPads    = pW->pfnLoadPads;
    pW->planeInit.pPadUser       = pW->pLoadPadsUser;
    pW->planeInit.pPanels        = &pW->planeCraftPanels;               /* the tail call's out pair */
    pW->planeInit.pBuildEnv      = &pW->planeBuild;

    memset(&pW->planeList, 0, sizeof(pW->planeList));
    pW->planeList.pbBuilt       = &pW->bPlaneListBuilt;                 /* unk_161F24 */
    pW->planeList.nLineCount    = pW->nPlaneSoldiersUnassigned + pW->nPlaneSoldiersAboard; /* 0x161AB4 + 0x161AB8 */
    pW->planeList.apCraft       = pW->apCraft;
    pW->planeList.apSoldiers    = pW->apSoldiers;
    pW->planeList.nSoldierCount = pW->nSoldierCount;
    pW->planeList.nBaseIdx      = pW->nSelectedBase;
    pW->planeList.nCraftIdx     = pW->nHangarCraftIdx;                  /* 0x161AC4 */
    pW->planeList.pszNameBuf    = pW->szLabelScratch;
    pW->planeList.cchNameBuf    = (i32)sizeof(pW->szLabelScratch);
    pW->planeList.apfnNavClick[0] = World_PlaneListPrevThunk;           /* BuySellScreen_ListPanelPrev @ 0x79a20 */
    pW->planeList.apfnNavClick[1] = World_PlaneListNextThunk;           /* BuySellScreen_ListPanelNext @ 0x79a48 */
    pW->planeList.pfnCrewClick  = World_PlaneCrewClickThunk;            /* BasePlaneScreen_FillCrewPanel @ 0x7a188 */
    pW->planeList.pfnLoadPads   = pW->pfnLoadPads;
    pW->planeList.pPadUser      = pW->pLoadPadsUser;

    memset(&pW->planeStoreBuild, 0, sizeof(pW->planeStoreBuild));
    pW->planeStoreBuild.pbStoresCommitted = &pW->bPlaneStoresBuilt;     /* 0x161F28 */
    pW->planeStoreBuild.anSlotCarryCount  = pW->anPlaneCarryCount;      /* 0x161AD0, zeroed by the build arm */
    pW->planeStoreBuild.pnCrewQuota       = &pW->nPlaneCrewQuota;       /* 0x161EE0 */
    pW->planeStoreBuild.nCraftIdx         = pW->nHangarCraftIdx;
    pW->planeStoreBuild.nBaseIdx          = pW->nSelectedBase;
    pW->planeStoreBuild.aTopics           = pW->aTopics;                /* aResearchTopics @ 0x15EF08, the row gate */
    pW->planeStoreBuild.abSlotTopic       = g_abSlotTopicGate;          /* byte_AA968 @ 0xAA968 */
    pW->planeStoreBuild.apfnNavClick[0]   = World_PlaneStorePrevThunk;  /* BuySellScreen_DetailPanelPrev @ 0x79a70 */
    pW->planeStoreBuild.apfnNavClick[1]   = World_PlaneStoreNextThunk;  /* BuySellScreen_DetailPanelNext @ 0x79ac8 */
    pW->planeStoreBuild.pfnSlotClick      = World_PlaneSlotClickThunk;  /* Cache_ObjPlus0xBD8 @ 0x7afec */
    pW->planeStoreBuild.pfnSelRowClick    = World_PlaneAdjustQtyThunk;  /* BasePlaneScreen_AdjustSelectedQty @ 0x7a9b4 */
    pW->planeStoreBuild.pfnCommitClick    = World_PlaneCommitStoresThunk; /* BasePlaneScreen_CommitStores @ 0x7ac38 */
    pW->planeStoreBuild.pfnLoadPads       = pW->pfnLoadPads;
    pW->planeStoreBuild.pPadUser          = pW->pLoadPadsUser;

    memset(&pW->planeStores, 0, sizeof(pW->planeStores));
    pW->planeStores.pRowPanel     = pW->planeStorePanels.pSelRowPanel;   /* unk_161F00, the "<" column */
    pW->planeStores.pCommitPanel  = pW->planeStorePanels.pCommitPanel;   /* unk_161EEC, the ">" column */
    pW->planeStores.pSlotPanel    = pW->planeStorePanels.pSlotPanel;     /* unk_161AC8, the slot id column */
    pW->planeStores.pAvailPanel   = pW->planeStorePanels.pAvailPanel;    /* unk_161A40 */
    pW->planeStores.pTakenPanel   = pW->planeStorePanels.pTakenPanel;    /* unk_161EF4 */
    pW->planeStores.anSlotCarryCount = pW->anPlaneCarryCount;
    pW->planeStores.pnCrewQuota   = &pW->nPlaneCrewQuota;
    pW->planeStores.apCraft       = pW->apCraft;
    pW->planeStores.nCraftIdx     = pW->nHangarCraftIdx;
    pW->planeStores.apBases       = pW->apBases;
    pW->planeStores.nBaseIdx      = pW->nSelectedBase;
    pW->planeStores.apSoldiers    = pW->apSoldiers;
    pW->planeStores.nSoldierCount = pW->nSoldierCount;
    pW->planeStores.pbShortfallFlag = &pW->nShortfall1870C8;             /* byte_1870C8, written as a dword */
    pW->planeStores.pnResidueFlag = &pW->nResidue1870B4;                 /* dword_1870B4 */

    memset(&pW->planeSelect, 0, sizeof(pW->planeSelect));
    pW->planeSelect.pCraftPanel        = pW->planeCraftPanels.pCraftPanel;    /* 0x161EFC */
    pW->planeSelect.pStatusPanel       = pW->planeCraftPanels.pStatusPanel;   /* 0x161ED0 */
    pW->planeSelect.pnSelCraftIdx      = &pW->nPlaneSelCraftIdx;              /* 0x161F04 */
    pW->planeSelect.pnSelectedCraftIdx = &pW->nHangarCraftIdx;                /* 0x161AC4 */
    pW->planeSelect.apCraft            = pW->apCraft;
    pW->planeSelect.pbShowCraftDetail  = &pW->bPlaneShowDetail;               /* 0x161F20 */

    memset(&pW->planeCrew, 0, sizeof(pW->planeCrew));
    pW->planeCrew.pCrewPanel    = pW->planeListPanels.pCrewPanel;        /* 0x161EDC */
    pW->planeCrew.pStatusPanel  = pW->planeListPanels.pCrewStatusPanel;  /* 0x161A3C */
    pW->planeCrew.apCraft       = pW->apCraft;
    pW->planeCrew.nCraftIdx     = pW->nHangarCraftIdx;
    pW->planeCrew.apSoldiers    = pW->apSoldiers;
    pW->planeCrew.nSoldierCount = pW->nSoldierCount;
    pW->planeCrew.pszNameBuf    = pW->szLabelScratch;
    pW->planeCrew.cchNameBuf    = (i32)sizeof(pW->szLabelScratch);

    memset(&pW->planeWeapon, 0, sizeof(pW->planeWeapon));
    pW->planeWeapon.apCraft    = pW->apCraft;
    pW->planeWeapon.apBases    = pW->apBases;
    pW->planeWeapon.nCraftIdx  = pW->nHangarCraftIdx;
    pW->planeWeapon.nBaseIdx   = pW->nSelectedBase;

    memset(&pW->planeClose, 0, sizeof(pW->planeClose));
    pW->planeClose.apCraft         = pW->apCraft;
    pW->planeClose.apBases         = pW->apBases;
    pW->planeClose.nCraftIdx       = pW->nHangarCraftIdx;
    pW->planeClose.nBaseIdx        = pW->nSelectedBase;
    pW->planeClose.bStoresCommitted = pW->bPlaneStoresBuilt;             /* byte_161F28, read as a value */
    pW->planeClose.anSlotCount     = pW->anPlaneCarryCount;              /* unk_161AD0, the 31..35 rows */
    pW->planeClose.pnCrewQuota     = &pW->nPlaneCrewQuota;
    pW->planeClose.pfnCloseUi      = World_CloseGeoscapeHook;            /* Geoscape_CloseUI @ 0x8b94c */
    pW->planeClose.pUser           = pW;

    memset(&pW->planeApply, 0, sizeof(pW->planeApply));
    pW->planeApply.anSlotCarryCount = pW->anPlaneCarryCount;             /* 0x161AD0, the staged change */
    pW->planeApply.apBases         = pW->apBases;
    pW->planeApply.nBaseIdx        = pW->nSelectedBase;
    pW->planeApply.apCraft         = pW->apCraft;
    pW->planeApply.nCraftIdx       = pW->nHangarCraftIdx;
    pW->planeApply.nCraftCount     = (i32)pW->bCraftCount;
    pW->planeApply.pfnCloseUi      = World_CloseGeoscapeHook;            /* the tail call at 0x78e70 */
    pW->planeApply.pUser           = pW;

    memset(&pW->planeStatEnv, 0, sizeof(pW->planeStatEnv));
    pW->planeStatEnv.nBaseIdx       = pW->nSelectedBase;
    pW->planeStatEnv.pnCraftCannon  = &pW->anPlaneStats[0];              /* 0x161ED8, weapon 25 */
    pW->planeStatEnv.pnStingray     = &pW->anPlaneStats[1];              /* 0x161EE8, 26 */
    pW->planeStatEnv.pnAvalanche    = &pW->anPlaneStats[2];              /* 0x161EF8, 27 */
    pW->planeStatEnv.pnLaserCannon  = &pW->anPlaneStats[3];              /* 0x161EF0, 28 */
    pW->planeStatEnv.pnPlasmaCannon = &pW->anPlaneStats[4];              /* 0x161ABC, 29 */
    pW->planeStatEnv.pnFusionBall   = &pW->anPlaneStats[5];              /* 0x161ED4, 30 */
    pW->planeStatEnv.pnRowCount     = &pW->nPlaneStatRows;               /* 0x161A38 */

    memset(&pF->planeDetail, 0, sizeof(pF->planeDetail));
    pF->planeDetail.pColor            = pColor;
    pF->planeDetail.pScreen           = &pW->planeBlock;
    pF->planeDetail.apCraft           = pW->apCraft;
    pF->planeDetail.nCraftCount       = (i32)pW->bCraftCount;
    pF->planeDetail.pnCraftIdx        = &pW->nHangarCraftIdx;            /* 0x161AC4, the index THIS callee reads */
    pF->planeDetail.pnBaseIdx         = &pW->nSelectedBase;
    pF->planeDetail.pbListPanelsBuilt = &pW->bPlaneListBuilt;
    pF->planeDetail.pbStoresCommitted = &pW->bPlaneStoresBuilt;
    pF->planeDetail.pdwActionFail     = (const u32 *)&pW->nShortfall1870C8;  /* dwActionFail @ 0x1870C8 */
    pF->planeDetail.pnRefuseReason    = &pW->nResidue1870B4;                 /* dword_1870B4 */
    pF->planeDetail.pbPanelFlagScratch = &pW->bPlaneRowFlag;                /* 0x161F0C, consumed at 0x7BE50 */
    pF->planeDetail.pCrewPanel        = pW->planeListPanels.pCrewPanel;
    pF->planeDetail.pCrewStatusPanel  = pW->planeListPanels.pCrewStatusPanel;
    pF->planeDetail.pSlotPanel        = pW->planeStorePanels.pSlotPanel;
    pF->planeDetail.pAvailPanel       = pW->planeStorePanels.pAvailPanel;
    pF->planeDetail.pSelRowPanel      = pW->planeStorePanels.pSelRowPanel;
    pF->planeDetail.pCommitPanel      = pW->planeStorePanels.pCommitPanel;
    pF->planeDetail.pTakenPanel       = pW->planeStorePanels.pTakenPanel;
    pF->planeDetail.pnRowCursor       = &pW->planeScratch.nCache186BE8;  /* dword_186BE8 */
    pF->planeDetail.pbPreviewOk       = &pW->bRoot35;                    /* bBaseFacPreviewOk @ 0x18652D */
    pF->planeDetail.pnCursorY         = &pF->g.nCursorY;                 /* 0x186BD4 */
    pF->planeDetail.pCtlConfirm       = pUi->apCtl[GEOUI_CTL_186BA4];     /* the OK control, recentred at 0x7C058 */
    pF->planeDetail.pAnim             = &pF->anim;                       /* the refused-transfer panel's pair */
    pF->planeDetail.apSoldiers        = pW->apSoldiers;
    pF->planeDetail.nSoldierCount     = pW->nSoldierCount;
    pF->planeDetail.pnSoldiersAboard  = &pW->nPlaneSoldiersAboard;       /* 0x161AB8 */
    pF->planeDetail.pnSoldiersAtBase  = &pW->nPlaneSoldiersAtBase;       /* 0x161EE4 */
    pF->planeDetail.pnSoldiersUnassigned = &pW->nPlaneSoldiersUnassigned; /* 0x161AB4 */
    pF->planeDetail.pnCrewCarried     = &pW->nPlaneCrewCarried;          /* 0x161AC0 */
    pF->planeDetail.pStatPanels       = &pW->planeStatPanels;
    pF->planeDetail.pnStatRowCount    = &pW->nPlaneStatRows;
    pF->planeDetail.pStatEnv          = &pW->planeStatEnv;               /* the reset branch's callee (0x7C9DC) */
    if (pHit != NULL) {
        pF->planeDetail.pfnCtlBlit   = pHit->pfnBlit;
        pF->planeDetail.pfnCtlBlitEx = pHit->pfnBlitEx;
        pF->planeDetail.pCtlUser     = pHit->pUser;
    }

    memset(&pF->planeDraw, 0, sizeof(pF->planeDraw));
    pF->planeDraw.pColor            = pColor;
    pF->planeDraw.pbShowCraftDetail = &pW->bPlaneShowDetail;             /* gate 1, 0x7D37C */
    pF->planeDraw.pbPanelsBuilt     = &pW->bPlaneBuilt;                  /* gate 2, 0x7D398 */
    pF->planeDraw.pbPreviewActive   = &pW->bRoot34;                      /* gate 3, 0x7D3D0 */
    pF->planeDraw.pnCursorY         = &pF->g.nCursorY;                   /* 0x186BD4, read LDRSH */
    pF->planeDraw.pCraftPanel       = pW->planeCraftPanels.pCraftPanel;
    pF->planeDraw.pStatusPanel      = pW->planeCraftPanels.pStatusPanel;  /* the band gate's panel */
    pF->planeDraw.apCraft           = pW->apCraft;
    pF->planeDraw.nCraftCount       = (i32)pW->bCraftCount;
    pF->planeDraw.pnSelCraftIdx     = &pW->nPlaneSelCraftIdx;             /* 0x161F04, not 0x161AC4 */
    pF->planeDraw.apSoldiers        = pW->apSoldiers;
    pF->planeDraw.nSoldierCount     = pW->nSoldierCount;
    pF->planeDraw.pDetail           = &pF->planeDetail;

    memset(&pF->planeCtl, 0, sizeof(pF->planeCtl));
    if (pHit != NULL) {
        pF->planeCtl.pfnCtlBlit   = pHit->pfnBlit;
        pF->planeCtl.pfnCtlBlitEx = pHit->pfnBlitEx;
        pF->planeCtl.pCtlBlitUser = pHit->pUser;
    }

    memset(&pF->planeArm, 0, sizeof(pF->planeArm));
    pF->planeArm.ppBackdropPad     = &pW->planeBlock.pBackdropPad;        /* 0x930A0, and the block base */
    pF->planeArm.apCtl             = pW->apPlaneCtl;
    pF->planeArm.pbStoresCommitted = &pW->bPlaneStoresBuilt;
    pF->planeArm.pbListPanelsBuilt = &pW->bPlaneListBuilt;
    pF->planeArm.pbShowCraftDetail = &pW->bPlaneShowDetail;
    pF->planeArm.pbStatPanelAOpen  = &pW->planeStatPanels.bPanelAOpen;    /* 0x161F18 */
    pF->planeArm.pbStatPanelBOpen  = &pW->planeStatPanels.bPanelBOpen;    /* 0x161F1C */
    pF->planeArm.pbPreviewOk       = &pW->bRoot35;
    pF->planeArm.ppSelRowPanel     = &pW->planeStorePanels.pSelRowPanel;
    pF->planeArm.ppCommitRowPanel  = &pW->planeStorePanels.pCommitPanel;
    pF->planeArm.pnCursorX         = &pW->nPlaneCursorX;
    pF->planeArm.pnCursorY         = &pW->nPlaneCursorY;
    pF->planeArm.pnSelCraftIdx     = &pW->nHangarCraftIdx;                /* 0x931D8 indexes apCraft with it */
    pF->planeArm.pnSelBaseIdx      = &pW->nSelectedBase;                  /* read SIGNED (0x93590) */
    pF->planeArm.apCraft           = pW->apCraft;
    pF->planeArm.nCraftCount       = (i32)pW->bCraftCount;
    pF->planeArm.pnRowCount        = &pW->nPlaneStatRows;
    pF->planeArm.pnStats           = pW->anPlaneStats;
    pF->planeArm.pColor            = pColor;
    pF->planeArm.pPlaneEnv         = &pF->planeDraw;                      /* the call at 0x930F8 */
    pF->planeArm.pCtlEnv           = &pF->planeCtl;

    pF->draw.pPlaneArm = &pF->planeArm;                                    /* 0x8C1C8, case 6's arm slot */

    /* The press seam for the same screen: Geoscape_OnPointer reads the block's control run, the six
     * panel pointers and the eight gate bytes off GeoscapePointerEnv (0x88210..0x88748), so the hangar's
     * row clicks -- the craft list, the crew column, the slot column, the two quantity rows -- only
     * reach their handlers through these fields. Re-read every frame, exactly like the arm's copy. */
    pPtr->apPlaneCtl             = pW->apPlaneCtl;                          /* 0x88210 walks block +0x30..+0x68 */
    pPtr->pbPlanePanelsBuilt     = &pW->bPlaneBuilt;                        /* 0x161F08, gates the panel hits */
    pPtr->pbPlaneShowCraftDetail = &pW->bPlaneShowDetail;                   /* 0x161F20 */
    pPtr->pbPlaneStoresCommitted = &pW->bPlaneStoresBuilt;                  /* 0x161F28 */
    pPtr->pbPlaneListBuilt       = &pW->bPlaneListBuilt;                    /* 0x161F24 */
    pPtr->pbStatPanelAOpen       = &pW->planeStatPanels.bPanelAOpen;        /* 0x161F18 */
    pPtr->pbStatPanelBOpen       = &pW->planeStatPanels.bPanelBOpen;        /* 0x161F1C */
    pPtr->pPlaneCraftPanel       = pW->planeCraftPanels.pCraftPanel;       /* 0x161EFC, hit at 0x88234 */
    pPtr->pPlaneCraftStatusPanel = pW->planeCraftPanels.pStatusPanel;      /* 0x161ED0 */
    pPtr->pPlaneSelRowPanel      = pW->planeStorePanels.pSelRowPanel;      /* 0x161F00, the "<" column */
    pPtr->pPlaneCommitRowPanel   = pW->planeStorePanels.pCommitPanel;      /* 0x161EEC, the ">" column */
    pPtr->pPlaneSlotPanel        = pW->planeStorePanels.pSlotPanel;        /* 0x161AC8, the name column */
    pPtr->pPlaneCrewPanel        = pW->planeListPanels.pCrewPanel;         /* 0x161EDC, the crew column */
    for (i = 0; i < PUFO_GEO_PLANE_STAT_COUNT; ++i) {
        pPtr->apanStatGate[i] = &pW->anPlaneStats[i];                       /* 0x161ED8, 0x161EE8, 0x161EF8,
                                                                             * 0x161EF0, 0x161ABC, 0x161ED4,
                                                                             * the six gates at 0x88674..0x88748 */
    }

    if (pAssets != NULL) {
        pW->planeBuild.pTextRecs          = pAssets->pTextRecs;            /* Ship_FormatDisplayName's names */
        pW->planeList.pTextRecs           = pAssets->pTextRecs;            /* the crew status column */
        pW->planeCrew.pTextRecs           = pAssets->pTextRecs;
        pW->planeStoreBuild.apObjects     = pAssets->apObjects;            /* the row scan's two columns */
        pW->planeStoreBuild.nObjectCount  = (i32)pAssets->nObjectCount;
        pW->planeStores.apObjects         = pAssets->apObjects;
        pW->planeStores.nObjectCount      = (i32)pAssets->nObjectCount;
        pW->planeWeapon.apObjects         = pAssets->apObjects;
        pW->planeWeapon.nObjectCount      = (i32)pAssets->nObjectCount;
        pW->planeClose.apObjects          = pAssets->apObjects;   /* PlaneCloseEnv carries no count: its walk is slots 31..35 */
        pW->planeApply.apObjects          = pAssets->apObjects;
        pW->planeApply.nObjectCount       = (i32)pAssets->nObjectCount;
        pW->planeStatEnv.apObjects        = pAssets->apObjects;
        pW->planeStatEnv.nObjectCount     = (i32)pAssets->nObjectCount;
        pF->planeDraw.apObjects           = pAssets->apObjects;
        pF->planeDraw.nObjectCount        = (i32)pAssets->nObjectCount;
        pF->planeDraw.pTextRecs           = (const char *)pAssets->pTextRecs;
        pF->planeDetail.apObjects         = pAssets->apObjects;
        pF->planeDetail.nObjectCount      = (i32)pAssets->nObjectCount;
        pF->planeDetail.pTextRecs         = (char *)pAssets->pTextRecs;    /* the stat labels write NULs into it */
        pF->planeArm.apObjects            = pAssets->apObjects;
        pF->planeArm.nObjectCount         = (i32)pAssets->nObjectCount;
        pF->planeArm.pTextRecs            = (const char *)pAssets->pTextRecs;
    }

    pW->planeOpenHooks.pfnPlaySample = World_PlaySampleWithArg;            /* Sound_Play(17, 4) at 0x6180C */
    pW->planeOpenHooks.pfnInit       = World_PlaneInitThunk;              /* the BL at 0x61830 */
    pW->planeOpenHooks.pSlot         = NULL;
    pW->planeOpenHooks.pUser         = pW;

    /* ---- case 8, the research screen -------------------------------------------------------------
     *
     * The original keeps this screen's own objects inside its topic table: the "Research" pad at the block
     * base (Init's operator new at 0x749B4, stored at 0x749D8) and the four controls at block + 0x1D48
     * (0x74A28), i.e. array indices 1874..1877. The shell owns the same six objects and Init fills the block
     * view; BuildPanels' teardown empties the pad slot and the three panel cells. */
    for (i = 0; i < 4; ++i) {
        pW->apResearchCtl[i]     = &pW->aResearchCtl[i];
        pW->researchInit.apCtl[i] = &pW->aResearchCtl[i];   /* Init stores them into ppRoot[1..4], 0x74A28 */
        pW->researchBuild.apCtl[i] = &pW->aResearchCtl[i];  /* the teardown frees their pads, 0x73A10 */
    }

    pW->researchInit.ppRoot             = pW->apResearchRoot;             /* the block at 0x15EF08 */
    pW->researchInit.pCloseCtl          = pUi->apCtl[GEOUI_CTL_186B00];   /* *g_pUiCloseCtl, (224,1) at 0x74AA0 */
    pW->researchInit.pCancelCtl         = pUi->apCtl[GEOUI_CTL_186BB4];   /* *pGeoscapeCtlA, (165,188) at 0x74B98 */
    pW->researchInit.pStartCtl          = pUi->apCtl[GEOUI_CTL_186BB0];   /* *pGeoscapeCtlB, (165,166) at 0x74BAC */
    pW->researchInit.pfnCloseClick      = World_ResearchCloseThunk;       /* 0x74AB8 */
    pW->researchInit.pfnCancelClick     = World_ResearchCancelThunk;      /* 0x74AC4 */
    pW->researchInit.pfnStartClick      = World_ResearchStartThunk;       /* 0x74AD0 */
    pW->researchInit.pfnPanelPrev       = World_ResearchPanelPrevThunk;   /* 0x74A4C */
    pW->researchInit.pfnPanelNext       = World_ResearchPanelNextThunk;   /* 0x74A64 */
    pW->researchInit.pfnIncQty          = World_ResearchIncQtyThunk;      /* 0x74A7C */
    pW->researchInit.pfnDecQty          = World_ResearchDecQtyThunk;      /* 0x74A94 */
    pW->researchInit.pfnRebuildTopicList = World_ResearchRebuildThunk;    /* the call at 0x74B00 */
    pW->researchInit.pfnBuildPanels     = World_ResearchBuildThunk;       /* the call at 0x74BB8 */
    pW->researchInit.aTopics            = pW->aTopics;                    /* aResearchTopics @ 0x15EF08 */
    pW->researchInit.pbEntryCount       = (const u8 *)&pW->nResearchEntries; /* 0x160C4C, read with LDRB */
    pW->researchInit.apResearchCols     = pW->auResearchCol;              /* auResearchCol @ 0x16022C */
    pW->researchInit.pnSelectedBaseIdx  = &pW->nSelectedBase;             /* 0x159D81, read SIGNED */
    pW->researchInit.pfnLoadPads        = pW->pfnLoadPads;
    pW->researchInit.pPadUser           = pW->pLoadPadsUser;

    pW->researchBuild.pbPanelsBuilt     = &pW->bResearchBuilt;            /* 0x160C7C, the build switch */
    pW->researchBuild.ppRoot            = pW->apResearchRoot;
    pW->researchBuild.ppPanelTopic      = &pW->pResearchTopicPanel;       /* 0x160C68 */
    pW->researchBuild.ppPanelB          = &pW->pResearchQtyPanel;         /* 0x15EEFC */
    pW->researchBuild.ppPanelC          = &pW->pResearchProgPanel;        /* 0x15EF00 */
    pW->researchBuild.aTopics           = pW->aTopics;
    pW->researchBuild.pbEntryCount      = (const u8 *)&pW->nResearchEntries;
    pW->researchBuild.apResearchCols    = pW->auResearchCol;
    pW->researchBuild.pnSelectedBaseIdx = &pW->nSelectedBase;
    pW->researchBuild.pfnPanelClick     = World_ResearchTopicClickThunk;  /* 0x7348C */

    pW->researchQty.pnTopicRow          = &pW->researchScratch.nCache160C74; /* nResearchTopicRowCache */
    pW->researchQty.apResearchCols      = pW->auResearchCol;
    pW->researchQty.puResearchEntries   = &pW->nResearchEntries;
    pW->researchQty.abFacilityGrid      = pW->abFacilityGrid;             /* the lab-capacity pass */
    pW->researchQty.pnSelectedBaseIdx   = &pW->nSelectedBase;

    pW->researchStart.qty               = pW->researchQty;
    pW->researchStart.aTopics           = pW->aTopics;
    pW->researchStart.apManufCols       = pW->auManufCol;                 /* auManufCol @ 0x15E19C */
    pW->researchStart.pbTopicSelected   = &pW->researchScratch.bFlag160C80; /* unk_160C80, cleared at 0x7324C */
    pW->researchStart.pfnPlaySample     = World_ResearchPlaySample;       /* sample 17 at 0x73258 */
    pW->researchStart.pSampleUser       = pW;
    pW->researchStart.pfnRebuild        = World_HookRebuildResearchTopics; /* the tail call at 0x73320 */
    pW->researchStart.pRebuildUser      = pW;

    pW->researchCancel.qty              = pW->researchQty;
    pW->researchCancel.pClose           = &pW->closeEnv;                  /* the tail call at 0x733DC */

    World_FillScreenOpenEnv(pW, &pW->researchOpen);   /* the prologue's cells, rebuilt live (Part 73) */
    pW->researchOpen.init.pfnInit = World_ResearchInitThunk;              /* 0x61778 B ResearchScreen_Init */
    pW->researchOpen.init.pSlot   = NULL;
    pW->researchOpen.init.pUser   = pW;

    memset(&pF->researchCtl, 0, sizeof(pF->researchCtl));
    if (pHit != NULL) {
        pF->researchCtl.apCtl        = pW->apResearchCtl;      /* the arm blits [0..1], CMP #2 at 0x9414C */
        pF->researchCtl.pfnCtlBlit   = pHit->pfnBlit;
        pF->researchCtl.pfnCtlBlitEx = pHit->pfnBlitEx;
        pF->researchCtl.pCtlBlitUser = pHit->pUser;
    }

    memset(&pF->researchDraw, 0, sizeof(pF->researchDraw));
    pF->researchDraw.pPad             = (const PadImage *)pW->apResearchRoot[0]; /* blitted at 0x73A40 */
    pF->researchDraw.pColor           = pColor;
    pF->researchDraw.pAnim            = &pF->anim;
    pF->researchDraw.pbPanelsBuilt    = &pW->bResearchBuilt;
    pF->researchDraw.pbTopicSelected  = &pW->researchScratch.bFlag160C80;
    pF->researchDraw.pnTopicRow       = &pW->researchScratch.nCache160C74;
    pF->researchDraw.pnSelectedBaseIdx = &pW->nSelectedBase;
    pF->researchDraw.apSiteRecs       = pW->aSiteRecs;                    /* 0x186C40, the base name */
    pF->researchDraw.apResearchCols   = pW->auResearchCol;
    pF->researchDraw.pbEntryCount     = (const u8 *)&pW->nResearchEntries;
    pF->researchDraw.abFacilityGrid   = pW->abFacilityGrid;               /* Base_CalcLabCap's grid */
    pF->researchDraw.apCtlQty[0]      = &pW->aResearchCtl[2];             /* 0x160C58, the inc control */
    pF->researchDraw.apCtlQty[1]      = &pW->aResearchCtl[3];             /* 0x160C5C, the dec control */
    pF->researchDraw.pCtlCancel       = pUi->apCtl[GEOUI_CTL_186BB4];
    pF->researchDraw.pCtlStart        = pUi->apCtl[GEOUI_CTL_186BB0];
    pF->researchDraw.pCtlAllocAll     = pUi->apCtl[GEOUI_CTL_186B94];     /* re-pointed at (100,164) each frame */
    pF->researchDraw.pCtlClose        = pUi->apCtl[GEOUI_CTL_186B00];     /* blitted only with no row selected */
    pF->researchDraw.pfnAllocAllClick = World_ResearchAllocAllThunk;      /* 0x74188 */
    if (pHit != NULL) {
        pF->researchDraw.pfnCtlBlit   = pHit->pfnBlit;
        pF->researchDraw.pfnCtlBlitEx = pHit->pfnBlitEx;
        pF->researchDraw.pCtlUser     = pHit->pUser;
    }

    pF->draw.pResearchDraw = &pF->researchDraw;                            /* 0x9411C */
    pF->draw.pResearchCtl  = &pF->researchCtl;                             /* 0x94120..0x94150 */

    /* The press seam. Case 8 derefs bUiRowSelected and the topic panel with NO test (0x88B18), so these
     * three fields are what makes the screen pressable at all: with no row selected it hit-tests the scroll
     * pair (0..1), with a row selected the quantity pair (2..3) plus the three shared bottom controls. */
    pPtr->pbRowSelected   = &pW->researchScratch.bFlag160C80;
    pPtr->apResearchCtl   = pW->apResearchCtl;

    if (pAssets != NULL) {
        pW->researchBuild.pTextRecs        = (const char *)pAssets->pTextRecs; /* the row labels, TEXTS[topic + 4] */
        pW->researchQty.pScientistRec      = (PUFO_ROW_SCIENTIST < (i32)pAssets->nObjectCount)
                                                 ? pAssets->apObjects[PUFO_ROW_SCIENTIST] : NULL;
        pF->researchDraw.pScientistRec     = pW->researchQty.pScientistRec;
        pF->researchDraw.pTextRecs         = (const char *)pAssets->pTextRecs;
    }

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
    i32                 i;

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

    /* Case 18, the options screen. The flat array is the original's one aOptionsListCtl @ 0x159E20: slots
     * 0..6 (the seven menu rows) and 9..20 (the twelve radio buttons) name objects the shell owns, so
     * they never move, but slots 7 and 8 exist only while OptionsScreen_Open holds its pair, and the four
     * panels likewise -- Open allocates them and its close half (0x6DC44..0x6DCC8) frees them. The
     * by-value inputs the original re-reads at the call -- the screen id and the overlay origin (0x6C368),
     * the anim phase the menu gate 0x6C640 sits under, the strateg sprite base (0x6C348), and the soldier
     * count the craft loop walks -- follow the frame here. */
    for (i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {
        pW->options.apCtlFlat[i] = pW->options.ctl.apList[i];
    }
    for (i = 0; i < 2; ++i) {
        pW->options.apCtlFlat[7 + i] = pW->options.apPairCtl[i];
    }
    for (i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        pW->options.apCtlFlat[9 + i]  = pW->options.ctl.apMusic[i]; /* aOptionsMusicBtn @ 0x159E44 */
        pW->options.apCtlFlat[15 + i] = pW->options.ctl.apSfx[i];   /* aOptionsSfxBtn @ 0x159E5C */
    }
    pW->options.draw.nScreenId       = pW->screen.nGeoscapeScreen;
    pW->options.draw.nOverlayOriginX = pF->g.nOverlayOriginX;
    pW->options.draw.nMapBitsBase    = (pW->pFont != NULL) ? pW->pFont->nMapBitsBase : 0;
    pW->options.draw.nAnimPhase      = (pW->pnAnimPhase != NULL) ? (u16)*pW->pnAnimPhase : 0;
    pW->options.draw.pMapPad         = pSet->pGrid;    /* *pSmapPadCur @ 0x186E40 */
    pW->options.draw.pLayerPad       = pSet->pOverlay; /* *pSmapLayerPad @ 0x186BE0 */
    pW->options.draw.pPanelShips     = pW->options.apPanel[0]; /* 0x159E18, the panel whose nFont the rules
                                                                * use is [1] @ 0x159E78 */
    pW->options.draw.pPanelMidA      = pW->options.apPanel[1];
    pW->options.draw.pPanelMidB      = pW->options.apPanel[2];
    pW->options.draw.pPanelRight     = pW->options.apPanel[3];
    pW->options.open.nSoldierCount   = pW->nSoldierCount;        /* the cargo column's crew count */

    /* Case 6, the hangar. Its three panel sets are allocated and freed by their own builders, and the
     * consumers read the globals they live in -- 0x161EFC/0x161ED0 for the craft pair, 0x161EDC/0x161A3C
     * for the crew pair, 0x161AC8/0x161A40/0x161F00/0x161EEC/0x161EF4 for the five store columns -- at
     * every call. The block's ten control words (+0x30..+0x68) are read the same way (0x88210, 0x93130),
     * and so are the by-value inputs the handlers pass on: nSelectedCraftIdx @ 0x161AC4, the two census
     * cells the crew builder adds (0x161AB4 + 0x161AB8), the craft count byte and bStoresCommitted's
     * value. So all of it follows the frame here, not only at World_WireGeoscapeFrame. */
    for (i = 0; i < PUFO_PLANE_MAIN_CTLS; ++i) {
        pW->apPlaneCtl[PUFO_PLANE_ARM_MAIN_FIRST + i] = pW->planeBlock.apMainCtl[i];
    }
    for (i = 0; i < PUFO_PLANE_WEAPON_CTLS; ++i) {
        pW->apPlaneCtl[PUFO_PLANE_ARM_WEAPON_FIRST + i] = pW->planeBlock.apWeaponCtl[i];
    }
    for (i = 0; i < PUFO_PLANE_LIST_NAV; ++i) {
        pW->apPlaneCtl[PUFO_PLANE_ARM_LIST_FIRST + i] = pW->planeListPanels.apNav[i];
    }
    for (i = 0; i < PUFO_PLANE_STORE_NAV; ++i) {
        pW->apPlaneCtl[PUFO_PLANE_ARM_STORE_FIRST + i] = pW->planeStorePanels.apNavCtl[i];
    }
    pF->planeDraw.pCraftPanel       = pW->planeCraftPanels.pCraftPanel;
    pF->planeDraw.pStatusPanel      = pW->planeCraftPanels.pStatusPanel;
    pF->planeDraw.nCraftCount       = (i32)pW->bCraftCount;
    pW->planeStores.pRowPanel       = pW->planeStorePanels.pSelRowPanel;
    pW->planeStores.pCommitPanel    = pW->planeStorePanels.pCommitPanel;
    pW->planeStores.pSlotPanel      = pW->planeStorePanels.pSlotPanel;
    pW->planeStores.pAvailPanel     = pW->planeStorePanels.pAvailPanel;
    pW->planeStores.pTakenPanel     = pW->planeStorePanels.pTakenPanel;
    pW->planeSelect.pCraftPanel     = pW->planeCraftPanels.pCraftPanel;
    pW->planeSelect.pStatusPanel    = pW->planeCraftPanels.pStatusPanel;
    pW->planeCrew.pCrewPanel        = pW->planeListPanels.pCrewPanel;
    pW->planeCrew.pStatusPanel      = pW->planeListPanels.pCrewStatusPanel;
    pW->planeList.nLineCount        = pW->nPlaneSoldiersUnassigned + pW->nPlaneSoldiersAboard; /* 0x161AB4 + 0x161AB8 */
    pW->planeStoreBuild.nCraftIdx   = pW->nHangarCraftIdx;   /* 0x161AC4, re-read by every handler */
    pW->planeStores.nCraftIdx       = pW->nHangarCraftIdx;
    pW->planeCrew.nCraftIdx         = pW->nHangarCraftIdx;
    pW->planeWeapon.nCraftIdx       = pW->nHangarCraftIdx;
    pW->planeClose.nCraftIdx        = pW->nHangarCraftIdx;
    pW->planeApply.nCraftIdx        = pW->nHangarCraftIdx;
    pW->planeApply.nCraftCount      = (i32)pW->bCraftCount;
    pW->planeClose.bStoresCommitted = pW->bPlaneStoresBuilt;  /* 0x161F28, read as a value */
    pF->planeArm.nCraftCount        = (i32)pW->bCraftCount;
    pF->planeArm.pnCursorX          = &pW->nPlaneCursorX;
    pF->planeArm.pnCursorY          = &pW->nPlaneCursorY;
    pW->nPlaneCursorX               = (i16)pF->g.nCursorX;    /* 0x186BD0 / 0x186BD4, read LDRSH (0x930D0) */
    pW->nPlaneCursorY               = (i16)pF->g.nCursorY;
    /* The detail draw's own seven panel reads, 0x7BD00..0x7BF5C: with a stale copy each arm still paints
     * its rails, its headers and its OK control and silently skips every column it was asked to draw. */
    pF->planeDetail.pCrewPanel        = pW->planeListPanels.pCrewPanel;
    pF->planeDetail.pCrewStatusPanel  = pW->planeListPanels.pCrewStatusPanel;
    pF->planeDetail.pSlotPanel        = pW->planeStorePanels.pSlotPanel;
    pF->planeDetail.pAvailPanel       = pW->planeStorePanels.pAvailPanel;
    pF->planeDetail.pSelRowPanel      = pW->planeStorePanels.pSelRowPanel;
    pF->planeDetail.pCommitPanel      = pW->planeStorePanels.pCommitPanel;
    pF->planeDetail.pTakenPanel       = pW->planeStorePanels.pTakenPanel;
    pW->ptrEnv.pPlaneCraftPanel       = pW->planeCraftPanels.pCraftPanel;
    pW->ptrEnv.pPlaneCraftStatusPanel = pW->planeCraftPanels.pStatusPanel;
    pW->ptrEnv.pPlaneSelRowPanel      = pW->planeStorePanels.pSelRowPanel;
    pW->ptrEnv.pPlaneCommitRowPanel   = pW->planeStorePanels.pCommitPanel;
    pW->ptrEnv.pPlaneSlotPanel        = pW->planeStorePanels.pSlotPanel;
    pW->ptrEnv.pPlaneCrewPanel        = pW->planeListPanels.pCrewPanel;

    /* Case 8, the research screen. BuildPanels replaces the pad and the three panels behind the envs' backs
     * (Init builds them, the close releases them), so the draw env, the three handler envs and the press arm
     * all re-read the shell's cells here -- the same rule that fixed the hangar's columns. */
    pF->researchDraw.pPad             = (const PadImage *)pW->apResearchRoot[0]; /* 0x73A40's blit */
    pF->researchDraw.pPanelTopic      = pW->pResearchTopicPanel;
    pF->researchDraw.pPanelQty        = pW->pResearchQtyPanel;
    pF->researchDraw.pPanelProgress   = pW->pResearchProgPanel;
    pW->researchInit.pPanelTopic      = pW->pResearchTopicPanel;
    pW->researchInit.pPanelQty        = pW->pResearchQtyPanel;
    pW->researchInit.pPanelProgress   = pW->pResearchProgPanel;
    pW->researchQty.pTopicPanel       = pW->pResearchTopicPanel;
    pW->researchStart.qty             = pW->researchQty;
    pW->researchStart.pPanelQty       = pW->pResearchQtyPanel;
    pW->researchStart.pPanelProgress  = pW->pResearchProgPanel;
    pW->researchCancel.qty            = pW->researchQty;
    pW->ptrEnv.pResearchPanel         = pW->pResearchTopicPanel;   /* derefed unguarded at 0x88B30 */
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
