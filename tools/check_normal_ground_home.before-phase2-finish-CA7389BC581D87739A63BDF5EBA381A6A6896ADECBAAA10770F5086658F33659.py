#!/usr/bin/env python3
"""Check the natural carrier return and its separate recovery pulse."""

from __future__ import annotations

import re
import sys
from pathlib import Path


FIELD = re.compile(r"([A-Za-z][A-Za-z0-9]*)=([^\s]+)")
HOME_OBSERVE = "pocketufo: campaign home-observe stage="
HOME_CARRIER = "pocketufo: campaign home-carrier stage="
HOME_MARKER = "pocketufo: campaign home-marker stage="
HOME_RETURN = "pocketufo: campaign home-return-control stage="
HOME_STOCK = "pocketufo: campaign home-stock stage="
HOME_CREW = "pocketufo: campaign home-crew stage="
HOME_ORDER_MARKER = "pocketufo: campaign home-order input=marker stage="
HOME_ORDER_RETURN = "pocketufo: campaign home-order input=return stage="
HOME_WAIT_START = "pocketufo: campaign home-wait start "
HOME_WAIT_PULSE = "pocketufo: campaign home-wait pulse="
HOME_WAIT_ARRIVAL = "pocketufo: campaign home-wait arrival pulses="
HOME_WAIT_ARRIVAL_DETAIL = "pocketufo: campaign home-wait arrival-detail "
HOME_WAIT_ARRIVAL_STOCK = "pocketufo: campaign home-wait arrival-stock "
HOME_WAIT_CARGO_OBJECT = "pocketufo: campaign home-wait arrival-cargo-object "
ACK_BEFORE = "pocketufo: campaign ack-confirm: before "
ACK_AFTER = "pocketufo: campaign ack-confirm: after "
ACK_SKIP = "pocketufo: campaign ack-confirm: SKIP no_flag10 "
SPEED_CONTROL = re.compile(
    r"^pocketufo: strategy control speed (?P<index>-?\d+) present (?P<present>\d+) "
    r"enabled (?P<enabled>\d+) rect (?P<x>-?\d+)/(?P<y>-?\d+)/(?P<width>\d+)/(?P<height>\d+) "
    r"mode (?P<mode>\d+) state (?P<state>\d+) callback (?P<callback>\d+)$"
)
STRATEGY_TUPLE_RE = re.compile(
    r"^pocketufo: strategy tuple (?P<label>[^:]+): screen (?P<appScreen>-?\d+)/(?P<geoScreen>-?\d+) "
    r"baseCount (?P<baseCount>-?\d+) selectedBase (?P<selectedBase>-?\d+) base (?P<baseValid>-?\d+) "
    r"coords (?P<baseX>-?\d+)/(?P<baseY>-?\d+) funds (?P<funds>-?\d+) sec (?P<sec>-?\d+) "
    r"date (?P<year>-?\d+)/(?P<month>-?\d+)/(?P<day>-?\d+) (?P<hour>-?\d+):(?P<minute>-?\d+) "
    r"tickSec (?P<tickSec>-?\d+) pause (?P<pause>\d+)(?: .*)?$"
)


class CheckError(Exception):
    pass


def records(lines: list[str], prefix: str) -> list[tuple[int, dict[str, str]]]:
    result = []
    for i, line in enumerate(lines):
        if not line.startswith(prefix):
            continue
        row = dict(FIELD.findall(line))
        result.append((i, row))
    return result


def one_record(lines: list[str], prefix: str, label: str) -> tuple[int, dict[str, str]]:
    found = records(lines, prefix)
    if len(found) != 1:
        raise CheckError(f"expected one {label} record, found {len(found)}")
    return found[0]


def stage_record(lines: list[str], prefix: str, stage: str) -> tuple[int, dict[str, str]]:
    found = [(i, row) for i, row in records(lines, prefix) if row.get("stage") == stage]
    if len(found) != 1:
        raise CheckError(f"expected one {stage!r} record under {prefix!r}, found {len(found)}")
    return found[0]


def number(fields: dict[str, str], key: str) -> int:
    try:
        return int(fields[key], 0)
    except (KeyError, ValueError) as exc:
        raise CheckError(f"missing or invalid {key}={fields.get(key)!r}") from exc


def pair(fields: dict[str, str], key: str, delimiter: str = "/") -> tuple[int, int]:
    try:
        a, b = fields[key].split(delimiter, 1)
        return int(a, 0), int(b, 0)
    except (KeyError, ValueError) as exc:
        raise CheckError(f"missing or invalid pair {key}={fields.get(key)!r}") from exc


def tuple_numbers(fields: dict[str, str], key: str, count: int, delimiter: str = ",") -> tuple[int, ...]:
    try:
        values = tuple(int(part, 0) for part in fields[key].split(delimiter))
    except (KeyError, ValueError) as exc:
        raise CheckError(f"missing or invalid tuple {key}={fields.get(key)!r}") from exc
    if len(values) != count:
        raise CheckError(f"expected {count} values in {key}, found {len(values)}")
    return values


