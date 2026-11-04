/* pufo_audio_winmm.c - the Win64 sound output, at the PufoAudioBackend seam.
 *
 * The original links fmodce.dll (FMOD 3 for WinCE, ARM); that DLL is not re-linked
 * here (CLAUDE.md constraint), so the game-side logic stays in pufo_audio.c and this
 * file answers the backend table with winmm. The addresses behind each slot are in
 * pufo_audio_winmm.h.
 *
 * The device runs at the shipped WAVs' native 11025 Hz. The game still passes
 * FSOUND_Init's original 22000 mixrate to this backend, but the Win64 device
 * does not use it: playing the 11025 Hz data natively removes the resampler
 * (user-directed, 2026-11-04). 8-bit mono PCM is widened to signed 16-bit at
 * load; voices read source frames 1:1. OpenMPT renders the music at the same
 * 11025 Hz output rate.
 */
#include "audio/pufo_audio_winmm.h"

#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <mmsystem.h>
#include <libopenmpt/libopenmpt.h>

/* Output format and SFX voice count. All 35 shipped TACT*.WAV are
 * 11025 Hz / 8-bit / mono, so 11025 is the native rate of the whole SFX set.
 * The queue is 8 x 256 frames (~186 ms): two 11.6 ms buffers starved the
 * Windows audio engine, the mixer thread woke later than a buffer drained, and
 * the user heard underrun crackle with the whole stream stretched (measured
 * 2026-11-04: 50 completions/s against a nominal 86, a 1190 ms sample finishing
 * in 2080 ms). 23 ms per buffer leaves the refill thread a real wake budget. */
#define WINMM_BITS   16u
#define WINMM_OUTPUT_RATE 11025u
#define WINMM_SFX_CHANNELS 32u
#define WINMM_MIX_FRAMES 256u
#define WINMM_MIX_BUFFERS 8u

typedef struct WinmmSample {
    i16 *pData;       /* 16-bit mono samples at WINMM_OUTPUT_RATE, owned by this record */
    u32  nFrameCount;
} WinmmSample;

typedef struct WinmmVoice {
    WinmmSample *pSample;
    u32          nSourceFrame;
} WinmmVoice;

typedef struct WinmmMixBuffer {
    WAVEHDR hdr;
    i16 aPcm[WINMM_MIX_FRAMES];
    bool bPrepared;
    bool bSampleDone;
    bool bMusicNonzero;
    bool bOutputNonzero;
    bool bQueued;
} WinmmMixBuffer;

static HWAVEOUT g_hOut;
static HANDLE   g_hWaveDone;  /* waveOut completion event (CALLBACK_EVENT) */
static HANDLE   g_hSampleDone;/* a completed sample's final mixed buffer */
static HANDLE   g_hMusicDone; /* a nonzero music buffer completed by waveOut */
static HANDLE   g_hStop;
static HANDLE   g_hMixerThread;
static bool     g_bOpen;
static i32      g_nSfxVolume = 255;
static WinmmVoice g_aVoices[WINMM_SFX_CHANNELS];
static WinmmMixBuffer g_aMixBuffers[WINMM_MIX_BUFFERS];
static SRWLOCK  g_MixerLock = SRWLOCK_INIT;
static openmpt_module *g_pMusicModule;
static i32      g_nMusicVolume = 255;
static bool     g_bMusicPlaying;
static bool     g_bMusicPaused;
static bool     g_bMusicLooping;
static bool     g_bMusicEnded;   /* set when a played module runs out of frames */
static u32      g_nOutputRate;

static void Winmm_MixBuffer(WinmmMixBuffer *pBuffer);
static DWORD WINAPI Winmm_MixerThread(void *pUser);
static void Winmm_Close(void);

static u32 Winmm_RdU32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u16 Winmm_RdU16(const u8 *p) { return (u16)((u32)p[0] | ((u32)p[1] << 8)); }

/* One output frame at the native rate: the device runs at the WAVs' own rate,
 * so a voice just walks its source frames. */
static bool Winmm_ReadNativeFrame(WinmmVoice *pVoice, i16 *pOut)
{
    if (pVoice == NULL || pOut == NULL || pVoice->pSample == NULL
        || pVoice->nSourceFrame >= pVoice->pSample->nFrameCount) {
        return false;
    }
    *pOut = pVoice->pSample->pData[pVoice->nSourceFrame];
    ++pVoice->nSourceFrame;
    return true;
}

