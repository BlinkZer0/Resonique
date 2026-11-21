#pragma once
// ============================================================================
//  Virtual microphones and the acoustic rendering chain.
//  Inputs per chunk: the soundboard's radiated signal (left/right from the
//  mode radiation weights), the direct bridge-force bus (per-voice panned),
//  and the mechanical noise "air" bus. The perspective sets distance, spread,
//  orientation (player / audience), how much of the dense high-frequency
//  radiation and how much room reaches the mics. Then early reflections, the
//  reverb, the tone shelves, the master and the safety clipper.
// ============================================================================
#include "Reverb.h"

namespace resonique
{
struct MicSettings
{
    double distance = 1.2;      // m
    double spread = 0.6;        // stereo separation
    double position = 0.0;      // -1 bass .. 1 treble
    bool flip = false;          // audience perspective: bass on the right
    double hfLevel = 1.0;       // direct dense-mode radiation
    double erLevel = 0.3;
    double width = 0.7;
    double brightness = 0.0;    // -1..1
};

class SpatialRenderer
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reverb.prepare (fs);
        const int erMax = (int) (0.09 * fs) + 8;
        erBufL.assign ((size_t) erMax, 0.0f);
        erBufR.assign ((size_t) erMax, 0.0f);
        erPos = 0;
        dcL.prepare (fs); dcR.prepare (fs);
        hpL.highPass (fs, 2200.0, 0.6); hpR.highPass (fs, 2200.0, 0.6);
        airL.lowPass (fs, 16000.0); airR.lowPass (fs, 16000.0);
        setTone (0, 0);
        masterSm.setTime (0.02, fs); masterSm.reset (0.8);
        mixSm.setTime (0.05, fs); mixSm.reset (0.18);
        widthSm.setTime (0.05, fs); widthSm.reset (0.7);
        hfSm.setTime (0.05, fs); hfSm.reset (1.0);
        erSm.setTime (0.05, fs); erSm.reset (0.3);
        distSm.setTime (0.08, fs); distSm.reset (1.2);
        updateEr (0.5);
        micsPrimed = false;
        gainsPrimed = false;
    }

    // Everything that holds signal: the room, the early reflections and the filters (a filter left
    // ringing after a panic would re-excite the freshly cleared reverb with a faint click).
    void reset()
    {
        reverb.reset();
        std::fill (erBufL.begin(), erBufL.end(), 0.0f);
        std::fill (erBufR.begin(), erBufR.end(), 0.0f);
        for (auto* f : { &hpL, &hpR, &bassL, &bassR, &trebL, &trebR, &airL, &airR }) f->reset();
        dcL.reset();
        dcR.reset();
    }
    void setMaster (double g, bool snap) { masterSm.target = g; if (snap) masterSm.reset (g); }

    void setMics (const MicSettings& m)
    {
        mics = m;
        distSm.target = m.distance;
        widthSm.target = m.width;
        hfSm.target = m.hfLevel;
        erSm.target = m.erLevel;
        if (! micsPrimed)   // the first configuration after prepare(): no need to glide there
        {
            distSm.reset (m.distance); widthSm.reset (m.width); hfSm.reset (m.hfLevel); erSm.reset (m.erLevel);
            micsPrimed = true;
        }
        const double bright = m.brightness;
        hpL.highPass (fs, 2200.0 * std::pow (2.0, -bright), 0.6);
        hpR.copyCoefficients (hpL);
        // air absorption with distance
        const double air = clampT (18000.0 / (1.0 + 0.35 * m.distance), 5000.0, 18000.0);
        airL.lowPass (fs, air); airR.copyCoefficients (airL);
    }
    void setTone (double bassDb, double trebleDb)
    {
        bassL.lowShelf (fs, 220.0, bassDb); bassR.copyCoefficients (bassL);
        trebL.highShelf (fs, 3800.0, trebleDb); trebR.copyCoefficients (trebL);
    }
    void setMaster (double g) { masterSm.target = g; }
    void setReverb (int type, double size, double damp, double mix, double predelayMs)
    {
        if (type != reverbType) { reverbType = type; reverb.setType (std::min (type, 3)); }
        if (std::abs (size - rvSize) > 1e-4) { rvSize = size; reverb.setSize (size); updateEr (size); }
        if (std::abs (damp - rvDamp) > 1e-4) { rvDamp = damp; reverb.setDamping (damp); }
        if (std::abs (predelayMs - rvPre) > 1e-3) { rvPre = predelayMs; reverb.setPredelay (predelayMs); }
        mixSm.target = mix;
    }
    bool usesExternalReverb() const { return reverbType >= 4; }

    // Once per kChunk samples: advances the smoothers and sets up the per-sample gain ramps.
    // Every gain ramps linearly across the chunk from its previous value: a per-chunk staircase would
    // modulate the signal at the chunk rate and smear broadband sidebands over the spectrum.
    void beginChunk()
    {
        const double master = masterSm.advance();
        const double mix = mixSm.advance();
        const double width = widthSm.advance();
        const double hf = hfSm.advance();
        const double er = erSm.advance();
        const double dist = distSm.advance();
        // distance: direct level falls, the room grows
        const double directGain = clampT (1.0 / std::sqrt (std::max (0.3, dist)), 0.28, 1.6);
        const double roomGain = clampT (0.35 + 0.3 * dist, 0.3, 2.0);
        const double hfGain = hf * directGain * 0.65 * kDirectScale;   // bridge force (N) -> board-velocity units (m/s)
        const double sideGain = width * 1.6;
        if (! gainsPrimed) { gDirect = directGain; gHf = hfGain; gSide = sideGain; gEr = er * roomGain * 0.5; gRoom = roomGain; gMaster = master; gMix = mix; gainsPrimed = true; }
        const float invN = 1.0f / (float) kChunk;
        dDirect = (float) (directGain - gDirect) * invN; dHf = (float) (hfGain - gHf) * invN; dSide = (float) (sideGain - gSide) * invN;
        dEr = (float) (er * roomGain * 0.5 - gEr) * invN; dRoom = (float) (roomGain - gRoom) * invN;
        dMaster = (float) (master - gMaster) * invN; dMix = (float) (mix - gMix) * invN;
        tDirect = directGain; tHf = hfGain; tSide = sideGain; tEr = er * roomGain * 0.5; tRoom = roomGain; tMaster = master; tMix = mix;
        rampPos = 0;
    }

    // Whole-chunk convenience (tests): a chunk of n <= kChunk samples
    void processChunk (int n, const float* boardL, const float* boardR, const float* directL, const float* directR,
                       float outputScale, float* outL, float* outR, float* sendL, float* sendR)
    {
        beginChunk();
        processSamples (n, boardL, boardR, directL, directR, outputScale, outL, outR, sendL, sendR);
    }

    // c samples of the current chunk (after beginChunk()). sendL/sendR receive the reverb send
    // (pre-reverb mix) so the host layer can run a convolution reverb instead.
    void processSamples (int n, const float* boardL, const float* boardR, const float* directL, const float* directR,
                         float outputScale, float* outL, float* outR, float* sendL, float* sendR)
    {
        const float flipL = mics.flip ? 1.0f : 0.0f;
        float gd = (float) gDirect, gh = (float) gHf, gs = (float) gSide, ge = (float) gEr, gr = (float) gRoom;

        float dry[2][kChunk];
        for (int i = 0; i < n; ++i)
        {
            gd += dDirect; gh += dHf; gs += dSide; ge += dEr; gr += dRoom;
            // board radiation + the direct (dense high-mode) path
            float dl = directL[i], dr = directR[i];
            if (flipL > 0.5f) std::swap (dl, dr);
            float l = boardL[i] * gd + hpL.process (dl) * gh;
            float r = boardR[i] * gd + hpR.process (dr) * gh;
            // mid/side width
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            l = mid + side * gs;
            r = mid - side * gs;
            l *= outputScale; r *= outputScale;
            // early reflections (cross-fed taps)
            erBufL[(size_t) erPos] = l; erBufR[(size_t) erPos] = r;
            float el = 0, erR = 0;
            for (int k = 0; k < kTaps; ++k)
            {
                int pos = erPos - tapDelay[k]; if (pos < 0) pos += (int) erBufL.size();
                el += (k & 1 ? erBufR : erBufL)[(size_t) pos] * tapGain[k];
                erR += (k & 1 ? erBufL : erBufR)[(size_t) pos] * tapGain[k];
            }
            erPos = (erPos + 1) % (int) erBufL.size();
            l += el * ge;
            r += erR * ge;
            dry[0][i] = l; dry[1][i] = r;
            sendL[i] = l * gr;
            sendR[i] = r * gr;
        }
        gDirect = gd; gHf = gh; gSide = gs; gEr = ge; gRoom = gr;

        float wet[2][kChunk];
        if (! usesExternalReverb())
            reverb.processChunk (n, sendL, sendR, wet[0], wet[1]);
        else
            for (int i = 0; i < n; ++i) wet[0][i] = wet[1][i] = 0;

        float gm = (float) gMaster, gx = (float) gMix;
        for (int i = 0; i < n; ++i)
        {
            gm += dMaster; gx += dMix;
            const float wetG = gx * 0.9f, dryG = 1.0f - 0.35f * gx;
            float l = dry[0][i] * dryG + wet[0][i] * wetG;
            float r = dry[1][i] * dryG + wet[1][i] * wetG;
            l = trebL.process (bassL.process (airL.process (l)));
            r = trebR.process (bassR.process (airR.process (r)));
            l = dcL.process (l) * gm;
            r = dcR.process (r) * gm;
            outL[i] = softClip (l);
            outR[i] = softClip (r);
        }
        gMaster = gm; gMix = gx;
        rampPos += n;
        if (rampPos >= kChunk)   // the ramp has landed: snap to the targets so rounding never drifts
        {
            gDirect = tDirect; gHf = tHf; gSide = tSide; gEr = tEr; gRoom = tRoom; gMaster = tMaster; gMix = tMix;
            dDirect = dHf = dSide = dEr = dRoom = dMaster = dMix = 0;
        }
    }

    // External (convolution) reverb returns here: mixes wet into the already-rendered output.
    double currentMix() const { return mixSm.value; }

