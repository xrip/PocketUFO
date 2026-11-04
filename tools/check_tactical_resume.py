#!/usr/bin/env python3
"""Check a cold-process tactical save/load from live app logs and DAT chunks."""

from __future__ import annotations

from pathlib import Path
import re
import struct
import sys


DAT_ENTRY_SIZE = 21
DAT_NAME_SIZE = 13

TACT_HEADER_RE = re.compile(
    r"^pocketufo: tact: busy (-?\d+), phase (-?\d+), enemy turn (-?\d+), "
    r"player input (-?\d+), battle ended (-?\d+), ui screen (-?\d+), "
    r"latch (-?\d+), mode (-?\d+), reserve (-?\d+)$"
)
TACT_VIEW_RE = re.compile(
    r"^pocketufo: tact view: dims (\d+)/(\d+) scroll (-?\d+)/(-?\d+) "
    r"cursor (-?\d+)/(-?\d+) layer (\d+)$"
)
TACT_TERRAIN_RE = re.compile(
    r"^pocketufo: tact terrain: dims (\d+)/(\d+) ids ([0-9A-Fa-f]{8})$"
)
QUEUE_RE = re.compile(
    r"^  queue: len (-?\d+), cursor (-?\d+), active (-?\d+), flag0 (-?\d+), "
    r"flag1 (-?\d+), phase max (-?\d+), phase tick (-?\d+)$"
)
CURSOR_RE = re.compile(
    r"^  cursor (-?\d+)/(-?\d+) layer (-?\d+); selected at "
    r"(-?\d+)/(-?\d+)/(-?\d+), uTu (\d+), hp (\d+), status (\d+), "
    r"facing (\d+), kills (\d+)$"
)
XCOM_RE = re.compile(
    r"^  xcom \d+: at (-?\d+)/(-?\d+)/(-?\d+), uTu (\d+), hp (\d+), "
    r"status (\d+), large (\d+)$"
)
ENEMY_RE = re.compile(
    r"^  enemy \d+: type (\d+) at (-?\d+)/(-?\d+)/(-?\d+), uTu (\d+), "
    r"hp (\d+), status (\d+), aiTick -?\d+, aiDone \d+, large (\d+)$"
)
CAMPAIGN_HEAD_RE = re.compile(
    r"^pocketufo: campaign state [^:]+: screen (-?\d+) tact (-?\d+)/(-?\d+) "
    r"mission (-?\d+).*?\bxcom (-?\d+) enemy (-?\d+) reserve (-?\d+) items (-?\d+)\b"
)
CAMPAIGN_SELECTED_RE = re.compile(
    r"^  selected id -?\d+ cell (-?\d+)/(-?\d+)/(-?\d+) tu (\d+) hp (\d+) "
    r"hands (-?\d+)/(-?\d+)$"
)
TACT_ITEM_RE = re.compile(
    r"^  item \d+ ref -?\d+ object (-?\d+) ammo (\d+) magazine (\d+) "
    r"armed (\d+) fuse (\d+)$"
)
CAMPAIGN_MAP_RE = re.compile(
    r"\bmission (-?\d+) tier (-?\d+) variant (-?\d+) damageBand (\d+) "
    r"sprites (\d+)/(\d+)/(\d+)/(\d+)"
)
HOME_OBSERVE_RE = re.compile(
    r"^pocketufo: campaign home-observe stage=(?P<stage>\w+) scene=\d+/\d+ "
    r"tact=(?P<tact>\d+)\b"
)
HOME_ROSTER_RE = re.compile(
    r"^pocketufo: campaign home-roster stage=(?P<stage>\w+) total=(?P<total>\d+) "
    r"shown=(?P<shown>\d+) truncated=(?P<truncated>\d+)$"
)
HOME_CREW_RE = re.compile(
    r"^pocketufo: campaign home-crew stage=(?P<stage>\w+) "
    r"rosterIndex=(?P<ordinal>\d+) present=(?P<present>\d+)"
    r"(?: .*? loadoutItems=(?P<count>\d+) items=(?P<items>\S+))?$"
)
CARGO_HEAD_RE = re.compile(
    r"^pocketufo: campaign cargo (?P<stage>\w+): craftIndex (?P<craft_index>-?\d+) "
    r"craftSlot (?P<craft_slot>-?\d+) objects (?P<object_count>\d+) sites (?P<site_count>\d+)$"
)
CARGO_ITEM_RE = re.compile(
    r"^pocketufo: campaign cargo item (?P<stage>\w+): object (?P<object>\d+) "
    r"craftIndexQty (?P<craft_qty>\d+) shipSlotQty (?P<ship_qty>\d+)$"
)
CARGO_SITE_RE = re.compile(
    r"^pocketufo: campaign cargo site (?P<stage>\w+): site (?P<site>\d+) "
    r"object (?P<object>\d+) qty (?P<qty>\d+)$"
)
CARGO_SUMMARY_RE = re.compile(
    r"^pocketufo: campaign cargo summary (?P<stage>\w+): craftObjects "
    r"(?P<craft_objects>\d+) siteObjects (?P<site_objects>\d+)$"
)


def fail(message: str) -> None:
    print(f"TACTICAL RESUME FAILED: {message}", file=sys.stderr)
    raise SystemExit(1)


def need(text: str, pattern: str, label: str) -> re.Match[str]:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        fail(label)
    print(f"ok: {label}")
    return match


def read_log(path: Path) -> tuple[str, list[str]]:
    if not path.is_file():
        fail(f"log is missing: {path}")
    text = path.read_bytes().decode("utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n")
    return text, text.splitlines()