bool PufoAudioWinmm_HasDevice(void)
{
    return waveOutGetNumDevs() > 0u;
}

u32 PufoAudioWinmm_GetOutputRate(void)
{
    return g_bOpen ? g_nOutputRate : 0u;
}

u32 PufoAudioWinmm_TestCopyNativeFrames(const i16 *pSource, u32 nSourceFrames,
                                        i16 *pOutput, u32 nOutputCapacity)
{
    WinmmSample sample = { (i16 *)pSource, nSourceFrames };
    WinmmVoice voice = { &sample, 0u };
    u32 nOutputFrames = 0;

    if (pSource == NULL || pOutput == NULL) {
        return 0u;
    }
    while (nOutputFrames < nOutputCapacity
           && Winmm_ReadNativeFrame(&voice, &pOutput[nOutputFrames])) {
        ++nOutputFrames;
    }
    return nOutputFrames;
}

bool PufoAudioWinmm_WaitDone(u32 nTimeoutMs)
{
    if (g_hSampleDone == NULL) {
        return false;
    }
    return WaitForSingleObject(g_hSampleDone, nTimeoutMs) == WAIT_OBJECT_0;
}

void PufoAudioWinmm_DrainDone(i32 nMaxSignals)
{
    if (g_hSampleDone == NULL) {
        return;
    }
    while (nMaxSignals > 0
           && WaitForSingleObject(g_hSampleDone, 0) == WAIT_OBJECT_0) {
        --nMaxSignals;
    }
}

bool PufoAudioWinmm_WaitMusicDone(u32 nTimeoutMs)
{
    if (g_hMusicDone == NULL) {
        return false;
    }
    return WaitForSingleObject(g_hMusicDone, nTimeoutMs) == WAIT_OBJECT_0;
}

/* FSOUND_Init(mixrate, maxchannels, flags). The original opened FMOD at 22000
 * and resampled; this Win64 device runs at the shipped WAVs' native 11025 Hz,
 * so the SFX need no resampling (user-directed deviation, 2026-11-04). The
 * requested mixrate is kept in the seam signature; no fallback hides an open
 * failure. */
static bool Winmm_Init(u32 nMixRate, i32 nMaxChannels, u32 nFlags)
{
    WAVEFORMATEX fmt;

    (void)nMixRate; /* the device runs at WINMM_OUTPUT_RATE, not FMOD's mixrate */
    (void)nMaxChannels;
    (void)nFlags;

    if (g_bOpen || waveOutGetNumDevs() == 0u) {
        return false;
    }
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = 1;
    fmt.nSamplesPerSec  = WINMM_OUTPUT_RATE;
    fmt.wBitsPerSample  = WINMM_BITS;
    fmt.nBlockAlign     = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8u);
    fmt.nAvgBytesPerSec = (DWORD)(fmt.nSamplesPerSec * fmt.nBlockAlign);

    g_hWaveDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_hSampleDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_hMusicDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_hStop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_hWaveDone == NULL || g_hSampleDone == NULL || g_hMusicDone == NULL
        || g_hStop == NULL) {
        Winmm_Close();
        return false;
    }
    if (waveOutOpen(&g_hOut, WAVE_MAPPER, &fmt, (DWORD_PTR)g_hWaveDone, 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        Winmm_Close();
        return false;
    }
    g_bOpen = true;
    g_nOutputRate = WINMM_OUTPUT_RATE;

    for (u32 i = 0; i < WINMM_MIX_BUFFERS; ++i) {
        WinmmMixBuffer *pBuffer = &g_aMixBuffers[i];
        memset(pBuffer, 0, sizeof(*pBuffer));
        pBuffer->hdr.lpData = (LPSTR)pBuffer->aPcm;
        pBuffer->hdr.dwBufferLength = sizeof(pBuffer->aPcm);
        if (waveOutPrepareHeader(g_hOut, &pBuffer->hdr, sizeof(pBuffer->hdr))
                != MMSYSERR_NOERROR) {
            Winmm_Close();
            return false;
        }
        pBuffer->bPrepared = true;
        Winmm_MixBuffer(pBuffer);
        if (waveOutWrite(g_hOut, &pBuffer->hdr, sizeof(pBuffer->hdr))
                != MMSYSERR_NOERROR) {
            Winmm_Close();
            return false;
        }
        pBuffer->bQueued = true;
    }

    g_hMixerThread = CreateThread(NULL, 0, Winmm_MixerThread, NULL, 0, NULL);
    if (g_hMixerThread == NULL) {
        Winmm_Close();
        return false;
    }
    return true;
}

