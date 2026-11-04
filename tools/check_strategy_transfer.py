#!/usr/bin/env python3
"""Check the private real-UI two-base transfer regression."""

from __future__ import annotations

from pathlib import Path
import re
import sys


def fail(message: str) -> None:
    print(f"STRATEGY TRANSFER FAILED: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_lines(path: str) -> list[bytes]:
    return Path(path).read_bytes().splitlines()


def one_line(lines: list[bytes], prefix: bytes) -> bytes:
    found = [line for line in lines if line.startswith(prefix)]
    if len(found) != 1:
        fail(f"expected one line beginning {prefix!r}, found {len(found)}")
    return found[0]


def tuple_for(lines: list[bytes], label: str) -> bytes:
    return one_line(lines, f"pocketufo: strategy tuple {label}:".encode()).split(b": ", 2)[2]


def field(data: bytes, name: str) -> int:
    match = re.search(rb"(?:^| )" + re.escape(name.encode()) + rb" (-?[0-9]+)(?: |$)", data)
    if match is None:
        fail(f"tuple has no {name} field: {data!r}")
    return int(match.group(1))


def geo_screen(data: bytes) -> int:
    match = re.search(rb"(?:^| )screen [0-9]+/([0-9]+)(?: |$)", data)
    if match is None:
        fail(f"tuple has no geoscape screen field: {data!r}")
    return int(match.group(1))


def rows(lines: list[bytes], label: str, slot: int) -> list[tuple[int, int, int, int]]:
    prefix = f"pocketufo: strategy transfer-row {label}:".encode()
    result = []
    for line in lines:
        if not line.startswith(prefix):
            continue
        match = re.search(rb"row ([0-9]+) slot (-?[0-9]+) source (-?[0-9]+) pending (-?[0-9]+)$", line)
        if match is not None:
            row, aux, source, pending = map(int, match.groups())
            if aux == slot:
                result.append((row, aux, source, pending))
    return result


def control_rect(lines: list[bytes], index: int) -> tuple[int, int, int, int]:
    prefix = f"pocketufo: strategy control transfer {index} ".encode()
    matches = [line for line in lines if line.startswith(prefix)]
    if not matches:
        fail(f"no live transfer control {index} was reported")
    for line in reversed(matches):
        match = re.search(rb"present 1 enabled 1 rect (-?[0-9]+)/(-?[0-9]+)/([0-9]+)/([0-9]+)", line)
        if match is not None:
            return tuple(map(int, match.groups()))
    fail(f"transfer control {index} was never enabled: {matches[-1]!r}")


def panel_view(lines: list[bytes], label: str) -> tuple[int, int, int, int, int, int]:
    line = one_line(lines, f"pocketufo: strategy panel {label}:".encode())
    match = re.search(
        rb"shop rows ([0-9]+) pos (-?[0-9]+)/(-?[0-9]+) font ([0-9]+) style ([0-9]+) scroll (-?[0-9]+)$",
        line,
    )
    if match is None:
        fail(f"live transfer panel geometry is incomplete: {line!r}")
    return tuple(map(int, match.groups()))


def transfer_scroll_clicks(lines: list[bytes], label: str, row: int, target_scroll: int) -> tuple[str, int]:
    _count, _x, _y, _font, style, start_scroll = panel_view(lines, label)
    if row < 0 or row >= _count:
        fail(f"source row {row} is outside the live list of {_count}: {label}")
    required_scroll = max(start_scroll, row - style + 1)
    if target_scroll < required_scroll:
        fail(f"row {row} is still outside the live {style}-row page at scroll {target_scroll}")
    nClicks = target_scroll - start_scroll
    if nClicks == 0:
        return "", 0
    x, y, width, height = control_rect(lines, 1)
    nClickX, nClickY = x + width // 2, y + height // 2
    commands = "\n".join(command for _ in range(nClicks)
                            for command in (f"click {nClickX} {nClickY}", "tick 1"))
    return commands, nClicks


def check_two_base_ready(lines: list[bytes], label: str) -> bytes:
    state = tuple_for(lines, label)
    if field(state, "baseCount") != 2 or field(state, "selectedBase") != 1:
        fail(f"second-base setup did not leave base 1 selected: {state!r}")
    if geo_screen(state) != 0 or field(state, "sites") < 0:
        fail(f"setup did not return to the map: {state!r}")
    if not re.search(rb" stores 300 ", state):
        fail(f"base 1 has no live 300-unit General Stores capacity: {state!r}")
    fac_blocks = [i for i, line in enumerate(lines) if line == b"pocketufo: grid base 1"]
    if not fac_blocks:
        fail("no base-1 facility grid was printed")
    start = fac_blocks[-1]
    fac_line = next((line for line in lines[start + 1 :] if line.startswith(b"  facTyp row 1:")), None)
    days_line = next((line for line in lines[start + 1 :] if line.startswith(b"  days  row 1:")), None)
    if fac_line is None or days_line is None:
        fail("the final base-1 facility grid is incomplete")
    fac_values = [int(v) for v in fac_line.split(b":", 1)[1].split()]
    day_values = [int(v) for v in days_line.split(b":", 1)[1].split()]
    if len(fac_values) != 6 or fac_values[3] != 6 or fac_values[4] != 0:
        fail(f"base 1 does not show Stores beside its Access Lift: {fac_values!r}")
    if day_values[3] != 0:
        fail(f"the Stores build has not completed on the real clock: {day_values!r}")
    fac0 = [i for i, line in enumerate(lines) if line == b"pocketufo: grid base 0"]
    if not fac0:
        fail("no base-0 facility grid was printed")
    start0 = fac0[-1]
    fac0_line = next((line for line in lines[start0 + 1 :] if line.startswith(b"  facTyp row 2:")), None)
    days0_line = next((line for line in lines[start0 + 1 :] if line.startswith(b"  days  row 2:")), None)
    if fac0_line is None or days0_line is None:
        fail("the final base-0 facility grid is incomplete")
    fac0_values = [int(v) for v in fac0_line.split(b":", 1)[1].split()]
    days0_values = [int(v) for v in days0_line.split(b":", 1)[1].split()]
    if len(fac0_values) != 6 or fac0_values[1] != 6 or days0_values[1] != 0:
        fail(f"base 0's added Stores are missing or unfinished: {fac0_values!r} / {days0_values!r}")
    return state


def check_site(lines: list[bytes], label: str, expected_index: int,
               slot: int, qty: int) -> tuple[int, int]:
    site_prefix = f"pocketufo: strategy site {label}:".encode()
    site_lines = [line for line in lines if line.startswith(site_prefix)]
    matches = []
    for line in site_lines:
        match = re.search(rb"index ([0-9]+) flag ([0-9]+) target ([0-9]+)", line)
        if match is not None:
            index, flag, target = map(int, match.groups())
            if flag == 24:
                matches.append((index, target))
    expected = [entry for entry in matches if entry[0] == expected_index]
    if len(expected) != 1:
        fail(f"no unique committed transfer site at index {expected_index}: {matches!r}")
    item_prefix = f"pocketufo: strategy site-item {label}:".encode()
    item_ok = any(
        re.search(rb"site " + str(expected_index).encode() + rb" slot " + str(slot).encode()
                  + rb" qty " + str(qty).encode() + rb"$", line)
        for line in lines if line.startswith(item_prefix)
    )
    if not item_ok:
        fail(f"transfer site {expected_index} carries no slot {slot} quantity {qty} at {label}")
    return expected[0]


def emit_scroll_probe(open_path: str, output_path: str, template_path: str) -> None:
    lines = read_lines(open_path)
    state = tuple_for(lines, "transfer_open")
    if field(state, "baseCount") != 2 or field(state, "selectedBase") != 0 or geo_screen(state) != 10:
        fail(f"scroll probe did not open the transfer screen from base 0: {state!r}")
    count, _x, _y, _font, style, scroll = panel_view(lines, "transfer_open")
    candidates = rows(lines, "transfer_open", 68)
    if len(candidates) != 1 or candidates[0][2] < 1 or candidates[0][3] != 0:
        fail(f"slot 68 is not one live available transfer row: {candidates!r}")
    row = candidates[0][0]
    nTargetScroll = count - style
    if nTargetScroll < scroll:
        fail(f"transfer list starts beyond its real scroll clamp: {scroll}/{nTargetScroll}")
    commands, nNeeded = transfer_scroll_clicks(lines, "transfer_open", row, nTargetScroll)
    text = Path(template_path).read_text(encoding="utf-8")
    text = text.replace("@TRANSFER_SCROLL_CLICKS@", commands)
    if "@TRANSFER_" in text:
        fail("generated scroll probe still has a placeholder")
    Path(output_path).write_text(text, encoding="utf-8", newline="\n")
    print(f"ok: live slot68 row {row} needs {nNeeded} real Next clicks on the {style}-row page")


def emit_commit(probe_path: str, output_path: str, template_path: str) -> None:
    lines = read_lines(probe_path)
    state = tuple_for(lines, "transfer_probe")
    if field(state, "baseCount") != 2 or field(state, "selectedBase") != 0 or geo_screen(state) != 10:
        fail(f"probe did not open the transfer screen from base 0: {state!r}")
    for label in ("transfer_destination_selected", "transfer_scrolled", "transfer_probe"):
        work_line = one_line(lines, f"pocketufo: strategy transfer {label}:".encode())
        if not re.search(rb"built 1 picker 0 workBase 1 ", work_line):
            fail(f"the live draw did not keep base 1 selected at {label}: {work_line!r}")

    item_slot = 68  # starting Geoscape stock: 20 Silacoid corpses, carried through the real mission.
    candidates = rows(lines, "transfer_probe", item_slot)
    if len(candidates) != 1 or candidates[0][2] < 1 or candidates[0][3] != 0:
        fail(f"slot {item_slot} is not one live, available source row: {candidates!r}")
    row, _slot, source, _pending = candidates[0]
    _count, _x, nPanelY, nFont, nStyle, nScroll = panel_view(lines, "transfer_probe")
    if row < nScroll or row >= nScroll + nStyle:
        fail(f"slot {item_slot} row {row} is outside the scrolled page {nScroll}..{nScroll + nStyle - 1}")
    nVisibleRow = row - nScroll
    nRowY = nPanelY + nVisibleRow * nFont + nFont // 2
    scroll_commands, _nScrollClicks = transfer_scroll_clicks(lines, "transfer_open", row, nScroll)

    x, y, w, h = control_rect(lines, 3)
    nCommitX = x + w // 2
    nCommitY = y + h // 2
    text = Path(template_path).read_text(encoding="utf-8")
    text = text.replace("@TRANSFER_SCROLL_CLICKS@", scroll_commands)
    text = text.replace("@TRANSFER_ROW_Y@", str(nRowY))
    text = text.replace("@TRANSFER_COMMIT_X@", str(nCommitX))
    text = text.replace("@TRANSFER_COMMIT_Y@", str(nCommitY))
    if "@TRANSFER_" in text:
        fail("the generated transfer flow still has a placeholder")
    Path(output_path).write_text(text, encoding="utf-8", newline="\n")
    print(f"ok: live source row {row} is slot {item_slot}, quantity {source}; page {nScroll}, click y={nRowY}")
    print(f"ok: live commit control center is {nCommitX}/{nCommitY}")


def setup(log_path: str, save_path: str) -> None:
    lines = read_lines(log_path)
    cancelled = tuple_for(lines, "transfer_contact_cancelled")
    if geo_screen(cancelled) != 0 or field(cancelled, "pause") != 0:
        fail(f"the live UFO contact dialog did not close and resume the map clock: {cancelled!r}")
    completed = tuple_for(lines, "transfer_store_completion_acknowledged")
    if geo_screen(completed) != 0 or field(completed, "pause") != 0:
        fail(f"the Stores-complete card did not close and resume map input: {completed!r}")
    selected = tuple_for(lines, "transfer_setup_base1_selected")
    if (geo_screen(selected) != 1 or field(selected, "selectedBase") != 1
            or field(selected, "pause") != 0):
        fail(f"the live base-overview strip did not select base 1 while unpaused: {selected!r}")
    check_two_base_ready(lines, "transfer_setup_ready")
    stock_prefix = b"pocketufo: strategy stock victory_materials_at_base0:"
    materials = {}
    for line in lines:
        if not line.startswith(stock_prefix):
            continue
        match = re.search(rb"slot ([0-9]+) qty ([0-9]+)$", line)
        if match is not None:
            materials[int(match.group(1))] = int(match.group(2))
    recovered = {slot: materials.get(slot, 0) for slot in (105, 106, 107, 109)}
    if not any(qty > 0 for qty in recovered.values()):
        fail(f"the normal hourly site-recovery path delivered no victory materials to base 0: {recovered!r}")
    base0_state = tuple_for(lines, "victory_materials_at_base0")
    if field(base0_state, "stores") != 900:
        fail(f"the added base-0 Stores capacity did not become live: {base0_state!r}")
    print(f"ok: normal site recovery delivered victory materials to base 0: {recovered!r}")
    if not re.search(rb"pocketufo: save slot 1: written", b"\n".join(lines)):
        fail("setup did not write private slot 1")
    save = Path(save_path)
    if not save.is_file() or save.stat().st_size == 0:
        fail(f"private setup save is missing or empty: {save}")
    print(f"ok: real base 1 Access Lift and completed Stores are saved ({save.stat().st_size} bytes)")


def scroll_probe(log_path: str, output_path: str, template_path: str) -> None:
    emit_scroll_probe(log_path, output_path, template_path)


def newbase(log_path: str, save_path: str) -> None:
    lines = read_lines(log_path)
    before = tuple_for(lines, "victory_handoff_loaded")
    created = tuple_for(lines, "second_base_created")
    saved_state = tuple_for(lines, "second_base_map")
    if field(before, "baseCount") != 1 or field(before, "funds") != 4147000:
        fail(f"the private victory input did not match the measured one-base handoff: {before!r}")
    if field(created, "baseCount") != 2 or field(created, "selectedBase") != 1 or geo_screen(created) != 1:
        fail(f"the second-base Access Lift input did not commit: {created!r}")
    spent = field(before, "funds") - field(created, "funds")
    if spent < 300000 or spent > 1300000:
        fail(f"the real Access Lift/region commit charged an unexpected amount: {spent}")
    grids = [i for i, line in enumerate(lines) if line == b"pocketufo: grid base 1"]
    if not grids:
        fail("the new base's facility grid was not printed")
    start = grids[-1]
    fac_line = next((line for line in lines[start + 1 :] if line.startswith(b"  facTyp row 1:")), None)
    if fac_line is None:
        fail("the new base's Access Lift row is missing")
    fac_values = [int(v) for v in fac_line.split(b":", 1)[1].split()]
    if len(fac_values) != 6 or fac_values[4] != 0:
        fail(f"the live Access Lift did not occupy base 1 cell (4,1): {fac_values!r}")
    if field(saved_state, "baseCount") != 2 or geo_screen(saved_state) != 0:
        fail(f"the committed base was not saved from the map: {saved_state!r}")
    if not re.search(rb"pocketufo: save slot 1: written", b"\n".join(lines)):
        fail("the live second-base state was not saved to private slot 1")
    save = Path(save_path)
    if not save.is_file() or save.stat().st_size == 0:
        fail(f"private second-base save is missing or empty: {save}")
    print(f"ok: normal two-press Access Lift placement created base 1 and charged {spent}")
    print(f"ok: second-base state saved ({save.stat().st_size} bytes)")


def commit(probe_path: str, commit_path: str, save_path: str) -> None:
    probe = read_lines(probe_path)
    lines = read_lines(commit_path)
    before = tuple_for(lines, "transfer_destination_selected")
    scrolled = tuple_for(lines, "transfer_scrolled")
    queued = tuple_for(lines, "transfer_item_queued")
    committed = tuple_for(lines, "transfer_committed")
    after = tuple_for(lines, "transfer_before_save")
    if field(before, "baseCount") != 2 or field(before, "selectedBase") != 0 or geo_screen(before) != 10:
        fail(f"commit did not use the live transfer screen from base 0: {before!r}")
    if field(queued, "selectedBase") != 0 or geo_screen(queued) != 10:
        fail(f"item selection left the real transfer screen: {queued!r}")
    for label, state in (("transfer_destination_selected", before), ("transfer_scrolled", scrolled)):
        if geo_screen(state) != 10 or field(state, "selectedBase") != 0:
            fail(f"the real scroll left the transfer screen at {label}: {state!r}")
        work_line = one_line(lines, f"pocketufo: strategy transfer {label}:".encode())
        if not re.search(rb"built 1 picker 0 workBase 1 ", work_line):
            fail(f"destination base 1 was lost at {label}: {work_line!r}")
    probe_page = panel_view(probe, "transfer_probe")
    commit_page = panel_view(lines, "transfer_scrolled")
    if commit_page[5] != probe_page[5]:
        fail(f"commit flow reached panel top {commit_page[5]}, probe measured {probe_page[5]}")
    probe_rows = rows(probe, "transfer_probe", 68)
    if len(probe_rows) != 1:
        fail(f"slot68 has no unique live source row in the probe: {probe_rows!r}")
    target_row = probe_rows[0][0]
    if not (commit_page[5] <= target_row < commit_page[5] + commit_page[4]):
        fail(f"slot68 row {target_row} is outside the live commit page: {commit_page!r}")
    queued_page = panel_view(lines, "transfer_item_queued")
    if queued_page[5] != commit_page[5]:
        fail(f"selecting slot68 changed the visible page top: {commit_page[5]} -> {queued_page[5]}")
    initial = rows(probe, "transfer_probe", 68)
    pending = rows(lines, "transfer_item_queued", 68)
    if len(initial) != 1 or len(pending) != 1:
        fail(f"transfer row trace is missing: {initial!r} -> {pending!r}")
    if pending[0][0] != initial[0][0] or pending[0][2] != initial[0][2] - 1 or pending[0][3] != 1:
        fail(f"the real > click did not queue one source item: {initial!r} -> {pending!r}")
    queued_work = one_line(lines, b"pocketufo: strategy transfer transfer_item_queued:")
    if not re.search(rb"built 1 picker 0 workBase 1 ", queued_work):
        fail(f"base 1 did not stay selected through the post-queue draw: {queued_work!r}")

    pending_site, _target = check_site(lines, "transfer_committed", field(before, "sites"), 68, 1)
    if field(committed, "sites") != field(before, "sites") + 1:
        fail(f"the real commit did not add one transfer site: {before!r} -> {committed!r}")
    if geo_screen(after) != 0 or field(after, "baseCount") != 2:
        fail(f"transfer did not close back to the two-base map before save: {after!r}")
    stock_before = next(
        (int(m.group(1)) for line in lines
         if line.startswith(b"pocketufo: strategy stock transfer_destination_selected:")
         if (m := re.search(rb"slot 68 qty ([0-9]+)$", line))),
        None,
    )
    stock_after = next(
        (int(m.group(1)) for line in lines
         if line.startswith(b"pocketufo: strategy stock transfer_before_save:")
         if (m := re.search(rb"slot 68 qty ([0-9]+)$", line))),
        None,
    )
    if stock_before is None or stock_after != stock_before - 1:
        fail(f"source base stock did not fall by one: {stock_before} -> {stock_after}")
    if not re.search(rb"pocketufo: save slot 0: written", b"\n".join(lines)):
        fail("the pending transfer was not saved in private slot 0")
    save = Path(save_path)
    if not save.is_file() or save.stat().st_size == 0:
        fail(f"private transfer save is missing or empty: {save}")
    print(f"ok: slot 68 moved once through the real transfer row, into site {pending_site}")
    print(f"ok: source stock fell {stock_before} -> {stock_after}; pending transfer saved")


def load_and_arrival(commit_path: str, load_path: str) -> None:
    saved = read_lines(commit_path)
    loaded = read_lines(load_path)
    saved_tuple = tuple_for(saved, "transfer_before_save")
    loaded_tuple = tuple_for(loaded, "transfer_after_load")
    if saved_tuple != loaded_tuple:
        fail(f"fresh load changed the pending two-base transfer tuple: {saved_tuple!r} -> {loaded_tuple!r}")
    saved_site_prefix = b"pocketufo: strategy site transfer_before_save:"
    loaded_site_prefix = b"pocketufo: strategy site transfer_after_load:"
    saved_item_prefix = b"pocketufo: strategy site-item transfer_before_save:"
    loaded_item_prefix = b"pocketufo: strategy site-item transfer_after_load:"
    saved_site = [line[len(saved_site_prefix):] for line in saved if line.startswith(saved_site_prefix)]
    loaded_site = [line[len(loaded_site_prefix):] for line in loaded if line.startswith(loaded_site_prefix)]
    saved_items = [line[len(saved_item_prefix):] for line in saved if line.startswith(saved_item_prefix)]
    loaded_items = [line[len(loaded_item_prefix):] for line in loaded if line.startswith(loaded_item_prefix)]
    if saved_site != loaded_site or saved_items != loaded_items:
        fail(f"fresh load changed the committed transfer site: {saved_site!r} / {saved_items!r} -> "
             f"{loaded_site!r} / {loaded_items!r}")

    arrival = tuple_for(loaded, "transfer_after_clock_source")
    if field(arrival, "baseCount") != 2:
        fail(f"the fresh-loaded world lost its second base: {arrival!r}")
    dest = tuple_for(loaded, "transfer_arrived_base1")
    if field(dest, "selectedBase") != 1 or field(dest, "baseCount") != 2 or geo_screen(dest) != 0:
        fail(f"the arrival check did not use base 1 on the live map: {dest!r}")
    stock = [line for line in loaded if line.startswith(b"pocketufo: strategy stock transfer_arrived_base1:")]
    if not any(re.search(rb"slot 68 qty [1-9][0-9]*$", line) for line in stock):
        fail(f"base 1 did not receive the transferred slot 68 after real clock pulses: {stock!r}")
    if field(dest, "stores") != 300:
        fail(f"destination Stores capacity did not survive load and arrival: {dest!r}")
    print("ok: fresh process restored the pending transfer before the clock advanced")
    print("ok: real geoscape timer delivered one item into base 1's Stores")


def main() -> int:
    if len(sys.argv) < 2:
        fail("usage: check_strategy_transfer.py setup|probe|commit|load ...")
    mode = sys.argv[1]
    if mode == "newbase" and len(sys.argv) == 4:
        newbase(sys.argv[2], sys.argv[3])
    elif mode == "setup" and len(sys.argv) == 4:
        setup(sys.argv[2], sys.argv[3])
    elif mode == "scroll-probe" and len(sys.argv) == 5:
        scroll_probe(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "probe" and len(sys.argv) == 5:
        emit_commit(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "commit" and len(sys.argv) == 5:
        commit(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "load" and len(sys.argv) == 4:
        load_and_arrival(sys.argv[2], sys.argv[3])
    else:
        fail("wrong arguments for the selected mode")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
