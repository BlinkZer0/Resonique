#pragma once
// ============================================================================
//  PianoEngine: the whole instrument, JUCE-free.
//    prepare(fs) -> setTables(t) at block boundaries -> setParamValues(v) ->
//    noteOn / noteOff / controller / pitchBend -> render(L, R, n)
//  Signal flow per 32-sample chunk (the soundboard exchange runs every sample):
//    voices (strings + hammer + local bridge + damper)  -> bridge force per zone
//    sympathetic bank (undamped light strings)           -> bridge force per zone
//    mechanical noises                                   -> zone forces + air bus
//    soundboard (modal, shared)                          -> radiation L/R, zone accelerations
//    spatial renderer (mics, ER, reverb, tone, master)   -> output
//  The zone accelerations feed back into the strings on the next coupling step
//  sample, which keeps the coupling dissipative even for near-lossless strings.
//  Nothing here allocates, locks or does I/O after prepare().
// ============================================================================
#include "dsp/VoiceManager.h"
#include "dsp/SoundboardModel.h"
#include "dsp/CouplingEngine.h"
#include "dsp/PedalModel.h"
#include "dsp/MechanicalNoise.h"
#include "dsp/SpatialRenderer.h"
#include "dsp/ParamTable.h"

namespace resonique
{
class PianoEngine
{
public:
    static constexpr float kOutputScale = 50.0f;   // board-velocity units (m/s) -> full scale (calibrated by the tests)
    static constexpr int kCoupleStep = 1;          // samples between soundboard -> string exchanges (1: the loop stays passive even for near-lossless strings)

    PianoEngine()
    {
        params = defaultParamValues();
    }

    // Off the audio thread.
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        board.prepare (fs);
        symp.prepare (fs);
        noises.prepare (fs);
        renderer.prepare (fs);
        voices.freeAll();
        std::memset (zoneAccel, 0, sizeof (zoneAccel));
        bendRatio = 1.0;
        chunkPos = 0;
        fading = false;
        panicRequested = false;
        if (tables != nullptr && std::abs (tables->sampleRate - fs) < 0.5) { board.adopt (tables->board); symp.adopt (*tables); }
        applyLiveParams (true);
    }

    // Audio thread, at a block boundary. The table must outlive its use (the owner retires it later).
    void setTables (const InstrumentTables* t)
    {
        tables = t;
        if (t == nullptr) return;
        board.adopt (t->board);
        symp.adopt (*t);
    }
    const InstrumentTables* currentTables() const { return tables; }

    void setParamValues (const float* v)
    {
        std::memcpy (params.data(), v, sizeof (ParamValues));
        paramsDirty = true;
    }
    const ParamValues& paramValues() const { return params; }

    // ---- MIDI ------------------------------------------------------------------
    void noteOn (int midiNote, int velocity)
    {
        if (midiNote < kFirstNote || midiNote > kLastNote || tables == nullptr) return;
        if (velocity <= 0) { noteOff (midiNote); return; }
        const double vel01 = velocity / 127.0;
        const auto& nt = tables->notes[(size_t) (midiNote - kFirstNote)];
        PianoVoice* v = voices.voiceForNote (midiNote);
        if (v != nullptr)
            v->restrike (nt, vel01, params[dynamics], params[velCurve], pedals.soft * params[softPedalDepth]);
        else
        {
            v = voices.allocate (midiNote);
            v->start (nt, vel01, fs, params[dynamics], params[velCurve], pedals.soft * params[softPedalDepth], params[bridgeCoupling], params[stringDecay]);
            v->setBend (bendRatio);
        }
        if (pedals.sostenuto) v->sostenuto = false;    // a new note is not part of the sostenuto set
        symp.setKey (midiNote - kFirstNote, true);
        symp.setHasVoice (midiNote - kFirstNote, true);
        const double level = params[noiseHammer] * params[noiseMaster] * 2.0 * tables->model.noiseLevel;
        noises.trigger (NoiseType::Hammer, nt.zone, (float) nt.pan, level, vel01, nt.hammerNoiseColour);
    }

