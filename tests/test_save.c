/* test_save.c - checks save-file container and soldier record round-tripping.
 *
 * The record layout is derived field by field from Save_WriteSoldiers @ 0x42350.
 * The strongest check here is the byte count: the non-tactical prefix must be
 * exactly 740 bytes, because every field was transcribed from the write order and
 * any omission or double-write would change the total.
 */
#include "asset/pufo_asset.h"
#include "game/pufo_geoscape.h"
#include "game/pufo_save.h"
#include "game/pufo_unit.h"
#include "util/pufo_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static void CheckEqStr(const char *pszGot, const char *pszWant, const char *pszWhat)
{
    if (strcmp(pszGot, pszWant) == 0) {
        printf("  ok   %s (\"%s\")\n", pszWhat, pszGot);
    } else {
        printf("  FAIL %s: got \"%s\", want \"%s\"\n", pszWhat, pszGot, pszWant);
        g_nFailures += 1;
    }
}

static void TestSlotPath(void)
{
    char szPath[64];

    printf("Save_BuildSlotPath\n");
    Check(Save_BuildSlotPath(szPath, sizeof(szPath), "C:\\game\\", 3), "  built");
    CheckEqStr(szPath, "C:\\game\\sav3.sav", "  matches szAppDir + sav + N + .sav");

    Check(Save_BuildSlotPath(szPath, sizeof(szPath), "", 0), "  empty dir still builds");
    CheckEqStr(szPath, "sav0.sav", "  no separator when the dir is empty");

    /* Bounds: a tiny buffer must fail rather than truncate silently. */
    Check(!Save_BuildSlotPath(szPath, 4, "C:\\game\\", 3), "  short buffer refused");
}

/* Fills a soldier with distinctive values so any field swap is detectable. */
static void FillSoldier(Soldier *pUnit)
{
    memset(pUnit, 0, sizeof(*pUnit));
    pUnit->bFlags = 0x3F;      /* all six saved bits set */
    pUnit->bFacing = 5;
    pUnit->uTu = 0x1234;
    pUnit->uStunOrArmor = 0x5678;
    pUnit->uHp = 0x9ABC;
    pUnit->bMorale = 77;
    pUnit->nAnimPose = 3;
    pUnit->nDrawOffX = -4;
    pUnit->nDrawOffY = 6;
    pUnit->bStatus = 0;
    pUnit->uTuSpent = 0x0DEF;
    pUnit->nMultiTarget = 2;
    pUnit->bUnk148 = 1;
    pUnit->bPsiAssigned = 1;
    pUnit->nLook = 0x11223344;
    pUnit->bFemale = 1;
    pUnit->nUnitType = 4;
    for (i32 i = 0; i < PUFO_UNIT_STAT_COUNT; ++i) {
        pUnit->auStat[i] = (u16)(100 + i);
    }
    pUnit->nKind = 0;
    pUnit->bUnk183 = 0xFF;
    pUnit->uMissions = 0x0BEE;
    pufo_strncpy(pUnit->szName, "Jane Doe", sizeof(pUnit->szName));
    pUnit->bRankType = 2;
    for (i32 i = 0; i < 9; ++i) {
        pUnit->auStatBase[i] = (u16)(200 + i);
    }
    pUnit->bKneelOrPrimed = 3;
    pUnit->uKills = 0x0ACE;
    for (i32 i = 0; i < 5; ++i) {
        pUnit->abReady252[i] = (u8)(1 + i);
    }
    pUnit->bUnk961 = 1;
    for (i32 i = 0; i < 25; ++i) {
        pUnit->auUnk962[i] = (u16)(300 + i);
        pUnit->abUnk1012[i] = (u8)i;
    }
    pUnit->bHumanoidInv = 1;
    pUnit->uInvBurden = 0x0FED;
    pUnit->bPsiActive = 7;
    pUnit->nArmorPoints = 0x0AABBCCD;
    pUnit->nClass = 0x01020304;
    pUnit->nUnk436 = 0x05060708;
    pUnit->nUnk440 = 0x090A0B0C;
    pUnit->nPsiTrainDays = 3;
    pUnit->bInTact = 1;
}

static void TestSoldierRoundTrip(void)
{
    static DatFile dat;
    Soldier        aWritten;
    u8             aRaw[2048];
    u32            cbRead = 0;
    i32            nRaw;

    printf("Save_WriteSoldier / Save_ReadSoldier round trip\n");
    remove("test_save.tmp");
    DatFile_ctor(&dat);
    Check(DatFile_Open(&dat, "test_save.tmp", PUFO_DAT_CREATE, 4), "  container opened");

    FillSoldier(&aWritten);
    {
        Soldier        *apSoldiers[1];
        Soldier        *apXcom[30];
        TeamUnitsState  rosters;

        memset(apXcom, 0, sizeof(apXcom));
        apXcom[0] = &aWritten;
        memset(&rosters, 0, sizeof(rosters));
        rosters.nXcomCount  = 1;
        rosters.apXcomUnits = apXcom;

        apSoldiers[0] = &aWritten;
        /* Must go through the chunk wrapper: DatFile_Write refuses without an
         * open chunk, exactly as the original requires. The rosters decide bInTact:
         * the soldier stands in apXcomUnits, so it is flagged into the mission. */
        Check(Save_WriteSoldiers(&dat, apSoldiers, 1, 1, &rosters), "  soldier written");
    }
    DatFile_Close(&dat);

    /* The exact prefix size is the layout check. */
    {
        HANDLE hFile = CreateFileW(L"test_save.tmp", GENERIC_READ, FILE_SHARE_READ,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD cb = 0;

        Check(hFile != INVALID_HANDLE_VALUE, "  file reopenable");
        if (hFile != INVALID_HANDLE_VALUE) {
            (void)ReadFile(hFile, aRaw, sizeof(aRaw), &cb, NULL);
            (void)CloseHandle(hFile);
        }
        /* Header is 4 + 21*4 = 88, then the tacticver chunk (21-byte dir slot
         * already counted), so the soldier data starts at 88 + 2. */
        nRaw = (i32)cb;
        /* 4 + 21*4 entries = 88 header; then the `soldiers` chunk payload is
         * u16 count + 740-byte record + stactflg (1) + 1_24scb (4). */
        Check(nRaw == 88 + 2 + PUFO_SAVE_SOLDIER_PREFIX + 1 + 4,
              "  file is header + count + 740-byte record + 5 trailing bytes");
    }

    /* Now read it back through the container. */
    DatFile_ctor(&dat);
    Check(DatFile_Open(&dat, "test_save.tmp", PUFO_DAT_READ, 0), "  reopened for read");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_SOLDIERS), "  soldiers chunk found");
    {
        static Soldier aBacking[1];
        Soldier        *apSoldiers[1];
        i32            nCount = 0;

        memset(aBacking, 0, sizeof(aBacking));
        apSoldiers[0] = &aBacking[0];
        Check(Save_ReadSoldiers(&dat, apSoldiers, &nCount, 1, NULL, 0), "  soldiers read");
        CheckEqI32(nCount, 1, "  one soldier");

        /* Spot-check the fields that matter, plus one deep field to prove the
         * whole prefix was traversed in the right order. */
        CheckEqI32(aBacking[0].bFlags, aWritten.bFlags, "  flags round-trip");
        CheckEqI32(aBacking[0].bFacing, aWritten.bFacing, "  facing round-trip");
        CheckEqI32(aBacking[0].uTu, aWritten.uTu, "  TU round-trip");
        CheckEqI32(aBacking[0].uHp, aWritten.uHp, "  HP round-trip");
        CheckEqI32(aBacking[0].bMorale, aWritten.bMorale, "  morale round-trip");
        CheckEqI32(aBacking[0].nLook, aWritten.nLook, "  look round-trip");
        CheckEqI32(aBacking[0].bFemale, aWritten.bFemale, "  gender round-trip");
        CheckEqI32(aBacking[0].nUnitType, aWritten.nUnitType, "  type round-trip");
        CheckEqI32(aBacking[0].nKind, aWritten.nKind, "  kind round-trip");
        CheckEqI32(aBacking[0].bRankType, aWritten.bRankType, "  rank round-trip");
        CheckEqI32(aBacking[0].uKills, aWritten.uKills, "  kills round-trip");
        CheckEqI32(aBacking[0].nArmorPoints, aWritten.nArmorPoints, "  armor round-trip");
        CheckEqI32(aBacking[0].nClass, aWritten.nClass, "  class round-trip");
        CheckEqI32(aBacking[0].nUnk440, aWritten.nUnk440, "  roster id round-trip");
        CheckEqStr(aBacking[0].szName, aWritten.szName, "  name round-trip");
        CheckEqI32(aBacking[0].auStat[0], aWritten.auStat[0], "  stat[0] round-trip");
        CheckEqI32(aBacking[0].auStat[9], aWritten.auStat[9], "  stat[9] round-trip");
        CheckEqI32(aBacking[0].auStatBase[8], aWritten.auStatBase[8], "  statBase[8] round-trip");
        CheckEqI32(aBacking[0].uInvBurden, aWritten.uInvBurden, "  burden round-trip");
        CheckEqI32(aBacking[0].bInTact, 1, "  bInTact round-trip via stactflg");
    }
    (void)cbRead;
    DatFile_Close(&dat);
    /* Verify the derived size constant itself. */
    CheckEqI32(PUFO_SAVE_SOLDIER_PREFIX, 740, "  prefix constant is 740 bytes");

    remove("test_save.tmp");
}

/* An independent structural check: write the same record twice and confirm the
 * bytes are identical, which proves there is no uninitialised padding in the
 * record (every byte comes from a named field). */
static void TestRecordDeterminism(void)
{
    static DatFile dat;
    Soldier        aUnit;
    u8             aFirst[2048];
    u8             aSecond[2048];
    DWORD          cb1 = 0;
    DWORD          cb2 = 0;

    printf("Save_WriteSoldier emits identical bytes for identical input\n");
    FillSoldier(&aUnit);

    for (int pass = 0; pass < 2; ++pass) {
        HANDLE hFile;
        u8     *pOut = (pass == 0) ? aFirst : aSecond;

        remove("test_save2.tmp");
        DatFile_ctor(&dat);
        (void)DatFile_Open(&dat, "test_save2.tmp", PUFO_DAT_CREATE, 2);
        (void)Save_WriteSoldier(&dat, &aUnit, 0);
        DatFile_Close(&dat);

        hFile = CreateFileW(L"test_save2.tmp", GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD cb = 0;

            (void)ReadFile(hFile, pOut, sizeof(aFirst) > 2048 ? 2048 : 2048, &cb, NULL);
            if (pass == 0) {
                cb1 = cb;
            } else {
                cb2 = cb;
            }
            (void)CloseHandle(hFile);
        }
    }
    Check(cb1 == cb2 && cb1 > 0, "  both writes produced the same size");
    Check(cb1 == cb2 && memcmp(aFirst, aSecond, cb1) == 0,
          "  both writes produced identical bytes (no uninitialised padding)");
    remove("test_save2.tmp");
}

/* --- the small chunks ------------------------------------------------------ */

/* A tiny helper: open a fresh container for writing. Keeps each test's boilerplate
 * down to one line. */
static bool OpenWrite(const char *pszPath, DatFile *pDat, i32 nEntries)
{
    remove(pszPath);
    DatFile_ctor(pDat);
    return DatFile_Open(pDat, pszPath, PUFO_DAT_CREATE, nEntries);
}

static bool OpenRead(const char *pszPath, DatFile *pDat)
{
    DatFile_ctor(pDat);
    return DatFile_Open(pDat, pszPath, PUFO_DAT_READ, 0);
}

/* Size of a freshly written container, for the exact-payload checks. */
static i32 FileSizeOf(const wchar_t *pwPath)
{
    HANDLE hFile = CreateFileW(pwPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    i32 cb = -1;

    if (hFile != INVALID_HANDLE_VALUE) {
        cb = (i32)GetFileSize(hFile, NULL);
        (void)CloseHandle(hFile);
    }
    return cb;
}

/* The gamestate chunk is the clearest case of the original's save/load asymmetry:
 * what is written is the tactical flag, what is read back goes into a DIFFERENT
 * slot-state byte. The port keeps both cells explicit. */
static void TestGameStateChunk(void)
{
    static DatFile dat;
    u8             bSlotState = 0xEE;

    printf("Save_WriteGameState / Save_ReadGameState\n");
    Check(OpenWrite("test_save3.tmp", &dat, 2), "  container opened");
    Check(Save_WriteGameState(&dat, 1), "  wrote the tactical flag");
    DatFile_Close(&dat);

    /* 4 + 2*21 = 46 header bytes, then the chunk's one data byte. */
    CheckEqI32(FileSizeOf(L"test_save3.tmp"), 46 + 1, "  file is header + one byte");

    Check(OpenRead("test_save3.tmp", &dat), "  reopened");
    Check(Save_ReadGameState(&dat, &bSlotState), "  read the gamestate byte");
    CheckEqI32(bSlotState, 1, "  the byte landed in the slot byte, not the flag");
    DatFile_Close(&dat);

    Check(!Save_WriteGameState(NULL, 1), "  NULL dat refused");
    Check(!Save_ReadGameState(NULL, &bSlotState), "  NULL dat refused on read");
    Check(!Save_ReadGameState(&dat, NULL), "  NULL output refused");
    remove("test_save3.tmp");
}

static void TestObjBeforeChunk(void)
{
    static DatFile dat;
    ObjectRecFull  aObjs[3];
    ObjectRecFull *apObjs[3];

    printf("Save_WriteObjBefore / Save_ReadObjBefore\n");
    memset(aObjs, 0, sizeof(aObjs));
    for (i32 i = 0; i < 3; ++i) {
        aObjs[i].uRecovScore = (u16)(100 + i);
        aObjs[i].uRecovCount = 0x77;
        apObjs[i] = &aObjs[i];
    }

    Check(OpenWrite("test_save4.tmp", &dat, 2), "  container opened");
    Check(Save_WriteObjBefore(&dat, apObjs, 3), "  wrote three objects");
    DatFile_Close(&dat);

    /* Only the u16 at 0xCA is serialised: 3 objects -> 6 data bytes. */
    CheckEqI32(FileSizeOf(L"test_save4.tmp"), 46 + 6,
               "  file is header + one u16 per object");

    for (i32 i = 0; i < 3; ++i) {
        aObjs[i].uRecovScore = 0;
    }
    Check(OpenRead("test_save4.tmp", &dat), "  reopened");
    Check(Save_ReadObjBefore(&dat, apObjs, 3), "  read three objects");
    CheckEqI32(aObjs[0].uRecovScore, 100, "  object 0 score");
    CheckEqI32(aObjs[1].uRecovScore, 101, "  object 1 score");
    CheckEqI32(aObjs[2].uRecovScore, 102, "  object 2 score");
    /* The count field is NOT serialised, so it must be untouched. */
    CheckEqI32(aObjs[0].uRecovCount, 0x77, "  the count byte was not written");
    DatFile_Close(&dat);

    Check(!Save_WriteObjBefore(NULL, apObjs, 3), "  NULL dat refused");
    Check(!Save_WriteObjBefore(&dat, NULL, 3), "  NULL array refused");

    /* A NULL element is refused rather than dereferenced. */
    Check(OpenWrite("test_save5.tmp", &dat, 2), "  container opened");
    apObjs[1] = NULL;
    Check(!Save_WriteObjBefore(&dat, apObjs, 3), "  NULL element refused");
    DatFile_Close(&dat);
    remove("test_save4.tmp");
    remove("test_save5.tmp");
}

static void TestTimeChunk(void)
{
    static DatFile dat;
    SaveTimeRecord aWritten;
    SaveTimeRecord aRead;

    printf("Save_WriteTime / Save_ReadTime\n");
    memset(&aWritten, 0, sizeof(aWritten));
    aWritten.nSlot105 = 0x11223344;
    aWritten.nSlot106 = 0x55;
    aWritten.nSlot103 = 0x66778899;
    aWritten.nSlot102 = 0xAABBCCDD;
    aWritten.nSlot101 = 0x01020304;
    aWritten.nSlot114 = 0x05060708;
    aWritten.nSlot115 = 0x090A0B0C;
    aWritten.nWord1472C8 = 0x0D0E;

    Check(OpenWrite("test_save6.tmp", &dat, 2), "  container opened");
    Check(Save_WriteTime(&dat, &aWritten), "  time written");
    DatFile_Close(&dat);

    /* 4 + 1 + 4*5 + 2 = 27 data bytes. */
    CheckEqI32(FileSizeOf(L"test_save6.tmp"), 46 + 27,
               "  file is header + 27 time bytes");

    memset(&aRead, 0, sizeof(aRead));
    Check(OpenRead("test_save6.tmp", &dat), "  reopened");
    Check(Save_ReadTime(&dat, &aRead), "  time read");
    CheckEqI32((i32)aRead.nSlot105, 0x11223344, "  slot 105 round-trips");
    CheckEqI32(aRead.nSlot106, 0x55, "  slot 106 round-trips");
    CheckEqI32((i32)aRead.nSlot103, 0x66778899, "  slot 103 round-trips");
    CheckEqI32((i32)aRead.nSlot102, (i32)0xAABBCCDDu, "  slot 102 round-trips");
    CheckEqI32((i32)aRead.nSlot101, 0x01020304, "  slot 101 round-trips");
    CheckEqI32((i32)aRead.nSlot114, 0x05060708, "  slot 114 round-trips");
    CheckEqI32((i32)aRead.nSlot115, 0x090A0B0C, "  slot 115 round-trips");
    CheckEqI32(aRead.nWord1472C8, 0x0D0E, "  the trailing word round-trips");
    DatFile_Close(&dat);

    Check(!Save_WriteTime(NULL, &aWritten), "  NULL dat refused");
    Check(!Save_WriteTime(&dat, NULL), "  NULL record refused");
    remove("test_save6.tmp");
}

static void TestResearchChunk(void)
{
    static DatFile dat;
    static SaveResearchState written;
    static SaveResearchState read;

    printf("Save_WriteResearch / Save_ReadResearch\n");
    memset(&written, 0, sizeof(written));
    for (i32 i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        written.aTopics[i].wUnk0 = (u16)(0x100 + i);
        written.aTopics[i].wUnk2 = (u16)(0x200 + i);
        written.aTopics[i].wNameId = (u16)(0x300 + i);
        written.aTopics[i].wProgress = (u16)(0x400 + i);
        written.aTopics[i].bAvailable = (u8)i;
        written.aTopics[i].bComplete = (u8)(i ^ 0x5A);
        written.aTopics[i].bUnk30 = (u8)(i + 1);
        written.aTopics[i].bUnk31 = (u8)(i + 2);
        written.aTopics[i].bUnk32 = (u8)(i + 3);
        written.aTopics[i].bUnk33 = (u8)(i + 4);
        written.aTopics[i].abUnk10[0] = (u8)(i + 5);
        written.aTopics[i].abUnk20[4] = (u8)(i + 6);
        written.aTopics[i].abUnk25[2] = (u8)(i + 7);
        written.aTopics[i].abUnlock[3] = (u8)(i + 8);
    }

    Check(OpenWrite("test_save7.tmp", &dat, 2), "  container opened");
    Check(Save_WriteResearch(&dat, &written), "  research written");
    DatFile_Close(&dat);

    /* 4896 header bytes (2304 + 2304 + 144 + 144) + 144 * 34 record bytes = 9792. */
    CheckEqI32(FileSizeOf(L"test_save7.tmp"), 46 + 9792,
               "  file is header + the research chunk");

    memset(&read, 0, sizeof(read));
    Check(OpenRead("test_save7.tmp", &dat), "  reopened");
    Check(Save_ReadResearch(&dat, &read), "  research read");
    CheckEqI32(read.aTopics[0].wUnk0, 0x100, "  topic 0 wUnk0 round-trips");
    CheckEqI32(read.aTopics[0].wUnk2, 0x200, "  topic 0 wUnk2 round-trips");
    CheckEqI32(read.aTopics[0].wNameId, 0x300, "  topic 0 wNameId round-trips");
    CheckEqI32(read.aTopics[0].wProgress, 0x400, "  topic 0 wProgress round-trips");
    CheckEqI32(read.aTopics[0].bComplete, 0x5A, "  topic 0 bComplete round-trips");
    CheckEqI32(read.aTopics[143].wUnk0, 0x100 + 143, "  last topic wUnk0 round-trips");
    CheckEqI32(read.aTopics[143].bUnk33, (143 + 4) & 0xFF, "  last topic bUnk33 round-trips");
    CheckEqI32(read.aTopics[100].abUnlock[3], (100 + 8) & 0xFF, "  an unlock byte round-trips");
    DatFile_Close(&dat);

    Check(!Save_WriteResearch(NULL, &written), "  NULL dat refused");
    Check(!Save_WriteResearch(&dat, NULL), "  NULL state refused");
    remove("test_save7.tmp");
}

static void TestSmapChunk(void)
{
    static DatFile dat;
    static SaveSmapState written;
    static SaveSmapState read;

    printf("Save_WriteSmap / Save_ReadSmap\n");
    memset(&written, 0, sizeof(written));
    written.nCamParam0 = 0x11111111;
    written.nCamScrollX = 0x22222222;
    written.nCamScrollY = 0x33333333;
    written.nParam3 = 0x44444444;
    written.nParam4 = 0x55555555;
    written.aSites[0] = (SmapSite){ 0 };
    written.aSites[0].bFlag0 = 0xAA;
    written.aSites[0].bFlag1 = 0xBB;
    written.aSites[0].bUnk518 = 0xCC;
    written.aSites[0].uRecoveredSoldiers = 0x1234;
    written.aSites[0].bHasLoot = 0x11;
    written.aSites[0].bRecoveredCraftCount = 0x22;
    written.aSites[0].bRecoveredSoldierAssign = 0x33;
    written.aSites[0].auItemQty[0] = 7;
    written.aSites[0].auItemQty[255] = 9;
    written.aSites[0].anCraftIds[0] = 5;
    written.aSites[0].anCraftIds[9] = 6;
    written.aSites[0].anSoldierIds[0] = 3;
    written.aSites[0].anSoldierIds[99] = 4;
    written.aSites[199].bFlag0 = 0x77;
    written.abTail5[0] = 0xDE;
    written.abTail8a[7] = 0xAD;
    written.abTail8b[7] = 0xBE;

    Check(OpenWrite("test_save8.tmp", &dat, 2), "  container opened");
    Check(Save_WriteSmap(&dat, &written), "  smap written");
    DatFile_Close(&dat);

    /* 20 header + 200 * 740 + 21 trailing = 148041. */
    CheckEqI32(FileSizeOf(L"test_save8.tmp"), 46 + 148041,
               "  file is header + the smap chunk");

    memset(&read, 0, sizeof(read));
    Check(OpenRead("test_save8.tmp", &dat), "  reopened");
    Check(Save_ReadSmap(&dat, &read, NULL, NULL, NULL, false), "  smap read");
    CheckEqI32((i32)read.nCamParam0, 0x11111111, "  header dword 0 round-trips");
    CheckEqI32((i32)read.nCamScrollY, 0x33333333, "  header dword 2 round-trips");
    CheckEqI32(read.aSites[0].bUnk518, 0xCC, "  site 0 bUnk518 round-trips");
    CheckEqI32(read.aSites[0].uRecoveredSoldiers, 0x1234, "  site 0 recovered soldiers");
    CheckEqI32(read.aSites[0].auItemQty[255], 9, "  item qty tail round-trips");
    CheckEqI32(read.aSites[0].anSoldierIds[99], 4, "  soldier id tail round-trips");
    CheckEqI32(read.aSites[199].bFlag0, 0x77, "  last site round-trips");
    CheckEqI32(read.abTail8b[7], 0xBE, "  trailing bytes round-trip");
    DatFile_Close(&dat);

    Check(!Save_WriteSmap(NULL, &written), "  NULL dat refused");
    Check(!Save_WriteSmap(&dat, NULL), "  NULL state refused");

    /* The reader restores the zoom level it just read, which is not a mirror of
     * the writer. Mode 1 publishes 600 x 300 and takes the map size from that
     * level's grid PadImage. */
    {
        SmapView view;
        SmapWorldSize world;
        PadImage grid;
        SmapZoomSet aSets[3];

        memset(&grid, 0, sizeof(grid));
        grid.uWidth = 123;
        grid.uHeight = 45;
        memset(aSets, 0, sizeof(aSets));
        aSets[1].pGrid = &grid;
        aSets[1].uWorldW = 600;
        aSets[1].uWorldH = 300;

        memset(&view, 0, sizeof(view));
        view.nMapW = 1;
        view.nMapH = 2;

        /* The first file stored nCamParam0 = 0x11111111, a mode outside 0..2. */
        Check(OpenRead("test_save8.tmp", &dat), "  reopened for the mode switch");
        Check(Save_ReadSmap(&dat, &read, &view, aSets, &world, false), "  read with a view");
        DatFile_Close(&dat);
        CheckEqI32(view.nMapW, 1, "a mode outside 0..2 restores nothing");

        written.nCamParam0 = 1;
        written.nCamScrollX = 500;
        written.nCamScrollY = 250;
        Check(OpenWrite("test_smapzoom.tmp", &dat, 2), "  zoom container");
        Check(Save_WriteSmap(&dat, &written), "  mode 1 written");
        DatFile_Close(&dat);
        Check(OpenRead("test_smapzoom.tmp", &dat), "  reopened");
        Check(Save_ReadSmap(&dat, &read, &view, aSets, &world, false), "  read with a view");
        DatFile_Close(&dat);
        CheckEqI32(view.nMode, 1, "the mode comes out of the chunk");
        CheckEqI32(view.nScrollX, 500, "scroll is restored as stored, with no rescale");
        CheckEqI32(view.nMapW, 123, "nMapW comes from the level's grid PadImage");
        CheckEqI32(view.nMapH, 45, "and so does nMapH");
        CheckEqI32((i32)world.uW, 600, "the world pair becomes 600 x 300");
        CheckEqI32((i32)world.uH, 300, "for zoom level 1");

        /* The unk_15821C gate: with the restore skipped the map size is left alone. */
        view.nMapW = 7;
        view.nMapH = 8;
        Check(OpenRead("test_smapzoom.tmp", &dat), "  reopened again");
        Check(Save_ReadSmap(&dat, &read, &view, aSets, &world, true), "  read with the gate set");
        DatFile_Close(&dat);
        CheckEqI32(view.nMapW, 7, "the gate leaves the map size alone");
        CheckEqI32(view.nMode, 1, "but the mode is still read");

        remove("test_smapzoom.tmp");
    }
    remove("test_save8.tmp");
}

static void TestAssignRosterIndices(void)
{
    Soldier   aSoldiers[3];
    Soldier  *apSoldiers[3];
    TactItem  aItems[4];
    TactItem *apItems[4];

    printf("Save_AssignRosterIndices\n");
    memset(aSoldiers, 0, sizeof(aSoldiers));
    memset(aItems, 0, sizeof(aItems));
    for (i32 i = 0; i < 3; ++i) {
        aSoldiers[i].nSaveId = 99;
        apSoldiers[i] = &aSoldiers[i];
    }
    for (i32 i = 0; i < 4; ++i) {
        aItems[i].nRefId = 99;
        apItems[i] = &aItems[i];
    }

    Check(Save_AssignRosterIndices(true, apSoldiers, 3, apItems, 4), "  assigned");
    CheckEqI32(aSoldiers[0].nSaveId, 0, "  soldier 0 id 0");
    CheckEqI32(aSoldiers[1].nSaveId, 1, "  soldier 1 id 1");
    CheckEqI32(aSoldiers[2].nSaveId, 2, "  soldier 2 id 2");
    /* The item counter restarts at 0 rather than continuing from the soldier count. */
    CheckEqI32(aItems[0].nRefId, 0, "  item 0 ref id 0 (restarts, does NOT continue)");
    CheckEqI32(aItems[3].nRefId, 3, "  item 3 ref id 3");

    /* Without the tactical flag the function does nothing at all. */
    for (i32 i = 0; i < 3; ++i) {
        aSoldiers[i].nSaveId = 99;
    }
    Check(!Save_AssignRosterIndices(false, apSoldiers, 3, apItems, 4),
          "  non-tactical returns false");
    CheckEqI32(aSoldiers[0].nSaveId, 99, "  and changes nothing");

    /* NULL lists are tolerated: each half is independent. */
    Check(Save_AssignRosterIndices(true, apSoldiers, 3, NULL, 0), "  items NULL is fine");
    CheckEqI32(aSoldiers[0].nSaveId, 0, "  soldiers still assigned");
    Check(Save_AssignRosterIndices(true, NULL, 0, apItems, 4), "  soldiers NULL is fine");
}

static void TestProductChunk(void)
{
    static DatFile dat;
    static u8 aBlock[PUFO_PRODUCT_BLOCK_BYTES];
    static u8 aRead[PUFO_PRODUCT_BLOCK_BYTES];
    static u8 aRaw[12672];

    printf("Save_WriteProduct / Save_ReadProduct\n");

    memset(aBlock, 0, sizeof(aBlock));
    aBlock[0] = 0xA1;        /* array A, entry 0 */
    aBlock[6399] = 0xA2;     /* array A, last byte */
    aBlock[6400] = 0xB1;     /* array B, entry 0 */
    aBlock[12799] = 0xB2;    /* array B, last byte */
    aBlock[12800] = 0xC1;    /* array C, entry 0 */
    aBlock[15999] = 0xC2;    /* array C, last byte */
    aBlock[16000] = 0xD1;    /* the tail region, which the writer must NOT touch */

    Check(OpenWrite("test_prod.tmp", &dat, 2), "  container opened");
    Check(Save_WriteProduct(&dat, aBlock), "  product chunk written");
    DatFile_Close(&dat);

    CheckEqI32(FileSizeOf(L"test_prod.tmp"), 46 + 16000,
               "  the new chunk payload is 6400 + 6400 + 3200");

    memset(aRead, 0xEE, sizeof(aRead));
    Check(OpenRead("test_prod.tmp", &dat), "  reopened for read");
    Check(Save_ReadProduct(&dat, 3, aRead), "  version 3 read");
    DatFile_Close(&dat);

    CheckEqI32(aRead[0], 0xA1, "array A start round-trips");
    CheckEqI32(aRead[6399], 0xA2, "array A end round-trips");
    CheckEqI32(aRead[6400], 0xB1, "array B sits at block +6400");
    CheckEqI32(aRead[12799], 0xB2, "array B end round-trips");
    CheckEqI32(aRead[12800], 0xC1, "array C sits at block +12800");
    CheckEqI32(aRead[15999], 0xC2, "array C end round-trips");
    CheckEqI32(aRead[16000], 0xEE, "the tail is never written");

    /* An old save: same offsets, smaller sizes. Everything past the shrunk size must keep
     * what the reader already had, which here is the 0xEE sentinel. */
    memset(aRaw, 0, sizeof(aRaw));
    aRaw[0] = 0x11;
    aRaw[4607] = 0x12;        /* A's 4608 bytes */
    aRaw[4608] = 0x21;        /* B's 4608 bytes */
    aRaw[9215] = 0x22;
    aRaw[9216] = 0x31;        /* C's 2304 bytes */
    aRaw[11519] = 0x32;
    aRaw[11520] = 0x41;       /* the tail's 1152 bytes */
    aRaw[12671] = 0x42;

    Check(OpenWrite("test_prod2.tmp", &dat, 2), "  old container opened");
    Check(DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_PRODUCT), "  old chunk begun");
    Check(DatFile_Write(&dat, aRaw, 4608), "  old A written");
    Check(DatFile_Write(&dat, aRaw + 4608, 4608), "  old B written");
    Check(DatFile_Write(&dat, aRaw + 9216, 2304), "  old C written");
    Check(DatFile_Write(&dat, aRaw + 11520, 1152), "  old tail written");
    DatFile_Close(&dat);

    memset(aRead, 0xEE, sizeof(aRead));
    Check(OpenRead("test_prod2.tmp", &dat), "  old reopened");
    Check(Save_ReadProduct(&dat, 2, aRead), "  version 2 read");
    DatFile_Close(&dat);

    CheckEqI32(aRead[0], 0x11, "old A start lands at block +0");
    CheckEqI32(aRead[4607], 0x12, "old A ends at entry 1151");
    CheckEqI32(aRead[4608], 0xEE, "old A leaves a gap the reader never fills");
    CheckEqI32(aRead[6400], 0x21, "old B still lands at block +6400");
    CheckEqI32(aRead[6400 + 4607], 0x22, "old B ends 4608 bytes later");
    CheckEqI32(aRead[12800], 0x31, "old C still lands at block +12800");
    CheckEqI32(aRead[12800 + 2303], 0x32, "old C ends 2304 bytes later");
    CheckEqI32(aRead[16000], 0x41, "the old tail lands at block +16000");
    CheckEqI32(aRead[16000 + 1151], 0x42, "and runs 1152 bytes");

    Check(!Save_WriteProduct(NULL, aBlock), "  NULL dat refused");
    Check(!Save_WriteProduct(&dat, NULL), "  NULL block refused");
    Check(!Save_ReadProduct(&dat, 3, NULL), "  a NULL read block is refused");
}

