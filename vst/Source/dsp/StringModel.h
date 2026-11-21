#pragma once
// ============================================================================
//  The pre-computed tables the audio thread plays from: one NoteTable per key
//  (up to three StringTables, the hammer, damper and bridge numbers) and the
//  SoundboardTable. Built by ModelBuilder from an InstrumentModel + the user
//  parameters + the tuning, for one sample rate; voices copy what they need
//  at note-on, so a table can be swapped at any block boundary.
//
//  String physics (per partial n of a stiff string, f0 the fundamental):
//      f_n   = n f0 sqrt((1 + B n^2) / (1 + B))      (normalised so f_1 = f0)
//      sigma = sigma0 + sigma1 f + sigma2 f^2 + T Y(f) / L   (air/internal + bridge loss)
//  Modal state per partial is (p, q) = (displacement, velocity / omega), rotated
//  each sample by (rc, rs) = e^(-sigma dt) (cos omega dt, sin omega dt).
// ============================================================================
#include "Common.h"
#include "../instruments/InstrumentModel.h"

namespace resonique
{
struct StringTable
{
    int numPartials = 0;
    float freq[kMaxPartials] {};          // Hz
    float sigma[kMaxPartials] {};         // 1/s
    float rc[kMaxPartials] {};            // rotation coefficients (sample-rate specific)
    float rs[kMaxPartials] {};
    float hammerShape[kMaxPartials] {};   // phi_n(x_h): mode shape at the strike point (with felt-width roll-off)
    float hammerInj[kMaxPartials] {};     // force (N) -> dq per sample
    float bridgeW[kMaxPartials] {};       // p (m) -> force on the bridge (N)
    float damperSigma[kMaxPartials] {};   // extra loss at full damper pressure (1/s)
    float coupleInj[kCouplePartials] {};  // bridge acceleration (m/s^2) -> dq per sample
    double f0 = 440;                      // this string's fundamental (unison detune included)
    double tension = 700, length = 0.6, linearDensity = 0.007;
    double staticK = 0;                   // T / L: bridge displacement -> force
};

struct NoteTable
{
    int midiNote = 60;
    int numStrings = 1;
    int zone = 0;                         // bridge zone (0 = bass end)
    double f0 = 261.6;                    // nominal fundamental
    double inharmonicity = 0;             // B
    StringTable strings[kMaxStrings];

    // hammer
    double hammerMass = 0.008;            // kg
    double feltK = 4e9;                   // N / m^p
    double feltP = 2.5;
    double feltLambda = 0.6;              // s/m
    double velPP = 0.25, velFF = 6.0;     // m/s
    double hammerNoiseColour = 0.5;

    // damper
    bool hasDamper = true;
    double damperDropMs = 25;

    // local bridge (shared by the unison strings)
    double bridgeMass = 0.05;             // kg
    double bridgeR = 1200;                // N s / m

    double pan = 0;                       // -1 (bass) .. 1 (treble), player's perspective
    float gain = 1.0f;                    // per-key level balance
};

struct BoardMode
{
    float freq = 100, sigma = 10;
    float rc = 1, rs = 0;
    float inj = 0;                        // force (N) -> dq per sample  (dt / (M omega))
    float velW = 1;                       // q -> velocity (omega)
    float zoneW[kNumZones] {};            // mode shape at the bridge zones
    float outL = 0, outR = 0;             // radiation weights (velocity -> pressure)
};

struct SoundboardTable
{
    BoardMode modes[kSoundboardModes];
    double modalMass = 1.6;
    double bgAdmittance = 5e-4;
    double hfLevel = 1.0;                 // direct high-frequency radiation level
    double sympathetic = 1.0;             // x feedback into the strings
    double radiationCorner = 500;
    // Real part of the driving-point admittance at a zone (m/s/N): the modal part (scaled by
    // modalWeight) plus the background of the dense modes above the bank
    double admittanceAt (double hz, int zone, double modalWeight = 1.0) const
    {
        const double w = kTwoPi * hz;
        double y = 0;
        for (const auto& m : modes)
        {
            const double wm = kTwoPi * m.freq, s = m.sigma;
            const double d = wm * wm - w * w;
            const double re = 2.0 * s * w * w / (d * d + 4.0 * s * s * w * w);
            const double shape = m.zoneW[(size_t) zone];
            y += shape * shape * re / modalMass;
        }
        return y * modalWeight + bgAdmittance * (1.0 + 0.2 * hz / 4000.0);
    }
};

struct InstrumentTables
{
    double sampleRate = 48000;
    int instrumentIndex = 0;
    InstrumentModel model;                // the physical description (globals used at run time)
    NoteTable notes[kNumNotes];
    SoundboardTable board;
    double outputGain = 1.0;
    uint32_t buildId = 0;
};

inline int zoneForNote (int midiNote)
{
    return clampT ((midiNote - kFirstNote) * kNumZones / kNumNotes, 0, kNumZones - 1);
}
} // namespace resonique