    void noteOff (int midiNote)
    {
        if (midiNote < kFirstNote || midiNote > kLastNote) return;
        symp.setKey (midiNote - kFirstNote, false);
        if (auto* v = voices.voiceForNote (midiNote))
        {
            if (v->keyDown)
            {
                v->release();
                if (tables != nullptr)
                {
                    const double level = params[noiseKeyRelease] * params[noiseMaster] * 2.0 * tables->model.noiseLevel;
                    noises.trigger (NoiseType::KeyRelease, v->zone, v->pan, level, 0.5 + 0.5 * v->velocity, 0.5);
                }
            }
        }
    }

    void controller (int cc, int value)
    {
        const double v = clampT (value, 0, 127) / 127.0;
        switch (cc)
        {
            case 64:
            {
                const auto ev = pedals.setSustain (v);
                if (ev != Pedals::Event::None && tables != nullptr)
                {
                    const double level = params[noisePedal] * params[noiseMaster] * 2.0 * tables->model.noiseLevel;
                    noises.trigger (ev == Pedals::Event::Down ? NoiseType::PedalDown : NoiseType::PedalUp, 0, -0.6f, level, 1.0, 0.5);
                }
                break;
            }
            case 66:
                if (pedals.setSostenuto (value >= 64))
                {
                    if (pedals.sostenuto)
                    {
                        for (auto& vc : voices.voices) if (vc.inUse) vc.sostenuto = vc.keyDown;
                        symp.captureSostenuto();
                    }
                    else
                    {
                        for (auto& vc : voices.voices) vc.sostenuto = false;
                        symp.releaseSostenuto();
                    }
                }
                break;
            case 67: pedals.setSoft (v); break;
            case 120: panic(); break;
            case 121: pedals.setSustain (0); pedals.setSostenuto (false); pedals.setSoft (0); for (auto& vc : voices.voices) vc.sostenuto = false; symp.releaseSostenuto(); break;
            case 123: allNotesOff(); break;
            default: break;
        }
    }

    void pitchBend (double semitones)
    {
        bendRatio = std::pow (2.0, clampT (semitones, -24.0, 24.0) / 12.0);
    }

    void allNotesOff()
    {
        for (int n = kFirstNote; n <= kLastNote; ++n) { symp.setKey (n - kFirstNote, false); }
        for (auto& v : voices.voices) if (v.inUse) v.release();
    }

    // Everything off. The output fades over the next chunk before the reset, so there is no click.
    void panic() { panicRequested = true; }

    void panicNow()
    {
        panicRequested = false;
        voices.freeAll();
        for (int i = 0; i < kNumNotes; ++i) { symp.setKey (i, false); symp.setHasVoice (i, false); }
        symp.reset();
        board.reset();
        renderer.reset();
        noises.prepare (fs);
        pedals.setSustain (0);
        pedals.setSostenuto (false);
        pedals.setSoft (0);
        std::memset (zoneAccel, 0, sizeof (zoneAccel));
    }

    // ---- rendering -------------------------------------------------------------
    // sendL/sendR (optional) receive the reverb send for an external convolution reverb.
    // Chunks of kChunk samples run at a fixed phase whatever the block size: a call renders the
    // rest of the current chunk, then whole chunks, then the start of the next one, so the
    // control-rate work happens every 32 samples and the output does not depend on how the
    // host slices time.
    void render (float* L, float* R, int n, float* sendL = nullptr, float* sendR = nullptr)
    {
        int pos = 0;
        while (pos < n)
        {
            if (chunkPos == 0) beginChunk();
            const int c = std::min (kChunk - chunkPos, n - pos);
            processSamples (c, L + pos, R != nullptr ? R + pos : nullptr,
                            sendL != nullptr ? sendL + pos : nullptr, sendR != nullptr ? sendR + pos : nullptr);
            chunkPos += c;
            pos += c;
            if (chunkPos >= kChunk) { endChunk(); chunkPos = 0; }
        }
    }

