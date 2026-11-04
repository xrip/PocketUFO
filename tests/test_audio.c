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

#include <windows.h>

#include <libopenmpt/libopenmpt.h>

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

/* --- recording fake backend --- */

typedef struct FakeState {
    i32  nInitCalls;
    bool bInitSucceeds;
    i32  nSampleLoads;
    bool bNullSample18;
    u32  aSampleModes[512];
    u32  aSampleSetMode[512];
    i32  nPlayCalls;
    void *pLastPlayedSample;
    i32  nSetSfxVolume;
    i32  nSetSfxPaused;
    i32  nSetSfxPausedChannel;
    i32  nMusicLoads;
    u32  nMusicMode;
    i32  nMusicLoopSet;
    i32  nMusicOptimizeMax;
    i32  nMusicOptimizeMin;
    i32  nMusicPlayCalls;
    i32  nMusicVolume;
    i32  nMusicPaused;
    i32  nMusicFreeCalls;
    i32  nCloseCalls;
    i32  nSampleFreeCalls;
    bool bMusicFinished;      /* the port-side pfnMusicFinished answer */
    i32  nMusicLoopCalls;     /* how many times SetLooping ran, and in what order */
    i32  aMusicLoopSeq[8];
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
    /* Record wrapper arguments without decoding PCM. One fixture injects a
     * null handle at the shipped empty slot; the input buffer is not retained. */
    const i32 nIndex = g_fake.nSampleLoads;
    if (g_fake.nSampleLoads < 512) {
        g_fake.aSampleModes[g_fake.nSampleLoads] = nMode;
    }
    g_fake.nSampleLoads += 1;
    (void)pData;
    (void)cbLength;
    if (g_fake.bNullSample18 && nIndex == 18) {
        return NULL;
    }
    return (void *)(uintptr_t)(nIndex + 1); /* distinct opaque handle per slot */
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
    g_fake.pLastPlayedSample = pSample;
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
    if (g_fake.nMusicLoopCalls < 8) {
        g_fake.aMusicLoopSeq[g_fake.nMusicLoopCalls] = bLoop;
    }
    g_fake.nMusicLoopCalls += 1;
    (void)pMod;
}

static bool Fake_MusicFinished(const void *pMod)
{
    (void)pMod;
    return g_fake.bMusicFinished;
}

static void Fake_MusicOptimize(void *pMod, i32 nMaxChannels, i32 nMinVolume)
{
    /* FMUSIC_OptimizeChannels(mod, 16, 10) at 0x9E208 -- both constants are asserted. */
    g_fake.nMusicOptimizeMax = nMaxChannels;
    g_fake.nMusicOptimizeMin = nMinVolume;
    (void)pMod;
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
    Fake_MusicPaused, Fake_MusicFree, Fake_Close, Fake_MusicFinished
};

static void Fake_Reset(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.bInitSucceeds = true;
}

/* --- tests --- */

/* Sound_InitAndLoadSamples @ 0x9e268: FSOUND_Init retried at i = 0..10 (11 attempts, 0x9E27C..0x9E2B4),
 * then the prefix scan -- "<prefix>0", "<prefix>1", ... -- that stops at the first index the asset reader
 * does not have, each hit going through FSOUND_Sample_Load with mode 0xA000 and then FSOUND_Sample_SetMode(1).
 * g_bSoundInitReady is set after the retry loop on both paths -- the original's quirk, kept. */
static void TestLoadSamples(void)
{
    printf("Sound_InitAndLoadSamples(\"tact\")\n");
    Fake_Reset();
    Sound_SetBackend(&g_fakeBackend);
    Check(Sound_InitAndLoadSamples("tact"), "returns true");
    CheckEqI32(g_fake.nInitCalls, 1, "  FSOUND_Init called once on success");
    /* main.dat holds TACT0.WAV .. TACT34.WAV with no gaps, so the scan must
     * stop at the first missing index: 35 samples. */
    CheckEqI32(Sound_GetSampleCount(), 35, "  indexed 35 sample slots (tact0..tact34)");
    CheckEqI32(g_fake.nSampleLoads, 35, "  backend sample load attempts");
    Check(Sound_IsInitialised(), "  audio ready flag set");
    /* Mode 0xA000 was passed to Sample_Load, then SetMode(1). */
    Check(g_fake.aSampleModes[0] == 0xA000u, "  sample load mode 0xA000");
    Check(g_fake.aSampleSetMode[0] == 1u, "  sample set mode 1");

    /* Sound_Shutdown @ 0x9e3d4: FSOUND_Sample_Free per item while the index runs to
     * g_nSoundSampleCount (0x9E3F4, loop test at 0x9E408), FMUSIC_FreeSong only when g_bMusicLoaded
     * stands (0x9E418 / 0x9E420), FSOUND_Close at 0x9E424, then g_bSoundInitReady = 0 at 0x9E430 and
     * g_bMusicLoaded = 0 at 0x9E438. */
    printf("Sound_Shutdown\n");
    Sound_Shutdown();
    CheckEqI32(g_fake.nSampleFreeCalls, 35, "  freed every sample");
    CheckEqI32(g_fake.nMusicFreeCalls, 0, "  no song free here: no music was loaded, so 0x9E418 fails");
    CheckEqI32(g_fake.nCloseCalls, 1, "  backend closed");
    Check(!Sound_IsInitialised(), "  ready flag cleared");
}

