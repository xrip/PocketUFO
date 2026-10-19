/* pufo_tactinit.c - Tact_InitMission @ 0x3c550.
 *
 * The ARM is 947 instructions over 117 blocks. The order below is the ARM's order,
 * section by section, with the address of each section's first instruction in the
 * comment above it.
 */
#include "game/pufo_tactinit.h"

#include "asset/pufo_asset.h"
#include "game/pufo_timer.h"
#include "pufo_common.h"
#include "render/pufo_render.h"
#include "util/pufo_rand.h"
#include "util/pufo_str.h"

#include <stdlib.h>

/* --- the five panels, 0x3C738..0x3CB00 -------------------------------------------------
 *
 * All five get nStyle 14 (panel+0xBE0), nFont 16 (panel+0x1B9C) and the same colour pair
 * F0,F0,F0 / 14,14,14 through UiPanel_SetBytes6to11. A -1 below means the original does
 * not write that field at all, so UiPanel_ctor's default stays in place. */
typedef struct PanelLayout {
    i32 nLineCount; /* UiPanel_ctor's argument: 14, or 28 for the briefing panel */
    i32 nPosX;      /* panel+0x1B84 */
    i32 nPosY;      /* panel+0x1B88 */
    i8  nAlign;     /* nUnk7076 @ panel+0x1BA4: 0 left, 1 centre, 2 right */
    i8  bFlag7072;  /* panel+0x1BA0 */
    i8  bFlag7073;  /* panel+0x1BA1 */
    i8  bFlag7074;  /* panel+0x1BA2 */
} PanelLayout;

static const PanelLayout g_aPanelLayout[PUFO_TACTINIT_PANELS] = {
    { 14,  10, 30, -1, 0, 0, -1 }, /* 0x1568E8 */
    { 14, 200, 30, -1, 0, 0,  1 }, /* 0x1568E4 */
    { 14,  10, 55,  0, 1, 0, -1 }, /* 0x1568E0 */
    { 14, 230, 55,  2, 1, 0, -1 }, /* 0x1568DC */
    { 28, 120, 30,  1, 0, 0, -1 }, /* unk_1571EC, the briefing panel */
};

/* gap_0[6..8] and gap_0[9..11], written through UiPanel_SetBytes6to11. */
#define PUFO_PANEL_STYLE_RGB 0xF0u
#define PUFO_PANEL_STYLE_DIM 0x14u
/* nStyle @ panel+0xBE0, the visible row count: 0x0E. Distinct from the dim bytes above. */
#define PUFO_PANEL_STYLE_ROWS 14
#define PUFO_PANEL_FONT_ID    16

/* The constant the prologue stores into the selected-weapon record's third field. */
#define PUFO_WEAPON_SEL_CONST 10

/* Cell lookup over the mission's heap grid, which is what Map_BuildMissionTerrain
 * allocates and Tact_Shutdown frees. Out-of-range gives NULL; the original indexes its
 * pointer table with no bounds test at all. */
static MapCell *Init_LookupCell(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    const TactInitEnv *pEnv = (const TactInitEnv *)pUser;

    if (pEnv == NULL || pEnv->apCells == NULL) {
        return NULL;
    }
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    if (nY < 0 || nY >= PUFO_MAP_CELL_DIM || nX < 0 || nX >= PUFO_MAP_CELL_DIM) {
        return NULL;
    }
    return pEnv->apCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer
                         + PUFO_MAP_CELL_DIM * nY + nX];
}

/* 0x3C738..0x3C854: five UiPanel allocations, all before any of them is laid out.
 * The original stores the new pointer, or NULL when the allocation fails. */
static void Init_AllocPanels(TactInitEnv *pEnv)
{
    for (i32 i = 0; i < PUFO_TACTINIT_PANELS; ++i) {
        pEnv->apPanel[i] = (UiPanel *)malloc(sizeof(UiPanel));
        if (pEnv->apPanel[i] == NULL) {
            continue;
        }
        if (pEnv->hooks.pfnPanelCtor != NULL) {
            (void)pEnv->hooks.pfnPanelCtor(pEnv->apPanel[i],
                                           g_aPanelLayout[i].nLineCount,
                                           pEnv->hooks.pUser);
        }
    }
}

/* 0x3C854..0x3CB00: the layout writes. Deviation: the original writes through the panel
 * pointer with no null test, so a failed allocation is a crash. */
static void Init_SetupPanels(TactInitEnv *pEnv)
{
    for (i32 i = 0; i < PUFO_TACTINIT_PANELS; ++i) {
        UiPanel *pPanel = pEnv->apPanel[i];
        const PanelLayout *p = &g_aPanelLayout[i];

        if (pPanel == NULL) {
            continue;
        }
        pPanel->nStyle = PUFO_PANEL_STYLE_ROWS;
        pPanel->nFont  = PUFO_PANEL_FONT_ID;
        pPanel->nPosX  = p->nPosX;
        pPanel->nPosY  = p->nPosY;
        if (pEnv->hooks.pfnPanelStyle6to11 != NULL) {
            pEnv->hooks.pfnPanelStyle6to11(pPanel,
                                           PUFO_PANEL_STYLE_RGB, PUFO_PANEL_STYLE_RGB,
                                           PUFO_PANEL_STYLE_RGB, PUFO_PANEL_STYLE_DIM,
                                           PUFO_PANEL_STYLE_DIM, PUFO_PANEL_STYLE_DIM,
                                           pEnv->hooks.pUser);
        }
        if (p->bFlag7072 >= 0) {
            pPanel->bFlag7072 = (u8)p->bFlag7072;
        }
        if (p->bFlag7073 >= 0) {
            pPanel->bFlag7073 = (u8)p->bFlag7073;
        }
        if (p->bFlag7074 >= 0) {
            pPanel->bFlag7074 = (u8)p->bFlag7074;
        }
        if (p->nAlign >= 0) {
            pPanel->nUnk7076 = (i32)p->nAlign;
        }
    }
}

