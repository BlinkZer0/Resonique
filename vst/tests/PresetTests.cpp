// Factory presets, user files, tuning blocks, favourites.
#include "TestRender.h"
#include "TableStore.h"

using namespace resonique;
using namespace resonique::test;

namespace
{
// Synthesis parameters that make presets genuinely different (not EQ / reverb / mics / master)
const int kSynthParams[] = { instrument, hammerHardness, dynamics, velCurve, stringResonance, soundboardResonance,
                             inharmonicity, stringStiffness, stringDecay, hammerPosition, hammerMass, feltStiffness, feltExponent,
                             unisonDetune, damperEfficiency, bridgeCoupling, soundboardSize, soundboardDamping, soundboardBrightness,
                             noiseMaster, temperament, stretch };

double synthDistance (const PresetData& a, const PresetData& b)
{
    double d = 0;
    for (int i : kSynthParams)
    {
        const auto& def = paramDef (i);
        const double va = std::isnan (a.values[(size_t) i]) ? def.def : a.values[(size_t) i];
        const double vb = std::isnan (b.values[(size_t) i]) ? def.def : b.values[(size_t) i];
        const double na = paramToNorm (def, va), nb = paramToNorm (def, vb);
        if (i == instrument) d += (va != vb) ? 1.0 : 0.0;
        else d += std::abs (na - nb);
    }
    return d;
}
} // namespace

