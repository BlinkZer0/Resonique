#include "PluginEditor.h"
#include "BinaryData.h"

using namespace resonique;

namespace
{
const juce::Colour kBackground { 0xff0b0a0c };

const char* mimeForExtension (const juce::String& ext)
{
    if (ext == "html") return "text/html";
    if (ext == "js")   return "text/javascript";
    if (ext == "css")  return "text/css";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "json") return "application/json";
    if (ext == "png")  return "image/png";
    return "application/octet-stream";
}

std::vector<std::byte> toBytes (const void* data, size_t size)
{
    const auto* p = static_cast<const std::byte*> (data);
    return { p, p + size };
}

juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* obj = new juce::DynamicObject();
    for (const auto& [k, v] : props) obj->setProperty (k, v);
    return juce::var (obj);
}
} // namespace

//==============================================================================
ResoniqueEditor::ResoniqueEditor (ResoniqueProcessor& p)
    : AudioProcessorEditor (p), processor (p), browser (makeBrowserOptions())
{
    lastSent.fill (std::numeric_limits<float>::quiet_NaN());

    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (700, 520, 2600, 2200);
    setSize (1180, 860);
    startTimerHz (30);
}

ResoniqueEditor::~ResoniqueEditor()
{
    stopTimer();
    for (int i = 0; i < kNumParams; ++i)
        if (inGesture[(size_t) i]) processor.param (i)->endChangeGesture();
}

void ResoniqueEditor::paint (juce::Graphics& g) { g.fillAll (kBackground); }
void ResoniqueEditor::resized() { browser.setBounds (getLocalBounds()); }

//==============================================================================
juce::WebBrowserComponent::Options ResoniqueEditor::makeBrowserOptions()
{
    using Opts = juce::WebBrowserComponent::Options;
    const auto userData = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ResoniqueVST-WebView2");

    return Opts{}
        .withBackend (Opts::Backend::webview2)
        .withWinWebView2Options (Opts::WinWebView2{}
                                     .withUserDataFolder (userData)
                                     .withBackgroundColour (kBackground)
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withEventListener ("uiReady",    [this] (const juce::var&) { onUiReady(); })
        .withEventListener ("setParam",   [this] (const juce::var& e) { onSetParam (e); })
        .withEventListener ("setParams",  [this] (const juce::var& e) { onSetParams (e); })
        .withEventListener ("gesture",    [this] (const juce::var& e) { onGesture (e); })
        .withEventListener ("noteOn", [this] (const juce::var& e)
        {
            const int note = juce::jlimit (0, 127, (int) e["note"]);
            const float vel = juce::jlimit (1, 127, (int) e["vel"]) / 127.0f;
            processor.keyboardState.noteOn (1, note, vel);
        })
        .withEventListener ("noteOff", [this] (const juce::var& e)
        {
            processor.keyboardState.noteOff (1, juce::jlimit (0, 127, (int) e["note"]), 0.0f);
        })
        .withEventListener ("panic", [this] (const juce::var&)
        {
            processor.keyboardState.allNotesOff (0);
            processor.requestPanic();
        })
        .withEventListener ("pedal", [this] (const juce::var& e) { processor.requestController ((int) e["cc"], (int) e["value"]); })
        .withEventListener ("loadPreset", [this] (const juce::var& e)
        {
            juce::String error;
            if (! processor.loadPresetById (e["id"].toString(), error)) sendMessage (error);
            sendPreset();
        })
        .withEventListener ("savePreset", [this] (const juce::var& e)
        {
            const auto name = e["name"].toString().trim();
            bool exists = false;
            juce::String error;
            const bool ok = processor.saveUserPreset (name, e["category"].toString(), (bool) e["overwrite"], exists, error);
            browser.emitEventIfBrowserIsVisible ("saveResult", makeObject ({
                { "ok", ok }, { "exists", exists }, { "name", name }, { "error", error } }));
            if (ok) { sendPresetList(); sendPreset(); }
        })
        .withEventListener ("deletePreset", [this] (const juce::var& e)
        {
            juce::String error;
            if (processor.deleteUserPreset (e["id"].toString(), error)) sendMessage ("Preset moved to the Recycle Bin");
            else sendMessage (error);
            sendPresetList();
            sendPreset();
        })
        .withEventListener ("setFavourite", [this] (const juce::var& e)
        {
            processor.presetManager.setFavourite (e["id"].toString(), (bool) e["on"]);
            sendPresetList();
        })
        .withEventListener ("initPreset", [this] (const juce::var&) { processor.initPreset(); sendPreset(); })
        .withEventListener ("factoryReset", [this] (const juce::var&)
        {
            processor.initPreset();
            for (const auto& id : juce::StringArray (processor.presetManager.favourites())) processor.presetManager.setFavourite (id, false);
            processor.clearImpulseResponse();
            processor.setCurrentProgram (0);
            sendPresetList();
            sendPreset();
            sendMessage ("Factory defaults restored (your preset files are untouched)");
        })
        .withEventListener ("loadPresetFile", [this] (const juce::var&) { chooseAndLoadPresetFile(); })
        .withEventListener ("exportPreset", [this] (const juce::var&) { chooseExportFile(); })
        .withEventListener ("paramMenu", [this] (const juce::var& e) { showParamMenu (e); })
        .withEventListener ("openPresetFolder", [] (const juce::var&)
        {
            const auto folder = PresetManager::userFolder();
            folder.createDirectory();
            folder.startAsProcess();
        })
        // ---- tuning
        .withEventListener ("setDetune", [this] (const juce::var& e) { processor.setNoteDetune ((int) e["note"], (double) e["cents"]); })
        .withEventListener ("resetDetune", [this] (const juce::var&) { processor.resetDetune(); sendTuning(); })
        .withEventListener ("loadScala", [this] (const juce::var&) { chooseScalaFile(); })
        .withEventListener ("clearScala", [this] (const juce::var&) { processor.clearScala(); sendTuning(); })
        // ---- impulse response
        .withEventListener ("loadIR", [this] (const juce::var&) { chooseIrFile(); })
        .withEventListener ("clearIR", [this] (const juce::var&) { processor.clearImpulseResponse(); sendIr(); });
}

void ResoniqueEditor::chooseAndLoadPresetFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load Resonique preset", PresetManager::userFolder(),
                                                        juce::String ("*") + PresetManager::fileExtension);
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (flags, [safeThis = juce::Component::SafePointer<ResoniqueEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        juce::String error;
        if (safeThis->processor.loadPresetFile (file, error)) safeThis->sendMessage ("Loaded " + file.getFileName());
        else safeThis->sendMessage (error);
        safeThis->sendPreset();
    });
}

