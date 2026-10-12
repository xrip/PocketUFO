/* pufo_panel.h - the shared UI panel: the line engine every screen draws through.
 *
 * Evidence: UiPanel_ctor @ 0x3e730, UiPanel_FreeLines @ 0x3e8c8,
 *           UiPanelMsg_Draw @ 0x3e94c, UiPanel_WrapText @ 0x37144,
 *           UiPanel_SetLineMeta @ 0x3f06c, UiPanel_SetBytes0to2 @ 0x3f05c,
 *           UiPanel_SetBytes3to5 @ 0x3e93c, UiPanel_SetBytes6to11 @ 0x3e914,
 *           IncDwordIfInRange_AtPlus0xBD4 @ 0x3f00c,
 *           DecDwordIfPos_AtPlus0xBD4 @ 0x3f040,
 *           UiPanel_ScrollNext @ 0x99f78, ScrollPrev @ 0x99f88,
 *           UiScrollList_TrackHold @ 0x3ed44.
 *
 * ## Which functions belong to this type, and which do NOT
 *
 * `UiPanel` is 7092 (0x1BB4) bytes; the allocation size is explicit at the
 * `UiPanel_ctor` call site (`MOV R0, #0x1BB4`) and the layout is confirmed two
 * independent ways:
 *
 *   gUiRootPanel @ 0x158890 + apLines(0xBE4) + 4*105 = 0x159618 = nGameTimeSec
 *   gUiRootPanel @ 0x158890 + apLines(0xBE4) + 4*106 = 0x15961C = bGeoscapeFlag
 *   UiScrollList_TrackHold @ 0x3ed44 dword indices all land on 7092-layout fields
 *
 * The first pair matters: the clock globals are reached *through* apLines[105]
 * and apLines[106], so those slots are reused as a packed timestamp and a pause
 * flag rather than as line pointers.
 *
 * `UiPanel_InsertLine` @ 0x5a294, `UiPanel_RemoveLine` @ 0x5a3dc,
 * `UiPanel_GetLineFlag` @ 0x5a334 and `UiPanel_GetLineColor` @ 0x5a33c are a
 * DIFFERENT structure that IDA merged under the same name. They index a plain
 * `u8 abLineFlags[nLine]` at offset 0 and 16-byte records with four dword colour
 * slots at `+0xC8..+0xD4`, with a `u8` count at `+0xD48` (3400). They are
 * therefore deliberately NOT ported here; they need that second structure
 * recovered first. See docs/parity/game-core.md for the full proof.
 *
 * ## Layout
 *
 *   gap_0        @ 0x0000  16 panel-level style bytes
 *   aLineMeta    @ 0x0010  500 * 6 bytes: two RGB triples per line
 *   nUnk3016     @ 0x0BC8
 *   pfnClick     @ 0x0BCC
 *   bUnk3024     @ 0x0BD0
 *   nUnk3028     @ 0x0BD4  first visible row (the scroll cursor)
 *   nUnk3032     @ 0x0BD8
 *   nLineCount   @ 0x0BDC
 *   nStyle       @ 0x0BE0  visible row count
 *   apLines      @ 0x0BE4  500 line buffers
 *   anLineAux    @ 0x13B4  500 aux dwords
 *   nPosX/Y      @ 0x1B84
 *   nViewW/H     @ 0x1B94
 *   nFont        @ 0x1B9C
 */
#ifndef PUFO_PANEL_H
#define PUFO_PANEL_H

#include "game/pufo_font.h"
#include "pufo_common.h"

#define PUFO_PANEL_LINES      500
#define PUFO_PANEL_LINE_BYTES 50  /* 0x32, per-line buffer size from UiPanel_ctor */
#define PUFO_PANEL_META_BYTES 6   /* aLineMeta stride per line */
#define PUFO_PANEL_STYLE_LEN  16  /* gap_0 size */

/* Per-line meta byte offsets inside aLineMeta[6 * nLine]. Two RGB triples:
 * the first is the normal row colour, the second the highlighted row colour. */
#define PUFO_PANEL_META_RGB1 0
#define PUFO_PANEL_META_RGB2 3

/* Panel-level style byte offsets inside gap_0, from SetBytes0to2/3to5/6to11. */
#define PUFO_PANEL_STY_KEY   0 /* gap_0[0..2], passed to the row blit as the key */
#define PUFO_PANEL_STY_SRC   3 /* gap_0[3..5], source rect */
#define PUFO_PANEL_STY_RGB1  6 /* gap_0[6..8],  panel colour triple */
#define PUFO_PANEL_STY_RGB2  9 /* gap_0[9..11], panel colour triple */

