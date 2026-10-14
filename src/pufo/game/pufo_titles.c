/* pufo_titles.c - see pufo_titles.h for evidence. */
#include "game/pufo_titles.h"

#include "game/pufo_font.h"
#include "game/pufo_timer.h" /* Timer_StartA, the StartTimers_A the ending re-arms */
#include "asset/pufo_asset.h" /* AssetReader, the titleN.dat lookup */
#include "audio/pufo_audio.h" /* Sound_InitAndLoadSamples / LoadAndPlayMusic, the ending audio */
#include "util/pufo_rand.h" /* Game_RandU16, the star and particle rolls */

#include <math.h>
#include <stdio.h>
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
            return Titles_LoadScene(pEnv);
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

/* Titles_LoadScene @ 0x468cc. */
i32 Titles_LoadScene(TitlesAnimation *pEnv)
{
    AssetReader rdr;
    char szName[32];
    u8 *pData;
    u32 cb;
    i32 nPairs;

    AssetReader_Clear(&rdr);
    pEnv->nParticleCount = 0;

    snprintf(szName, sizeof(szName), "title%d.dat", pEnv->nSceneIndex);
    (void)AssetReader_Find(&rdr, szName);

    cb = AssetReader_Size(&rdr);
    pData = (u8 *)malloc(cb);
    if (pData != NULL) {
        (void)AssetReader_Read(&rdr, pData, cb);
    }

    /* The original computes (size - 2) / 2 on an unsigned size, so a member smaller than 2 bytes wraps
     * to a huge count and is only saved by the signed test that follows. The port states the guard. */
    nPairs = (pData != NULL && cb >= 2u) ? (i32)((cb - 2u) / 2u) : 0;

    if (nPairs > 0) {
        pEnv->nSceneScaleX = (i32)pData[0];
        pEnv->nSceneScaleY = (i32)pData[1];

        for (i32 i = 0; i < nPairs; ++i) {
            TitlesParticle *p = &pEnv->aParticles[pEnv->nParticleCount];
            i32 nHomeA = (i32)pData[2 + 2 * i];
            i32 nHomeB = (i32)pData[3 + 2 * i];

            p->nX       = (i32)Game_RandU16(1000) - 500;
            p->nY       = (i32)Game_RandU16(350) - 350;
            p->nTargetX = 120;
            p->nTargetY = 160;
            p->nHomeX   = 3 * nHomeA + 3 * (80 - pEnv->nSceneScaleX) / 2;
            p->nHomeY   = 3 * nHomeB + (320 - 3 * pEnv->nSceneScaleY) / 2;

            pEnv->nParticleCount += 1;
        }
    }

    free(pData);

    pEnv->nFrame = 0;
    pEnv->nSceneIndex += 1;
    if (pEnv->nSceneIndex >= 2) {
        pEnv->nSceneIndex = 0;
    }
    return pEnv->nSceneIndex;
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
    /* The original hands the state block itself to Titles_DrawCreditLines, whose scroll is the
     * dword at state +87228. The port's Credits env carries that scroll as a value, so copy it
     * here to reproduce the aliasing. */
    pEnv->Credits.nScroll = pEnv->nScroll;
    return Titles_DrawCreditLines(&pEnv->Credits);
}

/* Titles_ctor @ 0x46e88. Scene index zero, the titles surface (240x320, mode 0 = one byte per
 * pixel, which is why the whole surface is 0x12C00 bytes), 190 stars seeded above the top edge,
 * then one star step. */
void Titles_ctor(TitlesAnimation *pEnv)
{
    pEnv->nSceneIndex = 0; /* 0x46E98: state +16 */

    pEnv->pTitlesSurface = (SoftSurf *)malloc(sizeof(SoftSurf)); /* 0x46E9C: new(0x28) */
    if (pEnv->pTitlesSurface != NULL) {
        SoftSurf_ctor(pEnv->pTitlesSurface); /* 0x46EA8 */
        (void)SoftSurf_AllocPixels(pEnv->pTitlesSurface, 240, 320, 0); /* 0x46EC8 */
    }
    g_pTitlesSurface = pEnv->pTitlesSurface; /* 0x46EC4/0x46EB0: the global, NULL on failure */

    pEnv->nStarCount = 190; /* 0x46EF8: state +87220 */
    for (i32 i = 0; i < pEnv->nStarCount; ++i) { /* 0x46F1C..0x46F8C */
        TitlesStar *p = &pEnv->aStars[i];
        p->nBaseX     = (i32)Game_RandU16(0xF0u);          /* 0x46F1C */
        p->nY         = (i32)Game_RandU16(0x140u) - 320;   /* 0x46F3C: -320..-1, above the top edge */
        p->nSpeed     = (i32)Game_RandU16(3u) + 1;         /* 0x46F5C: 1..4 */
        p->nAmplitude = (i32)Game_RandU16(0x64u) + 1;      /* 0x46F78: 1..101 */
    }
    Titles_UpdateStars(pEnv); /* 0x46F94 */
}

