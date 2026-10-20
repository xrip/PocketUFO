/* test_smaphit.c - Smap_HitTestStrategSelect @ 0x82be8: what the pointer can select on the map.
 *
 * The function's answer is a byte, so every check here goes through what it leaves behind: the
 * status panel's lines (text and aux index), the message list, the clock, the four flag bytes, the
 * radar timer, the pending pick cell, and the sample the seam played. Two of its gates are pinned
 * by near misses rather than by round trips - the 16x16 window (eight cells in, nine out) and the
 * two zoom scalers (the stored coordinate that lands on the cell, and the next one that misses it)
 * - because those are the only assertions that actually identify the constant or the bound.
 *
 * The fixture's view is 160x160 at scroll (0,0), so a click at (120,160) is cell (80,80) and every
 * item coordinate below is written as a cell.
 */
#include "game/pufo_baseui.h"
#include "game/pufo_boot.h"      /* PUFO_TEXT_REC_STRIDE */
#include "game/pufo_clock.h"     /* GameTime */
#include "game/pufo_ship.h"      /* Ship, BaseRec */
#include "render/pufo_render.h"  /* PadImage */
#include "ui/pufo_linelist.h"    /* UiLineList */
#include "ui/pufo_panel.h"       /* UiPanel, PUFO_PANEL_LINE_BYTES */
#include "util/pufo_ptrarray.h"  /* PtrArray, the strateg list */
#include "util/pufo_str.h"      /* pufo_strcpy, the fixture's text records */

#include <stdio.h>
#include <string.h>