/* 0x3CA90..0x3CB30: showship.dat is allocated to its own size and read whole. The
 * original never tests AssetReader_Find, so a missing member gives a zero-size
 * allocation that the reveal block then reads through. Deviation: a zero-size member
 * stores NULL instead, which the reveal block already treats as "nothing to reveal". */
static void Init_LoadShowShip(TactInitEnv *pEnv, AssetReader *pRdr)
{
    (void)AssetReader_Find(pRdr, "showship.dat");
    const u32 cb = AssetReader_Size(pRdr);

    *pEnv->ppShowShip = NULL;
    if (cb == 0) {
        return;
    }

    void *pBuf = malloc(cb);
    if (pBuf == NULL) {
        return;
    }
    *pEnv->ppShowShip = pBuf;
    (void)AssetReader_Read(pRdr, pBuf, AssetReader_Size(pRdr));
}

/* 0x3CB60..0x3CB7C: lightmap.dat, a fixed 0x211 bytes into a static buffer. */
static void Init_LoadLightmap(TactInitEnv *pEnv, AssetReader *pRdr)
{
    (void)AssetReader_Find(pRdr, "lightmap.dat");
    (void)AssetReader_Read(pRdr, pEnv->pLightmap, PUFO_TACTINIT_LIGHTMAP_LEN);
}

/* 0x3CC20: the deploy-point sweep. The record index is 26*(nMapLayers-1)+n dwords into
 * aDeployCellOffsets, i.e. 4*n + 104*(nMapLayers-1) bytes, and the cell it names is
 * 3600*entry[2] + 60*(10*craftY + entry[1]) + (10*craftX + entry[0]). */
static void Init_DeployCellFlags(TactInitEnv *pEnv)
{
    const i32 nLayers = (i32)pEnv->pMap->nMapLayers;

    if (nLayers <= 0 || pEnv->pDeployPointCounts == NULL || pEnv->pDeployOffsets == NULL) {
        return;
    }
    const i32 nPoints = (i32)pEnv->pDeployPointCounts[nLayers - 1];
    if (nPoints <= 0) {
        return;
    }

    for (i32 n = 0; n < nPoints; ++n) {
        const u8 *pRec = pEnv->pDeployOffsets
                       + PUFO_DEPLOY_RECORD_BYTES * n
                       + PUFO_DEPLOY_GROUP_BYTES * (nLayers - 1);
        const i32 nCellX = PUFO_CELLS_PER_TILE * (i32)*pEnv->pnCraftMapX + (i32)pRec[0];
        const i32 nCellY = PUFO_CELLS_PER_TILE * (i32)*pEnv->pnCraftMapY + (i32)pRec[1];
        const i32 nLayer = (i32)pRec[2];
        MapCell *pCell = Init_LookupCell(nCellX, nCellY, nLayer, pEnv);

        if (pCell != NULL) {
            pCell->nFlags40 |= PUFO_DEPLOY_CELL_FLAG;
        }
    }
}

/* 0x3CCC4..0x3CE2C: the bFlags29 bit-4 sweep over the whole grid.
 *
 * The rule, from the ARM at 0x3CD24..0x3CDE4:
 *
 *   mission type 4 (alien base) -- set only on layers 0 and 1, on the last cell row, and
 *     from cell 10*(dimY-3) rightward; everywhere else the bit is cleared
 *   any other mission type -- set when the map has no layers at all, cleared otherwise
 *
 * Both bounds use nMapDimY, not nMapDimX, which is what the ARM does. */
static void Init_MissionCellFlags(TactInitEnv *pEnv)
{
    const i32 nDimX = (i32)pEnv->pMap->geom.nDimX;
    const i32 nDimY = (i32)pEnv->pMap->geom.nDimY;
    const i32 nLayers = (i32)pEnv->pMap->nMapLayers;
    const i32 nMissionType = (i32)pEnv->pMap->nMissionType;
    const i32 nLastRow = PUFO_CELLS_PER_TILE * nDimY - PUFO_CELLS_PER_TILE;
    const i32 nFirstCol = PUFO_CELLS_PER_TILE * (nDimY - 3);

    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 y = 0; y < PUFO_CELLS_PER_TILE * nDimY; ++y) {
            for (i32 x = 0; x < PUFO_CELLS_PER_TILE * nDimX; ++x) {
                bool bSet;

                if (nMissionType == PUFO_MISSION_TYPE_ALIEN_BASE) {
                    bSet = (nLayer <= 1 && y >= nLastRow && x >= nFirstCol);
                } else {
                    bSet = (nLayers == 0);
                }

                MapCell *pCell = Init_LookupCell(x, y, nLayer, pEnv);
                if (pCell == NULL) {
                    continue;
                }
                if (bSet) {
                    pCell->bFlags29 = (u8)(pCell->bFlags29 | 0x10u);
                } else {
                    pCell->bFlags29 = (u8)(pCell->bFlags29 & 0xEFu);
                }
            }
        }
    }
}

