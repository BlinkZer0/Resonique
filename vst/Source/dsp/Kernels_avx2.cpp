// AVX2 build of the kernel loops (this file is compiled with /arch:AVX2; see CMakeLists.txt).
// Only ever called after cpuSupportsAvx2() said yes.
#include "KernelsImpl.h"

namespace resonique
{
Kernels makeKernelsAvx2() { return makeKernelsImpl ("AVX2"); }
}
