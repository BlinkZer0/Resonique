# Resonique — VST3 plugin (JUCE 8)

This folder is the source of truth for Resonique:
- **C++ DSP:** the physically modelled piano (JUCE-free core in `Source/dsp`, `Source/instruments`, `Source/PianoEngine.h`).
- **HTML/SVG UI:** shown inside the plugin through WebView2 (`ui/`), the family console.
- **Tests and tools:** `ResoniqueTests` (console), `tests/check-params.ps1`.

## Build

```
build.bat            VST3 + Standalone + tests (Release)
build.bat Debug
install.bat          copies Resonique.vst3 to C:\Program Files\Common Files\VST3 (asks for admin)
powershell -ExecutionPolicy Bypass -File package.ps1     dist\Resonique-<version>-Windows-x64.zip
```

Requirements: Visual Studio 2022 or 2026 with the C++ workload, Git, and the WebView2 runtime (built into
Windows 11). On first build CMake downloads JUCE 8.0.17 unless a sibling project's copy exists in
`%LOCALAPPDATA%\<Sibling>VST\deps\juce-src`. Build files go to `%LOCALAPPDATA%\ResoniqueVST`, so OneDrive doesn't
sync them:

- `...\build\ResoniqueVST_artefacts\Release\VST3\Resonique.vst3`
- `...\build\ResoniqueVST_artefacts\Release\Standalone\Resonique.exe` is the quickest way to test without a DAW.
  Use *Options → Audio/MIDI Settings* to pick the audio device and MIDI input.
- `...\build\ResoniqueTests_artefacts\Release\ResoniqueTests.exe`

To open the project in Visual Studio, open `%LOCALAPPDATA%\ResoniqueVST\build\ResoniqueVST.slnx` after one build.

## Signal flow

```
per key:    hammer (felt contact) -> 1..3 modal strings <-> local bridge  -> bridge force into its zone
global:     8 zone forces (+ sympathetic bank + noises) -> modal soundboard -> radiation L/R, zone accelerations
            zone accelerations -> back into every undamped string (next 32-sample chunk)
output:     board L/R + direct HF path -> mics (distance, spread, perspective) -> early reflections -> reverb
            -> tone shelves -> master -> soft clip
```

The physics is written up in [../docs/ACOUSTICS.md](../docs/ACOUSTICS.md); the house rules in
[../docs/CONVENTIONS.md](../docs/CONVENTIONS.md).

## In the DAW

- Loads as an instrument (Instrument|Synth|Piano) with stereo out and MIDI in.
- **48 automatable parameters.** Knob drags record as single automation gestures. **Right-click any control** for
  the host's parameter menu (FL Studio's *Create automation clip*, Cubase's automation menu, ...). Shift+right-click
  opens DevTools.
- **Programs:** the 190 factory presets appear in the DAW's preset/program menu.
- **MIDI:** note on/off with velocity, sustain **CC64 (continuous, half-pedal)**, sostenuto **CC66**, soft **CC67**,
  pitch bend (range parameter), all sound off (CC120), all notes off (CC123), reset controllers (CC121). All channels.
  Events are applied at the exact sample they arrive.
- **State:** the project saves every parameter, the selected preset, the tuning block (per-note detune, Scala text)
  and the impulse-response path.
- **Physical parameters** (instrument, hammer, string, damper, soundboard, tuning) rebuild the note tables on a
  worker thread; sounding notes keep playing with their old tables, new notes use the new ones. The MODEL panel
  shows "rebuilding…" while it happens. Everything else (tone, mics, reverb, resonance amounts, noises, pedals) is
  live.

## Pages

