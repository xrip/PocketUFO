#!/usr/bin/env python3
"""Check a real New Game transfer of one raw slot-52 item to a second base."""

from __future__ import annotations

from pathlib import Path
import re
import sys


# Raw Game_StartNew @ 0x94554, Base0 owner cell 0x114B2C, ObjectRec pointer
# cell 0x113B90, quantity 20. Map.apObjects begins at 0x113AC0, so this is
# array index 52. This is BaseRec stock, not craft cargo or a visible row id.
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
BASE0_AFTER_TRANSFER = {27: 1, 52: 19, 53: 15, 54: 10, 112: 10, 113: 10}
START_FUNDS = 4_147_000


def fail(message: str) -> None:
    raise SystemExit(f"RAW TRANSFER FLOW FAILED: {message}")


def read_lines(path: str) -> list[bytes]:
    return Path(path).read_bytes().splitlines()


def one_line(lines: list[bytes], prefix: bytes) -> bytes:
    found = [line for line in lines if line.startswith(prefix)]
    if len(found) != 1:
        fail(f"expected one {prefix!r} line, found {len(found)}")
    return found[0]


def tuple_for(lines: list[bytes], label: str) -> bytes:
    return one_line(lines, f"pocketufo: strategy tuple {label}:".encode())


def field(line: bytes, name: str) -> int:
    match = re.search(rb"(?:^| )" + re.escape(name.encode()) + rb" (-?[0-9]+)(?: |$)", line)
    if match is None:
        fail(f"tuple has no {name} value: {line!r}")
    return int(match.group(1))


def pair_field(line: bytes, name: str) -> tuple[int, int]:
    match = re.search(rb"(?:^| )" + re.escape(name.encode()) + rb" ([0-9]+)/([0-9]+)(?: |$)", line)
    if match is None:
        fail(f"tuple has no {name} pair: {line!r}")
    return tuple(map(int, match.groups()))


def expect_same_base_metrics(before: bytes, after: bytes, label: str) -> None:
    for name in ("engineersAvailable/Total", "scientistsAvailable/Total"):
        if pair_field(before, name) != pair_field(after, name):
            fail(f"{label} changed selected-Base {name}: {before!r} -> {after!r}")
    for name in ("researchAssigned", "engineerAssigned", "soldiers", "craft",
                 "lab", "workshop", "stores", "quarters", "occupied"):
        if field(before, name) != field(after, name):
            fail(f"{label} changed selected-Base {name}: {before!r} -> {after!r}")


def screen_pair(line: bytes) -> tuple[int, int]:
    match = re.search(rb"(?:^| )screen ([0-9]+)/([0-9]+)(?: |$)", line)
    if match is None:
        fail(f"tuple has no App/Geo screen pair: {line!r}")
    return tuple(map(int, match.groups()))


def expect_screen(line: bytes, geo: int, label: str) -> None:
    # The first value is App state; the second is the geoscape screen id.
    if screen_pair(line) != (22, geo):
        fail(f"{label} is not App/Geo 22/{geo}: {line!r}")


def expect_raw_base0_newgame(line: bytes, label: str) -> None:
    expect_screen(line, 1, label)
    if field(line, "baseCount") != 1 or field(line, "selectedBase") != 0:
        fail(f"{label} is not the one-base New Game Base 0: {line!r}")
    if pair_field(line, "engineersAvailable/Total") != (10, 10) \
            or pair_field(line, "scientistsAvailable/Total") != (10, 10) \
            or field(line, "researchAssigned") != 0 or field(line, "engineerAssigned") != 0:
        fail(f"{label} does not have the raw ten-person Base 0 workforce: {line!r}")


def expect_cold_load(line: bytes, label: str) -> None:
    expect_screen(line, 0, label)
    if field(line, "tickSec") != 5:
        fail(f"{label} did not use the raw fresh-load five-second speed: {line!r}")


def expect_state(
    line: bytes,
    label: str,
    geo: int,
    bases: int,
    selected: int,
    *,
    funds: int | None = None,
    sites: int | None = None,
) -> None:
    expect_screen(line, geo, label)
    if field(line, "baseCount") != bases or field(line, "selectedBase") != selected:
        fail(f"{label} has the wrong base count or selection: {line!r}")
    if funds is not None and field(line, "funds") != funds:
        fail(f"{label} funds changed: {line!r}")
    if sites is not None and field(line, "sites") != sites:
        fail(f"{label} has {field(line, 'sites')} sites, wanted {sites}")
    if field(line, "researchAssigned") != 0:
        fail(f"{label} changed research assignments: {line!r}")


def stock_rows(lines: list[bytes], label: str) -> dict[int, int]:
    prefix = f"pocketufo: strategy stock {label}:".encode()
    result: dict[int, int] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"slot ([0-9]+) qty ([0-9]+)$", line)
        if match is None:
            fail(f"bad stock row: {line!r}")
        slot, qty = map(int, match.groups())
        if slot in result:
            fail(f"duplicate stock slot {slot} at {label}")
        result[slot] = qty
    return result


def site_rows(lines: list[bytes], label: str) -> list[tuple[int, int]]:
    prefix = f"pocketufo: strategy site {label}:".encode()
    result: list[tuple[int, int]] = []
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"index ([0-9]+) flag ([0-9]+) ", line)
        if match is None:
            fail(f"bad site row: {line!r}")
        result.append(tuple(map(int, match.groups())))
    return result


def site_items(lines: list[bytes], label: str) -> dict[tuple[int, int], int]:
    prefix = f"pocketufo: strategy site-item {label}:".encode()
    result: dict[tuple[int, int], int] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"site ([0-9]+) slot ([0-9]+) qty ([0-9]+)$", line)
        if match is None:
            fail(f"bad site-item row: {line!r}")
        site, slot, qty = map(int, match.groups())
        key = (site, slot)
        if key in result:
            fail(f"duplicate site-item row {key} at {label}")
        result[key] = qty
    return result


def cargo_rows(lines: list[bytes], label: str) -> dict[int, tuple[int, int]]:
    prefix = f"pocketufo: campaign cargo item {label}:".encode()
    result: dict[int, tuple[int, int]] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"object ([0-9]+) craftIndexQty ([0-9]+) shipSlotQty ([0-9]+)$", line)
        if match is None:
            fail(f"bad craft cargo row: {line!r}")
        slot, craft_qty, ship_qty = map(int, match.groups())
        if slot in result:
            fail(f"duplicate craft slot {slot} at {label}")
        result[slot] = (craft_qty, ship_qty)
    summary = one_line(lines, f"pocketufo: campaign cargo summary {label}:".encode())
    match = re.search(rb"craftObjects ([0-9]+) siteObjects ([0-9]+)$", summary)
    if match is None or tuple(map(int, match.groups())) != (20, 0):
        fail(f"raw cargo census differs at {label}: {summary!r}")
    expected = {slot: (qty, qty) for slot, qty in START_CARGO.items()}
    if result != expected:
        fail(f"raw craft-0 cargo differs at {label}: {result!r}")
    return result


