#!/usr/bin/env python3
"""Check a natural victory flow whose live Rifle owners leave the transport first."""

from __future__ import annotations

import pathlib
import re
import sys


def fail(message: str) -> None:
    print(f"OUTSIDE-PLANE VICTORY FAILED: {message}", file=sys.stderr)
    raise SystemExit(1)


def need(text: str, pattern: str, label: str) -> re.Match[str]:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        fail(f"missing expected evidence: {label}")
    print(f"ok: {label}")
    return match


if len(sys.argv) != 4:
    fail("usage: check_e2e_victory_outside_plane.py LOG SAV0 OUTPUT_DIR")

log_path = pathlib.Path(sys.argv[1])
save_path = pathlib.Path(sys.argv[2])
out_dir = pathlib.Path(sys.argv[3])
if not log_path.is_file():
    fail(f"log is missing: {log_path}")
text = log_path.read_text(encoding="utf-8", errors="replace")

if "campaign abort: yes at" in text or "mindendcheck" in text or "mindgoodcheck" in text:
    fail("the flow used an abort or a direct ending fixture")

need(text, r"campaign gear: craft \d+ crew [1-9]\d* cargo rifle 6 clip 10 grenade 4",
     "the original 6/10/4 starter kit reached the craft")
need(text, r"campaign contact created: id \d+ class 2 race 0 hp 50 at ",
     "the test contact was made through the campaign helper")
need(text, r"campaign arrival: PASS (?:inventory screen|native screen0 path)",
     "the real arrival path opened the tactical mission")

launch = need(
    text,
    r"campaign state victory_launch:.*mission (\d+).*xcom (\d+) enemy (\d+) reserve (\d+)",
    "the launched mission state was printed",
)
mission_type, xcom_count, enemy_count, reserve_count = map(int, launch.groups())
if mission_type != 1 or xcom_count <= 0 or enemy_count <= 0 or reserve_count != 0:
    fail(f"launch was not an ordinary type-1 mission: type={mission_type}, "
         f"xcom={xcom_count}, enemy={enemy_count}, reserve={reserve_count}")
need(text, r"campaign deploy: committed [1-9]\d* XCOM against [1-9]\d* enemies",
     "live inventory input deployed the crew")

layout = need(
    text,
    r"campaign natural-fight-outside-plane layout: mission 1 layers 1 geometry 6/6 "
    r"craftTile (\d+)/(\d+) footprint (\d+)\.\.(\d+)/(\d+)\.\.(\d+) crew 8 deployGroup 0",
    "the new flow proved mission, layout selector, geometry, anchor, and group zero",
)
craft_x, craft_y, plane_min_x, plane_max_x, plane_min_y, plane_max_y = map(
    int, layout.groups()
)
if (plane_min_x, plane_max_x, plane_min_y, plane_max_y) != (
    10 * craft_x, 10 * craft_x + 9, 10 * craft_y, 10 * craft_y + 19
):
    fail("the reported footprint does not match the live craft tile")
crew_rows = re.findall(
    r"^pocketufo: campaign natural-fight-outside-plane crew: slot (\d+) actor 0x[0-9A-Fa-f]+ "
    r"expected (\d+)/(\d+)/(\d+) actual (\d+)/(\d+)/(\d+) cellSlot (\d+) "
    r"assignedCraft \d+ valid (\d+)$",
    text,
    re.MULTILINE,
)
if len(crew_rows) != 8 or {int(row[0]) for row in crew_rows} != set(range(8)):
    fail(f"expected eight ordered live group-zero crew checks, found {len(crew_rows)}")
group0 = ((4, 9, 1), (5, 9, 1), (4, 10, 1), (5, 10, 1),
          (4, 11, 1), (5, 11, 1), (4, 12, 1), (5, 12, 1))
for row in crew_rows:
    slot, ex, ey, ez, ax, ay, az, cell_slot, valid = map(int, row)
    group_x, group_y, group_z = group0[slot]
    wanted = (10 * craft_x + group_x, 10 * craft_y + group_y, group_z)
    if valid != 1 or cell_slot < 0 or (ex, ey, ez) != wanted \
            or (ex, ey, ez) != (ax, ay, az):
        fail(f"live group-zero crew slot {slot} did not match its expected cell")
print("ok: eight live XCOM cells matched deploy group zero")

selection = need(
    text,
    r"campaign outside-plane move: selected by live widget 0 actor 0x([0-9A-Fa-f]+) "
    r"-> 0x([0-9A-Fa-f]+)",
    "a real widget-0 input selected the movement owner",
)
if selection.group(1).lower() == selection.group(2).lower():
    fail("widget 0 did not change the selected actor")

snap_quote_pattern = re.compile(
    r"^pocketufo: campaign outside-plane Snap quote: actorKey 0x([0-9A-Fa-f]+) roster (\d+) "
    r"weaponKey 0x([0-9A-Fa-f]+) itemIndex (\d+) handWidget (17|18) snapWidget 33 "
    r"snapCost ([1-9]\d*) snapEnabled ([1-9]\d*) widgetEnabled 1 widgetCallback 1 "
    r"menuOpened 1 menuCloseKey confirm menuClosed 1 selectedWeapon 1 "
    r"screen 0 ammo (\d+)->(\d+) tu (\d+)->(\d+) queue 0/0 quoteReady 1$",
    re.MULTILINE,
)
snap_quotes = list(snap_quote_pattern.finditer(text))
if not snap_quotes:
    fail("missing expected evidence: the real Rifle hand menu supplied and canceled a Snap quote")
for quote in snap_quotes:
    if quote.group(8) != quote.group(9) or quote.group(10) != quote.group(11):
        fail("opening and canceling the hand menu changed ammo or TU")
print(f"ok: {len(snap_quotes)} real Rifle hand-menu Snap quotes were canceled without cost")

move_quote_pattern = re.compile(
    r"campaign outside-plane move quote: actor 0x([0-9A-Fa-f]+) roster (\d+) targetLayer (\d+) "
    r"from (\d+)/(\d+)/(\d+) to (\d+)/(\d+)/(\d+) plane (\d+)\.\.(\d+)/(\d+)\.\.(\d+) "
    r"pathCost (\d+) snapCost (\d+) tuBefore (\d+) hpBefore (\d+)",
    re.MULTILINE,
)
move_quotes = list(move_quote_pattern.finditer(text))
if not move_quotes:
    fail("missing expected evidence: the path and live Snap cost were quoted before movement")

