/* test_menu.c - checks the options-menu button handlers.
 *
 * The interesting assertions are the per-group differences: the music group plays
 * no sample at all, and the sfx group plays its sample after applying the volume
 * rather than before. All four groups must keep their pads one-hot.
 */
#include "game/pufo_menu.h"

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

/* --- recording hooks: the call ORDER matters, so keep a trace --------------- */

#define TRACE_MAX 16

typedef struct Trace {
    char aEvent[TRACE_MAX][24];
    i32  nEvents;
    bool bAppliedMusic;
    bool bAppliedSfx;
    bool bPalettes;
    bool bEndTurn;
    bool bResize;
} Trace;

static Trace g_trace;

static void TraceAdd(const char *pszEvent)
{
    if (g_trace.nEvents < TRACE_MAX) {
        snprintf(g_trace.aEvent[g_trace.nEvents], sizeof(g_trace.aEvent[0]), "%s",
                 pszEvent);
    }
    g_trace.nEvents += 1;
}

static void TraceReset(void)
{
    memset(&g_trace, 0, sizeof(g_trace));
}

static bool TraceAt(i32 nIndex, const char *pszWant)
{
    if (nIndex < 0 || nIndex >= g_trace.nEvents || nIndex >= TRACE_MAX) {
        return false;
    }
    return strcmp(g_trace.aEvent[nIndex], pszWant) == 0;
}

static void H_Play(i32 nSample, void *pUser)
{
    (void)pUser;
    if (nSample == PUFO_MENU_SAMPLE_PRE) {
        TraceAdd("sample17");
    } else if (nSample == PUFO_MENU_SAMPLE_POST) {
        TraceAdd("sample16");
    } else {
        TraceAdd("sample?");
    }
}

static void H_ApplyMusic(void *pUser)
{
    (void)pUser;
    g_trace.bAppliedMusic = true;
    TraceAdd("applyMusic");
}

static void H_ApplySfx(void *pUser)
{
    (void)pUser;
    g_trace.bAppliedSfx = true;
    TraceAdd("applySfx");
}

static void H_LoadPalettes(void *pUser)
{
    (void)pUser;
    g_trace.bPalettes = true;
    TraceAdd("palettes");
}

static const MenuHooks g_hooks = {
    H_Play, H_ApplyMusic, H_ApplySfx, H_LoadPalettes, NULL
};

static void H_BeginEnemyTurn(void *pUser)
{
    (void)pUser;
    g_trace.bEndTurn = true;
    TraceAdd("beginEnemyTurn");
}

static void H_ResizeWindow(void *pUser)
{
    (void)pUser;
    g_trace.bResize = true;
    TraceAdd("resize");
}

/* Counts the set entries in one pad group. */
static i32 PadSum(const u8 *pPads)
{
    i32 n = 0;

    for (i32 i = 0; i < PUFO_MENU_STEPS; ++i) {
        n += pPads[i] != 0 ? 1 : 0;
    }
    return n;
}

/* Returns the lit index of a one-hot group, or -1. */
static i32 PadLit(const u8 *pPads)
{
    for (i32 i = 0; i < PUFO_MENU_STEPS; ++i) {
        if (pPads[i] != 0) {
            return i;
        }
    }
    return -1;
}

static void TestSpeed(void)
{
    PufoOptions opt;
    GameClock   clock;
    MenuPads    pads;

    printf("Menu_SetSpeed\n");
    for (i32 step = 0; step < PUFO_MENU_STEPS; ++step) {
        memset(&opt, 0, sizeof(opt));
        memset(&clock, 0, sizeof(clock));
        memset(&pads, 0, sizeof(pads));
        /* Pre-light every pad to prove the handler clears them. */
        memset(pads.abSpeed, 1, sizeof(pads.abSpeed));
        TraceReset();

        Check(Menu_SetSpeed(&opt, &clock, &pads, step, &g_hooks), "  speed applied");
        CheckEqI32(clock.nSpeed, (u32)step, "  clock speed stored");
        CheckEqI32(PadSum(pads.abSpeed), 1, "  speed pads are one-hot");
        CheckEqI32(PadLit(pads.abSpeed), step, "  the right speed pad is lit");
        Check(TraceAt(0, "sample17"), "  sample 17 plays first");
        CheckEqI32(PadSum(pads.abGamma), 0, "  gamma pads untouched");
        CheckEqI32(PadSum(pads.abMusic), 0, "  music pads untouched");
        CheckEqI32(PadSum(pads.abSfx), 0, "  sfx pads untouched");
    }

    memset(&opt, 0, sizeof(opt));
    memset(&clock, 0, sizeof(clock));
    memset(&pads, 0, sizeof(pads));
    Check(!Menu_SetSpeed(&opt, &clock, &pads, 5, &g_hooks), "  step 5 refused");
    Check(!Menu_SetSpeed(&opt, &clock, &pads, -1, &g_hooks), "  step -1 refused");
    Check(!Menu_SetSpeed(NULL, &clock, &pads, 0, &g_hooks), "  NULL options refused");
}

