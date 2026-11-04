/* pufo_panel.c - the shared UI panel line engine.
 *
 * Evidence: see pufo_panel.h for the full address list.
 */
#include "ui/pufo_panel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/pufo_render.h"
#include "util/pufo_str.h"

bool UiPanel_ctor(UiPanel *pPanel, i32 nLineCount)
{
    i32 i;

    memset(pPanel, 0, sizeof(*pPanel));

    /* The original trusts its caller here and would walk past apLines for a
     * count above 500. The clamp is a deviation, listed in the parity notes. */
    if (nLineCount < 0) {
        nLineCount = 0;
    }
    if (nLineCount > PUFO_PANEL_LINES) {
        nLineCount = PUFO_PANEL_LINES;
    }
    pPanel->nLineCount = nLineCount;

    for (i = 0; i < nLineCount; ++i) {
        pPanel->apLines[i] = calloc(1, PUFO_PANEL_LINE_BYTES);
        if (pPanel->apLines[i] == NULL) {
            UiPanel_FreeLines(pPanel);
            pPanel->nLineCount = 0;
            return false;
        }
        /* aLineMeta and anLineAux are already zero from the memset above, which
         * is what the original's explicit loop achieves. */
    }

    /* Defaults, in the original's order. */
    pPanel->bHoverActive = 0;
    pPanel->nFont    = 17;
    pPanel->nUnk3028 = 0;
    pPanel->nUnk3032 = 0;
    pPanel->bSelLineHighlight = 1;
    pPanel->nStyle    = 1;
    pPanel->nPosX     = 0;
    pPanel->nPosY     = 0;
    pPanel->nRectX  = 0;
    pPanel->nRectY  = 0;
    pPanel->nViewW    = 240;
    pPanel->nViewH    = 320;
    pPanel->bRowRules = 1;
    pPanel->bAuxNumbers = 0;
    pPanel->bAuxPlusSign  = 0;
    pPanel->nUnk7076  = 0;
    pPanel->bRepeatArmed  = 0;
    pPanel->nHoldCount  = 0;
    pPanel->nRepeatDelay  = 0;
    pPanel->bColorKey  = 0xFF;
    return true;
}

/* UiPanel_FreeLines @ 0x3e8c8. Note the original iterates nLineCount, so it only
 * frees the lines the ctor allocated. */
void UiPanel_FreeLines(UiPanel *pPanel)
{
    i32 i;

    for (i = 0; i < pPanel->nLineCount && i < PUFO_PANEL_LINES; ++i) {
        free(pPanel->apLines[i]);
        pPanel->apLines[i] = NULL;
    }
}

void UiPanel_SetBytes0to2(UiPanel *pPanel, u8 b0, u8 b1, u8 b2)
{
    pPanel->aStyleRgb[0] = b0;
    pPanel->aStyleRgb[1] = b1;
    pPanel->aStyleRgb[2] = b2;
}

void UiPanel_SetBytes3to5(UiPanel *pPanel, u8 b3, u8 b4, u8 b5)
{
    pPanel->aStyleRgb[3] = b3;
    pPanel->aStyleRgb[4] = b4;
    pPanel->aStyleRgb[5] = b5;
}

void UiPanel_SetBytes6to11(UiPanel *pPanel, u8 b6, u8 b7, u8 b8, u8 b9, u8 b10, u8 b11)
{
    pPanel->aStyleRgb[6]  = b6;
    pPanel->aStyleRgb[7]  = b7;
    pPanel->aStyleRgb[8]  = b8;
    pPanel->aStyleRgb[9]  = b9;
    pPanel->aStyleRgb[10] = b10;
    pPanel->aStyleRgb[11] = b11;
}

/* UiPanel_SetLineMeta @ 0x3f06c.
 *
 * The decompiled form writes slot 1 before slot 0 (v8[17] = a4; v8[16] = a3),
 * which is just instruction scheduling; the net effect is the six bytes written
 * in order, which is what this does. */
void UiPanel_SetLineMeta(UiPanel *pPanel, i32 nLine, u8 b0, u8 b1, u8 b2,
                         u8 b3, u8 b4, u8 b5)
{
    u8 *pMeta;

    if (nLine < 0 || nLine >= PUFO_PANEL_LINES) {
        return;
    }
    pMeta = &pPanel->aLineMeta[PUFO_PANEL_META_BYTES * nLine];
    pMeta[0] = b0;
    pMeta[1] = b1;
    pMeta[2] = b2;
    pMeta[3] = b3;
    pMeta[4] = b4;
    pMeta[5] = b5;
}

