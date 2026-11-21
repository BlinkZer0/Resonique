// Acoustic validation on rendered audio: tuning, inharmonic partials, decay,
// velocity response, unison beating, polyphony, sympathetic resonance, pedals,
// sample-rate independence, stability, MIDI edge cases.
#include "TestRender.h"

using namespace resonique;
using namespace resonique::test;

namespace
{
void tuningAndPartials()
{
    std::printf ("     fundamental tuning (coupled / uncoupled):");
    for (int n : { 21, 33, 45, 57, 69, 81, 93, 105 })
    {
        // single strings (unison detune 0) so the peak is one partial; long windows for the bass
        Patch p; p.set ("unisonDetune", 0.0f);
        const double f0 = noteFrequency (n, p.tuning);
        const double t0 = n > 90 ? 0.05 : 0.3, t1 = n > 90 ? 0.35 : (n < 40 ? 2.8 : 1.3);
        auto r = render (p, t1 + 0.1, { on (0.0, n, 80) });
        auto m = r.mono();
        const double f = peakFreqSec (m, t0, t1, f0 * 0.94, f0 * 1.06);
        const double cents = 1200.0 * std::log2 (f / f0);
        Patch q; q.set ("unisonDetune", 0.0f).set ("stringResonance", 0.0f).set ("bridgeCoupling", 0.0f);
        auto mq = render (q, t1 + 0.1, { on (0.0, n, 80) }).mono();
        const double centsQ = 1200.0 * std::log2 (peakFreqSec (mq, t0, t1, f0 * 0.94, f0 * 1.06) / f0);
        std::printf (" %d:%+.1f/%+.1f", n, cents, centsQ);
        CHECKF (std::abs (cents) < 3.0, "note %d measured %.3f Hz expected %.3f (%.1f ct)", n, f, f0, cents);
        CHECK (allFinite (m));
    }
    std::printf (" ct\n");

    // inharmonic partials of C4: measured peaks follow f_n = n f0 sqrt((1+Bn^2)/(1+B))
    Patch p;
    p.set ("unisonDetune", 0.0f);
    auto t = ModelBuilder::build (p.v, p.tuning, kSr, 1);
    const auto& nt = t->notes[60 - kFirstNote];
    auto r = render (p, 1.0, { on (0.0, 60, 90) });
    auto m = r.mono();
    for (int k = 1; k < 8; ++k)
    {
        const double expect = nt.strings[0].freq[k];
        const double f = peakFreqSec (m, 0.1, 0.6, expect * 0.985, expect * 1.015);
        CHECKF (std::abs (f / expect - 1.0) < 0.004, "partial %d at %.2f expected %.2f", k + 1, f, expect);
    }
    // with more inharmonicity the upper partials move up
    Patch q; q.set ("unisonDetune", 0.0f).set ("inharmonicity", 3.0f);
    auto rq = render (q, 1.0, { on (0.0, 60, 90) });
    auto mq = rq.mono();
    const double p6a = peakFreqSec (m, 0.1, 0.6, nt.f0 * 5.9, nt.f0 * 6.3);
    const double p6b = peakFreqSec (mq, 0.1, 0.6, nt.f0 * 5.9, nt.f0 * 6.4);
    CHECKF (p6b > p6a + 1.0, "inharmonicity x3 should raise partial 6: %.2f vs %.2f", p6b, p6a);
}

void decayTests()
{
    std::printf ("     T60 of the fundamental (s):");
    struct R { int note; double lo, hi; } rows[] = { { 36, 3, 60 }, { 60, 2.5, 40 }, { 84, 0.6, 10 }, { 100, 0.2, 5 } };
    for (const auto& row : rows)
    {
        Patch p;
        const double f0 = noteFrequency (row.note, p.tuning);
        const double len = row.note >= 84 ? 2.5 : 5.0;
        auto r = render (p, len, { on (0.0, row.note, 90) });
        auto m = r.mono();
        const double t60 = t60At (m, f0, row.note >= 84 ? 0.1 : 0.4, len, row.note >= 84 ? 0.12 : 0.4);
        std::printf ("  %d: %.1f", row.note, t60);
        CHECKF (t60 > row.lo && t60 < row.hi, "note %d T60 %.2f s outside [%.1f, %.1f]", row.note, t60, row.lo, row.hi);
        // higher partials die faster
        const double t60p5 = t60At (m, f0 * 5.0 * std::sqrt (1.0 + 25.0 * 3e-4) / std::sqrt (1.0 + 3e-4), 0.2, std::min (len, 2.0), 0.15);
        if (row.note <= 60) CHECKF (t60p5 < t60, "note %d partial 5 (%.2f) should decay faster than f1 (%.2f)", row.note, t60p5, t60);
    }
    std::printf ("\n");

    // how much of the decay the local bridge takes (diagnostic)
    {
        std::printf ("     C4 T60 vs bridge coupling:");
        for (float c : { 0.0f, 0.5f, 1.0f })
        {
            Patch p; p.set ("bridgeCoupling", c);
            auto m = render (p, 4.0, { on (0.0, 60, 90) }).mono();
            std::printf ("  %.1f: %.1f s", c, t60At (m, noteFrequency (60, p.tuning), 0.3, 4.0));
        }
        std::printf ("\n");
    }

    // the String Decay parameter scales it
    Patch a; a.set ("stringDecay", 0.5f);
    Patch b; b.set ("stringDecay", 2.0f);
    const double f0 = noteFrequency (60, a.tuning);
    auto ra = render (a, 3.0, { on (0.0, 60, 90) }), rb = render (b, 3.0, { on (0.0, 60, 90) });
    auto ma = ra.mono(), mb = rb.mono();
    const double ta = t60At (ma, f0, 0.3, 3.0), tb = t60At (mb, f0, 0.3, 3.0);
    CHECKF (tb > ta * 1.8, "String Decay x0.5 -> %.2f s, x2 -> %.2f s", ta, tb);

    // damped release: after note-off the sound dies quickly
    Patch p;
    auto r = render (p, 2.0, { on (0.0, 60, 100), off (0.6, 60) });
    auto m = r.mono();
    const double held = rmsSec (m, 0.3, 0.55), after = rmsSec (m, 1.2, 1.6);
    CHECKF (dB (after) < dB (held) - 40, "damper: %.1f dB held, %.1f dB 0.6 s after release", dB (held), dB (after));
    // no damper above E6: a released C7 keeps more of its fundamental than a released (damped) E6 does
    auto ratioAfterRelease = [&] (int n)
    {
        auto mm = render (p, 1.2, { on (0.0, n, 100), off (0.3, n) }).mono();
        const double f0 = noteFrequency (n, p.tuning);
        return magSec (mm, 0.55, 0.7, f0) / std::max (1e-12, magSec (mm, 0.1, 0.25, f0));
    };
    const double undamped = ratioAfterRelease (96), damped = ratioAfterRelease (86);
    CHECKF (undamped > damped * 5, "C7 (no damper) keeps %.3f of its level, E6 (damped) %.3f", undamped, damped);
}

void velocityTests()
{
    std::printf ("     velocity -> level / centroid:");
    double lastRms = -1, lastCent = -1, rms20 = 0, rms127 = 0;
    for (int vel : { 20, 50, 80, 110, 127 })
    {
        Patch p;
        auto r = render (p, 0.6, { on (0.0, 60, vel) });
        auto m = r.mono();
        const double level = rmsSec (m, 0.02, 0.3);
        const double cent = centroid (m, (size_t) (0.02 * kSr), (size_t) (0.3 * kSr));
        std::printf ("  %d: %.1fdB/%.0fHz", vel, dB (level), cent);
        CHECKF (level > lastRms, "level not rising at velocity %d", vel);
        CHECKF (cent > lastCent * 1.01, "spectral centroid not rising at velocity %d (%.0f vs %.0f)", vel, cent, lastCent);
        lastRms = level; lastCent = cent;
        if (vel == 20) rms20 = level;
        if (vel == 127) rms127 = level;
    }
    std::printf ("\n");
    const double range = dB (rms127) - dB (rms20);
    CHECKF (range > 15 && range < 50, "dynamic range 20..127 = %.1f dB", range);
    CHECKF (dB (rms127) > -24 && dB (rms127) < -3, "ff level %.1f dBFS", dB (rms127));

    // the Dynamics and Velocity Curve parameters do something sensible
    Patch soft; soft.set ("velCurve", -1.0f);
    Patch hard; hard.set ("velCurve", 1.0f);
    const double ls = rmsSec (render (soft, 0.4, { on (0.0, 60, 64) }).mono(), 0.02, 0.3);
    const double lh = rmsSec (render (hard, 0.4, { on (0.0, 60, 64) }).mono(), 0.02, 0.3);
    CHECKF (lh > ls * 1.2, "velocity curve +1 should be louder at 64 (%.1f vs %.1f dB)", dB (lh), dB (ls));
    // hammer hardness brightens
    Patch dull; dull.set ("hammerHardness", 0.1f);
    Patch bright; bright.set ("hammerHardness", 0.9f);
    const double cd = centroid (render (dull, 0.4, { on (0.0, 60, 100) }).mono(), 1000, (size_t) (0.3 * kSr));
    const double cb = centroid (render (bright, 0.4, { on (0.0, 60, 100) }).mono(), 1000, (size_t) (0.3 * kSr));
    CHECKF (cb > cd * 1.15, "hammer hardness: centroid %.0f (soft) vs %.0f (hard)", cd, cb);
}

void unisonTests()
{
    // the fundamental of a three-string note beats when the strings are detuned
    auto modulation = [] (float detune)
    {
        Patch p; p.set ("unisonDetune", detune).set ("bridgeCoupling", 0.5f);
        auto r = render (p, 4.0, { on (0.0, 64, 90) });
        auto m = r.mono();
        const double f0 = noteFrequency (64, p.tuning);
        double mx = -1e9, mn = 1e9;
        std::vector<double> env;
        for (double t = 0.5; t < 3.9; t += 0.1) { const double v = dB (magSec (m, t, t + 0.1, f0)); env.push_back (v); }
        // remove the overall decay trend before measuring the ripple
        double sumT = 0, sumV = 0; const double n = (double) env.size();
        for (size_t i = 0; i < env.size(); ++i) { sumT += (double) i; sumV += env[i]; }
        const double mt = sumT / n, mv = sumV / n;
        double num = 0, den = 0;
        for (size_t i = 0; i < env.size(); ++i) { num += ((double) i - mt) * (env[i] - mv); den += ((double) i - mt) * ((double) i - mt); }
        const double slope = num / den;
        for (size_t i = 0; i < env.size(); ++i) { const double d = env[i] - (mv + slope * ((double) i - mt)); mx = std::max (mx, d); mn = std::min (mn, d); }
        return mx - mn;
    };
    const double m0 = modulation (0.0f), m3 = modulation (3.0f);
    std::printf ("     unison ripple: 0 ct %.2f dB, 3 ct %.2f dB\n", m0, m3);
    CHECKF (m3 > m0 + 1.5, "detuned unison should beat (ripple %.2f vs %.2f dB)", m3, m0);
}

void polyphonyTests()
{
    // every key at once with the pedal down: finite, bounded, within the voice limit
    Patch p; p.set ("polyphony", 128);
    std::vector<Event> ev { cc (0.0, 64, 127) };
    for (int n = kFirstNote; n <= kLastNote; ++n) ev.push_back (on (0.001 * (n - kFirstNote), n, 70 + (n % 40)));
    for (int n = kFirstNote; n <= kLastNote; ++n) ev.push_back (off (0.5 + 0.001 * (n - kFirstNote), n));
    int maxVoices = 0;
    auto r = render (p, 3.0, ev, kSr, [&maxVoices] (PianoEngine& e, int) { maxVoices = std::max (maxVoices, e.activeVoices()); });
    CHECK (allFinite (r.L) && allFinite (r.R));
    CHECKF (r.peak <= 1.0, "peak %.3f", r.peak);
    CHECK (maxVoices <= 128 && maxVoices >= 88);
    std::printf ("     88-note chord: peak %.2f dBFS, %d voices\n", dB (r.peak), maxVoices);

    // voice stealing keeps the count at the polyphony limit
    Patch q; q.set ("polyphony", 16);
    std::vector<Event> ev2;
    for (int i = 0; i < 40; ++i) ev2.push_back (on (0.01 * i, 40 + i, 90));
    int mv = 0;
    auto r2 = render (q, 1.0, ev2, kSr, [&mv] (PianoEngine& e, int) { mv = std::max (mv, e.activeVoices()); });
    CHECKF (mv <= 16, "polyphony 16 but %d voices", mv);
    CHECK (allFinite (r2.L) && r2.peak <= 1.0);

    // restriking a ringing key keeps one voice and adds energy
    Patch s;
    int voicesAfter = 0;
    auto r3 = render (s, 1.0, { on (0.0, 60, 60), on (0.3, 60, 110) }, kSr,
                      [&voicesAfter] (PianoEngine& e, int pos) { if (pos > (int) (0.35 * kSr)) voicesAfter = std::max (voicesAfter, e.activeVoices()); });
    auto m3 = r3.mono();
    CHECK (voicesAfter == 1);
    CHECKF (rmsSec (m3, 0.31, 0.5) > rmsSec (m3, 0.1, 0.29), "restrike should add energy");

    // all voices end after release: nothing lingers
    Patch e;
    int endVoices = -1;
    render (e, 9.0, { on (0.0, 60, 100), on (0.0, 64, 100), on (0.0, 67, 100), off (0.5, 60), off (0.5, 64), off (0.5, 67) }, kSr,
            [&endVoices] (PianoEngine& eng, int pos) { if (pos >= (int) (8.9 * kSr)) endVoices = eng.activeVoices(); });
    CHECKF (endVoices == 0, "%d voices still active 8.5 s after release", endVoices);
}

void resonanceAndPedalTests()
{
    // sympathetic resonance: with the pedal down, striking C4 makes the undamped C3 string ring
    // (its partials line up) much more than C#3 (they don't)
    {
        Patch p; p.set ("stringResonance", 0.8f);
        Setup s (p);
        auto& e = *s.engine;
        e.controller (64, 127);
        e.noteOn (60, 110);
        float L[kBlock], R[kBlock];
        for (int b = 0; b < (int) (1.0 * kSr / kBlock); ++b) e.render (L, R, kBlock);
        const auto& bank = e.sympatheticBank();
        const float c3 = bank.energyOfNote (48 - kFirstNote), cs3 = bank.energyOfNote (49 - kFirstNote), c2 = bank.energyOfNote (36 - kFirstNote);
        std::printf ("     sympathetic energy after C4: C3 %.2e, C#3 %.2e, C2 %.2e\n", c3, cs3, c2);
        CHECK (c3 > 0 && std::isfinite (c3));
        CHECKF (c3 > cs3 * 2.5, "harmonically related string should resonate more (C3 %.2e vs C#3 %.2e)", c3, cs3);
        // pedal up: the bank stays silent
        Setup s2 (p);
        s2.engine->noteOn (60, 110);
        for (int b = 0; b < (int) (1.0 * kSr / kBlock); ++b) s2.engine->render (L, R, kBlock);
        const float c3up = s2.engine->sympatheticBank().energyOfNote (48 - kFirstNote);
        CHECKF (c3up < c3 * 0.05, "with the pedal up C3 should not ring (%.2e vs %.2e)", c3up, c3);
        // the string-resonance parameter at 0 removes it
        Patch z; z.set ("stringResonance", 0.0f);
        Setup s3 (z);
        s3.engine->controller (64, 127);
        s3.engine->noteOn (60, 110);
        for (int b = 0; b < (int) (1.0 * kSr / kBlock); ++b) s3.engine->render (L, R, kBlock);
        CHECK (s3.engine->sympatheticBank().energyOfNote (48 - kFirstNote) < c3 * 0.05);
    }

    // sustain and half pedal: ordering of what is left 1 s after release
    {
        auto left = [] (int pedal)
        {
            Patch p;
            auto r = render (p, 2.2, { cc (0.0, 64, pedal), on (0.0, 55, 100), off (0.5, 55) });
            return rmsSec (r.mono(), 1.6, 2.1);
        };
        const double up = left (0), half = left (64), down = left (127);
        std::printf ("     1.1 s after release: pedal up %.1f dB, half %.1f dB, down %.1f dB\n", dB (up), dB (half), dB (down));
        CHECKF (down > half * 1.5 && half > up * 1.5, "half-pedal ordering broken");
        CHECKF (dB (down) - dB (up) > 20, "sustain pedal should keep the note ringing");
    }

    // re-pedalling: pressing the pedal after release stops the damping (the note had not died yet)
    {
        Patch p;
        auto r = render (p, 2.5, { on (0.0, 50, 110), off (0.3, 50), cc (0.38, 64, 127) });
        auto m = r.mono();
        auto r2 = render (p, 2.5, { on (0.0, 50, 110), off (0.3, 50) });
        auto m2 = r2.mono();
        CHECKF (rmsSec (m, 1.5, 2.0) > rmsSec (m2, 1.5, 2.0) * 3, "repedalling should catch the note");
    }

    // sostenuto: holds the notes down when it was pressed, not the ones played after
    {
        Patch p;
        auto r = render (p, 2.5, { on (0.0, 48, 100), cc (0.2, 66, 127), off (0.3, 48), on (0.4, 62, 100), off (0.6, 62) });
        auto m = r.mono();
        const double c3 = magSec (m, 1.5, 2.0, noteFrequency (48, p.tuning));
        const double d4 = magSec (m, 1.5, 2.0, noteFrequency (62, p.tuning));
        std::printf ("     sostenuto: held C3 %.1f dB, later D4 %.1f dB\n", dB (c3), dB (d4));
        CHECKF (c3 > d4 * 10, "sostenuto should sustain C3 but not D4");
    }

    // soft pedal: quieter and duller
    {
        Patch p;
        auto a = render (p, 0.5, { on (0.0, 60, 100) }).mono();
        auto b = render (p, 0.5, { cc (0.0, 67, 127), on (0.0, 60, 100) }).mono();
        CHECKF (rmsSec (b, 0.02, 0.3) < rmsSec (a, 0.02, 0.3) * 0.9, "soft pedal should reduce the level");
        CHECK (centroid (b, 1000, (size_t) (0.3 * kSr)) < centroid (a, 1000, (size_t) (0.3 * kSr)));
    }

    // the soundboard shapes the tone: changing its resonance changes the spectrum
    {
        Patch a; a.set ("soundboardResonance", 0.0f);
        Patch b; b.set ("soundboardResonance", 1.0f);
        auto ma = render (a, 0.6, { on (0.0, 48, 100) }).mono(), mb = render (b, 0.6, { on (0.0, 48, 100) }).mono();
        double diff = 0, tot = 0;
        for (double f = 60; f < 3000; f *= 1.03)
        {
            const double x = dB (magSec (ma, 0.05, 0.5, f)), y = dB (magSec (mb, 0.05, 0.5, f));
            diff += std::abs (x - y); tot += 1;
        }
        std::printf ("     soundboard resonance 0 vs 1: mean spectral difference %.1f dB\n", diff / tot);
        CHECK (diff / tot > 1.0);
    }
}

void rateIndependenceAndStability()
{
    std::printf ("     sample rates:");
    double refF = 0, refLevel = 0;
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        Patch p;
        auto r = render (p, 1.0, { on (0.0, 62, 90, sr) }, sr);
        auto m = r.mono();
        const double f0 = noteFrequency (62, p.tuning);
        const double f = peakFreqSec (m, 0.2, 0.8, f0 * 0.97, f0 * 1.03, sr);
        const double lvl = dB (rmsSec (m, 0.05, 0.5, sr));
        std::printf ("  %.1fk: %.2f Hz %.1f dB %.0f Hz", sr / 1000, f, lvl, centroid (m, (size_t) (0.02 * sr), (size_t) (0.3 * sr), sr));
        CHECK (allFinite (m) && r.peak <= 1.0);
        if (refF == 0) { refF = f; refLevel = lvl; }
        else
        {
            CHECKF (std::abs (f / refF - 1.0) < 0.001, "pitch differs at %.0f Hz", sr);
            CHECKF (std::abs (lvl - refLevel) < 2.0, "level differs at %.0f Hz: %.1f vs %.1f dB", sr, lvl, refLevel);
        }
    }
    std::printf ("\n");

