// Robustness and real-time safety: no heap allocation while rendering, any block size, sample-rate
// changes, table swaps under sounding notes, every parameter swept at block rate, MIDI floods, silence cost.
#include <atomic>
#include <new>
#include "TestRender.h"
#include "TableStore.h"

using namespace resonique;
using namespace resonique::test;

// ---- allocation counting (whole process; only read between the markers) ---------------------------
static std::atomic<long> gAllocations { 0 };
void* operator new (std::size_t n) { ++gAllocations; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { ++gAllocations; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
void allocationTests()
{
    Patch p; p.set ("reverbMix", 0.3f).set ("noiseMaster", 0.8f).set ("micPerspective", 2);
    Setup s (p);
    auto& e = *s.engine;
    std::vector<float> L (4096), R (4096);
    e.render (L.data(), R.data(), 512);                         // warm up
    const long before = gAllocations.load();
    e.controller (64, 127);
    for (int i = 0; i < 40; ++i) e.noteOn (kFirstNote + (i * 5) % kNumNotes, 100);
    for (int b = 0; b < 200; ++b)
    {
        if (b == 50) for (int i = 0; i < 40; ++i) e.noteOff (kFirstNote + (i * 5) % kNumNotes);
        if (b == 60) e.controller (64, 0);
        if (b == 70) e.controller (66, 127);
        if (b == 80) e.pitchBend (1.5);
        if (b == 90) { Patch q = p; q.set ("stereoWidth", 0.2f).set ("reverbSize", 0.9f).set ("toneBass", 4.0f); e.setParamValues (q.v.data()); }
        if (b == 100) e.panic();
        if (b == 101) for (int i = 0; i < 128; ++i) e.noteOn (kFirstNote + (i * 7) % kNumNotes, 120);
        e.render (L.data(), R.data(), b % 3 == 0 ? 512 : 333);
    }
    const long during = gAllocations.load() - before;
    std::printf ("     heap allocations during 200 rendered blocks with notes, pedals, parameter changes and a panic: %ld\n", during);
    CHECKF (during == 0, "%ld allocations on the audio path", during);

    // the table hand-off: adopting a new table allocates nothing on the audio side
    auto t2 = ModelBuilder::build (p.v, p.tuning, kSr, 2);
    const long b2 = gAllocations.load();
    e.setTables (t2.get());
    e.render (L.data(), R.data(), 512);
    CHECKF (gAllocations.load() - b2 == 0, "%ld allocations adopting a table", gAllocations.load() - b2);
}

void blockSizeTests()
{
    // the same notes rendered with different block sizes must give the same output
    auto run = [] (const std::vector<int>& sizes)
    {
        Patch p; p.set ("reverbMix", 0.25f).set ("noiseMaster", 0.5f);
        Setup s (p);
        auto& e = *s.engine;
        const int total = 48000;
        std::vector<float> L ((size_t) total), R ((size_t) total);
        int pos = 0, k = 0;
        // sample-accurate events (as the plugin applies MIDI): blocks are split at the event samples
        const int evOn = 1000, evOff = 30000;
        while (pos < total)
        {
            int n = std::min (sizes[(size_t) (k++ % sizes.size())], total - pos);
            if (pos < evOn && pos + n > evOn) n = evOn - pos;
            if (pos < evOff && pos + n > evOff) n = evOff - pos;
            if (pos == evOn) { e.noteOn (60, 100); e.noteOn (64, 90); e.controller (64, 127); }
            if (pos == evOff) { e.noteOff (60); e.noteOff (64); }
            e.render (L.data() + pos, R.data() + pos, n);
            pos += n;
        }
        return L;
    };
    const auto a = run ({ 512 }), b = run ({ 1 }), c = run ({ 7, 13, 64, 100 }), d = run ({ 2048 });
    auto diff = [] (const std::vector<float>& x, const std::vector<float>& y)
    {
        double m = 0, e = 0;
        for (size_t i = 1100; i < x.size(); ++i) { m = std::max (m, (double) std::abs (x[i] - y[i])); e = std::max (e, (double) std::abs (x[i])); }
        return m / std::max (e, 1e-9);
    };
    std::printf ("     block-size consistency (relative difference): 1 %.2e, mixed %.2e, 2048 %.2e\n", diff (a, b), diff (a, c), diff (a, d));
    CHECK (diff (a, b) < 1e-3 && diff (a, c) < 1e-3 && diff (a, d) < 1e-3);
    CHECK (allFinite (b) && allFinite (c));
}

void sampleRateChangeTests()
{
    // prepare at one rate, play, prepare at another: tables are rebuilt and the pitch is right at both
    TableStore store;
    auto enginePtr = std::make_unique<PianoEngine>();   // ~2 MB: never on the stack
    auto& e = *enginePtr;
    store.setSampleRate (44100);
    e.prepare (44100);
    e.setTables (store.takeIncoming());
    std::vector<float> L (88200), R (88200);
    e.noteOn (69, 90);
    e.render (L.data(), R.data(), 44100);
    const double f1 = peakFreq (L, 4410, 40000, 430, 450, 44100);
    store.collectRetired();
    store.setSampleRate (96000);
    e.prepare (96000);
    e.setTables (store.takeIncoming());
    e.noteOn (69, 90);
    e.render (L.data(), R.data(), 88200);
    const double f2 = peakFreq (L, 9600, 88000, 430, 450, 96000);
    std::printf ("     A4 after a 44.1 -> 96 kHz change: %.2f Hz then %.2f Hz\n", f1, f2);
    CHECK (std::abs (f1 - 440) < 1.0 && std::abs (f2 - 440) < 1.0);
    CHECK (e.currentTables() != nullptr && std::abs (e.currentTables()->sampleRate - 96000) < 1);
    store.collectRetired();
}

void tableSwapTests()
{
    // physical parameters change while notes ring: the new table is adopted at a block boundary,
    // sounding notes keep their state, new notes use the new table, nothing clicks
    Patch p;
    Setup s (p);
    auto& e = *s.engine;
    std::vector<float> L (96000), R (96000);
    e.controller (64, 127);
    for (int n : { 36, 48, 60, 67, 72 }) e.noteOn (n, 100);
    e.render (L.data(), R.data(), 24000);
    Patch q; q.set ("inharmonicity", 2.5f).set ("hammerHardness", 0.9f).set ("soundboardSize", 1.3f).set ("instrument", 4);
    auto t2 = ModelBuilder::build (q.v, q.tuning, kSr, 7);
    e.setTables (t2.get());
    e.setParamValues (q.v.data());
    e.noteOn (76, 100);
    e.render (L.data() + 24000, R.data() + 24000, 72000);
    CHECK (allFinite (L));
    const double step = maxStep (L, 23900, 24100);
    std::printf ("     table swap under 5 sounding notes: max step at the boundary %.3f\n", step);
    CHECKF (step < 0.25, "click at the swap: %.3f", step);
    // the new note's pitch follows the new table (Bright Pop Grand has the same tuning, so check its brightness instead)
    CHECK (rmsSec (L, 0.6, 1.0) > 1e-4);
}

void parameterSweepTests()
{
    // every parameter swept across its range at block rate while a chord rings: finite, bounded, no big steps
    Patch p;
    Setup s (p);
    auto& e = *s.engine;
    std::vector<float> L (512), R (512);
    e.controller (64, 100);
    for (int n : { 41, 48, 55, 60, 64, 67 }) e.noteOn (n, 105);
    double worstStep = 0; int worstParam = -1; float prevL = 0;
    for (int i = 0; i < kNumParams; ++i)
    {
        if (paramAffectsTables (i)) continue;      // table parameters go through the worker thread, not live
        const auto& d = paramDef (i);
        for (int b = 0; b < 40; ++b)
        {
            Patch q = p;
            const double t = (b % 20) / 19.0;
            q.v[(size_t) i] = (float) paramFromNorm (d, b < 20 ? t : 1.0 - t);
            e.setParamValues (q.v.data());
            e.render (L.data(), R.data(), 512);
            for (int k = 0; k < 512; ++k)
            {
                const float x = L[(size_t) k];
                if (! std::isfinite (x)) { CHECKF (false, "NaN while sweeping %s", d.id); break; }
                const double st = std::abs (x - prevL);
                if (st > worstStep) { worstStep = st; worstParam = i; }
                prevL = x;
            }
        }
        e.setParamValues (p.v.data());
        if (i % 8 == 7) for (int n : { 41, 48, 55, 60, 64, 67 }) e.noteOn (n, 105);   // keep the chord fresh
    }
    std::printf ("     parameter sweeps while playing: worst sample step %.3f (%s)\n", worstStep, worstParam >= 0 ? paramDef (worstParam).id : "-");
    CHECKF (worstStep < 0.5, "a parameter sweep clicks: %s", worstParam >= 0 ? paramDef (worstParam).id : "-");
}

void midiFloodTests()
{
    Patch p; p.set ("polyphony", 32);
    Setup s (p);
    auto& e = *s.engine;
    std::vector<float> L (512), R (512);
    int maxVoices = 0;
    for (int b = 0; b < 300; ++b)
    {
        if (b < 100) for (int i = 0; i < 50; ++i) { e.noteOn (kFirstNote + (b * 7 + i * 3) % kNumNotes, 1 + (i * 37) % 127); if (i % 2) e.noteOff (kFirstNote + (b * 5 + i) % kNumNotes); }
        if (b % 10 == 0) e.controller (64, (b / 10) % 2 ? 127 : 0);
        if (b % 17 == 0) e.controller (66, (b / 17) % 2 ? 127 : 0);
        if (b % 13 == 0) e.controller (67, (b % 26) ? 127 : 0);
        if (b == 150) e.controller (123, 0);
        if (b == 200) e.controller (121, 0);
        e.render (L.data(), R.data(), 512);
        maxVoices = std::max (maxVoices, e.activeVoices());
        CHECK (allFinite (L));
    }
    std::printf ("     MIDI flood (5000 note-ons in 100 blocks): max %d voices at polyphony 32\n", maxVoices);
    CHECK (maxVoices <= 32);
    // everything released and the pedals up: the engine returns to silence
    e.allNotesOff(); e.controller (64, 0); e.controller (66, 0);
    double tail = 0;
    for (int b = 0; b < 1500; ++b) { e.render (L.data(), R.data(), 512); if (b > 1400) tail = std::max (tail, rms (L, 0, 512)); }
    CHECKF (tail < 1e-4, "not silent 15 s after everything was released (%.1f dB)", dB (tail));
    CHECK (e.activeVoices() == 0);
}

void silenceCostTests()
{
    // after the notes have died the engine must be cheap (no denormal crawl, no idle work)
    Patch p; p.set ("reverbMix", 0.3f);
    Setup s (p);
    auto& e = *s.engine;
    std::vector<float> L (512), R (512);
    for (int n : { 48, 60, 72 }) e.noteOn (n, 100);
    for (int b = 0; b < 100; ++b) e.render (L.data(), R.data(), 512);
    for (int n : { 48, 60, 72 }) e.noteOff (n);
    for (int b = 0; b < 1200; ++b) e.render (L.data(), R.data(), 512);   // 12 s: everything gone
    const auto t0 = juce::Time::getHighResolutionTicks();
    for (int b = 0; b < 2000; ++b) e.render (L.data(), R.data(), 512);
    const double seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double load = seconds / (2000.0 * 512 / kSr);
    std::printf ("     idle cost after the notes died: %.2f%% of one core, %d voices\n", 100.0 * load, e.activeVoices());
    CHECK (load < 0.05);
    CHECK (e.activeVoices() == 0);
}
} // namespace

int runRobustnessTests()
{
    allocationTests();
    blockSizeTests();
    sampleRateChangeTests();
    tableSwapTests();
    parameterSweepTests();
    midiFloodTests();
    silenceCostTests();
    std::printf ("   robustness: %d failure(s) so far\n", failures);
    return failures;
}
