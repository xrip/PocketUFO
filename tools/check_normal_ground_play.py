#!/usr/bin/env python3
"""Check real tactical play after the natural New Game ground deployment."""

from __future__ import annotations

import re
import sys
from pathlib import Path


FIELD = re.compile(r"([A-Za-z][A-Za-z0-9]*)=([^\s]+)")
CAMPAIGN_STATE = re.compile(
    r"^pocketufo: campaign state (?P<label>[^:]+): screen (?P<screen>-?\d+) "
    r"tact (?P<tactLive>-?\d+)/(?P<tactFlag>-?\d+) mission (?P<mission>-?\d+).*?"
    r"\bxcom (?P<xcom>-?\d+) enemy (?P<enemy>-?\d+) reserve (?P<reserve>-?\d+) "
    r"items (?P<items>-?\d+) queue (?P<queueCursor>-?\d+)/(?P<queueLen>-?\d+) "
    r"active (?P<queueActive>-?\d+) phase (?P<phase>-?\d+) busy (?P<busy>-?\d+) "
    r"enemyturn (?P<enemyTurn>-?\d+) input (?P<playerInput>-?\d+) "
    r"battle (?P<battleEnded>-?\d+) enemyCells (?P<enemyCells>-?\d+) primary9 (?P<primary9>-?\d+)$"
)
TACT_STATE = re.compile(
    r"^pocketufo: tact: busy (-?\d+), phase (-?\d+), enemy turn (-?\d+), "
    r"player input (-?\d+), battle ended (-?\d+), ui screen (-?\d+), "
    r"latch (-?\d+), mode (-?\d+), reserve (-?\d+)$"
)
TACT_QUEUE = re.compile(
    r"^  queue: len (-?\d+), cursor (-?\d+), active (-?\d+), flag0 (-?\d+), "
    r"flag1 (-?\d+), phase max (-?\d+), phase tick (-?\d+)$"
)
TACT_SELECTED = re.compile(
    r"^  cursor -?\d+/-?\d+ layer -?\d+; selected at (-?\d+)/(-?\d+)/(-?\d+), "
    r"uTu (\d+), hp (\d+), status (\d+), facing \d+, kills \d+$"
)
TACT_XCOM = re.compile(
    r"^  xcom (\d+): at (-?\d+)/(-?\d+)/(-?\d+), uTu (\d+), hp (\d+), "
    r"status (\d+), large (\d+)$"
)
TACT_ENEMY = re.compile(
    r"^  enemy (\d+): type (\d+) at (-?\d+)/(-?\d+)/(-?\d+), uTu (\d+), "
    r"hp (\d+), status (\d+), aiTick -?\d+, aiDone \d+, large (\d+)$"
)
CAMPAIGN_SELECTED = re.compile(
    r"^  selected id -?\d+ cell (-?\d+)/(-?\d+)/(-?\d+) tu (\d+) hp (\d+) "
    r"hands (-?\d+)/(-?\d+)$"
)
TACT_ITEM = re.compile(
    r"^  item (\d+) ref -?\d+ object (-?\d+) ammo (\d+) magazine (\d+) "
    r"armed \d+ fuse \d+$"
)
XCOM_MOVE = re.compile(
    r"^pocketufo: campaign move: (-?\d+)/(-?\d+)/(-?\d+) -> "
    r"(-?\d+)/(-?\d+)/(-?\d+), tu (\d+) -> (\d+), queue drained$"
)
MOVE_CANDIDATE = re.compile(
    r"^pocketufo: campaign move candidate: (-?\d+)/(-?\d+)/(-?\d+) "
    r"via pen (-?\d+)/(-?\d+) distance (\d+)$"
)
CELL_SCAN = re.compile(
    r"^pocketufo: campaign cell scan: layer (-?\d+) pixels (-?\d+) near (-?\d+) "
    r"floor (-?\d+) reachable (-?\d+) picks (-?\d+) radius (-?\d+) path (-?\d+) "
    r"searchFlagOne (-?\d+) searchFlagZero (-?\d+)$"
)
QUEUE_DRAIN = re.compile(
    r"^pocketufo: campaign tick drain: (\d+), queue (-?\d+)/(-?\d+) "
    r"active (-?\d+) busy (-?\d+)$"
)
TARGET_CELL = re.compile(
    r"^pocketufo: campaign target cell (-?\d+)/(-?\d+)/(-?\d+) at (-?\d+)/(-?\d+)$"
)
FIRE_RESULT = re.compile(
    r"^pocketufo: campaign fire: rifle 4 ammo (\d+) -> (\d+), tu (\d+) -> (\d+), "
    r"target (-?\d+)/(-?\d+)/(-?\d+), queue (-?\d+) -> (-?\d+), "
    r"shots (\d+), target hp (\d+)$"
)
POINTER_INPUT = re.compile(
    r"^pocketufo: campaign input: pointer widget (\d+) at (-?\d+)/(-?\d+) "
    r"\(pressed (\d+), released (\d+)\)$"
)
ENEMY_TURN_SETTLED = re.compile(
    r"^pocketufo: campaign enemyturn: ticks (\d+) enemy (-?\d+) input (-?\d+) "
    r"busy (-?\d+) queue (-?\d+)/(-?\d+) changed (-?\d+)$"
)
ENEMY_TURN_TERMINAL = re.compile(
    r"^pocketufo: campaign enemyturn: terminal screen (13|18) battle (-?\d+) "
    r"victory (-?\d+) ticks (\d+)$"
)
ABORT_YES = re.compile(
    r"^pocketufo: campaign abort: yes at (-?\d+)/(-?\d+), ended 1 screen 13$"
)
ABORT_RETURNED = re.compile(
    r"^pocketufo: campaign abort: returned geoscape 1 screen 0 tact 0$"
)
RETURN_ACK = re.compile(
    r"^pocketufo: campaign returnack: PASS acked (\d+) lines 0 modal 0 geo 1 screen 0 tact 0 pause 0$"
)
RETURN_ACK_NONE = re.compile(
    r"^pocketufo: campaign returnack: NONE pending lines 0 modal 0 geo 1 screen 0 tact 0 pause 0 timerWindows 2$"
)
RETURN_ACK_BEFORE = re.compile(
    r"^pocketufo: campaign returnack: before timer lines (\d+) firstFlag (\d+) pause (\d+)$"
)
RETURN_ACK_CARD = re.compile(
    r"^pocketufo: campaign returnack: card lines (\d+) firstFlag (\d+) slots (\d+)/(\d+)/(\d+)/(\d+) "
    r"pause (\d+) geo (\d+) screen (-?\d+) modal (\d+) panel (\d+) row (-?\d+) aux (-?\d+) panelClick (\d+)$"
)
RETURN_PROOF = re.compile(r"^pocketufo: campaign returnproof stage=(\S+) (.+)$")
RETURN_PROOF_PASS = re.compile(r"^pocketufo: campaign returnproof: PASS (.+)$")
RETURN_CREW = re.compile(r"^pocketufo: campaign returncrew stage=(\S+) (.+)$")
CARGO_HEADER = re.compile(
    r"^pocketufo: campaign cargo play_returned: craftIndex (-?\d+) craftSlot (-?\d+) "
    r"objects (\d+) sites (\d+)$"
)
CARGO_ITEM = re.compile(
    r"^pocketufo: campaign cargo item play_returned: object (\d+) "
    r"craftIndexQty (\d+) shipSlotQty (\d+)$"
)
ENEMY_FADE_START = re.compile(
    r"^pocketufo: campaign enemyturn fade start counter=(-?\d+) step=(-?\d+) "
    r"dir=(\d+)/(\d+) busy=(\d+) track=(\d+) phase=(\d+) input=(\d+) "
    r"queue=(\d+)/(\d+) active=(\d+)$"
)
ENEMY_FADE_FRAME = re.compile(
    r"^pocketufo: campaign enemyturn fade frame=(\d+) counter=(-?\d+)->(-?\d+) "
    r"step=(-?\d+)->(-?\d+) dir=(\d+)/(\d+)->(\d+)/(\d+) "
    r"timer=0 gameTick=0 enemyAI=0$"
)
ENEMY_FADE_SETTLED = re.compile(
    r"^pocketufo: campaign enemyturn fade: SETTLED frames=(\d+) counter=(-?\d+) "
    r"step=(-?\d+) dir=(\d+)/(\d+) atPlayerInput=(\d+) homeArrival=not-applicable$"
)
APP_RETURN_STATE = re.compile(
    r"^pocketufo: state play_after_return: start screen -?\d+, list mode -?\d+, "
    r"geoscape screen 0, geo ui byte 1, tact flag 0,(?: |$)"
)
SNAPSHOTS = {
    "play_before": "normal_ground_play_before.ppm",
    "play_after_move": "normal_ground_play_after_move.ppm",
    "play_after_fire": "normal_ground_play_after_fire.ppm",
    "play_after_enemyturn": "normal_ground_play_after_enemyturn.ppm",
    "play_after_enemyturn_fade_before": "normal_ground_play_after_enemyturn_fade_before.ppm",
    "play_after_enemyturn_fade_settled": "normal_ground_play_after_enemyturn_fade_settled.ppm",
    "play_returned": "normal_ground_play_returned.ppm",
    "campaign_debrief": "campaign_debrief.ppm",
    "campaign_return_card": "campaign_return_card.ppm",
}


