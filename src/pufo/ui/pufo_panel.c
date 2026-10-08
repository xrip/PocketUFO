/* pufo_panel.c - the shared UI panel line engine.
 *
 * Evidence: see pufo_panel.h for the full address list.
 */
#include "ui/pufo_panel.h"

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
    pPanel->bUnk3024 = 0;
    pPanel->nFont    = 17;
    pPanel->nUnk3028 = 0;
    pPanel->nUnk3032 = 0;
    pPanel->bFlag7073 = 1;
    pPanel->nStyle    = 1;
    pPanel->nPosX     = 0;
    pPanel->nPosY     = 0;
    pPanel->nUnk7052  = 0;
    pPanel->nUnk7056  = 0;
    pPanel->nViewW    = 240;
    pPanel->nViewH    = 320;
    pPanel->bFlag7072 = 1;
    pPanel->bFlag7074 = 0;
    pPanel->bUnk7089  = 0;
    pPanel->nUnk7076  = 0;
    pPanel->bUnk7080  = 0;
    pPanel->nUnk3016  = 0;
    pPanel->nUnk7084  = 0;
    pPanel->bUnk7088  = 0xFF;
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
    pPanel->gap_0[0] = b0;
    pPanel->gap_0[1] = b1;
    pPanel->gap_0[2] = b2;
}

void UiPanel_SetBytes3to5(UiPanel *pPanel, u8 b3, u8 b4, u8 b5)
{
    pPanel->gap_0[3] = b3;
    pPanel->gap_0[4] = b4;
    pPanel->gap_0[5] = b5;
}