def grid_snapshot(lines: list[bytes], base: int, occurrence: int = -1) -> tuple[dict[int, list[int]], dict[int, list[int]]]:
    marker = f"pocketufo: grid base {base}".encode()
    starts = [i for i, line in enumerate(lines) if line == marker]
    if not starts:
        fail(f"no grid for base {base}")
    if occurrence < 0:
        occurrence += len(starts)
    if occurrence < 0 or occurrence >= len(starts):
        fail(f"base {base} has only {len(starts)} grid snapshots; requested {occurrence}")
    start = starts[occurrence]
    end = next((i for i in range(start + 1, len(lines))
                if lines[i].startswith(b"pocketufo: grid base ")), len(lines))
    facilities: dict[int, list[int]] = {}
    days: dict[int, list[int]] = {}
    for line in lines[start + 1:end]:
        stripped = line.strip()
        match = re.match(rb"facTyp row ([0-5]):\s*(.*)$", stripped)
        if match is not None:
            facilities[int(match.group(1))] = [int(value) for value in match.group(2).split()]
            continue
        match = re.match(rb"days\s+row ([0-5]):\s*(.*)$", stripped)
        if match is not None:
            days[int(match.group(1))] = [int(value) for value in match.group(2).split()]
    if len(facilities) != 6 or len(days) != 6 \
            or any(len(row) != 6 for row in facilities.values()) \
            or any(len(row) != 6 for row in days.values()):
        fail(f"base {base} grid is incomplete: {facilities!r} / {days!r}")
    return facilities, days


def stores_days(lines: list[bytes], complete: bool, occurrence: int = -1) -> int:
    facilities, days = grid_snapshot(lines, 1, occurrence)
    cells = [(row, col) for row, values in facilities.items()
             for col, value in enumerate(values) if value == 6]
    if cells != [(1, 3)]:
        fail(f"Base 1 does not have one Stores cell at row 1, column 3: {cells!r}")
    remaining = days[1][3]
    if complete and remaining != 0:
        fail(f"Base 1 Stores still has {remaining} build days")
    if not complete and remaining <= 0:
        fail(f"pending Stores cell has no build days: {remaining}")
    return remaining


def transfer_rows(lines: list[bytes], label: str) -> list[tuple[int, int, int, int]]:
    prefix = f"pocketufo: strategy transfer-row {label}:".encode()
    result: list[tuple[int, int, int, int]] = []
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"row ([0-9]+) slot (-?[0-9]+) source (-?[0-9]+) pending (-?[0-9]+)$", line)
        if match is not None:
            result.append(tuple(map(int, match.groups())))
    return result


def panel_view(lines: list[bytes], label: str) -> tuple[int, int, int, int, int, int]:
    line = one_line(lines, f"pocketufo: strategy panel {label}:".encode())
    match = re.search(
        rb"shop rows ([0-9]+) pos (-?[0-9]+)/(-?[0-9]+) font ([0-9]+) style ([0-9]+) scroll (-?[0-9]+)$",
        line,
    )
    if match is None:
        fail(f"transfer panel geometry is incomplete: {line!r}")
    return tuple(map(int, match.groups()))


def control_rect(lines: list[bytes], index: int) -> tuple[int, int, int, int]:
    prefix = f"pocketufo: strategy control transfer {index} ".encode()
    matches = [line for line in lines if line.startswith(prefix)]
    for line in reversed(matches):
        match = re.search(rb"present 1 enabled 1 rect (-?[0-9]+)/(-?[0-9]+)/([0-9]+)/([0-9]+)", line)
        if match is not None:
            return tuple(map(int, match.groups()))
    fail(f"transfer control {index} was not live and enabled: {matches[-1:]!r}")


def strategy_control_rect(lines: list[bytes], label: str, group: str,
                          index: int) -> tuple[int, int, int, int]:
    snapshot = tuple_for(lines, label)
    start = lines.index(snapshot)
    end = next((i for i in range(start + 1, len(lines))
                if lines[i].startswith(b"pocketufo: strategy tuple ")
                or lines[i].startswith(b"pocketufo: step ")), len(lines))
    # Read-only grid/cargo diagnostics may sit between strategy snapshots without
    # writing a `step` line; the next tuple is also a hard snapshot boundary.
    prefix = f"pocketufo: strategy control {group} {index} ".encode()
    found = [line for line in lines[start:end] if line.startswith(prefix)]
    if len(found) != 1:
        fail(f"expected one live {group} control {index} at {label}: {found!r}")
    line = found[0]
    match = re.search(rb"present 1 enabled 1 rect (-?[0-9]+)/(-?[0-9]+)/([0-9]+)/([0-9]+) .*callback 1$", line)
    if match is None:
        fail(f"{group} control {index} is not live, enabled, and connected: {line!r}")
    return tuple(map(int, match.groups()))


def strategy_control_center(lines: list[bytes], label: str, group: str,
                             index: int) -> tuple[int, int]:
    x, y, width, height = strategy_control_rect(lines, label, group, index)
    return x + width // 2, y + height // 2


def last_line_flags(lines: list[bytes]) -> tuple[int, list[int]]:
    headers = [i for i, line in enumerate(lines) if line.startswith(b"pocketufo: lines:")]
    if not headers:
        fail("log has no read-only status-line snapshot")
    start = headers[-1]
    match = re.search(rb"pocketufo: lines: ([0-9]+) of [0-9]+(?:,|$)", lines[start])
    if match is None:
        fail(f"bad status-line header: {lines[start]!r}")
    count = int(match.group(1))
    flags: list[int] = []
    for line in lines[start + 1:]:
        if line.startswith(b"pocketufo:"):
            break
        row = re.search(rb"^\s*\[[0-9]+\] flag (-?[0-9]+) slots ", line)
        if row is not None:
            flags.append(int(row.group(1)))
    if len(flags) != count:
        fail(f"status-line count differs: header={count}, rows={flags!r}")
    return count, flags


def hours_to_store_completion(total_seconds: int, days_left: int) -> int:
    if days_left <= 0:
        fail(f"pending Stores has invalid remaining days {days_left}")
    next_day_count = total_seconds // 86400 + days_left
    remaining_seconds = next_day_count * 86400 - total_seconds
    return (remaining_seconds + 3600) // 3601


def require_destination(lines: list[bytes], label: str) -> None:
    line = one_line(lines, f"pocketufo: strategy transfer {label}:".encode())
    if not re.search(rb"built 1 picker 0 workBase 1 ", line):
        fail(f"Base 1 is not latched as destination at {label}: {line!r}")


def save_written(lines: list[bytes], slot: int) -> None:
    if not re.search(rb"pocketufo: save slot " + str(slot).encode() + rb": written", b"\n".join(lines)):
        fail(f"save slot {slot} was not written")


def write_new_flow(path: str, text: str) -> None:
    target = Path(path)
    if target.exists():
        fail(f"generated flow already exists: {target}")
    size = len(text.encode("utf-8"))
    if size >= 7900:
        fail(f"generated flow is too large for the app script buffer: {size} bytes")
    target.write_text(text, encoding="utf-8", newline="\n")


