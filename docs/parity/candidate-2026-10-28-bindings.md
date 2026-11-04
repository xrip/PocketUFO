# Compile-verified candidate after mission binding repairs

Status: Gate 1 source stabilization complete; runtime acceptance pending.
This candidate is not claimed playable or parity-complete.

## Source review scope

- Terrain-shot resolver: installed before `Tact_InitMission`, receives the current
  World/map, geometry, state, rosters and hit windows; records the current weapon
  before invoking the production resolver. No live-mission flag blocks init-time
  calls. This is a wiring review, not complete resolver behavior verification.
- Panel cleanup: restores capacities 14,14,14,14,28 before the row destructor.
  The Debug heap fixture constructs those capacities, hides the rows, and checks
  heap balance after World cleanup. It remains unexecuted.
- Restart lifetime: all fields used after immediate shutdown by the Mars tail
  are World-owned. The layer write no longer targets the freed heap Map. Deferred
  dispatch copies it to the still-owned old Map and initializes after unwinding.
  The existing queued case does not prove the direct case; direct runtime
  coverage is still required, not silently treated as passed.
- Placement/sprite refresh and fixture isolation are reviewed in the linked
  [binding evidence](mission-placement-refresh-2026-10-28.md).

No further code change resulted from this review except the terrain fixture
isolation correction. No runtime executable was launched.

## Identity

Source-tree fingerprint: **8CD5EC9BDA93C97539B546C660FBE559B94EA6205F2794323AA5B23E6E6B82A5**.
Scope: all 434 files under `src`, `tests`, `cmake` (where present), plus root
`CMakeLists.txt`. Sort unique workspace-relative POSIX paths lexically; for each
write `path`, one space, lowercase file SHA-256, LF. Hash the concatenated UTF-8
rows, including final LF. This fingerprint detects changes; it is not an immutable
checkout or a replacement for preserving the working tree.

| Artifact | SHA-256 |
| --- | --- |
| Debug game | 03F03404699B9D099A8D5BDFFF2B6D2FC97027CA3D4EB134D730DCD2C2ED62A5 |
| Release game | A40A3B8EE3230967BEE2ABEACA8138DC7F97732C7E824F7DE4827C2F84D6AF14 |
| Debug terrain test | 05CFE49685B41C5F74728B3DA706D80F52D8008C2065D48746B4CED1BA91E338 |
| Release terrain test | 82D0D29DF66657280A6B8DF3AEEB84472431BF7DD522B24AF3CECFDB6B15C872 |
| main.dat | B97DE23D47C3F02393FEA968A826413BB3F7054E79661B211B244BCBE9406B7A |
| lang.dat | F0A62DA355CCBB9156ABC2C6ADC18DA0E3FECB975B1C92DB09F8925ECE47DD76 |

Both configurations compiled `pocketufo`, `test_geoscapeframe`, `test_tactinit`
and `test_terrain`, exit 0, zero compiler diagnostics. Logs are linked from the
binding evidence. This is not a clean rebuild of all project targets or a test pass.

Next acceptance requirements are Gate 2 in the completion plan: same-candidate
gameplay and full suites on an authorized test host. Required source fixes found
by parity work or runtime must reopen Gate 1 and produce new candidate identities.