move_done = need(
    text,
    r"campaign outside-plane move: actor 0x([0-9A-Fa-f]+) "
    r"(\d+)/(\d+)/(\d+) -> (\d+)/(\d+)/(\d+) pathCost (\d+) snapCost (\d+) "
    r"tu (\d+) -> (\d+) hp (\d+) -> (\d+) queue (\d+)/(\d+) outside 1",
    "real pointer movement left the plane and drained its queue",
)
(
    done_actor, _dsx, _dsy, _dsz, _ddx, _ddy, _ddz, done_path, done_snap,
    done_tu_before, done_tu_after, _hp0, _hp1, queue_len, queue_active,
) = move_done.groups()
done_start = tuple(map(int, move_done.groups()[1:4]))
done_end = tuple(map(int, move_done.groups()[4:7]))
matching_move_quotes = [
    quote for quote in move_quotes
    if quote.start() < move_done.start()
    and quote.group(1).lower() == done_actor.lower()
    and tuple(map(int, quote.groups()[3:6])) == done_start
    and tuple(map(int, quote.groups()[6:9])) == done_end
    and int(quote.group(14)) == int(done_path)
    and int(quote.group(15)) == int(done_snap)
    and int(quote.group(16)) == int(done_tu_before)
]
if not matching_move_quotes:
    fail("no earlier candidate quote matches the successful actor, path, TU, and XYZ move record")
move_quote = matching_move_quotes[-1]
print("ok: a prior candidate quote matches the successful pointer move")
move_gate_pattern = re.compile(
    r"^pocketufo: campaign outside-plane move input gate: actor 0x([0-9A-Fa-f]+) "
    r"target (\d+)/(\d+)/(\d+) fall (\d+) fire (\d+) throw (\d+) "
    r"queueActive (\d+) queueLen (\d+) screen (\d+) modal (\d+) player (\d+) "
    r"latch (\d+) busy (\d+)$",
    re.MULTILINE,
)
move_gates = list(move_gate_pattern.finditer(text))
move_gate = next(
    (
        match for match in reversed(move_gates)
        if move_quote.start() < match.start() < move_done.start()
        and match.group(1).lower() == done_actor.lower()
        and tuple(map(int, match.groups()[1:4])) == done_end
    ),
    None,
)
if move_gate is None:
    fail("the successful full move has no clear live Input_OnAction gate record")
if tuple(map(int, move_gate.groups()[4:])) != (0, 0, 0, 0, 0, 0, 0, 1, 0, 0):
    fail("the successful full move entered Input_OnAction with a raw gate set")
if selection.start() > move_quote.start():
    fail("the live actor selection came after the successful move quote")
(
    move_actor, move_slot, target_layer, sx, sy, sz, dx, dy, dz,
    quote_min_x, quote_max_x, quote_min_y, quote_max_y,
    path_cost, snap_cost, tu_before, _hp_before,
) = move_quote.groups()
snap_quote = next(
    (quote for quote in reversed(snap_quotes)
     if quote.start() < move_quote.start() and quote.group(1).lower() == move_actor.lower()),
    None,
)
if snap_quote is None:
    fail("the moved owner has no live menu quote before its path quote")
selection_rows = re.findall(
    r"campaign outside-plane move: selected by live widget 0 actor 0x([0-9A-Fa-f]+) "
    r"-> 0x([0-9A-Fa-f]+)",
    text,
)
if not any(new_key.lower() == snap_quote.group(1).lower()
           for _old_key, new_key in selection_rows):
    fail("widget 0 did not select the actor whose Rifle was quoted")
if (int(quote_min_x), int(quote_max_x), int(quote_min_y), int(quote_max_y)) != (
    plane_min_x, plane_max_x, plane_min_y, plane_max_y
):
    fail("move quote used a different footprint from the live layout")
if not (plane_min_x <= int(sx) <= plane_max_x and plane_min_y <= int(sy) <= plane_max_y):
    fail("the quoted owner did not start inside the plane")
if plane_min_x <= int(dx) <= plane_max_x and plane_min_y <= int(dy) <= plane_max_y:
    fail("the quoted destination is inside the plane")
if int(path_cost) <= 0 or int(snap_cost) <= 0 or int(path_cost) + int(snap_cost) > int(tu_before):
    fail("the live path plus Snap quote did not fit current TU")
if int(target_layer) != int(dz):
    fail("the path quote target layer differs from the live clicked cell")
if snap_quote.group(1).lower() != move_actor.lower() \
        or snap_quote.group(6) != snap_cost \
        or snap_quote.group(10) != tu_before:
    fail("the move did not use the selected owner's live Snap quote")
layer_scans = re.findall(
    r"^pocketufo: campaign outside-plane layer scan: layer (\d+) pixels (\d+) near (\d+) floor (\d+) "
    r"reachable (\d+) minPathCost (-?\d+) minAt (-?\d+)/(-?\d+)/(-?\d+) "
    r"budgetRejected (\d+) maxPathCost (\d+) picks (\d+)$",
    text,
    re.MULTILINE,
)
if not any(int(row[0]) == int(target_layer) and int(row[3]) > 0
           and int(row[4]) > 0 and int(row[11]) > 0
           and int(path_cost) <= int(row[10])
           for row in layer_scans):
    fail("the chosen target layer has no measured outside floor/path candidate within the Snap budget")

if done_actor.lower() != move_actor.lower() or int(done_path) != int(path_cost) \
        or int(done_snap) != int(snap_cost) or int(done_tu_before) != int(tu_before) \
        or int(done_tu_after) >= int(done_tu_before) or int(queue_len) != 0 \
        or int(queue_active) != 0:
    fail("the real move differed from its quote or did not spend TU")

owner_pattern = re.compile(
    r"campaign outside-plane owner: actor 0x([0-9A-Fa-f]+) roster (\d+) "
    r"weaponKey 0x([0-9A-Fa-f]+) itemIndex (\d+) ammo (\d+) "
    r"start (\d+)/(\d+)/(\d+) end (\d+)/(\d+)/(\d+) "
    r"pathCost (\d+) snapCost (\d+) tu (\d+)->(\d+) hp (\d+)->(\d+)",
    re.MULTILINE,
)
owner_matches = list(owner_pattern.finditer(text))
owner = next(
    (
        match for match in owner_matches
        if match.start() > move_done.start()
        and match.group(1).lower() == done_actor.lower()
        and int(match.group(2)) == int(move_slot)
        and tuple(map(int, match.groups()[5:8])) == done_start
        and tuple(map(int, match.groups()[8:11])) == done_end
        and int(match.group(12)) == int(done_path)
        and int(match.group(13)) == int(done_snap)
        and int(match.group(14)) == int(done_tu_before)
        and int(match.group(15)) == int(done_tu_after)
    ),
    None,
)
if owner is None:
    fail("no later owner record matches the successful actor, roster, path, TU, and XYZ move")
print("ok: a later owner record matches the successful pointer move")
owner_rows = re.findall(
    r"^pocketufo: campaign outside-plane owner: actor 0x([0-9A-Fa-f]+) roster (\d+) "
    r"weaponKey 0x([0-9A-Fa-f]+)",
    text,
    re.MULTILINE,
)
if not owner_rows:
    fail("no moved outside-plane Rifle owner was logged")
owner_keys = {row[0].lower() for row in owner_rows}
if owner.group(1).lower() != move_actor.lower() or int(owner.group(2)) != int(move_slot):
    fail("the post-move owner or roster slot changed")
if snap_quote.group(3).lower() != owner.group(3).lower() \
        or snap_quote.group(4) != owner.group(4) \
        or snap_quote.group(8) != owner.group(5):
    fail("the quoted Rifle key, item index, or ammo changed before movement")
