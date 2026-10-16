/* pufo_geopointer.c - Geoscape_OnPointer @ 0x872d0, the geoscape pointer dispatcher.
 *
 * See pufo_geopointer.h for the evidence, the arm-by-arm shape and the deviations.
 * The addresses in the comments are the original's own.
 */
#include "game/pufo_geopointer.h"

#include <stddef.h>

/* The message-list flags the world arm dispatches on, in the order it tests them. */
#define GEO_FLAG_SITE_LIST   10
#define GEO_FLAG_INTERCEPT   11
#define GEO_FLAG_SELECT_ROW  12
#define GEO_FLAG_CLOSE_ONLY  13
#define GEO_FLAG_CANCEL      14
#define GEO_FLAG_CONFIRM     15
#define GEO_FLAG_PLACEMENT   16
#define GEO_FLAG_BUILD       17
#define GEO_FLAG_MULTI_A     18
#define GEO_FLAG_MULTI_B     19
#define GEO_FLAG_MULTI_C     20
#define GEO_FLAG_ALT_A       21
#define GEO_FLAG_ALT_B       22
#define GEO_FLAG_ALT_C       23
#define GEO_FLAG_SCROLLING   27
#define GEO_FLAG_SPAWN_MSG   60

/* The tap division the original builds out of shifts (0x87E94..0x87EAC): sign the value,
 * add 31 when it is negative, then arithmetic-shift right by five. It is not floor
 * division - (v + 31) >> 5 is what the ARM computes, and -31 gives 0, not -1. */
static i32 Ptr_Div32(i32 n)
{
    if (n < 0) {
        n += 31;
    }
    return n >> 5;
}

/* One hit test, in the form every arm uses: bDown = 1, bForceClick = 0 (0x87E40 /
 * 0x87E44 and the same pair at every call site). A NULL slot is skipped - the original
 * would dereference it (deviation 1). */
static i32 Ptr_Hit(UiControl *pCtl, i32 nX, i32 nY, const UiControlHitCtx *pHit)
{
    if (pCtl == NULL) {
        return 0;
    }
    return UiControl_HitTest(pCtl, nX, nY, true, false, pHit) ? 1 : 0;
}

static i32 Ptr_HitSlot(const GeoscapePointerEnv *pEnv, GeoscapeUiCtlSlot nSlot,
                       i32 nX, i32 nY)
{
    return Ptr_Hit(pEnv->apCtl[nSlot], nX, nY, pEnv->pHit);
}

/* A run of consecutive slots, from nFirst to nLast inclusive. */
static void Ptr_HitRange(UiControl *const *apCtl, i32 nFirst, i32 nLast,
                         i32 nX, i32 nY, const UiControlHitCtx *pHit)
{
    for (i32 i = nFirst; i <= nLast; ++i) {
        (void)Ptr_Hit(apCtl[i], nX, nY, pHit);
    }
}

/* Smap_HitTestStrategSelect @ 0x82be8 is ported in game/pufo_baseui.c, but it runs on its own
 * SmapHitEnv, so it reaches the dispatcher through this seam. With no hook installed the port reads
 * "nothing selectable under the pointer", which is what the app does today because it builds no
 * SmapHitEnv yet (deviation 2). */
static u8 Ptr_StrategHit(const GeoscapePointerEnv *pEnv, i32 nX, i32 nY)
{
    if (pEnv->pfnHitTestStrategSelect == NULL) {
        return 0;
    }
    return pEnv->pfnHitTestStrategSelect(pEnv->pView, nX, nY, pEnv->pUser);
}

/* ------------------------------------------------- screens 0 and 2: the world arm
 *
 * LABEL_13 @ 0x87530. The message-flag chain first, then the drag block, then the
 * thirteen root controls. */
static i32 PointerWorld(const GeoscapePointerEnv *pEnv, i32 nX, i32 nY, bool bDown)
{
    const u8 nFlag = UiLineList_GetFlag(pEnv->pLines, 0);

    if (nFlag == GEO_FLAG_PLACEMENT && bDown) {                              /* 0x87530 */
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
    }
    if (*pEnv->pbTargetSelectActive != 0) {                                  /* 0x8759C */
        const i32 nHit = Ptr_HitSlot(pEnv, GEOUI_CTL_186B8C, nX, nY);
        if (nHit != 0) {
            return nHit;
        }
    }
    if (nFlag == GEO_FLAG_BUILD && bDown) {                                  /* 0x875E4 */
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
    }
    if ((nFlag == GEO_FLAG_ALT_A || nFlag == GEO_FLAG_ALT_B ||
         nFlag == GEO_FLAG_ALT_C) && bDown) {                                /* 0x87658 */
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB0, nX, nY);
    }
    if ((nFlag == GEO_FLAG_MULTI_A || nFlag == GEO_FLAG_MULTI_B ||
         nFlag == GEO_FLAG_MULTI_C) && bDown) {                              /* 0x876C4..0x87810 */
        if (*pEnv->pbMapSelectionFlag != 0) {
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
            return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB0, nX, nY);
        }
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
    }
    if (nFlag == GEO_FLAG_CONFIRM && bDown) {                                /* 0x87890 */
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
    }
    if (nFlag == GEO_FLAG_CANCEL && bDown) {                                 /* 0x878D4 */
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
    }
    if (nFlag == GEO_FLAG_INTERCEPT && bDown) {                              /* 0x87938 */
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA8, nX, nY);
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B9C, nX, nY);
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B98, nX, nY);
        if (*pEnv->pbInterceptDone != 0) {
            return *pEnv->pbInterceptDone;
        }
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB0, nX, nY);
    }
    if (nFlag == GEO_FLAG_SELECT_ROW && bDown) {                             /* 0x8765C */
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB0, nX, nY);
    }
    if (*pEnv->pbRadarScreenActive != 0) {                                   /* 0x87A54 */
        Ptr_HitRange(pEnv->apRadarCtl, 0, PUFO_RADAR_CONTROLS - 1, nX, nY, pEnv->pHit);
        return PUFO_RADAR_CONTROLS;
    }
    if (*pEnv->pbMultiSelectPanel != 0) {                                    /* 0x87ABC */
        return UiPanel_TrackHoldRow(pEnv->pStatusPanel, nX, nY);
    }
    if (nFlag == GEO_FLAG_SITE_LIST && bDown) {                              /* 0x87AEC */
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
    }
    if (nFlag == GEO_FLAG_SPAWN_MSG) {                                       /* 0x87B50 */
        if (pEnv->pfnPlaySample != NULL) {
            pEnv->pfnPlaySample(17, pEnv->pUser);
        }
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
    }
    if (nFlag == GEO_FLAG_CLOSE_ONLY && bDown) {                             /* 0x87BC0 */
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB0, nX, nY);
    }

    /* ORIGINAL BUG (residue, 0x87C94): the original calls Smap_HitTestStrategSelect twice
     * with the same arguments on one press - once here to decide the drag flag and once
     * again inside the cooldown gate below. Reproduced as two calls.
     *
     * 0x87C2C: a press that misses a selectable object (or the scrolling flag) drops the
     * drag flag; otherwise the flag carries through. */
    const i32 nCurX = *pEnv->pnCursorX;
    const i32 nCurY = *pEnv->pnCursorY;
    u8 bDrag;
    if (bDown && (Ptr_StrategHit(pEnv, nCurX, nCurY) == 0 ||
                  nFlag == GEO_FLAG_SCROLLING)) {
        bDrag = 0;
        *pEnv->pbDragActive = 0;
    } else {
        bDrag = *pEnv->pbDragActive;
    }

    /* 0x87C48..0x87CE4: the strip height is the current zoom overlay pad's uHeight. */
    const i32 nStripH = pEnv->pZoomOverlay->uHeight;
    if (*pEnv->pnTapCooldown < 6 && bDown && nCurY < PUFO_SURF_H - nStripH) {
        if (Ptr_StrategHit(pEnv, nCurX, nCurY) != 0) {
            (void)Smap_GetTileAtScreen(pEnv->pView, pEnv->pTiles, nCurX, nCurY);
            *pEnv->pnTapCooldown = 255;
            return 255;
        }
    } else if (!bDown || nCurY >= PUFO_SURF_H - nStripH) {
        goto DragDone;
    }

    /* 0x87D58: start (or keep) the drag. */
    bDrag = 1;
    *pEnv->pbDragActive = 1;
    *pEnv->pnTapCooldown = 0;
    pEnv->pView->nLastDx = nCurX;
    pEnv->pView->nLastDy = nCurY;

