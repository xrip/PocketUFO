#!/usr/bin/env python3
"""Check natural base-defense defeat, real restart input, and basic PPM sanity."""

from __future__ import annotations

import pathlib
import re
import sys


def fail(message: str) -> None:
    print(f"BASE DEFENSE DEFEAT FAILED: {message}", file=sys.stderr)
    raise SystemExit(1)


def need(text: str, pattern: str, label: str) -> re.Match[str]:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        fail(label)
    print(f"ok: {label}")
    return match


def flow_actions(path: pathlib.Path) -> list[str]:
    return [
        line.strip()
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]


def contains_in_order(actions: list[str], wanted: list[str]) -> bool:
    at = 0
    for action in actions:
        if at < len(wanted) and action == wanted[at]:
            at += 1
    return at == len(wanted)


def check_ppm(path: pathlib.Path) -> None:
    if not path.is_file():
        fail(f"picture is missing: {path}")
    data = path.read_bytes()
    header_end = data.find(b"\n255\n")
    if header_end < 0:
        fail(f"picture is not a P6 PPM: {path.name}")
    header = data[:header_end].split()
    if len(header) != 3 or header[0] != b"P6":
        fail(f"picture has a bad P6 header: {path.name}")
    try:
        width, height = int(header[1]), int(header[2])
    except ValueError:
        fail(f"picture has bad dimensions: {path.name}")
    pixels = data[header_end + len(b"\n255\n") :]
    if (width, height) != (240, 320) or len(pixels) != width * height * 3:
        fail(f"picture has wrong dimensions or pixel bytes: {path.name}")
    colours = {pixels[i : i + 3] for i in range(0, len(pixels), 3)}
    visible = sum(
        pixels[i : i + 3] != b"\xff\xff\xff" for i in range(0, len(pixels), 3)
    )
    if len(colours) < 4 or visible < 100:
        fail(f"picture is empty or nearly blank: {path.name}")
    print(f"ok: {path.name} is a nonempty 240x320 P6 frame ({len(colours)} colors)")


if len(sys.argv) != 4:
    fail("usage: check_base_defense_defeat.py LOG COMPOSED_FLOW APP_DIR")

log_path, flow_path, app_dir = map(pathlib.Path, sys.argv[1:])
if not log_path.is_file() or not flow_path.is_file():
    fail("log or composed flow is missing")
text = log_path.read_text(encoding="utf-8", errors="replace")
actions = flow_actions(flow_path)

source_dir = pathlib.Path(__file__).resolve().parent
prefix_path = source_dir / "e2e_flow_base_defense_entry.txt"
continuation_path = source_dir / "e2e_flow_base_defense_defeat.txt"
expected_flow = prefix_path.read_text(encoding="utf-8") + continuation_path.read_text(encoding="utf-8")
if flow_path.read_text(encoding="utf-8") != expected_flow:
    fail("the private flow does not exactly join the current entry flow and continuation")
print("ok: the private flow is the canonical entry flow plus this continuation")

forbidden = re.compile(
    r"^(?:newgame|launch|mindendcheck|mindgoodcheck|mindbankreport|quityes|"
    r"campaign\s+(?:abort|close-debrief|natural-fight|natural-defeat)(?:\s|$)|"
    r"campaign\s+enemyturn\s+grenade)(?:\s|$)",
    re.IGNORECASE,
)
bad_actions = [action for action in actions if forbidden.search(action)]
if bad_actions:
    fail(f"flow contains a shortcut command: {bad_actions[0]}")
if not contains_in_order(
    actions,
    [
        "click 120 124",
        "tick 20",
        "pick",
        "close",
        "close",
        "tick 20",
        "click 60 308",
        "campaign base-defense-entry",
        "click 120 290",
        "confirm",
        "gametick 20",
        "tick 20",
        "campaign enemyturn",
        "campaign natural-defeat-exposed 32",
    ],
):
    fail("entry actions do not reach the natural helper through Base At and real confirm input")
if actions.count("campaign natural-defeat-exposed 32") != 1:
    fail("flow must call the bounded exposed natural-defeat helper exactly once")
