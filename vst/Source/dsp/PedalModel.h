#pragma once
// ============================================================================
//  Pedals: sustain (CC64, continuous or switch, with a half-pedal point),
//  sostenuto (CC66) and soft / una corda (CC67). The sustain pedal turns into
//  a damper *lift* that the voices and the sympathetic bank apply as pressure.
// ============================================================================
#include "Common.h"

namespace resonique
{
struct Pedals
{
    double sustainPos = 0;    // raw pedal position 0..1
    double lift = 0;          // effective damper lift 0..1
    double soft = 0;          // 0..1
    bool sostenuto = false;
    bool switchMode = false;
    double halfPoint = 0.5;

    enum class Event { None, Down, Up };

    Event setSustain (double pos)
    {
        pos = clampT (pos, 0.0, 1.0);
        const bool wasDown = lift >= 0.5;
        sustainPos = pos;
        if (switchMode) lift = pos >= 0.5 ? 1.0 : 0.0;
        else
        {
            const double a = halfPoint - 0.25, b = halfPoint + 0.25;
            const double t = clampT ((pos - a) / (b - a), 0.0, 1.0);
            lift = t * t * (3.0 - 2.0 * t);
        }
        const bool down = lift >= 0.5;
        if (down && ! wasDown) return Event::Down;
        if (! down && wasDown) return Event::Up;
        return Event::None;
    }

    // returns true when the state changed
    bool setSostenuto (bool on) { const bool c = on != sostenuto; sostenuto = on; return c; }
    void setSoft (double v) { soft = clampT (v, 0.0, 1.0); }
};
} // namespace resonique