    // stress: everything turned up, every key, pedal, 12 s: finite, bounded, decays after release
    Patch p;
    p.set ("stringResonance", 1.0f).set ("bridgeCoupling", 1.0f).set ("soundboardResonance", 1.0f).set ("polyphony", 128)
     .set ("stringDecay", 4.0f).set ("noiseMaster", 1.0f).set ("reverbMix", 0.5f).set ("micPerspective", 3);
    std::vector<Event> ev { cc (0.0, 64, 127) };
    for (int n = kFirstNote; n <= kLastNote; ++n) ev.push_back (on (0.002 * (n - kFirstNote), n, 127));
    for (int n = kFirstNote; n <= kLastNote; ++n) ev.push_back (off (1.0, n));
    ev.push_back (cc (2.0, 64, 0));
    auto r = render (p, 12.0, ev);
    auto m = r.mono();
    CHECK (allFinite (r.L) && allFinite (r.R));
    CHECKF (r.peak <= 1.0, "stress peak %.3f", r.peak);
    const double loud = rmsSec (m, 0.5, 1.0), tail = rmsSec (m, 10.0, 12.0);
    std::printf ("     stress: %.1f dB while held, then %.1f / %.1f / %.1f / %.1f dB at 3 / 5 / 8 / 11 s\n", dB (loud),
                 dB (rmsSec (m, 3.0, 3.5)), dB (rmsSec (m, 5.0, 5.5)), dB (rmsSec (m, 8.0, 8.5)), dB (rmsSec (m, 11.0, 11.5)));
    CHECKF (dB (tail) < dB (loud) - 30, "coupled system should settle after the pedal comes up");

