#!/usr/bin/env python3
"""Check a paid staff and item order through fresh-process load and delivery."""

from pathlib import Path
import re
import sys


# Raw Game_StartNew @ 0x94554 grants these Base 0 rows and craft-0 cargo rows.
# Map.apObjects starts at 0x113AC0; its slot 112/113 records are Engineer and
# Scientist. The raw price bytes for object slot 4 are 3000 at 0xAA2D0.
# The raw Engineer buy arm uses MOV R0,#0xC350 (50000) at 0x76010.
START_BASE = {27: 1, 52: 20, 53: 15, 54: 10, 112: 10, 113: 10}
START_CARGO = {
    1: 3,
    4: 6,
    7: 2,
    8: 2,
    10: 1,
    14: 4,
    15: 4,
    16: 4,
    17: 4,
    36: 4,
    38: 10,
    40: 2,
    41: 2,
    42: 2,
    43: 2,
    44: 2,
    45: 2,
    46: 2,
    47: 1,
    48: 1,
}
START_FUNDS = 4_147_000
ENGINEER_PRICE = 50_000
RIFLE_PRICE = 3_000
PURCHASE_FUNDS = START_FUNDS - ENGINEER_PRICE - RIFLE_PRICE


def fail(message: str) -> None:
    raise SystemExit(f"PURCHASE/STAFF FLOW FAILED: {message}")


def read_lines(path: str) -> list[bytes]:
    return Path(path).read_bytes().splitlines()


def one_line(lines: list[bytes], prefix: bytes) -> bytes:
    found = [line for line in lines if line.startswith(prefix)]
    if len(found) != 1:
        fail(f"expected one {prefix!r} line, found {len(found)}")
    return found[0]


def tuple_line(lines: list[bytes], label: str) -> bytes:
    return one_line(lines, f"pocketufo: strategy tuple {label}:".encode())


def int_field(line: bytes, name: str) -> int:
    match = re.search(rb"(?:^| )" + re.escape(name.encode()) + rb" (-?[0-9]+)(?: |$)", line)
    if match is None:
        fail(f"tuple has no {name} value: {line!r}")
    return int(match.group(1))


def pair_field(line: bytes, name: str) -> tuple[int, int]:
    match = re.search(rb"(?:^| )" + re.escape(name.encode()) + rb" ([0-9]+)/([0-9]+)(?: |$)", line)
    if match is None:
        fail(f"tuple has no {name} pair: {line!r}")
    return int(match.group(1)), int(match.group(2))


def screen_pair(line: bytes) -> tuple[int, int]:
    # App_ScriptStrategyState prints g_nUiScreen first, then nGeoscapeScreen.
    match = re.search(rb"(?:^| )screen ([0-9]+)/([0-9]+)(?: |$)", line)
    if match is None:
        fail(f"tuple has no screen pair: {line!r}")
    return tuple(map(int, match.groups()))


def expect_screen(line: bytes, app_screen: int, geo_screen: int, label: str) -> None:
    actual = screen_pair(line)
    expected = (app_screen, geo_screen)
    if actual != expected:
        fail(f"{label} App/Geo screen pair is {actual}, expected {expected}: {line!r}")


def stock_rows(lines: list[bytes], label: str) -> dict[int, int]:
    prefix = f"pocketufo: strategy stock {label}:".encode()
    stock: dict[int, int] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"slot ([0-9]+) qty ([0-9]+)$", line)
        if match is None:
            fail(f"malformed stock row: {line!r}")
        slot, qty = map(int, match.groups())
        if slot in stock:
            fail(f"duplicate stock slot {slot} at {label}")
        stock[slot] = qty
    return stock


def site_rows(lines: list[bytes], label: str) -> list[tuple[int, int]]:
    prefix = f"pocketufo: strategy site {label}:".encode()
    sites: list[tuple[int, int]] = []
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"index ([0-9]+) flag ([0-9]+) ", line)
        if match is None:
            fail(f"malformed site row: {line!r}")
        sites.append(tuple(map(int, match.groups())))
    return sites


def site_items(lines: list[bytes], label: str) -> dict[tuple[int, int], int]:
    prefix = f"pocketufo: strategy site-item {label}:".encode()
    items: dict[tuple[int, int], int] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"site ([0-9]+) slot ([0-9]+) qty ([0-9]+)$", line)
        if match is None:
            fail(f"malformed site item row: {line!r}")
        site, slot, qty = map(int, match.groups())
        key = (site, slot)
        if key in items:
            fail(f"duplicate pending item row {key} at {label}")
        items[key] = qty
    return items