def parse_chunks(path: Path) -> dict[str, tuple[int, bytes]]:
    if not path.is_file() or path.stat().st_size < 4:
        fail(f"tactical save is missing or too small: {path}")
    data = path.read_bytes()
    entry_count = struct.unpack_from("<I", data, 0)[0]
    if entry_count == 0 or entry_count > 4096:
        fail(f"invalid DAT directory capacity: {entry_count}")
    directory_end = 4 + DAT_ENTRY_SIZE * entry_count
    if directory_end > len(data):
        fail(f"DAT directory extends beyond the file: {directory_end} > {len(data)}")

    chunks: dict[str, tuple[int, bytes]] = {}
    for index in range(entry_count):
        entry = 4 + DAT_ENTRY_SIZE * index
        raw_name = data[entry : entry + DAT_NAME_SIZE].split(b"\0", 1)[0]
        if not raw_name:
            continue
        try:
            tag = raw_name.decode("ascii").lower()
        except UnicodeDecodeError:
            fail(f"non-ASCII DAT tag in directory row {index}")
        size, offset = struct.unpack_from("<II", data, entry + DAT_NAME_SIZE)
        if offset < directory_end or offset + size > len(data):
            fail(f"DAT chunk {tag} has invalid extent offset={offset} size={size}")
        if tag in chunks:
            fail(f"duplicate DAT chunk tag: {tag}")
        chunks[tag] = (size, data[offset : offset + size])

    return chunks


def verify_tactical_chunks(path: Path) -> None:
    chunks = parse_chunks(path)
    required = {
        "tactver", "gamestate", "soldiers", "stactflg", "1_24scb", "mcnt",
        "tgamedata", "teamunits", "aliens", "diedsolds", "hiddensold", "flobjs",
        "map", "v124.dat", "objbefore", "rmp",
    }
    missing = sorted(required - chunks.keys())
    if missing:
        fail(f"saved slot lacks tactical chunks: {', '.join(missing)}")
    tactver_size, tactver = chunks["tactver"]
    game_size, gamestate = chunks["gamestate"]
    tgamedata_size, _ = chunks["tgamedata"]
    if tactver_size != 2 or struct.unpack("<H", tactver)[0] != 1:
        fail(f"unexpected tactver chunk: size={tactver_size} bytes={tactver.hex()}")
    if game_size != 1 or gamestate != b"\x01":
        fail(f"slot was not saved with a live tactical flag: {gamestate.hex()}")
    if tgamedata_size != 43:
        fail(f"tgamedata chunk is not the 43-byte tactical record: {tgamedata_size}")
    for tag in ("teamunits", "map", "flobjs", "rmp"):
        if chunks[tag][0] == 0:
            fail(f"required tactical payload is empty: {tag}")
    print("ok: private slot contains gamestate=1, tgamedata, rosters, map, items, and RMP chunks")
    print("tactical chunk sizes: " + ", ".join(
        f"{tag}={chunks[tag][0]}" for tag in sorted(required)
    ))


def parse_loadout(raw: str) -> tuple[tuple[int, int], ...]:
    entries: list[tuple[int, int, int]] = []
    try:
        for field in raw.split(","):
            slot_raw, values = field.split(":", 1)
            item_raw, ammo_raw = values.split("/", 1)
            entries.append((int(slot_raw, 0), int(item_raw, 0), int(ammo_raw, 0)))
    except ValueError as exc:
        fail(f"invalid persisted loadout field {raw!r}: {exc}")
    if len(entries) != 25 or [slot for slot, _item, _ammo in entries] != list(range(25)):
        fail(f"expected ordered 25-slot Soldier loadout, got {len(entries)} entries")
    return tuple((item, ammo) for _slot, item, ammo in entries)


def home_loadout_snapshot(lines: list[str], stage: str) -> dict[int, tuple[tuple[int, int], ...]]:
    headers = [
        match for line in lines
        if (match := HOME_OBSERVE_RE.match(line)) is not None and match["stage"] == stage
    ]
    if len(headers) != 1:
        fail(f"expected one home-observe header for {stage}, got {len(headers)}")
    if headers[0]["tact"] != "1":
        fail(f"home-observe snapshot {stage} is not inside a live tactical mission")
    rosters = [
        match for line in lines
        if (match := HOME_ROSTER_RE.match(line)) is not None and match["stage"] == stage
    ]
    if len(rosters) != 1:
        fail(f"expected one complete home-roster header for {stage}, got {len(rosters)}")
    roster = rosters[0]
    if roster["truncated"] != "0" or roster["total"] != roster["shown"]:
        fail(f"home-observe roster is partial at {stage}: {roster.group(0)}")

    result: dict[int, tuple[tuple[int, int], ...]] = {}
    seen_ordinals: set[int] = set()
    seen_rows = 0
    for line in lines:
        match = HOME_CREW_RE.match(line)
        if match is None or match["stage"] != stage:
            continue
        ordinal = int(match["ordinal"])
        if ordinal in seen_ordinals:
            fail(f"duplicate home-observe Soldier ordinal {ordinal} at {stage}")
        seen_ordinals.add(ordinal)
        seen_rows += 1
        if match["present"] != "1" or match["count"] is None or match["items"] is None:
            fail(f"home-observe Soldier row {ordinal} is absent or incomplete at {stage}")
        pairs = parse_loadout(match["items"])
        loadout_count = sum(1 for item, _ammo in pairs if item != 0)
        if int(match["count"]) != loadout_count:
            fail(f"home-observe Soldier row {ordinal} reports {match['count']} items, parsed {loadout_count}")
        result[ordinal] = pairs
    if seen_rows == 0:
        fail(f"home-observe snapshot has no complete Soldier rows at {stage}")
    if seen_rows != int(roster["shown"]):
        fail(f"home-observe row count is incomplete at {stage}: {seen_rows}/{roster['shown']}")
    return result


def cargo_snapshot(lines: list[str], stage: str) -> dict[str, object]:
    heads = [
        match for line in lines
        if (match := CARGO_HEAD_RE.match(line)) is not None and match["stage"] == stage
    ]
    summaries = [
        match for line in lines
        if (match := CARGO_SUMMARY_RE.match(line)) is not None and match["stage"] == stage
    ]
    if len(heads) != 1 or len(summaries) != 1:
        fail(f"expected one complete campaign cargo snapshot for {stage}")
    items: dict[int, tuple[int, int]] = {}
    sites: dict[tuple[int, int], int] = {}
    for line in lines:
        match = CARGO_ITEM_RE.match(line)
        if match is not None and match["stage"] == stage:
            obj = int(match["object"])
            if obj in items:
                fail(f"duplicate campaign cargo object {obj} at {stage}")
            items[obj] = (int(match["craft_qty"]), int(match["ship_qty"]))
            continue
        match = CARGO_SITE_RE.match(line)
        if match is not None and match["stage"] == stage:
            key = (int(match["site"]), int(match["object"]))
            if key in sites:
                fail(f"duplicate campaign cargo site row {key} at {stage}")
            sites[key] = int(match["qty"])

    head = heads[0]
    summary = summaries[0]
    nonzero_items = sum(1 for craft, ship in items.values() if craft != 0 or ship != 0)
    if nonzero_items != int(summary["craft_objects"]):
        fail(f"campaign cargo summary count does not match rows at {stage}")
    if len(sites) != int(summary["site_objects"]):
        fail(f"campaign cargo site summary count does not match rows at {stage}")
    return {
        "craft_index": int(head["craft_index"]),
        "craft_slot": int(head["craft_slot"]),
        "objects": int(head["object_count"]),
        "site_count": int(head["site_count"]),
        "items": items,
        "sites": sites,
    }


