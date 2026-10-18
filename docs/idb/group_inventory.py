"""Group the pufobeta.exe function inventory into recreation subsystems.

Input:  E:/pocketufo/docs/idb/functions.tsv   (addr, name, size)
Output: E:/pocketufo/docs/idb/inventory-grouped.tsv (addr, name, size, subsystem)
        stdout: per-subsystem counts + any unclassified names.

Rules are ordered; first match wins. Every function must classify.
"""
import re
import sys
import os
from collections import Counter

SRC = r"E:\pocketufo\docs\idb\functions.tsv"
OUT = r"E:\pocketufo\docs\idb\inventory-grouped.tsv"
IMPORTS_TSV = r"E:\pocketufo\docs\idb\imports.tsv"

# Load import names for platform thunk detection
IMPORT_NAMES = set()
if os.path.exists(IMPORTS_TSV):
    with open(IMPORTS_TSV, encoding="utf-8") as f:
        next(f)  # header
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2:
                IMPORT_NAMES.add(parts[1])  # name column

ROWS = [
    # (subsystem, [regexes])
    ("platform", [
        r"^j_", r"^nullsub_", r"^(start|_onexit|atexit|_cinit|Crt_)",
        r"^__imp_", r"^\?\?[0-9]@", r"^_rt_", r"^_[a-z]{3}$",  # _rt_sdiv, _itod, ...
        # The six CRT init-term entries carried IDA's nearest-import guess
        # (?SelectGdiObject@CDC@@...) until they were named in the IDB on 2026-10-18.
        r"^InitTerm_", r"^Debug_TracePrint$",
    ]),
    ("app-shell", [
        r"^(WinMain|InitInstance|PocketUFO_WndProc)",
        r"^App_", r"^Reg_(Get|Set)String", r"^(Start|Kill)Timers", r"CDialogTemplate",
    ]),
    ("util", [
        r"^PtrArray_", r"^Bank_", r"^Math_", r"^Game_RandU16",
        r"^(Inc|Dec)Dword", r"^Cache_", r"^Store_dword",
        r"^Set_byte_", r"^PlaySample7_SetFlags_Cache0xBD8",
    ]),
    ("dat", [r"^DatFile_", r"^AssetReader_"]),
    ("save", [r"^Save_", r"^(SaveGame|LoadGame)$", r"^SaveSlot_"]),
    ("ui-core", [
        r"^PadImage_", r"^UiBank_", r"^UiControl_", r"^UiPanel", r"^UiScrollList_",
        r"^UiModal_", r"^Ui_", r"^Text_", r"^StatusMsg_", r"^Font_",
        r"^UiScreen_", r"^EmptyBtn_", r"^Cydonia_Blit_Then_UiPanelMsg_Draw",
        r"^UiPulse_",
    ]),
    ("tact-ui", [
        r"^TactHud_", r"^TactUnit_", r"^Tact_(Draw|LoadUiPads|InitUiControls)",
        r"^MainMenu_Draw", r"^Input_", r"^Inv_", r"^InvSel_",
    ]),
    ("tact", [
        r"^Tact", r"^EnemyAi_", r"^(Before|After)EnemyStep", r"^Combat_",
        r"^Unit_", r"^Soldier_(Create|Generate|Pick|Find|Count|Promote|ctor|Reload|Remove|Format)",
        r"^Soldiers_Reload", r"^GameTick_", r"^GameSpeed_", r"^Map", r"^Path_",
        r"^Ship_(SpawnAtSlot|RemoveWithCargo)", r"^UnitList_",
    ]),
    ("strat-ui", [
        r"^BaseInfoScreen", r"^BaseAtScreen", r"^BaseFacScreen", r"^BasePlaneScreen",
        r"^BuySell", r"^ManufScreen", r"^PsiScreen", r"^Research", r"^SoldierScreen",
        r"^NewBaseScreen", r"^Transfer", r"^OptionsScreen", r"^Ufopaedia",
        r"^Ui_(DrawBaseInfo|InitBaseInfo|OpenBaseInfo|OpenResearch|OpenFunding|InitTransfer|ShowTError|OpenScreen18|CloseScreen18)",
        r"^Menu", r"^Options_", r"^SaveLoad_Slot",
        r"^(SaveGame_Slot|LoadGame_Slot)",
    ]),
    ("strat", [
        r"^Geoscape", r"^Smap", r"^Strateg", r"^RadarScreen", r"^GameTime",
        r"^Game_StartNew", r"^Funding", r"^Base", r"^Ship_", r"^Tilemap_",
        r"^Craft_",
    ]),
    ("titles", [r"^Titles_", r"^EndScreen_"]),
    ("boot-assets", [
        r"^Assets_LoadStartup", r"^GameUI_InitStartup", r"^StartUi_Shutdown",
        r"^GameAssets_Shutdown",
    ]),
    ("render", [
        r"^SoftSurf_", r"^Blit", r"^Render_", r"^Gfx_", r"^GX_", r"^Palette_Load",
    ]),
    ("audio", [r"^Sound_"]),
]
# Add a catch-all for import thunks: if the name is in the import set, treat as platform.
# We'll handle this in classify() after regexes.
COMPILED = [(s, [re.compile(p) for p in pats]) for s, pats in ROWS]


def classify(name: str) -> str:
    # First, check import thunks
    if name in IMPORT_NAMES:
        return "platform"
    for sub, pats in COMPILED:
        for p in pats:
            if p.search(name):
                return sub
    return "UNCLASSIFIED"


def main() -> None:
    rows = []
    with open(SRC, encoding="utf-8") as f:
        next(f)  # header
        for line in f:
            addr, name, size = line.rstrip("\n").split("\t")
            rows.append((addr, name, int(size)))

    out = []
    counts = Counter()
    unclassified = []
    for addr, name, size in rows:
        sub = classify(name)
        if sub == "UNCLASSIFIED":
            unclassified.append((addr, name, size))
        counts[sub] += 1
        out.append(f"{addr}\t{name}\t{size}\t{sub}")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("addr\tname\tsize\tsubsystem\n")
        f.write("\n".join(out) + "\n")

    print(f"total={len(rows)}")
    for sub, _pats in ROWS:
        print(f"{sub}\t{counts.get(sub, 0)}")
    if unclassified:
        print("UNCLASSIFIED:")
        for addr, name, size in unclassified:
            print(f"  {addr}  {name}  {size}")
        sys.exit(1)


if __name__ == "__main__":
    main()