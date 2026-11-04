#!/usr/bin/env python3
"""Check a retained natural air-to-ground deployment run."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Optional


FIELD = re.compile(r"([A-Za-z][A-Za-z0-9]*)=([^\s]+)")
SNAPSHOTS = {
    "site_before": "normal_ground_site_before.ppm",
    "options": "normal_ground_options.ppm",
    "carrier_selected": "normal_ground_carrier_selected.ppm",
    "flag20": "normal_ground_flag20.ppm",
    "ordered": "normal_ground_ordered.ppm",
    "flag23": "normal_ground_flag23.ppm",
    "inventory_entry": "normal_ground_inventory.ppm",
    "deployed": "normal_ground_deployed.ppm",
}


def records(lines: list[str], prefix: str) -> list[tuple[int, dict[str, str]]]:
    return [
        (i, dict(FIELD.findall(line[len(prefix) :])))
        for i, line in enumerate(lines)
        if line.startswith(prefix)
    ]


def one_record(lines: list[str], prefix: str) -> tuple[int, dict[str, str]]:
    found = records(lines, prefix)
    if len(found) != 1:
        raise ValueError(f"expected one {prefix!r} record, found {len(found)}")
    return found[0]


def stage_record(lines: list[str], prefix: str, stage: str) -> tuple[int, dict[str, str]]:
    found = [(i, fields) for i, fields in records(lines, prefix)
             if fields.get("stage") == stage]
    if len(found) != 1:
        raise ValueError(f"expected one {stage!r} record under {prefix!r}, found {len(found)}")
    return found[0]


def site_stage_from_records(lines: list[str], stage: str, seed: int) -> dict[str, str]:
    _, fields = stage_record(lines, "pocketufo: normal-ground site ", stage)
    if number(fields, "seedExpected") != seed:
        raise ValueError(f"site stage {stage!r} belongs to another contact")
    return fields


def number(fields: dict[str, str], key: str) -> int:
    try:
        return int(fields[key], 0)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"missing or invalid {key}={fields.get(key)!r}") from exc


def pair(fields: dict[str, str], key: str) -> tuple[int, int]:
    try:
        left, right = fields[key].split("/", 1)
        return int(left, 0), int(right, 0)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"missing or invalid {key}=x/y") from exc


def check_point(fields: dict[str, str]) -> None:
    if not (0 <= number(fields, "pointX") < 240
            and 0 <= number(fields, "pointY") < 320):
        raise ValueError("real pointer point is outside the logical display")


def check_site(fields: dict[str, str], seed: int, require_visible: bool = True) -> int:
    if (number(fields, "seedExpected") != seed
            or number(fields, "seed") != seed
            or number(fields, "present") != 1
            or number(fields, "type") != 4
            or number(fields, "active") != 1
            or number(fields, "timer") <= 0):
        raise ValueError("ground flow lost the same live type-4 site")
    if require_visible and number(fields, "visible") != 1:
        raise ValueError("site is no longer visible for a real map click")
    index = number(fields, "currentIndex")
    if index < 0:
        raise ValueError("live site has no current array index")
    return index


def check_carrier(fields: dict[str, str], craft_id: int,
                  craft_index: Optional[int] = None, ordered: bool = False) -> int:
    if (number(fields, "expectedId") != craft_id
            or number(fields, "id") != craft_id
            or number(fields, "type") != 0):
        raise ValueError("ground flow changed the type-0 transport identity")
    index = number(fields, "currentIndex")
    if craft_index is not None and index != craft_index:
        raise ValueError("transport array index changed without a matching live rebind")
    if ordered and (number(fields, "atBase") != 0 or number(fields, "intercept") != 1):
        raise ValueError("transport did not keep its real site order")
    return index


def check_frames(log_path: Path) -> None:
    app_dir = log_path.parent / "app"
    for stage, filename in SNAPSHOTS.items():
        path = app_dir / filename
        if not path.is_file() or path.stat().st_size <= 32:
            raise ValueError(f"missing or empty real frame for {stage}: {path}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_normal_ground.py <captured-output>", file=sys.stderr)
        return 2

    log_path = Path(sys.argv[1])
    try:
        lines = log_path.read_bytes().decode("utf-8", errors="replace").splitlines()
        for prefix in ("pocketufo: normal-air FAIL ",
                       "pocketufo: normal-air-combat FAIL ",
                       "pocketufo: normal-ground FAIL "):
            if any(line.startswith(prefix) for line in lines):
                raise ValueError(f"helper failed before its required ground milestone: {prefix}")
        if sum(line.startswith("pocketufo: step normalairground") for line in lines) != 1:
            raise ValueError("normalairground command is missing or repeated")

        arrival_i, arrival = one_record(lines, "pocketufo: normal-air PASS ")
        combat_i, combat = one_record(lines, "pocketufo: normal-air-combat PASS ")
        origin_i, origin = one_record(lines, "pocketufo: normal-ground origin ")
        ground_pass_i, ground_pass = one_record(lines, "pocketufo: normal-ground PASS ")
        ground_step_i, _ = one_record(lines, "pocketufo: step normalairground")
        if (arrival.get("stage") != "radar_boundary"
                or combat.get("stage") != "terminal"
                or combat.get("outcome") not in ("landed_site", "crashed_site")):
            raise ValueError("the chain did not reach a natural land type-4 combat outcome")
        site_seed = number(combat, "seed")
        if (number(origin, "siteSeed") != site_seed
                or origin.get("siteKind") != "type4"
                or not (arrival_i < combat_i < origin_i < ground_pass_i < ground_step_i)):
            raise ValueError("ground flow did not follow the saved natural air result")

        site_start_i, site_start_state = stage_record(
            lines, "pocketufo: normal-ground state ", "site_start"
        )
        site_start_i2, site_start = stage_record(
            lines, "pocketufo: normal-ground site ", "site_start"
        )
        site_after_i, site_after = stage_record(
            lines, "pocketufo: normal-ground site ", "site_after_snapshot"
        )
        base_click_i, base_click = one_record(
            lines, "pocketufo: normal-ground input control=base_marker "
        )
        options_state_i, options_state = stage_record(
            lines, "pocketufo: normal-ground state ", "options_ready"
        )
        options_site = site_stage_from_records(lines, "options_ready", site_seed)
        check_site(options_site, site_seed)
        options_input_i, options_input = one_record(
            lines, "pocketufo: normal-ground input control=options_row "
        )
        carrier_state_i, carrier_state = stage_record(
            lines, "pocketufo: normal-ground state ", "carrier_selected_after_snapshot"
        )
        carrier_site_i, carrier_site = stage_record(
            lines, "pocketufo: normal-ground site ", "carrier_selected_after_snapshot"
        )
        carrier_i, carrier = stage_record(
            lines, "pocketufo: normal-ground carrier ", "carrier_selected_after_snapshot"
        )

        site_index_from_start = check_site(site_start, site_seed)
        check_site(site_after, site_seed)
        if (number(origin, "siteIndex") != site_index_from_start
                or number(site_start_state, "screen") != 0
                or number(site_start_state, "mapMode") != 0
                or number(site_start_state, "geoUi") != 1
                or number(site_start_state, "paused") != 0
                or number(site_start_state, "modal") != 0
                or number(site_start_state, "radarActive") != 0
                or number(site_start_state, "lineCount") != 0
                or number(site_start_state, "lineFlag") != 0
                or number(site_after, "seed") != site_seed):
            raise ValueError("landed site did not start from the clear running map")
        check_point(base_click)
        if number(base_click, "index") < 0 or number(base_click, "siteId") < 0:
            raise ValueError("base Options did not come from a live base-marker click")
        if (number(options_state, "screen") != 18
                or number(options_state, "mapMode") != 0
                or number(options_state, "geoUi") != 1
                or number(options_state, "paused") != 1
                or number(options_state, "modal") != 0
                or number(options_state, "lineCount") != 0
                or number(options_state, "lineFlag") != 0
                or number(options_state, "clock") != number(site_start_state, "clock")):
            raise ValueError("real base click did not open paused Options over the same site")
        if not (site_start_i < site_start_i2 < site_after_i < base_click_i
                < options_state_i < options_input_i):
            raise ValueError("site capture, base click, and Options row input are out of order")

        row = number(options_input, "row")
        rows = [fields for _, fields in records(lines, "pocketufo: normal-ground options-row ")
                if number(fields, "row") == row]
        if len(rows) != 1:
            raise ValueError("clicked Options row lacks one live row census")
        selected_row = rows[0]
        carrier_id = number(selected_row, "craftId")
        carrier_index = number(selected_row, "craftIndex")
        if (number(selected_row, "visible") != 1
                or number(selected_row, "nameAux") != carrier_index
                or number(selected_row, "readyAux") != 0
                or number(selected_row, "type") != 0
                or number(selected_row, "atBase") != 1
                or number(selected_row, "rangeRemain") != number(selected_row, "rangeMax")
                or number(selected_row, "fuel") != number(selected_row, "fuelMax")
                or number(selected_row, "assignedCrew") != 8
                or number(options_input, "craftIndex") != carrier_index
                or number(options_input, "craftId") != carrier_id
                or number(options_input, "type") != 0
                or number(options_input, "readyAux") != 0
                or number(options_input, "assignedCrew") != 8):
            raise ValueError("selected row is not a ready type-0 transport with eight assigned soldiers")
        check_point(options_input)
        carrier_index_selected = check_carrier(carrier, carrier_id)
        if (number(carrier_state, "screen") != 0
                or number(carrier_state, "mapMode") != 0
                or number(carrier_state, "geoUi") != 1
                or number(carrier_state, "paused") != 1
                or number(carrier_state, "lineFlag") != 27
                or number(carrier_state, "selection") != 1
                or number(carrier_state, "selectedShip") != carrier_index_selected
                or number(carrier_state, "slot0") != carrier_index_selected):
            raise ValueError("real Options row click did not select the carrier while preserving the site")
        carrier_index = carrier_index_selected
        check_site(carrier_site, site_seed)
        if (number(carrier, "atBase") != 1 or number(carrier, "intercept") != 0
                or number(carrier, "assignedCrew") != 8):
            raise ValueError("selected transport row lost its ready, crew-bearing state")

        site_click_i, site_click = one_record(
            lines, "pocketufo: normal-ground input control=site_marker "
        )
        flag20_input_i, flag20_input = one_record(
            lines, "pocketufo: normal-ground input control=flag20_b "
        )
        ground_site_map_click_prefix = "pocketufo: normal-order map-click "
        ground_site_map_clicks = [
            (i, dict(FIELD.findall(line[len(ground_site_map_click_prefix) :])))
            for i, line in enumerate(lines)
            if line.startswith(ground_site_map_click_prefix)
            and carrier_state_i < i
            and i < flag20_input_i
        ]
        flag20_state_i, flag20_state = stage_record(
            lines, "pocketufo: normal-ground state ", "flag20"
        )
        flag20_site_i, flag20_site = stage_record(
            lines, "pocketufo: normal-ground site ", "flag20"
        )
        flag20_carrier_i, flag20_carrier = stage_record(
            lines, "pocketufo: normal-ground carrier ", "flag20"
        )
        flag20_after_i, flag20_after = stage_record(
            lines, "pocketufo: normal-ground state ", "flag20_after_snapshot"
        )
        flag20_after_site_i, flag20_after_site = stage_record(
            lines, "pocketufo: normal-ground site ", "flag20_after_snapshot"
        )
        flag20_after_carrier_i, flag20_after_carrier = stage_record(
            lines, "pocketufo: normal-ground carrier ", "flag20_after_snapshot"
        )
        if len(ground_site_map_clicks) != 1:
            raise ValueError("selected carrier phase must contain one live target map click before flag-20 control B")
        site_map_click_i, site_map_click = ground_site_map_clicks[0]
        if not (carrier_state_i < site_map_click_i < site_click_i < flag20_state_i < flag20_site_i
                < flag20_carrier_i < flag20_after_i < flag20_after_site_i
                < flag20_after_carrier_i < flag20_input_i):
            raise ValueError("carrier select, site click, and live flag-20 control are out of order")
        site_index_at20 = check_site(flag20_site, site_seed)
        carrier_index_at20 = check_carrier(flag20_carrier, carrier_id)
        check_point(site_click)
        check_point(flag20_input)
        map_click_x, map_click_y = pair(site_map_click, "point")
        map_item_x, map_item_y = pair(site_map_click, "item")
        if (number(site_map_click, "index") != number(site_click, "siteIndex")
                or number(site_click, "siteSeed") != site_seed
                or map_click_x != number(site_click, "pointX")
                or map_click_y != number(site_click, "pointY")
                or map_item_x != number(flag20_site, "x")
                or map_item_y != number(flag20_site, "y")
                or number(site_map_click, "mode") != 0
                or number(site_map_click, "pausedBefore") != 1
                or number(site_map_click, "flagBefore") != 27
                or number(site_map_click, "selectionBefore") != 1
                or number(site_map_click, "pausedAfter") != 1
                or number(site_map_click, "flagAfter") != 20
                or number(site_map_click, "selectionAfter") != 1
                or number(flag20_state, "lineFlag") != 20
                or number(flag20_state, "screen") != 0
                or number(flag20_state, "mapMode") != 0
                or number(flag20_state, "geoUi") != 1
                or number(flag20_state, "paused") != 1
                or number(flag20_state, "modal") != 0
                or number(flag20_state, "selectedShip") != carrier_index_at20
                or number(flag20_state, "slot0") != 0
                or number(flag20_state, "slot1") != site_index_at20
                or number(flag20_input, "siteIndex") != number(flag20_after, "slot1")
                or number(flag20_input, "siteSeed") != site_seed):
            raise ValueError("site click did not arm raw flag-20 slot 1 for the selected carrier")
        if (number(flag20_after, "lineFlag") != 20
                or number(flag20_after, "screen") != 0
                or number(flag20_after, "mapMode") != 0
                or number(flag20_after, "geoUi") != 1
                or number(flag20_after, "selectedShip") != check_carrier(flag20_after_carrier, carrier_id)
                or number(flag20_after, "slot0") != 0
                or number(flag20_after, "slot1") != check_site(flag20_after_site, site_seed)
                or number(flag20_after, "modal") != 0
                or number(flag20_after, "paused") != 1):
            raise ValueError("flag-20 site identity changed during its saved frame")

        order_i, order = one_record(lines, "pocketufo: normal-ground order ")
        order_site_i, order_site = stage_record(
            lines, "pocketufo: normal-ground site ", "ordered"
        )
        order_carrier_i, order_carrier = stage_record(
            lines, "pocketufo: normal-ground carrier ", "ordered"
        )
        ordered_state_i, ordered_state = stage_record(
            lines, "pocketufo: normal-ground state ", "ordered_after_snapshot"
        )
        ordered_site_i, ordered_site = stage_record(
            lines, "pocketufo: normal-ground site ", "ordered_after_snapshot"
        )
        ordered_carrier_i, ordered_carrier = stage_record(
            lines, "pocketufo: normal-ground carrier ", "ordered_after_snapshot"
        )
        speed_i, speed = one_record(lines, "pocketufo: normal-ground input control=speed ")
        flight_i, flight = one_record(lines, "pocketufo: normal-ground flight-start ")
        flag23_state_i, flag23_state = stage_record(
            lines, "pocketufo: normal-ground state ", "flag23"
        )
        flag23_site_i, flag23_site = stage_record(
            lines, "pocketufo: normal-ground site ", "flag23"
        )
        flag23_carrier_i, flag23_carrier = stage_record(
            lines, "pocketufo: normal-ground carrier ", "flag23"
        )
        flag23_after_i, flag23_after = stage_record(
            lines, "pocketufo: normal-ground state ", "flag23_after_snapshot"
        )
        flag23_after_site_i, flag23_after_site = stage_record(
            lines, "pocketufo: normal-ground site ", "flag23_after_snapshot"
        )
        flag23_after_carrier_i, flag23_after_carrier = stage_record(
            lines, "pocketufo: normal-ground carrier ", "flag23_after_snapshot"
        )
        flag23_input_i, flag23_input = one_record(
            lines, "pocketufo: normal-ground input control=flag23_b "
        )
        if not (flag20_input_i < order_i < order_site_i < order_carrier_i < ordered_state_i < ordered_site_i
                < ordered_carrier_i < speed_i < flight_i < flag23_state_i < flag23_site_i
                < flag23_carrier_i < flag23_after_i < flag23_after_site_i
                < flag23_after_carrier_i < flag23_input_i):
            raise ValueError("flag-20 order, minute flight, and flag-23 control are out of order")
        site_index_before_dump = check_site(order_site, site_seed)
        carrier_index_before_order = check_carrier(flag20_after_carrier, carrier_id)
        carrier_index_after_order = check_carrier(order_carrier, carrier_id, ordered=True)
        site_index_ordered = check_site(ordered_site, site_seed)
        if (number(order, "siteSeed") != site_seed
                or number(order, "siteIndex") != site_index_before_dump
                or number(order, "carrierId") != carrier_id
                or number(order, "carrierIndexBefore") != carrier_index_before_order
                or number(order, "carrierIndexAfter") != carrier_index_after_order
                or number(order, "atBaseBefore") != 1
                or number(order, "atBaseAfter") != 0
                or number(order, "interceptBefore") != 0
                or number(order, "interceptAfter") != 1
                or number(order, "shipTargetAfter") != site_index_before_dump
                or number(order, "paused") != 0
                or number(order, "clock") != number(options_state, "clock")
                or number(order, "flag") != 0):
            raise ValueError("live flag-20 control B did not order the same transport to the site")
        check_carrier(ordered_carrier, carrier_id, ordered=True)
        if (number(ordered_carrier, "targetIndex") != site_index_ordered
                or number(ordered_state, "screen") != 0
                or number(ordered_state, "mapMode") != 0
                or number(ordered_state, "geoUi") != 1
                or number(ordered_state, "paused") != 0
                or number(ordered_state, "modal") != 0
                or number(ordered_state, "lineFlag") != 0
                or number(ordered_state, "lineCount") != 0
                or check_site(ordered_site, site_seed) != site_index_ordered):
            raise ValueError("post-order site or carrier state changed")
        check_point(speed)
        if (number(speed, "root") != 3 or number(speed, "tickSec") != 60
                or number(flight, "tickSec") != 60
                or number(flight, "clock") != number(order, "clock")
                or number(flight, "maxPulses") != 600):
            raise ValueError("flight did not use the live one-minute control with no clock jump")

        arrival_pulses = number(flag23_state, "pulses")
        site_index_arrival = check_site(flag23_site, site_seed)
        carrier_index_arrival = check_carrier(flag23_carrier, carrier_id, ordered=True)
        if (not (1 <= arrival_pulses <= 600)
                or number(flag23_state, "clock") != number(flight, "clock") + arrival_pulses * 61
                or number(flag23_state, "tickSec") != 60
                or number(flag23_state, "screen") != 0
                or number(flag23_state, "geoUi") != 1
                or number(flag23_state, "paused") != 1
                or number(flag23_state, "modal") != 0
                or number(flag23_state, "radarActive") != 0
                or number(flag23_state, "lineFlag") != 23
                or number(flag23_state, "lineCount") == 0
                or number(flag23_state, "slot0") != carrier_index_arrival
                or number(flag23_state, "slot1") != site_index_arrival):
            raise ValueError("minute flight did not reach the same-site paused flag-23 boundary")
        if (number(flag23_site, "visible") != 1
                or number(flag23_carrier, "targetIndex") != site_index_arrival):
            raise ValueError("flag-23 site or transport order changed")
        if (number(flag23_after, "lineFlag") != 23
                or number(flag23_after, "paused") != 1
                or number(flag23_after, "screen") != 0
                or number(flag23_after, "geoUi") != 1
                or number(flag23_after, "modal") != 0
                or number(flag23_after, "radarActive") != 0
                or number(flag23_after, "lineCount") == 0
                or number(flag23_after, "clock") != number(flag23_state, "clock")
                or number(flag23_after, "slot0") != check_carrier(flag23_after_carrier,
                                                                     carrier_id, ordered=True)
                or number(flag23_after, "slot1") != check_site(flag23_after_site, site_seed)
                or number(flag23_after_carrier, "targetIndex")
                    != number(flag23_after, "slot1")):
            raise ValueError("flag-23 saved frame changed the site, carrier slots, clock, or pause")
        check_point(flag23_input)

        inventory_i, tact_entry = stage_record(
            lines, "pocketufo: normal-ground tactical ", "inventory_entry"
        )
        entry_frame_i, tact_entry_after_frame = stage_record(
            lines, "pocketufo: normal-ground tactical ", "inventory_after_snapshot"
        )
        carrier_index_entry = number(tact_entry, "carrierCurrentIndex")
        if (number(tact_entry, "screen") not in (0, 1)
                or number(tact_entry, "tactLive") != 1
                or number(tact_entry, "tactFlag") == 0
                or number(tact_entry, "mapPresent") != 1
                or number(tact_entry, "queuePresent") != 1
                or number(tact_entry, "mapKey") == 0
                or number(tact_entry, "queueKey") == 0
                or number(tact_entry, "lineCount") != 0
                or number(tact_entry, "modal") != 0
                or number(tact_entry, "carrierExpectedId") != carrier_id
                or number(tact_entry, "carrierId") != carrier_id
                or number(tact_entry, "carrierType") != 0
                or number(tact_entry, "mapCraftIndex") != carrier_index_entry
                or number(tact_entry, "expectedSiteSeed") != site_seed
                or number(tact_entry, "siteSeed") != site_seed
                or number(tact_entry, "siteType") != 4
                or number(tact_entry, "siteActive") != 1
                or number(tact_entry, "expectedCrew") != 8
                or number(tact_entry, "missionMode") != 0):
            raise ValueError("flag-23 did not create the same transport/site tactical owners")
        if (number(tact_entry_after_frame, "mapKey") != number(tact_entry, "mapKey")
                or number(tact_entry_after_frame, "queueKey") != number(tact_entry, "queueKey")
                or number(tact_entry_after_frame, "siteSeed") != site_seed
                or number(tact_entry_after_frame, "carrierId") != carrier_id
                or number(tact_entry_after_frame, "mapCraftIndex")
                    != number(tact_entry_after_frame, "carrierCurrentIndex")
                or number(tact_entry_after_frame, "siteType") != 4
                or number(tact_entry_after_frame, "siteActive") != 1
                or number(tact_entry_after_frame, "modal") != 0
                or number(tact_entry_after_frame, "lineCount") != 0):
            raise ValueError("inventory frame changed tactical owners or left an unknown message")

        confirm_inputs = records(lines, "pocketufo: normal-ground input key=confirm ")
        live_deploy_inputs = records(lines, "pocketufo: normal-ground input deploy=already_live_player_screen")
        if len(confirm_inputs) + len(live_deploy_inputs) != 1:
            raise ValueError("ground flow did not use one real deploy input path")
        if number(tact_entry, "screen") == 1:
            if len(confirm_inputs) != 1 or len(live_deploy_inputs) != 0:
                raise ValueError("inventory entry did not use the real confirm key")
        elif (len(live_deploy_inputs) != 1 or len(confirm_inputs) != 0
              or number(tact_entry, "playerInput") != 1):
            raise ValueError("already-live player screen lacked input")

        if number(tact_entry, "screen") == 1:
            tact_after_key_i, tact_after_key = stage_record(
                lines, "pocketufo: normal-ground tactical ", "after_deploy_key"
            )
            tact_after_paint_i, tact_after_paint = stage_record(
                lines, "pocketufo: normal-ground tactical ", "after_deploy_paint"
            )
            if not (entry_frame_i < tact_after_key_i < tact_after_paint_i):
                raise ValueError("inventory confirm input and paint census are out of order")
            for phase in (tact_after_key, tact_after_paint):
                if (number(phase, "mapKey") != number(tact_entry, "mapKey")
                        or number(phase, "queueKey") != number(tact_entry, "queueKey")
                        or number(phase, "siteSeed") != site_seed
                        or number(phase, "carrierId") != carrier_id
                        or number(phase, "mapCraftIndex") != number(phase, "carrierCurrentIndex")
                        or number(phase, "siteType") != 4
                        or number(phase, "siteActive") != 1
                        or number(phase, "modal") != 0
                        or number(phase, "lineCount") != 0):
                    raise ValueError("confirm changed the actual tactical owner or left an unknown message")

        deployed_i, deployed = stage_record(
            lines, "pocketufo: normal-ground tactical ", "deployed"
        )
        deployed_units = [fields for _, fields in records(
            lines, "pocketufo: normal-ground unit-check stage=deployed "
        )]
        assigned = [fields for _, fields in records(
            lines, "pocketufo: normal-ground assigned-crew "
        )]
        assigned_indices = [number(x, "rosterIndex") for x in assigned]
        assigned_keys = {number(x, "rosterIndex"): number(x, "soldierKey") for x in assigned}
        if (len(assigned) != 8 or len(set(assigned_indices)) != 8
                or len(assigned_keys) != 8 or len(set(assigned_keys.values())) != 8
                or any(index < 0 for index in assigned_indices)
                or 0 in assigned_keys.values()
                or any(number(x, "carrierIndex") != carrier_index
                       or number(x, "carrierId") != carrier_id
                       or number(x, "assignedCraft") != carrier_index
                       for x in assigned)):
            raise ValueError("Options did not show eight distinct live roster owners assigned to the transport")
        if len(deployed_units) != 8:
            raise ValueError("deployed tactical map did not report eight crew units")
        carrier_index_deployed = number(deployed, "carrierCurrentIndex")
        deployed_indices = [number(x, "expectedRosterIndex") for x in deployed_units]
        deployed_keys = [number(x, "unitKey") for x in deployed_units]
        if len(set(deployed_indices)) != 8 or len(set(deployed_keys)) != 8:
            raise ValueError("deployed map does not contain eight distinct roster pointers")
        for i, unit in enumerate(deployed_units):
            source_index = number(unit, "expectedRosterIndex")
            if (number(unit, "index") != i or number(unit, "unitPresent") != 1
                    or number(unit, "expected") != 1
                    or source_index not in assigned_keys
                    or number(unit, "unitKey") != assigned_keys[source_index]
                    or number(unit, "sourceMatch") != 1
                    or number(unit, "assignedCraft") != carrier_index_deployed
                    or number(unit, "craftMatch") != 1
                    or number(unit, "cellXYBounds") != 1
                    or number(unit, "cellBackingZ") != 1
                    or number(unit, "cellOwner") != 1
                    or number(unit, "cellOccupant") != 1
                    or number(unit, "cellValid") != 1
                    or number(unit, "cellUnique") != 1
                    or number(unit, "valid") != 1):
                raise ValueError(f"deployed crew unit {i} does not match its live assigned roster owner and valid cell")
        if (number(deployed, "screen") != 0 or number(deployed, "tactLive") != 1
                or number(deployed, "tactFlag") == 0
                or number(deployed, "mapPresent") != 1 or number(deployed, "queuePresent") != 1
                or number(deployed, "mapKey") != number(tact_entry, "mapKey")
                or number(deployed, "queueKey") != number(tact_entry, "queueKey")
                or number(deployed, "carrierId") != carrier_id
                or number(deployed, "carrierType") != 0
                or number(deployed, "mapCraftIndex") != carrier_index_deployed
                or number(deployed, "backingLayers") != 4
                or number(deployed, "siteSeed") != site_seed
                or number(deployed, "siteType") != 4
                or number(deployed, "siteActive") != 1
                or number(deployed, "xcomCount") != 8
                or number(deployed, "enemyCount") <= 0
                or number(deployed, "expectedCrew") != 8
                or number(deployed, "queueLen") != 0
                or number(deployed, "queueCursor") != 0
                or number(deployed, "queueActive") != 0
                or number(deployed, "busy") != 0
                or number(deployed, "playerInput") != 1
                or number(deployed, "enemyTurn") != 0
                or number(deployed, "battleEnded") != 0
                or number(deployed, "lineCount") != 0
                or number(deployed, "modal") != 0):
            raise ValueError("deployed mission lacks eight crew, stable owners, idle queue, or player input")

        if not (deployed_i < ground_pass_i < ground_step_i):
            raise ValueError("crew-deployed pass and command marker are out of order")
        if (ground_pass.get("stage") != "crew_deployed"
                or number(ground_pass, "pulses") != arrival_pulses
                or number(ground_pass, "arrivalPulse") != arrival_pulses
                or number(ground_pass, "clockStart") != number(flight, "clock")
                or number(ground_pass, "clockEnd") != number(flag23_state, "clock")
                or number(ground_pass, "siteSeed") != site_seed
                or number(ground_pass, "carrierId") != carrier_id
                or number(ground_pass, "carrierIndex") != carrier_index_deployed
                or number(ground_pass, "expectedCrew") != 8
                or number(ground_pass, "xcomCount") != 8
                or number(ground_pass, "enemyCount") <= 0
                or number(ground_pass, "playerInput") != 1
                or number(ground_pass, "queueLen") != 0
                or number(ground_pass, "queueCursor") != 0):
            raise ValueError("PASS does not prove a live eight-crew deployment")

        snapshot_lines = records(lines, "pocketufo: normal-ground snapshot ")
        snapshot_stages = {fields.get("stage") for _, fields in snapshot_lines}
        if set(SNAPSHOTS) - snapshot_stages:
            raise ValueError("one or more required real tactical frames are missing")
        check_frames(log_path)
    except (OSError, ValueError, AssertionError) as exc:
        print(f"FAIL: {exc}")
        return 1

    print("PASS: same landed site reached live tactical input with eight assigned crew in distinct valid cells")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
