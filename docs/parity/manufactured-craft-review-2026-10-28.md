# Manufactured craft completion: bounded connection review

Status: raw/source review; no new handoff defect confirmed. No runtime ran.
The [raw capture](manufactured-craft-raw-2026-10-28.json) records the completion
branch, reservation helper, base loop and product mapper.

## Checked branch and owners

The original `ManufScreen_CommitProduction @ 0x6F34C` takes the craft branch
only for product119..121 (`CMP #118 / BLE`, `CMP #122 / BGE`). Products117/118
use the stock-item branch even though the shared mapper recognizes them.

Before spawning, `0x6F604..0x6F62C` obtains the selected base's key and decrements
its reservation. `BaseGrid_SubByte0x654 @ 0x6ACEC` resolves that key to an active
base index, then addresses grid+0x654+index. This matches the C helper's explicit
base-array lookup and already-offset reservation-byte array.

The selected base need not be the base whose production is being processed.
Raw `0x6F63C..0x6F650` passes the current production base to Ship_SpawnAtSlot,
while the reservation decrement above uses the selected base. The port preserves
that difference; it must not be changed to one common base on intuition.
The loop's current-base ordinal is initialized at0x6F35C, restored at0x6F48C
and advanced at0x6F838..0x6F844.

The mapper `Ship_ProductSlotToType @ 0x65A6C` masks the product to8 bits and
returns the same values as the C switch:

| Product | Craft type |
|---|---|
|117|0|
|118|3|
|119|4|
|120|1|
|121|2|
|Other low-byte values|-1|

World_HookRefreshBaseTabs builds the commit and spawn environments from the live
craft array/count, next craft ID, base array, facility grid, constructor environment,
strategic list, seed and zoom owner. It supplies live funds, work/done/request
columns, stock, site count and prices to the commit path. This is the same hourly
hook used by the [research/manufacture handoff](research-manufacture-handoff-2026-10-28.md).
The craft branch is present and connected; it is not merely an unbound helper.

## Fixture limits

The test_baseui commit fixture supplies a local ShipSpawnEnv. It checks product119
creates a Firestorm, product120 creates a Lightning, selected-base reservation
decrement, and product117 remains an item. Those are direct rule fixtures, not
a World-clock craft-production acceptance run.

The existing World research/manufacture fixture produces an ordinary matched
product. A joined craft case remains open: unlock/select a supported craft row,
allocate work and resources, advance normal clock dispatch, and check the live
craft's type/home/hangar/strategic entry plus funds, components and completion.
Current assertions remain unrun. No whole-manufacture-body, native UI or full
campaign pass is inferred from this bounded branch review.
