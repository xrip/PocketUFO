/* test_panel.c - checks the shared UI panel line engine.
 *
 * The layout is the thing to pin: UiPanel is 7092 bytes and the offsets are proven
 * (see pufo_panel.h). The scroll arithmetic and the wrap behaviour are the two
 * pieces of logic most likely to be "tidied" later.
 */
#include "asset/pufo_asset.h"
#include "ui/pufo_panel.h"
#include "ui/pufo_linelist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static void TestLayout(void)
{
    UiPanel panel;

    printf("UiPanel layout\n");
    /* Fields before the first pointer are at fixed offsets on every target. */
    Check(offsetof(UiPanel, aStyleRgb) == 0x0000, "  aStyleRgb at 0x0000");
    Check(offsetof(UiPanel, aLineMeta) == 0x0010, "  aLineMeta at 0x0010");
    /* aLineMeta spans 500*6 = 3000 bytes, so nHoldCount follows with no padding on
     * either target. Everything from pfnClick onward holds pointers and shifts on
     * Win64, so those absolute offsets are asserted only for ILP32. */
    Check(offsetof(UiPanel, nHoldCount) == 0x0BC8, "  nHoldCount at 0x0BC8");

    /* The scalar run from bHoverActive to nStyle keeps its relative layout on both
     * targets, which is where a field-order mistake would show. */
    Check(offsetof(UiPanel, bHoverActive) < offsetof(UiPanel, nUnk3028), "  bHoverActive before nUnk3028");
    Check(offsetof(UiPanel, nUnk3028) < offsetof(UiPanel, nUnk3032), "  nUnk3028 before nUnk3032");
    Check(offsetof(UiPanel, nUnk3032) < offsetof(UiPanel, nLineCount), "  nUnk3032 before nLineCount");
    Check(offsetof(UiPanel, nLineCount) < offsetof(UiPanel, nStyle), "  nLineCount before nStyle");
    Check(offsetof(UiPanel, nStyle) < offsetof(UiPanel, apLines), "  nStyle before apLines");

#if UINTPTR_MAX == 0xFFFFFFFFu
    /* The rest holds pointers, so the Win64 struct is larger and the tail shifts.
     * The ILP32 offsets are the ones the IDB reports, so assert them there only. */
    Check(offsetof(UiPanel, pfnClick) == 0x0BCC, "  pfnClick at 0x0BCC (ILP32)");
    Check(offsetof(UiPanel, bHoverActive) == 0x0BD0, "  bHoverActive at 0x0BD0 (ILP32)");
    Check(offsetof(UiPanel, nUnk3028) == 0x0BD4, "  nUnk3028 at 0x0BD4 (ILP32)");
    Check(offsetof(UiPanel, nLineCount) == 0x0BDC, "  nLineCount at 0x0BDC (ILP32)");
    Check(offsetof(UiPanel, nStyle) == 0x0BE0, "  nStyle at 0x0BE0 (ILP32)");
    Check(offsetof(UiPanel, apLines) == 0x0BE4, "  apLines at 0x0BE4 (ILP32)");
    Check(offsetof(UiPanel, nPosX) == 0x1B84, "  nPosX at 0x1B84 (ILP32)");
    Check(offsetof(UiPanel, nViewW) == 0x1B94, "  nViewW at 0x1B94 (ILP32)");
    Check(offsetof(UiPanel, nFont) == 0x1B9C, "  nFont at 0x1B9C (ILP32)");
    Check(offsetof(UiPanel, bAuxPlusSign) == 0x1BB1, "  bAuxPlusSign at 0x1BB1 (ILP32)");
    Check(sizeof(UiPanel) >= 0x1BB1 + 1, "  struct covers the last field");
#else
    /* On Win64 the ordering is what matters. Assert the relative layout of the
     * scalar tail, which is where a field-order mistake would show up. */
    Check(offsetof(UiPanel, nPosX) < offsetof(UiPanel, nPosY), "  nPosX before nPosY");
    Check(offsetof(UiPanel, nPosY) < offsetof(UiPanel, nRectX), "  nPosY before nRectX");
    Check(offsetof(UiPanel, nRectY) < offsetof(UiPanel, nViewW), "  nRectY before nViewW");
    Check(offsetof(UiPanel, nViewW) < offsetof(UiPanel, nViewH), "  nViewW before nViewH");
    Check(offsetof(UiPanel, nViewH) < offsetof(UiPanel, nFont), "  nViewH before nFont");
    Check(offsetof(UiPanel, nFont) < offsetof(UiPanel, bRowRules), "  nFont before bRowRules");
    Check(offsetof(UiPanel, bAuxPlusSign) == offsetof(UiPanel, bColorKey) + 1,
          "  bAuxPlusSign directly after bColorKey");
#endif

    /* The apLines-to-clock aliasing is fixed arithmetic on 32-bit pointers. On
     * Win64 the entries widen, so the alias cannot hold; the port deliberately
     * does not model it (see the header). Assert the ILP32 form as documentation
     * of why the original's clock access worked. */
#if UINTPTR_MAX == 0xFFFFFFFFu
    Check(0x0BE4 + 4 * 105 == 0x0D88, "  apLines[105] lands at +0x0D88 (ILP32)");
    Check(0x0BE4 + 4 * 106 == 0x0D8C, "  apLines[106] lands at +0x0D8C (ILP32)");
#endif
    (void)panel;
}