def check_ack_segment(lines: list[str], start: int, stop: int, label: str,
                      expected_ack: bool | None = None) -> bool:
    before = [(i, row) for i, row in records(lines, ACK_BEFORE) if start < i < stop]
    after = [(i, row) for i, row in records(lines, ACK_AFTER) if start < i < stop]
    skipped = [(i, row) for i, row in records(lines, ACK_SKIP) if start < i < stop]
    if len(before) == 1 and len(after) == 1 and not skipped:
        if expected_ack is False:
            raise CheckError(f"{label} unexpectedly acknowledged a status card")
        before_i, before_row = before[0]
        after_i, after_row = after[0]
        if not before_i < after_i:
            raise CheckError(f"{label} status ack records are out of order")
        if (number(before_row, "flag") != 10
                or number(before_row, "lineCount") != 1
                or number(before_row, "pause") != 1
                or number(before_row, "selection") != 0
                or number(before_row, "screen") != 0
                or number(before_row, "geo") != 1
                or number(before_row, "tact") != 0
                or number(before_row, "tactOwner") != 0
                or number(before_row, "modal") != 0
                or number(before_row, "confirmOwner") != 1
                or number(before_row, "enabled") != 1
                or number(before_row, "callbackMatch") != 1):
            raise CheckError(f"{label} did not use one enabled live status control")
        x, y = check_input_point(before_row, "center")
        rx, ry, width, height = tuple_numbers(before_row, "rect", 4, "/")
        if not (rx <= x < rx + width and ry <= y < ry + height):
            raise CheckError(f"{label} status click center is outside its live rectangle")
        if (number(after_row, "lineCount") != 0
                or number(after_row, "firstFlag") != -1
                or number(after_row, "pause") != 0
                or number(after_row, "selection") != 0
                or number(after_row, "screen") != 0
                or number(after_row, "geo") != 1
                or number(after_row, "tact") != 0
                or number(after_row, "modal") != 0
                or number(after_row, "tickSec") != 5
                or after_row.get("result") != "PASS"):
            raise CheckError(f"{label} did not clear the live status and restore five-second speed")
        return True
    if len(before) == 0 and len(after) == 0 and len(skipped) == 1:
        if expected_ack is True:
            raise CheckError(f"{label} missed its expected live status ack")
        _, row = skipped[0]
        if (number(row, "screen") != 0 or number(row, "pause") != 0
                or number(row, "selection") != 0):
            raise CheckError(f"{label} reported no status while the geoscape was not clear")
        return False
    raise CheckError(f"{label} has missing, duplicate, or mixed status ack records")


def home_stage(lines: list[str], stage: str) -> dict[str, dict[str, str]]:
    _head_i, head = stage_record(lines, HOME_OBSERVE, stage)
    _carrier_i, carrier = stage_record(lines, HOME_CARRIER, stage)
    _marker_i, marker = stage_record(lines, HOME_MARKER, stage)
    return {"head": head, "carrier": carrier, "marker": marker}


def home_stock_rows(lines: list[str], stage: str) -> dict[int, dict[str, int]]:
    result: dict[int, dict[str, int]] = {}
    for _i, row in records(lines, HOME_STOCK):
        if row.get("stage") != stage:
            continue
        obj = number(row, "object")
        if obj in result:
            raise CheckError(f"duplicate home stock row for stage {stage}, object {obj}")
        result[obj] = {
            "base": number(row, "base"),
            "craftIndex": number(row, "craftIndex"),
            "shipSlot": number(row, "shipSlot"),
        }
    return result


def parse_loadout(raw: str) -> tuple[tuple[int, int, int], ...]:
    entries = []
    try:
        for field in raw.split(","):
            slot_raw, values = field.split(":", 1)
            item_raw, ammo_raw = values.split("/", 1)
            entries.append((int(slot_raw, 0), int(item_raw, 0), int(ammo_raw, 0)))
    except ValueError as exc:
        raise CheckError(f"invalid persisted loadout field {raw!r}") from exc
    if len(entries) != 25 or [x[0] for x in entries] != list(range(25)):
        raise CheckError(f"expected ordered 25-slot loadout, got {len(entries)} slots")
    return tuple(entries)


def home_crew_rows(lines: list[str], stage: str) -> dict[int, dict[str, object]]:
    result: dict[int, dict[str, object]] = {}
    for _i, row in records(lines, HOME_CREW):
        if row.get("stage") != stage:
            continue
        roster_index = number(row, "rosterIndex")
        if roster_index in result:
            raise CheckError(f"duplicate home crew roster index {roster_index} at {stage}")
        if number(row, "present") == 0:
            raise CheckError(f"missing live soldier pointer at roster index {roster_index} in {stage}")
        result[roster_index] = {
            "key": number(row, "soldierKey"),
            "id": number(row, "id"),
            "base": number(row, "base"),
            "assignment": number(row, "assignment"),
            "unitType": number(row, "unitType"),
            "credit": number(row, "credit"),
            "missionValue": number(row, "missionValue"),
            "loadoutItems": number(row, "loadoutItems"),
            "items": parse_loadout(row["items"]),
        }
    return result


def check_input_point(fields: dict[str, str], key: str, width: int = 240,
                      height: int = 320) -> tuple[int, int]:
    x, y = pair(fields, key)
    if not (0 <= x < width and 0 <= y < height):
        raise CheckError(f"{key} is outside the logical screen: {x}/{y}")
    return x, y


def c_div(numer: int, denom: int) -> int:
    """C signed division, truncated toward zero."""
    q = abs(numer) // abs(denom)
    return -q if (numer < 0) != (denom < 0) else q


