# Objective
Recreate the PocketUFO game (`pufobeta.exe`, WinCE, ~641 KB, 1103 functions) from the IDA PRO MCP database (`pufobeta.exe.i64`) as a full-featured Win64 build, achieving behavior parity with the original across every subsystem.
Update IDB with newly discovered or corrected information.

## Success criteria
- CMake + Clang project builds a Win64 executable with zero errors/warnings-as-errors.
- Every original subsystem is recreated: startup/config, data loading (main.dat, lang.dat, saves), map/world, entities, game logic, UI/input, rendering, audio.
- The binary launches on Win64 and the main playable flows work (load a map, interact with the world, UI responds).
- Per-subsystem parity evidence: each recreated module has a recorded comparison against the IDA decompilation of the corresponding original functions, with deviations explicitly listed.
- A final parity report mapping original functions → recreated code, with no unexplained gaps.

## Boundaries
- In scope: the `pufobeta.exe` game client and the data formats it reads (main.dat, lang.dat, save files); WinCE → Win64 Win32 API adaptation.
- Out of scope: `save_editor.exe` and other companion tools, original WinCE targets, mobile ports, new features not in the original, packaging/installer.

## Constraints
- Toolchain: C23, Clang, CMake (per project AGENTS.md).
- IDA PRO MCP is the source of truth: every behavior must be traceable to the decompilation/disassembly in `pufobeta.exe.i64`; do not guess behavior — verify in the IDB.
- YAGNI/KISS/DRY: recreate what the original does, nothing more; no speculative abstractions.
- Preserve original data-file formats so original .dat files work as-is.
- External dependencies of the original (e.g. fmodce.dll audio) are wrapped behind a thin interface; original DLLs are not re-linked.

## Verification contract
Before marking complete: (1) `cmake --build` succeeds clean; (2) the exe launches and the main flow runs on Win64; (3) the parity report exists and covers every subsystem, each entry checked against the IDA decompilation; (4) re-read this contract and confirm every success criterion is met with evidence.

## If blocked
Stop and ask the user. Expected blockers: IDA decompilation ambiguity (document and ask), missing original data files, unreproducible WinCE-only behavior.

# IMPORTANT: While the user talks to you in Russian, answer only in English.
## Rules
- Use the `basic-english` and `karpathy-guidelines` skills.
- Ask, do not assume. If something important is unclear, ask before making changes. When running unattended, use the most reasonable interpretation, continue, and record the assumption.
- Use the simplest good solution. Do not over-engineer or add flexibility that is not needed now.
- Do not change unrelated code. Report bad code or design problems you find so we can handle them as a separate issue.
- State uncertainty clearly. When useful, make a small, local, low-risk test and report the idea and result.
- Suggest better approaches when they give a clear or long-term benefit.
- DRY, KISS, YAGNI is our first-class mindset!
- Before each tool call, write one short sentence saying what you are about to do and why.

