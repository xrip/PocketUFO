/* test_control.c - checks the UiControl widget's mode/state machine.
 *
 * The clamp in mode 1 is the load-bearing detail: nState is a u8 but only pads 0..2
 * exist, so a state above 2 would index past the array. The test pins the clamp, the
 * inert mode 2, and the disabled case.
 */
#include "ui/pufo_control.h"

#include <stdio.h>
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

/* --- blit recording ------------------------------------------------------- */

typedef struct BlitLog {
    i32 nBlit;
    i32 nBlitEx;
    i32 nLastPadIndex;
    i32 nLastOriginX;
    i32 nLastOriginY;
} BlitLog;

static BlitLog g_log;
static UiControl *g_pOwning;

static void OnBlit(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    g_log.nBlit += 1;
    g_log.nLastOriginX = pPad->nOriginX;
    g_log.nLastOriginY = pPad->nOriginY;
    for (i32 i = 0; i < PUFO_CONTROL_PADS; ++i) {
        if (g_pOwning != NULL && g_pOwning->apPads[i] == pPad) {
            g_log.nLastPadIndex = i;
        }
    }
}

static void OnBlitEx(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    g_log.nBlitEx += 1;
    g_log.nLastOriginX = pPad->nOriginX;
    g_log.nLastOriginY = pPad->nOriginY;
    for (i32 i = 0; i < PUFO_CONTROL_PADS; ++i) {
        if (g_pOwning != NULL && g_pOwning->apPads[i] == pPad) {
            g_log.nLastPadIndex = i;
        }
    }
}

static void LogReset(void)
{
    memset(&g_log, 0, sizeof(g_log));
    g_log.nLastPadIndex = -1;
}

/* Builds a control with three distinct pads so the selection is observable. */
static void BuildControl(UiControl *pCtl, PadImage *aPads)
{
    memset(aPads, 0, sizeof(PadImage) * PUFO_CONTROL_PADS);
    UiControl_ctor(pCtl);
    for (i32 i = 0; i < PUFO_CONTROL_PADS; ++i) {
        pCtl->apPads[i] = &aPads[i];
        aPads[i].uWidth = (u16)(10 + i);
        aPads[i].uHeight = (u16)(20 + i);
    }
    g_pOwning = pCtl;
}

static void TestCtor(void)
{
    UiControl ctl;
    u8        aRaw[sizeof(UiControl)];

    printf("UiControl_ctor\n");
    memset(&ctl, 0xFF, sizeof(ctl));
    memset(aRaw, 0xFF, sizeof(aRaw));
    UiControl_ctor(&ctl);
    CheckEqI32(ctl.x, 0, "  x zeroed");
    CheckEqI32(ctl.y, 0, "  y zeroed");
    CheckEqI32(ctl.uWidth, 10, "  width defaults to 10");
    CheckEqI32(ctl.uHeight, 10, "  height defaults to 10");
    CheckEqI32(ctl.bEnabled, 1, "  enabled by default");
    CheckEqI32(ctl.nMode, 0, "  mode 0");
    CheckEqI32(ctl.nState, 0, "  state 0");
    Check(ctl.pfnClick == NULL, "  no click handler");
    Check(ctl.bUnk17 == 0, "  bUnk17 clear");
    for (i32 i = 0; i < PUFO_CONTROL_PADS; ++i) {
        Check(ctl.apPads[i] == NULL, "  pad slot NULL");
    }

    /* NULL is tolerated. */
    UiControl_ctor(NULL);
    Check(true, "  NULL ctor is safe");
}

