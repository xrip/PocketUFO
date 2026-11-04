#!/usr/bin/env python3
"""Check the live Alien Alloys to Personal Armour strategy path."""

from __future__ import annotations

from pathlib import Path
import math
import re
import sys

RECOVERED = (105, 106, 107, 109)


def fail(message: str) -> None:
    print(f"STRATEGY MANUFACTURE FAILED: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_lines(path: str) -> list[bytes]:
    return Path(path).read_bytes().splitlines()


def one_line(lines: list[bytes], prefix: bytes) -> bytes:
    found = [line for line in lines if line.startswith(prefix)]
    if len(found) != 1:
        fail(f"expected one line starting with {prefix!r}, found {len(found)}")
    return found[0]


def state_tuple(lines: list[bytes], label: str) -> bytes:
    return one_line(lines, f"pocketufo: strategy tuple {label}:".encode()).split(b": ", 2)[2]


def number(data: bytes, name: str) -> int:
    match = re.search(rb"(?:^| )" + re.escape(name.encode()) + rb" (-?[0-9]+)(?: |$)", data)
    if match is None:
        fail(f"tuple has no {name} field: {data!r}")
    return int(match.group(1))


def geo_screen(data: bytes) -> int:
    match = re.search(rb"(?:^| )screen [0-9]+/([0-9]+)(?: |$)", data)
    if match is None:
        fail(f"tuple has no geoscape screen field: {data!r}")
    return int(match.group(1))


def topic_line(lines: list[bytes], label: str, topic: int) -> bytes:
    return one_line(lines, f"pocketufo: strategy topic {label}: id {topic} ".encode())


def line_list_after(lines: list[bytes], label: str) -> tuple[bytes, list[bytes]]:
    prefix = f"pocketufo: strategy tuple {label}:".encode()
    start = next((i for i, line in enumerate(lines) if line.startswith(prefix)), None)
    if start is None:
        fail(f"strategy tuple is missing before line-list check {label}")
    list_index = next((i for i in range(start + 1, len(lines))
                       if lines[i].startswith(b"pocketufo: lines:")), None)
    if list_index is None:
        fail(f"line-list output is missing after {label}")
    end = next((i for i in range(list_index + 1, len(lines))
                if lines[i].startswith(b"pocketufo:")), len(lines))
    return lines[list_index], lines[list_index + 1:end]


def stock(lines: list[bytes], label: str, slot: int) -> int:
    prefix = f"pocketufo: strategy stock {label}:".encode()
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"slot ([0-9]+) qty ([0-9]+)$", line)
        if match is not None and int(match.group(1)) == slot:
            return int(match.group(2))
    return 0


def recovered_stock(lines: list[bytes], label: str) -> dict[int, int]:
    result = {slot: stock(lines, label, slot) for slot in RECOVERED}
    if result[106] < 4:
        fail(f"base 0 lacks four recovered alloy items at {label}: {result!r}")
    return result


def product_rows(lines: list[bytes], label: str) -> list[tuple[int, int, int, tuple[int, ...], tuple[int, ...]]]:
    prefix = f"pocketufo: strategy unlocked-product {label}:".encode()
    result = []
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(
            rb"topic ([0-9]+) item ([0-9]+) work ([0-9]+) comps ([0-9]+)/([0-9]+)/([0-9]+)/([0-9]+) "
            rb"stock ([0-9]+)/([0-9]+)/([0-9]+)/([0-9]+)$",
            line[len(prefix) :],
        )
        if match is not None:
            values = tuple(map(int, match.groups()))
            result.append((values[0], values[1], values[2], values[3:7], values[7:11]))
    return result


def row_id(lines: list[bytes], kind: str, label: str, item: int) -> int:
    prefix = f"pocketufo: strategy row {label}: {kind} ".encode()
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.fullmatch(rb"([0-9]+) aux ([0-9]+)", line[len(prefix) :])
        if match is not None and int(match.group(2)) == item:
            return int(match.group(1))
    fail(f"live {kind} panel has no row for id {item} at {label}")


