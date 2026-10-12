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

#endif /* PUFO_TITLES_H */
