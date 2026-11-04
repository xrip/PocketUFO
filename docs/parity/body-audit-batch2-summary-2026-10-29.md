# Second raw-body audit batch

This batch added27 members to the first40: base-key helpers5, archive readers6, surfaces10 and
panel text/scroll6. Raw coverage, valid-input equivalence, explicit adaptations
and unresolved boundaries remain separate. This is not a whole-port pass.
The [live index](body-audit-index-2026-10-29.json) also includes later batches.

The base-key batch found the [repaired signed-search contract](base-search-contract-review-2026-10-29.md).
The other three found no new valid-input production defect in their checked
domains. Archive lookup/read failure and stale-field behavior differ explicitly
from the original; AssetReader has no pointers and retains its32-byte offsets
on Win64. Surface allocation's invalid-mode case reuses prior bytesPP in raw
code while the port chooses0; observed allocation callers use modes0/2.
Surface destruction clears pointers in the port. The live Win64 DIB backend
remains a separate adaptation and runtime check.

Panel hold state and callbacks were checked through actual call paths. Two
intermediate switch-label claims were false: branch0x13CE8 targets0x148C8,
and0x882A0 targets0x89444. Both are epilogues, not the dispatch addresses in
their generated label suffixes. Native callers overwrite the propagated R0.
The [decoded target record](input-return-branch-check-2026-10-29.json) supplies
the correction; no missing dispatcher handoff was inferred from those labels.

Three owner parameters were refined to SoftSurf* or UiPanel*, then decompiled
and saved with backup/readback. The setter and WrapText return models were
retained: [before](surface-scroll-types-before-2026-10-29.json),
[after](surface-scroll-types-after-2026-10-29.json).
The first guard attempt made no change; an isolated IDA Python namespace removed
a stale-generator-variable comparison before the successful application.

The [compiled candidate](base-search-build-artifacts-2026-10-29.json) includes
the signed lookup repair, live-hook/direct regressions and corrected layout
comments. Both full incremental builds pass with zero diagnostics. Assertions
remain unrun; native input, device output and campaign acceptance remain open.
