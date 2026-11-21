#pragma once
// ============================================================================
//  PianoVoice: one key. Up to three modal strings (structure-of-arrays float
//  banks, rotated per sample), the hammer, the damper and the local bridge
//  that couples the unison strings (Weinreich: beating + two-stage decay) and
//  carries the soundboard's motion back into the strings (sympathetic
//  resonance). Everything the voice needs is copied from the NoteTable at
//  note-on, so tables can be swapped while it plays.
// ============================================================================
#include "HammerModel.h"
#include "DamperModel.h"
#include "Kernels.h"

namespace resonique
{
struct VoiceString
{
    float p[kMaxPartials];
    float q[kMaxPartials];
    float rc[kMaxPartials];
    float rs[kMaxPartials];
    float damp[kMaxPartials];
    float bridgeW[kMaxPartials];
    float hammerShape[kMaxPartials];
    float hammerInj[kMaxPartials];
    float damperSigma[kMaxPartials];
    float freq[kMaxPartials];
    float omega[kMaxPartials];
    float coupleInj[kCouplePartials];
    int numPartials = 0;
    int active = 0;          // partials still computed (the top ones are dropped as they die)
    float staticK = 0;
    float strikeGain = 1;    // soft pedal: the third string is only brushed
    float level = 0;         // bridge-force amplitude estimate (N), chunk rate

    void load (const StringTable& t)
    {
        numPartials = active = t.numPartials;
        std::memcpy (rc, t.rc, sizeof (rc));
        std::memcpy (rs, t.rs, sizeof (rs));
        std::memcpy (bridgeW, t.bridgeW, sizeof (bridgeW));
        std::memcpy (hammerShape, t.hammerShape, sizeof (hammerShape));
        std::memcpy (hammerInj, t.hammerInj, sizeof (hammerInj));
        std::memcpy (damperSigma, t.damperSigma, sizeof (damperSigma));
        std::memcpy (freq, t.freq, sizeof (freq));
        std::memcpy (coupleInj, t.coupleInj, sizeof (coupleInj));
        for (int n = 0; n < kMaxPartials; ++n) { omega[n] = (float) (kTwoPi * t.freq[n]); damp[n] = 1.0f; }
        staticK = (float) t.staticK;
    }
    void clearState()
    {
        std::memset (p, 0, sizeof (p));
        std::memset (q, 0, sizeof (q));
        level = 0;
    }
};

class PianoVoice
{
public:
    // ---- identity / state ----------------------------------------------------
    int note = -1;
    bool inUse = false;
    bool keyDown = false;
    bool sostenuto = false;
    int zone = 0;
    float pan = 0;
    float gain = 1;           // per-key level balance (from the table)
    float velocity = 0;
    uint32_t age = 0;         // chunks since note-on
    double startLevel = 0;    // peak level seen (for noise scaling)

    Hammer hammer;
    Damper damper;
    VoiceString strings[kMaxStrings];
    int numStrings = 1;

    // local bridge: m dv/dt = -F - r v, integrated exactly per sample for a constant F
    struct Bridge { double u = 0, v = 0, mass = 0.05, r = 2600, decay = 0.0, invR = 1.0 / 2600; } bridge;

    double fs = 48000, dt = 1.0 / 48000;
    double appliedBend = 1.0;
    float bridgeForceAtDamping = 0;   // level when the damper landed (damper-noise scaling)

    static constexpr float kEndLevel = 2.5e-6f;     // N: below this the voice is silent
    static constexpr float kTrimLevel2 = 1e-15f;    // N^2 per partial: top partial dropped below this

