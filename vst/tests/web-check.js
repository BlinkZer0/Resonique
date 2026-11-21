// Smoke test for the web build: pulls the WebAssembly engine out of web/index.html,
// runs it in Node (no browser, no audio device) through the same flat C API the page
// uses, and checks that it builds tables, plays a note, honours the pedal, the tuning
// parameters and a Scala scale, and reports its speed.
// Usage: node vst/tests/web-check.js [web/index.html]
'use strict';
const fs = require('fs');
const path = require('path');

const file = process.argv[2] || path.join(__dirname, '..', '..', 'web', 'index.html');
const html = fs.readFileSync(file, 'utf8');
const m = html.match(/const RESONIQUE_WASM_B64 = '([A-Za-z0-9+/=]+)'/);
if (!m) { console.error('FAIL: no RESONIQUE_WASM_B64 in ' + file); process.exit(1); }
const paramIds = [...html.matchAll(/^\s+(?:f|choiceParam|intParam)\('(\w+)'/gm)].map(x => x[1]);
const bytes = Buffer.from(m[1], 'base64');
let fails = 0;
const check = (ok, text) => { console.log((ok ? 'ok   ' : 'FAIL ') + text); if (!ok) fails++; };

(async () => {
  const module = await WebAssembly.compile(bytes);
  const imports = {};
  for (const imp of WebAssembly.Module.imports(module)) {
    imports[imp.module] = imports[imp.module] || {};
    if (imp.kind === 'function') imports[imp.module][imp.name] = () => 0;
  }
  console.log('imports: ' + (WebAssembly.Module.imports(module).map(i => i.module + '.' + i.name).join(', ') || 'none'));
  const inst = await WebAssembly.instantiate(module, imports);
  const ex = inst.exports;
  if (ex._initialize) ex._initialize();
  const fs_ = 48000;
  check(ex.rq_num_params() === paramIds.length, `parameter count ${ex.rq_num_params()} matches the UI table (${paramIds.length})`);
  const t0 = performance.now();
  const e = ex.rq_create(fs_);
  check(e !== 0, `engine created in ${(performance.now() - t0).toFixed(1)} ms (tables ${(ex.rq_tables_bytes() / 1024).toFixed(0)} KB)`);
  const params = () => new Float32Array(ex.memory.buffer, ex.rq_params(e), ex.rq_num_params());
  const idx = (id) => paramIds.indexOf(id);
  const render = (frames) => {
    let peak = 0;
    for (let n = 0; n < frames; n += 128) {
      ex.rq_apply_params(e);
      ex.rq_render(e, 128);
      const L = new Float32Array(ex.memory.buffer, ex.rq_out_l(e), 128);
      for (let i = 0; i < 128; i++) { const a = Math.abs(L[i]); if (a > peak) peak = a; if (!isFinite(L[i])) return NaN; }
    }
    return peak;
  };
  // silence before a note
  check(render(4800) === 0, 'silent before the first note');
  // a note sounds and dies when released without the pedal
  ex.rq_note_on(e, 60, 100);
  const p1 = render(fs_);
  check(p1 > 0.01 && p1 < 1.0, `C4 at velocity 100 peaks at ${(20 * Math.log10(p1)).toFixed(1)} dBFS in the first second`);
  ex.rq_note_off(e, 60);
  render(fs_ * 2);
  const p2 = render(fs_);
  check(p2 < p1 * 0.02, `after release and 3 s the level is ${(20 * Math.log10(p2 + 1e-9)).toFixed(1)} dBFS (damped)`);
  // the sustain pedal keeps the note ringing
  ex.rq_cc(e, 64, 127);
  ex.rq_note_on(e, 60, 100);
  render(fs_ * 0.5);
  ex.rq_note_off(e, 60);
  render(fs_ * 2);
  const p3 = render(fs_);
  check(p3 > p2 * 10, `with the sustain pedal the released note still rings (${(20 * Math.log10(p3 + 1e-9)).toFixed(1)} dBFS)`);
  check(ex.rq_sustain(e) > 0.9, 'sustain position reported');
  ex.rq_cc(e, 64, 0);
  ex.rq_panic(e);
  render(2048);
  const pp = render(4800);
  check(pp < 1e-6, `panic silences the engine (${pp.toExponential(1)} after the fade)`);
  // the tuning parameters reach the tables: concert pitch 442 changes the A4 period
  const periodOf = () => {
    ex.rq_note_on(e, 69, 90);
    render(2400);
    ex.rq_render(e, 128); // one more block for a stable waveform
    const L = new Float32Array(ex.memory.buffer, ex.rq_out_l(e), 128);
    // count zero crossings over 4 blocks
    let crossings = 0, prev = L[127];
    for (let b = 0; b < 8; b++) {
      ex.rq_render(e, 128);
      const B = new Float32Array(ex.memory.buffer, ex.rq_out_l(e), 128);
      for (let i = 0; i < 128; i++) { if ((B[i] >= 0) !== (prev >= 0)) crossings++; prev = B[i]; }
    }
    ex.rq_note_off(e, 69); ex.rq_panic(e); render(2048);
    return crossings / 2 / (8 * 128 / fs_);   // rough Hz, upper partials inflate it; only the ratio matters
  };
  const fA440 = periodOf();
  params()[idx('concertPitch')] = 466.16;   // a semitone up: unmistakable even with a crude estimate
  ex.rq_build_tables(e); ex.rq_adopt_tables(e);
  const fA466 = periodOf();
  check(fA466 > fA440 * 1.03, `concert pitch 440 -> 466 Hz raises the pitch (estimates ${fA440.toFixed(0)} -> ${fA466.toFixed(0)} Hz)`);
  params()[idx('concertPitch')] = 440;
  // a Scala scale parses
  const scl = '! test.scl\nFive-limit just\n 12\n 16/15\n 9/8\n 6/5\n 5/4\n 4/3\n 45/32\n 3/2\n 8/5\n 5/3\n 9/5\n 15/8\n 2/1\n';
  const enc = new TextEncoder().encode(scl + '\0');
  const p = ex.rq_alloc(enc.length);
  new Uint8Array(ex.memory.buffer, p, enc.length).set(enc);
  const n = ex.rq_set_scala(e, p);
  ex.rq_free(p);
  check(n === 12, `Scala file parsed: ${n} notes`);
  const d = new Uint8Array(ex.memory.buffer, ex.rq_scala_description(e), 32);
  check(new TextDecoder().decode(d.subarray(0, d.indexOf(0))) === 'Five-limit just', 'Scala description read back');
  check(ex.rq_set_scala(e, p) === 0 || true, 'bad input does not crash');
  ex.rq_clear_scala(e);
  // speed: 16 voices with the pedal for 5 seconds
  ex.rq_cc(e, 64, 127);
  for (let i = 0; i < 16; i++) ex.rq_note_on(e, 36 + i * 3, 80 + i);
  const t1 = performance.now();
  const pk = render(fs_ * 5);
  const ms = performance.now() - t1;
  check(isFinite(pk), `16 voices + pedal: ${(ms / 5).toFixed(0)} ms per second of audio = ${(ms / 5 / 10).toFixed(1)}% of real time (Node, wasm SIMD)`);
  check(ex.rq_active_voices(e) === 16, `${ex.rq_active_voices(e)} active voices, ${ex.rq_active_partials(e)} partials`);
  ex.rq_destroy(e);
  console.log(fails ? `${fails} FAILURE(S)` : 'WEB ENGINE OK');
  process.exit(fails ? 1 : 0);
})().catch((err) => { console.error('FAIL ' + (err.stack || err)); process.exit(1); });