DragDone:
    /* 0x87CF8: a message on the status line stops the globe scrolling. */
    if (bDrag != 0 && pEnv->pLines->nLineCount == 0) {
        Smap_ScrollBy(pEnv->pView, nCurX, nCurY, *pEnv->pnScreenId, nStripH);
    }
    if (nFlag == GEO_FLAG_SCROLLING) {                                       /* 0x87D28 */
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BB0, nX, nY);
    }
    /* ORIGINAL BUG (residue, 0x87D84): this tail hands back the drag flag, or - when the
     * flag is clear - the screen id itself. Screen 0 therefore returns 0 and screen 2
     * returns 2 from the same code path. No caller reads it: Input_OnPointer branches
     * away at 0x13CE8. Reproduced, not normalised. */
    i32 nResult = *pEnv->pbDragActive;
    if (nResult == 0) {                                                      /* 0x87D84 */
        nResult = *pEnv->pnScreenId;
        if (nResult != 2) {
            Ptr_HitRange(pEnv->apRootCtl, 0, PUFO_GEOUI_ROOT_CTLS - 1, nX, nY, pEnv->pHit);
        }
    }
    return nResult;
}

/* ------------------------------------------------- screen 1: the base strip */
static i32 PointerBaseStrip(const GeoscapePointerEnv *pEnv, i32 nX, i32 nY)
{
    (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);                      /* 0x87E1C */

    /* 0x87E30: the ten grid slots, and these are the only slots the original null-checks. */
    for (i32 i = 0; i < PUFO_GEO_STRIP_CTLS; ++i) {
        (void)Ptr_Hit(pEnv->apStripCtl[i], nX, nY, pEnv->pHit);
    }

    /* 0x87E6C: outside the map area the tap coordinates are left alone and the stored
     * tap flag decides; inside them the tap cell is recomputed and the flag is raised. */
    u8 bTap;
    if (nY >= 232 || nX >= 192 || nY <= 36) {
        bTap = *pEnv->pbPlaceActive;
    } else {
        *pEnv->pbPlaceFail = 0;                                              /* 0x87EA0 */
        *pEnv->pnTapX = Ptr_Div32(*pEnv->pnCursorX);                         /* 0x87EAC */
        *pEnv->pnTapY = Ptr_Div32(*pEnv->pnCursorY - 40);                    /* 0x87ECC */
        *pEnv->pbPlaceActive = 1;                                            /* 0x87ED4 */
        bTap = 1;
    }
    if (bTap != 0) {
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);                   /* 0x87F10 */
    }

    const i32 nBase = Base_HitTestFacilityStrip(&pEnv->stripHit,
                                                *pEnv->pnCursorX, *pEnv->pnCursorY);
    *pEnv->pnSelectedBaseIdx = (i8)nBase;                                    /* 0x87F34 */
    return nBase;
}

/* ------------------------------------------------- screens 3 and 17: the transfer strip
 *
 * Both arms run the strip hit test and then compare the result with the base the
 * transfer dialog has selected; a match inside the drawn rows drops the row selection. */
static i32 PointerTransferRow(const GeoscapePointerEnv *pEnv, i32 nX, i32 nY, u8 bClearPsiRow)
{
    const i32 nBase = Base_HitTestFacilityStrip(&pEnv->stripHit, nX, nY);
    *pEnv->pbStripHitBase = (i8)nBase;                                       /* 0x88AA4 */
    if (bClearPsiRow != 0) {
        *pEnv->pbPsiRowSel = 0;                                              /* 0x890C4 */
    }
    if ((i8)nBase == *pEnv->pnTransferBaseIdx &&
        *pEnv->pnCursorY < 28 * *pEnv->pnBaseCount) {                        /* 0x88AD8 */
        *pEnv->pbTransferRowSel = 0;
    }
    return nBase;
}

/* ------------------------------------------------- screen 5: the soldier-name edit */
static i32 PointerSoldierNames(const GeoscapePointerEnv *pEnv, i32 nX, i32 nY, bool bDown)
{
    if (*pEnv->pbNameEditActive != 0) {                                      /* 0x88128 */
        if (bDown && nY > 80 && nY < 290) {                                  /* 0x88154 */
            *pEnv->pbPopupB = 1;
            if (pEnv->pfnAppendNameGlyph != NULL) {
                pEnv->pfnAppendNameGlyph(nX, nY, pEnv->pUser);
            }
        }
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA8, nX, nY);
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186B9C, nX, nY);
    }

    /* 0x881F0: the first press inside the name band turns the edit on and parks the two
     * controls at the bottom of the screen with their handlers installed. */
    if (nX > 10 && nY > 1 && nX < 220 && nY < 30 && bDown) {
        UiControl *pBackspace = pEnv->apCtl[GEOUI_CTL_186BA8];
        UiControl *pConfirm = pEnv->apCtl[GEOUI_CTL_186B9C];
        *pEnv->pbNameEditActive = 1;
        *pEnv->pbUiAnimStarted = 0;
        if (pBackspace != NULL) {
            pBackspace->x = 1;
            pBackspace->y = 300;
            pBackspace->pfnClick = pEnv->pfnBackspaceName;
        }
        if (pConfirm != NULL) {
            pConfirm->x = 120;
            pConfirm->y = 300;
            pConfirm->pfnClick = pEnv->pfnCopyNameToEdit;
        }
    }

    (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);                       /* 0x88274 */
    if (*pEnv->pbTransferFlag != 0) {                                        /* 0x88284 */
        return UiPanel_TrackHoldRow(pEnv->pSolPanelEquip,
                                    *pEnv->pnCursorX, *pEnv->pnCursorY);
    }
    Ptr_HitRange(pEnv->apSolNameCtl, 0, 2, nX, nY, pEnv->pHit);              /* 0x882C4 */
    return 3;
}