static void TestBasesChunk(void)
{
    DatFile dat;
    SaveBasesState st, got;
    ObjectRecFull aObj[2];
    ObjectRecFull *apObj[2];
    BaseRec aBase[3];
    const BaseRec *apBase[3];
    BaseRec *apRead[8];
    i32 b;

    printf("Save_WriteBases / Save_ReadBases\n");

    memset(&st, 0, sizeof(st));
    memset(&got, 0, sizeof(got));
    for (i32 i = 0; i < 288; ++i)  { st.abBlock288[i]   = (u8)(i * 3 + 1); }
    for (i32 i = 0; i < 1152; ++i) { st.abBlock1152[i]  = (u8)(i * 5 + 7); }
    st.bBaseCount = 3;
    st.bUnk159D81 = 0x11;
    st.bUnk159D82 = 0x22;
    for (i32 i = 0; i < 4; ++i) { st.abUnk159D90[i] = (u8)(0x30 + i); }
    for (i32 i = 0; i < 8; ++i) { st.abUnk159DBC[i] = (u8)(0x40 + i); }
    for (i32 i = 0; i < 4; ++i) { st.abUnk1598D8[i] = (u8)(0x50 + i); }
    st.bUnk158848 = 0x66;
    for (i32 i = 0; i < 17; ++i) { st.abFlag20[i]     = (u8)(i + 1); }
    for (i32 i = 0; i < 9; ++i)  { st.aRegionBuildCount[i]  = (u8)(0x70 + i); }
    for (i32 f = 0; f < 8; ++f) {
        snprintf(st.aFac[f].szName, 20, "Fac%02d", (int)f);
        snprintf(st.aFac[f].szUnk6, 20, "U6_%02d", (int)f);
        st.aFac[f].bType = (u8)f;
        st.aFac[f].nBuildState = 1000 + f;
        st.aFac[f].nFacType = 100 + f;
        st.aFac[f].nUnk48 = 200 + f;
        st.aFac[f].uUnk2 = (u16)(300 + f);
        st.aFac[f].uUnk4 = (u16)(400 + f);
        st.aFac[f].nDemolishState = 500 + f;
    }
    for (i32 o = 0; o < 2; ++o) {
        memset(&aObj[o], 0, sizeof(aObj[o]));
        for (i32 k = 0; k < 8; ++k) { aObj[o].auPerBaseVal[k] = (u16)(1000 + k); }
        aObj[o].aItemQty[0] = 4242; /* 0x3a, the field an unbounded ctor index would hit */
        apObj[o] = &aObj[o];
    }
    memset(apBase, 0, sizeof(apBase));
    for (b = 0; b < 3; ++b) {
        memset(&aBase[b], 0, sizeof(aBase[b]));
        aBase[b].bSlot = (u8)(b + 1);
        aBase[b].bUnk4 = (u8)(0x10 + b);
        aBase[b].nStoresTotal = 7 + b;
        aBase[b].nUnk8 = 80 + b;
        aBase[b].nUnk12 = 90 + b;
        aBase[b].bUnk16 = (u8)(0xA0 + b);
        aBase[b].bRegionId = (u8)(0xB0 + b);
        apBase[b] = &aBase[b];
    }

    Check(OpenWrite("test_bases.tmp", &dat, 2), "  container opened");
    Check(Save_WriteBases(&dat, &st, apBase, apObj, 2), "  bases chunk written");
    DatFile_Close(&dat);

    /* 288+1152+1+1+1+4+8+4+1+17+9+8*61 = 1974, then 3 * (16 + 2*2). */
    CheckEqI32(FileSizeOf(L"test_bases.tmp"), 46 + 1974 + 60,
               "the chunk is 1974 header bytes plus 20 per base");

    memset(&got, 0xEE, sizeof(got));
    memset(apRead, 0, sizeof(apRead));
    Check(OpenRead("test_bases.tmp", &dat), "  reopened for read");
    Check(Save_ReadBases(&dat, 3, &got, apRead, 0, 0x5A, apObj, 2), "  version 3 read");
    DatFile_Close(&dat);

    Check(memcmp(got.abBlock288, st.abBlock288, 288) == 0, "the 288-byte block round-trips");
    Check(memcmp(got.abBlock1152, st.abBlock1152, 1152) == 0, "the 1152-byte block round-trips");
    CheckEqI32(got.bBaseCount, 3, "the base count round-trips");
    CheckEqI32(got.bUnk159D81, 0x11, "0x159D81 round-trips");
    CheckEqI32(got.bUnk159D82, 0x22, "0x159D82 round-trips");
    Check(memcmp(got.abUnk159D90, st.abUnk159D90, 4) == 0, "the 4-byte field round-trips");
    Check(memcmp(got.abUnk159DBC, st.abUnk159DBC, 8) == 0, "version 3 reads all 8 bytes");
    CheckEqI32(got.bUnk158848, 0x66, "version 3 reads the 0x158848 byte");
    Check(memcmp(got.abFlag20, st.abFlag20, 17) == 0, "all 17 place-flag bytes round-trip");
    Check(memcmp(got.aRegionBuildCount, st.aRegionBuildCount, 9) == 0, "the 9-byte block round-trips");

    CheckEqStr(got.aFac[0].szName, "Fac00", "facility 0 name round-trips");
    CheckEqI32(got.aFac[0].nBuildState, 1000, "facility 0 build state round-trips");
    CheckEqStr(got.aFac[7].szUnk6, "U6_07", "facility 7 unknown-6 string round-trips");
    CheckEqI32(got.aFac[7].nFacType, 107, "facility 7 type round-trips");
    CheckEqI32(got.aFac[7].uUnk2, 307, "facility 7 uUnk2 round-trips");
    CheckEqI32(got.aFac[7].nDemolishState, 507, "facility 7 demolish state round-trips");

    Check(apRead[0] != NULL && apRead[2] != NULL, "the reader allocated one record per base");
    if (apRead[0] != NULL) {
        CheckEqI32(apRead[0]->bSlot, 1, "bSlot round-trips");
        CheckEqI32(apRead[0]->bUnk4, 0x10, "bUnk4 round-trips");
        CheckEqI32(apRead[0]->nStoresTotal, 7, "the store total round-trips");
        CheckEqI32(apRead[0]->nUnk12, 90, "nUnk12 round-trips");
        CheckEqI32(apRead[0]->bUnk16, 0xA0, "bUnk16 round-trips");
        CheckEqI32(apRead[0]->bRegionId, 0xB0, "bKind round-trips");
    }

    /* The writer's column is the loop index, so bases 0..2 wrote columns 0..2. */
    CheckEqI32(aObj[0].auPerBaseVal[0], 1000, "base 0 wrote object column 0");
    CheckEqI32(aObj[1].auPerBaseVal[2], 1002, "base 2 wrote object column 2");
    /* BaseRec_Ctor is handed the count byte, so it zeroes column 3 of every object. */
    CheckEqI32(aObj[0].auPerBaseVal[3], 0, "the ctor zeroed column 3 of object 0");
    CheckEqI32(aObj[1].auPerBaseVal[3], 0, "the ctor zeroed column 3 of object 1");
    CheckEqI32(aObj[0].auPerBaseVal[4], 1004, "column 4 was left alone");
    CheckEqI32(aObj[0].aItemQty[0], 4242, "the port clamps the ctor index");

    for (b = 0; b < 8; ++b) {
        free(apRead[b]);
        apRead[b] = NULL;
    }

    /* Same file, old reader: the two version gates consume 4 + 1 fewer bytes, so
     * everything after them is shifted by five. That is what makes the gates real. */
    memset(&got, 0xEE, sizeof(got));
    Check(OpenRead("test_bases.tmp", &dat), "  reopened for the old-format read");
    (void)Save_ReadBases(&dat, 2, &got, apRead, 0, 0x5A, apObj, 2);
    DatFile_Close(&dat);
    for (b = 0; b < 8; ++b) {
        free(apRead[b]);
        apRead[b] = NULL;
    }
    CheckEqI32(got.abUnk159DBC[7], 0xEE,
               "version 2 reads only 4 of the 8 bytes");
    CheckEqI32(got.bUnk158848, 0xEE, "version 2 never reads the 0x158848 byte");
    CheckEqI32(got.abFlag20[0], 0x50, "the flags start on the 0x1598D8 block");
    CheckEqI32(got.abFlag20[1], 0x51, "five bytes earlier than they should");

    /* BaseRec_Ctor on its own: the index is the byte at 0x159D80, and the original
     * does not bound it. */
    aObj[0].auPerBaseVal[7] = 9999;
    BaseRec_Ctor(&aBase[0], 7, 0x33, apObj, 2);
    CheckEqI32(aObj[0].auPerBaseVal[7], 0, "index 7 zeroes the last column");
    CheckEqI32(aBase[0].bSlot, 7, "the ctor stores the index as bSlot");
    CheckEqI32(aBase[0].bUnk4, 0x33, "and the global byte as bUnk4");
    CheckEqI32(aBase[0].nStoresTotal, 0, "and clears the store total");
    aObj[0].auPerBaseVal[7] = 8888;
    aObj[0].aItemQty[0] = 4242;
    BaseRec_Ctor(&aBase[0], 8, 0x33, apObj, 2);
    CheckEqI32(aObj[0].aItemQty[0], 4242,
               "index 8 would hit aItemQty[0]; the port leaves it");

    Check(!Save_WriteBases(NULL, &st, apBase, apObj, 2), "NULL dat refused");
    Check(!Save_WriteBases(&dat, NULL, apBase, apObj, 2), "NULL state refused");
    Check(!Save_ReadBases(NULL, 3, &got, apRead, 0, 0, apObj, 2), "NULL read dat refused");
}

