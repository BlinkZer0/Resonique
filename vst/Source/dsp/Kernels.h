#pragma once
// ============================================================================
//  The per-sample hot loops as kernels with runtime instruction-set dispatch.
//  KernelsImpl.h holds the loops; Kernels_sse.cpp and Kernels_avx2.cpp compile
//  it with the baseline and with /arch:AVX2; Kernels.cpp picks a set once,
//  off the audio thread (PianoEngine::prepare calls kernels()).
//  Set RESONIQUE_NO_AVX2=1 in the environment to force the baseline kernels.
// ============================================================================
#include "Common.h"

namespace resonique
{
struct VoiceString;

struct Kernels
{
    const char* name = "scalar";
    // one sample of one string: coupling + hammer injection, rotation, damping; returns the bridge force
    float (*stringTick) (VoiceString& st, float aTotal, float hammerForce, bool damping) = nullptr;
    // one sample of the sympathetic bank (N partials grouped by zone): inject, rotate, sum per zone (added to zoneForceOut)
    void (*sympTick) (float* p, float* q, const float* rc, const float* rs, const float* damp, const float* inj, const float* outW,
                      const int* zoneStart, const float* zoneAccel, float sympGain, float* zoneForceOut) = nullptr;
    // one sample of the soundboard (M modes): inject the zone forces, rotate, radiate, zone velocities
    void (*boardTick) (float* p, float* q, const float* rc, const float* rs, const float* inj, const float* zoneW, const float* zoneWv,
                       const float* outLv, const float* outRv, const float* zoneForce, float gain, float* outL, float* outR, float* zoneVel) = nullptr;
};

Kernels makeKernelsBaseline();
Kernels makeKernelsAvx2();
bool cpuSupportsAvx2();
const Kernels& kernels();          // the selected set (selected on the first call; call it from prepare())
} // namespace resonique
