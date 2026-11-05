/* pufo_modal.c - see pufo_modal.h for evidence. */
#include "ui/pufo_modal.h"
#include "ui/pufo_uigeom.h"

#include <string.h>

void UiModal_Begin(UiModalState *pModal, i32 n12, i32 n13, i32 n14, void *pLabel15,
                   void (*pfn16)(void), void *p17, void (*pfn18)(void))
{
    if (pModal == NULL) {
        return;
    }
    /* The original clears the dword at pCtx+48 and anActionPhaseSave0[112]. */
    pModal->nField48 = 0;
    pModal->nActionPhaseSave112 = 0;
    pModal->bOpen = true;   /* LOBYTE(apUiSlotsA[19]) = 1 */
    pModal->anRect12_14[0] = n12;
    pModal->anRect12_14[1] = n13;
    pModal->anRect12_14[2] = n14;
    pModal->pLabel15 = pLabel15;
    pModal->pfn16 = pfn16;
    pModal->p17 = p17;
    pModal->pfn18 = pfn18;
}

void UiModal_End(UiModalState *pModal)
{
    if (pModal == NULL) {
        return;
    }
    /* Only the open flag is cleared; the stored fields survive. */
    pModal->bOpen = false;
}

/* UiModal_EndThunk @ 0x12254: tail call to UiModal_End @ 0x2648c. See the header for the
 * zero-argument deviation. */
void UiModal_EndThunk(UiModalState *pModal)
{
    UiModal_End(pModal);
}

/* abs32 as the original's ARM uses it: the phase formulas all measure distance from 4. */
static i32 Abs32(i32 n)
{
    return n < 0 ? -n : n;
}

/* One 50x17 modal button: a filled body and four one-pixel edges. The left button and the
 * right button use different key formulas for the same five rectangles, which is the only
 * difference between them. */
static void DrawModalButton(i32 x, i32 y, bool bSelected, bool bRight, i32 nPhaseDist)
{
    i32 nFillKey = bSelected ? 150
                             : (bRight ? 15 * nPhaseDist - 10 : 50 - 15 * nPhaseDist);
    i32 nEdgeKey = bRight ? 25 * (nPhaseDist + 4) : 25 * (8 - nPhaseDist);

    SoftSurf_BlitSrcRect(x, y, 50, 17, 0x9Bu, 255, 0, nFillKey);
    SoftSurf_BlitSrcRect(x, y, 50, 1, 0x9Bu, 255, 0, nEdgeKey);
    SoftSurf_BlitSrcRect(x, y, 1, 17, 0x9Bu, 255, 0, nEdgeKey);
    SoftSurf_BlitSrcRect(x + 50, y, 1, 17, 0x9Bu, 255, 0, nEdgeKey);
    SoftSurf_BlitSrcRect(x, y + 16, 50, 1, 0x9Bu, 255, 0, nEdgeKey);
}

/* UiModal_DrawDialog @ 0x264a0.
 *
 * Height is 13 * nLines + 10, or 13 * nLines + 35 when either button label is present, and
 * both dimensions are centred in the 240x320 screen. */