def panel_geometry(lines: list[bytes], kind: str, label: str) -> tuple[int, int, int, int]:
    line = one_line(lines, f"pocketufo: strategy panel {label}:".encode())
    match = re.search(re.escape(kind.encode())
                      + rb" rows ([0-9]+) pos ([0-9]+)/([0-9]+) font ([0-9]+)$", line)
    if match is None:
        fail(f"{kind} panel geometry is missing: {line!r}")
    return tuple(map(int, match.groups()))


def control_center(lines: list[bytes], group: str, index: int) -> tuple[int, int]:
    prefix = f"pocketufo: strategy control {group} {index} ".encode()
    lines_for_control = [line for line in lines if line.startswith(prefix)]
    valid = []
    for line in lines_for_control:
        match = re.search(
            rb"present 1 enabled 1 rect (-?[0-9]+)/(-?[0-9]+)/([0-9]+)/([0-9]+)"
            rb"(?: mode [0-9]+ state [0-9]+)? callback 1$",
            line,
        )
        if match is not None:
            valid.append((line, match))
    if len(valid) != 1:
        fail(f"expected one enabled, wired {group} control {index}; found {len(valid)} of {len(lines_for_control)} records")
    line, match = valid[0]
    x, y, width, height = map(int, match.groups())
    if width <= 0 or height <= 0:
        fail(f"live {group} control {index} has an empty rect: {line!r}")
    return x + width // 2, y + height // 2


def generate_research_flow(probe_path: str, previous_path: str, output_path: str,
                           template_path: str, topic: int, blocks: int) -> None:
    label = f"research{topic}_probe"
    probe = read_lines(probe_path)
    previous = read_lines(previous_path)
    loaded = state_tuple(probe, f"research{topic}_probe_loaded")
    prior_label = "staff_arrived" if topic == 85 else "research85_complete"
    if state_tuple(previous, prior_label) != loaded:
        fail(f"fresh load changed state before topic {topic} selection")
    state = state_tuple(probe, label)
    if geo_screen(state) != 8 or number(state, "selectedBase") != 0:
        fail(f"topic {topic} probe is not on base 0's research screen: {state!r}")
    active = topic_line(probe, label, topic)
    if not re.search(rb"active 1 unlocked 0", active):
        fail(f"topic {topic} is not active in the live research list: {active!r}")
    if topic == 94:
        prior = topic_line(probe, label, 85)
        if not re.search(rb"unlocked 1", prior):
            fail(f"topic 94 loaded without its completed topic-85 prerequisite: {prior!r}")
    row = row_id(probe, "research", label, topic)
    if row >= 15:
        fail(f"topic {topic} row {row} is outside the visible research page")
    _count, _x, top, font = panel_geometry(probe, "research", label)
    row_y = top + row * font + font // 2

    text = Path(template_path).read_text(encoding="utf-8")
    text = text.replace(f"@RESEARCH{topic}_ROW_Y@", str(row_y))
    text = text.replace(f"__RESEARCH{topic}_SCIENTISTS__",
                        "\n".join("click 15 171" for _ in range(16)))
    clock_commands = [command for _ in range(blocks)
                      for command in ("tick 8", "click 29 186")]
    if topic in (85, 94):
        # Each measured clock run opens a real contact card and pauses. Close its
        # live Cancel control, then supply enough ordinary work hours to finish.
        tail_blocks = 13 if topic == 85 else 17
        clock_commands.extend((f"strategystate research{topic}_before_contact_cancel", "lines",
                               "click 120 169", "tick 2",
                               f"strategystate research{topic}_contact_cancelled"))
        clock_commands.extend(command for _ in range(tail_blocks)
                              for command in ("tick 8", "click 29 186"))
    text = text.replace(f"__RESEARCH{topic}_CLOCK__", "\n".join(clock_commands))
    if "@RESEARCH" in text or "__RESEARCH" in text:
        fail(f"generated topic {topic} flow still has a placeholder")
    Path(output_path).write_text(text, encoding="utf-8", newline="\n")
    suffix = f"; one live contact cancel plus {13 if topic == 85 else 17} more 8-hour blocks" \
        if topic in (85, 94) else ""
    print(f"ok: live topic {topic} row {row}; sixteen scientists; {blocks * 8} base hours{suffix}")


