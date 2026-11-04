# Strategic cold-load comparison rules

Compare serialized game state, with explicit checks for load-time UI resets.
Raw instructions and byte widths are the proof; comments are navigation only.

For a non-tactical save, SaveGame tests tactFlag at `0x44A54..0x44A60`
and skips MCNT/TGameData/RMP/Map. The mission byte at Map `+0x1511A`
belongs to TGameData. Startup chooses a new inactive mission type, so the
completed mission's selector is not a strategic-save field. Pending region
`0x159DF4` and globe sweeps `0x159DF8/0x159DFC` are UI-owned globals;
the explicit Smap camera/site stream does not store them.
[Raw branch and ownership audit](../../.workers/luna-cold-roster-raw-audit/RESULT.md).

The private strategy comparator omits only pending/sweepV/sweepH/mission when
both checkpoints are tact0 and the cold checkpoint proves a clear ready
geoscape. All other state, time, pause, funds, base, crew, health, cargo, site
and route assertions remain. Tactical checkpoints do not get this exception.

The simulation interval is also not serialized. Save_WriteTime `0x40204` and
Save_ReadTime `0x41600` use the same fixed globals and widths; neither includes
interval `0x187070`. LoadGame calls Geoscape_InitUI at `0x44CBC` before the time
reader at `0x44DA0`. GeoInit calls the five-second setter at `0x86AB0`.
That setter uses literal `0x187070`, `MOV R1,#5` at `0x84F14`
(`05 10 A0 E3`) and `STR R1,[R0]` at `0x84F1C` (`00 10 80 E5`).
[Fresh-load review](../../.workers/luna-integration-review-current/HOME-COLD-TICK-REVIEW.md).

Thus slot1 saved after a selected one-hour pulse has tickSec3600, while its
fresh menu load correctly has tickSec5. Clock/last ticks remain4381, date and
pause remain exact. Explicit fresh slot1/slot3 checks require loaded head and
strategy tuple both5, and omit only this interval from equality. Same-process
selected-speed3600, actual input, elapsed-clock and flight checks remain strict.
Cached positives and clock/pause/bad-reset negatives pass; this is not a
production codec repair.

The retained phase1 input save is
`408A891EA3CFA75B2E4A615CA06177A95C46BEA23B2A31B79B6B7C0BDD5A29F5`.
Its fixture was already home and advanced one real hour, site8 to7. Later work
must resume that checkpoint; it must not claim a flight or replay the hour.