bool UiPanel_GetLineMeta(const UiPanel *pPanel, i32 nLine, i32 nWhich,
                         u8 *pR, u8 *pG, u8 *pB)
{
    const u8 *pMeta;

    if (nLine < 0 || nLine >= PUFO_PANEL_LINES) {
        return false;
    }
    if (nWhich != 1 && nWhich != 2) {
        return false;
    }
    pMeta = &pPanel->aLineMeta[PUFO_PANEL_META_BYTES * nLine
                              + (nWhich == 1 ? PUFO_PANEL_META_RGB1
                                             : PUFO_PANEL_META_RGB2)];
    *pR = pMeta[0];
    *pG = pMeta[1];
    *pB = pMeta[2];
    return true;
}

/* Raw writer: gated on CAPACITY, not on nLineCount.
 *
 * UiPanel_SetLineText gates on nLineCount because that is what a caller means by
 * "this line exists". WrapText cannot use it: it rebuilds nLineCount as it goes,
 * so writing line 3 while nLineCount is still 1 would be refused. The buffer is
 * allocated by the ctor, so capacity is the correct bound there. */
static bool UiPanel_SetLineRaw(UiPanel *pPanel, i32 nLine, const char *psz)
{
    if (nLine < 0 || nLine >= PUFO_PANEL_LINES || pPanel->apLines[nLine] == NULL) {
        return false;
    }
    /* The original strcpy's into a 50-byte buffer. Truncating instead is a
     * deviation that turns an overflow into a shorter string. */
    pufo_strncpy((char *)pPanel->apLines[nLine], psz, PUFO_PANEL_LINE_BYTES);
    return true;
}

bool UiPanel_SetLineText(UiPanel *pPanel, i32 nLine, const char *psz)
{
    if (nLine < 0 || nLine >= pPanel->nLineCount) {
        return false;
    }
    return UiPanel_SetLineRaw(pPanel, nLine, psz);
}

const char *UiPanel_GetLineText(const UiPanel *pPanel, i32 nLine)
{
    if (nLine < 0 || nLine >= pPanel->nLineCount || pPanel->apLines[nLine] == NULL) {
        return NULL;
    }
    return (const char *)pPanel->apLines[nLine];
}

void UiPanel_SetLineAux(UiPanel *pPanel, i32 nLine, i32 nAux)
{
    if (nLine < 0 || nLine >= PUFO_PANEL_LINES) {
        return;
    }
    pPanel->anLineAux[nLine] = nAux;
}

i32 UiPanel_GetLineAux(const UiPanel *pPanel, i32 nLine)
{
    if (nLine < 0 || nLine >= PUFO_PANEL_LINES) {
        return 0;
    }
    return pPanel->anLineAux[nLine];
}

/* IncDwordIfInRange_AtPlus0xBD4 @ 0x3f00c:
 *     limit = nLineCount - nStyle;   if (nUnk3028 < limit) nUnk3028 += 1; */
void UiPanel_ScrollNext(UiPanel *pPanel)
{
    i32 nLimit = pPanel->nLineCount - pPanel->nStyle;

    if (pPanel->nUnk3028 < nLimit) {
        pPanel->nUnk3028 += 1;
    }
}

/* DecDwordIfPos_AtPlus0xBD4 @ 0x3f040. */
void UiPanel_ScrollPrev(UiPanel *pPanel)
{
    if (pPanel->nUnk3028 > 0) {
        pPanel->nUnk3028 -= 1;
    }
}

/* UiPanel_WrapText @ 0x37144.
 *
 * The raw loop appends one space to a working copy, then measures at each space.
 * Equality fits (`BLE` at 0x37244). When the next word does not fit, the prior
 * delimiter stays on the line being committed. Use a virtual final space instead
 * of copying the whole input, and keep the existing 50-byte / 500-line bounds. */