static int g_nChecks;
static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    ++g_nChecks;
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckI(i32 nGot, i32 nWant, const char *pszWhat)
{
    ++g_nChecks;
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static void CheckS(const char *pszGot, const char *pszWant, const char *pszWhat)
{
    ++g_nChecks;
    if (pszGot != NULL && strcmp(pszGot, pszWant) == 0) {
        printf("  ok   %s (%s)\n", pszWhat, pszGot);
    } else {
        printf("  FAIL %s: got %s, want %s\n", pszWhat,
               (pszGot != NULL) ? pszGot : "(null)", pszWant);
        g_nFailures += 1;
    }
}

/* ------------------------------------------------- the fixture */
#define HIT_BASES       3
#define HIT_CRAFTS      2
#define HIT_ITEMS       6
#define HIT_LINES      24
#define HIT_STRIP_H    20                    /* 320 - 20 = 300 is the strip gate */
#define HIT_TEXT_LAST 1148                   /* records 0..1147 */
#define HIT_MAP       160                    /* nMapW / nMapH */
#define HIT_CLICK_X   120                    /* cell 80 */
#define HIT_CLICK_Y   160                    /* cell 80 */
#define HIT_CELL       80

static u8 g_aTextRecs[HIT_TEXT_LAST][PUFO_TEXT_REC_STRIDE];
static BaseSiteRec g_aBaseInfo[4];           /* byte_186C24 == 0x186C40: 64-byte records, abName at +26 */
static u8 g_apLineBuf[HIT_LINES][PUFO_PANEL_LINE_BYTES];

static i32 g_nSample;                        /* the env's play-sample seam */
static i32 g_nOptSample;                     /* OptionsScreenHooks' own sample */
static i32 g_nOptMode = -1;                  /* -1 until the options hook runs */

static void HookSample(i32 nSample, void *pUser)
{
    (void)pUser;
    g_nSample = nSample;
}

static void HookOptSample(u32 nSample, u32 nArg, void *pUser)
{
    (void)nArg;
    (void)pUser;
    g_nOptSample = (i32)nSample;
}

static void HookOptOpen(i32 nMode, void *pUser)
{
    (void)pUser;
    g_nOptMode = nMode;
}

static const OptionsScreenHooks g_oOptHooks = {
    HookOptSample, NULL, HookOptOpen, NULL
};

typedef struct Fix {
    SmapView   oView;
    PadImage   oStrip;
    UiPanel    pStatus;
    UiPanel    pOptShips;
    UiLineList oLines;
    PtrArray   oList;
    StrategItem aItem[HIT_ITEMS];
    i32        nItemCount;

    BaseRec    aBase[HIT_BASES];
    BaseRec   *apBase[HIT_BASES];
    i8         nBaseCount;

    Ship       aCraft[HIT_CRAFTS];
    Ship      *apCraft[HIT_CRAFTS];
    u8         nCraftCount;

    GameTime   oTime;
    u8         bMapSelect;
    i32        nPickX, nPickY;
    i32        nScreenId, nSelStrateg, nSelShip, nStrategIdx;
    u8         bFlag8C, bFlag90, bFlag98, bFlagA0;
    i32        nRadarTimer;
} Fix;

static Fix g;
static SmapHitEnv gEnv;

static void Fix_Panel(UiPanel *p)
{
    memset(p, 0, sizeof(*p));
    p->nStyle = 13;                    /* Geoscape_InitUI @ 0x857f4 builds the status panel this way */
    p->nFont = 16;
    p->nViewW = 240;
    p->nViewH = 320;
    p->bFlag7073 = 1;                  /* the UiPanel_ctor default, which some branches clear */
    for (i32 i = 0; i < HIT_LINES; ++i) {
        p->apLines[i] = g_apLineBuf[i];
        g_apLineBuf[i][0] = 0;
    }
}

static void Fix_Reset(void)
{
    if (g.oList.apItems != NULL) {
        PtrArray_Clear(&g.oList);
    }
    memset(&g, 0, sizeof(g));
    g_nSample = -1;
    g_nOptSample = -1;
    g_nOptMode = -1;

    g.oStrip.uHeight = HIT_STRIP_H;

    g.oView.nMapW = HIT_MAP;
    g.oView.nMapH = HIT_MAP;

    Fix_Panel(&g.pStatus);
    Fix_Panel(&g.pOptShips);

    g.nBaseCount = HIT_BASES;
    for (i32 i = 0; i < HIT_BASES; ++i) {
        g.apBase[i] = &g.aBase[i];
    }
    g.aBase[0].bUnk4 = 5;
    g.aBase[1].bUnk4 = 6;
    g.aBase[2].bUnk4 = 200; /* 0xC8: the original loads this field with LDRSB, so it is -56 */

    g.nCraftCount = HIT_CRAFTS;
    for (i32 i = 0; i < HIT_CRAFTS; ++i) {
        g.apCraft[i] = &g.aCraft[i];
    }
    g.aCraft[0].bId = 11;
    g.aCraft[0].bCraftType = 0;
    g.aCraft[0].bType = 3;
    g.aCraft[1].bId = 12;
    g.aCraft[1].bCraftType = 3;
    g.aCraft[1].bType = 2;

    g.nSelShip = 7;                    /* the pick-cell tail puts this into colour slot 0 */
    PtrArray_ctor(&g.oList);

    memset(&gEnv, 0, sizeof(gEnv));
    gEnv.pStripPad = &g.oStrip;
    gEnv.pStatusPanel = &g.pStatus;
    gEnv.pLines = &g.oLines;
    gEnv.pStrategList = &g.oList;
    gEnv.pTextRecs = &g_aTextRecs[0][0];
    gEnv.apBases = g.apBase;
    gEnv.pnBaseCount = &g.nBaseCount;
    gEnv.apSiteRecs = g_aBaseInfo;
    gEnv.apCraft = g.apCraft;
    gEnv.pnCraftCount = &g.nCraftCount;
    gEnv.pbSelectionFlag = &g.bMapSelect;
    gEnv.pnPendingPickX = &g.nPickX;
    gEnv.pnPendingPickY = &g.nPickY;
    gEnv.pnScreenId = &g.nScreenId;
    gEnv.pnSelectedStrategIdx = &g.nSelStrateg;
    gEnv.pnSelectedShipIdx = &g.nSelShip;
    gEnv.pnMsgShipIdx = &g.nStrategIdx;
    gEnv.pOptPanelShips = &g.pOptShips;
    gEnv.pTime = &g.oTime;
    gEnv.pbFlag18708C = &g.bFlag8C;
    gEnv.pbFlag187090 = &g.bFlag90;
    gEnv.pbFlag187098 = &g.bFlag98;
    gEnv.pbFlag1870A0 = &g.bFlagA0;
    gEnv.pnRadarPulseTimer = &g.nRadarTimer;
    gEnv.pOptionsHooks = &g_oOptHooks;
    gEnv.pfnPlaySample = HookSample;
}

/* One strateg item appended to the list, in list order. */
static StrategItem *Fix_Item(i32 nType, i32 bActive, i32 bVisible, i32 nX, i32 nY,
                             i32 nSiteId, i32 nDisplayId)
{
    StrategItem *p = &g.aItem[g.nItemCount++];

    memset(p, 0, sizeof(*p));
    p->nType = (u8)nType;
    p->bActive = (u8)bActive;
    p->bVisible = (u8)bVisible;
    p->nX = nX;
    p->nY = nY;
    p->nSiteId = nSiteId;
    p->nDisplayId = nDisplayId;
    PtrArray_Add(&g.oList, p);
    return p;
}

static u8 Fix_Run(i32 nX, i32 nY)
{
    return Smap_HitTestStrategSelect(&g.oView, &gEnv, nX, nY);
}

static const char *LineText(i32 nLine) { return (const char *)g.pStatus.apLines[nLine]; }
static i32 LineAux(i32 nLine) { return g.pStatus.anLineAux[nLine]; }

static void Fix_Text(void)
{
    memset(g_aTextRecs, 0, sizeof(g_aTextRecs));
    pufo_strcpy((char *)g_aTextRecs[PUFO_TEXT_REC_SITE_LABEL], "SITE ");
    pufo_strcpy((char *)g_aTextRecs[PUFO_TEXT_REC_ALIEN_LABEL], "ALIEN ");
    pufo_strcpy((char *)g_aTextRecs[PUFO_TEXT_REC_CRAFT_LABEL], "CRAFT ");
    pufo_strcpy((char *)g_aTextRecs[137], "SKYRANGER");
    pufo_strcpy((char *)g_aTextRecs[138], "INTERCEPTOR");
    pufo_strcpy((char *)g_aTextRecs[139], "FIRE STORM");
    pufo_strcpy((char *)g_aTextRecs[418], "LIGTHNING");
    pufo_strcpy((char *)g_aTextRecs[419], "AVENGER");

    memset(g_aBaseInfo, 0, sizeof(g_aBaseInfo));
    pufo_strcpy(g_aBaseInfo[0].abName, "ALPHA");
    pufo_strcpy(g_aBaseInfo[1].abName, "BETA");
    pufo_strcpy(g_aBaseInfo[2].abName, "GAMMA");
    pufo_strcpy(g_aBaseInfo[3].abName, "SPARE");
}

/* ------------------------------------------------- the strip gate */
static void TestStripGate(void)
{
    printf("the zoom strip gate (0x82C18)\n");

    Fix_Reset();
    g.pStatus.nLineCount = 7;
    CheckI(Fix_Run(HIT_CLICK_X, 301), 0, "a click below the strip edge answers 0");
    CheckI(g.pStatus.nLineCount, 7, "the panel is untouched - the gate runs before the line clear");

    Fix_Reset();
    g.pStatus.nLineCount = 7;
    CheckI(Fix_Run(HIT_CLICK_X, 300), 0, "a click exactly on the strip edge is still a map click");
    CheckI(g.pStatus.nLineCount, 0, "so the line count is cleared");

    Fix_Reset();
    g.oStrip.uHeight = 0;
    g.pStatus.nLineCount = 7;
    CheckI(Fix_Run(0, 320), 0, "with a zero-height strip only a click at y 321 is out");
    CheckI(g.pStatus.nLineCount, 0, "and y 320 scans");
}

/* ------------------------------------------------- the cell formula and the two scalers */
static void TestCellAndScale(void)
{
    printf("screen-to-cell and the zoom scalers (0x82C84, 0x82D48, 0x82DD0)\n");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL, HIT_CELL, 42, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "level 0 compares the stored coordinate as it is");
    CheckI(g.pStatus.nLineCount, 1, "one line for the item on the clicked cell");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL + 9, HIT_CELL, 42, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 0, "nine cells away is not a match");
    CheckI(g.pStatus.nLineCount, 0, "and it adds no line");

    /* The window is eight cells wide, so the scalers are pinned at its far edge. 591 * 1/1.333 =
     * 443.36 -> 443, which is +7 around cell 436; 1/1.33 would give 444.36 -> 444 and miss. 592
     * scales to 444.11 -> 444, one cell past that edge. */
    Fix_Reset();
    g.oView.nMode = 1;
    g.oView.nScrollX = 356;
    g.oView.nScrollY = 356;
    Fix_Item(0, 1, 1, 591, 591, 42, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "level 1 scales the item by 1/1.333, not by 1/1.33");
    CheckI(g.pStatus.nLineCount, 1, "591 scales onto the far edge of the window");

    Fix_Reset();
    g.oView.nMode = 1;
    g.oView.nScrollX = 356;
    g.oView.nScrollY = 356;
    Fix_Item(0, 1, 1, 592, 592, 42, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 0, "592 scales one cell past that edge");

    /* 2631 * 1/1.694 = 1553.13 -> 1553, the far edge of the window around cell 1546. The product of
     * the two scroll factors, 1/(1.33 * 1.27) = 0.5906988558163163, gives 1554.13 -> 1554 and misses. */
    Fix_Reset();
    g.oView.nMode = 2;
    g.oView.nScrollX = 1466;
    g.oView.nScrollY = 1466;
    Fix_Item(0, 1, 1, 2631, 2631, 42, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1,
           "level 2 scales by 1/1.694, not by the product of the scroll factors");
    CheckI(g.pStatus.nLineCount, 1, "2631 scales onto the far edge of the window");

    Fix_Reset();
    g.oView.nMode = 2;
    g.oView.nScrollX = 1466;
    g.oView.nScrollY = 1466;
    Fix_Item(0, 1, 1, 2646, 2646, 42, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 0, "2646 scales fifteen cells past the clicked cell");

    Fix_Reset();
    g.oView.nMode = 7;
    Fix_Item(0, 1, 1, HIT_CELL, HIT_CELL, 42, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "a mode outside 0..2 leaves the coordinate unscaled");
}

