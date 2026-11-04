#!/usr/bin/env python3
"""Check and drive one raw New Game Laser-technology research route."""

from __future__ import annotations

from pathlib import Path
import math
import re
import struct
import sys


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
RAW_TOPICS = 144
RAW_TOPIC_BYTES = 34
# The strategy tuple's researchAssigned value is Base_CountActiveResearch's
# sum of the base's Scientist allocations, not a boolean project count.


def fail(message: str) -> None:
    raise SystemExit(f"RESEARCH 47 FLOW FAILED: {message}")


def read_lines(path: str) -> list[bytes]:
    return Path(path).read_bytes().splitlines()


def one_line(lines: list[bytes], prefix: bytes) -> bytes:
    found = [line for line in lines if line.startswith(prefix)]
    if len(found) != 1:
        fail(f"expected one {prefix!r} line, found {len(found)}")
    return found[0]


def save_written(lines: list[bytes], slot: int) -> None:
    line = one_line(lines, f"pocketufo: save slot {slot}:".encode())
    if b": written (" not in line:
        fail(f"save slot {slot} was not written: {line!r}")


def tuple_for(lines: list[bytes], label: str) -> bytes:
    return one_line(lines, f"pocketufo: strategy tuple {label}:".encode())


def field(line: bytes, name: str) -> int:
    match = re.search(rb"(?:^| )" + re.escape(name.encode()) + rb" (-?[0-9]+)(?: |$)", line)
    if match is None:
        fail(f"tuple has no {name} field: {line!r}")
    return int(match.group(1))


def pair_field(line: bytes, name: str) -> tuple[int, int]:
    match = re.search(rb"(?:^| )" + re.escape(name.encode()) + rb" ([0-9]+)/([0-9]+)(?: |$)", line)
    if match is None:
        fail(f"tuple has no {name} pair: {line!r}")
    return tuple(map(int, match.groups()))


def screen_pair(line: bytes) -> tuple[int, int]:
    match = re.search(rb"(?:^| )screen ([0-9]+)/([0-9]+)(?: |$)", line)
    if match is None:
        fail(f"tuple has no App/Geo screen pair: {line!r}")
    return tuple(map(int, match.groups()))


def expect_screen(line: bytes, geo: int, label: str) -> None:
    if screen_pair(line) != (22, geo):
        fail(f"{label} is not App/Geo 22/{geo}: {line!r}")


def expect_cold_load(line: bytes, label: str) -> None:
    expect_screen(line, 0, label)
    if field(line, "tickSec") != 5:
        fail(f"{label} did not use the raw fresh-load five-second speed: {line!r}")


def expect_state(line: bytes, label: str, geo: int, funds: int = START_FUNDS) -> None:
    expect_screen(line, geo, label)
    if field(line, "baseCount") != 1 or field(line, "selectedBase") != 0:
        fail(f"{label} is not Base 0 in a one-base game: {line!r}")
    if field(line, "funds") != funds or field(line, "sites") != 0:
        fail(f"{label} changed funds or sites: {line!r}")


def archive_member(path: str, tag: str) -> bytes:
    data = Path(path).read_bytes()
    if len(data) < 4:
        fail(f"archive is too small: {path}")
    count = struct.unpack_from("<I", data, 0)[0]
    if 4 + count * 21 > len(data):
        fail(f"archive directory is out of range: {path}")
    for index in range(count):
        offset = 4 + index * 21
        name = data[offset:offset + 13].split(b"\0", 1)[0].decode("ascii", "strict")
        size, start = struct.unpack_from("<II", data, offset + 13)
        if name == tag:
            if start + size > len(data):
                fail(f"{tag} payload is out of range in {path}")
            return data[start:start + size]
    fail(f"{tag} is missing from {path}")


def text_record(lang_path: str, index: int) -> str:
    blob = archive_member(lang_path, "TEXTS.DAT")
    if len(blob) < 2:
        fail("TEXTS.DAT has no two-byte header")
    stride = 200
    header_stride = struct.unpack_from("<H", blob, 0)[0]
    payload_bytes = len(blob) - 2
    # Raw Assets_LoadStartup @ 0x38508 subtracts the 2-byte marker and divides
    # by 0xC8 at 0x38510; it then reads 0xC8 bytes per record and advances the
    # destination by 0xCD. The header is the disk stride, not a record count.
    if header_stride != stride or payload_bytes % stride != 0:
        fail(f"TEXTS.DAT record span differs: header={header_stride}, bytes={len(blob)}")
    count = payload_bytes // stride
    if index < 0 or index >= count:
        fail(f"TEXTS.DAT record index is out of range: count={count}, index={index}")
    start = 2 + index * stride
    return blob[start:start + stride].split(b"\0", 1)[0].decode("cp1251")


def research_record(blob: bytes, topic: int) -> bytes:
    if len(blob) != RAW_TOPICS * RAW_TOPIC_BYTES:
        fail(f"RESEARCH.DAT size differs: {len(blob)}")
    if topic < 0 or topic >= RAW_TOPICS:
        fail(f"research topic id is out of range: {topic}")
    start = topic * RAW_TOPIC_BYTES
    return blob[start:start + RAW_TOPIC_BYTES]


