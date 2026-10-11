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

/* UiControl_HitTest @ 0x1a3d8. */
static int g_nClicks;
static u32 g_nUiSlot0;

static void OnClick(void) { g_nClicks += 1; }

static UiControlHitCtx HitCtx(void)
{
    UiControlHitCtx c;
    c.pnUiSlot0 = &g_nUiSlot0;
    c.pfnBlit   = OnBlit;
    c.pfnBlitEx = OnBlitEx;
    c.pUser     = NULL;
    return c;
}

/* A 10x10 control at (20,30) with three pads and a click handler. */
static void HitControl(UiControl *pCtl, u8 nMode, u8 nState)
{
    static PadImage aPads[PUFO_CONTROL_PADS];

    BuildControl(pCtl, aPads);
    pCtl->x       = 20;
    pCtl->y       = 30;
    pCtl->uWidth  = 10;
    pCtl->uHeight = 10;
    pCtl->nMode   = nMode;
    pCtl->nState  = nState;
    pCtl->pfnClick = OnClick;
}

static void TestHitTest(void)
{
    UiControl ctl;
    UiControlHitCtx ctx = HitCtx();

    printf("UiControl_HitTest -- press, release, mode transitions\n");

    /* A disabled control refuses everything. */
    HitControl(&ctl, 0, 0);
    ctl.bEnabled = 0;
    LogReset();
    g_nClicks = 0;
    Check(!UiControl_HitTest(&ctl, 22, 32, true, true, &ctx), "  disabled control refuses");
    Check(g_nClicks == 0 && ctl.nState == 0, "  disabled control changes nothing");

    /* The force flag fires a mode-0 control already in state 1, with no position test. */
    HitControl(&ctl, 0, 1);
    LogReset();
    g_nClicks = 0;
    Check(UiControl_HitTest(&ctl, 0, 0, false, true, &ctx), "  force click fires");
    Check(g_nClicks == 1 && ctl.nState == 1, "  force click leaves the state alone");
    Check(g_log.nBlit == 0, "  force click does not redraw");

    /* The force flag is mode 0 only. */
    HitControl(&ctl, 1, 1);
    LogReset();
    g_nClicks = 0;
    Check(!UiControl_HitTest(&ctl, 0, 0, false, true, &ctx), "  force click is mode 0 only");
    Check(g_nClicks == 0, "  force click does not fire in mode 1");

    /* Press inside the box: state 0 -> 1, redraw, UI slot cleared. */
    HitControl(&ctl, 0, 0);
    LogReset();
    g_nUiSlot0 = 7;
    Check(UiControl_HitTest(&ctl, 22, 32, true, false, &ctx), "  press inside the box hits");
    Check(ctl.nState == 1 && g_log.nBlit == 1, "  press raises the state and redraws");
    Check(g_nUiSlot0 == 0, "  press clears the UI slot");

    /* Press inside when already raised: no change, but the slot is still cleared. */
    HitControl(&ctl, 0, 1);
    LogReset();
    g_nUiSlot0 = 7;
    Check(UiControl_HitTest(&ctl, 22, 32, true, false, &ctx), "  press on a raised control hits");
    Check(ctl.nState == 1 && g_log.nBlit == 0, "  press on a raised control does nothing else");
    Check(g_nUiSlot0 == 0, "  a no-change press still clears the UI slot");

    /* Press outside the box while raised: state falls back and redraws. */
    HitControl(&ctl, 0, 1);
    LogReset();
    Check(!UiControl_HitTest(&ctl, 5, 5, true, false, &ctx), "  press outside the box misses");
    Check(ctl.nState == 0 && g_log.nBlit == 1, "  a missed press lowers a raised control");

    /* Press outside with nothing raised: no state change, no redraw. */
    HitControl(&ctl, 0, 0);
    LogReset();
    Check(!UiControl_HitTest(&ctl, 5, 5, true, false, &ctx), "  press outside with state 0");
    Check(ctl.nState == 0 && g_log.nBlit == 0, "  a missed press on state 0 does nothing");

    /* The far edge is exclusive at width - 1 and height - 1, so the last inside
     * pixel is x + width - 2. */
    HitControl(&ctl, 0, 0);
    LogReset();
    Check(UiControl_HitTest(&ctl, 28, 38, true, false, &ctx), "  the last box pixel still hits");
    HitControl(&ctl, 0, 0);
    LogReset();
    Check(!UiControl_HitTest(&ctl, 29, 38, true, false, &ctx), "  x + width - 1 is outside");
    HitControl(&ctl, 0, 0);
    LogReset();
    Check(!UiControl_HitTest(&ctl, 28, 39, true, false, &ctx), "  y + height - 1 is outside");
    HitControl(&ctl, 0, 0);
    LogReset();
    Check(UiControl_HitTest(&ctl, 20, 30, true, false, &ctx), "  the near corner is inside");

    /* Mode 1 press: state += 2 with a redraw. */
    HitControl(&ctl, 1, 0);
    LogReset();
    Check(UiControl_HitTest(&ctl, 22, 32, true, false, &ctx), "  mode 1 press hits");
    Check(ctl.nState == 2 && g_log.nBlit == 1, "  mode 1 press adds two to the state");

    /* Mode 1 press when already pressed: nothing changes. */
    HitControl(&ctl, 1, 2);
    LogReset();
    Check(UiControl_HitTest(&ctl, 22, 32, true, false, &ctx), "  mode 1 press on a pressed control");
    Check(ctl.nState == 2 && g_log.nBlit == 0, "  a second mode 1 press changes nothing");

    /* Mode 1 press outside while pressed: state -= 2 and NO redraw. */
    HitControl(&ctl, 1, 2);
    LogReset();
    Check(!UiControl_HitTest(&ctl, 5, 5, true, false, &ctx), "  mode 1 press outside while pressed");
    Check(ctl.nState == 0, "  a missed mode 1 press subtracts two");
    Check(g_log.nBlit == 0, "  a missed mode 1 press does not redraw");

    /* Mode 1 press outside with a low state: nothing changes. */
    HitControl(&ctl, 1, 1);
    LogReset();
    Check(!UiControl_HitTest(&ctl, 5, 5, true, false, &ctx), "  mode 1 press outside with state 1");
    Check(ctl.nState == 1 && g_log.nBlit == 0, "  state 1 is not a mode 1 press");

    /* An unknown mode reports the hit and changes nothing. */
    HitControl(&ctl, 3, 1);
    LogReset();
    Check(UiControl_HitTest(&ctl, 22, 32, true, false, &ctx), "  unknown mode still reports the hit");
    Check(ctl.nState == 1 && g_log.nBlit == 0, "  unknown mode changes nothing on press");

    /* Release, mode 0: state 1 -> 0, redraw, click. */
    HitControl(&ctl, 0, 1);
    LogReset();
    g_nClicks = 0;
    Check(UiControl_HitTest(&ctl, 999, 999, false, false, &ctx), "  release ignores the box");
    Check(ctl.nState == 0 && g_log.nBlit == 1 && g_nClicks == 1, "  release completes the click");

    /* Release, mode 0 with any other state: nothing. */
    HitControl(&ctl, 0, 2);
    LogReset();
    g_nClicks = 0;
    Check(!UiControl_HitTest(&ctl, 22, 32, false, false, &ctx), "  release needs state 1");
    Check(ctl.nState == 2 && g_nClicks == 0, "  release from state 2 does nothing");

    /* Release, mode 2 behaves like mode 0. */
    HitControl(&ctl, 2, 1);
    LogReset();
    g_nClicks = 0;
    Check(UiControl_HitTest(&ctl, 22, 32, false, false, &ctx), "  mode 2 release works");
    Check(ctl.nState == 0 && g_nClicks == 1, "  mode 2 release completes the click");

    /* Release, mode 1: state 2 -> 1. */
    HitControl(&ctl, 1, 2);
    LogReset();
    g_nClicks = 0;
    Check(UiControl_HitTest(&ctl, 22, 32, false, false, &ctx), "  mode 1 release works");
    Check(ctl.nState == 1 && g_log.nBlit == 1 && g_nClicks == 1, "  mode 1 release drops 2 to 1");

    /* Release, mode 1 from a state above 2: it drops to 0, not to 1. */
    HitControl(&ctl, 1, 3);
    LogReset();
    Check(UiControl_HitTest(&ctl, 22, 32, false, false, &ctx), "  mode 1 release from state 3");
    CheckEqI32((i32)ctl.nState, 0, "  a state above 2 releases to 0");

    /* Release, mode 1 with a low state: nothing. */
    HitControl(&ctl, 1, 1);
    LogReset();
    g_nClicks = 0;
    Check(!UiControl_HitTest(&ctl, 22, 32, false, false, &ctx), "  mode 1 release needs state 2+");
    Check(ctl.nState == 1 && g_nClicks == 0, "  mode 1 release from state 1 does nothing");

    /* Release with no click handler still counts as handled. */
    HitControl(&ctl, 0, 1);
    ctl.pfnClick = NULL;
    LogReset();
    Check(UiControl_HitTest(&ctl, 22, 32, false, false, &ctx), "  release without a handler is handled");
    Check(ctl.nState == 0 && g_log.nBlit == 1, "  release still redraws without a handler");

    /* An unknown mode never releases. */
    HitControl(&ctl, 4, 1);
    LogReset();
    g_nClicks = 0;
    Check(!UiControl_HitTest(&ctl, 22, 32, false, false, &ctx), "  unknown mode never releases");
    Check(ctl.nState == 1 && g_nClicks == 0, "  unknown mode release changes nothing");

    Check(!UiControl_HitTest(NULL, 0, 0, true, true, &ctx), "  NULL control is safe");
    Check(!UiControl_HitTest(&ctl, 0, 0, true, true, NULL), "  NULL hit context is safe");
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
    TestHitTest();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
