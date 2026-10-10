/* pufo_vistry.c - see pufo_vistry.h for the evidence. */
#include "pufo_vistry.h"

#include <math.h>

/* The single-precision soft-FPU helpers the original imports from coredll:
 * _itos @ 0xa6038, _muls @ 0xa603c, _stoi @ 0xa6040, _divs @ 0xa607c. The `s`
 * suffix is single precision, the `d` suffix (itod, dtoi, muld, addd, gtd) is
 * double. They are not fixed point: the builder computes
 * stoi(muls(divs(itos(15), itos(1)), itos(15))) and stores the result in a byte,
 * which no 16-bit fixed format can hold.
 *
 * _stoi is taken to truncate toward zero, as C does. That is the one assumption
 * in this module. */
static float Vis_Ftos(i32 n) { return (float)n; }
static i32   Vis_Stoi(float f) { return (i32)f; }
static float Vis_Div(float a, float b) { return a / b; }
static float Vis_Mul(float a, float b) { return a * b; }

static i32 Vis_Abs(i32 n) { return n < 0 ? -n : n; }

void Tact_BuildVisRayTable(VisRay *apRays, u8 *paSteps)
{
    /* 0x37af0..0x37bb4: the 120 rays are the perimeter of the 31 x 31 box, in
     * four bands of 30. IDA's unk_B2728, unk_B2BD8 and unk_B3088 are bands 1, 2
     * and 3 of the same array. */
    for (i32 i = 0; i < 30; ++i) {
        apRays[i].nDx = i - 15;
        apRays[i].nDy = -15;
    }
    for (i32 i = 0; i < 30; ++i) {
        apRays[30 + i].nDx = 15;
        apRays[30 + i].nDy = i - 15;
    }
    for (i32 i = 0; i < 30; ++i) {
        apRays[60 + i].nDx = 15 - i;
        apRays[60 + i].nDy = 15;
    }
    for (i32 i = 0; i < 30; ++i) {
        apRays[90 + i].nDx = -15;
        apRays[90 + i].nDy = 15 - i;
    }
    /* 0x37bb8..0x37be0: clear the eight pass flags of every ray. */
    for (i32 r = 0; r < PUFO_VIS_RAY_COUNT; ++r) {
        for (i32 p = 0; p < PUFO_VIS_PASS_COUNT; ++p) {
            apRays[r].aPass[p] = 0;
        }
    }

    /* 0x37bec..0x37c54: seed the pass flags. The original writes dwords at
     * (600 * pass + 40 * k) mod 4800 + 4 * pass + 8, which reduces to record
     * (15 * pass + k) mod 120, slot pass. The mod wrap is what keeps pass 7
     * inside the array. */
    for (i32 p = 0; p < PUFO_VIS_PASS_COUNT; ++p) {
        for (i32 k = 0; k < 31; ++k) {
            apRays[(15 * p + k) % PUFO_VIS_RAY_COUNT].aPass[p] = 1;
        }
    }

    for (i32 p = 0; p < PUFO_VIS_PASS_COUNT; ++p) {
        i32 nActive = 0;
        for (i32 r = 0; r < PUFO_VIS_RAY_COUNT; ++r) {
            if (apRays[r].aPass[p] == 0) {
                continue;
            }
            i32 nDx = apRays[r].nDx;
            i32 nDy = apRays[r].nDy;

            /* 0x37cd0: walk the dominant axis. Ties walk y. */
            bool bWalkX = Vis_Abs(nDx) > Vis_Abs(nDy);
            i32 nDominant = bWalkX ? nDx : nDy;
            i32 nSign = (nDominant != 0) ? nDominant / Vis_Abs(nDominant) : 0;

            i32 nRow = 15 * (31 * p + nActive);
            ++nActive;
            if (nDominant == 0) {
                continue;
            }

            i32 nPrevY = 0;   /* 0x37d0c */
            i32 nPrevX = 0;   /* 0x37d10 */
            i32 nStep = 0;

            for (;;) {
                size_t nBase = 4 * (size_t)(nRow + Vis_Abs(nStep));

                if (bWalkX) {
                    /* 0x37d68: other = v * dy / dx, in single precision. */
                    i32 nOther = Vis_Stoi(Vis_Mul(Vis_Div(Vis_Ftos(nDy), Vis_Ftos(nDx)),
                                                  Vis_Ftos(nStep)));
                    if (sqrt((double)nOther * nOther + (double)nStep * nStep)
                        > PUFO_VIS_RAY_LIMIT) {
                        paSteps[nBase] = PUFO_VIS_RAY_END;
                        goto RayDone;
                    }
                    paSteps[nBase + 0] = (u8)nStep;
                    paSteps[nBase + 1] = (u8)nOther;
                    paSteps[nBase + 2] = (nSign <= 0) ? 2 : 1;
                    if (nPrevY == nOther) {
                        paSteps[nBase + 3] = 0;
                    } else {
                        /* 0x37edc: the flag lands on the previous step's byte 3. */
                        paSteps[nBase - 1] = (nPrevY > nOther) ? 2 : 1;
                    }
                    nPrevY = nOther;
                } else {
                    /* 0x37f14: other = v / (dy / dx), and zero when dx is zero. */
                    i32 nOther = 0;
                    if (nDx != 0) {
                        nOther = Vis_Stoi(Vis_Div(Vis_Ftos(nStep),
                                                  Vis_Div(Vis_Ftos(nDy), Vis_Ftos(nDx))));
                    }
                    if (sqrt((double)nOther * nOther + (double)nStep * nStep)
                        > PUFO_VIS_RAY_LIMIT) {
                        paSteps[nBase] = PUFO_VIS_RAY_END;
                        goto RayDone;
                    }
                    paSteps[nBase + 0] = (u8)nOther;
                    paSteps[nBase + 1] = (u8)nStep;
                    if (nPrevX == nOther) {
                        paSteps[nBase + 2] = 0;
                    } else {
                        /* 0x3802c: the flag lands on the previous step's byte 2. */
                        paSteps[nBase - 2] = (nPrevX < nOther) ? 1 : 2;
                    }
                    nPrevX = nOther;
                    paSteps[nBase + 3] = (nSign <= 0) ? 2 : 1;
                }
                nStep += nSign;
                if (nStep == nDominant) {
                    break;
                }
            }
RayDone:
            ;
        }
    }
}

