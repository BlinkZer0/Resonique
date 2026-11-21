#pragma once
// ============================================================================
//  ModelBuilder: InstrumentModel + parameters + tuning + sample rate -> tables.
//  Runs on a worker thread (or at prepare time); never on the audio thread.
// ============================================================================
#include <memory>
#include "StringModel.h"
#include "ParamTable.h"
#include "Tuning.h"
#include "../instruments/InstrumentLibrary.h"

namespace resonique
{
struct ModelBuilder
{
    static constexpr double kSteelE = 2.0e11;     // Young's modulus of piano wire (Pa)
    static constexpr double kTableModalWeight = 0.08;

    // The frequency ceiling for partials: below Nyquist with margin, and nothing above 20 kHz.
    static double partialLimit (double fs) { return std::min (0.45 * fs, 20000.0); }

    static void buildSoundboard (const InstrumentModel& m, const ParamValues& v, double fs, SoundboardTable& b)
    {
        Rng rng (m.seed * 2654435761u + 7u);
        const double size = v[soundboardSize];
        const double dampMul = v[soundboardDamping];
        const double dt = 1.0 / fs;
        b.modalMass = m.boardModalMassKg;
        b.bgAdmittance = m.boardAdmittanceBg;
        b.hfLevel = m.boardHfLevel;
        b.sympathetic = m.boardSympathetic;
        b.radiationCorner = m.boardRadiationCornerHz;
        const double fLimit = partialLimit (fs) * 0.9;
        for (int i = 0; i < kSoundboardModes; ++i)
        {
            auto& md = b.modes[(size_t) i];
            const double mi = (double) i;
            double f = (m.boardFirstModeHz + (24.0 * mi + 0.30 * mi * mi) * m.boardModeSpread) / size;
            f *= 1.0 + 0.3 * rng.bipolar() * std::min (1.0, mi / 4.0 + 0.25);   // jitter grows with mode index
            f = clampT (f, 25.0, fLimit);
            const double t = clampT (std::log (f / 50.0) / std::log (4000.0 / 50.0), 0.0, 1.0);
            const double q = m.boardQLow * std::pow (m.boardQHigh / m.boardQLow, t) / dampMul;
            const double sigma = kPi * f / q;
            md.freq = (float) f;
            md.sigma = (float) sigma;
            const double w = kTwoPi * f;
            const double r = std::exp (-sigma * dt);
            md.rc = (float) (r * std::cos (w * dt));
            md.rs = (float) (r * std::sin (w * dt));
            const double mass = m.boardModalMassKg * (1.0 + 0.5 * rng.uniform());
            md.inj = (float) (dt / (mass * w));
            md.velW = (float) w;
            // mode shape along the bridge: smooth for the first modes, increasingly wiggly
            const double k = (0.6 + 1.3 * std::sqrt (mi)) * (0.8 + 0.4 * rng.uniform());
            const double phase = rng.uniform() * kTwoPi;
            const double amp = 0.30 * (0.7 + 0.6 * rng.uniform()) * std::sqrt (m.boardModalMassKg / mass) * m.boardModalGain;
            for (int z = 0; z < kNumZones; ++z)
                md.zoneW[(size_t) z] = (float) (amp * std::cos (k * (z + 0.5) / kNumZones * kPi + phase));
            // radiation: weak below the corner (the board is a poor radiator below its first modes), flat above;
            // fourth order so the lowest fundamentals fall away as they do in the reference recordings
            const double fr = f / m.boardRadiationCornerHz;
            const double rad = fr * fr * fr * fr / (fr * fr * fr * fr + 1.0);
            const double a = (0.55 + 0.45 * rng.uniform()) * rad * (rng.uniform() < 0.5 ? -1.0 : 1.0);
            const double th = 0.2 + 1.17 * rng.uniform();
            md.outL = (float) (a * std::cos (th));
            md.outR = (float) (a * std::sin (th));
        }
    }

