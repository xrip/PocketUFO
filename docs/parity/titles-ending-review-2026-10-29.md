# Credits exit and campaign continuation

Credits could open but not close: App_OnPointerUp returned early after Titles_Init
cleared the startup-UI flag. Raw0x14AE8..0x14AF8 handles screen16 before the
tactical-active gate, calling Titles_Shutdown then startup. The App now lets
screen16 reach that existing release hook.

The unchanged [credits flow](../../.workers/port-goal/credits.flow.txt) remains
on screen16 in the [old run](../../.workers/port-goal/credits-c78a13bd9ac74a1693f8819c6600d5eb/credits.log).
The [repaired run](../../.workers/port-goal/credits-1cd3076e3d914b2a98c0f34b22b72781/credits.log)
returns to screen17, then starts New Game and places a base with8 soldiers and
funds4147000. Four240x320 pictures have77..432 colors. Execution used a private
desktop; the input desktop stayed Default.

## Original code comparison

[All13 captured bodies](titles-ending-raw-2026-10-29.json), totaling4444 bytes,
match the original PE bytes exactly. [The structured function map](titles-ending-review-2026-10-29.json)
records each original address, same-named C binding, effects, domain, adaptations,
source hashes and checks. It covers the complete credits/end-screen group:
DrawAnimatedFrame, DrawBackdrop, Init/Shutdown, EndScreen_Init/Shutdown,
PresentToSoftSurf, DrawCreditLines, LoadScene, AnimateParticles, UpdateStars and
ctor/dtor. The44/10/16 row tables, five glyph names and sound prefix also match
the [original data](titles-data-2026-10-29.json).

The scene loader now returns2 on the1->0 stored-index wrap, as ARM R0 does, and
stores scales for a two-byte zero-particle member. The wrap assertion
[fails on old code](../../.workers/port-goal/titles-before-fix.log) and passes
after repair. Header-only stores have static proof at0x46984/0x4698C; no custom
two-byte archive fixture was run. Jitter calls now explicitly use Y then X.
The prior Clang image already used this order; an exact blurred-image assertion
pins it without claiming an earlier pixel defect.

The animation argument is renderer context0x10DA40, with its scratch at+0x100,
separate from g_pTitlesState. The phantom second argument and misleading name
were corrected in the IDB. Header/wrap, input-release and681-reload facts were
recorded, re-decompiled, read back and saved. The main edits have
[before](titles-ending-idb-before-2026-10-29.json) and
[after](titles-ending-idb-after-2026-10-29.json) records.

Explicit adaptations include native state ownership/pointer clearing, safe
missing/short-resource handling, local scratch counters, palette slots instead
of ARM pointers, and unused numeric return registers. No allocator-residue,
malformed oversized-data, allocation-failure or cross-platform libm bit identity
is claimed. Counts are evidence records, not completion percentages.

## Further playable evidence

- [Home return](../../.workers/port-goal/home-return.log): actual flight home,
  landing transfer, minute recovery, one hourly pulse and save/load pass on the
  preceding tactical-fix Debug image.
- [Saved campaign](../../.workers/port-goal/second-mission.log):5 survivors,
  12 loadout rows, funds, base position and craft cargo persist. The
  [second process](../../.workers/port-goal/campaign-c415e6df6e5740aabb787b49e697cffb/e2e_flow_campaign_load.txt.log)
  keeps post-load assignments through navigation/contact and deploys the one
  available craft soldier against2 enemies. Four survivors are marked254; the
  observer does not force them aboard. The183759-byte save was reused without
  replaying the first mission.
- The old campaign checker demanded a return notice even while the craft was
  away. It now accepts the explicit NONE-pending state with empty status/modal
  queues as well as completed acknowledgements. No game status rule changed.
- [Audio](../../.workers/port-goal/audio-output.log): BACK0.IT produced nonzero
  PCM in a completed waveOut buffer; the sample reached its final mixed buffer.
  This proves device-buffer completion, not speaker or FMODCE waveform identity.

Both full builds exit0 with zero compiler diagnostics. Affected
titles/input/AppShell suites pass3/3 in both:
[Debug](../../.workers/port-goal/credits-Debug-tests.log),
[Release](../../.workers/port-goal/credits-Release-tests.log).
The required master-report regeneration then ran all67 Release suites on this
build: [generator result](../../.workers/port-goal/parity-report.log),
[master report](../../PARITY.md). All pass. Debug has the current3/3 affected
checks and the preceding D615 full67/67 gate; unrelated Debug suites were not
repeated for this bounded credits/UI change.

Source fingerprint: `DA8B77783701CC930D08C4A0E175FD563F577E648D987B84088A59D43A02DC29`.
Release EXE: `84C5C9DB8BD3BA75532BF3569D326E5564DBE10BB7B689FD1C9C144BC285C804`.
The body index now has236 unique records. Other subsystem mappings and the full
completion contract remain open.