/* ------------------------------------------------- screen 6: the craft loadout */
static i32 PointerCraft(const GeoscapePointerEnv *pEnv, i32 nX, i32 nY)
{
    *pEnv->pbPopupB = 1;                                                     /* 0x88304 */
    const i32 nCurX = *pEnv->pnCursorX;
    const i32 nCurY = *pEnv->pnCursorY;

    if (*pEnv->pbPlanePanelsBuilt != 0 && *pEnv->pbPlaneShowCraftDetail == 0) {  /* 0x88320 */
        *pEnv->pbPopupA = 1;
        (void)UiPanel_TrackHoldRow(pEnv->pPlaneCraftPanel, nCurX, nCurY);
        (void)UiPanel_TrackHoldRow(pEnv->pPlaneCraftStatusPanel, 20, nY);
        (void)UiPanel_ClickRow(pEnv->pPlaneCraftStatusPanel, 20, nY);
    }

    if (*pEnv->pbPlaneStoresCommitted != 0) {                                /* 0x88384 */
        (void)UiPanel_TrackHoldRow(pEnv->pPlaneSelRowPanel, nCurX, nCurY);
        (void)UiPanel_TrackHoldRow(pEnv->pPlaneCommitRowPanel, nCurX, nCurY);
        (void)UiPanel_TrackHoldRow(pEnv->pPlaneSlotPanel, 20, nY);
        (void)UiPanel_ClickRow(pEnv->pPlaneSlotPanel, 20, nY);
        Ptr_HitRange(pEnv->apPlaneCtl, PUFO_GEO_PLANE_STORE_CTL,
                     PUFO_GEO_PLANE_STORE_CTL + 1, nX, nY, pEnv->pHit);
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
    }

    if (*pEnv->pbPlaneListBuilt != 0) {                                      /* 0x88448 */
        (void)UiPanel_TrackHoldRow(pEnv->pPlaneCrewPanel, nX, nY);
        Ptr_HitRange(pEnv->apPlaneCtl, PUFO_GEO_PLANE_CREW_CTL,
                     PUFO_GEO_PLANE_CREW_CTL + 1, nX, nY, pEnv->pHit);
    }

    if (*pEnv->pbPlaneShowCraftDetail != 0) {                                /* 0x884C4 */
        const Ship *pCraft = pEnv->apCraft[*pEnv->pnSelectedCraftIdx];
        const bool bQuiet = *pEnv->pbStatPanelAOpen == 0 && *pEnv->pbStatPanelBOpen == 0 &&
                            *pEnv->pbPlaneListBuilt == 0 && *pEnv->pbPlaneStoresCommitted == 0;
        if (pCraft != NULL && bQuiet) {
            /* ORIGINAL BUG (residue, 0x88524): the crew-slot gate compares the unsigned byte
             * with `CMP R2, #0 / BLS`, i.e. <= 0, which for a u8 is just == 0. Reproduced as
             * a plain zero test. */
            if (pCraft->bCrewSlots != 0) {                                   /* 0x88524 */
                Ptr_HitRange(pEnv->apPlaneCtl, PUFO_GEO_PLANE_WEAPON_CTL,
                             PUFO_GEO_PLANE_WEAPON_CTL + pCraft->bCrewSlots - 1,
                             nX, nY, pEnv->pHit);
            }
            if (pCraft->bCanDogfight != 0) {                                 /* 0x885D8 */
                Ptr_HitRange(pEnv->apPlaneCtl, PUFO_GEO_PLANE_DOGFIGHT_CTL,
                             PUFO_GEO_PLANE_DOGFIGHT_CTL + 1, nX, nY, pEnv->pHit);
            }
        }
    }

    if (*pEnv->pbStatPanelAOpen != 0 || *pEnv->pbStatPanelBOpen != 0) {      /* 0x88644 */
        (void)Ptr_Hit(pEnv->apPlaneCtl[PUFO_GEO_PLANE_STAT_CTL_A], nX, nY, pEnv->pHit);
        for (i32 i = 0; i < PUFO_GEO_PLANE_STAT_COUNT; ++i) {
            if (*pEnv->apanStatGate[i] > 0) {
                (void)Ptr_Hit(pEnv->apPlaneCtl[PUFO_GEO_PLANE_STAT_CTL + i], nX, nY, pEnv->pHit);
            }
        }
    }

    return Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);
}

/* ------------------------------------------------- screen 7: the build dialog */
static i32 PointerBuildDialog(const GeoscapePointerEnv *pEnv, i32 nX, i32 nY)
{
    *pEnv->pbPopupA = 1;                                                     /* 0x87F44 */
    const i32 nCurX = *pEnv->pnCursorX;
    const i32 nCurY = *pEnv->pnCursorY;

    if (*pEnv->pbFacPlaceArmed == 0) {
        pEnv->pFacPanel->bFlag7073 = 1;                                      /* 0x87F60 */
        Ptr_HitRange(pEnv->apFacCtl, 0, 1, nX, nY, pEnv->pHit);              /* 0x87F88 */
    }

    /* 0x87FE4 / 0x88024: the two hot corners of the dialog hide the placement overlay. */
    if (nCurY > 229 && nCurX > 220 && nCurY < 250) {
        pEnv->pFacPanel->bFlag7073 = 0;
    }
    if (nY > 251 && nX > 0 && nY < 300 && nX < 40) {
        pEnv->pFacPanel->bFlag7073 = 0;
    }

    if (*pEnv->pbFacPlaceArmed == 0) {
        (void)UiPanel_TrackHoldRow(pEnv->pFacPanel, nCurX, nCurY);           /* 0x88048 */
    }
    return Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);
}

