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

| **10 Instrument library** | The nine derived pianos are re-expressed relative to the recalibrated base: geometry (lengths, tensions, cores, strike points, hammer masses, dampers, boards) stays absolute, felt (`voiceFelt`: force at 0.5 mm × factor, exponent offset), losses and bridge loss are factors on the base, so a recalibration of the concert grand carries through. `tests/InstrumentTests.cpp` (`--instruments`, and part of the default run) measures contact time, inharmonicity, broadband decay, centroid and level at C2 / C4 / C6 for every instrument, checks each against sane ranges and against the base (at least two metrics must differ), and per-instrument level trims keep every instrument within a few dB of the grand. |

| **11 Robustness and real-time safety** | `tests/RobustnessTests.cpp` (part of the default run, `--robustness`): a global `operator new` counter proves **zero heap allocations** across 200 rendered blocks with notes, pedals, parameter changes, a panic and a table hand-off; the output is **bit-identical for block sizes 1, 7 / 13 / 64 / 100, 512 and 2048** (the engine's 32-sample control chunks now keep a fixed phase across calls instead of restarting with every block, and the renderer's gain ramps are chunk-based); a 44.1 → 96 kHz change mid-session rebuilds and retunes; a table swap under five sounding notes and every live parameter swept across its range at block rate stay click-free (worst sample step 0.07); 5000 note-ons in 100 blocks never exceed the polyphony and the engine returns to silence; and the idle cost after the notes have died is checked (the soundboard and the reverb now sleep when they have rung down). Also found: a 2 MB `PianoEngine` on the stack overflows (tests allocate it on the heap; the plugin always did). |

| **12 AVX2 dispatch** | The three per-sample hot loops (string rotation with coupling / hammer injection and damping, the sympathetic bank, the soundboard) live in `dsp/KernelsImpl.h` with internal linkage and are compiled twice, `Kernels_sse.cpp` (x64 baseline) and `Kernels_avx2.cpp` (`/arch:AVX2`); `Kernels.cpp` picks a set once (CPUID + XGETBV for OS support), from `PianoEngine::prepare`, never on the audio thread; `RESONIQUE_NO_AVX2=1` forces the baseline. Both sets pass the acoustic and robustness suites (block-size invariance included). Gain on this machine: 16 voices 7.6 → 6.6 %, 64 + pedal 20.1 → 18.7 %, idle with the pedal 2.4 → 1.5 % of a core; the modal loops are memory-bound enough that eight-wide math buys 10–15 %, not 2×. |

