// Engine tools: --render (diagnostic WAVs + spectra), --loudness (preset level matching), --dump-params.
#include "TestRender.h"
#include "Params.h"

using namespace resonique;
using namespace resonique::test;

int runDumpParams()
{
    std::printf ("%s\n", paramTableToJson().toRawUTF8());
    return 0;
}

int runRender (const juce::String& dirName)
{
    const juce::File dir = juce::File::isAbsolutePath (dirName) ? juce::File (dirName) : juce::File::getCurrentWorkingDirectory().getChildFile (dirName);
    dir.createDirectory();
    std::printf ("rendering diagnostics into %s\n", dir.getFullPathName().toRawUTF8());

    auto save = [&] (const juce::String& name, Render& r, double specT0 = 0.05, double specT1 = 0.55)
    {
        writeWav (dir.getChildFile (name + ".wav"), r);
        writeSpectrumCsv (dir.getChildFile (name + "-spectrum.csv"), r.mono(), specT0, specT1, r.sr);
        std::printf ("  %-32s peak %6.1f dBFS\n", name.toRawUTF8(), dB (r.peak));
    };

    // single notes across the compass at mf
    for (int n : { 24, 36, 48, 60, 72, 84, 96, 108 })
    {
        Patch p; p.set ("micPerspective", 0);
        auto r = render (p, n >= 84 ? 3.0 : 6.0, { on (0.0, n, 90), off (n >= 84 ? 2.0 : 4.5, n) });
        save ("note-" + juce::String (n) + "-v90", r);
    }
    // velocity ladder at C4
    for (int v : { 15, 40, 70, 100, 127 })
    {
        Patch p; p.set ("micPerspective", 0);
        auto r = render (p, 3.0, { on (0.0, 60, v), off (2.5, 60) });
        save ("c4-velocity-" + juce::String (v), r);
    }
    // chords and pedal
    {
        Patch p; p.set ("micPerspective", 0).set ("reverbMix", 0.2f).set ("noiseMaster", 0.5f).set ("earlyReflections", 0.3f);
        auto r = render (p, 8.0, { cc (0.0, 64, 127), on (0.0, 48, 100), on (0.0, 55, 100), on (0.0, 64, 100), on (0.0, 67, 95), on (0.0, 72, 90),
                                   off (2.0, 48), off (2.0, 55), off (2.0, 64), off (2.0, 67), off (2.0, 72), cc (6.0, 64, 0) });
        save ("chord-pedal-room", r, 0.1, 1.1);
        auto r2 = render (p, 6.0, { on (0.0, 36, 110), on (0.5, 43, 100), on (1.0, 52, 100), on (1.5, 60, 100), on (2.0, 67, 100),
                                    off (3.5, 36), off (3.5, 43), off (3.5, 52), off (3.5, 60), off (3.5, 67) });
        save ("arpeggio", r2, 2.0, 3.0);
        auto r3 = render (p, 5.0, { cc (0.0, 64, 64), on (0.0, 57, 100), off (0.4, 57), on (0.5, 60, 100), off (0.9, 60) });
        save ("half-pedal", r3, 1.0, 2.0);
    }
    // instruments
    for (int i = 0; i < kNumInstruments; ++i)
    {
        Patch p; p.set ("instrument", (float) i).set ("micPerspective", 0).set ("noiseMaster", 0.5f);
        auto r = render (p, 4.0, { on (0.0, 48, 95), on (0.0, 60, 95), on (0.0, 64, 95), on (0.0, 67, 95), off (3.0, 48), off (3.0, 60), off (3.0, 64), off (3.0, 67) });
        save ("instrument-" + juce::String (i) + "-" + juce::String (instrumentChoices()[(size_t) i]).replaceCharacter (' ', '_').toLowerCase(), r);
    }
    return 0;
}

// Loudness of every factory preset: the loudest 100 ms RMS of a mf chord (dBFS), as CSV.
int runLoudness()
{
    PresetManager pm;
    std::printf ("name,category,loudest_100ms_dbfs,peak_dbfs\n");
    for (const auto& preset : pm.factory())
    {
        Patch p;
        for (int k = 0; k < kNumParams; ++k)
            if (! paramDef (k).noPreset && ! std::isnan (preset.values[(size_t) k])) p.v[(size_t) k] = preset.values[(size_t) k];
        p.v[presetGain] = 0;
        auto r = render (p, 1.5, { on (0.0, 48, 96), on (0.0, 55, 96), on (0.0, 64, 96), on (0.0, 72, 96) });
        auto m = r.mono();
        double loudest = 0;
        for (double t = 0; t + 0.1 <= 1.5; t += 0.025) loudest = std::max (loudest, rmsSec (m, t, t + 0.1));
        std::printf ("\"%s\",\"%s\",%.2f,%.2f\n", preset.name.toRawUTF8(), preset.category.toRawUTF8(), dB (loudest), dB (r.peak));
    }
    return 0;
}