static void TestGamma(void)
{
    PufoOptions opt;
    MenuPads    pads;

    printf("Menu_SetGamma -- writes gamma then rebuilds palettes\n");
    for (i32 step = 0; step < PUFO_MENU_STEPS; ++step) {
        memset(&opt, 0, sizeof(opt));
        memset(&pads, 0, sizeof(pads));
        TraceReset();

        Check(Menu_SetGamma(&opt, &pads, step, &g_hooks), "  gamma applied");
        CheckEqI32(opt.nGamma, step, "  gamma stored in the options block");
        CheckEqI32(PadLit(pads.abGamma), step, "  the right gamma pad is lit");
        Check(g_trace.bPalettes, "  palettes were rebuilt");
        Check(TraceAt(0, "sample17"), "  sample 17 plays first");
        Check(TraceAt(1, "palettes"), "  palettes are rebuilt after the sample");
    }

    memset(&opt, 0, sizeof(opt));
    memset(&pads, 0, sizeof(pads));
    Check(!Menu_SetGamma(&opt, &pads, 5, &g_hooks), "  step 5 refused");
}

static void TestMusicVolume(void)
{
    PufoOptions opt;
    MenuPads    pads;

    printf("Menu_SetMusicVolume -- the ONLY group that plays no sample\n");
    for (i32 step = 0; step < PUFO_MENU_STEPS; ++step) {
        memset(&opt, 0, sizeof(opt));
        memset(&pads, 0, sizeof(pads));
        TraceReset();

        Check(Menu_SetMusicVolume(&opt, &pads, step, &g_hooks), "  volume applied");
        CheckEqI32(opt.nMusicVolume, step + 1, "  music volume stored as 1..5");
        CheckEqI32(PadLit(pads.abMusic), step, "  the right music pad is lit");
        Check(g_trace.bAppliedMusic, "  the volume was applied");
        CheckEqI32(g_trace.nEvents, 1, "  exactly one event happened");
        Check(TraceAt(0, "applyMusic"), "  and it was the volume apply, not a sample");
    }

    /* The pad index is 0-based even though the original names these 1..5. */
    memset(&opt, 0, sizeof(opt));
    memset(&pads, 0, sizeof(pads));
    Check(Menu_SetMusicVolume(&opt, &pads, 0, &g_hooks), "  step 0 is valid (named Vol1)");
    CheckEqI32(PadLit(pads.abMusic), 0, "  step 0 lights pad index 0");
    CheckEqI32(opt.nMusicVolume, 1, "  Vol1 stores the immediate 1");
    /* The original's arithmetic, Sound_ApplyMusicVolume @ 0x9E454:
     * value = muted ? 0 : 12*volume + 4, level = 4*value - 1. */
    CheckEqI32(Options_MusicLevel(&opt), 4 * (12 * 1 + 4) - 1, "  volume 1 gives level 63");
    opt.bMusicOff = 1;
    CheckEqI32(Options_MusicLevel(&opt), -1, "  muted gives -1, not 0");
}

static void TestSfxVolume(void)
{
    PufoOptions opt;
    MenuPads    pads;

    printf("Menu_SetSfxVolume -- applies the volume BEFORE the sample\n");
    for (i32 step = 0; step < PUFO_MENU_STEPS; ++step) {
        memset(&opt, 0, sizeof(opt));
        memset(&pads, 0, sizeof(pads));
        TraceReset();

        Check(Menu_SetSfxVolume(&opt, &pads, step, &g_hooks), "  volume applied");
        CheckEqI32(opt.nSoundVolume, step + 1, "  sound volume stored as 1..5");
        CheckEqI32(PadLit(pads.abSfx), step, "  the right sfx pad is lit");
        CheckEqI32(g_trace.nEvents, 2, "  two events happened");
        Check(TraceAt(0, "applySfx"), "  the volume apply comes FIRST");
        Check(TraceAt(1, "sample16"), "  sample 16 comes LAST");
    }
}

