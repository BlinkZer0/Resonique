#pragma once

#include <bitset>
#include "PluginProcessor.h"

// Hosts the HTML/SVG UI (vst/ui, compiled in as BinaryData) in a WebView2 and
// bridges it to the processor (family convention, see vst/README.md):
//   UI -> C++ : uiReady, setParam{id,value}, setParams{values}, gesture{id,begin},
//               noteOn{note,vel}, noteOff{note}, panic, pedal{cc,value},
//               loadPreset{id}, savePreset{name,category,overwrite}, deletePreset{id},
//               loadPresetFile, exportPreset, openPresetFolder, initPreset, factoryReset,
//               setFavourite{id,on}, paramMenu{id,x,y},
//               setDetune{note,cents}, resetDetune, loadScala, clearScala, loadIR, clearIR
//   C++ -> UI : params{id:value...}, notes{on[],off[]}, stats{...}, config{...},
//               presetList{presets[],folder,favorites[]}, preset{id,name,category,description},
//               saveResult{ok,exists,name,error}, message{text}, tuning{detune[],scala,valid}, ir{name}
class ResoniqueEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ResoniqueEditor (ResoniqueProcessor&);
    ~ResoniqueEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::WebBrowserComponent::Options makeBrowserOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    int indexForId (const juce::String& id) const;
    void onUiReady();
    void onSetParam (const juce::var&);
    void onSetParams (const juce::var&);
    void onGesture (const juce::var&);
    void sendConfig();
    void sendPresetList();
    void sendPreset();
    void sendTuning();
    void sendIr();
    void sendMessage (const juce::String& text);
    void chooseAndLoadPresetFile();
    void chooseExportFile();
    void chooseScalaFile();
    void chooseIrFile();
    void showParamMenu (const juce::var&);

    ResoniqueProcessor& processor;

    std::array<float, resonique::kNumParams> lastSent {};
    std::array<bool, resonique::kNumParams> inGesture {};
    std::bitset<128> lastNotes;
    int lastPresetVersion = -1;
    int lastTuningVersion = -1;
    int lastIrVersion = -1;
    int ticks = 0;
    bool uiReady = false;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::WebBrowserComponent browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResoniqueEditor)
};