owner_end_x, owner_end_y = int(owner.group(9)), int(owner.group(10))
if plane_min_x <= owner_end_x <= plane_max_x and plane_min_y <= owner_end_y <= plane_max_y:
    fail("the measured move left its owner inside the plane")
if int(owner.group(12)) != int(path_cost) or int(owner.group(13)) != int(snap_cost):
    fail("the owner record changed the measured path or Snap cost")

fire_before_pattern = re.compile(
    r"^pocketufo: campaign fire outside-plane before: actorKey 0x([0-9A-Fa-f]+) "
    r"roster (\d+) cell (\d+)/(\d+)/(\d+) weaponKey 0x([0-9A-Fa-f]+) rifle 4 "
    r"itemIndex (\d+) footprint (\d+)\.\.(\d+)/(\d+)\.\.(\d+) "
    r"targetKey 0x([0-9A-Fa-f]+) requested (\d+)/(\d+)/(\d+) queue (\d+) "
    r"ammo (\d+) tu (\d+) targetHp (-?\d+)$",
    re.MULTILINE,
)
fire_before_matches = list(fire_before_pattern.finditer(text))
fire_before = [match.groups() for match in fire_before_matches]
fire_after = re.findall(
    r"^pocketufo: campaign fire outside-plane after: actorKey 0x([0-9A-Fa-f]+) "
    r"cell (\d+)/(\d+)/(\d+) targetKey 0x([0-9A-Fa-f]+) targetHpBefore (-?\d+) "
    r"targetHpAfter (-?\d+) targetLive (\d+) ammo (\d+)->(\d+) tu (\d+)->(\d+) "
    r"queue (\d+)/(\d+) result (?:hp-loss|no-hp-loss)$",
    text,
    re.MULTILINE,
)
outside_target_decision_pattern = re.compile(
    r"^pocketufo: campaign outside-plane fire decision: phase (precheck|commit) "
    r"actor 0x([0-9A-Fa-f]+) roster (\d+) target 0x([0-9A-Fa-f]+) targetRoster (-?\d+) "
    r"cell (-?\d+)/(-?\d+)/(-?\d+) distance (-?\d+) range ([01]) los (-1|[01]) "
    r"visible ([01]) selected ([01]) actionGate ([01]) reason ([a-z_]+)$",
    re.MULTILINE,
)
outside_target_decisions = list(outside_target_decision_pattern.finditer(text))
for decision in outside_target_decisions:
    groups = decision.groups()
    if groups[12] == "1" and (groups[9] != "1"
                                  or groups[10] != "1" or groups[11] != "1"
                                  or groups[13] != "1"):
        fail("an outside shot target was selected without native visibility, range, LOS, or input gate")
decision_batches: list[list[re.Match[str]]] = []
current_decisions: list[re.Match[str]] = []
for decision in outside_target_decisions:
    if current_decisions:
        previous = current_decisions[-1]
        same_batch = (
            previous.group(1) == decision.group(1)
            and previous.group(2).lower() == decision.group(2).lower()
            and text[previous.end():decision.start()].strip() == ""
        )
        if not same_batch:
            decision_batches.append(current_decisions)
            current_decisions = []
    current_decisions.append(decision)
if current_decisions:
    decision_batches.append(current_decisions)
for batch in decision_batches:
    eligible = [
        decision for decision in batch
        if decision.group(10) == "1" and decision.group(11) == "1"
        and decision.group(12) == "1" and decision.group(14) == "1"
        and decision.group(15) in ("native_visible", "farther_visible_target")
    ]
    selected = [decision for decision in batch if decision.group(13) == "1"]
    if not eligible:
        if selected:
            fail("an outside fire phase selected a target that failed eligibility")
        continue
    if len(selected) != 1:
        fail("an outside fire phase did not select exactly one eligible visible target")
    closest = min(eligible, key=lambda decision: (int(decision.group(9)),
                                                   int(decision.group(5))))
    if (selected[0].group(4).lower() != closest.group(4).lower()
            or selected[0].group(5) != closest.group(5)
            or selected[0].group(9) != closest.group(9)):
        fail("an outside fire phase did not select its closest eligible visible enemy")
if not fire_before or not fire_after:
    fail("the flow has no completed outside-plane Snap target witness")
for row in fire_before:
    ax, ay = int(row[2]), int(row[3])
    if (int(row[7]), int(row[8]), int(row[9]), int(row[10])) != (
        plane_min_x, plane_max_x, plane_min_y, plane_max_y
    ):
        fail("a fire witness used a different plane footprint")
    if plane_min_x <= ax <= plane_max_x and plane_min_y <= ay <= plane_max_y:
        fail("an inside-plane owner reached the Fire/Snap input path")
before_keys = {(row[0].lower(), row[11].lower()) for row in fire_before}
for before_match, before in zip(fire_before_matches, fire_before):
    actor_key, target_key = before[0].lower(), before[11].lower()
    target_xyz = tuple(map(int, before[12:15]))
    matching_decisions = [
        decision for decision in outside_target_decisions
        if decision.start() < before_match.start()
        and decision.group(1) == "commit" and decision.group(13) == "1"
        and decision.group(2).lower() == actor_key
        and decision.group(3) == before[1]
        and decision.group(4).lower() == target_key
        and tuple(map(int, decision.groups()[5:8])) == target_xyz
    ]
    if not matching_decisions:
        fail("an outside shot request has no earlier selected native-visible target decision")
    decision = matching_decisions[-1]
    if (decision.group(9) == "-1" or decision.group(10) != "1"
            or decision.group(11) != "1" or decision.group(12) != "1"
            or decision.group(14) != "1"):
        fail("an outside shot request was not bound to a clear native visibility and action gate")
if not any(row[0].lower() in owner_keys for row in fire_before):
    fail("no moved outside-plane Rifle owner entered the Snap path")
for row in fire_after:
    ax, ay = int(row[1]), int(row[2])
    if plane_min_x <= ax <= plane_max_x and plane_min_y <= ay <= plane_max_y:
        fail("a completed shot owner was inside the plane")
if not any((row[0].lower(), row[4].lower()) in before_keys for row in fire_after):
    fail("no completed shot matched a live outside-plane Fire request")
print(f"ok: {len(fire_before)} outside-plane Snap requests and {len(fire_after)} completed shot records")

need(text, r"campaign natural-fight-outside-plane: PASS",
     "the new bounded driver completed on native victory")
win = need(
    text,
    r"campaign natural-fight outcome: victory after .*screen 13, xcom ([1-9]\d*), enemy \d+, "
    r"reserve 0, missionVictory 1",
    "the game opened its own screen-13 win with the victory byte set",
)
bounded = need(text, r"campaign natural-fight: PASS turns (\d+) actions (\d+)",
               "the natural fight reported its turn and action counts")
turns, actions = map(int, bounded.groups())
if turns > 12 or actions > 160 or actions < 2:
    fail(f"fight exceeded its budget or omitted preparation: turns={turns}, actions={actions}")