def clock_stage_flow(load_slot: int, hours: int, initial: bool, final_event: bool,
                     speed_center: tuple[int, int], ready_label: str = "stores_ready") -> str:
    speed_x, speed_y = speed_center
    commands = [
        f"# final-known-event {1 if final_event else 0}",
        f"load {load_slot}",
        "strategystate stores_clock_loaded",
        "grid 1",
        "campaign cargo stores_clock_loaded",
    ]
    if initial:
        commands.append("strategystate stores_pending_load")
    commands.append(f"click {speed_x} {speed_y}")
    for hour in range(hours):
        commands.append("tick 1")
        if final_event and hour + 1 == hours:
            commands.extend(("campaign ack-status 38", f"click {speed_x} {speed_y}"))
        else:
            commands.extend(("campaign ack-confirm", f"click {speed_x} {speed_y}"))
    commands.extend((
        "strategystate stores_clock_end",
        "lines",
        "grid 1",
        "campaign cargo stores_clock_end",
    ))
    if final_event:
        commands.extend((
            f"strategystate {ready_label}",
            "lines",
            "grid 1",
            f"campaign cargo {ready_label}",
        ))
    commands.extend(("save 1", "state stores_clock_saved"))
    return "\n".join(commands) + "\n"


def check_hourly_speed_inputs(flow: list[str], hours: int,
                              speed_center: tuple[int, int], terminal_status: str | None) -> None:
    click = f"click {speed_center[0]} {speed_center[1]}"
    ticks = [i for i, line in enumerate(flow) if line.strip() == "tick 1"]
    if len(ticks) != hours or ticks[0] == 0 or flow[ticks[0] - 1].strip() != click:
        fail("clock flow must select the live one-hour control after load and before its first pulse")
    for hour, index in enumerate(ticks):
        expected_ack = terminal_status if terminal_status is not None and hour + 1 == hours else "campaign ack-confirm"
        if index + 2 >= len(flow) or flow[index + 1].strip() != expected_ack \
                or flow[index + 2].strip() != click:
            fail(f"clock pulse {hour + 1} must use its guarded ack then reselect live one-hour speed")


def check_ack_contract(lines: list[bytes], expected_calls: int) -> int:
    prefix = b"pocketufo: campaign ack-confirm:"
    events = [line for line in lines if line.startswith(prefix)]
    failures = [line for line in events if b": FAIL" in line]
    if failures:
        fail(f"guarded flag-0xA acknowledgement rejected the live state: {failures!r}")
    skips = [line for line in events if b": SKIP no_flag10" in line]
    before = [line for line in events if b": before flag=10 " in line]
    after = [line for line in events if b": after lineCount=0 " in line]
    passed = [line for line in events if line.endswith(b": PASS")]
    if len(skips) + len(before) != expected_calls or len(before) != len(after) \
            or len(passed) != expected_calls:
        fail(f"ack helper did not log one result per call: {events!r}")
    skip_re = re.compile(rb"SKIP no_flag10 screen=0 pause=0 selection=0$")
    before_re = re.compile(
        rb"before flag=10 lineCount=1 pause=1 selection=0 .*confirmOwner=1 enabled=1 "
        rb"rect=[0-9]+/[0-9]+/[0-9]+/[0-9]+ .*callbackMatch=1 tickSec=3600$"
    )
    after_re = re.compile(
        rb"after lineCount=0 firstFlag=-1 pause=0 selection=0 .*tickSec=5 result=PASS$"
    )
    if any(skip_re.search(line) is None for line in skips):
        fail(f"ack helper skipped when the clock was not clear: {skips!r}")
    if any(before_re.search(line) is None for line in before):
        fail(f"ack helper used a state outside the raw line-0xA contract: {before!r}")
    if any(after_re.search(line) is None for line in after):
        fail(f"ack helper did not clear and unpause through the real control: {after!r}")
    return len(before)


