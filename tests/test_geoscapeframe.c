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

static BootAssets     g_assets;
static PufoWorldState g_world;
/* The font is the port's one fontSys global (game/pufo_font.h), which InitUI fills at 0x860D4..0x86728. */
static PufoOptions    g_options;
static GfxColorEnv    g_color;
static u32            g_nUiSlot0;   /* UiControlHitCtx.pnUiSlot0, the Map+0x60 word */
static u8             g_nMapPhase;  /* nMapPhase @ 0x1139EF (Map + 0x37): the arm's cursor colour, and the
                                     * byte the original's 0xC352 handler wraps (PocketUFO_WndProc 0xA0988) */
static UiControlHitCtx g_hit;

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

static void RecSample(i32 nSample, void *pUser)
{
    (void)nSample;
    (void)pUser;
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
    (void)World_NewGame(&g_world, &g_assets, &g_fontSys, &g_options, &hooks);

    pF   = &g_world.frame;
    pSet = &g_world.sets[0];

    /* 1. Where the new game leaves the world, and what the wiring points at. */
    CheckEqI32(g_world.bGeoscapeUi, 1, "InitUI sets Map.bGeoscapeUi (unk_1139FC @ Map+0x44)");
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

    /* 5. The chain tail on screen 2, both arms of 0x91D98. Game_StartNew stores 1 into bPlacementPending
     * (0x945D4 MOV R6,#1 / 0x945E0 STRB), so the new game takes the BNE and never blits the close control.
     * With placement not pending the tail reaches 0x91DA8, and with the panel animation finished -- the
     * geoscape timer tick is what advances that progress in the running game, the test arms it at 100 --
     * the hover gets past 0x67490 into its scan stage. The panel is called on its own here so the map
     * ink cannot hide the scan stage's two 75 x 30 blocks at (160,200) and (5,200) (0x674BC/0x674E0). */
    {
        UiControl *pClose = g_world.ui.apCtl[GEOUI_CTL_186B00];
        const PadImage *pClosePad = (pClose != NULL) ? pClose->apPads[pClose->nState] : NULL;

        ClearSurface();
        g_nBlits = 0;
        Geoscape_DrawStatusPanel(&pF->panel);
        Check(pClose != NULL && !SawPadBlit(pClosePad),
              "bPlacementPending set (0x945E0): 0x91D98's BNE skips the close-control blit");

        g_world.bPlacementPending = 0;
        *pF->anim.pbStarted  = 1;
        *pF->anim.pnProgress = 100;
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
    g_world.nActionPhaseSave112 = 0;
    g_world.nListMode = PUFO_GEOFRAME_LIST_MODE_SAVING;
    (void)DrawFrame();
    CheckEqI32(g_world.nActionPhaseSave112, PUFO_GEOFRAME_PHASE_SAVE,
               "list mode 4 writes Map.anActionPhaseSave0[112] = 0x28 (0x944E0)");
    CheckEqI32(g_nPresents, 1, "and the frame still presents once (0x94538)");
    g_world.nActionPhaseSave112 = 0;
    g_world.nListMode = 0;
    (void)DrawFrame();
    CheckEqI32(g_world.nActionPhaseSave112, 0, "any other list mode leaves the cell alone");

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

    World_GeoscapePointerUp(&g_world);
    CheckEqI32(pF->g.bDragActive, 0, "the release clears the drag flag (0x896BC)");
    CheckEqI32(g_nSamples, 0, "screen 2 with an empty message line plays no sample on release");

    /* 8. The cursor pair really is 16-bit: a pen below the surface wraps the stored halfword. */
    World_GeoscapePointer(&g_world, 100, 33000, true);
    CheckEqI32(pF->g.nCursorY, (i32)(i16)33000, "0x872F8 stores the pen with STRH (0x872F8)");
    pF->g.nCursorY = 0;

    /* 9. The arms the wiring does not serve stay NULL, which is the arm not drawing at all. */
    Check(pF->draw.pPlaneArm == NULL
              && pF->draw.pResearchDraw == NULL && pF->draw.pBaseFacArm == NULL
              && pF->draw.pBuySellPad == NULL && pF->draw.pBaseAtEnv == NULL,
          "the arms the shell has no state for stay NULL (see pufo_geoscape.h)");
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

    World_Free(&g_world);
    Boot_Shutdown(&g_assets);

    printf("%s\n", g_nFailures == 0 ? "all geoscapeframe checks passed" : "FAILURES");
    return g_nFailures == 0 ? 0 : 1;
}
