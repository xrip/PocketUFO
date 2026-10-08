/* pufo_options.c - the game's option block and its registry persistence.
 *
 * Evidence: Options_SaveToRegistry @ 0x114d8, Reg_SetString @ 0x9ed0c,
 *           Reg_GetString @ 0x9edbc, Sound_ApplyMusicVolume @ 0x9e454,
 *           Sound_ApplySfxVolume @ 0x9e4dc.
 */
#include "game/pufo_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/pufo_reg.h"

void Options_InitDefaults(PufoOptions *pOpt)
{
    memset(pOpt, 0, sizeof(*pOpt));
    /* Verified against Map_InitAndLoadRegistry @ 0x22648: each Reg_GetString call
     * supplies a literal default string, and the flag defaults are read as
     * `strcmp(value, "0") != 0`, so a default of "1" means set and "0" clear.
     *
     *   gamespeed    default "4"   (aad4c)
     *   gamma        default "2"   (aad48)
     *   soundvolume  default "5"   (aad44)
     *   musicvolume  default "5"
     *   musicoff/soundoff/forcenodrag/turnbypen   default "0" -> clear
     *   menubypen/runbydblclick/scrollbypen/showborder  default "1" -> set */
    pOpt->bScrollByPen   = 1;
    pOpt->bShowBorder    = 1;
    pOpt->bRunByDblClick = 1;
    pOpt->bTurnByPen     = 0;
    pOpt->bMenuByPen     = 1;
    pOpt->bMusicOff      = 0;
    pOpt->bSoundOff      = 0;
    pOpt->nSoundVolume   = 5;
    pOpt->nMusicVolume   = 5;
    pOpt->bForceNoDrag   = 0;
    pOpt->nGameSpeed     = 4;
    pOpt->nGamma         = 2;
}

/* Writes a number the way the original does: itoa() to base 10, then store as a
 * string. Reg_SetString is called with the text, never the raw integer. */
static void Options_SaveInt(const char *pszName, i32 nValue)
{
    char szBuf[32];

    snprintf(szBuf, sizeof(szBuf), "%d", (int)nValue);
    (void)Reg_SetString(pszName, szBuf);
}

/* The original writes "1" for a set flag and "0" for a clear one, using the
 * shared "1" @ 0xa62a8 and "0" @ 0xa62a4 strings. */
static void Options_SaveFlag(const char *pszName, u8 bSet)
{
    (void)Reg_SetString(pszName, bSet != 0 ? "1" : "0");
}

static i32 Options_LoadInt(const char *pszName, i32 nDefault)
{
    const char *psz = Reg_GetString(pszName, NULL);

    return psz == NULL ? nDefault : (i32)atoi(psz);
}

/* The original reads each numeric registry value through Reg_GetString into a
 * stack buffer, then applies an inclusive range check whose OUT-OF-RANGE fallback
 * is a fixed constant, not the caller's default. The two happen to be equal here,
 * but the distinction matters if a default ever changes, so the fallback is
 * spelled out explicitly:
 *
 *   gamespeed    valid 0..4, fallback 4
 *   gamma        valid 0..4, fallback 2
 *   soundvolume  valid 1..5, fallback 5
 *   musicvolume  valid 1..5, fallback 5
 *
 * A missing value takes the Reg_GetString default, which is then clamped like any
 * other value. Note the range is inclusive on both ends and the volume range
 * EXCLUDES 0 -- the original treats 0 as out of range and promotes it to 5. */
static i32 Options_LoadIntClamped(const char *pszName, i32 nDefault, i32 nMin,
                                  i32 nMax, i32 nFallback)
{
    i32 v = Options_LoadInt(pszName, nDefault);

    if (v > nMax || v < nMin) {
        return nFallback;
    }
    return v;
}

void Options_SaveToRegistry(const PufoOptions *pOpt)
{
    Options_SaveInt("gamespeed", pOpt->nGameSpeed);
    Options_SaveInt("gamma", pOpt->nGamma);
    Options_SaveInt("soundvolume", pOpt->nSoundVolume);
    Options_SaveInt("musicvolume", pOpt->nMusicVolume);
    Options_SaveFlag("musicoff", pOpt->bMusicOff);
    Options_SaveFlag("soundoff", pOpt->bSoundOff);
    Options_SaveFlag("forcenodrag", pOpt->bForceNoDrag);
    Options_SaveFlag("menubypen", pOpt->bMenuByPen);
    Options_SaveFlag("runbydblclick", pOpt->bRunByDblClick);
    Options_SaveFlag("scrollbypen", pOpt->bScrollByPen);
    Options_SaveFlag("showborder", pOpt->bShowBorder);
    Options_SaveFlag("turnbypen", pOpt->bTurnByPen);
}

void Options_LoadFromRegistry(PufoOptions *pOpt)
{
    PufoOptions def;

    Options_InitDefaults(&def);

    pOpt->nGameSpeed = Options_LoadIntClamped("gamespeed", def.nGameSpeed, 0, 4, 4);
    pOpt->nGamma     = Options_LoadIntClamped("gamma", def.nGamma, 0, 4, 2);
    pOpt->nSoundVolume = Options_LoadIntClamped("soundvolume", def.nSoundVolume, 1, 5, 5);
    pOpt->nMusicVolume = Options_LoadIntClamped("musicvolume", def.nMusicVolume, 1, 5, 5);
    /* Load mirrors save: read the string, compare to "0" for a clear flag. The
     * original uses strcmp directly, so any value other than "0" -- including an
     * empty string -- sets the flag; atoi would disagree for non-numeric text. */
    pOpt->bMusicOff     = (u8)(Options_LoadInt("musicoff", def.bMusicOff) != 0);
    pOpt->bSoundOff     = (u8)(Options_LoadInt("soundoff", def.bSoundOff) != 0);
    pOpt->bForceNoDrag  = (u8)(Options_LoadInt("forcenodrag", def.bForceNoDrag) != 0);
    pOpt->bMenuByPen    = (u8)(Options_LoadInt("menubypen", def.bMenuByPen) != 0);
    pOpt->bRunByDblClick = (u8)(Options_LoadInt("runbydblclick", def.bRunByDblClick) != 0);
    pOpt->bScrollByPen  = (u8)(Options_LoadInt("scrollbypen", def.bScrollByPen) != 0);
    pOpt->bShowBorder   = (u8)(Options_LoadInt("showborder", def.bShowBorder) != 0);
    pOpt->bTurnByPen    = (u8)(Options_LoadInt("turnbypen", def.bTurnByPen) != 0);
}

/* Sound_ApplyMusicVolume @ 0x9e454 and Sound_ApplySfxVolume @ 0x9e4dc:
 *     v = muted ? 0 : 12*step + 4;   level = 4*v - 1;
 * Muted yields -1, which is the original's arithmetic. See the header note. */
i32 Options_MusicLevel(const PufoOptions *pOpt)
{
    i32 v = pOpt->bMusicOff != 0 ? 0 : 12 * pOpt->nMusicVolume + 4;

    return 4 * v - 1;
}

i32 Options_SfxLevel(const PufoOptions *pOpt)
{
    i32 v = pOpt->bSoundOff != 0 ? 0 : 12 * pOpt->nSoundVolume + 4;

    return 4 * v - 1;
}