def check_staff(log_path: str, save_path: str) -> None:
    lines = read_lines(log_path)
    before = state_tuple(lines, "manufacture_setup_loaded")
    order = state_tuple(lines, "staff_order")
    arrived = state_tuple(lines, "staff_arrived")
    if number(before, "baseCount") != 1 or number(before, "funds") != 4147000:
        fail(f"input is not the measured victory save: {before!r}")
    if number(arrived, "baseCount") != 1 or number(arrived, "selectedBase") != 0 or geo_screen(arrived) != 0:
        fail(f"staff did not arrive at base 0 on the map: {arrived!r}")
    if number(order, "funds") != 2387000:
        fail(f"the live shop did not charge the real sixteen-plus-sixteen staff order: {order!r}")
    if not re.search(rb"engineersAvailable/Total 16/16", arrived):
        fail(f"sixteen engineers did not arrive: {arrived!r}")
    if not re.search(rb"scientistsAvailable/Total 16/16", arrived):
        fail(f"sixteen scientists did not arrive: {arrived!r}")
    materials = recovered_stock(lines, "staff_arrived")
    pending_items = [line for line in lines if line.startswith(b"pocketufo: strategy site-item staff_pending:")]
    if not any(re.search(rb"site 1 slot 112 qty 16$", line) for line in pending_items):
        fail(f"the live pending shop site did not carry sixteen engineers: {pending_items!r}")
    if not any(re.search(rb"site 1 slot 113 qty 16$", line) for line in pending_items):
        fail(f"the live pending shop site did not carry sixteen scientists: {pending_items!r}")
    if not re.search(rb"pocketufo: save slot 1: written", b"\n".join(lines)):
        fail("staff and recovered materials were not saved to private slot 1")
    save = Path(save_path)
    if not save.is_file() or save.stat().st_size == 0:
        fail(f"private staff save is missing or empty: {save}")
    print(f"ok: live shop delivered sixteen engineers and scientists; recovered stock {materials!r}")