/* 0x3CE30 and 0x3D0D4: the visibility walk over the deployed XCom roster. Each call site
 * re-reads the count, which is what lets the hardware-soldier deploy path feed its own
 * additions into the walk. */
static void Init_RefreshVisibility(TactInitEnv *pEnv)
{
    const i32 nCount = (i32)pEnv->pMap->nXcomCount;

    for (i32 i = 0; i < nCount; ++i) {
        Soldier *pUnit = pEnv->pMap->apXcomUnits[i];
        if (pUnit != NULL) {
            (void)Unit_UpdateVisibility(pUnit, &pEnv->vis);
        }
    }
}

/* 0x3CEA4..0x3CFE4: the showship reveal.
 *
 * The blob is two u16 header words then one byte per entry: word 0 is the entry count
 * (the outer loop), word 1 is the entry length (the inner loop), and 0xFF means "this
 * entry reveals nothing". The centre is the craft tile minus half the extent, offset by
 * the two values the nMapLayers switch chose. Every revealed column is then run through
 * all four layers -- the +0x3840 step is 3600 dwords, one layer. */
static void Init_ShowShipReveal(TactInitEnv *pEnv, i32 nExtX, i32 nExtY)
{
    if (pEnv->pMap->nMapLayers == 0) {
        return;
    }

    const u8 *pShow = (const u8 *)*pEnv->ppShowShip;
    if (pShow == NULL) {
        return;
    }

    const i32 nEntries = (i32)*(const u16 *)(pShow + 0);
    const i32 nEntryLen = (i32)*(const u16 *)(pShow + 2);
    if (nEntries <= 0) {
        return;
    }

    const i32 nHalfX = nEntryLen / 2;
    const i32 nHalfY = nEntries / 2;

    for (i32 r = 0; r < nEntries; ++r) {
        for (i32 c = 0; c < nEntryLen; ++c) {
            const i32 nX = PUFO_CELLS_PER_TILE * (i32)*pEnv->pnCraftMapX - nHalfX + c + nExtX;
            const i32 nY = PUFO_CELLS_PER_TILE * (i32)*pEnv->pnCraftMapY - nHalfY + r + nExtY;

            if (!Map_IsValidCell(&pEnv->pMap->geom, nX, nY, 0)) {
                continue;
            }
            if (pShow[PUFO_SHOWSHIP_HDR_BYTES + r * nEntryLen + c] == PUFO_SHOWSHIP_NONE) {
                continue;
            }
            for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
                MapCell *pCell = Init_LookupCell(nX, nY, nLayer, pEnv);
                if (pCell != NULL) {
                    (void)MapCell_SetBFlags29_4(pCell);
                }
            }
        }
    }
}

/* 0x3CFE4..0x3D094: when unk_128AD6 is clear, every cell's occlusion height is reset so
 * Map_RefreshCellMeta can rebuild it from scratch. */
static void Init_ClearCellMeta(TactInitEnv *pEnv)
{
    if (*pEnv->pbUnkMap1511E != 0) {
        return;
    }

    const i32 nDimX = (i32)pEnv->pMap->geom.nDimX;
    const i32 nDimY = (i32)pEnv->pMap->geom.nDimY;

    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 y = 0; y < PUFO_CELLS_PER_TILE * nDimY; ++y) {
            for (i32 x = 0; x < PUFO_CELLS_PER_TILE * nDimX; ++x) {
                MapCell *pCell = Init_LookupCell(x, y, nLayer, pEnv);
                if (pCell != NULL) {
                    pCell->bUnk56 = 0;
                }
            }
        }
    }
}

/* Copy one NUL-terminated string into a panel line buffer -- the original's LDRSB/STRB
 * loop. Deviation: the original writes with no bound; this stops at the 50-byte buffer. */
static void Init_CopyLine(UiPanel *pPanel, i32 nLine, const char *pszSrc)
{
    if (pPanel == NULL || pszSrc == NULL) {
        return;
    }
    if (nLine < 0 || nLine >= PUFO_PANEL_LINES) {
        return;
    }

    u8 *pDst = pPanel->apLines[nLine];
    if (pDst == NULL) {
        return;
    }

    i32 i = 0;
    for (; i < PUFO_PANEL_LINE_BYTES - 1; ++i) {
        const char c = pszSrc[i];
        pDst[i] = (u8)c;
        if (c == 0) {
            break;
        }
    }
    pDst[i] = 0;
}

/* One aTextRecs slice, bounds-checked against the loaded blob. */
static const char *Init_BriefText(const TactInitEnv *pEnv, u32 nOff)
{
    if (pEnv->pTextRecs == NULL || nOff >= pEnv->nTextRecCount) {
        return NULL;
    }
    return (const char *)pEnv->pTextRecs + nOff;
}