/* ------------------------------------------------- the dispatcher */
i32 Geoscape_OnPointer(const GeoscapePointerEnv *pEnv, i32 nX, i32 nY, bool bDown)
{
    if (pEnv == NULL || pEnv->pnCursorX == NULL || pEnv->pnCursorY == NULL) {
        return 0;
    }

    /* 0x872F8 / 0x8731C: the cursor pair is stored as two halfwords. */
    *pEnv->pnCursorY = (i16)nY;
    *pEnv->pnCursorX = (i16)nX;
    *pEnv->pbPlaceFail = 0;                                                  /* 0x87320 */

    i32 nResult = nX;
    i32 nScreen = *pEnv->pnScreenId;

    /* 0x87324: screen 0 with a main-menu message hands the pointer to the menu controls. */
    if (nScreen == 0) {
        const u8 nFlag = UiLineList_GetFlag(pEnv->pLines, 0);
        if (nFlag > PUFO_GEO_MENU_FLAG_MIN && nFlag < PUFO_GEO_MENU_FLAG_MAX) {
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_18704C, nX, nY);
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_187024, nX, nY);
            if (nFlag == PUFO_GEO_MENU_FLAG_DIALOG) {                        /* 0x87384 */
                (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
                return Ptr_HitSlot(pEnv, GEOUI_CTL_186BAC, nX, nY);
            }
            return Ptr_HitSlot(pEnv, GEOUI_CTL_187024, nX, nY);
        }
        nResult = *pEnv->pnCursorX;
        nScreen = *pEnv->pnScreenId;
    }

    switch (nScreen) {
    case 1:
        return PointerBaseStrip(pEnv, nX, nY);

    case 2:
        /* 0x874B4: the globe screen arms new-base placement on every pointer move. */
        if (*pEnv->pbPlacementPending == 0) {
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);
        }
        *pEnv->pbNewBaseMode = 1;                                            /* 0x874E4 */
        if (*pEnv->pbPendingRegionId != 0xFF) {
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
        }
        return PointerWorld(pEnv, nX, nY, bDown);

    case 0:
        return PointerWorld(pEnv, nX, nY, bDown);

    case 3:
        if (*pEnv->pbTransferRowSel != 0) {                                  /* 0x88A08 */
            return PointerTransferRow(pEnv, nX, nY, 0);
        }
        Ptr_HitRange(pEnv->apBaseAtCtl, 0, 1, nX, nY, pEnv->pHit);           /* 0x88A24 */
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);

    case 4:
        Ptr_HitRange(pEnv->apSolPanelCtl, 0, 1, nX, nY, pEnv->pHit);         /* 0x88094 */
        if (*pEnv->pbSolScreenOpen != 0) {                                   /* 0x880D8 */
            (void)UiPanel_TrackHoldRow(pEnv->pSolPanelNames,
                                       *pEnv->pnCursorX, *pEnv->pnCursorY);
        }
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);

    case 5:
        return PointerSoldierNames(pEnv, nX, nY, bDown);

    case 6:
        return PointerCraft(pEnv, nX, nY);

    case 7:
        return PointerBuildDialog(pEnv, nX, nY);

    case 8: {
        if (*pEnv->pbRowSelected == 0) {                                     /* 0x88B18 */
            (void)UiPanel_TrackHoldRow(pEnv->pResearchPanel, nX, nY);
        }
        if (*pEnv->pbRowSelected != 0) {                                     /* 0x88B3C */
            Ptr_HitRange(pEnv->apResearchCtl, 2, 3, nX, nY, pEnv->pHit);
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB0, nX, nY);
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B94, nX, nY);
        } else {
            Ptr_HitRange(pEnv->apResearchCtl, 0, 1, nX, nY, pEnv->pHit);
        }
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);
    }

    case 9: {
        *pEnv->pbPopupB = 1;                                                 /* 0x88C5C */
        (void)UiPanel_TrackHoldRow(pEnv->pManufProductPanel,
                                   *pEnv->pnCursorX, *pEnv->pnCursorY);
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);
        if (*pEnv->pbProductSelected != 0) {                                 /* 0x88D08 */
            Ptr_HitRange(pEnv->apManufCtl, 2, 5, nX, nY, pEnv->pHit);
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB0, nX, nY);
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B94, nX, nY);
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B90, nX, nY);
        } else {
            Ptr_HitRange(pEnv->apManufCtl, 0, 1, nX, nY, pEnv->pHit);
        }
        (void)UiPanel_TrackHoldRow(pEnv->pManufPanelD, 20, nY);              /* 0x88DBC */
        return UiPanel_ClickRow(pEnv->pManufPanelD, 20, nY);
    }

    case 10:
        if (bDown) {
            *pEnv->pnRadarPulseTimer = 0;                                    /* 0x88938 */
        }
        *pEnv->pbPopupB = 1;
        if (*pEnv->pbTransferRowSel != 0) {                                  /* 0x88950 */
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
        } else {
            Ptr_HitRange(pEnv->apSiteInfoCtl, 0, 3, nX, nY, pEnv->pHit);
            (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);
        }
        if (*pEnv->pbTransferRowSel == 0) {                                  /* 0x889F0 */
            (void)UiPanel_TrackHoldRow(pEnv->pBuySellPanelA, 20, nY);
            return UiPanel_ClickRow(pEnv->pBuySellPanelA, 20, nY);
        }
        return *pEnv->pbTransferRowSel;

    case 11:
    case 12:
        if (bDown) {
            *pEnv->pnRadarPulseTimer = 0;                                    /* 0x887E8 */
        }
        *pEnv->pbPopupB = 1;
        Ptr_HitRange(pEnv->apBuySellCtl, 0, 1, nX, nY, pEnv->pHit);          /* 0x88804 */
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
        (void)UiPanel_TrackHoldRow(pEnv->pBuySellPanelA, 20, nY);            /* 0x88DBC */
        return UiPanel_ClickRow(pEnv->pBuySellPanelA, 20, nY);

    case 15:
    case 19:
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);

    case 16: {
        i32 nListX = nResult;
        i32 nListY = nY;
        if (*pEnv->pb16ModeA != 0 && *pEnv->pb16ModeB == 0) {                /* 0x88E0C */
            Ptr_HitRange(pEnv->ap16Ctl, 4, 5, nX, nY, pEnv->pHit);
            nListX = *pEnv->pnCursorX;
            nListY = *pEnv->pnCursorY;
        }
        if (*pEnv->pb16ModeB != 0) {                                         /* 0x88E70 */
            if (*pEnv->pb16ListFull != 0) {
                Ptr_HitRange(pEnv->ap16Ctl, 0, 3, nX, nY, pEnv->pHit);
            } else {
                Ptr_HitRange(pEnv->ap16Ctl, 2, 3, nX, nY, pEnv->pHit);
            }
            nListX = *pEnv->pnCursorX;
            nListY = *pEnv->pnCursorY;
        }
        if (*pEnv->pb16ModeB == 0) {                                         /* 0x88F34 */
            /* ORIGINAL BUG (residue, 0x88F4C): the original sign-extends the cursor x with
             * <<16 / >>16 although nSmapCursorX is already a 16-bit halfword, so the round
             * trip changes nothing. The cast keeps the port honest about that. */
            UiPanel *pList = (*pEnv->pb16ModeA != 0)
                                 ? pEnv->ap16PanelSet[*pEnv->pn16PanelIndex]
                                 : pEnv->p16Panel;
            (void)UiPanel_TrackHoldRow(pList, (i16)nListX, nListY);
        }
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186B00, nX, nY);
    }

    case 17:
        if (*pEnv->pbTransferRowSel != 0) {                                  /* 0x88FB0 */
            return PointerTransferRow(pEnv, nX, nY, 1);
        }
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BAC, nX, nY);
        if (*pEnv->pbPsiPanelOpen == 0) {                                    /* 0x89008 */
            return 0;
        }
        if (*pEnv->pbPsiRowSel == 0) {
            return 0;
        }
        (void)UiPanel_TrackHoldRow(pEnv->apPsiPanel[*pEnv->pnTransferBaseIdx],
                                   *pEnv->pnCursorX, *pEnv->pnCursorY);      /* 0x89038 */
        Ptr_HitRange(pEnv->apPsiCtl, 0, 1, nX, nY, pEnv->pHit);             /* 0x89048 */
        return 2;

    case 18: {
        if (*pEnv->pbOptionsPageActive != 0) {                               /* 0x89110 */
            const i8 nPage = *pEnv->pnOptionsPage;
            bool bTailOnly = false;
            if (nPage == 0) {                                                /* 0x892D0 */
                Ptr_HitRange(pEnv->apOptListCtl, 0, 3, nX, nY, pEnv->pHit);
            } else if (nPage == 1 || nPage == 2) {
                /* ORIGINAL BUG (duplicate, 0x89210 / 0x89270): the page-2 arm is
                 * byte-for-byte the page-1 arm - the same seven aOptionsListCtl slots over
                 * the same bounds. The port keeps one loop for both. */
                Ptr_HitRange(pEnv->apOptListCtl, 0, 6, nX, nY, pEnv->pHit);
            } else if (nPage == 3) {                                         /* 0x891C8 */
                (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
                nResult = Ptr_HitSlot(pEnv, GEOUI_CTL_186BB0, nX, nY);
                bTailOnly = true;
            } else if (nPage == 4) {                                         /* 0x89160 */
                for (i32 i = 0; i < 2 * PUFO_GEO_OPT_SOUND_CTLS; ++i) {
                    UiControl *pCtl = (i < PUFO_GEO_OPT_SOUND_CTLS)
                                          ? pEnv->apOptMusicCtl[i]
                                          : pEnv->apOptSfxCtl[i - PUFO_GEO_OPT_SOUND_CTLS];
                    (void)Ptr_Hit(pCtl, nX, nY, pEnv->pHit);
                }
            } else {
                bTailOnly = true;                                            /* 0x89154 */
            }
            if (!bTailOnly) {
                nResult = Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);      /* 0x89328 */
            }
        }

        if (!bDown || *pEnv->pbOptionsConfirm == 0) {                        /* 0x89334 / 0x89344 */
            return nResult;
        }
        (void)Ptr_HitSlot(pEnv, GEOUI_CTL_186BB4, nX, nY);
        (void)Ptr_Hit(pEnv->pOptCtlPrev, nX, nY, pEnv->pHit);               /* 0x89370: [7] and [8] */
        (void)Ptr_Hit(pEnv->pOptCtlNext, nX, nY, pEnv->pHit);
        return UiPanel_TrackHoldRow(pEnv->pOptPanelShips, nX, nY);          /* 0x893B4 */
    }

    case 20:
        nResult = *pEnv->pbBaseAtDone;                                       /* 0x89414 */
        if (nResult == 0) {
            return nResult;
        }
        return Ptr_HitSlot(pEnv, GEOUI_CTL_186BA4, nX, nY);

    default:
        return nResult;
    }
}

