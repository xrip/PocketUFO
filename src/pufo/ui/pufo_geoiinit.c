/* pufo_geoiinit.c - Geoscape_InitUI @ 0x857f4. See pufo_geoiinit.h for the map.
 *
 * The static tables below were read out of the executable with
 * tools/gen_geoiinit_tables.py (descriptors at 0xABED8 / 0xAC078, prices at 0xAA2C0);
 * the facility name records and costs are the text-record indices and literal-pool
 * values of InitUI's own copy loops (0x85B18..0x85F58).
 */
#include "ui/pufo_geoiinit.h"

#include "game/pufo_boot.h"  /* PUFO_TEXT_REC_STRIDE */
#include "game/pufo_save.h"  /* PUFO_RESEARCH_TOPIC_COUNT */
#include "game/pufo_timer.h" /* Timer_StartD @ 0x9e7f4 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* g_aGeodescRoot @ 0x000ABED8: the 13 root controls at 0x1864F8 + 4*i. */
static const UiControlDesc g_aGeodescRoot[13] = {
    {     0,   236, "", "cplus", "", 0 },
    {    40,   236, "", "cminus", "", 0 },
    {     0,   257, "", "c5sec", "c5sec", 1 },
    {    40,   257, "", "c1min", "c1min", 1 },
    {     0,   278, "", "c5min", "c5min", 1 },
    {    40,   278, "", "c30min", "c30min", 1 },
    {     0,   298, "", "c1hour", "c1hour", 1 },
    {    40,   298, "", "c1day", "c1day", 1 },
    {    82,   236, "", "coption", "", 0 },
    {   165,   236, "", "cinterc", "", 0 },
    {   165,   257, "", "cbases", "", 0 },
    {   165,   277, "", "cufop", "", 0 },
    {   165,   298, "", "cfund", "", 0 }
};

/* g_aGeodescSingle @ 0x000AC078: the 13 control singletons, in record order. */
static const UiControlDesc g_aGeodescSingle[13] = {
    {   224,     1, "bclose", "bcloseu", "", 0 },
    {     1,     1, "emptyb", "cemptyb", "", 0 },
    {     1,     1, "emptyb1", "cemptyb1", "", 0 },
    {     1,     1, "emptyb1", "cemptyb1", "", 0 },
    {     1,     1, "emptyb", "cemptyb", "", 0 },
    {     1,     1, "memptyb", "cmemptyb", "", 0 },
    {     1,     1, "memptyb", "cmemptyb", "", 0 },
    {     1,     1, "emptyb", "cemptyb", "", 0 },
    {     1,     1, "emptyb", "cemptyb", "", 0 },
    {     1,     1, "emptyb", "cemptyb", "", 0 },
    {     1,     1, "_empty", "_emptys", "", 0 },
    {     1,     1, "_empty", "_emptys", "", 0 },
    {     1,     1, "emptyb", "cemptyb", "", 0 }
};

/* Which descriptor each singleton slot gets, from the UiControl_Setup call order at
 * 0x863D4..0x864DC: slots 0..4 take descriptors 0..4, slots 5..10 take 7..12, and the
 * last two slots (18704C, 187024) take descriptors 5 and 6. */
static const u8 g_anGeodescSingleSlot[13] = { 0, 1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 5, 6 };