/* ------------------------------------------------- the 16x16 window and the wrap */
static void TestScanWindow(void)
{
    printf("the 16x16 cell window (0x82C48..0x82C50) and the one wrap (0x82CB4)\n");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL - 8, HIT_CELL, 1, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "the window reaches eight cells left of the click");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL - 9, HIT_CELL, 1, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 0, "nine cells out is beyond the window");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL + 7, HIT_CELL + 7, 1, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "the far edge is +7 on both axes");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL + 8, HIT_CELL + 8, 1, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 0, "+8 is outside");

    /* scroll 0, map 160, click x 0: 0 + 80 + 0 - 120 = -40, wrapped once to 120. */
    Fix_Reset();
    Fix_Item(0, 1, 1, 120, HIT_CELL, 1, -1);
    CheckI(Fix_Run(0, HIT_CLICK_Y), 1, "a negative cell wraps by the map width once");

    Fix_Reset();
    Fix_Item(0, 1, 1, -40, HIT_CELL, 1, -1);
    CheckI(Fix_Run(0, HIT_CLICK_Y), 0, "the item coordinate is not wrapped, only the computed cell");
}

/* ------------------------------------------------- the type 0 / 3 lines */
static void TestSiteLines(void)
{
    printf("the nType 0 / 3 line builder (0x82E6C..0x83070)\n");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL, HIT_CELL, 42, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "SITE 42", "an active site is TEXTS 1145 plus itoa(nSiteId)");
    CheckI(LineAux(0), 0, "the aux column carries the item's list index");

    Fix_Reset();
    Fix_Item(3, 1, 1, HIT_CELL, HIT_CELL, 7, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "ALIEN 7", "type 3 replaces the prefix from index 0 instead of appending");

    Fix_Reset();
    Fix_Item(0, 1, 0, HIT_CELL, HIT_CELL, 42, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "an invisible site is still selectable");
    CheckI(g.pStatus.nLineCount, 0, "but it contributes no line");

    Fix_Reset();
    Fix_Item(0, 0, 1, HIT_CELL, HIT_CELL, 5, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "ALPHA", "an inactive site names the base whose site byte is 5");
    CheckI(LineAux(0), 0, "with the item index, not the base index");

    Fix_Reset();
    Fix_Item(0, 0, 1, HIT_CELL, HIT_CELL, 6, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "BETA", "the second base is found by its own byte");

    Fix_Reset();
    Fix_Item(0, 0, 1, HIT_CELL, HIT_CELL, 200, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.pStatus.nLineCount, 1, "a site with no base still adds a line");
    CheckS(LineText(0), "", "and 200 does not match a byte the original loads as -56 (LDRSB at 0x82ED4)");

    Fix_Reset();
    Fix_Item(0, 0, 1, HIT_CELL, HIT_CELL, 99, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.pStatus.nLineCount, 1, "a search that finds nothing still adds a line");
    CheckS(LineText(0), "", "the port writes an empty name where the original reads one record past the end");

    Fix_Reset();
    g.bMapSelect = 1;
    Fix_Item(0, 0, 1, HIT_CELL, HIT_CELL, 5, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "with a map selection in progress the item is selectable");
    CheckI(g.pStatus.nLineCount, 0, "and the base line is gated out");

    Fix_Reset();
    Fix_Item(0, 0, 0, HIT_CELL, HIT_CELL, 5, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "ALPHA", "the inactive branch does not test bVisible");
}

