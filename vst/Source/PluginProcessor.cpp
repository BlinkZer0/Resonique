#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace resonique;

ResoniqueProcessor::ResoniqueProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ResoniqueState", createParameterLayout())
{
    engine = std::make_unique<PianoEngine>();
    for (auto& c : pendingCc) c.store (-1);
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& id = paramDef (i).id;
        params[(size_t) i] = apvts.getParameter (id);
        rawValues[(size_t) i] = apvts.getRawParameterValue (id);
        jassert (params[(size_t) i] != nullptr && rawValues[(size_t) i] != nullptr);
        apvts.addParameterListener (id, this);
    }
    setCurrentProgram (0);          // a fresh instance starts on the first factory preset
    startTimerHz (20);
}

ResoniqueProcessor::~ResoniqueProcessor()
{
    stopTimer();
    for (int i = 0; i < kNumParams; ++i) apvts.removeParameterListener (paramDef (i).id, this);
}

//==============================================================================
void ResoniqueProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    statSampleRate = sampleRate;
    tables.setSampleRate (sampleRate);               // synchronous build for this rate
    engine->prepare (sampleRate);
    if (auto* t = tables.takeIncoming()) engine->setTables (t);
    else if (tables.active() != nullptr) engine->setTables (tables.active());
    engine->setParamValues (currentValues().data());
    keyboardState.reset();
    tablesDirty = false;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    convolution.prepare (spec);
    sendBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    wetBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    if (irPath.isNotEmpty())
    {
        const juce::File f (irPath);
        if (f.existsAsFile())
            convolution.loadImpulseResponse (f, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0,
                                             juce::dsp::Convolution::Normalise::yes);
    }
}

bool ResoniqueProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

ParamValues ResoniqueProcessor::currentValues() const
{
    ParamValues v;
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = paramValue (i);
    return v;
}

void ResoniqueProcessor::parameterChanged (const juce::String& id, float)
{
    const int i = paramIndexForId (id.toStdString());
    if (i >= 0 && paramAffectsTables (i)) tablesDirty = true;
}

void ResoniqueProcessor::timerCallback()
{
    tables.collectRetired();
    if (tablesDirty.exchange (false))
        tables.requestBuild (currentValues());
}

void ResoniqueProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();

    if (auto* t = tables.takeIncoming()) engine->setTables (t);
    if (panicRequested.exchange (false)) engine->panic();
    for (int cc = 0; cc < 128; ++cc)
    {
        const int v = pendingCc[(size_t) cc].exchange (-1);
        if (v >= 0) engine->controller (cc, v);
    }

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    engine->setParamValues (currentValues().data());

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const bool useIr = irLoaded.load() && juce::roundToInt (paramValue (reverbType)) == 4 && R != nullptr && sendBuffer.getNumSamples() >= n;
    float* sL = useIr ? sendBuffer.getWritePointer (0) : nullptr;
    float* sR = useIr ? sendBuffer.getWritePointer (1) : nullptr;
    const double bendRange = paramValue (pitchBendRange);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos)
        {
            engine->render (L + pos, R != nullptr ? R + pos : nullptr, t - pos, sL != nullptr ? sL + pos : nullptr, sR != nullptr ? sR + pos : nullptr);
            pos = t;
        }
        routeMidi (meta.getMessage(), *engine, bendRange);
    }
    if (pos < n)
        engine->render (L + pos, R != nullptr ? R + pos : nullptr, n - pos, sL != nullptr ? sL + pos : nullptr, sR != nullptr ? sR + pos : nullptr);

    if (useIr)
    {
        juce::dsp::AudioBlock<float> block (sendBuffer.getArrayOfWritePointers(), 2, 0, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        convolution.process (ctx);
        const float mix = paramValue (reverbMix) * 0.9f;
        for (int i = 0; i < n; ++i)
        {
            L[i] = softClip (L[i] + sL[i] * mix);
            R[i] = softClip (R[i] + sR[i] * mix);
        }
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    statVoices.store (engine->activeVoices());
    statPartials.store (engine->activePartials());
    statSustain.store ((float) engine->sustainPosition());
    statSostenuto.store (engine->sostenutoDown());
    statSoft.store ((float) engine->softPosition());
    const float pk = engine->takePeak();
    if (pk > statPeak.load()) statPeak.store (pk);

    const double seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double budget = n / juce::jmax (1.0, getSampleRate());
    const double load = budget > 0 ? seconds / budget : 0.0;
    cpuSmoothed += (load - cpuSmoothed) * 0.1;
    statCpu.store ((float) cpuSmoothed);
}

//==============================================================================
void ResoniqueProcessor::applyPreset (const PresetData& preset)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        if (paramDef (i).noPreset) continue;
        const float stored = preset.values[(size_t) i];
        const float v = std::isnan (stored) ? (float) paramDef (i).def : stored;
        auto* p = params[(size_t) i];
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
    tables.tuningFromJson (preset.tuning);
    tablesDirty = true;
}

