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
 * slots at `+0xC8..+0xD4`, with a `u8` count at `+0xD48` (3400). They are ported as
 * their own type in `ui/pufo_linelist.{h,c}`, NOT as UiPanel members. See
 * docs/parity/game-core.md, "The second structure IDA merges under UiPanel".
 *
 * ## Layout
 *
 *   aStyleRgb    @ 0x0000  12 panel-level style bytes: [0..2] the row-rule RGB the
 *                          UiPanelMsg_Draw rule blits take (SoftSurf_BlitRectProxy
 *                          nIdx/nColorMid/nColorLow), [3..5] the selection-band RGB
 *                          (SoftSurf_BlitSrcRect), [6..8] and [9..11] the two text-row
 *                          RGB triples (Gfx_PackRgb565Pair r1,g1,b1 / r2,g2,b2)
 *   nHoverRow    @ 0x000C  the row last under the pointer (TrackHold / ClickRow)
 *   aLineMeta    @ 0x0010  500 * 6 bytes: two RGB triples per line
 *   nHoldCount   @ 0x0BC8  press-and-hold counter
 *   pfnClick     @ 0x0BCC
 *   bHoverActive @ 0x0BD0  1 while the pointer is inside the hover box (TrackHold)
 *   nUnk3028     @ 0x0BD4  first visible row (the scroll cursor)
 *   nUnk3032     @ 0x0BD8
 *   nLineCount   @ 0x0BDC
 *   nStyle       @ 0x0BE0  visible row count
 *   apLines      @ 0x0BE4  500 line buffers
 *   anLineAux    @ 0x13B4  500 aux dwords
 *   nPosX/Y      @ 0x1B84  text origin
 *   nRectX/Y     @ 0x1B8C  hover box origin (TrackHold/ClickRow hit test, rule blit x)
 *   nViewW/H     @ 0x1B94  hover box size
 *   nFont        @ 0x1B9C
 */
#ifndef PUFO_PANEL_H
#define PUFO_PANEL_H

#include "game/pufo_font.h"
#include "render/pufo_render.h"
#include "pufo_common.h"

#define PUFO_PANEL_LINES      500
#define PUFO_PANEL_LINE_BYTES 50  /* 0x32, per-line buffer size from UiPanel_ctor */
#define PUFO_PANEL_META_BYTES 6   /* aLineMeta stride per line */

/* Per-line meta byte offsets inside aLineMeta[6 * nLine]. Two RGB triples:
 * the first is the normal row colour, the second the highlighted row colour. */
#define PUFO_PANEL_META_RGB1 0
#define PUFO_PANEL_META_RGB2 3

typedef struct UiPanel {
    u8   aStyleRgb[12]; /* +0x00: [0..2] rule RGB, [3..5] select-band RGB,
                         * [6..8]/[9..11] the text-row RGB pair (see the layout note) */
    i32  nHoverRow;     /* +0x0C: row last under the pointer */
    u8   aLineMeta[PUFO_PANEL_LINES * PUFO_PANEL_META_BYTES];
    i32  nHoldCount;    /* +0xBC8: press-and-hold counter */
    void (*pfnClick)(void);
    u8   bHoverActive;  /* +0xBD0: 1 while the pointer is inside the hover box */
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
    i32  nPosX;         /* text origin */
    i32  nPosY;
    i32  nRectX;        /* +0x1B8C: hover box left */
    i32  nRectY;        /* +0x1B90: hover box top */
    i32  nViewW;        /* hover box width */
    i32  nViewH;        /* hover box height */
    i32  nFont;
    /* The four UiPanelMsg_Draw @ 0x3e94c draw flags, named for the branch each one
     * gates in the decompilation. The original's own names are the field offsets. */
    u8   bRowRules;          /* panel+0x1BA0: 0x3E974 blits a 1px rail at (nRectX, nPosY-1)
                              * and 0x3EADC a 1px rule under every visible row. */
    u8   bSelLineHighlight;  /* panel+0x1BA1: with bHoverActive != 0, 0x3E9E4 blits a
                              * (nViewW-2) x nFont band at row nHoverRow, keyed by bColorKey. */
    u8   bAuxNumbers;        /* panel+0x1BA2: 0x3EBA8 takes the numeric branch -- the row is
                              * itoa(anLineAux[row]), never apLines[row] (0x3EC08..0x3EC90). */
    i32  nUnk7076;
    u8   bRepeatArmed;  /* panel+0x1BA8: press-and-hold repeat armed; cleared when the counter
                         * reaches nRepeatDelay, so one hold fires one repeat (0x3EE50..0x3EEB8) */
    i32  nRepeatDelay;  /* panel+0x1BAC: the hold counter threshold; the shop/transfer row-panel
                         * builders store 5 */
    u8   bColorKey;     /* panel+0x1BB0: the key byte SoftSurf_BlitSrcRect takes (ctor 0xFF) */
    u8   bAuxPlusSign;       /* panel+0x1BB1: with bAuxNumbers and aux > 0 the number gets the
                              * leading "+" of asc_AAF90 before the itoa digits (0x3EC34..0x3EC6C). */
} UiPanel;