def verify_raw_contract(main_path: str, lang_path: str) -> None:
    research = archive_member(main_path, "RESEARCH.DAT")
    objects = archive_member(main_path, "OBJECTS.DAT")
    if len(objects) != 149 * 208:
        fail(f"OBJECTS.DAT size differs: {len(objects)}")

    topic47 = research_record(research, 47)
    if struct.unpack_from("<H", topic47, 0)[0] != 467 \
            or struct.unpack_from("<H", topic47, 4)[0] != 3600 \
            or topic47[10:15] != bytes(5) or topic47[15:20] != bytes(5) \
            or topic47[20:25] != bytes(5) or topic47[25:30] != bytes((6, 0, 0, 0, 0)):
        fail(f"raw topic 47 record differs: {topic47.hex(' ')}")

    topic6 = research_record(research, 6)
    if struct.unpack_from("<H", topic6, 0)[0] != 426 \
            or struct.unpack_from("<H", topic6, 4)[0] != 2400 \
            or topic6[10:15] != bytes(5) \
            or topic6[15:20] != bytes((47, 0, 0, 0, 0)) \
            or topic6[20:25] != bytes((2, 0, 0, 0, 0)):
        fail(f"raw topic 6 record differs: {topic6.hex(' ')}")

    name47 = text_record(lang_path, 467)
    name6 = text_record(lang_path, 426)
    if name47 != "Лазерная технология" or name6 != "Лазерный пистолет":
        fail(f"raw research names differ: {name47!r} / {name6!r}")

    object2 = objects[2 * 208:(2 + 1) * 208]
    object2_name = object2[:36].split(b"\0", 1)[0].decode("cp1251")
    object2_work = struct.unpack_from("<H", object2, 0xBA)[0]
    object2_parts = object2[0xC0:0xC4]
    if object2_name != "Laser pistol" or object2_work != 576 or object2_parts != bytes(4):
        fail(f"raw OBJECTS.DAT slot 2 differs: {object2_name!r}, work={object2_work}, parts={object2_parts.hex()}")


def stock_rows(lines: list[bytes], label: str) -> dict[int, int]:
    prefix = f"pocketufo: strategy stock {label}:".encode()
    result: dict[int, int] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"slot ([0-9]+) qty ([0-9]+)$", line)
        if match is None:
            fail(f"bad stock row: {line!r}")
        slot, quantity = map(int, match.groups())
        if slot in result:
            fail(f"duplicate stock slot {slot} at {label}")
        result[slot] = quantity
    return result


def cargo_rows(lines: list[bytes], label: str) -> dict[int, tuple[int, int]]:
    prefix = f"pocketufo: campaign cargo item {label}:".encode()
    result: dict[int, tuple[int, int]] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"object ([0-9]+) craftIndexQty ([0-9]+) shipSlotQty ([0-9]+)$", line)
        if match is None:
            fail(f"bad cargo row: {line!r}")
        slot, craft_quantity, ship_quantity = map(int, match.groups())
        if slot in result:
            fail(f"duplicate cargo slot {slot} at {label}")
        result[slot] = (craft_quantity, ship_quantity)
    summary = one_line(lines, f"pocketufo: campaign cargo summary {label}:".encode())
    match = re.search(rb"craftObjects ([0-9]+) siteObjects ([0-9]+)$", summary)
    if match is None or tuple(map(int, match.groups())) != (20, 0):
        fail(f"raw craft cargo census differs at {label}: {summary!r}")
    expected = {slot: (quantity, quantity) for slot, quantity in START_CARGO.items()}
    if result != expected:
        fail(f"raw craft-0 cargo differs at {label}: {result!r}")
    return result


def topic_rows(lines: list[bytes], label: str) -> dict[int, tuple[int, int, int, int, int]]:
    prefix = f"pocketufo: strategy topic {label}:".encode()
    result: dict[int, tuple[int, int, int, int, int]] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(
            rb"id ([0-9]+) active ([0-9]+) unlocked ([0-9]+) progress ([0-9]+)/([0-9]+) scientists ([0-9]+)$",
            line,
        )
        if match is None:
            fail(f"bad topic row: {line!r}")
        topic, active, unlocked, progress, total, scientists = map(int, match.groups())
        if topic in result:
            fail(f"duplicate topic {topic} at {label}")
        result[topic] = (active, unlocked, progress, total, scientists)
    return result


def last_line_flags(lines: list[bytes]) -> tuple[int, list[int]]:
    headers = [i for i, line in enumerate(lines) if line.startswith(b"pocketufo: lines:")]
    if not headers:
        fail("log has no read-only lines snapshot")
    start = headers[-1]
    match = re.search(rb"pocketufo: lines: ([0-9]+) of [0-9]+(?:,|$)", lines[start])
    if match is None:
        fail(f"bad lines header: {lines[start]!r}")
    count = int(match.group(1))
    flags: list[int] = []
    for line in lines[start + 1:]:
        if line.startswith(b"pocketufo:"):
            break
        row = re.search(rb"^\s*\[[0-9]+\] flag (-?[0-9]+) slots ", line)
        if row is not None:
            flags.append(int(row.group(1)))
    if len(flags) != count:
        fail(f"lines snapshot count differs: header={count}, rows={flags!r}")
    return count, flags


def research_panel(lines: list[bytes], label: str) -> tuple[int, int, int, int]:
    line = one_line(lines, f"pocketufo: strategy panel {label}:".encode())
    match = re.search(rb"research rows ([0-9]+) pos (-?[0-9]+)/(-?[0-9]+) font ([0-9]+)$", line)
    if match is None:
        fail(f"research panel geometry is incomplete: {line!r}")
    return tuple(map(int, match.groups()))


def research_visible_rows(lines: list[bytes], label: str) -> dict[int, int]:
    prefix = f"pocketufo: strategy row {label}: research ".encode()
    result: dict[int, int] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"research ([0-9]+) aux ([0-9]+)$", line)
        if match is None:
            fail(f"bad Research row: {line!r}")
        row, topic = map(int, match.groups())
        if row in result:
            fail(f"duplicate visible Research row {row} at {label}")
        result[row] = topic
    return result


