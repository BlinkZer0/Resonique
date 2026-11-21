// ============================================================================
//  Web host: stands in for the plugin's C++ side when the UI runs in a
//  browser. It provides window.__JUCE__.backend with exactly the events the
//  JUCE editor uses (see PluginEditor.h), so vst/ui runs unchanged:
//    audio    the engine compiled to WebAssembly (RESONIQUE_WASM_B64, from
//             vst/Source via webhost/wasm-engine.cpp) in an AudioWorklet, or on
//             the main thread through a ScriptProcessor when opened from disk
//    tables   the note tables are built by a second engine instance on the
//             main thread and copied into the worklet (no stall on the audio thread)
//    MIDI     Web MIDI (all inputs or one): notes, sustain / sostenuto / soft,
//             pitch bend, all notes off
//    presets  factory (FACTORY_JSON, resolved like PresetManager), this browser
//             (localStorage), import / export of the same .rqpreset files as the VST
//    tuning   per-note detune and Scala files, like the plugin
// ============================================================================
(function () {
  'use strict';

  // Where the page runs: the Android app (android/, which injects window.AndroidMidi and
  // window.AndroidFiles before this script) or a browser. Query options:
  //   ?layout=portrait|landscape|desktop   force the phone layout (screenshots, tests)
  //   ?screenshot=<page>                   no start overlay, lively fake state, that page open
  //   ?selftest                            play a note and report (tests/run-web-check.ps1)
  const query = new URLSearchParams(location.search);
  const androidApp = !!window.AndroidMidi;
  if (query.get('layout')) window.__rqLayout = query.get('layout');
  const screenshotPage = query.get('screenshot');
  if (screenshotPage) { try { localStorage.setItem('resonique.page', screenshotPage); } catch (e) { /* ignore */ } }

  // ---------------------------------------------------------- the bridge ---
  const listeners = {};
  const handlers = {};
  const toUi = (ev, data) => queueMicrotask(() => (listeners[ev] || []).forEach(f => f(data)));
  window.__JUCE__ = {
    backend: {
      addEventListener(ev, fn) { (listeners[ev] = listeners[ev] || []).push(fn); },
      emitEvent(ev, data) { const h = handlers[ev]; if (h) h(data || {}); },   // synchronous: file pickers keep the click's activation
    },
  };
  const message = (text) => toUi('message', { text });
  const store = {
    get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); return true; } catch (e) { return false; } },
  };
  const PARAM_INDEX = Object.fromEntries(PARAM_DEFS.map((d, i) => [d.id, i]));
  const TABLE_PARAMS = new Set(['instrument', 'hammerHardness', 'inharmonicity', 'stringStiffness', 'stringDecay', 'hammerPosition',
    'hammerMass', 'feltStiffness', 'feltExponent', 'unisonDetune', 'damperEfficiency', 'soundboardSize', 'soundboardDamping',
    'temperament', 'temperamentRoot', 'concertPitch', 'stretch']);   // mirrors paramAffectsTables() in ParamTable.h

  // ------------------------------------------------------- preset values ---
  // params may name a choice ("Upright") instead of its index, like the factory file does
  function readParams(block, into) {
    let found = 0;
    for (const id in (block || {})) {
      const d = PARAM_BY_ID[id];
      if (!d || d.noPreset) continue;
      let v = block[id];
      if (typeof v === 'string' && d.type === 'choice') { const i = d.choices.indexOf(v); if (i < 0) continue; v = i; }
      v = Number(v);
      if (isNaN(v)) continue;
      into[id] = clamp(v, d.min, d.max);
      found++;
    }
    return found;
  }
  const FACTORY = [];
  for (const cat of FACTORY_JSON.categories || []) {
    for (const p of cat.presets || []) {
      const values = {};
      readParams(p.params, values);
      FACTORY.push({ id: `factory:${FACTORY.length}`, name: p.name, category: cat.name, description: p.description || '', factory: true,
                     values, tuning: p.tuning || null });
    }
  }
  function loadUserPresets() { try { return JSON.parse(store.get('resonique.presets') || '[]'); } catch (e) { return []; } }
  function saveUserPresets(list) { store.set('resonique.presets', JSON.stringify(list)); }
  let favorites = new Set();
  try { favorites = new Set(JSON.parse(store.get('resonique.favorites') || '[]')); } catch (e) { /* ignore */ }

  // ---------------------------------------------------------------- state ---
  const values = defaultParamValues();
  let current = { id: '', name: '', category: '', description: '' };
  const tuning = { detune: new Array(88).fill(0), scala: '', scalaText: '', scalaNotes: 0 };

  // ------------------------------------------------------------- wasm ---
  function wasmBytes() {
    const bin = atob(RESONIQUE_WASM_B64);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return bytes;
  }
  // stub every import the module asks for (a standalone build asks for little or nothing)
  function wasmImports(module) {
    const imports = {};
    for (const imp of WebAssembly.Module.imports(module)) {
      imports[imp.module] = imports[imp.module] || {};
      if (imp.kind === 'function') imports[imp.module][imp.name] = (...a) => { if (imp.name === 'proc_exit') throw new Error('wasm exit'); return 0; };
      else if (imp.kind === 'memory') imports[imp.module][imp.name] = new WebAssembly.Memory({ initial: 1024 });
    }
    return imports;
  }
  // Engine: a thin JS wrapper around one instance's exports (shared by the main thread and the worklet)
  function makeEngine(instance, sampleRate) {
    const ex = instance.exports;
    if (ex._initialize) ex._initialize();   // once per instance (the exports object is frozen in browsers, so no marker on it)
    if (ex.rq_num_params() !== PARAM_DEFS.length) throw new Error(`parameter table mismatch: wasm ${ex.rq_num_params()} vs UI ${PARAM_DEFS.length}`);
    const ptr = ex.rq_create(sampleRate);
    const mem = () => ex.memory.buffer;
    const paramsPtr = ex.rq_params(ptr);
    const nParams = PARAM_DEFS.length;
    const floats = (p, n) => new Float32Array(mem(), p, n);
    const tableBytes = ex.rq_tables_bytes();
    const e = {
      ex, ptr, sampleRate, maxFrames: ex.rq_max_frames(), tableBytes,
      params() { return floats(paramsPtr, nParams); },
      apply() { ex.rq_apply_params(ptr); },
      render(frames) { ex.rq_render(ptr, frames); return [floats(ex.rq_out_l(ptr), frames), floats(ex.rq_out_r(ptr), frames)]; },
      noteOn(n, v) { ex.rq_note_on(ptr, n, v); }, noteOff(n) { ex.rq_note_off(ptr, n); },
      cc(c, v) { ex.rq_cc(ptr, c, v); }, bend(semis) { ex.rq_bend(ptr, semis); },
      allNotesOff() { ex.rq_all_notes_off(ptr); }, panic() { ex.rq_panic(ptr); },
      // tables: build here (main thread) and copy out, or copy in and adopt (worklet)
      buildTables() { const p = ex.rq_build_tables(ptr); return new Uint8Array(mem(), p, tableBytes).slice(); },
      adoptOwnBuild() { ex.rq_adopt_tables(ptr); },
      receiveTables(bytes) { new Uint8Array(mem(), ex.rq_table_target(ptr), tableBytes).set(bytes); ex.rq_adopt_tables(ptr); },
      // tuning state
      setDetune(i, c) { ex.rq_set_detune(ptr, i, c); }, resetDetune() { ex.rq_reset_detune(ptr); },
      setScala(text) {
        const bytes = new TextEncoder().encode(text + '\0');
        const p = ex.rq_alloc(bytes.length);
        new Uint8Array(mem(), p, bytes.length).set(bytes);
        const n = ex.rq_set_scala(ptr, p);
        ex.rq_free(p);
        return n;
      },
      clearScala() { ex.rq_clear_scala(ptr); },
      scalaDescription() {
        const p = ex.rq_scala_description(ptr);
        const b = new Uint8Array(mem(), p);
        let n = 0; while (b[n] !== 0 && n < 200) n++;
        return new TextDecoder().decode(b.subarray(0, n));
      },
      stats() {
        return { voices: ex.rq_active_voices(ptr), partials: ex.rq_active_partials(ptr), peak: ex.rq_take_peak(ptr), sustain: ex.rq_sustain(ptr),
                 sostenoto: false, sostenuto: !!ex.rq_sostenuto(ptr), soft: ex.rq_soft(ptr) };
      },
    };
    return e;
  }

  // the worklet: the same wrapper, driven by messages from the host
  function synthWorkletModule() {
    // makeEngine and wasmImports are pasted in below as source (functions cannot cross processorOptions)
    const makeEngine = new Function('PARAM_DEFS', 'clamp', `return (${'__MAKE_ENGINE__'});`)(PARAM_DEFS_IN_WORKLET, (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v));
    const wasmImports = new Function(`return (${'__WASM_IMPORTS__'});`)();
    class ResoniqueProcessor extends AudioWorkletProcessor {
      constructor(options) {
        super();
        const o = options.processorOptions;
        this.engine = null;
        this.pending = [];
        this.sinceStats = 0;
        this.port.onmessage = (e) => this.onMessage(e.data);
        WebAssembly.instantiate(o.module, wasmImports(o.module)).then((inst) => {
          this.engine = makeEngine(inst, sampleRate);
          this.engine.params().set(o.params);
          this.engine.apply();
          if (o.tables) this.engine.receiveTables(new Uint8Array(o.tables));
          for (const m of this.pending) this.onMessage(m);
          this.pending = [];
          this.port.postMessage({ type: 'ready' });
        }).catch((err) => this.port.postMessage({ type: 'error', error: String(err && err.message || err) }));
      }
      onMessage(m) {
        const e = this.engine;
        if (!e) { this.pending.push(m); return; }
        switch (m.type) {
          case 'param': e.params()[m.index] = m.value; break;
          case 'params': { const p = e.params(); for (const i in m.values) p[i] = m.values[i]; break; }
          case 'noteOn': e.noteOn(m.note, m.vel); break;
          case 'noteOff': e.noteOff(m.note); break;
          case 'cc': e.cc(m.cc, m.value); break;
          case 'bend': e.bend(m.value); break;
          case 'allNotesOff': e.allNotesOff(); break;
          case 'panic': e.panic(); break;
          case 'tables': e.receiveTables(new Uint8Array(m.data)); break;
          default: break;
        }
      }
      process(inputs, outputs) {
        const e = this.engine;
        const out = outputs[0];
        if (!e || !out || !out[0]) return true;
        const frames = out[0].length;
        e.apply();
        const [L, R] = e.render(frames);
        out[0].set(L);
        if (out[1]) out[1].set(R);
        this.sinceStats += frames;
        if (this.sinceStats >= sampleRate / 30) {
          this.sinceStats = 0;
          const s = e.stats();
          s.type = 'stats';
          this.port.postMessage(s);
        }
        return true;
      }
    }
    registerProcessor('resonique-piano', ResoniqueProcessor);
  }

  // --------------------------------------------------------------- audio ---
  const audio = {
    ctx: null, node: null, port: null, mode: '', error: '', module: null, main: null, ready: false,
    post(m, transfer) { if (this.port) this.port.postMessage(m, transfer || []); },
    get running() { return !!this.ctx && this.ctx.state === 'running' && this.ready; },

    async init(sampleRate) {
      this.module = await WebAssembly.compile(wasmBytes());
      // the main-thread instance: builds the tables; also the fallback sound engine
      const inst = await WebAssembly.instantiate(this.module, wasmImports(this.module));
      this.main = makeEngine(inst, sampleRate);
      this.main.params().set(PARAM_DEFS.map(d => values[d.id]));
      this.main.apply();
      pushTuning(this.main);
    },

    async start() {
      const ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
      this.ctx = ctx;
      await this.init(ctx.sampleRate);
      ctx.onstatechange = sendConfig;
      let built = null;
      if (ctx.audioWorklet && location.protocol !== 'file:') {
        try {
          const src = `const PARAM_DEFS_IN_WORKLET = ${JSON.stringify(PARAM_DEFS.map(d => ({ id: d.id })))};\n(${synthWorkletModule.toString().replace("'__MAKE_ENGINE__'", JSON.stringify(makeEngine.toString())).replace("'__WASM_IMPORTS__'", JSON.stringify(wasmImports.toString()))})();`;
          const url = URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
          try { await ctx.audioWorklet.addModule(url); } finally { URL.revokeObjectURL(url); }
          const tables = this.main.buildTables();
          this.main.adoptOwnBuild();
          const node = new AudioWorkletNode(ctx, 'resonique-piano', {
            numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
            processorOptions: { module: this.module, params: PARAM_DEFS.map(d => values[d.id]), tables: tables.buffer },
          });
          node.port.onmessage = (e) => onDspMessage(e.data);
          built = { node, port: node.port, mode: 'AudioWorklet' };
        } catch (e) { console.warn('AudioWorklet unavailable, using the main thread', e); }
      }
      if (!built) built = this.scriptProcessor(ctx);
      this.node = built.node; this.port = built.port; this.mode = built.mode;
      this.node.connect(ctx.destination);
      sendConfig();
    },

    // Chrome / Edge block worklet modules on file://: run the main-thread engine through a ScriptProcessor
    scriptProcessor(ctx) {
      const main = this.main;
      let since = 0;
      const node = ctx.createScriptProcessor(1024, 0, 2);
      node.onaudioprocess = (ev) => {
        const L = ev.outputBuffer.getChannelData(0), R = ev.outputBuffer.getChannelData(1);
        for (let off = 0; off < L.length; off += 128) {
          main.apply();
          const [l, r] = main.render(128);
          L.set(l, off); R.set(r, off);
        }
        since += L.length;
        if (since >= ctx.sampleRate / 30) { since = 0; const s = main.stats(); s.type = 'stats'; onDspMessage(s); }
      };
      const port = { postMessage: (m) => {
        switch (m.type) {
          case 'param': main.params()[m.index] = m.value; break;
          case 'params': { const p = main.params(); for (const i in m.values) p[i] = m.values[i]; break; }
          case 'noteOn': main.noteOn(m.note, m.vel); break;
          case 'noteOff': main.noteOff(m.note); break;
          case 'cc': main.cc(m.cc, m.value); break;
          case 'bend': main.bend(m.value); break;
          case 'allNotesOff': main.allNotesOff(); break;
          case 'panic': main.panic(); break;
          case 'tables': break;   // the main instance adopted its own build already
          default: break;
        }
      } };
      this.ready = true;
      return { node, port, mode: 'ScriptProcessor' };
    },
  };

  let building = false;
  function onDspMessage(m) {
    if (m.type === 'ready') { audio.ready = true; sendConfig(); return; }
    if (m.type === 'error') { audio.error = m.error; message(`Audio engine error: ${m.error}`); sendConfig(); return; }
    if (m.type === 'stats') {
      toUi('stats', { voices: m.voices, partials: m.partials, peak: m.peak, sustain: m.sustain, sostenuto: m.sostenuto, soft: m.soft,
                      cpu: 0, building, sampleRate: audio.ctx ? audio.ctx.sampleRate : 0 });
    }
  }

  // ---- note tables: rebuilt on the main thread when a physical parameter or the tuning changes ----
  let rebuildTimer = 0;
  function scheduleRebuild() {
    if (!audio.main) return;
    building = true;
    clearTimeout(rebuildTimer);
    rebuildTimer = setTimeout(() => {
      rebuildTimer = 0;
      const main = audio.main;
      main.params().set(PARAM_DEFS.map(d => values[d.id]));
      const bytes = main.buildTables();
      if (audio.mode === 'AudioWorklet') audio.post({ type: 'tables', data: bytes.buffer }, [bytes.buffer]);
      main.adoptOwnBuild();
      building = false;
    }, 60);
  }
  function pushTuning(engine) {
    engine.resetDetune();
    tuning.detune.forEach((c, i) => { if (c) engine.setDetune(i, c); });
    if (tuning.scalaText) { tuning.scalaNotes = engine.setScala(tuning.scalaText); tuning.scala = engine.scalaDescription(); }
    else engine.clearScala();
  }
  function sendTuning() { toUi('tuning', { detune: tuning.detune.slice(), scala: tuning.scala, scalaNotes: tuning.scalaNotes, valid: tuning.scalaNotes > 0 }); }

  // ------------------------------------------------------------ parameters ---
  function setParamValue(id, v, fromPreset) {
    const d = PARAM_BY_ID[id];
    if (!d) return;
    v = clamp(Number(v), d.min, d.max);
    values[id] = v;
    audio.post({ type: 'param', index: PARAM_INDEX[id], value: v });
    if (TABLE_PARAMS.has(id)) scheduleRebuild();
  }
  handlers.setParam = ({ id, value }) => setParamValue(id, value);
  handlers.setParams = ({ values: obj }) => { for (const id in obj) setParamValue(id, obj[id]); };
  handlers.gesture = () => {};
  handlers.paramMenu = ({ id }) => { const d = PARAM_BY_ID[id]; if (d) { setParamValue(id, d.def); toUi('params', { [id]: d.def }); } };

  // --------------------------------------------------------------- notes ---
  handlers.noteOn = ({ note, vel }) => { audio.post({ type: 'noteOn', note, vel }); toUi('notes', { on: [note], off: [] }); };
  handlers.noteOff = ({ note }) => { audio.post({ type: 'noteOff', note }); toUi('notes', { on: [], off: [note] }); };
  handlers.panic = () => { audio.post({ type: 'panic' }); };
  handlers.pedal = ({ cc, value }) => audio.post({ type: 'cc', cc, value });

  // ------------------------------------------------------------- presets ---
  function sendPresetList() {
    const user = loadUserPresets().map(p => ({ id: p.id, name: p.name, category: p.category || 'User', description: p.description || '', factory: false }));
    toUi('presetList', { presets: FACTORY.map(p => ({ id: p.id, name: p.name, category: p.category, description: p.description, factory: true })).concat(user),
                         folder: 'this browser (localStorage)', favorites: Array.from(favorites) });
  }
  function sendPreset() { toUi('preset', current); }
  function applyValues(vals, tuningBlock) {
    const changed = {};
    for (const d of PARAM_DEFS) {
      if (d.noPreset) continue;
      const v = vals[d.id] !== undefined ? vals[d.id] : d.def;
      values[d.id] = v; changed[d.id] = v;
    }
    audio.post({ type: 'params', values: Object.fromEntries(PARAM_DEFS.map((d, i) => [i, values[d.id]])) });
    tuning.detune = new Array(88).fill(0);
    tuning.scalaText = ''; tuning.scala = ''; tuning.scalaNotes = 0;
    if (tuningBlock && typeof tuningBlock === 'object') {
      if (Array.isArray(tuningBlock.detune)) tuningBlock.detune.slice(0, 88).forEach((c, i) => { tuning.detune[i] = clamp(Number(c) || 0, -100, 100); });
      if (typeof tuningBlock.scala === 'string' && tuningBlock.scala) tuning.scalaText = tuningBlock.scala;
    }
    if (audio.main) pushTuning(audio.main);
    toUi('params', changed);
    sendTuning();
    scheduleRebuild();
  }
  function loadById(id) {
    const f = FACTORY.find(p => p.id === id);
    if (f) { applyValues(f.values, f.tuning); current = { id: f.id, name: f.name, category: f.category, description: f.description }; sendPreset(); return true; }
    const u = loadUserPresets().find(p => p.id === id);
    if (u) { applyValues(u.params || {}, u.tuning); current = { id: u.id, name: u.name, category: u.category || 'User', description: u.description || '' }; sendPreset(); return true; }
    message('Preset not found');
    return false;
  }
  handlers.loadPreset = ({ id }) => loadById(id);
  function presetJson(name, category) {
    const params = {};
    for (const d of PARAM_DEFS) if (!d.noPreset) params[d.id] = Math.round(values[d.id] * 1e6) / 1e6;
    const t = {};
    if (tuning.detune.some(c => c)) t.detune = tuning.detune.slice();
    if (tuning.scalaText) t.scala = tuning.scalaText;
    const j = { format: 'Resonique Preset', version: 1, name, category: category || 'User', params };
    if (Object.keys(t).length) j.tuning = t;
    return j;
  }
  handlers.savePreset = ({ name, category, overwrite }) => {
    name = (name || '').trim();
    if (!name) { toUi('saveResult', { ok: false, exists: false, name, error: 'Enter a name for the preset' }); return; }
    const list = loadUserPresets();
    const id = `user:${name}`;
    const existing = list.findIndex(p => p.id === id);
    if (existing >= 0 && !overwrite) { toUi('saveResult', { ok: false, exists: true, name, error: '' }); return; }
    const j = presetJson(name, (category || '').trim() || 'User');
    const entry = { id, name, category: j.category, params: j.params, tuning: j.tuning || null };
    if (existing >= 0) list[existing] = entry; else list.push(entry);
    saveUserPresets(list);
    current = { id, name, category: j.category, description: '' };
    toUi('saveResult', { ok: true, exists: false, name, error: '' });
    sendPresetList(); sendPreset();
  };
  handlers.deletePreset = ({ id }) => {
    const list = loadUserPresets();
    const i = list.findIndex(p => p.id === id);
    if (i < 0) { message('Only user presets can be deleted'); return; }
    list.splice(i, 1); saveUserPresets(list);
    favorites.delete(id); store.set('resonique.favorites', JSON.stringify(Array.from(favorites)));
    if (current.id === id) { current = { id: '', name: current.name, category: 'User', description: '' }; sendPreset(); }
    message('Preset deleted');
    sendPresetList();
  };
  handlers.setFavourite = ({ id, on }) => { if (on) favorites.add(id); else favorites.delete(id); store.set('resonique.favorites', JSON.stringify(Array.from(favorites))); sendPresetList(); };
  handlers.initPreset = () => { applyValues({}, null); current = { id: '', name: 'Init', category: 'User', description: 'Every parameter at its default' }; sendPreset(); };
  handlers.factoryReset = () => { favorites = new Set(); store.set('resonique.favorites', '[]'); loadById('factory:0'); message('Factory defaults restored (your saved presets are untouched)'); sendPresetList(); };
  handlers.openPresetFolder = () => message(androidApp
    ? 'SAVE keeps presets in the app (they are part of your phone backup); EXPORT writes a .rqpreset file, IMPORT opens one from your phone or a cloud drive.'
    : 'Presets are stored in this browser. Use EXPORT to write a .rqpreset file.');
  // import / export: the same .rqpreset files as the plugin
  function pickFile(accept, onText) {
    const input = document.createElement('input');
    input.type = 'file'; input.accept = accept; input.style.display = 'none';
    input.addEventListener('change', () => {
      const f = input.files && input.files[0];
      if (f) f.text().then(t => onText(t, f.name)).catch(err => message(String(err)));
      input.remove();
    });
    document.body.appendChild(input);
    input.click();
  }
  handlers.loadPresetFile = () => pickFile('.rqpreset,application/json', (text, fileName) => {
    let j;
    try { j = JSON.parse(text); } catch (e) { message(`Not a valid preset file: ${fileName}`); return; }
    const vals = {};
    if (!j || typeof j !== 'object' || readParams(j.params, vals) === 0) { message(`No Resonique settings found in ${fileName}`); return; }
    applyValues(vals, j.tuning);
    current = { id: `file:${fileName}`, name: j.name || fileName.replace(/\.rqpreset$/i, ''), category: 'File', description: j.description || '' };
    sendPreset();
    message(`Loaded ${fileName}`);
  });
  handlers.exportPreset = () => {
    const name = current.name && current.name !== '—' ? current.name : 'Resonique preset';
    const j = presetJson(name, current.category || 'User');
    const text = JSON.stringify(j, null, 1);
    const fileName = `${name.replace(/[\\/:*?"<>|]+/g, '_')}.rqpreset`;
    if (window.AndroidFiles) {   // the Android app: a "save as" dialog (WebView cannot download a blob)
      window.AndroidFiles.saveTextFile(fileName, 'application/json', text);
      return;
    }
    const blob = new Blob([text], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = fileName;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 2000);
    message(`Exported ${a.download}`);
  };

  // -------------------------------------------------------------- tuning ---
  handlers.setDetune = ({ note, cents }) => {
    if (note < 0 || note >= 88) return;
    tuning.detune[note] = clamp(Number(cents) || 0, -100, 100);
    if (audio.main) audio.main.setDetune(note, tuning.detune[note]);
    scheduleRebuild();
  };
  handlers.resetDetune = () => { tuning.detune = new Array(88).fill(0); if (audio.main) audio.main.resetDetune(); sendTuning(); scheduleRebuild(); };
  handlers.loadScala = () => pickFile('.scl,text/plain', (text, fileName) => {
    if (!audio.main) { message('Start the audio first'); return; }
    const n = audio.main.setScala(text);
    if (!n) { message(`Scala: cannot read ${fileName}`); return; }
    tuning.scalaText = text; tuning.scalaNotes = n; tuning.scala = audio.main.scalaDescription() || fileName;
    setParamValue('temperament', TEMPERAMENTS.length - 1);
    toUi('params', { temperament: TEMPERAMENTS.length - 1 });
    sendTuning(); scheduleRebuild();
    message(`Loaded scale ${fileName}`);
  });
  handlers.clearScala = () => { tuning.scalaText = ''; tuning.scala = ''; tuning.scalaNotes = 0; if (audio.main) audio.main.clearScala(); sendTuning(); scheduleRebuild(); };
  handlers.loadIR = () => message('Impulse-response reverb is not available in the browser build; the algorithmic rooms are.');
  handlers.clearIR = () => {};

  // ---------------------------------------------------------------- MIDI ---
  const midi = { access: null, selected: store.get('resonique.midi') || 'all', inputs: [] };
  function onMidi(ev) {
    const [status, d1, d2] = ev.data;
    const type = status & 0xf0;
    if (type === 0x90 && d2 > 0) handlers.noteOn({ note: d1, vel: d2 });
    else if (type === 0x80 || (type === 0x90 && d2 === 0)) handlers.noteOff({ note: d1 });
    else if (type === 0xb0) {
      if (d1 === 64 || d1 === 66 || d1 === 67) audio.post({ type: 'cc', cc: d1, value: d2 });
      else if (d1 === 120) audio.post({ type: 'panic' });
      else if (d1 === 123 || d1 === 121) audio.post({ type: 'allNotesOff' });
    } else if (type === 0xe0) {
      const v = ((d2 << 7) | d1) - 8192;
      audio.post({ type: 'bend', value: v / 8192 * values.pitchBendRange });
    }
  }
  function bindMidi() {
    if (!midi.access) return;
    midi.inputs = Array.from(midi.access.inputs.values());
    for (const inp of midi.inputs) inp.onmidimessage = (midi.selected === 'all' || midi.selected === inp.id) ? onMidi : null;
    renderMidiSelect();
  }
  let midiSelect = null;
  function renderMidiSelect() {
    if (!midiSelect) return;
    midiSelect.innerHTML = '';
    if (!midi.access) { midiSelect.appendChild(new Option('MIDI unavailable', 'all')); midiSelect.disabled = true; return; }
    midiSelect.disabled = false;
    midiSelect.appendChild(new Option(midi.inputs.length ? `All inputs (${midi.inputs.length})` : 'No MIDI devices', 'all'));
    midi.inputs.forEach(d => midiSelect.appendChild(new Option(d.name + (d.state === 'connected' ? '' : ' (off)'), d.id)));
    midiSelect.value = Array.from(midiSelect.options).some(o => o.value === midi.selected) ? midi.selected : 'all';
  }
  if (typeof navigator.requestMIDIAccess === 'function') {
    navigator.requestMIDIAccess({ sysex: false }).then((access) => {
      midi.access = access;
      access.onstatechange = bindMidi;
      bindMidi();
    }).catch(() => renderMidiSelect());
  }

  // ------------------------------------------------------- page additions ---
  // a MIDI input picker in the top bar and the start-audio overlay (browsers need a gesture)
  function addPageBits() {
    const controls = document.querySelector('.top-controls');
    if (controls) {
      const label = document.createElement('label');
      label.className = 'indicator';
      label.innerHTML = 'MIDI IN <select id="midi-select" style="margin-left:6px;max-width:170px;background:var(--screen);color:var(--text);border:1px solid var(--panel-edge);border-radius:5px;padding:3px 6px;font:11px var(--sans)"></select>';
      controls.insertBefore(label, controls.firstChild);
      midiSelect = label.querySelector('select');
      midiSelect.addEventListener('change', () => { midi.selected = midiSelect.value; store.set('resonique.midi', midi.selected); bindMidi(); });
      midiSelect.addEventListener('keydown', (e) => e.stopPropagation());
      renderMidiSelect();
    }
    const style = document.createElement('style');
    style.textContent = `
      .rq-overlay { position: fixed; inset: 0; z-index: 30; display: flex; align-items: center; justify-content: center; background: rgba(5,6,8,.78); backdrop-filter: blur(4px); }
      .rq-overlay.hidden { display: none; }
      .rq-card { display: flex; flex-direction: column; align-items: center; gap: 8px; text-align: center; padding: 16px; color: var(--text); font-family: var(--sans); }
      .rq-power { width: 96px; height: 96px; border-radius: 50%; background: radial-gradient(circle at 40% 30%, #2e2a30, #15131a); border: 2px solid var(--accent-dim); color: var(--accent); display: flex; align-items: center; justify-content: center; cursor: pointer; box-shadow: 0 0 30px rgba(217,84,74,.15); }
      .rq-power:hover { border-color: var(--accent); box-shadow: 0 0 40px var(--accent-glow); }
      .rq-power:disabled { opacity: .5; cursor: wait; }
      .rq-title { margin: 10px 0 0; font-weight: 700; letter-spacing: .2em; text-transform: uppercase; font-size: 13px; }
      .rq-sub { margin: 0; color: var(--muted); font-size: 13px; max-width: 340px; line-height: 1.5; }`;
    document.head.appendChild(style);
    const overlay = document.createElement('div');
    overlay.className = 'rq-overlay';
    overlay.innerHTML = `<div class="rq-card"><button class="rq-power" type="button" aria-label="Start audio"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M12 3v9"/><path d="M6.3 6.3a8 8 0 1 0 11.4 0"/></svg></button><p class="rq-title">Resonique</p><p class="rq-sub">A physically modelled piano running in your browser. Click to start the audio, then play with a MIDI keyboard, the keys, or A–' on the computer keyboard.</p></div>`;
    document.body.appendChild(overlay);
    const btn = overlay.querySelector('button');
    const start = async () => {
      btn.disabled = true;
      try { await audio.start(); overlay.classList.add('hidden'); }
      catch (e) { console.error(e); overlay.classList.remove('hidden'); overlay.querySelector('.rq-sub').textContent = `Could not start the audio: ${e && e.message ? e.message : e}`; btn.disabled = false; }
    };
    btn.addEventListener('click', start);
    // the Android app allows audio without a gesture (and screenshots want the panel): no overlay
    if (androidApp || screenshotPage) overlay.classList.add('hidden');
    if (androidApp) start();
  }

  // The Android app calls these when it leaves / returns to the screen: no hanging notes, no
  // audio running in the background.
  window.__rqWebHost = {
    get audioMode() { return audio.mode; }, get running() { return audio.running; },
    get time() { return audio.ctx ? audio.ctx.currentTime : -1; },   // advances only while the device renders audio
    suspend() {
      audio.post({ type: 'panic' });
      toUi('notes', { on: [], off: Array.from({ length: 88 }, (_, i) => 21 + i) });
      if (audio.ctx && audio.ctx.state === 'running') audio.ctx.suspend().then(sendConfig).catch(() => {});
    },
    resume() {
      if (audio.ctx && audio.ctx.state === 'suspended') audio.ctx.resume().then(sendConfig).catch(() => {});
    },
    // the Android app: a file written by AndroidFiles.saveTextFile
    saved(name) { message(name ? `Exported ${name}` : 'Export cancelled'); },
  };

  function sendConfig() {
    const ctx = audio.ctx;
    toUi('config', { sampleRate: ctx ? ctx.sampleRate : 0, blockSize: audio.mode === 'ScriptProcessor' ? 1024 : 128, wrapper: 'Web', voices: NUM_VOICES,
                     version: RESONIQUE_VERSION, audio: ctx ? `${audio.mode || 'starting'} · ${(ctx.sampleRate / 1000).toFixed(1)} kHz` : 'click the power button to start', running: audio.running });
  }

  handlers.uiReady = () => {
    document.body.classList.add('web');
    addPageBits();
    sendConfig();
    sendPresetList();
    const first = FACTORY[0];
    if (first) { current = { id: first.id, name: first.name, category: first.category, description: first.description }; for (const id in first.values) values[id] = first.values[id]; }
    toUi('params', Object.assign({}, values));
    sendPreset();
    sendTuning();
    toUi('ir', { name: '' });
    if (query.has('selftest')) selfTest();
    if (screenshotPage) {   // a playing instrument for the store screenshots
      toUi('config', { sampleRate: 48000, blockSize: 128, wrapper: 'Web', voices: NUM_VOICES, version: RESONIQUE_VERSION, audio: 'AudioWorklet · 48.0 kHz', running: true });
      toUi('stats', { voices: 7, partials: 486, peak: 0.42, sustain: 1, sostenuto: false, soft: 0, cpu: 0.11, building: false, sampleRate: 48000 });
      toUi('notes', { on: [48, 55, 60, 64, 67], off: [] });
    }
  };

  // ?selftest: starts the audio without a click (needs a browser that allows it, e.g. headless Edge
  // with --autoplay-policy=no-user-gesture-required), plays a note and writes the outcome into the
  // page for tests/run-web-check.ps1 to read.
  async function selfTest() {
    const el = document.createElement('pre');
    el.id = 'rq-selftest';
    el.style.cssText = 'position:fixed;left:0;bottom:0;z-index:99;margin:0;padding:4px;background:#000;color:#0f0;font:12px monospace';
    document.body.appendChild(el);
    const report = (o) => { el.textContent = JSON.stringify(o); };
    try {
      let last = null, maxPeak = 0;
      window.__JUCE__.backend.addEventListener('stats', (s) => { last = s; if (s.peak > maxPeak) maxPeak = s.peak; });
      await audio.start();
      const t0 = performance.now();
      while (!audio.ready && !audio.error && performance.now() - t0 < 5000) await new Promise(r => setTimeout(r, 50));
      handlers.noteOn({ note: 60, vel: 100 });
      await new Promise(r => setTimeout(r, 1500));
      report({ ok: !!last && last.voices > 0 && maxPeak > 0.001, mode: audio.mode, ready: audio.ready, state: audio.ctx.state,
               sampleRate: audio.ctx.sampleRate, voices: last ? last.voices : -1, partials: last ? last.partials : -1, maxPeak, error: audio.error });
    } catch (e) { report({ ok: false, error: String(e && e.message || e) }); }
  }
})();