def check_status_ack(lines: list[bytes], expected_flag: int) -> None:
    prefix = b"pocketufo: campaign ack-status:"
    events = [line for line in lines if line.startswith(prefix)]
    failures = [line for line in events if b": FAIL" in line]
    before = [line for line in events if b": before expected=" in line]
    after = [line for line in events if b": after expected=" in line]
    passed = [line for line in events if line.endswith(b": PASS")]
    if failures or len(before) != 1 or len(after) != 1 or len(passed) != 1:
        fail(f"same-process status ack did not log one PASS: {events!r}")
    flag = str(expected_flag).encode()
    before_re = re.compile(
        rb"before expected=" + flag + rb" flag=" + flag
        + rb" lineCount=1 pause=1 selection=0 screen=0 geo=1 tact=0 tactOwner=0 modal=0 "
        + rb"control=GEOUI_CTL_187024 owner=1 enabled=1 "
        + rb"rect=(-?[0-9]+)/(-?[0-9]+)/([0-9]+)/([0-9]+) "
        + rb"center=(-?[0-9]+)/(-?[0-9]+) callbackMatch=1 tickSec=3600$"
    )
    match = before_re.search(before[0])
    if match is None:
        fail(f"status ack accepted a state outside the raw flag-{expected_flag} guard: {before[0]!r}")
    x, y, width, height, center_x, center_y = map(int, match.groups())
    if (center_x, center_y) != (x + width // 2, y + height // 2):
        fail(f"status ack did not use the live control center: {before[0]!r}")
    after_re = re.compile(
        rb"after expected=" + flag
        + rb" lineCount=0 firstFlag=-1 pause=0 selection=0 screen=0 geo=1 tact=0 modal=0 "
        + rb"tickSec=5 result=PASS$"
    )
    if after_re.search(after[0]) is None:
        fail(f"status ack did not clear its line and unpause at 5-second speed: {after[0]!r}")


def check_status_ack(lines: list[bytes], expected_flag: int) -> None:
    prefix = b"pocketufo: campaign ack-status:"
    events = [line for line in lines if line.startswith(prefix)]
    failures = [line for line in events if b": FAIL" in line]
    before = [line for line in events if b": before expected=" in line]
    after = [line for line in events if b": after expected=" in line]
    passed = [line for line in events if line.endswith(b": PASS")]
    if failures or len(before) != 1 or len(after) != 1 or len(passed) != 1:
        fail(f"same-process status acknowledgement did not log one PASS: {events!r}")
    flag = str(expected_flag).encode()
    before_re = re.compile(
        rb"before expected=" + flag + rb" flag=" + flag
        + rb" lineCount=1 pause=1 selection=0 screen=0 geo=1 tact=0 tactOwner=0 modal=0 "
        + rb"control=GEOUI_CTL_187024 owner=1 enabled=1 "
        + rb"rect=(-?[0-9]+)/(-?[0-9]+)/([0-9]+)/([0-9]+) "
        + rb"center=(-?[0-9]+)/(-?[0-9]+) callbackMatch=1 tickSec=3600$"
    )
    match = before_re.search(before[0])
    if match is None:
        fail(f"status helper accepted a state outside the raw flag-{expected_flag} guard: {before[0]!r}")
    x, y, width, height, center_x, center_y = map(int, match.groups())
    if (center_x, center_y) != (x + width // 2, y + height // 2):
        fail(f"status helper did not click the live control center: {before[0]!r}")
    after_re = re.compile(
        rb"after expected=" + flag
        + rb" lineCount=0 firstFlag=-1 pause=0 selection=0 screen=0 geo=1 tact=0 modal=0 "
        + rb"tickSec=5 result=PASS$"
    )
    if after_re.search(after[0]) is None:
        fail(f"status helper did not clear its line and unpause at 5-second speed: {after[0]!r}")


def mode_setup(log_path: str, save_path: str, clock_flow: str) -> None:
    lines = read_lines(log_path)
    start = tuple_for(lines, "new_game")
    expect_raw_base0_newgame(start, "new_game after pick")
    if field(start, "funds") != START_FUNDS or field(start, "sites") != 0:
        fail(f"raw New Game state differs: {start!r}")
    if stock_rows(lines, "new_game") != START_BASE:
        fail(f"raw Base 0 stock differs: {stock_rows(lines, 'new_game')!r}")
    cargo_rows(lines, "new_game")
    expect_state(tuple_for(lines, "new_game_map"), "new_game_map", 0, 1, 0,
                 funds=START_FUNDS, sites=0)

    build_mode = tuple_for(lines, "build_mode")
    expect_state(build_mode, "build_mode", 1, 1, 0, funds=START_FUNDS, sites=0)
    expect_state(tuple_for(lines, "new_base_region"), "new_base_region", 2, 1, 0,
                 funds=START_FUNDS, sites=0)
    expect_state(tuple_for(lines, "new_base_access_lift"), "new_base_access_lift", 7, 1, 1,
                 funds=START_FUNDS, sites=0)
    created = tuple_for(lines, "second_base_created")
    expect_state(created, "second_base_created", 1, 2, 1, sites=0)
    base_funds = field(created, "funds")
    if base_funds <= 0 or base_funds >= START_FUNDS:
        fail(f"real Access Lift/base placement did not debit funds: {created!r}")
    expect_state(tuple_for(lines, "second_base_map"), "second_base_map", 0, 2, 1,
                 funds=base_funds, sites=0)

    expect_state(tuple_for(lines, "stores_list_open"), "stores_list_open", 7, 2, 1,
                 funds=base_funds, sites=0)
    expect_state(tuple_for(lines, "stores_selected"), "stores_selected", 7, 2, 1,
                 funds=base_funds, sites=0)
    placed = tuple_for(lines, "stores_placed")
    expect_state(placed, "stores_placed", 7, 2, 1, sites=0)
    placed_funds = field(placed, "funds")
    if placed_funds <= 0 or placed_funds >= base_funds:
        fail(f"real Stores placement did not debit funds: {placed!r}")
    build_days = stores_days(lines, complete=False)
    pending = tuple_for(lines, "stores_pending_map")
    expect_state(pending, "stores_pending_map", 0, 2, 1, funds=placed_funds, sites=0)
    expect_same_base_metrics(placed, pending, "Base 1 map close after Stores placement")
    if pair_field(pending, "engineersAvailable/Total") != (0, 0) \
            or pair_field(pending, "scientistsAvailable/Total") != (0, 0):
        fail(f"new Base 1 has an unexpected workforce: {pending!r}")
    if stock_rows(lines, "stores_pending_map"):
        fail(f"Base 1 received unrequested stock: {stock_rows(lines, 'stores_pending_map')!r}")
    save_written(lines, 0)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"pending Stores save is missing or empty: {saved}")

    pending_sec = field(pending, "sec")
    hours = hours_to_store_completion(pending_sec, build_days)
    stage_hours = min(120, hours)
    speed_center = strategy_control_center(lines, "stores_pending_map", "speed", 7)
    write_new_flow(clock_flow, clock_stage_flow(
        0, stage_hours, initial=True, final_event=hours <= stage_hours,
        speed_center=speed_center,
    ))
    print("ok: raw start has Base 0 slot52 x20 and all 20 craft cargo rows")
    print(f"ok: live base/store controls charged funds; measured Store build days {build_days}; "
          f"normal clock needs {hours} hourly pulses")


def mode_clock_stage(previous_path: str, current_path: str, flow_path: str,
                     save_path: str) -> None:
    previous = read_lines(previous_path)
    current = read_lines(current_path)
    flow = Path(flow_path).read_text(encoding="utf-8").splitlines()
    hours = sum(line.strip() == "tick 1" for line in flow)
    final_event = "# final-known-event 1" in flow
    ack_calls = sum(line.strip() == "campaign ack-confirm" for line in flow)
    status_calls = [line.strip() for line in flow if line.strip().startswith("campaign ack-status ")]
    if hours < 1 or hours > 120:
        fail(f"Stores clock stage has invalid pulse count {hours}")
    expected_ack = hours - (1 if final_event else 0)
    if ack_calls != expected_ack or status_calls != (["campaign ack-status 38"] if final_event else []):
        fail(f"Stores stage has {ack_calls} flag-0xA helper calls for {hours} pulses; expected {expected_ack}")
    match = re.fullmatch(r"stores-clock-([0-9]{2})", Path(flow_path).stem)
    if match is None:
        fail(f"Stores clock flow name has no stage index: {flow_path}")
    stage = int(match.group(1))
    previous_label = "stores_pending_map" if stage == 0 else "stores_clock_end"
    before = tuple_for(previous, previous_label)
    loaded = tuple_for(current, "stores_clock_loaded")
    ended = tuple_for(current, "stores_clock_end")
    expect_state(loaded, "stores_clock_loaded", 0, 2, 1,
                 funds=field(before, "funds"), sites=0)
    expect_cold_load(loaded, "stores_clock_loaded")
    speed_center = strategy_control_center(current, "stores_clock_loaded", "speed", 7)
    check_hourly_speed_inputs(
        flow, hours, speed_center,
        "campaign ack-status 38" if final_event else None,
    )
    expect_state(ended, "stores_clock_end", 0, 2, 1,
                 funds=field(before, "funds"), sites=0)
    for name in ("sec", "pause", "funds", "baseCount", "selectedBase", "sites"):
        if field(loaded, name) != field(before, name):
            fail(f"fresh load changed {name} before Stores stage {stage}: {before!r} -> {loaded!r}")
    expect_same_base_metrics(before, loaded, f"fresh load before Stores stage {stage}")
    if strategy_control_center(previous, previous_label, "speed", 7) \
            != strategy_control_center(current, "stores_clock_loaded", "speed", 7):
        fail(f"fresh load moved the live one-hour speed control before Stores stage {stage}")

    previous_days = stores_days(previous, complete=False)
    loaded_days = stores_days(current, complete=False, occurrence=0)
    if loaded_days != previous_days:
        fail(f"fresh load changed Stores days before stage {stage}: {previous_days} -> {loaded_days}")
    elapsed = field(ended, "sec") - field(loaded, "sec")
    if elapsed != hours * 3601:
        fail(f"Stores stage {stage} advanced {elapsed} seconds for {hours} real one-hour pulses")
    day_ticks = field(ended, "sec") // 86400 - field(loaded, "sec") // 86400
    expected_days = max(0, loaded_days - day_ticks)
    end_days = stores_days(current, complete=(expected_days == 0), occurrence=1)
    if end_days != expected_days:
        fail(f"Stores countdown differs from raw daily clock: {loaded_days} -> {end_days}, expected {expected_days}")
    if final_event != (expected_days == 0):
        fail(f"Stores final-pulse marker disagrees with the live day counter: marker={final_event}, days={expected_days}")
    if field(ended, "tickSec") != 3600:
        fail(f"Stores stage did not leave one-hour speed selected: {ended!r}")

    if stage == 0:
        if stock_rows(previous, "stores_pending_map") or stock_rows(current, "stores_clock_loaded"):
            fail("Base 1 received stock before Stores completion")
        cargo_rows(previous, "new_game")
        cargo_rows(current, "stores_clock_loaded")
    else:
        if stock_rows(previous, "stores_clock_end") != stock_rows(current, "stores_clock_loaded"):
            fail(f"fresh load changed Base 1 stock before stage {stage}")
        cargo_rows(previous, "stores_clock_end")
        cargo_rows(current, "stores_clock_loaded")

    acked = check_ack_contract(current, ack_calls)
    speed_center = strategy_control_center(current, "stores_clock_loaded", "speed", 7)
    speed_click = f"click {speed_center[0]} {speed_center[1]}"
    clicks = [line.strip() for line in flow if line.strip().startswith("click ")]
    if clicks != [speed_click] * (hours + 1):
        fail(f"Stores stage did not reselect the live one-hour control around every pulse: {clicks!r}")
    line_count, flags = last_line_flags(current)
    if final_event and expected_days == 0:
        if field(ended, "stores") != 300:
            fail(f"raw day countdown ended but Stores capacity is not 300: {ended!r}")
        check_status_ack(current, 38)
        if field(ended, "pause") != 0 or line_count != 0 or flags:
            fail(f"same-process flag38 acknowledgement did not clear the completion card: {flags!r} / {ended!r}")
    else:
        if expected_days == 0:
            fail("Stores completed earlier than the generated final-event pulse")
        if field(ended, "pause") != 0 or line_count != 0:
            fail(f"non-final Stores stage left a message or pause: {flags!r} / {ended!r}")

    if stock_rows(current, "stores_clock_end"):
        fail(f"Base 1 received unrequested stock while Stores built: {stock_rows(current, 'stores_clock_end')!r}")
    cargo_rows(current, "stores_clock_end")
    save_written(current, 1)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"Stores clock stage {stage} save is missing or empty: {saved}")
    print(f"ok: Stores stage {stage} advanced {hours} hours / {day_ticks} raw day ticks; acknowledged {acked} flag-0xA alerts")


