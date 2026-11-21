# Resonique: the acoustic model

This is what the engine computes, where the numbers come from, and how the automated tests check it. Everything is
this repository's own implementation of published piano acoustics; nothing is sampled.

## 1. Strings: a stiff-string modal model

Each string is a bank of up to 128 **modes** (partials). Partial *n* of a string with fundamental *f₀* and
inharmonicity coefficient *B* sits at

    f_n = n · f₀ · √((1 + B n²) / (1 + B))

The usual textbook form is `n f₀ √(1 + Bn²)`; the normalisation by `√(1 + B)` keeps partial 1 exactly on the
intended pitch so the tuning stays correct (the test `tuningAndPartials` checks both the pitch and the partial
positions). *B* is computed from the string's physics, `B = π³ E d⁴ / (64 T L²)` with Young's modulus *E* = 2·10¹¹ Pa,
the steel core diameter *d*, the tension *T* and the speaking length *L* that the instrument model gives for every
key (concert grand: 1.98 m / 1250 N at A0, 0.63 m / 760 N at C4, 0.052 m / 700 N at C8). The result runs from about
9·10⁻⁵ at A0 to 2·10⁻² at C8, which is the published range for grands (Fletcher & Rossing).

Every mode is a **rotation (coupled-form) resonator**: the state is `(p, q)` = (displacement, velocity / ω) and each
sample rotates it by the angle `ω·dt` while scaling it by `e^(−σ·dt)`:

    p' = c·p + s·q,   q' = c·q − s·p,   c = e^(−σ dt) cos(ω dt),  s = e^(−σ dt) sin(ω dt)

Four multiplies and two adds per partial per sample, no trigonometry in the audio loop, exact frequency and decay,
and (unlike a direct-form biquad) no precision problem in `float` at 27.5 Hz: the DSP test runs a 27.5 Hz mode for
ten seconds and checks its amplitude. The coefficients are precomputed per sample rate in the note tables.

**Losses.** The decay rate of partial *n* is

    σ_n = σ₀ + σ₁·f + σ₂·f² + T·Y(f) / L

The first three terms are air and internal (viscoelastic) losses with register-dependent coefficients; the last one
is the energy the string loses into the bridge, derived from the soundboard's driving-point admittance *Y(f)* at that
string's bridge zone (section 4). `T·Y/L` is what makes the decay register-dependent without any hand tuning: long,
heavily tensioned bass strings lose little per cycle, short treble strings lose a lot. The resulting T60 of the
fundamental is about 15 s at A0, 9 s at C3, 6 s at C4, 1.5 s at C6 and 0.35 s at C8 in the tables, and the rendered
audio is measured against those ranges in `decayTests`. Higher partials die faster, as the test checks.

**Partial count.** A string carries every partial below `min(0.45·fs, 20 kHz)`, capped at 128: all 128 for the bass
(A0's 128th partial is at 3.5 kHz), 52 at C4, 7 at C7, 4 at C8 at 48 kHz. Higher sample rates get more partials in the
treble. While a note rings, the top partial is dropped whenever its force contribution falls below −150 dB re 1 N,
so an old note costs a fraction of a fresh one.

## 2. Hammer: a nonlinear, hysteretic felt contact

The hammer is a mass (11 g at A0 to 3.8 g at C8) thrown at the strings with a velocity that follows the MIDI
velocity (about 0.25 m/s at velocity 1 to 6 m/s at 127; the Dynamics and Velocity Curve parameters shape the map).
During contact the felt compression ξ = η − y(x_h) (hammer position minus the string's displacement under it)
produces the force

    F = K · ξ^p · (1 + λ · dξ/dt)

the Hunt–Crossley form of the Hertz / Stulov felt law. *K* and *p* come from the instrument model (concert grand:
K ≈ 3·10⁸ N/mᵖ, p ≈ 2.3 at A0 up to 1.2·10¹² N/mᵖ, p ≈ 3.1 at C8, after Chaigne & Askenfelt), and the Hammer Hardness
and Felt parameters scale them. The hammer and the strings are integrated explicitly per sample (symplectic Euler for
the hammer, force injection into every mode of every unison string through its mode shape at the strike point).

Because the law is nonlinear, harder blows compress the felt further and the contact gets **shorter**: the tables
give about 5.5 ms at C1 and 0.7 ms at G#7 at velocity 64, and 4.8 ms / 0.2 ms at velocity 120 (`hammerTests`). A
shorter force pulse has more high-frequency content, so velocity changes the spectrum, not just the level: the
`velocityTests` check that both the RMS level and the spectral centroid rise monotonically from velocity 20 to 127.

The mode shape at the strike point, `sin(nπ x_h / L)`, suppresses the partials with a node there (the 7th–9th for the
usual 1/8 strike ratio), and the felt's finite contact width rolls the highest partials off. The soft pedal (una
corda) lowers *K* and the hammer speed and only brushes the third string.

## 3. Unison strings and the local bridge

Notes have one, two or three strings (concert grand: singles to E1, pairs to B2, triples from C3), each with its own
fundamental: the Unison Detune parameter sets the spread in cents and a deterministic per-note randomisation keeps
the beating rates different from key to key. The strings of a note share a **local bridge**: a small mass with a
resistive termination. Each string pushes the bridge with `F_b = T·Σ (nπ/L)(−1)ⁿ p_n` (its slope at the end, with
the static term `T·u/L`) and the bridge's acceleration goes back into the low modes of every string with the
projection `(2/(nπ))(−1)ⁿ` that the modal expansion of a moving boundary gives. This is Weinreich's coupled-string
mechanism, so the beating and the two-stage decay of real unisons fall out of the physics (`unisonTests` measures
the envelope ripple with and without detune). The bridge mass is raised, when necessary, to `0.4·Σ μL` so the
coupled kinetic energy stays positive definite and the explicit integration stays stable (`rateIndependenceAndStability`
hammers all 88 keys with every coupling at maximum for 12 s).

## 4. Soundboard: a reduced-order modal model

The soundboard is one shared bank of 96 modes between 48 Hz and about 3.8 kHz (first mode 48 Hz on the concert
grand, 78 Hz on the upright; the spacing grows with frequency and is jittered deterministically per instrument).
Each mode has a modal mass (≈1.6 kg), a Q between 32 and 18 (falling with frequency), a **shape across eight bridge
zones** and two **radiation weights** (left / right) that fade in below a corner frequency (dipole cancellation).

Every voice's bridge force is summed into its zone; the modes integrate the zone forces (through their shapes),
and their velocities radiate to the two output channels and move the bridge zones. The zone *accelerations* are
fed back into every undamped string (section 5), and the real part of the modal admittance

    Re Y(ω) = Σ ψ_m² / M_m · 2σ_m ω² / ((ω_m² − ω²)² + 4σ_m² ω²)  +  Y_bg(f)

(plus a background term for the dense modes above the bank) is what the string losses use. The board therefore
shapes the attack, the decay and the spatial image of every note, rather than colouring the output afterwards; the
`resonanceAndPedalTests` check that changing the board's resonance changes the spectrum of a rendered note by more
than 1 dB on average. The dense high-frequency region above the modal bank is handled by a direct radiation path
(the per-voice bridge forces, high-passed), whose level the Soundboard Brightness parameter and the microphone
perspective set.

## 5. Sympathetic resonance and the pedals

Every key also has a **light copy** of its first string (12 partials) in the sympathetic bank, laid out as one
structure-of-arrays bank grouped by zone. When a key's damper is off (key held, sostenuto, or the sustain pedal
lifting the dampers) and no full voice is playing that key, its light string picks up the zone acceleration and
rings; otherwise its damper loss holds it down. Energy arrives only where the soundboard moves at the string's
partials, so harmonically related keys respond and unrelated ones do not: the test strikes C4 with the pedal down
and checks that the C3 copy carries at least 2.5× the energy of the C#3 copy (and nothing with the pedal up).