void ResoniqueEditor::chooseExportFile()
{
    const auto info = processor.currentPreset();
    const auto start = PresetManager::userFolder().getChildFile (juce::File::createLegalFileName (info.name) + PresetManager::fileExtension);
    fileChooser = std::make_unique<juce::FileChooser> ("Export preset", start, juce::String ("*") + PresetManager::fileExtension);
    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;
    fileChooser->launchAsync (flags, [safeThis = juce::Component::SafePointer<ResoniqueEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (! file.hasFileExtension (PresetManager::fileExtension)) file = file.withFileExtension (PresetManager::fileExtension);
        juce::String error;
        if (safeThis->processor.exportPreset (file, error)) safeThis->sendMessage ("Exported " + file.getFileName());
        else safeThis->sendMessage (error);
        safeThis->sendPresetList();
    });
}

void ResoniqueEditor::chooseScalaFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load a Scala scale (.scl)", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.scl");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (flags, [safeThis = juce::Component::SafePointer<ResoniqueEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        juce::String error;
        if (safeThis->processor.loadScalaFile (file, error)) safeThis->sendMessage ("Loaded scale " + file.getFileName());
        else safeThis->sendMessage ("Scala: " + error);
        safeThis->sendTuning();
    });
}

void ResoniqueEditor::chooseIrFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load an impulse response", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                        "*.wav;*.aif;*.aiff;*.flac");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (flags, [safeThis = juce::Component::SafePointer<ResoniqueEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        juce::String error;
        if (safeThis->processor.loadImpulseResponse (file, error)) safeThis->sendMessage ("Loaded impulse response " + file.getFileName());
        else safeThis->sendMessage (error);
        safeThis->sendIr();
    });
}