/* UiPanel_ctor @ 0x3e730.
 *
 * Allocates nLineCount line buffers of 50 bytes, each an empty string, and
 * applies the original's defaults: nFont 17, bRowRules 1, bSelLineHighlight 1, nStyle 1,
 * nViewW 240, nViewH 320, bColorKey 0xFF, everything else 0.
 *
 * nLineCount is clamped to the 500-line capacity. The original trusts its caller;
 * the clamp is a deviation listed in the parity notes. Returns false only when a
 * line buffer could not be allocated. */
bool UiPanel_ctor(UiPanel *pPanel, i32 nLineCount);

/* UiPanel_FreeLines @ 0x3e8c8. Frees every line buffer. */
void UiPanel_FreeLines(UiPanel *pPanel);

/* Style setters. Each writes the exact aStyleRgb byte slots the original touches. */
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
 * via SoftSurf_BlitRectProxy and SoftSurf_BlitSrcRect (both ported in pufo_render.c).
 * The panel engine does not call them -- it renders the rows only. That is the part
 * the panel engine itself owns. */
void UiPanel_DrawText(UiPanel *pPanel);

/* UiScrollList_ClickRow @ 0x3eee0.
 *
 * Computes the clicked row as `(nY - nPosY) / nFont`, stores it in `nHoverRow` (the dword at
 * panel offset 12) and clears `nHoldCount`, `bHoverActive` and the global `unk_158018`.
 * `nUnk3028` is READ, never written: ARM 0x3EEE0..0x3F000 writes only unk_158018,
 * 0xBC8, 0xC and 0xBD0, and reaches 0xBD4 only as the left side of the sum.
 * When `bSelLineHighlight` is set and the row is within `nStyle`/`nLineCount`, and (nX, nY) is
 * strictly inside the rect at (`nRectX`, `nRectY`) sized `nViewW` x `nViewH`, it
 * sets `nUnk3032 = nUnk3028 + row` and calls `pfnClick`. Returns the row index. */
i32 UiPanel_ClickRow(UiPanel *pPanel, i32 nX, i32 nY);

/* UiScrollList_TrackHold @ 0x3ed44, the pointer-move half of the same list.
 *
 * It computes the row the same way and stores it in `nHoverRow`, but it does NOT clear
 * `nHoldCount` or `unk_158018` on entry. Outside the rect, or past `nStyle`/`nLineCount`,
 * it clears `bHoverActive` and `nHoldCount`. Inside, it sets `bHoverActive` and then runs the
 * press-and-hold repeat: only while `bRepeatArmed` is set does it count, it resets the
 * counter whenever the hovered row differs from the global `unk_158018`, and when
 * `nHoldCount` reaches `nRepeatDelay` it clears `bRepeatArmed`, clamps `nHoldCount` to that
 * threshold, sets `nUnk3032 = nUnk3028 + row` and calls `pfnClick`. So a held pointer
 * auto-repeats the row click once per `nRepeatDelay` calls, and moving to another row
 * restarts the count. Returns the row index. */
i32 UiPanel_TrackHoldRow(UiPanel *pPanel, i32 nX, i32 nY);

/* Ui_SwapStringSlots @ 0x6aaac. Swaps two lines' text pointers' contents and their
 * anLineAux values. The original copies through a 200-byte stack buffer, which the
 * port keeps as the same bound. */
bool UiPanel_SwapLines(UiPanel *pPanel, i32 nIdxA, i32 nIdxB);