static void TestCtor(void)
{
    UiPanel panel;
    i32     i;

    printf("UiPanel_ctor\n");
    Check(UiPanel_ctor(&panel, 10), "  constructed with 10 lines");
    CheckEqI32(panel.nLineCount, 10, "  line count stored");
    /* Defaults from UiPanel_ctor @ 0x3e730. */
    CheckEqI32(panel.nFont, 17, "  nFont 17");
    CheckEqI32(panel.bRowRules, 1, "  bRowRules set");
    CheckEqI32(panel.bSelLineHighlight, 1, "  bSelLineHighlight set");
    CheckEqI32(panel.nStyle, 1, "  nStyle 1");
    CheckEqI32(panel.nViewW, 240, "  nViewW 240");
    CheckEqI32(panel.nViewH, 320, "  nViewH 320");
    CheckEqI32(panel.bColorKey, 0xFF, "  bColorKey 0xFF");
    CheckEqI32(panel.nUnk3028, 0, "  scroll cursor 0");

    {
        bool bAllocated = true;

        for (i = 0; i < 10; ++i) {
            if (panel.apLines[i] == NULL) {
                bAllocated = false;
            }
        }
        Check(bAllocated, "  all 10 line buffers allocated");
        Check(strlen((const char *)panel.apLines[0]) == 0, "  lines start empty");
    }

    /* The capacity clamp is a documented deviation. */
    printf("UiPanel_ctor clamps above the 500-line capacity\n");
    Check(UiPanel_ctor(&panel, 600), "  constructed with 600 requested");
    CheckEqI32(panel.nLineCount, PUFO_PANEL_LINES, "  clamped to 500");
    UiPanel_FreeLines(&panel);

    Check(UiPanel_ctor(&panel, 0), "  zero lines is valid");
    CheckEqI32(panel.nLineCount, 0, "  line count 0");
    UiPanel_FreeLines(&panel);
}

static void TestLinesAndMeta(void)
{
    UiPanel panel;
    u8      r;
    u8      g;
    u8      b;

    printf("Line text and meta\n");
    Check(UiPanel_ctor(&panel, 4), "  constructed");

    Check(UiPanel_SetLineText(&panel, 0, "hello"), "  set line 0");
    Check(strcmp(UiPanel_GetLineText(&panel, 0), "hello") == 0, "  line 0 reads back");
    Check(!UiPanel_SetLineText(&panel, 99, "x"), "  out-of-range line refused");
    Check(UiPanel_GetLineText(&panel, 99) == NULL, "  out-of-range read returns NULL");

    /* Over-long text is truncated rather than overflowing the 50-byte buffer. */
    {
        char szLong[300];

        memset(szLong, 'A', sizeof(szLong) - 1);
        szLong[sizeof(szLong) - 1] = '\0';
        Check(UiPanel_SetLineText(&panel, 1, szLong), "  long text accepted");
        Check(strlen(UiPanel_GetLineText(&panel, 1)) == PUFO_PANEL_LINE_BYTES - 1,
              "  text truncated to 49 bytes");
    }

    printf("UiPanel_SetLineMeta / GetLineMeta\n");
    /* UiPanel_SetLineMeta @ 0x3f06c writes the six meta bytes of one line: the decompiled form is
     * v8[17] = a4, v8[16] = a3, aLineMeta[6*nLine+2] = a5, then 19, 20, 21 -- scheduling only, the net
     * effect is a3..a8 landing on meta +0..+5 in order. */
    UiPanel_SetLineMeta(&panel, 2, 10, 20, 30, 40, 50, 60);
    Check(UiPanel_GetLineMeta(&panel, 2, 1, &r, &g, &b), "  triple 1 readable @ 0x3f06c");
    Check(r == 10 && g == 20 && b == 30, "  meta +0/+1/+2 hold a3,a4,a5 @ 0x3f06c");
    Check(UiPanel_GetLineMeta(&panel, 2, 2, &r, &g, &b), "  triple 2 readable");
    Check(r == 40 && g == 50 && b == 60, "  meta +3/+4/+5 hold a6,a7,a8 @ 0x3f06c");
    Check(!UiPanel_GetLineMeta(&panel, 2, 3, &r, &g, &b), "  invalid triple refused");
    /* Meta of line 0 must be untouched: the block is 6 bytes per line at +0x10. */
    Check(UiPanel_GetLineMeta(&panel, 0, 1, &r, &g, &b) && r == 0 && g == 0 && b == 0,
          "  line 0 meta untouched @ 0x3f06c");
    Check(UiPanel_GetLineMeta(&panel, 3, 2, &r, &g, &b) && r == 0 && g == 0 && b == 0,
          "  line 3 meta untouched @ 0x3f06c");

    printf("Style setters\n");
    /* Three setters, three disjoint slot ranges: SetBytes0to2 @ 0x3f05c, SetBytes3to5 @ 0x3e93c,
     * SetBytes6to11 @ 0x3e914. Each writes its own bytes and no others, in argument order. */
    UiPanel_SetBytes0to2(&panel, 1, 2, 3);
    UiPanel_SetBytes3to5(&panel, 4, 5, 6);
    UiPanel_SetBytes6to11(&panel, 7, 8, 9, 10, 11, 12);
    Check(panel.aStyleRgb[0] == 1 && panel.aStyleRgb[1] == 2 && panel.aStyleRgb[2] == 3,
          "  bytes 0..2 in argument order @ 0x3f05c");
    Check(panel.aStyleRgb[3] == 4 && panel.aStyleRgb[4] == 5 && panel.aStyleRgb[5] == 6,
          "  bytes 3..5 in argument order @ 0x3e93c");
    Check(panel.aStyleRgb[6] == 7 && panel.aStyleRgb[7] == 8 && panel.aStyleRgb[8] == 9
              && panel.aStyleRgb[9] == 10 && panel.aStyleRgb[10] == 11 && panel.aStyleRgb[11] == 12,
          "  all six of bytes 6..11 in argument order @ 0x3e914");
    UiPanel_SetBytes6to11(&panel, 0x16, 0xF4, 0xFC, 0x04, 0x46, 0x4B);
    Check(panel.aStyleRgb[6] == 0x16 && panel.aStyleRgb[9] == 0x04,
          "  the second call overwrites the same six slots @ 0x3e914");

    printf("Aux words\n");
    UiPanel_SetLineAux(&panel, 1, 4242);
    CheckEqI32(UiPanel_GetLineAux(&panel, 1), 4242, "  aux stored");
    CheckEqI32(UiPanel_GetLineAux(&panel, 0), 0, "  other aux untouched");

    /* UiPanel_FreeLines @ 0x3e8c8 walks nLineCount and not the capacity: 0x3E8D4 is the `count > 0`
     * guard and 0x3E900 is `while (v2 < pPanel->nLineCount)`, one delete per iteration. A pointer
     * parked past the count is therefore left alone -- that is the only way to tell the bound from a
     * loop over the 500-entry array. */
    panel.apLines[5] = malloc(8);
    UiPanel_FreeLines(&panel);
    Check(panel.apLines[0] == NULL && panel.apLines[3] == NULL,
          "  the four constructed lines are released @ 0x3e8c8");
    Check(panel.apLines[5] != NULL, "  FreeLines stops at nLineCount, not at the capacity @ 0x3e8c8");
    free(panel.apLines[5]);
    panel.apLines[5] = NULL;
    UiPanel_FreeLines(&panel);
    Check(panel.apLines[5] == NULL, "  the second call frees nothing twice @ 0x3e8c8");
}

