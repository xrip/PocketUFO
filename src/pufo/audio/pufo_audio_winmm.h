/* pufo_audio_winmm.h - the Win64 output backend at the PufoAudioBackend seam.
 *
 * Install it with Sound_SetBackend before Sound_InitAndLoadSamples; passing NULL
 * back returns the module to its no-op stub, which is what keeps every control-flow
 * test (init retry, sample naming, ready flag) independent of a sound device.
 *
 * Evidence for the seam itself is in pufo_audio.h: FSOUND_Init(22000, 32, 0) at
 * 0x9E2C0, FSOUND_SetHWND at 0x9E2C0-branch, FSOUND_Sample_Load(-2, data, 0xA000)
 * at 0x9E38C, FSOUND_Sample_SetMode(1) at 0x9E398, FMUSIC_LoadSongEx(data, 0, len,
 * 0x8000) at 0x9E1F4, FMUSIC_SetLooping/OptimizeChannels(16, 10)/PlaySong at
 * 0x9E1F8..0x9E210, FMUSIC_SetPaused at 0x9E230, FSOUND_SetSFXMasterVolume at
 * 0x9E518, FSOUND_SetPaused(FSOUND_ALL, flag) at 0x9E548 / 0xA0534 / 0xA0620.
 *
 * Deviations from fmodce.dll, all recorded in docs/parity/render-audio.md:
 *  - FMOD mixed at 22000 Hz; this backend opens the device at the shipped samples'
 *    own format instead of resampling. main.dat's TACT*.WAV are PCM, mono, 11025 Hz,
 *    8-bit (measured across the archive), so pitch is right with no resampler and
 *    8-bit payloads are widened to 16-bit on load. A sample in any other format
 *    fails the load the way FSOUND_Sample_Load would.
 *  - FSOUND_SetHWND has no winmm counterpart: waveOut needs no window.
 *  - .it music keeps load / looping / volume / pause state only. There is no MOD
 *    playback here, so a loaded module is silent; every flag the game reads back
 *    (byte_187F24, byte_187F28) is still the original's.
 *  - FMOD 3 mixed 32 channels; winmm queues one write per play on one device, so a
 *    sample that is still sounding is skipped rather than overlapped.
 */
#ifndef PUFO_AUDIO_WINMM_H
#define PUFO_AUDIO_WINMM_H

#include "audio/pufo_audio.h"

/* The backend table to hand Sound_SetBackend. */
extern const PufoAudioBackend g_pufoAudioWinmm;

/* True when the machine has at least one wave-output device. */
bool PufoAudioWinmm_HasDevice(void);

/* Waits up to nTimeoutMs for the last waveOutWrite to report WOM_DONE.
 * Returns true when the completion arrived. Test helper only. */
bool PufoAudioWinmm_WaitDone(u32 nTimeoutMs);

/* Drops up to nMaxSignals already-queued device messages (WOM_OPEN arrives on the
 * event too, so a gate that waits for WOM_DONE must drain the open first).
 * Test helper only. */
void PufoAudioWinmm_DrainDone(i32 nMaxSignals);

#endif /* PUFO_AUDIO_WINMM_H */