void ResoniqueProcessor::setPresetInfo (const juce::String& id, const juce::String& name, const juce::String& category, const juce::String& description)
{
    {
        const juce::ScopedLock sl (presetLock);
        presetInfo = { id, name, category, description };
    }
    ++presetVersion;
}

ResoniqueProcessor::PresetInfo ResoniqueProcessor::currentPreset() const
{
    const juce::ScopedLock sl (presetLock);
    return presetInfo;
}

void ResoniqueProcessor::markDirty() { }

void ResoniqueProcessor::setCurrentProgram (int index)
{
    index = juce::jlimit (0, getNumPrograms() - 1, index);
    currentProgram = index;
    const auto& preset = presetManager.factory()[(size_t) index];
    applyPreset (preset);
    setPresetInfo ("factory:" + juce::String (index), preset.name, preset.category, preset.description);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

const juce::String ResoniqueProcessor::getProgramName (int index)
{
    if (index < 0 || index >= getNumPrograms()) return {};
    const auto& p = presetManager.factory()[(size_t) index];
    return p.category + " - " + p.name;
}

bool ResoniqueProcessor::loadPresetById (const juce::String& id, juce::String& error)
{
    if (id.startsWith ("factory:"))
    {
        const int index = id.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (index < 0 || index >= getNumPrograms()) { error = "Unknown preset"; return false; }
        setCurrentProgram (index);
        return true;
    }
    presetManager.rescanIfChanged();
    if (const auto* entry = presetManager.findUser (id))
    {
        PresetData data;
        if (! PresetManager::readFile (entry->file, data, error)) return false;
        applyPreset (data);
        setPresetInfo (entry->id, entry->name, entry->category, data.description);
        return true;
    }
    error = "Preset not found (was it moved or deleted?)";
    return false;
}

bool ResoniqueProcessor::loadPresetFile (const juce::File& file, juce::String& error)
{
    PresetData data;
    if (! PresetManager::readFile (file, data, error)) return false;
    applyPreset (data);
    const auto userId = presetManager.idForFile (file);
    if (userId.isNotEmpty())
        setPresetInfo (userId, file.getFileNameWithoutExtension(), presetManager.categoryForFile (file), data.description);
    else
        setPresetInfo ("file:" + file.getFullPathName(), data.name, "File", data.description);
    return true;
}

bool ResoniqueProcessor::saveUserPreset (const juce::String& rawName, const juce::String& category, bool overwrite,
                                         bool& alreadyExists, juce::String& error)
{
    alreadyExists = false;
    const auto name = rawName.trim();
    if (name.isEmpty()) { error = "Enter a name for the preset"; return false; }

    auto folder = PresetManager::userFolder();
    const auto cat = category.trim();
    if (cat.isNotEmpty() && cat != "User") folder = folder.getChildFile (juce::File::createLegalFileName (cat));
    const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + PresetManager::fileExtension);
    if (file.existsAsFile() && ! overwrite) { alreadyExists = true; return false; }

    PresetData data;
    data.name = name;
    data.category = cat.isEmpty() ? "User" : cat;
    for (int i = 0; i < kNumParams; ++i)
        if (! paramDef (i).noPreset) data.values[(size_t) i] = paramValue (i);
    data.tuning = tables.tuningToJson();

    if (! PresetManager::writeFile (file, data, error)) return false;
    presetManager.rescanIfChanged();
    setPresetInfo (presetManager.idForFile (file), file.getFileNameWithoutExtension(), presetManager.categoryForFile (file), {});
    return true;
}

