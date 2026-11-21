#include "PresetManager.h"
#include "BinaryData.h"

namespace resonique
{
namespace
{
float clampParam (int index, float v)
{
    const auto& d = paramDef (index);
    return juce::jlimit ((float) d.min, (float) d.max, v);
}
} // namespace

PresetManager::PresetManager()
{
    loadFactory();
    loadFavourites();
}

//==============================================================================
// Factory file format:
//   { "categories": [ { "name": "Concert Grands",
//                       "presets": [ { "name": "...", "description": "...", "params": { "<id>": value, ... },
//                                      "tuning": { "detune": [...], "scala": "..." } }, ... ] } ] }
// Anything not given loads at its default.
void PresetManager::loadFactory()
{
    const auto json = juce::JSON::parse (juce::String::fromUTF8 (BinaryData::factory_presets_json,
                                                                 BinaryData::factory_presets_jsonSize));
    if (const auto* cats = json["categories"].getArray())
    {
        for (const auto& cat : *cats)
        {
            const auto* list = cat["presets"].getArray();
            if (list == nullptr) continue;
            for (const auto& p : *list)
            {
                PresetData d;
                juce::String err;
                if (! readJson (p, d, err)) continue;
                d.category = cat["name"].toString();
                factoryPresets.push_back (std::move (d));
            }
        }
    }
    if (factoryPresets.empty()) // never leave the host with zero programs
    {
        PresetData init;
        init.name = "Concert Grand";
        init.category = "Factory";
        factoryPresets.push_back (init);
    }
}

int PresetManager::findFactory (const juce::String& name) const
{
    for (size_t i = 0; i < factoryPresets.size(); ++i)
        if (factoryPresets[i].name == name) return (int) i;
    return -1;
}

//==============================================================================
juce::File PresetManager::userRoot()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Resonique");
}

juce::File PresetManager::userFolder()
{
    return userRoot().getChildFile ("Presets");
}

juce::String PresetManager::idForFile (const juce::File& f) const
{
    const auto root = userFolder();
    if (! f.isAChildOf (root)) return {};
    return "user:" + f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
}

juce::String PresetManager::categoryForFile (const juce::File& f) const
{
    const auto root = userFolder();
    const auto parent = f.getParentDirectory();
    if (parent == root || ! f.isAChildOf (root)) return "User";
    return parent.getRelativePathFrom (root).replaceCharacter ('\\', '/');
}

bool PresetManager::rescanIfChanged()
{
    const auto root = userFolder();
    root.createDirectory();

    const auto files = root.findChildFiles (juce::File::findFiles, true, juce::String ("*") + fileExtension);

    juce::String sig;
    for (const auto& f : files)
        sig << f.getFullPathName() << '|' << f.getSize() << '|' << f.getLastModificationTime().toMilliseconds() << ';';

    if (scanned && sig == signature) return false;
    scanned = true;
    signature = sig;

    user.clear();
    for (const auto& f : files)
        user.push_back ({ idForFile (f), f.getFileNameWithoutExtension(), categoryForFile (f), f });

    std::sort (user.begin(), user.end(), [] (const PresetEntry& a, const PresetEntry& b)
    {
        if (a.category != b.category) return a.category.compareNatural (b.category) < 0;
        return a.name.compareNatural (b.name) < 0;
    });
    return true;
}

const PresetEntry* PresetManager::findUser (const juce::String& id) const
{
    for (const auto& e : user)
        if (e.id == id) return &e;
    return nullptr;
}

juce::File PresetManager::fileForName (const juce::String& name) const
{
    return userFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + fileExtension);
}

//==============================================================================
// User file format:
//   { "format": "Resonique Preset", "version": 1, "name": "...", "category": "...", "description": "...",
//     "params": { "<paramId>": value, ... },        (master is never stored)
//     "tuning": { "detune": [88 cents], "scala": "<.scl text>" } }   (optional)
bool PresetManager::readJson (const juce::var& json, PresetData& d, juce::String& error)
{
    if (! json.isObject()) { error = "Not a preset"; return false; }
    const auto params = json["params"];
    int found = 0;
    if (params.isObject())
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& def = paramDef (i);
            if (def.noPreset) continue;
            const juce::Identifier id (def.id);
            if (params.hasProperty (id))
            {
                const auto& v = params[id];
                float value;
                if (v.isString() && def.kind == Kind::Choice)   // choices may be named in the factory file
                {
                    int idx = -1;
                    for (size_t c = 0; c < def.choices.size(); ++c)
                        if (v.toString() == juce::String (def.choices[c])) idx = (int) c;
                    if (idx < 0) continue;
                    value = (float) idx;
                }
                else value = (float) (double) v;
                d.values[(size_t) i] = clampParam (i, value);
                ++found;
            }
        }
    }
    if (found == 0) { error = "No Resonique settings found"; return false; }
    d.name = json["name"].toString().trim();
    d.category = json["category"].toString();
    d.description = json["description"].toString();
    d.tuning = json["tuning"].isObject() ? json["tuning"] : juce::var();
    return true;
}

bool PresetManager::readFile (const juce::File& f, PresetData& d, juce::String& error)
{
    juce::var json;
    if (! f.existsAsFile() || juce::JSON::parse (f.loadFileAsString(), json).failed() || ! json.isObject())
    {
        error = "Not a valid preset file: " + f.getFileName();
        return false;
    }
    if (! readJson (json, d, error)) { error = error + " in " + f.getFileName(); return false; }
    if (d.name.isEmpty()) d.name = f.getFileNameWithoutExtension();
    return true;
}

juce::var PresetManager::toJson (const PresetData& d)
{
    auto* params = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& def = paramDef (i);
        if (def.noPreset || std::isnan (d.values[(size_t) i])) continue;
        // round to 6 significant digits so the files stay readable
        const double v = juce::String (d.values[(size_t) i], 6).getDoubleValue();
        params->setProperty (def.id, v);
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", formatName);
    root->setProperty ("version", formatVersion);
    root->setProperty ("name", d.name);
    root->setProperty ("category", d.category);
    if (d.description.isNotEmpty()) root->setProperty ("description", d.description);
    root->setProperty ("params", juce::var (params));
    if (d.tuning.isObject() && d.tuning.getDynamicObject()->getProperties().size() > 0) root->setProperty ("tuning", d.tuning);
    return juce::var (root);
}

bool PresetManager::writeFile (const juce::File& f, const PresetData& d, juce::String& error)
{
    if (! f.getParentDirectory().createDirectory() || ! f.replaceWithText (juce::JSON::toString (toJson (d))))
    {
        error = "Couldn't write " + f.getFullPathName();
        return false;
    }
    return true;
}

//==============================================================================
void PresetManager::loadFavourites()
{
    favs.clear();
    const auto f = userRoot().getChildFile ("favorites.json");
    if (! f.existsAsFile()) return;
    const auto json = juce::JSON::parse (f.loadFileAsString());
    if (const auto* arr = json["favorites"].getArray())
        for (const auto& v : *arr) favs.addIfNotAlreadyThere (v.toString());
}

void PresetManager::saveFavourites() const
{
    juce::Array<juce::var> arr;
    for (const auto& s : favs) arr.add (s);
    auto* o = new juce::DynamicObject();
    o->setProperty ("favorites", arr);
    const auto f = userRoot().getChildFile ("favorites.json");
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (juce::var (o)));
}

bool PresetManager::isFavourite (const juce::String& id) const { return favs.contains (id); }

void PresetManager::setFavourite (const juce::String& id, bool on)
{
    if (on) favs.addIfNotAlreadyThere (id); else favs.removeString (id);
    saveFavourites();
}
} // namespace resonique