/* ScrollNext is bounded by nLineCount - nStyle, which is the interesting part:
 * the window can never scroll past the last full page.
 *
 * Evidence: IncDwordIfInRange_AtPlus0xBD4 @ 0x3f00c -- `v = *(0xBD4); if (v < *(0xBDC) - *(0xBE0))
 * *(0xBD4) = v + 1` (the compare at 0x3F034, the store at 0x3F038) -- and
 * DecDwordIfPos_AtPlus0xBD4 @ 0x3f040 -- `if (v > 0) v - 1` (0x3F050 / 0x3F054). Both return the
 * object pointer, neither wraps, and the port keeps them as UiPanel_ScrollNext / UiPanel_ScrollPrev. */
static void TestScroll(void)
{
    UiPanel panel;
    i32     i;

    printf("UiPanel_ScrollNext / ScrollPrev\n");
    Check(UiPanel_ctor(&panel, 10), "  constructed with 10 lines");
    panel.nStyle = 3;

    /* ScrollPrev at the top is a no-op (the `v > 0` guard at 0x3F050). */
    UiPanel_ScrollPrev(&panel);
    CheckEqI32(panel.nUnk3028, 0, "  cannot scroll above 0 @ 0x99f88");

    /* The last valid cursor is nLineCount - nStyle == 7: the compare at 0x3F034 is `<`, so a cursor
     * already at the limit does not move. */
    for (i = 0; i < 20; ++i) {
        UiPanel_ScrollNext(&panel);
    }
    CheckEqI32(panel.nUnk3028, 7, "  clamped to nLineCount - nStyle @ 0x99f78");

    UiPanel_ScrollPrev(&panel);
    CheckEqI32(panel.nUnk3028, 6, "  scrolls back one");
    UiPanel_ScrollPrev(&panel);
    CheckEqI32(panel.nUnk3028, 5, "  scrolls back two");

    /* A style larger than the line count makes the limit negative, so scrolling
     * stays at 0. That is 0x3f00c's own arithmetic -- the subtraction at 0x3F034 is a signed dword
     * compare, not a special case. */
    printf("UiPanel_ScrollNext with nStyle > nLineCount\n");
    panel.nStyle = 20;
    panel.nUnk3028 = 0;
    UiPanel_ScrollNext(&panel);
    CheckEqI32(panel.nUnk3028, 0, "  negative limit keeps the cursor at 0");

    UiPanel_FreeLines(&panel);
}

