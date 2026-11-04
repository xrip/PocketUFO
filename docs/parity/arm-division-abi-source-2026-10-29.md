# ARM signed-division helper ABI corroboration

The original target imports `_rt_sdiv` through COREDLL at IAT0xA600C; the resolver
audit records the target import and raw payloads R0=100/R1=readiness and
R0=10/R1=damage delta. The original COREDLL implementation is not in this IDB.

Microsoft's Windows ARM runtime discussion shows the software signed helper
using the second input register as numerator and the first as denominator,
returning quotient and remainder in R0/R1. This independently corroborates the
divisor-first convention used by the existing project ABI record. See
[Microsoft: ARM miscellaneous instructions and software division](https://devblogs.microsoft.com/oldnewthing/20210618-00/?p=105324).

This external contract is supporting ABI evidence, not an executed original
COREDLL test or a replacement for the target import/register-load proof.
The old project note alone was not treated as independent implementation proof.
