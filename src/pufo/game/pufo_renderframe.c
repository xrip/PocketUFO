/* pufo_renderframe.c - see pufo_renderframe.h for the dispatch table and evidence. */
#include "game/pufo_renderframe.h"

#include "game/pufo_font.h"     /* g_nGlyphPalSlotBase is set by the default branch */
#include "render/pufo_render.h" /* SoftSurf_Blit, SoftSurf_BlitSrcRect, g_nGlyphPalSlotBase */

/* The modal panel pair differs only in the text record: right 1103, left 1102 (0x2247c / 0x22470). */
#define PUFO_MODAL_PANEL_REC_RIGHT 1103
#define PUFO_MODAL_PANEL_REC_LEFT  1102

i32 Render_Frame(const RenderFrameEnv *pEnv)
{
    i32 nFadeIn;
    if (pEnv->pfnGeoscapeDraw != NULL && pEnv->bGeoscapeMode != 0) {
        /* 0x22328 / 0x22330: the geoscape branch returns Geoscape_Draw's value and never
         * reaches GX_Present -- Geoscape_Draw presents itself. */
        return pEnv->pfnGeoscapeDraw(pEnv->pGeoscapeUser);
    }

    switch (pEnv->nUiScreen) {
    case 0x10: /* 0x225f0 */
        (void)Titles_DrawAnimatedFrame(pEnv->pTitlesAnim);
        break;
    case 0x11: /* 0x225e4 */
        MainMenu_Draw(pEnv->pMainMenu);
        break;
    case 0x12: /* 0x225dc */
        (void)Titles_DrawBackdrop(pEnv->pTitlesBackdrop);
        break;
    default:
        nFadeIn = pEnv->nFadeIn;
        /* 0x22360: the cursor-direction tick runs for every tactical screen. */
        if (pEnv->pCursorDir != NULL) {
            TactHud_TickCursorDir(pEnv->pCursorDir, pEnv->bCursorBusy,
                                  pEnv->bCursorTrackUnit, pEnv->bCursorUnk70115);
            nFadeIn = pEnv->pCursorDir->nSlot10;
        }
        g_nGlyphPalSlotBase = 34; /* 0x22370 */

        switch (pEnv->nUiScreen) {
        case 0: case 3: case 5: case 7: case 8: case 9:
        case 0xA: case 0xB: case 0xC: case 0xF:
            if (pEnv->bDrawIsoLayer != 0) { /* 0x2240c */
                MapView_DrawIsoLayer(pEnv->pIso, pEnv->pIsoLayer); /* 0x22414 */
                switch (pEnv->nUiScreen) {
                case 3: /* 0x224a0 */
                    (void)TactHud_DrawUnitRoster(pEnv->pRosterPads, pEnv->pRoster,
                                                 pEnv->pColor, pEnv->pfnBlit,
                                                 pEnv->pfnBlitEx, pEnv->pBlitUser);
                    break;
                case 5: /* 0x22494 */
                    (void)TactHud_CopyStatusText(pEnv->pStatusPads, pEnv->pszStatus,
                                                 pEnv->pColor, pEnv->pfnBlit,
                                                 pEnv->pfnBlitEx, pEnv->pBlitUser);
                    break;
                case 7: /* 0x22488 */
                    (void)Menu_DrawLabeledPads(pEnv->pLabeledPads, pEnv->pTextRecs,
                                               pEnv->pColor, pEnv->pfnBlit,
                                               pEnv->pfnBlitEx, pEnv->pBlitUser);
                    break;
                case 0xA: /* 0x2247c, TactHud_DrawModalRight */
                    (void)TactHud_DrawModalPanel(pEnv->pModalPanelPads, pEnv->pModalLabelTable,
                                                 pEnv->nModalPanelMode, PUFO_MODAL_PANEL_REC_RIGHT,
                                                 pEnv->pnActionPhaseSave112, pEnv->pModalAnim,
                                                 pEnv->pTextRecs, pEnv->pfnBlit,
                                                 pEnv->pfnBlitEx, pEnv->pBlitUser);
                    break;
                case 0xB: /* 0x22470, TactHud_DrawModalLeft */
                    (void)TactHud_DrawModalPanel(pEnv->pModalPanelPads, pEnv->pModalLabelTable,
                                                 pEnv->nModalPanelMode, PUFO_MODAL_PANEL_REC_LEFT,
                                                 pEnv->pnActionPhaseSave112, pEnv->pModalAnim,
                                                 pEnv->pTextRecs, pEnv->pfnBlit,
                                                 pEnv->pfnBlitEx, pEnv->pBlitUser);
                    break;
                case 0xC: /* 0x22464 */
                    (void)TactHud_DrawTopBar(pEnv->pTopBarPads, pEnv->pTextRecs,
                                             pEnv->pColor, pEnv->pfnBlit,
                                             pEnv->pfnBlitEx, pEnv->pBlitUser);
                    break;
                case 0xF: /* 0x22458 */
                    (void)TactHud_DrawActionButtons(pEnv->pActionPads, pEnv->pActionGate,
                                                    pEnv->pTextRecs, pEnv->pColor,
                                                    pEnv->pfnBlit, pEnv->pfnBlitEx,
                                                    pEnv->pBlitUser);
                    break;
                default:
                    break;
                }
            }
            break;
        case 1: /* 0x224ac */
            (void)TactHud_DrawInventoryScreen(pEnv->pInventory, pEnv->pColor,
                                              pEnv->pfnBlit, pEnv->pfnBlitEx,
                                              pEnv->pBlitUser);
            break;
        case 2: /* 0x224b8 */
            MapView_DrawMinimap(pEnv->pIso, pEnv->pMinimap);
            break;
        case 4: /* 0x224c4 */
            Tact_DrawCommandPads(pEnv->pIso, pEnv->pCmdPads);
            break;
        case 6: /* 0x224d0 */
            (void)TactHud_DrawUnitMarkers(pEnv->pMarkerPad, pEnv->pMarkers,
                                          pEnv->apXcom, pEnv->nXcomCount,
                                          pEnv->apEnemy, pEnv->nEnemyCount,
                                          pEnv->pfnBlit, pEnv->pfnBlitEx, pEnv->pBlitUser);
            break;
        case 0xD: /* 0x223e8 */
            (void)TactHud_DrawStatusBanner(pEnv->pBannerPad, pEnv->pBannerBackdrop,
                                           pEnv->pBannerState, pEnv->pszBannerFooter,
                                           pEnv->pszBannerPadLabel, pEnv->pBannerPanelA,
                                           pEnv->pBannerPanelB, pEnv->pColor,
                                           pEnv->pfnBlit, pEnv->pfnBlitEx, pEnv->pBlitUser);
            break;
        case 0x13: /* 0x223f4 */
            (void)TactHud_DrawConfirmDialog(pEnv->pConfirmPad, pEnv->pConfirmBackdrop,
                                            pEnv->szConfirmPadLabel, pEnv->szConfirmTitle,
                                            pEnv->szConfirmLeft, pEnv->szConfirmRight,
                                            pEnv->pConfirmPanelA, pEnv->pConfirmPanelB,
                                            pEnv->pColor, pEnv->pfnBlit, pEnv->pfnBlitEx,
                                            pEnv->pBlitUser);
            break;
        case 0x14: case 0x15: /* 0x223dc */
            Cydonia_BlitThenDrawPanel(pEnv->pCydoniaPad, pEnv->pCydoniaPanel, pEnv->pColor);
            break;
        default:
            break;
        }

        /* 0x224dc: the modal dialog, straight from the apUiSlotsA cells. */
        if (pEnv->bModalActive != 0) {
            (void)UiModal_DrawDialog(pEnv->pModalAnim, pEnv->pTextRecs, pEnv->nModalWidth,
                                     pEnv->nModalRec, pEnv->nModalLines, pEnv->pszModalBtn1,
                                     pEnv->pszModalBtn2, pEnv->pModalLayout);
        }

        /* 0x2250c: the fade block. */
        if (pEnv->nFadeCount <= 0) {
            if (nFadeIn < 10) {
                if (pEnv->pFadePad != NULL && pEnv->pFadePad->pData != NULL) {
                    if (nFadeIn != 0) {
                        SoftSurf_Blit(0, 0, pEnv->pFadePad->pData, pEnv->pFadePad->nMode,
                                      24 * (10 - nFadeIn)); /* 0x225b8 */
                    } else {
                        SoftSurf_Blit(0, 0, pEnv->pFadePad->pData, pEnv->pFadePad->nMode,
                                      255); /* 0x2257c */
                    }
                }
                if (pEnv->bDrawIsoLayer != 0 && pEnv->pStatusStack != NULL) {
                    (void)StatusMsg_DrawStack(pEnv->pStatusStack, pEnv->pnStatusOutY,
                                              pEnv->pStatusHooks); /* 0x225d4 */
                }
            }
        } else {
            SoftSurf_BlitSrcRect(0, 0, 240, 320, 0, 0, 0, 20); /* 0x22538 */
        }
        break;
    }

    if (pEnv->pfnPresent != NULL) {
        return pEnv->pfnPresent(pEnv->pPresentUser); /* 0x22600: GX_Present -> GX_PresentFrame */
    }
    return 0;
}

/* Render_Frame_DecRef @ 0x36818: decrement the banner-fade counter, then render one frame.
 * See the header for the Map+0x64 offset deviation. */
void Render_Frame_DecRef(i32 *pnFadeCount, const RenderFrameEnv *pEnv)
{
    RenderFrameEnv frame;
    TactHudBannerState banner;

    if (pnFadeCount != NULL) {
        --*pnFadeCount; /* 0x3681C..0x36820: LDR/SUB/STR against Map+0x64 */
    }
    frame = *pEnv;
    if (pnFadeCount != NULL) {
        frame.nFadeCount = *pnFadeCount;
        if (frame.nUiScreen == 0xD && frame.pBannerState != NULL
                && !(frame.pfnGeoscapeDraw != NULL && frame.bGeoscapeMode != 0)) {
            banner = *frame.pBannerState;
            banner.nFadeCount = *pnFadeCount;
            frame.pBannerState = &banner;
        }
    }
    (void)Render_Frame(&frame); /* Both views see the post-decrement Map+0x64 value. */
}
