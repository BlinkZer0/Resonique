// --compare <folder> [--instrument N] [--out <csv>]
// Compares the model against reference recordings of single notes. The folder holds files named
// like the University of Iowa Musical Instrument Samples ("Piano.ff.C4.aiff", "Piano.pp.Bb0.aiff";
// any format JUCE decodes). For every file the same features are measured on the recording and
// on a render of the same note at a matching velocity (pp 28, mf 76, ff 118):
//   - inharmonicity B fitted from the measured partial frequencies
//   - the first partials' levels relative to the fundamental, 30-230 ms after the onset
//   - the T60 of partials 1, 2 and 3
//   - the spectral centroid of the first 300 ms
//   - the decay of the whole signal 1, 2 and 4 s after the onset
// and the differences are summarised. Nothing is bundled: the references stay the user's.
#include "TestRender.h"

using namespace resonique;
using namespace resonique::test;

namespace
{
struct Features
{
    double f1 = 0, B = 0, centroid = 0;
    double partialDb[12] {};     // relative to partial 1 (0 for the fundamental)
    bool partialOk[12] {};
    double t60[3] {};
    double drop1 = 0, drop2 = 0, drop4 = 0;   // dB below the early level at 1 / 2 / 4 s
    double peakDb = 0;
    double onset = 0, length = 0;
    double bandDb[7] {};        // energy share of 0-250, 250-500, 500-1k, 1-2k, 2-4k, 4-8k, 8-20k Hz (dB re total), first 300 ms
};

void bandShares (const std::vector<float>& x, size_t from, size_t to, double sr, double* out)
{
    to = std::min (to, x.size());
    int order = 10;
    while ((size_t) (1 << order) < to - from && order < 18) ++order;
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    const size_t len = to - from;
    for (size_t i = 0; i < len; ++i) buf[i] = (float) (x[from + i] * (0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) len)));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    static const double edges[8] = { 0, 250, 500, 1000, 2000, 4000, 8000, 20000 };
    double e[7] {}, tot = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double hz = k * sr / N, p = (double) buf[(size_t) k] * buf[(size_t) k];
        for (int b = 0; b < 7; ++b) if (hz >= edges[b] && hz < edges[b + 1]) { e[b] += p; break; }
        if (hz < 20000) tot += p;
    }
    for (int b = 0; b < 7; ++b) out[b] = dB (std::sqrt (e[b] / std::max (tot, 1e-30)));
}