/* FSOUND_SetHWND(hWndMain): waveOut has no window argument. */
static void Winmm_SetWindow(void *hWnd)
{
    (void)hWnd;
}

/* FSOUND_Sample_Load(-2, data, mode 0xA000, 0, length). The data is the whole
 * .wav image straight out of main.dat (FMOD got the same image: LOADMEMORY), so
 * the RIFF chunks are walked here and only the fmt/data pair is read. */
static void *Winmm_SampleLoad(const void *pData, u32 cbLength, u32 nMode)
{
    const u8 *p   = (const u8 *)pData;
    u32       pos;
    u32       nBits = 0;
    u32       nRate = 0;
    const u8 *pSrc  = NULL;
    u32       cbSrc = 0;
    WinmmSample *s;
    i16      *p16;

    (void)nMode; /* LOADMEMORY: the image is already in memory, as FMOD got it */

    if (p == NULL || cbLength < 12u
        || memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) {
        return NULL;
    }
    pos = 12u;
    while (pos + 8u <= cbLength) {
        u32 cb = Winmm_RdU32(p + pos + 4);
        if (cb > cbLength - pos - 8u) {
            return NULL; /* a chunk that runs past the image is a broken load */
        }
        if (memcmp(p + pos, "fmt ", 4) == 0) {
            u16 nTag, nChan, nBlock;
            if (cb < 16u) {
                return NULL;
            }
            /* fmt fields start at pos+8: tag, channels, rate(4), avgBytes(4),
             * blockAlign, bitsPerSample -- the last two are at +20 and +22. */
            nTag   = Winmm_RdU16(p + pos + 8);
            nChan  = Winmm_RdU16(p + pos + 10);
            nRate  = Winmm_RdU32(p + pos + 12);
            nBlock = Winmm_RdU16(p + pos + 20);
            nBits  = Winmm_RdU16(p + pos + 22);
            if (nTag != WAVE_FORMAT_PCM || nChan != 1u
                || nRate != WINMM_OUTPUT_RATE
                || (nBits != 8u && nBits != 16u)
                || nBlock != nBits / 8u) {
                return NULL; /* unsupported or invalid source format */
            }
        } else if (memcmp(p + pos, "data", 4) == 0) {
            pSrc  = p + pos + 8u;
            cbSrc = cb;
        }
        pos += 8u + cb + (cb & 1u); /* chunks are word-padded */
    }
    if (pSrc == NULL || cbSrc == 0u || (nBits != 8u && nBits != 16u)
        || (nBits == 16u && (cbSrc & 1u) != 0u)) {
        return NULL;
    }

    s = (WinmmSample *)calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->nFrameCount = cbSrc / (nBits / 8u);
    s->pData = (i16 *)malloc((size_t)s->nFrameCount * sizeof(*s->pData));
    if (s->pData == NULL) {
        free(s);
        return NULL;
    }
    if (nBits == 16u) {
        memcpy(s->pData, pSrc, cbSrc);
    } else {
        /* 8-bit PCM is unsigned and centred at 128; widen it into the device's
         * signed 16-bit lane without changing its level. */
        p16 = (i16 *)s->pData;
        for (u32 i = 0; i < cbSrc; ++i) {
            p16[i] = (i16)(((i32)pSrc[i] - 128) * 256);
        }
    }
    return s;
}

/* FSOUND_Sample_SetMode(sptr, 1). FMOD's mode 1 is its non-streaming playback
 * mode; winmm samples are always the whole buffer. */
static void Winmm_SampleSetMode(void *pSample, u32 nMode)
{
    (void)pSample;
    (void)nMode;
}

static void Winmm_SampleFree(void *pSample)
{
    WinmmSample *s = (WinmmSample *)pSample;

    if (s == NULL) {
        return;
    }
    AcquireSRWLockExclusive(&g_MixerLock);
    for (u32 i = 0; i < WINMM_SFX_CHANNELS; ++i) {
        if (g_aVoices[i].pSample == s) {
            g_aVoices[i].pSample = NULL;
            g_aVoices[i].nSourceFrame = 0u;
        }
    }
    ReleaseSRWLockExclusive(&g_MixerLock);
    free(s->pData);
    free(s);
}

