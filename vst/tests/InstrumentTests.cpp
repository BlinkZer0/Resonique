// The instrument library: a metrics table per instrument (contact time, inharmonicity, decay, brightness
// and level at C2 / C4 / C6) and sanity checks that every instrument is playable and differs from the base.
#include "TestRender.h"

using namespace resonique;
using namespace resonique::test;

namespace
{
struct NoteMetrics { double contactMs, B, t60, centroid, peakDb; };

// decay of the whole signal (robust to unison beating): slope of the RMS in dB from 0.5 s to the end
double broadbandT60 (const std::vector<float>& x, double t0, double t1, double win = 0.25)
{
    std::vector<double> ts, dbs;
    for (double t = t0; t + win <= t1; t += win) { const double r = rmsSec (x, t, t + win); if (r < 1e-9) break; ts.push_back (t); dbs.push_back (dB (r)); }
    if (ts.size() < 3) return 0;
    double mt = 0, md = 0;
    for (size_t i = 0; i < ts.size(); ++i) { mt += ts[i]; md += dbs[i]; }
    mt /= (double) ts.size(); md /= (double) ts.size();
    double num = 0, den = 0;
    for (size_t i = 0; i < ts.size(); ++i) { num += (ts[i] - mt) * (dbs[i] - md); den += (ts[i] - mt) * (ts[i] - mt); }
    const double slope = den > 0 ? num / den : 0;
    return slope < 0 ? -60.0 / slope : 1e9;
}

NoteMetrics measure (int instrument, int note)
{
    Patch p; p.set ("instrument", (float) instrument);
    NoteMetrics m {};
    {
        Setup s (p);
        m.B = s.tables->notes[(size_t) (note - kFirstNote)].inharmonicity;
        s.engine->noteOn (note, 76);
        float L[512], R[512];
        for (int b = 0; b < 10; ++b) s.engine->render (L, R, 512);
        if (const auto* v = s.engine->voiceManager().voiceForNote (note)) m.contactMs = v->hammer.contactSamples / kSr * 1000.0;
    }
    const double len = note >= 84 ? 2.5 : 4.0;
    auto r = render (p, len, { on (0.0, note, 76) });
    auto mono = r.mono();
    m.t60 = broadbandT60 (mono, 0.5, len, note >= 84 ? 0.15 : 0.25);
    m.centroid = centroid (mono, (size_t) (0.02 * kSr), (size_t) (0.3 * kSr));
    m.peakDb = dB (r.peak);
    return m;
}
} // namespace

// check = true adds CHECKs (the default test run); false only prints (--instruments)
int runInstrumentTable (bool check)
{
    std::printf ("     %-18s | %-34s | %-34s | %-34s\n", "instrument", "C2: ms  B      T60   cent  peak", "C4: ms  B      T60   cent  peak", "C6: ms  B      T60   cent  peak");
    NoteMetrics base[3] {};
    for (int inst = 0; inst < kNumInstruments; ++inst)
    {
        const int notes[3] = { 36, 60, 84 };
        NoteMetrics mm[3];
        std::printf ("     %-18s", instrumentChoices()[(size_t) inst].c_str());
        int diffs = 0;
        double levelOff[3] {};
        for (int k = 0; k < 3; ++k)
        {
            mm[k] = measure (inst, notes[k]);
            std::printf (" | %4.2f %.1e %5.1f %5.0f %6.1f", mm[k].contactMs, mm[k].B, mm[k].t60, mm[k].centroid, mm[k].peakDb);
            if (inst == 0) base[k] = mm[k];
            else
            {
                if (std::abs (mm[k].contactMs / std::max (0.01, base[k].contactMs) - 1.0) > 0.12) ++diffs;
                if (std::abs (mm[k].B / std::max (1e-9, base[k].B) - 1.0) > 0.15) ++diffs;
                if (std::abs (mm[k].t60 / std::max (0.01, base[k].t60) - 1.0) > 0.15) ++diffs;
                if (std::abs (mm[k].centroid / std::max (1.0, base[k].centroid) - 1.0) > 0.12) ++diffs;
            }
            levelOff[k] = mm[k].peakDb - base[k].peakDb;
            if (check)
            {
                const double lo[3] = { 1.5, 0.8, 0.3 }, hi[3] = { 14.0, 7.0, 4.5 };
                CHECKF (mm[k].contactMs > lo[k] && mm[k].contactMs < hi[k], "%s note %d contact %.2f ms", instrumentChoices()[(size_t) inst].c_str(), notes[k], mm[k].contactMs);
                const double tlo[3] = { 1.5, 1.0, 0.4 }, thi[3] = { 60.0, 40.0, 15.0 };
                CHECKF (mm[k].t60 > tlo[k] && mm[k].t60 < thi[k], "%s note %d T60 %.1f s", instrumentChoices()[(size_t) inst].c_str(), notes[k], mm[k].t60);
                CHECKF (mm[k].centroid > 150 && mm[k].centroid < 5000, "%s note %d centroid %.0f", instrumentChoices()[(size_t) inst].c_str(), notes[k], mm[k].centroid);
                CHECKF (mm[k].peakDb > -30 && mm[k].peakDb < -3, "%s note %d peak %.1f dBFS", instrumentChoices()[(size_t) inst].c_str(), notes[k], mm[k].peakDb);
                CHECKF (std::abs (levelOff[k]) < 8, "%s note %d is %.1f dB off the concert grand", instrumentChoices()[(size_t) inst].c_str(), notes[k], levelOff[k]);
            }
        }
        if (inst > 0) std::printf ("   level vs grand %+.1f / %+.1f / %+.1f dB", levelOff[0], levelOff[1], levelOff[2]);
        std::printf ("\n");
        if (check && inst > 0) CHECKF (diffs >= 2, "%s is too close to the concert grand (%d differing metrics)", instrumentChoices()[(size_t) inst].c_str(), diffs);
    }
    return failures;
}