static void TestPadsAreIndependent(void)
{
    PufoOptions opt;
    GameClock   clock;
    MenuPads    pads;

    printf("The four pad groups are independent\n");
    memset(&opt, 0, sizeof(opt));
    memset(&clock, 0, sizeof(clock));
    memset(&pads, 0, sizeof(pads));

    (void)Menu_SetSpeed(&opt, &clock, &pads, 2, &g_hooks);
    (void)Menu_SetGamma(&opt, &pads, 3, &g_hooks);
    (void)Menu_SetMusicVolume(&opt, &pads, 1, &g_hooks);
    (void)Menu_SetSfxVolume(&opt, &pads, 4, &g_hooks);

    CheckEqI32(PadLit(pads.abSpeed), 2, "  speed pad still 2");
    CheckEqI32(PadLit(pads.abGamma), 3, "  gamma pad still 3");
    CheckEqI32(PadLit(pads.abMusic), 1, "  music pad still 1");
    CheckEqI32(PadLit(pads.abSfx), 4, "  sfx pad still 4");
    CheckEqI32(clock.nSpeed, 2, "  clock speed still 2");

    /* And the option block ended with every group's value. */
    CheckEqI32(opt.nGamma, 3, "  gamma 3");
    CheckEqI32(opt.nMusicVolume, 2, "  music 2 (step 1 + 1)");
    CheckEqI32(opt.nSoundVolume, 5, "  sfx 5 (step 4 + 1)");
}

static void TestButtons(void)
{
    printf("Menu_EndTurn / Menu_ResizeWindow\n");
    TraceReset();
    Menu_EndTurn(&g_hooks, H_BeginEnemyTurn);
    Check(g_trace.bEndTurn, "  EndTurn began the enemy turn");
    Check(TraceAt(0, "sample17"), "  EndTurn plays sample 17 first");
    Check(TraceAt(1, "beginEnemyTurn"), "  then begins the turn");

    TraceReset();
    Menu_ResizeWindow(&g_hooks, H_ResizeWindow);
    Check(g_trace.bResize, "  ResizeWindow resized");
    Check(TraceAt(0, "sample16"), "  ResizeWindow plays sample 16");

    /* A NULL callback must not crash. */
    TraceReset();
    Menu_EndTurn(&g_hooks, NULL);
    Menu_ResizeWindow(&g_hooks, NULL);
    Check(true, "  NULL callbacks are safe");
}

static void TestMuteToggles(void)
{
    PufoOptions opt;
    u8 nMusicPad = 9;
    u8 nSfxPad = 9;

    printf("Menu_ToggleMusicMute / Menu_ToggleSfxMute\n");
    Options_InitDefaults(&opt);
    opt.bMusicOff = 0;
    opt.bSoundOff = 0;

    /* Ui_ToggleMusicMute @ 0x19948: the flag is flipped, Sound_ApplyMusicVolume is called on BOTH arms
     * (0x19974 on the clear arm, 0x19994 on the set arm), and the new value lands in the pad byte last
     * (STRB at 0x199A0, into unk_AD05A -- a byte at pBank + 0x98A). The port hands that byte in as
     * pPad. The flag stores are 0x19970 and 0x19990. */
    TraceReset();
    Check(Menu_ToggleMusicMute(&opt, &nMusicPad, &g_hooks), "  music mute turns on @ 0x19948");
    CheckEqI32(opt.bMusicOff, 1, "  music flag set @ 0x19948");
    CheckEqI32(nMusicPad, 1, "  the pad byte mirrors the new flag @ 0x19948");
    Check(TraceAt(g_trace.nEvents - 1, "applyMusic"), "  music mute applies music volume @ 0x19948");

    Check(!Menu_ToggleMusicMute(&opt, &nMusicPad, &g_hooks), "  music mute turns off @ 0x19948");
    CheckEqI32(opt.bMusicOff, 0, "  music flag cleared");
    CheckEqI32(nMusicPad, 0, "  the pad byte mirrors the new flag @ 0x19948");

    /* Ui_ToggleSfxMute @ 0x199ac is the same shape on Map_bSoundOff with Sound_ApplySfxVolume and the
     * pad byte unk_AD07A. */
    TraceReset();
    Check(Menu_ToggleSfxMute(&opt, &nSfxPad, &g_hooks), "  sfx mute turns on @ 0x199ac");
    CheckEqI32(opt.bSoundOff, 1, "  sfx flag set @ 0x199ac");
    CheckEqI32(nSfxPad, 1, "  the pad byte mirrors the new flag @ 0x199ac");
    Check(TraceAt(g_trace.nEvents - 1, "applySfx"), "  sfx mute applies sfx volume @ 0x199ac");

    /* NULL option and NULL pad must be safe. */
    Check(!Menu_ToggleMusicMute(NULL, NULL, &g_hooks), "  NULL options refused");
    (void)Menu_ToggleSfxMute(&opt, NULL, NULL);

    /* ForceNoDrag: the pad is the INVERSE of the new value. */
    {
        u8 nPad = 9;
        opt.bForceNoDrag = 0;
        Check(Menu_ToggleForceNoDrag(&opt, &nPad), "  forcenodrag turns on");
        CheckEqI32(opt.bForceNoDrag, 1, "  flag set");
        CheckEqI32(nPad, 0, "  pad is 0 when turning ON");
        Check(!Menu_ToggleForceNoDrag(&opt, &nPad), "  forcenodrag turns off");
        CheckEqI32(opt.bForceNoDrag, 0, "  flag cleared");
        CheckEqI32(nPad, 1, "  pad is 1 when turning OFF");
        Check(!Menu_ToggleForceNoDrag(NULL, NULL), "  NULL options refused");
    }
}