static void TestWrapText(void)
{
    UiPanel panel;
    i32     nLines;

    printf("UiPanel_WrapText\n");
    Check(UiPanel_ctor(&panel, 40), "  constructed");

    /* Short text fits on one line. */
    nLines = UiPanel_WrapText(&panel, 200, 0, "short");
    CheckEqI32(nLines, 1, "  short text is one line");
    CheckEqI32(panel.nLineCount, 1, "  nLineCount set to the wrapped count");
    Check(strncmp(UiPanel_GetLineText(&panel, 0), "short", 5) == 0, "  line 0 holds the text");

    /* Long text wraps over several lines. */
    nLines = UiPanel_WrapText(&panel, 40, 0,
                              "alpha beta gamma delta epsilon zeta eta theta");
    Check(nLines > 1, "  long text wraps to more than one line");
    {
        bool bAllFilled = true;
        i32  i;

        for (i = 0; i < nLines; ++i) {
            const char *p = UiPanel_GetLineText(&panel, i);

            if (p == NULL || p[0] == '\0') {
                bAllFilled = false;
            }
        }
        if (!bAllFilled) {
            printf("       wrapped %d lines:\n", (int)nLines);
            for (i32 k = 0; k < nLines; ++k) {
                const char *q = UiPanel_GetLineText(&panel, k);

                printf("         [%d] %s\n", (int)k, q == NULL ? "(null)" : q);
            }
        }
        Check(bAllFilled, "  every produced line is non-empty");
    }

    /* Wrapping into a non-zero start line offsets the writes. */
    printf("UiPanel_WrapText with a start line\n");
    UiPanel_FreeLines(&panel);
    Check(UiPanel_ctor(&panel, 40), "  reconstructed");
    nLines = UiPanel_WrapText(&panel, 40, 5, "alpha beta gamma delta epsilon zeta");
    Check(nLines > 0, "  produced lines");
    Check(UiPanel_GetLineText(&panel, 0) == NULL || UiPanel_GetLineText(&panel, 0)[0] == '\0',
          "  line 0 untouched when starting at 5");
    Check(UiPanel_GetLineText(&panel, 5) != NULL && UiPanel_GetLineText(&panel, 5)[0] != '\0',
          "  line 5 holds the first wrapped line");
    CheckEqI32(panel.nLineCount, 5 + nLines, "  nLineCount = start + produced");

    /* Empty input produces no lines. */
    printf("UiPanel_WrapText with empty input\n");
    UiPanel_FreeLines(&panel);
    Check(UiPanel_ctor(&panel, 8), "  reconstructed");
    nLines = UiPanel_WrapText(&panel, 40, 0, "");
    CheckEqI32(nLines, 0, "  empty input gives zero lines");

    UiPanel_FreeLines(&panel);
}

static i32 g_nClicks;
static void RecClick(void) { g_nClicks += 1; }

static void TestClickRow(void)
{
    UiPanel panel;

    printf("UiPanel_ClickRow\n");
    Check(UiPanel_ctor(&panel, 10), "  constructed");
    panel.nPosY = 10;
    panel.nFont = 20;      /* row = (nY - 10) / 20 */
    panel.nRectX = 5;    /* rect left */
    panel.nViewW = 100;
    panel.nRectY = 10;   /* rect top */
    panel.nViewH = 50;
    panel.nStyle = 3;
    panel.nLineCount = 10;
    panel.bSelLineHighlight = 1;
    panel.nUnk3028 = 3;    /* the scroll cursor; ClickRow only reads it */
    g_nClicks = 0;

    /* Inside the rect: row 2, callback fires. */
    panel.pfnClick = RecClick;
    /* UiScrollList_ClickRow @ 0x3eee0: two clears first (0x3EEF8 the global hover row, 0x3EEFC the
     * panel's +0xBC8), then row = (nY - [panel+0x1B88]) / [panel+0x1B9C] through _rt_sdiv at
     * 0x3EF24..0x3EF30, the row stored at panel+0xC (0x3EF38) and byte 0xBD0 cleared (0x3EF40). The
     * cursor at 0xBD4 is only READ, at 0x3EF4C on, to seed 0xBD8. */
    CheckEqI32(UiPanel_ClickRow(&panel, 20, 50), 2, "  inside click row is 2 @ 0x3eee0");
    CheckEqI32(panel.nUnk3028, 3, "  the cursor at +0xBD4 is read, never written @ 0x3eee0");
    CheckEqI32(panel.nUnk3032, 5, "  +0xBD8 = the cursor + row @ 0x3eee0");
    CheckEqI32(g_nClicks, 1, "  the tail call runs the handler @ 0x3eee0");
    Check(panel.pfnClick != NULL, "  callback installed");

    /* Outside the x range: no callback, but the row is still stored (0x3EF38 and 0x3EF40 run before
     * any of the seven gates at 0x3EF44..0x3EFCC). */
    g_nClicks = 0;
    CheckEqI32(UiPanel_ClickRow(&panel, 1, 50), 2, "  outside x still returns the row @ 0x3eee0");
    CheckEqI32(g_nClicks, 0, "  ... and the handler does not run @ 0x3eee0");

    /* Above the rect: row 0 (the division truncates toward zero) and no callback. */
    CheckEqI32(UiPanel_ClickRow(&panel, 20, 5), 0, "  above the rect row 0 @ 0x3eee0");

    /* bSelLineHighlight ([panel+0x1BA1], read at 0x3EF1C) disables the handler but not the row. */
    panel.bSelLineHighlight = 0;
    CheckEqI32(UiPanel_ClickRow(&panel, 20, 50), 2, "  disabled flag still returns the row @ 0x3eee0");
    CheckEqI32(g_nClicks, 0, "  ... and still no handler @ 0x3eee0");

    UiPanel_FreeLines(&panel);
}

