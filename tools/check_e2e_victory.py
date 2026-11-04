#!/usr/bin/env python3
"""Check the natural mission victory flow and report its measured inventory."""

from __future__ import annotations

import pathlib
import re
import sys


def fail(message: str) -> None:
    print(f"VICTORY FLOW FAILED: {message}", file=sys.stderr)
    raise SystemExit(1)


def need(text: str, pattern: str, label: str) -> re.Match[str]:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        fail(f"missing expected evidence: {label}")
    print(f"ok: {label}")
    return match


if len(sys.argv) != 3:
    fail("usage: check_e2e_victory.py LOG SAV0")

log_path = pathlib.Path(sys.argv[1])
save_path = pathlib.Path(sys.argv[2])
if not log_path.is_file():
    fail(f"log is missing: {log_path}")
text = log_path.read_text(encoding="utf-8", errors="replace")

if "campaign abort: yes at" in text or "mindendcheck" in text or "mindgoodcheck" in text:
    fail("the flow used an abort or a direct ending fixture")

need(text, r"campaign gear: craft \d+ crew [1-9]\d* cargo rifle 6 clip 10 grenade 4",
     "the original 6/10/4 starter kit was carried by the craft")
need(text, r"campaign contact created: id \d+ class 2 race 0 hp 50 at ",
     "the documented test contact was made through the contact helper")
need(text, r"campaign arrival: PASS (?:inventory screen|native screen0 path)",
     "the real arrival and launch path opened the tactical mission")

launch = need(
    text,
    r"campaign state victory_launch:.*mission (\d+).*xcom (\d+) enemy (\d+) reserve (\d+)",
    "the launched mission state was printed",
)
mission_type, xcom, enemy, reserve = map(int, launch.groups())
if mission_type != 1 or xcom <= 0 or enemy <= 0 or reserve != 0:
    fail(f"launch was not an ordinary type-1 mission with live actors and no reserve: "
         f"type={mission_type}, xcom={xcom}, enemy={enemy}, reserve={reserve}")
print(f"launch facts: type={mission_type}, xcom={xcom}, enemy={enemy}, reserve={reserve}")

need(text, r"campaign deploy: committed [1-9]\d* XCOM against [1-9]\d* enemies",
     "the live inventory confirm deployed the crew")
need(text, r"campaign move: \d+/\d+/\d+ -> \d+/\d+/\d+, tu \d+ -> \d+, queue drained",
     "a real pointer move changed an XCOM cell and spent TU")
need(text, r"campaign fire: rifle 4 ammo \d+ -> \d+, tu \d+ -> \d+, target ",
     "a real rifle input spent ammo and TU")
need(text, r"campaign throw: command 17 grenade 17 at ",
     "a real grenade input primed and threw the carried grenade")

pickup_pattern = re.compile(
    r"^pocketufo: campaign grenade source: key 0x([0-9a-f]+) ownerSlot (\d+) "
    r"kind ([1-3]) slot -?\d+ index (\d+) ammo (\d+) armed 0 fuse 0$"
)
throw_source_pattern = re.compile(
    r"^pocketufo: campaign throw source: key 0x([0-9a-f]+) pool (\d+) "
    r"ownerSlot (\d+) index (\d+) ammo (\d+) armed 0 fuse 0$"
)
landing_pattern = re.compile(
    r"^pocketufo: campaign throw identity: key 0x([0-9a-f]+) pool (\d+) "
    r"index (\d+) ammo (\d+)->(\d+) armed 1 fuse 0 cell \d+/\d+/\d+$"
)
pickup_rows = [m for line in text.splitlines() if (m := pickup_pattern.fullmatch(line))]
throw_source_rows = [m for line in text.splitlines()
                     if (m := throw_source_pattern.fullmatch(line))]
landing_rows = [m for line in text.splitlines()
                if (m := landing_pattern.fullmatch(line))]
if len(pickup_rows) != 1 or len(throw_source_rows) != 1 or len(landing_rows) != 1:
    fail("the flow must report one grenade pickup, throw source, and landing identity")
pickup = pickup_rows[0]
throw_source = throw_source_rows[0]
landing = landing_rows[0]
if (pickup.group(1), pickup.group(2), pickup.group(4), pickup.group(5)) != (
    throw_source.group(1), throw_source.group(3), throw_source.group(4), throw_source.group(5)
):
    fail("the grenade held before prime differs from the picked-up item")