/* 0x3D2A8..0x3D540: the Cydonia briefing.
 *
 * The pad is allocated, loaded at mode 0x21, and its origins cleared. Then the text is
 * assembled from aTextRecs slices into a 0x348-byte stack buffer, wrapped into the panel
 * from line 2 (mission type 9) or line 0 (everything else), and two more lines are
 * appended by hand: the fixed line at 0x10DA38 and the closing slice.
 *
 * The panel's line count matters here. UiPanel_ctor stores its argument in nLineCount, so panel 4 starts the
 * mission at 28 with 28 empty lines; UiPanel_WrapText then REPLACES that count with lineStart + the lines it
 * committed, and the two appends land at the replaced count. That is inside the panel as long as the wrap stays
 * below 26. Past it the slot is unallocated and holds 0, and the original -- which treats the slot as a
 * panel-relative word offset and follows the line record's +0xBE8 field as the buffer -- would write through
 * whatever lives at panel+0xBE8. Recorded latent fault; the port's Init_CopyLine refuses the unallocated slot. */
static void Init_Briefing(TactInitEnv *pEnv)
{
    char aszText[PUFO_TACTINIT_TEXT_CAP];
    UiPanel *pPanel = pEnv->apPanel[PUFO_BRIEF_PANEL];

    *pEnv->pnUiSlot11 = (i32)*pEnv->puUiScreen;
    *pEnv->puUiScreen = 20;

    PadImage *pPad = (PadImage *)malloc(sizeof(PadImage));
    *pEnv->ppBriefPad = pPad;
    if (pPad != NULL) {
        PadImage_ctor(pPad);
        if (PadImage_Load(pPad, "cydonia", 0x21)) {
            pPad->nOriginX = 0;
            pPad->nOriginY = 0;
        }
    }

    if (pPanel == NULL) {
        return;
    }

    aszText[0] = 0;

    if ((i32)pEnv->pMap->nMissionType == PUFO_MISSION_TYPE_BRIEF_A) {
        (void)pufo_strcat(aszText, Init_BriefText(pEnv, PUFO_BRIEF_A_HEAD), sizeof aszText);
        (void)pufo_strcat(aszText, Init_BriefText(pEnv, PUFO_BRIEF_A_JOIN), sizeof aszText);
        Init_CopyLine(pPanel, 0, Init_BriefText(pEnv, PUFO_BRIEF_A_LINE0));
        Init_CopyLine(pPanel, 1, pEnv->pszBriefLine);
        if (pEnv->hooks.pfnPanelWrapText != NULL) {
            (void)pEnv->hooks.pfnPanelWrapText(pPanel, PUFO_TACTINIT_WRAP_W, 2, aszText,
                                               pEnv->hooks.pUser);
        }
    } else {
        (void)pufo_strcat(aszText, Init_BriefText(pEnv, PUFO_BRIEF_B_HEAD), sizeof aszText);
        (void)pufo_strcat(aszText, Init_BriefText(pEnv, PUFO_BRIEF_B_JOIN1), sizeof aszText);
        (void)pufo_strcat(aszText, Init_BriefText(pEnv, PUFO_BRIEF_B_JOIN2), sizeof aszText);
        if (pEnv->hooks.pfnPanelWrapText != NULL) {
            (void)pEnv->hooks.pfnPanelWrapText(pPanel, PUFO_TACTINIT_WRAP_W, 0, aszText,
                                               pEnv->hooks.pUser);
        }
    }

    Init_CopyLine(pPanel, pPanel->nLineCount, pEnv->pszBriefLine);
    Init_CopyLine(pPanel, pPanel->nLineCount + 1, Init_BriefText(pEnv, PUFO_BRIEF_TAIL));
    pPanel->nLineCount += 2;
}

/* Fill the mission-derived half of every callee context. The caller fills the
 * world-dependent half -- hooks, tables, the soldier array, the craft roster. */
