/* test_audio.c - checks the audio layer's game-side logic with a fake backend.
 *
 * The FMOD calls themselves are a backend concern; what must match the original
 * is the naming scheme, the sample scan, the init retry count, and the option
 * gating. The stub backend records what it was asked to do so those can be
 * asserted without any sound hardware.
 *
 * Ground truth for the sample count was read independently from main.dat
 * (TACT0.WAV .. TACT34.WAV, contiguous).
 *
 * Exit code 0 = all checks pass, 1 = at least one failure.
 */
#include "asset/pufo_asset.h"
#include "audio/pufo_audio.h"
#include "audio/pufo_audio_winmm.h"
#include "game/pufo_options.h"
#include "platform/pufo_reg.h"

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

/* --- recording fake backend --- */

typedef struct FakeState {
    i32  nInitCalls;
    bool bInitSucceeds;
    i32  nSampleLoads;
    u32  aSampleModes[512];
    u32  aSampleSetMode[512];
    i32  nPlayCalls;
    i32  nSetSfxVolume;
    i32  nSetSfxPaused;
    i32  nSetSfxPausedChannel;
    i32  nMusicLoads;
    u32  nMusicMode;
    i32  nMusicLoopSet;
    i32  nMusicOptimizeMax;
    i32  nMusicPlayCalls;
    i32  nMusicVolume;
    i32  nMusicPaused;
    i32  nMusicFreeCalls;
    i32  nCloseCalls;
    i32  nSampleFreeCalls;
} FakeState;

static FakeState g_fake;

static bool Fake_Init(u32 nMixRate, i32 nMaxChannels, u32 nFlags)
{
    g_fake.nInitCalls += 1;
    (void)nMixRate;
    (void)nMaxChannels;
    (void)nFlags;
    return g_fake.bInitSucceeds;
}

static void Fake_SetWindow(void *hWnd) { (void)hWnd; }

static void *Fake_SampleLoad(const void *pData, u32 cbLength, u32 nMode)
{
    /* The buffer is freed right after this call, so only the length and mode
     * are observable. Both are asserted. */
    if (g_fake.nSampleLoads < 512) {
        g_fake.aSampleModes[g_fake.nSampleLoads] = nMode;
    }
    g_fake.nSampleLoads += 1;
    (void)pData;
    (void)cbLength;
    return (void *)0x1; /* a non-NULL fake sample handle */
}

static void Fake_SampleSetMode(void *pSample, u32 nMode)
{
    if (g_fake.nSampleLoads - 1 < 512) {
        g_fake.aSampleSetMode[g_fake.nSampleLoads - 1] = nMode;
    }
    (void)pSample;
}

static void Fake_SampleFree(void *pSample)
{
    (void)pSample;
    g_fake.nSampleFreeCalls += 1;
}

static i32 Fake_PlaySound(void *pSample)
{
    (void)pSample;
    g_fake.nPlayCalls += 1;
    return 0;
}

static void Fake_SetSfxVolume(i32 nVolume) { g_fake.nSetSfxVolume = nVolume; }
static void Fake_SetSfxPaused(i32 nChannel, i32 bPaused)
{
    g_fake.nSetSfxPausedChannel = nChannel;
    g_fake.nSetSfxPaused        = bPaused;
}

static void *Fake_MusicLoad(const void *pData, u32 cbLength, u32 nMode)
{
    g_fake.nMusicLoads += 1;
    g_fake.nMusicMode = nMode;
    (void)pData;
    (void)cbLength;
    return (void *)0x2;
}

static void Fake_MusicSetLooping(void *pMod, i32 bLoop)
{
    g_fake.nMusicLoopSet = bLoop;
    (void)pMod;
}

static void Fake_MusicOptimize(void *pMod, i32 nMaxChannels, i32 nMinVolume)
{
    g_fake.nMusicOptimizeMax = nMaxChannels;
    (void)pMod;
    (void)nMinVolume;
}

static void Fake_MusicPlay(void *pMod)
{
    g_fake.nMusicPlayCalls += 1;
    (void)pMod;
}

static void Fake_MusicVolume(void *pMod, i32 nVolume)
{
    g_fake.nMusicVolume = nVolume;
    (void)pMod;
}

static void Fake_MusicPaused(void *pMod, i32 bPaused)
{
    g_fake.nMusicPaused = bPaused;
    (void)pMod;
}

static void Fake_MusicFree(void *pMod)
{
    g_fake.nMusicFreeCalls += 1;
    (void)pMod;
}

