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

/* Smap_HitTestStrategSelect (0x82be8, 3,088 B) is not ported, so the select test is
 * injected. With no hook installed the port reads "nothing selectable under the
 * pointer", which is the arm's common case (deviation 2). */
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
