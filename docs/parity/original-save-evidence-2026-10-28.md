# Original save evidence gap

The user confirmed on2026-10-28 that no original WinCE-generated saves are
available. Do not ask for the same unavailable files again without new context.

The original pUFO1.26RUS.cab contains eight entries: installer metadata, the
game, main.dat, lang.dat, two DLLs and save_editor.exe. It contains no save.
The repository scan outside generated build/worker folders also found no
independent .sav. The save editor is outside the port's scope and was not run.

Port-generated save/load roundtrips and raw format checks remain useful, but
they do not empirically prove loading a save produced by the original game.
Keep that compatibility claim unverified. The remaining code, binding and raw
parity work can continue without such a sample. Do not run the original ARM
game or an emulator to create one without explicit execution approval.