private:
    static constexpr int kTaps = 6;
    static constexpr double kDirectScale = 1.0e-3;   // effective admittance of the dense high modes
    void updateEr (double size)
    {
        static const double ms[kTaps] = { 7.1, 11.3, 17.9, 23.7, 31.1, 41.9 };
        static const double g[kTaps] = { 0.55, -0.45, 0.4, -0.32, 0.26, -0.2 };
        const double scale = 0.6 + 1.2 * size;
        for (int k = 0; k < kTaps; ++k)
        {
            tapDelay[k] = clampT ((int) (ms[k] * scale * 1e-3 * fs), 1, (int) erBufL.size() - 2);
            tapGain[k] = (float) g[k];
        }
    }

    double fs = 48000;
    MicSettings mics;
    Reverb reverb;
    int reverbType = 2;
    double rvSize = -1, rvDamp = -1, rvPre = -1;
    std::vector<float> erBufL, erBufR;
    int erPos = 0;
    int tapDelay[kTaps] {};
    float tapGain[kTaps] {};
    Biquad hpL, hpR, bassL, bassR, trebL, trebR, airL, airR;
    DcBlocker dcL, dcR;
    Smoother masterSm, mixSm, widthSm, hfSm, erSm, distSm;
    bool micsPrimed = false, gainsPrimed = false;
    double gDirect = 1, gHf = 0, gSide = 1, gEr = 0, gRoom = 1, gMaster = 0.8, gMix = 0;    // current (ramping) gains
    double tDirect = 1, tHf = 0, tSide = 1, tEr = 0, tRoom = 1, tMaster = 0.8, tMix = 0;    // this chunk's targets
    float dDirect = 0, dHf = 0, dSide = 0, dEr = 0, dRoom = 0, dMaster = 0, dMix = 0;       // per-sample increments
    int rampPos = 0;
};
} // namespace resonique
