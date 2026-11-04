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
#include "game/pufo_font.h"
#include "game/pufo_options.h"
#include "render/pufo_render.h"

#define PUFO_TITLES_W        240
#define PUFO_TITLES_HEIGHT   320
#define PUFO_TITLES_ROW_STEP 20
#define PUFO_TITLES_ROW_BASE 17

/* The credits backdrop glyph is drawn in palette slot 21; an '@' glyph in slot 1. */
#define PUFO_TITLES_BACKDROP_PAL 21
#define PUFO_TITLES_GLYPH_PAL     1

/* The two slots Titles_DrawAnimatedFrame hardcodes. The particle and star fills take
 * MEMORY[0x1472D0] -- palette slot 0 -- and Titles_PresentToSoftSurf passes the POINTER
 * unk_14A2D0 @ 0x14A2D0, which is aPalette + 0x3000, i.e. slot 12 (the call is at 0x46718 with no
 * variable in front of either). They are constants of the original, not state: the port once carried
 * them in the animation block and set them from the shell, where nothing read or checked them. */
#define PUFO_TITLES_FILL_PAL_SLOT    0
#define PUFO_TITLES_PRESENT_PAL_SLOT 12

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
 * ported at this file:347) and freed in Titles_dtor (0x46fa4, ported at this file:501), and this is the only module that
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
 *   +84020  star array, stride 16
 *   +87220  star count
 *   +87228  the scroll position the credit lines use
 * The frame loop cursor is at the separate Render_Frame caller context+0x100, not in this block.
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
    /* The two bytes at the head of a title scene, read by Titles_LoadScene. They scale where the
     * home pair lands, so they are state and not locals. */
    i32             nSceneScaleX;    /* g_pTitlesState + 0 */
    i32             nSceneScaleY;    /* g_pTitlesState + 4 */
    i32             nSceneIndex;     /* g_pTitlesState + 16: which titleN.dat to load next */
    SoftSurf       *pTitlesSurface;  /* g_pTitlesSurface, 0x158808 */
    TitlesBackdrop  Credits;         /* the credit lines; note their scroll is this nScroll */
} TitlesAnimation;

/* Titles_PresentToSoftSurf @ 0x466dc. Copies g_pTitlesSurface into g_pMainSurface at (0, 0) using that
 * surface's own allocation as the glyph, through PUFO_TITLES_PRESENT_PAL_SLOT. There is no argument at
 * all: the original has none, the surface is the global, and the palette is the hardcoded slot. */
void Titles_PresentToSoftSurf(void);

/* Titles_UpdateStars @ 0x46dbc. Every star advances by its speed; above 320 it is reborn at a random
 * x with speed 1..3 and amplitude 1..100. */
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

/* Titles_LoadScene @ 0x468cc. Loads "titleN.dat" where N is the scene index, which cycles 0, 1, 0.
 * The file is a 2-byte header followed by byte pairs: the header gives the two scale factors, and each
 * pair is one particle's home position. Every particle starts at a random point with its target fixed at
 * (120, 160), and the frame counter is reset.
 *
 * Deviation: the original never checks whether the member was found, so a missing titleN.dat makes it
 * allocate zero bytes and then read the header out of that allocation. The port skips the record loop in
 * that case and still resets the frame counter and advances the scene index. */
i32 Titles_LoadScene(TitlesAnimation *pEnv);

/* Titles_ctor @ 0x46e88. Zeroes the scene index (state +16), creates the titles surface — new(0x28),
 * SoftSurf_ctor, AllocPixels(240, 320, mode 0), i.e. 240x320 at one byte per pixel, which is why the
 * whole surface is 0x12C00 bytes — and stores it in g_pTitlesSurface, then seeds 190 stars (state
 * +87220): x = RandU16(0xF0), y = RandU16(0x140) - 320 (so every star starts above the top edge,
 * -320..-1), speed = RandU16(3) + 1, amplitude = RandU16(0x64) + 1 — and runs one Titles_UpdateStars.
 *
 * Deviation: the original calls SoftSurf_AllocPixels on the new(0x28) result even when it is NULL; the
 * port only constructs the surface when the allocation succeeded. */