i32 UiPanel_WrapText(UiPanel *pPanel, u8 nMaxW, u8 nLineStart, const char *pszText)
{
    char        szLine[PUFO_PANEL_LINE_BYTES];
    const size_t nLineCopyMax = sizeof(szLine) - 1u;
    const size_t nTextLen = strlen(pszText);
    size_t      nTextLineStart = 0;
    size_t      nLastSpace = 0;
    size_t      nScan = 0;
    i32         nCommitted = 0;

    while (nScan <= nTextLen) {
        size_t nCandidateLen;
        size_t nCopy;

        /* nTextLen is the raw loop's appended space; do not write a sentinel
         * into the caller's string. */
        if (nScan < nTextLen && pszText[nScan] != ' ') {
            ++nScan;
            continue;
        }
        if (nLastSpace >= nScan) {
            nLastSpace = nScan;
            ++nScan;
            continue;
        }

        nCandidateLen = nScan - nTextLineStart;
        nCopy = (nCandidateLen < nLineCopyMax) ? nCandidateLen : nLineCopyMax;
        if (nCopy != 0) {
            memcpy(szLine, pszText + nTextLineStart, nCopy);
        }
        szLine[nCopy] = '\0';

        if (Text_MeasureWidth(szLine) <= (i32)nMaxW) {
            nLastSpace = nScan;
            ++nScan;
            continue;
        }

        if (nLastSpace > nTextLineStart) {
            /* The raw overflow arm commits through the previous delimiter. */
            nCandidateLen = nLastSpace + 1u - nTextLineStart;
            nCopy = (nCandidateLen < nLineCopyMax) ? nCandidateLen : nLineCopyMax;
            memcpy(szLine, pszText + nTextLineStart, nCopy);
            szLine[nCopy] = '\0';
            if ((size_t)nLineStart + (size_t)nCommitted < PUFO_PANEL_LINES) {
                (void)UiPanel_SetLineRaw(pPanel, (i32)nLineStart + nCommitted, szLine);
            }
            nCommitted += 1;
            nTextLineStart = nLastSpace + 1u;
            nLastSpace = nTextLineStart;
            nScan = nTextLineStart + 1u; /* the raw scan skips this first byte */
        } else {
            /* Keep the safe port behavior for a first word wider than nMaxW. */
            if (nCandidateLen != 0) {
                if ((size_t)nLineStart + (size_t)nCommitted < PUFO_PANEL_LINES) {
                    (void)UiPanel_SetLineRaw(pPanel, (i32)nLineStart + nCommitted, szLine);
                }
                nCommitted += 1;
            }
            nTextLineStart = nScan + 1u;
            nLastSpace = nTextLineStart;
            nScan = nTextLineStart + 1u;
        }
    }

    /* The virtual delimiter flushes the last candidate, including source spaces. */
    if (nTextLineStart < nTextLen) {
        size_t nLineLen = nTextLen - nTextLineStart;
        size_t nCopy = (nLineLen < nLineCopyMax) ? nLineLen : nLineCopyMax;

        memcpy(szLine, pszText + nTextLineStart, nCopy);
        szLine[nCopy] = '\0';
        if ((size_t)nLineStart + (size_t)nCommitted < PUFO_PANEL_LINES) {
            (void)UiPanel_SetLineRaw(pPanel, (i32)nLineStart + nCommitted, szLine);
        }
        nCommitted += 1;
    }

    pPanel->nLineCount = (i32)nLineStart + nCommitted;
    return nCommitted;
}

