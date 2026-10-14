/* pufo_mainmenu.c - see pufo_mainmenu.h for evidence and the layout table. */
#include "game/pufo_mainmenu.h"

#include "game/pufo_font.h"     /* Text_DrawString / Text_MeasureWidth */
#include "render/pufo_render.h" /* SoftSurf_BlitIndexed / SoftSurf_BlitSrcRect */

/* The version stamp, ::psz @ 0xAAD28. */
static const char s_szVersion[] = "V1.26";

/* The row text colour: the cyan pair scaled by key/256. The original runs itod / muld / dtoi per
 * channel; the six constants are the exact binary fractions below, so the products are exact and
 * dtoi is C's truncation. */
static void PackRowColor(const GfxColorEnv *pColor, i32 nKey)
{
    (void)Gfx_PackRgb565Pair(pColor,
                             (i32)((double)nKey * (26.0  / 256.0)),
                             (i32)((double)nKey * (244.0 / 256.0)),
                             (i32)((double)nKey * (252.0 / 256.0)),
                             (i32)((double)nKey * (13.0  / 256.0)),
                             (i32)((double)nKey * (122.0 / 256.0)),
                             (i32)((double)nKey * (126.0 / 256.0)));
}

/* One row frame: body at (x, y, w, 18) with index 0 and colours (87, 94), then the four 1-pixel
 * strips at index 1 with colours (159, 171) — top at y-1, bottom at y+18, left at x-1, right at
 * x+w. The label is centred as (w - Text_MeasureWidth) / 2 + x, one pixel below the body top. The
 * divide is the compiler's signed divide-by-two (RSB / CMP #0 / ADMI #1 / ASR #1 at 0x1DEE8),
 * which truncates toward zero exactly like C's. */
static void DrawMenuRow(const MainMenuEnv *pEnv, i32 nKey, i32 x, i32 y, i32 nWidth,
                        const char *pszLabel)
{
    i32 nTextW;

    SoftSurf_BlitSrcRect(x, y, nWidth, 18, 0, 87, 94, nKey);
    SoftSurf_BlitSrcRect(x, y - 1, nWidth, 1, 1, 159, 171, nKey);
    SoftSurf_BlitSrcRect(x, y + 18, nWidth, 1, 1, 159, 171, nKey);
    SoftSurf_BlitSrcRect(x - 1, y, 1, 18, 1, 159, 171, nKey);
    SoftSurf_BlitSrcRect(x + nWidth, y, 1, 18, 1, 159, 171, nKey);

    PackRowColor(pEnv->pColor, nKey);
    nTextW = Text_MeasureWidth(pszLabel);
    Text_DrawString((nWidth - nTextW) / 2 + x, y + 1, pszLabel);
}

static i32 RowKey(const MainMenuEnv *pEnv, i32 nRow)
{
    if (pEnv->abRowDisabled[nRow] != 0) {
        return 255;
    }
    return pEnv->anRowKeys[nRow];
}

void MainMenu_Draw(const MainMenuEnv *pEnv)
{
    i32 i;

    SoftSurf_BlitIndexed(0, 0, pEnv->pBgGlyph, 20);

    if (pEnv->nListMode == PUFO_MAINMENU_MODE_MAIN ||
        pEnv->nListMode == PUFO_MAINMENU_MODE_SLOTCLICK) {
        const i32 nRecBase = (pEnv->nListMode == PUFO_MAINMENU_MODE_MAIN)
                                 ? PUFO_MAINMENU_REC_MENU0
                                 : PUFO_MAINMENU_REC_MENU1;

        for (i = 0; i < PUFO_MAINMENU_ROW_COUNT; ++i) {
            DrawMenuRow(pEnv, RowKey(pEnv, i), 50, 115 + PUFO_MAINMENU_ROW_STEP * i, 140,
                        (const char *)(pEnv->pTextRecs
                                       + PUFO_TEXT_REC_STRIDE * (nRecBase + i)));
        }
    } else if (pEnv->nListMode == PUFO_MAINMENU_MODE_LOADLIST ||
               pEnv->nListMode == PUFO_MAINMENU_MODE_LOADING) {
        const char *pszCtlLabel;

        for (i = 0; i < PUFO_MAINMENU_SLOT_COUNT; ++i) {
            DrawMenuRow(pEnv, RowKey(pEnv, i), 25, 65 + PUFO_MAINMENU_ROW_STEP * i, 190,
                        pEnv->pSlotLabels + PUFO_MAINMENU_SLOT_LABEL_LEN * i);
        }

        (void)UiControl_BlitActivePad(pEnv->pStartCtl, pEnv->pfnCtlBlit,
                                      pEnv->pfnCtlBlitEx, pEnv->pCtlUser);

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 26, 244, 252, 0, 122, 126);
        pszCtlLabel = (const char *)(pEnv->pTextRecs
                                     + PUFO_TEXT_REC_STRIDE * PUFO_MAINMENU_REC_CTL);
        Text_DrawString(pEnv->pStartCtl->x
                            + ((i32)pEnv->pStartCtl->uWidth - Text_MeasureWidth(pszCtlLabel)) / 2,
                        pEnv->pStartCtl->y + 1, pszCtlLabel);

        if (pEnv->nListMode == PUFO_MAINMENU_MODE_LOADING) {
            UiModalLayout layout;

            *pEnv->pnActionPhaseSave112 = 40;
            (void)UiModal_DrawDialog(pEnv->pModalAnim, pEnv->pTextRecs, 150,
                                     PUFO_MAINMENU_REC_MODAL, 1, NULL, NULL, &layout);
        }
    }

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 255, 255, 255, 90, 90, 90);
    Text_DrawString(5, 300, s_szVersion);
}
