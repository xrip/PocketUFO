/* pufo_titles.h - the credits screen.
 *
 * Evidence: Titles_DrawBackdrop @ 0x1e630.
 *
 * The credits screen scrolls a list of text records up the screen, twenty pixels per row, and the
 * whole list shifts up by the scroll position. Two things here are not obvious from the
 * pseudocode:
 *
 *   A record whose first character is '@' is not text. The rest of the record is a decimal index
 *   into a glyph table, and that glyph is drawn centred instead of the string. The ARM at 0x1E7E8
 *   is `BL atoi` then `LDR R0, [R11,R0,LSL#2]` then `LDRH R0, [R0]`: the table holds POINTERS,
 *   four bytes each, and the width comes from the glyph blob's own header, not from a struct.
 *
 *   The centring is `(240 - width) / 2` written as `RSB; CMP #0; ADMI #1; ASR #1`, which is the
 *   compiler's signed divide-by-two: a negative difference rounds toward zero rather than away
 *   from it. That only matters for a glyph wider than the screen.
 */
#ifndef PUFO_TITLES_H
#define PUFO_TITLES_H

#include "pufo_common.h"

#include "game/pufo_boot.h"
#include "render/pufo_render.h"

#define PUFO_TITLES_W        240
#define PUFO_TITLES_HEIGHT   320
#define PUFO_TITLES_ROW_STEP 20
#define PUFO_TITLES_ROW_BASE 17

/* The credits backdrop glyph is drawn in palette slot 21; an '@' glyph in slot 1. */
#define PUFO_TITLES_BACKDROP_PAL 21
#define PUFO_TITLES_GLYPH_PAL     1

typedef struct TitlesBackdrop {
    i32 nScroll;                /* unk_138E88: the scroll position in pixels */
    i32 nMaxRows;               /* unk_157194: how many credit rows the data has */
    const u8 *pBackdrop;        /* unk_1571C0: the full-screen backdrop glyph */
    const i32 *anRowRec;        /* unk_B35D8: the text-record index of each row */
    const u8 *const *apGlyphs;  /* unk_1571A0: glyph blobs, each headed by its u16 width */
    const u8 *pTextRecs;        /* aTextRecs @ 0xB38D0, 205-byte records */
    const GfxColorEnv *pColor;
} TitlesBackdrop;

/* Titles_DrawBackdrop @ 0x1e630.
 *
 * Backdrop first. Then, while the scroll is under 50, a white fill over the whole screen; between
 * 50 and 280 a keyed full-screen blit whose key is 3 * (135 - scroll) clamped to 255, which is a
 * fade from white. Above 320 the visible window slides: first row (scroll - 340)/20, and it spans
 * 36 rows; below that the window starts at row 0 and grows to scroll/20 rows. */
i32 Titles_DrawBackdrop(TitlesBackdrop *pEnv);

/* The original's titles surface, unk_158808 @ 0x158808. It is created in Titles_ctor (0x46e88,
 * not ported) and freed in Titles_dtor (0x46fa4, not ported), and this is the only module that
 * touches it, so the port owns it here. */
extern SoftSurf *g_pTitlesSurface;

/* SoftSurf_DrawBorderAndBlur @ 0x9e5c8.
 *
 * Four strips of palette entry 0 around the edge of the titles surface, then a blur of the whole
 * surface. Note the asymmetry the original actually has: the four strips are hardcoded 240 and
 * 320, while the blur is given the surface's real width and height read through the accessors. */
u8 *SoftSurf_DrawBorderAndBlur(void);

/* --- the titles animation state ------------------------------------------------------------
 *
 * The original keeps this in one block at g_pTitlesState (0x158804). Offsets below are proven from ARM
 * displacements, not inferred:
 *
 *   +8      frame counter, incremented once per animated frame
 *   +12     particle count
 *   +20     particle array, stride 28
 *   +256    a scratch loop cursor, reset to 0 at the start of each of the two draw loops
 *   +84020  star array, stride 16
 *   +87220  star count
 *   +87228  the scroll position the credit lines use
 *
 * The particle record is 28 bytes and the ARM reads it as base + 28*i + {20, 24, 28, 32, 36, 40, 44},
 * which is a 28-byte record starting at +20 with fields at +0, +4, +8, +12, +16, +20, +24. Reading the
 * same fields as offsets from the block base makes them look like they run past the end of a record.
 */