/* UiPanelMsg_Draw @ 0x3e94c, the whole function: two head blits, then the row loop.
 *
 * Head blits, both before any text is drawn:
 *   bRowRules (panel+0x1BA0) -- 0x3E9BC SoftSurf_BlitRectProxy(nRectX, nPosY - 1, nViewW, 1,
 *     aStyleRgb[0], aStyleRgb[1], aStyleRgb[2]): the rule above the first row.
 *   bSelLineHighlight (panel+0x1BA1) AND bHoverActive (panel+0xBD0) -- 0x3EA44
 *     SoftSurf_BlitSrcRect(nRectX + 1, nHoverRow * nFont + nPosY, nViewW - 2, nFont,
 *     aStyleRgb[3], aStyleRgb[4], aStyleRgb[5], bColorKey): the hovered row's fill. It is
 *     painted BEFORE the text pass, so a highlighted row keeps its string on top of the fill.
 *
 * Row loop. It walks every row from nUnk3028 to nLineCount and packs
 * Gfx_PackRgb565Pair(aStyleRgb[6..8], aStyleRgb[9..11]) at 0x3EAB8 on EVERY iteration -- rows
 * past the visible window pack too (0x3ED18 only skips their drawing). The pack writes palette
 * slot 2 entries 33/27, so the pair left by the panel's LAST line is the pair whatever text is
 * drawn next anywhere picks up. Inside the drawn-row branch, in this order:
 *   bRowRules -- 0x3EB30 SoftSurf_BlitRectProxy(nRectX, (nVisible + 1) * nFont + nPosY, nViewW,
 *     1, aStyleRgb[0], aStyleRgb[1], aStyleRgb[2]): the rule under this row.
 *   aLineMeta[row][0..2] not all zero -- 0x3EB7C Gfx_PackRgb565Pair(aLineMeta[row][0..2],
 *     aLineMeta[row][3..5]): the row's own colour pair, replacing the style pair for this row.
 *
 * The load widths are not uniform and the port keeps them apart: the two proxy calls take the
 * rule triple through LDRB (unsigned), while the BlitSrcRect colour operands (0x3EA20..0x3EA34)
 * and both pack arguments (0x3EA6C..0x3EA94, 0x3EB58..0x3EB78) are LDRSB, so the original hands
 * those callees sign-extended bytes. The key byte at panel+0x1BB0 is an LDRB.
 *
 * Two ways to pick the string, from the branch at 0x3EBA8:
 *   bAuxNumbers != 0 (0x3EC08..0x3EC90): the line is a number. apLines is never read;
 *     psz = itoa(anLineAux[row]), and when bAuxPlusSign != 0 and aux > 0 the string is the
 *     "+" of asc_AAF90 followed by the itoa digits (0x3EC34..0x3EC6C, then at 0x3EC8C
 *     the plain itoa covers aux <= 0 and panels without bAuxPlusSign).
 *   bAuxNumbers == 0: psz = apLines[row].
 *
 * Row y, from the decompilation:
 *     y = (row - nUnk3028) * nFont + (nFont - 16) / 2 + nPosY
 * and the x origin is nPosX adjusted by the measured width of whichever string was
 * picked, according to nUnk7076: 1 -> centre (half the width), 2 -> right (full
 * width), 0 -> left. */
void UiPanel_DrawText(UiPanel *pPanel, const GfxColorEnv *pColor)
{
    i32 row;

    if (pPanel->bRowRules != 0) {
        SoftSurf_BlitRectProxy(0, pPanel->nRectX, pPanel->nPosY - 1, pPanel->nViewW, 1,
                               pPanel->aStyleRgb[0], pPanel->aStyleRgb[1],
                               pPanel->aStyleRgb[2]);                                 /* 0x3E9BC */
    }
    if (pPanel->bSelLineHighlight != 0 && pPanel->bHoverActive != 0) {
        SoftSurf_BlitSrcRect(pPanel->nRectX + 1,
                             pPanel->nHoverRow * pPanel->nFont + pPanel->nPosY,
                             pPanel->nViewW - 2, pPanel->nFont,
                             (u32)(i8)pPanel->aStyleRgb[3], (i8)pPanel->aStyleRgb[4],
                             (i8)pPanel->aStyleRgb[5], pPanel->bColorKey);            /* 0x3EA44 */
    }

    for (row = pPanel->nUnk3028; row < pPanel->nLineCount; ++row) {
        char        szNum[16];
        const char *psz;
        const u8   *pMeta;
        i32         nVisible = row - pPanel->nUnk3028;
        i32         nWidth;
        i32         x;
        i32         y;

        (void)Gfx_PackRgb565Pair(pColor,
                                 (i8)pPanel->aStyleRgb[6], (i8)pPanel->aStyleRgb[7],
                                 (i8)pPanel->aStyleRgb[8], (i8)pPanel->aStyleRgb[9],
                                 (i8)pPanel->aStyleRgb[10], (i8)pPanel->aStyleRgb[11]); /* 0x3EAB8 */
        if (nVisible >= pPanel->nStyle) {
            continue; /* 0x3ED18: the row is packed, not drawn */
        }
        if (pPanel->bRowRules != 0) {
            SoftSurf_BlitRectProxy(0, pPanel->nRectX,
                                   (nVisible + 1) * pPanel->nFont + pPanel->nPosY, pPanel->nViewW, 1,
                                   pPanel->aStyleRgb[0], pPanel->aStyleRgb[1],
                                   pPanel->aStyleRgb[2]);                             /* 0x3EB30 */
        }
        pMeta = &pPanel->aLineMeta[PUFO_PANEL_META_BYTES * row]; /* aLineMeta @ 0x10, stride 6 */
        if (pMeta[PUFO_PANEL_META_RGB1] != 0 || pMeta[1] != 0 || pMeta[2] != 0) {
            (void)Gfx_PackRgb565Pair(pColor, (i8)pMeta[0], (i8)pMeta[1], (i8)pMeta[2],
                                     (i8)pMeta[3], (i8)pMeta[4], (i8)pMeta[5]);       /* 0x3EB7C */
        }
        if (pPanel->bAuxNumbers != 0) {
            const i32 nAux = pPanel->anLineAux[row]; /* anLineAux @ 0x13B4, 0x3EC30 / 0x3EC8C */

            if (pPanel->bAuxPlusSign != 0 && nAux > 0) {
                snprintf(szNum, sizeof(szNum), "+%d", nAux);
            } else {
                snprintf(szNum, sizeof(szNum), "%d", nAux);
            }
            psz = szNum;
        } else {
            psz = UiPanel_GetLineText(pPanel, row);
            if (psz == NULL || psz[0] == '\0') {
                continue;
            }
        }
        nWidth = Text_MeasureWidth(psz);
        if (pPanel->nUnk7076 == 1) {
            x = pPanel->nPosX - nWidth / 2;
        } else if (pPanel->nUnk7076 == 2) {
            x = pPanel->nPosX - nWidth;
        } else {
            x = pPanel->nPosX;
        }
        y = nVisible * pPanel->nFont + (pPanel->nFont - 16) / 2 + pPanel->nPosY;

        Text_DrawString(x, y, psz);
    }
}