    // the soundboard <-> string feedback must stay dissipative at full strength: one note with the
    // pedal down decays monotonically after the hammer has left
    {
        Patch c; c.set ("stringResonance", 1.0f).set ("bridgeCoupling", 1.0f).set ("soundboardResonance", 1.0f);
        auto mc = render (c, 8.0, { cc (0.0, 64, 127), on (0.0, 52, 110), off (0.3, 52) }).mono();
        const double e1 = dB (rmsSec (mc, 0.5, 1.5)), e2 = dB (rmsSec (mc, 3.0, 4.0)), e3 = dB (rmsSec (mc, 7.0, 8.0));
        std::printf ("     full coupling, E3 with pedal: %.1f / %.1f / %.1f dB at 1 / 3.5 / 7.5 s\n", e1, e2, e3);
        CHECKF (e2 < e1 - 3 && e3 < e2 - 3, "coupled note should keep decaying");
        CHECK (allFinite (mc));
    }
}

void midiEdgeCases()
{
    Patch p;
    // note-off without note-on, out-of-range notes, velocity 0 as note-off
    auto r = render (p, 0.5, { off (0.0, 60), on (0.0, 5, 100), on (0.0, 120, 100), on (0.01, 60, 100), { (int) (0.1 * kSr), [] (PianoEngine& e) { e.noteOn (60, 0); } } });
    auto m = r.mono();
    CHECK (allFinite (m));
    CHECKF (rmsSec (m, 0.4, 0.5) < rmsSec (m, 0.02, 0.09) * 0.1, "velocity 0 should release the note");
    // all notes off (CC123) releases; all sound off (CC120) is immediate silence
    auto r2 = render (p, 1.0, { on (0.0, 60, 100), on (0.0, 64, 100), cc (0.3, 123, 0) }).mono();
    CHECKF (rmsSec (r2, 0.8, 1.0) < rmsSec (r2, 0.1, 0.3) * 0.1, "CC123 should release everything");
    int voicesAfterPanic = -1;
    auto r3 = render (p, 1.0, { on (0.0, 60, 100), on (0.0, 64, 100), cc (0.3, 120, 0) }, kSr,
                      [&voicesAfterPanic] (PianoEngine& e, int pos) { if (pos > (int) (0.35 * kSr)) voicesAfterPanic = e.activeVoices(); }).mono();
    CHECK (voicesAfterPanic == 0);
    CHECKF (rmsSec (r3, 0.35, 0.5) < 1e-4, "CC120 should silence immediately (%.1f dB)", dB (rmsSec (r3, 0.35, 0.5)));
    CHECKF (maxStep (r3, (size_t) (0.29 * kSr), (size_t) (0.32 * kSr)) < 0.6, "panic click");
    // pitch bend: +2 semitones moves the pitch by the right ratio
    auto r4 = render (p, 1.2, { on (0.0, 60, 90), bend (0.3, 2.0) }).mono();
    const double f0 = noteFrequency (60, p.tuning);
    const double fb = peakFreqSec (r4, 0.6, 1.1, f0 * 1.05, f0 * 1.2);
    const double ratio = fb / f0, expect = std::pow (2.0, 2.0 / 12.0);
    CHECKF (std::abs (ratio / expect - 1.0) < 0.005, "bend ratio %.4f expected %.4f", ratio, expect);
    // bend back to 0 restores the pitch (single string so the peak is clean)
    Patch u; u.set ("unisonDetune", 0.0f);
    auto r5 = render (u, 1.6, { on (0.0, 60, 90), bend (0.2, 2.0), bend (0.6, 0.0) }).mono();
    const double fr = peakFreqSec (r5, 1.0, 1.5, f0 * 0.97, f0 * 1.03);
    CHECKF (std::abs (fr / f0 - 1.0) < 0.001, "pitch after bend returns: %.3f vs %.3f", fr, f0);
    // mechanical noise is there when enabled: the difference between the two renders is not silence
    Patch n; n.set ("noiseMaster", 1.0f).set ("noiseKeyRelease", 1.0f);
    auto rn = render (n, 1.0, { on (0.0, 72, 40), off (0.5, 72) }).mono();
    auto rq = render (p, 1.0, { on (0.0, 72, 40), off (0.5, 72) }).mono();
    std::vector<float> diff (rn.size());
    for (size_t i = 0; i < diff.size(); ++i) diff[i] = rn[i] - rq[i];
    CHECKF (rmsSec (diff, 0.5, 0.56) > rmsSec (rq, 0.5, 0.56) * 0.05, "key-release noise (%.1f dB vs note %.1f dB)", dB (rmsSec (diff, 0.5, 0.56)), dB (rmsSec (rq, 0.5, 0.56)));
    CHECKF (rmsSec (diff, 0.0, 0.03) > 1e-5, "hammer noise");
    // notes started mid-block and across block boundaries make sound without clicks at the join
    auto r6 = render (p, 0.4, { { 77, [] (PianoEngine& e) { e.noteOn (60, 100); } } }).mono();
    CHECK (rmsSec (r6, 0.05, 0.3) > 1e-4);
}
} // namespace

int runAcousticTests()
{
    tuningAndPartials();
    decayTests();
    velocityTests();
    unisonTests();
    polyphonyTests();
    resonanceAndPedalTests();
    rateIndependenceAndStability();
    midiEdgeCases();
    std::printf ("   acoustic: %d failure(s) so far\n", failures);
    return failures;
}