def strategy_snapshot_lines(lines: list[bytes], label: str) -> list[bytes]:
    snapshot = tuple_for(lines, label)
    start = lines.index(snapshot)
    # Read-only commands such as cargo or grid can separate strategy snapshots
    # without a `step` record, so the next tuple is also a hard boundary.
    end = next((i for i in range(start + 1, len(lines))
                if lines[i].startswith(b"pocketufo: strategy tuple ")
                or lines[i].startswith(b"pocketufo: step ")), len(lines))
    return lines[start:end]


def control_rect(lines: list[bytes], label: str, group: str,
                 index: int) -> tuple[int, int, int, int]:
    snapshot = strategy_snapshot_lines(lines, label)
    prefix = f"pocketufo: strategy control {group} {index} ".encode()
    found = [line for line in snapshot if line.startswith(prefix)]
    if len(found) != 1:
        fail(f"expected one {group} control {index} at {label}: {found!r}")
    line = found[0]
    match = re.search(rb"present 1 enabled 1 rect (-?[0-9]+)/(-?[0-9]+)/([0-9]+)/([0-9]+) .*callback 1$", line)
    if match is None:
        fail(f"{group} control {index} is not live, enabled, and connected at {label}: {line!r}")
    return tuple(map(int, match.groups()))


def control_center_in_snapshot(lines: list[bytes], label: str, group: str, index: int) -> tuple[int, int]:
    x, y, width, height = control_rect(lines, label, group, index)
    return x + width // 2, y + height // 2


