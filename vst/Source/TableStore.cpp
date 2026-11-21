#include "TableStore.h"

namespace resonique
{
TableStore::TableStore() : juce::Thread ("Resonique tables")
{
    requestValues = defaultParamValues();
    startThread (juce::Thread::Priority::normal);
}

TableStore::~TableStore()
{
    signalThreadShouldExit();
    notify();
    stopThread (5000);
    delete incoming.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete parked.exchange (nullptr);
    delete activeTable;
    activeTable = nullptr;
}

//==============================================================================
void TableStore::setSampleRate (double fs)
{
    sampleRate = fs;
    ParamValues v;
    {
        const juce::ScopedLock sl (requestLock);
        requestRate = fs;
        v = requestValues;
    }
    buildNow (v);
}

void TableStore::requestBuild (const ParamValues& values)
{
    {
        const juce::ScopedLock sl (requestLock);
        requestValues = values;
        requestTuning = tuning;
        pending = true;
        latestBuildId.store (nextId++);
    }
    notify();
}

void TableStore::buildNow (const ParamValues& values)
{
    uint32_t id;
    {
        const juce::ScopedLock sl (requestLock);
        requestValues = values;
        requestTuning = tuning;
        pending = false;
        id = nextId++;
        latestBuildId.store (id);
    }
    publish (ModelBuilder::build (values, tuning, sampleRate, id));
}

void TableStore::run()
{
    while (! threadShouldExit())
    {
        ParamValues v;
        TuningSettings t;
        double fs;
        uint32_t id;
        bool work;
        {
            const juce::ScopedLock sl (requestLock);
            work = pending;
            pending = false;
            v = requestValues;
            t = requestTuning;
            fs = requestRate;
            id = latestBuildId.load();
        }
        if (work)
        {
            busy = true;
            publish (ModelBuilder::build (v, t, fs, id));
            busy = false;
            continue;   // check for a newer request before sleeping
        }
        wait (-1);
    }
}

void TableStore::publish (std::unique_ptr<InstrumentTables> t)
{
    // Only one unread table is kept for the audio thread; an older unread one is replaced.
    // Whatever gets replaced is parked for the message thread to free.
    auto* old = incoming.exchange (t.release());
    if (old != nullptr)
    {
        auto* p = parked.exchange (old);
        delete p;   // parked is only ever touched by the worker + message thread (collectRetired), never the audio thread
    }
}

void TableStore::collectRetired()
{
    delete retired.exchange (nullptr);
    delete parked.exchange (nullptr);
}

const InstrumentTables* TableStore::takeIncoming()
{
    if (retired.load() != nullptr) return nullptr;        // wait until the message thread freed the last one
    if (auto* t = incoming.exchange (nullptr))
    {
        retired.store (activeTable);
        activeTable = t;
        publishedBuildId.store (t->buildId);
        return t;
    }
    return nullptr;
}

//==============================================================================
bool TableStore::setScala (const juce::String& text, juce::String& error)
{
    ScalaScale s;
    std::string err;
    if (! ScalaScale::parse (text.toStdString(), s, err)) { error = juce::String (err); return false; }
    tuning.scala = s;
    scalaText = text;
    ++tuningVersion;
    return true;
}

void TableStore::clearScala()
{
    tuning.scala = {};
    scalaText = {};
    ++tuningVersion;
}

void TableStore::setNoteDetune (int noteIndex, double cents)
{
    if (noteIndex < 0 || noteIndex >= kNumNotes) return;
    tuning.noteDetune[(size_t) noteIndex] = clampT (cents, -100.0, 100.0);
    ++tuningVersion;
}

void TableStore::resetDetune()
{
    for (auto& d : tuning.noteDetune) d = 0;
    ++tuningVersion;
}

juce::var TableStore::tuningToJson() const
{
    auto* o = new juce::DynamicObject();
    bool any = false;
    juce::Array<juce::var> det;
    for (const auto& d : tuning.noteDetune) { det.add (d); any = any || std::abs (d) > 1e-6; }
    if (any) o->setProperty ("detune", det);
    if (scalaText.isNotEmpty()) o->setProperty ("scala", scalaText);
    return juce::var (o);
}

void TableStore::tuningFromJson (const juce::var& v)
{
    for (auto& d : tuning.noteDetune) d = 0;
    tuning.scala = {};
    scalaText = {};
    if (v.isObject())
    {
        if (const auto* arr = v["detune"].getArray())
            for (int i = 0; i < juce::jmin (kNumNotes, arr->size()); ++i)
                tuning.noteDetune[(size_t) i] = clampT ((double) (*arr)[i], -100.0, 100.0);
        const auto s = v["scala"].toString();
        if (s.isNotEmpty())
        {
            juce::String err;
            setScala (s, err);
        }
    }
    ++tuningVersion;
}
} // namespace resonique
