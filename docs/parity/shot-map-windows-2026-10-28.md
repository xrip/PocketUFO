# Shared shot windows and area-fire phase fields

Updated: 2026-10-28. Source repaired; affected targets compile in both configurations.
No game, test or debugger executable was launched. Runtime acceptance is pending.

## Shared hit-window defect

Original0x2F258 onward clears the Map window at+0x4EBC:41x41x4 dwords.
For non-normal resolution,0x2F764..0x2F784 builds source Map+0x4EBC,
destination Map+0xB7CC and length0x6910, then calls memmove.
All original resolver callers therefore operate on the same Map windows.

The port's proximity and death-explosion paths passed null buffer arguments.
They selected a detached working scratch array and skipped the snapshot copy,
despite using a live Map. That left the Map windows disconnected from those
resolutions. The earlier weapon-owner repair did not restore this window state.

CombatCtx now also carries optional paHitWindow/paHitWindowSave bindings.
Explicit call buffers take precedence; missing buffers use the context's Map
windows. Unowned standalone contexts retain their working scratch fallback.
All four World combat contexts bind the current Map pair: terrain, spawn kill,
active-unit tick and shared step/proximity. Initialization/reset sites were checked.

No attenuation formula or loop was changed. Raw downward propagation reads the
current working window; it does not bulk-restore the snapshot between directions.
The review checked this directly before deciding on the storage-only repair.

## Two area-fire field mismatches

Raw mode1/2 tail:

- 0x2FA68 (0100d1e5) reads a byte at Map+1, then the zero test gates setting
  the tactical phase byte. The port incorrectly checked action latch Map+2.
- 0x2FA84/0x2FA8C build0x1118C;0x2FA90 sets R2=0;
  0x2FA94 (032080e7) stores zero to tactical state+0x1118C.
  The port instead cleared nUnk70024Slot4 at+0x11198.

World_TactAreaFireThunk now checks bMapUnk1139B9 and resets nPhaseCounter.
The old slot's misleading source comment was corrected. The unrelated slot
remains unchanged. These are offset/owner corrections, not a new animation rule.

[Raw words](shot-map-windows-raw-current.json) preserve clear/copy, downward
propagation and phase-tail ranges independently of IDA comments/macros.

## Test purposes and limits

Existing test_combat cases now exercise null-buffer owner fallback:
direct-hit FOV writes into the bound working window; ceiling break snapshots
the bound saved window before attenuation; due timed detonation uses the bound
working window. Existing damage, range, snapshot, countdown and removal checks
remain. The captured FOV pointer is reset to null before each resolver fixture.

New TestAreaFirePhaseWorld in test_geoscapeframe launches its own seeded normal
UFO mission and settles it. It checks the actually installed callback and owner,
then stages opposing flag values:

| Map+1 | Action latch+2 | Expected phase from initial0 |
| ---: | ---: | ---: |
| 0 | 1 | 1 |
| 1 | 0 | 0 |

Each case requires nPhaseCounter reset and a sentinel in the unrelated+0x11198
slot preserved. Staged values are restored. The next PSI fixture creates its
own mission; the phase fixture does not borrow the damaged-mission/PSI state.

These checks have not run. There is no executed red/green result, current
explosion animation proof, or native gameplay acceptance result. Rebuild all
targets on the authorized host before full suites, then verify actual timed/
proximity/death explosions and phase progression through the live game tick.

## IDB metadata

Added raw-offset comments at0x2FA68 and0x2FA94 only. No names, types, instructions
or control flow were changed. Both comment read-backs matched. IDA save with
DBFL_BAK returned true. Comment preimages/postimages are preserved:
[before](../../.workers/luna-mission-init-audit/area-phase-idb-comments-before.json),
[after](../../.workers/luna-mission-init-audit/area-phase-idb-comments-after.json).
The comments are annotations; the raw words above remain the evidence.

## Latest compiled candidate

Both commands exited0 with zero diagnostics:

```
cmake --build build --config Debug --target pocketufo test_combat test_unit test_vis test_tactstate test_turn test_step test_enemyai test_ballist test_geoscapeframe test_appshell --parallel 2
cmake --build build --config Release --target pocketufo test_combat test_unit test_vis test_tactstate test_turn test_step test_enemyai test_ballist test_geoscapeframe test_appshell --parallel 2
```

[Debug log](../../.workers/luna-mission-init-audit/debug-shot-window-compile.log),
[Release log](../../.workers/luna-mission-init-audit/release-shot-window-compile.log).
Only these targets were built; full-suite executables must be rebuilt before use.

434-file source fingerprint:
**9511AA5430F774047B0A1185E7F006C800796350BF290262E22B8E8AEF1EBEDF**.

| Artifact | SHA-256 |
| --- | --- |
| src/pufo/game/pufo_combat.h | 9A6CF1B7E35857F7876B7D8DE95EB951C81D9DE6DB361A3682A415BB300F6D77 |
| src/pufo/game/pufo_combat.c | BF8ECD76B2FD5BA2D814C7012609394B87747CA9287F86E2A29045B033AB6304 |
| src/pufo/app/pufo_world.c | 8F95CB1058392B1C48C9A397205349E78BBB3DD451F647F5A09CD7C097DE6FCF |
| tests/test_combat.c | 7440B84489FB70BEF7D1CDB18ACF91B2F6A012DD0F67684128B6E58B26C7B3C5 |
| src/pufo/game/pufo_tactstate.h | E4A41E98E2CD1F4082B0ADD2A8181C4432D2F701FD63C1368AA96450C895E9CB |
| tests/test_geoscapeframe.c | 81A99EE594A82C993261CF1F2A6BD1B9F00B0E44878E6061C127D6427CE801A6 |
| build/tests/Debug/test_geoscapeframe.exe | 517998DFFD0B984075E00DF95569D49B1694D24324E77EB524387822D458988E |
| build/tests/Release/test_geoscapeframe.exe | 2E6916D2F06831AF973518EF07069C7078D85139643E8340A1657B895E39D0A4 |
| build/src/pufo/Debug/pocketufo.exe | 22927644A4D83BCA33B439447DDDE69EB5179ADCBD9C9EEDFB9BBEB75D8871F5 |
| build/src/pufo/Debug/pocketufo.pdb | 92E7D507D3DC1794B9BFAF4E2217E36196B8631C457DEE89D662994917918DC5 |
| build/src/pufo/Release/pocketufo.exe | A4501C71EAE8BD615E89B52AADA2C5AD8DC6EAECA8DB26EB6340DD0F2FB189A4 |
| build/tests/Debug/test_combat.exe | 65291BC43EFC45E68FCEB56A212F402252A0153EEEB3DA26FD54D55491146978 |
| build/tests/Release/test_combat.exe | F3948A7EE7DC5B7AB69F842C869BBFBD64F666C8380D73F6AEBD97D772FDD82E |

This supersedes the prior weapon-owner candidate. Source-location ledgers were
refreshed. The port is incomplete; no old runtime pass is a pass for this image.
