// ============================================================================
//  UI wiring (family pattern): talks only to JuceEngine; the host owns all
//  parameter state and everything flows back in as events; the whole panel
//  scales to fit the window.
// ============================================================================
(function () {
  'use strict';

  const $ = (sel) => document.querySelector(sel);
  function el(tag, cls, html) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (html !== undefined) e.innerHTML = html;
    return e;
  }
  const esc = (s) => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  // ---------------------------------------------------------------- state ---
  const values = defaultParamValues();
  let presetList = [];
  let favorites = new Set();
  let presetFolder = '';
  let current = { id: '', name: '—', category: '', description: '' };
  let presetDirty = false;
  let activeCat = null;
  let search = '';
  const FAV_CAT = '★ Favourites';

  const engine = new JuceEngine();
  const knobs = {};
  const bound = {};
  const dragging = new Set();
  const lastLocal = {};
  const refreshers = [];          // functions run when parameters change: (changedIds|null)

  // ---------------------------------------------------- parameter plumbing ---
  function applyValue(id, v) {
    values[id] = v;
    if (knobs[id]) knobs[id].forEach(k => k.setValue(v));
    (bound[id] || []).forEach(c => c.setValue(v));
  }
  function setParam(id, v) {
    lastLocal[id] = performance.now();
    applyValue(id, v);
    engine.setParam(id, v);
    if (!PARAM_BY_ID[id].noPreset && !presetDirty) { presetDirty = true; renderPresetName(); }
    refreshAll([id]);
  }
  function setParams(obj) {
    for (const id in obj) { lastLocal[id] = performance.now(); applyValue(id, obj[id]); }
    engine.setParams(obj);
    if (!presetDirty) { presetDirty = true; renderPresetName(); }
    refreshAll(Object.keys(obj));
  }
  function refreshAll(ids) { refreshers.forEach(fn => fn(ids)); }

  function control(Cls, id, ...extra) {
    const c = new Cls(PARAM_BY_ID[id], values[id], (v) => setParam(id, v), ...extra);
    (bound[id] = bound[id] || []).push(c);
    c.el.dataset.param = id;
    return c;
  }
  function addKnob(row, id, cls) {
    const k = new Knob(PARAM_BY_ID[id], values[id],
      (v) => setParam(id, v),
      (begin) => {
        if (begin) { dragging.add(id); engine.beginGesture(id); }
        else { dragging.delete(id); engine.endGesture(id); }
      });
    (knobs[id] = knobs[id] || []).push(k);
    k.el.dataset.param = id;
    if (cls) k.el.classList.add(cls);
    row.appendChild(k.el);
    return k;
  }
  function makePanel(title, extraHeader, cls) {
    const p = el('div', 'panel' + (cls ? ' ' + cls : ''));
    const h = el('div', 'panel-head', `<span class="panel-title">${title}</span>`);
    if (extraHeader) h.appendChild(extraHeader);
    p.appendChild(h);
    return p;
  }
  function knobRow(parent, ids) {
    const row = el('div', 'knob-row');
    ids.forEach(id => addKnob(row, id));
    parent.appendChild(row);
    return row;
  }
  function note(parent, text) { parent.appendChild(el('div', 'panel-note', text)); }

  engine.on('params', (obj) => {
    const now = performance.now();
    const changed = [];
    for (const id in obj) {
      if (!PARAM_BY_ID[id]) continue;
      if (dragging.has(id) || (lastLocal[id] !== undefined && now - lastLocal[id] < 300)) continue;
      applyValue(id, obj[id]);
      changed.push(id);
    }
    if (changed.length) refreshAll(changed);
  });

  // ------------------------------------------------------------ presets ----
  engine.on('presetList', ({ presets, folder, favorites: favs }) => {
    presetList = presets;
    presetFolder = folder;
    favorites = new Set(favs || []);
    if (!activeCat || !categories().includes(activeCat)) activeCat = current.category || (presets[0] && presets[0].category);
    renderBrowser();
    renderPresetName();
  });
  engine.on('preset', (p) => {
    const changed = p.id !== current.id || p.name !== current.name;
    current = p;
    if (changed) presetDirty = false;
    if (p.category && categories().includes(p.category) && activeCat !== FAV_CAT) activeCat = p.category;
    renderPresetName();
    renderBrowser();
  });
  engine.on('message', ({ text }) => logMidi(text));

  function categories() {
    const cats = [];
    for (const p of presetList) if (!cats.includes(p.category)) cats.push(p.category);
    if (!presetList.some(p => !p.factory) && !cats.includes('User')) cats.push('User');
    cats.push(FAV_CAT);
    return cats;
  }
  function visiblePresets() {
    const q = search.trim().toLowerCase();
    return presetList.filter(p => {
      if (q) return p.name.toLowerCase().includes(q) || (p.description || '').toLowerCase().includes(q) || p.category.toLowerCase().includes(q);
      if (activeCat === FAV_CAT) return favorites.has(p.id);
      return p.category === activeCat;
    });
  }
  function loadPreset(p) {
    presetDirty = false;
    current = { id: p.id, name: p.name, category: p.category, description: p.description || '' };
    for (const id in lastLocal) delete lastLocal[id];
    renderPresetName();
    renderBrowser();
    engine.loadPreset(p.id);
  }
  function stepPreset(dir) {
    const list = visiblePresets().length && !search ? visiblePresets() : presetList;
    if (!list.length) return;
    const i = list.findIndex(p => p.id === current.id);
    const next = i < 0 ? 0 : (i + dir + list.length) % list.length;
    loadPreset(list[next]);
  }
  function renderPresetName() {
    $('#preset-name').textContent = current.name + (presetDirty ? ' *' : '');
    $('#preset-cat').textContent = (current.category || 'Preset').toUpperCase();
    $('#preset-fav').classList.toggle('on', favorites.has(current.id));
    $('#preset-desc').textContent = current.description || '';
  }
  const tabsEl = $('#cat-tabs'), gridEl = $('#preset-grid');
  function renderBrowser() {
    const cats = categories();
    tabsEl.innerHTML = '';
    for (const c of cats) {
      const n = c === FAV_CAT ? presetList.filter(p => favorites.has(p.id)).length : presetList.filter(p => p.category === c).length;
      const t = el('button', 'cat-tab', `${esc(c)}<span class="count">${n}</span>`);
      t.type = 'button';
      t.setAttribute('role', 'tab');
      t.classList.toggle('active', c === activeCat && !search);
      t.classList.toggle('has-current', c === current.category);
      t.addEventListener('click', () => { activeCat = c; search = ''; searchEl.value = ''; renderBrowser(); });
      tabsEl.appendChild(t);
    }
    gridEl.innerHTML = '';
    const items = visiblePresets();
    if (!items.length) {
      gridEl.appendChild(el('div', 'grid-empty', search ? `No presets match "<b>${esc(search)}</b>".`
        : activeCat === FAV_CAT ? 'No favourites yet. Click the <b>★</b> on a preset to keep it here.'
        : `No user presets yet. Click <b>SAVE</b> to store the current sound.<br>Preset files in <span class="path">${esc(presetFolder)}</span> show up here automatically.`));
      return;
    }
    for (const p of items) {
      const b = el('button', 'preset-btn', `<span class="pb-name">${esc(p.name)}</span>`);
      b.type = 'button';
      b.title = p.description ? `${p.name}\n${p.description}` : p.name;
      b.classList.toggle('selected', p.id === current.id);
      b.addEventListener('click', () => loadPreset(p));
      const fav = el('span', 'pb-fav' + (favorites.has(p.id) ? ' on' : ''), '★');
      fav.title = favorites.has(p.id) ? 'Remove from favourites' : 'Add to favourites';
      fav.addEventListener('click', (e) => { e.stopPropagation(); engine.setFavourite(p.id, !favorites.has(p.id)); });
      b.appendChild(fav);
      if (!p.factory) {
        const del = el('span', 'pb-del', '×');
        del.title = 'Delete preset';
        del.addEventListener('click', (e) => {
          e.stopPropagation();
          openModal({ title: 'Delete preset', body: `Move <b>${esc(p.name)}</b> to the Recycle Bin?`, ok: 'DELETE', danger: true, onOk: () => engine.deletePreset(p.id) });
        });
        b.appendChild(del);
      }
      gridEl.appendChild(b);
    }
  }
  const searchEl = $('#preset-search');
  searchEl.addEventListener('input', () => { search = searchEl.value; renderBrowser(); });
  searchEl.addEventListener('keydown', (e) => { e.stopPropagation(); if (e.key === 'Escape') { searchEl.value = ''; search = ''; renderBrowser(); searchEl.blur(); } });
  searchEl.addEventListener('keyup', (e) => e.stopPropagation());

  function savePrompt() {
    const isUser = current.id && !current.id.startsWith('factory:');
    openModal({
      title: 'Save preset',
      body: `Saved to <span class="path">${esc(presetFolder)}</span>. A category becomes a sub-folder.`,
      input: isUser ? current.name : `${current.name} (mine)`,
      input2: { label: 'Category', value: isUser && current.category !== 'User' ? current.category : '' },
      ok: 'SAVE',
      onOk: (name, cat) => { if (name.trim()) engine.savePreset(name.trim(), cat.trim(), false); },
    });
  }
  engine.on('saveResult', ({ ok, exists, name, error }) => {
    if (ok) { presetDirty = false; renderPresetName(); logMidi(`Saved preset "${name}"`); }
    else if (exists) {
      openModal({ title: 'Replace preset?', body: `A preset named <b>${esc(name)}</b> already exists. Replace it?`, ok: 'REPLACE', danger: true,
                  onOk: () => engine.savePreset(name, lastSaveCategory, true) });
    } else logMidi(`Save failed: ${error}`);
  });
  let lastSaveCategory = '';
  $('#preset-prev').addEventListener('click', () => stepPreset(-1));
  $('#preset-next').addEventListener('click', () => stepPreset(1));
  $('#preset-save').addEventListener('click', savePrompt);
  $('#preset-load').addEventListener('click', () => engine.loadPresetFile());
  $('#preset-export').addEventListener('click', () => engine.exportPreset());
  $('#preset-folder').addEventListener('click', () => engine.openPresetFolder());
  $('#preset-init').addEventListener('click', () => openModal({ title: 'Init', body: 'Set <b>every parameter</b> to its default and clear the tuning edits?', ok: 'INIT', onOk: () => engine.initPreset() }));
  $('#preset-fav').addEventListener('click', () => { if (current.id) engine.setFavourite(current.id, !favorites.has(current.id)); });

  // ------------------------------------------------------------- modal -----
  const modal = $('#modal'), modalInput = $('#modal-input'), modalInput2 = $('#modal-input2'), modalLabel2 = $('#modal-label2'), modalOk = $('#modal-ok');
  let modalOnOk = null;
  function openModal({ title, body, input, input2, ok, danger, onOk }) {
    $('#modal-title').textContent = title;
    $('#modal-body').innerHTML = body || '';
    modalOk.textContent = ok || 'OK';
    modalOk.classList.toggle('danger', !!danger);
    modalInput.style.display = input === undefined ? 'none' : '';
    modalInput.value = input || '';
    modalLabel2.style.display = modalInput2.style.display = input2 ? '' : 'none';
    if (input2) { modalLabel2.textContent = input2.label; modalInput2.value = input2.value || ''; }
    modalOnOk = onOk;
    modal.classList.remove('hidden');
    if (input !== undefined) { modalInput.focus(); modalInput.select(); } else modalOk.focus();
  }
  function closeModal(accept) {
    if (modal.classList.contains('hidden')) return;
    modal.classList.add('hidden');
    const fn = modalOnOk;
    modalOnOk = null;
    lastSaveCategory = modalInput2.value;
    if (accept && fn) fn(modalInput.value, modalInput2.value);
  }
  modalOk.addEventListener('click', () => closeModal(true));
  $('#modal-cancel').addEventListener('click', () => closeModal(false));
  modal.addEventListener('pointerdown', (e) => { if (e.target === modal) closeModal(false); });
  modal.addEventListener('keydown', (e) => {
    e.stopPropagation();
    if (e.key === 'Enter') { e.preventDefault(); closeModal(true); }
    else if (e.key === 'Escape') { e.preventDefault(); closeModal(false); }
  });
  modal.addEventListener('keyup', (e) => e.stopPropagation());

  // ============================================================ MAIN page ===
  const INSTRUMENT_NOTES = [
    'A 2.7 m concert grand: long bass strings, 700–1250 N tensions, eleven to four-gram hammers, a 48 Hz soundboard.',
    'A 1.9 m studio grand: shorter bass, a little more inharmonicity, a smaller, livelier board.',
    'A 1.2 m upright: short low-tension strings, high inharmonicity, a small stiff board, weak bass dampers.',
    'An old upright: worn hammers, uneven unisons, tired strings, a rattly action.',
    'Lacquered hammers and a stiff board that radiates the high partials well.',
    'Needled soft hammers and a warm board with strong low modes.',
    'A felt strip between hammers and strings: very soft, slow contact; high partials muffled.',
    'Objects on the strings: partials pushed off their stiff-string positions, uneven losses, buzzes.',
    'Wide, uneven unisons, hard worn hammers, a boxy board.',
    'Four-metre strings with almost no stiffness, a huge singing board, strike points near the middle.',
  ];
  {
    const row = $('#main-row');
    const inst = makePanel('INSTRUMENT');
    const bank = el('div', 'instrument-bank');
    const instBtns = INSTRUMENTS.map((name, i) => {
      const b = el('button', 'inst-btn', `<span class="inst-num">${String(i + 1).padStart(2, '0')}</span><span class="inst-name">${esc(name)}</span><span class="led"></span>`);
      b.type = 'button';
      b.dataset.param = 'instrument';
      b.addEventListener('click', () => setParam('instrument', i));
      bank.appendChild(b);
      return b;
    });
    inst.appendChild(bank);
    const instNote = el('div', 'panel-note', '');
    inst.appendChild(instNote);
    row.appendChild(inst);
    refreshers.push(() => {
      const i = clamp(Math.round(values.instrument), 0, 9);
      instBtns.forEach((b, k) => b.classList.toggle('selected', k === i));
      instNote.textContent = INSTRUMENT_NOTES[i];
    });

    const tone = makePanel('OUTPUT');
    tone.appendChild(el('div', 'screen stat-screen', `
      <div class="vs-row"><span class="vs-label">VOICES</span><span class="vs-value"><b id="voice-count">0</b>/<span id="poly-limit">64</span></span></div>
      <div class="meter"><div id="meter-fill" class="meter-fill"></div><div class="meter-clip"></div></div>
      <div class="vs-row"><span class="vs-label">CPU</span><span class="vs-value"><b id="cpu-pct">0</b>%</span></div>
      <div class="meter"><div id="cpu-fill" class="meter-fill cpu-fill"></div></div>`));
    const trow = el('div', 'knob-row');
    addKnob(trow, 'master', 'knob-lg');
    addKnob(trow, 'toneBass');
    addKnob(trow, 'toneTreble');
    addKnob(trow, 'stereoWidth');
    tone.appendChild(trow);
    row.appendChild(tone);

    const ch = makePanel('CHARACTER');
    note(ch, 'Hardness voices the felt (stiffness and exponent). String resonance is the coupling through the soundboard; board resonance sets the modes’ Q.');
    knobRow(ch, ['hammerHardness', 'stringResonance', 'soundboardResonance', 'reverbMix']);
    knobRow(ch, ['dynamics', 'unisonDetune', 'stringDecay', 'noiseMaster']);
    row.appendChild(ch);
  }

  // ========================================================= PHYSICS page ===
  {
    const row = $('#physics-row');
    const strings = makePanel('STRINGS');
    const partials = new PartialScreen(strings);
    knobRow(strings, ['inharmonicity', 'stringStiffness', 'stringDecay', 'unisonDetune', 'bridgeCoupling']);
    note(strings, 'fₙ = n·f₀·√((1+Bn²)/(1+B)). Stiffness scales the loss of the high partials, Decay the whole decay time, Bridge the local coupling (beating, two-stage decay).');
    row.appendChild(strings);
    refreshers.push(() => partials.update(values.inharmonicity, values.stringStiffness, values.stringDecay, values.hammerPosition, values.unisonDetune));

    const hammer = makePanel('HAMMER');
    const hs = new HammerScreen(hammer);
    knobRow(hammer, ['hammerPosition', 'hammerMass', 'feltStiffness', 'feltExponent']);
    note(hammer, 'Hunt–Crossley felt: F = K·ξᵖ·(1+λξ̇). Harder playing shortens the contact, so velocity changes the spectrum. Position is the strike ratio (≈ 1/8).');
    row.appendChild(hammer);
    refreshers.push(() => hs.update(values.hammerHardness, values.feltStiffness, values.feltExponent, values.hammerMass));

    const col = el('div', 'col');
    const board = makePanel('DAMPERS · SOUNDBOARD');
    knobRow(board, ['damperEfficiency', 'soundboardSize', 'soundboardDamping', 'soundboardBrightness']);
    note(board, 'Size scales the 96 soundboard modes (1/size), Damping their loss; the board’s admittance also sets part of every string’s decay. Brightness balances the modal body against the dense high-frequency radiation. Tables rebuild in the background: <span id="build-state">ready</span>.');
    col.appendChild(board);
    const mech = makePanel('MECHANICAL NOISES');
    knobRow(mech, ['noiseMaster', 'noiseHammer', 'noiseKeyRelease', 'noiseDamper', 'noisePedal']);
    note(mech, 'Synthesised, not sampled: the key bottoming out and the shank knock, the key return, the damper felt landing on a moving string, the pedal mechanism. Most of it goes through the soundboard and takes on its colour.');
    col.appendChild(mech);
    row.appendChild(col);
  }

  // ============================================================ MICS page ===
  {
    const row = $('#mics-row');
    const persp = makePanel('PERSPECTIVE');
    const diagram = new MicDiagram(persp);
    const seg = control(Segmented, 'micPerspective', ['Player', 'Close', 'Audience', 'Wide', 'Mono', 'Custom']);
    persp.appendChild(seg.el);
    knobRow(persp, ['micDistance', 'micSpread', 'micPosition', 'earlyReflections']);
    const micNote = el('div', 'panel-note', '');
    persp.appendChild(micNote);
    row.appendChild(persp);
    refreshers.push(() => {
      diagram.update(values.micPerspective, values.micDistance, values.micSpread, values.micPosition, values.stereoWidth);
      const custom = Math.round(values.micPerspective) === 5;
      micNote.textContent = custom ? 'Custom: distance sets direct level, air absorption and room; spread the pair separation; position shifts the pair along the bridge.'
        : 'The five fixed perspectives set distance, spread, orientation and the dense-mode radiation. Choose CUSTOM to use the Distance, Spread and Reflections knobs (Position applies always).';
      ['micDistance', 'micSpread', 'earlyReflections'].forEach(id => (knobs[id] || []).forEach(k => k.el.classList.toggle('dim', !custom)));
    });

    const room = makePanel('ROOM');
    const typeRow = el('div', 'labeled-row', '<span class="ctl-label">TYPE</span>');
    typeRow.appendChild(control(SelectBox, 'reverbType').el);
    room.appendChild(typeRow);
    knobRow(room, ['reverbMix', 'reverbSize', 'reverbDamp', 'reverbPredelay']);
    const irRow = el('div', 'switch-row');
    const irLoad = el('button', 'pd-btn small', 'LOAD IR');
    irLoad.type = 'button';
    irLoad.title = 'Load an impulse response (WAV/AIFF/FLAC) for the Impulse Response reverb type';
    irLoad.addEventListener('click', () => engine.loadIR());
    const irName = el('span', 'scala-status', 'no impulse response');
    const irClear = el('button', 'pd-btn small', 'CLEAR');
    irClear.type = 'button';
    irClear.addEventListener('click', () => engine.clearIR());
    irRow.append(irLoad, irName, irClear);
    room.appendChild(irRow);
    note(room, 'Room, Chamber, Hall and Stage are an 8-line feedback delay network with early reflections. Impulse Response convolves your own file (algorithmic Hall until one is loaded).');
    row.appendChild(room);
    engine.on('ir', ({ name }) => { irName.innerHTML = name ? `IR: <b>${esc(name)}</b>` : 'no impulse response'; });

    const out = makePanel('STEREO · TONE');
    knobRow(out, ['stereoWidth', 'toneBass', 'toneTreble']);
    knobRow(out, ['soundboardBrightness', 'master']);
    note(out, 'Width scales the side signal of the mic pair. The soundboard modes radiate to the two channels with their own weights, so the image comes from the model, not from panning alone.');
    row.appendChild(out);
  }

  // ===================================================== PERFORMANCE page ===
  {
    const row = $('#perf-row');
    const vel = makePanel('VELOCITY');
    const editor = new VelocityEditor(vel, (obj) => setParams(obj), (begin) => {
      ['dynamics', 'velCurve'].forEach(id => { if (begin) { dragging.add(id); engine.beginGesture(id); } else { dragging.delete(id); engine.endGesture(id); } });
    });
    knobRow(vel, ['dynamics', 'velCurve']);
    note(vel, 'Drag the curve: up/down bends it, left/right widens the dynamic range. The grey line is linear. Hammer speed runs from about 0.25 m/s to 6 m/s; the level and the spectrum both follow it.');
    row.appendChild(vel);
    refreshers.push(() => editor.set(values.dynamics, values.velCurve));

    const ped = makePanel('PEDALS · MIDI');
    const modeRow = el('div', 'labeled-row', '<span class="ctl-label">SUSTAIN</span>');
    modeRow.appendChild(control(Segmented, 'pedalMode').el);
    ped.appendChild(modeRow);
    knobRow(ped, ['halfPedalPoint', 'softPedalDepth', 'pitchBendRange', 'polyphony']);
    note(ped, 'CC64 sustain (continuous: the dampers lift around the half-pedal point), CC66 sostenuto, CC67 soft (una corda: the third string is only brushed). Polyphony is the voice budget; the quietest released voice is stolen first.');
    row.appendChild(ped);

    const stats = makePanel('STATISTICS');
    stats.appendChild(el('div', 'screen stat-screen', `
      <div class="vs-row"><span class="vs-label">VOICES</span><span class="vs-value"><b id="voice-count2">0</b></span></div>
      <div class="vs-row"><span class="vs-label">PARTIALS</span><span class="vs-value"><b id="partial-count2">0</b></span></div>
      <div class="vs-row"><span class="vs-label">CPU</span><span class="vs-value"><b id="cpu-pct2">0</b>%</span></div>
      <div class="meter"><div id="cpu-fill2" class="meter-fill cpu-fill"></div></div>
      <div class="vs-row"><span class="vs-label">PEDAL</span><span class="vs-value" id="pedal-info">up</span></div>
      <div class="vs-row"><span class="vs-label">RATE</span><span class="vs-value" id="rate-info">—</span></div>`));
    note(stats, 'CPU is the audio callback’s time against its budget. Every partial listed is a modal resonator running right now; partials that have died are dropped automatically.');
    row.appendChild(stats);
  }

  // ========================================================== TUNING page ===
  {
    const row = $('#tuning-row');
    const temp = makePanel('TEMPERAMENT');
    const r1 = el('div', 'labeled-row', '<span class="ctl-label">SCALE</span>');
    r1.appendChild(control(SelectBox, 'temperament').el);
    const r2 = el('div', 'labeled-row', '<span class="ctl-label">ROOT</span>');
    r2.appendChild(control(SelectBox, 'temperamentRoot').el);
    temp.append(r1, r2);
    knobRow(temp, ['concertPitch', 'stretch']);
    const sclRow = el('div', 'switch-row');
    const sclLoad = el('button', 'pd-btn small', 'LOAD .SCL');
    sclLoad.type = 'button';
    sclLoad.addEventListener('click', () => engine.loadScala());
    const sclClear = el('button', 'pd-btn small', 'CLEAR');
    sclClear.type = 'button';
    sclClear.addEventListener('click', () => engine.clearScala());
    const sclStatus = el('span', 'scala-status', 'no Scala scale');
    sclRow.append(sclLoad, sclStatus, sclClear);
    temp.appendChild(sclRow);
    note(temp, 'Stretch follows a Railsback curve (about −28 ct at A0, +33 ct at C8 when fully on). A Scala file sets the Custom temperament; its root is in the C4 octave.');
    row.appendChild(temp);

    const det = makePanel('PER-NOTE DETUNE');
    const reset = el('button', 'pd-btn small', 'RESET');
    reset.type = 'button';
    reset.addEventListener('click', () => engine.resetDetune());
    det.querySelector('.panel-head').appendChild(reset);
    let sendTimer = 0, pendingDetune = null;
    const detune = new DetuneEditor(det, (i, cents) => {
      pendingDetune = { i, cents };
      if (!sendTimer) sendTimer = setTimeout(() => { sendTimer = 0; if (pendingDetune) engine.setDetune(pendingDetune.i, pendingDetune.cents); pendingDetune = null; }, 40);
      if (!presetDirty) { presetDirty = true; renderPresetName(); }
    });
    note(det, 'Offsets in cents per key, on top of the temperament and the stretch. Saved with presets and projects.');
    row.appendChild(det);
    engine.on('tuning', ({ detune: arr, scala, scalaNotes, valid }) => {
      detune.setAll(arr || []);
      sclStatus.innerHTML = valid ? `Scala: <b>${esc(scala || 'scale')}</b> (${scalaNotes} notes)` : 'no Scala scale';
    });
  }

  // ========================================================== page tabs ===
  const pageTabs = Array.from(document.querySelectorAll('.page-tab'));
  const pageEls = Array.from(document.querySelectorAll('.page'));
  const PAGE_HINTS = {
    main: 'Instrument · output · character',
    physics: 'Strings · hammer · dampers · soundboard · mechanical noises (tables rebuild in the background)',
    mics: 'Virtual microphones · room · stereo',
    perf: 'Velocity · pedals · polyphony · statistics',
    tuning: 'Temperaments · concert pitch · stretch · Scala · per-note detune',
  };
  let activePage = 'main';
  try { activePage = localStorage.getItem('resonique.page') || 'main'; } catch (e) { /* storage blocked */ }
  function showPage(p) {
    if (!PAGE_HINTS[p]) p = 'main';
    activePage = p;
    pageEls.forEach(pg => pg.classList.toggle('active', pg.dataset.page === p));
    pageTabs.forEach(t => { t.classList.toggle('active', t.dataset.page === p); t.setAttribute('aria-selected', t.dataset.page === p); });
    $('#page-hint').textContent = PAGE_HINTS[p];
    try { localStorage.setItem('resonique.page', p); } catch (e) { /* storage blocked */ }
  }
  pageTabs.forEach(t => t.addEventListener('click', () => showPage(t.dataset.page)));

  // ---------------------------------------------------------- notes / UI ---
  const monitor = $('#midi-monitor');
  const midiLed = $('#midi-led');
  let ledTimer = 0;
  function logMidi(text) { monitor.textContent = text; }
  function flashLed() {
    midiLed.classList.add('on');
    clearTimeout(ledTimer);
    ledTimer = setTimeout(() => midiLed.classList.remove('on'), 90);
  }

  const piano = new Piano($('#piano'), { numKeys: 88, baseNote: 21, onNoteOn: (n, v) => noteOn(n, v), onNoteOff: (n) => noteOff(n) });
  function noteOn(note, vel) { engine.noteOn(note, vel); piano.setNote(note, true); }
  function noteOff(note) { engine.noteOff(note); piano.setNote(note, false); }
  engine.on('notes', ({ on, off }) => {
    off.forEach(n => piano.setNote(n, false));
    on.forEach(n => piano.setNote(n, true));
    flashLed();
    if (on.length) logMidi(`NOTE ON   ${on.map(n => `${midiNoteName(n)} (${n})`).join('  ')}`);
    else if (off.length) logMidi(`NOTE OFF  ${off.map(n => `${midiNoteName(n)} (${n})`).join('  ')}`);
  });

  // pedals
  const pedalsEl = $('#pedals');
  const pedalBtns = {};
  for (const [label, cc] of [['SOFT', 67], ['SOST', 66], ['SUST', 64]]) {
    const b = new PedalButton(label, cc, (c, v) => { engine.pedal(c, v); b.setValue(v / 127); });
    pedalBtns[cc] = b;
    pedalsEl.insertBefore(b.el, pedalsEl.firstChild);
  }

  // Computer keyboard: two rows, A = C; Z / X shift the octave
  const KEYMAP = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12, o: 13, l: 14, p: 15, ';': 16, "'": 17 };
  const heldKeys = new Map();
  let keyBase = 60;
  window.addEventListener('keydown', (e) => {
    if (e.repeat || e.ctrlKey || e.metaKey || e.altKey) return;
    if (/^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName) || !modal.classList.contains('hidden')) return;
    const key = e.key.toLowerCase();
    if (key === 'z') { keyBase = clamp(keyBase - 12, 24, 96); logMidi(`Keyboard octave: ${midiNoteName(keyBase)}`); return; }
    if (key === 'x') { keyBase = clamp(keyBase + 12, 24, 96); logMidi(`Keyboard octave: ${midiNoteName(keyBase)}`); return; }
    if (key === ' ') { e.preventDefault(); engine.pedal(64, pedalBtns[64].value >= 0.5 ? 0 : 127); return; }
    if (key in KEYMAP && !heldKeys.has(key)) {
      const note = keyBase + KEYMAP[key];
      if (note < 21 || note > 108) return;
      heldKeys.set(key, note);
      noteOn(note, 96);
    }
  });
  window.addEventListener('keyup', (e) => {
    const key = e.key.toLowerCase();
    if (heldKeys.has(key)) { noteOff(heldKeys.get(key)); heldKeys.delete(key); }
  });
  window.addEventListener('blur', () => { for (const [, n] of heldKeys) noteOff(n); heldKeys.clear(); });

  $('#panic-btn').addEventListener('click', () => {
    engine.panic();
    piano.clear();
    heldKeys.clear();
    logMidi('PANIC — all voices killed');
  });

  // ------------------------------------------------------------- stats -----
  const meterFill = $('#meter-fill'), meterClip = document.querySelector('.meter-clip');
  let clipTimer = 0;
  engine.on('stats', ({ voices, partials, peak, sustain, sostenuto, soft, cpu, building, sampleRate }) => {
    $('#voice-count').textContent = voices;
    $('#voice-count2').textContent = voices;
    $('#partial-count2').textContent = partials;
    $('#poly-limit').textContent = Math.round(values.polyphony);
    const pct = Math.round(clamp(cpu, 0, 2) * 100);
    $('#cpu-pct').textContent = pct; $('#cpu-pct2').textContent = pct;
    $('#cpu-fill').style.width = `${clamp(pct, 0, 100)}%`;
    $('#cpu-fill2').style.width = `${clamp(pct, 0, 100)}%`;
    $('#build-state').textContent = building ? 'rebuilding…' : 'ready';
    $('#rate-info').textContent = sampleRate > 0 ? `${(sampleRate / 1000).toFixed(1)} kHz` : '—';
    $('#sustain-led').classList.toggle('on', sustain >= 0.5);
    $('#sustain-led').classList.toggle('half', sustain > 0.05 && sustain < 0.5);
    $('#sost-led').classList.toggle('on', !!sostenuto);
    $('#soft-led').classList.toggle('on', soft >= 0.5);
    pedalBtns[64].setValue(sustain); pedalBtns[66].setValue(sostenuto ? 1 : 0); pedalBtns[67].setValue(soft);
    $('#pedal-info').textContent = sustain >= 0.5 ? 'sustain' : sustain > 0.05 ? `half (${Math.round(sustain * 100)}%)` : 'up';
    const db = 20 * Math.log10(Math.max(peak, 1e-5));
    meterFill.style.width = `${clamp((db + 60) / 60, 0, 1) * 100}%`;
    if (peak > 0.95) {
      meterClip.classList.add('on');
      clearTimeout(clipTimer);
      clipTimer = setTimeout(() => meterClip.classList.remove('on'), 800);
    }
  });

  let connectedOnce = false;
  engine.on('config', ({ sampleRate, blockSize, wrapper, version }) => {
    const sr = sampleRate > 0 ? `${(sampleRate / 1000).toFixed(1)} kHz · ${blockSize} smp` : 'not playing yet';
    $('#audio-info').textContent = `${wrapper} · ${sr}`;
    $('#audio-led').classList.add('on');
    if (version) $('#brand-sub').textContent = `PHYSICAL PIANO · v${version}`;
    if (!connectedOnce) logMidi('Connected — play your MIDI controller, the keys, or A–\' on the computer keyboard (space = sustain)');
    connectedOnce = true;
    fit();
  });

  // --------------------------------------------------------- fit window ----
  const synth = $('.synth');
  function fit() {
    const vw = window.innerWidth, vh = window.innerHeight;
    const w = synth.offsetWidth + 20, h = synth.offsetHeight + 30;
    const s = Math.min(vw / w, vh / h);
    synth.style.transform = `translateX(-50%) scale(${s})`;
  }
  window.addEventListener('resize', fit);

  // Right-click on any control = the host's parameter menu; Shift+right-click keeps the browser menu (DevTools)
  document.addEventListener('contextmenu', (e) => {
    if (!engine.connected || e.shiftKey) return;
    e.preventDefault();
    const ctl = e.target.closest('[data-param]');
    if (ctl) engine.paramMenu(ctl.dataset.param, e.clientX, e.clientY);
  });

  // -------------------------------------------------------------- init -----
  renderPresetName();
  renderBrowser();
  showPage(activePage);
  refreshAll(null);
  fit();
  if (engine.connected) engine.uiReady();
  else logMidi('Not running inside the plugin — no audio engine connected (UI preview only)');
})();