/* The original's global flag unk_158018, cleared on every click. */
static i32 s_nClickRowFlag;

/* UiScrollList_ClickRow @ 0x3eee0. */
i32 UiPanel_ClickRow(UiPanel *pPanel, i32 nX, i32 nY)
{
    i32 nRow;
    i32 nLeft;
    i32 nTop;

    if (pPanel == NULL) {
        return 0;
    }
    s_nClickRowFlag = 0;          /* unk_158018 */
    pPanel->nHoldCount = 0;
    /* Row = (nY - nPosY) / nFont, exactly the original's two field reads. */
    nRow = (nY - pPanel->nPosY) / pPanel->nFont;
    /* The original stores the row into nHoverRow, the dword at panel offset 12.
     * It never writes nUnk3028 here; ARM 0x3EEE0..0x3F000 writes only unk_158018,
     * 0xBC8, 0xC and 0xBD0, and reads 0xBD4 as the left side of the sum below. */
    pPanel->nHoverRow = nRow;
    pPanel->bHoverActive = 0;

    if (pPanel->bSelLineHighlight != 0
        && nRow <= pPanel->nStyle - 1
        && nRow <= pPanel->nLineCount - 1) {
        nLeft = pPanel->nRectX;
        if (nX > nLeft && nX < pPanel->nViewW + nLeft) {
            nTop = pPanel->nRectY;
            if (nY > nTop && nY < pPanel->nViewH + nTop) {
                pPanel->nUnk3032 = pPanel->nUnk3028 + nRow;
                if (pPanel->pfnClick != NULL) {
                    pPanel->pfnClick();
                }
                return nRow;
            }
        }
    }
    return nRow;
}

