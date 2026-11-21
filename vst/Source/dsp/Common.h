#pragma once
// ============================================================================
//  Resonique DSP core: shared constants and small helpers. No JUCE in here.
// ============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace resonique
{
constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

constexpr int kFirstNote       = 21;    // A0
constexpr int kLastNote        = 108;   // C8
constexpr int kNumNotes        = 88;
constexpr int kMaxPartials     = 128;   // modes per string
constexpr int kMaxStrings      = 3;     // strings per note
constexpr int kMaxVoices       = 128;
constexpr int kChunk           = 32;    // control rate (samples)
constexpr int kNumZones        = 8;     // bridge zones along the soundboard
constexpr int kSoundboardModes = 96;
constexpr int kSympPartials    = 12;    // partials in the light sympathetic bank
constexpr int kCouplePartials  = 16;    // partials that take part in bridge coupling
constexpr int kNoiseEvents     = 48;    // simultaneous mechanical-noise generators

inline int noteIndex (int midiNote) { return std::clamp (midiNote, kFirstNote, kLastNote) - kFirstNote; }

template <typename T> inline T clampT (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double lerp (double a, double b, double t) { return a + (b - a) * t; }
inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) { return 20.0 * std::log10 (std::max (g, 1e-12)); }
inline double midiToHz (double note, double a4 = 440.0) { return a4 * std::pow (2.0, (note - 69.0) / 12.0); }
inline float flushDenormal (float x) { return std::abs (x) < 1e-30f ? 0.0f : x; }

// Piecewise-linear curve across the keyboard: value at anchor notes, linear in between.
struct NoteCurve
{
    struct Pt { double note, value; };
    std::array<Pt, 8> pts {};
    int n = 0;

    NoteCurve() = default;
    NoteCurve (std::initializer_list<Pt> l)
    {
        for (const auto& p : l) if (n < 8) pts[(size_t) n++] = p;
    }
    double at (double note) const
    {
        if (n == 0) return 0.0;
        if (note <= pts[0].note) return pts[0].value;
        for (int i = 1; i < n; ++i)
            if (note <= pts[(size_t) i].note)
            {
                const auto& a = pts[(size_t) i - 1];
                const auto& b = pts[(size_t) i];
                return lerp (a.value, b.value, (note - a.note) / (b.note - a.note));
            }
        return pts[(size_t) n - 1].value;
    }
    // geometric interpolation (for quantities that scale exponentially across the keyboard)
    double atLog (double note) const
    {
        if (n == 0) return 1.0;
        if (note <= pts[0].note) return pts[0].value;
        for (int i = 1; i < n; ++i)
            if (note <= pts[(size_t) i].note)
            {
                const auto& a = pts[(size_t) i - 1];
                const auto& b = pts[(size_t) i];
                const double t = (note - a.note) / (b.note - a.note);
                return a.value * std::pow (b.value / a.value, t);
            }
        return pts[(size_t) n - 1].value;
    }
};

// Deterministic PRNG (xorshift32); the same seed gives the same instrument on every machine.
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    explicit Rng (uint32_t seed = 0x9e3779b9u) : s (seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double uniform() { return (next() >> 8) * (1.0 / 16777216.0); }        // [0,1)
    double bipolar() { return uniform() * 2.0 - 1.0; }                     // [-1,1)
    float  noise()   { return (float) bipolar(); }
};

// One-pole smoother evaluated at the chunk rate.
struct Smoother
{
    double value = 0, target = 0, coef = 0.2;
    void setTime (double seconds, double sampleRate)
    {
        const double chunksPerSecond = sampleRate / kChunk;
        coef = seconds <= 0 ? 1.0 : 1.0 - std::exp (-1.0 / (seconds * chunksPerSecond));
    }
    void reset (double v) { value = target = v; }
    double advance()
    {
        value += (target - value) * coef;
        if (std::abs (target - value) < 1e-9) value = target;
        return value;
    }
};

// Soft output clipper: linear below ~-1.9 dBFS, tanh-like above, never exceeds 1.
inline float softClip (float x)
{
    constexpr float t = 0.8f;
    const float a = std::abs (x);
    if (a <= t) return x;
    const float over = (a - t) / (1.0f - t);
    const float y = t + (1.0f - t) * 0.999f * std::tanh (over);
    return x < 0 ? -y : y;
}

// Biquad (RBJ), float state, double coefficients computed off the hot path.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    inline float process (float x)
    {
        const float y = (float) (b0 * x) + z1;
        z1 = (float) (b1 * x - a1 * y) + z2;
        z2 = (float) (b2 * x - a2 * y);
        return y;
    }
    void reset() { z1 = z2 = 0; }
    // coefficients only: the filter keeps its own state (assigning a whole Biquad would copy the other's state too)
    void copyCoefficients (const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    void lowShelf (double fs, double f, double dbGain, double q = 0.707)
    {
        const double A = std::pow (10.0, dbGain / 40.0), w = kTwoPi * f / fs, cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2 * q), sa = 2 * std::sqrt (A) * alpha;
        const double a0 = (A + 1) + (A - 1) * cw + sa;
        b0 = A * ((A + 1) - (A - 1) * cw + sa) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
        b2 = A * ((A + 1) - (A - 1) * cw - sa) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
        a2 = ((A + 1) + (A - 1) * cw - sa) / a0;
    }
    void highShelf (double fs, double f, double dbGain, double q = 0.707)
    {
        const double A = std::pow (10.0, dbGain / 40.0), w = kTwoPi * f / fs, cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2 * q), sa = 2 * std::sqrt (A) * alpha;
        const double a0 = (A + 1) - (A - 1) * cw + sa;
        b0 = A * ((A + 1) + (A - 1) * cw + sa) / a0;
        b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
        b2 = A * ((A + 1) + (A - 1) * cw - sa) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
        a2 = ((A + 1) - (A - 1) * cw - sa) / a0;
    }
    void lowPass (double fs, double f, double q = 0.707)
    {
        const double w = kTwoPi * std::min (f, fs * 0.49) / fs, cw = std::cos (w), alpha = std::sin (w) / (2 * q);
        const double a0 = 1 + alpha;
        b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = b0;
        a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
    }
    void highPass (double fs, double f, double q = 0.707)
    {
        const double w = kTwoPi * std::min (f, fs * 0.49) / fs, cw = std::cos (w), alpha = std::sin (w) / (2 * q);
        const double a0 = 1 + alpha;
        b0 = (1 + cw) / 2 / a0; b1 = -(1 + cw) / a0; b2 = b0;
        a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
    }
    void bandPass (double fs, double f, double q)
    {
        const double w = kTwoPi * std::min (f, fs * 0.49) / fs, cw = std::cos (w), alpha = std::sin (w) / (2 * q);
        const double a0 = 1 + alpha;
        b0 = alpha / a0; b1 = 0; b2 = -alpha / a0;
        a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
    }
    void peak (double fs, double f, double dbGain, double q)
    {
        const double A = std::pow (10.0, dbGain / 40.0), w = kTwoPi * f / fs, cw = std::cos (w), alpha = std::sin (w) / (2 * q);
        const double a0 = 1 + alpha / A;
        b0 = (1 + alpha * A) / a0; b1 = -2 * cw / a0; b2 = (1 - alpha * A) / a0;
        a1 = -2 * cw / a0; a2 = (1 - alpha / A) / a0;
    }
    void identity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

struct DcBlocker
{
    float x1 = 0, y1 = 0, r = 0.995f;
    void prepare (double fs) { r = (float) std::exp (-kTwoPi * 10.0 / fs); }
    void reset() { x1 = y1 = 0; }
    inline float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = flushDenormal (y); return y; }
};

// First-order one-pole lowpass in float
struct OnePoleLP
{
    float a = 0.1f, z = 0;
    void set (double fs, double hz) { a = (float) (1.0 - std::exp (-kTwoPi * std::min (hz, fs * 0.45) / fs)); }
    inline float process (float x) { z += a * (x - z); return z; }
};
} // namespace resonique