def compare_home_loadouts(saved: dict[int, tuple[tuple[int, int], ...]],
                          loaded: dict[int, tuple[tuple[int, int], ...]]) -> None:
    if saved.keys() != loaded.keys():
        fail(f"Soldier wire ordinals changed across processes: {sorted(saved)} -> {sorted(loaded)}")
    for ordinal in sorted(saved):
        if saved[ordinal] != loaded[ordinal]:
            for slot, (before, after) in enumerate(zip(saved[ordinal], loaded[ordinal])):
                if before != after:
                    fail(f"Soldier ordinal {ordinal} stored slot {slot} changed: {before} -> {after}")
            fail(f"Soldier ordinal {ordinal} stored 25-slot loadout length changed")
    print(f"ok: all 25 stored index/ammo pairs match for {len(saved)} Soldier wire ordinals")


def compare_cargo(saved: dict[str, object], loaded: dict[str, object]) -> None:
    for key in ("craft_index", "craft_slot", "objects", "site_count", "items", "sites"):
        if saved[key] != loaded[key]:
            fail(f"live campaign cargo changed across processes: {key} {saved[key]} -> {loaded[key]}")
    print(f"ok: campaign cargo object rows and site rows match for craft index {saved['craft_index']}")


def flow_commands(path: Path) -> list[list[str]]:
    if not path.is_file():
        fail(f"flow file is missing: {path}")
    commands = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            commands.append(line.split())
    return commands


def campaign_snapshot(lines: list[str], label: str) -> dict[str, object]:
    marker = f"pocketufo: campaign state {label}:"
    start = next((i for i, line in enumerate(lines) if line.startswith(marker)), -1)
    if start < 0:
        fail(f"missing live campaign snapshot: {label}")
    head = CAMPAIGN_HEAD_RE.match(lines[start])
    if head is None:
        fail(f"campaign state line has changed format: {lines[start]}")
    screen, tact_live, tact_flag, mission, xcom_count, enemy_count, reserve, item_count = map(
        int, head.groups()
    )
    map_match = CAMPAIGN_MAP_RE.search(lines[start])
    if map_match is None:
        fail(f"campaign state line lacks the live mission map tuple: {lines[start]}")
    map_signature = tuple(map(int, map_match.groups()))

    stop = next(
        (i for i in range(start + 1, len(lines)) if lines[i].startswith("pocketufo: tact:")),
        len(lines),
    )
    selected = None
    items: list[tuple[int, int, int, int, int]] = []
    for line in lines[start + 1 : stop]:
        match = CAMPAIGN_SELECTED_RE.match(line)
        if match is not None:
            selected = tuple(map(int, match.groups()))
            continue
        match = TACT_ITEM_RE.match(line)
        if match is not None:
            obj, ammo, magazine, armed, fuse = map(int, match.groups())
            # SaveId/reference and pool order are process-local; retain only live item fields.
            if obj >= 0:
                items.append((obj, ammo, magazine, armed, fuse))
    if selected is None:
        fail(f"campaign snapshot has no selected live tactical unit: {label}")
    if not items:
        fail(f"campaign snapshot has no live tactical item records: {label}")
    return {
        "screen": screen,
        "tact_live": tact_live,
        "tact_flag": tact_flag,
        "mission": mission,
        "map_signature": map_signature,
        "xcom_count": xcom_count,
        "enemy_count": enemy_count,
        "reserve": reserve,
        "item_count": item_count,
        "selected": selected,
        "items": tuple(sorted(items)),
    }


def tact_snapshot(lines: list[str], label: str) -> dict[str, object]:
    marker = f"pocketufo: campaign state {label}:"
    campaign_index = next((i for i, line in enumerate(lines) if line.startswith(marker)), -1)
    if campaign_index < 0:
        fail(f"cannot find campaign state before tactical snapshot: {label}")
    start = next(
        (i for i in range(campaign_index + 1, len(lines)) if lines[i].startswith("pocketufo: tact:")),
        -1,
    )
    if start < 0:
        fail(f"no live tact report follows campaign snapshot: {label}")
    if lines[start] == "pocketufo: tact: no live mission":
        fail(f"no tactical mission is live at snapshot: {label}")
    header = TACT_HEADER_RE.match(lines[start])
    if header is None:
        fail(f"tact header has changed format: {lines[start]}")
    keys = ("busy", "phase", "enemy_turn", "player_input", "battle_ended", "ui_screen",
            "latch", "mode", "reserve")
    state = dict(zip(keys, map(int, header.groups())))

    queue = None
    cursor_selected = None
    xcoms: list[tuple[int, ...]] = []
    enemies: list[tuple[int, ...]] = []
    saw_end = False
    for line in lines[start + 1 :]:
        if line == "pocketufo: step tact":
            saw_end = True
            break
        match = QUEUE_RE.match(line)
        if match is not None:
            queue = tuple(map(int, match.groups()))
            continue
        match = CURSOR_RE.match(line)
        if match is not None:
            values = tuple(map(int, match.groups()))
            # Exclude kills; the save audit checks persisted input and unit state.
            cursor_selected = values[:10]
            continue
        match = XCOM_RE.match(line)
        if match is not None:
            xcoms.append(tuple(map(int, match.groups())))
            continue
        match = ENEMY_RE.match(line)
        if match is not None:
            enemies.append(tuple(map(int, match.groups())))
    if not saw_end or queue is None or cursor_selected is None:
        fail(f"incomplete live tact report: {label}")
    if not xcoms or not enemies:
        fail(f"tact report lacks live XCOM or enemy map rows: {label}")
    return {
        "state": state,
        "queue": queue,
        "cursor_selected": cursor_selected,
        "xcoms": tuple(sorted(xcoms)),
        "enemies": tuple(sorted(enemies)),
    }


