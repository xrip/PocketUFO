#!/usr/bin/env python3
"""Drive and check one raw New Game topic-14 -> Motion Scanner order."""

from __future__ import annotations

from pathlib import Path
import math
import re
import struct
import sys

# Pure log and archive parsers only. The imported checker has a __main__ guard,
# so importing it does not run its command-line interface.
import check_strategy_research47 as shared


START_BASE = {27: 1, 52: 20, 53: 15, 54: 10, 112: 10, 113: 10}
START_CARGO = dict(shared.START_CARGO)
START_FUNDS = 4_147_000
ENGINEERS = 10
SCIENTISTS = 10
TOPIC = 14
ITEM = 20
ITEM_WORK = 432
ITEM_PRICE = 45_000  # raw `anObjectPriceInit[20]` at 0xAA310
ITEM_COST = ITEM_PRICE * 7 // 10  # raw 0.7 multiplier at 0x6F740..0x6F754
MATERIAL_SLOTS = (105, 106, 107, 109)


def fail(message: str) -> None:
    raise SystemExit(f"NEW GAME MANUFACTURE 20 FAILED: {message}")


def read_lines(path: str) -> list[bytes]:
    return Path(path).read_bytes().splitlines()


def product_rows(lines: list[bytes], label: str) -> dict[int, tuple[int, tuple[int, int, int, int], int, int, int, int, int]]:
    prefix = f"pocketufo: strategy product {label}:".encode()
    result: dict[int, tuple[int, tuple[int, int, int, int], int, int, int, int, int]] = {}
    pattern = re.compile(
        rb"item ([0-9]+) work ([0-9]+) comps ([0-9]+)/([0-9]+)/([0-9]+)/([0-9]+) "
        rb"stock ([0-9]+) request ([0-9]+) engineers ([0-9]+) committed ([0-9]+) accumulated ([0-9]+)$"
    )
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = pattern.search(line)
        if match is None:
            fail(f"bad product row: {line!r}")
        values = tuple(map(int, match.groups()))
        slot, work = values[:2]
        components = values[2:6]
        stock, requested, assigned, committed, accumulated = values[6:]
        if slot in result:
            fail(f"duplicate product {slot} at {label}")
        result[slot] = (work, components, stock, requested, assigned, committed, accumulated)
    return result


def unlocked_product_rows(
    lines: list[bytes], label: str,
) -> list[tuple[int, int, int, tuple[int, int, int, int], tuple[int, int, int, int]]]:
    prefix = f"pocketufo: strategy unlocked-product {label}:".encode()
    pattern = re.compile(
        rb"topic ([0-9]+) item ([0-9]+) work ([0-9]+) comps "
        rb"([0-9]+)/([0-9]+)/([0-9]+)/([0-9]+) stock "
        rb"([0-9]+)/([0-9]+)/([0-9]+)/([0-9]+)$"
    )
    result = []
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = pattern.search(line)
        if match is None:
            fail(f"bad unlocked-product row: {line!r}")
        v = tuple(map(int, match.groups()))
        result.append((v[0], v[1], v[2], v[3:7], v[7:11]))
    return result


def manuf_panel(lines: list[bytes], label: str) -> tuple[int, int, int, int]:
    line = shared.one_line(lines, f"pocketufo: strategy panel {label}:".encode())
    match = re.search(rb"manuf rows ([0-9]+) pos (-?[0-9]+)/(-?[0-9]+) font ([0-9]+)$", line)
    if match is None:
        fail(f"manufacture panel geometry is incomplete: {line!r}")
    return tuple(map(int, match.groups()))


def manuf_visible_rows(lines: list[bytes], label: str) -> dict[int, int]:
    prefix = f"pocketufo: strategy row {label}: manuf ".encode()
    result: dict[int, int] = {}
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"manuf ([0-9]+) aux ([0-9]+)$", line)
        if match is None:
            fail(f"bad Manufacture row: {line!r}")
        row, item = map(int, match.groups())
        if row in result:
            fail(f"duplicate visible Manufacture row {row} at {label}")
        result[row] = item
    return result


def text_record(lang_path: str, index: int) -> str:
    blob = shared.archive_member(lang_path, "TEXTS.DAT")
    stride = 200
    if len(blob) < 2 or struct.unpack_from("<H", blob, 0)[0] != stride \
            or (len(blob) - 2) % stride != 0:
        fail(f"bad TEXTS.DAT header or record span: bytes={len(blob)}")
    count = (len(blob) - 2) // stride
    if index < 0 or index >= count:
        fail(f"TEXTS.DAT index is out of range: {index}/{count}")
    start = 2 + index * stride
    return blob[start:start + stride].split(b"\0", 1)[0].decode("cp1251")


def verify_raw_contract(main_path: str, lang_path: str) -> None:
    research = shared.archive_member(main_path, "RESEARCH.DAT")
    objects = shared.archive_member(main_path, "OBJECTS.DAT")
    if len(objects) != 149 * 208:
        fail(f"OBJECTS.DAT size differs: {len(objects)}")
    topic14 = shared.research_record(research, TOPIC)
    if struct.unpack_from("<H", topic14, 0)[0] != 434 \
            or struct.unpack_from("<H", topic14, 4)[0] != 3600 \
            or topic14[10:15] != bytes(5) or topic14[15:20] != bytes(5) \
            or topic14[20:25] != bytes((ITEM, 0, 0, 0, 0)) \
            or topic14[25:30] != bytes(5):
        fail(f"raw topic 14 differs: {topic14.hex(' ')}")
    if text_record(lang_path, 434) != "Сканер движения" \
            or text_record(lang_path, 1466) != "Сканер движения":
        fail("raw topic/product text for Motion Scanner differs")

    object20 = objects[ITEM * 208:(ITEM + 1) * 208]
    name = object20[:36].split(b"\0", 1)[0].decode("cp1251")
    work = struct.unpack_from("<H", object20, 0xBA)[0]
    price_field = struct.unpack_from("<I", object20, 0xBC)[0]
    components = object20[0xC0:0xC4]
    cargo_size = object20[0xC6]
    if name != "Motion scaner" or work != ITEM_WORK or price_field != 0 \
            or components != bytes(4) or cargo_size != 1:
        fail(f"raw OBJECTS.DAT slot 20 differs: {name!r}, work={work}, price={price_field}, "
             f"components={components.hex()}, cargo={cargo_size}")


def expect_base_state(lines: list[bytes], label: str, geo_screen: int = 0,
                      funds: int = START_FUNDS) -> bytes:
    state = shared.tuple_for(lines, label)
    shared.expect_screen(state, geo_screen, label)
    if shared.field(state, "baseCount") != 1 or shared.field(state, "selectedBase") != 0 \
            or shared.field(state, "funds") != funds or shared.field(state, "sites") != 0:
        fail(f"{label} is not the expected one-base campaign: {state!r}")
    return state