    static void buildNote (const InstrumentModel& m, const ParamValues& v, const TuningSettings& tuning,
                           double fs, const SoundboardTable& board, int midiNote, NoteTable& nt)
    {
        Rng rng (m.seed ^ (uint32_t) (midiNote * 0x9e3779b1u));
        const double dt = 1.0 / fs;
        const double note = (double) midiNote;
        nt.midiNote = midiNote;
        nt.zone = zoneForNote (midiNote);
        nt.numStrings = stringsForNote (m, midiNote);
        nt.f0 = noteFrequency (midiNote, tuning);
        nt.pan = clampT ((note - 60.0) / 36.0, -1.0, 1.0);
        nt.gain = (float) dbToGain ((m.levelDb.n > 0 ? m.levelDb.at (note) : 0.0) + (m.levelTrimDb.n > 0 ? m.levelTrimDb.at (note) : 0.0));

        const double L = m.lengthM.atLog (note);
        const double T = m.tensionN.at (note);
        const double d = m.coreDiameterMm.at (note) * 1e-3;
        const double Bphys = kPi * kPi * kPi * kSteelE * d * d * d * d / (64.0 * T * L * L);
        const double B = clampT (Bphys * m.inharmonicityScale.at (note) * v[inharmonicity], 1e-6, 0.3);
        nt.inharmonicity = B;

        // losses
        const double b0 = m.lossBase.at (note);
        const double b1 = m.lossLinear.at (note) * v[stringStiffness];
        const double b2 = m.lossQuad.at (note) * v[stringStiffness];
        const double decayMul = 1.0 / v[stringDecay];

        // hammer
        // The felt exponent is referenced to a typical mf compression (0.5 mm): raising it leaves the
        // force there unchanged and makes the felt harden faster with velocity.
        const double h = v[hammerHardness];
        const double baseP = m.feltP.at (note);
        const double dp = v[feltExponent] + 0.6 * (h - 0.5);
        nt.hammerMass = m.hammerMassG.at (note) * 1e-3 * v[hammerMass];
        nt.feltP = clampT (baseP + dp, 1.5, 4.5);
        nt.feltK = m.feltK.atLog (note) * v[feltStiffness] * std::pow (4.0, 2.0 * (h - 0.5)) * std::pow (0.5e-3, baseP - nt.feltP);
        nt.feltLambda = m.feltHysteresis;
        nt.velPP = m.hammerVelPP;
        nt.velFF = m.hammerVelFF;
        nt.hammerNoiseColour = m.hammerNoiseColour;
        const double strike = clampT (m.strikeRatio.at (note) * v[hammerPosition], 0.03, 0.5);
        // effective felt contact length along the string (m): a compressed felt spreads over several
        // centimetres in the bass; it is what keeps the string's kink from re-exciting the hammer
        const double feltWidth = lerp (0.035, 0.020, clampT ((note - 21.0) / 87.0, 0.0, 1.0));

        // damper
        nt.hasDamper = midiNote <= m.lastDamperNote;
        nt.damperDropMs = m.damperDropMs;
        const double dRate = nt.hasDamper ? m.damperRate.at (note) * v[damperEfficiency] : 0.0;

        // local bridge
        nt.bridgeMass = m.bridgeMassKg;
        nt.bridgeR = m.bridgeResistance / std::max (0.05, m.bridgeAdmittanceScale.at (note));

        // unison detune (cents) per string
        const double dCents = v[unisonDetune] * m.unisonScale + m.unisonExtraCents;
        double offsets[kMaxStrings] { 0, 0, 0 };
        if (nt.numStrings == 2) { offsets[0] = -0.5 * dCents; offsets[1] = 0.5 * dCents; }
        else if (nt.numStrings == 3) { offsets[0] = -dCents; offsets[1] = 0; offsets[2] = dCents; }
        for (int s = 0; s < nt.numStrings; ++s)
            offsets[s] = offsets[s] * (1.0 + m.unisonRandom * rng.bipolar()) + 0.3 * m.unisonRandom * dCents * rng.bipolar();

        const double fLimit = partialLimit (fs);
        for (int s = 0; s < nt.numStrings; ++s)
        {
            auto& st = nt.strings[s];
            st.f0 = nt.f0 * std::pow (2.0, offsets[s] / 1200.0);
            st.length = L;
            st.tension = T;
            st.linearDensity = T / std::pow (2.0 * L * st.f0, 2.0);
            st.staticK = T / L;
            const double mu = st.linearDensity;
            const double Mn = mu * L / 2.0;
            const double xh = strike * L;
            int n = 0;
            for (; n < kMaxPartials; ++n)
            {
                const double k = n + 1.0;
                double f = k * st.f0 * std::sqrt ((1.0 + B * k * k) / (1.0 + B));
                if (m.partialJitterCents > 0 && n > 0)
                    f *= std::pow (2.0, m.partialJitterCents * rng.bipolar() * std::min (1.0, k / 6.0) / 1200.0);
                if (f >= fLimit) break;
                const double fk = f / 1000.0;
                double sigma = (b0 + b1 * fk + b2 * fk * fk) * decayMul;
                // bridge loss from the board's admittance. The live soundboard feedback already drains
                // energy into the modes, so only part of the modal admittance goes into the table.
                sigma += T * board.admittanceAt (f, nt.zone, kTableModalWeight) / L * m.boardLossScale * decayMul;
                if (m.partialLossJitter > 0 && n > 0) sigma *= 1.0 + m.partialLossJitter * rng.uniform() * std::min (1.0, k / 4.0);
                sigma = clampT (sigma, 0.02, 2000.0);
                const double w = kTwoPi * f;
                const double r = std::exp (-sigma * dt);
                st.freq[n] = (float) f;
                st.sigma[n] = (float) sigma;
                st.rc[n] = (float) (r * std::cos (w * dt));
                st.rs[n] = (float) (r * std::sin (w * dt));
                const double widthRoll = std::exp (-0.5 * std::pow (k * kPi * feltWidth / (2.0 * L), 2.0));
                const double shape = std::sin (k * kPi * xh / L) * widthRoll;
                st.hammerShape[n] = (float) shape;
                st.hammerInj[n] = (float) (shape * dt / (Mn * w));
                const double sign = (n % 2 == 0) ? -1.0 : 1.0;      // (-1)^k with k = n + 1
                st.bridgeW[n] = (float) (T * k * kPi / L * sign);
                st.damperSigma[n] = (float) (dRate * (0.4 + 0.6 * std::min (1.0, f / 600.0)));
                if (n < kCouplePartials)
                    st.coupleInj[n] = (float) ((2.0 / (k * kPi)) * sign * dt / w);
            }
            st.numPartials = std::max (n, 1);
            for (int z = n; z < kMaxPartials; ++z)
            {
                st.freq[z] = 0; st.sigma[z] = 0; st.rc[z] = 0; st.rs[z] = 0; st.hammerShape[z] = 0; st.hammerInj[z] = 0;
                st.bridgeW[z] = 0; st.damperSigma[z] = 0;
            }
            for (int z = n; z < kCouplePartials; ++z) st.coupleInj[z] = 0;
        }
    }