/* UiScrollList_TrackHold @ 0x3ed44. */
i32 UiPanel_TrackHoldRow(UiPanel *pPanel, i32 nX, i32 nY)
{
    i32 nRow;
    i32 nLeft;
    i32 nTop;
    i32 nThreshold;

    if (pPanel == NULL) {
        return 0;
    }
    nRow = (nY - pPanel->nPosY) / pPanel->nFont;
    pPanel->nHoverRow = nRow;

    if (pPanel->bSelLineHighlight == 0) {
        return nRow;
    }
    nLeft = pPanel->nRectX;
    nTop = pPanel->nRectY;
    if (nRow > pPanel->nStyle - 1
        || nRow > pPanel->nLineCount - 1
        || nX <= nLeft
        || nX >= pPanel->nViewW + nLeft
        || nY <= nTop
        || nY >= pPanel->nViewH + nTop) {
        pPanel->bHoverActive = 0;
        pPanel->nHoldCount = 0;
        return nRow;
    }

    pPanel->bHoverActive = 1;
    if (pPanel->bRepeatArmed == 0) {
        return nRow;
    }
    /* 0x3EE50: a different hovered row restarts the repeat counter. */
    if (s_nClickRowFlag != nRow) {
        pPanel->nHoldCount = 0;
    }
    pPanel->nHoldCount += 1;
    s_nClickRowFlag = nRow;

    nThreshold = pPanel->nRepeatDelay;
    if (pPanel->nHoldCount >= nThreshold) {
        /* 0x3EE84..0x3EEB8: re-reads nHoverRow rather than reusing the register. */
        i32 nStored = pPanel->nHoverRow;
        pPanel->bRepeatArmed = 0;
        pPanel->nHoldCount = nThreshold;
        pPanel->nUnk3032 = pPanel->nUnk3028 + nStored;
        if (pPanel->pfnClick != NULL) {
            pPanel->pfnClick();
        }
        /* The original returns the click callback's value; pfnClick is modelled as
         * void here, so the row is returned instead. Same deviation as ClickRow. */
        return nRow;
    }
    return nRow;
}

/* Ui_SwapStringSlots @ 0x6aaac. The original copies through a 200-byte stack
 * buffer; the port keeps the same bound but uses a bounded copy. */
#define PUFO_PANEL_SWAP_TEMP 200

bool UiPanel_SwapLines(UiPanel *pPanel, i32 nIdxA, i32 nIdxB)
{
    char szTemp[PUFO_PANEL_SWAP_TEMP];
    u8 *pszA;
    u8 *pszB;
    i32 nAuxA;
    i32 nAuxB;

    if (pPanel == NULL || nIdxA < 0 || nIdxA >= PUFO_PANEL_LINES
        || nIdxB < 0 || nIdxB >= PUFO_PANEL_LINES) {
        return false;
    }
    pszA = pPanel->apLines[nIdxA];
    pszB = pPanel->apLines[nIdxB];
    if (pszA == NULL || pszB == NULL) {
        return false;
    }
    pufo_strncpy(szTemp, (const char *)pszA, sizeof(szTemp));
    nAuxA = pPanel->anLineAux[nIdxA];
    nAuxB = pPanel->anLineAux[nIdxB];

    pufo_strcpy((char *)pszA, (const char *)pszB);
    pPanel->anLineAux[nIdxA] = nAuxB;
    pufo_strcpy((char *)pszB, szTemp);
    pPanel->anLineAux[nIdxB] = nAuxA;
    return true;
}

/* Cydonia_Blit_Then_UiPanelMsg_Draw @ 0x1a7a0. */
void Cydonia_BlitThenDrawPanel(PadImage *pPad, UiPanel *pPanel, const GfxColorEnv *pColor)
{
    PadImage_Blit(pPad);
    UiPanel_DrawText(pPanel, pColor);
}

/* --- Ui_AnimExpandBlit @ 0x95bf0 --- */

