/* test_geouimanage.c - the geoscape UI teardown.
 * (GeoscapeUi_FreeAll @ 0x86d20.) */
#include "audio/pufo_audio.h"
#include "ui/pufo_geouimanage.h"

#include "game/pufo_timer.h"
#include "pufo_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_nChecks, g_nFails;

static void TestCheck(const char *pszMsg, bool bCond)
{
    ++g_nChecks;
    if (bCond) {
        printf("  ok   %s\n", pszMsg);
    } else {
        ++g_nFails;
        printf("  FAIL %s\n", pszMsg);
    }
}

static void TestEqI32(const char *pszMsg, i32 nGot, i32 nWant)
{
    ++g_nChecks;
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszMsg, nGot);
    } else {
        ++g_nFails;
        printf("  FAIL %s: got %d, want %d\n", pszMsg, nGot, nWant);
    }
}

/* ------------------------------------------------- the timer seam, to see KillTimers_D */

static u32 g_aKilled[16];
static i32 g_nKilled;

static void RecSet(HWND hWnd, u32 nId, u32 nMs, void *pUser)
{
    (void)hWnd; (void)nId; (void)nMs; (void)pUser;
}

static void RecKill(HWND hWnd, u32 nId, void *pUser)
{
    (void)hWnd; (void)pUser;
    if (g_nKilled < (i32)(sizeof(g_aKilled) / sizeof(g_aKilled[0]))) {
        g_aKilled[g_nKilled++] = nId;
    }
}

/* ------------------------------------------------- the teardown itself */

static void TestFreeUiOnly(GeoscapeUiState *pUi, GeoscapeRoster *pRoster)
{
    /* bFreeWorld == 0 releases the UI and leaves the world alone. */
    g_nKilled = 0;
    GeoscapeUi_FreeAll(pUi, pRoster, 0);

    TestEqI32("KillTimers_D kills its four ids", g_nKilled, 4);
    TestEqI32("the first is the render timer", (i32)g_aKilled[0], (i32)PUFO_TIMER_RENDER);
    TestEqI32("the second is the animation timer", (i32)g_aKilled[1], (i32)PUFO_TIMER_ANIM);
    TestEqI32("the third is the ui-pulse timer", (i32)g_aKilled[2], (i32)PUFO_TIMER_UIPULSE);
    TestEqI32("the fourth is the extra timer", (i32)g_aKilled[3], (i32)PUFO_TIMER_EXTRA);

    TestEqI32("Map.bGeoscapeUi is cleared", (i32)*pUi->pbGeoscapeUi, 0);
    TestEqI32("and the font is freed", (i32)pUi->pFont->bInited, 0);

    /* Every slot must come back NULL. A slot the loops never reached would stay set, so this
     * is what proves the 13 / 13 / 10 / 13 bounds. */
    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        TestCheck("the UI root's widget slot is emptied", pUi->apRootCtl[i] == NULL);
    }
    for (i32 i = 0; i < PUFO_GEOUI_PAD_SLOTS; ++i) {
        TestCheck("a pad singleton is emptied", pUi->apPad[i] == NULL);
        TestCheck("a widget singleton is emptied", pUi->apCtl[i] == NULL);
    }
    for (i32 i = 0; i < PUFO_GEOUI_LAYER_PADS; ++i) {
        TestCheck("a layer pad is emptied", pUi->apLayerPad[i] == NULL);
    }
    TestCheck("the world-size block is emptied", pUi->pWorldSize == NULL);
    TestCheck("the status panel is emptied", pUi->pStatusPanel == NULL);

    /* The world is untouched. */
    TestCheck("no soldier is released", pRoster->apSoldiers[0] != NULL);
    TestCheck("no base is released", pRoster->apBases[0] != NULL);
    TestCheck("no craft is released", pRoster->apCraft[0] != NULL);
    TestEqI32("and the strateg list keeps its items", (i32)pRoster->pStrategList->nCount, 2);
}

