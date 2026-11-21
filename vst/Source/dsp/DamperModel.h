#pragma once
// ============================================================================
//  The damper of one note: a felt block whose pressure on the strings sets an
//  extra, frequency-dependent loss. Pressure follows the key, the sustain pedal
//  position (half-pedalling), sostenuto and the mechanical settling time, so
//  partial damping and re-pedalling fall out of the model.
// ============================================================================
#include "Common.h"

namespace resonique
{
struct Damper
{
    bool hasDamper = true;
    double pressure = 0;      // 0 = fully raised, 1 = resting on the strings
    double target = 0;
    double dropCoef = 0.2;    // per chunk, when falling onto the strings
    double liftCoef = 0.5;    // per chunk, when being raised
    bool seatedEvent = false; // set for one chunk when the damper first lands (noise)

    void prepare (double fs, double dropMs)
    {
        const double chunksPerSecond = fs / kChunk;
        dropCoef = 1.0 - std::exp (-1.0 / (std::max (1.0, dropMs) * 1e-3 * chunksPerSecond));
        liftCoef = 1.0 - std::exp (-1.0 / (8e-3 * chunksPerSecond));
    }

    // keyHeld: the key is down; lift: effective damper lift from the pedals (0..1)
    void update (bool keyHeld, bool sostenuto, double lift)
    {
        seatedEvent = false;
        if (! hasDamper) { pressure = target = 0; return; }
        target = (keyHeld || sostenuto) ? 0.0 : std::pow (clampT (1.0 - lift, 0.0, 1.0), 2.5);   // a half-lifted damper barely touches
        const double before = pressure;
        const double c = target > pressure ? dropCoef : liftCoef;
        pressure += (target - pressure) * c;
        if (std::abs (target - pressure) < 1e-4) pressure = target;
        if (before < 0.5 && pressure >= 0.5) seatedEvent = true;
    }

    bool resting() const { return pressure > 1e-4; }
};
} // namespace resonique
