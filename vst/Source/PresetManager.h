#pragma once
// ============================================================================
//  Presets
//   - factory presets compiled in from presets/factory_presets.json
//     (also exposed to the host as programs)
//   - user presets: *.rqpreset JSON files in Documents\Resonique\Presets
//     (shared by every DAW / instance; sub-folders become categories,
//      the folder is re-scanned automatically)
//   - favourites: Documents\Resonique\favorites.json (a list of preset ids)
// ============================================================================
#include <juce_core/juce_core.h>
#include <vector>
#include "dsp/ParamTable.h"

namespace resonique
{
struct PresetData
{
    juce::String name, category, description;
    std::array<float, kNumParams> values;   // NaN = not in preset -> parameter default
    juce::var tuning;                       // { "detune": [...], "scala": "..." } or void

    PresetData() { values.fill (std::numeric_limits<float>::quiet_NaN()); }
};

struct PresetEntry
{
    juce::String id;        // "user:<relative/path.rqpreset>"
    juce::String name, category;
    juce::File file;
};

class PresetManager
{
public:
    PresetManager();

    static juce::File userRoot();          // Documents\Resonique
    static juce::File userFolder();        // Documents\Resonique\Presets
    static constexpr const char* fileExtension = ".rqpreset";
    static constexpr const char* formatName = "Resonique Preset";
    static constexpr int formatVersion = 1;

    // Factory (immutable, any thread)
    const std::vector<PresetData>& factory() const { return factoryPresets; }
    int findFactory (const juce::String& name) const;

    // User library (message thread only)
    bool rescanIfChanged();                     // true if the list changed
    const std::vector<PresetEntry>& userPresets() const { return user; }
    const PresetEntry* findUser (const juce::String& id) const;
    juce::File fileForName (const juce::String& name) const;
    juce::String idForFile (const juce::File& f) const;  // "" if not inside the user folder
    juce::String categoryForFile (const juce::File& f) const;

    static bool readFile (const juce::File&, PresetData&, juce::String& error);
    static bool readJson (const juce::var& json, PresetData&, juce::String& error);
    static bool writeFile (const juce::File&, const PresetData&, juce::String& error);
    static juce::var toJson (const PresetData&);

    // Favourites (message thread)
    bool isFavourite (const juce::String& id) const;
    void setFavourite (const juce::String& id, bool on);
    const juce::StringArray& favourites() const { return favs; }

private:
    void loadFactory();
    void loadFavourites();
    void saveFavourites() const;

    std::vector<PresetData> factoryPresets;
    std::vector<PresetEntry> user;
    juce::StringArray favs;
    juce::String signature;
    bool scanned = false;
};
} // namespace resonique
