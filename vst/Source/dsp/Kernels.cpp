// Kernel selection: AVX2 when the CPU and the OS support it (and RESONIQUE_NO_AVX2 is not set).
#include <cstdlib>
#include "Kernels.h"
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
 #include <intrin.h>
#endif

namespace resonique
{
bool cpuSupportsAvx2()
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int r[4] {};
    __cpuid (r, 0);
    if (r[0] < 7) return false;
    __cpuid (r, 1);
    const bool osxsave = (r[2] & (1 << 27)) != 0, avx = (r[2] & (1 << 28)) != 0;
    if (! osxsave || ! avx) return false;
    const unsigned long long xcr0 = _xgetbv (0);
    if ((xcr0 & 6) != 6) return false;             // the OS saves the YMM state
    __cpuidex (r, 7, 0);
    return (r[1] & (1 << 5)) != 0;                  // AVX2
#else
    return false;
#endif
}

const Kernels& kernels()
{
    static const Kernels selected = []
    {
        bool avx2 = cpuSupportsAvx2();
        if (const char* no = std::getenv ("RESONIQUE_NO_AVX2"); no != nullptr && no[0] != '\0' && no[0] != '0') avx2 = false;
        return avx2 ? makeKernelsAvx2() : makeKernelsBaseline();
    }();
    return selected;
}
} // namespace resonique