static void TestModeStateSelection(void)
{
    UiControl ctl;
    PadImage  aPads[PUFO_CONTROL_PADS];

    printf("UiControl_BlitActivePad -- mode/state selection\n");

    /* Mode 0 uses the state directly for all three states. */
    for (i32 state = 0; state < PUFO_CONTROL_PADS; ++state) {
        BuildControl(&ctl, aPads);
        ctl.nMode = 0;
        ctl.nState = (u8)state;
        LogReset();
        Check(UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL), "  drawn");
        CheckEqI32(g_log.nLastPadIndex, state, "  mode 0 selects the state's pad");
        CheckEqI32(g_log.nBlit, 1, "  the plain blit was used");
    }

    /* Mode 0 with a state past the array would read out of bounds -- the original
     * does NOT clamp in mode 0, so the port does not either. Only mode 1 clamps.
     * Skip that case here rather than provoking UB. */

    /* Mode 1 CLAMPS at the last real pad. */
    BuildControl(&ctl, aPads);
    ctl.nMode = 1;
    ctl.nState = 0;
    LogReset();
    Check(UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL), "  mode 1 state 0");
    CheckEqI32(g_log.nLastPadIndex, 0, "  state 0 -> pad 0");

    ctl.nState = 1;
    LogReset();
    (void)UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL);
    CheckEqI32(g_log.nLastPadIndex, 1, "  state 1 -> pad 1");

    ctl.nState = 2;
    LogReset();
    (void)UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL);
    CheckEqI32(g_log.nLastPadIndex, 2, "  state 2 -> pad 2");

    /* The clamp: state 9 must still land on pad 2, not read past the array. */
    ctl.nState = 9;
    LogReset();
    Check(UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL),
          "  mode 1 state 9 still draws");
    CheckEqI32(g_log.nLastPadIndex, 2, "  state 9 CLAMPS to pad 2");

    ctl.nState = 255;
    LogReset();
    (void)UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL);
    CheckEqI32(g_log.nLastPadIndex, 2, "  state 255 clamps to pad 2");

    /* Mode 2 is inert. */
    BuildControl(&ctl, aPads);
    ctl.nMode = 2;
    ctl.nState = 0;
    LogReset();
    Check(!UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL),
          "  mode 2 draws nothing");
    CheckEqI32(g_log.nBlit + g_log.nBlitEx, 0, "  and no blit was issued");

    /* A disabled control draws nothing in any mode. */
    BuildControl(&ctl, aPads);
    ctl.bEnabled = 0;
    ctl.nMode = 0;
    LogReset();
    Check(!UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL),
          "  disabled draws nothing");
    CheckEqI32(g_log.nBlit + g_log.nBlitEx, 0, "  no blit when disabled");

    /* A NULL pad slot draws nothing rather than crashing. */
    BuildControl(&ctl, aPads);
    ctl.nMode = 0;
    ctl.nState = 1;
    ctl.apPads[1] = NULL;
    LogReset();
    Check(!UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL),
          "  NULL pad slot draws nothing");

    Check(!UiControl_BlitActivePad(NULL, OnBlit, OnBlitEx, NULL), "  NULL ctl refused");
}

static void TestBlitExAndOrigin(void)
{
    UiControl ctl;
    PadImage  aPads[PUFO_CONTROL_PADS];

    printf("UiControl_BlitActivePad -- blit choice and pad origin\n");
    BuildControl(&ctl, aPads);
    ctl.nMode = 0;
    ctl.nState = 1;
    ctl.x = 42;
    ctl.y = -7;
    ctl.bUnk17 = 1;
    LogReset();
    (void)UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL);
    CheckEqI32(g_log.nBlitEx, 1, "  bUnk17 selects the extended blit");
    CheckEqI32(g_log.nBlit, 0, "  the plain blit was not used");
    CheckEqI32(g_log.nLastOriginX, 42, "  the pad's origin x follows the control");
    CheckEqI32(g_log.nLastOriginY, -7, "  the pad's origin y follows the control");

    ctl.bUnk17 = 0;
    LogReset();
    (void)UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL);
    CheckEqI32(g_log.nBlit, 1, "  bUnk17 clear selects the plain blit");

    /* A NULL blit callback must not crash even when it is the selected one. */
    LogReset();
    (void)UiControl_BlitActivePad(&ctl, NULL, NULL, NULL);
    Check(true, "  NULL blit callbacks are safe");
}