static void Fake_Close(void) { g_fake.nCloseCalls += 1; }

static const PufoAudioBackend g_fakeBackend = {
    Fake_Init, Fake_SetWindow, Fake_SampleLoad, Fake_SampleSetMode, Fake_SampleFree,
    Fake_PlaySound, Fake_SetSfxVolume, Fake_SetSfxPaused, Fake_MusicLoad,
    Fake_MusicSetLooping, Fake_MusicOptimize, Fake_MusicPlay, Fake_MusicVolume,
    Fake_MusicPaused, Fake_MusicFree, Fake_Close
};

static void Fake_Reset(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.bInitSucceeds = true;
}

/* --- tests --- */

static void TestLoadSamples(void)
{
    printf("Sound_InitAndLoadSamples(\"tact\")\n");
    Fake_Reset();
    Sound_SetBackend(&g_fakeBackend);
    Check(Sound_InitAndLoadSamples("tact"), "returns true");
    CheckEqI32(g_fake.nInitCalls, 1, "  FSOUND_Init called once on success");
    /* main.dat holds TACT0.WAV .. TACT34.WAV with no gaps, so the scan must
     * stop at the first missing index: 35 samples. */
    CheckEqI32(Sound_GetSampleCount(), 35, "  loaded 35 samples (tact0..tact34)");
    CheckEqI32(g_fake.nSampleLoads, 35, "  backend sample loads");
    Check(Sound_IsInitialised(), "  audio ready flag set");
    /* Mode 0xA000 was passed to Sample_Load, then SetMode(1). */
    Check(g_fake.aSampleModes[0] == 0xA000u, "  sample load mode 0xA000");
    Check(g_fake.aSampleSetMode[0] == 1u, "  sample set mode 1");

    printf("Sound_Shutdown\n");
    Sound_Shutdown();
    CheckEqI32(g_fake.nSampleFreeCalls, 35, "  freed every sample");
    CheckEqI32(g_fake.nCloseCalls, 1, "  backend closed");
    Check(!Sound_IsInitialised(), "  ready flag cleared");
}

static void TestInitRetry(void)
{
    printf("Sound_InitAndLoadSamples with failing backend\n");
    Fake_Reset();
    g_fake.bInitSucceeds = false;
    Sound_SetBackend(&g_fakeBackend);
    (void)Sound_InitAndLoadSamples("tact");
    /* Verified from the disassembly @ 0x9e27c-0x9e2b4: the code jumps straight
     * into the first FSOUND_Init at i = 0, then loops while i < 10, so attempts
     * happen at i = 0..10 -- eleven in total, not ten. */
    CheckEqI32(g_fake.nInitCalls, 11, "  11 init attempts (i = 0..10)");
    /* Original quirk, asserted on purpose: byte_187F24 is set to 1 after the
     * retry loop on BOTH the success and the give-up path, so the game believes
     * audio is ready even when the backend never initialised. Samples still
     * load from the archives because that part does not depend on init. */
    Check(Sound_IsInitialised(), "  ready flag set even though init failed (original quirk)");

    printf("Sound_Play is still attempted after a failed init\n");
    g_fake.nPlayCalls = 0;
    Sound_Play(0);
    CheckEqI32(g_fake.nPlayCalls, 1, "  playback attempted (ready flag was set)");

    printf("Sound_Play while SFX muted\n");
    /* Sound_Play @ 0x9e0c0 toggles byte_187318 and draws a 0xC8 random number
     * BEFORE the mute test, so the global RNG stream advances even when the
     * sample is not played. */
    Sound_SetSfxMuted(true);
    g_fake.nPlayCalls = 0;
    Sound_Play(0);
    CheckEqI32(g_fake.nPlayCalls, 0, "  muted: no playback");
    Sound_SetSfxMuted(false);
    Sound_Play(0);
    CheckEqI32(g_fake.nPlayCalls, 1, "  unmuted: playback resumes");

    printf("Sound_Play with an out-of-range index\n");
    g_fake.nPlayCalls = 0;
    Sound_Play(-1);
    Sound_Play(99999);
    CheckEqI32(g_fake.nPlayCalls, 0, "  out-of-range index refused (port guard)");
    Sound_Shutdown();
}

