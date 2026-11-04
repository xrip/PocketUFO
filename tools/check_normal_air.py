#!/usr/bin/env python3
"""Check captured output from the natural New Game fighter-arrival flow."""

from __future__ import annotations

import re
import sys
from pathlib import Path


FIELD = re.compile(r"([A-Za-z][A-Za-z0-9]*)=([^\s]+)")


def one_line(lines: list[str], prefix: str) -> tuple[int, str]:
    found = [(index, line) for index, line in enumerate(lines) if line.startswith(prefix)]
    if len(found) != 1:
        raise ValueError(f"expected one {prefix!r} line, found {len(found)}")
    return found[0]


def record(lines: list[str], name: str) -> tuple[int, dict[str, str]]:
    prefix = f"pocketufo: normal-air {name} "
    index, line = one_line(lines, prefix)
    return index, dict(FIELD.findall(line[len(prefix) :]))


def record_with_fields(lines: list[str], name: str,
                       required_fields: set[str]) -> tuple[int, dict[str, str]]:
    prefix = f"pocketufo: normal-air {name} "
    found = []
    for index, line in enumerate(lines):
        if not line.startswith(prefix):
            continue
        values = dict(FIELD.findall(line[len(prefix) :]))
        if required_fields <= values.keys():
            found.append((index, values))
    if len(found) != 1:
        raise ValueError(f"expected one {prefix!r} record with {sorted(required_fields)}, found {len(found)}")
    return found[0]


def number(values: dict[str, str], key: str) -> int:
    try:
        return int(values[key], 0)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"missing or invalid {key}={values.get(key)!r}") from exc


def pair(values: dict[str, str], key: str) -> tuple[int, int]:
    try:
        left, right = values[key].split("/", 1)
        return int(left, 0), int(right, 0)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"missing or invalid {key}=current/max") from exc


def full_pair(values: dict[str, str], key: str) -> None:
    current, maximum = pair(values, key)
    if current != maximum:
        raise ValueError(f"craft is not ready: {key}={current}/{maximum}")


def check_map_click(values: dict[str, str]) -> None:
    if number(values, "mode") != 0:
        raise ValueError("map click did not use zoom mode 0")
    scroll_x, scroll_y = pair(values, "scroll")
    map_w, map_h = pair(values, "map")
    screen_x, screen_y = pair(values, "point")
    item_x, item_y = pair(values, "item")
    if map_w <= 0 or map_h <= 0:
        raise ValueError("map view has invalid dimensions")
    base_x = item_x - scroll_x - map_w // 2 + 120
    base_y = item_y - scroll_y - map_h // 2 + 160
    if screen_x not in (base_x, base_x + map_w, base_x - map_w) or screen_y != base_y:
        raise ValueError("map click does not invert the live mode-0 cell transform")
    if not (0 <= screen_x < 240 and 0 <= screen_y < 320):
        raise ValueError("map click point is outside the logical display")