static void TestNullSampleSlot(void)
{
    printf("sample bank: a null handle at slot 18 must not truncate later slots\n");
    Fake_Reset();
    g_fake.bNullSample18 = true;
    Sound_SetBackend(&g_fakeBackend);
    Sound_SetMuteBytes(NULL, NULL);
    Sound_SetSfxMuted(false);
    Check(Sound_InitAndLoadSamples("tact"), "the wrapper initializes with an empty slot");
    CheckEqI32(g_fake.nSampleLoads, 35, "all 35 resource slots reach the backend");
    CheckEqI32(Sound_GetSampleCount(), 35, "the indexed span includes slots after the null handle");
    Check(g_fake.aSampleSetMode[18] == 1u && g_fake.aSampleSetMode[34] == 1u,
          "mode setup continues at the empty and final slots");

    Sound_Play(34);
    Check(g_fake.nPlayCalls == 1 && g_fake.pLastPlayedSample == (void *)(uintptr_t)35,
          "the final slot reaches its own backend handle");
    Sound_Play(18);
    Check(g_fake.nPlayCalls == 2 && g_fake.pLastPlayedSample == NULL,
          "the empty slot keeps its index and forwards its null handle");
    Sound_Shutdown();
    CheckEqI32(g_fake.nSampleFreeCalls, 34, "shutdown frees the 34 non-null handles");
    CheckEqI32(g_fake.nCloseCalls, 1, "the backend closes once");
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

/* Sound_LoadAndPlayMusic @ 0x9e130: build "back<N>.it" (aBack at 0xAC670, aIt at 0xAC66C, the
 * strcat pair at 0x9E17C/0x9E188), read it through the asset reader, allocate the buffer, and hand
 * it to FMUSIC_LoadSongEx with mode 0x8000 (LOADMEMORY) at 0x9E1F4 -- then SetLooping(1) at 0x9E1F8,
 * OptimizeChannels(16, 10) at 0x9E208, PlaySong at 0x9E210, SetPaused(1) only when Map_bMusicOff
 * stands (0x9E228/0x9E230), free the buffer, and set g_bMusicLoaded at 0x9E244 whether the load
 * worked or not. */
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
    CheckEqI32(g_fake.nMusicOptimizeMin, 10, "  with min volume 10 (0x9E208 passes 16 and 10)");
    CheckEqI32(g_fake.nMusicPlayCalls, 1, "  song played");
    Check(Sound_HasMusic(), "  music flag set");

    /* Recorded deviation, not a claim that the port is right: the original has no "is the asset
     * there" test. It reads whatever AssetReader_Size gives (0 for a missing name), allocates 0 bytes,
     * calls FMUSIC_LoadSongEx with length 0 anyway, and sets g_bMusicLoaded at 0x9E244 regardless of
     * what the load returned. The port returns early. The difference is inert in this build -- the
     * default backend is the Stub_ set, which no-ops every FMUSIC_* call -- so it is recorded rather
     * than reproduced, and the loaded flag the original would have left set is what would make a
     * later FMUSIC_FreeSong(NULL) reachable at 0x9E420. */
    printf("Sound_LoadAndPlayMusic for a missing track\n");
    g_fake.nMusicLoads = 0;
    Sound_LoadAndPlayMusic(99);
    CheckEqI32(g_fake.nMusicLoads, 0, "  no load attempt for missing track");

    /* 0x9E418 gates FMUSIC_FreeSong on g_bMusicLoaded: the flag stands here (track 2 loaded), so the
     * shutdown frees the song exactly once. */
    printf("Sound_Shutdown frees the loaded song\n");
    Sound_Shutdown();
    CheckEqI32(g_fake.nMusicFreeCalls, 1, "  one FMUSIC_FreeSong because the loaded flag stands (0x9E418/0x9E420)");
    Check(!Sound_HasMusic(), "  and the flag is cleared at 0x9E438");
}