def check_home_order(lines: list[str]) -> tuple[int, dict[str, int | str]]:
    before_head_i, before_head = stage_record(lines, HOME_OBSERVE, "home_before_order")
    before_i, before = stage_record(lines, HOME_CARRIER, "home_before_order")
    marker_before_i, marker_before = stage_record(lines, HOME_MARKER, "home_before_order")
    point_i, point = stage_record(lines, HOME_ORDER_MARKER, "before")
    marker_after_i, marker_after = stage_record(lines, HOME_ORDER_MARKER, "after")
    return_before_i, return_before = stage_record(lines, HOME_ORDER_RETURN, "before")
    return_after_i, return_after = stage_record(lines, HOME_ORDER_RETURN, "after")
    pass_i, passed = one_record(lines, "pocketufo: campaign home-order: PASS ", "home-order PASS")

    if (pair(before_head, "scene") != (1, 0) or number(before_head, "tact") != 0
            or number(before_head, "modal") != 0 or number(before_head, "lines") != 0
            or number(before_head, "pause") != 0 or number(before_head, "mapSelect") != 0
            or number(before_head, "viewMode") != 0):
        raise CheckError("carrier home order did not start on the clear mode-0 geoscape")

    craft_index = number(before, "craftIndex")
    carrier_id = number(before, "id")
    carrier_key = number(before, "key")
    base = number(before, "homeBase")
    base_key = number(before, "baseKey")
    marker_seed = number(marker_before, "seed")
    marker_sub = number(marker_before, "sub")
    if (number(before, "present") != 1 or carrier_key == 0 or base_key == 0
            or number(before, "atBase") != 0 or number(before, "intercept") != 0
            or number(before, "contactMsg") != 0 or number(marker_before, "present") != 1
            or number(marker_before, "type") != 1 or number(marker_before, "active") != 0
            or number(marker_before, "visible") != 1
            or number(marker_before, "display") != carrier_id):
        raise CheckError("pre-order snapshot is not a visible away carrier with its own marker")
    if (number(point, "craftIndex") != craft_index or number(point, "carrierId") != carrier_id
            or number(point, "carrierKey") != carrier_key
            or number(point, "baseKey") != base_key
            or number(point, "markerSeed") != marker_seed
            or number(point, "markerSub") != marker_sub):
        raise CheckError("home order did not pin the live carrier and own marker")
    if (number(marker_after, "lineFlag") != 11
            or number(marker_after, "lineCraft") != craft_index
            or number(marker_after, "pause") != 1
            or number(marker_after, "markerSub") != marker_sub):
        raise CheckError("real own-marker click did not open the flag-11 carrier card")
    if not (before_head_i < before_i < marker_before_i < point_i < marker_after_i
            < return_before_i < return_after_i < pass_i):
        raise CheckError("home order inputs or state records are out of order")

    cbx, cby = check_input_point(point, "point")
    if pair(point, "marker") != pair(marker_before, "at"):
        raise CheckError("home-order marker input changed the live own-marker coordinates")
    # App_ScriptCampaignHomeOrder emits this input record only after resolving the live card control
    # and passing its owner, enabled, callback, and rectangle guards. The input record itself does
    # not contain a `card` field; marker_after carries the observed line-11 craft-card state.
    if (number(return_before, "craftIndex") != craft_index
            or number(return_before, "carrierId") != carrier_id
            or number(return_before, "carrierKey") != carrier_key
            or number(return_before, "contactMsg") != 0
            or number(return_before, "baseKey") != base_key
            or return_before.get("callback") != "dismiss-selected" \
            or return_before.get("control") != "GEOUI_CTL_186BA8"):
        raise CheckError("Return input did not target the pinned carrier's guarded DismissSelected control")
    rx, ry = check_input_point(return_before, "center")
    rect = tuple_numbers(return_before, "rect", 4, "/")
    if not (rect[0] <= rx < rect[0] + rect[2] and rect[1] <= ry < rect[1] + rect[3]):
        raise CheckError("home Return click center is outside the observed live control rectangle")

    if (number(return_after, "lineCount") != 0 or number(return_after, "pause") != 0
            or number(return_after, "selection") != 0
            or number(return_after, "carrierId") != carrier_id
            or number(return_after, "carrierKey") != carrier_key
            or number(return_after, "baseKey") != base_key
            or number(return_after, "atBase") != 0 or number(return_after, "intercept") != 0
            or number(return_after, "markerSeed") != marker_seed
            or number(return_after, "sub") != 1 or number(return_after, "visible") != 1
            or number(return_after, "timer") != 0):
        raise CheckError("real Return control did not leave the pinned carrier on a visible home leg")
    if pair(return_after, "dest") != pair(return_after, "home"):
        raise CheckError("home order destination differs from the live home base coordinates")
    if pair(passed, "home") != pair(return_after, "home"):
        raise CheckError("home-order PASS changed the live base coordinates")
    armed_head_i, armed_head = stage_record(lines, HOME_OBSERVE, "home_order_armed")
    armed_carrier_i, armed_carrier = stage_record(lines, HOME_CARRIER, "home_order_armed")
    if not pass_i < armed_head_i < armed_carrier_i:
        raise CheckError("home order did not capture its post-input live state")
    if (pair(armed_head, "scene") != (1, 0) or number(armed_head, "tact") != 0
            or number(armed_head, "modal") != 0 or number(armed_head, "lines") != 0
            or number(armed_head, "pause") != 0 or number(armed_head, "mapSelect") != 0
            or number(armed_carrier, "key") != carrier_key
            or number(armed_carrier, "baseKey") != base_key):
        raise CheckError("home order did not leave the pinned carrier on the clear geoscape")
    if (number(passed, "carrierId") != carrier_id
            or number(passed, "craftIndex") != craft_index
            or number(passed, "carrierKey") != carrier_key
            or number(passed, "baseKey") != base_key
            or number(passed, "markerSeed") != marker_seed):
        raise CheckError("home-order PASS changed carrier or marker identity")

    return pass_i, {"craftIndex": craft_index, "carrierId": carrier_id,
                    "carrierKey": carrier_key, "baseKey": base_key, "base": base,
                    "markerSeed": marker_seed,
                    "markerPointX": cbx, "markerPointY": cby,
                    "returnPointX": rx, "returnPointY": ry,
                    "travelMin": number(passed, "travelMin")}


def check_landing_crew(before: dict[int, dict[str, object]],
                       after: dict[int, dict[str, object]], carrier_index: int,
                       transfer_gate: bool) -> None:
    if before.keys() != after.keys():
        raise CheckError("home arrival changed the canonical soldier roster indices")
    for index in before:
        old, new = before[index], after[index]
        if old["key"] != new["key"] or old["id"] != new["id"]:
            raise CheckError(f"home arrival replaced soldier pointer/roster identity at {index}")
        if (old["base"] != new["base"] or old["unitType"] != new["unitType"]
                or old["missionValue"] != new["missionValue"] or old["items"] != new["items"]):
            raise CheckError(f"home landing changed persisted crew identity/loadout at {index}")
        expected_assignment = old["assignment"]
        expected_credit = old["credit"]
        if (transfer_gate and old["assignment"] == carrier_index
                and old["credit"] == 0 and old["missionValue"] != 0):
            expected_credit = c_div(40 * int(old["missionValue"]), 100) & 0xFF
            expected_assignment = 254
        if new["assignment"] != expected_assignment or new["credit"] != expected_credit:
            raise CheckError(f"home landing changed crew assignment/credit outside raw gates at {index}")