def expect_no_materials(stock: dict[int, int], label: str) -> None:
    present = {slot: stock[slot] for slot in MATERIAL_SLOTS if slot in stock}
    if present:
        fail(f"{label} has an unexpected manufacturing material stock: {present!r}")


def expect_unlocked_motion_scanner(lines: list[bytes], label: str) -> None:
    matches = [row for row in unlocked_product_rows(lines, label)
               if row[0] == TOPIC and row[1] == ITEM]
    if matches != [(TOPIC, ITEM, ITEM_WORK, (0, 0, 0, 0), (0, 0, 0, 0))]:
        fail(f"topic 14 did not expose only the expected slot-20 recipe: {matches!r}")


def write_flow(path: str, text: str) -> None:
    shared.write_new_flow(path, text)


def mode_newgame_probe(log_path: str, template_path: str, output_path: str,
                       main_path: str, lang_path: str) -> None:
    verify_raw_contract(main_path, lang_path)
    lines = read_lines(log_path)
    baseline = expect_base_state(lines, "new_game", geo_screen=1)
    if shared.field(baseline, "researchAssigned") != 0 \
            or shared.field(baseline, "engineerAssigned") != 0 \
            or shared.field(baseline, "lab") != 50 \
            or shared.field(baseline, "workshop") != 50 \
            or shared.field(baseline, "stores") != 600:
        fail(f"raw New Game facilities or assignments differ: {baseline!r}")
    if shared.pair_field(baseline, "engineersAvailable/Total") != (10, 10) \
            or shared.pair_field(baseline, "scientistsAvailable/Total") != (10, 10):
        fail(f"raw New Game staff totals differ: {baseline!r}")
    if shared.stock_rows(lines, "new_game") != START_BASE:
        fail(f"raw New Game Base stock differs: {shared.stock_rows(lines, 'new_game')!r}")
    if ITEM in shared.stock_rows(lines, "new_game"):
        fail("New Game already has a Motion Scanner in Base stock")
    expect_no_materials(shared.stock_rows(lines, "new_game"), "New Game")
    if unlocked_product_rows(lines, "new_game"):
        fail("New Game has a manufacturable unlocked product; re-evaluate the route")
    if shared.topic_rows(lines, "new_game").get(TOPIC) != (1, 0, 0, 3600, 0):
        fail("raw topic 14 is not active, locked, and unassigned at New Game")
    shared.cargo_rows(lines, "new_game")

    expect_base_state(lines, "manufacture_base_info", geo_screen=3)
    expect_base_state(lines, "manufacture_base_strip", geo_screen=1)
    manuf_start = expect_base_state(lines, "manuf_newgame_probe", geo_screen=9)
    if shared.field(manuf_start, "funds") != START_FUNDS \
            or shared.field(manuf_start, "researchAssigned") != 0 \
            or shared.field(manuf_start, "engineerAssigned") != 0 \
            or shared.pair_field(manuf_start, "engineersAvailable/Total") != (10, 10) \
            or shared.pair_field(manuf_start, "scientistsAvailable/Total") != (10, 10):
        fail(f"opening initial Manufacture changed the raw campaign: {manuf_start!r}")
    panel_count, _, _, _ = manuf_panel(lines, "manuf_newgame_probe")
    if panel_count != 0 or manuf_visible_rows(lines, "manuf_newgame_probe"):
        fail("raw New Game has a live Manufacture product; do not use the Research route")
    if unlocked_product_rows(lines, "manuf_newgame_probe"):
        fail("raw New Game exposed an unlocked manufacture recipe in the live screen")
    if shared.stock_rows(lines, "manuf_newgame_probe") != START_BASE:
        fail("opening initial Manufacture changed Base stock")
    expect_base_state(lines, "manuf_newgame_map", geo_screen=0)

    expect_base_state(lines, "research_base_strip", geo_screen=1)
    opened = expect_base_state(lines, "research_probe", geo_screen=8)
    if shared.field(opened, "funds") != START_FUNDS \
            or shared.field(opened, "researchAssigned") != 0 \
            or shared.field(opened, "engineerAssigned") != 0 \
            or shared.pair_field(opened, "engineersAvailable/Total") != (10, 10) \
            or shared.pair_field(opened, "scientistsAvailable/Total") != (10, 10):
        fail(f"Research probe changed the campaign: {opened!r}")
    if shared.topic_rows(lines, "research_probe").get(TOPIC) != (1, 0, 0, 3600, 0):
        fail("topic 14 changed before real Research input")
    visible = shared.research_visible_rows(lines, "research_probe")
    rows = [row for row, topic in visible.items() if topic == TOPIC]
    if len(rows) != 1:
        fail(f"topic 14 has no unique live Research row: {visible!r}")
    row = rows[0]
    count, panel_x, panel_y, font = shared.research_panel(lines, "research_probe")
    if row >= count or row >= 14 or font <= 0:
        fail(f"topic 14 is not on the first visible Research page: row={row}, count={count}")
    plus = shared.control_rect(lines, "research_probe", "research", 2)
    start_x, start_y = shared.start_control_center(lines, "research_probe")
    if shared.last_line_flags(lines) != (0, []):
        fail("New Game Research probe has a pending status line")
    text = Path(template_path).read_text(encoding="utf-8")
    replacements = {
        "@TOPIC14_ROW_X@": str(panel_x + 5),
        "@TOPIC14_ROW_Y@": str(panel_y + row * font + font // 2),
        "__TOPIC14_SCIENTIST_CLICKS__": "\n".join(
            f"click {plus[0] + plus[2] // 2} {plus[1] + plus[3] // 2}" for _ in range(SCIENTISTS)
        ),
        "@TOPIC14_START_X@": str(start_x),
        "@TOPIC14_START_Y@": str(start_y),
    }
    for old, new in replacements.items():
        text = text.replace(old, new)
    if "@TOPIC14_" in text or "__TOPIC14_" in text:
        fail("generated Research start flow has a placeholder")
    write_flow(output_path, text)
    print(f"ok: raw topic 14 row {row}; ten real Scientist clicks; live Start at {start_x}/{start_y}")


def mode_research_start(log_path: str, save_path: str, main_path: str, lang_path: str) -> None:
    verify_raw_contract(main_path, lang_path)
    lines = read_lines(log_path)
    new_game = expect_base_state(lines, "new_game", geo_screen=1)
    if shared.pair_field(new_game, "engineersAvailable/Total") != (10, 10) \
            or shared.pair_field(new_game, "scientistsAvailable/Total") != (10, 10):
        fail(f"Research start did not begin with raw staff totals: {new_game!r}")
    expect_base_state(lines, "research_base_strip", geo_screen=1)
    opened = expect_base_state(lines, "research_open", geo_screen=8)
    if shared.field(opened, "funds") != START_FUNDS or shared.field(opened, "researchAssigned") != 0 \
            or shared.field(opened, "engineerAssigned") != 0 \
            or shared.pair_field(opened, "engineersAvailable/Total") != (10, 10) \
            or shared.pair_field(opened, "scientistsAvailable/Total") != (10, 10):
        fail(f"Research did not open from the raw start: {opened!r}")
    visible = shared.research_visible_rows(lines, "research_open")
    if sum(topic == TOPIC for topic in visible.values()) != 1:
        fail(f"topic 14 is not the unique live Research row: {visible!r}")
    started = expect_base_state(lines, "research_started", geo_screen=8)
    if shared.field(started, "researchAssigned") != SCIENTISTS \
            or shared.field(started, "engineerAssigned") != 0 \
            or shared.pair_field(started, "scientistsAvailable/Total") != (0, 10) \
            or shared.pair_field(started, "engineersAvailable/Total") != (10, 10):
        fail(f"the ten raw Scientists were not assigned to topic 14: {started!r}")
    topics = shared.topic_rows(lines, "research_started")
    if topics.get(TOPIC) != (1, 0, 1, 3600, SCIENTISTS) \
            or any(topic != TOPIC and row[4] != 0 for topic, row in topics.items()):
        fail(f"Start did not create only the raw topic-14 work record: {topics!r}")
    if shared.stock_rows(lines, "research_started") != START_BASE \
            or shared.field(started, "funds") != START_FUNDS:
        fail("starting Research changed funds or raw Base stock")
    shared.cargo_rows(lines, "new_game")
    on_map = expect_base_state(lines, "research_on_map", geo_screen=0)
    if shared.field(on_map, "researchAssigned") != SCIENTISTS \
            or shared.field(on_map, "engineerAssigned") != 0:
        fail(f"closing Research changed the running project: {on_map!r}")
    if shared.pair_field(on_map, "scientistsAvailable/Total") != (0, 10) \
            or shared.pair_field(on_map, "engineersAvailable/Total") != (10, 10):
        fail(f"closing Research changed staff availability: {on_map!r}")
    if shared.last_line_flags(lines) != (0, []):
        fail("ongoing Research save has a status line")
    shared.save_written(lines, 0)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"ongoing topic-14 save is missing or empty: {saved}")
    print("ok: ten existing Scientists started topic 14 through the Research UI; no purchase or stock write")