/* The volume mapping is exact arithmetic in both appliers: Sound_ApplyMusicVolume @ 0x9e454 and
 * Sound_ApplySfxVolume @ 0x9e4dc compute `v = muted ? 0 : 12*level + 4` (for sfx the branch is at
 * 0x9E4F4 and the arithmetic at 0x9E50C) and hand the backend `4 * v - 1` (0x9E518). Step 0 gives 15,
 * step 5 gives 255, and a muted pair gives -1 -- the original's value, not a clamped 0.
 * Options_MusicLevel / Options_SfxLevel are where the port keeps the arithmetic. */
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

/* Load and decode each music module from the shipped archive without opening a
 * sound device. This proves that the real backend accepts the IT data and that
 * the returned module produces PCM; it does not prove audible device output. */
static void TestShippedMusicDecode(void)
{
    AssetReader rdr;
    char szName[16];
    i16 aPcm[PUFO_AUDIO_MIXRATE];

    printf("WinMM music decode (the shipped .it modules)\n");
    for (i32 track = 0; track < 5; ++track) {
        void *pData;
        void *pModule;
        u32 cb;
        size_t nTotalFrames = 0u;
        bool bNonzero = false;

        snprintf(szName, sizeof(szName), "back%d.it", (int)track);
        AssetReader_Clear(&rdr);
        if (!AssetReader_Find(&rdr, szName)) {
            Check(false, szName);
            continue;
        }
        cb = AssetReader_Size(&rdr);
        pData = malloc(cb);
        if (pData == NULL || !AssetReader_Read(&rdr, pData, cb)) {
            free(pData);
            Check(false, "archive module data can be read");
            continue;
        }
        pModule = g_pufoAudioWinmm.pfnMusicLoad(pData, cb, 0x8000u);
        free(pData);
        if (pModule == NULL) {
            Check(false, szName);
            continue;
        }

        /* This handle is the OpenMPT module owned by the WinMM backend. */
        for (u32 second = 0; second < 10u && !bNonzero; ++second) {
            size_t nFrames = openmpt_module_read_mono(
                (openmpt_module *)pModule, PUFO_AUDIO_MIXRATE,
                sizeof(aPcm) / sizeof(aPcm[0]), aPcm);
            nTotalFrames += nFrames;
            for (size_t i = 0; i < nFrames; ++i) {
                if (aPcm[i] != 0) {
                    bNonzero = true;
                    break;
                }
            }
            if (nFrames == 0u) {
                break;
            }
        }
        Check(nTotalFrames > 0u && bNonzero, szName);
        g_pufoAudioWinmm.pfnMusicFree(pModule);
    }
}

/* A minimal mono PCM WAV image with a chosen fmt rate, for the loader's
 * native-rate gate. */
static void BuildWavImage(u8 *p, u32 nRate)
{
    memset(p, 0, 46);
    memcpy(p, "RIFF", 4);
    p[4] = 38;
    memcpy(p + 8, "WAVE", 4);
    memcpy(p + 12, "fmt ", 4);
    p[16] = 16;
    p[20] = 1; /* wFormatTag = PCM */
    p[22] = 1; /* nChannels = 1 */
    p[24] = (u8)nRate;
    p[25] = (u8)(nRate >> 8);
    p[26] = (u8)(nRate >> 16);
    p[27] = (u8)(nRate >> 24);
    p[28] = (u8)nRate;  /* nAvgBytesPerSec = rate (8-bit mono) */
    p[29] = (u8)(nRate >> 8);
    p[30] = (u8)(nRate >> 16);
    p[31] = (u8)(nRate >> 24);
    p[32] = 1;  /* nBlockAlign */
    p[34] = 8;  /* wBitsPerSample */
    memcpy(p + 36, "data", 4);
    p[40] = 2;
    p[44] = 128; /* one silent 8-bit pair */
    p[45] = 128;
}