if not contains_in_order(
    actions,
    [
        "campaign natural-defeat-exposed 32",
        "tick 160",
        "mindendstate",
        "dump base_defense_bad_ending.ppm",
        "click 100 100",
        "state base_defense_startup_after_loss",
        "click 120 124",
        "state base_defense_new_game_started",
        "tick 20",
        "pick",
        "close",
        "close",
        "tick 20",
        "state base_defense_new_game_placed",
        "strategystate base_defense_new_game_placed",
        "dump base_defense_next_new_game.ppm",
    ],
):
    fail("ending release or fresh-game input sequence is incomplete")
print("ok: flow uses the real ending release and New Game clicks")

need(
    text,
    r"campaign base-defense-entry: PASS Base At screen 20 confirm 1 follows 1 class 2 race 0 targetBase 0",
    "the native subtype-6 event reached Base At through the live clock",
)
need(
    text,
    r"campaign state base_defense_deployed:.*mission 3 .*xcom [1-9]\d* enemy \d+ reserve \d+",
    "the real Base At key path deployed defenders in a type-3 mission",
)
need(
    text,
    r"campaign state base_defense_after_first_enemyturn:.*mission 3 .*xcom [1-9]\d* "
    r"enemy [1-9]\d* reserve \d+",
    "the entry enemy turn returned to a live type-3 roster",
)
need(text, r"pocketufo: step click 120 290", "the Base At panel received a real pointer click")
need(text, r"pocketufo: step confirm", "the map confirm used the real key dispatcher")

start = need(
    text,
    r"campaign natural-defeat-exposed: start maxTurns 32 type 3 baseCount 1 xcom ([1-9]\d*) "
    r"enemy (\d+) reserve (\d+)",
    "the bounded driver started on one live type-3 base-defense mission",
)
if int(start.group(2)) + int(start.group(3)) <= 0:
    fail("natural defeat had no live attacker or reserve")
before = need(
    text,
    r"campaign natural-defeat snapshot turn 1 phase before mission 3 xcom ([1-9]\d*) "
    r"enemy (\d+) reserve (\d+)",
    "the driver captured its first live pre-turn roster snapshot",
)
if int(before.group(2)) + int(before.group(3)) <= 0:
    fail("first snapshot had no live attacker or reserve")

need(
    text,
    r"campaign input: pointer widget 0 at",
    "the exposed driver cycled defenders through the real widget0 input",
)
need(
    text,
    r"campaign natural-defeat movement turn \d+: moved actor 0x[0-9a-fA-F]+ toward enemy \d+",
    "the exposed driver recorded a defender move toward a live enemy",
)
approach = need(
    text,
    r"campaign approach: enemy \d+ from (-?\d+)/(-?\d+)/(-?\d+) to "
    r"(-?\d+)/(-?\d+)/(-?\d+), distance (\d+)->(\d+), pathCost \d+ "
    r"TU (\d+)->(\d+) targetHP \d+",
    "a real map path reduced distance and spent TU without changing target HP",
)
if int(approach.group(7)) <= int(approach.group(8)) or int(approach.group(9)) <= int(approach.group(10)):
    fail("the recorded approach did not reduce distance and TU")
need(
    text,
    r"campaign natural-defeat movement turn 1: moved [1-9]\d* xcom [1-9]\d*->[1-9]\d* hpDrops \d+ removed \d+",
    "the first pre-movement snapshot was compared with a post-movement snapshot",
)

turn_summaries = re.findall(
    r"campaign natural-defeat turn (\d+): xcom (\d+)->(\d+) hpDrops (\d+) removed (\d+) "
    r"enemy (\d+)->(\d+) hpDrops (\d+) removed (\d+) reserve (\d+)->(\d+)",
    text,
)
terminal = need(
    text,
    r"campaign natural-defeat terminal PASS attemptedTurns ([1-9]\d*) mission 3 baseCount 1 "
    r"screen 18 tact 0/0 owners 0/0 battle [1-9]\d* victory 0 endingRows [1-9]\d* backdrop 1 "
    r"preXcom ([1-9]\d*) preEnemy (\d+) preReserve (\d+) observedXcomHpDrops (\d+) "
    r"observedXcomRemoved (\d+)",
    "the real type-3 loss reached screen 18 with no tactical owners and a bad-ending presentation",
)
attempted_turns = int(terminal.group(1))
if attempted_turns > 32:
    fail(f"natural driver exceeded its turn bound: {attempted_turns}")