int noteFromName (const juce::String& name)   // "C4" / "Bb0" / "Db1"
{
    static const char* names[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const auto parts = juce::StringArray::fromTokens (name, ".", "");
    for (const auto& tok : parts)
    {
        if (tok.length() < 2 || tok.length() > 3) continue;
        const auto pitch = tok.dropLastCharacters (1);
        const int octave = tok.getLastCharacter() - '0';
        if (octave < 0 || octave > 9) continue;
        for (int i = 0; i < 12; ++i)
            if (pitch == names[i]) return 12 * (octave + 1) + i;
        // sharps
        if (pitch.endsWith ("#"))
            for (int i = 0; i < 12; ++i)
                if (juce::String (names[i]).substring (0, 1) == pitch.substring (0, 1)) return 12 * (octave + 1) + i + 1;
    }
    return -1;
}

int velocityFromName (const juce::String& name)
{
    if (name.contains (".pp.")) return 28;
    if (name.contains (".mf.")) return 76;
    if (name.contains (".ff.")) return 118;
    return 90;
}

// onset: from the loudest 5 ms window walk back while the level stays within 24 dB of it;
// the first window below that is the start of the attack (lead-in noise and room are ignored)
double findOnset (const std::vector<float>& x, double sr)
{
    const size_t w = (size_t) (0.005 * sr);
    double best = 0; size_t bi = 0;
    for (size_t i = 0; i + w < x.size(); i += w) { const double r = rms (x, i, i + w); if (r > best) { best = r; bi = i; } }
    size_t i = bi;
    while (i >= w && rms (x, i - w, i) > best * 0.063) i -= w;
    return (double) i / sr;
}

Features analyse (const std::vector<float>& x, double sr, double nominalF0)
{
    Features f;
    const double t0 = findOnset (x, sr);
    const double len = (double) x.size() / sr - t0;
    f.onset = t0; f.length = len;
    auto seg = [&] (double a, double b) { return std::make_pair ((size_t) ((t0 + a) * sr), (size_t) std::min ((double) x.size(), (t0 + std::min (b, len)) * sr)); };
    for (float v : x) f.peakDb = std::max (f.peakDb, (double) std::abs (v));
    f.peakDb = dB (f.peakDb);

    // the fundamental, then the partial series with a fitted B
    auto [a, b] = seg (0.03, std::min (0.63, len * 0.5));
    f.f1 = peakFreq (x, a, b, nominalF0 * 0.95, nominalF0 * 1.05, sr);
    double B = 1e-4;
    for (int iter = 0; iter < 4; ++iter)
    {
        double sumB = 0; int cnt = 0;
        for (int n = 2; n <= 8; ++n)
        {
            const double expect = n * f.f1 * std::sqrt ((1 + B * n * n) / (1 + B));
            if (expect > sr * 0.45) break;
            const double fn = peakFreq (x, a, b, expect * 0.96, expect * 1.04, sr);
            const double r = fn / (n * f.f1);
            const double Bn = (r * r - 1.0) / (n * n - r * r);
            if (Bn > 0 && Bn < 0.05 && n >= 3) { sumB += Bn; ++cnt; }
        }
        if (cnt > 0) B = sumB / cnt;
    }
    f.B = B;
    const double ref = magAt (x, a, (size_t) std::min ((double) x.size(), (t0 + 0.23) * sr), f.f1, sr);
    for (int n = 1; n <= 12; ++n)
    {
        const double expect = n * f.f1 * std::sqrt ((1 + B * n * n) / (1 + B));
        if (expect > sr * 0.45) break;
        const double fn = peakFreq (x, a, (size_t) std::min ((double) x.size(), (t0 + 0.23) * sr), expect * 0.985, expect * 1.015, sr);
        const double m = magAt (x, a, (size_t) std::min ((double) x.size(), (t0 + 0.23) * sr), fn, sr);
        f.partialDb[n - 1] = dB (m) - dB (ref);
        f.partialOk[n - 1] = m > 1e-6;
    }
    for (int n = 1; n <= 3; ++n)
    {
        const double expect = n * f.f1 * std::sqrt ((1 + B * n * n) / (1 + B));
        f.t60[n - 1] = t60At (x, expect, t0 + 0.3, t0 + std::min (len, 4.0), 0.25, sr);
    }
    auto [c0, c1] = seg (0.0, 0.3);
    f.centroid = centroid (x, c0, c1, sr);
    bandShares (x, seg (0.03, 0.33).first, seg (0.03, 0.33).second, sr, f.bandDb);
    const double early = dB (rms (x, seg (0.05, 0.15).first, seg (0.05, 0.15).second));
    auto drop = [&] (double t) { if (len < t + 0.1) return 0.0; return early - dB (rms (x, seg (t, t + 0.1).first, seg (t, t + 0.1).second)); };
    f.drop1 = drop (1.0); f.drop2 = drop (2.0); f.drop4 = drop (4.0);
    return f;
}

bool loadMono (const juce::File& file, std::vector<float>& out, double& sr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) return false;
    sr = r->sampleRate;
    const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (sr * 12.0));
    juce::AudioBuffer<float> buf ((int) r->numChannels, n);
    r->read (&buf, 0, n, 0, true, true);
    out.assign ((size_t) n, 0.0f);
    for (int ch = 0; ch < (int) r->numChannels; ++ch)
        for (int i = 0; i < n; ++i) out[(size_t) i] += buf.getSample (ch, i) / (float) r->numChannels;
    return true;
}
} // namespace

// --probe-voice <note> [vel]: the raw bridge force of one voice (no board, no renderer), band shares and
// per-sample steps, to tell string/hammer artefacts from rendering ones.
int runProbeVoice (int note, int vel)
{
    Patch p;
    auto t = ModelBuilder::build (p.v, p.tuning, kSr, 1);
    auto v = std::make_unique<PianoVoice>();
    v->start (t->notes[(size_t) (note - kFirstNote)], vel / 127.0, kSr, 0.5, 0.0, 0.0, 0.5);
    const int total = (int) (1.0 * kSr);
    std::vector<float> f ((size_t) total, 0.0f);
    float za[kChunk] {};
    for (int pos = 0; pos < total; pos += kChunk)
    {
        v->updateDamper (0.0);
        v->processSamples (kChunk, za, 0.0f, f.data() + pos);
        v->endChunk();
    }
    double bands[7];
    const char* names[7] = { "0-250", "250-500", "500-1k", "1-2k", "2-4k", "4-8k", "8-20k" };
    for (double t0 : { 0.03, 0.3, 0.6 })
    {
        bandShares (f, (size_t) (t0 * kSr), (size_t) ((t0 + 0.3) * kSr), kSr, bands);
        std::printf ("bridge force %.2f-%.2f s:", t0, t0 + 0.3);
        for (int b = 0; b < 7; ++b) std::printf ("  %s %.1f", names[b], bands[b]);
        std::printf ("\n");
    }
    double peak = 0; for (float x : f) peak = std::max (peak, (double) std::abs (x));
    std::printf ("peak %.2f N, max step 0.05-1 s: %.3g N, contact %.2f ms, partials active %d\n", peak, maxStep (f, (size_t) (0.05 * kSr)),
                 v->hammer.contactSamples / kSr * 1000.0, v->strings[0].active);
    // the top partials' individual levels: do the highest modes carry energy they should not?
    const auto& st = v->strings[0];
    std::printf ("top partials (k: freq Hz, |force| N):");
    for (int k = std::max (0, st.numPartials - 6); k < st.numPartials; ++k)
        std::printf ("  %d: %.0f %.2e", k + 1, st.freq[k], std::sqrt (st.p[k] * st.p[k] + st.q[k] * st.q[k]) * std::abs (st.bridgeW[k]));
    std::printf ("\n");
    return 0;
}

