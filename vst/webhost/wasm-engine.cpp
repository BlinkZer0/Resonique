// ============================================================================
//  The WebAssembly build of the Resonique engine: a flat C API over
//  PianoEngine.h + dsp/*.h (the same source the plugin compiles), called from
//  web-host.js. Nothing JUCE, nothing JavaScript-specific in here.
//
//  Build (vst/webhost/build-web.ps1 does this):
//    em++ wasm-engine.cpp ../Source/dsp/Kernels*.cpp -O3 -std=c++20 -I../Source -msimd128 -ffast-math
//         -s STANDALONE_WASM=1 --no-entry -s EXPORTED_FUNCTIONS=[...] -o resonique.wasm
//
//  One Instance = one engine + two table buffers. The page runs two instances:
//  one in the AudioWorklet that makes the sound, one on the main thread that
//  builds the note tables (a few milliseconds) and hands the bytes over, so
//  the audio thread never stalls on a physical-parameter change.
// ============================================================================
#include <cstdlib>
#include <cstring>
#include <string>
#include "PianoEngine.h"
#include "dsp/ModelBuilder.h"

using namespace resonique;

namespace
{
constexpr int kMaxFrames = 512;

struct Instance
{
    PianoEngine engine;
    ParamValues params {};
    TuningSettings tuning;
    std::string scalaDescription;
    InstrumentTables* tables[2] { nullptr, nullptr };
    int active = -1;
    double sampleRate = 48000;
    uint32_t builds = 0;
    float outL[kMaxFrames] {}, outR[kMaxFrames] {};
    double detune[kNumNotes] {};
};
} // namespace

#define RQ_EXPORT extern "C" __attribute__((used, visibility ("default")))

RQ_EXPORT void* rq_alloc (int bytes) { return std::malloc ((size_t) bytes); }
RQ_EXPORT void rq_free (void* p) { std::free (p); }

RQ_EXPORT int rq_num_params() { return kNumParams; }
RQ_EXPORT float rq_param_default (int i) { return i >= 0 && i < kNumParams ? (float) paramDef (i).def : 0.0f; }
RQ_EXPORT int rq_max_frames() { return kMaxFrames; }
RQ_EXPORT int rq_tables_bytes() { return (int) sizeof (InstrumentTables); }
RQ_EXPORT int rq_num_notes() { return kNumNotes; }

RQ_EXPORT Instance* rq_create (double sampleRate)
{
    auto* in = new Instance();
    in->sampleRate = sampleRate;
    in->params = defaultParamValues();
    in->tables[0] = new InstrumentTables();
    in->tables[1] = new InstrumentTables();
    *in->tables[0] = *ModelBuilder::build (in->params, in->tuning, sampleRate, ++in->builds);
    in->active = 0;
    in->engine.setParamValues (in->params.data());
    in->engine.prepare (sampleRate);
    in->engine.setTables (in->tables[0]);
    return in;
}
RQ_EXPORT void rq_destroy (Instance* in)
{
    delete in->tables[0];
    delete in->tables[1];
    delete in;
}

// ---- parameters: write into the array, then apply (once per block) ---------
RQ_EXPORT float* rq_params (Instance* in) { return in->params.data(); }
RQ_EXPORT void rq_apply_params (Instance* in) { in->engine.setParamValues (in->params.data()); }

// ---- tables: build on the main thread, copy the bytes into the worklet, adopt there ----
// builds the inactive buffer from the current parameters + tuning; returns its bytes
RQ_EXPORT unsigned char* rq_build_tables (Instance* in)
{
    const int target = 1 - in->active;
    *in->tables[target] = *ModelBuilder::build (in->params, in->tuning, in->sampleRate, ++in->builds);
    return reinterpret_cast<unsigned char*> (in->tables[target]);
}
// the inactive buffer, for a copy from another instance
RQ_EXPORT unsigned char* rq_table_target (Instance* in) { return reinterpret_cast<unsigned char*> (in->tables[1 - in->active]); }
// switch to the inactive buffer (after rq_build_tables or after the bytes were written)
RQ_EXPORT void rq_adopt_tables (Instance* in)
{
    in->active = 1 - in->active;
    in->engine.setTables (in->tables[in->active]);
}

// ---- audio ------------------------------------------------------------------
RQ_EXPORT float* rq_out_l (Instance* in) { return in->outL; }
RQ_EXPORT float* rq_out_r (Instance* in) { return in->outR; }
RQ_EXPORT void rq_render (Instance* in, int frames)
{
    if (frames > kMaxFrames) frames = kMaxFrames;
    in->engine.render (in->outL, in->outR, frames);
}

// ---- MIDI -------------------------------------------------------------------
RQ_EXPORT void rq_note_on (Instance* in, int note, int vel) { in->engine.noteOn (note, vel); }
RQ_EXPORT void rq_note_off (Instance* in, int note) { in->engine.noteOff (note); }
RQ_EXPORT void rq_cc (Instance* in, int cc, int value) { in->engine.controller (cc, value); }
RQ_EXPORT void rq_bend (Instance* in, double semitones) { in->engine.pitchBend (semitones); }
RQ_EXPORT void rq_all_notes_off (Instance* in) { in->engine.allNotesOff(); }
RQ_EXPORT void rq_panic (Instance* in) { in->engine.panic(); }

// ---- stats ------------------------------------------------------------------
RQ_EXPORT int rq_active_voices (Instance* in) { return in->engine.activeVoices(); }
RQ_EXPORT int rq_active_partials (Instance* in) { return in->engine.activePartials(); }
RQ_EXPORT float rq_take_peak (Instance* in) { return in->engine.takePeak(); }
RQ_EXPORT float rq_sustain (Instance* in) { return (float) in->engine.sustainPosition(); }
RQ_EXPORT int rq_sostenuto (Instance* in) { return in->engine.sostenutoDown() ? 1 : 0; }
RQ_EXPORT float rq_soft (Instance* in) { return (float) in->engine.softPosition(); }

// ---- tuning state (per-note detune, Scala); the temperament / pitch / stretch are parameters ----
RQ_EXPORT void rq_set_detune (Instance* in, int noteIndex, double cents)
{
    if (noteIndex >= 0 && noteIndex < kNumNotes) in->tuning.noteDetune[(size_t) noteIndex] = clampT (cents, -100.0, 100.0);
}
RQ_EXPORT void rq_reset_detune (Instance* in) { for (auto& d : in->tuning.noteDetune) d = 0; }
RQ_EXPORT double* rq_detune (Instance* in)
{
    for (int i = 0; i < kNumNotes; ++i) in->detune[i] = in->tuning.noteDetune[(size_t) i];
    return in->detune;
}
// returns the number of notes in the scale, 0 on error
RQ_EXPORT int rq_set_scala (Instance* in, const char* text)
{
    ScalaScale s;
    std::string err;
    if (! ScalaScale::parse (std::string (text), s, err)) return 0;
    in->tuning.scala = s;
    in->scalaDescription = s.description;
    return (int) s.cents.size();
}
RQ_EXPORT void rq_clear_scala (Instance* in) { in->tuning.scala = {}; in->scalaDescription.clear(); }
RQ_EXPORT const char* rq_scala_description (Instance* in) { return in->scalaDescription.c_str(); }
RQ_EXPORT int rq_scala_notes (Instance* in) { return in->tuning.scala.valid() ? (int) in->tuning.scala.cents.size() : 0; }