/* ------------------------------------------------- the type 1 / 4 lines */
static void TestCraftLines(void)
{
    printf("the nType 1 / 4 line builder (0x830B4..0x83234)\n");

    Fix_Reset();
    Fix_Item(1, 1, 1, HIT_CELL, HIT_CELL, -1, 11);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "CRAFT 11", "an active craft is TEXTS 1147 plus itoa(nDisplayId)");

    Fix_Reset();
    Fix_Item(4, 1, 1, HIT_CELL, HIT_CELL, -1, 12);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "CRAFT 12", "type 4 uses the same prefix and the same id field");

    Fix_Reset();
    Fix_Item(1, 1, 0, HIT_CELL, HIT_CELL, -1, 11);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "an invisible craft is selectable");
    CheckI(g.pStatus.nLineCount, 0, "with no line");

    Fix_Reset();
    Fix_Item(1, 0, 1, HIT_CELL, HIT_CELL, -1, 12);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "LIGTHNING-2", "an inactive craft is named by Ship_FormatDisplayName");

    Fix_Reset();
    Fix_Item(4, 0, 1, HIT_CELL, HIT_CELL, -1, 11);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "SKYRANGER3", "the craft search matches Ship.bId against nDisplayId");

    Fix_Reset();
    Fix_Item(1, 0, 0, HIT_CELL, HIT_CELL, -1, 12);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "an invisible inactive craft is selectable");
    CheckI(g.pStatus.nLineCount, 0, "and the inactive craft branch needs bVisible");

    Fix_Reset();
    Fix_Item(4, 0, 1, HIT_CELL, HIT_CELL, -1, 99);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.pStatus.nLineCount, 1, "a craft search that finds nothing still adds a line");
    CheckS(LineText(0), "", "the port leaves it empty where the original formats apCraft[nCraftCount]");

    Fix_Reset();
    g.bMapSelect = 1;
    Fix_Item(1, 0, 1, HIT_CELL, HIT_CELL, -1, 12);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "the selection flag does not stop the select answer");
    CheckI(g.pStatus.nLineCount, 0, "but it gates the craft line out");
}