typedef struct UiPanel {
    u8   gap_0[PUFO_PANEL_STYLE_LEN];
    u8   aLineMeta[PUFO_PANEL_LINES * PUFO_PANEL_META_BYTES];
    i32  nUnk3016;
    void (*pfnClick)(void);
    u8   bUnk3024;
    i32  nUnk3028;  /* scroll cursor: first visible row */
    i32  nUnk3032;
    i32  nLineCount;
    i32  nStyle;    /* visible row count */
    u8  *apLines[PUFO_PANEL_LINES];
    /* Slots 105 and 106 alias nGameTimeSec and bGeoscapeFlag_15961C in the
     * original. That aliasing is NOT modelled: the port writes the clock through
     * GameClock (see pufo_tactctl.h) so the two identities stay separate. The
     * slots exist here only to preserve the array geometry. */
    i32  anLineAux[PUFO_PANEL_LINES];
    i32  nPosX;
    i32  nPosY;
    i32  nUnk7052;
    i32  nUnk7056;
    i32  nViewW;
    i32  nViewH;
    i32  nFont;
    u8   bFlag7072;
    u8   bFlag7073;
    u8   bFlag7074;
    i32  nUnk7076;
    u8   bUnk7080;
    i32  nUnk7084;
    u8   bUnk7088;
    u8   bUnk7089;
} UiPanel;

/* UiPanel_ctor @ 0x3e730.
 *
 * Allocates nLineCount line buffers of 50 bytes, each an empty string, and
 * applies the original's defaults: nFont 17, bFlag7072 1, bFlag7073 1, nStyle 1,
 * nViewW 240, nViewH 320, bUnk7088 0xFF, everything else 0.
 *
 * nLineCount is clamped to the 500-line capacity. The original trusts its caller;
 * the clamp is a deviation listed in the parity notes. Returns false only when a
 * line buffer could not be allocated. */
bool UiPanel_ctor(UiPanel *pPanel, i32 nLineCount);

/* UiPanel_FreeLines @ 0x3e8c8. Frees every line buffer. */
void UiPanel_FreeLines(UiPanel *pPanel);

/* Style setters. Each writes the exact gap_0 byte slots the original touches. */
void UiPanel_SetBytes0to2(UiPanel *pPanel, u8 b0, u8 b1, u8 b2);
void UiPanel_SetBytes3to5(UiPanel *pPanel, u8 b3, u8 b4, u8 b5);
void UiPanel_SetBytes6to11(UiPanel *pPanel, u8 b6, u8 b7, u8 b8, u8 b9, u8 b10, u8 b11);

/* UiPanel_SetLineMeta @ 0x3f06c. Writes aLineMeta[6*nLine + 0..5].
 *
 * The original's parameter order is (a3..a8) with a3 going to slot 0, a4 to slot
 * 1, a5 to slot 2, a6 to slot 3, a7 to slot 4, a8 to slot 5 -- i.e. two triples
 * written in order. Reproduced as-is. */
void UiPanel_SetLineMeta(UiPanel *pPanel, i32 nLine, u8 b0, u8 b1, u8 b2,
                         u8 b3, u8 b4, u8 b5);

/* Reads one RGB triple of a line's meta. nWhich is 1 or 2 (the two triples). */
bool UiPanel_GetLineMeta(const UiPanel *pPanel, i32 nLine, i32 nWhich,
                         u8 *pR, u8 *pG, u8 *pB);

/* Writes a line's text, truncated to 49 bytes plus NUL. The original's buffers
 * are 50 bytes and it copies with strcpy, so an over-long string overflows. The
 * truncation here is a deviation. */
bool UiPanel_SetLineText(UiPanel *pPanel, i32 nLine, const char *psz);

/* The line pointer, or NULL. */
const char *UiPanel_GetLineText(const UiPanel *pPanel, i32 nLine);

/* anLineAux, the numeric column shown beside a line. */
void UiPanel_SetLineAux(UiPanel *pPanel, i32 nLine, i32 nAux);
i32  UiPanel_GetLineAux(const UiPanel *pPanel, i32 nLine);

