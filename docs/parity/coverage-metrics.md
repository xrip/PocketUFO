# Parity report metric definitions

`PARITY.md`, `docs/parity/coverage.py`, and `docs/parity/gen_report.py` report source-citation inventory. These counts do not prove raw ARM behavior parity, that a live player flow reaches the function, or that a test assertion checks its effect.

## Source citations

A source citation is an `@ 0xADDR` comment that matches a function address in `docs/idb/inventory-grouped.tsv`. A slash-separated family citation contributes one inventory hit for each listed address. The total citation count includes any cited address that is also classified as a no-game-behavior artifact. Cited bytes likewise sum the sizes of all cited inventory entries, including artifacts.

## No-game-behavior artifacts

`docs/idb/image-artifacts.tsv` supplies the artifact classification from the IDB instruction audit. Classification is independent of source citations. Every artifact address in the function inventory is excluded from the behavior-bearing denominator, whether or not the port cites it.

The report separately shows all classified artifacts and the uncited artifact rows that appear in the remaining-gaps list. A citation to an artifact may count as inventory bookkeeping, but it does not count toward behavior-bearing citation totals.

## Behavior-bearing inventory

The behavior-bearing inventory is the function inventory minus all artifact addresses. Its source-cited count is the intersection of source-cited addresses and that behavior-bearing inventory. This is still citation coverage only; it is not a parity percentage.

## Reach and assertion evidence

The `docs/parity/teeth.tsv` `test_names_it` and `test_calls_directly` fields describe static call-graph or address-mention inventory. A direct call does not prove a test asserts the original function's behavior; a transitive call or address mention does not prove the live path is reachable. Tests report suite-level checks, not a complete function-by-function assertion map.

Raw behavior parity requires separate evidence from original disassembly, the ported path, and checks that assert the relevant state or output. Keep those conclusions in the focused parity notes and runtime audit records rather than deriving them from citation or call counts.
