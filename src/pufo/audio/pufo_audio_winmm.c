/* pufo_audio_winmm.c - the Win64 sound output, at the PufoAudioBackend seam.
 *
 * The original links fmodce.dll (FMOD 3 for WinCE, ARM); that DLL is not re-linked
 * here (CLAUDE.md constraint), so the game-side logic stays in pufo_audio.c and this
 * file answers the backend table with winmm. The addresses behind each slot are in
 * pufo_audio_winmm.h.
 *
 * The device is opened once, at init, in the format the shipped samples actually
 * use: main.dat carries TACT0.WAV..TACT34.WAV as PCM / 1 channel / 11025 Hz / 8-bit,
 * measured across the whole archive. Opening at that rate is what keeps pitch right;
 * opening at FMOD's 22000 Hz mix rate would play every sample an octave too fast,
 * and there is no resampler here to fix it. 8-bit samples are widened to signed
 * 16-bit at load; anything else (other rates, other channel counts, non-PCM tags)
 * fails the load the way FSOUND_Sample_Load would.
 */
#include "audio/pufo_audio_winmm.h"

#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <mmsystem.h>

/* The format the device is opened in and the format every accepted sample is
 * converted to. From the shipped archive, not from FMOD's mix rate. */
#define WINMM_RATE   11025u
#define WINMM_BITS   16u

typedef struct WinmmSample {
    WAVEHDR hdr;      /* hdr.lpData -> pData, hdr.dwBufferLength -> cbData */
    void   *pData;    /* the 16-bit mono copy, owned by this record */
    u32     cbData;
    bool    bPrepared;
} WinmmSample;

static HWAVEOUT g_hOut;
static HANDLE   g_hDone;      /* the waveOut completion event (CALLBACK_EVENT) */
static bool     g_bOpen;

static u32 Winmm_RdU32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u16 Winmm_RdU16(const u8 *p) { return (u16)((u32)p[0] | ((u32)p[1] << 8)); }

bool PufoAudioWinmm_HasDevice(void)
{
    return waveOutGetNumDevs() > 0u;
}

bool PufoAudioWinmm_WaitDone(u32 nTimeoutMs)
{
    if (g_hDone == NULL) {
        return false;
    }
    return WaitForSingleObject(g_hDone, nTimeoutMs) == WAIT_OBJECT_0;
}

void PufoAudioWinmm_DrainDone(i32 nMaxSignals)
{
    if (g_hDone == NULL) {
        return;
    }
    while (nMaxSignals > 0
           && WaitForSingleObject(g_hDone, 0) == WAIT_OBJECT_0) {
        --nMaxSignals;
    }
}

/* FSOUND_Init(mixrate, maxchannels, flags). The mixrate is the one argument the
 * backend ignores: this output runs at the samples' own rate (header note).
 * No device, or a device that will not open, is a false -- exactly the state the
 * original reaches when FSOUND_Init keeps failing, and Sound_InitAndLoadSamples
 * sets its ready flag either way (0x9E2D4). */
static bool Winmm_Init(u32 nMixRate, i32 nMaxChannels, u32 nFlags)
{
    WAVEFORMATEX fmt;

    (void)nMixRate;
    (void)nMaxChannels;
    (void)nFlags;

    if (g_bOpen || waveOutGetNumDevs() == 0u) {
        return false;
    }
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = 1;
    fmt.nSamplesPerSec  = WINMM_RATE;
    fmt.wBitsPerSample  = WINMM_BITS;
    fmt.nBlockAlign     = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8u);
    fmt.nAvgBytesPerSec = (DWORD)(fmt.nSamplesPerSec * fmt.nBlockAlign);

    g_hDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (g_hDone == NULL) {
        return false;
    }
    if (waveOutOpen(&g_hOut, WAVE_MAPPER, &fmt, (DWORD_PTR)g_hDone, 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(g_hDone);
        g_hDone = NULL;
        return false;
    }
    g_bOpen = true;
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
            u32 nRate;
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
            (void)nBlock;
            if (nTag != WAVE_FORMAT_PCM || nChan != 1u || nRate != WINMM_RATE
                || (nBits != 8u && nBits != 16u)) {
                return NULL; /* not the device's format: refuse, as FMOD would */
            }
        } else if (memcmp(p + pos, "data", 4) == 0) {
            pSrc  = p + pos + 8u;
            cbSrc = cb;
        }
        pos += 8u + cb + (cb & 1u); /* chunks are word-padded */
    }
    if (pSrc == NULL || cbSrc == 0u || (nBits != 8u && nBits != 16u)) {
        return NULL;
    }

    s = (WinmmSample *)calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->cbData = cbSrc * (nBits == 8u ? 2u : 1u);
    s->pData  = malloc(s->cbData);
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
            p16[i] = (i16)(((i32)pSrc[i] - 128) << 8);
        }
    }
    s->hdr.lpData         = (LPSTR)s->pData; /* mmeapi.h's WAVEHDR names it lpData */
    s->hdr.dwBufferLength = s->cbData;
    s->hdr.dwFlags        = 0u;
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
    if (s->bPrepared && g_bOpen
        && waveOutUnprepareHeader(g_hOut, &s->hdr, sizeof(s->hdr)) != MMSYSERR_NOERROR) {
        /* ERR_STILLPLAYING: the queue has to be drained before the header is
         * released. Sound_Shutdown frees samples right before FSOUND_Close's
         * counterpart, so cutting the queue here is what the original's
         * FSOUND_Close does anyway. */
        waveOutReset(g_hOut);
        (void)waveOutUnprepareHeader(g_hOut, &s->hdr, sizeof(s->hdr));
    }
    free(s->pData);
    free(s);
}