static void Run(void)
{
    static GeoscapeUiState ui;
    static GeoscapeRoster roster;
    static PufoFont font;
    static u8 bGeoscapeUi;
    static Soldier *apSoldiers[2];
    static BaseRec *apBases[2];
    static Ship *apCraft[2];
    static PtrArray strateg;
    UiPanel *pPanel;

    printf("GeoscapeUi_FreeAll\n");

    TimerBackend backend;
    backend.pfnSet = RecSet;
    backend.pfnKill = RecKill;
    backend.pUser = NULL;
    Timer_SetBackend(&backend);

    memset(&ui, 0, sizeof(ui));
    memset(&roster, 0, sizeof(roster));
    memset(&font, 0, sizeof(font));

    font.bInited = 1;
    bGeoscapeUi = 1;
    ui.pFont = &font;
    ui.pbGeoscapeUi = &bGeoscapeUi;

    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        ui.apRootCtl[i] = (UiControl *)calloc(1, sizeof(UiControl));
    }
    for (i32 i = 0; i < PUFO_GEOUI_PAD_SLOTS; ++i) {
        ui.apPad[i] = (PadImage *)calloc(1, sizeof(PadImage));
        ui.apCtl[i] = (UiControl *)calloc(1, sizeof(UiControl));
    }
    for (i32 i = 0; i < PUFO_GEOUI_LAYER_PADS; ++i) {
        ui.apLayerPad[i] = (PadImage *)calloc(1, sizeof(PadImage));
    }
    ui.pWorldSize = (SmapWorldSize *)calloc(1, sizeof(SmapWorldSize));

    /* A panel with real line buffers, so UiPanel_FreeLines has something to walk. */
    pPanel = (UiPanel *)calloc(1, sizeof(UiPanel));
    if (!UiPanel_ctor(pPanel, 30)) {
        TestCheck("the status panel was built", false);
    } else {
        TestCheck("the status panel was built", true);
    }
    ui.pStatusPanel = pPanel;

    for (i32 i = 0; i < 2; ++i) {
        apSoldiers[i] = (Soldier *)calloc(1, sizeof(Soldier));
        apBases[i] = (BaseRec *)calloc(1, sizeof(BaseRec));
        apCraft[i] = (Ship *)calloc(1, sizeof(Ship));
    }
    strateg.nCount = 2;
    /* PtrArray_Clear frees apItems, so it has to be a heap array. A static one here
     * corrupts the heap. */
    strateg.apItems = calloc(2, sizeof(void *));
    strateg.apItems[0] = calloc(1, 16);
    strateg.apItems[1] = calloc(1, 16);

    roster.apSoldiers = apSoldiers;
    roster.nSoldierCount = 2;
    roster.apBases = apBases;
    roster.nBaseCount = 2;
    roster.apCraft = apCraft;
    roster.nCraftCount = 2;
    roster.pStrategList = &strateg;

    TestFreeUiOnly(&ui, &roster);

    /* Now release the world too. */
    GeoscapeUi_FreeAll(&ui, &roster, 1);
    TestCheck("the soldiers are released", apSoldiers[0] == NULL && apSoldiers[1] == NULL);
    TestCheck("the bases are released", apBases[0] == NULL && apBases[1] == NULL);
    TestCheck("the craft are released", apCraft[0] == NULL && apCraft[1] == NULL);
    TestEqI32("and the strateg list is emptied", (i32)strateg.nCount, 0);

    /* A second call is a no-op: everything is already NULL. */
    GeoscapeUi_FreeAll(&ui, &roster, 1);
    TestCheck("a second teardown finds nothing to do", ui.pStatusPanel == NULL);
}

/* ------------------------------------------------- the three count widths */

static void TestCountWidths(void)
{
    static GeoscapeUiState emptyUi;
    static GeoscapeRoster roster;
    static Soldier *apSoldiers[2];
    static BaseRec *apBases[2];
    static Ship *apCraft[2];

    printf("GeoscapeUi_FreeAll count gates\n");

    memset(&emptyUi, 0, sizeof(emptyUi));

    /* The soldier gate is a 16-bit compare, so a count of one releases one entry and leaves
     * the second alone. */
    memset(&roster, 0, sizeof(roster));
    apSoldiers[0] = calloc(1, sizeof(Soldier));
    apSoldiers[1] = calloc(1, sizeof(Soldier));
    roster.apSoldiers = apSoldiers;
    roster.nSoldierCount = 1;
    GeoscapeUi_FreeAll(&emptyUi, &roster, 1);
    TestCheck("one soldier count releases one soldier", apSoldiers[0] == NULL);
    TestCheck("and leaves the second in place", apSoldiers[1] != NULL);
    free(apSoldiers[1]);

    /* The base count is a SIGNED byte and the gate is `<= 0`, so a negative count releases
     * nothing rather than wrapping to a huge loop. */
    apBases[0] = calloc(1, sizeof(BaseRec));
    roster.apSoldiers = NULL;
    roster.apBases = apBases;
    roster.nBaseCount = -1;
    GeoscapeUi_FreeAll(&emptyUi, &roster, 1);
    TestCheck("a negative base count releases nothing", apBases[0] != NULL);
    free(apBases[0]);

    /* The craft count is an unsigned byte. */
    apCraft[0] = calloc(1, sizeof(Ship));
    roster.apBases = NULL;
    roster.apCraft = apCraft;
    roster.nCraftCount = 0;
    GeoscapeUi_FreeAll(&emptyUi, &roster, 1);
    TestCheck("a zero craft count releases nothing", apCraft[0] != NULL);
    free(apCraft[0]);

    /* A NULL roster is refused rather than walked. */
    GeoscapeUi_FreeAll(&emptyUi, NULL, 1);
    TestCheck("a NULL roster is refused", true);
}

int main(void)
{
    /* Sound_Shutdown reaches the audio backend, so give it the no-op stub. */
    Sound_SetBackend(NULL);

    Run();
    TestCountWidths();
    printf("test_geouimanage: %d checks, %d failures\n", g_nChecks, g_nFails);
    return g_nFails != 0;
}