def start_control_center(lines: list[bytes], label: str) -> tuple[int, int]:
    matches: list[tuple[int, int]] = []
    for line in strategy_snapshot_lines(lines, label):
        if not line.startswith(b"pocketufo: strategy control geo "):
            continue
        match = re.search(
            rb"geo ([0-9]+) present 1 enabled 1 rect (-?[0-9]+)/(-?[0-9]+)/([0-9]+)/([0-9]+) .*callback 1$",
            line,
        )
        if match is None:
            continue
        _index, x, y, width, height = map(int, match.groups())
        if x <= 200 < x + width and y <= 176 < y + height:
            matches.append((x + width // 2, y + height // 2))
    if len(matches) != 1:
        fail(f"the live Research Start control at 200/176 is not unique: {matches!r}")
    return matches[0]


def check_newgame(lines: list[bytes], label: str, main_path: str, lang_path: str) -> bytes:
    verify_raw_contract(main_path, lang_path)
    state = tuple_for(lines, label)
    expect_screen(state, 1, f"{label} after pick")
    if field(state, "baseCount") != 1 or field(state, "selectedBase") != 0 \
            or field(state, "funds") != START_FUNDS or field(state, "sites") != 0:
        fail(f"raw New Game tuple differs: {state!r}")
    if pair_field(state, "engineersAvailable/Total") != (10, 10) \
            or pair_field(state, "scientistsAvailable/Total") != (10, 10) \
            or field(state, "researchAssigned") != 0 or field(state, "engineerAssigned") != 0 \
            or field(state, "lab") != 50:
        fail(f"raw New Game staff, lab, or assignment state differs: {state!r}")
    if stock_rows(lines, label) != START_BASE:
        fail(f"raw Base 0 inventory differs: {stock_rows(lines, label)!r}")
    cargo_rows(lines, label)
    topics = topic_rows(lines, label)
    if topics.get(47) != (1, 0, 0, 3600, 0) or 6 in topics:
        fail(f"raw Laser technology start/prerequisite state differs: {topics.get(47)!r} / {topics.get(6)!r}")
    return state


def write_new_flow(path: str, text: str) -> None:
    target = Path(path)
    if target.exists():
        fail(f"generated flow already exists: {target}")
    size = len(text.encode("utf-8"))
    if size >= 7900:
        fail(f"generated flow exceeds app script input: {size} bytes")
    target.write_text(text, encoding="utf-8", newline="\n")


def mode_probe(log_path: str, template_path: str, output_path: str, main_path: str, lang_path: str) -> None:
    lines = read_lines(log_path)
    check_newgame(lines, "new_game", main_path, lang_path)
    opened = tuple_for(lines, "research_probe")
    expect_screen(opened, 8, "research_probe")
    if field(opened, "baseCount") != 1 or field(opened, "selectedBase") != 0 \
            or field(opened, "funds") != START_FUNDS or field(opened, "researchAssigned") != 0 \
            or field(opened, "engineerAssigned") != 0:
        fail(f"Research probe is not the raw Base 0 state: {opened!r}")
    topics = topic_rows(lines, "research_probe")
    if topics.get(47) != (1, 0, 0, 3600, 0) or 6 in topics:
        fail(f"topic47 is not the active raw project before input: {topics.get(47)!r} / {topics.get(6)!r}")

    visible = research_visible_rows(lines, "research_probe")
    matching = [row for row, topic in visible.items() if topic == 47]
    if len(matching) != 1:
        fail(f"the live Research list has no unique topic47 row: {visible!r}")
    row = matching[0]
    count, panel_x, panel_y, font = research_panel(lines, "research_probe")
    if row >= 14 or row >= count or font <= 0:
        fail(f"topic47 is not on the first visible Research page: row={row}, count={count}")
    row_x = panel_x + 5
    row_y = panel_y + row * font + font // 2

    inc_x, inc_y, inc_w, inc_h = control_rect(lines, "research_probe", "research", 2)
    plus_x, plus_y = inc_x + inc_w // 2, inc_y + inc_h // 2
    start_x, start_y = start_control_center(lines, "research_probe")
    template = Path(template_path).read_text(encoding="utf-8")
    template = template.replace("@TOPIC47_ROW_X@", str(row_x))
    template = template.replace("@TOPIC47_ROW_Y@", str(row_y))
    template = template.replace(
        "__TOPIC47_SCIENTIST_CLICKS__",
        "\n".join(f"click {plus_x} {plus_y}" for _ in range(10)),
    )
    template = template.replace("@TOPIC47_START_X@", str(start_x))
    template = template.replace("@TOPIC47_START_Y@", str(start_y))
    if "@TOPIC47_" in template or "__TOPIC47_" in template:
        fail("generated Research flow has a placeholder")
    write_new_flow(output_path, template)
    print(f"ok: raw topic47 row {row}; ten real +1 clicks at {plus_x}/{plus_y}; Start at {start_x}/{start_y}")


def mode_start(start_path: str, save_path: str, main_path: str, lang_path: str) -> None:
    lines = read_lines(start_path)
    check_newgame(lines, "new_game", main_path, lang_path)
    opened = tuple_for(lines, "research_open")
    expect_state(opened, "research_open", 8)
    if field(opened, "researchAssigned") != 0 or field(opened, "engineerAssigned") != 0:
        fail(f"opening Research changed Base 0 assignments: {opened!r}")
    visible = research_visible_rows(lines, "research_open")
    if sum(topic == 47 for topic in visible.values()) != 1:
        fail(f"topic47 is not the unique selected Research row: {visible!r}")
    started = tuple_for(lines, "research_started")
    expect_state(started, "research_started", 8)
    if field(started, "funds") != START_FUNDS or field(started, "sites") != 0 \
            or field(started, "researchAssigned") != 10 or field(started, "engineerAssigned") != 0:
        fail(f"the Start input changed funds/sites or missed the topic assignment: {started!r}")
    if pair_field(started, "scientistsAvailable/Total") != (0, 10) \
            or pair_field(started, "engineersAvailable/Total") != (10, 10):
        fail(f"the ten raw scientists were not assigned to this one topic: {started!r}")
    topics = topic_rows(lines, "research_started")
    if topics.get(47) != (1, 0, 1, 3600, 10):
        fail(f"Start did not create one real topic47 work record: {topics.get(47)!r}")
    if any(topic != 47 and row[4] != 0 for topic, row in topics.items()):
        fail(f"scientists were assigned to an unexpected topic: {topics!r}")
    if stock_rows(lines, "research_started") != START_BASE:
        fail("starting Research changed Base stock")
    cargo_rows(lines, "new_game")
    on_map = tuple_for(lines, "research_on_map")
    expect_state(on_map, "research_on_map", 0)
    if field(on_map, "funds") != START_FUNDS or field(on_map, "researchAssigned") != 10 \
            or field(on_map, "engineerAssigned") != 0:
        fail(f"closing the Research screen changed the running work: {on_map!r}")
    save_written(lines, 0)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"ongoing Research save is missing or empty: {saved}")
    print("ok: ten existing Base 0 Scientists started raw topic47; no purchase or stock write")


def clock_flow_text(load_slot: int, hours: int, initial: bool,
                    speed_center: tuple[int, int], final_stage: bool) -> str:
    speed_x, speed_y = speed_center
    commands = [
        f"# final-topic-pulse {1 if final_stage else 0}",
        f"load {load_slot}",
        "strategystate research_clock_loaded",
        "campaign cargo research_clock_loaded",
    ]
    if initial:
        commands.extend((
            "strategystate research_loaded",
            "campaign cargo research_loaded",
        ))
    commands.append(f"click {speed_x} {speed_y}")
    for hour in range(hours):
        commands.append("tick 1")
        if final_stage and hour + 1 == hours:
            commands.extend(("campaign ack-status 37", f"click {speed_x} {speed_y}"))
        else:
            commands.extend(("campaign ack-confirm", f"click {speed_x} {speed_y}"))
    commands.extend((
        "strategystate research_clock_end",
        "lines",
        "campaign cargo research_clock_end",
        "save 1",
        "state research_clock_saved",
    ))
    return "\n".join(commands) + "\n"


def check_hourly_speed_inputs(flow: list[str], hours: int,
                              speed_center: tuple[int, int], terminal_status: str | None) -> None:
    click = f"click {speed_center[0]} {speed_center[1]}"
    ticks = [i for i, line in enumerate(flow) if line.strip() == "tick 1"]
    if len(ticks) != hours or not ticks or ticks[0] == 0 or flow[ticks[0] - 1].strip() != click:
        fail("clock flow must select the live one-hour control after load and before its first pulse")
    for hour, index in enumerate(ticks):
        expected_ack = terminal_status if terminal_status is not None and hour + 1 == hours else "campaign ack-confirm"
        if index + 2 >= len(flow) or flow[index + 1].strip() != expected_ack \
                or flow[index + 2].strip() != click:
            fail(f"clock pulse {hour + 1} must use its guarded ack then reselect live one-hour speed")


def mode_clock_start(start_path: str, output_path: str, main_path: str, lang_path: str) -> None:
    lines = read_lines(start_path)
    verify_raw_contract(main_path, lang_path)
    state = tuple_for(lines, "research_started")
    expect_state(state, "research_started", 8)
    topic = topic_rows(lines, "research_started").get(47)
    if topic != (1, 0, 1, 3600, 10):
        fail(f"cannot time an unmeasured topic47 job: {topic!r}")
    if field(state, "researchAssigned") != 10 or field(state, "engineerAssigned") != 0 \
            or field(state, "funds") != START_FUNDS:
        fail(f"topic47 is not the one paid-free raw starting research job: {state!r}")
    remaining_hours = math.ceil((topic[3] - topic[2]) / topic[4])
    hours = min(120, remaining_hours)
    speed_center = control_center_in_snapshot(lines, "research_on_map", "speed", 7)
    write_new_flow(output_path, clock_flow_text(
        0, hours, initial=True, speed_center=speed_center,
        final_stage=remaining_hours <= hours,
    ))
    print(f"ok: first fresh-load clock stage uses {hours} actual one-hour input pulses")


def check_ack_contract(lines: list[bytes], hours: int) -> int:
    prefix = b"pocketufo: campaign ack-confirm:"
    events = [line for line in lines if line.startswith(prefix)]
    failures = [line for line in events if b": FAIL" in line]
    if failures:
        fail(f"guarded acknowledgement rejected the live state: {failures!r}")
    skips = [line for line in events if b": SKIP no_flag10" in line]
    before = [line for line in events if b": before flag=10 " in line]
    after = [line for line in events if b": after lineCount=0 " in line]
    passed = [line for line in events if line.endswith(b": PASS")]
    if len(skips) + len(before) != hours:
        fail(f"ack helper ran {len(skips) + len(before)} times for {hours} clock pulses: {events!r}")
    if len(before) != len(after) or len(passed) != hours:
        fail(f"ack helper did not log one full line-clear result per alert: {events!r}")
    before_re = re.compile(
        rb"before flag=10 lineCount=1 pause=1 selection=0 .*confirmOwner=1 enabled=1 "
        rb"rect=[0-9]+/[0-9]+/[0-9]+/[0-9]+ .*callbackMatch=1 tickSec=3600$"
    )
    skip_re = re.compile(rb"SKIP no_flag10 screen=0 pause=0 selection=0$")
    after_re = re.compile(
        rb"after lineCount=0 firstFlag=-1 pause=0 selection=0 .*tickSec=5 result=PASS$"
    )
    if any(skip_re.search(line) is None for line in skips):
        fail(f"ack helper skipped while the expected clear state was not live: {skips!r}")
    if any(before_re.search(line) is None for line in before):
        fail(f"ack helper accepted a state outside the raw flag-0xA guard: {before!r}")
    if any(after_re.search(line) is None for line in after):
        fail(f"ack helper did not clear line 0 and unpause through the live control: {after!r}")
    return len(before)


def check_status37_contract(lines: list[bytes], calls: int) -> None:
    prefix = b"pocketufo: campaign ack-status:"
    events = [line for line in lines if line.startswith(prefix)]
    failures = [line for line in events if b": FAIL" in line]
    if failures:
        fail(f"guarded flag-37 acknowledgement rejected the live state: {failures!r}")
    before = [line for line in events if b" before expected=37 " in line]
    after = [line for line in events if b" after expected=37 " in line]
    if calls == 0:
        if events:
            fail(f"unexpected status acknowledgement in a non-final clock stage: {events!r}")
        return
    if calls != 1 or len(before) != 1 or len(after) != 1:
        fail(f"expected one same-process flag-37 status acknowledgement: {events!r}")
    before_re = re.compile(
        rb"before expected=37 flag=37 lineCount=1 pause=1 selection=0 screen=0 geo=1 tact=0 "
        rb"tactOwner=[0-9-]+ modal=0 control=GEOUI_CTL_187024 owner=1 enabled=1 "
        rb"rect=[0-9]+/[0-9]+/[0-9]+/[0-9]+ center=[0-9]+/[0-9]+ callbackMatch=1 tickSec=3600$"
    )
    after_re = re.compile(
        rb"after expected=37 lineCount=0 firstFlag=-1 pause=0 selection=0 screen=0 geo=1 "
        rb"tact=0 modal=0 tickSec=5 result=PASS$"
    )
    if before_re.search(before[0]) is None:
        fail(f"flag-37 status helper accepted an unproved control state: {before!r}")
    if after_re.search(after[0]) is None:
        fail(f"flag-37 status helper did not clear the live card: {after!r}")


def mode_clock_stage(previous_path: str, current_path: str, flow_path: str,
                     save_path: str, main_path: str, lang_path: str) -> None:
    previous_lines = read_lines(previous_path)
    current = read_lines(current_path)
    verify_raw_contract(main_path, lang_path)
    flow = Path(flow_path).read_text(encoding="utf-8").splitlines()
    hours = sum(line.strip() == "tick 1" for line in flow)
    if hours < 1 or hours > 120:
        fail(f"clock stage has an invalid normal pulse count: {hours}")
    final_stage = "# final-topic-pulse 1" in flow
    ack_calls = sum(line.strip() == "campaign ack-confirm" for line in flow)
    status_calls = sum(line.strip() == "campaign ack-status 37" for line in flow)
    expected_ack_calls = hours - (1 if final_stage else 0)
    if ack_calls != expected_ack_calls:
        fail(f"clock stage has {ack_calls} acknowledgement inputs for {hours} pulses; expected {expected_ack_calls}")
    expected_status_calls = 1 if final_stage else 0
    if status_calls != expected_status_calls:
        fail(f"clock stage has {status_calls} flag-37 status inputs; expected {expected_status_calls}")
    if final_stage:
        status_index = flow.index("campaign ack-status 37")
        if status_index == 0 or flow[status_index - 1].strip() != "tick 1" \
                or status_index + 1 >= len(flow) or not flow[status_index + 1].strip().startswith("click "):
            fail("flag-37 status input must follow the final pulse and precede live speed reselection")

    match = re.fullmatch(r"clock-([0-9]{2})", Path(flow_path).stem)
    if match is None:
        fail(f"clock flow name does not carry a two-digit stage number: {flow_path}")
    stage = int(match.group(1))
    previous_label = "research_on_map" if stage == 0 else "research_clock_end"
    previous_state = tuple_for(previous_lines, previous_label)
    expect_state(previous_state, previous_label, 0)
    loaded = tuple_for(current, "research_clock_loaded")
    ended = tuple_for(current, "research_clock_end")
    expect_state(loaded, "research_clock_loaded", 0)
    expect_cold_load(loaded, "research_clock_loaded")
    expect_state(ended, "research_clock_end", 0)
    if field(loaded, "researchAssigned") != 10 or field(loaded, "engineerAssigned") != 0:
        fail(f"clock load lost the ten Scientists assigned to Base 0: {loaded!r}")

    speed_center = control_center_in_snapshot(current, "research_clock_loaded", "speed", 7)
    check_hourly_speed_inputs(
        flow, hours, speed_center,
        "campaign ack-status 37" if final_stage else None,
    )

    retained_fields = (
        "baseCount", "selectedBase", "funds", "sec", "pause", "sites", "researchAssigned",
        "engineerAssigned", "engineersAvailable/Total", "scientistsAvailable/Total",
    )
    for name in retained_fields:
        if name == "engineersAvailable/Total" or name == "scientistsAvailable/Total":
            before_value = pair_field(previous_state, name)
            after_value = pair_field(loaded, name)
        else:
            before_value = field(previous_state, name)
            after_value = field(loaded, name)
        if before_value != after_value:
            fail(f"fresh load changed {name} before clock stage {stage}: {before_value!r} -> {after_value!r}")
    prev_stock_label = "research_started" if stage == 0 else "research_clock_end"
    if stock_rows(previous_lines, prev_stock_label) != stock_rows(current, "research_clock_loaded"):
        fail(f"fresh load changed raw Base stock before clock stage {stage}")
    prev_topic_label = "research_started" if stage == 0 else "research_clock_end"
    previous_topics = topic_rows(previous_lines, prev_topic_label)
    loaded_topics = topic_rows(current, "research_clock_loaded")
    if previous_topics != loaded_topics:
        fail(f"fresh load changed Research topics before clock stage {stage}")
    if stage == 0:
        cargo_rows(previous_lines, "new_game")
        cargo_rows(current, "research_loaded")
    else:
        cargo_rows(previous_lines, "research_clock_end")
        cargo_rows(current, "research_clock_loaded")

    initial = loaded_topics.get(47)
    final = topic_rows(current, "research_clock_end").get(47)
    if initial is None or final is None or initial[4] <= 0:
        fail(f"clock stage lost the assigned topic47 project: {initial!r} -> {final!r}")
    ack_count = check_ack_contract(current, ack_calls)
    check_status37_contract(current, status_calls)
    elapsed = field(ended, "sec") - field(loaded, "sec")
    if elapsed != hours * 3601:
        fail(f"stage {stage} elapsed {elapsed} seconds for {hours} normal one-hour pulses; expected {hours * 3601}")
    if field(ended, "tickSec") != 3600:
        fail(f"stage {stage} did not leave the clock unpaused at one-hour speed: {ended!r}")

    elapsed_hours = elapsed // 3600
    expected_progress = min(initial[3], initial[2] + elapsed_hours * initial[4])
    if final[2] < expected_progress:
        fail(f"stage {stage} topic47 work is below elapsed clock time: expected at least {expected_progress}, got {final[2]}")
    if final[1] == 0 and (final[4] != initial[4] or final[2] >= final[3]):
        fail(f"stage {stage} changed topic47 assignment before completion: {initial!r} -> {final!r}")

    line_count, line_flags = last_line_flags(current)
    if final_stage:
        if field(ended, "pause") != 0 or line_count != 0 or line_flags:
            fail(f"final status handling left a message or pause: flags={line_flags!r}, state={ended!r}")
        if final[1] != 1 or final[2] < final[3] or final[4] != 0:
            fail(f"final Research pulse did not complete topic47 and release its Scientists: {final!r}")
        if field(ended, "researchAssigned") != 0 or field(ended, "engineerAssigned") != 0:
            fail(f"final Research pulse left an assignment count: {ended!r}")
        if topic_rows(current, "research_clock_end").get(6) != (1, 0, 0, 2400, 0):
            fail("final Research pulse did not activate topic6")
    elif field(ended, "pause") != 0 or line_count != 0 \
            or field(ended, "researchAssigned") != 10 or field(ended, "engineerAssigned") != 0:
        fail(f"non-final Research stage changed the ten-Scientist assignment or left a message: "
             f"flags={line_flags!r}, state={ended!r}")

    if stock_rows(current, "research_clock_end") != START_BASE:
        fail(f"stage {stage} changed raw Base stock")
    cargo_rows(current, "research_clock_end")
    save_written(current, 1)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"clock stage {stage} save is missing or empty: {saved}")
    status_note = "; cleared flag 37 in-process" if final_stage else ""
    print(f"ok: clock stage {stage} advanced {elapsed_hours} actual hours; acknowledged {ack_count} raw flag-0xA alerts{status_note}")


