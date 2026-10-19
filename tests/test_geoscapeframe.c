/* test_geoscapeframe.c - the geoscape frame wiring, end to end.
 *
 * World_NewGame (Geoscape_InitUI @ 0x857f4 mode 0 + Game_StartNew @ 0x94554) builds a real world out
 * of the shipped archives, World_WireGeoscapeFrame wires every env Geoscape_Draw @ 0x8C180 reaches,
 * and this file draws frames through the same call the shell's render timer takes (Render_Frame
 * @ 0x22310's geoscape branch) and moves the pointer through the same call its WM_LBUTTONDOWN takes
 * (Input_OnPointer @ 0x13c6c's geoscape seam).
 *
 * What is pinned, in the order the frame runs it:
 *   - the new game lands in geoscape mode on screen 2 (NewBaseScreen_Open @ 0x61620) at zoom 0;
 *   - the map arm's half-extents (0x8C2B8..0x8C2EC), its four pad origins (0x8C498..0x8C548), the
 *     overlay drift (0x8C2F4..0x8C3C8) and the scroll wrap, X for every id and Y only for id 2
 *     (0x8C3D8..0x8C494);
 *   - the map pad really paints: every one of the 240 x 320 surface pixels equals palette slot 10's
 *     entry for the earth100 glyph byte the clipped blit reaches there. That is the case the render
 *     clip used to drop -- a glyph whose near edge is left of the surface and whose far edge is past
 *     its right edge, which is exactly the wrapped map copy at -280 (see Blit_ClipGlyph and
 *     BlitIndexed_Inner @ 0xA30B8 / 0xA310C);
 *   - the crosshair tail (0x8C69C..0x8C760): clear pending parks the cooldown at 0xFF, an armed pick
 *     paints the 50/200 pair at the +3 offsets with the strip pad's height as the line's bound;
 *   - the chain tail (0x91CEC..0x91DB0) runs the globe hover, whose counters the shell owns;
 *   - the frame end (0x944B8..0x94544) writes Map.anActionPhaseSave0[112] only for the saving and
 *     loading list modes, and presents exactly once;
 *   - the pointer dispatcher stores the cursor pair as halfwords (0x872F8 / 0x8731C), starts the
 *     globe drag (0x87D58), and Smap_ScrollBy @ 0x82104 moves the camera by the pen delta off the
 *     last pen position; the release clears the drag flag (0x896BC).
 */
#include "app/pufo_world.h"
#include "game/pufo_boot.h"
#include "game/pufo_clock.h"
#include "game/pufo_font.h"
#include "game/pufo_options.h"
#include "game/pufo_tactend.h"
#include "render/pufo_render.h"
#include "ui/pufo_control.h"

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

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static i32 FindStrategById(const PtrArray *pList, i32 nId)
{
    for (int i = 0; i < PtrArray_GetCount(pList); ++i) {
        const StrategItem *pItem = (const StrategItem *)PtrArray_Get(pList, i);
        if (pItem != NULL && pItem->nDisplayId == nId) {
            return i;
        }
    }
    return -1;
}

static BootAssets     g_assets;
static PufoWorldState g_world;
/* The font is the port's one fontSys global (game/pufo_font.h), which InitUI fills at 0x860D4..0x86728. */
static PufoOptions    g_options;
static GfxColorEnv    g_color;
static u32            g_nUiSlot0;   /* UiControlHitCtx.pnUiSlot0, the Map+0x60 word */
static u8             g_nMapPhase;  /* nMapPhase @ 0x1139EF (Map + 0x37): the arm's cursor colour, and the
                                     * byte the original's 0xC352 handler wraps (PocketUFO_WndProc 0xA0988) */
static UiControlHitCtx g_hit;

/* --- the tact bank bring-up fixtures (Part 54) -------------------------------------
 * The widget bank stands in for the app's g_bank (the WorldHooks field the world installs
 * handlers and pads onto), the input hooks are the ones the installed MenuBtn handler hands to
 * Input_OnConfirm @ 0x15f3c, and the slot hooks are the SaveLoad_Slot0..6 pair. Bank samples are
 * kept apart from g_nSamples so a tact click cannot disturb the frame asserts that pin it. */
static UiWidgetBank   g_bank;
static TactInputHooks g_tactInHooks;
static bool           g_bBankActionFlag;   /* UiBank_bActionFlag, WorldHooks.pbBankActionFlag */
static i32            g_aBankSamples[64];
static i32            g_nBankSamples;
static i32            g_nSlotFormatCalls;  /* Menu_OpenSlotList's injected SaveSlot_FormatList */
static i32            g_nLoadSlotCalls;    /* the load hook SaveLoad_Slot runs on a load screen */
static i32            g_nLoadSlotId;
static i32            g_nListModeCell;     /* *0x157188, the caller-owned list mode */

static void RecBankSample(u32 nSample, void *pUser)
{
    (void)pUser;
    if (g_nBankSamples < (i32)(sizeof(g_aBankSamples) / sizeof(g_aBankSamples[0]))) {
        g_aBankSamples[g_nBankSamples] = (i32)nSample;
    }
    ++g_nBankSamples;
}

static i32 RecLoadGame(i32 nSlot, void *pUser)
{
    (void)pUser;
    ++g_nLoadSlotCalls;
    g_nLoadSlotId = nSlot;
    return 1;                                   /* "loaded": the list mode stays 1 (0x19E08 arm) */
}

static const SaveLoadSlotHooks g_slotHooks = { NULL, RecLoadGame, NULL };  /* no save unit yet */

static void RecFormatSlots(void *pUser)
{
    (void)pUser;
    ++g_nSlotFormatCalls;
}

/* The shell's blit seam, recorded so the frame's control blits are checkable. */
static const PadImage *g_apBlitPad[64];
static i32            g_nBlits;
static i32            g_nSamples;
static i32            g_nPresents;

static void RecBlitPad(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    if (g_nBlits < (i32)(sizeof(g_apBlitPad) / sizeof(g_apBlitPad[0]))) {
        g_apBlitPad[g_nBlits] = pPad;
    }
    ++g_nBlits;
    PadImage_Blit(pPad);
}

static void RecBlitPadEx(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    if (g_nBlits < (i32)(sizeof(g_apBlitPad) / sizeof(g_apBlitPad[0]))) {
        g_apBlitPad[g_nBlits] = pPad;
    }
    ++g_nBlits;
    PadImage_BlitEx(pPad, 255u);
}

static i32 g_aFrameSamples[512];   /* the WorldHooks sample seam, values as well as the count,
                                    * so the tact bank clicks' samples are checkable (Part 54) */

static void RecSample(i32 nSample, void *pUser)
{
    (void)pUser;
    if (g_nSamples < (i32)(sizeof(g_aFrameSamples) / sizeof(g_aFrameSamples[0]))) {
        g_aFrameSamples[g_nSamples] = nSample;
    }
    ++g_nSamples;
}

/* GX_Present @ 0x27788's stand-in. Its value is what Geoscape_Draw and Render_Frame hand back on the
 * geoscape branch, so it is checked as a sentinel. */
static i32 PresentThunk(void *pUser)
{
    (void)pUser;
    ++g_nPresents;
    return 7;
}

static bool LoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                     const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2);
    return true;
}

static void ClearSurface(void)
{
    SoftSurf *p = g_assets.pSurface;

    memset(p->pPixels, 0, (size_t)p->nWidth * (size_t)p->nHeight * 2u);
}

static i32 SurfPix(i32 x, i32 y)
{
    const SoftSurf *p = g_assets.pSurface;

    return (i32)((const u16 *)p->pPixels)[(size_t)y * (size_t)p->nWidth + (size_t)x];
}

/* The 16-bit colour SoftSurf_BlitSrcRect_Inner @ 0xA4450 packs from an index/mid/low triple: each
 * 5-6-5 field comes from the top bits of its byte. */
static i32 PackInk(i32 nIdx, i32 nMid, i32 nLow)
{
    return (i32)(((nIdx >> 3) << 11) | ((nMid >> 2) << 5) | (nLow >> 3));
}

static i32 DrawFrame(void)
{
    g_nPresents = 0;
    g_nBlits    = 0;
    return World_DrawGeoscapeFrame(&g_world, PresentThunk, NULL);
}

/* Did the seam see this pad drawn during the blits recorded so far? */
static bool SawPadBlit(const PadImage *pPad)
{
    for (i32 i = 0; i < g_nBlits; ++i) {
        if (pPad != NULL && g_apBlitPad[i] == pPad) {
            return true;
        }
    }
    return false;
}

/* The tile the globe hover samples. Geoscape_DrawGlobeHover @ 0x67894 calls Smap_GetTileAtScreen @ 0x82244 at
 * the fixed screen point (120, nMidY) with nMidY = (320 - strip height) / 2 (0x67328, 0x6734C), so the region
 * it reports follows the view scroll, exactly as Smap_GetTileAtScreen resolves it. */
static i32 HoverSampleTile(i32 nMidY, i32 *pnTileX, i32 *pnTileY)
{
    const SmapView *pView = &g_world.view;
    const u8       *pTiles = g_world.frame.hover.pTiles;
    i32             nX = pView->nScrollX + pView->nMapW / 2;
    i32             nY = pView->nScrollY + pView->nMapH / 2 + nMidY - 160;

    if (nX < 0) { nX += pView->nMapW; }
    if (nY < 0) { nY += pView->nMapH; }
    *pnTileX = nX;
    *pnTileY = nY;
    if (pTiles == NULL || nX >= pView->nMapW || nY >= pView->nMapH) {
        return 0xFF;
    }
    return pTiles[nY * pView->nMapW + nX];
}

/* The buildable tile (0x80..0x89) nearest to (nFromX, nFromY), or -1 / -1 when the grid holds none. */
static void NearestRegionTile(i32 nFromX, i32 nFromY, i32 *pnX, i32 *pnY)
{
    const SmapView *pView = &g_world.view;
    const u8       *pTiles = g_world.frame.hover.pTiles;
    i32             nBest = 0x7FFFFFFF;
    i32             nX, nY;

    *pnX = -1;
    *pnY = -1;
    if (pTiles == NULL) {
        return;
    }
    for (nY = 0; nY < pView->nMapH; ++nY) {
        for (nX = 0; nX < pView->nMapW; ++nX) {
            const u8 nTile = pTiles[nY * pView->nMapW + nX];
            i32      nDist;
            if (nTile < PUFO_REGION_ID_FIRST || nTile >= PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT) {
                continue;
            }
            nDist = (nX - nFromX) * (nX - nFromX) + (nY - nFromY) * (nY - nFromY);
            if (nDist < nBest) {
                nBest = nDist;
                *pnX = nX;
                *pnY = nY;
            }
        }
    }
}

/* Step cUiAnimProgress @ 0x187058 the way the app's WM_TIMER branch does (0xA0C88..0xA0C90) and draw between the
 * steps, until the globe hover gets past its 0x67490 bail and reports a buildable region. The hover stops while the
 * bottom panel is still expanding, and mode 3 of Ui_AnimExpandBlit lands on progress 100 once 50 * progress reaches
 * the panel width (0x95D60) -- five frames in practice. This test runs no timer, so it steps the byte the way the
 * timer does. */
static void RunFramesUntilHoverRegion(void)
{
    for (i32 i = 0; i < 12; ++i) {
        if (g_world.cUiAnimProgress < 101) {
            ++g_world.cUiAnimProgress;
        }
        (void)DrawFrame();
        if (g_world.bPendingRegionId >= PUFO_REGION_ID_FIRST
            && g_world.bPendingRegionId < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT) {
            return;
        }
    }
}

/* Bring a buildable tile under the hover's fixed sample point with pen strokes only. A press inside the drag band
 * arms bGeoscapeDragActive @ 0x187074 (0x87D60), clears the tap cooldown (0x87D64) and records the pen into
 * g_smapCamera.nLastDx/nLastDy (0x87D74 / 0x87D7C) with no motion of its own -- the store precedes the BLEQ
 * Smap_ScrollBy at 0x87D14 and 0x82104 / 0x82114 add it straight back. The pointer events that follow (bDown clear)
 * carry the camera by the pen's own travel, and the release clears the byte (0x896BC). One stroke carries at most
 * 100 tiles so the pen never leaves the pen screen, and the short way round is taken because the map wraps in x
 * (0x82244's own rule). Returns the tile the hover samples afterwards, so the caller can tell whether the shipped
 * grid holds a buildable region at all. */
static i32 DragCameraOntoRegion(i32 nMidY)
{
    i32 nTileX, nTileY, nRegX, nRegY;
    i32 nDragX, nDragY;
    i32 nTile = HoverSampleTile(nMidY, &nTileX, &nTileY);

    if (nTile >= PUFO_REGION_ID_FIRST && nTile < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT) {
        return nTile;
    }
    NearestRegionTile(nTileX, nTileY, &nRegX, &nRegY);
    if (nRegX < 0) {
        return nTile;
    }
    for (i32 i = 0; i < 12 && (nTile < PUFO_REGION_ID_FIRST || nTile >= PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT);
         ++i) {
        nDragX = nRegX - nTileX;
        nDragY = nRegY - nTileY;
        if (nDragX >  g_world.view.nMapW / 2) { nDragX -= g_world.view.nMapW; }
        if (nDragX < -g_world.view.nMapW / 2) { nDragX += g_world.view.nMapW; }
        if (nDragX >  100) { nDragX =  100; } else if (nDragX < -100) { nDragX = -100; }
        if (nDragY >  100) { nDragY =  100; } else if (nDragY < -100) { nDragY = -100; }
        World_GeoscapePointer(&g_world, 120, nMidY, true);            /* the press arm moves nothing */
        World_GeoscapePointer(&g_world, 120 - nDragX, nMidY - nDragY, false);
        World_GeoscapePointerUp(&g_world);                            /* 0x896BC */
        nTile = HoverSampleTile(nMidY, &nTileX, &nTileY);
    }
    return nTile;
}