    // ---- lifecycle -------------------------------------------------------------
    void start (const NoteTable& nt, double vel01, double sampleRate, double dynamics, double velCurve,
                double soft, double bridgeCouplingParam, double decayParam = 1.0)
    {
        fs = sampleRate;
        dt = 1.0 / fs;
        note = nt.midiNote;
        zone = nt.zone;
        pan = (float) nt.pan;
        gain = nt.gain;
        numStrings = nt.numStrings;
        velocity = (float) vel01;
        double sumMuL = 0;
        for (int s = 0; s < numStrings; ++s)
        {
            strings[s].load (nt.strings[s]);
            strings[s].clearState();
            strings[s].strikeGain = 1.0f;
            sumMuL += nt.strings[s].linearDensity * nt.strings[s].length;
        }
        if (numStrings == 3 && soft > 0.01) strings[2].strikeGain = (float) (1.0 - 0.85 * soft);
        damper.hasDamper = nt.hasDamper;
        damper.prepare (fs, nt.damperDropMs);
        damper.pressure = damper.target = 0;
        bridge.u = bridge.v = 0;
        // local admittance: 0.5 = the instrument's own, 0 = ten times stiffer, 1 = twice as mobile.
        // The String Decay parameter also scales the bridge's loss, so it reaches the live part of the decay.
        bridge.r = nt.bridgeR * clampT (decayParam, 0.25, 4.0) / (0.1 + 1.8 * clampT (bridgeCouplingParam, 0.0, 1.0));
        bridge.mass = std::max (nt.bridgeMass, 0.4 * sumMuL);   // keeps the coupled kinetic energy positive definite
        bridge.decay = std::exp (-bridge.r / bridge.mass * dt);
        bridge.invR = 1.0 / bridge.r;
        hammer.strike (nt, vel01, fs, dynamics, velCurve, soft);
        keyDown = true;
        sostenuto = false;
        inUse = true;
        age = 0;
        startLevel = 0;
        appliedBend = 1.0;
    }

    // The key is struck again while the strings still ring: the strings keep their state.
    void restrike (const NoteTable& nt, double vel01, double dynamics, double velCurve, double soft)
    {
        velocity = (float) vel01;
        for (int s = 0; s < numStrings; ++s) strings[s].active = strings[s].numPartials;
        hammer.strike (nt, vel01, fs, dynamics, velCurve, soft);
        keyDown = true;
        age = 0;
    }

    void release() { keyDown = false; }

    bool finished() const
    {
        if (hammer.active) return false;
        for (int s = 0; s < numStrings; ++s)
            if (strings[s].level > kEndLevel) return false;
        return age > 2;
    }

    float level() const
    {
        float l = 0;
        for (int s = 0; s < numStrings; ++s) l += strings[s].level;
        return l;
    }

    // ---- per chunk ---------------------------------------------------------------
    // lift: damper lift from the pedals (0..1). Once per chunk, before the samples.
    void updateDamper (double lift)
    {
        damper.update (keyDown, sostenuto, lift);
        if (damper.seatedEvent) bridgeForceAtDamping = level();
        if (damper.resting())
        {
            const float pr = (float) (damper.pressure * dt);
            for (int s = 0; s < numStrings; ++s)
            {
                auto& st = strings[s];
                for (int n = 0; n < st.active; ++n)
                    st.damp[n] = std::max (0.0f, 1.0f - st.damperSigma[n] * pr);
            }
        }
    }

    // Pitch bend: rotate the coefficient vectors so every partial's angle scales by `ratio`.
    void setBend (double ratio)
    {
        if (std::abs (ratio - appliedBend) < 1e-7) return;
        const double delta = ratio - appliedBend;
        appliedBend = ratio;
        const float k = (float) (kTwoPi * dt * delta);
        for (int s = 0; s < numStrings; ++s)
        {
            auto& st = strings[s];
            for (int n = 0; n < st.active; ++n)
            {
                const float d = k * st.freq[n];
                const float d2 = d * d;
                const float c = 1.0f - d2 * (0.5f - d2 * (1.0f / 24.0f));
                const float si = d * (1.0f - d2 * (1.0f / 6.0f - d2 * (1.0f / 120.0f)));
                const float rc = st.rc[n], rs = st.rs[n];
                st.rc[n] = rc * c - rs * si;
                st.rs[n] = rs * c + rc * si;
            }
        }
    }