approach_goal_pattern = re.compile(
    r"^pocketufo: campaign outside-plane approach goal: actor 0x([0-9A-Fa-f]+) "
    r"cell (\d+)/(\d+)/(\d+) target 0x([0-9A-Fa-f]+) roster (\d+) "
    r"cell (\d+)/(\d+)/(\d+) distance (\d+) metric chebyshev "
    r"reason (no_visible_target|unaffordable|no_visible_and_unaffordable|native_menu_gate)$",
    re.MULTILINE,
)
approach_candidate_pattern = re.compile(
    r"^pocketufo: campaign outside-plane approach candidate: actor 0x([0-9A-Fa-f]+) "
    r"target 0x([0-9A-Fa-f]+) roster (\d+) cell (\d+)/(\d+)/(\d+) "
    r"distance (\d+) live 1 selected ([01])$",
    re.MULTILINE,
)
approach_move_pattern = re.compile(
    r"^pocketufo: campaign outside-plane approach move: actor 0x([0-9A-Fa-f]+) "
    r"targetPlan 0x([0-9A-Fa-f]+) targetPlanCell (\d+)/(\d+)/(\d+) "
    r"targetNow 0x([0-9A-Fa-f]+) targetNowLive ([01]) targetNowCell (-?\d+)/(-?\d+)/(-?\d+) "
    r"goal (\d+)/(\d+)/(\d+) goalPointerMatch 1 "
    r"start (\d+)/(\d+)/(\d+) end (\d+)/(\d+)/(\d+) "
    r"distance (\d+)->(\d+) pathCost (\d+) tu (\d+)->(\d+) debit (\d+) "
    r"targetHp (\d+)->(-?\d+) queue (\d+)/(\d+) outside 1$",
    re.MULTILINE,
)
approach_partial_pattern = re.compile(
    r"^pocketufo: campaign outside-plane approach partial: actor 0x([0-9A-Fa-f]+) "
    r"targetPlan 0x([0-9A-Fa-f]+) targetPlanCell (\d+)/(\d+)/(\d+) "
    r"targetNow 0x([0-9A-Fa-f]+) targetNowLive ([01]) targetNowCell (-?\d+)/(-?\d+)/(-?\d+) "
    r"goal (\d+)/(\d+)/(\d+) goalPointerMatch (-1|[01]) "
    r"start (\d+)/(\d+)/(\d+) end (-?\d+)/(-?\d+)/(-?\d+) "
    r"planDistance (\d+)->(\d+) pathCost (\d+) tu (\d+)->(-?\d+) debit (-?\d+) "
    r"queue (-?\d+)/(-?\d+) outsideEndpoint ([01]) reason ([a-z_]+)$",
    re.MULTILINE,
)
approach_gate_pattern = re.compile(
    r"^pocketufo: campaign outside-plane approach input gate: actor 0x([0-9A-Fa-f]+) "
    r"target 0x([0-9A-Fa-f]+) goal (\d+)/(\d+)/(\d+) fall (\d+) fire (\d+) "
    r"throw (\d+) queueActive (\d+) queueLen (\d+) screen (\d+) modal (\d+) "
    r"player (\d+) latch (\d+) busy (\d+)$",
    re.MULTILINE,
)
approach_action_pattern = re.compile(
    r"^pocketufo: campaign natural-fight-outside-plane: approach counted status "
    r"(moved|partial|interrupted) actions (\d+)/160 turn (\d+)/(\d+) "
    r"turnsRemain (\d+) actionsRemain (\d+) target 0x([0-9A-Fa-f]+) roster (\d+)$",
    re.MULTILINE,
)
approach_fire_summary_pattern = re.compile(
    r"^pocketufo: campaign fire: no usable outside action after "
    r"(?:pinned owner|bounded rifle) search tried \d+/\d+ actors "
    r"noLoadedRifle \d+ noAffordableMode (\d+) noVisibleTarget (\d+) "
    r"reason (no_visible_target|unaffordable|no_visible_and_unaffordable|native_menu_gate)$",
    re.MULTILINE,
)
approach_goals = list(approach_goal_pattern.finditer(text))
approach_moves = list(approach_move_pattern.finditer(text))
approach_partials = list(approach_partial_pattern.finditer(text))
approach_candidates = list(approach_candidate_pattern.finditer(text))
approach_gates = list(approach_gate_pattern.finditer(text))
approach_actions = list(approach_action_pattern.finditer(text))
approach_summaries = list(approach_fire_summary_pattern.finditer(text))
approach_action_counts: set[int] = set()
for move in approach_moves:
    move_actor, target_key = move.group(1).lower(), move.group(2).lower()
    target_xyz = tuple(map(int, move.groups()[2:5]))
    target_now_key = move.group(6).lower()
    target_now_live = move.group(7)
    target_now_xyz = tuple(map(int, move.groups()[7:10]))
    goal_xyz = tuple(map(int, move.groups()[10:13]))
    start_xyz = tuple(map(int, move.groups()[13:16]))
    end_xyz = tuple(map(int, move.groups()[16:19]))
    distance_before, distance_after = map(int, move.groups()[19:21])
    path_cost, tu_before, tu_after, debit = map(int, move.groups()[21:25])
    hp_before, hp_after, queue_len, queue_active = map(int, move.groups()[25:29])
    if (distance_after >= distance_before or path_cost <= 0 or path_cost > tu_before
            or tu_after >= tu_before or debit != tu_before - tu_after or debit <= 0
            or hp_before <= 0 or queue_len != 0 or queue_active != 0
            or start_xyz == end_xyz or goal_xyz != end_xyz
            or plane_min_x <= end_xyz[0] <= plane_max_x
            and plane_min_y <= end_xyz[1] <= plane_max_y):
        fail("an outside approach move missed its selected XYZ goal or lacked a real affordable debit")
    if target_now_live == "1":
        if (target_now_key == "0x0" or target_now_key != target_key
                or min(target_now_xyz) < 0 or hp_after <= 0):
            fail("a live post-move target key differs from its planned key or lacks cell/HP")
    elif (target_now_key != "0x0" or target_now_xyz != (-1, -1, -1) or hp_after != -1):
        fail("a removed post-move target has non-live fields")
    goals = [
        goal for goal in approach_goals
        if goal.start() < move.start()
        and goal.group(1).lower() == move_actor
        and goal.group(5).lower() == target_key
        and tuple(map(int, goal.groups()[6:9])) == target_xyz
        and tuple(map(int, goal.groups()[1:4])) == start_xyz
        and int(goal.group(10)) == distance_before
    ]
    if not goals:
        fail("an outside approach move has no earlier matching live enemy goal")
    goal = goals[-1]
    summaries = [summary for summary in approach_summaries if summary.start() < goal.start()]
    if not summaries:
        fail("an outside approach goal has no preceding failed fire-action search")
    fire_summary = summaries[-1]
    no_affordable, no_visible = int(fire_summary.group(1)), int(fire_summary.group(2))
    reason = fire_summary.group(3)
    if (goal.group(11) != reason
            or (reason == "no_visible_target" and no_visible <= 0)
            or (reason == "unaffordable" and no_affordable <= 0)
            or (reason == "no_visible_and_unaffordable"
                and (no_visible <= 0 or no_affordable <= 0))):
        fail("an outside approach goal does not match its failed fire-action reason")
    candidates = [
        candidate for candidate in approach_candidates
        if fire_summary.end() < candidate.start() < goal.start()
        and candidate.group(1).lower() == move_actor
    ]
    if not candidates:
        fail("an outside approach goal has no live candidate ranking")
    closest = min(candidates, key=lambda candidate: (int(candidate.group(7)), int(candidate.group(3))))
    if (closest.group(2).lower() != target_key
            or tuple(map(int, closest.groups()[3:6])) != target_xyz
            or int(closest.group(7)) != int(goal.group(10))
            or closest.group(8) != "1"):
        fail("the approach goal did not select the closest live enemy")
    if sum(candidate.group(8) == "1" for candidate in candidates) != 1:
        fail("the approach candidate list did not mark exactly one target")
    gates = [
        gate for gate in approach_gates
        if goal.start() < gate.start() < move.start()
        and gate.group(1).lower() == move_actor
        and gate.group(2).lower() == target_key
        and tuple(map(int, gate.groups()[2:5])) == goal_xyz
    ]
    if not gates or tuple(map(int, gates[-1].groups()[5:])) != (
        0, 0, 0, 0, 0, 0, 0, 1, 0, 0
    ):
        fail("an outside approach click has no clear live Input_OnAction gate witness")
    next_goal_start = min(
        (next_goal.start() for next_goal in approach_goals if next_goal.start() > goal.start()),
        default=len(text),
    )
    actions_for_move = [
        action for action in approach_actions
        if move.end() < action.start() < next_goal_start
        and action.group(7).lower() == target_key
        and int(action.group(8)) == int(goal.group(6))
    ]
    if len(actions_for_move) != 1:
        fail("an outside approach move does not have one shared action-budget record")
    action = actions_for_move[0]
    if action.group(1) != "moved":
        fail("a complete approach endpoint has a non-moved action status")
    action_count, action_turn, action_max_turns, turns_remain, actions_remain = map(
        int, action.groups()[1:6]
    )
    if (action_count in approach_action_counts or action_count > actions
            or action_turn > turns or action_max_turns <= 0 or action_max_turns > 12
            or action_turn >= action_max_turns
            or turns_remain != action_max_turns - action_turn
            or actions_remain != 160 - action_count):
        fail("an outside approach move exceeded or detached from the shared natural-fight budget")
    approach_action_counts.add(action_count)