def check_home_arrival(lines: list[str], order: dict[str, int | str]) -> tuple[int, dict[str, int | str]]:
    start_i, start = one_record(lines, HOME_WAIT_START, "home-wait start")
    pulse_records = [(i, row) for i, row in records(lines, HOME_WAIT_PULSE)]
    arrival_i, arrival = one_record(lines, HOME_WAIT_ARRIVAL, "home-wait arrival")
    detail_i, detail = one_record(lines, HOME_WAIT_ARRIVAL_DETAIL, "arrival detail")
    pass_i, home_pass = one_record(
        lines, "pocketufo: campaign home-wait: PASS stage=natural_home_arrival ",
        "natural home-arrival PASS")
    final_home_i, _ = stage_record(lines, HOME_OBSERVE, "home_arrival")
    if any(line.startswith("pocketufo: campaign home-wait: FAIL ") for line in lines):
        raise CheckError("home-wait reported a bounded travel/state failure")

    if not pulse_records:
        raise CheckError("home-wait emitted no real C353/paint pulse records")
    if not (int(order["carrierId"]) == number(start, "carrierId")
            and int(order["craftIndex"]) == number(start, "craftIndex")
            and int(order["carrierKey"]) == number(start, "carrierKey")
            and int(order["baseKey"]) == number(start, "baseKey")
            and int(order["base"]) == number(start, "baseIndex")
            and int(order["markerSeed"]) == number(start, "markerSeed")
            and int(order["travelMin"]) == number(start, "travelMin")):
        raise CheckError("home-wait start lost the armed carrier/base/marker/travel identity")
    if number(start, "tickSec") != 60:
        raise CheckError("home-wait did not use the real one-minute speed control")
    start_snapshot = home_stage(lines, "home_wait_start")
    start_head = start_snapshot["head"]
    start_carrier = start_snapshot["carrier"]
    start_marker = start_snapshot["marker"]
    start_home_xy = pair(start_carrier, "baseXY")
    if (pair(start_head, "scene") != (1, 0) or number(start_head, "tact") != 0
            or number(start_head, "modal") != 0 or number(start_head, "lines") != 0
            or number(start_head, "pause") != 0 or number(start_head, "mapSelect") != 0):
        raise CheckError("home wait did not start on a clear live geoscape")
    if (number(start_carrier, "present") != 1
            or number(start_carrier, "key") != int(order["carrierKey"])
            or number(start_carrier, "baseKey") != int(order["baseKey"])
            or number(start_carrier, "craftIndex") != int(order["craftIndex"])
            or number(start_carrier, "id") != int(order["carrierId"])
            or number(start_carrier, "homeBase") != int(order["base"])
            or number(start_carrier, "atBase") != 0
            or number(start_marker, "present") != 1
            or number(start_carrier, "intercept") != 0
            or number(start_marker, "seed") != int(order["markerSeed"])
            or number(start_marker, "type") != 1 or number(start_marker, "active") != 0
            or number(start_marker, "visible") != 1
            or number(start_marker, "display") != int(order["carrierId"])
            or number(start_marker, "index") != number(start, "markerIndex")
            or number(start_marker, "sub") != 1 or number(start_marker, "timer") != 0):
        raise CheckError("home wait did not start from the pinned live away leg")
    if (pair(start, "dest") != start_home_xy
            or pair(start_marker, "dest") != start_home_xy):
        raise CheckError("home wait destination differs from the pinned carrier's live home base")
    elapsed_start = number(start, "elapsedMin")
    travel_min = number(start, "travelMin")
    remaining_start = max(0, travel_min - elapsed_start)
    pulse_bound = remaining_start + 2
    if number(start, "remainingMin") != remaining_start or number(start, "pulseBound") != pulse_bound:
        raise CheckError("home-wait pulse bound was not derived from the live travel remainder")
    if len(pulse_records) > pulse_bound:
        raise CheckError("home-wait exceeded the travel-derived pulse bound")
    last_pulse_index = -1
    arrival_pulse = number(arrival, "pulses")
    if arrival_pulse != len(pulse_records) or arrival_pulse > pulse_bound:
        raise CheckError("arrival did not stop on the last bounded pulse")
    pulse_flag10_positions = []
    for expected_pulse, (i, row) in enumerate(pulse_records, start=1):
        if number(row, "pulse") != expected_pulse:
            raise CheckError("home-wait pulse records are missing or out of order")
        try:
            before_sec_raw, after_sec_raw = row["totalSec"].split("->", 1)
            before_sec, after_sec = int(before_sec_raw, 0), int(after_sec_raw, 0)
        except (KeyError, ValueError) as exc:
            raise CheckError("home-wait pulse lacks a valid totalSec pair") from exc
        if number(row, "delta") != 61 or after_sec - before_sec != 61:
            raise CheckError(f"home-wait pulse {expected_pulse} did not use one 61-second real minute pulse")
        elapsed = (after_sec - number(start, "travelEpoch")) // 60
        remaining = max(0, travel_min - elapsed)
        if number(row, "elapsedMin") != elapsed or number(row, "remainingMin") != remaining:
            raise CheckError(f"home-wait pulse {expected_pulse} disagrees with elapsed travel time")
        if (number(row, "carrierKey") != int(order["carrierKey"])
                or number(row, "carrierId") != int(order["carrierId"])
                or number(row, "baseKey") != int(order["baseKey"])
                or number(row, "baseIndex") != int(order["base"])):
            raise CheckError("home-wait pulse changed the pinned carrier or home-base owner")
        if number(row, "markerSeed") != int(order["markerSeed"]):
            raise CheckError("home-wait pulse changed marker seed")
        if pair(row, "dest") != pair(start, "dest"):
            raise CheckError("home-wait pulse changed the live home destination")
        if number(row, "lineCount") != 0:
            if number(row, "lineCount") != 1 or number(row, "lineFlag") != 10 \
                    or number(row, "pause") != 1 or number(row, "selection") != 0:
                raise CheckError(f"home-wait pulse {expected_pulse} hit an unknown message or pause")
            pulse_flag10_positions.append(i)
        elif (number(row, "lineFlag") != -1 or number(row, "pause") != 0
                or number(row, "selection") != 0):
            raise CheckError(f"home-wait pulse {expected_pulse} has no line but remains paused or selecting")
        if (number(row, "geo") != 1 or number(row, "screen") != 0
                or number(row, "tact") != 0 or number(row, "tactOwner") != 0
                or number(row, "modal") != 0):
            raise CheckError(f"home-wait pulse {expected_pulse} left the live geoscape")
        if expected_pulse < arrival_pulse:
            if (number(row, "atBase") != 0 or number(row, "intercept") != 0
                    or number(row, "markerType") != 1 or number(row, "active") != 0
                    or number(row, "visible") != 1 or number(row, "sub") != 1):
                raise CheckError(f"home-wait pulse {expected_pulse} lost the live away leg")
        last_pulse_index = i

    if not (start_i < pulse_records[0][0] <= last_pulse_index < detail_i < arrival_i < pass_i < final_home_i):
        raise CheckError("home-wait start, pulse, arrival, detail, and pass records are out of order")
    if (number(arrival, "arrivalState") != 1 or number(arrival, "atBase") != 1
            or number(arrival, "intercept") != 0 or number(arrival, "markerSub") != 0
            or number(arrival, "markerVisible") != 0
            or number(arrival, "carrierKey") != int(order["carrierKey"])
            or number(arrival, "carrierId") != int(order["carrierId"])
            or number(arrival, "craftIndex") != int(order["craftIndex"])
            or number(arrival, "baseKey") != int(order["baseKey"])
            or number(arrival, "homeBase") != int(order["base"])
            or number(arrival, "markerSeed") != int(order["markerSeed"])):
        raise CheckError("arrival did not prove the pinned carrier's natural home transition")
    transfer_gate = number(arrival, "transferGateObserved") == 1
    hot_before = number(arrival, "transferHotBefore")
    hot_after = number(arrival, "transferHotAfter")
    if transfer_gate != (hot_before != 0 and hot_after == 0):
        raise CheckError("landing transfer claim does not match the observed bWeaponsHot transition")
    shortfall_before, shortfall_after = pair(detail, "shortfall", "->")
    if shortfall_after != number(arrival, "shortfall"):
        raise CheckError("arrival shortfall does not match the landing pulse snapshot")
    if number(detail, "pulse") != arrival_pulse:
        raise CheckError("arrival detail is not from the terminal home-leg pulse")

    arrival_flag10 = (number(arrival, "lineCount") == 1
                      and number(arrival, "lineFlag") == 10
                      and number(arrival, "pause") == 1
                      and number(arrival, "selection") == 0)
    if number(arrival, "lineCount") not in (0, 1) or (
            number(arrival, "lineCount") == 1 and not arrival_flag10):
        raise CheckError("arrival stopped on an unsupported or ambiguous status card")
    if number(arrival, "lineCount") == 0 and (
            number(arrival, "lineFlag") != -1 or number(arrival, "pause") != 0
            or number(arrival, "selection") != 0):
        raise CheckError("arrival has no status line but remains paused or selecting")
    if (number(arrival, "geo") != 1 or number(arrival, "screen") != 0
            or number(arrival, "tact") != 0 or number(arrival, "tactOwner") != 0
            or number(arrival, "modal") != 0):
        raise CheckError("natural arrival did not remain on the clear geoscape scene")
    if (arrival_flag10 and ("pendingFlag10" not in home_pass
                            or number(home_pass, "pendingFlag10") != 1)):
        raise CheckError("home-wait PASS lost its pending flag-10 marker")
    if not arrival_flag10 and "pendingFlag10" in home_pass:
        raise CheckError("home-wait PASS does not match its pending flag-10 state")

    terminal_pulse_i = pulse_records[-1][0]
    for i in pulse_flag10_positions:
        terminal_ack = i == terminal_pulse_i and arrival_flag10
        if terminal_ack:
            ack_start, ack_stop = pass_i, final_home_i
        else:
            next_pulse = next((j for j, _ in pulse_records if j > i), None)
            if next_pulse is None:
                raise CheckError("a nonterminal flag-10 pulse has no following live pulse")
            ack_start, ack_stop = i, next_pulse
        check_ack_segment(lines, ack_start, ack_stop, "home-wait flag-10 pulse", True)
        speed_ok = [
            (j, row) for j, row in records(lines, "pocketufo: campaign home-wait speed: OK ")
            if ack_start < j < ack_stop and row.get("stage") == "home_wait_after_ack"
        ]
        if (not terminal_ack and (len(speed_ok) != 1
                or number(speed_ok[0][1], "tickSec") != 60)):
            raise CheckError("home-wait did not restore the live one-minute control after flag-10 ack")

    if not arrival_flag10:
        check_ack_segment(lines, pass_i, final_home_i, "final home-wait ack", False)

    final_home = home_stage(lines, "home_arrival")
    carrier = final_home["carrier"]
    marker = final_home["marker"]
    if (number(carrier, "id") != int(order["carrierId"])
            or number(carrier, "craftIndex") != int(order["craftIndex"])
            or number(carrier, "key") != int(order["carrierKey"])
            or number(carrier, "baseKey") != int(order["baseKey"])
            or number(carrier, "homeBase") != int(order["base"])
            or number(carrier, "atBase") != 1 or number(carrier, "intercept") != 0
            or number(carrier, "weaponsHot") != hot_after
            or number(marker, "seed") != int(order["markerSeed"])
            or number(marker, "sub") != 0 or number(marker, "visible") != 0):
        raise CheckError("post-ack home snapshot does not retain the natural arrival state")
    arrival_stocks = {}
    for i, row in records(lines, HOME_WAIT_ARRIVAL_STOCK):
        if not (detail_i < i < arrival_i):
            continue
        obj = number(row, "object")
        if obj in arrival_stocks:
            raise CheckError(f"duplicate arrival stock evidence for object {obj}")
        arrival_stocks[obj] = (number(row, "baseBefore"), number(row, "baseAfter"))
    if not {52, 53, 54, 55, 56, 57, 58, 105}.issubset(arrival_stocks):
        raise CheckError("arrival lacks the raw base ammo/cargo stock snapshot")
    arrival_cargo_objects = {}
    for i, row in records(lines, HOME_WAIT_CARGO_OBJECT):
        if not (detail_i < i < arrival_i):
            continue
        obj = number(row, "object")
        if obj in arrival_cargo_objects:
            raise CheckError(f"duplicate arrival cargo-object row for object {obj}")
        arrival_cargo_objects[obj] = row
    if not {31, 32, 34, 35}.issubset(arrival_cargo_objects):
        raise CheckError("arrival lacks the raw carrier cargo-object slot snapshot")
    final_stock_rows = home_stock_rows(lines, "home_arrival")
    if not set(arrival_stocks).issubset(final_stock_rows):
        raise CheckError("post-ack arrival observer lacks a base stock row seen at landing")
    for obj, (_before, after) in arrival_stocks.items():
        if final_stock_rows[obj]["base"] != after:
            raise CheckError(f"base stock object {obj} changed after the arrival snapshot")
    if not set(arrival_cargo_objects).issubset(final_stock_rows):
        raise CheckError("post-ack arrival observer lacks a carrier cargo row seen at landing")
    for obj, row in arrival_cargo_objects.items():
        if (number(row, "craftIndexAfter") != final_stock_rows[obj]["craftIndex"]
                or number(row, "shipSlotAfter") != final_stock_rows[obj]["shipSlot"]):
            raise CheckError(f"carrier cargo object {obj} changed after the arrival snapshot")

    home_before = home_stage(lines, "home_before_order")
    if (number(home_before["head"], "shortfall") != 0
            or number(start, "shortfall") != number(home_before["head"], "shortfall")
            or shortfall_before != number(start, "shortfall")):
        raise CheckError("home arrival shortfall does not start from the observed clear stores state")
    if transfer_gate and number(carrier, "weaponsHot") != 0:
        raise CheckError("landing transfer gate did not clear the hot flag")
    before_crew = home_crew_rows(lines, "home_before_order")
    after_crew = home_crew_rows(lines, "home_arrival")
    if not before_crew:
        raise CheckError("pre-home snapshot contains no soldier identity/loadout census")
    check_landing_crew(before_crew, after_crew, int(order["craftIndex"]), transfer_gate)

    mount_text = detail["mount"]
    try:
        before_mounts_raw, after_mounts_raw = mount_text.split("->", 1)
        before_mounts = tuple(int(x, 0) for x in before_mounts_raw.split("/"))
        after_mounts = tuple(int(x, 0) for x in after_mounts_raw.split("/"))
    except ValueError as exc:
        raise CheckError("arrival detail has malformed mount identities") from exc
    if len(before_mounts) != 2 or len(after_mounts) != 2:
        raise CheckError("arrival detail did not report two mount slots")
    if before_mounts == (0, 0) and after_mounts == (0, 0):
        mounted_rearm = "not_applicable"
    else:
        mounted_rearm = "observed_state_only"

    final_home_i, final_home = stage_record(lines, HOME_OBSERVE, "home_arrival")
    final_home_carrier_i, _ = stage_record(lines, HOME_CARRIER, "home_arrival")
    final_home_marker_i, _ = stage_record(lines, HOME_MARKER, "home_arrival")
    if not (pass_i < final_home_i < final_home_carrier_i < final_home_marker_i):
        raise CheckError("post-ack arrival snapshot is out of order")
    if (pair(final_home, "scene") != (1, 0) or number(final_home, "tact") != 0
            or number(final_home, "modal") != 0 or number(final_home, "lines") != 0
            or number(final_home, "pause") != 0 or number(final_home, "mapSelect") != 0
            or number(final_home, "shortfall") != shortfall_after):
        raise CheckError("post-ack arrival snapshot retained a paused message")
    return final_home_i, {"craftIndex": int(order["craftIndex"]),
                         "carrierId": int(order["carrierId"]),
                         "carrierKey": int(order["carrierKey"]),
                         "baseKey": int(order["baseKey"]),
                         "transferGate": transfer_gate,
                         "shortfall": number(arrival, "shortfall"),
                         "mountedRearm": mounted_rearm,
                         "arrivalPulse": arrival_pulse,
                         "pulseBound": pulse_bound}