| Page | Controls |
|---|---|
| **MAIN** | Instrument (10 physical models) · Master, Bass, Treble, Width · Hardness, String Resonance, Board Resonance, Reverb · Dynamics, Unison, Decay, Noise |
| **PHYSICAL MODEL** | Strings: Inharmonicity, Stiffness, Decay, Unison, Bridge (+ the partial-series screen) · Hammer: Position, Mass, Felt K, Felt Exponent (+ the force-curve screen) · Dampers, Board Size, Board Damping, Brightness · Mechanical noises: Amount, Hammer, Release, Damper, Pedal · Model statistics |
| **MICROPHONES** | Perspective (Player / Close / Audience / Wide / Mono / Custom) with the diagram · Distance, Spread, Position, Reflections · Room: Type (Room / Chamber / Hall / Stage / Impulse Response), Mix, Size, Damping, Predelay, LOAD IR · Width, Bass, Treble, Brightness, Master |
| **PERFORMANCE** | Velocity curve editor (drag) with Dynamics and Curve · Sustain mode (Continuous / Switch), Half-Pedal Point, Soft Depth, Bend Range, Polyphony · voices, partials, CPU, pedal state |
| **TUNING** | Temperament (Equal, Pythagorean, Just, Meantone ¼, Werckmeister III, Kirnberger III, Vallotti, Young, Custom/Scala), Root, A4, Stretch · LOAD .SCL · per-note detune editor (drag the keys, ±50 ct) |

The keybed shows all 88 keys (click, drag for glissando, lower = harder), three pedal buttons (SOFT, SOST, SUST),
and the computer keyboard plays A–' with Z / X for the octave and Space for the sustain pedal.

## Presets

- **Factory:** 190 presets in 12 categories (Concert Grands, Classical, Jazz, Pop, Rock, Cinematic, Intimate, Felt,
  Uprights, Vintage, Ambient, Experimental), generated from recipes in [presets/make_presets.py](presets/make_presets.py).
  Each one is a distinct set of physical parameters (the script and the tests refuse near-duplicates).
- **User folder:** `Documents\Resonique\Presets`, shared by every DAW and every instance; subfolders become
  categories; rescanned every ~2 s. **SAVE** asks for a name and a category, **IMPORT** opens any `.rqpreset`,
  **EXPORT** writes one anywhere, **FOLDER** opens the folder, **INIT** resets everything, **★** marks favourites
  (stored in `Documents\Resonique\favorites.json`), the search box searches names, descriptions and categories.
- File format (plain JSON):
  ```json
  { "format": "Resonique Preset", "version": 1, "name": "My Grand", "category": "User", "description": "...",
    "params": { "instrument": 0, "hammerHardness": 0.6, "inharmonicity": 1.2 },
    "tuning": { "detune": [0, 0, ...], "scala": "..." } }
  ```

## Tests

```
cmake --build %LOCALAPPDATA%\ResoniqueVST\build --config Release --target ResoniqueTests
cd %LOCALAPPDATA%\ResoniqueVST\build\ResoniqueTests_artefacts\Release
```

| Command | Checks |
|---|---|
| `ResoniqueTests.exe` | everything below except the tools |
| `ResoniqueTests.exe --dsp` | helpers, tuning and Scala, the parameter table, the model builder (pitch, partial series, B range, decay ranges, string counts, sample-rate independence, every instrument), the resonator (frequency, decay, float precision at 27.5 Hz), hammers (velocity map, contact times by register), pedals, dampers, noises, reverb, renderer |
| `ResoniqueTests.exe --acoustic` | rendered audio: tuning, inharmonic partials, decay times, velocity level and brightness, unison beating, polyphony and stealing, restrikes, voices ending, sympathetic resonance, sustain / half / re-pedal, sostenuto, soft pedal, soundboard influence, sample rates, 12 s stress, MIDI edge cases, pitch bend |
| `ResoniqueTests.exe --presets` | 190 factory presets (ranges, names, categories, every instrument used, synthesis distinctness), file round trips, bad / partial files, the tuning block, a few presets' levels |
| `ResoniqueTests.exe --bench` | table build time; CPU at 16 / 32 / 64 / 128 voices |
| `ResoniqueTests.exe --render <dir>` | diagnostic WAVs + spectrum CSVs: notes across the compass, a velocity ladder, chords with pedal and room, half-pedal, every instrument |
| `ResoniqueTests.exe --loudness` | loudness of every factory preset (level matching: `make_presets.py --match`) |
| `ResoniqueTests.exe --host-check <Resonique.vst3>` | loads the built plugin like a DAW: scan, parameters, sound, automation, state round trip, programs |
| `ResoniqueTests.exe --compare <folder> [--instrument N] [--out file.csv] [--set id=value] [--set bands=1]` | measures reference recordings of single notes (files named like `Piano.ff.C4.aiff`) and renders of the same notes: inharmonicity, partial levels, decay, centroid, band shares; see docs/ACOUSTICS.md §10 |
| `ResoniqueTests.exe --probe-voice <note> [vel]` | the raw bridge force of one voice without board or renderer (band shares, steps, contact time) |
| `powershell -File tests\check-params.ps1` | the C++ parameter table against `ui/js/params.js` |

