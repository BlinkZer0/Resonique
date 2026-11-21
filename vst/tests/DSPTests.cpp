// DSP unit checks: helpers, tuning, the parameter table, the model builder,
// the modal resonator, the hammer, pedals, dampers, noises, reverb, renderer.
#include "TestRender.h"
#include "dsp/Reverb.h"
#include "dsp/SpatialRenderer.h"
#include "dsp/MechanicalNoise.h"
#include "dsp/PedalModel.h"

using namespace resonique;
using namespace resonique::test;

namespace
{
void helperTests()
{
    NoteCurve c { { 21, 1.0 }, { 60, 2.0 }, { 108, 4.0 } };
    CHECK (std::abs (c.at (21) - 1.0) < 1e-12);
    CHECK (std::abs (c.at (40.5) - 1.5) < 1e-12);
    CHECK (std::abs (c.at (108) - 4.0) < 1e-12);
    CHECK (std::abs (c.at (200) - 4.0) < 1e-12);
    CHECK (std::abs (c.atLog (40.5) - std::sqrt (2.0)) < 1e-9);

    Rng a (5), b (5);
    for (int i = 0; i < 100; ++i) CHECK (a.next() == b.next());
    double mean = 0; Rng r (9);
    for (int i = 0; i < 10000; ++i) mean += r.bipolar();
    CHECK (std::abs (mean / 10000.0) < 0.03);

    CHECK (softClip (0.5f) == 0.5f);
    CHECK (softClip (4.0f) < 1.0f && softClip (4.0f) > 0.9f);
    CHECK (softClip (-4.0f) > -1.0f);

    Biquad lp; lp.lowPass (48000, 1000);
    double e1k = 0, e10k = 0;
    for (int i = 0; i < 4800; ++i) { const float y = lp.process ((float) std::sin (kTwoPi * 100 * i / 48000.0)); if (i > 2400) e1k += y * y; }
    lp.reset();
    for (int i = 0; i < 4800; ++i) { const float y = lp.process ((float) std::sin (kTwoPi * 10000 * i / 48000.0)); if (i > 2400) e10k += y * y; }
    CHECK (e10k < e1k * 0.01);

    Smoother s; s.setTime (0.05, 48000); s.reset (0); s.target = 1;
    double v = 0; for (int i = 0; i < 3000; ++i) v = s.advance();
    CHECK (v > 0.99);
}

void tuningTests()
{
    TuningSettings t;
    t.stretch = 0;
    CHECK (std::abs (noteFrequency (69, t) - 440.0) < 1e-9);
    CHECK (std::abs (noteFrequency (60, t) - 261.6256) < 1e-3);
    CHECK (std::abs (noteFrequency (21, t) - 27.5) < 1e-6);
    t.concertPitch = 442;
    CHECK (std::abs (noteFrequency (69, t) - 442.0) < 1e-9);
    t.concertPitch = 440;
    t.temperament = Temperament::Werckmeister;
    CHECK (std::abs (noteFrequency (60, t) - 261.6256) < 1e-3);       // root C unchanged
    CHECK (noteFrequency (64, t) < 329.6276);                           // E flatter than ET
    t.root = 9;                                                          // A
    CHECK (std::abs (noteFrequency (69, t) - 440.0) < 1e-9);
    t.temperament = Temperament::Equal; t.root = 0;
    t.stretch = 1;
    CHECK (noteFrequency (21, t) < 27.5);
    CHECK (noteFrequency (108, t) > 4186.0);
    CHECK (std::abs (noteFrequency (69, t) - 440.0) < 1e-9);
    t.stretch = 0;
    t.noteDetune[60 - kFirstNote] = 50;
    CHECK (std::abs (noteFrequency (60, t) / 261.6256 - std::pow (2.0, 50.0 / 1200.0)) < 1e-6);
    t.noteDetune[60 - kFirstNote] = 0;

    ScalaScale s; std::string err;
    CHECK (ScalaScale::parse ("! test.scl\nTwelve-tone equal\n 12\n 100.0\n 200.0\n 300.0\n 400.0\n 500.0\n 600.0\n 700.0\n 800.0\n 900.0\n 1000.0\n 1100.0\n 2/1\n", s, err));
    CHECK (s.valid() && s.cents.size() == 12 && std::abs (s.cents[11] - 1200.0) < 1e-9);
    t.temperament = Temperament::Custom; t.scala = s;
    CHECK (std::abs (noteFrequency (69, t) - 440.0) < 1e-6);
    CHECK (std::abs (noteFrequency (48, t) - 130.8128) < 1e-3);
    CHECK (ScalaScale::parse ("Just major\n7\n9/8\n5/4\n4/3\n3/2\n5/3\n15/8\n2/1\n", s, err));
    CHECK (s.cents.size() == 7 && std::abs (s.cents[0] - 203.91) < 0.01);
    t.scala = s;
    CHECK (std::abs (noteFrequency (67, t) / noteFrequency (60, t) - 2.0) < 1e-9);   // 7 steps = period
    CHECK (std::abs (noteFrequency (61, t) / noteFrequency (60, t) - 9.0 / 8.0) < 1e-9);
    CHECK (! ScalaScale::parse ("garbage\n", s, err));
    CHECK (! ScalaScale::parse ("desc\n3\n100.0\nabc\n300.0\n", s, err));
    CHECK (ScalaScale::parse ("empty\n0\n", s, err) && s.cents.size() == 1);
}

void paramTableTests()
{
    const auto& t = paramTable();
    CHECK ((int) t.size() == kNumParams);
    for (size_t i = 0; i < t.size(); ++i)
    {
        const auto& d = t[i];
        CHECKF (d.def >= d.min && d.def <= d.max, "%s", d.id);
        CHECKF (paramIndexForId (d.id) == (int) i, "%s", d.id);
        for (size_t j = i + 1; j < t.size(); ++j) CHECKF (std::string (d.id) != t[j].id, "duplicate %s", d.id);
        const double mid = d.logSkew ? std::sqrt (d.min * d.max) : (d.min + d.max) / 2;
        CHECKF (std::abs (paramFromNorm (d, paramToNorm (d, mid)) - mid) < 1e-6 * std::max (1.0, std::abs (mid)), "%s", d.id);
        if (d.kind == Kind::Choice) CHECKF ((int) d.choices.size() == (int) d.max + 1, "%s", d.id);
    }
    CHECK (paramDef (master).noPreset);
    CHECK (paramIndexForId ("nope") == -1);
}

void modelBuilderTests()
{
    Patch p;
    auto t = ModelBuilder::build (p.v, p.tuning, 48000.0, 1);
    CHECK (t->instrumentIndex == 0);
    std::printf ("     note  strings partials   B        T60(f1)  K        p     contact-ish\n");
    double lastB = 0;
    for (int n : { 21, 28, 29, 36, 47, 48, 60, 72, 84, 96, 108 })
    {
        const auto& nt = t->notes[(size_t) (n - kFirstNote)];
        const auto& st = nt.strings[0];
        CHECKF (std::abs (st.f0 / noteFrequency (n, p.tuning) - 1.0) < 0.01, "note %d f0", n);
        CHECKF (std::abs (st.freq[0] / st.f0 - 1.0) < 1e-5, "note %d partial 1 = f0 (%g vs %g)", n, st.freq[0], st.f0);
        CHECK (st.numPartials >= 4 && st.numPartials <= kMaxPartials);
        CHECK (st.freq[st.numPartials - 1] < 0.45 * 48000.0);
        for (int k = 1; k < st.numPartials; ++k)
        {
            const double kk = k + 1.0;
            const double expect = kk * st.f0 * std::sqrt ((1 + nt.inharmonicity * kk * kk) / (1 + nt.inharmonicity));
            CHECKF (std::abs (st.freq[k] / expect - 1.0) < 1e-4, "note %d partial %d", n, k + 1);
            CHECKF (st.sigma[k] >= st.sigma[k - 1] * 0.5, "note %d loss not rising", n);
            CHECK (st.rc[k] * st.rc[k] + st.rs[k] * st.rs[k] < 1.0f);
        }
        const double t60 = 6.908 / st.sigma[0];
        std::printf ("     %3d   %d     %3d    %.2e  %6.2fs  %.1e  %.2f\n", n, nt.numStrings, st.numPartials, nt.inharmonicity, t60, nt.feltK, nt.feltP);
        if (n >= 36) CHECKF (nt.inharmonicity > lastB * 0.8, "B should rise through the compass at %d", n);
        lastB = nt.inharmonicity;
        CHECKF (nt.numStrings == (n <= 28 ? 1 : n <= 47 ? 2 : 3), "note %d strings", n);
        CHECK (nt.hasDamper == (n <= 88));
        CHECK (nt.hammerMass > 0.003 && nt.hammerMass < 0.013);
    }
    // register decay-time ranges (fundamental)
    auto t60 = [&] (int n) { return 6.908 / t->notes[(size_t) (n - kFirstNote)].strings[0].sigma[0]; };
    CHECKF (t60 (33) > 6 && t60 (33) < 60, "A1 T60 %.1f", t60 (33));
    CHECKF (t60 (60) > 4 && t60 (60) < 30, "C4 T60 %.1f", t60 (60));
    CHECKF (t60 (96) > 0.4 && t60 (96) < 6, "C7 T60 %.1f", t60 (96));
    CHECKF (t60 (108) > 0.15 && t60 (108) < 3, "C8 T60 %.1f", t60 (108));
    // B within published ranges
    CHECK (t->notes[0].inharmonicity > 2e-5 && t->notes[0].inharmonicity < 5e-4);
    CHECK (t->notes[60 - kFirstNote].inharmonicity > 1e-4 && t->notes[60 - kFirstNote].inharmonicity < 1.5e-3);
    CHECK (t->notes[kNumNotes - 1].inharmonicity > 3e-3 && t->notes[kNumNotes - 1].inharmonicity < 0.1);

    // unison detune: three strings spread around f0
    const auto& c4 = t->notes[60 - kFirstNote];
    CHECK (c4.strings[0].f0 < c4.f0 && c4.strings[2].f0 > c4.f0);
    CHECK (std::abs (1200.0 * std::log2 (c4.strings[2].f0 / c4.strings[0].f0)) > 1.0);

    // soundboard: modes inside range, admittance plausible
    double ymax = 0, ymean = 0; int cnt = 0;
    for (int m = 0; m < kSoundboardModes; ++m)
    {
        const auto& md = t->board.modes[(size_t) m];
        CHECK (md.freq >= 25 && md.freq < 20000);
        CHECK (md.sigma > 0 && md.rc * md.rc + md.rs * md.rs < 1.0f);
    }
    for (double f = 60; f < 2000; f *= 1.05) { const double y = t->board.admittanceAt (f, 4); ymax = std::max (ymax, y); ymean += y; ++cnt; }
    ymean /= cnt;
    std::printf ("     soundboard admittance: mean %.2e, max %.2e m/s/N\n", ymean, ymax);
    CHECK (ymean > 2e-4 && ymean < 1e-2);
    CHECK (ymax < 0.05);

    // sample-rate independence of the physics
    auto t96 = ModelBuilder::build (p.v, p.tuning, 96000.0, 2);
    auto t44 = ModelBuilder::build (p.v, p.tuning, 44100.0, 3);
    for (int n : { 21, 60, 100 })
    {
        const auto& a = t->notes[(size_t) (n - kFirstNote)].strings[0];
        const auto& b = t96->notes[(size_t) (n - kFirstNote)].strings[0];
        const auto& c = t44->notes[(size_t) (n - kFirstNote)].strings[0];
        for (int k = 0; k < std::min (a.numPartials, 20); ++k)
        {
            CHECKF (std::abs (a.freq[k] - b.freq[k]) < 1e-3f, "freq differs across rates (note %d, partial %d)", n, k);
            CHECKF (std::abs (a.sigma[k] - b.sigma[k]) < 1e-4f * (1 + a.sigma[k]), "loss differs across rates (note %d)", n);
            CHECKF (std::abs (a.freq[k] - c.freq[k]) < 1e-3f, "freq differs at 44.1k (note %d)", n);
            // rotation angle scales with 1/fs
            const double angA = std::atan2 (a.rs[k], a.rc[k]), angB = std::atan2 (b.rs[k], b.rc[k]);
            CHECKF (std::abs (angA / angB - 2.0) < 1e-3, "angle ratio %g", angA / angB);
        }
        CHECK (b.numPartials >= a.numPartials);
    }

    // the tuning parameters reach the tables: concert pitch, temperament, root, stretch
    {
        Patch q; q.v[concertPitch] = 442.0f;
        auto tq = ModelBuilder::build (q.v, q.tuning, 48000.0, 20);
        CHECKF (std::abs (tq->notes[69 - kFirstNote].f0 - 442.0) < 1e-3, "concert pitch parameter: A4 = %.3f", tq->notes[69 - kFirstNote].f0);
        Patch w; w.v[temperament] = (float) Temperament::Werckmeister; w.v[temperamentRoot] = 0;
        auto tw = ModelBuilder::build (w.v, w.tuning, 48000.0, 21);
        CHECK (tw->notes[64 - kFirstNote].f0 < 329.6);                  // E flatter than equal temperament
        CHECK (std::abs (tw->notes[60 - kFirstNote].f0 - 261.6256) < 1e-3);
        Patch s; s.v[stretch] = 1.0f;
        auto ts = ModelBuilder::build (s.v, s.tuning, 48000.0, 22);
        CHECK (ts->notes[0].f0 < 27.5 && ts->notes[kNumNotes - 1].f0 > 4186.0);
    }

    // every instrument builds and differs from the concert grand
    const auto& ref = t->notes[60 - kFirstNote];
    for (int inst = 1; inst < kNumInstruments; ++inst)
    {
        Patch q; q.v[instrument] = (float) inst;
        auto ti = ModelBuilder::build (q.v, q.tuning, 48000.0, 10 + (uint32_t) inst);
        CHECK (ti->instrumentIndex == inst);
        const auto& nt = ti->notes[60 - kFirstNote];
        const bool differs = std::abs (nt.inharmonicity - ref.inharmonicity) > 1e-6 || std::abs (nt.feltK - ref.feltK) > 1 ||
                             std::abs (nt.strings[0].sigma[5] - ref.strings[0].sigma[5]) > 1e-3 || nt.numStrings != ref.numStrings;
        CHECKF (differs, "instrument %d is identical to the concert grand at C4", inst);
        for (int n = 0; n < kNumNotes; ++n)
            for (int s = 0; s < ti->notes[(size_t) n].numStrings; ++s)
                CHECK (ti->notes[(size_t) n].strings[s].numPartials >= 1);
    }
}

// One partial run through the voice's own rotation: frequency and decay must be exact.
void resonatorTests()
{
    const double fs = 48000, f = 440.0, sigma = 2.0;
    const double r = std::exp (-sigma / fs), w = kTwoPi * f / fs;
    float p = 0, q = 1;    // starts with velocity only (a struck string)
    const float rc = (float) (r * std::cos (w)), rs = (float) (r * std::sin (w));
    std::vector<float> x (48000);
    for (int i = 0; i < 48000; ++i)
    {
        const float np = rc * p + rs * q;
        q = rc * q - rs * p;
        p = np;
        x[(size_t) i] = p;
    }
    const double measured = peakFreq (x, 0, 24000, 430, 450, fs);
    CHECKF (std::abs (measured - f) < 0.05, "resonator %.3f Hz", measured);
    const double a0 = magAt (x, 0, 4800, f, fs), a1 = magAt (x, 24000, 28800, f, fs);
    const double expectedRatio = std::exp (-sigma * 0.5);
    CHECKF (std::abs (a1 / a0 / expectedRatio - 1.0) < 0.02, "decay ratio %.4f expected %.4f", a1 / a0, expectedRatio);
    // very low frequency keeps precision in float (27.5 Hz, 30 s)
    p = 0; q = 1;
    const double fl = 27.5, rl = std::exp (-0.1 / fs), wl = kTwoPi * fl / fs;
    const float rcl = (float) (rl * std::cos (wl)), rsl = (float) (rl * std::sin (wl));
    double maxAmp = 0;
    for (int i = 0; i < 48000 * 10; ++i)
    {
        const float np = rcl * p + rsl * q;
        q = rcl * q - rsl * p; p = np;
        if (i > 48000 * 9) maxAmp = std::max (maxAmp, (double) std::abs (p));
    }
    CHECKF (maxAmp < 1.0 && maxAmp > 0.3, "low-frequency float resonator amplitude after 10 s: %g", maxAmp);
}

void hammerTests()
{
    NoteTable nt;   // defaults: C4-ish
    double last = 0;
    for (int v = 1; v <= 127; v += 7)
    {
        const double s = Hammer::velocityToSpeed (v / 127.0, 0.25, 6.0, 0.5, 0.0);
        CHECKF (s > last, "hammer speed not monotonic at %d", v);
        last = s;
    }
    CHECK (Hammer::velocityToSpeed (1.0, 0.25, 6.0, 0.5, 0.0) > 5.0 && Hammer::velocityToSpeed (1.0, 0.25, 6.0, 0.5, 0.0) <= 6.0);
    CHECK (Hammer::velocityToSpeed (1 / 127.0, 0.25, 6.0, 0.5, 0.0) < 0.6);
    CHECK (Hammer::velocityToSpeed (0.5, 0.25, 6.0, 0.5, 1.0) > Hammer::velocityToSpeed (0.5, 0.25, 6.0, 0.5, -1.0));
    Hammer h;
    h.strike (nt, 0.7, 48000, 0.5, 0.0, 0.0);
    CHECK (h.active && h.v > 0);
    CHECK (h.force (0, 0.0) == 0.0);        // no compression yet
    h.eta = 1e-3;
    CHECK (h.force (0, 0.0) > 0.0);
    CHECK (h.force (0, 2e-3) == 0.0);       // string ahead of the hammer

    // contact times by register, on a real voice
    Patch p;
    Setup s (p);
    std::printf ("     contact (ms) at velocity 64 / 120:");
    for (int n : { 24, 48, 60, 84, 104 })
    {
        double ms[2] = { 0, 0 };
        int k = 0;
        for (int vel : { 64, 120 })
        {
            float L[512], R[512];
            s.engine->panic();
            s.engine->render (L, R, 512);      // the panic fades and resets on the next chunk
            s.engine->noteOn (n, vel);
            for (int b = 0; b < 8; ++b) s.engine->render (L, R, 512);
            const auto* v = s.engine->voiceManager().voiceForNote (n);
            CHECK (v != nullptr);
            if (v != nullptr) ms[k] = v->hammer.contactSamples / 48.0;
            ++k;
        }
        std::printf ("  %d: %.2f/%.2f", n, ms[0], ms[1]);
        CHECKF (ms[0] > 0.3 && ms[0] < 8.0, "contact time note %d = %.2f ms", n, ms[0]);
        CHECKF (ms[1] < ms[0], "harder strike should be shorter at %d (%.2f vs %.2f)", n, ms[1], ms[0]);
    }
    std::printf ("\n");
}

void pedalTests()
{
    Pedals p;
    p.halfPoint = 0.5;
    CHECK (p.setSustain (0.0) == Pedals::Event::None && p.lift == 0.0);
    CHECK (p.setSustain (1.0) == Pedals::Event::Down && p.lift == 1.0);
    CHECK (p.setSustain (0.5) == Pedals::Event::None && std::abs (p.lift - 0.5) < 1e-9);
    CHECK (p.setSustain (0.0) == Pedals::Event::Up);
    p.switchMode = true;
    p.setSustain (0.4); CHECK (p.lift == 0.0);
    p.setSustain (0.6); CHECK (p.lift == 1.0);
    CHECK (p.setSostenuto (true) && ! p.setSostenuto (true));

    Damper d; d.prepare (48000, 25);
    d.update (true, false, 0); CHECK (d.pressure == 0);
    for (int i = 0; i < 200; ++i) d.update (false, false, 0);
    CHECK (d.pressure > 0.99);
    d.update (false, false, 1.0); CHECK (d.target == 0);
    for (int i = 0; i < 100; ++i) d.update (false, false, 1.0);
    CHECK (d.pressure < 0.01);
    d.update (false, true, 0); CHECK (d.target == 0);   // sostenuto holds it up
    d.hasDamper = false; d.update (false, false, 0); CHECK (d.pressure == 0);
}

void noiseAndFxTests()
{
    NoisePool pool; pool.prepare (48000);
    pool.trigger (NoiseType::Hammer, 3, 0.0f, 1.0, 0.8, 0.5);
    pool.trigger (NoiseType::PedalDown, 0, -0.5f, 1.0, 1.0, 0.5);
    CHECK (pool.activeCount() == 2);
    double e = 0;
    for (int c = 0; c < 400; ++c)
    {
        float zf[kNumZones][kChunk] {}; float dl[kChunk] {}, dr[kChunk] {};
        pool.processChunk (kChunk, zf, dl, dr);
        for (int i = 0; i < kChunk; ++i) { e += zf[3][i] * zf[3][i]; CHECK (std::isfinite (dl[i])); }
    }
    CHECK (e > 0 && pool.activeCount() == 0);

    Reverb rv; rv.prepare (48000);
    rv.setType (2); rv.setSize (0.8); rv.setDamping (0.3);
    float inL[kChunk] {}, inR[kChunk] {}, oL[kChunk], oR[kChunk];
    inL[0] = inR[0] = 1.0f;
    double early = 0, late = 0;
    for (int c = 0; c < 48000 * 8 / kChunk; ++c)
    {
        rv.processChunk (kChunk, inL, inR, oL, oR);
        inL[0] = inR[0] = 0;
        for (int i = 0; i < kChunk; ++i)
        {
            CHECK (std::isfinite (oL[i]));
            if (c < 1500) early += oL[i] * oL[i]; else if (c > 48000 * 7 / kChunk) late += oL[i] * oL[i];
        }
    }
    CHECK (early > 1e-6 && late < early * 1e-4);

    SpatialRenderer sr; sr.prepare (48000);
    float z[kChunk] {}, L[kChunk], R[kChunk], sL[kChunk], sR[kChunk];
    for (int c = 0; c < 100; ++c) sr.processChunk (kChunk, z, z, z, z, 1.0f, L, R, sL, sR);
    CHECK (std::abs (L[5]) < 1e-9f && std::abs (R[5]) < 1e-9f);
    float sine[kChunk];
    for (int i = 0; i < kChunk; ++i) sine[i] = (float) std::sin (kTwoPi * 1000 * i / 48000.0) * 5.0f;
    for (int c = 0; c < 100; ++c) sr.processChunk (kChunk, sine, sine, z, z, 1.0f, L, R, sL, sR);
    for (int i = 0; i < kChunk; ++i) CHECK (std::abs (L[i]) <= 1.0f && std::isfinite (L[i]));
}
} // namespace

int runDspTests()
{
    helperTests();
    tuningTests();
    paramTableTests();
    modelBuilderTests();
    resonatorTests();
    hammerTests();
    pedalTests();
    noiseAndFxTests();
    std::printf ("   dsp: %d failure(s) so far\n", failures);
    return failures;
}