    // ---- stats ------------------------------------------------------------------
    int activeVoices() const { return voices.activeCount(); }
    float takePeak() { const float p = peak; peak = 0; return p; }
    bool sustainDown() const { return pedals.lift > 0.5; }
    double sustainPosition() const { return pedals.sustainPos; }
    bool sostenutoDown() const { return pedals.sostenuto; }
    double softPosition() const { return pedals.soft; }
    int activeNoises() const { return noises.activeCount(); }
    int activePartials() const
    {
        int c = 0;
        for (const auto& v : voices.voices) if (v.inUse) for (int s = 0; s < v.numStrings; ++s) c += v.strings[s].active;
        return c;
    }
    const VoiceManager& voiceManager() const { return voices; }
    VoiceManager& voiceManager() { return voices; }
    const SympatheticBank& sympatheticBank() const { return symp; }
    const Pedals& pedalState() const { return pedals; }
    double sampleRate() const { return fs; }

private:
    void applyLiveParams (bool force)
    {
        if (! paramsDirty && ! force) return;
        paramsDirty = false;
        voices.setPolyphony ((int) std::lround (params[polyphony]));
        pedals.switchMode = params[pedalMode] >= 0.5f;
        pedals.halfPoint = params[halfPedalPoint];
        pedals.setSustain (pedals.sustainPos);   // re-evaluate the lift with the new mapping (no event)
        // 0.5 = the physical coupling; the top of the range stays inside the stability margin of the one-sample loop
        sympGain = (float) (params[stringResonance] * 1.6 * (tables != nullptr ? tables->board.sympathetic : 1.0));
        board.setResonance (params[soundboardResonance]);
        renderer.setMaster (params[master], force);
        renderer.setTone (params[toneBass], params[toneTreble]);
        renderer.setReverb ((int) std::lround (params[reverbType]), params[reverbSize], params[reverbDamp], params[reverbMix], params[reverbPredelay]);
        MicSettings m;
        const double hfBase = tables != nullptr ? tables->board.hfLevel : 1.0;
        switch ((int) std::lround (params[micPerspective]))
        {
            case 1: m.distance = 0.4; m.spread = 0.9; m.flip = false; m.hfLevel = 1.3; m.erLevel = 0.15; break;      // close stereo
            case 2: m.distance = 4.0; m.spread = 0.5; m.flip = true;  m.hfLevel = 0.7; m.erLevel = 0.45; break;      // audience
            case 3: m.distance = 1.5; m.spread = 1.0; m.flip = false; m.hfLevel = 1.0; m.erLevel = 0.35; break;      // wide
            case 4: m.distance = 1.2; m.spread = 0.0; m.flip = false; m.hfLevel = 1.0; m.erLevel = 0.3; break;       // mono
            case 5: m.distance = params[micDistance]; m.spread = params[micSpread]; m.flip = false; m.hfLevel = 1.0;
                    m.erLevel = params[earlyReflections]; break;                                                    // custom
            default: m.distance = 0.8; m.spread = 0.6; m.flip = false; m.hfLevel = 1.0; m.erLevel = 0.25; break;     // player
        }
        m.position = params[micPosition];
        m.width = ((int) std::lround (params[micPerspective]) == 4) ? 0.0 : params[stereoWidth];
        m.hfLevel *= hfBase * std::pow (2.0, params[soundboardBrightness]);
        m.brightness = params[soundboardBrightness];
        renderer.setMics (m);
        micSpreadNow = (float) m.spread;
        micPosShift = (float) m.position;
        outputGain = kOutputScale * (float) ((tables != nullptr ? tables->outputGain : 1.0) * dbToGain (params[presetGain]));
    }

    // Chunk-rate work at the start of each 32-sample chunk
    void beginChunk()
    {
        applyLiveParams (false);
        std::memset (zoneForce, 0, sizeof (zoneForce));
        std::memset (directL, 0, sizeof (directL));
        std::memset (directR, 0, sizeof (directR));
        fading = panicRequested;

        // pedals -> dampers
        const double lift = pedals.lift;
        symp.updateDampers (lift);
        for (auto& v : voices.voices)
        {
            if (! v.inUse) continue;
            v.updateDamper (lift);
            v.setBend (bendRatio);
            if (v.damper.seatedEvent && tables != nullptr)
            {
                const double level = params[noiseDamper] * params[noiseMaster] * 2.0 * tables->model.noiseLevel;
                noises.trigger (NoiseType::Damper, v.zone, v.pan, level, clampT (v.bridgeForceAtDamping / 4.0, 0.0, 1.0), 0.5);
            }
        }
        renderer.beginChunk();
    }