class CheckError(Exception):
    pass


def one_line(lines: list[str], prefix: str) -> tuple[int, str]:
    found = [(i, line) for i, line in enumerate(lines) if line.startswith(prefix)]
    if len(found) != 1:
        raise CheckError(f"expected one {prefix!r} line, found {len(found)}")
    return found[0]


def cell_scan_records(lines: list[str], start: int, stop: int) -> list[tuple[int, dict[str, str]]]:
    names = ("layer", "pixels", "near", "floor", "reachable", "picks", "radius", "path",
             "searchFlagOne", "searchFlagZero")
    result = []
    for i in range(start + 1, stop):
        line = lines[i]
        if not line.startswith("pocketufo: campaign cell scan: "):
            continue
        match = CELL_SCAN.fullmatch(line)
        if match is None:
            raise CheckError(f"malformed phase-scoped campaign cell scan at line {i + 1}")
        result.append((i, dict(zip(names, match.groups()))))
    return result


def return_proof(lines: list[str], stage: str) -> tuple[int, dict[str, str]]:
    prefix = f"pocketufo: campaign returnproof stage={stage} "
    i, line = one_line(lines, prefix)
    match = RETURN_PROOF.match(line)
    if match is None or match.group(1) != stage:
        raise CheckError(f"returnproof stage {stage!r} has changed format")
    return i, dict(FIELD.findall(match.group(2)))


def return_crew_rows(lines: list[str], stage: str) -> list[tuple[int, dict[str, str]]]:
    result = []
    for i, line in enumerate(lines):
        match = RETURN_CREW.fullmatch(line)
        if match is not None and match.group(1) == stage:
            result.append((i, dict(FIELD.findall(match.group(2)))))
    return result


def parse_item_index_ammo(raw: str) -> list[tuple[int, int, int]]:
    entries = []
    for field in raw.split(","):
        try:
            slot_raw, values_raw = field.split(":", 1)
            index_raw, ammo_raw = values_raw.split("/", 1)
            entries.append((int(slot_raw, 0), int(index_raw, 0), int(ammo_raw, 0)))
        except ValueError as exc:
            raise CheckError(f"invalid persistent item-index/ammo slot {field!r}") from exc
    if len(entries) != 25 or [entry[0] for entry in entries] != list(range(25)):
        raise CheckError(f"expected 25 persistent item-index/ammo slots, found {len(entries)}")
    return entries