/* ============================================================ the release half
 *
 * Geoscape_OnPointerUp @ 0x89450. It takes only the 0x1864F8 block (R9 = a1): every coordinate
 * it uses is the cursor pair Geoscape_OnPointer stored, and every UiControl_HitTest it makes is
 * a release - x = y = 0, bDown = 0, bForceClick = 0, which is what the ARM sets at every call
 * site (0x89D74..0x89D84, 0x89F74..0x89F80, ...). A release ignores the hit box entirely, so the
 * zero coordinates are not a guess; they are what the original passes.
 *
 * The two halves are not the same function with bDown flipped. The release has no drag block, no
 * strateg-select call and no tap arithmetic; it clears the drag flag instead (0x896BC). It adds
 * Sound_Play(17) to almost every flag arm, it clicks the scroll lists (UiPanel_ClickRow) where
 * the press only tracked the hovered row, and it carries the shortfall gate on byte_1870C8 that
 * the press does not touch. */

static i32 PtrUp_Hit(const GeoscapePointerEnv *pEnv, UiControl *pCtl)
{
    if (pCtl == NULL) {
        return 0;
    }
    return UiControl_HitTest(pCtl, 0, 0, false, false, pEnv->pHit) ? 1 : 0;
}

static i32 PtrUp_HitSlot(const GeoscapePointerEnv *pEnv, GeoscapeUiCtlSlot nSlot)
{
    return PtrUp_Hit(pEnv, pEnv->apCtl[nSlot]);
}

static void PtrUp_HitRange(const GeoscapePointerEnv *pEnv, UiControl *const *apCtl,
                           i32 nFirst, i32 nLast)
{
    for (i32 i = nFirst; i <= nLast; ++i) {
        (void)PtrUp_Hit(pEnv, apCtl[i]);
    }
}

static void PtrUp_Sample(const GeoscapePointerEnv *pEnv)
{
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, pEnv->pUser);
    }
}

/* The shared panel tail at 0x89B64..0x89B90: click the row under the stored cursor, then raise
 * the panel's bFlag7073 (the ARM builds 0x1BA1 out of `MOV R3, #0x1B80 / ORR R3, R3, #0x21`).
 * Two sites jump into it: the world arm's multi-select gate on the status panel (0x18641C) and
 * the options confirm tail (0x8AF1C loads 0x159E18 and branches to 0x89B64). */
static void PtrUp_ClickPanelRow(const GeoscapePointerEnv *pEnv, UiPanel *pPanel)
{
    if (pPanel == NULL) {
        return;
    }
    (void)UiPanel_ClickRow(pPanel, *pEnv->pnCursorX, *pEnv->pnCursorY);
    pPanel->bFlag7073 = 1;
}

/* ------------------------------------------------- screens 0 and 2: LABEL_25 @ 0x896B4
 *
 * The flag chain first, then the radar / multi-select gates, then the fall-through that clears
 * the target-select flag and - on any screen but 2 - releases all thirteen root controls. */