def check_research_complete(log_path: str, save_path: str, topic: int) -> None:
    lines = read_lines(log_path)
    label = f"research{topic}_complete"
    completed = topic_line(lines, label, topic)
    if not re.search(rb"unlocked 1", completed):
        fail(f"real hourly research did not unlock topic {topic}: {completed!r}")
    if topic in (85, 94):
        interrupted_label = f"research{topic}_before_contact_cancel"
        resumed_label = f"research{topic}_contact_cancelled"
        expected_contact_slot = 4 if topic == 85 else 6
        interrupted = state_tuple(lines, interrupted_label)
        if number(interrupted, "pause") != 1:
            fail(f"the expected live contact did not pause the research clock: {interrupted!r}")
        line_header, line_entries = line_list_after(lines, interrupted_label)
        contact_flag = rb"\[0\] flag 10 slots " + str(expected_contact_slot).encode()
        if not re.search(rb"lines: 1 of", line_header) or not any(
            re.search(contact_flag, line) for line in line_entries
        ):
            fail(f"the live contact card was not the observed flag-10 event: {line_header!r} {line_entries!r}")
        resumed = state_tuple(lines, resumed_label)
        if number(resumed, "pause") != 0 or number(resumed, "sec") <= number(interrupted, "sec"):
            fail(f"the live Cancel input did not resume the normal clock: {interrupted!r} -> {resumed!r}")
    if topic == 85:
        next_topic = topic_line(lines, label, 94)
        if not re.search(rb"active 1 unlocked 0", next_topic):
            fail(f"topic85 did not activate topic94: {next_topic!r}")
        if not re.search(rb"scientistsAvailable/Total 16/16", state_tuple(lines, label)):
            fail("completed topic85 did not free its sixteen scientists")
        save_slot = 2
    elif topic == 94:
        product = [row for row in product_rows(lines, label) if row[0] == 94 and row[1] == 122]
        if len(product) != 1:
            fail(f"topic94 did not expose a unique Personal Armour product: {product!r}")
        _topic, _item, work, components, quantities = product[0]
        if work != 1584 or components != (0, 4, 0, 0) or quantities[1] < 4:
            fail(f"slot122 recipe or recovered stock differs: {product[0]!r}")
        if stock(lines, label, 106) < 4:
            fail("base0 does not hold four recovered Alien Alloys")
        save_slot = 3
    else:
        fail(f"unexpected research topic {topic}")
    if not re.search(rb"pocketufo: save slot " + str(save_slot).encode() + rb": written",
                     b"\n".join(lines)):
        fail(f"topic {topic} result was not saved to private slot {save_slot}")
    save = Path(save_path)
    if not save.is_file() or save.stat().st_size == 0:
        fail(f"private research save is missing or empty: {save}")
    print(f"ok: topic {topic} completed through the real UI/clock path and was saved")


