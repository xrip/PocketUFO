/* test_control.c - checks the UiControl widget's mode/state machine.
 *
 * The clamp in mode 1 is the load-bearing detail: nState is a u8 but only pads 0..2
 * exist, so a state above 2 would index past the array. The test pins the clamp, the
 * inert mode 2, and the disabled case.
 */
#include "ui/pufo_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

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
    /* UiControl_ctor @ 0x1a28c writes nine fields and reaches no tenth: nMode 0, nState 0, x 0, y 0,
     * uWidth and uHeight both from the same literal (MOV R2,#0xA at 0x1A290, STRH at 0x1A2A8 and 0x1A2AC),
     * bEnabled 1 at 0x1A2B0, bUnk17 0 at 0x1A2B4, and the three pad pointers at +0x14/+0x18/+0x1C to NULL
     * (0x1A2B8, 0x1A2BC, 0x1A2C0). pfnClick at +12 is NOT among them -- the original leaves it to whoever
     * installs the handler, the port's memset clears it, which is the stated deviation. */
    CheckEqI32(ctl.x, 0, "  x zeroed @ 0x1a28c");
    CheckEqI32(ctl.y, 0, "  y zeroed @ 0x1a28c");
    CheckEqI32(ctl.uWidth, 10, "  width defaults to 10 @ 0x1a28c");
    CheckEqI32(ctl.uHeight, 10, "  height defaults to 10, not the width @ 0x1a28c");
    CheckEqI32(ctl.bEnabled, 1, "  enabled by default @ 0x1a28c");
    CheckEqI32(ctl.nMode, 0, "  mode 0 @ 0x1a28c");
    CheckEqI32(ctl.nState, 0, "  state 0 @ 0x1a28c");
    Check(ctl.pfnClick == NULL, "  no click handler (the original leaves the slot alone) @ 0x1a28c");
    Check(ctl.bUnk17 == 0, "  bUnk17 clear @ 0x1a28c");
    for (i32 i = 0; i < PUFO_CONTROL_PADS; ++i) {
        Check(ctl.apPads[i] == NULL, "  pad slot NULL @ 0x1a28c");
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
    /* UiControl_BlitActivePad @ 0x1a340, the choice half: after the pad is picked, the original stores
     * pad->nOriginY = ctl->y (`STR R3,[R1,#4]` at 0x1A3A4) and pad->nOriginX = ctl->x (`STR R3,[R1]` at
     * 0x1A3B0), then branches on bUnk17 (CMP R3,#0 at 0x1A3BC) -- set means `PadImage_BlitEx(pad, 0)`
     * (the call at 0x1A3C8, the key a literal 0), clear means `PadImage_Blit(pad)` (0x1A3D0). */
    (void)UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL);
    CheckEqI32(g_log.nBlitEx, 1, "  bUnk17 selects the extended blit @ 0x1a340");
    CheckEqI32(g_log.nBlit, 0, "  the plain blit was not used @ 0x1a340");
    CheckEqI32(g_log.nLastOriginX, 42, "  the pad's origin x follows the control @ 0x1a340");
    CheckEqI32(g_log.nLastOriginY, -7, "  the pad's origin y follows the control @ 0x1a340");

    ctl.bUnk17 = 0;
    LogReset();
    (void)UiControl_BlitActivePad(&ctl, OnBlit, OnBlitEx, NULL);
    CheckEqI32(g_log.nBlit, 1, "  bUnk17 clear selects the plain blit @ 0x1a340");
    CheckEqI32(g_log.nBlitEx, 0, "  ... and not the keyed one @ 0x1a340");

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
    /* UiControl_SetPosMode @ 0x1a5c0: y first (`STRH R2,[R0,#2]` at 0x1A5C8), then the mode test
     * `CMP R2,#2` at 0x1A5D8, which turns the size block into conditional instructions: x is `STRH
     * R1,[R0]` at 0x1A5DC (unconditional), the pad-1 pointer is `LDRNE R1,[R0,#0x18]` at 0x1A5E0, its
     * uWidth read from its +8 and written at 0x1A5E8, its uHeight read from its +0xA at 0x1A5EC and
     * written by `STRHNE [R0,#6]` at 0x1A5F8 -- the height store still carries the NE condition that
     * survived the nMode store at 0x1A5F0. nState is cleared last at 0x1A5FC, whatever the mode. So mode
     * 2 writes x, y, nMode and nState and touches neither size. There is no NULL test on pad 1: the
     * original faults on a control with no second pad. */
    UiControl_SetPosMode(&ctl, 11, 22, 0);
    CheckEqI32(ctl.x, 11, "  x set @ 0x1a5c0");
    CheckEqI32(ctl.y, 22, "  y set @ 0x1a5c0");
    CheckEqI32(ctl.nMode, 0, "  mode set @ 0x1a5c0");
    CheckEqI32(ctl.nState, 0, "  state reset to 0 @ 0x1a5c0");
    CheckEqI32(ctl.uWidth, 11, "  width adopted from pad 1 @ 0x1a5c0");
    CheckEqI32(ctl.uHeight, 21, "  height adopted from pad 1's +10, not its +8 @ 0x1a5c0");

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

    /* UiControl_Setup @ 0x1a75c is exactly two calls in a fixed order: the three name pointers are read
     * out of the descriptor at +4, +0xD and +0x16 (0x1A774, 0x1A770, 0x1A76C), `BL UiControl_LoadPads`
     * at 0x1A77C runs FIRST, and only then are the two coordinates formed -- `ADD R1,R6,R1` for
     * xBase + xOff at 0x1A794 and `ADD R2,R7,R2` for yBase + yOff at 0x1A790 -- and handed to
     * UiControl_SetPosMode. Loading the pads before the size is set is what lets SetPosMode adopt the
     * freshly loaded pad's dimensions. */
    UiControl_Setup(&ctl, 100, 200, &desc,
                    (bool (*)(UiControl *, const char *, const char *, const char *,
                              void *))NULL,
                    NULL);
    (void)ctx;
    CheckEqI32(ctl.x, 105, "  x is base + desc offset @ 0x1a75c");
    CheckEqI32(ctl.y, 197, "  y is base + desc offset, negative offset works @ 0x1a75c");
    CheckEqI32(ctl.nMode, 1, "  mode comes from the description @ 0x1a75c");
    CheckEqI32(ctl.nState, 0, "  state reset, through SetPosMode @ 0x1a75c");
    CheckEqI32(ctl.uWidth, 11, "  and mode 1 adopts the pad size Setup loaded first @ 0x1a75c");

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

