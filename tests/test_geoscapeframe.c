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
#include "dat/pufo_dat.h"
#include "game/pufo_boot.h"
#include "game/pufo_clock.h"
#include "game/pufo_font.h"
#include "game/pufo_mapcell.h" /* MapCell_SetOccupant, the pair Unit_PlaceOnMap writes */
#include "game/pufo_options.h"
#include "game/pufo_plane.h"   /* InterceptMissionCtx, BasePlaneScreen_AddInterceptMission (Part 100.4) */
#include "game/pufo_save.h"
#include "game/pufo_tactend.h"
#include "game/pufo_tacthud.h"  /* PUFO_HUD_CONFIRM_FIRST and the promotion-report text records (Part 84) */
#include "game/pufo_terrain.h" /* PUFO_TERRAIN_CELLS_PER_LAYER, the shell's cell-table stride */
#include "game/pufo_tactctl.h"  /* g_aDeployCellOffsets / g_abDeployPointCounts, the deploy points (Part 66) */
#include "game/pufo_isodraw.h"  /* MapView_bResolveIsoCell, for the temporary iso probe */
#include "game/pufo_los.h"       /* Map_TraceLos, for the LOS probe */
#include "render/pufo_render.h"
#include "ui/pufo_control.h"
#include "ui/pufo_geoiinit.h"  /* g_anObjectPrice @ 0xAA2C0, the price the production commit charges (Part 99) */

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
    fflush(stdout); /* a crash later in the test must not swallow this line */
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
    fflush(stdout);
}

/* The screen pixel a map cell lands on -- the inverse of the arithmetic Smap_HitTestStrategSelect
 * resolves a click with (0x82C84 / 0x82CAC, one wrap per axis). The select test scans a 16x16 window
 * of cells around the click, so any pixel resolving to the cell is a valid press. */
static bool ScreenForCell(const SmapView *pView, i32 nCellX, i32 nCellY, i32 *pnX, i32 *pnY)
{
    for (i32 x = 0; x < PUFO_SURF_W; ++x) {
        i32 nCol = pView->nScrollX + pView->nMapW / 2 + x - 120;

        if (nCol < 0) {
            nCol += pView->nMapW; /* 0x82CB4, one wrap only */
        }
        if (nCol != nCellX) {
            continue;
        }
        for (i32 y = 0; y < PUFO_SURF_H; ++y) {
            i32 nRow = pView->nScrollY + pView->nMapH / 2 + y - 160;

            if (nRow < 0) {
                nRow += pView->nMapH; /* 0x82CC4 */
            }
            if (nRow == nCellY) {
                *pnX = x;
                *pnY = y;
                return true;
            }
        }
    }
    return false;
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

/* The original's empty facility cell is NOT a zero cell. Game_StartNew's own stamp (0x9485C..0x948EC) clears
 * the three tile layers and the whole buildDays lane but writes the 0xFF sentinel into the facType lane of all
 * eight base blocks (0x94908: MOV R11,#0xFF with STRB R11,[R0,#0x48] at 0x9491C). Every reader keys on that
 * sentinel: Base_CalcMonthlyUpkeep charges for a type byte that is not 0xFF, Smap_InitCraftSelectState skips
 * it and leaves the tactical tile at 20, and Geoscape_OnMapClick's cell clear writes exactly this state. A
 * plain memset leaves 0, which the tile builder reads as facility kind 0 -- placement code 0 -- and turns an
 * empty base into 35 copies of the xbase_0 ROOM module in the tactical map a later block builds. */
static void GeoFillEmptyGrid(void)
{
    for (i32 b = 0; b < PUFO_FAC_BASE_COUNT; ++b) {
        const i32 nFac = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * b;

        for (i32 c = 0; c < PUFO_GRID_CELL_COUNT; ++c) {
            g_world.abFacilityGrid[nFac + PUFO_GRID_TILEA_OFF + c]    = 0;   /* 0x94904 */
            g_world.abFacilityGrid[nFac + PUFO_GRID_TILEB_OFF + c]    = 0;   /* 0x94914 */
            g_world.abFacilityGrid[nFac + PUFO_GRID_LAYER2_OFF + c]   = 0;   /* 0x94920 */
            g_world.abFacilityGrid[nFac + PUFO_GRID_FAC_TYPE_OFF + c] = PUFO_FAC_TYPE_NONE; /* 0x9491C */
            g_world.abFacilityGrid[PUFO_GRID_DAYS_STRIDE * b + c]     = 0;   /* the days lane, 0x948F0 */
        }
    }
}

/* --- the tact bank bring-up fixtures (Part 54) -------------------------------------
 * The widget bank stands in for the app's g_bank (the WorldHooks field the world installs
 * handlers and pads onto), the input hooks are the ones the installed MenuBtn handler hands to
 * Input_OnConfirm @ 0x15f3c, and the slot hooks are the SaveLoad_Slot0..6 pair. Bank samples are
 * kept apart from g_nSamples so a tact click cannot disturb the frame asserts that pin it. */
static UiWidgetBank   g_bank;
static TactInputHooks g_tactInHooks;
static bool           g_bBankActionFlag;   /* UiBank_bActionFlag, WorldHooks.pbBankActionFlag */
static i32            g_nResizeCalls;      /* the WorldHooks.pfnResizeMainWindow seam (Part 61) */
static i32            g_nSaveRenders;      /* the save unit's frame seam (Part 62) */
static i32            g_aBankSamples[64];
static i32            g_nBankSamples;
static i32            g_nSlotFormatCalls;  /* Menu_OpenSlotList's injected SaveSlot_FormatList */

/* Fixture for MainMenu_pSlotCtx @ 0x156938: seven 40-byte slot labels. This test's SaveSlot_FormatList
 * seam only counts calls, so screens 10 and 11 draw these rows; they prove the 40-byte stride and the
 * pad placement, not the text a real save file would produce. */
static char g_aSlotLabelRows[PUFO_HUD_MODAL_LABELS][PUFO_HUD_MODAL_LABEL_STRIDE] = {
    "slot 0  01.03 12:00", "slot 1  02.03 12:00", "slot 2  03.03 12:00",
    "slot 3  04.03 12:00", "slot 4  05.03 12:00", "slot 5  06.03 12:00",
    "slot 6  07.03 12:00",
};
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

/* Input_OnConfirm's screen-1 arm reaches Inv_CloseAndCommit @ 0x15a48 only through this hook; the
 * app's twin is App_HookInvCloseAndCommit (pufo_app.c:616) and the callee is what deploys the team
 * (Part 66), so the tact part of this test has to install it the same way. */
static void Rec_InvCloseAndCommit(void *pUser)
{
    (void)pUser;
    World_InvCloseAndCommit(&g_world);
}

/* The keyboard arms' seams (Part 67): the same install set pufo_app.c applies, each counting its own
 * call so a chain assert can say which seam ran. The bodies are the world's wrappers, i.e. the very
 * functions the shell's thunks call. pfnSaveOptions stays NULL here on purpose -- the app installs
 * Options_SaveToRegistry @ 0x114d8, which writes the real registry, and this suite keeps its hands off
 * it; the arm is gated by its own IDB evidence in test_input. pfnShutdownReinit (Tact_Shutdown(0) +
 * Geoscape_InitUI(1), which the app installs as App_HookTactEscapeGeoscape) is not installed here at
 * all: it tears the mission down, and the blocks after this one run on the mission the fixture built. */
typedef struct SeamTrace {
    i32 nMapClicks;
    i32 nSelNext;
    i32 nSelPrev;
    i32 nScrollUp;
    i32 nScrollDown;
    i32 nConfirm;
    i32 nCursorChanged;
    i32 nSetCursor;
    i32 nPushText;
} SeamTrace;

static SeamTrace g_seams;

static void Rec_TactMapClick(void *pUser)      { (void)pUser; ++g_seams.nMapClicks;    World_TactMapClick(); }
static void Rec_TactSelNext(void *pUser)       { (void)pUser; ++g_seams.nSelNext;      World_TactSelectNextUnit(); }
static void Rec_TactSelPrev(void *pUser)       { (void)pUser; ++g_seams.nSelPrev;      World_TactSelectPrevUnit(); }
static void Rec_TactScrollUp(void *pUser)      { (void)pUser; ++g_seams.nScrollUp;     World_TactInvScrollUp(); }
static void Rec_TactScrollDown(void *pUser)    { (void)pUser; ++g_seams.nScrollDown;   World_TactInvScrollDown(); }
static void Rec_TactOnConfirm(u16 nScreen, void *pUser)
{
    (void)nScreen;
    (void)pUser;
    ++g_seams.nConfirm;
    World_TactConfirmInPlace();
}
static void Rec_TactSetCursorPos(i32 nX, i32 nY, void *pUser)
{
    (void)pUser;
    ++g_seams.nSetCursor;
    World_TactSetCursorPos(nX, nY);
}
static void Rec_TactPushText(i32 nRecIndex, void *pUser)
{
    (void)pUser;
    ++g_seams.nPushText;
    World_TactPushTextRecord(nRecIndex);
}
static void Rec_CursorChanged(void *pUser)     { (void)pUser; ++g_seams.nCursorChanged; } /* nullsub_3 @ 0x22b50 */

static i32 RecLoadGame(i32 nSlot, void *pUser)
{
    (void)pUser;
    ++g_nLoadSlotCalls;
    g_nLoadSlotId = nSlot;
    return 1;                                   /* "loaded": the list mode stays 1 (0x19E08 arm) */
}

/* The save arm stays a stub here: this fixture presses the LOAD screen's SaveLoad_Slot0 (widget 105),
 * not the save screen's, so the shell's save unit is gated directly in the mission block below
 * (Part 62); App_HookSaveGame is the app's own half of that arm. */
static const SaveLoadSlotHooks g_slotHooks = { NULL, RecLoadGame, NULL };

static void RecResizeMainWindow(void *pUser)
{
    (void)pUser;
    ++g_nResizeCalls;
}

/* The save unit's frame seam (SaveGame renders before it opens the file, 0x4499C). */
static void RecSaveRenderFrame(void *pUser)
{
    (void)pUser;
    ++g_nSaveRenders;
}

static void RecFormatSlots(void *pUser)
{
    (void)pUser;
    ++g_nSlotFormatCalls;
}

/* The shell's blit seam, recorded so the frame's control blits are checkable. */
static const PadImage *g_apBlitPad[64];
static i32            g_nBlits;

/* The mission exactly as the Part 62 save unit wrote it; the Part 65 load-back compares against these. */
static i32            g_nMarkXcom, g_nMarkEnemy, g_nMarkCiv, g_nMarkHidden, g_nMarkItems;
static i32            g_nMarkDimX, g_nMarkDimY, g_nMarkLayers;
static i32            g_nMarkCellX, g_nMarkCellY, g_nMarkCellZ;
static Soldier       *g_pMarkUnit;
/* Part 103.3: the selected soldier's cell as the slot-63 snapshot wrote it -- the patched-dims load at
 * the tail of main has to put an occupant back into (or, past row 50 of a 5-row walk, find nothing at)
 * that slot. Coordinates only: soldier records move between missions, their addresses must not be chased. */
static i32            g_nSnap63X, g_nSnap63Y, g_nSnap63Z;

/* Part 103.3 fixture: patch the "map" chunk's two dim bytes in place. On-disk DatFile: u32 count at
 * 0, then count x 21-byte directory entries (name[13], nDataSize u32, nFileOff u32), then the payloads;
 * DatFile_BeginChunk upper-cases the tag, so the map chunk's entry name is "MAP" and the chunk payload
 * starts with the two dim bytes (the read head at 0x43400/0x43418 takes them first). */
static i32 Test_PatchMapDims(const wchar_t *pwFile, u8 bNewX, u8 bNewY, u8 *pOldX, u8 *pOldY)
{
    HANDLE hFile = CreateFileW(pwFile, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    u32    nCount = 0;
    i32    bOk = 0;

    if (hFile == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (ReadFile(hFile, &nCount, 4, NULL, NULL) && nCount <= 256) {
        for (u32 i = 0; i < nCount; ++i) {
            char szName[13];
            u32  aTail[2];
            DWORD cb = 0;

            (void)SetFilePointer(hFile, (LONG)(4 + 21 * i), NULL, FILE_BEGIN);
            if (!ReadFile(hFile, szName, 13, &cb, NULL) || cb != 13) {
                break;
            }
            if (!ReadFile(hFile, aTail, 8, &cb, NULL) || cb != 8) {
                break;
            }
            if (szName[0] == 'M' && szName[1] == 'A' && szName[2] == 'P' && szName[3] == 0) {
                u8 abNew[2] = { bNewX, bNewY };

                (void)SetFilePointer(hFile, (LONG)aTail[1], NULL, FILE_BEGIN);
                if (ReadFile(hFile, pOldX, 1, &cb, NULL) && cb == 1
                    && ReadFile(hFile, pOldY, 1, &cb, NULL) && cb == 1) {
                    (void)SetFilePointer(hFile, (LONG)aTail[1], NULL, FILE_BEGIN);
                    bOk = WriteFile(hFile, abNew, 2, &cb, NULL) && cb == 2;
                }
                break;
            }
        }
    }
    (void)CloseHandle(hFile);
    return bOk;
}
/* Part 93: the strateg list as the save unit found it. Save_WriteStrateg bounds its record loop with the
 * count the geoscape block keeps at +0x38 -- 0x186530, written into the chunk at 0x3FC14 and read back as
 * the loop bound at 0x3FE28 and 0x401D4 -- and not with the PtrArray's own count; the original keeps that
 * cell equal to the list at every change (StrategList_AddItem 0x82710, StrategList_RemoveAt 0x82B34). The
 * Part 65 load-back compares the restored list against this mark. */
static i32            g_nMarkStrateg;
static u8             g_aMarkStratType[16];
static i32            g_aMarkStratX[16], g_aMarkStratY[16], g_aMarkStratId[16];
static u8             g_aMarkShipHot[PUFO_FAC_BASE_COUNT], g_aMarkShipBase[PUFO_FAC_BASE_COUNT];
static i32            g_aMarkShipIdx[PUFO_FAC_BASE_COUNT];
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

/* WorldHooks.pfnInitStartupUi's stand-in (Part 102): the startup tail callee of
 * OptionsScreen_ReturnToTitle (GameUI_InitStartup @ 0x38a28, the store at 0x6B490) belongs to the
 * menu owner -- the app answers it with App_InitStartupUi; this fixture answers it with a counter,
 * because the rebuild itself (banking the widgets, loading the fonts) is the app's bring-up. */
static i32 g_nStartupUiThunk;
static i32 Test_InitStartupThunk(void *pUser)
{
    (void)pUser;
    ++g_nStartupUiThunk;
    return 1;
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

/* Lit pixels in a surface box, ignoring any a snapshot already lights (Part 64): the debrief banner
 * blits the endmiss backdrop under its own text, so a box count with the backdrop's ink excluded is
 * the text's own contribution. The snapshot uses the 240-wide stride this test's surface has. */
static i32 InkInBox(i32 x0, i32 y0, i32 x1, i32 y1, const u16 *pExcl)
{
    i32 n = 0;

    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            if (SurfPix(x, y) == 0) { continue; }
            if (pExcl != NULL && pExcl[(size_t)y * 240u + (size_t)x] != 0) { continue; }
            ++n;
        }
    }
    return n;
}

/* Pixels in a surface box whose colour differs from a snapshot (Part 64): the score's one glyph lands
 * on ink the backdrop pad already lit, so its presence proves nothing -- its colour does. */
static i32 DiffInBox(i32 x0, i32 y0, i32 x1, i32 y1, const u16 *pRef)
{
    i32 n = 0;

    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            if (SurfPix(x, y) != (i32)pRef[(size_t)y * 240u + (size_t)x]) { ++n; }
        }
    }
    return n;
}

/* A control press through the pen: UiControl_HitTest measures the box only on the press, x .. x + uWidth - 1
 * and y .. y + uHeight - 1 (0x1A42C), and the release never consults it -- it fires whatever the press armed
 * (0x1A540). So the box centre is the coordinate to send, not a hand-picked offset. */
static void PressCtl(UiControl *pCtl)
{
    const i32 nX = pCtl->x + (i32)pCtl->uWidth / 2;
    const i32 nY = pCtl->y + (i32)pCtl->uHeight / 2;

    World_GeoscapePointer(&g_world, nX, nY, true);
    (void)World_GeoscapePointerUp(&g_world);
}

/* How many status-panel rows Smap_HitTestStrategSelect would build for a press whose cell is
 * (nCellX, nCellY): its scan is the 16x16 window dx, dy over -8..7 (0x82C48 / 0x82C50), and the gates
 * that add a row are the ones the builders apply -- an ACTIVE type 0/3/1/4 needs bVisible
 * (0x82E84..0x82F8C, 0x830D4..0x831A0), an INACTIVE type 0/3 needs the selection flag down
 * (0x82E94 gate at 0x82E6C's else side), an INACTIVE type 1/4 needs BOTH bVisible and the flag down
 * (0x830F4). Zoom mode 0 only: the scale at 0x82D48 / 0x82DD0 runs at mode 1 and 2. The Part 100.4
 * round uses this to pick press cells that answer as single-item clicks, never as the >1 box (0x832B0). */
static i32 TestHitLineCount(const PtrArray *pList, i32 nCellX, i32 nCellY, bool bSelectionUp)
{
    i32 nLines = 0;

    for (i32 i = 0; i < PtrArray_GetCount(pList); ++i) {
        const StrategItem *const pItem = (const StrategItem *)PtrArray_Get(pList, i);
        i32 nDx, nDy;

        if (pItem == NULL) {
            continue;
        }
        nDx = pItem->nX - nCellX;
        nDy = pItem->nY - nCellY;
        if (nDx < -8 || nDx > 7 || nDy < -8 || nDy > 7) {
            continue;
        }
        if (pItem->nType == 0 || pItem->nType == 3) {
            if (pItem->bActive != 0 ? pItem->bVisible != 0 : !bSelectionUp) {
                ++nLines;
            }
        } else if (pItem->nType == 1 || pItem->nType == 4) {
            if (pItem->bActive != 0 ? pItem->bVisible != 0 : (pItem->bVisible != 0 && !bSelectionUp)) {
                ++nLines;
            }
        }
    }
    return nLines;
}

/* The frame as a picture (Part 64's verification change): the surface is 5-6-5 (R at bit 11, G at bit 5
 * -- PackInk above), so the dump scales each field to 8 bits and writes a binary PPM under tests/ref.
 * ctest runs every test with WORKING_DIRECTORY at the repo root, which is where that path lands. */
static void DumpFrame(const char *pszName)
{
    const SoftSurf *p = g_assets.pSurface;
    char   szPath[160];
    char   szHeader[64];
    char   aRow[240 * 3];
    HANDLE hFile;
    DWORD  cbWritten;
    int    nHeader;

    snprintf(szPath, sizeof(szPath), "tests/ref/%s.ppm", pszName);
    hFile = CreateFileA(szPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { return; }
    nHeader = snprintf(szHeader, sizeof(szHeader), "P6\n%d %d\n255\n", p->nWidth, p->nHeight);
    if (nHeader > 0) { (void)WriteFile(hFile, szHeader, (DWORD)nHeader, &cbWritten, NULL); }
    for (i32 y = 0; y < p->nHeight; ++y) {
        const u16 *pRow = (const u16 *)p->pPixels + (size_t)y * (size_t)p->nWidth;
        for (i32 x = 0; x < p->nWidth; ++x) {
            const u16 v = pRow[x];
            aRow[3 * x + 0] = (char)(((v >> 11) & 0x1Fu) * 255 / 31);
            aRow[3 * x + 1] = (char)(((v >> 5)  & 0x3Fu) * 255 / 63);
            aRow[3 * x + 2] = (char)((v & 0x1Fu) * 255 / 31);
        }
        (void)WriteFile(hFile, aRow, (DWORD)(p->nWidth * 3), &cbWritten, NULL);
    }
    (void)CloseHandle(hFile);
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

/* The cell table the shell owns, addressed the way the original does: 3600 dwords per layer,
 * 60 per row (Map.apCells @ Map+0x15338). Used by the Part 66 deploy gate. */
static MapCell *FrameCellAt(i32 nX, i32 nY, i32 nLayer)
{
    if (nX < 0 || nX >= PUFO_MAP_CELL_DIM || nY < 0 || nY >= PUFO_MAP_CELL_DIM
            || nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return g_world.apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer + PUFO_MAP_CELL_DIM * nY + nX];
}

/* The same table as a lookup callback, for the fixtures that move a unit the way the original does. */
static MapCell *FrameCellLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;

    return FrameCellAt(nX, nY, nLayer);
}

/* A cell a shot can be fired from: an empty floor cell the mission's own search reaches
 * (Path_ComputeReachable @ 0x2b820, the call Unit_PathToCell @ 0x2c334 makes) and from which the
 * game's own line test (Map_TraceLos @ 0x30890) reaches a live alien. The original's voxel test
 * skips Map.pSelUnit on every call (the test at 0x324A4), so the shooter's own body never blocks
 * its own line; Map_TraceLos carries the exclusion pair as NULL, NULL, so the probe lifts the
 * shooter off its own cell for the walk and puts it straight back. */
static bool FrameShotSpot(MapCell *pFrom, const Soldier *pSel, i32 nX, i32 nY, i32 nZ, i32 *pnFoe)
{
    const GameMap *pMap = g_world.pTactMap;
    MapCell *pC = FrameCellAt(nX, nY, nZ);
    Soldier *pSelf;
    bool bOk = false;

    *pnFoe = -1;

    if (pC == NULL || pC->pFloorSpr == NULL || pC->pUnit != NULL) {
        return false;
    }
    if (!Path_ComputeReachable(&g_world.sTactPathScratch, pFrom->nX, pFrom->nY, pFrom->nZ,
                               pSel->bFacing, nX, nY, nZ,
                               pSel->nUnitType < PUFO_UNIT_TYPE_COUNT
                                   && g_aUnitTypeFlag0[pSel->nUnitType] != 0,
                               FrameCellLookup, NULL, NULL)) {
        return false;
    }
    pSelf = pFrom->pUnit;
    pFrom->pUnit = NULL;
    for (i32 k = 0; pMap != NULL && k < (i32)pMap->nEnemyCount && !bOk; ++k) {
        const Soldier *pE = pMap->apEnemyUnits[k];
        Soldier *pFoeSave;

        if (pE == NULL || pE->pCell == NULL || pE->uHp == 0) {
            continue;
        }
        /* The trace ends inside the alien's own cell, and the alien is now a real occupant
         * there (the corrected unitloft row base). The shot the spot stands for stops ON that
         * occupant (nFireArg5 4, the direct-hit slot), so the probe lifts the target's stamp
         * for its own walk and puts it back -- "clear up to the alien", not "clear through it". */
        pFoeSave = pE->pCell->pUnit;
        pE->pCell->pUnit = NULL;
        bOk = Map_TraceLos(&g_world.sTactLoft, FrameCellLookup, NULL,
                           16 * nX + 8, 16 * nY + 8, 12 * nZ + 6,
                           16 * (i32)pE->pCell->nX + 8, 16 * (i32)pE->pCell->nY + 8,
                           12 * (i32)pE->pCell->nZ + 6, NULL, NULL);
        pE->pCell->pUnit = pFoeSave;
        if (bOk) {
            *pnFoe = k;
        }
    }
    pFrom->pUnit = pSelf;
    return bOk;
}

/* How many of the 14400 slots own a unit -- the census the iso layer's occupant tests read. */
static i32 FrameCountOccupants(void)
{
    i32 n = 0;

    for (i32 k = 0; k < PUFO_TERRAIN_CELL_SLOTS; ++k) {
        if (g_world.apTactCells[k] != NULL && g_world.apTactCells[k]->pUnit != NULL) {
            ++n;
        }
    }
    return n;
}

/* Cells the tactical draw admits a unit onto: MapCell.bFlags44 bit 0, the byte
 * MapView_DrawUnit tests at 0x1CA54 (BEQ 0x1D5E4 skips the unit) and
 * Unit_UpdateVisibility @ 0x48580 sets at 0x48E04 / 0x48E48 / 0x48EC0. */
static i32 FrameCountRevealed(void)
{
    i32 n = 0;

    for (i32 k = 0; k < PUFO_TERRAIN_CELL_SLOTS; ++k) {
        if (g_world.apTactCells[k] != NULL && (g_world.apTactCells[k]->bFlags44 & 1u) != 0) {
            ++n;
        }
    }
    return n;
}

/* Part 76's counting rule for the furniture arms. Paint the mission as it stands on its furniture
 * screen and keep that picture; then paint the SAME state with Map_nUiScreen forced to 0, which is the
 * iso layer with no arm on top of it. The difference is that arm's own pad blits: the iso layer's only
 * screen-sensitive step is the cover at pufo_isodraw.c:748, and that goes through
 * SoftSurf_BlitSrcRectEx, never through the pad hook. Both paints see the same mission state, which a
 * screen-0 baseline taken before the opening click would not -- the viewport-guide arm at
 * pufo_isodraw.c:398 blits several pads and is gated on cells the opening handlers change. */
static i32 PaintFurnitureFrame(const char *pszName)
{
    u16 nScreen = g_world.nUiScreen;
    i32 nArm;
    i32 nBase;

    ClearSurface();
    g_nBlits = 0;
    World_TactFrame(&g_world);
    nArm = g_nBlits;
    DumpFrame(pszName);

    g_world.nUiScreen = 0;
    ClearSurface();
    g_nBlits = 0;
    World_TactFrame(&g_world);
    nBase = g_nBlits;
    g_world.nUiScreen = nScreen;
    return nArm - nBase;
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

    /* InitTerm_UiBankEntry @ 0x11148 (Part 60): the CRT entry builds all 130 widgets with
     * bEnabled 1 -- without it the pen path's HitTest never fires. The test's stand-in bank
     * needs the same bring-up the app now runs. */
    (void)UiBank_CtorAll(&g_bank, &g_bBankActionFlag);

    if (!World_Init(&g_world)) {
        printf("FAIL: World_Init\n");
        return 1;
    }
    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadPads   = LoadPads;
    hooks.pfnPlaySample = RecSample;
    hooks.pHit          = &g_hit;
    hooks.pColor        = &g_color;
    hooks.pfnPresentFrame = PresentThunk;   /* WorldHooks.pfnPresentFrame, the tact frame's GX_Present (Part 63) */
    hooks.pPresentUser    = NULL;
    hooks.pfnInitStartupUi = Test_InitStartupThunk; /* the quit page's Да tail callee, 0x6B490 (Part 102) */
    hooks.pszSaveDir      = "";                     /* the options save/load rows' directory: the cwd, as in the save unit */
    hooks.pnAnimPhase   = (i32 *)&g_nUiSlot0; /* Map + 0x60 @ 0x113A18, the cell the pulse's phase gate counts */
    hooks.pnMapPhase    = &g_nMapPhase;       /* Map + 0x37 @ 0x1139EF, the placement arm's cursor phase */
    /* The tact bank bring-up wiring (Part 54): the world installs InitUiControls' handlers and pads
     * on this bank, and the installed MenuBtn handler reaches back through these fields. */
    memset(&g_tactInHooks, 0, sizeof(g_tactInHooks));
    g_tactInHooks.pfnPlaySample        = RecBankSample;
    g_tactInHooks.pfnInvCloseAndCommit = Rec_InvCloseAndCommit; /* the screen-1 arm's callee (Part 66) */
    /* Part 67: the keyboard arms' seams, the same set the shell installs (pufo_app.c). */
    g_tactInHooks.pfnHandleMapClick    = Rec_TactMapClick;
    g_tactInHooks.pfnSelectNextUnit    = Rec_TactSelNext;
    g_tactInHooks.pfnSelectPrevUnit    = Rec_TactSelPrev;
    g_tactInHooks.pfnInvScrollUp       = Rec_TactScrollUp;
    g_tactInHooks.pfnInvScrollDown     = Rec_TactScrollDown;
    g_tactInHooks.pfnOnConfirm         = Rec_TactOnConfirm;
    g_tactInHooks.pfnSetCursorPos      = Rec_TactSetCursorPos;
    g_tactInHooks.pfnPushTextRecord    = Rec_TactPushText;
    g_tactInHooks.pfnCursorChanged     = Rec_CursorChanged;
    hooks.pBank               = &g_bank;
    hooks.pTactInputHooks     = &g_tactInHooks;
    hooks.pfnResizeMainWindow = RecResizeMainWindow; /* widget 58's window seam (Part 61) */
    hooks.pResizeUser         = NULL;
    hooks.pbBankActionFlag    = &g_bBankActionFlag;
    hooks.pSaveLoadHooks      = &g_slotHooks;
    hooks.pfnFormatSlotList   = RecFormatSlots;
    hooks.pFormatSlotListUser = NULL;
    hooks.pSlotLabels         = &g_aSlotLabelRows[0][0];  /* MainMenu_pSlotCtx @ 0x156938 (Part 76) */
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
     * longer in that list: the shell owns the base-attack record, so the arm points at its own draw env.
     * Case 3 left it too: BaseInfoScreen_Open @ 0x6167c is installed on nav slot 1 (the unrolled store at
     * 0x67B80) and the shell now owns 0x158828 / 0x15882C / 0x158830 and the three panels, so the arm,
     * its control run and its draw env all point at the shell's fields. Case 6 left the same list with
     * Part 87: BasePlaneScreen_Open @ 0x617f4 sits on nav slot 3 (0x67B98) and the shell owns the object
     * block at 0x161A48, its control run and all three panel sets. Case 8 left it with Part 88:
     * ResearchScreen_Open @ 0x616ac sits on nav slot 5 (the unrolled store at 0x67BAC) and the shell owns the
     * screen's pad, its four controls, its three panels and its two row cells. */
    Check(pF->draw.pManufPad == NULL && pF->draw.pTransferPad == NULL && pF->draw.pBuySellPad == NULL,
          "the three arms whose screen the shell still cannot open stay NULL (see pufo_geoscape.h)");
    Check(pF->draw.pResearchDraw == &pF->researchDraw && pF->draw.pResearchCtl == &pF->researchCtl
              && pF->researchCtl.apCtl == g_world.apResearchCtl
              && pF->researchDraw.pAnim == &pF->anim
              && pF->researchDraw.apCtlQty[0] == &g_world.aResearchCtl[2]
              && pF->researchDraw.apCtlQty[1] == &g_world.aResearchCtl[3]
              && pF->researchDraw.pPanelTopic == g_world.pResearchTopicPanel,
          "case 8 reaches ResearchScreen_Draw through the shell's own draw env and control seam (0x9411C, 0x94120)");
    Check(pF->draw.pPlaneArm == &pF->planeArm
              && pF->planeArm.apCtl == g_world.apPlaneCtl
              && pF->planeArm.ppBackdropPad == &g_world.planeBlock.pBackdropPad
              && pF->planeArm.pbStatPanelAOpen == &g_world.planeStatPanels.bPanelAOpen
              && pF->planeArm.pPlaneEnv == &pF->planeDraw && pF->planeArm.pCtlEnv == &pF->planeCtl
              && pF->planeDraw.pDetail == &pF->planeDetail
              && pF->planeDetail.pStatEnv == &g_world.planeStatEnv,
          "case 6 reaches BasePlaneScreen_Draw through the shell's own arm, control seam, draw env and detail env (0x8C1C8, 0x930F8)");
    Check(pF->draw.pBaseInfoArm == &g_world.baseInfoArm
              && pF->draw.pBaseInfoCtl == &g_world.baseInfoCtl
              && pF->draw.pBaseInfoDraw == &g_world.baseInfoDraw,
          "case 3 is wired to the shell's own base-info envs, reachable from nav slot 1 (0x67B80, id 3 at 0x6169C)");
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
    Check(pF->panel.pfnCtlOpen != NULL && pF->panel.pfnCtlSfx != NULL,
          "the panel's two footer controls run Geoscape_PrepScreenOpen @ 0x85194 and its Sfx twin @ 0x852b8");
    Check(pF->panel.pfnConfirmReport == NULL && pF->panel.pfnApplyResearch == NULL,
          "only the two screen openers stay unwired: Ui_OpenBaseInfoAlt @ 0x84d18 ends by opening ufopaedia "
          "screen 16 (0x84DE4 / 0x84DF4) and Ui_OpenResearch @ 0x84bf8 opens research screen 8 (0x84CEC / "
          "0x84CF0), and the shell has no Init for either screen yet -- the same gap as nav slots 6/8/9/10/11");

    /* 9b. The message-answer panels. Ten status-panel arms test a handler field and return before drawing
     * when it is NULL, so the whole family was invisible whatever the message line said. With the handlers
     * installed, every one of them paints more than the no-arm baseline does -- the counts are the proof,
     * because a suppressed arm paints exactly the baseline (the chain tail only). */
    {
        static const u8 aPanelFlags[] = { 0x11, 0x0F, 0x0C, 0x0E, 0x14, 0x13, 0x12, 0x17, 0x16, 0x15, 0x3C };
        static const char *aPanelNames[] = {
            "base lost (0x8F270)", "fuel low (0x8F41C)", "fly to point (0x8F5B4)", "point reached (0x8F768)",
            "target panel (0x909B8)", "mission site (0x90D90)", "travel panel (0x9119C)",
            "descent panel (0x91498)", "descent alt (0x91930)", "descent target (0x91698)",
            "alien base panel (0x91B2C)"
        };
        UiControl *pCtlA   = g_world.ui.apCtl[GEOUI_CTL_186BB4];  /* *pGeoscapeCtlA @ 0x186BB4 */
        UiControl *pCtlB   = g_world.ui.apCtl[GEOUI_CTL_186BB0];  /* *pGeoscapeCtlB @ 0x186BB0 */
        i32 nBaseline;
        i32 nAboard;

        Check(pF->panel.pfnResolveInterceptUI != NULL && pF->panel.pfnDismissSelected != NULL
                  && pF->panel.pfnConfirmCraftTarget != NULL && pF->panel.pfnConfirmSelected != NULL
                  && pF->panel.pfnConfirmSelectedAlt != NULL && pF->panel.pfnSetWaypoint != NULL
                  && pF->panel.pfnSelectMapTarget != NULL && pF->panel.pfnCancelTarget != NULL
                  && pF->panel.pfnSelectTarget != NULL && pF->panel.pfnClearSelection != NULL
                  && pF->panel.pfnBeginCraftSelect != NULL,
              "the panel's eleven command handlers are the ported Smap_* functions, not stubs");

        /* The chain tail's own branch (0x91CF4) is part of what the picture has to show, so the fixture takes
         * the camera off screen 2 first: on screen 2 the tail runs the globe-hover arm at 0x91D78, which paints
         * the hover crosshair and its "no build here" message into every one of these pictures -- a
         * combination the game never reaches with a message-answer panel up. Section 5 left the camera on
         * screen 2 (0x61620); sections 9c, 10 and 12 each state the id they need. */
        g_world.screen.nGeoscapeScreen = 0;
        g_world.bPlacementPending = 0;

        /* No flag in the chain's range and none of the tested ones: no arm runs, so the count is what the
         * chain tail alone paints. */
        g_world.msgLines.nLineCount = 0;
        UiLineList_Insert(&g_world.msgLines, 0, 1, 0, 0, 0, 0);
        ClearSurface();
        g_nBlits = 0;
        Geoscape_DrawStatusPanel(&pF->panel);
        nBaseline = g_nBlits;

        printf("       panel arms, blits over the %d-blit tail baseline:\n", nBaseline);
        for (i = 0; i < (i32)(sizeof(aPanelFlags) / sizeof(aPanelFlags[0])); ++i) {
            pCtlA->pfnClick = NULL;
            pCtlB->pfnClick = NULL;
            UiLineList_SetLine(&g_world.msgLines, 0, aPanelFlags[i], 0, 0, 0, 0);
            ClearSurface();
            g_nBlits = 0;
            Geoscape_DrawStatusPanel(&pF->panel);
            Check(g_nBlits > nBaseline,
                  "the arm paints more than the tail does, i.e. its guard let it through");
            printf("         flag 0x%02X %-24s blits %d, ctlA %s, ctlB %s\n", (int)aPanelFlags[i],
                   aPanelNames[i], g_nBlits, pCtlA->pfnClick ? "armed" : "-", pCtlB->pfnClick ? "armed" : "-");
        }

        /* The descent panel's own handler table (0x91578 / 0x915F8..0x91604 / 0x91630). Smap_BeginCraftSelect
         * is stored first; the four LDREQ / STREQ at 0x915F8 overwrite it with Smap_ResolveInterceptUI when
         * the craft the line names carries no soldiers, and no craft in a new game does. */
        UiLineList_SetLine(&g_world.msgLines, 0, PUFO_STATUS_FLAG_DESCENT_PANEL, 0, 0, 0, 0);
        pCtlA->pfnClick = NULL;
        pCtlB->pfnClick = NULL;
        ClearSurface();
        g_nBlits = 0;
        Geoscape_DrawStatusPanel(&pF->panel);
        nAboard = Craft_CountSoldiersAboard(g_world.apSoldiers, (i32)g_world.nSoldierCount, 0);
        Check(pCtlB->pfnClick == ((nAboard == 0) ? pF->panel.pfnResolveInterceptUI
                                                  : pF->panel.pfnBeginCraftSelect),
              "the launch control carries Smap_BeginCraftSelect (0x91578), and 0x915F8..0x91604's four LDREQ / "
              "STREQ swap it for the resolve handler exactly when the craft colour slot 0 names carries nobody");
        Check(pCtlA->pfnClick == pF->panel.pfnResolveInterceptUI,
              "and the second control carries the resolve handler the arm stored at 0x91630");
        Check(pCtlB->y == PUFO_STATUS_TARGETCTL_Y_CANCEL && pCtlA->y == PUFO_STATUS_TARGETCTL_Y_OK2,
              "and both controls sit on the arm's own rows: 0x9156C puts CtlB at y 102, 0x91624 puts CtlA at y 125");
        printf("         descent panel: craft 0 carries %d soldier(s), blits %d\n", nAboard, g_nBlits);
        DumpFrame("geoscape_descent_panel"); /* the picture the 0x91498 arm paints */

        /* UiPanel_GetLineFlag @ 0x5a334 reads the flag byte with no test of the count, so a drawn arm stays
         * live after the line is dropped. The sections below run on an empty status line, so the byte goes
         * back to zero with the count. */
        UiLineList_SetLine(&g_world.msgLines, 0, 0, 0, 0, 0, 0);
        g_world.msgLines.nLineCount = 0; /* 0x87CF8: a message on the status line stops the scrolling */
    }

    /* 9c. A nav row pressed THROUGH the dispatcher. The screen-1 arm walks the ten base-strip slots at
     * [root + 0x120] (0x87E34) -- apNavCtl @ 0x159888, the same ten controls Geoscape_BuildNavControls
     * installs the handlers on and lays out through stru_AB4B0 @ 0xAB4B0 -- so a press inside slot 1's box
     * (descriptor offset (1,247)) is a call into BaseInfoScreen_Open @ 0x6167c: sample 17 (0x61688),
     * BaseInfoScreen_Init (0x61690), and only then the id 3 (0x6169C). */
    g_world.screen.nGeoscapeScreen = 1;
    g_world.bPlaceActive = 0;
    nSamples0 = g_nSamples;
    World_GeoscapePointer(&g_world, 60, 252, true);
    World_GeoscapePointerUp(&g_world);
    CheckEqI32(g_world.screen.nGeoscapeScreen, 3,
               "the nav row press runs slot 1's handler and stores 3 (0x6169C)");
    Check(g_nSamples > nSamples0, "and the opener played its sample (Sound_Play(17, 4) at 0x61688)");
    Check(g_world.pBaseInfoPad != NULL && g_world.apBaseInfoCtl[0] != NULL
              && g_world.apBaseInfoCtl[1] != NULL && g_world.apBaseInfoPanel[0] != NULL
              && g_world.apBaseInfoPanel[1] != NULL && g_world.apBaseInfoPanel[2] != NULL
              && g_world.bBaseInfoPanelsBuilt != 0 && g_world.bBaseInfoPanelMode != 0,
          "BaseInfoScreen_Init @ 0x57688 allocated the pad (0x158828) and the two controls (0x15882C / "
          "0x158830), and its tail call built the three 42-line panels and set byte_AB190 (0x5581C)");
    Check(g_world.apBaseInfoCtl[0]->pfnClick != NULL && g_world.apBaseInfoCtl[1]->pfnClick != NULL
              && g_world.ui.apCtl[GEOUI_CTL_186B00]->x == 224
              && g_world.ui.apCtl[GEOUI_CTL_186B00]->y == 1,
          "OnOk installed on the first control (0x5772C), the panel-mode toggle on the second (0x5771C), and "
          "the shared close control moved to (224, 1) (0x57738 / 0x57744)");
    CheckEqI32((i32)g_world.nBaseInfoWorkBase, (i32)g_world.nSelectedBase,
               "Init stamps byte_1870F4 with the base the geoscape has selected (0x5778C)");
    CheckEqI32(g_world.bBaseInfoOverlayOpen, 0, "and clears the base-select overlay byte (0x57794)");

    /* Closing it is Geoscape_CloseUI case 3: the id goes to 1 (0x8BDE8), the panels are released because
     * the built flag is set (0x8BDF8), and the pad is freed (0x8BE10 / 0x8BE18). */
    World_CloseGeoscape(&g_world);
    CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "case 3 hands the geoscape back to screen 1 (0x8BDE8)");
    Check(g_world.pBaseInfoPad == NULL && g_world.apBaseInfoPanel[0] == NULL
              && g_world.apBaseInfoPanel[1] == NULL && g_world.apBaseInfoPanel[2] == NULL
              && g_world.apBaseInfoCtl[0] == NULL && g_world.apBaseInfoCtl[1] == NULL
              && g_world.bBaseInfoPanelsBuilt == 0,
          "the release arm freed the three panels and the two controls and CloseFreePad freed the pad");

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

    /* Part 73's gate. RollCraftStats @ 0x81A9C takes no difficulty argument: it reads the weight-row index
     * from nMonthCounter at 0x81AB4 and the craft-class cap off nAlienPoolAddend at 0x81B7C, at the roll.
     * In the shell each of those cells has exactly three users -- the monthly writer
     * (FundingScreen_OpenMonthly @ 0x5D170), the day tick (Geoscape_TickBases @ 0x5D3C8) and the mission roll
     * -- and the spawn context names them instead of holding a copy. This block fails the moment somebody
     * puts a by-value pair back into the bring-up wiring, which is the bug it exists to catch. */
    Check(g_world.spawnCtx.pnDifficulty == &g_world.nMonthCounter
              && (const void *)g_world.spawnCtx.pnEarlyGate
                     == (const void *)g_world.geo.pnDaysSinceStart,
          "the roll's two inputs are the cells themselves: the monthly writer's handle and the day tick's");
    Check(g_world.fundingInit.pnMonthCounter == &g_world.nMonthCounter,
          "one month counter @ 0x159648, not one for the funding screen and a second one for the roll");

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
    GeoFillEmptyGrid(); /* an empty base in the original's own sense: the score can only be 0 */
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
    /* Part 100.3 picture 1: the same pick as a picture -- the globe, the resolved region readout and the
     * seated "Build base" caption, on the frame 1621-1622 already drew. */
    DumpFrame("newbase_pick");

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
    /* Part 100.3 picture 2: the new-base build screen as the player sees it -- the dialog list, the 6x6
     * placement overlay over the wiped grid of the slot the new base takes, and the row-new-base caption. */
    ClearSurface();
    (void)DrawFrame();
    DumpFrame("newbase_overlay");
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

    /* Part 100.3 picture 3: the state the commit landed on (0x59B2C) -- the placement strip with the NEW base
     * selected (0x8B638 seated it on the old count), so the strip's own base list is the proof: the record
     * Base_CreateNamedEntry added at 0x66930 stands there with its built cell. */
    ClearSurface();
    (void)DrawFrame();
    DumpFrame("newbase_created");

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
    Check(g_world.pFacPanel->bSelLineHighlight == 1, "and re-arms the panel while the placement is unarmed (0x87F60)");
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

    /* 12h. The hangar (screen 6), reached the way the game reaches it: nav control slot 3. The screen is
     * the object block at 0x161A48 with three panel sets behind it -- the craft pair Init's tail call
     * builds, the crew pair the third main control builds, the five store columns the fourth one builds --
     * and Geoscape_CloseUI case 6 tears those sets down ONE arm per call, so the chain below runs the
     * dispatcher several times before it reaches the placement screen. */
    {
        UiPanel *pCraft  = g_world.planeCraftPanels.pCraftPanel;
        UiPanel *pSlot   = g_world.planeStorePanels.pSlotPanel;
        i32      nCraftRows, nCensusRows, nStoreRows;
        i32      nSampleMark, nStock43Save, nCarry43Save;

        /* The state the screen reads, before anything is opened: the shipped start state. */
        CheckEqI32((i32)g_assets.apObjects[43]->auPerBaseVal[0], 1,
                   "base 0 holds one Heavy cannon [AP] clip -- slot 43, the only slot the hangar's row gate (slot < 25 || 30 < slot < 52, 0x7B190) admits at a new game");
        CheckEqI32((i32)g_assets.apObjects[43]->bCargoSize, 2, "and its hold cost is 2");
        CheckEqI32((i32)g_assets.apObjects[43]->aItemQty[0], 0, "while the Skyranger carries none of it");
        Check(g_abSlotTopicGate[43] == 1 && g_world.aTopics[1].bUnlocked == 0,
              "the slot's topic gate is topic 1 (byte_AA968 @ 0xAA968) and research.dat ships every topic locked, so the stores list starts empty");
        Check(g_world.apCraft[0]->bAtBase == 1 && g_world.apCraft[0]->bHomeBase == 0
                  && (i32)Craft_GetTypeCargoCap(g_world.apCraft[0]) == 140,
              "craft 0 is the home-base craft with a 140-unit hold (Ship.bCraftType 0, Craft_GetTypeCargoCap @ 0x65ac4)");

        nSampleMark = g_nSamples;
        g_world.nav.apNav[3]->pfnClick();                 /* BasePlaneScreen_Open @ 0x617f4 */
        Check(g_nSamples == nSampleMark + 1 && g_aFrameSamples[nSampleMark] == 17,
              "the opener plays sample 17 with its second argument 4 (0x61800)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 6, "and writes the id 6 BEFORE its tail call to Init (0x61828)");
        Check(g_world.ui.apCtl[GEOUI_CTL_186B00]->x == 224 && g_world.ui.apCtl[GEOUI_CTL_186B00]->y == 1,
              "the opener seats the close control at (224,1) (0x6180C MOV #0xE0, 0x61820 MOV #1)");

        Check(g_world.planeBlock.pBackdropPad != NULL, "Init @ 0x7d9e4 loaded the plane backdrop onto the block base");
        for (i = 0; i < 5; ++i) {
            Check(g_world.planeBlock.apCraftPads[i] != NULL, "and the five baseb001..005 craft pads");
        }
        for (i = 0; i < 6; ++i) {
            Check(g_world.planeBlock.apWeaponPads[i] != NULL, "and the six baseb049..054 weapon pads");
        }
        for (i = 0; i < PUFO_PLANE_MAIN_CTLS; ++i) {
            Check(g_world.planeBlock.apMainCtl[i] != NULL && g_world.planeBlock.apMainCtl[i]->pfnClick != NULL,
                  "with the four main controls and their handlers installed");
        }
        for (i = 0; i < PUFO_PLANE_WEAPON_CTLS; ++i) {
            Check(g_world.planeBlock.apWeaponCtl[i] != NULL && g_world.planeBlock.apWeaponCtl[i]->pfnClick != NULL,
                  "and the seven weapon controls, RearmFromStores plus SelectWeapon25..30");
        }
        CheckEqI32(g_world.bPlaneBuilt, 1, "Init's tail call BasePlaneScreen_BuildPanels built the craft list (0x7CCAC)");
        Check(g_world.planeCraftPanels.pCraftPanel != NULL && g_world.planeCraftPanels.pStatusPanel != NULL
                  && g_world.planeCraftPanels.pCraftPanel->pfnClick != NULL
                  && g_world.planeCraftPanels.pStatusPanel->pfnClick != NULL,
              "both craft panels exist with their two select handlers (0x7CBC8 / 0x7CC3C)");
        ClearSurface();
        (void)DrawFrame();
        Check(SurfPix(2, 2) != 0 && SurfPix(120, 60) != 0 && SurfPix(120, 200) != 0
                  && SurfPix(20, 120) != 0,
              "case 6 paints: the backdrop at the top-left, the craft art down the middle, the craft list lower still");
        CheckEqI32(g_nBlits, 1,
                   "and one control comes through the seam: the close pad at (224,1) -- the list view skips the "
                   "detail control runs (0x93114 BEQ loc_93380) and the gate lands on the shared close blit "
                   "(0x9339C BEQ loc_91DA8 -> 0x91DA8/0x93058)");
        DumpFrame("geoscape_hangar"); /* the picture case 6 (0x9309C..0x93DD4) paints: backdrop, craft art, weapon icons, craft list */

        /* The wiring re-reads every panel pointer each frame, exactly as the original's consumers read the
         * globals the builders write, so the panels are only visible to the envs from the next frame on. */
        pCraft = g_world.planeCraftPanels.pCraftPanel;
        Check(g_world.ptrEnv.pPlaneCraftPanel == pCraft
                  && g_world.ptrEnv.pPlaneCraftStatusPanel == g_world.planeCraftPanels.pStatusPanel
                  && g_world.ptrEnv.pPlaneSlotPanel == g_world.planeStorePanels.pSlotPanel
                  && g_world.ptrEnv.apPlaneCtl == g_world.apPlaneCtl,
              "the press seam sees the same panels and the same control run (0x88210..0x88234)");
        nCraftRows = (pCraft != NULL) ? pCraft->nLineCount : -1;
        CheckEqI32(nCraftRows, (i32)g_world.bCraftCount,
                   "the list has one line per craft whose Ship.bHomeBase is the selected base (the count walk at 0x7CCA0)");
        CheckEqI32(pCraft->anLineAux[0], 0, "the rows are grouped by Ship.bCraftType through aPlaneCraftTypeOrder @ 0xABE28: the type-0 Skyranger first");
        CheckEqI32(pCraft->anLineAux[1], 1, "then the two type-3 craft");

        /* A row of the craft list, through the handler the panel itself carries. */
        if (pCraft != NULL) {
            pCraft->nUnk3032 = 0;
            pCraft->pfnClick();
        }
        CheckEqI32(g_world.nPlaneSelCraftIdx, pCraft != NULL ? pCraft->anLineAux[0] : -1,
                   "SelectCraft takes the row's roster index into nPlaneSelCraftIdx (0x7CBF8)");
        CheckEqI32(g_world.nHangarCraftIdx, g_world.nPlaneSelCraftIdx,
                   "and into nSelectedCraftIdx, the index the stores and weapon side reads (0x7CC00)");
        CheckEqI32(g_world.bPlaneShowDetail, 1,
                   "the craft is at base, so the screen switches to the detail view (0x7CC14 LDRB Ship.bAtBase, 0x7CC20 STRB)");

        ClearSurface();
        (void)DrawFrame();
        DumpFrame("geoscape_hangar_detail"); /* the detail arm: the two weapon columns, the six stat rows, the crew census */
        /* The detail draw is what computes the crew census -- Soldier.bUnk183 against the selected craft,
         * Soldier.nKind against the selected base (0x7C614..0x7C680) -- and a new game ships the whole
         * eight-man squad already in the Skyranger. */
        CheckEqI32(g_world.nPlaneSoldiersAboard, 8,
                   "aboard: every starter has Soldier.bUnk183 == the Skyranger's index (0x7C650)");
        CheckEqI32(g_world.nPlaneSoldiersAtBase, 8, "at base: every starter's Soldier.nKind @ +0xB6 is base 0 (0x7C664)");
        CheckEqI32(g_world.nPlaneSoldiersUnassigned, 0, "unassigned: none of them is free, so none is counted (0x7C678)");
        CheckEqI32(g_world.nPlaneCrewCarried, 0, "the five crew slots carry nothing (0x161AC0)");
        for (i = 0; i < 6; ++i) {
            CheckEqI32(g_world.anPlaneStats[i], 0, "and the six weapon stat cells are empty: no weapon is carried");
        }

        /* The next frame carries that census into the crew builder's line count: BuildListPanels sizes its
         * two panels with aboard + unassigned (0x161AB4 + 0x161AB8), read at the call. */
        (void)DrawFrame();
        g_world.planeBlock.apMainCtl[2]->pfnClick();                 /* BasePlaneScreen_BuildListPanels @ 0x7a3f4 */
        CheckEqI32(g_world.bPlaneListBuilt, 1, "the crew builder posts bPlaneListPanelsBuilt (0x7A4BC)");
        Check(g_world.planeListPanels.apNav[0] != NULL && g_world.planeListPanels.apNav[1] != NULL
                  && g_world.planeListPanels.apNav[0]->pfnClick != NULL
                  && g_world.planeListPanels.pCrewPanel != NULL
                  && g_world.planeListPanels.pCrewStatusPanel != NULL
                  && g_world.planeListPanels.pCrewPanel->pfnClick != NULL,
              "with its nav pair, its crew column and its status column, all wired");
        nCensusRows = g_world.planeListPanels.pCrewPanel->nLineCount;
        CheckEqI32(nCensusRows, g_world.nPlaneSoldiersAboard + g_world.nPlaneSoldiersUnassigned,
                   "the two panels are sized with the census the draw wrote");
        CheckEqI32(g_world.planeListPanels.pCrewPanel->anLineAux[0], 0, "the first row is soldier 0");
        (void)DrawFrame(); /* the envs pick the new panels up, as they pick up every panel the frame sees */
        Check(SurfPix(12, 74) != 0 && SurfPix(130, 74) != 0,
              "the crew pair paints its two columns: the eight names out of Soldier.szName @ +0xBA on the left and the craft each man rides on the right (0x7BD00, 0x7BD0C)");

        /* The fourth main control builds the five store columns. With every topic locked -- the shipped
         * research.dat -- the scan admits nothing, so the list is real but empty. */
        g_world.planeBlock.apMainCtl[3]->pfnClick();                 /* BasePlaneScreen_BuildControls @ 0x7b014 */
        CheckEqI32(g_world.bPlaneStoresBuilt, 1,
                   "BuildControls posts bPlaneStoresCommitted BEFORE a single row exists (0x7B12C)");
        Check(g_world.planeStorePanels.pSlotPanel != NULL && g_world.planeStorePanels.pAvailPanel != NULL
                  && g_world.planeStorePanels.pSelRowPanel != NULL && g_world.planeStorePanels.pCommitPanel != NULL
                  && g_world.planeStorePanels.pTakenPanel != NULL
                  && g_world.planeStorePanels.pSlotPanel->pfnClick != NULL
                  && g_world.planeStorePanels.pSelRowPanel->pfnClick != NULL
                  && g_world.planeStorePanels.pCommitPanel->pfnClick != NULL
                  && g_world.planeStorePanels.pAvailPanel->pfnClick == NULL
                  && g_world.planeStorePanels.pTakenPanel->pfnClick == NULL,
              "the five columns exist, the two quantity rows and the name column carry handlers, the two count columns carry none (0x7B244, 0x7B454, 0x7B574, 0x7B354, 0x7B6C4)");
        nStoreRows = g_world.planeStorePanels.pSlotPanel->nLineCount;
        CheckEqI32(nStoreRows, 0, "and the locked topic table leaves the stores list with no row at all");
        CheckEqI32(g_world.nPlaneCrewQuota, 0,
                   "the crew quota starts as what the craft carries of slots 31..35, and the Skyranger carries nothing there (0x7B05C..0x7B09C)");
        CheckEqI32(g_world.anPlaneCarryCount[43], 0, "the build zeroed the 256 staged counts first (0x7B038..0x7B058)");

        ClearSurface();
        (void)DrawFrame();
        DumpFrame("geoscape_hangar_locked"); /* the empty list over the backdrop: what a new game's hangar really shows */

        /* Re-open the same builder with the topic gate open -- research is what does this in play -- and the
         * shipped stock finally has a row. The builder's own clear arm runs first (the flag is set). */
        g_world.aTopics[1].bUnlocked = 1;
        g_world.planeBlock.apMainCtl[3]->pfnClick();
        CheckEqI32(g_world.bPlaneStoresBuilt, 0, "a second call takes the clear arm and frees the set (0x7D238 shape)");
        Check(g_world.planeStorePanels.pSlotPanel == NULL, "the columns are gone");
        g_world.planeBlock.apMainCtl[3]->pfnClick();
        (void)DrawFrame();
        pSlot = g_world.planeStorePanels.pSlotPanel;
        nStoreRows = (pSlot != NULL) ? pSlot->nLineCount : -1;
        CheckEqI32(nStoreRows, 1, "with topic 1 open the scan keeps exactly one row");
        CheckEqI32(pSlot->anLineAux[0], 43, "and it is slot 43, the clip in the base's stores");
        CheckEqI32(g_world.planeStorePanels.pAvailPanel->anLineAux[0], 1, "its available column carries the base's own count");
        CheckEqI32(g_world.planeStorePanels.pTakenPanel->anLineAux[0], 0, "and the carried column is empty");
        CheckEqI32(pSlot->nPosX, 10, "the name column sits at x 10, nPosY 70, nStyle 14, view width 125 (0x7B238)");
        CheckEqI32(pSlot->nViewW, 125, "and its name row is the truncated item name (0x7B838 caps it at 17 bytes)");

        /* Stage one unit, then take it back, then stage it again: the two quantity handlers on the same row. */
        g_world.planeStorePanels.pCommitPanel->nUnk3032 = 0;
        CheckEqI32(g_world.planeStorePanels.pCommitPanel->pfnClick == NULL ? 0 : 1, 1, "the \">\" row carries its handler");
        g_world.planeStorePanels.pCommitPanel->pfnClick();
        CheckEqI32(g_world.anPlaneCarryCount[43], 1, "CommitStores stages one unit of the row (0x7AFD8)");
        CheckEqI32(g_world.planeStorePanels.pTakenPanel->anLineAux[0], 1, "the carried column goes up");
        CheckEqI32(g_world.planeStorePanels.pAvailPanel->anLineAux[0], 0, "the available column comes down (0x7AFC4)");
        CheckEqI32(g_world.nShortfall1870C8, 0,
                   "and the hold gate passes: 2 for this clip against the 140-unit cap (0x7AD38..0x7AE50)");
        CheckEqI32((i32)g_assets.apObjects[43]->auPerBaseVal[0], 1,
                   "committing stages only: the base's stock has not moved yet");
        g_world.planeStorePanels.pSelRowPanel->nUnk3032 = 0;
        g_world.planeStorePanels.pSelRowPanel->pfnClick();                /* BasePlaneScreen_AdjustSelectedQty @ 0x7a9b4 */
        CheckEqI32(g_world.anPlaneCarryCount[43], 0, "the \"<\" row takes the staged unit back (0x7AA28)");
        CheckEqI32(g_world.planeStorePanels.pAvailPanel->anLineAux[0], 1, "and puts it back in the available column");
        g_world.planeStorePanels.pCommitPanel->pfnClick();
        CheckEqI32(g_world.anPlaneCarryCount[43], 1, "staging it again puts the count where the confirm will find it");

        /* The crew column is a toggle. Every starter ships in the Skyranger, so the first click takes the
         * row's man off him (0x7A338..0x7A388) and the next click re-boards him inside the craft's own
         * crew gate (0x7A2C0..0x7A2F8). */
        CheckEqI32(Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, 0), 8,
                   "all eight starters ride craft 0: Soldier.bUnk183 is the craft index");
        g_world.planeListPanels.pCrewPanel->nUnk3032 = 0;
        g_world.planeListPanels.pCrewPanel->pfnClick();              /* BasePlaneScreen_FillCrewPanel @ 0x7a188 */
        CheckEqI32((i32)g_world.apSoldiers[0]->bUnk183, 255, "the row click writes -1 into Soldier.bUnk183 (0x7A388)");
        CheckEqI32(Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, 0), 7,
                   "and the craft now carries seven men");
        (void)DrawFrame();
        CheckEqI32(g_world.nPlaneSoldiersAboard, 8,
                   "yet the census cell still says eight: while the crew pair is up the detail draw takes its crew arm and returns before the read-out (0x7BCF8..0x7BD44), so 0x161AB8 keeps the value the last read-out wrote -- stale on screen in the original too");
        g_world.planeListPanels.pCrewPanel->pfnClick();
        CheckEqI32((i32)g_world.apSoldiers[0]->bUnk183, 0,
                   "the same row the other way re-boards him (0x7A2F8), inside the type-0 gate 7 + 4 * 0 + 1 <= 14 (0x7A21C)");
        g_world.planeListPanels.pCrewPanel->pfnClick();
        CheckEqI32((i32)g_world.apSoldiers[0]->bUnk183, 255,
                   "and off again: the column toggles, it does not assign, and he stays off for the census check below");

        /* The two main controls that open the weapon columns, and the weapon row that applies a swap. */
        g_world.planeBlock.apMainCtl[0]->pfnClick();                     /* BasePlaneScreen_DrawStatLabelA @ 0x79e00 */
        g_world.planeBlock.apMainCtl[1]->pfnClick();                     /* DrawStatLabelB @ 0x79fc4 */
        CheckEqI32(g_world.planeStatPanels.bPanelAOpen, 1, "DrawStatLabelA raises bStatPanelAOpen (0x79E28)");
        CheckEqI32(g_world.planeStatPanels.bPanelBOpen, 1, "and B raises bStatPanelBOpen (0x79FEC)");
        g_world.planeBlock.apWeaponCtl[1]->pfnClick();                   /* SelectWeapon25 @ 0x79b20 */
        CheckEqI32(g_world.planeStatPanels.nPanelASel, 25,
                   "SelectWeapon25 writes the weapon into every OPEN column (0x79B34, 0x79B4C)");
        CheckEqI32(g_world.planeStatPanels.nPanelBSel, 25, "both of them here");
        CheckEqI32(g_world.planeStatPanels.bPanelAOpen, 0,
                   "and its tail call applies the swap, which closes both columns (0x79A10, 0x79A18)");
        CheckEqI32(g_world.planeStatPanels.bPanelBOpen, 0, "both of them");

        /* The pulse's screen-6 auto-repeat (0x95644..0x95870) walks the block's +0x5C/+0x60 crew pair and
         * +0x64/+0x68 stores pair, each gated on its own built flag. The scroll cursor is nUnk3028 --
         * nUnk3032 is the row the click handlers read. */
        g_world.planeStorePanels.pSlotPanel->nUnk3028      = 3;
        g_world.planeStorePanels.pSelRowPanel->nUnk3028    = 3;
        g_world.planeStorePanels.pCommitPanel->nUnk3028    = 3;
        g_world.planeStorePanels.pAvailPanel->nUnk3028     = 3;
        g_world.planeStorePanels.pTakenPanel->nUnk3028     = 3;
        g_world.planeListPanels.pCrewPanel->nUnk3028       = 3;
        g_world.planeListPanels.pCrewStatusPanel->nUnk3028 = 3;
        *g_world.pnAnimPhase = 1;                /* 0x95644: the switch runs when the phase ARRIVES at 1 */
        (void)World_PulseGeoscape(&g_world);
        CheckEqI32(g_world.planeStorePanels.pSlotPanel->nUnk3028, 3,
                   "nothing repeats for an unarmed control: UiControl_HitTest's force arm fires only a mode-0 control already in state 1 (0x1A3F0)");
        CheckEqI32(g_world.planeListPanels.pCrewPanel->nUnk3028, 3, "and a freshly built hangar has no control in that state");

        g_world.planeStorePanels.apNavCtl[0]->nState = 1;   /* nState 1 is what a held button leaves behind */
        g_world.planeListPanels.apNav[0]->nState = 1;
        *g_world.pnAnimPhase = 1;
        (void)World_PulseGeoscape(&g_world);
        CheckEqI32(g_world.planeStorePanels.pSlotPanel->nUnk3028, 2, "the held PREV repeats (0x95790, block +0x64)");
        CheckEqI32(g_world.planeStorePanels.pAvailPanel->nUnk3028, 2, "and the five columns move together (0x79A78..0x79AB8)");
        CheckEqI32(g_world.planeStorePanels.pCommitPanel->nUnk3028, 2, "the two row columns with them");
        CheckEqI32(g_world.planeListPanels.pCrewPanel->nUnk3028, 2, "the crew pair repeats on the same pass (block +0x5C)");
        CheckEqI32(g_world.planeListPanels.pCrewStatusPanel->nUnk3028, 2, "both of its columns");

        /* NEXT is clamped by nLineCount - nStyle (0x3F00C), and a hangar set is always under that limit. */
        g_world.planeStorePanels.pSlotPanel->nUnk3028 = 0;
        g_world.planeStorePanels.apNavCtl[1]->nState = 1;
        *g_world.pnAnimPhase = 1;
        (void)World_PulseGeoscape(&g_world);
        CheckEqI32(g_world.planeStorePanels.pSlotPanel->nUnk3028, 0,
                   "NEXT moves nothing: one row minus nStyle 14 is a negative limit, so the hangar list never scrolls");
        CheckEqI32(g_world.planeStorePanels.pSlotPanel->nStyle, 14, "the five columns are nStyle 14 (0x7B238)");
        CheckEqI32(g_world.planeListPanels.pCrewPanel->nStyle, 14, "and the two crew columns are too (0x7A554)");
        for (i = 0; i < PUFO_PLANE_STORE_NAV; ++i) {
            g_world.planeStorePanels.apNavCtl[i]->nState = 0;
            g_world.planeListPanels.apNav[i]->nState = 0;
        }
        g_world.planeStorePanels.pSelRowPanel->nUnk3028    = 0;
        g_world.planeStorePanels.pCommitPanel->nUnk3028    = 0;
        g_world.planeStorePanels.pAvailPanel->nUnk3028     = 0;
        g_world.planeStorePanels.pTakenPanel->nUnk3028     = 0;
        g_world.planeListPanels.pCrewPanel->nUnk3028       = 0;
        g_world.planeListPanels.pCrewStatusPanel->nUnk3028 = 0;

        /* The confirm control (unk_186BA4, installed by Init at 0x7DDE8) is what moves the staged unit for
         * real, and it tail-calls the close dispatcher (0x78E74) -- which for case 6 takes the FIRST arm. */
        nStock43Save = (i32)g_assets.apObjects[43]->auPerBaseVal[0];
        nCarry43Save = (i32)g_assets.apObjects[43]->aItemQty[0];
        nSampleMark  = g_nSamples;
        g_world.ui.apCtl[GEOUI_CTL_186BA4]->pfnClick();                  /* BuySellScreen_ApplyQtyChanges @ 0x78d90 */
        CheckEqI32((i32)g_assets.apObjects[43]->auPerBaseVal[0], nStock43Save - 1,
                   "the confirm takes the clip out of the base's stores (0x78E34 BaseRec_SubItemQty)");
        CheckEqI32((i32)g_assets.apObjects[43]->aItemQty[0], nCarry43Save + 1,
                   "and into the craft's cargo column (0x78E4C InvSel_AddItemQty, ObjectRecFull.aItemQty[craft] at +0x3A)");
        Check(g_nSamples == nSampleMark + 1 && g_aFrameSamples[nSampleMark] == 17,
              "the close it tail-calls opens with Sound_Play(17, 4) like every other close (0x8B95C)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 6,
                   "the tail close stays on 6: case 6's first arm only tears the crew pair down (0x8C018..0x8C028)");
        CheckEqI32(g_world.bPlaneListBuilt, 0, "bPlaneListPanelsBuilt is cleared by that teardown");
        Check(g_world.planeListPanels.pCrewPanel == NULL && g_world.planeListPanels.pCrewStatusPanel == NULL,
              "and both crew panels are gone");
        CheckEqI32(g_world.anPlaneCarryCount[43], 1,
                   "the staged count is left standing: only the next BuildControls zeroes the 256 cells (0x7B038)");

        /* The one frame the five store columns are actually on screen: with the crew pair down the detail
         * draw reaches its stores arm (0x7BD70..0x7C228), showing the committed row as 1 taken / 0 available. */
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("geoscape_hangar_stores");
        Check(SurfPix(30, 80) != 0 && SurfPix(190, 80) != 0,
              "the stores arm paints the row it was given: the item name at x 10 and the two quantity glyphs at x 170 and x 186 (0x7BE78, 0x7BF00, 0x7BF2C)");

        /* The rest of the ladder, one arm per call. The second step goes through the close control itself,
         * which is BasePlaneScreen_Close (0x7c9e8): its own crew walk over slots 31..35 first -- nothing is
         * staged there, because a new game's craft carries no crew items and the quota is the carried sum --
         * and then the same dispatcher tail call. */
        g_world.ui.apCtl[GEOUI_CTL_186B00]->pfnClick();                  /* BasePlaneScreen_Close @ 0x7c9e8 */
        CheckEqI32(g_world.bPlaneStoresBuilt, 0, "its dispatcher call takes the stores arm (0x8C034..0x8C044)");
        Check(g_world.planeStorePanels.pSlotPanel == NULL && g_world.planeStorePanels.pCommitPanel == NULL
                  && g_world.planeStorePanels.pSelRowPanel == NULL,
              "with the five columns freed");
        CheckEqI32(g_world.anPlaneCarryCount[43], 1, "the teardown does not clear the staged count either");

        /* Both panel sets are down now, so the detail draw finally reaches its read-out arm and the four
         * census cells are recomputed (0x7C5C0..0x7C68C): the crew column's toggle shows up here, not on the
         * frame the click happened on. */
        (void)DrawFrame();
        CheckEqI32(g_world.nPlaneSoldiersAboard, 7, "seven men aboard, counted by the read-out at 0x7C650");
        CheckEqI32(g_world.nPlaneSoldiersUnassigned, 1, "and one man unassigned at base 0 (0x7C678)");
        CheckEqI32(g_world.nPlaneSoldiersAtBase, 8, "still eight at the base (0x7C664)");

        /* The crew builder sizes its two panels from exactly those cells, which closes the loop: 7 + 1 rows,
         * and row 0 is still the man the column took off (the walk stores the roster index, 0x7A934). */
        g_world.planeBlock.apMainCtl[2]->pfnClick();
        CheckEqI32(g_world.planeListPanels.pCrewPanel->nLineCount, 8, "seven aboard plus one unassigned");
        (void)DrawFrame();
        g_world.planeListPanels.pCrewPanel->nUnk3032 = 0;
        CheckEqI32(g_world.planeListPanels.pCrewPanel->anLineAux[0], 0, "row 0 is soldier 0");
        g_world.planeListPanels.pCrewPanel->pfnClick();
        CheckEqI32((i32)g_world.apSoldiers[0]->bUnk183, 0,
                   "and the crew column puts him back: census, rows, click -- the whole hangar loop runs through the shell's own seams");

        /* The ladder's remaining arms, one per call. */
        World_CloseGeoscape(&g_world);
        CheckEqI32(g_world.bPlaneListBuilt, 0, "the crew arm tears the rebuilt pair down (0x8C018..0x8C028)");
        g_world.planeBlock.apMainCtl[0]->pfnClick();                     /* DrawStatLabelA re-opens a column */
        g_world.planeBlock.apMainCtl[1]->pfnClick();                     /* and DrawStatLabelB the other */
        Check(g_world.planeStatPanels.bPanelAOpen == 1 && g_world.planeStatPanels.bPanelBOpen == 1,
              "both stat columns are open again (0x79E28, 0x79FEC)");
        World_CloseGeoscape(&g_world);
        CheckEqI32(g_world.planeStatPanels.bPanelAOpen, 0, "the next call clears the two stat gates (0x8C0B0/0x8C0B4)");
        CheckEqI32(g_world.planeStatPanels.bPanelBOpen, 0, "both of them, on the same call");
        World_CloseGeoscape(&g_world);
        CheckEqI32(g_world.bPlaneShowDetail, 0, "the next clears the detail view (0x8C08C)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 6, "and still leaves the id at 6");
        World_CloseGeoscape(&g_world);
        CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "the last one is the tail: the id goes to 1 (0x8C094)");
        CheckEqI32(g_world.bPlaneBuilt, 0, "and BuildPanels' teardown arm ran (0x7D234)");
        Check(g_world.planeCraftPanels.pCraftPanel == NULL && g_world.planeCraftPanels.pStatusPanel == NULL,
              "leaving no craft list behind");

        g_world.aTopics[1].bUnlocked = 0; /* back to the shipped state for the checks below */
        CheckEqI32((i32)g_assets.apObjects[43]->aItemQty[0], nCarry43Save + 1,
                   "the moved clip stays moved: the hangar is the seam that arms a craft");
        CheckEqI32((i32)g_assets.apObjects[43]->auPerBaseVal[0], nStock43Save - 1,
                   "and the base is one clip lighter for it");
    }

    /* 12i. The research screen (screen 8), reached the way the game reaches it: nav control slot 5.
     * Geoscape_BuildNavControls @ 0x67b1c installs ResearchScreen_Open @ 0x616ac at apNavCtl + 0x14 (the
     * LDR at 0x67BB0); the opener plays sample 17 at 0x616B4 and writes the id 8 at 0x6176C before its tail
     * call to ResearchScreen_Init @ 0x749a8. The screen's own list is EMPTY at a new game, and the first
     * half of this gate is the reason, measured against the records main.dat's RESEARCH.DAT ships (4896
     * bytes = 144 records of 34, copied raw at 0x86900 with wUnk6 zeroed at 0x86938): every branch of
     * ResearchScreen_RebuildTopicList @ 0x74270 that could arm a topic is closed by that data. */
    {
        UiPanel *pTopic, *pQty, *pProg;
        i32 nActive0 = 0, nUnlocked0 = 0, nNeeds0 = 0, nRunning0 = 0, nRangeEq0 = 0, nSumZero0 = 0;
        i32 nSciSave, nSampleMark, nColIdx, nGridSaveI;
        u8  abGridSave[PUFO_GRID_SIZE];

        for (i = 1; i < (i32)g_world.nResearchEntries; ++i) {
            const ResearchTopicLive *pT = &g_world.aTopics[i];
            if (pT->bActive == 1) ++nActive0;
            if (pT->bUnlocked != 0) ++nUnlocked0;
            if (Research_TopicNeedsItem(g_world.aTopics, (ObjectRecFull *const *)g_assets.apObjects,
                                        i, 0) != 0) ++nNeeds0;
            if (pT->wTotal != 0 && pT->wProgress == 0 && pT->bUnlocked == 0) {
                i32 nSum = 0;
                ++nRunning0; /* 0x74364 skips its gate here, so this record reaches 0x747A0 */
                if (i + 1 < (i32)g_world.nResearchEntries
                    && g_world.aTopics[i + 1].abUnk0[2] == g_world.aTopics[i + 1].abUnk0[3]) {
                    ++nRangeEq0; /* 0x7487C: an equal (lo,hi) pair skips the availability scan */
                }
                for (int k = 0; k < 5; ++k) {
                    if (pT->abPreTopics[k] != 0) { nSum += pT->abPreTopics[k]; } /* 0x747BC */
                    if (pT->abNeedItems[k] != 0) { nSum += pT->abNeedItems[k]; } /* 0x74808 */
                }
                if (nSum == 0) { ++nSumZero0; }
            }
        }
        CheckEqI32((i32)g_world.nResearchEntries, 144, "the topic count is 144 (0x160C4C), one per RESEARCH.DAT record");
        CheckEqI32(nUnlocked0, 0, "and RESEARCH.DAT ships every topic locked (+0xD == 0): 0x74294's record-143 gate and the 0x74334 unlocked step never run");
        CheckEqI32(nActive0, 0, "nothing ships active either (+0xC == 0), so the list is empty before the screen is opened");
        CheckEqI32(nNeeds0, 0, "Research_TopicNeedsItem @ 0x72c8c answers 0 for all 144: only the two bands 55..65 and 118..142 read an item, every one with abNeedItems[0] == 0, and object slot 0 carries no stock at base 0 (0x72CE4, 0x72D00)");
        CheckEqI32(nRunning0, 30, "30 records are locked with progress booked, so they take the in-progress branch at 0x747A0");
        CheckEqI32(nRangeEq0, 30, "all 30 have an equal (lo,hi) pair in the NEXT record's head bytes, the only pair the scan reads (0x74868), so nothing is ever scanned as unlocked");
        CheckEqI32(nSumZero0, 28, "and 28 of the 30 sum to zero, so 0x748D0's nTotal != 0 fails for them; the other two carry a locked prerequisite, which 0x747AC multiplies into the gate as 0");
        CheckEqI32(g_world.aTopics[28].abUnk0[0] + 256 * g_world.aTopics[28].abUnk0[1], 448,
                   "the record's own name index is the u16 at +0: record 28 is the Cydonia topic, TEXTS[448] names it, and 0x6CFA8 reads its +0xD");
        CheckEqI32((i32)g_world.aTopics[28].wGate4, 6000,
                   "while +0x4 is the 6000 cost -- and 0x7373C reads THAT field as the row's TEXTS index (open row in PLAN.md)");

        nSampleMark = g_nSamples;
        g_world.nav.apNav[5]->pfnClick();                     /* ResearchScreen_Open @ 0x616ac */
        Check(g_nSamples == nSampleMark + 1 && g_aFrameSamples[nSampleMark] == 17,
              "the opener plays Sound_Play(17, 4) on the way in (0x616B4)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 8, "and writes the id 8 BEFORE its tail call to Init (0x6176C, 0x61778)");
        CheckEqI32(g_world.bResearchBuilt, 1, "Init's BuildPanels hook sets the built flag (0x73410)");
        Check(g_world.apResearchRoot[0] != NULL
                  && ((PadImage *)g_world.apResearchRoot[0])->nMode == PUFO_RESEARCH_PAD_MODE,
              "Init loads the \"Research\" pad (mode 6, 0x749DC) onto the block base (0x749D8)");

        for (i = 0; i < 4; ++i) {
            Check(g_world.apResearchCtl[i] == &g_world.aResearchCtl[i]
                      && g_world.aResearchCtl[i].pfnClick != NULL,
                  "the four block controls carry Init's handlers (0x74A28, 0x74A4C..0x74A94)");
        }
        Check(g_world.aResearchCtl[0].x == 205 && g_world.aResearchCtl[0].y == 18
                  && g_world.aResearchCtl[1].x == 205 && g_world.aResearchCtl[1].y == 38
                  && g_world.aResearchCtl[2].x == 10  && g_world.aResearchCtl[2].y == 163
                  && g_world.aResearchCtl[3].x == 10  && g_world.aResearchCtl[3].y == 183,
              "UiControl_Setup seats them on g_aResearchCtlDesc @ 0xABB10: prev (205,18), next (205,38), "
              "the +1 at (10,163), the -1 at (10,183), mode 0 (0x74AE4)");
        Check(g_world.ui.apCtl[GEOUI_CTL_186B00]->x == 224 && g_world.ui.apCtl[GEOUI_CTL_186B00]->y == 1,
              "the shared close control moves to (224,1) with the research thunk on it (0x74AA0, 0x74AAC, 0x74AB8)");
        Check(g_world.ui.apCtl[GEOUI_CTL_186BB4]->x == 165 && g_world.ui.apCtl[GEOUI_CTL_186BB4]->y == 188
                  && g_world.ui.apCtl[GEOUI_CTL_186BB0]->x == 165 && g_world.ui.apCtl[GEOUI_CTL_186BB0]->y == 166,
              "and the two shared bottom controls are positioned only after the topic walk: cancel (165,188), "
              "start (165,166) (0x74B98..0x74BB4)");

        pTopic = g_world.pResearchTopicPanel; pQty = g_world.pResearchQtyPanel;
        pProg  = g_world.pResearchProgPanel;
        Check(pTopic != NULL && pQty != NULL && pProg != NULL, "BuildPanels built the three list panels (0x73428..0x73700)");
        if (pTopic != NULL) {
            CheckEqI32(pTopic->nStyle, PUFO_RESEARCH_PANEL_STYLE, "all three are nStyle 15 (0x73464)");
            CheckEqI32(pTopic->nFont, PUFO_RESEARCH_PANEL_FONT, "over nFont 16, which is also the row pitch (0x73478)");
            CheckEqI32(pTopic->nPosY, PUFO_RESEARCH_PANEL_POSY, "at y 70 (0x734B4)");
            Check(pTopic->nPosX == 10 && pTopic->nViewW == 230 && pTopic->nRectX == 5
                      && pTopic->bSelLineHighlight == 1 && pTopic->pfnClick != NULL,
                  "panel A is the name column: x 10, 230 wide, hit from x 5, its click handler installed (0x7348C..0x7352C)");
            CheckEqI32(pTopic->nLineCount, 0,
                       "and it carries zero rows, because the walk counts bActive topics starting at 1 (0x73034)");
        }
        Check(pQty != NULL && pQty->nPosX == 197 && pQty->nViewW == 30 && pQty->nUnk7076 == 2,
              "panel B is the allocated-quantity column at x 197, right aligned (0x735BC..0x73628)");
        Check(pProg != NULL && pProg->nPosX == 233 && pProg->nViewW == 30 && pProg->nUnk7076 == 2,
              "panel C is the percentage column at x 233 (0x7368C..0x736FC)");
        CheckEqI32(pQty != NULL ? pQty->nLineCount : -1, 0, "both quantity columns start with no rows either (0x7307C, 0x73084)");
        CheckEqI32(pProg != NULL ? pProg->nLineCount : -1, 0, "the percentage column included");
        CheckEqI32(g_world.aTopics[0].bActive, 1,
                   "topic 0 is forced active by BuildPanels and is the block's control slot, not a row -- both walks skip it (0x7341C, 0x73028)");

        g_world.nSelectedBase = 0; /* the quantity column follows nSelectedBaseIdx @ 0x159D81 (0x72F04) */
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("geoscape_research");
        CheckEqI32(g_nBlits, 3,
                   "the no-row frame blits three control pads: ResearchScreen_Draw stops after the close "
                   "(0x73D64) and the arm adds the paging pair (0x94120, CMP #2 at 0x9414C)");
        Check(SawPadBlit(g_world.ui.apCtl[GEOUI_CTL_186B00]->apPads[0])
                  && SawPadBlit(g_world.aResearchCtl[0].apPads[0])
                  && SawPadBlit(g_world.aResearchCtl[1].apPads[0]),
              "the close pad and the two paging pads, at their descriptor positions (224,1), (205,18), (205,38)");
        Check(!SawPadBlit(g_world.aResearchCtl[2].apPads[0]) && !SawPadBlit(g_world.aResearchCtl[3].apPads[0]),
              "and neither quantity pad: 0x73D64 returns before 0x7406C, so the +1 / -1 pair is not on the empty screen");
        Check(pF->researchDraw.apCtlQty[0] == &g_world.aResearchCtl[2]
                  && pF->researchDraw.apCtlQty[1] == &g_world.aResearchCtl[3],
              "the +1 / -1 pair is the draw env's apCtlQty (0x160C58 / 0x160C5C), waiting for a row");

        /* With no row selected the press arm hit-tests the paging pair only (0x88B18, 0x88B3C), so the +1
         * control cannot be armed; and the empty list cannot be clicked, because UiPanel_ClickRow's own
         * nRow <= nLineCount - 1 test fails with no rows (0x3EEDC). */
        nColIdx = PUFO_RESEARCH_TOPIC_COUNT * 0 + 29;
        PressCtl(&g_world.aResearchCtl[2]);
        CheckEqI32(g_world.auResearchCol[nColIdx], 0, "a press on the +1 control with no row selected allocates nothing");
        CheckEqI32(g_world.researchScratch.bFlag160C80, 0, "and the empty list tracks no row (0x88B18, 0x8A794)");

        /* The close, through the control the opener installed. No row is selected, so case 8 takes the
         * teardown arm (0x8BDD8) and BuildPanels' second half runs (0x73944..0x73A28). */
        nSampleMark = g_nSamples;
        PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);
        Check(g_nSamples == nSampleMark + 1 && g_aFrameSamples[nSampleMark] == 17,
              "the close opens with Sound_Play(17, 4) like every other close (0x8B95C)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "case 8's no-row arm writes the id 1 (0x8BDC4)");
        CheckEqI32(g_world.bResearchBuilt, 0, "and runs BuildPanels' teardown arm (0x8BDD8 -> 0x7394C)");
        Check(g_world.apResearchRoot[0] == NULL && g_world.pResearchTopicPanel == NULL
                  && g_world.pResearchQtyPanel == NULL && g_world.pResearchProgPanel == NULL,
              "freeing the pad and then the three panels in the A, B, C order 0x739A4 / 0x739C0 / 0x739DC walks");

        /* The rows the screen is built to show, reached the way the tree reaches them: finishing a topic
         * stores bActive = 1 into the five abActivateTopics bytes at +0x1D (0x74584), and RESEARCH.DAT
         * records 123..127 name topics 29 and 36 there. Shipped data can never reach that store -- the item
         * gate at 0x74364 is the door and it answers 0 for every record -- so the fixture writes exactly the
         * two bytes 0x74584 would have written. Topics 29 and 36 are also the two whose name read stays
         * inside TEXTS.DAT, because their +0x4 is 0. */
        g_world.aTopics[29].bActive = 1;
        g_world.aTopics[36].bActive = 1;
        nSciSave = (i32)g_assets.apObjects[113]->auPerBaseVal[0];
        for (nGridSaveI = 0; nGridSaveI < (i32)sizeof(abGridSave); ++nGridSaveI) {
            abGridSave[nGridSaveI] = g_world.abFacilityGrid[nGridSaveI];
        }
        CheckEqI32(g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * 0
                                          + PUFO_GRID_FAC_TYPE_OFF + 6 * 2 + 5], PUFO_FAC_TYPE_NONE,
                   "the cell the lab fixture takes is empty in the shipped base");
        CheckEqI32(Base_CalcLabCap(g_world.abFacilityGrid, 0), 0,
                   "and base 0 has no laboratory, so its capacity is 0 (0x65818)");

        nSampleMark = g_nSamples;
        g_world.nav.apNav[5]->pfnClick();
        Check(g_nSamples == nSampleMark + 1, "the screen re-opens through the same nav control");
        pTopic = g_world.pResearchTopicPanel; pQty = g_world.pResearchQtyPanel;
        pProg  = g_world.pResearchProgPanel;
        nColIdx = PUFO_RESEARCH_TOPIC_COUNT * 0 + 29;
        CheckEqI32(pTopic != NULL ? pTopic->nLineCount : -1, 2,
                   "with two rows now: BuildPanels counts bActive topics 1..143 (0x73734)");
        if (pTopic != NULL && pTopic->nLineCount == 2 && pQty != NULL && pProg != NULL) {
            CheckEqI32(UiPanel_GetLineAux(pTopic, 0), 29, "row 0 carries the topic number as its aux (0x7378C)");
            CheckEqI32(UiPanel_GetLineAux(pTopic, 1), 36, "and row 1 the next active topic");
            Check(strcmp((char *)pTopic->apLines[0],
                         (const char *)g_world.pAssets->pTextRecs
                             + PUFO_TEXT_REC_STRIDE * g_world.aTopics[29].wGate4) == 0,
                  "the row name is TEXTS[record + 4], the field 0x7373C reads (open row in PLAN.md)");
            Check(*(char *)pQty->apLines[0] == '\0' && *(char *)pProg->apLines[0] == '\0',
                  "both quantity columns start empty: a zero entry gets the empty string (0x7313C), and so does wTotal == 0 (0x731EC)");
        } else {
            Check(false, "the two fixture rows are on the list");
        }

        ClearSurface();
        (void)DrawFrame();
        DumpFrame("geoscape_research_rows");
        Check(SurfPix(10 + 20, 70 + 8) != 0 && SurfPix(10 + 40, 70 + 24) != 0,
              "the name column paints its two rows inside panel A (0x7375C..0x73774)");

        /* The row click, pen first: the press tracks the row (0x88B18), the release's UiPanel_ClickRow stores
         * scroll + row into the panel's own scroll cell and runs its handler (0x3EEF8), and that handler is
         * PlaySample7_SetFlags_Cache0xBD8 @ 0x73328. */
        nSampleMark = g_nSamples;
        World_GeoscapePointer(&g_world, 10 + 20, 70 + 8, true);
        (void)World_GeoscapePointerUp(&g_world);
        Check(g_nSamples == nSampleMark + 1 && g_aFrameSamples[nSampleMark] == 7,
              "the row click plays sample 7 (0x7332C)");
        CheckEqI32(g_world.researchScratch.bFlag160C80, 1, "and sets the row-selected byte unk_160C80 (0x73334)");
        CheckEqI32(g_world.researchScratch.bFlag187080, 0, "with the animation byte cleared (0x73340)");
        CheckEqI32(g_world.researchScratch.nCache160C74, 0,
                   "and caches the panel's scroll cell into unk_160C74 -- the row the quantity pair acts on (0x73350)");

        /* The +1 control's two gates, in the original's order (0x72EC8, then 0x72EEC). Base 0 has no
         * scientist and no laboratory, so the shipped state refuses on its own. */
        CheckEqI32(Base_GetScientistCount(g_assets.apObjects[113], g_world.auResearchCol, 0,
                                          (i32)g_world.nResearchEntries), 0,
                   "Base_GetScientistCount @ 0x6812c is slot 113's stock minus the allocated column");
        PressCtl(&g_world.aResearchCtl[2]);
        CheckEqI32(g_world.auResearchCol[nColIdx], 0, "the scientist gate (0x72EC8) refuses with no scientist at the base");

        g_assets.apObjects[113]->auPerBaseVal[0] = 2; /* as if the base hired two scientists */
        PressCtl(&g_world.aResearchCtl[2]);
        CheckEqI32(g_world.auResearchCol[nColIdx], 0,
                   "and the lab gate still refuses: 1 needed against a capacity of 0 (0x72EEC, 0x662bc, 0x65818)");

        g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * 0
                               + PUFO_GRID_FAC_TYPE_OFF + 6 * 2 + 5] = FAC_LABORATORY;
        g_world.abFacilityGrid[PUFO_GRID_DAYS_STRIDE * 0 + 6 * 2 + 5] = 0;
        CheckEqI32(Base_CalcLabCap(g_world.abFacilityGrid, 0), 50,
                   "a finished laboratory (buildDays 0) is one free lab cell, weighted 50 (0x65890)");
        PressCtl(&g_world.aResearchCtl[2]);
        CheckEqI32(g_world.auResearchCol[nColIdx], 1,
                   "so the +1 stores: the topic's entry in the selected base's column (0x72F58)");

        PressCtl(&g_world.aResearchCtl[3]);
        CheckEqI32(g_world.auResearchCol[nColIdx], 0, "and the -1 takes it back (0x72FE4)");

        PressCtl(&g_world.aResearchCtl[2]);
        CheckEqI32(g_world.auResearchCol[nColIdx], 1, "and the +1 stores again (0x72F58)");

        /* The allocate-all control gets its handler from the draw (0x74184) and its position from the same
         * block (0x74190, 0x7419C), so the button only exists after a row-selected frame has run. */
        ClearSurface();
        (void)DrawFrame();
        Check(g_nBlits > 3, "the row-selected frame blits more than the empty one's three: the +1 / -1 pair "
                            "(0x7406C, 0x74088), the cancel (0x7409C), the start (0x74120), the allocate-all (0x741A4)");
        Check(SawPadBlit(g_world.aResearchCtl[2].apPads[0]) && SawPadBlit(g_world.aResearchCtl[3].apPads[0]),
              "including the two quantity pads, which the empty screen never drew");
        Check(pF->researchDraw.pCtlAllocAll != NULL && pF->researchDraw.pCtlAllocAll->pfnClick != NULL,
              "and it installs ResearchScreen_UpdateProgress on the allocate-all control (0x74184)");
        Check(pF->researchDraw.pCtlAllocAll != NULL
                  && pF->researchDraw.pCtlAllocAll->x == PUFO_RESEARCH_ALLOC_X
                  && pF->researchDraw.pCtlAllocAll->y == PUFO_RESEARCH_ALLOC_Y,
              "re-pointing that control at (100,164) on the way (0x74190, 0x7419C)");

        PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B94]);
        CheckEqI32(g_world.auResearchCol[nColIdx], 2,
                   "so the allocate-all runs ResearchScreen_UpdateProgress @ 0x72d14 and stops at the scientist count: the second iteration sees nSci == 0 (0x72D94)");

        ClearSurface();
        (void)DrawFrame();
        DumpFrame("geoscape_research_allocated");
        Check(SawPadBlit(g_world.aResearchCtl[2].apPads[0])
                  && SawPadBlit(pF->researchDraw.pCtlCancel != NULL ? pF->researchDraw.pCtlCancel->apPads[0] : NULL)
                  && SawPadBlit(pF->researchDraw.pCtlStart != NULL ? pF->researchDraw.pCtlStart->apPads[0] : NULL),
              "the allocated frame shows the quantity pair and the two bottom controls together (0x7406C..0x74120)");

        nSampleMark = g_nSamples;
        PressCtl(g_world.ui.apCtl[GEOUI_CTL_186BB0]); /* pGeoscapeCtlB -> ResearchScreen_StartSelected */
        Check(g_nSamples == nSampleMark + 1 && g_aFrameSamples[nSampleMark] == 17,
              "the start control plays sample 17 (0x73258)");
        CheckEqI32(g_world.aTopics[29].wProgress, 1,
                   "and books the first progress point on a topic that has an allocation and none yet (0x732C8, 0x732D8)");
        CheckEqI32(g_world.researchScratch.bFlag160C80, 0, "clearing the row-selected byte on the way (0x7324C)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 8, "StartSelected does not close the screen");
        if (pQty != NULL && pProg != NULL) {
            Check(strcmp((char *)pQty->apLines[0], "2") == 0,
                  "its RefreshPanels pass writes the allocated count into the quantity row (0x730F8, 0x73108)");
            Check(*(char *)pProg->apLines[0] == '\0',
                  "and leaves the percentage row empty because wTotal is 0 -- the original would divide by it (0x73160, deviation in pufo_baseui.h)");
        }

        /* The cancel arm, and what its tail close really does: CancelSelectedAndClose @ 0x73380 stores 0 into
         * the entry (0x733D8) and tail-calls Geoscape_CloseUI with the row STILL selected, so case 8 takes the
         * StartSelected arm (0x8BDB4) instead of the teardown. */
        World_GeoscapePointer(&g_world, 10 + 20, 70 + 8, true);
        (void)World_GeoscapePointerUp(&g_world);
        CheckEqI32(g_world.researchScratch.bFlag160C80, 1, "the row is selected again (0x7324C cleared it, 0x73334 sets it)");
        PressCtl(g_world.ui.apCtl[GEOUI_CTL_186BB4]); /* pGeoscapeCtlA -> ResearchScreen_CancelSelectedAndClose */
        CheckEqI32(g_world.auResearchCol[nColIdx], 0, "cancel clears the entry with no gate at all (0x733D8)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 8,
                   "but its tail close does NOT close: with the row still selected case 8 takes the "
                   "StartSelected arm, which returns before the id store at 0x8BDC4 (0x8BDB4)");
        CheckEqI32(g_world.bResearchBuilt, 1, "so nothing is torn down and the screen stays open (0x8BDB4)");
        Check(g_world.pResearchTopicPanel != NULL && g_world.pResearchQtyPanel != NULL
                  && g_world.pResearchProgPanel != NULL && g_world.apResearchRoot[0] != NULL,
              "with the pad and all three panels still allocated");
        CheckEqI32(g_world.auResearchCol[nColIdx], 0,
                   "and StartSelected's own run leaves the entry cleared, so the commit is a no-op here");

        World_CloseGeoscape(&g_world);
        CheckEqI32(g_world.bResearchBuilt, 0, "the next close call finds no row selected and takes the teardown arm (0x8BDD8)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "which is the arm that writes the id (0x8BDC4)");
        Check(g_world.apResearchRoot[0] == NULL && g_world.pResearchTopicPanel == NULL
                  && g_world.pResearchQtyPanel == NULL && g_world.pResearchProgPanel == NULL,
              "releasing the pad and the three panels once more");
        for (i = 0; i < 4; ++i) {
            Check(g_world.aResearchCtl[i].apPads[0] == NULL && g_world.aResearchCtl[i].apPads[1] == NULL,
                  "and the four controls' pads, through UiControl_FreePads (0x73A10)");
        }

        /* Back to the shipped state the checks after this one run against: the two active bytes, the progress
         * point, the hired scientists, and every grid lane the start handler's needed-marking passes touched
         * (0x732F0, 0x73304). */
        g_world.aTopics[29].bActive = 0;
        g_world.aTopics[36].bActive = 0;
        g_world.aTopics[29].wProgress = 0;
        g_world.auResearchCol[nColIdx] = 0;
        g_assets.apObjects[113]->auPerBaseVal[0] = (u16)nSciSave;
        for (nGridSaveI = 0; nGridSaveI < (i32)sizeof(abGridSave); ++nGridSaveI) {
            g_world.abFacilityGrid[nGridSaveI] = abGridSave[nGridSaveI];
        }
        CheckEqI32((int)g_world.aTopics[29].bActive, 0, "the fixture is gone");
        CheckEqI32(Base_CalcLabCap(g_world.abFacilityGrid, 0), 0, "and base 0 has no laboratory again");
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
    /* ---- Part 100.2: speeding the game clock up and slowing it down live -------------------------
     * The six speed buttons are the geoscape root controls 2..7 (original cells 0x186500..0x186514).
     * Their press arm is the map screen's own tail: on screen 0 the press tail 0x87CF8..0x87D84
     * hit-ranges all thirteen root controls once the drag chain leaves it alone, and the release arm
     * 0x89CD4..0x89D20 "releases all thirteen root controls" on every screen but 2 (the base strip's
     * arms never touch them). Geoscape_InitUI installed the six handlers GameTime_Set5Sec..
     * GameTime_SetSpeedIdx5 (0x84f00..0x85158): each stores its interval (5/60/300/1380/2460/3600
     * into nGeoscapeTickSeconds @ 0x187070) and posts 1 into its own control's nState, 0 into the
     * other five (the six unrolled stores at 0x84F24..0x84F5C). The clock only advances on the map
     * screen: Geoscape_PulseUpdate (0x95510, 0x95608, 0x95618) runs the craft pass and the
     * nTickSeconds + 1 advance per pulse, which the three pulses just above already proved at the
     * slow speed. The same six pulses here are 21606 game seconds at 3600 and 36 at 5. The section
     * runs at this clean geoscape point (screen 0, empty message list, shortfall 0, unpaused): with a
     * status line carrying flag 11 on line 0 the flag-11 arm (press 0x87938, release 0x899EC) routes
     * the pen to the world's card controls instead, so the speed buttons cannot be pressed at all --
     * 100.1's tail leaves exactly such a line standing, which is why the round runs here. */
    {
        UiControl *const *pSpeed = g_world.ui.apRootCtl + 2; /* the original's 0x186500..0x186514 six */
        i32 nTotal0, nTotal1, nTotal2;

        /* Fastest: the 3600-second button, through the pen. */
        PressCtl(pSpeed[5]);
        CheckEqI32(g_world.geo.world.nTickSeconds, 3600, "GameTime_SetSpeedIdx5 stores 3600 into 0x187070 (0x85124 chain)");
        CheckEqI32((i32)pSpeed[5]->nState, 1, "its own control posts state 1 (0x84F24 chain)");
        CheckEqI32((i32)pSpeed[0]->nState + (i32)pSpeed[1]->nState + (i32)pSpeed[2]->nState
                       + (i32)pSpeed[3]->nState + (i32)pSpeed[4]->nState, 0, "the other five go to 0");
        nTotal0 = g_world.geo.time.nTotalSec;
        for (i32 i = 0; i < 6; ++i) {
            World_PulseGeoscape(&g_world);             /* WM_TIMER 0xC353's geoscape half -> Geoscape_PulseUpdate */
        }
        nTotal1 = g_world.geo.time.nTotalSec;
        CheckEqI32(nTotal1 - nTotal0, 6 * (3600 + 1), "six pulses at this speed are 6*(nTickSeconds+1) game seconds (0x95618)");
        CheckEqI32(g_world.geo.time.nHour, (nTotal1 / GAME_SECONDS_HOUR) % 24, "the decoded hour follows the advanced total (GameTime_Decode)");
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("speed_fast"); /* the pressed fast button and the jumped clock on the status panel */

        /* Slowest again: the 5-second button. */
        PressCtl(pSpeed[0]);
        CheckEqI32(g_world.geo.world.nTickSeconds, 5, "GameTime_Set5Sec stores 5 back into 0x187070 (0x84F24 chain)");
        CheckEqI32((i32)pSpeed[0]->nState, 1, "the slow button now carries the state");
        CheckEqI32((i32)pSpeed[5]->nState, 0, "and the fast one lost it");
        for (i32 i = 0; i < 6; ++i) {
            World_PulseGeoscape(&g_world);
        }
        nTotal2 = g_world.geo.time.nTotalSec;
        CheckEqI32(nTotal2 - nTotal1, 6 * (5 + 1), "the same six pulses now move only 36 game seconds -- the speed control IS the time rate");
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("speed_slow");
        printf("  speed round: 6 pulses at 3600 = %d game seconds, 6 pulses at 5 = %d game seconds\n",
               nTotal1 - nTotal0, nTotal2 - nTotal1);
    }

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
             * 12..16, so an empty cell of the starting base is the one to arm -- and the original's empty
             * cell is the 0xFF sentinel with days 0 (Game_StartNew's stamp at 0x9491C), not kind 0: kind 0 is
             * a facility the player can actually build, and Base_CalcMonthlyUpkeep charges for it. */
            i32 nDefCell = -1;
            for (i32 c = 0; c < 36; ++c) {
                if (g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase0
                                           + PUFO_GRID_FAC_TYPE_OFF + c] == PUFO_FAC_TYPE_NONE
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
                                       + PUFO_GRID_FAC_TYPE_OFF + nDefCell] = PUFO_FAC_TYPE_NONE;
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
            /* The fixture's base has exactly one facility and it is 16 days from finished, so a mission
             * launched here lands on a base with nothing built on it: Smap_InitCraftSelectState skips a cell
             * whose buildDays byte is non-zero (0x61BE8) as well as the 0xFF sentinel (0x61C20), every tile
             * keeps the 0x61BB4 empty value, and Map_BuildMissionTerrain's own guard skips the .rmp of a 0x14
             * tile (0x24150) -- the mission would have no route nodes at all. Run the clock's base tick 16
             * times, which is how the game finishes a facility (Geoscape_AdvanceTime's day loop 0x5CE40 calls
             * Geoscape_TickBases 0x5D3BC, which calls Base_TickFacilityConstruction 0x62824, and that
             * decrements every non-zero buildDays byte once per day). The call is the tick the clock would
             * run, taken on its own so no month, funding or shipping tick rides along. */
            for (i32 nDay = 0; nDay < 16; ++nDay) {
                if (g_world.geoClock.pfnTickBases != NULL) {
                    g_world.geoClock.pfnTickBases(g_world.geoClock.pUser);
                }
            }
            CheckEqI32(g_world.abFacilityGrid[PUFO_GRID_DAYS_STRIDE * nBase0 + 6 * 2 + 2], 0,
                       "16 day ticks of 0x62824 finish the cell (2,2) facility: its buildDays reach 0");

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
            /* The tactical tile grid @ 0x159D5C, built from the grid this test prepares the way Game_StartNew
             * does (0x9485C..0x948EC) and then ages the way the geoscape clock does: base 0's TYPE lane is the
             * 0xFF sentinel (0x9491C) and only cell (2,2) holds a facility, finished by 0x62824 above.
             * Smap_InitCraftSelectState fills the grid with 20 first (0x61BB4), skips the sentinel cells
             * (0x61C20) and stamps the built one with its placement code (0x61C28): kind 1 is code 1. The
             * fixture used to memset the TYPE lane to 0, and 0 is facility kind 0 with placement code 0 --
             * that is what put 35 phantom xbase_0 rooms on the map a later block builds. */
            {
                i32 bTiles = 1;
                for (i32 c = 0; c < 36; ++c) {
                    if (g_world.abMapTileTypes[c] != (c == 14 ? 1 : 20)) { bTiles = 0; }
                }
                Check(bTiles, "35 tiles stay 20 and cell (2,2) takes placement code 1: the sentinel skips an empty cell, a built cell stamps its room");
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
            /* The state a mission launch actually leaves: screen 1, the inventory, over the shell's own
             * selected unit. Part 80 put 0x224AC on the frame seam, so this is a paint the app runs the
             * moment a mission starts -- gated here so a missing env or a NULL Map.pSelUnit shows up as a
             * failed check rather than as a crash in the shell. */
            {
                i32 nTileA = g_world.sTactIso.nTileX;
                i32 nTileB = g_world.sTactIso.nTileY;

                g_world.sTactIso.nTileX = -1;
                g_world.sTactIso.nTileY = -1;
                ClearSurface();
                g_nBlits = 0;
                World_TactFrame(&g_world);
                Check(World_bTactFrameLive(&g_world) && g_world.nUiScreen == 1,
                      "a mission launch leaves screen 1 on the frame seam (0x2237C case 1)");
                Check(g_nBlits > 0 && g_world.sTactIso.nTileX == -1 && g_world.sTactIso.nTileY == -1,
                      "and that paint runs the inventory arm, not the iso layer (0x224AC, never 0x22414)");
                g_world.sTactIso.nTileX = nTileA;
                g_world.sTactIso.nTileY = nTileB;
            }
            Check(g_world.nMapUnk138E6C >= 10 && g_world.nMapUnk138E6C <= 50
                      && g_world.nMapUnk138E70 >= 0 && g_world.nMapUnk138E70 <= 20,
                  "the prologue random pair RandU16(0x28)+10 and RandU16(0x14): each inside its scaled range; the seed is the clock, so the exact pair is not pinned");
            Check(g_world.bMapDrawIsoLayer == 1 && g_world.bMapSweepArmed == 1,
                  "the tail sets the iso-layer byte (Map_bDrawIsoLayer, STRB #1 at 0x3D55C -- 0x3D260 is "
                  "GameSpeed_Set, not this store) and the sweep-armed byte at 0x3D268");
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
                i32 bInstalled = 1, bStart = 1;
                for (i32 c = 0; c < 113; ++c) {
                    if (g_bank.aCtl[c].pfnClick == NULL) { bInstalled = 0; }
                }
                for (i32 c = 113; c < 128; ++c) {
                    if (g_bank.aCtl[c].pfnClick != NULL) { bStart = 0; }
                }
                Check(bInstalled, "every widget 0..112 handler is installed (Parts 54..61 -- the deferred families 56, 57 and 58 closed)");
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

                /* 5: MenuBtn_EndTurn runs the whole enemy-turn bracket on the live frame (0x16494:
                 * sample 17 + Tact_BeginEnemyTurn 0x4ca18, its only original caller). The field is
                 * empty here: Begin sets busy / phase 3 / the enemy-turn byte and runs BeforeEnemyStep
                 * (0x4b9b8); the phase-3 tick finds no unit and takes its tail (0x4D920), and
                 * Tact_EndEnemyTurn (0x4cb74) clears the byte, busy and phase and re-enables input (Part 56). */
                pMapX->apXcomUnits[0]->bUnk1037 = 1;
                pMapX->apXcomUnits[0]->uTu = 1;      /* AfterEnemyStep's refill (0x4C74C) must lift it back to its stat block */
                g_world.bMapPlayerInput = 0;
                g_world.bMapEnemyTurnActive = 9;
                i32 nEnemySave = pMapX->nEnemyCount;  /* the mission really fields units here; empty the field
                                                       * AND the hidden park (BeforeEnemyStep 0x4BA3C would
                                                       * pull reinforcements onto it on this mission type), so
                                                       * the bracket takes its nothing-acted tail */
                i32 nReserveSave = g_world.nMapEnemyReserveCount;
                pMapX->nEnemyCount = 0;
                g_world.nMapEnemyReserveCount = 0;
                nSampleMark = g_nSamples;
                CLICK(5);
                pMapX->nEnemyCount = nEnemySave;
                g_world.nMapEnemyReserveCount = nReserveSave;

                /* 17/18: the hand menus (Tact_OpenLeftHandMenu 0x17030 / Right 0x17148).
                 * 18 with sort key 24 and bPsiActive clear: the weapon IS stored into
                 * Map.pSelWeapon (0x17060, before any gate) but the menu stays shut.
                 * 17 with a category-3 item: mode 3 is offered (cost auStat[0]*20/100,
                 * affordable), so mode 12 + screen 3 land on the live cells. (Part 57) */
                {
                    static TactItem aHandItem;
                    static ObjectRecFull aHandObj;
                    Soldier *pSelH = pMapX->pSelUnit;
                    TactItem *pLSave = pSelH->pHandL, *pRSave = pSelH->pHandR;
                    TactItem *pWepSave = pMapX->pSelWeapon;
                    u8 bPsiSave = pSelH->bPsiActive;
                    i32 nModeSave = g_world.nMapActionMode;
                    i32 nTopic24 = g_abSlotTopicGate[24], nTopic1 = g_abSlotTopicGate[1];
                    u8 bGate24Save = g_world.aTopics[nTopic24].bUnlocked;
                    u8 bGate1Save = g_world.aTopics[nTopic1].bUnlocked;

                    memset(&aHandItem, 0, sizeof(aHandItem));
                    memset(&aHandObj, 0, sizeof(aHandObj));
                    aHandItem.pObj = &aHandObj;
                    pSelH->bPsiActive = 0;
                    g_world.aTopics[nTopic24].bUnlocked = 1;
                    aHandObj.uSortKey = 24;
                    pSelH->pHandR = &aHandItem;
                    nSampleMark = g_nSamples;
                    CLICK(18);
                    Check(g_aFrameSamples[nSampleMark] == 17
                              && pMapX->pSelWeapon == &aHandItem
                              && g_world.nUiScreen == 11 && g_world.nMapActionMode == nModeSave,
                          "widget 18 stores the hand item as Map.pSelWeapon (0x17060) but the psi gate (sort 24, +0x1A6) keeps screen 11");

                    aHandObj.uSortKey = 1;      /* gate 1 unlocked; category 3 arms mode 3 (0x170D0 scan) */
                    aHandObj.nCategory = 3;
                    g_world.aTopics[nTopic1].bUnlocked = 1;
                    pSelH->pHandL = &aHandItem;
                    nSampleMark = g_nSamples;
                    CLICK(17);
                    Check(g_aFrameSamples[nSampleMark] == 17
                              && pMapX->pSelWeapon == &aHandItem
                              && g_world.nUiScreen == 3 && g_world.nMapActionMode == 12,
                          "widget 17 opens the left-hand menu: an offered affordable mode writes Map_nActionMode 12 (0x17138) and screen 3 (0x17140)");

                    pSelH->pHandL = pLSave;
                    pSelH->pHandR = pRSave;
                    pMapX->pSelWeapon = pWepSave;
                    pSelH->bPsiActive = bPsiSave;
                    g_world.aTopics[nTopic24].bUnlocked = bGate24Save;
                    g_world.aTopics[nTopic1].bUnlocked = bGate1Save;
                    g_world.nMapActionMode = nModeSave;
                }
                Check(g_aFrameSamples[nSampleMark] == 17
                          && pMapX->apXcomUnits[0]->bUnk1037 == 0
                          && pMapX->apXcomUnits[0]->uTu > 1
                          && g_world.bMapEnemyTurnActive == 0
                          && pMapX->tact.bBusy == 0 && pMapX->tact.nPhase == 0
                          && g_world.bMapPlayerInput == 1
                          && pMapX->nEnemyCount == 0
                          && pMapX->pSelUnit == pMapX->apXcomUnits[1],
                      "widget 5 runs the bracket: 17 played, BeforeEnemyStep cleared the Xcom 0x40D byte (0x4B9E4), the tail cleared the enemy-turn byte (0x4CB88), busy, and phase, input came back, and the saved selection held");

                /* 56: the deploy confirm opener (Map_BuildDeployConfirmYesNo 0x18a68). The count pass
                 * reads only pCell->nX/nY, nUnitType and a civilian's uHp, so static fixtures drive
                 * the four buckets exactly: craft tile (3,4), craft type 1 -> the rect 30..50 x
                 * 40..50 (the 0x3331C rule), one small Xcom inside, one big Xcom outside, one big
                 * civilian inside, one zero-uHp civilian skipped (0x18C24). (Part 58) */
                {
                    static Soldier aDepX[2], aDepC[2];
                    static MapCell aDepCell[2];
                    static char aszWant1[256], aszWant2[256];
                    const char *pRecs = (const char *)g_world.pAssets->pTextRecs;
                    UiModalState modalSave = g_world.sModal;
                    u8 bTypeSave = g_world.apCraft[g_world.nMapCraftIndex]->bCraftType;
                    u8 bMissionSave = pMapX->nMissionType;
                    u8 bXcomSave = pMapX->nXcomCount;
                    i32 nCivSave = pMapX->nCivilianCount;
                    u8 bCraftXSave = g_world.bMapCraftMapX, bCraftYSave = g_world.bMapCraftMapY;
                    u16 nScreenSave = g_world.nUiScreen;
                    Soldier *pX0Save = pMapX->apXcomUnits[0], *pX1Save = pMapX->apXcomUnits[1];
                    Soldier *pC0Save = pMapX->apCivilians[0], *pC1Save = pMapX->apCivilians[1];

                    memset(aDepCell, 0, sizeof(aDepCell));
                    aDepCell[0].nX = 35; aDepCell[0].nY = 45;   /* inside 30..50 x 40..50 */
                    aDepCell[1].nX = 80; aDepCell[1].nY = 80;   /* outside */
                    memset(aDepX, 0, sizeof(aDepX));
                    memset(aDepC, 0, sizeof(aDepC));
                    aDepX[0].pCell = &aDepCell[0]; aDepX[0].nUnitType = 0;               /* small in  */
                    aDepX[1].pCell = &aDepCell[1]; aDepX[1].nUnitType = 5;               /* big out   */
                    aDepC[0].pCell = &aDepCell[0]; aDepC[0].nUnitType = 6; aDepC[0].uHp = 1; /* big in */
                    aDepC[1].pCell = &aDepCell[0]; aDepC[1].nUnitType = 6; aDepC[1].uHp = 0; /* skipped */

                    g_world.apCraft[g_world.nMapCraftIndex]->bCraftType = 1;
                    pMapX->nMissionType = 0;          /* not the AlienBase(4) whole-map branch */
                    g_world.bMapCraftMapX = 3;
                    g_world.bMapCraftMapY = 4;
                    pMapX->nXcomCount = 2;
                    pMapX->apXcomUnits[0] = &aDepX[0];
                    pMapX->apXcomUnits[1] = &aDepX[1];
                    pMapX->nCivilianCount = 2;
                    pMapX->apCivilians[0] = &aDepC[0];
                    pMapX->apCivilians[1] = &aDepC[1];

                    snprintf(aszWant1, sizeof(aszWant1), "%d%s%d%s%s", 1,
                             pRecs + PUFO_TXTREC_DEPLOY_IN * PUFO_TEXT_REC_STRIDE, 1,
                             pRecs + PUFO_TXTREC_DEPLOY_JOIN * PUFO_TEXT_REC_STRIDE,
                             pRecs + PUFO_TXTREC_DEPLOY_TAIL * PUFO_TEXT_REC_STRIDE);
                    snprintf(aszWant2, sizeof(aszWant2), "%d%s%d%s%s", 0,
                             pRecs + PUFO_TXTREC_DEPLOY_IN * PUFO_TEXT_REC_STRIDE, 1,
                             pRecs + PUFO_TXTREC_DEPLOY_JOIN * PUFO_TEXT_REC_STRIDE,
                             pRecs + PUFO_TXTREC_DEPLOY_TAIL2 * PUFO_TEXT_REC_STRIDE);

                    nSampleMark = g_nSamples;
                    CLICK(56);
                    Check(g_aFrameSamples[nSampleMark] == 16
                              && g_world.sModal.bOpen
                              && g_world.sModal.anRect12_14[0] == 170
                              && g_world.sModal.anRect12_14[1] == 1069
                              && g_world.sModal.anRect12_14[2] == 5
                              && strcmp((const char *)g_world.sModal.pLabel15, "YES") == 0
                              && strcmp((const char *)g_world.sModal.p17, "NO") == 0
                              && g_world.sModal.pfn16 != NULL && g_world.sModal.pfn18 != NULL
                              && g_world.nUiScreen == 9
                              && strcmp(g_world.aszDeployMsg1, aszWant1) == 0
                              && strcmp(g_world.aszDeployMsg2, aszWant2) == 0,
                          "widget 56 counts 1 small in / 1 big in / 0 small out / 1 big out (the zero-uHp civilian skipped, 0x18C24), formats both messages from records 28/29/30 and 28/29/1068, plays 16, opens the YES/NO modal bank at 170/1069 five rows and writes screen 9 (0x18DE0)");

                    g_world.sModal.pfn18();  /* the stored NO callback: Map_ClearUiScreen @ 0x18a54 */
                    Check(g_world.nUiScreen == 0, "the stored NO callback writes Map_nUiScreen = 0 (0x18a54)");

                    g_world.sModal = modalSave;
                    g_world.nUiScreen = nScreenSave;
                    g_world.apCraft[g_world.nMapCraftIndex]->bCraftType = bTypeSave;
                    pMapX->nMissionType = bMissionSave;
                    pMapX->nXcomCount = bXcomSave;
                    pMapX->nCivilianCount = nCivSave;
                    pMapX->apXcomUnits[0] = pX0Save;
                    pMapX->apXcomUnits[1] = pX1Save;
                    pMapX->apCivilians[0] = pC0Save;
                    pMapX->apCivilians[1] = pC1Save;
                    g_world.bMapCraftMapX = bCraftXSave;
                    g_world.bMapCraftMapY = bCraftYSave;
                }

                /* 57: the quit confirm dialog (Menu_ConfirmQuitDialog 0x18e2c -> Menu_OpenQuitDialog):
                 * sample 16, the modal bank at 140/24 four rows, YES/NO, and the DIRECT live screen-8
                 * write (0x18E6C). The YES thunk reuses the app's screen-8 escape seam, so it is only
                 * checked for being stored -- running it would quit the test process. The stored NO
                 * callback (Menu_CancelQuitDialog 0x18e04) is safe and is run. (Part 59) */
                {
                    UiModalState modalSave = g_world.sModal;
                    u16 nScreenSave = g_world.nUiScreen;

                    nSampleMark = g_nSamples;
                    CLICK(57);
                    Check(g_aFrameSamples[nSampleMark] == 16
                              && g_world.sModal.bOpen
                              && g_world.sModal.anRect12_14[0] == 140
                              && g_world.sModal.anRect12_14[1] == 24
                              && g_world.sModal.anRect12_14[2] == 4
                              && strcmp((const char *)g_world.sModal.pLabel15, "YES") == 0
                              && strcmp((const char *)g_world.sModal.p17, "NO") == 0
                              && g_world.sModal.pfn16 != NULL && g_world.sModal.pfn18 != NULL
                              && g_world.nUiScreen == 8,
                          "widget 57 plays 16, opens the modal bank at 140/24 four rows with YES/NO and both callbacks stored, and writes screen 8 direct (0x18E6C)");

                    g_world.sModal.pfn18();  /* Menu_CancelQuitDialog @ 0x18e04: Map_nUiScreen = 0 */
                    Check(g_world.nUiScreen == 0, "the stored NO callback clears the screen (0x18e04)");

                    g_world.sModal = modalSave;
                    g_world.nUiScreen = nScreenSave;
                }

                /* 60: the pen path is live on the mission. A press at the EndTurn pad's centre
                 * (widget 5 of the tact HUD group, 0x14130 HUD band -> 0x14610 whole-group hit-test)
                 * arms the control, and the release half (UiControl_HitTest @ 0x1A540, x=0 y=0
                 * bDown=0) runs the installed handler. Same bracket as the CLICK(5) gate, same
                 * empty-field + empty-park fixture. (Part 60) */
                {
                    TactPointerWorld ptr;
                    PadImage *pHud = g_world.apTactPads[PUFO_UIPAD_HUD];
                    i32 nCx = (i32)g_bank.aCtl[5].x + (i32)g_bank.aCtl[5].uWidth / 2;
                    i32 nCy = (i32)g_bank.aCtl[5].y + (i32)g_bank.aCtl[5].uHeight / 2;
                    u16 nScreenSave2 = g_world.nUiScreen;
                    u8 bLatchSave2 = pMapX->bActionLatch;
                    u8 bFire1Save = pMapX->bFireStatus1, bFire2Save = g_world.bMapFire2;
                    u8 bTrajSave = pMapX->bTrajHitStop;
                    u8 bTurnSave2 = g_world.bMapEnemyTurnActive;
                    i32 nEnemySave2 = pMapX->nEnemyCount;
                    i32 nReserveSave2 = g_world.nMapEnemyReserveCount;

                    Check(pHud != NULL && nCy >= (i32)pHud->nOriginY,
                          "the EndTurn pad centre sits at or below the dpanelv origin, so the HUD band gate (0x14130) routes to the widget group");

                    memset(&ptr, 0, sizeof(ptr));
                    ptr.pBank = &g_bank;
                    ptr.hit = g_hit;

                    pMapX->apXcomUnits[0]->bUnk1037 = 1;
                    pMapX->apXcomUnits[0]->uTu = 1;
                    g_world.nUiScreen = 0;              /* screen 0: the map arm (0x1407C) */
                    pMapX->bActionLatch = 0;            /* whole group 0..18 is hit-tested (0x14610) */
                    pMapX->bFireStatus1 = 0;
                    g_world.bMapFire2 = 0;
                    pMapX->bTrajHitStop = 0;            /* the CanScroll gate (0x143DC/0x14B18) is open */
                    g_world.bMapEnemyTurnActive = 9;
                    pMapX->nEnemyCount = 0;             /* the CLICK(5) fixture again: empty field */
                    g_world.nMapEnemyReserveCount = 0;  /* and empty hidden park */
                    nSampleMark = g_nSamples;

                    World_TactPointerMove(&g_world, &ptr, nCx, nCy, true);  /* WM_LBUTTONDOWN */
                    Check(g_bank.aCtl[5].nState != 0,
                          "the press hit-tests the tact HUD group with real coordinates and arms widget 5");

                    World_TactPointerUp(&g_world, &ptr);                    /* WM_LBUTTONUP */
                    pMapX->nEnemyCount = nEnemySave2;
                    g_world.nMapEnemyReserveCount = nReserveSave2;

                    Check(g_aFrameSamples[nSampleMark] == 17
                              && pMapX->apXcomUnits[0]->bUnk1037 == 0
                              && pMapX->apXcomUnits[0]->uTu > 1
                              && g_world.bMapEnemyTurnActive == 0
                              && pMapX->tact.bBusy == 0 && pMapX->tact.nPhase == 0
                              && g_world.bMapPlayerInput == 1
                              && g_world.nUiScreen == 0,
                          "the release half runs the installed handler through the ported pen path: 17 plays, the bracket runs end to end (0x4ca18..0x4d920), BeforeEnemyStep cleared the 0x40D byte, and screen 0 survives the SyncOut diff flush");

                    g_world.nUiScreen = nScreenSave2;
                    pMapX->bActionLatch = bLatchSave2;
                    pMapX->bFireStatus1 = bFire1Save;
                    g_world.bMapFire2 = bFire2Save;
                    pMapX->bTrajHitStop = bTrajSave;
                    g_world.bMapEnemyTurnActive = bTurnSave2;
                }

                /* 58: MenuBtn_ResizeWindow (0x19930) -- sample 16, then the caller's window seam
                 * (App_ResizeMainWindow @ 0x9ecb8 is the app's own SetWindowPos body; the test
                 * records the seam call instead of resizing a real window). (Part 61) */
                {
                    i32 nResizeSave = g_nResizeCalls;

                    nSampleMark = g_nSamples;
                    CLICK(58);
                    Check(g_aFrameSamples[nSampleMark] == 16 && g_nResizeCalls == nResizeSave + 1,
                          "widget 58 plays 16 (Menu_Play) and tail-calls the window seam once (0x9ecb8 twin)");
                }

                /* --- the inventory close commits the deploy (Part 66) --------------------------
                 * The block opens the inventory (widget 4) so the confirm's screen-1 arm
                 * (0x160FC plays sample 16, 0x16104 calls) runs Inv_CloseAndCommit @ 0x15a48 over the
                 * live mission through the hook the shell installs: snapshot every non-large unit's loadout into auUnk962 / abUnk1012,
                 * clear the Map+0x01 commit flag, then Tact_DeployTeamAndHwps @ 0x39980,
                 * MapView_FocusSelected @ 0x16710, Map_RefreshCellMeta @ 0x2c7f4 and
                 * Unit_UpdateVisibility @ 0x48580 per unit, merge a loose clip into the hand that can
                 * take it, and put Map_nUiScreen to 0. This is the step that spreads the team out:
                 * Tact_SelectTeamUnits @ 0x39744 computes its anchor ONCE (0x3978C..0x3979C) and
                 * 0x3986C places every accepted soldier on it, so the bring-up's eight units share
                 * one cell by the original's own design; phase 3 (0x39E9C) is what re-places the
                 * first (roster - deployed) units onto free cells. */
                {
                    i32 nOccBefore  = FrameCountOccupants();
                    i32 nXcomBefore = pMapX->nXcomCount;
                    i32 nHwpsBefore = g_world.nMapDeployedHwps;
                    i32 i;
                    bool bSpawned = true, bSnapshotted = true;
                    MapCell *pPark = FrameCellAt(1, 1, 0);

                    /* The widget block above moves Map_nUiScreen around, so this block opens the
                     * inventory itself: widget 4 is the installed opener, and screen 1 is what makes
                     * the confirm take the commit arm. */
                    CLICK(4);
                    Check(g_world.nUiScreen == 1, "screen 1 (the inventory this block opened) is the arm's gate");
                    Check(g_world.bMapUnk1139B9 == 1,
                          "Map+0x01 is up on a new mission (Tact_InitMission's Xcom branch, 0x3D08C) -- that byte is what makes the commit do its work");

                    for (i = 0; i < nXcomBefore; ++i) {
                        pMapX->apXcomUnits[i]->auUnk962[0] = 0xABCD;  /* the snapshot must overwrite it */
                    }

                    g_nBankSamples = 0;
                    CLICK(98);   /* MenuBtn_Confirm(16) -> Input_OnConfirm screen-1 arm -> commit */

                    /* The bank recorder sees both 16s: MenuBtn_Confirm's own and the arm's at 0x160FC. */
                    Check(g_nBankSamples >= 2 && g_aBankSamples[0] == 16 && g_aBankSamples[1] == 16,
                          "the button plays 16 and the screen-1 arm plays 16 (0x160FC)");
                    Check(g_world.nUiScreen == 0, "the callee's tail clears Map_nUiScreen (0x15F34)");
                    Check(g_world.bMapUnk1139B9 == 0, "the commit flag is cleared before the deploy runs (0x15C68)");

                    for (i = 0; i < nXcomBefore; ++i) {
                        if (pMapX->apXcomUnits[i]->auUnk962[0] == 0xABCD) { bSnapshotted = false; }
                    }
                    Check(bSnapshotted, "the loadout snapshot ran on the roster units: auUnk962[0] holds a slot's sort key or 0, not the sentinel (0x15BFC..0x15C40)");

                    /* The shipped data spawns no hardware soldiers: every craft-store record 31..35
                     * carries uRecovScore 0, which is the spawn count phase 2 reads at 0x39A24. */
                    for (i = 31; i <= 35; ++i) {
                        if (g_assets.apObjects[i]->uRecovScore != 0) { bSpawned = false; }
                    }
                    Check(bSpawned && pMapX->nXcomCount == nXcomBefore && g_world.nMapDeployedHwps == nHwpsBefore,
                          "no hardware soldier spawns because apObjects[31..35].uRecovScore is 0 in the shipped main.dat (0x39A24 / 0x39F90)");

                    /* Phase 3 (0x39E9C) re-places the first (nXcomUnitCount - Map_nDeployedHwpsCount) units, and
                     * on a base defence mission that arm is Map_PickRmpSpawnForUnit @ 0x55418: a random start
                     * (0x5543C), a forward walk that advances before it uses the index (0x55464), the type gate at
                     * 0x55470 -- a unit whose type byte pUnit[0xA0] is <= 8 requires node byte bFlag20 == 1, which
                     * 0x55524 tests -- and the first node whose cell has no occupant (0x55618) gets
                     * Unit_PlaceOnMap (0x55648). The node list here is the room this base actually has:
                     * Smap_InitCraftSelectState stamped cell (2,2) with placement code 1, Map_BuildMissionTerrain
                     * loaded xbase_1 for that tile, and all eight records of XBASE_1.RMP carry record byte 20 == 1.
                     * Eight soldiers, eight nodes, eight distinct cells: nothing stays on the (1,1,0) cell phase 1
                     * (0x399BC) parked every one of them on, and the occupant census goes from the single park
                     * cell to one per soldier. An earlier reading of this gate had the eight units stacked on the
                     * park cell, and the cause was in the loader rather than here: Map_LoadRmp @ 0x55060 appends
                     * (apRmpNodes[nRmpCount], nRmpCount++ at 0x55208..0x5527C) and the port's version freed the
                     * node list and zeroed the count on every call. */
                    {
                        i32 nNodes  = (i32)g_world.sTactRmp.nNodeCount;
                        i32 nOwn    = 0;
                        i32 nDistinct = 0;
                        bool bEachNode = (nNodes == nXcomBefore);

                        for (i = 0; i < nXcomBefore; ++i) {
                            const MapCell *pC = (pMapX->apXcomUnits[i] != NULL)
                                                    ? pMapX->apXcomUnits[i]->pCell : NULL;
                            bool bOnNode = false;
                            bool bNew    = true;
                            i32 k;

                            if (pC == NULL) { bEachNode = false; continue; }
                            for (k = 0; k < nNodes; ++k) {
                                const RmpNode *pN = g_world.sTactRmp.apNodes[k];

                                if (pN != NULL && pN->nX == pC->nX && pN->nY == pC->nY && pN->nZ == pC->nZ) {
                                    bOnNode = true;
                                    break;
                                }
                            }
                            if (!bOnNode) { bEachNode = false; }
                            for (k = 0; k < i; ++k) {
                                const MapCell *pOther = (pMapX->apXcomUnits[k] != NULL)
                                                            ? pMapX->apXcomUnits[k]->pCell : NULL;

                                if (pOther == pC) { bNew = false; }
                            }
                            if (bNew) { nDistinct += 1; }
                            nOwn += 1;
                        }
                        Check(bEachNode && nOwn == nXcomBefore,
                              "each of the eight soldiers stands on one of the mission's eight route nodes: 0x55524 wants byte 20 == 1 and XBASE_1.RMP gives it to all eight");
                        Check(nDistinct == nXcomBefore,
                              "and each on a cell of its own -- phase 1's park at 0x399BC is emptied, cell by cell, by the re-place walk");
                        Check(!MapCell_HasOccupant(pPark),
                              "the (1,1,0) cell every unit was parked on has no occupant left");
                        Check(FrameCountOccupants() == nOccBefore - 1 + nXcomBefore,
                              "and the occupant census the iso layer reads goes from the single park cell to one per soldier");
                        {
                            i32 nNodes = (i32)g_world.sTactRmp.nNodeCount;
                            i32 nOcc   = FrameCountOccupants();

                            printf("       deploy: nodes=%d mods=%d occupants %d -> %d |",
                                   (int)g_world.sTactRmp.nNodeCount, (int)g_world.sTactRmp.nModCount,
                                   (int)nOccBefore, (int)nOcc);
                            for (i = 0; i < nNodes; ++i) {
                                const RmpNode *pN = g_world.sTactRmp.apNodes[i];

                                if (pN != NULL) { printf(" n%d=%d,%d,%d", i, pN->nX, pN->nY, pN->nZ); }
                            }
                            printf(" |");
                            for (i = 0; i < nXcomBefore; ++i) {
                                const MapCell *pC = (pMapX->apXcomUnits[i] != NULL)
                                                        ? pMapX->apXcomUnits[i]->pCell : NULL;

                                printf(" u%d=%d,%d,%d", i, (pC != NULL) ? (int)pC->nX : -1,
                                       (pC != NULL) ? (int)pC->nY : -1, (pC != NULL) ? (int)pC->nZ : -1);
                            }
                            printf("\n");
                        }
                    }

                    /* Phase 5 (0x3A038): the arm runs -- layers 0 and mission type 3 -- and builds an
                     * empty table. The grid it filters is abFacilityGrid + 0x5F4, which is
                     * abTactTileTypes @ 0x159D5C, rebuilt from the home base by Smap_InitCraftSelectState
                     * @ 0x61B94, and this new base has no tile of the scatter type 7 (0x3A08C CMP #7).
                     * The item walk after it is bounded by nTactItemCount and stops at once: cell
                     * (0,0,0) has no item list. */
                    Check(g_world.nMapScatterCount == 0,
                          "the scatter table is empty: the arm ran (layers 0, mission 3) but no facility-grid tile matches the type-7 test at 0x3A08C");
                }

                /* --- the inventory select keys on a deployed mission (Part 67) -------------------
                 * Screen 1's up/down pair runs the two select seams (Input_OnUp @ 0x11974 ->
                 * Tact_SelectNextUnit @ 0x17304, Input_OnDown @ 0x11A74 -> Tact_SelectPrevUnit @ 0x17260).
                 * Tact_SelectUnit's tail re-places the cursor on the selected unit's OWN cell
                 * (MapCursor_SetPos @ 0x27014 over pUnit->pCell), so the gate needs a roster whose units
                 * carry that link. The deploy put every unit on a cell of its own (the Part 86 gate above), so
                 * the reference here is the selected unit's own cell rather than a parked one, and the cursor
                 * pair (0x15128/0x1512C) is read against it; the same pair is gated on the reloaded mission
                 * below, where the link comes out of the save reader instead. */
                {
                    SeamTrace  s0 = g_seams;
                    MapCell   *pCell0;

                    CLICK(4);
                    Check(g_world.nUiScreen == 1, "the inventory opens for the select arms");
                    pCell0 = (pMapX->pSelUnit != NULL) ? pMapX->pSelUnit->pCell : NULL;
                    Check(pCell0 != NULL && MapCell_HasOccupant(pCell0),
                          "a unit is selected before the keys run, and its own cell link points at a cell that holds an occupant");

                    World_TactKeyDown(&g_world, PUFO_KEY_UP);
                    Check(g_seams.nSelNext == s0.nSelNext + 1,
                          "up runs the installed select-next seam (0x11974 -> 0x17304)");
                    Check(pMapX->pSelUnit != NULL && pMapX->pSelUnit->pCell != NULL
                              && MapCell_HasOccupant(pMapX->pSelUnit->pCell),
                          "the callee landed on a roster unit whose own cell link is set and occupied");
                    CheckEqI32(g_world.tactEnv.cursor.nX,
                               (pMapX->pSelUnit != NULL && pMapX->pSelUnit->pCell != NULL)
                                   ? (i32)pMapX->pSelUnit->pCell->nX : -1,
                               "Tact_SelectUnit's tail put the cursor on the selected unit's cell x");
                    CheckEqI32(g_world.tactEnv.cursor.nY,
                               (pMapX->pSelUnit != NULL && pMapX->pSelUnit->pCell != NULL)
                                   ? (i32)pMapX->pSelUnit->pCell->nY : -1,
                               "and its y: the live pair 0x15128/0x1512C, not the staged copy the dispatch flushes");
                    World_TactKeyUp(&g_world, PUFO_KEY_UP);
                    Check(g_bank.abHeldDir[PUFO_KEY_UP] == 0 && g_seams.nSelNext == s0.nSelNext + 1,
                          "the up release clears only its held byte (0xA0CFC): no second select");

                    World_TactKeyDown(&g_world, PUFO_KEY_DOWN);
                    Check(g_seams.nSelPrev == s0.nSelPrev + 1,
                          "down runs the select-previous seam (0x11A74 -> 0x17260)");
                    Check(g_world.nUiScreen == 1, "the select arms never change the screen id");
                    World_TactKeyUp(&g_world, PUFO_KEY_DOWN);
                    Check(g_bank.abHeldDir[PUFO_KEY_DOWN] == 0, "and the down release clears 0xAD71D");

                    /* Escape closes the inventory the original's way: the release arm is
                     * Input_OnMenuEscape (0xA0DC0), whose case 1/2/4 calls Input_OnConfirm @ 0x163c8 --
                     * the same screen-1 commit arm the confirm button takes. The press has no arm at all
                     * (the WndProc's escape slot is nullsub_1, 0xA0F70). The commit flag is 0 now, so the
                     * deploy half is gated off; only the merge tail runs again. */
                    World_TactKeyDown(&g_world, PUFO_KEY_ESCAPE);
                    Check(g_world.nUiScreen == 1 && g_seams.nConfirm == s0.nConfirm,
                          "the escape press does nothing: the WndProc's escape slot is nullsub_1 (0xA0F70)");
                    World_TactKeyUp(&g_world, PUFO_KEY_ESCAPE);
                    Check(g_seams.nConfirm == s0.nConfirm + 1 && g_world.nUiScreen == 0,
                          "the escape release closes it through the confirm arm (0xA0DC0 -> 0x163c8 -> the screen-1 commit)");
                }

                /* The shell save unit over the live mission (Part 62): World_SaveSlot builds the whole
                 * SaveGameInput from cells this state owns. Distinctive values go into the tact cells the
                 * tgamedata payload records, the slot file is read back, and every compared value comes
                 * from the file. The chunk ORDER SaveGame writes is pinned in tests/test_save.c; this gate
                 * pins the wiring -- which live cell lands in which chunk field. */
                {
                    static const char *const aTactChunks[] = {
                        PUFO_SAVE_TAG_MCNT, PUFO_SAVE_TAG_TGAMEDATA, PUFO_SAVE_TAG_TEAMUNITS,
                        PUFO_SAVE_TAG_ALIENS, PUFO_SAVE_TAG_DIEDSOLDS, PUFO_SAVE_TAG_HIDDENSOL,
                        PUFO_SAVE_TAG_FLOBJS, PUFO_SAVE_TAG_MAP, PUFO_SAVE_TAG_V124,
                        PUFO_SAVE_TAG_OBJBEFORE, PUFO_SAVE_TAG_RMP
                    };
                    WorldHooks saveHooks;
                    DatFile dat;
                    u8 bCount0 = g_world.bMissionCount;      /* byte_138E8C */
                    u8 bPanel0 = g_world.sTactInput.bDeployPanelOpen;
                    u8 bAlt0 = g_world.bMapSpawnAlt;
                    i32 nTileX0 = g_world.nMapSpawnTileX, nTileY0 = g_world.nMapSpawnTileY;
                    i32 nHwp0 = g_world.nMapDeployedHwps;
                    i32 n6C0 = g_world.nMapUnk138E6C, n70C0 = g_world.nMapUnk138E70;
                    i32 nCamX0 = g_world.tactEnv.focus.nCamScrollX;
                    i32 nCamY0 = g_world.tactEnv.focus.nCamScrollY;
                    u8 bPanel = 0, bLay = 0, b115 = 0, bMType = 0, bMode = 0, b11E = 0;
                    u8 aThree[3] = {0}, aSpr[4] = {0}, aTri[3] = {0}, aTile[3] = {0}, bMcnt = 0;
                    i32 nTierRaw = 0, nDep = 0, aFour[4] = {0}, aV124[2] = {0};
                    u8 aStrategSkip[74];   /* version, 4 dwords, 5 type bytes, 13 dwords */
                    i32 aGlobalPtr[2] = {0};
                    /* The clock the mission runs on right now. Save_WriteTime @ 0x40204 reads these cells
                     * directly -- its eight writes have no record in between -- so these are the values the
                     * chunk has to answer with. */
                    i32 nMarkTotalSec = g_world.geo.time.nTotalSec;
                    i32 nMarkTickMin  = g_world.geo.time.nLastTickMinSec;
                    i32 nMarkTickHour = g_world.geo.time.nLastTickHourSec;
                    i32 nMarkTickDay  = g_world.geo.time.nLastTickDaySec;
                    i32 nMarkUnk34    = g_world.geo.time.nUnk34;
                    i32 nMarkMonthSec = g_world.geo.time.nMonthEpochSec;
                    u16 wMarkPool     = g_world.nAlienPoolAddend;
                    u8  bMarkPaused   = g_world.geo.time.bPaused;
                    /* Part 93: Save_WriteStrateg bounds its record loop with the count the geoscape block
                     * keeps at +0x38 -- 0x186530, written into the chunk at 0x3FC14 and read back as the
                     * loop bound at 0x3FE28 and 0x401D4 -- not with the PtrArray's own count. The original
                     * keeps that cell equal to the list at every change (StrategList_AddItem 0x82710,
                     * StrategList_RemoveAt 0x82B34), so this snapshot of the live list is what the chunk
                     * has to match record for record. */

                    g_world.sTactInput.bDeployPanelOpen = 1;    /* 0x15110 */
                    g_world.tactEnv.focus.nCamScrollX = 21;     /* 0x15130 */
                    g_world.tactEnv.focus.nCamScrollY = 23;     /* 0x15134 */
                    g_world.nMapDeployedHwps = 5;               /* 0x15140 */
                    g_world.bMapSpawnAlt = 1;                   /* 0x15148 */
                    g_world.nMapSpawnTileX = 7;                 /* 0x1514C */
                    g_world.nMapSpawnTileY = 9;                 /* 0x15150 */
                    g_world.nMapUnk138E6C = 0x1234;             /* v124.dat, first dword */
                    g_world.nMapUnk138E70 = 0x5678;             /* v124.dat, second dword */

                    /* Marks for the Part 65 load-back at the end of main: what the live mission looks
                     * like right now, to be found again once the readers rebuild it from the file. */
                    g_nMarkXcom   = g_world.pTactMap->nXcomCount;
                    g_nMarkEnemy  = g_world.pTactMap->nEnemyCount;
                    g_nMarkCiv    = g_world.pTactMap->nCivilianCount;
                    g_nMarkHidden = g_world.nMapEnemyReserveCount;
                    g_nMarkItems  = g_world.nTactItemCount;
                    g_nMarkDimX   = (i32)g_world.pTactMap->geom.nDimX;
                    g_nMarkDimY   = (i32)g_world.pTactMap->geom.nDimY;
                    g_nMarkLayers = g_world.pTactMap->nMapLayers;
                    g_pMarkUnit   = g_world.pTactMap->apXcomUnits[0];

                    /* The save's map chunk stores the occupant id at cell+0x30 (written from the cell,
                     * read back at 0x436C0), so the unit has to STAND on a real terrain cell here. The
                     * original gets there through Unit_PlaceOnMap @ 0x49A0C -- eight MapCell_SetOccupant
                     * @ 0x548B8 calls, one per sub-cell of a large unit -- which writes BOTH links AND drops
                     * the cell the unit was standing on. The deploy left every soldier on its own node cell,
                     * so writing the pair by hand here would leave the unit an occupant of two cells, and the
                     * map chunk would write its id into both: the reload's walk scans layer 0 first and would
                     * find the old one. The fixture therefore calls the original's own mover. */
                    if (g_pMarkUnit != NULL) {
                        Unit_PlaceOnMap(g_pMarkUnit, 10, 12, 1, FrameCellLookup, NULL);

                        Check(g_pMarkUnit->pCell == FrameCellAt(10, 12, 1)
                                  && FrameCellAt(10, 12, 1) != NULL
                                  && FrameCellAt(10, 12, 1)->pUnit == g_pMarkUnit,
                              "Unit_PlaceOnMap (0x49A0C) moved the unit to cell (10,12,1) and owns it there");
                    }
                    g_nMarkCellX  = (g_pMarkUnit != NULL && g_pMarkUnit->pCell != NULL) ? g_pMarkUnit->pCell->nX : -1;
                    g_nMarkCellY  = (g_pMarkUnit != NULL && g_pMarkUnit->pCell != NULL) ? g_pMarkUnit->pCell->nY : -1;
                    g_nMarkCellZ  = (g_pMarkUnit != NULL && g_pMarkUnit->pCell != NULL) ? g_pMarkUnit->pCell->nZ : -1;
                    Check(g_pMarkUnit != NULL && g_pMarkUnit->pCell != NULL
                              && g_pMarkUnit->pCell->pUnit == g_pMarkUnit,
                          "the live cell owns the unit it stands on, so Save_WriteMap has an id to write (cell+0x30)");

                    memset(&saveHooks, 0, sizeof(saveHooks));
                    saveHooks.pfnRenderFrame = RecSaveRenderFrame;
                    g_nSaveRenders = 0;

                    g_nMarkStrateg = PtrArray_GetCount(&g_world.strategList);
                    Check(g_nMarkStrateg > 0
                              && g_nMarkStrateg <= (i32)(sizeof(g_aMarkStratType) / sizeof(g_aMarkStratType[0])),
                          "the live strateg list fits the mark the chunk is compared against");
                    for (i = 0; i < g_nMarkStrateg; ++i) {
                        const StrategItem *pSnap = (const StrategItem *)PtrArray_Get(&g_world.strategList, i);

                        g_aMarkStratType[i] = (u8)((pSnap != NULL) ? pSnap->nType : 0xFF);
                        g_aMarkStratX[i]    = (pSnap != NULL) ? pSnap->nX : 0;
                        g_aMarkStratY[i]    = (pSnap != NULL) ? pSnap->nY : 0;
                        g_aMarkStratId[i]   = (pSnap != NULL) ? pSnap->nDisplayId : 0;
                    }
                    for (i = 0; i < (i32)g_world.bCraftCount && i < PUFO_FAC_BASE_COUNT; ++i) {
                        g_aMarkShipHot[i]  = (g_world.apCraft[i] != NULL) ? g_world.apCraft[i]->bWeaponsHot : 0;
                        g_aMarkShipBase[i] = (g_world.apCraft[i] != NULL) ? g_world.apCraft[i]->bAtBase : 0;
                        g_aMarkShipIdx[i]  = (g_world.apCraft[i] != NULL) ? g_world.apCraft[i]->nStrategIdx : -1;
                    }
                    Check(World_SaveSlot(&g_world, "", 62, &saveHooks) != 0,
                          "World_SaveSlot wrote the tactical slot (SaveGame 0x44988 over the live mission)");
                    CheckEqI32(g_nSaveRenders, 1, "the save renders one frame before the file opens (0x4499C)");
                    Check(DatFile_Exists("sav62.sav"), "  sav62.sav was built from the dir and slot");

                    DatFile_ctor(&dat);
                    Check(DatFile_Open(&dat, "sav62.sav", PUFO_DAT_READ, 0), "  reopened for reading");
                    for (i = 0; i < (i32)(sizeof(aTactChunks) / sizeof(aTactChunks[0])); ++i) {
                        char szMsg[80];
                        snprintf(szMsg, sizeof(szMsg), "  the tact branch wrote the %s chunk", aTactChunks[i]);
                        Check(DatFile_FindChunk(&dat, aTactChunks[i]), szMsg);
                    }

                    /* The time chunk, read back against the live clock. Save_WriteTime @ 0x40204 keeps no record
                     * of its own: its eight writes take 0x159618, 0x15961C, 0x159610, 0x15960C, 0x159608,
                     * 0x15963C, 0x159640 and 0x1472C8 straight out of the globals. The port writes through
                     * SaveTimeRecord, and until here the shell filled that record only on the LOAD side -- so
                     * every slot this shell saved recorded an all-zero clock: the load restarted the calendar at
                     * 1999-01-01 00:00 (GameTime_Decode @ 0x5cfd4 bases the year on the saved total) and
                     * SaveSlot_FormatList @ 0x45904 stamped every slot "01.01.1999 00:00". */
                    Check(nMarkTotalSec > 0, "the mission's clock has run, so a zero time chunk is a defect and not a value");
                    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_TIME), "  the time chunk");
                    {
                        u32 uSlot105 = 0, uSlot103 = 0, uSlot102 = 0, uSlot101 = 0, uSlot114 = 0, uSlot115 = 0;
                        u8  bSlot106 = 0;
                        u16 wWord1472C8 = 0;

                        Check(DatFile_Read(&dat, &uSlot105, 4) && uSlot105 == (u32)nMarkTotalSec,
                              "  nTotalSec @ 0x159618 lands in the chunk's first dword");
                        Check(DatFile_Read(&dat, &bSlot106, 1) && bSlot106 == bMarkPaused,
                              "  bGameClockPaused @ 0x15961C lands in the byte");
                        Check(DatFile_Read(&dat, &uSlot103, 4) && uSlot103 == (u32)nMarkTickDay,
                              "  the day-tick stamp @ 0x159610");
                        Check(DatFile_Read(&dat, &uSlot102, 4) && uSlot102 == (u32)nMarkTickHour,
                              "  the hour-tick stamp @ 0x15960C");
                        Check(DatFile_Read(&dat, &uSlot101, 4) && uSlot101 == (u32)nMarkTickMin,
                              "  the minute-tick stamp @ 0x159608");
                        Check(DatFile_Read(&dat, &uSlot114, 4) && uSlot114 == (u32)nMarkUnk34,
                              "  the fifth tick stamp @ 0x15963C");
                        Check(DatFile_Read(&dat, &uSlot115, 4) && uSlot115 == (u32)nMarkMonthSec,
                              "  the month-throttle stamp @ 0x159640");
                        Check(DatFile_Read(&dat, &wWord1472C8, 2) && wWord1472C8 == wMarkPool,
                              "  nAlienPoolAddend @ 0x1472C8, the word the chunk closes with");
                    }

                    /* Part 77: what the tactical save actually carries for the roster. The record's own
                     * geometry is pinned here -- 325 bytes of fixed head, then the 150-byte inventory block
                     * the original writes at 0x429DC..0x42C18 -- because the block's position is what makes
                     * the flagged and unflagged arms interchangeable. */
                    {
                        u8  aSkip325[325];
                        u8  aInv150[150];
                        u8  aFlag;
                        u16 uSoldiers = 0;
                        i32 nFlagged = 0;
                        i32 nIds = 0;
                        i32 nLiveArmed = 0;

                        for (i32 s = 0; s < (i32)pMapX->nXcomCount; ++s) {
                            if (pMapX->apXcomUnits[s] != NULL && pMapX->apXcomUnits[s]->pHandL != NULL) {
                                ++nLiveArmed;
                            }
                        }
                        Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_SOLDIERS)
                                  && DatFile_Read(&dat, &uSoldiers, 2)
                                  && DatFile_Read(&dat, aSkip325, 325)
                                  && DatFile_Read(&dat, aInv150, 150),
                              "  the soldiers record puts its inventory block at offset 325 (0x429DC is where the 27 slot ids start)");
                        for (i32 s = 0; s < 27; ++s) {
                            u32 uId;

                            memcpy(&uId, aInv150 + 4 * s, 4);
                            if (uId != PUFO_SAVE_ID_NONE) { ++nIds; }
                        }
                        if (DatFile_FindChunk(&dat, PUFO_SAVE_TAG_STACTFLG)) {
                            for (i32 s = 0; s < (i32)uSoldiers && s < 64; ++s) {
                                if (DatFile_Read(&dat, &aFlag, 1) && aFlag != 0) { ++nFlagged; }
                            }
                        }
                        Check(uSoldiers > 0 && nFlagged == (i32)uSoldiers,
                              "  every soldier of the mission is flagged in, so the three bInTact arms run on every record");
                        printf("  cargo in this fixture: %d of %d live soldiers armed; soldier 0's 27 slot ids: %d are not 0x7FFF\n",
                               nLiveArmed, (i32)pMapX->nXcomCount, nIds);
                    }

                    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_STRATEG)
                              && DatFile_Read(&dat, aStrategSkip, 74) && DatFile_Read(&dat, aGlobalPtr, 8)
                              && aGlobalPtr[0] == g_world.economy.aZero44_49[0]
                              && aGlobalPtr[1] == g_world.economy.anFundBar[0],
                          "the strateg chunk writes the two globals the original hands as addresses -- "
                          "0x1865A8 (0x3FD44) then 0x186594 (0x3FD54): the save unit wires the shell's own "
                          "funding cells, the pair Save_WriteStrateg refuses without");

                    /* The rest of the header (13 more dwords, 0x3FD64..0x3FE24) sits between the two pointer
                     * writes and the first record, and the dword at chunk offset 13 is the block's +0x38
                     * count -- 0x186530, the value 0x3FE28 loops the records on. */
                    {
                        u8   aStratHead[52];
                        u8   aRec[113];
                        i32  nFileCount = 0;
                        i32  nRec;
                        bool bMatch = true;

                        (void)DatFile_Read(&dat, aStratHead, sizeof(aStratHead));
                        memcpy(&nFileCount, aStrategSkip + 13, 4);
                        Check(nFileCount == g_nMarkStrateg,
                              "the chunk's count dword is the live list's count -- the shell re-states the "
                              "original's +0x38 mirror before the write, and 0x3FE28 / 0x401D4 bound the "
                              "records with that cell rather than the PtrArray's own count");
                        Check(dat.nChunkSize == (u32)(134 + 113 * g_nMarkStrateg),
                              "and the chunk is exactly 134 header bytes plus 113 per item: 38 header writes "
                              "(0x3FBD4..0x3FE24) and 33 field writes per record (0x3FE50..0x401D0)");
                        for (nRec = 0; nRec < g_nMarkStrateg; ++nRec) {
                            i32 nId, nX, nY;

                            if (!DatFile_Read(&dat, aRec, sizeof(aRec))) { bMatch = false; break; }
                            memcpy(&nId, aRec + 16, 4);  /* nDisplayId, the 8th field written */
                            memcpy(&nX,  aRec + 21, 4);  /* nX, the 10th */
                            memcpy(&nY,  aRec + 33, 4);  /* nY, the 13th */
                            bMatch = aRec[67] == g_aMarkStratType[nRec]   /* nType, the 21st field, 0x40080 */
                                       && nId == g_aMarkStratId[nRec]
                                       && nX == g_aMarkStratX[nRec]
                                       && nY == g_aMarkStratY[nRec];
                            if (!bMatch) {
                                break;
                            }
                        }
                        Check(bMatch,
                              "record for record the chunk holds the item's own type, display id and "
                              "coordinates -- the round trip is the live list, not an empty one");
                        printf("  strateg chunk: %d records, %u bytes (count dword %d)\n",
                               g_nMarkStrateg, (unsigned)dat.nChunkSize, nFileCount);
                    }

                    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_MCNT) && DatFile_Read(&dat, &bMcnt, 1)
                              && bMcnt == (u8)(bCount0 + 1),
                          "mcnt is the incremented mission counter (0x44A80 writes the new value)");
                    Check(g_world.bMissionCount == (u8)(bCount0 + 1),
                          "byte_138E8C took the increment back through the unit's i32 mirror");

                    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_TGAMEDATA)
                              && DatFile_Read(&dat, &bPanel, 1) && DatFile_Read(&dat, &nTierRaw, 4)
                              && DatFile_Read(&dat, aThree, 3) && DatFile_Read(&dat, &bLay, 1)
                              && DatFile_Read(&dat, &b115, 1) && DatFile_Read(&dat, aSpr, 4)
                              && DatFile_Read(&dat, &bMType, 1) && DatFile_Read(&dat, &bMode, 1)
                              && DatFile_Read(&dat, &b11E, 1),
                          "tgamedata's first 17 fields read back");
                    Check(bPanel == 1 && nTierRaw == g_world.nMapMissionTierRaw
                              && aThree[0] == g_world.nMapTerrainVariant
                              && aThree[1] == g_world.bMapUnk15112 && aThree[2] == g_world.nMapCraftIndex
                              && bLay == pMapX->nMapLayers && b115 == g_world.bMapUnk15115
                              && memcmp(aSpr, g_world.abMapSpriteSet, 4) == 0
                              && bMType == pMapX->nMissionType && bMode == g_world.bMapMissionMode
                              && b11E == g_world.bMapUnk1511E,
                          "the tgamedata head is the mission's own cells, 0x15110..0x1511E");
                    Check(DatFile_Read(&dat, aFour, 16)
                              && aFour[0] == g_world.tactEnv.cursor.nX
                              && aFour[1] == g_world.tactEnv.cursor.nY
                              && aFour[2] == 21 && aFour[3] == 23,
                          "the four dwords are the cursor pair (0x15128/0x1512C) and the camera pair the "
                          "mission's focus owns (0x15130/0x15134)");
                    Check(DatFile_Read(&dat, aTri, 3) && aTri[0] == g_world.tactEnv.cursor.nFlags
                              && aTri[1] == g_world.bMapCraftMapX && aTri[2] == g_world.bMapCraftMapY,
                          "the three bytes are nLayerScratch and the craft map tile (0x15138/0x15139/0x1513A)");
                    Check(DatFile_Read(&dat, &nDep, 4) && nDep == 5,
                          "the deployed-hwps dword is the mission's running count (0x15140)");
                    Check(DatFile_Read(&dat, aTile, 3) && aTile[0] == 1 && aTile[1] == 7 && aTile[2] == 9,
                          "the three craft-tile dwords are written ONE BYTE each (0x15148/0x1514C/0x15150)");
                    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_V124) && DatFile_Read(&dat, aV124, 8)
                              && aV124[0] == 0x1234 && aV124[1] == 0x5678,
                          "v124.dat is the two scratch dwords (0x448B8..0x448EC)");
                    Check(g_world.nUiScreen == 0,
                          "the tactical branch closed the ui screen (0x44B80), as the driver's port does");

                    DatFile_Close(&dat);
                    /* sav62.sav stays on disk: the Part 65 load-back at the end of main loads it. */

                    g_world.bMissionCount = bCount0;
                    g_world.sTactInput.bDeployPanelOpen = bPanel0;
                    g_world.tactEnv.focus.nCamScrollX = nCamX0;
                    g_world.tactEnv.focus.nCamScrollY = nCamY0;
                    g_world.nMapDeployedHwps = nHwp0;
                    g_world.bMapSpawnAlt = bAlt0;
                    g_world.nMapSpawnTileX = nTileX0;
                    g_world.nMapSpawnTileY = nTileY0;
                    g_world.nMapUnk138E6C = n6C0;
                    g_world.nMapUnk138E70 = n70C0;
                }

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
                /* the tail's AfterEnemyStep now really runs (Part 56): its visibility scan
                 * reads each enemy's cell, so the fakes need valid ones. */
                aEnemyFix[0].pCell   = &pMapE->aCells[0][0][0];
                aEnemyFix[1].pCell   = &pMapE->aCells[0][0][0];
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

            /* --- the tactical frame (Part 63) -------------------------------------------------
             * World_TactFrame builds Render_Frame @ 0x22310's default branch over the live mission:
             * the cursor tick, MapView_DrawIsoLayer @ 0x21e2c with its frame furniture, the modal
             * dialog, the fade block and the status stack, then GX_Present. The fixture is the real
             * mission the earlier blocks built, so the iso layer walks the mission's own grid. */
            {
                GameMap *pMapF    = g_world.pTactMap;
                i32      nScreenKeep = g_world.nUiScreen;
                i32      nSlot1Keep  = g_world.nMapUiSlot1;
                i32      nSlot9Keep  = g_world.nMapUiSlot9;
                i32      nSlot10Keep = g_world.nMapUiSlot10;
                u8       bByte2Keep  = g_world.bMapSlot8Byte2;
                u8       bByte3Keep  = g_world.bMapSlot8Byte3;
                u8       bInputKeep  = g_world.bMapPlayerInput;
                u8       bIsoKeep    = g_world.bMapDrawIsoLayer;
                u8       nFlagsKeep  = g_world.tactEnv.cursor.nFlags;
                i32      nCamXKeep   = g_world.tactEnv.focus.nCamScrollX;
                i32      nCamYKeep   = g_world.tactEnv.focus.nCamScrollY;
                u32      nSlot5Keep  = g_world.sTactInput.nUiSlot5;
                i32      nSlot20Keep = g_world.sTactInput.nSlot20X;
                i32      nSlot21Keep = g_world.sTactInput.nSlot21X;
                i32      nSlot22Keep = g_world.sTactInput.nSlot22Y;
                i32      nSlot23Keep = g_world.sTactInput.nSlot23H;
                i32      nCur112Keep = g_world.sModal.nActionPhaseSave112;
                i32      nProg113Keep= g_world.sModal.nActionPhaseSave113;
                bool     bModalKeep  = g_world.sModal.bOpen;
                i32      nStatCntKeep= pMapF->status.nCount;
                bool     bBusyKeep   = pMapF->tact.bBusy;
                Soldier *pTrackKeep  = pMapF->tact.pTrackUnit;
                u8       bPhaseKeep  = g_world.pTactState->bPhaseActive;
                u8       bSelStatKeep= (pMapF->pSelUnit != NULL) ? pMapF->pSelUnit->bStatus : 0;
                i32      nDimX = pMapF->geom.nDimX;   /* 0x1511B */
                i32      nDimY = pMapF->geom.nDimY;   /* 0x1511C */
                i32      nPadY = g_world.apTactPads[PUFO_UIPAD_HUD]->nOriginY;
                i32      nInkNoMsg, nInkMsg, nInkPad, nInkDark;
                char     szStatKeep[PUFO_STATUS_REC_BYTES];

                memcpy(szStatKeep, pMapF->status.aszMsg[0], sizeof(szStatKeep));

                pMapF->tact.pTrackUnit = NULL;
                g_world.pTactState->bPhaseActive = 0;
                g_world.bMapPlayerInput = 1;
                if (pMapF->pSelUnit != NULL) {
                    pMapF->pSelUnit->bStatus = UNIT_STATUS_OPERATIVE;
                }

                /* The gate. Live on 0x2237C's jumptable group "cases 0,3,5,7-12,15" plus 13, the status
                 * banner arm at 0x223E8, plus 1, 2, 4, 6, 0x13 and 0x14/0x15 -- the arms outside that group
                 * whose envs the shell already owns (0x224AC, 0x224B8, 0x224C4, 0x224D0, 0x223F4, 0x223DC).
                 * Render_Frame calls MapView_DrawIsoLayer with no null test (0x22414) for the group; Part 76
                 * built the group's six furniture envs, Parts 80/81/82/83/84/85 the six replace-arms. Only the
                 * title and menu ids (16..18, the outer cases 0x10..0x12) stay off it. */
                g_world.nUiScreen = 0;
                Check(World_bTactFrameLive(&g_world), "the frame seam is live on screen 0 of a live mission");
                g_world.nUiScreen = 8;
                Check(World_bTactFrameLive(&g_world), "and on 8 (the quit-confirm map screen, 0x18E6C writes it)");
                g_world.nUiScreen = 9;
                Check(World_bTactFrameLive(&g_world), "and on 9, the screen the deploy confirm writes at 0x18DE0");
                g_world.nUiScreen = 3;
                Check(World_bTactFrameLive(&g_world), "and on 3, the roster arm 0x224A0 draws on the iso layer");
                g_world.nUiScreen = 0xC;
                Check(World_bTactFrameLive(&g_world), "and on 0xC, the top bar arm 0x22464 draws on it");
                g_world.nUiScreen = 2;
                Check(World_bTactFrameLive(&g_world), "and on 2, the minimap arm 0x224B8, which replaces the iso layer");
                g_world.nUiScreen = 4;
                Check(World_bTactFrameLive(&g_world), "and on 4, the command-pad arm 0x224C4, which replaces it");
                g_world.nUiScreen = 0x13;
                Check(World_bTactFrameLive(&g_world),
                      "and on 0x13, the promotion-report arm 0x223F4, which replaces it");
                g_world.nUiScreen = 0x14;
                Check(World_bTactFrameLive(&g_world),
                      "and on 0x14, the Cydonia briefing arm 0x223DC, which replaces it (Part 85)");
                g_world.nUiScreen = 0x15;
                Check(World_bTactFrameLive(&g_world), "and on 0x15, the dead twin of the same jumptable entry");
                g_world.nUiScreen = 18;
                Check(!World_bTactFrameLive(&g_world),
                      "not on 18: the outer cases 0x10..0x12 are the title and menu screens, not the tactical frame");
                g_world.nUiScreen = 0;

                /* The cursor tick (0x22360) with the busy arm taken: byte3 = old byte2, byte2 = 1, and
                 * the (0,1) recompute overwrites the pair it just advanced with step -1 / countdown 9. */
                pMapF->tact.bBusy = 1;
                g_world.bMapSlot8Byte2 = 0;
                g_world.bMapSlot8Byte3 = 0;
                g_world.nMapUiSlot9  = 0;
                g_world.nMapUiSlot10 = 5;
                g_nPresents = 0;
                World_TactFrame(&g_world);
                CheckEqI32(g_nPresents, 1, "the frame tail reaches the present seam exactly once (GX_Present 0x27788)");
                CheckEqI32(g_world.bMapSlot8Byte2, 1, "the tick rewrites apUiSlotsA[8] byte 2 as 1 for the busy arm (0x1E8DC)");
                CheckEqI32(g_world.bMapSlot8Byte3, 0, "and byte 3 takes the old byte 2 (0x1E8D4)");
                CheckEqI32(g_world.nMapUiSlot9, -1, "the (byte3 0, byte2 1) case writes step -1 into slot 9 (0x1A7C8)");
                CheckEqI32(g_world.nMapUiSlot10, 9, "and countdown 9 into slot 10");

                /* The iso layer ran over the shell's own camera and counts: the focus pair 0x15130/34,
                 * the layer byte 0x15138, the tile counts 0x34/35. */
                g_world.tactEnv.focus.nCamScrollX = 37;
                g_world.tactEnv.focus.nCamScrollY = 11;
                /* Map_nTileCountX / Map_nTileCountY (Map+0x34/0x35), the iso layer's column and row
                 * bounds. Map_InitFacingScratch produced them at the mission bring-up -- 9 written at
                 * 0x26F48, 24*Map+0x15138/16 + 47 written at 0x26F70 -- and nothing else in the
                 * beta writes either byte. The shell used to poke 6/7 here because it kept a private
                 * copy of these cells that no reader saw; with one owner the produced pair is what the
                 * layer walks over. */
                CheckEqI32(g_world.sTactInput.nTileShiftX, 9, "the init wrote Map_nTileCountX = 9 (0x26F48)");
                CheckEqI32(g_world.sTactInput.nTileShiftY, 24 * 0 / 16 + 47,
                           "and Map_nTileCountY = 24*n/16 + 47 with n = 0: Map+0x15138 is still zero when "
                           "Tact_InitMission reaches the facing-scratch call at 0x3CB30, so 47 is what the "
                           "mission runs on (0x26F70)");
                g_world.bMapDrawIsoLayer = 1;
                ClearSurface();
                g_nBlits = 0;
                World_TactFrame(&g_world);
                CheckEqI32(g_world.sTactIso.nIsoOffA, 37 - 5 * nDimX + 11,
                           "Map_IsoOffsetA reads the focus camera: 37 - 5 * nMapDimX + 11");
                CheckEqI32(g_world.sTactIso.nIsoOffB, 5 * nDimY - 37 + 11 - 1, "Map_IsoOffsetB, same pair");
                CheckEqI32(g_world.sTactIso.nLayer, (i32)g_world.tactEnv.cursor.nFlags,
                           "the layer counter ends at the 0x15138 byte the frame was built with");
                CheckEqI32(g_world.sTactIso.nTileX, 9, "the inner loop ended at the Map+0x34 tile count");
                CheckEqI32(g_world.sTactIso.nTileY, 47, "the outer loop at Map+0x35");
                Check(g_nBlits > 0, "the frame furniture blits through the shell's pad hook (the HUD panel's 19 controls, the look pad)");
                DumpFrame("tact_frame_iso");   /* the picture this seam paints: iso layer + furniture */

                /* Part 86: the mission's route nodes, read off the frame's own state. The one room this base
                 * has contributed eight nodes, and every one of them carries record byte 20 == 1, which is
                 * what Map_PickRmpSpawnForUnit @ 0x55524 asks a unit whose type byte pUnit[0xA0] is <= 8 for;
                 * the deploy gate above is where they get consumed. Map_LoadRmp @ 0x55060 APPENDS the modules
                 * it loads and nothing else in the beta clears the list except TactBuf11408_ctor @ 0x54ff8
                 * (its only code caller is Tact_InitMission at 0x3C630) and Map_FreeRmpNodes @ 0x55018 from
                 * Tact_Shutdown @ 0x3d6a0. */
                {
                    i32 nNodes = (i32)g_world.sTactRmp.nNodeCount;
                    i32 nFlag1 = 0;

                    for (i32 n = 0; n < nNodes; ++n) {
                        const RmpNode *pN = g_world.sTactRmp.apNodes[n];

                        if (pN != NULL && pN->bFlag20 == 1) { nFlag1 += 1; }
                    }
                    CheckEqI32((i32)g_world.sTactRmp.nModCount, 1,
                                "one module carries route nodes: the finished room at tile (2,2), the only tile that is not 0x14");
                    CheckEqI32(nNodes, 8, "XBASE_1.RMP is 192 bytes: eight records off the truncating divide (0x550E8)");
                    CheckEqI32(nFlag1, 8, "and all eight are the byte-20 == 1 kind a soldier's gate accepts");
                    Check(g_world.sTactRmp.nModCount > 0
                              && strcmp(g_world.sTactRmp.aszModName[0], "xbase_1") == 0,
                          "the terrain build named the module off the tactical tile grid (the .rmp load is recorded at 0x550FC)");
                    printf("       rmp: mods=%d nodes=%d byte20==1:%d; tile (2,2) = %d\n",
                           (int)g_world.sTactRmp.nModCount, (int)nNodes, (int)nFlag1,
                           (int)g_world.abMapTileTypes[14]);
                }

                /* The modal arm: the bank's cells in, the pen's slots 20..23 out. Width 120, two
                 * lines, two labels: nX 60, step 6, so 66 / 122 / 165 with the count 2. */
                UiModal_Begin(&g_world.sModal, 120, 3, 2, (void *)"YES", NULL, (void *)"NO", NULL);
                g_world.sModal.nActionPhaseSave113 = 1;   /* the progressive arm, so the cursor moves */
                g_world.sTactInput.nSlot20X = -777;
                g_world.sTactInput.nSlot21X = -777;
                g_world.sTactInput.nSlot22Y = -777;
                g_world.sTactInput.nSlot23H = -777;
                World_TactFrame(&g_world);
                CheckEqI32(g_world.sTactInput.nSlot20X, 66,
                           "apUiSlotsA[20] = nX + (120-100)/3, written by the dialog at 0x266D4");
                CheckEqI32(g_world.sTactInput.nSlot21X, 122, "apUiSlotsA[21] = nX + 2*(step+25) (0x268F0)");
                CheckEqI32(g_world.sTactInput.nSlot22Y, 165, "apUiSlotsA[22] = 13*lines + nY + 10 (0x268E4)");
                CheckEqI32(g_world.sTactInput.nSlot23H, 2, "apUiSlotsA[23] = 2, the two-button arm (0x26AE4)");
                Check(g_world.sModal.nActionPhaseSave112 > 0 && g_world.sModal.nActionPhaseSave112 <= 40,
                      "the typewriter cursor cell advanced inside its 40 wrap (0x26D1C..0x26D48)");
                UiModal_End(&g_world.sModal);

                /* The status stack, at the hud-pad base the four conditions select (0x1B00C..0x1B048)
                 * and one 14-pixel step (0x1B054): the same state twice, only the message differs. */
                pMapF->tact.bBusy = 0;              /* the pad-origin branch wants the idle context */
                g_world.tactEnv.cursor.nFlags = 0;  /* no per-cell glyphs inside the band */
                g_world.sTactInput.nUiSlot5 = 0;    /* no edge bars either */
                g_world.nMapUiSlot1  = 0;
                pMapF->status.nCount = 0;
                ClearSurface();
                World_TactFrame(&g_world);
                nInkNoMsg = 0;
                for (i32 y = nPadY - 14; y < nPadY - 4; ++y) {
                    for (i32 x = 5; x < 100; ++x) {
                        if (SurfPix(x, y) != 0) { ++nInkNoMsg; }
                    }
                }
                pMapF->status.nCount = 1;
                snprintf(pMapF->status.aszMsg[0], sizeof(pMapF->status.aszMsg[0]), "%s", "MMMMMMMMMM");
                ClearSurface();
                World_TactFrame(&g_world);
                nInkMsg = 0;
                for (i32 y = nPadY - 14; y < nPadY - 4; ++y) {
                    for (i32 x = 5; x < 100; ++x) {
                        if (SurfPix(x, y) != 0) { ++nInkMsg; }
                    }
                }
                Check(nInkMsg > nInkNoMsg,
                      "the stack's message is drawn at x 5 / pad origin - 14 (0x1B048, 0x1B0A8)");

                /* The fade gate (0x2250c): with the countdown below 10 the "enemystp" pad goes
                 * full-screen at alpha 24*(10-count) = 120 (0x225B8); with the banner counter up the
                 * whole 240x320 is dark-blitted instead and the pad never runs (0x22538). */
                pMapF->tact.bBusy = 1;              /* the busy arm leaves the countdown at 9 < 10 */
                g_world.bMapSlot8Byte2 = 0;
                g_world.nMapUiSlot1  = 0;
                g_world.nMapUiSlot10 = 5;
                ClearSurface();
                World_TactFrame(&g_world);
                nInkPad = 0;
                for (i32 y = 0; y < 320; ++y) {
                    for (i32 x = 0; x < 240; ++x) {
                        if (SurfPix(x, y) != 0) { ++nInkPad; }
                    }
                }
                g_world.bMapSlot8Byte2 = 0;
                g_world.nMapUiSlot1  = 10;
                g_world.nMapUiSlot10 = 5;
                ClearSurface();
                World_TactFrame(&g_world);
                nInkDark = 0;
                for (i32 y = 0; y < 320; ++y) {
                    for (i32 x = 0; x < 240; ++x) {
                        if (SurfPix(x, y) != 0) { ++nInkDark; }
                    }
                }
                Check(nInkPad > nInkDark,
                      "the no-fade arm blits the enemystp pad (0x225B8); the banner counter dark-blits the screen instead (0x22538)");

                /* Back to the state the mission-end block below expects. */
                g_world.nUiScreen = nScreenKeep;
                g_world.nMapUiSlot1 = nSlot1Keep;
                g_world.nMapUiSlot9 = nSlot9Keep;
                g_world.nMapUiSlot10 = nSlot10Keep;
                g_world.bMapSlot8Byte2 = bByte2Keep;
                g_world.bMapSlot8Byte3 = bByte3Keep;
                g_world.bMapPlayerInput = bInputKeep;
                g_world.bMapDrawIsoLayer = bIsoKeep;
                g_world.tactEnv.cursor.nFlags = nFlagsKeep;
                g_world.tactEnv.focus.nCamScrollX = nCamXKeep;
                g_world.tactEnv.focus.nCamScrollY = nCamYKeep;
                g_world.sTactInput.nUiSlot5 = nSlot5Keep;
                g_world.sTactInput.nSlot20X = nSlot20Keep;
                g_world.sTactInput.nSlot21X = nSlot21Keep;
                g_world.sTactInput.nSlot22Y = nSlot22Keep;
                g_world.sTactInput.nSlot23H = nSlot23Keep;
                g_world.sModal.nActionPhaseSave112 = nCur112Keep;
                g_world.sModal.nActionPhaseSave113 = nProg113Keep;
                g_world.sModal.bOpen = bModalKeep;
                pMapF->status.nCount = nStatCntKeep;
                memcpy(pMapF->status.aszMsg[0], szStatKeep, sizeof(szStatKeep));
                pMapF->tact.bBusy = bBusyKeep;
                pMapF->tact.pTrackUnit = pTrackKeep;
                g_world.pTactState->bPhaseActive = bPhaseKeep;
                if (pMapF->pSelUnit != NULL) {
                    pMapF->pSelUnit->bStatus = bSelStatKeep;
                }
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

            /* --- the debrief status banner and its fade timer (Part 64) ---------------------
             * TactHud_DrawStatusBanner @ 0x1e920 on screen 13, and the WM_TIMER 0xC352 arm at
             * 0xA0990..0xA09AC that ticks the 10-tick fade out through Render_Frame_DecRef @ 0x36818.
             * Two runs of the SAME state -- counter up (banner skipped) and counter down (banner
             * drawn) -- so every difference is the banner's own ink. */
            {
                i32 nFadeKeep  = g_world.nMapUiSlot1;
                i32 nS10Keep   = g_world.nMapUiSlot10;
                i32 nScreenKeep= g_world.nUiScreen;
                const PadImage *pEnd = g_world.apTactPads[PUFO_UIPAD_ENDMISSION];
                i32 nCapW  = Text_MeasureWidth(g_world.aMapMissionCaption);
                i32 nCapX  = 120 - nCapW / 2;                        /* 0x1E9DC..0x1E9F8 */
                i32 nFootW = Text_MeasureWidth((const char *)g_world.pAssets->pTextRecs
                                               + PUFO_DEBRIEF_TEXT_BANNER_FOOTER * PUFO_TEXT_REC_STRIDE);
                static u16 aPadInk[240 * 320];   /* the backdrop's own ink, for the delta probes */
                i32 nSkipCap, nDrawCap, nSkipScore, nDrawScore, nSkipFoot, nDrawFoot;
                i32 nSkipPad, nDrawPad;
                i32 nPadX = -1, nPadY = -1;
                i32 nCapY0 = 10, nCapY1 = 23;    /* the caption box: Text_DrawString at y 10 (0x1E9F8) */
                i32 nLowY0 = 260, nLowY1 = 273;  /* the footer and the score both sit at y 260 */
                i32 nPastX0 = 10 + nFootW + 1;
                i32 nPastX1 = nPastX0 + 20 < 200 ? nPastX0 + 20 : 200;

                /* Backdrop identity: blit the endmiss pad on its own, keep its whole ink as a snapshot,
                 * and remember a pixel it lights. The banner's own blit (0x1E948) is the only thing that
                 * can light the same one in a banner run -- screen 13 draws no iso layer, the modal is
                 * closed, and with the countdown at 10 the fade tail blits neither the enemystp pad nor
                 * the status stack. Every text probe below excludes this ink, so what survives is the
                 * text's own contribution, not the pad sitting underneath it. */
                ClearSurface();
                PadImage_Blit(pEnd);
                for (i32 y = 0; y < 320; ++y) {
                    for (i32 x = 0; x < 240; ++x) {
                        aPadInk[(size_t)y * 240u + (size_t)x] = (u16)SurfPix(x, y);
                        if (nPadX < 0 && SurfPix(x, y) != 0) { nPadX = x; nPadY = y; }
                    }
                }
                Check(nPadX >= 0, "the endmiss pad 0x156A74 lays ink on its own (the fixture's premise)");

                Check(g_world.nUiScreen == PUFO_DEBRIEF_UI_SCREEN && g_world.nMapUiSlot1 == 10,
                      "the mission left the debrief screen up with the banner counter at 10 (0x351EC, 0x351FC)");
                Check(World_bTactFrameLive(&g_world), "screen 13 is on the frame seam (the 0x223E8 arm)");

                g_world.nMapUiSlot1  = 10;
                g_world.nMapUiSlot10 = 10;  /* keeps the fade tail quiet in both runs (0x2250c) */
                ClearSurface();
                g_nPresents = 0;
                World_TactFrame(&g_world);
                CheckEqI32(g_nPresents, 1, "the banner frame presents exactly once");
                nSkipCap  = InkInBox(nCapX, nCapY0, nCapX + nCapW, nCapY1, aPadInk);
                nSkipScore= InkInBox(200, nLowY0, 240, nLowY1, aPadInk);
                nSkipFoot = InkInBox(10, nLowY0, 10 + nFootW, nLowY1, aPadInk);
                nSkipPad  = SurfPix(nPadX, nPadY) != 0;

                g_world.nMapUiSlot1 = 0;   /* 0x1E934: the banner is skipped only while the counter is up */
                ClearSurface();
                World_TactFrame(&g_world);
                nDrawCap   = InkInBox(nCapX, nCapY0, nCapX + nCapW, nCapY1, aPadInk);
                nDrawScore = DiffInBox(200, nLowY0, 240, nLowY1, aPadInk);
                nDrawFoot  = InkInBox(10, nLowY0, 10 + nFootW, nLowY1, aPadInk);
                nDrawPad   = SurfPix(nPadX, nPadY) != 0;
                DumpFrame("debrief_banner");   /* the picture the 0x1e920 arm paints */

                Check(nDrawCap > 0 && nSkipCap == 0,
                      "the caption appears only with the counter down, at 120 - width/2, y 10 (0x1E9DC)");
                Check(InkInBox(0, nCapY0, nCapX, nCapY1, aPadInk) == 0,
                      "and nothing sits left of its centred start");
                Check(nDrawScore > 0 && nSkipScore == 0,
                      "the score text re-colours its own pixels at (200, 260) over the backdrop (0x1EA4C..0x1EAA8)");
                Check(nDrawFoot > 0 && nSkipFoot == 0,
                      "the footer TEXTS[180] (unk_BC8F4, aTextRecs + 205*180) draws at (10, 260) (0x1EB24)");
                Check(InkInBox(nPastX0, nLowY0, nPastX1, nLowY1, aPadInk) == 0,
                      "and stops with the record: past its measured width, before the score column, is bare");
                Check(nDrawPad && !nSkipPad, "the backdrop is the endmiss pad 0x156A74 (0x1E948 PadImage_Blit)");

                /* 0xA0990..0xA09AC: banner counter up AND screen 13, then Render_Frame_DecRef. */
                g_world.nMapUiSlot1 = 10;
                g_nPresents = 0;
                World_TickBannerFade(&g_world);
                CheckEqI32(g_world.nMapUiSlot1, 9, "the 0xC352 arm decrements Map_nBannerFadeTicks (0x3681C)");
                CheckEqI32(g_nPresents, 1, "and draws the frame through Render_Frame_DecRef (0x36828)");
                World_TickBannerFade(&g_world);
                CheckEqI32(g_world.nMapUiSlot1, 8, "the next tick ticks again (the timer runs at 25 ms)");
                g_world.nUiScreen = 0;
                World_TickBannerFade(&g_world);
                CheckEqI32(g_world.nMapUiSlot1, 8,
                          "with any other screen the arm does nothing: the BLEQ at 0xA09AC is conditional");
                g_world.nUiScreen = PUFO_DEBRIEF_UI_SCREEN;
                g_world.nMapUiSlot1 = 0;
                World_TickBannerFade(&g_world);
                CheckEqI32(g_world.nMapUiSlot1, 0, "and at zero the counter stays put (0xA0994 BLE)");

                g_world.nMapUiSlot1 = nFadeKeep;
                g_world.nMapUiSlot10 = nS10Keep;
                g_world.nUiScreen = nScreenKeep;
            }

            /* --- screen 0x13: the promotion report at 0x223F4 (Part 84) ------------------------
             * The fifth arm outside 0x2237C's iso group, and the only one of the five whose opener is a
             * cancel. UiScreen_OnCancel tests screen 13 against Map_bPromotionReport (0x162B8) and writes
             * 19 (0x162D4) INSTEAD of shutting the mission down: the teardown waits for the pad. The
             * backdrop is the same endmiss pad the banner blits (0x156A74); the one control is bank widget
             * 129 (unk_AD6F0 = pBank + 4128, at 46,290 with the pushpu/pushpd pads); the four labels are
             * TEXTS records that the boot record-copy loop mirrors into unk_EACE0 / 0xEAF47 / 0xEADAD /
             * 0xEAE7A -- 1104 "OK" centred on the pad ((uWidth - w)/2 + x, y + 1 at 0x1EB98), 1107
             * "Продвижение" centred at ((240 - w)/2, 10), 1105 "Имя солдата" at (10,30) and 1106
             * "Новое звание" at (170,30). The two columns are the report's own panels, pUiPanel2 @ 0x1568E0
             * (names, left-aligned at x 10) and pUiPanel3 @ 0x1568DC (ranks, right-aligned at x 230), laid
             * out by Tact_InitMission at 0x3C854..0x3CB00 as 14 rows at y 55 + 16 * row, and filled by
             * Tact_ClearSoldierBattleFlags at 0x34E34..0x34EC0 out of aszTextE @ 0xB2200. */
            {
                u16          nScreenKeep  = g_world.nUiScreen;
                u8           nPromoteKeep = g_world.bMapPromotionReport;
                UiPanel     *pName        = g_world.tactEnv.apPanel[2];
                UiPanel     *pRank        = g_world.tactEnv.apPanel[3];
                const PadImage *pEnd      = g_world.apTactPads[PUFO_UIPAD_ENDMISSION];
                UiControl   *pPad         = &g_bank.aCtl[PUFO_HUD_CONFIRM_FIRST];
                const char  *pszOk        = (const char *)g_world.pAssets->pTextRecs
                                              + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_OK;
                const char  *pszTitle     = (const char *)g_world.pAssets->pTextRecs
                                              + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_PROMOTION;
                const char  *pszNameLbl   = (const char *)g_world.pAssets->pTextRecs
                                              + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_PROMOTION_NAME;
                const char  *pszRankLbl   = (const char *)g_world.pAssets->pTextRecs
                                              + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_PROMOTION_RANK;
                static u16   aRepInk[240 * 320];   /* backdrop + control pad: the arm's own two blits */
                Soldier     *apRow[2];
                i32          nRows = 0;
                i32          nSnapBlits, nCtlBlits;
                i32          nTitleW, nTitleX, nOkW, nOkX, nOkY;
                i32          nLblW, nRankLblW;
                i32          nEmptyName, nEmptyRank;
                i32          nFadeKeep, nFadeInKeep;

                /* The fade block at 0x2250C is common to every arm of Render_Frame, this one included, and
                 * the debrief leaves its counter at 10 -- the counter-up branch would re-blit the whole
                 * screen (0x22538) into every census below. The counters are held exactly as Part 64's
                 * banner-drawn run holds them: counter down, fade-in at 10, so the inner test fails and the
                 * block contributes nothing. Neither counter gates this arm; the report paints the same
                 * either way. */
                nFadeKeep   = g_world.nMapUiSlot1;
                nFadeInKeep = g_world.nMapUiSlot10;
                g_world.nMapUiSlot1  = 0;
                g_world.nMapUiSlot10 = 10;

                Check(pName != NULL && pRank != NULL,
                      "the report's two panels are the tactinit pair 0x1568E0 / 0x1568DC (allocated at 0x3C738..0x3C854)");
                Check(pName->nPosX == 10 && pName->nPosY == 55 && pName->nStyle == 14 && pName->nFont == 16,
                      "the name column is laid out left-aligned at (10, 55), 14 rows at 16 pixels (0x3C854..0x3CB00)");
                Check(pRank->nPosX == 230 && pRank->nPosY == 55,
                      "the rank column sits right-aligned at x 230 on the same rows (the layout's align 2)");
                Check(pName->nLineCount == 0 && pRank->nLineCount == 0 && nPromoteKeep == 0,
                      "this debrief earned no rows: Tact_CheckBattleEnd zeroed both panels and left the flag down (0x352C4, 0x352D8)");

                /* The arm's own first two calls, run on their own so the snapshot holds everything the
                 * arm blits and nothing it draws: the endmiss backdrop (0x1EB78) and widget 129 through
                 * the same control rules the arm goes through. Every label probe below excludes this ink. */
                ClearSurface();
                PadImage_Blit(pEnd);
                g_nBlits = 0;
                (void)UiControl_BlitActivePad(pPad, RecBlitPad, RecBlitPadEx, NULL);
                nSnapBlits = g_nBlits;
                for (i32 y = 0; y < 320; ++y) {
                    for (i32 x = 0; x < 240; ++x) {
                        aRepInk[(size_t)y * 240u + (size_t)x] = (u16)SurfPix(x, y);
                    }
                }

                nTitleW = Text_MeasureWidth(pszTitle);
                nTitleX = (240 - nTitleW) / 2;                                     /* 0x1EBB4..0x1EBD0 */
                nOkW    = Text_MeasureWidth(pszOk);
                nOkX    = (i32)pPad->x + ((i32)pPad->uWidth - nOkW) / 2;          /* 0x1EB98 */
                nOkY    = (i32)pPad->y + 1;
                nLblW   = Text_MeasureWidth(pszNameLbl);
                nRankLblW = Text_MeasureWidth(pszRankLbl);

                /* Open it the way the game does: the flag up, then the debrief's cancel. The arm writes 19
                 * and returns -- the teardown is the arm AFTER this one. */
                g_world.bMapPromotionReport = 1;
                Check(World_MissionCancelEnd(&g_world),
                      "the debrief cancel runs on screen 13 with the promotion flag up (0x1626C, 0x162B8 taken)");
                CheckEqI32(g_world.nUiScreen, PUFO_PROMOTION_UI_SCREEN,
                           "and sends the debrief to 19 (0x162D4) instead of shutting the mission down");
                Check(g_world.bTactFlag != 0 && g_world.bTactLive != 0,
                      "no teardown on that arm: Map+0x00 and the live frame survive, the pad has to close the report first");
                Check(World_bTactFrameLive(&g_world),
                      "the report is on the tactical frame seam (0x2237C case 0x13 -> 0x223F4)");

                ClearSurface();
                g_nBlits = 0;
                World_TactFrame(&g_world);
                nCtlBlits = g_nBlits;
                CheckEqI32(nCtlBlits, nSnapBlits,
                           "the report blits exactly its one control -- the pad is there when the control's own rules have a pad for its state");

                /* The label lands on pad art that is already lit, so a lit-pixel count proves nothing
                 * here -- the re-coloured pixels do, the same way the debrief score does at 0x1EA4C. */
                Check(DiffInBox(nOkX, nOkY, nOkX + nOkW, nOkY + 14, aRepInk) > 0,
                      "unk_EACE0 = TEXTS[1104] \"OK\" draws centred on widget 129's pad ((uWidth - w)/2 + x, y + 1)");
                Check(DiffInBox((i32)pPad->x, nOkY, nOkX, nOkY + 14, aRepInk) == 0,
                      "and its centred start leaves the left half of the pad untouched");
                Check(InkInBox(nTitleX, 10, nTitleX + nTitleW, 24, aRepInk) > 0,
                      "unk_EAF47 = TEXTS[1107] \"Продвижение\" draws centred at ((240 - w)/2, 10)");
                Check(InkInBox(0, 10, nTitleX, 24, aRepInk) == 0,
                      "with nothing left of that centre");
                Check(InkInBox(10, 30, 10 + nLblW, 44, aRepInk) > 0,
                      "unk_EADAD = TEXTS[1105] \"Имя солдата\" draws at (10, 30), over the name column's own x");
                Check(InkInBox(170, 30, 170 + nRankLblW, 44, aRepInk) > 0,
                      "unk_EAE7A = TEXTS[1106] \"Новое звание\" draws at (170, 30), over the right-aligned rank column");

                nEmptyName = DiffInBox(10, 55, 240, 55 + 16, aRepInk);
                nEmptyRank = DiffInBox(10, 55 + 16, 240, 55 + 32, aRepInk);
                Check(nEmptyName == 0 && nEmptyRank == 0,
                      "with both panels at zero lines the row band is the backdrop and nothing else (UiPanel_DrawText's nLineCount test)");
                printf("  promotion report: widget 129 at %d,%d width %d; label \"OK\" %d px at %d,%d; title %d px at %d,10; "
                       "headers %d px at 10,30 and %d px at 170,30; empty row band diff %d + %d\n",
                       pPad->x, pPad->y, pPad->uWidth, nOkW, nOkX, nOkY,
                       nTitleW, nTitleX, nLblW, nRankLblW, nEmptyName, nEmptyRank);

                /* Two rows, staged the way 0x34E34..0x34EC0 leaves them: the soldier's own szName in the
                 * name column and that soldier's rank name out of aszTextE, terminator-inclusive byte
                 * copies into apLines[n]. The eligibility walk is the writer's (bInZone, operative); only
                 * the bRankDirty test is missing, because a mission won with an empty field earns none. */
                for (i32 s = 0; s < g_world.nSoldierCount && nRows < 2; ++s) {
                    Soldier *pS = g_world.apSoldiers[s];

                    if (pS == NULL || pS->bInZone == 0 || !Unit_bIsOperative(pS)) {
                        continue;
                    }
                    apRow[nRows] = pS;
                    ++nRows;
                }
                for (i32 r = 0; r < nRows; ++r) {
                    const char *pszRank = g_textTables.pE[apRow[r]->bRankType];
                    i32 i = 0;

                    for (;;) { pName->apLines[r][i] = (u8)apRow[r]->szName[i]; if (apRow[r]->szName[i] == 0) { break; } ++i; }
                    i = 0;
                    for (;;) { pRank->apLines[r][i] = (u8)pszRank[i]; if (pszRank[i] == 0) { break; } ++i; }
                }
                pName->nLineCount = nRows;
                pRank->nLineCount = nRows;
                Check(nRows == 2, "the report is staged with two rows, one name and one rank each (0x34E34, 0x34E74)");

                ClearSurface();
                World_TactFrame(&g_world);
                for (i32 r = 0; r < nRows; ++r) {
                    i32 nNameW = Text_MeasureWidth((const char *)pName->apLines[r]);
                    i32 nRankW = Text_MeasureWidth((const char *)pRank->apLines[r]);
                    i32 nY0 = 55 + 16 * r;

                    Check(DiffInBox(10, nY0, 10 + nNameW, nY0 + 14, aRepInk) > 0,
                          "the name row draws left-aligned at the panel's x, 16 pixels per row");
                    Check(DiffInBox(230 - nRankW, nY0, 230, nY0 + 14, aRepInk) > 0,
                          "the rank row draws right-aligned at the panel's x");
                    printf("  promotion report: row %d \"%s\" / \"%s\" (rank type %d) at y %d, %d control blits\n",
                           r, (const char *)pName->apLines[r], (const char *)pRank->apLines[r],
                           apRow[r]->bRankType, nY0, nCtlBlits);
                }
                DumpFrame("tact_hud_promotion");

                for (i32 r = 0; r < nRows; ++r) {
                    pName->apLines[r][0] = 0;
                    pRank->apLines[r][0] = 0;
                }
                pName->nLineCount = 0;
                pRank->nLineCount = 0;
                g_world.bMapPromotionReport = nPromoteKeep;
                g_world.nUiScreen = nScreenKeep;
                g_world.nMapUiSlot1  = nFadeKeep;
                g_world.nMapUiSlot10 = nFadeInKeep;
                ClearSurface();
                World_TactFrame(&g_world);
            }

            /* Arm one inventory link right here, after every frame of Parts 62-64 is already on disk:
             * the link has to exist going into the teardown and must not come out of it. The original
             * re-establishes the whole inventory block from the soldiers chunk on the next load
             * (Save_ReadSoldiers 0x419e8..0x42300, the InvPocketPair linking loops -- a recorded gap),
             * while the shell keeps the soldier records alive across the item pool's free (they stay
             * owned by apSoldiers), so a stale pointer into the freed pool has to be cleared here. */
            if (g_pMarkUnit != NULL && g_world.nTactItemCount > 0) {
                TactItem *pArmItem = g_world.apTactItems[0];
                g_pMarkUnit->pHandL = pArmItem;
                g_pMarkUnit->pHandR = pArmItem;
                g_pMarkUnit->apBackpack[0] = pArmItem;
                g_pMarkUnit->apBelt[0] = pArmItem;
                g_pMarkUnit->aPockets[0].apItem[0] = pArmItem;
            }

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
    Check(g_pMarkUnit == NULL || (g_pMarkUnit->pCell == NULL
                                  && g_pMarkUnit->pHandL == NULL && g_pMarkUnit->pHandR == NULL
                                  && g_pMarkUnit->apBackpack[0] == NULL
                                  && g_pMarkUnit->apBelt[0] == NULL
                                  && g_pMarkUnit->aPockets[0].apItem[0] == NULL),
          "the unit records that outlive the freed cells and the freed item pool come out of the teardown "
          "with no pointer into either (World_ClearUnitLinks; the original leaves Soldier+0x10 dangling -- "
          "only 0x49B2C and 0x419DC write it -- and rebuilds the inventory from the soldiers chunk instead)");
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

    /* --- the mission loads back (Part 65) ------------------------------------------------------
     * The very slot the save unit wrote over the live mission. World_LoadSlot keeps the file open
     * across the driver's Tact_InitMission(1) -- 0x44E24 sits before DatFile_Close at 0x44E2C -- and
     * the mission is rebuilt out of it: tgamedata, teamunits, aliens, diedsolds, hiddensold, flobjs,
     * map and rmp. Compare against the marks taken while it was live. */
    {
        GameMap *pMapL;
        i32 nBlitsBefore;
        /* Map+0x34/0x35 (IDA: nTileCountX 0x1139EC, nTileCountY 0x1139ED) have no writer anywhere in the
         * beta: their only cross-references are the four Input_On* scroll clamps and MapView_DrawIsoLayer's
         * two cell passes, all reads, and the image bytes are zero. Save_ReadMap restores the map DIMS only.
         * So the counts are a cell the shell carries, not one the load produces -- the fixture carries them
         * across the load exactly as Part 63 carries them across its teardown, and the gap is recorded. */
        i32 nRet;

        /* The app loads a saved mission from the geoscape with no tactical mission live, and that is the
         * state this run is already in: Part 64's teardown dropped the map record, the tactical state and
         * every cell. The load therefore has to rebuild the grid itself -- the checks below are the load's
         * own work, not leftovers. */
        Check(g_world.pTactMap == NULL, "no map record stands before the load (Part 64's teardown)");
        {
            i32 nBefore = 0, k;

            for (k = 0; k < 14400; ++k) {
                if (g_world.apTactCells[k] != NULL) {
                    ++nBefore;
                }
            }
            CheckEqI32(nBefore, 0, "no cell of the previous mission survives into the load");
        }

        nRet = World_LoadSlot(&g_world, "", 62, &g_assets, &g_fontSys, &g_options, &hooks);
        Check(nRet != 0, "World_LoadSlot loaded the tactical slot (LoadGame 0x44bbc over sav62.sav)");

        /* Part 93: the restored list IS the saved list, item for item. That is the whole point of the
         * +0x38 count mirror: Save_WriteStrateg bounds its record loop with that cell (the loop tests at
         * 0x3FE28 and 0x401D4), and the original re-states it from PtrArray_GetCount on every add
         * (0x82710) and every removal (0x82B34). A shell that leaves the cell at its last-read value writes
         * too few records, and the load then has nothing to restore -- which is what LoadGame's per-craft
         * find-or-create (0x44F7C..0x45014) is for: it gives every craft an entry when the list has none.
         * Here 0x44FB0 finds each craft its own record instead, and 0x45008 never runs. */
        {
            i32 nMark   = g_nMarkStrateg;
            i32 nNow    = PtrArray_GetCount(&g_world.strategList);
            bool bSame  = (nNow == nMark);
            bool bCraft = true;
            bool bArmA  = true;   /* 0x45074..0x4508C */
            bool bArmB  = true;   /* 0x450B8..0x450CC */
            i32  nType0 = 0;
            i32  nDeArmed = 0;
            i32  j;

            for (j = 0; j < nNow && j < nMark; ++j) {
                const StrategItem *pItem = (const StrategItem *)PtrArray_Get(&g_world.strategList, j);

                if (pItem == NULL || pItem->nType != g_aMarkStratType[j]
                    || pItem->nDisplayId != g_aMarkStratId[j]
                    || pItem->nX != g_aMarkStratX[j] || pItem->nY != g_aMarkStratY[j]) {
                    bSame = false;
                }
                if (pItem != NULL && pItem->nType == 0) {
                    ++nType0;
                }
            }
            Check(bSame,
                  "every saved item came back with its own type, display id and coordinates -- Save_ReadStrateg "
                  "empties the list first (0x40F68) and then reads the chunk's own count (0x411CC, 0x41594), so "
                  "the two sides agree only because the writer counted the same list");
            Check(nNow == nMark,
                  "and the list did not grow: the per-craft loop ran 0x44FB0 for every craft and found one, so "
                  "the add at 0x45008 never ran and the find-or-create stayed the fallback it is");
            Check(nType0 == (i32)g_world.nBaseCount,
                  "one inactive type-0 entry per base stands in the reloaded geoscape (Base_CreateNamedEntry "
                  "adds it at 0x669A8 on every create) -- the pass StrategList_UpdateDetect runs its base loop "
                  "on (0x813D0) and the select test's base-name branch (0x82E94) both have a basis again");

            /* The pairing the two passes look up: BaseRec.bUnk4 @ +0x04 is the site id BaseRec_Create took from
             * the next-site-id global (0x556B8, the same dword 0x66A34 stores into the site record), and the
             * base-name branch matches THAT against the entry's nSiteId with a signed read (0x82ED4). */
            printf("  bases and their entries:");
            for (j = 0; j < (i32)g_world.nBaseCount && j < PUFO_FAC_BASE_COUNT; ++j) {
                const BaseRec *pBase = g_world.apBases[j];
                printf(" base %d site %d cell %d,%d", j,
                       (pBase != NULL) ? (i32)(i8)pBase->bUnk4 : -1,
                       (pBase != NULL) ? pBase->nUnk8 : 0, (pBase != NULL) ? pBase->nUnk12 : 0);
            }
            printf("\n");
            for (j = 0; j < nNow; ++j) {
                const StrategItem *pItem = (const StrategItem *)PtrArray_Get(&g_world.strategList, j);
                if (pItem != NULL && pItem->nType == 0) {
                    printf("  entry %d: type 0 active %d visible %d site %d id %d at %d,%d\n", j,
                           (i32)pItem->bActive, (i32)pItem->bVisible, pItem->nSiteId, pItem->nDisplayId,
                           pItem->nX, pItem->nY);
                }
            }
            /* The pairing, measured on the reloaded world. BaseRec.bUnk4 @ +0x04, the entry's nSiteId and
             * BaseSiteRec.nFacType are the same counter read three times before 0x66B50 raises it
             * (BaseRec_ctor 0x556C0, 0x669A8, 0x66A34), and every lookup matches on it:
             * Base_FindIndexByTypeByte @ 0x6ac30 -- the copy Base_FindIndexBySiteId @ 0x5a8d0 shares --
             * answers the base-name branch (0x82E94 through the signed compare at 0x82ED4), the options
             * walk (0x6D12C), and the placement commit, which finds the entry by the base's byte (0x8B598)
             * and moves it to the new cell (0x8B5E4 / 0x8B5F8). That last one is why the entry of a base
             * this run placed stands where the base stands. */
            bSame = true;
            for (j = 0; j < (i32)g_world.nBaseCount && j < PUFO_FAC_BASE_COUNT; ++j) {
                const BaseRec     *pPairBase = g_world.apBases[j];
                const StrategItem *pPairItem = NULL;
                const StrategItem *pOther;
                i32 nSite, k, nShares = 0;

                if (pPairBase == NULL) {
                    bSame = false;
                    continue;
                }
                nSite = (i32)(i8)pPairBase->bUnk4;
                if (Base_FindIndexByTypeByte(g_world.apBases, (i32)g_world.nBaseCount,
                                             (u8)pPairBase->bUnk4) != j) {
                    bSame = false;  /* two bases would answer the same site id */
                }
                for (k = 0; k < nNow; ++k) {
                    pOther = (const StrategItem *)PtrArray_Get(&g_world.strategList, k);
                    if (pOther != NULL && pOther->nType == 0 && pOther->nSiteId == nSite) {
                        ++nShares;
                        pPairItem = pOther;
                    }
                }
                if (nShares != 1 || pPairItem == NULL || pPairItem->nX != pPairBase->nUnk8 ||
                    pPairItem->nY != pPairBase->nUnk12 ||
                    g_world.aSiteRecs[j].nFacType != nSite) {
                    bSame = false;
                }
            }
            Check(bSame, "base, map entry and site record carry one site id each, and the entry stands on "
                  "its base's cell -- the save round trip restores BaseRec.bUnk4 (Save_WriteBases 0x3F400, "
                  "Save_ReadBases 0x40558) and the placement already moved the entry, so the base-name "
                  "branch finds the name instead of an empty row");

            for (j = 0; j < (i32)g_world.bCraftCount && j < PUFO_FAC_BASE_COUNT; ++j) {
                const Ship        *pShip = g_world.apCraft[j];
                const StrategItem *pItem;
                i32  nIdx;
                bool bOffMap;

                if (pShip == NULL) {
                    continue;
                }
                /* The finder the load uses: StrategList_FindIdxForShip @ 0x6A05C returns the first item whose
                 * nDisplayId equals apCraft[n]->bId, with NO type gate and NO active gate (unlike
                 * StrategList_FindByShipId @ 0x5A9B4). Its result is the index the two arms below then work
                 * on; 0x45018..0x450E8 never writes Ship.nStrategIdx, so the craft keeps the stale index the
                 * ships chunk gave it (Save_ReadShips 0x40BA8). */
                nIdx  = FindStrategById(&g_world.strategList, (i32)pShip->bId);
                pItem = (const StrategItem *)PtrArray_Get(&g_world.strategList, nIdx);
                bCraft = bCraft && pItem != NULL && pItem->nType == 1 && pItem->nDisplayId == (i32)pShip->bId
                           && pShip->nStrategIdx == g_aMarkShipIdx[j];
                bOffMap = (pItem != NULL
                           && (pItem->nX > 1000 || pItem->nX < -1000 || pItem->nY > 1000 || pItem->nY < -1000));
                if (bOffMap && g_aMarkShipHot[j] != 0) {
                    bArmA = bArmA && pShip->bWeaponsHot == 0 && pShip->bAtBase == 1;
                    ++nDeArmed;
                }
                if (pShip->bAtBase != 0) {
                    bArmB = bArmB && pItem != NULL && pItem->bVisible == 0;
                }
            }
            Check(bCraft,
                  "each craft is matched to the entry whose nDisplayId is its own bId, and its nStrategIdx is "
                  "still the value the save wrote: 0x44F7C..0x450E8 reads the craft record at +0x34 and +6 and "
                  "never stores to +0x38, so a loaded craft keeps a stale target index");
            Check(bArmA,
                  "and a craft saved hot whose entry sits off the map came back disarmed and back at base "
                  "(the arm at 0x45074 clears bWeaponsHot @ 0x4507C and sets bAtBase @ 0x45088)");
            Check(bArmB,
                  "and every craft the load leaves standing at base has its entry made invisible again "
                  "(0x450B8 tests bAtBase and 0x450C4 clears bVisible)");
            printf("  restored strateg list: %d items, %d of them the base entries; %d craft disarmed by 0x45074\n",
                   nNow, nType0, nDeArmed);
        }
        Check(g_world.bTactLive && g_world.bTactFlag == 1,
              "the driver's Tact_InitMission(1) ran and the mission is live again (0x44E24)");
        pMapL = g_world.pTactMap;
        Check(pMapL != NULL && g_world.pTactState != NULL,
              "the bring-up's two heap records are back (GameMap and pMapTactical)");
        if (pMapL != NULL) {
            CheckEqI32((i32)pMapL->geom.nDimX, g_nMarkDimX, "Save_ReadMap restored the map width (Map_nMapDimX)");
            CheckEqI32((i32)pMapL->geom.nDimY, g_nMarkDimY, "and the height");
            CheckEqI32(pMapL->nMapLayers, g_nMarkLayers,
                      "Map_nMapLayers @ 0x15114 lands through the apply hook, before Font_LoadTactSprites reads it (0x4444C)");
            CheckEqI32(pMapL->nXcomCount, g_nMarkXcom, "the Xcom roster count, from Save_ReadTeamUnits");
            CheckEqI32(pMapL->nEnemyCount, g_nMarkEnemy, "the alien roster count");
            CheckEqI32(pMapL->nCivilianCount, g_nMarkCiv, "the civilian count");
            Check(g_pMarkUnit != NULL && pMapL->apXcomUnits[0] != NULL
                      && pMapL->apXcomUnits[0] == g_pMarkUnit,
                  "the same Soldier record stands in the roster again -- the readers re-link the saved ids into apSoldiers");
            /* The link the map chunk really restores: Save_ReadMap reads a saved id and stores it at
             * cell+0x30 (0x436C0), so the CELL side is the parity check, and the cell's own
             * coordinates are derived from the walk, not stored (0x434DC).
             * The SOLDIER's own link (Soldier+0x10) is restored by Save_ReadSoldiers @ 0x419A8..0x419DC:
             * three saved coordinates index apCells as 60 * (60 * z + y) + x -- an INDEX, so it needs the
             * grid Init_LoadMission just allocated (0x3D168..0x3D1C0) but NOT the coordinates the map
             * chunk derives later (0x434DC). Order on this path: soldiers at 0x3D22C, the map chunk inside
             * Save_ReadTGameData at 0x3D234 -> 0x444B4. */
            {
                MapCell *pFound = NULL;
                i32 nZ, nY2, nX2;

                for (nZ = 0; nZ < 4 && pFound == NULL; ++nZ) {
                    for (nY2 = 0; nY2 < PUFO_MAP_CELL_DIM && pFound == NULL; ++nY2) {
                        for (nX2 = 0; nX2 < PUFO_MAP_CELL_DIM; ++nX2) {
                            MapCell *pCell = g_world.apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * nZ
                                                                 + PUFO_MAP_CELL_DIM * nY2 + nX2];

                            if (pCell != NULL && pCell->pUnit == g_pMarkUnit) {
                                pFound = pCell;
                                break;
                            }
                        }
                    }
                }
                {
                    i32 nCells[4] = {0, 0, 0, 0};

                    for (nZ = 0; nZ < PUFO_MAP_LAYERS; ++nZ) {
                        for (nY2 = 0; nY2 < PUFO_MAP_CELL_DIM; ++nY2) {
                            for (nX2 = 0; nX2 < PUFO_MAP_CELL_DIM; ++nX2) {
                                if (g_world.apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * nZ
                                                        + PUFO_MAP_CELL_DIM * nY2 + nX2] != NULL) {
                                    ++nCells[nZ];
                                }
                            }
                        }
                    }
                    /* Init_LoadMission has to allocate every column: the original's 0x3D168 loops reset
                     * the column register per row and step the table pointer +4 per column and +0x3840
                     * (3600 dwords) per layer. The "map" chunk writes one record per slot, so a slot left
                     * NULL makes Save_ReadMap read the records that belong to other slots. */
                    Check(nCells[0] == 3600 && nCells[1] == 3600 && nCells[2] == 3600 && nCells[3] == 3600,
                          "the loaded grid allocates 60x60 cells on all four layers (0x3D168..0x3D1C0)");
                }
                Check(pFound != NULL,
                      "Save_ReadMap put the unit back into some terrain cell (cell+0x30, 0x436C0)");
                if (pFound != NULL) {
                    /* The coordinates the walk derived for that cell, against the coordinates the unit
                     * was standing on when the slot was written. */
                    CheckEqI32((i32)pFound->nX, g_nMarkCellX, "the cell's x, derived by the walk (0x434DC)");
                    CheckEqI32((i32)pFound->nY, g_nMarkCellY, "and y");
                    CheckEqI32((i32)pFound->nZ, g_nMarkCellZ, "and the layer");
                }
                Check(g_pMarkUnit != NULL && pFound != NULL && g_pMarkUnit->pCell == pFound,
                      "and the soldier's OWN link -- Soldier+0x10, restored from the soldiers chunk at 0x419DC -- names the very cell the map chunk put the occupant on: two chunks, one cell");
            }
        }
        CheckEqI32(g_world.nMapEnemyReserveCount, g_nMarkHidden, "the reserve roster (unk_138DF0) count");
        CheckEqI32(g_world.nTactItemCount, g_nMarkItems, "the flobjs item count (Save_AllocFlobjs + Save_ReadFlobjs)");
        {
            const Soldier *pSelL = (pMapL != NULL) ? pMapL->pSelUnit : NULL;
            const MapCell *pSelCell = (pSelL != NULL) ? pSelL->pCell : NULL;

            Check(g_world.sTactSave.anUnk15188[2] == 21 && g_world.sTactSave.anUnk15188[3] == 23,
                  "the saved camera pair comes back into the staged tgamedata record (anUnk15188[2]/[3] = "
                  "Map_nCamScrollX/Y @ 0x15130/0x15134, Save_ReadTGameData @ 0x44248)");
            Check(pSelCell != NULL,
                  "the selected unit stands on a cell at load time, so the resume tail has something to centre on");
            if (pSelCell != NULL) {
                CamOffset co;
                i32 nA;
                i32 nB;

                MapView_SetCamOffset(&pMapL->geom, (i32)g_world.tactEnv.focus.nMapTileW >> 1,
                                     (i32)g_world.tactEnv.focus.nMapTileH >> 1, false, 0, &co);
                nA = pSelCell->nX - (i32)(i8)co.bScratch40;
                nB = pSelCell->nY - (i32)(i8)co.bScratch41;
                Check(g_world.tactEnv.focus.nCamScrollX
                              == Map_CenterOffsetX((i32)g_world.tactEnv.focus.nDimX, nA, nB)
                          && g_world.tactEnv.focus.nCamScrollY == Map_CenterOffsetY(nA, nB),
                      "and the live camera is the re-centred one, not the saved pair: Save_ReadTGameData runs at "
                      "0x3D234 and the load chain's tail is MapView_FocusSelected at 0x3D248 (0x16710), which puts "
                      "0x15130/0x15134 on the selected unit's cell through the SetCamOffset scratch and "
                      "Map_CenterOffsetX/Y (0x276F0, 0x26FCC/0x26FFC)");
                Check(!(g_world.tactEnv.focus.nCamScrollX == 21 && g_world.tactEnv.focus.nCamScrollY == 23),
                      "the saved camera is overwritten rather than left in place -- before the 0x419DC link was "
                      "restored, MapView_FocusOnCell took its NULL early-return and the saved 21/23 survived by "
                      "accident; that gate was measuring the missing link, not the resume tail");
            }
        }
        Check(g_world.sTactInput.nTileShiftX == 9 && g_world.sTactInput.nTileShiftY == 47,
              "the loaded mission keeps the iso bounds the first bring-up wrote: Map_InitFacingScratch guards "
              "on the facing CHANGING (Map+0x36 @ 0x26F28), so Tact_InitMission's second call re-writes "
              "nothing at 0x26F48/0x26F70");
        Check(g_world.nMapDeployedHwps == 5 && g_world.bMapSpawnAlt == 1
                  && g_world.nMapSpawnTileX == 7 && g_world.nMapSpawnTileY == 9,
              "0x15140, 0x15148, 0x1514C and 0x15150 come back too");
        Check(g_world.nMapUnk138E6C == 0x1234 && g_world.nMapUnk138E70 == 0x5678,
              "v124.dat was present, so the stored dwords are used rather than rolled (0x445D4..0x446A0)");
        /* Only the cells the reset block writes. bMapMissionVictory is tgamedata's own byte
         * (anActionPhaseSave0[92]) and 0x444C0..0x445CC never touches it, so the check stays off it. */
        Check(g_world.bMapBattleEnded == 0 && g_world.bMapEnemyTurnActive == 0
                  && g_world.bMapPlayerInput == 1,
              "the tactical reset ran on the live cells: the battle bytes clear and bPlayerInputEnabled is SET to 1 (0x444C0..0x445CC)");
        CheckEqI32(g_world.nMapUiSlot10, 10, "apUiSlotsA[10] comes back holding the value 10");
        Check(g_world.nUiScreen == 0, "Map_nUiScreen is cleared to 0 by the same reset");

        /* The picture the rebuild paints: the frame seam over the mission the readers brought back. */
        Check(World_bTactFrameLive(&g_world), "the reloaded mission sits on the tactical frame seam");
        /* Part 97: the layer is not ours to turn on. Tact_InitMission clears Map_bDrawIsoLayer @ 0x138E68 at
         * 0x3C5C0 and its return tail sets it at 0x3D55C, and LoadGame runs that whole bring-up at 0x44E24 -- so
         * the mission the save rebuilt already has the iso layer on. Render_Frame only reads the byte (0x22404,
         * 0x225C4); the only other writer is Inv_OpenScreen, which puts 0 on the inventory screen. */
        Check(g_world.bMapDrawIsoLayer == 1,
              "the bring-up left the iso layer on for the reloaded mission (0x3C5C0 clears, 0x3D55C sets, "
              "LoadGame runs the bring-up at 0x44E24)");
        ClearSurface();
        nBlitsBefore = g_nBlits;
        World_TactFrame(&g_world);
        Check(g_nBlits > nBlitsBefore,
              "the iso chain runs over the reloaded mission: the cell passes and the unit glyphs blit");
        DumpFrame("tact_loaded_mission");

        /* --- Part 97: the tactical view the mission really shows --------------------------------
         * Two camera states the game itself produces over the map the readers rebuilt: the centring the
         * load's resume tail put on the selected unit (MapView_FocusSelected @ 0x16710, run at 0x3D248) and
         * the same focus moved to another unit of the squad. The gate byte, the window pair and the size
         * pair are only checked here, never written. */
        {
            static u16 aView0[240 * 320];
            static u16 aSnap0[240 * 320];
            Soldier *pOther = NULL;
            i32 nCamX0 = g_world.tactEnv.focus.nCamScrollX;
            i32 nCamY0 = g_world.tactEnv.focus.nCamScrollY;
            i32 nBand0, nBand1, nMoved, nReveal0, nReveal1, nUnitPx;
            i32 nIsoA0, nIsoB0;
            i32 i2;

            Check(g_world.tactEnv.tileSize.nTileW == PUFO_TILE_W
                      && g_world.tactEnv.tileSize.nTileH == PUFO_TILE_H,
                  "Map_InitFacingScratch's size pair Map+0x30/0x32 = 240/320 (STRH at 0x26F50, 0x26F60) is the "
                  "one the mission runs with -- the iso offsets and the focus scratch both read it");

            for (i2 = 0; i2 < pMapL->nXcomCount && pOther == NULL; ++i2) {
                Soldier *pU = pMapL->apXcomUnits[i2];

                if (pU != NULL && pU != pMapL->pSelUnit && pU->pCell != NULL) {
                    pOther = pU;
                }
            }
            Check(pMapL->pSelUnit != NULL && pMapL->pSelUnit->pCell != NULL && pOther != NULL,
                  "the squad came back with a selected unit on a cell and at least one other unit on a cell, "
                  "so the view can be re-centred the way a press on that unit does");

            /* What the reloaded grid actually holds, before anything is drawn. */
            {
                i32 nCells = 0, nFloor = 0, nOcc = 0;

                for (i2 = 0; i2 < PUFO_MAP_LAYERS; ++i2) {
                    for (i32 yy = 0; yy < PUFO_MAP_CELL_DIM; ++yy) {
                        for (i32 xx = 0; xx < PUFO_MAP_CELL_DIM; ++xx) {
                            const MapCell *pC = g_world.apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * i2
                                                                    + PUFO_MAP_CELL_DIM * yy + xx];

                            if (pC == NULL) { continue; }
                            ++nCells;
                            if (pC->pFloorSpr != NULL) { ++nFloor; }
                            if (pC->pUnit != NULL) { ++nOcc; }
                        }
                    }
                }
                nReveal0 = FrameCountRevealed();
                printf("  the reloaded grid: %d cells, %d with a floor sprite, %d with an occupant, %d revealed\n",
                       nCells, nFloor, nOcc, nReveal0);
                Check(nFloor > 14000 && nOcc == pMapL->nXcomCount,
                      "the map chunk restored the terrain and put every squad member on a cell: 14 379 floor "
                      "sprites and 8 occupants on the 14 400-cell grid (Save_ReadMap's records, 0x434DC walk)");
                Check((pMapL->pSelUnit->pCell->bFlags44 & 1u) == 0,
                      "and the selected unit's own cell is NOT revealed, which is why the frame above draws no "
                      "unit: MapView_DrawUnit's gate reads the drawn cell's byte +0x2C and skips the unit when "
                      "bit 0 is clear (0x1CA50 LDR, 0x1CA54 LDRB [R3,#0x2C], 0x1CA58 ANDS #1, 0x1CA5C BEQ "
                      "0x1D5E4), and Tact_InitMission's two visibility calls at 0x3CE54 and 0x3D10C run BEFORE "
                      "Save_ReadTeamUnits reads the roster at 0x3D234, so on a load they walk an empty squad");
            }

            /* Snapshot the same frame WITHOUT the refresh, so the unit's own pixels can be measured later. */
            ClearSurface();
            World_TactFrame(&g_world);
            for (i2 = 0; i2 < 320; ++i2) {
                for (i32 x = 0; x < 240; ++x) {
                    aSnap0[(size_t)i2 * 240u + (size_t)x] = (u16)SurfPix(x, i2);
                }
            }

            /* The game's own refresh: Unit_UpdateVisibility @ 0x48580 once per squad member, the call
             * Tact_RefreshTeamVisibility @ 0x4f368 makes per unit (and UnitList_ForEachRefresh @ 0x22b54 per
             * roster, reached from AfterEnemyStep at 0x4C930). It is the pass that puts bFlags44 bit 0 on the
             * cells MapView_DrawUnit admits. In THIS mission the pass adds one cell, not hundreds: every unit
             * stands in a room, and the ray fan stops at the first cell whose ceiling is lofted -- the ceiling
             * id test at 0x48F90..0x48FE4 (Desert 6 allows ceiling ids 0x1B..0x41, Forest 2 allows 0x1B..0x40
             * plus 82, any other mission type blocks every ceiling) -- so a ray reveals the cell it starts in
             * and stops at the room's ceiling. */
            for (i2 = 0; i2 < pMapL->nXcomCount; ++i2) {
                if (pMapL->apXcomUnits[i2] != NULL) {
                    (void)Unit_UpdateVisibility(pMapL->apXcomUnits[i2], &g_world.tactEnv.vis);
                }
            }
            Check((pMapL->pSelUnit->pCell->bFlags44 & 1u) != 0,
                  "the refresh revealed the selected unit's cell: the only writes of cell byte +0x2C bit 0 in "
                  "Unit_UpdateVisibility @ 0x48580 are 0x48688/0x486F0/0x48750 (a large unit's other footprint "
                  "cells), 0x487DC (down its column), 0x4885C (up its column), 0x48E04 (the cell a ray starts "
                  "in), 0x48E48/0x48EC0 (up/down the column a ray entered -- the port's Unit_RevealColumn), and "
                  "0x493A0 (an enemy the LOS trace reached)");
            {
                i32 nOwnUnlit = 0;

                for (i2 = 0; i2 < pMapL->nXcomCount; ++i2) {
                    if (pMapL->apXcomUnits[i2] != NULL && pMapL->apXcomUnits[i2]->pCell != NULL
                        && (pMapL->apXcomUnits[i2]->pCell->bFlags44 & 1u) == 0) {
                        ++nOwnUnlit;
                    }
                }
                Check(nOwnUnlit == 0,
                      "after the game's own refresh no squad member stands on an unrevealed cell, so every one "
                      "of them is admissible to MapView_DrawUnit's gate at 0x1CA54");
                printf("  members on an unrevealed cell after the refresh: %d\n", nOwnUnlit);
            }
            printf("  the view the layer draws: tile window %dx%d, %d layers, camera %d,%d, "
                   "iso pair %d,%d, selected cell %d,%d,%d\n",
                   g_world.sTactInput.nTileShiftX, g_world.sTactInput.nTileShiftY,
                   g_world.tactEnv.cursor.nFlags, g_world.tactEnv.focus.nCamScrollX,
                   g_world.tactEnv.focus.nCamScrollY, g_world.sTactIso.nIsoOffA,
                   g_world.sTactIso.nIsoOffB, pMapL->pSelUnit->pCell->nX,
                   pMapL->pSelUnit->pCell->nY, pMapL->pSelUnit->pCell->nZ);

            /* Both the readers of the mission grid have to agree on its order or the terrain lands
             * transposed under the units. Save_ReadMap walks 10*dimY rows x 10*dimX columns and stores
             * the row counter as nY and the column counter as nX, into the slot its own index math
             * builds; MapView_bResolveIsoCell reads the same inline table back with nMapX in the row
             * position. */
            {
                const MapCell *pSel = pMapL->pSelUnit->pCell;
                i32 nIdxRowX = PUFO_TERRAIN_CELLS_PER_LAYER * (i32)pSel->nZ
                             + PUFO_MAP_CELL_DIM * (i32)pSel->nX + (i32)pSel->nY;
                i32 nIdxRowY = PUFO_TERRAIN_CELLS_PER_LAYER * (i32)pSel->nZ
                             + PUFO_MAP_CELL_DIM * (i32)pSel->nY + (i32)pSel->nX;

                Check(g_world.apTactCells[nIdxRowY] == pSel && g_world.apTactCells[nIdxRowX] != pSel,
                      "the mission grid is [layer][nY][nX]: Save_ReadMap's index is Map+0x15338 + "
                      "240*(i + row) + 4*col -- 0x4349C adds the row counter to i (which steps 60 per "
                      "layer at 0x43440) and 0x434A0 multiplies by 0xF0, 0x43880 adds 4 per column -- and "
                      "the counters land as nX at 0x434DC / nY at 0x434E0, while MapView_bResolveIsoCell "
                      "indexes apCells[60*(60*layer + nMapX) + nMapY] with nMapX bounded by 10*nMapDimY");
            }
            nReveal1 = FrameCountRevealed();
            printf("  after the refresh: revealed cells went from %d to %d\n", nReveal0, nReveal1);
            Check(nReveal1 > nReveal0,
                  "the pass adds ground rather than only the selected unit's cell, and the count is small "
                  "because of the ceiling rule, not a stalled fan: seven members' own cells came back revealed "
                  "in the map chunk, and a facing's pass is 31 rays of the 120 (Tact_BuildVisRayTable @ 0x37AE4 "
                  "seeds pass p on rays 15p+k for k=0..30)");

            ClearSurface();
            World_TactFrame(&g_world);
            nIsoA0 = g_world.sTactIso.nIsoOffA;
            nIsoB0 = g_world.sTactIso.nIsoOffB;
            nBand0 = InkInBox(0, 0, 240, 240, NULL);
            for (i2 = 0; i2 < 320; ++i2) {
                for (i32 x = 0; x < 240; ++x) {
                    aView0[(size_t)i2 * 240u + (size_t)x] = (u16)SurfPix(x, i2);
                }
            }
            nUnitPx = DiffInBox(40, 40, 200, 220, aSnap0);
            printf("  the centred view: camera %d,%d iso %d/%d, ink over the map band %d of %d, "
                   "new pixels where the revealed unit stands %d\n", nCamX0, nCamY0, nIsoA0, nIsoB0,
                   nBand0, 240 * 240, nUnitPx);
            Check(nUnitPx > 40,
                  "the squad member is drawn once its cell is revealed: the sprite and its selection arrow "
                  "(pPadSelSold, blitted with bFlag 0xFF at 0x1D5D8) change the centred area against the same "
                  "frame before the refresh");
            Check(nBand0 > 40000,
                  "the terrain pass paints the map band, not a strip of it: the 9 x 47 window over the real "
                  "camera covers most of the 240 x 240 area above the HUD");
            /* The state this picture holds: the camera the load's own resume tail put on the selected
             * unit's cell, and that cell revealed (bFlags44 bit 0) but NOT seen. MapView_bResolveIsoCell
             * gives a cell whose bFlags29 bit 4 is clear the palette 255, which Font_BlitGlyphToSoft
             * clamps to 19 at 0x9EC58, while a seen cell draws with its own palette (bUnk56, or 0 when
             * nFlags32 & 7 is 4) -- so the ground under the soldier is a fallback-palette floor and the
             * lit corner along the bottom is the cells the save carried with bit 4 set. The soldier is
             * drawn because its cell passes 0x1CA54, which tests bFlags44 alone. */
            DumpFrame("tact_view_centred");

            MapView_FocusSelected(&g_world.tactEnv.focus, pOther->pCell, g_world.bMapSweepArmed,
                                  &g_world.tactEnv.cursor);
            Check(g_world.tactEnv.cursor.nFlags == (i32)(pOther->pCell->nZ + 1),
                  "MapCursor_SetFlags @ 0x26F78 puts the layer byte Map+0x15138 on the new cell's layer + 1");
            Check(g_world.tactEnv.focus.nCamScrollX != nCamX0 || g_world.tactEnv.focus.nCamScrollY != nCamY0,
                  "the focus put 0x15130/0x15134 on the other unit's cell (Map_CenterOffsetX/Y @ 0x26FCC/0x26FFC)");

            ClearSurface();
            World_TactFrame(&g_world);
            nBand1 = InkInBox(0, 0, 240, 240, NULL);
            nMoved = DiffInBox(0, 0, 240, 320, aView0);
            printf("  the re-centred view: camera %d,%d iso %d/%d (was %d/%d), ink %d, picture moved %d px\n",
                   g_world.tactEnv.focus.nCamScrollX, g_world.tactEnv.focus.nCamScrollY,
                   g_world.sTactIso.nIsoOffA, g_world.sTactIso.nIsoOffB, nIsoA0, nIsoB0, nBand1, nMoved);
            Check(g_world.sTactIso.nIsoOffA != nIsoA0 || g_world.sTactIso.nIsoOffB != nIsoB0,
                  "the layer walk changed with the camera: MapView_DrawIsoLayer re-reads Map_IsoOffsetA/B @ "
                  "0x26F88/0x26FA8 (camX - 5*nMapDimX + camY, 5*nMapDimY - camX + camY - 1) at 0x21E5C/0x21E84");
            Check(nBand1 > 40000, "and the terrain still covers the band at the second camera");
            Check(nMoved > 5000, "the terrain and the units moved with the camera rather than staying put");
            DumpFrame("tact_view_refocus");
        }

        /* --- screen 1: the inventory arm at 0x224AC (Part 80) -------------------------------
         * 0x2237C routes case 1 to 0x224AC, never through 0x22414, so this arm draws NO iso layer: the
         * "invv" backdrop pad (Map+0x100), the 13 panel controls at pBank + 0x260 = bank widgets 19..31,
         * the burden line (TEXTS[55]), the unit header, the item strip and the unit portrait from the
         * 32-slot manual pad set at Map+0x199C. The mission is the reloaded one, whose selected unit the
         * resume tail just re-centred the camera on. */
        {
            u16 nScreenSave = g_world.nUiScreen;
            i32 nInv;

            /* Open it the way the game does: widget 4 carries TactHud_OpenInventory (0x16ef4, Part 55),
             * so the filter bytes this screen reads are the opener's own writes, not a fixture's. */
            g_bank.aCtl[4].pfnClick();
            CheckEqI32(g_world.nUiScreen, 1, "the installed opener writes Map_nUiScreen 1 (0x16FD8)");
            Check(g_world.sTactUi.bFilterPadAll != 0 && g_world.sTactUi.bFilterPadX == 0
                      && g_world.sTactUi.abFilterPad[0] == 0 && g_world.sTactUi.abFilterPad[1] == 0
                      && g_world.sTactUi.abFilterPad[2] == 0 && g_world.sTactUi.abFilterPad[3] == 0
                      && g_world.sTactUi.abFilterPad[4] == 0,
                  "and its pad writes are All lit with the six others dark (0x16F68, 0x16F78..0x16F98)");
            Check(World_bTactFrameLive(&g_world),
                  "the inventory screen is on the tactical frame seam now (0x2237C case 1 -> 0x224AC)");
            g_world.sTactIso.nTileX = -1;                /* the iso layer's own loop bounds */
            g_world.sTactIso.nTileY = -1;
            ClearSurface();
            g_nBlits = 0;
            World_TactFrame(&g_world);
            nInv = g_nBlits;
            Check(g_world.sTactIso.nTileX == -1 && g_world.sTactIso.nTileY == -1,
                  "and the arm runs with no iso layer at all: MapView_DrawIsoLayer (0x22414) is off this path");
            DumpFrame("tact_hud_inventory");

            /* Which blits belong to the filter row: the state byte is the one the opener lit, so
             * clearing it has to take the pad with it. Six of the seven filter widgets are dark by
             * the opener's own write, so they draw nothing either way -- lighting one of them has to
             * ADD a blit. */
            {
                u8 bAllSave = g_world.sTactUi.bFilterPadAll;
                u8 abSave[PUFO_INV_FILTER_PADS];
                i32 nInvDark;
                i32 nInvLit;

                memcpy(abSave, g_world.sTactUi.abFilterPad, sizeof(abSave));

                g_world.sTactUi.bFilterPadAll = 0;
                ClearSurface();
                g_nBlits = 0;
                World_TactFrame(&g_world);
                nInvDark = g_nBlits;

                g_world.sTactUi.bFilterPadAll = bAllSave;
                g_world.sTactUi.abFilterPad[0] = 1;
                ClearSurface();
                g_nBlits = 0;
                World_TactFrame(&g_world);
                nInvLit = g_nBlits;

                g_world.sTactUi.bFilterPadAll = bAllSave;
                memcpy(g_world.sTactUi.abFilterPad, abSave, sizeof(abSave));
                ClearSurface();                  /* re-paint, so the bank holds the restored state */
                g_nBlits = 0;
                World_TactFrame(&g_world);

                printf("  filter row: %d blits lit, %d with All cleared, %d with pad 0x1A25 too\n",
                       nInv, nInvDark, nInvLit);
                CheckEqI32(nInv - nInvDark, 1,
                           "the All pad (bank widget 24, 0xAC9DA) blits because the mirror put the byte in the bank");
                CheckEqI32(nInvLit - nInv, 1,
                           "and lighting abFilterPad[0] (bank 25, 0xAC9FA) adds its own pad");
            }
            {
                i32 bMirror = 1;

                if (g_bank.aCtl[PUFO_HUD_INV_FILTER_X].nState != g_world.sTactUi.bFilterPadX) { bMirror = 0; }
                if (g_bank.aCtl[PUFO_HUD_INV_FILTER_ALL].nState != g_world.sTactUi.bFilterPadAll) { bMirror = 0; }
                for (i32 c = 0; c < PUFO_INV_FILTER_PADS; ++c) {
                    if (g_bank.aCtl[PUFO_HUD_INV_FILTER_FIRST + c].nState != g_world.sTactUi.abFilterPad[c]) {
                        bMirror = 0;
                    }
                }
                Check(bMirror, "all seven filter bytes stand in the bank, in their own widget slots 21 / 24 / 25..29");
            }
            /* Two pads, and the reason for the other eleven is in the descriptor: their loaded pad
             * lands in slot 1 (0x1A6A4 stores the three descriptor names into slots 0, 1, 2 in order
             * and the inventory rows name nothing in slot 0), while UiControl_BlitActivePad reads
             * apPads[nState] -- so at rest a dark control draws nothing and the panel's static art
             * comes from the "invv" backdrop. Widget 30 is the one row whose descriptor does fill
             * slot 0 (a 15x15 pad at 225, 3), and it is the other blit. */
            CheckEqI32(nInv, 2,
                       "the arm blits two pads: the lit All filter (bank 24) and widget 30, the one control whose pad sits in slot 0");
            g_world.nUiScreen = nScreenSave;
        }

        /* --- screen 2: the minimap arm at 0x224B8 (Part 81) ------------------------------------
         * The other arm outside 0x2237C's iso group: case 2 goes straight to 0x224B8, so this screen
         * draws no iso layer either. What sets it apart from the inventory arm is that it is the only
         * reader of the tile-size pair AS A LOOP BOUND -- Map_nMinimapSpanX / Map_nMinimapSpanY @
         * 0x1139E8 / 0x1139EA (Map+0x30 / Map+0x32) tested as `value >> 2` -- and that pair is not
         * writerless: Map_InitFacingScratch puts 240 and 320 into it at 0x26F38 / 0x26F40, two plain
         * STRHs through the Map pointer, the store shape that leaves no xref in this IDB. The bounds
         * are therefore 60 and 80 and BOTH cell passes run, Map_IsValidCell doing the clipping.
         * Opened the way the game opens it: bank widget 3 carries MapView_EnterLookMode @ 0x163d4. */
        {
            u16 nScreenSave = g_world.nUiScreen;
            i32 nWorldXSave = g_world.sTactInput.anCamWorldX;   /* Map_nMinimapScrollX @ 0x118864 */
            i32 nWorldYSave = g_world.sTactInput.anCamWorldY;   /* Map_nMinimapScrollY @ 0x118868 */
            i32 nMini;
            i32 nInk;
            i32 nWorldX;
            i32 nWorldY;

            Check(g_world.tactEnv.tileSize.nTileW == 240 && g_world.tactEnv.tileSize.nTileH == 320,
                  "the tile-size pair holds 240 / 320: the bring-up's Map_InitFacingScratch wrote it at "
                  "0x26F38 / 0x26F40 (Map+0x30 / Map+0x32), which is the writer the xref census could not see");

            g_nSamples = 0;
            g_bank.aCtl[3].pfnClick();
            CheckEqI32(g_world.nUiScreen, 2, "the installed opener writes Map_nUiScreen 2 (0x163F0)");
            Check(g_nSamples >= 1 && g_aFrameSamples[0] == 16, "and plays sample 16 first (0x163E0)");
            Check(World_bTactFrameLive(&g_world),
                  "the look-mode screen is on the tactical frame seam (0x2237C case 2 -> 0x224B8)");

            /* The arm's scroll pair is the world-offset cell the opener just derived (0x16408..0x16488),
             * and it is the cell the look-mode pan arm of Input_OnLeft moves -- NOT the iso camera pair
             * at 0x15130/0x15134, which belongs to the screen-0 branch. */
            {
                i32 nCamX = g_world.tactEnv.focus.nCamScrollX;
                i32 nCamY = g_world.tactEnv.focus.nCamScrollY;

                nWorldX = g_world.sTactInput.anCamWorldX;
                nWorldY = g_world.sTactInput.anCamWorldY;

                World_TactKeyDown(&g_world, PUFO_KEY_LEFT);
                CheckEqI32(g_world.sTactInput.anCamWorldX, nWorldX - 1,
                           "on screen 2 the left key pans Map_nMinimapScrollX @ 0x118864 (Input_OnLeft @ 0x1177C's look arm)");
                Check(g_world.tactEnv.focus.nCamScrollX == nCamX && g_world.tactEnv.focus.nCamScrollY == nCamY,
                      "and leaves the iso camera pair @ 0x15130/0x15134 where it was -- that is the screen-0 arm");
                Check(g_world.sTactInput.anCamWorldY == nWorldY, "and pans one axis only");
                World_TactKeyUp(&g_world, PUFO_KEY_LEFT);
            }

            /* The clamp writes back into the cell it read, so the stored pair is the clamped pair.
             * Max first, then min, each against the value the previous clamp left (0x1EE5C..0x1EE80). */
            {
                const i32 nMaxX = 40 * (g_world.pTactMap->geom.nDimX - 3) / 4;
                const i32 nMaxY = 40 * (g_world.pTactMap->geom.nDimY - 4) / 4;

                g_world.sTactInput.anCamWorldX = 5000;
                g_world.sTactInput.anCamWorldY = 5000;
                World_TactFrame(&g_world);
                CheckEqI32(g_world.sTactInput.anCamWorldX, nMaxX,
                           "the max clamp stores 40*(nMapDimX-3)/4 back into 0x118864");
                CheckEqI32(g_world.sTactInput.anCamWorldY, nMaxY,
                           "and 40*(nMapDimY-4)/4 back into 0x118868");

                g_world.sTactInput.anCamWorldX = -5000;
                g_world.sTactInput.anCamWorldY = -5000;
                World_TactFrame(&g_world);
                CheckEqI32(g_world.sTactInput.anCamWorldX, -30, "the min clamp stores -30 into 0x118864");
                CheckEqI32(g_world.sTactInput.anCamWorldY, -40, "and -40 into 0x118868");
            }

            /* Set the scroll to the clamp's own minima so the two passes cover the whole field: each loop
             * runs from the scroll up to the bound, so the context cursor has to end exactly on 60 and 80
             * (240 >> 2 and 320 >> 2). The iso arm's cursor is what the inventory arm left untouched. */
            g_world.sTactIso.nTileX = -1;
            g_world.sTactIso.nTileY = -1;
            ClearSurface();
            World_TactFrame(&g_world);
            CheckEqI32(g_world.sTactIso.nTileX, (i32)g_world.tactEnv.tileSize.nTileW >> 2,
                       "both cell passes ran to the bound: the context tile cursor ends on nMapTileW >> 2 = 60");
            CheckEqI32(g_world.sTactIso.nTileY, (i32)g_world.tactEnv.tileSize.nTileH >> 2,
                       "and on nMapTileH >> 2 = 80 -- this mission is a 6 by 6 block map (60 by 60 cells), so the x "
                       "bound is exactly the grid width and Map_IsValidCell (10*nMapDimY rows) does the y clipping");

            nInk = 0;
            for (i32 y = 0; y < 320; ++y) {
                for (i32 x = 0; x < 240; ++x) {
                    if (SurfPix(x, y) != 0) { ++nInk; }
                }
            }
            Check(nInk > 0, "and the glyphs land on the surface: the ground pass plus the per-layer sprites, "
                            "occupants and item markers");

            /* The two paints above are fixtures. The picture is the view the opener actually centres on:
             * the scroll pair 0x16408..0x16488 derived, with the map under the camera. */
            g_world.sTactInput.anCamWorldX = nWorldX;
            g_world.sTactInput.anCamWorldY = nWorldY;
            ClearSurface();
            g_nBlits = 0;
            World_TactFrame(&g_world);
            nMini = g_nBlits;
            DumpFrame("tact_hud_minimap");

            /* The furniture the arm blits through the shell: the 'minimcur' pad goes direct (0x1F4B0,
             * key 0x3F) and the three screen-2 controls go through the blit seam (0x1F554 loop over
             * pBank + 0xC40 = bank widgets 98..100), so the seam count is those controls only. */
            {
                const char *pRec = (const char *)g_world.pAssets->pTextRecs
                                     + PUFO_TEXT_REC_STRIDE * PUFO_TEXT_REC_MINIMAP_LABEL;
                const i32 nRecLen = (i32)strlen(pRec);
                char szCount[16];

                snprintf(szCount, sizeof(szCount), "%d", (i32)(i8)g_world.tactEnv.cursor.nFlags);
                Check(nRecLen > 0 && strncmp(g_world.sTactIso.szText, pRec, (size_t)nRecLen) == 0,
                      "the label is text record 17 (0xB466D) copied to its NUL, the only reference in the binary (0x1F4DC)");
                Check(strcmp(&g_world.sTactIso.szText[50], szCount) == 0,
                      "and the layer count is itoa'd at szText[50] and strcat onto it (0x1F4EC..0x1F500)");
                printf("  minimap: %d control blits, %d inked pixels, scroll %d/%d, label \"%s\" + layer %s\n",
                       nMini, nInk, g_world.sTactInput.anCamWorldX, g_world.sTactInput.anCamWorldY,
                       g_world.sTactIso.szText, szCount);
            }

            g_world.sTactInput.anCamWorldX = nWorldXSave;
            g_world.sTactInput.anCamWorldY = nWorldYSave;
            g_world.nUiScreen = nScreenSave;
            ClearSurface();
            World_TactFrame(&g_world);   /* re-paint screen 0 so the surface holds what the next block expects */
        }

        /* --- screen 4: the command-pad panel at 0x224C4 (Part 82) ------------------------------
         * The third arm outside 0x2237C's iso group. Opened through the installed handler: bank widget 16
         * carries Tact_OpenCommandScreen @ 0x16ffc, which plays 16, CLEARS the look-commit byte
         * unk_128A44 @ Map+0x1508C at 0x1701C and stores 4 at 0x17024 with no gate on a selection -- which
         * is why the arm reads Map.pSelUnit with no null test at 0x1FCC4. The panel is the "soldier" pad
         * backdrop (unk_128AC0 @ Map+0x15108), the two insignia slots at (5,2) and (39,2), the name at
         * (75,2) and (75,20), eleven label/value rows from text records 56..66, the 12 x 24 tick ruler that
         * cuts through them (0x20AD4, the recorded original bug), the frame, then bank widgets 101..103
         * (unk_AD370 = pBank + 0xCA0). */
        {
            u16  nScreenSave = g_world.nUiScreen;
            u8   nFlagSave   = g_world.pTactMap->bLookCommitFlag;   /* unk_128A44 @ Map+0x1508C */
            i32  nCmd;
            i32  nCmdFlagged;
            i32  nInk;
            const PadImage *pCs0 = &g_world.pAssets->aPadCs[0];
            const PadImage *pCs1 = &g_world.pAssets->aPadCs[1];
            const PadImage *pCs2 = &g_world.pAssets->aPadCs[2];

            Check(pMapL != NULL && pMapL->pSelUnit != NULL,
                  "the reloaded mission has a selected unit, which is what 0x1FCC4 dereferences with no null test");

            g_nSamples = 0;
            g_bank.aCtl[16].pfnClick();
            Check(g_nSamples >= 1 && g_aFrameSamples[0] == 16, "the opener plays 16 (0x17008)");
            CheckEqI32(g_world.pTactMap->bLookCommitFlag, 0,
                       "and it CLEARS unk_128A44 first (0x1701C) -- the byte that gates two of its controls");
            CheckEqI32(g_world.nUiScreen, 4, "and writes Map_nUiScreen 4 (0x17024)");
            Check(World_bTactFrameLive(&g_world),
                  "the command screen is on the tactical frame seam (0x2237C case 4 -> 0x224C4)");

            ClearSurface();
            g_nBlits = 0;
            World_TactFrame(&g_world);
            nCmd = g_nBlits;
            CheckEqI32(nCmd, 3,
                       "the panel blits three controls: widget 101 always, 102 and 103 while the look-commit byte is clear");

            nInk = 0;
            for (i32 y = 0; y < 320; ++y) {
                for (i32 x = 0; x < 240; ++x) {
                    if (SurfPix(x, y) != 0) { ++nInk; }
                }
            }
            Check(nInk > 0, "and the panel paints: the backdrop pad, the slots, the name, the eleven rows, "
                            "the bars and the ruler");
            printf("  command panel: unit type %d, class %d, uMissions %d (<= 3 takes the empty-slot branch); "
                   "cs pad origins cs0 %d/%d cs1 %d/%d cs2 %d/%d; %d inked pixels\n",
                   pMapL->pSelUnit->nUnitType, pMapL->pSelUnit->nClass, pMapL->pSelUnit->uMissions,
                   pCs0->nOriginX, pCs0->nOriginY, pCs1->nOriginX, pCs1->nOriginY,
                   pCs2->nOriginX, pCs2->nOriginY, nInk);
            /* Either branch is legal for this unit: the cs pads were rewritten into the two slots the arm
             * owns ((5,2) and (39,2)), or they stayed where the startup load left them (0,0) because the
             * empty-slot branch drew frames instead. No other origin is reachable. */
            {
                i32 bSlots = 1;

                for (i32 c = 0; c < 3; ++c) {
                    const PadImage *p = (c == 0) ? pCs0 : (c == 1) ? pCs1 : pCs2;

                    if (!((p->nOriginX == 0 && p->nOriginY == 0)
                          || (p->nOriginY == 2 && (p->nOriginX == 5 || p->nOriginX == 39)))) {
                        bSlots = 0;
                    }
                }
                Check(bSlots, "the cs pads sit either in the panel's two slots (5,2)/(39,2) or untouched at the "
                              "origin the startup load zeroed (0x387F4..0x38894) -- the arm rewrites them at 0x1FD84..0x1FE80");
            }
            DumpFrame("tact_hud_cmdpads");

            /* The gate on the look-commit byte. Its writers in the original are the pen press and release
             * (0x1480C, 0x14D0C), the confirm (0x16168, 0x161A8), the look-device commit inside
             * Tact_ProcessFallAction (0x52C38) and the clear at 0x1701C; the flagged command opener at
             * 0x198c4 writes a DIFFERENT byte (unk_113A38), so the flag is set here the way the look device
             * leaves it and the two side controls have to drop out of the panel. */
            g_world.pTactMap->bLookCommitFlag = 1;
            ClearSurface();
            g_nBlits = 0;
            World_TactFrame(&g_world);
            nCmdFlagged = g_nBlits;
            CheckEqI32(nCmd - nCmdFlagged, 2,
                       "with unk_128A44 set the panel blits only widget 101 (0x20C04 test, 0x20C0C store)");

            g_world.pTactMap->bLookCommitFlag = nFlagSave;
            g_world.nUiScreen = nScreenSave;
            ClearSurface();
            World_TactFrame(&g_world);
        }

        /* --- screen 6: the marked-unit overlay at 0x224D0 (Part 83) ----------------------------
         * The fourth arm outside 0x2237C's iso group. Its opener is not a bank pad at all: the mark special
         * pays its cost off the unit, runs Special_MarkRoster over both rosters and only then stores 6 into
         * Map_nUiScreen (0x18078..0x18248), so what the arm reads is the units' own bSpecialMark bytes.
         * The overlay pad ("mscanv", unk_113ABC @ Map+0x104) is re-originated to (0,0) at 0x20C6C, the marks
         * are "sbsold" unk_138EB8 and "ssold" unk_156A80 placed 11 pixels per cell of difference from
         * Map_pSelUnit->pCell -- read with no null test at 0x20CEC -- the XCOM band 11*(dy+14) and the enemy
         * band 11*dy+155 (one pixel apart, not symmetric), large units offset 2 for XCOM and 8 for enemies,
         * small ones offset 4 either way. */
        {
            u16     nScreenSave = g_world.nUiScreen;
            Soldier *pSel6      = pMapL->pSelUnit;
            i32      nMark;
            i32      nMarkNone;
            i32      nInk;
            i32      nMarked;
            PadImage *pBig  = g_world.apTactPads[PUFO_UIPAD_SBSOLD];   /* unk_138EB8 "sbsold" */
            PadImage *pSmall = g_world.apTactPads[PUFO_UIPAD_SOLD];    /* unk_156A80 "ssold" */

            Check(pSel6 != NULL && pSel6->pCell != NULL,
                  "the arm reads Map_pSelUnit->pCell with no null test (0x20CEC); the reloaded mission has both");

            /* Clear every mark first: the census is then the arm's own doing, not the mission's. */
            nMarked = 0;
            for (i32 i = 0; i < pMapL->nXcomCount; ++i) { nMarked += (pMapL->apXcomUnits[i]->bSpecialMark != 0); pMapL->apXcomUnits[i]->bSpecialMark = 0; }
            for (i32 i = 0; i < pMapL->nEnemyCount; ++i) { nMarked += (pMapL->apEnemyUnits[i]->bSpecialMark != 0); pMapL->apEnemyUnits[i]->bSpecialMark = 0; }
            printf("  markers: %d units carried bSpecialMark on the loaded mission, all cleared for the gate\n", nMarked);

            /* Screen 6 has exactly one opener in the image: the mark branch of TactHud_UseSpecialItem20 @
             * 0x17d54 (bank widget 40), which pays the cost off Soldier.uTu (0x18078..0x180A8), marks both
             * rosters by radius (0x180A0..0x18184, the sqrt loop) and only then stores 6 at 0x18248. Driving
             * that branch would first have to put a uSortKey-20 object in the selected unit's hand, and
             * Special_MarkRoster is static inside pufo_tacthud.c, so the screen id and the mark bytes are
             * staged here the way that branch leaves them; everything the arm then does is its own. */
            g_world.nUiScreen = 6;
            ClearSurface();
            g_nBlits = 0;
            World_TactFrame(&g_world);
            nMarkNone = g_nBlits;
            Check(World_bTactFrameLive(&g_world),
                  "the marker screen is on the tactical frame seam (0x2237C case 6 -> 0x224D0)");

            /* One XCOM mark: the arm's own arithmetic, pinned on the pad it wrote. The unit picked is the
             * one furthest from the selected unit's cell, so the two offsets are not both zero. */
            {
                Soldier *pU = pMapL->apXcomUnits[0];
                i32 nBest = -1;
                i32 nX, nY;

                for (i32 i = 0; i < pMapL->nXcomCount; ++i) {
                    const Soldier *pC = pMapL->apXcomUnits[i];
                    i32 nSpan = abs((i32)pC->pCell->nX - (i32)pSel6->pCell->nX)
                              + abs((i32)pC->pCell->nY - (i32)pSel6->pCell->nY);

                    if (nSpan > nBest) {
                        nBest = nSpan;
                        pU = (Soldier *)pC;
                    }
                }
                Check(nBest > 0, "the unit marked for the arithmetic check sits on a DIFFERENT cell than the selection");

                pU->bSpecialMark = 1;
                ClearSurface();
                g_nBlits = 0;
                World_TactFrame(&g_world);
                nMark = g_nBlits;

                nX = 11 * ((i32)pU->pCell->nX - (i32)pSel6->pCell->nX) + 120;
                nY = 11 * ((i32)pU->pCell->nY - (i32)pSel6->pCell->nY + 14);
                if (Unit_bIsLarge(pU)) {
                    Check(pBig->nOriginX == nX - 2 && pBig->nOriginY == nY - 2,
                          "the XCOM band is 11*(dy+14) with the large mark offset 2 (0x20CEC..0x20D50)");
                } else {
                    Check(pSmall->nOriginX == nX - 4 && pSmall->nOriginY == nY - 4,
                          "the XCOM band is 11*(dy+14) with the small mark offset 4 (0x20CEC..0x20D84)");
                }
                CheckEqI32(nMark, nMarkNone,
                           "the seam count is the arm's one control either way: the marks go direct through "
                           "PadImage_BlitEx (0x20D8C / 0x20EA0), never through the shell's blit hook");
                printf("  markers: xcom unit at %d/%d against the selection at %d/%d -> mark at %d/%d; "
                       "%d control blits, %d with no mark\n",
                       pU->pCell->nX, pU->pCell->nY, pSel6->pCell->nX, pSel6->pCell->nY, nX, nY,
                       nMark, nMarkNone);
                pU->bSpecialMark = 0;
            }

            /* One enemy mark: the other band, 11*dy + 155. This mission has no enemy on the field (the 13
             * hostiles sit in the reserve roster unk_138DF0, which the arm never walks), so the band is only
             * checkable when a mission actually has one -- test_tacthud.c pins it over a fixture that does. */
            if (pMapL->nEnemyCount > 0) {
                Soldier *pE = pMapL->apEnemyUnits[0];
                i32 nX, nY;

                pE->bSpecialMark = 1;
                ClearSurface();
                World_TactFrame(&g_world);

                nX = 11 * ((i32)pE->pCell->nX - (i32)pSel6->pCell->nX) + 120;
                nY = 11 * ((i32)pE->pCell->nY - (i32)pSel6->pCell->nY) + 155;
                if (Unit_bIsLarge(pE)) {
                    Check(pBig->nOriginX == nX - 8 && pBig->nOriginY == nY - 8,
                          "the enemy band is 11*dy+155 with the large mark offset 8 (0x20E10..0x20E74)");
                } else {
                    Check(pSmall->nOriginX == nX - 4 && pSmall->nOriginY == nY - 4,
                          "the enemy band is 11*dy+155 with the small mark offset 4 (0x20E10..0x20E98)");
                }
                printf("  markers: enemy 0 at %d/%d -> mark at %d/%d (one pixel below the xcom band's 11*dy+154)\n",
                       pE->pCell->nX, pE->pCell->nY, nX, nY);
            } else {
                printf("  markers: no enemy on this field, so the 11*dy+155 band is not reachable here\n");
            }

            /* The picture: the whole field marked, the way the mark device leaves it. */
            for (i32 i = 0; i < pMapL->nXcomCount; ++i) { pMapL->apXcomUnits[i]->bSpecialMark = 1; }
            for (i32 i = 0; i < pMapL->nEnemyCount; ++i) { pMapL->apEnemyUnits[i]->bSpecialMark = 1; }
            ClearSurface();
            World_TactFrame(&g_world);
            nInk = 0;
            for (i32 y = 0; y < 320; ++y) {
                for (i32 x = 0; x < 240; ++x) {
                    if (SurfPix(x, y) != 0) { ++nInk; }
                }
            }
            Check(nInk > 0, "the overlay paints: the mscanv backdrop at (0,0) and one mark per marked unit");
            printf("  markers: %d xcom + %d enemy marks inked %d pixels\n",
                   pMapL->nXcomCount, pMapL->nEnemyCount, nInk);
            DumpFrame("tact_hud_markers");
            for (i32 i = 0; i < pMapL->nXcomCount; ++i) { pMapL->apXcomUnits[i]->bSpecialMark = 0; }
            for (i32 i = 0; i < pMapL->nEnemyCount; ++i) { pMapL->apEnemyUnits[i]->bSpecialMark = 0; }

            g_world.nUiScreen = nScreenSave;
            ClearSurface();
            World_TactFrame(&g_world);
        }
        /* sav62.sav stays on disk: section 18 resumes the live mission from it and
         * removes the file there. */

        /* --- screens 0x14/0x15: the Cydonia briefing pair at 0x223DC (Part 85) ------------------
         * The last two arms of 0x2237C's replace family, and the handler is three instructions:
         * Cydonia_Blit_Then_UiPanelMsg_Draw @ 0x1a7a0 blits the pad POINTER held at unk_15692C (PadImage_Blit
         * @ 0x110dc, called at 0x1A7AC) and then draws the panel at unk_1571EC (UiPanelMsg_Draw @ 0x3e94c,
         * called at 0x1A7B4). Both operands are cells the mission already owns -- pW->pTactBriefPad and
         * tactEnv.apPanel[4] -- so what this part adds is two env wires and the seam, nothing else.
         *
         * The opener is Tact_InitMission's own tail: 0x3D284 reads Map_bSpawnAlt, 0x3D29C tests the mode
         * argument, either one fails out to 0x3D548, which drops unk_15692C to NULL and leaks what the previous
         * mission left (recorded original bug). The taken branch saves Map_nUiScreen into apUiSlotsA[11]
         * (0x3D2A4 LDRH, 0x3D2A8 STR), stores 20 (0x3D2AC/0x3D2B0), allocates the 20-byte pad, loads "cydonia"
         * at mode 0x21 with its origins cleared, then appends the panel's two lines and += 2 (0x3D4EC..0x3D540):
         * the image's standing empty string at 0x10DA38, then the text-blob slice at unk_EC419 = aTextRecs +
         * 0x38B49. That branch -- the gate, the pad, the saved slot and the panel content -- is gated in
         * test_tactinit.c (TestBriefing, TestBriefingSkipped) and both closers in test_input.c
         * (Input_OnPointerUp's 20/21 arm, UiScreen_OnCancel's). Re-running a whole mission init for one paint is
         * not a state the shell reaches, so this block lays the screen write, the saved slot and the pad exactly as
         * the taken branch leaves them and gates the PAINT: the seam, the arm's own calls against the frame, the
         * panel's row and centre arithmetic, the dead 0x15 case, and both closers through the shell. */
        {
            u16          nScreenKeep = g_world.nUiScreen;
            i32          nSlot11Keep = g_world.nMapUiSlot11;
            i32          nFadeKeep   = g_world.nMapUiSlot1;
            i32          nFadeInKeep = g_world.nMapUiSlot10;
            i32          nIsoXKeep   = g_world.sTactIso.nTileX;
            i32          nIsoYKeep   = g_world.sTactIso.nTileY;
            UiPanel     *pPanel      = g_world.tactEnv.apPanel[PUFO_BRIEF_PANEL];
            PadImage    *pBrief      = (PadImage *)malloc(sizeof(PadImage));
            const char  *pszTail     = (const char *)g_world.pAssets->pTextRecs + PUFO_BRIEF_TAIL;
            static u16   aPadOnly[240 * 320];   /* 0x1A7AC's call alone: everything the panel is not */
            static u16   aPaint20[240 * 320];   /* the frame paint on 20, for the 0x15 comparison */
            i32          nFrameBlits;
            i32          nTailW, nTailX;
            i32          nTailInk, nLeftInk, nRightInk, nBelowInk, nOutsideInk;

            Check(pBrief != NULL, "the briefing pad is a heap PadImage, the 20-byte new(0x14) of 0x3D2B4");
            Check(pPanel != NULL, "the briefing panel is tactEnv.apPanel[4] = unk_1571EC, the one 0x1A7B4 draws");
            Check(pPanel != NULL && pPanel->nPosX == 120 && pPanel->nPosY == 30
                      && pPanel->nStyle == 14 && pPanel->nFont == 16 && pPanel->nUnk7076 == 1,
                  "laid out at (120,30), centre-aligned, 14 visible rows of 16 (0x3C854..0x3CB00, align 1)");
            Check(pPanel->nLineCount == 28 && g_world.pTactBriefPad == NULL,
                  "this mission came in through the skipped gate: unk_15692C NULL as 0x3D548 leaves it, and the panel "
                  "still holds the 28 lines its ctor was given -- UiPanel_ctor stores its line count in nLineCount and "
                  "nothing on this path resets it (0x3C738..0x3C854)");
            {
                i32 nLinesEmpty = 0;

                for (i32 r = 0; r < pPanel->nLineCount; ++r) {
                    if (pPanel->apLines[r] != NULL && pPanel->apLines[r][0] == 0) { ++nLinesEmpty; }
                }
                Check(nLinesEmpty == 28 && pPanel->apLines[28] == NULL,
                      "all 28 lines are empty and slot 28 was never allocated: UiPanelMsg_Draw walks its 14 visible rows "
                      "and finds nothing, and Init_Briefing's two appends only land inside the panel while its wrap keeps "
                      "them below 28 (past that the original writes through a zero slot offset -- recorded latent fault)");
            }

            /* What the taken branch leaves, laid in its order. */
            g_world.nMapUiSlot11 = (i32)g_world.nUiScreen;   /* 0x3D2A4 LDRH Map_nUiScreen, 0x3D2A8 STR */
            g_world.nUiScreen    = 20;                       /* 0x3D2AC MOV R0,#0x14, 0x3D2B0 STRH */
            Check(g_world.nMapUiSlot11 == 0 && g_world.nUiScreen == 20,
                  "the screen moves to 20 with its old value saved in apUiSlotsA[11] -- and the old value is 0, "
                  "because the mission prologue cleared Map_nUiScreen at 0x3C64C");

            PadImage_ctor(pBrief);
            Check(PadImage_Load(pBrief, "cydonia", 0x21), "the pad is the archive member \"cydonia\", loaded at mode 0x21 (0x3D2D0)");
            pBrief->nOriginX = 0;
            pBrief->nOriginY = 0;
            g_world.pTactBriefPad = pBrief;                  /* unk_15692C, the pointer 0x1A7AC blits */

            Check(World_bTactFrameLive(&g_world), "the briefing is on the tactical frame seam (0x2237C case 0x14 -> 0x223DC)");

            /* The arm's first call on its own: the pad at (0,0). Every census below compares a frame paint against
             * this, so the panel's own ink is what shows up. */
            ClearSurface();
            PadImage_Blit(pBrief);
            for (i32 y = 0; y < 320; ++y) {
                for (i32 x = 0; x < 240; ++x) {
                    aPadOnly[(size_t)y * 240u + (size_t)x] = (u16)SurfPix(x, y);
                }
            }

            /* The fade block at 0x2250C belongs to every arm of Render_Frame and re-blits the whole screen while
             * its counter is up (0x22538), so it is held down here exactly as in Part 64 and Part 84. Neither
             * counter gates this arm; the briefing paints the same either way. */
            g_world.nMapUiSlot1  = 0;
            g_world.nMapUiSlot10 = 10;
            g_world.sTactIso.nTileX = -1;                    /* the iso layer's own loop bounds */
            g_world.sTactIso.nTileY = -1;
            ClearSurface();
            g_nBlits = 0;
            World_TactFrame(&g_world);
            nFrameBlits = g_nBlits;
            for (i32 y = 0; y < 320; ++y) {
                for (i32 x = 0; x < 240; ++x) {
                    aPaint20[(size_t)y * 240u + (size_t)x] = (u16)SurfPix(x, y);
                }
            }

            Check(g_world.sTactIso.nTileX == -1 && g_world.sTactIso.nTileY == -1,
                  "and the arm runs with no iso layer at all: 0x2237C sends 0x14 straight to 0x223DC, never through 0x22414");
            CheckEqI32(nFrameBlits, 0,
                  "and nothing goes through the control-blit hook: the handler is PadImage_Blit plus UiPanelMsg_Draw, "
                  "and these two screens carry no control");
            CheckEqI32(DiffInBox(0, 0, 240, 320, aPadOnly), 0,
                  "and with the panel as the mission leaves it -- 28 empty lines -- the briefing frame IS the pad: "
                  "UiPanelMsg_Draw walks the 14 visible rows and puts nothing down");

            /* One line, staged, to pin the arm's row and align arithmetic against the panel's own fields. The writer
             * of this panel is Init_Briefing (0x3D2A8..0x3D540) and its content -- the branch select, the wrap from
             * 0x37144, the two appends at 0x3D4EC..0x3D540 -- is gated in test_tactinit.c over its own fixture; what
             * is gated here is where a line lands. The string is the real slice the writer appends last, the blob at
             * unk_EC419, straight out of lang.dat. */
            {
                i32 i = 0;

                for (;;) { pPanel->apLines[0][i] = (u8)pszTail[i]; if (pszTail[i] == 0) { break; } ++i; }
            }
            pPanel->nLineCount = 1;

            ClearSurface();
            World_TactFrame(&g_world);
            for (i32 y = 0; y < 320; ++y) {
                for (i32 x = 0; x < 240; ++x) {
                    aPaint20[(size_t)y * 240u + (size_t)x] = (u16)SurfPix(x, y);
                }
            }

            nTailW  = Text_MeasureWidth(pszTail);
            nTailX  = 120 - nTailW / 2;                      /* UiPanelMsg_Draw's align 1: nPosX - w/2 */
            nTailInk   = DiffInBox(nTailX, 30, nTailX + nTailW, 46, aPadOnly);
            nLeftInk   = DiffInBox(0, 30, nTailX, 46, aPadOnly);
            nRightInk  = DiffInBox(nTailX + nTailW, 30, 240, 46, aPadOnly);
            nBelowInk  = DiffInBox(0, 46, 240, 320, aPadOnly);
            nOutsideInk = DiffInBox(0, 0, 240, 320, aPadOnly) - nTailInk - nLeftInk - nRightInk - nBelowInk;

            Check(nTailInk > 0,
                  "the line draws on row 0 at the centred x: 120 - w/2, y 30 (nPosY 30 + row 0 * nFont 16)");
            Check(nLeftInk == 0 && nRightInk == 0,
                  "and the row is centred, not offset: nothing left of its start, nothing right of its measured width");
            CheckEqI32(nBelowInk, 0, "and nothing below it: with one line the rows below draw nothing");
            CheckEqI32(nOutsideInk, 0,
                  "the frame paint against the pad alone is that one line: no iso layer, no furniture, no fade block");
            printf("  briefing: pad \"cydonia\" at 0,0; line \"%s\" %d px centred at %d,30; %d px over the pad "
                   "alone, %d control blits\n", pszTail, nTailW, nTailX, nTailInk, nFrameBlits);
            DumpFrame("tact_briefing");

            /* case 0x15 shares the handler. 21 is never stored into Map_nUiScreen anywhere in the binary, and
             * apUiSlotsA[11] is written only with what Map_nUiScreen held, which on the briefing path is 0 -- so
             * the case is dead in the original. Painting it shows both cases land on the same 0x223DC. */
            g_world.nUiScreen = 21;
            Check(World_bTactFrameLive(&g_world), "case 0x15 is on the seam too, off the same jumptable entry");
            ClearSurface();
            World_TactFrame(&g_world);
            CheckEqI32(DiffInBox(0, 0, 240, 320, aPaint20), 0,
                      "and paints the screen-20 frame byte for byte: 0x223DC is one handler for both cases");

            /* Closer 1, the pen. Input_OnPointerUp's 20/21 arm adopts apUiSlotsA[11] (0x14C4C -> 0x14C50) with no
             * hit-test and no teardown; test_input.c pins the arm, this runs it through the shell's dispatch. */
            {
                TactPointerWorld ptr;

                memset(&ptr, 0, sizeof(ptr));
                ptr.pBank = &g_bank;
                ptr.hit = g_hit;
                g_world.nUiScreen = 20;
                World_TactPointerUp(&g_world, &ptr);
                CheckEqI32((i32)g_world.nUiScreen, g_world.nMapUiSlot11,
                      "the release brings the mission screen back through the shell's pen dispatch (0x14C4C, 0x14C50)");
            }

            /* Closer 2, the cancel. UiScreen_OnCancel's 0x14..0x15 arm writes slot 11 and returns; the teardown
             * belongs to the other arms (0x1626C passes the range test, 0x16294 writes the saved screen). */
            g_world.nUiScreen = 20;
            Check(World_MissionCancelEnd(&g_world), "the cancel takes the 0x14..0x15 arm (0x1626C passed, 0x16294 taken)");
            CheckEqI32((i32)g_world.nUiScreen, g_world.nMapUiSlot11,
                  "and the screen comes back out of apUiSlotsA[11] -- 0, the value the gate saved at 0x3D2A8");
            Check(g_world.bTactFlag != 0 && g_world.bTactLive && pMapL == g_world.pTactMap,
                  "with no teardown and no geoscape rebuild: the briefing is a screen over the live mission");

            pPanel->apLines[0][0] = 0;
            pPanel->nLineCount = 28;         /* back to what the ctor gave this panel and the skipped gate left */
            PadImage_FreeData(pBrief);
            free(pBrief);
            g_world.pTactBriefPad = NULL;   /* the port's teardown rule (Part 51's drop site); 0x3D548 only drops the pointer */
            g_world.nUiScreen    = nScreenKeep;
            g_world.nMapUiSlot11 = nSlot11Keep;
            g_world.nMapUiSlot1  = nFadeKeep;
            g_world.nMapUiSlot10 = nFadeInKeep;
            g_world.sTactIso.nTileX = nIsoXKeep;
            g_world.sTactIso.nTileY = nIsoYKeep;
            ClearSurface();
            World_TactFrame(&g_world);
        }

        /* --- and on a loaded mission the same close deploys nothing (Part 66) ------------------
         * Inv_CloseAndCommit gates the loadout snapshot AND the whole map refresh on the Map+0x01
         * commit flag (the test at 0x15A60), and a loaded mission restores that byte out of the slot
         * file as 0. The confirm still plays 16, still merges a loose clip, still clears the screen --
         * but no deploy runs, so the roster keeps what the "map" chunk restored and the cell census
         * does not move. This is the other half of the same gate the bring-up runs through. */
        {
            i32 nOccBefore  = FrameCountOccupants();
            i32 nXcomBefore = pMapL->nXcomCount;

            Check(g_world.bMapUnk1139B9 == 0,
                  "the reloaded mission has Map+0x01 clear, which gates off the commit's map half (0x15A60)");

            pMapL->pSelUnit = pMapL->apXcomUnits[0];   /* fixture: a unit is selected */
            g_bank.aCtl[4].pfnClick();                 /* the installed inventory opener */
            Check(g_world.nUiScreen == 1, "the inventory pad (widget 4) opens screen 1 on the reloaded mission");

            g_nBankSamples = 0;
            g_bank.aCtl[98].pfnClick();

            Check(g_nBankSamples >= 2 && g_aBankSamples[0] == 16 && g_aBankSamples[1] == 16,
                  "the arm still plays 16 (0x160FC)");
            Check(g_world.nUiScreen == 0, "and still clears Map_nUiScreen at the tail (0x15F34)");
            Check(pMapL->nXcomCount == nXcomBefore, "no soldier spawns");
            Check(FrameCountOccupants() == nOccBefore,
                  "and no cell changes occupant: the census is exactly what it was before the close");
        }

        /* --- the keyboard chains run over the live mission (Part 67) ---------------------------
         * World_TactKeyDown / World_TactKeyUp are PocketUFO_WndProc's WM_KEYDOWN chain (0xA0DC8..0xA0F74)
         * and WM_KEYUP chain (0xA0CE0..0xA0DC4), staged exactly like the pen dispatch. The camera asserts
         * read tactEnv.focus, the live owner of Map_nCamScrollX/Y @ 0x15130/0x15134 that the iso layer and
         * the pointer's cell mapping also read -- so they show the arm reaching the cell the picture is
         * drawn from, not just the dispatch's own mirror. */
        {
            i32 nScrollX = g_world.tactEnv.focus.nCamScrollX;   /* 21, out of the slot file */
            i32 nScrollY = g_world.tactEnv.focus.nCamScrollY;   /* 23 */
            i32 nClicks;

            Check(g_world.nUiScreen == 0 && World_bTactPointerLive(&g_world),
                  "the chain starts on the tactical map with the mission owning the keys");

            World_TactKeyDown(&g_world, PUFO_KEY_LEFT);
            Check(g_world.tactEnv.focus.nCamScrollX == nScrollX - 1,
                  "the left key's first press moves the camera scroll on the live cell (Input_OnLeft @ 0x1177C)");
            Check(g_bank.abHeldDir[2] == 1, "and sets UiBank_bHeldLeft @ pBank+0x104E (0xAD71E)");
            World_TactKeyDown(&g_world, PUFO_KEY_LEFT);
            Check(g_world.tactEnv.focus.nCamScrollX == nScrollX - 1,
                  "the repeat is swallowed while Map_nUiScreen is 0 (the 0xA0DE8/0xA0DF8 pair)");
            World_TactKeyUp(&g_world, PUFO_KEY_LEFT);
            Check(g_bank.abHeldDir[2] == 0, "the WM_KEYUP arm clears only its held byte (0xA0D3C)");
            World_TactKeyDown(&g_world, PUFO_KEY_LEFT);
            Check(g_world.tactEnv.focus.nCamScrollX == nScrollX - 2, "and after a release the next press scrolls again");
            World_TactKeyUp(&g_world, PUFO_KEY_LEFT);

            World_TactKeyDown(&g_world, PUFO_KEY_UP);
            Check(g_world.tactEnv.focus.nCamScrollY == nScrollY - 1,
                  "the up key's screen-0 arm moves the y scroll (Input_OnUp @ 0x1197C)");
            World_TactKeyUp(&g_world, PUFO_KEY_UP);
            Check(g_bank.abHeldDir[0] == 0, "and the up release clears 0xAD71C");

            /* The confirm key on the tactical map: Input_OnConfirm @ 0x15f3c, the unarmed case -- sample 7
             * and the deploy-panel byte 0x15110, which sTactInput owns outright. */
            g_nBankSamples = 0;
            g_world.pTactMap->bActionLatch = 0;         /* fixture: the unarmed case (Map+0x02) */
            g_world.sTactInput.bDeployPanelOpen = 0;    /* 0x15110 */
            World_TactKeyDown(&g_world, PUFO_KEY_CONFIRM);
            Check(g_nBankSamples == 1 && g_aBankSamples[0] == 7, "the confirm key's unarmed arm plays sample 7");
            Check(g_world.sTactInput.bDeployPanelOpen == 1, "and toggles the deploy-panel byte");

            /* Action on screen 0 runs the map-click seam for real (the BL at 0x15668); its selection and
             * move rules are gated in test_input against 0x1512c's own body. Park the cursor on the
             * selected unit's own cell first: Tact_HandleMapClick's "clicked the cell you stand on"
             * return (0x151A0) keeps this dispatch test side-effect-free -- with the input mirror now
             * carrying the dims and the option gate bytes, a click on a DIFFERENT cell really enqueues
             * a walk, and a live queue would change every expectation below. */
            nClicks = g_seams.nMapClicks;
            g_bBankActionFlag = false;
            if (pMapL->pSelUnit != NULL && pMapL->pSelUnit->pCell != NULL) {
                g_world.tactEnv.cursor.nX     = pMapL->pSelUnit->pCell->nX;
                g_world.tactEnv.cursor.nY     = pMapL->pSelUnit->pCell->nY;
                g_world.tactEnv.cursor.nFlags = (u8)pMapL->pSelUnit->pCell->nZ + 1u;
            }
            World_TactKeyDown(&g_world, PUFO_KEY_ACTION);
            Check(g_seams.nMapClicks == nClicks + 1, "the action key reaches Tact_HandleMapClick");
            Check(g_bBankActionFlag, "and Input_OnAction set UiBank_bActionFlag (the STRB at 0x155B4)");
            World_TactKeyUp(&g_world, PUFO_KEY_ACTION);
            Check(!g_bBankActionFlag, "the release arm's UiScreen_OnBack clears it again (0x15684)");
            Check(g_world.nUiScreen == 0, "and screen 0 keeps its id through both arms");

            /* The escape slot: nullsub_1 on press (0xA0F70), Input_OnMenuEscape on release (0xA0DC0),
             * whose case 0 opens the escape screen 7 with sample 14 (0x16394). */
            g_nBankSamples = 0;
            World_TactKeyDown(&g_world, PUFO_KEY_ESCAPE);
            Check(g_nBankSamples == 0 && g_world.nUiScreen == 0, "the press arm calls nullsub_1 and changes nothing");
            World_TactKeyUp(&g_world, PUFO_KEY_ESCAPE);
            Check(g_world.nUiScreen == 7, "the release arm opens screen 7");
            Check(g_nBankSamples == 1 && g_aBankSamples[0] == 14, "with sample 14");

            /* Screen 7 is not one of Input_OnAction's arms (not <= 2, not 4, not 6/12/15): the routing
             * leaves it alone, which is what 0x155FC..0x1566C does. */
            g_nBankSamples = 0;
            nClicks = g_seams.nMapClicks;
            World_TactKeyDown(&g_world, PUFO_KEY_ACTION);
            Check(g_world.nUiScreen == 7 && g_seams.nMapClicks == nClicks && g_nBankSamples == 0,
                  "the action key has no arm for screen 7: no map click, no sample, no screen write");
            World_TactKeyUp(&g_world, PUFO_KEY_ACTION);   /* UiScreen_OnBack: screen 7 is below 8, no arm */
            Check(g_world.nUiScreen == 7, "and its release leaves screen 7 alone (UiScreen_OnBack only arms 8/9/13, 0x1568C..0x156A0)");

            /* Screen 1's left/right arms scroll the item filter (Input_OnLeft @ 0x11774 -> Inv_ScrollUp
             * @ 0x173e4, Input_OnRight's mirror -> Inv_ScrollDown @ 0x173a8). With a screen open a held key
             * repeats instead of being swallowed. The camera stays put: the directional handlers only take
             * the scroll branch on screen 0. Screen 1's up/down pair (Tact_SelectNextUnit @ 0x17304 /
             * Tact_SelectPrevUnit @ 0x17260) is gated at the end of this block, on the cell link the
             * soldiers chunk restores at 0x419DC. */
            (void)g_bank.aCtl[4].pfnClick();       /* the installed inventory opener: Map_nUiScreen = 1 */
            {
                SeamTrace s0 = g_seams;

                Check(g_world.nUiScreen == 1, "the inventory screen re-opens for the direction arms");
                nScrollX = g_world.tactEnv.focus.nCamScrollX;
                nScrollY = g_world.tactEnv.focus.nCamScrollY;

                World_TactKeyDown(&g_world, PUFO_KEY_LEFT);
                World_TactKeyDown(&g_world, PUFO_KEY_LEFT);
                Check(g_seams.nScrollUp == s0.nScrollUp + 2,
                      "left scrolls the filter, and a held left keeps scrolling with a screen open");
                World_TactKeyDown(&g_world, PUFO_KEY_RIGHT);
                Check(g_seams.nScrollDown == s0.nScrollDown + 1, "right scrolls it the other way");
                World_TactKeyUp(&g_world, PUFO_KEY_LEFT);
                World_TactKeyUp(&g_world, PUFO_KEY_RIGHT);
                Check(g_bank.abHeldDir[PUFO_KEY_LEFT] == 0 && g_bank.abHeldDir[PUFO_KEY_RIGHT] == 0,
                      "the two releases clear only their held bytes (0xA0D3C / 0xA0D5C)");
                Check(g_world.tactEnv.focus.nCamScrollX == nScrollX
                          && g_world.tactEnv.focus.nCamScrollY == nScrollY,
                      "and neither arm touched the camera scroll: 0x15130/0x15134 only move on screen 0");
                Check(g_world.nUiScreen == 1, "the direction arms never change the screen id");

                /* The select pair on the mission the readers rebuilt -- the part Part 67 had to leave
                 * on the deployed mission. `Tact_SelectUnit`'s tail reads `pUnit->pCell`, and a reloaded
                 * unit only has one because Save_ReadSoldiers resolved its three record bytes into a
                 * cell pointer (0x419DC) against the grid Init_LoadMission allocated at 0x3D168..0x3D1C0. */
                {
                    bool bLinked = false;

                    for (i32 i = 0; i < pMapL->nXcomCount; ++i) {
                        if (pMapL->apXcomUnits[i] != NULL && pMapL->apXcomUnits[i]->pCell != NULL) {
                            bLinked = true;
                        }
                    }
                    Check(bLinked,
                          "the reloaded roster stands on cells again: the soldiers chunk's three bytes became a pointer (0x419DC)");

                    World_TactKeyDown(&g_world, PUFO_KEY_UP);
                    Check(g_seams.nSelNext == s0.nSelNext + 1, "up runs the select-next seam on the loaded mission too");
                    Check(pMapL->pSelUnit != NULL && pMapL->pSelUnit->pCell != NULL,
                          "and the unit it selected stands on a cell");
                    Check(pMapL->pSelUnit != NULL && pMapL->pSelUnit->pCell != NULL
                              && g_world.tactEnv.cursor.nX == (i32)pMapL->pSelUnit->pCell->nX
                              && g_world.tactEnv.cursor.nY == (i32)pMapL->pSelUnit->pCell->nY,
                          "with the live cursor pair (0x15128/0x1512C) moved onto that cell by MapCursor_SetPos @ 0x27014 -- the read side of the restored link");
                    World_TactKeyUp(&g_world, PUFO_KEY_UP);
                    Check(g_bank.abHeldDir[PUFO_KEY_UP] == 0, "the up release clears 0xAD71C");
                    World_TactKeyDown(&g_world, PUFO_KEY_DOWN);
                    Check(g_seams.nSelPrev == s0.nSelPrev + 1, "down runs the select-previous seam");
                    World_TactKeyUp(&g_world, PUFO_KEY_DOWN);
                    Check(g_world.nUiScreen == 1, "and neither select arm changes the screen id");
                }
            }
        }
    }

    /* --- the six tactical HUD screens, painted through the frame (Part 76) -----------------
     *
     * Render_Frame's iso group is 0x2237C's jumptable "cases 0,3,5,7-12,15": the iso layer plus the
     * furniture of the screen it is on. Part 76 gives the shell the six furniture envs, so every
     * picture below is the live reloaded mission drawn through the handler the original itself
     * installs on the bank widget that opens it (Tact_InitUiControls @ 0x3b498).
     *
     * Each count is the arm's own pad blits, measured by PaintFurnitureFrame over the state the opening
     * handler just produced. */
    {
        GameMap *pMapH = g_world.pTactMap;
        Soldier *pUnit = NULL;
        i32      nArmed = 0;
        i32      nVisible;
        i32      nSampleMark;
        i32      i;

        Check(pMapH != NULL && pMapH->nXcomCount > 0, "the reloaded mission still has a roster");
        pUnit = (pMapH != NULL && pMapH->nXcomCount > 0) ? pMapH->apXcomUnits[0] : NULL;

        /* Weapon census. Tact_OpenLeftHandMenu @ 0x17030 needs a hand slot holding an item with an object
         * record, and every row the fire-mode panel can draw comes from that item. The loaded roster has
         * none, and since Part 77 the reason is not the save format: the soldiers chunk writes the 27 slot
         * ids and resolves them back (Save_WriteSoldier / Save_ReadSoldier, `TestSoldierTacticalArms` in
         * tests/test_save.c), and this slot's file holds 21 flagged-in records whose slot ids are all 0x7FFF
         * because the mission had nothing to hand out before the save -- `Tact_SetupCraftCargo @ 0x38f00` and
         * `Tact_EquipTeamFromCraft @ 0x3be24` both run, over the shell's own cargo cells, and the shell's
         * fixture craft carries no cargo. That is a gap in the cargo chain's DATA, recorded as a separate
         * issue. The fixture in the screen-3 block stands in for the armed case, as Part 57's hand-menu block does. */
        for (i = 0; i < pMapH->nXcomCount; ++i) {
            Soldier *pTry = pMapH->apXcomUnits[i];

            if (pTry != NULL && pTry->pHandL != NULL && pTry->pHandL->pObj != NULL) { ++nArmed; }
        }
        printf("  weapon census of the loaded roster: %d of %d carry a left-hand item with an object record\n",
               nArmed, pMapH->nXcomCount);
        Check(nArmed == 0, "the loaded roster arrives unarmed because the live one was: the mission fixture hands out no cargo (the slot ids themselves round-trip -- TestSoldierTacticalArms)");

        pMapH->pSelUnit = pUnit;
        pMapH->bActionLatch = 0;              /* Map_bActionLatch @ Map+0x02: the unarmed case */
        g_world.bMapDrawIsoLayer = 1;         /* unk_138E68 @ Map+0x254B0, the gate at 0x2240C */

        /* Back onto the map through a real handler: widget 92 is MenuBtn_ConfirmClear @ 0x194c8, a tail
         * call into Input_OnConfirm, whose screen-1 arm closes the inventory and whose tail stores 0 at
         * 0x15F34. */
        (void)g_bank.aCtl[92].pfnClick();
        Check(g_world.nUiScreen == 0, "the confirm row closes the inventory back onto the map (0x15F34)");

        ClearSurface();
        g_nBlits = 0;
        World_TactFrame(&g_world);
        Check(g_nBlits > 0, "the reference paint: iso layer and the HUD panel, no furniture arm");

        /* ---- screen 3: the fire-mode roster ----
         * Tact_OpenLeftHandMenu @ 0x17030 stores the hand slot into Map.pSelWeapon at 0x17060 before any
         * of its gates, refills the cost/enabled banks, and stores 3 at 0x17140 when a mode is both
         * offered and affordable. TactHud_DrawUnitRoster @ 0x21908 then gates on that same weapon pointer
         * (0x21918), draws only the modes whose Map+0x4E7C entry is non-zero, and rewrites each drawn
         * pad's y to 23 * drawn-so-far + 67 (0x21968). */
        if (pUnit != NULL) {
            static TactItem aPanelItem;
            static ObjectRecFull aPanelObj;
            TactItem *pLSave = pUnit->pHandL;
            TactItem *pWepSave = pMapH->pSelWeapon;
            i32      nGate1 = g_abSlotTopicGate[1];
            u8       bGateSave = g_world.aTopics[nGate1].bUnlocked;

            /* A weapon for the panel: a sort-key-1, category-3 item with gate 1 unlocked is exactly the
             * case Part 57 verified opens screen 3 (0x17140) -- 0x170D0's scan offers mode 3 and the cost
             * auTu*20/100 is affordable, so the store at 0x17138 lands. Stands in for the missing save
             * field recorded above; restored at the end of this block. */
            memset(&aPanelItem, 0, sizeof(aPanelItem));
            memset(&aPanelObj, 0, sizeof(aPanelObj));
            aPanelItem.pObj = &aPanelObj;
            aPanelObj.uSortKey = 1;
            aPanelObj.nCategory = 3;
            g_world.aTopics[nGate1].bUnlocked = 1;
            pUnit->pHandL = &aPanelItem;

            g_bank.aCtl[17].pfnClick();
            Check(g_world.nUiScreen == 3, "widget 17 (Tact_OpenLeftHandMenu) opens screen 3 (0x17140)");
            Check(pMapH->pSelWeapon == pUnit->pHandL,
                  "Map.pSelWeapon is that hand slot -- the same pointer the roster's own gate reads at 0x21918");
            Check(World_bTactFrameLive(&g_world), "screen 3 is on the frame seam now (0x2237C case 3)");

            nVisible = 0;
            for (i = 0; i < PUFO_HUD_ROSTER_PADS; ++i) {
                if (g_world.sTactUi.anActionEnabled[i] != 0) { ++nVisible; }   /* Map+0x4E7C */
            }
            printf("  this weapon offers %d of the %d fire modes\n", nVisible, PUFO_HUD_ROSTER_PADS);
            Check(nVisible > 0 && nVisible <= PUFO_HUD_ROSTER_PADS,
                  "Ui_FillActionCosts offered at least one mode for this weapon");
            /* Part 79: 0x26404/0x2641C. The same fill writes the enabled flag a second time, as
             * bEnabled of bank widgets 32..43, and that byte is what UiControl_HitTest @ 0x1A3F0
             * tests first -- Input_OnPointer hit-tests 32..43 on screen 3, so an unoffered row is
             * unclickable in the original and was clickable here. */
            {
                i32  nPadsOn = 0;
                bool bMirror = true;

                for (i = 0; i < PUFO_HUD_ROSTER_PADS; ++i) {
                    const u8 uPad = g_bank.aCtl[PUFO_HUD_ROSTER_FIRST + i].bEnabled;

                    if ((uPad != 0) != (g_world.sTactUi.anActionEnabled[i] != 0)) { bMirror = false; }
                    if (uPad != 0) { ++nPadsOn; }
                }
                Check(bMirror && nPadsOn == nVisible,
                      "and the twelve pads of that span are enabled exactly where a mode is offered (0x2641C: pBank+0x400+32*m+9)");
            }
            CheckEqI32(PaintFurnitureFrame("tact_hud_fire_modes"), nVisible,
                       "the roster blits one pad per offered mode over bank widgets 32..43 (0x21958)");
            {
                i32  nRow = 0;
                bool bPacked = true;

                for (i = 0; i < PUFO_HUD_ROSTER_PADS; ++i) {
                    if (g_world.sTactUi.anActionEnabled[i] == 0) { continue; }
                    if (g_bank.aCtl[PUFO_HUD_ROSTER_FIRST + i].y != (i16)(23 * nRow + 67)) { bPacked = false; }
                    ++nRow;
                }
                Check(bPacked, "every drawn row's y became 23 * drawn-so-far + 67, so the panel packs its rows");
            }

            /* ---- screen 5: the item sub-mode panel ----
             * TactAction_ConfirmUseItem @ 0x178d4: mode 3, the mode-3 cost from the same table, and for any
             * sort key but 14 the store at 0x179B8 puts 5 in Map_nUiScreen. TactHud_CopyStatusText @ 0x21688
             * centres text record 5 (unk_B3CD1) at y 100 and then prints each of the nine pads' own index. */
            g_bank.aCtl[35].pfnClick();
            Check(g_world.nUiScreen == 5, "the item-confirm pad (widget 35) opens screen 5 (0x179B8)");
            CheckEqI32(PaintFurnitureFrame("tact_hud_sub_modes"), PUFO_HUD_STATUS_PADS,
                       "the sub-mode panel blits its nine pads, bank widgets 44..52 (0x216CC)");

            pUnit->pHandL = pLSave;
            pMapH->pSelWeapon = pWepSave;
            g_world.aTopics[nGate1].bUnlocked = bGateSave;
        }

        /* ---- screen 7: the tactical menu ----
         * Widget 14 is Map_UiScreen7_PlaySample0E @ 0x164b8: stores 7, plays 14. Menu_DrawLabeledPads
         * @ 0x217e0 blits bank widgets 53..59 and labels them with a sparse record map (18..22, 402, 23)
         * -- the same seven widgets whose click handlers are the menu's seven rows. */
        nSampleMark = g_nSamples;
        g_bank.aCtl[14].pfnClick();
        Check(g_world.nUiScreen == 7, "widget 14 opens the tactical menu screen 7 (0x164C0)");
        Check(g_nSamples == nSampleMark + 1 && g_aFrameSamples[nSampleMark] == 14,
              "and plays sample 14 on the way (0x164D4)");
        CheckEqI32(PaintFurnitureFrame("tact_hud_menu"), PUFO_HUD_MENU_PADS,
                   "the menu blits its seven pads, bank widgets 53..59 (0x21824)");

        /* ---- screens 10 and 11: the save and load slot lists ----
         * Menu_OpenSaveSlots @ 0x188a4 and Menu_OpenLoadSlots @ 0x18864: format the seven slot labels,
         * play 16, store 10 / 11, and set MainMenu_nListMode to 0. TactHud_DrawModalPanel @ 0x21cec blits
         * eight pads (105..112) but labels only seven, and opens the one-line modal dialog only when the
         * list mode is exactly 1 -- so these two screens carry labels and no dialog. */
        {
            i32 nFmt = g_nSlotFormatCalls;

            g_bank.aCtl[54].pfnClick();
            Check(g_world.nUiScreen == 10 && g_nSlotFormatCalls == nFmt + 1 && g_nListModeCell == 0,
                  "widget 54 formats the slot list, stores 10 and clears the list mode (0x188A4..0x188B0)");
            CheckEqI32(PaintFurnitureFrame("tact_hud_save_slots"), PUFO_HUD_MODAL_PADS,
                       "the save list blits its eight pads, bank widgets 105..112 (0x21D10)");

            g_bank.aCtl[53].pfnClick();
            Check(g_world.nUiScreen == 11 && g_nSlotFormatCalls == nFmt + 2,
                  "widget 53 opens the load list on 11 (0x18884)");
            CheckEqI32(PaintFurnitureFrame("tact_hud_load_slots"), PUFO_HUD_MODAL_PADS,
                       "and the load list draws the same eight pads with record 1102 (0x21bac)");
        }

        /* ---- screen 12: the options screen ----
         * Widget 55 is Options_SyncUiFromMap_GotoScreen12 @ 0x188e4: clear all 29 pad bytes, re-derive
         * each one from the option block, store 12. TactHud_DrawTopBar @ 0x20ef8 draws four centred
         * records, six left-aligned records at x 40, and blits the 29 pads. */
        g_bank.aCtl[55].pfnClick();
        Check(g_world.nUiScreen == 12, "widget 55 opens the options screen 12 (0x18A34)");
        CheckEqI32(PaintFurnitureFrame("tact_hud_options"), PUFO_HUD_TOPBAR_PADS,
                   "the options screen blits all 29 pads, bank widgets 60..88 (0x21254)");
        {
            i32 nLit = 0;
            bool bSame = true;

            /* The handler keeps the 29 bytes in the option block (0x188F4..0x18A2C); the bank copy is what
             * the drawing arm reads, and World_TactPushOptionPads writes it during the paint just above. */
            for (i = 0; i < 5; ++i) {            /* the speed group is widgets 60..64 */
                if (g_bank.aCtl[PUFO_HUD_TOPBAR_FIRST + i].nState != 0) { ++nLit; }
                if (g_bank.aCtl[PUFO_HUD_TOPBAR_FIRST + i].nState
                    != g_world.sTactMenuPads.abSpeed[i]) { bSame = false; }
            }
            Check(bSame && nLit == 1,
                  "the speed pads 60..64 carry the option block's one-hot byte in the bank now (0x18938)");
        }

        /* ---- screen 15: the nine action buttons ----
         * Widget 15 is Map_UiScreen0F_PlaySample0F @ 0x164d4: store 15, play 15. TactHud_DrawActionButtons
         * @ 0x21430 clears each button's bEnabled first, lets its own rule re-light it, and rewrites a
         * visible button's y to 19 * visible-so-far + 87 -- the rules read the latch byte, the
         * multi-target count and the deploy-panel byte. */
        nSampleMark = g_nSamples;
        g_bank.aCtl[15].pfnClick();
        Check(g_world.nUiScreen == 15, "widget 15 opens the action-button screen 15 (0x164DC)");
        Check(g_nSamples == nSampleMark + 1 && g_aFrameSamples[nSampleMark] == 15, "and plays sample 15 (0x164EC)");
        {
            TactHudActionGate gate;
            i32 nRow = 0;
            bool bPacked = true;

            gate.nActionMode       = pMapH->bActionLatch;
            gate.nMultiTargetCount = pMapH->nMultiTargetCount;
            gate.bDeployPanelOpen  = g_world.sTactInput.bDeployPanelOpen;

            nVisible = 0;
            for (i = 0; i < PUFO_HUD_ACTION_PADS; ++i) {
                if (TactHud_ActionButtonRecord(&gate, i) >= 0) { ++nVisible; }
            }
CheckEqI32(PaintFurnitureFrame("tact_hud_actions"), nVisible,
                       "the action row blits exactly the buttons its own rules kept (0x2158C)");
            for (i = 0; i < PUFO_HUD_ACTION_PADS; ++i) {
                if (TactHud_ActionButtonRecord(&gate, i) < 0) { continue; }
                if (g_bank.aCtl[PUFO_HUD_ACTION_FIRST + i].y != (i16)(19 * nRow + 87)) { bPacked = false; }
                ++nRow;
            }
            Check(bPacked, "every visible button's y became 19 * visible-so-far + 87 (0x215BC)");
            Check(nVisible > 0 && nVisible < PUFO_HUD_ACTION_PADS,
                  "with the latch at 0 some buttons hide and some show: the packed stack is neither empty nor full");
        }

        /* Fixture: the section is over, put the mission back on the map before the teardown. */
        g_world.nUiScreen = 0;
    }

    /* ---- section 13: the descent offer, painted from a real arrival, launched with the pen
     *
     * This is the geoscape's own core loop, walked end to end on the shell's seams:
     *   1. back out of the mission the earlier sections started -- Tact_Shutdown(0) +
     *      Geoscape_InitUI(1) (0x162BC / 0x162C8), the rebuild arm, which is what puts the thirteen
     *      root controls back that the launch seam freed at 0x83ABC;
     *   2. order an intercept with Ship_LaunchIntercept @ 0x5a64c on the shell's own shipEnv: it
     *      parks the leg at the home base (0x5A718/0x5A73C), arms the craft (0x5A790 hot, 0x5A79C
     *      target, 0x5A7A4 off base, 0x5A7AC intercepting) and hands the leg to
     *      StrategEntry_InitTravel, whose bB argument is 0 on this call -- so the launch leaves the
     *      craft's own map entry INVISIBLE (0x7F638);
     *   3. the pulse lights it: Geoscape_PulseUpdate's 0x9552C..0x955FC pass gives every craft that
     *      is not at base its entry's bVisible (0x955C0) and its hot byte (0x955C8). Without that
     *      store the event pass would skip the marker at 0x7FE1C (inactive AND invisible) and the
     *      craft would never move;
     *   4. the event pass StrategList_ProcessEvents @ 0x7fcb0 chases the marker (0x7FED8..0x80504)
     *      and, inside the arrival box (|dx| < 2 && |dy| < 2 at 0x80348/0x80364), dispatches on the
     *      TARGET's type: a type-4 UFO pauses by storing 1 straight into 0x15961C (0x807A0), writes
     *      the intercept pair (0x807B8, 0x807C8), appends the flag-23 line with the ship in colour
     *      slot 0 and the target in slot 1 (0x807D8), and writes the panel's row 0 with TEXTS[1147]
     *      "НЛО" + the target's display id, its aux and the row cursor (StatusPanelRow0, 0x80748);
     *   5. Geoscape_Draw's flag-0x17 arm paints the descent panel (0x9148C..0x91688): the reveal,
     *      TEXTS[1239] + the selected row centred at y 70, the launch control centred at y 102 with
     *      Smap_BeginCraftSelect stored at 0x91578 before its blit, the "ОК" label at y 103, the
     *      soldiers-aboard swap gate (0x915D4..0x91604), and the cancel control at y 125 with
     *      Smap_ResolveInterceptUI at 0x91630;
     *   6. the pen presses it: the press arm's flag-21/22/23 case (0x87658) hit-tests the cancel and
     *      the launch control, the release arm's LABEL_52 (0x89AA0) plays 17 and clicks them, and the
     *      launch control's handler is the shell thunk World_BeginCraftSelectThunk over
     *      World_WireMissionSeams' env;
     *   7. Smap_BeginCraftSelect's flag-23 arm (0x83C60..0x83EF0) reads the mission type off the
     *      alternate tilemap tile under the target (240 desert 6 / 241 jungle 0 / 242 polar 8 /
     *      243 forest 2 / 244 culta 1), clears the mode (0x83D18), takes the tier budget and the
     *      terrain variant from the target's class (0x83E20/0x83E24) and the damage band from its hp
     *      (0x83E6C..0x83ED0), and the mode gate (0x84004) falls to the launch tail: DismissSelected
     *      (0x83AA0), the selection flag cleared (0x83AB4), GeoscapeUi_FreeAll(0) (0x83ABC) and
     *      Tact_InitMission(0) (0x83AC4).
     *
     * What stays open is the pen-side ORDER: bMapSelectionFlag (0x159E84) has six writer chains in
     * the image (OptionsScreen_OnConfirm @ 0x6cf18, Smap_SelectMapTarget @ 0x84484,
     * Smap_HitTestStrategSelect @ 0x82be8, ...) and none of them is walkable through the shell's
     * pointer yet, so the launch itself is reached through the game's own Ship_LaunchIntercept on the
     * shell's own env rather than through a click. Recorded in PLAN.md. */
    {
        Ship           *pCraft = g_world.apCraft[0];
        StrategItem    *pMarker = NULL;
        StrategItem    *pUfo = NULL;
        i32             nMarker;
        i32             nUfo;
        i32             nListCount0;
        i32             nBlitsPlain;
        i32             nBlitsOffer;
        i32             nSampleMark;
        const BaseRec  *pHome;
        i32             nHomeX = 0;
        i32             nHomeY = 0;
        u8              abSpriteBefore[4];
        u8              abSpriteAfter[4];

        printf("section 13: back to the geoscape, order an intercept, arrive, launch\n");

        World_TactEscapeToGeoscape();
        /* Part 93: this is the return arm that KEEPS the map. UiScreen_OnCancel's 13/19 arm (0x162BC /
         * 0x162C8) runs Tact_Shutdown with bKeepSome 0, and the store that empties the live strateg list --
         * LDR R0,=sGeoscapeScreenBlock / ADD R0,#0x3C / BL PtrArray_DeleteAll at 0x3E14C, 0x3E150, 0x3E154 --
         * sits in the bKeepSome != 0 branch, together with the frees of the tactical copies of the rosters.
         * The bKeepSome == 0 branch runs Shutdown_RemoveUnits and Shutdown_MissionOutcome (0x3DCF4) instead,
         * and those reach StrategList_RemoveAt only for the craft's own entry (0x3DF6C, 0x3DFBC). */
        CheckEqI32(PtrArray_GetCount(&g_world.strategList), g_nMarkStrateg,
                  "the mission return left the whole strateg list standing: Tact_Shutdown(0) never reaches "
                  "the DeleteAll at 0x3E154");
        Check(g_world.bTactLive == 0 && g_world.bTactFlag == 0,
              "the escape seam runs Tact_Shutdown(0) and leaves geoscape mode (0x162BC)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 0, "on the geoscape map (0x162C8 rebuilds the UI, not the screen)");
        Check(g_world.ui.apCtl[GEOUI_CTL_186BB0] != NULL && g_world.ui.apCtl[GEOUI_CTL_186BB4] != NULL
                  && g_world.ui.pStatusPanel != NULL,
              "Geoscape_InitUI(1) put the two panel controls and the status panel back (0x86100..0x864DC)");

        /* The descent panel reads line 0, so the list starts empty. UiLineList_Remove @ 0x5a3dc is the
         * remover the original's own answer arms use. */
        while (g_world.msgLines.nLineCount > 0) {
            UiLineList_Remove(&g_world.msgLines, 0);
        }
        CheckEqI32(g_world.msgLines.nLineCount, 0, "the line list is empty before the order");

        Check(pCraft != NULL && pCraft->bAtBase == 1 && pCraft->bIntercepting == 0,
              "the new game's first craft stands at its base with no order (Game_StartNew's own state)");
        nMarker = FindStrategById(&g_world.strategList, (i32)pCraft->bId);
        Check(nMarker >= 0, "the rebuild left the craft its own map entry, matched on Ship.bId");
        if (nMarker >= 0) {
            pMarker = (StrategItem *)PtrArray_Get(&g_world.strategList, nMarker);
        }
        Check(pMarker != NULL && pMarker->nType == 1 && pMarker->bActive == 0 && pMarker->bVisible == 0,
              "and it is the inactive invisible marker 0x7FE1C skips, with bVisible 0");

        /* The contact, parked on the HOME BASE's cell and not on the marker's own cell. A parked craft's
         * entry keeps the coordinates its record was created with. This run's copy came back out of the
         * save chunk, and on the new game `Base_CreateNamedEntry` put base 0 at (-6000, -6000) (0x94964)
         * with its nUnk8 / nUnk12 written at 0x66A38 / 0x66A50 and the craft entries beside it, so the
         * restored marker still sits there: the geoscape moves an entry only when its craft travels.
         * `Ship_LaunchIntercept` is what pulls it onto the base -- 0x5A6E8's bAtBase arm reads
         * apBases[Ship.bHomeBase].nUnk8 / .nUnk12 (0x5A718, 0x5A73C) and writes the pair into the leg start
         * AND the entry's own position (0x5A760, 0x5A780). Parking the contact on that cell parks it where
         * the marker will stand when the order runs, so the arrival box |dx| < 2 && |dy| < 2 (0x80348,
         * 0x80364) closes on the first pass. The chase's own arithmetic -- the wrapped leg, the winner
         * kept, the accumulator whole-part move -- is gated in tests/test_strateg.c against the same
         * function.
         *
         * StrategList_AddItem writes nSiteId / nDisplayId only for a type 0 or 3 (0x82990..0x829A8) or a
         * type 1 (0x829C8..0x829F8); the ninth argument is inert for a type 4, whose two id fields keep
         * whatever the allocation left -- the same in the original, whose type-4 records arrive read raw
         * out of the save chunk (0x41430). So the id the arrival's row text prints is set here. */
        pHome  = (pCraft != NULL && pCraft->bHomeBase < PUFO_FAC_BASE_COUNT)
                     ? g_world.apBases[pCraft->bHomeBase] : NULL;
        nHomeX = (pHome != NULL) ? pHome->nUnk8  : 0;   /* BaseRec.nUnk8 @ +8, written at 0x66A38 */
        nHomeY = (pHome != NULL) ? pHome->nUnk12 : 0;   /* BaseRec.nUnk12 @ +12, written at 0x66A50 */
        Check(pMarker != NULL && pHome != NULL && (pMarker->nX != nHomeX || pMarker->nY != nHomeY),
              "and the restored marker does NOT stand on its base's cell: it holds the coordinates its "
              "record was created with (the new game parked base 0 at -6000,-6000), and the snap onto the "
              "base is the launch's 0x5A6E8 arm");

        nListCount0 = PtrArray_GetCount(&g_world.strategList);
        pUfo = StrategList_AddItem(&g_world.strategList, &g_world.nStrategSeed, g_world.view.nMode,
                                   4 /* a UFO contact */, 0, 2 /* class 2: tier raw 1, budget 250 */,
                                   nHomeX, nHomeY, 4250, 1);
        nUfo = nListCount0;
        if (pUfo != NULL) {
            pUfo->nDisplayId = 3;
        }
        Check(pUfo != NULL && PtrArray_GetCount(&g_world.strategList) == nListCount0 + 1,
              "the contact is in the strateg list (0x82704)");
        Check(pUfo != NULL && pUfo->nType == 4, "as a type-4 item, the type the arrival dispatch reads (0x803A4)");

        /* 1. The order, on the shell's own shipEnv (World_WireMissionSeams). */
        (void)Ship_LaunchIntercept(&g_world.shipEnv, 0, nUfo);
        Check(pCraft->bWeaponsHot == 1 && pCraft->bAtBase == 0 && pCraft->bIntercepting == 1
                  && pCraft->nStrategIdx == nUfo,
              "0x5A790 hot, 0x5A7A4 off base, 0x5A7AC intercepting, 0x5A79C holding the target index");
        Check(pMarker->nTimer == 0 && pMarker->nX == pMarker->nStartX && pMarker->nY == pMarker->nStartY,
              "the entry's timer restarts (0x5A67C) and its leg starts where the base is (0x5A6A4..0x5A780)");
        Check(pMarker->bVisible == 0, "and the launch leaves it invisible -- bB is 0 at 0x7F638");

        /* 2. The pulse is what makes the marker chaseable. */
        World_PulseGeoscape(&g_world);
        Check(pMarker->bVisible == 1 && pCraft->bWeaponsHot == 1,
              "Geoscape_PulseUpdate lights the entry of a craft that is not at base (0x955C0, 0x955C8)");

        /* 3. The event pass, on the hook Geoscape_TickBases calls. The pulse above may have posted
         * its own lines, so the list is emptied again: the flag-23 line has to be line 0 for the
         * panel and for the pen arm to read it. The empty-list frame is the baseline the offer frame
         * is compared against. */
        while (g_world.msgLines.nLineCount > 0) {
            UiLineList_Remove(&g_world.msgLines, 0);
        }
        ClearSurface();
        (void)DrawFrame();
        nBlitsPlain = g_nBlits;

        Check(g_world.geoHooks.pfnProcessEvents != NULL, "the shell wired the event-pass hook (World_HookProcessEvents)");
        g_world.geoHooks.pfnProcessEvents(0, g_world.geoHooks.pUser);

        CheckEqI32(g_world.msgLines.nLineCount, 1, "one line posted: the arrival offer (0x807D8)");
        CheckEqI32(g_world.msgLines.nLineCount > 0 ? (i32)UiLineList_GetFlag(&g_world.msgLines, 0) : -1, 23,
                   "with flag 23, the descent offer (0x807D8)");
        CheckEqI32(g_world.msgLines.nLineCount > 0 ? (i32)UiLineList_GetColor(&g_world.msgLines, 0, 0) : -1, 0,
                   "colour slot 0 carries the ship index (0x807D8 a3)");
        CheckEqI32(g_world.msgLines.nLineCount > 0 ? (i32)UiLineList_GetColor(&g_world.msgLines, 0, 1) : -1, nUfo,
                   "and slot 1 the target index (0x807D8 a4)");
        CheckEqI32(g_world.geo.time.bPaused, 1, "the arrival pauses by a store, not by GameTime_Pause (0x807A0)");
        CheckEqI32(g_world.nInterceptShipIdx, 0, "the intercept pair holds the ship (0x807B8)");
        CheckEqI32(g_world.nInterceptStrategIdx, nUfo, "  and the target (0x807C8)");
        {
            /* The caption is compared as BYTES against the loaded record, not against a literal: the
             * TEXTS block is cp1251 and this source file is not. */
            const char *pszCap = (const char *)g_assets.pTextRecs + 1147 * PUFO_TEXT_REC_STRIDE;
            const char *pszRow = (g_world.ui.pStatusPanel->apLines[0] != NULL)
                                     ? (const char *)g_world.ui.pStatusPanel->apLines[0] : NULL;
            char szWant[24];

            snprintf(szWant, sizeof(szWant), "%d", pUfo->nDisplayId);
            Check(pMarker != NULL && pCraft->bIntercepting == 1,
                  "the order survives the arrival: the craft is still chasing (a type-4 target clears nothing)");
            Check(pszRow != NULL && strncmp(pszRow, pszCap, strlen(pszCap)) == 0,
                  "the status panel's row 0 starts with TEXTS[1147], the UFO caption (StrategCaption, 0x80708)");
            Check(pszRow != NULL && strcmp(pszRow + strlen(pszCap), szWant) == 0,
                  "  and continues with the target's display id (StatusPanelRow0's \"%s%d\", 0x80748)");
            CheckEqI32((i32)g_world.ui.pStatusPanel->anLineAux[0], nUfo, "with its aux holding the target (0x80748)");
            CheckEqI32((i32)g_world.ui.pStatusPanel->nUnk3032, 0, "and the row cursor reset (0x80760)");
        }

        /* 4. The frame paints the offer. */
        ClearSurface();
        (void)DrawFrame();
        nBlitsOffer = g_nBlits;
        printf("  the offer frame blits %d pads over the empty-list frame's %d\n",
               (int)nBlitsOffer, (int)nBlitsPlain);
        Check(nBlitsOffer > nBlitsPlain, "the flag-0x17 arm painted over the empty-list frame (0x91498)");
        {
            const UiControl *pCtlB = g_world.ui.apCtl[GEOUI_CTL_186BB0];
            const UiControl *pCtlA = g_world.ui.apCtl[GEOUI_CTL_186BB4];
            Check(pCtlB->y == PUFO_STATUS_TARGETCTL_Y_CANCEL && pCtlA->y == PUFO_STATUS_TARGETCTL_Y_OK2,
                  "the launch control at y 102 and the cancel at y 125 (0x9156C, 0x91624)");
            Check(pCtlB->pfnClick == g_world.frame.panel.pfnBeginCraftSelect
                      && pCtlB->pfnClick != NULL,
                  "0x91578 stored the shell's own launch handler on the control");
            Check(pCtlA->pfnClick == g_world.frame.panel.pfnResolveInterceptUI
                      && pCtlA->pfnClick != NULL,
                  "and 0x91630 the cancel handler on the other one");
            Check(pCtlB->x == 120 - ((i32)pCtlB->uWidth / 2), "and centred it on x 120 (0x91554..0x91560)");
        }
        memcpy(abSpriteBefore, g_world.abMapSpriteSet, 4);
        DumpFrame("geoscape_descent_offer");

        /* 5. The pen presses the offer's launch control. The press arm's flag-21/22/23 case (0x87658)
         * hit-tests 186BB4 then 186BB0 with real coordinates, and only the control that got the press
         * fires: the release passes x = y = 0 and never measures the box (0x1A540), so the cancel control
         * beside it stays silent. */
        {
            UiControl *pCtl  = g_world.ui.apCtl[GEOUI_CTL_186BB0];
            const u8    nTile  = Tilemap_GetAt(g_world.ui.apPad[GEOUI_PAD_186C18], pUfo->nX, pUfo->nY);
            const i32   nType0 = g_world.nMapMissionType;
            /* 0x83CD4..0x83D00 maps five alt tiles and nothing else: the default arm is a BREAK, so the
             * type byte keeps whatever the previous mission's arm wrote. The previous launch here was the
             * assault, whose own arm stores 3 at 0x840FC. */
            const i32   nWant  = (nTile == 240) ? PUFO_MISSION_TYPE_DESERT
                                 : (nTile == 241) ? PUFO_MISSION_TYPE_JUNGLE
                                 : (nTile == 242) ? PUFO_MISSION_TYPE_POLAR
                                 : (nTile == 243) ? PUFO_MISSION_TYPE_FOREST
                                 : (nTile == 244) ? PUFO_MISSION_TYPE_CULTA : nType0;

            nSampleMark = g_nSamples;
            World_GeoscapePointer(&g_world, pCtl->x + (i32)pCtl->uWidth / 2,
                                  pCtl->y + (i32)pCtl->uHeight / 2, true);
            (void)World_GeoscapePointerUp(&g_world);

            Check(g_nSamples > nSampleMark && g_aFrameSamples[nSampleMark] == 17,
                  "the release arm plays 17 before it clicks (0x89AA0 PtrUp_Sample); the mission's own "
                  "Sound_Init and music passes are the samples after it");
            printf("  the alt tile under the contact is %d: mission type %d -> %d, tier raw %d, variant %d, "
                   "band %d, layers %d\n", (int)nTile, (int)nType0, (int)g_world.nMapMissionType,
                   (int)g_world.nMapMissionTierRaw, (int)g_world.nMapTerrainVariant,
                   (int)g_world.bMapUnk15115, (int)g_world.nMapMapLayers);

            /* 6. What the launch arm wrote, read back off the shell's Map header. */
            CheckEqI32(g_world.bMapMissionMode, 0, "the flag-23 arm clears the mission mode (0x83D18)");
            CheckEqI32(g_world.nMapMissionType, nWant,
                       "the type is what the alt tile maps, or the previous mission's value when the tile "
                       "is outside 240..244 (0x83CB0..0x83D00, the default arm writes nothing)");
        }
        CheckEqI32(g_world.nMapMissionTierRaw, 1, "class 2 of the 0x83D2C table: tier raw 1 (budget 250)");
        CheckEqI32(g_world.nMapTerrainVariant, 2, "  and terrain variant variant+1 = 2 (0x83E24)");
        CheckEqI32(g_world.nMapMapLayers, (i32)pCraft->bCraftType + 1,
                   "the layer count comes off the selected craft's class (0x83EF0)");
        CheckEqI32(g_world.nMapCraftIndex, 0, "the selection pass put line 0's slot 0 into Map_nCraftIndex (0x839B8)");
        CheckEqI32(g_world.nSelectedStrategIdx, nUfo, "  and slot 1 into 0x186BE4 (0x839D8)");
        memcpy(abSpriteAfter, g_world.abMapSpriteSet, 4);
        Check(memcmp(abSpriteAfter, abSpriteBefore, 4) == 0,
              "the sprite set stands: both the contact and the marker carry bRace 255, outside the five races (0x83BB0..0x83C40)");
        CheckEqI32(g_world.bMapSelectionFlag, 0, "the tail cleared the selection flag (0x83AB4)");
        Check(g_world.bMapTickHold == 0 && g_world.bMapBattleEnded == 0,
              "and never reached the bad-end pair (0x84238, 0x84240) -- that block is the mode-1 assault's");

        /* 7. The dismiss the head of the tail runs (0x83AA0, the mode != 1 arm), then the hand-over. The
         * visibility pass at 0x838FC..0x83988 only touches a craft that is ALREADY at base or already cold;
         * this one was hot and off base, so it passed through unchanged and the order is undone by the
         * dismiss instead: Ship_RetargetTravel @ 0x84448 sends the craft to its home base with bB 1 -- the
         * store at 0x7F638 that makes a homing craft's entry VISIBLE, the opposite of what the launch's
         * bB 0 did -- and clears the intercept byte at 0x5A59C. The contact is type 4, not a waypoint, so
         * the 0x5A4E8 removal does not run and it stays on the map. */
        Check(g_world.msgLines.nLineCount == 0,
              "Smap_DismissSelected dropped the descent offer's line (0x83AA0, then the tail's own clear at 0x83AB4)");
        CheckEqI32((i32)pCraft->bIntercepting, 0, "the retarget cleared the intercept byte (0x5A59C)");
        Check(pCraft->bAtBase == 0 && pCraft->bWeaponsHot == 1,
              "and sent it home rather than parking it: bAtBase and the hot byte are the visibility pass's "
              "test only (0x8393C), which skipped a hot craft that was off base");
        Check(pMarker != NULL && pMarker->bVisible == 1,
              "the homing leg's bB 1 lights the map entry again (0x7F638) -- the launch's bB 0 was the exception");
        Check(g_world.bTactLive == 1 && g_world.bTactFlag == 1,
              "the tail ran Tact_InitMission(0) (0x83AC4) and the mission is live");
        Check(g_world.pTactMap != NULL && (i32)g_world.pTactMap->nMissionType == g_world.nMapMissionType,
              "the frame's own header carries the type the alt tile picked");

        /* 8. The Cydonia briefing the launch pulled in. Map_bSpawnAlt @ Map+0x15148 is a saved byte
         * (Save_Read/WriteTGameData @ 0x44410 / 0x4486C) and 0x83A74 -- the assault tail's own arm -- is
         * the only writer of 1 in the mission-launch code, so the byte is still 1 from the earlier assault
         * launch and 0x3D2A0 runs the briefing on this mission too. */
        {
            const UiPanel *pPanel = g_world.tactEnv.apPanel[PUFO_BRIEF_PANEL];
            const char *pszTail = (const char *)g_assets.pTextRecs + PUFO_BRIEF_TAIL;
            i32 nLast;

            CheckEqI32((i32)g_world.bMapSpawnAlt, 1,
                       "Map_bSpawnAlt is still 1: the assault launch wrote it and the descent launch reads it (0x3D2A0)");
            CheckEqI32((i32)g_world.nUiScreen, 20, "the briefing gate set Map_nUiScreen to 20 (0x3D2B0)");
            CheckEqI32(g_world.nMapUiSlot11, 1,
                       "  and saved into apUiSlotsA[11] the id the mission had at that moment: the prologue "
                       "cleared it (0x3C67C) but the new-mission path's own Inv_OpenScreen wrote 1 (0x3D0B8), "
                       "so 0x3D29C stores 1, not 0");
            Check(g_world.pTactBriefPad != NULL, "the briefing pad was allocated and loaded (0x3D2B4..0x3D2E0)");
            Check(pPanel != NULL, "the briefing panel is the mission's own (unk_1571EC, 28 lines)");
            if (pPanel != NULL) {
                nLast = pPanel->nLineCount - 1;
                Check(pPanel->apLines[nLast] != NULL
                          && strcmp((const char *)pPanel->apLines[nLast], pszTail) == 0,
                      "the closing slice is the aTextRecs byte offset 0x38B49, the record the original "
                      "names unk_EC419 at 0x3D510 -- a byte offset, not a record index");
            }
        }
        /* The mission's own seam: Map_nUiScreen is 20, so this is the briefing the launch pulled in --
         * the "cydonia" pad full-screen with the panel's centred closing line. */
        ClearSurface();
        World_TactFrame(&g_world);
        DumpFrame("tact_mission_from_descent");
    }

    /* ---- section 14: the pen orders a craft -- the chain from the click to the fly-to-point OK
     *
     * Section 13 ordered the intercept with the game's own Ship_LaunchIntercept. Here the order is the
     * pen's, along the seams the original chains together:
     *
     *   1. Smap_HitTestStrategSelect @ 0x82be8, reached from the press dispatcher's generic arm (0x87C10,
     *      and once more at 0x87C94). No message line is outstanding, so the dispatcher reads flag byte 0,
     *      falls through its special arms and runs the select test; the marker's cell under the pointer
     *      builds the type-1 inactive line (0x830B4, 0x830F4), named by Ship_FormatDisplayName of the craft
     *      whose Ship.bId equals the item's display id (the walk at 0x830F8..0x83158).
     *   2. The one-line dispatch (0x83394), type-1 inactive arm (0x83688): the ship index into 0x187054 and
     *      0x159E74, the clock paused, and a flag-11 line that carries that index in COLOUR SLOT 0 -- the
     *      only one of the six that keeps the line its scan built.
     *   3. Geoscape_Draw's flag-0x0B arm (0x8F9E4..0x9021C) paints the craft's card and installs
     *      Smap_SelectMapTarget @ 0x84484 on its "new target" control (0x8FFE8), and the press arm for flag
     *      11 (0x87938) is the one that hit-tests that control.
     *   4. Smap_SelectMapTarget puts bMapSelectionFlag up (0x84498), reads the ship index back out of the
     *      line (0x844A4 / 0x844C4), replaces line 0 with the flag-27 "choose a target" prompt keeping that
     *      index in colour slot 0 (0x84574), removes the old line (0x84580) and pauses (0x84588).
     *   5. A press on an empty cell: flag 27 is not one of the special press arms, so the select test runs
     *      again, builds no line, and its tail (0x83708..0x837BC) clears bMapSelectionFlag, publishes the
     *      clicked cell into 0x186414 / 0x186418, and re-inserts line 0 as flag 12 with the ship index in
     *      colour slot 0.
     *   6. Geoscape_Draw's flag-0x0C arm (0x8F5B4..0x8F758) installs Smap_ConfirmCraftTarget @ 0x842ec on its
     *      upper control; the flag-12 press arm (0x8765C) hit-tests it, and the confirm writes a fresh
     *      type-5 waypoint at the published cell (0x843AC) and launches the craft at it (0x843D8).
     *
     * Fixture: the type-4 contact section 13 left sitting on the base cell comes back out through the same
     * remover the order code itself uses (StrategList_RemoveAt @ 0x82ab0, the call sites 0x84364 and
     * 0x84504), because two items in one cell make the scan build two lines and the >1 branch (0x832B0) is
     * the multi-select box -- that branch is pinned in tests/test_smaphit.c. */
    {
        Ship             *pCraft   = NULL;
        UiControl        *pCtlTarget = NULL;
        UiControl        *pCtlOk     = NULL;
        UiControl        *pCtlCancel = NULL;
        i32   nMarker, nContact, nCountBefore, nCountAfter;
        i32   nCellX = 0, nCellY = 0;
        i32   nPressX = 0, nPressY = 0;
        i32   nEmptyX = 0, nEmptyY = 0;
        i32   nWaypoint = -1;
        i32   nBlitsCard, nBlitsPick, nBlitsFly, nBlitsPlain;
        char  szName[64];

        printf("section 14: the pen-side order -- click, card, target prompt, fly-to-point\n");

        /* The escape runs Geoscape_InitUI(1), which allocates the thirteen control singletons afresh
         * (0x86100..0x864DC) after GeoscapeUi_FreeAll released the old ones (0x87008..0x8704C). Every UI
         * pointer this section uses is therefore read out of g_world.ui AFTER that call, never cached
         * across it. */
        World_TactEscapeToGeoscape();
        pCraft     = g_world.apCraft[0];
        pCtlTarget = g_world.ui.apCtl[GEOUI_CTL_186B9C];
        pCtlOk     = g_world.ui.apCtl[GEOUI_CTL_186BB4];
        pCtlCancel = g_world.ui.apCtl[GEOUI_CTL_186BB0];
        Check(g_world.bTactLive == 0 && g_world.screen.nGeoscapeScreen == 0,
              "back on the geoscape map, with the mission behind (0x162BC, 0x162C8)");
        CheckEqI32(g_world.msgLines.nLineCount, 0, "no message line outstanding");
        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 0,
                   "and the flag byte the press arm reads is 0: the offer's removal shifted the empty slot up "
                   "(0x5A428 shifts flags[k] = flags[k+1]), so the dispatcher takes its generic arm");

        nContact = FindStrategById(&g_world.strategList, 3);
        nCountBefore = PtrArray_GetCount(&g_world.strategList);
        if (nContact >= 0) {
            (void)StrategList_RemoveAt(&g_world.strategList, &g_world.geo.world, nContact,
                                       g_world.shipEnv.pnSpawnOnceFlag, g_world.shipEnv.pSpawnCtx);
        }
        nCountAfter = PtrArray_GetCount(&g_world.strategList);
        printf("  the contact at index %d removed: %d items -> %d\n", nContact, nCountBefore, nCountAfter);
        Check(nContact >= 0 && nCountAfter == nCountBefore - 1,
              "the section's own contact is out of the list, so one item is under the pointer");

        nMarker = FindStrategById(&g_world.strategList, (i32)pCraft->bId);
        if (nMarker >= 0) {
            StrategItem *pItem = (StrategItem *)PtrArray_Get(&g_world.strategList, nMarker);

            Check(nMarker >= 0 && pItem != NULL && pItem->nType == 1 && pItem->bActive == 0
                      && pItem->bVisible == 1,
                  "the craft's own entry is the inactive visible item the select test builds a line for (0x830F4)");
            if (pItem != NULL) {
                /* Part 94 moved the base's own entry onto the base's cell, so this cell is shared and
                 * the press on it is no longer a single-item press. One counter feeds three stores: the
                 * base record's site byte (BaseRec_ctor 0x556C0 reads the counter straight out of
                 * nNextBaseSiteId @ 0x186558), the entry's nSiteId (0x669A8) and the site record's
                 * nFacType (0x66A34). The placement commit then walks the list for the entry whose nSiteId equals
                 * the base's byte (0x8B598, LDRSB) and writes the new cell into it (0x8B5E4 / 0x8B5F8).
                 * Base 0 was placed at (400,158) by this run's own flow, and Ship_LaunchIntercept's
                 * bAtBase arm (0x5A718 / 0x5A73C reading the base's nUnk8 / nUnk12 into the leg start and
                 * into the entry at 0x5A760 / 0x5A780) put the marker on that same cell. The -8..7 scan
                 * (0x82C48) therefore finds two items, and 0x832B0 answers a >1 count with the
                 * multi-select box -- base row named by 0x82E94/0x82F1C, craft row named by
                 * Ship_FormatDisplayName -- which is what the original does on that cell.
                 * The card dispatch this section walks is the 1-line path, so the fixture gives the
                 * marker a cell of its own, 50 columns left: outside the scan window and clear of the
                 * other two base entries ((-6000,-6000) unplaced, (587,165)). 50 and not 40 because the
                 * fly-to-point press is "40 columns off the marker's cell", and 40 would land it exactly
                 * on the base's cell -- the waypoint would then share a cell with the base entry and
                 * section 15's two-row box would have three rows. */
                nCellX = pItem->nX - 50;
                nCellY = pItem->nY;
                pItem->nX = nCellX;   /* the cell is also copied out: an added item can realloc the array */
                pItem->nY = nCellY;
            }
        }
        Check(ScreenForCell(&g_world.view, nCellX, nCellY, &nPressX, &nPressY),
              "and its cell has a screen pixel at the current scroll (the inverse of 0x82C84 / 0x82CAC)");
        printf("  marker at cell %d,%d pressed at screen %d,%d; strip height %d so the map runs to y %d\n",
               nCellX, nCellY, nPressX, nPressY,
               (i32)g_world.ui.apPad[GEOUI_PAD_186BBC]->uHeight,
               PUFO_SURF_H - (i32)g_world.ui.apPad[GEOUI_PAD_186BBC]->uHeight);

        /* A. First the press on the base's OWN entry, while the escape's fresh thirteen controls are
         * still un-installed (0x86100..0x864DC) -- the case-18 tail hit-tests *pGeoscapeCtlA /
         * *pGeoscapeCtlB (0x8932C), and at this point neither hotspot carries a handler yet.
         *
         * The base's entry now stands on the base's cell, so this press is the original's own "click
         * your base". The scan finds the one inactive type-0 item, its row is named by 0x82E94 ->
         * SmapHit_FindBaseBySiteId -> apSiteRecs[nBase].abName (0x82F1C), and the dispatch takes the
         * type-0 inactive arm 0x83448: row cursor cleared (0x83450), GameTime_Pause (0x83458),
         * *pnScreenId = 18 (0x83464), OptionsScreen_OpenWithSound(0, nSiteId, pOptionsHooks) (0x83480)
         * and pOptPanelShips->bSelLineHighlight = 0 (0x83494), which the shell reads out of its own panel cell
         * (OptionsOwners.apPanel[0]) now that Part 95 owns the screen. The arm writes no message line and
         * no flag byte.
         * Before Part 94 this arm was unreachable through the shell: the broken pairing left the entry at
         * (-6000,-6000), outside every scan window, so pressing a base built no line. Reaching it exposed
         * the next gap: Geoscape_OnPointer's case-18 arm derefs gOptionsScratch[4] @ 0x159E8C (0x89110)
         * and bOptionsScreenOpen @ 0x159E80 (0x89338) with no null test -- the original needs none,
         * because those globals are always there -- and the shell owned neither, so the press after this
         * one crashed in the ported arm. Part 95 gave the shell that state (OptionsOwners, wired by
         * World_WireOptions), and the rest of this block walks the arm through it: the overlay Open
         * builds, the confirm tail, the cancel hotspot's close, and the double call. */
        {
            const BaseRec     *pBaseRec  = g_world.apBases[0];
            const StrategItem *pBaseItem = NULL;
            i32 nGCellX = 0, nGCellY = 0, nGPressX = 0, nGPressY = 0;
            i32 nLines0 = g_world.msgLines.nLineCount;
            i32 nPause0 = g_world.geo.time.bPaused;
            i32 nSampleMark = g_nSamples;
            i32 nBlitsGeo0 = 0;
            i32 nMsgShip0 = g_world.nMsgShipIdx;
            i32 nSelShip0 = g_world.nSelectedShipIdx;
            i32 k;

            printf("  the base's own entry first\n");
            Check(pBaseRec != NULL, "the run's placed base is still in the array");
            for (k = 0; k < PtrArray_GetCount(&g_world.strategList); ++k) {
                const StrategItem *pCand = (const StrategItem *)PtrArray_Get(&g_world.strategList, k);
                if (pCand != NULL && pCand->nType == 0 && pBaseRec != NULL
                    && pCand->nSiteId == (i32)(i8)pBaseRec->bUnk4) {
                    pBaseItem = pCand;
                    break;
                }
            }
            Check(pBaseItem != NULL,
                  "its entry is found by the same byte the hit test searches on (0x82ED4)");
            if (pBaseItem != NULL) {
                nGCellX = pBaseItem->nX;
                nGCellY = pBaseItem->nY;
            }
            printf("  base 0 site %d cell %d,%d; its entry at %d,%d; the site record names it \"%s\"\n",
                   (pBaseRec != NULL) ? (i32)(i8)pBaseRec->bUnk4 : -1,
                   (pBaseRec != NULL) ? pBaseRec->nUnk8 : 0,
                   (pBaseRec != NULL) ? pBaseRec->nUnk12 : 0,
                   nGCellX, nGCellY, g_world.aSiteRecs[0].abName);
            CheckEqI32(g_world.bMapSelectionFlag, 0,
                       "no map selection is up, so the base-name branch runs (0x82E94)");
            CheckEqI32(g_world.bFlag1870A0, 0,
                       "and no multi-select box is up, so the press reaches the map arm (0x87ABC)");
            Check(ScreenForCell(&g_world.view, nGCellX, nGCellY, &nGPressX, &nGPressY),
                  "the base's cell has a screen pixel at the current scroll (the inverse of 0x82C84 / 0x82CAC)");

            /* The plain map first, so the overlay frame below has something to be compared with. */
            ClearSurface();
            (void)DrawFrame();
            nBlitsGeo0 = g_nBlits;

            /* A1. Hold the tap cooldown at 6. 0x87C48 gates the SECOND Smap_HitTestStrategSelect call --
             * the residue the port reproduces at 0x87C94, which otherwise runs the very arm again inside
             * the same press -- so with the cooldown held the arm runs once and the base's craft list
             * stays open, which is what 0x855C8's status-panel path does on its own. */
            nSampleMark = g_nSamples;
            g_world.frame.g.nTapCooldown = 6;
            World_GeoscapePointer(&g_world, nGPressX, nGPressY, true);

            CheckEqI32(g_world.ui.pStatusPanel->nLineCount, 1,
                       "the scan built one line, for the base entry alone");
            Check(g_world.ui.pStatusPanel->apLines[0] != NULL
                      && strcmp((const char *)g_world.ui.pStatusPanel->apLines[0],
                                g_world.aSiteRecs[0].abName) == 0
                      && g_world.aSiteRecs[0].abName[0] != 0,
                  "and the row is the base's own name: the site-id search answers the base and 0x82F1C copies "
                  "byte_186C24 + 54 (the name the placement commit wrote at 0x8B5E4..0x8B5F8's own label store)");
            CheckEqI32(g_world.msgLines.nLineCount, nLines0,
                       "the arm posts no message line of its own");
            CheckEqI32((i32)g_world.ui.pStatusPanel->nUnk3032, 0, "it clears the panel's row cursor (0x83450)");
            CheckEqI32(g_world.geo.time.bPaused, 1, "it pauses (0x83458)");
            CheckEqI32(g_world.screen.nGeoscapeScreen, 18, "and it opens screen 18 (0x83464)");
            CheckEqI32(g_world.bMapSelectionFlag, 0,
                       "the type-0 inactive arm is one of the six that writes no selection byte");
            Check(g_nSamples == nSampleMark + 1 && g_aFrameSamples[nSampleMark] == 7,
                  "0x6DD10 plays Sound_Play(7, 4) before it tail-calls the open (OptionsScreen_OpenWithSound)");
            CheckEqI32(g_world.bOptionsScreenOpen, 1,
                       "the toggle OptionsScreen_Open writes at 0x6D0C8: the arm's 0x83480 call now runs for "
                       "real, through OptionsScreen_OpenWithSound -> World_OptionsOpenRun -> 0x6d0ac");
            CheckEqI32((i32)g_world.abOptionsScratch[4], 0,
                       "the menu byte stays 0: only OptionsScreen_Init sets it, and the menu itself opens from "
                       "root control [8] (0x186518), not from this arm");
            {
                UiControl *pCtlA  = g_world.ui.apCtl[GEOUI_CTL_186BB4]; /* *pGeoscapeCtlA @ 0x186BB4 */
                UiControl *pPairA = g_world.options.apPairCtl[0];
                UiControl *pPairB = g_world.options.apPairCtl[1];
                UiPanel   *pName  = g_world.options.apPanel[0];
                UiPanel   *pStores = g_world.options.apPanel[1];
                UiPanel   *pRegion = g_world.options.apPanel[2];
                UiPanel   *pCargo  = g_world.options.apPanel[3];
                i32 nOpenBaseIdx = -1;
                i32 nHomed = 0;
                i32 nGSiteId = (pBaseItem != NULL) ? pBaseItem->nSiteId : -1;

                /* The same search the callee runs at 0x6D118..0x6D148: the base whose site byte equals the
                 * entry's, over the SIGNED base-count byte at 0x159D80. */
                for (k = 0; k < (i32)g_world.nBaseCount; ++k) {
                    if (g_world.apBases[k] != NULL
                        && (i8)g_world.apBases[k]->bUnk4 == nGSiteId) {
                        nOpenBaseIdx = k;
                        break;
                    }
                }
                for (k = 0; k < (i32)g_world.bCraftCount; ++k) {
                    if (g_world.apCraft[k] != NULL
                        && (i32)g_world.apCraft[k]->bHomeBase == nOpenBaseIdx) {
                        ++nHomed;
                    }
                }

                Check(pCtlA != NULL, "the cancel hotspot the arm re-arms is the shell's own [0x186BB4] cell");
                Check(pCtlA != NULL && pCtlA->pfnClick != NULL,
                      "0x6D24C installs Options_SaveAudioSettings on it");
                CheckEqI32(pCtlA != NULL ? pCtlA->x : 0,
                           pCtlA != NULL ? 120 - ((i32)pCtlA->uWidth >> 1) : 0,
                           "0x6D25C centres it: x = 120 - (uWidth >> 1)");
                CheckEqI32(pCtlA != NULL ? pCtlA->y : 0, 179, "0x6D26C drops it to y = 179");
                Check(pPairA != NULL && pPairB != NULL,
                      "the prev/next pair is allocated into aOptionsListCtl[7] / [8] (0x6D1B0, 0x6D1C0)");
                Check(pPairA != NULL && pPairA->pfnClick != NULL,
                      "with OptionsScreen_PanelPrev installed at 0x6D208");
                Check(pPairB != NULL && pPairB->pfnClick != NULL,
                      "and OptionsScreen_PanelNext at 0x6D214");
                Check(pName != NULL && pStores != NULL && pRegion != NULL && pCargo != NULL,
                      "and the four columns are built (0x6D270, 0x6D3BC, 0x6D490, 0x6D55C)");
                CheckEqI32(nOpenBaseIdx, 0, "0x6D12C finds the run's base by the site byte the entry carries");
                CheckEqI32(pName != NULL ? pName->nLineCount : -1, nHomed,
                           "one row per craft homed at that base (0x6D16C..0x6D190 count, then UiPanel_ctor)");
                CheckEqI32(pStores != NULL ? pStores->nLineCount : -1, nHomed,
                           "the same count in the other three columns");
                Check(pName != NULL && pName->pfnClick != NULL,
                      "0x6D2E8 installs OptionsScreen_OnConfirm as the names panel's click handler");
                CheckEqI32(pName != NULL ? pName->bSelLineHighlight : -1, 0,
                           "and the arm's own 0x83494 clears that panel's bSelLineHighlight right after Open, so no "
                           "row is selected yet");

                printf("  the overlay: base %d, %d craft homed; ctlA %d,%d %ux%u state %d; pair %d,%d / %d,%d; "
                       "columns %d rows\n",
                       nOpenBaseIdx, nHomed,
                       pCtlA != NULL ? (i32)pCtlA->x : 0, pCtlA != NULL ? (i32)pCtlA->y : 0,
                       pCtlA != NULL ? (u32)pCtlA->uWidth : 0u, pCtlA != NULL ? (u32)pCtlA->uHeight : 0u,
                       pCtlA != NULL ? (i32)pCtlA->nState : -1,
                       pPairA != NULL ? (i32)pPairA->x : -1, pPairA != NULL ? (i32)pPairA->y : -1,
                       pPairB != NULL ? (i32)pPairB->x : -1, pPairB != NULL ? (i32)pPairB->y : -1,
                       pName != NULL ? pName->nLineCount : -1);
                for (k = 0; k < nHomed && pName != NULL; ++k) {
                    printf("    row %d: \"%s\" | \"%s\" | \"%s\" | \"%s\"  aux %d / %d\n", k,
                           (const char *)pName->apLines[k],
                           (pStores != NULL) ? (const char *)pStores->apLines[k] : "",
                           (pRegion != NULL) ? (const char *)pRegion->apLines[k] : "",
                           (pCargo != NULL) ? (const char *)pCargo->apLines[k] : "",
                           pName->anLineAux[k], (pStores != NULL) ? pStores->anLineAux[k] : -999);
                }
                /* The overlay on the picture: the frame's case-18 arm (0x942F4 -> Geoscape_DrawScreen
                 * @ 0x6c16c) now paints from the shell's own env, fed by World_WireOptions and refreshed
                 * every frame by World_RefreshGeoscapeFrame. Its own expansion animation gates it:
                 * OptionsScreen_Open cleared the start byte at 0x6D0DC, so Ui_AnimExpandBlit @ 0x95bf0
                 * re-arms at progress 0 and mode 2 grows the height 50 per step (0x95CB0) until it reaches
                 * the 180 the call asks for (0x6C380), at which point 0x95D60 parks the progress at 100 and
                 * only then does any of the overlay draw. The app's WM_TIMER branch steps that byte
                 * (0xA0C88); this fixture has no timer, so it steps it the same way, drawing between the
                 * steps, and the picture is taken from the settled frame. */
                {
                    i32 nFrame;
                    i32 nBlitsOver = 0;

                    /* The byte is at 100 from the earlier sections, and the first frame after Open finds
                     * the start byte cleared (0x6D0DC), so it re-arms at 0 (0x95C14) and the run starts
                     * again; the loop is written for that, not for the value the byte happens to hold. */
                    for (nFrame = 0; nFrame < 8; ++nFrame) {
                        if (g_world.cUiAnimProgress < 101) { ++g_world.cUiAnimProgress; }
                        ClearSurface();
                        (void)DrawFrame();
                        nBlitsOver = g_nBlits;
                        if (g_world.cUiAnimProgress == 100) { break; }
                    }
                    CheckEqI32(g_world.cUiAnimProgress, 100,
                                "the expansion ran to its end: mode 2 clamps the height to the 180 the call "
                                "asks for and 0x95D60 parks the progress at 100");
                    /* The three control pads the case-18 arm blits at 0x6C59C and 0x6C5DC -- the cancel
                     * hotspot and aOptionsListCtl[7] / [8] -- come through the test's own pad seam, so the
                     * seam count is the direct evidence that this arm ran on the shell's env. */
                    Check(nBlitsOver > nBlitsGeo0,
                          "the settled overlay frame blits more pads than the plain map frame: 0x6C59C blits "
                          "the cancel hotspot and 0x6C5DC the prev/next pair, all through the shell's env");
                    printf("  the settled overlay frame blits %d pads over the plain map's %d, after %d frames; "
                           "on the seam: prev %d/%d/%d, hotspot %d/%d/%d\n",
                           nBlitsOver, nBlitsGeo0, nFrame + 1,
                           (pPairA != NULL) ? SawPadBlit(pPairA->apPads[0]) : 0,
                           (pPairA != NULL) ? SawPadBlit(pPairA->apPads[1]) : 0,
                           (pPairA != NULL) ? SawPadBlit(pPairA->apPads[2]) : 0,
                           (pCtlA != NULL) ? SawPadBlit(pCtlA->apPads[0]) : 0,
                           (pCtlA != NULL) ? SawPadBlit(pCtlA->apPads[1]) : 0,
                           (pCtlA != NULL) ? SawPadBlit(pCtlA->apPads[2]) : 0);
                    Check(pPairA != NULL && pPairB != NULL
                              && (SawPadBlit(pPairA->apPads[0]) || SawPadBlit(pPairA->apPads[1])
                                  || SawPadBlit(pPairA->apPads[2]))
                              && (SawPadBlit(pPairB->apPads[0]) || SawPadBlit(pPairB->apPads[1])
                                  || SawPadBlit(pPairB->apPads[2])),
                          "and the pads on the seam are the pair's own: aOptionsListCtl[7] / [8] as Open "
                          "built them, not the strip pad the map draws");
                    DumpFrame("geoscape_options_base");
                }

                printf("  the press point %d,%d; ctlA state %d; scratch[0] %d; radar timer %d\n",
                       nGPressX, nGPressY, pCtlA != NULL ? (i32)pCtlA->nState : -1,
                       (i32)g_world.abOptionsScratch[0], g_world.nRadarPulseTimer);

                /* A2. The press is the map press the arm ran on its own; the release is the case-18
                 * release arm (0x8ACE4). gOptionsScratch[4] is 0, so no page arm runs; the confirm byte
                 * is the toggle, and it is 1, so LABEL_229 releases ctlA (0x8AED4), then re-reads the
                 * pair slots out of their cells (0x8AEEC) and jumps into the shared panel tail with the
                 * names panel (0x8AF1C -> 0x89B64). */
                (void)World_GeoscapePointerUp(&g_world);
                printf("  after that release: open %d, ctlA state %d, names bSelLineHighlight %d, panel rows %d\n",
                       g_world.bOptionsScreenOpen, pCtlA != NULL ? (i32)pCtlA->nState : -1,
                       pName != NULL ? (i32)pName->bSelLineHighlight : -1,
                       pName != NULL ? pName->nLineCount : -1);
                CheckEqI32(g_world.msgLines.nLineCount, nLines0, "and it posts no line either");

                /* A3. A press on a row of the names column. The dispatcher is in the case-18 arm,
                 * scratch[4] 0 skips the page walk (0x89110), and the confirm gate at 0x89334 / 0x89344
                 * lets the tail run: ctlA (0x8932C), the two pair slots re-read out of their cells
                 * (0x89370), then the names panel's press-and-hold tracking (0x893B4). The point is inside
                 * the panel's rect (nRectX 5, nRectY 70, nViewW 230) and outside ctlA's box
                 * (86..153, 179..197) and outside both pair boxes, so nothing is pressed. */
                World_GeoscapePointer(&g_world, 100, 90, true);
                CheckEqI32(pCtlA != NULL ? (i32)pCtlA->nState : -1, 0,
                           "the press misses ctlA's box, so its state stays 0 and Options_SaveAudioSettings "
                           "does not run (UiControl_HitTest measures the box only on a press, 0x1A42C)");
                Check(g_world.options.apPairCtl[0] != NULL && g_world.options.apPairCtl[1] != NULL,
                      "0x89370's re-read still finds the pair: nothing closed in between");
                CheckEqI32(pName != NULL ? (i32)pName->bHoverActive : -1, 1,
                           "the panel's hold tracking marks the hovered row (0x3ED44 sets the byte inside the rect)");
                CheckEqI32(pName != NULL ? pName->nUnk3032 : -1, 0,
                           "but no row is selected yet: the click needs the hold count to reach nRepeatDelay");
                CheckEqI32(g_world.bOptionsScreenOpen, 1, "the overlay is still up");

                (void)World_GeoscapePointerUp(&g_world);
                /* The release arm: 0x8ACE4 skips the page walk, LABEL_229 sees the confirm byte 1, 0x8AED4
                 * releases ctlA (state 0, so its force click finds nothing), 0x8AEEC releases the pair, and
                 * 0x8AF1C jumps into the shared panel tail 0x89B64 with the names panel: UiScrollList_ClickRow
                 * selects the row under the stored cursor -- the byte 0x83494 cleared is back up, raised by
                 * the release before this one -- and tail-calls OptionsScreen_OnConfirm @ 0x6cf18. */
                CheckEqI32(pName != NULL ? pName->nUnk3032 : -1, 1,
                           "the clicked row: (90 - nPosY 70) / nFont 16 = 1, held in the panel's row cursor");
                CheckEqI32(g_world.nMsgShipIdx, pName != NULL ? pName->anLineAux[1] : -1,
                           "0x6CF4C takes the craft index out of the names row's aux dword");
                CheckEqI32(g_world.nMsgShipIdx, 1, "and that row is craft 1");
                CheckEqI32(g_world.nSelectedShipIdx, 1, "0x6CF50 writes the same index into 0x187054");
                CheckEqI32(g_world.bMapSelectionFlag, 1,
                           "the readiness gate 0x6CF6C passes for this row (its stores aux is 0, the "
                           "range-and-fuel-full case at 0x6D77C) and 0x6CF78 arms the selection");
                CheckEqI32(g_world.msgLines.nLineCount, nLines0 + 1,
                           "0x6CFA4 appends the fly-to-point prompt");
                CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, nLines0), 27, "as flag 27");
                CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, nLines0, 0), 1,
                           "with the craft index in colour slot 0 (0x6CF8C re-reads it LDRB)");
                CheckEqI32(g_world.geo.time.bPaused, 1, "and GameTime_Pause @ 0x6CFE4");
                CheckEqI32(g_world.bOptionsScreenOpen, 1, "the confirm leaves the overlay up");

                /* B. The cancel hotspot. Pressing ctlA inside its own box puts it in state 1; the release
                 * force-clicks it (0x1A540 never consults the box) and Options_SaveAudioSettings @ 0x6b7ac
                 * runs: 0x6B7C4 calls Geoscape_CloseUI, whose case 18 finds gOptionsScratch[4] 0 and the
                 * toggle 1, so it answers at 0x8BD14 with screen 0 and 0x8BD18 OptionsScreen_Open(-1); that
                 * call takes the close half -- 0x6DC38 screen 0, 0x6DC40 toggle 0, the four panels freed at
                 * 0x6DC58 / 0x6DC60, the pair at 0x6DCDC / 0x6DCE4. The callee's tail then clears the
                 * selection byte at 0x6B8CC; the 0x6B8C4 unpause is skipped because the confirm had just
                 * armed the selection. */
                {
                    i32 nCtlAX = (pCtlA != NULL) ? (i32)pCtlA->x + 2 : 0;
                    i32 nCtlAY = (pCtlA != NULL) ? (i32)pCtlA->y + 2 : 0;

                    World_GeoscapePointer(&g_world, nCtlAX, nCtlAY, true);
                    CheckEqI32(pCtlA != NULL ? (i32)pCtlA->nState : -1, 1,
                               "the press lands inside the box 0x6D25C / 0x6D26C wrote");
                    (void)World_GeoscapePointerUp(&g_world);
                    CheckEqI32(g_world.bOptionsScreenOpen, 0, "0x6DC40 clears the toggle");
                    CheckEqI32(g_world.screen.nGeoscapeScreen, 0,
                               "and screen 0, written twice: 0x8BD14 then 0x6DC38");
                    Check(g_world.options.apPanel[0] == NULL && g_world.options.apPanel[1] == NULL
                              && g_world.options.apPanel[2] == NULL && g_world.options.apPanel[3] == NULL,
                          "all four columns are released (0x6DC58 frees the lines, 0x6DC60 the panel)");
                    Check(g_world.options.apPairCtl[0] == NULL && g_world.options.apPairCtl[1] == NULL,
                          "and the prev/next pair with them (0x6DCDC / 0x6DCE4)");
                    CheckEqI32(g_world.bMapSelectionFlag, 0, "the callee's own tail at 0x6B8CC");
                    CheckEqI32(g_world.geo.time.bPaused, 1,
                               "the clock stays paused: 0x6B8C4 unpauses only when the selection byte was 0, "
                               "and OptionsScreen_OnConfirm had just armed it at 0x6CF78");
                    Check(pCtlA != NULL && pCtlA->pfnClick != NULL && pCtlA->y == 179,
                          "the hotspot keeps the handler and the place Open gave it -- the close half frees "
                          "the panels and the pair only, never the two geoscape hotspots");
                }

                /* The confirm's flag-27 prompt stays on the message list in the original too: nothing on
                 * this path removes it, only the next press on the map answers it (0x84554 / 0x83784).
                 * The two checks below want a clean list -- the world press reads line 0 for its own flag
                 * gates -- so the fixture drops it here, exactly as this block restores the pause at the end. */
                (void)UiLineList_Remove(&g_world.msgLines, nLines0);
                CheckEqI32(g_world.msgLines.nLineCount, nLines0,
                           "the prompt the confirm appended, dropped");

                /* C. The same press again with the tap cooldown under 6, which is how the original
                 * actually behaves on your own base: Geoscape_OnPointer calls Smap_HitTestStrategSelect
                 * twice for one press -- 0x87C2C to decide the drag flag, then 0x87C94 again inside the
                 * cooldown gate (0x87C48 tests nTapCooldown < 6, and the press point is above the zoom
                 * strip) -- so the type-0 arm runs twice. The first pass opens the craft list; the second
                 * finds the toggle already set and takes OptionsScreen_Open's close half, so the list the
                 * player asked for is gone before the pointer is released, and the gate parks the cooldown
                 * at 255 (0x87CB4) and hands back 255. Recorded as the original's own behaviour, not a
                 * port defect: the port reproduces the double call at 0x87C94 (Part 91's note). */
                {
                    UiControl *pCtl = g_world.ui.apCtl[GEOUI_CTL_186BB4];

                    g_world.frame.g.nTapCooldown = 0;
                    World_GeoscapePointer(&g_world, nGPressX, nGPressY, true);
                    CheckEqI32(g_world.bOptionsScreenOpen, 0,
                               "open then closed inside the one press (0x6D0C8, then 0x6D0C0 -> the close half)");
                    CheckEqI32(g_world.screen.nGeoscapeScreen, 0, "screen 18 written, then 0 at 0x6DC38");
                    Check(g_world.options.apPanel[0] == NULL && g_world.options.apPanel[2] == NULL,
                          "the columns the first pass allocated are the ones the second pass freed");
                    Check(g_world.options.apPairCtl[0] == NULL && g_world.options.apPairCtl[1] == NULL,
                          "and the pair with them");
                    CheckEqI32(g_world.frame.g.nTapCooldown, 255, "the gate's own store (0x87CB4)");
                    Check(pCtl != NULL && pCtl->pfnClick != NULL && pCtl->y == 179,
                          "the cancel hotspot is the same object, handler and place, through both passes");
                    CheckEqI32(g_world.geo.time.bPaused, 1, "the arm's GameTime_Pause @ 0x83458 stands");
                    printf("  the double call: open %d, screen %d, cooldown %d, columns %d / %d\n",
                           g_world.bOptionsScreenOpen, g_world.screen.nGeoscapeScreen,
                           g_world.frame.g.nTapCooldown,
                           g_world.options.apPanel[0] != NULL, g_world.options.apPairCtl[0] != NULL);
                    (void)World_GeoscapePointerUp(&g_world);
                    CheckEqI32(g_world.msgLines.nLineCount, nLines0,
                               "no line posted by either pass: the type-0 inactive arm writes none");
                }
            }
            /* 0x6CF4C / 0x6CF50 wrote the confirm's craft index into 0x159E74 and 0x187054, and the sections
             * below pick the craft they intercept out of those two cells; the original carries both forward
             * too, so this is the fixture handing its own experiment back, not a parity clear. */
            g_world.nMsgShipIdx      = nMsgShip0;
            g_world.nSelectedShipIdx = nSelShip0;
            g_world.geo.time.bPaused = (u8)nPause0;
        }

        /* E. The geoscape's own options hotspot: root control [8], the handler word at 0x186518 that
         * Geoscape_InitUI installs at 0x86590 (World_FillCtorEnv hands World_OptionsMenuClick to that
         * slot). Ui_OpenScreen18 @ 0x84e9c plays 17, writes screen 18, and tail-calls
         * OptionsScreen_RebuildWithSound @ 0x6cf00, which tail-calls OptionsScreen_Init @ 0x6be74: 0x6BE90
         * sets gOptionsScratch[4], 0x6BEA0..0x6BEF0 builds the seven list rows, 0x6BF1C / 0x6BF2C put
         * Options_SaveAudioSettings and OptionsScreen_ReturnToTitle on the two hotspots, 0x6BF34..0x6C018
         * build the twelve volume buttons, 0x6C02C..0x6C0B0 give them their handlers, and 0x6C0B4 / 0x6C0B8
         * run the volume highlight and OptionsScreen_LayoutButtons.
         *
         * The word is called directly here: in the original the press that reaches it is Input_OnPointer's
         * index walk @ 0x13c6c (ported in pufo_input.c, gated in test_input.c), and Geoscape_OnPointer's own
         * press path hit-tests the root controls only when the drag flag is clear (0x87D84). */
        {
            UiControl *pCtlA = g_world.ui.apCtl[GEOUI_CTL_186BB4];
            UiControl *pCtlB = g_world.ui.apCtl[GEOUI_CTL_186BB0];
            i32 nSampleMark = g_nSamples;
            i32 nMusicSel = -1;
            i32 nSelCount = 0;
            i32 nCtlAX0 = (pCtlA != NULL) ? (i32)pCtlA->x : 0;
            i32 nCtlAY0 = (pCtlA != NULL) ? (i32)pCtlA->y : 0;
            i32 nCtlBX0 = (pCtlB != NULL) ? (i32)pCtlB->x : 0;
            i32 nCtlBY0 = (pCtlB != NULL) ? (i32)pCtlB->y : 0;
            i32 nMusicVol0 = g_options.nMusicVolume;
            i32 nMusicOff0 = g_options.bMusicOff;
            i32 i;

            g_world.ui.apRootCtl[8]->pfnClick(); /* 0x186518, installed at 0x86590 */

            Check(g_nSamples == nSampleMark + 2 && g_aFrameSamples[nSampleMark] == 17
                      && g_aFrameSamples[nSampleMark + 1] == 7,
                  "Ui_OpenScreen18 opens with Sound_Play(17) at 0x84EA4, and the callee it tail-calls, "
                  "OptionsScreen_RebuildWithSound @ 0x6cf00, plays its own 7 at 0x6CF08 before Init");
            CheckEqI32(g_world.screen.nGeoscapeScreen, 18, "and writes the id 18 (0x84ECC)");
            CheckEqI32((i32)g_world.abOptionsScratch[4], 1,
                       "OptionsScreen_Init's own byte: the controls exist (0x6BE90)");
            CheckEqI32((i32)g_world.abOptionsScratch[0], 0,
                       "and LayoutButtons cleared the tab byte to the audio page (0x6B6D0)");

            for (i = 0; i < 4; ++i) {
                UiControl *pRow = g_world.options.ctl.apList[i];

                Check(pRow != NULL && pRow->pfnClick != NULL,
                      "the four menu rows carry the handlers LayoutButtons installs");
                CheckEqI32(pRow != NULL ? pRow->y : -1, 90 + 25 * i,
                           "dropped on the rows 0x6B704..0x6B744 lays out (90 + 25 * i)");
                CheckEqI32(pRow != NULL ? (i32)pRow->x : -1,
                           pRow != NULL ? 120 - ((i32)pRow->uWidth >> 1) : -1,
                           "and centred on x 120 (0x6B718)");
            }
            Check(g_world.options.ctl.apList[0]->pfnClick == g_world.options.tab.pfnBindSaveSlots
                      && g_world.options.ctl.apList[1]->pfnClick == g_world.options.tab.pfnBindLoadSlots
                      && g_world.options.ctl.apList[2]->pfnClick == g_world.options.tab.pfnSetTab3
                      && g_world.options.ctl.apList[3]->pfnClick == g_world.options.tab.pfnSetTab4,
                  "0x6B6DC / 0x6B6E8 / 0x6B6F4 / 0x6B700 in order: save, load, quit, volume");
            Check(g_world.options.ctl.apList[4] != NULL && g_world.options.ctl.apList[4]->y == 229
                      && g_world.options.ctl.apList[4]->x == 1,
                  "rows 4..6 are built by 0x6BEA0..0x6BEF0 with the one shared descriptor "
                  "g_aOptionsListDesc {1, 229, \"firebtn\"} and LayoutButtons does not touch them: they stay "
                  "where the descriptor put them until a save or load row press centres them (0x6B514 / 0x6B538)");
            CheckEqI32(pCtlA != NULL ? pCtlA->y : -1, 198, "0x6B784 seats the cancel hotspot at y 198");
            CheckEqI32(pCtlA != NULL ? (i32)pCtlA->x : -1,
                       pCtlA != NULL ? 120 - ((i32)pCtlA->uWidth >> 1) : -1, "centred (0x6B75C)");
            Check(pCtlB != NULL && pCtlB->pfnClick == g_world.options.init.pfnReturnToTitle
                      && pCtlB->pfnClick != NULL,
                  "Init's store at 0x6BF48 seats the shell's OptionsScreen_ReturnToTitle thunk on ctlB "
                  "(Part 102: World_OptionsReturnToTitleThunk, wired in World_WireOptions)");
            CheckEqI32(pCtlB != NULL ? (i32)pCtlB->x : -1,
                       (pCtlA != NULL) ? 120 - ((i32)pCtlA->uWidth >> 1) : -1,
                       "ORIGINAL BUG (0x6B764): its x is centred on the CANCEL control's width, read again "
                       "from the same register, while the store lands on the other control");

            for (i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
                UiControl *pMusic = g_world.options.ctl.apMusic[i];
                UiControl *pSfx   = g_world.options.ctl.apSfx[i];

                Check(pMusic != NULL && pMusic->pfnClick != NULL && pSfx != NULL && pSfx->pfnClick != NULL,
                      "the twelve radio buttons carry their handlers (0x6C02C..0x6C0B0)");
                CheckEqI32(pMusic != NULL ? pMusic->y : -1, 107, "the music row at y 107 (0x6BFBC)");
                CheckEqI32(pSfx != NULL ? pSfx->y : -1, 147, "the sound row at y 147 (0x6BFF8)");
                CheckEqI32(pMusic != NULL ? (i32)pMusic->x : -1, 46 + 25 * i, "x = 46 + 25 * i (0x6BFDC)");
                if (pMusic != NULL && pMusic->nState == PUFO_CONTROL_MAX_STATE) { ++nSelCount; nMusicSel = i; }
            }
            CheckEqI32(nSelCount, 1,
                       "RefreshVolumeHighlight lights exactly one music button (0x6C0B4 -> 0x6B944)");
            printf("  the menu: rows at y 90 115 140 165, cancel at %d,198; music selected step %d for the "
                   "stored byte %d off %d; tab %d, controls-exist %d\n",
                   pCtlA != NULL ? (i32)pCtlA->x : -1, nMusicSel, g_options.nMusicVolume,
                   g_options.bMusicOff, (i32)g_world.abOptionsScratch[0],
                   (i32)g_world.abOptionsScratch[4]);

            /* E2. The volume row. Screen 18 with the controls up: 0x89110 lets the page arm run, the tab
             * byte is 0 so 0x892D0 walks rows 0..3, and the confirm gate at 0x89334 / 0x89344 stops the
             * tail because bOptionsScreenOpen -- the byte 0x89338 reads -- is the OVERLAY toggle, and the
             * menu is up without the overlay. The release arm's page-0 walk (0x8AE64) fires what the press
             * armed, and the row's handler is World_OptionsVolumeTab. */
            PressCtl(g_world.options.ctl.apList[3]);
            CheckEqI32((i32)g_world.abOptionsScratch[0], 4,
                       "the row's handler runs BaseUi_SetScratch4 (0x6B6B8): the tab byte is 4");
            PressCtl(g_world.options.ctl.apList[0]);
            CheckEqI32((i32)g_world.abOptionsScratch[0], 4,
                       "a press on a list row does nothing on the volume page: 0x89160 hit-tests slots 9..20 "
                       "and 0x8AD1C releases the same twelve, so no row handler is armed");

            /* E3. A music step. With the tab at 4 the press arm walks slots 9..20 (0x89160) and the release
             * arm walks the same twelve and then the cancel (0x8AD1C). */
            PressCtl(g_world.options.ctl.apMusic[3]);
            CheckEqI32(g_options.nMusicVolume, 3, "0x6BAD0 stores the step in the Map option byte");
            CheckEqI32(g_options.bMusicOff, 0, "and 0x6BAA8 clears the mute byte for any step above 0");
            CheckEqI32(g_world.options.ctl.apMusic[3]->nState, PUFO_CONTROL_MAX_STATE,
                       "the pressed button is the lit one (0x6BA50)");
            CheckEqI32(g_world.options.ctl.apMusic[2]->nState, 0, "and the other ten were cleared first (0x6BA2C)");
            CheckEqI32((i32)g_world.abOptionsScratch[0], 4, "the tab stays on the volume page");

            ClearSurface();
            (void)DrawFrame();
            printf("  the volume page: music %d off %d, sound %d off %d; blits %d\n",
                   g_options.nMusicVolume, g_options.bMusicOff, g_options.nSoundVolume,
                   g_options.bSoundOff, g_nBlits);
            DumpFrame("geoscape_options_volume");

            /* E4. The cancel. Options_SaveAudioSettings @ 0x6b7ac with the tab at 4: 0x6B7C4's close is
             * gated on the overlay toggle and the overlay is not up, so the menu survives; 0x6B7D4 sees the
             * controls exist, 0x6B808 takes the tab-4 arm and writes the four registry values, 0x6B8A8 clears
             * the tab, and 0x6B8AC runs LayoutButtons again -- which is what puts the four rows back. */
            PressCtl(pCtlA);
            CheckEqI32((i32)g_world.abOptionsScratch[0], 0, "0x6B8A8");
            CheckEqI32(g_world.options.ctl.apList[3]->y, 165,
                       "0x6B8AC's LayoutButtons re-lays the rows (0x6B704..0x6B744)");
            CheckEqI32(pCtlA != NULL ? pCtlA->y : -1, 198, "and re-seats the cancel (0x6B784)");
            CheckEqI32(g_world.bOptionsScreenOpen, 0, "the overlay never opened: 0x6B7C4 is gated on it");
            CheckEqI32((i32)g_world.abOptionsScratch[4], 1, "the menu is still up");

            /* E5. The quit row. Its handler is Options_SetTab3_AndUiY @ 0x6b674, which the port splits into
             * BaseUi_SetScratch3AndUiY: the tab byte and the two STRH writes 0x6B68C / 0x6B69C that put the
             * two hotspots on the quit page's rows. */
            PressCtl(g_world.options.ctl.apList[2]);
            CheckEqI32((i32)g_world.abOptionsScratch[0], 3, "0x6B680");
            CheckEqI32(pCtlB != NULL ? pCtlB->y : -1, 120, "0x6B68C's STRH on *pGeoscapeCtlB");
            CheckEqI32(pCtlA != NULL ? pCtlA->y : -1, 142, "0x6B69C's STRH on *pGeoscapeCtlA");

            /* E6. The close, through Geoscape_CloseUI @ 0x8b94c case 18 -- 0x8BCD0 reads the tab byte
             * SIGNED, it is 3, so 0x8BCFC clears it and 0x8BD08 runs Options_SaveAudioSettings. That callee
             * now finds the tab 0, so its own 0x6B7F0 arm closes again, and the second pass takes 0x8BCE4 /
             * 0x8BCE8: OptionsScreen_RebuildWithSound, which reaches Init's teardown half (0x6BE84 -> 0x6C0FC)
             * and screen 0. The callee reaches no 0x6B8AC on that path -- the tab-0 arm returns into its own
             * 0x6B8C4 unpause -- so the menu's controls keep whatever seats the quit page gave them. */
            Geoscape_CloseUI(&g_world.closeEnv);
            CheckEqI32((i32)g_world.abOptionsScratch[4], 0, "Init's teardown half clears the controls byte (0x6C0FC)");
            CheckEqI32(g_world.screen.nGeoscapeScreen, 0, "0x8BCE8");
            CheckEqI32((i32)g_world.abOptionsScratch[0], 0, "0x8BCFC, then 0x6B6D0");
            CheckEqI32(pCtlA != NULL ? pCtlA->y : -1, 142,
                       "and nothing is re-laid: 0x8BCFC already cleared the tab, so SaveAudioSettings takes "
                       "its tab-0 arm at 0x6B7F0 and never reaches 0x6B8AC -- the cancel keeps the quit page's "
                       "seat 0x6B69C gave it");
            printf("  the menu closed: controls-exist %d, tab %d, screen %d\n",
                   (i32)g_world.abOptionsScratch[4], (i32)g_world.abOptionsScratch[0],
                   g_world.screen.nGeoscapeScreen);

            /* The menu leaves its marks on shared state the sections below draw and save: the two hotspots'
             * seats (0x6B68C / 0x6B69C moved them to the quit page's rows and the close reached no 0x6B8AC to
             * put them back) and the two Map volume bytes the volume page wrote. The original would carry all
             * four forward; this block is an experiment inside a longer section, so the fixture puts them
             * back for the sections that follow. */
            if (pCtlA != NULL) { pCtlA->x = (i16)nCtlAX0; pCtlA->y = (i16)nCtlAY0; }
            if (pCtlB != NULL) { pCtlB->x = (i16)nCtlBX0; pCtlB->y = (i16)nCtlBY0; }
            g_options.nMusicVolume = nMusicVol0;
            g_options.bMusicOff    = (u8)nMusicOff0;
        }

        /* 1. The press on the craft, and 2. the line its dispatch posts. */
        ClearSurface();
        (void)DrawFrame();
        nBlitsPlain = g_nBlits;
        World_GeoscapePointer(&g_world, nPressX, nPressY, true);
        (void)World_GeoscapePointerUp(&g_world);

        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 11,
                   "the type-1 inactive arm appends flag 11 (0x836D0), and it is the only branch that does not "
                   "drop the line it found");
        CheckEqI32(g_world.msgLines.nLineCount, 1, "one line on the list");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), 0,
                   "colour slot 0 holds the ship index (0x836A0), the value every later reader re-reads");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 1), 0, "and slots 1 and 2 stay 0");
        CheckEqI32(g_world.nSelectedShipIdx, 0, "0x187054");
        CheckEqI32(g_world.nMsgShipIdx, 0, "0x159E74");
        CheckEqI32(g_world.geo.time.bPaused, 1, "GameTime_Pause @ 0x836B4");
        CheckEqI32(g_world.nRadarPulseTimer, 250, "and the radar pulse timer (0x836D8)");
        Check(g_world.bFlag18708C == 0 && g_world.bFlag187090 == 0 && g_world.bFlag187098 == 0
                  && g_world.bFlag1870A0 == 0,
              "none of the four flag bytes is touched: the type-1 inactive arm is the one that writes no flag");
        {
            const char *pszLine = (g_world.ui.pStatusPanel->apLines[0] != NULL)
                                      ? (const char *)g_world.ui.pStatusPanel->apLines[0] : NULL;

            (void)Ship_FormatDisplayName(g_world.apCraft, 0, g_assets.pTextRecs, szName, sizeof(szName));
            Check(pszLine != NULL && strcmp(pszLine, szName) == 0,
                  "the panel's row 0 is Ship_FormatDisplayName of that craft (0x83164)");
            CheckEqI32((i32)g_world.ui.pStatusPanel->anLineAux[0], nMarker,
                       "with the item's list index beside it (0x83230)");
            CheckEqI32((i32)g_world.ui.pStatusPanel->nLineCount, 1, "one panel row");
        }

        /* 3. The card the draw paints for flag 11, and the control that arms the target. */
        ClearSurface();
        (void)DrawFrame();
        nBlitsCard = g_nBlits;
        printf("  the card frame blits %d pads; the four controls sit at y %d %d %d %d\n", (int)nBlitsCard,
               (i32)g_world.ui.apCtl[GEOUI_CTL_186BA8]->y, (i32)pCtlTarget->y,
               (i32)g_world.ui.apCtl[GEOUI_CTL_186B98]->y, pCtlCancel->y);
        Check(pCtlTarget->y == PUFO_STATUS_DETAILCTL_Y_TARGET && pCtlCancel->y == PUFO_STATUS_DETAILCTL_Y_CANCEL,
              "the card's four controls at y 140, 160, 180, 200 (0x9005C..0x90088)");
        Check(pCtlTarget->pfnClick == g_world.frame.panel.pfnSelectMapTarget,
              "and 0x8FFE8 installed Smap_SelectMapTarget on the new-target control");
        Check(pCtlCancel->pfnClick == g_world.frame.panel.pfnResolveInterceptUI,
              "with the resolve handler on the cancel (0x8FFFC)");
        DumpFrame("geoscape_craft_card");

        PressCtl(pCtlTarget);
        CheckEqI32(g_world.bMapSelectionFlag, 1, "Smap_SelectMapTarget arms the selection (0x84498)");
        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 27,
                   "line 0 is now the flag-27 pick-a-cell prompt (0x84574 insert, 0x84580 removal)");
        CheckEqI32(g_world.msgLines.nLineCount, 1, "still one line");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), 0,
                   "and its colour slot 0 is the ship index the arm read off the card line (0x84554)");
        CheckEqI32(g_world.nSelectedShipIdx, 0, "re-stored from the same slot (0x845A4)");
        CheckEqI32(g_world.nMsgShipIdx, 0, "and into 0x159E74 (0x845A4)");
        CheckEqI32(g_world.geo.time.bPaused, 1, "the arm pauses unconditionally (0x84588)");
        CheckEqI32((i32)pCraft->bIntercepting, 0,
                   "the waypoint removal did not run: it is inside the bIntercepting test (0x844D0)");

        ClearSurface();
        (void)DrawFrame();
        nBlitsPick = g_nBlits;
        Check(pCtlCancel->x == PUFO_STATUS_LISTCTL_X && pCtlCancel->y == PUFO_STATUS_LISTCTL_Y,
              "the prompt's own control at x 140, y 274 (0x90874..0x90888)");
        CheckEqI32(g_world.nSelectedShipIdx, (i32)UiLineList_GetColor(&g_world.msgLines, 0, 0),
                   "the flag-27 arm reads the ship index back out of the line (0x90748)");
        DumpFrame("geoscape_pick_target");

        /* 5. A press on a cell with nothing on it: the select test's flag-27 tail. */
        Check(ScreenForCell(&g_world.view, nCellX + 40, nCellY, &nEmptyX, &nEmptyY),
              "a cell 40 columns off the base has a screen pixel too");
        printf("  the empty press lands at screen %d,%d -> cell %d,%d\n", nEmptyX, nEmptyY,
               nCellX + 40, nCellY);
        World_GeoscapePointer(&g_world, nEmptyX, nEmptyY, true);
        (void)World_GeoscapePointerUp(&g_world);

        CheckEqI32(g_world.bMapSelectionFlag, 0, "the tail clears the selection it was answering (0x8372C)");
        CheckEqI32(g_world.view.nPickX, nCellX + 40, "0x186414 holds the clicked cell (0x83784)");
        CheckEqI32(g_world.view.nPickY, nCellY, "0x186418 (0x83788)");
        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 12,
                   "and line 0 becomes the flag-12 fly-to-point order (0x837B4)");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), 0,
                   "with the selected ship index in colour slot 0");
        CheckEqI32(g_world.msgLines.nLineCount, 1, "one line, the prompt replaced rather than added to");
        CheckEqI32((i32)g_world.ui.pStatusPanel->nUnk3032, 0, "and the panel's row cursor cleared (0x837BC)");
        CheckEqI32(g_world.geo.time.bPaused, 1, "the tail pauses (0x83720)");

        /* 6. The fly-to-point card and its confirm. */
        ClearSurface();
        (void)DrawFrame();
        nBlitsFly = g_nBlits;
        printf("  blits: card %d, prompt %d, fly-to-point %d, plain %d\n", (int)nBlitsCard, (int)nBlitsPick,
               (int)nBlitsFly, (int)nBlitsPlain);
        Check(pCtlOk->y == PUFO_STATUS_ORDERCTL_Y_TOP && pCtlCancel->y == PUFO_STATUS_ORDERCTL_Y_BOTTOM,
              "the two controls of the flag-12 arm at y 100 and y 124 (0x8F678, 0x8F68C)");
        Check(pCtlOk->pfnClick == g_world.frame.panel.pfnConfirmCraftTarget,
              "0x8F688 installed Smap_ConfirmCraftTarget on the upper one");
        Check(pCtlOk->x == 120 - ((i32)pCtlOk->uWidth / 2) && pCtlCancel->x == 120 - ((i32)pCtlCancel->uWidth / 2),
              "both centred on x 120 (0x8F650..0x8F674)");
        DumpFrame("geoscape_fly_to_point");

        nCountBefore = PtrArray_GetCount(&g_world.strategList);
        PressCtl(pCtlOk);
        nCountAfter = PtrArray_GetCount(&g_world.strategList);
        nWaypoint = nCountBefore;
        {
            const StrategItem *pWay = (nCountAfter > nCountBefore)
                                          ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nWaypoint)
                                          : NULL;

            Check(nWaypoint < nCountAfter && pWay != NULL, "the confirm added one item (0x843AC)");
            Check(pWay != NULL && pWay->nType == 5, "as a type-5 waypoint");
            Check(pWay != NULL && pWay->bActive == 1 && pWay->bVisible == 1, "active and visible");
            Check(pWay != NULL && pWay->nX == nCellX + 40 && pWay->nY == nCellY,
                  "at the published cell -- and at mode 0 the type-5 scaling (0x8275C) leaves it unchanged");
        }
        Check(pCraft->bWeaponsHot == 1 && pCraft->bAtBase == 0 && pCraft->bIntercepting == 1,
              "Ship_LaunchIntercept @ 0x843D8 has the craft hot, off base and chasing");
        CheckEqI32(pCraft->nStrategIdx, nWaypoint, "and its target is the new waypoint (Count - 1, 0x843D8)");
        CheckEqI32(g_world.msgLines.nLineCount, 0, "the confirm drops line 0 (0x843E4)");
        CheckEqI32(g_world.geo.time.bPaused, 0, "and unpauses, the list being empty (0x843F0)");
        CheckEqI32(g_world.bMapSelectionFlag, 0, "with the selection cleared (0x84400)");

        /* The order the pen wrote is the order the game flies: one event pass and the entry chases. */
        {
            const StrategItem *pWay = (const StrategItem *)PtrArray_Get(&g_world.strategList, nWaypoint);
            i32 nOwn = FindStrategById(&g_world.strategList, (i32)pCraft->bId);
            const StrategItem *pOwn = (nOwn >= 0) ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nOwn)
                                                  : NULL;
            i32 nDx0 = (pOwn != NULL && pWay != NULL) ? abs(pOwn->nX - pWay->nX) : -1;

            World_PulseGeoscape(&g_world);
            printf("  the waypoint is at %d,%d; the entry at %d,%d; distance %d\n",
                   pWay != NULL ? pWay->nX : -1, pWay != NULL ? pWay->nY : -1,
                   pOwn != NULL ? pOwn->nX : -1, pOwn != NULL ? pOwn->nY : -1, nDx0);
            Check(pOwn != NULL && pOwn->bVisible == 1,
                  "Geoscape_PulseUpdate keeps the entry of a craft that is not at base visible (0x955C0)");
            Check(pWay != NULL && pWay->nType == 5 && pWay->nX == nCellX + 40,
                  "the waypoint itself does not move");
        }
    }


    /* Section 15: the two pointer paths an intercept order needs on the map side, both walked with the pen.
     *
     * A. The contact order, i.e. which writer pairs a flag-20 line with an armed selection.
     *   1. The card's "new target" control arms the selection: Smap_SelectMapTarget @ 0x84484 puts
     *      bMapSelectionFlag up (0x84498), re-reads the ship index out of the card line (0x844A4), and --
     *      because Ship+53 (bIntercepting) is set and the chased entry is a type 5 (0x844D0..0x844F0) --
     *      removes that waypoint (StrategList_RemoveAt @ 0x84504) and clears the byte (0x84514). Line 0
     *      becomes the flag-27 prompt (0x84574, 0x84580).
     *   2. A press on the cell of an ACTIVE visible item reaches the select test again (flag 27 has no press
     *      arm; 0x87D28 only consults it after the drag block) and this time the scan builds one line for it
     *      (the type-4 active gate 0x831A0..0x831E8). The dispatch's type-4 active arm (0x834D0) clears the
     *      panel row cursor (0x834D8), stores the item index at 0x186BE4 (0x834E4), pauses (0x834EC), sets
     *      byte_18708C (0x834F8), drops the prompt (0x83504) and appends flag 20 with the cursor in colour
     *      slot 0 and the item index in slot 1 (0x83524) -- and it is the one branch of the six that never
     *      writes bMapSelectionFlag, so the selection the card armed is still up.
     *   3. Geoscape_Draw's flag-0x14 arm branches on exactly that pair (0x909DC). With the selection up it
     *      paints "Миссия: <panel row>" and installs Smap_CancelTarget @ 0x84254 on *pGeoscapeCtlB at y 102
     *      (0x90C64, 0x90C74) with the ОК label at y 103 (0x90CD4), and *pGeoscapeCtlA at y 125 (0x90CF0) with
     *      Smap_ResolveInterceptUI (0x90D00) and "Отмена" at y 126 (0x90D5C). The press arm for flag 20
     *      (0x876C4) hit-tests *pGeoscapeCtlA then *pGeoscapeCtlB while the selection is up (0x876D0..0x87704).
     *   4. Smap_CancelTarget is the launch despite its name: it reads colour SLOT 1 (0x84268) as the strateg
     *      index and calls Ship_LaunchIntercept @ 0x8427C with 0x187054 as the ship, then clears the
     *      selection (0x84288), byte_187090[8] (0x84294), the line (0x842A0), byte_18708C (0x842BC) and
     *      byte_187090 (0x842C8). So the flag-20 panel's own ОК is the order, and the slot that carries the
     *      target is slot 1 -- the slot 0 the panel paints with (0x90A30) is its ROW, not the target.
     *
     * B. The multi-select box, i.e. what the >1 branch (0x832B0's else side) leaves and how it is answered.
     *   0x832C0..0x83378 turns the status panel into a centred box (nPosX 120, nRectX 20, nViewW 200,
     *   nRectY = nPosY = 120 - (count / 2) * nFont), pauses the clock (0x83360), raises byte_18709C[4]
     *   (0x8336C), plays sample 7 (0x83378) -- and DROPS the panel's row-selection byte 0x1BA1
     *   (bSelLineHighlight, 0x83358, STRB of the always-zero R9). The message list is left exactly as it was.
     *   The press arm for that byte (0x87ABC) hands the pointer to UiScrollList_TrackHold at loc_88298, which
     *   returns at once while 0x1BA1 is 0; the release arm (0x89B54) runs UiScrollList_ClickRow -- gated on
     *   0x1BA1 too, so it selects nothing -- and then stores 1 back into it (0x89B8C). So the release that
     *   follows the box-building press is what re-arms the panel, and the next press/release pair is the row
     *   click: UiScrollList_ClickRow sets [0xBD8] = [0xBD4] + row and tail-calls [0xBC4], the panel's pfnClick,
     *   which Geoscape_InitUI installed as Geoscape_OnRadarListClick @ 0x854c4 (0x86C1C).
     *   Inside it: the item index comes out of anLineAux at the selected row (0x85500), byte_18709C[4] is
     *   cleared (0x85504), the index is stored at 0x186BE4 (0x8550C) and the clock is unpaused (0x85510)
     *   before the six (type, active) tests. Its type-4 arm (0x85674) posts the same flag 20 as the press
     *   dispatch's, with one difference: colour slot 0 carries the panel ROW it just selected (0x856B4's a3 is
     *   pStatusPanel->nUnk3032), which is what the flag-0x14 arm reads as the row it paints. With no selection
     *   armed the arm takes its other branch (0x90B34..0x90B68): "Крушение <row> <size>" with a single control,
     *   *pGeoscapeCtlA at y 115 carrying Smap_ResolveInterceptUI. */
    {
        Ship        *pCraft;
        UiControl   *pCtlTarget, *pCtlA, *pCtlB;
        StrategItem *pContact;
        i32  nEntry, nWaypoint, nCellX = 0, nCellY = 0;
        i32  nTgtX = 0, nTgtY = 0, nPressX = 0, nPressY = 0, nTgtPressX = 0, nTgtPressY = 0;
        i32  nCount0, nContact, nContact2, nLineCount, nRowPick, nRowY, nBoxTop;
        i32  nBlitsOrder, nBlitsBox, nBlitsClear;
        char szWant[64];

        printf("section 15: the contact order through the click, and the multi-select box\n");

        pCraft     = g_world.apCraft[0];
        pCtlTarget = g_world.ui.apCtl[GEOUI_CTL_186B9C];
        pCtlA      = g_world.ui.apCtl[GEOUI_CTL_186BB4];  /* *pGeoscapeCtlA */
        pCtlB      = g_world.ui.apCtl[GEOUI_CTL_186BB0];  /* *pGeoscapeCtlB */

        nWaypoint = pCraft->nStrategIdx;
        {
            const StrategItem *pWay = (const StrategItem *)PtrArray_Get(&g_world.strategList, nWaypoint);
            const StrategItem *pOwn = (const StrategItem *)PtrArray_Get(&g_world.strategList,
                                                                        FindStrategById(&g_world.strategList,
                                                                                        (i32)pCraft->bId));

            Check(pCraft->bIntercepting == 1 && pWay != NULL && pWay->nType == 5,
                  "the state section 14 left: the craft chases the type-5 waypoint its own click wrote");
            if (pWay != NULL) { nTgtX = pWay->nX; nTgtY = pWay->nY; }   /* copied out: adds and removes realloc */
            nEntry = FindStrategById(&g_world.strategList, (i32)pCraft->bId);
            if (pOwn != NULL) { nCellX = pOwn->nX; nCellY = pOwn->nY; }
            Check(nEntry >= 0 && pOwn != NULL && pOwn->nType == 1 && pOwn->bActive == 0 && pOwn->bVisible == 1,
                  "the craft's entry is still the inactive visible item (0x955C0 made it visible again)");
        }
        Check(ScreenForCell(&g_world.view, nCellX, nCellY, &nPressX, &nPressY),
              "the entry's cell has a screen pixel (the inverse of 0x82C84 / 0x82CAC)");
        Check(ScreenForCell(&g_world.view, nTgtX, nTgtY, &nTgtPressX, &nTgtPressY),
              "and so has the waypoint's cell, where the contact is put");

        /* 1. The card, then the control that arms the selection and drops the outstanding waypoint. */
        World_GeoscapePointer(&g_world, nPressX, nPressY, true);
        (void)World_GeoscapePointerUp(&g_world);
        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 11, "the press on the craft gives its card line");
        nCount0 = PtrArray_GetCount(&g_world.strategList);
        PressCtl(pCtlTarget);
        CheckEqI32(g_world.bMapSelectionFlag, 1, "Smap_SelectMapTarget arms the selection (0x84498)");
        Check(pCraft->bIntercepting == 0,
              "and the waypoint removal runs this time: Ship+53 is set and the chased entry is a type 5 "
              "(0x844D0, 0x844F0), so 0x84504 removes it and 0x84514 clears the byte");
        CheckEqI32(PtrArray_GetCount(&g_world.strategList), nCount0 - 1,
                   "one item fewer in the list -- the waypoint it was chasing (StrategList_RemoveAt @ 0x82ab0)");
        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 27, "line 0 is the flag-27 prompt again");

        /* 2. A contact stands on the waypoint's cell, and the press there answers the prompt with flag 20. */
        nCount0  = PtrArray_GetCount(&g_world.strategList);
        pContact = StrategList_AddItem(&g_world.strategList, &g_world.nStrategSeed, g_world.view.nMode,
                                       4, 1 /* active */, 2, nTgtX, nTgtY, -1, 1);
        nContact = nCount0;
        if (pContact != NULL) { pContact->nDisplayId = 7; }
        Check(pContact != NULL && pContact->nType == 4 && pContact->bActive == 1 && pContact->bVisible == 1,
              "a type-4 active visible contact at the waypoint's cell, the shape the type-4 gate reads (0x831A0)");

        World_GeoscapePointer(&g_world, nTgtPressX, nTgtPressY, true);
        (void)World_GeoscapePointerUp(&g_world);

        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 20,
                   "the type-4 active arm appends flag 20 (0x83524)");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), 0,
                   "colour slot 0 is the panel row cursor the arm cleared (0x834D8)");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 1), nContact,
                   "and slot 1 is the item index it stored at 0x186BE4 (0x834E4)");
        CheckEqI32(g_world.nSelectedStrategIdx, nContact, "0x186BE4");
        CheckEqI32(g_world.bMapSelectionFlag, 1,
                   "the selection is STILL up: the type-4 arm is the one of the six that never writes the byte");
        CheckEqI32(g_world.bFlag18708C, 1, "byte_18708C set (0x834F8)");
        CheckEqI32(g_world.geo.time.bPaused, 1, "and the clock paused (0x834EC)");
        CheckEqI32((i32)g_world.ui.pStatusPanel->nUnk3032, 0, "the panel's row cursor cleared (0x834D8)");
        CheckEqI32(g_world.nRadarPulseTimer, 250, "the type-4 arm writes no radar pulse timer, unlike 11/19/18");

        /* 3. The panel the pair paints, and its ОК. */
        ClearSurface();
        (void)DrawFrame();
        nBlitsOrder = g_nBlits;
        Check(pCtlB->y == PUFO_STATUS_TARGETCTL_Y_CANCEL && pCtlB->pfnClick == g_world.frame.panel.pfnCancelTarget,
              "the selecting arm puts Smap_CancelTarget on *pGeoscapeCtlB at y 102 (0x90C64, 0x90C74)");
        Check(pCtlA->y == PUFO_STATUS_TARGETCTL_Y_OK2 && pCtlA->pfnClick == g_world.frame.panel.pfnResolveInterceptUI,
              "and Smap_ResolveInterceptUI on *pGeoscapeCtlA at y 125 (0x90CF0, 0x90D00)");
        Check(pCtlA->x == 120 - ((i32)pCtlA->uWidth / 2) && pCtlB->x == 120 - ((i32)pCtlB->uWidth / 2),
              "both centred on x 120 (0x90C54, 0x90CE0)");
        DumpFrame("geoscape_order_contact");

        nCount0 = PtrArray_GetCount(&g_world.strategList);
        PressCtl(pCtlB);
        Check(pCraft->bWeaponsHot == 1 && pCraft->bAtBase == 0 && pCraft->bIntercepting == 1
                  && pCraft->nStrategIdx == nContact,
              "Smap_CancelTarget @ 0x8427C launches at the item colour SLOT 1 names (0x84268), not slot 0");
        CheckEqI32(g_world.bMapSelectionFlag, 0, "and clears the selection (0x84288)");
        CheckEqI32(g_world.bFlag18708C, 0, "with byte_18708C (0x842BC)");
        CheckEqI32(g_world.bFlag187090, 0, "and byte_187090 (0x842C8)");
        CheckEqI32(g_world.bFlag187098, 0, "and byte_187090[8] (0x84294)");
        CheckEqI32(g_world.msgLines.nLineCount, 0, "the panel line is gone (0x842A0)");
        CheckEqI32(g_world.geo.time.bPaused, 0, "and the clock runs again, the list being empty (0x842AC)");
        Check(pContact != NULL && pContact->nType == 4 && pContact->nX == nTgtX && pContact->nY == nTgtY
                  && PtrArray_GetCount(&g_world.strategList) == nCount0,
              "the contact itself is untouched and still in the list");

        /* 4. The box: two contacts in one cell. */
        {
            StrategItem *pSecond = StrategList_AddItem(&g_world.strategList, &g_world.nStrategSeed,
                                                       g_world.view.nMode, 4, 1, 2, nTgtX, nTgtY, -1, 1);
            nContact2 = nCount0;
            if (pSecond != NULL) { pSecond->nDisplayId = 8; }
            Check(pSecond != NULL, "a second active contact in the same cell, so the scan builds two lines");
        }

        World_GeoscapePointer(&g_world, nTgtPressX, nTgtPressY, true);

        nLineCount = (i32)g_world.ui.pStatusPanel->nLineCount;
        nBoxTop    = 120 - (nLineCount / 2) * (i32)g_world.ui.pStatusPanel->nFont;
        for (nRowPick = 0; nRowPick < nLineCount; ++nRowPick) {
            if (g_world.ui.pStatusPanel->anLineAux[nRowPick] == nContact2) { break; }
        }
        nRowY = nBoxTop + nRowPick * (i32)g_world.ui.pStatusPanel->nFont + 8;
        printf("  the box has %d rows at y %d..%d; the picked row %d is clicked at x 120, y %d\n",
               nLineCount, nBoxTop, nBoxTop + nLineCount * (i32)g_world.ui.pStatusPanel->nFont, nRowPick, nRowY);

        Check(nLineCount >= 2 && nRowPick < nLineCount,
              "the panel carries a row per selectable item in the cell, the picked one carrying the new index (0x83230)");
        Check(g_world.ui.pStatusPanel->nPosX == 120 && g_world.ui.pStatusPanel->nRectX == 20
                  && g_world.ui.pStatusPanel->nViewW == 200,
              "the >1 branch lays the panel out as a box: nPosX 120 (0x832C0), left 20 (0x83308), width 200 (0x83348)");
        Check(g_world.ui.pStatusPanel->nRectY == nBoxTop && g_world.ui.pStatusPanel->nPosY == nBoxTop,
              "with nRectY and nPosY both 120 - (count / 2) * nFont (0x832F4, 0x83334)");
        CheckEqI32(g_world.ui.pStatusPanel->bSelLineHighlight, 0,
                   "and it DROPS the panel's row-selection byte 0x1BA1 (0x83358, an STRB of the always-zero R9), "
                   "which is why neither of the two panel arms can select a row on this press");
        CheckEqI32(g_world.bFlag1870A0, 1, "byte_18709C[4] up (0x8336C), the gate the press arm 0x87ABC reads");
        CheckEqI32(g_world.geo.time.bPaused, 1, "the branch pauses (0x83360)");
        CheckEqI32(g_world.msgLines.nLineCount, 0,
                   "and the message list is left exactly as it was: no line is added, removed or changed");
        CheckEqI32(g_world.bMapSelectionFlag, 0, "the selection stays down, so the clicked row answers as a list click");

        ClearSurface();
        (void)DrawFrame();
        nBlitsBox = g_nBlits;
        DumpFrame("geoscape_multi_box");

        (void)World_GeoscapePointerUp(&g_world);
        CheckEqI32(g_world.msgLines.nLineCount, 0,
                   "the box-building release selects nothing either: UiScrollList_ClickRow is gated on 0x1BA1 too "
                   "(0x3EF44), so no row click runs");
        CheckEqI32(g_world.ui.pStatusPanel->bSelLineHighlight, 1,
                   "and it then stores 1 back into the byte (0x89B8C) -- the panel is clickable from here on");

        /* 5. The row click, now that the release re-armed the panel: the press hover-tracks and the release
         *    selects the row and tail-calls the panel's pfnClick, which InitUI installed as
         *    Geoscape_OnRadarListClick (0x86C1C). */
        World_GeoscapePointer(&g_world, 120, nRowY, true);
        CheckEqI32(g_world.msgLines.nLineCount, 0, "the press itself adds nothing (UiScrollList_TrackHold, loc_88298)");
        (void)World_GeoscapePointerUp(&g_world);

        CheckEqI32(g_world.ui.pStatusPanel->nUnk3032, g_world.ui.pStatusPanel->nUnk3028 + nRowPick,
                   "UiScrollList_ClickRow sets [0xBD8] = [0xBD4] + row (0x3EFD8..0x3EFE8)");
        CheckEqI32(g_world.bFlag1870A0, 0, "and Geoscape_OnRadarListClick clears byte_18709C[4] (0x85504)");
        CheckEqI32(g_world.nSelectedStrategIdx, nContact2,
                   "the item index read out of anLineAux at that row (0x85500) lands at 0x186BE4 (0x8550C)");
        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 20,
                   "its type-4 active test appends flag 20 (0x856B4), the same post as the press dispatch's");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), nRowPick,
                   "with colour slot 0 carrying the panel ROW it just selected (0x856B4's third argument is "
                   "pStatusPanel->nUnk3032), not 0 and not the item index");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 1), nContact2,
                   "and slot 1 the item index, which is what the ОК reads (0x84268)");
        CheckEqI32(g_world.bFlag18708C, 1, "byte_18708C up (0x85688)");
        CheckEqI32(g_world.geo.time.bPaused, 1, "and the clock paused again (0x8567C)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 0,
                   "no screen change: only the type-0 inactive test opens one (0x855B0)");

        /* The row the scan built: TEXTS[1147]'s own bytes then the display id, exactly SmapHit_PutLabel
         * (0x831A0) followed by SmapHit_AppendNumber (0x831E8) -- compared against the loaded blob, not a
         * source literal, because the record is cp1251. */
        (void)snprintf(szWant, sizeof(szWant), "%s%d",
                       g_assets.pTextRecs + (size_t)PUFO_TEXT_REC_CRAFT_LABEL * PUFO_TEXT_REC_STRIDE, 8);
        {
            const char *pszRow = (g_world.ui.pStatusPanel->apLines[nRowPick] != NULL)
                                     ? (const char *)g_world.ui.pStatusPanel->apLines[nRowPick] : NULL;

            Check(pszRow != NULL && strcmp(pszRow, szWant) == 0,
                  "the row colour slot 0 names is the panel's own row: TEXTS[1147] + the display id (0x831E8)");
        }
        printf("  the box's rows read \"%s\" and \"%s\"\n",
               (g_world.ui.pStatusPanel->apLines[0] != NULL) ? (const char *)g_world.ui.pStatusPanel->apLines[0] : "",
               (g_world.ui.pStatusPanel->apLines[1] != NULL) ? (const char *)g_world.ui.pStatusPanel->apLines[1] : "");

        /* 6. With no selection armed the same flag-20 line paints the OTHER arm: one control, the resolve. */
        ClearSurface();
        (void)DrawFrame();
        nBlitsClear = g_nBlits;
        Check(pCtlA->y == PUFO_STATUS_TARGETCTL_Y_OK && pCtlA->pfnClick == g_world.frame.panel.pfnResolveInterceptUI,
              "the clear arm puts Smap_ResolveInterceptUI on *pGeoscapeCtlA at y 115 (0x90B4C, 0x90B60)");
        DumpFrame("geoscape_target_panel");

        PressCtl(pCtlA);
        CheckEqI32(g_world.bFlag18708C, 0, "Smap_ResolveInterceptUI clears byte_18708C (0x84A2C)");
        CheckEqI32(g_world.bFlag187090, 0, "byte_187090 (0x84A24)");
        CheckEqI32(g_world.bFlag187098, 0, "byte_187090[8] (0x84A3C)");
        CheckEqI32(g_world.bMapSelectionFlag, 0, "and the selection (0x84A34)");
        CheckEqI32(g_world.msgLines.nLineCount, 0, "removes line 0 (0x84A80), no dismiss test matching flag 20");
        CheckEqI32(g_world.geo.time.bPaused, 0, "and unpauses with an empty list (0x84A8C)");
        CheckEqI32(g_world.bFinalMissionCtlArmed, 0, "and clears the final-mission armed byte (0x84A98)");
        printf("  blits: order panel %d, box %d, clear panel %d\n", (int)nBlitsOrder, (int)nBlitsBox,
               (int)nBlitsClear);
    }

    /* Section 16: a live alien craft on the map, and the panel that either centres on it or attacks it.
     *
     * The fixture is the game's own spawn call. StrategList_AddUfoAt @ 0x7e374 is a tail call to
     * StrategList_AddItem with the alien-craft arguments (nType 1, bActive 1, nId 50000 =
     * STRATEG_ID_UNASSIGNED, bVisible 0), and StrategList_AddItem leaves nTimer 0 (0x82A2C), the phase 0
     * (0x828D4) and the destination equal to the start (0x82964..0x82984), so the entry stands still until a
     * leg is given it. The reveal is the proximity pass, StrategList_UpdateMotion @ 0x80fd8, which walks the
     * ACTIVE items against every player craft off base and makes a near nType 1 item visible; Geoscape_TickShips
     * runs it on every minute tick, which World_PulseGeoscape drives. (The radar pass, StrategList_UpdateDetect
     * @ 0x81948, is not available in this world: it measures from a type-0 inactive list entry and this world has
     * none -- recorded as a separate issue in PLAN.md.)
     *
     * What the panel is then the point of the section. A flag-10 line on message line 0 paints
     * StatusPanel_DrawSelectedContact (0x8E708..0x8F24C, reached through PUFO_STATUS_SELFLAG at 0x8E5D4) with
     * its two handlers installed at 0x8EC84 (*pGeoscapeCtlConfirm -> Smap_OnPointerUp) and 0x8EC90
     * (*pGeoscapeCtlA -> Smap_ClearSelection @ 0x84648), at y 0x88 and 0xA0 (0x8ECCC, 0x8ECF4), and its action
     * label is TEXTS 1212 "Атака НЛО!" when bMapSelectionFlag is set and TEXTS 1213 "Центр на НЛО" when it is
     * clear (0x8ED08's test, the two label y's 0x88 / 0xA1). Both Smap_OnPointerUp arms therefore stand behind
     * this one control: 0x84698 tests the selection, and with it up 0x8491C reads colour slot 0 and launches at
     * it (0x84930) -- the last Ship_LaunchIntercept caller without a gate, and the only one that reads the
     * TARGET out of colour slot 0; with the selection down the same press recentres the camera on the entry
     * slot 0 names (0x846C4..0x848B8, the zoom unscaling at 0x84730..0x84824, the scroll clamp at 0x84838..
     * 0x84884, the crosshair pair at 0x848A4/0x848AC and bSmapCrosshairPending at 0x848B8, nRadarCooldownR
     * cleared at 0x848C4). */
    {
        Ship        *pCraft;
        UiControl   *pCtlTarget, *pCtlConfirm, *pCtlA;
        StrategItem *pUfo;
        i32  nUfo, nCount0, nEntry, nDispId0;
        i32  nOwnX = 0, nOwnY = 0, nUfoX = 0, nUfoY = 0, nUfoPressX = 0, nUfoPressY = 0;
        i32  nEntryX = 0, nEntryY = 0, nEntryPressX = 0, nEntryPressY = 0;
        i32  nScrollX0, nScrollY0, nScrollX1, nScrollY1, nBlitsPanel, nTicks, nChased0;

        printf("section 16: an alien craft the near pass reveals, and the panel that centres or attacks it\n");

        pCraft      = g_world.apCraft[0];
        pCtlTarget  = g_world.ui.apCtl[GEOUI_CTL_186B9C];   /* the card's "new target" */
        pCtlConfirm = g_world.ui.apCtl[GEOUI_CTL_186BA4];   /* *pGeoscapeCtlConfirm */
        pCtlA       = g_world.ui.apCtl[GEOUI_CTL_186BB4];   /* *pGeoscapeCtlA */

        /* The reveal is the proximity pass, not the radar one. StrategList_UpdateMotion @ 0x80fd8 walks the
         * ACTIVE items (0x81034) against every player craft that is not at base (the bAtBase test at 0x81078,
         * after finding the craft's own entry through StrategList_FindByShipId at 0x81084), measures
         * |entry - item| as (int)sqrt(dx*dx + dy*dy + nThresh) (the addd at 0x81100, the sqrt at 0x81104, the
         * dtoi at 0x81114, with nThresh 120 or 1 when nGeoscapeTickSeconds > 300 at 0x8100c) and compares it with
         * the per-craft radius (type 0 -> 15 at 0x81174, type 1 and 4 -> 20 at 0x81148, type 2 -> 25 at 0x8116c,
         * type 3 -> 15, any other value skips the test at 0x81144). Inside that radius an nType 1 item is simply
         * made visible (0x811c8), and 0x8122c then gives a still-unassigned item its display id off
         * nNextStrategDisplayId. Craft 0 is off base and chasing here, so the alien goes eight cells from its
         * entry -- well inside the 15 of a type-0 craft. */
        nEntry = FindStrategById(&g_world.strategList, (i32)pCraft->bId);
        if (nEntry >= 0) {
            const StrategItem *pOwn = (const StrategItem *)PtrArray_Get(&g_world.strategList, nEntry);
            if (pOwn != NULL) { nOwnX = pOwn->nX; nOwnY = pOwn->nY; }
        }
        Check(pCraft->bAtBase == 0 && nEntry >= 0,
              "the chasing craft is off base, the gate 0x81078 tests before the proximity pass measures it");

        nCount0 = PtrArray_GetCount(&g_world.strategList);
        pUfo    = StrategList_AddUfoAt(&g_world.strategList, &g_world.nStrategSeed, g_world.view.nMode,
                                       nOwnX + 8, nOwnY, 0);
        nUfo    = nCount0;
        Check(pUfo != NULL && pUfo->nType == 1 && pUfo->bActive == 1 && pUfo->bVisible == 0
                  && pUfo->nDisplayId == STRATEG_ID_UNASSIGNED && pUfo->nSiteId == -1
                  && pUfo->nTimer == 0 && PtrArray_GetCount(&g_world.strategList) == nCount0 + 1,
              "StrategList_AddUfoAt @ 0x7e374 puts an active invisible type-1 entry eight cells off the chasing "
              "craft (the arguments 0x7E46C hands it), and AddItem leaves nTimer 0 (0x82A2C)");

        /* bRandA (+0x14) is the one StrategItem cell no add path writes. AddItem's field list covers nType
         * (0x82728), bActive (0x82748), bHyperDetect (0x82888), bDetected (0x828A0), bRace (0x828BC), nPhase
         * (0x828D4), bDone (0x828EC), the six coordinate dwords (0x82904..0x82984), the two id dwords
         * (0x829A8..0x829F8), bVisible (0x82A14), nTimer (0x82A2C), bFlagA (0x82A44), bCraftClass (0x82A5C) and
         * nHp (0x82A74) -- and stops. The two writers the port knows are StrategEntry_InitTravel's type-1 active
         * arm (0x7F74C's Game_RandU16(0x12), times the class jitter roll at 0x7F7B0..0x7F7CC, which doubles as
         * the speed jitter) and Base_CreateNamedEntry's region byte (0x669B4, a type-0 entry only), and this
         * contact is never handed a leg, so its byte is whatever the allocator left. The card this section paints
         * reads exactly that byte: the SLOT 0 entry's LDRSB [R0,#0x14] ahead of the class switch, 61 as the
         * multiplier (0x8E920 MOV R2,#0x3D), class 0's base 2200 (0x8E914 MOV R3,#0x898), and the speed line
         * printing the same number again at 0x8EC44. That is why the two pictures this section dumps changed from
         * run to run -- 2078 on the run the committed references came from (2200 - 61*2), 5486 on the next
         * (|2200 - 61*126|), both off the same list. The fixture states the byte, the way the other contact
         * fixtures state nDisplayId: 0 is the no-jitter value, what a zeroed allocation carries, and the value
         * that makes the two references reproducible. The committed pictures are re-made with it. */
        if (pUfo != NULL) {
            pUfo->bRandA = 0;
        }
        nDispId0 = (pUfo != NULL) ? pUfo->nDisplayId : -1;

        /* One pulse is not one minute tick: Geoscape_PulseUpdate advances the clock by
         * Geoscape_AdvanceTime(pTime, nTickSeconds + 1, ...) at 0x95618, and Geoscape_TickShips -- the caller of
         * the proximity pass -- only runs when the clock reaches a minute. So the fixture pulses until the pass
         * answers, the way the running game does. */
        printf("  the alien is at cell %d,%d, the chasing craft (type %d) at %d,%d\n", nOwnX + 8, nOwnY,
               (int)pCraft->bCraftType, nOwnX, nOwnY);
        for (nTicks = 0; nTicks < 40 && (pUfo == NULL || pUfo->bVisible == 0); ++nTicks) {
            World_PulseGeoscape(&g_world);
        }
        printf("  revealed after %d pulse(s) at cell %d,%d\n", nTicks,
               pUfo != NULL ? pUfo->nX : -1, pUfo != NULL ? pUfo->nY : -1);
        Check(pUfo != NULL && pUfo->bVisible == 1,
              "the proximity pass revealed it: inside the radius an nType 1 item is made visible with no timer and "
              "no weapons test (0x811c8, the store sits after the bWeaponsHot block)");
        Check(pUfo != NULL && pUfo->nDisplayId != nDispId0 && pUfo->nDisplayId >= 0
                  && pUfo->nDisplayId != STRATEG_ID_UNASSIGNED,
              "and assigned it a display id off the list counter in the same pass (0x8122c, the "
              "nNextStrategDisplayId++ for a visible item still at 50000)");

        /* Then the fixture moves the revealed entry sixty cells north, and says so plainly. The reveal needs the
         * alien NEAR the craft; the press needs it ALONE in its window -- the scan at 0x82C48 walks 16 x 16 cells
         * around the press, and a second match takes 0x832B0's box branch instead of the one-item dispatch, while
         * the chasing craft flies east along this very row. A wandering alien also has a leg by now (the pass gave
         * it one), so the move re-writes the leg the way AddItem wrote it: nDest = nStart = position
         * (0x82964..0x82984), which is the no-leg state the motion pass leaves alone. Nothing else is touched. */
        if (pUfo != NULL) {
            pUfo->nX = nOwnX;
            pUfo->nY = nOwnY - 60;
            pUfo->nStartX = pUfo->nX;
            pUfo->nStartY = pUfo->nY;
            pUfo->nDestX  = pUfo->nX;
            pUfo->nDestY  = pUfo->nY;
        }
        nEntry = FindStrategById(&g_world.strategList, (i32)pCraft->bId);
        if (nEntry >= 0) {
            const StrategItem *pOwn = (const StrategItem *)PtrArray_Get(&g_world.strategList, nEntry);
            if (pOwn != NULL) { nOwnX = pOwn->nX; nOwnY = pOwn->nY; }
        }
        Check(pUfo != NULL && (pUfo->nX - nOwnX > 8 || nOwnX - pUfo->nX > 8
                                 || pUfo->nY - nOwnY > 8 || nOwnY - pUfo->nY > 8),
              "the alien now sits outside the 16 x 16 scan window (0x82C48) around its own press, so the press "
              "meets one item and 0x832B0 dispatches instead of building a box");

        /* The contact message inside the pass (0x818FC) is a 1000-sided draw -- nForce 100 makes the
         * percentage 100, so only rolls under 100 post it -- and the section needs the message list empty to
         * reach the select test rather than a message arm. The remover is the same one every panel uses. */
        while (g_world.msgLines.nLineCount > 0) {
            UiLineList_Remove(&g_world.msgLines, 0);
        }
        (void)GameTime_Unpause(&g_world.geo.time);   /* GameTime_Pause @ 0x5d6ac is a store; this is its pair */
        if (pUfo != NULL) { nUfoX = pUfo->nX; nUfoY = pUfo->nY; }
        Check(ScreenForCell(&g_world.view, nUfoX, nUfoY, &nUfoPressX, &nUfoPressY)
                  && nUfoPressY < PUFO_SURF_H - (i32)g_world.ui.apPad[GEOUI_PAD_186BBC]->uHeight,
                  "the alien's cell has a screen pixel above the strip (0x82C18's gate)");
        printf("  the alien stands at cell %d,%d, pressed at screen %d,%d\n", nUfoX, nUfoY, nUfoPressX, nUfoPressY);

        /* 1. A press on it with no selection armed: the type-1 ACTIVE dispatch arm, 0x833D0. */
        nScrollX0 = g_world.view.nScrollX;
        nScrollY0 = g_world.view.nScrollY;
        World_GeoscapePointer(&g_world, nUfoPressX, nUfoPressY, true);
        (void)World_GeoscapePointerUp(&g_world);
        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), PUFO_STATUS_SELFLAG,
                   "the type-1 active arm inserts flag 10 at line 0 (0x83408), the panel's own flag");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), nUfo,
                   "with the ITEM index in colour slot 0 (0x83408's fourth argument) -- the value both "
                   "Smap_OnPointerUp arms read back");
        CheckEqI32((i32)g_world.ui.pStatusPanel->nUnk3032, 0, "the panel row cursor cleared (0x833DC)");
        CheckEqI32(g_world.geo.time.bPaused, 1, "and the arm pauses (GameTime_Pause @ 0x83410)");
        CheckEqI32(g_world.bMapSelectionFlag, 0, "with the selection still down, so the panel offers the centring");

        ClearSurface();
        (void)DrawFrame();
        nBlitsPanel = g_nBlits;
        Check(pCtlConfirm->y == PUFO_STATUS_SELCTL_Y_STD_CONF
                  && pCtlConfirm->pfnClick == g_world.frame.panel.pfnSelectTarget,
              "0x8EC84 installed Smap_OnPointerUp on *pGeoscapeCtlConfirm at y 0x88 (0x8ECCC)");
        Check(pCtlA->y == PUFO_STATUS_SELCTL_Y_STD_CLEAR
                  && pCtlA->pfnClick == g_world.frame.panel.pfnClearSelection,
              "and 0x8EC90 installed Smap_ClearSelection on *pGeoscapeCtlA at y 0xA0 (0x8ECF4)");
        Check(pCtlConfirm->x == 120 - ((i32)pCtlConfirm->uWidth / 2)
                  && pCtlA->x == 120 - ((i32)pCtlA->uWidth / 2),
              "both controls centred on x 120 (StatusPanel_BlitSelControl's 0x8ECC0), while the size and speed "
              "lines are the left-aligned text at x 40 (0x8EBBC, 0x8EC68)");
        DumpFrame("geoscape_contact_centre");

        /* 2. The cancel control: Smap_ClearSelection @ 0x84648. */
        PressCtl(pCtlA);
        CheckEqI32(g_world.msgLines.nLineCount, 0, "it drops line 0 (0x84658)");
        CheckEqI32(g_world.geo.time.bPaused, 0, "and unpauses, the list being empty (0x84664)");
        CheckEqI32(g_world.bMapSelectionFlag, 0, "with the selection byte cleared (0x84674)");

        /* 3. The centring press: the same control with the selection down takes Smap_OnPointerUp's other arm. */
        Check(ScreenForCell(&g_world.view, nUfoX, nUfoY, &nUfoPressX, &nUfoPressY),
              "the alien's cell is on screen at the unchanged scroll (the first press dragged, and a drag with a "
              "message on the line does not scroll -- 0x87CF8)");
        World_GeoscapePointer(&g_world, nUfoPressX, nUfoPressY, true);
        (void)World_GeoscapePointerUp(&g_world);
        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), PUFO_STATUS_SELFLAG, "flag 10 posted again");
        ClearSurface();
        (void)DrawFrame();
        PressCtl(pCtlConfirm);
        nScrollX1 = g_world.view.nScrollX;
        nScrollY1 = g_world.view.nScrollY;
        printf("  the camera went from scroll %d,%d to %d,%d for the entry at cell %d,%d\n",
               nScrollX0, nScrollY0, nScrollX1, nScrollY1, nUfoX, nUfoY);
        CheckEqI32(g_world.view.nMode, 0,
                   "the view is still at zoom 0, so neither unscale branch runs: 0x8471C tests mode 1 and "
                   "0x847A4 mode 2, and both multiply nX / nY down before the centring");
        {
            const i32 nHalfW = g_world.view.nMapW / 2 - 120;   /* the ARM's v3, 0x846C4 */
            const i32 nHalfH = g_world.view.nMapH / 2 - 160;   /* its v4, 0x846D8 */
            const i32 nStrip = (i32)g_world.ui.apPad[GEOUI_PAD_186BBC]->uHeight;
            i32 nWantY = nUfoY - nHalfH - 160;                 /* 0x84830 */
            i32 nWantX = nUfoX - nHalfW - 120;                 /* 0x8484C, the x has no clamp */
            i32 nWantCrossY;

            if (nUfoY - 160 < 0) { nWantY = -nHalfH; }         /* 0x84848's RSBMI arm */
            if (nWantY + nHalfH > nStrip + g_world.view.nMapH - 320) {
                nWantY = nStrip - nHalfH + g_world.view.nMapH - 320;   /* 0x84874..0x84884 */
            }
            nWantCrossY = nUfoY - nWantY - nHalfH;             /* 0x84888 */
            if (nWantCrossY > g_world.view.nMapH) { nWantCrossY -= g_world.view.nMapH; } /* 0x84890 */

            Check(g_world.view.nScrollX == nWantX && g_world.view.nScrollY == nWantY,
                  "the clear arm recentres the scroll on the entry colour slot 0 names (0x8484C, 0x84838), the "
                  "y clamp at 0x84848 because the entry sits above row 160");
            Check(g_world.frame.cross.nCrosshairX == 120
                      && g_world.frame.cross.nCrosshairY == nWantCrossY,
                  "and writes the crosshair pair as nY - nScrollY - (nMapH / 2 - 160) with x 120 (0x84888, 0x848A4)");
        }
        Check(g_world.frame.g.bCrosshairPending == 1, "and arms the crosshair (bSmapCrosshairPending @ 0x848B8)");
        CheckEqI32(g_world.frame.g.nTapCooldown, 0, "with the radar cooldown cleared (nRadarCooldownR @ 0x848C4)");
        CheckEqI32(g_world.msgLines.nLineCount, 0, "the line is removed either way (0x84944)");
        CheckEqI32(g_world.bMapSelectionFlag, 0, "and the select arm 0x84698 was not taken (0x84938)");

        /* 4. The attack: arm the selection from the card, then press the alien. The 0x833D0 arm never writes
         *    bMapSelectionFlag -- the same fact Part 91 pinned for the type-4 arm -- so flag 10 arrives with the
         *    selection still up, which is the pair the ATTACK label and the launch arm need. */
        nEntry = FindStrategById(&g_world.strategList, (i32)pCraft->bId);
        if (nEntry >= 0) {
            const StrategItem *pOwn = (const StrategItem *)PtrArray_Get(&g_world.strategList, nEntry);
            if (pOwn != NULL) { nEntryX = pOwn->nX; nEntryY = pOwn->nY; }
        }
        Check(nEntry >= 0 && ScreenForCell(&g_world.view, nEntryX, nEntryY, &nEntryPressX, &nEntryPressY),
              "the craft's own entry is reachable at the new scroll");
        nChased0 = pCraft->nStrategIdx;
        World_GeoscapePointer(&g_world, nEntryPressX, nEntryPressY, true);
        (void)World_GeoscapePointerUp(&g_world);
        Check(g_world.msgLines.nLineCount == 2 && UiLineList_GetFlag(&g_world.msgLines, 0) == 11
                  && UiLineList_GetFlag(&g_world.msgLines, 1) == 11,
              "the press runs Smap_HitTestStrategSelect TWICE -- 0x87C10 to decide the drag flag and 0x87C94 again "
              "inside the cooldown gate (0x87C58 tests nRadarCooldownR < 6, and 0x848C4 cleared it -- a successful "
              "press hit would park it at 255 at 0x87CB4) -- and the type-1 inactive branch appends a card line at "
              "0x83688 each time, so lines 0 and 1 are both the craft's card");
        PressCtl(pCtlTarget);
        Check(g_world.msgLines.nLineCount == 2 && UiLineList_GetFlag(&g_world.msgLines, 0) == 27
                  && UiLineList_GetFlag(&g_world.msgLines, 1) == 11,
              "Smap_SelectMapTarget puts the prompt at index 1 (0x84574) and removes index 0 (0x84580): the insert "
              "shifts the two cards up to 1 and 2, the removal pulls the prompt down to 0 and copies 11 into 1, "
              "because UiLineList_Remove never clears the vacated top slot (0x5A428, Part 91's note)");
        CheckEqI32(g_world.bMapSelectionFlag, 1, "the selection armed (0x84498)");
        Check(pCraft->bIntercepting == 1,
              "and the waypoint removal arm SKIPPED: 0x844F0 tests the chased entry's type, and this craft is "
              "chasing a type-4 contact, not a type-5 waypoint");

        /* The centring moved the camera, so the alien's press pixel has to come from the new scroll. */
        Check(ScreenForCell(&g_world.view, nUfoX, nUfoY, &nUfoPressX, &nUfoPressY),
              "the alien is on screen at the centred scroll");
        World_GeoscapePointer(&g_world, nUfoPressX, nUfoPressY, true);
        (void)World_GeoscapePointerUp(&g_world);
        Check(g_world.msgLines.nLineCount == 2 && UiLineList_GetFlag(&g_world.msgLines, 0) == 10
                  && UiLineList_GetFlag(&g_world.msgLines, 1) == 11,
              "0x83408 removes line 0 and inserts flag 10 at 0, so the second card line is still there underneath "
              "the contact line");
        CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), PUFO_STATUS_SELFLAG,
                   "the press on the alien answers the prompt with flag 10 (0x833D0's arm)");
        CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), nUfo, "colour slot 0 is the alien's index");
        CheckEqI32(g_world.bMapSelectionFlag, 1,
                   "and the selection is STILL up: 0x833D0's arm writes no flag byte either");

        ClearSurface();
        (void)DrawFrame();
        Check(pCtlConfirm->y == PUFO_STATUS_SELCTL_Y_STD_CONF
                  && pCtlConfirm->pfnClick == g_world.frame.panel.pfnSelectTarget,
              "the same control, now answering the ATTACK label (0x8ED08's test, TEXTS 1212 at 0x8EDC8)");
        DumpFrame("geoscape_contact_attack");

        /* The press arm for flag 10 (0x87AEC) hit-tests *pGeoscapeCtlConfirm then *pGeoscapeCtlA, and
         * UiControl_HitTest fires a mode-0 control's pfnClick on the RELEASE: the press path measures the box at
         * 0x1A42C..0x1A480, branches on bDown at 0x1A488, and only raises nState 0 -> 1 at 0x1A4F8..0x1A50C, while
         * the release path -- box never measured -- tests nState == 1 at 0x1A558, drops it at 0x1A568, redraws at
         * 0x1A56C and tail-calls the handler at 0x1A57C/0x1A580. So the launch is the release's doing, and the
         * press only arms the control. */
        nCount0 = PtrArray_GetCount(&g_world.strategList);
        (void)World_GeoscapePointer(&g_world, pCtlConfirm->x + (i32)pCtlConfirm->uWidth / 2,
                                    pCtlConfirm->y + (i32)pCtlConfirm->uHeight / 2, true);
        Check(pCraft->nStrategIdx == nChased0 && pCtlConfirm->nState == 1
                  && UiLineList_GetFlag(&g_world.msgLines, 0) == PUFO_STATUS_SELFLAG,
              "the press arms *pGeoscapeCtlConfirm (nState 1) and nothing else: the craft still chases what it "
              "chased and line 0 is still the contact line (UiControl_HitTest's press path 0x1A42C..0x1A530)");
        (void)World_GeoscapePointerUp(&g_world);
        Check(pCraft->bWeaponsHot == 1 && pCraft->bAtBase == 0 && pCraft->bIntercepting == 1
                  && pCraft->nStrategIdx == nUfo,
              "the release runs it: Smap_OnPointerUp's select arm launches at the index colour SLOT 0 names "
              "(0x8491C reads it, 0x84930 calls Ship_LaunchIntercept) -- the last of the four callers to be gated");
        CheckEqI32(g_world.bMapSelectionFlag, 0, "and clears the selection (0x84938)");
        Check(g_world.msgLines.nLineCount == 1 && UiLineList_GetFlag(&g_world.msgLines, 0) == 11,
              "the tail removal at 0x84944 peels the flag-10 contact line and leaves the SECOND card line the "
              "press's double call wrote underneath it, now at index 0");
        CheckEqI32(g_world.geo.time.bPaused, 1,
                   "and the unpause at 0x84954 is not taken: it is gated on the list being empty, and the leftover "
                   "card line keeps it non-empty");
        Check(pUfo != NULL && pUfo->nType == 1 && pUfo->bActive == 1
                  && PtrArray_GetCount(&g_world.strategList) == nCount0,
              "the alien is still in the list, active, and the target of the craft");
        printf("  panel blits %d; the craft chases entry %d at cell %d,%d\n", (int)nBlitsPanel, nUfo,
               pUfo != NULL ? pUfo->nX : -1, pUfo != NULL ? pUfo->nY : -1);
    }

    /* ---- section 17: the arm that ends a run empties the map the geoscape walks (Part 93) -------
     * Every return from a mission so far took Tact_Shutdown with bKeepSome 0. The value 1 belongs to the
     * arms that END a run -- Tact_CheckBattleEnd's victory arm (0x35014) and its failed-base arm (0x35180),
     * UiScreen_OnBack's quit arm (0x156D4) and App_QuitFromTact (0x18E20) -- and that branch is where the
     * teardown reaches into the geoscape's own block: LDR R0,=sGeoscapeScreenBlock (0x3E14C), ADD R0,#0x3C
     * (0x3E150), BL PtrArray_DeleteAll (0x3E154). The strateg list is that +0x3C field, so the ending arm
     * empties it and frees the items. Nothing on this side of the run re-creates it: Base_CreateNamedEntry
     * is the only writer of a type-0 entry (0x669A8) and its two code callers are the new game (0x94964) and
     * the placement commit (0x59AFC), while Geoscape_InitUI(1) skips the new-game branch (gate 0x868EC).
     *
     * The arm reached here is the abort button: UiScreen_OnBack case 9 runs UiModal_End (0x156BC) and
     * Tact_CheckBattleEnd with bAbort 1 (0x156C8), which stores the abort into Map_bAltOutcomePath (0x34FE4)
     * and, with no victory, takes LABEL_20 -> LABEL_21 and then the failed-base gate at 0x35120. That gate
     * reads the mission type twice (0x35138 for the alien-base/Mars half, 0x3515C for the base half) and
     * reads the base count byte at 0x159D80 SIGNED (0x35160) -- so the arm means "the defence of the base was
     * lost and the world has one base". The run has three bases and a victory byte left over from the mission
     * won at 0x350F0, so the fixture states the two cells the gate reads, runs the check, and puts them back.
     * The mission itself is brought up with the shell's own Tact_InitMission seam (World_InitTactical, the
     * same call 0x83AC4 makes on the launch tail and 0x44E24 makes inside LoadGame). */
    {
        i32  nBefore = PtrArray_GetCount(&g_world.strategList);
        i8   bBaseSave = g_world.nBaseCount;
        i32  nTypeSave = g_world.nMapMissionType;
        u16  nScreenSave = g_world.nUiScreen;
        /* The arm writes three cells this fixture does not want carried forward: LABEL_20 sets
         * Map_bTickHold (Map+0x5A) at 0x35114, 0x35190 sets Map_bBattleEnded, and 0x35110 stores the
         * outcome. Map_bTickHold is GameTick_OnTimer's gate 1 (0x274FC) -- left standing, it keeps every
         * later tick out of the tactical branch, which is what stalled the mission section 18 builds. */
        bool bTickSave = g_world.bMapTickHold != 0;
        bool bEndSave  = g_world.bMapBattleEnded != 0;
        i32  nOutSave  = g_world.nMapMissionOutcome;
        bool bLive;

        printf("section 17: the failed base defence ends the run, and the ending arm empties the strateg list\n");
        Check(g_world.bTactLive == 0,
              "the geoscape stands on its own here: no mission is live before the one this section brings up");
        bLive = World_InitTactical(&g_world, 0);
        Check(bLive && g_world.bTactLive && g_world.pTactMap != NULL && g_world.pTactState != NULL,
              "Tact_InitMission mode 0 brings the frame up (the launch tail's own 0x83AC4 call)");
        CheckEqI32(nBefore, PtrArray_GetCount(&g_world.strategList),
                   "and bringing it up does not touch the map: the strateg list is the geoscape's, not the frame's");

        /* The two cells the arm reads, stated for the arm and restored after it. */
        g_world.nMapMissionType    = PUFO_MISSION_TYPE_XCOM_BASE;
        g_world.nBaseCount         = (i8)PUFO_FACILITY_END_FLAG_SET;
        g_world.bMapMissionVictory = 0;
        Check(g_world.nMapMissionType == PUFO_MISSION_TYPE_XCOM_BASE && g_world.nBaseCount == 1
                  && g_world.bMapMissionVictory == 0,
              "the fixture states the base-defence type and the base count byte the gate tests at 0x3515C and "
              "0x35160, and a lost mission (0x35178 needs bMissionVictory 0)");

        Check(World_MissionEndCheck(&g_world, 1) == true,
              "the abort button's check runs to the end (Map_bBattleEnded was 0, so 0x34FFC does not return early)");
        Check(g_world.nMapMissionOutcome == 2 && g_world.bMapTickHold == 1 && g_world.bMapBattleEnded == 1,
              "the abort wrote outcome 2 at 0x35110, LABEL_20 set the tick hold at 0x35114 and the arm set "
              "battle-ended at 0x35190");
        Check(g_world.nUiScreen == nScreenSave,
              "and the debrief never ran: the arm returns at 0x35194, before Map_nUiScreen = 13 at 0x351F4 and "
              "before any of the tallies");
        CheckEqI32(PtrArray_GetCount(&g_world.strategList), 0,
                   "the teardown emptied the geoscape's own list -- 0x3E154 is PtrArray_DeleteAll over the block "
                   "at +0x3C -- and freed the items, so the map the geoscape walks is empty");
        Check(PtrArray_Get(&g_world.strategList, 0) == NULL,
              "and it is empty rather than merely counted down: the array is gone with its items");
        Check(g_world.saveStrateg.nStrategCount != 0,
              "the count mirror is left at its old value, exactly as the original leaves 0x186530 stale here "
              "(only 0x82710 and 0x82B34 refresh it) -- a save made from this state would walk items that no "
              "longer exist, which is why World_SaveSlot re-states the cell before every write");
        Check(g_world.bTactLive == 0 && g_world.bTactFlag == 0 && g_world.pTactMap == NULL,
              "and the frame is down: the same seam Part 47 made stand for the original's global teardown");
        printf("  the ending arm took the map from %d entries to 0 (mirror left at %d)\n", nBefore,
               g_world.saveStrateg.nStrategCount);

        g_world.nBaseCount      = bBaseSave;
        g_world.nMapMissionType = nTypeSave;
        g_world.nUiScreen       = nScreenSave;
        g_world.bMapTickHold    = bTickSave ? 1 : 0;
        g_world.bMapBattleEnded = bEndSave ? 1 : 0;
        g_world.nMapMissionOutcome = nOutSave;
    }

    /* ---- section 18: the resumed mission plays (Part 98) ----------------------------------
     *
     * The mission Part 62 saved comes back at the very end of the run and plays through the
     * seams the game itself uses, with a picture at every stage:
     *
     *   build         World_InitTactical(0) builds the mission the way the launch tail does (0x83AC4), on
     *                 a tile grid the fixture finished, so the terrain build loads both shipped node kinds
     *                 and the deploy and the reinforcement picker have somewhere to put anybody; the
     *                 visibility pass Part 97 established makes the squad drawable.
     *   pad 5         MenuBtn_EndTurn @ 0x16494 -> Tact_BeginEnemyTurn @ 0x4ca18: BeforeEnemyStep
     *                 @ 0x4ba3c pulls real aliens from the reserve onto the field (the pull the
     *                 widget-5 test emptied on purpose), the phase-3 ticks run Tact_EnemyAiTick,
     *                 Tact_EndEnemyTurn @ 0x4cb74 closes the bracket.
     *   pen x2        PointerScreen0's camera arm @ 0x143D0 moves the cursor under the pen and
     *                 0x144CC commits a press that did not move it: Input_OnAction ->
     *                 Tact_HandleMapClick @ 0x1512c, whose latch-0 arm reaches Unit_PathToCell
     *                 @ 0x2c2a8 -- turn, steps, end mark enqueued by the original's own code.
     *   pad 39        Tact_BeginMultiTarget @ 0x18380 arms the targeting latch; the same double
     *                 press on an enemy cell lands in case 3 (0x152E4: cell +0x40 flag, target
     *                 list, look control enabled), and the look control's installed handler is
     *                 TactAction_ConfirmFireBurst @ 0x18774 -> TactState_EnqueueFireBurst
     *                 @ 0x4b080: command 14 enters the queue and the fire processor
     *                 Tact_ProcessMoveAction's cmd-0xE arm @ 0x4FE34..0x509D8 resolves the shot
     *                 through Unit_ApplyCellDamage @ 0x2e5d0 -- real HP comes off a real alien.
     *   pad 5 again   the aliens get their own turn: some enemy cell changes or a squad member's
     *                 HP/status changes (Tact_EnemyAiTick @ 0x4cbe0 through EnemyAi_TryFireOrWander
     *                 @ 0x4abc8, Unit_PathToCell again or the burst at 0x4d910).
     *   save 64       World_SaveSlot writes the mission block (mcnt @ 0x44988's tact arm,
     *                 Save_WriteTGameData @ 0x446a8, the map, the roster, the aliens, the hidden
     *                 park); load 64 brings the same HP and cells back.
     *
     * It is the LAST section on purpose: every Game_RandU16 draw it spends belongs to it alone,
     * so no earlier pin -- the squad stream, the contact cards, the descent rolls -- shifts. */
    {
#define CLICK(i) do { if (g_bank.aCtl[i].pfnClick != NULL) { g_bank.aCtl[i].pfnClick(); } } while (0)
        /* One clock of the real pump: PocketUFO_WndProc @ 0xA08C0 runs the 0xC350 handler AND a frame,
         * and the frame is not a cosmetic half -- TactHud_TickCursorDir @ 0x1e888 (0x1e8a8) is the only
         * mover of apUiSlotsA[10], and GameTick_OnTimer's gate 3 (0x2753C) refuses the tactical arm
         * while that field holds anything but 10 or 0. A drain of ticks alone therefore stops dead the
         * moment the fade-out animation (TactHud_ComputeCursorDirState @ 0x1a7c8, 0x1a828/0x1a82c) has
         * the field mid-count. Every drain below pumps both. */
#define PUMP() do { (void)World_GameTickOnce(&g_world); World_TactFrame(&g_world); } while (0)
        GameMap         *pMap;
        TactState       *pQ;
        TactPointerWorld ptr;
        WorldHooks       saveHooks;
        DatFile          dat;
        Soldier         *pSel;
        MapCell         *pTarget;
        i32              i, k, t;
        i32              nReserve0, nEnemy0, nXcom0;
        i32              nPenX = -1, nPenY = -1;
        u16              uTuSel0;
        u16              aEHp0[PUFO_MAP_ROSTER_MAX];
        u16              aEXHp0[PUFO_MAP_ROSTER_MAX];
        i32              aEX0[PUFO_MAP_ROSTER_MAX], aEY0[PUFO_MAP_ROSTER_MAX];
        u16              uHpSel0, uHpSel1, uStSel1;
        i32              nHpDropped = 0, nEnemyMoved = 0, nShotQueued = 0;
        i32              nEnemyShot0 = 0, nEnemyAct0 = 0;

        printf("section 18: a mission built on a finished base -- reinforcements, a walk, a shot, an enemy turn, a save and a load\n");
        /* The map is the game's own build on a base the fixture finished. The 36 tactical tile bytes at
         * aMissionTileTypes @ 0x159D5C are what Smap_InitCraftSelectState fills at 0x84124, and they name the
         * .ma1/.rmp pair Map_BuildMissionTerrain puts on each tile (0x23eec: "<xbase_>" + the tile byte,
         * 0x240f4: the .rmp load, skipped only for a 0x14 tile). Part 66's mission had ONE finished facility,
         * so its map was bare ground plus xbase_1: eight nodes, every one record byte 20 == 1 -- the soldier
         * nodes. A rank-2 alien is gated on byte 20 == 2 by the movement-class test at 0x55524, and xbase_0 is
         * that record set, so the fixture finishes the whole base: 36 tiles carrying placement codes 0..7, both
         * node kinds, floors that are not the transparent placeholder, and a squad the deploy can place. */
        for (i = 0; i < 36; ++i) {
            g_world.abMapTileTypes[i] = (u8)(i % 8);
        }
        g_world.nMapMissionType = PUFO_MISSION_TYPE_XCOM_BASE;
        g_world.nMapMapLayers   = 0;   /* the Xcom arm 0x84118 writes 0, which selects the fixed 6x6 base build */
        /* The launch tail ends with GeoscapeUi_FreeAll(0) at 0x83ABC before Tact_InitMission(0) at
         * 0x83AC4, and that call's write at 0x86D34 is what CLEARS Map_bGeoscapeUi (Map+0x44) -- the
         * byte GameTick_OnTimer's gate 2 (0x27508) tests before it takes the tactical branch. This
         * section brings the mission up through the World_InitTactical seam rather than the Smap tail,
         * so the byte the FreeAll would have written is stated here; without it every tick stays on
         * the geoscape arm and the mission's queue never advances. */
        g_world.bGeoscapeUi     = 0;
        Check(World_InitTactical(&g_world, 0) != 0,
              "Tact_InitMission mode 0 builds the mission on the finished base (0x3c550 -> 0x22e10)");
        pMap = g_world.pTactMap;
        pQ   = g_world.pTactState;
        printf("  build wiring: tact live %d, frame hooks %d, tact input hooks %d, bank 5 %d, bank 39 %d, look click %d\n",
               World_bTactPointerLive(&g_world), g_world.pFrameHooks != NULL,
               (g_world.pFrameHooks != NULL && g_world.pFrameHooks->pTactInputHooks != NULL),
               g_bank.aCtl[5].pfnClick != NULL, g_bank.aCtl[39].pfnClick != NULL,
               g_world.sTactLookCtl.pfnClick != NULL);
        printf("  built map: dims %d/%d, tile shift %d/%d, nXcom %d, field %d, reserve %d\n",
               pMap != NULL ? pMap->geom.nDimX : -1, pMap != NULL ? pMap->geom.nDimY : -1,
               g_world.sTactInput.nTileShiftX, g_world.sTactInput.nTileShiftY,
               pMap != NULL ? (int)pMap->nXcomCount : -1,
               pMap != NULL ? (int)pMap->nEnemyCount : -1, g_world.nMapEnemyReserveCount);
        for (i = 0; i < (i32)pMap->nXcomCount; ++i) {
            Soldier *pU = pMap->apXcomUnits[i];

            if (pU != NULL) {
                printf("    parked %d: cell %d/%d/%d, type %u, uTu %u, hp %u, flags %02X\n", i,
                       pU->pCell != NULL ? pU->pCell->nX : -1,
                       pU->pCell != NULL ? pU->pCell->nY : -1,
                       pU->pCell != NULL ? (int)pU->pCell->nZ : -1, (unsigned)pU->nUnitType,
                       (unsigned)pU->uTu, (unsigned)pU->uHp, (unsigned)pU->bFlags);
            }
        }
        printf("  queue after the build: len %d, active %d, cursor %d\n", (int)pQ->nQueueLen,
               (int)pQ->bQueueActive, (int)pQ->nQueueCursor);
        printf("  options: scroll %d show %d run %d turn %d menu %d nodrag %d\n",
               (int)g_options.bScrollByPen, (int)g_options.bShowBorder, (int)g_options.bRunByDblClick,
               (int)g_options.bTurnByPen, (int)g_options.bMenuByPen, (int)g_options.bForceNoDrag);
        /* The pen dispatch reads the shared option bytes (anPhaseSave0[93..97], 0x128A8D/8F/90/91 --
         * Map_InitAndLoadRegistry @ 0x22648, the toggle pads 0x19250..0x1941c). The options-screen
         * tests ran earlier in this file; restate the original's registry defaults here so the walk
         * press commits (scrollbypen/runbydblclick "1", turnbypen/forcenodrag "0"). */
        g_options.bScrollByPen   = 1;
        g_options.bRunByDblClick = 1;
        g_options.bTurnByPen     = 0;
        g_options.bMenuByPen     = 1;
        /* A new mission comes up on the loadout screen -- the ui screen 20 the state print shows. The
         * player's confirm is Inv_CloseAndCommit @ 0x15a48 (Input_OnConfirm's screen-1 arm at 0x16104):
         * the loadout snapshot, then Tact_DeployTeamAndHwps @ 0x39980, whose phase 3 (0x39E9C) walks the
         * mission's deploy points and re-places every unit on the first cell with no occupant, then
         * MapView_FocusSelected, Map_RefreshCellMeta, the per-unit visibility pass, and Map_nUiScreen = 0.
         * Tact_SelectTeamUnits @ 0x39744 parked all eight on ONE anchor cell, and for a layer count of 0
         * that anchor is the (0,0,0) the "parked" print above shows. */
        World_InvCloseAndCommit(&g_world);
        {
            i32 nDeployed = 0;

            for (i = 0; i < (i32)pMap->nXcomCount; ++i) {
                Soldier *pU = pMap->apXcomUnits[i];

                if (pU == NULL || pU->pCell == NULL) {
                    continue;
                }
                printf("    deployed %d: cell %d/%d/%d\n", i, pU->pCell->nX, pU->pCell->nY,
                       (int)pU->pCell->nZ);
                if (pU->pCell->pUnit == pU) {
                    ++nDeployed;
                }
            }
            Check(nDeployed == (i32)pMap->nXcomCount,
                  "the deploy put every squad member on a cell that owns it (0x39E9C re-place, Unit_PlaceOnMap @ 0x49A0C)");
        }
        printf("  queue after the deploy commit: len %d, active %d, cursor %d, entry 0 cmd %u at %d/%d/%d\n",
               (int)pQ->nQueueLen, (int)pQ->bQueueActive, (int)pQ->nQueueCursor,
               pQ->nQueueLen > 0 ? (unsigned)pQ->aQueue[0].nCmd : 0u,
               pQ->nQueueLen > 0 ? (int)pQ->aQueue[0].nX : -1,
               pQ->nQueueLen > 0 ? (int)pQ->aQueue[0].nY : -1,
               pQ->nQueueLen > 0 ? (int)pQ->aQueue[0].nZ : -1);
        printf("  state: tact live %d, tact flag %d, geoscape ui %d, ui screen %d, tick hold %d, "
               "battle ended %d\n",
               (int)g_world.bTactLive, (int)g_world.bTactFlag, (int)g_world.bGeoscapeUi,
               (int)g_world.nUiScreen, (int)g_world.bMapTickHold, (int)g_world.bMapBattleEnded);
        Check(pMap != NULL && pQ != NULL && g_world.bTactFlag != 0 && g_world.bGeoscapeUi == 0,
              "the mission frame stands live and the pen is the mission's (GeoscapeUi_FreeAll cleared the mode byte at 0x86D34)");

        /* The pass Part 97 established: one Unit_UpdateVisibility @ 0x48580 per squad member, so
         * every member stands on a revealed cell and MapView_DrawUnit @ 0x1c958 admits them. */
        for (i = 0; i < (i32)pMap->nXcomCount; ++i) {
            if (pMap->apXcomUnits[i] != NULL) {
                (void)Unit_UpdateVisibility(pMap->apXcomUnits[i], &g_world.tactEnv.vis);
            }
        }
        ClearSurface();
        World_TactFrame(&g_world);
        DumpFrame("tact_play_start");

        pSel = pMap->pSelUnit;
        Check(pSel != NULL && pSel->pCell != NULL, "a squad member stands selected on the resume");
        nXcom0   = (i32)pMap->nXcomCount;
        nEnemy0  = (i32)pMap->nEnemyCount;
        nReserve0 = g_world.nMapEnemyReserveCount;
        printf("  resume: %d xcom, %d on the field, %d in the reserve, selected at %d/%d/%d, uTu %u, hp %u\n",
               nXcom0, nEnemy0, nReserve0, pSel->pCell->nX, pSel->pCell->nY, pSel->pCell->nZ,
               (unsigned)pSel->uTu, (unsigned)pSel->uHp);

        /* 1) reinforcements through the game's own end-turn bracket. BeginEnemyTurn @ 0x4ca18 runs
         * BeforeEnemyStep, then a full Tact_ProcessActionPhase pass at 0x4cb54 before it returns --
         * the press drives the whole enemy turn, and EndEnemyTurn's tail hands the turn back:
         * input re-enabled, the AI bookkeeping reset, uTu refilled by AfterEnemyStep (0x4c4a0). */
        printf("  rmp spawn table: %d nodes, %d modules\n",
               (int)g_world.sTactRmp.nNodeCount, (int)g_world.sTactRmp.nModCount);
        for (i = 0; i < (i32)g_world.sTactRmp.nNodeCount && i < 12; ++i) {
            const RmpNode *pN = g_world.sTactRmp.apNodes[i];

            if (pN != NULL) {
                printf("    node %d: %d/%d/%d flag19 %u flag20 %u\n", i,
                       (int)pN->nX, (int)pN->nY, (int)pN->nZ, (unsigned)pN->bFlag19,
                       (unsigned)pN->bFlag20);
            }
        }
        for (i = 0; i < g_world.nMapEnemyReserveCount && i < 4; ++i) {
            Soldier *pH = g_world.apTactHidden[i];

            if (pH != NULL) {
                printf("    hidden %d: type %u rank(+0xE2) %u unk435(+0x1B3) %u cell %d/%d/%d\n",
                       i, (unsigned)pH->nUnitType, (unsigned)pH->bRankType, (unsigned)pH->bUnk435,
                       pH->pCell != NULL ? pH->pCell->nX : -1,
                       pH->pCell != NULL ? pH->pCell->nY : -1,
                       pH->pCell != NULL ? (int)pH->pCell->nZ : -1);
            }
        }
        fflush(stdout);
        CLICK(5); /* widget 5 = MenuBtn_EndTurn @ 0x16494 */
        printf("  after pad 5: busy %d, phase %d, enemy byte %d, field %d, reserve %d, player input %d\n",
               (int)pMap->tact.bBusy, (int)pMap->tact.nPhase, (int)g_world.bMapEnemyTurnActive,
               (int)pMap->nEnemyCount, g_world.nMapEnemyReserveCount, (int)g_world.bMapPlayerInput);
        Check((i32)pMap->nEnemyCount > nEnemy0 && g_world.nMapEnemyReserveCount < nReserve0,
              "BeforeEnemyStep @ 0x4ba3c pulled real aliens out of the reserve onto the field -- the "
              "reinforcement path the widget-5 test emptied on purpose now runs for real");
        /* BeginEnemyTurn's own tail runs a full Tact_ProcessActionPhase pass at 0x4cb54, and with the
         * aliens standing on real cells that pass queues real work: the queued actions resolve on the
         * clock, so the bracket reaches EndEnemyTurn @ 0x4cb74 only once GameTick_OnTimer (0x274f0) has
         * driven the queue empty. Earlier, with nobody placed, the pass had nothing to run and closed
         * inside the press. */
        /* The drain runs the pump the way PocketUFO_WndProc @ 0xA08C0 does it: the clock handler AND a
         * frame. That matters because of gate 3 (0x2753C): the tick only takes the tactical arm while
         * apUiSlotsA[10] (dword_113A40) is 10 or 0, and the pen work above put it at 9 -- the fade-out
         * value TactHud_ComputeCursorDirState @ 0x1a7c8 writes when it also arms the step byte
         * unk_113A3C (0x1a828: -1, and 0x1a82c: 9). The ONLY mover of that field is
         * TactHud_TickCursorDir @ 0x1e888, 0x1e8a8 -- "not 0 and not 10, so add the step" -- and that
         * function runs from the frame (World_TactFrame -> World_TactFrameDraw @ 0x3518), not from the
         * tick. A drain of ticks alone therefore cannot open gate 3: the queue would sit busy forever,
         * which is exactly what the first run of this section showed. Counting 9 down to 0 takes nine
         * frames; the pump has far more than that between two player presses. */
        for (t = 0; t < 900 && (pMap->tact.bBusy != 0 || g_world.bMapEnemyTurnActive != 0); ++t) {
            PUMP();
        }
        printf("  after %d ticks: busy %d, phase %d, enemy byte %d, player input %d, queue len %d\n",
               t, (int)pMap->tact.bBusy, (int)pMap->tact.nPhase, (int)g_world.bMapEnemyTurnActive,
               (int)g_world.bMapPlayerInput, (int)pQ->nQueueLen);
        for (i = 0; i < (i32)pQ->nQueueLen && i < 6; ++i) {
            const TactQueueEntry *pEnt = &pQ->aQueue[i];

            printf("    entry %d: cmd %u unit %s cell %d/%d/%d mode %d tu %d argDword %d facing %d | "
                   "unit type %u its cell %d/%d/%d uTu %u status %u\n",
                   i, (unsigned)pEnt->nCmd, pEnt->pUnit != NULL ? "unit" : "none",
                   (int)pEnt->nX, (int)pEnt->nY, (int)pEnt->nZ, (int)pEnt->nMode, (int)pEnt->nTuCost,
                   (int)pEnt->nArgDword, (int)pEnt->nFacing,
                   pEnt->pUnit != NULL ? (unsigned)pEnt->pUnit->nUnitType : 0u,
                   (pEnt->pUnit != NULL && pEnt->pUnit->pCell != NULL) ? pEnt->pUnit->pCell->nX : -1,
                   (pEnt->pUnit != NULL && pEnt->pUnit->pCell != NULL) ? pEnt->pUnit->pCell->nY : -1,
                   (pEnt->pUnit != NULL && pEnt->pUnit->pCell != NULL) ? (int)pEnt->pUnit->pCell->nZ : -1,
                   pEnt->pUnit != NULL ? (unsigned)pEnt->pUnit->uTu : 0u,
                   pEnt->pUnit != NULL ? (unsigned)pEnt->pUnit->bStatus : 0u);
        }
        printf("    queue cursor %d, queue active %d, ai action %d, ai unit %s, track unit %s, "
               "phase counter %d, flag0 %d flag1 %d | phase active %d, slot8 byte1 %d, ui slot10 %d, "
               "frame scratch %d, tick hold %d, phase tick %d phase max %d\n",
               (int)pQ->nQueueCursor, (int)pQ->bQueueActive, (int)pQ->bAiActionActive,
               pQ->pAiActionUnit != NULL ? "set" : "none",
               pMap->tact.pTrackUnit != NULL ? "set" : "none", (int)pQ->nPhaseCounter,
               (int)pQ->bFlag0, (int)pQ->bFlag1, (int)pQ->bPhaseActive, (int)g_world.bMapSlot8Byte1,
               (int)g_world.nMapUiSlot10, (int)g_world.bMapFrameScratch, (int)g_world.bMapTickHold,
               (int)pQ->nPhaseTick, (int)pQ->nPhaseMax);
        Check(pMap->tact.bBusy == 0 && g_world.bMapEnemyTurnActive == 0 && g_world.bMapPlayerInput != 0,
              "the bracket closed through EndEnemyTurn @ 0x4cb74's tail: busy down, byte down, input back");
        {
            i32 nRefilled = 0;

            /* AfterEnemyStep @ 0x4c4a0's Xcom arm (0x4C73C..0x4C82C): uTu = auStat[0], then the
             * burden penalty q = (4*burden - 4)/auStat[8], d = (2*q*auStat[0])/10. The refill is
             * each soldier's OWN stat block, not one shared number -- comparing every member to
             * the selected soldier's 55 measured the wrong thing. */
            for (i = 0; i < (i32)pMap->nXcomCount; ++i) {
                Soldier *pU = pMap->apXcomUnits[i];
                u16      want;

                if (pU == NULL) {
                    continue;
                }
                want = pU->auStat[0];
                if (pU->auStat[8] != 0) {
                    i32 q = (4 * (i32)pU->uInvBurden - 4) / (i32)pU->auStat[8];

                    want = (u16)((i32)want - (2 * q * (i32)want) / 10);
                }
                if (pU->uTu == want) {
                    ++nRefilled;
                }
            }
            Check(nRefilled == (i32)pMap->nXcomCount,
                  "AfterEnemyStep (0x4c4a0, 0x4C73C..0x4C82C) topped every soldier's own auStat[0] "
                  "back up with the burden penalty taken off -- every member to its own stat block");
        }
        ClearSurface();
        World_TactFrame(&g_world);
        DumpFrame("tact_play_deployed");
        printf("  first enemy turn: %d enemies stand on the field (was %d), %d left in the reserve\n",
               (i32)pMap->nEnemyCount, nEnemy0, g_world.nMapEnemyReserveCount);
        for (i = 0; i < (i32)pMap->nEnemyCount; ++i) {
            Soldier *pE = pMap->apEnemyUnits[i];

            if (pE == NULL) {
                continue;
            }
            printf("    enemy %d: type %u rank %u unk435 %u cell %d/%d/%d, uTu %u, status %u, aiTick %d, aiDone %d, large %d\n",
                   i, (unsigned)pE->nUnitType, (unsigned)pE->bRankType, (unsigned)pE->bUnk435,
                   pE->pCell != NULL ? pE->pCell->nX : -1,
                   pE->pCell != NULL ? pE->pCell->nY : -1,
                   pE->pCell != NULL ? (int)pE->pCell->nZ : -1,
                   (unsigned)pE->uTu, (unsigned)pE->bStatus, (int)pE->nAiTick, (int)pE->bAiDone,
                   (int)Unit_bIsLarge(pE));
        }

        /* The shot further down has to come from a cell the traced arc can actually reach an alien
         * from: the base is walled, and the traced arc stops at the first blocking voxel (0x30184 with
         * Map_TraceVoxelOccupancy returning 0 for wall, ceiling and unit alike), so firing from inside
         * the base at an alien outside it stops at the wall one cell out of the muzzle. The nearest
         * cell that is both reachable by the mission's own search and open onto an alien is the walk
         * target, which is exactly the move a player makes: walk to a firing position, then shoot. */
        /* 2) a walk the map click enqueues. The target is taken the way the game's pen takes a cell: the
         * camera arm (PointerScreen0's 0x143D0 through MapView_SetCamOffset and MapCursor_SetPos @ 0x26f78)
         * resolves each pen pixel onto a cell, and that cell must also pass the mission's own search --
         * Path_ComputeReachable @ 0x2b820 is exactly the call Unit_PathToCell makes at 0x2c334, seeded with
         * the unit's cell, its facing and its type flag (0x2c33c), and a target the fill reached ends with a
         * non-zero cost (the test at 0x2c388). A cell whose ceiling OR floor sprite is the transparent 255
         * placeholder is never a target (MapCell_TestFlag selector 0 @ 0x549c8, the bail at 0x2b84c), though
         * expansion only needs a floor sprite (0x2b604), so such cells are walkable THROUGH. Taking a cell
         * the hover actually lands on is what the click needs too: the press can only commit a cell the
         * cursor can stand on, and the iso resolver answers the cell the screen shows at that pixel -- a
         * floor buried under another storey is not it. The cursor layer byte is the cell layer + 1. */
        i32 aCandX[1], aCandY[1], aCandZ[1];
        i32 nCand = 0;
        i32 nSpotFoe = -1;
        i32 nHoverMinX = 9999, nHoverMaxX = -9999, nHoverMinY = 9999, nHoverMaxY = -9999;

        memset(&ptr, 0, sizeof(ptr));
        ptr.pBank = &g_bank;
        ptr.hit   = g_hit;
        {
            const PadImage *pHud = g_world.apTactPads[PUFO_UIPAD_HUD];
            i32 nMapBottom = (pHud != NULL) ? (i32)pHud->nOriginY - 20 : 240;

            /* The pen hovers only the band the real screen gives it (about eleven cells -- see the
             * foe probe further down), so the camera is focused onto an alien first: the band then
             * sits along the line towards the field, and the DOWN key arm at 0x11AA8 slides it the
             * rest of the way. */
            for (i32 k = 0; k < (i32)pMap->nEnemyCount; ++k) {
                if (pMap->apEnemyUnits[k] != NULL && pMap->apEnemyUnits[k]->pCell != NULL) {
                    MapView_FocusSelected(&g_world.tactEnv.focus, pMap->apEnemyUnits[k]->pCell,
                                          g_world.bMapSweepArmed, &g_world.tactEnv.cursor);
                    break;
                }
            }
            for (i = 0; i < 18 && nCand == 0; ++i) {
            for (i32 py = 20; py <= nMapBottom && nCand == 0; py += 4) {
                for (i32 px = 24; px <= 216 && nCand == 0; px += 8) {
                    MapCell *pC;
                    i32 nCx, nCy, nCz;

                    World_TactPointerMove(&g_world, &ptr, px, py, false);
                    nCx = g_world.tactEnv.cursor.nX;
                    nCy = g_world.tactEnv.cursor.nY;
                    nCz = (i32)g_world.tactEnv.cursor.nFlags - 1;
                    if (nCx < nHoverMinX) { nHoverMinX = nCx; }
                    if (nCx > nHoverMaxX) { nHoverMaxX = nCx; }
                    if (nCy < nHoverMinY) { nHoverMinY = nCy; }
                    if (nCy > nHoverMaxY) { nHoverMaxY = nCy; }
                    if (pSel->pCell == NULL
                        || (nCx == pSel->pCell->nX && nCy == pSel->pCell->nY
                            && nCz == (i32)pSel->pCell->nZ)) {
                        continue; /* the unit's own cell is the select arm (0x151A0), not a walk */
                    }
                    pC = FrameCellAt(nCx, nCy, nCz);
                    if (pC == NULL || pC->pFloorSpr == NULL || pC->pUnit != NULL) {
                        continue;
                    }
                    if (MapCell_TestFlag(pC, CELLFLAG_IS_BLANK)) {
                        continue; /* 0x2b84c: selector-0 true and the fill never starts */
                    }
                    if (!Path_ComputeReachable(&g_world.sTactPathScratch,
                                               pSel->pCell->nX, pSel->pCell->nY, pSel->pCell->nZ,
                                               pSel->bFacing, nCx, nCy, nCz,
                                               (pSel->nUnitType < PUFO_UNIT_TYPE_COUNT)
                                                   && g_aUnitTypeFlag0[pSel->nUnitType] != 0,
                                               FrameCellLookup, NULL, NULL)) {
                        continue;
                    }
                    /* The cell must also be one the traced arc can shoot an alien from. The base is
                     * walled, and the arc stops at the first blocking voxel (0x30184, where every
                     * blocked path returns 0), so a shot fired from inside the base stops at the wall
                     * one cell out of the muzzle. Walking to a cell with a clear line is what a player
                     * does, and it is the only way the burst can reach a live alien. */
                    if (!FrameShotSpot(pSel->pCell, pSel, nCx, nCy, nCz, &nSpotFoe)) {
                        continue;
                    }
                    aCandX[0] = nCx;
                    aCandY[0] = nCy;
                    aCandZ[0] = nCz;
                    nPenX = px;
                    nPenY = py;
                    k     = 0;
                    pTarget = pC;
                    ++nCand;
                }
            }
                if (nCand == 0) {
                    World_TactKeyDown(&g_world, PUFO_KEY_DOWN);   /* 0x11AA8: ++Map_nCamScrollY */
                    World_TactKeyUp(&g_world, PUFO_KEY_DOWN);
                }
            }
            printf("  walk target: cell %d/%d/%d at pen %d/%d; hover reached cells x %d..%d y %d..%d\n",
                   nCand != 0 ? aCandX[0] : -1, nCand != 0 ? aCandY[0] : -1,
                   nCand != 0 ? aCandZ[0] : -1, nPenX, nPenY,
                   nHoverMinX, nHoverMaxX, nHoverMinY, nHoverMaxY);
            Check(nCand > 0, "a pen pixel hovers an empty floor cell the mission's own search can reach");
            printf("  hover probe: %d candidates, pen %d/%d, cursor now %d/%d flags %d\n",
                   nCand, nPenX, nPenY, g_world.tactEnv.cursor.nX, g_world.tactEnv.cursor.nY,
                   (int)g_world.tactEnv.cursor.nFlags);
            printf("  input mirror: screen %d, dims %d/%d, scroll %d/%d, layer %d, edge %d, half %d, commit %d, input on %d, slot3 %d\n",
                   (int)g_world.sTactInput.nUiScreen, g_world.sTactInput.nMapDimX,
                   g_world.sTactInput.nMapDimY, g_world.sTactInput.nCamScrollX,
                   g_world.sTactInput.nCamScrollY, (int)g_world.sTactInput.nLayerScratch,
                   (int)g_world.sTactInput.bEdgeTestOn, (int)g_world.sTactInput.bNoHalfCursor,
                   (int)g_world.sTactInput.bClickCommits, (int)g_world.sTactInput.bPlayerInputOn,
                   (int)g_world.sTactInput.nUiSlot3);
            if (nPenX >= 0) {
                for (i = 0; i < PUFO_TERRAIN_CELL_SLOTS; ++i) {
                    MapCell *pC = g_world.apTactCells[i];

                    if (pC != NULL && pC->nX == aCandX[k] && pC->nY == aCandY[k] && pC->nZ == aCandZ[k]) {
                        pTarget = pC;
                        break;
                    }
                }
            }
        }
        Check(nPenX >= 0 && pTarget != NULL,
              "a pen pixel in the map region hovers the cursor exactly onto a walk candidate");

        if (nPenX >= 0 && pTarget != NULL) {
            u8  bLatchSave3 = pMap->bActionLatch;
            MapCell *pBelow = NULL;

            for (i = 0; i < PUFO_TERRAIN_CELL_SLOTS; ++i) {
                MapCell *pC = g_world.apTactCells[i];

                if (pC != NULL && pC->nX == pTarget->nX && pC->nY == pTarget->nY && pC->nZ == 0) {
                    pBelow = pC;
                    break;
                }
            }
            printf("    walk fixture: cand %d/%d/%d floor %d ceil %d occupant %d flags44 %02X | "
                   "below %s ceil %d ceilparam %d occupant %d | type %u flag0 %d unitflags %02X\n",
                   pTarget->nX, pTarget->nY, (int)pTarget->nZ, pTarget->pFloorSpr != NULL,
                   pTarget->pCeilSpr != NULL, pTarget->pUnit != NULL, (unsigned)pTarget->bFlags44,
                   pBelow != NULL ? "cell" : "none",
                   pBelow != NULL && pBelow->pCeilSpr != NULL,
                   (pBelow != NULL && pBelow->pCeilSpr != NULL) ? pBelow->pCeilSpr->iCeilParam : 0,
                   pBelow != NULL && pBelow->pUnit != NULL, (unsigned)pSel->nUnitType,
                   (pSel->nUnitType < PUFO_UNIT_TYPE_COUNT) ? (int)g_aUnitTypeFlag0[pSel->nUnitType] : -1,
                   (unsigned)pSel->bFlags);
            fflush(stdout);
            i32 nCellX0 = pSel->pCell->nX, nCellY0 = pSel->pCell->nY;

            pMap->bActionLatch = 0; /* the resume already stands latch 0; assert the walk arm */
            uTuSel0 = pSel->uTu;

            World_TactPointerMove(&g_world, &ptr, nPenX, nPenY, true);  /* the press moves the cursor */
            printf("    press1: cursor %d/%d flags %d slot3 %d commit %d edge %d latch %d clicks %d\n",
                   g_world.tactEnv.cursor.nX, g_world.tactEnv.cursor.nY,
                   (int)g_world.tactEnv.cursor.nFlags, (int)g_world.sTactInput.nUiSlot3,
                   (int)g_world.sTactInput.bClickCommits, (int)g_world.sTactInput.bEdgeTestOn,
                   (int)pMap->bActionLatch, (int)g_seams.nMapClicks);
            fflush(stdout);
            World_TactPointerUp(&g_world, &ptr);
            World_TactPointerMove(&g_world, &ptr, nPenX, nPenY, true);  /* 0x144CC: a press that did not move it commits */
            printf("    press2: cursor %d/%d flags %d slot3 %d latch %d clicks %d qlen %d qact %d\n",
                   g_world.tactEnv.cursor.nX, g_world.tactEnv.cursor.nY,
                   (int)g_world.tactEnv.cursor.nFlags, (int)g_world.sTactInput.nUiSlot3,
                   (int)pMap->bActionLatch, (int)g_seams.nMapClicks,
                   (int)pQ->nQueueLen, (int)pQ->bQueueActive);
            fflush(stdout);
            World_TactPointerUp(&g_world, &ptr);
            printf("    after up: cursor %d/%d flags %d slot3 %d latch %d clicks %d qlen %d qact %d\n",
                   g_world.tactEnv.cursor.nX, g_world.tactEnv.cursor.nY,
                   (int)g_world.tactEnv.cursor.nFlags, (int)g_world.sTactInput.nUiSlot3,
                   (int)pMap->bActionLatch, (int)g_seams.nMapClicks,
                   (int)pQ->nQueueLen, (int)pQ->bQueueActive);
            fflush(stdout);

            {
                i32 nSteps = 0;

                for (i = 0; i < (i32)pQ->nQueueLen; ++i) {
                    if (pQ->aQueue[i].nCmd == TACTCMD_STEP) {
                        ++nSteps;
                    }
                }
                Check((i32)pQ->nQueueLen > 0 && pQ->bQueueActive != 0 && nSteps > 0,
                      "Tact_HandleMapClick's latch-0 arm reached Unit_PathToCell @ 0x2c2a8: turn, steps and end mark in the queue");
                printf("  walk queue: len %d, cursor %d, %d step entries\n",
                       (i32)pQ->nQueueLen, (i32)pQ->nQueueCursor, nSteps);
            }

            for (t = 0; t < 8000 && (pQ->nQueueLen != 0 || pQ->bQueueActive != 0); ++t) {
                PUMP();
            }
            Check(pQ->nQueueLen == 0 && pSel->pCell->nX == pTarget->nX && pSel->pCell->nY == pTarget->nY,
                  "the walkers moved the soldier to the clicked cell through ProcessMoveAction's step arm @ 0x4f68c");
            Check(pSel->uTu < uTuSel0, "the steps charged time units");
            printf("  the walk: %d/%d -> %d/%d, uTu %u -> %u\n", nCellX0, nCellY0,
                   pSel->pCell->nX, pSel->pCell->nY, (unsigned)uTuSel0, (unsigned)pSel->uTu);
            ClearSurface();
            World_TactFrame(&g_world);
            DumpFrame("tact_play_moved");
            pMap->bActionLatch = bLatchSave3;
        }

        /* 3) a shot the targeting chain enqueues. BeginMultiTarget arms the latch; the double
         * press on an enemy cell is case 3; the look control's installed handler confirms the
         * burst into command 14. */
        if ((i32)pMap->nEnemyCount > 0 && pSel->pCell != NULL) {
            Soldier *pFoe = pMap->apEnemyUnits[0];

            /* The pen hovers the cell at the cursor's own layer (PointerScreen0's camera arm
             * maps through nLayerScratch), so prefer an alien standing on the shooter's layer --
             * and above that, the alien the walk target was chosen for: nSpotFoe is the alien the
             * cell the squad member walked to has a clear line to. */
            pFoe = NULL;
            if (nSpotFoe >= 0 && nSpotFoe < (i32)pMap->nEnemyCount) {
                Soldier *pE = pMap->apEnemyUnits[nSpotFoe];

                if (pE != NULL && pE->pCell != NULL && pE->uHp != 0
                    && pE->pCell->nZ == pSel->pCell->nZ) {
                    pFoe = pE;
                }
            }
            if (pFoe == NULL) {
                for (i = 0; i < (i32)pMap->nEnemyCount; ++i) {
                    Soldier *pE = pMap->apEnemyUnits[i];

                    if (pE != NULL && pE->pCell != NULL && pE->uHp != 0
                        && pE->pCell->nZ == pSel->pCell->nZ) {
                        pFoe = pE;
                        break;
                    }
                }
            }
            if (pFoe == NULL) {
                for (i = 0; i < (i32)pMap->nEnemyCount; ++i) {
                    Soldier *pE = pMap->apEnemyUnits[i];

                    if (pE != NULL && pE->pCell != NULL && pE->uHp != 0) {
                        pFoe = pE;
                        break;
                    }
                }
            }
            for (i = 0; i < (i32)pMap->nEnemyCount; ++i) {
                Soldier *pE = pMap->apEnemyUnits[i];

                if (pE == NULL || pE->pCell == NULL) {
                    continue;
                }
                printf("    foe %d: type %u cell %d/%d/%d hp %u (shooter layer %d)\n",
                       i, (unsigned)pE->nUnitType, pE->pCell->nX, pE->pCell->nY,
                       (int)pE->pCell->nZ, (unsigned)pE->uHp, (int)pSel->pCell->nZ);
            }
            Check(pFoe != NULL && pFoe->pCell != NULL, "an alien stands on the field to shoot");

            /* No cargo reached this squad (the fixture craft carries none -- the cargo-chain DATA
             * gap recorded in PLAN), so the save holds empty hands and Tact_BeginMultiTarget @ 0x18380
             * dereferences Map.pSelWeapon for the ammo test. Plant a stand-in weapon the same way
             * Part 76's screen-3 fixture does: a sort-key-1, category-3 item with research gate 1
             * unlocked, nRefId = none so the save writes it as an absent slot. The hand menu pad 17
             * stores it into Map.pSelWeapon at 0x17060 before its gates run. */
            {
                static TactItem aShotItem;
                static ObjectRecFull aShotObj;
                i32 nGate = g_abSlotTopicGate[1];

                memset(&aShotItem, 0, sizeof(aShotItem));
                memset(&aShotObj, 0, sizeof(aShotObj));
                aShotItem.pObj   = &aShotObj;
                aShotItem.nAmmo  = 8;
                aShotItem.nRefId = PUFO_SAVE_ID_NONE;
                aShotObj.uSortKey  = 1;
                aShotObj.nCategory = 3;
                /* Combat_DirectUnitDamage @ 0x2f44c reads uFirePower for the whole hit:
                 * power - armour byte, then minus rand(power / 2). A zeroed record therefore
                 * resolved a direct hit as zero damage -- the burst looked fired but hurt nobody.
                 * A rifle-class power that punches through an alien's armour points. */
                aShotObj.uFirePower = 120;
                g_world.aTopics[nGate].bUnlocked = 1;
                pSel->pHandL = &aShotItem;
                CLICK(17); /* Tact_OpenLeftHandMenu @ 0x17030: stores the hand slot at 0x17060 */
            }
            /* The hand menu left the tactical screen on screen 3 (0x17108: the action-mode scan writes
             * Map_nUiScreen = 3). Input_OnPointer's switch sends a screen-3 pen to the twelve action
             * controls (0x14014), never to the map arm at 0x143D0, so the cursor could not be moved onto
             * a cell while that screen was up. The menu's own close is its release tail:
             * PointerUpScreen3 runs the twelve releases, keeps the latch unless the mode is 0..2/8/11/12,
             * and at 0x15028..0x1505C drops the screen back to 0 for every mode but 3 and 8 -- mode 12 is
             * what 0x17104 left there. A press and release over an empty stretch of the map does exactly
             * that: nothing is under the pen, so no control fires and the tail is all that runs. */
            World_TactPointerMove(&g_world, &ptr, 24, 20, true);
            World_TactPointerUp(&g_world, &ptr);
            CLICK(39); /* Tact_BeginMultiTarget @ 0x18380 */
            printf("  after pad 39: latch %d, mode %d, screen %d, weapon %d, hands %d/%d\n",
                   (int)pMap->bActionLatch, (int)g_world.nMapActionMode, (int)g_world.nUiScreen,
                   pMap->pSelWeapon != NULL, pSel->pHandL != NULL, pSel->pHandR != NULL);
            Check(pMap->bActionLatch == 3u, /* PUFO_ACTION_LATCH_TARGETING @ pufo_tactui.h:74 */
                  "the targeting pad arms latch 3 (0x18380)");

            nPenX = -1;
            /* The focus centres the camera, not the pen's reach. MapView_FocusOnUnit @ 0x2c758 runs
             * MapView_SetCamOffset(Map+0x30 >> 1, Map+0x32 >> 1, 0) -- the size pair is 240/320
             * (Map_InitFacingScratch @ 0x26f50), so the virtual pen it stamps is at the screen's far
             * corner, and Map_CenterOffsetX/Y then anchor the camera at cell MINUS that scratch pair.
             * The pen, on the other hand, can only hover the band the real screen gives it: 0x143EC
             * halves the pen coordinates (unk_1139EE @ Map+0x3E has ONE reference, 0x143D0, and no
             * writer anywhere, so the halving always runs), 0x27714 adds 24 per layer, and
             * 0x27744/0x2777c divide them down to the two scratch bytes -- about eleven cells of
             * coverage. A cell the focus anchored past that band is out of the pen's reach until the
             * view is scrolled, which is exactly what the player does: on screen 0 with an idle queue the
             * DOWN key arm at 0x11AA8 adds 1 to Map_nCamScrollY, and both iso offsets (0x26f88, 0x26fa8)
             * read that value with a +1, so the hoverable band slides diagonally. Focus, scroll with the
             * game's own key, then hover. */
            MapView_FocusSelected(&g_world.tactEnv.focus, pFoe->pCell, g_world.bMapSweepArmed,
                                  &g_world.tactEnv.cursor);
            {
                const PadImage *pHud = g_world.apTactPads[PUFO_UIPAD_HUD];
                i32 nMapBottom = (pHud != NULL) ? (i32)pHud->nOriginY - 20 : 240;
                i32 nScrollKeys = 0;

                for (i = 0; i < 10 && nPenX < 0; ++i) {
                    for (i32 py = 20; py <= nMapBottom && nPenX < 0; py += 4) {
                        for (i32 px = 24; px <= 216 && nPenX < 0; px += 8) {
                            World_TactPointerMove(&g_world, &ptr, px, py, false);
                            if (g_world.tactEnv.cursor.nX == pFoe->pCell->nX
                                && g_world.tactEnv.cursor.nY == pFoe->pCell->nY
                                && (i32)g_world.tactEnv.cursor.nFlags == pFoe->pCell->nZ + 1) {
                                nPenX = px;
                                nPenY = py;
                            }
                        }
                    }
                    if (nPenX < 0) {
                        World_TactKeyDown(&g_world, PUFO_KEY_DOWN);   /* 0x11AA8: ++Map_nCamScrollY */
                        World_TactKeyUp(&g_world, PUFO_KEY_DOWN);
                        ++nScrollKeys;
                    }
                }
                printf("  foe probe: alien at %d/%d/%d, %d scroll keys, pen %d/%d, cursor now %d/%d flags %d\n",
                       pFoe->pCell->nX, pFoe->pCell->nY, pFoe->pCell->nZ, nScrollKeys, nPenX, nPenY,
                       g_world.tactEnv.cursor.nX, g_world.tactEnv.cursor.nY,
                       (int)g_world.tactEnv.cursor.nFlags);
            }
            Check(nPenX >= 0, "a pen pixel hovers the cursor onto the alien's cell");

            if (nPenX >= 0) {
                World_TactPointerMove(&g_world, &ptr, nPenX, nPenY, true);
                World_TactPointerUp(&g_world, &ptr);
                World_TactPointerMove(&g_world, &ptr, nPenX, nPenY, true);
                World_TactPointerUp(&g_world, &ptr);
            }
            Check(g_world.sTactLookCtl.bEnabled != 0 || pSel->nMultiTarget > 0,
                  "case 3 (0x152E4) took the click: the look control is up and the target list carries the alien");

            /* The look control's installed handler IS TactAction_ConfirmFireBurst @ 0x18774 --
             * press it the way the pen's hit test would (0x140A0 first test in PointerScreen0). */
            if (pMap->pSelWeapon == NULL) {
                CLICK(18); /* the right-hand menu stores the unit's own item at 0x17060 */
                printf("  after pad 18: weapon %d, screen %d, latch %d, mode %d\n",
                       pMap->pSelWeapon != NULL, (int)g_world.nUiScreen, (int)pMap->bActionLatch,
                       (int)g_world.nMapActionMode);
            }
            Check(pMap->pSelWeapon != NULL, "the shooter has a selected weapon (Map.pSelWeapon @ 0x113A00)");
            if (g_world.sTactLookCtl.pfnClick != NULL && pMap->pSelWeapon != NULL) {
                g_world.sTactLookCtl.pfnClick();
            }
            for (i = 0; i < (i32)pQ->nQueueLen; ++i) {
                if (pQ->aQueue[i].nCmd == TACTCMD_FIRE_SHOT) {
                    ++nShotQueued;
                }
            }
            Check(nShotQueued > 0, "ConfirmFireBurst @ 0x18774 -> TactState_EnqueueFireBurst @ 0x4b080: command 14 stands in the queue");

            for (k = 0; k < (i32)pMap->nEnemyCount; ++k) {
                aEHp0[k] = (pMap->apEnemyUnits[k] != NULL) ? pMap->apEnemyUnits[k]->uHp : 0u;
            }
            uHpSel0      = pSel->uHp;
            nEnemyShot0  = (i32)pMap->nEnemyCount;

            ClearSurface();
            World_TactFrame(&g_world);
            DumpFrame("tact_play_aim");
            for (t = 0; t < 8000 && (pQ->nQueueLen != 0 || pQ->bQueueActive != 0); ++t) {
                if (t == 3) {
                    ClearSurface();
                    World_TactFrame(&g_world); /* the burst mid-flight: the aim samples the fire processor wrote */
                    DumpFrame("tact_play_shot_mid");
                }
                (void)World_GameTickOnce(&g_world);
                World_TactFrame(&g_world);
            }
            for (k = 0; k < (i32)pMap->nEnemyCount; ++k) {
                if (pMap->apEnemyUnits[k] != NULL && pMap->apEnemyUnits[k]->uHp < aEHp0[k]) {
                    ++nHpDropped;
                }
            }
            Check(nHpDropped > 0 || (i32)pMap->nEnemyCount < nEnemyShot0,
                  "the burst resolved: Unit_ApplyCellDamage @ 0x2e5d0 took HP off a real alien, or the alien left the field");
            printf("  the shot: %d field alien(s) lost HP, shooter hp %u -> %u, queue len back to %d\n",
                   nHpDropped, (unsigned)uHpSel0, (unsigned)pSel->uHp, (i32)pQ->nQueueLen);
            ClearSurface();
            World_TactFrame(&g_world);
            DumpFrame("tact_play_shot");
        }

        /* 4) the aliens' own turn: something moves or something bleeds. */
        for (k = 0; k < (i32)pMap->nEnemyCount; ++k) {
            aEXHp0[k] = (pMap->apEnemyUnits[k] != NULL && pMap->apEnemyUnits[k]->pCell != NULL)
                          ? pMap->apEnemyUnits[k]->uHp : 0u;
            aEX0[k] = (pMap->apEnemyUnits[k] != NULL && pMap->apEnemyUnits[k]->pCell != NULL)
                        ? pMap->apEnemyUnits[k]->pCell->nX : -999;
            aEY0[k] = (pMap->apEnemyUnits[k] != NULL && pMap->apEnemyUnits[k]->pCell != NULL)
                        ? pMap->apEnemyUnits[k]->pCell->nY : -999;
        }
        uHpSel1     = (pSel->pCell != NULL) ? pSel->uHp : 0u;
        uStSel1     = pSel->bStatus;
        nEnemyAct0  = (i32)pMap->nEnemyCount;
        CLICK(5);
        for (t = 0; t < 8000 && pMap->tact.bBusy != 0; ++t) {
            PUMP();
        }
        for (k = 0; k < (i32)pMap->nEnemyCount; ++k) {
            if (pMap->apEnemyUnits[k] != NULL && pMap->apEnemyUnits[k]->pCell != NULL
                && (pMap->apEnemyUnits[k]->pCell->nX != aEX0[k]
                    || pMap->apEnemyUnits[k]->pCell->nY != aEY0[k])) {
                ++nEnemyMoved;
            }
        }
        Check(nEnemyMoved > 0 || pSel->uHp != uHpSel1 || pSel->bStatus != uStSel1
                  || (i32)pMap->nEnemyCount < nEnemyAct0,
              "Tact_EnemyAiTick put the aliens in motion or into fire this turn: cells changed, or the squad member was hit, or an alien left the field");
        printf("  enemy turn 2: %d aliens changed cell, squad member hp %u\n", nEnemyMoved, (unsigned)pSel->uHp);
        ClearSurface();
        World_TactFrame(&g_world);
        DumpFrame("tact_play_enemyturn");

        /* 5) save the mission mid-fight and resume from it. */
        memset(&saveHooks, 0, sizeof(saveHooks));
        saveHooks.pfnRenderFrame = RecSaveRenderFrame;
        g_nSaveRenders = 0;
        Check(World_SaveSlot(&g_world, "", 64, &saveHooks) != 0,
              "World_SaveSlot writes the live mission to slot 64 (SaveGame @ 0x44988)");
        Check(DatFile_Exists("sav64.sav"), "sav64.sav stands on disk");
        {
            bool bTact = false, bMap = false, bTeam = false, bAliens = false, bHidden = false;

            DatFile_ctor(&dat);  /* the slot 8080 block declared; ctor is what gives nBufCap/pWriteBuf
                                  * their defined values. DatFile_Open frees the old write buffer at
                                  * 0x9f1d6 -- an unconstructed struct freed its garbage pointer there. */
            if (DatFile_Open(&dat, "sav64.sav", PUFO_DAT_READ, 0)) {
                bTact   = DatFile_FindChunk(&dat, PUFO_SAVE_TAG_TACTVER);
                bMap    = DatFile_FindChunk(&dat, PUFO_SAVE_TAG_MAP);
                bTeam   = DatFile_FindChunk(&dat, PUFO_SAVE_TAG_TEAMUNITS);
                bAliens = DatFile_FindChunk(&dat, PUFO_SAVE_TAG_ALIENS);
                bHidden = DatFile_FindChunk(&dat, PUFO_SAVE_TAG_HIDDENSOL);
                (void)DatFile_Close(&dat);
            }
            Check(bTact && bMap && bTeam && bAliens && bHidden,
                  "the save carries the mission's own chunks: the map, the team roster, the aliens, the hidden park");
        }
        {
            i32 nCellX = pSel->pCell->nX, nCellY = pSel->pCell->nY, nCellZ = pSel->pCell->nZ;
            u16 uHp = pSel->uHp;
            i32 nFoeCellX = -999, nFoeCellY = -999;
            u16 uFoeHp = 0;

            if (pMap->nEnemyCount > 0 && pMap->apEnemyUnits[0] != NULL && pMap->apEnemyUnits[0]->pCell != NULL) {
                nFoeCellX = pMap->apEnemyUnits[0]->pCell->nX;
                nFoeCellY = pMap->apEnemyUnits[0]->pCell->nY;
                uFoeHp    = pMap->apEnemyUnits[0]->uHp;
            }
            Check(World_LoadSlot(&g_world, "", 64, &g_assets, &g_fontSys, &g_options, &hooks) != 0,
                  "load 64 resumes the mission from its own save (the tact flag made SaveGame write the mission block)");
            pMap = g_world.pTactMap;
            pQ   = g_world.pTactState;
            Check(pMap != NULL && pMap->pSelUnit != NULL && pMap->pSelUnit->pCell != NULL
                      && pMap->pSelUnit->pCell->nX == nCellX && pMap->pSelUnit->pCell->nY == nCellY
                      && pMap->pSelUnit->pCell->nZ == nCellZ,
                  "the selected soldier stands back on the cell the walk left her on");
            Check(pMap->pSelUnit->uHp == uHp, "and with the same HP the fight left her with");
            if (nFoeCellX != -999) {
                bool bFound = false;

                for (k = 0; k < (i32)pMap->nEnemyCount; ++k) {
                    if (pMap->apEnemyUnits[k] != NULL && pMap->apEnemyUnits[k]->pCell != NULL
                        && pMap->apEnemyUnits[k]->pCell->nX == nFoeCellX
                        && pMap->apEnemyUnits[k]->pCell->nY == nFoeCellY) {
                        bFound = (pMap->apEnemyUnits[k]->uHp == uFoeHp);
                        break;
                    }
                }
                Check(bFound, "the shot alien comes back on its cell with the HP the burst left it with");
            }
            printf("  resume 64: %d xcom, %d enemies on the field, selected at %d/%d/%d hp %u\n",
                   (i32)pMap->nXcomCount, (i32)pMap->nEnemyCount, pMap->pSelUnit->pCell->nX,
                   pMap->pSelUnit->pCell->nY, pMap->pSelUnit->pCell->nZ, (unsigned)pMap->pSelUnit->uHp);
            for (i = 0; i < (i32)pMap->nXcomCount; ++i) {
                if (pMap->apXcomUnits[i] != NULL) {
                    (void)Unit_UpdateVisibility(pMap->apXcomUnits[i], &g_world.tactEnv.vis);
                }
            }
            ClearSurface();
            World_TactFrame(&g_world);
            DumpFrame("tact_play_resumed");
        }

        /* Part 103.3: snapshot this live mission into slot 63. The patched-dims load tests at the tail
         * of main run on this file; nothing downstream needs the file itself -- the mission stays live
         * here, and Part 99 stage 0 is what leaves it. Snap the selected soldier's cell: the 50-row walk
         * over a 5-row dims save has to put her back on exactly that cell. */
        Check(World_SaveSlot(&g_world, "", 63, &hooks) != 0, "slot 63 snapshots the live mission");
        g_nSnap63X = pMap->pSelUnit->pCell->nX;
        g_nSnap63Y = pMap->pSelUnit->pCell->nY;
        g_nSnap63Z = pMap->pSelUnit->pCell->nZ;

        (void)remove("sav64.sav");
        (void)remove("sav62.sav");
#undef CLICK
    }

    /* ---- Part 99: the economy round on a live geoscape ------------------------------------------
     *
     * Everything the run has gated so far gated one screen's machinery at a time. This block walks
     * the economic loop the shipped game runs it in: end the mission, buy a building and pay for it,
     * run a research topic to its end on the hourly tick, manufacture the item that research
     * unlocked and pay for the production, buy at the shop and pay for it, and arm a transport and a
     * combat craft. The clock calls are the clock's own: pfnTickBases finishes a facility the way
     * Geoscape_AdvanceTime does (0x5CE40 -> 0x5D3BC -> 0x62824), and pfnTickHourly is the tick that
     * carries research progress (Geoscape_TickHourly 0x...: pfnAccumulateResearch @ 0x74914) and the
     * manufacturing refresh with its tail commit (pfnRefreshBaseTabs @ 0x70ebc tail-calling 0x71048).
     * No test has ever run pfnTickHourly before -- the presence check at 1298 was the only one. */
    {
        i32 nEBase, nRow, nTick, nKind, nTopic, nProd, nIdx, nIx;
        i32 nFunds0, nCost0, nTapX, nTapY, nAimX, nAimY;
        UiPanel *pPanel;

        /* 99.0 Leaving the mission. World_TactEscapeToGeoscape is the seam UiScreen_OnCancel @ 0x16200
         * reaches through TactInputHooks.pfnShutdownReinit: Tact_Shutdown(0) keeping nothing, then
         * Geoscape_InitUI(1) rebuilding the geoscape singletons the launch freed (0x162BC / 0x162C8),
         * which re-runs World_WireGeoscapeFrame and re-seats every env this block then presses
         * through. The UI byte stays 13 in the original at this arm (the debrief walk is the tact
         * UI's own chain, gated in test_tactend); the geoscape seams below never read it, and
         * clearing it states the plain-geoscape condition the frames show. */
        printf("  econ stage 0: leaving the mission\n");
        fflush(stdout);
        World_TactEscapeToGeoscape();
        printf("  econ stage 0b: shutdown+rebuild returned\n");
        fflush(stdout);
        g_world.nUiScreen = 0;
        Check(g_world.bTactFlag == 0, "the mission is over: bTactFlag is clear after the shutdown+rebuild (0x162BC)");
        /* The mission-end walk leaves the nav bar down: the CloseUI case 1 teardown frees the fifteen
         * controls at 0x159888..0x1598AC (0x8BEA8, 0x8BEDC, 0x8BF0C), and InitUI mode 1 does not rebuild
         * them -- its callers are Game_StartNew (0x9542C), Base_EnterBuildMode (0x84E6C) and
         * Ui_OpenResearch (0x84CBC). The geoscape's build widget -- the root control at 0x186520, whose
         * handler InitUI installs at 0x865AC -- is what a player presses to get the bar back, and
         * pressing it here is that same act. */
        Check(g_world.ui.apRootCtl[10] != NULL && g_world.ui.apRootCtl[10]->pfnClick != NULL,
              "the build widget carries Base_EnterBuildMode (Geoscape_InitUI 0x865AC)");
        g_world.ui.apRootCtl[10]->pfnClick(); /* sample 17 at 0x84E2C, the id at 0x84E64, the bar at 0x84E6C */
        CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "the build widget writes the id 1 (0x84E64)");
        Check(g_world.nav.apNav[6] != NULL && g_world.nav.apNav[6]->pfnClick != NULL
                  && g_world.nav.apNav[8] != NULL && g_world.nav.apNav[8]->pfnClick != NULL
                  && g_world.nav.apNav[9] != NULL && g_world.nav.apNav[9]->pfnClick != NULL,
              "Base_EnterBuildMode rebuilt the nav bar with the manufacturing, buy and sell handlers (0x84E6C, 0x67BC8, 0x67BD4, 0x67BE0)");
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("econ_after_mission");

        printf("  econ stage 1: facilities\n");
        fflush(stdout);
        nEBase = 0;
        g_world.nSelectedBase = nEBase;
        g_world.nFunds = 5000000; /* the three purchases and the production all have to be affordable */

        /* The earlier soldier-equip pass ran the hangar equipment row, and its new-equipment arm
         * subtracts from the base without any stock test (BaseRec_AddItemQty -1 at 0x61B04) -- the
         * original's own behaviour, reproduced. Two such presses left the equipment object wrapped
         * to 65534 in this base's stock, and Base_CalcStorageUsed (0x6555c) charges the engineers
         * gate of 0x6E300 with it. The economic round starts from clean stock, as a fresh game's
         * would be. */
        g_assets.apObjects[25]->auPerBaseVal[nEBase] = 0;

        /* 99.1 Buying a new building: the dialog, a row, an aimed cell, the commit. The sequence is
         * 12f's, kept whole: the row press's release runs the panel's handler and arms the placement
         * (0x5897C, 0x58998), the map press aims the cell under the cursor (0x58EC0, 0x58EC8), its
         * release copies the tap into the pending pair (0x89E8C, 0x89E98) and sets the ok byte
         * (0x89E84), and the second press's frame runs the overlay's commit: the funds gate at
         * 0x59A2C, SetCellBuildState at 0x59A9C, the spend at 0x59AC8. A workshop, a laboratory, a
         * stores block and a living-quarters cell where its 50 stands at or below the mission load --
         * the capacities the research, manufacturing and shop-crew gates read. Between two purchases
         * the dialog closes and re-opens the way a
         * player does it: the commit leaves the placement armed (0x89F10 then skips the row click),
         * so a new selection needs a fresh open. */
        {
            i32 aKinds[5];
            i32 nKindCount = 0;

            if (Base_CalcWorkshopCap(g_world.abFacilityGrid, nEBase) == 0) {
                aKinds[nKindCount++] = FAC_WORKSHOP;   /* 3: Base_CalcWorkshopCap's kind (0x65894) */
            }
            if (Base_CalcLabCap(g_world.abFacilityGrid, nEBase) == 0) {
                aKinds[nKindCount++] = FAC_LABORATORY; /* 2: Base_CalcLabCap's kind (0x65818) */
            }
            if (Base_CalcStoresCapacity(g_world.abFacilityGrid, nEBase) == 0) {
                aKinds[nKindCount++] = FAC_GENERAL_STORES; /* 6 */
                /* Two stores cells, not one: the mission left its recovered items in this base's
                 * stock, and the engineers budget at 0x6E300 charges their cargo against the
                 * capacity before it lets an ask rise -- one cell's 300 is below that load. */
                aKinds[nKindCount++] = FAC_GENERAL_STORES;
            }
            if (Base_CalcLivingQuartersCap(g_world.abFacilityGrid, nEBase)
                <= Base_CalcOccupiedSpace(g_world.apSoldiers, g_world.nSoldierCount,
                                          (ObjectRecFull *const *)g_assets.apObjects,
                                          (i32)g_assets.nObjectCount, nEBase)) {
                /* The shop's crew buys pay only while 0x75E60's living-quarters gate passes:
                 * outbound + occupied + 1 <= the kind-1 capacity, and the mission load already
                 * sits above one cell's 50. */
                aKinds[nKindCount++] = FAC_LIVING_QUARTERS; /* 1: Base_CalcLivingQuartersCap (0x6579C) */
            }
            if (nKindCount == 0) {
                aKinds[nKindCount++] = FAC_WORKSHOP; /* the goal's picture needs a purchase either way */
            }
            Check(nKindCount > 0, "the round buys at least one new facility at the selected base");

            g_world.bFacDone = 0; /* the new-base byte, if a BaseAt or craft-select pass left it set: the
                                   * old-base commit arm at 0x59ACC reads it, and the goal's purchase is
                                   * a facility on an existing base */
            /* Stage 12f left the build dialog open, and the mission-end seam leaves the open dialog's
             * flag and panel exactly as it found them. The original BaseFacScreen_Init tail-calls
             * BaseFacScreen_Toggle (0x5A27C), so a nav press with the dialog already open CLOSES it;
             * this pre-press is the original's own semantics, and the loop's press then re-opens the
             * screen with a fresh panel. */
            if (g_world.bFacDialogOpen != 0) {
                g_world.nav.apNav[4]->pfnClick(); /* id 7 at 0x6185C, then the closing toggle at 0x5A27C */
                CheckEqI32(g_world.bFacDialogOpen, 0, "the carried-open dialog closes on the first nav press (BaseFacScreen_Init's toggle 0x5A27C)");
            }
            for (nIx = 0; nIx < nKindCount; ++nIx) {
                nKind = aKinds[nIx];
                g_world.nav.apNav[4]->pfnClick(); /* BaseFacScreen_Open @ 0x61844 */
                CheckEqI32(g_world.screen.nGeoscapeScreen, 7, "nav slot 4 writes the build-screen id (0x61828 -> 0x61844)");
                Check(g_world.bFacDialogOpen == 1 && g_world.pFacPanel != NULL,
                      "the dialog is open with its panel (BaseFacScreen_Toggle 0x58A44)");

                pPanel = g_world.pFacPanel;
                for (nRow = 0; nRow < pPanel->nLineCount && UiPanel_GetLineAux(pPanel, nRow) != nKind; ++nRow) {
                }
                Check(nRow < pPanel->nLineCount, "the dialog lists a row for the facility kind (0x58A60)");
                if (nRow >= pPanel->nLineCount) {
                    continue;
                }

                /* UiPanel_ClickRow's gate admits only window rows 0..nStyle-1 (0x3EEE0 with the
                 * dialog's nStyle 5 at 0x58A7C), so a line below the window is reached with the
                 * dialog's down control at {11,277}: BaseFacScreen_Init installs the scroll thunk at
                 * 0x5A220 and one click moves nUnk3028 by 1 (its own limit nLineCount-nStyle). The
                 * click then lands on the window row, and 0x3EEE0 maps it back to the line through
                 * nUnk3032 = nUnk3028 + nRow. */
                while (nRow - pPanel->nUnk3028 > pPanel->nStyle - 1) {
                    PressCtl(&g_world.ctlFacDown);
                }
                nAimX = pPanel->nPosX;
                nAimY = pPanel->nPosY + pPanel->nFont * (nRow - pPanel->nUnk3028) + pPanel->nFont / 2;
                World_GeoscapePointer(&g_world, nAimX, nAimY, true);
                (void)DrawFrame();
                World_GeoscapePointerUp(&g_world);
                CheckEqI32(g_world.bArmedPlacement, 1, "the row's release arms the placement (0x5897C)");
                CheckEqI32(g_world.nPanelSel, nRow, "and stores the row into unk_158868 (0x58998)");
                CheckEqI32(UiPanel_GetLineAux(g_world.pFacPanel, g_world.nPanelSel), nKind,
                           "the stored row's aux is the facility kind the commit takes (0x58E20)");
                ClearSurface();
                (void)DrawFrame();
                DumpFrame("econ_facility_dialog");

                /* Aim the empty cell that touches a finished neighbour: the overlay's score is that
                 * neighbourhood (FacOverlay_Score), and the window needs a tap in 1..5 on both axes
                 * (0 < nDx < 192 at 0x591D4, 36 < nDy < 227 at 0x591E8 with the +36 origin 0x924D0). */
                nTapX = -1;
                nTapY = -1;
                for (nRow = 1; nRow <= 5 && nTapX < 0; ++nRow) {
                    for (nTick = 1; nTick <= 5 && nTapX < 0; ++nTick) {
                        const i32 nCell = 6 * nRow + nTick;
                        /* The score's footprint gate and its neighbour sum read the tileA lane
                         * (the 0x592C4 / 0x59320 reads), the days the buildDays lane. A cell counts as
                         * empty only when tileA is zero AND the facType sentinel is still 0xFF -- the
                         * original's own empty state (Game_StartNew's stamp 0x94904..0x9491C). */
                        const u8 *pFac = g_world.abFacilityGrid + PUFO_GRID_FAC_OFF
                                             + PUFO_GRID_FAC_STRIDE * nEBase + PUFO_GRID_FAC_TYPE_OFF;
                        const u8 *pTile = g_world.abFacilityGrid + PUFO_GRID_FAC_OFF
                                             + PUFO_GRID_FAC_STRIDE * nEBase + PUFO_GRID_TILEA_OFF;
                        const u8 *pDays = g_world.abFacilityGrid + PUFO_GRID_DAYS_STRIDE * nEBase;
                        if (pFac[nCell] != PUFO_FAC_TYPE_NONE || pTile[nCell] != 0) {
                            continue; /* the cell must still be empty in both lanes the score reads */
                        }
                        {
                            const i32 aNeigh[4][2] = { { nRow - 1, nTick }, { nRow + 1, nTick },
                                                       { nRow, nTick - 1 }, { nRow, nTick + 1 } };
                            i32 k;
                            for (k = 0; k < 4; ++k) {
                                if (aNeigh[k][0] < 0 || aNeigh[k][0] > 5 || aNeigh[k][1] < 0
                                    || aNeigh[k][1] > 5) {
                                    continue;
                                }
                                if (pFac[6 * aNeigh[k][0] + aNeigh[k][1]] != PUFO_FAC_TYPE_NONE
                                    && pTile[6 * aNeigh[k][0] + aNeigh[k][1]] != 0
                                    && pDays[6 * aNeigh[k][0] + aNeigh[k][1]] == 0) {
                                    nTapX = nTick;
                                    nTapY = nRow;
                                    break;
                                }
                            }
                        }
                    }
                }
                Check(nTapX > 0, "an empty cell next to a finished facility was found (FacOverlay_Score's neighbourhood)");
                if (nTapX <= 0) {
                    continue;
                }

                nAimX = pF->facOverlay.nOriginX + PUFO_PLA_TAP_CELL * nTapX;
                nAimY = pF->facOverlay.nOriginY + PUFO_PLA_TAP_Y0 + PUFO_PLA_TAP_CELL * nTapY;
                World_GeoscapePointer(&g_world, nAimX, nAimY, true);
                (void)DrawFrame();
                CheckEqI32(g_world.nSmapTapX, nTapX, "the held press aims the cell under the cursor (0x58EC0)");
                CheckEqI32(g_world.nSmapTapY, nTapY, "on both axes (0x58EC8)");
                World_GeoscapePointerUp(&g_world);
                CheckEqI32(g_world.nUnk15887C, nTapX, "the release copies the tap into the pending pair (0x89E8C)");
                CheckEqI32(g_world.nUnk158880, nTapY, "and sets the ok byte (0x89E84)");

                nFunds0 = g_world.nFunds;
                nCost0  = (i32)g_world.aFac[nKind].uCost; /* aBaseFacFlags[kind] + 0x18, read at 0x59AC0 */
                ClearSurface();
                World_GeoscapePointer(&g_world, nAimX, nAimY, true);
                (void)DrawFrame();
                World_GeoscapePointerUp(&g_world);
                CheckEqI32(g_world.nFunds, nFunds0 - nCost0,
                           "the commit pays the record's own cost: nFunds falls by aFac[kind].uCost (0x59AC8)");
                CheckEqI32(g_world.abFacilityGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nEBase
                                                  + PUFO_GRID_FAC_TYPE_OFF + 6 * nTapY + nTapX],
                           (i32)g_world.aFac[nKind].bFlag21,
                           "the aimed cell takes the record's kind (BaseFac_SetCellBuildState 0x59A9C)");
                Check(g_world.abFacilityGrid[PUFO_GRID_DAYS_STRIDE * nEBase + 6 * nTapY + nTapX] != 0,
                      "and its buildDays start above zero -- the cell is under construction (0x623BC)");
                ClearSurface();
                (void)DrawFrame();
                DumpFrame("econ_facility_commit");

                /* Let the clock finish the cell first: the day tick is Base_TickFacilityConstruction
                 * @ 0x62824, one decrement per call per cell, and its completion arm appends message
                 * line 38 and sets dwActionFail @ 0x1870C8 (0x62934) -- the flag that makes the NEXT
                 * pointer release return at 0x895C4 without dispatching. CloseUI's head clears the
                 * same flag (0x8B96C), so finishing before closing is the order that keeps the next
                 * iteration's row press live. */
                for (nTick = 0; nTick < 40
                     && g_world.abFacilityGrid[PUFO_GRID_DAYS_STRIDE * nEBase + 6 * nTapY + nTapX] != 0; ++nTick) {
                    g_world.geoClock.pfnTickBases(g_world.geoClock.pUser);
                }
                CheckEqI32(g_world.abFacilityGrid[PUFO_GRID_DAYS_STRIDE * nEBase + 6 * nTapY + nTapX], 0,
                           "the day ticks of 0x62824 finish the new facility (Geoscape_TickBases 0x5D3BC)");
                CheckEqI32((i32)g_world.nShortfall1870C8, 1,
                           "the completion arm leaves dwActionFail set with its message (0x62934)");

                /* Close the dialog the way the shipped game does between two purchases. The completion
                 * message's dwActionFail makes the NEXT pointer release return at 0x895C4 after clearing
                 * the flag -- PressCtl releases through World_GeoscapePointerUp, so its first press is
                 * the eaten one and reaches neither the CloseUI head nor its case 7. The second press
                 * lands on the armed arm at 0x8BFA0 and only clears bBaseFacPlaceArmed (0x8BFA4); the
                 * third reaches the dialog-open arm (0x8BFB8) and its toggle (0x8BF7C) closes it. */
                PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);
                CheckEqI32(g_world.bArmedPlacement, 1, "the completion release is the swallowed one: the arm is still set (0x895C4)");
                CheckEqI32(g_world.bFacDialogOpen, 1, "the dialog is still open");
                CheckEqI32((i32)g_world.nShortfall1870C8, 0, "and the swallowed release clears dwActionFail on its way out (0x895C4)");
                PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);
                CheckEqI32(g_world.bArmedPlacement, 0, "the next press clears the arm set by the commit (CloseUI case 7, 0x8BFA4)");
                CheckEqI32(g_world.bFacDialogOpen, 1, "and leaves the dialog open");
                PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);
                CheckEqI32(g_world.bFacDialogOpen, 0, "the third press runs the toggle and shuts the dialog (0x8BFB8 -> BaseFacScreen_Toggle 0x589ec)");
            }

            Check(Base_CalcWorkshopCap(g_world.abFacilityGrid, nEBase) >= 50,
                  "the base now has workshop capacity (one free kind-3 cell is 50, 0x65894)");
            Check(Base_CalcLabCap(g_world.abFacilityGrid, nEBase) >= 50,
                  "and laboratory capacity (one free kind-2 cell is 50, 0x65818)");
            printf("  econ: base %d has workshop %d, lab %d, stores %d capacity\n", nEBase,
                   Base_CalcWorkshopCap(g_world.abFacilityGrid, nEBase),
                   Base_CalcLabCap(g_world.abFacilityGrid, nEBase),
                   Base_CalcStoresCapacity(g_world.abFacilityGrid, nEBase));
        }

        /* 99.2 Running a research topic to its end on the hourly tick. The crew column and the
         * fixture-active topics follow the research section's own precedent: the hired crew is the
         * object record's per-base column (Base_GetScientistCount @ 0x6812c reads slot 113 of it),
         * and topics 29/36 are the two RESEARCH.DAT records 123..127 name in the activation bytes,
         * which the shipped item gate can never reach. Row select, the allocate-all and the start are
         * the presses; from there the tick does the work: ResearchScreen_AccumulateAllocated @ 0x74914
         * adds every base's allocation into wProgress, and ResearchScreen_RebuildTopicList @ 0x74314
         * closes the topic when wProgress reaches wTotal (0x743A4 -> 0x7440C bUnlocked = 1). */
        {
            const i32 nColBase = PUFO_RESEARCH_TOPIC_COUNT * nEBase;

            g_assets.apObjects[113]->auPerBaseVal[nEBase] = (u16)((i32)g_assets.apObjects[113]->auPerBaseVal[nEBase] + 2);
            g_assets.apObjects[112]->auPerBaseVal[nEBase] = (u16)((i32)g_assets.apObjects[112]->auPerBaseVal[nEBase] + 2);
            CheckEqI32(Base_GetScientistCount(g_assets.apObjects[113], g_world.auResearchCol, nEBase,
                                              (i32)g_world.nResearchEntries) >= 2 ? 2 : 0, 2,
                       "the base now has scientists (0x6812c: slot 113's column minus the allocation)");

            /* Pick a topic the shipped data can actually run. Stage 12i measured 30 records that are
             * locked with nothing booked and a real wTotal -- the ones 0x74364 lets past its gate into
             * the in-progress branch at 0x747A0 -- and 28 of those with abPreTopics and abNeedItems
             * both summing to zero, so 0x747AC/0x74808's gates open and 0x748D0 admits the record.
             * Topics 29/36 are NOT among them: their wTotal is 0, and RebuildTopicList drops a
             * wTotal == 0 record at 0x74328 before it ever books progress. The fixture arms one of
             * the 28; the completion arm at 0x74584 then switches on whatever abActivateTopics names,
             * which is how the original activates the next topics. */
            nTopic = -1;
            for (nRow = 1; nRow < (i32)g_world.nResearchEntries && nTopic < 0; ++nRow) {
                const ResearchTopicLive *pT = &g_world.aTopics[nRow];
                i32 nSum = 0;
                i32 k;
                if (pT->wTotal == 0 || pT->wProgress != 0 || pT->bUnlocked != 0) {
                    continue; /* the 0x74364 shape: locked, unbooked, with a real total */
                }
                for (k = 0; k < 5; ++k) {
                    nSum += (i32)pT->abPreTopics[k] + (i32)pT->abNeedItems[k];
                }
                if (nSum != 0) {
                    continue;
                }
                { /* the topic must feed the manufacturing list: an abItemIds slot the 0x6EA50 filter
                   * takes and whose object carries uManufWorkPerUnit @ +0xBA (0x6F3EC divides by it). */
                    i32 j;
                    i32 bProduct = 0;
                    for (j = 0; j < 5; ++j) {
                        const i32 nItem = pT->abItemIds[j];
                        if (nItem > 0 && nItem < (i32)g_assets.nObjectCount
                            && g_assets.apObjects[nItem] != NULL
                            && g_assets.apObjects[nItem]->uManufWorkPerUnit != 0) {
                            bProduct = 1;
                            break;
                        }
                    }
                    if (bProduct == 0) {
                        continue;
                    }
                }
                nTopic = nRow;
            }
            Check(nTopic > 0, "a shipped record is locked, unbooked, gate-open, with a real wTotal");
            g_world.aTopics[nTopic >= 0 ? nTopic : 29].bActive = 1;
            g_world.nav.apNav[5]->pfnClick(); /* ResearchScreen_Open @ 0x616ac */
            CheckEqI32(g_world.screen.nGeoscapeScreen, 8, "nav slot 5 writes the research id (0x6176C)");
            pPanel = g_world.pResearchTopicPanel;
            Check(pPanel != NULL && pPanel->nLineCount >= 1, "BuildPanels lists the active topics (0x73734)");
            if (nTopic >= 0) {
                const i32 nColIdx = nColBase + nTopic;
                const i32 nProgress0 = (i32)g_world.aTopics[nTopic].wProgress;

                nRow = 0;
                while (nRow < pPanel->nLineCount && UiPanel_GetLineAux(pPanel, nRow) != nTopic) {
                    ++nRow;
                }
                Check(nRow < pPanel->nLineCount, "the chosen topic stands on the screen list (0x73734)");
                nAimY = pPanel->nPosY + pPanel->nFont * nRow + pPanel->nFont / 2;
                World_GeoscapePointer(&g_world, 30, nAimY, true);
                (void)World_GeoscapePointerUp(&g_world);
                CheckEqI32(g_world.researchScratch.bFlag160C80, 1, "the row press selects the row (0x73334)");
                CheckEqI32(g_world.researchScratch.nCache160C74, nRow,
                           "and caches the row the quantity pair acts on (0x73350)");

                ClearSurface();
                (void)DrawFrame(); /* the row-selected frame installs the allocate-all (0x74184) */
                {
                    const i32 nSci0 = Base_GetScientistCount(g_assets.apObjects[113], g_world.auResearchCol,
                                                             nEBase, (i32)g_world.nResearchEntries);
                    PressCtl(&g_world.aResearchCtl[2]); /* ResearchScreen_IncreaseSelected @ 0x72ec8 */
                    CheckEqI32(g_world.auResearchCol[nColIdx], 1,
                               "the +1 allocates one scientist: lab gate open, scientist gate open (0x72F58)");
                    PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B94]); /* ResearchScreen_UpdateProgress @ 0x72d14 */
                    CheckEqI32(g_world.auResearchCol[nColIdx], nSci0,
                               "and the allocate-all runs to the scientist count it started with (0x72D94)");
                }
                {
                    /* Every press ends in RebuildTopicList, and its 5a gate activates more topics
                     * (10322), so RefreshPanels re-lists the rows and a row cached by an earlier
                     * click can name a different topic by the next press -- the player re-clicks,
                     * and so does the fixture. */
                    nRow = 0;
                    while (nRow < pPanel->nLineCount && UiPanel_GetLineAux(pPanel, nRow) != nTopic) {
                        ++nRow;
                    }
                    Check(nRow < pPanel->nLineCount, "the topic is still on the list before the start press");
                    if (nRow < pPanel->nLineCount) {
                        nAimY = pPanel->nPosY + pPanel->nFont * nRow + pPanel->nFont / 2;
                        World_GeoscapePointer(&g_world, 30, nAimY, true);
                        (void)World_GeoscapePointerUp(&g_world);
                    }
                    CheckEqI32(g_world.researchScratch.nCache160C74, nRow,
                               "the re-click caches the topic's new row (0x73350)");
                }
                {
                    const i32 nSm0 = g_nSamples;
                    PressCtl(g_world.ui.apCtl[GEOUI_CTL_186BB0]); /* ResearchScreen_StartSelected @ 0x73258 */
                    /* The topic has wTotal 1 and progress booked, so the same call's tail rebuild
                     * finishes it, and the finished branch's UiLineList_AppendWithSound @ 0x74554
                     * plays its own sample 7 right after the start's 17. */
                    Check(g_nSamples >= nSm0 + 1 && g_aFrameSamples[nSm0] == 17,
                          "the start control plays sample 17 (0x73258)");
                    Check(g_nSamples >= nSm0 + 2 && g_aFrameSamples[nSm0 + 1] == 7,
                          "the completion line append plays sample 7 too (0x74554)");
                }
                Check(g_world.aTopics[nTopic].wProgress >= nProgress0,
                      "and the topic stands with its booked progress, 0x732C8's arm on a topic that has one");
                ClearSurface();
                (void)DrawFrame();
                DumpFrame("econ_research_running");

                for (nTick = 0; nTick < 3000 && g_world.aTopics[nTopic].bUnlocked == 0; ++nTick) {
                    g_world.geoClock.pfnTickHourly(g_world.geoClock.pUser);
                }
                CheckEqI32(g_world.aTopics[nTopic].bUnlocked, 1,
                           "the hourly accumulation finishes the topic (0x74914 -> 0x7440C)");
                printf("  econ: topic %d finished after %d hourly ticks, wTotal %u\n",
                       nTopic, nTick, (unsigned)g_world.aTopics[nTopic].wTotal);
                CheckEqI32((i32)g_world.nFinishedTopic, nTopic,
                           "and nResearchFinishedTopic names it (0x74414)");
                CheckEqI32((i32)g_world.auResearchCol[nColIdx], 0,
                           "with the topic's allocation cleared across the columns (0x7442C)");
                ClearSurface();
                (void)DrawFrame();
                DumpFrame("econ_research_done");
            }
            /* The finished branch at 0x74458 also sets dwActionFail, so the next pointer release --
             * the first close press on screen 8 -- is the swallowed one: it clears the flag at
             * 0x895C4 and dispatches nothing. The press after it reaches the no-row-selected arm
             * (StartSelected had cleared the flag at 0x7324C), writes the id 1 (0x8BDC4) and runs
             * the BuildPanels teardown (0x8BDD8). */
            CheckEqI32((i32)g_world.nShortfall1870C8, 1, "the finished branch sets dwActionFail too (0x74458)");
            PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);
            CheckEqI32(g_world.bResearchBuilt, 1, "the first close press is the swallowed release (0x895C4)");
            CheckEqI32((i32)g_world.nShortfall1870C8, 0, "and it clears dwActionFail on its way out (0x895C4)");
            PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);
            CheckEqI32(g_world.bResearchBuilt, 0, "the next press closes the screen with its teardown (0x8BDD8)");
        }

        /* 99.3 Manufacturing the researched item. The product list is the unlocked topics' five item
         * slots each (ManufScreen_BuildPanels 0x6EA0C), filtered by the per-object manufacturable
         * flag, so the topic 99.2 just finished is what the list offers. The row press selects
         * (release-arm UiPanel_ClickRow on panel A, 0x8A8DC -> ManufScreen_SelectListed @ 0x6e914),
         * the engineers pair asks for a unit against the workshop/stores budget (0x6E214), the
         * allocate-all hands the hired engineers to the ask (0x6df6c), and every pfnTickHourly runs
         * ManufScreen_RefreshBaseTabs @ 0x70ebc -- the allocated hours land in the accumulated column
         * (0x70FE8) and the tail call at 0x71048 commits: units = accumulated / uManufWorkPerUnit
         * (0x6F3EC), BaseRec_AddItemQty puts the item in the base (0x6F6A4), the components leave it
         * (0x6F6C8), and the funds pay price * 1.4 (0x6F70C..0x6F788). */
        {
            const ObjectRecFull *pRec;
            const i32 aComp[4] = { 105, 106, 107, 109 }; /* OBJ_ELERIUM_115, OBJ_ALIEN_ALLOYS,
                                                           * OBJ_UFO_POWER_SOURCE, OBJ_UFO_NAVIGATION
                                                           * -- g_anManufComponentSlots @ 0x... in 0x6F57C */

            nProd = -1;
            for (nRow = 0; nRow < 5 && nProd < 0; ++nRow) {
                const i32 nItem = g_world.aTopics[nTopic >= 0 ? nTopic : 29].abItemIds[nRow];
                if (nItem > 0 && nItem < (i32)g_assets.nObjectCount
                    && g_assets.apObjects[nItem]->uManufWorkPerUnit != 0) {
                    nProd = nItem; /* a product the finished topic unlocked */
                }
            }

            g_world.nav.apNav[6]->pfnClick(); /* ManufScreen_Open @ 0x614cc, nav slot 6 (0x67BC8) */
            CheckEqI32(g_world.screen.nGeoscapeScreen, 9, "nav slot 6 writes the manufacturing id (0x614E4)");
            Check(g_world.bManufBuilt == 1 && g_world.apManufRoot[0] != NULL,
                  "Init loads the manuf pad onto the block base and its tail builds the panels (0x710E0, 0x71268)");
            pPanel = g_world.pManufProductPanel;
            Check(pPanel != NULL && pPanel->nLineCount >= 1, "the product list has rows (0x6EA0C)");

            if (nProd < 0 || pPanel == NULL) {
                nProd = (pPanel != NULL) ? UiPanel_GetLineAux(pPanel, 0) : -1; /* deviation: any offered product */
            } else {
                nRow = 0;
                while (nRow < pPanel->nLineCount && UiPanel_GetLineAux(pPanel, nRow) != nProd) {
                    ++nRow;
                }
                if (nRow >= pPanel->nLineCount) {
                    nProd = UiPanel_GetLineAux(pPanel, 0);
                }
            }
            Check(nProd >= 0 && nProd < (i32)g_assets.nObjectCount, "the screen offers a product");
            pRec = g_assets.apObjects[nProd];
            Check(pRec != NULL && pRec->uManufWorkPerUnit != 0,
                  "and it is a manufacturable object (uManufWorkPerUnit non-zero at +0xBA)");
            printf("  econ: manufacturing product %d, work per unit %u, price %u\n",
                   nProd, pRec != NULL ? (unsigned)pRec->uManufWorkPerUnit : 0u,
                   (unsigned)g_anObjectPrice[nProd]);
            nRow = 0;
            while (nRow < pPanel->nLineCount && UiPanel_GetLineAux(pPanel, nRow) != nProd) {
                ++nRow;
            }

            nAimY = pPanel->nPosY + pPanel->nFont * nRow + pPanel->nFont / 2;
            World_GeoscapePointer(&g_world, pPanel->nPosX + 10, nAimY, true);
            (void)World_GeoscapePointerUp(&g_world);
            CheckEqI32(g_world.bManufProductSelected, 1, "the row's release selects the product (0x6E968)");
            CheckEqI32(g_world.nManufProductRow, nRow, "and caches the row (0x6E94C)");

            /* Seed the components first: ManufScreen_RefreshBaseTabs sets bManufQtyGate @ 0x15EE50
             * when a component is short (0x7019C) and every engineers control bails on that gate,
             * so the stock a production needs has to stand in the base before the first draw. The
             * commit (0x6F57C) reads the same four columns and is what takes them back out. */
            for (nRow = 0; nRow < 4; ++nRow) {
                const i32 nNeed = (pRec != NULL) ? (i32)pRec->abManufComponents[nRow] : 0;
                if (nNeed > 0) {
                    BaseRec_AddItemQty(g_world.apBases[nEBase], g_assets.apObjects[aComp[nRow]], nNeed + 2);
                }
            }

            /* The row click clears bUiAnimStarted @ 0x187080 (0x6E964), and the detail panel's
             * Ui_AnimExpandBlit gate at 0x6FE00 returns before the quantity pair and the two shared
             * bottom controls are moved and re-pointed (0x70B38..0x70B94) -- the allocate-all keeps
             * the research screen's position and handler until the progress byte
             * cUiAnimProgress @ 0x187058 climbs to 5 for mode 3's 236-wide panel. The fixture runs
             * no timer, so it steps the byte as the app's WM_TIMER branch does (0xA0C88), drawing
             * between the steps. */
            for (nTick = 0; nTick < 8; ++nTick) {
                if (g_world.cUiAnimProgress < 101) {
                    ++g_world.cUiAnimProgress;
                }
                ClearSurface();
                (void)DrawFrame();
            }
            PressCtl(&g_world.aManufCtl[4]); /* ManufScreen_IncEngineers @ 0x6e13c */
            nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nEBase + nProd;
            CheckEqI32(g_world.anProductReq[nIdx], 1,
                       "the engineers + asks for one unit against the capacity (0x6E214, 0x6E31C)");
            PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B94]); /* ManufScreen_UpdateProgress @ 0x6df6c */
            Check(g_world.auManufCol[nIdx] >= 1, "and the allocate-all assigns hired engineers to it (0x6df6c)");

            ClearSurface();
            (void)DrawFrame();
            DumpFrame("econ_manuf_alloc");

            nFunds0 = g_world.nFunds;
            {
                const i32 nStock0 = (i32)g_assets.apObjects[nProd]->auPerBaseVal[nEBase];
                i32 aComp0[4];

                for (nTick = 0; nTick < 4; ++nTick) {
                    aComp0[nTick] = (i32)g_assets.apObjects[aComp[nTick]]->auPerBaseVal[nEBase];
                }
                /* The filled slot does not stay counted: the same refresh that commits runs its
                 * ClearProductSlot pass (0x710A0 -> 0x6F918/0x6F92C), zeroing the accumulator, the
                 * committed column and the ask. The stock entry and the paid components are the
                 * production's evidence, not the columns. */
                for (nTick = 0; nTick < 3000
                     && (i32)g_assets.apObjects[nProd]->auPerBaseVal[nEBase] == nStock0; ++nTick) {
                    g_world.geoClock.pfnTickHourly(g_world.geoClock.pUser);
                }
                Check(g_world.anProductReq[nIdx] == 0 && g_world.auManufCol[nIdx] == 0,
                      "the hourly refresh's commit produced the unit and its slot pass cleared the ask (0x71048 -> 0x6F5E8, 0x6F6B8, 0x710A0)");
                printf("  econ: production committed after %d hourly ticks\n", nTick);
                CheckEqI32((i32)g_assets.apObjects[nProd]->auPerBaseVal[nEBase], nStock0 + 1,
                           "the item stands in the base's stores (BaseRec_AddItemQty 0x6F6A4)");
                for (nTick = 0; nTick < 4; ++nTick) {
                    CheckEqI32((i32)g_assets.apObjects[aComp[nTick]]->auPerBaseVal[nEBase],
                               aComp0[nTick] - (pRec != NULL ? (i32)pRec->abManufComponents[nTick] : 0),
                               "and the four component slots paid for it (0x6F6C8)");
                }
                CheckEqI32(g_world.nFunds, (i32)((double)nFunds0 - (double)g_anObjectPrice[nProd] * 1.4),
                           "production pays the price times 1.4 (0x6F70C..0x6F788)");
                printf("  econ: production funds %d -> %d, price %u\n",
                       nFunds0, g_world.nFunds, (unsigned)g_anObjectPrice[nProd]);
                ClearSurface();
                (void)DrawFrame();
                DumpFrame("econ_manuf_done");
            }

            /* Closing with a product selected is the original's two-step: the first close press takes
             * case 9's selected arm, which only runs ManufScreen_Draw @ 0x8BD78 -- and that draw
             * clears the selection at 0x6E410 -- and only the second press reaches the teardown. */
            PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);
            CheckEqI32(g_world.screen.nGeoscapeScreen, 9, "the first close press draws and stays on 9 (0x8BD78)");
            CheckEqI32(g_world.bManufProductSelected, 0, "and ManufScreen_Draw clears the selection (0x6E410)");
            PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);
            CheckEqI32(g_world.bManufBuilt, 0, "the second press runs the teardown (0x8BD94 -> 0x6F264)");
            CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "and writes the id 1 (0x8BD98)");
        }

        /* 99.4 Buying at the shop. Nav slot 8 is BuySellScreen_OpenBuy @ 0x61794: the id 11 is written
         * BEFORE the shared Init @ 0x78c30 (0x617AC), which snapshots the funds (0x78C44), allocates the
         * pad (0x78C70), clears the transaction pair (0x78CAC, 0x78CB4) and the slot deltas (0x78D3C),
         * installs ConfirmSiteAndClose on the shared confirm control (0x78D60) and calls BuildControls
         * (0x78D64). Row select is the press arm's fixed-x row click on panel A (SyncSelection @
         * 0x76138); the buy press-and-release on panel C (x 196) is the release arm's row click onto
         * BuySellScreen_ValidateCapacity @ 0x75b88, which pays, queues the delta and loads the working
         * site record (0x75FBC, 0x75FA8, 0x75FF8). The confirm reaches ConfirmSiteAndClose @ 0x75090:
         * the working record becomes a real site (0x75120, 0x75124) and the shop closes through case 11
         * with its controls teardown and pad release (0x8C0D0, 0x8C0F0). The teardown re-reads the site
         * count live at 0x76C14, so AFTER a confirm it clears the next empty slot and the record just
         * confirmed survives. Smap_GrantSiteRecovery @ 0x98b50, on the geoscape hourly tick, ages the
         * record's flag 24 one step per tick (0x98B68), delivers each slot's quantity into the base's
         * stock (0x9900C), marks it delivered (0x99484) and compacts it out of the list (0x994C8) --
         * twenty-four ticks later the bought crew and weapon stand in the base. */
        {
            const i32 nSite0 = g_world.nSiteCount;

            g_world.nav.apNav[8]->pfnClick(); /* nav slot 8 (0x67BD4) */
            CheckEqI32(g_world.screen.nGeoscapeScreen, 11, "nav slot 8 writes the buy id before Init (0x617AC)");
            Check(g_world.pBuySellPad != NULL && g_world.bBuySellCtlBuilt == 1,
                  "Init allocates the shop pad (0x78C70) and its 11 arm builds the panels and controls (0x78D64)");
            ClearSurface();
            (void)DrawFrame();
            DumpFrame("econ_buysell_open");

            pPanel = g_world.pBuySellPanelA;
            Check(pPanel != NULL && pPanel->nLineCount >= 1, "the shop lists its transferable rows (0x76FB4)");
            if (pPanel != NULL && pPanel->nLineCount >= 1) {
                UiPanel *pShopA   = pPanel;
                i32 aBuyRows[4]   = { (i32)PUFO_ROW_ENGINEER, (i32)PUFO_ROW_SCIENTIST, -1, -1 };
                i32 nBuySell0;

                for (nRow = 0; nRow < pShopA->nLineCount; ++nRow) {
                    const i32 nAux = UiPanel_GetLineAux(pShopA, nRow);
                    if (nAux >= WM_CANNON && nAux <= WM_FUSION && g_anObjectPrice[nAux] > 0) {
                        aBuyRows[2] = nAux; /* a craft weapon the shop lists, bought with the crew */
                        break;
                    }
                }
                for (nRow = 0; nRow < pShopA->nLineCount; ++nRow) {
                    const i32 nAux = UiPanel_GetLineAux(pShopA, nRow);
                    if (nAux >= 36 && nAux <= 52 && g_anObjectPrice[nAux] > 0) {
                        aBuyRows[3] = nAux; /* a hold item: the hangar's staging pass (0x65B30) loads 36..52 */
                        break;
                    }
                }
                for (nIx = 0; nIx < 4; ++nIx) {
                    const i32 nSlot = aBuyRows[nIx];
                    i32 nPrice;

                    if (nSlot < 0) {
                        continue;
                    }
                    nPrice = (nSlot == (i32)PUFO_ROW_ENGINEER) ? 50000
                             : (nSlot == (i32)PUFO_ROW_SCIENTIST) ? 60000
                                                                  : (i32)g_anObjectPrice[nSlot];
                    for (nRow = 0; nRow < pShopA->nLineCount && UiPanel_GetLineAux(pShopA, nRow) != nSlot; ++nRow) {
                    }
                    Check(nRow < pShopA->nLineCount, "the shop lists a row for the slot");
                    printf("  econ: buying slot %d at the shop\n", nSlot);
                    if (nRow >= pShopA->nLineCount) {
                        continue;
                    }

                    /* Panel A lists its nStyle 14 rows out of its own scroll cursor (0x76384),
                     * and UiPanel_ClickRow only admits window rows 0..nStyle-1 (0x3EEE0), so the
                     * two paging controls Init installs at 0x76268 and 0x76274 move the window to
                     * the row (BuySellScreen_PanelPrev @ 0x76088 and PanelNext @ 0x760e0, both
                     * scrolling all five panels together). The click then maps back through
                     * nUnk3028 + row (0x3EEE0). */
                    while (pShopA->nUnk3028 > nRow) {
                        PressCtl(&g_world.aBuySellCtl[0]); /* PanelPrev */
                    }
                    while (nRow - pShopA->nUnk3028 > pShopA->nStyle - 1) {
                        PressCtl(&g_world.aBuySellCtl[1]); /* PanelNext */
                    }
                    nAimY = pShopA->nPosY + pShopA->nFont * (nRow - pShopA->nUnk3028)
                            + pShopA->nFont / 2;
                    World_GeoscapePointer(&g_world, 25, nAimY, true); /* the row click is at panel x 20 (0x88DBC) */
                    (void)World_GeoscapePointerUp(&g_world);
                    CheckEqI32(g_world.buySellScratch.nCache186BE8, nRow,
                               "the row click caches the row the columns act on (0x76148)");

                    nFunds0   = g_world.nFunds;
                    nBuySell0 = g_world.nBuySellTransTotal;
                    pPanel    = g_world.pBuySellPanelC;              /* the ">" column at x 196 (0x764A0) */
                    nAimY     = pPanel->nPosY + pPanel->nFont * (nRow - pPanel->nUnk3028)
                                + pPanel->nFont / 2;
                    World_GeoscapePointer(&g_world, pPanel->nPosX + 4, nAimY, true);
                    (void)World_GeoscapePointerUp(&g_world);         /* 0x8A4EC -> ValidateCapacity 0x75b88 */
                    CheckEqI32(g_world.nFunds, nFunds0 - nPrice,
                               "the '>' press pays for one unit of the slot (0x75FBC / 0x75D28)");
                    CheckEqI32(g_world.nBuySellTransTotal, nBuySell0 + nPrice, "the transaction total carries it (0x75FD8)");
                    CheckEqI32(UiPanel_GetLineAux(g_world.pBuySellPanelD, nRow), 1,
                               "the queue column counts the unit on panel D (0x75FDC / 0x75D84)");
                    CheckEqI32((i32)g_world.apSites[g_world.nSiteCount].auItemQty[nSlot], 1,
                               "and the working site record holds it (0x75FF8)");
                }

                ClearSurface();
                (void)DrawFrame();
                DumpFrame("econ_buysell_queued");

                {
                    const i32 nFunds1 = g_world.nFunds;
                    PressCtl(g_world.ui.apCtl[GEOUI_CTL_186BA4]);
                    CheckEqI32(g_world.nSiteCount, nSite0 + 1, "the confirm promotes the working site (0x75124)");
                    CheckEqI32((i32)g_world.apSites[nSite0].bFlag0, PUFO_SITE_FLAG_CONFIRM,
                               "its bFlag0 takes 24 (0x75120)");
                    Check(g_world.apSites[nSite0].auItemQty[PUFO_ROW_ENGINEER] >= 1
                              && g_world.apSites[nSite0].auItemQty[PUFO_ROW_SCIENTIST] >= 1,
                          "the promoted record carries the bought crew cargo");
                    CheckEqI32(g_world.nBuySellFundsSnapshot, nFunds1,
                               "and the funds snapshot now equals nFunds (0x75130)");
                    CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "case 11 closes the shop (0x8C0D8)");
                    Check(g_world.pBuySellPad == NULL && g_world.bBuySellCtlBuilt == 0,
                          "its teardown releases the controls and the pad (0x8C0D0, 0x8C0F0)");

                    /* The confirmed record waits for its transport. Smap_GrantSiteRecovery ages the
                     * countdown byte one step per geoscape hourly tick (0x98B68..0x98BAC), so the flag
                     * 24 written at 0x75120 reaches 0 at the 24th tick, and the delivery pass adds
                     * each slot's quantity to the base's stock (0x9900C), marks the record delivered
                     * (0x99484) and compacts it out of the site list (0x994C8..0x99558). The base it
                     * delivers to is the one its bFlag1 was opened with (0x761D0, matched at 0x98C18). */
                    {
                        const i32 nEng0 = (i32)g_assets.apObjects[PUFO_ROW_ENGINEER]->auPerBaseVal[nEBase];
                        const i32 nSci0 = (i32)g_assets.apObjects[PUFO_ROW_SCIENTIST]->auPerBaseVal[nEBase];
                        const i32 nWpn0 = (aBuyRows[2] >= 0)
                                              ? (i32)g_assets.apObjects[aBuyRows[2]]->auPerBaseVal[nEBase]
                                              : 0;
                        const i32 nHold0 = (aBuyRows[3] >= 0)
                                              ? (i32)g_assets.apObjects[aBuyRows[3]]->auPerBaseVal[nEBase]
                                              : 0;

                        for (nTick = 0; nTick < 25; ++nTick) {
                            g_world.geoClock.pfnTickHourly(g_world.geoClock.pUser);
                        }
                        CheckEqI32((i32)g_assets.apObjects[PUFO_ROW_ENGINEER]->auPerBaseVal[nEBase],
                                   nEng0 + 1, "the delivery pass credits the bought engineer to the base (0x9900C)");
                        CheckEqI32((i32)g_assets.apObjects[PUFO_ROW_SCIENTIST]->auPerBaseVal[nEBase],
                                   nSci0 + 1, "and the bought scientist (0x9900C)");
                        if (aBuyRows[2] >= 0) {
                            Check((i32)g_assets.apObjects[aBuyRows[2]]->auPerBaseVal[nEBase] >= nWpn0 + 1,
                                  "and the bought weapon arrives too (0x9900C; the resupply at 0x68F2c may spend some)");
                        }
                        if (aBuyRows[3] >= 0) {
                            CheckEqI32((i32)g_assets.apObjects[aBuyRows[3]]->auPerBaseVal[nEBase],
                                       nHold0 + 1, "and the bought hold item (0x9900C)");
                        }
                        CheckEqI32(g_world.nSiteCount, nSite0,
                                    "the delivered record is compacted out of the site list (0x994C8)");
                    }
                }
            }
        }

        /* 99.5 Selling one back. Nav slot 9 is BuySellScreen_OpenSell @ 0x617c4 (id 12 written first,
         * 0x617DC); Init's 12 arm installs CommitTransaction on the confirm control (0x78D70) and
         * calls BuySellScreen_BuildPanels (0x78D7C), whose fill pass lists the base's own stock --
         * panel D's aux carries the engineer count (0x77C20). The '>' press is the release arm's
         * row click onto panel C, whose handler here is BuySellScreen_FillSellPanel @ 0x75464
         * (0x774EC): the crew branch marks the sale with the slot delta at -1 (0x75750) and drops
         * the queue count by one, and refunds nothing -- the money paths at 0x757AC/0x757BC sit in
         * the plain-object branch only, so a crew sale carries no price. The confirm reaches
         * CommitTransaction @ 0x74bf8: its slot pass runs BaseRec_SubItemQty(base, item, -delta)
         * per marked slot (0x74C60) -- the sold engineer leaves the base's stores -- then the funds
         * snapshot (0x75070) and the case-12 close (0x75078). */
        {
            const i32 nStock0 = (i32)g_assets.apObjects[PUFO_ROW_ENGINEER]->auPerBaseVal[nEBase];

            g_world.nav.apNav[9]->pfnClick(); /* nav slot 9 (0x67BE0) */
            CheckEqI32(g_world.screen.nGeoscapeScreen, 12, "nav slot 9 writes the sell id before Init (0x617DC)");
            Check(g_world.bBuySellBuilt == 1 && g_world.pBuySellPanelA != NULL,
                  "Init's 12 arm builds the five panels (0x78D7C)");
            ClearSurface();
            (void)DrawFrame();
            DumpFrame("econ_buysell_sell");

            pPanel = g_world.pBuySellPanelA;
            for (nRow = 0; pPanel != NULL && nRow < pPanel->nLineCount
                 && UiPanel_GetLineAux(pPanel, nRow) != (i32)PUFO_ROW_ENGINEER; ++nRow) {
            }
            Check(pPanel != NULL && nRow < pPanel->nLineCount, "the engineer stands on the sell list");
            if (pPanel != NULL && nRow < pPanel->nLineCount) {
                i32 nBuySell1, nAux0;

                /* Same window gate as the buy side (0x3EEE0; sell panels get nStyle 13 @ 0x76384):
                 * the paging control Init installs at 0x76274 scrolls all five panels together. */
                while (nRow - pPanel->nUnk3028 > pPanel->nStyle - 1) {
                    PressCtl(&g_world.aBuySellCtl[1]);
                }
                nAimY = pPanel->nPosY + pPanel->nFont * (nRow - pPanel->nUnk3028)
                        + pPanel->nFont / 2;
                World_GeoscapePointer(&g_world, 25, nAimY, true);
                (void)World_GeoscapePointerUp(&g_world);
                CheckEqI32(g_world.buySellScratch.nCache186BE8, nRow, "the row is cached again (0x76148)");

                nFunds0   = g_world.nFunds;
                nBuySell1 = g_world.nBuySellTransTotal;
                nAux0     = UiPanel_GetLineAux(g_world.pBuySellPanelD, nRow); /* the stock seed at 0x77C20 */
                pPanel    = g_world.pBuySellPanelC;   /* sell's panel C is FillSellPanel @ 0x774EC, the column at x 180 */
                nAimY     = pPanel->nPosY + pPanel->nFont * (nRow - pPanel->nUnk3028)
                            + pPanel->nFont / 2;
                World_GeoscapePointer(&g_world, pPanel->nPosX + 4, nAimY, true);
                (void)World_GeoscapePointerUp(&g_world);            /* ClickRow(C) -> FillSellPanel 0x75464 */
                CheckEqI32(g_world.anBuySellSlotDelta[PUFO_ROW_ENGINEER], -1,
                           "the '>' press marks one engineer to sell (0x75750)");
                CheckEqI32(UiPanel_GetLineAux(g_world.pBuySellPanelD, nRow), nAux0 - 1,
                           "and the queue column drops one from its stock seed (0x77C20 seed, 0x755FC step)");
                CheckEqI32(g_world.nFunds, nFunds0,
                           "the crew sale refunds nothing at marking (0x757AC / 0x757BC are the plain-object branch)");
                CheckEqI32(g_world.nBuySellTransTotal, nBuySell1, "and carries no price into the total");

                {
                    const i32 nFunds1 = g_world.nFunds;
                    PressCtl(g_world.ui.apCtl[GEOUI_CTL_186BA4]);   /* CommitTransaction @ 0x74bf8 */
                    CheckEqI32((i32)g_assets.apObjects[PUFO_ROW_ENGINEER]->auPerBaseVal[nEBase], nStock0 - 1,
                               "the commit takes the sold engineer out of the base (0x74C54 pass, 0x55708 store)");
                    CheckEqI32(g_world.nBuySellFundsSnapshot, nFunds1, "and snapshots the funds (0x75070)");
                    CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "the tail close writes the id 1 (0x8C124)");
                    Check(g_world.pBuySellPad == NULL && g_world.bBuySellBuilt == 0,
                          "case 12's teardown releases the panels and the pad (0x8C11C, 0x8C13C)");
                }
            }
        }

        /* 99.6 Arming a transport and a combat craft. Screen 6 is the hangar (nav slot 3, 0x67B98): the
         * craft column's rows are roster indices grouped by Ship.bCraftType (aPlaneCraftTypeOrder
         * 0xABE28), the store pair's staging pass is BasePlaneScreen_CommitStores @ 0x7afd8 (it only
         * stages -- the base's stock moves when BuySellScreen_ApplyQtyChanges @ 0x78d90, sitting on the
         * shared confirm control, commits), and the weapon column's SelectWeapon24..30 handlers
         * (BasePlaneScreen_SelectWeapon25 @ 0x79b20 and its twins) put the mount byte on the craft
         * through the swap tail call (PlaneSwapSide 0x79A10's shape). The transport's proof is cargo;
         * the combat craft's proof is a real weapon mount, armed from a weapon the economy just made
         * or bought. */
        {
            UiPanel *pCraftPanel;
            Ship *pShip;
            i32 nTransport = -1, nCombat = -1, nWeapon = nProd;
            i32 nSlot = -1;

            if (nWeapon < WM_CANNON || nWeapon > WM_FUSION) {
                nWeapon = -1; /* the manufactured product is not a mount weapon; pick one below */
            }

            /* The transport the Part 98 mission flew out is still away, exactly as the original
             * leaves it after a debrief. Its home leg is armed the way the original's own handlers
             * arm one -- Ship_RetargetTravel @ 0x5a484, the call the dismissal at 0x84448, the radar
             * exits at 0x612CC / 0x613EC and the intercept resolve at 0x849AC all make -- and the
             * geoscape ship tick runs the event pass unconditionally (Geoscape_TickShips -> 0x7fcb0),
             * whose landing branch parks a craft whose home leg has run its time: bAtBase 1 at 0x80E5C,
             * nMissionSub 0 at 0x80E6C. */
            {
                i32 nAway = -1;
                for (nRow = 0; nRow < (i32)g_world.bCraftCount; ++nRow) {
                    if (g_world.apCraft[nRow] != NULL &&
                        g_world.apCraft[nRow]->bCraftType == CRAFT_TYPE_SKYRANGER &&
                        (i32)g_world.apCraft[nRow]->bHomeBase == nEBase &&
                        g_world.apCraft[nRow]->bAtBase == 0) {
                        nAway = nRow;
                        break;
                    }
                }
                Check(nAway >= 0, "the transport the mission flew out is still away from its home base");
                if (nAway >= 0) {
                    (void)Ship_RetargetTravel(&g_world.shipEnv, nAway); /* 0x5a484: the home leg, bC 1 at 0x7F648 */
                    for (nTick = 0; nTick < 400 && g_world.apCraft[nAway]->bAtBase == 0; ++nTick) {
                        if (g_world.geo.time.bPaused != 0) {
                            GameTime_Unpause(&g_world.geo.time); /* the event pass runs only on an unpaused clock */
                        }
                        g_world.geoClock.pfnTickHourly(g_world.geoClock.pUser);
                        g_world.geoClock.pfnTickShips(g_world.geoClock.pUser);
                    }
                    Check(g_world.apCraft[nAway]->bAtBase != 0,
                          "the event pass landed the transport home (0x80DF8..0x80E78: bAtBase 1 at 0x80E5C)");
                }
            }

            g_world.nav.apNav[3]->pfnClick(); /* BasePlaneScreen_Open @ 0x617f4 */
            CheckEqI32(g_world.screen.nGeoscapeScreen, 6, "nav slot 3 writes the hangar id (0x61828)");
            ClearSurface();
            (void)DrawFrame();
            pCraftPanel = g_world.planeCraftPanels.pCraftPanel;
            Check(pCraftPanel != NULL && pCraftPanel->nLineCount >= 2,
                  "the craft column lists the craft by type (0x7CCA0, 0xABE28)");
            for (nRow = 0; pCraftPanel != NULL && nRow < pCraftPanel->nLineCount; ++nRow) {
                const i32 nC = UiPanel_GetLineAux(pCraftPanel, nRow);
                if (nC < 0 || nC >= (i32)g_world.bCraftCount || g_world.apCraft[nC] == NULL) {
                    continue;
                }
                /* Only craft that are actually at the base pass the detail gate (0x7CC14), so the
                 * staging and the mount use those. The transport bought at 99.4 arrives through the
                 * grant's craft pass and stands here; the transport the Part 98 mission flew out is
                 * still away, exactly as the original leaves it. */
                if (nTransport < 0 && g_world.apCraft[nC]->bCraftType == CRAFT_TYPE_SKYRANGER
                    && g_world.apCraft[nC]->bAtBase != 0) {
                    nTransport = nRow;
                }
                if (nCombat < 0 && g_world.apCraft[nC]->bCraftType != CRAFT_TYPE_SKYRANGER
                    && g_world.apCraft[nC]->bAtBase != 0) {
                    nCombat = nRow;
                }
            }
            Check(nTransport >= 0 && nCombat >= 0,
                  "the roster holds a type-0 transport and a non-transport combat craft (0xABE28)");

            /* The transport: stage one unit of a base-stocked hold item into the craft. */
            if (nTransport >= 0) {
                pCraftPanel->nUnk3032 = nTransport;
                pCraftPanel->pfnClick();                          /* BasePlaneScreen_SelectCraft @ 0x7cbf0 */
                Check(g_world.bPlaneShowDetail == 1, "the craft is at base, so the detail view opens (0x7CC14)");
                g_world.planeBlock.apMainCtl[3]->pfnClick();      /* BasePlaneScreen_BuildControls @ 0x7b014 */
                Check(g_world.planeStorePanels.pCommitPanel != NULL, "the store columns are built (0x7B1C4)");
                ClearSurface();
                (void)DrawFrame(); /* World_RefreshGeoscapeFrame repoints the store handlers at the fresh panels */

                for (nSlot = 36; nSlot <= 52; ++nSlot) {
                    if ((i32)g_assets.apObjects[nSlot]->auPerBaseVal[nEBase] > 0) {
                        break;
                    }
                }
                Check(nSlot <= 52, "the base stocks a hold item (objects 36..52 fill the hold, 0x65b30)");
                if (nSlot <= 52) {
                    const i32 nStock0 = (i32)g_assets.apObjects[nSlot]->auPerBaseVal[nEBase];
                    const UiPanel *pSlotP = g_world.planeStorePanels.pSlotPanel;
                    i32 nStoreRow = -1;

                    /* The row pass lists only slots the base stocks or the craft carries, and the aux
                     * column holds the slot id (0x7B8A0), so the row to press is the one whose aux is
                     * the stocked item. CommitStores reads its own panel's cursor (0x161EEC) and bails
                     * when the row's available count is not positive (0x7AC68). */
                    for (nRow = 0; pSlotP != NULL && nRow < pSlotP->nLineCount; ++nRow) {
                        if (UiPanel_GetLineAux(pSlotP, nRow) == nSlot) {
                            nStoreRow = nRow;
                            break;
                        }
                    }
                    Check(nStoreRow >= 0, "the store list carries a row for the stocked item (0x7B7F8..0x7B930)");
                    if (nStoreRow >= 0) {
                        g_world.planeStorePanels.pCommitPanel->nUnk3032 = nStoreRow;
                        g_world.planeStorePanels.pSelRowPanel->nUnk3032 = nStoreRow;
                        g_world.planeStorePanels.pCommitPanel->pfnClick(); /* CommitStores @ 0x7ac38 */
                        CheckEqI32(g_world.anPlaneCarryCount[nSlot], 1,
                                   "CommitStores stages one unit of the row (0x7AFD8)");
                        CheckEqI32((i32)g_assets.apObjects[nSlot]->auPerBaseVal[nEBase], nStock0,
                                   "and staging has not moved the base's stock yet");
                        g_world.ui.apCtl[GEOUI_CTL_186BA4]->pfnClick(); /* ApplyQtyChanges @ 0x78d90 */
                        Check((i32)g_assets.apObjects[nSlot]->auPerBaseVal[nEBase] < nStock0,
                              "the confirm commit moves the unit out of the base (0x78D90's commit pass)");
                        CheckEqI32((i32)g_assets.apObjects[nSlot]->aItemQty[UiPanel_GetLineAux(pCraftPanel, nTransport)], 1,
                                   "and the transport carries it (the per-craft column 0x693EC walks)");
                        ClearSurface();
                        (void)DrawFrame();
                        DumpFrame("econ_hangar_transport");
                    }
                }
            }

            /* The combat craft: a weapon mount from a stocked weapon. A produced product in 24..30 is
             * the economy's own weapon; otherwise the cheapest stocked mount weapon, seeded if the
             * round produced none -- the base's weapon column (auPerBaseVal @ 0x2A) is the view the
             * swap loads from (Base_CraftWeaponItemId 0x6641C reads it). */
            if (nCombat >= 0) {
                if (nWeapon < 0) {
                    for (nRow = WM_CANNON; nRow <= WM_FUSION; ++nRow) {
                        if ((i32)g_assets.apObjects[nRow]->auPerBaseVal[nEBase] > 0) {
                            nWeapon = nRow;
                            break;
                        }
                    }
                    if (nWeapon < 0) {
                        nWeapon = WM_LASER; /* TEXTS[1474]: the mount the resupply switch leaves
                                             * alone, so the seeded unit is the whole requirement */
                        BaseRec_AddItemQty(g_world.apBases[nEBase], g_assets.apObjects[nWeapon], 1);
                    }
                }
                pCraftPanel->nUnk3032 = nCombat;
                pCraftPanel->pfnClick();
                CheckEqI32(g_world.nHangarCraftIdx, UiPanel_GetLineAux(pCraftPanel, nCombat),
                           "the combat craft is selected (0x7CBF8, 0x7CC00)");
                g_world.planeBlock.apMainCtl[0]->pfnClick();       /* BasePlaneScreen_DrawStatLabelA @ 0x79e00 */
                g_world.planeBlock.apMainCtl[1]->pfnClick();       /* DrawStatLabelB @ 0x79fc4 */
                g_world.planeBlock.apWeaponCtl[nWeapon - PUFO_WEAPON_ID_MIN]->pfnClick(); /* SelectWeapon24..30 */
                pShip = g_world.apCraft[UiPanel_GetLineAux(pCraftPanel, nCombat)];
                Check(pShip != NULL && ((char)pShip->bWeaponL == (char)nWeapon
                                            || (char)pShip->bWeaponR == (char)nWeapon),
                      "the swap mounts the weapon on the combat craft (0x79B34, the swap tail at 0x79A10)");
                printf("  econ: combat craft %d mounts weapon %d\n",
                       UiPanel_GetLineAux(pCraftPanel, nCombat), nWeapon);
                ClearSurface();
                (void)DrawFrame();
                DumpFrame("econ_hangar_combat");
            }

            /* Case 6 walks one arm per press, exactly like case 7's three presses above. With the craft
             * detail open, the first close press only exits the detail (CloseUI 0x8C07C keeps the screen,
             * 0x8C08C clears the detail flag and returns); the second reaches 0x8C094 (screen 1) and runs
             * the panels teardown at 0x8C098, which is what clears bPlaneBuilt. */
            PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);          /* BasePlaneScreen_Close @ 0x7c9e8 */
            CheckEqI32(g_world.bPlaneShowDetail, 0,
                       "the first close press only exits the craft detail (CloseUI 0x8C08C)");
            CheckEqI32(g_world.bPlaneBuilt, 1, "and leaves the panel build in place (0x8C07C)");
            PressCtl(g_world.ui.apCtl[GEOUI_CTL_186B00]);
            CheckEqI32(g_world.bPlaneBuilt, 0,
                       "the second press runs the panels teardown (0x8C094 id 1, 0x8C098)");
        }

        printf("  econ round: funds %d, site count %d, finished topic %d, product %d\n",
               g_world.nFunds, (i32)g_world.nSiteCount, (i32)g_world.nFinishedTopic, nProd);
    }

    /* ---- Part 100.1: the transfer screen end to end, exactly as the shipped build runs it ----------
     * The goal asks for transfer between bases to be proven live; the IDB says the shipped build cannot
     * commit one through its own UI (docs/parity/bugs.tsv rows 0x88950, 0x96368, 0x186BE8, 0x989d4):
     * Ui_InitTransfer arms the destination picker byte_1870F8 at 0x995B8 on every open and nothing on
     * the screen ever clears it, so the press arm (0x88950 BNE loc_889C4) routes presses to the confirm
     * control ONLY -- the prev/next/reset/commit buttons can never click, the confirm press is a bare
     * close (0x9965C thunk -> CloseUI case 10, teardown only), and the overlay frame pins byte_1870F4
     * to the source base (0x98994), so the destination equals the source. This section proves all of
     * that live, and runs the commit-and-delivery round through the controls' installed handlers, the
     * same functions the buttons would call (0x99624 / 0x99630). The staging press itself IS live: the
     * release arm blits ClickRow(B/C) unconditionally (0x8A64C), which is the screen's only working
     * control, and bugs.tsv row 0x96368 says the aux pair lands on the cached cursor row, not the aimed
     * row -- asserted here as it happens. The round: open at base 0, stage through the only live press,
     * commit (bFlag0 = 24, bFlag1 = the destination base's id, the pending quantity leaves the source),
     * close through the live SELECT press, delivery through Smap_GrantSiteRecovery 24 hours later. The
     * craft the commit queues (the pending row is a craft row) is never re-homed: the grant loop walks
     * bRecoveredCraftCount, which the commit never raised (bugs.tsv row 0x97C18) -- asserted too. */
    {
        const i32 nSlot    = 43;                   /* slot 43, the Heavy cannon [AP] clip, bCargoSize 2 */
        const i32 nSite0   = (i32)g_world.nSiteCount;
        UiPanel *pTrA, *pTrC, *pTrD, *pTrE;
        i32 nRow43 = -1, nTicks = 0;

        Check(g_world.nBaseCount >= 2, "the world has a second base");

        /* Stock the item at base 0 -- the cell the shop delivery's AddItemQty (0x9900C) writes, and
         * the cell the transfer fill (0x97810) reads. Base 0 also has the stores the gate measures
         * (the 99.x purchases built its storage block), which the picker pin then makes the
         * destination, so every gate measures the same base the items leave. */
        g_assets.apObjects[nSlot]->auPerBaseVal[0] = (u16)1;
        g_world.nSelectedBase = 0;                 /* nSelectedBaseIdx @ 0x159D81: source AND pinned destination */

        g_world.nav.apNav[7]->pfnClick();          /* nav slot 7: TransferScreen_Open @ 0x614fc */
        CheckEqI32(g_world.screen.nGeoscapeScreen, 10, "nav slot 7 lands the transfer id AFTER the Init (0x6151C)");
        CheckEqI32((i32)g_world.bTransferBuilt, 1, "Init's call at 0x995A4 built the five columns (0x96B60)");
        CheckEqI32((i32)g_world.bBaseInfoOverlayOpen, 1, "Init arms the picker: byte_1870F8 = 1 (0x995B8)");
        pTrA = g_world.pBuySellPanelA;
        pTrC = g_world.pBuySellPanelC;
        pTrD = g_world.pBuySellPanelD;
        pTrE = g_world.pBuySellPanelE;
        Check(pTrA != NULL && pTrC != NULL && pTrD != NULL && pTrE != NULL && pTrC->pfnClick != NULL,
              "all five columns exist and the increment column carries its handler (0x97168)");
        Check(g_world.ui.apCtl[GEOUI_CTL_186B00]->x == 224 && g_world.ui.apCtl[GEOUI_CTL_186B00]->y == 1,
              "Init seats the close control at (224,1) (0x9963C/0x99648)");
        CheckEqI32(g_world.ui.apCtl[GEOUI_CTL_186BA4]->y, 28 * (i32)g_world.nBaseCount + 10,
                   "Init seats the confirm control under the picker list, at the SELECT caption (0x99698/0x996B8)");
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("transfer_open"); /* picker list + SELECT, From: line, the five columns with digits */

        CheckEqI32((i32)g_world.nBaseInfoWorkBase, 0,
                   "the overlay frame pinned byte_1870F4 to the source base (the 0x98994 store)");
        for (i32 nRow = 0; pTrA != NULL && nRow < pTrA->nLineCount; ++nRow) {
            if (UiPanel_GetLineAux(pTrA, nRow) == nSlot) {
                nRow43 = nRow;
            }
        }
        Check(nRow43 >= 0, "the stocked slot is on the list (the 0x96CC4 filter, auPerBaseVal non-zero)");

        /* The dead buttons, live: a press at the commit button's box centre changes nothing -- the
         * press arm is behind the picker gate and the close thunk never runs for it. */
        if (nRow43 >= 0) {
            const i32 nCtlCx = g_world.aTransferCtl[3].x + (i32)g_world.aTransferCtl[3].uWidth / 2;
            const i32 nCtlCy = g_world.aTransferCtl[3].y + (i32)g_world.aTransferCtl[3].uHeight / 2;
            World_GeoscapePointer(&g_world, nCtlCx, nCtlCy, true);
            (void)World_GeoscapePointerUp(&g_world);
            CheckEqI32(g_world.screen.nGeoscapeScreen, 10, "the commit press cannot click: the arm hit-tests only 186BA4 (0x88950)");
            CheckEqI32((i32)g_world.nSiteCount, nSite0, "and it committed nothing either");
            PressCtl(&g_world.aTransferCtl[2]);    /* the reset button centre -- same gate */
            CheckEqI32((i32)g_world.nResidue1870B4, 0, "reset pressed dead: no reason code moves");
            CheckEqI32((i32)g_world.nShortfall1870C8, 0, "and no block flag either");
            Check(nRow43 > 12, "the stocked row sits past the 13 clickable rows (UiScrollList_ClickRow bounds nRow at nStyle-1) while the scroll buttons are dead (0x88950): the shipped screen cannot reach it");

            /* Staging through the screen's only live control: a press+release aimed at the increment
             * column's arrow box (panel C rect 184..199, row band from nPosY 70, nFont 16). The press
             * lands on the picker-gated arm (no effect); the release runs ClickRow(B/C) unconditionally
             * (0x8A64C..0x8A678). The aimed row supplies the SLOT (row 2, a soldier row), but the branch
             * and the aux pair follow the cached cursor row (dword_186BE8 -- never written on this
             * screen, so 0, a craft row): bugs.tsv row 0x96368, asserted here as it happens. */
            const i32 nCurRow = g_world.transferScratch.nCache186BE8; /* dword_186BE8, the cursor row */
            SmapSite *pSite = &g_world.apSites[nSite0];
            Ship *pShip = NULL;
            CheckEqI32(nCurRow, 0, "the transfer screen never caches a row: its only writer (0x96E30) is behind the dead arm");
            CheckEqI32(UiPanel_GetLineAux(pTrA, 0), 118, "row 0 is the first craft row (aux 117..121, the 0x97604 id-slot)");
            CheckEqI32(UiPanel_GetLineAux(pTrA, 2), 1000, "row 2 is a soldier row (the 0x9741C marker) -- the row the arrow is aimed at");
            CheckEqI32(UiPanel_GetLineAux(pTrD, 0), 1, "row 0's stock digit is the craft (one per craft row, 0x97610)");
            CheckEqI32(UiPanel_GetLineAux(pTrE, 0), 0, "row 0's pending digit starts at zero");
            CheckEqI32((i32)pSite->bRecoveredSoldierAssign, 0, "the working record starts without a soldier assignment");

            World_GeoscapePointer(&g_world, 191, 70 + 16 * 2 + 8, true);
            (void)World_GeoscapePointerUp(&g_world);
            CheckEqI32((i32)pTrC->nUnk3032, 2, "ClickRow(C) stores the aimed row into panel C (UiScrollList_ClickRow, 0x3EE..0x3F000 chain)");
            CheckEqI32((i32)pSite->bRecoveredSoldierAssign, 1,
                       "the soldier row supplied the slot: the crew gate ran and handed one soldier to the working record (0x9672C)");
            CheckEqI32(UiPanel_GetLineAux(pTrD, nCurRow), 0, "the stock digit drops on the CRAFT row (the cursor row), not the aimed row (0x96704)");
            CheckEqI32(UiPanel_GetLineAux(pTrE, nCurRow), 1, "the pending digit 1 lands on row 0 (the assign at 0x966E8, set not incremented)");
            CheckEqI32((i32)g_world.nShortfall1870C8, 0, "the living-quarters gate at the pinned destination passes (0x96690: 100 capacity vs 68 occupied + 1)");
            ClearSurface();
            (void)DrawFrame();
            DumpFrame("transfer_staged"); /* the pending 1 sits on row 0 -- under the overlay's black band (0x9877C spans y 0..87, row 0 spans 70..85): the stuck-open picker hides the very digit its own row pairing produced */

            /* Commit through the control's installed handler (Init wires it at 0x99630, the shell
             * thunk refreshes the envs first) -- the function a press would call if the press arm
             * ever reached it. The row with pending 1 is row 0, a craft row, so the commit queues
             * THAT craft and the soldier assignment rides in the record unsynchronised. */
            for (i32 i = 0; i < (i32)g_world.bCraftCount; ++i) {
                Ship *pC0 = g_world.apCraft[i];
                if (pC0 != NULL && (i32)pC0->bId == (i32)pTrC->anLineAux[nCurRow]) {
                    pShip = pC0;
                }
            }
            Check(pShip != NULL && (i32)pShip->bHomeBase == 0,
                  "row 0's craft (panel C carries its bId, 0x97690) stands homed at base 0");
            const i32 nHome0 = (i32)pShip->bHomeBase;
            g_world.aTransferCtl[3].pfnClick();  /* World_TransferCommitThunk -> TransferScreen_CommitTransfer @ 0x97aa4 */
            CheckEqI32((i32)pSite->bFlag0, PUFO_TRANSFER_SITE_FLAG_COMMIT, "the filled record closes with flag 24 (0x97DA4)");
            CheckEqI32((i32)pSite->bFlag1, (i32)g_world.apBases[0]->bUnk4,
                       "bFlag1 is the pinned destination base's id byte -- the source itself (0x97DBC over the 0x98994 pin)");
            CheckEqI32((i32)pSite->anCraftIds[0], (i32)pShip->bId, "the commit queued the craft of the pending row (0x97C48)");
            CheckEqI32((i32)pShip->bHomeBase, 255, "the craft leaves the roster: bHomeBase := 255 (0x97CD4)");
            CheckEqI32((i32)g_world.abFacilityGrid[444 + 144 * nHome0 + 6 * pShip->bHangarY + pShip->bHangarX], 0,
                       "and the home hangar cell is cleared (0x97CCC)");
            CheckEqI32(UiPanel_GetLineAux(pTrE, nCurRow), 0, "the commit zeroes the pending digit over every row (0x97DE8)");
            CheckEqI32((i32)g_world.nSiteCount, nSite0 + 1, "the commit raises nSmapSiteCount (0x97E8C)");
            CheckEqI32((i32)g_world.apSites[nSite0 + 1].bRecoveredSoldierAssign, 0, "the fresh working record is cleared (0x97F54)");
            CheckEqI32((i32)pSite->bRecoveredCraftCount, 0,
                       "the commit queued the craft id but never raised the counter its delivery loop counts (0x97C18 vs 0x205 -- the 0x96604 increment belongs to the craft staging that never ran here)");
            ClearSurface();
            (void)DrawFrame();
            DumpFrame("transfer_committed");

            /* The one press the live screen offers: SELECT under the picker list -- a bare close. */
            PressCtl(g_world.ui.apCtl[GEOUI_CTL_186BA4]); /* 0x9965C thunk -> Geoscape_CloseUI case 10 (0x8BE2C..0x8BE44) */
            CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "SELECT closes: case 10 releases to screen 1 and runs the teardown (no commit, no delivery request)");
            CheckEqI32((i32)g_world.bTransferBuilt, 0, "the teardown cleared the built flag (0x97994)");
            CheckEqI32((i32)g_world.bBaseInfoOverlayOpen, 0, "and the picker flag with it (0x979A0) -- the only live clear of byte_1870F8");
            Check(g_world.pBuySellPanelA == NULL && g_world.pBuySellPanelE == NULL,
                  "and freed the columns (0x97994..0x97A88)");

            /* 24 hours on the geoscape clock: the record delivers to the base its bFlag1 id resolves to. */
            while (nTicks < 25 && (i32)g_world.nSiteCount > nSite0) {
                g_world.geoClock.pfnTickHourly(g_world.geoClock.pUser);
                ++nTicks;
            }
            CheckEqI32((i32)g_world.nSiteCount, nSite0, "the delivered record compacts out of the list (0x994C8)");
            CheckEqI32((i32)pShip->bHomeBase, 255,
                       "the craft is never re-homed: the grant loop walks bRecoveredCraftCount (0), so the queued id (0x97C18) strands the craft -- bugs.tsv row 0x97C18");
            printf("  transfer round: slot %d stocked at base 0, staged at row 2 (soldier slot) while the digits moved on row 0 (craft), commit queued craft %d with the counter still 0, record delivered and compacted in %d ticks, craft %d left homeless (255)\n",
                   nSlot, (i32)pShip->bId, nTicks, (i32)pShip->bId);
        }
    }

    /* ---- Part 100.4: the crashed disc dispatch, map press to arrival offer ------------------------
     * The goal asks for sending craft to crashed discs, and the IDB says the shipped build runs the
     * whole thing through six links:
     *
     *   1. The generator. A subtype-5 mission's landing effect (MissionLanding's bSub 5 branch,
     *      0x7ED10) calls BasePlaneScreen_AddInterceptMission @ 0x7de14: the mission-site record the
     *      class picks (LDRSH +0 / +2 at 0x7DE48 / 0x7DE3C) becomes a strateg entry of type 3,
     *      active, visible, with nId = the mission counter (0x7DE64) and nTimer 2880 (0x7DE80) --
     *      the countdown 0x7FD14..0x7FD34 spends before the disc expires. The round runs that very
     *      function over the shell's own cells, on the same ctx the shell's World_MissionAddIntercept
     *      builds.
     *   2. The craft. The status panel's installed click handler (Geoscape_InitUI at 0x86C1C) answers a
     *      row of a parked craft's entry through its type-1 inactive arm (0x855FC):
     *      Ship_FindIndexByStrategType writes the craft index into 0x187054 and 0x159E74 (0x85624,
     *      0x85630), the clock pauses (0x85610) and a flag-11 craft-card line is appended (0x8563C).
     *      (The press-side twin 0x83688 needs a LIT marker and is gated live in Part 15; a parked
     *      craft's spawn entry is dark -- added with bVisible 0 at 0x2CB90, and 0x9554C lights only
     *      off-base entries.) The card's "new target" control carries Smap_SelectMapTarget (installed
     *      at 0x8FFEC): it raises bMapSelectionFlag (0x84498) and turns line 0 into the flag-27 "fly to
     *      the chosen point" prompt (0x84574, 0x84580).
     *   3. The disc press. Flag 27 has no press arm, so a press on the disc's cell reaches the select
     *      test again, and its type-3 active arm (0x83560) clears the panel row cursor (0x83568),
     *      stores the disc index at 0x186BE4 (0x83574), pauses (0x8357C), raises byte_187090 -- the
     *      crash panel's gate -- (0x83588), drops the prompt (0x83594) and appends flag 19 with the
     *      row in colour slot 0 and the item index in slot 1 (0x835B4), plus the radar pulse timer
     *      250 (0x835BC). Like the type-4 arm, it never touches bMapSelectionFlag, so the selection
     *      the card armed is still up.
     *   4. The offer. Flag 19 == 0x13 selects the mission-site panel arm (0x90D78): line 1 is
     *      TEXTS[1238] "Миссия: " with the panel's selected row glued on -- that row is the disc's
     *      own HitTest row, TEXTS[1146] written over 1145 at 0x82FE4 with itoa(nSiteId) appended
     *      (0x8301C) -- line 2 is TEXTS[1232] "Город: " with
     *      aMissionSites[strategList[slot 1].bCraftClass].szName (0x91038: LDRSB +0x02, *0x18, off
     *      0x1865C0 -- the very table the generator read), and with a selection up it seats
     *      *pGeoscapeCtlB at y 105 with Smap_CancelTarget (0x91098, 0x910AC) under the TEXTS[1127]
     *      caption at y 106, and *pGeoscapeCtlA at y 128 with Smap_ResolveInterceptUI (0x91120,
     *      0x91134) above "Отмена" at y 129.
     *   5. The launch. Smap_CancelTarget @ 0x84254 is the order despite its name: colour SLOT 1
     *      (0x84268) is the disc index, 0x187054 the craft, and 0x8427C calls Ship_LaunchIntercept --
     *      hot, off base, chasing with nStrategIdx = the disc (0x5A790, 0x5A7A4, 0x5A7AC, 0x5A79C).
     *      It then clears the selection (0x84284), 0x187098 (0x84290), the line (0x8429C), unpauses
     *      when the list runs empty (0x842B4), and clears 0x18708C (0x842BC) and 0x187090 (0x842C8).
     *   6. The arrival. Each event pass walks the marker dTrackA = dSpeed * 0.02 along the true
     *      diagonal (0x7FA3C, 0x8043C..0x80504); when |dx| < 2 && |dy| < 2 (0x80348, 0x80364) closes
     *      over a type-3 target (0x80684): the clock pauses by store, 0x159E74 takes the ship
     *      (0x80694), StatusPanelRow0 builds the terror caption with the disc's site id
     *      (0x80708..0x80748), and flag 21 is appended (0x80774) -- the descent offer onto the crash
     *      site, which Part 100.5 answers.
     *
     * The pen path mirrors section 15's card sequence. Fixture hygiene: the message list is emptied
     * with the same leaf every answer arm uses (UiLineList_Remove @ 0x5a3dc, as section 13 does
     * before an order); the tap cooldown is pre-set to 255, the value 0x87CE4 itself leaves after
     * every selecting press, so the disc press cannot run 0x87C94's second dispatch; and the disc
     * press is aimed at a cell whose 16x16 scan reads exactly one row, so the >1 box branch (0x832B0)
     * never runs. */
    {
        UiControl *pCtlNew = g_world.ui.apCtl[GEOUI_CTL_186B9C]; /* the craft card's "new target" */
        UiControl *pCtlA   = g_world.ui.apCtl[GEOUI_CTL_186BB4]; /* *pGeoscapeCtlA */
        UiControl *pCtlB   = g_world.ui.apCtl[GEOUI_CTL_186BB0]; /* *pGeoscapeCtlB */
        InterceptMissionCtx ictx;
        i32 nMissionId0 = g_world.nMissionId;
        i32 nList0, nSiteIdx = -1, nDiskIdx = -1;
        i32 nShipIdx = -1, nMarkIdx = -1;
        i32 nMarkX = 0, nMarkY = 0, nRowIdx = -1;
        i32 nSiteX = 0, nSiteY = 0, nSitePressX = 0, nSitePressY = 0;
        i32 nStripH, nBest, nMid = -1, nPasses = 0, nArriveFlag = -1, i;
        const i32 nPassMax = 2870; /* the disc expires at 2880 passes (0x7FD14), one less stays honest */

        printf("part 100.4: the crashed disc dispatch, map press to arrival offer\n");

        /* Close from the screen 1 the transfer's SELECT left the world on (100.1's live press), so
         * CloseUI case 1 runs (0x8BE94) and the world arm owns the pen again. */
        CheckEqI32(g_world.screen.nGeoscapeScreen, 1, "100.1's SELECT close left screen 1 (0x8BE44)");
        World_CloseGeoscape(&g_world);
        CheckEqI32(g_world.screen.nGeoscapeScreen, 0, "the close reaches the globe (0x8BE94)");

        /* The line 100.1's commit left standing (100.2's opening comment names it) is dropped with
         * the leaf every answer arm uses -- section 13 empties the list the same way before an order. */
        while (g_world.msgLines.nLineCount > 0) {
            UiLineList_Remove(&g_world.msgLines, 0);
        }
        CheckEqI32(g_world.msgLines.nLineCount, 0, "line 0 is free, so no flag arm reroutes the pen");
        printf("  the opening state: sel %d, 18708C %d, 187090 %d, 187098 %d, 1870A0 %d, shortfall %d, paused %d, zoom %d\n",
               g_world.bMapSelectionFlag, g_world.bFlag18708C, g_world.bFlag187090, g_world.bFlag187098,
               g_world.bFlag1870A0, g_world.nShortfall1870C8, g_world.geo.time.bPaused, g_world.view.nMode);
        CheckEqI32(g_world.bMapSelectionFlag, 0,
                   "the selection byte is down: an inactive marker may build its row (0x830F4 gate)");
        CheckEqI32(g_world.view.nMode, 0, "zoom 0, the mode at which stored coordinates are used raw (0x82D48 gate)");
        for (i = 0; i < (i32)g_world.bCraftCount; ++i) {
            const Ship *const pC = g_world.apCraft[i];
            const i32 nM = (pC != NULL) ? FindStrategById(&g_world.strategList, (i32)pC->bId) : -1;
            const StrategItem *const pE = (nM >= 0) ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nM) : NULL;
            printf("  craft %d: id %d atBase %d hot %d chas %d home %d contactMsg %d | marker %d: %s type %d act %d vis %d at %d,%d timer %d\n",
                   i, (pC != NULL) ? (i32)pC->bId : -1, (pC != NULL) ? (i32)pC->bAtBase : -1,
                   (pC != NULL) ? (i32)pC->bWeaponsHot : -1, (pC != NULL) ? (i32)pC->bIntercepting : -1,
                   (pC != NULL) ? (i32)pC->bHomeBase : -1, (pC != NULL) ? (i32)pC->bContactMsg : -1,
                   nM, (pE != NULL) ? "item" : "none", (pE != NULL) ? (i32)pE->nType : -1,
                   (pE != NULL) ? (i32)pE->bActive : -1, (pE != NULL) ? (i32)pE->bVisible : -1,
                   (pE != NULL) ? pE->nX : 0, (pE != NULL) ? pE->nY : 0, (pE != NULL) ? pE->nTimer : -1);
        }
        printf("  view: scroll %d,%d size %d,%d, selShip %d, msgShip %d\n", g_world.view.nScrollX,
               g_world.view.nScrollY, g_world.view.nMapW, g_world.view.nMapH,
               g_world.nSelectedShipIdx, g_world.nMsgShipIdx);
        printf("  root status panel: %d rows, cursor %d", (i32)g_world.ui.pStatusPanel->nLineCount,
               (i32)g_world.ui.pStatusPanel->nUnk3032);
        for (i = 0; i < (i32)g_world.ui.pStatusPanel->nLineCount && i < 8; ++i) {
            printf(" | row %d aux %d", i, (i32)g_world.ui.pStatusPanel->anLineAux[i]);
        }
        printf("\n");

        nStripH = (i32)g_world.ui.apPad[GEOUI_PAD_186BBC]->uHeight;
        g_world.frame.g.nTapCooldown = 255; /* the value 0x87CE4 itself leaves after a selecting press */

        /* The craft. A parked craft is selected in the shipped build through the status panel's own row
         * click: Geoscape_InitUI installs Geoscape_OnRadarListClick @ 0x854c4 as the root panel's click
         * handler (0x86C1C), and its type-1 inactive arm (0x855FC) takes the row's aux -- a strateg-list
         * index -- through Ship_FindIndexByStrategType into 0x187054 and 0x159E74 (0x85624, 0x85630),
         * pauses (0x85610), appends the flag-11 craft-card line (0x8563C) and lights the radar pulse
         * timer 250 (0x85644). The press-side answer 0x83688 is the same arm for a LIT marker, and Part
         * 15 gates it live; here all three craft stand at base and the entries they were spawned with
         * stay dark (Ship_SpawnAtSlot @ 0x2CB90 adds the entry with bVisible 0, and the hot pass at
         * 0x9554C lights only the entry of a craft that is not at base), so the row click is the live
         * selection in this state -- the installed-handler idiom Part 12f's rows used (pfnClick()). */
        for (i = 0; i < (i32)g_world.bCraftCount && nShipIdx < 0; ++i) {
            const Ship *const pCand = g_world.apCraft[i];
            const StrategItem *pMark;
            i32 nM;

            if (pCand == NULL || pCand->bContactMsg != 0 || i > 254
                || (i32)pCand->bHomeBase >= (i32)g_world.nBaseCount) {
                continue; /* bContactMsg would re-hand the card's controls to Resolve (0x8FFC8..0x8FFF8) */
            }
            nM = FindStrategById(&g_world.strategList, (i32)pCand->bId);
            pMark = (nM >= 0) ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nM) : NULL;
            if (pMark == NULL || pMark->nType != 1 || pMark->bActive != 0) {
                continue; /* the spawn's entry IS the craft's map marker (0x2CB90) */
            }
            nShipIdx = i;
            nMarkIdx = nM;
            nMarkX = pMark->nX;
            nMarkY = pMark->nY;
        }
        Check(nShipIdx >= 0, "one parked craft has a valid home base and its spawn entry on the list");
        Check(nMarkIdx >= 0 && nShipIdx <= 254, "the craft index fits the byte the line slots carry (0x8563C's a3 is LDRB-masked)");

        /* The row that names it: the click handler indexes anLineAux with the cursor row (0x85500), so the
         * cursor goes to a row whose aux already holds the marker index -- the rows the geoscape's own
         * list passes left standing. */
        for (i = 0; nMarkIdx >= 0 && i < (i32)g_world.ui.pStatusPanel->nLineCount; ++i) {
            if ((i32)g_world.ui.pStatusPanel->anLineAux[i] == nMarkIdx) {
                nRowIdx = i;
                break;
            }
        }
        Check(nRowIdx >= 0 && g_world.ui.pStatusPanel->pfnClick != NULL,
              "the root panel carries a row at the marker's list index and the click handler InitUI installed at 0x86C1C");

        /* The disc's home: the closest mission-site record (0x1865C0, stride 0x18 -- 0x91038's own
         * scale) that lands on-screen clear of the strip, answers as a single active row under an
         * armed selection, and stands far enough from the marker that the marker press stays single
         * too (a type-3 active row builds under either flag state, 0x82F8C). */
        nBest = 0x7FFFFFFF;
        for (i = 0; nShipIdx >= 0 && i < PUFO_MISSION_SITE_COUNT; ++i) {
            const MissionSiteRec *const pSite = &g_world.aMissionSites[i];
            const i32 nDx = pSite->x - nMarkX;
            const i32 nDy = pSite->y - nMarkY;
            const i32 nAbsX = (nDx < 0) ? -nDx : nDx;
            const i32 nAbsY = (nDy < 0) ? -nDy : nDy;
            const i32 nCheb = (nAbsX > nAbsY) ? nAbsX : nAbsY;
            i32 nPx, nPy;

            if (nCheb <= 8 || nCheb >= nBest) {
                continue;
            }
            if (!ScreenForCell(&g_world.view, pSite->x, pSite->y, &nPx, &nPy)
                || nPy > 320 - nStripH - 6) {
                continue;
            }
            if (TestHitLineCount(&g_world.strategList, pSite->x, pSite->y, true) != 0) {
                continue; /* with the disc added this cell must read as one row, so it starts empty */
            }
            nBest = nCheb;
            nSiteIdx = i;
            nSiteX = pSite->x;
            nSiteY = pSite->y;
            nSitePressX = nPx;
            nSitePressY = nPy;
        }
        Check(nSiteIdx >= 0, "a mission-site cell stands on-screen, clear, and near the chosen craft");

        if (nSiteIdx < 0 || nShipIdx < 0) {
            printf("  the fixture found no craft/site pair -- the dispatch round cannot run (craft %d, site %d)\n",
                   nShipIdx, nSiteIdx);
            Check(nSiteIdx >= 0 && nShipIdx >= 0, "both picks exist");
        }

        /* The map before the disc exists, and a 16x16 window at the site's press pixel. The site
         * scan above read a wider box of that cell with the selection up, so the window holds no
         * item the icon pass could already paint; a redraw that changes pixels inside it can then
         * only be the disc's own glyph (Geoscape_Draw's call at 0x8C694/0x8C698, the type-3 row at
         * 0x82594). Nothing between the two draws touches the view, the clock, or the anim phase,
         * so the map, its drift, and the strip are pixel-identical in both. */
        u16 aGlyphBefore[16 * 16];
        i32 nGlyphX0 = 0, nGlyphY0 = 0;
        if (nSiteIdx >= 0 && nShipIdx >= 0) {
            nGlyphX0 = nSitePressX;
            nGlyphY0 = nSitePressY;
            if (nGlyphX0 + 16 > PUFO_SURF_W) { nGlyphX0 = PUFO_SURF_W - 16; }
            if (nGlyphY0 + 16 > PUFO_SURF_H) { nGlyphY0 = PUFO_SURF_H - 16; }
            ClearSurface();
            (void)DrawFrame();
            for (i = 0; i < 16; ++i) {
                memcpy(&aGlyphBefore[i * 16],
                       &((const u16 *)g_assets.pSurface->pPixels)
                           [(size_t)(nGlyphY0 + i) * (size_t)g_assets.pSurface->nWidth
                                + (size_t)nGlyphX0],
                       16 * sizeof(u16));
            }
        }

        /* 1. The generator, on the shell's own cells: the ctx is what World_MissionAddIntercept
         * (pufo_world.c) builds for the 0x7ED10 hook. */
        if (nSiteIdx >= 0 && nShipIdx >= 0) {
            i32 nRet;

            memset(&ictx, 0, sizeof(ictx));
            ictx.pList       = &g_world.strategList;
            ictx.pnZoomMode  = &g_world.view.nMode;
            ictx.pnSeed      = &g_world.nStrategSeed;
            ictx.apSites     = g_world.aMissionSites;
            ictx.nSiteCount  = PUFO_MISSION_SITE_COUNT;
            ictx.pnMissionId = &g_world.nMissionId;
            ictx.pnLastIndex = &g_world.nSelectedStrategIdx;

            nList0 = PtrArray_GetCount(&g_world.strategList);
            nRet = BasePlaneScreen_AddInterceptMission(nSiteIdx, &ictx);
            nDiskIdx = nList0;
            CheckEqI32(PtrArray_GetCount(&g_world.strategList), nList0 + 1, "the add lands one item on the list (0x7DE64)");
            CheckEqI32(g_world.nSelectedStrategIdx, nDiskIdx, "0x7DE98 stores the new index at 0x186BE4");
            CheckEqI32(nRet, nMissionId0 + 1, "the call returns the POST-incremented mission id");
            Check(nDiskIdx <= 254, "the disc index fits the byte the flag-19 line and the arrival line carry (0x835B4, 0x80774)");

            {
                const StrategItem *const pDisk =
                    (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx);
                Check(pDisk != NULL && pDisk->nType == 3 && pDisk->bActive == 1 && pDisk->bVisible == 1,
                      "type 3, active, visible -- the shape the crash arms read (0x7DE64 arguments)");
                Check(pDisk != NULL && (i32)(i8)pDisk->bCraftClass == nSiteIdx && pDisk->nSiteId == nMissionId0
                          && pDisk->nX == nSiteX && pDisk->nY == nSiteY,
                      "its byte is the site index the table read used, its id the mission id the counter held, its cell the record's own x/y (0x7DE48, 0x7DE3C, 0x7DE64)");
                Check(pDisk != NULL && pDisk->nTimer == 2880, "0x7DE80 sets the expiry timer 2880");
                Check(pDisk != NULL && pDisk->nDisplayId == -1, "nType 3 sends the id to nSiteId and clears nDisplayId (0x82990..0x829A8)");
            }
        }

        /* 2. The disc on the map before anything answers: the picture is the icon the type-3 draw
         * puts up (the mapbits + anim + 8 blend Part 90 gated at unit level). */
        if (nSiteIdx >= 0) {
            ClearSurface();
            (void)DrawFrame();
            DumpFrame("crashdisk_map");
            if (nShipIdx >= 0) {
                const u16 *const pPix = (const u16 *)g_assets.pSurface->pPixels;
                i32 nDiff = 0;
                for (i = 0; i < 16; ++i) {
                    for (int x = 0; x < 16; ++x) {
                        if (pPix[(size_t)(nGlyphY0 + i) * (size_t)g_assets.pSurface->nWidth
                                 + (size_t)(nGlyphX0 + x)]
                            != aGlyphBefore[i * 16 + x]) {
                            ++nDiff;
                        }
                    }
                }
                printf("  glyph window at %d,%d changed in %d of 256 pixels\n",
                       nGlyphX0, nGlyphY0, nDiff);
                Check(nDiff > 0, "the disc glyph appears between chrome and crosshair (0x8C694, 0x8C698, 0x82594)");
            }
        }

        if (nSiteIdx >= 0 && nShipIdx >= 0) {
            /* 3. The row click the shipped build answers with: point the panel's cursor at the marker's
             * row and run the handler InitUI installed -- exactly what a release on that row runs
             * (UiScrollList_ClickRow sets [0xBD8] = [0xBD4] + row and tail-calls [0xBC4], section 15 B). */
            g_world.ui.pStatusPanel->nUnk3032 = nRowIdx;
            g_world.ui.pStatusPanel->pfnClick();
            CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 11, "the type-1 inactive arm appends the card line (0x8563C)");
            CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), nShipIdx, "its colour slot carries the craft the lookup found (0x85624's result)");
            CheckEqI32(g_world.nSelectedShipIdx, nShipIdx, "0x187054 holds it (0x85624)");
            CheckEqI32(g_world.nMsgShipIdx, nShipIdx, "and 0x159E74 with it (0x85630)");
            CheckEqI32(g_world.nSelectedStrategIdx, nMarkIdx, "the click stored the row's aux at 0x186BE4 first (0x8550C)");
            CheckEqI32(g_world.nRadarPulseTimer, 250, "and the arm lights the radar pulse timer (0x85644)");
            CheckEqI32(g_world.geo.time.bPaused, 1, "the arm pauses (0x85610)");

            /* 4. The card's "new target" control arms the selection and posts the fly-to-point prompt. */
            ClearSurface();
            (void)DrawFrame();
            Check(pCtlNew->pfnClick == g_world.frame.panel.pfnSelectMapTarget,
                  "the craft-detail card carries Smap_SelectMapTarget on 186B9C (0x8FFE8, and 0x8FFE0's Dismiss store is overwritten two instructions later)");
            PressCtl(pCtlNew);
            CheckEqI32(g_world.bMapSelectionFlag, 1, "Smap_SelectMapTarget arms the selection (0x84498)");
            CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 27, "line 0 is the fly-to-point prompt (0x84574, 0x84580)");

            /* 5. The disc press answers its own cell. */
            World_GeoscapePointer(&g_world, nSitePressX, nSitePressY, true);
            (void)World_GeoscapePointerUp(&g_world);
            CheckEqI32((i32)UiLineList_GetFlag(&g_world.msgLines, 0), 19, "the type-3 active arm appends flag 19 (0x835B4)");
            CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), 0, "colour slot 0 is the row cursor it cleared (0x83568)");
            CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 1), nDiskIdx, "slot 1 carries the disc index (0x83574 -> 0x835B4)");
            CheckEqI32(g_world.nSelectedStrategIdx, nDiskIdx, "0x186BE4 holds the disc (0x83574)");
            CheckEqI32(g_world.bFlag187090, 1, "byte_187090, the crash panel gate, is up (0x83588)");
            CheckEqI32(g_world.bMapSelectionFlag, 1, "the selection is STILL up: the type-3 arm never writes the byte");
            CheckEqI32(g_world.nRadarPulseTimer, 250, "and 0x835BC lights the pulse timer");
            CheckEqI32(g_world.geo.time.bPaused, 1, "the arm pauses by store (0x8357C)");

            /* 6. The mission-site card as the player sees it. */
            ClearSurface();
            (void)DrawFrame();
            Check(pCtlB->y == PUFO_STATUS_SITECTL_Y_CANCEL && pCtlB->pfnClick == g_world.frame.panel.pfnCancelTarget,
                  "the selecting arm puts Smap_CancelTarget on *pGeoscapeCtlB at y 105 (0x91098, 0x910AC)");
            Check(pCtlA->y == PUFO_STATUS_SITECTL_Y_OK2 && pCtlA->pfnClick == g_world.frame.panel.pfnResolveInterceptUI,
                  "and Smap_ResolveInterceptUI on *pGeoscapeCtlA at y 128 (0x91120, 0x91134)");
            Check(pCtlA->x == 120 - ((i32)pCtlA->uWidth / 2) && pCtlB->x == 120 - ((i32)pCtlB->uWidth / 2),
                  "both centred on x 120 (0x91088, 0x91110)");
            {
                const char *const pszCap = (const char *)g_assets.pTextRecs
                                           + 1146 * PUFO_TEXT_REC_STRIDE; /* PUFO_TEXT_REC_ALIEN_LABEL, 0x82FE4's record */
                const char *const pszRow = (g_world.ui.pStatusPanel->apLines[0] != NULL)
                                               ? (const char *)g_world.ui.pStatusPanel->apLines[0] : NULL;
                char szWant[24];

                snprintf(szWant, sizeof(szWant), "%d", nMissionId0);
                Check(pszRow != NULL && strncmp(pszRow, pszCap, strlen(pszCap)) == 0,
                      "the panel's row 0 is the disc's HitTest row: TEXTS[1146] over 1145 (0x82FE4)");
                Check(pszRow != NULL && strcmp(pszRow + strlen(pszCap), szWant) == 0,
                      "  and it ends with itoa(nSiteId) (0x8301C) -- the card's line 1 is this row glued onto TEXTS[1238] (0x90F7C..0x90FB8)");
                CheckEqI32((i32)g_world.ui.pStatusPanel->anLineAux[0], nDiskIdx, "the row's aux holds the disc (0x8306C)");
            }
            DumpFrame("crashdisk_offer");

            /* 7. The ОК: Smap_CancelTarget is the order despite its name. */
            PressCtl(pCtlB);
            {
                Ship *const pShip = g_world.apCraft[nShipIdx];
                const StrategItem *const pDisk =
                    (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx);
                Check(pShip != NULL && pShip->bWeaponsHot == 1 && pShip->bAtBase == 0
                          && pShip->bIntercepting == 1 && (i32)pShip->nStrategIdx == nDiskIdx,
                      "Ship_LaunchIntercept @ 0x8427C: hot (0x5A790), off base (0x5A7A4), chasing the disc index 0x84268 read out of colour slot 1 (0x5A79C, 0x5A7AC)");
                Check(pDisk != NULL && pDisk->nType == 3 && pDisk->nX == nSiteX && pDisk->nY == nSiteY,
                      "the disc itself stands untouched where the table put it");
            }
            CheckEqI32(g_world.bMapSelectionFlag, 0, "the launch clears the selection (0x84284)");
            CheckEqI32(g_world.bFlag187098, 0, "0x187098 (0x84290)");
            CheckEqI32(g_world.msgLines.nLineCount, 0, "and the card line (0x8429C)");
            CheckEqI32(g_world.geo.time.bPaused, 0, "the clock runs again with the list empty (0x842B4)");
            CheckEqI32(g_world.bFlag18708C, 0, "0x18708C (0x842BC)");
            CheckEqI32(g_world.bFlag187090, 0, "and the crash gate 0x187090 (0x842C8)");

            /* 8. The launch leaves the marker dark; the pulse's hot pass is what lights it (0x955C0),
             * and only a lit marker is walked by the travel pass (0x7FE1C skips the dark one). */
            {
                StrategItem *const pMark =
                    (StrategItem *)PtrArray_Get(&g_world.strategList, nMarkIdx);
                Check(pMark != NULL && pMark->nTimer == 0, "the launch restarts the entry's timer (0x5A67C)");
                Check(pMark != NULL && pMark->bVisible == 0, "and leaves the entry invisible (0x7F638's bB 0)");
            }
            World_PulseGeoscape(&g_world);
            {
                const StrategItem *const pMark =
                    (const StrategItem *)PtrArray_Get(&g_world.strategList, nMarkIdx);
                Check(pMark != NULL && pMark->bVisible == 1,
                      "Geoscape_PulseUpdate lights the entry of the craft that is off base (0x955C0, 0x955C8)");
                nMarkX = (pMark != NULL) ? pMark->nX : 0;
                nMarkY = (pMark != NULL) ? pMark->nY : 0;
            }
            ClearSurface();
            (void)DrawFrame();
            DumpFrame("crashdisk_dispatched");

            /* 9. The chase, pass by pass, until |dx| < 2 && |dy| < 2 closes (0x80348, 0x80364).
             * nMid carries the opening leg: the en-route picture is dumped once the marker has
             * closed half of it, and nMid then drops to 0 so the dump runs at most once. */
            {
                const StrategItem *const pMark0 =
                    (const StrategItem *)PtrArray_Get(&g_world.strategList, nMarkIdx);
                const StrategItem *const pDisk0 =
                    (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx);
                if (pMark0 != NULL && pDisk0 != NULL) {
                    const i32 nAx = pDisk0->nX - pMark0->nX;
                    const i32 nAy = pDisk0->nY - pMark0->nY;
                    const i32 nAbsX = (nAx < 0) ? -nAx : nAx;
                    const i32 nAbsY = (nAy < 0) ? -nAy : nAy;
                    nMid = (nAbsX > nAbsY) ? nAbsX : nAbsY;
                }
            }
            while (nPasses < nPassMax && nArriveFlag < 0) {
                const StrategItem *const pMark =
                    (const StrategItem *)PtrArray_Get(&g_world.strategList, nMarkIdx);
                const StrategItem *const pDisk =
                    (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx);
                i32 nDx, nDy, nCheb;

                if (pMark == NULL || pDisk == NULL) {
                    break; /* the disc expired (0x7FD34) or the entry left the list */
                }
                nDx = pDisk->nX - pMark->nX;
                nDy = pDisk->nY - pMark->nY;
                nCheb = ((nDx < 0 ? -nDx : nDx) > (nDy < 0 ? -nDy : nDy)) ? (nDx < 0 ? -nDx : nDx)
                                                                          : (nDy < 0 ? -nDy : nDy);
                ++nPasses;
                g_world.geoHooks.pfnProcessEvents(0, g_world.geoHooks.pUser);
                if (g_world.msgLines.nLineCount > 0) {
                    const i32 nFlag = (i32)UiLineList_GetFlag(&g_world.msgLines, 0);
                    if (nFlag == 21 || nFlag == 13) {
                        nArriveFlag = nFlag;
                        break;
                    }
                    printf("  pass %d: a stray flag-%d line from an unrelated item -- dropped for the round\n", nPasses, nFlag);
                    UiLineList_Remove(&g_world.msgLines, 0);
                }
                if (nMid > 0 && nCheb * 2 <= nMid) { /* the marker has closed half its opening leg */
                    ClearSurface();
                    (void)DrawFrame();
                    DumpFrame("crashdisk_enroute");
                    nMid = 0;
                }
                if ((nPasses % 100) == 0) {
                    printf("  pass %d: marker at %d,%d, disc at %d,%d (cheb %d), disc timer %d\n",
                           nPasses, pMark->nX, pMark->nY, pDisk->nX, pDisk->nY, nCheb, pDisk->nTimer);
                }
            }
            Check(nArriveFlag == 21, "the chase closed on the disc and the arrival posted its offer (0x80684, 0x80774)");

            if (nArriveFlag == 21) {
                CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), nShipIdx, "the offer line names the craft (0x80774's a3)");
                CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 1), nDiskIdx, "  and the disc (its a4)");
                CheckEqI32(g_world.geo.time.bPaused, 1, "the arrival pauses by store (0x80684)");
                CheckEqI32(g_world.nMsgShipIdx, nShipIdx, "0x159E74 takes the ship (0x80694)");
                {
                    const char *const pszCap = (const char *)g_assets.pTextRecs
                                               + 1146 * PUFO_TEXT_REC_STRIDE; /* the terror caption, PUFO_CAP_TERROR_TEXT at 0x964 */
                    const char *const pszRow = (g_world.ui.pStatusPanel->apLines[0] != NULL)
                                                   ? (const char *)g_world.ui.pStatusPanel->apLines[0] : NULL;
                    char szWant[24];

                    snprintf(szWant, sizeof(szWant), "%d", nMissionId0);
                    Check(pszRow != NULL && strncmp(pszRow, pszCap, strlen(pszCap)) == 0
                              && strcmp(pszRow + strlen(pszCap), szWant) == 0,
                          "StatusPanelRow0 rebuilt the terror caption with the disc's site id (0x80708..0x80748)");
                    CheckEqI32((i32)g_world.ui.pStatusPanel->anLineAux[0], nDiskIdx, "its aux holds the disc (0x80748)");
                }
                {
                    const Ship *const pShip = g_world.apCraft[nShipIdx];
                    Check(pShip != NULL && pShip->bIntercepting == 1,
                          "the craft is still chasing: a type-3 arrival clears nothing (0x80684..0x80774 has no clear, unlike the type-5 arm at 0x80668)");
                }
                ClearSurface();
                (void)DrawFrame();
                Check(pCtlB->y == PUFO_STATUS_SITECTL_Y_CANCEL && pCtlB->pfnClick == g_world.frame.panel.pfnBeginCraftSelect,
                      "flag 21 is the descent-target panel (0x916A0): its launch control at y 105 carries the craft-select handler (0x917F8, 0x9180C)");
                Check(pCtlA->y == PUFO_STATUS_SITECTL_Y_OK2 && pCtlA->pfnClick == g_world.frame.panel.pfnResolveInterceptUI,
                      "and the cancel handler sits at y 128 (0x918B0, 0x918C4)");
                DumpFrame("crashdisk_arrival");
            }
            printf("  disc round: mission id %d, site %d, disc item %d at (%d,%d); craft %d dispatched by the card's ОК, closed the %d-cell leg in %d passes; arrival flag %d\n",
                   nMissionId0, nSiteIdx, nDiskIdx, nSiteX, nSiteY, nShipIdx, nBest, nPasses, nArriveFlag);
        }
    }

    /* ---- section 100.5: the descent offer decides -- Отмена recalls the craft, ОК opens the urban
     *      assault with the mission mode switched (Smap_BeginCraftSelect @ 0x838dc) -------------
     *
     * The standing state is 100.4's arrival: the flag-21 line (slots ship, disc), the clock paused,
     * the card installed with ОК = pfnBeginCraftSelect on *pGeoscapeCtlB at y 105 (0x917F8, 0x9180C)
     * and Отмена = pfnResolveInterceptUI on *pGeoscapeCtlA at y 128 (0x918B0, 0x918C4). Both card
     * answers run here, and the mission header shows what only the ОК answer changes.
     *
     * A. Отмена -- Smap_ResolveInterceptUI @ 0x84964. Its head resets the tick to 5 s (0x84968) and
     *    clears bInterceptDone (0x84984); the flag is 21, so neither the flag-27 retarget (0x849AC)
     *    nor the flag-13 insert (0x849DC) runs; the clear block drops 0x187090/18708C/selection/
     *    0x187098 (0x84A24..0x84A3C); the flag-21 arm at 0x84A58 runs Smap_DismissSelected --
     *    Ship_RetargetTravel @ 0x5a484 sends the craft to apBases[bHomeBase] with bB 1 (the entry
     *    goes VISIBLE again, 0x7F638, the opposite of the launch's bB 0) with nMissionSub 1
     *    (0x7F648), timer 0 (0x5A524), and the intercept byte cleared (0x5A59C); then the ORIGINAL
     *    BUG at 0x84A80 -- the unconditional Remove(0) -- and the unpause-if-empty (0x84A84). The
     *    mission header does not move and no mission starts.
     * B. The re-order runs through the game's own Ship_LaunchIntercept @ 0x5a64c on the shell's env,
     *    the recorded section-13 precedent: the card chain that reaches the same call (0x8427C) was
     *    proven live in 100.4, so 100.5 spends its pen on the two card answers. With bAtBase 0 the
     *    leg leaves from the entry's current cell (0x5A6A4 -- the 0x5A718 base snap needs bAtBase),
     *    which is the arrival cell, two cells from the disc: the pulse lights the entry (0x955C0) and
     *    the very next event passes re-run the arrival arm at 0x80684.
     * C. ОК -- the press arm's flag-21/22/23 case (0x87658, proven pressing this card in section 13)
     *    arms the launch control, the release (0x89AA0) clicks it, and the thunk runs
     *    Smap_BeginCraftSelect @ 0x838dc on the shell's craft-select env. The visibility pass
     *    (0x838FC..0x83988) parks the two parked craft (hot 0 at 0x8393C, at base at 0x83950, entry
     *    hidden at 0x83968) and passes the chasing one through. The selection pass (mode != 1,
     *    0x839A4) republishes the line's slots into 0x187054/0x159E74/Map_nCraftIndex (0x839CC..
     *    0x839DC) and 0x186BE4 (0x839E4); the overlay at the target decides unk_128AD6 (0x83B54);
     *    the marker's bRace 255 keeps the sprite set (0x83BB0..0x83C40). The flag-21 arm (0x83F1C)
     *    switches the mission: Map_nMissionType = Urban 5 (0x83F28), Map_bMissionMode = 3 (0x83F34),
     *    the geometry trio 0/0/craft class+1 (0x83F44..0x83F68), and the disc is REMOVED from the
     *    list (StrategList_RemoveAt 0x83F7C) -- the offer consumed its site. The mode gate falls to
     *    the tail (0x84004, 0x83A0C): Smap_DismissSelected (0x83AA0), the selection clear (0x83AB4),
     *    GeoscapeUi_FreeAll(0) (0x83ABC) and Tact_InitMission(0) (0x83AC4) over the shell's
     *    World_InitTactical. The briefing gate (spawnAlt AND the INIT-ARGUMENT mode 0, 0x3D28C/0x3D29C)
     *    tests that argument, not Map_bMissionMode 3 -- measured: the Cydonia briefing DOES stand
     *    (screen 20, slot 11 saves the 1 the init's own Inv_OpenScreen 0x3D0B8 left); cancel (0x16294)
     *    restores that 1 and the tactical confirm arm (0x160FC/0x16104, Inv_CloseAndCommit 0x15a48)
     *    commits the deploy (0x39980, spread 0x39E9C) and clears the screen (0x15F34); the frame dumps
     *    the battlefield. The escape seam (0x162BC/0x162C8) then returns to the geoscape -- measured:
     *    the list STANDS (Tact_Shutdown(0) never reaches the DeleteAll at 0x3E154, Part 93) and the
     *    craft's own entry survives visible. */
    {
        Ship      *pCraft = NULL;
        UiControl *pCtlOk     = g_world.ui.apCtl[GEOUI_CTL_186BB0];
        UiControl *pCtlCancel = g_world.ui.apCtl[GEOUI_CTL_186BB4];
        i32 nShipIdx = -1, nMarkIdx = -1, nDiskIdx = -1;
        i32 nModeBefore = g_world.bMapMissionMode;
        i32 nTypeBefore = g_world.nMapMissionType;
        i32 nTickBefore = g_world.geo.world.nTickSeconds;
        i32 abSpriteBefore[4];
        i32 nOverlayAtDisc;
        i32 nSoldiers = -1;
        i32 nCountBeforePress;
        i32 nPasses = 0, nArriveFlag = -1, i;
        const i32 nPassMax = 400;

        printf("part 100.5: the descent offer decides -- Отмена, re-order, ОК into the urban assault\n");

        /* The standing state, read off the live cells. */
        CheckEqI32(UiLineList_GetFlag(&g_world.msgLines, 0), 21,
                   "100.4's arrival left the flag-21 line standing (0x80774)");
        CheckEqI32(g_world.geo.time.bPaused, 1, "and the arrival's pause by store (0x80684)");
        Check(pCtlOk != NULL && pCtlCancel != NULL, "both card controls are live (0x186BB0, 0x186BB4)");
        if (pCtlOk != NULL) {
            Check(pCtlOk->pfnClick == g_world.frame.panel.pfnBeginCraftSelect
                      && pCtlOk->y == PUFO_STATUS_SITECTL_Y_CANCEL,
                  "ОК carries the craft-select handler at y 105 (0x917F8, 0x9180C)");
        }
        if (pCtlCancel != NULL) {
            Check(pCtlCancel->pfnClick == g_world.frame.panel.pfnResolveInterceptUI
                      && pCtlCancel->y == PUFO_STATUS_SITECTL_Y_OK2,
                  "and Отмена carries the resolve handler at y 128 (0x918B0, 0x918C4)");
        }

        /* The chasing craft is 100.4's -- its target byte still names the disc. */
        for (i = 0; i < (i32)g_world.bCraftCount; ++i) {
            Ship *pS = g_world.apCraft[i];
            if (pS != NULL && pS->bIntercepting != 0) {
                nShipIdx = i;
                pCraft   = pS;
                break;
            }
        }
        Check(nShipIdx >= 0 && pCraft != NULL && pCraft->bAtBase == 0 && pCraft->bWeaponsHot == 1,
              "exactly one craft is mid-intercept, hot and off base (0x5A790, 0x5A7A4, 0x5A7AC)");
        if (pCraft != NULL) {
            nDiskIdx = (i32)pCraft->nStrategIdx; /* 0x5A79C */
            nMarkIdx = FindStrategById(&g_world.strategList, (i32)pCraft->bId);
        }
        {
            const StrategItem *const pDisk =
                (nDiskIdx >= 0) ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx) : NULL;
            Check(pDisk != NULL && pDisk->nType == 3 && pDisk->bActive == 1,
                  "the disc stands at the craft's target index (0x5A79C names 0x835B4's item)");
            Check(nMarkIdx >= 0, "and the marker entry is on the list (0x2CB90's spawn entry)");
        }
        memcpy(abSpriteBefore, g_world.abMapSpriteSet, 4);
        nSoldiers = Craft_CountSoldiersAboard(g_world.apSoldiers, g_world.nSoldierCount, nShipIdx);
        nOverlayAtDisc = -1;
        if (pCraft != NULL) {
            const StrategItem *const pDisk =
                (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx);
            const PadImage *aOv[3] = {
                g_world.ui.apPad[GEOUI_PAD_186C28], g_world.ui.apPad[GEOUI_PAD_186C1C],
                g_world.ui.apPad[GEOUI_PAD_186BB8] /* the three zoom overlays, 0x186C28/1C/18 */
            };
            if (pDisk != NULL) {
                nOverlayAtDisc = Smap_GetOverlayAtMode(&g_world.view, aOv,
                                                       g_world.frame.g.nOverlayOriginX,
                                                       pDisk->nX, pDisk->nY);
            }
        }
        printf("  before the answers: mode %d, type %d, tick %d, overlay at the disc %d, soldiers aboard %d\n",
               nModeBefore, nTypeBefore, nTickBefore, nOverlayAtDisc, nSoldiers);
        Check(nSoldiers > 0, "the craft carries soldiers -- why the arrival card put BeginCraftSelect on ОК (CraftSelectGate 0x91868)");

        /* A. Отмена. */
        PressCtl(pCtlCancel);
        CheckEqI32(g_world.geo.world.nTickSeconds, 5,
                   "the resolve head reset the tick to 5 s (0x84968)");
        CheckEqI32(g_world.bInterceptDone, 0, "and cleared bInterceptDone (0x84984)");
        CheckEqI32(g_world.msgLines.nLineCount, 0,
                   "the dismiss dropped the card line and the unconditional Remove(0) found nothing to eat (0x84454, 0x84A80)");
        CheckEqI32(g_world.geo.time.bPaused, 0,
                   "an empty panel unpaused the clock (0x84A84)");
        Check(g_world.bFlag187090 == 0 && g_world.bFlag18708C == 0 && g_world.bFlag187098 == 0
                  && g_world.bMapSelectionFlag == 0,
              "the clear block ran before the dismiss (0x84A24..0x84A3C)");
        if (pCraft != NULL) {
            CheckEqI32((i32)pCraft->bIntercepting, 0, "the retarget cleared the intercept byte (0x5A59C)");
            Check(pCraft->bAtBase == 0 && pCraft->bWeaponsHot == 1,
                  "a recall is not a park: only the visibility pass touches those bytes (0x8393C), and it skipped a hot craft off base");
            {
                StrategItem *const pMark = (nMarkIdx >= 0)
                                               ? (StrategItem *)PtrArray_Get(&g_world.strategList, nMarkIdx)
                                               : NULL;
                const BaseRec *const pHome = (pCraft->bHomeBase < PUFO_FAC_BASE_COUNT)
                                                 ? g_world.apBases[pCraft->bHomeBase] : NULL;
                Check(pMark != NULL && pMark->bVisible == 1 && pMark->nTimer == 0
                          && (i32)pMark->nMissionSub == 1,
                      "the homing leg lights the entry (0x7F638's bB 1), restarts the timer (0x5A524) and arms the landing byte nMissionSub 1 (0x7F648)");
                Check(pMark != NULL && pHome != NULL && pMark->nDestX == pHome->nUnk8
                          && pMark->nDestY == pHome->nUnk12,
                      "and its destination is the home base cell (0x5A570 reads BaseRec +8/+12)");
            }
        }
        {
            const StrategItem *const pDisk =
                (nDiskIdx >= 0) ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx) : NULL;
            Check(pDisk != NULL && pDisk->nType == 3 && pDisk->bActive == 1,
                  "the disc SURVIVED the recall -- only ОК consumes it (0x84A58 dismisses, 0x83F7C removes)");
        }
        Check(g_world.bMapMissionMode == nModeBefore && g_world.nMapMissionType == nTypeBefore,
              "and the mission header did not move: Отмена writes nothing there");
        Check(g_world.bTactLive == 0, "no mission started (0x83AC4 is the ОК tail's call, 0x83AC4)");

        /* B. Re-order the same craft at the same disc, then the arrival again. */
        {
            const StrategItem *const pDisk =
                (nDiskIdx >= 0) ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx) : NULL;
            const i32 nLaunch = Ship_LaunchIntercept(&g_world.shipEnv, nShipIdx, nDiskIdx);
            Check(nLaunch >= 0, "Ship_LaunchIntercept @ 0x5a64c took the order (the 0x8427C call is 100.4's, proven there)");
            if (pDisk != NULL && pCraft != NULL) {
                const StrategItem *const pMark =
                    (nMarkIdx >= 0) ? (StrategItem *)PtrArray_Get(&g_world.strategList, nMarkIdx) : NULL;
                Check(pMark != NULL && pMark->bVisible == 0 && pMark->nTimer == 0
                          && pMark->nStartX == pMark->nX && pMark->nStartY == pMark->nY
                          && (i32)pCraft->bIntercepting == 1 && (i32)pCraft->bWeaponsHot == 1
                          && (i32)pCraft->bAtBase == 0,
                      "the launch left the entry dark (0x7F638's bB 0), restarted the leg timer (0x5A67C), and left the leg at the CURRENT cell -- the 0x5A718 snap needs bAtBase, and this craft is mid-flight (0x5A6A4)");
            }
        }
        World_PulseGeoscape(&g_world);
        {
            StrategItem *const pMark = (nMarkIdx >= 0)
                                           ? (StrategItem *)PtrArray_Get(&g_world.strategList, nMarkIdx) : NULL;
            Check(pMark != NULL && pMark->bVisible == 1,
                  "the pulse lights the entry of the craft that is off base (0x955C0, 0x955C8)");
        }
        while (nPasses < nPassMax && nArriveFlag < 0) {
            const StrategItem *const pDisk = (nDiskIdx >= 0)
                                                 ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx)
                                                 : NULL;
            if (pDisk == NULL) {
                printf("  pass %d: the disc is gone (0x7FD14 expiry)\n", nPasses);
                break;
            }
            ++nPasses;
            g_world.geoHooks.pfnProcessEvents(0, g_world.geoHooks.pUser);
            if (g_world.msgLines.nLineCount > 0) {
                const i32 nFlagNow = (i32)UiLineList_GetFlag(&g_world.msgLines, 0);
                if (nFlagNow == 21 || nFlagNow == 13) {
                    nArriveFlag = nFlagNow;
                    break;
                }
                printf("  pass %d: stray line %d removed\n", nPasses, nFlagNow);
                (void)UiLineList_Remove(&g_world.msgLines, 0);
            }
        }
        CheckEqI32(nArriveFlag, 21,
                   "the short leg re-arrived and the type-3 arm posted the offer again (0x80348, 0x80684, 0x80774)");
        CheckEqI32(g_world.geo.time.bPaused, 1, "and paused by store (0x80684)");
        if (nShipIdx >= 0 && nDiskIdx >= 0) {
            CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 0), nShipIdx,
                       "line 0 slot 0 names the ship (0x80774's a3)");
            CheckEqI32((i32)UiLineList_GetColor(&g_world.msgLines, 0, 1), nDiskIdx,
                       "and slot 1 the disc (its a4)");
        }
        ClearSurface();
        (void)DrawFrame();
        Check(pCtlOk->pfnClick == g_world.frame.panel.pfnBeginCraftSelect
                  && pCtlOk->y == PUFO_STATUS_SITECTL_Y_CANCEL,
              "the rebuilt card seats the launch control again (0x917F8, 0x9180C)");

        /* C. ОК -- the mode switch and the mission. */
        nCountBeforePress = PtrArray_GetCount(&g_world.strategList);
        i32 nDiskX = -1, nDiskY = -1;
        {
            const StrategItem *const pDisk =
                (nDiskIdx >= 0) ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nDiskIdx) : NULL;
            if (pDisk != NULL) { nDiskX = pDisk->nX; nDiskY = pDisk->nY; }
        }
        PressCtl(pCtlOk);

        {
            i32 bPark = 1;
            for (i = 0; i < (i32)g_world.bCraftCount; ++i) {
                const Ship *const pS = g_world.apCraft[i];
                const i32 nOwnIdx = (pS != NULL) ? FindStrategById(&g_world.strategList, (i32)pS->bId) : -1;
                const StrategItem *const pOwn = (nOwnIdx >= 0)
                                                    ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nOwnIdx)
                                                    : NULL;
                /* Park (0x8393C/0x83950) then the cargo pass's at-home re-arm (0x39114..0x39138) --
                 * but the re-arm loop sits in the base-assault arm the mission-type test selects
                 * (0x38FB8 CMP Map_nMissionType,#3, BEQ 0x39044): an urban run (type 5) takes the
                 * craft-cargo copy at 0x38FFC instead, so a parked craft STAYS cold here. The
                 * re-arm chain is a type-3 fact (the 0x840FC assault arm writes that type). */
                const i32 bWantHot = (pS != NULL && g_world.nMapMissionType == 3
                                          && (i32)pS->bHomeBase == (i32)g_world.nCraftHomeBase
                                          && pS->bAtBase != 0) ? 1 : 0;
                if (pS == NULL || i == nShipIdx) {
                    continue;
                }
                if (pS->bAtBase != 1 || pS->bWeaponsHot != bWantHot) { bPark = 0; }
                if (pOwn != NULL && pOwn->bVisible != 0 && pOwn->nType == 1) { bPark = 0; }
            }
            Check(bPark, "the visibility pass parked every idle craft at base and hid its entry (0x8393C, 0x83950, 0x83968); the cargo pass's at-home re-arm (0x39114..0x39138) only runs in the type-3 base-assault arm (0x38FB8 CMP #3) this urban run never enters");
            Check(pCraft->bWeaponsHot == 1 && pCraft->bAtBase == 0,
                  "while the chasing one passed the pass untouched (0x83938's test) and the tail recalled it afterwards (0x83AA0)");
        }
        CheckEqI32(g_world.nSelectedShipIdx, nShipIdx, "the selection pass read line slot 0 into 0x187054 (0x839CC)");
        CheckEqI32(g_world.nMsgShipIdx, nShipIdx, "  0x159E74 (0x839D0)");
        CheckEqI32(g_world.nMapCraftIndex, nShipIdx, "  and Map_nCraftIndex (0x839DC)");
        CheckEqI32(g_world.nSelectedStrategIdx, nDiskIdx, "line slot 1 into 0x186BE4 (0x839E4)");
        Check((nOverlayAtDisc < 0x32) ? g_world.bMapUnk1511E == 0 : g_world.bMapUnk1511E == 1,
              "unk_128AD6 answers the overlay read at the disc cell (0x83B54 read the three zoom pads at 0x83B44)");
        Check(memcmp(abSpriteBefore, g_world.abMapSpriteSet, 4) == 0,
              "the marker's bRace 255 is outside the five race rows, so the sprite set stands (0x83BB0..0x83C40)");

        CheckEqI32(g_world.nMapMissionType, 5, "ОК switched the mission TYPE to Urban (0x83F28)");
        CheckEqI32(g_world.bMapMissionMode, 3, "and the MODE to 3 (0x83F34) -- the switch Отмена never made");
        CheckEqI32(g_world.bMapUnk15115, 0, "the damage band is cleared for an urban run (0x83F44)");
        CheckEqI32(g_world.nMapTerrainVariant, 0, "  the terrain variant (0x83F50)");
        CheckEqI32(g_world.nMapMapLayers, (i32)pCraft->bCraftType + 1,
                   "  and the layer count comes off the selected craft's class (0x83F68)");
        CheckEqI32(PtrArray_GetCount(&g_world.strategList), nCountBeforePress - 1,
                   "the offer consumed its site: the disc is removed from the list (0x83F7C)");
        {
            i32 bGone = 1;
            for (i = 0; i < PtrArray_GetCount(&g_world.strategList); ++i) {
                const StrategItem *const pItem =
                    (const StrategItem *)PtrArray_Get(&g_world.strategList, i);
                if (pItem != NULL && pItem->nType == 3 && pItem->bActive != 0
                    && pItem->nX == nDiskX && pItem->nY == nDiskY) { bGone = 0; }
            }
            Check(bGone, "no active type-3 item stands at the disc's cell any more (0x83F7C removed it)");
        }
        Check(g_world.bMapSelectionFlag == 0, "the tail cleared the selection byte (0x83AB4)");
        Check(g_world.bMapTickHold == 0 && g_world.bMapBattleEnded == 0,
              "and never reached the bad-end pair -- that block is the mode-1 assault's (0x84238, 0x84240)");
        Check(g_world.msgLines.nLineCount == 0,
              "the tail's DismissSelected dropped the offer line (0x83AA0, 0x84454)");
        CheckEqI32((i32)pCraft->bIntercepting, 0, "with the craft recalled by the same call (0x84448 -> 0x5A59C)");
        CheckEqI32(g_world.bGeoscapeUi, 0,
                   "GeoscapeUi_FreeAll(0) cleared the geoscape flag -- the write that opens the tick's tactical branch (0x86D34)");

        Check(g_world.bTactLive == 1 && g_world.bTactFlag == 1,
              "the tail ran Tact_InitMission(0) (0x83AC4) and the urban mission is live");
        Check(g_world.pTactMap != NULL && (i32)g_world.pTactMap->nMissionType == 5,
              "the frame's own header carries the type ОК picked (0x83F28)");
        const i32 nXcomFielded = (g_world.pTactMap != NULL) ? (i32)g_world.pTactMap->nXcomCount : -1;
        const i32 nEnemyFielded = (g_world.pTactMap != NULL) ? (i32)g_world.pTactMap->nEnemyCount : -1;
        printf("  the mission fielded %d XCOM units against %d enemies (Map_nXcomUnitCount 0x1511F, Map_nEnemyUnitCount 0x15124), %d deployed by the cargo pass (0x15140)\n",
               nXcomFielded, nEnemyFielded, g_world.nMapDeployedHwps);
        Check(g_world.pTactMap != NULL && g_world.pTactMap->nXcomCount > 0
                  && g_world.pTactMap->nEnemyCount > 0,
              "the mission fielded both sides (Map_nXcomUnitCount 0x1511F, Map_nEnemyUnitCount 0x15124)");
        Check(g_world.pTactMap != NULL && (i32)g_world.pTactMap->nXcomCount == nSoldiers
                  && g_world.nMapDeployedHwps == 0,
              "the XCOM side is exactly the embarked crew -- no hardware was deployed (Map_nDeployedHwps @ 0x15140)");

        /* The launch hand-over passes nMode 0 to Tact_InitMission (0x83AC4), and the briefing gate
         * tests that ARGUMENT together with Map_bSpawnAlt (0x3D28C, 0x3D29C) -- Map_bMissionMode 3
         * does not gate it. The Cydonia briefing therefore stands: Map_nUiScreen 20 (0x3D2B0), the
         * pre-launch screen saved into apUiSlotsA[11] (0x3D2A4/0x3D2A8), the pad loaded (0x3D2C0..
         * 0x3D2E0). Its cancel arm is World_MissionCancelEnd's 0x14/0x15 case: the saved screen
         * comes back out of slot 11 (0x16294) and the mission is left standing. */
        CheckEqI32(g_world.nUiScreen, 0x14, "the briefing gate fired: spawnAlt is 1 and the init argument is 0 (0x3D28C, 0x3D29C, 0x3D2B0)");
        Check(g_world.pTactBriefPad != NULL, "the cydonia pad was allocated and loaded (0x3D2C0..0x3D2E0)");
        CheckEqI32(g_world.nMapUiSlot11, 1,
              "slot 11 saves 1: the prologue cleared Map_nUiScreen (0x3C67C), but the deploy walk reaches Inv_OpenScreen (0x3D0B8) and that store saves the 1 it left (0x3D29C)");
        ClearSurface();
        World_TactFrame(&g_world);
        DumpFrame("tact_briefing_urban");
        Check(World_MissionCancelEnd(&g_world), "the mission's cancel arm takes the 0x14/0x15 case (0x16294)");
        CheckEqI32(g_world.nUiScreen, 1, "and restores the screen slot 11 saved (1): the briefing closes onto the inventory the init left open");

        /* The shipped build then closes that inventory with its own confirm arm:
         * MenuBtn_Confirm(16) -> Input_OnConfirm's screen-1 arm (0x160FC plays 16, 0x16104 calls),
         * whose Inv_CloseAndCommit @ 0x15a48 snapshots the loadouts, clears the Map+0x01 commit flag
         * (0x15C68), runs Tact_DeployTeamAndHwps @ 0x39980 (phase 3 at 0x39E9C spreads the squad that
         * shared one cell), then clears Map_nUiScreen (0x15F34). The battlefield is what screen 0
         * paints. */
        CheckEqI32(g_world.bMapUnk1139B9, 1, "Map+0x01 is still up for this new mission (0x3D08C) -- that byte is what makes the commit do its work");
        {
#define CLICK(i) do { if (g_bank.aCtl[i].pfnClick != NULL) { g_bank.aCtl[i].pfnClick(); } } while (0)
            CLICK(98); /* MenuBtn_Confirm(16), the arm's gate is the open screen 1 */
#undef CLICK
        }
        CheckEqI32(g_world.nUiScreen, 0, "the arm closes the inventory and the callee tail clears Map_nUiScreen (0x160FC, 0x16104, 0x15F34)");
        CheckEqI32(g_world.bMapUnk1139B9, 0, "the commit flag is cleared before the deploy runs (0x15C68)");
        Check(g_world.pTactMap != NULL && (i32)g_world.pTactMap->nXcomCount == nSoldiers
                  && g_world.pTactMap->nEnemyCount == nEnemyFielded,
              "the mission still fields the crew against the aliens through the deploy commit (0x39980)");

        /* The battlefield picture: the frame the live mission paints. */
        ClearSurface();
        World_TactFrame(&g_world);
        DumpFrame("tact_mission_urban");

        /* D. Back to the geoscape through the proven return arm. */
        World_TactEscapeToGeoscape();
        Check(g_world.bTactLive == 0 && g_world.bTactFlag == 0,
              "the escape seam runs Tact_Shutdown(0) and leaves geoscape mode (0x162BC, 0x162C8)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 0, "and the map screen rebuilds (0x162C8's InitUI(1))");
        Check(g_world.ui.apCtl[GEOUI_CTL_186BB0] != NULL && g_world.ui.apCtl[GEOUI_CTL_186BB4] != NULL
                  && g_world.ui.pStatusPanel != NULL,
              "the thirteen root controls came back, freshly allocated (0x86100..0x864DC)");
        Check(PtrArray_GetCount(&g_world.strategList) > 0,
              "the return arm leaves the standing map: Tact_Shutdown(0) never reaches the DeleteAll at 0x3E154 (Part 93)");
        {
            const i32 nOwnIdx = FindStrategById(&g_world.strategList, (i32)pCraft->bId);
            const StrategItem *const pOwn = (nOwnIdx >= 0)
                                                ? (const StrategItem *)PtrArray_Get(&g_world.strategList, nOwnIdx)
                                                : NULL;
            printf("  after the return: craft %d atBase %d hot %d, its entry %s (idx %d, vis %d, act %d)\n",
                   nShipIdx, (i32)pCraft->bAtBase, (i32)pCraft->bWeaponsHot,
                   (nOwnIdx >= 0) ? "stands" : "is gone", nOwnIdx,
                   (pOwn != NULL) ? (i32)pOwn->bVisible : -1, (pOwn != NULL) ? (i32)pOwn->bActive : -1);
        }
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("geoscape_after_mission");

        printf("  descent decision: Отмена recalled craft %d with the header at mode %d/type %d; the re-order + ОК switched the mission %d -> mode 3 type 5 (Urban), consumed the disc at %d, and fielded %d XCOM vs %d aliens on the live map\n",
               nShipIdx, nModeBefore, nTypeBefore, nModeBefore, nDiskIdx, nXcomFielded, nEnemyFielded);
    }

    /* ---- section 100.6: the map's own traffic -- the replacement arm spawns a UFO, it flies a
     *      rolled leg, its site-mission landing stamps a terror site the panel captions and the map
     *      draws, and the base entry stands on the map through every anim phase
     *      (StrategList_MaybeSpawnCraft_Process @ 0x7e420 / StrategList_RollCraftStats @ 0x81a9c /
     *      Geoscape_SpawnMissionSite @ 0x7e49c: MissionPickLandTarget 0x7E6F0-ish re-roll, MissionDwell
     *      0x7E9A0-ish, MissionLanding 0x7EAD0..0x7EB14, StrategList_AddAlienSiteIfValid @ 0x7e3d8;
     *      Smap_DrawStrategIcons @ 0x822c4 type-1 rows loc_8259C, type-0 active+visible row the
     *      +6 block at loc_82650, type-0 inactive row the +anim block at loc_8268C) -----------------
     *
     * A. Spawn. StrategList_MaybeSpawnCraft_Process with bSpawn 1 is the arm both callers reach:
     *    Geoscape_TickHourly's `CountActive < 2 && Game_RandU16(4) == 1 -> ProcessEvents(1)` and
     *    StrategList_RemoveAt's replacement loop (0x82B50/0x82B70, `3 - active` rolls of 1/3) --
     *    the world wires it as World_HookProcessEvents. The arm is AddUfoAt at a rolled cell
     *    (0x7E440/0x7E448, x drawn first) + RollCraftStats (race/class from the difficulty rows,
     *    the 0x81B7C early cap, mission sub from the class row, 0x81BDC hands it to World_SpawnMission
     *    which starts it: nPhase 3 for sub 0, 4 otherwise) + the event pass tail.
     * B. Flight. An active item's position is re-derived every event pass at LABEL_84
     *    (0x809C0..0x80DD0): nX = nStartX +/- elapsed minutes * nTravelStartSec / uTravelMin --
     *    the accumulator block 0x80418..0x80504 belongs to the intercept branch, not to this path.
     *    At the 5-second tick one cell costs ~340 game seconds, so the section presses the live
     *    3600-second speed button (root control 7, GameTime_SetSpeedIdx5 -- Part 100.2) and the
     *    UFO walks ~10 cells per pulse. The glyph proof toggles bVisible around one probe window:
     *    the two draws differ only in that byte, so the diff is exactly this item's glyph row.
     * C. Terror site. The engine's own mission advance (SpawnMissionSite(pItem, -1), the call
     *    0x809B8 makes) walks the phase gates: the land pick + travel leg, MissionDwell stamps
     *    g_anMissionDwell[sub] (2880 for sub 4) and nPhase 2, and a spent timer opens MissionLanding --
     *    the 0x14+1 dice at 0x7EAD0: 16 or less posts an undiscovered site, 17..20 the discovered one
     *    with the flag-60 line and GameTime_Pause 0x7EB14. AddAlienSiteIfValid (0x7E3D8 rejects ocean,
     *    0x7E404 stores class 255) makes it a type-0 active item, and MissionTagLastItem stamps the
     *    race and target (0x7EB48/0x7EB64). UpdateMotion's visible-item pass then trades the carried
     *    50000 for a real nSiteId off nNextSiteId -- the number the panel row captions with TEXTS[1146]
     *    (0x82FE4/0x8301C, the pair 100.4 pinned) and the +6 glyph row paints the cell.
     * The fixture shortens the travel bookkeeping (uTravelMin, the dwell timer, nPhase 2) the way the
     * per-pass timer decrement 0x7FD24 and the 0x80560 elapsed gate spend them in a running game --
     * stated at each step; everything else runs on the game's own functions and cells. */
    {
        SpawnMissionCtx missionCtx;
        StrategItem *pUfo     = NULL;
        StrategItem *pSite    = NULL;
        i32 nCount0 = 0, nCount1 = 0, nIdxUfo = -1, nIdxSite = -1, i;
        i32 nSiteId0 = -1, nPx = 0, nPy = 0;
        i32 nOldX = 0, nOldY = 0, nPulses = 0, nMotionPulses = 0, nTries = 0;
        i32 nBaseIdx = -1, nSub = -1;
        u16 aSnapA[16 * 16], aSnapB[16 * 16];

        printf("part 100.6: the game spawns a UFO, it flies, and its landing stamps a terror site\n");

        nCount0 = PtrArray_GetCount(&g_world.strategList);
        printf("  the list at 100.6's start (%d), paused %d, tick %d:\n",
               nCount0, (i32)g_world.geo.time.bPaused, g_world.geo.world.nTickSeconds);
        for (i = 0; i < nCount0; ++i) {
            const StrategItem *p = (const StrategItem *)PtrArray_Get(&g_world.strategList, i);
            if (p == NULL) {
                continue;
            }
            printf("    idx %d: type %d act %d vis %d dispId %d siteId %d sub %d phase %d timer %d cell %d,%d race %d class %d\n",
                   i, (i32)p->nType, (i32)p->bActive, (i32)p->bVisible, p->nDisplayId, p->nSiteId,
                   (i32)p->nMissionSub, p->nPhase, p->nTimer, p->nX, p->nY, (i32)p->bRace,
                   (i32)p->bCraftClass);
            if (p->nType == 0 && p->bActive == 0) {
                nBaseIdx = i;
            }
        }

        /* A. The game's own replacement arm, on the world's own spawn context. */
        (void)StrategList_MaybeSpawnCraft_Process(&g_world.strategList, g_world.spawnCtx.pnSeed,
                                                  g_world.spawnCtx.nZoomMode, g_world.spawnCtx.pTables,
                                                  g_world.nMonthCounter, (i32)g_world.nAlienPoolAddend,
                                                  1, g_world.spawnCtx.pfnSpawnMission,
                                                  g_world.spawnCtx.pfnProcessEvents,
                                                  g_world.spawnCtx.pUser);
        nCount1 = PtrArray_GetCount(&g_world.strategList);
        Check(nCount1 > nCount0,
              "the arm put a new item on the list (0x7E46C AddUfoAt + 0x81A9C RollCraftStats)");
        for (i = nCount0; i < nCount1; ++i) {
            const StrategItem *p = (const StrategItem *)PtrArray_Get(&g_world.strategList, i);
            if (p != NULL && p->nType == 1 && p->bActive == 1) {
                nIdxUfo = i;
            }
        }
        pUfo = (nIdxUfo >= 0) ? (StrategItem *)PtrArray_Get(&g_world.strategList, nIdxUfo) : NULL;
        Check(pUfo != NULL && pUfo->nType == 1 && pUfo->bActive == 1 && pUfo->bVisible == 0
                  && pUfo->nDisplayId == 50000 && pUfo->nSiteId == -1 && pUfo->nHp == -1,
              "it is the alien shape AddUfoAt builds: type 1, active, invisible, id 50000, no site id, hp -1 (0x7E3AC, 0x82990..0x82A74)");
        if (pUfo != NULL) {
            nSub = (i32)pUfo->nMissionSub;
            printf("  the rolled craft: race %d class %d mission sub %d phase %d cell %d,%d dest %d,%d\n",
                   (i32)pUfo->bRace, (i32)pUfo->bCraftClass, nSub, pUfo->nPhase,
                   pUfo->nX, pUfo->nY, pUfo->nDestX, pUfo->nDestY);
            Check(nSub >= 0 && nSub <= 7 && pUfo->nPhase == ((nSub == 0) ? 3 : 4),
                  "RollCraftStats rolled a mission sub off the class row and World_SpawnMission started it: phase 3 for sub 0, 4 otherwise (0x81B7C..0x81BDC, 0x3D28C-side start arm 0x7E4CC)");
            Check(pUfo->nStartX == pUfo->nX && pUfo->nStartY == pUfo->nY
                      && (pUfo->nDestX != pUfo->nX || pUfo->nDestY != pUfo->nY),
                  "the start leg is the MissionRollTravel roll: start == spawn, a rolled destination on the 20..780 / 20..360 field (0x7E440/0x7E448, 0x82964..0x82984)");
            Check(pUfo->dSpeed > 0.0 && pUfo->dTrackA > 0.0
                      && pUfo->dTrackA == pUfo->dSpeed * 0.02,
                  "the speed took the active type-1 arm and dTrackA is dSpeed*0.02 (0x7F74C..0x7F7CC, 0x7FA3C)");
            printf("    dSpeed %f, dTrackA %f, uTravelMin %d, dir %d\n",
                   pUfo->dSpeed, pUfo->dTrackA, (i32)pUfo->uTravelMin, (i32)pUfo->bTravelDir);

            /* B. Flight. The live speed button first: at 5 s per pulse the interpolation needs
             * ~340 game seconds to tick one cell over this 1065-minute leg. */
            PressCtl(g_world.ui.apRootCtl[7]); /* root control 7 = GameTime_SetSpeedIdx5 (0x85124 chain) */
            CheckEqI32(g_world.geo.world.nTickSeconds, 3600,
                       "the fastest speed button is pressed live (0x84F00..0x85158, Part 100.2)");
            g_world.view.nScrollX = pUfo->nX - g_world.view.nMapW / 2 - 40;
            g_world.view.nScrollY = pUfo->nY - g_world.view.nMapH / 2 + 40;
            Check(ScreenForCell(&g_world.view, pUfo->nX, pUfo->nY, &nPx, &nPy)
                      && nPx == 160 && nPy == 120,
                  "the spawn cell sits dead centre of the map view after the fixture's scroll pair");
            nOldX = pUfo->nX;
            nOldY = pUfo->nY;
            while (nPulses < 40 && pUfo->nX == nOldX && pUfo->nY == nOldY) {
                World_PulseGeoscape(&g_world);
                ++nPulses;
            }
            Check(pUfo->nX != nOldX || pUfo->nY != nOldY,
                  "the UFO flew: LABEL_84 re-derives the position from nStartX + elapsed minutes * nTravelStartSec / uTravelMin every pass (0x809C0..0x80DD0)");
            printf("  after %d pulse(s): cell %d,%d (from %d,%d), dest %d,%d, timer %d, phase %d\n",
                   nPulses, pUfo->nX, pUfo->nY, nOldX, nOldY, pUfo->nDestX, pUfo->nDestY,
                   pUfo->nTimer, pUfo->nPhase);
            nMotionPulses = nPulses;
            /* The glyph at the new cell: two draws that differ only in the entry's bVisible byte,
             * so the diff can only be this item's own glyph row (type 1 active: the animated row at
             * 0x8263C or the static row at 0x82620, behind the visible test in the loc_8259C arm). */
            pUfo->bVisible = 1; /* fixture: the byte the proximity store 0x811c8 writes, proven live in section 16 */
            Check(ScreenForCell(&g_world.view, pUfo->nX, pUfo->nY, &nPx, &nPy),
                  "the flown-to cell stands on the map view");
            g_world.view.nScrollX = pUfo->nX - g_world.view.nMapW / 2 - 40;
            g_world.view.nScrollY = pUfo->nY - g_world.view.nMapH / 2 + 40;
            (void)ScreenForCell(&g_world.view, pUfo->nX, pUfo->nY, &nPx, &nPy);
            {
                i32 nX0 = nPx, nY0 = nPy, nDiff = 0;
                if (nX0 + 16 > PUFO_SURF_W) { nX0 = PUFO_SURF_W - 16; }
                if (nY0 + 16 > PUFO_SURF_H) { nY0 = PUFO_SURF_H - 16; }
                ClearSurface();
                (void)DrawFrame();
                for (i = 0; i < 16; ++i) {
                    memcpy(&aSnapB[i * 16],
                           &((const u16 *)g_assets.pSurface->pPixels)
                               [(size_t)(nY0 + i) * (size_t)g_assets.pSurface->nWidth + (size_t)nX0],
                           16 * sizeof(u16));
                }
                pUfo->bVisible = 0;
                ClearSurface();
                (void)DrawFrame();
                for (i = 0; i < 16; ++i) {
                    memcpy(&aSnapA[i * 16],
                           &((const u16 *)g_assets.pSurface->pPixels)
                               [(size_t)(nY0 + i) * (size_t)g_assets.pSurface->nWidth + (size_t)nX0],
                           16 * sizeof(u16));
                }
                pUfo->bVisible = 1;
                for (i = 0; i < 16 * 16; ++i) {
                    if (aSnapA[i] != aSnapB[i]) { ++nDiff; }
                }
                printf("  the entry's glyph window at %d,%d holds %d of 256 pixels\n", nX0, nY0, nDiff);
                Check(nDiff > 0, "the flown UFO's glyph is painted at its cell by the type-1 visible row (0x8C694/0x8C698, 0x8259C's arm)");
            }
            ClearSurface();
            (void)DrawFrame();
            DumpFrame("geoscape_ufo_enroute");

            /* D. The base entry stands on the map with no visibility test at all: the type-0
             * inactive row draws nBase + anim (loc_8268C's block). Two anim phases paint two
             * frames of the same glyph; a cell that holds such an item must change between them. */
            if (nBaseIdx >= 0 && g_world.pnAnimPhase != NULL) {
                const StrategItem *const pBase =
                    (const StrategItem *)PtrArray_Get(&g_world.strategList, nBaseIdx);
                if (pBase != NULL && ScreenForCell(&g_world.view, pBase->nX, pBase->nY, &nPx, &nPy)) {
                    i32 nX0 = nPx, nY0 = nPy, nDiff = 0;
                    g_world.view.nScrollX = pBase->nX - g_world.view.nMapW / 2 - 40;
                    g_world.view.nScrollY = pBase->nY - g_world.view.nMapH / 2 + 40;
                    (void)ScreenForCell(&g_world.view, pBase->nX, pBase->nY, &nPx, &nPy);
                    nX0 = nPx; nY0 = nPy;
                    if (nX0 + 16 > PUFO_SURF_W) { nX0 = PUFO_SURF_W - 16; }
                    if (nY0 + 16 > PUFO_SURF_H) { nY0 = PUFO_SURF_H - 16; }
                    *g_world.pnAnimPhase = 0;
                    ClearSurface();
                    (void)DrawFrame();
                    for (i = 0; i < 16; ++i) {
                        memcpy(&aSnapA[i * 16],
                               &((const u16 *)g_assets.pSurface->pPixels)
                                   [(size_t)(nY0 + i) * (size_t)g_assets.pSurface->nWidth + (size_t)nX0],
                               16 * sizeof(u16));
                    }
                    *g_world.pnAnimPhase = 3;
                    ClearSurface();
                    (void)DrawFrame();
                    for (i = 0; i < 16; ++i) {
                        memcpy(&aSnapB[i * 16],
                               &((const u16 *)g_assets.pSurface->pPixels)
                                   [(size_t)(nY0 + i) * (size_t)g_assets.pSurface->nWidth + (size_t)nX0],
                               16 * sizeof(u16));
                    }
                    for (i = 0; i < 16 * 16; ++i) {
                        if (aSnapA[i] != aSnapB[i]) { ++nDiff; }
                    }
                    printf("  the base entry at cell %d,%d changes in %d of 256 window pixels between anim 0 and anim 3\n",
                           pBase->nX, pBase->nY, nDiff);
                    Check(nDiff > 0,
                          "the base glyph stands on the map and animates -- the type-0 inactive row has no visibility test (0x822c4's loc_8268C arm, nBase + anim)");
                } else {
                    printf("  the base entry is off the map view -- its on-map glyph is not probed here\n");
                }
            } else {
                printf("  no type-0 inactive entry stands in this world -- base visibility rests on newbase_created (Part 100.3)\n");
            }

            /* C. The terror chain, on the engine's own mission advance (SpawnMissionSite(pItem, -1)
             * is exactly what 0x809B8 calls once an item's travel time is up). The fixture shortens
             * the travel bookkeeping to the state the per-pass decrement 0x7FD24 and the 0x80560
             * elapsed gate reach in a running game, and pins the mission subtype to 4 -- a row the
             * class-weight roll 0x81B7C..0x81BDC can produce -- so the landing arm walks in this run. */
            World_FillMissionCtx(&g_world, &missionCtx);
            /* Fixture: the spawn's rolled sub 0 starts at phase 3 with a cleared target -- that
             * start dwells onto (0,0) (MissionDwell teleports nTargetX/nTargetY, and 0x7E4CC's start
             * arm clears them). The fixture pins the site subtype 4 -- a row of the class-weight
             * table 0x81B7C..0x81BDC rolls from -- and the phase 4 state that same start arm leaves
             * for every non-zero sub, so the chain runs the land pick 0x7E6F0 instead. */
            pUfo->nMissionSub = 4;
            pUfo->nPhase      = 4;
            nSiteId0 = g_world.nNextSiteId; /* nNextStrategSiteId @ 0x186548 */
            {
                const i32 nLines0 = g_world.msgLines.nLineCount;
                while (pSite == NULL && nIdxUfo >= 0 && nTries < 20) {
                    ++nTries;
                    (void)Geoscape_SpawnMissionSite(pUfo, -1, &missionCtx); /* nPhase 4: land pick + travel leg */
                    (void)Geoscape_SpawnMissionSite(pUfo, -1, &missionCtx); /* nPhase 3: MissionDwell -- timer g_anMissionDwell[4] = 2880, nPhase 2 */
                    pUfo->nPhase = 2;
                    pUfo->nTimer = 0; /* the 2880 passes of 0x7FD24, shortened */
                    nCount0 = PtrArray_GetCount(&g_world.strategList);
                    (void)Geoscape_SpawnMissionSite(pUfo, -1, &missionCtx); /* MissionLanding: the 0x14+1 dice */
                    nCount1 = PtrArray_GetCount(&g_world.strategList);
                    if (nCount1 > nCount0) {
                        const StrategItem *const pTry =
                            (const StrategItem *)PtrArray_Get(&g_world.strategList, nCount1 - 1);
                        if (pTry != NULL && pTry->nType == 0 && pTry->bActive == 1 && pTry->bVisible == 1) {
                            pSite  = (StrategItem *)pTry;
                            nIdxSite = nCount1 - 1;
                        } else if (pTry != NULL && pTry->nType == 0) {
                            printf("  try %d: the dice rolled 16 or less -- an undiscovered site at %d,%d; radar cannot reveal it in this world, so the fixture removes it and rolls again\n",
                                   nTries, pTry->nX, pTry->nY);
                            g_world.bSpawnOnce = 0;
                            (void)StrategList_RemoveAt(&g_world.strategList, &g_world.geo.world,
                                                       nCount1 - 1, &g_world.bSpawnOnce, &g_world.spawnCtx);
                            if (nIdxUfo >= nCount1) { nIdxUfo = -1; }
                        } else {
                            printf("  try %d: no item -- AddAlienSiteIfValid refused the dwelling cell (0x7E3D8)\n", nTries);
                        }
                    }
                }
                Check(pSite != NULL && nIdxSite >= 0 && nTries <= 20,
                      "the dice reached the 17..20 branch: the discovered site was stamped with its line and pause (0x7EB2C/0x7EB14)");
                if (pSite != NULL) {
                    printf("  the site stands at %d,%d (try %d): type %d act %d vis %d race %d class %d siteId %d; the UFO moved on to %d,%d\n",
                           pSite->nX, pSite->nY, nTries, (i32)pSite->nType, (i32)pSite->bActive,
                           (i32)pSite->bVisible, (i32)pSite->bRace, (i32)pSite->bCraftClass,
                           pSite->nSiteId, pUfo->nX, pUfo->nY);
                    Check(pSite->nType == 0 && pSite->bActive == 1 && pSite->bCraftClass == 255
                              && pSite->nSiteId == 50000,
                          "AddAlienSiteIfValid's shape: type 0, active, class 255 through the byte store, the carried 50000 site id (0x7E404, 0x82990..0x829F8)");
                    Check(pSite->bRace == pUfo->bRace,
                          "MissionTagLastItem stamped the landing UFO's race onto the site (0x7EB48)");
                    Check(g_world.geo.time.bPaused == 1, "the discovered landing paused the clock (GameTime_Pause 0x7EB14, 0x5d6ac)");
                    Check(g_world.msgLines.nLineCount > nLines0
                              && (i32)UiLineList_GetFlag(&g_world.msgLines, g_world.msgLines.nLineCount - 1) == 60,
                          "and appended the flag-60 line naming the new item (0x7EB2C's append)");

                    /* The visible-item pass trades the carried 50000 for a real site id off the
                     * counter (UpdateMotion @ 0x80fd8), and the UFO must be off the site cell before
                     * the glyph probe reads it. Event passes do both. */
                    /* Fixture: the clock 0x7EB14 paused runs again -- in a live game the player's
                     * next answered action does it. Neither the per-minute UpdateMotion pass (the
                     * site id) nor the interpolation (flying the UFO off the landing cell) runs
                     * while it is held. */
                    g_world.geo.time.bPaused = 0;
                    nPulses = 0;
                    while (nPulses < 40 && (pSite->nSiteId == 50000
                               || (pUfo->nX >= pSite->nX - 10 && pUfo->nX <= pSite->nX + 10
                                   && pUfo->nY >= pSite->nY - 10 && pUfo->nY <= pSite->nY + 10))) {
                        World_PulseGeoscape(&g_world);
                        ++nPulses;
                    }
                    printf("  after %d pulse(s): site id %d (counter was %d), UFO at %d,%d\n",
                           nPulses, pSite->nSiteId, nSiteId0, pUfo->nX, pUfo->nY);
                    Check(pSite->nSiteId != 50000 && pSite->nSiteId == nSiteId0,
                          "UpdateMotion's visible-item pass assigned the site id off nNextSiteId (the counter 0x18654C pair); it is what TEXTS[1145] captions with itoa");
                }
            }

            if (pSite != NULL) {
                /* The site's own map cell, clear of the UFO: centre, press, and read the row the
                 * press arm builds (TEXTS[1145] + itoa(nSiteId) -- 0x82E6C/0x8301C; the 1146 overwrite
                 * at 0x82FE4 is the type-3 disc row 100.4 pinned). */
                g_world.view.nScrollX = pSite->nX - g_world.view.nMapW / 2 - 40;
                g_world.view.nScrollY = pSite->nY - g_world.view.nMapH / 2 + 40;
                Check(ScreenForCell(&g_world.view, pSite->nX, pSite->nY, &nPx, &nPy)
                          && nPx == 160 && nPy == 120,
                      "the site cell sits dead centre of the map view");
                {
                    i32 nX0 = 152, nY0 = 112, nDiff = 0, nPhaseTry = 0;
                    /* The row index is nBase + animPhase + 6, and some sprite frames of an animated
                     * row are blanks; the probe tries the phases and keeps the first that paints. */
                    for (nPhaseTry = 0; nPhaseTry < 4 && nDiff == 0; ++nPhaseTry) {
                        if (g_world.pnAnimPhase != NULL) {
                            *g_world.pnAnimPhase = (i32)nPhaseTry;
                        }
                        pSite->bVisible = 0;
                        ClearSurface();
                        (void)DrawFrame();
                        for (i = 0; i < 16; ++i) {
                            memcpy(&aSnapA[i * 16],
                                   &((const u16 *)g_assets.pSurface->pPixels)
                                       [(size_t)(nY0 + i) * (size_t)g_assets.pSurface->nWidth + (size_t)nX0],
                                   16 * sizeof(u16));
                        }
                        pSite->bVisible = 1;
                        ClearSurface();
                        (void)DrawFrame();
                        for (i = 0; i < 16; ++i) {
                            memcpy(&aSnapB[i * 16],
                                   &((const u16 *)g_assets.pSurface->pPixels)
                                       [(size_t)(nY0 + i) * (size_t)g_assets.pSurface->nWidth + (size_t)nX0],
                                   16 * sizeof(u16));
                        }
                        for (i = 0; i < 16 * 16; ++i) {
                            if (aSnapA[i] != aSnapB[i]) { ++nDiff; }
                        }
                    }
                    printf("  the terror site's glyph window holds %d of 256 pixels (anim phase %d)\n",
                           nDiff, nPhaseTry - 1);
                    Check(nDiff > 0,
                          "the terror site paints its cell through the type-0 active+visible glyph row (+6, 0x822c4's loc_82650 arm)");
                }
                /* The landing left its flag-60 line on 0, and a standing line routes the press arm to
                 * the card controls instead of the world (the 0x87658 guard the Part 100.2 note names
                 * for flag 11). The fixture clears it with the same UiLineList_Remove the engine's
                 * own arms use (0x83628 below). */
                (void)UiLineList_Remove(&g_world.msgLines, 0);
                Check(TestHitLineCount(&g_world.strategList, pSite->nX, pSite->nY, false) == 1,
                      "the site stands alone in the 16 x 16 scan window of its own press (0x82C48), so 0x832B0 dispatches one item");
                World_GeoscapePointer(&g_world, nPx, nPy, true);
                (void)World_GeoscapePointerUp(&g_world);
                {
                    const char *const pszCap = (const char *)g_assets.pTextRecs
                                               + 1145 * PUFO_TEXT_REC_STRIDE; /* PUFO_TEXT_REC_SITE_LABEL: for a type-0 site the line is record 1145 plus the number (1146 is the type-3 overwrite at 0x82FE4) */
                    const char *const pszRow = (g_world.ui.pStatusPanel->apLines[0] != NULL)
                                                   ? (const char *)g_world.ui.pStatusPanel->apLines[0] : NULL;
                    char szWant[16];
                    snprintf(szWant, sizeof(szWant), "%d", pSite->nSiteId);
                    printf("  after the press: %d line(s), line 0 flag %d; controls B %p y %d, A %p y %d\n",
                           g_world.msgLines.nLineCount,
                           (g_world.msgLines.nLineCount > 0)
                               ? (i32)UiLineList_GetFlag(&g_world.msgLines, 0) : -1,
                           (const void *)g_world.ui.apCtl[GEOUI_CTL_186BB0],
                           (g_world.ui.apCtl[GEOUI_CTL_186BB0] != NULL) ? (i32)g_world.ui.apCtl[GEOUI_CTL_186BB0]->y : -1,
                           (const void *)g_world.ui.apCtl[GEOUI_CTL_186BB4],
                           (g_world.ui.apCtl[GEOUI_CTL_186BB4] != NULL) ? (i32)g_world.ui.apCtl[GEOUI_CTL_186BB4]->y : -1);
                    Check(pszRow != NULL && strncmp(pszRow, pszCap, strlen(pszCap)) == 0,
                          "the press builds the panel's row 0 from TEXTS[1145], the site prefix (0x82F8C/0x82E6C)");
                    Check(pszRow != NULL && strcmp(pszRow + strlen(pszCap), szWant) == 0,
                          "  and it ends with itoa(the site id) (0x8301C)");
                    CheckEqI32((i32)g_world.ui.pStatusPanel->anLineAux[0], nIdxSite,
                                "the row's aux holds the site item (0x8306C)");
                    Check(g_world.msgLines.nLineCount == 1
                              && (i32)UiLineList_GetFlag(&g_world.msgLines, 0) == 18,
                          "the type-0 active dispatch cleared the HitTest line and posted the flag-18 card line (0x83628/0x8362C's arm at 0x835F4)");
                    Check(g_world.bFlag187098 == 1 && g_world.geo.time.bPaused == 1,
                          "and the arm set byte 0x187098 and paused (0x8361C, 0x83610)");
                }
                ClearSurface();
                (void)DrawFrame();
                DumpFrame("geoscape_terror_site");
            }

            printf("  flight round: spawn sub %d, %d pulse(s) flew %d,%d -> %d,%d; site after %d dice try(ies) at %d,%d with id %d\n",
                   nSub, nMotionPulses, nOldX, nOldY,
                   (pUfo != NULL) ? pUfo->nX : -1, (pUfo != NULL) ? pUfo->nY : -1,
                   nTries, (pSite != NULL) ? pSite->nX : -1, (pSite != NULL) ? pSite->nY : -1,
                   (pSite != NULL) ? pSite->nSiteId : -1);
        }
    }

    /* Part 102: the options screen answers the player through the shell's own thunks. The menu opens as
     * in section E (root control [8]'s word at 0x186518); every press after that is a real pen press --
     * the tab-0 arm walks rows 0..3 (0x892D0), the tab-1/2 arm walks all seven slot rows (0x89210 /
     * 0x89270), and the tab-3 arm hit-tests ctlA and ctlB (0x891C8). A standing card line from the
     * flight round would re-route the press arm (the 0x87658 guard), so the lines go first. */
    {
        UiControl *pCtlA = g_world.ui.apCtl[GEOUI_CTL_186BB4];
        UiControl *pCtlB = g_world.ui.apCtl[GEOUI_CTL_186BB0];
        i32 nFunds0 = g_world.nFunds;
        i32 nBase0 = (i32)g_world.nBaseCount, nSold0 = g_world.nSoldierCount;
        i32 nCraft0 = (i32)g_world.bCraftCount;
        i32 nStartup0 = g_nStartupUiThunk;
        i32 nStrat0 = (i32)PtrArray_GetCount(&g_world.strategList);

        while (g_world.msgLines.nLineCount > 0) {
            (void)UiLineList_Remove(&g_world.msgLines, 0);
        }
        (void)remove("sav0.sav"); /* slot 0's file starts absent, so "exists" afterwards is the press's doing */

        g_world.ui.apRootCtl[8]->pfnClick(); /* 0x186518, installed at 0x86590 (section E's route) */
        CheckEqI32(g_world.screen.nGeoscapeScreen, 18, "the menu is up (0x84ECC)");

        /* 102.1 the save page (SaveGame_Slot0_CloseUI @ 0x6af80 over the shell driver). */
        PressCtl(g_world.options.ctl.apList[0]); /* row 0: OptionsScreen_BindSaveSlots @ 0x6b49c */
        CheckEqI32((i32)g_world.abOptionsScratch[0], 1, "the save page: BindSaveSlots writes tab 1 (0x6B4A8)");
        Check(g_world.options.ctl.apList[0]->pfnClick == g_world.options.tab.apfnSaveSlot[0]
                  && g_world.options.ctl.apList[6]->pfnClick == g_world.options.tab.apfnSaveSlot[6],
              "0x6B4B4..0x6B4FC: the seven rows now carry the seven shell slot thunks (World_OptionsSaveSlot0..6)");
        CheckEqI32(g_world.options.ctl.apList[0]->y, 40, "the rows re-centre at 40 + 22 * i (0x6B514)");
        CheckEqI32(g_world.options.ctl.apList[6]->y, 40 + 22 * 6, "row 6 at 172 (0x6B538)");
        CheckEqI32(pCtlA != NULL ? pCtlA->y : -1, 215, "the cancel hotspot moves to 215 (0x6B550)");
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("options_save_tab");

        PressCtl(g_world.options.ctl.apList[0]); /* the row itself: SaveGame over szAppDir + "sav0.sav" */
        Check(DatFile_Exists("sav0.sav"),
              "0x6AF98's SaveGame ran through the shell driver (World_SaveSlot @ 0x44988 over the live world)");
        g_world.nFunds += 1000; /* falsify the live funds so the load's answer cannot be the value it never changed */
        CheckEqI32((i32)g_world.abOptionsScratch[0], 0, "0x6AFA4 clears the tab byte");
        CheckEqI32(g_world.nListMode, 1, "0x6AFAC writes MainMenu_nListMode 1 (the thunk held it at 4, 0x6AF90, while writing)");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 0,
                   "the tail Geoscape_CloseUI (0x6AFB8): case 18 with the tab already 0 reaches RebuildWithSound's teardown, screen 0");
        CheckEqI32((i32)g_world.abOptionsScratch[4], 0, "0x6C0FC");
        printf("  the save row: file exists, list mode %d, tab %d, screen %d, controls %d\n",
               g_world.nListMode, (i32)g_world.abOptionsScratch[0],
               g_world.screen.nGeoscapeScreen, (i32)g_world.abOptionsScratch[4]);

        /* 102.2 the load page (LoadGame_Slot0_InitOptions @ 0x6b194): tab 2, the teardown Init, and the
         * slot file read back over the live world. */
        g_world.ui.apRootCtl[8]->pfnClick();
        PressCtl(g_world.options.ctl.apList[1]); /* row 1: OptionsScreen_BindLoadSlots @ 0x6b588 */
        CheckEqI32((i32)g_world.abOptionsScratch[0], 2, "the load page: BindLoadSlots writes tab 2 (0x6B594)");
        Check(g_world.options.ctl.apList[0]->pfnClick == g_world.options.tab.apfnLoadSlot[0]
                  && g_world.options.ctl.apList[6]->pfnClick == g_world.options.tab.apfnLoadSlot[6],
              "the same 0x6B4B4..0x6B4FC loop with the seven load thunks (World_OptionsLoadSlot0..6)");
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("options_load_tab");

        PressCtl(g_world.options.ctl.apList[0]); /* the row itself: LoadGame over the file just written */
        CheckEqI32(g_world.nFunds, nFunds0, "the loaded world carries the saved funds back");
        CheckEqI32((i32)g_world.nBaseCount, nBase0, "  ... the bases ...");
        CheckEqI32(g_world.nSoldierCount, nSold0, "  ... the soldiers ...");
        CheckEqI32((i32)g_world.bCraftCount, nCraft0, "  ... and the craft");
        Check((i32)PtrArray_GetCount(&g_world.strategList) == nStrat0 && nStrat0 >= 1,
              "the flight round's strateg entries round-trip through the chunk (no fewer than before)");
        CheckEqI32(g_world.nListMode, 1,
                   "list mode 1: 0x6B1DC's LOW-byte test with a no-save return, LoadGame's own 0x44DC8 store otherwise");
        CheckEqI32((i32)g_world.bGeoscapeUi, 1, "Save_ReadGameState writes the geoscape byte the file carried back");
        printf("  the load row: funds %d == %d, bases %d/%d, soldiers %d/%d, craft %d/%d, strateg %d/%d, list mode %d, geoscape byte %d\n",
               g_world.nFunds, nFunds0, (i32)g_world.nBaseCount, nBase0, g_world.nSoldierCount, nSold0,
               (i32)g_world.bCraftCount, nCraft0, (i32)PtrArray_GetCount(&g_world.strategList), nStrat0,
               g_world.nListMode, (i32)g_world.bGeoscapeUi);

        /* 102.3 the quit page's Да (OptionsScreen_ReturnToTitle @ 0x6b46c, seated on ctlB by Init's
         * store at 0x6BF48): tab 0 (0x6B478), close (0x6B480), free-all with a1 = 1 (0x6B488), and the
         * startup tail (0x6B490) through WorldHooks.pfnInitStartupUi. */
        g_world.ui.apRootCtl[8]->pfnClick();
        PressCtl(g_world.options.ctl.apList[2]); /* row 2: BaseUi_SetScratch3AndUiY @ 0x6b674 */
        CheckEqI32((i32)g_world.abOptionsScratch[0], 3, "0x6B680");
        CheckEqI32(pCtlB != NULL ? (i32)pCtlB->y : -1, 120, "Да seated at 120 (0x6B68C)");
        CheckEqI32(pCtlA != NULL ? (i32)pCtlA->y : -1, 142, "Нет at 142 (0x6B69C)");
        Check(pCtlB != NULL && pCtlB->pfnClick == g_world.options.init.pfnReturnToTitle,
              "ctlB carries the shell's return-to-title thunk (wired in World_WireOptions, Part 102)");
        ClearSurface();
        (void)DrawFrame();
        DumpFrame("options_quit_yes");

        PressCtl(pCtlB); /* the tab-3 arm hit-tests ctlA then ctlB (0x891C8); the press arms ctlB */
        CheckEqI32((i32)g_world.abOptionsScratch[0], 0, "0x6B478 clears the tab byte first");
        Check(g_nStartupUiThunk == nStartup0 + 1,
              "the startup tail (0x6B490) reached the menu owner's WorldHooks.pfnInitStartupUi seam exactly once");
        CheckEqI32(g_world.screen.nGeoscapeScreen, 0, "the 0x6B480 close (tab already 0) reaches the teardown and screen 0");
        CheckEqI32((i32)g_world.abOptionsScratch[4], 0, "0x6C0FC clears the controls byte");
        CheckEqI32((i32)g_world.bGeoscapeUi, 0, "the free-all clears Map_bGeoscapeUi at 0x86D34");
        CheckEqI32((i32)PtrArray_GetCount(&g_world.strategList), 0,
                   "a1 = 1 empties the strateg list (PtrArray_DeleteAll @ 0x87208)");
        Check(g_world.nSoldierCount > 0 && g_world.apSoldiers[0] == NULL,
              "the soldier records are freed AND nulled, the 0x87114..0x871C8 stores");
        printf("  the quit Да: startup tail %d time(s), screen %d, controls %d, geoscape byte %d, strateg items %d\n",
               g_nStartupUiThunk - nStartup0, g_world.screen.nGeoscapeScreen,
               (i32)g_world.abOptionsScratch[4], (i32)g_world.bGeoscapeUi,
               (i32)PtrArray_GetCount(&g_world.strategList));
        (void)remove("sav0.sav");
    }

    /* Part 103: the quit-Yes arm (App_QuitFromTact @ 0x18e18, the Tact_Shutdown(1) store at 0x18E20) now
     * empties the LIVE strateg list. App_ShutdownTactNoMission runs the shell's World_EndHookShutdown
     * mirror with bKeepSome 1 -- the scripted tools/e2e_flow_quit.txt runs the same seam through
     * App_HookShutdownQuit in the real exe -- and here the mirror runs over a live world directly. The
     * world the 102 block's quit Да left is torn down, so the post-title new-game rebuild the menu path
     * performs (App_HookNewGame -> World_NewGame, the same call) gives the arm a live list to empty. The
     * +0x38 count cell (0x186530, the shell's saveStrateg.nStrategCount) must come out STALE exactly as
     * the original leaves it: only StrategList_AddItem 0x82710 and StrategList_RemoveAt 0x82B34 restamp
     * it, and neither runs at 0x3E154. */
    {
        i32 nItems0;

        Check(World_NewGame(&g_world, &g_assets, &g_fontSys, &g_options, &hooks) != 0,
              "the post-title new-game rebuild (the App_HookNewGame call shape) brings the world back");
        nItems0 = (i32)PtrArray_GetCount(&g_world.strategList);
        Check(nItems0 > 0, "the new run starts with live strateg items");
        CheckEqI32(g_world.saveStrateg.nStrategCount, 0,
                   "InitUI zeroes the +0x38 cell (block+0x38 in Geoscape_InitUI) and the port's live list ops "
                   "keep the count in the PtrArray alone -- the recorded Part 62 deviation, so the cell is 0 here");
        (void)World_SaveSlot(&g_world, "", 0, &hooks); /* 0x2728's restamp: the save boundary stamps the mirror */
        CheckEqI32(g_world.saveStrateg.nStrategCount, nItems0,
                   "World_SaveSlot restamps 0x186530 from the list before Save_WriteStrateg (0x3FE28's bound)");

        World_EndHookShutdown(1, &g_world); /* 0x18E20's arm, tail 0x3E14C..0x3E154 */
        CheckEqI32((i32)PtrArray_GetCount(&g_world.strategList), 0,
                   "the arm empties the live list (PtrArray_DeleteAll @ 0x3E154)");
        CheckEqI32(g_world.saveStrateg.nStrategCount, nItems0,
                   "ORIGINAL DEFECT asserted, not hidden: the arm writes no +0x38 mirror, so 0x186530 is stale");
        CheckEqI32((i32)g_world.bTactFlag, 0, "the Map+0x00 tact-flag clear at 0x3D588 (the shell's tact-flag cell, named Map_bTactFlag in the IDB)");
        printf("  the quit-Yes arm: %d live items emptied, mirror cell stays %d, tact flag %d\n",
               nItems0, g_world.saveStrateg.nStrategCount, (i32)g_world.bTactFlag);
        (void)remove("sav0.sav");
    }

    /* ---- Part 103.3: the "map" chunk's dim bytes drive the shell grid, and the capacity guard stops
     * dims that overrun the table ---------------------------------------------------------------
     * The IDB has no test in either walk (0x434B4/0x434BC read, 0x4398C/0x43994 write dereference the
     * slot untested); the chunk is positional full coverage, and the dims it carries (0x43400/0x43418)
     * are what Init_LoadMission sizes the allocation from -- while Tact_InitMission itself stores 6/6
     * at 0x3C6C0/0x3C6D0, the value the save then writes back. So the proof is a save whose dim bytes
     * lie: run the load on the slot-63 snapshot with 5 stamped into the file and the grid must follow
     * the file, and with 7 stamped in it the guard must refuse the allocation instead of overrunning
     * the 14400-slot table (the original would overrun its own inline table at 0x23438). */
    {
        u8     bOldX, bOldY, bT1, bT2;
        i32    nRet, nCells, nLayer, nRow, nCol;
        GameMap *pMap63;

        Check(Test_PatchMapDims(L"sav63.sav", 6, 5, &bOldX, &bOldY),
              "sav63.sav holds a map chunk (dir entry \"MAP\")");
        Check(bOldX == 6 && bOldY == 6,
              "the live mission saved the dims InitMission wrote (0x3C6C0/0x3C6D0 store 6/6)");

        nRet = World_LoadSlot(&g_world, "", 63, &g_assets, &g_fontSys, &g_options, &hooks);
        Check(nRet != 0, "the 6x5 save loads -- Save_ReadMapDims sizes the shell grid from the file, not the 6/6");
        pMap63 = g_world.pTactMap;
        Check(pMap63 != NULL && pMap63->geom.nDimX == 6 && pMap63->geom.nDimY == 5,
              "the loaded map carries the FILE's dims 6x5 (0x43400/0x43418 beat the hard-coded 6/6)");

        nCells = 0;
        for (nLayer = 0; nLayer < 4; ++nLayer) {
            for (nRow = 0; nRow < 60; ++nRow) {
                for (nCol = 0; nCol < 60; ++nCol) {
                    if (g_world.apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer
                                            + PUFO_MAP_CELL_DIM * nRow + nCol] != NULL) {
                        ++nCells;
                    }
                }
            }
        }
        CheckEqI32(nCells, 12000,
                   "Init_LoadMission allocated 10*5 rows x 60 cols x 4 layers -- the loop bounds follow the file");

        {
            bool bRowsEmpty = true;

            for (nLayer = 0; nLayer < 4 && bRowsEmpty; ++nLayer) {
                for (nRow = 50; nRow < 60 && bRowsEmpty; ++nRow) {
                    for (nCol = 0; nCol < 60; ++nCol) {
                        if (g_world.apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer
                                                + PUFO_MAP_CELL_DIM * nRow + nCol] != NULL) {
                            bRowsEmpty = false;
                            break;
                        }
                    }
                }
            }
            Check(bRowsEmpty, "rows 50..59 are never reached by a 50-row walk, so they stay NULL");
        }

        /* Positional proof: the walk's first 50 rows keep their slots under a 50-row grid. Check the
         * slot the save wrote the selected soldier's occupant into -- the identity the record resolves
         * to depends on the roster's nSaveId assignment (Save_AssignRosterIndices), not on the dims,
         * so the gate is the slot and its walk-derived coordinates, and the occupant being there at all. */
        {
            MapCell *pSlot = g_world.apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * g_nSnap63Z
                                                 + PUFO_MAP_CELL_DIM * g_nSnap63Y + g_nSnap63X];

            if (g_nSnap63Y < 50) {
                Check(pSlot != NULL && pSlot->pUnit != NULL
                          && pSlot->nX == g_nSnap63X && pSlot->nY == g_nSnap63Y
                          && pSlot->nZ == g_nSnap63Z,
                      "the chunk's row 12 occupant still lands in its own slot (coordinates derived at 0x434DC..0x434E4)");
            } else {
                Check(pSlot == NULL,
                      "the soldier's chunk row is 50+ and a 50-row walk allocates and consumes no row past 49");
            }
            printf("  dims patch: 6x5 save, grid %d cells, occupant slot %d/%d/%d -> pUnit %s\n",
                   nCells, g_nSnap63X, g_nSnap63Y, g_nSnap63Z,
                   (pSlot != NULL && pSlot->pUnit != NULL) ? "set" : "none");
        }

        World_TactEscapeToGeoscape();
        Check(g_world.bTactFlag == 0 && g_world.pTactMap == NULL, "the 6x5 mission tears down (0x162BC/0x162C8)");

        Check(Test_PatchMapDims(L"sav63.sav", 7, 6, &bT1, &bT2) && bT1 == 6 && bT2 == 5,
              "the same chunk stamped 7 columns (and the 6x5 stamp reads back cleanly)");
        nRet = World_LoadSlot(&g_world, "", 63, &g_assets, &g_fontSys, &g_options, &hooks);
        Check(nRet != 0, "the 7-column save still runs the load chain -- only the grid allocation is refused");
        nCells = 0;
        for (nLayer = 0; nLayer < 4; ++nLayer) {
            for (nRow = 0; nRow < 60; ++nRow) {
                for (nCol = 0; nCol < 60; ++nCol) {
                    if (g_world.apTactCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer
                                            + PUFO_MAP_CELL_DIM * nRow + nCol] != NULL) {
                        ++nCells;
                    }
                }
            }
        }
        CheckEqI32(nCells, 0,
                   "the capacity guard refused 7*10 > 60 pitch: not one cell allocated, Save_ReadMap's seam guard refuses the chunk");
        Check(g_world.pTactMap != NULL && g_world.pTactMap->geom.nDimX == 7,
              "the dims byte is still the chunk's (7) -- the guard is at the allocation, not at the read");
        World_TactEscapeToGeoscape();
        Check(g_world.bTactFlag == 0 && g_world.pTactMap == NULL, "the refused mission tears down too");
        (void)remove("sav63.sav");
    }

    World_Free(&g_world);
    Boot_Shutdown(&g_assets);

    printf("%s\n", g_nFailures == 0 ? "all geoscapeframe checks passed" : "FAILURES");
    return g_nFailures == 0 ? 0 : 1;
}