void ResoniqueEditor::showParamMenu (const juce::var& e)
{
    const int i = indexForId (e["id"].toString());
    if (i < 0) return;
    auto* p = processor.param (i);
    const juce::Point<int> pos ((int) e["x"], (int) e["y"]);

    if (auto* host = getHostContext())
        if (auto menu = host->getContextMenuForParameter (p))
        {
            menu->showNativeMenu (pos);
            return;
        }

    juce::PopupMenu m;
    m.addSectionHeader (p->getName (64) + ": " + p->getCurrentValueAsText());
    m.addItem (1, "Reset to default");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                .withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (pos.x, pos.y, 1, 1))),
                     [safeThis = juce::Component::SafePointer<ResoniqueEditor> (this), i] (int result)
                     {
                         if (safeThis == nullptr || result != 1) return;
                         auto* param = safeThis->processor.param (i);
                         param->beginChangeGesture();
                         param->setValueNotifyingHost (param->getDefaultValue());
                         param->endChangeGesture();
                     });
}

void ResoniqueEditor::sendMessage (const juce::String& text)
{
    browser.emitEventIfBrowserIsVisible ("message", makeObject ({ { "text", text } }));
}

void ResoniqueEditor::sendPresetList()
{
    auto& pm = processor.presetManager;
    pm.rescanIfChanged();

    juce::Array<juce::var> list;
    const auto& factory = pm.factory();
    for (size_t i = 0; i < factory.size(); ++i)
        list.add (makeObject ({ { "id", "factory:" + juce::String ((int) i) }, { "name", factory[i].name },
                                { "category", factory[i].category }, { "description", factory[i].description },
                                { "instrument", (int) std::lround (std::isnan (factory[i].values[instrument]) ? 0.0f : factory[i].values[instrument]) },
                                { "factory", true } }));
    for (const auto& e : pm.userPresets())
        list.add (makeObject ({ { "id", e.id }, { "name", e.name }, { "category", e.category }, { "factory", false } }));
    juce::Array<juce::var> favs;
    for (const auto& f : pm.favourites()) favs.add (f);

    browser.emitEventIfBrowserIsVisible ("presetList", makeObject ({
        { "presets", list }, { "folder", PresetManager::userFolder().getFullPathName() }, { "favorites", favs } }));
}

void ResoniqueEditor::sendPreset()
{
    const auto info = processor.currentPreset();
    lastPresetVersion = processor.presetVersion.load();
    browser.emitEventIfBrowserIsVisible ("preset", makeObject ({
        { "id", info.id }, { "name", info.name }, { "category", info.category }, { "description", info.description } }));
}

void ResoniqueEditor::sendTuning()
{
    lastTuningVersion = processor.tables.tuningVersion.load();
    juce::Array<juce::var> det;
    for (const auto& d : processor.tables.tuning.noteDetune) det.add (d);
    const auto& scala = processor.tables.tuning.scala;
    browser.emitEventIfBrowserIsVisible ("tuning", makeObject ({
        { "detune", det },
        { "scala", juce::String (scala.description) },
        { "scalaNotes", scala.valid() ? (int) scala.cents.size() : 0 },
        { "valid", scala.valid() } }));
}

void ResoniqueEditor::sendIr()
{
    lastIrVersion = processor.irVersion.load();
    browser.emitEventIfBrowserIsVisible ("ir", makeObject ({ { "name", processor.impulseResponseName() } }));
}

std::optional<juce::WebBrowserComponent::Resource> ResoniqueEditor::getResource (const juce::String& url)
{
    auto path = url.upToFirstOccurrenceOf ("?", false, false).trimCharactersAtStart ("/");
    if (path.isEmpty()) path = "index.html";
    const auto fileName = path.fromLastOccurrenceOf ("/", false, false);
    const auto mime = mimeForExtension (fileName.fromLastOccurrenceOf (".", false, false).toLowerCase());

    // Dev mode: set RESONIQUE_UI_DIR to the vst/ui folder to load the UI from disk.
    const auto devDir = juce::SystemStats::getEnvironmentVariable ("RESONIQUE_UI_DIR", {});
    if (devDir.isNotEmpty())
    {
        const auto f = juce::File (devDir).getChildFile (path);
        juce::MemoryBlock mb;
        if (f.existsAsFile() && f.loadFileAsData (mb))
            return juce::WebBrowserComponent::Resource { toBytes (mb.getData(), mb.getSize()), mime };
    }

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (fileName == BinaryData::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                return juce::WebBrowserComponent::Resource { toBytes (data, (size_t) size), mime };
        }
    }
    return std::nullopt;
}

