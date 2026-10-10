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