static void TestShipsChunk(void)
{
    DatFile dat;
    Ship aShip[2];
    const Ship *apShip[2];
    Ship *apRead[8];
    ObjectRecFull aObj[2];
    ObjectRecFull *apObj[2];
    ShipCtorEnv env;

    printf("Save_WriteShips / Save_ReadShips / Ship_Ctor\n");

    for (i32 o = 0; o < 2; ++o) {
        memset(&aObj[o], 0, sizeof(aObj[o]));
        for (i32 k = 0; k < 52; ++k) { aObj[o].aItemQty[k] = (u16)(500 + k); }
        for (i32 k = 0; k < 8; ++k) { aObj[o].auPerBaseVal[k] = (u16)(1000 + k); }
        apObj[o] = &aObj[o];
    }

    memset(aShip, 0, sizeof(aShip));
    for (i32 s = 0; s < 2; ++s) {
        aShip[s].nFuel = (i16)(100 + s);
        aShip[s].bHomeBase = (u8)(s + 1);
        aShip[s].nSlotIdx = s;
        aShip[s].bCraftType = (u8)(s == 0 ? 0 : 3);
        aShip[s].bWeaponL = (i8)(10 + s);
        aShip[s].uRangeRemain = (u16)(200 + s);
        aShip[s].bHangarX = (u8)(3 + s);
        aShip[s].bHangarY = (u8)(4 + s);
        aShip[s].anCargoQty[0] = 11 + s;
        aShip[s].anCargoQty[3] = 14 + s;
        aShip[s].bNeedsSupply = (u8)(s == 1);
        aShip[s].bCanDogfight = (u8)(s == 0);
        aShip[s].uUnk4 = (u16)(30 + s);
        aShip[s].uFuelMax = (u16)(40 + s);
        aShip[s].uRangeMax = (u16)(1234 + s);
        aShip[s].bCrewSlots = (u8)(2 + s);
        aShip[s].bType = (u8)(7 + s);
        aShip[s].bAtBase = (u8)(s == 0);
        aShip[s].bWeaponsHot = 1;
        aShip[s].bAmmo25 = (u8)(25 + s);
        aShip[s].bAmmo26 = (u8)(26 + s);
        aShip[s].bAmmo27 = (u8)(27 + s);
        aShip[s].bAmmo29 = (u8)(29 + s);
        aShip[s].bAmmo30 = (u8)(30 + s);
        aShip[s].bRefueling = (u8)(s == 1);
        aShip[s].bWeaponR = (i8)(40 + s);
        aShip[s].bId = (u8)(90 + s);
        aShip[s].uFuelCap = (u16)(500 + s);
        aShip[s].uUnk14 = (u16)(14 + s);
        aShip[s].uRangeRate = (u16)(20 + s);
        aShip[s].bIntercepting = (u8)(s == 0);
        aShip[s].nStrategIdx = 300 + s;
        aShip[s].bContactMsg = (u8)(s == 1);
        apShip[s] = &aShip[s];
    }

    memset(&env, 0, sizeof(env));
    env.nShipCount = 2;
    env.apObjects = apObj;
    env.nObjectCount = 2;

    Check(OpenWrite("test_ships.tmp", &dat, 2), "  container opened");
    Check(Save_WriteShips(&dat, apShip, 2, apObj, 2), "  ships chunk written");
    DatFile_Close(&dat);

    /* 1 count byte + 2 * (61 field bytes + 2*2 object bytes). */
    CheckEqI32(FileSizeOf(L"test_ships.tmp"), 46 + 1 + 2 * 65,
               "the chunk is 1 byte plus 65 per ship");

    memset(apRead, 0, sizeof(apRead));
    Check(OpenRead("test_ships.tmp", &dat), "  reopened for read");
    {
        i32 nCount = 0; /* the caller held no ships before the load */
        Check(Save_ReadShips(&dat, apRead, &nCount, &env), "  read");
        CheckEqI32(nCount, 2, "the count comes back out of the reader");
    }
    DatFile_Close(&dat);

    Check(apRead[0] != NULL && apRead[1] != NULL, "one record allocated per ship");
    if (apRead[0] != NULL && apRead[1] != NULL) {
        CheckEqI32(apRead[0]->nFuel, 100, "nFuel round-trips");
        CheckEqI32(apRead[0]->nSlotIdx, 0, "nSlotIdx round-trips");
        CheckEqI32(apRead[1]->anCargoQty[3], 15, "the cargo counts round-trip");
        CheckEqI32(apRead[1]->bType, 8, "bType IS saved in this chunk");
        CheckEqI32(apRead[1]->uRangeMax, 1235, "a non-zero craft type keeps its range max");
        CheckEqI32(apRead[1]->nStrategIdx, 301, "nStrategIdx round-trips");
        CheckEqI32(apRead[1]->bWeaponR, 41, "bWeaponR round-trips");
        /* 0x40C24: a craft type of 0 gets its uRangeMax forced after the load. */
        CheckEqI32(apRead[0]->uRangeMax, 10000,
                   "craft type 0 has uRangeMax forced to 10000 after the load");
    }

    /* The writer's column is the loop index, so ships 0 and 1 wrote aItemQty[0..1].
     * Ship_Ctor is handed the count (2), so it zeroes aItemQty[2] of every object. */
    CheckEqI32(aObj[0].aItemQty[0], 500, "ship 0 wrote object column 0");
    CheckEqI32(aObj[1].aItemQty[1], 501, "ship 1 wrote object column 1");
    CheckEqI32(aObj[0].aItemQty[2], 0, "the ctor zeroed aItemQty[2] of object 0");
    CheckEqI32(aObj[1].aItemQty[2], 0, "the ctor zeroed aItemQty[2] of object 1");
    CheckEqI32(aObj[0].aItemQty[3], 503, "column 3 was left alone");
    CheckEqI32(aObj[0].auPerBaseVal[0], 1000, "the ships chunk never touches auPerBaseVal");

    for (i32 s = 0; s < 8; ++s) {
        free(apRead[s]);
        apRead[s] = NULL;
    }

    /* Ship_Ctor on its own. */
    memset(&aShip[0], 0xEE, sizeof(aShip[0]));
    (void)Ship_Ctor(&aShip[0], 1, 5, 6, 1, &env);
    CheckEqI32(aShip[0].bCraftType, 0xEE, "bSkip leaves the record untouched");

    memset(&aShip[0], 0, sizeof(aShip[0]));
    env.abTypeCounters[0] = 3;
    (void)Ship_Ctor(&aShip[0], 0, 5, 6, 0, &env);
    CheckEqI32(aShip[0].bType, 4, "bType is the incremented per-type counter");
    CheckEqI32(env.abTypeCounters[0], 4, "and the counter is written back");
    CheckEqI32(aShip[0].bCrewSlots, 0, "interceptor crew is 0");
    CheckEqI32(aShip[0].uRangeMax, 10000, "interceptor range max");
    CheckEqI32(aShip[0].uFuelMax, 150, "interceptor fuel max");
    CheckEqI32(aShip[0].uFuelCap, 760, "interceptor fuel cap");
    CheckEqI32(aShip[0].bAtBase, 1, "a new ship starts at base");
    CheckEqI32(aShip[0].bRefueling, 0, "and the u16 store clears the byte beside it");
    CheckEqI32(aShip[0].nSlotIdx, 2, "nSlotIdx takes the current ship count");

    memset(&aShip[1], 0, sizeof(aShip[1]));
    (void)Ship_Ctor(&aShip[1], 2, 5, 6, 0, &env);
    CheckEqI32(aShip[1].uRangeMax, 60000, "type 2 range max is -5536 as a u16");
    CheckEqI32(aShip[1].uFuelCap, 5400, "type 2 fuel cap");
    CheckEqI32(aShip[1].bCrewSlots, 2, "type 2 crew is 2");
    (void)Ship_Ctor(&aShip[1], 1, 5, 6, 0, &env);
    CheckEqI32(aShip[1].bCrewSlots, 1, "type 1 crew is 1");
    CheckEqI32(aShip[1].uRangeMax, 30000, "type 1 range max");

    memset(&aShip[1], 0xEE, sizeof(aShip[1]));
    aObj[0].aItemQty[2] = 777;
    (void)Ship_Ctor(&aShip[1], 5, 5, 6, 0, &env);
    CheckEqI32(aShip[1].uFuelMax, 0xEEEE, "a type above 4 sets no fuel field");
    CheckEqI32(aShip[1].bType, 1, "and bType stays 1");
    CheckEqI32(aObj[0].aItemQty[2], 0, "but the cargo column is still cleared");

    Check(!Save_WriteShips(NULL, apShip, 2, apObj, 2), "NULL dat refused");
    Check(!Save_ReadShips(&dat, apRead, NULL, &env), "NULL count refused");
}

static void TestAllocFlobjs(void)
{
    DatFile dat;
    TactItem *apItems[8];
    i32 nCount;

    printf("Save_AllocFlobjs\n");

    /* Wide form: the count is a 4-byte field. */
    Check(OpenWrite("test_flobjs.tmp", &dat, 2), "  container opened");
    Check(DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_FLOBJS), "  chunk begun");
    {
        i32 nWrite = 3;
        Check(DatFile_Write(&dat, &nWrite, 4), "  4-byte count written");
    }
    DatFile_Close(&dat);

    memset(apItems, 0, sizeof(apItems));
    nCount = 0;
    Check(OpenRead("test_flobjs.tmp", &dat), "  reopened");
    Check(Save_AllocFlobjs(&dat, 1, &nCount, apItems), "  wide read");
    DatFile_Close(&dat);
    CheckEqI32(nCount, 3, "the wide read takes 4 bytes");
    Check(apItems[0] != NULL && apItems[2] != NULL, "one record per item");
    if (apItems[0] != NULL) {
        CheckEqI32(apItems[0]->pNext == NULL, 1, "the ctor clears pNext");
        CheckEqI32(apItems[0]->bUnk7, 0, "and bUnk7");
        CheckEqI32(apItems[0]->bUnk8, 0, "and bUnk8");
        CheckEqI32(apItems[0]->pLinkedSoldier == NULL, 1, "and the linked soldier");
    }
    for (i32 i = 0; i < 8; ++i) {
        free(apItems[i]);
        apItems[i] = NULL;
    }

    /* Narrow form: one byte into a 4-byte global, so the upper three bytes survive. */
    Check(OpenWrite("test_flobjs2.tmp", &dat, 2), "  narrow container opened");
    Check(DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_FLOBJS), "  narrow chunk begun");
    {
        u8 bWrite = 2;
        Check(DatFile_Write(&dat, &bWrite, 1), "  1-byte count written");
    }
    DatFile_Close(&dat);

    memset(apItems, 0, sizeof(apItems));
    nCount = 0;
    Check(OpenRead("test_flobjs2.tmp", &dat), "  narrow reopened");
    Check(Save_AllocFlobjs(&dat, 0, &nCount, apItems), "  narrow read");
    DatFile_Close(&dat);
    CheckEqI32(nCount, 2, "the narrow read takes 1 byte");
    Check(apItems[0] != NULL && apItems[1] != NULL, "the loop used the low byte only");
    Check(apItems[2] == NULL, "and stopped there");
    for (i32 i = 0; i < 8; ++i) {
        free(apItems[i]);
        apItems[i] = NULL;
    }

    /* The upper three bytes of the count global survive a narrow read. The array is
     * NULL here on purpose: the loop would otherwise run 0x123402 times, which is what
     * the original would do too. */
    nCount = 0x123400; /* what the global held before the load */
    Check(OpenRead("test_flobjs2.tmp", &dat), "  reopened again");
    Check(!Save_AllocFlobjs(&dat, 0, &nCount, NULL), "  NULL array stops the loop");
    DatFile_Close(&dat);
    CheckEqI32(nCount, 0x123402, "the narrow read leaves the upper three bytes alone");

    Check(!Save_AllocFlobjs(NULL, 1, &nCount, apItems), "NULL dat refused");
    Check(!Save_AllocFlobjs(&dat, 1, NULL, apItems), "NULL count refused");
}

static void TestFlobjsChunk(void)
{
    DatFile dat;
    ObjectRecFull aObj[2];
    const ObjectRecFull *apObjC[2];
    ObjectRecFull *apObj[2];
    Soldier aSol[2];
    Soldier *apSol[2];
    TactItem aItem[3];
    const TactItem *apItemC[3];
    TactItem *apItem[3];

    printf("Save_WriteFlobjs / Save_ReadFlobjs\n");

    memset(aObj, 0, sizeof(aObj));
    memset(aSol, 0, sizeof(aSol));
    memset(aItem, 0, sizeof(aItem));
    for (i32 i = 0; i < 2; ++i) {
        apObj[i] = &aObj[i];
        apObjC[i] = &aObj[i];
        apSol[i] = &aSol[i];
        aSol[i].nSaveId = i; /* nSaveId IS the roster index; the reader indexes apSoldiers with it */
    }
    for (i32 i = 0; i < 3; ++i) {
        apItem[i] = &aItem[i];
        apItemC[i] = &aItem[i];
        aItem[i].nRefId = i; /* what Save_AssignRosterIndices leaves behind */
        aItem[i].nIndex = (u16)(7 + i);
        aItem[i].nAmmo = (u8)(30 + i);
        aItem[i].bUnk7 = (u8)i;
        aItem[i].bUnk8 = (u8)(2 - i);
    }
    aItem[0].pObj = &aObj[0];
    aItem[1].pObj = &aObj[1];
    aItem[2].pObj = &aObj[0];
    aItem[0].pNext = &aItem[1];
    aItem[1].pNext = &aItem[2];
    aItem[2].pNext = NULL;
    aItem[0].pLinkedSoldier = &aSol[0];
    aItem[1].pLinkedSoldier = NULL;
    aItem[2].pLinkedSoldier = &aSol[1];

    Check(OpenWrite("test_flobjs3.tmp", &dat, 2), "  container opened");
    Check(Save_WriteFlobjs(&dat, apItemC, 3, apObjC, 2), "  chunk written");
    DatFile_Close(&dat);
    /* 4-byte count + 3 * (4 + 2 + 1 + 1 + 1 + 4 + 4). */
    CheckEqI32(FileSizeOf(L"test_flobjs3.tmp"), 46 + 4 + 3 * 17,
               "17 bytes per item");

    memset(aItem, 0xEE, sizeof(aItem));
    Check(OpenRead("test_flobjs3.tmp", &dat), "  reopened");
    {
        i32 nCount = 0;
        Check(Save_ReadFlobjs(&dat, 1, &nCount, apItem, apObj, 2, apSol), "  read");
        CheckEqI32(nCount, 3, "the count round-trips");
    }
    DatFile_Close(&dat);

    Check(aItem[0].pObj == &aObj[0], "the object index resolves back to the record");
    Check(aItem[1].pObj == &aObj[1], "the second item gets its own object");
    CheckEqI32(aItem[0].nIndex, 7, "nIndex round-trips");
    CheckEqI32(aItem[2].nAmmo, 32, "nAmmo round-trips");
    CheckEqI32(aItem[2].bUnk8, 0, "bUnk8 round-trips");
    Check(aItem[0].pNext == &aItem[1], "the link is stored as the pool index");
    Check(aItem[1].pNext == &aItem[2], "and resolves back through the same array");
    Check(aItem[2].pNext == NULL, "0x7FFF becomes a NULL next pointer");
    Check(aItem[0].pLinkedSoldier == &aSol[0], "the soldier link is nSaveId");
    Check(aItem[1].pLinkedSoldier == NULL, "and 0x7FFF becomes a NULL soldier");
    Check(aItem[2].pLinkedSoldier == &aSol[1], "the second soldier resolves by its own id");

    aItem[1].pObj = NULL; /* an object the table does not hold */
    Check(!Save_WriteFlobjs(&dat, apItemC, 3, apObjC, 2), "an unfound object is refused");
    Check(!Save_WriteFlobjs(NULL, apItemC, 3, apObjC, 2), "NULL dat refused");
    Check(!Save_ReadFlobjs(&dat, 1, NULL, apItem, apObj, 2, apSol), "NULL count refused");
}

static MapCell g_aMapCells[PUFO_MAP_LAYERS][10][10];

static MapCell *MapTestLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS || nX < 0 || nX >= 10 || nY < 0 || nY >= 10) {
        return NULL;
    }
    return &g_aMapCells[nLayer][nY][nX];
}

static void TestMapChunk(void)
{
    DatFile dat;
    MapGeom geom;
    MapCell *pCell;
    Soldier aSol[2];
    Soldier *apSol[2];
    TactItem aItem[2];
    TactItem *apItem[2];
    Mc1Rec aMc1[3];
    Mc1Rec *apMc1[3];
    PufoFont font;

    printf("Save_WriteMap / Save_ReadMap\n");

    memset(g_aMapCells, 0, sizeof(g_aMapCells));
    memset(aSol, 0, sizeof(aSol));
    memset(aItem, 0, sizeof(aItem));
    memset(aMc1, 0, sizeof(aMc1));
    memset(&font, 0, sizeof(font));

    geom.nDimX = 1; /* 10 x 10 cells per layer, 4 layers */
    geom.nDimY = 1;

    for (i32 i = 0; i < 2; ++i) {
        apSol[i] = &aSol[i];
        apItem[i] = &aItem[i];
        aSol[i].nSaveId = i;
        aItem[i].nRefId = i;
    }
    for (i32 i = 0; i < 3; ++i) {
        apMc1[i] = &aMc1[i];
        aMc1[i].nGlobalIdx = (u16)i;
    }
    font.apMc1Index = apMc1;
    font.nMc1IndexCount = 3;

    pCell = &g_aMapCells[2][3][4];
    pCell->abFaceDamage[0] = 0x11;
    pCell->abFaceDamage[1] = 0x22;
    pCell->abFaceDamage[2] = 0x33;
    pCell->abFaceDamage[3] = 0x44;
    pCell->bFlags29 = 0x35; /* bits 0, 2, 4 set; bit 5 is the shot-trace flag */
    pCell->nFlags32 = 0x1A9;
    pCell->bFlags36 = 1;
    pCell->nFlags40 = 0x72;
    pCell->bFlags44 = 0x03; /* bit 1 must not survive a load */
    pCell->bUnk56 = 0x7F;
    pCell->bFlags57[0] = 0x0F; /* bits 0 and 1 are outside the stored pair */
    pCell->bFlags57[1] = 0x2A;
    pCell->pUnit = &aSol[1];
    pCell->pItemList = &aItem[1];
    pCell->pFloorSpr = &aMc1[1];
    pCell->pWallSprA = &aMc1[2];
    pCell->pWallSprB = NULL;
    pCell->pCeilSpr = &aMc1[0];

    Check(OpenWrite("test_mapchunk.tmp", &dat, 2), "  container opened");
    Check(Save_WriteMap(&dat, &geom, MapTestLookup, NULL), "  chunk written");
    DatFile_Close(&dat);
    /* 2 dimension bytes + 400 cells * 43 bytes. */
    CheckEqI32(FileSizeOf(L"test_mapchunk.tmp"), 46 + 2 + 400 * 43,
               "43 bytes per cell");

    memset(g_aMapCells, 0, sizeof(g_aMapCells));
    /* Pre-fill AFTER the write: the file holds zeros for this cell while memory holds
     * ones, which is what shows which bits a load recomputes and which it leaves alone. */
    g_aMapCells[0][0][1].bFlags29 = 0xFF;
    g_aMapCells[0][0][1].bFlags44 = 0xFF;
    g_aMapCells[0][0][1].nFlags32 = 0x100;
    g_aMapCells[0][0][1].bFlags57[0] = 0x03;
    Check(OpenRead("test_mapchunk.tmp", &dat), "  reopened");
    Check(Save_ReadMap(&dat, &geom, MapTestLookup, NULL, apSol, 2, apItem, 2, &font), "  read");
    DatFile_Close(&dat);

    pCell = &g_aMapCells[2][3][4];
    CheckEqI32(pCell->abFaceDamage[0], 0x11, "the 0x10 block round-trips");
    CheckEqI32(pCell->abFaceDamage[3], 0x44, "as four bytes, not one");
    CheckEqI32(pCell->nX, 4, "nX comes from the column counter");
    CheckEqI32(pCell->nY, 3, "nY from the row counter");
    CheckEqI32(pCell->nZ, 2, "nZ from the layer counter");
    CheckEqI32(pCell->nPathCost, 0, "nPathCost is cleared, not read");
    CheckEqI32(pCell->bPathDir, 0, "and so are bPathDir and bPathFlag");
    CheckEqI32(pCell->bFlags29, 0x15, "bFlags29 bit 5 is cleared by the 0x8F mask");
    CheckEqI32(pCell->nFlags32, 0x29, "nFlags32 keeps only its low 7 bits");
    CheckEqI32(pCell->bFlags36, 1, "bFlags36 round-trips");
    CheckEqI32(pCell->nFlags40, 0x72, "nFlags40 round-trips");
    CheckEqI32(pCell->bFlags44, 0x01, "bFlags44 bit 1 is cleared on the way in");
    CheckEqI32(pCell->bUnk56, 0x7F, "bUnk56 round-trips");
    CheckEqI32(pCell->bFlags57[0], 0x0C, "only bits 2 and 3 of bFlags57[0] are stored");
    CheckEqI32(pCell->bFlags57[1], 0x2A, "bFlags57[1] round-trips as a byte");
    Check(pCell->pUnit == &aSol[1], "the occupant resolves through nSaveId");
    Check(pCell->pItemList == &aItem[1], "the item list head through nRefId");
    Check(pCell->pFloorSpr == &aMc1[1], "the floor sprite through nGlobalIdx");
    Check(pCell->pWallSprA == &aMc1[2], "and so the first wall sprite");
    Check(pCell->pWallSprB == NULL, "0x7FFF means no wall sprite B");
    Check(pCell->pCeilSpr == &aMc1[0], "and the ceiling sprite resolves too");

    pCell = &g_aMapCells[0][0][1];
    CheckEqI32(pCell->bFlags29, 0x8F, "a stored zero still clears bits 5 and 6");
    CheckEqI32(pCell->bFlags44, 0xFC, "and bFlags44 loses bits 0 and 1 whatever it held");
    CheckEqI32(pCell->nFlags32, 0x100, "bits 7 and up of nFlags32 are not stored at all");
    CheckEqI32(pCell->bFlags57[0], 0x03, "and bits 0 and 1 of bFlags57[0] are left alone");

    pCell = &g_aMapCells[3][9][9];
    Check(pCell->pUnit == NULL && pCell->pItemList == NULL, "an empty cell keeps both links null");
    Check(pCell->pFloorSpr == NULL && pCell->pCeilSpr == NULL,
          "and all four sprites null");

    /* An occupant id the roster does not hold. */
    g_aMapCells[0][0][0].pUnit = NULL;
    Check(OpenWrite("test_mapbad.tmp", &dat, 2), "  second container");
    {
        /* Force a bad id by writing a cell whose occupant is out of range: point the
         * grid at a soldier outside the array the reader is given. */
        Soldier aFar[3];
        memset(aFar, 0, sizeof(aFar));
        aFar[2].nSaveId = 77;
        g_aMapCells[0][0][0].pUnit = &aFar[2];
        Check(Save_WriteMap(&dat, &geom, MapTestLookup, NULL), "  bad id written");
    }
    DatFile_Close(&dat);
    Check(OpenRead("test_mapbad.tmp", &dat), "  reopened");
    Check(!Save_ReadMap(&dat, &geom, MapTestLookup, NULL, apSol, 2, apItem, 2, &font),
          "an out-of-range occupant id is refused");
    DatFile_Close(&dat);

    Check(!Save_WriteMap(NULL, &geom, MapTestLookup, NULL), "NULL dat refused");
    Check(!Save_WriteMap(&dat, &geom, NULL, NULL), "NULL lookup refused");

    /* The good file, but with no font: the sprite index at cell [2][3][4] cannot resolve. */
    Check(OpenRead("test_mapchunk.tmp", &dat), "  reopened for the font check");
    Check(!Save_ReadMap(&dat, &geom, MapTestLookup, NULL, apSol, 2, apItem, 2, NULL),
          "a sprite index with no font is refused");
    DatFile_Close(&dat);
}

static void TestTeamUnitsChunk(void)
{
    DatFile dat;
    Soldier aSol[6];
    Soldier *apSoldiers[6];
    Soldier *apXcom[PUFO_SAVE_ROSTER_MAX];
    Soldier *apEnemy[PUFO_SAVE_ROSTER_MAX];
    Soldier *apCiv[PUFO_SAVE_CIV_MAX];
    Soldier *apHidden[PUFO_SAVE_ROSTER_MAX];
    TeamUnitsState st;

    printf("Save_WriteTeamUnits / Save_ReadTeamUnits\n");

    memset(aSol, 0, sizeof(aSol));
    for (i32 i = 0; i < 6; ++i) {
        apSoldiers[i] = &aSol[i];
        aSol[i].nSaveId = i;
    }
    memset(apXcom, 0, sizeof(apXcom));
    memset(apEnemy, 0, sizeof(apEnemy));
    memset(apCiv, 0, sizeof(apCiv));
    memset(apHidden, 0, sizeof(apHidden));

    apXcom[0] = &aSol[0];
    apXcom[1] = &aSol[1];
    apXcom[2] = &aSol[2];
    apXcom[5] = &aSol[3]; /* past the count of 3, so it is not saved */
    apEnemy[0] = &aSol[4];
    apEnemy[1] = &aSol[5];
    apCiv[0] = &aSol[0];
    apCiv[1] = &aSol[1];
    apCiv[2] = &aSol[2];
    apCiv[3] = &aSol[3];
    apCiv[10] = &aSol[4]; /* past the count of 4 */
    apHidden[0] = &aSol[5];

    memset(&st, 0, sizeof(st));
    st.nXcomCount = 3;
    st.nEnemyCount = 2;
    st.nCivilianCount = 4;
    st.nHiddenCount = 1;
    st.apXcomUnits = apXcom;
    st.apEnemyUnits = apEnemy;
    st.apCivilians = apCiv;
    st.apHiddenUnits = apHidden;
    st.pSelUnit = &aSol[2];

    Check(OpenWrite("test_teamunits.tmp", &dat, 5), "  container opened");
    Check(Save_WriteTeamUnits(&dat, &st), "  four chunks written");
    DatFile_Close(&dat);
    /* 1+4+120, 1+120, 4+240, 4+120. */
    CheckEqI32(FileSizeOf(L"test_teamunits.tmp"), 109 + 125 + 121 + 244 + 124,
               "the four payloads");

    memset(apXcom, 0xEE, sizeof(apXcom));
    Check(OpenRead("test_teamunits.tmp", &dat), "  reopened");
    Check(Save_ReadTeamUnits(&dat, &st, apSoldiers, 6), "  read");
    DatFile_Close(&dat);

    CheckEqI32(st.nXcomCount, 3, "the team count round-trips as a byte");
    CheckEqI32(st.nEnemyCount, 2, "the enemy count as a byte");
    CheckEqI32(st.nCivilianCount, 4, "the civilian count as a dword");
    CheckEqI32(st.nHiddenCount, 1, "and the reserve count as a dword");
    Check(st.pSelUnit == &aSol[2], "the selected unit is the second field of teamunits");
    Check(apXcom[0] == &aSol[0] && apXcom[2] == &aSol[2], "the team resolves by nSaveId");
    Check(apXcom[5] == NULL, "a slot past the count is dropped even though it held a unit");
    Check(apEnemy[0] == &aSol[4] && apEnemy[1] == &aSol[5], "the enemy roster resolves");
    Check(apEnemy[2] == NULL, "and stops at the count");
    Check(apCiv[3] == &aSol[3], "the civilian roster resolves");
    Check(apCiv[10] == NULL, "and its stale slot past the count is dropped too");
    Check(apHidden[0] == &aSol[5], "the reserve roster resolves");
    Check(apHidden[1] == NULL, "0x7FFF becomes a NULL reserve slot");
    Check(apXcom[29] == NULL && apCiv[59] == NULL,
          "the reader consumes all 30 and all 60 slots");

    /* An id the roster does not hold. */
    aSol[0].nSaveId = 99;
    Check(OpenWrite("test_teambad.tmp", &dat, 5), "  second container");
    Check(Save_WriteTeamUnits(&dat, &st), "  bad id written");
    DatFile_Close(&dat);
    Check(OpenRead("test_teambad.tmp", &dat), "  reopened");
    Check(!Save_ReadTeamUnits(&dat, &st, apSoldiers, 6), "an out-of-range id is refused");
    DatFile_Close(&dat);

    aSol[0].nSaveId = 0;
    st.pSelUnit = NULL;
    Check(OpenWrite("test_teamsel.tmp", &dat, 5), "  third container");
    Check(Save_WriteTeamUnits(&dat, &st), "  no selection written");
    DatFile_Close(&dat);
    Check(OpenRead("test_teamsel.tmp", &dat), "  reopened");
    Check(Save_ReadTeamUnits(&dat, &st, apSoldiers, 6), "  read");
    DatFile_Close(&dat);
    Check(st.pSelUnit == NULL, "no selection becomes 0x7FFF and back to NULL");

    Check(!Save_WriteTeamUnits(NULL, &st), "NULL dat refused");
    Check(!Save_WriteTeamUnits(&dat, NULL), "NULL state refused");
    Check(!Save_ReadTeamUnits(&dat, NULL, apSoldiers, 6), "NULL read state refused");
}