def generate_manuf_flow(probe_path: str, output_path: str, template_path: str) -> None:
    lines = read_lines(probe_path)
    state = state_tuple(lines, "manuf_probe")
    if geo_screen(state) != 9 or number(state, "baseCount") != 1 or number(state, "selectedBase") != 0:
        fail(f"manufacturing probe is not on base0: {state!r}")
    topic = topic_line(lines, "manuf_probe", 94)
    if not re.search(rb"unlocked 1", topic):
        fail(f"Personal Armour is not unlocked: {topic!r}")
    materials = recovered_stock(lines, "manuf_probe")
    product = [row for row in product_rows(lines, "manuf_probe") if row[0] == 94 and row[1] == 122]
    if len(product) != 1:
        fail(f"live product list lacks unique slot122: {product!r}")
    _topic, _item, work, components, quantities = product[0]
    if work != 1584 or components != (0, 4, 0, 0) or quantities[1] < 4 or materials[106] < 4:
        fail(f"slot122 cannot use the recovered alloys at base0: {product[0]!r} / {materials!r}")
    row = row_id(lines, "manuf", "manuf_probe", 122)
    if row >= 13:
        fail(f"slot122 row {row} is outside the visible product page")
    _count, _x, top, font = panel_geometry(lines, "manuf", "manuf_probe")
    engx, engy = control_center(lines, "manuf", 2)
    unitx, unity = control_center(lines, "manuf", 4)
    text = Path(template_path).read_text(encoding="utf-8")
    text = text.replace("@MANUF_ROW_Y@", str(top + row * font + font // 2))
    text = text.replace("@MANUF_ENGINEER_X@", str(engx)).replace("@MANUF_ENGINEER_Y@", str(engy))
    text = text.replace("@MANUF_UNIT_X@", str(unitx)).replace("@MANUF_UNIT_Y@", str(unity))
    text = text.replace("__MANUF_ENGINEER_CLICKS__",
                        "\n".join(f"click {engx} {engy}" for _ in range(16)))
    if "@MANUF_" in text or "__MANUF_" in text:
        fail("generated slot122 flow still has a placeholder")
    Path(output_path).write_text(text, encoding="utf-8", newline="\n")
    print(f"ok: real product row122 uses four of {materials[106]} recovered alloys")


def product_state(lines: list[bytes], label: str) -> tuple[int, int, int, int] | None:
    prefix = b"pocketufo: strategy product " + label.encode() + b": item 122 "
    found = [line for line in lines if line.startswith(prefix)]
    if len(found) > 1:
        fail(f"multiple slot122 production records at {label}: {found!r}")
    if not found:
        return None
    match = re.search(rb"request ([0-9]+) engineers ([0-9]+) committed ([0-9]+) accumulated ([0-9]+)$", found[0])
    if match is None:
        fail(f"slot122 production record is malformed: {found[0]!r}")
    return tuple(map(int, match.groups()))


def generate_completion_flow(commit_lines: list[bytes], output_path: str, template_path: str) -> None:
    order = product_state(commit_lines, "manuf_after_hour")
    if order is None or order[0] != 1 or order[1] != 16 or order[2] != 0 or order[3] <= 0:
        fail(f"slot122 was not saved as an ongoing sixteen-engineer order: {order!r}")
    hours = math.ceil((1584 - order[3]) / 16)
    blocks = max(1, math.ceil((hours + 25) / 25))
    text = Path(template_path).read_text(encoding="utf-8")
    commands = "\n".join(command for _ in range(blocks)
                          for command in ("tick 25", "click 29 186"))
    text = text.replace("__MANUF_WORK_CLOCK__", commands)
    if "__MANUF_WORK_CLOCK__" in text:
        fail("generated work flow still has a placeholder")
    if len(text.encode("utf-8")) >= 7900:
        fail("generated work flow exceeds the App script input buffer")
    Path(output_path).write_text(text, encoding="utf-8", newline="\n")
    print(f"ok: saved item122 needs about {hours} hourly pulses; generated {blocks * 25}")


def check_manuf_commit(probe_path: str, commit_path: str, save_path: str,
                       load_output: str, load_template: str) -> None:
    probe = read_lines(probe_path)
    lines = read_lines(commit_path)
    allocated = product_state(lines, "manuf_allocated")
    if allocated is None or allocated[0] != 1 or allocated[1] != 16 or allocated[2] != 0:
        fail(f"normal controls did not set one unit and sixteen engineers: {allocated!r}")
    before = state_tuple(lines, "manuf_before_hour")
    after = state_tuple(lines, "manuf_after_hour")
    if geo_screen(before) != 0 or geo_screen(after) != 0 or number(after, "selectedBase") != 0:
        fail(f"manufacturing did not use base0's hourly path: {before!r} / {after!r}")
    pre_order = product_state(lines, "manuf_before_hour")
    after_order = product_state(lines, "manuf_after_hour")
    if pre_order is None or after_order is None or after_order[3] <= pre_order[3]:
        fail(f"one real hour did not advance product122 work: {pre_order!r} -> {after_order!r}")
    if after_order[0] != 1 or after_order[1] != 16 or after_order[2] != 0:
        fail(f"manufacture order is not still active at the save point: {after_order!r}")
    if stock(lines, "manuf_after_hour", 122) != stock(probe, "manuf_probe", 122):
        fail("Personal Armour completed before the fresh-load checkpoint")
    if stock(lines, "manuf_after_hour", 106) != stock(probe, "manuf_probe", 106):
        fail("alloys were consumed before product output")
    if number(after, "funds") != number(before, "funds"):
        fail("funds changed before the product was completed")
    if not re.search(rb"pocketufo: save slot 0: written", b"\n".join(lines)):
        fail("ongoing production was not saved to private slot0")
    save = Path(save_path)
    if not save.is_file() or save.stat().st_size == 0:
        fail(f"private ongoing-order save is missing: {save}")
    generate_completion_flow(lines, load_output, load_template)
    print(f"ok: saved ongoing Personal Armour order after real work {after_order[3]}/1584")


def check_manuf_output(probe_path: str, commit_path: str, load_path: str, save_path: str) -> None:
    probe = read_lines(probe_path)
    committed = read_lines(commit_path)
    loaded = read_lines(load_path)
    saved_state = state_tuple(committed, "manuf_after_hour")
    load_state = state_tuple(loaded, "manufacture_after_load")
    if saved_state != load_state:
        fail(f"fresh load changed the active order: {saved_state!r} -> {load_state!r}")
    saved_order = product_state(committed, "manuf_after_hour")
    loaded_order = product_state(loaded, "manufacture_after_load")
    if saved_order is None or saved_order != loaded_order:
        fail(f"fresh load changed the saved product122 work: {saved_order!r} -> {loaded_order!r}")
    final_state = state_tuple(loaded, "manufacture_after_work")
    if geo_screen(final_state) != 0 or number(final_state, "selectedBase") != 0:
        fail(f"Personal Armour output was not checked on base0's map: {final_state!r}")

    initial_alloys = stock(probe, "manuf_probe", 106)
    final_alloys = stock(loaded, "manufacture_after_work", 106)
    initial_product = stock(probe, "manuf_probe", 122)
    final_product = stock(loaded, "manufacture_after_work", 122)
    if final_alloys != initial_alloys - 4:
        fail(f"one product did not consume exactly four recovered alloys: {initial_alloys}->{final_alloys}")
    if final_product != initial_product + 1:
        fail(f"one Personal Armour did not enter base stock: {initial_product}->{final_product}")
    for slot in (105, 107, 109):
        if stock(loaded, "manufacture_after_work", slot) != stock(probe, "manuf_probe", slot):
            fail(f"unexpected recovered material change for slot {slot}")
    debit = number(load_state, "funds") - number(final_state, "funds")
    if debit != 63000:
        fail(f"slot122 debit was {debit}, expected 63,000 at the raw 0.7 multiplier")
    if product_state(loaded, "manufacture_after_work") is not None:
        fail("the one-unit product order remained active after output")
    if not re.search(rb"engineersAvailable/Total 16/16", final_state):
        fail(f"engineers were not free after production: {final_state!r}")
    if number(final_state, "sec") <= number(load_state, "sec"):
        fail("the fresh-process production clock did not advance")
    if not re.search(rb"pocketufo: save slot 4: written", b"\n".join(loaded)):
        fail("the completed Personal Armour was not saved to private slot4")
    save = Path(save_path)
    if not save.is_file() or save.stat().st_size == 0:
        fail(f"private completed-product save is missing or empty: {save}")
    print("ok: fresh load restored the ongoing order before time advance")
    print("ok: real manufacture made one Personal Armour, consumed four recovered alloys, and debited 63,000")


def campaign_section(lines: list[bytes], label: str, kind: bytes = b"state") -> list[bytes]:
    prefix = b"pocketufo: campaign " + kind + b" " + label.encode() + b":"
    start = next((i for i, line in enumerate(lines) if line.startswith(prefix)), None)
    if start is None:
        fail(f"campaign {kind.decode()} section is missing for {label}")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith(b"pocketufo:")), len(lines))
    return lines[start:end]


