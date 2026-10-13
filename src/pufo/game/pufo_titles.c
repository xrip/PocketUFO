/* pufo_titles.c - see pufo_titles.h for evidence. */
#include "game/pufo_titles.h"

#include "game/pufo_font.h"
#include "util/pufo_rand.h" /* Game_RandU16, the star and particle rolls */

#include <math.h>
#include <stdlib.h>

/* The original's titles surface, unk_158808. */
SoftSurf *g_pTitlesSurface;

/* One credit row, shared by Titles_DrawBackdrop @ 0x1e630 and Titles_DrawCreditLines @ 0x46738. A record
 * that starts with '@' is not text: the rest of it is a decimal index into the glyph table, and that
 * glyph is centred. Everything else is centred text. The two callers differ only in palette. */
static void DrawCreditRow(const TitlesBackdrop *pEnv, i32 nRow, i32 y, i32 nPal);

/* SoftSurf_DrawBorderAndBlur @ 0x9e5c8. */
u8 *SoftSurf_DrawBorderAndBlur(void)
{
    SoftSurf_FillRectEx(g_pTitlesSurface, 0, 0, 240, 1, 0, 0);
    SoftSurf_FillRectEx(g_pTitlesSurface, 0, 319, 240, 1, 0, 0);
    SoftSurf_FillRectEx(g_pTitlesSurface, 0, 0, 1, 320, 0, 0);
    SoftSurf_FillRectEx(g_pTitlesSurface, 239, 0, 1, 320, 0, 0);

    return SoftSurf_BlurPixels(SoftSurf_GetPixels(g_pTitlesSurface),
                               SoftSurf_GetWidth(g_pTitlesSurface),
                               SoftSurf_GetHeight(g_pTitlesSurface));
}

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
        DrawCreditRow(pEnv, i,
                      PUFO_TITLES_ROW_STEP * (i + PUFO_TITLES_ROW_BASE) - pEnv->nScroll,
                      PUFO_TITLES_GLYPH_PAL);
    }

    return pEnv->nMaxRows;
}

static void DrawCreditRow(const TitlesBackdrop *pEnv, i32 nRow, i32 y, i32 nPal)
{
    const u8 *pRec = pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * pEnv->anRowRec[nRow];

    if (pRec[0] == '@') {
        const u8 *pGlyph = pEnv->apGlyphs[atoi((const char *)(pRec + 1))];
        i32 nGlyphW = (i32)((u16)pGlyph[0] | ((u16)pGlyph[1] << 8));

        SoftSurf_BlitIndexedKeyed((PUFO_TITLES_W - nGlyphW) / 2, y, pGlyph, nPal, 0);
    } else {
        i32 nWidth = Text_MeasureWidth((const char *)pRec);

        Text_DrawString((PUFO_TITLES_W - nWidth) / 2, y, (const char *)pRec);
    }
}

/* Titles_PresentToSoftSurf @ 0x466dc. */
void Titles_PresentToSoftSurf(SoftSurf *pTitlesSurface, i32 nPresentPalSlot)
{
    SoftSurf_BlitIndexedEx(g_pMainSurface, 0, 0,
                           (const u8 *)SoftSurf_GetAlloc(pTitlesSurface), nPresentPalSlot);
}

/* Titles_UpdateStars @ 0x46dbc. A star advances by its speed and is reborn once it passes the bottom of
 * the screen. The reborn speed is 1..3 and the amplitude is 1..100, because Game_RandU16(n) returns
 * 0..n-1. */
void Titles_UpdateStars(TitlesAnimation *pEnv)
{
    for (i32 i = 0; i < pEnv->nStarCount; ++i) {
        TitlesStar *pStar = &pEnv->aStars[i];

        pStar->nY += pStar->nSpeed;
        if (pStar->nY > PUFO_TITLES_HEIGHT) {
            pStar->nBaseX     = (i32)Game_RandU16(PUFO_TITLES_W);
            pStar->nY         = 0;
            pStar->nSpeed     = (i32)Game_RandU16(3) + 1;
            pStar->nAmplitude = (i32)Game_RandU16(100) + 1;
        }
    }
}

/* The walk both branches share: step towards the target by (target - current) / 15, with a sign fallback
 * so a gap smaller than 15 still moves exactly one pixel per frame rather than stalling. The division is
 * the signed one (_rt_sdiv at 0x46B30 and 0x46B48), so it truncates toward zero. */
static void ParticleStep(TitlesParticle *p)
{
    i32 nDx = p->nTargetX - p->nX;
    i32 nDy = p->nTargetY - p->nY;
    i32 nSx = nDx / 15;
    i32 nSy = nDy / 15;

    if (nDx != 0 && nSx == 0) {
        nSx = nDx / abs(nDx);
    }
    if (nDy != 0 && nSy == 0) {
        nSy = nDy / abs(nDy);
    }
    p->nX += nSx;
    p->nY += nSy;
}

/* The timer branch. A zero timer sends the particle to the right edge and picks a new target below the
 * middle, which makes the timer negative. A negative timer never counts down again: it only ever moves
 * the target, which is what keeps a particle circling once it has run out of time. */