static void TestTGameDataWrite(void)
{
    DatFile dat;
    TactSaveState st;
    TeamUnitsState rosters;
    FlobjsState flobjs;
    MapGeom geom;
    Soldier aSol[2];
    Soldier *apSol[2];
    Soldier *apXcom[PUFO_SAVE_ROSTER_MAX];
    Soldier *apEnemy[PUFO_SAVE_ROSTER_MAX];
    Soldier *apCiv[PUFO_SAVE_CIV_MAX];
    Soldier *apHidden[PUFO_SAVE_ROSTER_MAX];
    ObjectRecFull aObj[1];
    const ObjectRecFull *apObjC[1];
    TactItem aItem[1];
    const TactItem *apItemC[1];
    /* The 43 bytes the tgamedata payload must be, in the original's write order. */
    static const u8 aWant[43] = {
        0x11,             /* 0x15110 */
        0x04, 0x03, 0x02, 0x01, /* 0x1510C as a dword */
        0x21, 0x22, 0x23, /* 0x15111..0x15113 */
        0x04,             /* 0x15114, the layer count */
        0x25,             /* 0x15115 */
        0x00, 0x05, 0x17, 0x07, /* 0x15116: four sprite-set ids, 0x17 == 23 means none */
        0x09,             /* nMissionType */
        0x01,             /* 0x1517D */
        0x01,             /* the light-enable gate */
        0xAA, 0xAA, 0xAA, 0xAA, 0x01, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, /* 0x15188..0x15194 */
        0x01, 0x02, 0x03, /* 0x15198..0x1519A */
        0x44, 0x33, 0x22, 0x11, /* 0x151A0 */
        0x07, 0x08, 0x09  /* the three craft tiles, one byte each */
    };
    u8 aGot[43];

    printf("Save_WriteTGameData\n");

    memset(&st, 0, sizeof(st));
    st.bUnk15110 = 0x11;
    st.nUnk1510C = 0x01020304;
    st.abUnk15111[0] = 0x21;
    st.abUnk15111[1] = 0x22;
    st.abUnk15111[2] = 0x23;
    st.nMapLayers = 4;
    st.bUnk15115 = 0x25;
    st.abSpriteSet[0] = 0;
    st.abSpriteSet[1] = 5;
    st.abSpriteSet[2] = 23; /* the "no set" id */
    st.abSpriteSet[3] = 7;
    st.nMissionType = 9;
    st.bMissionMode = 1;
    st.bUnk1511E = 1;
    st.anUnk15188[0] = (i32)0xAAAAAAAA;
    st.anUnk15188[1] = 1;
    st.anUnk15188[2] = 2;
    st.anUnk15188[3] = 3;
    st.abUnk15198[0] = 1;
    st.abUnk15198[1] = 2;
    st.abUnk15198[2] = 3;
    st.nUnk151A0 = 0x11223344;
    st.anCraftTile[0] = 7;
    st.anCraftTile[1] = 8;
    st.anCraftTile[2] = 9;
    st.nUnk138E6C = 1234;
    st.nUnk138E70 = 5678;

    memset(aSol, 0, sizeof(aSol));
    for (i32 i = 0; i < 2; ++i) {
        apSol[i] = &aSol[i];
        aSol[i].nSaveId = i;
    }
    memset(apXcom, 0, sizeof(apXcom));
    memset(apEnemy, 0, sizeof(apEnemy));
    memset(apCiv, 0, sizeof(apCiv));
    memset(apHidden, 0, sizeof(apHidden));
    apXcom[0] = &aSol[0];
    apEnemy[0] = &aSol[1];

    memset(&rosters, 0, sizeof(rosters));
    rosters.nXcomCount = 1;
    rosters.nEnemyCount = 1;
    rosters.nCivilianCount = 0;
    rosters.nHiddenCount = 0;
    rosters.apXcomUnits = apXcom;
    rosters.apEnemyUnits = apEnemy;
    rosters.apCivilians = apCiv;
    rosters.apHiddenUnits = apHidden;
    rosters.pSelUnit = &aSol[0];

    memset(aObj, 0, sizeof(aObj));
    memset(aItem, 0, sizeof(aItem));
    apObjC[0] = &aObj[0];
    apItemC[0] = &aItem[0];
    aItem[0].pObj = &aObj[0];
    aItem[0].nRefId = 0;
    aItem[0].nIndex = 12;

    memset(&flobjs, 0, sizeof(flobjs));
    flobjs.apTactItems = apItemC;
    flobjs.nTactItemCount = 1;
    flobjs.apObjects = apObjC;
    flobjs.nObjectCount = 1;

    geom.nDimX = 1;
    geom.nDimY = 1;

    Check(OpenWrite("test_tgamedata.tmp", &dat, 9), "  container opened");
    Check(Save_WriteTGameData(&dat, &st, &rosters, &flobjs, &geom, MapTestLookup, NULL),
          "eight chunks written");
    DatFile_Close(&dat);

    /* 43 + (125+121+244+124) + (4+17) + (2+400*43) + 8, over a header for 8 chunks. */
    CheckEqI32(FileSizeOf(L"test_tgamedata.tmp"),
               4 + 21 * 9 + 43 + 614 + 21 + 17202 + 8, "the whole tactical half");

    Check(OpenRead("test_tgamedata.tmp", &dat), "  reopened");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_TGAMEDATA), "  tgamedata found");
    memset(aGot, 0, sizeof(aGot));
    Check(DatFile_Read(&dat, aGot, 43), "  43 bytes read back");
    Check(memcmp(aGot, aWant, 43) == 0, "the field order matches the original");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_TEAMUNITS), "  teamunits follows");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_ALIENS), "  aliens");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_DIEDSOLDS), "  diedsolds");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_HIDDENSOL), "  hiddensold");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_MAP), "  map");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_V124), "  and v124.dat last");
    {
        i32 aPair[2] = { 0, 0 };
        Check(DatFile_Read(&dat, aPair, 8), "  v124.dat is two dwords");
        CheckEqI32(aPair[0], 1234, "unk_138E6C round-trips");
        CheckEqI32(aPair[1], 5678, "unk_138E70 round-trips");
    }
    DatFile_Close(&dat);

    Check(!Save_WriteTGameData(NULL, &st, &rosters, &flobjs, &geom, MapTestLookup, NULL),
          "NULL dat refused");
    Check(!Save_WriteTGameData(&dat, &st, NULL, &flobjs, &geom, MapTestLookup, NULL),
          "NULL rosters refused");
}

static i32 g_nRmpCalls;
static i32 g_aRmpTileX[4], g_aRmpTileY[4], g_aRmpSizeX[4], g_aRmpSizeY[4], g_aRmpLevel[4];
static char g_aRmpName[4][20];

static i32 RmpRecorder(RmpData *pRmp, const char *pszBase, i32 nTileX, i32 nTileY,
                       i32 nSizeX, i32 nSizeY, i32 nLevel, void *pUser)
{
    (void)pRmp;
    (void)pUser;
    if (g_nRmpCalls < 4) {
        g_aRmpTileX[g_nRmpCalls] = nTileX;
        g_aRmpTileY[g_nRmpCalls] = nTileY;
        g_aRmpSizeX[g_nRmpCalls] = nSizeX;
        g_aRmpSizeY[g_nRmpCalls] = nSizeY;
        g_aRmpLevel[g_nRmpCalls] = nLevel;
        memcpy(g_aRmpName[g_nRmpCalls], pszBase, 20);
    }
    ++g_nRmpCalls;
    return 0;
}

static bool WriteRmpChunk(DatFile *pDat, i32 nCount)
{
    i32 aTileX[PUFO_SAVE_RMP_MODULE_MAX] = {0};
    i32 aTileY[PUFO_SAVE_RMP_MODULE_MAX] = {0};
    i32 aSizeX[PUFO_SAVE_RMP_MODULE_MAX] = {0};
    i32 aSizeY[PUFO_SAVE_RMP_MODULE_MAX] = {0};
    i32 aLevel[PUFO_SAVE_RMP_MODULE_MAX] = {0};
    char aNames[PUFO_SAVE_RMP_MODULE_MAX][20];

    aTileX[0] = 1;  aTileX[1] = 2;  aTileX[2] = 3;
    aTileY[0] = 10; aTileY[1] = 20; aTileY[2] = 30;
    aSizeX[0] = 100; aSizeX[1] = 200; aSizeX[2] = 300;
    aSizeY[0] = 1000; aSizeY[1] = 2000; aSizeY[2] = 3000;
    aLevel[0] = 7;  aLevel[1] = 8;  aLevel[2] = 9;
    memset(aNames, 0, sizeof(aNames));
    memcpy(aNames[0], "mod0", 5);
    memcpy(aNames[1], "mod1", 5);
    memcpy(aNames[2], "mod2", 5);

    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_RMP)) {
        return false;
    }
    if (!DatFile_Write(pDat, &nCount, 4)) { return false; }
    if (!DatFile_Write(pDat, aTileX, sizeof(aTileX))) { return false; }
    if (!DatFile_Write(pDat, aTileY, sizeof(aTileY))) { return false; }
    if (!DatFile_Write(pDat, aSizeX, sizeof(aSizeX))) { return false; }
    if (!DatFile_Write(pDat, aSizeY, sizeof(aSizeY))) { return false; }
    if (!DatFile_Write(pDat, aNames, sizeof(aNames))) { return false; }
    return DatFile_Write(pDat, aLevel, sizeof(aLevel));
}

static void TestReadRmp(void)
{
    DatFile dat;
    RmpData rmp;

    printf("Save_ReadRmp\n");

    memset(&rmp, 0, sizeof(rmp));

    Check(OpenWrite("test_rmp.tmp", &dat, 2), "  container opened");
    Check(WriteRmpChunk(&dat, 3), "  rmp chunk written");
    DatFile_Close(&dat);
    /* 4 + 5*240 + 1200. */
    CheckEqI32(FileSizeOf(L"test_rmp.tmp"), 46 + 4 + 5 * 240 + 1200, "the chunk payload");

    g_nRmpCalls = 0;
    Check(OpenRead("test_rmp.tmp", &dat), "  reopened");
    Check(Save_ReadRmp(&dat, &rmp, RmpRecorder, NULL), "  read");
    DatFile_Close(&dat);

    CheckEqI32(g_nRmpCalls, 3, "one Map_LoadRmp call per module");
    CheckEqI32(g_aRmpTileX[0], 1, "the first block read becomes nTileX");
    CheckEqI32(g_aRmpTileY[0], 10, "the second becomes nTileY");
    CheckEqI32(g_aRmpSizeX[0], 100, "the third becomes nSizeX");
    CheckEqI32(g_aRmpSizeY[0], 1000, "the fourth becomes nSizeY");
    CheckEqI32(g_aRmpLevel[0], 7, "and the LAST block becomes nLevel");
    Check(strcmp(g_aRmpName[0], "mod0") == 0, "the names come from the 1200-byte block");
    Check(strcmp(g_aRmpName[2], "mod2") == 0, "at 20 bytes per module");
    CheckEqI32(g_aRmpLevel[2], 9, "the third module gets the third level");

    /* A zero count loads nothing. */
    Check(OpenWrite("test_rmp0.tmp", &dat, 2), "  empty container");
    Check(WriteRmpChunk(&dat, 0), "  count 0 written");
    DatFile_Close(&dat);
    g_nRmpCalls = 0;
    Check(OpenRead("test_rmp0.tmp", &dat), "  reopened");
    Check(Save_ReadRmp(&dat, &rmp, RmpRecorder, NULL), "  read");
    DatFile_Close(&dat);
    CheckEqI32(g_nRmpCalls, 0, "a zero count loads nothing");

    /* More modules than the original's fixed arrays hold. */
    Check(OpenWrite("test_rmpbig.tmp", &dat, 2), "  oversized container");
    Check(WriteRmpChunk(&dat, 61), "  count 61 written");
    DatFile_Close(&dat);
    Check(OpenRead("test_rmpbig.tmp", &dat), "  reopened");
    Check(!Save_ReadRmp(&dat, &rmp, RmpRecorder, NULL), "a count above 60 is refused");
    DatFile_Close(&dat);

    Check(!Save_ReadRmp(&dat, &rmp, NULL, NULL), "NULL loader refused");
    Check(!Save_ReadRmp(NULL, &rmp, RmpRecorder, NULL), "NULL dat refused");

    remove("test_rmp.tmp");
    remove("test_rmp0.tmp");
    remove("test_rmpbig.tmp");
}

static bool WriteSaveFile(const char *pszName, i32 nTotalSec, u8 bGameState,
                          bool bWithMcnt, u8 nMcnt)
{
    DatFile dat;

    if (!OpenWrite(pszName, &dat, 4)) {
        return false;
    }
    if (!DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_TIME)) { return false; }
    if (!DatFile_Write(&dat, &nTotalSec, 4)) { return false; }
    if (!DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_GAMESTATE)) { return false; }
    if (!DatFile_Write(&dat, &bGameState, 1)) { return false; }
    if (bWithMcnt) {
        if (!DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_MCNT)) { return false; }
        if (!DatFile_Write(&dat, &nMcnt, 1)) { return false; }
    }
    DatFile_Close(&dat);
    return true;
}

static void TestSlotList(void)
{
    SaveSlotFormatCtx ctx;
    GameTime time;
    u8 bGameState;
    static char apLines[PUFO_SAVE_SLOT_COUNT][PUFO_SAVE_SLOT_LINE_SIZE];

    printf("SaveSlot_FormatList\n");

    /* 27524520 s = day 15, month 11, 13:42 of 1999. */
    Check(WriteSaveFile("sav0.sav", 27524520, 1, true, 7), "  slot 0 written");
    /* 0 s = day 1, month 1, 00:00, no mission flag, no mcnt chunk. */
    Check(WriteSaveFile("sav1.sav", 0, 0, false, 0), "  slot 1 written");

    memset(&time, 0, sizeof(time));
    time.nTotalSec = 12345; /* must come back unchanged */
    bGameState = 0;

    memset(apLines, 0, sizeof(apLines));
    memset(&ctx, 0, sizeof(ctx));
    ctx.apLines = apLines;
    ctx.pszSaveDir = "";
    ctx.pTime = &time;
    ctx.pbGameState = &bGameState;
    ctx.pszPrefix = "";                  /* aTextRecs[205 * 1800]: out of range */
    ctx.pszNotSaved = "Game not saved";  /* aTextRecs[205 * 1074] */
    ctx.pszMission = " (mission)";       /* aTextRecs[205 * 1075] */

    SaveSlot_FormatList(&ctx);

    CheckEqStr(apLines[0], "15.11.1999 13:42 (mission) 7",
               "slot 0 is the decoded date plus the mission marker");
    CheckEqStr(apLines[1], "01.01.1999 00:00",
               "slot 1 pads every single-digit field with the separator");
    CheckEqStr(apLines[2], "Game not saved", "a missing save is one text record");
    CheckEqStr(apLines[6], "Game not saved", "for every missing slot");

    CheckEqI32(time.nTotalSec, 12345, "the live clock's total is restored at the end");
    CheckEqI32(time.nDay, 1, "but the decoded day is left in the clock");
    CheckEqI32(time.nMonth, 1, "and so is the month, from the LAST file inspected");
    CheckEqI32(bGameState, 0, "bGameState comes from the last gamestate chunk read");

    /* The last file inspected wins: drop slot 1 and the leftovers come from slot 0. */
    remove("sav1.sav");
    bGameState = 0;
    memset(apLines, 0, sizeof(apLines));
    SaveSlot_FormatList(&ctx);
    CheckEqI32(time.nDay, 15, "with slot 1 gone the clock keeps slot 0's day");
    CheckEqI32(time.nMonth, 11, "and its month");
    CheckEqI32(bGameState, 1, "and bGameState keeps slot 0's value");
    CheckEqI32(time.nTotalSec, 12345, "while the total is still restored");

    /* A mission save with no mcnt chunk gets the marker but no number. */
    Check(WriteSaveFile("sav2.sav", 0, 1, false, 0), "  slot 2 written");
    memset(apLines, 0, sizeof(apLines));
    SaveSlot_FormatList(&ctx);
    CheckEqStr(apLines[2], "01.01.1999 00:00 (mission)", "the mission marker without mcnt");

    /* A file with no gamestate chunk leaves the flag as it was. */
    Check(WriteSaveFile("sav3.sav", 0, 0, false, 0), "  slot 3 written");
    bGameState = 1;
    memset(apLines, 0, sizeof(apLines));
    SaveSlot_FormatList(&ctx);
    CheckEqStr(apLines[3], "01.01.1999 00:00", "slot 3 has no marker");
    CheckEqI32(bGameState, 0, "and the read cleared the flag");

    /* Lines are 40 bytes apart. */
    CheckEqI32((i32)(sizeof(apLines[1]) - sizeof(apLines[0])), 0, "the stride is uniform");
    CheckEqI32((i32)sizeof(apLines[0]), PUFO_SAVE_SLOT_LINE_SIZE, "40 bytes per line");

    remove("sav0.sav");
    remove("sav1.sav");
    remove("sav2.sav");
    remove("sav3.sav");
}

static void FillStrategItem(StrategItem *p, i32 nBase)
{
    p->nType = (u8)(nBase & 0xFF);
    p->bActive = (u8)(nBase + 1);
    p->bCraftClass = (u8)(nBase + 2);
    p->bUnk3 = (u8)(nBase + 3);
    p->nX = nBase + 4;
    p->nY = nBase + 5;
    p->nSiteId = nBase + 6;
    p->nDisplayId = nBase + 7;
    p->bRandA = (u8)(nBase + 8);
    p->nStartX = nBase + 9;
    p->nStartY = nBase + 10;
    p->nDestX = nBase + 11;
    p->nDestY = nBase + 12;
    p->bTravelDir = (i8)(nBase + 13);
    p->nTimer = nBase + 14;
    p->bFlagA = (u8)(nBase + 15);
    p->nSeed = nBase + 16;
    p->bVisible = (u8)(nBase + 17);
    p->nMissionSub = (u8)(nBase + 18);
    p->dTrackA = (double)nBase + 19.5;
    p->dSpeed = (double)nBase + 20.25;
    p->dFracX = (double)nBase + 21.125;
    p->dFracY = (double)nBase + 22.0625;
    p->nTravelStartSec = nBase + 23;
    p->bHyperDetect = (u8)(nBase + 24);
    p->bDetected = (u8)(nBase + 25);
    p->uTravelMin = (u16)(nBase + 26);
    p->nTravelEpochSec = nBase + 27;
    p->nHp = nBase + 28;
    p->bRace = (u8)(nBase + 29);
    p->nPhase = nBase + 30;
    p->bDone = (u8)(nBase + 31);
    p->nTargetX = nBase + 32;
    p->nTargetY = nBase + 33;
    p->nTargetIdx = nBase + 34;
}

static void CheckStrategItem(const StrategItem *pGot, const StrategItem *pWant, const char *pszWhat)
{
    CheckEqI32(pGot->bActive, pWant->bActive, pszWhat);
    CheckEqI32(pGot->nSiteId, pWant->nSiteId, pszWhat);
    CheckEqI32(pGot->nTimer, pWant->nTimer, pszWhat);
    CheckEqI32(pGot->nSeed, pWant->nSeed, pszWhat);
    CheckEqI32(pGot->nDisplayId, pWant->nDisplayId, pszWhat);
    CheckEqI32(pGot->nX, pWant->nX, pszWhat);
    CheckEqI32(pGot->nDestX, pWant->nDestX, pszWhat);
    CheckEqI32(pGot->nY, pWant->nY, pszWhat);
    CheckEqI32(pGot->nStartY, pWant->nStartY, pszWhat);
    CheckEqI32(pGot->nMissionSub, pWant->nMissionSub, pszWhat);
    CheckEqI32(pGot->nTravelStartSec, pWant->nTravelStartSec, pszWhat);
    CheckEqI32(pGot->bHyperDetect, pWant->bHyperDetect, pszWhat);
    CheckEqI32(pGot->nType, pWant->nType, pszWhat);
    CheckEqI32(pGot->uTravelMin, pWant->uTravelMin, pszWhat);
    CheckEqI32(pGot->nTravelEpochSec, pWant->nTravelEpochSec, pszWhat);
    CheckEqI32(pGot->nHp, pWant->nHp, pszWhat);
    CheckEqI32(pGot->nPhase, pWant->nPhase, pszWhat);
    CheckEqI32(pGot->bRace, pWant->bRace, pszWhat);
    CheckEqI32(pGot->nTargetIdx, pWant->nTargetIdx, pszWhat);
    CheckEqI32(pGot->bDone, pWant->bDone, pszWhat);
    CheckEqI32(pGot->nTargetX, pWant->nTargetX, pszWhat);
    CheckEqI32(pGot->nTargetY, pWant->nTargetY, pszWhat);
    Check(pGot->dSpeed == pWant->dSpeed, pszWhat);
    Check(pGot->dTrackA == pWant->dTrackA, pszWhat);
    Check(pGot->dFracX == pWant->dFracX && pGot->dFracY == pWant->dFracY, pszWhat);
}

