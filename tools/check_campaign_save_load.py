#!/usr/bin/env python3
"""Compare only geoscape fields and records written by the campaign save."""

from pathlib import Path
import re
import sys


STATE_RE = re.compile(
    rb"bases ([0-9]+), base 0 at (-?[0-9]+)/(-?[0-9]+), "
    rb"soldiers (-?[0-9]+), funds (-?[0-9]+)"
)
# The displayed roster id is Soldier.nUnk440, which the save record writes and
# reads at Soldier+0x1B8. The checker never uses runtime TactItem.nRefId values.
ROSTER_RE = re.compile(
    rb"^  roster ([0-9]+) id ([0-9]+) name .* type ([0-9]+) "
    rb"armor (-?[0-9]+) craft ([0-9]+) loadout ([0-9]+)$"
)
LOADOUT_RE = re.compile(
    rb"^    loadout slot ([0-9]+) sort ([0-9]+) ammo type ([0-9]+)$"
)
# The cargo tuple is the geoscape ObjectRec.aItemQty column, not a tactical item id.
CARGO_RE = re.compile(
    rb"^  stores base ([0-9]+) rifle ([0-9]+) clip ([0-9]+) "
    rb"grenade ([0-9]+) craft ([0-9]+) cargo ([0-9]+)/([0-9]+)/([0-9]+)$"
)


def section(lines, header):
    start = next((i for i, line in enumerate(lines) if header in line), -1)
    if start < 0:
        return []
    out = []
    for line in lines[start + 1 :]:
        if line.startswith(b"pocketufo: "):
            break
        out.append(line)
    return out


def state_tuple(lines, label):
    prefix = b"pocketufo: state " + label + b":"
    line = next((line for line in lines if prefix in line), b"")
    match = STATE_RE.search(line)
    if match is None:
        raise SystemExit(f"SAVE STATE FAILED: missing base/crew/funds tuple for {label.decode()}")
    return tuple(part.decode("ascii") for part in match.groups())


def campaign_tuples(lines, label):
    block = section(lines, b"pocketufo: campaign state " + label + b":")
    roster = []
    loadout = []
    cargo_match = None
    for line in block:
        match = ROSTER_RE.match(line)
        if match is not None:
            roster.append(match.groups())
        match = LOADOUT_RE.match(line)
        if match is not None:
            loadout.append(match.groups())
        match = CARGO_RE.match(line)
        if match is not None:
            cargo_match = match
    if not roster or cargo_match is None:
        raise SystemExit(f"SAVE STATE FAILED: empty roster or cargo tuple for {label.decode()}")
    return (
        tuple(b":".join(row).decode("ascii") for row in roster),
        tuple(b":".join(row).decode("ascii") for row in loadout),
        (b":".join(cargo_match.groups()).decode("ascii"),),
    )


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_campaign_save_load.py SAVED_LOG LOADED_LOG")
        return 2

    saved_lines = Path(sys.argv[1]).read_bytes().splitlines()
    loaded_lines = Path(sys.argv[2]).read_bytes().splitlines()
    saved_geo = state_tuple(saved_lines, b"before_save")
    loaded_geo = state_tuple(loaded_lines, b"after_load")
    if saved_geo != loaded_geo:
        raise SystemExit(f"SAVE STATE FAILED: geoscape tuple changed: {saved_geo} -> {loaded_geo}")

    saved_records = campaign_tuples(saved_lines, b"before_save")
    loaded_records = campaign_tuples(loaded_lines, b"after_load")
    names = ("serialized roster", "serialized loadout", "craft cargo")
    for name, saved, loaded in zip(names, saved_records, loaded_records):
        if not saved or saved != loaded:
            raise SystemExit(f"SAVE STATE FAILED: {name} changed: {saved} -> {loaded}")

    print(f"ok: saved geoscape tuple {saved_geo}")
    print(f"ok: saved {len(saved_records[0])} roster rows and {len(saved_records[1])} loadout rows")
    print(f"ok: saved craft cargo {saved_records[2][0]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