/* g_anObjectPrice @ 0xAA2C0: the initial ObjectRecFull.uPrice per object slot. */
static const u32 g_anObjectPrice[149] = {
             0,   /* 0 */       800,   /* 1 */     26000,   /* 2 */
         84000,   /* 3 */      3000,   /* 4 */     36000,   /* 5 */
        126000,   /* 6 */     13500,   /* 7 */     10400,   /* 8 */
         50000,   /* 9 */      4000,   /* 10 */   150000,   /* 11 */
        140000,   /* 12 */    210000,  /* 13 */       60,   /* 14 */
           150,   /* 15 */       500,  /* 16 */       300,  /* 17 */
         15000,   /* 18 */      1200,  /* 19 */    45000,   /* 20 */
         46000,   /* 21 */      1250,  /* 22 */   120000,   /* 23 */
        100000,   /* 24 */     30000,  /* 25 */    16000,   /* 26 */
         17000,   /* 27 */    211000,  /* 28 */   267000,   /* 29 */
        250000,   /* 30 */    420000,  /* 31 */   480000,   /* 32 */
        720000,   /* 33 */    980000,  /* 34 */  1200000,   /* 35 */
            70,   /* 36 */      4400,  /* 37 */       200,  /* 38 */
          6290,   /* 39 */       500,  /* 40 */       600,  /* 41 */
           550,   /* 42 */       300,  /* 43 */       500,  /* 44 */
           400,   /* 45 */       600,  /* 46 */       900,  /* 47 */
          1200,   /* 48 */     10150,  /* 49 */    15200,   /* 50 */
         30000,   /* 51 */      1240,  /* 52 */     3000,   /* 53 */
          6000,   /* 54 */     80000,  /* 55 */       200,  /* 56 */
          3000,   /* 57 */     50000,  /* 58 */    20000,   /* 59 */
         20000,   /* 60 */     20000,  /* 61 */    20000,   /* 62 */
         20000,   /* 63 */     20000,  /* 64 */    20000,   /* 65 */
         20000,   /* 66 */     20000,  /* 67 */    20000,   /* 68 */
         20000,   /* 69 */         0,  /* 70 */        0,   /* 71 */
             0,   /* 72 */         0,  /* 73 */        0,   /* 74 */
             0,   /* 75 */         0,  /* 76 */        0,   /* 77 */
             0,   /* 78 */         0,  /* 79 */        0,   /* 80 */
             0,   /* 81 */         0,  /* 82 */        0,   /* 83 */
             0,   /* 84 */         0,  /* 85 */        0,   /* 86 */
             0,   /* 87 */         0,  /* 88 */        0,   /* 89 */
             0,   /* 90 */         0,  /* 91 */        0,   /* 92 */
             0,   /* 93 */         0,  /* 94 */        0,   /* 95 */
             0,   /* 96 */         0,  /* 97 */        0,   /* 98 */
             0,   /* 99 */         0,  /* 100 */       0,   /* 101 */
             0,   /* 102 */        0,  /* 103 */       0,   /* 104 */
          2000,   /* 105 */     1000,  /* 106 */  280000,   /* 107 */
         38000,   /* 108 */   250000,  /* 109 */    5000,   /* 110 */
         20000,   /* 111 */    50000,  /* 112 */   60000,   /* 113 */
             0,   /* 114 */        0,  /* 115 */       0,   /* 116 */
        500000,   /* 117 */   600000,  /* 118 */ 5000000,   /* 119 */
       8000000,   /* 120 */ 20000000,  /* 121 */   90000,   /* 122 */
        310000,   /* 123 */   550000,  /* 124 */       0,   /* 125 */
             0,   /* 126 */        0,  /* 127 */       0,   /* 128 */
             0,   /* 129 */        0,  /* 130 */       0,   /* 131 */
             0,   /* 132 */        0,  /* 133 */       0,   /* 134 */
             0,   /* 135 */        0,  /* 136 */       0,   /* 137 */
             0,   /* 138 */        0,  /* 139 */       0,   /* 140 */
             0,   /* 141 */    10000,  /* 142 */   20000,   /* 143 */
             0,   /* 144 */        0,  /* 145 */       0,   /* 146 */
             0,   /* 147 */        0   /* 148 */
};

/* The facility name text records, in table order: 106..118, then 463 (the odd one out,
 * unk_CAB93), then 120..122. */
static const i32 g_anFacNameRec[PUFO_BASEFAC_COUNT] = {
    106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 463, 120, 121, 122
};

