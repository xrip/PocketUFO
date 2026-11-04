#!/usr/bin/env python3
"""Check the saved natural radar combat run and its captured frames."""

from __future__ import annotations

import re
import sys
from pathlib import Path


FIELD = re.compile(r"([A-Za-z][A-Za-z0-9]*)=([^\s]+)")
OUTCOMES = {
    "landed_site": (1, 1601),
    "crashed_site": (2, 1602),
    "destroyed_target": (3, 1600),
    "sea_removal": (4, 1602),
}
SNAPSHOTS = {
    "before": "radar_combat_before.ppm",
    "firing": "radar_combat_firing.ppm",
    "damage": "radar_combat_damage.ppm",
    "terminal": "radar_combat_terminal.ppm",
    "after_cleanup": "radar_combat_after_cleanup.ppm",
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
    found = [
        (i, fields)
        for i, fields in records(lines, prefix)
        if fields.get("stage") == stage
    ]
    if len(found) != 1:
        raise ValueError(f"expected one {stage!r} record under {prefix!r}, found {len(found)}")
    return found[0]


def number(fields: dict[str, str], key: str) -> int:
    try:
        return int(fields[key], 0)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"missing or invalid {key}={fields.get(key)!r}") from exc


def number_pair(fields: dict[str, str], key: str) -> tuple[int, int]:
    try:
        first, second = fields[key].split("/")
        return int(first, 0), int(second, 0)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"missing or invalid integer pair {key}={fields.get(key)!r}") from exc


def check_identity(fields: dict[str, str], target: int, seed: int, craft_id: int,
                   craft_type: int, live: bool) -> None:
    if (number(fields, "targetExpected") != target
            or number(fields, "seedExpected") != seed
            or (live and number(fields, "seed") != seed)):
        raise ValueError("combat observer lost the natural target seed")
    if live:
        if (number(fields, "targetCurrent") != target
                or number(fields, "targetPresent") != 1
                or number(fields, "type") != 1
                or number(fields, "active") != 1):
            raise ValueError("live combat observer lost the active flying target")
    elif number(fields, "targetPresent") == 0:
        if number(fields, "targetCurrent") != -1 or number(fields, "seed") != -1:
            raise ValueError("removed target still appears in the cleanup observer")
    elif (number(fields, "targetCurrent") != target or number(fields, "seed") != seed):
        raise ValueError("cleanup observer changed the surviving target identity")
    if (number(fields, "craftExpectedId") != craft_id
            or number(fields, "craftId") != craft_id
            or number(fields, "craftType") != craft_type):
        raise ValueError("combat observer changed the selected craft identity")
    if live and (number(fields, "craftCurrent") < 0
                 or number(fields, "atBase") != 0
                 or number(fields, "intercept") != 1
                 or number(fields, "shipTarget") != target):
        raise ValueError("live combat observer lost the fighter order")