/* ------------------------------------------------- more than one line */
static void TestMultiSelect(void)
{
    printf("the multi-select layout (0x832B0..0x83378)\n");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL, HIT_CELL, 1, -1);
    Fix_Item(0, 1, 1, HIT_CELL - 1, HIT_CELL, 2, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "two contacts under the pointer");
    CheckI(g.pStatus.nLineCount, 2, "both lines are kept");
    CheckI(LineAux(0), 1, "cells are scanned before items, so the line order follows the cell offset");
    CheckI(LineAux(1), 0, "and not the list order");
    CheckI(g.pStatus.nPosX, 120, "the panel is centred at x 120");
    CheckI(g.pStatus.nPosY, 104, "120 - (2 / 2) * nFont");
    CheckI(g.pStatus.nUnk7052, 20, "the view rect starts at x 20");
    CheckI(g.pStatus.nUnk7056, 104, "and its y is recomputed, not copied");
    CheckI(g.pStatus.nViewW, 200, "a 200-wide view");
    Check(g.pStatus.bFlag7073 == 0, "bFlag7073 is cleared");
    Check(g.oTime.bPaused == 1, "the clock pauses");
    CheckI(g.bFlagA0, 1, "byte_18709C[4] goes up");
    CheckI(g_nSample, 7, "Sound_Play(7) plays");
    CheckI(g.oLines.nLineCount, 0, "and no message line is touched");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL, HIT_CELL, 1, -1);
    Fix_Item(0, 1, 1, HIT_CELL - 1, HIT_CELL, 2, -1);
    Fix_Item(0, 1, 1, HIT_CELL - 2, HIT_CELL, 3, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.pStatus.nPosY, 104, "three lines still give (3 / 2) == 1");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL, HIT_CELL, 1, -1);
    Fix_Item(0, 1, 1, HIT_CELL - 1, HIT_CELL, 2, -1);
    Fix_Item(0, 1, 1, HIT_CELL - 2, HIT_CELL, 3, -1);
    Fix_Item(0, 1, 1, HIT_CELL - 3, HIT_CELL, 4, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.pStatus.nLineCount, 4, "four lines");
    CheckI(g.pStatus.nPosY, 88, "120 - (4 / 2) * 16");

    Fix_Reset();
    Fix_Item(0, 1, 0, HIT_CELL, HIT_CELL, 1, -1);
    Fix_Item(0, 1, 0, HIT_CELL - 1, HIT_CELL, 2, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "two invisible contacts are still a select");
    CheckI(g.pStatus.nLineCount, 0, "but the panel stays empty, so the single-line path runs");
    CheckI(g.bFlagA0, 0, "and the multi-select flag is not set");
}