static void TestTrackHold(void)
{
    UiPanel panel;

    printf("UiPanel_TrackHoldRow\n");
    Check(UiPanel_ctor(&panel, 10), "  constructed");
    panel.nPosY = 10;
    panel.nFont = 20;      /* row = (nY - 10) / 20 */
    panel.nRectX = 5;
    panel.nViewW = 100;
    panel.nRectY = 10;
    panel.nViewH = 50;
    panel.nStyle = 3;
    panel.nLineCount = 10;
    panel.bSelLineHighlight = 1;
    panel.bRepeatArmed = 1;    /* hold repeat armed */
    panel.nRepeatDelay = 3;    /* threshold */
    panel.nUnk3028 = 4;    /* scroll cursor */
    panel.pfnClick = RecClick;
    g_nClicks = 0;

    /* Outside the rect: hover flag and counter both clear, no click. */
    CheckEqI32(UiPanel_TrackHoldRow(&panel, 1, 50), 2, "  outside x still returns row 2");
    CheckEqI32(panel.bHoverActive, 0, "  outside clears bHoverActive");
    CheckEqI32(panel.nHoldCount, 0, "  outside clears the repeat counter");
    CheckEqI32(g_nClicks, 0, "  outside never clicks");

    /* Inside: hover set, counting, nothing fires until the threshold. */
    CheckEqI32(UiPanel_TrackHoldRow(&panel, 20, 50), 2, "  inside row 2");
    CheckEqI32(panel.bHoverActive, 1, "  inside sets bHoverActive");
    CheckEqI32(panel.nHoldCount, 1, "  first hold tick");
    CheckEqI32(g_nClicks, 0, "  no click below the threshold");
    (void)UiPanel_TrackHoldRow(&panel, 20, 50);
    CheckEqI32(panel.nHoldCount, 2, "  second hold tick");
    CheckEqI32(g_nClicks, 0, "  still below the threshold");
    (void)UiPanel_TrackHoldRow(&panel, 20, 50);
    CheckEqI32(panel.nHoldCount, 3, "  counter clamped to the threshold");
    CheckEqI32(g_nClicks, 1, "  the threshold fires the click once");
    CheckEqI32(panel.nUnk3032, 6, "  nUnk3032 = nUnk3028 + row");
    CheckEqI32(panel.bRepeatArmed, 0, "  bRepeatArmed clears after firing");

    /* With the hold flag clear it hovers but never repeats. */
    g_nClicks = 0;
    (void)UiPanel_TrackHoldRow(&panel, 20, 50);
    (void)UiPanel_TrackHoldRow(&panel, 20, 50);
    CheckEqI32(panel.nHoldCount, 3, "  counter keeps its clamped value; only leaving the rect or changing row clears it");
    CheckEqI32(panel.bHoverActive, 1, "  hover flag still set");
    CheckEqI32(g_nClicks, 0, "  no repeat without bRepeatArmed");

    /* Moving to another row restarts the count. */
    panel.bRepeatArmed = 1;
    panel.nHoldCount = 0;
    g_nClicks = 0;
    CheckEqI32(UiPanel_TrackHoldRow(&panel, 20, 30), 1, "  row 1 at nY 30");
    CheckEqI32(panel.nHoldCount, 1, "  row 1 counts 1");
    (void)UiPanel_TrackHoldRow(&panel, 20, 50);
    CheckEqI32(panel.nHoldCount, 1, "  changing row restarts the counter");

    /* bSelLineHighlight clear disables everything but the row itself. */
    panel.bSelLineHighlight = 0;
    CheckEqI32(UiPanel_TrackHoldRow(&panel, 20, 50), 2, "  disabled flag still returns row");
    CheckEqI32(panel.bHoverActive, 1, "  disabled flag leaves bHoverActive alone");

    UiPanel_FreeLines(&panel);
}

static i32 g_nLineSample;
static void Line_Sample(i32 nSample, void *pUser) { (void)pUser; g_nLineSample = nSample; }

