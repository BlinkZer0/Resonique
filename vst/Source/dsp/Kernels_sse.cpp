// Baseline (x64: SSE2) build of the kernel loops.
#include "KernelsImpl.h"

namespace resonique
{
Kernels makeKernelsBaseline() { return makeKernelsImpl ("SSE2"); }
}