/* ------------------------------------------------- the single-line dispatch */
static void TestSingleDispatch(void)
{
    printf("the six-branch dispatch (0x83394..0x836D8)\n");

    Fix_Reset();
    Fix_Item(1, 1, 1, HIT_CELL, HIT_CELL, -1, 11);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.pStatus.nUnk3032, 0, "nUnk3032 is cleared before the line edit");
    CheckI(g.oLines.nLineCount, 1, "line 0 dropped, flag 10 inserted at 0");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 10, "flag 10 for an active craft");
    CheckI((i32)UiLineList_GetColor(&g.oLines, 0, 0), 0, "colour slot 0 is the item index");
    Check(g.oTime.bPaused == 1, "the clock pauses");

    Fix_Reset();
    Fix_Item(0, 0, 1, HIT_CELL, HIT_CELL, 5, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.nScreenId, 18, "an inactive site opens the options screen id");
    CheckI(g_nOptMode, 5, "OptionsScreen_OpenWithSound gets nSiteId");
    CheckI(g_nOptSample, 7, "with its own sample 7");
    Check(g.pOptShips.bFlag7073 == 0, "the options ships panel is reset, which the status click does not do");
    CheckI(g.oLines.nLineCount, 0, "and no message line is edited");

    Fix_Reset();
    g.nRadarTimer = 99;
    Fix_Item(4, 1, 1, HIT_CELL, HIT_CELL, -1, 11);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.bFlag8C, 1, "0x18708C goes up for an active type 4");
    CheckI(g.nSelStrateg, 0, "the item index is stored at 0x186BE4");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 20, "flag 20 appended");
    CheckI((i32)UiLineList_GetColor(&g.oLines, 0, 0), 0, "slot 0 is the nUnk3032 the branch just cleared");
    CheckI((i32)UiLineList_GetColor(&g.oLines, 0, 1), 0, "slot 1 is read back from 0x186BE4");
    CheckI(g.nRadarTimer, 99, "this branch leaves the radar pulse timer alone");

    Fix_Reset();
    Fix_Item(3, 1, 1, HIT_CELL, HIT_CELL, 7, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.bFlag90, 1, "0x187090 goes up for an active alien site");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 19, "flag 19 appended");
    CheckI(g.nRadarTimer, 250, "and the radar pulse timer is set");

    Fix_Reset();
    Fix_Item(0, 1, 1, HIT_CELL, HIT_CELL, 42, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.bFlag98, 1, "0x187090 + 8 goes up for an active site");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 18, "flag 18 appended");
    CheckI(g.nRadarTimer, 250, "timer 250");
    CheckI(g.bFlag90, 0, "and 0x187090 itself stays down");

    Fix_Reset();
    g.oLines.abLineFlags[0] = 5;
    g.oLines.nLineCount = 1;
    g.nRadarTimer = 0;
    Fix_Item(1, 0, 1, HIT_CELL, HIT_CELL, -1, 12);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.nSelShip, 1, "the craft whose bId is the item's nDisplayId");
    CheckI(g.nStrategIdx, 1, "mirrored into 0x159E74");
    CheckI(g.oLines.nLineCount, 2, "line 0 is kept and the new line is appended");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 5, "the only branch that does not drop line 0");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 1), 11, "flag 11 appended at the end");
    CheckI((i32)UiLineList_GetColor(&g.oLines, 1, 0), 1, "with the ship index in colour slot 0");
    CheckI(g.nRadarTimer, 250, "timer 250");
    CheckI(g.bFlag8C, 0, "and no craft flag is set");

    Fix_Reset();
    g.oLines.abLineFlags[0] = 5;
    g.oLines.nLineCount = 1;
    Fix_Item(1, 0, 1, HIT_CELL, HIT_CELL, -1, 99);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.nSelShip, -1, "a craft search with no answer stores -1");
    CheckI((i32)UiLineList_GetColor(&g.oLines, 1, 0), 0xFFu,
           "and the colour slot keeps only its low byte, as UiPanel_InsertLine masks it");

    /* The dispatch reads the aux column the line builder wrote, not the head of the list: the only
     * item that matches is the second one, so the branch has to be the type-0 one. */
    Fix_Reset();
    Fix_Item(1, 1, 1, HIT_CELL + 9, HIT_CELL, -1, 11);
    Fix_Item(0, 1, 1, HIT_CELL, HIT_CELL, 42, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.pStatus.nLineCount, 1, "one line, from the second item in the list");
    CheckI(LineAux(0), 1, "the aux column records which item produced it");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 18, "and the dispatch runs that item's branch");
    CheckI(g.bFlag8C, 0, "not the branch of the item that never matched");
}