    // n samples of the current chunk, starting at chunkPos
    void processSamples (int n, float* L, float* R, float* sendL, float* sendR)
    {
        float outF[kCoupleStep];
        // coupling steps: voices and the bank write forces, the board answers with accelerations
        const bool anyVoices = voices.activeCount() > 0;
        for (int pos = chunkPos; pos < chunkPos + n; pos += kCoupleStep)
        {
            const int c = std::min (kCoupleStep, chunkPos + n - pos);
            if (anyVoices) for (auto& v : voices.voices)
            {
                if (! v.inUse) continue;
                v.processSamples (c, zoneAccel[v.zone], sympGain, outF);
                const float pan = clampT (v.pan * micSpreadNow + micPosShift * 0.5f, -1.0f, 1.0f);
                const float a = (pan + 1.0f) * 0.25f * (float) kPi;
                const float gl = std::cos (a), gr = std::sin (a);
                float* zf = zoneForce[v.zone] + pos;
                for (int i = 0; i < c; ++i)
                {
                    const float f = outF[i] * v.gain;
                    zf[i] += f;
                    directL[pos + i] += f * gl;
                    directR[pos + i] += f * gr;
                }
            }
            symp.processSamples (c, zoneAccel, sympGain, zoneForce, pos);
            noises.processSamples (c, zoneForce, directL + pos, directR + pos, pos);
            board.processSamples (c, zoneForce, boardL + pos, boardR + pos, zoneAccel, pos);
        }

        float sl[kChunk], sr[kChunk];
        float* outR = R != nullptr ? R : sr;   // mono host: render to a scratch right channel
        renderer.processSamples (n, boardL + chunkPos, boardR + chunkPos, directL + chunkPos, directR + chunkPos, outputGain, L, outR,
                                 sendL != nullptr ? sendL : sl, sendR != nullptr ? sendR : sr);
        if (R == nullptr) for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + outR[i]);

        if (fading)   // a panic: fade this chunk out, the reset follows at its end
        {
            for (int i = 0; i < n; ++i)
            {
                const float g = 1.0f - (float) (chunkPos + i + 1) / (float) kChunk;
                L[i] *= g;
                if (R != nullptr) R[i] *= g;
                if (sendL != nullptr) sendL[i] *= g;
                if (sendR != nullptr) sendR[i] *= g;
            }
        }

        float pk = peak;
        bool finite = true;
        for (int i = 0; i < n; ++i)
        {
            const float a = std::max (std::abs (L[i]), R != nullptr ? std::abs (R[i]) : 0.0f);
            if (a > pk) pk = a;
            if (! std::isfinite (a)) finite = false;
        }
        peak = pk;
        if (! finite)
        {
            for (int i = 0; i < n; ++i) { L[i] = 0; if (R != nullptr) R[i] = 0; }
            peak = 0;
            panicRequested = true;
            fading = true;
        }
    }

    // Chunk-rate work at the end of each chunk
    void endChunk()
    {
        for (auto& v : voices.voices)
        {
            if (! v.inUse) continue;
            v.endChunk();
            if (v.finished())
            {
                symp.setHasVoice (v.note - kFirstNote, false);
                voices.free (v);
            }
        }
        symp.endChunk();
        board.endChunk();
        if (fading) { panicNow(); fading = false; }
    }

    double fs = 48000;
    ParamValues params {};
    bool paramsDirty = true;
    const InstrumentTables* tables = nullptr;
    VoiceManager voices;
    Soundboard board;
    SympatheticBank symp;
    NoisePool noises;
    SpatialRenderer renderer;
    Pedals pedals;
    float zoneAccel[kNumZones][kCoupleStep] {};
    float sympGain = 1.0f, micSpreadNow = 0.6f, micPosShift = 0.0f, outputGain = kOutputScale;
    double bendRatio = 1.0;
    float peak = 0;
    bool panicRequested = false, fading = false;
    int chunkPos = 0;                                  // samples of the current chunk already rendered
    float zoneForce[kNumZones][kChunk] {};             // the chunk's buses (persist across render() calls)
    float directL[kChunk] {}, directR[kChunk] {}, boardL[kChunk] {}, boardR[kChunk] {};
};
} // namespace resonique
