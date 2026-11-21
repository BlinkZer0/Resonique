#pragma once
// ============================================================================
//  The soundboard: a reduced-order modal model shared by every string. The
//  bridge forces of all voices (grouped into zones along the bridge) drive the
//  modes; the modes radiate to the two output channels and move the bridge,
//  and that bridge motion goes back into the strings (coupling / sympathetic
//  resonance) and sets part of their decay (through the admittance the
//  ModelBuilder evaluates). Modes live as (p, q) rotation pairs like strings.
// ============================================================================
#include "StringModel.h"

namespace resonique
{
class Soundboard
{
public:
    static constexpr int M = kSoundboardModes;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        std::memset (p, 0, sizeof (p));
        std::memset (q, 0, sizeof (q));
        for (auto& v : zoneVelPrev) v = 0;
        asleep = true;
    }

    // Copies a table's coefficients; the modal state keeps ringing.
    void adopt (const SoundboardTable& t)
    {
        table = t;
        hfLevel = (float) t.hfLevel;
        applyResonance (resonance, true);
        for (int m = 0; m < M; ++m)
        {
            const auto& md = t.modes[(size_t) m];
            inj[m] = md.inj;
            for (int z = 0; z < kNumZones; ++z)
            {
                zoneW[z][m] = md.zoneW[(size_t) z];
                zoneWv[z][m] = md.zoneW[(size_t) z] * md.velW;
            }
        }
        haveTable = true;
    }

    // 0..1: scales mode Q (0.5 = the instrument's own) and modal level
    void setResonance (double r) { applyResonance (r, false); }

    // c samples starting at `pos` of the chunk. zoneForce[z][pos + i]: forces into the zones.
    // Writes board radiation to outL/outR[i] (velocity units) and the zone accelerations
    // zoneAccel[z][i] for the next coupling step.
    template <int W>
    void processSamples (int c, const float zoneForce[kNumZones][kChunk], float* outL, float* outR,
                         float (&zoneAccel)[kNumZones][W], int pos)
    {
        if (! haveTable)
        {
            for (int i = 0; i < c; ++i) { outL[i] = outR[i] = 0; for (int z = 0; z < kNumZones; ++z) zoneAccel[z][i] = 0; }
            return;
        }
        if (asleep)   // nothing ringing: stay silent until a force arrives (idle plugins cost nothing)
        {
            bool input = false;
            for (int z = 0; z < kNumZones && ! input; ++z)
                for (int i = 0; i < c; ++i) if (zoneForce[z][pos + i] != 0.0f) { input = true; break; }
            if (! input)
            {
                for (int i = 0; i < c; ++i) { outL[i] = outR[i] = 0; for (int z = 0; z < kNumZones; ++z) zoneAccel[z][i] = 0; }
                return;
            }
            asleep = false;
        }
        const float fsF = (float) fs;
        float* __restrict pp = p;
        float* __restrict qq = q;
        const float* __restrict crc = rc;
        const float* __restrict crs = rs;
        const float* __restrict cinj = inj;
        const float* __restrict ol = outLv;
        const float* __restrict orr = outRv;
        for (int i = 0; i < c; ++i)
        {
            for (int z = 0; z < kNumZones; ++z)
            {
                // zoneForce is T y'(L), the string's end slope; the force on the board is its negative
                const float f = -zoneForce[z][pos + i];
                if (f == 0.0f) continue;
                const float* __restrict w = zoneW[z];
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
            outL[i] = l * gain;
            outR[i] = r * gain;
            for (int z = 0; z < kNumZones; ++z)
            {
                const float* __restrict w = zoneWv[z];
                float v = 0;
                for (int m = 0; m < M; ++m) v += w[m] * qq[m];
                zoneAccel[z][i] = (v - zoneVelPrev[z]) * fsF;
                zoneVelPrev[z] = v;
            }
        }
    }

    void endChunk()
    {
        // safety: a modal bank cannot blow up on its own, but guard against bad input
        if (! std::isfinite (p[0]) || ! std::isfinite (q[0]) || std::abs (q[0]) > 1e6f) reset();
        if (! asleep && energy() < 1e-22f) { reset(); asleep = true; }   // rung down: sleep until the next force
    }

    float energy() const
    {
        float e = 0;
        for (int m = 0; m < M; ++m) e += p[m] * p[m] + q[m] * q[m];
        return e;
    }

    float hfLevel = 1.0f;

private:
    void applyResonance (double r, bool force)
    {
        r = clampT (r, 0.0, 1.0);
        if (! force && std::abs (r - resonance) < 1e-4) return;
        resonance = r;
        const double sigmaMul = std::pow (2.0, 1.0 - 2.0 * r);     // r = 1 -> half the loss
        gain = (float) (0.6 + 0.8 * r);
        const double dt = 1.0 / fs;
        for (int m = 0; m < M; ++m)
        {
            const auto& md = table.modes[(size_t) m];
            const double sigma = md.sigma * sigmaMul;
            const double w = kTwoPi * md.freq;
            const double rad = std::exp (-sigma * dt);
            rc[m] = (float) (rad * std::cos (w * dt));
            rs[m] = (float) (rad * std::sin (w * dt));
            outLv[m] = md.outL * md.velW;
            outRv[m] = md.outR * md.velW;
        }
    }

    double fs = 48000;
    double resonance = 0.5;
    float gain = 1.0f;
    bool haveTable = false, asleep = true;
    SoundboardTable table;
    float p[M] {};
    float q[M] {};
    float rc[M] {};
    float rs[M] {};
    float inj[M] {};
    float outLv[M] {};
    float outRv[M] {};
    float zoneW[kNumZones][M] {};
    float zoneWv[kNumZones][M] {};
    float zoneVelPrev[kNumZones] {};
};
} // namespace resonique