def mode_clock_next(stage_path: str, output_flow: str) -> None:
    lines = read_lines(stage_path)
    state = tuple_for(lines, "stores_clock_end")
    days_left = stores_days(lines, complete=(field(state, "stores") == 300))
    if days_left == 0:
        if field(state, "stores") != 300:
            fail(f"Stores countdown reached zero without capacity: {state!r}")
        if Path(output_flow).exists():
            fail(f"unexpected pre-existing next Stores flow: {output_flow}")
        print("ok: measured Stores countdown is complete; no more clock flow is needed")
        return
    if field(state, "pause") != 0 or field(state, "tickSec") != 3600:
        fail(f"cannot continue Stores from paused time or wrong speed: {state!r}")
    match = re.fullmatch(r"stores-clock-([0-9]{2})", Path(output_flow).stem)
    if match is None:
        fail(f"Stores flow name has no stage index: {output_flow}")
    stage = int(match.group(1))
    if stage >= 6:
        fail(f"Stores remains unfinished after {stage} saved stages")
    hours_left = hours_to_store_completion(field(state, "sec"), days_left)
    hours = min(120, hours_left)
    speed_center = strategy_control_center(lines, "stores_clock_end", "speed", 7)
    write_new_flow(output_flow, clock_stage_flow(
        1, hours, initial=False, final_event=hours_left <= hours,
        speed_center=speed_center,
    ))
    print(f"ok: measured Stores has {days_left} build days / {hours_left} hour pulses left; wrote stage {stage}")


def mode_stores(setup_path: str, first_clock_path: str, final_clock_path: str,
                save_path: str) -> None:
    setup = read_lines(setup_path)
    first_clock = read_lines(first_clock_path)
    clock = read_lines(final_clock_path)
    placed = tuple_for(setup, "stores_placed")
    saved_map = tuple_for(setup, "stores_pending_map")
    placed_funds = field(placed, "funds")
    start = tuple_for(first_clock, "stores_pending_load")
    expect_state(start, "stores_pending_load", 0, 2, 1, funds=placed_funds, sites=0)
    expect_cold_load(start, "stores_pending_load")
    if field(start, "pause") != 0:
        fail(f"pending Stores load is paused: {start!r}")
    for name in ("sec", "pause", "baseCount", "selectedBase", "sites",
                 "funds"):
        before_value = field(saved_map, name)
        loaded_value = field(start, name)
        if before_value != loaded_value:
            fail(f"fresh Stores load changed saved {name}: {before_value!r} -> {loaded_value!r}")
    expect_same_base_metrics(saved_map, start, "Base 1 fresh Stores load")
    ready = tuple_for(clock, "stores_ready")
    expect_state(ready, "stores_ready", 0, 2, 1, funds=placed_funds, sites=0)
    if field(ready, "pause") != 0 or field(ready, "tickSec") != 3600 \
            or field(ready, "sec") <= field(start, "sec") or field(ready, "stores") != 300:
        fail(f"normal clock did not finish Stores at Base 1: {ready!r}")
    for name in ("engineersAvailable/Total", "scientistsAvailable/Total"):
        if pair_field(start, name) != (0, 0) or pair_field(ready, name) != pair_field(start, name):
            fail(f"normal Stores clock changed Base 1 {name}: {start!r} -> {ready!r}")
    for name in ("researchAssigned", "engineerAssigned", "soldiers", "craft",
                 "lab", "workshop", "quarters", "occupied"):
        if field(ready, name) != field(start, name):
            fail(f"normal Stores clock changed Base 1 {name}: {start!r} -> {ready!r}")
    initial_days = stores_days(setup, complete=False)
    expected_hours = hours_to_store_completion(field(start, "sec"), initial_days)
    if field(ready, "sec") - field(start, "sec") != expected_hours * 3601:
        fail(f"Stores clock advanced beyond or short of the measured build boundary: need {expected_hours} pulses, "
             f"elapsed {field(ready, 'sec') - field(start, 'sec')} seconds")
    if stock_rows(clock, "stores_ready"):
        fail(f"Base 1 received free stock while Stores built: {stock_rows(clock, 'stores_ready')!r}")
    stores_days(clock, complete=True)
    cargo_rows(clock, "stores_ready")
    save_written(clock, 1)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"completed Stores save is missing or empty: {saved}")
    print("ok: normal hourly inputs finished the real Base 1 Stores build")