## Layout

```
CMakeLists.txt  build.bat  install.bat  package.ps1
Source/
  dsp/            Common, ParamTable, Tuning, StringModel (tables), ModelBuilder, HammerModel, DamperModel,
                  PianoVoice, SoundboardModel, CouplingEngine (sympathetic bank), PedalModel, MechanicalNoise,
                  Reverb, SpatialRenderer, VoiceManager        (no JUCE)
  instruments/    InstrumentModel, ConcertGrand, InstrumentLibrary (10 pianos)
  PianoEngine.h   the engine
  Params.h        APVTS layout, formatting, JSON dump
  TableStore.*    worker-thread table builds, lock-free hand-off, tuning state
  PresetManager.* factory + user presets, favourites
  MidiRouting.h   MIDI -> engine
  PluginProcessor.*  PluginEditor.*
ui/               index.html, css/style.css, js/{params, engine-juce, knob, controls, piano, editors, app}.js
presets/          make_presets.py -> factory_presets.json
resources/icon.png
tests/            TestMain, TestRender.h, DSPTests, AcousticTests, PerformanceTests, PresetTests, HostCheck,
                  EngineTools, check-params.ps1
```

### Bridge events

| UI → host | host → UI (30 Hz, only changes) |
|---|---|
| `uiReady`, `setParam {id,value}`, `setParams {values}`, `gesture {id,begin}` | `params {id:value,…}` |
| `noteOn {note,vel}`, `noteOff {note}`, `panic`, `pedal {cc,value}` | `notes {on:[],off:[]}`, `stats {voices,partials,peak,sustain,sostenuto,soft,cpu,building,sampleRate}` |
| `loadPreset {id}`, `savePreset {name,category,overwrite}`, `deletePreset {id}`, `setFavourite {id,on}`, `initPreset`, `factoryReset` | `presetList {presets,folder,favorites}`, `preset {id,name,category,description}`, `saveResult {ok,exists,name,error}` |
| `loadPresetFile`, `exportPreset`, `openPresetFolder`, `paramMenu {id,x,y}` | `message {text}`, `config {sampleRate,blockSize,wrapper,voices,version}` |
| `setDetune {note,cents}`, `resetDetune`, `loadScala`, `clearScala`, `loadIR`, `clearIR` | `tuning {detune,scala,scalaNotes,valid}`, `ir {name}` |

## Troubleshooting

- **Live UI editing:** set the environment variable `RESONIQUE_UI_DIR` to this repo's `vst\ui` folder before
  starting the DAW or Standalone. The UI then loads from disk: edit HTML/CSS/JS and just reopen the plugin window.
- **DevTools:** Shift + right-click in the plugin window, then *Inspect*.
- **Blank window:** the WebView2 runtime is missing or blocked. Its data folder is `%TEMP%\ResoniqueVST-WebView2`;
  deleting it is safe.
- **CPU:** the PERFORMANCE page shows the callback's load. Lower Polyphony, or play with the pedal up less often:
  the sympathetic bank only runs while some damper is off.
