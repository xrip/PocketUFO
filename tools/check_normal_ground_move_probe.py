#!/usr/bin/env python3
"""Check one bounded real-pointer probe in the natural ground mission."""

from __future__ import annotations

import re
import sys
from pathlib import Path


FIELD = re.compile(r"([A-Za-z][A-Za-z0-9]*)=([^\s]+)")
EVENT = re.compile(r"^pocketufo: campaign moveprobe event=(\S+) (.*)$")
CANDIDATE = re.compile(
    r"^pocketufo: campaign move candidate: (-?\d+)/(-?\d+)/(-?\d+) "
    r"via pen (-?\d+)/(-?\d+) distance (\d+)$"
)
STOP = re.compile(
    r"^pocketufo: campaign moveprobe: STOP after one candidate, "
    r"queue (\d+)/(-?\d+) active (\d+); no tick or further candidate followed$"
)


class CheckError(Exception):
    pass


def fields(raw: str) -> dict[str, str]:
    return dict(FIELD.findall(raw))


def number(record: dict[str, str], key: str) -> int:
    try:
        return int(record[key], 0)
    except (KeyError, ValueError) as exc:
        raise CheckError(f"missing or invalid {key}={record.get(key)!r}") from exc


def tuple_field(record: dict[str, str], key: str, count: int) -> tuple[int, ...]:
    try:
        parts = tuple(int(part, 0) for part in record[key].split("/"))
    except (KeyError, ValueError) as exc:
        raise CheckError(f"missing or invalid {key}={record.get(key)!r}") from exc
    if len(parts) != count:
        raise CheckError(f"invalid {key}={record.get(key)!r}")
    return parts


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_normal_ground_move_probe.py <captured-log> <private-app-dir>", file=sys.stderr)
        return 1
    lines = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace").splitlines()
    app_dir = Path(sys.argv[2])
    candidates: list[tuple[int, ...]] = []
    events: list[tuple[str, dict[str, str]]] = []
    stops: list[tuple[int, ...]] = []
    for line in lines:
        match = CANDIDATE.match(line)
        if match is not None:
            candidates.append(tuple(map(int, match.groups())))
            continue
        match = EVENT.match(line)
        if match is not None:
            events.append((match.group(1), fields(match.group(2))))
            continue
        match = STOP.match(line)
        if match is not None:
            stops.append(tuple(map(int, match.groups())))

    expected_events = ["before", "down1", "up1", "down2", "up2", "after"]
    if len(candidates) != 1:
        raise CheckError(f"expected one bounded move candidate, found {len(candidates)}")
    if len(stops) != 1:
        raise CheckError(f"expected one one-candidate stop record, found {len(stops)}")
    if [event for event, _ in events] != expected_events:
        raise CheckError("probe must capture before, both downs and ups, and after in order")

    target_x, target_y, target_z, pen_x, pen_y, _distance = candidates[0]
    records = [record for _event, record in events]
    for event, record in events:
        if number(record, "owners") != 1:
            raise CheckError(f"{event} lost the live tactical map or queue owner")
        if (number(record, "screen"), number(record, "tact"), number(record, "geoscape")) != (0, 1, 0):
            raise CheckError(f"{event} is outside the live tactical map input state")
        if (number(record, "mapInput"), number(record, "inputOn"), number(record, "canScroll")) != (1, 1, 1):
            raise CheckError(f"{event} has a closed player-input gate")
        if (number(record, "actionMode"), number(record, "inputLatch"), number(record, "mapLatch")) != (0, 0, 0):
            raise CheckError(f"{event} is not in the ordinary select/move mode")
        if (tuple_field(record, "fire", 2), number(record, "traj"), number(record, "busy")) != ((0, 0), 0, 0):
            raise CheckError(f"{event} is blocked by fire, trajectory, or map busy state")
        if number(record, "selected") != 1:
            raise CheckError(f"{event} changed the selected actor")
        if tuple_field(record, "target", 3) != (target_x, target_y, target_z):
            raise CheckError(f"{event} target identity differs from the scanned candidate")
        if tuple_field(record, "dest", 3) != (target_x, target_y, target_z):
            raise CheckError(f"{event} original floor descent changed the target coordinates")
        if tuple_field(record, "pen", 2) != (pen_x, pen_y):
            raise CheckError(f"{event} pen coordinate differs from the scanned candidate")
        if number(record, "candidateFloor") != 1 or number(record, "destFloor") != 1:
            raise CheckError(f"{event} target/destination is not the scanned floor cell")
        if number(record, "candidatePtr") == 0 or number(record, "sourcePtr") == 0:
            raise CheckError(f"{event} lacks a live source or candidate cell pointer")
        if number(record, "destPtr") != number(record, "candidatePtr"):
            raise CheckError(f"{event} floor resolution changed the candidate cell owner")
        if event in ("down1", "up1", "down2", "up2") and number(record, "cursorMatch") != 1:
            raise CheckError(f"{event} pen transform did not resolve to the candidate cell")

    before, down1, up1, down2, up2, after = records
    source_before = tuple_field(before, "source", 3)
    if source_before == (target_x, target_y, target_z):
        raise CheckError("probe candidate is the selected unit's own cell")
    for record in records:
        if number(record, "type") != 0 or number(record, "typePathFlag") != 0:
            raise CheckError("selected actor/type path flag differs from the observed source-floor case")
        if number(record, "sourceFloor") != 0 or tuple_field(record, "source", 3)[2] != 1:
            raise CheckError("the observed actor is not on the floorless layer-one source")
        if number(record, "sourceFloorPtr") != 0:
            raise CheckError("source floor pointer is not null despite the floor flag")
        if number(record, "destCost") < 0:
            raise CheckError("the destination path cost was not observed")
        if tuple_field(record, "source", 3) != source_before:
            raise CheckError("the actor moved during the bounded input probe")

    option = number(before, "optionRun")
    if option not in (0, 1) or any(number(record, "clickCommits") != option for record in records):
        raise CheckError("live double-click option and staged commit gate do not agree")
    if tuple_field(before, "queue", 2)[0] != 0 or number(before, "active") != 0:
        raise CheckError("probe did not begin with an empty idle queue")
    if number(before, "previewPathFlag") != 1 or number(before, "destCost") <= 0:
        raise CheckError("probe did not capture the preview's positive path cost")

    if option == 0:
        if number(down2, "slot3") != number(before, "slot3"):
            raise CheckError("disabled double-click option changed the pending click latch")
        if (tuple_field(down2, "queue", 2)[0] != 0 or number(down2, "active") != 0
                or number(down2, "destCost") != number(before, "destCost")):
            raise CheckError("disabled double-click option still changed path or queue state")
        branch = "double_click_option_off"
    else:
        first_commit = (number(before, "cursorMatch") == 1
                        and number(before, "slot3") > 0
                        and number(down1, "slot3") == 0)
        second_commit = (number(down1, "slot3") > 0
                         and number(up1, "slot3") > 0
                         and number(down2, "slot3") == 0)
        if not (first_commit or second_commit):
            raise CheckError("live presses did not follow the original same-cell commit latch")
        commit = down1 if first_commit else down2
        if number(commit, "actionFlag") != 1:
            raise CheckError("the committing press did not reach Input_OnAction")
        if number(commit, "destCost") == 0:
            if tuple_field(commit, "queue", 2)[0] != 0 or number(commit, "active") != 0:
                raise CheckError("path refusal unexpectedly left a live movement queue")
            branch = "input_committed_path_rejected"
        elif tuple_field(commit, "queue", 2)[0] > 0 or number(commit, "active") != 0:
            branch = "input_committed_path_queued"
        else:
            raise CheckError("Input_OnAction ran, but neither path cost nor live queue shows its result")

    after_queue = tuple_field(after, "queue", 2)
    stop_cursor, stop_len, stop_active = stops[0]
    if (after_queue[0], after_queue[1], number(after, "active")) != (
        stop_len, stop_cursor, stop_active
    ):
        raise CheckError("final trace and stop record disagree about queue state")
    if any("campaign fire:" in line or "campaign enemyturn:" in line or "campaign abort:" in line
           for line in lines):
        raise CheckError("bounded probe continued into fire, turn, or return actions")
    for image in ("normal_ground_move_probe_before.ppm", "normal_ground_move_probe_after.ppm"):
        path = app_dir / image
        if not path.is_file() or path.stat().st_size < 128 or path.read_bytes()[:2] != b"P6":
            raise CheckError(f"missing real {image} framebuffer capture")

    print("MOVE PROBE PASS: one natural candidate, six live input snapshots, "
          f"branch={branch}, actorType=0 sourceZ=1 sourceFloor=0; no movement result claimed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CheckError as exc:
        print(f"MOVE PROBE FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
