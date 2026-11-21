#pragma once
// ============================================================================
//  Tuning: equal temperament, historical temperaments, Scala (.scl) scales,
//  concert pitch, stretch (Railsback-style) and per-note detune.
//  Everything here runs off the audio thread (it feeds the note tables).
// ============================================================================
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include "Common.h"

namespace resonique
{
enum class Temperament { Equal, Pythagorean, Just, Meantone, Werckmeister, Kirnberger, Vallotti, Young, Custom };

// Offsets from equal temperament in cents for the 12 pitch classes, root = C.
inline const std::array<double, 12>& temperamentOffsets (Temperament t)
{
    static const std::array<double, 12> equal        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    static const std::array<double, 12> pythagorean  { 0, 13.7, 3.9, -5.9, 7.8, -2.0, 11.7, 2.0, 15.6, 5.9, -3.9, 9.8 };
    static const std::array<double, 12> just         { 0, 11.7, 3.9, 15.6, -13.7, -2.0, -9.8, 2.0, 13.7, -15.6, 17.6, -11.7 };
    static const std::array<double, 12> meantone     { 0, -24.0, -6.8, 10.3, -13.7, 3.4, -20.5, -3.4, -27.4, -10.3, 6.8, -17.1 };
    static const std::array<double, 12> werckmeister { 0, -9.8, -7.8, -5.9, -9.8, -2.0, -11.7, -3.9, -7.8, -11.7, -3.9, -7.8 };
    static const std::array<double, 12> kirnberger   { 0, -9.8, -6.8, -5.9, -13.7, -2.0, -11.7, -3.4, -7.8, -10.3, -3.9, -11.7 };
    static const std::array<double, 12> vallotti     { 0, -5.9, -3.9, -2.0, -7.8, 2.0, -7.8, -2.0, -3.9, -5.9, 0, -9.8 };
    static const std::array<double, 12> young        { 0, -6.1, -3.9, -2.1, -7.8, 2.0, -8.0, -2.0, -4.1, -5.9, 0.0, -9.9 };
    switch (t)
    {
        case Temperament::Pythagorean: return pythagorean;
        case Temperament::Just:        return just;
        case Temperament::Meantone:    return meantone;
        case Temperament::Werckmeister: return werckmeister;
        case Temperament::Kirnberger:  return kirnberger;
        case Temperament::Vallotti:    return vallotti;
        case Temperament::Young:       return young;
        default:                       return equal;
    }
}

// A Scala scale: degrees in cents above the root (the last one is the period, usually 1200).
struct ScalaScale
{
    std::string description;
    std::vector<double> cents;   // size N >= 1, cents[N-1] = period
    bool valid() const { return ! cents.empty(); }

    // a whole token as a number (no exceptions: the same source builds without them for the web)
    static bool parseNumber (const std::string& tok, double& value)
    {
        if (tok.empty()) return false;
        char* end = nullptr;
        value = std::strtod (tok.c_str(), &end);
        return end != nullptr && *end == '\0' && end != tok.c_str();
    }

    // Parses .scl text. Returns false and leaves the scale empty on error.
    static bool parse (const std::string& text, ScalaScale& out, std::string& error)
    {
        out = {};
        std::istringstream in (text);
        std::string line;
        int stage = 0, expected = -1;
        std::vector<double> degrees;
        while (std::getline (in, line))
        {
            if (! line.empty() && line.back() == '\r') line.pop_back();
            if (! line.empty() && line[0] == '!') continue;              // comment
            if (stage == 0) { out.description = line; stage = 1; continue; }
            // trim
            const auto first = line.find_first_not_of (" \t");
            if (first == std::string::npos) continue;
            line = line.substr (first);
            if (stage == 1)
            {
                double n = 0;
                const std::string tok = line.substr (0, line.find_first_of (" \t"));
                if (! parseNumber (tok, n) || n < 0 || n > 4096 || n != std::floor (n)) { error = "Bad note count"; out = {}; return false; }
                expected = (int) n;
                stage = 2;
                continue;
            }
            // a pitch: "cents" (contains '.'), "n/d" ratio, or integer ratio
            std::string tok = line.substr (0, line.find_first_of (" \t"));
            double c = 0;
            bool ok = true;
            if (tok.find ('.') != std::string::npos) ok = parseNumber (tok, c);
            else if (tok.find ('/') != std::string::npos)
            {
                double n = 0, d = 0;
                ok = parseNumber (tok.substr (0, tok.find ('/')), n) && parseNumber (tok.substr (tok.find ('/') + 1), d) && n > 0 && d > 0;
                if (ok) c = 1200.0 * std::log2 (n / d);
            }
            else
            {
                double n = 0;
                ok = parseNumber (tok, n) && n > 0;
                if (ok) c = 1200.0 * std::log2 (n);
            }
            if (! ok) { error = "Bad pitch: " + tok; out = {}; return false; }
            degrees.push_back (c);
            if ((int) degrees.size() == expected) break;
        }
        if (expected < 0) { error = "Not a Scala file"; return false; }
        if (expected == 0) { out.cents = { 1200.0 }; return true; }
        if ((int) degrees.size() != expected) { error = "Scale lists fewer pitches than declared"; out = {}; return false; }
        out.cents = degrees;
        return true;
    }

    // cents of `note` relative to the root note (root = degree 0 at 0 cents)
    double centsFromRoot (int note, int rootNote) const
    {
        const int n = (int) cents.size();
        const double period = cents[(size_t) n - 1];
        int steps = note - rootNote;
        int octaves = 0;
        while (steps < 0) { steps += n; --octaves; }
        octaves += steps / n;
        const int deg = steps % n;
        return octaves * period + (deg == 0 ? 0.0 : cents[(size_t) deg - 1]);
    }
};

struct TuningSettings
{
    Temperament temperament = Temperament::Equal;
    int root = 0;                       // pitch class 0..11 (C..B)
    double concertPitch = 440.0;
    double stretch = 0.6;               // 0 = none, 1 = strong Railsback curve
    std::array<double, kNumNotes> noteDetune {};   // cents per note (user tuning editor)
    ScalaScale scala;                   // used when temperament == Custom and valid
};

// Railsback-style stretch curve in cents (at stretch = 1).
inline double stretchCents (int midiNote)
{
    // flat through the middle, lower in the bass, higher in the treble; roughly
    // A0 -28 ct, C2 -10 ct, C4..A4 0 ct, C6 +6 ct, C7 +15 ct, C8 +33 ct.
    const double n = (double) midiNote;
    if (n < 57) { const double x = (57 - n) / 36.0; return -28.0 * x * x; }
    if (n > 72) { const double x = (n - 72) / 36.0; return 33.0 * x * x; }
    return 0.0;
}

// The fundamental frequency of a note before unison detune.
inline double noteFrequency (int midiNote, const TuningSettings& t)
{
    double cents = 0.0;
    if (t.temperament == Temperament::Custom && t.scala.valid())
    {
        const int rootNote = 60 + t.root;    // the scale's root is in the C4 octave
        cents = t.scala.centsFromRoot (midiNote, rootNote) - 100.0 * (midiNote - rootNote);
    }
    else
    {
        const auto& off = temperamentOffsets (t.temperament);
        const int pc = ((midiNote - t.root) % 12 + 12) % 12;
        cents = off[(size_t) pc];
    }
    cents += t.stretch * stretchCents (midiNote);
    const int idx = midiNote - kFirstNote;
    if (idx >= 0 && idx < kNumNotes) cents += t.noteDetune[(size_t) idx];
    return midiToHz (midiNote + cents / 100.0, t.concertPitch);
}
} // namespace resonique