static i32 PointerUpWorld(const GeoscapePointerEnv *pEnv)
{
    *pEnv->pbDragActive = 0;                                                      /* 0x896BC */
    const u8 nFlag = UiLineList_GetFlag(pEnv->pLines, 0);

    if (nFlag == GEO_FLAG_PLACEMENT) {                                            /* 0x896C8 */
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
    }
    if (*pEnv->pbTargetSelectActive != 0) {                                       /* 0x8971C */
        const i32 nHit = PtrUp_HitSlot(pEnv, GEOUI_CTL_186B8C);
        if (nHit != 0) {
            return nHit;
        }
    }
    /* 0x8975C (flag 17) and 0x89950 (flag 15) both land on LABEL_30 @ 0x89760. */
    if (nFlag == GEO_FLAG_BUILD || nFlag == GEO_FLAG_CONFIRM) {
        PtrUp_Sample(pEnv);
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
    }
    /* 0x897A4..0x897D0 (flags 21, 22, 23) and 0x89A9C (flag 12) both land on LABEL_52 @ 0x89AA0. */
    if (nFlag == GEO_FLAG_ALT_A || nFlag == GEO_FLAG_ALT_B || nFlag == GEO_FLAG_ALT_C ||
        nFlag == GEO_FLAG_SELECT_ROW) {
        PtrUp_Sample(pEnv);
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB0);
    }
    /* 0x897E4 / 0x89864 / 0x898DC: the three multi-select flags are the same arm, and each one
     * plays the sample before it tests bMapSelectionFlag - the press arm does not. */
    if (nFlag == GEO_FLAG_MULTI_A || nFlag == GEO_FLAG_MULTI_B || nFlag == GEO_FLAG_MULTI_C) {
        PtrUp_Sample(pEnv);
        if (*pEnv->pbMapSelectionFlag != 0) {
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
            return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB0);
        }
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
    }
    if (nFlag == GEO_FLAG_CANCEL) {                                               /* 0x89994 */
        PtrUp_Sample(pEnv);
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);
    }
    if (nFlag == GEO_FLAG_INTERCEPT) {                                            /* 0x899EC */
        PtrUp_Sample(pEnv);
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA8);
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186B9C);
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186B98);
        if (*pEnv->pbInterceptDone == 0) {
            return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB0);
        }
        return *pEnv->pbInterceptDone;
    }
    if (*pEnv->pbRadarScreenActive != 0) {                                        /* 0x89AF0 */
        PtrUp_Sample(pEnv);
        PtrUp_HitRange(pEnv, pEnv->apRadarCtl, 0, PUFO_RADAR_CONTROLS - 1);
        return PUFO_RADAR_CONTROLS;
    }
    if (*pEnv->pbMultiSelectPanel != 0) {                                         /* 0x89B54 */
        PtrUp_ClickPanelRow(pEnv, pEnv->pStatusPanel);
        return 0;   /* the original hands back the panel pointer itself; see deviation 5 */
    }
    if (nFlag == GEO_FLAG_SITE_LIST || nFlag == GEO_FLAG_SPAWN_MSG) {            /* 0x89BB0 / 0x89C0C */
        PtrUp_Sample(pEnv);
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
    }
    if (nFlag == GEO_FLAG_CLOSE_ONLY || nFlag == GEO_FLAG_SCROLLING) {           /* 0x89C64 / 0x89CA4 */
        PtrUp_Sample(pEnv);
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB0);
    }

    /* 0x89CD4..0x89D20: nothing matched. The target-select flag is dropped, screen 2 returns 2,
     * and every other screen releases all thirteen root controls - with no null check, unlike
     * the strip run in case 1. */
    *pEnv->pbTargetSelectActive = 0;
    if (*pEnv->pnScreenId == 2) {
        return 2;
    }
    PtrUp_HitRange(pEnv, pEnv->apRootCtl, 0, PUFO_GEOUI_ROOT_CTLS - 1);
    return 0;
}

/* ------------------------------------------------- screen 1: the base strip */
static i32 PointerUpBaseStrip(const GeoscapePointerEnv *pEnv)
{
    (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);                                  /* 0x89D88 */

    /* 0x89D9C..0x89DD0: the ten nav slots, and - as in the press - these are the only slots the
     * original null-checks (0x89DA4). A null slot is skipped but the index still advances. */
    for (i32 i = 0; i < PUFO_GEO_STRIP_CTLS; ++i) {
        (void)PtrUp_Hit(pEnv, pEnv->apStripCtl[i]);
    }

    if (*pEnv->pbPlaceActive == 0) {                                              /* 0x89DD4, dword_1870B4[2] */
        return 0;
    }
    return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
}

/* ------------------------------------------------- screen 6: the craft loadout
 *
 * The press arm tracked the hovered row on five panels and clicked only the slot panel; the
 * release clicks four of them and never touches pPlaneCraftStatusPanel or pPlaneSlotPanel. */
static i32 PointerUpCraft(const GeoscapePointerEnv *pEnv)
{
    *pEnv->pbPopupA = 0;                                                          /* 0x8A0CC */
    const i32 nCurX = *pEnv->pnCursorX;
    const i32 nCurY = *pEnv->pnCursorY;

    if (*pEnv->pbPlanePanelsBuilt != 0 && *pEnv->pbPlaneShowCraftDetail == 0) {   /* 0x8A0F0 */
        (void)UiPanel_ClickRow(pEnv->pPlaneCraftPanel, nCurX, nCurY);
    }
    if (*pEnv->pbPlaneListBuilt != 0) {                                           /* 0x8A110 */
        (void)UiPanel_ClickRow(pEnv->pPlaneCrewPanel, nCurX, nCurY);
    }
    if (*pEnv->pbPlaneStoresCommitted != 0) {                                     /* 0x8A130 */
        (void)UiPanel_ClickRow(pEnv->pPlaneSelRowPanel, nCurX, nCurY);
        (void)UiPanel_ClickRow(pEnv->pPlaneCommitRowPanel, nCurX, nCurY);
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);
    }
    /* ORIGINAL BUG (residue, 0x8A188): the clear of bPopupVisibleB is written as a conditional
     * store - `if (bPopupVisibleB != 0) bPopupVisibleB = 0`. Reproduced as the plain clear it is. */
    *pEnv->pbPopupB = 0;

    if (*pEnv->pbPlaneShowCraftDetail != 0) {                                     /* 0x8A198 */
        const Ship *pCraft = pEnv->apCraft[*pEnv->pnSelectedCraftIdx];
        const bool bQuiet = *pEnv->pbStatPanelAOpen == 0 && *pEnv->pbStatPanelBOpen == 0 &&
                            *pEnv->pbPlaneListBuilt == 0 && *pEnv->pbPlaneStoresCommitted == 0;
        if (*pEnv->pbPlaneStoresCommitted != 0) {                                 /* 0x8A1A8, slots 13,14 */
            PtrUp_HitRange(pEnv, pEnv->apPlaneCtl, PUFO_GEO_PLANE_STORE_CTL,
                           PUFO_GEO_PLANE_STORE_CTL + 1);
        }
        if (*pEnv->pbPlaneListBuilt != 0) {                                       /* 0x8A1F0, slots 11,12 */
            PtrUp_HitRange(pEnv, pEnv->apPlaneCtl, PUFO_GEO_PLANE_CREW_CTL,
                           PUFO_GEO_PLANE_CREW_CTL + 1);
        }
        /* 0x8A270..0x8A2D0: the weapon slots, one per Ship.bCrewSlots (+0x14), gated on the four
         * panels being closed. The loop re-reads the crew count every iteration (0x8A2C0). */
        if (pCraft != NULL && bQuiet && pCraft->bCrewSlots != 0) {
            PtrUp_HitRange(pEnv, pEnv->apPlaneCtl, PUFO_GEO_PLANE_WEAPON_CTL,
                           PUFO_GEO_PLANE_WEAPON_CTL + pCraft->bCrewSlots - 1);
        }
        /* 0x8A320: the dogfight pair, with its own copy of the same four gates. */
        if (pCraft != NULL && pCraft->bCanDogfight != 0 && bQuiet) {
            PtrUp_HitRange(pEnv, pEnv->apPlaneCtl, PUFO_GEO_PLANE_DOGFIGHT_CTL,
                           PUFO_GEO_PLANE_DOGFIGHT_CTL + 1);
        }
    }

    if (*pEnv->pbStatPanelAOpen != 0 || *pEnv->pbStatPanelBOpen != 0) {           /* 0x8A370 */
        (void)PtrUp_Hit(pEnv, pEnv->apPlaneCtl[PUFO_GEO_PLANE_STAT_CTL_A]);
        for (i32 i = 0; i < PUFO_GEO_PLANE_STAT_COUNT; ++i) {
            if (*pEnv->apanStatGate[i] > 0) {
                (void)PtrUp_Hit(pEnv, pEnv->apPlaneCtl[PUFO_GEO_PLANE_STAT_CTL + i]);
            }
        }
    }
    return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);
}