    // The tuning parameters (temperament, root, concert pitch, stretch) come from the parameter values;
    // the per-note detune and the Scala scale from `tuningState`.
    static TuningSettings tuningFor (const ParamValues& v, const TuningSettings& tuningState)
    {
        TuningSettings t = tuningState;
        t.temperament = (Temperament) clampT ((int) std::lround (v[temperament]), 0, (int) Temperament::Custom);
        t.root = clampT ((int) std::lround (v[temperamentRoot]), 0, 11);
        t.concertPitch = clampT ((double) v[concertPitch], 380.0, 500.0);
        t.stretch = clampT ((double) v[stretch], 0.0, 1.0);
        return t;
    }

    static std::unique_ptr<InstrumentTables> build (const ParamValues& v, const TuningSettings& tuningState, double fs, uint32_t buildId = 0)
    {
        const TuningSettings tuning = tuningFor (v, tuningState);
        auto t = std::make_unique<InstrumentTables>();
        t->sampleRate = fs;
        t->instrumentIndex = clampT ((int) std::lround (v[instrument]), 0, kNumInstruments - 1);
        t->model = instrumentModel (t->instrumentIndex);
        t->outputGain = t->model.outputGain;
        t->buildId = buildId;
        buildSoundboard (t->model, v, fs, t->board);
        for (int i = 0; i < kNumNotes; ++i)
            buildNote (t->model, v, tuning, fs, t->board, kFirstNote + i, t->notes[(size_t) i]);
        return t;
    }
};
} // namespace resonique