def tact_view_snapshot(lines: list[str], label: str) -> dict[str, object]:
    marker = f"pocketufo: campaign state {label}:"
    campaign_index = next((i for i, line in enumerate(lines) if line.startswith(marker)), -1)
    if campaign_index < 0:
        fail(f"cannot find campaign state before tactical view: {label}")
    start = next(
        (i for i in range(campaign_index + 1, len(lines)) if lines[i].startswith("pocketufo: tact:")),
        -1,
    )
    if start < 0:
        fail(f"no tactical view follows campaign snapshot: {label}")
    stop = next(
        (i for i in range(start + 1, len(lines)) if lines[i] == "pocketufo: step tact"),
        len(lines),
    )
    for line in lines[start + 1 : stop]:
        match = TACT_VIEW_RE.match(line)
        if match is not None:
            dims_x, dims_y, scroll_x, scroll_y, cursor_x, cursor_y, layer = map(
                int, match.groups()
            )
            return {
                "dims": (dims_x, dims_y),
                "scroll": (scroll_x, scroll_y),
                "cursor": (cursor_x, cursor_y),
                "layer": layer,
            }
    fail(f"no live tactical view follows campaign snapshot: {label}")


def tact_terrain_snapshot(lines: list[str], label: str) -> dict[str, object]:
    marker = f"pocketufo: campaign state {label}:"
    campaign_index = next((i for i, line in enumerate(lines) if line.startswith(marker)), -1)
    if campaign_index < 0:
        fail(f"cannot find campaign state before terrain signature: {label}")
    start = next(
        (i for i in range(campaign_index + 1, len(lines)) if lines[i].startswith("pocketufo: tact:")),
        -1,
    )
    if start < 0 or lines[start] == "pocketufo: tact: no live mission":
        fail(f"no live tactical terrain report follows campaign snapshot: {label}")
    stop = next(
        (i for i in range(start + 1, len(lines)) if lines[i] == "pocketufo: step tact"),
        -1,
    )
    if stop < 0:
        fail(f"incomplete tactical terrain report: {label}")
    matches = [
        TACT_TERRAIN_RE.match(line)
        for line in lines[start + 1 : stop]
        if line.startswith("pocketufo: tact terrain:")
    ]
    if not matches or any(match is None for match in matches):
        fail(f"missing or malformed terrain digest: {label}")
    if len(matches) != 1:
        fail(f"expected one terrain digest in tactical report: {label}")
    match = matches[0]
    assert match is not None
    dims_x, dims_y, ids = match.groups()
    return {"dims": (int(dims_x), int(dims_y)), "ids": int(ids, 16)}


def require_idle(snapshot: dict[str, object], label: str) -> None:
    state = snapshot["state"]
    queue = snapshot["queue"]
    selected = snapshot["cursor_selected"]
    assert isinstance(state, dict) and isinstance(queue, tuple) and isinstance(selected, tuple)
    checks = {
        "busy": 0,
        "phase": 0,
        "enemy_turn": 0,
        "player_input": 1,
        "battle_ended": 0,
        "ui_screen": 0,
    }
    for key, expected in checks.items():
        if state[key] != expected:
            fail(f"{label} is not at idle player input: {key}={state[key]} expected {expected}")
    if queue[0] != 0 or queue[2] != 0:
        fail(f"{label} has queued or active work: len={queue[0]} active={queue[2]}")
    if any(value < 0 for value in selected[3:6]):
        fail(f"{label} has no selected live map cell: {selected[3:6]}")
    print(f"ok: {label} is idle with player input, no enemy turn, and an empty queue")


def require_enemyturn_completed(lines: list[str]) -> None:
    start = next(
        (i for i, line in enumerate(lines) if line == "pocketufo: campaign enemyturn: started enemy 1 input 0"),
        -1,
    )
    if start < 0:
        fail("the loaded mission did not start an enemy turn from player input")
    passed = next(
        (i for i in range(start + 1, len(lines)) if lines[i] == "pocketufo: campaign enemyturn: PASS"),
        -1,
    )
    if passed < 0:
        fail("the loaded mission enemy turn did not pass its completion check")
    abort = next(
        (i for i in range(passed + 1, len(lines)) if lines[i].startswith("pocketufo: campaign abort:")),
        len(lines),
    )
    report = next(
        (i for i in range(passed + 1, abort) if lines[i].startswith("pocketufo: tact:")),
        -1,
    )
    if report < 0:
        fail("no tactical state report follows the completed enemy turn before abort")
    header = TACT_HEADER_RE.match(lines[report])
    if header is None:
        fail(f"enemy-turn tactical report has changed format: {lines[report]}")
    state = dict(zip(
        ("busy", "phase", "enemy_turn", "player_input", "battle_ended", "ui_screen",
         "latch", "mode", "reserve"),
        map(int, header.groups()),
    ))
    queue_line = next(
        (line for line in lines[report + 1 : abort] if QUEUE_RE.match(line) is not None),
        None,
    )
    if queue_line is None:
        fail("enemy-turn tactical report has no queue state before abort")
    queue_match = QUEUE_RE.match(queue_line)
    assert queue_match is not None
    queue = tuple(map(int, queue_match.groups()))
    expected = {"busy": 0, "phase": 0, "enemy_turn": 0, "player_input": 1,
                "battle_ended": 0, "ui_screen": 0}
    for key, value in expected.items():
        if state[key] != value:
            fail(f"enemy turn did not return to idle player input: {key}={state[key]} expected {value}")
    if queue[0] != 0 or queue[2] != 0:
        fail(f"enemy turn left queued or active work: len={queue[0]} active={queue[2]}")
    print("ok: the loaded enemy turn completed and returned to idle player input before abort")


