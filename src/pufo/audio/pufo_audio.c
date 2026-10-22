/* pufo_audio.c - sound and music, behind a backend seam.
 *
 * Evidence: see pufo_audio.h for the full address list.
 */
#include "audio/pufo_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "asset/pufo_asset.h"
#include "util/pufo_rand.h"
#include "util/pufo_str.h"

/* paSoundSamples @ 0x187428, byte_187F24 @ 0x187f24, byte_187F28 @ 0x187f28,
 * dword_187324 @ 0x187324 (loaded sample count), pMusicMod @ 0x187320. */
static void            *s_apSamples[PUFO_MAX_SAMPLES];
static i32              s_nSampleCount;
static bool             s_bAudioReady;   /* byte_187F24 */
static bool             s_bMusicLoaded;  /* byte_187F28 */
static void            *s_pMusicMod;     /* pMusicMod */
static bool             s_bSfxToggle;    /* byte_187318 */
static bool             s_bSfxMuted;     /* fallback mirror of pCtxBase +99 (soundoff) */
static const u8        *s_pbSoundOff;    /* pCtxBase +99 when the app hands it over */
static const u8        *s_pbMusicOff;    /* Map_bMusicOff @ 0x128A92, ditto */

/* No-op backend: every call reports failure the way a missing fmodce.dll would.
 * This makes the port behave like the original does when audio is unavailable
 * (Sound_Play and the volume functions early-out on the ready flag). */
static bool Stub_Init(u32 nMixRate, i32 nMaxChannels, u32 nFlags)
{
    (void)nMixRate;
    (void)nMaxChannels;
    (void)nFlags;
    return false;
}

static void  Stub_Void(void) { }
static void  Stub_SetWindow(void *hWnd) { (void)hWnd; }
static void *Stub_SampleLoad(const void *pData, u32 cbLength, u32 nMode)
{
    (void)pData;
    (void)cbLength;
    (void)nMode;
    return NULL;
}
static void Stub_SampleSetMode(void *pSample, u32 nMode) { (void)pSample; (void)nMode; }
static void Stub_SampleFree(void *pSample) { (void)pSample; }
static i32  Stub_PlaySound(void *pSample) { (void)pSample; return -1; }
static void Stub_SetSfxVolume(i32 nVolume) { (void)nVolume; }
static void Stub_SetSfxPaused(i32 nChannel, i32 bPaused) { (void)nChannel; (void)bPaused; }
static void *Stub_MusicLoad(const void *pData, u32 cbLength, u32 nMode)
{
    (void)pData;
    (void)cbLength;
    (void)nMode;
    return NULL;
}
static void Stub_MusicMod2(void *pMod, i32 nA) { (void)pMod; (void)nA; }
static void Stub_MusicOptimize(void *pMod, i32 nMaxChannels, i32 nMinVolume)
{
    (void)pMod;
    (void)nMaxChannels;
    (void)nMinVolume;
}
static void Stub_MusicPlay(void *pMod) { (void)pMod; }
static void Stub_MusicVolume(void *pMod, i32 nVolume) { (void)pMod; (void)nVolume; }
static void Stub_MusicFree(void *pMod) { (void)pMod; }

static const PufoAudioBackend s_stubBackend = {
    Stub_Init,
    Stub_SetWindow,
    Stub_SampleLoad,
    Stub_SampleSetMode,
    Stub_SampleFree,
    Stub_PlaySound,
    Stub_SetSfxVolume,
    Stub_SetSfxPaused,
    Stub_MusicLoad,
    Stub_MusicMod2,   /* set looping */
    Stub_MusicOptimize,
    Stub_MusicPlay,
    Stub_MusicVolume,
    Stub_MusicMod2,   /* set paused */
    Stub_MusicFree,
    Stub_Void         /* close */
};

static const PufoAudioBackend *s_pBackend = &s_stubBackend;

void Sound_SetBackend(const PufoAudioBackend *pBackend)
{
    s_pBackend = (pBackend != NULL) ? pBackend : &s_stubBackend;
}

void Sound_SetSfxMuted(bool bMuted)
{
    s_bSfxMuted = bMuted;
}

void Sound_SetMuteBytes(const u8 *pbSoundOff, const u8 *pbMusicOff)
{
    s_pbSoundOff = pbSoundOff;
    s_pbMusicOff = pbMusicOff;
}

/* The soundoff read Sound_Play makes before it plays: the live options cell when
 * one is installed, the mirror otherwise. */
static bool Sound_SfxMuted(void)
{
    return (s_pbSoundOff != NULL) ? (*s_pbSoundOff != 0u) : s_bSfxMuted;
}