def check_frames(log_path: Path) -> None:
    app_dir = log_path.parent / "app"
    for stage, filename in SNAPSHOTS.items():
        path = app_dir / filename
        if not path.is_file() or path.stat().st_size <= 32:
            raise ValueError(f"missing or empty saved frame for {stage}: {path}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_normal_air_combat.py <captured-output>", file=sys.stderr)
        return 2

    log_path = Path(sys.argv[1])
    try:
        lines = log_path.read_bytes().decode("utf-8", errors="replace").splitlines()
        if any(line.startswith("pocketufo: normal-air-combat FAIL ") for line in lines):
            raise ValueError("the combat helper stopped before a verified target terminal")
        if sum(line.startswith("pocketufo: step normalaircombat") for line in lines) != 1:
            raise ValueError("normalaircombat command record is missing or repeated")

        start_i, start = one_record(lines, "pocketufo: normal-air-combat start ")
        start_state_i, start_state = stage_record(
            lines, "pocketufo: normal-air-combat state ", "start"
        )
        before_state_i, before_state = stage_record(
            lines, "pocketufo: normal-air-combat state ", "before"
        )
        before_radar_i, before_radar = stage_record(
            lines, "pocketufo: normal-air-combat radar ", "before"
        )
        start_ident_i, start_ident = stage_record(
            lines, "pocketufo: normal-air-combat identity ", "start"
        )
        before_ident_i, before_ident = stage_record(
            lines, "pocketufo: normal-air-combat identity ", "before"
        )
        input_i, live_input = one_record(lines, "pocketufo: normal-air-combat input control=attack_max ")
        armed_i, armed = stage_record(
            lines, "pocketufo: normal-air-combat radar ", "attack_armed"
        )
        armed_ident_i, armed_ident = stage_record(
            lines, "pocketufo: normal-air-combat identity ", "attack_armed"
        )

        target = number(start, "target")
        seed = number(start, "seed")
        craft_id = number(start, "shipId")
        craft_type = number(start, "craftType")
        hp_start = number(start, "hp")
        hp_max = number(start, "hpMax")
        ammo_l_start = number(start, "ammoL")
        ammo_r_start = number(start, "ammoR")
        if craft_type != 3 or hp_start <= 0 or hp_max <= 0:
            raise ValueError("combat did not start with a live type-3 fighter and target HP")
        if (number(start_state, "radarActive") != 1 or number(start_state, "paused") != 1
                or number(start_state, "screen") != 0 or number(start_state, "geoUi") != 1
                or number(start_state, "modal") != 0 or number(start_state, "tickSec") != 60
                or number(start_state, "lineFlag") != 0):
            raise ValueError("combat did not start at the paused radar boundary")
        check_identity(start_ident, target, seed, craft_id, craft_type, live=True)
        check_identity(before_ident, target, seed, craft_id, craft_type, live=True)
        check_identity(armed_ident, target, seed, craft_id, craft_type, live=True)
        if not (start_i < start_state_i < before_state_i < input_i < armed_i):
            raise ValueError("combat setup and real NE click are out of order")
        if (number(before_state, "radarActive") != 1 or number(before_state, "paused") != 1
                or number(before_state, "sec") != number(start_state, "sec")
                or number(before_state, "tickSec") != 60
                or number_pair(before_radar, "hp") != (hp_start, hp_max)
                or number(before_radar, "ammoL") != ammo_l_start
                or number(before_radar, "ammoR") != ammo_r_start):
            raise ValueError("pre-combat snapshot changed radar, ammo, HP, or clock")
        if live_input.get("button") != "NE" or number(live_input, "index") != 1:
            raise ValueError("the max-range NE control was not clicked in the live screen")
        # The center is printed as x/y. Parse both axes while keeping the full real rect proof.
        center = live_input.get("center", "")
        if "/" not in center:
            raise ValueError("NE click did not log the live control center")
        center_x, center_y = (int(part, 0) for part in center.split("/", 1))
        if not (0 <= center_x < 240 and 0 <= center_y < 320):
            raise ValueError("NE click point is outside the logical display")
        if (number(live_input, "messageRec") != 1596
                or number(armed, "messageRec") != 1596
                or number(live_input, "radarActive") != 1
                or number(live_input, "paused") != 1):
            raise ValueError("NE input did not leave the live attack-max mode on radar")
        if (number(live_input, "ammoLBefore") != ammo_l_start
                or number(live_input, "ammoLAfter") != ammo_l_start
                or number(live_input, "ammoRBefore") != ammo_r_start
                or number(live_input, "ammoRAfter") != ammo_r_start):
            raise ValueError("NE range input changed weapon ammo")

        shot_records = records(lines, "pocketufo: normal-air-combat evidence stage=shot ")
        damage_records = records(lines, "pocketufo: normal-air-combat evidence stage=damage ")
        if not shot_records:
            raise ValueError("no live projectile launch consumed mounted ammo")
        shot_i, shot = shot_records[0]
        left_spent = number(shot, "ammoLAfter") < number(shot, "ammoLBefore")
        right_spent = number(shot, "ammoRAfter") < number(shot, "ammoRBefore")
        if not (left_spent or right_spent):
            raise ValueError("shot evidence has no ammo decrement")
        if (number(shot, "target") != target or number(shot, "seed") != seed
                or number(shot, "craftId") != craft_id):
            raise ValueError("shot evidence belongs to a different target or fighter")
        if ((left_spent and number(shot, "engageL") != 1)
                or (right_spent and number(shot, "engageR") != 1)):
            raise ValueError("ammo was not spent by a live engaged weapon")
        if not damage_records:
            raise ValueError("no target HP damage was recorded")
        damage_i = -1
        persistent_writeback = False
        for i, damage in damage_records:
            if i <= shot_i:
                continue
            if (number(damage, "hpAfter") >= number(damage, "hpBefore")
                    or number(damage, "ammoSpent") != 1):
                continue
            if (number(damage, "targetExpected") != target
                    or number(damage, "targetCurrent") != target
                    or number(damage, "seedExpected") != seed
                    or number(damage, "seed") != seed
                    or number(damage, "craftId") != craft_id):
                continue
            if (number(damage, "itemPresent") == 1
                    and number(damage, "itemHp") == number(damage, "hpAfter")
                    and number(damage, "hpWriteback") == 1):
                persistent_writeback = True
                damage_i = i
                break
        if damage_i < 0 or not persistent_writeback:
            raise ValueError("damage did not show the target item HP writeback after a real shot")

        terminal_i, terminal = one_record(lines, "pocketufo: normal-air-combat outcome-observed ")
        cleanup_i, cleanup = one_record(lines, "pocketufo: normal-air-combat cleanup ")
        pass_i, passed = one_record(lines, "pocketufo: normal-air-combat PASS ")
        after_state_i, after_state = stage_record(
            lines, "pocketufo: normal-air-combat state ", "after_cleanup"
        )
        after_ident_i, after_ident = stage_record(
            lines, "pocketufo: normal-air-combat identity ", "after_cleanup"
        )
        if not (input_i < shot_i < damage_i < terminal_i < after_state_i < cleanup_i < pass_i):
            raise ValueError("firing, damage, terminal, and cleanup records are out of order")

        outcome = passed.get("outcome")
        if outcome not in OUTCOMES:
            raise ValueError(f"combat terminal is not a target outcome: {outcome!r}")
        expected_kind, expected_text = OUTCOMES[outcome]
        if (number(terminal, "kind") != expected_kind
                or number(terminal, "textRec") != expected_text
                or number(terminal, "target") != target
                or number(terminal, "seedExpected") != seed
                or number(terminal, "craftId") != craft_id
                or number(terminal, "craftType") != craft_type
                or number(terminal, "craftPresent") != 1
                or number(terminal, "craftAtBase") != 0
                or number(terminal, "craftTarget") != target
                or number(terminal, "outOfFuel") != 0
                or number(terminal, "traceLost") != 0
                or number(terminal, "lineFlag") != 0):
            raise ValueError("outcome kind does not match the original radar text record")
        expected_intercept = 0 if outcome == "destroyed_target" else 1
        if number(terminal, "craftIntercept") != expected_intercept:
            raise ValueError("terminal craft order does not match the raw outcome branch")
        if outcome in ("landed_site", "crashed_site", "destroyed_target", "sea_removal"):
            if outcome in ("landed_site", "crashed_site"):
                if number(terminal, "targetCurrent") != target or number(terminal, "seed") != seed:
                    raise ValueError("landed-site terminal changed the target identity")
            elif number(terminal, "targetCurrent") != -1 or number(terminal, "seed") != -1:
                raise ValueError("removed-target terminal still exposes the original item")
        if (number(cleanup, "radarActive") != 0 or number(cleanup, "paused") != 0
                or number(cleanup, "done") != 1 or number(cleanup, "outOfFuel") != 0
                or number(cleanup, "traceLost") != 0 or number(cleanup, "lineFlag") != 0
                or number(cleanup, "lineCount") != 0
                or number(cleanup, "sec") != number(start_state, "sec")
                or number(cleanup, "messageRec") != expected_text
                or number(cleanup, "targetExpected") != target
                or number(cleanup, "targetSeed") != (seed if number(cleanup, "targetPresent") else -1)
                or number(cleanup, "craftId") != craft_id
                or number(cleanup, "craftPresent") != 1
                or number(after_ident, "craftType") != craft_type
                or number(cleanup, "craftCurrent") != number(after_ident, "craftCurrent")
                or number(cleanup, "damage") != 1 or number(cleanup, "ammoSpent") != 1
                or number(cleanup, "hpWriteback") != 1 or number(cleanup, "ammoWriteback") != 1):
            raise ValueError("radar terminal cleanup did not preserve the observed hit and ammo writeback")
        if (number(passed, "pulses") <= 0 or number(passed, "pulses") > 4096
                or number(passed, "clock") != number(start_state, "sec")
                or number(passed, "target") != target or number(passed, "seed") != seed
                or number(passed, "craftId") != craft_id
                or number(passed, "craft") < 0
                or number(passed, "hpEnd") >= hp_start
                or (number(passed, "ammoL") >= ammo_l_start
                    and number(passed, "ammoR") >= ammo_r_start)):
            raise ValueError("PASS record is not the same bounded paused natural combat")
        if (number(after_state, "radarActive") != 0 or number(after_state, "paused") != 0
                or number(after_state, "screen") != 0 or number(after_state, "modal") != 0
                or number(after_state, "sec") != number(start_state, "sec")):
            raise ValueError("post-cleanup state is still active or changed the game clock")
        check_identity(after_ident, target, seed, craft_id, craft_type, live=False)
        if outcome in ("landed_site", "crashed_site"):
            if (number(cleanup, "targetPresent") != 1
                    or number(cleanup, "targetType") != 4
                    or number(cleanup, "targetActive") != 1
                    or number(cleanup, "targetTimer") != 7200):
                raise ValueError("landed/crashed outcome lacks the real type-4 target site")
        else:
            if number(cleanup, "targetPresent") != 0:
                raise ValueError("destroyed or sea outcome kept the original target item")
            if outcome == "destroyed_target" and number(cleanup, "hp") > 0:
                raise ValueError("destroyed target has positive session HP")
            if outcome == "sea_removal" and number(cleanup, "hp") <= 0:
                raise ValueError("sea removal does not match the live positive HP value")

        snapshot_lines = records(lines, "pocketufo: normal-air-combat snapshot ")
        snapshot_stages = {fields.get("stage") for _, fields in snapshot_lines}
        if set(SNAPSHOTS) - snapshot_stages:
            raise ValueError("one or more required real frame captures are missing from the log")
        check_frames(log_path)
    except (OSError, ValueError, AssertionError) as exc:
        print(f"FAIL: {exc}")
        return 1

    print(f"PASS: live shot, persistent target damage, and {outcome} cleanup are verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