/* The opening credits' row table, verbatim from the image blob unk_A63A8 (44 dwords). Record 0x5A0
 * recurs as a spacer row between the three blocks. */
const i32 g_aTitlesRowRecs[44] = {
    0x587, 0x588, 0x589, 0x58A, 0x58B, 0x58C, 0x58D, 0x58E, 0x58F, 0x590,
    0x591, 0x592, 0x593, 0x594, 0x595, 0x596, 0x597, 0x598, 0x599, 0x59A,
    0x59B, 0x59C, 0x655, 0x656, 0x5A0, 0x64A, 0x64B, 0x64C, 0x64D, 0x64E,
    0x64F, 0x650, 0x651, 0x652, 0x5A0, 0x653, 0x654, 0x5A0, 0x59D, 0x59E,
    0x59F, 0x5A0, 0x5A1, 0x5A2
};

/* Titles_Init @ 0x380d8: the opening credits screen, in the original's order. */
void Titles_Init(TitlesInitEnv *pEnv)
{
    AssetReader rdr;
    char szName[16];
    u32  cb;

    AssetReader_Clear(&rdr); /* 0x380E4 */
    Titles_ctor(pEnv->pAnim); /* 0x380F0..0x38118: new(0x154C0) + Titles_ctor; the block is caller-owned */
    *pEnv->pnMaxRows = 44; /* 0x38128 */
    memcpy(pEnv->anRowRec, g_aTitlesRowRecs, 44u * sizeof(i32)); /* 0x38134 */

    PufoFont_Init(pEnv->pFont); /* 0x38140 */
    pEnv->pFont->nGlyphBase = pEnv->pFont->nTotalSprFrames; /* 0x3814C: before "font" is loaded */
    (void)Font_LoadSpr(pEnv->pFont, "font"); /* 0x38154 */
    Font_BuildIndex(pEnv->pFont); /* 0x3815C */

    Palette_Load("fade.pal", 12u); /* 0x38168 */
    *pEnv->puUiScreen = 16; /* 0x38174 */
    /* 0x3817C: unk_156930 = 0. No reader anywhere in the binary (the only other write is
     * Map_ComputeReachable @ 0x2b820); the port does not model it. */

    (void)Titles_LoadScene(pEnv->pAnim); /* 0x38184 */
    Timer_StartC(); /* 0x38188 */
    pEnv->pAnim->nScroll = 0; /* 0x38198: state +87228, the credits scroll */
    Palette_Load("titles.pal", 31u); /* 0x381A4 */

    for (i32 i = 0; i < 5; ++i) { /* 0x381B8..0x381F8: the original steps 20 bytes between names */
        snprintf(szName, sizeof(szName), "titles%d.pad", i);
        (void)AssetReader_Find(&rdr, szName); /* 0x381BC */
        cb = AssetReader_Size(&rdr); /* 0x381C4 */
        pEnv->apGlyphs[i] = (u8 *)malloc(cb); /* 0x381CC: operator new, unchecked in the original */
        cb = AssetReader_Size(&rdr); /* 0x381D4: the size is re-read, not kept */
        if (pEnv->apGlyphs[i] != NULL) { /* deviation: the original reads whatever new returned */
            (void)AssetReader_Read(&rdr, pEnv->apGlyphs[i], cb); /* 0x381E4 */
        }
    }

    (void)Sound_InitAndLoadSamples("tact"); /* 0x38200 */
    Sound_LoadAndPlayMusic(3); /* 0x38208 */
    Sound_ApplyMusicVolume(Options_MusicLevel(pEnv->pOptions), pEnv->pOptions->bMusicOff); /* 0x3820C */
    Sound_ApplySfxVolume(Options_SfxLevel(pEnv->pOptions), pEnv->pOptions->bSoundOff); /* 0x38210 */

    if (g_pTitlesSurface != NULL) { /* deviation: the original dereferences GetPixels' result */
        (void)memset(SoftSurf_GetPixels(g_pTitlesSurface), 0, 0x12C00u); /* 0x38220/0x38234 */
    }
}