def mode_clock_next(stage_path: str, output_path: str) -> None:
    lines = read_lines(stage_path)
    state = tuple_for(lines, "research_clock_end")
    expect_state(state, "research_clock_end", 0)
    if field(state, "pause") != 0 or field(state, "tickSec") != 3600:
        fail(f"cannot continue from a paused clock or wrong speed: {state!r}")
    topics = topic_rows(lines, "research_clock_end")
    topic = topics.get(47)
    topic6 = topics.get(6)
    if topic is None:
        fail("topic47 disappeared from the live Research table")
    if topic[1] == 1:
        if topic[2] < topic[3] or topic[4] != 0 or topic6 != (1, 0, 0, 2400, 0):
            fail(f"topic47 unlock did not activate raw topic6: {topic!r} / {topic6!r}")
        if field(state, "researchAssigned") != 0 or field(state, "engineerAssigned") != 0:
            fail(f"completed topic47 retained an assignment count: {state!r}")
        if Path(output_path).exists():
            fail(f"unexpected pre-existing next clock flow: {output_path}")
        print("ok: research is complete; no more clock flow is needed")
        return
    if topic[4] <= 0 or topic[1] != 0 or topic[2] >= topic[3]:
        fail(f"topic47 is neither running nor complete: {topic!r}")
    if field(state, "researchAssigned") != 10 or field(state, "engineerAssigned") != 0:
        fail(f"active topic47 has the wrong Base 0 assignment totals: {state!r}")
    match = re.fullmatch(r"clock-([0-9]{2})", Path(output_path).stem)
    if match is None:
        fail(f"next clock flow name does not carry a two-digit stage number: {output_path}")
    stage = int(match.group(1))
    if stage >= 6:
        fail(f"topic47 remained incomplete after {stage} saved clock stages")
    remaining_hours = math.ceil((topic[3] - topic[2]) / topic[4])
    hours = min(120, remaining_hours)
    speed_center = control_center_in_snapshot(lines, "research_clock_end", "speed", 7)
    write_new_flow(output_path, clock_flow_text(
        1, hours, initial=False, speed_center=speed_center,
        final_stage=remaining_hours <= hours,
    ))
    print(f"ok: live topic47 needs {hours} more one-hour pulses; wrote stage {stage}")