def strategy_tuple(lines: list[str], label: str) -> tuple[int, dict[str, str]]:
    found = []
    for i, line in enumerate(lines):
        match = STRATEGY_TUPLE_RE.fullmatch(line)
        if match is not None and match.group("label") == label:
            found.append((i, match.groupdict()))
    if len(found) != 1:
        raise CheckError(f"expected one {label!r} strategy tuple, found {len(found)}")
    return found[0]


def speed_control(lines: list[str], label: str, index: int) -> tuple[int, dict[str, str]]:
    found = []
    for i, line in enumerate(lines):
        match = SPEED_CONTROL.fullmatch(line)
        if (match is not None and int(match.group("index"), 10) == index
                and match.group("present") == "1"):
            found.append((i, match.groupdict()))
    if len(found) == 0:
        raise CheckError(f"missing live speed control {index} at {label}")
    # App_ScriptStrategyState labels each record but does not attach the tuple label to it;
    # bind it to the nearest matching tuple with the requested label.
    tuple_i, _ = strategy_tuple(lines, label)
    prior = [(i, row) for i, row in found if i < tuple_i]
    later = [(i, row) for i, row in found if i > tuple_i]
    nearest = later[0] if later else (prior[-1] if prior else None)
    if nearest is None:
        raise CheckError(f"speed control {index} is not near strategy tuple {label}")
    return nearest