def cargo_rows(lines: list[bytes], label: str) -> dict[int, tuple[int, int]]:
    prefix = f"pocketufo: campaign cargo item {label}:".encode()
    cargo: dict[int, tuple[int, int]] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"object ([0-9]+) craftIndexQty ([0-9]+) shipSlotQty ([0-9]+)$", line)
        if match is None:
            fail(f"malformed craft item row: {line!r}")
        slot, at_index, at_slot = map(int, match.groups())
        if slot in cargo:
            fail(f"duplicate craft slot {slot} at {label}")
        cargo[slot] = (at_index, at_slot)
    summary = one_line(lines, f"pocketufo: campaign cargo summary {label}:".encode())
    match = re.search(rb"craftObjects ([0-9]+) siteObjects ([0-9]+)$", summary)
    if match is None or tuple(map(int, match.groups())) != (20, 0):
        fail(f"raw craft-0 cargo census differs at {label}: {summary!r}")
    expected = {slot: (qty, qty) for slot, qty in START_CARGO.items()}
    if cargo != expected:
        fail(f"raw craft-0 cargo differs at {label}: {cargo!r}")
    return cargo


def check_tuple_start(line: bytes) -> None:
    # The normal `pick` command confirms the original pending region. Raw ARM
    # leaves Game_StartNew on id 2 with bPlacementPending=1 (0x95450,
    # 0x945D4/0x945E0). NewBaseScreen_TryConfirm skips the funds gate while
    # pending (0x6153C..0x61588), then Geoscape_OnMapClick's pending screen-2
    # arm writes id 1 and clears the byte (0x8B600/0x8B608). This label is
    # after that real pen path, so App/Geo is exactly 22/1.
    expect_screen(line, 22, 1, "new_game after pick")
    if int_field(line, "baseCount") != 1 \
            or int_field(line, "selectedBase") != 0 or int_field(line, "funds") != START_FUNDS:
        fail(f"the normal New Game state differs: {line!r}")
    if pair_field(line, "engineersAvailable/Total") != (10, 10) \
            or pair_field(line, "scientistsAvailable/Total") != (10, 10):
        fail(f"the raw 10/10 staff start differs: {line!r}")
    if int_field(line, "researchAssigned") != 0 or int_field(line, "sites") != 0:
        fail(f"New Game starts with assigned research or an order: {line!r}")


def check_tuple_pending(line: bytes, label: str) -> None:
    expect_screen(line, 22, 0, label)
    if int_field(line, "baseCount") != 1 \
            or int_field(line, "selectedBase") != 0:
        fail(f"{label} is not on the one-base map: {line!r}")
    if int_field(line, "funds") != PURCHASE_FUNDS:
        fail(f"{label} did not keep the exact paid debit to {PURCHASE_FUNDS}: {line!r}")
    if pair_field(line, "engineersAvailable/Total") != (10, 10) \
            or pair_field(line, "scientistsAvailable/Total") != (10, 10):
        fail(f"staff changed before site delivery at {label}: {line!r}")
    if int_field(line, "researchAssigned") != 0 or int_field(line, "sites") != 1:
        fail(f"{label} lost the pending order or started research: {line!r}")


def shop_rows(lines: list[bytes], label: str) -> dict[int, int]:
    prefix = f"pocketufo: strategy row {label}: shop ".encode()
    result: dict[int, int] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"shop ([0-9]+) aux ([0-9]+)$", line)
        if match is None:
            fail(f"malformed live shop row: {line!r}")
        row, slot = map(int, match.groups())
        if row in result:
            fail(f"duplicate shop row {row} at {label}")
        result[row] = slot
    return result