/* The version-2 read stops before nTargetX / nTargetY (the gate at 0x41554 is on the BUILD's
 * version byte), so those two fields keep whatever StrategItem_AllocAdd left in the fresh
 * record. Asserting them compares uninitialised memory -- it only ever passed because the
 * allocator happened to hand back the block the earlier list had used. Everything the read does
 * write is checked here instead. */
static void CheckStrategItemV2(const StrategItem *pGot, const StrategItem *pWant,
                               const char *pszWhat)
{
    CheckEqI32(pGot->bActive, pWant->bActive, pszWhat);
    CheckEqI32(pGot->nSiteId, pWant->nSiteId, pszWhat);
    CheckEqI32(pGot->nTimer, pWant->nTimer, pszWhat);
    CheckEqI32(pGot->nSeed, pWant->nSeed, pszWhat);
    CheckEqI32(pGot->nDisplayId, pWant->nDisplayId, pszWhat);
    CheckEqI32(pGot->nX, pWant->nX, pszWhat);
    CheckEqI32(pGot->nDestX, pWant->nDestX, pszWhat);
    CheckEqI32(pGot->nY, pWant->nY, pszWhat);
    CheckEqI32(pGot->nStartY, pWant->nStartY, pszWhat);
    CheckEqI32(pGot->nMissionSub, pWant->nMissionSub, pszWhat);
    CheckEqI32(pGot->nTravelStartSec, pWant->nTravelStartSec, pszWhat);
    CheckEqI32(pGot->bHyperDetect, pWant->bHyperDetect, pszWhat);
    CheckEqI32(pGot->nType, pWant->nType, pszWhat);
    CheckEqI32(pGot->uTravelMin, pWant->uTravelMin, pszWhat);
    CheckEqI32(pGot->nTravelEpochSec, pWant->nTravelEpochSec, pszWhat);
    CheckEqI32(pGot->nHp, pWant->nHp, pszWhat);
    CheckEqI32(pGot->nPhase, pWant->nPhase, pszWhat);
    CheckEqI32(pGot->bRace, pWant->bRace, pszWhat);
    CheckEqI32(pGot->nTargetIdx, pWant->nTargetIdx, pszWhat);
    CheckEqI32(pGot->bDone, pWant->bDone, pszWhat);
    Check(pGot->dSpeed == pWant->dSpeed, pszWhat);
    Check(pGot->dTrackA == pWant->dTrackA, pszWhat);
    Check(pGot->dFracX == pWant->dFracX && pGot->dFracY == pWant->dFracY, pszWhat);
}

static void TestStrategChunk(void)
{
    DatFile dat;
    StrategSaveState st;
    StrategSaveState got;
    PtrArray list;
    PtrArray list2;
    StrategItem want[2];
    i32 nPtrA = 0x51515151;
    i32 nPtrB = 0x52525252;

    printf("Save_WriteStrateg / Save_ReadStrateg\n");

    memset(&st, 0, sizeof(st));
    st.bSaveVersion = 3;
    st.nUnk186558 = 0x11111111;
    st.nUnk186550 = 0x22222222;
    st.nUnk186554 = 0x33333333;
    st.abTypeCounters[0] = 1;
    st.abTypeCounters[1] = 2;
    st.abTypeCounters[2] = 0;
    st.abTypeCounters[3] = 4;
    st.abTypeCounters[4] = 0;
    st.nStrategCount = 2;
    st.nUnk186544 = 0x44444444;
    st.nUnkMap6691p4845 = 0x45454545;
    st.nUnk1865BC = 0xBCBCBCBC;
    st.nUnk186574 = 0x74747474;
    st.pnUnk1865A8 = &nPtrA;
    st.pnUnk186594 = &nPtrB;
    st.nUnk186568 = 0x68686868;
    st.nUnk186BE4 = 0xBE4BE4BE;
    st.nUnkMap6691p6937 = 0x69696969;
    st.nSelectedShipIdx = 7;

    FillStrategItem(&want[0], 1000);
    FillStrategItem(&want[1], 2000);
    /* The shifted-read test below depends on this byte. */
    want[0].nTargetX = 0x11223344;
    want[1].bActive = 7;

    PtrArray_ctor(&list);
    Check(StrategItem_AllocAdd(&list, 0), "  item 0 allocated");
    Check(StrategItem_AllocAdd(&list, 0), "  item 1 allocated");
    memcpy(PtrArray_Get(&list, 0), &want[0], sizeof(StrategItem));
    memcpy(PtrArray_Get(&list, 1), &want[1], sizeof(StrategItem));

    Check(OpenWrite("test_strateg.tmp", &dat, 2), "  container opened");
    Check(Save_WriteStrateg(&dat, &st, &list), "  strateg written");
    DatFile_Close(&dat);
    PtrArray_Clear(&list);

    /* 134 header bytes plus 113 per item. */
    CheckEqI32(FileSizeOf(L"test_strateg.tmp"), 46 + 134 + 2 * 113, "the chunk payload");

    /* A current-format load. */
    memset(&got, 0, sizeof(got));
    got.bSaveVersion = 3;
    got.pnUnk1865A8 = &nPtrA;
    got.pnUnk186594 = &nPtrB;
    nPtrA = 0;
    nPtrB = 0;
    PtrArray_ctor(&list2);
    Check(OpenRead("test_strateg.tmp", &dat), "  reopened");
    Check(Save_ReadStrateg(&dat, &got, &list2), "  read");
    DatFile_Close(&dat);

    CheckEqI32(list2.nCount, 2, "the list is rebuilt by StrategItem_AllocAdd");
    CheckEqI32(got.nStrategCount, 2, "and the separate count global comes from the chunk");
    CheckEqI32(got.nUnk186558, 0x11111111, "the header round trips");
    CheckEqI32(got.nUnkMap6691p4845, 0x45454545, "including the Map-block entries");
    CheckEqI32(got.nSelectedShipIdx, 7, "and the selected ship");
    CheckEqI32(nPtrA, 0x51515151, "the two pointer entries carry what they point at");
    CheckEqI32(nPtrB, 0x52525252, "not the addresses");
    CheckEqI32(got.abTypeCounters[3], 4, "the craft-type counters survive a load");

    CheckStrategItem((const StrategItem *)PtrArray_Get(&list2, 0), &want[0], "item 0");
    CheckStrategItem((const StrategItem *)PtrArray_Get(&list2, 1), &want[1], "item 1");
    PtrArray_DeleteAll(&list2);
    PtrArray_Clear(&list2);

    /* The gate is on the BUILD's version byte, so a version-2 build reads 105 bytes per
     * item out of records that are 113 bytes long, and the second item starts inside the
     * first one's nTargetX. */
    memset(&got, 0, sizeof(got));
    got.bSaveVersion = 2;
    got.pnUnk1865A8 = &nPtrA;
    got.pnUnk186594 = &nPtrB;
    PtrArray_ctor(&list2);
    Check(OpenRead("test_strateg.tmp", &dat), "  reopened");
    Check(Save_ReadStrateg(&dat, &got, &list2), "  read");
    DatFile_Close(&dat);

    CheckEqI32(list2.nCount, 2, "the rebuilt list still has two items");
    CheckStrategItemV2((const StrategItem *)PtrArray_Get(&list2, 0), &want[0],
                       "the first item is unaffected");
    CheckEqI32(((StrategItem *)PtrArray_Get(&list2, 1))->bActive, 0x44,
               "a version-2 build reads 105 bytes per item, so the second item starts"
               " inside the first one's nTargetX");
    PtrArray_DeleteAll(&list2);
    PtrArray_Clear(&list2);

    /* A save with no strateg chunk still empties the live list. */
    Check(OpenWrite("test_nost.tmp", &dat, 2), "  container without strateg");
    Check(DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_TIME), "  a dummy chunk");
    Check(DatFile_Write(&dat, &nPtrA, 4), "  written");
    DatFile_Close(&dat);

    PtrArray_ctor(&list2);
    Check(StrategItem_AllocAdd(&list2, 0), "  a live item");
    CheckEqI32(list2.nCount, 1, "one item before the load");
    got.nStrategCount = 0;
    Check(OpenRead("test_nost.tmp", &dat), "  reopened");
    Check(!Save_ReadStrateg(&dat, &got, &list2), "  the missing chunk is reported");
    DatFile_Close(&dat);
    CheckEqI32(list2.nCount, 0, "and the list was emptied");
    PtrArray_Clear(&list2);

    /* The count is a global the reader only overwrites when the read succeeds. With a stale
     * non-zero count and no chunk, the original still allocates that many items and reads
     * nothing into them, so the list comes back full of uninitialised records. */
    PtrArray_ctor(&list2);
    Check(StrategItem_AllocAdd(&list2, 0), "  a live item");
    got.nStrategCount = 3;
    Check(OpenRead("test_nost.tmp", &dat), "  reopened");
    Check(!Save_ReadStrateg(&dat, &got, &list2), "  the missing chunk is reported");
    DatFile_Close(&dat);
    CheckEqI32(list2.nCount, 3, "but the stale count still allocates three items");
    PtrArray_DeleteAll(&list2);
    PtrArray_Clear(&list2);

    Check(!Save_WriteStrateg(&dat, &st, NULL), "NULL list refused");
    Check(!Save_ReadStrateg(&dat, &got, NULL), "NULL list refused");

    /* Save_ReadStrategVer @ 0x3f1c8: LoadGame reads the save format version out of the strateg
     * chunk before it reads any chunk body, because that byte decides how the others are sized. */
    {
        i8 bVer = 99;
        Check(OpenRead("test_strateg.tmp", &dat), "  reopened for the version read");
        Check(Save_ReadStrategVersion(&dat, &bVer), "  version byte read");
        CheckEqI32(bVer, 3, "  the version byte is the one that was written");
        DatFile_Close(&dat);
        Check(!Save_ReadStrategVersion(NULL, &bVer), "  NULL dat refused");
        Check(!Save_ReadStrategVersion(&dat, NULL), "  NULL version out refused");
    }

    remove("test_strateg.tmp");
    remove("test_nost.tmp");
}

/* SaveGame @ 0x44988. */
static int   g_nRenderCalls;
static int   g_bFileExistedAtRender;
static const char *g_pszRenderProbe;

static void RenderFrameProbe(void *pUser)
{
    (void)pUser;
    g_nRenderCalls++;
    g_bFileExistedAtRender = DatFile_Exists(g_pszRenderProbe) ? 1 : 0;
}

/* Read the container directory straight out of the file, so the chunk ORDER can be
 * checked and not only the presence of each chunk. Names are stored uppercased. */
static bool TagEq(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 'a' + 'A');
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return false;
        }
    }
    return *a == 0 && *b == 0;
}
static i32 ReadDirTags(const wchar_t *pwPath, char aOut[40][14], u32 aSize[40])
{
    u8 hdr[4 + 21 * 100];
    HANDLE hFile;
    DWORD nRead = 0;
    i32 n = 0;

    hFile = CreateFileW(pwPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }
    if (!ReadFile(hFile, hdr, (DWORD)sizeof(hdr), &nRead, NULL) ||
        nRead != (DWORD)sizeof(hdr)) {
        (void)CloseHandle(hFile);
        return -1;
    }
    (void)CloseHandle(hFile);
    for (i32 i = 0; i < 100; ++i) {
        const u8 *p = hdr + 4 + 21 * i;

        if (p[0] == 0) {
            continue;
        }
        if (n >= 40) {
            break;
        }
        memcpy(aOut[n], p, 13);
        aOut[n][13] = 0;
        memcpy(&aSize[n], p + 13, 4);
        n++;
    }
    return n;
}

static void TestSaveGame(void)
{
    static u8 aProduct[16000];
    static RmpData rmp;
    static i32 nPtrA, nPtrB;
    static SaveGameInput in;
    static char aTags[40][14];
    static u32 aSize[40];
    DatFile dat;
    SaveBasesState bases;
    SaveResearchState research;
    SaveSmapState smap;
    StrategSaveState strateg;
    SaveTimeRecord time;
    Soldier aSol[1];
    Soldier *apSol[1];
    TactItem aItem[1];
    TactItem *apItem[1];
    ObjectRecFull aObj[1];
    ObjectRecFull *apObj[1];
    const BaseRec *apBases[1];
    const Ship *apShips[1];
    PtrArray list;
    TactSaveState tact;
    TeamUnitsState rosters;
    FlobjsState flobjs;
    MapGeom geom;
    Soldier *apXcom[PUFO_SAVE_ROSTER_MAX];
    Soldier *apEnemy[PUFO_SAVE_ROSTER_MAX];
    Soldier *apCiv[PUFO_SAVE_CIV_MAX];
    Soldier *apHidden[PUFO_SAVE_ROSTER_MAX];
    i32 nMissionCount;
    u16 nUiScreen;
    i32 nDir;
    static const char *const aWantOrder[] = {
        PUFO_SAVE_TAG_TACTVER, PUFO_SAVE_TAG_GAMESTATE, PUFO_SAVE_TAG_BASES,
        PUFO_SAVE_TAG_PRODUCT, PUFO_SAVE_TAG_RESEARCH, PUFO_SAVE_TAG_SHIPS,
        PUFO_SAVE_TAG_SMAP, PUFO_SAVE_TAG_STRATEG, PUFO_SAVE_TAG_TIME,
        PUFO_SAVE_TAG_SOLDIERS, PUFO_SAVE_TAG_STACTFLG, PUFO_SAVE_TAG_124SCB,
        PUFO_SAVE_TAG_MCNT, PUFO_SAVE_TAG_TGAMEDATA, PUFO_SAVE_TAG_TEAMUNITS,
        PUFO_SAVE_TAG_ALIENS, PUFO_SAVE_TAG_DIEDSOLDS, PUFO_SAVE_TAG_HIDDENSOL,
        PUFO_SAVE_TAG_FLOBJS, PUFO_SAVE_TAG_MAP, PUFO_SAVE_TAG_V124,
        PUFO_SAVE_TAG_OBJBEFORE, PUFO_SAVE_TAG_RMP
    };

    printf("SaveGame\n");

    memset(&bases, 0, sizeof(bases));
    memset(&research, 0, sizeof(research));
    memset(&smap, 0, sizeof(smap));
    memset(&time, 0, sizeof(time));
    memset(&tact, 0, sizeof(tact));
    memset(&rosters, 0, sizeof(rosters));
    memset(&rmp, 0, sizeof(rmp));
    memset(aProduct, 0x5A, sizeof(aProduct));

    bases.bBaseCount = 0;
    nPtrA = 0x1111;
    nPtrB = 0x2222;
    memset(&strateg, 0, sizeof(strateg));
    strateg.bSaveVersion = 3;
    strateg.nStrategCount = 0;
    strateg.pnUnk1865A8 = &nPtrA;
    strateg.pnUnk186594 = &nPtrB;
    memset(&list, 0, sizeof(list));

    memset(aSol, 0, sizeof(aSol));
    memset(aItem, 0, sizeof(aItem));
    memset(aObj, 0, sizeof(aObj));
    apSol[0] = &aSol[0];
    apItem[0] = &aItem[0];
    apObj[0] = &aObj[0];
    aItem[0].pObj = &aObj[0];
    apBases[0] = NULL;
    apShips[0] = NULL;

    memset(apXcom, 0, sizeof(apXcom));
    memset(apEnemy, 0, sizeof(apEnemy));
    memset(apCiv, 0, sizeof(apCiv));
    memset(apHidden, 0, sizeof(apHidden));
    rosters.nXcomCount = 1;
    rosters.nEnemyCount = 0;
    rosters.nCivilianCount = 0;
    rosters.nHiddenCount = 0;
    rosters.apXcomUnits = apXcom;
    rosters.apEnemyUnits = apEnemy;
    rosters.apCivilians = apCiv;
    rosters.apHiddenUnits = apHidden;
    rosters.pSelUnit = &aSol[0];
    apXcom[0] = &aSol[0];

    geom.nDimX = 1;
    geom.nDimY = 1;

    rmp.nModCount = 2;
    rmp.anModTileX[0] = 3;
    rmp.anModTileY[0] = 5;
    rmp.anModSizeX[0] = 7;
    rmp.anModSizeY[0] = 9;
    rmp.anModLevel[0] = 11;
    memcpy(rmp.aszModName[0], "mod_a", 6);
    rmp.anModTileX[1] = 13;
    rmp.anModLevel[1] = 17;
    memcpy(rmp.aszModName[1], "mod_b", 6);

    nMissionCount = 41;
    nUiScreen = 7;

    memset(&in, 0, sizeof(in));
    in.pszAppDir = "";
    in.nSlot = 5;
    in.bTactFlag = 1;
    in.pnMissionCount = &nMissionCount;
    in.pnUiScreen = &nUiScreen;
    in.pBases = &bases;
    in.apBases = apBases;
    in.pProductBlock = aProduct;
    in.pResearch = &research;
    in.apShips = apShips;
    in.nShipCount = 0;
    in.pSmap = &smap;
    in.pStrateg = &strateg;
    in.pStrategList = &list;
    in.pTime = &time;
    in.apSoldiers = apSol;
    in.nSoldierCount = 1;
    in.apTactItems = apItem;
    in.nTactItemCount = 1;
    in.apObjects = apObj;
    in.nObjectCount = 1;
    in.pTact = &tact;
    in.pRosters = &rosters;
    in.pFlobjs = &flobjs;
    in.pMap = &geom;
    in.pfnLookup = MapTestLookup;
    in.pRmp = &rmp;
    in.pfnRenderFrame = RenderFrameProbe;
    memset(&flobjs, 0, sizeof(flobjs));
    flobjs.apTactItems = (const TactItem *const *)apItem;
    flobjs.nTactItemCount = 1;
    flobjs.apObjects = (const ObjectRecFull *const *)apObj;
    flobjs.nObjectCount = 1;

    (void)DeleteFileW(L"sav5.sav");
    g_nRenderCalls = 0;
    g_bFileExistedAtRender = -1;
    g_pszRenderProbe = "sav5.sav";

    DatFile_ctor(&dat);   /* the original's handle is a zero-initialised global */
    Check(SaveGame(&dat, &in), "tactical slot written");
    CheckEqI32(g_nRenderCalls, 1, "  Render_Frame runs once");
    CheckEqI32(g_bFileExistedAtRender, 0, "  and before the slot file exists");
    Check(DatFile_Exists("sav5.sav"), "  sav5.sav was built from the dir and slot");

    nDir = ReadDirTags(L"sav5.sav", aTags, aSize);
    CheckEqI32(nDir, 23, "  twenty-three chunks in the tactical branch");
    for (i32 i = 0; i < nDir && i < 23; ++i) {
        char szMsg[80];
        snprintf(szMsg, sizeof(szMsg), "  chunk %d is %s", i, aTags[i]);
        Check(TagEq(aTags[i], aWantOrder[i]), szMsg);
    }

    CheckEqI32((i32)aSize[0], 2, "  tactver is two bytes");
    CheckEqI32((i32)aSize[1], 1, "  gamestate is one byte");
    CheckEqI32((i32)aSize[3], 16000, "  product is the whole 16000-byte block");
    CheckEqI32((i32)aSize[7], PUFO_SAVE_STRATEG_HEADER_SIZE, "  strateg header only, no items");
    CheckEqI32((i32)aSize[10], 1, "  stactflg follows soldiers");
    CheckEqI32((i32)aSize[11], 4, "  and 1_24scb after it");
    CheckEqI32((i32)aSize[12], 1, "  mcnt is one byte");
    CheckEqI32((i32)aSize[22], 4 + 5 * 240 + 1200,
               "  rmp is count plus five dword blocks plus the names");

    CheckEqI32(nMissionCount, 42, "  the mission counter was incremented at save time");
    CheckEqI32(nUiScreen, 0, "  nUiScreen is cleared only in the tactical branch");

    Check(OpenRead("sav5.sav", &dat), "  reopened");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_MCNT), "  mcnt found");
    {
        u8 bMcnt = 0;
        Check(DatFile_Read(&dat, &bMcnt, 1), "  mcnt byte read");
        CheckEqI32(bMcnt, 42, "  the value written is the INCREMENTED counter");
    }
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_RMP), "  rmp found");
    {
        i32 nCount = 0;
        i32 aBlock[60];
        u8 aNames[1200];

        Check(DatFile_Read(&dat, &nCount, 4), "  rmp count first");
        CheckEqI32(nCount, 2, "  two modules");
        Check(DatFile_Read(&dat, aBlock, 240), "  then tileX");
        CheckEqI32(aBlock[0], 3, "  tileX[0]");
        Check(DatFile_Read(&dat, aBlock, 240), "  then tileY");
        CheckEqI32(aBlock[0], 5, "  tileY[0]");
        Check(DatFile_Read(&dat, aBlock, 240), "  then sizeX");
        CheckEqI32(aBlock[0], 7, "  sizeX[0]");
        Check(DatFile_Read(&dat, aBlock, 240), "  then sizeY");
        CheckEqI32(aBlock[0], 9, "  sizeY[0]");
        memset(aNames, 0, sizeof(aNames));
        Check(DatFile_Read(&dat, aNames, 1200), "  then the 1200-byte name block");
        CheckEqStr((const char *)aNames, "mod_a", "  name[0]");
        CheckEqStr((const char *)aNames + 20, "mod_b", "  name[1] at +20");
        Check(DatFile_Read(&dat, aBlock, 240), "  and level LAST, after the names");
        CheckEqI32(aBlock[0], 11, "  level[0]");
    }
    DatFile_Close(&dat);

    /* The geoscape half: same driver, no tactical chunks, no side effects. */
    in.nSlot = 6;
    in.bTactFlag = 0;
    nUiScreen = 7;
    (void)DeleteFileW(L"sav6.sav");
    DatFile_ctor(&dat);
    Check(SaveGame(&dat, &in), "geoscape slot written");
    Check(DatFile_Exists("sav6.sav"), "  sav6.sav");
    nDir = ReadDirTags(L"sav6.sav", aTags, aSize);
    CheckEqI32(nDir, 12, "  twelve chunks without the tactical branch");
    Check(OpenRead("sav6.sav", &dat), "  reopened");
    Check(!DatFile_FindChunk(&dat, PUFO_SAVE_TAG_MCNT), "  no mcnt");
    Check(!DatFile_FindChunk(&dat, PUFO_SAVE_TAG_TGAMEDATA), "  no tgamedata");
    Check(!DatFile_FindChunk(&dat, PUFO_SAVE_TAG_RMP), "  no rmp");
    Check(DatFile_FindChunk(&dat, PUFO_SAVE_TAG_SOLDIERS), "  soldiers still there");
    CheckEqI32(nMissionCount, 42, "  the counter is untouched by a geoscape save");
    CheckEqI32(nUiScreen, 7, "  and so is nUiScreen");
    CheckEqI32(g_nRenderCalls, 2, "  a frame is rendered for both kinds of save");
    DatFile_Close(&dat);

    (void)DeleteFileW(L"sav5.sav");
    (void)DeleteFileW(L"sav6.sav");
}

/* LoadGame @ 0x44bbc. Round-trip against the SaveGame driver above: write a geoscape slot,
 * then load it into a fresh state and check the re-derivation passes the readers do not do
 * themselves. */
