#pragma once
// ============================================================================
//  Voice allocation. A key that is struck again while it rings keeps its voice
//  (the strings keep vibrating, the hammer adds energy). When every voice is in
//  use the quietest released voice goes first, then the quietest of all.
// ============================================================================
#include "PianoVoice.h"

namespace resonique
{
class VoiceManager
{
public:
    VoiceManager()
    {
        for (auto& v : noteToVoice) v = -1;
    }

    void setPolyphony (int n) { polyphony = clampT (n, 1, kMaxVoices); }
    int getPolyphony() const { return polyphony; }

    PianoVoice* voiceForNote (int midiNote)
    {
        const int i = noteToVoice[(size_t) clampT (midiNote, 0, 127)];
        return i >= 0 && voices[(size_t) i].inUse ? &voices[(size_t) i] : nullptr;
    }

    // Returns a voice to start the note on (free, or stolen). Never nullptr.
    PianoVoice* allocate (int midiNote)
    {
        int best = -1;
        int used = 0;
        for (int i = 0; i < kMaxVoices; ++i) used += voices[(size_t) i].inUse ? 1 : 0;
        if (used < polyphony)
            for (int i = 0; i < kMaxVoices; ++i)
                if (! voices[(size_t) i].inUse) { best = i; break; }
        if (best < 0)
        {
            // steal: released & quietest first, otherwise the quietest
            float bestScore = 1e30f;
            for (int i = 0; i < kMaxVoices; ++i)
            {
                const auto& v = voices[(size_t) i];
                if (! v.inUse) continue;
                const float score = v.level() * (v.keyDown ? 1000.0f : 1.0f) + (v.hammer.active ? 1e6f : 0.0f);
                if (score < bestScore) { bestScore = score; best = i; }
            }
        }
        auto& v = voices[(size_t) best];
        if (v.inUse && v.note >= 0 && v.note < 128 && noteToVoice[(size_t) v.note] == best) noteToVoice[(size_t) v.note] = -1;
        noteToVoice[(size_t) clampT (midiNote, 0, 127)] = best;
        return &v;
    }

    void free (PianoVoice& v)
    {
        v.inUse = false;
        v.keyDown = false;
        v.sostenuto = false;
        if (v.note >= 0 && v.note < 128 && noteToVoice[(size_t) v.note] == (int) (&v - voices)) noteToVoice[(size_t) v.note] = -1;
        v.note = -1;
    }

    void freeAll() { for (auto& v : voices) if (v.inUse) free (v); }

    int activeCount() const { int c = 0; for (const auto& v : voices) c += v.inUse ? 1 : 0; return c; }

    PianoVoice voices[kMaxVoices];

private:
    int noteToVoice[128];
    int polyphony = 64;
};
} // namespace resonique
