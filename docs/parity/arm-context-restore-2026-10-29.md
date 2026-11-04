# ARM register-restore entry0xA42E0

The original1103-entry inventory retains `nullsub_10 @0xA42E0`, four bytes
`FF 9F BD E8`. The instruction is `LDMFD SP!,{R0-R12,PC}`: it reads fourteen
stack words, restores those registers and advances SP by56 bytes. It has no
calls or game/global-memory writes. It is not an ordinary empty C function.

Live IDA reports no references. The preceding instruction at0xA42DC is identical
and returns unconditionally. The original PE contains no literal VA0xA42E0 or
RVA0x942E0, and no pdata entry starts at or covers this address. These are static
facts; absolute indirect unreachability is not claimed.

Disposition: **arm-context-restore**, an ARM ABI adaptation. Native compiler ABI
restoration serves the translated blitter code. This does not assert that the
native compiler recreates an independently callable context-restore routine.
No separate gameplay implementation is shown to be required. The historical
inventory entry remains fully accounted for without calling it an empty stub.

This corrects the former contradiction between image-artifacts.tsv/PARITY.md
and import-bindings-review-2026-10-28.md. The independent final contract audit
accepts this qualified disposition; see
[audit](../../.workers/final-contract-audit-20261029/RESULT.md).