def base_soldier_indices(lines: list[bytes], label: str) -> list[int]:
    section = campaign_section(lines, label, b"roster")
    found: list[int] = []
    for line in section:
        match = re.match(rb"  roster soldier ([0-9]+) id -?[0-9]+ base ([0-9]+) ", line)
        if match is not None and int(match.group(2)) == 0:
            found.append(int(match.group(1)))
    if not found:
        fail(f"no live base-0 soldier is available for the normal name-row click at {label}")
    return found


def campaign_soldier(lines: list[bytes], label: str, index: int) -> tuple[bytes, int, int]:
    section = campaign_section(lines, label)
    prefix = f"  roster {index} id ".encode()
    found = [line for line in section if line.startswith(prefix)]
    if len(found) != 1:
        fail(f"expected one live roster row {index} at {label}, found {len(found)}")
    match = re.match(rb"  roster [0-9]+ id -?[0-9]+ name (.*?) type ([0-9]+) armor (-?[0-9]+) ", found[0])
    if match is None:
        fail(f"campaign roster row is malformed at {label}: {found[0]!r}")
    name, unit_type, armor_points = match.groups()
    if not name:
        fail(f"campaign roster row {index} has no soldier name at {label}")
    return name, int(unit_type), int(armor_points)


def choose_gear_soldier(lines: list[bytes]) -> tuple[int, int, tuple[bytes, int, int]]:
    indices = base_soldier_indices(lines, "gear_roster_before")
    for row, index in enumerate(indices):
        soldier = campaign_soldier(lines, "gear_before", index)
        if soldier[1] != 1:
            return index, row, soldier
    fail("all live base-0 soldiers already have type 1, so no real type transition is available")


