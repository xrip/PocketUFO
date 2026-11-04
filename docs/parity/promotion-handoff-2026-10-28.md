# Mission results, promotion and persistent Soldier records

Status: bounded raw/source review; no additional handoff defect confirmed.
No game, test, decoder, debugger, audio or UI execution occurred.

The [worker report](../../.workers/luna-promotion-handoff/RESULT.md) and
[ARM capture](../../.workers/luna-promotion-handoff/ARM.txt) record the chain:

- Battle-end calls unit tally at0x35284. Eligible XCOM/civilian recovery paths
  increment missions at0x338BC..0x338C4 and0x33998..0x339A0.
- The clear/report call at0x35388 reaches promotion at0x3473C. Scoring reads
  missions at Soldier+0xB8 and kills at+0xFA as halfwords and computes
  `5 * (kills + 2 * missions)`. The ladder writes rank at+0xE2 and dirty at+0x1BC.
- World team selection places the same live Soldier pointers into the tactical
  roster. Loaded team IDs resolve against the live campaign roster. No copied
  Soldier object separates tally, promotion, save/load or detail display.
- Original save/read widths are two bytes for missions and kills, one for rank.
  World supplies the live Soldier array/count to save and load.

The separate [kill-credit producer repair](kill-credit-owner-2026-10-28.md)
corrects the previously disconnected source of earned kills. This review does
not widen that repair's tested scope or claim terminal-hit credit.

Existing promotion fixtures preset missions/kills and call the clear/promote
routine directly. Save fixtures preset record values; the existing roundtrip
does not assert missions. Detail fixtures use synthetic Soldiers. Those checks
do not establish a joined mission-result/promotion/save/reload flow.

Acceptance remains: finish a mission through the normal World caller, check
the same stable Soldier ID's earned missions/kills and rank, save/reload, then
check those fields and the detail display again. Current assertions remain
unrun. Original-save compatibility still lacks an original save sample.
