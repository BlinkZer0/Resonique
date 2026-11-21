// ============================================================================
//  Browser stand-in for the JUCE bridge, so the UI can be opened and
//  screenshotted outside the plugin (tests/run-ui-tests.ps1). Installed as
//  window.__JUCE__ before the UI scripts load; feeds a config, the factory
//  preset list, a preset, tuning, and periodic stats.
// ============================================================================
(function () {
  'use strict';
  const listeners = {};
  const backend = {
    addEventListener: (ev, fn) => { (listeners[ev] = listeners[ev] || []).push(fn); },
    emitEvent: (id, payload) => { setTimeout(() => handle(id, payload || {}), 0); },
  };
  const emit = (ev, data) => (listeners[ev] || []).forEach(fn => fn(data));
  window.__JUCE__ = { backend };

  const presets = window.__PREVIEW_PRESETS__ || [];
  const values = {};
  let current = presets[0] || { id: 'factory:0', name: 'Concert Grand', category: 'Concert Grands', description: '' };

  function handle(id, p) {
    switch (id) {
      case 'uiReady':
        emit('config', { sampleRate: 48000, blockSize: 256, wrapper: 'Preview', voices: 128, version: '0.2.0' });
        emit('presetList', { presets, folder: 'C:\\Users\\you\\Documents\\Resonique\\Presets', favorites: ['factory:3'] });
        emit('preset', current);
        emit('tuning', { detune: new Array(88).fill(0).map((_, i) => (i % 17 === 0 ? 6 : 0)), scala: '', scalaNotes: 0, valid: false });
        emit('ir', { name: '' });
        emit('params', values);
        break;
      case 'setParam': values[p.id] = p.value; break;
      case 'setParams': Object.assign(values, p.values); break;
      case 'loadPreset':
        current = presets.find(x => x.id === p.id) || current;
        if (current.params) { Object.assign(values, current.params); emit('params', current.params); }
        emit('preset', current);
        break;
      case 'noteOn': emit('notes', { on: [p.note], off: [] }); break;
      case 'noteOff': emit('notes', { on: [], off: [p.note] }); break;
      default: break;
    }
  }

  let t = 0;
  setInterval(() => {
    t += 1;
    emit('stats', { voices: 6, partials: 412, peak: 0.25 + 0.2 * Math.sin(t / 3), sustain: (t % 60) < 30 ? 1 : 0, sostenuto: false, soft: 0,
                    cpu: 0.11, building: false, sampleRate: 48000 });
  }, 100);
})();