def ammo_loaded(weapon: int, ammo: int) -> bool:
    if weapon == 28:
        return ammo == -1
    return weapon in (25, 26, 27, 29, 30) and ammo > 0


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_normal_air.py <captured-output>", file=sys.stderr)
        return 2

    try:
        lines = Path(sys.argv[1]).read_bytes().decode("utf-8", errors="replace").splitlines()
        if any(line.startswith("pocketufo: normal-air FAIL ") for line in lines):
            raise ValueError("the helper stopped before the radar boundary")
        if any(line.startswith("pocketufo: step newgame") or line.startswith("pocketufo: step close")
               for line in lines):
            raise ValueError("flow used a direct new-game or close command")

        before_i, before_line = one_line(lines, "pocketufo: state before_menu_click:")
        newgame_i, newgame_line = one_line(lines, "pocketufo: new game:")
        click_i, _ = one_line(lines, "pocketufo: step click 120 124")
        pick_i, _ = one_line(lines, "pocketufo: step pick")
        after_i, after_line = one_line(lines, "pocketufo: state after_new_game_pick:")
        if not re.search(r"start screen 17,", before_line):
            raise ValueError("the real menu click did not start from menu screen 17")
        match = re.search(r"\d+ funds, (\d+) bases, (\d+) soldiers, (\d+) craft,", newgame_line)
        if match is None or int(match.group(1)) < 1 or int(match.group(2)) < 1 or int(match.group(3)) < 1:
            raise ValueError("the menu callback did not report a populated New Game")
        if not re.search(r"bases [1-9]\d*,", after_line):
            raise ValueError("live pick input did not leave a base in the world")

        speed_i, speed = record(lines, "speed")
        contact_i, contact = record(lines, "contact")
        clear_i, cleared = record(lines, "cleared")
        base_i, base = record(lines, "base")
        screen18_i, screen18 = record(lines, "screen18")
        anim_i, anim = record(lines, "options-animation")
        census_i, census = record(lines, "row-census")
        selected_row_i, selected_row = record(lines, "selected-row")
        selected_i, selected = record(lines, "selected")
        target_i, target = record_with_fields(
            lines, "target", {"index", "seed", "lineFlag", "slot0", "selection"}
        )
        launch_i, launch = record(lines, "launch")
        minute_i, minute = record(lines, "minute-speed")
        flight_prefix = "pocketufo: normal-air state stage=flight_start "
        flight_i, flight_line = one_line(lines, flight_prefix)
        flight = dict(FIELD.findall(flight_line[len(flight_prefix) :]))
        boundary_i, boundary = record(lines, "boundary")
        pass_i, passed = record(lines, "PASS")
        row_lines = [(i, dict(FIELD.findall(line[len("pocketufo: normal-air row ") :])))
                     for i, line in enumerate(lines)
                     if line.startswith("pocketufo: normal-air row row=")]
        map_clicks = [(i, dict(FIELD.findall(line.split("map-click ", 1)[1])))
                      for i, line in enumerate(lines)
                      if line.startswith("pocketufo: normal-order map-click ")
                      and clear_i < i < pass_i]
        if len(row_lines) < 1:
            raise ValueError("Options row census is missing")
        if len(map_clicks) != 2:
            raise ValueError(f"expected live base and UFO map clicks, found {len(map_clicks)}")

        if not (before_i < newgame_i < click_i < pick_i < after_i < speed_i < contact_i
                < clear_i < map_clicks[0][0] < base_i < screen18_i < anim_i < census_i
                < row_lines[0][0] < selected_row_i < selected_i < map_clicks[1][0]
                < target_i < launch_i < minute_i < flight_i < boundary_i < pass_i):
            raise ValueError("AIR flow evidence is missing or out of order")
        if number(speed, "root") != 4 or number(speed, "tickSec") != 300:
            raise ValueError("natural contact did not use the live five-minute control")
        contact_clock_start = number(speed, "clockStart")
        contact_pulses = number(contact, "pulses")
        contact_seconds = number(contact, "seconds")
        target_index = number(contact, "target")
        target_seed = number(contact, "seed")
        if contact.get("source") != "c353" or not (1 <= contact_pulses <= 2016):
            raise ValueError("contact did not come from bounded C353 pulses")
        if (number(contact, "tickSec") != 300
                or number(contact, "clockStart") != contact_clock_start
                or contact_seconds != contact_clock_start + contact_pulses * 301):
            raise ValueError("natural contact clock does not match the live five-minute pulses")
        if (number(contact, "lineFlag") != 10 or number(contact, "paused") != 1
                or number(contact, "targetType") != 1 or number(contact, "targetActive") != 1
                or number(contact, "targetVisible") != 1 or target_index < 0):
            raise ValueError("natural contact did not pause on visible active flag 10")
        for key in ("targetTimer", "targetPhase", "targetSub", "targetTravel"):
            number(contact, key)
        if (number(cleared, "flag") != 0 or number(cleared, "paused") != 0
                or number(cleared, "selection") != 0):
            raise ValueError("live geo[2] click did not clear and resume contact")
        if number(map_clicks[0][1], "index") != number(base, "index"):
            raise ValueError("base screen did not follow the logged real base click")
        check_map_click(map_clicks[0][1])
        if (number(screen18, "screen") != 18 or number(screen18, "options") != 1
                or number(screen18, "namePanel") != 1 or number(screen18, "storesPanel") != 1):
            raise ValueError("real base click did not open the craft Options screen")
        if (number(anim, "frames") > 101 or number(anim, "progress") < 100
                or number(anim, "started") != 1 or number(anim, "paused") != 1
                or number(anim, "clock") != contact_seconds):
            raise ValueError("Options animation did not finish while paused without clock time")

        chosen_craft = number(selected_row, "craft")
        chosen_type = number(selected_row, "type")
        if (number(selected_row, "rowReadyAux") != 0 or chosen_type != 3
                or number(selected_row, "atBase") != 1 or number(selected_row, "loadedWeapon") != 1):
            raise ValueError("selected row is not a ready interceptor with a loaded weapon")
        full_pair(selected_row, "range")
        full_pair(selected_row, "fuel")
        if "canDogfight" not in selected_row:
            raise ValueError("selected row did not log the live canDogfight field")
        left_loaded = ammo_loaded(number(selected_row, "weaponL"), number(selected_row, "ammoL"))
        right_loaded = ammo_loaded(number(selected_row, "weaponR"), number(selected_row, "ammoR"))
        if not (left_loaded or right_loaded):
            raise ValueError("selected interceptor row has no mounted weapon with ammo")
        if not any(number(row, "row") == number(selected_row, "row")
                   and number(row, "craft") == chosen_craft
                   and number(row, "readyAux") == 0
                   and number(row, "type") == 3
                   and number(row, "visible") == 1
                   for _, row in row_lines):
            raise ValueError("chosen craft is not present in the visible Options row census")
        if number(selected, "craft") != chosen_craft or number(selected, "type") != chosen_type:
            raise ValueError("live Options row click did not select the logged interceptor")
        if (number(selected, "flag27") != 27 or number(selected, "slot0") != chosen_craft
                or number(selected, "selection") != 1):
            raise ValueError("live Options row click did not arm flag 27")

        if number(map_clicks[1][1], "index") != target_index:
            raise ValueError("second live map click did not target the natural contact")
        check_map_click(map_clicks[1][1])
        if (number(map_clicks[1][1], "pausedBefore") != 1
                or number(map_clicks[1][1], "flagBefore") != 27
                or number(map_clicks[1][1], "selectionBefore") != 1
                or number(map_clicks[1][1], "pausedAfter") != 1
                or number(map_clicks[1][1], "flagAfter") != 10
                or number(map_clicks[1][1], "selectionAfter") != 1):
            raise ValueError("real UFO click did not preserve the paused flag-27 selection")
        if (number(target, "index") != target_index or number(target, "seed") != target_seed
                or number(target, "lineFlag") != 10 or number(target, "slot0") != target_index
                or number(target, "selection") != 1):
            raise ValueError("visible UFO click did not arm the same natural target")
        if (launch.get("confirm") != "geo1" or number(launch, "craft") != chosen_craft
                or number(launch, "craftId") != number(selected_row, "craftId")
                or number(launch, "type") != chosen_type
                or number(launch, "target") != target_index
                or number(launch, "seed") != target_seed
                or number(launch, "atBaseBefore") != 1 or number(launch, "atBaseAfter") != 0
                or number(launch, "interceptBefore") != 0 or number(launch, "interceptAfter") != 1
                or number(launch, "shipTargetAfter") != target_index
                or number(launch, "targetSeedAfter") != target_seed
                or number(launch, "pause") != 0 or number(launch, "lineFlag") != 0):
            raise ValueError("geo[1] did not order the selected interceptor to the same target")

        flight_clock_start = number(launch, "clock")
        if (number(minute, "root") != 3 or number(minute, "tickSec") != 60
                or number(minute, "clockStart") != flight_clock_start):
            raise ValueError("one-minute speed did not come from the live root-3 control")
        if (number(flight, "pulses") != 0 or number(flight, "tickSec") != 60
                or number(flight, "paused") != 0 or number(flight, "radarActive") != 0):
            raise ValueError("flight did not begin on the running one-minute geoscape")

        arrival_pulses = number(boundary, "pulses")
        if not (1 <= arrival_pulses <= 600):
            raise ValueError("radar activation did not occur within 600 minute pulses")
        if (boundary.get("kind") != "radar_active" or number(boundary, "radarActive") != 1
                or number(boundary, "tickSec") != 60 or number(boundary, "paused") != 1
                or number(boundary, "target") != target_index
                or number(boundary, "seedExpected") != target_seed
                or number(boundary, "seed") != target_seed
                or number(boundary, "targetType") != 1
                or number(boundary, "targetActive") != 1
                or number(boundary, "targetVisible") not in (0, 1)
                or number(boundary, "targetTimer") > 0
                or number(boundary, "craft") != chosen_craft
                or number(boundary, "craftId") != number(selected_row, "craftId")
                or number(boundary, "craftType") != chosen_type
                or number(boundary, "craftTarget") != target_index
                or number(boundary, "craftAtBase") != 0
                or number(boundary, "craftIntercept") != 1
                or number(boundary, "craftFuel") <= 0 or number(boundary, "craftRange") <= 0
                or number(boundary, "lineFlag") != 0 or number(boundary, "modal") != 0):
            raise ValueError("radar boundary is not the same flying target with nTimer <= 0")
        if (number(boundary, "clockStart") != flight_clock_start
                or number(boundary, "seconds") != flight_clock_start + arrival_pulses * 61):
            raise ValueError("arrival clock does not match one C353 minute pulse per step")
        for line in lines:
            if line.startswith("pocketufo: normal-air visibility "):
                prefix = "pocketufo: normal-air visibility "
                visibility = dict(FIELD.findall(line[len(prefix) :]))
                if (number(visibility, "target") != target_index
                        or number(visibility, "seed") != target_seed
                        or number(visibility, "previous") not in (0, 1)
                        or number(visibility, "visible") not in (0, 1)
                        or number(visibility, "previous") == number(visibility, "visible")):
                    raise ValueError("visibility change did not belong to the same target")
        for key in ("targetPhase", "targetSub", "targetTravel", "ownIndex", "ownX", "ownY",
                    "craftFuel", "craftRange"):
            number(boundary, key)
        if (passed.get("stage") != "radar_boundary" or number(passed, "pulses") != arrival_pulses
                or number(passed, "clock") != number(boundary, "seconds")
                or number(passed, "target") != target_index
                or number(passed, "seed") != target_seed
                or number(passed, "craft") != chosen_craft
                or number(passed, "craftId") != number(selected_row, "craftId")):
            raise ValueError("PASS record does not match the real radar boundary")
        for stage in ("real_order", "flight_start", "radar_boundary"):
            state_i, _ = one_line(lines, f"pocketufo: normal-air state stage={stage} ")
            craft_i, _ = one_line(lines, f"pocketufo: normal-air craft stage={stage} ")
            stop_i = next(
                (i for i in range(state_i + 1, len(lines))
                 if lines[i].startswith("pocketufo: normal-air state stage=")),
                len(lines),
            )
            target_rows = [
                i for i in range(state_i + 1, stop_i)
                if lines[i].startswith("pocketufo: normal-air target expected=")
            ]
            if len(target_rows) != 1 or not state_i < craft_i < target_rows[0]:
                raise ValueError(f"expected one ordered target snapshot for stage {stage}")
            target_snapshot = dict(FIELD.findall(lines[target_rows[0]]))
            if (number(target_snapshot, "expected") != target_index
                    or number(target_snapshot, "actualSeed") != target_seed):
                raise ValueError(f"target snapshot identity changed at stage {stage}")
        one_line(lines, "pocketufo: normal-air own-marker stage=flight_start ")
        one_line(lines, "pocketufo: normal-air own-marker stage=radar_boundary ")
    except (OSError, ValueError, AssertionError) as exc:
        print(f"FAIL: {exc}")
        return 1

    print("PASS: live interceptor row and real order reached the same-target nTimer<=0 radar boundary")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
