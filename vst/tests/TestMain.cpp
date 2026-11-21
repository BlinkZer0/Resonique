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

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
