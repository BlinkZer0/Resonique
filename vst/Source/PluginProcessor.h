#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "PresetManager.h"
#include "TableStore.h"
#include "PianoEngine.h"
#include "MidiRouting.h"

class ResoniqueProcessor : public juce::AudioProcessor,
                           private juce::Timer,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
    ResoniqueProcessor();
    ~ResoniqueProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    // Factory presets are host programs.
    int getNumPrograms() override { return (int) presetManager.factory().size(); }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Presets (message thread)
    struct PresetInfo { juce::String id, name, category, description; };
    PresetInfo currentPreset() const;
    std::atomic<int> presetVersion { 0 };   // bumps whenever currentPreset() changes

    bool loadPresetById (const juce::String& id, juce::String& error);
    bool loadPresetFile (const juce::File& file, juce::String& error);
    bool saveUserPreset (const juce::String& name, const juce::String& category, bool overwrite, bool& alreadyExists, juce::String& error);
    bool exportPreset (const juce::File& file, juce::String& error);
    bool deleteUserPreset (const juce::String& id, juce::String& error);
    void initPreset();          // every parameter to its default, tuning cleared
    void markDirty();           // the UI changed something since the preset loaded

    resonique::PresetManager presetManager;

    //==============================================================================
    // Tuning editor (message thread)
    resonique::TableStore tables;
    void setNoteDetune (int noteIndex, double cents);
    void resetDetune();
    bool loadScalaFile (const juce::File&, juce::String& error);
    bool loadScalaText (const juce::String& text, juce::String& error);
    void clearScala();
    void tuningChanged();       // after editing `tables.tuning`

    //==============================================================================
    // Impulse-response reverb (message thread). The algorithmic reverb is the fallback.
    bool loadImpulseResponse (const juce::File&, juce::String& error);
    void clearImpulseResponse();
    juce::String impulseResponseName() const { return irName; }
    std::atomic<int> irVersion { 0 };

    //==============================================================================
    // Used by the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // UI notes in, all sounding notes out
    juce::RangedAudioParameter* param (int index) const { return params[(size_t) index]; }
    float paramValue (int index) const { return rawValues[(size_t) index]->load(); }
    void requestPanic() { panicRequested = true; }
    void requestController (int cc, int value) { pendingCc[(size_t) juce::jlimit (0, 127, cc)].store (juce::jlimit (0, 127, value)); }

    std::atomic<int>   statVoices { 0 };
    std::atomic<int>   statPartials { 0 };
    std::atomic<float> statPeak { 0.0f };
    std::atomic<float> statSustain { 0.0f };
    std::atomic<bool>  statSostenuto { false };
    std::atomic<float> statSoft { 0.0f };
    std::atomic<float> statCpu { 0.0f };
    std::atomic<double> statSampleRate { 0.0 };

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& id, float newValue) override;
    void applyPreset (const resonique::PresetData&);
    void setPresetInfo (const juce::String& id, const juce::String& name, const juce::String& category, const juce::String& description);
    resonique::ParamValues currentValues() const;

    std::array<juce::RangedAudioParameter*, resonique::kNumParams> params {};
    std::array<std::atomic<float>*, resonique::kNumParams> rawValues {};
    std::unique_ptr<resonique::PianoEngine> engine;
    std::atomic<int> currentProgram { 0 };
    std::atomic<bool> panicRequested { false };
    std::atomic<bool> tablesDirty { true };
    std::array<std::atomic<int>, 128> pendingCc;

    mutable juce::CriticalSection presetLock;
    PresetInfo presetInfo;

    // convolution reverb
    juce::dsp::Convolution convolution;
    std::atomic<bool> irLoaded { false };
    juce::String irName, irPath;
    juce::AudioBuffer<float> sendBuffer, wetBuffer;

    double cpuSmoothed = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResoniqueProcessor)
};
