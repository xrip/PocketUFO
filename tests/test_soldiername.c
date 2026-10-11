/* test_soldiername.c - soldier name editing (Map_BackspaceSelectedName
 * @ 0x63138, SoldierScreen_CopyNameToEdit @ 0x6318c,
 * SoldierScreen_AppendNameGlyph @ 0x63208). */
#include <stdio.h>
#include <string.h>

#include "game/pufo_font.h"
#include "game/pufo_soldiername.h"
#include "game/pufo_unit.h"
#include "util/pufo_str.h"
#include "pufo_test.h"

/* This suite keeps the (condition, what) argument order its call sites use. */
static inline void Check(bool bCond, const char *pszWhat)
{
    TestCheck(pszWhat, bCond);
}

static inline void CheckEqStr(const char *pszGot, const char *pszWant, const char *pszWhat)
{
    ++g_nChecks;
    if (strcmp(pszGot, pszWant) == 0) {
        printf("  ok   %s\n", pszWhat);
        return;
    }
    ++g_nFails;
    printf("  FAIL %s (got \"%s\" want \"%s\")\n", pszWhat, pszGot, pszWant);
}

static void TestBackspace(void)
{
    Soldier s;

    Unit_ctor(&s);

    Check(SoldierName_Backspace(&s) == 0, "backspace empty returns 0");
    CheckEqStr(s.szName, "", "backspace empty leaves name");

    pufo_strcpy(s.szName, "RK");
    Check(SoldierName_Backspace(&s) == 2, "backspace two-char returns 2");
    CheckEqStr(s.szName, "R", "backspace removes last char");

    Check(SoldierName_Backspace(&s) == 1, "backspace one-char returns 1");
    CheckEqStr(s.szName, "", "backspace removes final char");

    Check(SoldierName_Backspace(NULL) == 0, "backspace NULL soldier returns 0");
}

static void TestCopyToEdit(void)
{
    Soldier s;
    char szEdit[PUFO_NAME_EDIT_MAX];

    Unit_ctor(&s);
    pufo_strcpy(s.szName, "CYDNIA");

    memset(szEdit, 0x7F, sizeof(szEdit));
    SoldierName_CopyToEdit(&s, szEdit);
    CheckEqStr(szEdit, "CYDNIA", "copy name to edit buffer");

    SoldierName_CopyToEdit(NULL, szEdit);
    CheckEqStr(szEdit, "CYDNIA", "copy NULL soldier leaves buffer");

    SoldierName_CopyToEdit(&s, NULL);
}

static void TestGlyphAt(void)
{
    /* (x - 12) / 14 + 16 * ((y - 80) / 13 + 2), truncating division. */
    Check(SoldierName_GlyphAt(12, 80) == 32, "glyph at grid origin is 32");
    Check(SoldierName_GlyphAt(26, 80) == 33, "glyph one column right");
    Check(SoldierName_GlyphAt(12, 93) == 48, "glyph one row down");
    Check(SoldierName_GlyphAt(12, 80 + 13 * 3) == 80, "glyph three rows down");
    /* Truncation toward zero: 11 is one less than 12. */
    Check(SoldierName_GlyphAt(11, 80) == 32, "glyph truncates toward zero");
}

static void TestAppendGlyph(void)
{
    Soldier s;

    Unit_ctor(&s);
    memset(&g_fontMetrics, 0, sizeof(g_fontMetrics));
    g_fontMetrics.aAdvance[33] = 2; /* '!' advances, so it can be appended */
    g_fontMetrics.aAdvance[32] = 0; /* space has no width, so it is refused */

    pufo_strcpy(s.szName, "A");
    Check(SoldierName_AppendGlyphAt(&s, 26, 80), "append advancing glyph succeeds");
    CheckEqStr(s.szName, "A!", "append advances the name");

    /* Width 0 refuses the append and leaves the name untouched. */
    Check(!SoldierName_AppendGlyphAt(&s, 12, 80), "append zero-width glyph refused");
    CheckEqStr(s.szName, "A!", "refused append leaves name");

    /* At 38 characters one more still fits; at 39 the append is refused because
     * the original's 40th character would overwrite the terminator. */
    memset(s.szName, 'A', 38);
    s.szName[38] = '\0';
    Check(SoldierName_AppendGlyphAt(&s, 26, 80), "append at length 38 succeeds");
    CheckEqStr(s.szName, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA!", "38 plus one");
    Check(!SoldierName_AppendGlyphAt(&s, 26, 80), "append at length 39 refused");

    Check(!SoldierName_AppendGlyphAt(NULL, 26, 80), "append NULL soldier refused");
}

int main(void)
{
    TestBackspace();
    TestCopyToEdit();
    TestGlyphAt();
    TestAppendGlyph();

    return TestSummary("test_soldiername");
}
