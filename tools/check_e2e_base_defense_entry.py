#!/usr/bin/env python3
"""Check the live base-defense entry, deployment, and first enemy turn."""

import pathlib
import re
import sys


def fail(message: str) -> None:
    raise SystemExit(f"BASE DEFENSE ENTRY FAILED: {message}")


def read_log(path: pathlib.Path) -> bytes:
    try:
        return path.read_bytes()
    except OSError as exc:
        fail(f"cannot read log {path}: {exc}")


def require(pattern: bytes, data: bytes, message: str) -> re.Match[bytes]:
    match = re.search(pattern, data)
    if match is None:
        fail(message)
    return match


def require_step_order(log: bytes, steps: tuple[bytes, ...]) -> None:
    cursor = 0
    for step in steps:
        position = log.find(step, cursor)
        if position < 0:
            fail(f"missing or out-of-order real UI step: {step.decode('ascii', errors='replace')}")
        cursor = position + len(step)


def campaign_state(log: bytes, label: str) -> dict[str, int]:
    label_bytes = re.escape(label.encode("ascii"))
    match = require(
        rb"(?m)^pocketufo: campaign state "
        + label_bytes
        + rb": screen (?P<screen>\d+) tact (?P<tact_live>\d+)/(?P<tact_flag>\d+) "
        + rb"mission (?P<mission>\d+)[^\r\n]* xcom (?P<xcom>\d+) "
        + rb"enemy (?P<enemy>\d+) reserve (?P<reserve>\d+)[^\r\n]* "
        + rb"queue (?P<queue_cursor>\d+)/(?P<queue_len>\d+) active (?P<active>\d+) "
        + rb"phase (?P<phase>\d+) busy (?P<busy>\d+) enemyturn (?P<enemyturn>\d+) "
        + rb"input (?P<input>\d+) battle (?P<battle>\d+) enemyCells (?P<enemy_cells>\d+)"
        + rb"[^\r\n]*\r?$",
        log,
        f"missing campaign state report {label}",
    )
    return {name: int(value) for name, value in match.groupdict().items()}


def tactical_section(log: bytes, label: str, dump_name: str) -> bytes:
    label_bytes = re.escape(label.encode("ascii"))
    start = require(
        rb"(?m)^pocketufo: campaign state " + label_bytes + rb":",
        log,
        f"missing campaign state report {label}",
    ).start()
    end_marker = b"pocketufo: step dump " + dump_name.encode("ascii")
    end = log.find(end_marker, start)
    if end < 0:
        fail(f"missing tactical dump step after state {label}")
    return log[start:end]


def tactical_rows(section: bytes, side: str) -> list[tuple[int, int, int, int, int]]:
    if side == "xcom":
        pattern = rb"(?m)^  xcom (\d+): at (-?\d+)/(-?\d+)/(-?\d+), uTu \d+, hp (-?\d+), status -?\d+, large \d+\r?$"
    else:
        pattern = rb"(?m)^  enemy (\d+): type \d+ at (-?\d+)/(-?\d+)/(-?\d+), uTu \d+, hp (-?\d+), status -?\d+, aiTick -?\d+, aiDone -?\d+, large \d+\r?$"
    return [tuple(int(value) for value in match.groups()) for match in re.finditer(pattern, section)]


def validate_tactical_rows(
    section: bytes, side: str, expected_count: int, stage: str
) -> list[int]:
    rows = tactical_rows(section, side)
    if len(rows) != expected_count:
        fail(f"{stage} logged {len(rows)} {side} cells for roster count {expected_count}")

    positions: set[tuple[int, int, int]] = set()
    hit_points: list[int] = []
    for expected_index, (index, x, y, z, hp) in enumerate(rows):
        if index != expected_index:
            fail(f"{stage} {side} cell indexes are not continuous at {index}")
        if min(x, y, z) < 0 or (x, y, z) == (0, 0, 0):
            fail(f"{stage} {side} {index} has an invalid or empty cell {x}/{y}/{z}")
        if (x, y, z) in positions:
            fail(f"{stage} {side} share cell {x}/{y}/{z}")
        if hp <= 0:
            fail(f"{stage} {side} {index} is not living: hp {hp}")
        positions.add((x, y, z))
        hit_points.append(hp)
    return hit_points