def research_clock_flow(load_slot: int, hours: int, final_stage: bool,
                        speed_center: tuple[int, int]) -> str:
    x, y = speed_center
    commands = [
        f"# final-topic14-pulse {1 if final_stage else 0}",
        f"load {load_slot}",
        "state research_clock_loaded",
        "strategystate research_clock_loaded",
        "campaign cargo research_clock_loaded",
        "lines",
        f"click {x} {y}",
    ]
    for hour in range(hours):
        commands.append("tick 1")
        if final_stage and hour + 1 == hours:
            commands.extend(("campaign ack-status 37", f"click {x} {y}"))
        else:
            commands.extend(("campaign ack-confirm", f"click {x} {y}"))
    commands.extend((
        "strategystate research_clock_end",
        "lines",
        "campaign cargo research_clock_end",
        "save 1",
        "state research_clock_saved",
    ))
    return "\n".join(commands) + "\n"


def mode_clock_start(start_path: str, output_path: str, main_path: str, lang_path: str) -> None:
    verify_raw_contract(main_path, lang_path)
    lines = read_lines(start_path)
    started = expect_base_state(lines, "research_started", geo_screen=8)
    topic = shared.topic_rows(lines, "research_started").get(TOPIC)
    if topic != (1, 0, 1, 3600, SCIENTISTS) \
            or shared.field(started, "researchAssigned") != SCIENTISTS \
            or shared.field(started, "engineerAssigned") != 0:
        fail(f"cannot time an unmeasured topic-14 job: {topic!r} / {started!r}")
    remaining = math.ceil((topic[3] - topic[2]) / topic[4])
    hours = min(120, remaining)
    speed = shared.control_center_in_snapshot(lines, "research_on_map", "speed", 7)
    write_flow(output_path, research_clock_flow(0, hours, remaining <= hours, speed))
    print(f"ok: first Research clock stage uses {hours} actual one-hour pulses")


