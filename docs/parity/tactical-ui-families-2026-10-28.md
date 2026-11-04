# Tactical radio and sub-mode families

Updated: 2026-10-28. Valid-input source/raw contracts and callback bindings
checked; current runtime behavior unverified. No source change was needed.
No game, test or debugger executable was launched.

[Raw evidence](tactical-ui-families-raw-current.json) contains all13 handler
bodies, the sound callee, decoded constants and bank bindings. IDA combines
some ARM constant loads into multi-instruction items; the capture preserves
the full item byte spans and decodes literal loads from every 4-byte word.

## Nine sub-mode handlers

0x18418,0x18478,0x184D4,0x18534,0x18594,0x185F4,0x18654,0x186B4,0x18714
all call sound with R0=17, then write weapon byte8=1 and byte7=mode0..8.
They read the current action-mode index, multiply it by4 and load a halfword
at Map+0x4E4C+4*mode. Each subtracts that cost from the selected unit's
halfword at offset6 and stores the low16 result. There is no saturation.
Mode1 is shorter because it reuses the constant1 for both weapon stores.

Ui_SetSubMode implements those stores and casts the cost/result to u16.
Its cost index is pUi->nActionMode, not the selected sub-mode. World passes
the live selected weapon/unit and refreshes action mode from the Map owner.
The cost bank stays in sTactUi, filled by the production action-cost helper;
SyncIn does not replace it with an old input-context copy.

Documented input-domain adaptations remain: the port rejects missing state,
weapon or unit, and sub-modes outside0..8. Raw handlers assume valid objects.
This review does not prove the full action-cost calculation or resulting
weapon effect after a later turn.

## Four radio handlers

0x16E04,0x16E40,0x16E7C,0x16EB8 call sound17, then write a one-hot pattern
to bank offsets0x14A,0x16A,0x18A,0x1AA. Ui_SelectRadio produces the same
final pattern. It zeroes the whole small array before setting the selected
byte; the raw handlers store their four final bytes individually. No call
occurs between those raw byte stores.

World_TactUiSyncOut calls World_TactPushRadioBank immediately, copying the
model into widgets10..13. The movement wiring refreshes the first three
reserve/warning flags from that model each tick. This checks the connection
and copying; it does not re-prove the downstream armour/TU warning function.

## Original and port callback bindings

Raw0x3B498's setup loads bank0xAC6D0 into R4 at0x3B4C0. The literal at
0x3BD64 confirms that value. The handler address loads and following STRs
bind these callback fields:

| Raw handler | Widget index | Callback offset from bank |
| --- | ---: | --- |
| 0x16e04 | 10 | 0x14c |
| 0x16e40 | 11 | 0x16c |
| 0x16e7c | 12 | 0x18c |
| 0x16eb8 | 13 | 0x1ac |
| 0x18418 | 44 | 0x58c |
| 0x18478 | 45 | 0x5ac |
| 0x184d4 | 46 | 0x5cc |
| 0x18534 | 47 | 0x5ec |
| 0x18594 | 48 | 0x60c |
| 0x185f4 | 49 | 0x62c |
| 0x18654 | 50 | 0x64c |
| 0x186b4 | 51 | 0x66c |
| 0x18714 | 52 | 0x68c |

These are direct bank stores, not a local stack table. The32-byte original
widget stride and callback field offset0xC give the listed indices.
Radio state fields are two bytes before the corresponding callbacks.

The port's g_aTactBankClicks has the same handlers/indices. World supplies
that array and its live frame bank to Tact_InitUiControls; the latter assigns
apfnClick[i] into aCtl[i].pfnClick after setup. Each wrapper passes its own
mode/radio constant to the shared helper. Matching function bodies therefore
does not stand in for the binding check; both were inspected.

## Sound ABI check

Target0x9E0C0 is in pufobeta.exe, ARM32, Windows CE. Observed register ABI:
R0 carries the32-bit sample index, copied to saved R4. R1 is set to4 by all13
reviewed callers, but the callee overwrites it at0x9E0DC before reading it.
R2/R3 and incoming stack slots are not live inputs. There is one recoverable
live input; unobservable unused source-language formals cannot be recovered.

The port's World sample bridge reaches App's sample callback and Sound_Play.
Its one-argument interface is valid for these calls. Sound_Play's raw body
checks readiness, toggles the SFX byte, consumes Rand(200), then checks mute
and conditionally plays the sample. The port has the same operation order,
plus a documented index guard. Backend/audio parity is a separate requirement.

ABI skill audit:

| Check | Result/evidence |
| --- | --- |
| Platform/convention | pass: live IDB ARM,32-bit; R0 sample, saved R4, POP return; existing ARM __fastcall annotation retained |
| Incoming/calls | pass: all13 calls set sample17; R1 is overwritten before use; no stack argument read |
| Parameter type/name | pass within observed domain:32-bit array index, existing int nSample retained; signedness outside valid indices not newly inferred |
| Return paths/use | pass for retained scalar type: not-ready returns0, mute returns its byte value, playback leaves the backend channel result; reviewed handlers overwrite R0 and ignore it |
| Debug-string identifier evidence | n/a: no diagnostic identifier string in this body; existing names preserved |
| this/hidden result/varargs | n/a: index use, no object/result pointer or vararg access |
| Boolean-return rule | n/a: no reviewed Boolean use; backend result is numeric, so int is retained |
| noreturn/nonstandard registers | n/a: normal POP return; no unexplained live argument registers |
| Prototype write/recompile | n/a: no IDB edit required; read-back already int __fastcall(int nSample) |

The port discards the raw numeric return. This review proves that discard only
for the13 reviewed UI callers; other original return-use sites were not audited
here. No return-type change or blanket original source-signature claim is made.

## Test purpose and remaining proof

Existing test_tactui checks all four one-hot selections, all nine sub-modes,
one sample call, the selected action mode's cost, and u16 TU wrap-down.
Those are prepared-state leaf checks. They do not prove World pointer
installation, native input delivery, selected-item lifetime or movement
behavior after a real click. They were not executed in this pass.

Run the current candidate's leaf/full suites and live weapon/radio button
flows on an authorized host. Require matching visible radio states, reserve
behavior, selected weapon flags and TU consumption. No new runtime pass is claimed.

Only evidence files changed. The candidate source fingerprint remains
7CDDE20DFBE5670263B443AE14AE1FB06E5C9A1FE7681B63075136B9AE162752.
