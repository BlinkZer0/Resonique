// ============================================================================
//  The kernel loops. Included by Kernels_sse.cpp and Kernels_avx2.cpp, which
//  are compiled with different /arch flags: everything here has internal
//  linkage (static) so each translation unit keeps its own machine code.
// ============================================================================
#include "Kernels.h"
#include "PianoVoice.h"

namespace resonique
{
namespace
{
static float stringTickImpl (VoiceString& st, float aTotal, float hammerForce, bool damping)
{
    const int a = st.active;
    const int lim = std::min (kCouplePartials, a);
    float* __restrict p = st.p;
    float* __restrict q = st.q;
    const float* __restrict rc = st.rc;
    const float* __restrict rs = st.rs;
    const float* __restrict bw = st.bridgeW;
    const float* __restrict ci = st.coupleInj;
    for (int k = 0; k < lim; ++k) q[k] += ci[k] * aTotal;
    if (hammerForce > 0.0f)
    {
        const float* __restrict hi = st.hammerInj;
        for (int k = 0; k < a; ++k) q[k] += hi[k] * hammerForce;
    }
    float acc = 0.0f;
    if (damping)
    {
        const float* __restrict dm = st.damp;
        for (int k = 0; k < a; ++k)
        {
            const float pk = p[k], qk = q[k], c = rc[k], sn = rs[k], d = dm[k];
            const float np = (c * pk + sn * qk) * d;
            q[k] = (c * qk - sn * pk) * d;
            p[k] = np;
            acc += bw[k] * np;
        }
    }
    else
    {
        for (int k = 0; k < a; ++k)
        {
            const float pk = p[k], qk = q[k], c = rc[k], sn = rs[k];
            const float np = c * pk + sn * qk;
            q[k] = c * qk - sn * pk;
            p[k] = np;
            acc += bw[k] * np;
        }
    }
    return acc;
}

static void sympTickImpl (float* p, float* q, const float* rc, const float* rs, const float* damp, const float* inj, const float* outW,
                          const int* zoneStart, const float* zoneAccel, float sympGain, float* zoneForceOut)
{
    float* __restrict pp = p;
    float* __restrict qq = q;
    const float* __restrict crc = rc;
    const float* __restrict crs = rs;
    const float* __restrict cinj = inj;
    const float* __restrict cout = outW;
    const float* __restrict cd = damp;
    const int N = zoneStart[kNumZones];
    for (int z = 0; z < kNumZones; ++z)
    {
        const float a = zoneAccel[z] * sympGain;
        if (a == 0.0f) continue;
        const int from = zoneStart[z], to = zoneStart[z + 1];
        for (int j = from; j < to; ++j) qq[j] += cinj[j] * a;
    }
    for (int j = 0; j < N; ++j)
    {
        const float pj = pp[j], qj = qq[j], cc = crc[j], s = crs[j], d = cd[j];
        pp[j] = (cc * pj + s * qj) * d;
        qq[j] = (cc * qj - s * pj) * d;
    }
    for (int z = 0; z < kNumZones; ++z)
    {
        const int from = zoneStart[z], to = zoneStart[z + 1];
        float f = 0;
        for (int j = from; j < to; ++j) f += cout[j] * pp[j];
        zoneForceOut[z] += f;
    }
}

static void boardTickImpl (float* p, float* q, const float* rc, const float* rs, const float* inj, const float* zoneW, const float* zoneWv,
                           const float* outLv, const float* outRv, const float* zoneForce, float gain, float* outL, float* outR, float* zoneVel)
{
    constexpr int M = kSoundboardModes;
    float* __restrict pp = p;
    float* __restrict qq = q;
    const float* __restrict crc = rc;
    const float* __restrict crs = rs;
    const float* __restrict cinj = inj;
    const float* __restrict ol = outLv;
    const float* __restrict orr = outRv;
    for (int z = 0; z < kNumZones; ++z)
    {
        const float f = -zoneForce[z];            // zoneForce is T y'(L); the force on the board is its negative
        if (f == 0.0f) continue;
        const float* __restrict w = zoneW + z * M;
        for (int m = 0; m < M; ++m) qq[m] += cinj[m] * w[m] * f;
    }
    float l = 0, r = 0;
    for (int m = 0; m < M; ++m)
    {
        const float pm = pp[m], qm = qq[m], cc = crc[m], s = crs[m];
        pp[m] = cc * pm + s * qm;
        const float nq = cc * qm - s * pm;
        qq[m] = nq;
        l += ol[m] * nq;
        r += orr[m] * nq;
    }
    *outL = l * gain;
    *outR = r * gain;
    for (int z = 0; z < kNumZones; ++z)
    {
        const float* __restrict w = zoneWv + z * M;
        float v = 0;
        for (int m = 0; m < M; ++m) v += w[m] * qq[m];
        zoneVel[z] = v;
    }
}

static Kernels makeKernelsImpl (const char* name)
{
    Kernels k;
    k.name = name;
    k.stringTick = &stringTickImpl;
    k.sympTick = &sympTickImpl;
    k.boardTick = &boardTickImpl;
    return k;
}
} // namespace
} // namespace resonique
