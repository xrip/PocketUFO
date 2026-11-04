# Stored loadout extent

The original stores 25 index/byte pairs. Its ordinary inventory commit loop
visits only 24 entries. These bounds must remain separate.

| Soldier offset | Raw saved width |
|---|---:|
| `+0x3C1` | 1 separate flag byte |
| `+0x3C2` | 50 bytes: 25 halfwords |
| `+0x3F4` | 25 bytes |

In `Save_WriteSoldiers`, `0x429A8 E3A03D0F` and `0x429B0 E3833002`
construct offset `0x3C2`. `0x429B4 E3A02032` supplies length 50 and
`0x429BC` calls the writer. `0x429C4 E3A02019` supplies length 25;
`0x429C8 E2841FFD` forms `Soldier+0x3F4`, followed by the write at
`0x429CC`. The reader mirrors these lengths at `0x41D3C` and `0x41D4C`.

The re-equip loop starts with `0x3C140 E3A09019` (count 25), reads a
halfword at `+0x3C2`, decrements the count at `0x3C21C`, and advances the
record pointer by two at `0x3C220`. `0x3C22C` repeats while the count is
positive. Thus it reads entries 0 through 24.

The ordinary commit loop starts at zero, increments at `0x15C44`, compares
against 24 (`0x15C48 E3510018`), and repeats only while less than 24
(`0x15C4C BAFFFF97`). The switch contains a case for index 24, but this
loop does not reach it. This is observed original behavior; its intent is
unknown. The separate flag byte is not an extra array entry.

The port's stored arrays and save codec already have 25 entries. The new
return observer had reused `PUFO_INV_COMMIT_SLOTS` (24), so its first trace
omitted the final stored pair. The observer is corrected to cover all 25;
the original 24-slot commit behavior is unchanged. The corrected observer's
natural run records all 25 pairs for each of eight crew members before and
after debrief shutdown. The strict saved-log checker passes their identities
and all pairs: [batch12/13 evidence](../../.workers/luna-runtime-corrected-port/RESULT.md).
This proves persisted debrief exit; the craft was still away from home.

The raw read-back notes were added to the IDB and saved. The full audit is in
[STORED-LOADOUT-COUNT.md](../../.workers/luna-spawn-floor-audit/STORED-LOADOUT-COUNT.md).