def check_gear_probe(log_path: str, flow_output: str, template_path: str) -> None:
    lines = read_lines(log_path)
    loaded = state_tuple(lines, "gear_checkpoint_loaded")
    if number(loaded, "baseCount") != 1 or number(loaded, "selectedBase") != 0 or geo_screen(loaded) != 0:
        fail(f"gear checkpoint is not the measured base-0 map: {loaded!r}")
    if stock(lines, "gear_checkpoint_loaded", 122) != 1:
        fail("the fresh-loaded checkpoint does not contain one manufactured Personal Armour")
    roster_index, soldier_row, before = choose_gear_soldier(lines)
    if geo_screen(state_tuple(lines, "gear_nav_ready")) != 1:
        fail("the real root Build click did not open the base screen")
    nav_x, nav_y = control_center(lines, "nav", 2)
    text = Path(template_path).read_text(encoding="utf-8")
    text = text.replace("@GEAR_NAV_X@", str(nav_x)).replace("@GEAR_NAV_Y@", str(nav_y))
    text = text.replace("@GEAR_SOLDIER_ROW_Y@", str(70 + 18 * soldier_row + 9))
    if "@GEAR_" in text:
        fail("generated Soldier Gear flow still has a placeholder")
    Path(flow_output).write_text(text, encoding="utf-8", newline="\n")
    print(f"ok: measured Soldier nav at {nav_x}/{nav_y}; base-0 row {soldier_row} selects roster {roster_index} {before[0]!r}")


def check_gear_equip(log_path: str, save_path: str) -> tuple[int, bytes, int, int]:
    lines = read_lines(log_path)
    roster_index, _soldier_row, before = choose_gear_soldier(lines)
    equipped = campaign_soldier(lines, "gear_equipped", roster_index)
    saved = campaign_soldier(lines, "gear_saved", roster_index)
    if before[0] != equipped[0] or before[0] != saved[0]:
        fail("the first base-0 soldier's saved name changed during the live equip flow")
    if equipped[1] != 1 or equipped[1] == before[1]:
        fail(f"the slot122 row did not set the raw aux-1 soldier type: {before!r} -> {equipped!r}")
    if equipped[2] != before[2]:
        fail(f"the gear click unexpectedly changed per-mission armor points before battle reset: {before!r} -> {equipped!r}")
    if saved != equipped:
        fail(f"closing to the map changed the equipped soldier: {equipped!r} -> {saved!r}")
    for x, y in ((104, 46), (60, 129)):
        expected_click = f"pocketufo: step click {x} {y}".encode()
        if expected_click not in lines:
            fail(f"the Soldier Gear flow did not use the expected normal pointer click at {x}/{y}")
    equipped_state = state_tuple(lines, "gear_equipped")
    saved_state = state_tuple(lines, "gear_saved")
    if geo_screen(equipped_state) != 5 or number(equipped_state, "selectedBase") != 0:
        fail(f"the armor row was not used from screen 5 at base 0: {equipped_state!r}")
    if geo_screen(saved_state) != 0 or number(saved_state, "selectedBase") != 0:
        fail(f"the gear state was not saved from base 0's map: {saved_state!r}")
    if roster_index not in base_soldier_indices(lines, "gear_roster_saved"):
        fail("the equipped soldier is no longer assigned to base 0 at save time")
    if stock(lines, "gear_checkpoint_loaded", 122) != 1 or stock(lines, "gear_equipped", 122) != 0:
        fail("the live UI did not consume the one manufactured slot122 item")
    if not re.search(rb"pocketufo: save slot 5: written", b"\n".join(lines)):
        fail("the equipped soldier was not saved to private slot 5")
    save = Path(save_path)
    if not save.is_file() or save.stat().st_size == 0:
        fail(f"private equipped-soldier save is missing or empty: {save}")
    print(f"ok: live Soldier Gear input used slot122; soldier {roster_index} {before[0]!r} type {before[1]} -> {equipped[1]} with armor points unchanged at {equipped[2]}")
    print(f"ok: one manufactured item left base stock and equipped state saved ({save.stat().st_size} bytes)")
    return roster_index, before[0], equipped[1], equipped[2]