static void Click_ClearListMode(void *pUser)
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
    /* EmptyBtn_OnClick @ 0x1a228 plays one sample and then clears one dword, in that order:
     * `MOV R0,#0x10` at 0x1A230 with `BL Sound_Play` at 0x1A234, then `LDR R0,=MainMenu_nListMode` at
     * 0x1A238 and `MOV R1,#0` + `STR R1,[R0]` at 0x1A23C/0x1A240. It reaches nothing else. */
    EmptyBtn_OnClick(Click_PlaySample, Click_ClearListMode, NULL);
    CheckEqI32(g_nClickSamples, 1, "  one sample played @ 0x1a228");
    CheckEqI32(g_nClickLastSample, PUFO_CONTROL_SAMPLE_CLICK, "  sample index 16, the 0x10 literal @ 0x1a228");
    CheckEqI32(g_nClickCleared, 1, "  the draw-mode dword was cleared @ 0x1a228");

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

static void TestBankAll(void)
{
    static UiWidgetBank bank;
    static i32 aSeen[PUFO_PBANK_WIDGET_COUNT];
    bool bAction = true;
    bool bOk;
    i32 nTotal = 0;

    printf("UiBank_CtorAll / UiBank_FreeAllPads\n");

    /* The fourteen groups tile the 130 slots exactly once. */
    for (i32 i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        aSeen[i] = 0;
    }
    for (i32 g = 0; g < PUFO_PBANK_SPAN_COUNT; ++g) {
        for (i32 k = 0; k < (i32)g_aUiBankSpans[g].nCount; ++k) {
            i32 nIdx = (i32)g_aUiBankSpans[g].nFirst + k;
            Check(nIdx >= 0 && nIdx < PUFO_PBANK_WIDGET_COUNT, "  group index in range");
            aSeen[nIdx] += 1;
        }
        nTotal += (i32)g_aUiBankSpans[g].nCount;
    }
    CheckEqI32(nTotal, PUFO_PBANK_WIDGET_COUNT, "  the groups total 130");
    bOk = true;
    for (i32 i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        if (aSeen[i] != 1) {
            bOk = false;
        }
    }
    Check(bOk, "  every slot is covered exactly once");

    /* Construct: the original dirties nothing, so start from a dirty bank and check
     * that all 130 come out as the ctor defaults. */
    memset(&bank, 0xFF, sizeof(bank));
    Check(UiBank_CtorAll(&bank, &bAction) == &bank, "  returns the bank");
    CheckEqI32((i32)bank.bEscapeFlag, 0, "  escape flag at bank+0x1042 cleared");
    Check(!bAction, "  UiBank_bActionFlag action flag cleared");
    bOk = true;
    for (i32 i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        if (bank.aCtl[i].uWidth != 10 || bank.aCtl[i].uHeight != 10 ||
            bank.aCtl[i].bEnabled != 1 || bank.aCtl[i].pfnClick != NULL) {
            bOk = false;
        }
    }
    Check(bOk, "  all 130 widgets are 10x10 enabled with no handler");

    /* FreeAllPads walks the same groups backwards, so every slot must be visited. */
    bOk = true;
    for (i32 i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        PadImage *pPad = (PadImage *)malloc(sizeof(*pPad));
        if (pPad != NULL) {
            PadImage_ctor(pPad);
        } else {
            bOk = false;
        }
        bank.aCtl[i].apPads[0] = pPad;
    }
    Check(bOk, "  every bank slot has an owned pad for the release sweep");
    UiBank_FreeAllPads(&bank);
    bOk = true;
    for (i32 i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        if (bank.aCtl[i].apPads[0] != NULL) {
            bOk = false;
        }
    }
    Check(bOk, "  FreeAllPads visited all 130 widgets");

    /* NULL is tolerated by both. */
    Check(UiBank_CtorAll(NULL, &bAction) == NULL, "  NULL bank returns NULL");
    UiBank_FreeAllPads(NULL);
    Check(true, "  NULL bank is tolerated by FreeAllPads");
}