// --probe-stability: the 88-key stress scenario under several coupling / loss settings; prints the tail levels
int runProbeStability()
{
    struct S { const char* name; float reso, coupling, decay, board; };
    const S cases[] = { { "defaults", 0.5f, 0.5f, 1.0f, 0.5f }, { "reso 1", 1.0f, 0.5f, 1.0f, 0.5f }, { "decay 4", 0.5f, 0.5f, 4.0f, 0.5f },
                        { "board 1", 0.5f, 0.5f, 1.0f, 1.0f }, { "coupling 1", 0.5f, 1.0f, 1.0f, 0.5f }, { "reso 1 + decay 4", 1.0f, 0.5f, 4.0f, 0.5f },
                        { "reso 1 + board 1", 1.0f, 0.5f, 1.0f, 1.0f }, { "all max", 1.0f, 1.0f, 4.0f, 1.0f } };
    for (const auto& c : cases)
    {
        Patch p;
        p.set ("stringResonance", c.reso).set ("bridgeCoupling", c.coupling).set ("stringDecay", c.decay).set ("soundboardResonance", c.board).set ("polyphony", 128);
        std::vector<Event> ev { cc (0.0, 64, 127) };
        for (int n = kFirstNote; n <= kLastNote; ++n) ev.push_back (on (0.002 * (n - kFirstNote), n, 127));
        for (int n = kFirstNote; n <= kLastNote; ++n) ev.push_back (off (1.0, n));
        ev.push_back (cc (2.0, 64, 0));
        auto m = render (p, 9.0, ev).mono();
        std::printf ("%-20s held %6.1f dB, then %6.1f / %6.1f / %6.1f / %6.1f dB at 3 / 5 / 7 / 8.5 s\n", c.name, dB (rmsSec (m, 0.5, 1.0)),
                     dB (rmsSec (m, 3.0, 3.5)), dB (rmsSec (m, 5.0, 5.5)), dB (rmsSec (m, 7.0, 7.5)), dB (rmsSec (m, 8.5, 9.0)));
    }
    return 0;
}