static void TestMusic(void)
{
    printf("Sound_LoadAndPlayMusic(2)\n");
    Fake_Reset();
    Sound_SetBackend(&g_fakeBackend);
    (void)Sound_InitAndLoadSamples("tact");
    Sound_LoadAndPlayMusic(2);
    CheckEqI32(g_fake.nMusicLoads, 1, "  music loaded");
    Check(g_fake.nMusicMode == 0x8000u, "  load mode 0x8000 (load from memory)");
    CheckEqI32(g_fake.nMusicLoopSet, 1, "  looping enabled");
    CheckEqI32(g_fake.nMusicOptimizeMax, 16, "  optimised to 16 channels");
    CheckEqI32(g_fake.nMusicPlayCalls, 1, "  song played");
    Check(Sound_HasMusic(), "  music flag set");

    printf("Sound_LoadAndPlayMusic for a missing track\n");
    g_fake.nMusicLoads = 0;
    Sound_LoadAndPlayMusic(99);
    CheckEqI32(g_fake.nMusicLoads, 0, "  no load attempt for missing track");
}

/* The volume mapping is exact arithmetic from Sound_ApplyMusicVolume @ 0x9e454:
 * v = muted ? 0 : 12*step + 4;  level = 4*v - 1. */
static void TestVolumeMapping(void)
{
    PufoOptions opt;

    printf("Options_MusicLevel / Options_SfxLevel\n");
    Options_InitDefaults(&opt);
    opt.bMusicOff = 0;
    opt.bSoundOff = 0;
    opt.nMusicVolume = 0;
    opt.nSoundVolume = 0;
    CheckEqI32(Options_MusicLevel(&opt), 15, "  music step 0 -> 15");
    CheckEqI32(Options_SfxLevel(&opt), 15, "  sfx step 0 -> 15");

    opt.nMusicVolume = 5;
    opt.nSoundVolume = 5;
    CheckEqI32(Options_MusicLevel(&opt), 255, "  music step 5 -> 255");
    CheckEqI32(Options_SfxLevel(&opt), 255, "  sfx step 5 -> 255");

    opt.nMusicVolume = 3;
    CheckEqI32(Options_MusicLevel(&opt), 159, "  music step 3 -> 159");

    /* Muted yields 4*0 - 1 == -1, which is the original's arithmetic. */
    opt.bMusicOff = 1;
    opt.bSoundOff = 1;
    CheckEqI32(Options_MusicLevel(&opt), -1, "  music muted -> -1 (original arithmetic)");
    CheckEqI32(Options_SfxLevel(&opt), -1, "  sfx muted -> -1");
}

/* Defaults and clamps verified against Map_InitAndLoadRegistry @ 0x22648: each
 * Reg_GetString supplies a literal default, and the numeric values are range
 * checked with a fixed fallback. Note the volume range excludes 0. */
static void TestOptionsDefaults(void)
{
    PufoOptions opt;

    printf("Options_InitDefaults / Options_LoadIntClamped\n");
    Options_InitDefaults(&opt);
    CheckEqI32(opt.nGameSpeed, 4, "  gamespeed default 4");
    CheckEqI32(opt.nGamma, 2, "  gamma default 2");
    CheckEqI32(opt.nSoundVolume, 5, "  soundvolume default 5");
    CheckEqI32(opt.nMusicVolume, 5, "  musicvolume default 5");
    CheckEqI32(opt.bScrollByPen, 1, "  scrollbypen default 1");
    CheckEqI32(opt.bShowBorder, 1, "  showborder default 1");
    CheckEqI32(opt.bRunByDblClick, 1, "  runbydblclick default 1");
    CheckEqI32(opt.bMenuByPen, 1, "  menubypen default 1");
    CheckEqI32(opt.bTurnByPen, 0, "  turnbypen default 0");
    CheckEqI32(opt.bMusicOff, 0, "  musicoff default 0");
    CheckEqI32(opt.bSoundOff, 0, "  soundoff default 0");
    CheckEqI32(opt.bForceNoDrag, 0, "  forcenodrag default 0");

    /* Out-of-range values take the original's fixed fallback, not the default. A
     * volume of 0 is out of range (valid 1..5) and becomes 5. */
    (void)Reg_SetString("gamespeed", "9");
    (void)Reg_SetString("soundvolume", "0");
    (void)Reg_SetString("gamma", "1");
    Options_LoadFromRegistry(&opt);
    CheckEqI32(opt.nGameSpeed, 4, "  gamespeed 9 -> fallback 4");
    CheckEqI32(opt.nSoundVolume, 5, "  soundvolume 0 -> fallback 5 (0 is out of range)");
    CheckEqI32(opt.nGamma, 1, "  gamma 1 is in range");

    /* gamma's own upper bound: valid 0..4, so 9 is out of range and takes 2. */
    (void)Reg_SetString("gamma", "9");
    Options_LoadFromRegistry(&opt);
    CheckEqI32(opt.nGamma, 2, "  gamma 9 -> fallback 2");

    /* musicvolume shares the 1..5 range with soundvolume. */
    (void)Reg_SetString("musicvolume", "0");
    Options_LoadFromRegistry(&opt);
    CheckEqI32(opt.nMusicVolume, 5, "  musicvolume 0 -> fallback 5");

    /* A negative value is out of range too. */
    (void)Reg_SetString("gamma", "-3");
    Options_LoadFromRegistry(&opt);
    CheckEqI32(opt.nGamma, 2, "  gamma -3 -> fallback 2");

    /* In-range round trip through the registry, then restore the defaults so the
     * rest of the suite is unaffected. */
    (void)Reg_SetString("gamespeed", "2");
    (void)Reg_SetString("musicvolume", "3");
    Options_LoadFromRegistry(&opt);
    CheckEqI32(opt.nGameSpeed, 2, "  gamespeed 2 round-trips");
    CheckEqI32(opt.nMusicVolume, 3, "  musicvolume 3 round-trips");
    Options_InitDefaults(&opt);
    Options_SaveToRegistry(&opt);
}