/* Cydonia_Blit_Then_UiPanelMsg_Draw @ 0x1a7a0.
 *
 * The entire handler for screens 0x14 and 0x15: blit one pad, then draw one message panel. The
 * original reaches both through globals - a pad POINTER stored at 0x15692C and a panel at 0x1571EC
 * - and the port passes them. It lives here rather than in its own module because it is two calls
 * and nothing else; the Cydonia briefing screens reach it from the Geoscape_Draw arms wired in
 * pufo_world.c. */
void Cydonia_BlitThenDrawPanel(PadImage *pPad, UiPanel *pPanel);

/* ------------------------------------------------- the animated panel frame
 *
 * Evidence: Ui_AnimExpandBlit @ 0x95bf0 (948 B) and Ui_AnimExpandBlit_IfEnabled @ 0x663c4 (88 B).
 * Sixteen call sites: BaseFacScreen_DrawBuildDialog, RadarScreen_Update, Geoscape_Draw,
 * Geoscape_DrawScreen, Geoscape_DrawGlobeHover, PsiScreen_Draw, ManufScreen_DrawFrame,
 * BuySellScreen_Draw, BasePlaneScreen_DrawCraftDetail, ResearchScreen_Draw, Ui_TextInputDialog and
 * the wrapper.
 *
 * The animation state is two globals: the byte at 0x187080 says whether the animation has started
 * (the first call sets it and zeroes the progress), and the SIGNED byte at 0x187058 is the 0..100
 * progress. nMode picks how the rect grows:
 *
 *   0: the full rect, x/y/nW/nH unchanged.
 *   1: a 50*progress square anchored at (x, y).
 *   2: full width, height 50*progress centred: y becomes nH/2 - 25*progress.
 *   3: width 50*progress, full height, anchored at (x, y).
 *   4: a 50*progress square centred: x becomes nW/2 - 25*progress, y becomes nH/2 - 25*progress.
 *
 * The width and height are then clamped to nW / nH and the origin back up to x / y. When all four
 * match the full rect the progress byte is set to 100 -- the animation is only "done" once the
 * clamped rect is exactly the requested one. SoftSurf_BlitSrcRect draws the panel body, and eight
 * SoftSurf_BlitRectProxy calls draw the bevelled frame: the outer bars at (x, y, w+4, 6),
 * (x, y+h, w+4, 6), (x, y+4, 6, h-2), (x+w-2, y+4, 6, h-2) and the inner bars at (x+2, y+2, w, 2),
 * (x+2, y+h+2, w, 2), (x+2, y+2, 2, h), (x+w, y+2, 2, h).
 *
 * The frame colours come from the byte at root + 0x6E (0x186566): 1 gives the bright frame
 * (outer 40,120,200 / inner 60,150,220) and 0 or 2 the ordinary one (outer 10,80,90 / inner
 * 20,90,100). Returns whether the progress byte is 100.
 *
 * Deviations: a mode outside 0..4 or a style byte of 3 or more reads an uninitialised stack slot in
 * the original; the port falls back to the full rect and the ordinary frame. */
typedef struct UiAnimBlitEnv {
    u8 *pbStarted;   /* byte_187080: the "the animation has already started" flag */
    i8 *pnProgress;  /* the signed byte at 0x187058, the 0..100 progress */
    i8  nFrameStyle; /* the byte at 0x186566 (root + 0x6E) */
} UiAnimBlitEnv;

bool Ui_AnimExpandBlit(const UiAnimBlitEnv *pEnv, i32 x, i32 y, i32 nW, i32 nH,
                       u8 nIdx, u8 nSrcX, u8 nSrcY, u8 nKey, i8 nMode);

/* Ui_AnimExpandBlit_IfEnabled @ 0x663c4: the fixed-parameter wrapper -- panel index 4, source
 * origin 0x46/0x4B, key 0xC8, mode 0 -- and only when bEnabled is exactly 1. The original's first
 * argument (a grid pointer) is never read. Returns bEnabled when it does not draw. */
i32 Ui_AnimExpandBlit_IfEnabled(const UiAnimBlitEnv *pEnv, i32 x, i32 y, i32 nW, i32 nH,
                                i8 bEnabled);

#endif /* PUFO_PANEL_H */