int main(void)
{
    const char *const apsz[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    WorldHooks  hooks;
    GeoscapeFrameState *pF;
    const SmapZoomSet *pSet;
    PadImage *pMapPad;
    i32       nHalfW, nHalfH;
    i32       nTick, nSec0, i;
    i32       nNavCnt, nPhase, nCurX, nCurY, nIdx;
    i32       nOpen0, nSamples0;
    i32       nKind, nSpan, nCellW, nCost0, nFunds0, nBase0, nSiteId0, nRegionCnt0;
    i32       nRowX, nRowY, nAimX, nAimY, nFunds1;
    i32       nMidY, nTile, nRegion, nRegionIdx, nNewCost;
    i32       nBase1, nSiteId1, nMode0, nCost1, nCnt1, nRow2Y, nRowSel, nSel0;

    if (!Boot_LoadStartup(&g_assets, apsz)) {
        printf("FAIL: Boot_LoadStartup\n");
        return 1;
    }
    /* The original arms the blit target (Render_SetTarget @ 0x9faa4) before it draws; the shell does
     * it in App_PresentLoadedSurface. Without it SoftSurf_BlitGlyph_Inner @ 0xA41D4 draws nothing, so
     * the test arms it once, as the shell does from the second frame on. */
    (void)Render_SetTarget(240, 320, 2, 2, (i32)g_assets.pSurface->nWidth * 2,
                           g_assets.pSurface->pPixels);

    g_hit.pnUiSlot0 = &g_nUiSlot0;
    g_hit.pfnBlit   = RecBlitPad;
    g_hit.pfnBlitEx = RecBlitPadEx;

    if (!World_Init(&g_world)) {
        printf("FAIL: World_Init\n");
        return 1;
    }
    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadPads   = LoadPads;
    hooks.pfnPlaySample = RecSample;
    hooks.pHit          = &g_hit;
    hooks.pColor        = &g_color;
    hooks.pnAnimPhase   = (i32 *)&g_nUiSlot0; /* Map + 0x60 @ 0x113A18, the cell the pulse's phase gate counts */
    hooks.pnMapPhase    = &g_nMapPhase;       /* Map + 0x37 @ 0x1139EF, the placement arm's cursor phase */
    /* The tact bank bring-up wiring (Part 54): the world installs InitUiControls' handlers and pads
     * on this bank, and the installed MenuBtn handler reaches back through these fields. */
    memset(&g_tactInHooks, 0, sizeof(g_tactInHooks));
    g_tactInHooks.pfnPlaySample = RecBankSample;
    hooks.pBank               = &g_bank;
    hooks.pTactInputHooks     = &g_tactInHooks;
    hooks.pbBankActionFlag    = &g_bBankActionFlag;
    hooks.pSaveLoadHooks      = &g_slotHooks;
    hooks.pfnFormatSlotList   = RecFormatSlots;
    hooks.pFormatSlotListUser = NULL;
    hooks.pnListMode          = &g_nListModeCell;
    hooks.pTactOptions        = &g_options;   /* the menu handlers write options through this alias */
    (void)World_NewGame(&g_world, &g_assets, &g_fontSys, &g_options, &hooks);

    pF   = &g_world.frame;
    pSet = &g_world.sets[0];

    /* 1. Where the new game leaves the world, and what the wiring points at. */
    CheckEqI32(g_world.bGeoscapeUi, 1, "InitUI sets Map.bGeoscapeUi (Map_bGeoscapeUi @ Map+0x44)");
    CheckEqI32(g_world.screen.nGeoscapeScreen, 2,
               "StartNew ends on geoscape screen 2 (NewBaseScreen_Open 0x61620)");
    CheckEqI32(g_world.view.nMode, 0, "InitUI publishes the view at zoom level 0 (0x868D8..0x868E8)");
    Check(pF->draw.pMapView == &pF->map && pF->draw.pStatusPanel == &pF->panel
              && pF->draw.pCrosshair == &pF->cross && pF->draw.pRadarArm == &pF->radar
              && pF->draw.pFrameEnd == &pF->frameEnd,
          "the dispatcher's four map-screen arms and the frame end are wired");
    Check(pF->map.pWorldBits == (const u8 *)g_world.ui.pWorldSize
              && pF->map.pMapPal == g_aPalette[PUFO_RADAR_MAP_PAL_SLOT]
              && pF->map.pnOverlayOriginX == &pF->g.nOverlayOriginX,
          "the map env reads the 0x186BC0 world record, aPalette slot 29 and the frame's own drift cell");
    Check(pF->panel.pFac == &pF->fac && pF->panel.pAnim == &pF->anim
              && pF->panel.pLines == &g_world.msgLines && pF->panel.pColor == &g_color
              && pF->panel.pCtlSfx == g_world.ui.apCtl[GEOUI_CTL_18704C]
              && pF->panel.pCtlOpen == g_world.ui.apCtl[GEOUI_CTL_187024]
              && pF->panel.pCloseCtl == g_world.ui.apCtl[GEOUI_CTL_186B00]
              && pF->panel.apClockDigitPads == g_world.ui.apClockDigitPads
              && pF->panel.apRootCtl == g_world.ui.apRootCtl,
          "the status panel reads the shell's own anim root, message list and control singletons");
    Check(pF->panel.pGlobeEnv == &pF->hover && pF->hover.pnScanX == &g_world.nGlobeScanX
              && pF->hover.pnPendingPickX == &g_world.nPlaceX
              && pF->hover.paRegionCost == (const u32 *)g_anNewBaseCostByRegion,
          "the chain tail's globe hover is wired to the shell's sweep counters and aNewBaseCostByRegion");
    Check(pF->cross.pbPending == &pF->g.bCrosshairPending
              && pF->cross.pnCooldownR == &pF->g.nTapCooldown
              && pF->cross.pChromePad == g_world.ui.apPad[GEOUI_PAD_186BBC],
          "the crosshair tail reads the frame's pending/cooldown pair and the 0x186BBC strip pad");

    /* 2. One whole frame at the new-game camera. */
    ClearSurface();
    g_world.geo.time.nHour   = 3;   /* the drift is a function of the clock, so the clock is fixed here */
    g_world.geo.time.nMinute = 30;
    g_world.view.nScrollX = 0;
    g_world.view.nScrollY = 0;
    pMapPad = pSet->pGrid;      /* earth100.pad, 800 x 400, palette slot 10 */
    nHalfW  = (i32)pMapPad->uWidth / 2 - 120;
    nHalfH  = (i32)pMapPad->uHeight / 2 - 160;
    CheckEqI32((i32)pMapPad->uWidth, 800, "the zoom-0 map pad is earth100 at 800 wide");
    CheckEqI32(nHalfW, 280, "nHalfW = nMapW / 2 - 120 (0x8C2B8..0x8C2D8)");
    CheckEqI32(nHalfH, 40,  "nHalfH = nMapH / 2 - 160 (0x8C2DC..0x8C2EC)");

    CheckEqI32(DrawFrame(), 7, "the frame returns the present thunk's value (0x94538)");
    CheckEqI32(g_nPresents, 1, "the frame end presents exactly once");
    Check(pF->map.pMapPad == pSet->pGrid && pF->map.pLayerPad == pSet->pOverlay,
          "the frame reads the map and layer pads out of the zoom set its view selected");
    CheckEqI32(pF->map.nScreenId, 2, "and the screen id out of 0x186BC4");
    CheckEqI32(g_world.view.nScrollX, -800,
               "the X wrap moves the camera: 0 + 280 > 0, so 0 - 800 (0x8C40C..0x8C414)");
    CheckEqI32(g_world.view.nScrollY, -400,
               "the Y wrap does too, because the id is 2 (0x8C48C..0x8C494)");
    /* Screen 2 blits four copies. The last one is the x + nMapW copy at the original y (0x8C534/0x8C53C),
     * so that is where the pad's own origin is left behind -- not the copy that hits the surface. */
    CheckEqI32(pMapPad->nOriginX, -(-800 + 800 + nHalfW),
               "the last map blit leaves the pad at -(nScrollX + nMapW + nHalfW) (0x8C534)");
    CheckEqI32(pMapPad->nOriginY, -(-400 + nHalfH),
               "and at -(nScrollY + nHalfH) (0x8C53C), the copy at (-280, 360)");
    CheckEqI32(pF->g.nOverlayOriginX, -115,
               "the drift is (int)(3 * (800 / -24) - 800*30/1440) = -115 (0x8C2F4..0x8C3C8)");
    CheckEqI32((i32)pSet->pOverlay->nOriginX, -115 + 800 - nHalfW + 800,
               "screen 2 stores the third layer origin and skips all three flips (0x8C548, 0x8C55C BNE)");
    CheckEqI32(pF->g.bCrosshairPending, 0,
               "the new game leaves bSmapCrosshairPending clear -- the radar pick is what sets it");
    CheckEqI32(pF->g.nTapCooldown, 0xFF,
               "so the crosshair tail clears the flag and parks the cooldown at 0xFF (0x8C740..0x8C754)");
    Check(g_world.nGlobeScanX == 0 && g_world.nGlobeSweepV == 0 && g_world.nGlobeSweepH == 0,
          "the hover runs but stops at its panel animation while the anim progress is 0 (0x67490 BEQ loc_67B10)");
    printf("       frame diagnostics: message lines %d, flag %d, control blits %d, "
           "scan %d sweepV %d sweepH %d, close ctl enabled %d state %d\n",
           (int)g_world.msgLines.nLineCount, (int)g_world.msgLines.abLineFlags[0],
           (int)g_nBlits, (int)g_world.nGlobeScanX, (int)g_world.nGlobeSweepV,
           (int)g_world.nGlobeSweepH,
           g_world.ui.apCtl[GEOUI_CTL_186B00] != NULL ? (int)g_world.ui.apCtl[GEOUI_CTL_186B00]->bEnabled : -1,
           g_world.ui.apCtl[GEOUI_CTL_186B00] != NULL ? (int)g_world.ui.apCtl[GEOUI_CTL_186B00]->nState : -1);
    printf("       panel gate: ctlSfx %p ctlOpen %p ctlBlit %p, placement pending %d, "
           "pending region %d, strip pad %p h %d\n",
           (void *)pF->panel.pCtlSfx, (void *)pF->panel.pCtlOpen, (void *)pF->panel.pfnCtlBlit,
           (int)g_world.bPlacementPending, (int)g_world.bPendingRegionId,
           (void *)pF->panel.pStripPad,
           pF->panel.pStripPad != NULL ? (int)pF->panel.pStripPad->uHeight : -1);

    /* 3. The map arm alone on a cleared surface. Screen 2's four copies are at (520, 360), (520, -40),
     * (-280, -40) and (-280, 360); only the third reaches the 240 x 320 surface, and its near edge is
     * 280 columns off the left. Every surface pixel must therefore be palette slot 10's entry for the
     * glyph byte at (y + 40, x + 280) -- the clipped blit the original's BlitIndexed_Inner preamble
     * (0xA310C..0xA3120, measured against the SAVED far edges) produces. */
    {
        const u8 *pGlyph = pMapPad->pData;
        const i32 nW = (i32)((u16)pGlyph[0] | ((u16)pGlyph[1] << 8));
        const i32 nH = (i32)((u16)pGlyph[2] | ((u16)pGlyph[3] << 8));
        const i32 nPaintX = -(-800 + 800 + nHalfW);  /* the third copy: (-280, -40), the only one that
                                                      * reaches the 240 x 320 surface */
        const i32 nPaintY = -(-400 + 400 + nHalfH);
        i32 nWrong = 0;
        i32 x, y;

        ClearSurface();
        g_world.view.nScrollX = 0;
        g_world.view.nScrollY = 0;
        Geoscape_BlitMapView(&pF->map);

        CheckEqI32(nW, 800, "the earth100 glyph header says 800 wide");
        CheckEqI32(nH, 400, "and 400 tall");
        CheckEqI32(pMapPad->nOriginX, nPaintX, "the copy the pad ends on shares the painting copy's x, -280 (0x8C534)");
        CheckEqI32(pMapPad->nOriginY, -(-400 + nHalfH),
                   "but its y is the 360 copy (0x8C53C); the painting copy sits at -40 (0x8C528)");
        for (y = 0; y < 320; ++y) {
            for (x = 0; x < 240; ++x) {
                const u8 nIdx = pGlyph[4 + (y - nPaintY) * nW + (x - nPaintX)];
                if (SurfPix(x, y) != (i32)g_aPalette[10][nIdx]) {
                    ++nWrong;
                }
            }
        }
        CheckEqI32(nWrong, 0, "every one of the 76800 surface pixels is slot 10's entry for its glyph byte");
        printf("       slot 10 entries 0..2: %04X %04X %04X; corner pixels %04X %04X\n",
               g_aPalette[10][0], g_aPalette[10][1], g_aPalette[10][2],
               SurfPix(0, 0), SurfPix(239, 319));
    }

    /* 4. The crosshair tail with the pick armed: the pair paints at +3 on both axes, and the vertical
     * line is 320 - stripHeight tall (0x8C6CC..0x8C73C). */
    {
        const PadImage *pStrip = g_world.ui.apPad[GEOUI_PAD_186BBC];
        const i32 nInk = PackInk(PUFO_SMAP_CROSSHAIR_INDEX, PUFO_SMAP_CROSSHAIR_MID,
                                 PUFO_SMAP_CROSSHAIR_INDEX);
        const i32 nLineH = 320 - (i32)pStrip->uHeight;

        pF->g.bCrosshairPending = 1;
        pF->g.nTapCooldown      = 0;
        pF->g.nCrosshairX       = 100;
        pF->g.nCrosshairY       = 50;
        pF->cross.nCrosshairX   = 100;   /* the frame copies the pair in World_RefreshGeoscapeFrame;
                                          * the tail on its own needs the env's own copies set */
        pF->cross.nCrosshairY   = 50;
        /* The tail on its own. In a whole frame the chain tail's globe hover draws over this region after
         * the crosshair (its 0x672D8 fill and the card from 0x672DC), so the armed ink is only readable
         * here; a new game never arms the tail at all, which section 2 asserts. */
        ClearSurface();
        Geoscape_BlitCrosshair(&pF->cross);
        CheckEqI32(SurfPix(103, 40), nInk,
                   "the vertical line paints at nSmapCrosshairX + 3 (0x8C6F4)");
        CheckEqI32(SurfPix(103, nLineH - 1), nInk, "over 320 - stripHeight rows (0x8C6E0)");
        Check(SurfPix(103, nLineH) != nInk, "and no further down");
        CheckEqI32(SurfPix(0, 53), nInk, "the horizontal line starts at x 0");
        CheckEqI32(SurfPix(239, 53), nInk, "and runs 240 wide at nSmapCrosshairY + 3 (0x8C71C)");
        printf("       crosshair: ink %04X line height %d (strip h %d), pending after %d, cooldown after %d, "
               "probes %04X %04X %04X\n",
               nInk, nLineH, (int)pStrip->uHeight, (int)pF->g.bCrosshairPending,
               (int)pF->g.nTapCooldown, SurfPix(103, 40), SurfPix(103, 53), SurfPix(0, 53));
        pF->g.bCrosshairPending = 0;
        pF->g.nTapCooldown      = 0;
    }

    /* 5. The chain tail on screen 2, both arms of 0x91D98, and the pick Game_StartNew armed. Game_StartNew stores 1
     * into bPlacementPending (0x945D4 MOV R6,#1 / 0x945E0 STRB), so the new game takes the BNE and never blits the
     * close control. Nothing below writes that byte by hand: the pick is committed the only way the game commits it,
     * through the confirm control the hover seats -- one press and one release -- and the clear at 0x8B608, inside
     * Geoscape_OnMapClick's pick arm, is what drops it. With placement no longer pending the tail reaches 0x91DA8;
     * and with the panel animation finished -- the geoscape timer tick is what advances that progress in the running
     * game, so the test steps it the same way -- the hover gets past 0x67490 into its scan stage. The panel is called
     * on its own at the end here so the map ink cannot hide the scan stage's two 75 x 30 blocks at (160,200) and
     * (5,200) (0x674BC/0x674E0). */
    {
        UiControl    *pClose   = g_world.ui.apCtl[GEOUI_CTL_186B00];
        UiControl    *pConfirm = g_world.ui.apCtl[GEOUI_CTL_186BA4];
        const PadImage *pClosePad = (pClose != NULL) ? pClose->apPads[pClose->nState] : NULL;
        BaseRec      *pBase0   = g_world.apBases[0];
        StrategItem  *pContact = NULL;
        i32           nContactHits = 0;

        ClearSurface();
        g_nBlits = 0;
        Geoscape_DrawStatusPanel(&pF->panel);
        Check(pClose != NULL && !SawPadBlit(pClosePad),
              "bPlacementPending set (0x945E0): 0x91D98's BNE skips the close-control blit");

        /* The hover is the screen's own gate: while the bottom panel is still expanding it stops at 0x67490 and
         * returns 0, and cUiAnimProgress @ 0x187058 is stepped only by the app's WM_TIMER branch
         * (0xA0C88..0xA0C90). The pen waits it out the way the timer does -- frames, not flags. */
        nMidY = (320 - (i32)g_world.ui.apPad[GEOUI_PAD_186BBC]->uHeight) / 2;
        RunFramesUntilHoverRegion();
        nTile = DragCameraOntoRegion(nMidY);
        Check(nTile >= PUFO_REGION_ID_FIRST && nTile < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT,
              "a pen stroke brought a buildable region under the hover's sample (0x87D58..0x87D7C -> 0x82124)");
        Check(pF->g.bDragActive == 0, "and that stroke's release left the drag byte clear (0x896BC)");
        (void)DrawFrame(); /* the draw is what stores the hovered tile, and it lags its own pick by a frame */
        nRegion = g_world.bPendingRegionId;
        Check(nRegion >= PUFO_REGION_ID_FIRST && nRegion < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT,
              "Geoscape_Draw stores the hovered tile into bPendingRegionId @ 0x159DF4 (0x91D94)");
        nRegionIdx = (nRegion >= PUFO_REGION_ID_FIRST && nRegion < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT)
                     ? nRegion - PUFO_REGION_ID_FIRST : 0;
        printf("       pick armed: progress %d, pending region %02X, place %d/%d, lines %d\n",
               (int)g_world.cUiAnimProgress, (int)g_world.bPendingRegionId, g_world.nPlaceX, g_world.nPlaceY,
               g_world.msgLines.nLineCount);

        Check(pConfirm != NULL && pConfirm->y == PUFO_GLOBE_LABEL_Y_BUILD
                  && pConfirm->x == 120 - (i32)(pConfirm->uWidth >> 1),
              "the region arm seated the confirm control centred on 120 at y 290 (0x679F0 / 0x67A00)");

        nBase1  = g_world.nBaseCount;
        nFunds1 = g_world.nFunds;
        nCnt1   = (i32)g_world.aRegionBuildCount[nRegionIdx];
        for (i = 0; i < PtrArray_GetCount(&g_world.strategList); ++i) {
            StrategItem *pItem = (StrategItem *)PtrArray_Get(&g_world.strategList, i);
            if (pItem != NULL && (i32)(i8)pBase0->bUnk4 == pItem->nSiteId) {
                ++nContactHits;
                if (pContact == NULL) {
                    pContact = pItem;
                }
            }
        }

        nSamples0 = g_nSamples;
        World_GeoscapePointer(&g_world, pConfirm->x + 2, pConfirm->y + 2, true);
        CheckEqI32(g_world.bFacDone, 1, "the press arms the new-base byte on its way through (0x874E4)");
        Check(pConfirm->nState == 1,
              "and the case-2 preamble hit-tests 186BA4 because the region byte is not 0xFF, so the control is pressed (0x874EC)");
        CheckEqI32(pConfirm->nState, 1, "the control is in its pressed state going into the release");
        World_GeoscapePointerUp(&g_world);

        CheckEqI32(g_nSamples, nSamples0 + 1,
                   "the release's own preamble (0x89694) released it: TryConfirm's Sound_Play(17, 4) (0x61538)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "the pick arm writes the placement screen id (0x8B600)");
        CheckEqI32(g_world.bPlacementPending, 0, "and clears the pick it committed (0x8B608)");
        CheckEqI32(g_world.bFacDone, 0,
                   "with the new-base byte the press had set -- only this arm has that clear (0x8B60C, cf. 0x8B64C)");
        Check(g_world.nBaseCount == nBase1 && g_world.nFunds == nFunds1,
              "committing a pick makes no base and spends nothing");
        Check(pBase0 != NULL && pBase0->nUnk8 == g_world.nPlaceX && pBase0->nUnk12 == g_world.nPlaceY,
              "the base takes the pick the hover resolved (0x8B504 / 0x8B518)");
        CheckEqI32((i32)pBase0->bRegionId, nRegion, "its region byte is the hovered tile (0x8B52C)");
        CheckEqI32((i32)pBase0->bUnk16, 1, "and its +0x10 byte is set (0x8B53C)");
        CheckEqI32((i32)g_world.aSiteRecs[0].bType, nRegionIdx,
                   "the site record's type is the tile less 0x80 (SUB R0,R1,#0x80 at 0x8B544)");
        Check(g_world.aSiteRecs[0].abName[0] != 0, "the label built at 0x8B550 is copied into the record (0x8B56C)");
        CheckEqI32((i32)g_world.aRegionBuildCount[nRegionIdx], nCnt1 + 1,
                   "and building that label bumped the region's counter, the label holding the value from before it (0x6A46C, 0x6A49C)");
        if (pContact != NULL) {
            Check(pContact->nX == g_world.nPlaceX && pContact->nY == g_world.nPlaceY,
                  "the contact whose site id is the base's was moved to the pick (0x8B5E4 / 0x8B5F8)");
        } else {
            CheckEqI32(nContactHits, 0,
                       "no contact carries the starting base's site id, so the pick arm's scan (0x8B570..0x8B5B4) finds nothing to move");
        }

        g_world.nav.apNav[0]->pfnClick(); /* NewBaseScreen_Open @ 0x615d0, the same control the nav bar carries */
        CheckEqI32(g_world.screen.nGeoscapeScreen, 2, "and the nav bar's own opener puts the camera back on screen 2 (0x61620)");
        RunFramesUntilHoverRegion(); /* its 0x6164C clear re-arms the panel wipe the hover waits on */

        ClearSurface();
        g_nBlits = 0;
        Geoscape_DrawStatusPanel(&pF->panel);
        Check(pClose != NULL && SawPadBlit(pClosePad),
              "with placement not pending the tail blits the close control (0x91DA8)");
        Check(SurfPix(160, 200) != 0 && SurfPix(5, 200) != 0,
              "the hover reached its scan stage: it fills 75 x 30 at (160,200) and (5,200) (0x674BC/0x674E0)");
        Check(g_world.nGlobeScanX != 0 || g_world.nGlobeSweepV != 0 || g_world.nGlobeSweepH != 0,
              "the scan stage writes the three shell globals the wiring points at (0x67680, 0x67670, 0x67674)");
        printf("       hover past the animation: scan %d sweepV %d sweepH %d, control blits %d, "
               "pending region %d, scan ink %04X\n",
               (int)g_world.nGlobeScanX, (int)g_world.nGlobeSweepV, (int)g_world.nGlobeSweepH,
               (int)g_nBlits, (int)g_world.bPendingRegionId, SurfPix(160, 200));
    }

    /* 6. The frame end's modal gate: the phase cell is written only for the saving and loading list
     * modes (0x944C4 / 0x94500), and 0x28 is the value (0x944CC). */
    g_world.sModal.nActionPhaseSave112 = 0;
    g_world.nListMode = PUFO_GEOFRAME_LIST_MODE_SAVING;
    (void)DrawFrame();
    CheckEqI32(g_world.sModal.nActionPhaseSave112, PUFO_GEOFRAME_PHASE_SAVE,
               "list mode 4 writes Map.anActionPhaseSave0[112] = 0x28 (0x944E0)");
    CheckEqI32(g_nPresents, 1, "and the frame still presents once (0x94538)");
    g_world.sModal.nActionPhaseSave112 = 0;
    g_world.nListMode = 0;
    (void)DrawFrame();
    CheckEqI32(g_world.sModal.nActionPhaseSave112, 0, "any other list mode leaves the cell alone");

    /* 7. The pointer dispatcher over the same envs. */
    ClearSurface();
    g_world.msgLines.nLineCount = 0; /* 0x87CF8: a message on the status line stops the scrolling */
    g_world.view.nScrollX = -800;
    g_world.view.nScrollY = -400;
    g_world.view.nLastDx  = 0;
    g_world.view.nLastDy  = 0;
    pF->g.nTapCooldown = 0;
    pF->g.bDragActive  = 0;

    World_GeoscapePointer(&g_world, 100, 100, true);
    CheckEqI32(pF->g.nCursorX, 100, "OnPointer stores the pen as the cursor pair (0x872F8 / 0x8731C)");
    CheckEqI32(pF->g.nCursorY, 100, "as two halfwords");
    CheckEqI32(pF->g.bDragActive, 1, "a press below the strip starts the globe drag (0x87D58)");
    CheckEqI32(pF->g.nTapCooldown, 0, "the drag branch clears the tap cooldown (0x87D80)");
    CheckEqI32(g_world.view.nLastDx, 100, "and records the pen as the last position (0x87D6C)");

    /* A press inside the map area is the drag-start arm: 0x87D74/0x87D7C write the pen into
     * view+0x14/+0x18 BEFORE the call at 0x87D14, and Smap_ScrollBy adds the stored value straight back
     * (0x820F4..0x82114), so the delta cancels and a press on its own never moves the camera. */
    World_GeoscapePointer(&g_world, 140, 120, true);
    CheckEqI32(g_world.view.nScrollX, -800, "the press arm stores the pen first, so the delta cancels (0x87D74 -> 0x820F4)");
    CheckEqI32(g_world.view.nScrollY, -400, "on both axes");
    CheckEqI32(g_world.view.nLastDx, 140, "and records the new pen position (0x82164)");

    /* What does move it is the event that reaches DragDone without re-storing the pen: the same pen with
     * the button up while bGeoscapeDragActive is still set (0x87C48, 0x87CEC, 0x87CF8). */
    World_GeoscapePointer(&g_world, 180, 140, false);
    CheckEqI32(g_world.view.nScrollX, -800 - 180 + 140,
               "pos - delta + last_delta moves the camera by the pen's own travel (0x820F4..0x82104)");
    CheckEqI32(g_world.view.nScrollY, -400 - 140 + 120,
               "on both axes, with no Y clamp because the id is 2 (0x82120 BEQ loc_82164)");

    /* The pick commit earlier in this run played its own samples, so the claim here is about THIS release. */
    nSamples0 = g_nSamples;
    World_GeoscapePointerUp(&g_world);
    CheckEqI32(pF->g.bDragActive, 0, "the release clears the drag flag (0x896BC)");
    CheckEqI32(g_nSamples, nSamples0,
               "screen 2 with an empty message line, no pressed control and no pending pick plays no sample on release");

    /* 8. The cursor pair really is 16-bit: a pen below the surface wraps the stored halfword. */
    World_GeoscapePointer(&g_world, 100, 33000, true);
    CheckEqI32(pF->g.nCursorY, (i32)(i16)33000, "0x872F8 stores the pen with STRH (0x872F8)");
    pF->g.nCursorY = 0;

    /* 9. The arms the wiring does not serve stay NULL, which is the arm not drawing at all. Case 20 is no
     * longer in that list: the shell owns the base-attack record, so the arm points at its own draw env. */
    Check(pF->draw.pPlaneArm == NULL
              && pF->draw.pResearchDraw == NULL && pF->draw.pBaseInfoArm == NULL
              && pF->draw.pManufPad == NULL && pF->draw.pTransferPad == NULL
              && pF->draw.pBuySellPad == NULL,
          "the arms whose screen the shell cannot open stay NULL (see pufo_geoscape.h)");
    Check(pF->draw.pBaseAtEnv == &pF->baseAtDraw,
          "case 20's arm reaches BaseAtScreen_Draw through the shell's own draw env (0x944B4)");
    Check(pF->draw.pSoldierDraw == &pF->solDraw && pF->draw.pSoldierArm == &pF->solArm
              && pF->draw.pSoldierPanelCtl == &pF->solPanelCtl
              && pF->draw.pSoldierFindCtl == &pF->solFindCtl
              && pF->draw.pBaseFacArm == &pF->baseFac && pF->draw.pBaseFacGrid == &pF->facNameGrid
              && pF->draw.pBaseFacStrip == &pF->facStrip && pF->draw.pBaseFacDialog == &pF->facDialog
              && pF->baseFac.apPanelCtl == g_world.apFacPanelCtl
              && g_world.ptrEnv.apFacCtl == g_world.apFacPanelCtl
              && pF->baseFac.pPad == g_world.ui.apPad[GEOUI_PAD_186BA0]
              && pF->solArm.pbTransferFlag == &g_world.bTransferFlag
              && pF->solDraw.pnCursor == &g_world.nSoldierCursor
              && pF->solPanelCtl.apCtl == g_world.nav.apPanel
              && pF->solFindCtl.apCtl == g_world.nav.apFind,
          "cases 4, 5 and 7 run on the shell's own soldier panels, block controls and dialog state");
    Check(pF->draw.pPlacementArm == &pF->placement && pF->draw.pPlacementCtl == &pF->placementCtl
              && pF->draw.pPlacementGrid == &pF->facNameGrid
              && pF->draw.pPlacementStrip == &pF->facStrip
              && pF->placement.pPad == g_world.ui.apPad[GEOUI_PAD_186BA0]
              && pF->placement.apNavCtl == g_world.nav.apNav
              && pF->placement.pCloseCtl == g_world.ui.apCtl[GEOUI_CTL_186B00]
              && pF->placement.pMapCtl == g_world.ui.apCtl[GEOUI_CTL_186BB4]
              && pF->placement.pnMapPhase == &g_nMapPhase
              && pF->placement.pnSmapOriginX == &g_world.nSmapOriginX,
          "Geoscape_Draw case 1 (0x91DC8) runs on the shell's own pad, nav set, controls and map phase");
    Check(pF->radar.pRadarEnv == NULL && g_world.ptrEnv.apRadarCtl == NULL,
          "the radar arm's own env and aRadarControls @ 0x159684 stay unwired");
    Check(pF->panel.pfnCtlOpen == NULL && pF->panel.pfnCtlSfx == NULL,
          "the panel's four screen openers stay unwired (0x85194 / 0x852b8 / 0x84d18 / 0x84bf8)");

    /* 10. The clock. Geoscape_PulseUpdate @ 0x954dc is WM_TIMER 0xC353's geoscape half (UiPulse_OnTimer
     * 0x36630 returns at 0x36644 after one hook call), and it advances the clock only while the geoscape
     * screen id is 0 (0x95618), so the id goes to 0 here the way the map screen has it. */
    Check(g_world.geo.pList == &g_world.strategList
              && g_world.geo.pnNextDisplayId == &g_world.nNextDisplayId
              && g_world.geo.pnNextSiteId == &g_world.nNextSiteId
              && g_world.geo.pnDaysSinceStart == &g_world.nAlienPoolAddend
              && g_world.geo.pnSelectedShipIdx == &g_world.nSelectedShipIdx
              && (const void *)g_world.geo.pnLowSupplyFlag == (const void *)&g_world.nShortfall1870C8
              && g_world.geo.apEmptyFacilityCells == g_world.abRadarPct
              && g_world.geo.pnSoldierCount == &g_world.nSoldierCount,
          "the tick state points at the shell's own cells: one instance of each global (0x186534, "
          "0x186544/48, 0x1472C8, 0x187054, 0x1870C8, 0x162008, 0x115350)");
    Check(g_world.geoHooks.pfnContactMsg != NULL && g_world.geoHooks.pfnGrantSiteRecovery != NULL
              && g_world.geoHooks.pfnRefreshBaseTabs != NULL
              && g_world.geoHooks.pfnAccumulateResearch != NULL
              && g_world.geoHooks.pfnTickFacilityConstruction != NULL
              && g_world.geoHooks.pfnClearFacilityGridRow != NULL
              && g_world.geoHooks.pfnCountEmptyFacilityCells != NULL
              && g_world.geoHooks.pfnOpenMonthly != NULL
              && g_world.geoHooks.pfnProcessEvents != NULL
              && g_world.geoHooks.pUser == &g_world,
          "the ten ported GeoscapeHooks callees are wired over the shell state");
    Check(g_world.geoClock.pfnTickShips != NULL && g_world.geoClock.pfnTickHourly != NULL
              && g_world.geoClock.pfnTickBases != NULL && g_world.geoClock.pfnOpenMonthly != NULL
              && g_world.geoClock.pUser == &g_world.geoBundle,
          "Geoscape_BuildClockHooks fills the four hooks Geoscape_AdvanceTime calls");

    nTick = g_world.geo.world.nTickSeconds; /* nGeoscapeTickSeconds @ 0x187070 */
    Check(nTick > 0, "the new game leaves a real tick length at 0x187070");

    g_world.screen.nGeoscapeScreen = 0;
    g_world.geo.time.bPaused = 0;
    nSec0 = g_world.geo.time.nTotalSec;
    for (i = 0; i < 4; ++i) {
        World_PulseGeoscape(&g_world);
    }
    CheckEqI32(g_world.geo.time.nTotalSec, nSec0 + 4 * (nTick + 1),
               "four pulses advance the clock by nTickSeconds + 1 each (0x95618 -> 0x5ce40)");
    Check(g_world.geo.time.nMonth >= 1 && g_world.geo.time.nMonth <= 12
              && g_world.geo.time.nDay >= 1 && g_world.geo.time.nDay <= 31
              && g_world.geo.time.nHour >= 0 && g_world.geo.time.nHour <= 23,
          "and GameTime_Decode @ 0x5cdbc ran on the way out (0x95628)");

    /* The four radar counters step by one and stop at 255 on EVERY screen (0x95B80..0x95BCC). */
    g_world.nRadarCooldownL = 254;
    g_world.nRadarMsgTimer  = 255;
    g_world.nRadarPulseTimer = 7;
    pF->g.nTapCooldown      = 0;
    g_world.nShortfall1870C8 = 0;
    World_PulseGeoscape(&g_world);
    CheckEqI32(g_world.nRadarCooldownL, 255, "nRadarCooldownL @ 0xAC21C steps to its cap");
    CheckEqI32(g_world.nRadarMsgTimer, 255, "nRadarMsgTimer @ 0xAC220 never goes past it");
    CheckEqI32(g_world.nRadarPulseTimer, 8, "nRadarPulseTimer @ 0xAC224 steps by one");
    CheckEqI32(pF->g.nTapCooldown, 1, "and nRadarCooldownR @ 0xAC218 is the frame's tap-cooldown cell");

    /* dwActionFail == 1 stops the whole tick before anything, cooldowns and clock included (0x954EC). */
    g_world.nShortfall1870C8 = 1;
    nSec0 = g_world.geo.time.nTotalSec;
    World_PulseGeoscape(&g_world);
    CheckEqI32(g_world.geo.time.nTotalSec, nSec0, "the latch stops the clock");
    CheckEqI32(g_world.nRadarPulseTimer, 8, "and the four counters too");
    g_world.nShortfall1870C8 = 0;

    /* 11. The held-direction scroll. The switch arm runs only when nUiSlot0AnimPhase @ 0x113A18 ARRIVES
     * as 1 (0x95644..0x95654) and the phase is cleared at 0x95B78 either way, so the test puts the 1 in
     * by hand -- nothing else in the geoscape leaves it there. Map screen (id 0) scrolls by 25, and
     * Smap_ScrollBy takes pos - delta + last_delta with nLastDx / nLastDy preset to 120 / 160, so the
     * camera moves by exactly the step. */
    /* The starting pair sits inside the screen-0 clamp window Smap_ClampScrollY keeps
     * (-nHalfH .. stripHeight - nHalfH + mapH - 320, i.e. -40..124 for the 800x400 zoom-0 map with the
     * 0x186BBC strip), so the two axis checks below see the plain step and not the clamp. */
    g_world.view.nScrollX = 200;
    g_world.view.nScrollY = 60;
    g_world.view.nLastDx  = 0;
    g_world.view.nLastDy  = 0;
    g_world.abScrollFlags[PUFO_PULSE_FLAG_UP]    = 1; /* +y at 0x95718 */
    g_world.abScrollFlags[PUFO_PULSE_FLAG_RIGHT] = 1; /* +x at 0x95724 */
    g_world.abScrollFlags[PUFO_PULSE_FLAG_DOWN]  = 0;
    g_world.abScrollFlags[PUFO_PULSE_FLAG_LEFT]  = 0;
    *g_world.pnAnimPhase = 1;
    World_PulseGeoscape(&g_world);
    CheckEqI32(g_world.view.nScrollX, 200 - 25,
               "the right flag scrolls by the map step: 200 - (120+25) + 120 (0x95864 -> 0x82104)");
    CheckEqI32(g_world.view.nScrollY, 60 - 25, "and the up flag on y (0x82114)");
    CheckEqI32(g_world.view.nLastDx, 120 + 25, "the pair is stored after the clamp (0x82164)");
    CheckEqI32(g_world.view.nLastDy, 160 + 25, "on both axes");
    Check(g_world.abScrollFlags[PUFO_PULSE_FLAG_UP] == 0
              && g_world.abScrollFlags[PUFO_PULSE_FLAG_RIGHT] == 0
              && g_world.abScrollFlags[PUFO_PULSE_FLAG_DOWN] == 0
              && g_world.abScrollFlags[PUFO_PULSE_FLAG_LEFT] == 0,
          "the pulse clears all four direction bytes (0x95874..0x95880)");
    CheckEqI32(*g_world.pnAnimPhase, 0, "and the animation phase at 0x95B78");

    /* 12. The screen close (Geoscape_CloseUI @ 0x8b94c) over the shell's own env. A new game ends on screen 2
     * with bPlacementPending set (Game_StartNew 0x945D4/0x945E0), and case 2 returns at 0x8BE58 while that byte
     * is set -- so the globe close does nothing until the base is confirmed. Case 1 then reaches the map, where
     * the pulse's clock gate (section 10) is open again. */
    g_world.nShortfall1870C8 = 1u;
    g_world.screen.nGeoscapeScreen = 2;
    g_world.bPlacementPending = 1;
    g_world.bFacDone          = 1;
    g_world.bArmedPlacement   = 1;
    World_CloseGeoscape(&g_world);
    CheckEqI32(g_world.screen.nGeoscapeScreen, 2,
               "case 2 returns before touching the id while bPlacementPending @ 0x1870AC is set (0x8BE58)");
    CheckEqI32((i32)g_world.nShortfall1870C8, 0, "the head cleared dwActionFail @ 0x1870C8 (0x8B96C, a dword)");

    g_world.bPlacementPending = 0;
    World_CloseGeoscape(&g_world);
    CheckEqI32(g_world.screen.nGeoscapeScreen, 1,
               "the confirmed globe close lands on the placement screen (0x8BE68)");
    Check(g_world.bFacDone == 0 && g_world.bArmedPlacement == 0,
          "and clears bBaseFacDone @ 0x187078 and bBaseFacPlaceArmed @ 0x158888 (0x8BE70, 0x8BE74)");

    /* 12b. The screen that close lands on: Geoscape_Draw case 1 (0x91DC8..0x92ADC), the placement arm,
     * drawn over the shell's own state. The cursor colour comes from nMapPhase (0x92464 MOV R7,#0xA,
     * 0x92470 ADD R3,R2,#0xF), so the phase byte is fixed here to make the ink checkable. */
    g_nMapPhase = 5;
    g_world.nSelectedBase = 0;
    g_world.nSmapTapX = 2;
    g_world.nSmapTapY = 1;
    g_world.bPlaceActive = 1; /* bBasePlaceActive @ 0x1870BC */
    g_world.bPlaceFail   = 0;
    nIdx = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * g_world.nSelectedBase
         + PUFO_PLA_TAP_CELL_STRIDE * g_world.nSmapTapY + g_world.nSmapTapX; /* 0x923F0..0x92418 */
    g_world.abFacilityGrid[nIdx] = 3; /* not a 9..12 kind, so the one-cell cursor branch at 0x925AC */

    nNavCnt = 0;
    for (i = 0; i < PUFO_PLA_NAV_CTLS; ++i) {
        if (g_world.nav.apNav[i] != NULL) {
            ++nNavCnt;
        }
    }
    ClearSurface();
    (void)DrawFrame();

    Check(g_world.ui.apCtl[GEOUI_CTL_186B00]->x == PUFO_PLA_CLOSE_X
              && g_world.ui.apCtl[GEOUI_CTL_186B00]->y == PUFO_PLA_CLOSE_Y
              && g_world.ui.apCtl[GEOUI_CTL_186B00]->pfnClick != NULL,
          "the arm moves the close control to (194,4) and installs its close thunk (0x9201C..0x9203C)");
    CheckEqI32(g_nBlits, nNavCnt + 2,
               "the nav set, the close control and the demolish control are the frame's control blits "
               "(0x92010, 0x92040, 0x927C8)");
    Check(g_world.ui.apCtl[GEOUI_CTL_186BB4]->pfnClick != NULL
              && g_world.ui.apCtl[GEOUI_CTL_186BB4]->y == PUFO_PLA_MAPCTL_Y,
          "a non-empty cell installs the map-click handler and seats the control at y 15 (0x92764, 0x927B8)");

    nPhase = PUFO_PLA_TAP_COLOR_STEP * ((i32)g_nMapPhase + PUFO_PLA_TAP_COLOR_BASE);
    nCurX  = g_world.nSmapOriginX + PUFO_PLA_TAP_CELL * g_world.nSmapTapX;   /* ADD R1,R0,R1,LSL#5 */
    nCurY  = g_world.nSmapOriginY + PUFO_PLA_TAP_CELL * g_world.nSmapTapY + PUFO_PLA_TAP_Y0;
    CheckEqI32(SurfPix(nCurX, nCurY), PackInk(0, nPhase, 0),
               "the cursor's top edge sits at origin + 32 * tap, in the nMapPhase colour (0x925F0)");
    CheckEqI32(g_world.bPlaceActive, 1,
               "a non-empty cell keeps the placement flag set (0x92424 takes the empty branch instead)");

    /* 12c. The arm's two grid callees and their arguments: the base index is re-read from 0x159D81 with
     * every call, and the strip's highlight row is the arm's own immediate, not a state value. */
    CheckEqI32(pF->facNameGrid.nBaseIdx, (i32)g_world.nSelectedBase,
               "the name grid's base index follows nSelectedBaseIdx every frame (0x922B0 LDRSB [R5,#0x619])");
    CheckEqI32(pF->facStrip.nHighlightRow, PUFO_PLA_STRIP_MARK,
               "the wiring's own strip row is the arm's -1 (0x922B8 MOV R1,#0xFFFFFFFF)");

    g_world.abFacilityGrid[nIdx] = 0;
    (void)DrawFrame();
    CheckEqI32(g_world.bPlaceActive, 0,
               "an empty cell ends the placement: the arm clears bBasePlaceActive (0x92424 -> 0x92844)");

    /* 12d. The other three screens the shell can serve, opened the way the game opens them: through the
     * handler the control carries. */
    Check(g_world.nav.apNav[0]->pfnClick != NULL && g_world.nav.apNav[4]->pfnClick != NULL
              && g_world.nav.apPanel[0]->pfnClick != NULL && g_world.nav.apFind[2]->pfnClick != NULL,
          "the fifteen nav handlers are installed on the controls (Geoscape_BuildNavControls 0x67B74..0x67D28)");

    nSamples0 = g_nSamples;
    g_world.nav.apNav[4]->pfnClick(); /* BaseFacScreen_Open @ 0x61844 */
    CheckEqI32(g_world.screen.nGeoscapeScreen, 7, "nav slot 4 writes the id 7 BEFORE its Init (0x6185C)");
    CheckEqI32(g_nSamples, nSamples0 + 1, "and plays sample 17 first, with its second argument 4 (0x61850)");
    Check(g_world.ctlFacUp.pfnClick != NULL && g_world.ctlFacDown.pfnClick != NULL,
          "BaseFacScreen_Init @ 0x5a198 put the prev/next handlers on the two block controls");

    CheckEqI32(g_world.bFacDialogOpen, 1,
               "the build dialog is open at this point (0x158884, 1 while the panel exists)");
    ClearSurface();
    g_world.bArmedPlacement = 0;
    (void)DrawFrame();
    /* The arm's own pad blit (0x92AF4) is a direct PadImage_Blit, not the control seam, so the arm is checked
     * through its rails: 24 steps of 7 at x 2, 1x7 rects climbing from y 1 in colour 100 + 5k. */
    CheckEqI32(SurfPix(2, 1), PackInk(100, 100, 100),
               "case 7's first climbing rail rect is at (2,1) in colour 100 (0x92B08..0x92B2C)");
    CheckEqI32(g_nBlits, 3, "the two block controls (0x92D78..0x92D94) plus the open dialog's own control, blitted "
                            "inside BaseFacScreen_DrawBuildDialog @ 0x59b60, which the arm calls at 0x92D44");
    ClearSurface();
    g_world.bArmedPlacement = 1;
    (void)DrawFrame();
    CheckEqI32(g_nBlits, 1, "with bBaseFacPlaceArmed @ 0x158888 set the arm blits NO control of its own (0x92D64); "
                            "the dialog call at 0x92D44 sits before that gate");

    /* 12e. What that dialog call reaches once the overlay env is wired: BaseFacScreen_DrawPlacementOverlay
     * @ 0x58e10, the call at 0x59EF8, drawn and committed over the shell's own state. The facility kind is the
     * dialog panel's own row aux (0x58E20) and only the hangar makes the footprint 2x2 (0x58E34), so the
     * geometry below follows the panel's record instead of a fixed kind. */
    nKind = UiPanel_GetLineAux(g_world.pFacPanel, pF->facOverlay.nPanelSel);
    Check(nKind >= 0 && nKind < PUFO_BASEFAC_COUNT,
          "the dialog panel's selected row carries a facility kind (BaseFacScreen_Toggle stored it as the aux)");
    nSpan  = (g_world.aFac[nKind].bFlag21 == PUFO_FAC_TYPE_HANGAR) ? PUFO_FAC_OVERLAY_SPAN : 0;
    nCellW = (nSpan != 0) ? 2 * PUFO_PLA_TAP_CELL : PUFO_PLA_TAP_CELL;
    nPhase = PUFO_PLA_TAP_COLOR_STEP * ((i32)g_nMapPhase + PUFO_PLA_TAP_COLOR_BASE); /* 0x5909C..0x590AC */

    g_world.bArmedPlacement  = 1;      /* 0x59B84: only the armed branch reaches 0x59EF8 */
    g_world.bRoot34          = 1;      /* bBaseFacPreviewActive @ 0x18652C: aimed, so 0x58E8C takes the cursor */
    g_world.bRoot35          = 1;      /* bBaseFacPreviewOk @ 0x18652D */
    g_world.bFacOverlayShown = 0;
    g_world.nUnk15887C       = 0;      /* the pending pair, deliberately NOT the aimed tap */
    g_world.nUnk158880       = 0;
    memset(g_world.abFacilityGrid, 0, sizeof(g_world.abFacilityGrid)); /* an empty base: the score can only be 0 */
    nCurX = pF->facOverlay.nOriginX + 2 * PUFO_PLA_TAP_CELL;
    nCurY = pF->facOverlay.nOriginY + 2 * PUFO_PLA_TAP_CELL + PUFO_PLA_TAP_Y0;
    pF->g.nCursorX = nCurX;            /* nSmapCursorX @ 0x186BD0, which the overlay reads as a halfword */
    pF->g.nCursorY = nCurY;
    nBase0  = g_world.nBaseCount;
    nFunds0 = g_world.nFunds;

    ClearSurface();
    (void)DrawFrame();

    CheckEqI32(g_world.nSmapTapX, 2, "the overlay resolves the tap from the cursor and writes it back (0x58EC0)");
    CheckEqI32(g_world.nSmapTapY, 2, "on both axes (0x58EC8)");
    CheckEqI32(g_world.bFacOverlayShown, 1, "a cursor inside 0 < dx < 192 - span and 36 < dy < 227 - span raises "
                                            "bBaseFacOverlayShown @ 0x15888C (0x59234)");
    CheckEqI32(g_world.bRoot35, 0, "an empty footprint scores 0 and clears bBaseFacPreviewOk (0x599AC)");
    CheckEqI32(SurfPix(nCurX, nCurY), PackInk(nPhase, 0, 0), "the outline's top-left corner: a zero score puts the "
                                                             "phase colour in the index byte");
    CheckEqI32(SurfPix(nCurX + nCellW - 1, nCurY), PackInk(nPhase, 0, 0), "the right edge of the same rect");
    CheckEqI32(SurfPix(nCurX, nCurY + nCellW - 1), PackInk(nPhase, 0, 0), "and its bottom edge");
    Check(g_world.nBaseCount == nBase0 && g_world.nFunds == nFunds0, "and a zero score commits nothing");

    g_world.bRoot35 = 1;
    g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * g_world.nSelectedBase + 6 * 1 + 2] = 1;
    ClearSurface();
    (void)DrawFrame();
    CheckEqI32(SurfPix(nCurX, nCurY), PackInk(0, 0, nPhase), "a neighbour above the tap scores non-zero and the same "
                                                             "four rects swap the colour into the low byte");
    CheckEqI32(g_world.bRoot35, 1, "and bBaseFacPreviewOk survives a non-zero score");
    Check(g_world.nBaseCount == nBase0 && g_world.nFunds == nFunds0,
          "the commit still does not run: the aimed tap is not the pending pair (the guards at 0x599D8..0x599EC)");

    nSiteId0    = g_world.nCreateNextSiteId;      /* dword_186558 */
    nCost0      = (i32)g_world.aFac[nKind].uCost; /* aBaseFacFlags[kind] + 0x18, read at 0x59AC0 */
    nRegionCnt0 = (i32)g_world.aRegionBuildCount[0];
    g_world.nFunds = 2000000;                     /* the purchase gate at 0x59A2C must be open */
    nFunds0 = g_world.nFunds;
    g_world.nUnk15887C = 2;                       /* now the pending pair IS the aimed tap */
    g_world.nUnk158880 = 2;
    g_world.bRoot34 = 1;
    g_world.bRoot35 = 1;
    g_world.bFacDone = 1;                         /* 0x187078, byte 4: new-base mode, read at 0x59588 */
    g_world.bPendingRegionId = 0x80;              /* 0x159DF4, region 0x80 */
    g_world.nPlaceX = -6000;                      /* nPendingPickX/Y @ 0x159D94/0x159D98, the two loads at 0x59AF4 */
    g_world.nPlaceY = -6000;
    (void)DrawFrame();

    CheckEqI32(g_world.nFunds, nFunds0 - nCost0 - g_anNewBaseCostByRegion[0],
               "the commit spends the record's own cost (the store at 0x59AC8) and the region's new-base cost (0x59B18)");
    CheckEqI32(g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * g_world.nSelectedBase
                                      + PUFO_GRID_FAC_TYPE_OFF + 6 * 2 + 2], (i32)g_world.aFac[nKind].bFlag21,
               "BaseFac_SetCellBuildState writes the aimed cell's kind (0x59A9C)");
    CheckEqI32(g_world.nBaseCount, nBase0 + 1, "Base_CreateNamedEntry (0x59AFC) raises the base count 0x159D80");
    CheckEqI32(g_world.nCreateNextSiteId, nSiteId0 + 1, "and the next site id dword_186558");
    CheckEqI32((i32)g_world.aSiteRecs[nBase0].bType, 0, "the new site record's bType is region - 0x80");
    Check(g_world.aSiteRecs[nBase0].abName[0] != 0, "with the label BaseFac_FormatCostLabel built (0x59AE4) as its name");
    CheckEqI32((i32)g_world.aRegionBuildCount[0], nRegionCnt0 + 1,
               "and that label's counter goes up by one in g_anRegionBuildCount @ 0x18655D (0x6A49C)");
    CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "the commit lands the geoscape on the placement screen (0x59B2C)");
    Check(g_world.bFacDone == 0 && g_world.bRoot34 == 0 && g_world.bRoot35 == 0,
          "clearing the new-base byte (0x59B30), bBaseFacPreviewActive (0x59B28) and bBaseFacPreviewOk (0x59B34)");
    CheckEqI32(g_world.bFacDialogOpen, 0, "BaseFacScreen_Toggle closes the dialog (the call at 0x59B1C)");
    Check(g_world.pFacPanel == NULL, "and frees its panel, which the overlay read its kind from");
    CheckEqI32(g_world.nShortfall1870C8, 0, "with no shortfall left behind: dwActionFail @ 0x1870C8 stays clear");
    g_world.bArmedPlacement = 0;
    g_world.nUnk15887C = 0;
    g_world.nUnk158880 = 0;

    /* 12g. A whole new base, reached only through the seams the game uses. Nav slot 0 is NewBaseScreen_Open
     * @ 0x615d0 (installed at 0x67B74); the globe hover seats its "Build base" control at (120 - width/2, 290)
     * (0x679F0 / 0x67A00); that control's handler is NewBaseScreen_TryConfirm @ 0x6152c -- Sound_Play(17, 4),
     * then the funds gate at 0x6156C, skipped while a pick is pending -- and its tail call is Geoscape_OnMapClick
     * @ 0x8b4a8. OnMapClick's no-pick arm (0x8B634) seats the build screen on the base COUNT (0x8B638 / 0x8B644),
     * which is the slot the new base takes, writes the id 7 (0x8B64C) and NEVER clears the new-base byte the
     * press set at 0x874E4; the clear lives in the other arm, at 0x8B60C. That is what lets the overlay's gate at
     * 0x59ACC take 12f's pen build through Base_CreateNamedEntry @ 0x66930 instead of a facility on an old base.
     *
     * The hovered region is not written by hand. The hover samples one fixed screen point -- (120, nMidY) with
     * nMidY = (320 - strip height) / 2, 0x67328 / 0x6734C -- through Smap_GetTileAtScreen @ 0x82244, so the region
     * follows the view scroll. When the point the new-game camera already samples is not a buildable region the
     * scroll is moved here the way the game moves it -- pen strokes through World_GeoscapePointer, never an
     * assignment. Funds are data too: three purchases have to be affordable. */
    nBase1    = g_world.nBaseCount;                    /* 12e's commit made this two */
    nSiteId1  = g_world.nCreateNextSiteId;             /* dword_186558 */
    nMode0    = g_world.view.nMode;
    nMidY     = (320 - (i32)g_world.ui.apPad[GEOUI_PAD_186BBC]->uHeight) / 2;
    g_world.nFunds = 3000000;
    nFunds1 = g_world.nFunds;

    nSamples0 = g_nSamples;
    g_world.nav.apNav[0]->pfnClick();                  /* NewBaseScreen_Open @ 0x615d0 */
    CheckEqI32(g_world.screen.nGeoscapeScreen, 2, "nav slot 0 writes the new-base screen id (0x61620)");
    CheckEqI32(g_nSamples, nSamples0 + 1, "after its Sound_Play(17, 4) (0x615dc)");
    Check(g_world.ui.apCtl[GEOUI_CTL_186B00]->x == 224 && g_world.ui.apCtl[GEOUI_CTL_186B00]->y == 1,
          "with the close control at (224,1) (0x61608 / 0x61614)");
    CheckEqI32(g_world.view.nMode, nMode0,
               "and its two Smap_ZoomOut change nothing at zoom 0: level 0 has no outward step (0x81E5C)");

    Check(pF->hover.pTiles != NULL, "the frame carries the tile grid the hover samples (0x186B04 -> pData)");
    CheckEqI32(g_world.bUiAnimStarted, 0, "and the opener re-arms the bottom panel's expansion (0x187080, 0x6164C)");

    /* The hover is the screen's own second gate: while that panel is still expanding it stops at 0x67490 and
     * returns 0, so the scan animation, the readouts, the region byte and the confirm-control seat all wait for
     * the panel to finish. The pen waits it out the way the render timer does -- frames, not flags. */
    nTile = DragCameraOntoRegion(nMidY);
    Check(nTile >= PUFO_REGION_ID_FIRST && nTile < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT,
          "the shipped grid holds a buildable region and the pen strokes reached it (0x87D58..0x87D7C -> 0x82124)");
    Check(pF->g.bDragActive == 0, "and the stroke's release leaves the drag byte clear (0x896BC)");
    RunFramesUntilHoverRegion(); /* the wipe NewBaseScreen_Open re-armed at 0x6164C */
    nRegion = g_world.bPendingRegionId;
    Check(nRegion >= PUFO_REGION_ID_FIRST && nRegion < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT,
          "the globe hover resolves a buildable region from its own sample (0x67894, returned at 0x67B0C)");
    /* 0x6786C / 0x67874 store the pick of the PREVIOUS sample: the hover writes the view's nPickX/nPickY first
     * and only then calls Smap_GetTileAtScreen @ 0x82244, which overwrites them. Over a still camera the two
     * coincide, so one more frame makes the pair comparable. */
    ClearSurface();
    (void)DrawFrame();
    Check(g_world.nPlaceX == g_world.view.nPickX && g_world.nPlaceY == g_world.view.nPickY,
          "nPendingPickX/Y @ 0x159D94/0x159D98 carry the pick the hover resolved a frame earlier (0x67860..0x67874)");
    Check(g_world.ui.apCtl[GEOUI_CTL_186BA4]->y == PUFO_GLOBE_LABEL_Y_BUILD
              && g_world.ui.apCtl[GEOUI_CTL_186BA4]->x == 120
                 - (i32)(g_world.ui.apCtl[GEOUI_CTL_186BA4]->uWidth >> 1),
          "the region arm seats the confirm control centred on 120 at y 290 (0x679F0 / 0x67A00)");

    nRowX = g_world.ui.apCtl[GEOUI_CTL_186BA4]->x + 2;
    nRowY = g_world.ui.apCtl[GEOUI_CTL_186BA4]->y + 2;
    nRegionIdx = (nRegion >= PUFO_REGION_ID_FIRST && nRegion < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT)
                 ? nRegion - PUFO_REGION_ID_FIRST : 0;
    nNewCost   = (i32)g_anNewBaseCostByRegion[nRegionIdx];
    Check(g_world.bPlacementPending == 0, "the starting pick was committed earlier in this run, so the gate is live (0x6153C)");

    g_world.nFunds   = nNewCost - 1;                   /* the gate's own price, one short */
    nSamples0        = g_nSamples;
    World_GeoscapePointer(&g_world, nRowX, nRowY, true);
    World_GeoscapePointerUp(&g_world);
    CheckEqI32(g_nSamples, nSamples0 + 1, "TryConfirm plays its sample before the gate (0x61538)");
    CheckEqI32(g_world.nShortfall1870C8, 1, "short of the region's price it fails: dwActionFail @ 0x1870C8 (0x61574)");
    CheckEqI32(g_world.nResidue1870B4, 0, "with dwActionFailReason 0 (0x61580)");
    CheckEqI32(g_world.screen.nGeoscapeScreen, 2, "so the tail call never runs and the screen stays on the pick");
    Check(g_world.nBaseCount == nBase1 && g_world.nFunds == nNewCost - 1, "with no base and no spend");

    /* The failed price leaves dwActionFail @ 0x1870C8 set, and 0x89578..0x89600 is the low-supply report: the
     * next geoscape release clears the flag and hands back without reaching any control -- screen 2 is not one
     * of the six screens that keep the shortfall (6, 9, 10, 11, 12). The pen pays for that: the press stays
     * down, its release is swallowed, and only the pair after it reaches the confirm control. */
    nSamples0 = g_nSamples;
    World_GeoscapePointer(&g_world, nRowX, nRowY, true);
    World_GeoscapePointerUp(&g_world);
    CheckEqI32(g_world.nShortfall1870C8, 0, "the next release clears the shortfall (0x895C4)");
    CheckEqI32(g_nSamples, nSamples0, "and reaches no control at all: no sample (0x89578..0x89600)");
    CheckEqI32(g_world.screen.nGeoscapeScreen, 2, "with the screen left on the pick");
    CheckEqI32(g_world.ui.apCtl[GEOUI_CTL_186BA4]->nState, 1, "the control keeps the state its swallowed press set (0x1A42C)");

    g_world.nFunds = nFunds1;                          /* the gate opens with the funds back */
    nSamples0 = g_nSamples;
    World_GeoscapePointer(&g_world, nRowX, nRowY, true);
    CheckEqI32(g_world.bFacDone, 1, "the press arms new-base mode again (0x874E4)");
    (void)DrawFrame();                                 /* the pen stays down while the globe redraws */
    World_GeoscapePointerUp(&g_world);
    CheckEqI32(g_nSamples, nSamples0 + 1, "and the affordable click plays the same sample (0x61538)");
    CheckEqI32(g_world.nShortfall1870C8, 0, "the gate lets it through (0x6156C)");
    CheckEqI32(g_world.screen.nGeoscapeScreen, 7, "the no-pick arm writes the build-screen id (0x8B64C)");
    CheckEqI32(g_world.nSelectedBase, g_world.nBaseCount,
               "and seats the selected base on the base COUNT (0x8B638 / 0x8B644) -- the slot the new base takes");
    CheckEqI32(g_world.bFacDone, 1, "leaving the byte the press set: this arm does not clear it, and 0x59ACC reads it");
    CheckEqI32(g_world.bFacDialogOpen, 1, "BaseFacScreen_Init's tail call opens the dialog (0x5A27C)");
    Check(g_world.pFacPanel != NULL, "with a fresh panel for the overlay's row aux (0x58A44)");
    Check(g_world.nUnk15887C == 0 && g_world.nUnk158880 == 0, "Init clearing the pending pair (0x5A1AC / 0x5A1B4)");
    Check(g_world.ui.apCtl[GEOUI_CTL_186B00]->x == 224 && g_world.ui.apCtl[GEOUI_CTL_186B00]->y == 229,
          "with the close control at (224,229) (0x5A260 / 0x5A26C)");
    CheckEqI32(g_world.nFunds, nFunds1, "and reaching the build screen spends nothing");

    /* The open's new-base branch (0x58C18..0x58D74): Init cleared the arm a moment earlier (0x5A274) and the
     * Toggle then arms it again, stores the ROW COUNT into unk_158868 (0x58C88 -- one past the last line after
     * 0x58C68, i.e. the 17th row, whose aux the open wrote as 0 at 0x58C50), and wipes the whole 6 by 6 grid of
     * the slot the new base takes. The facility a new base starts with is therefore decided by the dialog's own
     * table, not by a row the pen picked. */
    CheckEqI32(g_world.bArmedPlacement, 1, "the open re-arms the placement in new-base mode (0x58C94, after 0x5A274)");
    CheckEqI32(g_world.nPanelSel, g_world.pFacPanel->nLineCount,
               "and stores the row count into unk_158868 (0x58C88): the 17th row the open wrote at 0x58C50, which "
               "the 0x58C68 cut just dropped from the count");
    CheckEqI32(g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase1
                                      + PUFO_GRID_FAC_TYPE_OFF + 10], PUFO_FAC_TYPE_NONE,
               "wiping the cell the pen is about to aim at, before anything is aimed (0x58C9C..0x58D74)");

    nKind  = UiPanel_GetLineAux(g_world.pFacPanel, g_world.nPanelSel);
    CheckEqI32(nKind, 0, "and that stored row's aux is 0, the 17th row the open writes (0x58C50)");
    nPhase = PUFO_PLA_TAP_COLOR_STEP * ((i32)g_nMapPhase + PUFO_PLA_TAP_COLOR_BASE);
    nCost1 = (i32)g_world.aFac[nKind].uCost;
    nCnt1  = (i32)g_world.aRegionBuildCount[nRegionIdx];

    /* Row 1, not the stored row: the click moves the panel's own selection, but the buy stores it into
     * unk_158868 only while the new-base byte is clear, so the overlay keeps the row the open stored. */
    nSel0   = g_world.nPanelSel;
    nRowSel = (g_world.pFacPanel->nLineCount > 1 && g_world.pFacPanel->nStyle > 1) ? 1 : 0;
    nRow2Y  = g_world.pFacPanel->nPosY + g_world.pFacPanel->nFont * nRowSel + g_world.pFacPanel->nFont / 2;
    World_GeoscapePointer(&g_world, g_world.pFacPanel->nPosX, nRow2Y, true);
    (void)DrawFrame();
    World_GeoscapePointerUp(&g_world);
    CheckEqI32(g_world.bArmedPlacement, 1, "the row click runs the panel's handler and the buy arms the placement (0x5897C)");
    CheckEqI32(g_world.nPanelSel, nSel0, "while the new-base byte is set the buy does NOT store the row (the ANDS at 0x5898C gates the store at 0x58998)");

    nAimX = pF->facOverlay.nOriginX + 4 * PUFO_PLA_TAP_CELL;
    nAimY = pF->facOverlay.nOriginY + PUFO_PLA_TAP_Y0 + PUFO_PLA_TAP_CELL;
    World_GeoscapePointer(&g_world, nAimX, nAimY, true);
    (void)DrawFrame();
    CheckEqI32(g_world.nSmapTapX, 4, "the held press aims the cell under the cursor (0x58EC0)");
    CheckEqI32(g_world.nSmapTapY, 1, "on the row axis too (0x58EC8)");
    CheckEqI32(g_world.nFunds, nFunds1, "and with the pending pair empty it aims without committing");
    World_GeoscapePointerUp(&g_world);
    CheckEqI32(g_world.nUnk15887C, 4, "the release copies the tap into the pending pair (0x89E8C / 0x89E98)");

    nBase0 = g_world.nBaseCount;
    ClearSurface();
    World_GeoscapePointer(&g_world, nAimX, nAimY, true);
    (void)DrawFrame();
    CheckEqI32(SurfPix(nAimX, nAimY), PackInk(0, 0, nPhase), "the new-base preview scores non-zero on an empty "
                                                             "footprint: 0x5957C / 0x59588 force it, so the phase "
                                                             "colour sits in the low byte");
    CheckEqI32(g_world.nFunds, nFunds1 - nCost1 - nNewCost,
               "and the commit pays the record's cost (0x59AC8) plus the region's new-base cost (0x59B18)");
    Check(g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase1
                                 + PUFO_GRID_FAC_TYPE_OFF + 6 * 1 + 4] != PUFO_FAC_TYPE_NONE,
          "the aimed cell is no longer the NONE the open wiped there");
    CheckEqI32(g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase1
                                      + PUFO_GRID_FAC_TYPE_OFF + 6 * 1 + 4], (i32)g_world.aFac[nKind].bFlag21,
               "and takes the kind of the row the open stored (0x59A9C)");
    Check(g_world.nBaseCount == nBase0 + 1, "Base_CreateNamedEntry (0x59AFC) raises the base count 0x159D80");
    CheckEqI32(g_world.nCreateNextSiteId, nSiteId1 + 1, "and the next site id dword_186558");
    CheckEqI32((i32)g_world.aSiteRecs[nBase1].bType, nRegionIdx, "the new site record's bType is region - 0x80");
    Check(g_world.aSiteRecs[nBase1].abName[0] != 0, "with the label BaseFac_FormatCostLabel built (0x59AE4) as its name");
    CheckEqI32((i32)g_world.aRegionBuildCount[nRegionIdx], nCnt1 + 1, "and that label's region counter up by one (0x6A49C)");
    Check(g_world.apBases[nBase1] != NULL && g_world.apBases[nBase1]->bRegionId == nRegion
              && g_world.apBases[nBase1]->nUnk8 == g_world.nPlaceX
              && g_world.apBases[nBase1]->nUnk12 == g_world.nPlaceY,
          "the new base record carries the region and the hover's pick coordinates (0x66A38 / 0x66A50)");
    CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "and the commit lands the geoscape on the placement screen (0x59B2C)");
    Check(g_world.bFacDone == 0 && g_world.bRoot34 == 0 && g_world.bRoot35 == 0,
          "clearing the new-base byte (0x59B30) and the two preview bytes (0x59B28, 0x59B34)");
    CheckEqI32(g_world.bFacDialogOpen, 0, "BaseFacScreen_Toggle closes the dialog (the call at 0x59B1C)");
    Check(g_world.pFacPanel == NULL, "and frees its panel");
    CheckEqI32(g_world.nShortfall1870C8, 0, "with no shortfall left behind (0x1870C8)");

    /* 12f. The same purchase reached the way a pen reaches it: press, frame, release. No flow byte is written
     * by hand here -- only the two coordinates the WM_LBUTTONDOWN / WM_LBUTTONUP paths carry, and the funds the
     * purchase has to be affordable against. World_GeoscapePointer @ 0x872d0 and World_GeoscapePointerUp @ 0x89450
     * are the shell's own seams, and the frame between them is the render timer's call. */
    g_world.nFunds = 2000000;
    nFunds1 = g_world.nFunds;
    g_world.nav.apNav[4]->pfnClick();                /* BaseFacScreen_Open @ 0x61844 again, after 12e closed it */
    CheckEqI32(g_world.bFacDialogOpen, 1, "the dialog re-opens, and its toggle allocates a fresh panel (0x58A44)");
    Check(g_world.pFacPanel != NULL, "the reopened dialog's panel is allocated (0x58A44)");

    nRowX = g_world.pFacPanel->nPosX;                /* the dialog panel's own geometry: row 0 is nPosY..+16 */
    nRowY = g_world.pFacPanel->nPosY + 8;
    World_GeoscapePointer(&g_world, nRowX, nRowY, true);
    /* The follow is asserted at the dispatch, not before it: the original re-reads 0x15886C at the
     * dereference itself (0x87F60), and the port's press seam re-reads it at entry. Checking between
     * the open and the first dispatch only measured the allocator's reuse of the freed block. */
    Check(g_world.ptrEnv.pFacPanel == g_world.pFacPanel,
          "the press seam re-reads the panel pointer before dispatch, so the dispatcher's copy follows at the dereference, as the original's live read does (0x87620 -> 0x87F60)");
    CheckEqI32(g_world.bRoot34, 1, "a press on the build screen sets bBaseFacPreviewActive @ 0x18652C (0x87F44)");
    Check(g_world.pFacPanel->bFlag7073 == 1, "and re-arms the panel while the placement is unarmed (0x87F60)");
    (void)DrawFrame();
    CheckEqI32(g_world.nFunds, nFunds1, "the row press spends nothing");
    World_GeoscapePointerUp(&g_world);
    CheckEqI32(g_world.bArmedPlacement, 1, "the release's row click runs the panel's own handler and the buy arms "
                                           "the placement: unk_158888 at 0x5897C");
    CheckEqI32(g_world.nPanelSel, 0, "and stores the selected row while the new-base byte is clear (the store at 0x58998)");
    CheckEqI32(g_world.nFunds, nFunds1, "the arm spends nothing either: only the commit pays");
    nKind = UiPanel_GetLineAux(g_world.pFacPanel, g_world.nPanelSel); /* the overlay reads unk_158868, now row 0 */

    nAimX = pF->facOverlay.nOriginX + 3 * PUFO_PLA_TAP_CELL;     /* tap column 3 */
    nAimY = pF->facOverlay.nOriginY + PUFO_PLA_TAP_Y0 + PUFO_PLA_TAP_CELL; /* tap row 1, next to the neighbour 12e seeded */
    World_GeoscapePointer(&g_world, nAimX, nAimY, true);
    (void)DrawFrame();
    CheckEqI32(g_world.nSmapTapX, 3, "the held press aims at the cell under the cursor and writes the tap back (0x58EC0)");
    CheckEqI32(g_world.nSmapTapY, 1, "on the row axis too (0x58EC8)");
    CheckEqI32(g_world.nFunds, nFunds1, "with the pending pair elsewhere the draw aims but does not commit");

    World_GeoscapePointerUp(&g_world);
    CheckEqI32(g_world.nUnk15887C, 3, "the release copies the tap into the pending pair (0x89E8C)");
    CheckEqI32(g_world.nUnk158880, 1, "on both axes (0x89E98)");
    CheckEqI32(g_world.bRoot35, 1, "and sets bBaseFacPreviewOk @ 0x18652D (0x89E84)");
    CheckEqI32(g_world.bRoot34, 0, "while clearing the aimed byte (0x89E14)");
    CheckEqI32(g_world.nPanelSel, 0, "and armed, so its row click is skipped and the selection stands (0x89F10)");

    nBase0 = g_world.nBaseCount;
    ClearSurface();
    World_GeoscapePointer(&g_world, nAimX, nAimY, true);
    (void)DrawFrame();
    CheckEqI32(SurfPix(nAimX, nAimY), PackInk(0, 0, nPhase), "the second press draws the aimed outline in the phase "
                                                             "colour at origin + 32 * tap, where the cursor is");
    CheckEqI32(g_world.nFunds, nFunds1 - (i32)g_world.aFac[nKind].uCost,
               "and commits: the record's own cost off nFunds (0x59AC8), with no new-base cost and no base");
    CheckEqI32(g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * g_world.nSelectedBase
                                      + PUFO_GRID_FAC_TYPE_OFF + 6 * 1 + 3], (i32)g_world.aFac[nKind].bFlag21,
               "the aimed cell takes the record's kind (0x59A9C)");
    Check(g_world.nBaseCount == nBase0 && g_world.bFacDone == 0,
          "the non-new-base commit creates nothing: no Base_CreateNamedEntry, no new-base byte");
    CheckEqI32(g_world.bFacDialogOpen, 1, "and leaves the dialog open for the next purchase");
    CheckEqI32(g_world.screen.nGeoscapeScreen, 7, "with the screen id left at 7 -- only the new-base path moves it (0x59B2C)");
    CheckEqI32(g_world.nUnk15887C, 3, "the pending pair is left as the cell that was built");
    CheckEqI32(g_world.nUnk158880, 1, "on both axes");

    /* The purchase leaves the dialog open and the arm clear is only a fixture for the soldier and close-chain
     * checks below; World_Free closes the dialog the way the shell closes any open one. */
    g_world.bArmedPlacement = 0;

    nOpen0 = g_world.bSolScreenOpen;
    g_world.nav.apNav[2]->pfnClick(); /* SoldierScreen_Toggle @ 0x64af0 */
    CheckEqI32(g_world.bSolScreenOpen, (nOpen0 != 0) ? 0 : 1, "nav slot 2 flips 0x159DE0 in either direction");
    CheckEqI32(g_world.screen.nGeoscapeScreen, 4, "and writes the id 4 either way (0x651C4)");
    g_world.bSolScreenOpen = 1;
    ClearSurface();
    (void)DrawFrame();
    Check(g_world.ui.apCtl[GEOUI_CTL_186B00]->x == 224 && g_world.ui.apCtl[GEOUI_CTL_186B00]->y == 1,
          "case 4 seats the close control at (224,1) (0x92DB0 MOV #0xE0, 0x92DC8 MOV #1)");
    CheckEqI32(g_nBlits, 3, "the two page buttons then the close control, in that order (0x92DE0..0x92E04)");

    nSamples0 = g_nSamples;
    g_world.nav.apFind[2]->pfnClick(); /* Ui_SetTransferFlag @ 0x619a8 */
    CheckEqI32(g_world.bTransferFlag, 1, "the third find button sets bTransferFlag @ 0x159E10 (0x619C0)");
    CheckEqI32(g_nSamples, nSamples0 + 1, "after Sound_Play(17, 4) (0x619B4)");

    /* 12g left the NEW base selected and it holds no soldiers, so the walk here runs against the starting base. */
    g_world.nSelectedBase = 0;
    g_world.nSoldierCursor = -1;
    g_world.nav.apFind[0]->pfnClick(); /* SoldierScreen_FindNextAtBase @ 0x6192c */
    CheckEqI32(g_world.nSoldierCursor, 0, "the next-at-base walk starts at cursor + 1 and stops on the base");
    g_world.nav.apFind[1]->pfnClick(); /* SoldierScreen_FindPrevAtBase @ 0x618c4 */
    CheckEqI32(g_world.nSoldierCursor, 0,
               "and Prev from index 0 walks off the bottom but leaves the cursor where it was (0x618D4)");

    if (g_world.pSolPanelNames != NULL && g_world.pSolPanelCraft != NULL) {
        g_world.pSolPanelNames->nUnk3028 = 3;
        g_world.pSolPanelCraft->nUnk3028 = 3;
        g_world.nav.apPanel[0]->pfnClick(); /* SoldierScreen_PanelPrev @ 0x61874 */
        CheckEqI32(g_world.pSolPanelNames->nUnk3028, 2, "PanelPrev moves the names panel first");
        CheckEqI32(g_world.pSolPanelCraft->nUnk3028, 2, "then the craft panel, the pair at 0x61880/0x61890");
        g_world.nav.apPanel[1]->pfnClick(); /* SoldierScreen_PanelNext @ 0x6189c */
        CheckEqI32(g_world.pSolPanelCraft->nUnk3028, 2,
                   "and Next cannot move either: both are nStyle 13 over 8 lines, so the bound is negative");
    } else {
        Check(false, "the soldier screen's two list panels exist");
    }

    g_world.screen.nGeoscapeScreen = 1; /* back to the placement screen for the close chain below */
    g_world.bPlaceActive = 1;
    g_world.bPlaceFail   = 1; /* the demolish-failure panel's gate, 0x92848 */
    World_CloseGeoscape(&g_world);
    CheckEqI32(g_world.screen.nGeoscapeScreen, 0, "the placement close reaches the map (0x8BE94)");
    Check(g_world.bPlaceActive == 0 && g_world.bPlaceFail == 0,
          "and its two placement bytes, [2] before [1] (0x8BE9C, 0x8BEA4)");
    Check(g_world.nav.apNav[0] == NULL && g_world.nav.apNav[PUFO_NAV_CTLS - 1] == NULL
              && g_world.nav.apPanel[0] == NULL && g_world.nav.apFind[PUFO_NAV_FIND_CTLS - 1] == NULL,
          "case 1 frees the fifteen nav controls through the same leaf and nulls every slot "
          "(0x8BEA8, 0x8BEDC, 0x8BF0C)");

    nSec0 = g_world.geo.time.nTotalSec;
    for (i = 0; i < 3; ++i) {
        World_PulseGeoscape(&g_world);
    }
    CheckEqI32(g_world.geo.time.nTotalSec, nSec0 + 3 * (nTick + 1),
               "so at screen 0 the pulse advances the clock again (0x95510, 0x9562C)");

    /* 13. The base-attack seam, end to end: the assault branch fills the shell's record, the opener builds
     * the panel, the frame reveals it, and the OK click runs the close thunk and the teardown the dispatcher
     * runs after it. The mission advance is the same call 0x809B8 makes once an item's travel time is up. */
    {
        SpawnMissionCtx missionCtx;
        StrategItem *pUfo;
        UiControl *pConfirm = g_world.ui.apCtl[GEOUI_CTL_186BA4];
        i32 nListCount0 = PtrArray_GetCount(&g_world.strategList);
        i32 nBase0 = -1;

        /* Pin the CRT stream for the two runs below. The game seeds rand() from the clock (InitInstance
         * 0x9FD48), and the mission numbers the asserts read -- Tact_SpawnAliens 0x3A3E0 builds the squad as
         * (15 - RandU16(5)) + (3 - RandU16(3)) -- come straight off that stream. Run B's defence pair rolls
         * sit between the earlier fixture draws and Run A's mission pass, so without a pin the pinned
         * numbers here would name Run B's draws, not Run A's squad. srand(1) is the same LCG in both builds. */
        srand(1u);

        /* A base with no defence facility (kind 12..16) at all: the report then fires nothing, which is
         * what makes the two tails below decide the same way every run. */
        for (i32 b = 0; b < (i32)g_world.nBaseCount; ++b) {
            i32 nDef = 0;
            for (i32 c = 0; c < 36; ++c) {
                const i32 nKind = g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * b
                                                         + PUFO_GRID_FAC_TYPE_OFF + c];
                if (nKind >= 12 && nKind <= 16) { nDef += 1; }
            }
            if (nDef == 0) { nBase0 = b; break; }
        }
        Check(nBase0 >= 0, "at least one base has no defence facility, so the report's rolls are not needed");

        if (nBase0 >= 0) {
            /* Run B FIRST, while the geoscape UI is intact: after Run A's hand-over the seam's
             * GeoscapeUi_FreeAll (0x83ABC) frees the controls, the font tables and the speed
             * controls, and only the unported mission-end unit would rebuild them -- Run B's close
             * dispatch reads the freed speed controls and faults. This tail is geoscape-mode
             * evidence, so it runs before the mission. Give the same class-8 attacker one defence
             * to fire, and the budget test is reached -- 0 < 1, so the craft is destroyed whatever
             * the roll says. */
            /* Update counts a defence as a cell that is FINISHED (its buildDays byte is 0) and of kind
             * 12..16, so an empty cell of the starting base -- kind 0, days 0, the sentinel the starting
             * base uses before any facility goes in -- is the one to arm. */
            i32 nDefCell = -1;
            for (i32 c = 0; c < 36; ++c) {
                if (g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase0
                                           + PUFO_GRID_FAC_TYPE_OFF + c] == 0
                    && g_world.abFacilityGrid[PUFO_GRID_DAYS_STRIDE * nBase0 + c] == 0) {
                    nDefCell = c;
                    break;
                }
            }
            if (nDefCell >= 0) {
                g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase0
                                       + PUFO_GRID_FAC_TYPE_OFF + nDefCell] = 16; /* fusion ball */
            }
            Check(nDefCell >= 0, "one empty cell of that base is armed with a fusion ball");

            /* The list positions are made to differ from the creation serials on purpose. The teardown hands
             * 0x158844 -- the item's +0x34 serial, taken from the counter at 0x18640C -- to
             * StrategList_RemoveAt as its nIndex (the IDB labels the load `nIndex`). The two agree only while
             * nothing has ever been removed, so one contact is removed first and the run then asks which slot
             * the teardown actually empties. */
            StrategItem *pFirst = StrategList_AddItem(&g_world.strategList, &g_world.nStrategSeed,
                                                      g_world.view.nMode, 1, 1, 8, 120, 160, 4240, 1);
            pUfo = StrategList_AddItem(&g_world.strategList, &g_world.nStrategSeed, g_world.view.nMode,
                                       1, 1, 8, 120, 160, 4243, 1);
            (void)StrategList_AddItem(&g_world.strategList, &g_world.nStrategSeed, g_world.view.nMode,
                                      1, 1, 8, 120, 160, 4244, 1);
            Check(pFirst != NULL && pUfo != NULL && FindStrategById(&g_world.strategList, 4244) >= 0,
                  "the fixture is three contacts: one to lose, the attacker, and its neighbour");
            g_world.bSpawnOnce = 0; /* no replacement roll out of this removal */
            (void)StrategList_RemoveAt(&g_world.strategList, &g_world.geo.world,
                                       FindStrategById(&g_world.strategList, 4240), &g_world.bSpawnOnce,
                                       &g_world.spawnCtx);
            CheckEqI32(FindStrategById(&g_world.strategList, 4243) + 1, pUfo->nSeed,
                       "after a removal the +0x34 serial is one past the attacker's index (0x82704, 0x82A84)");

            i32 nListCountB = PtrArray_GetCount(&g_world.strategList);
            if (pUfo != NULL) {
                pUfo->nMissionSub = 6;
                pUfo->nPhase      = 2;
                pUfo->nTimer      = 0;
                pUfo->nTargetIdx  = nBase0;
                pUfo->bRace       = 3;
                World_FillMissionCtx(&g_world, &missionCtx);
                (void)Geoscape_SpawnMissionSite(pUfo, -1, &missionCtx);
                CheckEqI32(g_world.bBaseAtMissionFollows, 0,
                           "with a defence firing, the spent budget takes the destroyed tail (0x58618 arm)");
                CheckEqI32(g_world.bBaseAtActive, 1, "and the report is built again");
                CheckEqI32(g_world.nBaseAtStrategIdx, pUfo->nSeed,
                           "the teardown's slot is the serial, not the index (0x7EF60, 0x158844)");

                if (pConfirm != NULL && pConfirm->pfnClick != NULL) {
                    pConfirm->pfnClick();
                }
                CheckEqI32(PtrArray_GetCount(&g_world.strategList), nListCountB - 1,
                           "the click runs StrategList_RemoveAt once (0x577D4)");
                Check(FindStrategById(&g_world.strategList, 4244) == -1
                          && FindStrategById(&g_world.strategList, 4243) >= 0,
                      "and it empties the slot the serial names -- the neighbour, not the contact. 0x158844 "
                      "carries a creation serial and RemoveAt wants an index; the original never reconciles them");
                CheckEqI32(g_world.geo.time.bPaused, 0, "and unpauses (0x577EC)");
                CheckEqI32(g_world.bBaseAtActive, 0, "and the teardown arm releases the panel");
                Check(g_world.pBaseAtPanel == NULL && g_world.pBaseAtPad == NULL,
                      "and both pointers with it");
            }
            /* The same gate tested the other way: Run B's destroyed tail leaves 0x158850 at 0, so
             * this teardown runs the release arm and the screen write but does NOT re-enter
             * Smap_BeginCraftSelect -- the Map header stands exactly as Run A left it. */
            Check(g_world.closeEnv.pbBaseAtAssault == &g_world.bBaseAtMissionFollows
                      && g_world.bBaseAtMissionFollows == 0,
                  "Run B's destroyed tail leaves the 0x8BB10 gate byte at 0 (0x58618 arm)");
            CheckEqI32(g_world.nMapMissionType, 0, "and no mission type is stamped: the 0x8BB10 gate closed, so the 0x8BB20 write and the 0x840FC stamp never ran");

            /* Fixture cleanup: the teardown just asserted emptied the wrong slot (0x158844 names a serial,
             * RemoveAt wants an index), so the attacker's own contact is still in the list. Run A's mission
             * must start from the fixture list it was asserted against, so remove it by the index FindStrateg
             * reports. */
            (void)StrategList_RemoveAt(&g_world.strategList, &g_world.geo.world,
                                       FindStrategById(&g_world.strategList, 4243), &g_world.bSpawnOnce,
                                       &g_world.spawnCtx);

            /* Put the armed cell back to the empty-cell sentinel: Run A's tile asserts read the
             * facility grid the same way Smap_InitCraftSelectState does, and they expect the grid
             * the fixture started with. */
            if (nDefCell >= 0) {
                g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase0
                                       + PUFO_GRID_FAC_TYPE_OFF + nDefCell] = 0;
            }
        }

        /* Run A's +1 assert below counts from the list as Run B's fixture left it. */
        nListCount0 = PtrArray_GetCount(&g_world.strategList);

        /* A class-8 attacker: g_anBaseAtBudget covers 0..7 only, so the budget is 0 -- but the budget test
         * sits INSIDE the defence-pair loop (0x57D7C and its three twins), so a base with nothing to fire
         * never reaches it and the landing tail runs anyway. Run B gives the same attacker one defence cell
         * to take the other arm. */
        pUfo = StrategList_AddItem(&g_world.strategList, &g_world.nStrategSeed, g_world.view.nMode,
                                   1, 1, 8, 120, 160, 4242, 1);
        Check(pUfo != NULL, "the attacking contact is in the strateg list");
        if (pUfo != NULL && nBase0 >= 0) {
            pUfo->nMissionSub = 6;         /* the assault subtype */
            pUfo->nPhase      = 2;         /* arrived, timer spent -- the gate at 0x7EF34 */
            pUfo->nTimer      = 0;
            pUfo->nTargetIdx  = nBase0;    /* +0x84: the base, and the mode handed to the opener */
            pUfo->bRace       = 2;

            /* Re-pin the stream for the mission pass. Run B's defence pair rolls sit between the top of this
             * block and here, and the squad Tact_InitMission builds (0x3A3E0: (15 - RandU16(5)) +
             * (3 - RandU16(3)), all of it into the hidden park for a base assault) and the cargo counts the
             * asserts below read come straight off that stream. With the pin the pinned numbers name Run A's
             * squad, not Run B's rolls. */
            srand(1u);
            World_FillMissionCtx(&g_world, &missionCtx);
            Check(missionCtx.hooks.pfnContactScreen != NULL, "the shell fills the assault hook");
            (void)Geoscape_SpawnMissionSite(pUfo, -1, &missionCtx);

            CheckEqI32(g_world.screen.nGeoscapeScreen, 20, "the branch writes the screen id 20 (0x7EF78)");
            CheckEqI32(g_world.geo.time.bPaused, 1, "and pauses the clock before it (0x7EF4C)");
            CheckEqI32(g_world.nBaseAtClass, 8, "the class byte comes from the item's +2 (0x7EF58)");
            CheckEqI32(g_world.nBaseAtStrategIdx, pUfo->nSeed,
                       "the strateg slot is the item's +0x34 seed, not its list index (0x7EF60)");
            CheckEqI32(g_world.nAssaultBaseRace, 2, "the race byte from +0x70 (0x7EF68)");
            CheckEqI32(g_world.nCraftHomeBase, nBase0, "and the base index into 0x158848 (0x7EF70)");
            CheckEqI32(g_world.bBaseAtActive, 1, "Update ran on the build arm (0x57838)");
            Check(g_world.pBaseAtPanel != NULL && g_world.pBaseAtPad != NULL,
                  "and allocated both the panel and the pad it reads back through globals (0x578E0, 0x57A38)");
            Check(g_world.nBaseAtLineLimit > 0 && g_world.pBaseAtPanel->nLineCount == 0,
                  "the row count is set and the reveal starts from the first row");
            CheckEqI32(g_world.bBaseAtMissionFollows, 1,
                       "and a zero budget still lands: with no defence firing, bBudgetGone is never set");
            Check(pConfirm != NULL && pConfirm->pfnClick != NULL,
                  "Open installed the close thunk on the shared confirm control (0x58900)");
            Check(pConfirm != NULL && pConfirm->y == 290 && pConfirm->x == 120 - ((i32)pConfirm->uWidth / 2),
                  "centred on x 120 at y 290 (0x588E4, 0x588F4)");

            /* The frame reveals one row per cooldown step, pacing through the shared radar counter. */
            ClearSurface();
            (void)DrawFrame();
            CheckEqI32(g_world.pBaseAtPanel != NULL ? g_world.pBaseAtPanel->nLineCount : -1, 1,
                       "case 20's fall-through shows the first row (0x58760..0x587CC)");
            CheckEqI32(pF->g.nTapCooldown, 255,
                       "and parks the cooldown for that row's aux 0 -- 255 - 0 (0x587A8)");
            CheckEqI32(g_world.bBaseAtConfirmShown, 0, "with the OK control still waiting");

            /* The OK click: the thunk the opener installed, not a hand-built call. These four cells
             * are the ones Smap_BeginCraftSelect's failure block (0x84158..0x841DC) would move, so
             * capturing them makes the UNCHANGED state after the click an assertion. */
            /* The new-game craft sit at base with cold weapons, so the visibility pass would only
             * restate that. Arming the byte here makes the park's 1 -> 0 at 0x8393C a transition. */
            g_world.apCraft[0]->bWeaponsHot = 1;
            i32 nFunding0 = g_world.economy.aZero44_49[5];
            i32 nRadar0   = g_world.nRadarPulseTimer;
            i32 nLines0   = g_world.msgLines.nLineCount;
            i32 nHot0     = (g_world.apCraft[0] != NULL) ? (i32)g_world.apCraft[0]->bWeaponsHot : -1;
            if (pConfirm != NULL && pConfirm->pfnClick != NULL) {
                pConfirm->pfnClick();
            }
            CheckEqI32(PtrArray_GetCount(&g_world.strategList), nListCount0 + 1,
                       "a contact with a mission behind it KEEPS its item (the 0x577C8 test fails)");
            CheckEqI32(g_world.geo.time.bPaused, 1, "and keeps the clock stopped (no 0x577EC)");
            CheckEqI32(g_world.bBaseAtActive, 0, "while the tail call's Open(-1) still runs the release arm (0x586B4)");
            Check(g_world.pBaseAtPanel == NULL && g_world.pBaseAtPad == NULL,
                  "which frees both and nulls the slots (0x586EC, 0x5870C)");
            CheckEqI32(g_world.bSpawnOnce, 1, "and arms the replacement-craft byte (0x58718)");
            CheckEqI32(g_world.screen.nGeoscapeScreen, 0, "the dispatcher leaves the geoscape map (0x8BB08)");

            /* The mission-follow branch is live now (0x8BB10): this click ran Smap_BeginCraftSelect
             * on the shell's own craft-select env (World_WireMissionSeams) over the Map mission
             * header the shell owns. The line list is empty, so GetFlag(0) reads 0 and no
             * flag-21/22/23 arm ran (0x83C60); the mode already read 1 because case 20 wrote it at
             * 0x8BB20, so the selection pass was skipped (0x83994) and the mode_gate fell into the
             * Xcom assault block. All eight soldiers are stationed at base 0 and aboard craft 0,
             * which the visibility pass just parked, so Base_CountSoldiersAssigned @ 0x66230 returns
             * 8 and this run takes the launch tail at 0x8413C -- the failure tail, with its funding
             * block, the flag-17 line and the demolish call, is covered by case 5 of the
             * test_geoscape.c fixture against a hand-built env. */
            Check(g_world.closeEnv.pbBaseAtAssault == &g_world.bBaseAtMissionFollows,
                  "the 0x8BB10 test now reads the shell's mission-follow byte");
            CheckEqI32(g_world.bMapMissionMode, 1, "Map_bMissionMode is written at 0x8BB20 and stays 1 through the assault arm (0x83AA0)");
            CheckEqI32(g_world.nMapMissionType, 3, "the assault arm stamps the Xcom base mission type (0x840FC)");
            Check(g_world.abMapSpriteSet[0] == 11 && g_world.abMapSpriteSet[1] == 20
                      && g_world.abMapSpriteSet[2] == 23 && g_world.abMapSpriteSet[3] == 23,
                  "the sprite set is the race-2 row of the 0x84054..0x840E4 table -- the assault base's race");
            Check(g_world.bMapUnk1511E == 0 && g_world.nMapMapLayers == 0
                      && g_world.nMapTerrainVariant == 0 && g_world.bMapUnk15115 == 0,
                  "unk_128AD6 and the geometry trio are zeroed by the assault block (0x840EC..0x84108)");
            CheckEqI32(g_world.nSelectedBase, nBase0, "nSelectedBaseIdx takes nCraftHomeBase (0x84130)");
            /* The tactical tile grid @ 0x159D5C, built from the grid THIS test prepared: its own
             * memset left base 0's TYPE lane at 0 -- the original new game stamps 0xFF there, settled
             * in the Part 44 follow-up -- and Smap_InitCraftSelectState skips only 0xFF (0x61C20),
             * so every finished cell takes the placement code of type 0, which is 0 (0x61C28); the
             * one cell with buildDays 16 contributes nothing and keeps 0x61BB4's 20 (0x61BE8). */
            {
                i32 bTiles = 1;
                for (i32 c = 0; c < 36; ++c) {
                    if (g_world.abMapTileTypes[c] != (c == 14 ? 20 : 0)) { bTiles = 0; }
                }
                Check(bTiles, "all 36 tiles are the type-0 placement code except cell (2,2), under construction, which keeps 20");
            }
            /* Two passes touch the weapons-hot byte inside one click. The visibility pass parks every
             * craft cold and at base (0x8393C writes 0, 0x83950 writes 1). The launch hand-over then
             * runs the cargo pass, whose step-5 loop (0x39108) walks the craft list and re-arms the
             * weapons of every craft sitting at its own home base for this mission (0x39138 writes 1).
             * The cold value is the middle of the chain, not the end of it. */
            Check(nHot0 == 1 && g_world.apCraft[0]->bWeaponsHot == 1 && g_world.apCraft[0]->bAtBase == 1
                      && (i32)g_world.bCraftCount == 3,
                  "the mission craft was hot, got parked by the visibility pass (0x8393C), and was re-armed by the cargo pass's at-home store (0x39138)");
            {
                i32 bReArm = 1;
                for (i32 c = 0; c < (i32)g_world.bCraftCount; ++c) {
                    i32 bHome = ((i32)g_world.apCraft[c]->bHomeBase == (i32)g_world.nCraftHomeBase)
                                    && g_world.apCraft[c]->bAtBase;
                    if ((i32)g_world.apCraft[c]->bWeaponsHot != bHome) { bReArm = 0; }
                }
                Check(bReArm, "every craft's hot byte equals the cargo loop's own keep condition (bHomeBase == mission base AND bAtBase), the test the 0x39108 loop uses before its 0x39138 store");
            }
            CheckEqI32(g_world.geo.time.bPaused, 1, "the clock stays stopped: the tail never reaches 0x841F0, and DismissSelected is skipped (0x83AA0)");
            Check(g_world.economy.aZero44_49[5] == nFunding0 && g_world.nRadarPulseTimer == nRadar0
                      && g_world.msgLines.nLineCount == nLines0,
                  "the failure block never ran: the funding dword, the radar timer and the line count stand (gate at 0x8413C, 8 assigned soldiers)");
            CheckEqI32(g_world.bMapSelectionFlag, 0, "the selection flag is cleared at 0x83AB4");
            CheckEqI32(g_world.bScratch187084, 0, "unk_187084 is untouched on this tail; its only writer is 0x841FC");
            Check(g_world.bMapTickHold == 0 && g_world.bMapBattleEnded == 0,
                  "the tick/battle pair stays clear: the zero-base tail (0x84214) is not reached with three bases standing");

            /* The hand-over is more than a flag write now: the shell ran Tact_InitMission
             * @ 0x3c550 and owns a live tactical frame. What follows pins what that call
             * leaves behind, pass by pass. */
            Check(g_world.bTactLive == 1 && g_world.pTactMap != NULL
                      && g_world.pTactMap->nMissionType == 3,
                  "the 0x83AC4 hand-over ran the mission bring-up and the frame's own header says assault (3)");
            Check(g_world.sMapSentinel138EA0.pObj == g_world.pAssets->apObjects[0]
                      && g_world.sMapSentinel138EA0.nRefId == 0x7D00,
                  "the 0x3CB40 sentinel seed: dword 0 takes apObjects[0] and the dword at ILP32 +0x14 takes 0x7D00");
            Check(g_world.sTactWeaponSel.pObj == g_world.pAssets->apObjects[1]
                      && g_world.sTactWeaponSel.bConst10 == 10
                      && g_world.sTactWeaponSel.uAmmoTypeId
                          == g_world.pAssets->apObjects[1]->anAmmoTypeId[0],
                  "the weapon-select record: apObjects[1], the constant 10, and that object's first ammo type");
            {
                i32 bPanels = 1;
                for (i32 c = 0; c < PUFO_TACTINIT_PANELS; ++c) {
                    if (g_world.tactEnv.apPanel[c] == NULL) { bPanels = 0; }
                }
                Check(bPanels, "Init_AllocPanels mallocs all five UiPanels itself, before hooking the ctor");
                Check(g_world.tactEnv.apPanel[0]->nLineCount == 14 && g_world.tactEnv.apPanel[1]->nLineCount == 14
                          && g_world.tactEnv.apPanel[2]->nLineCount == 14 && g_world.tactEnv.apPanel[3]->nLineCount == 14
                          && g_world.tactEnv.apPanel[4]->nLineCount == 28,
                      "the ctor got 14 lines for each message panel and 28 for the briefing panel (table 0x1568E8..0x1571EC)");
            }
            Check(g_world.bTactFlag == 1 && g_world.bMapUnk1139B9 == 1 && g_world.bMapPlayerInput == 1
                      && g_world.nMapUiSlot1 == 0 && g_world.bMapFire0 == 0 && g_world.bMapFire2 == 0
                      && g_world.nMapUiSlot9 == 0 && g_world.nMapUiSlot10 == 10 && g_world.nUiScreen == 1,
                  "the flag block: tactical flag and player input on, unk_1139B9 on because the Xcom roster branch ran (0x3D08C), the fire pair clear, screen 1 -- the team branch at 0x3D0B8 opened the inventory (TactHud_OpenInventory 0x16ef4, Part 55)");
            {
                const TactUiState *pUiOpen = &g_world.sTactUi;
                i32 bAllLit = (pUiOpen->bFilterPadAll == 1);
                for (i32 c = 0; c < PUFO_INV_FILTER_PADS; ++c) {
                    if (pUiOpen->abFilterPad[c] != 0) { bAllLit = 0; }
                }
                Check(bAllLit && pUiOpen->nInvFilterType == 0 && pUiOpen->nInvListSel == 0xFF
                          && pUiOpen->nUiScreen == 1 && pUiOpen->bActionLatch == 0,
                      "the inventory opener ran during bring-up: All filter lit, filter type 0, selection unset, staging screen 1 and latch 0 (0x16F14..0x16F40)");
            }
            Check(g_world.nMapUnk138E6C >= 10 && g_world.nMapUnk138E6C <= 50
                      && g_world.nMapUnk138E70 >= 0 && g_world.nMapUnk138E70 <= 20,
                  "the prologue random pair RandU16(0x28)+10 and RandU16(0x14): each inside its scaled range; the seed is the clock, so the exact pair is not pinned");
            Check(g_world.bMapDrawIsoLayer == 1 && g_world.bMapSweepArmed == 1,
                  "the tail sets the iso-layer byte (0x3D260) and the sweep-armed byte (0x3D268)");
            Check(g_world.bMapSpawnAlt == 0 && g_world.bMapAltOutcomePath == 0 && g_world.pTactBriefPad == NULL,
                  "the assault tail wrote bSpawnAlt 0 (0x83A6C), so the briefing gate (spawnAlt AND mode 0) takes its NULL branch and no pad is loaded");
            {
                /* Tact_LoadUiPads @ 0x3b068 ran through the shell hook (Part 53): 32 manual pads,
                 * 11 named singletons, 11 buttons, every slot filled from main.dat. */
                i32 nFilled = 0;
                for (i32 c = 0; c < PUFO_UIPAD_SLOT_COUNT; ++c) {
                    if (g_world.apTactPads[c] != NULL) { nFilled++; }
                }
                Check(nFilled == PUFO_UIPAD_SLOT_COUNT, "all 54 pad slots are filled: 32 manual, 11 named, 11 buttons (0x3b068)");
                i32 nShape = 1;
                for (i32 c = 0; c < PUFO_UIPAD_SLOT_COUNT; ++c) {
                    const PadImage *pPad = g_world.apTactPads[c];
                    if (pPad == NULL || pPad->pData == NULL || pPad->uWidth == 0 || pPad->uHeight == 0) { nShape = 0; }
                }
                Check(nShape, "every loaded pad carries its data block and non-zero LE dimensions (PadImage_Load 0x11020 reads u16 w/h first)");
                static const struct { i32 nSlot; u8 nMode; } aNamedMode[11] = {
                    { PUFO_UIPAD_HUD, 1 }, { PUFO_UIPAD_INV, 1 }, { PUFO_UIPAD_MARKER, 16 },
                    { PUFO_UIPAD_SOLDIER, 8 }, { PUFO_UIPAD_SELSOLD, 34 }, { PUFO_UIPAD_SBSOLD, 16 },
                    { PUFO_UIPAD_SOLD, 16 }, { PUFO_UIPAD_ENEMYSTEP, 9 }, { PUFO_UIPAD_ENDMISSION, 11 },
                    { PUFO_UIPAD_TWONONE, 15 }, { PUFO_UIPAD_MINICURSOR, 16 },
                };
                static const i32 aZeroOrigin[5] = { PUFO_UIPAD_INV, PUFO_UIPAD_SOLDIER, PUFO_UIPAD_MARKER,
                                                    PUFO_UIPAD_ENEMYSTEP, PUFO_UIPAD_ENDMISSION };
                i32 nMan = 1;
                for (i32 c = 0; c < 32; ++c) {
                    const PadImage *pPad = g_world.apTactPads[c];
                    if (pPad == NULL || pPad->nMode != 34 || pPad->nOriginX != 50 || pPad->nOriginY != 55) { nMan = 0; }   /* mode 34, origin forced 50,55 */
                }
                Check(nMan, "the 32 manual pads (Map+0x199C sprite set) have mode 34 and the forced origin 50,55");
                i32 nBtn = 1;
                for (i32 c = 32; c < 43; ++c) {
                    const PadImage *pPad = g_world.apTactPads[c];
                    if (pPad == NULL || pPad->nMode != 1 || pPad->nOriginX != 0 || pPad->nOriginY != 0) { nBtn = 0; }
                }
                Check(nBtn, "the eleven buttons b0..b10 (Map+0xCC) have mode 1 and origin zeroed");
                i32 nMode = 1;
                for (i32 c = 0; c < 11; ++c) {
                    const PadImage *pPad = g_world.apTactPads[aNamedMode[c].nSlot];
                    if (pPad == NULL || pPad->nMode != aNamedMode[c].nMode) { nMode = 0; }
                }
                Check(nMode, "the named pads keep their per-pad modes 1/1/16/8/34/16/16/9/11/15/16 in load order");
                i32 nZero = 1;
                for (i32 c = 0; c < 5; ++c) {
                    const PadImage *pPad = g_world.apTactPads[aZeroOrigin[c]];
                    if (pPad == NULL || pPad->nOriginX != 0 || pPad->nOriginY != 0) { nZero = 0; }
                }
                Check(nZero, "invv/soldier/mscanv/enemystp/endmiss take the ORIGIN_ZERO branch; the KEEP pads keep their ctor origins");
                const PadImage *pMini = g_world.apTactPads[PUFO_UIPAD_MINICURSOR];
                Check(pMini != NULL && pMini->nOriginX == (((i32)pMini->uWidth > 240 ? 241 - (i32)pMini->uWidth : 240 - (i32)pMini->uWidth) >> 1)
                          && pMini->nOriginY == (((i32)pMini->uHeight > 320 ? 321 - (i32)pMini->uHeight : 320 - (i32)pMini->uHeight) >> 1),
                      "minimcur takes the centring rule with the odd +1 branch (x = (240 - uWidth) >> 1, +1 only when the pad is wider than the field)");
            }
            {
                /* Tact_InitUiControls @ 0x3b498 ran through the shell hook (Part 54): the handlers are
                 * installed on the shell's bank in the g_aUiClickName order, the look control is built,
                 * the HUD origin is recomputed and the direct overrides land. */
                static const i32 aDeferred[6] = { 5, 17, 18, 56, 57, 58 };
                i32 bInstalled = 1, bDeferred = 1, bStart = 1;
                for (i32 c = 0; c < 113; ++c) {
                    i32 bWant = 1;
                    for (i32 d = 0; d < 6; ++d) {
                        if (aDeferred[d] == c) { bWant = 0; }
                    }
                    if (bWant && g_bank.aCtl[c].pfnClick == NULL) { bInstalled = 0; }
                    if (!bWant && g_bank.aCtl[c].pfnClick != NULL) { bDeferred = 0; }
                }
                for (i32 c = 113; c < 128; ++c) {
                    if (g_bank.aCtl[c].pfnClick != NULL) { bStart = 0; }
                }
                Check(bInstalled, "every handler the port models is installed on bank widgets 0..112");
                Check(bDeferred, "the six deferred families stay NULL: 5 end-turn, 17/18 hand menus, 56 deploy confirm, 57 quit, 58 resize (widget 4 is installed since Part 55)");
                Check(bStart && g_bank.aCtl[128].pfnClick != NULL && g_bank.aCtl[129].pfnClick != NULL,
                      "113..127 stay uninstalled (start-menu widgets) and 128/129 take MenuBtn_Cancel (0x1a24c / 0x1a26c)");
                const UiControl *pLook = &g_world.sTactLookCtl;
                Check(pLook->bEnabled == 0 && pLook->bUnk17 == 1 && pLook->pfnClick != NULL,
                      "the look control is ctor'd, left disabled, gets the ConfirmFireBurst handler and bUnk17 (0x3B4C4..0x3B4F4)");
                Check(pLook->apPads[0] != NULL && pLook->apPads[1] != NULL && pLook->apPads[2] == NULL,
                      "the look control's pads come from g_uiLookDesc: launch + claunch loaded, the empty third name loads nothing (0xA8138)");
                Check(g_world.sTactInput.pbScratchFlag9 == &g_world.sTactLookCtl.bEnabled,
                      "Input_OnConfirm's *unk_1571E4+9 scratch flag now points at the look control's bEnabled byte");
                const PadImage *pHudPad = g_world.apTactPads[PUFO_UIPAD_HUD];
                Check(pHudPad != NULL
                          && pHudPad->nOriginX == (((i32)pHudPad->uWidth > 240 ? 241 - (i32)pHudPad->uWidth : 240 - (i32)pHudPad->uWidth) >> 1)
                          && pHudPad->nOriginY == 320 - (i32)pHudPad->uHeight,
                      "the HUD pad origin is recomputed: x centred (odd +1 only past 240), y bottom-aligned with no +1 branch (0x3B500)");
                Check(g_bank.aCtl[16].uWidth == 100 && g_bank.aCtl[16].uHeight == 30
                          && g_bank.aCtl[17].uWidth == 34 && g_bank.aCtl[17].uHeight == 53
                          && g_bank.aCtl[18].uWidth == 34 && g_bank.aCtl[18].uHeight == 53,
                      "the mode-2 size overrides land: widget 16 at 100x30, the hand-menu widgets at 34x53");
                Check(g_bank.aCtl[14].bUnk17 == 1 && g_bank.aCtl[15].bUnk17 == 1 && g_bank.aCtl[31].bUnk17 == 1
                          && g_bank.aCtl[10].nState == 1,
                      "widgets 14/15/31 get bUnk17 and radio 0 starts selected (nState 1 on widget 10)");
            }
            {
                /* Live click dispatch on the installed bank (Part 54). Every fixture the dispatch
                 * moves is restored, so the walker and mission-end asserts below run on the values
                 * they expect. */
                GameMap *pMapX = g_world.pTactMap;
                StatusStack statusSave = pMapX->status;
                Soldier *pSelSave = pMapX->pSelUnit;
                u8 nLatchSave = pMapX->bActionLatch;
                u16 nUiSave = g_world.nUiScreen;
                i32 nCurXSave = g_world.tactEnv.cursor.nX;
                i32 nCurYSave = g_world.tactEnv.cursor.nY;
                u8  nFlagsSave = g_world.tactEnv.cursor.nFlags;
                i32 nScrXSave = g_world.tactEnv.focus.nCamScrollX;
                i32 nScrYSave = g_world.tactEnv.focus.nCamScrollY;
                CamOffset camSave = g_world.tactEnv.focus.camOffset;
                MenuPads padsSave = g_world.sTactMenuPads;
                u8 bScrollSave = g_options.bScrollByPen, bShowSave = g_options.bShowBorder;
                i32 nListSave = g_nListModeCell, nFmtSave = g_nSlotFormatCalls;
                i32 nSamplesSave = g_nSamples;
                i32 nSampleMark;
                /* Click through the installed pointer; a missing installation (a mutation that
                 * breaks InitUiControls' wiring) must turn the asserts red, not crash. */
#define CLICK(i) do { if (g_bank.aCtl[i].pfnClick != NULL) { g_bank.aCtl[i].pfnClick(); } } while (0)

                /* 59: samples 16 then 14, screen back to 0 (Map_PlaySamples16_14_ClearUiScreen 0x18e9c). */
                g_world.nUiScreen = 7;
                nSampleMark = g_nSamples;
                CLICK(59);
                Check(g_world.nUiScreen == 0 && g_nSamples >= nSampleMark + 2
                          && g_aFrameSamples[nSampleMark] == 16 && g_aFrameSamples[nSampleMark + 1] == 14,
                      "widget 59 plays 16, 14 and clears screen 7 back to 0 through the staging sync");

                /* 70: the scroll toggle with its SHOW coupling (Map_ToggleFlag128A8D 0x19250). */
                g_options.bScrollByPen = 0;
                g_options.bShowBorder = 1;
                CLICK(70);
                Check(g_options.bScrollByPen == 1 && g_options.bShowBorder == 1, "turning SCROLL on leaves SHOW alone");
                CLICK(70);
                Check(g_options.bScrollByPen == 0 && g_options.bShowBorder == 0
                          && g_world.sTactMenuPads.abPen[0] == 0 && g_world.sTactMenuPads.abPen[1] == 0,
                      "turning SCROLL off clears SHOW, and both the option bytes and the MenuPads flag pads write back");

                /* 99: the layer byte steps through the staging sync, with no MapCursor_SetFlags call (0x19670). */
                g_world.tactEnv.cursor.nFlags = 2;
                nSampleMark = g_nSamples;
                CLICK(99);
                Check(g_world.tactEnv.cursor.nFlags == 3 && g_aFrameSamples[nSampleMark] == 17,
                      "widget 99 plays 17 and steps the cursor layer 2 -> 3");

                /* 0: the roster cycle (Tact_CycleNextUnit 0x16684): select, focus, latch clear. */
                pMapX->pSelUnit = pMapX->apXcomUnits[0];
                pMapX->bActionLatch = 4;
                nSampleMark = g_nSamples;
                CLICK(0);
                Check(pMapX->pSelUnit == pMapX->apXcomUnits[1] && pMapX->bActionLatch == 0
                          && g_aFrameSamples[nSampleMark] == 17,
                      "widget 0 advances the selection 0 -> 1 (Tact_SelectUnit writes the live slot), clears Map+0x02 and plays 17");

                /* 9: focus recentres on the selected unit's cell (MapView_FocusSelected 0x16710). */
                g_world.tactEnv.focus.nCamScrollX = 12345;
                g_world.tactEnv.focus.nCamScrollY = 54321;
                CLICK(9);
                Check(g_world.tactEnv.focus.nCamScrollX != 12345 || g_world.tactEnv.focus.nCamScrollY != 54321,
                      "widget 9 recentres the camera scroll off the sentinel onto the selected unit's cell");

                /* 98: MenuBtn_Confirm(16) straight into Input_OnConfirm on the tact branch (0x19650). */
                g_world.nUiScreen = 7;
                g_nBankSamples = 0;
                CLICK(98);
                Check(g_world.nUiScreen == 0 && g_aBankSamples[0] == 16 && g_nBankSamples >= 2,
                      "widget 98 plays 16, then Input_OnConfirm's screen-7 arm plays 14 and closes the live screen cell");

                /* 4: the inventory opener, the same composition 0x3D0B8 ran at bring-up (0x16ef4). */
                g_world.nUiScreen = 0;
                g_world.sTactUi.bFilterPadAll = 0;
                g_world.sTactUi.abFilterPad[0] = 1;
                g_world.sTactUi.nInvFilterType = 3;
                g_world.sTactUi.nInvListSel = 4;
                pMapX->bActionLatch = 4;
                nSampleMark = g_nSamples;
                CLICK(4);
                Check(g_world.nUiScreen == 1 && g_world.sTactUi.bFilterPadAll == 1
                          && g_world.sTactUi.abFilterPad[0] == 0 && g_world.sTactUi.nInvFilterType == 0
                          && g_world.sTactUi.nInvListSel == 0xFF && pMapX->bActionLatch == 0
                          && g_aFrameSamples[nSampleMark] == 16,
                      "widget 4 re-opens the inventory: sample 16 (the sweep gate is up), All filter lit, filter type 0, selection unset, Map+0x02 cleared, live screen 1");

                /* 53: the load-slot list opener (Menu_OpenLoadSlots 0x18864): the load list is screen 11,
                 * the save list 10 (PUFO_UI_SCREEN_LOAD_SLOTS / _SAVE_SLOTS). */
                g_nListModeCell = 2;
                g_world.nUiScreen = 10;
                nSampleMark = g_nSamples;
                CLICK(53);
                Check(g_nSlotFormatCalls == nFmtSave + 1 && g_world.nUiScreen == 11 && g_nListModeCell == 0
                          && g_aFrameSamples[nSampleMark] == 16,
                      "widget 53 rebuilds the slot list, plays 16, clears *0x157188 and opens screen 11");

                /* 105: SaveLoad_Slot0 on the LOAD screen runs the load hook (0x19de4, 0x19E08). */
                g_world.nUiScreen = 11;
                CLICK(105);
                Check(g_nLoadSlotCalls >= 1 && g_nLoadSlotId == 0 && g_nListModeCell == 1 && g_world.nUiScreen == 11,
                      "widget 105 on screen 11 runs the load hook for slot 0 and sets the list mode to 1");

                /* Restore everything the dispatch moved. */
                pMapX->status = statusSave;
                pMapX->pSelUnit = pSelSave;
                pMapX->bActionLatch = nLatchSave;
                g_world.nUiScreen = nUiSave;
                g_world.tactEnv.cursor.nX = nCurXSave;
                g_world.tactEnv.cursor.nY = nCurYSave;
                g_world.tactEnv.cursor.nFlags = nFlagsSave;
                g_world.tactEnv.focus.nCamScrollX = nScrXSave;
                g_world.tactEnv.focus.nCamScrollY = nScrYSave;
                g_world.tactEnv.focus.camOffset = camSave;
                g_world.sTactMenuPads = padsSave;
                g_options.bScrollByPen = bScrollSave;
                g_options.bShowBorder = bShowSave;
                g_nListModeCell = nListSave;
                g_nSlotFormatCalls = nFmtSave;
                g_nSamples = nSamplesSave;
                g_nBankSamples = 0;
#undef CLICK
            }
            Check(g_world.apTactCells[0] != NULL && g_world.pTactDeployCell == g_world.apTactCells[0],
                  "the terrain pass filled the grid slots and the zero-layer cargo pass reads cell (0,0,0) (Init_LookupCell's index with every coordinate 0)");
            Check(g_world.pTactMap->nXcomCount == 8 && g_world.pTactMap->nEnemyCount == 0
                      && g_world.apTactHidden[0] != NULL && g_world.nMapEnemyReserveCount == 13,
                  "the rosters after the mission pass: 8 soldiers selected, 0 aliens on the field, 13 held in "
                  "the hidden park -- the kill seam (pfnKillUnit NULL) parks the stealth-flagged units instead "
                  "of killing them; 13 is the squad 0x3A3E0 draws from the stream pinned above");
            Check(g_world.nTactItemCount == 23,
                  "the cargo and belt passes built 23 TactItem records for this fixture's object table, "
                  "the same stream pin");
            Check(g_world.nMapDeployedHwps == 0,
                  "no heavy weapon was deployed on this fixture: the deploy pass ran and the count stays 0");
            CheckEqI32(g_world.nMapObjectCountU16, (i32)g_world.pAssets->nObjectCount,
                  "the object count cell mirrors the loaded table (149 objects in this main.dat)");
            Check(g_world.pTactShowShip != NULL && g_world.sTactLoft.pLoftemps != NULL,
                  "the showship record loaded from the open archive and the loft tables were built");
            Check(g_world.pTactMap->pSelUnit == g_world.pTactMap->apXcomUnits[0],
                  "the Xcom branch selected the first roster unit (0x3D0B4 SelectUnit; the inventory-open hook at 0x16ef4 is a NULL seam)");
            CheckEqI32((i32)g_world.gameClock.nSpeed, (i32)g_world.pOptions->nGameSpeed,
                  "the speed cell the tail timer reads is the copy World_NewGame made from the options");

            /* The original's hand-over order is 0x83AB4 (clear the selection flag), 0x83ABC
             * (GeoscapeUi_FreeAll with a1 = 0), 0x83AC4 (Tact_InitMission). The free clears the
             * mode byte at 0x86D34 -- that is what opens the 0xC350 tactical arm for the mission. */
            CheckEqI32(g_world.bGeoscapeUi, 0, "the seam's GeoscapeUi_FreeAll clears Map_bGeoscapeUi (0x86D34): the shell stands in tactical mode after the mission");
            /* The launch left one status message: the Xcom branch's Tact_SelectUnit pushed the
             * selected soldier's name onto the stack (Map+0x54 count, TTL at Map+0x1408C). */
            i32 nStatusCount0 = g_world.pTactMap->status.nCount;
            i32 nTtl0 = g_world.pTactMap->status.anTtl[0];
            Check(nStatusCount0 == 1 && nTtl0 == PUFO_STATUS_TTL
                      && g_world.pTactMap->status.aszMsg[0][0] != '\0',
                  "one status message with TTL 90: Tact_SelectUnit (0x3D0B4 branch) pushed the selected soldier's name");

            /* A 0xC350 firing on that live frame: gates 1..4 all pass (hold clear, mode clear
             * from the seam, slot 10 written by the init, tact flag from the prologue). The body
             * decays the TTL words (0x27588), finds nothing to shift (oldest 89 > 0 at 0x275A4),
             * runs GameTick_Step (no phase active) and lands on the 0x275E8 clear. */
            g_world.bMapFrameScratch = 1;
            Check(World_GameTickOnce(&g_world), "the 0xC350 handler takes the tactical arm on the live frame (0x27508 sees the cleared mode byte the seam wrote)");
            CheckEqI32(g_world.bMapFrameScratch, 0, "the tick ends at the 0x275E8 frame-scratch clear (unk_138E74 @ Map+0x254BC)");
            Check(g_world.pTactMap->status.nCount == 1 && g_world.pTactMap->status.anTtl[0] == PUFO_STATUS_TTL - 1,
                  "the tick decayed the TTL word by exactly one (0x27588) and shifted nothing: the oldest message is still above 0 (gate at 0x275A4)");
            Check(g_world.pTactState->nPhaseCounter == 0 && g_world.pTactMap->tact.bBusy == 0,
                  "nothing was queued or phased at the launch frame: the phase counter stands at 0, the context is not busy");

            /* The tactical walker (Part 50): pfnTickActiveUnit now drives the queue on the live
             * frame. A synthetic command-0 turn on the first Xcom unit -- the unit MapSelectUnit
             * already selected -- and three 0xC350 firings walk it: the move processor arms the
             * turn and the camera adopts the unit, the fire processor spends the turn's time units
             * one per tick, and the phase-over tick raises bFlag1 so the fall processor's tail
             * drains the queue. All state the walk leaves is restored before the mission end. */
            {
                GameMap *pMapW = g_world.pTactMap;
                TactState *pQW = g_world.pTactState;
                Soldier *pTurnUnit = pMapW->apXcomUnits[0];
                MapCell *pTurnCell = pTurnUnit->pCell;
                u8 nFacing0 = pTurnUnit->bFacing;
                u8 nFacing1 = (u8)((nFacing0 + 1u) & 7u);
                u16 uTu0 = pTurnUnit->uTu;
                i32 nFlags0 = pTurnCell->bFlags44;

                Check(pTurnCell != NULL && pTurnUnit->uTu >= 4,
                      "fixture: the walking unit stands on a cell and has at least 4 time units");
                /* Sentinels so the focus call is observable: the launch already left a centred
                 * scroll and a layer-1 cursor flag, which an unarmed assert would pass through. */
                g_world.tactEnv.focus.nCamScrollX += 9999;
                g_world.tactEnv.focus.nCamScrollY += 9999;
                g_world.tactEnv.cursor.nFlags = 0;
                pTurnCell->bFlags44 = (u8)(nFlags0 | 1u);   /* arm the camera-follow gate */
                pMapW->tact.bBusy = 1;
                memset(&pQW->aQueue[0], 0, sizeof pQW->aQueue[0]);
                pQW->aQueue[0].nCmd    = TACTCMD_TURN;
                pQW->aQueue[0].pUnit   = pTurnUnit;
                pQW->aQueue[0].nFacing = nFacing1;
                pQW->nQueueCursor = 0;
                pQW->nQueueLen    = 1;
                pQW->bQueueActive = 1;
                pQW->bFlag0 = 0;
                pQW->bFlag1 = 0;

                Check(World_GameTickOnce(&g_world),
                      "walk 1: the 0xC350 firing reaches the walker, which runs the move processor on the armed queue (0x541C4 takes the move arm because bFlag0 is clear)");
                Check(pQW->bFlag0 == 1 && pQW->bFlag1 == 0 && pQW->nPhaseMax == 1
                          && pQW->aQueue[0].nTurnDir == 1,
                      "MoveTurn armed the turn: bFlag0 raised, nPhaseMax = the one-step facing delta (Map_FacingDelta @ 0x276A0), turn direction positive for a +1 facing (Map_bFacingTurnPositive @ 0x27668)");
                Check(pMapW->tact.pTrackUnit == pTurnUnit,
                      "0x54574: a busy walk with no tracked unit adopts the action's own unit before the focus");
                {
                    CamOffset co;
                    MapView_SetCamOffset(&pMapW->geom, (i32)g_world.tactEnv.focus.nMapTileW >> 1,
                                         (i32)g_world.tactEnv.focus.nMapTileH >> 1, false, 0, &co);
                    i32 nA = pTurnCell->nX - (i32)(i8)co.bScratch40;
                    i32 nB = pTurnCell->nY - (i32)(i8)co.bScratch41;
                    Check(g_world.tactEnv.focus.nCamScrollX
                                  == Map_CenterOffsetX((i32)g_world.tactEnv.focus.nDimX, nA, nB)
                              && g_world.tactEnv.focus.nCamScrollY == Map_CenterOffsetY(nA, nB),
                          "the focus call is MapView_FocusOnCell's own arithmetic (0x2C758): cell minus the SetCamOffset scratch, through Map_CenterOffsetX/Y (0x26FCC/0x26FFC)");
                    Check(g_world.tactEnv.cursor.nFlags == (u8)(pTurnCell->nZ + 1),
                          "the focus's tail sets the cursor layer to the cell layer + 1 (MapCursor_SetFlags @ 0x26F78, Map+0x15138)");
                }
                CheckEqI32(pTurnUnit->uTu, (i32)uTu0,
                      "the move arm charges nothing: the TU spend belongs to the fire processor's turn arm");

                Check(World_GameTickOnce(&g_world), "walk 2: bFlag0 now routes the entry to the fire processor (0x54588)");
                Check(pTurnUnit->uTu == (u16)(uTu0 - 1) && pTurnUnit->bFacing == nFacing1,
                      "the turn arm spends 1 TU (0x50F14) and steps the facing by the turn direction -- the unit now faces nFacing + 1");
                Check(pMapW->tact.pTrackUnit == pTurnUnit,
                      "the tracked unit stands: the walker's re-run gate at 0x54814 refuses a second pass for it");

                Check(World_GameTickOnce(&g_world),
                      "walk 3: the phase is over (nPhaseTick 2 > nPhaseMax 1), Fire_EndPhase raises bFlag1 (0x52AC8) and the same walk runs the fall processor");
                Check(pQW->nQueueLen == 0 && pQW->bQueueActive == 0 && pQW->nQueueCursor == 1
                          && pQW->bFlag0 == 0,
                      "the fall tail (0x540E0..0x54128) cleared bFlag0, advanced the cursor past the only entry, and drained the queue");
                CheckEqI32(pTurnUnit->uTu, (i32)uTu0 - 1,
                      "the whole turn cost exactly nPhaseMax time units: one charged, the phase-over tick charges none");
                Check(pQW->bPhaseActive == 0 && pQW->nPhaseCounter == 0 && pMapW->tact.nPhase == 0,
                      "no phase machinery ran: the opportunity/phase pair never armed (GameTick_Step is gated on bPhaseActive, ProcessActionPhase had no phase to run)");

                /* Restore the idle-frame state the mission-end asserts expect. */
                pMapW->tact.bBusy = 0;
                pMapW->tact.pTrackUnit = NULL;
                pTurnCell->bFlags44 = (u8)nFlags0;
            }

            /* Modal bank (Part 51): the drop hook opens the bank's modal, the stored slot-16
             * callback ends it, and the 8/9 cancel arm runs UiModal_End before the screen write.
             * Run A is inside the mission here (tact flag set, queue drained by the walker, no
             * in-flight byte set), so the cancel gates 0x16208/0x16218/0x1626C all pass. */
            Check(g_world.sTactDropHooks.pfnModal != NULL,
                  "the drop ctx has the modal opener wired (the UiModal_Begin shape of the call at 0x123E0)");
            g_world.sModal.nActionPhaseSave112 = 77; /* the frame-end cell @ 0x128AA0: Begin clears it at 0x2644C */
            g_world.sTactDropHooks.pfnModal(160, 31, 1, "OK", &g_world);
            Check(g_world.sModal.bOpen, "the drop modal set the open byte (0x26454)");
            CheckEqI32(g_world.sModal.anRect12_14[0], 160, "bank slot 12 carries the dialog width (Begin a2 at 0x26458)");
            CheckEqI32(g_world.sModal.anRect12_14[1], 31,
                       "bank slot 13 carries the first text record, 31 = the not-enough-time line (Begin a3 at 0x26460)");
            CheckEqI32(g_world.sModal.anRect12_14[2], 1, "bank slot 14 carries the line count (Begin a4 at 0x26464)");
            Check(g_world.sModal.nActionPhaseSave112 == 0,
                  "Begin cleared anActionPhaseSave0[112] @ 0x128AA0 (0x2644C) -- the same cell the frame tail writes (0x944E0/0x9451C), one owner now");
            Check(g_world.sModal.pfn16 != NULL, "slot 16 stores the modal-end callback, the UiModal_EndThunk store at 0x123E0");
            if (g_world.sModal.pfn16 != NULL) {
                g_world.sModal.pfn16(); /* hover column 1 at 0x14B94: end, then the stored action -- here the stored action IS the end */
            }
            Check(!g_world.sModal.bOpen, "the stored modal-end clears the open byte (UiModal_End @ 0x2648C)");

            u16 nScreenSave = g_world.nUiScreen;
            g_world.sModal.bOpen = true;
            g_world.nUiScreen = 8;
            Check(World_MissionCancelEnd(&g_world),
                  "the cancel dispatch runs on screen 8 inside the mission");
            Check(!g_world.sModal.bOpen,
                  "the 8/9 arm runs UiModal_End before the screen write (0x162DC) -- the Part 49 write-only deviation is closed");
            CheckEqI32(g_world.nUiScreen, 0, "the 8/9 arm writes screen 0 (0x162E0)");
            g_world.sModal.bOpen = true;
            g_world.nUiScreen = 9;
            (void)World_MissionCancelEnd(&g_world);
            Check(!g_world.sModal.bOpen && g_world.nUiScreen == 0,
                  "screen 9 takes the same arm; OnCancel does NOT run the abort check -- that is the OnBack arm at 0x156C8");
            g_world.sModal.bOpen = false;
            g_world.nUiScreen = nScreenSave;

            /* Enemy phase (Part 52): phase 3 now dispatches Tact_EnemyAiTick on the live frame.
             * Plant two fake enemies: one under the TU floor (marked done and skipped, 0x4CC58),
             * one past the tick cap (skipped WITHOUT the counter advancing, 0x4CC34). Nothing
             * acts, so the tick runs its tail at 0x4D920: Tact_EndEnemyTurn (busy down, phase to
             * none, player input re-enabled, the invalid saved selection replaced by the first
             * Xcom unit and re-focused) and TactState_KickQueue on the empty queue. */
            {
                GameMap *pMapE = g_world.pTactMap;
                TactCtx272 *pT = &pMapE->tact;
                static Soldier aEnemyFix[2];

                memset(aEnemyFix, 0, sizeof(aEnemyFix));
                aEnemyFix[0].uTu     = 5;  /* under PUFO_ENEMYAI_TU_MIN (15) */
                aEnemyFix[1].nAiTick = PUFO_ENEMYAI_TICK_MAX + 1;
                pMapE->apEnemyUnits[0] = &aEnemyFix[0];
                pMapE->apEnemyUnits[1] = &aEnemyFix[1];
                pMapE->nEnemyCount = 2;

                pT->bBusy          = 1;
                pT->nPhase         = PUFO_PHASE_ENEMY_AI;
                pT->pSavedSelUnit  = NULL;
                pMapE->pSelUnit    = pMapE->apXcomUnits[1]; /* the re-select must land on unit 0 */
                g_world.bMapPlayerInput = 0;
                i32 nScrollX0 = g_world.tactEnv.focus.nCamScrollX;
                i32 nScrollY0 = g_world.tactEnv.focus.nCamScrollY;
                g_world.tactEnv.focus.nCamScrollX += 9999; /* sentinel: the focus must recompute */
                g_world.tactEnv.focus.nCamScrollY += 9999;

                Check(World_GameTickOnce(&g_world),
                      "the 0xC350 firing reaches the phase-3 arm (0x4D950 busy && phase 3 -> TurnHooks.pfnEnemyAiTick)");
                CheckEqI32(aEnemyFix[0].nAiTick, 1, "the low-TU enemy got its counter (0x4CC4C ++nAiTick)");
                CheckEqI32(aEnemyFix[0].bAiDone, 1, "and it is marked done for the rest of the mission (0x4CC58)");
                CheckEqI32(aEnemyFix[1].nAiTick, PUFO_ENEMYAI_TICK_MAX + 1,
                           "the over-cap enemy is skipped WITHOUT advancing (0x4CC34)");
                CheckEqI32(aEnemyFix[1].bAiDone, 0, "the over-cap enemy stays not-done");
                CheckEqI32(pT->bBusy, 0, "nothing acted, so the tail's EndEnemyTurn ran: bBusy down (0x4CB94)");
                CheckEqI32(pT->nPhase, PUFO_PHASE_NONE, "and the phase returns to none");
                CheckEqI32(g_world.bMapPlayerInput, 1, "EndEnemyTurn re-enables player input");
                Check(pT->pSavedSelUnit == pMapE->apXcomUnits[0],
                      "the invalid saved selection is replaced by Map_apXcomUnits[0]");
                Check(pMapE->pSelUnit == pMapE->apXcomUnits[0],
                      "and the select hook lands on it (Map_pSelUnit 0x128CEC)");
                Check(g_world.tactEnv.focus.nCamScrollX == nScrollX0
                          && g_world.tactEnv.focus.nCamScrollY == nScrollY0,
                      "the focus seam recomputed the same centered scroll the walker's focus produced for this unit's cell");
                Check(g_world.pTactState->nQueueLen == 0 && g_world.pTactState->bQueueActive == 0,
                      "KickQueue on the empty queue touches nothing (Part 41 gate)");

                /* Second tick: the done/over-cap units are skipped and the enemy turn ends again. */
                pT->bBusy  = 1;
                pT->nPhase = PUFO_PHASE_ENEMY_AI;
                Check(World_GameTickOnce(&g_world), "tick 2: the same dispatch runs again with both enemies skipped");
                CheckEqI32(aEnemyFix[0].nAiTick, 2, "the done enemy still gets its counter before the bAiDone skip");
                CheckEqI32(aEnemyFix[0].bAiDone, 1, "and stays done");
                CheckEqI32(aEnemyFix[1].nAiTick, PUFO_ENEMYAI_TICK_MAX + 1, "the over-cap counter never moves");
                CheckEqI32(pT->bBusy, 0, "the tail closed the enemy turn again");

                /* Restore the empty-roster idle frame the mission-end asserts expect. */
                pMapE->nEnemyCount   = 0;
                pMapE->apEnemyUnits[0] = NULL;
                pMapE->apEnemyUnits[1] = NULL;
                pT->bBusy  = 0;
                pT->nPhase = PUFO_PHASE_NONE;
            }

            /* Mission end (Part 49). Empty the hidden park and the field so the CheckBattleEnd head
             * finds no enemy side and takes the win arm (0x350F0); the enemy-turn byte is pre-set so
             * its clear at 0x3523C is a transition. No abort, so the lost-mark pass (0x3535C) marks
             * nobody, and with an empty field there are no loss, kill or loot lines to draw. */
            g_world.nMapEnemyReserveCount = 0;
            g_world.pTactMap->nEnemyCount = 0;
            g_world.bMapEnemyTurnActive = 1;
            i32 nFundBeforeEnd = g_world.economy.aZero44_49[5]; /* g_nFundPending @ 0x1865BC */
            i32 nSiteBeforeEnd = g_world.nSiteCount;            /* nSmapSiteCount @ 0x186410 */
            Check(World_MissionEndCheck(&g_world, 0),
                  "the battle-end check takes the win arm: reserve and field are empty for an Xcom-base mission (gate 0x350E8, write 0x350F0)");
            Check(g_world.bMapMissionVictory == 1 && g_world.nMapMissionOutcome == 0
                      && g_world.bMapTickHold == 1 && g_world.bMapBattleEnded == 1,
                  "the head's four writes land: victory 1, outcome 0 (the dead store @ Map+0x1A1C), tick hold 1, battle ended 1");
            Check(g_world.nUiScreen == 13 && g_world.nMapUiSlot1 == 10
                      && g_world.pTactMap->tact.bBusy == 0 && g_world.bMapEnemyTurnActive == 0,
                  "the debrief setup: screen 13 (0x351EC), banner fade counter 10 (0x351FC), the context busy flag down (0x35228), the enemy-turn byte cleared (0x3523C)");
            Check(strcmp(g_world.aMapMissionCaption,
                         (const char *)g_world.pAssets->pTextRecs
                             + PUFO_TEXT_REC_STRIDE * PUFO_DEBRIEF_TEXT_VICTORY)
                      == 0,
                  "the caption is TEXTS[160] copied into Map_szMissionCaption (0x353C4): the victory text, straight from main.dat's record block");
            Check(strcmp(g_world.aMapMissionScore, "0") == 0,
                  "the score text is the itoa of a zero debrief sum (0x3657C): nothing to print for an empty field");
            Check(g_world.nMapDebriefLine == 0 && g_world.tactEnv.apPanel[0] != NULL
                      && g_world.tactEnv.apPanel[0]->nStyle == 0 && g_world.tactEnv.apPanel[1]->nStyle == 0,
                  "zero debrief lines: the cursor stays 0 and both debrief panels' nStyle take it (0x36514, 0x36524)");
            Check(g_world.economy.aZero44_49[5] == nFundBeforeEnd,
                  "g_nFundPending is unchanged: the running score was 0 (0x365B0)");
            Check(g_world.nSiteCount == nSiteBeforeEnd,
                  "no site commit: the 0x364A0 arm needs a credited remainder and none exists");
            Check(g_world.bSpawnOnce == 1,
                  "the replacement-spawn gate is set to 1 unconditionally (0x36584)");
            {
                i32 bNoneMarked = 1;
                for (i32 s = 0; s < g_world.nSoldierCount; ++s) {
                    if (g_world.apSoldiers[s] != NULL
                        && (g_world.apSoldiers[s]->bFlags & PUFO_SOLDIER_FLAG_LOST) != 0) {
                        bNoneMarked = 0;
                    }
                }
                Check(bNoneMarked,
                      "no soldier carries the lost mark: a won mission with no alternate path marks nobody (0x3535C)");
            }
            Check(g_world.bMapPromotionReport == 0 && g_world.tactEnv.apPanel[2]->nLineCount == 0
                      && g_world.tactEnv.apPanel[3]->nLineCount == 0,
                  "the promotion report stays empty: flag 0, both promotion panels zero lines (0x352E0, 0x352F0)");

            /* The cancel the debrief's cancel button reaches (UiScreen_OnCancel 0x162BC / 0x162C8):
             * teardown keeping nothing, then Geoscape_InitUI mode 1 -- the rebuild of everything the
             * launch seam freed. */
            Check(World_MissionCancelEnd(&g_world),
                  "the cancel arm runs for screen 13 with no promotion report (0x1627C, 0x162B8 not taken)");
            Check(g_world.bTactFlag == 0 && g_world.bTactLive == 0,
                  "the teardown mirror clears Map+0x00 (Tact_Shutdown's first store, 0x3D574) and drops the live frame");
            Check(g_world.bGeoscapeUi == 1,
                  "the rebuild writes the geoscape flag back to 1 (Geoscape_InitUI 0x85858) -- the mode-1 arm at 0x162C8, no new-game branch");
            Check(g_world.ui.apCtl[GEOUI_CTL_186BA4] != NULL && g_world.ui.apCtl[GEOUI_CTL_186BA4] != pConfirm,
                  "the control singletons are new objects: InitUI reallocated the very control Run B's click used");
            CheckEqI32(g_world.nUiScreen, 13,
                       "and Map_nUiScreen stays 13: only the 8/9 and 0x14/0x15 arms write it in UiScreen_OnCancel");

        }
    }

    /* The teardown the shell owns ran through the mission-end cancel: World_EndHookShutdown is the
     * shell's stand-in for Tact_Shutdown @ 0x3d568 (Part 47's choice), and the cancel arm called it.
     * What is asserted here is that its releases held. */
    Check(g_world.bTactLive == 0 && g_world.pTactMap == NULL && g_world.pTactState == NULL
              && g_world.pTactShowShip == NULL,
          "the cancel's teardown dropped the live flag and freed the two heap records plus the showship block");
    {
        i32 bClear = 1;
        for (i32 c = 0; c < (i32)(sizeof(g_world.apTactCells) / sizeof(g_world.apTactCells[0])); ++c) {
            if (g_world.apTactCells[c] != NULL) { bClear = 0; }
        }
        for (i32 c = 0; c < PUFO_TACTINIT_PANELS; ++c) {
            if (g_world.tactEnv.apPanel[c] != NULL) { bClear = 0; }
        }
        Check(bClear, "every grid slot and every panel pointer is cleared after the free");
    }
    Check(g_world.nTactItemCount == 0 && g_world.apTactItems[0] == NULL
              && g_world.sTactLoft.pLoftemps == NULL && g_world.apTactHidden[0] == NULL
              && g_world.nMapEnemyReserveCount == 0,
          "the item pool, the loft buffers and the hidden park are released and cleared; the soldier records stay owned by apSoldiers");
    {
        /* The pad table (Part 53): Tact_Shutdown frees the 32-slot sprite set (0x3E150), the eleven
         * Map+0xCC buttons and the named singletons (0x3E17C..0x3E318); the shell mirror frees every
         * slot the load thunk filled. */
        i32 bPadsClear = 1;
        for (i32 c = 0; c < PUFO_UIPAD_SLOT_COUNT; ++c) {
            if (g_world.apTactPads[c] != NULL) { bPadsClear = 0; }
        }
        Check(bPadsClear, "every tact pad slot is freed and cleared at teardown, as Tact_Shutdown frees them (0x3E150, 0x3E17C..0x3E318)");
    }
    {
        /* The tact widget pads (Part 54): Tact_Shutdown frees the pads of the thirteen tact families
         * between 0x3D6D8 and 0x3DC44 (bank widgets 0..112 plus 128/129) and the look control's at
         * 0x3DC98; the start-menu widgets 113..127 belong to StartUi_Shutdown, not to this teardown. */
        i32 bCtlPads = 1;
        for (i32 c = 0; c < 113; ++c) {
            if (g_bank.aCtl[c].apPads[0] != NULL || g_bank.aCtl[c].apPads[1] != NULL
                    || g_bank.aCtl[c].apPads[2] != NULL) { bCtlPads = 0; }
        }
        if (g_bank.aCtl[128].apPads[0] != NULL || g_bank.aCtl[128].apPads[1] != NULL
                || g_bank.aCtl[129].apPads[0] != NULL || g_bank.aCtl[129].apPads[1] != NULL) { bCtlPads = 0; }
        Check(bCtlPads, "every tact widget pad is freed and cleared at teardown (Tact_Shutdown 0x3D6D8..0x3DC44, and the port nulls what it frees)");
        Check(g_world.sTactLookCtl.apPads[0] == NULL && g_world.sTactLookCtl.apPads[1] == NULL
                  && g_world.sTactLookCtl.apPads[2] == NULL,
              "the look control's launch/claunch pads are freed too (0x3DC98)");
        Check(g_world.sTactInput.bTactFlag == false && g_world.sTactInput.pbScratchFlag9 == NULL,
              "the input-side teardown clears Map+0x00 (Tact_Shutdown's first store, 0x3D574) and releases the scratch flag");
    }

    World_Free(&g_world);
    Boot_Shutdown(&g_assets);

    printf("%s\n", g_nFailures == 0 ? "all geoscapeframe checks passed" : "FAILURES");
    return g_nFailures == 0 ? 0 : 1;
}
