# HANDOFF — PocketUFO Win64 recreation

Date: 2026-10-17. HEAD before this edit: `2c3c1cd`. Unit 107 is in progress: it is `Geoscape_Draw`
@ 0x8c180 (33,744 B), the last large gap. This commit carries its eleventh part -- the descent panel at
0x9148C..0x91688 (flag 0x17), with 31 new checks, 17/17 mutations caught and 7 IDB comments.
Parts 1..10 -- the map-view chrome in `5917656`, the 13-arm panel body in `2485d25`, the action-fail line
in `79b7979`, the order questions in `a2d454b`, the craft-detail card in `c5f90b5`, the trace-lost panel
in `4a02d1a`, the attacked-city panel in `ac13d77`, the intercept list in `c612882`, the map-target panel
in `7c14ff8` and the mission-site / travel panels in `e9145c0` + `2c3c1cd` -- are behind this.
What this part found:

- **The IDA MCP client was down, the server was not.** The session cached a failed connection at startup;
  the server on 127.0.0.1:13337 answers JSON-RPC, so the IDB work now goes through a small client script
  (`C:/Windows/Temp/pufo_scripts/idac.py`) that speaks the MCP wire protocol directly. Everything written
  into the IDB was re-read from the ARM first. If Claude Code's own ida-pro-mcp tools come back, prefer
  them; the script is a workaround, not the intended path.
- **Flag 0x17 has ONE path.** No `bMapSelectionFlag` read anywhere in 0x9148C..0x91688. Line = TEXTS[1239]
  "Снижение: " (R6 from the test prologue at 0x91490) + the panel's SELECTED row (0x9151C strcat, row read
  at 0x9150C with R11 = 0xBD8 still sticky from the 0x13 test prologue at 0x90D88), centred at y 70.
- **An empty craft turns a select button into a resolve.** CtlB gets Smap_BeginCraftSelect @ 0x838dc at
  0x91578 before its 0x91580 blit; `Craft_CountSoldiersAboard` on the colour-slot byte then decides, and
  a zero count lets four LDREQ / STREQ at 0x915F8..0x91604 overwrite CtlB+0xC with
  Smap_ResolveInterceptUI -- after the pad is already painted. That order is observable through the blit
  callback (handler at blit time vs handler after the run), which is what the mutation sweep checked.
- **Two dead things in that call.** `abFacilityGrid` at 0x915E8 is the first argument and the callee never
  reads it (0x664A0 loads its own roster at 0x1139B8, 0x664B0 the count from nSoldierCount) -- bugs.tsv
  residue row. And `AND R1,R0,#0xFF` at 0x915E4 changes nothing: UiPanel_InsertLine writes a colour slot
  with `LDRB` of a byte argument into the record's dword slot (0x5A314 / 0x5A318), so slots are 0..255
  before any reader masks them. The first byte test for this was vacuous because of exactly that.
- **This is the third copy of the 0x14 set-arm body** (same nine anim values at 0x914A4..0x914DC, same
  rows 70/102/103/125/126) -- recorded as a duplicate row. The port reuses PUFO_STATUS_TARGET_* for all
  three.

Test hygiene fixes this part needed: the CtlA handler-order check passed on a mutant because `PnCapture`
runs the arm twice and the second run saw the first run's installed handler -- it now nulls both
handlers and runs once; and the mask test was replaced by a real one (a soldier aboard craft 255 counts
for slot 255 and not for slot 4).

Next: 0x9168C (the flag-0x15 test) through 0x91CEC, then the screen arms 0x91DC8..0x944B0. The gate
measures 966/1103 (87.6%) and 596752/598792 (99.7%) with 17154 checks across 60 suites -- that byte
figure credits all 33,744 bytes of `Geoscape_Draw` because `coverage.py` counts a function once its start
address is cited, so it says nothing about how much of that function is ported. The read map and the arm
table are in `docs/parity/game-core.md` under "Geoscape_Draw @ 0x8c180 (33,744 B) - read, ported in
parts".

The unit before it (unit 106, the ufopaedia entry draw) is described under "Test
checks run across the 60 suites" below,
and the per-unit chain
there runs back through every unit since. The block that follows is the
older **`Tact_EnemyAiTick` (0x4cbe0, 3,440 B)** unit's write-up, kept because its four findings still apply
to anyone touching that code:

- **`pTactCtx272+0x100` is a byte, not a pointer.** Hex-Rays types it `Soldier *pSavedFocus` because every read
  is `LOBYTE(pCtx->pSavedFocus)`, but all seven accesses in the binary are 0/1 byte stores and tests (0x4CC00,
  0x4D688, 0x4D84C, 0x4D86C, 0x4D878, 0x4D888, 0x4D920) and nothing ever loads it as an address. The unit the
  enemy turn restores is at +0x104, which is what `Tact_EndEnemyTurn` reads. Now `bAiFocusSaved` (u8 + 3 pad)
  in the IDB struct and in `TactCtx272`, with a layout check that it is one byte and `pSavedSelUnit` sits four
  bytes after it.
- **`g_aUnitTypeRecord` column +3 had no name and exactly one reader** (`LDRB R3,[R3,#3]` at 0x4CDBC — the only
  `+3` read among all 26 xrefs to the table). Its six set rows are Chryssalid, Celacid, both civilians, Silacoid
  and Riper: the units with no weapon to fire and only a melee action, and they are what selects the command-13
  melee branch. Added as `g_aUnitTypeFlag3`. Re-dumping the table also found a wrong row in **this port's own
  documentation**: `pufo_enemyai.h` had copied flag0's 1s at types 11 and 13 into the `large` row; the binary
  says 0 there.
- **The neighbour-melee hand-off is an original bug** (0x4d660). A usable multi-target sends the tick down a
  path that builds the whole action around a *different* enemy — `Tact_SelectUnit(neighbour)`, the command-18
  enqueue and `Map_pSelUnit->uTu >>= 1` (0x4d728..0x4d730) all address the neighbour — while the unit this tick
  was picked for spends no TU, is not marked done, and is picked again next tick. Reproduced, with a test that
  asserts the queue entry's `pUnit` is the neighbour and the actor's TU is untouched.
- **The fire gate charges less than it tests.** `uTu >= anActionCost[mode] * 2/3 + 8.0` (0x4d448
  `0x3FE5555555555555`, 0x4d458 `0x4020000000000000`), but the queue entry carries `(int)(cost * 2/3)`: a
  26-cost mode needs 26 TU and charges 17. Pinned on both sides of the boundary.

The rest of the shape: `nAiTick > 15` skips a unit **without** advancing the counter, so such a unit stays
skipped forever, while `bAiDone` is skipped *after* `++nAiTick`. The melee branch's eight-direction scan and a
large unit's three footprint cells index `apCells` with no `Map_IsValidCell` call at all — unlike the
facing-delta lookup at 0x4ce8c, which does call it — so both are unguarded reads (0x4d09c, 0x4ce50); the port
bounds-checks. The RMP walk increments its index **before** using it (0x4D8F8), so the drawn starting index is
never itself a candidate, and its distance test uses the *untruncated* sqrt (`_led` against 19.875) where every
reach test in the same function truncates with `_dtoi`. `TestRmpWalkOrder` asserts the visit order rather than
just the count. And `unk_156910` / `unk_156A58` / `unk_156A88` turned out to be **`TactItem` records, not pointer
variables**: each has a `TactItem_ctor` thunk (0x37124, 0x37134, 0x37088) and `Tact_InitMission` stores an
`apObjects` entry into the record's `pObj` at +0. They are now `TactItem_156910` etc. in the IDB, and the port's
`ObjectRec **ppObj…` fields became `TactItem *pItem…` — which is what let the AI hand them to the queue as
weapons. 99 new checks, 3 original bugs recorded. Written up in `docs/parity/game-core.md` under
`## Tact_EnemyAiTick (0x4cbe0): the enemy AI driver`.

The unit before it (`5e8b0d7`) ported the low-morale enemy AI: **`EnemyAi_TryFireOrWander` (0x4abc8, 1,208 B)**
— the function that writes the `pAiActionUnit` / `bAiActionActive` pair the unit before that named. Three things
about it are worth knowing before you touch it:

- **Hex-Rays actively misleads on the burst's target rule.** When the drawn column is the shooter's own, the
  drawn row is replaced by the shooter's row and then 0x4ada4 `BNE loc_4ACE0` jumps back to the *draw*, not to
  the shot decrement at 0x4ae0c — so a same-column draw spends no shot, and a shooter on row 0 fires a burst at
  its own cell. The pseudocode's fall-through-to-decrement would fire zero shots where the ARM fires one.
  Recorded as an IDB comment at 0x4ada4 and pinned by `TestSameColumnRedraw`.
- **Two floor walks in one function, different depths.** The weapon-drop descent (`SUB R7,#1; CMP R7,#0; BGT`,
  0x4ae84-0x4ae90) tests layers nZ..1 and reaches layer 0 untested; the wander descent (`SUBS / BPL`,
  0x4afcc-0x4afd0) tests nZ..0 inclusive. Kept as the asymmetry, recorded at 0x4ae8c.
- **The gates caught my own citation error.** The first draft cited `Unit_PathToCell @ 0x2c2a8` and
  `Unit_CanStepTo @ 0x27bb8` in a comment, which credited 16,976 bytes of unported code and produced a NEGATED
  citation. `audit_credits` and `audit_names` both flagged it; both citations are now the
  `Name (0xADDR, unported)` form the metric ignores. This is the trap §8 already records, caught by the gate
  rather than by reading.

A large unit does nothing at all (0x4abdc jumps to the epilogue, skipping the RNG, the `Map.pSelWeapon` clear,
the queue and the status message). The AI-action pair is raised at 0x4AC04/0x4AC1C **before** 0x4ac94 chooses
between the random fire burst and the weapon-drop-and-wander, which is the direct proof that the flag means "an
AI action is in progress". The burst reads the two action tables `Ui_FillActionCosts` writes —
`nTuCost = Map.anActionCost[mode]` (Map+0x4E4C) and `nArg = Map.anActionEnabled[mode]` (Map+0x4E7C) — and
ignores the enqueue's result, so an unaffordable burst queues nothing while the latch clear, the queue kick and
the name push still happen. `unk_104078`, which the status message strcat's onto the unit's name, is all zeros
and has no writer anywhere in the binary. 79 new checks, 7 original bugs recorded. Written up in
`docs/parity/game-core.md` under `## EnemyAi_TryFireOrWander (0x4abc8): the low-morale AI`.

The unit two before it (`08450e3`) ported the queue walker: **`Tact_TickActiveUnit` (0x54130, 1,868 B)** — the loop
`GameTick_OnTimer` runs over the action queue, and the link that connected the three phase processors to the
game tick. Three things about it are worth knowing before you touch it:

- **`bFlag0` picks the processor; it is not a gate on one of them.** Set runs `Tact_ProcessFireAction`, clear
  runs `Tact_ProcessMoveAction` — alternatives. The move processor's prologue raising `bFlag0` is what hands
  the next tick to the fire phase.
- **`TactState+0x111C8` and `+0x111E2` are one pair and neither had a name.** `+0x111E2` was `bUnk70114`;
  `+0x111C8` was the head of `gap_70088`. `EnemyAi_TryFireOrWander @ 0x4abc8` stores the acting unit at
  +0x111C8 (0x4AC04) and raises +0x111E2 (0x4AC1C) together, **unconditionally — before** the branch at 0x4ACA0
  that picks its fire-burst path or its weapon-drop-and-wander path. So the flag means "an AI action is in
  progress": the move processor suppresses the "not enough time" record while it is set and the fire processor
  skips the enemy-spotted reaction while it is set. The walker clears it at 0x544D8 and zeroes the acting unit's
  `uTu` through the pointer at 0x544E0. Found by searching the disassembly for the `#0x1C8` offset — the field has
  no xref, exactly the blind spot §8 describes. Now `pAiActionUnit` / `bAiActionActive` in the IDB struct and in
  the port; the first draft's `pAiWanderUnit` / `bAiWanderActive` was too narrow and is corrected.
- **Hex-Rays double-counts the queue index.** ARM computes `R0 = pState + 56*nQueueCursor` and then reads
  `[R0,#0x18]`; Hex-Rays renders it as `v5->aQueue[offsetof(TactState,bFlag0)]`, adding the 0x18 twice. Recorded
  as an IDB comment at 0x54258.

