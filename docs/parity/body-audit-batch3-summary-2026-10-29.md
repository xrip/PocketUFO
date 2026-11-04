# Third raw-body audit batch

The [live index](body-audit-index-2026-10-29.json) contains89 addressed members.
This batch adds controls7, blit-state6, strategic/time save records5 and unit
basics4. Raw coverage is recorded separately from domains, adaptations and
unresolved boundaries. No game or test was executed.

The control batch found the [App transparency-key repair](control-pad-key-review-2026-10-29.md).
The body’s fixed key0 had been lost in the live callback, which used255. Existing
control tests inject fake blitters and App input tests do not install the private
render callback, so they could not catch that binding defect. The current full
builds compile the fix; native pixel acceptance remains open.

Save/Strateg/Time source and raw field order match in the checked valid formats.
Strateg has38 header fields/134 bytes and33 current item fields/113 bytes; its
old item tail is105 bytes. Time has8 wire fields totaling27 bytes; the32-byte C
record includes host padding. Pointer-like economy fields serialize live cell
values, and World staging/joins bind them before reads. The version is the saved
byte read before chunk decoding; older source “build version” comments are stale.
Original-save interoperability and current generated-save assertions remain unrun.

Unit reset callers pass their own type and ten stats, the24-row size table
matches, and facing uses two angle calculations followed by RNG limits
100,100,20,20. Exact Win64 math identity at angle thresholds is unverified.

Blit-state fields publish the checked descriptors/targets. Flip masking uses a
direct native pointer in the port instead of the original32-bit global publication;
the checked data flow is an adaptation, not a lost mask. The preserved return
through the external GXEndDraw boundary remains a separate follow-up, not a
claimed mismatch from a register merely being unchanged.

The [compiled candidate](control-key-build-artifacts-2026-10-29.json) passes
both full incremental configurations with zero diagnostics. The remaining
per-function inventory and joined native/campaign acceptance are still open.