def mode_clock_stage(previous_path: str, current_path: str, flow_path: str,
                     save_path: str, main_path: str, lang_path: str) -> None:
    verify_raw_contract(main_path, lang_path)
    previous = read_lines(previous_path)
    current = read_lines(current_path)
    flow = Path(flow_path).read_text(encoding="utf-8").splitlines()
    hours = sum(line.strip() == "tick 1" for line in flow)
    if hours < 1 or hours > 120:
        fail(f"Research clock stage has invalid pulse count {hours}")
    final_stage = "# final-topic14-pulse 1" in flow
    ack_calls = sum(line.strip() == "campaign ack-confirm" for line in flow)
    status_calls = sum(line.strip() == "campaign ack-status 37" for line in flow)
    if ack_calls != hours - (1 if final_stage else 0) or status_calls != int(final_stage):
        fail(f"Research clock flow has wrong guarded inputs: hours={hours}, ack10={ack_calls}, ack37={status_calls}")
    if final_stage:
        status_i = flow.index("campaign ack-status 37")
        if flow[status_i - 1].strip() != "tick 1" or not flow[status_i + 1].strip().startswith("click "):
            fail("flag-37 helper must follow the final hour pulse and precede live speed reselection")

    match = re.fullmatch(r"research-clock-([0-9]{2})\.flow\.txt", Path(flow_path).name)
    if match is None:
        fail(f"Research clock flow name lacks its two-digit stage: {flow_path}")
    stage = int(match.group(1))
    previous_label = "research_on_map" if stage == 0 else "research_clock_end"
    loaded = expect_base_state(current, "research_clock_loaded", geo_screen=0)
    ended = expect_base_state(current, "research_clock_end", geo_screen=0)
    shared.expect_cold_load(loaded, "research_clock_loaded")
    if shared.field(loaded, "researchAssigned") != SCIENTISTS \
            or shared.field(loaded, "engineerAssigned") != 0:
        fail(f"Research stage load does not retain ten assigned Scientists: {loaded!r}")
    before = expect_base_state(previous, previous_label, geo_screen=0)
    for name in ("baseCount", "selectedBase", "funds", "sites", "researchAssigned",
                 "engineerAssigned", "sec", "pause", "engineersAvailable/Total",
                 "scientistsAvailable/Total"):
        before_value = shared.pair_field(before, name) if "/" in name else shared.field(before, name)
        loaded_value = shared.pair_field(loaded, name) if "/" in name else shared.field(loaded, name)
        if before_value != loaded_value:
            fail(f"fresh load changed {name} before Research stage {stage}")
    if shared.stock_rows(previous, previous_label) != shared.stock_rows(current, "research_clock_loaded") \
            or shared.topic_rows(previous, previous_label) != shared.topic_rows(current, "research_clock_loaded"):
        fail(f"fresh load changed stock or topic rows before Research stage {stage}")
    shared.cargo_rows(current, "research_clock_loaded")
    speed_center = shared.control_center_in_snapshot(current, "research_clock_loaded", "speed", 7)
    shared.check_hourly_speed_inputs(
        flow, hours, speed_center,
        "campaign ack-status 37" if final_stage else None,
    )
    initial = shared.topic_rows(current, "research_clock_loaded").get(TOPIC)
    final = shared.topic_rows(current, "research_clock_end").get(TOPIC)
    if initial is None or final is None or initial[4] != SCIENTISTS:
        fail(f"Research stage lost its ten Scientists: {initial!r} -> {final!r}")
    shared.check_ack_contract(current, ack_calls)
    shared.check_status37_contract(current, status_calls)
    elapsed = shared.field(ended, "sec") - shared.field(loaded, "sec")
    if elapsed != hours * 3601:
        fail(f"Research stage elapsed {elapsed} seconds for {hours} one-hour pulses")
    if shared.field(ended, "tickSec") != 3600 or shared.field(ended, "pause") != 0:
        fail(f"Research clock did not end at live one-hour speed: {ended!r}")
    if shared.stock_rows(current, "research_clock_end") != START_BASE:
        fail("Research clock changed raw Base stock")
    flags_count, flags = shared.last_line_flags(current)
    if flags_count != 0 or flags:
        fail(f"Research clock left a status line: {flags!r}")
    if final_stage:
        if final[1] != 1 or final[2] < final[3] or final[4] != 0 \
                or shared.field(ended, "researchAssigned") != 0 \
                or shared.field(ended, "engineerAssigned") != 0:
            fail(f"final pulse did not finish topic 14 and release Scientists: {final!r} / {ended!r}")
        if shared.pair_field(ended, "scientistsAvailable/Total") != (10, 10) \
                or shared.pair_field(ended, "engineersAvailable/Total") != (10, 10):
            fail(f"final Research pulse did not release all ten Scientists: {ended!r}")
        expect_unlocked_motion_scanner(current, "research_clock_end")
    elif final[1] != 0 or final[0] != 1 or final[4] != SCIENTISTS or final[2] >= final[3] \
            or shared.field(ended, "researchAssigned") != SCIENTISTS \
            or shared.field(ended, "engineerAssigned") != 0:
        fail(f"non-final pulse changed the active topic-14 job: {final!r}")
    elif shared.pair_field(ended, "scientistsAvailable/Total") != (0, 10) \
            or shared.pair_field(ended, "engineersAvailable/Total") != (10, 10):
        fail(f"non-final Research pulse changed staff availability: {ended!r}")
    shared.save_written(current, 1)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"Research checkpoint is missing or empty: {saved}")
    print(f"ok: Research stage {stage} advanced {hours} measured hours; flag37 cleared in-process={int(final_stage)}")


def mode_clock_next(current_path: str, output_path: str, main_path: str, lang_path: str) -> None:
    verify_raw_contract(main_path, lang_path)
    lines = read_lines(current_path)
    state = expect_base_state(lines, "research_clock_end", geo_screen=0)
    topic = shared.topic_rows(lines, "research_clock_end").get(TOPIC)
    if topic is None or shared.field(state, "pause") != 0 or shared.field(state, "tickSec") != 3600:
        fail(f"cannot continue Research from this state: {topic!r} / {state!r}")
    if topic[1] == 1:
        if topic[2] < topic[3] or topic[4] != 0 \
                or shared.field(state, "researchAssigned") != 0 \
                or shared.field(state, "engineerAssigned") != 0:
            fail(f"topic 14 unlock state is incomplete: {topic!r} / {state!r}")
        expect_unlocked_motion_scanner(lines, "research_clock_end")
        if Path(output_path).exists():
            fail(f"unexpected extra Research clock flow already exists: {output_path}")
        print("ok: topic 14 is complete; no more Research work is needed")
        return
    if topic[0] != 1 or topic[4] != SCIENTISTS or topic[1] != 0 or topic[2] >= topic[3]:
        fail(f"topic 14 is neither active nor complete: {topic!r}")
    if shared.field(state, "researchAssigned") != SCIENTISTS \
            or shared.field(state, "engineerAssigned") != 0:
        fail(f"active topic 14 has the wrong Base 0 assignment totals: {state!r}")
    match = re.fullmatch(r"research-clock-([0-9]{2})\.flow\.txt", Path(output_path).name)
    if match is None:
        fail(f"next Research flow name lacks its two-digit stage: {output_path}")
    stage = int(match.group(1))
    if stage >= 6:
        fail(f"topic 14 remained incomplete after {stage} clock stages")
    remaining = math.ceil((topic[3] - topic[2]) / topic[4])
    hours = min(120, remaining)
    speed = shared.control_center_in_snapshot(lines, "research_clock_end", "speed", 7)
    write_flow(output_path, research_clock_flow(1, hours, remaining <= hours, speed))
    print(f"ok: topic 14 needs {hours} more one-hour pulses")