for partial in approach_partials:
    actor_key, target_key = partial.group(1).lower(), partial.group(2).lower()
    target_xyz = tuple(map(int, partial.groups()[2:5]))
    target_now_key = partial.group(6).lower()
    target_now_live = partial.group(7)
    target_now_xyz = tuple(map(int, partial.groups()[7:10]))
    goal_xyz = tuple(map(int, partial.groups()[10:13]))
    goal_pointer_match = partial.group(14)
    start_xyz = tuple(map(int, partial.groups()[14:17]))
    end_xyz = tuple(map(int, partial.groups()[17:20]))
    distance_before, distance_after = map(int, partial.groups()[20:22])
    path_cost, tu_before, tu_after, debit = map(int, partial.groups()[22:26])
    queue_len, queue_active = map(int, partial.groups()[26:28])
    outside_endpoint = partial.group(29)
    reason = partial.group(30)
    if reason != "endpoint_mismatch":
        fail(f"an outside approach stopped on unresolved interrupt reason {reason}")
    if ((goal_pointer_match == "1" and end_xyz == goal_xyz)
            or min(end_xyz) < 0 or distance_after >= distance_before
            or path_cost <= 0 or path_cost > tu_before or tu_after >= tu_before
            or debit != tu_before - tu_after or debit <= 0
            or queue_len != 0 or queue_active != 0
            or outside_endpoint != ("0" if plane_min_x <= end_xyz[0] <= plane_max_x
                                    and plane_min_y <= end_xyz[1] <= plane_max_y else "1")):
        fail("a partial approach record lacks an interrupted endpoint or live TU/queue proof")
    if target_now_live == "1":
        if (target_now_key == "0x0" or target_now_key != target_key
                or min(target_now_xyz) < 0):
            fail("a live partial-approach target key differs from its planned key or lacks cell")
    elif target_now_key != "0x0" or target_now_xyz != (-1, -1, -1):
        fail("a removed target after partial approach has non-live fields")
    goals = [
        goal for goal in approach_goals
        if goal.start() < partial.start()
        and goal.group(1).lower() == actor_key
        and goal.group(5).lower() == target_key
        and tuple(map(int, goal.groups()[6:9])) == target_xyz
        and tuple(map(int, goal.groups()[1:4])) == start_xyz
        and int(goal.group(10)) == distance_before
    ]
    if not goals:
        fail("a partial approach has no earlier matching planned target and route goal")
    goal = goals[-1]
    summaries = [summary for summary in approach_summaries if summary.start() < goal.start()]
    if not summaries:
        fail("a partial approach goal has no preceding failed fire-action search")
    fire_summary = summaries[-1]
    no_affordable, no_visible = int(fire_summary.group(1)), int(fire_summary.group(2))
    summary_reason = fire_summary.group(3)
    if (goal.group(11) != summary_reason
            or (summary_reason == "no_visible_target" and no_visible <= 0)
            or (summary_reason == "unaffordable" and no_affordable <= 0)
            or (summary_reason == "no_visible_and_unaffordable"
                and (no_visible <= 0 or no_affordable <= 0))):
        fail("a partial approach goal does not match its failed fire-action reason")
    candidates = [
        candidate for candidate in approach_candidates
        if fire_summary.end() < candidate.start() < goal.start()
        and candidate.group(1).lower() == actor_key
    ]
    if not candidates:
        fail("a partial approach goal has no live candidate ranking")
    closest = min(candidates, key=lambda candidate: (int(candidate.group(7)), int(candidate.group(3))))
    if (closest.group(2).lower() != target_key
            or tuple(map(int, closest.groups()[3:6])) != target_xyz
            or int(closest.group(7)) != int(goal.group(10))
            or closest.group(8) != "1"
            or sum(candidate.group(8) == "1" for candidate in candidates) != 1):
        fail("a partial approach did not bind the closest live enemy")
    gates = [
        gate for gate in approach_gates
        if goal.start() < gate.start() < partial.start()
        and gate.group(1).lower() == actor_key
        and gate.group(2).lower() == target_key
        and tuple(map(int, gate.groups()[2:5])) == goal_xyz
    ]
    if not gates or tuple(map(int, gates[-1].groups()[5:])) != (
        0, 0, 0, 0, 0, 0, 0, 1, 0, 0
    ):
        fail("a partial approach click has no clear live Input_OnAction gate witness")
    next_goal_start = min(
        (next_goal.start() for next_goal in approach_goals if next_goal.start() > goal.start()),
        default=len(text),
    )
    actions_for_partial = [
        action for action in approach_actions
        if partial.end() < action.start() < next_goal_start
        and action.group(7).lower() == target_key
        and int(action.group(8)) == int(goal.group(6))
    ]
    if len(actions_for_partial) != 1 or actions_for_partial[0].group(1) != "partial":
        fail("a partial approach lacks one counted partial action record")
    action = actions_for_partial[0]
    action_count, action_turn, action_max_turns, turns_remain, actions_remain = map(
        int, action.groups()[1:6]
    )
    if (action_count in approach_action_counts or action_count > actions
            or action_turn > turns or action_max_turns <= 0 or action_max_turns > 12
            or action_turn >= action_max_turns
            or turns_remain != action_max_turns - action_turn
            or actions_remain != 160 - action_count):
        fail("a partial approach exceeded or detached from the shared natural-fight budget")
    approach_action_counts.add(action_count)
