# PocketUFO final acceptance

Date: 2026-10-29. Status: **COMPLETE against the original Win64 port contract**.

Later user-requested follow-up: [status-button caption correction](parity/status-slow-label-2026-10-29.md).
That Release build passed the focused Geoscape test and an isolated arrival-screen
check. The hashes and full 67-suite result below describe the acceptance build
before this text-only change.

This is the current verdict. Earlier incremental notes are preserved in
[acceptance history](../.workers/port-goal/acceptance-history-20261029-final).
They are not a list of unfinished implementation work.

## Contract audit

| Original requirement | Result | Evidence |
|---|---|---|
| C23, Clang, CMake, Win64; clean builds | PASS | CMakeLists.txt selects C23 and warnings-as-errors; CMakeCache selects x64/ClangCL. [Debug build](../.workers/port-goal/title-owner-Debug-build.log) and [Release build](../.workers/port-goal/title-owner-Release-build.log) have exit0 and zero diagnostics. The executable is PE32+ machine0x8664. |
| All original subsystems recreated | PASS | [PARITY.md](../PARITY.md) maps all14 inventory groups to the native implementation. Startup/config, data/saves, map/world, entities, strategy/tactics, UI/input, rendering and audio have implementation and original-code comparison records. No required missing gameplay body remains identified. |
| Win64 launch and main playable flows | PASS | Native window input, New Game/base, Options Save/Load, name edit, title/credits return and reload pass. Recorded flows cover interception, air combat, deployment, move/fire/throw, enemy turns, victory/abort return, flight home, second deployment, research, manufacture, transfers and ending handlers. See linked results below. |
| Per-subsystem IDA comparisons and explicit deviations | PASS | [game-core](parity/game-core.md), [data-layer](parity/data-layer.md), [ui-core](parity/ui-core.md), [render/audio](parity/render-audio.md) and [scaffold](parity/scaffold.md) contain original function/family comparisons and adaptations. The independent audit found this evidence substantially wider than the newer body-capture index. |
| Final original-function map, no unexplained gaps | PASS | All1103 inventoried entries are accounted for:973 source-cited entries and130 explained platform/runtime/forwarding entries; no uncited behavior-bearing entry. [Header-body map](parity/header-citation-body-map.tsv) resolves all223 former header-only entries. [0xA42E0](parity/arm-context-restore-2026-10-29.md) is now correctly an ARM ABI adaptation. |
| Preserve original data formats | PASS | Original main.dat/lang.dat load unchanged. The35 save-body comparisons, codec tests, cold tactical/strategic loads and native saved-name round trip cover the implemented original wire formats. No conversion of the original archives is required. |
| Thin native adapters; do not relink WinCE DLLs | PASS | The actual PE imports USER32/GDI32/KERNEL32/WINMM/libopenmpt/CRT. Neither fmodce.dll nor gx.dll is imported. Native GDI and WinMM/openmpt adaptations are recorded. |
| Update IDB with supported findings | PASS | Shared screen-word, tactical queue-owner, text-line pointer and ARM restore facts were saved and read back in pufobeta.exe.i64. Original PE instruction bytes were not changed. |

The verification contract in AGENTS.md was re-read at closeout. Its four gates
are satisfied: clean build, running main flows, the complete mapped/comparison
report, and this requirement-by-requirement audit.

## Current build and checks

- Source fingerprint: `814370840079BDEA4547A8E550FC1BDD915266C08E3D66B47F1061D1A151D980`.
- Release EXE SHA256: `9CAFF8707108660D7C3DA93099998F20223828A130D78A439192AD512FAA6D58`.
- Debug EXE SHA256: `119E8CC34E17491F3628C22192DE1FB251438B08EF1CEA606FD01F55B98D614E`.
- Current Release: [67/67 suites pass](../.workers/port-goal/parity-report.log).
- Current affected App/startup/input/title suites: [Debug4/4](../.workers/port-goal/title-owner-Debug-tests.log),
  [Release4/4](../.workers/port-goal/title-owner-Release-tests.log).
- The final report-only classification refresh reused those real test results
  after checking unchanged source and executable hashes; it did not invent a
  new test run. [Refresh record](../.workers/port-goal/final-report-refresh.log).

## Playable evidence

- [Latest native name/title/load proof](parity/name-title-native-2026-10-29.json):
  OS mouse/key messages edit the second Soldier, save, return to title, enter/leave
  credits, load at startup and save again. Fresh load retains all8 names and
  exactly the intended single appended glyph.
- [Credits and campaign continuation](parity/titles-ending-review-2026-10-29.md):
  credits exit, flight home, saved progress, a second deployment and device-buffer
  audio output.
- [Battle/economy follow-up](parity/launch-first-followup-2026-10-29.md):
  repaired enemy-turn callback, research/manufacture, transfer, native victory
  and ending-screen handlers.
- [Earlier current-flow checkpoint](parity/playable-checkpoint-2026-10-29.md):
  native startup/Options, ground play, paid delivery and cold tactical continuation.
- [Independent contract audit](../.workers/final-contract-audit-20261029/RESULT.md)
  and its final ABI disposition confirmed no other unmet original requirement.

All automated native runs used isolated desktops; the user's input desktop
remained Default. Original ARM execution, companion tools, mobile targets and
packaging were not part of this work.

## Explicit limits, not new completion gates

This is engineering acceptance of the original port contract, not a mathematical
identity claim for every possible execution. Native ABI/API and audio-backend
differences, safe handling of invalid/malformed inputs, unused register returns
and allocator residue are documented adaptations or evidence limits.

Independent original-created WinCE saves and bit-identical FMODCE waveforms were
not available. Their absence does not replace the recorded format/behavior
comparisons with a claim of interoperability testing that never ran.

A natural full campaign was not required by the contract. The32-turn natural
base-defense loss driver still had3 survivors; ending handlers were separately
verified with stated fixtures. That observation is not an unimplemented feature.

The supplemental body index is not a remaining-function counter. Historical
per-entry/family comparisons remain evidence; hundreds of duplicate captures
are not required to finish this goal. No further mandatory port work remains
identified under the original acceptance criteria.