i32 UiModal_DrawDialog(UiModalAnim *pAnim, const u8 *pTextRecs, i32 nWidth,
                       i32 nRecIndex, i32 nLines, const char *pszBtn1,
                       const char *pszBtn2, UiModalLayout *pLayout)
{
    i32 nHeight;
    i32 nX;
    i32 nY;
    i32 nBtnY;
    i32 nPhaseDist;
    i32 nResult;
    i32 nLine;

    if (pAnim == NULL || pLayout == NULL) {
        return 0;
    }

    nHeight = 13 * nLines + 10;
    if (pszBtn1 != NULL || pszBtn2 != NULL) {
        nHeight = 13 * nLines + 35;
    }
    /* QoL: the box centres on the live face (the 240x320 design centre on the portrait
     * face, the 320x240 centre on the wide face, where the map fills the window behind it). */
    nX = PufoUi_MapX((240 - nWidth) / 2);
    nY = (((g_pMainSurface != NULL) ? (i32)g_pMainSurface->nHeight : PUFO_SURF_H) - nHeight) / 2;

    SoftSurf_BlitSrcRect(nX, nY, nWidth, nHeight, 0xFFu, 255, 255, 128);
    SoftSurf_BlitSrcRect(nX, nY, nWidth, 2, 0, 255, 0, 200);
    SoftSurf_BlitSrcRect(nX, nY, 2, nHeight, 0, 255, 0, 200);
    SoftSurf_BlitSrcRect(nX, nY + nHeight - 2, nWidth, 2, 0, 255, 0, 200);
    /* ORIGINAL BUG (0x265D4), reproduced: the right border is drawn at nX + nWidth, one pixel
     * outside the body the fill just covered, while the left border sits at nX. */
    SoftSurf_BlitSrcRect(nX + nWidth, nY, 2, nHeight, 0, 255, 0, 200);

    /* A 10-pixel vertical bar walks left to right with the typewriter cursor, and a 10-pixel
     * horizontal bar sits at a quarter-height step chosen by unk_1139EF. */
    SoftSurf_BlitSrcRect(nX + ((nWidth - 10) * pAnim->nCursor) / 40, nY, 10, nHeight,
                         0xFFu, 255, 255, 40);
    SoftSurf_BlitSrcRect(nX, nY + (pAnim->nBand % 4) * nHeight / 4, nWidth, 10,
                         0xFFu, 255, 255, 40);

    nBtnY = 13 * nLines + nY + 10;
    nPhaseDist = Abs32(pAnim->nAnimPhase - 4);

    if (pszBtn1 != NULL) {
        if (pszBtn2 != NULL) {
            i32 nStep = (nWidth - 100) / 3;
            i32 nX1 = nX + nStep;
            i32 nX2 = nX + 2 * (nStep + 25);
            i32 nWidth1;

            pLayout->nButtonX[0] = nX1;
            pLayout->nButtonX[1] = nX2;
            pLayout->nButtonY = nBtnY;

            DrawModalButton(nX1, nBtnY, pAnim->nSelButton == 1, false, nPhaseDist);
            Gfx_PackRgb565Pair(pAnim->pColor, 155 - 25 * nPhaseDist, 255 - 25 * nPhaseDist,
                               0, 10, 90, 50);
            nWidth1 = Text_MeasureWidth(pszBtn1);
            Text_DrawString(nX1 + (50 - nWidth1) / 2, nBtnY + 1, pszBtn1);

            DrawModalButton(nX2, nBtnY, pAnim->nSelButton == 2, true, nPhaseDist);
            Gfx_PackRgb565Pair(pAnim->pColor, 25 * nPhaseDist + 55, 25 * nPhaseDist + 155,
                               0, 10, 90, 50);
            Text_DrawString(nX2 + (50 - Text_MeasureWidth(pszBtn2)) / 2, nBtnY + 1, pszBtn2);
            pLayout->nButtonCount = 2;
        } else {
            i32 nX1 = nX + (nWidth - 50) / 2;
            i32 nLabelW;

            pLayout->nButtonX[0] = nX1;
            pLayout->nButtonY = nBtnY;

            DrawModalButton(nX1, nBtnY, pAnim->nSelButton == 1, false, nPhaseDist);
            Gfx_PackRgb565Pair(pAnim->pColor, 155 - 25 * nPhaseDist, 255 - 25 * nPhaseDist,
                               0, 10, 90, 50);
            nLabelW = Text_MeasureWidth(pszBtn1);
            Text_DrawString(nX1 + (50 - nLabelW) / 2, nBtnY + 1, pszBtn1);
            pLayout->nButtonCount = 1;
        }
    } else {
        pLayout->nButtonCount = 0;
    }

    /* The cursor advances BEFORE the text is drawn, so the loop below uses the new value. */
    nResult = pAnim->nCursor;
    if (pAnim->nCursor >= 40) {
        pAnim->nCursor = 1;
        pAnim->bProgressive = 0;
    } else {
        if (pAnim->nCursor == 0) {
            pAnim->bProgressive = 1;
        }
        pAnim->nCursor += 1;
    }

    for (nLine = 0; nLine < nLines; ++nLine) {
        char szLine[PUFO_TEXT_REC_STRIDE];
        const u8 *pRec;
        i32 nLen;
        i32 nCount;
        i32 nDrawX;
        i32 nTextY;
        i32 k;
        i32 i;

        if (pTextRecs == NULL) {
            break;
        }
        pRec = pTextRecs + PUFO_TEXT_REC_STRIDE * (nRecIndex + nLine);

        k = 0;
        for (;;) {
            szLine[k] = (char)pRec[k];
            if (szLine[k] == 0) {
                break;
            }
            ++k;
        }
        nLen = (i32)strlen(szLine);

        /* Whole lines when the progressive flag is clear; otherwise a prefix that shrinks by
         * one for every line already drawn. */
        nCount = (pAnim->bProgressive == 0) ? nLen : pAnim->nCursor - nLine - 1;
        nDrawX = nX + 7;
        nTextY = nY + 3 + 13 * nLine;

        for (i = 0; i < nCount; ++i) {
            char szChar[2];
            i32 nDist;

            if (i >= nLen) {
                continue;
            }
            /* ORIGINAL BUG (0x26E14), reproduced: the character index is i + nLine, so line N
             * drops as many leading characters as its own index even though every line is a
             * different record. With the count formula above the reveal runs diagonally. */
            szChar[0] = szLine[i + nLine];
            szChar[1] = 0;

            nDist = Abs32(i + nLine - pAnim->nCursor + 2);
            if (nDist <= 2) {
                Gfx_PackRgb565Pair(pAnim->pColor, 155 - 25 * nDist, 255 - 25 * nDist, 0,
                                   10, 90, 50);
            } else {
                Gfx_PackRgb565Pair(pAnim->pColor, 0, 200, 0, 10, 90, 50);
            }
            Text_DrawString(nDrawX, nTextY, szChar);
            nDrawX += Text_MeasureWidth(szChar);
        }
        nResult = nLen;
    }

    return nResult;
}