/* ------------------------------------------------- the pick-a-cell tail */
static void TestPickCellTail(void)
{
    printf("the flag-27 pick-a-cell tail (0x83708..0x837BC)\n");

    Fix_Reset();
    g.oLines.abLineFlags[0] = 27;
    g.oLines.nLineCount = 1;
    g.bMapSelect = 1;
    g.nPickX = -1;
    g.nPickY = -1;
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 0, "nothing is selectable under this click");
    CheckI(g.nPickX, HIT_CELL, "the clicked cell is published at 0x186414");
    CheckI(g.nPickY, HIT_CELL, "and at 0x186418");
    Check(g.bMapSelect == 0, "bMapSelectionFlag is cleared");
    Check(g.oTime.bPaused == 1, "the clock pauses");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 12, "line 0 becomes flag 12");
    CheckI((i32)UiLineList_GetColor(&g.oLines, 0, 0), 7, "with the selected ship index in colour slot 0");
    CheckI(g.pStatus.nUnk3032, 0, "nUnk3032 is cleared");

    Fix_Reset();
    g.oView.nScrollX = 0;
    g.oView.nScrollY = 0;
    g.oLines.abLineFlags[0] = 27;
    g.oLines.nLineCount = 1;
    g.nPickX = -1;
    CheckI(Fix_Run(0, HIT_CLICK_Y), 0, "a click at the left edge");
    CheckI(g.nPickX, 120, "publishes the wrapped cell, the same arithmetic the scan uses");

    Fix_Reset();
    g.oLines.abLineFlags[0] = 27;
    g.oLines.nLineCount = 1;
    Fix_Item(0, 1, 1, HIT_CELL, HIT_CELL, 42, -1);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 1, "a contact under the pointer is selectable");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 18, "so the single-line dispatch runs instead of the tail");
    CheckI(g.nPickX, 0, "and the tail publishes no cell");

    Fix_Reset();
    g.oLines.abLineFlags[0] = 12;
    g.oLines.nLineCount = 1;
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 12, "only flag 27 triggers the tail");
    CheckI(g.nPickX, 0, "no cell published");

    Fix_Reset();
    g.oLines.abLineFlags[0] = 27;
    g.oLines.nLineCount = 1;
    g.pStatus.nLineCount = 7;
    CheckI(Fix_Run(HIT_CLICK_X, 301), 0, "a click in the strip returns before any of this");
    CheckI((i32)UiLineList_GetFlag(&g.oLines, 0), 27, "line 0 is untouched");
    CheckI(g.nPickX, 0, "no cell published");
}