def event_has_staff_arrival(lines: list[bytes]) -> bool:
    return any(re.search(rb"^\s*\[0\] flag 41 slots ", line) for line in lines)


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: check_strategy_purchase_staff.py NEW_LOG DELIVERY_LOG FINAL_LOAD_LOG")
        return 2

    new_lines, delivery_lines, final_lines = (read_lines(path) for path in sys.argv[1:])
    new = tuple_line(new_lines, "new_game")
    check_tuple_start(new)
    if stock_rows(new_lines, "new_game") != START_BASE:
        fail(f"raw Base 0 stock differs: {stock_rows(new_lines, 'new_game')!r}")
    cargo_rows(new_lines, "new_game")
    expect_screen(tuple_line(new_lines, "base_info"), 22, 3, "base_info")
    expect_screen(tuple_line(new_lines, "base_strip"), 22, 1, "base_strip")
    expect_screen(tuple_line(new_lines, "shop_open"), 22, 11, "shop_open")
    rows = shop_rows(new_lines, "shop_open")
    if rows.get(3) != 112 or rows.get(7) != 4:
        fail(f"live shop row order is not Engineer 112 / Rifle 4: {rows!r}")

    engineer_paid = tuple_line(new_lines, "engineer_paid")
    expect_screen(engineer_paid, 22, 11, "engineer_paid")
    if int_field(engineer_paid, "funds") != START_FUNDS - ENGINEER_PRICE:
        fail(f"one Engineer did not cost exactly {ENGINEER_PRICE}: {engineer_paid!r}")
    if stock_rows(new_lines, "engineer_paid") != START_BASE:
        fail("paid Engineer changed Base stock before site arrival")

    rifle_paid = tuple_line(new_lines, "rifle_paid")
    expect_screen(rifle_paid, 22, 11, "rifle_paid")
    if int_field(rifle_paid, "funds") != PURCHASE_FUNDS:
        fail(f"one Rifle did not cost exactly {RIFLE_PRICE}: {rifle_paid!r}")
    if stock_rows(new_lines, "rifle_paid") != START_BASE:
        fail("paid Rifle changed Base stock before site arrival")

    purchase = tuple_line(new_lines, "purchase_pending")
    check_tuple_pending(purchase, "purchase_pending")
    if stock_rows(new_lines, "purchase_pending") != START_BASE:
        fail("Base stock changed before the pending site arrived")
    if site_rows(new_lines, "purchase_pending") != [(0, 24)]:
        fail(f"the paid order is not one normal site-24 record: {site_rows(new_lines, 'purchase_pending')!r}")
    if site_items(new_lines, "purchase_pending") != {(0, 4): 1, (0, 112): 1}:
        fail(f"the pending site does not contain exactly one Rifle and one Engineer: "
             f"{site_items(new_lines, 'purchase_pending')!r}")
    if not re.search(rb"pocketufo: save slot 0: written", b"\n".join(new_lines)):
        fail("the pending paid order was not saved to private slot 0")

    loaded = tuple_line(delivery_lines, "pending_load")
    check_tuple_pending(loaded, "pending_load")
    if int_field(loaded, "funds") != int_field(purchase, "funds") \
            or stock_rows(delivery_lines, "pending_load") != stock_rows(new_lines, "purchase_pending") \
            or site_rows(delivery_lines, "pending_load") != site_rows(new_lines, "purchase_pending") \
            or site_items(delivery_lines, "pending_load") != site_items(new_lines, "purchase_pending"):
        fail("fresh-process load changed the paid pending order")

    delivered = tuple_line(delivery_lines, "delivered")
    expect_screen(tuple_line(delivery_lines, "delivered_before_ack"), 22, 0,
                  "delivered_before_ack")
    expect_screen(delivered, 22, 0, "delivered")
    if int_field(delivered, "baseCount") != 1 \
            or int_field(delivered, "selectedBase") != 0 or int_field(delivered, "sites") != 0:
        fail(f"the normal clock did not deliver the site to Base 0: {delivered!r}")
    if int_field(delivered, "sec") <= int_field(loaded, "sec"):
        fail(f"the normal clock did not advance: {loaded!r} -> {delivered!r}")
    if int_field(delivered, "funds") != PURCHASE_FUNDS:
        fail(f"funds changed after delivery: {delivered!r}")
    if int_field(delivered, "researchAssigned") != 0:
        fail(f"the purchase flow changed research assignments: {delivered!r}")
    if pair_field(delivered, "engineersAvailable/Total") != (11, 11) \
            or pair_field(delivered, "scientistsAvailable/Total") != (10, 10):
        fail(f"delivery did not raise Engineers to 11/11 and retain Scientists 10/10: {delivered!r}")
    delivered_stock = dict(START_BASE)
    delivered_stock[4] = 1
    delivered_stock[112] = 11
    if stock_rows(delivery_lines, "delivered") != delivered_stock:
        fail(f"delivered Base stock differs: {stock_rows(delivery_lines, 'delivered')!r}")
    if not event_has_staff_arrival(delivery_lines):
        fail("the normal arrival card (flag 41) was not shown")
    cargo_rows(delivery_lines, "delivered")
    if not re.search(rb"pocketufo: save slot 1: written", b"\n".join(delivery_lines)):
        fail("the delivered paid order was not saved to private slot 1")

    final = tuple_line(final_lines, "final_load")
    expect_screen(final, 22, 0, "final_load")
    if int_field(final, "baseCount") != 1 \
            or int_field(final, "selectedBase") != 0 or int_field(final, "sites") != 0:
        fail(f"the final fresh-process load is not on the one-base map: {final!r}")
    if int_field(final, "funds") != PURCHASE_FUNDS \
            or pair_field(final, "engineersAvailable/Total") != (11, 11) \
            or pair_field(final, "scientistsAvailable/Total") != (10, 10) \
            or int_field(final, "researchAssigned") != 0:
        fail(f"paid funds or staff did not persist: {final!r}")
    if stock_rows(final_lines, "final_load") != delivered_stock:
        fail(f"final loaded Base stock differs: {stock_rows(final_lines, 'final_load')!r}")
    cargo_rows(final_lines, "final_load")

    print("ok: raw New Game funds, Base stock, staff, and all 20 craft-0 cargo rows matched")
    print("ok: live Buy rows were Engineer 112 and Rifle 4")
    print("ok: one Engineer cost 50,000; one Rifle cost 3,000; the 53,000 debit was exact")
    print("ok: one real site held exactly slot 112 x1 and slot 4 x1 before clock time")
    print("ok: pending site survived fresh-process load and normal arrival added one Engineer and Rifle")
    print("ok: delivered Base stock and staff survived a second fresh-process load")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
