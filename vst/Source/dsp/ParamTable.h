#pragma once
// ============================================================================
//  The parameter table in plain C++ (no JUCE). Mirrors vst/ui/js/params.js
//  (IDs, names, kinds, ranges, defaults, units, log skew, choice lists).
//
//  New parameters are only ever APPENDED (indices + IDs of existing ones never
//  change) and default to a neutral value, so older presets/projects load and
//  sound exactly as before. `ResoniqueTests --dump-params` prints this table as
//  JSON and tests/check-params.ps1 compares it with params.js.
// ============================================================================
#include <string>
#include <vector>
#include "Common.h"

namespace resonique
{
enum ParamIndex
{
    // ---- main page ---------------------------------------------------------
    instrument,            // which physical model (Concert Grand, ...)
    master,                // output level (never stored in presets)
    toneBass, toneTreble,  // output tilt EQ
    hammerHardness,        // brightness macro: felt stiffness + exponent
    dynamics,              // how wide the pp..ff hammer-velocity range is
    velCurve,              // velocity curve bend
    stringResonance,       // sympathetic coupling strength
    soundboardResonance,   // soundboard mode Q / level
    stereoWidth,
    reverbMix, reverbSize, reverbDamp, reverbType, reverbPredelay,
    // ---- physical model page -----------------------------------------------
    inharmonicity,         // x B
    stringStiffness,       // x high-partial loss
    stringDecay,           // x decay time
    hammerPosition,        // x strike ratio
    hammerMass,            // x hammer mass
    feltStiffness,         // x felt K
    feltExponent,          // + felt exponent p
    unisonDetune,          // cents between the strings of a note
    damperEfficiency,      // x damper loss
    bridgeCoupling,        // local bridge admittance (unison coupling, two-stage decay)
    soundboardSize,        // x soundboard mode frequency (1/size)
    soundboardDamping,     // x soundboard loss
    soundboardBrightness,  // modal body vs direct high-frequency radiation
    // ---- microphones -------------------------------------------------------
    micPerspective, micDistance, micSpread, micPosition, earlyReflections,
    // ---- performance -------------------------------------------------------
    polyphony, pedalMode, halfPedalPoint, softPedalDepth, pitchBendRange,
    // ---- mechanical noises -------------------------------------------------
    noiseHammer, noiseKeyRelease, noiseDamper, noisePedal, noiseMaster,
    // ---- tuning ------------------------------------------------------------
    temperament, temperamentRoot, concertPitch, stretch,
    // ---- level matching (written by the preset builder) --------------------
    presetGain,
    kNumParams
};

enum class Kind { Float, Bool, Choice, Int };
enum class Unit { None, Percent, BipolarPercent, Decibels, Hz, Cents, Semitones, Metres, Milliseconds, Ratio, Exponent };

struct ParamDef
{
    const char* id;
    const char* name;      // host / UI name
    Kind kind;
    double min, max, def;
    Unit unit;
    bool logSkew;
    bool noPreset;         // never stored in presets (master)
    std::vector<std::string> choices;
};

inline const std::vector<std::string>& instrumentChoices()
{
    static const std::vector<std::string> c { "Concert Grand", "Studio Grand", "Upright", "Vintage Upright", "Bright Pop Grand",
                                              "Dark Jazz Grand", "Felt Piano", "Prepared Piano", "Honky-Tonk", "Experimental Grand" };
    return c;
}
inline const std::vector<std::string>& reverbTypeChoices()
{
    static const std::vector<std::string> c { "Room", "Chamber", "Hall", "Stage", "Impulse Response" };
    return c;
}
inline const std::vector<std::string>& perspectiveChoices()
{
    static const std::vector<std::string> c { "Player", "Close Stereo", "Concert Audience", "Wide Stereo", "Mono", "Custom" };
    return c;
}
inline const std::vector<std::string>& pedalModeChoices()
{
    static const std::vector<std::string> c { "Continuous", "Switch" };
    return c;
}
inline const std::vector<std::string>& temperamentChoices()
{
    static const std::vector<std::string> c { "Equal", "Pythagorean", "Just Intonation", "Meantone 1/4", "Werckmeister III",
                                              "Kirnberger III", "Vallotti", "Young", "Custom / Scala" };
    return c;
}
inline const std::vector<std::string>& rootChoices()
{
    static const std::vector<std::string> c { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return c;
}

inline const std::vector<ParamDef>& paramTable()
{
    static const std::vector<ParamDef> t = []
    {
        std::vector<ParamDef> d;
        auto add = [&d] (const char* id, const char* name, Kind k, double mn, double mx, double def, Unit u = Unit::None,
                         bool log = false, bool noPreset = false)
        { d.push_back ({ id, name, k, mn, mx, def, u, log, noPreset, {} }); };
        auto choice = [&d] (const char* id, const char* name, const std::vector<std::string>& c, int def)
        { d.push_back ({ id, name, Kind::Choice, 0, (double) (c.size() - 1), (double) def, Unit::None, false, false, c }); };

        choice ("instrument", "Instrument", instrumentChoices(), 0);
        add ("master",       "Master",         Kind::Float, 0, 1, 0.8, Unit::Percent, false, true);
        add ("toneBass",     "Tone Bass",      Kind::Float, -12, 12, 0, Unit::Decibels);
        add ("toneTreble",   "Tone Treble",    Kind::Float, -12, 12, 0, Unit::Decibels);
        add ("hammerHardness", "Hammer Hardness", Kind::Float, 0, 1, 0.5, Unit::Percent);
        add ("dynamics",     "Dynamics",       Kind::Float, 0, 1, 0.5, Unit::Percent);
        add ("velCurve",     "Velocity Curve", Kind::Float, -1, 1, 0, Unit::BipolarPercent);
        add ("stringResonance",     "String Resonance",     Kind::Float, 0, 1, 0.5, Unit::Percent);
        add ("soundboardResonance", "Soundboard Resonance", Kind::Float, 0, 1, 0.5, Unit::Percent);
        add ("stereoWidth",  "Stereo Width",   Kind::Float, 0, 1, 0.7, Unit::Percent);
        add ("reverbMix",    "Reverb Mix",     Kind::Float, 0, 1, 0.18, Unit::Percent);
        add ("reverbSize",   "Reverb Size",    Kind::Float, 0, 1, 0.5, Unit::Percent);
        add ("reverbDamp",   "Reverb Damping", Kind::Float, 0, 1, 0.5, Unit::Percent);
        choice ("reverbType", "Reverb Type", reverbTypeChoices(), 2);
        add ("reverbPredelay", "Reverb Predelay", Kind::Float, 0, 100, 12, Unit::Milliseconds);

        add ("inharmonicity",   "Inharmonicity",    Kind::Float, 0.25, 4, 1, Unit::Ratio, true);
        add ("stringStiffness", "String Stiffness", Kind::Float, 0.25, 4, 1, Unit::Ratio, true);
        add ("stringDecay",     "String Decay",     Kind::Float, 0.25, 4, 1, Unit::Ratio, true);
        add ("hammerPosition",  "Hammer Position",  Kind::Float, 0.5, 2, 1, Unit::Ratio, true);
        add ("hammerMass",      "Hammer Mass",      Kind::Float, 0.5, 2, 1, Unit::Ratio, true);
        add ("feltStiffness",   "Felt Stiffness",   Kind::Float, 0.25, 4, 1, Unit::Ratio, true);
        add ("feltExponent",    "Felt Exponent",    Kind::Float, -1, 1, 0, Unit::Exponent);
        add ("unisonDetune",    "Unison Detune",    Kind::Float, 0, 6, 1.0, Unit::Cents);
        add ("damperEfficiency","Damper Efficiency",Kind::Float, 0.25, 4, 1, Unit::Ratio, true);
        add ("bridgeCoupling",  "Bridge Coupling",  Kind::Float, 0, 1, 0.5, Unit::Percent);
        add ("soundboardSize",  "Soundboard Size",  Kind::Float, 0.7, 1.4, 1, Unit::Ratio, true);
        add ("soundboardDamping", "Soundboard Damping", Kind::Float, 0.3, 3, 1, Unit::Ratio, true);
        add ("soundboardBrightness", "Soundboard Brightness", Kind::Float, -1, 1, 0, Unit::BipolarPercent);

        choice ("micPerspective", "Mic Perspective", perspectiveChoices(), 0);
        add ("micDistance", "Mic Distance", Kind::Float, 0.3, 8, 1.2, Unit::Metres, true);
        add ("micSpread",   "Mic Spread",   Kind::Float, 0, 1, 0.6, Unit::Percent);
        add ("micPosition", "Mic Position", Kind::Float, -1, 1, 0, Unit::BipolarPercent);
        add ("earlyReflections", "Early Reflections", Kind::Float, 0, 1, 0.3, Unit::Percent);

        add ("polyphony", "Polyphony", Kind::Int, 8, 128, 64);
        choice ("pedalMode", "Pedal Mode", pedalModeChoices(), 0);
        add ("halfPedalPoint", "Half-Pedal Point", Kind::Float, 0.1, 0.9, 0.5, Unit::Percent);
        add ("softPedalDepth", "Soft Pedal Depth", Kind::Float, 0, 1, 0.6, Unit::Percent);
        add ("pitchBendRange", "Pitch Bend Range", Kind::Int, 0, 12, 2, Unit::Semitones);

        add ("noiseHammer",     "Hammer Noise",      Kind::Float, 0, 1, 0.5, Unit::Percent);
        add ("noiseKeyRelease", "Key Release Noise", Kind::Float, 0, 1, 0.4, Unit::Percent);
        add ("noiseDamper",     "Damper Noise",      Kind::Float, 0, 1, 0.4, Unit::Percent);
        add ("noisePedal",      "Pedal Noise",       Kind::Float, 0, 1, 0.4, Unit::Percent);
        add ("noiseMaster",     "Mechanical Noise",  Kind::Float, 0, 1, 0.5, Unit::Percent);

        choice ("temperament", "Temperament", temperamentChoices(), 0);
        choice ("temperamentRoot", "Temperament Root", rootChoices(), 0);
        add ("concertPitch", "Concert Pitch", Kind::Float, 415, 466, 440, Unit::Hz);
        add ("stretch",      "Stretch Tuning", Kind::Float, 0, 1, 0.6, Unit::Percent);

        add ("presetGain", "Preset Gain", Kind::Float, -24, 12, 0, Unit::Decibels);
        return d;
    }();
    return t;
}

inline const ParamDef& paramDef (int i) { return paramTable()[(size_t) i]; }

inline int paramIndexForId (const std::string& id)
{
    const auto& t = paramTable();
    for (size_t i = 0; i < t.size(); ++i)
        if (id == t[i].id) return (int) i;
    return -1;
}

inline double paramToNorm (const ParamDef& d, double v)
{
    v = clampT (v, d.min, d.max);
    if (d.logSkew) return std::log (v / d.min) / std::log (d.max / d.min);
    return (v - d.min) / (d.max - d.min);
}
inline double paramFromNorm (const ParamDef& d, double n)
{
    n = clampT (n, 0.0, 1.0);
    if (d.logSkew) return d.min * std::pow (d.max / d.min, n);
    return d.min + (d.max - d.min) * n;
}

using ParamValues = std::array<float, kNumParams>;

inline ParamValues defaultParamValues()
{
    ParamValues v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = (float) paramDef (i).def;
    return v;
}

// Parameters that change the pre-computed note tables (rebuilt on a worker thread)
// rather than being read live by the audio thread.
inline bool paramAffectsTables (int i)
{
    switch (i)
    {
        case instrument: case hammerHardness: case inharmonicity: case stringStiffness: case stringDecay:
        case hammerPosition: case hammerMass: case feltStiffness: case feltExponent: case unisonDetune:
        case damperEfficiency: case soundboardSize: case soundboardDamping:
        case temperament: case temperamentRoot: case concertPitch: case stretch:
            return true;
        default:
            return false;
    }
}
} // namespace resonique