/* ------------------------------------------------- screen 7: the build dialog */
static i32 PointerUpBuildDialog(const GeoscapePointerEnv *pEnv)
{
    *pEnv->pbPopupA = 0;                                                          /* 0x89E14 */
    if (*pEnv->pbFacPlaceArmed == 0) {                                            /* 0x89E30 */
        pEnv->pFacPanel->bFlag7073 = 1;
    }
    if (*pEnv->pbFacPlaceArmed == 0) {                                            /* 0x89E40 */
        PtrUp_HitRange(pEnv, pEnv->apFacCtl, 0, 1);
    }
    *pEnv->pbPopupB = 1;                                                          /* 0x89E84 */
    *pEnv->pnPendingPlaceX = *pEnv->pnTapX;                                       /* 0x89E8C */
    *pEnv->pnPendingPlaceY = *pEnv->pnTapY;                                       /* 0x89E98 */

    const i32 nCurX = *pEnv->pnCursorX;
    i32 nCurY = *pEnv->pnCursorY;
    /* 0x89EC8 / 0x89EEC: the two hot corners of the dialog hide the placement overlay. Both use
     * the stored cursor here - the press arm used its arguments for the second corner. */
    if (nCurY > 229 && nCurX > 220 && nCurY < 250) {
        pEnv->pFacPanel->bFlag7073 = 0;
    }
    if (nCurY > 251 && nCurX > 0 && nCurY < 300 && nCurX < 40) {
        pEnv->pFacPanel->bFlag7073 = 0;
    }
    if (*pEnv->pbFacPlaceArmed == 0) {                                            /* 0x89F10 */
        (void)UiPanel_ClickRow(pEnv->pFacPanel, nCurX, nCurY);
    }
    return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);
}