def compare_live_state(saved: dict[str, object], loaded: dict[str, object]) -> None:
    saved_state = saved["state"]
    loaded_state = loaded["state"]
    assert isinstance(saved_state, dict) and isinstance(loaded_state, dict)
    # Action latch/mode and animation counters are not part of the tactical save record.
    for key in ("phase", "player_input", "battle_ended", "ui_screen", "reserve"):
        if saved_state[key] != loaded_state[key]:
            fail(f"live tactical state changed across processes: {key} {saved_state[key]} -> {loaded_state[key]}")
    saved_cursor = saved["cursor_selected"]
    loaded_cursor = loaded["cursor_selected"]
    assert isinstance(saved_cursor, tuple) and isinstance(loaded_cursor, tuple)
    # LoadGame calls MapView_FocusSelected after Save_ReadTGameData: raw 0x3D234/248,
    # 0x16710/744, and 0x2C7BC/7D8/7F0. That call keeps cursor X/Y, recenters the
    # camera, and sets the layer from the selected cell. Compare cursor X/Y and unit
    # state here; compare the layer against the selected cell below.
    if saved_cursor[:2] != loaded_cursor[:2] or saved_cursor[3:] != loaded_cursor[3:]:
        fail(f"live tactical cursor/unit state changed across processes: "
             f"{saved_cursor} -> {loaded_cursor}")
    for key in ("xcoms", "enemies"):
        if saved[key] != loaded[key]:
            fail(f"live tactical {key} changed across processes: {saved[key]} -> {loaded[key]}")
    print(f"ok: cursor XY and selected unit state, {len(saved['xcoms'])} live XCOM rows, "
          f"and {len(saved['enemies'])} live enemy rows match across processes")


def compare_tactical_view(saved: dict[str, object], loaded: dict[str, object],
                          saved_tact: dict[str, object], loaded_tact: dict[str, object]) -> None:
    saved_cursor = saved_tact["cursor_selected"]
    loaded_cursor = loaded_tact["cursor_selected"]
    assert isinstance(saved_cursor, tuple) and isinstance(loaded_cursor, tuple)
    if saved["dims"] != loaded["dims"]:
        fail(f"tactical map dimensions changed across processes: {saved['dims']} -> {loaded['dims']}")
    if saved["cursor"] != loaded["cursor"]:
        fail(f"tactical cursor cell changed across processes: {saved['cursor']} -> {loaded['cursor']}")
    if saved["cursor"] != saved_cursor[:2] or loaded["cursor"] != loaded_cursor[:2]:
        fail("tactical view cursor does not match its live tactical report")
    if saved["layer"] != saved_cursor[2] or loaded["layer"] != loaded_cursor[2]:
        fail("tactical view layer does not match its live tactical report")
    if loaded["layer"] != loaded_cursor[5] + 1:
        fail(f"loaded view layer is not selected-cell Z+1: layer={loaded['layer']} "
             f"selected={loaded_cursor[3:6]}")
    print(f"ok: view dimensions {saved['dims']} and cursor {saved['cursor']} persist; "
          f"load focus recalculates scroll {saved['scroll']} -> {loaded['scroll']} and "
          f"sets layer {saved['layer']} -> {loaded['layer']} from selected-cell Z+1")


def compare_tactical_terrain(saved: dict[str, object], loaded: dict[str, object],
                             saved_view: dict[str, object], loaded_view: dict[str, object]) -> None:
    saved_ids = int(saved["ids"])
    loaded_ids = int(loaded["ids"])
    if saved["dims"] != saved_view["dims"] or loaded["dims"] != loaded_view["dims"]:
        fail("terrain signature dimensions disagree with the live tactical view")
    if saved["dims"] != loaded["dims"]:
        fail(f"terrain dimensions changed across processes: {saved['dims']} -> {loaded['dims']}")
    if saved_ids != loaded_ids:
        fail(f"all-cell terrain tile IDs changed across processes: {saved_ids:08X} -> {loaded_ids:08X}")
    print(f"ok: all-cell terrain tile digest matches at dims {saved['dims']}: {saved_ids:08X}")


def verify_live_equipment(saved: dict[str, object], loaded: dict[str, object]) -> None:
    save_selected = saved["selected"]
    load_selected = loaded["selected"]
    save_items = saved["items"]
    load_items = loaded["items"]
    assert isinstance(save_selected, tuple) and isinstance(load_selected, tuple)
    assert isinstance(save_items, tuple) and isinstance(load_items, tuple)
    # This tuple has live cell/TU/HP plus hand object slots; SaveId and item refs are excluded.
    if save_selected != load_selected:
        fail(f"selected live unit and hand slots changed: {save_selected} -> {load_selected}")
    if save_items != load_items:
        fail(f"live tactical item object/ammo state changed: {save_items} -> {load_items}")
    hands = save_selected[-2:]
    if 4 not in hands:
        fail(f"selected live tactical unit does not carry the Rifle: hands={hands}")
    if not any(item[0] == 4 and item[2] == 38 and item[1] > 0 for item in save_items):
        fail("no live Rifle item has clip 38 and positive ammo")
    print(f"ok: selected hand slots {hands} and live Rifle clip/ammo state match")


OWNER_PREFIX = "pocketufo: campaign tact-resume-owners: "
OWNER_FIELDS = re.compile(r"([A-Za-z][A-Za-z0-9_]*)=([^\s]+)")


class OwnerGraphError(ValueError):
    pass


def _owner_row(line: str) -> dict[str, str]:
    return dict(OWNER_FIELDS.findall(line.split(OWNER_PREFIX, 1)[1].strip()))


def _owner_int(row: dict[str, str], key: str) -> int:
    try:
        return int(row[key], 10)
    except (KeyError, ValueError) as exc:
        raise OwnerGraphError(f"bad owner field {key}: {row}") from exc


def _owner_table(rows: list[dict[str, str]], key_names: tuple[str, ...],
                 label: str) -> dict[tuple[str, ...], dict[str, str]]:
    out: dict[tuple[str, ...], dict[str, str]] = {}
    for row in rows:
        key = tuple(row[name] for name in key_names)
        if key in out:
            raise OwnerGraphError(f"duplicate {label} record: {key}")
        # parse_tact_resume_owners has already checked the stage and record
        # type. They describe the log snapshot, not persisted game state.
        out[key] = {name: value for name, value in row.items()
                    if name not in ("stage", "record")}
    return out