//==============================================================================
int ResoniqueEditor::indexForId (const juce::String& id) const
{
    return paramIndexForId (id.toStdString());
}

void ResoniqueEditor::onUiReady()
{
    uiReady = true;
    lastSent.fill (std::numeric_limits<float>::quiet_NaN());
    lastNotes.reset();
    sendConfig();
    sendPresetList();
    sendPreset();
    sendTuning();
    sendIr();
    timerCallback();
}

void ResoniqueEditor::onSetParam (const juce::var& e)
{
    const int i = indexForId (e["id"].toString());
    if (i < 0) return;
    auto* p = processor.param (i);
    const float v = (float) (double) e["value"];
    const bool wrap = ! inGesture[(size_t) i];
    if (wrap) p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (v));
    if (wrap) p->endChangeGesture();
    lastSent[(size_t) i] = processor.paramValue (i);
}

void ResoniqueEditor::onSetParams (const juce::var& e)
{
    const auto values = e["values"];
    if (auto* obj = values.getDynamicObject())
    {
        for (const auto& prop : obj->getProperties())
        {
            const int i = indexForId (prop.name.toString());
            if (i < 0) continue;
            auto* p = processor.param (i);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) (double) prop.value));
            p->endChangeGesture();
            lastSent[(size_t) i] = processor.paramValue (i);
        }
    }
}

void ResoniqueEditor::onGesture (const juce::var& e)
{
    const int i = indexForId (e["id"].toString());
    if (i < 0) return;
    const bool begin = (bool) e["begin"];
    if (begin == inGesture[(size_t) i]) return;
    inGesture[(size_t) i] = begin;
    if (begin) processor.param (i)->beginChangeGesture();
    else       processor.param (i)->endChangeGesture();
}

void ResoniqueEditor::sendConfig()
{
    const auto wrapper = juce::String (juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));
    browser.emitEventIfBrowserIsVisible ("config", makeObject ({
        { "sampleRate", processor.getSampleRate() },
        { "blockSize",  processor.getBlockSize() },
        { "wrapper",    wrapper },
        { "voices",     kMaxVoices },
        { "version",    JucePlugin_VersionString },
    }));
}

void ResoniqueEditor::timerCallback()
{
    if (! uiReady) return;

    auto* changed = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = processor.paramValue (i);
        if (v != lastSent[(size_t) i])
        {
            changed->setProperty (paramDef (i).id, v);
            lastSent[(size_t) i] = v;
        }
    }
    juce::var changedVar (changed);
    if (changed->getProperties().size() > 0)
        browser.emitEventIfBrowserIsVisible ("params", changedVar);

    if (processor.presetVersion.load() != lastPresetVersion) sendPreset();
    if (processor.tables.tuningVersion.load() != lastTuningVersion) sendTuning();
    if (processor.irVersion.load() != lastIrVersion) sendIr();

    if (++ticks % 60 == 0 && processor.presetManager.rescanIfChanged())
        sendPresetList();

    juce::Array<juce::var> on, off;
    for (int n = 0; n < 128; ++n)
    {
        const bool down = processor.keyboardState.isNoteOnForChannels (0xffff, n);
        if (down != lastNotes[(size_t) n])
        {
            lastNotes[(size_t) n] = down;
            (down ? on : off).add (n);
        }
    }
    if (! on.isEmpty() || ! off.isEmpty())
        browser.emitEventIfBrowserIsVisible ("notes", makeObject ({ { "on", on }, { "off", off } }));

    browser.emitEventIfBrowserIsVisible ("stats", makeObject ({
        { "voices",    processor.statVoices.load() },
        { "partials",  processor.statPartials.load() },
        { "peak",      processor.statPeak.exchange (0.0f) },
        { "sustain",   processor.statSustain.load() },
        { "sostenuto", processor.statSostenuto.load() },
        { "soft",      processor.statSoft.load() },
        { "cpu",       processor.statCpu.load() },
        { "building",  processor.tables.building() },
        { "sampleRate", processor.statSampleRate.load() },
    }));
}
