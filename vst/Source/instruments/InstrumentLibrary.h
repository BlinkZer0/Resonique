#pragma once
// ============================================================================
//  The instrument library: ten pianos, each a different physical parameter set
//  derived from the calibrated concert grand (not EQ presets). Index order
//  matches instrumentChoices() in ParamTable.h.
// ============================================================================
#include "ConcertGrand.h"

namespace resonique
{
inline InstrumentModel studioGrandModel()
{
    InstrumentModel m = concertGrandModel();
    m.name = "Studio Grand";
    m.seed = 0x7a2b19e3;
    // a 1.9 m instrument: shorter bass strings, a little more inharmonicity, a smaller board
    m.lengthM = { { 21, 1.42 }, { 36, 1.05 }, { 48, 0.85 }, { 60, 0.60 }, { 72, 0.32 }, { 84, 0.160 }, { 96, 0.088 }, { 108, 0.051 } };
    m.tensionN = { { 21, 1000 }, { 36, 900 }, { 48, 820 }, { 60, 740 }, { 84, 720 }, { 108, 690 } };
    m.inharmonicityScale = { { 21, 1.35 }, { 60, 1.1 }, { 108, 1.0 } };
    m.singleUpTo = 30;
    m.doubleUpTo = 44;
    m.boardFirstModeHz = 62.0;
    m.boardModalMassKg = 1.2;
    m.boardQLow = 28.0;
    m.feltK = { { 21, 3.5e8 }, { 36, 5.0e8 }, { 60, 5.5e9 }, { 84, 1.3e11 }, { 108, 1.4e12 } };
    m.damperRate = { { 21, 18.0 }, { 40, 28.0 }, { 60, 45.0 }, { 88, 60.0 } };
    return m;
}

inline InstrumentModel uprightModel()
{
    InstrumentModel m = concertGrandModel();
    m.name = "Upright";
    m.seed = 0x11aa33cc;
    // 1.2 m upright: short, lower-tension strings, high inharmonicity, small stiff board, weak bass dampers
    m.lengthM = { { 21, 1.15 }, { 36, 0.92 }, { 48, 0.74 }, { 60, 0.55 }, { 72, 0.30 }, { 84, 0.150 }, { 96, 0.082 }, { 108, 0.049 } };
    m.tensionN = { { 21, 820 }, { 36, 760 }, { 48, 720 }, { 60, 680 }, { 84, 660 }, { 108, 640 } };
    m.coreDiameterMm = { { 21, 1.55 }, { 33, 1.35 }, { 45, 1.15 }, { 60, 1.08 }, { 84, 0.98 }, { 108, 0.82 } };
    m.inharmonicityScale = { { 21, 1.8 }, { 48, 1.4 }, { 72, 1.15 }, { 108, 1.05 } };
    m.singleUpTo = 32;
    m.doubleUpTo = 46;
    m.lossBase = { { 21, 0.22 }, { 60, 0.3 }, { 96, 0.9 }, { 108, 1.8 } };
    m.lossLinear = { { 21, 0.7 }, { 60, 0.95 }, { 108, 1.3 } };
    m.hammerMassG = { { 21, 9.5 }, { 48, 8.0 }, { 60, 7.2 }, { 84, 5.2 }, { 108, 3.6 } };
    m.feltK = { { 21, 3.0e8 }, { 36, 4.5e8 }, { 60, 4.0e9 }, { 84, 8.0e10 }, { 108, 9.0e11 } };
    m.strikeRatio = { { 21, 0.13 }, { 48, 0.13 }, { 72, 0.12 }, { 96, 0.10 }, { 108, 0.085 } };
    m.damperRate = { { 21, 7.0 }, { 40, 14.0 }, { 60, 30.0 }, { 88, 45.0 } };
    m.lastDamperNote = 86;
    m.boardFirstModeHz = 78.0;
    m.boardModeSpread = 1.25;
    m.boardQLow = 24.0;
    m.boardQHigh = 14.0;
    m.boardModalMassKg = 0.9;
    m.boardAdmittanceBg = 7.0e-4;
    m.boardRadiationCornerHz = 700.0;
    m.boardHfLevel = 0.85;
    m.bridgeResistance = 2000.0;
    m.hammerNoiseColour = 0.4;
    m.noiseLevel = 1.3;
    return m;
}

inline InstrumentModel vintageUprightModel()
{
    InstrumentModel m = uprightModel();
    m.name = "Vintage Upright";
    m.seed = 0x2b4d6f81;
    // worn hammers, uneven unisons, tired strings, rattly action
    m.feltK = { { 21, 1.5e8 }, { 36, 2.2e8 }, { 60, 1.6e9 }, { 84, 3.0e10 }, { 108, 3.0e11 } };
    m.feltP = { { 21, 2.2 }, { 60, 2.4 }, { 84, 2.6 }, { 108, 2.9 } };
    m.lossLinear = { { 21, 0.9 }, { 60, 1.3 }, { 108, 1.8 } };
    m.lossQuad = { { 21, 0.16 }, { 60, 0.2 }, { 108, 0.3 } };
    m.unisonExtraCents = 1.6;
    m.unisonRandom = 0.6;
    m.partialJitterCents = 0.35;
    m.partialLossJitter = 0.35;
    m.damperRate = { { 21, 5.0 }, { 40, 10.0 }, { 60, 22.0 }, { 88, 35.0 } };
    m.boardQLow = 20.0;
    m.boardQHigh = 12.0;
    m.noiseLevel = 1.8;
    m.hammerNoiseColour = 0.3;
    return m;
}

inline InstrumentModel brightPopGrandModel()
{
    InstrumentModel m = concertGrandModel();
    m.name = "Bright Pop Grand";
    m.seed = 0x3c5e7a9b;
    // hard, lacquered hammers, lively strings, a stiff board that radiates high partials well
    m.feltK = { { 21, 6.0e8 }, { 36, 9.0e8 }, { 60, 1.2e10 }, { 84, 3.0e11 }, { 108, 3.0e12 } };
    m.feltP = { { 21, 2.5 }, { 60, 2.8 }, { 84, 3.1 }, { 108, 3.4 } };
    m.lossLinear = { { 21, 0.42 }, { 60, 0.55 }, { 108, 0.85 } };
    m.lossQuad = { { 21, 0.08 }, { 60, 0.09 }, { 108, 0.16 } };
    m.strikeRatio = { { 21, 0.11 }, { 48, 0.112 }, { 72, 0.105 }, { 96, 0.09 }, { 108, 0.078 } };
    m.boardQLow = 36.0;
    m.boardQHigh = 22.0;
    m.boardRadiationCornerHz = 400.0;
    m.boardHfLevel = 1.3;
    m.unisonScale = 0.8;
    m.hammerNoiseColour = 0.7;
    return m;
}

inline InstrumentModel darkJazzGrandModel()
{
    InstrumentModel m = concertGrandModel();
    m.name = "Dark Jazz Grand";
    m.seed = 0x4d6f8ab2;
    // needled, soft hammers; warm board with strong low modes; mellow high partials
    m.feltK = { { 21, 1.6e8 }, { 36, 2.2e8 }, { 60, 2.0e9 }, { 84, 4.0e10 }, { 108, 4.5e11 } };
    m.feltP = { { 21, 2.2 }, { 60, 2.4 }, { 84, 2.65 }, { 108, 2.9 } };
    m.lossLinear = { { 21, 0.7 }, { 60, 0.95 }, { 108, 1.3 } };
    m.lossQuad = { { 21, 0.14 }, { 60, 0.17 }, { 108, 0.28 } };
    m.boardFirstModeHz = 44.0;
    m.boardQLow = 30.0;
    m.boardQHigh = 15.0;
    m.boardRadiationCornerHz = 650.0;
    m.boardHfLevel = 0.75;
    m.boardModalGain = 1.15;
    m.unisonScale = 1.1;
    m.hammerNoiseColour = 0.35;
    return m;
}

inline InstrumentModel feltPianoModel()
{
    InstrumentModel m = concertGrandModel();
    m.name = "Felt Piano";
    m.seed = 0x5e7f9bc3;
    // a strip of felt between hammers and strings: very soft, slow contact; high partials muffled
    m.feltK = { { 21, 3.0e7 }, { 36, 4.5e7 }, { 60, 3.0e8 }, { 84, 4.0e9 }, { 108, 3.0e10 } };
    m.feltP = { { 21, 2.0 }, { 60, 2.1 }, { 84, 2.3 }, { 108, 2.5 } };
    m.feltHysteresis = 1.2;
    m.hammerVelFF = 4.5;
    m.lossLinear = { { 21, 1.1 }, { 60, 1.6 }, { 108, 2.2 } };
    m.lossQuad = { { 21, 0.3 }, { 60, 0.4 }, { 108, 0.6 } };
    m.boardHfLevel = 0.6;
    m.boardQLow = 26.0;
    m.hammerNoiseColour = 0.25;
    m.noiseLevel = 1.6;
    m.damperRate = { { 21, 20.0 }, { 40, 32.0 }, { 60, 48.0 }, { 88, 60.0 } };
    return m;
}

inline InstrumentModel preparedPianoModel()
{
    InstrumentModel m = concertGrandModel();
    m.name = "Prepared Piano";
    m.seed = 0x6f8aacd4;
    // objects on the strings: partials pushed off their stiff-string positions, uneven losses,
    // heavier contact noise, dampers that no longer seat well
    m.partialJitterCents = 14.0;
    m.partialLossJitter = 0.7;
    m.inharmonicityScale = { { 21, 2.2 }, { 60, 1.8 }, { 108, 1.4 } };
    m.lossBase = { { 21, 0.5 }, { 60, 0.8 }, { 96, 1.6 }, { 108, 2.5 } };
    m.unisonExtraCents = 3.0;
    m.unisonRandom = 0.8;
    m.feltK = { { 21, 6.0e8 }, { 36, 8.0e8 }, { 60, 8.0e9 }, { 84, 1.5e11 }, { 108, 1.5e12 } };
    m.damperRate = { { 21, 6.0 }, { 40, 10.0 }, { 60, 18.0 }, { 88, 25.0 } };
    m.hammerNoiseColour = 0.8;
    m.noiseLevel = 2.0;
    m.bridgeResistance = 1700.0;
    return m;
}

inline InstrumentModel honkyTonkModel()
{
    InstrumentModel m = uprightModel();
    m.name = "Honky-Tonk";
    m.seed = 0x7a9bbde5;
    // the classic: wide, uneven unison detune, hard worn hammers, boxy board
    m.unisonScale = 1.5;
    m.unisonExtraCents = 9.0;
    m.unisonRandom = 0.7;
    m.singleUpTo = 26;
    m.doubleUpTo = 40;
    m.feltK = { { 21, 5.0e8 }, { 36, 7.0e8 }, { 60, 7.0e9 }, { 84, 1.6e11 }, { 108, 1.6e12 } };
    m.feltP = { { 21, 2.5 }, { 60, 2.7 }, { 84, 2.95 }, { 108, 3.2 } };
    m.boardFirstModeHz = 95.0;
    m.boardModeSpread = 1.35;
    m.boardQLow = 22.0;
    m.boardQHigh = 12.0;
    m.boardHfLevel = 1.1;
    m.noiseLevel = 1.5;
    m.hammerNoiseColour = 0.6;
    return m;
}

inline InstrumentModel experimentalGrandModel()
{
    InstrumentModel m = concertGrandModel();
    m.name = "Experimental Grand";
    m.seed = 0x8baccef6;
    // an impossible instrument: 4 m strings with almost no stiffness, a huge, lively board,
    // strong sympathetic coupling, strike points near the middle (hollow, odd-harmonic tone)
    m.lengthM = { { 21, 4.0 }, { 36, 2.6 }, { 48, 1.8 }, { 60, 1.1 }, { 72, 0.55 }, { 84, 0.28 }, { 96, 0.15 }, { 108, 0.08 } };
    m.tensionN = { { 21, 1500 }, { 48, 1100 }, { 84, 900 }, { 108, 800 } };
    m.inharmonicityScale = { { 21, 0.35 }, { 108, 0.35 } };
    m.lossBase = { { 21, 0.06 }, { 60, 0.1 }, { 96, 0.3 }, { 108, 0.7 } };
    m.lossLinear = { { 21, 0.35 }, { 60, 0.45 }, { 108, 0.7 } };
    m.strikeRatio = { { 21, 0.30 }, { 60, 0.35 }, { 108, 0.42 } };
    m.boardFirstModeHz = 30.0;
    m.boardModeSpread = 0.8;
    m.boardQLow = 60.0;
    m.boardQHigh = 30.0;
    m.boardModalMassKg = 3.0;
    m.boardSympathetic = 2.5;
    m.boardModalGain = 1.3;
    m.unisonScale = 1.3;
    m.damperRate = { { 21, 6.0 }, { 40, 10.0 }, { 60, 20.0 }, { 88, 30.0 } };
    return m;
}

inline InstrumentModel instrumentModel (int index)
{
    switch (index)
    {
        case 1: return studioGrandModel();
        case 2: return uprightModel();
        case 3: return vintageUprightModel();
        case 4: return brightPopGrandModel();
        case 5: return darkJazzGrandModel();
        case 6: return feltPianoModel();
        case 7: return preparedPianoModel();
        case 8: return honkyTonkModel();
        case 9: return experimentalGrandModel();
        default: return concertGrandModel();
    }
}
constexpr int kNumInstruments = 10;
} // namespace resonique