def mode_probe(setup_path: str, log_path: str, commit_flow: str) -> None:
    setup = read_lines(setup_path)
    lines = read_lines(log_path)
    raw_base0 = tuple_for(setup, "new_game")
    expect_raw_base0_newgame(raw_base0, "transfer setup New Game Base 0")
    loaded = tuple_for(lines, "transfer_probe_loaded")
    expect_state(loaded, "transfer_probe_loaded", 0, 2, 1, sites=0)
    expect_cold_load(loaded, "transfer_probe_loaded")
    source = tuple_for(lines, "transfer_source_selected")
    expect_state(source, "transfer_source_selected", 1, 2, 0, sites=0)
    expect_same_base_metrics(raw_base0, source, "Base 0 after loading Stores and selecting transfer source")
    if stock_rows(lines, "transfer_source_selected") != START_BASE:
        fail(f"Base 0 raw stock differs before transfer: {stock_rows(lines, 'transfer_source_selected')!r}")
    transfer_open = tuple_for(lines, "transfer_open")
    expect_state(transfer_open, "transfer_open", 10, 2, 0, sites=0)
    destination = tuple_for(lines, "transfer_destination_selected")
    expect_state(destination, "transfer_destination_selected", 10, 2, 0, sites=0)
    require_destination(lines, "transfer_destination_selected")

    candidates = [row for row in transfer_rows(lines, "transfer_destination_selected") if row[1] == 52]
    if len(candidates) != 1 or candidates[0][2:] != (20, 0):
        fail(f"live transfer row is not Base 0 slot52 x20 with no pending amount: {candidates!r}")
    row, _slot, _source, _pending = candidates[0]
    count, _panel_x, panel_y, font, style, scroll = panel_view(lines, "transfer_destination_selected")
    if row < 0 or row >= count or style <= 0 or font <= 0:
        fail(f"slot52 row or panel geometry is invalid: row={row}, count/style/font={count}/{style}/{font}")
    target_scroll = max(scroll, row - style + 1)
    max_scroll = max(0, count - style)
    if target_scroll > max_scroll:
        fail(f"slot52 row {row} cannot fit on the transfer page: {count}/{style}")
    scroll_commands: list[str] = []
    if target_scroll > scroll:
        x, y, width, height = control_rect(lines, 1)
        for _ in range(target_scroll - scroll):
            scroll_commands.extend((f"click {x + width // 2} {y + height // 2}", "tick 1"))

    commit_x, commit_y, commit_w, commit_h = control_rect(lines, 3)
    row_y = panel_y + (row - target_scroll) * font + font // 2
    commands = [
        "load 1",
        "state transfer_commit_loaded",
        "strategystate transfer_commit_loaded",
        "campaign cargo transfer_commit_loaded",
        "click 180 266",
        "tick 2",
        "click 220 15",
        "tick 2",
        "strategystate transfer_source_selected",
        "click 180 274",
        "tick 20",
        "strategystate transfer_open",
        "press 220 43",
        "tick 1",
        "release",
        "tick 1",
        "strategystate transfer_destination_selected",
        *scroll_commands,
        "strategystate transfer_scrolled",
        f"click 191 {row_y}",
        "tick 1",
        "strategystate transfer_item_queued",
        f"click {commit_x + commit_w // 2} {commit_y + commit_h // 2}",
        "tick 2",
        "strategystate transfer_committed",
        "click 231 8",
        "tick 2",
        "click 201 11",
        "tick 2",
        "strategystate transfer_before_save",
        "grid 1",
        "save 0",
        "state transfer_saved",
    ]
    write_new_flow(commit_flow, "\n".join(commands) + "\n")
    print(f"ok: live slot52 row {row}/{count}, source 20, pending 0; page {scroll}->{target_scroll}")
    print(f"ok: live transfer commit control center {commit_x + commit_w // 2}/{commit_y + commit_h // 2}")


def mode_commit(store_path: str, probe_path: str, commit_path: str, save_path: str) -> None:
    store_lines = read_lines(store_path)
    probe = read_lines(probe_path)
    lines = read_lines(commit_path)
    ready = tuple_for(store_lines, "stores_ready")
    funds = field(ready, "funds")
    for loaded, label in (
        (tuple_for(probe, "transfer_probe_loaded"), "transfer probe load"),
        (tuple_for(lines, "transfer_commit_loaded"), "transfer commit load"),
    ):
        expect_cold_load(loaded, label)
        for name in ("sec", "pause", "funds", "baseCount", "selectedBase", "sites"):
            before_value = pair_field(ready, name) if "/" in name else field(ready, name)
            after_value = pair_field(loaded, name) if "/" in name else field(loaded, name)
            if before_value != after_value:
                fail(f"{label} changed saved {name}: {before_value!r} -> {after_value!r}")
        expect_same_base_metrics(ready, loaded, label)
    if cargo_rows(store_lines, "stores_ready") != cargo_rows(lines, "transfer_commit_loaded"):
        fail("fresh transfer commit load changed saved craft cargo")
    probe_source = tuple_for(probe, "transfer_source_selected")
    commit_source = tuple_for(lines, "transfer_source_selected")
    expect_same_base_metrics(probe_source, commit_source, "Base 0 across transfer source loads")

    before = tuple_for(lines, "transfer_destination_selected")
    expect_state(before, "transfer_destination_selected", 10, 2, 0, funds=funds, sites=0)
    require_destination(lines, "transfer_destination_selected")
    initial = [row for row in transfer_rows(probe, "transfer_destination_selected") if row[1] == 52]
    scrolled = [row for row in transfer_rows(lines, "transfer_scrolled") if row[1] == 52]
    queued = [row for row in transfer_rows(lines, "transfer_item_queued") if row[1] == 52]
    if len(initial) != 1 or initial[0][2:] != (20, 0):
        fail(f"probe has no unique slot52 source row: {initial!r}")
    if len(scrolled) != 1 or scrolled[0][2:] != (20, 0):
        fail(f"page movement changed the slot52 source row: {scrolled!r}")
    if len(queued) != 1 or queued[0][2:] != (19, 1):
        fail(f"the live row click did not queue one of 20 rounds: {queued!r}")
    queued_state = tuple_for(lines, "transfer_item_queued")
    expect_state(queued_state, "transfer_item_queued", 10, 2, 0, funds=funds, sites=0)
    expect_same_base_metrics(commit_source, queued_state, "Base 0 after selecting transfer item")
    require_destination(lines, "transfer_item_queued")

    committed = tuple_for(lines, "transfer_committed")
    if field(committed, "baseCount") != 2 or field(committed, "sites") != 1 \
            or field(committed, "funds") != funds:
        fail(f"commit did not make one pending site without a funds change: {committed!r}")
    if site_rows(lines, "transfer_committed") != [(0, 24)]:
        fail(f"transfer is not one flag-24 site: {site_rows(lines, 'transfer_committed')!r}")
    if site_items(lines, "transfer_committed") != {(0, 52): 1}:
        fail(f"pending site does not hold exactly one slot52 item: {site_items(lines, 'transfer_committed')!r}")

    after = tuple_for(lines, "transfer_before_save")
    expect_state(after, "transfer_before_save", 0, 2, 0, funds=funds, sites=1)
    expect_same_base_metrics(commit_source, after, "Base 0 after transfer commit")
    if stock_rows(lines, "transfer_before_save") != BASE0_AFTER_TRANSFER:
        fail(f"Base 0 stock is not exactly one item lower: {stock_rows(lines, 'transfer_before_save')!r}")
    if site_rows(lines, "transfer_before_save") != [(0, 24)] \
            or site_items(lines, "transfer_before_save") != {(0, 52): 1}:
        fail("the committed slot52 site did not survive the screen close before save")
    save_written(lines, 0)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"pending transfer save is missing or empty: {saved}")
    print("ok: live slot52 source fell from 20 to 19; one flag-24 item awaits Base 1")


def event_has_transfer_arrival(lines: list[bytes]) -> bool:
    return any(re.search(rb"^\s*\[0\] flag 41 slots ", line) for line in lines)


