# Conventions (inherited from E-PianoVST, The Ham, Brass Knuckles and Eight Ball)

Resonique follows the house style of [E-PianoVST](https://github.com/BlinkZer0/E-PianoVST) as refined by Eight Ball
(local clones in `../`). This file restates the rules briefly and, under **Resonique:**, lists every place where
Resonique changes or extends a pattern.

## 1. Repository layout

```
README.md                    user-facing: features, build/install, release history, layout
LICENSES.md                  what the binaries contain and under which terms
.gitignore                   .vs/  build/  *.user  dist/
.gitattributes               * text=auto, *.bat and *.ps1 crlf
docs/                        CONVENTIONS.md, PLAN.md, ACOUSTICS.md (the model and the measurements)
vst/
  CMakeLists.txt  build.bat  install.bat  package.ps1  README.md
  Source/                    C++ (see 3)
  ui/                        HTML/CSS/SVG/JS UI, compiled into the binary as BinaryData
  presets/                   make_presets.py (recipes) -> factory_presets.json (generated), trims.json
  resources/icon.png
  tests/
```

**Android (the Brass Knuckles way):** `android/` is a Gradle project (AGP 8.13, Gradle 8.14, API 36, minSdk 26,
no permissions, no native code) whose `bundleWebApp` task copies `web/index.html` into the assets with
`android-midi.js` (the Web MIDI shim over `MidiBridge.java`) injected into `<head>`; `MainActivity` serves it from
`appassets.androidplatform.net`, hands file pickers and the save dialog (`window.AndroidFiles`) to the page and calls
`window.__rqWebHost.suspend/resume` when it leaves / returns to the screen. The phone layout is in the shared UI
(`body.compact`, `body.portrait` in `style.css`; `fit()` in `app.js`; `window.__rqLayout` forces it for screenshots).
Signing: `upload-key.jks` + `keystore.properties`, git-ignored. Store kit in `android/store/`, policy pages in
`docs/privacy.html` and `docs/terms.html`.

**Web build (the Eight Ball way):** `vst/webhost/wasm-engine.cpp` (flat C API `rq_*` over the unchanged engine),
`vst/webhost/web-host.js` (the bridge stand-in: `window.__JUCE__.backend` with exactly `PluginEditor`'s events,
AudioWorklet + main-thread table builder, Web MIDI, localStorage presets), `vst/webhost/build-web.ps1` (em++ →
one self-contained `web/index.html`, copied to `docs/index.html`), `start.bat` / `serve.ps1` (local HTTP), and
`vst/tests/run-web-check.ps1` (Node engine test, screenshot, DOM checks, a note through the AudioWorklet in headless
Edge). The engine source must stay buildable with `-fno-exceptions -fno-rtti` (no `try`, no `std::stoi`): the
Scala parser uses `strtod` with end-pointer checks.

## 2. Build system

- CMake 3.22+, **C++20**, **JUCE 8.0.17** (`GIT_TAG 8.0.17-security`), `FetchContent`, shallow. The cache variable
  `RESONIQUE_JUCE_DIR` points at a local JUCE checkout instead; `build.bat` passes a sibling project's download
  (E-Piano, Eight Ball, The Ham, No Strings, Brass Knuckles, I Wub You) when one exists.
- `juce_add_plugin`: `FORMATS VST3 Standalone`, `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `NEEDS_WEBVIEW2 TRUE`,
  `VST3_CATEGORIES Instrument Synth Piano`, `EDITOR_WANTS_KEYBOARD_FOCUS TRUE`, `COPY_PLUGIN_AFTER_BUILD FALSE`.
- Defines `JUCE_WEB_BROWSER=1`, `JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1`, `JUCE_USE_CURL=0`,
  `JUCE_VST3_CAN_REPLACE_VST2=0`, `JUCE_DISPLAY_SPLASH_SCREEN=0`. The MSVC runtime is linked statically.
  **Resonique:** `/fp:fast` on the plugin and the tests so the modal-bank loops vectorise.
- One `juce_add_binary_data` target holds `ui/*.html|css|js|svg` and `presets/factory_presets.json`.
- Console app `ResoniqueTests` compiles the preset manager, the table store and the test sources, and includes the
  header-only engine. Built with `JUCE_PLUGINHOST_VST3=1` for `--host-check`.
- `build.bat`: work dir `%LOCALAPPDATA%\ResoniqueVST` (build + deps), builds `ResoniqueVST_VST3`,
  `ResoniqueVST_Standalone` and `ResoniqueTests`. Messages are prefixed `[Resonique]`.
- `install.bat`: copies `Resonique.vst3` to `%CommonProgramFiles%\VST3` through an elevated PowerShell.
- `package.ps1`: `dist\Resonique-<version>-Windows-x64.zip` with the Standalone, the VST3 and a README.txt, after
  checking with `dumpbin` that neither binary needs the Visual C++ Redistributable.

Identity: product `Resonique`, target `ResoniqueVST`, company `Resonique`, manufacturer code `Rsnq`, plugin code
`Rsq1`, namespace `resonique`, env var `RESONIQUE_UI_DIR`, WebView2 data folder `%TEMP%\ResoniqueVST-WebView2`,
user presets `Documents\Resonique\Presets` (`.rqpreset`), favourites `Documents\Resonique\favorites.json`.

## 3. C++ source structure

| File | Role |
|---|---|
| `dsp/Common.h` | Constants (`kMaxPartials`, `kChunk`, zones, ...), `NoteCurve`, `Rng`, `Smoother`, `Biquad`, the safety clipper. |
| `dsp/ParamTable.h` | **The parameter table in plain C++** (no JUCE): `enum ParamIndex`, `paramTable()`, `paramToNorm / paramFromNorm`, `paramAffectsTables()`. |
| `dsp/Tuning.h` | Temperaments, Scala parsing, stretch, per-note detune -> `noteFrequency()`. |
| `instruments/InstrumentModel.h`, `ConcertGrand.h`, `InstrumentLibrary.h` | The physical description of each piano as curves across the keyboard. |
| `dsp/StringModel.h` | The table structs the audio thread plays from (`StringTable`, `NoteTable`, `SoundboardTable`, `InstrumentTables`). |
| `dsp/ModelBuilder.h` | Instrument + parameters + tuning + sample rate -> tables (worker thread). |
| `dsp/HammerModel.h`, `DamperModel.h`, `PianoVoice.h` | One key: three modal strings, the hammer, the damper, the local bridge. |
| `dsp/SoundboardModel.h` | The shared modal soundboard: zone forces in, radiation + zone accelerations out. |
| `dsp/CouplingEngine.h` | The sympathetic bank (light copies of every string, grouped by zone). |
| `dsp/PedalModel.h`, `MechanicalNoise.h`, `Reverb.h`, `SpatialRenderer.h` | Pedals, synthesised noises, the FDN, mics / room / tone / master. |
| `dsp/VoiceManager.h` | Allocation and stealing. |
| `dsp/Kernels.h`, `KernelsImpl.h`, `Kernels_sse.cpp`, `Kernels_avx2.cpp`, `Kernels.cpp` | The per-sample hot loops, compiled for the baseline and for AVX2, selected at run time (`kernels()`, called from `prepare()`; `RESONIQUE_NO_AVX2=1` forces the baseline). |
| `PianoEngine.h` | The engine: `prepare / setTables / setParamValues / noteOn / noteOff / controller / pitchBend / render`. |
| `Params.h` | JUCE side: `formatValue`, `logRange`, `createParameterLayout()` with `ParameterID{id, 1}`, `paramTableToJson()`. |
| `TableStore.*` | Message-thread owner of the tables: worker thread, lock-free hand-off, the tuning state (detune, Scala). |
| `PresetManager.*` | Factory presets from BinaryData, user folder scan, read/write, favourites. |
| `MidiRouting.h` | MIDI message -> engine (shared by the plugin and the test tools). |
| `PluginProcessor.*` | APVTS, sample-accurate MIDI, programs, state (params + preset + tuning + IR path), the convolution reverb, the table-rebuild timer. |
| `PluginEditor.*` | `WebBrowserComponent` (WebView2), resource provider, bridge events, 30 Hz timer that pushes only changes. |

Rules:
- `juce::ScopedNoDenormals` in `processBlock`. Nothing in the engine allocates, locks or does I/O after `prepare()`.
- **Control rate `kChunk = 32` samples.** Smoothing, damper pressure and partial trimming run once per chunk; modal states
  and the soundboard <-> string exchange run per sample.
- Master gain is smoothed per chunk; the output safety clipper is exactly linear below −1.9 dBFS.
- Parameter changes made by presets go through `beginChangeGesture / setValueNotifyingHost / endChangeGesture`.
  `master` is never stored in presets.

**Resonique:**
- **The DSP core has no JUCE in it at all** (Eight Ball's rule), including the parameter table and the instrument
  library. The engine takes the raw parameter array (`setParamValues (const float*)`).
- **Tables, not coefficients-on-the-fly.** Every expensive number (partial frequencies, losses, rotation coefficients,
  hammer and bridge weights, soundboard modes and admittance) lives in `InstrumentTables`, built off the audio
  thread by `ModelBuilder` and handed over through `TableStore` (atomic incoming / retired pointers). Voices copy
  what they need at note-on, so a table can be swapped at any block boundary. `paramAffectsTables()` lists the
  parameters that trigger a rebuild (the processor's 20 Hz timer debounces them); everything else is read live.
- **Float, structure-of-arrays modal banks** rotated per sample with plain loops the compiler vectorises; partials
  that have died are dropped from the top (`VoiceString::active`).
- **Two coupling levels:** the local bridge inside a voice (unison beating, two-stage decay) is per sample; the
  soundboard -> string feedback (sympathetic resonance) runs every sample through eight bridge zones (an 8-sample step pumped near-lossless strings).

## 4. Parameters

- **Append-only.** New parameters go at the end of `ParamIndex`. Indices and string IDs never change.
- **New parameters default to neutral**, so older presets and projects sound unchanged.
- The whole table (48 parameters) was defined in Phase 1. `dsp/ParamTable.h` and `ui/js/params.js` are mirrored by
  hand; `ResoniqueTests --dump-params` prints the C++ table as JSON and `tests/check-params.ps1` fails on any
  difference.
- **State that is not a host parameter** lives in the **tuning block** (per-note detune, the Scala text) and travels
  in presets and project state as JSON (`"tuning": { "detune": [...], "scala": "..." }`). The impulse-response path
  is project state only.

## 5. UI <-> C++ bridge

Inherited: `ui/js/engine-juce.js` wraps `window.__JUCE__.backend` in a `JuceEngine` class.
- UI -> host: `uiReady`, `setParam {id,value}`, `setParams {values}`, `gesture {id,begin}`, `noteOn {note,vel}`,
  `noteOff {note}`, `panic`, `pedal {cc,value}`, `loadPreset {id}`, `savePreset {name,category,overwrite}`,
  `deletePreset {id}`, `loadPresetFile`, `exportPreset`, `openPresetFolder`, `initPreset`, `factoryReset`,
  `setFavourite {id,on}`, `paramMenu {id,x,y}`, `setDetune {note,cents}`, `resetDetune`, `loadScala`, `clearScala`,
  `loadIR`, `clearIR`.
- Host -> UI (30 Hz, only changes): `params`, `notes`, `stats {voices, partials, peak, sustain, sostenuto, soft,
  cpu, building, sampleRate}`, `config`, `presetList {presets, folder, favorites}`, `preset {id, name, category,
  description}`, `saveResult`, `message`, `tuning {detune, scala, scalaNotes, valid}`, `ir {name}`.
- On `uiReady` the editor clears its `lastSent` cache, so everything is sent again. Drags send `gesture begin/end`;
  one-shot changes are wrapped in a gesture by the editor. UI notes go through `MidiKeyboardState`; UI pedals go
  through an atomic CC mailbox the audio thread drains.
- Dev mode: env var **`RESONIQUE_UI_DIR`** loads the UI from disk. Shift + right-click opens DevTools.

## 6. UI code

- Plain ES, `'use strict'`, no framework, no build step. Scripts load in order:
  `params.js, engine-juce.js, knob.js, controls.js, piano.js, editors.js, app.js`.
- All controls are SVG. `Knob` is the reference widget. Panels are built in `app.js` (`addKnob`, `control(Class, id)`)
  and every control carries `data-param`. A parameter may have more than one knob (`addKnob` registers them all).
  Pages are tabs; the whole console (1180 px wide) scales to fit the window.
- CSS custom properties on `:root`.

**Resonique:** same widget family, new identity: **polished-ebony chassis, ivory text and knob pointers, a felt-red
accent (`--accent: #d9544a`) for everything interactive, brass (`--brass: #c9a35b`) for the preset display and
readouts**, the resonance-arcs logo (the same `<symbol>` in the header; `vst/resources/icon.png` is the app icon).
Inverted keys (black naturals, ivory sharps) over the full 88 keys, with three pedal buttons. `editors.js` holds the
page screens: the stiff-string partial series, the felt force curve, the velocity-curve editor, the microphone
diagram and the per-note detune editor. The preset browser has category tabs with counts, a Favourites tab, a search
box and a description line.

## 7. Presets

- Factory presets: `presets/factory_presets.json`, **generated** by `presets/make_presets.py` from recipes written
  against a small builder (`P(...).hammer(...).strings(...).board(...).dampers(...).mics(...).room(...)`); edit the
  recipes, never the JSON. 190 presets in 12 categories; the script refuses presets whose synthesis parameters are
  near-identical to another preset's (and `ResoniqueTests --presets` checks the same thing). Choices may be named in
  the factory file.
- User presets: `Documents\Resonique\Presets`, subfolders = categories, rescanned every ~2 s by signature. Delete
  moves the file to the Recycle Bin. SAVE asks for a name and a category; IMPORT / EXPORT move `.rqpreset` files.
- File: `.rqpreset`, plain JSON `{ "format": "Resonique Preset", "version": 1, "name", "category", "description",
  "params": {id: value}, "tuning": {...} }`. Missing parameters load at their defaults, values are clamped, master is
  never stored.
- Factory presets are also host programs (`"<Category> - <Name>"`).
- **Level matching:** `ResoniqueTests --loudness` measures the loudest 100 ms RMS of a mf chord per preset and
  `make_presets.py --match <that csv>` writes `presetGain` trims (`presets/trims.json`) so each sits near −16 dBFS.

## 8. Tests

`ResoniqueTests` (console): `--dsp` (helpers, tuning, the table, the model builder, resonators, hammers, pedals,
dampers, noises, reverb), `--acoustic` (rendered audio: tuning, inharmonic partials, decay, velocity, unison
beating, polyphony, sympathetic resonance, pedals, sample rates, stability, MIDI edge cases), `--presets`,
`--bench`, `--render <dir>` (diagnostic WAVs + spectrum CSVs), `--loudness`, `--dump-params`, `--host-check
<Resonique.vst3>`. `tests/check-params.ps1` compares the C++ table with `params.js`.
