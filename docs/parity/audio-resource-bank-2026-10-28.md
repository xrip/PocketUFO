# Shipped audio bank and null-slot continuation

Updated: 2026-10-28. Data/source review plus compiled regression; runtime pending.
Production audio/game code was not changed. No test executable, decoder API or
audio device was run/opened in this pass.

## Actual shipped resources

Read both original archives with the21-byte directory format and language-first
lookup priority. All directory ranges checked were inside their files.
The [40-resource inventory](audio-resource-inventory.tsv) records member offsets,
sizes, hashes, format fields and source guard disposition.

- TACT0.WAV through TACT34.WAV exist with no missing index.
- All35 have PCM tag1, one channel,11025 Hz, block alignment1 and8-bit samples.
- 34 have nonempty PCM data. TACT18.WAV is a44-byte member with a
  valid format header and a zero-length data chunk; it is not a missing archive key.
- BACK0.IT through BACK4.IT have IMPM signatures. Their memory-loader connection
exists via libopenmpt; this inventory is not decoder success or FMOD equivalence.

| Archive | Directory entries | SHA-256 |
| --- | ---: | --- |
| lang.dat | 12 | F0A62DA355CCBB9156ABC2C6ADC18DA0E3FECB975B1C92DB09F8925ECE47DD76 |
| main.dat | 891 | B97DE23D47C3F02393FEA968A826413BB3F7054E79661B211B244BCBE9406B7A |

The inspected WinMM reader's format guards accept the nonempty WAV headers.
It explicitly returns null for zero-length data. This is source inspection,
not an executed decoder result. Allocation/device/mixing behavior remains outside
this data-only check.

## Why a count of35 is not35 decoded sounds

Sound_InitAndLoadSamples scans until a resource key is absent, not until the
backend returns a null sample. It preserves the null entry and advances.
Sound_GetSampleCount reports that indexed span. With the native empty member,
35 attempted slots can therefore include a null handle at18.

Raw load tail confirms the client behavior:0x9E38C calls Sample_Load;
0x9E394 stores R0 into the current bank cell;0x9E398 calls Sample_SetMode.
There is no null test before incrementing the file index and bank pointer and
branching to the next scan at0x9E3B4. The port has the same continuation.
[Raw tail and archive identities](audio-null-slot-raw-current.json) are preserved.
No claim is made about the original external FMOD decoder's handle for this file.

## Regression purpose

TestNullSampleSlot uses the real archive scan and a recording backend. It:

- returns a null handle only for index18;
- gives every other slot a distinct opaque handle;
- requires35 load attempts and35 indexed slots;
- checks mode setup at18 and34;
- requires Sound_Play(34) to pass slot34's exact handle, then Sound_Play(18)
  to preserve/forward the null slot;
- checks release of34 non-null handles and one backend close.

This detects premature scan termination, index compaction or a wrong later-slot
lookup. It does not decode PCM, prove audible output or compare FMOD mixing.
The old count label now says indexed slots/load attempts. The fake-loader comment
no longer claims byte-length assertions it did not perform.

The suite also contains separate shipped-module PCM and optional device-output
cases. Those scopes are distinct from this wrapper test. None ran here.

## Compiled test-inclusive candidate

Both commands exited0 with zero compiler diagnostics:

```
cmake --build build --config Debug --target pocketufo test_audio --parallel 2
cmake --build build --config Release --target pocketufo test_audio --parallel 2
```

[Debug log](../../.workers/luna-mission-init-audit/debug-audio-null-slot-compile.log),
[Release log](../../.workers/luna-mission-init-audit/release-audio-null-slot-compile.log).

434-file source/test fingerprint:
**F5914C04968A8177704EDC34C09728645C7D8036AB2A4847FB520FC4C0C16073**.

| Artifact | SHA-256 |
| --- | --- |
| tests/test_audio.c | 52F20C0E535AE7698E984546A7F9CC252C46A77F6EBF6C9BD75805C7DE20D159 |
| src/pufo/audio/pufo_audio.c | D8BF2861B5E3E51BFD2CAD90E35609C9305114884BA80A0C0060E03A8422F8FC |
| src/pufo/audio/pufo_audio_winmm.c | BEF1914E7F5BDED81C7EDA9EE415DA625A764D357CDB44C07BC9F88A8D0F6A14 |
| build/tests/Debug/test_audio.exe | 1C591DDC854B8DA14DD714018815DC1A638ACD147850D2CF3F727AC96C0AC240 |
| build/tests/Release/test_audio.exe | B1E4CE7EE9ABE79D54C334EFBCFB22A77AF4EF71D40EF7E650CA77831DFAF22D |
| build/src/pufo/Debug/pocketufo.exe | 22927644A4D83BCA33B439447DDDE69EB5179ADCBD9C9EEDFB9BBEB75D8871F5 |
| build/src/pufo/Release/pocketufo.exe | A4501C71EAE8BD615E89B52AADA2C5AD8DC6EAECA8DB26EB6340DD0F2FB189A4 |

The game images are unchanged from the
[shared-window/phase candidate](shot-map-windows-2026-10-28.md). Only the audio
test source changed in this pass; the fingerprint includes that new test.
Other suites still need their full rebuild before the authorized-host run.
No current audio/whole-game runtime pass is inferred from the source review.