/* FSOUND_PlaySound(-1, sample). Each play occupies one of the original 32
 * software voices and may overlap another play of the same sample. */
static i32 Winmm_PlaySound(void *pSample)
{
    WinmmSample *s = (WinmmSample *)pSample;
    i32 nVoice = -1;

    if (!g_bOpen || s == NULL) {
        return -1;
    }
    AcquireSRWLockExclusive(&g_MixerLock);
    for (u32 i = 0; i < WINMM_SFX_CHANNELS; ++i) {
        if (g_aVoices[i].pSample == NULL) {
            g_aVoices[i].pSample = s;
            g_aVoices[i].nSourceFrame = 0u;
            nVoice = (i32)i;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_MixerLock);
    return nVoice;
}

/* FSOUND_SetSFXMasterVolume(level). Options maps its 0..5 step through
 * 12*step+4 then 4*value-1; this value scales each SFX voice in the software mix. */
static void Winmm_SetSfxVolume(i32 nVolume)
{
    if (nVolume < 0) {
        nVolume = 0;
    }
    if (nVolume > 255) {
        nVolume = 255;
    }
    AcquireSRWLockExclusive(&g_MixerLock);
    g_nSfxVolume = nVolume;
    ReleaseSRWLockExclusive(&g_MixerLock);
}

/* FSOUND_SetPaused(FSOUND_ALL, flag): the channel argument is the whole device,
 * which is exactly what waveOutPause / waveOutRestart act on. */
static void Winmm_SetSfxPaused(i32 nChannel, i32 bPaused)
{
    (void)nChannel; /* FSOUND_ALL */
    if (!g_bOpen) {
        return;
    }
    if (bPaused != 0) {
        (void)waveOutPause(g_hOut);
    } else {
        (void)waveOutRestart(g_hOut);
    }
}

/* FMUSIC_LoadSongEx(data, 0, length, 0x8000, NULL, 0). The module parser copies
 * all data it needs before Sound_LoadAndPlayMusic releases the archive buffer. */
static void *Winmm_MusicLoad(const void *pData, u32 cbLength, u32 nMode)
{
    openmpt_module *pNew;
    openmpt_module *pOld;
    const char *pszError = NULL;
    int nError = 0;

    (void)nMode; /* 0x8000 = FSOUND_LOADMEMORY */
    if (pData == NULL || cbLength == 0u) {
        return NULL;
    }
    pNew = openmpt_module_create_from_memory2(pData, (size_t)cbLength,
                                               NULL, NULL, NULL, NULL,
                                               &nError, &pszError, NULL);
    if (pszError != NULL) {
        openmpt_free_string(pszError);
    }
    if (pNew == NULL) {
        return NULL;
    }

    AcquireSRWLockExclusive(&g_MixerLock);
    pOld = g_pMusicModule;
    g_pMusicModule = pNew;
    g_bMusicPlaying = false;
    g_bMusicPaused = false;
    g_bMusicLooping = false;
    g_bMusicEnded = false;
    ReleaseSRWLockExclusive(&g_MixerLock);
    if (pOld != NULL) {
        openmpt_module_destroy(pOld);
    }
    return pNew;
}

static void Winmm_MusicSetLooping(void *pMod, i32 bLoop)
{
    openmpt_module *pModule = (openmpt_module *)pMod;

    AcquireSRWLockExclusive(&g_MixerLock);
    if (pModule != NULL && pModule == g_pMusicModule) {
        g_bMusicLooping = bLoop != 0;
        (void)openmpt_module_set_repeat_count(pModule,
                                              g_bMusicLooping ? -1 : 0);
    }
    ReleaseSRWLockExclusive(&g_MixerLock);
}

static void Winmm_MusicOptimize(void *pMod, i32 nMaxChannels, i32 nMinVolume)
{
    (void)pMod;
    (void)nMaxChannels;
    (void)nMinVolume;
}

static void Winmm_MusicPlay(void *pMod)
{
    openmpt_module *pModule = (openmpt_module *)pMod;

    AcquireSRWLockExclusive(&g_MixerLock);
    if (pModule != NULL && pModule == g_pMusicModule) {
        (void)openmpt_module_set_position_seconds(pModule, 0.0);
        (void)openmpt_module_set_repeat_count(pModule,
                                              g_bMusicLooping ? -1 : 0);
        g_bMusicPlaying = true;
        g_bMusicPaused = false;
        g_bMusicEnded = false;
    }
    ReleaseSRWLockExclusive(&g_MixerLock);
}

static void Winmm_MusicVolume(void *pMod, i32 nVolume)
{
    openmpt_module *pModule = (openmpt_module *)pMod;

    if (nVolume < 0) {
        nVolume = 0;
    }
    if (nVolume > 255) {
        nVolume = 255;
    }
    AcquireSRWLockExclusive(&g_MixerLock);
    if (pModule != NULL && pModule == g_pMusicModule) {
        g_nMusicVolume = nVolume;
    }
    ReleaseSRWLockExclusive(&g_MixerLock);
}

static void Winmm_MusicSetPaused(void *pMod, i32 bPaused)
{
    openmpt_module *pModule = (openmpt_module *)pMod;

    AcquireSRWLockExclusive(&g_MixerLock);
    if (pModule != NULL && pModule == g_pMusicModule) {
        g_bMusicPaused = bPaused != 0;
    }
    ReleaseSRWLockExclusive(&g_MixerLock);
}

static void Winmm_MusicFree(void *pMod)
{
    openmpt_module *pModule = (openmpt_module *)pMod;
    bool bOwned = false;

    AcquireSRWLockExclusive(&g_MixerLock);
    if (pModule != NULL && pModule == g_pMusicModule) {
        g_pMusicModule = NULL;
        g_bMusicPlaying = false;
        g_bMusicPaused = false;
        g_bMusicLooping = false;
        bOwned = true;
    }
    ReleaseSRWLockExclusive(&g_MixerLock);
    if (bOwned) {
        openmpt_module_destroy(pModule);
    }
}

/* Port-side query: true once this module has played out. A looping module
 * never reaches it (OpenMPT's repeat count -1 keeps producing frames). */
static bool Winmm_MusicFinished(const void *pMod)
{
    bool bFinished;

    AcquireSRWLockExclusive(&g_MixerLock);
    bFinished = (pMod != NULL && (const openmpt_module *)pMod == g_pMusicModule
                 && g_bMusicEnded);
    ReleaseSRWLockExclusive(&g_MixerLock);
    return bFinished;
}

/* Called by the mixer thread or init before the thread starts. */
static void Winmm_MixBuffer(WinmmMixBuffer *pBuffer)
{
    i32 aMix[WINMM_MIX_FRAMES] = { 0 };
    i16 aMusic[WINMM_MIX_FRAMES] = { 0 };

    pBuffer->bSampleDone = false;
    pBuffer->bMusicNonzero = false;
    pBuffer->bOutputNonzero = false;
    AcquireSRWLockExclusive(&g_MixerLock);

    if (g_pMusicModule != NULL && g_bMusicPlaying && !g_bMusicPaused) {
        size_t nFrames = openmpt_module_read_mono(g_pMusicModule,
                                                   (int32_t)g_nOutputRate,
                                                   WINMM_MIX_FRAMES, aMusic);
        for (u32 i = 0; i < nFrames; ++i) {
            i32 nMusicSample = ((i32)aMusic[i] * g_nMusicVolume) / 255;
            aMix[i] += nMusicSample;
            if (nMusicSample != 0) {
                pBuffer->bMusicNonzero = true;
            }
        }
        if (nFrames == 0u) {
            g_bMusicPlaying = false;
            g_bMusicEnded = true; /* the "loop off" end-of-track signal */
        }
    }

    for (u32 ch = 0; ch < WINMM_SFX_CHANNELS; ++ch) {
        WinmmVoice *pVoice = &g_aVoices[ch];
        if (pVoice->pSample == NULL) {
            continue;
        }
        for (u32 i = 0; i < WINMM_MIX_FRAMES; ++i) {
            i16 nSample;
            if (!Winmm_ReadNativeFrame(pVoice, &nSample)) {
                pVoice->pSample = NULL;
                pVoice->nSourceFrame = 0u;
                pBuffer->bSampleDone = true;
                break;
            }
            aMix[i] += ((i32)nSample * g_nSfxVolume) / 255;
        }
    }

    for (u32 i = 0; i < WINMM_MIX_FRAMES; ++i) {
        if (aMix[i] > INT16_MAX) {
            aMix[i] = INT16_MAX;
        } else if (aMix[i] < INT16_MIN) {
            aMix[i] = INT16_MIN;
        }
        if (aMix[i] != 0) {
            pBuffer->bOutputNonzero = true;
        }
        pBuffer->aPcm[i] = (i16)aMix[i];
    }

    ReleaseSRWLockExclusive(&g_MixerLock);
}

static DWORD WINAPI Winmm_MixerThread(void *pUser)
{
    HANDLE aWait[2] = { g_hStop, g_hWaveDone };

    (void)pUser;
    for (;;) {
        DWORD dwWait = WaitForMultipleObjects(2, aWait, FALSE, INFINITE);
        if (dwWait == WAIT_OBJECT_0) {
            return 0;
        }
        if (dwWait != WAIT_OBJECT_0 + 1u) {
            return 1;
        }
        for (u32 i = 0; i < WINMM_MIX_BUFFERS; ++i) {
            WinmmMixBuffer *pBuffer = &g_aMixBuffers[i];
            if (!pBuffer->bQueued || (pBuffer->hdr.dwFlags & WHDR_DONE) == 0u) {
                continue;
            }
            pBuffer->bQueued = false;
            if (pBuffer->bSampleDone) {
                SetEvent(g_hSampleDone);
            }
            if (pBuffer->bMusicNonzero && pBuffer->bOutputNonzero) {
                SetEvent(g_hMusicDone);
            }
            Winmm_MixBuffer(pBuffer);
            if (waveOutWrite(g_hOut, &pBuffer->hdr, sizeof(pBuffer->hdr))
                    == MMSYSERR_NOERROR) {
                pBuffer->bQueued = true;
            }
        }
    }
}

/* FSOUND_Close(). Stop the refill thread before resetting its queued buffers. */
static void Winmm_Close(void)
{
    openmpt_module *pModule;

    if (g_hStop != NULL) {
        SetEvent(g_hStop);
    }
    if (g_hMixerThread != NULL) {
        (void)WaitForSingleObject(g_hMixerThread, INFINITE);
        CloseHandle(g_hMixerThread);
        g_hMixerThread = NULL;
    }
    if (g_hOut != NULL) {
        (void)waveOutReset(g_hOut);
        for (u32 i = 0; i < WINMM_MIX_BUFFERS; ++i) {
            if (g_aMixBuffers[i].bPrepared) {
                (void)waveOutUnprepareHeader(g_hOut, &g_aMixBuffers[i].hdr,
                                              sizeof(g_aMixBuffers[i].hdr));
            }
            memset(&g_aMixBuffers[i], 0, sizeof(g_aMixBuffers[i]));
        }
        (void)waveOutClose(g_hOut);
        g_hOut = NULL;
    }
    g_bOpen = false;
    g_nOutputRate = 0u;
    if (g_hWaveDone != NULL) {
        CloseHandle(g_hWaveDone);
        g_hWaveDone = NULL;
    }
    if (g_hSampleDone != NULL) {
        CloseHandle(g_hSampleDone);
        g_hSampleDone = NULL;
    }
    if (g_hMusicDone != NULL) {
        CloseHandle(g_hMusicDone);
        g_hMusicDone = NULL;
    }
    if (g_hStop != NULL) {
        CloseHandle(g_hStop);
        g_hStop = NULL;
    }

    AcquireSRWLockExclusive(&g_MixerLock);
    memset(g_aVoices, 0, sizeof(g_aVoices));
    pModule = g_pMusicModule;
    g_pMusicModule = NULL;
    g_bMusicPlaying = false;
    g_bMusicPaused = false;
    g_bMusicLooping = false;
    g_bMusicEnded = false;
    ReleaseSRWLockExclusive(&g_MixerLock);
    if (pModule != NULL) {
        openmpt_module_destroy(pModule);
    }
}

const PufoAudioBackend g_pufoAudioWinmm = {
    Winmm_Init,
    Winmm_SetWindow,
    Winmm_SampleLoad,
    Winmm_SampleSetMode,
    Winmm_SampleFree,
    Winmm_PlaySound,
    Winmm_SetSfxVolume,
    Winmm_SetSfxPaused,
    Winmm_MusicLoad,
    Winmm_MusicSetLooping,
    Winmm_MusicOptimize,
    Winmm_MusicPlay,
    Winmm_MusicVolume,
    Winmm_MusicSetPaused,
    Winmm_MusicFree,
    Winmm_Close,
    Winmm_MusicFinished
};
