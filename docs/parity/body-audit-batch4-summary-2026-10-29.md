# Fourth body-audit batch

Twenty-four additional original bodies are indexed, bringing the addressed total
to 113 of 1,103. This is scoped static evidence, not a game completion percentage.

- Six BASES/PRODUCT/RESEARCH bodies: a real old-save live-state join mismatch was
  repaired. The [repair review](base-load-join-review-2026-10-29.md) explains the
  raw gates, connected regression and compile-only evidence.
- Seven UiBank/radio bodies: construction, reverse cleanup spans, callbacks and
  sound ordering match within the [reviewed scope](../../.workers/luna-body-uibank/RESULT.md).
  The original start-menu cleanup leaves stale pad pointers before a registered
  exit sweep. The port's pointer clearing is retained as a safe adaptation.
  Two false IDA comments were backed up, corrected, read back and saved.
- Eleven map-cell/unit bodies: no new source defect was confirmed for valid,
  populated cells and valid links. The [report](../../.workers/luna-body-mapcell/RESULT.md)
  records flat footprint wrapping, head-only item insertion, variable raw removal
  returns and allocator-state limits. Shipped-map wrapped-slot population remains
  open; controlled fixture population does not prove it.

The earlier blit descriptor boundary is closed by the no-argument GXEndDraw
import. Its new platform result is a separate unresolved callback-result path.

Both current configurations compile cleanly. No assertions or native game flows
ran. See the [index](body-audit-index-2026-10-29.json) for report hashes, domains,
adaptations and open boundaries.
