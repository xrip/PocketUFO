# Battle reset inventory: stale gap claim checked

Status: bounded raw/source/data review; no new source fault confirmed.
No test, game, decoder, debugger, audio or UI execution occurred.

The old bugs.tsv claim that slot clearing and sentinel seeds are absent is
stale. Current `Unit_ResetBattleState` clears both hands, all8 pocket items,
all9 belt items and all8 backpack items, then seeds backpack5/6 with the empty
item. The [raw capture](reset-inventory-raw-2026-10-28.json) shows the same stores:

- Hands at0x498F4/0x498F8.
- Four pocket pairs: two passes at0x49900..0x49918, offsets0x10C/0x118/0x124/0x130.
- Nine belt entries: three passes at0x49928..0x49940.
- Eight backpack entries: two passes at0x49950..0x4996C.
- Sentinel seeds at0x49974/0x49978, followed by recalculation at0x49980.

The source replaces the last call with `uInvBurden=1`. This is a scoped
constant fold, not a reproduced call. Raw recalculation starts burden at1,
writes only Soldier+0x1A4 and adds item weight, with optional ammo weight.
The [raw initialization](reset-inventory-sentinel-init-raw-2026-10-28.json)
loads Map+0x108 at0x3CB28 and stores it into the sentinel at0x3CB30: its object
is slot0. Static reading of the original archive
finds OBJECTS.DAT at main.dat offset2749406,149 records of208 bytes. Object0
has weight0 at+0xA5 and value2 at+0x28, which bypasses ammo addition. Thus both
sentinel contributions are0, all other cleared pointers are null, and the
original result is exactly1 for the shipped assets.

The [data artifact](reset-inventory-object-data-2026-10-28.json) records archive
and payload hashes, directory bounds, record bytes and extracted fields. No
target or port decoder was run. Arbitrary changed object0 weights are outside
this equivalence proof.

`TestResetBattleState` seeds stale item pointers and checks all clears, sentinel
positions, burden and preserved saved-loadout preferences. This disproves the
old claim that no check reads those slots. It does not replace a current
mission-deploy/re-equip runtime check; current assertions remain unrun.

The existing table start at0x113AC0 was renamed from unk_113AC0 to
Map_apObjects0 after the raw slot reads were checked. Its existing type and
capacity were preserved, not newly proved. The guarded
[before](object-slot0-idb-before-2026-10-28.json) and
[saved readback](object-slot0-idb-after-2026-10-28.json) record that change.