def mode_manuf_load_probe(previous_path: str, log_path: str, output_path: str,
                          main_path: str, lang_path: str) -> None:
    verify_raw_contract(main_path, lang_path)
    previous = read_lines(previous_path)
    lines = read_lines(log_path)
    before = expect_base_state(previous, "research_clock_end", geo_screen=0)
    state = expect_base_state(lines, "manufacture_map_loaded", geo_screen=0)
    shared.expect_cold_load(state, "manufacture_map_loaded")
    if shared.field(before, "tickSec") != 3600 or shared.field(before, "pause") != 0:
        fail(f"completed Research checkpoint was not saved at live one-hour speed: {before!r}")
    for name in ("baseCount", "selectedBase", "funds", "sec", "pause", "sites",
                 "researchAssigned", "engineerAssigned", "engineersAvailable/Total",
                 "scientistsAvailable/Total"):
        before_value = shared.pair_field(before, name) if "/" in name else shared.field(before, name)
        loaded_value = shared.pair_field(state, name) if "/" in name else shared.field(state, name)
        if before_value != loaded_value:
            fail(f"Research-to-Manufacture load changed saved field {name}")
    if shared.stock_rows(previous, "research_clock_end") \
            != shared.stock_rows(lines, "manufacture_map_loaded") \
            or shared.topic_rows(previous, "research_clock_end") \
            != shared.topic_rows(lines, "manufacture_map_loaded") \
            or shared.cargo_rows(previous, "research_clock_end") \
            != shared.cargo_rows(lines, "manufacture_map_loaded"):
        fail("Research-to-Manufacture load changed saved stock, topics, or craft cargo")
    if shared.field(state, "pause") != 0:
        fail(f"completed-research load retained an unexpected clock pause: {state!r}")
    if shared.field(state, "researchAssigned") != 0 or shared.field(state, "engineerAssigned") != 0:
        fail(f"post-research map has unexpected assignments: {state!r}")
    if shared.pair_field(state, "engineersAvailable/Total") != (10, 10) \
            or shared.pair_field(state, "scientistsAvailable/Total") != (10, 10):
        fail(f"post-research map lost raw staff totals: {state!r}")
    topic = shared.topic_rows(lines, "manufacture_map_loaded").get(TOPIC)
    if topic is None or topic[1] != 1 or topic[2] < topic[3] or topic[4] != 0:
        fail("topic 14 is not unlocked in the manufacture map probe")
    if shared.stock_rows(lines, "manufacture_map_loaded") != START_BASE:
        fail("completed-research load changed raw Base stock")
    expect_unlocked_motion_scanner(lines, "manufacture_map_loaded")
    if shared.last_line_flags(lines) != (0, []):
        fail("saved completed research state contains a pending line")
    shared.cargo_rows(lines, "manufacture_map_loaded")

    base_strip = expect_base_state(lines, "manufacture_base_strip", geo_screen=1)
    root_click = b"pocketufo: step click 202 267"
    if root_click not in lines or lines.index(root_click) >= lines.index(shared.tuple_for(lines, "manufacture_base_strip")):
        fail("root control 10 did not open Base 1 from the immediate map load")
    for name in ("baseCount", "selectedBase", "funds", "sec", "tickSec", "pause", "sites",
                 "researchAssigned", "engineerAssigned", "engineersAvailable/Total",
                 "scientistsAvailable/Total"):
        map_value = shared.pair_field(state, name) if "/" in name else shared.field(state, name)
        base_value = shared.pair_field(base_strip, name) if "/" in name else shared.field(base_strip, name)
        if map_value != base_value:
            fail(f"Base 1 entry changed {name} after the map load")
    if shared.stock_rows(lines, "manufacture_map_loaded") \
            != shared.stock_rows(lines, "manufacture_base_strip") \
            or shared.topic_rows(lines, "manufacture_map_loaded") \
            != shared.topic_rows(lines, "manufacture_base_strip") \
            or shared.cargo_rows(lines, "manufacture_map_loaded") \
            != shared.cargo_rows(lines, "manufacture_base_strip"):
        fail("Base 1 entry changed saved stock, topics, or craft cargo")

    speed5 = shared.control_center_in_snapshot(lines, "manufacture_map_loaded", "speed", 2)
    nav_manuf = shared.control_center_in_snapshot(lines, "manufacture_base_strip", "nav", 6)
    flow = "\n".join((
        "load 1",
        "state manufacture_map_loaded",
        "strategystate manufacture_map_loaded",
        "campaign cargo manufacture_map_loaded",
        f"click {speed5[0]} {speed5[1]}",
        "click 202 267",
        "tick 20",
        "strategystate manufacture_base_strip",
        "campaign cargo manufacture_base_strip",
        f"click {nav_manuf[0]} {nav_manuf[1]}",
        "tick 20",
        "strategystate manuf_probe",
        "campaign cargo manuf_probe",
        "lines",
    )) + "\n"
    write_flow(output_path, flow)
    print("ok: loaded completed research; wrote live map -> Base 1 -> Manufacture probe")


