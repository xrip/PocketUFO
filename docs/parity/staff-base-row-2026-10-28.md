# Selected-base staff availability: missing row offsets

Updated: 2026-10-28. Source repaired and all default targets compiled.
Runtime checks pending. No game, test or debugger executable was launched.

## Raw contract

[Four raw bodies](staff-base-row-raw-current.json) establish:
- 0x66198 reads object slot113 (Map pointer field+0x2CC), and0x661B8 reads
  slot112 (+0x2C8). Both sign-extend the base byte and read u16 stock at
  object+0x2A+2*base.
- 0x6812C computes scientist availability as that stock minus the selected
  research row. Raw stride0x120 bytes means144 u16 entries per base.
- 0x681F4 does the corresponding engineer subtraction with manufacturing
  stride0x190 bytes, or200 u16 entries per base.
- The allocation entries are unsigned16-bit values; the result may be negative.
  These contracts were checked for valid base indices0..7. The port's stock
  guard outside that range is an existing documented adaptation.

The port free-staff helpers take an already selected allocation row; their
base argument selects the object's stock cell. They do not offset the supplied
row themselves. Research/manufacturing allocation, draw and Base Info callers
already used selected rows correctly.

## Confirmed wrong callers and repair

World supplies full manufacturing/research tables to the Buy/Sell row editor,
Buy/Sell panel builder and Transfer panel builder. Six calls from those contexts
passed the full table directly into row-only free-staff helpers. For base1+
they combined selected-base stock with base0 assignments.

This can hide free staff, expose assigned staff for sale/transfer, or report
the wrong availability when two bases have different allocations. Base0 cases
cannot reveal it because the missing offset is zero there.

A small private StaffColumnForBase selects the proper row, with null/index
guards, at those six calls. The already-correct row callers were retained, so
they do not get a second offset. Header comments now distinguish full-table
context fields from the row-only helper parameters.

The App's selected-base status output had the same mismatch; its two reads
now pre-offset the tables. Base0-only diagnostic probes remain unchanged.
No payroll rate or staff allocation algorithm was changed.

## Regression purpose

Existing Buy/Sell and Transfer panel fixtures now also use two bases with
different allocations. Base1 stock is3 engineers and4 scientists:
- base0 is fully assigned, while base1 assigns1 engineer and2 scientists:
  both rows must appear with available count2;
- base0 is free, while base1 is fully assigned:
  both staff rows must be absent.

The row-editor fixture covers both staff types. A held click requests three
units but may queue only the two free base1 staff; its available count reaches0.
With base1 fully assigned and base0 free, the editor must queue none.

These cases discriminate base selection, the two different strides, displayed
free counts, and the live sale limit. They were compiled, not executed. They do
not prove native second-base UI input, final stock commit, transfer arrival or
cold-load persistence. The existing base0 cases remain.

## Build and candidate

Affected targets first compiled in both configurations. Then all default
targets were updated:

```
cmake --build build --config Debug --parallel 2
cmake --build build --config Release --parallel 2
```

Both exited0, zero diagnostics,68 executable targets each (game+67 tests).
This was a full incremental build after the source change, not a fresh clean
build. [Debug log](../../.workers/luna-mission-init-audit/debug-staff-base-row-full-compile.log),
[Release log](../../.workers/luna-mission-init-audit/release-staff-base-row-full-compile.log).
The [artifact manifest](staff-base-row-build-artifacts-2026-10-28.json) records
all136 executable identities. No test pass is inferred.

434-file source fingerprint:
**44CFA251A3D68C7E0899AC9DDEB47ED2D4F5A8B80CB2C7C2822EA2AEFE4AB28A**.

| Artifact | SHA-256 |
| --- | --- |
| src/pufo/game/pufo_baseui.c | 92192DD8D58989FE9B4F3635688D318EAA0C61D42A422D604E9A03ED1FA74496 |
| src/pufo/game/pufo_baseui.h | 49D00D9886D842F34C66E8C6FE8D3C4F9BD7042FD84043B4C5DCED5A19DE734B |
| src/pufo/game/pufo_geoscape.h | 0B15D4A0C3C4F7F9B4AA408DA9A39092843607337A491E65657C7AD3178622B5 |
| src/pufo/app/pufo_app.c | 7673EB359FEA7CA040FBB65A4C48FDA2DAB30A9B2A55E90BAEECD18F8BD4073D |
| tests/test_baseui.c | 6187A2B8407FE9593A2699636FB25067C9981FFE77B911D791D11DC250958649 |
| build/tests/Debug/test_baseui.exe | A8D448A14CF38343CF1EB38A68134468CFA70D81FB9E8974230A1F7F9D09CC6E |
| build/tests/Release/test_baseui.exe | 0A712C18660152E4BFD813FED20BA00469EFCE9E9D73003EB00303DF20C67B7F |
| build/src/pufo/Debug/pocketufo.exe | DCC6470377089A5A200E3217F517B3AEA47CD94E95DD40BC592074A381CC967F |
| build/src/pufo/Release/pocketufo.exe | 61949228F01DC4FA898E1144C9C05DF7A91008C294AFDE9531227907EA3C8846 |

The previous clean-build checkpoint is historical for its own source. Current
source/body/import ledgers were refreshed. No IDB name/type change was needed
for this caller-binding repair; original instruction evidence is preserved.

On an authorized test host: run the new cases and full suites, then use two
actual bases with different research/workshop assignments, sell/transfer staff,
check the chosen base's stock and assignments, confirm, save/load, and recheck.
The full-port goal remains incomplete.