if len(approach_actions) != len(approach_moves) + len(approach_partials):
    fail("an outside approach action record has no matching live movement")
print(f"ok: {len(approach_moves)} full and {len(approach_partials)} partial outside approach actions were bound")

prep_move_pattern = re.compile(
    r"^pocketufo: campaign outside-plane prep move: seq (\d+) actor 0x([0-9A-Fa-f]+) "
    r"roster (\d+) goal (\d+)/(\d+)/(\d+) goalCost (\d+) pathSteps (\d+) "
    r"prefix (\d+)/(\d+)/(\d+) prefixCost (\d+) prefixSteps (\d+) snapReserve (\d+) "
    r"start (\d+)/(\d+)/(\d+) end (\d+)/(\d+)/(\d+) "
    r"tu (\d+)->(\d+) debit (\d+) pathMatch (\d+) queue (\d+)/(\d+) busy (\d+) "
    r"inside (\d+) outside (\d+) postGates (\d+)/(\d+)/(\d+) "
    r"postScreen (\d+) postModal (\d+) postPlayer (\d+) "
    r"turn (\d+) actionsBefore (\d+)/160 "
    r"turnsRemain (\d+) actionsRemain (\d+)$",
    re.MULTILINE,
)
prep_moves = list(prep_move_pattern.finditer(text))
if not prep_moves:
    fail("the outside-plane flow has no counted, live prefix move")
prep_route_pattern = re.compile(
    r"^pocketufo: campaign outside-plane prep route: seq (\d+) actor 0x([0-9A-Fa-f]+) "
    r"roster (\d+) start (\d+)/(\d+)/(\d+) goal (\d+)/(\d+)/(\d+) "
    r"goalCost (\d+) pathSteps (\d+) tu (\d+) snapReserve (\d+)$",
    re.MULTILINE,
)
prep_routes = list(prep_route_pattern.finditer(text))
prep_no_affordable_pattern = re.compile(
    r"^pocketufo: campaign outside-plane move: FAIL no loaded Rifle 4 owner has an "
    r"affordable live path outside the footprint(?: \(roster cycle\))?$",
    re.MULTILINE,
)
prep_no_affordable = list(prep_no_affordable_pattern.finditer(text))

prep_gate_pattern = re.compile(
    r"^pocketufo: campaign outside-plane prep input gate: seq (\d+) actor 0x([0-9A-Fa-f]+) "
    r"fall (\d+) fire (\d+) throw (\d+) queueActive (\d+) queueLen (\d+) "
    r"screen (\d+) modal (\d+) player (\d+) latch (\d+) busy (\d+)$",
    re.MULTILINE,
)
prep_gates = list(prep_gate_pattern.finditer(text))
prep_projection_pattern = re.compile(
    r"^pocketufo: campaign outside-plane prep projection layer: seq (\d+) actor 0x([0-9A-Fa-f]+) "
    r"cell (\d+)/(\d+)/(\d+) viewLayer (-?\d+) pen (\d+)/(\d+)$",
    re.MULTILINE,
)
prep_projections = list(prep_projection_pattern.finditer(text))
prep_action_pattern = re.compile(
    r"^pocketufo: campaign outside-plane prep action: seq (\d+) actions (\d+)/160 "
    r"turn (\d+)/(\d+) turnsRemain (\d+) actionsRemain (\d+)$",
    re.MULTILINE,
)
prep_actions = list(prep_action_pattern.finditer(text))
prep_turn_pattern = re.compile(
    r"^pocketufo: campaign outside-plane prep turn: seq (\d+) turn (\d+) "
    r"actions (\d+)/160 turnsRemain (\d+) actionsRemain (\d+) witness 1 "
    r"playerInput (\d+) enemyActive (\d+) screen (\d+) terminal (\d+) "
    r"resumeActor 0x([0-9A-Fa-f]+) priorTuAfter (\d+) resumeSlot (-?\d+) "
    r"resumeActive (\d+) resumeCell (-?\d+)/(-?\d+)/(-?\d+) "
    r"resumeTu (\d+) resumeHp (\d+) resumeStatus (\d+) queue (-?\d+)/(-?\d+)$",
    re.MULTILINE,
)
prep_turns = list(prep_turn_pattern.finditer(text))
prep_enemy_start_pattern = re.compile(
    r"^pocketufo: campaign enemyturn witness: start active (\d+) busy (\d+) input (\d+) "
    r"phase (-?\d+) phaseCounter (-?\d+) aiTickChanged (\d+)$",
    re.MULTILINE,
)
prep_enemy_progress_pattern = re.compile(
    r"^pocketufo: campaign enemyturn witness: (?:complete|terminal screen (?:13|18)) "
    r"steps \d+ callbacks (\d+) aiTickChanged (\d+) turnProgress 1",
    re.MULTILINE,
)

