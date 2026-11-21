#pragma once
// ============================================================================
//  Mechanical noises, synthesised: the hammer / key thump, key release, the
//  damper landing on the strings, the pedal mechanism. Each event is a short
//  shaped noise burst plus a damped "knock" resonator. Events go partly into
//  the soundboard (through the zone forces, so they take on the body's colour)
//  and partly straight to the output (the "air" path).
// ============================================================================
#include "Common.h"

namespace resonique
{
enum class NoiseType { Hammer, KeyRelease, Damper, PedalDown, PedalUp };

struct NoiseEvent
{
    bool active = false;
    NoiseType type = NoiseType::Hammer;
    int zone = 0;
    float panL = 0.7f, panR = 0.7f;
    int t = 0, dur = 0, attack = 0;
    float gainZone = 0, gainDirect = 0;
    float decay = 0.999f;     // per-sample envelope multiplier after the attack
    float env = 0;
    Biquad filt;
    OnePoleLP tone;
    float kp = 0, kq = 0, krc = 0, krs = 0, kGain = 0;   // knock resonator
    Rng rng;

    inline float sample()
    {
        if (t < attack) env = (float) t / (float) attack;
        else env *= decay;
        float x = tone.process (filt.process (rng.noise())) * env;
        // knock: one damped sine
        const float np = krc * kp + krs * kq;
        kq = krc * kq - krs * kp;
        kp = np;
        x += kp * kGain;
        if (++t >= dur) active = false;
        return x;
    }
};

class NoisePool
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& e : events) e.active = false;
    }

    // level: 0..1 overall gain for this event; strength: velocity-like 0..1; colour: 0 dull .. 1 bright
    void trigger (NoiseType type, int zone, float pan, double level, double strength, double colour)
    {
        if (level <= 0.0005) return;
        NoiseEvent* e = nullptr;
        for (auto& c : events) if (! c.active) { e = &c; break; }
        if (e == nullptr)   // steal the oldest
        {
            int best = 0;
            for (int i = 1; i < kNoiseEvents; ++i) if (events[(size_t) i].t > events[(size_t) best].t) best = i;
            e = &events[(size_t) best];
        }
        e->active = true;
        e->type = type;
        e->zone = zone;
        const float a = (float) ((clampT ((double) pan, -1.0, 1.0) + 1.0) * 0.25 * kPi);
        e->panL = std::cos (a);
        e->panR = std::sin (a);
        e->t = 0;
        e->env = 0;
        e->kp = e->kq = 0;
        e->rng = Rng ((uint32_t) (seed += 0x9e3779b9u));
        e->filt.reset();
        e->tone.z = 0;
        strength = clampT (strength, 0.0, 1.0);
        colour = clampT (colour, 0.0, 1.0);
        const double dt = 1.0 / fs;
        auto setDecay = [&] (double seconds) { e->decay = (float) std::exp (-dt / std::max (1e-4, seconds)); };
        auto setKnock = [&] (double hz, double seconds, double gain)
        {
            const double r = std::exp (-dt / std::max (1e-4, seconds));
            e->krc = (float) (r * std::cos (kTwoPi * hz * dt));
            e->krs = (float) (r * std::sin (kTwoPi * hz * dt));
            e->kq = 1.0f;     // starts with velocity: a sine from zero
            e->kGain = (float) gain;
        };
        switch (type)
        {
            case NoiseType::Hammer:
            {
                // the key bottoming out and the shank knock: a low thump that grows with velocity
                const double v = std::pow (strength, 1.6);
                e->attack = (int) (0.0008 * fs);
                e->dur = (int) (0.08 * fs);
                setDecay (0.012 + 0.01 * strength);
                e->filt.lowPass (fs, 900.0 + 2800.0 * colour * (0.4 + 0.6 * strength), 0.8);
                e->tone.set (fs, 4000.0 + 8000.0 * colour);
                setKnock (70.0 + 50.0 * (1.0 - (double) zone / kNumZones), 0.035, 0.45 * v);
                e->gainZone = (float) (level * 1.8 * v);
                e->gainDirect = (float) (level * 0.012 * v * (0.3 + 0.7 * colour));
                break;
            }
            case NoiseType::KeyRelease:
            {
                e->attack = (int) (0.0005 * fs);
                e->dur = (int) (0.05 * fs);
                setDecay (0.009);
                e->filt.bandPass (fs, 1800.0 + 1500.0 * colour, 0.9);
                e->tone.set (fs, 9000.0);
                setKnock (110.0, 0.02, 0.25);
                e->gainZone = (float) (level * 0.35);
                e->gainDirect = (float) (level * 0.006);
                break;
            }
            case NoiseType::Damper:
            {
                // felt meeting a vibrating string: a brief, bright "shh" scaled by how much the string was moving
                e->attack = (int) (0.002 * fs);
                e->dur = (int) (0.06 * fs);
                setDecay (0.012);
                e->filt.bandPass (fs, 2600.0, 0.7);
                e->tone.set (fs, 7000.0);
                setKnock (160.0, 0.012, 0.1);
                e->gainZone = (float) (level * 0.25 * strength);
                e->gainDirect = (float) (level * 0.005 * strength);
                break;
            }
            case NoiseType::PedalDown:
            {
                e->attack = (int) (0.003 * fs);
                e->dur = (int) (0.14 * fs);
                setDecay (0.03);
                e->filt.lowPass (fs, 600.0, 0.7);
                e->tone.set (fs, 2500.0);
                setKnock (48.0, 0.07, 0.6);
                e->gainZone = (float) (level * 1.2);
                e->gainDirect = (float) (level * 0.004);
                break;
            }
            case NoiseType::PedalUp:
            {
                e->attack = (int) (0.002 * fs);
                e->dur = (int) (0.09 * fs);
                setDecay (0.02);
                e->filt.lowPass (fs, 900.0, 0.7);
                e->tone.set (fs, 3000.0);
                setKnock (62.0, 0.04, 0.35);
                e->gainZone = (float) (level * 0.7);
                e->gainDirect = (float) (level * 0.003);
                break;
            }
        }
    }

    // c samples starting at `pos` of the chunk (directL/R already point at `pos`)
    void processSamples (int c, float zoneForce[kNumZones][kChunk], float* directL, float* directR, int pos)
    {
        for (auto& e : events)
        {
            if (! e.active) continue;
            float* zf = zoneForce[e.zone] + pos;
            for (int i = 0; i < c && e.active; ++i)
            {
                const float x = e.sample();
                zf[i] += x * e.gainZone;
                directL[i] += x * e.gainDirect * e.panL;
                directR[i] += x * e.gainDirect * e.panR;
            }
        }
    }
    void processChunk (int n, float zoneForce[kNumZones][kChunk], float* directL, float* directR)
    {
        processSamples (n, zoneForce, directL, directR, 0);
    }

    int activeCount() const { int c = 0; for (const auto& e : events) c += e.active ? 1 : 0; return c; }

private:
    double fs = 48000;
    uint32_t seed = 12345;
    NoiseEvent events[kNoiseEvents];
};
} // namespace resonique
