# Playable-flow acceptance

Updated: 2026-10-29. Original main-flow gate: **PASS**.
See [final acceptance](COMPLETION_AUDIT.md) for the complete contract and limits.

| Flow | Recorded result |
|---|---|
| Native startup, New Game, base placement | PASS |
| Native Options Save/Load | PASS |
| Native name edit, title/credits return, startup reload and second save | PASS on the final build |
| Interception, air combat, landing and deployment | PASS in recorded scripted-control flows |
| Move, fire, grenade prime/throw/detonation and enemy turn | PASS |
| Native battle victory/abort, debrief and globe return | PASS |
| Carrier flight home, landing transfer and recovery | PASS |
| Strategic and tactical cold save/load; second deployment | PASS |
| Research, unlock, manufacturing and delivery | PASS |
| Inter-base transfer and arrival | PASS |
| Ending screens and return to startup | PASS with explicitly staged end conditions |
| Audio device buffers | Nonzero music PCM and completed sample buffers observed |

Evidence is linked in [native UI](parity/name-title-native-2026-10-29.json),
[campaign/credits](parity/titles-ending-review-2026-10-29.md),
[battle/economy](parity/launch-first-followup-2026-10-29.md) and the
[earlier checkpoint](parity/playable-checkpoint-2026-10-29.md).
Each report retains its source/image scope and fixture limits.

Soldiers may remain on the battlefield when the carrier leaves. This is valid
behavior, as confirmed by the user, and is not a defect or a completion blocker.

A natural complete campaign and guaranteed defeat within32 turns were not
original acceptance requirements. No such result is claimed. Platform/audio
bit identity and original-created WinCE-save evidence limits remain explicit.

All automatic native checks used private desktops and kept the input desktop
unchanged. Historical open-flow notes are preserved in
[the archive](../.workers/port-goal/acceptance-history-20261029-final).
