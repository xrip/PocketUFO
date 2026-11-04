# Audio wrapper body audit

Six original bodies (1,176 bytes) were read in full. No new valid-data source
fault was confirmed in the wrapper contracts. This is not audio output parity.

The raw bodies, literal pools, external-call operands and incoming code xrefs
are in [raw evidence](audio-wrapper-raw-2026-10-29.json). One component query was
used for the six members; its empty call/shared-data lists omit calls and globals
that the instructions directly show. No transitive expansion was used. FMODCE
internals, WinMM mixing and decoder output remain outside this bounded audit.

## Checked behavior

- Sound_Play0x9E0C0 tests ready, toggles its byte and advances RNG with bound200
  before the mute gate. The port preserves this order; it adds an index bound.
- Init0x9E268 uses22000Hz and32 channels. Raw retry control gives11 attempts and
  10 sleeps of100ms. It sets ready even after all attempts fail, then loads
  contiguous prefix-number WAV assets. Port caps the sample array at202 and
  guards allocation/read failures. Its stub does not suppress this ready/RNG
  behavior; the source fallback comment claiming a ready-flag early-out is stale.
- Music load0x9E130 builds back-number IT names, loads mode0x8000, sets looping,
  optimize arguments16/10, starts playback, conditionally pauses, releases the
  source buffer, and sets loaded. The port guards failed handles. Replacing
  FMODCE with openmpt/WinMM is an explicit platform adaptation.
- Shutdown0x9E3D4 frees samples and loaded music, closes the backend, and clears
  ready/loaded. The port also clears pointers/count and skips NULL frees.
- Volume bodies0x9E454/0x9E4DC compute unmuted48*step+15 or muted-1, then apply
  pause state. C callers compute that value through Options helpers and pass it
  to the wrappers. The original calls read globals and take no level argument.

App installs g_pufoAudioWinmm before startup audio use and binds both live mute
bytes. Main-menu/title/mission/geoscape source callers apply current options.
The [five raw init caller windows](audio-init-caller-raw-2026-10-29.json) show
the init return being discarded; the ending-screen branch tests R5, not R0.

## Open limits

Raw Sound_Play has196 incoming code xrefs. This audit does not close every
callback return-forwarding path. The same limit applies to options callbacks;
member equivalence is limited to callers that discard the raw result.
FMODCE behavior on invalid handles, backend optimization, exact mixed output,
device failure/recovery and audible acceptance remain unproven. No claim that
FMODCE clamps a particular invalid volume is made from a source comment.

Existing test_audio source has mocked contract tests plus decoder/device tests.
None was run. No source or IDB change was made. Existing member names are retained;
no newly supported name/type change was needed. New names from debug strings:
n/a. IDB write/readback: n/a. Full bodies and bounded calls: pass. External
implementation, callback result paths and native output: explicitly open.

The [member result](audio-wrapper-result-2026-10-29.json) records each input
domain and adaptation. Source remains the
[compiled candidate](base-load-join-build-artifacts-2026-10-29.json).
