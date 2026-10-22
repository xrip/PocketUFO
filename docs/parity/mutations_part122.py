#!/usr/bin/env python3
"""Part 122's mutation proofs: the 7 rows it closed (audio 4, boot-assets 3).

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part122.py
Each case names the original address whose behaviour the mutated line reproduces.
"""
import os
import sys

# `python -I` keeps the script's own directory out of sys.path, so name it explicitly.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

AUDIO_C = "src/pufo/audio/pufo_audio.c"
AUDIO_H = "src/pufo/audio/pufo_audio.h"
OPTIONS = "src/pufo/game/pufo_options.c"
MENUBTN = "src/pufo/ui/pufo_menubtn.c"
BOOT = "src/pufo/game/pufo_boot.c"

CASES = [
    # 0x9e208: FMUSIC_OptimizeChannels(mod, 16, 10) -- the 10 was unread by any test before Part 122.
    ("0x9e130 OptimizeChannels min volume 10 -> 8", "test_audio", AUDIO_C,
     b"        s_pBackend->pfnMusicOptimize(s_pMusicMod, 16, 10);",
     b"        s_pBackend->pfnMusicOptimize(s_pMusicMod, 16, 8); /* MUTATION */"),
    # 0x9e418 / 0x9e420: FMUSIC_FreeSong gated on g_bMusicLoaded.
    ("0x9e3d4 Shutdown's g_bMusicLoaded gate removed", "test_audio", AUDIO_C,
     b"    if (s_bMusicLoaded && s_pMusicMod != NULL) {\r\n        s_pBackend->pfnMusicFree(s_pMusicMod);",
     b"    if (1) { /* MUTATION */\r\n        s_pBackend->pfnMusicFree(s_pMusicMod);"),
    # 0x9e4f4 / 0x9e50c / 0x9e518: v = 12*level + 4, then 4*v - 1. The music side stays green.
    ("0x9e4dc sfx level 4*v-1 -> 4*v", "test_audio", OPTIONS,
     b"    i32 v = pOpt->bSoundOff != 0 ? 0 : 12 * pOpt->nSoundVolume + 4;\n\n    return 4 * v - 1;",
     b"    i32 v = pOpt->bSoundOff != 0 ? 0 : 12 * pOpt->nSoundVolume + 4;\n\n    return 4 * v; /* MUTATION */"),
    # 0x9e27c..0x9e2b4: attempt, guard, sleep -- 11 attempts for a guard of 10.
    ("0x9e268 FSOUND_Init guard 10 -> 9", "test_audio", AUDIO_H,
     b"#define PUFO_AUDIO_INIT_GUARD     10",
     b"#define PUFO_AUDIO_INIT_GUARD     9 /* MUTATION */"),
    # 0x38d7c: MOV R3,#0xE34 = 32*113 + 0x14, so the 15 groups cover bank widgets 113..127.
    ("0x38d7c pad range start 113 -> 112", "test_menubtn", MENUBTN,
     b"            UiControl *pCtl = &pEnv->pBank[113 + i];",
     b"            UiControl *pCtl = &pEnv->pBank[112 + i]; /* MUTATION */"),
    # 0x38d8c: MOV R8,#0xF -- 15 groups, so widget 127 is the last one covered.
    ("0x38d7c pad group count 15 -> 14", "test_menubtn", MENUBTN,
     b"        for (i = 0; i < 15; ++i) {",
     b"        for (i = 0; i < 14; ++i) { /* MUTATION */"),
    # 0x38ed8 / 0x38ee0: SoftSurf_dtor + delete of g_pMainSurface.
    ("0x38e4c Boot_Shutdown skips the surface release", "test_boot", BOOT,
     b"    if (pAssets->pSurface != NULL) {\r\n        if (g_pMainSurface == pAssets->pSurface) {",
     b"    if (0) { /* MUTATION */\r\n        if (g_pMainSurface == pAssets->pSurface) {"),
    # 0x382d0: OBJECTS.DAT split by the 0xD0 (208) record size.
    ("0x382d0 object record size 208 -> 200", "test_boot", BOOT,
     b"        pAssets->nObjectCount = (u16)(cb / sizeof(ObjectRecFull));",
     b"        pAssets->nObjectCount = (u16)(cb / 200); /* MUTATION */"),
]

if __name__ == "__main__":
    for name, target, path, old, new in CASES:
        run_case(name, target, path, old, new)