Full voices also receive the zone acceleration, so held and sustained notes keep exchanging energy through the
board. The sustain pedal (CC64) is **continuous**: between the half-pedal point ± 0.25 the damper lift goes from 0 to 1
(smoothstep) and the damper pressure follows it, so partial damping, half-pedalling and re-pedalling work on the
actual string loss (`σ_damp,n = damperRate(note) · pressure · (0.4 + 0.6·min(1, f/600))`). A switch mode is available.
Sostenuto (CC66) captures the keys down at the moment it is pressed. The top 20 keys have no dampers, as on a grand.

## 6. Mechanical noises

Synthesised, not sampled: the key bottoming out and the shank knock (a low-passed burst plus a 70–120 Hz damped
sine, growing with velocity), the key return, the damper felt landing on a moving string (scaled by how much the
string was moving), and the pedal mechanism. Most of the energy is injected into the soundboard through the zone
forces, so it takes on the body's colour; a small part goes straight to the output as the "air" path.

## 7. Microphones and the room

The soundboard's two radiation channels are the core of the stereo image; the direct path is panned per key along
the bridge. The perspective (Player, Close, Audience, Wide, Mono, Custom) sets distance (direct level, air
absorption, room share), pair spread, orientation (the audience hears the bass on the right), the dense-mode
radiation level and the early-reflection level. Then six cross-fed early-reflection taps, an 8-line Householder
feedback delay network (Room / Chamber / Hall / Stage), or a user impulse response through JUCE's convolution, the
tone shelves, the master and a soft clipper that is linear below −1.9 dBFS.

## 8. Tuning

Equal temperament plus seven historical temperaments (cent tables for the twelve pitch classes, rotated to a root),
Scala `.scl` import (cents and ratios, any period and number of notes; the scale's root is in the C4 octave),
concert pitch 415–466 Hz, a Railsback-style stretch curve (−28 ct at A0, +33 ct at C8 when fully on; flat between
A3 and C5) and per-note detune from the tuning editor. All of it feeds the note tables, so the partial series follows.

## 9. Validation

`ResoniqueTests --acoustic` renders audio through the whole engine and measures it: fundamental pitch within 3 cents
on eight notes across the compass, the first eight partials of C4 within 0.4 % of the stiff-string formula, T60 of
the fundamental in register-dependent ranges, monotonic level and brightness against velocity with a 15–50 dB range,
unison beating, an 88-note chord under the voice limit with a bounded peak, voice stealing, restrikes, sympathetic
energy ratios, the pedal-up / half / down ordering 1.1 s after release, re-pedalling, sostenuto, the soft pedal,
sample-rate independence at 44.1 / 48 / 88.2 / 96 / 192 kHz (same pitch within 0.1 %, same level within 2 dB), a 12 s
stress render with every coupling at maximum, and the MIDI edge cases (note-off without note-on, velocity 0,
CC120 / CC123, pitch bend ratio and return). `--render <dir>` writes the same kind of material as WAV files with
spectrum CSVs for inspection in any editor; `docs/PLAN.md` records the measured numbers of each build.

What is **not** claimed: no comparison against measured piano recordings has been run yet (none with a licence
that allows redistribution are bundled), and the instrument has not been tuned by ear against Pianoteq or an
acoustic instrument. The model is a physically derived starting point with the knobs to calibrate it.