static void Init_FillCalleeEnvs(TactInitEnv *pEnv)
{
    GameMap *pMap = pEnv->pMap;

    MapTerrainEnv *pT = &pEnv->terrain;
    pT->pnMapLayers      = &pMap->nMapLayers;
    pT->pnMissionType    = &pMap->nMissionType;
    pT->pnMapDimX        = &pMap->geom.nDimX;
    pT->pnMapDimY        = &pMap->geom.nDimY;
    pT->pPlaneTileX      = pEnv->pnCraftMapX;
    pT->pPlaneTileY      = pEnv->pnCraftMapY;
    pT->pbSpawnAlt       = pEnv->pbSpawnAlt;
    pT->pnTerrainVariant = pEnv->pnTerrainVariant;
    pT->pnTier           = pEnv->pnTier;
    pT->pnTierSource     = pEnv->pnTierSource;
    pT->pbUnk15115       = pEnv->pbUnk15115;
    pT->pnShotX          = pEnv->pnShotX;
    pT->pnShotY          = pEnv->pnShotY;
    pT->pnSpawnTileX     = pEnv->pnSpawnTileX;
    pT->pnSpawnTileY     = pEnv->pnSpawnTileY;
    pT->apCells          = pEnv->apCells;
    pT->apShotWeapons    = (const ObjectRec *const *)pEnv->apObjects;
    pT->paTileTypes      = pEnv->pTileTypes;
    pT->pRmp             = pEnv->pRmp;
    pT->pFont            = pEnv->pFont;
    /* The craft sprite group is the u16 at fontSys+0x06, which is also the flag-3 template base the
     * builder resolves its pad slots through. Map_BuildMissionTerrain refreshes it after the
     * Font_LoadTactSprites call that writes it, so this is only the pre-build value. */
    pT->nCraftSprBase      = Font_GetFlagSprBase(pEnv->pFont, 3);
    pT->pfnLoadTactSprites = pEnv->hooks.pfnLoadTactSprites;
    pT->pUser              = pEnv->hooks.pUser;

    pEnv->select.ppSelUnit   = &pMap->pSelUnit;
    pEnv->select.pCursorPos  = &pEnv->cursorPos;
    pEnv->select.pfnLookup   = Init_LookupCell;
    pEnv->select.pLookupUser = pEnv;
    pEnv->select.pStatus     = &pMap->status;

    pEnv->teamSelect.nMapLayers  = pMap->nMapLayers;
    pEnv->teamSelect.nCraftMapX  = *pEnv->pnCraftMapX;
    pEnv->teamSelect.nCraftMapY  = *pEnv->pnCraftMapY;
    pEnv->teamSelect.apXcomUnits = pMap->apXcomUnits;
    pEnv->teamSelect.pnXcomCount = &pMap->nXcomCount;
    pEnv->teamSelect.pfnLookup   = Init_LookupCell;
    pEnv->teamSelect.pLookupUser = pEnv;
    pEnv->teamSelect.pSelectCtx  = &pEnv->select;
    /* The same aDeployCellOffsets table the deploy pass reads; the original reaches
     * it through the global in both places. */
    pEnv->teamSelect.pDeployOffsets = pEnv->pDeployOffsets;

    pEnv->cargo.apObjects       = pEnv->apObjects;
    pEnv->cargo.apTactItems     = pEnv->apTactItems;
    pEnv->cargo.pnTactItemCount = pEnv->pnTactItemCount;
    pEnv->cargo.ppDeployCell    = pEnv->ppDeployCell;
    pEnv->cargo.pDeployOffsets  = pEnv->pDeployOffsets;
    pEnv->cargo.nMissionType    = pMap->nMissionType;
    pEnv->cargo.nMapLayers      = pMap->nMapLayers;
    pEnv->cargo.nCraftMapX      = *pEnv->pnCraftMapX;
    pEnv->cargo.nCraftMapY      = *pEnv->pnCraftMapY;
    pEnv->cargo.pfnLookupCell   = Init_LookupCell;
    pEnv->cargo.pLookupUser     = pEnv;

    pEnv->equip.pItemListCell   = *pEnv->ppDeployCell;
    pEnv->equip.apXcomUnits     = pMap->apXcomUnits;
    pEnv->equip.pnXcomUnitCount = &pMap->nXcomCount;
    pEnv->equip.apObjects       = pEnv->apObjects;

    pEnv->deploy.apXcomUnits      = pMap->apXcomUnits;
    pEnv->deploy.pnXcomUnitCount  = &pMap->nXcomCount;
    pEnv->deploy.apTactItems      = pEnv->apTactItems;
    pEnv->deploy.pnTactItemCount  = pEnv->pnTactItemCount;
    pEnv->deploy.apObjects        = pEnv->apObjects;
    pEnv->deploy.pfnLookupCell    = Init_LookupCell;
    pEnv->deploy.pLookupUser      = pEnv;
    pEnv->deploy.nMapLayers       = (i32)pMap->nMapLayers;
    pEnv->deploy.nCraftMapX       = *pEnv->pnCraftMapX;
    pEnv->deploy.nCraftMapY       = *pEnv->pnCraftMapY;
    pEnv->deploy.nMissionType     = pMap->nMissionType;
    pEnv->deploy.pDeployOffsets   = pEnv->pDeployOffsets;
    pEnv->deploy.pDeployPointCounts = pEnv->pDeployPointCounts;
    pEnv->deploy.pRmp             = pEnv->pRmp;
    pEnv->deploy.pSelect          = &pEnv->select;

    pEnv->cursorPos.pCursor          = &pEnv->cursor;
    pEnv->cursorPos.geom             = pMap->geom;
    pEnv->cursorPos.bBusy            = pEnv->bBusy;
    pEnv->cursorPos.bSelUnitLarge    = pEnv->bSelUnitLarge;
    pEnv->cursorPos.bSuppressLarge   = pEnv->bSuppressLarge;

    pEnv->focus.nMapTileW   = pEnv->tileSize.nTileW;
    pEnv->focus.nMapTileH   = pEnv->tileSize.nTileH;
    pEnv->focus.nDimX       = pMap->geom.nDimX;
    pEnv->focus.nDimY       = pMap->geom.nDimY;
    pEnv->focus.nCamScrollX = pEnv->nCamScrollX;
    pEnv->focus.nCamScrollY = pEnv->nCamScrollY;
    pEnv->focus.camOffset   = pEnv->camOffset;

    pEnv->vis.pfnLookup     = Init_LookupCell;
    pEnv->vis.pUser         = pEnv;
    pEnv->vis.pGeom         = &pMap->geom;
    pEnv->vis.apRays        = pEnv->apVisRays;
    pEnv->vis.nMissionType  = pMap->nMissionType;
    pEnv->vis.pAim          = &pMap->aim;
    pEnv->vis.pTactCtx      = &pMap->tact;
    pEnv->vis.pbFireStatus1 = &pMap->bFireStatus1;

    pEnv->invOpen.pSelUnit    = pMap->pSelUnit;
    pEnv->invOpen.pCursor     = &pEnv->cursor;
    pEnv->invOpen.geom        = pMap->geom;
    pEnv->invOpen.pfnLookup   = Init_LookupCell;
    pEnv->invOpen.pLookupUser = pEnv;
    pEnv->invOpen.nXcomCount  = pMap->nXcomCount;
    pEnv->invOpen.bSweepGate  = *pEnv->pbSweepArmed;
}