static void ParticleTimer(TitlesParticle *p)
{
    if (p->nTimer != 0) {
        if (p->nTimer < 0) {
            if (p->nTargetY >= 300) {
                p->nTargetX -= 10;
            } else {
                p->nTargetY += 10;
            }
        } else {
            p->nTimer -= 1;
        }
    } else {
        p->nTargetX = 230;
        p->nTargetY = (i32)Game_RandU16(100) + 200;
        p->nTimer  -= 1;
    }
}

/* Titles_AnimateParticles @ 0x46acc. Every frame first walks the particles, then the frame number picks
 * what happens next: under 20 the timers run, 21 restores the targets from the home pair and re-arms the
 * timer, 20 and 22 through 500 do nothing, 501 through 679 run the timers again, and 680 or more hands
 * over to Titles_LoadScene (0x468cc, not ported). */
i32 Titles_AnimateParticles(TitlesAnimation *pEnv)
{
    i32 nFrame = pEnv->nFrame;

    for (i32 i = 0; i < pEnv->nParticleCount; ++i) {
        ParticleStep(&pEnv->aParticles[i]);
    }

    if (nFrame >= 20) {
        if (nFrame == 21) {
            for (i32 i = 0; i < pEnv->nParticleCount; ++i) {
                TitlesParticle *p = &pEnv->aParticles[i];

                p->nTargetX = p->nHomeX;
                p->nTargetY = p->nHomeY;
                p->nTimer   = (i32)Game_RandU16(30);
            }
            return nFrame;
        }
        if (nFrame > 500 && nFrame < 680) {
            for (i32 i = 0; i < pEnv->nParticleCount; ++i) {
                ParticleTimer(&pEnv->aParticles[i]);
            }
            return nFrame;
        }
        if (nFrame > 680) {
            if (pEnv->pfnLoadScene != NULL) {
                pEnv->pfnLoadScene(pEnv->pSceneUser);
            }
            return nFrame;
        }
        return nFrame;
    }

    for (i32 i = 0; i < pEnv->nParticleCount; ++i) {
        ParticleTimer(&pEnv->aParticles[i]);
    }
    return nFrame;
}

/* Titles_DrawCreditLines @ 0x46738. The window rule is the one Titles_DrawBackdrop uses, and so is the
 * '@' rule; the differences are the palette (31 here) and which scroll variable feeds it. */
i32 Titles_DrawCreditLines(TitlesBackdrop *pEnv)
{
    i32 nFirst;
    i32 nLast;

    if (pEnv->nScroll >= 320) {
        nFirst = (pEnv->nScroll - 340) / PUFO_TITLES_ROW_STEP;
        if (nFirst < 0) {
            nFirst = 0;
        }
        nLast = nFirst + 36;
    } else {
        nFirst = 0;
        nLast  = pEnv->nScroll / PUFO_TITLES_ROW_STEP;
    }

    if (nFirst > pEnv->nMaxRows) {
        return pEnv->nMaxRows;
    }
    if (nLast >= pEnv->nMaxRows) {
        nLast = pEnv->nMaxRows;
    }

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 255, 255, 255, 60, 60, 60);

    for (i32 i = nFirst; i < nLast; ++i) {
        DrawCreditRow(pEnv, i,
                      PUFO_TITLES_ROW_STEP * (i + PUFO_TITLES_ROW_BASE) - pEnv->nScroll, 31);
    }
    return pEnv->nMaxRows;
}

/* Titles_DrawAnimatedFrame @ 0x1a83c.
 *
 * Deviation: the original drives both draw loops through a scratch cursor at pTitles+0x100 and returns
 * whatever Titles_DrawCreditLines leaves in R0. The port uses local counters and returns the frame
 * number. */
i32 Titles_DrawAnimatedFrame(TitlesAnimation *pEnv)
{
    SoftSurf *pSurf = pEnv->pTitlesSurface;

    for (i32 i = 0; i < pEnv->nParticleCount; ++i) {
        const TitlesParticle *p = &pEnv->aParticles[i];

        /* Each particle jitters by 0..4 in both axes every frame. */
        SoftSurf_FillRectEx(pSurf, p->nX + (i32)Game_RandU16(5), p->nY + (i32)Game_RandU16(5),
                            1, 1, pEnv->nFillPalSlot, 255);
    }

    for (i32 i = 0; i < pEnv->nStarCount; ++i) {
        const TitlesStar *pStar = &pEnv->aStars[i];
        double dAngle = (double)pStar->nSpeed * (double)pStar->nY * PUFO_TITLES_STAR_ANGLE_SCALE;
        i32 nX = (i32)((double)pStar->nAmplitude * cos(dAngle) + (double)pStar->nBaseX);

        /* Both the width and the height are 3. ARM loads 3 into R11 for the 4th argument, and the stack
         * slot IDA labels nW holds 3 and is the 5th, which is the height. */
        SoftSurf_FillRectEx(pSurf, nX, pStar->nY, PUFO_TITLES_STAR_COLS, PUFO_TITLES_STAR_COLS,
                            pEnv->nFillPalSlot, 255);
    }

    pEnv->nFrame += 1;

    (void)SoftSurf_DrawBorderAndBlur();
    Titles_PresentToSoftSurf(pSurf, pEnv->nPresentPalSlot);
    (void)Titles_AnimateParticles(pEnv);
    Titles_UpdateStars(pEnv);
    return Titles_DrawCreditLines(&pEnv->Credits);
}