def check_arrival_flow(flow_path: str, lines: list[bytes]) -> None:
    flow = [line.strip() for line in Path(flow_path).read_text(encoding="utf-8").splitlines()]
    ticks = [i for i, line in enumerate(flow) if line == "tick 1"]
    ack_calls = sum(line == "campaign ack-confirm" for line in flow)
    status_calls = sum(line == "campaign ack-status 41" for line in flow)
    if len(ticks) != 24 or ack_calls != 23 or status_calls != 1:
        fail(f"arrival flow needs 24 normal hours, 23 flag-0xA acks, and one flag-41 ack: "
             f"{len(ticks)}/{ack_calls}/{status_calls}")

    speed_center = strategy_control_center(lines, "transfer_after_load", "speed", 7)
    speed_click = f"click {speed_center[0]} {speed_center[1]}"
    if ticks[0] == 0 or flow[ticks[0] - 1] != speed_click:
        fail("arrival flow must select the live one-hour speed before its first pulse")
    for hour, index in enumerate(ticks):
        terminal = hour + 1 == len(ticks)
        expected_ack = "campaign ack-status 41" if terminal else "campaign ack-confirm"
        if not terminal:
            if index + 1 >= len(flow) or flow[index + 1] != expected_ack:
                fail(f"arrival hour {hour + 1} has the wrong guarded acknowledgement")
            if index + 2 >= len(flow) or flow[index + 2] != speed_click:
                fail(f"arrival hour {hour + 1} does not reselect live one-hour speed after its ack")
        else:
            # These exact witnesses only print state. They do not pulse time or send input.
            final_tick_observers = [
                "strategystate transfer_clock_end", "lines", "grid 1",
                "campaign cargo transfer_clock_end",
            ]
            observer_start = index + 1
            observer_end = observer_start + len(final_tick_observers)
            if flow[observer_start:observer_end] != final_tick_observers \
                    or observer_end >= len(flow) or flow[observer_end] != expected_ack:
                fail("arrival final tick must be followed only by the four read-only clock witnesses, then flag 41")

    status_index = flow.index("campaign ack-status 41")
    expected_tail = [
        "campaign ack-status 41", "tick 2", "lines",
        "strategystate transfer_after_clock_source", "campaign cargo transfer_after_clock_source",
        "click 180 266", "tick 2", "click 220 43", "tick 2", "click 201 11", "tick 2",
        "strategystate transfer_arrived_base1", "grid 1", "campaign cargo transfer_arrived_base1",
        "save 1", "state transfer_arrival_saved",
    ]
    if flow[status_index:] != expected_tail:
        fail(f"arrival post-card UI sequence differs from the measured map/Base/map path: {flow[status_index:]!r}")
    expected_clicks = [speed_click] * 24 + ["click 180 266", "click 220 43", "click 201 11"]
    if [line for line in flow if line.startswith("click ")] != expected_clicks:
        fail("arrival flow changed its measured hourly or Base inspection clicks")


def mode_arrival(commit_path: str, arrival_path: str, save_path: str, flow_path: str) -> None:
    committed = read_lines(commit_path)
    lines = read_lines(arrival_path)
    saved_state = tuple_for(committed, "transfer_before_save")
    funds = field(saved_state, "funds")
    loaded = tuple_for(lines, "transfer_after_load")
    expect_state(loaded, "transfer_after_load", 0, 2, 0, funds=funds, sites=1)
    expect_cold_load(loaded, "transfer_after_load")
    for name in ("sec", "pause", "funds", "baseCount", "selectedBase", "sites"):
        before_value = field(saved_state, name)
        after_value = field(loaded, name)
        if before_value != after_value:
            fail(f"fresh load changed saved transfer {name}: {before_value!r} -> {after_value!r}")
    expect_same_base_metrics(saved_state, loaded, "Base 0 pending-transfer load")
    pending_sites = site_rows(lines, "transfer_after_load")
    if pending_sites != [(0, 24)] \
            or site_items(lines, "transfer_after_load") != {(0, 52): 1}:
        fail(f"fresh load did not retain the raw 24-hour slot52 site: {pending_sites!r}")
    if strategy_control_center(committed, "transfer_before_save", "speed", 7) \
            != strategy_control_center(lines, "transfer_after_load", "speed", 7):
        fail("fresh load moved the live one-hour control before the transfer timer")
    check_arrival_flow(flow_path, lines)
    if stock_rows(lines, "transfer_after_load") != BASE0_AFTER_TRANSFER:
        fail(f"fresh load changed Base 0 stock: {stock_rows(lines, 'transfer_after_load')!r}")

    clock_end = tuple_for(lines, "transfer_clock_end")
    expect_state(clock_end, "transfer_clock_end", 0, 2, 0, funds=funds, sites=0)
    expect_same_base_metrics(loaded, clock_end, "Base 0 during transfer clock")
    if field(clock_end, "sec") - field(loaded, "sec") != 24 * 3601 \
            or field(clock_end, "tickSec") != 3600 or field(clock_end, "pause") != 1:
        fail(f"raw site timer did not resolve on the 24th normal hourly pulse: {loaded!r} -> {clock_end!r}")
    if not event_has_transfer_arrival(lines):
        fail("normal transfer arrival card (flag 41 / raw 0x29) was not shown")
    check_ack_contract(lines, 23)
    check_status_ack(lines, 41)
    if stock_rows(lines, "transfer_clock_end") != BASE0_AFTER_TRANSFER:
        fail("Base 0 stock changed at delivery")

    source_after = tuple_for(lines, "transfer_after_clock_source")
    expect_state(source_after, "transfer_after_clock_source", 0, 2, 0, funds=funds, sites=0)
    expect_same_base_metrics(loaded, source_after, "Base 0 after flag 41 acknowledgement")
    if field(source_after, "sec") != field(clock_end, "sec") + 12 \
            or field(source_after, "pause") != 0 or field(source_after, "tickSec") != 5:
        fail(f"real flag-41 control did not clear the card at five-second speed: {clock_end!r} -> {source_after!r}")
    if last_line_flags(lines) != (0, []):
        fail(f"flag-41 delivery card was not cleared: {last_line_flags(lines)!r}")
    if stock_rows(lines, "transfer_after_clock_source") != BASE0_AFTER_TRANSFER:
        fail("Base 0 stock changed again at delivery")

    destination = tuple_for(lines, "transfer_arrived_base1")
    expect_state(destination, "transfer_arrived_base1", 0, 2, 1, funds=funds, sites=0)
    base1_before = tuple_for(committed, "transfer_commit_loaded")
    expect_same_base_metrics(base1_before, destination, "Base 1 delivery")
    if field(destination, "tickSec") != 5 or field(destination, "pause") != 0:
        fail(f"delivery inspection changed the clock state: {destination!r}")
    # Raw 0x95608 gates time on map screen 0 and 0x95618 gates on pause clear;
    # the validated flow has one two-pulse map block after ack and one after UI close.
    expected_inspection_sec = 2 * (field(source_after, "tickSec") + 1)
    if field(destination, "sec") - field(source_after, "sec") != expected_inspection_sec:
        fail(f"closing Base inspection did not advance two live map pulses: "
             f"{source_after!r} -> {destination!r}")
    if field(destination, "sec") - field(clock_end, "sec") != 24:
        fail(f"flag-41 acknowledgement and Base inspection advanced other than 24 seconds: "
             f"{clock_end!r} -> {destination!r}")
    if field(destination, "stores") != 300:
        fail(f"Base 1 has no 300-unit Stores capacity: {destination!r}")
    if stock_rows(lines, "transfer_arrived_base1") != {52: 1}:
        fail(f"Base 1 did not receive exactly one slot52 item: {stock_rows(lines, 'transfer_arrived_base1')!r}")
    stores_days(lines, complete=True)
    cargo_rows(lines, "transfer_arrived_base1")
    save_written(lines, 1)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"delivered transfer save is missing or empty: {saved}")
    print("ok: raw 24-hour timer delivered one slot52 item on its 24th clock pulse")
    print("ok: the live flag41 control cleared its line without adding another hour")