def verify_debrief_exit(lines: list[str], debrief_i: int, expected_crew: int) -> dict[str, int | str]:
    pre_i, pre = return_proof(lines, "debrief")
    shutdown_i, shutdown = return_proof(lines, "geoscape_after_shutdown")
    if not (debrief_i < pre_i < shutdown_i):
        raise CheckError("carrier/crew snapshots are outside the screen-13 shutdown boundary")
    if (tuple_field(pre, "scene", 2) != (0, 0) or number(pre, "tact") != 1
            or tuple_field(shutdown, "scene", 2) != (1, 0) or number(shutdown, "tact") != 0):
        raise CheckError("returnproof snapshots do not straddle tactical shutdown and geoscape init")

    stable = ("craftIndex", "carrierKey", "carrierId", "craftType", "homeBase",
              "craftSlot", "crew", "crewRows")
    for key in stable:
        if pre.get(key) != shutdown.get(key):
            raise CheckError(f"debrief shutdown changed persistent carrier/crew field {key}")
    if number(shutdown, "crew") != expected_crew or number(shutdown, "crewRows") != expected_crew:
        raise CheckError(f"return kept {shutdown.get('crewRows')} assigned crew rows, expected {expected_crew}")
    if number(pre, "loadoutSlots") != 25 or number(shutdown, "loadoutSlots") != 25:
        raise CheckError("returnproof did not inspect all 25 persistent item-index/ammo slots")

    for record in (pre, shutdown):
        if number(record, "carrierKey") == 0 or number(record, "craftIndex") < 0 \
                or number(record, "craftSlot") < 0:
            raise CheckError("returnproof lacks a live carrier identity/index/slot")
        for key in ("item4CraftIndex", "item4ShipSlot", "item17CraftIndex", "item17ShipSlot",
                    "item38CraftIndex", "item38ShipSlot"):
            if number(record, key) < 0:
                raise CheckError(f"returnproof lacks the numeric stock field {key}")
        if any(value < 0 for value in tuple_field(record, "cargo", 4, ",")):
            raise CheckError("returnproof has a negative carrier cargo column")

    pre_rows = return_crew_rows(lines, "debrief")
    shutdown_rows = return_crew_rows(lines, "geoscape_after_shutdown")
    if len(pre_rows) != expected_crew or len(shutdown_rows) != expected_crew:
        raise CheckError("returnproof lacks one persistent record for every assigned crew member")

    def crew_identity_and_loadout(rows: list[tuple[int, dict[str, str]]], stage: str) -> tuple[list[tuple[object, ...]], int]:
        identities: list[tuple[object, ...]] = []
        seen_indices: set[int] = set()
        seen_keys: set[int] = set()
        rifle_slots = 0
        for _i, record in rows:
            roster_index = number(record, "rosterIndex")
            soldier_key = number(record, "soldierKey")
            assigned = number(record, "assignedCraft")
            unit_type = number(record, "unitType")
            items = parse_item_index_ammo(record["itemIndexAmmo"])
            nonzero_items = sum(1 for _slot, item_index, _ammo in items if item_index != 0)
            if (roster_index in seen_indices or soldier_key == 0 or soldier_key in seen_keys
                    or assigned != number(shutdown if stage == "geoscape_after_shutdown" else pre,
                                          "craftIndex")
                    or nonzero_items != number(record, "loadoutItems") or nonzero_items == 0):
                raise CheckError(f"{stage} crew row lacks stable ownership or persistent item slots")
            seen_indices.add(roster_index)
            seen_keys.add(soldier_key)
            identities.append((roster_index, soldier_key, assigned, unit_type, tuple(items)))
            rifle_slots += sum(1 for _slot, item_index, _ammo in items if item_index == 38)
        identities.sort()
        return identities, rifle_slots

    pre_crew, _pre_rifle_slots = crew_identity_and_loadout(pre_rows, "debrief")
    shutdown_crew, shutdown_rifle_slots = crew_identity_and_loadout(
        shutdown_rows, "geoscape_after_shutdown"
    )
    if pre_crew != shutdown_crew:
        raise CheckError("crew roster indices, pointers, assignments, or types changed across shutdown")
    if shutdown_rifle_slots == 0:
        raise CheckError("no persistent crew loadout retains the original Rifle 4 entry")

    ack_candidates = [(i, line) for i, line in enumerate(lines)
                      if i > shutdown_i and (line.startswith("pocketufo: campaign returnack: PASS acked ")
                                             or line.startswith("pocketufo: campaign returnack: NONE pending "))]
    if len(ack_candidates) != 1:
        raise CheckError(f"expected one return-card outcome after raw shutdown, found {len(ack_candidates)}")
    ack_i, ack_line = ack_candidates[0]
    ack_match = RETURN_ACK.fullmatch(ack_line)
    no_ack_match = RETURN_ACK_NONE.fullmatch(ack_line)
    ack_before_i, ack_before_line = one_line(lines, "pocketufo: campaign returnack: before timer ")
    ack_before_match = RETURN_ACK_BEFORE.fullmatch(ack_before_line)
    ack_card_rows = [(i, RETURN_ACK_CARD.fullmatch(line))
                     for i, line in enumerate(lines[shutdown_i + 1:ack_i], shutdown_i + 1)
                     if line.startswith("pocketufo: campaign returnack: card lines ")]
    ack_card_rows = [(i, match) for i, match in ack_card_rows if match is not None]
    if (ack_before_match is None or ack_before_i <= shutdown_i or ack_before_i >= ack_i
            or not ack_card_rows):
        raise CheckError("returned geoscape did not run the real timer and card observer")
    before_lines, before_flag, before_pause = map(int, ack_before_match.groups())
    if before_pause != 0:
        raise CheckError("return card inspection began while the geoscape clock was paused")
    ack_clicks = [line for line in lines[shutdown_i + 1:ack_i]
                  if line.startswith("pocketufo: campaign returnack: click ")]
    ack_controls = [line for line in lines[shutdown_i + 1:ack_i]
                    if line.startswith("pocketufo: campaign control click: return-") and " at " in line]
    if no_ack_match is not None:
        if ack_clicks or ack_controls:
            raise CheckError("no-pending-card branch made an unneeded acknowledgement input")
        if before_lines != 0 or before_flag != 0 or len(ack_card_rows) != 1:
            raise CheckError("no-pending-card branch began with or later showed an unacknowledged line")
        no_card = ack_card_rows[0][1]
        assert no_card is not None
        card_lines, card_flag, _c0, _c1, _c2, _c3, card_pause, card_geo, card_screen, \
            card_modal, _panel, _row, _aux, _panel_click = map(int, no_card.groups())
        if (card_lines, card_flag, card_pause, card_geo, card_screen, card_modal) != (0, 0, 0, 1, 0, 0):
            raise CheckError("no-pending-card branch did not observe a clear returned geoscape card state")
        if (number(shutdown, "lines") != 0 or number(shutdown, "modal") != 0
                or tuple_field(shutdown, "scene", 2) != (1, 0)
                or number(shutdown, "tact") != 0):
            raise CheckError("no-pending-card marker lacks a clear returned geoscape state")
        ack_kind = "none_pending"
        acked = 0
    elif ack_match is not None:
        acked = int(ack_match.group(1))
        if acked <= 0 or len(ack_clicks) != acked or len(ack_controls) != acked:
            raise CheckError("live return-card acknowledgement count lacks matching real control clicks")
        ack_kind = "native_ack"
    else:
        raise CheckError("return card ended on an unrecognized or invalid marker")

    return_control_inputs = [event for event in pointer_inputs(lines, pre_i + 1, shutdown_i)
                             if event[1] == 128 and event[4] == 1]
    if len(return_control_inputs) != 1:
        raise CheckError("screen-13 debrief did not close through one live widget-128 input")

    proof_pass_i, proof_pass_line = one_line(lines, "pocketufo: campaign returnproof: PASS ")
    proof_pass_match = RETURN_PROOF_PASS.fullmatch(proof_pass_line)
    if proof_pass_match is None:
        raise CheckError("debrief-exit persistence marker has changed format")
    pass_fields = dict(FIELD.findall(proof_pass_match.group(1)))
    if (pass_fields.get("stage") != "debrief_exit_persisted"
            or pass_fields.get("loadoutIndexAmmo") != "stable"
            or pass_fields.get("itemStock") != "observed"
            or number(pass_fields, "carrierId") != number(shutdown, "carrierId")
            or number(pass_fields, "craftIndex") != number(shutdown, "craftIndex")
            or number(pass_fields, "crew") != expected_crew
            or number(pass_fields, "homeArrivalObserved") != number(shutdown, "atBase")
            or not (ack_i < proof_pass_i)):
        raise CheckError("debrief-exit PASS does not match the observed persistent carrier/crew state")

    returned_i, returned_line = one_line(lines, "pocketufo: campaign abort: returned geoscape ")
    if returned_i <= proof_pass_i or ABORT_RETURNED.fullmatch(returned_line) is None:
        raise CheckError("debrief exit did not restore the geoscape after persistence proof")
    abort_pass_i, abort_pass_line = one_line(lines, "pocketufo: campaign abort: PASS")
    if abort_pass_i <= returned_i or "debrief_exit_persisted" not in abort_pass_line:
        raise CheckError("abort PASS did not name the proved debrief-exit stage")
    return {"ackIndex": ack_i, "ackKind": ack_kind, "ackCount": acked,
            "proofIndex": proof_pass_i, "returnedIndex": returned_i,
            "abortPassIndex": abort_pass_i, "proof": shutdown}


