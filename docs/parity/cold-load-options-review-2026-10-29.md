# Options after a cold campaign load

The port installed optionsHooks only in World_NewGame. A fresh World_LoadSlot
consumer could open screen18 but skip building its controls because the rebuild
hook's pfnInit remained NULL. A warm NewGame-then-load scenario retained those
callbacks and could hide this lifecycle gap.

## Raw contract and repair

Original Ui_OpenScreen18 sets screen18 and unconditionally branches from0x84EC0
to OptionsScreen_RebuildWithSound. That routine plays sample7 and unconditionally
branches from0x6CF14 to OptionsScreen_Init. Parent read both complete wrapper
bodies in [raw evidence](cold-load-options-parent-raw-2026-10-29.json).

The four hook assignments and their initialization moved into
World_WireGameContexts, used after Geoscape_InitUI by both new-game and cold-load
paths. No new hook abstraction was added. The
[context audit](../../.workers/luna-cold-load-contexts/RESULT.md) found no other
valid cold-load owner gap in its stated scope.

## Regression purpose

TestWorldColdLoadOptions writes a geoscape save with one World, frees that
producer, and loads into a separate World_Init consumer that never calls
World_NewGame. It uses the startup menu screen17 precondition copied by App's
load caller. It then opens the actual installed root Options control through
World_GeoscapePointer/Up and requires the controls-exist state, live consumer
owners, list rows and volume callbacks.

The save tab is pressed through the same pointer route. The test checks that all
seven slot callbacks bind and that the supplied formatter is called. Because the
tab reuses those rows, the load-tab callback is captured from its installed row
and invoked directly afterward. It checks binding, not a save/load slot commit.
The previous hook placement would leave the consumer initializer NULL and fail
the initialization checks. The fixture frees Worlds before its owned boot assets.

The [peer report](../../.workers/luna-cold-load-options-peer/RESULT.md) records
the raw/source check and limits. Parent corrected the new test's byte-pointer
comparison to use the same byte view as production and corrected a comment:
startup supplies screen17; the later menu shutdown writes22.

## Compile evidence and open acceptance

Both full Debug and Release targets returned exit0 with68 executable outputs and
zero diagnostics on unchanged source. The
[manifest](cold-options-build-artifacts-2026-10-29.json) records source, images,
data hashes and the earlier corrected test-compile failure.

No assertions or runtime paths ran. This prepared World test omits native app
startup/menu teardown, invokes a supplied formatter, and does not prove the
private App formatter's live clock binding. The separate
[slot-list owner repair](slot-list-owner-review-2026-10-29.md) has raw/source and
compile evidence; native Options/Save/Load acceptance remains open.