static void TestLineList(void)
{
    UiLineList list;

    printf("UiLineList (the second structure IDA merges under UiPanel)\n");

    /* The layout the six functions prove: 200 flag bytes, then 200 16-byte records,
     * then the count byte at 3400. */
    CheckEqI32((i32)offsetof(UiLineList, abLineFlags), 0, "  flags at +0");
    CheckEqI32((i32)offsetof(UiLineList, anColor), 200, "  records at +0xC8 (200)");
    CheckEqI32((i32)offsetof(UiLineList, nLineCount), PUFO_LINELIST_COUNT_OFF,
               "  count at +0xD48 (3400): the LDRB at 0x5A2A0, the increment's STRB at 0x5A32C @ 0x5a294");
    CheckEqI32((i32)sizeof(UiLineList), 3404,
               "  3401 bytes of fields, padded to 3404 by the u32 record array");

    memset(&list, 0, sizeof(list));

    /* Append is an insert at the current count, so nothing shifts. */
    g_nLineSample = 0;
    UiLineList_AppendWithSound(&list, 11, 100, 101, 102, 103, Line_Sample, NULL);
    UiLineList_AppendWithSound(&list, 12, 200, 201, 202, 203, Line_Sample, NULL);
    /* UiPanel_AppendLineWithSound @ 0x5a394 is `UiPanel_InsertLine(base, [base+3400], ...)` followed by
     * `Sound_Play(7)` -- the append index is the count, which is why no shift ever runs here. */
    CheckEqI32((i32)list.nLineCount, 2, "  two appends @ 0x5a394");
    CheckEqI32(g_nLineSample, 7, "  append plays sample 7 @ 0x5a394");
    CheckEqI32(UiLineList_GetFlag(&list, 1), 12, "  line 1 flag @ 0x5a394");
    CheckEqI32((i32)UiLineList_GetColor(&list, 0, 2), 102, "  line 0 colour slot 2 @ 0x5a33c");
    CheckEqI32((i32)UiLineList_GetColor(&list, 1, 3), 203, "  line 1 colour slot 3, the last one @ 0x5a33c");
    CheckEqI32((i32)UiLineList_GetColor(&list, 1, 4), 0, "  slot 4 reads zero @ 0x5a33c");
    CheckEqI32((i32)UiLineList_GetColor(&list, 1, -1), 0, "  a negative slot reads zero @ 0x5a33c");

    /* Insert at the front shifts everything up one place. UiPanel_InsertLine @ 0x5a294 gates the shift
     * on `count > nLine` (CMP at 0x5A2A8, BLE to the write block at 0x5A2AC) and walks the shift from
     * the top end downwards: 0x5A2B0 puts the pointer at index `count`, 0x5A2B4 at record `count`,
     * 0x5A2BC reads the previous flag with LDRSB and 0x5A2CC stores it, and 0x5A2D4/0x5A2DC move the
     * 16-byte colour record one place up (R9 = 0x10 at 0x5A2C8 is the record length). */
    UiLineList_Insert(&list, 0, 9, 900, 901, 902, 903);
    CheckEqI32((i32)list.nLineCount, 3, "  insert increments the count byte @ 0x5a294");
    CheckEqI32(UiLineList_GetFlag(&list, 0), 9, "  the new line is at 0");
    CheckEqI32(UiLineList_GetFlag(&list, 1), 11, "  the old line 0 moved to 1");
    CheckEqI32(UiLineList_GetFlag(&list, 2), 12, "  the old line 1 moved to 2");
    CheckEqI32((i32)UiLineList_GetColor(&list, 1, 0), 100, "  the whole record moved @ 0x5a294");
    CheckEqI32((i32)UiLineList_GetColor(&list, 1, 3), 103, "  including its last slot @ 0x5a294");
    CheckEqI32((i32)UiLineList_GetColor(&list, 2, 0), 200, "  and so did the next one @ 0x5a294");
    /* Every slot is truncated to a byte at the write: slot 0 by `AND R3,R3,#0xFF` at 0x5A2FC,
     * slots 1..3 by the LDRB of their stack arguments at 0x5A30C..0x5A31C. */
    CheckEqI32((i32)UiLineList_GetColor(&list, 0, 0), 132, "  slot 0 keeps 900 & 0xFF @ 0x5a294");
    CheckEqI32((i32)UiLineList_GetColor(&list, 0, 1), 133, "  slot 1 keeps 901 & 0xFF @ 0x5a294");
    CheckEqI32((i32)UiLineList_GetColor(&list, 0, 2), 134, "  slot 2 keeps 902 & 0xFF @ 0x5a294");
    CheckEqI32((i32)UiLineList_GetColor(&list, 0, 3), 135, "  slot 3 keeps 903 & 0xFF @ 0x5a294");

    /* SetLine overwrites in place and leaves the count alone -- the same write block as InsertLine
     * without the shift: flag STRB at 0x5A454, slot 0 masked at 0x5A458, slots 1..3 through LDRB at
     * 0x5A468, 0x5A470, 0x5A478, and no count byte touched anywhere in 0x5A450..0x5A484. */
    UiLineList_SetLine(&list, 1, 77, 1, 2, 3, 4);
    CheckEqI32((i32)list.nLineCount, 3, "  SetLine does not touch the count @ 0x5a450");
    CheckEqI32(UiLineList_GetFlag(&list, 1), 77, "  flag replaced @ 0x5a450");
    CheckEqI32((i32)UiLineList_GetColor(&list, 1, 1), 2, "  colours replaced @ 0x5a450");
    CheckEqI32((i32)UiLineList_GetColor(&list, 1, 0), 1, "  slot 0 replaced through its byte mask @ 0x5a450");
    UiLineList_SetLine(&list, 3, 78, 0x1FF, 0x102, 0x103, 0x104);
    CheckEqI32((i32)UiLineList_GetColor(&list, 3, 0), 0xFF, "  slot 0 truncated to a byte @ 0x5a450");
    CheckEqI32((i32)UiLineList_GetColor(&list, 3, 3), 4, "  slot 3 truncated to a byte @ 0x5a450");
    CheckEqI32((i32)UiLineList_GetColor(&list, 0, 0), 132, "  neighbours untouched @ 0x5a450");

    /* Remove shifts the tail down. */
    UiLineList_Remove(&list, 0);
    CheckEqI32((i32)list.nLineCount, 2, "  remove decrements the count");
    CheckEqI32(UiLineList_GetFlag(&list, 0), 77, "  line 1 became line 0");
    CheckEqI32(UiLineList_GetFlag(&list, 1), 12, "  line 2 became line 1");
    CheckEqI32((i32)UiLineList_GetColor(&list, 0, 3), 4, "  its record moved with it");

    /* The decrement is guarded, so an empty list stays empty. */
    UiLineList_Remove(&list, 0);
    UiLineList_Remove(&list, 0);
    UiLineList_Remove(&list, 0);
    CheckEqI32((i32)list.nLineCount, 0, "  removing from an empty list keeps it at 0");

    /* Inserting past the end leaves a gap and still counts the line, which is what
     * the original does. */
    memset(&list, 0, sizeof(list));
    UiLineList_Insert(&list, 5, 55, 5, 5, 5, 5);
    CheckEqI32((i32)list.nLineCount, 1, "  an out-of-range insert still increments");
    CheckEqI32(UiLineList_GetFlag(&list, 5), 55, "  and writes the slot it was given");
    CheckEqI32(UiLineList_GetFlag(&list, 0), 0, "  leaving the earlier slots empty");

    /* NULL is tolerated everywhere. */
    UiLineList_Insert(NULL, 0, 1, 1, 1, 1, 1);
    UiLineList_SetLine(NULL, 0, 1, 1, 1, 1, 1);
    UiLineList_Remove(NULL, 0);
    CheckEqI32(UiLineList_GetFlag(NULL, 0), 0, "  GetFlag on NULL reads 0");
    CheckEqI32((i32)UiLineList_GetColor(NULL, 0, 0), 0, "  GetColor on NULL reads 0");
    UiLineList_AppendWithSound(NULL, 1, 1, 1, 1, 1, Line_Sample, NULL);
    Check(true, "  NULL list is safe");
}

