#pragma once
// ============================================================================
//  TableStore: owns the instrument tables. Expensive physics (every partial of
//  every string of every key, the soundboard admittance) is computed on a
//  worker thread whenever a table parameter, the tuning or the sample rate
//  changes, and handed to the audio thread lock-free (atomic incoming /
//  retired pointers, the same handoff the family uses for samples and models).
//  Also owns the non-parameter tuning state (per-note detune, Scala scale).
// ============================================================================
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include "dsp/ModelBuilder.h"

namespace resonique
{
class TableStore : private juce::Thread
{
public:
    TableStore();
    ~TableStore() override;

    // Message thread -------------------------------------------------------------
    void setSampleRate (double fs);                       // rebuilds synchronously (prepareToPlay)
    void requestBuild (const ParamValues& values);        // asynchronous; the latest request wins
    void buildNow (const ParamValues& values);            // synchronous (tests, prepare)
    void collectRetired();                                // frees what the audio thread retired

    // Tuning state (message thread). Changing it triggers no build by itself: call requestBuild.
    TuningSettings tuning;
    juce::String scalaText;                               // the imported .scl (kept for presets / state)
    bool setScala (const juce::String& text, juce::String& error);
    void clearScala();
    void setNoteDetune (int noteIndex, double cents);
    void resetDetune();
    juce::var tuningToJson() const;                       // { "detune": [...], "scala": "..." }
    void tuningFromJson (const juce::var&);               // missing parts -> defaults

    std::atomic<int> tuningVersion { 0 };
    std::atomic<uint32_t> latestBuildId { 0 };            // id of the most recent request
    std::atomic<uint32_t> publishedBuildId { 0 };         // id of the last table handed over
    bool building() const { return busy.load(); }

    // Audio thread ---------------------------------------------------------------
    // The latest table, or nullptr if nothing new since the last call. The old one is retired.
    const InstrumentTables* takeIncoming();
    const InstrumentTables* active() const { return activeTable; }

private:
    void run() override;
    void publish (std::unique_ptr<InstrumentTables>);

    juce::CriticalSection requestLock;
    ParamValues requestValues {};
    TuningSettings requestTuning;
    double requestRate = 48000.0;
    bool pending = false;
    uint32_t nextId = 1;
    std::atomic<bool> busy { false };

    std::atomic<InstrumentTables*> incoming { nullptr };
    std::atomic<InstrumentTables*> retired { nullptr };
    std::atomic<InstrumentTables*> parked { nullptr };    // a build that arrived while `incoming` was still unread
    InstrumentTables* activeTable = nullptr;              // audio thread only
    double sampleRate = 48000.0;
};
} // namespace resonique
