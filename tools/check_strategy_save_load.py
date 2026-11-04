#!/usr/bin/env python3
"""Compare live strategy records around a fresh-process save/load."""

from pathlib import Path
import re
import sys


def line_for(lines, prefix, label):
    marker = prefix + label + b":"
    return next((line for line in lines if line.startswith(marker)), None)


def rows_for(lines, kind, label):
    marker = b"pocketufo: strategy " + kind + b" " + label + b":"
    return [line[len(marker) :].strip() for line in lines if line.startswith(marker)]


def required_tuple(lines, label):
    line = line_for(lines, b"pocketufo: strategy tuple ", label)
    if line is None:
        raise SystemExit(f"STRATEGY SAVE FAILED: missing tuple for {label.decode()}")
    return line.split(b": ", 2)[2]


def number_field(tuple_text, name):
    match = re.search(rb"(?:^| )" + name + rb" ([0-9]+)(?: |$)", tuple_text)
    if match is None:
        raise SystemExit(f"STRATEGY SAVE FAILED: tuple has no {name.decode()} field")
    return int(match.group(1))


def research_progress(rows):
    result = {}
    for row in rows:
        match = re.match(
            rb"id ([0-9]+) active ([0-9]+) unlocked ([0-9]+) progress ([0-9]+)/([0-9]+) scientists ([0-9]+)$",
            row,
        )
        if match is not None:
            topic, active, unlocked, progress, total, assigned = map(int, match.groups())
            result[topic] = (active, unlocked, progress, total, assigned)
    return result


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_strategy_save_load.py SAVED_LOG LOADED_LOG")
        return 2

    saved_lines = Path(sys.argv[1]).read_bytes().splitlines()
    loaded_lines = Path(sys.argv[2]).read_bytes().splitlines()

    saved_tuple = required_tuple(saved_lines, b"before_save")
    loaded_tuple = required_tuple(loaded_lines, b"after_load")
    if saved_tuple != loaded_tuple:
        raise SystemExit(
            f"STRATEGY SAVE FAILED: base, funds, clock, staff, or job totals changed: "
            f"{saved_tuple!r} -> {loaded_tuple!r}"
        )

    kinds = (b"stock", b"topic", b"unlocked-product", b"product", b"site", b"site-item")
    saved_records = {kind: rows_for(saved_lines, kind, b"before_save") for kind in kinds}
    loaded_records = {kind: rows_for(loaded_lines, kind, b"after_load") for kind in kinds}
    for kind in kinds:
        if saved_records[kind] != loaded_records[kind]:
            raise SystemExit(
                f"STRATEGY SAVE FAILED: {kind.decode()} changed: "
                f"{saved_records[kind]!r} -> {loaded_records[kind]!r}"
            )

    topics = saved_records[b"topic"]
    active_topic = next(
        (row for row in topics if re.search(rb"active 1 unlocked 0 progress ([0-9]+)/([1-9][0-9]*) scientists [1-9][0-9]*$", row)),
        None,
    )
    if active_topic is None:
        raise SystemExit("STRATEGY SAVE FAILED: no real assigned, unfinished research topic was saved")

    if not any(re.search(rb"slot 112 qty [1-9][0-9]*$", row) for row in saved_records[b"stock"]):
        raise SystemExit("STRATEGY SAVE FAILED: no real engineer is present at the base")
    if not any(re.search(rb"slot 113 qty [1-9][0-9]*$", row) for row in saved_records[b"stock"]):
        raise SystemExit("STRATEGY SAVE FAILED: no real scientist is present at the base")

    pending_site = any(re.search(rb"flag 24 target [0-9]+ .*", row) for row in saved_records[b"site"])
    pending_item = any(
        re.search(rb"site [0-9]+ slot 4 qty [1-9][0-9]*$", row)
        for row in saved_records[b"site-item"]
    )
    if not pending_site or not pending_item:
        raise SystemExit("STRATEGY SAVE FAILED: no real pending shop shipment with rifle stock was saved")

    advanced_tuple = required_tuple(loaded_lines, b"after_advance")
    loaded_sec = number_field(loaded_tuple, b"sec")
    advanced_sec = number_field(advanced_tuple, b"sec")
    if advanced_sec <= loaded_sec:
        raise SystemExit(f"STRATEGY CLOCK FAILED: game time did not advance: {loaded_sec} -> {advanced_sec}")
    if number_field(advanced_tuple, b"sites") != 0:
        raise SystemExit("STRATEGY CLOCK FAILED: the pending rifle site did not arrive and close")

    advanced_stock = rows_for(loaded_lines, b"stock", b"after_advance")
    if not any(re.search(rb"slot 4 qty [1-9][0-9]*$", row) for row in advanced_stock):
        raise SystemExit("STRATEGY CLOCK FAILED: the pending rifle was not added to base stock")

    loaded_topics = research_progress(rows_for(loaded_lines, b"topic", b"after_load"))
    advanced_topics = research_progress(rows_for(loaded_lines, b"topic", b"after_advance"))
    advanced_work = any(
        topic in advanced_topics
        and (advanced_topics[topic][1] != 0 or advanced_topics[topic][2] > saved[2])
        for topic, saved in loaded_topics.items()
        if saved[0] != 0 and saved[4] != 0 and saved[3] != 0
    )
    if not advanced_work:
        raise SystemExit("STRATEGY CLOCK FAILED: no assigned live research topic gained work")

    print(f"ok: fresh-process load kept the strategy tuple: {saved_tuple.decode('ascii')}")
    print(f"ok: saved {len(saved_records[b'stock'])} nonzero base stock rows")
    print(f"ok: saved {len(topics)} topic rows, including ongoing research: {active_topic.decode('ascii')}")
    print(f"ok: saved pending purchase site and {len(saved_records[b'site-item'])} site item rows")
    print("ok: saved and loaded all live strategy rows before advancing time")
    print(f"ok: clock advanced {advanced_sec - loaded_sec} seconds, delivered the rifle, and advanced research")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
