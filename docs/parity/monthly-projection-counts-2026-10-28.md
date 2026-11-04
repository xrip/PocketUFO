# Monthly forecast uses live roster and site counts

Checked 2026-10-28 by Luna xhigh and parent raw capture. No executable was run.

The original screen19 calls Base_CalcMonthlyUpkeep at0x94350. That path reads
the live craft count at0x6A248, soldier count through0x661EC, and site count
at0x6A340. The port fundingArm cached all three counts during frame wiring.
World frame refresh updated other inputs but left these unchanged, so the
projected balance could use pre-load or pre-roster-change counts.

World_RefreshGeoscapeFrame now publishes craft, soldier and site counts before
each draw. Actual close-time funds calculation already uses live owners and
was not changed. The checked funds/save pointer wiring and region/row mapping
showed no additional mismatch in this bounded review.

Part107.b now hides valid counted prefixes for one real draw, compares the
draw adapter's production upkeep calculation with live-count upkeep, then
restores counts and draws again before closing the report. Parent added
restored-count/upkeep assertions so an old cache initially equal to zero cannot
pass by coincidence. Funds are not changed by the probe. A nonzero forecast
delta is required; no artificial rows or bankruptcy threshold are forced.

The draw helper does not expose its local balance, so this checks adapter
inputs plus the production formula, not rendered text/pixels or the warning
threshold branch. Site count may be zero in the fixture; a nonzero site delta
is not proved. Native acceptance must include a relevant inbound personnel
record and verify the displayed forecast and actual close result. The new
assertions remain unexecuted.

[Raw call and live-count reads](monthly-projection-counts-raw-2026-10-28.json).
[Luna source/owner map and limits](../../.workers/luna-monthly-finance/RESULT.md).

Both full incremental default builds completed cleanly. [Current source/image
identities](fire-finance-ai-build-artifacts-2026-10-28.json). No forecast or close
case was executed. Parent backed up the note at0x94350, checked the new readback
and saved the IDB with backup.