void UiPanel_SetBytes6to11(UiPanel *pPanel, u8 b6, u8 b7, u8 b8, u8 b9, u8 b10, u8 b11)
{
    pPanel->gap_0[6]  = b6;
    pPanel->gap_0[7]  = b7;
    pPanel->gap_0[8]  = b8;
    pPanel->gap_0[9]  = b9;
    pPanel->gap_0[10] = b10;
    pPanel->gap_0[11] = b11;
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
 * Greedy word wrap: words are accumulated while the measured width fits nMaxW,
 * then the accumulated line is committed and a new one starts.
 *
 * Confidence note. The decompiled loop for this function is a tangled index dance
 * (it copies spans into a candidate buffer using `v33[++v7 - v9] = 0` and resets
 * by writing a NUL at a computed position) and the reconstructed control flow did
 * not survive cross-checking: a literal transcription emitted a line containing
 * only the trailing space. Rather than guess at the original's exact index
 * arithmetic, this implements the behaviour that is observable at the call sites:
 * each produced line measures no wider than nMaxW, and words are not split.
 *
 * The one quirk that IS certain from the source structure is the trailing space
 * appended to the working copy. It is documented here rather than relied on, and
 * the port does not reproduce it, so a caller may get one line fewer than the
 * original in the exact-fit case. Recorded as an open deviation. */
i32 UiPanel_WrapText(UiPanel *pPanel, u8 nMaxW, u8 nLineStart, const char *pszText)
{
    char   szLine[PUFO_PANEL_LINE_BYTES];
    size_t nLineLen = 0;
    i32    nCommitted = 0;
    const char *p = pszText;

    szLine[0] = '\0';

    while (*p != '\0') {
        const char *pWord = p;
        size_t      nWord;
        char        szCandidate[PUFO_PANEL_LINE_BYTES];

        /* Skip runs of spaces between words. */
        while (*pWord == ' ') {
            ++pWord;
        }
        if (*pWord == '\0') {
            break;
        }
        nWord = 0;
        while (pWord[nWord] != '\0' && pWord[nWord] != ' ') {
            ++nWord;
        }

        /* Build the candidate line: current line + one space + this word. */
        if (nLineLen == 0) {
            /* First word on the line: no separator. */
            size_t nCopy = (nWord < sizeof(szCandidate) - 1u)
                         ? nWord : sizeof(szCandidate) - 1u;

            memcpy(szCandidate, pWord, nCopy);
            szCandidate[nCopy] = '\0';
        } else {
            size_t nNeed = nLineLen + 1u + nWord;

            if (nNeed >= sizeof(szCandidate)) {
                nNeed = sizeof(szCandidate) - 1u;
            }
            memcpy(szCandidate, szLine, nLineLen);
            szCandidate[nLineLen] = ' ';
            {
                size_t nRoom = nNeed - nLineLen - 1u;
                size_t nCopy = (nWord < nRoom) ? nWord : nRoom;

                memcpy(szCandidate + nLineLen + 1u, pWord, nCopy);
                szCandidate[nLineLen + 1u + nCopy] = '\0';
            }
        }

        if (nLineLen != 0 && Text_MeasureWidth(szCandidate) > (i32)nMaxW) {
            /* The word does not fit: commit the current line and start a new one
             * with this word. */
            if (nLineStart + nCommitted < PUFO_PANEL_LINES) {
                (void)UiPanel_SetLineRaw(pPanel, nLineStart + nCommitted, szLine);
            }
            nCommitted += 1;
            nLineLen = 0;
            szLine[0] = '\0';
            continue; /* retry the same word on the fresh line */
        }

        /* Accept the candidate as the new current line. */
        pufo_strncpy(szLine, szCandidate, sizeof(szLine));
        nLineLen = strlen(szLine);
        p = pWord + nWord;
    }

    /* Commit the final line, if any text was placed. Must use the raw writer:
     * nLineCount is only updated below, so a gated write would be refused whenever
     * an earlier call left nLineCount smaller than this wrap's line count. */
    if (nLineLen != 0) {
        if (nLineStart + nCommitted < PUFO_PANEL_LINES) {
            (void)UiPanel_SetLineRaw(pPanel, nLineStart + nCommitted, szLine);
        }
        nCommitted += 1;
    }

    pPanel->nLineCount = (i32)nLineStart + nCommitted;
    return nCommitted;
}

/* UiPanelMsg_Draw @ 0x3e94c, text rows.
 *
 * Row y, from the decompilation:
 *     y = (row - nUnk3028) * nFont + (nFont - 16) / 2 + nPosY
 * and the x origin is nPosX adjusted by the measured width according to nUnk7076:
 *     1 -> centre (half the width), 2 -> right (full width), 0 -> left.
 *
 * The original also packs two colour triples per row through Gfx_PackRgb565Pair
 * and writes them to a palette LUT; that LUT feeds the blit, which is not ported
 * yet, so the colour step is omitted here and only the text is drawn. */
void UiPanel_DrawText(UiPanel *pPanel)
{
    i32 row;

    for (row = pPanel->nUnk3028; row < pPanel->nLineCount; ++row) {
        const char *psz = UiPanel_GetLineText(pPanel, row);
        i32         nVisible = row - pPanel->nUnk3028;
        i32         nWidth;
        i32         x;
        i32         y;

        if (nVisible >= pPanel->nStyle) {
            break;
        }
        if (psz == NULL || psz[0] == '\0') {
            continue;
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

/* UiScrollList_ClickRow @ 0x3eee6 (entry 0x3eee0). */
i32 UiPanel_ClickRow(UiPanel *pPanel, i32 nX, i32 nY)
{
    i32 nRow;
    i32 nLeft;
    i32 nTop;

    if (pPanel == NULL) {
        return 0;
    }
    s_nClickRowFlag = 0;          /* unk_158018 */
    pPanel->nUnk3016 = 0;
    /* Row = (nY - nPosY) / nFont, exactly the original's two field reads. */
    nRow = (nY - pPanel->nPosY) / pPanel->nFont;
    /* The original stores the row into the dword at panel offset 12 (gap_0[12]). */
    *(i32 *)(void *)(pPanel->gap_0 + 12) = nRow;
    pPanel->nUnk3028 = nRow;
    pPanel->bUnk3024 = 0;

    if (pPanel->bFlag7073 != 0
        && nRow <= pPanel->nStyle - 1
        && nRow <= pPanel->nLineCount - 1) {
        nLeft = pPanel->nUnk7052;
        if (nX > nLeft && nX < pPanel->nViewW + nLeft) {
            nTop = pPanel->nUnk7056;
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