/* UiControl_FreePads @ 0x1a2c8 visits all three pad slots, each with its own NULL test
 * (LDR the slot, CMP #0, skip): 0x1A2D0/0x1A2D4 for pad 0, 0x1A2F4/0x1A2F8 for pad 1, 0x1A318/0x1A31C for
 * pad 2. Each visited pad gets `BL PadImage_FreeData` first (0x1A2E8, 0x1A30C, 0x1A330) and then the pad
 * record itself is operator-deleted. The port additionally clears the slot. */
static void TestFreePads(void)
{
    UiControl  ctl;
    PadImage  *apPlanted[3];
    i32        i;
#if defined(_DEBUG)
    _CrtMemState before, after, difference;
#endif

    printf("UiControl_FreePads\n");
    UiControl_ctor(&ctl);
#if defined(_DEBUG)
    _CrtMemCheckpoint(&before);
#endif
    for (i = 0; i < 3; ++i) {
        apPlanted[i] = (PadImage *)malloc(sizeof(PadImage));
        if (apPlanted[i] == NULL) {
            Check(false, "  owned pad allocation succeeds");
            continue;
        }
        PadImage_ctor(apPlanted[i]);
        apPlanted[i]->pData  = (u8 *)malloc(16);
        apPlanted[i]->uWidth = (u16)(4 + i);
        ctl.apPads[i] = apPlanted[i];
    }
    UiControl_FreePads(&ctl);
#if defined(_DEBUG)
    _CrtMemCheckpoint(&after);
    Check(_CrtMemDifference(&difference, &before, &after) == 0,
          "  release returns both pad records and pixel buffers to the heap");
#endif
    Check(ctl.apPads[0] == NULL && ctl.apPads[1] == NULL && ctl.apPads[2] == NULL,
          "  every one of the three slots is cleared @ 0x1a2c8");

    /* Empty slots are skipped by the three tests, and a NULL control is refused. */
    UiControl_FreePads(NULL);
    UiControl_ctor(&ctl);
    UiControl_FreePads(&ctl);
    Check(true, "  a NULL control and three empty slots are safe @ 0x1a2c8");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestCtor();
    TestFreePads();
    TestModeStateSelection();
    TestBlitExAndOrigin();
    TestSetPosMode();
    TestSetupAndClick();
    TestEmptyBtnClick();
    TestHitTest();
    TestBankAll();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