def parse_tact_resume_owners(lines: list[str], stage: str) -> dict[str, object]:
    groups: dict[str, list[dict[str, str]]] = {
        name: [] for name in (
            "idle", "summary", "roster", "unit", "footprint", "cell", "slot",
            "item", "ammoPayload", "poolTail", "error", "inactiveSlot",
        )
    }
    for line in lines:
        if OWNER_PREFIX not in line:
            continue
        row = _owner_row(line)
        if row.get("stage") != stage:
            raise OwnerGraphError(
                f"owner stage mismatch: expected {stage}, got {row.get('stage')}")
        record = row.get("record")
        if record not in groups:
            raise OwnerGraphError(f"unknown owner record: {row}")
        groups[record].append(row)

    if len(groups["idle"]) != 1:
        raise OwnerGraphError(f"{stage}: need one live idle-gate row")
    idle = groups["idle"][0]
    idle_values = (idle.get("status"), idle.get("screen"), idle.get("modal"),
                   idle.get("player"), idle.get("latch"), idle.get("busy"),
                   idle.get("queueLen"), idle.get("queueActive"), idle.get("fall"),
                   idle.get("fire"), idle.get("throw"), idle.get("enemyTurn"))
    if idle_values != ("pass", "0", "0", "1", "0", "0", "0", "0", "0", "0", "0", "0"):
        raise OwnerGraphError(f"{stage}: tactical checkpoint is not idle: {idle}")
    if len(groups["summary"]) != 1 or len(groups["poolTail"]) != 1:
        raise OwnerGraphError(f"{stage}: need one owner summary and one tail row")
    summary, tail = groups["summary"][0], groups["poolTail"][0]
    if summary.get("schema") != "1" or summary.get("status") != "complete":
        raise OwnerGraphError(f"{stage}: owner census is incomplete: {summary}")
    if _owner_int(summary, "errors") != 0 or groups["error"] or groups["inactiveSlot"]:
        raise OwnerGraphError(f"{stage}: owner census has unresolved edges")
    if _owner_int(summary, "cellScan") != 14400:
        raise OwnerGraphError(f"{stage}: cell table scan is incomplete")

    wire_count = _owner_int(summary, "wireUnits")
    side_members = _owner_int(summary, "sideMembers")
    item_count = _owner_int(summary, "itemPool")
    capacity = _owner_int(tail, "capacity")
    if (_owner_int(tail, "active") != item_count
            or _owner_int(tail, "tailSlots") != capacity - item_count
            or _owner_int(tail, "dereferenced") != 0
            or not 0 <= _owner_int(tail, "tailNonNull") <= capacity - item_count):
        raise OwnerGraphError(f"{stage}: active/tail pool boundary is invalid: {tail}")

    tables = {
        "roster": _owner_table(groups["roster"], ("side", "sideOrdinal"), "roster"),
        "unit": _owner_table(groups["unit"], ("wire",), "unit"),
        "footprint": _owner_table(groups["footprint"], ("wire", "offset"), "footprint"),
        "cell": _owner_table(groups["cell"], ("slot",), "cell"),
        "slot": _owner_table(groups["slot"], ("wire", "kind", "slot"), "slot"),
        "item": _owner_table(groups["item"], ("pool",), "item"),
        "ammoPayload": _owner_table(groups["ammoPayload"], ("parentPool",), "ammo payload"),
    }
    if set(tables["unit"]) != {(str(i),) for i in range(wire_count)}:
        raise OwnerGraphError(f"{stage}: unit records do not cover the wire pool")
    if set(tables["item"]) != {(str(i),) for i in range(item_count)}:
        raise OwnerGraphError(f"{stage}: item records do not cover the active pool")
    for field, table in (
        ("rosterSlots", tables["roster"]), ("cellRows", tables["cell"]),
        ("slots", tables["slot"]), ("items", tables["item"]),
        ("footprints", tables["footprint"]),
        ("ammoPayloads", tables["ammoPayload"]),
    ):
        if _owner_int(summary, field) != len(table):
            raise OwnerGraphError(f"{stage}: {field} rows are incomplete")
    if _owner_int(summary, "cellUnitOwners") != sum(
            _owner_int(row, "unitWire") >= 0 for row in tables["cell"].values()):
        raise OwnerGraphError(f"{stage}: cell unit-owner count does not match rows")
    if _owner_int(summary, "cellItemHeads") != sum(
            _owner_int(row, "itemHead") >= 0 for row in tables["cell"].values()):
        raise OwnerGraphError(f"{stage}: cell item-head count does not match rows")
    if _owner_int(summary, "slots") != side_members * 27:
        raise OwnerGraphError(f"{stage}: active inventory slots are incomplete")
    if side_members != sum(
            row["sideMember"] == "1" for row in tables["unit"].values()):
        raise OwnerGraphError(f"{stage}: side-member rows do not match roster count")
    for row in tables["item"].values():
        if (_owner_int(row, "ownerCount") != 1
                or row["ownerKind"] not in ("unitSlot", "groundCell")):
            raise OwnerGraphError(f"{stage}: item owner is not unique: {row}")
    if set(tables["ammoPayload"]) != set(tables["item"]):
        raise OwnerGraphError(f"{stage}: ammo payload parents do not cover the item pool")
    for row in tables["slot"].values():
        kind, slot, state = row["kind"], row["slot"], row["state"]
        item_pool = _owner_int(row, "itemPool")
        save_ref = _owner_int(row, "saveRef")
        if state == "item":
            if item_pool < 0 or save_ref != item_pool:
                raise OwnerGraphError(f"{stage}: item slot ordinal is not canonical: {row}")
        elif state == "null":
            if item_pool != -1 or save_ref != 32767:
                raise OwnerGraphError(f"{stage}: null slot has a non-null wire ref: {row}")
        elif state == "empty_sentinel":
            expected_ref = 32767 if kind == "backpack" and slot in ("5", "6") else 32000
            if item_pool != -1 or save_ref != expected_ref:
                raise OwnerGraphError(f"{stage}: sentinel slot has the wrong wire ref: {row}")
        else:
            raise OwnerGraphError(f"{stage}: unsupported slot state: {row}")

    stable_summary = {
        key: _owner_int(summary, key)
        for key in ("wireUnits", "sideMembers", "rosterSlots", "itemPool",
                    "cellScan", "cellRows", "cellUnitOwners", "cellItemHeads",
                    "slots", "items", "ammoPayloads", "footprints", "errors")
    }
    stable_tail = {
        key: _owner_int(tail, key)
        for key in ("active", "capacity", "tailSlots", "dereferenced")
    }
    return {"summary": stable_summary, "poolTail": stable_tail, **tables}