def check_hourly_pulse(lines: list[str], arrival_i: int, home: dict[str, int | str]) -> dict[str, int | str]:
    before_head_i, before = stage_record(lines, HOME_OBSERVE, "home_before_hour")
    before_carrier_i, before_carrier = stage_record(lines, HOME_CARRIER, "home_before_hour")
    after_head_i, after = stage_record(lines, HOME_OBSERVE, "home_after_hour")
    after_carrier_i, after_carrier = stage_record(lines, HOME_CARRIER, "home_after_hour")
    before_tuple_i, _before_tuple = strategy_tuple(lines, "home_hour_before")
    speed_i, speed = speed_control(lines, "home_hour_before", 7)
    after_tuple_i, after_tuple = strategy_tuple(lines, "home_hour_speed")
    clicks = [(i, line) for i, line in enumerate(lines)
              if line.strip() == "pocketufo: step click 60 308"]
    if len(clicks) != 1:
        raise CheckError(f"expected one live hourly-control click, found {len(clicks)}")
    click_i = clicks[0][0]
    after_speed_i, after_speed = speed_control(lines, "home_hour_speed", 7)
    if not (arrival_i < before_head_i < before_carrier_i < before_tuple_i < speed_i < click_i
            < after_tuple_i < after_speed_i < after_head_i < after_carrier_i):
        raise CheckError("hourly input/snapshot records are out of order")
    # The script's `tick 1` command has no standalone log line. The exact clock delta and last-tick
    # stamps below witness that input; keep the ack scoped to the selected-hour phase.
    check_ack_segment(lines, after_speed_i, after_head_i, "hourly pulse")
    if (number(before_carrier, "atBase") != 1
            or number(before_carrier, "id") != int(home["carrierId"])
            or number(before_carrier, "key") != int(home["carrierKey"])
            or number(before_carrier, "baseKey") != int(home["baseKey"])
            or number(before_carrier, "craftIndex") != int(home["craftIndex"])
            or number(before_carrier, "homeBase") != number(after_carrier, "homeBase")
            or pair(before, "scene") != (1, 0)
            or number(before, "lines") != 0
            or number(before, "pause") != 0
            or number(before, "mapSelect") != 0
            or number(before, "tact") != 0
            or number(before, "modal") != 0):
        raise CheckError("hourly probe did not start from the live arrived carrier")
    # A cold load may start at five seconds; validate the selected mode and live control below.
    if (number(after_tuple, "tickSec") != 3600
            or number(after_speed, "enabled") != 1 or number(after_speed, "callback") != 1
            or number(after_speed, "state") != 1):
        raise CheckError("one-hour speed was not selected through the live speed-7 control")
    rx, ry = number(speed, "x"), number(speed, "y")
    width, height = number(speed, "width"), number(speed, "height")
    if not (rx <= 60 < rx + width and ry <= 308 < ry + height):
        raise CheckError("hourly click 60/308 is outside the live speed-7 rectangle")
    total_before, total_after = number(before, "clock"), number(after, "clock")
    if total_after - total_before != 3601:
        raise CheckError("hourly stage did not run exactly one real 3601-second C353 pulse")
    last_hour_before, last_hour_after = number(before, "lastHour"), number(after, "lastHour")
    hour_ticks = (total_after - last_hour_before) // 3600
    if hour_ticks < 1:
        raise CheckError("hourly pulse did not cross a live 3600-second boundary")
    if (number(after_carrier, "atBase") != 1
            or number(after_carrier, "id") != int(home["carrierId"])
            or number(after_carrier, "key") != int(home["carrierKey"])
            or number(after_carrier, "baseKey") != int(home["baseKey"])
            or number(after_carrier, "craftIndex") != int(home["craftIndex"])
            or number(after_carrier, "homeBase") != number(before_carrier, "homeBase")):
        raise CheckError("hourly pulse changed the arrived carrier owner/home state")
    if (pair(after, "scene") != (1, 0) or number(after, "tact") != 0
            or number(after, "modal") != 0 or number(after, "lines") != 0
            or number(after, "pause") != 0 or number(after, "mapSelect") != 0
            or number(after, "shortfall") != number(before, "shortfall")):
        raise CheckError("hourly pulse did not return to a clear live geoscape")

    before_range, before_max = pair(before_carrier, "range")
    after_range, after_max = pair(after_carrier, "range")
    if before_max != after_max:
        raise CheckError("hourly pulse changed range maximum")
    before_fuel, before_fuel_max = pair(before_carrier, "fuel")
    after_fuel, after_fuel_max = pair(after_carrier, "fuel")
    if before_fuel_max != after_fuel_max:
        raise CheckError("hourly pulse changed fuel maximum")
    before_rate = number(before_carrier, "rate")
    before_needs_supply = number(before_carrier, "needsSupply")
    before_minute = number(before, "lastMinute")
    minute_ticks = (total_after - before_minute) // 60
    if minute_ticks < 1:
        raise CheckError("hourly pulse did not dispatch any live minute ship ticks")
    expected_fuel = min(before_fuel_max, before_fuel + hour_ticks * (1 if number(before_carrier, "type") in (0, 3) else 2))
    if before_fuel < before_fuel_max and after_fuel != expected_fuel:
        raise CheckError("hourly fuel change does not match the raw per-type refuel rate")
    if before_fuel == before_fuel_max and after_fuel != before_fuel:
        raise CheckError("already-full fuel changed during the hourly probe")

    if before_needs_supply == 0:
        expected_range = min(before_max, before_range + before_rate * minute_ticks)
        if after_range != expected_range:
            raise CheckError("minute range recovery did not match the live rate/max gate")
        range_result = "minute_recovery_observed" if after_range > before_range else "already_full_or_zero_rate"
    else:
        before_stock_rows = home_stock_rows(lines, "home_before_hour")
        after_stock_rows = home_stock_rows(lines, "home_after_hour")
        if 105 not in before_stock_rows or 105 not in after_stock_rows:
            raise CheckError("supply-backed hourly range path lacks object-105 base stock")
        stock = before_stock_rows[105]["base"]
        expected_range = before_range
        for _ in range(hour_ticks):
            if expected_range < before_max and stock > before_rate:
                expected_range = min(before_max, expected_range + 1000 * before_rate)
                stock -= before_rate
        if after_range != expected_range:
            raise CheckError("supply-backed range recovery does not match object-105 stock gates")
        if after_stock_rows[105]["base"] != stock:
            raise CheckError("hourly range supply did not debit the raw object-105 base stock")
        range_result = "hourly_supply_recovery_observed" if after_range > before_range else "supply_gate_closed"

    return {"hourTicks": hour_ticks, "minuteTicks": minute_ticks,
            "fuelBefore": before_fuel, "fuelAfter": after_fuel,
            "rangeBefore": before_range, "rangeAfter": after_range,
            "rangeResult": range_result}