static void TestNativePlayback(void)
{
    static const i16 aSource[4] = { 0, 1000, 2000, 3000 };
    i16 aOut[16] = { 0 };
    u8 aWav[46];
    void *pHandle;

    const u32 nCopied = PufoAudioWinmm_TestCopyNativeFrames(aSource, 4, aOut, 16);

    printf("WinMM native-rate frame path (the resampler is gone)\n");
    CheckEqI32((i32)nCopied, 4, "  four source frames produce four output frames at the native rate");
    Check(aOut[0] == 0 && aOut[1] == 1000 && aOut[2] == 2000 && aOut[3] == 3000,
          "  frames pass through the voice path unmodified");

    BuildWavImage(aWav, 11025u);
    pHandle = g_pufoAudioWinmm.pfnSampleLoad(aWav, 46, 0xA000u);
    Check(pHandle != NULL, "  a 11025-Hz mono WAV image loads");
    g_pufoAudioWinmm.pfnSampleFree(pHandle);
    BuildWavImage(aWav, 22050u);
    Check(g_pufoAudioWinmm.pfnSampleLoad(aWav, 46, 0xA000u) == NULL,
          "  a WAV at a non-native rate is refused, not resampled");
}

/* The port-side music loop toggle, recorded by the fake backend. */
static void TestMusicLoopToggle(void)
{
    printf("Sound_SetMusicLooping (the port-side loop toggle)\n");
    Fake_Reset();
    Sound_SetBackend(&g_fakeBackend);
    Sound_InitAndLoadSamples("tact");
    Sound_LoadAndPlayMusic(0);
    CheckEqI32(g_fake.nMusicLoopSet, 1, "  the load arms looping as 0x9E1F8 does");
    Check(Sound_IsMusicLooping(), "  the mirror reports looping");
    Sound_SetMusicLooping(false);
    CheckEqI32(g_fake.nMusicLoopSet, 0, "  toggle off reaches the backend");
    Check(!Sound_IsMusicLooping(), "  and the mirror follows");
    Sound_SetMusicLooping(true);
    CheckEqI32(g_fake.nMusicLoopSet, 1, "  toggle on reaches the backend");

    /* Port-side loop-off scenario (2026-11-04, user request): the options byte
     * loads a track unlooped, and the finished signal hands over to the next. */
    {
        u8 bLoop = 0;

        g_fake.nMusicLoopCalls = 0;
        Sound_SetLoopByte(&bLoop);
        Sound_LoadAndPlayMusic(0);
        Check(g_fake.nMusicLoopCalls >= 2
                  && g_fake.aMusicLoopSeq[0] == 1 && g_fake.aMusicLoopSeq[1] == 0,
              "  the load sets 1 (0x9E1F8) then 0 (the loop-off byte)");
        Check(!Sound_IsMusicLooping(), "  the live byte reports not looping");
        g_fake.nMusicLoads = 0;
        g_fake.nMusicLoopCalls = 0;
        g_fake.bMusicFinished = true;
        Sound_MusicProgress();
        CheckEqI32(g_fake.nMusicLoads, 1, "  the finished track hands over to the next");
        Check(g_fake.nMusicLoopCalls >= 2
                  && g_fake.aMusicLoopSeq[0] == 1 && g_fake.aMusicLoopSeq[1] == 0,
              "  the hand-over load runs the same set-1-then-0 sequence");
        g_fake.nMusicLoads = 0;
        g_fake.bMusicFinished = false;
        Sound_MusicProgress();
        CheckEqI32(g_fake.nMusicLoads, 0, "  no hand-over while the module is still playing");
        g_fake.bMusicFinished = true;
        Sound_SetLoopByte(NULL); /* back to the default: loop on */
        Sound_MusicProgress();
        CheckEqI32(g_fake.nMusicLoads, 0, "  no hand-over while the setting is ON");
    }
    Sound_Shutdown();
    Sound_SetBackend(NULL);
}

/* Duration in ms of a WAV image straight out of main.dat, from its fmt/data
 * chunks -- the same fields the backend's sample loader reads. */
static u32 WavImageDurationMs(const u8 *p, u32 cbLength)
{
    u32 pos = 12u;
    u32 nRate = 0u;
    u32 nBits = 0u;
    u32 cbData = 0u;

    while (pos + 8u <= cbLength) {
        const u32 cb = (u32)p[pos + 4] | ((u32)p[pos + 5] << 8)
                     | ((u32)p[pos + 6] << 16) | ((u32)p[pos + 7] << 24);
        if (memcmp(p + pos, "fmt ", 4) == 0 && cb >= 16u) {
            nRate = (u32)p[pos + 12] | ((u32)p[pos + 13] << 8)
                  | ((u32)p[pos + 14] << 16) | ((u32)p[pos + 15] << 24);
            nBits = (u32)p[pos + 22] | ((u32)p[pos + 23] << 8);
        } else if (memcmp(p + pos, "data", 4) == 0) {
            cbData = cb;
        }
        pos += 8u + cb + (cb & 1u);
    }
    if (nRate == 0u || nBits == 0u || cbData == 0u) {
        return 0u;
    }
    return (u32)(((unsigned long long)cbData / (nBits / 8u)) * 1000u / nRate);
}