/* ------------------------------------------------- refusals */
static void TestRefusals(void)
{
    printf("the port's refusals\n");

    Fix_Reset();
    CheckI(Smap_HitTestStrategSelect(NULL, &gEnv, HIT_CLICK_X, HIT_CLICK_Y), 0, "no viewport");
    CheckI(Smap_HitTestStrategSelect(&g.oView, NULL, HIT_CLICK_X, HIT_CLICK_Y), 0, "no env");

    Fix_Reset();
    PtrArray_Add(&g.oList, NULL);
    CheckI(Fix_Run(HIT_CLICK_X, HIT_CLICK_Y), 0, "a null list entry is skipped");
    CheckI(g.pStatus.nLineCount, 0, "and it contributes nothing");

    Fix_Reset();
    gEnv.pnBaseCount = NULL;
    Fix_Item(0, 0, 1, HIT_CELL, HIT_CELL, 5, -1);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckI(g.pStatus.nLineCount, 1, "with no base count the line is still added");
    CheckS(LineText(0), "", "with an empty name, since no base is searched");

    Fix_Reset();
    g.nCraftCount = 0;
    Fix_Item(1, 0, 1, HIT_CELL, HIT_CELL, -1, 11);
    (void)Fix_Run(HIT_CLICK_X, HIT_CLICK_Y);
    CheckS(LineText(0), "", "an empty craft array finds no craft either");
}

int main(void)
{
    Fix_Text();

    TestStripGate();
    TestCellAndScale();
    TestScanWindow();
    TestSiteLines();
    TestCraftLines();
    TestMultiSelect();
    TestSingleDispatch();
    TestPickCellTail();
    TestRefusals();

    printf("\nsmaphit: %d checks, %d failures\n", g_nChecks, g_nFailures);
    return g_nFailures != 0 ? 1 : 0;
}
