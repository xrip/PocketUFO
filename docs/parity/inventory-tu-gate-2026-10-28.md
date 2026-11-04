# Initial-inventory TU gate

Status: source repair reviewed; both full builds passed. No assertions ran.

## Confirmed mismatch and repair

Original inventory drop charging reads Map+1, the byte set during mission setup
and retained through the first inventory. The port instead read the separate
Map+2 action latch, so initial loadout transfers could spend TU and a nonzero
action latch could waive ordinary costs.

`InvDropCtx.pbInvNoTuCharge` now reads the live
`PufoWorldState.bMapUnk1139B9` owner. World binds that pointer before mission
auto-equip and reuses it for live drops. Clearing the flag on first commit is
therefore visible without a copied/stale flag. Action-latch bindings are unchanged.

The [raw capture](inventory-tu-gate-raw-2026-10-28.json) records setup, ground-drop
and commit instructions. The [full worker evidence](../../.workers/luna-inventory-input/raw-tu-gate.json)
records all12 Map+1 gate sites in `Inv_HandlePointer @ 0x12260`:
0x123AC,0x124F0,0x12628,0x12760,0x12950,0x12AF0,0x12CC8,0x12E48,
0x12F78,0x13188,0x133A8 and0x13558.

Setup writes1 at0x3C674, clears Map+2 separately at0x3C6B8 and calls auto-equip
at0x3CCBC. The team path opens inventory at0x3D0B8 while Map+1 remains set;
the no-team path clears it at0x3D0D4. First commit clears Map+1 at0x15C68.

Normal drop and clip gates waive their cost checks/debits under Map+1. Split
keeps a different original order: it requires at least6 TU before reading the
flag, then waives only the debit. The repair preserves that unconditional check.

## Why these fixtures exist

- `test_invdrag.c` distinguishes opposite Map+1/Map+2 states, normal and clip
  drops, and the split's required TU versus its waived debit. Its setup-equip
  fixture supplies the real free-mode condition instead of using the action latch.
- The World test performs hand-to-ground and same-item-to-same-hand transfers
  before the first commit, through live pointer handlers and rebuilt list lookup.
  It does not stage either flag. It checks TU, item identity/ownership, burden,
  cell/list/pool counts and preserved mode bytes.
- Existing Part161 paid transfers occur after the first commit. Their5/6-TU
  expectations remain valid and now have explicit mode preconditions. They were
  missing initial-mode coverage, not asserting the wrong original cost.

These source assertions are unrun. The World route does not establish native
WndProc input delivery, visible frame behavior or a complete campaign flow.

## Verification and IDB

Parent Debug builds of pocketufo, test_invdrag and test_geoscapeframe returned
exit0. Logs are `.workers/luna-mission-init-audit/debug-inventory-purchase-`
`targets-compile.log` and `debug-inventory-purchase-world-compile.log`.
The [worker report](../../.workers/luna-inventory-input/RESULT.md) lists stable edits.

Subsequent full Debug/Release builds returned exit0 with68 executable outputs
and zero diagnostics each. The [manifest](inventory-purchase-build-artifacts-2026-10-28.json)
records matching source fingerprints before/after the build interval and image
hashes. It includes the [joined hire fixture](purchase-clock-fixture-2026-10-28.md).
No test or native flow was executed.

Three IDB notes were guarded against the
[backup](inventory-tu-idb-before-2026-10-28.json), corrected, read back and saved
with DBFL_BAK. The [readback](inventory-tu-idb-after-2026-10-28.json) records them.
No original instruction or type changed.