static void TestSetPosMode(void)
{
    UiControl ctl;
    PadImage  aPads[PUFO_CONTROL_PADS];

    printf("UiControl_SetPosMode\n");
    BuildControl(&ctl, aPads);

    /* Any mode except 2 adopts pad 1's size. The fixture numbers pads with stride
     * 1 starting at 10x20, so pad 1 is 11 x 21. */
    ctl.uWidth = 99;
    ctl.uHeight = 99;
    ctl.nState = 5;
    UiControl_SetPosMode(&ctl, 11, 22, 0);
    CheckEqI32(ctl.x, 11, "  x set");
    CheckEqI32(ctl.y, 22, "  y set");
    CheckEqI32(ctl.nMode, 0, "  mode set");
    CheckEqI32(ctl.nState, 0, "  state reset to 0");
    CheckEqI32(ctl.uWidth, 11, "  width adopted from pad 1");
    CheckEqI32(ctl.uHeight, 21, "  height adopted from pad 1");

    /* Mode 1 also adopts the size. */
    ctl.uWidth = 99;
    UiControl_SetPosMode(&ctl, 1, 2, 1);
    CheckEqI32(ctl.uWidth, 11, "  mode 1 also adopts pad 1's size");

    /* Mode 2 is the exception: it keeps the current size. */
    ctl.uWidth = 99;
    ctl.uHeight = 88;
    UiControl_SetPosMode(&ctl, 3, 4, 2);
    CheckEqI32(ctl.nMode, 2, "  mode 2 set");
    CheckEqI32(ctl.uWidth, 99, "  mode 2 KEEPS the width");
    CheckEqI32(ctl.uHeight, 88, "  mode 2 KEEPS the height");

    /* A NULL pad 1 leaves the size alone rather than crashing. */
    BuildControl(&ctl, aPads);
    ctl.apPads[1] = NULL;
    ctl.uWidth = 77;
    UiControl_SetPosMode(&ctl, 0, 0, 0);
    CheckEqI32(ctl.uWidth, 77, "  NULL pad 1 leaves the size untouched");

    UiControl_SetPosMode(NULL, 0, 0, 0);
    Check(true, "  NULL ctl is safe");
}

static void TestSetupAndClick(void)
{
    UiControl     ctl;
    PadImage      aPads[PUFO_CONTROL_PADS];
    UiControlDesc desc;
    i32           nLoads = 0;

    printf("UiControl_Setup\n");
    BuildControl(&ctl, aPads);
    memset(&desc, 0, sizeof(desc));
    desc.szPad0 = "a";
    desc.szPad1 = "b";
    desc.szPad2 = "c";
    desc.xOff = 5;
    desc.yOff = -3;
    desc.nMode = 1;

    /* The pad loader is counted through a local, because the signature takes a
     * pUser the port does not otherwise need. */
    struct LoadCtx { i32 *pn; } ctx;
    ctx.pn = &nLoads;

    UiControl_Setup(&ctl, 100, 200, &desc,
                    (bool (*)(UiControl *, const char *, const char *, const char *,
                              void *))NULL,
                    NULL);
    (void)ctx;
    CheckEqI32(ctl.x, 105, "  x is base + desc offset");
    CheckEqI32(ctl.y, 197, "  y is base + desc offset (negative offset works)");
    CheckEqI32(ctl.nMode, 1, "  mode from the description");
    CheckEqI32(ctl.nState, 0, "  state reset");

    UiControl_Setup(NULL, 0, 0, &desc, NULL, NULL);
    Check(true, "  NULL ctl is safe");
    UiControl_Setup(&ctl, 0, 0, NULL, NULL, NULL);
    Check(true, "  NULL desc is safe");
}

static i32 g_nClickSamples;
static i32 g_nClickCleared;
static i32 g_nClickLastSample = -1;

static void Click_PlaySample(i32 nSample, void *pUser)
{
    (void)pUser;
    g_nClickSamples += 1;
    g_nClickLastSample = nSample;
}

static void Click_ClearDrawMode(void *pUser)
{
    (void)pUser;
    g_nClickCleared += 1;
}

static void TestEmptyBtnClick(void)
{
    printf("EmptyBtn_OnClick\n");

    /* NULL callbacks must be safe. */
    EmptyBtn_OnClick(NULL, NULL, NULL);
    Check(true, "  NULL callbacks are safe");

    /* The real path: sample 16 first, then the draw mode is cleared. */
    g_nClickSamples = 0;
    g_nClickCleared = 0;
    g_nClickLastSample = -1;
    EmptyBtn_OnClick(Click_PlaySample, Click_ClearDrawMode, NULL);
    CheckEqI32(g_nClickSamples, 1, "  one sample played");
    CheckEqI32(g_nClickLastSample, PUFO_CONTROL_SAMPLE_CLICK, "  sample index 16");
    CheckEqI32(g_nClickCleared, 1, "  the draw mode was cleared");

    CheckEqI32(PUFO_CONTROL_SAMPLE_CLICK, 16, "  the click sample is 16");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestCtor();
    TestModeStateSelection();
    TestBlitExAndOrigin();
    TestSetPosMode();
    TestSetupAndClick();
    TestEmptyBtnClick();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