def check_save_load(lines: list[str], hour_i: int, home: dict[str, int | str]) -> None:
    saved_i = next((i for i, line in enumerate(lines) if line == "pocketufo: step save slot 0"), -1)
    loaded_i = next((i for i, line in enumerate(lines) if line == "pocketufo: step load slot 0"), -1)
    after_hour_head_i, _ = stage_record(lines, HOME_OBSERVE, "home_after_hour")
    after_hour_carrier_i, _ = stage_record(lines, HOME_CARRIER, "home_after_hour")
    after_hour_marker_i, _ = stage_record(lines, HOME_MARKER, "home_after_hour")
    after_load_head_i, _ = stage_record(lines, HOME_OBSERVE, "home_after_load")
    after_load_carrier_i, _ = stage_record(lines, HOME_CARRIER, "home_after_load")
    after_load_marker_i, _ = stage_record(lines, HOME_MARKER, "home_after_load")
    if not (hour_i == after_hour_head_i < after_hour_carrier_i < after_hour_marker_i
            < saved_i < loaded_i < after_load_head_i < after_load_carrier_i < after_load_marker_i):
        raise CheckError("fresh private save/load did not follow the home and hourly observations")
    after_hour = home_stage(lines, "home_after_hour")
    after_load = home_stage(lines, "home_after_load")
    for snapshot in (after_hour["carrier"], after_load["carrier"]):
        if (number(snapshot, "present") != 1 or number(snapshot, "key") == 0
                or number(snapshot, "basePresent") != 1 or number(snapshot, "baseKey") == 0):
            raise CheckError("save/load snapshot lacks a live carrier or home-base owner")
    for field in ("craftIndex", "id", "type", "homeBase", "atBase", "intercept", "weaponsHot",
                  "needsSupply", "range", "fuel", "mount", "ammo25", "ammo26", "ammo27",
                  "ammo29", "ammo30", "cargo", "slot"):
        if after_hour["carrier"].get(field) != after_load["carrier"].get(field):
            raise CheckError(f"save/load changed home carrier field {field}")
    if number(after_load["carrier"], "atBase") != 1:
        raise CheckError("save/load did not keep the carrier at its home base")
    if (pair(after_load["head"], "scene") != (1, 0)
            or number(after_load["head"], "lines") != 0
            or number(after_load["head"], "pause") != 0
            or number(after_load["head"], "mapSelect") != 0
            or number(after_load["head"], "tact") != 0
            or number(after_load["head"], "modal") != 0):
        raise CheckError("save/load did not return to the clear live geoscape")
    if number(after_hour["head"], "shortfall") != number(after_load["head"], "shortfall"):
        raise CheckError("save/load changed the observed supply-shortfall flag")
    if number(after_hour["marker"], "seed") != number(after_load["marker"], "seed"):
        raise CheckError("save/load changed the carrier marker seed")
    stocks_hour = home_stock_rows(lines, "home_after_hour")
    stocks_load = home_stock_rows(lines, "home_after_load")
    if stocks_hour != stocks_load:
        raise CheckError("save/load changed observed base/craft/ship-slot stock quantities")
    crew_hour = home_crew_rows(lines, "home_after_hour")
    crew_load = home_crew_rows(lines, "home_after_load")
    if crew_hour.keys() != crew_load.keys():
        raise CheckError("save/load changed canonical soldier roster indices")
    for index in crew_hour:
        old, new = crew_hour[index], crew_load[index]
        if old["key"] == 0 or new["key"] == 0:
            raise CheckError(f"save/load has no live soldier pointer at roster {index}")
        if old["items"] != new["items"]:
            raise CheckError(f"save/load changed a soldier's 25 item-index/ammo pairs at roster {index}")