bool Sound_IsInitialised(void)
{
    return s_bAudioReady;
}

bool Sound_HasMusic(void)
{
    return s_bMusicLoaded;
}

i32 Sound_GetSampleCount(void)
{
    return s_nSampleCount;
}

/* Sound_InitAndLoadSamples @ 0x9e268.
 *
 * Order matters and is reproduced: retry FSOUND_Init, set the window, set the
 * ready flag, then load "<prefix><n>.wav" from n = 0 until one is missing. */
bool Sound_InitAndLoadSamples(const char *pszPrefix)
{
    AssetReader rdr;
    i32         nTry;
    i32         nIndex;

    AssetReader_Clear(&rdr);

    /* Structure copied from the disassembly @ 0x9e27c: attempt first, then test
     * the guard, then sleep. This yields 11 attempts / 10 sleeps, which is what
     * the original does. A plain `for` with the guard in the condition would
     * make 12 attempts. */
    nTry = 0;
    for (;;) {
        if (s_pBackend->pfnInit(PUFO_AUDIO_MIXRATE, PUFO_AUDIO_CHANNELS, 0)) {
            break;
        }
        if (nTry >= PUFO_AUDIO_INIT_GUARD) {
            break;
        }
        Sleep(PUFO_AUDIO_INIT_SLEEP_MS);
        ++nTry;
    }
    /* The original calls FSOUND_SetHWND unconditionally, even after a failed
     * init. The stub tolerates that. */
    s_pBackend->pfnSetWindow(NULL);

    s_nSampleCount = 0;
    s_bAudioReady  = true;

    for (nIndex = 0; nIndex < PUFO_MAX_SAMPLES; ++nIndex) {
        char  szName[56];
        char  szNum[16];
        void *pData;
        u32   cb;

        pufo_strncpy(szName, pszPrefix, sizeof(szName));
        snprintf(szNum, sizeof(szNum), "%d", (int)nIndex);
        if (!pufo_strcat(szName, szNum, sizeof(szName))
            || !pufo_strcat(szName, ".wav", sizeof(szName))) {
            break;
        }

        AssetReader_Clear(&rdr);
        if (!AssetReader_Find(&rdr, szName)) {
            break; /* first missing index ends the scan */
        }
        cb = AssetReader_Size(&rdr);
        pData = malloc(cb);
        if (pData == NULL) {
            break;
        }
        if (!AssetReader_Read(&rdr, pData, cb)) {
            free(pData);
            break;
        }

        /* FSOUND_Sample_Load(index -2, data, mode 0xA000, offset 0, length).
         * Mode 0xA000 is FSOUND_LOADMEMORY | FSOUND_16BITS in FMOD 3. */
        s_apSamples[nIndex] = s_pBackend->pfnSampleLoad(pData, cb, 0xA000u);
        s_pBackend->pfnSampleSetMode(s_apSamples[nIndex], 1u);
        free(pData);

        s_nSampleCount = nIndex + 1;
    }

    return s_bAudioReady;
}

void Sound_Shutdown(void)
{
    i32 i;

    for (i = 0; i < s_nSampleCount; ++i) {
        if (s_apSamples[i] != NULL) {
            s_pBackend->pfnSampleFree(s_apSamples[i]);
            s_apSamples[i] = NULL;
        }
    }
    if (s_bMusicLoaded && s_pMusicMod != NULL) {
        s_pBackend->pfnMusicFree(s_pMusicMod);
    }
    s_pMusicMod    = NULL;
    s_nSampleCount = 0;
    s_pBackend->pfnClose();
    s_bAudioReady  = false;
    s_bMusicLoaded = false;
}

/* Sound_Play @ 0x9e0c0. Order is part of the behaviour:
 *   1. early-out unless the ready flag is set
 *   2. toggle byte_187318
 *   3. draw one 0xC8 random number  <- advances the global RNG stream
 *   4. play only if the soundoff byte is clear
 * Step 3 happening before the mute test is why muting does not freeze the RNG.
 */
void Sound_Play(i32 nSample)
{
    if (!s_bAudioReady) {
        return;
    }
    s_bSfxToggle = !s_bSfxToggle;
    (void)Game_RandU16(0xC8);

    if (Sound_SfxMuted()) {
        return;
    }
    /* Deliberate deviation: the original indexes paSoundSamples with no bound
     * check, which is a buffer overrun if a caller passes a bad index. The
     * guard below is the only place the port is stricter than the original,
     * because the original's behaviour there is undefined rather than defined.
     * The NULL check the original also lacks is NOT reproduced: the backend
     * receives whatever handle is stored, exactly as FSOUND_PlaySound did. */
    if (nSample < 0 || nSample >= s_nSampleCount) {
        return;
    }
    (void)s_pBackend->pfnPlaySound(s_apSamples[nSample]);
}