| **13 Web build** | The Eight Ball pattern: `vst/webhost/wasm-engine.cpp` is a flat C API (`rq_*`: create, parameters, render, MIDI, stats, double-buffered tables, detune / Scala) over the unchanged `PianoEngine.h` + `dsp/`, compiled by `build-web.ps1` with Emscripten (`-O3 -msimd128 -ffast-math -fno-exceptions`, standalone wasm, 255 KB) and inlined base64 into one page with the UI, the CSS, the factory presets and `web-host.js`, which stands in for the C++ side behind `window.__JUCE__.backend` (every event of `PluginEditor`). Audio: an AudioWorklet runs one engine instance; a second instance on the main thread builds the note tables (about 5 ms) and posts the bytes into the worklet, so a physical-parameter change never stalls the audio thread; on `file://` (worklet modules blocked) a ScriptProcessor on the main thread takes over. Web MIDI (all inputs or one, remembered), pedals, pitch bend, CC 120 / 123. Presets: factory (resolved like `PresetManager`, named choices included), this browser's (localStorage), favourites, `.rqpreset` import / export, init / factory reset; tuning: per-note detune, Scala files. Tests: `web-check.js` (Node runs the wasm engine: silence, a note, release, pedal, panic, concert pitch, Scala, 16 voices + pedal at 7.8 % of real time), `web-audio-check.js` (headless Edge over HTTP via the DevTools protocol: the page's `?selftest` hook plays a note through the AudioWorklet — mode AudioWorklet, 48 kHz, 1 voice, peak 0.15), `run-web-check.ps1` (both plus a screenshot and DOM checks). Found and fixed in the engine: (a) `ModelBuilder::build` never read the temperament / root / concert pitch / stretch parameters (the plugin's tuning page only worked through Scala and detune) — a DSP test now checks 442 Hz, Werckmeister and stretch reach the tables; (b) after a panic the renderer's shelving / high-pass filters kept ringing into the freshly cleared reverb (a tail at −63 dB decaying over seconds) — `SpatialRenderer::reset` now clears every filter; `ScalaScale::parse` rewritten without exceptions. |

| **14 Android app** | The Brass Knuckles pattern: `android/` (Gradle 8.14, AGP 8.13, API 36, minSdk 26) wraps `web/index.html` in a full-screen WebView served from `appassets.androidplatform.net` (a secure origin, so the AudioWorklet and localStorage work), with `MidiBridge.java` + `android-midi.js` standing in for the missing Web MIDI (USB and Bluetooth devices, running status, batches to the page), the system file picker for IMPORT / LOAD .SCL and a "save as" dialog for EXPORT (`window.AndroidFiles`), suspend / resume when the app leaves the screen, no permissions, no native code (the engine is the wasm inside the page). The shared UI gained the phone layout (`body.compact` / `body.portrait`: one 480 px column with 25 keys in portrait, two panels across on 900 px with 37 keys in landscape, the keyboard pinned at the bottom with pedals and octave buttons, the browser folding behind the preset name); the desktop and plugin layouts are unchanged (`run-ui-tests.ps1` still passes). Outputs: signed `app-release.aab` (260 KB) and `app-release.apk` (240 KB), upload key generated and git-ignored. Store kit: adaptive grand-piano icon (+ monochrome layer), `store/icon-512.png`, `feature-1024x500.png`, four screenshots rendered from the real page (`?screenshot=<page>&layout=…`, 1080 × 2160 ×3 and 2160 × 1080), `listing.md` (texts, app-content answers, data safety, release notes, checklist), `privacy-policy.md`, and `docs/privacy.html` + `docs/terms.html` for GitHub Pages. Checked: `aapt2` badging (package, API 36, no permissions, optional MIDI / USB / touch features), `apksigner verify`, and `tests/check-device.js` on the connected Quest 3 (Android 14): installed, the page loads from the asset origin, audio runs in an AudioWorklet at 48 kHz, the Web MIDI shim is present. |

## Instrument metrics (phase 10, velocity 76)

| Instrument | C2: contact ms · B · T60 s · centroid Hz | C4 | C6 |
|---|---|---|---|
| Concert Grand | 3.6 · 1.2e-4 · 12.0 · 412 | 2.0 · 3.9e-4 · 12.4 · 767 | 1.19 · 2.5e-3 · 6.6 · 1319 |
| Studio Grand | 3.4 · 2.6e-4 · 9.3 · 434 | 1.9 · 4.9e-4 · 11.2 · 727 | 1.10 · 2.9e-3 · 5.3 · 1425 |
| Upright | 3.7 · 6.8e-4 · 10.8 · 423 | 1.9 · 8.2e-4 · 10.7 · 556 | 1.19 · 4.8e-3 · 2.5 · 1539 |
| Vintage Upright | 3.9 · 6.8e-4 · 14.9 · 337 | 2.1 · 8.2e-4 · 11.2 · 615 | 1.38 · 4.8e-3 · 2.0 · 1524 |
| Bright Pop Grand | 3.1 · 1.2e-4 · 8.7 · 539 | 1.8 · 3.9e-4 · 10.8 · 947 | 1.02 · 2.5e-3 · 4.5 · 2121 |
| Dark Jazz Grand | 3.9 · 1.2e-4 · 8.8 · 381 | 2.2 · 3.9e-4 · 13.2 · 759 | 1.35 · 2.5e-3 · 2.7 · 1351 |
| Felt Piano | 5.3 · 1.2e-4 · 7.9 · 248 | 2.9 · 3.9e-4 · 9.0 · 476 | 2.31 · 2.5e-3 · 3.0 · 874 |
| Prepared Piano | 3.4 · 2.4e-4 · 11.1 · 540 | 1.9 · 7.0e-4 · 8.2 · 775 | 1.12 · 4.0e-3 · 2.8 · 1847 |
| Honky-Tonk | 3.6 · 6.8e-4 · 12.3 · 450 | 1.9 · 8.2e-4 · 8.8 · 921 | 1.12 · 4.8e-3 · 3.0 · 1980 |
| Experimental Grand | 5.3 · 8.5e-6 · 33.3 · 492 | 3.7 · 3.3e-5 · 18.5 · 856 | 1.69 · 2.5e-4 · 4.6 · 1110 |

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
- CPU: the 88-voice worst case is about 24 % of a core with the AVX2 kernels (phase 12). The next step would be
  restructuring the string banks so several strings share one vector loop (the treble's short banks waste lanes).
- The Android app (phase 14) was checked on a Samsung SM-S948U1 (Android 17, 1080 x 2340) over DevTools: the
  portrait layout (15 white keys) and landscape (22) render, audio runs in an AudioWorklet at 48 kHz with the
  clock advancing, a note sounds (1 voice, peak 0.048), and the MIDI bridge lists the phone's USB MIDI port.
  Still to do by hand: play the touch keys and a USB MIDI keyboard, EXPORT / IMPORT through the system dialogs.
- The web build needs a hand test with a real MIDI keyboard in Chrome / Edge (Web MIDI) and in Firefox / Safari
  (AudioWorklet only; no headless check covers them here).
- Aftertouch is accepted and ignored (nothing physical for it to do on a piano).