def check_final_load_flow(flow_path: str) -> None:
    flow = [line.strip() for line in Path(flow_path).read_text(encoding="utf-8").splitlines()]
    expected = [
        "load 1", "state final_loaded", "strategystate final_base1", "grid 1",
        "click 180 266", "tick 2", "click 220 15", "tick 2",
        "click 201 11", "tick 2", "strategystate final_base0",
        "campaign cargo final_load",
    ]
    if flow != expected:
        fail(f"final fresh-load UI flow changed its Base select/close timer path: {flow!r}")


def mode_final(setup_path: str, probe_path: str, arrival_path: str,
               final_path: str, flow_path: str) -> None:
    setup = read_lines(setup_path)
    probe = read_lines(probe_path)
    arrival = read_lines(arrival_path)
    final = read_lines(final_path)
    check_final_load_flow(flow_path)
    saved = tuple_for(arrival, "transfer_arrived_base1")
    funds = field(saved, "funds")
    base1 = tuple_for(final, "final_base1")
    expect_state(base1, "final_base1", 0, 2, 1, funds=funds, sites=0)
    expect_cold_load(base1, "final_base1")
    for name in ("sec", "pause", "funds", "baseCount", "selectedBase", "sites"):
        saved_value = field(saved, name)
        loaded_value = field(base1, name)
        if saved_value != loaded_value:
            fail(f"final fresh load changed saved transfer {name}: {saved_value!r} -> {loaded_value!r}")
    expect_same_base_metrics(saved, base1, "Base 1 final fresh load")
    if stock_rows(arrival, "transfer_arrived_base1") != stock_rows(final, "final_base1"):
        fail("final fresh load changed Base 1 stock")
    if field(base1, "stores") != 300 or stock_rows(final, "final_base1") != {52: 1}:
        fail(f"final Base 1 lost Stores or the delivered ammo: {base1!r} / {stock_rows(final, 'final_base1')!r}")
    probe_loaded = tuple_for(probe, "transfer_probe_loaded")
    probe_source = tuple_for(probe, "transfer_source_selected")
    expect_state(probe_loaded, "transfer probe Base selector start", 0, 2, 1, sites=0)
    expect_state(probe_source, "transfer probe Base selector result", 1, 2, 0, sites=0)
    if field(probe_loaded, "sec") != field(probe_source, "sec") \
            or field(probe_loaded, "tickSec") != 5 or field(probe_source, "tickSec") != 5 \
            or field(probe_loaded, "pause") != 0 or field(probe_source, "pause") != 0:
        fail(f"Base selector reference path changed the unpaused five-second clock: {probe_loaded!r} -> {probe_source!r}")
    base0 = tuple_for(final, "final_base0")
    expect_state(base0, "final_base0", 0, 2, 0, funds=funds, sites=0)
    expect_cold_load(base0, "final_base0")
    raw_base0 = tuple_for(setup, "new_game")
    expect_raw_base0_newgame(raw_base0, "final-load New Game Base 0")
    expect_same_base_metrics(raw_base0, base0, "Base 0 final fresh load against New Game")
    for name in ("pause", "funds", "baseCount", "sites"):
        saved_value = field(base1, name)
        loaded_value = field(base0, name)
        if saved_value != loaded_value:
            fail(f"Base selection changed saved transfer {name}: {saved_value!r} -> {loaded_value!r}")
    if field(base0, "tickSec") != 5 or field(base0, "pause") != 0 \
            or field(base0, "sec") - field(base1, "sec") != 2 * (field(base0, "tickSec") + 1):
        fail(f"Base selection time differs from its one active two-pulse map segment: {base1!r} -> {base0!r}")
    if stock_rows(final, "final_base0") != BASE0_AFTER_TRANSFER:
        fail(f"final Base 0 has the wrong slot52 amount: {stock_rows(final, 'final_base0')!r}")
    cargo_rows(final, "final_load")
    print("ok: fresh final load retained Base 0 slot52 x19 and Base 1 slot52 x1")


def mode_arrival_flow(commit_path: str, path: str) -> None:
    committed = read_lines(commit_path)
    sites = site_rows(committed, "transfer_before_save")
    if sites != [(0, 24)] or site_items(committed, "transfer_before_save") != {(0, 52): 1}:
        fail(f"arrival clock needs the measured raw site flag24: {sites!r}")
    hours = sites[0][1]
    if hours != 24:
        fail(f"raw transfer site timer changed from 24 hourly calls: {hours}")
    speed_x, speed_y = strategy_control_center(committed, "transfer_before_save", "speed", 7)
    commands = [
        "load 0",
        "strategystate transfer_after_load",
        "campaign cargo transfer_after_load",
        "grid 1",
        f"click {speed_x} {speed_y}",
    ]
    for hour in range(hours):
        commands.append("tick 1")
        if hour + 1 < hours:
            commands.extend(("campaign ack-confirm", f"click {speed_x} {speed_y}"))
        else:
            commands.extend((
                "strategystate transfer_clock_end",
                "lines",
                "grid 1",
                "campaign cargo transfer_clock_end",
                "campaign ack-status 41",
                "tick 2",
                "lines",
                "strategystate transfer_after_clock_source",
                "campaign cargo transfer_after_clock_source",
            ))
    commands.extend((
        "click 180 266",
        "tick 2",
        "click 220 43",
        "tick 2",
        "click 201 11",
        "tick 2",
        "strategystate transfer_arrived_base1",
        "grid 1",
        "campaign cargo transfer_arrived_base1",
        "save 1",
        "state transfer_arrival_saved",
    ))
    write_new_flow(path, "\n".join(commands) + "\n")


def main() -> int:
    if len(sys.argv) < 2:
        fail("usage: setup|stores-stage|stores-next|stores|probe|commit|arrival-flow|arrival|final ...")
    mode = sys.argv[1]
    if mode == "setup" and len(sys.argv) == 5:
        mode_setup(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "stores-stage" and len(sys.argv) == 6:
        mode_clock_stage(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "stores-next" and len(sys.argv) == 4:
        mode_clock_next(sys.argv[2], sys.argv[3])
    elif mode == "stores" and len(sys.argv) == 6:
        mode_stores(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "probe" and len(sys.argv) == 5:
        mode_probe(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "commit" and len(sys.argv) == 6:
        mode_commit(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "arrival-flow" and len(sys.argv) == 4:
        mode_arrival_flow(sys.argv[2], sys.argv[3])
    elif mode == "arrival" and len(sys.argv) == 6:
        mode_arrival(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "final" and len(sys.argv) == 7:
        mode_final(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
    else:
        fail(f"bad arguments for mode {mode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

