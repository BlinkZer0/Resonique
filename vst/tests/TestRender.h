#pragma once
// Offline rendering + analysis helpers shared by the console tests and tools.
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <functional>
#include <memory>
#include <vector>
#include "PianoEngine.h"
#include "dsp/ModelBuilder.h"
#include "PresetManager.h"

extern int failures;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECKF(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

namespace resonique::test
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

struct Patch
{
    ParamValues v = defaultParamValues();
    TuningSettings tuning;
    Patch()
    {
        // analysis defaults: no room, no mechanical noise, flat tone, mono-ish mics
        v[reverbMix] = 0; v[noiseMaster] = 0; v[earlyReflections] = 0; v[micPerspective] = 4;
        tuning.stretch = 0;
        v[stretch] = 0;
    }
    Patch& set (const char* id, float value)
    {
        const int i = paramIndexForId (id);
        jassert (i >= 0);
        if (i >= 0) v[(size_t) i] = value;
        return *this;
    }
    Patch& set (int index, float value) { v[(size_t) index] = value; return *this; }
    Patch& preset (const PresetManager& pm, const juce::String& name)
    {
        const int i = pm.findFactory (name);
        jassert (i >= 0);
        if (i >= 0)
            for (int k = 0; k < kNumParams; ++k)
                if (! paramDef (k).noPreset && ! std::isnan (pm.factory()[(size_t) i].values[(size_t) k]))
                    v[(size_t) k] = pm.factory()[(size_t) i].values[(size_t) k];
        return *this;
    }
};

struct Event { int sample; std::function<void (PianoEngine&)> fn; };
inline Event on (double seconds, int note, int vel = 100, double sr = kSr) { return { (int) (seconds * sr), [note, vel] (PianoEngine& e) { e.noteOn (note, vel); } }; }
inline Event off (double seconds, int note, double sr = kSr) { return { (int) (seconds * sr), [note] (PianoEngine& e) { e.noteOff (note); } }; }
inline Event cc (double seconds, int ctl, int value, double sr = kSr) { return { (int) (seconds * sr), [ctl, value] (PianoEngine& e) { e.controller (ctl, value); } }; }
inline Event bend (double seconds, double semis, double sr = kSr) { return { (int) (seconds * sr), [semis] (PianoEngine& e) { e.pitchBend (semis); } }; }

struct Render
{
    std::vector<float> L, R;
    double peak = 0;
    int activeAtEnd = 0;
    double sr = kSr;
    std::vector<float> mono() const
    {
        std::vector<float> m (L.size());
        for (size_t i = 0; i < L.size(); ++i) m[i] = 0.5f * (L[i] + R[i]);
        return m;
    }
};

// An engine with tables built synchronously for the patch (heap: the engine is ~2 MB)
struct Setup
{
    std::unique_ptr<InstrumentTables> tables;
    std::unique_ptr<PianoEngine> engine;
    Setup (const Patch& p, double sr = kSr)
    {
        tables = ModelBuilder::build (p.v, p.tuning, sr, 1);
        engine = std::make_unique<PianoEngine>();
        engine->setParamValues (p.v.data());
        engine->prepare (sr);
        engine->setTables (tables.get());
    }
};

// Renders `seconds`; events fire at their sample (block-aligned to kBlock). `patch.v` is re-read every block.
inline Render render (Patch& patch, double seconds, const std::vector<Event>& events = {}, double sr = kSr,
                      std::function<void (PianoEngine&, int)> perBlock = nullptr)
{
    Setup s (patch, sr);
    auto& engine = *s.engine;
    Render r;
    r.sr = sr;
    const int total = (int) (seconds * sr);
    r.L.assign ((size_t) total, 0.0f);
    r.R.assign ((size_t) total, 0.0f);
    int pos = 0;
    while (pos < total)
    {
        const int n = std::min (kBlock, total - pos);
        for (const auto& ev : events) if (ev.sample >= pos && ev.sample < pos + n) ev.fn (engine);
        engine.setParamValues (patch.v.data());
        if (perBlock) perBlock (engine, pos);
        engine.render (r.L.data() + pos, r.R.data() + pos, n);
        pos += n;
    }
    for (size_t i = 0; i < r.L.size(); ++i) r.peak = std::max (r.peak, (double) std::max (std::abs (r.L[i]), std::abs (r.R[i])));
    r.activeAtEnd = engine.activeVoices();
    return r;
}

inline double rms (const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min (to, x.size());
    if (to <= from) return 0;
    double acc = 0;
    for (size_t i = from; i < to; ++i) acc += (double) x[i] * x[i];
    return std::sqrt (acc / (double) (to - from));
}
inline double rmsSec (const std::vector<float>& x, double t0, double t1, double sr = kSr) { return rms (x, (size_t) (t0 * sr), (size_t) (t1 * sr)); }
inline double dB (double a) { return 20.0 * std::log10 (std::max (a, 1e-12)); }

// Magnitude at one frequency (Goertzel, Hann window), normalised so a full-scale sine reads about 0.5
inline double magAt (const std::vector<float>& x, size_t from, size_t to, double hz, double sr = kSr)
{
    to = std::min (to, x.size());
    if (to <= from) return 0;
    const size_t n = to - from;
    const double w = kTwoPi * hz / sr;
    const double coef = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) n);
        s0 = x[from + i] * win + coef * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
    return std::sqrt (re * re + im * im) / (double) n * 2.0;
}
inline double magSec (const std::vector<float>& x, double t0, double t1, double hz, double sr = kSr) { return magAt (x, (size_t) (t0 * sr), (size_t) (t1 * sr), hz, sr); }

