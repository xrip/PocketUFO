/* test_modal.c - the modal yes/no dialogue state
 * (UiModal_Begin @ 0x26430, UiModal_End @ 0x2648c). */
#include <stdio.h>
#include <string.h>

#include "ui/pufo_modal.h"
#include "render/pufo_render.h"
#include "game/pufo_font.h"

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

static void Yes(void) {}
static void No(void) {}

static void TestBeginEnd(void)
{
    UiModalState modal;

    printf("UiModal_Begin / UiModal_End\n");
    memset(&modal, 0xAA, sizeof(modal));
    UiModal_Begin(&modal, 140, 24, 4, "YES", Yes, (void *)"NO", No);

    Check(modal.bOpen, "  open flag set");
    CheckEqI32(modal.nField48, 0, "  pCtx+48 cleared");
    CheckEqI32(modal.nActionPhaseSave112, 0, "  anActionPhaseSave0[112] cleared");
    CheckEqI32(modal.anRect12_14[0], 140, "  slot 12 stored");
    CheckEqI32(modal.anRect12_14[1], 24, "  slot 13 stored");
    CheckEqI32(modal.anRect12_14[2], 4, "  slot 14 stored");
    Check(modal.pLabel15 != NULL, "  slot 15 label stored");
    Check(modal.pLabel15 != NULL &&
          strcmp((const char *)modal.pLabel15, "YES") == 0, "  slot 15 is the first label");
    Check(modal.pfn16 == Yes, "  slot 16 callback stored");
    Check(modal.p17 != NULL, "  slot 17 argument stored");
    Check(modal.pfn18 == No, "  slot 18 callback stored");

    UiModal_End(&modal);
    Check(!modal.bOpen, "  end clears only the open flag");
    CheckEqI32(modal.anRect12_14[0], 140, "  stored fields survive end");
    Check(modal.pfn16 == Yes, "  callbacks survive end");
}

static void TestDrawDialog(void)
{
    SoftSurf surf;
    UiModalAnim anim;
    UiModalLayout layout;
    static u8 aRecs[3 * PUFO_TEXT_REC_STRIDE];
    GfxColorEnv color;
    i32 nRet;
    i32 i;

    printf("UiModal_DrawDialog @ 0x264a0\n");

    memset(&color, 0, sizeof(color));

    SoftSurf_ctor(&surf);
    SoftSurf_AllocPixels(&surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &surf;
    /* No glyph table: Font_DrawGlyph returns early, so the draw calls are harmless and the
     * test asserts geometry, which is what the function decides. */
    g_fontSys.apSprIndex = NULL;
    for (i = 0; i < 256; ++i) {
        g_fontMetrics.aAdvance[i] = 5;
    }

    memset(aRecs, 0, sizeof(aRecs));
    memcpy(aRecs + 0 * PUFO_TEXT_REC_STRIDE, "ABC", 4);
    memcpy(aRecs + 1 * PUFO_TEXT_REC_STRIDE, "DEFG", 5);
    memcpy(aRecs + 2 * PUFO_TEXT_REC_STRIDE, "HI", 3);

    /* No labels: the dialog is 13*nLines + 10 tall and no button slots are written. */
    memset(&anim, 0, sizeof(anim));
    anim.pColor = &color;
    memset(&layout, 0xAA, sizeof(layout));
    nRet = UiModal_DrawDialog(&anim, aRecs, 200, 0, 2, NULL, NULL, &layout);
    CheckEqI32(layout.nButtonCount, 0, "  no labels means no buttons");
    CheckEqI32(nRet, 4, "  returns the length of the last line (record 1, \"DEFG\")");

    /* One label: height becomes 13*nLines + 35, so nY = (320 - 61)/2 = 129 and the button
     * sits at 13*2 + 129 + 10 = 165, centred at nX + (200 - 50)/2 = 20 + 75. */
    memset(&anim, 0, sizeof(anim));
    anim.pColor = &color;
    memset(&layout, 0xAA, sizeof(layout));
    nRet = UiModal_DrawDialog(&anim, aRecs, 200, 0, 2, "YES", NULL, &layout);
    CheckEqI32(layout.nButtonCount, 1, "  one label means one button");
    CheckEqI32(layout.nButtonX[0], 95, "  single button is centred");
    CheckEqI32(layout.nButtonY, 165, "  button row follows the taller dialog");

    /* Two labels: x = nX + (200-100)/3 and nX + 2*((200-100)/3 + 25). */
    memset(&anim, 0, sizeof(anim));
    anim.pColor = &color;
    memset(&layout, 0xAA, sizeof(layout));
    nRet = UiModal_DrawDialog(&anim, aRecs, 200, 0, 2, "YES", "NO", &layout);
    CheckEqI32(layout.nButtonCount, 2, "  two labels means two buttons");
    CheckEqI32(layout.nButtonX[0], 53, "  left button at nX + (w-100)/3");
    CheckEqI32(layout.nButtonX[1], 136, "  right button at nX + 2*((w-100)/3 + 25)");
    CheckEqI32(layout.nButtonY, 165, "  both buttons share the row");

    /* The cursor advances before the text is drawn, and it wraps at 40. */
    memset(&anim, 0, sizeof(anim));
    anim.pColor = &color;
    anim.nCursor = 40;
    anim.bProgressive = 1;
    UiModal_DrawDialog(&anim, aRecs, 200, 0, 2, NULL, NULL, &layout);
    CheckEqI32(anim.nCursor, 1, "  a cursor of 40 wraps to 1");
    CheckEqI32(anim.bProgressive, 0, "  and the wrap clears progressive mode");

    memset(&anim, 0, sizeof(anim));
    anim.pColor = &color;
    anim.nCursor = 0;
    anim.bProgressive = 0;
    UiModal_DrawDialog(&anim, aRecs, 200, 0, 2, NULL, NULL, &layout);
    CheckEqI32(anim.nCursor, 1, "  a cursor of 0 increments to 1");
    CheckEqI32(anim.bProgressive, 1, "  and a cursor of 0 turns progressive mode on");

    memset(&anim, 0, sizeof(anim));
    anim.pColor = &color;
    anim.nCursor = 5;
    anim.bProgressive = 0;
    UiModal_DrawDialog(&anim, aRecs, 200, 0, 2, NULL, NULL, &layout);
    CheckEqI32(anim.nCursor, 6, "  otherwise the cursor just increments");
    CheckEqI32(anim.bProgressive, 0, "  and progressive mode stays off");

    /* Records are read at stride 205 from the index the caller passes. */
    memset(&anim, 0, sizeof(anim));
    anim.pColor = &color;
    nRet = UiModal_DrawDialog(&anim, aRecs, 200, 1, 2, NULL, NULL, &layout);
    CheckEqI32(nRet, 2, "  record index 1 with 2 lines ends on record 2");

    g_pMainSurface = NULL;
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestBeginEnd();
    TestDrawDialog();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
