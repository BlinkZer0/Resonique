#pragma once
// ============================================================================
//  The sympathetic bank: a light modal copy (the first kSympPartials partials
//  of one string) of every key, always present. Strings whose dampers are off
//  (sustain pedal, held key, sostenuto) and that no full voice is playing
//  pick up the soundboard's motion at their bridge zone and ring along; the
//  rest are held down by their damper loss. Together with the soundboard this
//  is the instrument's coupling network: every string talks to every other
//  one through the board (8 zone ports), not through an all-to-all matrix.
//  Laid out as one structure-of-arrays bank, grouped by zone, so the whole
//  thing is a few vectorisable loops per sample.
// ============================================================================
#include "StringModel.h"

namespace resonique
{
class SympatheticBank
{
public:
    static constexpr int K = kSympPartials;
    static constexpr int N = kNumNotes * K;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dt = 1.0 / fs;
        reset();
    }

    void reset()
    {
        std::memset (p, 0, sizeof (p));
        std::memset (q, 0, sizeof (q));
        for (auto& x : pressure) x = 1.0;
        for (auto& x : keyHeld) x = false;
        for (auto& x : sostenuto) x = false;
        for (auto& x : hasVoice) x = false;
        for (int i = 0; i < N; ++i) damp[i] = 1.0f;
        asleep = true;
    }

    void adopt (const InstrumentTables& t)
    {
        for (int z = 0; z <= kNumZones; ++z) zoneStart[z] = N;
        int zone = -1;
        for (int i = 0; i < kNumNotes; ++i)
        {
            const auto& nt = t.notes[(size_t) i];
            const auto& st = nt.strings[0];
            if (nt.zone != zone) { zone = nt.zone; zoneStart[zone] = i * K; }
            dropCoef[i] = 1.0 - std::exp (-1.0 / (std::max (1.0, nt.damperDropMs) * 1e-3 * fs / kChunk));
            hasDamper[i] = nt.hasDamper;
            for (int k = 0; k < K; ++k)
            {
                const int j = i * K + k;
                const bool ok = k < st.numPartials;
                rc[j] = ok ? st.rc[k] : 0.0f;
                rs[j] = ok ? st.rs[k] : 0.0f;
                inj[j] = ok ? st.coupleInj[k] : 0.0f;
                outW[j] = ok ? st.bridgeW[k] : 0.0f;
                damperSigma[j] = ok ? st.damperSigma[k] : 0.0f;
            }
        }
        for (int z = kNumZones - 1; z >= 0; --z)
            if (zoneStart[z] == N && z + 1 <= kNumZones) zoneStart[z] = zoneStart[z + 1];
        haveTable = true;
    }

    void setKey (int noteIdx, bool down) { keyHeld[(size_t) noteIdx] = down; }
    void setSostenuto (int noteIdx, bool on) { sostenuto[(size_t) noteIdx] = on; }
    void setHasVoice (int noteIdx, bool on) { hasVoice[(size_t) noteIdx] = on; }
    void captureSostenuto() { for (int i = 0; i < kNumNotes; ++i) sostenuto[(size_t) i] = keyHeld[(size_t) i]; }
    void releaseSostenuto() { for (auto& x : sostenuto) x = false; }

    // Once per chunk: update damper pressures from the pedal lift and refresh the damping factors.
    void updateDampers (double lift)
    {
        bool anyUndamped = false;
        for (int i = 0; i < kNumNotes; ++i)
        {
            double target;
            if (hasVoice[(size_t) i]) target = 3.0;                       // the full voice owns this note: kill the light copy
            else if (! hasDamper[(size_t) i]) target = 0.0;
            else target = (keyHeld[(size_t) i] || sostenuto[(size_t) i]) ? 0.0 : std::pow (clampT (1.0 - lift, 0.0, 1.0), 2.5);
            double& pr = pressure[(size_t) i];
            const double c = target > pr ? dropCoef[(size_t) i] : 0.5;
            pr += (target - pr) * c;
            if (std::abs (target - pr) < 1e-4) pr = target;
            if (pr < 0.999 && ! hasVoice[(size_t) i]) anyUndamped = true;
            const float prf = (float) (pr * dt);
            float* d = damp + i * K;
            const float* s = damperSigma + i * K;
            if (pr <= 1e-5f) { for (int k = 0; k < K; ++k) d[k] = 1.0f; }
            else if (hasVoice[(size_t) i]) { const float kill = std::max (0.0f, 1.0f - 3000.0f * (float) dt); for (int k = 0; k < K; ++k) d[k] = kill; }
            else { for (int k = 0; k < K; ++k) d[k] = std::max (0.0f, 1.0f - s[k] * prf); }
        }
        undamped = anyUndamped;
    }

    // c samples starting at `pos` of the chunk: adds the bank's bridge forces to zoneForce;
    // reads the previous coupling step's zone accelerations.
    template <int W>
    void processSamples (int c, const float (&zoneAccel)[kNumZones][W], float sympGain, float zoneForce[kNumZones][kChunk], int pos)
    {
        if (! haveTable) return;
        if (asleep && ! undamped) return;
        asleep = false;
        float* __restrict pp = p;
        float* __restrict qq = q;
        const float* __restrict crc = rc;
        const float* __restrict crs = rs;
        const float* __restrict cinj = inj;
        const float* __restrict cout = outW;
        const float* __restrict cd = damp;
        for (int i = 0; i < c; ++i)
        {
            for (int z = 0; z < kNumZones; ++z)
            {
                const float a = zoneAccel[z][i] * sympGain;
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
                zoneForce[z][pos + i] += f;
            }
        }
    }

    void endChunk()
    {
        // sleep when fully damped and silent
        if (asleep || undamped) return;
        float e = 0;
        for (int j = 0; j < N; ++j) e += p[j] * p[j] + q[j] * q[j];
        if (e < 1e-24f || ! std::isfinite (e)) { std::memset (p, 0, sizeof (p)); std::memset (q, 0, sizeof (q)); asleep = true; }
    }

    float energyOfNote (int noteIdx) const
    {
        float e = 0;
        for (int k = 0; k < K; ++k)
        {
            const int j = noteIdx * K + k;
            e += (p[j] * p[j] + q[j] * q[j]) * outW[j] * outW[j];
        }
        return std::sqrt (e);
    }

private:
    double fs = 48000, dt = 1.0 / 48000;
    bool haveTable = false, asleep = true, undamped = false;
    int zoneStart[kNumZones + 1] {};
    float p[N] {};
    float q[N] {};
    float rc[N] {};
    float rs[N] {};
    float inj[N] {};
    float outW[N] {};
    float damp[N] {};
    float damperSigma[N] {};
    double pressure[kNumNotes] {};
    double dropCoef[kNumNotes] {};
    bool hasDamper[kNumNotes] {};
    bool keyHeld[kNumNotes] {};
    bool sostenuto[kNumNotes] {};
    bool hasVoice[kNumNotes] {};
};
} // namespace resonique