if int(terminal.group(3)) + int(terminal.group(4)) <= 0:
    fail("terminal pre-turn snapshot had no live attacker or reserve")
if len(turn_summaries) != attempted_turns - 1:
    fail("turn snapshots do not match the number of real End Turn inputs before screen 18")
summary_hp_drops = sum(int(row[3]) for row in turn_summaries)
summary_removals = sum(int(row[4]) for row in turn_summaries)
terminal_removal = re.search(
    r"campaign natural-defeat(?:-exposed)?: terminal (?:during movement|after enemy) "
    r"turn \d+; pre-(?:movement|turn) XCOM removal evidence ([1-9]\d*)",
    text,
)
terminal_removal_count = int(terminal_removal.group(1)) if terminal_removal else 0
if summary_hp_drops != int(terminal.group(5)):
    fail("terminal XCOM HP loss total does not match completed turn snapshots")
if summary_removals + terminal_removal_count != int(terminal.group(6)):
    fail("terminal XCOM removals do not match completed turns and terminal pre-movement evidence")
if attempted_turns > 1:
    saw_loss = any(
        int(after) < int(before_count) or int(hp_drops) > 0 or int(removed) > 0
        for _turn, before_count, after, hp_drops, removed, *_rest in turn_summaries
    )
    if not saw_loss and terminal_removal_count == 0 and int(terminal.group(5)) == 0:
        fail("no natural XCOM HP loss or removal was observed before terminal screen 18")
    print("ok: completed enemy turns recorded natural XCOM HP loss or removal")
else:
    print("ok: screen 18 ended on the first real enemy turn; the pre-turn roster was preserved")

enemy_starts = re.findall(r"campaign enemyturn: started enemy \d+ input 0", text)
if len(enemy_starts) < attempted_turns:
    fail("the log lacks one real End Turn start per natural turn plus the entry probe")
print(f"ok: {len(enemy_starts)} real End Turn inputs were recorded")

ending = need(
    text,
    r"ending state: screen 18/18, rows [1-9]\d*, backdrop 1, scroll \d+, geo 0, "
    r"hold 1, ended 1, tact 0",
    "the read-only ending observer saw the visible bad-ending screen",
)
if ending.start() < terminal.start():
    fail("ending presentation was observed before the natural loss")

release = need(text, r"pocketufo: step click 100 100", "a real pointer click released the ending screen")
startup = need(
    text,
    r"state base_defense_startup_after_loss: start screen 17, .*geo ui byte 0, tact flag 0, bases 1,",
    "the ending release returned to startup screen 17",
)
if startup.start() < release.start():
    fail("startup screen state was printed before the ending release")

new_game_lines = list(re.finditer(r"pocketufo: new game: ([^\n]+)", text))
if len(new_game_lines) < 2:
    fail("the log lacks both the entry New Game and the post-ending New Game")
restart_game = next((m for m in new_game_lines if m.start() > startup.start()), None)
restart_click = need(
    text[startup.start() :], r"pocketufo: step click 120 124", "the startup New Game row received a real click"
)
restart_click_at = startup.start() + restart_click.start()
if restart_game is None or restart_game.start() > restart_click_at:
    fail("the post-ending menu click did not start the normal New Game path")
new_game_state = need(
    text,
    r"state base_defense_new_game_started: start screen 22, .*geoscape screen 2, geo ui byte 1,",
    "the normal New Game click entered a fresh placement world",
)
if not (startup.start() < restart_game.start() < restart_click_at < new_game_state.start()):
    fail("post-ending New Game events are out of order")

placed = need(
    text,
    r"state base_defense_new_game_placed: start screen \d+, .*geoscape screen 0, geo ui byte 1, "
    r"tact flag 0, bases 1, base 0 at (-?\d+)/(-?\d+), soldiers ([1-9]\d*),",
    "the new world has one placed base and live troops",
)
if (int(placed.group(1)), int(placed.group(2))) == (-6000, -6000):
    fail("fresh base placement was not completed")

check_ppm(app_dir / "base_defense_bad_ending.ppm")
check_ppm(app_dir / "base_defense_next_new_game.ppm")
print("base-defense defeat flow passed; PPM checks cover file sanity only, not visual parity")