/* The selected unit's cell, which is what MapView_FocusSelected centres on. */
static const MapCell *Init_SelCell(const TactInitEnv *pEnv)
{
    return (pEnv->pMap->pSelUnit != NULL) ? pEnv->pMap->pSelUnit->pCell : NULL;
}

/* 0x3CBB8..0x3D128: the new-mission path. */
static void Init_NewMission(TactInitEnv *pEnv)
{
    Map_BuildMissionTerrain(&pEnv->terrain);
    Init_DeployCellFlags(pEnv);
    (void)Tact_LoadLoftData(pEnv->pLoft);
    pEnv->vis.pTables = pEnv->pLoft;

    (void)Tact_SelectTeamUnits(&pEnv->teamSelect);
    (void)Tact_SpawnAliens(&pEnv->spawnAliens);
    Tact_SetupCraftCargo(&pEnv->cargo);
    pEnv->equip.pItemListCell = *pEnv->ppDeployCell;
    Tact_EquipTeamFromCraft(&pEnv->equip);

    Init_MissionCellFlags(pEnv);
    Init_RefreshVisibility(pEnv);

    /* 0x3CE70: the layer switch. Layers 1 and 3 give (5,10), layer 2 gives (5,5). Layer 0
     * skips the block that uses them, and layers 4 or more reach it with values the
     * function never wrote; the port initialises both to zero. See bugs.tsv. */
    i32 nExtX = 0;
    i32 nExtY = 0;
    switch (pEnv->pMap->nMapLayers) {
        case 1:
        case 3: nExtX = 5; nExtY = 10; break;
        case 2: nExtX = 5; nExtY = 5;  break;
        default: break;
    }

    Init_ShowShipReveal(pEnv, nExtX, nExtY);
    Init_ClearCellMeta(pEnv);

    if (pEnv->pMap->nXcomCount != 0) {
        Tact_SelectUnit(&pEnv->select, pEnv->pMap->apXcomUnits[0]);
        if (pEnv->hooks.pfnOpenInventory != NULL) {
            (void)pEnv->hooks.pfnOpenInventory(pEnv->hooks.pUser);
        }
    } else {
        *pEnv->pbUnkMap01 = 0;
        (void)Tact_DeployTeamAndHwps(&pEnv->deploy);
        MapView_FocusSelected(&pEnv->focus, Init_SelCell(pEnv), *pEnv->pbSweepArmed,
                              &pEnv->cursor);
        /* 0x3D0D4: the count is re-read after the deploy, so hardware soldiers it just
         * added do get a visibility pass. */
        Init_RefreshVisibility(pEnv);
    }

    /* 0x3D128: both branches end the same way. */
    MapView_FocusSelected(&pEnv->focus, Init_SelCell(pEnv), *pEnv->pbSweepArmed,
                          &pEnv->cursor);
    (void)Map_RefreshCellMeta(Init_LookupCell, pEnv, &pEnv->pMap->geom,
                              *pEnv->pbLightEnabled != 0, pEnv->pLightmap);
}

/* 0x3D138..0x3D24C: the save-load path. The grid is allocated here rather than built. */
static void Init_LoadMission(TactInitEnv *pEnv)
{
    /* The grid is sized from the dims the save carries, so the chunk's two header bytes are read
     * before the allocation. Save_ReadMap re-finds the chunk, so nothing here consumes the stream. */
    (void)Save_ReadMapDims(pEnv->pDat, &pEnv->pMap->geom);

    const i32 nDimX = (i32)pEnv->pMap->geom.nDimX;
    const i32 nDimY = (i32)pEnv->pMap->geom.nDimY;

    /* 0x3D168..0x3D1C0, the original's own loops: R7 (col) resets to 0 on every row, the table slot is
     * (60*row + col) + 3600*layer -- R8 walks +4 per column and +0x3840 (=3600 dwords) per layer -- and
     * MapCell_Init gets (col, row, layer). The slot MUST carry the column term: leaving it out stores one
     * pointer per row instead of 60, so 59 slots in 60 stay NULL and the "map" chunk's records land on the
     * wrong cells. */
    for (i32 y = 0; y < PUFO_CELLS_PER_TILE * nDimY; ++y) {
        for (i32 x = 0; x < PUFO_CELLS_PER_TILE * nDimX; ++x) {
            for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
                MapCell *pCell = (MapCell *)malloc(sizeof(MapCell));

                if (pCell != NULL) {
                    (void)MapCell_Reset(pCell);
                }
                pEnv->apCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer
                              + PUFO_MAP_CELL_DIM * y + x] = pCell;
                /* The original calls Init with the null pointer too. Reproduced. */
                (void)MapCell_Init(pCell, x, y, nLayer);
            }
        }
    }

    (void)Tact_LoadLoftData(pEnv->pLoft);
    pEnv->vis.pTables = pEnv->pLoft;

    (void)Save_AllocFlobjs(pEnv->pDat, *pEnv->pnFlobjsWide, pEnv->pnTactItemCount,
                           pEnv->apTactItems);
    /* The cell lookup is the mission's own grid, allocated by the loops just above (0x3D168..0x3D1C0):
     * 0x419DC resolves each flagged soldier's three cell bytes against it, so the order here is the
     * original's -- grid first, soldiers second. */
    /* The three bInTact arms of a soldier record resolve their ids here, on the mission's own tables:
     * the cell grid allocated just above, the tactical item pool Save_AllocFlobjs filled one line
     * before (Map.apTactItems @ Map+0x23438, indexed by TactItem.nRefId), and the roster itself. */
    {
        SoldierRecordEnv arms;

        memset(&arms, 0, sizeof(arms));
        arms.pfnCell        = Init_LookupCell;
        arms.pCellUser      = pEnv;
        arms.apTactItems    = pEnv->apTactItems;
        arms.nTactItemCount = *pEnv->pnTactItemCount;
        arms.pEmptyItem     = (TactItem *)pEnv->pUnk138EA0;
        (void)Save_ReadSoldiers(pEnv->pDat, pEnv->apSoldiers, pEnv->pnSoldierCount,
                                pEnv->nSoldierCapacity, &arms, *pEnv->pbTactFlag);
    }
    (void)Save_ReadTGameData(pEnv->pDat, pEnv->pSaveState, pEnv->pSaveRosters,
                             pEnv->pSaveFlobjs, &pEnv->pMap->geom, Init_LookupCell, pEnv,
                             pEnv->pSaveReset, pEnv->pSaveHooks);
    (void)Save_ReadRmp(pEnv->pDat, pEnv->pRmp, pEnv->pfnLoadRmp, pEnv->pRmpUser);

    pEnv->craftPatch.nCraftSprBase = pEnv->terrain.nCraftSprBase;
    pEnv->craftPatch.apCells      = pEnv->apCells;
    pEnv->craftPatch.nMapLayers   = (i32)pEnv->pMap->nMapLayers;
    pEnv->craftPatch.nCraftMapX   = (i32)*pEnv->pnCraftMapX;
    pEnv->craftPatch.nCraftMapY   = (i32)*pEnv->pnCraftMapY;
    Map_PatchCraftSprites(pEnv->pFont, &pEnv->craftPatch);

    MapView_FocusSelected(&pEnv->focus, Init_SelCell(pEnv), *pEnv->pbSweepArmed,
                          &pEnv->cursor);
}