int runCompare (const juce::String& folderName, int instrumentIndex, const juce::String& csvOut, const juce::StringArray& overrides)
{
    const bool showBands = overrides.contains ("bands=1");
    const juce::File folder = juce::File::isAbsolutePath (folderName) ? juce::File (folderName) : juce::File::getCurrentWorkingDirectory().getChildFile (folderName);
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.aif;*.aiff;*.wav;*.flac");
    files.sort();
    if (files.isEmpty()) { std::printf ("no reference files in %s\n", folder.getFullPathName().toRawUTF8()); return 2; }

    juce::String csv ("file,note,vel,ref_B,mod_B,ref_centroid,mod_centroid,ref_p2,mod_p2,ref_p3,mod_p3,ref_p4,mod_p4,ref_p6,mod_p6,ref_p8,mod_p8,ref_t60_1,mod_t60_1,ref_t60_2,mod_t60_2,ref_t60_3,mod_t60_3,ref_drop1,mod_drop1,ref_drop2,mod_drop2,ref_drop4,mod_drop4\n");
    std::printf ("%-18s %4s | %9s %9s | %7s %7s | %6s %6s | %6s %6s | %6s %6s | %6s %6s | %6s %6s | %6s %6s | %6s %6s\n",
                 "file", "vel", "B ref", "B mod", "cent r", "cent m", "p2 r", "p2 m", "p3 r", "p3 m", "p6 r", "p6 m", "T60a r", "T60a m", "d1 r", "d1 m", "d2 r", "d2 m", "d4 r", "d4 m");
    double sumAbs[8] {}; int cnt = 0;
    for (const auto& file : files)
    {
        const int note = noteFromName (file.getFileNameWithoutExtension());
        if (note < kFirstNote || note > kLastNote) { std::printf ("skip %s (note not recognised)\n", file.getFileName().toRawUTF8()); continue; }
        std::vector<float> ref; double sr = 44100;
        if (! loadMono (file, ref, sr)) { std::printf ("skip %s (cannot decode)\n", file.getFileName().toRawUTF8()); continue; }
        const int vel = velocityFromName (file.getFileName());
        Patch p;
        p.set ("instrument", (float) instrumentIndex).set ("micPerspective", 0).set ("noiseMaster", 0.5f).set ("reverbMix", 0.0f);
        for (const auto& o : overrides)   // "id=value"
        {
            const int idx = paramIndexForId (o.upToFirstOccurrenceOf ("=", false, false).toStdString());
            if (idx >= 0) p.v[(size_t) idx] = o.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            else std::printf ("unknown parameter in --set %s\n", o.toRawUTF8());
        }
        const double nominal = noteFrequency (note, p.tuning);
        const double len = std::min (8.0, (double) ref.size() / sr);
        auto r = render (p, len, { on (0.0, note, vel, sr), off (len - 0.1, note, sr) }, sr);
        auto m = r.mono();
        const Features a = analyse (ref, sr, nominal), b = analyse (m, sr, nominal);
        if (a.length < 1.5) std::printf ("note: %s has only %.1f s after its onset at %.2f s\n", file.getFileName().toRawUTF8(), a.length, a.onset);
        std::printf ("%-18s %4d | %9.2e %9.2e | %7.0f %7.0f | %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f\n",
                     file.getFileName().toRawUTF8(), vel, a.B, b.B, a.centroid, b.centroid, a.partialDb[1], b.partialDb[1], a.partialDb[2], b.partialDb[2],
                     a.partialDb[5], b.partialDb[5], a.t60[0], b.t60[0], a.drop1, b.drop1, a.drop2, b.drop2, a.drop4, b.drop4);
        if (showBands)
            std::printf ("      bands (dB re total, 30-330 ms)  ref: %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f   mod: %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f\n",
                         a.bandDb[0], a.bandDb[1], a.bandDb[2], a.bandDb[3], a.bandDb[4], a.bandDb[5], a.bandDb[6],
                         b.bandDb[0], b.bandDb[1], b.bandDb[2], b.bandDb[3], b.bandDb[4], b.bandDb[5], b.bandDb[6]);
        csv << file.getFileName() << "," << note << "," << vel << "," << a.B << "," << b.B << "," << a.centroid << "," << b.centroid;
        for (int n : { 1, 2, 3, 5, 7 }) csv << "," << a.partialDb[n] << "," << b.partialDb[n];
        for (int n = 0; n < 3; ++n) csv << "," << a.t60[n] << "," << b.t60[n];
        csv << "," << a.drop1 << "," << b.drop1 << "," << a.drop2 << "," << b.drop2 << "," << a.drop4 << "," << b.drop4 << "\n";
        const double d[8] = { std::abs (std::log10 (std::max (1e-6, b.B) / std::max (1e-6, a.B))) * 20, std::abs (dB (b.centroid) - dB (a.centroid)),
                              std::abs (b.partialDb[1] - a.partialDb[1]), std::abs (b.partialDb[2] - a.partialDb[2]), std::abs (b.partialDb[5] - a.partialDb[5]),
                              std::abs (b.drop1 - a.drop1), std::abs (b.drop2 - a.drop2), std::abs (b.drop4 - a.drop4) };
        for (int i = 0; i < 8; ++i) sumAbs[i] += d[i];
        ++cnt;
    }
    if (cnt > 0)
        std::printf ("\nmean |difference| over %d notes: B %.1f dB, centroid %.1f dB, partial 2 %.1f dB, partial 3 %.1f dB, partial 6 %.1f dB, decay at 1 s %.1f dB, 2 s %.1f dB, 4 s %.1f dB\n",
                     cnt, sumAbs[0] / cnt, sumAbs[1] / cnt, sumAbs[2] / cnt, sumAbs[3] / cnt, sumAbs[4] / cnt, sumAbs[5] / cnt, sumAbs[6] / cnt, sumAbs[7] / cnt);
    if (csvOut.isNotEmpty())
    {
        const juce::File out = juce::File::isAbsolutePath (csvOut) ? juce::File (csvOut) : juce::File::getCurrentWorkingDirectory().getChildFile (csvOut);
        out.getParentDirectory().createDirectory();
        out.replaceWithText (csv);
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    }
    return 0;
}