def _semantic_owner_rows(snapshot: dict[str, object]) -> dict[str, object]:
    result = dict(snapshot)
    slots = dict(result["slot"])
    normalized: dict[tuple[str, ...], dict[str, str]] = {}
    for key, source in slots.items():
        row = dict(source)
        if (row["kind"] == "backpack" and row["slot"] in ("5", "6")
                and row["state"] in ("null", "empty_sentinel")):
            row["state"] = "reserved-empty"
        elif row["state"] == "empty_sentinel":
            row["state"] = "empty-sentinel-32000"
        elif row["state"] == "null":
            row["state"] = "null-7fff"
        normalized[key] = row
    result["slot"] = normalized
    return result


def compare_tact_resume_owners(saved: dict[str, object],
                               loaded: dict[str, object]) -> None:
    before, after = _semantic_owner_rows(saved), _semantic_owner_rows(loaded)
    for section in ("summary", "poolTail", "roster", "unit", "footprint",
                    "cell", "slot", "item", "ammoPayload"):
        if before[section] != after[section]:
            raise OwnerGraphError(
                f"tactical owner graph changed across cold resume in {section}")


def main() -> int:
    if len(sys.argv) != 6:
        fail("usage: check_tactical_resume.py SAVE_LOG LOAD_LOG SAVE0 SAVE_FLOW LOAD_FLOW")
    save_text, save_lines = read_log(Path(sys.argv[1]))
    load_text, load_lines = read_log(Path(sys.argv[2]))
    save_path = Path(sys.argv[3])
    save_commands = flow_commands(Path(sys.argv[4]))
    load_commands = flow_commands(Path(sys.argv[5]))

    load_words = [row[0].lower() for row in load_commands]
    if ("newgame" in load_words or "pocketufo: step newgame" in load_text
            or "pocketufo: new game:" in load_text):
        fail("the second process starts a new game instead of resuming the saved mission")
    if not any(row == ["click", "120", "146"] for row in load_commands):
        fail("the load flow does not open the original load-slot menu row")
    if not any(row == ["click", "120", "74"] for row in load_commands):
        fail("the load flow does not select slot 0 through the original menu")
    required_save_steps = (
        ["click", "120", "124"], ["pick"], ["campaign", "gear"], ["campaign", "contact"],
        ["campaign", "launch"], ["campaign", "deploy"], ["campaign", "move"],
        ["campaign", "fire"], ["campaign", "cargo", "resume_staged_craft"],
        ["campaign", "cargo", "resume_before_save"],
        ["campaign", "home-observe", "resume_before_save"],
        ["campaign", "tact-resume-owners", "resume_before_save"], ["save", "0"],
    )
    if not save_commands or save_commands[0] != ["click", "120", "124"]:
        fail("save flow does not start through the original New Game menu control")
    for step in required_save_steps:
        if step not in save_commands:
            fail(f"save flow omits required real setup/action: {' '.join(step)}")
    required_load_steps = (
        ["campaign", "home-observe", "resume_after_load"],
        ["campaign", "cargo", "resume_after_load"],
        ["campaign", "tact-resume-owners", "resume_after_load"],
        ["campaign", "move"], ["campaign", "fire"], ["campaign", "enemyturn"],
        ["campaign", "abort"],
    )
    for step in required_load_steps:
        if step not in load_commands:
            fail(f"load flow omits required post-load action: {' '.join(step)}")
    save_snapshot_flow = [
        ["campaign", "move"], ["tact"], ["campaign", "fire"], ["tact"],
        ["campaign", "state", "resume_before_save"], ["tact"],
        ["state", "resume_before_save"],
        ["campaign", "home-observe", "resume_before_save"],
        ["campaign", "cargo", "resume_before_save"],
        ["campaign", "tact-resume-owners", "resume_before_save"], ["save", "0"],
    ]
    if not any(save_commands[i : i + len(save_snapshot_flow)] == save_snapshot_flow
               for i in range(len(save_commands) - len(save_snapshot_flow) + 1)):
        fail("save flow does not snapshot after move/fire immediately before saving slot 0")
    load_snapshot_flow = [
        ["click", "120", "74"], ["tick", "20"], ["state", "resume_after_menu_load"],
        ["campaign", "state", "resume_after_load"], ["tact"],
        ["campaign", "home-observe", "resume_after_load"],
        ["campaign", "cargo", "resume_after_load"],
        ["campaign", "tact-resume-owners", "resume_after_load"], ["campaign", "move"],
    ]
    if not any(load_commands[i : i + len(load_snapshot_flow)] == load_snapshot_flow
               for i in range(len(load_commands) - len(load_snapshot_flow) + 1)):
        fail("load flow does not snapshot immediately after slot 0 load and before player input")

    need(save_text, r"^pocketufo: step click 120 124$",
         "save process opens New Game through the real menu control")
    need(save_text, r"^pocketufo: step pick$", "save process places a base through the live placement input")
    need(save_text, r"campaign gear: craft \d+ crew [1-9]\d* cargo rifle 6 clip 10 grenade 4",
         "the original starter cargo remains 6 Rifles, 10 clips, and 4 grenades")
    need(save_text, r"^pocketufo: campaign gear: PASS$",
         "the read-only original starter gear check passes")
    need(save_text, r"campaign contact creation: PASS", "the real contact fixture is declared")
    need(save_text, r"campaign arrival: PASS (?:inventory screen|native screen0 path)",
         "the real arrival and launch path opened the mission")
    need(save_text, r"campaign deploy: committed [1-9]\d* XCOM against [1-9]\d* enemies",
         "the real deployment committed XCOM to the tactical map")
    need(save_text,
         r"campaign move: -?\d+/-?\d+/-?\d+ -> -?\d+/-?\d+/-?\d+, tu \d+ -> \d+, queue drained",
         "a real pointer move changed a live tactical cell")
    need(save_text, r"campaign fire: rifle 4 ammo \d+ -> \d+, tu \d+ -> \d+, target ",
         "a real Rifle action spent ammo and TU before saving")
    need(save_text, r"pocketufo: save slot 0: written \(\d+ soldiers, \d+ craft, \d+ tact items, ui screen 0\)",
         "World_SaveSlot wrote slot 0 while the tactical mission was live")
    if not save_path.is_file() or save_path.stat().st_size == 0:
        fail(f"private slot 0 is missing or empty: {save_path}")
    verify_tactical_chunks(save_path)

    staged_cargo = cargo_snapshot(save_lines, "resume_staged_craft")
    staged_items = staged_cargo["items"]
    assert isinstance(staged_items, dict)
    for obj, quantity, name in ((4, 6, "Rifle"), (38, 10, "clip"), (17, 4, "grenade")):
        if obj not in staged_items or staged_items[obj][0] != quantity:
            fail(f"original {name} grant is missing from craft-index cargo rows: {staged_items.get(obj)}")
    print("ok: original 6/10/4 starter grants match the read-only craft-index cargo rows")

    saved_campaign = campaign_snapshot(save_lines, "resume_before_save")
    saved_tact = tact_snapshot(save_lines, "resume_before_save")
    saved_view = tact_view_snapshot(save_lines, "resume_before_save")
    saved_terrain = tact_terrain_snapshot(save_lines, "resume_before_save")
    saved_loadouts = home_loadout_snapshot(save_lines, "resume_before_save")
    saved_cargo = cargo_snapshot(save_lines, "resume_before_save")
    try:
        saved_owners = parse_tact_resume_owners(save_lines, "resume_before_save")
    except OwnerGraphError as exc:
        fail(str(exc))
    require_idle(saved_tact, "pre-save tactical state")
    if (saved_campaign["screen"], saved_campaign["tact_live"], saved_campaign["tact_flag"],
        saved_campaign["mission"]) != (0, 1, 1, 1):
        fail(f"pre-save snapshot is not an active ordinary mission: {saved_campaign}")
    if saved_campaign["xcom_count"] <= 0 or saved_campaign["enemy_count"] <= 0:
        fail(f"pre-save snapshot has no live tactical rosters: {saved_campaign}")

    need(load_text, r"^pocketufo: state resume_fresh_menu:", "the second process reports its startup menu state")
    need(load_text, r"pocketufo: load slot 0: loaded \(", "the second process loads private slot 0")
    loaded_campaign = campaign_snapshot(load_lines, "resume_after_load")
    loaded_tact = tact_snapshot(load_lines, "resume_after_load")
    loaded_view = tact_view_snapshot(load_lines, "resume_after_load")
    loaded_terrain = tact_terrain_snapshot(load_lines, "resume_after_load")
    loaded_loadouts = home_loadout_snapshot(load_lines, "resume_after_load")
    loaded_cargo = cargo_snapshot(load_lines, "resume_after_load")
    try:
        loaded_owners = parse_tact_resume_owners(load_lines, "resume_after_load")
        compare_tact_resume_owners(saved_owners, loaded_owners)
    except OwnerGraphError as exc:
        fail(str(exc))
    require_idle(loaded_tact, "post-load tactical state")
    if (loaded_campaign["screen"], loaded_campaign["tact_live"], loaded_campaign["tact_flag"],
        loaded_campaign["mission"]) != (0, 1, 1, 1):
        fail(f"post-load snapshot is not the saved live mission: {loaded_campaign}")
    if loaded_campaign["xcom_count"] <= 0 or loaded_campaign["enemy_count"] <= 0:
        fail(f"post-load snapshot has no live tactical rosters: {loaded_campaign}")

    compare_live_state(saved_tact, loaded_tact)
    compare_home_loadouts(saved_loadouts, loaded_loadouts)
    compare_cargo(saved_cargo, loaded_cargo)
    compare_tactical_view(saved_view, loaded_view, saved_tact, loaded_tact)
    compare_tactical_terrain(saved_terrain, loaded_terrain, saved_view, loaded_view)
    compare_state_keys = ("screen", "tact_live", "tact_flag", "mission", "map_signature",
                          "xcom_count", "enemy_count", "reserve", "item_count")
    for key in compare_state_keys:
        if saved_campaign[key] != loaded_campaign[key]:
            fail(f"live campaign count/type changed across processes: {key} "
                 f"{saved_campaign[key]} -> {loaded_campaign[key]}")
    verify_live_equipment(saved_campaign, loaded_campaign)
    print(f"mission terrain signature (mission/tier/variant/damage/sprites): {saved_campaign['map_signature']}")

    need(load_text,
         r"campaign move: -?\d+/-?\d+/-?\d+ -> -?\d+/-?\d+/-?\d+, tu \d+ -> \d+, queue drained",
         "the loaded mission accepts another real move")
    need(load_text, r"campaign fire: rifle 4 ammo \d+ -> \d+, tu \d+ -> \d+, target ",
         "the loaded mission accepts another real Rifle action")
    need(load_text, r"campaign enemyturn: started enemy 1 input 0",
         "the loaded mission accepts a real End Turn")
    require_enemyturn_completed(load_lines)
    need(load_text, r"campaign abort: returned geoscape 1 screen 0 tact 0",
         "the real abort/debrief path returns to the geoscape")
    need(load_text,
         r"^pocketufo: state resume_after_geoscape_return: start screen -?\d+, list mode -?\d+, "
         r"geoscape screen 0, geo ui byte 1, tact flag 0,",
         "the resumed process ends back on the geoscape with no live tactical map")
    print(f"tactical resume passed across separate processes using {save_path.stat().st_size} private save bytes")
    print("ok: reciprocal MapCell/Soldier owners and tactical item-pool owner graph match "
          "across the cold save/load")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