prep_records: list[tuple[re.Match[str], re.Match[str], re.Match[str]]] = []
prep_route_by_sequence: dict[int, re.Match[str]] = {}
seen_prep_sequences: set[int] = set()
last_prep_sequence = 0
last_prep_turn_end = 0
for prep_move in prep_moves:
    (
        seq_s, actor_key, roster_s,
        goal_x_s, goal_y_s, _goal_z_s, goal_cost_s, path_steps_s,
        prefix_x_s, prefix_y_s, prefix_z_s, prefix_cost_s, prefix_steps_s, snap_reserve_s,
        start_x_s, start_y_s, start_z_s, end_x_s, end_y_s, end_z_s,
        tu_before_s, tu_after_s, debit_s, path_match_s, queue_len_s, queue_active_s,
        busy_s, inside_s, outside_s, post_fall_s, post_fire_s, post_throw_s,
        post_screen_s, post_modal_s, post_player_s, turn_before_s, actions_before_s,
        turns_rem_before_s, actions_rem_before_s,
    ) = prep_move.groups()
    seq = int(seq_s)
    tu_before, tu_after, debit = map(int, (tu_before_s, tu_after_s, debit_s))
    turn_before, actions_before = map(int, (turn_before_s, actions_before_s))
    if seq in seen_prep_sequences:
        fail(f"prefix action sequence {seq} was logged more than once")
    if seq <= last_prep_sequence:
        fail("prefix action records are not in increasing run order")
    seen_prep_sequences.add(seq)
    last_prep_sequence = seq
    if (int(goal_cost_s) <= int(prefix_cost_s) or int(path_steps_s) <= int(prefix_steps_s)
            or int(prefix_steps_s) <= 0 or int(prefix_cost_s) <= 1
            or int(prefix_cost_s) > tu_before or snap_reserve_s != "0"):
        fail(f"prefix {seq} is not a strict affordable path prefix with no Snap reserve")
    if (plane_min_x <= int(goal_x_s) <= plane_max_x
            and plane_min_y <= int(goal_y_s) <= plane_max_y):
        fail(f"prefix {seq} goal is inside the craft footprint")
    if not (plane_min_x <= int(start_x_s) <= plane_max_x
            and plane_min_y <= int(start_y_s) <= plane_max_y
            and plane_min_x <= int(prefix_x_s) <= plane_max_x
            and plane_min_y <= int(prefix_y_s) <= plane_max_y):
        fail(f"prefix {seq} did not start and end inside the craft footprint")
    if (int(start_x_s), int(start_y_s), int(start_z_s)) == (
        int(end_x_s), int(end_y_s), int(end_z_s)
    ) or (int(prefix_x_s), int(prefix_y_s), int(prefix_z_s)) != (
        int(end_x_s), int(end_y_s), int(end_z_s)
    ):
        fail(f"prefix {seq} did not move to its selected endpoint")
    if (path_match_s != "1" or queue_len_s != "0" or queue_active_s != "0"
            or busy_s != "0" or inside_s != "1" or outside_s != "0"
            or (post_fall_s, post_fire_s, post_throw_s) != ("0", "0", "0")
            or post_screen_s != "0" or post_modal_s != "0" or post_player_s != "1"):
        fail(f"prefix {seq} lacks its live route, drained queue, or inside-plane proof")
    if debit != tu_before - tu_after or debit <= 0 or tu_after >= tu_before:
        fail(f"prefix {seq} did not record a positive live TU debit")
    if (turn_before >= 12 or actions_before >= 158 or seq > actions
            or seq != actions_before + 1
            or int(turns_rem_before_s) != 12 - turn_before
            or int(actions_rem_before_s) != 160 - actions_before):
        fail(f"prefix {seq} did not use the shared turn/action budget")
    route_matches = [
        match for match in prep_routes
        if match.start() < prep_move.start()
        and match.group(1) == seq_s
        and match.group(2).lower() == actor_key.lower()
        and tuple(map(int, match.groups()[3:6]))
            == (int(start_x_s), int(start_y_s), int(start_z_s))
        and tuple(map(int, match.groups()[6:9]))
            == (int(goal_x_s), int(goal_y_s), int(_goal_z_s))
        and int(match.group(10)) == int(goal_cost_s)
        and int(match.group(11)) == int(path_steps_s)
        and int(match.group(12)) == tu_before
        and match.group(13) == "0"
    ]
    if not route_matches:
        fail(f"prefix {seq} has no matching recomputed full goal route")
    prep_route = route_matches[-1]
    prep_route_by_sequence[seq] = prep_route
    if not any(last_prep_turn_end < match.start() < prep_route.start()
               for match in prep_no_affordable):
        fail(f"prefix {seq} has no current failed full Snap-budget search before its prep route")

    projections = [
        match for match in prep_projections
        if prep_route.start() < match.start() < prep_move.start()
        and match.group(1) == seq_s
        and match.group(2).lower() == actor_key.lower()
    ]
    if not projections:
        fail(f"prefix {seq} has no live pen projection for its selected endpoint")
    projection = projections[-1]
    if (tuple(map(int, projection.groups()[2:5]))
            != (int(prefix_x_s), int(prefix_y_s), int(prefix_z_s))
            or int(projection.group(6)) != int(prefix_z_s)):
        fail(f"prefix {seq} projected a different cell or view layer than its route")

    gates = [
        match for match in prep_gates
        if match.start() < prep_move.start()
        and match.group(1) == seq_s
        and match.group(2).lower() == actor_key.lower()
    ]
    if not gates:
        fail(f"prefix {seq} has no pre-click Input_OnAction gate witness")
    gate = gates[-1]
    if projection.start() > gate.start():
        fail(f"prefix {seq} did not guard the projected cell immediately before click")
    if tuple(map(int, gate.groups()[2:])) != (0, 0, 0, 0, 0, 0, 0, 1, 0, 0):
        fail(f"prefix {seq} clicked while a raw input, modal, latch, or queue gate was set")

    action_matches = [
        match for match in prep_actions
        if match.start() > prep_move.end() and match.group(1) == seq_s
    ]
    if len(action_matches) != 1:
        fail(f"prefix {seq} does not have exactly one shared action count")
    action = action_matches[0]
    action_count, action_turn, action_max_turns, action_turns_rem, action_rem = map(
        int, action.groups()[1:]
    )
    if (action_count != seq or action_turn != turn_before or action_max_turns > 12
            or action_turns_rem != action_max_turns - action_turn
            or action_rem != 160 - action_count):
        fail(f"prefix {seq} action count differs from its shared budget record")

    turn_matches = [
        match for match in prep_turns
        if match.start() > action.end() and match.group(1) == seq_s
    ]
    if len(turn_matches) != 1:
        fail(f"prefix {seq} has no unique witnessed End Turn after movement")
    prep_turn = turn_matches[0]
    (
        _turn_seq, turn_after_s, turn_actions_s, turn_rem_s, action_rem_s,
        player_input_s, enemy_active_s, screen_s, terminal_s, resume_actor,
        prior_tu_after_s, resume_slot_s, resume_active_s,
        resume_x_s, resume_y_s, resume_z_s, resume_tu_s,
        resume_hp_s, resume_status_s, turn_queue_len_s, turn_queue_active_s,
    ) = prep_turn.groups()
    turn_after, turn_actions = int(turn_after_s), int(turn_actions_s)
    if (turn_after != turn_before + 1 or turn_after > turns
            or turn_actions != action_count or turn_actions > actions
            or int(turn_rem_s) != 12 - turn_after
            or int(action_rem_s) != 160 - turn_actions
            or int(prior_tu_after_s) != tu_after
            or resume_actor.lower() != actor_key.lower()):
        fail(f"prefix {seq} End Turn did not use the same actor and counters")
    if resume_active_s == "1":
        # Casualties compact the side list. Pointer identity above is stable; its slot is not.
        after_state = re.search(
            rf"campaign state natural_fight_enemy_turn_{turn_after}_after:.*? xcom (\d+) ",
            text[action.end():prep_turn.start()],
        )
        if (after_state is None or not 0 <= int(resume_slot_s) < int(after_state.group(1))
                or int(resume_hp_s) <= 0 or resume_status_s == "255"):
            fail(f"prefix {seq} resumed an invalid live owner")
    if resume_active_s == "0" and (
            resume_slot_s != "-1" or resume_tu_s != "0" or resume_hp_s != "0"
            or resume_status_s != "255"
            or (resume_x_s, resume_y_s, resume_z_s) != ("-1", "-1", "-1")):
        fail(f"prefix {seq} marked a lost owner as resumable")
    if terminal_s == "0":
        if (screen_s != "0" or player_input_s != "1" or enemy_active_s != "0"
                or turn_queue_len_s != "0" or turn_queue_active_s != "0"):
            fail(f"prefix {seq} End Turn did not restore idle player input")
    elif terminal_s != "1" or screen_s not in ("13", "18"):
        fail(f"prefix {seq} turn reported an unknown terminal screen")

    turn_region = text[prep_move.end():prep_turn.start()]
    if not any(
        match.group(1) == "1" and match.group(2) == "1"
        and match.group(3) == "0" and match.group(4) == "3"
        for match in prep_enemy_start_pattern.finditer(turn_region)
    ):
        fail(f"prefix {seq} has no observed enemy AI turn edge")
    if not any(
        int(callbacks) > 0 or int(ai_changed) != 0
        for callbacks, ai_changed in prep_enemy_progress_pattern.findall(turn_region)
    ):
        fail(f"prefix {seq} has no real enemy tick or AI progress witness")
    prep_records.append((prep_move, action, prep_turn))
    last_prep_turn_end = prep_turn.end()

