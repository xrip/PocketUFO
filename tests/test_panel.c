/* test_panel.c - checks the shared UI panel line engine.
 *
 * The layout is the thing to pin: UiPanel is 7092 bytes and the offsets are proven
 * (see pufo_panel.h). The scroll arithmetic and the wrap behaviour are the two
 * pieces of logic most likely to be "tidied" later.
 */
#include "asset/pufo_asset.h"
#include "ui/pufo_panel.h"

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
    Check(offsetof(UiPanel, gap_0) == 0x0000, "  gap_0 at 0x0000");
    Check(offsetof(UiPanel, aLineMeta) == 0x0010, "  aLineMeta at 0x0010");
    /* aLineMeta spans 500*6 = 3000 bytes, so nUnk3016 follows with no padding on
     * either target. Everything from pfnClick onward holds pointers and shifts on
     * Win64, so those absolute offsets are asserted only for ILP32. */
    Check(offsetof(UiPanel, nUnk3016) == 0x0BC8, "  nUnk3016 at 0x0BC8");

    /* The scalar run from bUnk3024 to nStyle keeps its relative layout on both
     * targets, which is where a field-order mistake would show. */
    Check(offsetof(UiPanel, bUnk3024) < offsetof(UiPanel, nUnk3028), "  bUnk3024 before nUnk3028");
    Check(offsetof(UiPanel, nUnk3028) < offsetof(UiPanel, nUnk3032), "  nUnk3028 before nUnk3032");
    Check(offsetof(UiPanel, nUnk3032) < offsetof(UiPanel, nLineCount), "  nUnk3032 before nLineCount");
    Check(offsetof(UiPanel, nLineCount) < offsetof(UiPanel, nStyle), "  nLineCount before nStyle");
    Check(offsetof(UiPanel, nStyle) < offsetof(UiPanel, apLines), "  nStyle before apLines");

#if UINTPTR_MAX == 0xFFFFFFFFu
    /* The rest holds pointers, so the Win64 struct is larger and the tail shifts.
     * The ILP32 offsets are the ones the IDB reports, so assert them there only. */
    Check(offsetof(UiPanel, pfnClick) == 0x0BCC, "  pfnClick at 0x0BCC (ILP32)");
    Check(offsetof(UiPanel, bUnk3024) == 0x0BD0, "  bUnk3024 at 0x0BD0 (ILP32)");
    Check(offsetof(UiPanel, nUnk3028) == 0x0BD4, "  nUnk3028 at 0x0BD4 (ILP32)");
    Check(offsetof(UiPanel, nLineCount) == 0x0BDC, "  nLineCount at 0x0BDC (ILP32)");
    Check(offsetof(UiPanel, nStyle) == 0x0BE0, "  nStyle at 0x0BE0 (ILP32)");
    Check(offsetof(UiPanel, apLines) == 0x0BE4, "  apLines at 0x0BE4 (ILP32)");
    Check(offsetof(UiPanel, nPosX) == 0x1B84, "  nPosX at 0x1B84 (ILP32)");
    Check(offsetof(UiPanel, nViewW) == 0x1B94, "  nViewW at 0x1B94 (ILP32)");
    Check(offsetof(UiPanel, nFont) == 0x1B9C, "  nFont at 0x1B9C (ILP32)");
    Check(offsetof(UiPanel, bUnk7089) == 0x1BB1, "  bUnk7089 at 0x1BB1 (ILP32)");
    Check(sizeof(UiPanel) >= 0x1BB1 + 1, "  struct covers the last field");
#else
    /* On Win64 the ordering is what matters. Assert the relative layout of the
     * scalar tail, which is where a field-order mistake would show up. */
    Check(offsetof(UiPanel, nPosX) < offsetof(UiPanel, nPosY), "  nPosX before nPosY");
    Check(offsetof(UiPanel, nPosY) < offsetof(UiPanel, nUnk7052), "  nPosY before nUnk7052");
    Check(offsetof(UiPanel, nUnk7056) < offsetof(UiPanel, nViewW), "  nUnk7056 before nViewW");
    Check(offsetof(UiPanel, nViewW) < offsetof(UiPanel, nViewH), "  nViewW before nViewH");
    Check(offsetof(UiPanel, nViewH) < offsetof(UiPanel, nFont), "  nViewH before nFont");
    Check(offsetof(UiPanel, nFont) < offsetof(UiPanel, bFlag7072), "  nFont before bFlag7072");
    Check(offsetof(UiPanel, bUnk7089) == offsetof(UiPanel, bUnk7088) + 1,
          "  bUnk7089 directly after bUnk7088");
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
    CheckEqI32(panel.bFlag7072, 1, "  bFlag7072 set");
    CheckEqI32(panel.bFlag7073, 1, "  bFlag7073 set");
    CheckEqI32(panel.nStyle, 1, "  nStyle 1");
    CheckEqI32(panel.nViewW, 240, "  nViewW 240");
    CheckEqI32(panel.nViewH, 320, "  nViewH 320");
    CheckEqI32(panel.bUnk7088, 0xFF, "  bUnk7088 0xFF");
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
    UiPanel_SetLineMeta(&panel, 2, 10, 20, 30, 40, 50, 60);
    Check(UiPanel_GetLineMeta(&panel, 2, 1, &r, &g, &b), "  triple 1 readable");
    Check(r == 10 && g == 20 && b == 30, "  triple 1 bytes in order");
    Check(UiPanel_GetLineMeta(&panel, 2, 2, &r, &g, &b), "  triple 2 readable");
    Check(r == 40 && g == 50 && b == 60, "  triple 2 bytes in order");
    Check(!UiPanel_GetLineMeta(&panel, 2, 3, &r, &g, &b), "  invalid triple refused");
    /* Meta of line 0 must be untouched. */
    Check(UiPanel_GetLineMeta(&panel, 0, 1, &r, &g, &b) && r == 0 && g == 0 && b == 0,
          "  line 0 meta untouched");

    printf("Style setters\n");
    UiPanel_SetBytes0to2(&panel, 1, 2, 3);
    UiPanel_SetBytes3to5(&panel, 4, 5, 6);
    UiPanel_SetBytes6to11(&panel, 7, 8, 9, 10, 11, 12);
    Check(panel.gap_0[0] == 1 && panel.gap_0[1] == 2 && panel.gap_0[2] == 3,
          "  bytes 0..2 written in order");
    Check(panel.gap_0[3] == 4 && panel.gap_0[4] == 5 && panel.gap_0[5] == 6,
          "  bytes 3..5 written in order");
    Check(panel.gap_0[6] == 7 && panel.gap_0[11] == 12, "  bytes 6..11 written in order");

    printf("Aux words\n");
    UiPanel_SetLineAux(&panel, 1, 4242);
    CheckEqI32(UiPanel_GetLineAux(&panel, 1), 4242, "  aux stored");
    CheckEqI32(UiPanel_GetLineAux(&panel, 0), 0, "  other aux untouched");

    UiPanel_FreeLines(&panel);
}