bool Tact_InitMission(TactInitEnv *pEnv, i32 nMode)
{
    if (pEnv == NULL || pEnv->pMap == NULL || pEnv->apCells == NULL || pEnv->pLoft == NULL) {
        return false;
    }

    AssetReader rdr;
    AssetReader_Clear(&rdr);

    /* 0x3C56C..0x3C5B0: two random mission parameters. The second is stored as a dword
     * even though IDA names the global a word. */
    *pEnv->pnUnkMap254B4 = (i32)Game_RandU16(0x28) + 10;
    *pEnv->pnUnkMap254B8 = (i32)Game_RandU16(0x14);

    /* 0x3C5B8..0x3C64C: the three singletons. Caller-owned storage -- see deviation 1. */
    *pEnv->pbDrawIsoLayer = 0;
    TactState_ctor(pEnv->pTactState);
    TactCtx_ctor(&pEnv->pMap->tact);
    TactBuf11408_ctor(pEnv->pRmp);

    /* 0x3C64C..0x3C734: the flag block, in the ARM's order. */
    *pEnv->pbAltOutcomePath = 0;                    /* unk_1153D9 @ Map+0x1A21 */
    *pEnv->pbBattleEnded    = 0;                    /* unk_113A14 @ Map+0x5C */
    *pEnv->pnUiSlot1        = 0;                    /* apUiSlotsA[1] @ Map+0x64 */
    *pEnv->pbGameTickHold   = 0;                    /* Map+0x5A */
    *pEnv->pbUnkMap01       = 1;                    /* unk_1139B9 @ Map+0x01 */
    *pEnv->puUiScreen       = 0;                    /* unk_1139F2 @ Map+0x3A */
    *pEnv->pbFire0          = 0;                    /* unk_113A08 @ Map+0x50 */
    *pEnv->pnTactItemCount  = 0;                    /* Map_nTactItemCount @ Map+0x1513C */
    *pEnv->pbTactFlag       = 1;                    /* Map+0x00 */
    pEnv->pTactState->nQueueLen = 0;                /* TactState+0x111D4 */
    *pEnv->pbPlayerInput    = 1;                    /* unk_1139F0 @ Map+0x38 */
    pEnv->pTactState->bQueueActive = 0;             /* TactState+0x111D6 */
    pEnv->pMap->bFireStatus1   = 0;                 /* Map+0x51 */
    pEnv->pMap->bActionLatch   = 0;                 /* Map+0x02 */
    pEnv->pMap->geom.nDimX     = 6;                 /* Map_nMapDimX @ Map+0x1511B */
    pEnv->pMap->geom.nDimY     = 6;                 /* Map_nMapDimY @ Map+0x1511C */
    pEnv->pMap->status.nCount  = 0;                 /* unk_113A0C @ Map+0x54 */
    *pEnv->pbFire2             = 0;                 /* byte_113A0A @ Map+0x52 */
    pEnv->pTactState->gap_70112[1] = 0;             /* TactState+0x111E1 */
    pEnv->pTactState->bAiActionActive    = 0;             /* TactState+0x111E2 */
    pEnv->pTactState->bPhaseActive = 0;             /* TactState+0x111E3 */
    pEnv->pMap->nCivilianCount = 0;                 /* nCivilianUnitCount @ Map+0x15120 */
    *pEnv->pbSlot8Byte2 = 0;                        /* Map+0x82 */
    *pEnv->pbSlot8Byte3 = 0;                        /* Map+0x83 */
    *pEnv->pnUiSlot9    = 0;                        /* apUiSlotsA[9] @ Map+0x84 */
    *pEnv->pnUiSlot10   = 10;                       /* apUiSlotsA[10] @ Map+0x88 */

    /* 0x3C720..0x3C734: the selected-weapon record, unk_10DB90. */
    pEnv->pWeaponSel->pObj       = pEnv->apObjects[1];
    pEnv->pWeaponSel->bConst10   = PUFO_WEAPON_SEL_CONST;
    pEnv->pWeaponSel->uAmmoTypeId = pEnv->apObjects[1]->anAmmoTypeId[0];

    Init_AllocPanels(pEnv);
    Init_SetupPanels(pEnv);
    Init_LoadShowShip(pEnv, &rdr);

    PufoFont_Init(pEnv->pFont);

    /* 0x3CB30: Map_InitFacingScratch(pMap, 1). The facing argument is the constant 1 and
     * the tactical flag is the byte the prologue just set. */
    (void)Map_InitFacingScratch(&pEnv->facing, &pEnv->tileSize, 1,
                                *pEnv->pbTactFlag != 0, (u8)pEnv->pMap->nLayerScratch,
                                pEnv->pnTileCountX, pEnv->pnTileCountY);

    /* 0x3CB40..0x3CB5C: the three static TactItem records seeded from Map.apObjects.
     * The original stores through the ILP32 byte offsets 0x00 (unk_138EA0) and 0x14
     * (unk_138EB4); those offsets name the pObj and nRefId fields of the record. On
     * Win64 the pointer fields slide (pNext moves to 0x10), so the fields are written
     * by name -- same record, same two dwords, no raw offset. */
    ((TactItem *)pEnv->pUnk138EA0)->pObj   = pEnv->apObjects[0];
    ((TactItem *)pEnv->pUnk138EA0)->nRefId = 0x7D00;
    /* 0x3CB40..0x3CB5C stores each object into the record's pObj field. */
    pEnv->pItem156A88->pObj = pEnv->apObjects[PUFO_SHOT_OBJ_VARIANT3];
    pEnv->pItem156910->pObj = pEnv->apObjects[PUFO_SHOT_OBJ_VARIANT2];
    pEnv->pItem156A58->pObj = pEnv->apObjects[141];

    Init_LoadLightmap(pEnv, &rdr);

    /* 0x3CB80..0x3CB8C. The original calls Tact_LoadPalettes with whatever
     * AssetReader_Read left in R0, but in ARM that function takes no argument at all --
     * Tact_BuildPaletteLuts reads Map_nGamma itself. The port's signature takes the gamma
     * explicitly, so it is passed here. */
    Tact_LoadPalettes(*pEnv->pnGamma);
    if (pEnv->hooks.pfnLoadUiPads != NULL) {
        (void)pEnv->hooks.pfnLoadUiPads(pEnv->hooks.pUser);
    }
    if (pEnv->hooks.pfnInitUiControls != NULL) {
        pEnv->hooks.pfnInitUiControls(pEnv->hooks.pUser);
    }
    Tact_BuildVisRayTable(pEnv->apVisRays, pEnv->paVisSteps);

    Init_FillCalleeEnvs(pEnv);

    /* 0x3CB90..0x3CBE0: the mode split. */
    if (nMode == 0) {
        *pEnv->pbUnkMap254D4 = 0;
    } else if (nMode == 1) {
        (void)Save_ReadObjBefore(pEnv->pDat, pEnv->apObjects, pEnv->nObjectCount);
    }

    pEnv->pMap->nXcomCount     = 0;
    pEnv->pMap->nEnemyCount    = 0;
    pEnv->pMap->nCivilianCount = 0;

    if (nMode == 0) {
        Init_NewMission(pEnv);
    } else {
        Init_LoadMission(pEnv);
    }

    /* 0x3D24C..0x3D560: the shared tail. */
    Timer_StartBySpeed(pEnv->pClock->nSpeed);
    (void)GameSpeed_Set(pEnv->pClock, pEnv->pClock->nSpeed);
    *pEnv->pbSweepArmed = 1;

    if (pEnv->hooks.pfnSoundInit != NULL) {
        (void)pEnv->hooks.pfnSoundInit("tact", pEnv->hooks.pUser);
    }
    if (pEnv->hooks.pfnSoundPlayMusic != NULL) {
        pEnv->hooks.pfnSoundPlayMusic(1, pEnv->hooks.pUser);
    }
    if (pEnv->hooks.pfnSoundApplyMusicVolume != NULL) {
        pEnv->hooks.pfnSoundApplyMusicVolume(pEnv->hooks.pUser);
    }
    if (pEnv->hooks.pfnSoundApplySfxVolume != NULL) {
        pEnv->hooks.pfnSoundApplySfxVolume(pEnv->hooks.pUser);
    }

    if (*pEnv->pbSpawnAlt != 0 && nMode == 0) {
        Init_Briefing(pEnv);
    } else {
        /* 0x3D548: the briefing pad pointer is dropped, not freed. Reproduced. */
        *pEnv->ppBriefPad = NULL;
    }

    *pEnv->pbDrawIsoLayer = 1;
    return true;
}