def mode_complete(start_path: str, first_clock_path: str, last_clock_path: str,
                  final_path: str, save_path: str, main_path: str, lang_path: str) -> None:
    start_lines = read_lines(start_path)
    first_clock = read_lines(first_clock_path)
    last_clock = read_lines(last_clock_path)
    final_lines = read_lines(final_path)
    verify_raw_contract(main_path, lang_path)
    saved_start = tuple_for(start_lines, "research_on_map")
    expect_state(saved_start, "research_on_map", 0)
    loaded = tuple_for(first_clock, "research_loaded")
    expect_state(loaded, "research_loaded", 0)
    expect_cold_load(loaded, "research_loaded")
    if field(loaded, "funds") != START_FUNDS or field(loaded, "sites") != 0 \
            or field(loaded, "researchAssigned") != 10 or field(loaded, "engineerAssigned") != 0:
        fail(f"fresh load lost the ongoing Research state: {loaded!r}")
    for name in ("baseCount", "selectedBase", "funds", "sec", "pause", "sites",
                 "researchAssigned", "engineerAssigned", "engineersAvailable/Total",
                 "scientistsAvailable/Total"):
        before_value = pair_field(saved_start, name) if "/" in name else field(saved_start, name)
        after_value = pair_field(loaded, name) if "/" in name else field(loaded, name)
        if before_value != after_value:
            fail(f"fresh load changed saved ongoing Research {name}: {before_value!r} -> {after_value!r}")
    if stock_rows(first_clock, "research_loaded") != stock_rows(start_lines, "research_on_map") \
            or topic_rows(first_clock, "research_loaded") != topic_rows(start_lines, "research_on_map"):
        fail("fresh load changed saved Base stock or Research topics")
    saved_end = tuple_for(last_clock, "research_clock_end")
    expect_state(saved_end, "research_clock_end", 0)
    completed = tuple_for(final_lines, "research_completed")
    expect_state(completed, "research_completed", 0)
    expect_cold_load(completed, "research_completed")
    if field(completed, "funds") != START_FUNDS or field(completed, "sites") != 0 \
            or field(completed, "researchAssigned") != 0 or field(completed, "engineerAssigned") != 0 \
            or field(completed, "pause") != 0:
        fail(f"completion changed funds/sites or left work assigned: {completed!r}")
    for name in ("baseCount", "selectedBase", "funds", "sec", "pause", "sites",
                 "researchAssigned", "engineerAssigned", "engineersAvailable/Total",
                 "scientistsAvailable/Total"):
        before_value = pair_field(saved_end, name) if "/" in name else field(saved_end, name)
        after_value = pair_field(completed, name) if "/" in name else field(completed, name)
        if before_value != after_value:
            fail(f"fresh load changed completed Research {name}: {before_value!r} -> {after_value!r}")
    if field(saved_end, "sec") <= field(loaded, "sec"):
        fail(f"normal clock did not advance: {loaded!r} -> {saved_end!r}")
    if field(saved_end, "pause") != 0 or field(saved_end, "tickSec") != 3600:
        fail(f"normal clock or one-hour speed was not restored after alert handling: {saved_end!r}")
    if field(saved_end, "researchAssigned") != 0 or field(saved_end, "engineerAssigned") != 0:
        fail(f"completed Research retained an assignment count: {saved_end!r}")
    if pair_field(completed, "engineersAvailable/Total") != (10, 10) \
            or pair_field(completed, "scientistsAvailable/Total") != (10, 10):
        fail(f"completed research did not release the ten Scientists: {completed!r}")
    initial_topic = topic_rows(first_clock, "research_loaded").get(47)
    if initial_topic is None or initial_topic[4] <= 0:
        fail(f"the loaded Research project has no assigned Scientists: {initial_topic!r}")
    required_hours = math.ceil((initial_topic[3] - initial_topic[2]) / initial_topic[4])
    elapsed = field(saved_end, "sec") - field(loaded, "sec")
    if elapsed < required_hours * 3600:
        fail(f"actual clock time is too short for the loaded Research work: need {required_hours} hours, elapsed {elapsed} seconds")
    topics = topic_rows(final_lines, "research_completed")
    if topics != topic_rows(last_clock, "research_clock_end"):
        fail("fresh load changed the completed Research rows")
    topic47 = topics.get(47)
    topic6 = topics.get(6)
    if topic47 is None or topic47[1] != 1 or topic47[2] < 3600 or topic47[4] != 0:
        fail(f"topic47 did not finish and unlock through the clock: {topic47!r}")
    if topic6 != (1, 0, 0, 2400, 0):
        fail(f"raw activation link did not expose topic6 after topic47: {topic6!r}")
    if stock_rows(final_lines, "research_completed") != START_BASE \
            or stock_rows(last_clock, "research_clock_end") != START_BASE:
        fail("research changed Base stock")
    cargo_rows(final_lines, "research_completed")
    cargo_rows(last_clock, "research_clock_end")
    save_written(last_clock, 1)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"completed Research save is missing or empty: {saved}")
    print("ok: normal clock finished topic47, unlocked it, and activated topic6")
    print("ok: funds, raw Base stock, and craft cargo stayed unchanged")