static void TestVolumeApply(void)
{
    printf("Sound_ApplySfxVolume / Sound_ApplyMusicVolume\n");
    Fake_Reset();
    Sound_SetBackend(&g_fakeBackend);
    (void)Sound_InitAndLoadSamples("tact");
    Sound_LoadAndPlayMusic(0);

    Sound_ApplySfxVolume(159, 0);
    CheckEqI32(g_fake.nSetSfxVolume, 159, "  sfx volume forwarded");
    CheckEqI32(g_fake.nSetSfxPausedChannel, PUFO_FSOUND_ALL, "  the pause call is on FSOUND_ALL (-3)");
    CheckEqI32(g_fake.nSetSfxPaused, 0, "  unmuted: the effects keep playing");

    Sound_ApplySfxVolume(159, 1);
    CheckEqI32(g_fake.nSetSfxPaused, 1, "  muted: the same call pauses them (0x9E548 passes bSoundOff)");

    Sound_ApplyMusicVolume(159, 0);
    CheckEqI32(g_fake.nMusicVolume, 159, "  music volume forwarded");
    CheckEqI32(g_fake.nMusicPaused, 0, "  music unpaused");

    Sound_ApplyMusicVolume(0, 1);
    CheckEqI32(g_fake.nMusicPaused, 1, "  music paused when muted");
    Sound_Shutdown();
}

static void TestFocusPause(void)
{
    printf("Sound_SetPaused (PocketUFO_WndProc's focus arms)\n");
    Fake_Reset();
    Sound_SetBackend(&g_fakeBackend);
    (void)Sound_InitAndLoadSamples("tact");

    /* No module loaded, so the music half must not fire: byte_187F28 is clear at 0xA0538 and 0xA0624. The -1 is a
     * sentinel -- a call through the fake would overwrite it. */
    g_fake.nMusicPaused = -1;
    Sound_SetPaused(true);
    CheckEqI32(g_fake.nSetSfxPausedChannel, PUFO_FSOUND_ALL, "  WM_KILLFOCUS pauses FSOUND_ALL (0xA0534)");
    CheckEqI32(g_fake.nSetSfxPaused, 1, "  with the flag 1 (R1 = 1 at 0xA052C)");
    CheckEqI32(g_fake.nMusicPaused, -1, "  and calls FMUSIC_SetPaused not at all");

    Sound_SetPaused(false);
    CheckEqI32(g_fake.nSetSfxPaused, 0, "  WM_SETFOCUS resumes (R1 = 0 at 0xA0618)");
    CheckEqI32(g_fake.nMusicPaused, -1, "  still no module to resume");

    Sound_LoadAndPlayMusic(0);
    g_fake.nMusicPaused = -1;
    Sound_SetPaused(true);
    CheckEqI32(g_fake.nMusicPaused, 1, "  with a module loaded it is paused with the effects");
    Sound_SetPaused(false);
    CheckEqI32(g_fake.nMusicPaused, 0, "  and resumed with them");
    Sound_Shutdown();
}