def verify_return_cargo(lines: list[str], after_state_i: int,
                        proof: dict[str, str]) -> int:
    header_i, header_line = one_line(lines, "pocketufo: campaign cargo play_returned: craftIndex ")
    header_match = CARGO_HEADER.fullmatch(header_line)
    if header_match is None:
        raise CheckError("returned craft-cargo header has changed format")
    craft_index, craft_slot, _object_count, _site_count = map(int, header_match.groups())
    if (header_i <= after_state_i or craft_index != number(proof, "craftIndex")
            or craft_slot != number(proof, "craftSlot")):
        raise CheckError("returned cargo did not read the debrief carrier's craft index and ship slot")
    cargo_rows = [(i, tuple(map(int, match.groups()))) for i, line in enumerate(lines)
                  if (match := CARGO_ITEM.fullmatch(line)) is not None and i > header_i]
    for nObject in (4, 17, 38):
        found = [values for _i, values in cargo_rows if values[0] == nObject]
        if len(found) != 1 or found[0][1] < 0 or found[0][2] < 0:
            raise CheckError(f"returned cargo lacks one numeric craft-index/ship-slot row for object {nObject}")
    summary_i, _ = one_line(lines, "pocketufo: campaign cargo summary play_returned: ")
    if summary_i <= header_i:
        raise CheckError("returned cargo summary is out of order")
    return header_i


def number(fields: dict[str, str], key: str) -> int:
    try:
        return int(fields[key], 0)
    except (KeyError, ValueError) as exc:
        raise CheckError(f"missing or invalid {key}={fields.get(key)!r}") from exc


def tuple_field(fields: dict[str, str], key: str, count: int,
                separator: str = "/") -> tuple[int, ...]:
    raw = fields.get(key)
    if raw is None or not separator:
        raise CheckError(f"missing or invalid {key}={raw!r}")
    parts = raw.split(separator)
    if len(parts) != count:
        raise CheckError(f"invalid {key} tuple length: expected {count}, found {len(parts)}")
    try:
        return tuple(int(part, 0) for part in parts)
    except ValueError as exc:
        raise CheckError(f"missing or invalid {key}={raw!r}") from exc


def campaign_state(lines: list[str], label: str) -> tuple[int, dict[str, int]]:
    i, line = one_line(lines, f"pocketufo: campaign state {label}:")
    match = CAMPAIGN_STATE.match(line)
    if match is None or match.group("label") != label:
        raise CheckError(f"campaign state {label!r} has changed format")
    return i, {key: int(value) for key, value in match.groupdict().items() if key != "label"}


def tact_snapshot(lines: list[str], after: int, label: str) -> tuple[int, int, dict[str, object] | None]:
    start = next((i for i in range(after + 1, len(lines))
                  if lines[i].startswith("pocketufo: tact:")), -1)
    if start < 0:
        raise CheckError(f"no tact snapshot follows {label}")
    stop = next((i for i in range(start + 1, len(lines))
                 if lines[i] == "pocketufo: step tact"), -1)
    if stop < 0:
        raise CheckError(f"tact snapshot is incomplete after {label}")
    if lines[start] == "pocketufo: tact: no live mission":
        return start, stop, None
    state_match = TACT_STATE.match(lines[start])
    if state_match is None:
        raise CheckError(f"tact header has changed format after {label}")
    state = dict(zip(
        ("busy", "phase", "enemyTurn", "playerInput", "battleEnded", "screen", "latch", "mode", "reserve"),
        map(int, state_match.groups()),
    ))
    queue = None
    selected = None
    selected = None
    xcoms: list[tuple[int, ...]] = []
    enemies: list[tuple[int, ...]] = []
    for line in lines[start + 1 : stop]:
        match = TACT_QUEUE.match(line)
        if match is not None:
            queue = tuple(map(int, match.groups()))
            continue
        match = TACT_SELECTED.match(line)
        if match is not None:
            selected = tuple(map(int, match.groups()))
            continue
        match = TACT_XCOM.match(line)
        if match is not None:
            xcoms.append(tuple(map(int, match.groups())))
            continue
        match = TACT_ENEMY.match(line)
        if match is not None:
            enemies.append(tuple(map(int, match.groups())))
    if queue is None or selected is None:
        raise CheckError(f"tact snapshot lacks queue or selected-unit data after {label}")
    return start, stop, {"state": state, "queue": queue, "selected": selected,
                         "xcoms": xcoms, "enemies": enemies}