/* FSOUND_PlaySound(-1, sample). The return value is FMOD's channel handle; the
 * callers in this image ignore it, so a queue refusal returns -1 and nothing else
 * reads it. A sample that is still sounding is not duplicated: winmm queues one
 * write per header (recorded deviation). */
static i32 Winmm_PlaySound(void *pSample)
{
    WinmmSample *s = (WinmmSample *)pSample;

    if (!g_bOpen || s == NULL) {
        return -1;
    }
    if (!s->bPrepared) {
        if (waveOutPrepareHeader(g_hOut, &s->hdr, sizeof(s->hdr)) != MMSYSERR_NOERROR) {
            return -1;
        }
        s->bPrepared = true;
    }
    if (waveOutWrite(g_hOut, &s->hdr, sizeof(s->hdr)) != MMSYSERR_NOERROR) {
        return -1;
    }
    return 0;
}

/* FSOUND_SetSFXMasterVolume(level). The level the port passes is the original's
 * own 0..255 (Options maps its 0..5 step through 12*step+4 then 4*value-1);
 * waveOut takes 0..65535 per channel. */
static void Winmm_SetSfxVolume(i32 nVolume)
{
    DWORD w;

    if (!g_bOpen) {
        return;
    }
    if (nVolume < 0) {
        nVolume = 0;
    }
    if (nVolume > 255) {
        nVolume = 255;
    }
    w = (DWORD)(nVolume * 257);
    (void)waveOutSetVolume(g_hOut, w | (w << 16));
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

/* FMUSIC_*: state only (header note). The token stands for "a module was loaded",
 * which is what byte_187F28 / Sound_HasMusic answer with in the original, and the
 * IMPT magic is checked so a non-module image fails the load the way
 * FMUSIC_LoadSongEx would. */
static void *Winmm_MusicLoad(const void *pData, u32 cbLength, u32 nMode)
{
    (void)nMode; /* 0x8000 = FSOUND_LOADMEMORY */
    if (pData == NULL || cbLength < 4u || memcmp(pData, "IMPT", 4) != 0) {
        return NULL;
    }
    return calloc(1, 4u); /* a non-NULL handle; nothing is played from it */
}

static void Winmm_MusicState(void *pMod, i32 nArg)
{
    (void)pMod;
    (void)nArg; /* looping, paused: recorded state, no playback to affect */
}

static void Winmm_MusicOptimize(void *pMod, i32 nMaxChannels, i32 nMinVolume)
{
    (void)pMod;
    (void)nMaxChannels;
    (void)nMinVolume;
}

static void Winmm_MusicPlay(void *pMod)
{
    (void)pMod; /* no MOD playback on Win64 (recorded deviation) */
}

static void Winmm_MusicVolume(void *pMod, i32 nVolume)
{
    (void)pMod;
    (void)nVolume;
}

static void Winmm_MusicFree(void *pMod)
{
    free(pMod);
}

/* FSOUND_Close(). */
static void Winmm_Close(void)
{
    if (!g_bOpen) {
        return;
    }
    (void)waveOutReset(g_hOut);
    (void)waveOutClose(g_hOut);
    g_bOpen = false;
    g_hOut  = NULL;
    if (g_hDone != NULL) {
        CloseHandle(g_hDone);
        g_hDone = NULL;
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
    Winmm_MusicState,   /* FMUSIC_SetLooping */
    Winmm_MusicOptimize,
    Winmm_MusicPlay,
    Winmm_MusicVolume,
    Winmm_MusicState,   /* FMUSIC_SetPaused */
    Winmm_MusicFree,
    Winmm_Close
};
