# Inventory refusal and close connection review

Status: bounded raw/source review; no additional normal-path mismatch found.
No runtime, test, input, decoder or debugger execution occurred.

The [worker report](../../.workers/luna-inventory-cancel/RESULT.md) and
[raw capture](../../.workers/luna-inventory-cancel/raw-pointer-cancel.json)
check the production pointer and close paths after the
[TU owner repair](inventory-tu-gate-2026-10-28.md).

Original Inv_HandlePointer returns constant1 at0x13C60. Pointer callers ignore
R0: calls at0x146B0 and0x1511C are followed by held-byte clears. The port's hook
is void and the App discards World_TactInvDropPointer's internal success bool.
That return adaptation does not change the checked production caller decisions.

Failure switch case0 at0x136A0 finds a floor cell, reinserts the original
TactItem at0x13748 and rebuilds the visible list at0x13750. The source's
Drop_Restore performs that same bounded restoration. Ordinary Escape from
screen1 reaches Input_OnConfirm and Inv_CloseAndCommit through the original
0x163C8/0x16104 path; completed pointer drops already have an owner by close.

An original edge remains: Escape before pointer-up can follow a source-slot
clear without running drop/restore. Neither checked version's close path
restores that held item. Static code permits the event order; actual runtime
input reachability is unverified. This is not evidence for adding a new item
restoration behavior to the port.

Existing pointer fixtures cover successful transfers and direct refusal rules.
They do not prove native drag/Escape sequencing or complete failed-drop/close
acceptance. Keep those runtime checks open on an authorized test host.