def mode_manuf_probe(log_path: str, order_template: str, output_path: str,
                     main_path: str, lang_path: str) -> None:
    verify_raw_contract(main_path, lang_path)
    lines = read_lines(log_path)
    loaded = expect_base_state(lines, "manufacture_map_loaded", geo_screen=0)
    shared.expect_cold_load(loaded, "manufacture_map_loaded")
    if shared.field(loaded, "pause") != 0:
        fail(f"Manufacture map load retained an unexpected clock pause: {loaded!r}")
    base_strip = expect_base_state(lines, "manufacture_base_strip", geo_screen=1)
    if shared.field(base_strip, "sec") != shared.field(loaded, "sec") \
            or shared.field(base_strip, "tickSec") != 5 \
            or shared.field(base_strip, "pause") != 0:
        fail(f"Map -> Base 1 changed saved time or five-second speed: {loaded!r} -> {base_strip!r}")
    nav_manuf = shared.control_center_in_snapshot(lines, "manufacture_base_strip", "nav", 6)
    root_click = b"pocketufo: step click 202 267"
    nav_click = f"pocketufo: step click {nav_manuf[0]} {nav_manuf[1]}".encode()
    base_line = shared.tuple_for(lines, "manufacture_base_strip")
    if root_click not in lines or nav_click not in lines \
            or not lines.index(root_click) < lines.index(base_line) < lines.index(nav_click):
        fail("Manufacture nav slot 6 was not clicked from the live Base 1 scene")
    state = expect_base_state(lines, "manuf_probe", geo_screen=9)
    if shared.field(state, "researchAssigned") != 0 or shared.field(state, "engineerAssigned") != 0 \
            or shared.field(state, "funds") != START_FUNDS:
        fail(f"Manufacture probe changed campaign assignments or funds: {state!r}")
    if shared.pair_field(state, "engineersAvailable/Total") != (10, 10) \
            or shared.pair_field(state, "scientistsAvailable/Total") != (10, 10):
        fail(f"Manufacture probe changed raw staff availability: {state!r}")
    expect_unlocked_motion_scanner(lines, "manuf_probe")
    panel_count, panel_x, panel_y, font = manuf_panel(lines, "manuf_probe")
    visible = manuf_visible_rows(lines, "manuf_probe")
    matching = [row for row, item in visible.items() if item == ITEM]
    if len(matching) != 1:
        fail(f"slot 20 has no unique live Manufacture row: {visible!r}")
    row = matching[0]
    if row >= panel_count or row >= 14 or font <= 0:
        fail(f"slot 20 is not on the first visible Manufacture page: row={row}, count={panel_count}")
    eng = shared.control_rect(lines, "manuf_probe", "manuf", 2)
    unit = shared.control_rect(lines, "manuf_probe", "manuf", 4)
    if shared.stock_rows(lines, "manuf_probe") != START_BASE:
        fail("opening Manufacture changed Base stock")
    expect_no_materials(shared.stock_rows(lines, "manuf_probe"), "Manufacture probe")
    if shared.last_line_flags(lines) != (0, []):
        fail("Manufacture probe has a pending status line")
    speed5 = shared.control_center_in_snapshot(lines, "manufacture_map_loaded", "speed", 2)
    close = shared.control_center_in_snapshot(lines, "manuf_probe", "close", 0)
    template = Path(order_template).read_text(encoding="utf-8")
    replacements = {
        "@SPEED5_X@": str(speed5[0]), "@SPEED5_Y@": str(speed5[1]),
        "@CLOSE_X@": str(close[0]), "@CLOSE_Y@": str(close[1]),
        "@NAV_MANUF_X@": str(nav_manuf[0]), "@NAV_MANUF_Y@": str(nav_manuf[1]),
        "@PRODUCT20_X@": str(panel_x + 10),
        "@PRODUCT20_Y@": str(panel_y + row * font + font // 2),
        "__ENGINEER_PLUS_CLICKS__": "\n".join(
            f"click {eng[0] + eng[2] // 2} {eng[1] + eng[3] // 2}" for _ in range(ENGINEERS)
        ),
        "@UNIT_PLUS_X@": str(unit[0] + unit[2] // 2),
        "@UNIT_PLUS_Y@": str(unit[1] + unit[3] // 2),
    }
    for old, new in replacements.items():
        template = template.replace(old, new)
    if "@" in template or "__ENGINEER_PLUS_CLICKS__" in template:
        fail("generated Manufacture order flow has a placeholder")
    write_flow(output_path, template)
    print(f"ok: live slot-20 row {row}; ten real Engineer + clicks; one unit + click")


def expect_pending_order(lines: list[bytes], label: str, accumulated: int = 1) -> bytes:
    # Raw ManufScreen_Draw seeds one work unit after a request and Engineer allocation.
    state = expect_base_state(lines, label, geo_screen=0)
    if shared.field(state, "funds") != START_FUNDS \
            or shared.field(state, "researchAssigned") != 0 \
            or shared.field(state, "engineerAssigned") != ENGINEERS \
            or shared.pair_field(state, "engineersAvailable/Total") != (0, 10) \
            or shared.pair_field(state, "scientistsAvailable/Total") != (10, 10):
        fail(f"{label} does not retain ten assigned Engineers and raw funds: {state!r}")
    stocks = shared.stock_rows(lines, label)
    if stocks != START_BASE or ITEM in stocks:
        fail(f"{label} changed Base stock before production: {stocks!r}")
    expect_no_materials(stocks, label)
    product = product_rows(lines, label).get(ITEM)
    if product != (ITEM_WORK, (0, 0, 0, 0), 0, 1, ENGINEERS, 0, accumulated):
        fail(f"{label} does not contain exactly one pending slot-20 order: {product!r}")
    if shared.topic_rows(lines, label).get(TOPIC, (0, 0, 0, 0, 0))[1] != 1:
        fail(f"{label} lost unlocked topic 14")
    return state


def mode_manuf_order(log_path: str, save_path: str) -> None:
    lines = read_lines(log_path)
    loaded = expect_base_state(lines, "manufacture_map_loaded", geo_screen=0)
    shared.expect_cold_load(loaded, "manufacture_map_loaded")
    base_strip = expect_base_state(lines, "manufacture_base_strip", geo_screen=1)
    if shared.field(base_strip, "sec") != shared.field(loaded, "sec") \
            or shared.field(base_strip, "tickSec") != 5 \
            or shared.field(base_strip, "pause") != 0:
        fail(f"Map -> Base 1 changed saved time or five-second speed: {loaded!r} -> {base_strip!r}")
    nav_manuf = shared.control_center_in_snapshot(lines, "manufacture_base_strip", "nav", 6)
    root_click = b"pocketufo: step click 202 267"
    nav_click = f"pocketufo: step click {nav_manuf[0]} {nav_manuf[1]}".encode()
    base_line = shared.tuple_for(lines, "manufacture_base_strip")
    if root_click not in lines or nav_click not in lines \
            or not lines.index(root_click) < lines.index(base_line) < lines.index(nav_click):
        fail("Manufacture order did not use live nav slot 6 from Base 1")
    opened = expect_base_state(lines, "manuf_open", geo_screen=9)
    if shared.field(opened, "funds") != START_FUNDS:
        fail("opening Manufacture changed funds")
    if ITEM not in manuf_visible_rows(lines, "manuf_open").values():
        fail("the live Manufacture list lost slot 20 before selection")
    allocated = expect_base_state(lines, "manuf_allocated", geo_screen=9)
    if shared.field(allocated, "funds") != START_FUNDS \
            or shared.field(allocated, "engineerAssigned") != ENGINEERS \
            or shared.pair_field(allocated, "engineersAvailable/Total") != (0, 10) \
            or shared.pair_field(allocated, "scientistsAvailable/Total") != (10, 10):
        fail(f"the real controls did not assign ten Engineers without cost: {allocated!r}")
    allocated_row = product_rows(lines, "manuf_allocated").get(ITEM)
    if allocated_row != (ITEM_WORK, (0, 0, 0, 0), 0, 1, ENGINEERS, 0, 0):
        fail(f"the UI order is not one slot-20 item with ten Engineers: {allocated_row!r}")
    refreshed = expect_base_state(lines, "manuf_refreshed", geo_screen=9)
    if shared.field(refreshed, "sec") != shared.field(allocated, "sec") \
            or shared.field(refreshed, "engineerAssigned") != ENGINEERS:
        fail(f"the first Manufacture draw changed clock or Engineer assignment: {refreshed!r}")
    refreshed_product = product_rows(lines, "manuf_refreshed").get(ITEM)
    if refreshed_product != (ITEM_WORK, (0, 0, 0, 0), 0, 1, ENGINEERS, 0, 1):
        fail(f"the first selected-product draw did not seed one raw work unit: {refreshed_product!r}")
    pending = expect_pending_order(lines, "manufacture_pending")
    if shared.field(pending, "tickSec") != 5 or shared.last_line_flags(lines) != (0, []):
        fail(f"pending order was not saved on a clear five-second map: {pending!r}")
    shared.cargo_rows(lines, "manufacture_pending")
    shared.save_written(lines, 2)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"pending manufacture save is missing or empty: {saved}")
    print("ok: real UI saved one slot-20 order with ten existing Engineers; no funds or stock changed")


def mode_pending_probe(log_path: str, order_log_path: str, output_path: str) -> None:
    lines = read_lines(log_path)
    previous = read_lines(order_log_path)
    state = expect_pending_order(lines, "manufacture_pending_loaded")
    shared.expect_cold_load(state, "manufacture_pending_loaded")
    before = shared.tuple_for(previous, "manufacture_pending")
    for name in ("baseCount", "selectedBase", "funds", "sec", "pause", "sites",
                 "engineerAssigned", "researchAssigned", "engineersAvailable/Total",
                 "scientistsAvailable/Total"):
        before_value = shared.pair_field(before, name) if "/" in name else shared.field(before, name)
        loaded_value = shared.pair_field(state, name) if "/" in name else shared.field(state, name)
        if before_value != loaded_value:
            fail(f"fresh load changed pending-order field {name}")
    if product_rows(lines, "manufacture_pending_loaded") \
            != product_rows(previous, "manufacture_pending") \
            or shared.stock_rows(lines, "manufacture_pending_loaded") \
            != shared.stock_rows(previous, "manufacture_pending") \
            or shared.topic_rows(lines, "manufacture_pending_loaded") \
            != shared.topic_rows(previous, "manufacture_pending") \
            or shared.cargo_rows(lines, "manufacture_pending_loaded") \
            != shared.cargo_rows(previous, "manufacture_pending"):
        fail("fresh load changed the pending product or Base stock")
    if shared.last_line_flags(lines) != (0, []):
        fail("pending-order load has a status line")
    shared.cargo_rows(lines, "manufacture_pending_loaded")
    speed = shared.control_center_in_snapshot(lines, "manufacture_pending_loaded", "speed", 7)
    commands = [
        "load 2",
        "state manufacture_clock_loaded",
        "strategystate manufacture_clock_loaded",
        "campaign cargo manufacture_clock_loaded",
        "click " + str(speed[0]) + " " + str(speed[1]),
    ]
    for _ in range(43):
        commands.extend(("tick 1", "campaign ack-confirm", f"click {speed[0]} {speed[1]}"))
    commands.extend((
        "strategystate manufacture_before_last_hour",
        "lines",
        "campaign cargo manufacture_before_last_hour",
        "tick 1",
        "campaign ack-status 34",
        f"click {speed[0]} {speed[1]}",
        "strategystate manufacture_after_clock",
        "lines",
        "campaign cargo manufacture_after_clock",
        "save 3",
        "state manufacture_saved",
    ))
    write_flow(output_path, "\n".join(commands) + "\n")
    print("ok: pending order survived fresh load; wrote 43-hour plus terminal 34-status flow")


def check_status_contract(lines: list[bytes], expected: int) -> None:
    prefix = b"pocketufo: campaign ack-status:"
    events = [line for line in lines if line.startswith(prefix)]
    before = [line for line in events if f" before expected={expected} ".encode() in line]
    after = [line for line in events if f" after expected={expected} ".encode() in line]
    failures = [line for line in events if b": FAIL" in line]
    if failures or len(before) != 1 or len(after) != 1 or len(events) not in (2, 3):
        fail(f"status-{expected} helper did not emit one guarded before/after pair: {events!r}")
    before_re = re.compile(
        rb"before expected=" + str(expected).encode()
        + rb" flag=" + str(expected).encode()
        + rb" lineCount=1 pause=1 selection=0 screen=0 geo=1 tact=0 tactOwner=[0-9-]+ modal=0 "
        rb"control=GEOUI_CTL_187024 owner=1 enabled=1 rect=[0-9]+/[0-9]+/[0-9]+/[0-9]+ "
        rb"center=[0-9]+/[0-9]+ callbackMatch=1 tickSec=3600$"
    )
    after_re = re.compile(
        rb"after expected=" + str(expected).encode()
        + rb" lineCount=0 firstFlag=-1 pause=0 selection=0 screen=0 geo=1 tact=0 modal=0 tickSec=5 result=PASS$"
    )
    if before_re.search(before[0]) is None or after_re.search(after[0]) is None:
        fail(f"status-{expected} helper control or result differs: {before!r} / {after!r}")


def mode_manuf_clock(pending_log_path: str, clock_log_path: str, flow_path: str,
                     save_path: str, main_path: str, lang_path: str) -> None:
    verify_raw_contract(main_path, lang_path)
    pending_lines = read_lines(pending_log_path)
    current = read_lines(clock_log_path)
    flow = Path(flow_path).read_text(encoding="utf-8").splitlines()
    ticks = [i for i, line in enumerate(flow) if line.strip() == "tick 1"]
    ack_calls = sum(line.strip() == "campaign ack-confirm" for line in flow)
    status_calls = sum(line.strip() == "campaign ack-status 34" for line in flow)
    if len(ticks) != 44 or ack_calls != 43 or status_calls != 1:
        fail(f"manufacture clock needs 43 guarded hours and one terminal status: {len(ticks)}/{ack_calls}/{status_calls}")
    status_index = flow.index("campaign ack-status 34")
    if status_index == 0 or flow[status_index - 1].strip() != "tick 1" \
            or status_index + 1 >= len(flow) or not flow[status_index + 1].strip().startswith("click "):
        fail("status 34 input must follow the 44th pulse and precede live speed reselection")
    loaded = expect_pending_order(current, "manufacture_clock_loaded")
    shared.expect_cold_load(loaded, "manufacture_clock_loaded")
    expected_loaded = shared.tuple_for(pending_lines, "manufacture_pending_loaded")
    for name in ("baseCount", "selectedBase", "funds", "sec", "pause", "sites",
                 "engineerAssigned", "researchAssigned", "engineersAvailable/Total",
                 "scientistsAvailable/Total"):
        loaded_value = shared.pair_field(loaded, name) if "/" in name else shared.field(loaded, name)
        before_value = shared.pair_field(expected_loaded, name) if "/" in name else shared.field(expected_loaded, name)
        if loaded_value != before_value:
            fail(f"clock process load changed pending-order field {name}")
    if product_rows(current, "manufacture_clock_loaded") != product_rows(pending_lines, "manufacture_pending_loaded") \
            or shared.stock_rows(current, "manufacture_clock_loaded") \
            != shared.stock_rows(pending_lines, "manufacture_pending_loaded") \
            or shared.topic_rows(current, "manufacture_clock_loaded") \
            != shared.topic_rows(pending_lines, "manufacture_pending_loaded") \
            or shared.cargo_rows(current, "manufacture_clock_loaded") \
            != shared.cargo_rows(pending_lines, "manufacture_pending_loaded"):
        fail("clock process load changed saved product, stock, Research, or cargo rows")
    speed_center = shared.control_center_in_snapshot(current, "manufacture_clock_loaded", "speed", 7)
    shared.check_hourly_speed_inputs(flow, 44, speed_center, "campaign ack-status 34")
    before = expect_pending_order(current, "manufacture_before_last_hour", accumulated=431)
    before_product = product_rows(current, "manufacture_before_last_hour").get(ITEM)
    if before_product != (ITEM_WORK, (0, 0, 0, 0), 0, 1, ENGINEERS, 0, 431):
        fail(f"the 1-work draw seed plus 43 hours did not leave exactly 431/432: {before_product!r}")
    if shared.field(before, "sec") - shared.field(loaded, "sec") != 43 * 3601:
        fail("the pre-completion checkpoint did not record exactly 43 normal hour pulses")
    if shared.last_line_flags(current) != (0, []):
        fail("pre-completion checkpoint has a pending message")
    shared.check_ack_contract(current, 43)
    check_status_contract(current, 34)

    ended = expect_base_state(current, "manufacture_after_clock", geo_screen=0,
                              funds=START_FUNDS - ITEM_COST)
    if shared.field(ended, "sec") - shared.field(loaded, "sec") != 44 * 3601:
        fail("manufacture did not advance exactly 44 normal one-hour pulses")
    if shared.field(ended, "tickSec") != 3600 or shared.field(ended, "pause") != 0 \
            or shared.field(ended, "funds") != START_FUNDS - ITEM_COST \
            or shared.field(ended, "researchAssigned") != 0 \
            or shared.field(ended, "engineerAssigned") != 0:
        fail(f"production did not debit the raw price and release Engineers: {ended!r}")
    if shared.pair_field(ended, "engineersAvailable/Total") != (10, 10) \
            or shared.pair_field(ended, "scientistsAvailable/Total") != (10, 10):
        fail(f"production did not return all ten Engineers and Scientists: {ended!r}")
    stock = shared.stock_rows(current, "manufacture_after_clock")
    if stock != {**START_BASE, ITEM: 1}:
        fail(f"manufacture did not add exactly one Motion Scanner: {stock!r}")
    expect_no_materials(stock, "manufacture output")
    if ITEM in product_rows(current, "manufacture_after_clock"):
        fail("completed slot-20 request/allocation/work was not cleared")
    if shared.topic_rows(current, "manufacture_after_clock").get(TOPIC, (0, 0, 0, 0, 0))[1] != 1:
        fail("manufacture changed the completed topic-14 unlock")
    expect_unlocked_motion_scanner(current, "manufacture_after_clock")
    if shared.last_line_flags(current) != (0, []):
        fail("manufacture status handling left a pending message")
    shared.cargo_rows(current, "manufacture_after_clock")
    shared.save_written(current, 3)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"completed manufacture save is missing or empty: {saved}")
    print("ok: 43 hours left 431/432 work; hour 44 made one slot-20 item, charged 31,500, and released all staff")


def mode_manuf_final(clock_log_path: str, final_log_path: str, save_path: str,
                     main_path: str, lang_path: str) -> None:
    verify_raw_contract(main_path, lang_path)
    clock_lines = read_lines(clock_log_path)
    final = read_lines(final_log_path)
    saved_state = expect_base_state(clock_lines, "manufacture_after_clock", geo_screen=0,
                                    funds=START_FUNDS - ITEM_COST)
    loaded = expect_base_state(final, "manufacture_final_load", geo_screen=0,
                               funds=START_FUNDS - ITEM_COST)
    shared.expect_cold_load(loaded, "manufacture_final_load")
    for name in ("baseCount", "selectedBase", "funds", "sec", "pause", "sites",
                 "researchAssigned", "engineerAssigned", "engineersAvailable/Total",
                 "scientistsAvailable/Total"):
        saved_value = shared.pair_field(saved_state, name) if "/" in name else shared.field(saved_state, name)
        loaded_value = shared.pair_field(loaded, name) if "/" in name else shared.field(loaded, name)
        if saved_value != loaded_value:
            fail(f"fresh load changed completed-manufacture field {name}")
    if shared.field(loaded, "funds") != START_FUNDS - ITEM_COST \
            or shared.pair_field(loaded, "engineersAvailable/Total") != (10, 10) \
            or shared.pair_field(loaded, "scientistsAvailable/Total") != (10, 10):
        fail(f"fresh load lost the paid output or staff release: {loaded!r}")
    expected_stock = {**START_BASE, ITEM: 1}
    if shared.stock_rows(clock_lines, "manufacture_after_clock") != expected_stock \
            or shared.stock_rows(final, "manufacture_final_load") != expected_stock:
        fail("saved and loaded Base stock do not contain exactly one slot-20 item")
    if product_rows(clock_lines, "manufacture_after_clock") \
            or product_rows(final, "manufacture_final_load"):
        fail("a product request, assignment, committed count, or work value survived completion")
    expect_no_materials(shared.stock_rows(final, "manufacture_final_load"), "fresh output load")
    if shared.topic_rows(final, "manufacture_final_load") != shared.topic_rows(clock_lines, "manufacture_after_clock"):
        fail("fresh output load changed saved Research rows")
    if shared.topic_rows(final, "manufacture_final_load").get(TOPIC, (0, 0, 0, 0, 0))[1] != 1:
        fail("fresh output load lost the topic-14 unlock")
    expect_unlocked_motion_scanner(final, "manufacture_final_load")
    if shared.last_line_flags(clock_lines) != (0, []) or shared.last_line_flags(final) != (0, []):
        fail("completed or loaded state has a pending status line")
    if shared.cargo_rows(clock_lines, "manufacture_after_clock") \
            != shared.cargo_rows(final, "manufacture_final_load"):
        fail("fresh output load changed saved craft cargo")
    shared.save_written(clock_lines, 3)
    saved = Path(save_path)
    if not saved.is_file() or saved.stat().st_size == 0:
        fail(f"completed output save is missing or empty: {saved}")
    print("ok: fresh load kept one Motion Scanner, the 31,500 debit, and ten free Engineers")


def main() -> int:
    if len(sys.argv) < 2:
        fail("usage: newgame-probe|research-start|clock-start|clock-stage|clock-next|manuf-load|manuf-probe|manuf-order|pending-probe|manuf-clock|manuf-final")
    mode = sys.argv[1]
    if mode == "newgame-probe" and len(sys.argv) == 7:
        mode_newgame_probe(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
    elif mode == "research-start" and len(sys.argv) == 6:
        mode_research_start(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "clock-start" and len(sys.argv) == 6:
        mode_clock_start(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "clock-stage" and len(sys.argv) == 8:
        mode_clock_stage(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6], sys.argv[7])
    elif mode == "clock-next" and len(sys.argv) == 6:
        mode_clock_next(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "manuf-load" and len(sys.argv) == 7:
        mode_manuf_load_probe(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
    elif mode == "manuf-probe" and len(sys.argv) == 7:
        mode_manuf_probe(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
    elif mode == "manuf-order" and len(sys.argv) == 4:
        mode_manuf_order(sys.argv[2], sys.argv[3])
    elif mode == "pending-probe" and len(sys.argv) == 5:
        mode_pending_probe(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "manuf-clock" and len(sys.argv) == 8:
        mode_manuf_clock(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6], sys.argv[7])
    elif mode == "manuf-final" and len(sys.argv) == 7:
        mode_manuf_final(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
    else:
        fail(f"bad arguments for mode {mode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