    // Renders `n` samples. zoneAccel: the soundboard's acceleration at this voice's bridge zone
    // for these samples (previous sub-chunk), scaled by sympGain. outF receives the force the
    // strings exert on the bridge (N).
    void processSamples (int n, const float* zoneAccel, float sympGain, float* outF)
    {
        const bool damping = damper.resting();
        const double bdt = dt;
        const auto tick = kernels().stringTick;

        for (int i = 0; i < n; ++i)
        {
            // ---- local bridge: force from the low partials + static term -> acceleration
            double Fb = 0.0;
            for (int s = 0; s < numStrings; ++s)
            {
                const auto& st = strings[s];
                const int lim = std::min (kCouplePartials, st.active);
                const float* __restrict bw = st.bridgeW;
                const float* __restrict p = st.p;
                float acc = 0.0f;
                for (int k = 0; k < lim; ++k) acc += bw[k] * p[k];
                Fb += acc + st.staticK * bridge.u;
            }
            // exact solution of m dv/dt = -F - r v over one sample with F held constant
            const double vInf = -Fb * bridge.invR;
            const double vOld = bridge.v;
            bridge.v = vInf + (vOld - vInf) * bridge.decay;
            bridge.u += 0.5 * (vOld + bridge.v) * bdt;
            const float aTotal = (float) ((bridge.v - vOld) / bdt) + zoneAccel[i] * sympGain;

            // ---- hammer contact (sub-stepped, string displacement extrapolated linearly)
            float hammerForce[kMaxStrings] { 0, 0, 0 };
            if (hammer.active)
            {
                double y[kMaxStrings] { 0, 0, 0 }, yd[kMaxStrings] { 0, 0, 0 };
                for (int s = 0; s < numStrings; ++s)
                {
                    const auto& st = strings[s];
                    const float* __restrict hs = st.hammerShape;
                    const float* __restrict p = st.p;
                    const float* __restrict q = st.q;
                    const float* __restrict om = st.omega;
                    float a0 = 0.0f, a1 = 0.0f;
                    for (int k = 0; k < st.active; ++k)
                    {
                        a0 += hs[k] * p[k];
                        a1 += hs[k] * q[k] * om[k];
                    }
                    y[s] = a0; yd[s] = a1;
                }
                const int sub = hammer.subSteps;
                const double sdt = hammer.dt;
                double sum[kMaxStrings] { 0, 0, 0 };
                bool contact = false;
                for (int j = 0; j < sub; ++j)
                {
                    const double tau = j * sdt;
                    double total = 0.0;
                    for (int s = 0; s < numStrings; ++s)
                    {
                        const double f = hammer.force (s, y[s] + yd[s] * tau) * strings[s].strikeGain;
                        sum[s] += f;
                        total += f;
                    }
                    if (total > 0.0) contact = true;
                    hammer.advanceSub (total);
                }
                for (int s = 0; s < numStrings; ++s) hammerForce[s] = (float) (sum[s] / sub);
                hammer.endSample (contact);
            }

            // ---- strings: inject, rotate, sum, damp (the kernel runs on the selected instruction set)
            float out = 0.0f;
            for (int s = 0; s < numStrings; ++s)
                out += tick (strings[s], aTotal, hammerForce[s], damping);
            outF[i] = out;
        }
    }

    // Chunk-rate bookkeeping after the samples: level, partial trimming, safety.
    void endChunk()
    {
        ++age;
        for (int s = 0; s < numStrings; ++s)
        {
            auto& st = strings[s];
            float e = 0.0f;
            for (int k = 0; k < st.active; ++k)
            {
                const float w = st.bridgeW[k];
                e += (st.p[k] * st.p[k] + st.q[k] * st.q[k]) * w * w;
            }
            st.level = std::sqrt (e);
            if (! hammer.active)
            {
                while (st.active > 4)
                {
                    const int k = st.active - 1;
                    const float w = st.bridgeW[k];
                    if ((st.p[k] * st.p[k] + st.q[k] * st.q[k]) * w * w > kTrimLevel2) break;
                    st.p[k] = st.q[k] = 0;
                    --st.active;
                }
            }
            if (! std::isfinite (st.level)) { st.clearState(); st.level = 0; }
        }
        startLevel = std::max (startLevel, (double) level());
        if (! std::isfinite (bridge.u) || ! std::isfinite (bridge.v)) bridge.u = bridge.v = 0;
    }
};
} // namespace resonique