// Frequency of the strongest component between lo and hi (coarse scan, then refine)
inline double peakFreq (const std::vector<float>& x, size_t from, size_t to, double lo, double hi, double sr = kSr)
{
    double best = lo, bestMag = -1;
    const double step = std::max (0.05, (hi - lo) / 400.0);
    for (double f = lo; f <= hi; f += step)
    {
        const double m = magAt (x, from, to, f, sr);
        if (m > bestMag) { bestMag = m; best = f; }
    }
    for (double f = best - step; f <= best + step; f += step / 40.0)
    {
        const double m = magAt (x, from, to, f, sr);
        if (m > bestMag) { bestMag = m; best = f; }
    }
    return best;
}
inline double peakFreqSec (const std::vector<float>& x, double t0, double t1, double lo, double hi, double sr = kSr)
{
    return peakFreq (x, (size_t) (t0 * sr), (size_t) (t1 * sr), lo, hi, sr);
}

// T60 of one partial: fits the dB slope of its magnitude between t0 and t1 (windows of `win` seconds)
inline double t60At (const std::vector<float>& x, double hz, double t0, double t1, double win = 0.25, double sr = kSr)
{
    std::vector<double> ts, dbs;
    for (double t = t0; t + win <= t1; t += win)
    {
        const double m = magSec (x, t, t + win, hz, sr);
        if (m < 1e-9) break;
        ts.push_back (t + win / 2); dbs.push_back (dB (m));
    }
    if (ts.size() < 2) return 0;
    double mt = 0, md = 0;
    for (size_t i = 0; i < ts.size(); ++i) { mt += ts[i]; md += dbs[i]; }
    mt /= (double) ts.size(); md /= (double) ts.size();
    double num = 0, den = 0;
    for (size_t i = 0; i < ts.size(); ++i) { num += (ts[i] - mt) * (dbs[i] - md); den += (ts[i] - mt) * (ts[i] - mt); }
    const double slope = den > 0 ? num / den : 0;   // dB per second
    return slope < 0 ? -60.0 / slope : 1e9;
}

// Spectral centroid (Hz) of a window, from the FFT magnitude (the FFT covers the whole window)
inline double centroid (const std::vector<float>& x, size_t from, size_t to, double sr = kSr)
{
    to = std::min (to, x.size());
    int order = 10;
    while ((size_t) (1 << order) < to - from && order < 20) ++order;
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    to = std::min (to, x.size());
    for (size_t i = from; i < to && i - from < (size_t) N; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (kTwoPi * (double) (i - from) / (double) std::min ((size_t) N, to - from));
        buf[i - from] = (float) (x[i] * win);
    }
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double hz = k * sr / N;
        if (hz > 20000.0) break;              // the audible band only, so rates compare
        const double m = buf[(size_t) k];
        num += m * hz; den += m;
    }
    return den > 0 ? num / den : 0;
}

// Largest sample-to-sample step (click detector)
inline double maxStep (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, x.size());
    double m = 0;
    for (size_t i = from + 1; i < to; ++i) m = std::max (m, (double) std::abs (x[i] - x[i - 1]));
    return m;
}

inline bool allFinite (const std::vector<float>& x)
{
    for (const float v : x) if (! std::isfinite (v)) return false;
    return true;
}

inline bool writeWav (const juce::File& f, const Render& r)
{
    f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (f);
    const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (r.sr).withNumChannels (2).withBitsPerSample (24);
    auto w = fmt.createWriterFor (out, opts);
    if (w == nullptr) return false;
    juce::AudioBuffer<float> b (2, (int) r.L.size());
    for (int i = 0; i < (int) r.L.size(); ++i) { b.setSample (0, i, r.L[(size_t) i]); b.setSample (1, i, r.R[(size_t) i]); }
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Spectrum CSV: frequency, dB (Hann window, 32768 points) of a window of the mono signal
inline bool writeSpectrumCsv (const juce::File& f, const std::vector<float>& x, double t0, double t1, double sr = kSr)
{
    const int order = 15, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    const size_t from = (size_t) (t0 * sr), to = std::min (x.size(), (size_t) (t1 * sr));
    const size_t len = std::min ((size_t) N, to - from);
    for (size_t i = 0; i < len; ++i)
        buf[i] = (float) (x[from + i] * (0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) len)));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    juce::String s ("frequency_hz,level_db\n");
    for (int k = 1; k < N / 2; ++k)
    {
        const double hz = k * sr / N;
        if (hz > 20000) break;
        s << juce::String (hz, 2) << "," << juce::String (dB (buf[(size_t) k] / (double) len * 4.0), 2) << "\n";
    }
    return f.replaceWithText (s);
}
} // namespace resonique::test