void Titles_ctor(TitlesAnimation *pEnv);

/* The opening credits' 44 row-record indices, verbatim from the image blob unk_A63A8 (0xB0 bytes).
 * Records 0x587..0x59C, then 0x655/0x656, 0x5A0, 0x64A..0x652, 0x5A0, 0x653/0x654, 0x5A0,
 * 0x59D..0x5A2. Record 0x5A0 recurs as a spacer row. */
extern const i32 g_aTitlesRowRecs[44];

/* Titles_Init @ 0x380d8: the opening credits screen. In the original's order: clear the reader,
 * new(0x154C0) + Titles_ctor (the port's state block and its particle/star arrays are caller-owned),
 * 44 rows + the row table copy, re-init the font with the glyph base taken from the running frame
 * count before "font" is loaded, build the index, fade.pal into slot 12, screen id 16, load scene 0,
 * StartTimers_C, zero the credits scroll (state +87228), titles.pal into slot 31, load the five
 * titles0..4.pad glyph blobs into unk_1571A0 (the '@' glyph table Titles_DrawCreditLines indexes),
 * the "tact" samples, music track 3, both volumes, and a memset of the whole titles surface.
 *
 * The original also clears unk_156930 (0x3817C); that global has no reader anywhere in the binary —
 * its only other write is Map_ComputeReachable @ 0x2b820 — so the port does not model it.
 *
 * Deviation: the original's operator new for each glyph blob is unchecked and the read runs on
 * whatever it returned; the port skips the read when the allocation failed. */
typedef struct TitlesInitEnv {
    PufoFont          *pFont;        /* fontSys @ 0x158268 */
    TitlesAnimation   *pAnim;        /* the 0x154C0 state block, caller-owned */
    i32               *pnMaxRows;    /* nTitlesMaxRows @ 0x157194 */
    i32               *anRowRec;     /* aTitlesRowRecs @ 0xB35D8, 44 slots */
    u8               **apGlyphs;     /* unk_1571A0..0x1571B0, five blobs */
    u16               *puUiScreen;   /* Map_nUiScreen @ Map+0x3A */
    const PufoOptions *pOptions;     /* the option block the two volume calls read */
} TitlesInitEnv;

void Titles_Init(TitlesInitEnv *pEnv);

/* --- the ending screens ---------------------------------------------------------------------
 *
 * Evidence: EndScreen_Init @ 0x388b0, called from Tact_CheckBattleEnd @ 0x3501C/0x35188,
 * Smap_BeginCraftSelect @ 0x84230, and Geoscape_CloseUI @ 0x8BBF8.
 *
 * The two endings share one body and differ in four values: the row count (10 good / 16 bad), the
 * row-record table (the image blobs unk_A6458 and unk_A6480 - text records 0x56B..0x574 for the
 * good end, 0x575..0x584 for the bad end), the palette (goodend.pal / badend.pal, both into slot
 * 0x15), and the music track (4 / 3). The backdrop glyph is goodend.pad / badend.pad, allocated to
 * the byte size the archive reports and read into it.
 *
 * The font step is the one that is easy to misread: unk_158290 is fontSys + 0x28, the glyph index
 * base Text_DrawString adds character bytes to, and the original sets it to the running sprite
 * frame count BEFORE loading "font" - so the glyph base becomes the first frame index of the font
 * group. Sound samples come from the "z" prefix. The tail is StartTimers_A, screen id 18, and a
 * zeroed scroll.
 */
extern const i32 g_aGoodEndRowRecs[10];
extern const i32 g_aBadEndRowRecs[16];

typedef struct EndScreenInitEnv {
    PufoFont        *pFont;      /* fontSys @ 0x158268 */
    i32             *pnMaxRows;  /* unk_157194 */
    i32             *anRowRec;   /* unk_B35D8: 16 u32 slots; the good end fills the first 10 */
    u8             **ppBackdrop; /* unk_1571C0: the end-screen backdrop glyph */
    i32             *pnScroll;   /* unk_138E88 */
    u16             *puUiScreen; /* Map_nUiScreen @ Map+0x3A */
    const PufoOptions *pOptions; /* the option block the two volume calls read */
} EndScreenInitEnv;