int runPresetTests()
{
    PresetManager pm;
    const auto& f = pm.factory();
    std::printf ("     %d factory presets\n", (int) f.size());
    CHECKF (f.size() >= 190, "%d presets", (int) f.size());

    juce::StringArray cats, names;
    int instrumentsUsed[kNumInstruments] {};
    for (size_t i = 0; i < f.size(); ++i)
    {
        const auto& p = f[i];
        CHECKF (p.name.isNotEmpty() && p.category.isNotEmpty(), "preset %d unnamed", (int) i);
        CHECKF (! names.contains (p.name), "duplicate name %s", p.name.toRawUTF8());
        names.add (p.name);
        cats.addIfNotAlreadyThere (p.category);
        int given = 0;
        for (int k = 0; k < kNumParams; ++k)
        {
            const float v = p.values[(size_t) k];
            if (std::isnan (v)) continue;
            ++given;
            CHECKF (v >= (float) paramDef (k).min && v <= (float) paramDef (k).max, "%s: %s out of range", p.name.toRawUTF8(), paramDef (k).id);
        }
        CHECKF (given >= 4, "%s sets only %d parameters", p.name.toRawUTF8(), given);
        CHECK (std::isnan (p.values[master]));
        if (! std::isnan (p.values[instrument])) instrumentsUsed[(int) p.values[instrument]]++;
    }
    std::printf ("     %d categories:", cats.size());
    for (const auto& c : cats) std::printf (" %s", c.toRawUTF8());
    std::printf ("\n");
    CHECK (cats.size() >= 10);
    for (int i = 0; i < kNumInstruments; ++i) CHECKF (instrumentsUsed[i] >= 3, "instrument %d used by only %d presets", i, instrumentsUsed[i]);

    // every preset differs from every other in its synthesis parameters, not just in EQ / reverb
    int tooClose = 0;
    double minD = 1e9;
    for (size_t i = 0; i < f.size(); ++i)
        for (size_t j = i + 1; j < f.size(); ++j)
        {
            const double d = synthDistance (f[i], f[j]);
            minD = std::min (minD, d);
            if (d < 0.08) { ++tooClose; if (tooClose <= 5) std::printf ("     near-identical: %s / %s (%.3f)\n", f[i].name.toRawUTF8(), f[j].name.toRawUTF8(), d); }
        }
    std::printf ("     smallest synthesis distance between two presets: %.3f\n", minD);
    CHECKF (tooClose == 0, "%d preset pairs are near-identical in their synthesis parameters", tooClose);

    // round trip through a user file (temp folder)
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ResoniqueTests");
    dir.createDirectory();
    {
        PresetData d;
        d.name = "Round Trip";
        d.category = "User";
        d.description = "test";
        for (int k = 0; k < kNumParams; ++k) d.values[(size_t) k] = (float) paramFromNorm (paramDef (k), 0.37);
        auto* t = new juce::DynamicObject();
        juce::Array<juce::var> det; for (int k = 0; k < kNumNotes; ++k) det.add (k == 10 ? 12.5 : 0.0);
        t->setProperty ("detune", det);
        t->setProperty ("scala", "test\n2\n100.0\n2/1\n");
        d.tuning = juce::var (t);
        const auto file = dir.getChildFile ("roundtrip.rqpreset");
        juce::String err;
        CHECK (PresetManager::writeFile (file, d, err));
        PresetData back;
        CHECK (PresetManager::readFile (file, back, err));
        CHECK (back.name == "Round Trip" && back.description == "test");
        for (int k = 0; k < kNumParams; ++k)
        {
            if (paramDef (k).noPreset) { CHECK (std::isnan (back.values[(size_t) k])); continue; }
            CHECKF (std::abs (back.values[(size_t) k] - d.values[(size_t) k]) < 1e-4f * std::max (1.0f, std::abs (d.values[(size_t) k])), "%s", paramDef (k).id);
        }
        CHECK (back.tuning.isObject() && (double) back.tuning["detune"][10] == 12.5);
        CHECK (back.tuning["scala"].toString().startsWith ("test"));
        // partial / old files load with defaults; bad files are rejected
        dir.getChildFile ("partial.rqpreset").replaceWithText ("{ \"format\": \"Resonique Preset\", \"version\": 1, \"name\": \"P\", \"params\": { \"hammerHardness\": 0.9 } }");
        PresetData part;
        CHECK (PresetManager::readFile (dir.getChildFile ("partial.rqpreset"), part, err));
        CHECK (std::abs (part.values[hammerHardness] - 0.9f) < 1e-6f && std::isnan (part.values[inharmonicity]));
        dir.getChildFile ("bad.rqpreset").replaceWithText ("this is not json");
        CHECK (! PresetManager::readFile (dir.getChildFile ("bad.rqpreset"), part, err));
        dir.getChildFile ("empty.rqpreset").replaceWithText ("{ \"params\": {} }");
        CHECK (! PresetManager::readFile (dir.getChildFile ("empty.rqpreset"), part, err));
        dir.getChildFile ("clamp.rqpreset").replaceWithText ("{ \"params\": { \"concertPitch\": 9999, \"instrument\": \"Upright\" } }");
        CHECK (PresetManager::readFile (dir.getChildFile ("clamp.rqpreset"), part, err));
        CHECK (part.values[concertPitch] == 466.0f && part.values[instrument] == 2.0f);
    }
    dir.deleteRecursively();

    // the tuning block drives the tables: a detuned note moves
    {
        TableStore store;
        store.setSampleRate (48000);
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> det; for (int k = 0; k < kNumNotes; ++k) det.add (k == 39 ? 50.0 : 0.0);   // C4
        o->setProperty ("detune", det);
        store.tuningFromJson (juce::var (o));
        CHECK (std::abs (store.tuning.noteDetune[39] - 50.0) < 1e-9);
        store.buildNow (defaultParamValues());
        const auto* t = store.takeIncoming();
        CHECK (t != nullptr);
        if (t != nullptr)
        {
            const double ratio = t->notes[39].f0 / t->notes[38].f0;    // C4 / B3: a semitone plus 50 cents
            CHECKF (std::abs (1200.0 * std::log2 (ratio) - 150.0) < 1.0 + 1200.0 * std::log2 (std::pow (2.0, 1.0 / 1200.0)) * 20, "detune ratio %.1f ct", 1200.0 * std::log2 (ratio));
        }
        CHECK (store.tuningToJson()["detune"].isArray());
        juce::String err;
        CHECK (! store.setScala ("nonsense", err));
        CHECK (store.setScala ("quarter\n24\n50.0\n100.0\n150.0\n200.0\n250.0\n300.0\n350.0\n400.0\n450.0\n500.0\n550.0\n600.0\n650.0\n700.0\n750.0\n800.0\n850.0\n900.0\n950.0\n1000.0\n1050.0\n1100.0\n1150.0\n2/1\n", err));
        CHECK (store.tuning.scala.cents.size() == 24);
        store.collectRetired();
    }

    // a few presets render to a sensible level (level matching sanity)
    for (const char* name : { "Concert Grand", "Upright", "Felt Piano", "Honky-Tonk Saloon", "Ambient Grand" })
    {
        if (pm.findFactory (name) < 0) { std::printf ("     (no preset named %s)\n", name); continue; }
        Patch p; p.preset (pm, name); p.set ("reverbMix", 0.0f);
        auto r = render (p, 1.0, { on (0.0, 48, 100), on (0.0, 60, 100), on (0.0, 67, 100) });
        const double level = dB (rmsSec (r.mono(), 0.02, 0.4));
        CHECKF (level > -30 && level < -3 && r.peak <= 1.0, "%s renders at %.1f dB, peak %.2f", name, level, r.peak);
    }

    std::printf ("   presets: %d failure(s) so far\n", failures);
    return failures;
}