def require_live_idle(state: dict[str, int], snapshot: dict[str, object], label: str,
                      expected_xcom: int | None = None) -> None:
    tact_state = snapshot["state"]
    queue = snapshot["queue"]
    selected = snapshot["selected"]
    assert isinstance(tact_state, dict) and isinstance(queue, tuple) and isinstance(selected, tuple)
    for key, expected in (("screen", 0), ("busy", 0), ("phase", 0),
                          ("enemyTurn", 0), ("playerInput", 1), ("battleEnded", 0)):
        if tact_state[key] != expected:
            raise CheckError(f"{label} is not idle player input: {key}={tact_state[key]} expected {expected}")
    if (state["screen"] != 0 or state["tactLive"] != 1 or state["tactFlag"] != 1
            or state["mission"] != 2 or state["battleEnded"] != 0
            or state["busy"] != 0 or state["enemyTurn"] != 0 or state["playerInput"] != 1):
        raise CheckError(f"{label} campaign header is not a live normal mission at player input")
    if state["queueLen"] != 0 or state["queueActive"] != 0 or queue[0] != 0 or queue[2] != 0:
        raise CheckError(f"{label} has queued or active work: campaign cursor/len="
                         f"{state['queueCursor']}/{state['queueLen']} tact len/cursor="
                         f"{queue[0]}/{queue[1]} active={queue[2]}")
    if (state["xcom"] <= 0 or state["enemy"] <= 0
            or len(snapshot["xcoms"]) != state["xcom"]
            or len(snapshot["enemies"]) != state["enemy"]
            or (expected_xcom is not None and state["xcom"] != expected_xcom)
            or any(value < 0 for value in selected[:3])
            or not any(row[1:7] == selected for row in snapshot["xcoms"])):
        raise CheckError(f"{label} lacks live XCOM/enemy rows or changed its crew count")


def campaign_items(lines: list[str], state_index: int) -> tuple[tuple[int, ...] | None, dict[int, tuple[int, int, int]]]:
    stop = next((i for i in range(state_index + 1, len(lines))
                 if lines[i].startswith("pocketufo: tact:")
                 or lines[i].startswith("pocketufo: campaign state ")), len(lines))
    selected = None
    items: dict[int, tuple[int, int, int]] = {}
    for line in lines[state_index + 1 : stop]:
        match = CAMPAIGN_SELECTED.match(line)
        if match is not None:
            selected = tuple(map(int, match.groups()))
            continue
        match = TACT_ITEM.match(line)
        if match is not None:
            item_index, obj, ammo, magazine = map(int, match.groups())
            items[item_index] = (obj, ammo, magazine)
    return selected, items


def pointer_inputs(lines: list[str], start: int, stop: int) -> list[tuple[int, int, int, int, int, int]]:
    result = []
    for i in range(start, stop):
        match = POINTER_INPUT.match(lines[i])
        if match is not None:
            result.append((i, *map(int, match.groups())))
    return result


def ensure_frames(app_dir: Path, filenames: list[str]) -> None:
    for name in filenames:
        path = app_dir / name
        if not path.is_file() or path.stat().st_size <= 32:
            raise CheckError(f"missing or empty real frame {name}: {path}")


def natural_terminal(state: dict[str, int]) -> bool:
    return state["battleEnded"] != 0 or state["screen"] == 13


def natural_return(lines: list[str], app_dir: Path, frames: list[str]) -> None:
    already_i, _ = one_line(lines, "pocketufo: campaign abort: battle already ended on screen 13")
    if any(line.startswith("pocketufo: campaign abort: yes at ") for line in lines):
        raise CheckError("natural result path also claimed a player abort confirmation")
    debrief_i, debrief = campaign_state(lines, "debrief")
    if already_i >= debrief_i or debrief["screen"] != 13 or debrief["battleEnded"] != 1:
        raise CheckError("natural terminal did not follow the real ended-screen debrief path")
    ground_i, ground_line = one_line(lines, "pocketufo: normal-ground PASS ")
    ground = dict(FIELD.findall(ground_line[len("pocketufo: normal-ground PASS ") :]))
    debrief_exit = verify_debrief_exit(lines, debrief_i, number(ground, "expectedCrew"))
    returned_i = int(debrief_exit["returnedIndex"])
    generic_i, generic_line = one_line(lines, "pocketufo: state play_after_return:")
    if generic_i <= returned_i or APP_RETURN_STATE.match(generic_line) is None:
        raise CheckError("natural result return did not restore the live geoscape")
    abort_pass_i = int(debrief_exit["abortPassIndex"])
    if not (returned_i < abort_pass_i < generic_i):
        raise CheckError("natural return did not finish the native abort helper before app-state capture")
    return_state_i, return_state = campaign_state(lines, "play_returned")
    _, tact_stop, tact_state = tact_snapshot(lines, return_state_i, "play_returned")
    if tact_state is not None or tact_stop <= return_state_i or return_state["tactLive"] != 0:
        raise CheckError("natural debrief return still owns a live tactical mission")
    verify_return_cargo(lines, return_state_i, debrief_exit["proof"])
    ensure_frames(app_dir, frames)