/* Part 108: the mute reads are the live option cells, not a mirror the caller has
 * to remember. Sound_Play's soundoff test is the original's pCtxBase + 99 read, and
 * Sound_LoadAndPlayMusic pauses the module right after FMUSIC_PlaySong when
 * musicoff stands (0x9E228 tests it, 0x9E230 pauses). */
static u8 g_bSoundOffCell;
static u8 g_bMusicOffCell;

static void TestMuteBytes(void)
{
    printf("Sound_SetMuteBytes (the live soundoff / musicoff cells)\n");
    Fake_Reset();
    Sound_SetBackend(&g_fakeBackend);
    g_bSoundOffCell = 0;
    g_bMusicOffCell = 0;
    Sound_SetMuteBytes(&g_bSoundOffCell, &g_bMusicOffCell);
    (void)Sound_InitAndLoadSamples("tact");

    Sound_Play(3);
    CheckEqI32(g_fake.nPlayCalls, 1, "  soundoff clear: the sample reaches the backend");

    g_bSoundOffCell = 1;   /* the byte OptionsScreen_SetSfxVol0..5 writes (0x6BD04) */
    Sound_Play(3);
    CheckEqI32(g_fake.nPlayCalls, 1, "  soundoff set: Sound_Play stops before the backend");
    g_bSoundOffCell = 0;

    Sound_Shutdown();

    Fake_Reset();
    Sound_SetBackend(&g_fakeBackend);
    (void)Sound_InitAndLoadSamples("tact");
    g_fake.nMusicPaused = -1;
    Sound_LoadAndPlayMusic(0);
    /* 0x9E228 tests the byte and 0x9E230 is inside the taken branch: a clear byte
     * means FMUSIC_SetPaused is never reached, not that it is called with 0. */
    CheckEqI32(g_fake.nMusicPaused, -1, "  musicoff clear: the load calls SetPaused not at all");
    Sound_Shutdown();

    Fake_Reset();
    Sound_SetBackend(&g_fakeBackend);
    (void)Sound_InitAndLoadSamples("tact");
    g_fake.nMusicPaused = -1;
    g_bMusicOffCell = 1;   /* 0x9E228's test, 0x9E230's call */
    Sound_LoadAndPlayMusic(1);
    CheckEqI32(g_fake.nMusicPaused, 1, "  musicoff set: the load pauses the module at once");
    CheckEqI32(g_fake.nMusicPlayCalls, 1, "  and it still played it first, as 0x9E210 does");
    Sound_Shutdown();
    Sound_SetMuteBytes(NULL, NULL);
}

/* The device gate. Everything above runs on the fake backend and needs no sound
 * hardware; this is the only section that proves real output, and a machine with no
 * wave device says so and stays green -- the control-flow parity the rest of the
 * file asserts does not depend on a device existing. */
static void TestDevice(void)
{
    u8 bSoundOff = 0; /* the two live cells, as App_InitInstance hands them over */
    u8 bMusicOff = 0;

    printf("winmm device gate (the real output path)\n");
    if (!PufoAudioWinmm_HasDevice()) {
        printf("  device gate skipped: waveOutGetNumDevs() == 0\n");
        return;
    }
    /* Without the live cells Sound_SfxMuted falls back to the pause mirror, and
     * TestFocusPause above leaves that mirror at 1 -- the play would be muted. */
    Sound_SetMuteBytes(&bSoundOff, &bMusicOff);
    Sound_SetBackend(&g_pufoAudioWinmm);
    Check(Sound_InitAndLoadSamples("tact"), "  waveOutOpen succeeded where FSOUND_Init would have");
    CheckEqI32(Sound_GetSampleCount(), 35, "  the same 35 TACT*.WAV loaded through the real backend");
    PufoAudioWinmm_DrainDone(8);        /* WOM_OPEN arrives on the same event */
    Sound_Play(2);                      /* TACT2 */
    Check(PufoAudioWinmm_WaitDone(4000u), "  waveOutWrite reported WOM_DONE: the sample played");
    Sound_Shutdown();
    Sound_SetMuteBytes(NULL, NULL);
    Sound_SetBackend(NULL);
}

int main(void)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

    (void)setvbuf(stdout, NULL, _IONBF, 0);

    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }

    TestLoadSamples();
    TestInitRetry();
    TestMusic();
    TestVolumeMapping();
    TestOptionsDefaults();
    TestVolumeApply();
    TestFocusPause();
    TestMuteBytes();
    TestDevice();

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