static void TestSwapLines(void)
{
    UiPanel panel;

    printf("UiPanel_SwapLines\n");
    Check(UiPanel_ctor(&panel, 4), "  constructed");
    UiPanel_SetLineText(&panel, 0, "alpha");
    UiPanel_SetLineText(&panel, 1, "bravo");
    UiPanel_SetLineAux(&panel, 0, 11);
    UiPanel_SetLineAux(&panel, 1, 22);

    /* Ui_SwapStringSlots @ 0x6aaac swaps a string slot AND its aux dword. The strings are the char*
     * entries at table + 4*nIdx + 3044 and the aux dwords are at table + 4*(nIdx + 1261) (= +5044);
     * the original copies A into a 200-byte stack buffer (A loaded at 0x6AAC0, copied to 0x6AAD8), reads
     * A's aux at 0x6AAEC, copies B over A in place (0x6AAF4..0x6AB08), reads B's aux at 0x6AB18 and
     * stores it into A at 0x6AB24, then writes the buffer into B (0x6AB2C..0x6AB44) and A's saved aux
     * into B at 0x6AB4C -- the buffer is what lets two char* entries swap without losing A. */
    Check(UiPanel_SwapLines(&panel, 0, 1), "  swapped @ 0x6aaac");
    Check(strcmp(UiPanel_GetLineText(&panel, 0), "bravo") == 0, "  line 0 is bravo @ 0x6aaac");
    Check(strcmp(UiPanel_GetLineText(&panel, 1), "alpha") == 0, "  line 1 is alpha @ 0x6aaac");
    CheckEqI32(UiPanel_GetLineAux(&panel, 0), 22, "  aux 0 takes B's aux dword @ 0x6aaac");
    CheckEqI32(UiPanel_GetLineAux(&panel, 1), 11, "  aux 1 takes A's aux dword @ 0x6aaac");

    /* Swapping with itself is stable. */
    Check(UiPanel_SwapLines(&panel, 0, 0), "  self swap accepted");
    Check(strcmp(UiPanel_GetLineText(&panel, 0), "bravo") == 0, "  self swap keeps text");

    Check(!UiPanel_SwapLines(&panel, 0, 99), "  out-of-range refused");
    Check(!UiPanel_SwapLines(NULL, 0, 1), "  NULL panel refused");

    UiPanel_FreeLines(&panel);
}

/* Cydonia_BlitThenDrawPanel @ 0x1a7a0 is the whole handler for screens 0x14 and 0x15: one pad
 * blit then one panel draw. What is worth asserting is that both halves really run and that the
 * panel half does not consume panel state, since the original calls this every frame. */
