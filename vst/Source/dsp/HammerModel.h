#pragma once
// ============================================================================
//  The felt hammer: a mass thrown at the strings with a nonlinear, hysteretic
//  contact force (Hunt-Crossley form of the Hertz / Stulov felt law):
//      F = K xi^p (1 + lambda d(xi)/dt),  xi = hammer position - string position
//  Velocity changes the pulse width, so the spectrum, not only the level.
//  The contact is integrated in sub-steps (about 192 kHz) with the string
//  displacement extrapolated linearly, so the result does not depend on the
//  audio sample rate.
// ============================================================================
#include "StringModel.h"

namespace resonique
{
struct Hammer
{
    bool active = false;
    double eta = 0;          // hammer position relative to the string's rest line (m, + = into the string)
    double v = 0;            // velocity (m/s)
    double mass = 0.008;
    double K = 4e9, p = 2.5, lambda = 0.6;
    double dt = 1.0 / 192000.0;   // sub-step
    int subSteps = 4;
    int samples = 0, maxSamples = 1440;
    double lastXi[kMaxStrings] { 0, 0, 0 };
    double contactSamples = 0;    // diagnostics: audio samples in contact
    bool wasInContact = false;

    // vel01 in (0, 1]
    static double velocityToSpeed (double vel01, double velPP, double velFF, double dynamics, double velCurve)
    {
        const double e = std::pow (2.0, -velCurve * 1.2);
        const double x = std::pow (clampT (vel01, 1.0 / 127.0, 1.0), e);
        const double ratio = std::pow (velFF / velPP, 0.5 + dynamics);
        // keep ff anchored: the dynamics parameter widens the range downwards
        const double top = velFF * std::pow (0.5 + dynamics, 0.35);
        return top / std::pow (ratio, 1.0 - x);
    }

    void strike (const NoteTable& nt, double vel01, double fs, double dynamics, double velCurve, double soft)
    {
        subSteps = std::max (1, (int) std::ceil (192000.0 / fs));
        dt = 1.0 / (fs * subSteps);
        mass = nt.hammerMass;
        K = nt.feltK * (1.0 - 0.5 * soft);
        p = nt.feltP;
        lambda = nt.feltLambda;
        v = velocityToSpeed (vel01, nt.velPP, nt.velFF, dynamics, velCurve) * (1.0 - 0.25 * soft);
        eta = 0.0;
        samples = 0;
        maxSamples = (int) (0.03 * fs);
        active = true;
        contactSamples = 0;
        wasInContact = false;
        for (auto& x : lastXi) x = 0;
    }

    // Contact force against one string whose displacement at the strike point is y (m), for one sub-step.
    inline double force (int stringIndex, double y)
    {
        const double xi = eta - y;
        const double xiDot = (xi - lastXi[stringIndex]) / dt;
        lastXi[stringIndex] = xi;
        if (xi <= 0.0) return 0.0;
        const double f = K * std::pow (xi, p) * (1.0 + lambda * xiDot);
        return f > 0.0 ? f : 0.0;
    }

    // Advance the hammer by one sub-step given the total force the strings push back with.
    inline void advanceSub (double totalForce)
    {
        v -= totalForce / mass * dt;
        eta += v * dt;
    }

    // Called once per audio sample after the sub-steps.
    inline void endSample (bool contact)
    {
        ++samples;
        if (contact) { contactSamples += 1; wasInContact = true; }
        // gone: moving away and clear of the strings, or out of time
        if ((v < 0.0 && eta < -0.5e-3) || samples > maxSamples)
            active = false;
    }
};
} // namespace resonique