/* The initial costs, the literal-pool values of 0x85B58..0x85F58. Record 10's literal is
 * 0x13D620, which IDA renders as a pointer to unk_13D620; as a number it is 1300000, and
 * BaseFacScreen_TryBuySelected @ 0x58910 reads the field as a number. */
static const u32 g_aFacCostInit[PUFO_BASEFAC_COUNT] = {
    300000, 400000, 750000, 800000, 500000, 800000, 150000, 500000, 750000, 2000000,
    1300000, 200000, 2300000, 300000, 900000, 1200000, 1800000
};

/* The 23 palettes InitUI loads before the controls (0x85FBC..0x860C4), then earth100.pal
 * with the earth pads (0x866E8). */
static const struct { const char *pszName; u8 nSlot; } g_aGeoscPalettes[] = {
    { "class.pal", 18 },    { "menu.pal", 1 },      { "base.pal", 3 },
    { "bbuild.pal", 4 },    { "soldier.pal", 8 },   { "plane.pal", 0 },
    { "transfer.pal", 13 }, { "baseinfo.pal", 14 }, { "research.pal", 6 },
    { "manuf.pal", 7 },     { "tactical.pal", 34 }, { "psi.pal", 19 },
    { "mapbits.pal", 32 },  { "radar.pal", 22 },    { "ufos.pal", 23 },
    { "river.pal", 24 },    { "cic.pal", 25 },      { "all_btn.pal", 26 },
    { "ufoflash.pal", 27 }, { "funding.pal", 28 },  { "earth_n.pal", 29 },
    { "baseat.pal", 30 },   { "cydonia.pal", 33 }
};

/* The pad loads, in GeoscapeUiState.apPad slot order. The research pad loads at 0x860D4,
 * the rest after the clock pads (0x866FC..0x868C4). */
static const struct {
    u8         nSlot;
    const char *pszName;
    u8         nMode;
    i16        nOriginX;
    i16        nOriginY;
} g_aGeoscPadLoads[] = {
    { GEOUI_PAD_186BA0,  "research",  6, 0, 0   }, /* 0x860D4 */
    { GEOUI_PAD_186B78,  "earth100", 10, 0, 0   }, /* 0x866FC */
    { GEOUI_PAD_186B74,  "m100",     10, 0, 0   }, /* 0x86720 */
    { GEOUI_PAD_186C28,  "e100n",    29, 0, 0   }, /* 0x86744 */
    { GEOUI_PAD_186C1C,  "e75n",     29, 0, 0   }, /* 0x86768 */
    { GEOUI_PAD_186BB8,  "e50n",     29, 0, 0   }, /* 0x8678C */
    { GEOUI_PAD_186C18,  "te100",    10, 0, 0   }, /* 0x867BC */
    { GEOUI_PAD_1864F0,  "earth75",  10, 0, 0   }, /* 0x867E0 */
    { GEOUI_PAD_187064,  "m75",      10, 0, 0   }, /* 0x86804 */
    { GEOUI_PAD_1864EC,  "earth50",  10, 0, 0   }, /* 0x86828 */
    { GEOUI_PAD_18705C,  "m50",      10, 0, 0   }, /* 0x8684C */
    { GEOUI_PAD_186BBC,  "gamemenu",  1, 0, 236 }, /* 0x86888 */
    { GEOUI_PAD_187048,  "bpanel",    4, 0, 228 }  /* 0x868B0 */
};

/* operator new(0x14) + PadImage_ctor, storing null when the allocation fails (0x85868). */
static PadImage *InitUI_NewPad(void)
{
    PadImage *pPad = (PadImage *)malloc(sizeof(PadImage));
    if (pPad == NULL) {
        return NULL;
    }
    PadImage_ctor(pPad);
    return pPad;
}

/* operator new(0x20) + UiControl_ctor (0x86100). */
static UiControl *InitUI_NewCtl(void)
{
    UiControl *pCtl = (UiControl *)malloc(sizeof(UiControl));
    if (pCtl == NULL) {
        return NULL;
    }
    UiControl_ctor(pCtl);
    return pCtl;
}

