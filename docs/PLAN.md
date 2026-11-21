# Resonique: plan, phases and measurements

The brief: a commercially credible, physically modelled concert grand as a Windows VST3 in the family of E-Piano,
Eight Ball and the others, with original DSP, no sample library, automated acoustic tests from the start, and a
phased roadmap. This file tracks what each phase delivered and what was measured. The physics is in
[ACOUSTICS.md](ACOUSTICS.md), the house rules in [CONVENTIONS.md](CONVENTIONS.md).

## Phases

| Phase | Delivered |
|---|---|
| **1 Foundation** | Family survey (E-Piano, Eight Ball, The Ham, Ocarina); `vst/CMakeLists.txt` (JUCE 8.0.17, C++20, VST3 + Standalone, static runtime, sibling JUCE reuse), `build.bat` / `install.bat` / `package.ps1`; the 48-parameter table frozen (`dsp/ParamTable.h` = `ui/js/params.js`, checked by `check-params.ps1`); MIDI routing; the family console with the 88-key keybed; plugin loads in a host (`--host-check`). |
| **2 String engine** | `ModalBuilder` tables (stiff-string partials, physical *B*, register-dependent losses incl. the board admittance), float rotation resonators, adaptive partial counts (per rate, per note, trimmed as they die), 128 voices with quiet-first stealing, restrikes. |
| **3 Hammer and unisons** | Hunt–Crossley felt contact with sub-stepped integration (rate independent), velocity → speed map with Dynamics / Curve, strike-point mode shapes and felt width, one / two / three strings with detune, the local bridge (unison beating, two-stage decay), soft pedal. |
| **4 Resonance** | 96-mode soundboard over 8 zones with radiation weights, feedback into the strings every sample (was every 8 until phase 9), the sympathetic bank (88 × 12 partials), continuous sustain with half-pedal and re-pedalling, sostenuto, dampers acting on the string losses, mechanical noises through the board. |
| **5 Instruments and tuning** | Ten instrument models (string scaling, hammers, dampers, boards, unison and partial irregularities), equal + 7 temperaments, Scala import, concert pitch, Railsback stretch, per-note detune, mic perspectives, early reflections, FDN reverb + impulse-response convolution. |
| **6 Interface and presets** | Five pages (MAIN, PHYSICAL MODEL, MICROPHONES, PERFORMANCE, TUNING) with the partial-series, felt-curve, velocity-curve, mic-diagram and detune editors; preset browser with search, favourites, categories, import / export, init, factory reset; 190 recipe-generated, level-matched presets with distinctness checks. |
| **7 Optimisation and release** | Benchmark, `--render` diagnostics, host check, UI screenshot tests, Standalone smoke test, packaging script, documentation. |
| **8 Optimisation and calibration** | MSVC's vectoriser report showed none of the modal loops vectorised (it could not prove the member arrays distinct); `__restrict` pointers on the string, sympathetic-bank and soundboard loops now vectorise all twelve partial-level loops: per-partial cost down ~40 % (16 voices 10.4 → 6.2 %, 64 voices 24.1 → 17.5 % of a core while carrying 20 % more live partials). Decay recalibrated: the table's modal-admittance share lowered to 0.15 (the live board feedback supplies the rest), background admittance 2.8e-4, bridge resistance 5500 N·s/m, bass internal loss 0.07 /s: C2 6.8 s, C4 6.4 s, C6 2.6 s, E7 1.0 s. Spectral-centroid measurement capped at 20 kHz so sample rates compare (1006–1011 Hz at every rate). Presets re-level-matched. |