/* Fov_EntryIndex - the 31 x 31 grid is indexed by an offset pair in -15..15. */
size_t Fov_EntryIndex(i32 nOffX, i32 nOffY)
{
    return (size_t)(PUFO_FOV_GRID * (nOffY + PUFO_FOV_OFFSET) + (nOffX + PUFO_FOV_OFFSET));
}

/* The original's wall test, shared by wall A and wall B. */
static bool Fov_WallCosts(const MapCell *pCell, const Mc1Rec *pSpr, u8 nCellCounter)
{
    if (pSpr == NULL) {
        return false;
    }
    if (!((pSpr->bStrength > nCellCounter || pSpr->bStrength == 255)
          && (pSpr->bBlocksMove == 0 || (pCell->bFlags29 & 4u) == 0))) {
        return false;
    }
    return true;
}

void Map_ComputeFov(FovField *pFov, u32 *paWindow,
                    const MapGeom *pGeom, MapCellLookup pfnLookup, void *pUser,
                    const u8 *paSteps,
                    i32 nColBase, i32 nRowBase, i32 nOriginX, i32 nOriginY,
                    i32 nLayer, i32 nRange, i32 nCostMode, u16 nWeaponSortKey)
{
    double dCostScale = (nCostMode == 1 || nWeaponSortKey == PUFO_FOV_WEAPON_CLASS_FAST)
                      ? PUFO_FOV_COST_FAST : PUFO_FOV_COST_SLOW;
    MapCell *pOriginCell;

    for (size_t e = 0; e < PUFO_FOV_GRID * PUFO_FOV_GRID; ++e) {
        for (i32 s = 0; s < PUFO_FOV_SLOTS; ++s) {
            pFov->aSlot[e][s] = 0;
        }
    }

    pOriginCell = pfnLookup(nOriginX, nOriginY, nLayer, pUser);

    for (i32 nPass = 0; nPass < PUFO_VIS_PASS_COUNT; nPass += 2) {
        for (i32 i = 0; i < PUFO_VIS_RAYS_PER_PASS; ++i) {
            i32 nCost = nRange;

            for (i32 k = 0; ; ++k) {
                const u8 *pStep = paSteps
                                + PUFO_VIS_GROUP_BYTES * (size_t)nPass
                                + 60 * (size_t)i + 4 * (size_t)k;
                i32 nOffX = (i8)pStep[0];
                i32 nOffY = (i8)pStep[1];
                i32 nNextX, nNextY;
                MapCell *pCell;
                MapCell *pNext;
                u16 *pEntry;
                u16 *pEntryNext;

                if (!Map_IsValidCell(pGeom, nOffX + nOriginX, nOffY + nOriginY, nLayer)) {
                    break;
                }
                pCell = pfnLookup(nOffX + nOriginX, nOffY + nOriginY, nLayer, pUser);
                pEntry = pFov->aSlot[Fov_EntryIndex(nOffX, nOffY)];

                /* Slot 3 is the running cost; slot 0 is stamped with it whenever
                 * this visit is better than the one already recorded. */
                if (pEntry[3] < (u16)nCost) {
                    pEntry[3] = (u16)nCost;
                    pEntry[0] = (u16)nCost;
                }
                if (k >= 14) {
                    break;
                }

                nNextX = (i8)pStep[4];
                nNextY = (i8)pStep[5];

                {
                    i32 nDx = nOffX - nNextX;
                    i32 nDy = nOffY - nNextY;
                    nCost -= (i32)(sqrt((double)nDx * nDx + (double)nDy * nDy) * dCostScale);
                }
                if (nCost < 1) {
                    break;
                }
                if (nNextX == PUFO_VIS_RAY_END) {
                    break;
                }
                if (!Map_IsValidCell(pGeom, nNextX + nOriginX, nNextY + nOriginY, nLayer)) {
                    break;
                }
                pNext = pfnLookup(nNextX + nOriginX, nNextY + nOriginY, nLayer, pUser);
                pEntryNext = pFov->aSlot[Fov_EntryIndex(nNextX, nNextY)];

                /* byte 2: wall A. Flag 2 is the wall of the cell being left,
                 * flag 1 the wall of the cell being entered. */
                if ((i8)pStep[2] == 2) {
                    pCell->bFlags57[0] = (char)((u8)pCell->bFlags57[0] | 2u);
                    if (Fov_WallCosts(pCell, pCell->pWallSprA, pCell->bUnk17)) {
                        if (pEntry[1] < (u16)nCost) {
                            pEntry[1] = (u16)nCost;
                        }
                        nCost -= pCell->pWallSprA->bWallRangeCost;
                        if (nCost < 1) {
                            break;
                        }
                    }
                } else if ((i8)pStep[2] == 1) {
                    if (Fov_WallCosts(pNext, pNext->pWallSprA, pNext->bUnk17)) {
                        nCost -= pNext->pWallSprA->bWallRangeCost;
                        if (pEntryNext[1] < (u16)nCost) {
                            pEntryNext[1] = (u16)nCost;
                        }
                        if (nCost < 1) {
                            break;
                        }
                    }
                }

                /* byte 3: wall B, same asymmetry. */
                if ((i8)pStep[3] == 2) {
                    if (Fov_WallCosts(pCell, pCell->pWallSprB, pCell->bUnk18)) {
                        if (pEntry[2] < (u16)nCost) {
                            pEntry[2] = (u16)nCost;
                        }
                        nCost -= pCell->pWallSprB->bWallRangeCost;
                        if (nCost < 1) {
                            break;
                        }
                    }
                } else if ((i8)pStep[3] == 1) {
                    if (Fov_WallCosts(pNext, pNext->pWallSprB, pNext->bUnk18)) {
                        nCost -= pNext->pWallSprB->bWallRangeCost;
                        if (pEntryNext[2] < (u16)nCost) {
                            pEntryNext[2] = (u16)nCost;
                        }
                        if (nCost < 1) {
                            break;
                        }
                    }
                }

                /* Ceiling, charged on every cell except the origin cell. */
                if (pCell->pCeilSpr != NULL) {
                    if (pCell != pOriginCell) {
                        nCost -= pCell->pCeilSpr->bWallRangeCost;
                    }
                    if (nCost < 1) {
                        break;
                    }
                }
            }
        }
    }

    /* Fold the 31 x 31 field into the 41 x 41 hit window, clamping at 255. Only
     * the low half of each record dword is touched, as the original's u16
     * read-modify-write does. */
    for (i32 nRowOff = 0; nRowOff < PUFO_FOV_GRID; ++nRowOff) {
        for (i32 nColOff = 0; nColOff < PUFO_FOV_GRID; ++nColOff) {
            i32 nRow = nRowOff + nRowBase + 5;
            i32 nCol = nColOff + nColBase + 5;

            if (nRow >= PUFO_HIT_WINDOW_SIDE || nCol >= PUFO_HIT_WINDOW_SIDE) {
                continue;
            }
            for (i32 s = 0; s < PUFO_FOV_SLOTS; ++s) {
                size_t nIdx = 4 * ((size_t)(PUFO_HIT_WINDOW_SIDE * nRow + nCol)) + (size_t)s;
                u32 *pRec = &paWindow[nIdx];
                i32 nSum = (i32)(u16)(*pRec & 0xFFFFu)
                         + (i32)pFov->aSlot[PUFO_FOV_GRID * nRowOff + nColOff][s];

                if ((u32)nSum > 0xFFu) {
                    nSum = 255;
                }
                *pRec = (*pRec & 0xFFFF0000u) | (u32)(u16)nSum;
            }
        }
    }
}