#define PUFO_TITLES_PARTICLE_STRIDE 28
#define PUFO_TITLES_STAR_STRIDE     16
#define PUFO_TITLES_STAR_COLS       3   /* both the width and the height of a star fill are 3 */

/* The angle constant is 0.01 exactly: the double pair 1065646817 / 1202590843 is 0x3F847AE147AE147B. */
#define PUFO_TITLES_STAR_ANGLE_SCALE 0.01

typedef struct TitlesParticle {
    i32 nX;         /* +0  where it is drawn, jittered by RandU16(5) at draw time */
    i32 nY;         /* +4 */
    i32 nTargetX;   /* +8  the point it walks towards */
    i32 nTargetY;   /* +12 */
    i32 nHomeX;     /* +16  restored into the target at frame 21 */
    i32 nHomeY;     /* +20 */
    i32 nTimer;     /* +24 counts down; a negative value sends it back up the screen */
} TitlesParticle;

typedef struct TitlesStar {
    i32 nBaseX;     /* +0  the centre the streak swings about */
    i32 nY;         /* +4  advances every frame and wraps above 320 */
    i32 nSpeed;     /* +8  also the angle rate: angle = nSpeed * nY * 0.01 */
    i32 nAmplitude; /* +12 how far the streak swings in x */
} TitlesStar;

typedef struct TitlesAnimation {
    TitlesParticle *aParticles;      /* g_pTitlesState + 20 */
    i32             nParticleCount;  /* g_pTitlesState + 12 */
    TitlesStar     *aStars;          /* g_pTitlesState + 84020 */
    i32             nStarCount;      /* g_pTitlesState + 87220 */
    i32             nFrame;          /* g_pTitlesState + 8 */
    i32             nScroll;         /* g_pTitlesState + 87228 */
    SoftSurf       *pTitlesSurface;  /* g_pTitlesSurface, 0x158808 */
    /* aPalette is at 0x1472D0 with 1024 bytes per slot, so the two palettes this module passes are
     * slot 0 (the original writes MEMORY[0x1472D0]) and slot 12 (the original writes &unk_14A2D0,
     * which is 0x1472D0 + 0x3000). */
    i32             nFillPalSlot;    /* 0 */
    i32             nPresentPalSlot; /* 12 */
    TitlesBackdrop  Credits;         /* the credit lines; note their scroll is this nScroll */
    void (*pfnLoadScene)(void *pUser); /* Titles_LoadScene (0x468cc), not ported */
    void           *pSceneUser;
} TitlesAnimation;

/* Titles_PresentToSoftSurf @ 0x466dc. Copies the titles surface into the main surface at (0, 0), using
 * the surface's own allocation as the glyph and palette slot 12. */
void Titles_PresentToSoftSurf(SoftSurf *pTitlesSurface, i32 nPresentPalSlot);

/* Titles_UpdateStars @ 0x46dbc. Every star advances by its speed; above 320 it is reborn at a random
 * x with speed 1..4 and amplitude 1..101. */
void Titles_UpdateStars(TitlesAnimation *pEnv);

/* Titles_AnimateParticles @ 0x46acc. Each particle steps towards its target by (target - current) / 15,
 * with a sign fallback so a gap under 15 still moves one pixel per frame. */
i32 Titles_AnimateParticles(TitlesAnimation *pEnv);

/* Titles_DrawCreditLines @ 0x46738. Same window rule as Titles_DrawBackdrop, same "@ means a glyph
 * index" rule, but the glyph is drawn with palette 31 and text is centred as (240 - width) / 2. */
i32 Titles_DrawCreditLines(TitlesBackdrop *pEnv);

/* Titles_DrawAnimatedFrame @ 0x1a83c. Draws the particles as 1x1 fills and the stars as 3x3 fills whose
 * x is dtoi(amplitude * cos(speed * y * 0.01) + baseX), then advances the frame counter and runs the
 * border blur, the present, the particle step, the star step and the credit lines. */
i32 Titles_DrawAnimatedFrame(TitlesAnimation *pEnv);

#endif /* PUFO_TITLES_H */