/* PadImage_Load then the two origin stores the original does after every load. */
static void InitUI_LoadPad(PadImage *pPad, const char *pszName, u8 nMode, i16 nOriginX,
                           i16 nOriginY)
{
    if (pPad == NULL) {
        return;
    }
    (void)PadImage_Load(pPad, pszName, nMode);
    pPad->nOriginX = nOriginX;
    pPad->nOriginY = nOriginY;
}

/* The original's strcpy-until-NUL, bounded to the destination field (deviation: the
 * original has no bound; the shipped names fit). */
static void InitUI_CopyName(char *pszDst, size_t cbDst, const char *pszSrc)
{
    size_t i;
    for (i = 0; i + 1 < cbDst; ++i) {
        pszDst[i] = pszSrc[i];
        if (pszSrc[i] == '\0') {
            return;
        }
    }
    pszDst[cbDst - 1] = '\0';
}

static const char *InitUI_TextRec(const GeoscapeInitEnv *pEnv, i32 nRec)
{
    return (const char *)(pEnv->pTextRecs) + PUFO_TEXT_REC_STRIDE * (size_t)nRec;
}

void Geoscape_InitUI(const GeoscapeInitEnv *pEnv, i32 nMode)
{
    GeoscapeUiState *pUi    = pEnv->pUi;
    SmapView        *pView  = pEnv->pView;
    PufoFont        *pFont  = pEnv->pFont;
    AssetReader     *pRdr   = pEnv->pRdr;
    UiControl       *apSpeed[PUFO_GEOSCAPE_SPEED_COUNT];
    SmapWorldSize   *pWorld;
    UiPanel         *pPanel;
    i32              i;

    /* 0x85814: the nine save-state bytes, then Font_Init, the screen id, the geoscape
     * flag byte (STRB), and the two root flags. */
    for (i = 0; i < 9; ++i) {
        pEnv->abUnk18655D[i] = 1;
    }
    PufoFont_Init(pFont);
    *pEnv->pnScreenMode = 0;
    *pEnv->pbGeoscapeUi = 1;
    *pEnv->pbRoot34 = 0;
    *pEnv->pbRoot35 = 0;

    /* 0x85868..0x85B04: the thirteen pad singletons. */
    for (i = 0; i < PUFO_GEOUI_PAD_SLOTS; ++i) {
        pUi->apPad[i] = InitUI_NewPad();
    }

    /* 0x85A34: the world-size block. The original allocates 0x4E204 bytes (the 800x400
     * pair plus a 320000-byte tail nothing reads) and stores the pair with STRH; the port
     * allocates just the pair (deviation). 0x160C4C gets its 144 here (STRB R1, #0x90). */
    pWorld = (SmapWorldSize *)malloc(sizeof(SmapWorldSize));
    if (pWorld != NULL) {
        pWorld->uW = 800;
        pWorld->uH = 400;
    }
    pUi->pWorldSize = pWorld;
    *pEnv->puResearchEntries = PUFO_RESEARCH_TOPIC_COUNT;

    /* 0x85B18..0x85F58: the facility table - name, bFlag21 = index, cost. */
    for (i = 0; i < PUFO_BASEFAC_COUNT; ++i) {
        InitUI_CopyName(pEnv->paFacFlags[i].szName, sizeof(pEnv->paFacFlags[i].szName),
                        InitUI_TextRec(pEnv, g_anFacNameRec[i]));
        pEnv->paFacFlags[i].bFlag21 = (u8)i;
        pEnv->paFacFlags[i].uCost   = g_aFacCostInit[i];
    }

    /* 0x85F64..0x85FB0: the four sprite groups, each base captured before its load. */
    pFont->nGlyphBase     = pFont->nTotalSprFrames;
    (void)Font_LoadSpr(pFont, "font");
    pFont->nBaseBitsBase  = pFont->nTotalSprFrames;
    (void)Font_LoadSpr(pFont, "basebits");
    pFont->nMapBitsBase   = pFont->nTotalSprFrames;
    (void)Font_LoadSpr(pFont, "mapbits");
    pFont->nBigObsBase    = pFont->nTotalSprFrames;
    (void)Font_LoadSpr(pFont, "bigobs");
    Font_BuildIndex(pFont);

    /* 0x85FBC..0x860C4: the 23 palettes. */
    for (i = 0; i < (i32)(sizeof(g_aGeoscPalettes) / sizeof(g_aGeoscPalettes[0])); ++i) {
        Palette_Load(g_aGeoscPalettes[i].pszName, g_aGeoscPalettes[i].nSlot);
    }

    /* 0x860D4: the research pad, then Map_nGlyphPalBase = 3. */
    InitUI_LoadPad(pUi->apPad[GEOUI_PAD_186BA0], "research", 6, 0, 0);
    *pEnv->pnGlyphPalBase = 3;

    /* 0x86100..0x864DC: the thirteen control singletons, then their descriptors. */
    for (i = 0; i < PUFO_GEOUI_PAD_SLOTS; ++i) {
        pUi->apCtl[i] = InitUI_NewCtl();
    }
    for (i = 0; i < PUFO_GEOUI_PAD_SLOTS; ++i) {
        if (pUi->apCtl[i] != NULL) {
            UiControl_Setup(pUi->apCtl[i], 0, 0, &g_aGeodescSingle[g_anGeodescSingleSlot[i]],
                            pEnv->pfnLoadPads, pEnv->pLoadPadsUser);
        }
    }

    /* 0x864E8..0x86600: the thirteen root controls, their click handlers, then their
     * descriptors. The handler slots are the root block's own dwords (see the header). */
    for (i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        pUi->apRootCtl[i] = InitUI_NewCtl();
    }
    for (i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        if (pUi->apRootCtl[i] != NULL) {
            pUi->apRootCtl[i]->pfnClick = pEnv->apfnRootClick[i];
        }
    }
    if (pUi->apCtl[GEOUI_CTL_186B8C] != NULL) {
        pUi->apCtl[GEOUI_CTL_186B8C]->pfnClick = pEnv->pfnCraftSelectClick; /* 0x865D0 */
    }
    for (i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        if (pUi->apRootCtl[i] != NULL) {
            UiControl_Setup(pUi->apRootCtl[i], 0, 0, &g_aGeodescRoot[i],
                            pEnv->pfnLoadPads, pEnv->pLoadPadsUser);
        }
    }

    /* 0x8660C..0x866DC: the ten clock digit pads "b0".."b9" (apClockDigitPads @ 0x186BF0),
     * each created and loaded in the same loop iteration. */
    for (i = 0; i < PUFO_GEOUI_LAYER_PADS; ++i) {
        char szName[8];
        pUi->apLayerPad[i] = InitUI_NewPad();
        (void)snprintf(szName, sizeof(szName), "b%d", (int)i);
        InitUI_LoadPad(pUi->apLayerPad[i], szName, 1, 0, 0);
    }

    /* 0x866E8..0x868C4: earth100.pal and the earth / chrome pads. */
    Palette_Load("earth100.pal", 10);
    for (i = 1; i < (i32)(sizeof(g_aGeoscPadLoads) / sizeof(g_aGeoscPadLoads[0])); ++i) {
        InitUI_LoadPad(pUi->apPad[g_aGeoscPadLoads[i].nSlot], g_aGeoscPadLoads[i].pszName,
                       g_aGeoscPadLoads[i].nMode, g_aGeoscPadLoads[i].nOriginX,
                       g_aGeoscPadLoads[i].nOriginY);
    }

    /* 0x867B0..0x86884: publish the three zoom layer sets. The original keeps the
     * level-0 trio in the globals 0x186E40 / 0x186B04 / 0x186BE0; the port hands all
     * three levels to the caller as data (deviation, see pufo_strateg.h). */
    pEnv->pSetsOut[0].pTiles   = pUi->apPad[GEOUI_PAD_186B74];
    pEnv->pSetsOut[0].pGrid    = pUi->apPad[GEOUI_PAD_186B78];
    pEnv->pSetsOut[0].pOverlay = pUi->apPad[GEOUI_PAD_186C28];
    pEnv->pSetsOut[0].uWorldW  = 800;
    pEnv->pSetsOut[0].uWorldH  = 400;
    pEnv->pSetsOut[1].pTiles   = pUi->apPad[GEOUI_PAD_187064];
    pEnv->pSetsOut[1].pGrid    = pUi->apPad[GEOUI_PAD_1864F0];
    pEnv->pSetsOut[1].pOverlay = pUi->apPad[GEOUI_PAD_186C1C];
    pEnv->pSetsOut[1].uWorldW  = 600;
    pEnv->pSetsOut[1].uWorldH  = 300;
    pEnv->pSetsOut[2].pTiles   = pUi->apPad[GEOUI_PAD_18705C];
    pEnv->pSetsOut[2].pGrid    = pUi->apPad[GEOUI_PAD_1864EC];
    pEnv->pSetsOut[2].pOverlay = pUi->apPad[GEOUI_PAD_186BB8];
    pEnv->pSetsOut[2].uWorldW  = 472;
    pEnv->pSetsOut[2].uWorldH  = 236;

    /* 0x868D8..0x868E8: the viewport starts at level 0 with the level-0 grid's size. */
    if (pUi->apPad[GEOUI_PAD_186B78] != NULL) {
        pView->nMapW = (i32)pUi->apPad[GEOUI_PAD_186B78]->uWidth;  /* 0x1863F4 */
        pView->nMapH = (i32)pUi->apPad[GEOUI_PAD_186B78]->uHeight; /* 0x1863F8 */
    }
    pView->nMode = 0;

    /* 0x868EC: the new-game branch, gated on the low byte of nMode. */
    if ((u8)nMode == 0) {
        u32 cb;
        (void)AssetReader_Find(pRdr, "research.dat");
        cb = AssetReader_Size(pRdr);
        if (cb > (u32)(PUFO_RESEARCH_TOPIC_COUNT * (i32)sizeof(ResearchTopicLive))) {
            cb = (u32)(PUFO_RESEARCH_TOPIC_COUNT * (i32)sizeof(ResearchTopicLive));
        }
        (void)AssetReader_Read(pRdr, pEnv->apTopics, cb);
        for (i = 0; i < (i32)*pEnv->puResearchEntries; ++i) {
            pEnv->apTopics[i].wUnk6 = 0; /* 0x86938: STRH [R1,#6] */
        }
        pView->nScrollX = 0;
        pView->nScrollY = 0;
        pView->nLastDx  = 0;
        pView->nLastDy  = 0;
        if (pEnv->pfnStartNew != NULL) {
            pEnv->pfnStartNew(pEnv->pHookUser); /* Game_StartNew @ 0x94554, not ported */
        }
    }

    (void)GameTime_Decode(pEnv->pTime); /* 0x86980 */

    /* 0x869A0: per-base site state. The gate reads nBaseCount as a signed byte. */
    if (*pEnv->pbBaseCount > 0) {
        for (i = 0; i < (i32)*pEnv->pbBaseCount; ++i) {
            (void)Base_ClearFacilityGridRow(pEnv->abFacilityGrid, i,
                                            pEnv->pWorld->apSiteState);
        }
    }

    /* 0x86A00: the initial prices, slots 1..nObjectCount-1. The table has 149 entries and
     * the shipped object count is 149; the port also stops at the table end (deviation). */
    if ((u32)pEnv->nObjectCount > 1u) {
        i32 nLimit = pEnv->nObjectCount;
        if (nLimit > (i32)(sizeof(g_anObjectPrice) / sizeof(g_anObjectPrice[0]))) {
            nLimit = (i32)(sizeof(g_anObjectPrice) / sizeof(g_anObjectPrice[0]));
        }
        for (i = 1; i < nLimit; ++i) {
            pEnv->apObjects[i]->uPrice = g_anObjectPrice[i];
        }
    }

    /* 0x86A38: object names, text records 1447..1594 into slots 0..147 - a fixed 148
     * copies, not nObjectCount (the last slot keeps whatever objects.dat carried). The
     * port also stops at the object count (deviation; the shipped count is 149). */
    for (i = 0; i < 148 && i < pEnv->nObjectCount; ++i) {
        InitUI_CopyName(pEnv->apObjects[i]->szName, sizeof(pEnv->apObjects[i]->szName),
                        InitUI_TextRec(pEnv, 1447 + i));
    }

    /* 0x86AA4: apObjects[1].uFirePower = 110 (STRH [R0,#0xB6]); 0x159DC4 = 10. */
    if (pEnv->nObjectCount > 1) {
        pEnv->apObjects[1]->uFirePower = 110;
    }
    *pEnv->pnUnk159DC4 = 10;

    /* 0x86AB0: the speed default and the geoscape timers. */
    for (i = 0; i < PUFO_GEOSCAPE_SPEED_COUNT; ++i) {
        apSpeed[i] = pUi->apRootCtl[2 + i]; /* apGeoscapeSpeedCtl @ 0x186500 */
    }
    GameTime_Set5Sec(pEnv->pWorld, apSpeed);
    Timer_StartD();

    /* 0x86AC0..0x86C30: the status line panel (unk_18641C). */
    pPanel = (UiPanel *)malloc(sizeof(UiPanel));
    if (pPanel != NULL) {
        (void)UiPanel_ctor(pPanel, 20);
    }
    pUi->pStatusPanel = pPanel;
    if (pPanel != NULL) {
        pPanel->nStyle = 13;
        pPanel->nFont  = 16;
        UiPanel_SetBytes0to2(pPanel, 0xA, 0x50, 0x5A);
        UiPanel_SetBytes6to11(pPanel, 0x44, 0xF4, 0xFC, 4, 0x46, 0x4B);
        UiPanel_SetBytes3to5(pPanel, 0x44, 0xF4, 0xFC);
        pPanel->nUnk7076  = 1;
        pPanel->bFlag7073 = 1;
        pPanel->pfnClick  = pEnv->pfnRadarClick; /* Geoscape_OnRadarListClick, not ported */
        pPanel->bUnk7088  = 100;
    }

    /* 0x86C34..0x86CDC: the 56 mission sites. With lang.dat present only 1650 text records
     * are loaded, so records 1655..1710 are zero and every name comes out empty - the
     * original's own behaviour, reproduced. */
    for (i = 0; i < PUFO_MISSION_SITE_COUNT; ++i) {
        char szName[16];
        (void)snprintf(szName, sizeof(szName), "%d.cts", (int)i);
        (void)AssetReader_Find(pRdr, szName);
        (void)AssetReader_Read(pRdr, &pEnv->paMissionSites[i], sizeof(MissionSiteRec));
        InitUI_CopyName(pEnv->paMissionSites[i].szName,
                        sizeof(pEnv->paMissionSites[i].szName),
                        InitUI_TextRec(pEnv, 1655 + i));
    }

    /* 0x86CE8..0x86D04: unpause the clock, stamp the save format version, bring up sound. */
    pEnv->pTime->bPaused = 0;
    *pEnv->pbSaveVersion = 3;
    (void)Sound_InitAndLoadSamples("tact");
    Sound_LoadAndPlayMusic(2);
    Sound_ApplyMusicVolume(Options_MusicLevel(pEnv->pOptions), pEnv->pOptions->bMusicOff);
    Sound_ApplySfxVolume(Options_SfxLevel(pEnv->pOptions), pEnv->pOptions->bSoundOff);
}