/* Options_SyncUiFromMap_GotoScreen12 @ 0x188e4: the options button changes no
 * option, it re-reads the option block into all 29 pad bytes and opens screen 12. */
static void TestSyncOptionPads(void)
{
    PufoOptions opt;
    MenuPads    pads;
    u16         nScreen = 0u;

    printf("Menu_SyncOptionPads (0x188e4)\n");

    TraceReset();
    Options_InitDefaults(&opt);
    memset(&pads, 0xFF, sizeof(pads));
    Menu_SyncOptionPads(&pads, &opt, &nScreen, &g_hooks);

    Check(TraceAt(0, "sample16"), "  sample 16 first");
    CheckEqI32((i32)nScreen, 12, "  screen 12 opens");
    /* Defaults: gamespeed 4, gamma 2, soundvolume 5, musicvolume 5. */
    CheckEqI32(PadLit(pads.abSpeed), 4, "  speed pad 4 lit");
    CheckEqI32(PadLit(pads.abGamma), 2, "  gamma pad 2 lit");
    CheckEqI32(PadLit(pads.abMusic), 4, "  music pad 4 lit (volume 5)");
    CheckEqI32(PadLit(pads.abSfx), 4, "  sfx pad 4 lit (volume 5)");
    CheckEqI32(PadSum(pads.abSpeed) + PadSum(pads.abGamma)
             + PadSum(pads.abMusic) + PadSum(pads.abSfx), 4, "  every group is one-hot");
    CheckEqI32(pads.bConfirm, 0, "  the confirm pad of widget 75 stays dark");

    /* The seven flag pads and the inverted forcenodrag pad. */
    memset(&pads, 0, sizeof(pads));
    opt.bScrollByPen = 0; opt.bShowBorder = 1; opt.bRunByDblClick = 0;
    opt.bTurnByPen = 1;   opt.bMenuByPen = 0;
    opt.bMusicOff = 1;    opt.bSoundOff = 0;
    opt.bForceNoDrag = 1;
    Menu_SyncOptionPads(&pads, &opt, &nScreen, &g_hooks);
    CheckEqI32(pads.abPen[0] + pads.abPen[1] + pads.abPen[2] + pads.abPen[3]
               + pads.abPen[4], 2, "  pen flags mirror the option bytes");
    CheckEqI32(pads.bMusicMute, 1, "  music mute pad follows musicoff");
    CheckEqI32(pads.bSfxMute, 0, "  sfx mute pad follows soundoff");
    CheckEqI32(pads.bForceNoDrag, 0, "  forcenodrag pad is INVERTED");

    memset(&pads, 0xFF, sizeof(pads));
    opt.bForceNoDrag = 0;
    Menu_SyncOptionPads(&pads, &opt, &nScreen, &g_hooks);
    CheckEqI32(pads.bForceNoDrag, 1, "  a clear forcenodrag lights its pad");

    /* Out-of-range values: the original writes past the group, the port does not. */
    memset(&pads, 0, sizeof(pads));
    opt.nGameSpeed = 9;
    opt.nMusicVolume = 0;
    Menu_SyncOptionPads(&pads, &opt, &nScreen, &g_hooks);
    CheckEqI32(PadSum(pads.abSpeed), 0, "  speed 9 lights nothing");
    CheckEqI32(PadSum(pads.abMusic), 0, "  volume 0 lights nothing");
    CheckEqI32(pads.bSfxMute, 0, "  and does not touch the mute pad");

    Menu_SyncOptionPads(NULL, &opt, &nScreen, &g_hooks);
    Menu_SyncOptionPads(&pads, NULL, &nScreen, &g_hooks);
    Menu_SyncOptionPads(&pads, &opt, NULL, NULL);
    Check(true, "  NULL arguments are safe");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestSpeed();
    TestGamma();
    TestMusicVolume();
    TestSfxVolume();
    TestPadsAreIndependent();
    TestButtons();
    TestMuteToggles();
    TestSyncOptionPads();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