if (throw_source.group(1), throw_source.group(2), throw_source.group(4), throw_source.group(5)) != (
    landing.group(1), landing.group(2), landing.group(3), landing.group(4)
) or landing.group(4) != landing.group(5):
    fail("the landed grenade key, pool slot, item index, or ammo differs from its source")
print("ok: pickup, throw, and landing kept the same grenade identity")

if len(re.findall(r"campaign enemyturn: started enemy 1 input 0", text)) < 2:
    fail("fewer than two real End Turn inputs started an enemy turn")
print("ok: real End Turn inputs started the enemy phase")

win = need(
    text,
    r"campaign natural-fight outcome: victory after .*screen 13, xcom ([1-9]\d*), enemy \d+, "
    r"reserve 0, missionVictory 1",
    "the final live action opened the normal victory debrief with the victory bit set",
)
need(text, r"campaign natural-fight: PASS turns \d+ actions \d+",
     "the bounded natural fight stopped on that victory")
need(text, r"campaign close-debrief: victory 1 screen 13 xcom [1-9]\d* enemy \d+ reserve 0",
     "the close helper saw a won screen-13 debrief before input")
need(text, r"campaign close-debrief: PASS normal victory returned to geoscape",
     "the normal OK input returned to the live geoscape without an abort")

def cargo(label: str) -> tuple[dict[int, int], dict[tuple[int, int], int]]:
    object_rows = re.findall(
        rf"campaign cargo item {re.escape(label)}: object (\d+) craftIndexQty (\d+) shipSlotQty (\d+)",
        text,
    )
    site_rows = re.findall(
        rf"campaign cargo site {re.escape(label)}: site (\d+) object (\d+) qty (\d+)",
        text,
    )
    return (
        {int(slot): int(qty) for slot, qty, _ship_qty in object_rows},
        {(int(site), int(slot)): int(qty) for site, slot, qty in site_rows},
    )


debrief_craft, debrief_site = cargo("victory_debrief")
returned_craft, returned_site = cargo("victory_returned")
debrief_nonzero = {slot: qty for slot, qty in debrief_craft.items() if qty > 0}
debrief_site_nonzero = {key: qty for key, qty in debrief_site.items() if qty > 0}
returned_nonzero = {slot: qty for slot, qty in returned_craft.items() if qty > 0}
returned_site_nonzero = {key: qty for key, qty in returned_site.items() if qty > 0}
base_stock = {
    int(slot): int(qty)
    for slot, qty in re.findall(
        r"strategy stock victory_returned: slot (\d+) qty (\d+)", text
    )
}
strategy_site = [
    (int(site), int(slot), int(qty))
    for site, slot, qty in re.findall(
        r"strategy site-item victory_returned: site (\d+) slot (\d+) qty (\d+)", text
    )
]
if not debrief_nonzero and not debrief_site_nonzero:
    fail("the win debrief reported no recovered craft or site quantities")
if not returned_nonzero and not returned_site_nonzero:
    fail("the returned geoscape reported no recovered craft or site quantities")
need(text, r"strategy tuple victory_returned:",
     "the post-debrief base and site state was observed")

print("measured recovered ObjectRec craft quantities at debrief:")
for slot, qty in sorted(debrief_nonzero.items()):
    print(f"  object {slot}: {qty}")
for (site, slot), qty in sorted(debrief_site_nonzero.items()):
    print(f"  site {site} object {slot}: {qty}")
print("measured craft/site quantities after return:")
for slot, qty in sorted(returned_nonzero.items()):
    print(f"  craft object {slot}: {qty}")
for (site, slot), qty in sorted(returned_site_nonzero.items()):
    print(f"  site {site} object {slot}: {qty}")
print("measured base stock after return:")
for slot, qty in sorted(base_stock.items()):
    print(f"  object {slot}: {qty}")
for site, slot, qty in strategy_site:
    print(f"  site {site} object {slot}: {qty}")

if not save_path.is_file() or save_path.stat().st_size == 0:
    fail(f"slot-0 handoff save is missing or empty: {save_path}")
print(f"ok: private slot-0 handoff save exists ({save_path.stat().st_size} bytes)")
print(f"victory flow passed; launch xcom count was {win.group(1)}")