def main() -> int:
    if len(sys.argv) != 3:
        fail("usage: check_e2e_base_defense_entry.py LOG OUTPUT_DIR")
    log_path = pathlib.Path(sys.argv[1])
    out_dir = pathlib.Path(sys.argv[2])
    log = read_log(log_path)

    if re.search(rb"(?m)^pocketufo: campaign base-defense-entry: FAIL\b", log):
        fail("the native subtype-6 event did not reach a confirmed Base At screen")

    require_step_order(
        log,
        (
            b"pocketufo: step click 120 124",
            b"pocketufo: step pick",
            b"pocketufo: step close",
            b"pocketufo: step close",
            b"pocketufo: state base_defense_setup:",
            b"pocketufo: step click 60 308",
            b"pocketufo: campaign base-defense-entry: staged fixture",
            b"pocketufo: campaign base-defense-entry: PASS Base At",
            b"pocketufo: state base_defense_base_at:",
            b"pocketufo: step click 120 290",
            b"pocketufo: campaign state base_defense_initial_loadout:",
            b"pocketufo: step dump base_defense_initial_loadout.ppm",
            b"pocketufo: campaign input: key confirm",
            b"pocketufo: step confirm",
            b"pocketufo: step gametick 20",
            b"pocketufo: state base_defense_deployed:",
            b"pocketufo: campaign state base_defense_deployed:",
            b"pocketufo: step dump base_defense_deployed.ppm",
            b"pocketufo: campaign enemyturn: started enemy 1 input 0",
            b"pocketufo: campaign enemyturn: PASS",
            b"pocketufo: campaign state base_defense_after_first_enemyturn:",
            b"pocketufo: step dump base_defense_after_first_enemyturn.ppm",
        ),
    )

    setup = require(
        rb"(?m)^pocketufo: state base_defense_setup: [^\r\n]*"
        rb"geoscape screen 0, geo ui byte 1, tact flag 0, bases 1,[^\r\n]*"
        rb" soldiers ([1-9]\d*)\b",
        log,
        "the normal menu and placement path did not leave one base with live soldiers on map screen 0",
    )
    defenders_at_setup = int(setup.group(1))

    hour_speed = require(
        rb"(?m)^pocketufo: strategy tuple base_defense_hour_speed: [^\r\n]*tickSec 3600 pause 0\b",
        log,
        "the real speed-button click did not set the logged one-hour clock step",
    )
    if hour_speed.start() < log.find(b"pocketufo: step click 60 308"):
        fail("the hour-speed tuple was logged before the real speed-button click")

    staged = require(
        rb"(?m)^pocketufo: campaign base-defense-entry: staged fixture item \d+ seed (\d+) "
        rb"origin -?\d+/-?\d+ class 2 race 0 strategicHp -1 subtype 6 phase 4 result -?\d+ "
        rb"baseCount 1 defenders (\d+)\b",
        log,
        "the entry fixture did not report a native class-2 race-0 UFO with factory HP and real base defenders",
    )
    seed = int(staged.group(1))
    staged_defenders = int(staged.group(2))
    if staged_defenders <= 0:
        fail(f"no base defenders were available at event entry: {staged_defenders}")

    arrived = require(
        rb"(?m)^pocketufo: campaign base-defense-entry: PASS Base At screen 20 confirm 1 follows 1 "
        rb"class 2 race 0 targetBase 0 seed (\d+) pulses (\d+) tickSec 3600 "
        rb"seconds (\d+)->(\d+)\r?$",
        log,
        "the real subtype-6 event did not reach Base At through normal event ticks",
    )
    if int(arrived.group(1)) != seed:
        fail(f"Base At event seed changed: staged {seed}, arrived {int(arrived.group(1))}")
    if int(arrived.group(2)) <= 0:
        fail("Base At was not reached through any real event pulses")
    if int(arrived.group(4)) <= int(arrived.group(3)):
        fail("the real geoscape clock did not advance during event travel")

    require(
        rb"(?m)^pocketufo: state base_defense_base_at: [^\r\n]*"
        rb"geoscape screen 20, geo ui byte 1, tact flag 0, bases 1,",
        log,
        "the live app did not show the real Base At screen over one geoscape base",
    )

    initial = campaign_state(log, "base_defense_initial_loadout")
    if initial["screen"] != 1 or initial["tact_live"] != 1 or initial["tact_flag"] != 1 or initial["mission"] != 3:
        fail(
            "Base At did not open the type-3 tactical inventory screen: "
            f"screen {initial['screen']} tact {initial['tact_live']}/{initial['tact_flag']} mission {initial['mission']}"
        )
    if initial["xcom"] <= 0 or initial["enemy"] != 0 or initial["reserve"] <= 0:
        fail(
            "initial type-3 loadout must have XCOM, no active enemies, and a hidden reserve: "
            f"XCOM {initial['xcom']} enemy {initial['enemy']} reserve {initial['reserve']}"
        )
    if initial["battle"] != 0:
        fail("the type-3 inventory screen already ended the battle")

    deployed = campaign_state(log, "base_defense_deployed")
    if deployed["screen"] != 0 or deployed["tact_live"] != 1 or deployed["tact_flag"] != 1 or deployed["mission"] != 3:
        fail(
            "the mapped confirm did not reach the live type-3 tactical map: "
            f"screen {deployed['screen']} tact {deployed['tact_live']}/{deployed['tact_flag']} mission {deployed['mission']}"
        )
    if deployed["input"] != 1 or deployed["busy"] != 0 or deployed["enemyturn"] != 0:
        fail(
            "the deployed map did not return to idle player input: "
            f"input {deployed['input']} busy {deployed['busy']} enemyturn {deployed['enemyturn']}"
        )
    if deployed["xcom"] <= 0 or deployed["enemy"] != 0 or deployed["reserve"] != initial["reserve"]:
        fail(
            "mapped confirm changed the type-3 rosters unexpectedly: "
            f"XCOM {deployed['xcom']} enemy {deployed['enemy']} reserve {deployed['reserve']}"
        )
    if deployed["battle"] != 0 or deployed["queue_len"] != 0 or deployed["active"] != 0:
        fail("the deployed map is not idle before the first enemy turn")

    deployed_hp = validate_tactical_rows(
        tactical_section(log, "base_defense_deployed", "base_defense_deployed.ppm"),
        "xcom",
        deployed["xcom"],
        "deployed map",
    )

    enemyturn = require(
        rb"(?m)^pocketufo: campaign enemyturn: ticks (\d+) enemy 0 input 1 busy 0 "
        rb"queue \d+/0 changed \d+\r?$",
        log,
        "the real enemy turn did not finish with player input, idle logic, and an empty queue",
    )
    if int(enemyturn.group(1)) <= 0:
        fail("the real enemy turn completed without any tactical ticks")

    post = campaign_state(log, "base_defense_after_first_enemyturn")
    if post["screen"] != 0 or post["tact_live"] != 1 or post["tact_flag"] != 1 or post["mission"] != 3:
        fail("the first enemy turn left the type-3 tactical map")
    if post["input"] != 1 or post["busy"] != 0 or post["enemyturn"] != 0 or post["queue_len"] != 0 or post["active"] != 0:
        fail("the first enemy turn did not return to idle player input with no active queue")
    if post["battle"] != 0:
        fail("the first enemy turn ended the battle")
    if post["xcom"] <= 0:
        fail("the first enemy turn left no living XCOM defenders")
    if post["enemy"] <= 0 or post["enemy_cells"] != post["enemy"]:
        fail(
            "the first enemy turn did not leave active enemies on distinct tactical cells: "
            f"enemy {post['enemy']} cells {post['enemy_cells']}"
        )
    if post["reserve"] >= deployed["reserve"]:
        fail(
            f"the first enemy turn did not reduce the hidden reserve: {deployed['reserve']} to {post['reserve']}"
        )

    post_section = tactical_section(log, "base_defense_after_first_enemyturn", "base_defense_after_first_enemyturn.ppm")
    surviving_hp = validate_tactical_rows(post_section, "xcom", post["xcom"], "after first enemy turn")
    validate_tactical_rows(post_section, "enemy", post["enemy"], "after first enemy turn")

    for image in (
        "base_defense_new_base.ppm",
        "base_defense_base_at.ppm",
        "base_defense_initial_loadout.ppm",
        "base_defense_deployed.ppm",
        "base_defense_after_first_enemyturn.ppm",
    ):
        path = out_dir / image
        if not path.is_file() or path.stat().st_size <= 32:
            fail(f"missing or empty live screen capture: {path}")

    print("ok: the native subtype-6 event reached Base At through the real clock and event path")
    print(f"ok: base 0 had {staged_defenders} available defenders before event travel")
    print(
        "ok: the real Base At click opened type-3 inventory with "
        f"{initial['xcom']} XCOM and {initial['reserve']} hidden reserve units"
    )
    print(f"ok: the mapped confirm placed {deployed['xcom']} XCOM units on distinct cells and returned player input")
    print(
        "ok: the real first enemy turn passed and returned idle with "
        f"{post['enemy']} enemies on {post['enemy_cells']} cells"
    )
    print(
        "observation: active XCOM "
        f"{deployed['xcom']}->{post['xcom']}, hidden reserve {deployed['reserve']}->{post['reserve']}, "
        f"deployed defender HP {deployed_hp}, post-turn defender HP {surviving_hp}"
    )
    print(
        "ok: captured new-base, Base At, initial loadout, deployed map, and first enemy-turn frames; "
        "the generic image check covers frame size and visible content, not visual parity"
    )
    print("limit: this run proves entry and the first enemy turn only; it does not prove tactical defeat or an ending")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