bool ResoniqueProcessor::exportPreset (const juce::File& file, juce::String& error)
{
    PresetData data;
    const auto info = currentPreset();
    data.name = file.getFileNameWithoutExtension();
    data.category = info.category.isEmpty() ? "User" : info.category;
    for (int i = 0; i < kNumParams; ++i)
        if (! paramDef (i).noPreset) data.values[(size_t) i] = paramValue (i);
    data.tuning = tables.tuningToJson();
    return PresetManager::writeFile (file, data, error);
}

bool ResoniqueProcessor::deleteUserPreset (const juce::String& id, juce::String& error)
{
    presetManager.rescanIfChanged();
    const auto* entry = presetManager.findUser (id);
    if (entry == nullptr) { error = "Only user presets can be deleted"; return false; }
    const auto file = entry->file;
    if (! file.moveToTrash() && ! file.deleteFile()) { error = "Couldn't delete " + file.getFullPathName(); return false; }
    presetManager.setFavourite (id, false);
    presetManager.rescanIfChanged();
    if (currentPreset().id == id)                       // keep the sound, drop the link to the file
        setPresetInfo ({}, currentPreset().name, "User", {});
    return true;
}

void ResoniqueProcessor::initPreset()
{
    PresetData d;   // all NaN -> defaults
    applyPreset (d);
    setPresetInfo ({}, "Init", "User", "Every parameter at its default");
}

//==============================================================================
void ResoniqueProcessor::setNoteDetune (int noteIndex, double cents) { tables.setNoteDetune (noteIndex, cents); tuningChanged(); }
void ResoniqueProcessor::resetDetune() { tables.resetDetune(); tuningChanged(); }
bool ResoniqueProcessor::loadScalaFile (const juce::File& f, juce::String& error)
{
    if (! f.existsAsFile()) { error = "File not found"; return false; }
    return loadScalaText (f.loadFileAsString(), error);
}
bool ResoniqueProcessor::loadScalaText (const juce::String& text, juce::String& error)
{
    if (! tables.setScala (text, error)) return false;
    auto* p = params[temperament];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) Temperament::Custom));
    p->endChangeGesture();
    tuningChanged();
    return true;
}
void ResoniqueProcessor::clearScala() { tables.clearScala(); tuningChanged(); }
void ResoniqueProcessor::tuningChanged() { tablesDirty = true; }

//==============================================================================
bool ResoniqueProcessor::loadImpulseResponse (const juce::File& f, juce::String& error)
{
    if (! f.existsAsFile()) { error = "File not found"; return false; }
    convolution.loadImpulseResponse (f, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0,
                                     juce::dsp::Convolution::Normalise::yes);
    irName = f.getFileName();
    irPath = f.getFullPathName();
    irLoaded = true;
    ++irVersion;
    return true;
}

void ResoniqueProcessor::clearImpulseResponse()
{
    irLoaded = false;
    irName = {};
    irPath = {};
    ++irVersion;
}

//==============================================================================
void ResoniqueProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    const auto info = currentPreset();
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.setProperty ("presetId", info.id, nullptr);
    state.setProperty ("presetName", info.name, nullptr);
    state.setProperty ("presetCategory", info.category, nullptr);
    state.setProperty ("tuning", juce::JSON::toString (tables.tuningToJson(), true), nullptr);
    state.setProperty ("irPath", irPath, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ResoniqueProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
            auto name = state.getProperty ("presetName").toString();
            if (name.isEmpty())
            {
                const auto& p = presetManager.factory()[(size_t) currentProgram.load()];
                setPresetInfo ("factory:" + juce::String (currentProgram.load()), p.name, p.category, p.description);
            }
            else
                setPresetInfo (state.getProperty ("presetId").toString(), name, state.getProperty ("presetCategory").toString(), {});
            tables.tuningFromJson (juce::JSON::parse (state.getProperty ("tuning").toString()));
            const auto ir = state.getProperty ("irPath").toString();
            if (ir.isNotEmpty()) { juce::String err; loadImpulseResponse (juce::File (ir), err); }
            else clearImpulseResponse();
            apvts.replaceState (state);
            tablesDirty = true;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* ResoniqueProcessor::createEditor()
{
    return new ResoniqueEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResoniqueProcessor();
}
