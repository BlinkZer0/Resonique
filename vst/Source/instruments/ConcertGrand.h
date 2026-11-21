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
    m.coreDiameterMm = { { 21, 1.45 }, { 33, 1.25 }, { 45, 1.10 }, { 60, 1.05 }, { 84, 0.95 }, { 108, 0.80 } };
    m.inharmonicityScale = { { 21, 1.0 }, { 108, 1.0 } };
    m.lossBase   = { { 21, 0.10 }, { 60, 0.12 }, { 96, 0.2 }, { 108, 0.4 } };
    m.lossLinear = { { 21, 0.5 }, { 60, 0.6 }, { 96, 0.5 }, { 108, 0.4 } };
    m.lossQuad   = { { 21, 0.10 }, { 60, 0.12 }, { 108, 0.12 } };
    m.boardLossScale = 1.0;

    m.hammerMassG = { { 21, 11.2 }, { 48, 9.0 }, { 60, 8.1 }, { 84, 5.6 }, { 108, 3.8 } };
    m.feltK = { { 21, 3.0e8 }, { 36, 4.0e8 }, { 60, 4.5e9 }, { 84, 1.0e11 }, { 108, 1.2e12 } };
    m.feltP = { { 21, 2.3 }, { 60, 2.55 }, { 84, 2.8 }, { 108, 3.1 } };
    m.strikeRatio = { { 21, 0.118 }, { 48, 0.123 }, { 72, 0.115 }, { 96, 0.095 }, { 108, 0.080 } };
    m.hammerVelPP = 0.25;
    m.hammerVelFF = 6.0;
    m.feltHysteresis = 0.6;
    m.hammerNoiseColour = 0.5;

    m.unisonScale = 1.0;
    m.unisonExtraCents = 0.0;
    m.unisonRandom = 0.25;

    m.lastDamperNote = 88;
    m.damperRate = { { 21, 14.0 }, { 40, 24.0 }, { 60, 40.0 }, { 88, 55.0 } };
    m.damperDropMs = 25.0;

    m.bridgeMassKg = 0.05;
    m.bridgeResistance = 4000.0;
    m.bridgeAdmittanceScale = { { 21, 0.8 }, { 60, 1.0 }, { 108, 1.3 } };

    m.boardFirstModeHz = 48.0;
    m.boardModeSpread = 1.0;
    m.boardQLow = 32.0;
    m.boardQHigh = 18.0;
    m.boardModalMassKg = 1.6;
    m.boardAdmittanceBg = 3.5e-4;
    m.boardRadiationCornerHz = 500.0;
    m.boardHfLevel = 1.0;
    m.boardSympathetic = 1.0;
    m.boardModalGain = 1.0;

    m.outputGain = 1.0;
    m.noiseLevel = 1.0;
    // the modal board radiates the top two octaves more efficiently than the rest; this evens the compass
    // (calibrated from the --render peaks at velocity 90)
    m.levelDb = { { 21, 0.0 }, { 48, 0.0 }, { 60, 2.0 }, { 72, 2.0 }, { 84, 0.0 }, { 92, -6.0 }, { 100, -9.0 }, { 108, -8.0 } };
    return m;
}
} // namespace resonique