/* The two ending row-record tables, verbatim from the image blobs unk_A6458 (40 bytes) and
 * unk_A6480 (64 bytes): text-record indices, ten for the good end, sixteen for the bad end. */
const i32 g_aGoodEndRowRecs[10] = {
    0x56B, 0x56C, 0x56D, 0x56E, 0x56F, 0x570, 0x571, 0x572, 0x573, 0x574
};
const i32 g_aBadEndRowRecs[16] = {
    0x575, 0x576, 0x577, 0x578, 0x579, 0x57A, 0x57B, 0x57C,
    0x57D, 0x57E, 0x57F, 0x580, 0x581, 0x582, 0x583, 0x584
};

/* EndScreen_Init @ 0x388b0. Set up the credits screen for an ending, in the original's order:
 * clear the reader, re-init the font, pick the four ending-dependent values, copy the row table
 * (40 or 64 bytes - the good end leaves slots 10..15 at whatever a previous run left), record the
 * glyph base as the frame count before the "font" group is loaded, load and index the font, load
 * the "z" samples, load the ending palette into slot 0x15, find the ending pad, start the music
 * track, apply both volumes, allocate the backdrop to the pad's reported size and read it in,
 * re-arm the A timers, set screen id 18, and zero the scroll. */
void EndScreen_Init(const EndScreenInitEnv *pEnv, u8 bGoodEnd)
{
    AssetReader rdr;
    const i32 *pRecs;
    const char *pszPalette;
    const char *pszPad;
    i32 nRows;
    i32 nTrack;
    u32 cb;

    AssetReader_Clear(&rdr); /* 0x388C0 */
    PufoFont_Init(pEnv->pFont); /* 0x388CC */

    if (bGoodEnd != 0) {
        *pEnv->pnMaxRows = 10; /* 0x388DC */
        nRows = 10;
        pRecs = g_aGoodEndRowRecs;
        pszPalette = "goodend.pal";
        pszPad = "goodend.pad";
        nTrack = 4; /* 0x38944 */
    } else {
        *pEnv->pnMaxRows = 16; /* 0x388EC */
        nRows = 16;
        pRecs = g_aBadEndRowRecs;
        pszPalette = "badend.pal";
        pszPad = "badend.pad";
        nTrack = 3; /* 0x38984 */
    }

    memcpy(pEnv->anRowRec, pRecs, (size_t)nRows * sizeof(i32)); /* 0x388FC */

    pEnv->pFont->nGlyphBase = pEnv->pFont->nTotalSprFrames; /* 0x38908: unk_158290 = fontSys + 0x28 */
    (void)Font_LoadSpr(pEnv->pFont, "font"); /* 0x38910 */
    Font_BuildIndex(pEnv->pFont); /* 0x38918 */

    (void)Sound_InitAndLoadSamples("z"); /* 0x38920 */

    Palette_Load(pszPalette, 0x15u); /* 0x38934 / 0x38974 */
    (void)AssetReader_Find(&rdr, pszPad); /* 0x38940 / 0x38980 */
    Sound_LoadAndPlayMusic(nTrack); /* 0x38988 */
    Sound_ApplyMusicVolume(Options_MusicLevel(pEnv->pOptions), pEnv->pOptions->bMusicOff); /* 0x3898C */
    Sound_ApplySfxVolume(Options_SfxLevel(pEnv->pOptions), pEnv->pOptions->bSoundOff); /* 0x38990 */

    cb = AssetReader_Size(&rdr); /* 0x38998 */
    *pEnv->ppBackdrop = (u8 *)malloc(cb); /* 0x389A4: operator new, unchecked in the original */
    cb = AssetReader_Size(&rdr); /* 0x389AC: the size is re-read, not kept */
    if (*pEnv->ppBackdrop != NULL) { /* deviation: the original reads whatever new returned */
        (void)AssetReader_Read(&rdr, *pEnv->ppBackdrop, cb); /* 0x389BC */
    }

    Timer_StartA(); /* 0x389C0: StartTimers_A */
    *pEnv->puUiScreen = 18; /* 0x389CC: Map_nUiScreen */
    *pEnv->pnScroll = 0; /* 0x389DC: unk_138E88 */
}
