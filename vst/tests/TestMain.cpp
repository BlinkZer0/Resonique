// ResoniqueTests: console test tool.
//   ResoniqueTests                       run every test (DSP, acoustic, presets)
//   ResoniqueTests --dsp | --acoustic | --presets
//   ResoniqueTests --bench               CPU benchmark
//   ResoniqueTests --render <dir>        diagnostic WAVs + spectrum CSVs
//   ResoniqueTests --loudness            loudness of every factory preset (level matching)
//   ResoniqueTests --dump-params         the parameter table as JSON (parity with params.js)
//   ResoniqueTests --host-check <path to Resonique.vst3>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cstdio>

int failures = 0;

int runDspTests();
int runAcousticTests();
int runPresetTests();
int runBench();
int runRender (const juce::String& dir);
int runLoudness();
int runDumpParams();
int runHostCheck (const juce::String& path);
int runCompare (const juce::String& folder, int instrumentIndex, const juce::String& csvOut, const juce::StringArray& overrides);
int runProbeVoice (int note, int vel);
int runProbeStability();
int runInstrumentTable (bool check);

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);

    if (args.contains ("--dump-params")) return runDumpParams();
    if (args.contains ("--bench")) return runBench();
    if (args.contains ("--loudness")) return runLoudness();
    if (args.contains ("--render"))
    {
        const int i = args.indexOf ("--render");
        return runRender (i + 1 < args.size() ? args[i + 1] : juce::String ("renders"));
    }
    if (args.contains ("--probe-stability")) return runProbeStability();
    if (args.contains ("--instruments")) return runInstrumentTable (false);
    if (args.contains ("--probe-voice"))
    {
        const int i = args.indexOf ("--probe-voice");
        return runProbeVoice (i + 1 < args.size() ? args[i + 1].getIntValue() : 60, i + 2 < args.size() ? args[i + 2].getIntValue() : 76);
    }
    if (args.contains ("--compare"))
    {
        const int i = args.indexOf ("--compare");
        if (i + 1 >= args.size()) { std::printf ("usage: --compare <folder of reference notes> [--instrument N] [--out file.csv]\n"); return 2; }
        const int inst = args.contains ("--instrument") ? args[args.indexOf ("--instrument") + 1].getIntValue() : 0;
        const juce::String out = args.contains ("--out") ? args[args.indexOf ("--out") + 1] : juce::String();
        juce::StringArray overrides;   // --set id=value (repeatable)
        for (int k = 0; k + 1 < args.size(); ++k) if (args[k] == "--set") overrides.add (args[k + 1]);
        return runCompare (args[i + 1], inst, out, overrides);
    }
    if (args.contains ("--host-check"))
    {
        const int i = args.indexOf ("--host-check");
        if (i + 1 >= args.size()) { std::printf ("usage: --host-check <Resonique.vst3>\n"); return 2; }
        return runHostCheck (args[i + 1]);
    }

    const bool all = args.isEmpty();
    if (all || args.contains ("--dsp")) { std::printf ("== DSP tests ==\n"); runDspTests(); }
    if (all || args.contains ("--acoustic")) { std::printf ("== Acoustic tests ==\n"); runAcousticTests(); }
    if (all || args.contains ("--presets")) { std::printf ("== Preset tests ==\n"); runPresetTests(); }
    if (all || args.contains ("--instruments-check")) { std::printf ("== Instrument library ==\n"); runInstrumentTable (true); }

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