def report_natural(stage: str, state: dict[str, int], lines: list[str], app_dir: Path,
                   frames: list[str]) -> int:
    returned = any(line.startswith("pocketufo: campaign abort: returned geoscape ") for line in lines)
    if returned:
        natural_return(lines, app_dir, frames)
        print(f"NATURAL RESULT: stage={stage} screen={state['screen']} "
              f"battleEnded={state['battleEnded']}; real debrief return observed, abort not claimed")
    else:
        ensure_frames(app_dir, frames)
        print(f"NATURAL RESULT: stage={stage} screen={state['screen']} "
              f"battleEnded={state['battleEnded']}; later actions/return were not proven")
    return 2


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_normal_ground_play.py <captured-output> <app-dir>", file=sys.stderr)
        return 64
    log_path = Path(sys.argv[1])
    app_dir = Path(sys.argv[2])
    try:
        lines = log_path.read_bytes().decode("utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n").splitlines()
        for prefix in ("pocketufo: normal-air FAIL ",
                       "pocketufo: normal-air-combat FAIL ",
                       "pocketufo: normal-ground FAIL "):
            if any(line.startswith(prefix) for line in lines):
                raise CheckError(f"required natural arrival/combat/deploy helper failed: {prefix}")
        if any(line.startswith("pocketufo: campaign gear")
               or line.startswith("pocketufo: campaign contact created") for line in lines):
            raise CheckError("play flow used campaign gear or contact seeding")

        arrival_i, arrival = one_line(lines, "pocketufo: normal-air PASS ")
        combat_i, combat = one_line(lines, "pocketufo: normal-air-combat PASS ")
        ground_i, ground_line = one_line(lines, "pocketufo: normal-ground PASS ")
        ground_step_i, _ = one_line(lines, "pocketufo: step normalairground")
        arrival_fields = dict(FIELD.findall(arrival[len("pocketufo: normal-air PASS ") :]))
        combat_fields = dict(FIELD.findall(combat[len("pocketufo: normal-air-combat PASS ") :]))
        ground = dict(FIELD.findall(ground_line[len("pocketufo: normal-ground PASS ") :]))
        if (arrival_fields.get("stage") != "radar_boundary"
                or combat_fields.get("stage") != "terminal"
                or combat_fields.get("outcome") not in ("landed_site", "crashed_site")
                or number(arrival_fields, "seed") != number(combat_fields, "seed")
                or ground.get("stage") != "crew_deployed"
                or number(ground, "siteSeed") != number(combat_fields, "seed")
                or not (arrival_i < combat_i < ground_i < ground_step_i)):
            raise CheckError("play flow did not follow the natural deployed type-4 site")

        before_i, before = campaign_state(lines, "play_before")
        before_tact_i, _, before_tact = tact_snapshot(lines, before_i, "play_before")
        if before_tact is None:
            raise CheckError("play_before has no live tactical mission")
        require_live_idle(before, before_tact, "play_before", expected_xcom=8)
        _, starter_items = campaign_items(lines, before_i)
        if not any(obj == 4 and ammo > 0 and magazine == 38
                   for obj, ammo, magazine in starter_items.values()):
            raise CheckError("deployed New Game has no loaded Rifle 4 with original clip 38")
        before_dump_i, _ = one_line(lines, "pocketufo: step dump normal_ground_play_before.ppm")
        move_passes = [i for i, line in enumerate(lines) if line == "pocketufo: campaign move: PASS"]
        if len(move_passes) != 1 or not (ground_step_i < before_i < before_tact_i < before_dump_i < move_passes[0]):
            raise CheckError("campaign move did not follow the read-only deployed snapshot")
        move_scans = cell_scan_records(lines, before_dump_i, move_passes[0])
        if not any(number(fields, "path") == 1
                   and number(fields, "reachable") > 0
                   and number(fields, "picks") > 0
                   and (number(fields, "searchFlagOne") + number(fields, "searchFlagZero")) > 0
                   for _, fields in move_scans):
            raise CheckError("move preview did not find live reachable cells with the selected unit's type flag")

        move_result_lines = [(i, line, XCOM_MOVE.match(line)) for i, line in enumerate(lines)
                             if XCOM_MOVE.match(line) is not None]
        if len(move_result_lines) != 1:
            raise CheckError(f"expected one completed live move, found {len(move_result_lines)}")
        move_i, _, move_match = move_result_lines[0]
        assert move_match is not None
        if not before_dump_i < move_i < move_passes[0]:
            raise CheckError("completed move result is outside its action stage")
        ox, oy, oz, dx, dy, dz, tu_before, tu_after = map(int, move_match.groups())
        if (ox, oy, oz) == (dx, dy, dz) or tu_after >= tu_before:
            raise CheckError("move did not change the crew cell and spend TU")
        move_candidates = []
        for i, line in enumerate(lines[before_dump_i + 1 : move_i], before_dump_i + 1):
            match = MOVE_CANDIDATE.match(line)
            if match is not None and tuple(map(int, match.groups()[:3])) == (dx, dy, dz):
                move_candidates.append(i)
        if not move_candidates:
            raise CheckError("completed destination was not reached by a live pen candidate")
        move_drains = []
        for i, line in enumerate(lines[move_candidates[-1] + 1 : move_i], move_candidates[-1] + 1):
            match = QUEUE_DRAIN.match(line)
            if match is not None:
                ticks, _cursor, qlen, active, busy = map(int, match.groups())
                if ticks > 0 and qlen == 0 and active == 0 and busy == 0:
                    move_drains.append(i)
        if not move_drains:
            raise CheckError("move did not settle through the real timer and live queue")

        after_move_i, after_move = campaign_state(lines, "play_after_move")
        after_move_tact_i, _, after_move_tact = tact_snapshot(lines, after_move_i, "play_after_move")
        after_move_dump_i, _ = one_line(lines, "pocketufo: step dump normal_ground_play_after_move.ppm")
        if not (move_passes[0] < after_move_i < after_move_tact_i < after_move_dump_i):
            raise CheckError("move snapshot, tactical report, or frame is out of order")
        if natural_terminal(after_move):
            return report_natural("after_move", after_move, lines, app_dir,
                                  ["normal_ground_play_before.ppm", "normal_ground_play_after_move.ppm"])
        if after_move_tact is None:
            raise CheckError("move did not leave a tactical report")
        require_live_idle(after_move, after_move_tact, "play_after_move")
        before_xcoms = before_tact["xcoms"]
        after_move_xcoms = after_move_tact["xcoms"]
        if (not any(row[1:5] == (ox, oy, oz, tu_before) for row in before_xcoms)
                or not any(row[1:5] == (dx, dy, dz, tu_after) for row in after_move_xcoms)):
            raise CheckError("move line does not match the live before/after XCOM cell and TU rows")

        fire_start_i = after_move_dump_i
        fire_results = [(i, FIRE_RESULT.match(line)) for i, line in enumerate(lines)
                        if FIRE_RESULT.match(line) is not None]
        if len(fire_results) != 1:
            raise CheckError(f"expected one completed held-rifle action, found {len(fire_results)}")
        fire_i, fire_match = fire_results[0]
        assert fire_match is not None
        fire_pass_i, _ = one_line(lines, "pocketufo: campaign fire: PASS")
        if not (fire_start_i < fire_i < fire_pass_i):
            raise CheckError("rifle fire evidence is outside its action stage")
        fire_values = tuple(map(int, fire_match.groups()))
        ammo_before, ammo_after, fire_tu_before, fire_tu_after = fire_values[:4]
        tx, ty, tz, queue_before, queue_after, shots, target_hp = fire_values[4:]
        if (ammo_before <= ammo_after or fire_tu_before <= fire_tu_after
                or queue_before != 0 or queue_after != 0 or shots <= 0 or target_hp < 0):
            raise CheckError("rifle action lacks ammo/TU debit, a fired queue command, or target evidence")
        target_cells = [(i, TARGET_CELL.match(line))
                        for i, line in enumerate(lines[fire_start_i + 1 : fire_i], fire_start_i + 1)
                        if TARGET_CELL.match(line) is not None]
        if len(target_cells) != 1 or target_cells[0][1] is None:
            raise CheckError("rifle action lacks one target cell found by the live pen view")
        target_cell_i, target_cell_match = target_cells[0]
        assert target_cell_match is not None
        target_cell = tuple(map(int, target_cell_match.groups()))
        if target_cell[:3] != (tx, ty, tz):
            raise CheckError("fire target coordinates do not match the live enemy pen cell")
        if not any(row[2:5] == (tx, ty, tz) and row[6] > 0 and row[7] < 3
                   for row in after_move_tact["enemies"]):
            raise CheckError("fired target cell is not an active enemy in the pre-fire snapshot")
        fire_inputs = pointer_inputs(lines, fire_start_i + 1, target_cell_i)
        hand_inputs = [event for event in fire_inputs if event[1] in (17, 18) and event[4] == 1]
        snap_inputs = [event for event in fire_inputs if event[1] == 33 and event[4] == 1]
        if (len(hand_inputs) != 1 or len(snap_inputs) != 1
                or not (hand_inputs[0][0] < snap_inputs[0][0])):
            raise CheckError("rifle fire did not use one live held-weapon and snap-mode input")
        fire_drains = []
        for i, line in enumerate(lines[target_cell_i + 1 : fire_i], target_cell_i + 1):
            match = QUEUE_DRAIN.match(line)
            if match is not None:
                ticks, _cursor, qlen, active, busy = map(int, match.groups())
                if ticks > 0 and qlen == 0 and active == 0 and busy == 0:
                    fire_drains.append(i)
        if not fire_drains or not (hand_inputs[0][0] < snap_inputs[0][0]
                                   < target_cell_i < fire_drains[-1] < fire_i < fire_pass_i):
            raise CheckError("rifle input, target, queue drain, and debit evidence is out of order")

        _, before_items = campaign_items(lines, after_move_i)
        after_fire_i, after_fire = campaign_state(lines, "play_after_fire")
        after_fire_tact_i, _, after_fire_tact = tact_snapshot(lines, after_fire_i, "play_after_fire")
        after_fire_dump_i, _ = one_line(lines, "pocketufo: step dump normal_ground_play_after_fire.ppm")
        if not (fire_pass_i < after_fire_i < after_fire_tact_i < after_fire_dump_i):
            raise CheckError("post-fire state, tactical report, or frame is out of order")
        if natural_terminal(after_fire):
            return report_natural("after_fire", after_fire, lines, app_dir,
                                  ["normal_ground_play_before.ppm", "normal_ground_play_after_move.ppm",
                                   "normal_ground_play_after_fire.ppm"])
        if after_fire_tact is None:
            raise CheckError("post-fire state has no live tactical report")
        after_selected, after_items = campaign_items(lines, after_fire_i)
        if after_selected is None or after_selected[3] != fire_tu_after or 4 not in after_selected[-2:]:
            raise CheckError("post-fire selected unit does not show the held rifle and reported TU")
        if not any(index in after_items and before_items[index] == (4, ammo_before, 38)
                   and after_items[index] == (4, ammo_after, 38) for index in before_items):
            raise CheckError("fire debit does not match a live object-4 item before and after the action")
        require_live_idle(after_fire, after_fire_tact, "play_after_fire")

        endturn_start_i, _ = one_line(lines, "pocketufo: campaign enemyturn: started enemy 1 input 0")
        if not after_fire_dump_i < endturn_start_i:
            raise CheckError("End Turn started before the post-fire snapshot")
        endturn_inputs = [event for event in pointer_inputs(lines, after_fire_dump_i + 1, endturn_start_i)
                          if event[1] == 5 and event[4] == 1]
        if len(endturn_inputs) != 1:
            raise CheckError("End Turn lacks one real live widget-5 click")
        terminal_turns = [(i, ENEMY_TURN_TERMINAL.match(line))
                          for i, line in enumerate(lines) if ENEMY_TURN_TERMINAL.match(line) is not None]
        after_enemy_i, after_enemy = campaign_state(lines, "play_after_enemyturn")
        after_enemy_tact_i, _, after_enemy_tact = tact_snapshot(lines, after_enemy_i, "play_after_enemyturn")
        after_enemy_dump_i, _ = one_line(lines, "pocketufo: step dump normal_ground_play_after_enemyturn.ppm")
        if not (endturn_start_i < after_enemy_i < after_enemy_tact_i < after_enemy_dump_i):
            raise CheckError("post-End-Turn snapshot, tactical report, or frame is out of order")
        terminal_turn = next((match for i, match in terminal_turns
                              if match is not None and endturn_start_i < i < after_enemy_i), None)
        if natural_terminal(after_enemy) or terminal_turn is not None:
            if terminal_turn is not None:
                screen, battle, victory, _ticks = map(int, terminal_turn.groups())
                terminal_state = {"screen": screen, "battleEnded": battle, "victory": victory}
            else:
                terminal_state = after_enemy
            return_observed = any(line.startswith("pocketufo: campaign abort: returned geoscape ")
                                  for line in lines[after_enemy_i:])
            needed = ["normal_ground_play_before.ppm", "normal_ground_play_after_move.ppm",
                      "normal_ground_play_after_fire.ppm", "normal_ground_play_after_enemyturn.ppm"]
            if return_observed:
                needed.extend(["campaign_debrief.ppm", "campaign_return_card.ppm",
                               "normal_ground_play_returned.ppm"])
            return report_natural("after_enemyturn", terminal_state, lines, app_dir, needed)

        if after_enemy_tact is None:
            raise CheckError("settled End Turn left no live tactical report")
        settled_lines = [ENEMY_TURN_SETTLED.match(line)
                         for line in lines[endturn_start_i + 1 : after_enemy_i]]
        settled_lines = [match for match in settled_lines if match is not None]
        if len(settled_lines) != 1:
            raise CheckError(f"expected one settled End Turn record, found {len(settled_lines)}")
        ticks, enemy, player_input, busy, _cursor, queue_len, _changed = map(int, settled_lines[0].groups())
        if ticks <= 0 or enemy != 0 or player_input != 1 or busy != 0 or queue_len != 0:
            raise CheckError("End Turn did not settle at player input with an empty queue")
        enemy_pass_i, _ = one_line(lines, "pocketufo: campaign enemyturn: PASS")
        if not endturn_start_i < enemy_pass_i < after_enemy_i:
            raise CheckError("End Turn PASS is outside its snapshot phase")
        fade_start_i, fade_start_line = one_line(lines, "pocketufo: campaign enemyturn fade start ")
        fade_settled_i, fade_settled_line = one_line(lines, "pocketufo: campaign enemyturn fade: SETTLED ")
        fade_before_i, _ = one_line(lines, "pocketufo: campaign frame capture normal_ground_play_after_enemyturn_fade_before.ppm")
        fade_image_i, _ = one_line(lines, "pocketufo: campaign frame capture normal_ground_play_after_enemyturn_fade_settled.ppm")
        fade_start = ENEMY_FADE_START.fullmatch(fade_start_line)
        fade_settled = ENEMY_FADE_SETTLED.fullmatch(fade_settled_line)
        fade_frames = [(i, ENEMY_FADE_FRAME.fullmatch(line))
                       for i, line in enumerate(lines)
                       if line.startswith("pocketufo: campaign enemyturn fade frame=")]
        fade_frames = [(i, match) for i, match in fade_frames if match is not None]
        if (fade_start is None or fade_settled is None
                or not (endturn_start_i < fade_start_i < fade_before_i < fade_image_i
                        < fade_settled_i < enemy_pass_i < after_enemy_i)):
            raise CheckError("paint-only cursor fade did not follow the settled real End Turn")
        start_counter, _step, _dir2, _dir3, fade_busy, _track, _phase, fade_input, \
            fade_queue_cursor, fade_queue_len, fade_queue_active = map(int, fade_start.groups())
        settled_frames, settled_counter, _settled_step, _settled_dir2, _settled_dir3, \
            settled_input = map(int, fade_settled.groups())
        if (fade_busy != 0 or fade_input != 1 or not (0 <= fade_queue_cursor < 1250)
                or fade_queue_len != 0
                or fade_queue_active != 0 or settled_input != 1 or settled_counter != 10
                or settled_frames != len(fade_frames) or settled_frames > 12):
            raise CheckError("cursor fade lost idle gates, queue cursor validity, or counter-10 settlement")
        previous_counter = start_counter
        for expected_frame, (frame_i, frame_match) in enumerate(fade_frames, 1):
            assert frame_match is not None
            frame_no, counter_before, counter_after, step_before, _step_after, \
                direction2_before, direction3_before, direction2_after, direction3_after = map(
                    int, frame_match.groups()
                )
            expected_counter = (counter_before + step_before
                                if 0 < counter_before < 10 else counter_before)
            if (frame_no != expected_frame or counter_before != previous_counter
                    or counter_after != expected_counter
                    or direction2_after != 0 or direction3_after != direction2_before):
                raise CheckError("paint-only cursor fade frame order or counter chain is invalid")
            if not fade_before_i < frame_i < fade_image_i:
                raise CheckError("a cursor fade paint frame is outside its bounded stage")
            previous_counter = counter_after
        if previous_counter != settled_counter:
            raise CheckError("settled cursor fade counter does not match its final paint frame")
        settled_ticks, settled_enemy, settled_input_flag, settled_busy, settled_queue_cursor, \
            settled_queue_len, _settled_changed = map(int, settled_lines[0].groups())
        tact_queue = after_enemy_tact["queue"]
        if (settled_queue_cursor != fade_queue_cursor or settled_queue_len != fade_queue_len
                or after_enemy["queueCursor"] != fade_queue_cursor or after_enemy["queueLen"] != 0
                or not isinstance(tact_queue, tuple) or tact_queue[0] != 0
                or tact_queue[1] != fade_queue_cursor or tact_queue[2] != 0):
            raise CheckError("queue cursor or empty queue changed across the paint-only fade stage")
        require_live_idle(after_enemy, after_enemy_tact, "play_after_enemyturn")

        abort_yes_i, abort_yes_line = one_line(lines, "pocketufo: campaign abort: yes at ")
        if not after_enemy_dump_i < abort_yes_i or ABORT_YES.fullmatch(abort_yes_line) is None:
            raise CheckError("real abort confirmation did not follow the settled End Turn")
        abort_inputs = pointer_inputs(lines, after_enemy_dump_i + 1, abort_yes_i)
        abort_widgets = [event[1] for event in abort_inputs if event[4] == 1]
        if abort_widgets.count(14) != 1 or abort_widgets.count(56) != 1 \
                or abort_widgets.index(14) > abort_widgets.index(56):
            raise CheckError("abort did not use the live menu and confirmation controls in order")
        debrief_i, debrief = campaign_state(lines, "debrief")
        if not abort_yes_i < debrief_i or debrief["screen"] != 13 or debrief["battleEnded"] != 1:
            raise CheckError("real abort did not create the tactical debrief")
        debrief_exit = verify_debrief_exit(lines, debrief_i, number(ground, "expectedCrew"))
        returned_i = int(debrief_exit["returnedIndex"])
        app_return_i, app_return_line = one_line(lines, "pocketufo: state play_after_return:")
        if app_return_i <= returned_i or APP_RETURN_STATE.match(app_return_line) is None:
            raise CheckError("post-abort app state is not the live geoscape")
        returned_campaign_i, returned_campaign = campaign_state(lines, "play_returned")
        returned_tact_i, _, returned_tact = tact_snapshot(lines, returned_campaign_i, "play_returned")
        cargo_i = verify_return_cargo(lines, returned_campaign_i, debrief_exit["proof"])
        returned_dump_i, _ = one_line(lines, "pocketufo: step dump normal_ground_play_returned.ppm")
        abort_pass_i = int(debrief_exit["abortPassIndex"])
        if (returned_tact is not None
                or not (returned_i < abort_pass_i < app_return_i < returned_campaign_i
                        < cargo_i < returned_tact_i < returned_dump_i)
                or returned_campaign["tactLive"] != 0 or returned_campaign["tactFlag"] != 0
                or returned_campaign["xcom"] != -1 or returned_campaign["enemy"] != -1):
            raise CheckError("returned campaign snapshot still owns tactical state")
        ensure_frames(app_dir, [
            "normal_ground_play_before.ppm", "normal_ground_play_after_move.ppm",
            "normal_ground_play_after_fire.ppm", "normal_ground_play_after_enemyturn_fade_before.ppm",
            "normal_ground_play_after_enemyturn_fade_settled.ppm", "normal_ground_play_after_enemyturn.ppm",
            "campaign_debrief.ppm", "campaign_return_card.ppm", "normal_ground_play_returned.ppm",
        ])
        print("PASS: natural ground deployment moved crew, fired a held rifle, settled the paint fade, and verified debrief-exit persistence; home arrival is not claimed")
        return 0
    except (OSError, ValueError, CheckError, AssertionError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