static u32 TactSampleDurationMs(i32 nIndex)
{
    AssetReader rdr;
    char szName[16];
    void *pData;
    u32 cb;
    u32 nMs = 0u;

    snprintf(szName, sizeof(szName), "TACT%d.WAV", (int)nIndex);
    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, szName)) {
        return 0u;
    }
    cb = AssetReader_Size(&rdr);
    pData = malloc(cb);
    if (pData != NULL && AssetReader_Read(&rdr, pData, cb)) {
        nMs = WavImageDurationMs((const u8 *)pData, cb);
    }
    free(pData);
    return nMs;
}

/* The device gate proves that music PCM and an SFX reach completed buffers on the
 * real waveOut path. It does not prove the user can hear them. */
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
    Check(Sound_InitAndLoadSamples("tact"), "  game-side audio-ready flag is set");
    CheckEqI32(PufoAudioWinmm_GetOutputRate(), 11025,
               "  WinMM runs at the shipped WAVs' native 11025 Hz (no resampler)");
    CheckEqI32(Sound_GetSampleCount(), 35, "  the same 35 TACT*.WAV loaded through the real backend");
    Sound_LoadAndPlayMusic(0);           /* BACK0.IT through the game audio path */
    Check(PufoAudioWinmm_WaitMusicDone(8000u),
          "  BACK0.IT produced nonzero PCM in a completed waveOut buffer");
    {
        LARGE_INTEGER t0, t1, tFreq;
        i32 nCompletions = 0;
        u32 nElapsedMs;

        QueryPerformanceFrequency(&tFreq);
        QueryPerformanceCounter(&t0);
        for (;;) {
            if (PufoAudioWinmm_WaitMusicDone(1000u)) {
                ++nCompletions;
            }
            QueryPerformanceCounter(&t1);
            nElapsedMs = (u32)((t1.QuadPart - t0.QuadPart) * 1000 / tFreq.QuadPart);
            if (nElapsedMs >= 4000u) {
                break;
            }
        }
        printf("  waveOut completions: %d in %u ms (256-frame buffers at 11025 Hz -> ~43/s)\n",
               nCompletions, nElapsedMs);
        /* 11025/256 = 43 completions/s; accept 32..55/s. */
        Check((unsigned)nCompletions * 1000u >= 32u * nElapsedMs
              && (unsigned)nCompletions * 1000u <= 55u * nElapsedMs,
              "  the device consumes mix buffers at its nominal rate");
    }
    PufoAudioWinmm_DrainDone(8);        /* discard any earlier SFX completion */
    {
        const u32 nExpectedMs = TactSampleDurationMs(2);
        LARGE_INTEGER t0, t1, tFreq;
        u32 nMeasuredMs;

        Check(nExpectedMs > 0u, "  TACT2.WAV header gives a playable duration");
        QueryPerformanceFrequency(&tFreq);
        QueryPerformanceCounter(&t0);
        Sound_Play(2);                      /* TACT2 */
        Check(PufoAudioWinmm_WaitDone(8000u), "  the mixed sample reached its final waveOut buffer");
        QueryPerformanceCounter(&t1);
        nMeasuredMs = (u32)((t1.QuadPart - t0.QuadPart) * 1000 / tFreq.QuadPart);
        printf("  TACT2 wall time: source %u ms, measured %u ms\n", nExpectedMs, nMeasuredMs);
        Check(nMeasuredMs * 3u >= nExpectedMs * 2u && nMeasuredMs * 2u <= nExpectedMs * 3u,
              "  the SFX reached the device in real time, neither stretched nor cut short");
    }
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
    TestNullSampleSlot();
    TestInitRetry();
    TestMusic();
    TestVolumeMapping();
    TestOptionsDefaults();
    TestVolumeApply();
    TestFocusPause();
    TestMuteBytes();
    TestMusicLoopToggle();
    TestShippedMusicDecode();
    TestNativePlayback();
    TestDevice();

    Asset_CloseArchives();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
