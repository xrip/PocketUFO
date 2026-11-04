# Raw CRT and debug-stub contracts

Updated: 2026-10-28. Scope: seven CRT artifact claims and one debug stub.
Raw bodies are captured in [wrapper-raw-current.json](wrapper-raw-current.json).
No original, port, test or debugger executable was launched.

## Contracts recovered from instructions

| Original address | Raw contract | Win64 replacement and remaining limit |
| --- | --- | --- |
| 0x9E0B0 | Save incoming SP, push argument registers and saved SP/LR, then LDM restores SP and returns through saved LR. No output call, global store or arithmetic result. R0 is preserved. | No game trace operation is required for this compiled-out body. Stack scratch writes are not persistent game state. |
| 0xA1534 | Preserve R0-R3 in R4-R7, BL 0xA1628, restore four arguments, BL original WinMain, then branch to 0xA1688 with its return value. | Native Windows CRT enters wWinMain in pufo_main.c, which returns App_Run's result. Explicit game initialization still has to preserve the table effects below. Native CRT startup itself was not executed in this review. |
| 0xA1568 | Append an on-exit function pointer. If capacity is short, allocate 16 bytes or grow by 16 bytes, preserve the used-slot offset across relocation, store the callback, advance the end by 4, return callback; return null on allocation failure. | Host CRT owns its own callback registry. This private ARM heap/table layout is not a game save or data format. This does not excuse omitted callback registrations in the port. |
| 0xA1610 | Call 0xA1568; return 0 on non-null result, -1 on null result. | Host atexit contract. Port registration sites still require checking. |
| 0xA1628 | Call the function walker on [0xA6220,0xA6224), then tail-call it on [0xA61E0,0xA621C). | Host CRT plus explicit initialization; math uses .CRT$XCU, other observed source replacements are listed below. Order equivalence is not yet established for every entry. |
| 0xA1658 | Walk aligned 4-byte slots from begin while current < end; call each non-null function, increment by 4. | Host CRT callback tables and explicit source calls. Original pointer width is image-specific, not a Win64 data-format requirement. |
| 0xA1688 | Set R2=0 and R1=0, branch to 0xA1694; preserve R0 exit result. | Normal host CRT return/exit path. |
| 0xA1694 | Save exit result and flags; store the R2 flag. Unless R1 skips it, invoke registered callbacks backward, then the first termination table. Always invoke the second termination table. If R2=0, call the WinCE exit service with saved result; otherwise return. | Host CRT exit plus explicit World/asset/UI ownership cleanup. Reusing the WinCE service or private ARM registry is unnecessary, but callback effects and cleanup ownership remain required. Runtime exit/heap checks are pending. |

Key instruction checks independent of navigation names:
0xA1548 BL reaches 0xA1628; 0xA155C BL reaches 0x9FCC4.
0xA1670/0xA1674 conditionally set LR/PC for a non-null table pointer.
0xA1678 increments by 4; 0xA1680 branches on unsigned current < end.
0xA16C8 starts reverse traversal at end minus 4;
0xA16F4 decrements by 4 and 0xA16FC loops while current >= begin.
0x9E0BC (00a09de8) restores SP and PC from the saved pair; there is no
debug output call hidden behind its name.

## Live startup table data

Direct DWORD reads of the current IDB produced:

| Table slot | Entry |
| --- | --- |
| 0xA61E0 | 0 |
| 0xA61E4 | 0x11148 |
| 0xA61E8 | 0x22604 |
| 0xA61EC | 0x22638 |
| 0xA61F0 | 0x37074 |
| 0xA61F4 | 0x37084 |
| 0xA61F8 | 0x37094 |
| 0xA61FC | 0x370E4 |
| 0xA6200 | 0x37120 |
| 0xA6204 | 0x37130 |
| 0xA6208 | 0x3F0B4 |
| 0xA620C | 0x460B4 |
| 0xA6210 | 0x61498 |
| 0xA6214 | 0x83870 |
| 0xA6218 | 0x83880 |
| 0xA621C | 0, exclusive table end |

The first startup range contains one zero at 0xA6220. Each exit range
[0xA6228,0xA622C) and [0xA6230,0xA6234) also contains one zero.
Thus the 14 non-null startup entries and dynamically registered callbacks
carry the initialization/termination effects; the empty fixed exit ranges do not.

## Source replacements checked in this pass

- App_InitStartupUi calls UiBank_CtorAll and Bank_RegisterAtexit, then
  Map_InitAndLoadRegistry with real option and tactical-state cells.
  These are explicit calls, not conclusions drawn from source comments.
- IsoMath_CrtInit invokes both constant setters via a .CRT$XCU entry.
  The original startup slots 0xA61F8/0xA61FC reach their store functions
  through one branch each. This checks the initialization connection, not
  the full floating-point equivalence of the math bodies.
- The five item-related startup wrappers reach the item constructor.
  Raw 0x3E718..0x3E72C writes zero at offsets 0xC, 8, 7, 0x10 and returns.
  The port constructor clears the corresponding next link, two flags, and
  linked-soldier pointer. g_itemSlotEmpty is static zero-initialized;
  World_Init zeroes the World containing the other tactical item records.
  This supplies these constructor defaults. It does not prove all later item uses.
- World_Init explicitly calls PtrArray_ctor for strategList.
- DatFile_ctor is called on the actual local save/load reader in World,
  replacing the original global reader's ownership pattern.
- AssetReader_Clear zeroes each caller-owned reader, including its found flag.
  It also clears fields the original global clear leaves stale; that adaptation
  is visible in source and requires valid reader call-order use.
- App_ShutdownWorld calls World_Free; the debug teardown also calls
  Boot_Shutdown and Gx_Close. These are observed cleanup connections,
  not a proof that every normal/failure exit has the right order or no leaks.

## Disposition and open work

The seven private CRT bodies are platform machinery with explicit contracts,
not seven missing game features. The debug body has no output behavior to port.
The function ledger now records this scoped raw review without marking whole
function/adapter parity complete.

Continue with the 14 startup effects, registrations, and normal/failure shutdown
ordering. In particular the font and archive-reader lifetimes, UI atexit registration,
and explicit World cleanup must be reconciled with the raw registration sequence.
No missing feature was confirmed in this bounded review. No source fix was made,
so the compile-verified candidate remains unchanged.
