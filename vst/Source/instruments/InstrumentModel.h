#pragma once
// ============================================================================
//  InstrumentModel: the physical description of one piano, as curves across
//  the keyboard (string scaling, hammers, dampers, bridge, soundboard). The
//  ModelBuilder turns it, together with the user parameters, into the per-note
//  modal tables the audio thread plays from. Nothing here runs per sample.
// ============================================================================
#include "../dsp/Common.h"

namespace resonique
{
struct InstrumentModel
{
    const char* name = "Concert Grand";
    uint32_t seed = 1;                 // deterministic per-instrument randomness

    // ---- string scaling -----------------------------------------------------
    int singleUpTo = 28;               // highest MIDI note with one string (E1)
    int doubleUpTo = 47;               // highest MIDI note with two strings (B2)
    NoteCurve lengthM;                 // speaking length (m)
    NoteCurve tensionN;                // tension per string (N)
    NoteCurve coreDiameterMm;          // steel core diameter (stiffness)
    NoteCurve inharmonicityScale;      // x physical B
    NoteCurve lossBase;                // sigma0: air + internal loss (1/s)
    NoteCurve lossLinear;              // per kHz (viscoelastic)
    NoteCurve lossQuad;                // per kHz^2
    double boardLossScale = 1.0;       // x soundboard-admittance loss (T Y / L)
    double partialJitterCents = 0.0;   // random per-partial detune (prepared / worn strings)
    double partialLossJitter = 0.0;    // random per-partial loss spread (0..1)

    // ---- hammers ------------------------------------------------------------
    NoteCurve hammerMassG;             // grams
    NoteCurve feltK;                   // N / m^p  (geometric interpolation)
    NoteCurve feltP;                   // exponent
    NoteCurve strikeRatio;             // x_h / L
    double hammerVelPP = 0.25;         // m/s at velocity 1
    double hammerVelFF = 6.0;          // m/s at velocity 127
    double feltHysteresis = 0.6;       // Hunt-Crossley lambda (s/m)
    double hammerNoiseColour = 0.5;    // 0 = dull thump, 1 = bright knock

    // ---- unison -------------------------------------------------------------
    double unisonScale = 1.0;          // x the Unison Detune parameter
    double unisonExtraCents = 0.0;     // added detune (worn / honky-tonk)
    double unisonRandom = 0.25;        // random spread of the per-string offsets (fraction)

    // ---- dampers ------------------------------------------------------------
    int lastDamperNote = 88;           // E6: higher notes have no damper
    NoteCurve damperRate;              // 1/s at full pressure (fundamental)
    double damperDropMs = 25.0;        // time for a damper to settle on the string

    // ---- bridge (local, per note) --------------------------------------------
    double bridgeMassKg = 0.05;
    double bridgeResistance = 2600.0;  // N s / m  (admittance ~ 1/R)
    NoteCurve bridgeAdmittanceScale;   // x local admittance along the compass

    // ---- soundboard ---------------------------------------------------------
    double boardFirstModeHz = 48.0;
    double boardModeSpread = 1.0;      // x spacing of the mode series
    double boardQLow = 32.0, boardQHigh = 18.0;
    double boardModalMassKg = 1.6;
    double boardAdmittanceBg = 5.0e-4; // background (dense-mode) admittance, m/s/N
    double boardRadiationCornerHz = 500.0;
    double boardHfLevel = 1.0;         // direct (dense high-mode) radiation level
    double boardSympathetic = 1.0;     // x feedback into the strings
    double boardModalGain = 1.0;       // x modal admittance

    // ---- output -------------------------------------------------------------
    double outputGain = 1.0;           // x calibration gain
    double noiseLevel = 1.0;           // x mechanical noise
    NoteCurve levelDb;                 // per-key level balance across the compass (dB)
};

inline int stringsForNote (const InstrumentModel& m, int midiNote)
{
    return midiNote <= m.singleUpTo ? 1 : (midiNote <= m.doubleUpTo ? 2 : 3);
}
} // namespace resonique