| **9 Calibration against reference recordings** | `ResoniqueTests --compare` (features of reference notes vs renders, `--set` overrides, band shares, `--probe-voice`). Against 16 mf / ff notes of the University of Iowa piano: decay recalibrated (losses, background admittance 1.0e-4, bridge resistance 12 000); a renderer defect found and fixed (whole-filter assignment copied the left channel's state into the right every block: a 1/f click train that made every note 10 dB too bright); treble inharmonicity lowered to the measured 1.5–5e-3; fourth-order radiation roll-off below 150 Hz; softer treble felt. The lower losses exposed that the soundboard → string exchange every 8 samples could pump near-lossless strings (String Decay ×4 with the pedal never settled): the exchange now runs every sample, which settles every combination of maximum coupling, resonance, board Q and decay (`--probe-stability`), for about 4 % more CPU; String Decay also scales the local bridge loss. Mean differences against the references: centroid 2.6 dB, B 5.5 dB, partials 2 / 3 7.3 / 8.2 dB, decay at 1 / 2 s 3.9 / 6.2 dB. See ACOUSTICS.md §10. |

## Measurements (phase 9 build of 2026-11-21, 48 kHz unless noted)

Headline numbers after phase 9:
- T60 of the fundamental on the output: C2 15.0 s, C4 10.2 s, C6 5.7 s, E7 1.7 s (references 21 / 15–23 / 7 / 2.7 s).
  C2 with bridge coupling 0 / 0.5 / 1: 18.4 / 15.1 / 12.8 s; with string resonance 0 / 1: 22.8 / 11.8 s;
  unison 0 / 0.5 / 1 ct: 16.6 / 16.2 / 15.1 s. C4: 11.4 / 10.0 / 9.2 s; 9.2 / 11.3 s; 11.1 / 9.5 / 10.0 s.
- Hammer contact at velocity 64 / 120: C1 5.2 / 4.8 ms, C3 2.6 / 2.3, C4 2.1 / 1.8, C6 1.3 / 0.9, G#7 0.9 / 0.5 ms.
- Velocity 20 / 50 / 80 / 110 / 127 at C4: −42.7 / −35.8 / −29.2 / −22.7 / −19.0 dBFS; centroid 485 / 627 / 789 / 958 / 1047 Hz.
- Sample rates 44.1 → 192 kHz on D4: 293.62 Hz everywhere, level −18.8 … −19.0 dB, centroid 592 … 612 Hz.
- Stress (every key at 127, every coupling at maximum, decay ×4, 12 s): −28 / −37 / −58 / −74 dB at 3 / 5 / 8 / 11 s.
  `--probe-stability`: defaults −158 dB at 8.5 s; "all max" −55 dB (slow, not oscillating).
- Benchmark (one core): 16 voices 7.3 %, 32 + pedal 11.4 %, 64 19.6 %, 88 + pedal 24.8 % (9646 partials),
  0 voices + pedal 2.9 %; tables 2.6 ms.
- Renders at velocity 90: C1 −12.6, C2 −12.0, C3 −12.1, C4 −11.8, C5 −12.2, C6 −12.5, C7 −12.6, C8 −14.9 dBFS.
- Reference comparison (16 mf / ff notes): mean |difference| centroid 2.5 dB, B 5.6 dB, partial 2 7.1 dB,
  partial 3 8.1 dB, partial 6 14.3 dB, broadband decay at 1 / 2 / 4 s 3.9 / 6.2 / 14.6 dB (`renders/compare-iowa.csv`).

Phase 8 headline numbers (superseded above; the detailed list below is the phase 7 record):
- T60 of the fundamental on the output: C2 6.8 s, C4 6.4 s, C6 2.6 s, E7 1.0 s. C2 with bridge coupling
  0 / 0.5 / 1: 8.6 / 6.9 / 5.8 s; with string resonance 0 / 1: 9.9 / 4.7 s. C4: 7.3 / 6.3 / 5.9 s and 6.2 / 5.8 s.
- Velocity 20 → 127 at C4: −45.5 → −20.6 dBFS, centroid 854 → 1575 Hz.
- Sample rates 44.1 → 192 kHz on D4: level −22.4 … −22.9 dB, centroid 1006 … 1011 Hz.
- Stress: −41 / −80 / −134 / −141 dB at 3 / 5 / 8 / 11 s. Full coupling E3: −28 / −46 / −70 dB.
- Benchmark (one core): 16 voices 6.2 %, 32 + pedal 9.9 %, 64 17.5 %, 64 + pedal 17.3 %, 88 + pedal 36.0 %
  (9845 partials), 0 voices + pedal (bank + board + reverb) 6.1 %; tables 2.7 ms.
- Renders at velocity 90: C1 −12.2, C2 −11.8, C3 −12.4, C4 −13.9, C5 −13.9, C6 −13.6, C7 −9.3, C8 −13.1 dBFS.

**Tables (`--dsp`)**

| Key | strings | partials | B | T60 of f₁ (table) | felt K (N/mᵖ) | p |
|---|---|---|---|---|---|---|
| A0 | 1 | 128 | 8.7e-5 | 20.6 s | 3.0e8 | 2.30 |
| C2 | 2 | 128 | 1.2e-4 | 13.5 s | 4.0e8 | 2.40 |
| C4 | 3 | 52 | 3.9e-4 | 9.4 s | 4.5e9 | 2.55 |
| C6 | 3 | 14 | 3.9e-3 | 2.6 s | 1.0e11 | 2.80 |
| C8 | 3 | 4 | 2.1e-2 | 0.7 s | 1.2e12 | 3.10 |

Soundboard driving-point admittance 60 Hz–2 kHz: mean 5.6e-4, max 1.7e-3 m/s/N. Hammer contact at velocity 64 /
120: C1 5.5 / 4.9 ms, C3 2.7 / 2.4, C4 2.1 / 1.7, C6 1.1 / 0.5, G#7 0.6 / 0.2 ms.

**Rendered audio (`--acoustic`)**

- Fundamental tuning, single strings: A0 −0.4 ct, A1 −0.6, A2 0.0, A3 +0.7, A4 +0.3, A5 +1.2, A6 +0.2, A7 +0.2
  (0.0 everywhere with the coupling off: the residual is the bridge coupling pulling the strings).
- C4 partials 2–8 within 0.4 % of the formula; inharmonicity ×3 raises partial 6 as expected.
- T60 of the fundamental measured on the output: C2 5.7 s, C4 5.6 s, C6 2.2 s, E7 0.9 s; C4 with bridge coupling
  0 / 0.5 / 1: 6.2 / 4.8 / 4.0 s. Partial 5 decays faster than the fundamental. Release with the damper: −40 dB
  within 0.6 s; C7 (no damper) keeps its fundamental after release.
- Velocity 20 / 50 / 80 / 110 / 127 at C4: −45.9 / −38.7 / −31.7 / −24.9 / −21.0 dBFS; centroid 859 / 1018 / 1217 /
  1443 / 1587 Hz. Velocity Curve +1 vs −1 at 64: louder. Hammer Hardness 0.9 vs 0.1: brighter.
- Unison beating: envelope ripple 0.0 dB at 0 ct, 25–30 dB at 3 ct.
- 88-key chord at ff with the pedal: 88 voices, peak −0.0 dBFS (soft clipper), finite. Polyphony 16 holds 16.
  Restrike keeps one voice and adds energy. Three released notes: zero voices after 8.5 s.
- Sympathetic energy after C4 with the pedal: C3 2.7e-1, C#3 6.8e-3, C2 8.3e-1 (bridge-force units); nothing with
  the pedal up or String Resonance at 0.
- 1.1 s after release: pedal up −138 dB, half −74 dB, down −46 dB. Re-pedalling catches a released note.
  Sostenuto holds C3 (−55 dB) and not a later D4 (−135 dB). Soft pedal: quieter and duller.
- Soundboard Resonance 0 vs 1: 15 dB mean spectral difference on C3.
- Sample rates 44.1 / 48 / 88.2 / 96 / 192 kHz on D4: 293.67 / 293.66 / 293.62 / 293.62 / 293.59 Hz, −23.1 / −23.2 /
  −23.4 / −23.4 / −23.5 dB.
- Stress (every key at 127, every coupling at maximum, 12 s): −4.6 dB held, −44 / −82 / −136 / −155 dB at
  3 / 5 / 8 / 11 s. Full coupling, E3 with the pedal: −30 / −52 / −77 dB at 1 / 3.5 / 7.5 s.
- MIDI edge cases: note-off without note-on, out-of-range notes, velocity 0, CC123, CC120 (silent within the chunk,
  no click), pitch bend +2 st within 0.5 % and back within 0.1 %, key-release and hammer noises present.

**Presets (`--presets`)**: 190 presets, 12 categories, every instrument used, smallest pairwise synthesis
distance 0.10 (threshold 0.08), file round trips, bad / partial / clamped files, the tuning block, levels after
matching within −30..−3 dBFS.

**Host check**: scans as `Instrument|Synth|Piano`, 48 + MIDI-CC parameters named, 190 programs, sound, automation
during playback, state round trip (50 parameters restored, 0 differ).

**Benchmark (Release, one core)**: tables 2.6 ms; 16 voices 11.0 %; 32 voices + pedal 16.3 %; 64 + pedal 25.6 %;
88 + pedal 56.0 % (7900 partials).

**Renders (`--render`)**: peaks at velocity 90 across the compass −12.4 (C1) .. −13.9 (C4) .. −9.4 (C7) .. −13.1 dBFS
(C8); velocity 15 → 127 at C4 −31.7 → −4.8 dBFS; the pedalled chord −1.2 dBFS.

## Open items

- Listen. The model is now calibrated to public measurements of one piano (phase 9); the remaining differences
  (partial 6 and the treble's upper partials, the bass hammers at ff) and everything the measurements do not
  capture need ears.
- CPU: the 88-voice worst case is 25 % of a core after phase 9; AVX2 with runtime dispatch would bring it down
  further.
- A web / Android build the Eight Ball way (the core is JUCE-free).
- Aftertouch is accepted and ignored (nothing physical for it to do on a piano).