/* ------------------------------------------------- the release dispatcher */
i32 Geoscape_OnPointerUp(const GeoscapePointerEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pnScreenId == NULL || pEnv->pdwShortfall == NULL) {
        return 0;
    }

    i32 nScreen = *pEnv->pnScreenId;                                              /* 0x89460 */

    /* 0x8946c..0x89568: screen 0 with a main-menu message. Same band as the press, but the
     * sample plays first and the flag-37 pair is released BEFORE the two menu controls. */
    if (nScreen == 0) {
        const u8 nFlag = UiLineList_GetFlag(pEnv->pLines, 0);
        if (nFlag > PUFO_GEO_MENU_FLAG_MIN && nFlag < PUFO_GEO_MENU_FLAG_MAX) {
            PtrUp_Sample(pEnv);                                                   /* 0x8949C */
            if (nFlag == PUFO_GEO_MENU_FLAG_DIALOG) {                             /* 0x894C0 */
                (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);
                (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BAC);
            }
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_18704C);
            return PtrUp_HitSlot(pEnv, GEOUI_CTL_187024);
        }
        nScreen = *pEnv->pnScreenId;                                              /* 0x89574 */
    }

    /* 0x89578..0x89600: the low-supply report. The shortfall dword at 0x1870C8 survives a
     * release only on the screens that draw the shortfall panel - 6, 9, 10, 11, 12 - and every
     * other screen clears it here. Screen 0 never reaches this gate. */
    if (*pEnv->pdwShortfall == 1 && nScreen != 0) {
        if (nScreen == 9) {                                                       /* 0x895D4 */
            *pEnv->pdwShortfall = 0;
            if (*pEnv->pbProductSelected == 0) {                                  /* 0x895E0 */
                *pEnv->pbPopupB = 0;
                *pEnv->pbPopupA = 0;
                return 0;
            }
            PtrUp_Sample(pEnv);                                                   /* 0x895F4 */
        } else if (nScreen != 6 && nScreen != 10 && nScreen != 11 && nScreen != 12) {
            *pEnv->pdwShortfall = 0;                                              /* 0x895C4 */
            return 0;   /* the original hands back the address of byte_1870C8; see deviation 5 */
        } else {
            *pEnv->pdwShortfall = 0;                                              /* 0x89600 */
        }
    }

    switch (nScreen) {
    case 2:
        if (*pEnv->pbPlacementPending == 0) {                                     /* 0x8966C */
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);
        }
        if (*pEnv->pbPendingRegionId != 0xFF) {                                   /* 0x89694 */
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);
        }
        return PointerUpWorld(pEnv);

    case 0:
        return PointerUpWorld(pEnv);

    case 1:
        return PointerUpBaseStrip(pEnv);

    case 3:
        if (*pEnv->pbTransferRowSel != 0) {                                       /* 0x8A710 */
            return *pEnv->pbTransferRowSel;
        }
        PtrUp_HitRange(pEnv, pEnv->apBaseAtCtl, 0, 1);                            /* 0x8A744 */
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);

    case 4:
        PtrUp_HitRange(pEnv, pEnv->apSolPanelCtl, 0, 1);                          /* 0x89F68 */
        if (*pEnv->pbSolScreenOpen != 0) {                                        /* 0x89FA4 */
            (void)UiPanel_ClickRow(pEnv->pSolPanelNames, *pEnv->pnCursorX, *pEnv->pnCursorY);
        }
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);

    case 5:
        if (*pEnv->pbNameEditActive != 0) {                                       /* 0x8A000 */
            *pEnv->pbPopupB = 0;
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA8);
            return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B9C);
        }
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);
        if (*pEnv->pbTransferFlag != 0) {                                         /* 0x8A058 */
            return UiPanel_ClickRow(pEnv->pSolPanelEquip, *pEnv->pnCursorX, *pEnv->pnCursorY);
        }
        PtrUp_HitRange(pEnv, pEnv->apSolNameCtl, 0, 2);                           /* 0x8A094 */
        return 0;

    case 6:
        return PointerUpCraft(pEnv);

    case 7:
        return PointerUpBuildDialog(pEnv);

    case 8:
        *pEnv->pbPopupB = 0;                                                      /* 0x8A788 */
        if (*pEnv->pbRowSelected == 0) {                                          /* 0x8A794 */
            (void)UiPanel_ClickRow(pEnv->pResearchPanel, *pEnv->pnCursorX, *pEnv->pnCursorY);
        }
        if (*pEnv->pbRowSelected != 0) {                                          /* 0x8A7C4 */
            PtrUp_HitRange(pEnv, pEnv->apResearchCtl, 2, 3);
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB0);
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186B94);
        } else {
            PtrUp_HitRange(pEnv, pEnv->apResearchCtl, 0, 1);
        }
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);

    case 9:
        (void)UiPanel_ClickRow(pEnv->pManufProductPanel, *pEnv->pnCursorX, *pEnv->pnCursorY);
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);                              /* 0x8A90C */
        if (*pEnv->pbProductSelected != 0) {                                      /* 0x8A920 */
            PtrUp_HitRange(pEnv, pEnv->apManufCtl, 2, 5);
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB0);
            (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186B94);
            return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B90);
        }
        PtrUp_HitRange(pEnv, pEnv->apManufCtl, 0, 1);
        return 0;

    case 10:
        *pEnv->pbPopupB = 0;                                                      /* 0x8A64C */
        (void)UiPanel_ClickRow(pEnv->pBuySellPanelB, *pEnv->pnCursorX, *pEnv->pnCursorY);
        (void)UiPanel_ClickRow(pEnv->pBuySellPanelC, *pEnv->pnCursorX, *pEnv->pnCursorY);
        if (*pEnv->pbTransferRowSel != 0) {                                       /* 0x8A680 */
            return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);
        }
        PtrUp_HitRange(pEnv, pEnv->apSiteInfoCtl, 0, 3);                          /* 0x8A6AC */
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);

    case 11:
    case 12:
        *pEnv->pbPopupB = 0;                                                      /* 0x8A4DC / 0x8A59C */
        (void)UiPanel_ClickRow(pEnv->pBuySellPanelB, *pEnv->pnCursorX, *pEnv->pnCursorY);
        (void)UiPanel_ClickRow(pEnv->pBuySellPanelC, *pEnv->pnCursorX, *pEnv->pnCursorY);
        PtrUp_HitRange(pEnv, pEnv->apBuySellCtl, 0, 1);                           /* 0x8A52C / 0x8A5EC */
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);                              /* LABEL_153 */
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);

    case 15:
    case 19:
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);

    case 16:
        if (*pEnv->pb16ModeA != 0 && *pEnv->pb16ModeB == 0) {                     /* 0x8AA4C */
            PtrUp_HitRange(pEnv, pEnv->ap16Ctl, 4, 5);
        }
        if (*pEnv->pb16ModeB != 0) {                                              /* 0x8AA90 */
            if (*pEnv->pb16ListFull != 0) {
                PtrUp_HitRange(pEnv, pEnv->ap16Ctl, 0, 3);
            } else {
                PtrUp_HitRange(pEnv, pEnv->ap16Ctl, 2, 3);
            }
        }
        if (*pEnv->pb16ModeB == 0) {                                              /* 0x8AB30 */
            UiPanel *pList = (*pEnv->pb16ModeA != 0)
                                 ? pEnv->ap16PanelSet[*pEnv->pn16PanelIndex]
                                 : pEnv->p16Panel;
            (void)UiPanel_ClickRow(pList, *pEnv->pnCursorX, *pEnv->pnCursorY);
        }
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186B00);

    case 17:
        if (*pEnv->pbTransferRowSel != 0) {                                       /* 0x8ABA0 */
            return *pEnv->pbTransferRowSel;
        }
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BAC);
        if (*pEnv->pbPsiPanelOpen != 0 && *pEnv->pbPsiRowSel != 0) {              /* 0x8AC08 */
            (void)UiPanel_ClickRow(pEnv->apPsiPanel[*pEnv->pnTransferBaseIdx],
                                   *pEnv->pnCursorX, *pEnv->pnCursorY);
            PtrUp_HitRange(pEnv, pEnv->apPsiCtl, 0, 1);
        }
        /* 0x8AC6C..0x8AC9C: both base indices are read SIGNED (LDRSB), the base count is read
         * SIGNED (LDRSB at abFacilityGrid + 0x618), and the row bound is the stored cursor y.
         *
         * ORIGINAL BUG (residue, 0x8AC9C): this clear can only fire on a byte that is already 0 -
         * the arm returns at 0x8ABA8 whenever byte_1870F8 is non-zero. It has an effect only if
         * the psi panel's own click handler, tail-called by UiScrollList_ClickRow at 0x89B78, put
         * the selection back up in between. Reproduced as written. */
        if (*pEnv->pbStripHitBase == *pEnv->pnTransferBaseIdx &&
            *pEnv->pnCursorY < 28 * *pEnv->pnBaseCount) {
            *pEnv->pbTransferRowSel = 0;
        }
        *pEnv->pbPsiRowSel = 1;                                                   /* 0x8ACA4 */
        return 1;

    case 18: {
        if (*pEnv->pbOptionsPageActive != 0) {                                    /* 0x8ACE4 */
            const i8 nPage = *pEnv->pnOptionsPage;                                /* LDRSB, 0x8ACEC */
            if (nPage == 0) {                                                     /* 0x8AE64 */
                PtrUp_HitRange(pEnv, pEnv->apOptListCtl, 0, 3);
                (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);                      /* LABEL_228 */
            } else if (nPage == 1 || nPage == 2) {                                /* 0x8AE14 / 0x8ADC4 */
                PtrUp_HitRange(pEnv, pEnv->apOptListCtl, 0, 6);
                (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
            } else if (nPage == 3) {                                              /* 0x8AD7C */
                const i32 nClose = PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
                if (nClose != 0) {
                    return nClose;
                }
                const i32 nCancel = PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB0);
                if (nCancel != 0) {
                    return nCancel;
                }
            } else if (nPage == 4) {                                              /* 0x8AD1C: slots 9..20 */
                PtrUp_HitRange(pEnv, pEnv->apOptMusicCtl, 0, PUFO_GEO_OPT_SOUND_CTLS - 1);
                PtrUp_HitRange(pEnv, pEnv->apOptSfxCtl, 0, PUFO_GEO_OPT_SOUND_CTLS - 1);
                (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);
            }
        }

        /* LABEL_229 @ 0x8AEB0: reached from every page arm, and it overwrites the incoming result
         * with the confirm byte before testing it. */
        if (*pEnv->pbOptionsConfirm == 0) {
            return 0;
        }
        (void)PtrUp_HitSlot(pEnv, GEOUI_CTL_186BB4);                              /* 0x8AED4 */
        (void)PtrUp_Hit(pEnv, pEnv->pOptCtlPrev);                                 /* 0x8AEEC: slots 7,8 */
        (void)PtrUp_Hit(pEnv, pEnv->pOptCtlNext);
        PtrUp_ClickPanelRow(pEnv, pEnv->pOptPanelShips);                          /* 0x8AF1C -> 0x89B64 */
        return 0;   /* the original hands back the panel pointer itself; see deviation 5 */
    }

    case 20:
        if (*pEnv->pbBaseAtDone == 0) {                                           /* 0x8AF70 */
            return 0;
        }
        return PtrUp_HitSlot(pEnv, GEOUI_CTL_186BA4);

    default:
        return 0;   /* screens 13 and 14: the original hands back the address of byte_1870C8 */
    }
}