/* Scrolling.
 *
 * IncDwordIfInRange_AtPlus0xBD4 @ 0x3f00c:
 *     if (nUnk3028 < nLineCount - nStyle) nUnk3028 += 1;
 * DecDwordIfPos_AtPlus0xBD4 @ 0x3f040:
 *     if (nUnk3028 > 0) nUnk3028 -= 1;
 * The non-Sel wrappers @ 0x99f78/0x99f88 act on a fixed panel pointer held in
 * dword_187200; the Sel wrappers @ 0x99f98/0x99fb4 on
 * dword_1871D0[byte_1871F8]. Both are the same operation, so the port takes the
 * panel explicitly. */
void UiPanel_ScrollNext(UiPanel *pPanel);
void UiPanel_ScrollPrev(UiPanel *pPanel);

/* UiPanel_WrapText @ 0x37144.
 *
 * Greedy word wrap of pszText into the line buffers from nLineStart, breaking on
 * spaces and measuring with Text_MeasureWidth. Returns the number of lines
 * produced, and sets nLineCount = nLineStart + that count.
 *
 * Reproduced including the trailing space the original appends to its working
 * copy: that means an input whose last word exactly fits can emit one extra
 * (empty) line. */
i32 UiPanel_WrapText(UiPanel *pPanel, u8 nMaxW, u8 nLineStart, const char *pszText);

/* UiPanelMsg_Draw @ 0x3e94c, text portion.
 *
 * Renders the visible rows into the main surface. Each row is drawn at
 *     y = (row - nUnk3028) * nFont + (nFont - 16) / 2 + nPosY
 * with the text centred, right-aligned or left-aligned according to nUnk7076
 * (1 = centre, 2 = right, 0 = left), and the x origin is nPosX minus that offset.
 *
 * The original also blits a background strip and a source rect before the rows,
 * via SoftSurf_BlitRectProxy and SoftSurf_BlitSrcRect. Those two entry points are
 * not ported yet, so this renders the rows only. That is the part the panel engine
 * itself owns. */
void UiPanel_DrawText(UiPanel *pPanel);

/* UiScrollList_ClickRow @ 0x3eee0.
 *
 * Computes the clicked row as `(nY - nPosY) / nFont`, stores it in the dword at panel
 * offset 12 (gap_0[12]) and clears `nUnk3016`, `bUnk3024` and the global `unk_158018`.
 * `nUnk3028` is READ, never written: ARM 0x3EEE0..0x3F000 writes only unk_158018,
 * 0xBC8, 0xC and 0xBD0, and reaches 0xBD4 only as the left side of the sum.
 * When `bFlag7073` is set and the row is within `nStyle`/`nLineCount`, and (nX, nY) is
 * strictly inside the rect at (`nUnk7052`, `nUnk7056`) sized `nViewW` x `nViewH`, it
 * sets `nUnk3032 = nUnk3028 + row` and calls `pfnClick`. Returns the row index. */
i32 UiPanel_ClickRow(UiPanel *pPanel, i32 nX, i32 nY);

/* UiScrollList_TrackHold @ 0x3ed44, the pointer-move half of the same list.
 *
 * It computes the row the same way and stores it in gap_0[12], but it does NOT clear
 * `nUnk3016` or `unk_158018` on entry. Outside the rect, or past `nStyle`/`nLineCount`,
 * it clears `bUnk3024` and `nUnk3016`. Inside, it sets `bUnk3024` and then runs the
 * press-and-hold repeat: only while `bUnk7080` is set does it count, it resets the
 * counter whenever the hovered row differs from the global `unk_158018`, and when
 * `nUnk3016` reaches `nUnk7084` it clears `bUnk7080`, clamps `nUnk3016` to that
 * threshold, sets `nUnk3032 = nUnk3028 + row` and calls `pfnClick`. So a held pointer
 * auto-repeats the row click once per `nUnk7084` calls, and moving to another row
 * restarts the count. Returns the row index. */
i32 UiPanel_TrackHoldRow(UiPanel *pPanel, i32 nX, i32 nY);

/* Ui_SwapStringSlots @ 0x6aaac. Swaps two lines' text pointers' contents and their
 * anLineAux values. The original copies through a 200-byte stack buffer, which the
 * port keeps as the same bound. */
bool UiPanel_SwapLines(UiPanel *pPanel, i32 nIdxA, i32 nIdxB);

#endif /* PUFO_PANEL_H */