/* Sound_LoadAndPlayMusic @ 0x9e130. */
void Sound_LoadAndPlayMusic(i32 nTrack)
{
    AssetReader rdr;
    char        szName[56];
    char        szNum[16];
    void       *pData;
    u32         cb;

    AssetReader_Clear(&rdr);

    pufo_strncpy(szName, "back", sizeof(szName));
    snprintf(szNum, sizeof(szNum), "%d", (int)nTrack);
    if (!pufo_strcat(szName, szNum, sizeof(szName))
        || !pufo_strcat(szName, ".it", sizeof(szName))) {
        return;
    }
    if (!AssetReader_Find(&rdr, szName)) {
        return;
    }
    cb = AssetReader_Size(&rdr);
    pData = malloc(cb);
    if (pData == NULL) {
        return;
    }
    if (!AssetReader_Read(&rdr, pData, cb)) {
        free(pData);
        return;
    }

    /* FMUSIC_LoadSongEx(data, 0, length, mode 0x8000 (=FSOUND_LOADMEMORY)). */
    s_pMusicMod = s_pBackend->pfnMusicLoad(pData, cb, 0x8000u);
    if (s_pMusicMod != NULL) {
        s_pBackend->pfnMusicSetLooping(s_pMusicMod, 1);
        s_pBackend->pfnMusicOptimize(s_pMusicMod, 16, 10);
        s_pBackend->pfnMusicPlay(s_pMusicMod);
        /* 0x9E228/0x9E230: the load pauses the module straight away when the
         * musicoff byte stands. The original reaches FMUSIC_SetPaused with no
         * null test on the module handle; the port tests the handle, which is the
         * recorded stricter guard. */
        if (s_pbMusicOff != NULL && *s_pbMusicOff != 0u) {
            s_pBackend->pfnMusicSetPaused(s_pMusicMod, 1);
        }
    }
    free(pData); /* the original frees the buffer right after LoadSongEx */

    s_bMusicLoaded = true;
}

/* dword_A3040-style volume application, from Sound_ApplyMusicVolume @ 0x9e454.
 * The original also sets the paused flag here; that is included. */
void Sound_ApplyMusicVolume(i32 nLevel, u8 bMuted)
{
    if (s_pMusicMod != NULL) {
        s_pBackend->pfnMusicSetVolume(s_pMusicMod, nLevel);
    }
    if (s_bMusicLoaded && s_pMusicMod != NULL) {
        s_pBackend->pfnMusicSetPaused(s_pMusicMod, bMuted != 0 ? 1 : 0);
    }
}

/* Sound_ApplySfxVolume @ 0x9e4dc. FSOUND_SetSFXMasterVolume first (0x9E518), then
 * `FSOUND_SetPaused(FSOUND_ALL, Map_bSoundOff != 0)` at 0x9E548 -- the mute flag is the pause argument, so muting the
 * effects pauses them rather than only lowering them -- and that second call only while byte_187F24 says the audio is
 * ready (0x9E528). */
void Sound_ApplySfxVolume(i32 nLevel, u8 bMuted)
{
    s_pBackend->pfnSetSfxVolume(nLevel);
    if (s_bAudioReady) {
        s_pBackend->pfnSetSfxPaused(PUFO_FSOUND_ALL, bMuted != 0 ? 1 : 0);
    }
}

/* The audio half of PocketUFO_WndProc's focus arms. Both arms call FSOUND_SetPaused(FSOUND_ALL, flag) with no
 * byte_187F24 test -- 0xA052C..0xA0534 pauses on WM_KILLFOCUS, 0xA0618..0xA0620 unpauses on WM_SETFOCUS -- and then
 * pause or resume the music module only when byte_187F28 is set (0xA0538, 0xA0624). The original reads the module out
 * of MEMORY[0x187320] with no null test; this module owns the pointer, so it tests it. */
void Sound_SetPaused(bool bPaused)
{
    s_pBackend->pfnSetSfxPaused(PUFO_FSOUND_ALL, bPaused ? 1 : 0);
    if (s_bMusicLoaded && s_pMusicMod != NULL) {
        s_pBackend->pfnMusicSetPaused(s_pMusicMod, bPaused ? 1 : 0);
    }
}
