#pragma once
// ============================================================================
//  The calibrated base instrument: a 2.7 m concert grand.
//  Numbers follow published piano-acoustics data (Fletcher & Rossing,
//  Chaigne & Askenfelt, Conklin): string scaling with ~170 m/s * (1/f)
//  lengths in the treble capped at ~2 m in the bass, tensions of 700-1300 N,
//  hammer masses of 11 g (A0) to 3.8 g (C8), felt stiffness rising from
//  4e8 to 1e12 N/m^p across the compass, strike points of 1/8 .. 1/12.
// ============================================================================
#include "InstrumentModel.h"

namespace resonique
{
inline InstrumentModel concertGrandModel()
{
    InstrumentModel m;
    m.name = "Concert Grand";
    m.seed = 0x5a17c0de;
    m.singleUpTo = 28;
    m.doubleUpTo = 47;
    // speaking length: 1.98 m at A0, 1.3 m at C2, 0.63 m at C4, 0.165 m at C6, 0.052 m at C8
    m.lengthM = { { 21, 1.98 }, { 36, 1.30 }, { 48, 0.98 }, { 60, 0.63 }, { 72, 0.33 }, { 84, 0.165 }, { 96, 0.090 }, { 108, 0.052 } };
    m.tensionN = { { 21, 1250 }, { 36, 1050 }, { 48, 900 }, { 60, 760 }, { 84, 740 }, { 108, 700 } };
    // steel core (bass strings are copper-wound on a thinner core; stiffness comes from the core)
    // (treble cores thinner than the published plain-wire gauges: the reference recordings' B at C6-C7 is
    //  about 1.5e-3, which d^4 reaches with 0.85 / 0.72 mm)
    m.coreDiameterMm = { { 21, 1.45 }, { 33, 1.25 }, { 45, 1.10 }, { 60, 1.05 }, { 84, 0.85 }, { 108, 0.72 } };
    m.inharmonicityScale = { { 21, 1.0 }, { 108, 1.0 } };
    // losses calibrated against the Iowa reference notes (T60 of the fundamental: C2 21 s, C4 18 s, C6 7 s, C7 2.7 s)
    m.lossBase   = { { 21, 0.04 }, { 60, 0.05 }, { 96, 0.10 }, { 108, 0.20 } };
    m.lossLinear = { { 21, 0.40 }, { 60, 0.45 }, { 96, 0.35 }, { 108, 0.30 } };
    m.lossQuad   = { { 21, 0.06 }, { 60, 0.08 }, { 108, 0.08 } };
    m.boardLossScale = 1.0;

    m.hammerMassG = { { 21, 11.2 }, { 48, 9.0 }, { 60, 8.1 }, { 84, 5.6 }, { 108, 3.8 } };
    // felt law K and p chosen for the force at 0.5 mm compression: 7.5 N (A0), 17 N (C4), 25 N (C6), 60 N (C8)
    m.feltK = { { 21, 3.0e8 }, { 36, 4.0e8 }, { 60, 3.2e9 }, { 84, 2.5e9 }, { 108, 2.0e10 } };
    m.feltP = { { 21, 2.3 }, { 60, 2.5 }, { 84, 2.5 }, { 108, 2.7 } };
    m.strikeRatio = { { 21, 0.118 }, { 48, 0.123 }, { 72, 0.115 }, { 96, 0.095 }, { 108, 0.080 } };
    m.hammerVelPP = 0.25;
    m.hammerVelFF = 6.0;
    m.feltHysteresis = 0.25;   // a mildly asymmetric force pulse (the references carry little energy above partial 3)
    m.hammerNoiseColour = 0.5;

    m.unisonScale = 1.0;
    m.unisonExtraCents = 0.0;
    m.unisonRandom = 0.25;

    m.lastDamperNote = 88;
    m.damperRate = { { 21, 14.0 }, { 40, 24.0 }, { 60, 40.0 }, { 88, 55.0 } };
    m.damperDropMs = 25.0;

    m.bridgeMassKg = 0.05;
    m.bridgeResistance = 16000.0;
    m.bridgeAdmittanceScale = { { 21, 0.8 }, { 60, 1.0 }, { 108, 1.3 } };

    m.boardFirstModeHz = 48.0;
    m.boardModeSpread = 1.0;
    m.boardQLow = 32.0;
    m.boardQHigh = 18.0;
    m.boardModalMassKg = 1.6;
    m.boardAdmittanceBg = 1.0e-4;
    m.boardRadiationCornerHz = 150.0;
    m.boardHfLevel = 1.0;
    m.boardSympathetic = 1.0;
    m.boardModalGain = 1.0;

    m.outputGain = 1.0;
    m.noiseLevel = 1.0;
    // the modal board radiates the top two octaves more efficiently than the rest; this evens the compass
    // (calibrated from the --render peaks at velocity 90)
    m.levelDb = { { 21, -2.0 }, { 36, -6.0 }, { 48, -6.0 }, { 60, -2.0 }, { 72, 0.0 }, { 84, 0.0 }, { 92, 3.0 }, { 100, 4.0 }, { 108, 7.0 } };
    return m;
}
} // namespace resonique
