# Current original-function evidence inventory

Updated: 2026-10-28. Overall parity remains incomplete.

## What was checked

Every address and byte size in `docs/idb/inventory-grouped.tsv` was compared
with the live IDA function inventory: **1103 / 1103 match**, no missing/extra
functions and no size mismatches. This establishes inventory agreement, not
correct function boundaries or behavioral parity.

Source citations were collected anew from current C/header lines. There are
**973 cited functions, 130 uncited functions**. All 130 uncited functions have
entries in the existing image-artifact list. That list has 135 entries, five of
which also have citations. The old generated report's 972/131 split is stale.
A citation may also name a field offset; address membership alone does not prove
that the source body implements that function.

The [function ledger](function-evidence-current.tsv) gives each original address,
size, subsystem, navigation name, current citation locations and artifact claim.
Its status distinguishes citation-only rows, scoped raw contracts, and source/raw
checks with runtime still pending. It does not import old blanket completion
claims or replace the narrower evidence in existing notes.
No inference of absence of prior work should be made from that conservative field.

| Subsystem | Original functions | Cited | Artifact claims (may overlap) |
| --- | ---: | ---: | ---: |
| ui-core | 82 | 82 | 0 |
| platform | 131 | 6 | 130 |
| util | 18 | 18 | 0 |
| strat-ui | 250 | 250 | 0 |
| tact-ui | 51 | 51 | 0 |
| tact | 214 | 212 | 2 |
| app-shell | 24 | 24 | 0 |
| titles | 13 | 13 | 0 |
| render | 52 | 52 | 0 |
| strat | 205 | 202 | 3 |
| boot-assets | 4 | 4 | 0 |
| dat | 18 | 18 | 0 |
| save | 35 | 35 | 0 |
| audio | 6 | 6 | 0 |

## Exclusion review from raw instructions

The [raw capture](wrapper-raw-current.json) records bytes and disassembly for all
135 artifact claims from the current IDB. Symbol names are navigation only.
Captured instruction families:

| Existing class claim | Count | Observed instruction shapes |
| --- | ---: | --- |
| import-thunk | 85 | LDR, LDR |
| code-thunk | 27 | B; LDR,B; LDR,MOV,B; LDR,LDR,B |
| init-term-entry | 6 | PUSH,BL,POP,B |
| empty-stub | 9 | eight RET; one POP |
| debug-print-stub | 1 | MOV,PUSH,PUSH,LDM |
| crt-runtime | 7 | multi-instruction bodies; no automatic semantic exclusion |

These are instruction-shape observations, not blanket no-behavior findings.
For example, raw 0x18A44 loads the Map argument and sets R1 to 1 before branching;
0x46728 loads and dereferences a surface pointer before branching. Constructor
wrappers call constructors and registration routines. A correct port must
preserve the argument/caller/lifecycle effect or explain the platform adaptation.
They cannot be discarded merely because they are small or named as thunks.
Even the 85 two-LDR entries still need their target and replacement API contracts
accounted for; shape alone does not prove import identity.

## Remaining parity work

The [header-only body map](header-citation-followthrough-2026-10-28.md) supplements
223 rows with actual C definitions, shared/renamed helper mappings and production
adapter locations. It establishes source presence, not raw behavioral equivalence.

1. Carry forward existing scoped raw findings with exact function/range and
   source identity; distinguish a checked branch or field from a checked body.
2. Resolve the 135 artifact claims: bytes/operands, targets, and port lifecycle
   or API replacement evidence. Start with seven CRT bodies, the debug stub,
   and argument-setting/constructor wrappers; these are not generic no-op cases.
3. For behavior functions, connect caller and callback instances as well as
   source bodies. Prior terrain/deployment defects show why citations alone fail.
4. Link current runtime checks only after the fixed candidate runs on an
   authorized host. Do not import old suite passes as current evidence.

No report generator or game/test executable was run. The generated PARITY.md
was left untouched because its generator invokes tests. These new artifacts
change only documentation; they do not replace or modify the compiled candidate.