bool Ui_AnimExpandBlit(const UiAnimBlitEnv *pEnv, i32 x, i32 y, i32 nW, i32 nH,
                       u8 nIdx, u8 nSrcX, u8 nSrcY, u8 nKey, i8 nMode)
{
    i32 nProg;
    i32 nX;
    i32 nY;
    i32 nW2;
    i32 nH2;
    u8 nOuterIdx;
    u8 nOuterMid;
    u8 nOuterLow;
    u8 nInnerIdx;
    u8 nInnerMid;
    u8 nInnerLow;

    if (pEnv == NULL || pEnv->pbStarted == NULL || pEnv->pnProgress == NULL) {
        return false;
    }

    if (*pEnv->pbStarted != 0) {                                   /* 0x95C24 */
        nProg = *pEnv->pnProgress;
    } else {                                                       /* 0x95C14..0x95C20 */
        *pEnv->pbStarted = 1;
        *pEnv->pnProgress = 0;
        nProg = 0;
    }

    switch (nMode) {
    case 1:                                                        /* 0x95CEC */
        nX = x;
        nY = y;
        nW2 = 50 * nProg;
        nH2 = nW2;
        break;
    case 2:                                                        /* 0x95CB0 */
        nX = x;
        nY = nH / 2 - 25 * nProg;
        nW2 = nW;
        nH2 = 50 * nProg;
        break;
    case 3:                                                        /* 0x95C9C */
        nX = x;
        nY = y;
        nW2 = 50 * nProg;
        nH2 = nH;
        break;
    case 4:                                                        /* 0x95C50 */
        nX = nW / 2 - 25 * nProg;
        nY = nH / 2 - 25 * nProg;
        nW2 = 50 * nProg;
        nH2 = nW2;
        break;
    default:                                                       /* 0x95D08 (mode 0) */
        nX = x;
        nY = y;
        nW2 = nW;
        nH2 = nH;
        break;
    }

    if (nW2 > nW) {                                                /* 0x95D28 */
        nW2 = nW;
    }
    if (nH2 > nH) {                                                /* 0x95D3C */
        nH2 = nH;
    }
    if (nX < x) {                                                  /* 0x95D44 */
        nX = x;
    }
    if (nY < y) {                                                  /* 0x95D4C */
        nY = y;
    }
    if (nW2 == nW && nH2 == nH && nX == x && nY == y) {            /* 0x95D60..0x95D74 */
        *pEnv->pnProgress = 100;
    }

    SoftSurf_BlitSrcRect(nX, nY, nW2, nH2, nIdx, (i32)nSrcX, (i32)nSrcY, nKey); /* 0x95DA8 */

    if (pEnv->nFrameStyle == 1) {                                  /* 0x95DE4 */
        nOuterIdx = 40;
        nOuterMid = 120;
        nOuterLow = (u8)(-56);                                     /* 200 */
        nInnerIdx = 60;
        nInnerMid = (u8)(-106);                                    /* 150 */
        nInnerLow = (u8)(-36);                                     /* 220 */
    } else {                                                       /* 0x95DC8 */
        nOuterIdx = 10;
        nOuterMid = 80;
        nOuterLow = 90;
        nInnerIdx = 20;
        nInnerMid = 90;
        nInnerLow = 100;
    }

    SoftSurf_BlitRectProxy(0, nX, nY, nW2 + 4, 6, nOuterIdx, nOuterMid, nOuterLow);        /* 0x95E18 */
    SoftSurf_BlitRectProxy(0, nX + 2, nY + 2, nW2, 2, nInnerIdx, nInnerMid, nInnerLow);    /* 0x95E6C */
    SoftSurf_BlitRectProxy(0, nX, nY + nH2, nW2 + 4, 6, nOuterIdx, nOuterMid, nOuterLow);  /* 0x95EA0 */
    SoftSurf_BlitRectProxy(0, nX + 2, nY + nH2 + 2, nW2, 2, nInnerIdx, nInnerMid,          /* 0x95ECC */
                           nInnerLow);
    SoftSurf_BlitRectProxy(0, nX, nY + 4, 6, nH2 - 2, nOuterIdx, nOuterMid, nOuterLow);    /* 0x95F00 */
    SoftSurf_BlitRectProxy(0, nX + 2, nY + 2, 2, nH2, nInnerIdx, nInnerMid, nInnerLow);    /* 0x95F28 */
    SoftSurf_BlitRectProxy(0, nX + nW2 - 2, nY + 4, 6, nH2 - 2, nOuterIdx, nOuterMid,      /* 0x95F58 */
                           nOuterLow);
    SoftSurf_BlitRectProxy(0, nX + nW2, nY + 2, 2, nH2, nInnerIdx, nInnerMid, nInnerLow);  /* 0x95F80 */

    return *pEnv->pnProgress == 100;                               /* 0x95F9C */
}

/* --- Ui_AnimExpandBlit_IfEnabled @ 0x663c4 --- */

i32 Ui_AnimExpandBlit_IfEnabled(const UiAnimBlitEnv *pEnv, i32 x, i32 y, i32 nW, i32 nH,
                                i8 bEnabled)
{
    if (bEnabled != 1) {
        return bEnabled;
    }
    return Ui_AnimExpandBlit(pEnv, x, y, nW, nH, 4, 0x46, 0x4B, 0xC8, 0) ? 1 : 0;
}
