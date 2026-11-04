# Facility placement, daily completion and capacity

Status: bounded raw/source review; no confirmed source defect. No runtime ran.
Detailed [worker report](../../.workers/luna-facility-completion/RESULT.md) and
[raw ARM](../../.workers/luna-facility-completion/ARM.txt) record the checked flow.

The row callback at0x58910 checks funds and arms placement without debiting.
Pointer release stores pending cell coordinates at0x89E8C/0x89E98. Draw commit
checks funds again, reads the selected base at grid+0x619, writes the facility
through BaseFac_SetCellBuildState and debits once at0x59AC8.

Normal time dispatch counts whole86400-second daily steps at0x5CF28..0x5CF70
and calls the base construction path. Raw0x62878..0x62890 decrements1 to0 and
completes the facility on that same callback. Capacity readers and the build/
Base Info views use that same base/grid owner. World refreshes its copied base,
cell, cursor and panel inputs before draw and pointer calls.

Existing World case99.1 uses base0 selected directly, opens through a nav callback,
then places through pointer handlers. Its countdown uses the direct daily hook
and stops at zero; it does not establish a precise day boundary or nonzero-base
selection through the strip. It checks calculator results, not the displayed
capacity after reopening Base Info. The base1 hire fixture prepares completed
quarters and therefore is not construction acceptance.

The open joined case is nonzero-base selection, player placement, one debit,
pending state in only that base, exact daily clock completion and reopened
grid/capacity display. This is an evidence gap, not a confirmed missing callback.
Current assertions remain unrun; native timer/input acceptance is separate.
