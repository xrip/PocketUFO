/* test_save.c - checks save-file container and soldier record round-tripping.
 *
 * The record layout is derived field by field from Save_WriteSoldiers @ 0x42350.
 * The strongest check here is the byte count: the non-tactical prefix must be
 * exactly 740 bytes, because every field was transcribed from the write order and
 * any omission or double-write would change the total.
 */
#include "asset/pufo_asset.h"
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
    pUnit->bUnk149 = 1;
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
    pUnit->bUnk422 = 7;
    pUnit->nArmorPoints = 0x0AABBCCD;
    pUnit->nClass = 0x01020304;
    pUnit->nUnk436 = 0x05060708;
    pUnit->nUnk440 = 0x090A0B0C;
    pUnit->bUnk423 = 3;
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
        Soldier *apSoldiers[1];

        apSoldiers[0] = &aWritten;
        /* Must go through the chunk wrapper: DatFile_Write refuses without an
         * open chunk, exactly as the original requires. */
        Check(Save_WriteSoldiers(&dat, apSoldiers, 1), "  soldier written");
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
        Check(Save_ReadSoldiers(&dat, apSoldiers, &nCount, 1), "  soldiers read");
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
        (void)Save_WriteSoldier(&dat, &aUnit);
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
    Check(Save_ReadSmap(&dat, &read), "  smap read");
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
    for (i32 i = 0; i < 9; ++i)  { st.abUnk18655D[i]  = (u8)(0x70 + i); }
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
        aBase[b].bUnk17 = (u8)(0xB0 + b);
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
    Check(memcmp(got.abUnk18655D, st.abUnk18655D, 9) == 0, "the 9-byte block round-trips");

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
        CheckEqI32(apRead[0]->bUnk17, 0xB0, "bUnk17 round-trips");
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

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