static int  g_nLoadRenders;
static int  g_nLoadShutdowns;
static int  g_nLoadInitUi;
static int  g_nLoadTactShut;
static int  g_nLoadFreeAll;
static int  g_nLoadTactInit;

static void LoadRenderProbe(void *pUser) { (void)pUser; g_nLoadRenders++; }
static void LoadShutdownProbe(void *pUser) { (void)pUser; g_nLoadShutdowns++; }
static void LoadInitUiProbe(i32 nMode, void *pUser) { (void)nMode; (void)pUser; g_nLoadInitUi++; }
static void LoadTactShutProbe(void *pUser) { (void)pUser; g_nLoadTactShut++; }
static void LoadFreeAllProbe(void *pUser) { (void)pUser; g_nLoadFreeAll++; }
static bool LoadTactInitProbe(i32 nMode, void *pUser) { (void)nMode; (void)pUser; g_nLoadTactInit++; return true; }

static void TestLoadGame(void)
{
    static u8 aProduct[17152 + 3200];
    static u8 aGrid[1524];
    static u8 aSiteState[8];
    static i32 aRadarPct[8]; /* unk_162008, eight dwords */
    /* auManufCol @ 0x15E19C: 200 u16 per base over eight bases = 3200 bytes, which is exactly the
     * block the version-<3 tail copies out (0x455F8) and clears (0x45638). */
    static u16 aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT];
    static u16 aResearchCols[144];
    static u16 uResearchEntries;
    static u16 aShotWords[128];
    static Ship aShip;
    static Ship *apShips[1];
    static BaseRec aBase;
    static BaseRec *apBases[1];
    static ObjectRecFull aObj[5];
    static ObjectRecFull *apObj[5];
    static Soldier aSol[1];
    static Soldier *apSol[1];
    static SaveBasesState bases;
    static SaveResearchState research;
    static SaveSmapState smap;
    static StrategSaveState strateg;
    static SaveTimeRecord timeRec;
    static GameTime clock;
    static SmapView view;
    static SmapWorldSize worldSize;
    static PtrArray list;
    static GeoscapeWorld world;
    static ShipCtorEnv shipEnv;
    static i32 nSeed;
    static LoadGameHooks hooks;
    static LoadGameEnv env;
    static i32 nListMode;
    static i32 nSoldierCount;
    static u32 nTactVerOut;
    static u8 bTactFlag, bGeoscapeUi, bGameState, bCraftCount, bMissionCount, bAssaultFlag;
    static u16 nUiScreen;
    static i32 nPtrA, nPtrB;
    static SaveGameInput in;
    static TactSaveState tact;
    static TeamUnitsState rosters;
    static FlobjsState flobjs;
    static MapGeom geom;
    static TactItem aItem[1];
    static TactItem *apItem[1];
    static LoadGameEnv bad;
    DatFile dat;

    printf("LoadGame @ 0x44bbc\n");

    /* Write a geoscape slot (no tactical chunks) with one base, one craft and one item. */
    memset(&bases, 0, sizeof(bases));
    memset(&research, 0, sizeof(research));
    memset(&smap, 0, sizeof(smap));
    memset(&timeRec, 0, sizeof(timeRec));
    memset(&strateg, 0, sizeof(strateg));
    memset(aProduct, 0, sizeof(aProduct));
    bases.bBaseCount = 1;
    strateg.bSaveVersion = 3;
    strateg.nStrategCount = 1;
    strateg.pnUnk1865A8 = &nPtrA; /* Save_WriteStrateg hands these to DatFile_Write as addresses */
    strateg.pnUnk186594 = &nPtrB;
    timeRec.nSlot105 = 31536000u; /* one year, for the decode check */

    /* The ship must be heap-allocated: Save_ReadShips frees the slots it is handed. */
    memset(&aShip, 0, sizeof(aShip));
    aShip.bId = 7;
    aShip.bCraftType = 2;
    aShip.bHomeBase = 0;
    aShip.bWeaponsHot = 1;   /* the off-map item must clear this and set bAtBase */
    aShip.bAtBase = 0;
    apShips[0] = (Ship *)malloc(sizeof(Ship));
    Check(apShips[0] != NULL, "  test ship allocated");
    memcpy(apShips[0], &aShip, sizeof(Ship));

    memset(&aBase, 0, sizeof(aBase));
    aBase.bUnk4 = 4;
    aBase.bSlot = 0;
    aBase.nUnk8 = 120;
    aBase.nUnk12 = 80;
    apBases[0] = &aBase;

    memset(aObj, 0, sizeof(aObj));
    for (int i = 0; i < 5; ++i) {
        apObj[i] = &aObj[i];
    }

    memset(&list, 0, sizeof(list));
    {
        StrategItem *pItem = (StrategItem *)calloc(1, sizeof(StrategItem));
        Check(pItem != NULL, "  test item allocated");
        pItem->nType = 1;
        pItem->bActive = 1;
        pItem->nDisplayId = 7;      /* matches Ship.bId */
        pItem->nX = 1500;           /* beyond 1000 */
        pItem->nY = 40;
        pItem->bVisible = 1;
        pItem->nTargetX = 900;      /* beyond 800 */
        pItem->nTargetY = 100;
        pItem->nDestX = 120;
        pItem->nDestY = 80;
        PtrArray_Add(&list, pItem);
    }

    memset(&in, 0, sizeof(in));
    memset(&tact, 0, sizeof(tact));
    memset(&rosters, 0, sizeof(rosters));
    memset(&flobjs, 0, sizeof(flobjs));
    memset(&geom, 0, sizeof(geom));
    memset(aItem, 0, sizeof(aItem));
    memset(aSol, 0, sizeof(aSol));
    apItem[0] = &aItem[0];
    apSol[0] = &aSol[0];
    geom.nDimX = 1;
    geom.nDimY = 1;
    in.pszAppDir = "";
    in.nSlot = 7;
    in.bTactFlag = 0;
    in.pBases = &bases;
    in.apBases = (const BaseRec *const *)apBases;
    in.pProductBlock = aProduct;
    in.pResearch = &research;
    in.apShips = (const Ship *const *)apShips;
    in.nShipCount = 1;
    in.pSmap = &smap;
    in.pStrateg = &strateg;
    in.pStrategList = &list;
    in.pTime = &timeRec;
    in.apSoldiers = apSol;
    in.nSoldierCount = 1;
    in.apTactItems = apItem;
    in.nTactItemCount = 1;
    in.apObjects = (ObjectRecFull *const *)apObj;
    in.nObjectCount = 5;
    in.pTact = &tact;
    in.pRosters = &rosters;
    in.pFlobjs = &flobjs;
    in.pMap = &geom;
    in.pfnLookup = MapTestLookup;
    in.pfnRenderFrame = RenderFrameProbe;
    (void)DeleteFileW(L"sav7.sav");
    g_pszRenderProbe = "sav7.sav";
    DatFile_ctor(&dat);
    Check(SaveGame(&dat, &in), "  slot 7 written for the load round-trip");
    DatFile_Close(&dat);

    /* Fresh state for the load: the readers re-read everything the writer wrote. */
    memset(aGrid, 0, sizeof(aGrid));
    memset(aSiteState, 0, sizeof(aSiteState));
    memset(aRadarPct, 0, sizeof(aRadarPct));
    memset(aManuf, 0, sizeof(aManuf));
    memset(aResearchCols, 0, sizeof(aResearchCols));
    memset(aShotWords, 0xEE, sizeof(aShotWords));
    memset(&world, 0, sizeof(world));
    memset(&view, 0, sizeof(view));
    memset(&clock, 0, sizeof(clock));
    memset(&shipEnv, 0, sizeof(shipEnv));
    memset(&bases, 0, sizeof(bases));
    bases.bBaseCount = 0;
    memset(&strateg, 0, sizeof(strateg));
    strateg.bSaveVersion = 3;
    strateg.nStrategCount = 1;
    memset(&list, 0, sizeof(list));
    {
        StrategItem *pItem = (StrategItem *)calloc(1, sizeof(StrategItem));
        pItem->nType = 1;
        pItem->bActive = 1;
        pItem->nDisplayId = 7;
        pItem->nX = 1500;
        pItem->nY = 40;
        pItem->bVisible = 1;
        pItem->nTargetX = 900;
        pItem->nTargetY = 100;
        pItem->nDestX = 120;
        pItem->nDestY = 80;
        PtrArray_Add(&list, pItem);
    }
    world.apCraft = (Ship *const *)apShips;
    world.nShipCount = 1;
    world.apSiteState = aSiteState;
    world.apRadarPct = aRadarPct;
    world.pTime = &clock;
    shipEnv.apObjects = apObj;
    shipEnv.nObjectCount = 5;

    bTactFlag = 0;
    bGeoscapeUi = 1;
    bGameState = 0;
    bCraftCount = 1;
    bMissionCount = 99;
    bAssaultFlag = 1;
    nUiScreen = 3;
    nListMode = 0;
    nSoldierCount = 0;
    nSeed = 5;
    uResearchEntries = 0;

    hooks.pfnRenderFrame = LoadRenderProbe;
    hooks.pfnStartUiShutdown = LoadShutdownProbe;
    hooks.pfnGeoscapeInitUi = LoadInitUiProbe;
    hooks.pfnTactShutdown = LoadTactShutProbe;
    hooks.pfnGeoscapeUiFreeAll = LoadFreeAllProbe;
    hooks.pfnTactInitMission = LoadTactInitProbe;
    hooks.pUser = NULL;

    memset(&env, 0, sizeof(env));
    env.pszAppDir = "";
    env.nSlot = 7;
    env.pbTactFlag = &bTactFlag;
    env.pbGeoscapeUi = &bGeoscapeUi;
    env.pbGameState = &bGameState;
    env.puUiScreen = &nUiScreen;
    env.pnSoldierCount = &nSoldierCount;
    env.pnListMode = &nListMode;
    env.pbMissionCount = &bMissionCount;
    env.pbCraftSelectAssaultFlag = &bAssaultFlag;
    env.pBases = &bases;
    env.apBases = apBases;
    env.nOldBaseCount = 0;
    env.pProductBlock = aProduct;
    env.pResearch = &research;
    env.apCraft = apShips;
    env.pbCraftCount = &bCraftCount;
    env.pShipEnv = &shipEnv;
    env.pSmap = &smap;
    env.pView = &view;
    env.paZoomSets = NULL;
    env.pWorldSize = &worldSize;
    env.pStrateg = &strateg;
    env.pStrategList = &list;
    env.pnSeed = &nSeed;
    env.apSoldiers = apSol;
    env.nSoldierCapacity = 1;
    env.apObjects = apObj;
    env.nObjectCount = 5;
    env.pnTactVerOut = &nTactVerOut;
    env.pWorld = &world;
    env.abFacilityGrid = aGrid;
    env.apResearchCols = aResearchCols;
    env.apManufCols = aManuf;
    env.puResearchEntries = &uResearchEntries;
    env.puShotObjectWords = aShotWords;
    env.pTimeRec = &timeRec;
    env.pTime = &clock;
    env.pSpawnCtx = NULL;
    env.pMissionCtx = NULL;
    env.pbSpawnOnceFlag = NULL;
    env.pHooks = &hooks;

    g_nLoadRenders = g_nLoadShutdowns = g_nLoadInitUi = 0;
    g_nLoadTactShut = g_nLoadFreeAll = g_nLoadTactInit = 0;

    bad = env;
    bad.nSlot = 98;
    DatFile_ctor(&dat);
    CheckEqI32(LoadGame(&dat, &bad), 0, "LoadGame(missing slot) == 0");
    CheckEqI32(g_nLoadRenders, 0, "  and no frame was rendered for it");
    DatFile_Close(&dat);

    DatFile_ctor(&dat);
    CheckEqI32(LoadGame(&dat, &env), 1, "LoadGame(sav7) == 1");
    CheckEqI32(g_nLoadRenders, 1, "  one frame rendered first");
    CheckEqI32(g_nLoadInitUi, 0, "  screen 3 is not the start menu: no InitUI");
    /* 0x44D4C..0x44D84: bGeoscapeUi && !bGameState is the one teardown combination that
     * frees NOTHING — the original's own path (verified in ARM: the FreeAll call sits
     * behind a second bGameState test that this combination fails). */
    CheckEqI32(g_nLoadFreeAll, 0, "  geoscape UI without a tactical save: the no-free path");
    CheckEqI32(g_nLoadTactShut, 0, "  and Tact_Shutdown did not");
    CheckEqI32(g_nLoadTactInit, 0, "  a geoscape save does not re-init a mission");
    CheckEqI32(nListMode, 1, "  MainMenu_nListMode becomes 1");
    CheckEqI32(bMissionCount, 99, "  the mission counter is untouched without mcnt");
    CheckEqI32(bGameState, 0, "  bGameState read from the gamestate chunk");

    {
        StrategItem *pItem = (StrategItem *)list.apItems[0];
        Check(pItem != NULL, "  the strateg item survived the read");
        if (pItem != NULL) {
            CheckEqI32(pItem->nX, 1500, "  version 3 does not teleport craft items");
            CheckEqI32(pItem->nTargetX, 900, "  and does not reset travel targets");
            /* The craft pass set bAtBase (off-map + weapons hot) and then cleared
             * bVisible, so the detection mirror lands on 0, not on the saved 1. */
            CheckEqI32(pItem->bVisible, 0, "  bAtBase cleared the item's bVisible");
            CheckEqI32(pItem->bDetected, 0, "  bDetected mirrors bVisible");
        }
    }
    /* Save_ReadShips replaced the slot with a freshly allocated ship; the checks read that. */
    Check(apShips[0] != NULL && apShips[0] != &aShip, "  the ship slot was re-allocated by the read");
    CheckEqI32(apShips[0] ? apShips[0]->bWeaponsHot : -1, 0, "  the off-map item cleared Ship.bWeaponsHot");
    CheckEqI32(apShips[0] ? apShips[0]->bAtBase : -1, 1, "  and set Ship.bAtBase");
    {
        StrategItem *pItem = (StrategItem *)list.apItems[0];
        Check(pItem != NULL && pItem->bVisible == 0, "  bAtBase then cleared the item's bVisible");
    }
    CheckEqI32(bCraftCount, 1, "  the craft count round-tripped");
    CheckEqI32((i32)bases.bBaseCount, 1, "  the base count round-tripped");
    CheckEqI32((i32)strateg.bSaveVersion, 3, "  the version byte is stamped 3 after the load");
    CheckEqI32(bAssaultFlag, 0, "  bCraftSelectAssaultFlag cleared");
    CheckEqI32((i32)timeRec.nSlot105, 31536000, "  the time chunk round-trips nSlot105");
    CheckEqI32(clock.nTotalSec, 31536000, "  and lands on the live clock");
    CheckEqI32(clock.nYear, 2000, "  GameTime_Decode ran on the loaded clock (1999 + 1 year)");
    Check(aShotWords[29] == 0 && aShotWords[101] == 0, "  the shot object's words 29 and 101 zeroed");
    Check(aShotWords[30] == 0xEEEE, "  word 30 is past the nCraftCount-1 stores and stays");
    CheckEqI32((i32)nTactVerOut, 1, "  tactver value handed to the slot-context slot");
    Check(apObj[4] == &aObj[4], "  and the object table is NOT touched by tactver");
    CheckEqI32((i32)aRadarPct[0], Base_CountEmptyFacilityCells(aGrid, 0), "  radar byte recomputed");

    DatFile_Close(&dat);
    for (int i = 0; i < list.nCount; ++i) {
        free(list.apItems[i]);
    }
    free(apShips[0]);   /* allocated by Save_ReadShips */
    free(apBases[0]);   /* allocated by Save_ReadBases */
    (void)DeleteFileW(L"sav7.sav");
}

/* 0x44E8C..0x44F64: the version-<3 tail of LoadGame. It purges every type-5 item no craft claims,
 * then teleports any active craft item off the map to (150, 250) and starts a subtype-0 mission on it
 * through Geoscape_SpawnMissionSite @ 0x44F64. Both halves need a spawn ctx and a mission ctx, and the
 * load path passed neither until Part 43: the callee's own guard (pufo_strateg.c:1971) turns a missing
 * ctx into a silent return 0, so the whole tail read as a no-op.
 *
 * The save carries version 2 and exactly ONE strateg item per run, because the writer has no version
 * gate -- Save_WriteStrateg always writes nTargetX / nTargetY (113 bytes) while a version-2 reader
 * consumes 105 per item, so a second item would start 8 bytes inside the first. That asymmetry is the
 * original's own and TestStrategChunk pins it; here it is simply avoided. */
static int g_nOldSpawnCalls;
static int g_nOldEventCalls;

static int OldSpawnProbe(StrategItem *pUfo, i32 nMissionSub, void *pUser)
{
    (void)pUfo; (void)nMissionSub; (void)pUser;
    g_nOldSpawnCalls += 1;
    return 0;
}

static int OldEventProbe(void *pUser)
{
    (void)pUser;
    g_nOldEventCalls += 1;
    return 1;
}