static void TestCydoniaHandler(void)
{
    SoftSurf  surf;
    PadImage  pad;
    UiPanel   panel;
    static u8 aGlyph[4 + 8];   /* a 4x2 glyph: two u16 header fields then the pixels */
    u16      *pPix;

    printf("Cydonia_BlitThenDrawPanel @ 0x1a7a0\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &surf;

    /* Glyph pixels are indices into the palette, so give the slot this pad uses a non-zero colour
     * for every index; otherwise "something was drawn" is indistinguishable from an empty surface. */
    memset(g_aPalette[30], 0x7F, sizeof(g_aPalette[30]));

    aGlyph[0] = 4;  aGlyph[1] = 0;
    aGlyph[2] = 2;  aGlyph[3] = 0;
    for (i32 i = 0; i < 8; ++i) { aGlyph[4 + i] = 1; }

    PadImage_ctor(&pad);
    pad.nOriginX = 40;
    pad.nOriginY = 60;
    pad.uWidth   = 4;
    pad.uHeight  = 2;
    pad.pData    = aGlyph;
    pad.nMode    = 30;

    Check(UiPanel_ctor(&panel, 3), "  panel constructed");
    panel.nPosX = 100;
    panel.nPosY = 200;

    Cydonia_BlitThenDrawPanel(&pad, &panel);

    pPix = (u16 *)surf.pPixels;
    Check(pPix[60 * 240 + 40] != 0, "  the pad half drew at the pad origin");
    Check(pPix[61 * 240 + 43] != 0, "  and covers the whole 4x2 glyph");
    CheckEqI32((i32)panel.nLineCount, 3, "  the panel half left the line count alone");
    CheckEqI32((i32)panel.nUnk3028, 0, "  and left the scroll position alone");

    g_pMainSurface = NULL;
}

/* Ui_AnimExpandBlit @ 0x95bf0 and Ui_AnimExpandBlit_IfEnabled @ 0x663c4. The state machine is the
 * interesting half: the started byte, the 0..100 progress, and the fact that the animation only
 * reports "done" once the clamped rect is exactly the requested one. The geometry is checked through
 * where the frame lands on the surface. */
static void TestAnimExpandBlit(void)
{
    SoftSurf surf;
    UiAnimBlitEnv env;
    static u8 bStarted;
    static i8 nProgress;
    u16 *pPix;
    u16 uCentrePlain;
    u16 uCentreBright;

    printf("Ui_AnimExpandBlit @ 0x95bf0\n");

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 240, 320, PIXMODE_16BPP);
    memset(surf.pPixels, 0, (size_t)240 * 320u * 2u);
    g_pMainSurface = &surf;
    g_blitState.nPixelMode = 2;
    pPix = (u16 *)surf.pPixels;

    bStarted = 0;
    nProgress = 0;
    memset(&env, 0, sizeof(env));
    env.pbStarted = &bStarted;
    env.pnProgress = &nProgress;
    env.nFrameStyle = 0;

    /* Mode 4 with no progress is a zero-size square at the centre of the rect. */
    Check(!Ui_AnimExpandBlit(&env, 0, 0, 200, 100, 4, 0x46, 0x4B, 0xC8, 4),
          "          progress 0 is not the finished frame");
    CheckEqI32(bStarted, 1, "          the first call raises the started byte (0x95C14)");
    CheckEqI32(nProgress, 0, "          and zeroes the progress");
    Check(pPix[50 * 240 + 100] != 0, "          mode 4 draws at (nW/2, nH/2) = (100, 50)");
    Check(pPix[0] == 0, "          with nothing at the rect origin");
    uCentrePlain = pPix[50 * 240 + 100];

    /* Half the progress overshoots, so the clamps pull it back to the full rect and the animation
     * reports done. */
    nProgress = 50;
    Check(Ui_AnimExpandBlit(&env, 0, 0, 200, 100, 4, 0x46, 0x4B, 0xC8, 4),
          "          progress 50 clamps to the full rect and reports done (0x95D60)");
    CheckEqI32(nProgress, 100, "          the progress byte goes to 100 (0x95D74)");

    /* Mode 1 at progress 1 is a 50x50 square anchored at the origin: not the full rect. */
    nProgress = 1;
    Check(!Ui_AnimExpandBlit(&env, 10, 20, 200, 100, 4, 0x46, 0x4B, 0xC8, 1),
          "          mode 1 at progress 1 is 50x50, not the full rect");
    CheckEqI32(nProgress, 1, "          so the progress is left alone");

    /* Mode 0 is the full rect whatever the progress. */
    Check(Ui_AnimExpandBlit(&env, 10, 20, 200, 100, 4, 0x46, 0x4B, 0xC8, 0),
          "          mode 0 is the full rect (0x95D08)");
    CheckEqI32(nProgress, 100, "          and that reports done");

    /* The frame colour comes from the style byte at root + 0x6E. */
    memset(surf.pPixels, 0, (size_t)240 * 320u * 2u);
    nProgress = 0;
    env.nFrameStyle = 0;
    (void)Ui_AnimExpandBlit(&env, 0, 0, 200, 100, 4, 0x46, 0x4B, 0xC8, 4);
    uCentrePlain = pPix[50 * 240 + 100];
    memset(surf.pPixels, 0, (size_t)240 * 320u * 2u);
    nProgress = 0;
    env.nFrameStyle = 1;
    (void)Ui_AnimExpandBlit(&env, 0, 0, 200, 100, 4, 0x46, 0x4B, 0xC8, 4);
    uCentreBright = pPix[50 * 240 + 100];
    Check(uCentrePlain != 0 && uCentreBright != 0 && uCentrePlain != uCentreBright,
          "          style 1 draws the bright frame, style 0 the ordinary one (0x95DE4)");

    /* The wrapper only draws when bEnabled is exactly 1. */
    nProgress = 50;
    CheckEqI32(Ui_AnimExpandBlit_IfEnabled(&env, 0, 0, 200, 100, 0), 0,
               "          bEnabled 0 returns its own value (0x663CC)");
    CheckEqI32(nProgress, 50, "          and touches nothing");
    CheckEqI32(Ui_AnimExpandBlit_IfEnabled(&env, 0, 0, 200, 100, 2), 2,
               "          nor does any other value (0x66414)");
    CheckEqI32(nProgress, 50, "          progress still 50");
    CheckEqI32(Ui_AnimExpandBlit_IfEnabled(&env, 0, 0, 200, 100, 1), 1,
               "          bEnabled 1 runs the fixed-parameter blit (0x6640C)");
    CheckEqI32(nProgress, 100, "          mode 0, so the animation completes");

    Check(!Ui_AnimExpandBlit(NULL, 0, 0, 10, 10, 4, 0, 0, 0, 0), "          a null env draws nothing");
}

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    /* The wrap test measures text, so the real advance table has to be loaded.
     * Without it every width is 0 and nothing would ever wrap. */
    if (Asset_OpenArchives(apszPaths)) {
        FontMetrics_Load();
    }
    Check(g_fontMetrics.bLoaded, "fontsize.dat loaded (needed by the wrap test)");

    TestLayout();
    TestCtor();
    TestLinesAndMeta();
    TestScroll();
    TestWrapText();
    TestClickRow();
    TestTrackHold();
    TestLineList();
    TestSwapLines();
    TestCydoniaHandler();
    TestAnimExpandBlit();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