def check_final_ui_flow(flow_path: str, lines: list[bytes]) -> None:
    flow = [
        line.strip()
        for line in Path(flow_path).read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    expected = [
        "load 1", "state final_load", "strategystate research_completed",
        "campaign cargo research_completed", "strategystate research_final_map",
        "campaign cargo final_load", "click 202 267", "tick 20",
        "strategystate research_final_base_strip", "click 180 238", "tick 20",
        "strategystate research_final_open", "lines",
    ]
    if flow != expected:
        fail(f"final UI flow is not the Base-root -> Research-nav path: {flow!r}")
    x, y, width, height = control_rect(lines, "research_final_base_strip", "nav", 5)
    if not (x <= 180 < x + width and y <= 238 < y + height):
        fail(f"Research nav slot 5 is not hit by the final flow click: rect={x}/{y}/{width}/{height}")


def mode_final(final_path: str, flow_path: str, main_path: str, lang_path: str) -> None:
    lines = read_lines(final_path)
    verify_raw_contract(main_path, lang_path)
    check_final_ui_flow(flow_path, lines)
    completed = tuple_for(lines, "research_completed")
    expect_state(completed, "research_completed", 0)
    expect_cold_load(completed, "research_completed")
    final_map = tuple_for(lines, "research_final_map")
    expect_state(final_map, "research_final_map", 0)
    expect_cold_load(final_map, "research_final_map")
    if field(final_map, "funds") != START_FUNDS or field(final_map, "sites") != 0 \
            or field(final_map, "researchAssigned") != 0 or field(final_map, "engineerAssigned") != 0 \
            or field(final_map, "pause") != 0:
        fail(f"final fresh load changed campaign totals: {final_map!r}")
    if pair_field(final_map, "scientistsAvailable/Total") != (10, 10) \
            or pair_field(final_map, "engineersAvailable/Total") != (10, 10):
        fail(f"final load lost the raw staff totals: {final_map!r}")
    if stock_rows(lines, "research_final_map") != START_BASE:
        fail(f"final load changed Base stock: {stock_rows(lines, 'research_final_map')!r}")
    topic_rows_final = topic_rows(lines, "research_final_map")
    if topic_rows_final.get(47) is None or topic_rows_final[47][1] != 1 \
            or topic_rows_final.get(6) != (1, 0, 0, 2400, 0):
        fail(f"final load lost the topic unlock chain: {topic_rows_final!r}")
    phase_fields = (
        "baseCount", "selectedBase", "funds", "sec", "tickSec", "pause", "sites",
        "researchAssigned", "engineerAssigned", "engineersAvailable/Total", "scientistsAvailable/Total",
        "soldiers", "craft", "lab", "workshop", "stores", "quarters", "occupied",
    )
    for name in phase_fields:
        before_value = pair_field(completed, name) if "/" in name else field(completed, name)
        after_value = pair_field(final_map, name) if "/" in name else field(final_map, name)
        if before_value != after_value:
            fail(f"map observation changed loaded {name}: {completed!r} -> {final_map!r}")
    if stock_rows(lines, "research_completed") != stock_rows(lines, "research_final_map") \
            or topic_rows(lines, "research_completed") != topic_rows_final:
        fail("map observation changed loaded Base stock or Research rows")
    if cargo_rows(lines, "research_completed") != cargo_rows(lines, "final_load"):
        fail("map observation changed loaded craft cargo")
    for label, geo in (("research_final_base_strip", 1), ("research_final_open", 8)):
        state = tuple_for(lines, label)
        expect_state(state, label, geo)
        for name in phase_fields:
            before_value = pair_field(final_map, name) if "/" in name else field(final_map, name)
            after_value = pair_field(state, name) if "/" in name else field(state, name)
            if before_value != after_value:
                fail(f"opening the Research UI changed {name}: {final_map!r} -> {state!r}")
        if stock_rows(lines, label) != stock_rows(lines, "research_final_map") \
                or topic_rows(lines, label) != topic_rows_final:
            fail(f"opening {label} changed saved Base stock or topic rows")
    if last_line_flags(lines) != (0, []):
        fail(f"final Research UI left a status line: {last_line_flags(lines)!r}")
    opened = tuple_for(lines, "research_final_open")
    visible = research_visible_rows(lines, "research_final_open")
    if sum(topic == 6 for topic in visible.values()) != 1:
        fail(f"Laser pistol project is not on the live Research list: {visible!r}")
    cargo_rows(lines, "final_load")
    print("ok: fresh load kept topic47 unlocked and the Laser pistol project visible")
    print("ok: ten Scientists, Base stock, funds, and craft cargo are unchanged")


def main() -> int:
    if len(sys.argv) < 2:
        fail("usage: probe|start|clock-start|clock-stage|clock-next|complete|final ...")
    mode = sys.argv[1]
    if mode == "probe" and len(sys.argv) == 7:
        mode_probe(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
    elif mode == "start" and len(sys.argv) == 6:
        mode_start(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "clock-start" and len(sys.argv) == 6:
        mode_clock_start(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "clock-stage" and len(sys.argv) == 8:
        mode_clock_stage(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6], sys.argv[7])
    elif mode == "clock-next" and len(sys.argv) == 4:
        mode_clock_next(sys.argv[2], sys.argv[3])
    elif mode == "complete" and len(sys.argv) == 9:
        mode_complete(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6], sys.argv[7], sys.argv[8])
    elif mode == "final" and len(sys.argv) == 6:
        mode_final(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    else:
        fail(f"bad arguments for mode {mode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