def check_gear_load(equip_path: str, load_path: str) -> None:
    equipped_lines = read_lines(equip_path)
    loaded_lines = read_lines(load_path)
    roster_index, _soldier_row, _before = choose_gear_soldier(equipped_lines)
    if roster_index not in base_soldier_indices(equipped_lines, "gear_roster_saved"):
        fail("the equipped soldier is no longer assigned to base 0 in the saved state")
    if roster_index not in base_soldier_indices(loaded_lines, "gear_roster_loaded"):
        fail("fresh load changed the equipped soldier's base-0 roster assignment")
    saved = campaign_soldier(equipped_lines, "gear_saved", roster_index)
    loaded = campaign_soldier(loaded_lines, "gear_after_load", roster_index)
    if loaded != saved:
        fail(f"fresh load changed the equipped soldier's name/type/armor: {saved!r} -> {loaded!r}")
    state = state_tuple(loaded_lines, "gear_after_load")
    if geo_screen(state) != 0 or number(state, "selectedBase") != 0:
        fail(f"fresh load did not restore the equipped base-0 map: {state!r}")
    if stock(loaded_lines, "gear_after_load", 122) != 0:
        fail("fresh load restored slot122 stock after the suit was equipped")
    print(f"ok: fresh process preserved soldier {roster_index} {loaded[0]!r} type {loaded[1]} armor {loaded[2]} and zero slot122 stock")


def main() -> int:
    if len(sys.argv) < 2:
        fail("usage: check_strategy_manufacture.py staff|research-probe|research-complete|manuf-probe|manuf-commit|manuf-output|gear-probe|gear-equip|gear-load ...")
    mode = sys.argv[1]
    if mode == "staff" and len(sys.argv) == 4:
        check_staff(sys.argv[2], sys.argv[3])
    elif mode == "research-probe" and len(sys.argv) == 7:
        topic = int(sys.argv[2])
        blocks = 40 if topic == 85 else 30
        generate_research_flow(sys.argv[4], sys.argv[3], sys.argv[5], sys.argv[6], topic, blocks)
    elif mode == "research-complete" and len(sys.argv) == 5:
        check_research_complete(sys.argv[2], sys.argv[3], int(sys.argv[4]))
    elif mode == "manuf-probe" and len(sys.argv) == 5:
        generate_manuf_flow(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "manuf-commit" and len(sys.argv) == 7:
        check_manuf_commit(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
    elif mode == "manuf-output" and len(sys.argv) == 6:
        check_manuf_output(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif mode == "gear-probe" and len(sys.argv) == 5:
        check_gear_probe(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "gear-equip" and len(sys.argv) == 4:
        check_gear_equip(sys.argv[2], sys.argv[3])
    elif mode == "gear-load" and len(sys.argv) == 4:
        check_gear_load(sys.argv[2], sys.argv[3])
    else:
        fail("wrong arguments for selected mode")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
