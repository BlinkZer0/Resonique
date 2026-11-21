// --host-check <path to Resonique.vst3>
// Loads the built plugin the way a DAW does (JUCE's VST3 hosting) and checks:
//   - it scans as an instrument with MIDI in and stereo out
//   - every parameter is exposed, with names and text values
//   - MIDI notes produce sound; automation during playback is safe
//   - state save -> restore into a fresh instance gives identical parameters
//   - programs (factory presets) switch and report names
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/ParamTable.h"

int runHostCheck (const juce::String& path)
{
    int fails = 0;
    auto check = [&fails] (bool ok, const char* what) { if (! ok) ++fails; std::printf ("%-4s %s\n", ok ? "ok" : "FAIL", what); };

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, path);
    check (types.size() == 1, "scans: one plugin in the bundle");
    if (types.isEmpty()) return 1;
    const auto& desc = *types[0];
    std::printf ("     name '%s', manufacturer '%s', category '%s', version %s\n", desc.name.toRawUTF8(),
                 desc.manufacturerName.toRawUTF8(), desc.category.toRawUTF8(), desc.version.toRawUTF8());
    check (desc.isInstrument, "is an instrument");
    check (desc.name == "Resonique", "name is 'Resonique'");

    juce::String error;
    auto make = [&] { return format.createInstanceFromDescription (desc, 48000.0, 512, error); };
    auto plugin = make();
    check (plugin != nullptr, "instantiates");
    if (plugin == nullptr) { std::printf ("     %s\n", error.toRawUTF8()); return 1; }

    check (plugin->acceptsMidi(), "accepts MIDI");
    check (plugin->getMainBusNumOutputChannels() == 2, "stereo output");

    auto& params = plugin->getParameters();
    int named = 0;
    for (auto* p : params)
        if (p->getName (64).isNotEmpty() && p->getCurrentValueAsText().isNotEmpty()) ++named;
    std::printf ("     %d host parameters (%d in the plugin's table)\n", params.size(), resonique::kNumParams);
    check (params.size() >= resonique::kNumParams, "every parameter is exposed");
    check (named == params.size(), "every parameter has a name and a text value");
    check (plugin->getNumPrograms() >= 100, "factory presets are programs");
    check (plugin->getProgramName (0).isNotEmpty(), "programs have names");

    plugin->setPlayConfigDetails (0, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    double energy = 0, peak = 0;
    juce::AudioProcessorParameter* hardness = nullptr;
    juce::AudioProcessorParameter* width = nullptr;
    for (auto* p : params)
    {
        if (p->getName (64) == "Hammer Hardness") hardness = p;
        if (p->getName (64) == "Stereo Width") width = p;
    }
    check (hardness != nullptr && width != nullptr, "parameters found by host name");
    for (int b = 0; b < 300; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 2) { midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 10); }
        if (b == 10) midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
        if (b == 150) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        if (b > 20 && b < 120 && hardness != nullptr) hardness->setValueNotifyingHost ((float) (b % 20) / 20.0f);
        if (b > 20 && b < 120 && width != nullptr) width->setValueNotifyingHost (0.3f + 0.3f * (float) std::sin (b * 0.1));
        buf.clear();
        plugin->processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
            {
                const double x = buf.getSample (ch, i);
                energy += x * x;
                peak = std::max (peak, std::abs (x));
            }
    }
    std::printf ("     rendered 300 blocks: energy %.3f, peak %.3f\n", energy, peak);
    check (energy > 0.5, "notes make sound");
    check (peak <= 1.0, "output never exceeds 0 dBFS");
    check (std::isfinite (energy), "no NaN / inf");

    if (hardness != nullptr) hardness->setValueNotifyingHost (0.77f);
    if (width != nullptr) width->setValueNotifyingHost (0.33f);
    plugin->setCurrentProgram (juce::jmin (3, plugin->getNumPrograms() - 1));
    if (hardness != nullptr) hardness->setValueNotifyingHost (0.77f);
    if (width != nullptr) width->setValueNotifyingHost (0.33f);
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    check (state.getSize() > 500, "state is saved");
    std::printf ("     state size %d bytes, program %d\n", (int) state.getSize(), plugin->getCurrentProgram());

    auto fresh = make();
    check (fresh != nullptr, "second instance");
    if (fresh != nullptr)
    {
        fresh->setStateInformation (state.getData(), (int) state.getSize());
        int same = 0, diff = 0;
        const auto& a = plugin->getParameters();
        const auto& b = fresh->getParameters();
        for (int i = 0; i < juce::jmin (a.size(), b.size()); ++i)
        {
            if (a[i]->getName (64).startsWith ("MIDI CC")) continue;   // JUCE's VST3 wrapper exposes the MIDI controllers as parameters; they are not state
            if (std::abs (a[i]->getValue() - b[i]->getValue()) < 1e-6f) ++same;
            else { ++diff; if (diff <= 5) std::printf ("     differs: %s %g vs %g\n", a[i]->getName (64).toRawUTF8(), a[i]->getValue(), b[i]->getValue()); }
        }
        std::printf ("     %d parameters restored, %d differ\n", same, diff);
        check (diff == 0, "state restores every parameter");
        check (fresh->getCurrentProgram() == plugin->getCurrentProgram(), "state restores the program");
    }

    plugin->releaseResources();
    std::printf (fails == 0 ? "HOST CHECK OK\n" : "HOST CHECK: %d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
