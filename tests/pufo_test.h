/* Shared harness for the test suites.
 *
 * Output format is fixed so docs/parity/gen_report.py can read the check count
 * out of any suite without knowing its local helper names:
 *
 *   ok   <what>              a check that passed
 *   FAIL <what>              a check that failed
 *   <suite>: N checks, M failures
 *
 * Helpers are static inline so a suite that uses only some of them still builds
 * clean under /W4 /WX. Counters are file-scope statics, so each suite gets its
 * own copy of the whole harness.
 */
#ifndef PUFO_TEST_H
#define PUFO_TEST_H

#include <math.h>
#include <stdio.h>

static int g_nChecks;
static int g_nFails;

static inline void TestCheck(const char *pWhat, int bOk)
{
    ++g_nChecks;
    if (bOk) {
        printf("  ok   %s\n", pWhat);
        fflush(stdout); /* a crash later in the test must not swallow this line */
        return;
    }
    ++g_nFails;
    printf("  FAIL %s\n", pWhat);
    fflush(stdout);
}

static inline void TestEqI32(const char *pWhat, int nGot, int nWant)
{
    ++g_nChecks;
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pWhat, nGot);
    } else {
        ++g_nFails;
        printf("  FAIL %s: got %d want %d\n", pWhat, nGot, nWant);
    }
    fflush(stdout);
}

static inline void TestEqU16(const char *pWhat, unsigned nGot, unsigned nWant)
{
    ++g_nChecks;
    if (nGot == nWant) {
        printf("  ok   %s (%u)\n", pWhat, nGot);
    } else {
        ++g_nFails;
        printf("  FAIL %s: got %u want %u\n", pWhat, nGot, nWant);
    }
    fflush(stdout);
}

static inline void TestNear(const char *pWhat, double dGot, double dWant)
{
    ++g_nChecks;
    if (fabs(dGot - dWant) < 1e-6) {
        printf("  ok   %s (%g)\n", pWhat, dGot);
    } else {
        ++g_nFails;
        printf("  FAIL %s: got %g want %g\n", pWhat, dGot, dWant);
    }
    fflush(stdout);
}

/* Print the summary line and give main()'s exit code: 0 when nothing failed. */
static inline int TestSummary(const char *pSuite)
{
    printf("%s: %d checks, %d failures\n", pSuite, g_nChecks, g_nFails);
    return g_nFails != 0;
}

#endif /* PUFO_TEST_H */