static void TestLoadGameOldVersionMissionSeam(void)
{
    static u8 aProduct[17152 + 3200];
    static u8 aGrid[1524];
    static u8 aTiles[4] = { 1, 1, 1, 1 };
    static u8 aSiteState[8];
    static i32 aRadarPct[8];
    static u16 aManuf[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT]; /* 3200 bytes, auManufCol */
    static u16 aResearchCols[144];
    static u16 uResearchEntries;
    static u16 aShotWords[128];
    static Ship aShip;
    static Ship *apShips[1];
    static BaseRec aBase;
    static BaseRec *apBases[1];
    static ObjectRecFull aObj[5];
    static ObjectRecFull *apObj[5];
    static Soldier aSol[1];
    static Soldier *apSol[1];
    static SaveBasesState bases;
    static SaveResearchState research;
    static SaveSmapState smap;
    static StrategSaveState strateg;
    static SaveTimeRecord timeRec;
    static GameTime clock;
    static SmapView view;
    static SmapWorldSize worldSize;
    static PtrArray list;
    static GeoscapeWorld world;
    static ShipCtorEnv shipEnv;
    static PadImage tiles;
    static MissionSiteRec aSites[2];
    static StrategSpawnCtx spawnCtx;
    static SpawnMissionCtx missionCtx;
    static LoadGameHooks hooks;
    static LoadGameEnv env;
    static SaveGameInput in;
    static TactSaveState tact;
    static TeamUnitsState rosters;
    static FlobjsState flobjs;
    static MapGeom geom;
    static TactItem aItem[1];
    static TactItem *apItem[1];
    static i32 nSeed, nPtrA, nPtrB, nListMode, nSoldierCount;
    static i32 nMonthCounter;    /* unk_159648, the roll's row index (0x81AB4) */
    static u16 nAlienPoolAddend; /* 0x1472C8, the roll's early gate (0x81B7C) */
    static u32 nTactVerOut;
    static u8 bTactFlag, bGeoscapeUi, bGameState, bCraftCount, bMissionCount, bAssaultFlag, bSpawnOnce;
    static u16 nUiScreen;
    DatFile dat;

    printf("LoadGame @ 0x44E8C..0x44F64: the version-2 purge, teleport and mission start\n");

    memset(&tiles, 0, sizeof(tiles));
    tiles.uWidth = 2;
    tiles.uHeight = 2;
    tiles.pData = aTiles;

    /* The purge's replacement roll reads the two cells through the context (0x82B80 -> 0x7E484), so
     * this fixture holds them as cells. Month 0 keeps the weight rows at row 0 and the gate at 99
     * keeps 0x81B7C's class cap off, which is what the pinned picks below assume. */
    nMonthCounter = 0;
    nAlienPoolAddend = 99;

    memset(&spawnCtx, 0, sizeof(spawnCtx));
    spawnCtx.nZoomMode = 0;
    spawnCtx.pnDifficulty = &nMonthCounter;
    spawnCtx.pnEarlyGate = &nAlienPoolAddend;
    spawnCtx.pnSeed = &nSeed;
    spawnCtx.pfnSpawnMission = OldSpawnProbe;
    spawnCtx.pfnProcessEvents = OldEventProbe;

    memset(&missionCtx, 0, sizeof(missionCtx));
    missionCtx.pList = &list;
    missionCtx.pnSeed = &nSeed;
    missionCtx.pView = &view;
    missionCtx.pTilemap = &tiles;      /* the shell supplies pW->sets[0].pTiles, the unk_186B74 grid */
    missionCtx.apBases = apBases;
    missionCtx.nBaseCount = 1;
    missionCtx.apSites = aSites;
    missionCtx.nSiteCount = 2;
    missionCtx.abFacilityGrid = aGrid;
    missionCtx.pnRadarPulseTimer = &aRadarPct[7];
    missionCtx.pWorld = &world;
    missionCtx.nGameTotalSec = 31536000;

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnRenderFrame = LoadRenderProbe;
    hooks.pfnStartUiShutdown = LoadShutdownProbe;
    hooks.pfnGeoscapeInitUi = LoadInitUiProbe;
    hooks.pfnTactShutdown = LoadTactShutProbe;
    hooks.pfnGeoscapeUiFreeAll = LoadFreeAllProbe;
    hooks.pfnTactInitMission = LoadTactInitProbe;
    hooks.pUser = NULL;

    memset(&aShip, 0, sizeof(aShip));
    aShip.bId = 9;
    aShip.bCraftType = 2;
    aShip.bHomeBase = 0;
    aShip.bIntercepting = 0;   /* nothing claims the type-5 item, so the purge is free to drop it */

    memset(&aBase, 0, sizeof(aBase));
    aBase.bUnk4 = 4;
    aBase.bSlot = 0;
    aBase.nUnk8 = 120;         /* the home base's coordinates, which a created item gets */
    aBase.nUnk12 = 80;
    apBases[0] = &aBase;

    memset(aObj, 0, sizeof(aObj));
    for (int i = 0; i < 5; ++i) {
        apObj[i] = &aObj[i];
    }

    /* Run 0 drops an unclaimed type-5 item; run 1 teleports an off-map craft and starts its mission. */
    for (int nRun = 0; nRun < 2; ++nRun) {
        char szSlot[16];
        StrategItem *pItem;

        printf("  run %d: %s\n", nRun, (nRun == 0) ? "the purge" : "the teleport and the mission start");

        snprintf(szSlot, sizeof(szSlot), "sav%d.sav", 8 + nRun);
        (void)DeleteFileA(szSlot);

        memset(&bases, 0, sizeof(bases));
        memset(&research, 0, sizeof(research));
        memset(&smap, 0, sizeof(smap));
        memset(&timeRec, 0, sizeof(timeRec));
        memset(&strateg, 0, sizeof(strateg));
        memset(aProduct, 0, sizeof(aProduct));
        bases.bBaseCount = 1;
        strateg.bSaveVersion = 2;      /* the byte every version-<3 gate reads (0x18655C) */
        strateg.nStrategCount = 1;
        strateg.pnUnk1865A8 = &nPtrA;
        strateg.pnUnk186594 = &nPtrB;
        timeRec.nSlot105 = 31536000u;  /* one year, and the value the mission's epoch must carry */

        apShips[0] = (Ship *)malloc(sizeof(Ship));
        Check(apShips[0] != NULL, "    test ship allocated");
        memcpy(apShips[0], &aShip, sizeof(Ship));

        memset(&list, 0, sizeof(list));
        pItem = (StrategItem *)calloc(1, sizeof(StrategItem));
        Check(pItem != NULL, "    test item allocated");
        pItem->bActive = 1;
        pItem->nDisplayId = 9;         /* matches Ship.bId, so the craft pass finds it */
        if (nRun == 0) {
            pItem->nType = 5;
            pItem->nX = 300;
            pItem->nY = 300;
        } else {
            pItem->nType = 1;
            pItem->nX = 1500;          /* beyond the 1000 bound at 0x44F28 */
            pItem->nY = 40;
            pItem->bVisible = 1;
            pItem->nTargetX = 900;     /* must come out as 0: the mission start clears both targets */
            pItem->nTargetY = 100;
        }
        PtrArray_Add(&list, pItem);

        memset(&in, 0, sizeof(in));
        memset(&tact, 0, sizeof(tact));
        memset(&rosters, 0, sizeof(rosters));
        memset(&flobjs, 0, sizeof(flobjs));
        memset(&geom, 0, sizeof(geom));
        memset(aItem, 0, sizeof(aItem));
        memset(aSol, 0, sizeof(aSol));
        apItem[0] = &aItem[0];
        apSol[0] = &aSol[0];
        geom.nDimX = 1;
        geom.nDimY = 1;
        in.pszAppDir = "";
        in.nSlot = 8 + nRun;
        in.bTactFlag = 0;
        in.pBases = &bases;
        in.apBases = (const BaseRec *const *)apBases;
        in.pProductBlock = aProduct;
        in.pResearch = &research;
        in.apShips = (const Ship *const *)apShips;
        in.nShipCount = 1;
        in.pSmap = &smap;
        in.pStrateg = &strateg;
        in.pStrategList = &list;
        in.pTime = &timeRec;
        in.apSoldiers = apSol;
        in.nSoldierCount = 1;
        in.apTactItems = apItem;
        in.nTactItemCount = 1;
        in.apObjects = (ObjectRecFull *const *)apObj;
        in.nObjectCount = 5;
        in.pTact = &tact;
        in.pRosters = &rosters;
        in.pFlobjs = &flobjs;
        in.pMap = &geom;
        in.pfnLookup = MapTestLookup;
        in.pfnRenderFrame = RenderFrameProbe;

        DatFile_ctor(&dat);
        Check(SaveGame(&dat, &in), "    slot written as a version-2 save");
        DatFile_Close(&dat);

        /* Fresh state again: the readers rebuild the list, the ship slot and the base slot. */
        memset(aGrid, 0, sizeof(aGrid));
        memset(aSiteState, 0, sizeof(aSiteState));
        memset(aRadarPct, 0, sizeof(aRadarPct));
        memset(aManuf, 0, sizeof(aManuf));
        memset(aResearchCols, 0, sizeof(aResearchCols));
        memset(aShotWords, 0xEE, sizeof(aShotWords));
        memset(&world, 0, sizeof(world));
        memset(&view, 0, sizeof(view));
        memset(&clock, 0, sizeof(clock));
        memset(&shipEnv, 0, sizeof(shipEnv));
        memset(&bases, 0, sizeof(bases));
        bases.bBaseCount = 0;
        memset(&strateg, 0, sizeof(strateg));
        strateg.bSaveVersion = 2;
        strateg.nStrategCount = 1;
        missionCtx.pList = &list;
        world.apCraft = (Ship *const *)apShips;
        world.nShipCount = 1;
        world.apSiteState = aSiteState;
        world.apRadarPct = aRadarPct;
        world.pTime = &clock;
        shipEnv.apObjects = apObj;
        shipEnv.nObjectCount = 5;

        bTactFlag = 0;
        bGeoscapeUi = 1;
        bGameState = 0;
        bCraftCount = 1;
        bMissionCount = 99;
        bAssaultFlag = 0;
        bSpawnOnce = 0;                /* 0x162028: 0 skips the refill RemoveAt would run */
        nUiScreen = 3;
        nListMode = 0;
        nSoldierCount = 0;
        nSeed = 5;
        uResearchEntries = 0;

        memset(&env, 0, sizeof(env));
        env.pszAppDir = "";
        env.nSlot = 8 + nRun;
        env.pbTactFlag = &bTactFlag;
        env.pbGeoscapeUi = &bGeoscapeUi;
        env.pbGameState = &bGameState;
        env.puUiScreen = &nUiScreen;
        env.pnSoldierCount = &nSoldierCount;
        env.pnListMode = &nListMode;
        env.pbMissionCount = &bMissionCount;
        env.pbCraftSelectAssaultFlag = &bAssaultFlag;
        env.pBases = &bases;
        env.apBases = apBases;
        env.nOldBaseCount = 0;
        env.pProductBlock = aProduct;
        env.pResearch = &research;
        env.apCraft = apShips;
        env.pbCraftCount = &bCraftCount;
        env.pShipEnv = &shipEnv;
        env.pSmap = &smap;
        env.pView = &view;
        env.paZoomSets = NULL;
        env.pWorldSize = &worldSize;
        env.pStrateg = &strateg;
        env.pStrategList = &list;
        env.pnSeed = &nSeed;
        env.apSoldiers = apSol;
        env.nSoldierCapacity = 1;
        env.apObjects = apObj;
        env.nObjectCount = 5;
        env.pnTactVerOut = &nTactVerOut;
        env.pWorld = &world;
        env.abFacilityGrid = aGrid;
        env.apResearchCols = aResearchCols;
        env.apManufCols = aManuf;
        env.puResearchEntries = &uResearchEntries;
        env.puShotObjectWords = (u16 *)aShotWords;
        env.pTimeRec = &timeRec;
        env.pTime = &clock;
        env.pSpawnCtx = &spawnCtx;
        env.pMissionCtx = &missionCtx;
        env.pbSpawnOnceFlag = &bSpawnOnce;
        env.pHooks = &hooks;

        g_nOldSpawnCalls = 0;
        g_nOldEventCalls = 0;

        memset(&list, 0, sizeof(list));
        {
            StrategItem *pFill = (StrategItem *)calloc(1, sizeof(StrategItem));
            pFill->nType = 1;
            pFill->nDisplayId = 99;    /* a placeholder the read replaces */
            PtrArray_Add(&list, pFill);
        }

        DatFile_ctor(&dat);
        CheckEqI32(LoadGame(&dat, &env), 1, "    LoadGame == 1");
        DatFile_Close(&dat);

        if (nRun == 0) {
            /* 0x44E8C: the purge dropped the unclaimed type-5 item. The one item left is the craft
             * item the per-craft pass created at the home base (0x44FC0..0x45014), which proves the
             * list was really shortened rather than left alone. */
            CheckEqI32(list.nCount, 1, "    the type-5 item is gone and the craft pass added one");
            {
                StrategItem *pGot = (StrategItem *)list.apItems[0];
                Check(pGot != NULL && pGot->nType == 1, "    and the survivor is the created craft");
                if (pGot != NULL) {
                    /* The id the craft pass handed StrategList_AddItem (0x45004) is the ship's own bId,
                     * which lands in nDisplayId because nType is 1. The base coordinates are not checked
                     * here: a version-2 base record re-lays-out its eight bytes, and Save_ReadBases' own
                     * tests pin that. */
                    CheckEqI32(pGot->nDisplayId, 9, "    the created item carries the ship's display id");
                    CheckEqI32(pGot->bActive, 0, "    and the pass creates it inactive");
                }
            }
        } else {
            StrategItem *pGot = (StrategItem *)list.apItems[0];
            Check(list.nCount == 1 && pGot != NULL && pGot->nType == 1,
                  "    the craft item is the one the read rebuilt");
            if (pGot == NULL) {
                goto oldNext;
            }
            /* 0x44F3C / 0x44F50: off the map means (150, 250). */
            CheckEqI32(pGot->nX, 150, "    the off-map craft was teleported to x 150");
            CheckEqI32(pGot->nY, 250, "    and to y 250");

            /* 0x44F64: the mission start. Subtype 0, and a subtype-0 start lands on nPhase 3. */
            CheckEqI32(pGot->nMissionSub, 0, "    a subtype-0 mission was started on it");
            CheckEqI32(pGot->nPhase, 3, "    a subtype-0 start sets nPhase 3, not 4");
            CheckEqI32(pGot->nTargetX, 0, "    both travel targets were cleared (900 became 0)");
            CheckEqI32(pGot->nTargetY, 0, "    including the y the save carried");

            /* StrategEntry_InitTravel ran, with the teleport as its start and the ctx's clock total as
             * its epoch. The destination comes from two draws, so only its range is pinned. */
            CheckEqI32(pGot->nStartX, 150, "    the travel starts from the teleport, not from 1500");
            CheckEqI32(pGot->nStartY, 250, "    on both axes");
            Check(pGot->nDestX >= 20 && pGot->nDestX <= 779,
                  "    rand(0x2F8)+20 put the destination inside the 800-wide map");
            Check(pGot->nDestY >= 20 && pGot->nDestY <= 379,
                  "    rand(0x168)+20 put it inside the 400-tall map");
            CheckEqI32(pGot->nTravelEpochSec, 31536000,
                       "    nTravelEpochSec is the ctx's clock total (apLines[105], 0x7FB6C)");
            Check(pGot->dFracX == 0.0 && pGot->dFracY == 0.0, "    the travel fraction was reset");
        }

        /* 0x82B3C: the refill RemoveAt would run is gated on 0x162028, held at 0 here, so neither the
         * spawn hook nor its event tail call ran. The refill itself is pinned by TestRemoveAtAndPurge. */
        CheckEqI32(bSpawnOnce, 0, "    the one-shot replacement byte was read as 0 and stayed 0");
        CheckEqI32(g_nOldSpawnCalls, 0, "    so no replacement craft was rolled");
        CheckEqI32(g_nOldEventCalls, 0, "    and the refill's event tail call never ran");

oldNext:
        for (int i = 0; i < list.nCount; ++i) {
            free(list.apItems[i]);
        }
        free(apShips[0]);   /* allocated by Save_ReadShips */
        free(apBases[0]);   /* allocated by Save_ReadBases */
        (void)DeleteFileA(szSlot);
    }
}

/* Save_ReadTGameData @ 0x44248. */
static int  g_nTactSpriteLoads;
static int  g_nSprCalls;
static int  g_nTactBuildIndex;
static int  g_nRandCalls;
static int  g_nApplyCalls;
static int  g_nSprCallsAtApply;
static i32  g_aSpriteSets[8];
static u32  g_aRandMax[8];

/* The reader's 23 fields land live in the original's Map block before 0x4444C's Font_LoadTactSprites;
 * recording g_nSprCalls here proves the port applies them at the same point. */
static void ApplyStateProbe(const TactSaveState *pSt, void *pUser)
{
    (void)pSt;
    (void)pUser;
    ++g_nApplyCalls;
    g_nSprCallsAtApply = g_nSprCalls;
}

static void LoadTactSpritesProbe(void *pUser)
{
    (void)pUser;
    g_nTactSpriteLoads++;
}

/* The real Font_LoadSpr @ 0x461d8 grows fontSys.nTotalSprFrames, and 0x44474 reads that counter fresh
 * on every pass of the sprite-set loop -- so the probe has to move it, or the per-pass read is invisible. */
static bool LoadSprProbe(i32 nSpriteSet, void *pUser)
{
    if (g_nSprCalls < 8) {
        g_aSpriteSets[g_nSprCalls] = nSpriteSet;
    }
    g_nSprCalls++;
    ((PufoFont *)pUser)->nTotalSprFrames = (u16)(((PufoFont *)pUser)->nTotalSprFrames + 0x10u);
    return true;
}

static void BuildIndexProbe(void *pUser)
{
    (void)pUser;
    g_nTactBuildIndex++;
}

static u32 RandProbe(u32 nMax, void *pUser)
{
    (void)pUser;
    if (g_nRandCalls < 8) {
        g_aRandMax[g_nRandCalls] = nMax;
    }
    g_nRandCalls++;
    return nMax == 0x28u ? 7u : 3u;
}

static void TestReadTGameData(void)
{
    static u8 bBlobFlag;
    static TactLoadHooks hooks;
    DatFile dat;
    TactSaveState w, r;
    TactResetState reset;
    TeamUnitsState rosters;
    FlobjsState flobjs;
    FlobjsReadState read;
    MapGeom geom;
    PufoFont font;
    Soldier aSol[2];
    Soldier *apSol[2];
    Soldier *apXcom[PUFO_SAVE_ROSTER_MAX];
    Soldier *apEnemy[PUFO_SAVE_ROSTER_MAX];
    Soldier *apCiv[PUFO_SAVE_CIV_MAX];
    Soldier *apHidden[PUFO_SAVE_ROSTER_MAX];
    ObjectRecFull aObj[1];
    ObjectRecFull *apObj[1];
    TactItem aItem[1];
    TactItem *apItem[1];
    i32 nItemCount;

    printf("Save_ReadTGameData\n");

    memset(&w, 0, sizeof(w));
    memset(&r, 0, sizeof(r));
    memset(&font, 0, sizeof(font));
    w.bUnk15110 = 0x11;
    w.nUnk1510C = 0x01020304;
    w.abUnk15111[0] = 0x21;
    w.abUnk15111[1] = 0x22;
    w.abUnk15111[2] = 0x23;
    w.nMapLayers = 4;
    w.bUnk15115 = 0x25;
    w.abSpriteSet[0] = 0;
    w.abSpriteSet[1] = 5;
    w.abSpriteSet[2] = PUFO_SPRITE_SET_NONE;   /* 23: this slot loads nothing */
    w.abSpriteSet[3] = 7;
    w.nMissionType = 9;
    w.bMissionMode = 1;
    w.bUnk1511E = 1;
    w.anUnk15188[0] = (i32)0xAAAAAAAA;
    w.anUnk15188[1] = 1;
    w.anUnk15188[2] = 2;
    w.anUnk15188[3] = 3;
    w.abUnk15198[0] = 1;
    w.abUnk15198[1] = 2;
    w.abUnk15198[2] = 3;
    w.nUnk151A0 = 0x11223344;
    w.anCraftTile[0] = 7;
    w.anCraftTile[1] = 8;
    w.anCraftTile[2] = 9;
    w.nUnk138E6C = 1234;
    w.nUnk138E70 = 5678;

    memset(aSol, 0, sizeof(aSol));
    for (i32 i = 0; i < 2; ++i) {
        apSol[i] = &aSol[i];
        aSol[i].nSaveId = i;
    }
    memset(apXcom, 0, sizeof(apXcom));
    memset(apEnemy, 0, sizeof(apEnemy));
    memset(apCiv, 0, sizeof(apCiv));
    memset(apHidden, 0, sizeof(apHidden));
    apXcom[0] = &aSol[0];
    apEnemy[0] = &aSol[1];
    memset(&rosters, 0, sizeof(rosters));
    rosters.nXcomCount = 1;
    rosters.nEnemyCount = 1;
    rosters.apXcomUnits = apXcom;
    rosters.apEnemyUnits = apEnemy;
    rosters.apCivilians = apCiv;
    rosters.apHiddenUnits = apHidden;
    rosters.pSelUnit = &aSol[0];

    memset(aObj, 0, sizeof(aObj));
    memset(aItem, 0, sizeof(aItem));
    apObj[0] = &aObj[0];
    apItem[0] = &aItem[0];
    aItem[0].pObj = &aObj[0];
    aItem[0].nRefId = 0;
    aItem[0].nIndex = 12;
    memset(&flobjs, 0, sizeof(flobjs));
    flobjs.apTactItems = (const TactItem *const *)apItem;
    flobjs.nTactItemCount = 1;
    flobjs.apObjects = (const ObjectRecFull *const *)apObj;
    flobjs.nObjectCount = 1;
    geom.nDimX = 1;
    geom.nDimY = 1;

    Check(OpenWrite("test_tload.tmp", &dat, 9), "  container opened");
    Check(Save_WriteTGameData(&dat, &w, &rosters, &flobjs, &geom, MapTestLookup, NULL),
          "  the tactical half written");
    DatFile_Close(&dat);

    /* Everything the reset touches starts dirty, so a clear is observable. */
    memset(&reset, 0xEE, sizeof(reset));
    /* fontSys @ 0x158268, dirty so a write is observable: the four alien bases and the running counter. */
    font.anAlienSprBase[0] = 0x1111;
    font.anAlienSprBase[1] = 0x2222;
    font.anAlienSprBase[2] = 0x3333;
    font.anAlienSprBase[3] = 0x4444;
    font.nTotalSprFrames   = 0x0077;
    bBlobFlag = 1;
    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnLoadTactSprites = LoadTactSpritesProbe;
    hooks.pfnLoadSpr = LoadSprProbe;
    hooks.pfnBuildIndex = BuildIndexProbe;
    hooks.pfnRandU16 = RandProbe;
    hooks.pfnApplyState = ApplyStateProbe;
    hooks.pUser = &font;
    hooks.pFont = &font;
    hooks.pbUnk157198Flag = &bBlobFlag;
    g_nTactSpriteLoads = 0;
    g_nSprCalls = 0;
    g_nTactBuildIndex = 0;
    g_nRandCalls = 0;
    g_nApplyCalls = 0;
    g_nSprCallsAtApply = -1;

    memset(&read, 0, sizeof(read));
    read.nWideCountFlag = 1;
    read.pnTactItemCount = &nItemCount;
    read.apTactItems = apItem;
    read.apObjects = apObj;
    read.nObjectCount = 1;
    read.apSoldiers = apSol;
    read.nSoldierCount = 2;
    nItemCount = 1;

    Check(OpenRead("test_tload.tmp", &dat), "  reopened for reading");
    Check(Save_ReadTGameData(&dat, &r, &rosters, &read, &geom, MapTestLookup, NULL,
                             &reset, &hooks), "  read back");
    DatFile_Close(&dat);

    CheckEqI32(r.bUnk15110, 0x11, "  0x15110");
    CheckEqI32(r.nUnk1510C, 0x01020304, "  0x1510C");
    CheckEqI32(r.abUnk15111[2], 0x23, "  0x15113, Map_nCraftIndex");
    CheckEqI32(r.nMapLayers, 4, "  the layer count");
    CheckEqI32(r.nMissionType, 9, "  nMissionType");
    CheckEqI32(r.bMissionMode, 1, "  0x1511D, the mission mode");
    CheckEqI32(r.bUnk1511E, 1, "  0x1511E");
    CheckEqI32(r.anUnk15188[0], (i32)0xAAAAAAAA, "  0x15188");
    CheckEqI32(r.nUnk151A0, 0x11223344, "  0x151A0");
    CheckEqI32(r.anCraftTile[2], 9, "  the third craft tile, one byte on disk");

    CheckEqI32(g_nApplyCalls, 1, "  the 23 fields are applied to the caller's cells once, right after the reads");
    CheckEqI32(g_nSprCallsAtApply, 0, "  and applied before Font_LoadTactSprites or any sprite set runs (0x4444C follows the reads)");
    CheckEqI32(g_nTactSpriteLoads, 1, "  Font_LoadTactSprites runs once, first");
    CheckEqI32(g_nSprCalls, 3, "  three sprite sets loaded, the 23 slot is skipped");
    CheckEqI32(g_aSpriteSets[0], 0, "  xcom_0");
    CheckEqI32(g_aSpriteSets[1], 5, "  xcom_5");
    CheckEqI32(g_aSpriteSets[2], 7, "  xcom_7");
    CheckEqI32(g_nTactBuildIndex, 1, "  Font_BuildIndex runs once, after the sets");
    CheckEqI32(font.anAlienSprBase[0], 0x0077, "  0x44474/0x4447C: anAlienSprBase[0] takes the counter as it stands before xcom_0 loads");
    CheckEqI32(font.anAlienSprBase[1], 0x0087, "  [1] takes it AFTER xcom_0 moved it: 0x44474 re-reads fontSys_nTotalSprFrames every pass");
    CheckEqI32(font.anAlienSprBase[2], 0x3333, "  [2] untouched: its set byte is 23 and 0x44470 skips the whole body");
    CheckEqI32(font.anAlienSprBase[3], 0x0097, "  [3] one bump later again, after xcom_5");
    CheckEqI32(font.nTotalSprFrames, 0x00A7, "  three loads moved the counter three times");

    CheckEqI32(rosters.nXcomCount, 1, "  Save_ReadTeamUnits ran");
    CheckEqI32(nItemCount, 1, "  Save_ReadFlobjs ran");
    CheckEqI32(aItem[0].nIndex, 12, "  and filled the item");

    CheckEqI32(reset.bPlayerInputEnabled, 1, "  bPlayerInputEnabled is SET to 1");
    CheckEqI32(reset.aUiSlot[4], 200, "  apUiSlotsA[4] becomes the value 200");
    CheckEqI32(reset.aUiSlot[10], 10, "  apUiSlotsA[10] becomes the value 10");
    CheckEqI32(reset.aUiSlot[0], 0, "  the other slots are cleared");
    CheckEqI32(reset.aUiSlot[8], 0, "  including slot 8, written as two halves");
    CheckEqI32(reset.nMultiTargetCount, 0, "  nMultiTargetCount cleared");
    CheckEqI32(reset.nAnimPhase, 0, "  nAnimPhase cleared");
    CheckEqI32(reset.abFire[4], 0, "  the fire-status dword cleared");
    CheckEqI32(reset.bBattleEnded, 0, "  bBattleEnded cleared");
    CheckEqI32(reset.nUiScreen, 0, "  nUiScreen cleared");
    CheckEqI32(bBlobFlag, 0, "  and unk_157198's flag is cleared through the pointer");

    CheckEqI32(r.nUnk138E6C, 1234, "  v124.dat present, so the stored dwords are used");
    CheckEqI32(r.nUnk138E70, 5678, "  both of them");
    CheckEqI32(g_nRandCalls, 0, "  and nothing is rolled");

    /* The same load with v124.dat missing: the two dwords are ROLLED. The tactical
     * half is written chunk by chunk here, with the 43 tgamedata bytes transcribed in
     * the original's write order. */
    static const u8 aTact43[43] = {
        0x11,
        0x04, 0x03, 0x02, 0x01,
        0x21, 0x22, 0x23,
        0x04,
        0x25,
        0x00, 0x05, 0x17, 0x07,
        0x09,
        0x01,
        0x01,
        0xAA, 0xAA, 0xAA, 0xAA, 0x01, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x01, 0x02, 0x03,
        0x44, 0x33, 0x22, 0x11,
        0x07, 0x08, 0x09
    };

    Check(OpenWrite("test_tload2.tmp", &dat, 8), "  container without v124.dat");
    Check(DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_TGAMEDATA), "  tgamedata");
    Check(DatFile_Write(&dat, aTact43, 43), "  43 bytes in the original's order");
    Check(Save_WriteTeamUnits(&dat, &rosters), "  teamunits, aliens, diedsolds, hiddensold");
    Check(Save_WriteFlobjs(&dat, flobjs.apTactItems, flobjs.nTactItemCount,
                           flobjs.apObjects, flobjs.nObjectCount), "  flobjs");
    Check(Save_WriteMap(&dat, &geom, MapTestLookup, NULL), "  and map");
    DatFile_Close(&dat);

    memset(&r, 0, sizeof(r));
    memset(&reset, 0xEE, sizeof(reset));
    g_nRandCalls = 0;
    Check(OpenRead("test_tload2.tmp", &dat), "  reopened");
    Check(Save_ReadTGameData(&dat, &r, &rosters, &read, &geom, MapTestLookup, NULL,
                             &reset, &hooks), "  read back");
    DatFile_Close(&dat);
    CheckEqI32(g_nRandCalls, 2, "  two rolls when v124.dat is absent");
    CheckEqI32(g_aRandMax[0], 0x28u, "  the first is rand(40)");
    CheckEqI32(g_aRandMax[1], 0x14u, "  the second is rand(20)");
    CheckEqI32(r.nUnk138E6C, 17, "  first dword is rand + 10");
    CheckEqI32(r.nUnk138E70, 3, "  second is the raw roll");

    remove("test_tload.tmp");
    remove("test_tload2.tmp");
}

