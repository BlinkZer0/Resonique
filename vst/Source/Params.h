#pragma once
// ============================================================================
//  JUCE side of the parameter table (dsp/ParamTable.h): value formatting, the
//  APVTS layout, and the JSON dump the parity check compares with params.js.
// ============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/ParamTable.h"

namespace resonique
{
inline juce::String formatValue (Unit unit, double v)
{
    switch (unit)
    {
        case Unit::Percent:        return juce::String (juce::roundToInt (v * 100.0)) + "%";
        case Unit::BipolarPercent: return (v > 0.004 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%";
        case Unit::Decibels:       return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB";
        case Unit::Hz:             return juce::String (v, 1) + " Hz";
        case Unit::Cents:          return juce::String (v, 2) + " ct";
        case Unit::Semitones:      return juce::String (juce::roundToInt (v)) + " st";
        case Unit::Metres:         return juce::String (v, 2) + " m";
        case Unit::Milliseconds:   return juce::String (juce::roundToInt (v)) + " ms";
        case Unit::Ratio:          return "x" + juce::String (v, 2);
        case Unit::Exponent:       return (v > 0.004 ? "+" : "") + juce::String (v, 2);
        case Unit::None:
        default:                   return juce::String (v, 2);
    }
}

// Same log mapping as paramToNorm / paramFromNorm (ParamTable.h, params.js)
inline juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float start, float end, float n) { return start * std::pow (end / start, n); },
             [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
             [] (float start, float end, float v) { return juce::jlimit (start, end, v); } };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& d : paramTable())
    {
        const juce::ParameterID pid { d.id, 1 };
        const auto unit = d.unit;
        switch (d.kind)
        {
            case Kind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.def >= 0.5));
                break;
            case Kind::Choice:
            {
                juce::StringArray choices;
                for (const auto& c : d.choices) choices.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, choices, (int) d.def));
                break;
            }
            case Kind::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    pid, d.name, (int) d.min, (int) d.max, (int) d.def,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction (
                        [unit] (int v, int) { return formatValue (unit, (double) v); })));
                break;
            case Kind::Float:
            default:
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, d.name,
                    d.logSkew ? logRange ((float) d.min, (float) d.max) : juce::NormalisableRange<float> ((float) d.min, (float) d.max),
                    (float) d.def,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                        [unit] (float v, int) { return formatValue (unit, (double) v); })));
                break;
        }
    }
    return layout;
}

inline juce::String paramTableToJson()
{
    juce::Array<juce::var> list;
    for (const auto& d : paramTable())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", d.id);
        o->setProperty ("name", d.name);
        o->setProperty ("kind", d.kind == Kind::Float ? "float" : d.kind == Kind::Bool ? "bool" : d.kind == Kind::Choice ? "choice" : "int");
        o->setProperty ("min", d.min);
        o->setProperty ("max", d.max);
        o->setProperty ("def", d.def);
        o->setProperty ("log", d.logSkew);
        o->setProperty ("noPreset", d.noPreset);
        if (! d.choices.empty())
        {
            juce::Array<juce::var> c;
            for (const auto& s : d.choices) c.add (juce::String (s));
            o->setProperty ("choices", c);
        }
        list.add (juce::var (o));
    }
    return juce::JSON::toString (juce::var (list));
}
} // namespace resonique
