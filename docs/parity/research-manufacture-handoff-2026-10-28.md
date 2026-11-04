# Research unlock and exact manufacture product

Checked 2026-10-28 by Luna xhigh. No production-source mismatch was confirmed
in this bounded path. No game, test, debugger or decoder was executed.

Raw hourly order calls manufacturing refresh at0x5D304, then research accrual
at0x5D30C. Completion sets the topic's unlocked byte at0x7440C. Manufacture
row construction gates on that byte at0x6EA30..0x6EA38, puts the item ID in
Panel A at0x6EF64 and topic ID in Panel C at0x6EF70. The hourly refresh tail
commits production at0x71048. The World supplies live owners to these hooks.
This establishes the checked handoffs, not runtime or full-body equivalence.

The unusual capacity call at0x6F504..0x6F50C really passes the site count as
the helper's requested site index. It is present in the original ARM; this
review does not change it into a more intuitive index.

## Fixture fault and correction

The section99 test first chose a product named by its completed topic, then
replaced it with the first offered row. It could therefore manufacture an
unrelated product and still report success for the research-to-production step.

The corrected fixture finds both that product in Panel A and that topic in
Panel C, records failure if the row is absent, and clicks the same matched row.
Later allocation, recipe, price and stock assertions keep the same product ID.
It no longer substitutes another offered product.

Topic selection uses loaded slot61 state. No fixed topic29/item20 outcome is
proved here. The existing picker does not check the manufacture-row flag; an
unsupported candidate now causes the exact-row assertion to fail visibly.
This pass did not change the picker or force an unlock to make the test pass.

The fixture calls the World hourly hook directly and stages facilities, staff
and resources. It does not prove app timer dispatch or normal elapsed-time
advancement. Native acceptance must complete research through the real clock,
offer that topic's exact product, and commit its stock/components/funds after
hourly time passes. The revised assertions remain unexecuted.

[Raw instruction capture](research-manufacture-handoff-raw-2026-10-28.json).
[Luna's source map, review and input limits](../../.workers/luna-research-manufacture/RESULT.md).

Both full incremental default builds passed with68 executable outputs and zero
compiler diagnostics. [Current source and image identities](return-research-build-artifacts-2026-10-28.json).
The parent preserved the prior IDB note at0x6EF70, recorded the supported row-ID
finding, checked readback and saved with backup. The old note is in
[this backup](research-topic-row-idb-2026-10-28.json).
