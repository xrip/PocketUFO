#!/usr/bin/env python3
"""Check captured output from the natural New Game intercept-order flow."""

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
    index, line = one_line(lines, f"pocketufo: normal-order {name} ")
    prefix = f"pocketufo: normal-order {name} "
    return index, dict(FIELD.findall(line[len(prefix) :]))


def number(values: dict[str, str], key: str) -> int:
    try:
        return int(values[key], 0)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"missing or invalid {key}={values.get(key)!r}") from exc


def full_pair(values: dict[str, str], key: str) -> None:
    value = values.get(key, "")
    if "/" not in value:
        raise ValueError(f"missing {key}=current/max")
    current, maximum = (int(part, 0) for part in value.split("/", 1))
    if current != maximum:
        raise ValueError(f"craft is not ready: {key}={value}")


def pair(values: dict[str, str], key: str) -> tuple[int, int]:
    try:
        left, right = values[key].split("/", 1)
        return int(left, 0), int(right, 0)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"missing or invalid {key}=x/y") from exc


def check_map_click(values: dict[str, str]) -> None:
    if number(values, "mode") != 0:
        raise ValueError("map cell click did not use zoom mode 0")
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


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_normal_intercept.py <captured-output>", file=sys.stderr)
        return 2

    try:
        lines = Path(sys.argv[1]).read_bytes().decode("utf-8", errors="replace").splitlines()
        if any(line.startswith("pocketufo: normal-order FAIL ") for line in lines):
            raise ValueError("the helper stopped before a real order")
        if any(line.startswith("pocketufo: step newgame") or line.startswith("pocketufo: step close")
               for line in lines):
            raise ValueError("flow used a direct new-game or close command")

        before_i, before = one_line(lines, "pocketufo: state before_menu_click:")
        newgame_i, newgame_line = one_line(lines, "pocketufo: new game:")
        click_i, _ = one_line(lines, "pocketufo: step click 120 124")
        pick_i, _ = one_line(lines, "pocketufo: step pick")
        after_i, after = one_line(lines, "pocketufo: state after_new_game_pick:")
        if not re.search(r"start screen 17,", before):
            raise ValueError("the real menu click did not start from menu screen 17")
        match = re.search(r"\d+ funds, (\d+) bases, (\d+) soldiers, (\d+) craft,", newgame_line)
        if match is None or int(match.group(1)) < 1 or int(match.group(2)) < 1 or int(match.group(3)) < 1:
            raise ValueError("the menu callback did not report a populated New Game")
        base_count = re.search(r"bases (\d+),", after)
        if base_count is None or int(base_count.group(1)) < 1:
            raise ValueError("the live pick input did not leave a base in the world")

        speed_i, speed = record(lines, "speed")
        contact_i, contact = record(lines, "contact")
        cleared_i, cleared = record(lines, "cleared")
        screen18_i, screen18 = record(lines, "screen18")
        options_anim_i, options_anim = record(lines, "options-animation")
        base_i, base = record(lines, "base")
        row_i, row = record(lines, "row")
        selected_i, selected = record(lines, "selected")
        target_i, target = record(lines, "target")
        launch_i, launch = record(lines, "launch")
        pass_i, passed = record(lines, "PASS")
        map_clicks = [(i, dict(FIELD.findall(line.split("map-click ", 1)[1])))
                      for i, line in enumerate(lines) if "normal-order map-click " in line]
        if len(map_clicks) != 2:
            raise ValueError(f"expected real base and UFO map clicks, found {len(map_clicks)}")

        if not (before_i < newgame_i < click_i < pick_i < after_i < speed_i < contact_i
                < cleared_i < map_clicks[0][0] < screen18_i < options_anim_i < base_i
                < row_i < selected_i < map_clicks[1][0]
                < target_i < launch_i < pass_i):
            raise ValueError("normal-order evidence is missing or out of order")
        if number(speed, "root") != 7 or number(speed, "tickSec") != 3600:
            raise ValueError("the one-hour speed control was not clicked through its live control")
        clock_start = number(speed, "clockStart")
        if contact.get("source") != "c353":
            raise ValueError("contact did not come from the regular C353 route")
        pulses = number(contact, "pulses")
        seconds = number(contact, "seconds")
        target_index = number(contact, "target")
        target_seed = number(contact, "seed")
        if not (1 <= pulses <= 120) or number(contact, "tickSec") != 3600:
            raise ValueError("natural contact was not reached within the one-hour pulse bound")
        if number(contact, "clockStart") != clock_start or seconds != clock_start + pulses * 3601:
            raise ValueError("clock does not match the logged one-hour C353 pulses")
        if (number(contact, "lineFlag") != 10 or number(contact, "paused") != 1
                or number(contact, "targetType") != 1 or number(contact, "targetActive") != 1
                or number(contact, "targetVisible") != 1 or target_index < 0):
            raise ValueError("natural contact did not pause on flag 10 with a target")
        if (number(cleared, "flag") != 0 or number(cleared, "paused") != 0
                or number(cleared, "selection") != 0):
            raise ValueError("the live contact-clear click did not resume the map")
        if number(map_clicks[0][1], "index") < 0 or number(base, "index") != number(map_clicks[0][1], "index"):
            raise ValueError("the base row was not opened by the recorded visible map click")
        check_map_click(map_clicks[0][1])
        if (number(screen18, "screen") != 18 or number(screen18, "options") != 1
                or number(screen18, "namePanel") != 1 or number(screen18, "storesPanel") != 1):
            raise ValueError("the base click did not open the live screen-18 craft list")
        if (number(options_anim, "frames") > 101 or number(options_anim, "progress") < 100
                or number(options_anim, "started") != 1 or number(options_anim, "paused") != 1
                or number(options_anim, "clockBefore") != seconds
                or number(options_anim, "clockAfter") != seconds):
            raise ValueError("screen-18 animation did not finish while paused without a clock advance")

        craft_index = number(row, "craft")
        if number(row, "rowReadyAux") != 0 or number(row, "crew") <= 0 or number(row, "atBase") != 1:
            raise ValueError("selected row is not a ready craft with crew at the base")
        full_pair(row, "range")
        full_pair(row, "fuel")
        if number(selected, "after") != craft_index or number(selected, "craft") != craft_index:
            raise ValueError("the real row click did not select the logged craft")
        if number(selected, "flag27") != 27 or number(selected, "slot0") != craft_index:
            raise ValueError("the craft row did not arm flag 27 and its craft index")
        if number(selected, "selectionBefore") != 0 or number(selected, "selection") != 1:
            raise ValueError("craft selection was not newly armed by the real row click")

        if number(map_clicks[1][1], "index") != target_index:
            raise ValueError("the second map click did not target the natural contact item")
        check_map_click(map_clicks[1][1])
        target_click = map_clicks[1][1]
        if (number(target_click, "pausedBefore") != 1
                or number(target_click, "flagBefore") != 27
                or number(target_click, "selectionBefore") != 1):
            raise ValueError("the real UFO click did not start from the paused flag-27 selection")
        if (number(target_click, "pausedAfter") != 1
                or number(target_click, "flagAfter") != 10
                or number(target_click, "selectionAfter") != 1):
            raise ValueError("the real UFO click did not reach flag 10 while preserving the selection")
        if number(target, "index") != target_index or number(target, "seed") != target_seed:
            raise ValueError("the clicked target differs from the natural contact")
        if number(target, "lineFlag") != 10 or number(target, "slot0") != target_index:
            raise ValueError("the visible UFO click did not restore flag 10 for the same target")
        if number(target, "selection") != 1:
            raise ValueError("the UFO click did not preserve the selected-craft flag")

        confirm_x, confirm_y = (int(part, 0) for part in launch.get("confirmCenter", "").split("/", 1))
        if launch.get("confirm") != "geo1" or not (0 <= confirm_x < 240 and 0 <= confirm_y < 320):
            raise ValueError("the live geo[1] confirm control was not clicked")
        if number(launch, "craft") != craft_index or number(launch, "selected") != craft_index:
            raise ValueError("the confirm did not use the selected craft")
        if number(launch, "target") != target_index or number(launch, "seed") != target_seed:
            raise ValueError("the order target differs from the clicked contact")
        if number(launch, "lineFlagBefore") != 10 or number(launch, "clock") != seconds:
            raise ValueError("the order did not begin from the paused flag-10 contact state")
        if (number(launch, "atBaseBefore") != 1 or number(launch, "atBaseAfter") != 0
                or number(launch, "interceptBefore") != 0 or number(launch, "interceptAfter") != 1
                or number(launch, "shipTargetAfter") != target_index or number(launch, "targetStill") != 1):
            raise ValueError("the confirm click did not change the craft into an order for this contact")
        if passed.get("stage") != "real_order":
            raise ValueError("no real-order pass record")
        if number(passed, "pulses") != pulses or number(passed, "clock") != seconds:
            raise ValueError("pass summary disagrees with contact and clock evidence")
    except (OSError, ValueError, AssertionError) as exc:
        print(f"FAIL: {exc}")
        return 1

    print("PASS: live menu, bounded C353 contact, ready crew craft, flag 27, UFO click, and geo[1] order verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
