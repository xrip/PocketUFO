# Loot-return indexing: two false quirks corrected

Updated: 2026-10-28. Two source repairs. The incoming-capacity question below
was subsequently [resolved as zero in normal flows](incoming-capacity-gate-2026-10-28.md).
No game, test or debugger executable was launched.

## Hangar marker destination

Craft type is read at Ship+2 at0x68480 to choose one of five marker values33..37.
Every branch then reloads the craft and byte Ship+3 before its base-stride multiply:
0x684D4,0x684FC,0x68524,0x6854C,0x68574.
The eligibility filter at0x683BC uses this same+3 byte against the outer base index.

The port instead used bCraftType as the destination block, reproducing a falsely
documented original bug. It now uses the home-base value; type still selects
the marker value. The old baseline test expectation was corrected accordingly.

## Stores update base index

Original R8 is overwritten by0x689E0 loading stack offset0x10 before sign extension
at0x689E8/0x689F0 and R1 setup at0x689F4. That stack word is initialized to0 at
0x6837C and incremented/stored at0x68F18: it is the outer base counter.
The older claim that R8 still held twice the craft index ignored the reload.
0x689E4 itself stores cargo quantity; it is not the sign-extension instruction.

The port now passes the processed nBase to both the stores-needed update and its
used-storage calculation. The old2*ship-index expression is removed.

[Raw evidence](loot-return-indexing-raw-current.json) preserves the full loot
body, stores-needed body and incoming-volume body. No comment or existing function
name was used as proof of these register assignments.

## Incoming capacity: initial finding, now resolved by the follow-up

Raw Base_UpdateStoresNeeded at0x65D44 sums current storage and the result of
0x67D68 with requested site index-1 and site scanning enabled.
The previously claimed unported helper now exists, but its compatibility is
not solved just by calling it.

Raw0x67D68 gates each other site's item contribution by the requested site's
quantity. With requested index-1, this reads before the site array:
site base0x162030, stride0x2E6, quantity address
0x162030 -0x2E6 +2 +2*objectIndex.
The port's explicit valid-site-index guard prevents that read and returns0
for this scan shape. The loot adapter therefore still uses current storage only.
Its stale “only the first helper is ported” comment was replaced with this actual
remaining deviation. No ineffective site inputs were added to the context.

This pass left the alias unresolved. The linked follow-up recovers it as untouched
zero-filled carry-count words 159..229, closing this specific gap without changing
the helper or adding context fields. Native gameplay remains unverified.

## Tests and ledger

The existing marker case now expects the home block to receive the marker and
the type-numbered block to remain unchanged. Added TestLootBaseSelection:
craft index0, type1, home base2,301 current cargo units. It requires the home
marker and two stores-needed flags at base2 while base0/type1 blocks stay unchanged.
This checks independent indices and current used storage, not incoming cargo.
Neither case was run in this pass.

Removed false0x6858C/0x689E4 original-bug data rows from bugs.tsv.
The ledger parses without malformed rows. Historical game-core.md statements
supporting those claims are superseded by this raw trace; that protected file
was not edited.

The prior554/476-character IDB notes were preserved
[before](../../.workers/luna-mission-init-audit/loot-index-idb-comments-before.json)
and replaced by raw register traces
[after](../../.workers/luna-mission-init-audit/loot-index-idb-comments-after.json).
Read-back matched and IDA save with DBFL_BAK returned true.
No code/type/name edit was made in the IDB.

## Current compiled candidate

Both full default builds exited0, zero diagnostics,68 executable targets each:

```
cmake --build build --config Debug --parallel 2
cmake --build build --config Release --parallel 2
```

[Debug log](../../.workers/luna-mission-init-audit/debug-loot-index-full-compile.log),
[Release log](../../.workers/luna-mission-init-audit/release-loot-index-full-compile.log).
[136-image manifest](loot-return-indexing-build-artifacts-2026-10-28.json).
These are incremental builds; no test pass is inferred.

434-file source fingerprint:
**B4F55899D7782F604B9B820E7E228725E9C0D283461EAA3DBF8645D87A07F79C**.

| Artifact | SHA-256 |
| --- | --- |
| src/pufo/game/pufo_ship.c | 4A105683D59903DC2C4CE6D75388624F0037D74272F443E14E24C5D91D91AD5D |
| src/pufo/game/pufo_ship.h | 6058FD3A2088D54325DD829971616ACF1ACAFD77822147DF4C12BFB8CEA4C749 |
| tests/test_ship.c | 9836EFFAC4583ECD700D0661EE35878DFEED9ACA712B04BBFCA464A52DE8B67A |
| build/tests/Debug/test_ship.exe | 08C58695A6CCC555AAF9870DAFCE82F390991571B25334D91967FB7A12B753E2 |
| build/tests/Release/test_ship.exe | C770F76513C5E84DBA45DC47572DBC6FFA0236A992A5E3B4C89C2B2725C38329 |
| build/src/pufo/Debug/pocketufo.exe | 8A834244843FF1599B743530D19D4ABAC492CD0EA567846C72E5D431C263105B |
| build/src/pufo/Release/pocketufo.exe | AC620003D57E19CB71F8E075679E5BD5A6D644DF023C870E1734C011EC17881F |

This supersedes the base-compaction candidate. Source/body/import locations were
refreshed. On an authorized host require the revised cases, a real craft return
to a home base different from craft type/index, rearming/store flags, full suites,
and save/load continuity. Full loot/capacity/port parity remains incomplete.
