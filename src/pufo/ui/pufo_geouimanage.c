/* pufo_geouimanage.c - the geoscape UI's teardown.
 *
 * Evidence: GeoscapeUi_FreeAll @ 0x86d20. See the header for the owned-object map and
 * the three count widths.
 */
#include "ui/pufo_geouimanage.h"

#include <stdlib.h>

/* 0x86D50..0x86D7C: the thirteen widgets the UI root at 0x1864F8 holds. */
static void FreeRootCtls(GeoscapeUiState *pUi)
{
    for (i32 i = 0; i < PUFO_GEOUI_ROOT_CTLS; ++i) {
        UiControl *pCtl = pUi->apRootCtl[i]; /* 0x86D50 */
        if (pCtl != NULL) {
            UiControl_FreePads(pCtl);        /* 0x86D60 */
            free(pCtl);                      /* 0x86D68, operator delete */
            pUi->apRootCtl[i] = NULL;
        }
    }
}

/* 0x86D88..0x86F28: the pad singletons. Every one is freed as pixel data plus the struct,
 * except the world-size block, which is neither a PadImage nor checked for null. */
static void FreePadSingletons(GeoscapeUiState *pUi)
{
    for (i32 i = 0; i < PUFO_GEOUI_PAD_SLOTS; ++i) {
        PadImage *pPad = pUi->apPad[i];
        if (pPad != NULL) {
            PadImage_FreeData(pPad);
            free(pPad);
            pUi->apPad[i] = NULL;
        }
    }

    if (pUi->pWorldSize != NULL) { /* 0x86DC0 loads 0x186BC0 and deletes it unchecked */
        free(pUi->pWorldSize);
        pUi->pWorldSize = NULL;
    }
}

/* 0x86F38..0x86F64: the ten layer pads at 0x186BF0. */
static void FreeLayerPads(GeoscapeUiState *pUi)
{
    for (i32 i = 0; i < PUFO_GEOUI_LAYER_PADS; ++i) {
        PadImage *pPad = pUi->apLayerPad[i];
        if (pPad != NULL) {
            PadImage_FreeData(pPad);
            free(pPad);
            pUi->apLayerPad[i] = NULL;
        }
    }
}

/* 0x86F70..0x87104: the thirteen widget singletons. */
static void FreeCtlSingletons(GeoscapeUiState *pUi)
{
    for (i32 i = 0; i < PUFO_GEOUI_PAD_SLOTS; ++i) {
        UiControl *pCtl = pUi->apCtl[i];
        if (pCtl != NULL) {
            UiControl_FreePads(pCtl);
            free(pCtl);
            pUi->apCtl[i] = NULL;
        }
    }
}

/* 0x8710C..0x87208: the world, and only when the argument is non-zero. Each count is read
 * at its own width and each gate is `count <= 0`, so a negative base count skips the loop. */
static void FreeWorld(const GeoscapeRoster *pRoster)
{
    if (pRoster == NULL) {
        return;
    }

    if (pRoster->apSoldiers != NULL) {
        for (i32 i = 0; (u16)i < pRoster->nSoldierCount; ++i) { /* LDRH at 0x87114 */
            free(pRoster->apSoldiers[i]);
            pRoster->apSoldiers[i] = NULL;
        }
    }

    if (pRoster->apBases != NULL) {
        for (i32 i = 0; i < (i32)pRoster->nBaseCount; ++i) { /* LDRSB at 0x87178 */
            free(pRoster->apBases[i]);
            pRoster->apBases[i] = NULL;
        }
    }

    if (pRoster->apCraft != NULL) {
        for (i32 i = 0; (u8)i < pRoster->nCraftCount; ++i) { /* LDRB at 0x871C8 */
            free(pRoster->apCraft[i]);
            pRoster->apCraft[i] = NULL;
        }
    }

    if (pRoster->pStrategList != NULL) {
        PtrArray_DeleteAll(pRoster->pStrategList); /* 0x87208, 0x1864F8 + 0x3C */
    }
}

void GeoscapeUi_FreeAll(GeoscapeUiState *pUi, const GeoscapeRoster *pRoster, char bFreeWorld)
{
    if (pUi == NULL) {
        return;
    }

    Timer_KillD();                        /* 0x86D28 */

    if (pUi->pbGeoscapeUi != NULL) {
        *pUi->pbGeoscapeUi = 0;           /* 0x86D34, Map.bGeoscapeUi */
    }

    if (pUi->pFont != NULL) {
        PufoFont_Free(pUi->pFont);        /* 0x86D3C, fontSys @ 0x158268 */
    }

    FreeRootCtls(pUi);
    FreePadSingletons(pUi);
    FreeLayerPads(pUi);
    FreeCtlSingletons(pUi);

    if (bFreeWorld != 0) {                /* 0x8710C */
        FreeWorld(pRoster);
    }

    /* 0x87220 writes 20 into panel + 0xBDC and only then tests the panel for null. The port
     * tests first; the original would fault on a null panel. */
    if (pUi->pStatusPanel != NULL) {
        pUi->pStatusPanel->nLineCount = 20;
        UiPanel_FreeLines(pUi->pStatusPanel); /* 0x87234 */
        free(pUi->pStatusPanel);              /* 0x8723C */
        pUi->pStatusPanel = NULL;
    }

    Sound_Shutdown();                     /* 0x87244, tail call */
}
