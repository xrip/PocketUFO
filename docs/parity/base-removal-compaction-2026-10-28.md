# Base removal: false manufacturing-shift quirk corrected

Updated: 2026-10-28. Source repaired; all default targets compile.
No game, test or debugger executable was launched. Runtime acceptance pending.

## What was wrong

The port deliberately left the removed base's manufacturing assignment row
unchanged and shifted later rows too far. Its test asserted that behavior, and
bugs.tsv/IDB notes classified it as an original bug. The raw register trace
disproves that classification. This was a port index/bound error, not a QoL
change to the original.

## Raw address derivation

[Full raw function/literal capture](base-removal-compaction-raw-current.json)
preserves the369 code items, including complete ARM macro byte spans.

1. The facility search index is copied to R11 at0x66BD0.
2. 0x66E50 sets source index s=removed+1 in R4; outer iteration uses s<count.
3. 0x66E7C loads literal0x159E98 from0x671A8.
   0x66E84 constructs R10=0x159E98+0x190*s.
4. 0x67070 reads a u16 at R10+0x4304;0x67078 stores it at R10+0x4174.
5. The engineer getter0x681F4 independently loads the same0x159E98 base
   via literal0x68258 and reads the manufacturing table at+0x4304.

Therefore M=0x159E98+0x4304=0x15E19C, source=M+400*s,
destination=M+400*(s-1). The offset difference is400 bytes, one200-word row.
R10 has no intervening body assignment before the copy; it advances by400
for the next outer source row. The original moves source row into its predecessor.

The research move is separate:288 bytes/144 words per base. Four product-block
moves use200 entries per base, with the appropriate16/32-bit width. The generic
header comment was narrowed accordingly. No assumption that every column has
the same research/manufacturing stride was used.

## Code and test correction

ShiftColumnU16's destination range is exclusive at its end. The manufacturing
call now uses [removed,count-1), taking each following row. Previously it used
[removed+1,count), both skipping the needed destination and reading beyond the
active base count.

For the existing three-base fixture removing base1:

| Storage row | Before | Required after |
| ---: | ---: | ---: |
| 0, prefix | 77 | 77 |
| 1, removed destination | 111 | 222 |
| 2, discarded last active storage | 222 | 222 |
| 3, out-of-count sentinel | 333 | 333 |

The test now requires those values. The earlier expectation of111 at row1 and
333 at row2 encoded the false quirk; it was corrected using the raw derivation,
not because of a failing runtime test. The updated case has not been executed.

World's demolition context binds live manufacturing, research, committed,
requested and accumulator arrays. Tactical shutdown outcome reaches this
demolition context on its base-loss paths. This verifies these source
connections, not every branch of base defeat or demolition.

## Evidence/ledger maintenance

Removed the false0x67048 bug data row from bugs.tsv and retained a correction
notice before its header. The ledger parses with no malformed rows.
Protected historical game-core.md passages claiming this original bug are
superseded by this report; that file was left intact. PARITY.md was not regenerated
because its generator runs test executables.

The misleading629-character IDB comment at0x67048 was preserved
[before](../../.workers/luna-mission-init-audit/base-compaction-idb-comment-before.json)
and replaced by the checked register/offset derivation
[after](../../.workers/luna-mission-init-audit/base-compaction-idb-comment-after.json).
Read-back matched; IDA save with DBFL_BAK returned true. No code/type/name edit
was made in the IDB. Comments remain annotations, not the primary evidence.

## Current build/candidate

Both full default commands exited0 with zero diagnostics,68 executables each:

```
cmake --build build --config Debug --parallel 2
cmake --build build --config Release --parallel 2
```

[Debug log](../../.workers/luna-mission-init-audit/debug-base-compaction-full-compile.log),
[Release log](../../.workers/luna-mission-init-audit/release-base-compaction-full-compile.log).
The [136-image manifest](base-removal-compaction-build-artifacts-2026-10-28.json)
records current outputs. These are incremental builds, not new clean builds.

434-file source fingerprint:
**F2CDD819A70CB152B6BB1ADA21D2CF15324EB949C289CAD4916EFCBA30ABAB12**.

| Artifact | SHA-256 |
| --- | --- |
| src/pufo/game/pufo_geoscape.c | 3DE3084909EEEC0DACCA898C34359DCDD00AB1A2BB422BC935ABB7BA0FD5F709 |
| src/pufo/game/pufo_geoscape.h | EC4F721C6104D2668232B3850EBE1C531EE1AE3EEEAB4B272EF9809AE6CDE126 |
| tests/test_geoscape.c | 75DE4BE47914447F452CBC3A37FB8B770BE66AF6A2C3D7E935A1D20126E93974 |
| build/tests/Debug/test_geoscape.exe | 93049576488614F72B1EA7112E793A2AF8928A16307F429568C006321B314D42 |
| build/tests/Release/test_geoscape.exe | BE40C844D274799F9901F23F59C727F8CB5EC47B442C11E39D4AB1247ED5743A |
| build/src/pufo/Debug/pocketufo.exe | A298B08C493BC4CF339A7694F5EC454094EA23BDEE60E8B2DC34F918136962F6 |
| build/src/pufo/Release/pocketufo.exe | 2DE363B45CFF82F82EE091C69A43FFE9B7B7A77930F4A411D6A8E42F84D8C4A5 |

This supersedes the staff-row candidate. The revised compaction case, full
suites, actual first/middle/last base-loss flow, surviving staff/workshop
assignments and save/load continuity still need the authorized test host.
Whole-function/base-defeat parity and the full-port goal remain incomplete.
