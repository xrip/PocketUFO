# Manual funding screen integration

This note covers the manual funding screen opened by root control 12. It is screen 15. The monthly funding report is screen 19 and has a different open and close path.

## Original behavior checked in IDA

The IDA comments were not used as proof. These facts come from the instruction streams:

- `Ui_OpenFunding @ 0x85164` loads sample `0x11`, calls `Sound_Play`, stores `0xF` to `nGeoscapeScreen`, then branches to `FundingScreen_Open`.
- `FundingScreen_Open @ 0x5B560` calls `FundingScreen_Toggle`. It then writes x `0xE0` (224), y `1`, and the `FundingScreen_CloseThunk` address into the shared close control.
- `FundingScreen_Toggle @ 0x5ABE4` checks and changes the open byte. On open it zeros the display total, allocates the funding pad and three panels, then reads the five bar and five arrow values into the panels. On close it clears the open byte and frees those panels and pad.
- `FundingScreen_CloseThunk @ 0x5ABD8` and `FundingScreen_ExitThunk @ 0x5C078` both load the geoscape screen block and branch to `Geoscape_CloseUI`. Its screen-15 branch calls `FundingScreen_Open` to toggle the manual panels off, then stores screen 0.
- `FundingScreen_OpenMonthly @ 0x5D160` increments the month, stores screen 19, calls `FundingScreen_Init`, then clears the UI animation byte. This is a separate path from root control 12.

## State ownership

The manual screen uses these original cells:

| Original state | Address | Use |
|---|---:|---|
| `pFundPad` | `0x1595DC` | One pointer slot. The manual toggle and monthly init each allocate their own pad instance into it. |
| `pFundPanelBars` | `0x1595E0` | Manual screen bar panel. |
| `pFundPanelNames` | `0x1595E4` | Manual screen country-name panel. |
| `pFundPanelArrows` | `0x1595E8` | Manual screen arrow panel. |
| `bFundScreenOpen` | `0x1595EC` | Manual screen draw gate. |
| `nFundDisplayTotal` | `0x159604` | Manual screen total, rebuilt from the five bar values. |
| `g_nFundBar0..4` | `0x186594..0x1865A4` | Values copied into the manual bars panel. |
| `g_nFundArrow0..4` | `0x1865A8..0x1865B8` | Values copied into the manual arrows panel. |

`FundingScreen_Init` uses the same `pFundPad` pointer slot, but allocates another pad and computes monthly funding. The manual root click must call `Ui_OpenFunding` and `FundingScreen_Open`; it must not call `FundingScreen_Init` or advance the month.

## Port state and live wiring

The C library already has `Ui_OpenFunding`, `FundingScreen_Toggle`, `FundingScreen_Open`, `FundingScreen_Draw`, `FundingScreen_Init`, and `FundingScreen_OpenMonthly` ([open wrapper](../../src/pufo/game/pufo_baseui.c#L103), [manual panel helpers](../../src/pufo/game/pufo_baseui.c#L5392), [monthly opener](../../src/pufo/game/pufo_baseui.c#L5330)). They remain unchanged.

`test_baseui` checks `Ui_OpenFunding` with a recording `pfnFundingOpen` callback ([stub setup](../../tests/test_baseui.c#L133)) and checks sample 17 plus screen 15 ([assertions](../../tests/test_baseui.c#L153)). It does not open the screen through the live world's root control or draw its panels.

`PufoWorldState` now holds a `FundingManualOwner` for the three panel pointers and persistent toggle/open envs ([owner type](../../src/pufo/app/pufo_world.h#L151), [owner field](../../src/pufo/app/pufo_world.h#L184)). `World_WireManualFunding` maps those envs to the original shared cells, the existing bar/arrow arrays, the shared close control, and the screen draw env ([wire helper](../../src/pufo/app/pufo_world.c#L2122)). The root wrapper calls `Ui_OpenFunding` through the existing screen hooks ([wrapper](../../src/pufo/app/pufo_world.c#L2109)).

The world integration points are now installed:

1. `World_FillInitUiEnv` assigns root click 12 to `World_OpenFundingClick`.
2. Geoscape setup and `World_RefreshGeoscapeFrame` call `World_WireManualFunding`. Refresh copies the current panel pointers after open or close.
3. Screen 19 keeps its separate monthly opener and `GeoscapeFundingArmEnv`.

The focused Release world run on 2026-10-25 verifies this wiring. Its `PressCtl`
helper sends the control center through `World_GeoscapePointer` and
`World_GeoscapePointerUp`; it does not call the click callback directly.

## Verified regression and limits

The actual-world frame test opens root 12 through the pointer route, checks screen
15, the open byte, pad and three panels, then draws the screen. Staged signed bar
values exercise the original clamp and produce a display total of 1400. Closing
through the shared control returns to screen 0, clears the open byte and releases
the manual records. This proves world wiring, draw state and cleanup. It does not
prove a full campaign's funding progression or this screen's Win32 mouse route.

Fresh log: `.workers/luna-parent/phase2-resume-focused.log`. The whole focused
test still fails in the separate guided-launcher pointer fixture; this funding
section passes.