/* Soldiers_Reload @ 0x430cc.
 *
 * It is the allocation half of a load: free the roster, read the saved count, and build that many empty
 * soldiers. The bodies come from Save_ReadSoldiers afterwards, so the two are tested together - a reload
 * followed by a read has to land the written values in the objects the reload allocated. */
static void TestSoldiersReload(void)
{
    static DatFile dat;
    static Soldier aWritten[3];
    Soldier       *apRoster[8];
    i32            nCount = 2;
    i32            nRead = 0;

    printf("Soldiers_Reload\n");

    remove("test_reload.tmp");
    DatFile_ctor(&dat);
    Check(DatFile_Open(&dat, "test_reload.tmp", PUFO_DAT_CREATE, 4), "  container opened");
    for (i32 i = 0; i < 3; ++i) {
        FillSoldier(&aWritten[i]);
        aWritten[i].nUnk440 = 500 + i;
    }
    {
        Soldier        *apWrite[3];
        Soldier        *apXcom[30];
        TeamUnitsState  rosters;

        memset(apXcom, 0, sizeof(apXcom));
        memset(&rosters, 0, sizeof(rosters));
        apWrite[0] = &aWritten[0];
        apWrite[1] = &aWritten[1];
        apWrite[2] = &aWritten[2];
        apXcom[0] = apWrite[0];
        apXcom[1] = apWrite[1];
        apXcom[2] = apWrite[2];
        rosters.nXcomCount  = 3;
        rosters.apXcomUnits = apXcom;
        Check(Save_WriteSoldiers(&dat, apWrite, 3, 1, &rosters), "  three soldiers written");
    }
    DatFile_Close(&dat);

    /* A roster of two heap soldiers standing in for a previous mission. Soldiers_Reload frees what the count
     * says is live, so the test must not free them again. */
    memset(apRoster, 0, sizeof(apRoster));
    apRoster[0] = (Soldier *)malloc(sizeof(Soldier));
    apRoster[1] = (Soldier *)malloc(sizeof(Soldier));
    apRoster[0]->nUnk440 = 111;
    apRoster[1]->nUnk440 = 222;

    DatFile_ctor(&dat);
    Check(DatFile_Open(&dat, "test_reload.tmp", PUFO_DAT_READ, 0), "  reopened for read");

    Check(Soldiers_Reload(&dat, apRoster, &nCount, 8), "  reload succeeded");
    CheckEqI32(nCount, 3, "  the roster size comes from the saved count");
    Check(apRoster[0] != NULL && apRoster[1] != NULL && apRoster[2] != NULL, "  three soldiers allocated");
    Check(apRoster[3] == NULL, "  nothing is allocated past the count");

    /* Freshly constructed, not loaded: every field the writer set is still zero here. */
    CheckEqI32(apRoster[0]->nUnk440, 0, "  a reloaded soldier starts empty");
    CheckEqI32(apRoster[0]->bFlags, 0, "  flags are zero before the body read");

    /* The pairing the original relies on: reload allocates, then the read fills the same objects. */
    Check(Save_ReadSoldiers(&dat, apRoster, &nRead, 8, NULL, 0), "  bodies read into the reloaded roster");
    CheckEqI32(nRead, 3, "  three bodies read");
    CheckEqI32(apRoster[0]->nUnk440, 500, "  first soldier landed in the reloaded object");
    CheckEqI32(apRoster[2]->nUnk440, 502, "  and so did the last");
    CheckEqI32(apRoster[0]->bFlags, 0x3F, "  flags round-trip through the reloaded object");
    DatFile_Close(&dat);

    for (i32 i = 0; i < 3; ++i) {
        free(apRoster[i]);
    }

    /* The capacity clamp. The original has none; the port stops at the caller's array size. */
    memset(apRoster, 0, sizeof(apRoster));
    nCount = 0;
    DatFile_ctor(&dat);
    Check(DatFile_Open(&dat, "test_reload.tmp", PUFO_DAT_READ, 0), "  reopened again");
    Check(Soldiers_Reload(&dat, apRoster, &nCount, 2), "  reload into a smaller array succeeded");
    CheckEqI32(nCount, 2, "  the count is clamped to the array the caller owns");
    Check(apRoster[0] != NULL && apRoster[1] != NULL, "  the two slots that fit are filled");
    Check(apRoster[2] == NULL, "  and the third is left alone");
    DatFile_Close(&dat);
    for (i32 i = 0; i < 2; ++i) {
        free(apRoster[i]);
    }

    /* The free at 0x430D8 runs before the chunk is even looked at, and the original leaves the
     * count pointing at memory it just freed. The port nulls the slots it freed, so a reload that
     * fails after the free still leaves the roster safe. This is the part of the function that a
     * successful reload cannot show. */
    memset(apRoster, 0, sizeof(apRoster));
    apRoster[0] = (Soldier *)malloc(sizeof(Soldier));
    apRoster[1] = (Soldier *)malloc(sizeof(Soldier));
    apRoster[0]->nUnk440 = 111;
    apRoster[1]->nUnk440 = 222;
    nCount = 2;
    DatFile_ctor(&dat);
    Check(DatFile_Open(&dat, "test_reload_none.tmp", PUFO_DAT_CREATE, 4), "  empty container opened");
    Check(!Soldiers_Reload(&dat, apRoster, &nCount, 8), "  reload refuses a container with no soldiers chunk");
    Check(apRoster[0] == NULL && apRoster[1] == NULL, "  the freed slots are nulled, not left dangling");
    CheckEqI32(nCount, 2, "  the count is left as the caller had it when the chunk read fails");
    DatFile_Close(&dat);
    remove("test_reload_none.tmp");

    remove("test_reload.tmp");
}

/* Save_ReadSoldiers' two resolves: the soldier's three cell bytes become a cell POINTER (0x419DC) for
 * a soldier flagged into the mission, and are read-and-dropped for one that is not (0x419F4); the
 * `1_24scb` u32 becomes a pointer into the roster that was just reloaded (0x42320). Both need
 * `stactflg` to have run first -- that is why the original reads it at 0x416D0, before the bodies at
 * 0x417BC -- and the chunk is optional, in which case every soldier takes LOBYTE(Map) (0x41790/0x4179C). */
static void TestSoldiersCellLink(void)
{
    static DatFile dat;
    static Soldier aWritten[3];
    static Soldier aRead[3];
    Soldier       *apWrite[3];
    Soldier       *apRoster[4];
    i32            nCount = 0;

    printf("Save_ReadSoldiers: the cell pointer, the 1_24scb link, and stactflg's order\n");

    memset(g_aMapCells, 0, sizeof(g_aMapCells));
    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nY = 0; nY < 10; ++nY) {
            for (i32 nX = 0; nX < 10; ++nX) {
                g_aMapCells[nLayer][nY][nX].nX = (u8)nX;
                g_aMapCells[nLayer][nY][nX].nY = (u8)nY;
                g_aMapCells[nLayer][nY][nX].nZ = (u8)nLayer;
            }
        }
    }

    remove("test_soldcell.tmp");
    for (i32 i = 0; i < 3; ++i) {
        FillSoldier(&aWritten[i]);
        aWritten[i].nSaveId = i;      /* the writer stores the linked soldier's nSaveId (0x42E64) */
        apWrite[i] = &aWritten[i];
    }
    aWritten[0].pCell = MapTestLookup(7, 9, 0, NULL);
    aWritten[2].pCell = MapTestLookup(0, 1, 2, NULL);
    aWritten[2].auStat[8] = 0;                     /* 0x4229C defaults it to 10 */
    aWritten[0].pLinked1040 = &aWritten[2];        /* stored as its nSaveId, 2 */

    /* Soldier 1 stands in NO roster, and that is what flags it out: the writer's own scans
     * (0x423D4..0x424B4) decide bInTact, and the record's cell arm follows the flag. */
    {
        Soldier        *apXcom[30];
        TeamUnitsState  rosters;

        memset(apXcom, 0, sizeof(apXcom));
        memset(&rosters, 0, sizeof(rosters));
        apXcom[0] = &aWritten[0];
        apXcom[1] = &aWritten[2];
        rosters.nXcomCount  = 2;
        rosters.apXcomUnits = apXcom;

        Check(OpenWrite("test_soldcell.tmp", &dat, 4), "  container opened");
        Check(Save_WriteSoldiers(&dat, apWrite, 3, 1, &rosters), "  three soldiers written");
    }
    DatFile_Close(&dat);

    memset(aRead, 0, sizeof(aRead));
    apRoster[0] = &aRead[0];
    apRoster[1] = &aRead[1];
    apRoster[2] = &aRead[2];
    apRoster[3] = NULL;

    Check(OpenRead("test_soldcell.tmp", &dat), "  reopened");
    {
        SoldierRecordEnv arms;

        memset(&arms, 0, sizeof(arms));
        arms.pfnCell = MapTestLookup;
        Check(Save_ReadSoldiers(&dat, apRoster, &nCount, 4, &arms, 0), "  read back");
    }
    CheckEqI32(nCount, 3, "  three bodies");
    Check(apRoster[0]->pCell == MapTestLookup(7, 9, 0, NULL),
          "the flagged soldier gets a cell POINTER, not three loose bytes (0x419DC)");
    Check(apRoster[0]->pCell != NULL && apRoster[0]->pCell->nX == 7 && apRoster[0]->pCell->nY == 9
              && apRoster[0]->pCell->nZ == 0,
          "and the pointer is the cell its three coordinates name");
    Check(apRoster[1]->pCell == NULL,
          "the flagged-out soldier's three bytes are read and dropped (0x419F4) -- the flag was live before the body read, which is why stactflg runs first");
    Check(apRoster[2]->pCell == MapTestLookup(0, 1, 2, NULL), "the layer byte reaches the resolve too");
    CheckEqI32(apRoster[0]->bInTact, 1, "stactflg's byte round-trips into bInTact");
    CheckEqI32(apRoster[1]->bInTact, 0, "and the two arms stay distinguishable");
    Check(apRoster[0]->pLinked1040 == apRoster[2],
          "1_24scb resolves as an index into the roster the read just filled (0x42320)");
    Check(apRoster[1]->pLinked1040 == NULL && apRoster[2]->pLinked1040 == NULL,
          "0x7FFF means no link");
    CheckEqI32(apRoster[2]->auStat[8], 10, "auStat[8] reads 10 when the record carries 0 (0x4229C)");
    CheckEqI32(apRoster[0]->auStat[8], 108, "and a non-zero value is left alone");
    DatFile_Close(&dat);
    remove("test_soldcell.tmp");

    /* The absent chunk: the original fills every soldier from Map+0x00. The port takes the caller's
     * fallback, and this container has the soldiers chunk and nothing else, so the fallback is the only
     * source the flag can come from. */
    {
        static Soldier aPlain[2];
        Soldier       *apPlain[2];
        u16            uTwo = 2;

        remove("test_soldflag.tmp");
        memset(aPlain, 0, sizeof(aPlain));
        for (i32 i = 0; i < 2; ++i) {
            FillSoldier(&aPlain[i]);
            aPlain[i].bInTact = 0;      /* the record is written with no flag and no cell */
            apPlain[i] = &aPlain[i];
        }
        Check(OpenWrite("test_soldflag.tmp", &dat, 4), "  second container opened");
        Check(DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_SOLDIERS), "  soldiers chunk begun by hand");
        Check(DatFile_Write(&dat, &uTwo, 2), "  count written");
        for (i32 i = 0; i < 2; ++i) {
            Check(Save_WriteSoldier(&dat, apPlain[i], 0), "  soldier written");
        }
        DatFile_Close(&dat);

        memset(aPlain, 0, sizeof(aPlain));
        Check(OpenRead("test_soldflag.tmp", &dat), "  reopened");
        Check(Save_ReadSoldiers(&dat, apPlain, &nCount, 2, NULL, 1), "  read with no stactflg chunk");
        CheckEqI32(nCount, 2, "  two bodies");
        CheckEqI32(aPlain[0].bInTact, 1, "a missing stactflg fills every soldier from the tact flag (0x41790/0x4179C), not from the record");
        CheckEqI32(aPlain[1].bInTact, 1, "and both entries, not just the first");
        Check(aPlain[0].pCell == NULL, "with no cell lookup the flagged bytes are dropped, not guessed at");
        DatFile_Close(&dat);
        remove("test_soldflag.tmp");
    }
}

/* The soldier record's three bInTact arms, both directions.
 *
 * Writer: the 30 multi-target ids, the 27 item ids + 42 meta bytes (0x429dc left hand, 0x429f8 right
 * hand, the four pocket pairs at 0x42a1c / 0x42a50 / 0x42a84 / 0x42ab8, the eight backpack slots at
 * 0x42aec, the nine belt slots at 0x42b58, then the metas at 0x42b9c..0x42c18), and the count + 60
 * spotted ids. Reader: 0x41A70, 0x41D60 and 0x42214, resolving the item ids against the tactical pool
 * `Map.apTactItems @ Map+0x23438` and the roster ids against `Map.apSoldiers @ Map+4504`. An unflagged
 * soldier writes 120 / 150 / 244 filler bytes in the same three places, so a record is the same size
 * either way -- measured here, not assumed. */
static void TestSoldierTacticalArms(void)
{
    static DatFile dat;
    static Soldier aWrite[2];
    static Soldier aRead[2];
    static TactItem aItems[6];
    static TactItem aEmpty;   /* stands for unk_138EA0 @ Map+0x254E8, the shared empty item */
    TactItem       *apPool[6];
    Soldier        *apRoster[2];
    Soldier        *apXcom[2];
    TeamUnitsState  rosters;
    SoldierRecordEnv arms;
    i32   nCount = 0;
    i32   nSizeTact = 0;
    i32   nSizePlain = 0;

    printf("The soldier record's three bInTact arms: ids out, pointers back, same bytes either way\n");

    for (i32 i = 0; i < 6; ++i) {
        memset(&aItems[i], 0, sizeof(aItems[i]));
        aItems[i].nRefId = i;   /* Save_AssignRosterIndices @ 0x42ea0 numbers the pool 0, 1, 2, ... */
        apPool[i] = &aItems[i];
    }
    memset(&aEmpty, 0, sizeof(aEmpty));
    aEmpty.nRefId = (i32)PUFO_SAVE_EMPTY_ITEM_ID;

    memset(aWrite, 0, sizeof(aWrite));
    FillSoldier(&aWrite[0]);
    FillSoldier(&aWrite[1]);
    aWrite[1].bInTact = 0;      /* the size pair below needs one record per arm */
    aWrite[0].pHandL = &aItems[0];
    aWrite[0].pHandR = NULL;                     /* 0x7FFF out, NULL back */
    aWrite[0].aPockets[0].apItem[0] = &aItems[1];
    aWrite[0].aPockets[1].apItem[1] = &aItems[2];
    aWrite[0].aPockets[2].apItem[1] = &aEmpty;   /* the 32000 id */
    aWrite[0].aPockets[3].apItem[0] = &aItems[3];
    aWrite[0].apBackpack[0] = &aItems[4];
    aWrite[0].apBackpack[5] = &aItems[5];        /* written 0x7FFF whatever it holds */
    aWrite[0].apBackpack[6] = &aItems[5];
    aWrite[0].apBackpack[7] = &aItems[5];
    aWrite[0].apBelt[4] = &aItems[5];
    aWrite[0].aPockets[2].abMeta[0] = 0x31;
    aWrite[0].abBackpackMeta[9] = 0x42;
    aWrite[0].abBeltMetaB[8] = 0x55;
    aWrite[0].nMultiTarget = 2;
    aWrite[0].multiOrAnim.apMultiTargets[0] = &aWrite[1];
    aWrite[0].multiOrAnim.apMultiTargets[1] = NULL;
    aWrite[0].nSpottedCount = 3;
    aWrite[0].apSpotted[0] = &aWrite[1];
    aWrite[0].apSpotted[1] = &aWrite[0];
    aWrite[0].apSpotted[2] = NULL;

    /* The size pair: the same soldier, flagged and unflagged, written straight into two containers. */
    remove("test_soldarm1.tmp");
    Check(OpenWrite("test_soldarm1.tmp", &dat, 4), "  flagged container opened");
    Check(DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_SOLDIERS), "  chunk begun");
    Check(Save_WriteSoldier(&dat, &aWrite[0], 1), "  flagged record written");
    DatFile_Close(&dat);
    remove("test_soldarm2.tmp");
    Check(OpenWrite("test_soldarm2.tmp", &dat, 4), "  unflagged container opened");
    Check(DatFile_BeginChunk(&dat, PUFO_SAVE_TAG_SOLDIERS), "  chunk begun");
    Check(Save_WriteSoldier(&dat, &aWrite[1], 0), "  unflagged record written");
    DatFile_Close(&dat);
    nSizeTact  = FileSizeOf(L"test_soldarm1.tmp");
    nSizePlain = FileSizeOf(L"test_soldarm2.tmp");
    Check(nSizeTact > 0 && nSizePlain > 0, "  both container sizes read");
    CheckEqI32((i32)nSizeTact, (i32)nSizePlain,
               "a flagged record and an unflagged one take the same bytes: 120 / 150 / 244 either way");
    remove("test_soldarm1.tmp");
    remove("test_soldarm2.tmp");

    /* The round trip: soldier 0 is in the roster, soldier 1 is not. */
    memset(&rosters, 0, sizeof(rosters));
    memset(apXcom, 0, sizeof(apXcom));
    apXcom[0] = &aWrite[0];
    rosters.nXcomCount  = 1;
    rosters.apXcomUnits = apXcom;
    for (i32 i = 0; i < 2; ++i) {
        aWrite[i].nSaveId = i;   /* the id a roster link stores (0x42E64) */
        apRoster[i] = &aWrite[i];
    }
    remove("test_soldtact.tmp");
    Check(OpenWrite("test_soldtact.tmp", &dat, 4), "  round-trip container opened");
    Check(Save_WriteSoldiers(&dat, apRoster, 2, 1, &rosters), "  two soldiers written");
    DatFile_Close(&dat);

    memset(aRead, 0, sizeof(aRead));
    apRoster[0] = &aRead[0];
    apRoster[1] = &aRead[1];
    memset(&arms, 0, sizeof(arms));
    arms.apTactItems    = apPool;
    arms.nTactItemCount = 6;
    arms.pEmptyItem     = &aEmpty;
    Check(OpenRead("test_soldtact.tmp", &dat), "  reopened");
    Check(Save_ReadSoldiers(&dat, apRoster, &nCount, 2, &arms, 0), "  both records read back");
    CheckEqI32(nCount, 2, "  two bodies");
    CheckEqI32(aRead[0].bInTact, 1, "  the roster member comes back flagged in");
    CheckEqI32(aRead[1].bInTact, 0, "  the soldier in no roster does not");

    Check(aRead[0].pHandL == &aItems[0], "the left hand resolves to its item (0x429dc -> 0x41E58)");
    Check(aRead[0].pHandR == NULL, "the empty right hand stays empty (0x7FFF)");
    Check(aRead[0].aPockets[0].apItem[0] == &aItems[1], "pocket 0 slot 0 resolves");
    Check(aRead[0].aPockets[1].apItem[1] == &aItems[2], "pocket 1 slot 1 resolves (the 12-byte pocket stride)");
    Check(aRead[0].aPockets[3].apItem[0] == &aItems[3], "and so does the fourth pocket pair");
    Check(aRead[0].aPockets[2].apItem[1] == &aEmpty, "the 32000 id names the shared empty item, not pool slot 0");
    Check(aRead[0].apBackpack[0] == &aItems[4], "the backpack slots resolve (0x42aec)");
    Check(aRead[0].apBackpack[7] == &aItems[5], "including the last one");
    Check(aRead[0].apBackpack[5] == NULL && aRead[0].apBackpack[6] == NULL,
          "apBackpack[5] and [6] never survive: the writer forces 0x7FFF (0x42b00..0x42b18) and the reader clears them");
    Check(aRead[0].apBelt[4] == &aItems[5], "the belt slots resolve (0x42b58)");
    Check(aRead[0].apBelt[0] == NULL, "an empty belt slot stays empty");
    CheckEqI32(aRead[0].aPockets[2].abMeta[0], 0x31, "the pocket metas ride along (0x42b9c)");
    CheckEqI32(aRead[0].abBackpackMeta[9], 0x42, "the second half of the backpack metas too (0x42bec)");
    CheckEqI32(aRead[0].abBeltMetaB[8], 0x55, "and the belt metas (0x42c14)");
    Check(aRead[0].multiOrAnim.apMultiTargets[0] == &aRead[1],
          "the multi-target id resolves into the roster the same read just filled");
    Check(aRead[0].multiOrAnim.apMultiTargets[1] == NULL, "and an empty target stays empty");
    Check(aRead[0].multiOrAnim.apMultiTargets[5] == NULL,
          "an entry past nMultiTarget is NULL whatever the record holds (the original's `v7 < nMultiTarget` gate)");
    CheckEqI32(aRead[0].nSpottedCount, 3, "the spotted count reads back");
    Check(aRead[0].apSpotted[0] == &aRead[1] && aRead[0].apSpotted[1] == &aRead[0],
          "the spotted ids resolve into the same roster (0x42214)");
    Check(aRead[0].apSpotted[2] == NULL && aRead[0].apSpotted[30] == NULL,
          "a NULL spotted entry and the 57 past the count both come back NULL");

    Check(aRead[1].pHandL == NULL && aRead[1].apBelt[4] == NULL && aRead[1].apBackpack[0] == NULL,
          "the unflagged record's filler arms leave every slot empty, as the original leaves it");
    CheckEqI32(aRead[1].nSpottedCount, 0, "and its spotted count is not read at all on that arm");
    Check(aRead[1].multiOrAnim.apMultiTargets[0] == NULL,
          "the 0xFF filler bytes are read and dropped on that arm, not indexed with (0x41AD8)");
    DatFile_Close(&dat);
    remove("test_soldtact.tmp");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestSlotPath();
    TestSoldierRoundTrip();
    TestRecordDeterminism();
    TestGameStateChunk();
    TestObjBeforeChunk();
    TestTimeChunk();
    TestAssignRosterIndices();
    TestResearchChunk();
    TestSmapChunk();
    TestProductChunk();
    TestBasesChunk();
    TestShipsChunk();
    TestAllocFlobjs();
    TestFlobjsChunk();
    TestMapChunk();
    TestTeamUnitsChunk();
    TestTGameDataWrite();
    TestReadRmp();
    TestSlotList();
    TestStrategChunk();
    TestSaveGame();
    TestLoadGame();
    TestLoadGameOldVersionMissionSeam();
    TestReadTGameData();
    TestSoldiersReload();
    TestSoldiersCellLink();
    TestSoldierTacticalArms();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