/* EndScreen_Init @ 0x388b0. bGoodEnd picks the good-ending values; zero picks the bad ones.
 *
 * Deviation: the original's operator new is unchecked and AssetReader_Read runs on it whatever
 * the archive lookup found; the port skips the read when the allocation failed. The two volume
 * calls read the option block in the original; the port passes its values explicitly. */
void EndScreen_Init(const EndScreenInitEnv *pEnv, u8 bGoodEnd);

/* --- shutting the two ending screens down ------------------------------------------------------
 *
 * Evidence: Titles_Shutdown @ 0x3826c (100 B) and EndScreen_Shutdown @ 0x389f8 (48 B), the screen-16
 * and screen-18 arms of Input_OnPointerUp @ 0x14924, plus Titles_dtor @ 0x46fa4 (60 B), the surface
 * half of the first.
 *
 * Titles_Shutdown, in order: Sound_Shutdown (0x38270), KillTimers_C (0x38274), Font_Free on
 * fontSys @ 0x158268 (0x3827C), then five operator delete over unk_1571A0..0x1571B0 (0x38280..0x3828C)
 * -- the five titlesN.pad glyph blobs Titles_Init allocated -- and finally, only when
 * g_pTitlesState @ 0x158804 is non-null (0x382AC), Titles_dtor followed by a delete of the 0x154C0
 * state block itself (0x382B4/0x382BC). Nothing is nulled afterwards: every one of those globals is
 * left dangling and the next Titles_Init simply overwrites them.
 *
 * Titles_dtor writes Map_nUiScreen (Map+0x3A) with 22 (0x46FB0) -- NOT the 16 Titles_Init set --
 * whether or not the surface exists, and only then, when g_pTitlesSurface @ 0x158808 is non-null
 * (0x46FC0), runs SoftSurf_dtor (0x46FC8) and the delete (0x46FD0).
 *
 * EndScreen_Shutdown: Sound_Shutdown (0x389FC), KillTimers_A (0x38A00), Font_Free (0x38A08), an
 * UNTESTED operator delete of the end-screen backdrop at unk_1571C0 (0x38A0C..0x38A14), and a tail
 * call into GameUI_InitStartup @ 0x38a28 (0x38A20) -- the ending does not exit, it goes back to the
 * start menu. The backdrop delete is unguarded: EndScreen_Init @ 0x388b0 is the only writer and its
 * own operator new is unchecked, so a screen that was never initialised deletes whatever a previous
 * run left there.
 *
 * Deviation: the port frees through the same owner Titles_Init / EndScreen_Init used (malloc) and
 * nulls every pointer it releases, which the original does not; the port also bounds the glyph array
 * and the screen-id pointer instead of dereferencing them blindly. */

#define PUFO_TITLES_DTOR_SCREEN 22 /* 0x46FB0: the screen id Titles_dtor leaves behind */

typedef struct TitlesShutdownEnv {
    PufoFont        *pFont;      /* fontSys @ 0x158268 */
    u8             **apGlyphs;   /* unk_1571A0..0x1571B0, the five blobs Titles_Init filled */
    TitlesAnimation *pAnim;      /* g_pTitlesState @ 0x158804 */
    u16             *puUiScreen; /* Map_nUiScreen @ Map+0x3A */
} TitlesShutdownEnv;

typedef struct EndScreenShutdownEnv {
    PufoFont             *pFont;      /* fontSys @ 0x158268 */
    u8                  **ppBackdrop; /* unk_1571C0, the backdrop EndScreen_Init allocated */
    struct GameUiStartupEnv *pStartup; /* GameUI_InitStartup @ 0x38a28, the tail call */
} EndScreenShutdownEnv;

/* Titles_dtor @ 0x46fa4: the screen id, then the surface. */
void Titles_dtor(u16 *puUiScreen);

void Titles_Shutdown(const TitlesShutdownEnv *pEnv);
void EndScreen_Shutdown(const EndScreenShutdownEnv *pEnv);

#endif /* PUFO_TITLES_H */
