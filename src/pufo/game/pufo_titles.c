/* pufo_titles.c - see pufo_titles.h for evidence. */
#include "game/pufo_titles.h"

#include "game/pufo_font.h"

#include <stdlib.h>

/* Titles_DrawBackdrop @ 0x1e630. */
i32 Titles_DrawBackdrop(TitlesBackdrop *pEnv)
{
    i32 nFirst;
    i32 nLast;
    i32 nKey;
    i32 i;

    SoftSurf_BlitIndexed(0, 0, pEnv->pBackdrop, PUFO_TITLES_BACKDROP_PAL);

    if (pEnv->nScroll < 50) {
        SoftSurf_BlitRectProxy(0, 0, 0, PUFO_TITLES_W, PUFO_TITLES_HEIGHT,
                               0xFFu, 0xFFu, 0xFFu);
    } else if (pEnv->nScroll < 280) {
        /* Only the upper bound is clamped. Past 135 the value goes negative and the blit is
         * skipped by the `> 0` test, which is how the fade stops rather than reversing. */
        nKey = 3 * (135 - pEnv->nScroll);
        if (nKey > 255) {
            nKey = 255;
        }
        if (nKey > 0) {
            SoftSurf_BlitSrcRect(0, 0, PUFO_TITLES_W, PUFO_TITLES_HEIGHT, 255, 255, 255, nKey);
        }
    }

    if (pEnv->nScroll >= 320) {
        nFirst = (pEnv->nScroll - 340) / PUFO_TITLES_ROW_STEP;
        if (nFirst < 0) {
            nFirst = 0;
        }
        nLast = nFirst + 36;
    } else {
        nFirst = 0;
        nLast = pEnv->nScroll / PUFO_TITLES_ROW_STEP;
    }

    if (nFirst > pEnv->nMaxRows) {
        return pEnv->nMaxRows;
    }
    if (nLast >= pEnv->nMaxRows) {
        nLast = pEnv->nMaxRows;
    }

    Gfx_PackRgb565Pair(pEnv->pColor, 255, 255, 255, 60, 60, 60);

    for (i = nFirst; i < nLast; ++i) {
        const u8 *pRec = pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * pEnv->anRowRec[i];
        i32 y = PUFO_TITLES_ROW_STEP * (i + PUFO_TITLES_ROW_BASE) - pEnv->nScroll;

        if (pRec[0] == '@') {
            const u8 *pGlyph = pEnv->apGlyphs[atoi((const char *)(pRec + 1))];
            i32 nGlyphW = (i32)((u16)pGlyph[0] | ((u16)pGlyph[1] << 8));

            SoftSurf_BlitIndexedKeyed((PUFO_TITLES_W - nGlyphW) / 2, y, pGlyph,
                                      PUFO_TITLES_GLYPH_PAL, 0);
        } else {
            i32 nWidth = Text_MeasureWidth((const char *)pRec);

            Text_DrawString((PUFO_TITLES_W - nWidth) / 2, y, (const char *)pRec);
        }
    }

    return pEnv->nMaxRows;
}