if len(prep_moves) > turns:
    fail("prep moves exceeded the shared End Turn budget")
if any(prep_move.start() < move_quote.start() <= prep_turn.end()
       for prep_move, _action, prep_turn in prep_records):
    fail("the first outside path quote came before its preceding prep turn completed")

for index in range(1, len(prep_records)):
    previous_move, _previous_action, previous_turn = prep_records[index - 1]
    current_move, _current_action, _current_turn = prep_records[index]
    previous_fields = previous_turn.groups()
    current_fields = current_move.groups()
    previous_resume_actor = previous_fields[9]
    previous_resume_active = previous_fields[12]
    current_actor = current_fields[1].lower()
    selection_region = text[previous_turn.end():current_move.start()]
    selected_by_widget0 = any(
        old_key.lower() != new_key.lower() and new_key.lower() == current_actor
        for old_key, new_key in re.findall(
            r"campaign outside-plane move: selected by live widget 0 actor "
            r"0x([0-9A-Fa-f]+) -> 0x([0-9A-Fa-f]+)",
            selection_region,
        )
    )
    if current_actor != previous_resume_actor.lower():
        owner_left = any(
            match.start() > previous_turn.end()
            and match.start() < current_move.start()
            and match.group(1).lower() == previous_resume_actor.lower()
            for match in owner_matches
        )
        if previous_resume_active == "1" and not owner_left:
            fail("a live prep owner was replaced before it moved outside")
        if not selected_by_widget0:
            fail("a replacement prep owner was not selected through live widget 0")
    if previous_resume_active == "1" and current_actor == previous_resume_actor.lower():
        previous_resume_tu = int(previous_fields[16])
        previous_resume_cell = tuple(map(int, previous_fields[13:16]))
        current_sequence = int(current_fields[0])
        current_route = prep_route_by_sequence[current_sequence]
        if int(current_route.group(12)) != previous_resume_tu \
                or int(current_fields[20]) != previous_resume_tu:
            fail("the resumed owner TU was not used as the next route budget")
        if tuple(map(int, current_fields[14:17])) != previous_resume_cell:
            fail("the resumed prep owner did not continue from its last live cell")
    elif previous_resume_active == "0":
        if not selected_by_widget0:
            fail("an inactive prep owner was not replaced through live widget 0")

final_resume_tu = [
    int(prep_turn.groups()[16])
    for _prep_move, _prep_action, prep_turn in prep_records
    if prep_turn.end() < move_quote.start()
    and prep_turn.groups()[9].lower() == move_actor.lower()
    and prep_turn.groups()[12] == "1"
]
if final_resume_tu and int(tu_before) != final_resume_tu[-1]:
    fail("the final same-owner move did not use the last live post-turn TU")

turn_start_rows = re.findall(
    r"^pocketufo: campaign enemyturn witness: start active (\d+) busy (\d+) input (\d+) "
    r"phase (-?\d+) phaseCounter (-?\d+) aiTickChanged (\d+)$",
    text,
    re.MULTILINE,
)
turn_starts = [
    row for row in turn_start_rows
    if (row[0] == "1" and row[1] == "1" and row[2] == "0" and row[3] == "3")
    or row[5] == "1"
]
turn_witnesses = re.findall(
    r"campaign enemyturn witness: (?:complete|terminal screen (?:13|18)) steps \d+ "
    r"callbacks (\d+) aiTickChanged (\d+) turnProgress 1",
    text,
)
if len(turn_starts) < turns or len(turn_witnesses) < turns:
    fail(f"only {len(turn_starts)} turn starts and {len(turn_witnesses)} completed tick witnesses "
         f"were logged for {turns} counted turns")
if any(int(callbacks) == 0 and int(ai_tick_changed) == 0
       for callbacks, ai_tick_changed in turn_witnesses):
    fail("a counted turn has no real timer callback or enemy AI tick witness")

need(text, r"campaign close-debrief: victory 1 screen 13 xcom [1-9]\d* enemy \d+ reserve 0",
     "the debrief close saw a native win before input")
victory_debrief_returned = need(
    text, r"campaign close-debrief: PASS normal victory returned to geoscape",
    "native return input restored the geoscape",
)
victory_save_written = need(
    text, r"(?m)^pocketufo: save slot 0: written.*$",
    "slot 0 was written after the native return",
)
victory_saved_state = need(
    text,
    r"(?m)^pocketufo: state victory_saved: start screen 22, list mode \d+, geoscape screen 0, "
    r"geo ui byte 1, tact flag 0, bases [1-9]\d*,.*$",
    "the declared post-save state is the live geoscape",
)
if not (victory_debrief_returned.end() <= victory_save_written.start()
        <= victory_saved_state.start()):
    fail("slot 0 and its state witness are not after the native debrief return")
if not save_path.is_file() or save_path.stat().st_size == 0:
    fail(f"slot-0 save is missing or empty: {save_path}")
print(f"ok: slot-0 save exists ({save_path.stat().st_size} bytes)")


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
if not any(qty > 0 for qty in debrief_craft.values()) \
        and not any(qty > 0 for qty in debrief_site.values()):
    fail("the victory debrief reported no recovered craft or site quantities")
if not any(qty > 0 for qty in returned_craft.values()) \
        and not any(qty > 0 for qty in returned_site.values()):
    fail("the returned geoscape reported no recovered craft or site quantities")
need(text, r"strategy tuple victory_returned:",
     "the post-debrief base and site state was observed")

for name in (
    "victory_outside_before.ppm",
    "victory_outside_debrief.ppm",
    "victory_outside_returned.ppm",
):
    path = out_dir / name
    if not path.is_file() or path.stat().st_size == 0:
        fail(f"expected flow picture is missing or empty: {path}")
    print(f"ok: captured {name} ({path.stat().st_size} bytes)")

print(f"outside-plane victory passed; winning XCOM count {win.group(1)}, turns {turns}, actions {actions}")