The camera follow is per command and the two halves are ordered differently: commands 0/1/8 follow
`pUnit->pCell` and adopt the tracked unit *before* the focus, command 0xE follows the cell the last aim sample
fell into and adopts `Map_apXcomUnits[0]` *after*, command 0x12 is gated on `pTarget->pCell`'s `bFlags44` and
adopts `pTarget` *after*. With a unit already tracked, a shot re-centres only when `(int)Map_dTravel % 60 == 0`
(0x54750). The kill credit at `TactState+0x111D0` (u16) is paid to the first shooter with `nUnitType <= 8` that
is operative, over `aQueue[0 .. nQueueCursor-1]` measured *after* `Fall_Tail` advanced the cursor — so the range
is every entry that already ran. 96 new checks, 6 original bugs recorded (five unguarded dereferences and one
missing bounds test on the move path's camera follow). Written up in `docs/parity/game-core.md` under
`## Tact_TickActiveUnit (0x54130): the queue walker`.

The unit before it (`b43a42c`) ported the fire phase: **`Tact_ProcessFireAction` (0x50adc, 8,192 B)** — the
largest single function ported so far, and the half of every tactical action that runs while a unit is
shooting. Three things about it are worth knowing before you touch it:

- **The phase keeps no state; the Map block is its register file.** `Map_dTravel @ 0x14020` is the FSM's own
  counter (the aim setup zeroes it), `Map_dTravelSave @ 0x14038` is the distance the aim's arc walk took
  (0x302D0, 0x323BC), and a shot lands when travel passes travelSave. Neither Map field had a producer in the
  port, so `AimState` and `Tact_TraceProjectileArc` had to be extended first.
- **The ten flight samples are measured from the muzzle**, at `dTravel - i * 1.41` from `Map_nAimOriginX/Y/Z`;
  a sample behind the muzzle is parked at -1000.0. At travel 10 the first eight samples are still in the
  muzzle cell.
- **`Map_bFireHitResult @ 0x1406C` is the arc walk's verdict**: `Tact_TraceProjectileArc` clears it at 0x2FC54
  and sets it at 0x3010C when the walked sample leaves the map, and the fire phase reads it at five places.
  Both writes are register-built, so `xrefs_to` finds neither — and this unit's first draft fell for exactly
  that and recorded the field as never written (see §8).

The command switch covers 0, 1, 3, 4, 5, 8, 0xE, 0xF, 0x10, 0x11 and 0x12; the shared exits are LABEL_157
(0x52AC8), LABEL_155/156 (0x52AC0, `CMP R3,#0xB`) and LABEL_70 (0x51170). 150 new checks, 17 Map globals
named, 7 original bugs recorded, and three stale offsets plus two wrong names in the rename ledger corrected
(`Map_dThrowX/Y/Layer` are at 0x13EF0/0x13F40/0x13F90, not 0x140F0/0x14140/0x14190 — they are sample slot 1
of `Map_adMtX/Y/Z @ 0x13EE8/0x13F38/0x13F88`). Written up in `docs/parity/game-core.md` under
`## Tact_ProcessFireAction (0x50adc): the fire-phase processor`.

The unit before it (`4ba666b`) ported the fall processor: `Unit_ApplyFallDamage` (0x4a7b0, 1,000 B) +
`Tact_ProcessFallAction` (0x52adc, 5,716 B). Fall damage is soft-float, not fixed point:
`damage = (int)((float)(nZ - nLayer - 1) * (float)auStat[1] * 0.4f)`, where 1053609165 is `0x3ECCCCCD` = the
single 0.4f and `_itos`/`_muls`/`_stoi` are coredll's soft-float helpers; `nZ - nLayer - 1` counts floors, so
a one-layer step costs nothing. Its label graph is not a straight line — the message-only exit LABEL_101 and
the two "morale survived" exits jump past LABEL_111's `Unit_UpdateVisibility(pActor)` straight to the tail.
The psi gate is `max(1600, victimProduct) / actorProduct`, so a weaker attacker cannot fail. Eleven Map
globals named and seven original bugs recorded.

The unit before it (`386b8c2`) ported `Tact_CheckBattleEnd` (0x34fbc, 5,748 B): the debrief emitter. Its
head stores the caller's abort request into `Map_bAltOutcomePath` — the same flag the four post-battle
tallies gate on — then tries four exits (already-ended, the victory-tile scan, the abort, and the two
count tests), emits up to 19 TEXTS lines under three meta colour shapes, pays the score into
`g_nFundPending`, commits the loot into `aSmapSites[nSmapSiteCount].auItemQty` and
`ObjectRec.uRecovCount`, and ends with the Mars-to-alien-base restart tail. Its failed-base-defence exit
reads a facility byte with `LDRSB`, which is recorded as a trap in §8.

The unit before it (`519f9d3`) ported `Tact_ClearSoldierBattleFlags` (0x346e0, 2268 B) with its callee
`Soldier_PromoteByTypeScores` (0x47280, 1096 B): the five-rung rank ladder, the per-class stat refresh
and the promotion report. **Testing the refresh found a real port bug.** The growth rule's auStatBase
index is not its auStat index — the ARM reads `auStatBase+0x10` (index 8) to grow `auStat+0x12`
(index 9), and reads `0xF2` (index 7) to grow `0xB2` (index 8) — and the port used the stat index for
both, so the class-8 rule read `auStatBase[9]`, past the end of a nine-entry array into the bytes
before `uKills`. `g_aClassStatGrowth @ 0xAA930` (was `byte_AA930`), six classes x nine weights of
1..3, is now byte-checked against the image by audit_tables.py.

The unit before that (`cd1d2f0`) settled that **`unk_128A8C` @ Map+0x150D4 is the victory flag**, not
an abort flag, and the port had it backwards: `Tact_CheckBattleEnd` writes it — set at 0x350F0 when no
enemy is left, cleared at 0x35070 when no Xcom unit is left and at 0x351CC when a `MissionType_XcomBase`
mission is called off — and the debrief reads it as the win/lose switch (TEXTS[160] "Victory" at
0x353C4, TEXTS[161] "Defeat" at 0x35400, green/red caption in `TactHud_DrawStatusBanner` at 0x1E968).
The reason no writer looked to exist is recorded as a general trap: ARM cannot encode a 6684-byte
displacement, so every far Map access is register-built and IDA records no data xref for it.

This document was regenerated from the actual repository state: `git log`, `git status`, a fresh
build, a full test run, and five audit scripts run in the same session. Every number below comes
from a command run while this file was written.

---

## 1. What this project is

`pufobeta.exe` is a 641 KB WinCE / ARM32 game binary (PocketUFO, an X-COM style tactics game).
It has been loaded into IDA Pro and analysed to `pufobeta.exe.i64` (1103 functions, 598,792 bytes of
code). The project recreates that binary as a native **Win64 C23 program** built with CMake and
Clang, aiming for **behaviour parity** with the original rather than a redesign.

The rule that drives everything: **the IDB is the source of truth.** Every behaviour in the port
must be traceable to a specific address in `pufobeta.exe.i64`. Guessing is not allowed; when
Hex-Rays pseudocode is ambiguous, ARM disassembly decides.

Parity is measured by **citations**: a source comment of the form `Name @ 0xADDR` claims that the
port answers to that original function. `docs/parity/coverage.py` counts those claims against the
IDB inventory. A citation means "this original function is accounted for", not "this is correct" —
correctness evidence is the per-subsystem notes in `docs/parity/` plus 56 test suites.

### Target state

A Win64 executable that boots, loads `main.dat` / `lang.dat`, renders the geoscape and the tactical
map, accepts input, and plays, with every original subsystem recreated and every remaining gap
explicitly recorded rather than hidden.

---

## 2. Current state, measured

Build and test, run for this document:

```
cmake --build build --config Release      -> build_exit=0, no errors, no warnings
ctest --test-dir build -C Release         -> 100% tests passed, 0 tests failed out of 60
                                             Total Test time (real) = 4.14 sec
```

In **Debug** three suites fail: `test_titles`, `test_save`, and `test_template` intermittently. All
three already fail at `04f1155` (checked by building that commit in a clean worktree with the same
ClangCL toolset), so they are not from the last step. They are Debug-CRT artefacts — `test_save` reads
the `0xCDCDCDCD` uninitialised-heap marker, `test_template` asserts that a `malloc` after a `free`
returns a different pointer, and `test_titles` disagrees with `Titles_DrawBackdrop @ 0x1e630` about
whether a scroll of 0 draws a credit row. The first two are test bugs; the third needs a fresh ARM
read. See §7.

Parity and audit scripts, same session:

```
python docs/parity/coverage.py            -> functions cited: 952 / 1103 (86.3%)
                                             bytes cited:     550628 / 598792 (92.0%)
python docs/parity/audit_credits.py       -> credited functions: 952, NEGATED citations: 0
python docs/parity/audit_names.py         -> renames on record: 560 (explain 213 citations)
                                             credited addresses whose original name is not defined: 0
                                             (worth 0 bytes -- still zero since 0x1512c Tact_HandleMapClick
                                              and 0x17bb8 TactAction_ConfirmMeleeMode6 closed in unit 68)
python docs/parity/audit_bugs.py          -> ledger rows: 266, ORIGINAL BUG markers in src/pufo: 90
                                             ledger and port agree: every marker is listed and every row resolves
python docs/parity/audit_tables.py        -> 43 tables checked, 0 mismatched
```

Launch check, same session:

```
build/src/pufo/Release/pocketufo.exe
  -> process alive (PID observed), window created, no crash
  -> prints: pocketufo: assets loaded (lang.dat=..., main.dat=...)
build/src/pufo/Release/pocketufo.exe --dump-frame <path.ppm>
  -> runs GameUI_InitStartup + 40 anim ticks + one MainMenu_Draw frame, writes the 240x320
     RGB565 surface as a P6 PPM, and exits (exit=0). The dump shows the start menu: backdrop,
     four teal rows with black Cyrillic labels, SMK logo.
```

Test checks run across the 60 suites: **15979**. Coverage as of the latest commit: **965 / 1103
functions (87.5%)**, **563008 / 598792 bytes (94.0%)**. This unit ported **`Ui_DrawBaseInfo` (0x9b5f4,
10,196 B)** -- the ufopaedia entry draw, and the last large gap in the binary apart from `Geoscape_Draw`.
Four layers in the ARM's order: the screen pad, then the dashed waiting frame when the entry is not dirty,
then the close control and three gates (not built / not open / not dirty-with-a-category-panel), then the
dirty path that fills the screen, draws the solid 5 px frame, normalises the entry number and dispatches on
the category. **The dashed frame ramps on the loop iteration, not on the coordinate**: 0x9B660 computes
`((i + 20) * 5)` and 0x9B754 computes `230 - 5*i` / `250 - 5*i` for a `y += 7` / `x += 5` walk, and the first
draft used the coordinate instead -- it agrees for the first dash and diverges after it. **The normalisation
is five boundaries** (0x9B8C8..0x9B964): 54..75 -> category 3, `> 0xBB7` -> category 2 with `sub = n - 3000`,
1001..1999 -> category 1 with `sub = n - 1000`, 2001..2999 -> category 26, 76..84 -> category 4; `0xBB7` is
2999, so entry 3000 is category 2 with sub 0. **The fire-row chains REPLACE the label rather than appending
to it** -- each mode arm ends in an inline strcpy loop over its topic chain, and only the four `BL strcat`
sites append ("$", "%", " %", " s"). The two scratch copies at 0x187100 and 0x187208 tile the block exactly
(0x187100 + 208 = 0x187208, + 208 = `g_nUfopaediaEntryNum` @ 0x1872D8) and have only this function's xrefs,
so the port keeps them as locals and the IDB now carries both as `unsigned char[208]`. IDB: the prototype set
to `void __fastcall Ui_DrawBaseInfo(PadImage **ppScreenPad)` with an evidence comment, `dword_1872DC` ->
`g_nUfopaediaEntrySub`, `unk_187208` -> `g_abUfopaediaAmmoScratch`, and `unk_186B00` -> **`g_pUiCloseCtl`** --
a correction of an earlier guess, because the xref list shows the same control slot re-armed by
BaseInfoScreen_Init, BaseFacScreen_*, FundingScreen_*, NewBaseScreen_Open, BasePlaneScreen_Open, the Transfer
screens and the ufopaedia. The 37 port comments and 13 parity citations that named the old globals were
updated in the same unit. 108 new checks (test_baseui at 4138), 27 mutations all caught; two first-run
survivors were test weaknesses (an overlap probe on a row the other pair already painted, an item-strip
probe sitting on the divider's own column) and two mutants were dropped as equivalent before the run.

Test checks before this unit: **15871**, coverage **964 / 1103 functions (87.4%)**, **552812 / 598792 bytes
(92.3%)**. That unit ported the last small wrappers
between the CRT init-term table and the ported subsystems: `Font_Init_ReturnArg` (0x46130),
`Font_RegisterAtexit` (0x460d0) with its thunk `j_Font_Free` (0x460dc), `Bank_RegisterAtexit` (0x11164)
with `Bank_AtexitThunk` (0x11170), `DatFile_RegisterAtexit` (0x3f0d0), `Render_Frame_DecRef` (0x36818),
`UiModal_EndThunk` (0x12254), `App_QuitFromTact` (0x18e18) and the `CDialogTemplate` dtor (0x54c48,
cited on `MapTemplate_Free`). **The atexit registrars all take the object their thunk releases**: the
original names a global (pBank, unk_158220, fontSys), the port's objects are caller-owned -- except the
font, which is a real global (`g_fontSys`), so its thunk names it exactly as `j_Font_Free` does.
**`Render_Frame_DecRef` decrements Map+0x64** (dword_113A1C = apUiSlotsA[1], the banner-fade ticks
`Tact_EndMission` seeds with 10) and tail-jumps `Render_Frame(&gIsoDraw)`; its only caller is the WndProc
timer 0xC352 arm at 0xA09AC. The order is pinned at the counter-1 boundary, where decrement-first takes
the fade-in branch and render-first takes the dark blit -- counter 10 to 9 alone cannot tell them apart.
**`App_QuitFromTact` is the quit dialog's YES callback** (`Tact_Shutdown(1)` at 0x18E20, `App_QuitDisplay`
at 0x18E28), ported in `pufo_menubtn.c` beside the other two trio members with both halves injected, and
wired to the screen-8 escape seam; the shell has no mission env, so its shutdown half reaches
`Tact_Shutdown(NULL, 1)` -- a documented deviation. IDB: `Render_Frame_DecRef`'s prototype corrected from
three int arguments to one (the Map context in R0) plus an evidence comment. Dispositions recorded for
`App_ResizeMainWindow` (0x9ecb8, pure Win32 glue whose button the shell never installs) and the five no-op
atexit registrars. **A harness defect the sweep caught**: `test_renderframe`'s `CheckEqI32` printed FAIL
without counting, so the suite could never exit non-zero -- fixed, along with five bail-out guards in
test_baseui / test_geoscape; the sweep is 16/16 after the fix. 39 new checks.

Test checks before this unit: **15832**, coverage **954 / 1103 functions (86.5%)**, **552656 / 598792
bytes (92.3%)**. That unit ported `Ui_InitBaseInfo` (0x9ddc8,
744 B) -- the tail of both base-info openers (`Ui_OpenBaseInfo` branches to it at 0x84BD4,
`Ui_OpenBaseInfoAlt` at 0x84DF8), with `g_pUfopaediaScreenPad @ 0x1872E0` as its argument. **The block is
shared with the ufopaedia**: slot 0 the screen pad this function allocates and `Ufopaedia_Open`'s teardown
releases, slot 1 the entry pad `Ufopaedia_RefreshEntry` owns and this function never touches, slots 2..7
the six controls at 0x1872E8..0x1872FC. **The reselect half is what `byte_18706C` is for**: a 25-case
switch on `nResearchTopicShown - 0x76` folds the alien-encounter topics onto the five race topics by
residue, EXCEPT cases 17 and 22 (135 and 140), which take the default arm -- the same two Muton encounters
`g_anRaceKnownTopics` omits -- and topics below 118 fall out because SUB / CMP / ADDLS is an unsigned test.
Then a 198-cell walk of `aUfopaediaCatTopics` (zeros are ordinary cells, so topic 0 lands on row 0's first
terminator), then category (a STRB), pad dirty, entry dirty 0, entry index, entry number, topic,
`bUfopaediaOpen @ 0x18730C` -- a different byte from the 0x187308 build toggle `Ufopaedia_Open` runs on --
then `Ufopaedia_RefreshEntry` and the flag clear. **An original bug**: five topics (47, 94, 95, 96, 143)
are in no category row and 135/140 reach the same state, so the walk breaks out at category 9 / index 22
and 0x9E054 reads `aUfopaediaCatTopics[9][22]`, the byte at 0xAA90C that belongs to the table after it
(0x0A, i.e. topic 10 / entry 1006). The port keeps the same counters and names that byte instead of reading
out of bounds (bugs.tsv). **A test-quality defect the sweep surfaced**: comparing a control's `pfnClick`
against an empty stub can never fail -- the linker folds identical empty functions to one address -- so the
handler checks now CALL the installed handler and read back an id each stub records; recorded under
"Separate issues", and the older fixtures with empty stubs are listed there too. 222 new checks in
test_baseui (4030 there), 25 mutations all caught. IDB: `stru_AC548` renamed `aUiBaseInfoCtlDesc` with a
data comment, a function comment on 0x9ddc8 covering the seven steps, and a comment on 0x9E054 naming the
out-of-bounds read. Two stale port comments went with the reading: the header called the research tail
"ResearchScreen_Init (0x615d0, not ported)" when 0x615d0 is `NewBaseScreen_Open` and the tail is
`B ResearchScreen_Init` at 0x61778, and the four tail-call addresses in the openers' comments now match the
ARM (0x61778, 0x84BD4, 0x84DF8, 0x84CF0).

Test checks before this unit: **15610**, coverage **953 / 1103 functions (86.4%)**, **551912 / 598792
bytes (92.2%)**. That unit ported `Map_InitAndLoadRegistry`
(0x22648, 1,284 B) -- the Map subsystem's startup entry, a CRT init-term record whose thunk at 0x22604
runs it and then tail-calls `Map_RegisterAtexit`. **Its scratch half writes fourteen slots and two
constants**: Map+0x15110, +0xC4, +0x7C, +0x150E8, +0x68 (byte), +0x150F0, +0x37 (byte), +0x5C (byte),
+0x6C, +0x74..0x77 (four byte stores) and +0x80 (byte) go to zero, +0xC8 takes `Game_RandU16(3)` and
+0x70 takes 200; slot numbering is `(offset - 0x60) / 4`. **Nothing writes +0x64** -- an early draft of
this port claimed slot 1 and the sentinel fixture caught it. **Its registry half tests flags with
`strcmp(value, "0") != 0`** (0x22950, 0x228B4, 0x229B0, ...), so any text other than "0" sets a flag, an
empty string included, and hands `Reg_GetString` a literal default per flag: "1" for menubypen,
runbydblclick, scrollbypen and showborder, "0" for musicoff, soundoff, forcenodrag and turnbypen. The
port's `Options_LoadFromRegistry` had been reading the flags through `atoi`, which disagrees on exactly
those two cases. Numerics keep their inclusive ranges and constant fallbacks (gamespeed 0..4 else 4,
gamma 0..4 else 2, both volumes 1..5 else 5 -- 0 is out of range for a volume).

**The naming fix this function proves.** The byte at Map+0x150E4 was `Map_bInvPanelOpen` in the IDB and
`bInvPanelOpen` in the port -- a name for what the inventory code does with it. Its registry name is
`forcenodrag` (written at 0x115C4, read back at 0x22928), the options screen lights widget 88 from its
INVERSE (0x18A18), and `Menu_ToggleForceNoDrag` @ 0x19d94 -- IDA's `TactHud_ToggleFlag128A9C` -- is that
widget's click handler. In play it picks the transfer style: set means the 13 controls are press
hit-tested and a press while an item is held places it (0x146A8), clear means a drag; the inventory
screen highlights the hit region when set and draws the carried-item ghost when clear. It is
`Map_bForceNoDrag` in the IDB and `bForceNoDrag` in `TactInputState`, `MapInitEnv` and `PufoOptions`.
**A second naming defect fell out of the same reading**: `TactHudInventoryEnv` carried the two bytes under
swapped names (`nDragActive` held Map+0x150E4, `bInvPanelOpen` held byte_1139F4 @ Map+0x3C) -- the code
read them the right way round, only the names were wrong -- and correcting them exposed three vacuous
"no highlight" probes in `test_tacthud`, which sat at x 98 while the leftover `nHitSub0` of 1 would have
painted region 0 at x 60. Recorded as a separate issue. Deviation: the port writes through `MapInitEnv`
members and skips the ones the caller passes as NULL, because the shell owns neither the battle-ended byte
nor the throw-target slot. 24 new checks in `test_boot` (62 there) and 2 in `test_audio` (55 there), with
the scratch fixture started at 0x5A / 0x5A5A5A5A, the roll sampled 64 times, and the flag cases using
"yes", "" and a 299-byte value (rejected by `Reg_GetString`, which is what makes the per-flag default
strings observable). A 14-mutation sweep turned all 14 red. IDB: 4 renames, comments on 0x22648, 0x128A9C,
0x19d94, 0xAD1DA and 0x128AB4, and a correction appended to the `TactHud_DrawInventoryScreen` comment.
Refreshing `docs/idb/functions.tsv` for the rename exposed **eleven rows that had drifted behind IDB
renames** -- the five `Map_PlaySamples10_*` / `Map_PlaySample11_*` handlers, `Ship_AbortCourse`,
`Smap_CanInterceptReach`, `Funding_AddRaceBonus`, `Set_byte_159E88_4`, and the two unit-101 renames
`Ui_TextInputDialog` / `Ui_DrawTextInputDialog`, which is why regenerating the grouped inventory had
pushed the stale names back into it. All eleven now carry the IDB's current names and six `renames.tsv`
rows map those to the port's, which takes `audit_names.py` to 0 unresolved citations.
The previous unit ported `Font_LoadTactSprites` (0x257c4, 752 B) -- the only writer of the per-group sprite bases in the font block at 0x158268, reached
from `Map_BuildMissionTerrain` (0x22e10) at 0x22E4C and from `Save_ReadTGameData` (0x44248) at 0x4444C.
**Every base is the running counter captured immediately before that group's `Font_LoadSpr`**, so it is
the group's first frame index: `nTotalSprFrames` (+0x36) for the .spr groups, `nTotalMc1` (+0x38) for the
two slots the .ma1 templates index as `fontSys + 2 * nFlag`. **The load order is not the slot order** --
tanks, floorob, handob and bigobs load in that sequence while their slots are +0x24, +0x22, +0x20 and
+0x26 -- and urban missions load "civm" (aXcom0 slot 17) before "civf" (slot 16). The mc1 total is
captured once at 0x25804 and then carried: +0x04 is the first value, +0x06 what the terrain loop left
(it re-reads after every load that ran, 0x2584C) and +0x08 what the craft load left (the UFO loop does
not re-read). Three shipped tables feed it and nothing else reads them: `aTactTerrainSetName` @ 0xA6858
(40 names of 9 = 10 mission types x 4 sets), `aTactUfoSetName` @ 0xA69C0 (32 = 8 classes x 4) and
`abTactUfoClassByVariant` @ 0xA6AE0 (16 bytes, indexed `3 * (nTerrainVariant - 1) + nTerrainParam`, only
15 reachable). The craft name is `aPlane[nMapLayers - 1]` -- the layer count, not the mission type the
terrain builder uses for the same table.

**A defect in this port, found and fixed.** `Font_GetFlagSprBase` read `((const u16 *)pFont)[nFlag]`,
which mirrors the original's `*(u16 *)(fontSys + 2 * nFlag)` only if `PufoFont` were laid out at the
original offsets -- it is a model, not a mirror, and its first five u16s are `{bInited, nSlotCount}`
followed by `anAlienSprBase[0..3]`, so flags 2, 3 and 4 were really reading the alien sprite-set bases.
`tests/test_terrain.c` had been written around it, priming `anAlienSprBase[0..2]` to make template
lookups miss. `PufoFont` now names the five slots as `anFlagSprBase[5]` (blanks, cursor, terrain, craft,
UFO), the accessor reads them by name and returns 0 for a flag the shipped data never produces, and the
terrain fixture primes `anFlagSprBase[2..4]`. **The seam stayed a seam**: both call sites still go
through `pfnLoadTactSprites`, because the load ends with `Font_BuildIndex`, which frees and rebuilds the
very index the terrain builder's template and stamping passes then read -- a caller that owns neither the
font nor its index cannot have the builder rebuild it underneath them. The app, which owns the real
`fontSys`, installs `Font_LoadTactSprites(pFont, &env)` built from the env's own Map scalars. Deviation:
the original bounds neither table index; the port skips the group instead of reading past the end. 34 new
checks in `test_font` (66 there), with the fixture presets `nTotalSprFrames = 11` / `nTotalMc1 = 7` so a
group base that takes the wrong counter shows up as 11 where 7 was wanted, and every base asserted as an
exact frame count from the shipped archives. A 12-mutation sweep turned all 12 red. IDB: 12 renames (the
three tables, which IDA had named after their first entries, and nine unnamed fontSys base slots), a
function comment on 0x257c4 and comments on the tables; three new audited tables (43 checked, 0
mismatched), dumped by `tools/gen_tact_sprite_tables.py`.
The previous unit ported the two functions behind SCREEN 5,
`SoldierScreen_DrawDetail` (0x632cc, 6,096 B) and `SoldierScreen_DrawEquipPanels` (0x62d78, 960 B) -- 7,056
bytes, the biggest pair left in `strat-ui`. **The IDB called 0x632cc `Ui_TextInputDialog`**, which describes only
the keyboard block in its last ~1,100 bytes; the body is the whole selected-soldier readout (header ramp, rank
icon, cs0/cs1/cs2 insignia, armour, record block, nine stat bars, the two armour panels, the transfer panels,
then the keyboard), so the port and the IDB now call it `SoldierScreen_DrawDetail`. **The equip panels' row count
is 1 plus the number of armour objects with stock in the selected base**: `nEquipRowCount @ 0xab710` is reset to
1 at 0x62DB8 and raised once per `apObjects[122..124]` whose `auPerBaseVal[nSelectedBaseIdx]` is non-zero, cached
into `nSolStockArmour1/2/3` (0x159D84/88/8C); the frame is three nested rects at (45,75), (46,76), (47,77) with
height `18 * (signed char)rows + 29/27/25`. **The nine stat-bar captions (TEXTS 127..136) are the second
independent statement of the `auStat` slot order** (the first is Unit 74's training screen), so `pufo_unit.h` now
carries a `SoldierStat` enum naming all ten slots -- and `pufo_ballist.h` disagrees with it (`auStat[8]` =
`nStatThrow`, `auStat[9]` = `nStatStrength`), reported as a separate issue and not fixed here. **`bCraftType` 3
and 4 read a 140-byte buffer this function never fills** (0x64218): the craft name for an Interceptor or a Fire
Storm is whatever an earlier screen left in `pszItoaScratch @ 0x159DD0`; the port names the default TEXTS[93]
"None" and the buffer is in the ledger. Two earlier IDB names turned out to be wrong and are fixed here: **0x10DA38
was `szBaseAtResultOk`** although `UiPanel_ctor` copies it into every line buffer it allocates (0x3E774) and none
of its 72 reads has a writer -- it is the shared empty string, and it is also exactly `aTextRecs + 205*1800`, a
zero-filled record slot past the 1650 lang.dat records -- and **0x186C30/34/38 were `apPadCs`/`apPadCs1`/`apPadCs2`**
with no type; they are the `cs0`/`cs1`/`cs2` pads `Assets_LoadStartup` loads at palette slot 18 right after
`class.pal` goes into that slot (0x387F0), used as the soldier class insignia here and as command-pad art in
`Tact_DrawCommandPads`. 111 new checks in `test_baseui` (3,808 there): Phase A runs with the font freed so every
assertion is a rect blit with an exact 565 value, Phase B loads the real font from lang.dat/main.dat and checks
ink bands. The keyboard's first and last grid rows assert the exact glyph colour the pair writes rather than ink
presence, because `Ui_AnimExpandBlit` blends every pixel inside the keyboard region and "some ink there" proves
nothing. A 22-mutation sweep turned all 22 red; 2 further patterns are true equivalences (the u16 `!= 0` versus
`> 0` stock capture, and the armour gate widened to `<= 255`, which differs only for a unit type the byte cannot
hold and indexes the label table out of bounds). IDB: 1 function rename, 13 global renames, `pSolPanelEquip`
typed `UiPanel *`, 18 comments; 5 new ledger rows (266).
The previous unit ported the three release functions that
complete `titles` (13/13): `Titles_Shutdown` (0x3826c, 100 B), `Titles_dtor` (0x46fa4, 60 B) and
`EndScreen_Shutdown` (0x389f8, 48 B) -- the screen-16 and screen-18 arms of `Input_OnPointerUp`
(0x14924), which until now were two seams the shell never filled. **`Titles_dtor` writes screen id 22
unconditionally** (0x46FB0), before the surface gate at 0x46FC0 and not the 16 the credits ran under;
`GameUI_InitStartup` runs straight after `Titles_Shutdown` and replaces it with 17, which is why 22 is
never seen on screen. **`Titles_Shutdown` nulls nothing it deletes**: the five `titlesN.pad` glyph blobs
at 0x1571A0..0x1571B0 and `g_pTitlesState @ 0x158804` keep pointing at freed memory, so a second shutdown
frees them again -- and its state-block release is gated on that pointer (0x382AC), so a null state block
leaves even the screen id untouched. **`EndScreen_Shutdown` does not exit the game**: it deletes the ending
backdrop at 0x1571C0 with no null test and no null store (0x38A0C..0x38A14) and then *tail-calls*
`GameUI_InitStartup @ 0x38a28` (0x38A20), which re-loads the very font its own `Font_Free` just released
-- the reason the font is not observably freed when the startup env is wired. A shipped-data finding came
out of the fixture: `EndScreen_Init` asks the archive for the `"z"` sample set at 0x38920, but the shipped
`main.dat` carries **no `Z*.WAV` at all** (only `TACT0..N.WAV` and `BACK0..4.IT`), so every ending loads
zero samples. Deviations: the port nulls everything it releases, bounds the glyph array and the screen-id
pointer, and has no counterpart for the original's delete of the 0x154C0 state block because the port keeps
that block in the shell's static storage. The shell now wires the screen-16 arm for real
(`App_TitlesToStartup`: `Titles_Shutdown` then `App_InitStartupUi`); the screen-18 arm stays a seam because
the shell has no ending flow yet. 26 new checks in `test_titles` (136 there), with `Sound_Shutdown` pinned
through the fake audio backend's sample-free and music-free counters, the timers through the timer
backend's kill log (two kills, 0xC351 then 0xC352), and the tail call through the start menu coming up at
17 with a startup env and staying at 18 without one. A 14-mutation sweep turned 12 red; two are true
equivalences (`KillTimers_A`/`B`/`C` are byte-identical at 0x9e6a4 / 0x9e754 / 0x9e7c4). IDB: 3 function
comments and 7 line comments, no renames; 3 new ledger rows (261).
The previous unit ported the base facility grid's names, build icons and craft,
`BaseFacScreen_DrawNameGrid` (0x61c8c, 1,840 B) -- the last unported `strat-ui` function, and the only place `abFacilityGrid @ 0x159768`'s four layers are read together: `buildDays`
(the remaining build days), `tileA` (the tile stamp), `tileB` (the facility name) and `layer2` (the craft
standing in the cell). **The grid is 32 px cells, six per axis, starting at y 36** -- the backdrop loop
bounds are `CMP #0xC0` on both axes (0x61CD4, 0x61CE0), so it spans x 0..191 and y 36..227, and the
backdrop is 36 copies of the blank *basebits* tile, not a font glyph. **That is the fact that made this
function testable at all**: `fontSys_nBaseBitsSprBase @ 0x158294` is written at 0x85F78 with the frame
count *before* the `"basebits"` load, so every sprite here -- backdrop, facility name, build icon, craft
-- is indexed from that group base, and a fixture that loads only `"font"` draws sprite 0 and proves
nothing. **Two name passes run over `tileB` (+36) and then `layer2` (+72)**, a zero byte skips the cell
outright (0x61D58), and the sprite is the name tile except for a cell that is still building in the first
pass, which gets the blank tile (0x61D74 tests `buildDays`, 0x61D84 tests the pass counter left in
`var_18`). **`tileA` is a seven-way compare** (0x61E04..0x61E38): stamps 1/2/3 draw the build icon at
sprite +4/+5/+6 with the days label at (x - w/2 + 16, y + 7); 9/10/11 draw +13/+14/+15 with **no days
label at all**; 12 draws +16 with the label at (x - w/2, y - 8); anything else draws nothing further.
**A cell carries a craft only when `tileA == 12`**, the hangar's bottom-right quadrant (0x62134), and the
`layer2` byte does double duty: it is the sprite id (33..37, the five craft glyphs) *and* it picks the
vertical nudge -- 33 and 35 one pixel HIGH (0x62278 falls through to the `SUB #1` at 0x622B0), 34 one
pixel LOW, 36 unshifted, 37 two pixels HIGH, anything else nothing. The craft sprite sits 16 px LEFT of
the cell origin (`R9 = -0x10` at 0x62124). **The original reaches `layer2` through leftover state**: the
name pass's counter is 2 by the time the craft pass runs, so the row index carries 6 * 2 = 12 and the flat
offset 72 lands exactly on `layer2`. Deviations: the port reads `layer2` directly, bounds the base index
at 8 (the original indexes the eight `fac` blocks and eight `buildDays` blocks with no test at all), skips
null craft pointers (the original derefs `apCraft[i]` at once), and returns void where the original returns
register residue. 15 new checks, the main one a whole-frame compare against a reference frame painted in
the test from the ARM's own coordinates and sprite offsets. A 42-mutation sweep turned 41 red; three are
true equivalences and are not counted (`nCraftCount <= 0` to `< 0`, the days colour 255 to 250 in blue --
both clamp to the same 5-bit 31 -- and `%u` to `%d` on a byte). IDB: 25 line comments plus function
comments on 0x61c8c and 0x85f78, no renames; 3 new ledger rows. **`strat-ui` is complete: 245/245
functions and 121,164 of 121,164 bytes.**
The previous unit ported the transfer error dump, `Transfer_WriteErrorDump` (0x996e4, 3,824 B) -- a debug
dump writer, not a UI display, reached from
`TransferScreen_CommitTransfer` (0x97EE4) and `Smap_GrantSiteRecovery` (0x99038) when some base's
per-object column passes 60000 (`0xEA60`). Both call sites now call it and the commit's `pfnShowTError`
seam is gone. **Two original defects sit in the 20 instructions that build the name**: the strcpy of
`szAppDir @ 0x1883B0` into the 40-byte stack buffer has no bound, and `fopen`'s result is never tested,
so a failed open sends NULL to every `fwrite` and to the `fclose` at 0x99F4C. **The binary imports no
`remove`, `unlink` or `DeleteFile` at all**, so nothing ever deletes a dump -- an overfull base
accumulates one `t_error<rand>.txt` per report. **Every line ends with the 2-byte value 2573 written at
0x99784**, which is 0x0D 0x0A on the wire, written by `fwrite(&var_358, 2, 1, f)` rather than as part of
the label. **The nine site values do not agree on signedness**: `bFlag1` (0x99A9C), `bFlag0` (0x99B68),
`bUnk518` (0x99BEC), `bRecoveredCraftCount` (0x99CF4) and `bRecoveredSoldierAssign` (0x99D74) are LDRSB
and print signed, while `uRecoveredSoldiers` (0x99C70, LDRH) and `bHasLoot` (0x99DF0, LDRB) print
unsigned. Two labels carry a trailing space that has to be kept ("transfer N ", "add_new_ship= "), and
"(incoming transfer)" is appended to the SAME line as the destination base, before its CRLF. **The site
walk keeps a half-stride counter**: var_350 adds 371 per site and is divided back by 0x173 at 0x99EB4
while the pointer advances by the real 742 (`MOV R3,#0x2E4 / ORR R3,#2` at 0x99F1C) -- residue, not a
second stride. Deviations: bounded path copy, `-1` on a failed open, and `CreateFileA`/`WriteFile`
instead of `fopen` because the MSVC CRT deprecates the whole family and `/WX` stays on project-wide. 27
new checks, the main one a byte-for-byte compare of the whole dump file against expected text built from
the ARM's labels. A 30-mutation sweep turned all 30 red; `nSiteCount > 0` to `>= 0` is a true
equivalence (the site loop runs zero iterations at a count of 0 either way) and is not counted. IDB: 10
line comments, function comments on 0x996e4 and 0x5a8d0, no renames (`szAppDir @ 0x1883B0` was already
named); 3 new ledger rows. `strat-ui` had moved to 244/245 functions and 119,324 of 121,164 bytes (98.5%).
The previous unit ported the hangar's other face, `BasePlaneScreen_DrawCraftDetail` (0x7baf8, 3,824 B): the stores-transfer list and the craft read-out that
take over the whole frame when `bPlaneShowCraftDetail @ 0x161F20` is set. `BasePlaneScreen_Draw` hands the
call over at 0x7D37C and returns its value, so the hook unit 96 carried for it is gone -- the draw env now
carries the detail env. **The list-built gate short-circuits everything after the rails and the name**:
panels built draws the crew pair, packs (200,200,200 / 10,25,10), puts the name at (120-w/2, 30) and
returns, so the stores set and the read-out are mutually exclusive. **`dwActionFail @ 0x1870C8` is read as
a dword and decides the two row panels' `bFlag7073`**: exactly 1 clears it, anything else sets it to 1 --
and that 1 is the leftover R11 from the rail loop's `nRectH` (0x7BDA4..0x7BDC4), not a constant a reader
can see. **The band is `16 * min(nLineCount, nStyle)` of the select-row panel** (`MOVGE R3,R0,LSL#4` at
0x7BE7C), every separator is 1 x (band+2) at y 69 in palette index 0xB4, and the interleaving is sep 130 /
avail / sep 166 / sel-row / sep 181 / commit / sep 196 / taken -- the avail panel paints over the 166
separator, not the reverse. **The bottom line splits on `bBaseFacPreviewOk @ 0x18652D`, one byte away from
the `bBaseFacPreviewActive @ 0x18652C` unit 96 gates on**, in the same control block, with
`75 < nSmapCursorY < band + 75` both ends exclusive; armed it draws the hovered row's object name at
(10, 297) straight out of `apObjects[pPlaneSlotPanel->anLineAux[nPanelRowCursor @ 0x186BE8]]`, not armed it
recentres `pGeoscapeCtlConfirm` (`x = 120 - (uWidth >> 1)`, y 297, both STRH) and draws TEXTS[1127].
**The refusal map is a switch, not the buy/sell screen's `405 + reason`** -- the two screens read one dword
and pick different records out of the same six (410 / 408 / 409 here, centred at (120-w/2, 158)); any other
reason returns with the red pair packed and nothing drawn. **Both percentage lines read a field their label
does not name**: "Топливо" gets `100 * uRangeRemain / uRangeMax`, "Повреждения" gets
`100 - 100 * nFuel / uFuelMax`, both through `_rt_sdiv` with the divisor in R0 (0x7C2F0, 0x7C388) and
neither with a zero test. **The pad order is not the weapon order**: 25 takes the third weapon pad, 26 the
first, 27 the second, 28..30 the fourth..sixth, with `nOriginX` written first (55 left, 156 right), and a
craft type outside 0..4 blits nothing. **All four counters are zeroed whatever `Ship.bCanDogfight` says**
(0x7C5C0..0x7C5F8) and only recomputed when it is non-zero. 71 new checks on a real 240x320 surface with the
real font. A 52-mutation sweep now turns all 52 red; five were initially unpinned and **all five were
fixture defects, not port defects** -- palette slot 0 was zeroed, so the soldier and crew icon ids 38 and 40
blitted as colour 0 and were invisible (38 -> 1 and 38 -> 100 passed too), the pad key 0xFE mapped through
a zeroed slot-2 entry 254, and the reason-0 check counted ink in the panel window instead of comparing the
frame. IDB: 17 renames (the four counters, `nPanelRowCursor`, `dwActionFailReason` and the eleven pads in
the object block at 0x161A48), a function comment at 0x7baf8 and 16 line comments; 10 new ledger rows.
`strat-ui` moves to 243/245 functions and 117,128 of 121,164 bytes (96.7%). The previous unit ported the
hangar's own draw, `BasePlaneScreen_Draw` (0x7d374, 1,648 B): the two list panels, the three-row craft readout, two captions
and four ramped rail loops. **Three gates, and they do not gate the same amount of the screen** -- the
not-built skip (0x7D398) and the preview-gate skip (0x7D3D0) both land on `loc_7D640`, the second colour
pair, so the two captions and all four rail loops run whatever the flags say: a hangar whose panels were
never built still paints the frame and still draws "Корабль: " and "Статус". **The readout is gated by a
geoscape flag and the pointer row** -- `bBaseFacPreviewActive @ 0x18652C`, which has six xrefs in the whole
binary (four in `BaseFacScreen_DrawPlacementOverlay`, one in `Geoscape_InitUI`, this one), and
`nPosY < nSmapCursorY < nPosY + 16 * nLineCount` with both ends exclusive (0x7D3E0 reads nPosY at +0x1B88
through `MOV R3,#0x1B80 / ORR R3,#8`, 0x7D408 the band from nLineCount at +0xBDC). `nSmapCursorY` is read
LDRSH and written STRH (Geoscape_OnPointer 0x872F8), so it is the signed 16-bit pointer row the pointer
dispatcher keeps live for whichever screen owns the pointer; unlike the transfer, manuf and research frames
this band has no `nStyle` cap. **All three sums index `aItemQty` with `nPlaneSelCraftIdx @ 0x161F04`** --
the draw uses that global, not the `nSelectedCraftIdx @ 0x161AC4` the stores builder uses -- over slots
25..30 (armament, the six weapon slots the `SelectWeapon25..30` handlers cover), 31..35 (crew quantity, the
same five crew slots the crew quota sums) and the soldiers whose `Soldier.bUnk183 @ +0xB7` equals the craft
index; the compare is `CMP R3,R5` with R3 the zero-extended byte, so a craft index above 255 matches
nothing. **The armament value is a pair**: `itoa(sum)`, the backslash at `aQtyPairSep @ 0xAB9F0`
(0x7D5A8 / 0x7D5B0), then `itoa(Ship.bCrewSlots @ +0x14)` (0x7D5D0 / 0x7D5E0). **Rails A and B are
byte-for-byte the shared screen rails**, so `BaseUi_DrawFrameRails` is no longer file-local and the hangar
calls it instead of carrying a fourth copy. **Rails C and D test their bound AFTER the step**, so the body
that lands on the bound still runs: rail C's last body is x2 = 120 (21 bodies, colour up to 200) and rail
D's is m = -34 / v = 17 (18 bodies, colour up to 185); reading the two `CMP`s as the last drawn value gives
20 and 17 and a frame with a gap at y 49..50. The two x runs 15..119 and 120..224 are continuous, so the
inner frame closes (15,15) to (224,85), and it runs after the text. 61 new checks in `test_baseui`, on a
real 240x320 surface with the real font: the readout window (x 14..229, y 14..99) is compared against a
hand-painted frame -- labels, the three value strings, the shared rails and the inner rails painted from
the ARM with literals rather than the port's macros -- and separate probes then move one input at a time
and re-compare. A 47-mutation sweep turned all 47 red; two were initially unpinned through the
**macro-aliasing defect again** (the fixture placed its caption text with the same record macro the port
reads, so moving the macro moved the expectation with it) and the fixtures now use literal record numbers.
IDB: 6 renames (the five TEXTS records the hangar draws -- `aTextRec146_ArmamentCap`, `aTextRec147_CrewCap`,
`aTextRec148_CrewQtyCap`, `aTextRec124_CraftCaption`, `aTextRec153_StatusCaption` -- plus `asc_AB9F0` to
`aQtyPairSep`), a function comment at 0x7d374 and 16 line comments. `strat-ui` moves to 242/245 functions
and 113,304 of 121,164 bytes (93.5%). The previous unit ported the hangar's stores-transfer panel set,
`BasePlaneScreen_BuildControls` (0x7b014, 2,788 B). **The row filter is three ARM compares** (0x7B190
`CMP #0x19`, 0x7B198 `CMP #0x1E`, 0x7B1A0 `CMP #0x34`): a slot gets a row when
`slot < 25 || (slot > 30 && slot < 52)` -- the refused 25..30 is the six weapon slots the hangar draws
through its own weapon columns (corrected in unit 96: it was called "the five craft slots and the five crew
slots" here), and 52 is `ObjectRecFull.aItemQty`'s own length.
**`byte_AA968` is a 156-byte slot-to-topic table** (now `g_abSlotTopicGate`): slots 0..24 map to topics
0,1,6,9,2,7,10,4,3,8,5,11,13,12,18,23,24,22,21,25,14,15,17,20,16, slots 31..35 to 97..101, 37 to 113, 39 to
114, 40..47 to 1 and 48..50 to 115, 117, 116 -- and a stocked slot still needs that topic's `bUnlocked` at
`ResearchTopicLive + 0xD`. **The crew quota is seeded from the craft rather than zeroed**:
`apObjects[31..35]->aItemQty[nSelectedCraftIdx]`, reached as the context base 0x1139B8 + 0x184.
**`bPlaneStoresCommitted` is raised before a single row exists** (0x7B12C) -- it means "this panel set is
live", which is why `BasePlaneScreen_Close` tests it the same way `CommitStores` sets it. **The sort walks
`nObjectCount-1` order entries of `g_aBuySellRowOrder`, not the table's 148, and swaps three panels where
the buy/sell builders swap five**; with the shipped 149 objects the two bounds coincide, so this is only
visible with a smaller object set, and the test pins it (slots 1, 24, 31, 32, 34, 35, 51 come out as
1, 51, 24, 31, 34, 35, 32). A 46-mutation sweep turned all 46 red, and the four initially unpinned were
test defects: three compared a value against the same macro the mutation moves, and the fourth swapped
clicks between **empty fixture handlers, which MSVC's identical-code folding merges into one address**,
making every pointer-identity check between them vacuous -- the same defect voided two checks in the older
`TestPlaneInit`, now fixed. `strat-ui` moves to 241/245 functions and 111,656 of 121,164 bytes (92.1%).
The previous unit ported the base-attack screen:
`BaseAtScreen_Update` (0x57808, 3,872 B), `BaseAtScreen_Draw` (0x58728, 420 B) and `BaseAtScreen_CloseThunk`
(0x577c0, 72 B). **The barrage is a round loop, not the flat sequence Hex-Rays renders**: 0x57BE8..0x583F0 walks
the four defence kinds in order, emits a FIRE row plus a HIT/MISS row per cell, emits the gravity-shield pair once
per round at 0x582A8 and loops while `nRound < nGravity` (0x583F0 `BLT loc_57BE8`), so a base with G gravity cells
and D defence cells prints `2*(D*(G+1)+G)+3` rows -- the same expression the capacity computation at 0x57A94
produces, which is what confirms the loop reading against the decompiler's scrambled `while(1)`. **Each kind has
its own roll threshold and its own damage** (50/500 missile, 60/600 laser, 70/900 plasma, 80/1500 fusion against
`Game_RandU16(100)`), and the attack budget is burned by the damages and tested only after a pair is emitted, so
the budgets 50, 200, 250, 500 and 500 are behaviourally identical and only 1200, 2200 and 3000 can be pinned.
**The tail names the outcome**: a burned budget prints "НЛО уничтожен" / "Угроза захвата снята" and clears the
follow flag, a surviving one prints "НЛО высадилось рядом с базой" / "Подготовка к защите" and sets it, and the
close thunk removes the strateg item and un-pauses the clock only when it is clear. **The original overflows its
own panel here** -- 18 gravity plus 18 defence cells asks for 723 rows against the 500 a `UiPanel` holds; recorded,
not reproduced. 187 new checks; **73 of 78 mutations turn at least one red**, and the five that stay green are
equivalences rather than gaps (the (i8) craft-class cast, the dead `nGravity > 0` conjunct, the disjoint
`SetBytes3to5`/`SetBytes6to11` ranges, an un-freed pad, and a scroll gate masked by `UiPanel_ScrollNext`'s own
guard). The sweep also caught two defects in the tests themselves: a static `StrategItem` handed to
`PtrArray_DeleteAt`, which `free()`s its element, aborted the suite with 0xC0000409, and a claimed 143 painted
pixels that were really the panel's own row text. `strat-ui` moves to 240/245 functions and 108,868 of 121,164
bytes (89.9%). The unit before that ported the base-info screen's frame,
`BaseInfoScreen_Draw` (0x56fb8, 1,744 B), which completes the base-info family. **The grey ramp's two halves
overlap**: 24 steps of 5 pixels, the left block at x+1 in `(5*(step+20), same, same)` and the right at x+119 in
`(-6-5*step, same, same)`, both as bytes -- the left climbs 100..215 and reaches x 120, the right falls
250..135 and starts at x 119, and because the steps run in order the left ramp's last block overdraws the right
ramp's first two pixels. Only 3 of 5 survive per band, so 250 appears 9 times in the frame and not 15. The byte
arithmetic also makes the halves share colours (`-6-5*7 = -41 = 215`, `5*(7+20) = 135`), so 215 and 135 each
appear 30 times. **The base name is drawn before the colour pair is packed** (0x57284 before 0x5729C), so it is
drawn in the colour the palette already held, not the one this function installs. **The bands move with
`Base_CountShipsAtBase`** when `bBaseInfoPanelMode` is clear -- 35, `18*n+59`, `18*n+169` -- against fixed
35/113/225 when it is set, and the original re-reads the count on all four calls of every step (0x57118,
0x57158, 0x57198, 0x571D8). **The three captions are real lang.dat records**, not the zero-filled scratch the
panel labels use: TEXTS[1112] "Выбор базы" centred at (60, 300), and the toggle label at (180, 300) is
TEXTS[1113] "Месячный расход" in panel mode or TEXTS[1114] "База" outside it -- now `aTextBaseInfoSelect`,
`aTextBaseInfoMonthly` and `aTextBaseInfoBase`. **The adopt arm is inside the overlay arm**: a differing
`byte_158834` is adopted by the draw itself, which clears `byte_1870F8` and `bBaseInfoPanelsBuilt`, frees the
three panels and calls `BaseInfoScreen_BuildPanels` again -- so the pickup lands on the next draw, not on a
click, and with the overlay closed a differing base is left alone entirely. 58 new checks; **all 35 mutations
turn at least one red**, so nothing here is left unpinned. 5 deviations: a null environment is refused, the panel
list and each panel pointer are guarded, the site-record index is bounded, the 60-byte name copy is bounded
where 0x57250..0x57264 is not, and the craft count is computed once per step instead of four times. `strat-ui`
moves to 237/245 functions and 104,504 of 121,164 bytes (86.2%). Unit 92 ported the base-info screen's bring-up and
panels: `BaseInfoScreen_Init` (0x57688, 312 B) and `BaseInfoScreen_BuildPanels` (0x557f4, 6,084 B) -- the
largest single unported `strat-ui` function. **Init takes the address of the three-slot block at 0x158828**:
slot 0 the screen pad (`operator new(0x14)`, `PadImage_ctor`, `PadImage_Load("baseinfo", 14)`, both origins 0),
slots 1 and 2 the two controls (`operator new(0x20)` + `UiControl_ctor` twice), with `BaseInfoScreen_OnOk` on
slot 1 and `BaseInfoScreen_TogglePanelMode` on slot 2 through +0x0C; `unk_186B00` moves to (224, 1) and takes
`BaseInfoScreen_CloseThunk`; both controls run `UiControl_Setup(ctl, 0, 0, stru_AB198[i])` over the descriptors
`{1, 300, "emptyb", "cemptyb", "", 0}` and `{120, 300, ...}`; the tail is `byte_1870F4 = nSelectedBaseIdx`,
`byte_1870F8 = 0` and the panel build as a tail call. **Every label on this screen is a clear, not a string** --
the sources 0x10DA38, 0x104C7B..0x105C7F, 0x105D4C and 0x105E19 all sit above 0xAC800, past the end of the
`.data` file image (vsz 0xE2D70 against rsz 0x6800), and none has a write xref anywhere in the binary, so each
copy writes an empty string. **Hex-Rays is wrong about the craft cost loop**: it renders the aux-0 case as
leaving the loop, but the `break` at 0x5683C only leaves the switch -- 0x56810 writes "500000$", 0x56838 adds
500000 and the test at 0x56858 continues, so aux 0 is priced exactly like aux 1 and only the total column's
marker ceasing to be 1 ends the run. **`Craft_CountSoldiersAboard` here is fed the craft ID** (the free
column's aux holds `Ship.bId`), the only call site in the binary that does not pass the roster index. The
**row map** is the substance of the function: free column 1 soldiers aboard, 2/3 the crew availability, 6..10
the five capacity deltas, 17.. the craft cost labels, `nShips+19/20/21` the fixed unit prices, `nShips+23` the
facility total (50000 per type byte != 0xFF over the same 36 cells every capacity walk uses, built or not) and
`nShips+25` the grand total; total column 1/2/3 crew, 6..10 capacities, 13 `Base_CountBuiltFacilities` and
`nShips+19/20/21` the money breakdown. The tail rows move with the craft count, which the no-craft case pins.
**6 deviations:** the port nulls the freed panel and control slots the original leaves dangling, guards the
three panel pointers after allocation, bounds every row index, and uses `snprintf` for `j_itoa`. 135 new
checks; 60 mutations turn at least one red, and four -- the clear of name row `nShips+17`, the eight-row clear
count at 0x55FE4, the 42-row clear count and the kind-4/kind-5 re-clear at 0x56DD4 -- are provably invisible
because each writes an empty string over a row that is already empty. 5 new IDB symbols (`unk_158824` ->
`pBaseInfoPanelNames`, `unk_158820` -> `pBaseInfoPanelFree`, `unk_158838` -> `pBaseInfoPanelTotals`, all typed
`UiPanel *`, `byte_AB190` -> `bBaseInfoPanelMode`, `asc_AB1D8` -> `aDollarSign`) and 4 comments, including the
aux-0 correction at 0x56810 and the id-vs-index note at 0x6649c. `strat-ui` moves to 236/245 functions and
102,760 of 121,164 bytes (84.8%). Unit 91 ported the psi screen's frame,
`PsiScreen_Draw` (0x72398, 1,960 B), which completes the psi family. **The whole body sits behind
`g_bPsiPanelOpen`** (0x723B8): a closed screen draws nothing at all -- not the rails, not the controls, not
the tail. **The rails are the shared `BaseUi_DrawFrameRails`** (0x723C8..0x72558), the identical block
`TransferScreen_Draw`, `ManufScreen_DrawFrame` and `ResearchScreen_Draw` each carry. **Four captions, and an
earlier IDB note about them was wrong** -- it called them scratch buffers nothing writes, but all four are
TEXTS records at `aTextRecs @ 0xB38D0` and the shipped lang.dat has text in every one: TEXTS[1188] the four
column captions at (7, 54), TEXTS[1189] centred at (60, 300), TEXTS[1127] "ОК" centred at (180, 300) -- the
same record the manuf, research and buy-sell frames use -- and TEXTS[1190] centred at (120, 158).
`unk_EF024`, `unk_EF0F1` and `unk_EF1BE` are now `aTextPsiColumnCaptions`, `aTextSelectBase` and
`aTextNoPsiRoom`, each `char[205]`. **Centring is `centre - (width >> 1)`**: the `ADDMI` at 0x7290C carries
flags from `MOVS` on a positive width, so the add never fires and the `ASR #1` at 0x72910 is a plain floor.
**Overlay** (byte_1870F8): the full-screen blend, a 32-wide erase strip at x 208 of height
`28*nBaseCount + 4`, `Base_DrawFacilityIconStrip` with the mark argument **-1** so the drawer paints no
chevron of its own, the 210-wide panel, then one bevelled row per base with its name centred at
(110, 28*i + 7); the row count is the signed byte at `abFacilityGrid + 0x618`, so a negative value draws no
rows. **A base whose `Base_CalcPsiLabCap` is 0 gets the 23-step white bracket** at x 212..234 -- the same
bracket the strip drawer paints for a marked base, here marking the ABSENCE of a psi lab. **Tail:**
`byte_158834` against `byte_1870F4`, both signed. Equal, or the overlay closed: pack the tail colour pair
and, with `dwActionFail == 1` and `dword_1870B4[0] == 0`, run the shortfall panel and its caption.
Different: `Base_CalcPsiLabCap` either adopts that base (0x72A28 / 0x72A2C) or does nothing at all, not even
the colour pair. The 70 in the panel call is R7, the same register the outer bevel used as its colour index
at 0x72868 (0x46 = 70). **5 deviations:** the caption and name sources are passed in rather than reached as
globals, the title's base index is bounded, the strip drawer and the two control blits are seams, and four
pointers the original reaches unguarded are checked. 82 new checks; 32 mutations turn at least one red, and
one -- the 210-wide panel rect narrowed from 210 to 200 -- is provably invisible, because the 32-wide erase
strip at x 208..239 covers exactly the difference. 3 new IDB symbols (476 -> 501 on record), 10 line
comments, and the corrected function comment at 0x72398. `strat-ui` moves to 234/245 functions and 96,364 of
121,164 bytes (79.5%). Unit 90 ported the psi screen (geoscape screen 17): `PsiScreen_BuildPanels`
(0x716d4, 2,940 B), `PsiScreen_SelectListed` (0x71298, 848 B),
`PsiScreen_Init` (0x72b40, 332 B) and `PsiScreen_ApplyAssignments` (0x72250, 328 B) -- 4,544 B with
`PsiScreen_PanelPrev` (0x715e8) now credited, its citation having been split across a comment line break
that `coverage.py`'s `@ 0x...` pattern cannot follow. **Four parallel per-base columns**, each an array of
eight `UiPanel *`: the soldier name, `auStat[6]\auStat[7]`, the training delta and the assignment box. The
middle two are not separate globals in the image -- they are the dword runs at `byte_15EE84+4` and `+36`,
and that 72-byte block also holds the row-selected flag at +0 (set by Init at 0x72C70, cleared by the
screen-17 branch of `Geoscape_OnPointerUp` at 0x890C4) and a scratch row index at +68 that SelectListed
uses to hand one row to its four `SetLineMeta` calls. **`BuildPanels` is a build/release toggle on
`g_bPsiPanelOpen` exactly like `Ufopaedia_Open`**, in three passes: count each base's soldiers (0x71734),
build four panels sized to that count (0x7178C), fill the rows with the count array rezeroed as the row
cursor (0x71BDC). **Assigning a soldier is what makes them psi-capable:** 0x71D00 stores 1 into
`Soldier.bPsiActive` for every assigned row, before the stat columns and the countdown read that byte.
**The two stat columns are gated on `bPsiActive` alone**, not on the assignment (0x71E2C), so a psi-capable
soldier shows their numbers with the box clear. **The training countdown** arms `30 * auStat[7] / 100` days
when `auStat[6]` is below `auStat[7]` and exactly **one** day when it is already at or above (0x7209C) --
which is what `Geoscape_TickBases` spends, one per day, raising `auStat[6]`: the gain the "+N" column
advertises. **The checkbox rows are cleared, not labelled** -- both label sources (`unk_EEE8A`,
`unk_EEF57`) sit above 0xAC800 in `.data`'s zero-filled span. **The assigned counter compares against 1
exactly** (0x71314). **ApplyAssignments closes the screen:** its tail call at 0x72394 reaches BuildPanels
while the flag is still set, so it takes the release arm. **1 original bug found, reproduced and pinned:**
0x72370 calls `Base_UpdatePsiLabNeeded` without ever loading its third argument -- R2's last write is the
inner loop's `ADD R2, R2, #1` at 0x7235C -- so the budget is the base's whole row count rather than
`Base_CountPsiSoldiers`, as the two other call sites (0x8BA4C, 0x953CC) pass. Twelve soldiers with one
assigned gets 12/10+1 = 2 instead of 1/10+1 = 1. **6 deviations:** unguarded dereferences of the freshly
built panels, unguarded and un-null-checked `Map.apSoldiers` indexing, unbounded `itoa`/`strcat`/`strcpy`
into 50-byte rows, an unbounded row cursor at 0x712EC, the port's 500-line `UiPanel_ctor` clamp, and a
return value no caller reads. 121 new checks; fourteen mutations turn 8, 6, 11, 3, 4, 1, 5, 4, 5, 2, 1, 6,
4 and 1 red. 8 IDB renames plus 5 new symbols, and the `Soldier` struct's three psi members renamed
(`bUnk149` -> `bPsiAssigned`, `bUnk422` -> `bPsiActive`, `bUnk423` -> `nPsiTrainDays`) in the IDB and in the
port (466 -> 476 on record); 4 function comments, 11 line comments, 1 new ledger row (228). `strat-ui`
moves to 233/245 functions and 94,404 of 121,164 bytes (77.9%). Unit 89 ported `Ufopaedia_Open` (0x9a8cc,
3,368 B), the ufopaedia's builder and teardown, which completes that family. **One function does both jobs on one
flag:** the build arm sets `g_bUfopaediaScreenBuilt` at 0x9A8E8 and *returns from inside its last loop*
(0x9B4FC), so the release code that follows the whole build block only runs on the next call, which clears
the flag first -- calling it twice is open-then-close. **The nStyle clamp lands on nStyle only.** Each
category panel is constructed with the full shown count and only `nStyle` is clamped to 13 (0x9AE44), so
category 5 keeps all 22 line buffers and pages through 22 entries while showing 13 -- and that full count is
what unit 88's paging filter compares against at 0x9A684. **The shown test runs twice**, a counting pass at
0x9AB04 that sizes each panel and a filling pass at 0x9B0C4 that writes the names, both on
`(aResearchTopics[topic].bUnlocked != 0 && topic != 0) || raceOverride != 0`. **The race lists are
hand-written and two of them are incomplete.** The 25 encounter topics 118..142 form five families by residue
mod 5, but Sectoid (118/123/128/133, fallback 138), Snakeman (121/126/131/136, fallback 141) and Floater
(124/129/134/139/59, fallback 119) cover their families while Ethereal never mentions 142 and Muton never
mentions 135 or 140 -- researching 142, 135 or 140 therefore shows nothing, and Ethereal's fallback is its own
topic 57 rather than a family slot. **Opening the screen writes the research mirror:** 0x9AD68 stores 1 into
`abResearchZero[topic]` for a race that passes its gate, which is *why* the paging gate at 0x9A6D4 accepts on
that byte at all -- a race met in the field becomes pageable without its own research topic ever being
unlocked. Row names come from `TEXTS[aResearchTopics[topic].wGate4]` and the five race rows are then
overwritten with the literals at 0xAC610..0xAC638; the selector's nine rows are **cleared, not labelled** --
the nine sources at 0x105EE6..0x10654E are 205 bytes apart and all sit above 0xAC800, inside `.data`'s
zero-filled span. **1 original bug found:** the teardown's control loop starts at 0x1872E0 and reads slot +2
on each of its six steps, releasing 0x1872E8..0x1872FC and never 0x1872E4 -- the live entry's PadImage that
every `Ufopaedia_RefreshEntry` allocates at 0x9A530 -- so closing the ufopaedia after any entry has been shown
leaks that pad. Ledger row added, not reproduced. 0x9B544 also writes `nLineCount = 50` into the text panel
*before* the null test at 0x9B550. 125 new checks; nine mutations -- completing Ethereal's family turns 1 red,
completing Muton's turns 2, removing the mirror write turns 1, raising the clamp to 22 turns 2, using 16 px
instead of 8 per visible row turns 10, skipping the race-name overwrite turns 9, not freeing the entry pad
turns 1, building the panel with the clamped count turns 4, and labelling the selector rows turns 1. 3 IDB
renames (466 on record) including `byte_160B2C` -> `abResearchZero`, 1 function comment, 9 line comments, 1
new ledger row (227), 90 markers. `strat-ui` moves to 228/245 functions and 89,860 of 121,164 bytes (74.2%).
Unit 88 ported the ufopaedia's paging and entry layout -- `Ufopaedia_RefreshEntry` (0x9a014, 1,528 B), `Ufopaedia_PrevEntry` (0x9a60c, 328 B) and
`Ufopaedia_NextEntry` (0x9a754, 364 B), 2,220 B into `strat-ui`, now 227/245 functions and 86,492 of
121,164 bytes (71.4%). **Two tables drive the screen.** `aUfopaediaCatTopics @ 0xAA830` -- IDA called it
`aFghijklmnop` because row 0's bytes happen to be ASCII `'f'..'p'` (102..112), which is also why the listing
rendered a 198-byte table as a 12-byte string -- is 9 rows of 22 research topic numbers, and **row 5 is the
only row that fills all 22 slots**: that is the sole reason the `++idx == 22` test exists. `anUfopaediaEntryNum
@ 0xAA5F0` maps topic to entry number, with topics 47/94/95/96/143 unmapped and the race topics 118..142
collapsing onto the five race entries 54..58, so each race appears once per race topic. **The category panels
reuse UiPanel's own storage for their entry list**: the paging code reads the topic as `panel + 4*(n+1261)`,
and 4*1261 = 5044 is exactly `anLineAux`'s offset in the 7092-byte panel -- so `apLines[n]` is the entry name,
`anLineAux[n]` its topic, `nLineCount` the count and `nUnk3032` the row the pointer last selected. No new
structure was needed. `dword_1871D0` is a ten-dword block: nine panels, then slot 9 at 0x1871F4 (the entry
index, which had no name of its own and reads as `dword_1871D0[9]`), then slot 10 (the category byte).
**PrevEntry and NextEntry are not symmetric** -- NextEntry stops on the table's zero terminator (0x9A7A8),
PrevEntry never consults the table at all: 0x9A640 walks the index down to -1, wraps the category there and
restarts at slot 21, leaving only the panel's own line-count filter to reject the slots past a short row.
**The unlock gate's second read is `abResearchZero[topic]`, not what Hex-Rays claims**: `MOV R0,#0x1C00 / ORR
#0x24 / ADD R1,R1,R4 / LDRB R1,[R1,R0]` addresses `aResearchTopics + topic + 0x1C24`, and 0x15EF08 + 0x1C24 =
0x160B2C. Hex-Rays renders the same address as `aResearchTopics[211].abActivateTopics[topic + 1]` -- one byte
late, which would test the *next* topic's mirror byte. The ARM is the authority and the port follows it; two
checks pin the direction by setting the two adjacent mirror bytes to opposite values. **The wrap loop at
0x9A308..0x9A4D0 is a byte-for-byte inline clone of `UiPanel_WrapText @ 0x37144`** -- same cursors, same
`psz[++v7 - v9] = 0` break, same `i = v7` resume, same trailing flush, same `nLineCount = nLineStart + count`
tail, same appended space -- so the port calls the shared function instead of duplicating 400 bytes of it.
**The layout cascade has a goto in it but reduces to four bands**: entry <= 1000 gives 220 px / style 7 / y 230
with no flag; 1000 < entry < 2000 gives 220 px plus the flag; 2000 < entry < 3000 gives 180 px / style 13 /
y 50; entry > 2999 gives 200 px / style 8 / y 110. Entry 1000 and entry 2000 both keep the default, and entry
3000 is the 200 px band rather than the 180 px one. Four further tests set only the flag (93..97, 54..75,
76..84, 85..92) and 93..97 also indents the text by seven lines so the panel's first rows stay clear of the
entry's picture. The description is `TEXTS[4*topic + 534..537]` concatenated -- 109470/109675/109880/110085
are 205*534..537. **The pad is a toggle, not a rebuild**: paging clears `bUfopaediaEntryDirty` (0x9A700 /
0x9A868), so the refresh it triggers takes the create arm, while any refresh that did not come from paging
finds the flag set and frees instead. **4 deviations recorded:** the four description records can need 816
bytes of an 800-byte stack buffer; the original never nulls the pad pointer after `operator delete` and reads
it again on the next refresh; `PadImage_Load` is called even when `operator new` returned null; and neither
paging function bounds its category or entry index. 107 new checks; seven mutations -- dropping NextEntry's
terminator test turns 3 red, giving PrevEntry the test NextEntry has turns 9 red, reading the mirror byte one
late exactly as Hex-Rays claims turns 2 red, making the > 2999 boundary exclusive turns 3 red, shifting the
description window one record early turns 6 red, removing the indent turns 2 red, and wrapping the category the
wrong way turns 3 red. 11 IDB renames (463 on record) including two retyped tables and a new symbol at
0x1871F4, 3 function comments and 7 line comments. Unit 87 ported `BaseFac_BuildStatusMsg`
(0x6a4bc, 1,520 B), the geoscape's two-line base-facility status string. **Thirteen arms on a character
code**, 0x20..0x2C (`SUB #0x20 / CMP #0xC / ADDLS PC` at 0x6A4E0, the char sign-extended), and
**`GameTime_Pause` runs at 0x6A4D4, ahead of the switch** -- so it happens on all thirteen arms and on the
default. **Every argument comes out of line 0 of the root message list** @ 0x158890 at the only call site
(0x8C82C): the case byte is that line's flag (0x8C818), the base index is its colour dword slot 2
(0x8C7C4), the row index slot 0 (0x8C7EC) and the column index slot 1 (0x8C7D8); a1 is abFacilityGrid
(0x8C790). The dispatcher switches on the same flag again at 0x8C83C and draws the two lines. `'!'` and
`'"'` are the same arm twice; `'$'`, `')'` and `'*'` all jump to one tail. **The six label prefixes are
TEXTS records** 1334/1339/1340/1341/1342/1343: "Base: ", the same word with a space before the colon,
"Production: ", "Research: ", "Module: ", "Craft: ". **The record-0 offset mattered and is now recorded.**
The two candidates in lang.dat are 0x102 and 0x1CA -- exactly one record apart -- and the wrong one still
produces a plausible string at every index, so nothing looks wrong. It was settled against the shipped
facility names: 106 Access Lift, 107 Living Quarters, 117 Hangar, 118 Gravity shield, 463 Missile
Defences, 122 Fusion Ball Defence, which is precisely the `g_anFacNameRec` order `Geoscape_InitUI` copies
into `aBaseFacFlags`. **`'&'` reads the facility grid** at `abFacilityGrid[480 + 6*(24*base + row) + col]`
and names the kind from `aBaseFacFlags[kind]`: +480 is row 18 of the eight 144-byte 24x6 blocks that start
at +372, a different window from the hangar arm's rows 12..17. **`'%'` reads the topic byte one record
in** -- `LDRB [R7,#0x22]` on a 34-byte record is `aTopics[nBaseIdx + 1].abUnk0[0]`, the same
off-by-one-record pairing `ResearchScreen_RebuildTopicList` uses. **`szScratch186488` carries a name
between calls**: `'\''` and `'('` write it, `','` appends whatever it finds there. 0x10DA38, the source the
255 sentinel copies from, is inside `.data`'s zero-filled span, so it copies one NUL -- a clear, not a
message. **1 deviation recorded:** unbounded copies into fixed scratch buffers and untested indices; the
port bounds both. 54 new checks; three mutations -- prefix indices shifted by one record (the exact mistake
made while writing this unit) turns 31 red, the hangar grid offset turns 4 red, reading the topic byte in
place turns 1 red. 6 IDB renames, the five arguments named, 1 function comment, 7 line comments, and a
comment at `aTextRecs` recording the record-0 offset with its proof. `strat` moves to 200/204 functions and
126,716 of 160,496 bytes (79.0%). Unit 86 ported `BasePlane_ResetDetailState`
(0x79cb8, 328 B), the plane screen's six craft-weapon stat rows. **The row count starts at 1, not 0**
(0x79D20): the panel always has its column-header row, and `BasePlaneScreen_DrawStatLabel` sizes its three
nested rects from it -- 230 x 20*(n+1), then 228 x 20*n+18 and 226 x 20*n+16 -- so a base holding nothing
still gets a one-row panel of headers. **The loop reads every slot and keeps six:** 0x79D48 walks
0..`Map_nObjectCount`-1 and evaluates `apObjects[i]->auPerBaseVal[nSelectedBaseIdx]` for all of them (the
LDRH at record +0x2A sits outside the switch), but only slots 25..30 have an arm, and the default arm does
not bump the row count either. The shipped OBJECTS.DAT makes the window concrete rather than arbitrary: 25
Craft cannon, 26 Stingray launcher, 27 Avalanche launcher, 28 Laser cannon, 29 Plasma cannon, 30 Fusion
ball launcher -- names exactly 208 bytes apart in main.dat at 0x2A082E..0x2A0C3E -- with Psi-arm at 24 on
one side and the five tank/hovertank weapons at 31..35 on the other. **The six counts are dwords** (every
store STR, every read LDR) and each one gates its row's control: `Geoscape_OnPointer` (0x88674..0x88730)
and `Geoscape_OnPointerUp` (0x8a38c..0x8a460) hit-test a row only when that row's count is non-zero. **One
global, two unrelated uses:** `nPlaneStatRowCount @ 0x161A38` is also the start-menu row-key flicker's
scratch -- 0xA0AEC stores `nMapPhase` into it, 0xA0B14 adds `Game_RandU16(8 - nMapPhase)`, 0xA0B20 writes
`it * 32 - 1` into `MainMenu_anRowKeys[i]`. The port had named it `g_nKeyAccum` after that second use,
which made the plane screen's row count look like a different global; it is now `g_nPlaneStatRowCount`.
**1 deviation recorded:** 0x79D5C indexes `auPerBaseVal` with a LDRSB'd signed byte and tests nothing, so an
out-of-range index reads inside the record (`auPerBaseVal[-1]` is the u16 at +0x28, `[8]` is `aItemQty[0]`
at +0x3A); the port refuses and lands on the same six-zeroes-one-row result. 49 new checks; two mutations
-- first weapon slot 25 -> 24 turns 13 red, dropping the non-zero filter turns 4 red. The guard is tested
by planting 0x1234 and 0x5678 exactly where the unchecked read would land, and the last case re-runs the
same object with a valid index so those zeroes cannot come from an empty fixture. 7 IDB renames, new enum
`PufoCraftWeaponSlot` (25..30 plus the five vehicle slots 31..35), 1 function comment, 6 line comments, 0
new ledger rows. `strat` moves to 199/204 functions and 125,196 of 160,496 bytes (78.0%). Unit 85 ported
`Geoscape_DrawGlobeHover`
(0x6727c, 2,208 B), the new-base globe hover frame: the crosshair, the scan animation, the two random
readout lines and the terrain / cost / Build base labels over the hovered region. **The four label sources
are TEXTS records, not literals.** 0xE7B3A / 0xEC680 / 0xEC74D / 0x103C77 sit past `.data`'s initialised
span -- the section declares 929,136 virtual bytes and carries only 26,624 raw, so everything from 0xAC800
up is zero-filled at load -- and each address is an exact multiple of 205 past `aTextRecs`, i.e. records
1042, 1136, 1137 and 1603. Out of lang.dat they read "Can`t build here", "Terrain: ", "Base cost: " and
"Build base". TEXTS[1042] is also the record `TEXTS[905 + 0x89]` names, which is why the ocean tile and
region 0x89 read identically and `aNewBaseCostByRegion[9] == 0`; the nine buildable regions are
TEXTS[1033..1041]. **The halving triple, twice more.** 0x6733C and 0x67884 are both
`RSB #0x140 / CMP #0 / ADDMI #1 / ASR #1`, plain C `(320 - uHeight) / 2`; Hex-Rays renders the second as
`321 - uHeight` for a tall strip, which reads like a special case and is not one. **The panel gates the
rest of the frame:** while `Ui_AnimExpandBlit` mode 3 is incomplete the function returns at 0x67490 and the
scan animation, the readouts, the labels AND the counter advance never run. The wash key is
`Game_RandU16(10)` with every roll below 8 collapsed to 0 (0x672B4), so only 8 and 9 survive and key 0
leaves the surface alone -- that is how the globe shows through, and the tests pin the blended pixel for
all three cases. **1 deviation recorded:** 0x67980 indexes `aNewBaseCostByRegion[tile - 0x80]` with no
range test; the port skips the cost line for an out-of-range tile. 108 new checks; two mutations were run
(tick base 3 -> 4 turns five red, horizontal sweep gate 15 -> 16 turns two red), and two helper bugs found
on the way are recorded in the parity doc because either would have made a whole group vacuous. 9 IDB
renames, `aNewBaseCostByRegion` retyped to `u32[10]`, 1 function comment, 9 line comments, 0 new ledger
rows. `strat` moves to 198/204 functions and 124,868 of 160,496 bytes (77.8%).
Unit 84 ported `Geoscape_PulseUpdate` (0x954dc, 1,812 B), the geoscape pulse tick, and
`Smap_SelectMapTarget` (0x84484, 340 B), the map-target arm. **The tick has three halves that do not depend on each other** -- the map-screen craft pass, the
phase-gated screen switch, and four saturating counters -- so the tests drive each alone. **What only the
ARM fixes.** `Map_nCraftCount` is read with **LDRB** (0x95524, 0x955F4): an unsigned byte. And the empty-
craft exit is `CMP R3,#0 / BLS loc_9560C` at 0x9552C, which jumps **into** the clock block rather than past
it, so an empty craft list still advances the clock; the pause byte 0x15961C gates the clock, not the craft
pass. **The phase gate is not a counter**: if `nUiSlot0AnimPhase` is already above 1 the whole switch is
skipped (0x95644), otherwise it is incremented and the switch runs only when that lands above 1 -- so the
switch fires exactly when the phase arrives as 1, and LABEL_88 resets it on every path. **The held-
direction scroll is absolute, not incremental**: `unk_1864F4..F7` (renamed `bGeoscapeScrollUp / Right /
Down / Left`) give +25 / +25 / -25 / -25 on the two terms (5 on screen 2), `nLastDx / nLastDy` are preset
120 / 160, and the call is `Smap_ScrollBy(dx + 120, dy + 160)` -- which is what that function's
`pos - delta + last_delta` form expects. `Smap_ScrollBy` skips its vertical clamp when the screen id is 2,
so the same starting scroll clamps on the map screen and does not on the zoom screen; both directions are
asserted. **A forced click only fires on a mode-0 control already in state 1**, so an unlit control in an
auto-repeat group is silent. **The craft pass is not the alien-site discovery pass** -- it makes an INACTIVE
type-1 entry visible, and type 1 is the player's own craft record. `Smap_SelectMapTarget` is the other end
of unit 83's wire: it SETS `bMapSelectionFlag` (0x84498), the flag `Geoscape_DrawScreen` tests at 0x6C180,
and it is the arm that PAUSES the geoscape with no unpause at all (0x84588); its tail reads the line colour
a fourth time, after the insert and the removal, so line 0 is the flag-27 line the insert put there and both
0x187054 and 0x159E74 receive the value it carried. **1 deviation recorded:** the original indexes apCraft
with the line colour with no bounds test (0x844BC); the port checks it and skips the two ship steps while
the line rewrite and the pause still run. **A fixture lesson that cost a crash:** `PtrArray_DeleteAt` frees
the item it drops and `PtrArray_Remove` frees the slot array and installs a freshly malloc'd one, so a test
that hands in a static array crashes and one that keeps a second handle on the items double-frees after a
removal. 65 new checks (mutating the port's zoom step from 5 to 25 turns two of them red), 4 IDB renames,
2 function comments, 9 line comments, 0 new ledger rows. `strat` moves to 197/204 functions and 122,660 of
160,496 bytes (76.4%).
Unit 83 ported `Geoscape_DrawScreen` (0x6c16c, 3,476 B), the whole geoscape frame whose two draw leaves
unit 82 ported first.
**The backdrop is the radar backdrop.** 0x6C1A4..0x6C340 is instruction-for-instruction the body of
`RadarScreen_BlitMapBackdrop`, so the port now has one `Smap_BlitWrappedBackdrop` helper instead of two
copies of the same six blits. **`gOptionsScratch[0]` is the options menu id**, decoded from the shipped
lang.dat strings rather than guessed: 0 menu (TEXTS[1156] "Опции" over 1157/1158/1159 rows), 1 save slots
(1160), 2 load slots (1161), 3 quit confirm (1162 + 1163 "Да" + 1164 "Нет"), 4 volume (1031/1032 captions
with six snprintf'd digits at x 54 + 25*i); anything else reaches the tail only. `gOptionsScratch[4]` gates
the whole layer — zero skips the wash, the menu **and** the tail label (0x6C64C `BEQ loc_6CEF0`). A new IDB
enum `PufoOptionsMenuId` records the dispatch. **The copy loops name their record through their offset**: the
byte offsets are exactly `record * 205` (0x39CE7 = 1155, 0x39E81 = 1157, 0x39F4E = 1158, 0x33A68 = 1032,
0x3A41C = 1164), which is how each label is pinned without reading strings the IDB stores as zero-filled
labels. **Hex-Rays mislabels another stack slot:** 0x6C39C stores 0xB4 into the dword it calls `nKey`; it is
`nH = 180`, and the real `nKey` is the byte at 0x6C398 set to 0. **The shared tail:** every case leaves its
last string in `psz` and 0x6CEB4/0x6CEEC blits `pGeoscapeCtlA` and draws it centred at `120 - width / 2` on
`pGeoscapeCtlA->y + 1`; cases 0..3 leave "Отмена", case 4 leaves "OK", and case 0 alone blits that control
twice. The seven slot rows come from `MainMenu_pSlotCtx @ 0x156938`, not from TEXTS. **One deviation
recorded:** the original never initialises `psz` and its switch default jumps straight to the tail, which
then draws stack garbage; the port initialises it. **And the test method changed.** Every menu paints a
solid wash behind its labels, so a lit-pixel count is satisfied by the panel alone — the first draft of this
group had that defect. Each case now runs twice, once with the TEXTS record intact and once with its first
byte zeroed, and the assertions are on the pixels that DIFFER; a mutation (title band moved from y 63 to
y 80) turns the title check red. Measured along the way: `Text_DrawString(x, y)` inks rows y+3..y+12 with
this font. 77 new checks, 14 line comments, one `gOptionsScratch` comment, one function comment, one new
enum, 0 renames, 0 new ledger rows. `strat` moves to 195/204 functions and 120,508 of 160,496 bytes (75.1%).
Unit 82 ported the two leaves that were still missing from `Geoscape_DrawScreen` — `Smap_DrawStrategIcons`
(0x822c4, 1,060 B), the pass that draws every strateg entry as a mapbits sprite, and `Geoscape_DrawClockPads`
(0x5c5c8, 2,168 B), the date/time strip.
**It also corrected a reading this project had wrong.** `MOVS / ADDMI R3,R3,#1 / ASR #1` (the middle word
is `0x42833001`) had been recorded three times as a **ceiling** half, and the map click (unit 80) and this
icon pass both implemented `ceil(n/2)`. ADDMI is add-if-**minus**: the +1 is skipped for every
non-negative value, so a positive extent is halved by plain `n >> 1` and the +1 only gives a negative
extent C's round-toward-zero (`(-5 + 1) >> 1 == -2 == -5 / 2`). Hex-Rays' `/ 2` was faithful; the port's
ceil was not, and on an odd map size the view and every strateg icon sat one pixel off. Both sites now use
`n / 2`, the 401x401 recentre test asserts 200 instead of 201, a new 401x401 icon check pins the reading in
the draw, and the IDB carries the correction at 0x822c4 / 0x6c348 / 0x8c698 plus a line comment at each of
the four halving sites (0x5EB44, 0x6C1A4, 0x822EC, 0x846B4). **What only the ARM fixes.** 0x82318
`MOV R0, #0x20` forces palette slot **32** into `Map_nGlyphPalBase` while the `Font_DrawGlyph` nPal argument
stays R9 = 0 — and `mapbits.pal` is palette slot 32 in main.dat, so the two confirm each other. Zoom scales
**both** axes (1/1.333, 1/1.694). The world→screen wrap is one conditional subtract per axis with a `>` test
(0x824BC/0x824C0, 0x824CC/0x824D0), so a coordinate exactly equal to the map size is NOT wrapped. The
animation phase is the **low halfword** of `nUiSlot0AnimPhase @ 0x113A18` (LDRH at 0x82634). **Measured, not
assumed:** mapbits has 16 frames (nMapBitsBase 256, nTotalSprFrames 272) and frame **+6 is blank in the
shipped main.dat** — lit counts for base+0..+15 are 32,32,20,20,20,20,**0**,32,32,32,28,28,28,28,28,28 — so
the test asserts the blank frame and that every checked frame paints pixels, which is what keeps a hash
comparison from passing on an empty box. **The clock strip is pads, not glyphs:** the ten digit pads are
`apClockDigitPads @ 0x186BF0`, indexed by digit value, and the literal pool at 0x8662C pairs 0x186BF0 with
the names "b0".."b9". That exposed a **port-side misnomer** — `pufo_geouimanage.{c,h}` and
`pufo_geoiinit.{c,h}` documented that array as layer pads, so it is `apClockDigitPads` /
`PUFO_GEOUI_CLOCK_DIGIT_PADS` now (the singular `pSmapChromePad @ 0x186BE0` keeps its name). The day tens
digit is SET always but BLITTED only when non-zero (0x5CA00 / 0x5CA14 / 0x5CA18); the digit split calls
`_rt_udiv` with `MOV R0, #0xA` first (0x5C9D0 / 0x5C9D8), the divisor-first convention confirmed a fourth
time. Weekday names are TEXTS[1244..1250], the day/month separator TEXTS[1251], months TEXTS[1252..1263];
the weekday is centred (`124 - width / 2` at 0x5C9B0) while the month sits at a fixed 130,287. **One
deviation recorded, not hidden:** the original copies a TEXTS record into a 52-byte stack buffer
(`psz[52]` at 0x5C5AC) with an unbounded byte loop; the port caps it at 51 + NUL, and that is not a ledger
row because the overflow is unreachable with the shipped data. 52 new checks, 8 IDB renames, 6 size fixes
(0x186BBC, 0x186BF0, 0x186C1C, 0x186C20, 0x186E40, 0x159618), 3 function comments, 4 halving-site comments
and 3 corrected comments, 0 new ledger rows. The port's own
clock-block claim was wrong too: the block is `gUiRootPanel(0x158890) + 0xBE4 + 4 * 101 = 0x159608`, not the
0x158A14 `pufo_clock.h` said, so `nTotalSec` is 0x159618 and the pause byte 0x15961C.
Unit 81 ported the geoscape event pass —
`Strateg_ConfirmInterceptTarget` (0x7fb98, 280 B) — and wired it into the shell as `World_ProcessEvents`, so the
geoscape tick now runs the real per-item update instead of the stub. **Three things only the ARM showed, and all
three change the answer.** `_rt_udiv` takes the **DIVISOR in R0 and the DIVIDEND in R1** (`Geoscape_AdvanceTime`
loads 60 / 3600 / 86400 into R0 against an elapsed-seconds R1), so travel completion is
`elapsed / 60 >= uTravelMin` and never `60 / elapsed`. The interpolation constant at 0x80A80 / 0x80B98 / 0x80C70 /
0x80D70 is the double 0x3F911111_11111111, **exactly 1/60** (exponent field 0x3F9 = 2^-6, mantissa 1 + 1/15). And
the arrival dispatch **re-reads** `Ship.nStrategIdx` and calls `PtrArray_Get` for each of its six type tests
(0x8036C..0x803F8) — the pseudocode's single `pTargetItem` hides that the original would follow a list that
shifted in between. A removal does not advance the loop index (0x805FC jumps back to 0x7FCF0 with the counter
unchanged, because the tail shifted into the hole) and a craft-to-craft arrival tail-calls the epilogue at
0x80FCC, ending the whole pass there. **ORIGINAL BUG (0x7FBE8):** the assault offer asks `Craft_CountSoldiersAboard`
with `Ship.bId`, while that callee matches its argument against `Soldier.bUnk183`, an apCraft INDEX; the two agree
only while no craft has ever been removed. **The naming fix and the defect it exposed:** 0x159E74 is an apCraft
INDEX — 0x3DE84, 0x3DF5C and 0x3DF88 all index `Map.apCraft` with it and only then read `Ship.nStrategIdx @ +0x38`
— so the port's `nStrategIndex` field became `pnMsgShipIdx` / `nMsgShipIdx` in nine environments, and that rename
showed that all three `Tact_Shutdown` removals passed the 0x159E74 value as the **list** index instead of the
craft's `nStrategIdx`: a mission ended by a craft whose list position and craft index differ removed the wrong
contact. Fixed on both paths, with two tests that fail on the old code. 105 new checks, 2 IDB renames
(`bSpawnReplacementOnce`, `pStatusPanel`), 1 size fix (0x159E74, 1 byte → dword; both save paths move 4 bytes),
3 comments, 1 ledger row.
Unit 80 ported the other geoscape map click — `Smap_OnPointerUp` (0x84688, 732 B), the handler `Geoscape_Draw`
installs next to unit 79's. `bMapSelectionFlag`
picks the arm and both arms share the tail (`UiPanel_RemoveLine(0)`, then unpause if the list is empty). The
selection arm launches `Ship_LaunchIntercept` on the strateg entry line 0's colour slot 0 names, with the ship
index from `[0x187054]`, and clears the flag; the recentre arm centres the view on that same entry. Four things
only the ARM showed: the half-extents are plain C halves (MOVS / ADDMI #1 / ASR #1, and ADDMI fires only on a
NEGATIVE value), so a 401-wide map centres at 200 — this unit recorded them as ceilings and unit 82 corrected
that; 0x846DC and 0x84700 read colour slot 0 **twice** so nX and
nY come from the SAME entry; nMode 1 unscales both coordinates by 1/1.333 and nMode 2 by 1/1.694, the exact
reciprocals of the `Geoscape_Draw` zoom factors; and `nScrollY` has **two** replace arms — the RSBMI at 0x8484C
(testing `nY - 160`, whose N flag comes from the `ADDS` at 0x84838 and not from the value just stored) and the
zoom-strip height test at 0x84870 on `uHeight` of the PadImage at `[0x186BBC]`. The crosshair x is algebraically
always 120 because `nScrollX` is exactly `nX - vW - 120` with no replace arm. **ORIGINAL BUG (0x84894):** the
crosshair y wraps by `nMapH` with ONE conditional subtraction, so a value more than nMapH above the range stays
out of range (200 on a 100x100 view); unreachable on the shipped 400x400 map, reproduced by a small-view test.
35 new checks, 3 IDB renames (`nSmapCrosshairX`, `nSmapCrosshairY`, `bSmapCrosshairPending` — written only here,
read only by `Geoscape_Draw` at 0x8C6A8), 5 comments, 1 ledger row.
Unit 79 ported the answer to a geoscape message line — `Smap_ResolveInterceptUI` (0x84964, 340 B), the click handler `Geoscape_Draw` installs, which reads the
flag on message line 0 and decides what the pending map order becomes. `GameTime_Set5Sec` and a cleared
`bInterceptDone` first; flag 27 sends the craft in colour slot 0 home through `Ship_RetargetTravel`; flag 13
rewrites the line (insert a flag-11 line at index 1 carrying line 0's colour **slot 1** masked to a byte, drop
line 0), sets `bInterceptDone`, pauses and returns; anything else clears the four selection bytes, runs three
dismiss tests on 23/21/22, removes line 0 again, unpauses only if the list is empty, and clears
`bFinalMissionCtlArmed`. Three things only the ARM showed: **0x84A10 branches to 0x84A9C, past the
`bFinalMissionCtlArmed` clear at 0x84A94**, so the flag-13 path is the only one that leaves the final-mission
control armed; the three dismiss tests each re-read line 0 and `Smap_DismissSelected` removes line 0 itself
(0x84454), so they are not three tests of one flag — a match shifts the list and the next test sees the line
underneath (`[23, 21]` dismisses two different craft); and **ORIGINAL BUG (0x84A80)** — the tail removal is
unconditional, so after a dismiss it eats the NEXT message line and the clock unpauses a step early. The naming
follow-through from unit 78: the port's `GeoscapePointerEnv` field for 0x187068 was `pbTargetSelectActive`, a
name from before unit 78 showed what that byte gates; it is `pbFinalMissionCtlArmed` in the env, both handlers
and the test fixture now, matching the IDB, and `byte_18709C` is `bInterceptDone`. 38 new checks, 1 IDB rename,
5 comments, 1 ledger row.
Unit 78 ported the options overlay's row confirm —
`OptionsScreen_OnConfirm` (0x6cf18, 260 B), the click handler `OptionsScreen_Open` installs on the names
panel at 0x6D2E8 — so the overlay is a ported screen with a ported handler. The row is the names panel's
`nUnk3032` and the craft is `pPanelName->anLineAux[nUnk3032]`, written into both selection globals before
anything is tested; the gate is the **stores** panel's aux dword at the **same** index (0x6CF5C reloads
`nUnk3032` from the names panel), so a craft that is not ready still selects itself and then does nothing
else at all. Ready: `bMapSelectionFlag`, `Sound_Play(7, 4)`, a flag-27 message line carrying the craft index
(the line `Smap_ResolveInterceptUI` answers with `Ship_RetargetTravel`), the final-mission arm, and
`GameTime_Pause` on both arms of the topic test. **The two constants behind that arm are pinned against the
shipped data files, not guessed.** lang.dat `TEXTS.DAT` is a 2-byte header plus 200-byte records
(330002 = 2 + 200 × 1650) against the 205-byte in-memory stride; read that way TEXTS[415..419] are the five
CraftType names, so `Ship` +2 == 2 is the **Avenger**, not the Interceptor. main.dat `RESEARCH.DAT` is 144
records of 34 whose first u16 is the topic's TEXTS name index, and record 28 is 448 = "Цидония или разорение"
at cost 6000 — the Cydonia topic. `Geoscape_Draw` @ 0x90908 blits `pFinalMissionCtl` and draws TEXTS[1236]
"Финальная миссия!" while the byte is set, and both pointer dispatchers hit-test that control first, which is
what `byte_187068` → `bFinalMissionCtlArmed` says. The same lang.dat read confirms unit 77's thirteen labels
and they are IDB comments now. 29 new checks, 3 IDB renames, 21 comments, 1 ledger row (a latent
out-of-bounds `apCraft[-1]` at 0x6CFD8).
Unit 77 ported the overlay itself — `OptionsScreen_Open` (0x6d0ac, 3,160 B), the last callee
`Geoscape_CloseUI` case 18 reached through a hook, so the whole case-18 chain is ported code calling ported
code. It is a toggle on `bOptionsScreenOpen`: the close half clears `nGeoscapeScreen` and the flag and
releases four panels plus the two scroll controls, and **that flag clear at 0x6DC40 is what ends the
case-18 / SaveAudioSettings recursion** — case 18 re-reads the flag at 0x8BD10 only after the callee returns,
so an inner `OptionsScreen_Open(-1)` makes the outer pass skip its own options-open arm. The open half builds
four parallel lists (names, stores, region, cargo) and writes one row per craft across all four. Four things
the ARM showed: the base-walk bound is the signed byte at **0x159D80** reached as `[abFacilityGrid,#0x618]`
(because the `LDREQ` of `0x1139B8` at 0x6D0F0 is conditional and did not run — Hex-Rays hides this);
`stru_AB9B0` is **not** two identical records but "up"/"sup" at (20,180) and "down"/"sdown" at (50,180); the
stores labels are TEXTS 149–152 and the region labels TEXTS 1165–1167, 1036 and 1168–1172, switched on
`BaseRec.bRegionId`, the same byte `FundingScreen_Init` groups; and **ORIGINAL BUG (0x6D884)** — the region
switch has no default copy and 0x6DA68 then `strcat`s onto the uninitialised 104-byte local, so any region
outside 0x80..0x88 prints stack garbage. 51 new checks, 13 IDB renames (the TEXTS records), 1 ledger row.
Unit 76 ported the callee the previous unit's recursion pointed at — `Options_SaveAudioSettings` (0x6b7ac, 336 B), the options screen's save-hotspot handler
and the function `Geoscape_CloseUI` case 18 calls at 0x8BD00, so the pair is now two ported functions calling
each other rather than a function and a hook. Three things it settled. **All three of its arguments are dead**:
0x6B824 overwrites R0 with `Map_nSoundVolume` before the first call and R1/R2 are never read anywhere, so the
block, the signed byte and the base-loop residue the close hands over reach a callee that ignores them. **The
recursion is not the two-level shape Hex-Rays suggests**: this function's own close at 0x6B7C4 is gated on
`bOptionsScreenOpen` — a global case 18 never touches — so it runs *first*, that pass runs
`OptionsScreen_RebuildWithSound` whose `OptionsScreen_Init` clears `gOptionsScratch[4]` at 0x6C0FC, and the
re-test at 0x6B7D4 therefore fails: 0x6B7F0 is never reached in the real flow. `test_closeui.c` pins both
halves by the sample count — 17 once when the controls byte is cleared, 17 twice when it survives. And the tail
is a clock effect: a clear `bMapSelectionFlag` clears 0x15961C, which is `GameTime.bPaused`. 49 new checks, 5
IDB renames — one of them a mistake this unit made and corrected before the commit (`unk_186BB4` / `unk_186BB0`
were first named as an options-only pair; they are a geoscape control pair reused by the manuf, research, park
and options screens) — and the port's own wrong comment on `ScreenScratch.nLineAux145` fixed: the byte is
`gOptionsScratch[0]` at 0x159E88 offset 0, not `gUiRootPanel.anLineAux[145]`, an owner that does not exist in
the IDB.
Unit 75 ported the screen close dispatcher —
`Geoscape_CloseUI` (0x8b94c, 2,100 B), the target of the 13 screen-close thunks that had been hook seams ever
since the screens were ported. It runs a head (`Sound_Play(17, 4)` — the second argument is 4 at all 7 call
sites, so `Sound_Play` takes it and the port's one-argument reading was wrong — then `dwActionFail = 0`, with
both popup bytes cleared through the shared tail), the speed re-apply chain, a per-base pass that re-reads
`nBaseCount` on every iteration (0x8BA70) and rebuilds the needed-flag grid at `grid + 444 + 144 * base`, and
a switch on `nScreenId` with **17 live cases** (1..12, 15, 16, 18, 19, 20); 13, 14 and 17 take the default
return, which is why a radar-list close leaves the screen id alone. The chain is the interesting half: it
tests 5, 60, 300, 1800, 3600 and 43200 while the table its setters write is
`g_aGeoscapeSpeedSec = {5, 60, 300, 1380, 2460, 3600}`, so 1800 and 43200 are values no speed control can
produce and every arm that does fire rewrites the tick — and **0x8B9E0** makes the 3600 arm call
`GameTime_SetSpeedIdx4` and then branch into the 43200 arm's `SetSpeedIdx5`, so a 3600 tick lands on index 5
and re-arms the sixth control. Case 19 adds the five funding-bar slots in bar order and tests `BGE` against
0xFFD23940, so bankruptcy is funds **strictly** below -3000000, and case 18 and `Options_SaveAudioSettings
@ 0x8bd00` turn out to be **mutually recursive** (0x6B7F0 calls back into the close) — the recursion ends only
because case 18 cleared `bOptionsScreenOpen` first. 174 new checks in `tests/test_closeui.c`, 11 ledger rows,
7 renames and 4 IDB size/type fixes.
Unit 74 ported the map select test —
`Smap_HitTestStrategSelect` (0x82be8, 3,088 B), the test the press dispatcher calls twice on one press
(0x87C10, 0x87C94), now in `pufo_baseui.c` beside `Geoscape_OnRadarListClick @ 0x854c4` on its own
`SmapHitEnv`. It is a strict strip gate (a click exactly at `320 - uHeight` still scans, and the gate runs
before the line clear), a **16x16 window of cells** walked outside the item loop — so the built lines follow
the cell offset, not the list order — item coordinates scaled by **1/1.333** at zoom level 1 and **1/1.694** at
level 2, one status-panel line per matching item with the item index in `anLineAux`, the same six-branch
dispatch the radar-list click runs for exactly one line, a multi-select layout for more, and a flag-27 tail that
writes the pending click at 0x186414 / 0x186418 — the writer `Smap_ConfirmCraftTarget`'s note had called
unported. Three things it fixed rather than inherited: `BaseRec.bUnk4` is loaded **signed** in both copies of
the base search (0x82ED4, 0x6AC68) while the argument is masked to a byte, and the port compared it as `u8`;
`Line_Write` now masks all four colour slots to a byte as 0x5A2FC..0x5A31C do, which matters because 0x836A0
stores -1 into 0x187054 and 0x836D0 appends it (255, not 0xFFFFFFFF); and `unk_ECDB5`, `unk_ECE82` and
`unk_ECF4F` are **TEXTS records 1145, 1146, 1147** (`aTextRecs + 205 * n`), renamed in the IDB. 132 new checks
in `tests/test_smaphit.c` — the two scalers pinned at the window edge (591 scales to 443 where 1/1.33 gives 444;
2631 scales to 1553 where the product of the scroll factors gives 1554) because a ±8 window swallows a 0.2%
error anywhere else — and 10 new ledger rows.
Unit 73 ported the pointer release — `Geoscape_OnPointerUp` (0x89450, 6,992 B), the release half of the same
dispatcher, on the same `GeoscapePointerEnv`. It is not the press with `bDown` flipped: it takes only the 0x1864F8 block, reads the
cursor pair the press stored (LDRSH), and every hit test it makes is a release (x = y = 0, bDown = 0,
bForceClick = 0 — a release ignores the hit box). It owns the shortfall gate on 0x1870C8 (read as a DWORD at
0x89584, the only such reader in the image, cleared unless the screen is 6, 9, 10, 11 or 12), it plays
`Sound_Play(17)` in almost every world-arm branch where the press plays it in two, it clicks the scroll lists
where the press only tracked the hovered row, and it ends the world arm by clearing the target-select flag and
releasing all thirteen root controls on any screen but 2. Three original findings: the screen-17 tail's
conditional clear of `byte_1870F8` can only fire on a byte that is already 0 (0x8ac9c), `bPopupVisibleB` cleared
through a conditional store (0x8a188), and three arms that hand back an address instead of a result (0x895c8 /
0x89b90, discarded by `Input_OnPointerUp` at 0x14910). It also caught a wrong pair of addresses in the port: the
soldier-screen slots are `apSoldierPanelCtl @ 0x1598B0` and `apSoldierFindCtl @ 0x1598B8` (ARM `[base,#0x148]` /
`[base,#0x150]`), not 0x159928 / 0x159948, which have no xrefs at all. 245 new checks in `tests/test_geopointer.c`
— a release is observed by arming every control first and asserting which ones it put back down — and 6 new
ledger rows. IDB: comments on the shortfall gate, the three residues, the null-check asymmetry, the slot
immediates and the signed base-count product.
Unit 72 ported the geoscape pointer dispatcher —
`Geoscape_OnPointer` (0x872d0, 8,576 B), the largest UI function in the binary and the only reader of the 13-slot
`GeoscapeScreenBlock` control array. It stores the cursor into the 16-bit globals `nSmapCursorX` / `nSmapCursorY`
(0x186BD0 / 0x186BD4, STRH — every arm then reads the stored halfwords, not its own arguments), runs the screen-0
main-menu band (a status-line flag with 30 < flag < 50, flag 37 adding the confirm pair), and dispatches over screen
ids 0..12 and 15..20, with 13 and 14 falling to the default arm that hands back the pointer x. The world arm is an
eleven-test chain over the status-line flag (values 10..23 and 60) with the target-select, radar-panel and
multi-select-panel gates interleaved, then the drag block: the globe area is `320 - <zoom overlay pad uHeight>`, a
press with `nRadarCooldownR @ 0xAC218 < 6` inside it resolves the tile under the pointer and sets the cooldown to
255, otherwise the drag starts, and a message on the status line stops the globe from scrolling. Two facts only the
ARM gave: the base-strip tap cell is `(v + 31) >> 5` for a negative v, not floor division, and `nRadarCooldownR`
doubles as the globe tap gate. Five original defects recorded — `Smap_HitTestStrategSelect` called twice with the
same arguments on one press (0x87C94), the world tail returning the drag flag or the screen id itself (0x87D84), a
`<<16 / >>16` sign-extend on a value that is already a halfword (0x88F4C), the options page-2 arm being byte-for-byte
the page-1 arm (0x89210 / 0x89270) and a `CMP #0 / BLS` on a u8 (0x88524) — plus three deviations: only the ten
base-strip slots are null-checked in the original (0x87E38), `Smap_HitTestStrategSelect` (0x82be8) is ported but
reaches the dispatcher through `pfnHitTestStrategSelect` because it runs on its own `SmapHitEnv` the app does not
build yet, and screen 10 tracks and clicks the buy/sell panel its own site-info controls do not belong to
(0x889F0). 235 new checks in `tests/test_geopointer.c`, 8 new ledger rows. IDB: `byte_187074` →
`bGeoscapeDragActive`, comments on the entry, the drag gate, the tap division and the five defects.
Unit 71 ported the step legality —
`Unit_CanStepTo` (0x27bb8, 14,716 B), the last blocker in the tact subsystem and the largest single function
ported so far. It seeds the candidate from `aDirDeltaXY[3 * g_nStepDir]` itself, runs a head every direction
shares (in range, not blank, and an occupant that is the swap partner, the mover, or a foreign unit that is not
flagged at `bUnk148`, not operative and not unit type 16 or 17), then dispatches through a ten-way switch, with
a second ten-way switch for a large unit at 0x28A44. Every arm runs `Map_ProbeStepOccupants` first except the
two vertical directions, which never call it. The arms' cell lists are the substance: a diagonal direction tests
the walls of the cells it passes between as well as the cell it enters, and a large unit's arms test all four
cells of its 2x2 footprint. **The seam had to change:** `Path_ExpandCell` takes the neighbour through
`g_pStepDstCell` (read at 0x2b768), and a downward re-anchor moves that cell without moving
`g_nStepDstLayer`, so the port reports `g_nStepAdjLayer` for the horizontal directions and `g_nStepDstLayer` for
the vertical pair. **The correction it forced (0x2b75c):** the +4 layer surcharge and `bFlags29` bit 3 both come
from the probe's `g_bStepReanchored` report, not from the search flag; the port used the flag for both, which
charged the surcharge to re-anchored horizontal steps and set bit 3 on the wrong condition. Three original
defects recorded: the large arms pass `nLayer = 0` to their footprint corner's bounds test (0x28c90), the arms'
occupant gate is weaker than the head's (0x27e0c), and the large vertical arms chain the same two floor tests
four and seven times over (0x2ab80). 48 new checks, 4 ledger rows. IDB: `Unit_CanStepTo` given the `Map *`
signature and comments on the head, the three defects and the surcharge gate.
Unit 70 ported the step probe —
`Map_ProbeStepOccupants` (0x2778c, 1,068 B), the half of the step test that decides *which cell* a step
is before any wall or occupant is examined. A step is not always the cell the direction delta points at:
the probe tries a downward re-anchor (0x27990, candidate floorless, current cell floored, search flag
clear, candidate above layer 0) and an upward one (0x27a6c, current-cell ceiling with `iCeilParam < -15`,
cell above the current one floorless, cell above the candidate floored), and if the cell it would move
into has no floor the step is refused outright rather than left unanchored (0x27a10). The two anchors are
not symmetric (0x27a48): the downward one stores the new cell and drops `g_nStepAdjLayer` but never touches
`g_nStepDstLayer`, while the upward one updates the cell and both counters — and the direction arms index
with `g_nStepAdjLayer`. A large unit drags a second column through both anchors, reached by adding one to
the `apCells` index rather than to the coordinate (`R7 = 0x1533C` at 0x27840, 0x278ac, 0x278fc, 0x27930,
0x27958), and that is where the original breaks: for a large unit stepping due east the cell the upward
anchor wants floorless is the same cell the preceding test requires to be floored, so the anchor can never
fire in that direction while a small unit with the same geometry does re-anchor (0x27ae4 against 0x27a94).
Three more reads of layer-gated state are unguarded — `var_14` (stored only for a candidate layer below 3,
read at 0x27a94), `g_pStepCellAbove` (written by `Path_ExpandCell` only for a current layer below 3, read at
0x27a88) and `var_8` (stored only for `g_nCurLayer < 3`, read at 0x27ae0) — plus two dead leftovers: a third
`Unit_bIsLarge` call with an unreachable `BEQ` to the return-zero path (0x27a24) and the non-large path
loading `R11`/`R6` from a slot only the large path writes (0x27988). The probe is a global-state machine in
the original, seeded by `Path_ExpandCell`, `Map_ComputeFov` (0x2deac), `Map_ScanBlastFootprint` (0x32cd4) and
`GameTick_Step` (0x27324); the port keeps the same shape as one explicit `StepProbe` struct. 38 new checks,
7 ledger rows. IDB: twelve step globals named (`g_nStepCurX`, `g_nStepCurY`, `g_nStepDir`, `g_nStepDstX`,
`g_nStepDstY`, `g_nStepDstLayer`, `g_pStepCurCell`, `g_pStepDstCell`, `g_pStepCellAbove`, `g_nStepAdjLayer`,
`g_bStepReanchored`, `g_nStepCeilParamNeg`) and `Map_ProbeStepOccupants` given the `Map *` signature.
Unit 69 ported the path replay —
`Unit_PathToCell` (0x2c2a8, 1,052 B), the move tail the map click and both enemy-AI paths were waiting
on. The flood fill it drives was already ported, and that is where the one real defect was: the port
dropped `Map_ComputeReachable`'s sixth argument, which the original stores to the global
`g_bPathSearchFlag` @ 0x10db84 (0x2b850/0x2b860) for `Path_ExpandCell` to read back (0x2b6ac). Set from
`g_aUnitTypeRecord[4 * nUnitType]` — types 3, 7, 8, 11, 13, 18 — it lifts two rules at once, the +4 layer
surcharge and the "no floor sprite, do not expand" gate, and the port only ever showed the first. It is an
argument now and both effects are asserted. The rest is the walk back through `MapCell.bPathDir` (opposite
compass direction, or the layer delta for a recorded vertical step) with its layer re-check on `bFlags29`
bit 3 — the layer above needs no floor at the old column and a cost at the new one, the layer below needs a
ceiling with `iCeilParam < -15` and a cost — and the replay, which takes its facings from
`g_aFacingFromDelta` @ 0xa7b00 (`3 * (3 * dz + dy) + dx + 13`, the compass facing except at the pure
vertical delta, where it reports 8 up and 9 down), turns only when the facing changes, runs
`Map_QueueDoorAction` per step, and emits STEP or VERT_STEP. `TactState_EnqueueVertStep`'s second argument
turned out to be a layer delta that the original stores in the entry's `nFacing` slot (the slot the vertical
step's draw offset reads at 0x50FBC); renamed to `nLayerDelta` in the port and in the IDB. 50 new checks,
6 ledger rows. IDB: `byte_A7B00` → `g_aFacingFromDelta`, `dword_113980` → `g_nCurLayer`, `unk_10DB84` →
`g_bPathSearchFlag`, and the three Map scratch fields named (`Map_apPathCells` @ Map+0x4a44,
`Map_nPathCount` @ Map+0x4e44, and `Map_pPathDestUnit` @ Map+0x150f4 — the swap partner
`Unit_CanStepTo` compares cell occupants against).
Unit 68 ported the tactical map click and the mode-6 confirm — `Tact_HandleMapClick`
(0x1512c, 1,104 B) and `TactAction_ConfirmMeleeMode6` (0x17bb8, 412 B): the one-layer-down cursor cell
(`apCells[nLayerScratch-1][nCursorY][nCursorX]`, base 0x11AF8), the four asymmetric latch arms (1 and 2
kick the queue, 3 does not, 4 clears the latch, 0 has three exits before its move), the -23 ceiling
threshold, the eight-target cap tested after the look control is armed, and the two melee gates that
disagree — the map-click arm refuses operative occupants, the action-button arm never tests operativeness.
138 new checks, 5 ledger rows. It also fixed two wrong text-record indices the port already carried:
`PUFO_TXT_OUT_OF_AMMO` was 0 where the original's pointer is record 6, and `Ui_ConfirmSpecial9` pushed
that constant twice where the original pushes records 11 and 12; both are now named constants and both are
asserted. `unk_128A48` is renamed `Map_pMeleeTarget` in the IDB (private scratch of the mode-6 confirm, all
six references inside it), the eight tactical text globals are named, and a `TactActionLatch` enum was added.
The previous unit ported the enemy-turn bracket —
`BeforeEnemyStep` (0x4b9b8, 2,792 B), `Tact_UpdateUnitControl` (0x4a444, 788 B) and `AfterEnemyStep`
(0x4c4a0, 1,368 B): the six-pass face loop that charges wall A twice when the west neighbour burns, the
`counter + step` term the store adds a second time, the burn run that probes against its own axis, the
burning-state gates on both countdowns, the reserve spawn's swap-remove, and the `SideLists` count
write-back. 109 new checks, 12 original findings recorded, and the coverage delta is exactly the three
functions' 4,948 bytes. Two of the port's own first-draft errors surfaced only because the tests asserted
values: the face loop had been modelled as six counters aliasing `nPathCost`, and `abFaceDamage` was reached
through the original's ILP32 byte offset, which on Win64 lands on `pWallSprB`. The unit before it ported
`Tact_EnemyAiTick` (0x4cbe0, 3,440 B) — the enemy AI driver: the byte-not-pointer at `TactCtx272+0x100`, the unit gates, the
`g_aUnitTypeFlag3` split between the melee and throw/fire branches, the neighbour-melee hand-off, the
`cost * 2/3 + 8.0` affordability gate, the RMP node walk and its tail. 99 new checks, 3 original bugs recorded,
and the coverage delta is exactly the function's 3,440 bytes. The gates caught a second inflation of mine on the
way: citing the three `TactItem_ctor` thunks with `@ 0xADDR` credited 36 bytes of code the port does not own, so
those citations are now prose and `audit_names` went back to its two known flags.
The unit before it ported `EnemyAi_TryFireOrWander`
(0x4abc8, 1,208 B) — the low-morale AI: the large-unit early exit, the one-handed weapon test, the random fire
burst with its `min(nAmmo,3)` / capacity-255 shot count and its two action tables, the same-column redraw rule
Hex-Rays gets wrong, the weapon drop and the two floor walks, and the `Unit_PathToCell` hook. 79 new checks,
7 original bugs recorded, and `Map_nAiFireX/Y/Layer` (0x128A20/0x128A24/0x128A28) named in the IDB as this
function's private scratch — all twelve of their xrefs are inside 0x4abc8, so they are not a Map contract.
The coverage delta is exactly the function's 1,208 bytes, which is the check the "Measured state" table asks
for. The previous unit (`Tact_TickActiveUnit`, 0x54130) moved **nothing**: `0x54130` was already credited by
the `@ 0x54130` citations in the move- and fall-processor comments, so porting it added no citation and only
`audit_names` changed (3 flags → 2). That is worth understanding rather than reading as a mistake — the metric
counts citations, not code. It also bit in reverse: mid-unit the numbers briefly read 899 / 436816 because a
comment cited `EnemyAi_TryFireOrWander @ 0x4ABC8`, 1,208 B of then-unported code; that citation is now the
`Name (0xADDR, unported)` form the metric ignores, and the same 1,208 bytes reappeared legitimately once the
function was actually ported. The metric counts every `@ 0xADDR` citation, which is why unported callees must
be written in the paren form — see "Separate issues" in `PLAN.md`. The unit before it ported `Tact_TickActiveUnit`
(0x54130, 1,868 B) — the queue walker `GameTick_OnTimer` drives: the empty-queue phase call, the
`bFlag0` fire-or-move choice, the busy multi-entry pre-dispatch, the per-command camera follow and its
60-voxel recentre stride, the AI-action clear, the kill-credit collection, the opportunity-fire epilogue
and the busy re-run loop. 96 new checks, `pAiActionUnit` / `uKillCredit` / `bAiActionActive` named in the
IDB's `TactState`, 6 original bugs recorded in the ledger, and `Map_BuildMoveActionCtx` now wires the two real
trace-exclusion sources (`Map.pSelUnit @ 0x15334`, `Map.pThrowTargetUnit @ 0x150F0`). The unit before it
ported `Tact_ProcessFireAction`
(0x50adc, 8,192 B) — the fire-phase interpreter, twelve command cases, the ten muzzle-relative flight
samples, the two disagreeing 1/12 conversions, the throw's ballistic-drop landing, the cell-flag decay
sweep — and closed the two producer-side gaps it reads but nothing produced yet: `Map_dTravelSave` and the
`Map_adMtX/Y/Z` sample arrays, both now written by `Tact_TraceProjectileArc` and by `MoveThrow`. 150 new
checks, 17 Map globals named, 7 original bugs recorded in the ledger, and three stale offsets plus two wrong names in the rename
ledger corrected. The unit before it ported `Unit_ApplyFallDamage`
(0x4a7b0, 1,000 B) and `Tact_ProcessFallAction` (0x52adc, 5,716 B) — the fall processor and the tail half
of every tactical action: the three fall refusals, the footprint-aware descent, the 0.4f soft-float damage
formula, the landing sweep, the twelve-case command switch, the psi gate and its morale formula, the psi
panic tail, the chryssalid transform, the medikit ladder, the human and alien melee, and the thrown-item
landing. 189 new checks, eleven Map globals named, and seven original bugs recorded in the ledger. The
unit before it ported `Tact_CheckBattleEnd` (0x34fbc, 5,748 B) — the debrief emitter with its four early
exits, 19 debrief lines, loot commit into `aSmapSites` and `ObjectRec.uRecovCount`, and the
Mars-to-alien-base restart tail — naming `Map_bEnemyTurnActive @ 0x113A13`, `Map_nBannerFadeTicks @
0x113A1C` and `pTactCtx272 @ 0x157180`. The unit before that ported
`Tact_ClearSoldierBattleFlags` (0x346e0, 2268 B) and its callee `Soldier_PromoteByTypeScores` (0x47280,
1096 B) — 3364 bytes, 106 new checks — named `Map_bPromotionReport @ 0x1153D8`, `g_aClassStatGrowth @
0xAA930`, `nPromoteBestScore @ 0x15880C` and `nPromoteBestPick @ 0x158810`, and fixed the port's
auStatBase index (see §8). The unit before it renamed `unk_128A8C` → `Map_bMissionVictory` in the IDB and
`pbMissionAborted` → `pbMissionVictory` in the port, and named four more Map fields
(`Map_szMissionCaption`, `Map_szMissionScore`, `Map_nDebriefLine`, `Map_nMissionOutcome`). The unit
before that ported the four
post-battle tallies — `Tact_TallyBattleStats` (0x3348c), `Tact_TallyUnitResults` (0x33780),
`Tact_TallyItemRecovery` (0x33c80), `Tact_TallyCraftRecovery` (0x344b4), 4,548 bytes and 91 new
checks: the debrief label table transcribed into the IDB, `Mc1Rec+0x1E` named `bFacilityKind` with a
`TileFacilityKind` enum, the `UnitType` / `UnitRank` enums derived from the alien slot table, and 37
`Map_apObjectsNN` renames. Before that, `Inv_CloseAndCommit` (0x15a48, 1268 B) closed the `tact-ui`
subsystem at 52 of 52 functions and 43,764 of 43,764 bytes.

The five rows of that report are five countries — lang.dat TEXTS[1302..1306] are США, Евро Альянс,
Азиатский Альянс, Австралия, Африка — and they are **not** in race-score order: the row → slot
permutation is 0, 2, 3, 1, 4, which both `FundingScreen_DrawExtended`'s five `Funding_DeltaToArrow`
calls (0x5B804..0x5B8A4) and `FundingScreen_Init`'s five share stores (0x5C34C..0x5C38C) agree on. The
share, bar and level arrays are slot-indexed while the baseline array at 0x1595F0 is row-indexed. The
money is `bonus = (pending / (sum(scores) + 1)) * multiplier`, `pending += bonus`, then
`share = pending / (sum + 1)`; every base adds `3 * share` to the slot its `BaseRec.bRegionId` maps
to. The funding screen's own names panel now confirms the same slots from the drawing side: it copies
TEXTS[1302], TEXTS[1305], TEXTS[1303], TEXTS[1304], TEXTS[1306] — USA, Australia, Euro, Asia, Africa —
i.e. in slot order, while the report rows use the permutation with titles TEXTS[1302 + row].

Two traps worth knowing. `nMonthCounter @ 0x159648` is incremented at 0x5D170 *before* the recompute,
and `BLE` at 0x5C230 and `BNE` at 0x5C250 land in the **same** block — so month 0 is not "no upkeep"
but the heaviest upkeep (-1100 / -300 / -800), identical to month 6 and above; months 1..3 give
-300/-100/-200, month 4 -500/-200/-350, month 5 -700/-500/-300. And `g_nFundArrow*` is not an arrow:
it holds `Funding_ScoreToBarLevel`'s result, a money amount of ±25000 or ±50000 times the row's
3/5/4/1/3 factor, while the draw re-derives the real -2..2 arrow from the share and the baseline and
uses the stored dword only for its sign. A bar that ends negative has its arrow zeroed with it
(0x5C4D4..0x5C53C), so a country in the red shows no arrow at all. `Funding_ScoreToBarLevel`'s first
argument is the row, not a score, so the port's parameter is renamed `nRow`; `BaseRec.bUnk17` is now
`bRegionId`. The naming problem this evidence exposed is fixed: the 0x80..0x89 id space is a world
region, so `PUFO_RACE_ID_*` became `PUFO_REGION_ID_*` with a `PufoRegionId` enum on both sides.

What the screen itself adds. `nFundDisplayTotal @ 0x159604` is not a leftover of the recompute —
`FundingScreen_Toggle` zeroes it on open (0x5AC0C) and rebuilds it from the five possibly-zeroed bars
(0x5B124..0x5B144), and `FundingScreen_Draw` prints it without recomputing. The toggle re-applies the
recompute's rule at draw time: a bar that is not positive zeroes its bar AND its arrow
(0x5B0EC..0x5B11C). The report's commentary is TEXTS[1307..1333] indexed by
`[arrow + 2][group][line]`, group 1 being rows 1 and 2 (Euro and Asia, 0x5BA34, 0x5BB80, 0x5BD70,
0x5BEA4); only the two negative arrows have a red variant of the third line (0x5BF60, 0x5BE34), and
the red flag is `(arrow < 0) && bar[slot] < 0` (0x5B810..0x5B820) — the bar decides, not the share.
`FundingScreen_DrawExtended` has no open gate and never reads the three panels.

The unit before it was the radar frame driver:
`RadarScreen_Update` (0x60570, 3804 B), 84 new checks, one more original bug, and the completion of the
radar screen. It draws the panel itself — five frame blits, five rails, five signal bars with mid colours
0x55/0x5A/0x5C/0x5A/0x55, five nav speed controls — and the panel is 140 tall normally but 190 tall while
`bRadarBlinkPhase` is set, which is the only way the 0x1F/0x4A/0x75 wash at (26, 176) becomes visible. The
middle bar row is 14 tall, not 22 (0x60D98 `MOV R8, #0xE`). `nRadarScanOffset @ 0x159724`, previously
`unk_159724`, is a scroll: it advances by 10 and wraps to 0 above 39, and `RadarScreen_Init` does not
clear it, so it carries from one radar screen to the next. Only four of the five button pads are ever
blitted (0x611A8 `MOV R8, #4`).

**The damage ladder is only reachable through a same-frame hit.** `RadarScreen_DrawOverlay` clears
`bRadarLastShotHit` at 0x5DCC0 before either mount runs, and the ladder's gate at 0x608C8 tests that byte,
so a target already at 0 HP is not noticed until a shot actually lands in the same frame. Three branches:
hp <= 0 posts TEXTS[1600] and removes the contact; 40..69 percent with a land tile and
`Game_RandU16(4) == 0` posts TEXTS[1601], converts the item to `nType 4` with `nTimer = 7200`, pays the
tile's race funding bonus and keeps the item; below 40 percent posts TEXTS[1602], sets
`bRadarInterceptDone` *before* the tile test, and removes the item instead of converting it when the tile
is 255. Three mutually exclusive tails follow: done plus a message timer above 30 closes the screen and
retargets the uncrewed chasers; out-of-fuel plus the same timer closes it, sets `byte_1870C8` as a DWORD,
posts message 44 and calls `Ship_RemoveWithCargo`; otherwise the full-zoom exit runs on
`bRadarFlip != 0 && nRadarRangeCur == 560`. **The bug:** the `Ship_ClearInterceptByStrategIdx` that exit
reaches with `BLEQ` at 0x61434 needs the zero flag set, and the only arrival that leaves it set is
`bRadarTraceLost == 0 AND bRadarFlip == 0` (0x61428..0x61430) while the gate that admits the block at all,
0x613BC, already requires `bRadarFlip != 0`. An intercept that reaches full zoom with the trace still held
therefore sends only `nInterceptShipIdx` home; every other craft chasing the same contact keeps chasing it
(bugs.tsv row 0x61434, reproduced by not calling it either).

The unit before it was the intercept shot animation:
`RadarScreen_DrawOverlay` (0x5dc84, 2788 B), 89 new checks and one more original bug. Each mount is
gated twice — its engage byte non-zero *and* its cooldown exactly 255 — and the two halves are
independent, so skipping the right one does not stop the left one. A round is consumed only when the
projectile offset is 0, i.e. only when a shot **starts**. The flight gate is the target's own distance
in the same 560/114 scale the range bar uses: `offset < nRadarCraftLevel + _rt_sdiv(560,
114 * nRadarRangeCur) - 33` for the cannon, stingray, avalanche and fusion ball, and `offset < 2` for
the laser and plasma, with steps +6/+2/+1 and `bRadarFlip` adding 2 to the four gated weapons. Landing
sets the cooldown to 255/240/235/251/249/230 and stores the roll's result. **The laser and plasma have a
third case nothing else has:** an offset that is neither below 2 nor exactly 2 does nothing at all — no
cooldown, no roll, no draw — so a beam that got past 2 never resolves (0x5DFEC `BNE`, 0x5E518 `return`).
The columns are not a constant offset either: the cannon is x 67/69 on *both* halves, the rockets are
64/65/63 left against 70/71/69 right, and the laser is x 64 left against x 72 right. The bug: the round
a shot consumes is gated only on the -1 "unlimited" sentinel, not on -2 "no weapon on this mount", and a
mount id outside 25..30 — 24, the psi amplifier — has no arm in this function's switch, so its
projectile offset never advances and the "a shot started" test stays true every frame. The counter walks
off through -3, -4, -5 … and `RadarScreen_DrawCraftArmament`'s label test is `!= -2`, so the radar screen
prints the runaway number where an ammo count should be (bugs.tsv row 0x5DD1C, reproduced).
The unit before it was the radar screen's two weapon mounts: `RadarScreen_DrawCraftArmament`
(0x5edf4, 1692 B), 69 new checks. `Ship.bWeaponL/bWeaponR` are
now the **`WeaponMount` enum** (24..30, in `pufo_ship.h` and in the IDB), and the names come out of the
shipped lang.dat TEXTS blob at record `1446 + id` — 1471 "Корабельная пушка", 1474 "Лазерная пушка",
1476 "Термояд. рак. уст." — which also corrects an older note that called id 24 "Psi-arm": 1470 is
"Пси-усилитель", a psi *amplifier*, and it is the one id no mount switch handles. Each id 25..30 carries
a glyph offset (50, 48, 49, 52, 53, 51) and **its own weapon range as a literal in the switch**
(140/260/360/180/300/420), and the bar height is `_rt_sdiv(560, 114 * range)`, so **the cannon's 140
scales to 28 pixels and `28 - 34` is negative: it draws no range bar at all**, while its tick row —
gated on `bWeaponL != 0`, not on the bar — is still drawn. The laser's 180 scales to 36 and draws 2 rows
but still gets its 3x3 cap, because the cap gate is `range > 170`. The engage gate is a distance test in
the same 560/114 scale the range bar uses, and out of gunshot the ARM's predicated group clears
`bRadarEngageL/R`, forces that mount's cooldown to 255 and drops the in-flight projectile; the two
cooldowns turn out to be the same kind of count-up-to-255 timer as the message and pulse timers, all four
advanced together by `Geoscape_PulseUpdate @ 0x95B80..0x95BCC`. The two halves are **not mirror images in
order** — left draws bar, cap, label, ticks; right draws label, bar, cap, ticks. One port-side defect was
fixed on the way: `Map_nGlyphPalBase @ 0x1183F4` is one global that the font reads directly, and the
shell kept a second copy in `PufoWorldState`, so a write through one never reached the other; that copy
is gone and every env now points at `g_nGlyphPalSlotBase`. This is the second real-surface test suite in
the project: the shipped `font.dat` and `basebits.dat` are loaded, so every mount icon and every ammo
label is the original's own sprite, and each id's icon is compared against a direct draw of its own
basebits frame. Nine more globals are named in the IDB: `nRadarShotYL/R`, `bRadarLastShotHit`,
`bRadarEngageL/R`, `nRadarBoundL/R`, `nRadarCooldownL/R`.
The unit before it was the radar screen's session:
`RadarScreen_Init` (0x5f490, 3600 B), `RadarScreen_SetActive` (0x6144c, 76 B) and
`RadarScreen_RollDetection` (0x602a0, 720 B), 194 new checks. `RadarScreen_Init` is **one function doing
both directions**, toggled on `bRadarScreenActive` (LDRB at 0x5F494) — which is why `SetActive` calls it
on both of its own branches — and `SetActive` then writes the same byte into `GameTime.bPaused` and
`bUiFrameStyle`, so **opening the radar screen pauses the game clock**. Its eight-case class switch is
**one flat table**: cases 1, 3 and 5 fall through into 2, 4 and 6 for the art and level half only, and
the `if (nInterceptTargetHp == 0)` guard inside them is always true because 0x5F614 just zeroed the
field, so the port reads it as `g_aRadarUfoClass`, which also carries RollDetection's per-class speed.
Two findings. The palette copy at 0x5FC34 runs **766 dwords into a 256-entry slot**, so it overwrites
slot 1 as intended and then all of slot 2 and 254 of 256 of slot 3; `base.pal` repairs slot 3 and the
close restores slot 1, but **nothing anywhere reloads slot 2**, which stays ufoflash.pal for the rest of
the session (bugs.tsv row 0x5FC34). And RollDetection's comparison is
`apCraft[nInterceptShipIdx]->uFuelCap * (1/60) < dSpeed` — `uFuelCap / 60` is the craft's own speed, the
same basis `StrategList_ProcessEvents` uses for an inactive craft, so the per-class constants 36.6666 …
83.3333 are contact speeds in the same units and the event means "the tracked contact outruns us". The
message it posts is TEXTS[1599], which lang.dat decodes as cp1251 "След **МАК** потерян" — the middle
word is Cyrillic М-А-К, not ASCII MAX. This is also the first suite that opens the shipped archives:
every pad the original loads is loaded for real and compared byte-for-byte against a directly loaded
reference member, and the palette copy is checked against independently reloaded palettes. Eighteen more
globals are named in the IDB, including `bRadarScreenActive`, `nInterceptShipIdx`, `nInterceptStrategIdx`,
`aRadarControls`, `aRadarButtonPads`, `aRadarCtlDesc`, `bRadarTraceLost`, `nRadarAmmoL/R` and
`nInterceptTargetHpMax`.
Before it, the radar screen's fuel gauge and range bar (`0x5d918` and `0x5d6e4`, 1088 bytes). The gauge draws the **consumed** percentage —
`100.0 - 100.0 * Ship.nFuel / Ship.uFuelMax`, with `uFuelMax` read signed like the original's `LDRSH` —
and keeps nothing of the number except `bRadarCraftOutOfFuel` when it reaches 100. Its tens digit is
drawn only when it is non-zero *or* there is a hundreds digit, which is the ARM's two branch shapes at
0x5DA9C and 0x5DAA4 rather than any rounding rule. The range bar's level term
`560*(nRadarCraftLevel - 13)/114` is always negative, so a higher craft class loosens the range gate
instead of tightening it, and `bCraftClass` is read with `LDRSB`, so the 255 sentinel becomes -1, passes
the non-zero gate and falls into the switch default to pay nothing. Five more globals are named:
`aRadarDigitPads`, `pRadarPercentPad`, `bRadarCraftOutOfFuel`, `nRadarRangeLimit` and
`nRadarCraftLevel`.
Before it, `RadarScreen_BlitMapBackdrop` (0x5eb10), 508 bytes, was the first radar-screen leaf, and it
corrected a standing misreading. The global at
`0x186BC0` was named `pSmapViewSize` and read as the world-size u16 pair the zoom steps publish; it is
a **pointer to the world map-bits glyph record** they resize in place — `Geoscape_InitUI` allocates
`800*400+4` bytes there and `Smap_ZoomIn`/`Smap_ZoomOut` only rewrite the u16 pair through it (600x300,
then 472x236). `SoftSurf_BlitFlip`'s third argument is that record and its fourth is the palette table,
which is what the IDB's own `nW`/`nH` parameter names obscure. The backdrop is a phase toggle before it
is a draw (alternate frames), its palette is `aPalette` slot 29, and its per-pixel alpha is the layer
pad's own byte plane. `pSmapViewSize` → `pSmapWorldBits`, `unk_159730` → `bRadarBlinkPhase`,
`nSmapModeXBias` → `nSmapOverlayOriginX` (written only by `Geoscape_Draw` at 0x8C3D4) and `unk_14E6D0` →
`aPaletteSlot29`; `pSmapPadCur` and `pSmapLayerPad` are now typed `PadImage *`.
Before it, `BuySellScreen_Draw` (0x77e6c), 3524 bytes, finished the buy/sell family and found the
clearest original defect so far: the half of the frame that runs while
`bBuySellPanelsBuilt` is 0 dereferences all five panels that `BuySellScreen_BuildPanels`' teardown
(0x77D60..0x77E38) has already `operator delete`'d without nulling them, and at 0x78B80 it uses the row
marker without the `- 2000` the built half applies. The port guards both and the row is in `bugs.tsv`. The
same unit shows the two halves of one function drawing differently (different total line, swapped column
captions, shifted separators, a shortfall panel on one side only) and pins the three row-marker decodings
(soldier via panel B's aux cell, alien as `apObjects[marker - 2000]`, everything else a priced object).
Before it, `ResearchScreen_Draw` (0x73a34) finished the research screen family — bring-up, panel build,
topic rebuild and the frame — and was the first port to load a real pad out of `main.dat` (research.pad,
240x320, mode 6) and blit it; its rails turned out to be a copy of the same 96 blits the transfer and
manufacturing frames carry, so all three now share one `BaseUi_DrawFrameRails`. Three other original
inconsistencies are on the record: `ManufScreen_Draw` (0x6e3e0) tests panel A's style dword at offset 0
against 119..121 instead of the selected product, so the hangar byte at 0x159DBC is never raised there;
`ResearchScreen_BuildPanels` computes the progress percentage as `100 * (wProgress / wTotal)` while
`ResearchScreen_RefreshPanels` computes `(100 * wProgress) / wTotal`, so a quarter-done topic reads 0% and
then 25% on the same data; and `ResearchScreen_RebuildTopicList` clears the finished topic's eight per-base
allocation entries a second time instead of the newly unlocked topic's (0x74648 `MOV R0,R7`), so a locked
topic can reach its unlock still carrying a stale allocation. The §2 block above is this same session's
audit output.

### Per-subsystem status

From `PARITY.md`, regenerated by `docs/parity/gen_report.py`.

| Subsystem | Recreated | Total | Bytes covered | What it is |
|---|---|---|---|---|
| `dat` | 17 | 18 | 2732 / 2744 | tagged archive container (DatFile) and AssetReader |
| `boot-assets` | 4 | 4 | 2744 / 2744 | startup asset load, text-table extraction, shutdown |
| `render` | 51 | 52 | 11104 / 11128 | software surface, blitters, palette loading |
| `audio` | 6 | 6 | 1176 / 1176 | FMOD-backed sound, behind a backend seam |
| `util` | 18 | 20 | 1136 / 1160 | growable pointer array, RNG, string helpers |
| `app-shell` | 21 | 24 | 6252 / 6376 | WinMain, window class, display bring-up, registry options |
| `platform` | 0 | 132 | 0 / 1908 | WinCE -> Win32 seams (GDI display, registry) |
| `save` | 35 | 35 | 27172 / 27172 | save/load chunk read/write |
| `tact` | 211 | 214 | 177036 / 178344 | tactical: map cells, entities, pathing, LOS, turns, action queue |
| `tact-ui` | 52 | 52 | 43764 / 43764 | tactical HUD, inventory, command pads, input dispatch |
| `ui-core` | 76 | 84 | 17380 / 36172 | shared UI panel, text engine, menus |
| `strat` | 200 | 204 | 126716 / 160496 | geoscape: globe, radar, ships, bases, strategy list |
| `strat-ui` | 245 | 245 | 121164 / 121164 | base/baseinfo/crew/manuf/research/buysell/transfer/options/ufopaedia/psi/base-attack/hangar screens |
| `titles` | 13 | 13 | 4444 / 4444 | intro/outro animated scenes |

`platform` at 0/132 is not a real gap in the usual sense: those 132 functions are WinCE APIs
(`gx.dll` GAPI, registry helpers) that the port deliberately replaces with Win32 equivalents rather
than recreates. The seams are `src/pufo/platform/pufo_gx.{h,c}` and `pufo_reg.{h,c}`.

---

## 3. What was done in this last stretch, and why

Twenty-six commits. Each one is a single unit: a port, or a decode recorded so a later port becomes
transcription. This stretch is the manufacturing and research screen families, the buy/sell frame, the
radar (intercept) screen taken from its leaves inward until all sixteen functions were in, the funding
screen taken from its arithmetic to its drawing, the inventory screen's close path — which closes the
`tact-ui` subsystem — the four post-battle tallies that produce the mission debrief, the victory-flag
naming fix they left open, the post-mission soldier cleanup that runs the rank ladder and the per-class
stat refresh, the battle-end / debrief emitter that runs all of them, and the fall processor that ends
the tactical action queue's command switch.

| Commit | Change | Why |
|---|---|---|
| `de08b2b` | port `ManufScreen_CommitProduction` (0x6f34c) | the manufacturing tables are work units, not item counts |
| `f44bf55` | port `ManufScreen_Init` (0x710b0) | the two bottom controls are shared geoscape UI, not manufacturing ones |
| `e85887a` | port `ManufScreen_BuildPanels` (0x6e9e8) | `aFacTypeTable` @ 0x15EF08 was the research topic table under a wrong name |
| `ebc5d2b` | port `ManufScreen_Draw` (0x6e3e0) | its hangar branch tests panel A's style dword, not the product |
| `4d9d3bb` | port `ManufScreen_DrawFrame` (0x6f9a4) | the manufacturing family is complete |
| `3daaeaf` | port `ResearchScreen_Init` (0x749a8) | research points the shared bottom controls the other way round |
| `054b2c5` | port `ResearchScreen_BuildPanels` (0x733f4) | the progress percentage is computed two ways |
| `f9e2c6c` | port `ResearchScreen_RebuildTopicList` (0x74270) | research completion is what unlocks a facility kind |
| `ab37f07` | port `ResearchScreen_Draw` (0x73a34) | the research screen's pad lives inside the topic table |
| `825c983` | port `BuySellScreen_Draw` (0x77e6c) | the not-built half dereferences the panels it deleted |
| `29084ae` | port `RadarScreen_BlitMapBackdrop` (0x5eb10) | the world-bits global was a pointer, not a size pair |
| `a605ee4` | port `RadarScreen_DrawFuelGauge` (0x5d918) with `RadarScreen_DrawRangeBar` (0x5d6e4) | the gauge reads the consumed percentage, and its tens digit is a branch shape |
| `8d632fa` | port `RadarScreen_Init` (0x5f490) with `RadarScreen_SetActive` (0x6144c) and `RadarScreen_RollDetection` (0x602a0) | opening the radar screen pauses the clock, and the palette copy overruns slot 2 |
| `fd73d69` | port `RadarScreen_DrawCraftArmament` (0x5edf4), the `WeaponMount` enum, the duplicated `Map_nGlyphPalBase` removed | the cannon's 140 range scales to a bar that draws nothing at all |
| `925e192` | port `RadarScreen_DrawOverlay` (0x5dc84) | the psi amplifier's -2 sentinel is not exempt from the round decrement |
| `94620a9` | port `RadarScreen_Update` (0x60570), completing the radar screen | the unreachable `Ship_ClearInterceptByStrategIdx` at 0x61434 |

`88a8241` (port `FundingScreen_Init` 0x5c084 with `FundingScreen_OpenMonthly` 0x5d160) follows
`94620a9`, `328171f` is the region naming fix, `f1a4daa` is the funding screen's own UI
(`FundingScreen_Toggle`, `FundingScreen_Open`, `FundingScreen_Draw`, `FundingScreen_DrawExtended`),
`ba0ab78` is `Inv_CloseAndCommit` (0x15a48) completing `tact-ui`, `8a2f64e` is the four post-battle
tallies (0x3348c, 0x33780, 0x33c80, 0x344b4), `cd1d2f0` is the victory-flag naming fix, `519f9d3` is
`Tact_ClearSoldierBattleFlags` (0x346e0) with its callee `Soldier_PromoteByTypeScores` (0x47280),
`386b8c2` is `Tact_CheckBattleEnd` (0x34fbc), and the unit this document is committed with —
`Unit_ApplyFallDamage` (0x4a7b0) with `Tact_ProcessFallAction` (0x52adc) — follows `386b8c2`; all are
described in §2.

### A real crash found in already-ported code

`Combat_ResolveShot` clears a 41x41x4 hit window and writes the outcome struct with no null test.
The original reaches both through Map globals, so it always has them. The port lifted them into
parameters — and two port call sites passed NULL for both. The detonation port crashed on the first
call. Fixed by giving `Combat_ResolveShot` a static scratch window and outcome when the caller
passes NULL, which reproduces the original's single shared window instead of inventing a new rule.
Recorded in `docs/parity/game-core.md`, not in `bugs.tsv`, because it is a port-side defect.

### A committed error, corrected rather than left in place

`8717217` stated that `Soldier.bFlags` is at `Soldier + 0x14`. That is wrong: `Soldier + 0x14` is
`nAnimPose`, which the port already models and static-asserts. `bFlags` is at `Soldier + 0x04`,
confirmed by ARM at `0x1D504` (`LDRB R3, [R0,#4]` then `ANDS R3, R3, #2`). Corrected in `6121f6d`
in both the docs and the IDB comment.

### Facts settled that were previously guesses

- **Cell array index is `60 * (60 * nZ + nY) + nX`.** The middle index is `nY`. Proven from ARM at
  `Unit_PlaceOnMap` (0x49A70). This is why `Map_PatchCraftSprites` looks coordinate-swapped when it
  is not.
- **`apSoldiers` is at Map + 0x1198**, not 0x1194. That correction turns a meaningless Hex-Rays
  gate into `apSoldiers[nSoldierCount - 1]->nUnitType == 6`.
- **23 unit types, indices 0 through 22**, confirmed independently by `g_aUnitMuzzleOffset` and by
  `aUnitWalkKind` occupying exactly the 1840 bytes up to `aUnitAnimFrame`.
- **`IsoDrawCtx` is fully mapped** — 57 members, size 316. The index that looked like a bug,
  `(char *)pIso + pIso->bAnimStep` then `[101]`, is not one: `bAnimStep` is at 0x44 and 101 is 0x65,
  which is `bUnitFlag0`, so it reads `bUnitFlag[bAnimStep]`.
- **`Mc1Rec+0x1E` is the tile facility kind**, not padding. It is read exactly once in the binary
  (0x33618), and the 27 shipped `.MC1` sets put it on exactly the sprites the debrief counts: 2 power
  source, 3 navigation, 5 reproduction, 7 entertainment, 8 surgery, 9 examination, 10 alloys.
- **The TEXTS in-memory stride is 205, not 200.** The on-disk record is 200 bytes; the loader leaves
  five bytes of slack, and every ARM label address is `aTextRecs + 205*n`. That is what made the
  debrief label table readable at all.
- **The alien unit types and ranks are a data fact, not a guess.** `Tact_TallyItemRecovery` credits
  one `apObjects` slot per `(nUnitType, bRankType)`, and 37 of those slots were already named
  `Map_apObjectsNN` in the IDB; each one is that race's soldier record in OBJECTS.DAT. The Muton row
  has no rank 4 and no rank 8 because the shipped Muton block (92..94) has no leader or commander.

---

## 4. Architecture and the design decisions that matter

### Layout

The source tree mirrors the IDB subsystem map (`docs/idb/inventory-grouped.tsv`). Directories are
created when a subsystem's work starts; nothing speculative.

```
CMakeLists.txt
src/pufo/CMakeLists.txt        libraries, the exe, one target per test suite
src/pufo/pufo_common.h         base types + DisplayMode
src/pufo/app/                  wWinMain + WndProc                        [app-shell]
src/pufo/platform/             GAPI replacement, registry options        [platform]
src/pufo/util/                 PtrArray, Game_RandU16, string helpers    [util]
src/pufo/dat/  src/pufo/asset/ DatFile container, AssetReader            [dat]
src/pufo/render/               software surfaces, blitters, palettes     [render]
src/pufo/audio/                sound behind a backend seam               [audio]
src/pufo/ui/                   panels, controls, menus, modal, linelist  [ui-core]
src/pufo/game/                 35 files: everything else                 [tact, strat, save, titles]
tests/                         48 suites + pufo_test.h
docs/idb/                      IDB inventory exports
docs/parity/                   parity reports, audits, bug ledger
tools/                         PE and table dump helpers
```

108 files under `src/`, 49 under `tests/`.

### Decision: env structs instead of inventing global owners

The original reaches hundreds of globals through a single `Map` object at `0x1139B8`. The port does
not own most of that block yet. Rather than invent a global owner for state it does not model, a
port function takes an explicit **env struct** describing exactly what it reads and writes.

Examples in use: `GfxColorEnv`, `IsoAnimGlobals`, `IsoSpriteBases`, `IsoGrid`, `IsoSelectGate`,
`IsoViewportGuide`, `IsoHeightGate`, `TactHudPanelEnv`, `TactTeamSelectEnv`, `CraftSpritePatchEnv`,
`UnitCreateEnv`, `TactEquipCtx`, `ShipRemovalEnv`.

This is what makes teardown and draw functions testable: a test fills the struct and asserts on it.

### Decision: reproduce original bugs by default

`docs/parity/bugs.tsv` is the ledger. 226 rows, 89 `ORIGINAL BUG` markers in the source, and
`docs/parity/audit_bugs.py` enforces that the two agree in both directions. A marker must carry the
canonical ledger address inside it, e.g. `ORIGINAL BUG (0x67048), reproduced: ...`.

Original bugs are reproduced unless the row says otherwise. A `qol` column marks which ones a future
quality-of-life patch should fix. This exists so a later QoL pass can be a diff against a list
instead of a hunt.

### Decision: parity integrity is a deliverable, not a report

Three audits exist because claims drifted:

- `audit_credits.py` — flags a port claim sitting next to a port denial on the same line.
- `audit_names.py` — flags a cited address whose original name does not appear in `src/pufo`.
  Resolved either as an explained rename (recorded in `docs/parity/renames.tsv`, 396 rows) or as a
  real gap. Unexplained flags went 116 to 0.
- `audit_tables.py` — extracts the port's static table initialisers and diffs them byte for byte
  against the shipped image. 28 tables, 0 mismatches.

### Decision: ARM disassembly beats Hex-Rays

Hex-Rays misrendered at least 20 things that ARM settled. The recurring failure modes:

- Base-advancing array rendering. `v6 += 4` then `v6->field` is a normal index walk, not a bug.
- Wrong struct member for a folded constant. `100 * v27` meant 100 four-byte elements, i.e. 400 bytes.
- Swapped operands. `SoftSurf_FillRectEx` argument 4 and 5 are reversed in the pseudocode.
- Wrong global. `MEMORY[0x158890].anLineAux[79]` was really the selected-base byte, loaded with
  `LDRSB`.
- Wrong offset. `pCeilSpr->gap_22[14]` and `*(pCeilSpr + 36)` are the same field, `Mc1Rec + 0x24`.
- Modulo rendered as division. `MOVS / ADDMI #0xF / ASR #4 / SUB R0, R0, R3, LSL#4` is `% 16`.
- Soft-float multiply rendered as string calls. `itos` / `muls` / `stoi` is a plain integer product.

### Decision: the map X/Y convention is genuinely swapped

`Map_nCraftMapX` and `Map_nCraftMapY` are craft tile positions; multiplying by 10 gives cells.
Several readers put `10 * nCraftMapY + row` into the slot others call `nMapX`. Confirmed
independently three times (`Tact_GetCameraBounds`, `Tact_SelectTeamUnits`, `Map_PatchCraftSprites`).
It is a game convention, not a port bug. Do not "fix" it.

---

## 5. What currently works

- The Win64 exe configures, builds with `/W4 /WX` and zero diagnostics, and launches.
- It creates its window, stays alive, and reports that `lang.dat` and `main.dat` loaded.
- Output PE verified: `machine=0x8664`, `magic=0x20b` (PE32+), `subsystem=2` (GUI).
- C23 confirmed: `CMAKE_C_STANDARD 23`, `CMAKE_C_EXTENSIONS OFF`, `__STDC_VERSION__ == 202311`.
- 56 test suites, 11980 checks, all passing.
- Save and load round-trips are exercised against real chunk layouts for product, bases, ships,
  flobjs, map, teamunits, tgamedata, smap, rmp and strateg.
- Tactical combat maths is exercised: ballistic resolution, blast footprint, splash, LOS/FOV,
  proximity arming and chain detonation.
- Tactical HUD drawing, inventory drag-and-drop, geoscape viewport and camera, base capacity and
  facility accounting, craft cargo and loadout chains, the mission-site spawn machine, and the whole
  titles animation cluster are ported and tested.

What does **not** work yet: there is no end-to-end playable session. `Tact_InitMission` is ported and
the tactical mission now runs end to end in the test fixture, but nothing in the built executable calls
it — the geoscape UI that would start a mission is not ported.

---

## 6. What remains incomplete

### The critical path to a playable mission

This is the shortest chain that produces a real game session, in dependency order:

1. Done: `Map_BuildMissionTerrain` (0x22e10, 10,676 B) — ported in `src/pufo/game/pufo_terrain.c`. Its
   own dependencies were already ported; `Font_LoadTactSprites` (0x257c4) is a hook and
   `Combat_ResolveShot` is a hook for context-ownership reasons, not because it is missing.
2. Done: `Tact_SpawnAliens` (0x3a2ac, 3,516 B) — ported, with its nine static tables inlined and
   image-audited (see section 7).
3. Done: `Tact_SetupCraftCargo` (0x38f00, 2,116 B). Its blockers were not shared state at all:
   `0x156CD0` and `0x157208` have no reader outside this function, `0x157204` is the deploy cell it
   writes for `Tact_EquipTeamFromCraft`, and `0x158848` is the mission base index. All four are
   `TactCargoEnv` fields now.
4. Done: `Tact_InitMission` (0x3c550, 4,120 B) — ported in `src/pufo/game/pufo_tactinit.{h,c}`. The
   prediction held: the callees were never the problem, the state was. The function allocates ten
   singletons and clears roughly thirty flags before it calls anything, and it hands eleven populated
   contexts to its callees, so `TactInitEnv` carries all of it as caller-owned pointers and the port
   fills the mission-derived half in `Init_FillCalleeEnvs`.
5. Done: `Tact_Shutdown` (0x3d568, 3,788 B) — ported in `src/pufo/game/pufo_tactend.{h,c}`. It was
   deferred on the grounds that it frees singletons the port does not yet allocate. That objection
   dissolves the same way it does everywhere else in this port: the function takes every slot it
   frees as a caller-owned pointer and owns nothing itself. It went first because it was the only
   Priority 1 item whose callees were all already ported.
6. Done: `LoadGame` (0x44bbc) and its last blocking callee `Geoscape_InitUI` (0x857f4, 5,420 B) —
   InitUI is ported in `src/pufo/ui/pufo_geoiinit.{h,c}` with a real-archive test
   (`tests/test_geoiinit.c`). `Geoscape_BuildNavControls` (0x67b1c) and its click handlers are now
   ported too (`pufo_baseui.c`: `NewBaseScreen_Open/TryConfirm`, `SoldierScreen_Find*AtBase`,
   `Ui_SetTransferFlag`; `SoldierScreen_Toggle` and `ResearchScreen_Open` stay env thunks,
   `Geoscape_OnMapClick` a hook). `Game_StartNew` (0x94554, 3,976 B) is ported too, in the same
   `pufo_geoiinit.{h,c}` behind `StartNewEnv`, with `tests/test_startnew.c` (107 checks) over the
   real archives; it also brought the verified enums (`FacilityKind`, `ObjectSlot`, `CraftType`,
   the `PUFO_GRID_*` layout) into the port and the IDB. What is left on the geoscape bring-up
   path is clear: `Geoscape_OnRadarListClick` (0x854c4) is ported in `pufo_baseui.c` behind
   `RadarClickEnv` (36 checks in `tests/test_baseui.c`). The shell wiring is
   done too: `app/pufo_world.{h,c}` owns the geoscape state (`PufoWorldState`) and
   `App_HookNewGame` runs `Geoscape_InitUI(0)` + `Game_StartNew` for real; `pocketufo
   --new-game --dump-frame <png>` prints the resulting state (4147000 funds, 1 base, 8
   soldiers, 3 craft, screen 2, zoom 0), and `World_LoadSlot` wires `LoadGame` over the same
   state so `--load-slot 7` loads a save for real. The geoscape click handlers (except the
   radar list) and the spawn hooks are no-op thunks until those screens land. All three leaves `Geoscape_OnMapClick` (0x8b4a8) needs are in now: `Base_CanPlaceHangarAt` (0x629b0) — a connectivity flood over the base's finished stamped cells, refused exactly when clearing the tapped 2x2 footprint would cut one off — `BaseFac_FormatCostLabel` (0x6a404), the `TEXTS[905 + kind]-<counter>` label that also increments `abBaseKindTable`, and `BaseFacScreen_Init` (0x5a198), the build dialog's up/down pair. `Geoscape_OnMapClick` itself is ported in `pufo_baseui.c` behind `MapClickEnv` and the shell runs it for real through the confirm control (`World_TryConfirmClick`): screen 1 places or clears a facility cell, screen 2 commits the pending facility or opens the build dialog. 74 checks in `tests/test_baseui.c`. The dialog itself is live too: `BaseFacScreen_Toggle` (0x589ec) builds the 17-row facility panel and wipes the selected base in new-base mode, and `BaseFacScreen_TryBuySelected` (0x58910) is its OK button -- 55 more checks in the same file. The soldier screen is live too: `SoldierScreen_Toggle` (0x64af0) builds the pad, the four panels and one row per soldier on the selected base, and `SoldierScreen_SelectListed` (0x64a9c) is its row click and `SoldierScreen_EquipSelected` (0x619cc) its four equip rows (row 0 returns the equipped item to the base, rows 1..3 swap objects 122..124) -- 81 more checks in the same file. The base-placement side is ported too: `BaseFacScreen_DrawPlacementOverlay` (0x58e10) scores the tap against the grid, draws the outline and commits the facility (35 more checks), waiting on `BaseFacScreen_DrawBuildDialog` for its shell wiring -- and so is the animated frame helper that dialog draws through, `Ui_AnimExpandBlit` (0x95bf0) with its `_IfEnabled` wrapper (0x663c4), 19 more checks in `tests/test_panel.c`. And the dialog itself is now ported: `BaseFacScreen_DrawBuildDialog` (0x59b60), the frame that draws the funds line, the selected facility, its cost and build days, and the only caller of the placement overlay -- 23 more checks in `tests/test_baseui.c`. Its shell wiring waits for `Geoscape_DrawScreen` (0x6c16c), the geoscape draw that calls it. The two craft-name formatters are ported too -- `Ship_FormatDisplayName` (0x666cc) and `Soldier_FormatDisplayName` (0x6654c) -- so the soldier screen's craft column now shows the real label plus the craft ordinal instead of an empty string. Two radar-screen leaves followed -- the intercept weapon roll `RadarScreen_RollWeaponHit` (0x5db24) and the overlay blit (0x5d6d4) -- which is also the first look at the radar screen's intercept-target state (`nInterceptStrategIdx`, `nInterceptTargetHp`). The options screen's audio tab followed — 17 functions, 2744 bytes — which also typed the options control block in the IDB (`aOptionsListCtl`, `aOptionsMusicBtn`, `aOptionsSfxBtn`) and put two more entries on the bug ledger. The fourteen save/load slot handlers the same screen installs followed (1260 bytes), which also corrected a stale "LoadGame is not ported" line in `pufo_save.h`.

### Largest unported functions by size

| Address | Function | Bytes | Note |
|---|---|---|---|
| 0x8c180 | `Geoscape_Draw` | 33,744 | the geoscape renderer |
| 0x9b5f4 | `Ui_DrawBaseInfo` | 10,196 | |
| 0x632cc | `Ui_TextInputDialog` | 6,096 | |
| 0x557f4 | `BaseInfoScreen_BuildPanels` | 6,084 | |
| 0x57808 | `BaseAtScreen_Update` | 3,872 | |

(Regenerated from `PARITY.md`'s per-subsystem "remaining" tables, so the ported `ManufScreen_DrawFrame`,
`BuySellScreen_BuildPanels`, `TransferScreen_BuildPanels`, `Geoscape_InitUI`, `Game_StartNew`,
`Tact_InitMission`, `Tact_DrawCommandPads`, `Tact_CheckBattleEnd`, `Unit_ApplyFallDamage`,
`Tact_ProcessFallAction`, `Tact_ProcessFireAction`, `Geoscape_OnPointer`, `Geoscape_OnPointerUp`,
`Smap_HitTestStrategSelect` and `StrategList_ProcessEvents` are no longer listed here.)

`tact` is now down to 3 functions and 1,308 bytes: `Map_InitAndLoadRegistry` (1,284 B) plus the two 12-byte
atexit registrars it installs. The step family — `Map_ProbeStepOccupants` in unit 70 and `Unit_CanStepTo` in
unit 71 — was the last game-behaviour gap in the subsystem.

`strat` is still the weakest subsystem by bytes: 126,716 of 160,496 (79.0%), and `Geoscape_Draw` alone is
33,744 of the 33,780 left. Both geoscape pointer handlers, the map select test they call, both map click
handlers they install, the per-tick strateg event pass, the strateg icon pass, the clock strip, the whole
`Geoscape_DrawScreen` frame, the pulse tick, the map-target arm, the globe hover frame, the plane screen's
stat rows and the base-facility status line are in now — what is left in the geoscape is `Geoscape_Draw`
itself (33,744 B) and the three 12-byte atexit registrars, which is exactly the subsystem's remaining gap.
`Geoscape_Draw` is now the single largest unported function in the binary.
`strat-ui` is complete: 245 of 245 functions and 121,164 of 121,164 bytes — the small screen handlers, the
whole base-info family, the base-attack screen, the hangar's panel set, frame, craft readout and craft
detail, the transfer error dump and the base facility grid's name / build-icon / craft pass are all in.

### Decoded but deliberately not ported

These are analysed and recorded in `docs/parity/` and in IDB comments. Porting them is transcription,
not research:

- `App_ResizeMainWindow` (0x9ecb8) with `MenuBtn_ResizeWindow` (0x19930) — fully decoded, blocked on
  the port having no owner for the stored window size (`0x188D40` / `0x188D50`).
- `Smap_BeginCraftSelectBtn` (0x84634) — blocked on `Smap_BeginCraftSelect` (0x838dc) being unported.

---

## 7. Known issues, constraints, assumptions, risks

### The six rank names are loaded text, not image bytes

`aszTextE @ 0xB2200` — the six 20-byte rank names the promotion report writes into
`pUiPanel3 @ 0x1568DC` — lives in the loaded-texts image, not in the shipped PE: `get_bytes` there
returns zeros and `get_string` finds nothing. The port therefore takes the names through
`TactBattleFlagsEnv.panRankName`, and `tests/test_tactend.c` supplies its own six strings so the test
asserts the `20 * bRankType` indexing (the ARM's `MOV R2,#0x14; MUL LR,R3,R2` at 0x34E74) rather than
the wording. Getting the real strings is the TEXTS loader's job and is recorded as a gap, not guessed.

### The `MapView_DrawUnit` table set is in the port and byte-checked

Nine static tables, all extents settled against the shipped image, all nine now defined in
`src/pufo/game/pufo_isodraw.c`, exported by `pufo_isodraw.h`, dumped by `tools/gen_isodraw_tables.py`,
and registered in `docs/parity/audit_tables.py`:

| Table | Address | Extent | Evidence |
|---|---|---|---|
| `aUnitWalkKind` | 0xA6B00 | 1840 B, 80 per unit type | exactly fills the gap to `aUnitAnimFrame` |
| `aUnitAnimFrame` | 0xA7230 | 468 B, 78 triples of u16 | largest frame value in the pose table is 78 |
| `aUnitWalkGlyph` | 0xA7408 | 594 B, 27 per type | covers types 0..21 only |
| `aPoseFrameTable` | 0xA7660 | 760 B, 152 rows of 5 | `5 * (133 + 17) + 4 = 754` fits 760 and nothing smaller |
| `aUnitTypePoseBase` | 0xA7958 | 23 B | values 19, 38, 57, 76, 95, 114, 133 |
| `aUnitHeightOff` | 0xA7A28 | 184 B, 8 signed per type | |
| `aBobPhaseY` | 0xA7BE0 | 8 signed bytes | next bytes are the ASCII `xcom_0`, which proves the end |
| `aUnitWeaponYOff` | 0xA9310 | 23 B | only type 9 (5) and type 11 (-1) are non-zero |
| `aUnitFacingDrawOff` | 0xA93A8 | 224 B, 16 signed per type, **14 types** | ends at `0xA9488`, where the u16 table `Map_TraceVoxelOccupancy (0x3242c)` begins |

**How the two short tables were settled.** `aUnitWalkGlyph` is 594 bytes, which is exactly one more
than the largest index reachable for type 21 (`27 * 21 + 9 * 2 + 8 = 593`), so it has 22 rows and type
22 has none. `aUnitFacingDrawOff` is 224 bytes, which is exactly one more than the largest index
reachable for type 13 (`2 * (8 * 13 + 7) + 1 = 223`), so it has 14 rows; the earlier reading that saw
"type 22 starting with 80" was reading the occupancy table that follows it. The port keeps both tables
short rather than padding them, so an out-of-range unit type reads the same bytes the original does.
`python tools/gen_isodraw_tables.py --check` prints the boundary bytes on both sides of all nine.

### The `.ma1` header's two extent bytes are Y first, then X

Fixed in this commit, and worth knowing before touching the terrain builder. `.ma1` header byte 0 is
the template's **row** count and byte 1 its **column** count. The size formula
`3 + 4 * nRows * nCols * nLevels` is symmetric so it cannot show this; the proof is three reads that
agree — `Map_ApplyTemplateToCells (0x54cf4)` builds its destination as
`10 * (6 * (10 * nOffY + row) + nOffX) + col` with `row` looping to byte 0, `CDialogTemplate_GetCellByte
(0x54c80)` uses byte 1 as the row stride, and every `Map_LoadRmp` call in `Map_BuildMissionTerrain
(0x22e10)` passes `nSizeX = byte1 / 10`, `nSizeY = byte0 / 10`.

`MapTemplate_ApplyToCells` had it the other way round: it looped `nX < nDimX` / `nY < nDimY` and passed
those to a getter whose stride is `nDimY`, so a non-square template read past the end of every row and
landed transposed on the map, and its offsets were taken as cell coordinates rather than tiles. The
fields are now `nRows` / `nCols`, the nesting matches the ARM, and offsets are tile coordinates scaled
by `PUFO_CELLS_PER_TILE`. `test_template.c` asserts the direction outright: the far corner is
`(x = cols-1, y = rows-1)`, the transposed corner is untouched, and a tile offset lands ten cells in.

Two shipped tables the terrain builder needs are now in `src/pufo/game/pufo_terrain.c` and audited:
`g_aTileSprIndex` (64 u16 in 16 rows of 4: floor, wall A, wall B, ceiling) and `g_aTerrainModuleCount`
(the per-mission-type module ceiling, 10 bytes). IDA declared `aTileSprIndex[128]`, which is 256 bytes
and runs 128 bytes past the real end into the module-name strings at `0xA6590`; the IDB now declares it
`[64]` and the 40 bytes at `0xA6568` are named separately as belonging to `Smap_InitCraftSelectState
(0x61b94)`.

### `Map_BuildMissionTerrain`: what the port had to settle that the decode left open

Ported in this commit into `src/pufo/game/pufo_terrain.c`. The full write-up is in
`docs/parity/game-core.md`; these are the decisions and corrections a later reader needs.

**The builder owns the cell allocation.** `MapTerrainEnv.apCells` is a caller-owned `MapCell *[14400]`
and the builder mallocs every `MapCell` into it, reproducing the original's allocation-failure path:
it stores NULL and still calls `MapCell_Init(NULL, x, y, layer)`, and the tail writes through the cell
pointer with no null test either. Both are in `bugs.tsv` as reproduced. `MapCtx`'s inline `aCells`
stays a separate, simpler model for the other tests — the two are not the same thing and are not
meant to be.

**Two hooks, for two different reasons.** `pfnLoadTactSprites` stands for `Font_LoadTactSprites`
(0x257c4), genuinely unported. `pfnResolveShot` stands for `Combat_ResolveShot @ 0x2f12c`, which IS
ported — the hook exists because its `CombatCtx` (`TactState`, the three rosters, the two 41×41 hit
windows) belongs to whoever owns the tactical context, and a terrain builder cannot construct one.
That is a context-ownership hook, not a missing-function hook, and the header says so.

**The tail mask is weaker than it looks.** `nFlags32 &= 0xFFFFFF87` clears bits 3..6 and keeps 0, 1, 2
and 7. `MapCell_Init` already masks with `0xFFFFFF80`. On a grid the builder just allocated the tail is
a no-op. An IDB comment of mine said it "clears the low 7 bits and bit 7", which is wrong both ways,
and is corrected.

**Both kind-array passes read `kind[row][col + 1]`.** Pass A's guard is `LDR R0, [R3, #4]` at 0x25338;
pass B starts one dword into the array and reads `[R3, #0]`. Same element. Neither reads past the 36
entries because pass A stops at column `nDimX-2` and pass B at row `nDimY-2`.

**The two XCom-base guards look at different neighbours, and their sentinel sets differ.** Pass 1
checks the tile one COLUMN right and skips `0x10`, `0x12`, `0x14`; pass 2 checks the tile one ROW
below and skips `0x10`, `0x11`, `0x14`. `test_terrain.c` pins both directions in both senses.

**The craft pad tile and `Map_nCraftMapX/Y` are the same two bytes.** The plane placement writes
`Map+0x15139` / `Map+0x1513A`; `Map_PatchCraftSprites` reads exactly those (its literal pool at
0x25E64 / 0x25E68). So the builder writes the craft's tile and the tail consumes it, and the env has
one field for it rather than two. `CraftSpritePatchEnv`'s comment in `pufo_font.h` claimed
`0x15199` / `0x1519A` — that was wrong and is fixed. The same byte pair also shows up in the sprite
group: a template's slots resolve through `*(u16 *)(fontSys + 2 * nFlag)`, the pad loads with
`nFlag 3`, and `Map_PatchCraftSprites` reads the u16 at `fontSys + 0x06`. One group, reached twice.

**The fallback appends the literal `"4"`.** `pDefault @ 0xAAD4C` is `"4"`, not the per-mission-type
`aTerrainNameSuffix0` entry, so even a jungle fallback loads `jungle4.ma1`.

**The shot sweep's selector indexes an object array.** The shot's object argument is
`*(Map + 0x108 + 4*selector)` — selector 18 is `Map+0x150`, selector 51 is `Map+0x1D4`. The two
`Game_RandU16(3)` draws that set the X and Y steps run even when the count is gated to zero, so they
are not inside the `if`.

**A correction to my own earlier IDB work.** The comment at `0x24150` listed the four cell slots as
`pFloorSpr / pWallSprB / pWallSprA / pCeilSpr`. `MapCell` puts wall A at `+0x04` and wall B at
`+0x08`; the ARM agrees. Fixed. `aUfo1a` was declared `char[216]`, 81 bytes past its real end into the
alien tables; it is now `[135]`, fifteen nine-byte slots, seven of which are empty strings.

### `Tact_SpawnAliens` data model

Four IDB symbols that looked like separate tables are one 80-byte block based at 0xA6618:
`aAlienRaceComposition` (0xA6628) is base + 16 and `aAlienRankTypes` (0xA6650) is base + 56, verified
byte for byte against the image. `aAlienBeltObjects` (0xAA008) is eight u16
`18, 23, -1, 3, 6, 11, 12, 13`, walked backwards into `apBelt[7..0]` while a parallel counter runs
14, 12, 10, 8, 6, 4, 2, 0 and skips the entry where the counter equals 4 — the `-1` sentinel is
skipped by a counter, not by a test.

**There is no stack overrun at 0x3AB6C — the earlier claim here was wrong.** The composition array is
one 8-dword array at `SP+0x70` (`bp-0x20` .. `bp-0x04`). The 5-entry seed loop at 0x3A34C writes its
first five slots, the loop at 0x3AB50 overwrites all eight, and the belt walk at 0x3AD50 starts at
`var_4` (`bp-0x04`) and steps **down** over the same eight slots. IDA's `var_14`, `var_10` and `var_4`
are three elements of that one array that Hex-Rays declared as separate variables, which is what made
`v87` look like `_DWORD[7]` overrun a `TactItem` stub. In the IDB the three phantom `var_14` /
`var_10` / `var_4` are deleted and the array head is named `anComposition` at `bp-0x20` (IDA rejects a
real 8-dword stack array over this frame). The port uses one `i32 anComposition[8]`.

**Two stack slots `Tact_SpawnAliens` reads without ever writing**, both in `docs/parity/bugs.tsv` and
both modelled as explicit `TactSpawnAliensEnv` fields because C cannot read uninitialised memory:
`var_24` (read at 0x3A8BC, bounds the fourth race group) and `var_48` (the alien set index, left
unwritten for mission mode > 3 by the `BNE loc_3A324` at 0x3A2F0).

### Assumptions carried forward

- `apObjects` is at Map + 0x108 and is an array of **pointers**, not records. `apCraft` at 0x10AC,
  `apBases` at 0x1174, `apSoldiers` at 0x1198.
- An empty facility cell is **255, not 0**. A zeroed 6x6 type block reads as 36 built facilities and
  charges 1,800,000 per month.
- Capacity weights are the capacities: one living-quarters cell houses 50, one stores cell 300 units,
  one containment cell 10 aliens, one hangar cell 1 craft, one psi-lab cell 10.
- `bUnk183 == 254` means away in a craft; `255` means not aboard anything.
- `bAmmoCap == 255` means no finite ammo cap.
- `ObjectRec.aItemQty[51]` and `[52]` are not quantities; they are out-of-range reads that land on
  other fields. Weapon icons come from the bigobs sprite group.
- `Map + 0x37` does double duty: it is the animation phase clock for unit types other than 20 and 21,
  and it is the byte `UiModal_DrawDialog` uses to pick its horizontal band.
- **`unk_128A8C` (Map + 0x150D4) is the victory flag — the port had this polarity backwards.** It is
  now `Map_bMissionVictory` in the IDB and `pbMissionVictory` in the port. `Tact_CheckBattleEnd` sets
  it at 0x350F0 when no enemy is left and clears it at 0x35070 when no Xcom unit is left or at 0x351CC
  when a base-defence mission is called off, and the debrief uses it as the win/lose switch
  (TEXTS[160] "Victory" / TEXTS[161] "Defeat", green/red caption). The earlier reading — "aborted" —
  came from reasoning only from its eight IDB xrefs, which are all reads because every write is a
  register-built offset. See §8.
- Win64 launch prints an asset-load line. The original `InitInstance` (0x9fd48) prints nothing. This
  is a recorded deviation, added deliberately so a good launch is distinguishable without a screen.

### Two ownership problems found while porting the pointer dispatcher (open, not fixed here)

- **`PufoWorldState` has two fields for one byte.** `app/pufo_world.h` carries `bFacDone` (commented
  `bBaseFacDone @ 0x187078`) and `bNewBaseMode` (commented `byte_187074[4]`), and `Geoscape_OnPointer`
  proves 0x187078 is a single byte written with `MOV R2, #1` at 0x874E4 while 0x187074 is a different
  byte (the drag flag, now `bGeoscapeDragActive`). One of the two fields is wrong; the fix touches every
  reader of `bFacDone`, so it is a separate change, not a drive-by edit.
- **`bBasePlaceFail @ 0x1870B8` was declared 16 bytes wide in the IDB** and so overlapped
  `bBasePlaceActive @ 0x1870BC`, which the same function writes separately (cleared at 0x87320 / 0x87EA0,
  set at 0x87ED4). **Fixed in unit 75:** the three writers are `dword_1870B4[0]` (nBaseFacPanelSel, 0x5896C),
  `[1]` (bBasePlaceFail, 0x8BEA4) and `[2]` (bBasePlaceActive, 0x8BE9C), so the object is three dwords and
  both flags are now 4-byte ints with no overlap.

### Three things unit 75 found that are separate issues, not drive-by fixes

- **`tests/test_save.c` asserted fields the version-2 read never writes.** `Save_ReadStrateg`'s gate at
  0x41554 stops before `nTargetX` / `nTargetY` when `bSaveVersion <= 2`, and the test compared them anyway on
  a version-2 read. It passed only because the heap happened to recycle the same block between two reads; a
  diff in `pufo_baseui.c` perturbed the reuse and the two checks failed. Fixed by a `CheckStrategItemV2` that
  drops exactly the two fields the gate skips, but the class of problem is wider: **a check on a field the
  port never wrote is a check on uninitialised memory, and it can pass for many units before it fails on an
  unrelated change.** Worth a sweep for the same shape in the other save tests.
- **0x18659C is unresolved, and the port now says so instead of pretending.** The third funding bar is filled
  at 0x9469C / 0x946A4 from a pool entry IDA names `unk_14B4C8`, so it holds 1357832 among four round money
  values, and `Geoscape_CloseUI` case 19 adds all five slots as plain ints. Nothing in the image dereferences
  it. Whether the original meant an address is not answerable from the binary; the port stores the value
  (`PUFO_FUND_BAR2_INIT`) and the note in `pufo_geoiinit.h` records the open question rather than resolving it
  by guess.
- **The close is ported but nothing in the app builds a `GeoscapeCloseEnv` yet**, and `TitleReturnHooks.pfnCloseUi`
  is still a hook. The same is true of `StepCtx`, `PathToCellCtx`, `EnemyStepCtx`, `TactTickCtx` and
  `GeoscapePointerEnv`: the ported functions are wired to real state in tests and to each other, but the shell
  does not own those structs. That is the seam Priority 1 has to close, not a defect in the ported functions.
- **`OptionsScreenHooks` routes two ported functions as pointers.** `OptionsScreen_RebuildWithSound`
  (0x6cf00) and `OptionsScreen_OpenWithSound` (0x6dd04) tail-call `OptionsScreen_Init` (0x6be74) and
  `OptionsScreen_Open` (0x6d0ac), and both of those are now ported — but the two wrappers carry them as
  `pfnInit` / `pfnOpen` because they have no env of their own to hand over. So the options screen can be
  rebuilt through a hook that a caller may leave NULL, silently doing nothing. The fix is to give the two
  wrappers the `OptionsInitEnv` / `OptionsOpenEnv` they need and call the ported functions directly; it
  touches every caller of the wrappers, so it is a separate change.

### Deliberate deviations from the original

Recorded rather than hidden. The full list is in `docs/parity/bugs.tsv` (221 rows, 84 matching
`ORIGINAL BUG` markers in `src/pufo`) and the subsystem notes. Notable ones:

- `UiModal_DrawDialog` uses a 205-byte record buffer where the original copies into a 92-byte stack
  buffer. Past 91 characters the original's behaviour is stack corruption, not defined behaviour.
- `Gfx_PackRgb565Pair` reads the display format from the port's own `g_ffFormat`; the original
  reaches it through a caller-supplied pointer.
- `Soldiers_Reload` nulls freed slots and bounds allocation by the caller's array size. The original
  frees without nulling and without a bounds check.
- `Craft_LoadStoresFromBase` guards `bHomeBase` outside 0..7 and null object arrays; the original
  dereferences whatever those hold.
- `Base_CalcOccupiedSpace` and `Base_CountAmmoItems` reproduce the original's unguarded `char` index
  into `auPerBaseVal` rather than adding a bounds test, because a bounds test would change which
  objects contribute.
- `Map_BuildMissionTerrain` bounds-checks its cell lookup and its two name-table indexes. The original
  indexes the 14400-dword cell array with no test, reads `aPlane[9*(nMissionType-1)]` for mission type
  0 (nine bytes before the table) and `aUfo1a[3*(variant-1)+tier]` past slot 14. Out of range the port
  yields an empty name, which fails the `.ma1` load the way a missing member does.

### Original bugs reproduced on purpose

89 markers in the source, 226 ledger rows. Examples:

- `0x8b9e0` — the 3600 arm of the close's speed chain calls `GameTime_SetSpeedIdx4` and then branches into the
  43200 arm's `SetSpeedIdx5`, so a 3600 tick lands on index 5. Reproduced, with the test asserting the landed
  index rather than the intended one.
- `0x8bf74` — `nSelectedBaseIdx` is decremented with LDRSB / SUB #1 / STRB and no floor, so a zero index wraps
  to 255 and case 7 toggles the facility screen for a base that does not exist.

- `0x2b75c` — the +4 layer surcharge and `bFlags29` bit 3 both come from the probe's `g_bStepReanchored`
  report, not from the search flag. The port used the flag for both until unit 71.
- `0x28c90` — every large-unit arm re-tests the far corner of its footprint with `nLayer = 0`, although
  `Map_IsValidCell` does test the layer; the call is really only an x/y test.
- `0x27e0c` — the occupant gate inside the direction arms is weaker than the head's: `bUnk148` alone lets a
  foreign unit through.
- `0x27a94` / `0x27a88` / `0x27ae0` — three reads of layer-gated state with no guard: `var_14`,
  `g_pStepCellAbove` and `var_8` are each written only when the layer is below 3 and dereferenced
  regardless, so a probe on layer 3 reads a stale pointer or an unwritten stack slot.
- `0x27ae4` — for a large unit stepping due east the upward re-anchor asks the same cell to be floored
  (0x27a94) and floorless, so the anchor can never fire in that direction.
- `0x39A5C` — writes `nAmmo = 0` into `apTactItems[nTactItemCount]`, a slot that does not exist yet.
- `0x39B20` — the ammo switch has cases 31, 32, 34, 35 with `default: break`, so object index 33
  reuses the previous index's ammo total.
- `0x3C4C8` — the third equipment pickup stores unit, item and pointer but never zeroes the drag
  offset, unlike the other three sites, so a stale offset makes the re-test land inside no rect.
- `0x398B0` — mode 1 loads the selected-base byte with `LDRSB`; a base byte of 0x80 or more
  sign-extends and matches no soldier.
- `0x37718` — the 24-bit palette branch reads only the low 16 bits and writes back 16 bits, dropping
  blue on 24-bit displays.
- `0x1B324` and `0x1B608` — unhandled action-latch modes draw with a stale glyph value.
- `0x66B70` family — `Base_DemolishFacility` shifts the manufacturing quantity column to index `idx`
  while every other array shifts to `idx - 1`, so that column lands one base late. It also has no
  "found" test: if no facility type matches, it still drops the base count and deletes the last base.
- `0x22EF4` — `Map_BuildMissionTerrain` stores the result of `operator new(0x3C)` whether or not it
  succeeded and then calls `MapCell_Init` on it. `MapCell_Init` has no null guard, so a failed
  allocation is a write to address zero. `0x25740` is the same shape at the tail: the cell pointer is
  written through with no null test.
- `0x51264` vs `0x51E5C` — the same sample-height-to-layer conversion is written two ways inside one
  function: `(int)(z * (1/12))` in the shot loop, `(int)z / 12` in the three resolve loops. 1/12 is not
  exact in binary, so at a height that is an exact multiple of 12 the two land a layer apart, and the
  muzzle heights the aim setup builds are `12*nZ + 6`.
- `0x52718` — the thrown item's in-bounds test uses `160 * Map_nMapDimX` as the limit for Y as well as
  for X; `Map_nMapDimY` is never read there.
- `0x127A24` — `Map_bFireInFlight` is read at five sites and written nowhere in the image, so five
  branches of the fire phase are permanently one-sided. **This one is retracted**: it was this unit's own
  misreading. The field is `Map.bFireHitResult`, written by `Tact_TraceProjectileArc` at 0x2FC54 and 0x3010C,
  both through register-built offsets an xref search cannot see. The ledger row is deleted and the name is
  corrected. It stays listed because the mistake and its correction are the lesson.

---

## 8. How to build, run, test, verify

### Build

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build --config Release
```

Output: `build/src/pufo/Release/pocketufo.exe`.

Verified toolchain on this machine: CMake 4.2.3, Clang 23.1.1 (`clang-cl.exe` from VS 2022
BuildTools), MSVC toolset 14.44.35207, Windows SDK 10.0.26100.0. `/W4 /WX` is applied through the
`pufo_options` interface target, so **any warning is a build error**.

### Run

`lang.dat` and `main.dat` must sit next to the executable. Copy them into
`build/src/pufo/Release/` if they are not already there.

```bash
cd build/src/pufo/Release && ./pocketufo.exe
```

Expected: a window, the line `pocketufo: assets loaded (lang.dat=..., main.dat=...)`, and a live
process. Kill it with `taskkill //F //IM pocketufo.exe` — killing the shell's job PID does not kill
the window process.

Two debug flags (not part of the original) render headlessly and exit:

```bash
./pocketufo.exe --dump-frame out.ppm            # one start-menu frame as a 240x320 PPM
./pocketufo.exe --dump-frame out.ppm --click 120 148   # press+release at logical coords first
```

`--click` drives the real menu click path (`Input_OnPointer` marks the row, `Input_OnPointerUp`
fires its handler), so `--click 120 148` opens the seven-slot load list in the dump.

### Test

```bash
ctest --test-dir build -C Release
```

Expected: `100% tests passed, 0 tests failed out of 54`.

### Verify parity

```bash
python docs/parity/gen_report.py      # regenerates PARITY.md
python docs/parity/coverage.py        # coverage numbers only
python docs/parity/audit_credits.py   # claims next to denials
python docs/parity/audit_names.py     # cited addresses with no matching name
python docs/parity/audit_bugs.py      # bug ledger vs source markers
python docs/parity/audit_tables.py    # port tables vs the shipped image
```

All six must be clean. Expected right now: 0 negated citations, **0 name flags** (every credited address
now has a function defined for it in `src/pufo`), ledger and markers agree, 40 tables exact.

### Traps that will cost you a unit if you ignore them

These are all real, all paid for already.

- **An xref search proves nothing about a far Map field.** ARM cannot encode a displacement larger
  than 4095 in one instruction, so every access past `Map + 0xFFF` is built in a register
  (`MOV R8,#0x15000; ORR R8,R8,#0xD4; STRB R7,[R5,R8]`) and IDA records no data xref for it. Reading
  "all eight xrefs are reads" for `unk_128A8C` led the port to name a victory flag `pbMissionAborted`.
  For any Map field past +0xFFF, search the listing for the constant halves (`#0x15000`, `#0x1A00`)
  instead of trusting xrefs. This fired again on `Map_dTravelSave @ 0x14038`: the only two writers are
  `MOV R12,#0x14000; ORR R12,R12,#0x38; STR R3,[R4,R12]` at 0x302D0 and the same shape at 0x323BC, and a
  pre-indexed `STR R3,[R4,R3]!` writes are invisible the same way. "No xrefs" is not "never written". The
  cost of not believing this is measurable: `Map.bFireHitResult @ 0x1406C` was named `Map_bFireInFlight`,
  recorded in the bug ledger as "read at five sites, written nowhere in the image", and a whole paragraph of
  parity text was written about its five permanently one-sided branches. All of it was wrong — the field is
  cleared at 0x2FC54 (`MOV R11,#0x14000; ORR R11,R11,#0x6C; STRB R3,[R0,R11]!`) and set at 0x3010C through a
  stack-held pointer. Before you write a ledger row that says "never written", search the listing for the
  offset halves and read the function that the decompiler shows writing the struct member.
- **A read offset and a write offset in the same block are not the same index.** The stat refresh
  looked like `auStat[k] = ... + auStatBase[k]`, but the ARM pairs `LDRH R0,[R8,#Soldier.auStatBase+0x10]`
  (auStatBase[8]) with `STRH R0,[R8,#Soldier.auStat+0x12]` (auStat[9]), and `[R8,#0xF2]` (auStatBase[7])
  with `[R8,#0xB2]` (auStat[8]). auStatBase is a *compacted* nine-entry array — auStatBase[7] holds
  auStat[8], auStatBase[8] holds auStat[9] — so the port's single-index version read `auStatBase[9]`,
  past the end of the array. Whenever a read and a write in one block carry different offsets, the
  array is compacted and the pairing is part of the behaviour, not noise.
- **Hex-Rays can add a struct offset twice.** `TactState.aQueue` is at +0x18 and an entry is 56 bytes. The
  ARM computes `R0 = pState + 56*nQueueCursor` and then reads `[R0,#0x18]`; Hex-Rays renders that as
  `v5->aQueue[offsetof(TactState,bFlag0)]` — the 0x18 appears once in the pointer arithmetic and once again
  as the subscript, which reads as a 24-byte entry stride. The real stride is 56 and the real index is
  `nQueueCursor`. Recorded as an IDB comment at 0x54258. Whenever the pseudocode subscripts an array by
  `offsetof(<struct>, <some earlier member>)`, the field is not indexed by that member: recompute the stride
  from the ARM (`MOV R2,#0x38; MUL`, `ADD R0,R0,R2,LSL#3` shapes) before believing the subscript.
- **A byte flag compared to 1 may be read signed.** `abFacilityGrid[0x618]` is loaded with `LDRSB` at
  0x35160 and tested against 1, so -1 is *not* "set"; Hex-Rays renders the same test as an unsigned
  `char` comparison and a `!= 0` reading would take the failed-base-defence exit on a -1 cell. When a
  byte global gates a branch, read the load mnemonic: `LDRB` and `LDRSB` are different predicates.
- **`_rt_sdiv` / `_rt_udiv` take R0 = the DIVISOR and R1 = the dividend**, quotient back in R0,
  remainder in R1. Reading them in C order inverted the psi gate into `actor / victim`, which makes a
  weak attacker always succeed. The decisive site is `MapView_bResolveIsoCell @ 0x1B118`:
  `MOV R0,#2; MOV R1,R6; BL _rt_sdiv; MOVS LR,R1` — the `MOVS` tests the remainder of `nTileY % 2`, so
  R1 is the dividend. Hex-Rays models the convention correctly, so its operand order is trustworthy even
  when a hand reading of the ARM is not. A negative power-of-two division appears as
  `MOVS r,r; ADDMI r,r,#(k-1); ASR#k`, i.e. C truncating division.
- **`_itos`, `_muls`, `_stoi`, `_dtoi`, `_dtos` are coredll's SOFT-FLOAT helpers, not a fixed-point
  format.** Every interpretation in fixed point gave absurd numbers. Decode the constants as IEEE-754:
  1053609165 = `0x3ECCCCCD` = the single **0.4f** (fall damage), 939524096 = `0x38000000` = 1/32768
  (`Game_RandU16`'s scale). `_dtoi` truncates a double to int, which is how the thrown item's sub-cell
  flight end becomes a cell coordinate.
- **A reciprocal multiply and an integer divide are not the same expression.** `1/16` is exact in binary,
  `1/12` is not. `(int)(z * (1.0/12.0))` and `(int)z / 12` agree everywhere except at an exact multiple of
  12, where the first is one smaller. `Tact_ProcessFireAction` uses the reciprocal form in its shot loop
  (0x51264) and the integer form in its three resolve loops (0x51E5C, 0x52124, 0x527B8), and the muzzle
  heights the aim setup builds are `12*nZ + 6`, so the disagreement is reachable in normal play. Do not
  "normalise" one form into the other; keep both and let the test show the gap.
- **A flight sample array is measured from the muzzle, not from the unit.** The ten `Map_adMtX/Y/Z`
  samples sit at `dTravel - i * 1.41` from `Map_nAimOriginX/Y/Z`, so at travel 10 the first eight are still
  inside the muzzle cell and only the last two reach the next one. A "one sample per step" reading produced
  three wrong test expectations before the ARM was re-read.
- **`audit_bugs.py` resolves a ledger row's `port_location` as `file:Function`.** `file:Foo and FooAlt`
  parses as a function named `FooAlt` and the row fails to resolve, which turns the gate red. One function
  per row; put the second site in the evidence column.
- **A `@ 0xADDR` citation credits the address whether or not you ported it.** Writing an unported callee as
  `Unit_PathToCell @ 0x2c2a8` in a comment added 2,260 bytes to the headline coverage and, because the same
  comment said "unported", produced a NEGATED citation that `audit_credits.py` flags. Both gates caught it;
  the fix is the paren form `Name (0xADDR, unported)`, which the metric ignores. Never let a coverage number
  rise from a comment — a unit's delta must match that function's size in `docs/idb/inventory-grouped.tsv`.
- **Check what a callee does to the pointer you are about to read.** The chryssalid transform reads
  `pVictim->pCell` after calling `Unit_ResetBattleState`, which nulls `pCell` at 0x498E0 — the port's
  first version dereferenced NULL and crashed the test suite. The original saves the cell into
  `pState->pStepFromCell` first (0x53248) and places from the saved copy (0x53268). When a helper takes
  the unit as its argument, read what it writes before assuming the surrounding state survived.
- `cmake --build build -C Release` is **invalid**. CMake wants `--config Release`; only `ctest`
  takes `-C`. The invalid form prints a usage block and exits 1, and piped through
  `grep -iE "error|warning"` the usage text matches nothing, so a filtered build looks clean while
  nothing compiled. `echo "exit=$?"` after a pipeline reports the last stage, not cmake. A stale
  binary stays in place and still prints `PASS: 0 failure(s)`.
- A failed link leaves the previous test binary. ctest can report green after a failed build. After
  adding a cross-module call, run the test binary directly and check its timestamp against the
  sources. This fired again in `d4c74f1`'s follow-up: 48/48 passed while 19 test targets failed to
  link.
- **`pufo_game` and `pufo_ui` depend on each other, and a new cross-module symbol can expose it.**
  `pufo_strateg.c` (in `pufo_game`) calls `UiLineList_Remove` (in `pufo_ui`, `ui/pufo_linelist.c`),
  while `pufo_ui` links `pufo_game`. Most test targets link `pufo_game` but not `pufo_ui`, and they
  only survive because nothing pulls `pufo_strateg.obj` out of the archive. Adding one shared symbol
  in `pufo_ship.c` that `pufo_tactctl.c` referenced pulled `pufo_ship.obj` in, which transitively
  pulled `pufo_strateg.obj`, and 19 test targets failed with `undefined symbol: UiLineList_Remove`.
  The cycle is a separate problem and is NOT fixed here. Until it is, share a value between modules
  as a define in a header, not as an `extern` symbol.
- A running `pocketufo.exe` locks the executable and breaks the rebuild. Kill it first.
- Coverage delta must match the byte sizes in `docs/idb/inventory-grouped.tsv`, not a guess from
  decompile output. `Base_CountAmmoItems` is 116 bytes, not the 268 I predicted.
- `coverage.py` scans `src/pufo` only, not docs. A `Name @ 0xADDR` in a markdown heading does not
  credit anything. The false-credit trap is **source comments only**.
- Never use the `@` form for a function that is a hook, a blocker, context, or an unported
  dependency. Write `Name (0xADDR)`. This fired six separate times.
- `edit` with a section heading as `oldText` deletes that heading. Anchor on the exact tail text of
  the preceding section instead.
- Do not transcribe long byte dumps by eye. A 96-byte dump was mis-read and produced a wrong table.
  Compute from the image.
- Test helper argument order is not consistent across suites. `test_combat.c` is
  `Check(msg, cond)` / `CheckEqI32(msg, got, want)`. `test_tactctl.c`, `test_geoscape.c` and
  `test_invdrag.c` are `Check(cond, msg)` / `CheckEqI32(got, want, msg)` — `test_geoscape.c` also has
  `CheckEqU32(got, want, msg)`, used for the pixel signatures that pin a sprite index. `test_ship.c` has
  `CheckEqI32` only and no `Check`.
- `strcpy` and `fopen` are banned under `/WX` as deprecated. Use `pufo_strcpy` from
  `util/pufo_str.h` and `CreateFileW` plus `ReadFile`.
- `TactItem.nAmmo` is a `u8` at offset 0x06. Passing 999 is a constant-conversion error.
- An empty facility cell is 255. A zeroed grid fixture silently reads as 36 built facilities.
- Do not assert on pointer identity across a `free`/`malloc` pair. `test_save.c` used to check that
  `Soldiers_Reload` handed back addresses different from the roster it freed. The CRT reuses the two
  just-freed 740-byte blocks, so the check failed on a clean tree while the port was correct. It now
  checks the part that is actually the port's behaviour: after a reload that fails at the chunk
  lookup, the freed slots are NULL and the caller's count is untouched.

---

## 9. Current development objective

The goal (`mu5qflmn-o361c0`) is **paused** at 4 of 7 tasks complete.

- Complete: `scaffold`, `dat-layer`, `render-audio`, `ui-core`.
- Current: `ui-input` — recreate UI and input: windows, menus, dialogues.
- Pending: `game-core` — recreate core game: map/world, entities, game logic.
- Pending: `integrate-parity`.

The objective is unchanged: full behaviour parity across every subsystem, with per-subsystem parity
evidence and a final report that has no unexplained gaps.

---

## 10. Next milestones, ordered

### Priority 1 — make a tactical mission start

This is the single highest-value thing in the repository, because nothing playable exists until it
does.

1. Done: the nine `MapView_DrawUnit` tables are in `src/pufo/game/pufo_isodraw.c`, exported by
   `pufo_isodraw.h`, generated by `tools/gen_isodraw_tables.py`, and audited by
   `docs/parity/audit_tables.py`. `aUnitFacingDrawOff` is 14 types wide, not 23.
2. Done: `Tact_SpawnAliens` (0x3a2ac) is ported as `Tact_SpawnAliens(TactSpawnAliensEnv *)` in
   `pufo_tactctl.c`, with the nine static tables inlined and generated by
   `tools/gen_alien_tables.py`. The 0x3AB6C "stack overrun" turned out not to exist (see §7).
3. Done: `Tact_SetupCraftCargo` (0x38f00) is ported. The two globals it was blocked on turned out to
   be private scratch with no other reader, so they are caller-owned arrays in `TactCargoEnv` rather
   than new global models.
4. Done: `Map_BuildMissionTerrain` (0x22e10, 10,676 B) is ported in `src/pufo/game/pufo_terrain.c` as
   `Map_BuildMissionTerrain(MapTerrainEnv *)`, with `tests/test_terrain.c` (59 checks) and the four
   module-name tables generated by `tools/gen_terrain_tables.py`. It was the last Priority 1 item
   blocked on another Priority 1 item. The open decode questions closed on the way in; see §7.
5. Done: `Tact_InitMission` (0x3c550, 4,120 B) is ported in `src/pufo/game/pufo_tactinit.{h,c}` as
   `Tact_InitMission(TactInitEnv *, int nMode)`, with `tests/test_tactinit.c` (8 cases). It fills the
   eleven callee contexts the mission-start sequence hands out, so the tactical mission now runs end
   to end through the ported terrain builder, team select, alien spawn, cargo, equip, the two cell
   sweeps, the visibility walk and the showship reveal. What the step settled is in
   `docs/parity/game-core.md`; the short version is that the deploy-point sweep and the showship reveal
   both centre on `Map_nCraftMapX/Y @ 0x15139/3A`, which the terrain builder re-rolls, so neither
   follows the caller's craft position.
6. Done: `Tact_Shutdown` (0x3d568) is ported. Also done: `LoadGame` (0x44bbc, 3,400 B) is ported in
   `pufo_save.c` as `LoadGame(DatFile*, LoadGameEnv*)` with a `LoadGameHooks` seam for
   `Geoscape_InitUI (0x857f4)` and the env-heavy UI calls; the budget helpers
   `Base_CountOutboundTransferSpace @ 0x68044`, `Base_CountItemQtyAcrossBases @ 0x67d68`,
   `Base_CountPsiSoldiers @ 0x65a08` are ported in `pufo_geoscape.c` and called directly;
   `test_save` round-trips a sav7 slot through it. Two IDB errors fixed on the way (tactver lands
   at `MainMenu_pSlotCtx+0x118`, not `apObjects[4]`; the shot object is `apObjects[0]`).
7. Done: `MapView_DrawUnit` (0x1c958) is ported in `pufo_isodraw.c` behind `IsoUnitDrawGate`. Re-reading
   every branch in ARM changed four things the pseudocode had wrong or left unstated: the pose-frame
   branch converges on a store that happens before the zero test, so a zero entry is stored as 0; the
   direction classification compares `nX` first, so equal coordinates fall through to the diagonal set;
   `bAnimStep` ends at the step count because both loop tails store the increment before the test; and
   the group-base index is unclamped, so `PUFO_ISO_GROUP_BASE_COUNT` is 28 rather than 18. Note also
   that the compact `disasm` listing dropped the instruction at `0x1CE34` — the address-anchored
   listing is the one to trust for exact code.
8. The tactical action chain is now ported end to end at the processor level: `Tact_ProcessMoveAction`,
   `Tact_ProcessFallAction`, `Tact_ProcessFireAction` (0x50adc, 8,192 B), the walker that dispatches them
   (`Tact_TickActiveUnit`, 0x54130, 1,868 B), and both enemy-AI functions — the panic path
   (`EnemyAi_TryFireOrWander`, 0x4abc8, 1,208 B) and the driver that calls it
   (`Tact_EnemyAiTick`, 0x4cbe0, 3,440 B) — all driven from `tests/test_tactstate.c` and
   `tests/test_enemyai.c`. Two things still stand between that and a playable mission:
   - **App-layer wiring.** `GameTick_OnTimer`'s `pfnTickActiveUnit` seam is unfilled because the app layer owns
     no `TactTickCtx`, and nothing in the app builds a `Tact_EnemyAiTick` context (`EnemyAiTickCtx` needs a
     `TactSelectCtx`, a `TurnHooks`, the RMP data and the two static melee `TactItem`s) or an `EnemyAiActCtx`
     either.
   - **`Unit_PathToCell` (0x2c2a8, 1,052 B) is ported** (unit 69, `src/pufo/game/pufo_path.c`) and its signature
     matches the `pfnPathToCell` seam in `MapClickCtx`, `EnemyAiActCtx` and `EnemyAiTickCtx`, so the real
     function drops into all three. So is the whole *step predicate* the flood fill runs:
     `Map_ProbeStepOccupants` (0x2778c, 1,068 B, unit 70) and `Unit_CanStepTo` (0x27bb8, 14,716 B, unit 71), both
     in `src/pufo/game/pufo_step.c`, 82 checks in `tests/test_step.c`. `Unit_CanStepTo` already matches the
     `PathStepProbe` seam, so a `StepCtx` over the real `GameMap` is all that stands between the flood fill and
     real legality — and nothing in the app builds one yet.
   The enemy-turn group is now ported too: `BeforeEnemyStep` (0x4b9b8, 2,792 B) and `AfterEnemyStep`
   (0x4c4a0, 1,368 B) live in `src/pufo/game/pufo_turn.c` behind `EnemyStepCtx`, driven from
   `tests/test_turn.c`. What is still missing is the app layer building an `EnemyStepCtx` — its nine seams
   (`MapCell_TickProximityItems`, `Map_RefreshCellMeta`, `UnitList_ForEachRefresh`, `Unit_UpdateVisibility`,
   `Game_RandU16`, `Tact_SelectUnit`, `StatusMsg_Push`, `EnemyAi_TryFireOrWander`, `Rmp_PickSpawnForUnit`)
   are all ported functions, but nothing in the app wires them into one context yet.
   The tactical **input** side is ported too now: `Tact_HandleMapClick` (0x1512c, 1,104 B) in
   `src/pufo/game/pufo_input.c` behind `MapClickCtx` (driven from `tests/test_input.c`), and
   `TactAction_ConfirmMeleeMode6` (0x17bb8, 412 B) in `src/pufo/game/pufo_tactui.c` as
   `Ui_ConfirmMeleeMode6`, beside its siblings `Ui_ConfirmUseItem`, `Ui_TryFireMode`,
   `Ui_ConfirmFireBurst` and `Ui_BeginMultiTarget`. `Input_OnAction`'s `pfnHandleMapClick` seam is still
   unfilled — the app populates no `TactInputHooks` at all yet — so the map click and its now-ported move tail
   are driven only from the tests.

### Priority 2 — finish the tactical renderer

1. Done: `MapView_DrawUnitFacingArrow` (0x1b7bc, 2,692 B) is ported in `src/pufo/game/pufo_isodraw.c`
   behind `IsoArrowGate`, with 46 checks in `tests/test_isodraw.c`. The roster it reads was already
   modelled — `Map.adMtX/adMtY/adMtZ` at 0x13EE8/0x13F38/0x13F88 and `AimState.anAimRow` at 0x13FD8 —
   so what the step actually added was the gate struct, one 64-byte nudge table (`g_abArrowGlyphOff`,
   audited against both of its image addresses because the original stores it twice), and
   `IsoDrawCtx.nRosterIndex` at 0x114.
2. Done: `MapView_DrawIsoLayer` (0x21e2c, 1,252 B) is ported in `src/pufo/game/pufo_isodraw.c` behind
   `IsoLayerEnv`, with 25 checks in `tests/test_isodraw.c`. It is the only caller of the per-cell chain,
   so `IsoLayerEnv` is where the seven per-cell gates get assembled — `Render_Frame` will own it. Two
   passes per layer (floors first, everything else second), and the unresolved-cell fallback runs on
   layer 0 only.
3. Done: `MapView_DrawMinimap` (0x1ed4c, 2,112 B) is ported in `src/pufo/game/pufo_isodraw.c` behind
   `IsoMinimapEnv`, with 37 checks in `tests/test_isodraw.c`. Its two loop-bound globals
   `Map_nMinimapSpanX/Y` (Map+0x30/0x32) have no writer anywhere in the binary, so the shipped beta
   never runs the cell passes; the port models them as env inputs. Named this step:
   `Map_nMinimapScrollX/Y` (0x118864/0x118868), `pMinimapCursorPad` (0x1571F0) and `aTextRec17`
   (0xB466D). `IsoDrawCtx.szText` corrected to 80 bytes (the scratch dword sits at 0xFC) and
   `IsoSpriteBases` gained `nScangBase` (fontSys+0x2E).
4. Done: `MainMenu_Draw` (0x1dbb8, 2,680 B) is ported as `src/pufo/game/pufo_mainmenu.c` behind
   `MainMenuEnv`, with 25 checks in `tests/test_mainmenu.c`. It is the start-screen branch of
   `Render_Frame`: mode 0/1 draws four menu rows (labels from `aTextRecs` records 1076-1079 /
   1080-1083), mode 2/3 draws seven slot rows plus the standalone control, mode 3 also writes 40
   into `anActionPhaseSave0[112]` and opens the 1-line modal (record 1102). Named this step:
   `MainMenu_nListMode`, `MainMenu_pBgGlyph`, `MainMenu_anRowKeys`, `MainMenu_abRowDisabled`,
   `MainMenu_pStartCtl`, `MainMenu_pSlotCtx`, `anActionPhaseSave0_112`, `aTextRec1076/1080/1083`,
   `pszGameVersion`.
5. Done: `Tact_DrawCommandPads` (0x1fc8c, 4,044 B) is ported in `src/pufo/game/pufo_isodraw.c` behind
   `TactCmdPadEnv`, with 42 checks in `tests/test_cmdpads.c`. It is the screen-4 status panel: the
   'soldier' pad, the two cs pad slots (class 0..5 picks the pair; craft types 4..8/14..20/22 skip
   them; `uMissions <= 3` draws empty frames), eleven label/value rows from `aTextRecs` records
   56..66 with current/max bars, and bank widgets 101..103. The ruler at 0x20AD4 (12x24 5x1 ticks
   over the label rows) is an original bug, reproduced and logged. Named this step: `pPadSoldier`,
   `pCmdPadCs0/1/2`, `aTextRec56..66`.
6. Done: `Render_Frame` (0x22310, 756 B) is ported as `src/pufo/game/pufo_renderframe.c` behind
   `RenderFrameEnv` (18 checks in `tests/test_renderframe.c`); its geoscape branch injects
   `Geoscape_Draw` (0x8c180) from Priority 3.
7. Done: the start menu is clickable. The shell feeds the already-ported `Input_OnPointer` (0x13c6c)
   and `Input_OnPointerUp` (0x148d0) from the mouse messages through the letterbox inverse, on the
   same bank slice the menu controls live in. Press marks rows in `abRowDisabled`, release fires the
   first marked row's handler. `--dump-frame out.ppm --click 120 148` proves it headlessly: the click
   lands on `MenuCtl1_OnClick` (0x1a034) and the dump shows the seven-slot load list. Priority 2 is
   closed.
8. Done: the credits screen opens. `Titles_Init` (0x380d8) and `Titles_ctor` (0x46e88) are ported in
   `pufo_titles.c` (24 checks in `tests/test_titles.c`); the shell's `--click 120 170` (MenuCtl2)
   dumps the first credit line with stars entering. The credits scroll is state +87228, advanced by
   the 0xC352 screen-0x10 branch (`App_TickAnim`).

### Priority 3 — the geoscape

`Geoscape_InitUI` (0x857f4), `Game_StartNew` (0x94554) and the shell wiring (`app/pufo_world.c`) are done; `Geoscape_Draw` is the remaining link. `Geoscape_Draw` (0x8c180, 33,744 B) is the largest
function in the binary. `Geoscape_OnPointer` (0x872d0, 8,576 B) is ported in `game/pufo_geopointer.{c,h}` behind
`GeoscapePointerEnv` — 235 checks in `tests/test_geopointer.c` — but nothing in the app builds that env yet, so
`TactInputHooks.pfnGeoscapePointer` stays an unfilled seam; the env needs the plane panels, the options controls and
the screen-16 panels that `PufoWorldState` does not own. `Geoscape_OnPointerUp` (0x89450, 6,992 B) is ported next to it in the
same module and on the same env — 245 more checks in the same test file — and it is where the shortfall report
at 0x1870C8 gets cleared on a pointer release. Both pointer halves are in; neither is wired into the app yet. The select test the press calls twice
(`Smap_HitTestStrategSelect`, 0x82be8, 3,088 B) is ported in `pufo_baseui.c` on its own `SmapHitEnv` — 132 checks
in `tests/test_smaphit.c` — and reaches the dispatcher through `pfnHitTestStrategSelect`, so wiring the app means
building that env too: the strip pad, the status panel, the base info records and the TEXTS blob it reads labels
from.
The radar (intercept) screen is **done**:
`RadarScreen_BlitMapBackdrop` (0x5eb10), `RadarScreen_DrawFuelGauge` (0x5d918),
`RadarScreen_DrawRangeBar` (0x5d6e4), `RadarScreen_Init` (0x5f490), `RadarScreen_SetActive` (0x6144c),
`RadarScreen_RollDetection` (0x602a0), `RadarScreen_DrawCraftArmament` (0x5edf4),
`RadarScreen_DrawOverlay` (0x5dc84) and `RadarScreen_Update` (0x60570) are in — 15,812 of 15,812 bytes
across all 16 `RadarScreen_*` functions — and typing them is
what pinned `pSmapWorldBits`, `bRadarBlinkPhase`, `nSmapOverlayOriginX`, `aPaletteSlot29`,
`aRadarDigitPads`, `pRadarPercentPad`, `bRadarCraftOutOfFuel`, `nRadarRangeLimit`, `nRadarCraftLevel`,
`bRadarScreenActive`, `nInterceptShipIdx`, `nInterceptStrategIdx`, `aRadarControls`, `aRadarButtonPads`,
`aRadarCtlDesc`, `bRadarTraceLost`, `bRadarFlip`, `nRadarAmmoL`, `nRadarAmmoR`, `nInterceptTargetHpMax`,
`pRadarFramePad`, `pRadarUfoPad`, `pRadarUfoFlashPad`, `pRadarUfoMissilePad`, `nRadarCursorX`,
`nRadarCursorY`, `aTextRadarTraceLost`, `nRadarShotYL`, `nRadarShotYR`, `bRadarLastShotHit`,
`bRadarEngageL`, `bRadarEngageR`, `nRadarBoundL`, `nRadarBoundR`, `nRadarCooldownL`,
`nRadarCooldownR`, `bRadarInterceptDone` (was `unk_159650`), `nRadarScanOffset` (was `unk_159724`) and
`szScratch186488` (was `unk_186488`, the geoscape caption scratch `BaseFac_BuildStatusMsg` and
`Geoscape_Draw` share). Nothing of it is left: the next geoscape step is `Geoscape_Draw` itself, or the
screen families in Priority 4 that it draws.

### Priority 4 — screens

`strat-ui` is **complete: 245 of 245 functions and 121,164 of 121,164 bytes**. The buy/sell, transfer,
manufacturing, research, soldier and base-facility families are complete, and so is the funding screen:
`FundingScreen_Init` (0x5c084) with `FundingScreen_OpenMonthly` (0x5d160) in one unit, and
`FundingScreen_Toggle` (0x5abe4), `FundingScreen_Open` (0x5b560), `FundingScreen_Draw` (0x5b1fc) and
`FundingScreen_DrawExtended` (0x5b59c) in one more — the report groups those four under `strat`, which is
200 of 204 functions and 126,716 of 160,496 bytes (79.0%), 4 functions / 33,780 bytes left, 33,744 of
that one function: `Geoscape_Draw` (0x8c180). The base-info family is complete: `BaseInfoScreen_BuildPanels` (0x557f4, 6,084 B,
the largest single `strat-ui` function) and `BaseInfoScreen_Init` (0x57688) came in with unit 92,
`BaseInfoScreen_Draw` (0x56fb8) with unit 93, the base-attack screen's update/draw/close trio
(`BaseAtScreen_Update` 0x57808, 3,872 B, `BaseAtScreen_Draw` 0x58728, 420 B, `BaseAtScreen_CloseThunk`
0x577c0, 72 B) with unit 94, and the hangar's stores panel set `BasePlaneScreen_BuildControls` (0x7b014,
2,788 B) with unit 95 and the hangar's own frame and craft readout `BasePlaneScreen_Draw` (0x7d374,
1,648 B) with unit 96 and the hangar's stores list and craft detail
`BasePlaneScreen_DrawCraftDetail` (0x7baf8, 3,824 B) with unit 97 and the transfer error dump
`Transfer_WriteErrorDump` (0x996e4, 2,196 B) with unit 98 and the base facility grid's names, build icons
and craft `BaseFacScreen_DrawNameGrid` (0x61c8c, 1,840 B) with unit 99 -- the last `strat-ui` handler.
The psi family is complete: `PsiScreen_BuildPanels`, `PsiScreen_SelectListed`, `PsiScreen_Init`,
`PsiScreen_ApplyAssignments` and the two scroll handlers came in with unit 90, `PsiScreen_Draw` (0x72398,
1,960 B) with unit 91. The ufopaedia family is complete: `Ufopaedia_Open` (0x9a8cc) came in with unit 89 after its
paging and entry layout came in with unit 88. The options screen is complete:
`OptionsScreen_Init`, `OptionsScreen_Open` (0x6d0ac) and `OptionsScreen_OnConfirm` (0x6cf18) are all ported;
the click slot still reaches the last one through a thunk because the slot takes no arguments, which is the
`OptionsScreenHooks` shape §7 records as a separate issue.

### Priority 5 — housekeeping

- Extend `audit_tables.py` to the remaining hand-dumped tables: `s_aFilterPad[6]`, `g_aUiBankSpans`,
  `g_aUiNamedPad`, and the eleven `aUiDesc*` control tables. Some are derived from code bounds
  rather than image byte tables; record that distinction rather than forcing a byte comparison.
- The 23 sprite-set name slots inside `aXcom0` (0xA7BE8) are still one typed object
  (`char aXcom0[23][9]`) with the names listed in an IDB comment, not 23 separate IDB names: the MCP
  `rename` data form only accepts already-named globals and rejects the unnamed addresses with
  `Global '0xA7C39' not found`. In the port they are `g_aSpriteSetName[23][9]`, which is named.

---

## 11. Files to look at first

| Path | Why |
|---|---|
| `PARITY.md` | the generated parity report; start here for any "is X done" question |
| `docs/parity/coverage.py` | how a citation is counted; read before adding comments |
| `docs/parity/audit_credits.py`, `audit_names.py`, `audit_bugs.py`, `audit_tables.py` | the four integrity gates |
| `docs/parity/renames.tsv` | 531 renames on record per `audit_names.py`, **UTF-8** (that gate opens it with `encoding="utf-8"`, so cp1251 rows break it); check here before "porting" something a name scan says is missing |
| `docs/parity/bugs.tsv` | 261-row bug ledger, with a `qol` column |
| `docs/parity/game-core.md` | the running decode log; most of the hard-won detail lives here |
| `docs/idb/inventory-grouped.tsv` | authoritative function sizes per subsystem; use it to check coverage deltas |
| `src/pufo/game/pufo_mapctx.h` | the Map block layout and cell indexing |
| `src/pufo/game/pufo_unit.h` | Soldier layout with `_Static_assert` on every offset |
| `src/pufo/game/pufo_mapcell.h` | MapCell and Mc1Rec layouts |
| `src/pufo/game/pufo_isodraw.h` | IsoDrawCtx field map and the iso draw env structs |
| `src/pufo/app/pufo_app.c` | `wWinMain`, `WndProc`, the launch diagnostics |
| `docs/BUILD.md` | verified toolchain versions and build commands |
| `pufobeta.exe.i64` | the IDB. Session id for MCP is `f53a1851` |

### IDA MCP usage notes

- The `database` parameter is the **session id string** (`"f53a1851"`), never a file path.
- Batch inner calls must **not** carry `database` in their `arguments`; only the batch top level does.
- `rename` for data symbols takes `{old, new}`, not `{addr, name}`.
- `set_comments` takes `items`, and can fail at an address that is not a valid comment anchor; record
  the fact at a nearby valid anchor instead of forcing it.
- `get_bytes` with `regions: [{addr, size}]` is the reliable way to read constants and tables.
- Image mapping, verified independently: `IDA_VA - 0x13000 == file offset` into `pufobeta.exe`.
  `.data` follows that rule; above `0xAC800` is `.bss`.

---

## 12. Files intentionally left uncommitted

`git status` shows these as untracked. They are **not** accidental and are **not** to be committed.

**Original game artifacts**, needed at runtime or as ground truth for the table audits:
`pufobeta.exe`, `main.dat`, `lang.dat`, `fmodce.dll`, `gx.dll`, `save_editor.exe`,
`pUFO1.26RUS.cab`, `7z.exe`, `7za.dll`, `7zxa.dll`, `_setup.xml`.

`pufobeta.exe` is the reference binary that `audit_tables.py` reads. Without it that audit cannot run.

**Agent and tooling directories**: `.agents/`, `.claude/`, `.pi/`, `.ida-mcp/`.

**Scratch junk that should probably just be deleted**: `1`, `asd`, `test_bases.tmp`,
`0000SKKV.000`, `Testing/`. These have no purpose and were never referenced.

**Ignored by `.gitignore`**: `build/`, `.tmp/`, `__pycache__/`, `*.id0`, `*.id1`, `*.id2`, `*.nam`,
`*.til`, `.tmp_analyze*.json`.

`.tmp/` in particular holds IDA worker caches, decompile dumps and scratch C files from previous
sessions. It is large and it is ignored. Do not commit it.

No secrets are present in the repository.

---

## 13. Commit hygiene

Commits in this repository use an **explicit path list**:

```bash
git add -A -- src/pufo tests docs pufobeta.exe.i64 PARITY.md
```

Do not use a bare `git add -A`. The repository root contains the untracked game binaries and agent
directories listed in section 12, and a bare `-A` would commit them.

Before every commit: build, run ctest, run all five audits, and confirm the coverage delta equals
the byte sizes in `docs/idb/inventory-grouped.tsv`. Quote coverage numbers from a run in the same
message, never from memory.