/* ScrollNext is bounded by nLineCount - nStyle, which is the interesting part:
 * the window can never scroll past the last full page. */
static void TestScroll(void)
{
    UiPanel panel;
    i32     i;

    printf("UiPanel_ScrollNext / ScrollPrev\n");
    Check(UiPanel_ctor(&panel, 10), "  constructed with 10 lines");
    panel.nStyle = 3;

    /* ScrollPrev at the top is a no-op. */
    UiPanel_ScrollPrev(&panel);
    CheckEqI32(panel.nUnk3028, 0, "  cannot scroll above 0");

    /* The last valid cursor is nLineCount - nStyle == 7. */
    for (i = 0; i < 20; ++i) {
        UiPanel_ScrollNext(&panel);
    }
    CheckEqI32(panel.nUnk3028, 7, "  clamped to nLineCount - nStyle");

    UiPanel_ScrollPrev(&panel);
    CheckEqI32(panel.nUnk3028, 6, "  scrolls back one");
    UiPanel_ScrollPrev(&panel);
    CheckEqI32(panel.nUnk3028, 5, "  scrolls back two");

    /* A style larger than the line count makes the limit negative, so scrolling
     * stays at 0. That is the original's arithmetic, not a special case. */
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

static void RecClick(void) {}

static void TestClickRow(void)
{
    UiPanel panel;

    printf("UiPanel_ClickRow\n");
    Check(UiPanel_ctor(&panel, 10), "  constructed");
    panel.nPosY = 10;
    panel.nFont = 20;      /* row = (nY - 10) / 20 */
    panel.nUnk7052 = 5;    /* rect left */
    panel.nViewW = 100;
    panel.nUnk7056 = 10;   /* rect top */
    panel.nViewH = 50;
    panel.nStyle = 3;
    panel.nLineCount = 10;
    panel.bFlag7073 = 1;

    /* Inside the rect: row 2, callback fires. */
    panel.pfnClick = RecClick;
    CheckEqI32(UiPanel_ClickRow(&panel, 20, 50), 2, "  inside click row is 2");
    CheckEqI32(panel.nUnk3028, 2, "  nUnk3028 stores the row");
    CheckEqI32(panel.nUnk3032, 4, "  nUnk3032 = row + nUnk3028");
    Check(panel.pfnClick != NULL, "  callback installed");

    /* Outside the x range: no callback, but the row is still stored. */
    CheckEqI32(UiPanel_ClickRow(&panel, 1, 50), 2, "  outside x still returns row");

    /* Above the rect: row 0 (the division truncates toward zero) and no callback. */
    CheckEqI32(UiPanel_ClickRow(&panel, 20, 5), 0, "  above the rect row 0");

    /* bFlag7073 clear disables the callback but not the row. */
    panel.bFlag7073 = 0;
    CheckEqI32(UiPanel_ClickRow(&panel, 20, 50), 2, "  disabled flag still returns row");

    UiPanel_FreeLines(&panel);
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

    Check(UiPanel_SwapLines(&panel, 0, 1), "  swapped");
    Check(strcmp(UiPanel_GetLineText(&panel, 0), "bravo") == 0, "  line 0 is bravo");
    Check(strcmp(UiPanel_GetLineText(&panel, 1), "alpha") == 0, "  line 1 is alpha");
    CheckEqI32(UiPanel_GetLineAux(&panel, 0), 22, "  aux 0 swapped");
    CheckEqI32(UiPanel_GetLineAux(&panel, 1), 11, "  aux 1 swapped");

    /* Swapping with itself is stable. */
    Check(UiPanel_SwapLines(&panel, 0, 0), "  self swap accepted");
    Check(strcmp(UiPanel_GetLineText(&panel, 0), "bravo") == 0, "  self swap keeps text");

    Check(!UiPanel_SwapLines(&panel, 0, 99), "  out-of-range refused");
    Check(!UiPanel_SwapLines(NULL, 0, 1), "  NULL panel refused");

    UiPanel_FreeLines(&panel);
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
    TestSwapLines();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