def check_frames(app_dir: Path) -> None:
    required = (
        "normal_ground_play_before.ppm",
        "normal_ground_play_after_move.ppm",
        "normal_ground_play_after_fire.ppm",
        "normal_ground_play_after_enemyturn.ppm",
        "normal_ground_play_after_enemyturn_fade_before.ppm",
        "normal_ground_play_after_enemyturn_fade_settled.ppm",
        "normal_ground_play_returned.ppm",
        "normal_ground_home_before_order.ppm",
        "normal_ground_home_ordered.ppm",
        "normal_ground_home_arrival.ppm",
        "normal_ground_home_after_hour.ppm",
        "normal_ground_home_after_load.ppm",
    )
    for name in required:
        path = app_dir / name
        if not path.is_file() or path.stat().st_size <= 32:
            raise CheckError(f"missing or empty saved frame {path}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_normal_ground_home.py <captured-output> <app-dir>", file=sys.stderr)
        return 64
    log_path = Path(sys.argv[1])
    app_dir = Path(sys.argv[2])
    try:
        lines = log_path.read_bytes().decode("utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n").splitlines()
        if any(line.startswith("pocketufo: normal-air FAIL ")
               or line.startswith("pocketufo: normal-air-combat FAIL ")
               or line.startswith("pocketufo: normal-ground FAIL ")
               or line.startswith("pocketufo: campaign home-order: FAIL ")
               or line.startswith("pocketufo: campaign home-wait: FAIL ")
               or line.startswith("pocketufo: campaign home-wait speed: FAIL ")
               or line.startswith("pocketufo: campaign ack-confirm: FAIL ")
               or line.startswith("pocketufo: campaign ack-status: FAIL ")
               for line in lines):
            raise CheckError("an existing natural stage or a home input/wait helper failed")

        if sum(line.startswith("pocketufo: campaign home-order: PASS ") for line in lines) != 1:
            raise CheckError("the real carrier Return control was not accepted exactly once")
        order_pass_i, order = one_record(lines, "pocketufo: campaign home-order: PASS ", "home-order pass")
        order_i, order_data = check_home_order(lines)
        if order_i != order_pass_i:
            raise CheckError("home-order pass records disagree")
        abort_pass_i, _ = one_record(
            lines, "pocketufo: campaign abort: PASS stage=debrief_exit_persisted",
            "persisted debrief exit")
        home_before_i, _ = stage_record(lines, HOME_OBSERVE, "home_before_order")
        if not abort_pass_i < home_before_i < order_pass_i:
            raise CheckError("carrier home order did not follow the persisted tactical debrief exit")

        arrival_i, arrival = check_home_arrival(lines, order_data)
        if not (order_pass_i < arrival_i):
            raise CheckError("home arrival predates the live carrier home order")
        hour_result = check_hourly_pulse(lines, arrival_i, order_data)
        after_hour_i, _ = one_record(lines, "pocketufo: campaign home-observe stage=home_after_hour ",
                                      "post-hour observer")
        check_save_load(lines, after_hour_i, order_data)
        check_frames(app_dir)

        mount_result = arrival.get("mountedRearm", "unknown")
        print(f"HOME FLOW PASS: carrier={arrival['carrierId']} craft={arrival['craftIndex']} "
              f"arrival_pulses={arrival['arrivalPulse']}/{arrival['pulseBound']} "
              f"landing_transfer={int(bool(arrival['transferGate']))} landing_shortfall={arrival['shortfall']} "
              f"mount_rearm={mount_result} "
              f"hour_ticks={hour_result['hourTicks']} minute_ticks={hour_result['minuteTicks']} "
              f"range={hour_result['rangeBefore']}->{hour_result['rangeAfter']} "
              f"recovery={hour_result['rangeResult']} save_load=checked")
        return 0
    except (CheckError, OSError) as exc:
        print(f"HOME FLOW FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
