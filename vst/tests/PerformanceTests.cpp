// --bench: CPU cost of the engine at different polyphony levels, and table build time.
#include "TestRender.h"

using namespace resonique;
using namespace resonique::test;

int runBench()
{
    std::printf ("== Benchmark (48 kHz, %d-sample blocks) ==\n", kBlock);
    {
        Patch p;
        const auto t0 = juce::Time::getHighResolutionTicks();
        auto t = ModelBuilder::build (p.v, p.tuning, kSr, 1);
        const double ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0;
        std::printf ("table build: %.1f ms (all 88 notes, %d soundboard modes)\n", ms, kSoundboardModes);
    }
    struct Case { const char* name; int voices; bool pedal; };
    const Case cases[] = { { "16 voices", 16, false }, { "32 voices, pedal", 32, true }, { "64 voices, pedal", 64, true }, { "128 voices, pedal", 128, true } };
    for (const auto& c : cases)
    {
        Patch p; p.set ("polyphony", 128).set ("reverbMix", 0.2f).set ("noiseMaster", 0.5f).set ("micPerspective", 0);
        Setup s (p);
        auto& e = *s.engine;
        if (c.pedal) e.controller (64, 127);
        float L[kBlock], R[kBlock];
        // strike the notes spread over the compass, then measure the steady-state cost
        for (int i = 0; i < c.voices; ++i) e.noteOn (kFirstNote + (i * 7) % kNumNotes, 100);
        for (int b = 0; b < 20; ++b) e.render (L, R, kBlock);
        const double seconds = 5.0;
        const int blocks = (int) (seconds * kSr / kBlock);
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int b = 0; b < blocks; ++b)
        {
            if (b % 400 == 0) for (int i = 0; i < c.voices; ++i) e.noteOn (kFirstNote + (i * 7) % kNumNotes, 100);   // keep them fresh
            e.render (L, R, kBlock);
        }
        const double el = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        std::printf ("%-20s %5.1f%% of one core  (%d voices, %d partials active)\n", c.name, 100.0 * el / seconds, e.activeVoices(), e.activePartials());
    }
    return 0;
}
