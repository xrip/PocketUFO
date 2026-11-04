# Soldier purchase through the World clock

Status: connected fixture reviewed and compiled in both full configurations;
assertions remain unrun. No native timer or game-flow acceptance is claimed.

## Purpose and route

`TestWorldPurchaseHireDelivery` in test_geoscapeframe.c checks that a Soldier
ordered for base1 arrives there after24 hourly clock steps, once, with the right
roster ID. Earlier shop fixtures called the hourly hook directly and never
checked the special Soldier order through the World pulse.

The fixture starts a fresh World, places the first base through its pointer
route, and prepares base1 through Base_CreateNamedEntry with finished living
quarters. These are setup conditions, not full base-construction acceptance.
Base Build/Buy open through their production control callbacks; base selection,
the exact row1000 order, confirm, return to globe and speed selection use World
pointer handling. No fake base record or direct hourly callback supplies delivery.

It checks the40,000 debit and pending hire's base key. World_PulseGeoscape then
drives the live screen0/unpaused clock path at3601 game seconds per pulse:

- After23 pulses, the hire remains pending at countdown1, with no Soldier or ID advance.
- Pulse24 appends the Soldier at the prior next ID, assigned to base1, and removes
  the completed order. Base0 roster and unrelated stock remain unchanged.
- Pulse25 produces no duplicate Soldier, pending order or second debit.

Fresh-game state supports the stock/funds assertions: the clock starts
1999-01-01 00:00 and reaches Jan2 00:00:24, so no month-end funding runs.
Starter craft are based/full-range, research/manufacture columns are clear,
and facilities are complete. No craft launches or other pending sites precede
this order. The test does not disable production clock hooks or force pauses clear.

## Evidence and limits

The [raw/source audit](purchase-delivery-2026-10-28.md) records debit, site key,
confirmation and grant owners. Raw0x98B68..0x98BAC decrements countdowns before
0x98BCC..0x98BD8 dispatches zero flags, so a24 order delivers on hourly call24.
The [worker report](../../.workers/luna-purchase-clock-fixture/RESULT.md) records
fixture isolation and unchanged-stock preconditions.

The [build manifest](inventory-purchase-build-artifacts-2026-10-28.json) identifies
the compiled source and images. The fixture runs only in the full World test,
after earlier scenarios; it has not been executed. It exercises the World pulse
entry used by C353, not WndProc delivery, OS timer cadence or desktop input.
